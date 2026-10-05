#pragma once
// King (Türk usulü "King", a.k.a. Rıfkı) — four-player trick-taking game engine. Pure logic: no raylib, no
// I/O, deterministic for a given seed. Rules: docs/kurallar_king.md. Bots: src/core/KingBot.h.
//
// Cards, suits and seats come from core/Cards.h (namespace kart): ids 0..51, seat 0 = bottom (human),
// play goes 0 -> 1 -> 2 -> 3 (counter-clockwise, "sağdan").
//
// A match ("parti") is Rules::numHands() hands ("el", default 20). Every hand: shuffle and deal 13 each,
// then the chooser ("seçen", rotating 0 -> 1 -> 2 -> 3 starting with the Karo 2 holder of the first deal)
// picks a contract — a penalty game ("ceza") or a trump game ("koz" + suit) — and leads the first trick.
// Each player chooses kozPerPlayer (2) trump games and cezaPerPlayer (3) penalty games; each penalty type may
// be chosen at most maxPerCeza (2) times in the whole match. Penalty points are negative, trump tricks
// positive; with the default numbers a full match sums to exactly zero.
//
// Typical UI loop:
//   g.startMatch(seed);                 // deals hand 0, stage Choosing
//   while (g.stage() != Stage::MatchOver) {
//       Choosing: show g.options(g.chooser()); human -> g.chooseContract(0, c, suit)
//       Playing:  highlight g.legalCards(0); g.playCard(g.current(), card)
//       HandOver: show the çetele (g.sheet()), then g.startNextHand()
//       drain g.drainEvents() every frame for animations / texts
//   }
#include "core/Cards.h"
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace king {

using okey::Rng;

// ---------------------------------------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------------------------------------

enum class Contract : int {
    ElAlmaz = 0,   // every trick -50
    KupaAlmaz = 1, // every heart -30
    ErkekAlmaz = 2,// every Papaz / Vale -60
    KizAlmaz = 3,  // every Kız -100
    Rifki = 4,     // the Kupa Papaz -320
    SonIki = 5,    // each of the last two tricks -180
    Koz = 6        // trump game: every trick +50
};
constexpr int NUM_CONTRACTS = 7;
constexpr int NUM_CEZA = 6; // contracts 0..5 are penalties

inline bool isCeza(Contract c) { return c != Contract::Koz; }
// Penalty decided by specific cards (Kupa, Erkek, Kız, Rıfkı) — these may end early once all are out.
inline bool isCardCeza(Contract c) {
    return c == Contract::KupaAlmaz || c == Contract::ErkekAlmaz || c == Contract::KizAlmaz || c == Contract::Rifki;
}
const char* contractNameTR(Contract c);  // "El Almaz", "Kupa Almaz", "Erkek Almaz", "Kız Almaz", "Rıfkı",
                                         // "Son İki", "Koz"
std::string contractLabelTR(Contract c, int trump); // "Kız Almaz", "Koz (Maça)"

// ---------------------------------------------------------------------------------------------------------
// Rules (all numbers data driven; defaults = the common Turkish "20 el" King)
// ---------------------------------------------------------------------------------------------------------

struct Rules {
    int kozPerPlayer = 2;   // trump games each player must choose
    int cezaPerPlayer = 3;  // penalty games each player must choose
    int maxPerCeza = 2;     // how many times one penalty type may be chosen in a match
    // Points per unit, indexed by Contract (magnitude; penalties are written negative, koz positive):
    // El almaz per trick, Kupa per heart, Erkek per K/J, Kız per Q, Rıfkı, Son iki per trick, Koz per trick.
    std::array<int, NUM_CONTRACTS> unit{{50, 30, 60, 100, 320, 180, 50}};
    int firstChooser = -1;  // -1: holder of the Karo 2 in the first deal; 0..3: that seat
    bool firstRoundCezaOnly = false;      // variant: the first round (one choice each) must be penalties
    bool endEarly = true;                 // card penalties stop once every penalty card has fallen
    // Penalty games
    bool voidMustDiscardPenalty = true;   // void in the led suit: must throw a Kız / erkek / Rıfkı (else a
                                          // kupa in Rıfkı) if you hold one
    bool kupaAlmazVoidMustHeart = true;   // Kupa almaz: void in the led suit -> must throw a kupa
    bool forceDropUnderHigher = true;     // Kız/Erkek/Rıfkı: a higher card of the led suit is on the table
                                          // and you hold that suit's penalty card -> you must play it
    bool heartLeadNeedsBreak = true;      // Kupa almaz / Rıfkı: no kupa lead until a kupa has been played
                                          // (unless you hold only kupa)
    // Trump games
    bool trumpLeadNeedsBreak = true;      // no trump lead until a trump has been played (unless only trumps)
    bool mustTrumpWhenVoid = true;        // void in the led suit -> must play a trump if you hold one
    bool mustRaiseOnTrumpLead = true;     // trump led: must beat the highest trump on the table if you can
    bool mustOvertrumpWhenRuffing = false;// ruffing over another ruff: must overtrump if you can
    bool mustBeatLedSuit = false;         // plain suit: must beat the highest card (Batak style; off in King)

    int numPlayers() const { return 4; }
    int choicesPerPlayer() const { return kozPerPlayer + cezaPerPlayer; }
    int numHands() const { return 4 * choicesPerPlayer(); }
    int points(Contract c) const { return isCeza(c) ? -unit[(int)c] : unit[(int)c]; }
    // Sanity: penalty slots must cover every player's penalty choices (else someone could be stuck).
    bool valid() const {
        return kozPerPlayer >= 0 && cezaPerPlayer >= 0 && choicesPerPlayer() > 0 && maxPerCeza >= 0 &&
               NUM_CEZA * maxPerCeza >= 4 * cezaPerPlayer;
    }
};

// ---------------------------------------------------------------------------------------------------------
// Card masks and the shared play rules (the engine and the bots use the same functions)
// ---------------------------------------------------------------------------------------------------------

using CardMask = uint64_t;
constexpr CardMask ALL_CARDS = (1ull << 52) - 1;
inline CardMask cardBit(int c) { return 1ull << c; }
inline CardMask suitMask(int s) { return 0x1FFFull << (13 * s); }
inline int popcount(CardMask m) { return __builtin_popcountll(m); }
inline int lowestCard(CardMask m) { return m ? __builtin_ctzll(m) : -1; }
inline int highestCard(CardMask m) { return m ? 63 - __builtin_clzll(m) : -1; }
CardMask maskOf(const std::vector<int>& cards);
std::vector<int> cardsOf(CardMask m); // ascending ids

// Cards worth points in a card penalty (Kupa: all hearts, Erkek: K+J, Kız: Q, Rıfkı: Kupa Papaz); 0 otherwise.
CardMask penaltyCards(Contract c);
// Points a single card costs its taker in contract c (<= 0; 0 for trick-based contracts and koz).
int cardPoints(const Rules& r, Contract c, int card);
// Points written on the winner of trick number `trickIndex` (0..12) containing `cards`.
int trickPoints(const Rules& r, Contract c, CardMask cards, int trickIndex);
// Index (0..n-1, in play order) of the card currently winning `cards[0..n-1]`.
int trickWinnerIndex(const int* cards, int n, Contract c, int trump);
constexpr int RIFKI = 1 * 13 + (13 - 2); // Kupa Papaz (kart::makeCard(kart::Kupa, kart::Papaz))

// Which restriction made a card illegal (ActionResult texts come from these).
enum class PlayRule : int {
    None = 0,
    FollowSuit,      // must follow the led suit
    BeatLed,         // (variant) must beat the highest card of the led suit
    RaiseTrump,      // trump led: must play a higher trump
    MustTrump,       // void: must trump
    Overtrump,       // (variant) void: must overtrump
    LeadNoTrump,     // trump not yet broken
    LeadNoHeart,     // kupa not yet broken (Kupa almaz / Rıfkı)
    DropUnder,       // higher card on the table: must play your Kız / erkek / Rıfkı of that suit
    VoidQueen,       // void: must throw a Kız
    VoidErkek,       // void: must throw a Papaz / Vale
    VoidRifki,       // void: must throw the Rıfkı
    VoidHeart        // void: must throw a kupa (Rıfkı / Kupa almaz)
};

// The state needed to decide what may be played next.
struct TrickContext {
    Contract contract = Contract::ElAlmaz;
    int trump = -1;            // suit in a koz game, else -1
    int n = 0;                 // cards already in the trick
    int cards[4] = {-1, -1, -1, -1}; // in play order
    bool heartsBroken = false; // a kupa was played earlier this hand
    bool trumpBroken = false;  // a trump was played earlier this hand
};

// Legality as an ordered list of filters: the legal set is hand & filter[k] for the FIRST k where that is
// non-empty (if none, the whole hand). Bots use the same list to infer what an opponent cannot hold.
struct PlayFilter {
    CardMask mask = 0;
    PlayRule rule = PlayRule::None;
};
constexpr int MAX_FILTERS = 6;
int playFilters(const Rules& r, const TrickContext& t, PlayFilter out[MAX_FILTERS]);
CardMask legalMask(const Rules& r, const TrickContext& t, CardMask hand);
// Why `card` (held in `hand`) may not be played: None if legal.
PlayRule whyIllegal(const Rules& r, const TrickContext& t, CardMask hand, int card);

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

enum class Stage { NotStarted, Choosing, Playing, HandOver, MatchOver };

struct TrickCard {
    int seat = -1;
    int card = -1;
};

struct Trick {
    int index = 0;                 // 0..12 within the hand
    int leader = -1;
    std::vector<TrickCard> cards;  // play order
    int winner = -1;               // -1 while in progress
    int points = 0;                // written on the winner (negative in ceza, +50 in koz)
};

// One play of the current hand, in order (public history).
struct PlayRecord {
    int seat = -1;
    int card = -1;
    int trick = 0;
};

// What the chooser may pick (one entry per contract, in Contract order; Koz is one entry — the UI then asks
// for the suit).
struct ContractOption {
    Contract contract = Contract::ElAlmaz;
    bool allowed = false;
    int remaining = 0;   // ceza: times it can still be chosen in this match (by anyone);
                         // koz: koz choices this seat has left
    std::string reason;  // why not allowed (Turkish), empty if allowed
};

// One row of the çetele (score sheet).
struct HandRecord {
    int index = 0;                 // 0-based hand number
    int chooser = -1;
    Contract contract = Contract::ElAlmaz;
    int trump = -1;
    std::string label;             // "Kız Almaz", "Koz (Maça)"
    std::array<int, 4> points{};   // this hand
    std::array<int, 4> totals{};   // running totals after this hand
    std::array<int, 4> tricks{};   // tricks taken
    std::array<int, 4> units{};    // penalty units taken (tricks / cards), or koz tricks
    int tricksPlayed = 13;
    bool endedEarly = false;
};

struct SeatInfo {
    std::string name;
    bool human = false;
    std::vector<int> hand;         // sorted ascending (suit Maça, Kupa, Karo, Sinek; rank 2..A)
    int tricks = 0;                // tricks taken this hand
    std::vector<int> taken;        // penalty cards taken this hand (card penalties only)
    int handPoints = 0;            // points this hand so far
    int total = 0;                 // running match total
    int kozUsed = 0;               // koz games chosen so far
    int cezaUsed = 0;              // penalty games chosen so far
};

enum class EvType {
    MatchStart,     // new match
    HandStart,      // seat = chooser, amount = hand index (0-based)
    Deal,           // cards dealt (amount = 13); hands: Game::hand(seat)
    ContractChosen, // seat = chooser, contract, trump (koz) — text e.g. "Emekli Nuri kız almaz seçti"
    TurnStart,      // seat = player to play
    Play,           // seat played card
    TrickWon,       // seat = winner, cards = trick (play order), amount = points written (0 if none)
    PenaltyCard,    // seat took `card` worth `amount` (< 0), e.g. "Rıfkıyı sen aldın! −320"
    HandEndEarly,   // every penalty card has fallen; the hand stops here (text says why)
    HandEnd,        // seat = -1; points = per-seat hand points; lines = per-seat result lines
    MatchEnd        // seat = winner (highest total, ties: lower seat); points = totals; lines = ranking
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int card = -1;
    Contract contract = Contract::ElAlmaz;
    int trump = -1;
    int amount = 0;
    std::vector<int> cards;
    std::array<int, 4> points{};
    std::string text;                // short Turkish line for the HUD / log
    std::vector<std::string> lines;  // HandEnd / MatchEnd details
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);          // only between matches (ignored while a match is running)
    const Rules& rules() const { return rules_; }
    void setPlayer(int seat, const std::string& name, bool human);
    void startMatch(uint64_t seed);         // resets the sheet; deals hand 0 -> Choosing
    void startNextHand();                   // HandOver -> next deal (Choosing)

    // ---- queries (public information, except hand(seat) of other seats: the UI shows only the human's
    //      hand and bots read only their own) ----
    Stage stage() const { return stage_; }
    int handIndex() const { return handIndex_; }        // 0-based
    int numHands() const { return rules_.numHands(); }
    int chooser() const { return chooser_; }
    int current() const { return current_; }            // chooser in Choosing, player to play in Playing
    Contract contract() const { return contract_; }     // valid from Playing on
    int trump() const { return trump_; }                // suit in a koz game, else -1
    std::string contractLabel() const { return contractLabelTR(contract_, trump_); }
    const SeatInfo& seat(int s) const { return seats_[s]; }
    const std::string& name(int s) const { return seats_[s].name; }
    bool isHuman(int s) const { return seats_[s].human; }
    const std::vector<int>& hand(int s) const { return seats_[s].hand; }
    int handSize(int s) const { return (int)seats_[s].hand.size(); }

    // Choosing
    std::vector<ContractOption> options(int seat) const;  // all 7 entries with allowed / remaining / reason
    bool canChoose(int seat, Contract c, std::string* why = nullptr) const;
    int timesChosen(Contract c) const { return timesChosen_[(int)c]; }
    int cezaRemaining(Contract c) const;     // how many more times this penalty may be chosen (match)
    int kozLeft(int s) const { return rules_.kozPerPlayer - seats_[s].kozUsed; }
    int cezaLeft(int s) const { return rules_.cezaPerPlayer - seats_[s].cezaUsed; }

    // Playing
    const Trick& currentTrick() const { return trick_; }  // cards played so far in this trick
    const Trick& lastTrick() const { return lastTrick_; } // last completed trick (winner >= 0), for display
    const std::vector<Trick>& tricks() const { return tricks_; } // completed tricks of this hand
    const std::vector<PlayRecord>& plays() const { return plays_; } // every play of this hand, in order
    int trickNumber() const { return (int)tricks_.size(); } // completed tricks
    int ledSuit() const;                                  // -1 when the trick is empty
    int trickWinnerSoFar() const;                         // seat currently winning the trick, -1 if empty
    bool heartsBroken() const { return heartsBroken_; }
    bool trumpBroken() const { return trumpBroken_; }
    CardMask playedMask() const { return played_; }       // every card played this hand
    bool shownVoid(int s, int suit) const { return (voids_[s] >> suit) & 1; } // failed to follow suit
    TrickContext trickContext() const;
    std::vector<int> legalCards(int seat) const;          // empty unless it is seat's turn to play
    bool isLegal(int seat, int card) const;

    // Score
    const std::vector<HandRecord>& sheet() const { return sheet_; } // çetele: one row per finished hand
    int total(int s) const { return seats_[s].total; }
    int handPoints(int s) const { return seats_[s].handPoints; }
    std::array<int, 4> ranking() const;                  // seats by total, best first (ties: lower seat)
    int leaderSeat() const { return ranking()[0]; }

    // ---- actions ----
    // trump: suit 0..3, required for Koz, ignored otherwise.
    ActionResult chooseContract(int seat, Contract c, int trump = -1);
    ActionResult playCard(int seat, int card);

    // ---- events ----
    std::vector<GameEvent> drainEvents();

    // ---- testing hooks (tests/ and tools/ only; never UI or bots) ----
    // Replace the hands of the current deal (Choosing stage); the caller keeps the 52 cards consistent.
    void debugSetHands(const std::array<std::vector<int>, 4>& hands);
    void debugSetChooser(int seat) { chooser_ = current_ = seat; }
    void debugSetUsage(int seat, int kozUsed, int cezaUsed) { seats_[seat].kozUsed = kozUsed; seats_[seat].cezaUsed = cezaUsed; }
    void debugSetTimesChosen(Contract c, int n) { timesChosen_[(int)c] = n; }
    void debugSetHandIndex(int i) { handIndex_ = i; }
    void debugSetTotal(int seat, int v) { seats_[seat].total = v; }

private:
    void dealHand();
    void push(GameEvent e);
    void beginTurn(int seat);
    void finishTrick();
    void endHand(bool early);
    bool isSen(int s) const { return seats_[s].human; }
    std::string says(int s, const std::string& third, const std::string& second) const;
    std::string earlyEndText() const;

    Rules rules_;
    std::array<SeatInfo, 4> seats_;
    Rng rng_;
    Stage stage_ = Stage::NotStarted;
    int handIndex_ = 0;
    int firstChooser_ = 0;
    int chooser_ = 0;
    int current_ = 0;
    Contract contract_ = Contract::ElAlmaz;
    int trump_ = -1;
    Trick trick_;
    Trick lastTrick_;
    std::vector<Trick> tricks_;
    std::vector<PlayRecord> plays_;
    bool heartsBroken_ = false;
    bool trumpBroken_ = false;
    CardMask played_ = 0;
    std::array<int, 4> voids_{};
    std::array<int, NUM_CONTRACTS> timesChosen_{};
    std::vector<HandRecord> sheet_;
    std::vector<GameEvent> events_;
};

} // namespace king
