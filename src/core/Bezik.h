#pragma once
// Bezik (two-handed bezique, played with a double piquet deck) — engine. Pure logic: no raylib, no I/O,
// deterministic for a given seed. Rules and the choices made: docs/kurallar_bezik.md. Bots: src/core/BezikBot.h.
//
// Cards: 64 = 7..A of the four suits, two of each. A card is a card *instance* id shared with r3d::Cards3D: the first
// copy uses the kart ids (core/Cards.h: suit * 13 + rank - 2), the second copy the same + 52. So suitOf / rankOf
// work through the face (id % 52); ranks 7..14 only. Order in a suit: A > 10 > P > K > V > 9 > 8 > 7.
//
// Seats: 0 = the player, 1 = the opponent (the table puts seat 1 across, at the two-seat table).
//
// A deal ("el"): 8 cards each, the next card is turned up under the stock and makes koz (a 7: the dealer writes 10).
// Stage 1 (while the stock lasts): any card may be played; the trick's winner may declare one combination (and score
// the koz 7), then the winner draws first, the loser second. Declared cards lie face up in front of their owner and
// can be played from there. Stage 2 (the stock is gone, the table cards go back to the hands): follow suit and win the
// trick if you can. Aces and tens taken ("brisk") 10 each, the last trick 10. The match goes to Rules::target.
//
// Typical UI loop:
//   g.startMatch(seed);                       // deals hand 0
//   Playing:  g.current() plays: g.legalCards(seat) / g.playCard(seat, card)
//   Declare:  g.current() (the trick's winner): g.availableMelds(seat), g.declare(seat, meld), g.koz7(seat), g.pass(seat)
//   HandOver: the sheet (g.sheet()), then g.startNextHand()
//   drain g.drainEvents() every frame for animations / texts
#include "core/Cards.h"
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bezik {

using okey::Rng;

// ---------------------------------------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------------------------------------

constexpr int NUM_IDS = 104;   // card instance ids 0..103 (only the 64 with rank 7..A are used)
constexpr int DECK_SIZE = 64;
constexpr int HAND_SIZE = 8;

inline int faceOf(int id) { return id % kart::NUM_CARDS; }
inline int suitOf(int id) { return kart::suitOf(faceOf(id)); }
inline int rankOf(int id) { return kart::rankOf(faceOf(id)); }
inline int copyOf(int id) { return id / kart::NUM_CARDS; }
inline bool isCard(int id) { return id >= 0 && id < NUM_IDS && rankOf(id) >= 7; }
// Trick strength in a suit: 7 -> 0, 8, 9, V, K, P, 10, A -> 7.
inline int strength(int id) {
    static const int s[15] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 6, 3, 4, 5, 7};
    return s[rankOf(id)];
}
inline bool isBrisque(int id) { return rankOf(id) == kart::As || rankOf(id) == 10; }
// Compact index 0..63 (copy * 32 + suit * 8 + strength order 7..A by rank - 7) and back.
inline int idxOf(int id) { return copyOf(id) * 32 + suitOf(id) * 8 + (rankOf(id) - 7); }
inline int idOf(int idx) { return (idx / 32) * kart::NUM_CARDS + kart::makeCard((idx % 32) / 8, idx % 8 + 7); }
// The same face, the other copy.
inline int twinOf(int id) { return id < kart::NUM_CARDS ? id + kart::NUM_CARDS : id - kart::NUM_CARDS; }
inline bool sameFace(int a, int b) { return faceOf(a) == faceOf(b); }
std::vector<int> fullDeck();              // the 64 ids, ascending
std::string cardName(int id);              // "Maça Kız"
std::string cardAccusative(int id);        // "Maça Kızı"

// Does `b` (played second) beat `a` (led)? Identical cards: the first wins.
inline bool beats(int b, int a, int trump) {
    if (suitOf(b) == suitOf(a)) return strength(b) > strength(a);
    return suitOf(b) == trump;
}

// ---------------------------------------------------------------------------------------------------------
// Combinations (deklarasyon)
// ---------------------------------------------------------------------------------------------------------

enum class MeldKind : int {
    Bezik = 0,     // Maça Kız + Karo Vale: 40
    CiftBezik,     // both Maça Kız + both Karo Vale: 500
    Seri,          // koz A 10 P K V: 250
    DortAs,        // four aces (any suits): 100
    DortPapaz,     // 80
    DortKiz,       // 60
    DortVale,      // 40
    Evlilik,       // P + K of a plain suit: 20
    KozEvlilik,    // P + K of koz: 40
    Count
};
constexpr int NUM_MELDS = (int)MeldKind::Count;
const char* meldNameTR(MeldKind k);        // "Bezik", "Çift bezik", "Koz serisi", "Dört as", ..., "Koz evliliği"
int meldBasePoints(MeldKind k);
constexpr int BEZIK_QUEEN = kart::Maca * 13 + (kart::Kiz - 2);  // Maça Kız (first copy)
constexpr int BEZIK_JACK = kart::Karo * 13 + (kart::Vale - 2);  // Karo Vale (first copy)

struct Meld {
    MeldKind kind = MeldKind::Bezik;
    std::vector<int> cards;  // the card ids it shows
    int points = 0;
};

// ---------------------------------------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------------------------------------

struct Rules {
    int target = 1000;          // the match: the first to reach this at the end of a deal (1000 or 1500)
    int brisque = 10;           // per ace / ten taken
    int lastTrick = 10;         // the deal's last trick
    int seven = 10;             // the koz 7 exchanged or shown, and a 7 turned up for koz (the dealer)
    std::array<int, NUM_MELDS> points{{40, 500, 250, 100, 80, 60, 40, 20, 40}};
};

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

enum class Stage { NotStarted, Playing, Declare, HandOver, MatchOver };

struct TrickCard {
    int seat = -1;
    int card = -1;
};

struct Trick {
    int index = 0;                  // 0..31 within the deal
    int leader = -1;
    std::vector<TrickCard> cards;   // play order
    int winner = -1;
    bool second = false;            // played in stage 2
};

// One row of the score sheet (a finished deal).
struct HandRecord {
    int index = 0;
    int dealer = 0;
    int trump = 0;
    std::array<int, 2> melds{};      // combinations + koz 7s (+ the dealer's turned 7)
    std::array<int, 2> brisques{};   // aces and tens taken (points)
    std::array<int, 2> last{};       // the last trick
    std::array<int, 2> points{};     // this deal
    std::array<int, 2> totals{};     // after it
    std::array<int, 2> best{};       // the biggest single combination
    std::array<std::string, 2> bestName{};
};

enum class EvType {
    MatchStart,     //
    HandStart,      // seat = dealer, amount = deal index
    Deal,           // 8 each; card = the turned-up koz card, amount = 10 when it is a 7 (seat = dealer scores it)
    TurnStart,      // seat = to play / to declare
    Play,           // seat played card (fromTable: it lay among their combinations)
    TrickWon,       // seat = winner, cards = the trick (play order)
    DeclareTurn,    // seat (the trick's winner) may declare now
    Declared,       // seat, meld kind (amount = points), cards
    Koz7,           // seat scored the koz 7 (card): swapped = took the turned-up card (card2) in exchange
    Passed,         // seat declared nothing
    Draw,           // seat drew card from the stock (turnUp: it was the turned-up koz card)
    Stage2,         // the stock is gone: the table cards go back to the hands; follow suit and win from now on
    HandEnd,        // points per seat (this deal), lines = per-seat result lines
    MatchEnd        // seat = winner; points = totals
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int card = -1;
    int card2 = -1;
    bool swapped = false;
    bool fromTable = false;
    bool turnUp = false;
    MeldKind meld = MeldKind::Bezik;
    int amount = 0;
    std::vector<int> cards;
    std::array<int, 2> points{};
    std::string text;
    std::vector<std::string> lines;
};

struct ActionResult {
    bool ok = false;
    std::string error;
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One successful action, as given to Game (a saved match is its seed plus these).
enum class LogKind { Play = 0, Declare = 1, Koz7 = 2, Pass = 3, NextHand = 4 };
struct LoggedAction {
    LogKind kind = LogKind::Play;
    int seat = -1;
    int value = -1;              // Play: the card; Declare: (int)MeldKind
    std::vector<int> cards;      // Declare: the meld's cards
    std::string encode() const;  // "kind seat value [cards...]"
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);
    const Rules& rules() const { return rules_; }
    void setPlayer(int seat, const std::string& name, bool human);
    void startMatch(uint64_t seed);
    void startNextHand();
    // No events and no texts (the bots' rollouts): much faster.
    void setSilent(bool on) { silent_ = on; }
    // A private copy for the bots: silent, without the match's history (log, sheet, events).
    void stripForSimulation() {
        silent_ = true;
        log_.clear();
        sheet_.clear();
        events_.clear();
    }

    // ---- queries ----
    Stage stage() const { return stage_; }
    int handIndex() const { return handIndex_; }
    int dealer() const { return dealer_; }
    int current() const { return current_; }
    int trump() const { return trump_; }
    int turnUp() const { return turnUp_; }                 // the koz card under the stock (-1: drawn)
    int stockSize() const { return (int)stock_.size(); }   // face-down cards left (not counting the turn-up)
    bool secondStage() const { return second_; }
    // The face-down stock, bottom first (drawn from the back). For the table's 3D cards only (they lie face down):
    // bots and the player never look at it.
    const std::vector<int>& stockCards() const { return stock_; }
    const std::string& name(int s) const { return names_[(size_t)s]; }
    bool isHuman(int s) const { return human_[(size_t)s]; }
    // Concealed hand (sorted) and the combinations' cards lying face up before the player (stage 1).
    const std::vector<int>& hand(int s) const { return hand_[(size_t)s]; }
    const std::vector<int>& tableCards(int s) const { return table_[(size_t)s]; }
    std::vector<int> playable(int s) const;               // hand + table cards
    int handCount(int s) const { return (int)(hand_[(size_t)s].size() + table_[(size_t)s].size()); }
    // Cards in s's concealed hand that everyone knows (the turned-up card taken in the koz 7 exchange or at the last
    // draw, a koz 7 shown): public.
    const std::vector<int>& exposed(int s) const { return exposed_[(size_t)s]; }
    const Trick& currentTrick() const { return trick_; }
    const std::vector<Trick>& tricks() const { return tricks_; }
    int trickNumber() const { return (int)tricks_.size(); }
    const std::vector<int>& won(int s) const { return won_[(size_t)s]; } // cards taken this deal
    std::vector<int> legalCards(int seat) const;
    bool isLegal(int seat, int card) const;
    // Declare stage: what the trick's winner may declare now (one canonical card set per combination), and whether a
    // koz 7 can be scored (exchanged, or shown when the turned-up card is a 7 already / the stock is empty).
    std::vector<Meld> availableMelds(int seat) const;
    int bestMeldPoints(int seat) const;                   // the biggest of them (0: none), cheap
    bool canKoz7(int seat) const;
    bool kozSevenSwaps() const;                           // the next koz 7 would be exchanged (else shown)
    bool declaredThisTurn() const { return declaredNow_; }
    // A combination's validity (cards present, not used in the same kind before, one new card at least).
    bool meldValid(int seat, const Meld& m, std::string* why = nullptr) const;
    // Every combination declared this deal (seat, meld), in order.
    const std::vector<std::pair<int, Meld>>& declared() const { return declared_; }
    uint16_t usedKinds(int card) const { return used_[(size_t)card]; } // bit k: used in a MeldKind k
    bool sevenScored(int card) const { return sevenDone_[(size_t)card]; }

    // Score
    int handPoints(int s) const { return meldPts_[(size_t)s] + brisquePts_[(size_t)s] + lastPts_[(size_t)s]; }
    int meldPoints(int s) const { return meldPts_[(size_t)s]; }
    int brisquePoints(int s) const { return brisquePts_[(size_t)s]; }
    int total(int s) const { return totals_[(size_t)s]; }
    const std::vector<HandRecord>& sheet() const { return sheet_; }
    int leader() const { return totals_[1] > totals_[0] ? 1 : 0; }
    int winner() const;                                   // MatchOver: the higher total

    // ---- actions ----
    ActionResult playCard(int seat, int card);
    ActionResult declare(int seat, const Meld& m);
    ActionResult koz7(int seat);
    ActionResult pass(int seat);

    // ---- save / resume ----
    uint64_t matchSeed() const { return matchSeed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a);

    std::vector<GameEvent> drainEvents();

    // ---- testing / bots' determinisation (tests, tools and the bots' private copies only) ----
    // Replace the current deal: hands, the stock (drawn from the back), the turned-up card; table cards cleared.
    void debugSetDeal(const std::array<std::vector<int>, 2>& hands, const std::vector<int>& stock, int turnUp);
    void debugSetHand(int seat, const std::vector<int>& cards) { hand_[(size_t)seat] = cards; sortHand(seat); }
    void debugSetStock(const std::vector<int>& stock) { stock_ = stock; }
    void debugSetWon(int seat, const std::vector<int>& cards) { won_[(size_t)seat] = cards; }
    void debugSetTotal(int seat, int v) { totals_[(size_t)seat] = v; }
    void debugSetCurrent(int seat) { current_ = seat; trick_.leader = seat; }

private:
    ActionResult playImpl(int seat, int card);
    void dealHand();
    void beginTurn(int seat);
    void finishTrick();
    void endDeclare();
    void drawCards(int winner);
    void enterSecondStage();
    void endHand();
    void push(GameEvent&& e);
    void sortHand(int s);
    bool removeCard(int s, int card, bool* fromTable);
    bool has(int s, int card) const;
    bool holdsKoz7(int seat) const;
    int scanMelds(int seat, std::vector<Meld>* out) const;
    bool inTable(int s, int card) const;
    std::string says(int s, const std::string& third, const std::string& second) const;
    void logAction(LogKind k, int seat, int value, const std::vector<int>& cards = {});

    Rules rules_;
    std::array<std::string, 2> names_{{"Sen", "Rakip"}};
    std::array<bool, 2> human_{{true, false}};
    Rng rng_;
    bool silent_ = false;
    Stage stage_ = Stage::NotStarted;
    int handIndex_ = 0;
    int firstDealer_ = 1;
    int dealer_ = 1;
    int current_ = 0;
    int trump_ = 0;
    int turnUp_ = -1;
    bool second_ = false;
    bool declaredNow_ = false;
    std::vector<int> stock_;
    std::array<std::vector<int>, 2> hand_, table_, won_, exposed_;
    std::array<uint16_t, NUM_IDS> used_{};
    std::array<bool, NUM_IDS> sevenDone_{};
    Trick trick_;
    std::vector<Trick> tricks_;
    std::vector<std::pair<int, Meld>> declared_;
    std::array<int, 2> meldPts_{}, brisquePts_{}, lastPts_{}, totals_{};
    std::array<int, 2> bestMeld_{};
    std::array<std::string, 2> bestName_{};
    std::vector<HandRecord> sheet_;
    std::vector<GameEvent> events_;
    uint64_t matchSeed_ = 0;
    std::vector<LoggedAction> log_;
};

} // namespace bezik
