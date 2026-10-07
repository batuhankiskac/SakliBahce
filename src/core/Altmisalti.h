#pragma once
// Altmışaltı (66) — the two-player trick-taking game with koz, evlilik and kapatmak. Pure logic: no raylib, no I/O,
// deterministic for a given seed. Rules: docs/kurallar_altmisalti.md. Bots: src/core/AltmisaltiBot.h.
//
// Cards are core/Cards.h's ids (namespace kart); the deck is the 24 cards 9, 10, Vale, Kız, Papaz, As of every suit.
// Players are 0 (the human's seat) and 1 (the opponent); the table maps player 1 to the seat across.
//
// A hand ("el"): 6 cards each (3 + 3), the 13th card face up under the stock is the koz. The non-dealer leads. While
// the stock is open there is no obligation; the trick's winner draws first, then the other. The player on lead may
// swap the koz 9 for the face-up koz (having taken a trick), close the stock (kapatmak), and declares a marriage
// (Kız + Papaz: 20, in koz 40) by leading one of the pair. Once the stock is closed or used up, suit must be followed
// and the trick headed if possible, else a koz played. 66 card points win the hand (declared automatically); game
// points 1 / 2 / 3 by the loser's points and tricks; the match goes to Rules::target (7).
//
// Typical UI loop:
//   g.startMatch(seed);                       // deals hand 0, stage Playing
//   while (g.stage() != Stage::MatchOver) {
//       Playing:  g.current() decides: g.legalCards(p), g.canExchange(p) / exchangeNine(p), g.canClose(p) /
//                 closeStock(p), g.playCard(p, card)
//       HandOver: show the sheet (g.sheet()), then g.startNextHand()
//       drain g.drainEvents() every frame for animations / texts
//   }
#include "core/Cards.h"
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace altmisalti {

using okey::Rng;
using CardMask = uint64_t;

constexpr int DECK_SIZE = 24;
constexpr int HAND_SIZE = 6;
constexpr int GOAL = 66;          // card points that win a hand
constexpr int SCHNEIDER = 33;     // the loser below this: 2 game points

inline CardMask cardBit(int c) { return 1ull << c; }
inline int popcount(CardMask m) { return __builtin_popcountll(m); }
inline int lowestCard(CardMask m) { return m ? __builtin_ctzll(m) : -1; }
inline bool inDeck(int c) { return kart::isValidCard(c) && kart::rankOf(c) >= 9; }
// The 6 cards of `suit` (9 .. As) as a mask.
inline CardMask suitMask(int suit) { return 0x1F80ull << (13 * suit); } // ranks 9..14 = bits 7..12 of the suit
constexpr CardMask DECK_MASK = (0x1F80ull) | (0x1F80ull << 13) | (0x1F80ull << 26) | (0x1F80ull << 39);
using kart::cardsOf; // ascending ids
using kart::maskOf;

// Card points: As 11, 10 10, Papaz 4, Kız 3, Vale 2, 9 0.
int cardPoints(int card);
// Rank order inside a suit: 9 0 < Vale 1 < Kız 2 < Papaz 3 < 10 4 < As 5.
int cardOrder(int card);
// Does `second` (played after `first`) take the trick?
bool beats(int first, int second, int trumpSuit);
// "Kupa 10", "Maça As": the card's spoken name (as kart::cardNameTR).
inline std::string cardName(int c) { return kart::cardNameTR(c); }

struct Rules {
    int target = 7;              // game points that win the match
    bool lastTrickBonus = true;  // the last trick of a hand played out (stock used up, not closed) is worth 10
};

// How a hand ended.
enum class EndReason : int {
    None = 0,
    Reached,        // the winner reached 66 with a trick
    Marriage,       // ... with a marriage declared on lead
    CloserFailed,   // the closer did not reach 66 (or the other one did first)
    LastTrick       // played out, nobody at 66: the last trick's winner takes the hand
};

// The full state of one deal: both hands and the stock. The engine's core, shared with the bots' searches (a bot
// works on a copy whose hidden cards it has filled in itself, see Game::viewOf). No strings, no events: cheap to copy.
struct Deal {
    std::array<CardMask, 2> hand{};
    std::array<int, 12> stock{};  // stock[0] = the koz card turned up under the stock, stock[stockN-1] = the top
    int stockN = 0;
    int trumpSuit = 0;
    bool closed = false;
    int closer = -1;
    int closeOppTricks = 0;       // the other player's tricks when the stock was closed
    std::array<int, 2> points{};  // card points counted (marriages included once the player has a trick)
    std::array<int, 2> pending{}; // marriage points waiting for the player's first trick
    std::array<int, 2> tricks{};
    std::array<uint8_t, 2> marriages{}; // suits whose marriage the player declared (bit per suit)
    int leader = 0;
    int lead = -1;                // the card led to the current trick (-1: none yet)
    CardMask played = 0;          // every card played this hand
    int lastWinner = -1;          // winner of the last finished trick
    bool lastBonus = true;        // Rules::lastTrickBonus
    // result
    bool over = false;
    int winner = -1;
    int gamePoints = 0;
    EndReason reason = EndReason::None;

    int turn() const { return lead < 0 ? leader : 1 - leader; }
    bool stockOpen() const { return !closed && stockN > 0; }
    bool strict() const { return !stockOpen(); }           // follow suit / head the trick / koz
    int faceUpTrump() const { return stockOpen() ? stock[0] : -1; }
    int handSize(int p) const { return popcount(hand[(size_t)p]); }
    CardMask legal(int p) const;
    bool canExchange(int p) const;
    bool canClose(int p) const;
    int marriageValue(int p, int card) const; // points a lead of `card` declares (0: none)
    // Applying (callers check legality first; the engine does, the bots use legal()).
    void exchange(int p);
    void close(int p);
    void play(int p, int card);
    // The card drawn now by the trick's winner / loser (filled by play() when it draws; -1 none).
    int drew[2] = {-1, -1};
    int trickCards[2] = {-1, -1}; // the trick play() just finished (leader's, follower's), else -1

private:
    void finish(int w, EndReason why);
};

// Score of a finished deal for `p`: + game points won, - game points lost.
inline int signedResult(const Deal& d, int p) { return !d.over ? 0 : d.winner == p ? d.gamePoints : -d.gamePoints; }

enum class Stage { NotStarted, Playing, HandOver, MatchOver };

// One play of the current hand, in order (public history).
struct PlayRecord {
    int player = -1;
    int card = -1;
    bool lead = false;
    bool strict = false;   // played under the strict rules (closed / used-up stock)
    int marriage = 0;      // points declared with this lead
};

// One row of the score sheet.
struct HandRecord {
    int index = 0;
    int dealer = 0;
    int winner = -1;
    int gamePoints = 0;
    EndReason reason = EndReason::None;
    int closer = -1;
    std::array<int, 2> points{};    // card points (marriages counted)
    std::array<int, 2> tricks{};
    std::array<int, 2> marriages{}; // marriage points declared
    std::array<int, 2> totals{};    // game points after this hand
};

enum class EvType {
    MatchStart,
    HandStart,   // seat = dealer, amount = hand index
    Deal,        // the hands are dealt, the koz card turned up (card)
    TurnStart,   // seat to play
    Exchange,    // seat swapped the koz 9 (card) for the face-up koz (amount = the card taken)
    Close,       // seat closed the stock
    Marriage,    // seat declared a marriage: suit = card's suit, amount = points; card = the card led
    Play,        // seat played card
    TrickWon,    // seat = winner, cards = (leader's, follower's), amount = card points of the trick
    Draw,        // seat drew from the stock (card: the drawn card only for the face-up koz, else -1)
    StockOut,    // the stock is used up: strict rules from now on
    HandEnd,     // seat = winner, amount = game points; text says why
    MatchEnd     // seat = winner
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int card = -1;
    int suit = -1;
    int amount = 0;
    std::vector<int> cards;
    std::string text;      // short Turkish line for the HUD / log
};

struct ActionResult {
    bool ok = false;
    std::string error;     // Turkish, user facing
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One successful action of the match (a saved match is its seed plus these, replayed).
enum class LogKind { Play = 0, Exchange = 1, Close = 2, NextHand = 3 };
struct LoggedAction {
    LogKind kind = LogKind::Play;
    int player = -1;
    int card = -1;
    std::string encode() const;  // "kind player card"
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    void setRules(const Rules& r) { if (stage_ == Stage::NotStarted || stage_ == Stage::MatchOver) rules_ = r; }
    const Rules& rules() const { return rules_; }
    void setPlayer(int p, const std::string& name, bool human);
    void startMatch(uint64_t seed);
    void startNextHand();

    // ---- queries (public, except hand(p) of the other player: the UI shows only the human's and bots read only
    //      their own; viewOf() gives a bot the deal with the hidden cards blanked) ----
    Stage stage() const { return stage_; }
    int handIndex() const { return handIndex_; }
    int dealer() const { return dealer_; }
    int current() const { return stage_ == Stage::Playing ? deal_.turn() : -1; }
    const std::string& name(int p) const { return names_[(size_t)p]; }
    bool isHuman(int p) const { return human_[(size_t)p]; }
    std::vector<int> hand(int p) const { return cardsOf(deal_.hand[(size_t)p]); }
    int handSize(int p) const { return deal_.handSize(p); }
    int trumpSuit() const { return deal_.trumpSuit; }
    int trumpCard() const { return deal_.stockN > 0 ? deal_.stock[0] : -1; } // the koz card under the stock
    int faceUpTrump() const { return deal_.faceUpTrump(); }                  // ... while it is still face up
    std::vector<int> stockCards() const;    // bottom (the koz card) first (the table draws them; bots must not)
    int stockCount() const { return deal_.stockN; }
    bool closed() const { return deal_.closed; }
    int closer() const { return deal_.closer; }
    bool strict() const { return deal_.strict(); }
    int leader() const { return deal_.leader; }
    int ledCard() const { return deal_.lead; }
    int points(int p) const { return deal_.points[(size_t)p]; }
    int pendingMarriage(int p) const { return deal_.pending[(size_t)p]; }
    int tricks(int p) const { return deal_.tricks[(size_t)p]; }
    const std::vector<int>& wonCards(int p) const { return won_[(size_t)p]; }
    const std::vector<PlayRecord>& plays() const { return plays_; }
    CardMask playedMask() const { return deal_.played; }
    // Public facts about p's hand: cards known to be there (the koz taken by exchange or with the last draw, the other
    // half of a declared marriage) and cards p cannot hold (shown by the strict rules).
    CardMask shownCards(int p) const { return shown_[(size_t)p] & deal_.hand[(size_t)p]; }
    CardMask cannotHold(int p) const { return cannot_[(size_t)p]; }
    std::vector<int> legalCards(int p) const;
    bool canExchange(int p) const { return stage_ == Stage::Playing && deal_.canExchange(p); }
    bool canClose(int p) const { return stage_ == Stage::Playing && deal_.canClose(p); }
    int marriageValue(int p, int card) const { return deal_.marriageValue(p, card); }
    const Deal& dealState() const { return deal_; } // (tests / tools; never bots: it holds the hidden cards)
    // The deal as player `viewer` may know it: the other hand holds only its shown cards, the face-down stock cards
    // are -1 (the koz card at the bottom stays known). The bots fill the blanks with a sample.
    Deal viewOf(int viewer) const;

    const std::vector<HandRecord>& sheet() const { return sheet_; }
    int total(int p) const { return totals_[(size_t)p]; }
    int matchWinner() const; // -1 while running

    // ---- actions ----
    ActionResult playCard(int p, int card);
    ActionResult exchangeNine(int p);
    ActionResult closeStock(int p);

    // ---- save / resume ----
    uint64_t matchSeed() const { return matchSeed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a);

    std::vector<GameEvent> drainEvents();

    // ---- testing hooks (tests / tools only) ----
    // Replace the deal: hands, the stock (bottom = the koz card first) and the leader; points / tricks reset.
    void debugSetDeal(const std::vector<int>& h0, const std::vector<int>& h1, const std::vector<int>& stock, int leader);
    void debugSetTotals(int a, int b) { totals_ = {a, b}; }
    void debugSetPoints(int p, int points, int tricks) { deal_.points[(size_t)p] = points; deal_.tricks[(size_t)p] = tricks; }

private:
    void dealHand();
    void push(GameEvent e);
    void afterAction();
    void endHand();
    std::string says(int p, const std::string& third, const std::string& second) const;

    Rules rules_;
    std::array<std::string, 2> names_{{"Sen", "Rakip"}};
    std::array<bool, 2> human_{{true, false}};
    Rng rng_;
    Stage stage_ = Stage::NotStarted;
    Deal deal_;
    int handIndex_ = 0;
    int dealer_ = 0;
    std::array<int, 2> totals_{};
    std::array<std::vector<int>, 2> won_;
    std::array<CardMask, 2> shown_{};
    std::array<CardMask, 2> cannot_{};
    std::vector<PlayRecord> plays_;
    std::vector<HandRecord> sheet_;
    std::vector<GameEvent> events_;
    uint64_t matchSeed_ = 0;
    std::vector<LoggedAction> log_;
};

} // namespace altmisalti
