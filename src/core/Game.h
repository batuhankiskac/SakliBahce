#pragma once
// Okey game engine: 101 (tekli / eşli) and klasik okey. Deck, dealing, turn flow, opening, işleme, penalties,
// scoring, match. Klasik okey (RulesConfig::variant == Variant::Okey) uses the same deck, turn flow and events
// without the table: 14 tiles each (the starter 15), and a hand is finished with finishHand().
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

enum class Variant { Yuzbir, Okey }; // 101, klasik (düz) okey
// 101 kuralları — bitiş katları: how the finish multipliers (okeyle, çiftten, elden bitiş: x2 each) combine.
// Stack ("katlanır", the default): they multiply, up to x8. Single ("tek kat"): any special finish doubles once.
// None ("katsız"): no finish multipliers (a pair opener's leftover still counts double). docs/kurallar_101.md.
enum class FinishMult { Stack = 0, Single = 1, None = 2 };

struct RulesConfig {
    Variant variant = Variant::Yuzbir;
    // Eşli 101: partners sit across (0 & 2, 1 & 3). When one finishes, the partner's hand is not counted (their
    // written penalties stay); the match is won by the team with the lower combined total.
    bool teams = false;
    int numHands = 5;                  // match length (el sayısı; 101 only — klasik okey plays until someone hits 0)
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
    // 101 kuralları: how the finish multipliers combine (see FinishMult; default: they stack, today's rule).
    FinishMult finishMult = FinishMult::Stack;
    // Klasik okey: everyone starts with okeyStartPoints and counts down; a finish takes okeyFinishPoints from each
    // other player (x2 finishing by discarding the okey, x2 with seven pairs), showing the gösterge takes
    // okeyIndicatorPoints from each other player. The game ends when somebody reaches 0; the highest total wins.
    int okeyStartPoints = 20;
    int okeyFinishPoints = 2;
    int okeyIndicatorPoints = 1;
    // Renkli okey: a red or black gösterge doubles everything of that hand (finishes and the gösterge's point).
    bool okeyColorDouble = false;
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
    int handPenalty = 0;            // penalties accumulated in the current hand (klasik okey: points lost to a gösterge)
    int totalScore = 0;             // cumulative match score (101: lower is better; klasik okey: points left, higher
                                    // is better)
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
    int indicatorShownBy = -1;       // klasik okey: who showed the gösterge this hand
    int multiplier = 1;
    std::array<int, 4> remaining{};  // points left in hand (before multipliers)
    std::array<int, 4> penalties{};  // penalties this hand
    std::array<int, 4> score{};      // final hand score written to the sheet (incl. penalties; klasik okey: the points
                                     // taken off, <= 0)
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
    MatchEnd,    // player = seat with the lowest total (klasik okey: highest; eşli: a seat of the winning team)
    ShowIndicator // klasik okey: player showed the gösterge's twin (`tile`); amount = points taken from each other
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

// One successful action of the match, as given to Game (a saved match is its seed plus these, replayed).
enum class LogKind { Draw, TakeLeft, ReturnLeft, Open, Lay, Add, Swap, Discard, Finish, ShowIndicator, NextHand };
struct LoggedAction {
    LogKind kind = LogKind::Draw;
    int seat = -1;
    int tile = -1;
    int meld = -1;
    int side = 0;                        // AddSide
    std::vector<std::vector<int>> melds; // Open / Lay
    std::string encode() const;          // one text line
    static bool decode(const std::string& line, LoggedAction& out);
};

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
    int leaderSeat() const;                            // lowest total score (ties: lower seat); klasik okey: highest;
                                                       // eşli: the lower seat of the team with the lower total
    bool classic() const { return cfg_.variant == Variant::Okey; }
    bool teams() const { return cfg_.teams && !classic(); }
    static int partnerOf(int seat) { return (seat + 2) % 4; }
    int teamTotal(int seat) const { return players_[seat].totalScore + players_[partnerOf(seat)].totalScore; }
    // ---- klasik okey ----
    // The gösterge's twin in `seat`'s hand if they may show it now (their turn, before their first discard of the
    // hand, nobody showed yet), else -1.
    int indicatorTwin(int seat) const;
    int indicatorShownBy() const { return indicatorShownBy_; }
    // Would discarding `tile` finish the hand now? 0 no, 1 with sets, 2 with seven pairs.
    int finishKind(int seat, int tile) const;
    // Renkli okey: this hand's points count double (the rule is on and the gösterge is red or black): 2, else 1.
    int colorMultiplier() const;
    // 101 kuralları: the hand multiplier of a finish with these properties under rules().finishMult (Stack: the
    // product of x2 per property; Single: x2 if any; None: 1). Bots, the analysis and the sheet use it.
    int finishMultiplier(bool withJoker, bool withPairs, bool inOneGo) const;
    // The multiplier of every finished hand of this match, in order (1 = no kat).
    const std::vector<int>& handMultipliers() const { return handMults_; }

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
    // Klasik okey: put `tile` down and show the other 14 (sets or seven pairs) — the hand is finished. Finishing
    // with the okey as that tile ("okey atarak") doubles, seven pairs double.
    ActionResult finishHand(int seat, int tile);
    ActionResult showIndicator(int seat);              // klasik okey: show the gösterge's twin

    // ---- save / resume: the seed of startMatch and every successful action since (startNextHand included) ----
    uint64_t matchSeed() const { return matchSeed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a); // applies one logged action (false: it does not apply here)

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
    ActionResult logged(ActionResult r, LoggedAction a);
    ActionResult drawFromPileImpl(int seat);
    ActionResult takeFromLeftImpl(int seat);
    ActionResult returnLeftTileImpl(int seat);
    ActionResult openHandImpl(int seat, const std::vector<std::vector<int>>& groups);
    ActionResult layMeldsImpl(int seat, const std::vector<std::vector<int>>& groups);
    ActionResult addToMeldImpl(int seat, int tile, int meldIndex, AddSide side);
    ActionResult swapJokerImpl(int seat, int tile, int meldIndex);
    ActionResult discardImpl(int seat, int tile);
    ActionResult finishHandImpl(int seat, int tile);
    ActionResult showIndicatorImpl(int seat);
    void dealHand();
    void beginTurn(int seat);
    void endHand(HandEndReason reason, int winner, bool finishedWithJoker);
    void endClassicHand(HandEndReason reason, int winner, bool finishedWithJoker, bool pairs);
    void pushMatchEnd();
    ActionResult yuzbirOnly() const;
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
    int indicatorShownBy_ = -1;
    HandResult lastResult_;
    std::vector<GameEvent> events_;
    uint64_t matchSeed_ = 0;
    std::vector<LoggedAction> log_;
    std::vector<int> handMults_; // (101 kuralları) multiplier of each finished hand
};

} // namespace okey
