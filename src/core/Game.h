#pragma once
// 101 Okey game engine: deck, dealing, turn flow, opening, işleme, penalties, scoring, match.
// PUBLIC API FROZEN. Implementation: src/core/Game.cpp (engine owner). The engine owner may add
// PRIVATE members/helpers below; nothing else may change without the architect.
//
// Seats: 0 = bottom (human), 1 = right, 2 = top, 3 = left. Play goes 0 -> 1 -> 2 -> 3 -> 0
// (counter-clockwise, "sağdan"). A player discards to their right; the NEXT player (rightOf) may take
// that tile "from the left" (yandan almak). So seat s may take the top of leftOf(s)'s discard pile.
//
// The engine is pure logic: no raylib, no I/O, deterministic for a given seed.
#include "core/Meld.h"
#include "core/Rng.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace okey {

struct RulesConfig {
    int numHands = 5;                  // match length (el sayısı)
    int openThreshold = 101;           // minimum series (run/group) total to open
    int minPairsToOpen = 5;            // minimum pairs to open with pairs (çift açmak)
    int penalty = 101;                 // one penalty (ceza)
    int unopenedScore = 202;           // hand score of a player who never opened
    int winnerScore = -101;            // base hand score of the finisher (multiplied)
    bool waitTurnAfterOpening = true;  // no işleme / new melds / joker swap in the turn you opened
    bool penaltyJokerDiscard = true;   // discarding the okey (except as the finishing tile)
    bool penaltyPlayableDiscard = true;// discarding a tile that fits a table meld ("işlek taş")
    bool penaltyReturnLeft = false;    // giving a tile taken from the left back costs a penalty (off: free)
    // Yandan alıp açma: opening with the tile taken from the left writes that tile's number x10 (series
    // opening) or x20 (pair opening) on the player who discarded it.
    bool leftOpenPenalty = true;
    // Katlamalı oyun: after a series opening you must open series with at least one more than it (116 ->
    // 117), after a pair opening with at least one more pair (5 -> 6). See seriesOpenNeed / pairsOpenNeed.
    bool katlamali = false;
};

struct PlayerInfo {
    std::string name;
    bool human = false;
    std::vector<int> hand;          // tile ids, unordered (UI keeps its own rack layout)
    std::vector<int> discards;      // this player's discard pile, back() = top (takeable by rightOf)
    bool opened = false;
    bool openedWithPairs = false;
    int openedTurn = -1;            // Game::turnNumber() when this player opened
    int openValue = 0;              // series total or number of pairs at opening
    int handPenalty = 0;            // penalties accumulated in the current hand
    int totalScore = 0;             // cumulative match score (lower is better)
    std::vector<int> handScores;    // score of each finished hand (incl. penalties)
};

enum class TurnStage { NeedDraw, Play }; // Play: may open/lay/işle/swap, must end by discarding
enum class HandState { NotStarted, Playing, HandOver, MatchOver };
enum class HandEndReason { None, PlayerFinished, PileExhausted };

struct HandResult {
    HandEndReason reason = HandEndReason::None;
    int winner = -1;
    bool finishedWithJoker = false;  // last discard was the okey (okeyle bitiş)
    bool finishedWithPairs = false;  // winner had opened with pairs (çiftten bitiş)
    bool finishedInOneGo = false;    // winner opened and finished in the same turn (elden bitiş)
    int multiplier = 1;
    std::array<int, 4> remaining{};  // points left in hand (before multipliers)
    std::array<int, 4> penalties{};  // penalties this hand
    std::array<int, 4> score{};      // final hand score written to the sheet (incl. penalties)
};

enum class EvType {
    MatchStart,  // new match
    HandStart,   // dealt; amount = hand index; tile = indicator id
    TurnStart,   // player = seat to move
    DrawPile,    // player drew `tile` from the pile (UI must hide the face for non-human seats)
    TakeLeft,    // player took `tile` from leftOf(player)'s discards
    ReturnLeft,  // player gave `tile` back (penalty follows)
    Open,        // player opened; meld = first new table meld index, count = melds, amount = value or pairs
    LayMelds,    // opened player laid more melds; meld = first index, count = number
    AddToMeld,   // player added `tile` to table meld `meld`
    SwapJoker,   // player put `tile` into meld `meld` and took the joker (amount = joker id)
    Discard,     // player discarded `tile`
    Penalty,     // player got `amount` penalty; text = reason
    HandEnd,     // see lastHandResult(); player = winner or -1
    MatchEnd     // player = seat with the lowest total
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int player = -1;
    int tile = -1;
    int meld = -1;
    int count = 0;
    int amount = 0;
    std::string text; // short Turkish description, e.g. "Hacı Rıza eli açtı (124)"
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing, e.g. "Açmak için en az 101 gerekli (şu an 87)"
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// Pre-validation of an opening / laying attempt (for UI live counters and bots).
struct OpenCheck {
    bool valid = false;  // would openHand()/layMelds() accept these groups right now?
    bool pairs = false;  // interpreted as pairs
    int value = 0;       // series total (or 0 for pairs)
    int pairCount = 0;
    std::string error;
};

struct DiscardRecord { int player; int tile; };

class Game {
public:
    explicit Game(const RulesConfig& cfg = RulesConfig());

    // ---- setup ----
    void setRules(const RulesConfig& cfg);             // only between matches
    const RulesConfig& rules() const { return cfg_; }
    void setPlayer(int seat, const std::string& name, bool human);
    void startMatch(uint64_t seed);                    // resets scores; deals hand 0 (HandStart event)
    void startNextHand();                              // after HandOver (not MatchOver)

    // ---- queries (all public information except player(seat).hand of other seats — bots must only
    //      read their own hand; the UI only shows the human hand) ----
    HandState handState() const { return handState_; }
    int handIndex() const { return handIndex_; }       // 0-based
    int numHands() const { return cfg_.numHands; }
    int current() const { return current_; }
    TurnStage stage() const { return stage_; }
    int turnNumber() const { return turnNumber_; }     // increments at every TurnStart
    int starter() const { return starter_; }           // seat that began this hand with 22 tiles
    const OkeyInfo& okey() const { return okey_; }
    int pileCount() const { return (int)pile_.size(); }
    const PlayerInfo& player(int seat) const { return players_[seat]; }
    const std::vector<Meld>& table() const { return table_; }
    const std::vector<DiscardRecord>& discardHistory() const { return discardHistory_; }
    const HandResult& lastHandResult() const { return lastResult_; }
    static int leftOf(int seat) { return (seat + 3) % 4; }   // previous player: I take their discards
    static int rightOf(int seat) { return (seat + 1) % 4; }  // next player: takes my discards
    int topDiscard(int seat) const;                    // top of seat's discard pile, -1 if empty
    int pendingLeftTile() const { return pendingLeftTile_; } // tile taken from left this turn that
                                                       // still has to be used on the table, else -1
    bool openedThisTurn(int seat) const;               // opened during the current turn
    bool canTakeFromLeft(int seat) const;              // stage/turn/pile checks only (not usefulness)
    bool canWorkTable(int seat) const;                 // may lay/işle/swap now (opened & not same turn)
    bool isPlayableOnTable(int tile) const;            // "işlek": fits some table run/group
    int handPoints(int seat) const;                    // face numbers left in hand (okeys not counted)
    int jokersInHand(int seat) const;                  // okeys left in hand (each +penalty at hand end if opened)
    bool pairsOpenedByOther(int seat) const;           // someone else opened with pairs (seri açan çift açabilir)
    // What an opening needs right now: openThreshold / minPairsToOpen, or in a katlamalı game one more than
    // the highest series opening / pair count already on the table this hand.
    int seriesOpenNeed() const;
    int pairsOpenNeed() const;
    OpenCheck checkOpen(int seat, const std::vector<std::vector<int>>& groups) const;
    OpenCheck checkLay(int seat, const std::vector<std::vector<int>>& groups) const;
    int leaderSeat() const;                            // lowest total score (ties: lower seat)

    // ---- actions (only valid for seat == current()) ----
    ActionResult drawFromPile(int seat);               // NeedDraw -> Play
    ActionResult takeFromLeft(int seat);               // NeedDraw -> Play, sets pendingLeftTile
    ActionResult returnLeftTile(int seat);             // give pending tile back, +penalty, -> NeedDraw
                                                       // (cannot take from left again this turn)
    // First opening. Each inner vector is one meld in display order (see makeMeld). All series, or all
    // pairs. Series total >= seriesOpenNeed() or pairs >= pairsOpenNeed(). Must include pendingLeftTile if
    // any. Must leave >= 1 tile in hand.
    ActionResult openHand(int seat, const std::vector<std::vector<int>>& groups);
    // Additional melds after opening (canWorkTable). Series openers lay series, pair openers lay pairs.
    ActionResult layMelds(int seat, const std::vector<std::vector<int>>& groups);
    ActionResult addToMeld(int seat, int tile, int meldIndex, AddSide side = AddSide::Auto);
    ActionResult swapJoker(int seat, int tile, int meldIndex);
    // Ends the turn. Rejected while pendingLeftTile is unused. Empty hand after discard = finish.
    // After the discard: if the pile is empty the hand ends (PileExhausted).
    ActionResult discard(int seat, int tile);

    // ---- events (UI animation/sound/banter; bots may observe) ----
    std::vector<GameEvent> drainEvents();              // returns and clears the queue

    // ---- testing hooks: tests/ and tools/ only (never UI or bots). They let a test build any
    //      position; the caller is responsible for keeping the 106 tiles consistent. ----
    PlayerInfo& debugPlayer(int seat) { return players_[seat]; }
    std::vector<int>& debugPile() { return pile_; }
    std::vector<Meld>& debugTable() { return table_; }
    void debugSetOkey(int indicatorId) { okey_ = OkeyInfo::fromIndicator(indicatorId); }
    void debugSetTurn(int seat, TurnStage stage) { current_ = seat; stage_ = stage; handState_ = HandState::Playing;
                                                   pendingLeftTile_ = -1; tookLeftThisTurn_ = false;
                                                   returnedLeftThisTurn_ = false; ++turnNumber_; }

private:
    // --- engine owner may add members/helpers here ---
    void dealHand();
    void beginTurn(int seat);
    void endHand(HandEndReason reason, int winner, bool finishedWithJoker);
    void push(GameEvent e);
    ActionResult checkTurn(int seat, TurnStage need) const;
    bool removeFromHand(int seat, int tile);
    bool handHas(int seat, int tile) const;
    bool buildGroups(int seat, const std::vector<std::vector<int>>& groups, bool pairMode,
                     std::vector<Meld>& out, std::string& err) const;
    // Shared validation of openHand/layMelds (laying = layMelds). Fills `melds` when valid.
    OpenCheck evaluate(int seat, const std::vector<std::vector<int>>& groups, bool laying,
                       std::vector<Meld>* melds) const;
    // "<name> <third-person verb>", or just the capitalised second-person phrase for the human seat.
    std::string says(int seat, const std::string& third, const std::string& second) const;
    bool isSen(int seat) const;
    void addPenalty(int seat, const std::string& text);
    void addPenalty(int seat, const std::string& text, int amount);

    RulesConfig cfg_;
    std::array<PlayerInfo, 4> players_;
    std::vector<int> pile_;          // back() = next tile drawn
    std::vector<Meld> table_;
    std::vector<DiscardRecord> discardHistory_;
    OkeyInfo okey_;
    Rng rng_;
    HandState handState_ = HandState::NotStarted;
    int handIndex_ = 0;
    int firstStarter_ = 0;
    int starter_ = 0;
    int current_ = 0;
    TurnStage stage_ = TurnStage::NeedDraw;
    int turnNumber_ = 0;
    int pendingLeftTile_ = -1;
    bool tookLeftThisTurn_ = false;
    bool returnedLeftThisTurn_ = false;
    HandResult lastResult_;
    std::vector<GameEvent> events_;
};

} // namespace okey
