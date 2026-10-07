#pragma once
// Konken (kahvehane usulü) — four-player rummy engine with two 52-card decks and four jokers. Pure logic: no raylib,
// no I/O, deterministic for a given seed. Rules: docs/kurallar_konken.md. Bots: src/core/KonkenBot.h.
//
// Cards: ids 0..103 are two decks of kart ids (id % 52 is the face: suit = face / 13, rank = face % 13 + 2, 2..14,
// 14 = As), ids 104..107 are the jokers. Seats as in the other card games: 0 = the human (bottom), play goes
// 0 -> 1 -> 2 -> 3 ("sağdan").
//
// A hand: the starter's left neighbour deals 14 cards to everyone, the next card is turned up to start the discard pile
// (the middle, "yer"), the rest is the face-down stock ("deste"). On your turn:
//   1. Draw: the top of the stock, or the top of the discard pile — a discard taken must be used on the table this
//      turn (in the opening, a new meld or added to a meld); if you can't, give it back (returnDiscard) and draw.
//   2. Play (optional): open (melds worth at least Rules::openMin in one go), and once opened lay more melds, add cards
//      to any meld on the table (işlemek) and take a joker off the table by putting the card it stands for in its place.
//   3. Discard one card (never a joker unless it is your last card). A hand that becomes empty ends the hand: by the
//      last discard or by laying the last card (no discard then).
// Melds ("per"): a set ("grup", 3-4 cards of one rank, all suits different) or a run ("seri", 3+ cards of one suit in
// sequence; the As is low in A-2-3 and high in Q-K-A, no K-A-2). Jokers stand in for any card; a meld needs at least two
// real cards. Scoring (low is good): the finisher writes 0, an opened player the points in hand (As 11, figures 10, the
// rest their number, a joker 25), a player who never opened Rules::unopenedPoints. Konken (elden bitmek: opening and
// finishing in the same turn) doubles everybody else's points. A player whose total reaches Rules::limit "yanar"
// (burns). Rules::lastStanding (the default, "son kalan"): he leaves the table, the others play on (4 -> 3 -> 2: the
// deal, the turn order and the discards skip him) until one is left; the order they burned in gives the standings.
// Otherwise ("ilk yanan") the match ends after the first burn and the lowest total wins.
//
// Typical UI loop:
//   g.startMatch(seed);                       // deals hand 0, stage Draw for the starter
//   while (g.stage() != Stage::MatchOver) {
//       Draw:  g.drawStock(s) / g.takeDiscard(s)
//       Play:  g.layMelds(s, melds) / g.addToMeld(s, card, m) / g.swapJoker(s, card, m) / g.returnDiscard(s) /
//              g.discard(s, card)
//       HandOver: show g.sheet(), then g.startNextHand()
//       drain g.drainEvents() every frame for animations / texts
//   }
#include "core/Cards.h"
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace konken {

using okey::Rng;

// ---------------------------------------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------------------------------------

constexpr int NUM_FACES = 52;
constexpr int NUM_JOKERS = 4;
constexpr int FIRST_JOKER = 2 * NUM_FACES;            // 104
constexpr int NUM_CARDS = FIRST_JOKER + NUM_JOKERS;   // 108

inline bool isValidCard(int c) { return c >= 0 && c < NUM_CARDS; }
inline bool isJoker(int c) { return c >= FIRST_JOKER && c < NUM_CARDS; }
inline int faceOf(int c) { return c % NUM_FACES; }    // the kart id (naturals only)
inline int suitOf(int c) { return kart::suitOf(c % NUM_FACES); }
inline int rankOf(int c) { return kart::rankOf(c % NUM_FACES); } // 2..14
std::string cardNameTR(int c);       // "Kupa 7", "Maça As", "Joker"
std::string cardAccusativeTR(int c); // "Kupa 7'yi", "Maça Ası", "Jokeri"
// What a card left in hand costs (As 11, Vale/Kız/Papaz 10, the rest its number, a joker `jokerPoints`).
int handPoints(int c, int jokerPoints = 25);
// What a card counts in a meld when it stands for `rank` (1 = the low As: 1; 14 = the high As: 11; figures 10).
int rankValue(int rank);

// ---------------------------------------------------------------------------------------------------------
// Rules (defaults = the game in docs/kurallar_konken.md)
// ---------------------------------------------------------------------------------------------------------

struct Rules {
    int handSize = 14;        // cards dealt to everyone
    int openMin = 51;         // the opening must be worth at least this much (Ayarlar: 40, 51, 71)
    int limit = 151;          // a total that reaches this burns ("yanar"; Ayarlar: 101, 151, 201)
    bool lastStanding = true; // a burned player leaves, the last one left wins (false: the first burn ends the match)
    int unopenedPoints = 100; // written on a player who never opened
    int jokerPoints = 25;     // a joker left in an opened player's hand
    bool konkenDouble = true; // konken (open and finish in one turn) doubles the others' points
    int maxHands = 40;        // safety net: the match never runs longer than this
};

// ---------------------------------------------------------------------------------------------------------
// Melds
// ---------------------------------------------------------------------------------------------------------

enum class MeldKind : int { Set = 0, Run = 1 };
enum class Side : int { Auto = 0, Front = 1, Back = 2 }; // where a card goes on a run (sets: Auto)

struct Meld {
    MeldKind kind = MeldKind::Set;
    int owner = -1;
    int suit = -1;           // run: its suit
    int rank = 0;            // set: its rank (2..14); run: the rank the first card stands for (1..12)
    std::vector<int> cards;  // run: low to high (card i stands for rank + i); set: in laying order
    int size() const { return (int)cards.size(); }
    int low() const { return rank; }
    int high() const { return kind == MeldKind::Run ? rank + size() - 1 : rank; }
    int jokers() const;
    int value() const;       // the points it counts in an opening
    // The rank card i stands for (run: rank + i; set: rank) and, for a joker in a set, the suits it may stand for.
    int rankAt(int i) const { return kind == MeldKind::Run ? rank + i : rank; }
    std::vector<int> missingSuits() const; // set: suits no real card has
};

// `cards` as one meld (validity + the engine's arrangement). Sets: one rank, different suits, 3-4 cards. Runs: one
// suit; if `cards` in the given order (or reversed) already form a run with jokers standing where they are, that
// arrangement is kept, otherwise the real cards are sorted and jokers fill the gaps, any spare joker going on the high
// end (the low end when the high one is full). The As is tried low and high (the higher value wins). At least two real
// cards. `why` (optional) gets a Turkish reason on failure.
bool makeMeld(const std::vector<int>& cards, Meld& out, std::string* why = nullptr);
// Whether `card` can be added to `m` (on `side` for a run; Auto: the high end first) and where it went.
bool canAdd(const Meld& m, int card, Side side = Side::Auto, Side* used = nullptr);
void addCard(Meld& m, int card, Side side);  // (only after canAdd)
// The index of a joker in `m` that `card` (a real card) may replace, -1 if none.
int swapIndex(const Meld& m, int card);

// The best way to lay `cards` (a hand, or the player's chosen cards) as melds.
enum class Goal : int {
    Value = 0, // the most meld value (openings); ties: more cards
    Shed = 1   // the fewest points left in hand
};
struct Partition {
    std::vector<std::vector<int>> melds; // each valid for makeMeld (jokers included)
    std::vector<int> rest;               // cards in no meld (spare jokers too, unless attached)
    int value = 0;                       // the melds' value
    int restPoints = 0;                  // handPoints of `rest`
    int laid = 0;                        // cards in melds
};
// `mustUse` (>= 0): only partitions where that card is in a meld (none -> empty melds, rest = cards). Spare jokers are
// put on melds with room when `attachJokers`, else kept in `rest`. Exact for the real cards (every candidate meld that
// uses each real card present is tried); a few thousand nodes for a 15-card hand.
Partition bestPartition(const std::vector<int>& cards, Goal goal, int mustUse = -1, bool attachJokers = true,
                        int jokerPoints = 25);

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

enum class Stage { NotStarted, Draw, Play, HandOver, MatchOver };

struct SeatInfo {
    std::string name;
    bool human = false;
    std::vector<int> hand;       // in the order the cards came (the UI keeps the player's own arrangement)
    bool opened = false;
    int openValue = 0;           // what the opening was worth
    int total = 0;               // running match total (low is good)
    bool out = false;            // burned and left the table (Rules::lastStanding)
    int outHand = -1;            // the hand (0-based) he burned in
    int place = 0;               // final place 1..4 once known (burned, or the match over); 0 still playing
};

// Every discard of the hand, in order (public): who threw what, and who took it back up (-1: nobody).
struct DiscardRecord {
    int seat = -1;
    int card = -1;
    int turn = 0;
    int takenBy = -1;
};

// One row of the hand sheet.
struct HandRecord {
    int index = 0;                  // 0-based hand number
    int starter = -1;
    int finisher = -1;              // -1: the stock ran out
    bool konken = false;            // the finisher opened and finished in the same turn
    std::array<int, 4> points{};    // this hand
    std::array<int, 4> totals{};    // after this hand
    std::array<bool, 4> opened{};
    std::array<int, 4> cardsLeft{};
    std::array<bool, 4> playing{};  // at the table this hand (Rules::lastStanding: the burned ones are not)
    std::array<bool, 4> burned{};   // burned with this hand
    int turns = 0;
};

enum class EvType {
    MatchStart,   // new match
    HandStart,    // seat = starter, amount = hand index (0-based)
    Deal,         // cards dealt; card = the first discard turned up
    TurnStart,    // seat to play
    DrawStock,    // seat drew `card` (the card is the drawer's own news: show it only to them)
    TakeDiscard,  // seat took `card` off the discard pile (must use it this turn)
    ReturnDiscard,// seat gave `card` back (then draws)
    Open,         // seat opened: melds = new table indices, amount = value
    LayMelds,     // seat laid more melds: melds = new table indices
    AddToMeld,    // seat added `card` to table meld `meld`
    SwapJoker,    // seat put `card` in meld `meld` and took joker `card2`
    Discard,      // seat discarded `card`
    HandEnd,      // seat = finisher (-1 none), amount = 1 for a konken; points = this hand; lines = per-seat lines
    MatchEnd,     // seat = winner (lowest total, ties: lower seat); points = totals; lines = ranking
    Burn          // (after HandEnd) seat burned and leaves the table (Rules::lastStanding); amount = his place,
                  // points = totals
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int card = -1;
    int card2 = -1;
    int meld = -1;
    int amount = 0;
    std::vector<int> melds;
    std::array<int, 4> points{};
    std::string text;               // short Turkish line for the HUD / log
    std::vector<std::string> lines; // HandEnd / MatchEnd details
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One successful action of the match (a saved match is its seed plus these, replayed).
enum class LogKind : int { Draw = 0, Take = 1, Return = 2, Lay = 3, Add = 4, Swap = 5, Discard = 6, NextHand = 7 };
struct LoggedAction {
    LogKind kind = LogKind::Draw;
    int seat = -1;
    int card = -1;                        // Add / Swap / Discard
    int meld = -1;                        // Add / Swap
    int side = 0;                         // Add: (int)Side
    std::vector<std::vector<int>> melds;  // Lay
    // One text line: "kind seat card meld side" and, for Lay, "| c c c | c c c" after it.
    std::string encode() const;
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);            // only between matches
    const Rules& rules() const { return rules_; }
    void setPlayer(int seat, const std::string& name, bool human);
    void startMatch(uint64_t seed);           // resets the sheet, deals hand 0
    void startNextHand();                     // HandOver -> next deal

    // ---- queries (public, except hand(seat) of the other seats: the UI shows only the human's, bots read their own) ----
    Stage stage() const { return stage_; }
    int handIndex() const { return handIndex_; }
    int starter() const { return starter_; }
    int dealer() const { return prevActive(starter_); }
    int current() const { return current_; }
    int turn() const { return turn_; }                     // turns played this hand (0-based for the current one)
    const SeatInfo& seat(int s) const { return seats_[s]; }
    const std::string& name(int s) const { return seats_[s].name; }
    bool isHuman(int s) const { return seats_[s].human; }
    const std::vector<int>& hand(int s) const { return seats_[s].hand; }
    int handSize(int s) const { return (int)seats_[s].hand.size(); }
    bool opened(int s) const { return seats_[s].opened; }
    // ---- the shrinking table (Rules::lastStanding): who still plays; the turn goes round the active seats only
    bool active(int s) const { return s >= 0 && s < 4 && !seats_[s].out; }
    bool isOut(int s) const { return !active(s); }
    int activeCount() const;
    int nextActive(int s) const;   // the next seat to the right that still plays (s itself if alone)
    int prevActive(int s) const;
    int place(int s) const { return seats_[s].place; } // 1..4 once known (burned / match over), else 0
    const std::vector<Meld>& table() const { return table_; }
    const std::vector<int>& discardPile() const { return discard_; } // bottom first; the top is back()
    int discardTop() const { return discard_.empty() ? -1 : discard_.back(); }
    int stockSize() const { return (int)stock_.size(); }
    const std::vector<DiscardRecord>& discards() const { return discards_; }
    int takenCard() const { return taken_; }               // the discard taken this turn, still to be used (-1 none)
    bool mustDrawStock() const { return mustDrawStock_; }  // gave the discard back: only the stock now
    bool openedThisTurn() const { return openedNow_; }
    int openNeed() const { return rules_.openMin; }

    // The checks the actions apply (never mutate; the UI calls them for live hints).
    bool canTakeDiscard(int seat, std::string* why = nullptr) const;
    // Melds `seat` could lay now (validity + opening value + cards in hand); `value` gets their total.
    bool checkLay(int seat, const std::vector<std::vector<int>>& melds, int* value = nullptr, std::string* why = nullptr) const;
    bool checkAdd(int seat, int card, int meld, Side side = Side::Auto, std::string* why = nullptr) const;
    bool checkSwap(int seat, int card, int meld, std::string* why = nullptr) const;
    bool canDiscard(int seat, int card, std::string* why = nullptr) const;
    // A joker may be thrown: it is the last card, or the hand is only jokers and none of them can go on the table
    // (an opened player could not move otherwise).
    bool mayDiscardJoker(int seat) const;
    // A card of `seat`'s hand that fits a meld on the table (işlek), any side.
    bool fitsTable(int card) const;

    // Score
    const std::vector<HandRecord>& sheet() const { return sheet_; }
    int total(int s) const { return seats_[s].total; }
    // The standings, best first: the players still at the table by total (lowest first; ties: lower seat), then the
    // burned ones, the last to burn first (the same hand: the lower total first). Without burns: by total.
    std::array<int, 4> ranking() const;
    int leaderSeat() const { return ranking()[0]; }
    // Points `seat` would write if the hand ended now without them finishing.
    int pointsInHand(int s) const;

    // ---- actions ----
    ActionResult drawStock(int seat);
    ActionResult takeDiscard(int seat);
    ActionResult returnDiscard(int seat);
    ActionResult layMelds(int seat, const std::vector<std::vector<int>>& melds);
    ActionResult addToMeld(int seat, int card, int meld, Side side = Side::Auto);
    ActionResult swapJoker(int seat, int card, int meld);
    ActionResult discard(int seat, int card);

    // ---- save / resume ----
    uint64_t matchSeed() const { return matchSeed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a);

    // ---- events ----
    std::vector<GameEvent> drainEvents();

    // ---- testing hooks (tests/ and tools/ only) ----
    // Replace the deal (Draw stage of the current turn): hands, stock (top = back), discard pile (top = back) and the
    // table; the caller keeps the 108 cards consistent (or not: the engine only checks what it needs).
    void debugSetup(const std::array<std::vector<int>, 4>& hands, const std::vector<int>& stock,
                    const std::vector<int>& discardPile, const std::vector<Meld>& table = {});
    void debugSetOpened(int seat, bool o) { seats_[seat].opened = o; }
    void debugSetCurrent(int seat) { current_ = seat; }
    void debugSetTotal(int seat, int v) { seats_[seat].total = v; }

private:
    void dealHand();
    void push(GameEvent e);
    void beginTurn(int seat);
    void endHand(int finisher);
    bool removeFromHand(int seat, int card);
    bool inHand(int seat, int card) const;
    bool checkTurn(int seat, Stage st, std::string* why) const;
    void afterTableAction(int seat);  // the taken card used? the hand empty -> finish
    std::string says(int s, const std::string& third, const std::string& second) const;

    Rules rules_;
    std::array<SeatInfo, 4> seats_;
    Rng rng_;
    Stage stage_ = Stage::NotStarted;
    int handIndex_ = 0;
    int starter_ = 0;
    int current_ = 0;
    int turn_ = 0;
    std::vector<int> stock_;
    std::vector<int> discard_;
    std::vector<Meld> table_;
    std::vector<DiscardRecord> discards_;
    int taken_ = -1;
    bool mustDrawStock_ = false;
    bool openedAtTurnStart_ = false;
    bool openedNow_ = false;
    std::vector<HandRecord> sheet_;
    std::vector<GameEvent> events_;
    uint64_t matchSeed_ = 0;
    std::vector<LoggedAction> log_;
};

} // namespace konken
