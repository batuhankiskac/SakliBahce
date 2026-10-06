#pragma once
// İhaleli Batak (tekli, 4 kişi) and Eşli İhaleli Batak (ortaklar karşılıklı: 0 & 2, 1 & 3) engine.
// Pure logic: no raylib, no I/O, deterministic for a given seed. Rules summary: docs/kurallar_batak.md.
//
// Seats as in kart:: (Cards.h): 0 = bottom (human), 1 = right, 2 = across, 3 = left; play, dealing and
// bidding go 0 -> 1 -> 2 -> 3 (counter-clockwise, "sağdan"). The dealer rotates to the right every hand.
//
// Flow of one hand (Stage):
//   Bidding        the dealer's right (nextSeat(dealer)) speaks first; each player bids more than the
//                  current high bid or passes ("pas"); a player who passed is out of the ihale. Bidding goes
//                  round and round until only the high bidder is left (or someone says 13). If all four
//                  pass, Rules::allPass decides (default: the first bidder must take it with allPassBid).
//   ChoosingTrump  the ihale winner (declarer) names the koz.
//   Playing        the declarer leads the first trick; the trick winner leads the next. In eşli with
//                  Rules::openDummy the declarer's partner lays the cards face up and the declarer plays them
//                  (controllerOf(dummy) == declarer).
//   HandOver       after 13 tricks; startNextHand() deals the next hand.
//   MatchOver      target score / hand limit reached or a made 13 bid ("king").
//
// Scores are kept per SIDE: tekli 4 sides (side == seat), eşli 2 sides (side 0 = seats 0 & 2, side 1 = 1 & 3).
#include "core/Cards.h"
#include "core/Rng.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace batak {

using okey::Rng;

enum class AllPass {
    FirstBidder, // the dealer's right, who spoke first, takes the ihale with allPassBid (most common)
    Dealer,      // the dealer takes it with allPassBid
    Redeal       // cards are collected and dealt again by the next dealer
};

struct Rules {
    bool esli = false;            // partners across (0 & 2 vs 1 & 3), bids and tricks per team
    int minBid = 5;               // lowest bid (tekli 5, eşli 8)
    int allPassBid = 4;           // forced contract when everybody passes (tekli 4, eşli 7)
    AllPass allPass = AllPass::FirstBidder;
    bool openDummy = true;        // eşli: after the koz is named the declarer's partner opens the hand and the
                                  // declarer plays it ("eşinin kağıtları yere açılır")
    int defenderMinTricks = 1;    // a non-declaring side below this many tricks batar (-contract)
    int multiplier = 1;           // 1 = kahvehane hesabı (el başına 1); 10 = onluk hesap
    bool kingEndsMatch = true;    // bidding 13 and taking all 13 wins the match at once
    int targetScore = 51;         // match ends after the hand in which a side reaches this (0 = no target)
    int numHands = 0;             // hard limit on hands (0 = none); at least one of these two must be > 0
    // Play rules (all on by default; switches exist for house variants and tests).
    bool mustBeat = true;             // following suit you must play higher than the best card of the led suit
    bool mustBeatEvenIfTrumped = false; // ...even when a koz has already been played on the trick
    bool mustTrump = true;            // void in the led suit: you must play a koz if you have one
    bool mustOvertrump = true;        // ...and a higher koz than the best one already on the trick, if you can
    bool trumpMustBeBroken = true;    // no koz lead before a koz has been played ("koz açılmadan koz atılmaz")
    int firstDealer = -1;             // -1 = drawn from the match seed

    static Rules tekli() { return Rules(); }
    static Rules esliBatak() {
        Rules r;
        r.esli = true;
        r.minBid = 8;
        r.allPassBid = 7;
        return r;
    }
};

enum class Stage { NotStarted, Bidding, ChoosingTrump, Playing, HandOver, MatchOver };

struct PlayedCard {
    int seat = -1;
    int card = -1;
};

struct Trick {
    int leader = -1;
    int winner = -1;
    std::vector<PlayedCard> cards; // in play order, cards[0] is the lead
};

struct BidRecord {
    int seat = -1;
    int value = 0; // 0 = pas
};

// One line of the hand's score sheet: one per side (tekli: per seat, eşli: per team).
struct SideResult {
    int side = -1;
    std::vector<int> seats;   // seats of the side
    bool declarer = false;    // this side held the ihale
    int bid = 0;              // the contract for the declaring side, else 0
    int tricks = 0;           // tricks taken by the side
    bool made = true;         // declarer: tricks >= bid; others: tricks >= defenderMinTricks
    int points = 0;           // points written this hand (+tricks*mult or -contract*mult)
    std::string text;         // Turkish line, e.g. "Kel Mahmut battı! 5 el, -7" / "İhaleyi yaptın: 8 el, +8"
};

struct HandResult {
    int handIndex = -1;
    int dealer = -1;
    int declarer = -1;
    int contract = 0;
    int trump = -1;
    bool forced = false;      // everybody passed, contract was forced
    bool king = false;        // 13 bid and made
    std::array<int, 4> tricks{};  // per seat
    std::array<int, 4> points{};  // per seat (eşli: both partners show the team's points)
    std::vector<SideResult> sides; // tekli: 4 (by seat), eşli: 2 (side 0, side 1)
};

enum class EvType {
    MatchStart,  // new match
    HandStart,   // seat = dealer, value = hand index (0-based)
    Deal,        // seat = dealer; cards are in hand(seat) (UI shows only the human's)
    Redeal,      // all passed under AllPass::Redeal; a HandStart + Deal follow with the next dealer
    TurnStart,   // seat = seat to act, stage = what is expected (Bidding / ChoosingTrump / Playing)
    Bid,         // seat bid `value` (0 = pas)
    BiddingWon,  // seat = declarer, value = contract, forced = everybody passed
    TrumpChosen, // seat = declarer, suit = koz
    DummyOpen,   // eşli openDummy: seat = the dummy, cards = its hand (face up from now on)
    Play,        // seat played `card`
    TrumpBroken, // seat played the first koz of the hand (card)
    TrickWon,    // seat = winner, value = trick number (1..13), cards = the 4 cards in play order
    HandEnd,     // see lastHandResult(); lines = per-side texts
    MatchEnd     // seat = a seat of the winning side (-1 on a tie), value = winning side
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int card = -1;
    int value = 0;
    int suit = -1;
    bool forced = false;
    Stage stage = Stage::NotStarted;
    std::vector<PlayedCard> cards;
    std::vector<std::string> lines;
    std::string text; // Turkish; 2nd person for the human ("İhaleyi 7 ile aldın"), 3rd with names for bots
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One successful action of the match, as given to Game (a saved match is its seed plus these, replayed).
enum class LogKind { Bid, Pass, Trump, Play, NextHand };
struct LoggedAction {
    LogKind kind = LogKind::Pass;
    int seat = -1;
    int value = -1;              // Bid: the bid, Trump: the suit, Play: the card
    std::string encode() const;  // one text line: "kind seat value"
    static bool decode(const std::string& line, LoggedAction& out);
};

// ---- card-set helpers (bit i = card id i) shared by the engine and the bots ----
inline uint64_t bit(int card) { return 1ull << card; }
inline uint64_t suitMask(int suit) { return 0x1FFFull << (13 * suit); }
// Cards of the same suit ranked above `card`.
inline uint64_t aboveMask(int card) { return suitMask(kart::suitOf(card)) & ~((bit(card) << 1) - 1); }
inline int popcount(uint64_t m) { return __builtin_popcountll(m); }
inline int lowestCard(uint64_t m) { return m ? __builtin_ctzll(m) : -1; }
inline int highestCard(uint64_t m) { return m ? 63 - __builtin_clzll(m) : -1; }
uint64_t maskOf(const std::vector<int>& cards);
std::vector<int> cardsOf(uint64_t mask); // ascending ids (= by suit, then rank)

// What the trick so far demands of the next player.
struct TrickView {
    int ledSuit = -1;   // -1: the next card leads
    int bestLed = -1;   // highest card of the led suit on the trick
    int bestTrump = -1; // highest koz on the trick when the led suit is not koz, else -1
    int winner = -1;    // seat currently winning the trick
    int winningCard = -1;
};
TrickView viewTrick(const std::vector<PlayedCard>& trick, int trump);
// Does `card` (played next) take the lead over the trick's current winning card?
bool beatsTrick(int card, const TrickView& v, int trump);
// The legal cards of `hand` given the trick so far (the single source of truth for the play rules).
uint64_t legalMask(uint64_t hand, const TrickView& v, int trump, bool trumpBroken, const Rules& r);
// Points of a side for the hand. `declaring`: this side held the ihale.
int sidePoints(const Rules& r, bool declaring, int tricks, int contract);

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);                 // only between matches
    const Rules& rules() const { return rules_; }
    void setPlayer(int seat, const std::string& name, bool human);
    const std::string& name(int seat) const { return names_[seat]; }
    bool isHuman(int seat) const { return human_[seat]; }
    void startMatch(uint64_t seed);                // resets scores, deals hand 0
    void startNextHand();                          // after HandOver (not MatchOver)

    // ---- state ----
    Stage stage() const { return stage_; }
    int handIndex() const { return handIndex_; }   // 0-based
    int dealer() const { return dealer_; }
    int firstBidder() const { return kart::nextSeat(dealer_); }
    int current() const { return current_; }       // seat to act (bid / koz / card), -1 if none
    // Who decides for `seat`: the declarer for the open dummy, else the seat itself. Drive the turn with
    // controllerOf(current()) (bot or human UI) and pass current() as the seat of the action.
    int controllerOf(int seat) const;
    int exposedSeat() const { return dummy_; }     // the open dummy (eşli), -1 if none: everybody sees it
    const std::vector<int>& hand(int seat) const { return hands_[seat]; } // sorted by suit then rank
    uint64_t handMask(int seat) const { return maskOf(hands_[seat]); }

    // ---- sides / scores ----
    int numSides() const { return rules_.esli ? 2 : 4; }
    int sideOf(int seat) const { return rules_.esli ? seat % 2 : seat; }
    std::vector<int> sideSeats(int side) const;
    int total(int seat) const { return totals_[sideOf(seat)]; }  // cumulative score of the seat's side
    int sideTotal(int side) const { return totals_[side]; }
    const std::vector<HandResult>& handResults() const { return results_; }
    const HandResult& lastHandResult() const { return lastResult_; }
    int leaderSide() const;                        // highest total (ties: lower side)
    int winnerSide() const { return winnerSide_; } // MatchOver: winning side, -1 for a tie
    std::string sideName(int side) const;          // "Kel Mahmut" / "Kel Mahmut ile Cemal"

    // ---- bidding ----
    const std::vector<BidRecord>& bids() const { return bids_; } // this hand, in order
    int highBid() const { return highBid_; }       // 0 = nobody bid yet
    int highBidder() const { return highBidder_; }
    bool hasPassed(int seat) const { return passed_[seat]; }
    int lastBidOf(int seat) const;                 // last bid value of the seat this hand (0 none / pas)
    std::vector<int> legalBids(int seat) const;    // ascending values; empty when not this seat's bid turn
    bool canPass(int seat) const;                  // pas is always allowed on your bid turn

    // ---- contract ----
    int declarer() const { return declarer_; }     // -1 before the ihale is decided
    int contract() const { return contract_; }
    bool forcedContract() const { return forced_; }
    int trump() const { return trump_; }           // -1 before it is named
    bool trumpBroken() const { return trumpBroken_; }

    // ---- play ----
    const std::vector<PlayedCard>& trick() const { return trick_; } // cards on the table, play order
    int ledSuit() const { return trick_.empty() ? -1 : kart::suitOf(trick_[0].card); }
    int trickLeader() const { return leader_; }
    int trickWinnerSoFar() const;                  // seat winning the current trick, -1 if empty
    int trickNumber() const { return (int)tricks_.size() + 1; } // 1..13 during play
    const std::vector<Trick>& tricks() const { return tricks_; } // completed tricks of this hand
    const Trick* lastTrick() const { return tricks_.empty() ? nullptr : &tricks_.back(); }
    int tricksWon(int seat) const { return tricksWon_[seat]; }
    int sideTricks(int side) const;
    uint64_t playedMask() const { return played_; } // every card played this hand (incl. current trick)
    std::vector<int> legalCards(int seat) const;   // empty when it is not this seat's card turn
    ActionResult checkPlay(int seat, int card) const; // the error playCard() would give, without playing

    // ---- actions ----
    ActionResult bid(int seat, int value);
    ActionResult pass(int seat);
    ActionResult chooseTrump(int seat, int suit);
    ActionResult playCard(int seat, int card);     // seat = owner of the card (== current())

    // ---- save / resume: the seed of startMatch and every successful action since (startNextHand included) ----
    uint64_t matchSeed() const { return matchSeed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a); // applies one logged action (false: it does not apply here)

    // ---- events ----
    std::vector<GameEvent> drainEvents();
    const std::vector<GameEvent>& pendingEvents() const { return events_; }

    // ---- testing hooks: tests/ and tools/ only (never UI or bots) ----
    // Restart the current hand's bidding with this dealer and these hands (4 x 13 distinct cards).
    void debugRedeal(int dealer, const std::array<std::vector<int>, 4>& hands);
    // Jump straight into play: hands (equal sizes), contract, koz, leader (-1 = declarer), koz broken.
    void debugStartPlay(const std::array<std::vector<int>, 4>& hands, int declarer, int contract, int trump,
                        int leader = -1, bool trumpBroken = false);
    void debugSetTotals(const std::array<int, 4>& sideTotals) { totals_ = sideTotals; }
    void debugSetHandIndex(int i) { handIndex_ = i; }

private:
    ActionResult logged(ActionResult r, LoggedAction a);
    ActionResult bidImpl(int seat, int value);
    ActionResult passImpl(int seat);
    ActionResult chooseTrumpImpl(int seat, int suit);
    ActionResult playCardImpl(int seat, int card);
    void dealHand(int dealer, const std::array<std::vector<int>, 4>* fixed);
    void resetHandState();
    void finishBidding(int seat, int value, bool forced);
    void beginTurn(int seat);
    void completeTrick();
    void endHand();
    void endMatch(int winner);
    void push(GameEvent e);
    std::string says(int seat, const std::string& third, const std::string& second) const;
    std::string sideSays(int side, const std::string& third, const std::string& second) const;
    bool sideHasHuman(int side) const;
    ActionResult checkTurn(int seat, Stage need) const;

    Rules rules_;
    std::array<std::string, 4> names_{{"Sen", "Oyuncu 2", "Oyuncu 3", "Oyuncu 4"}};
    std::array<bool, 4> human_{{true, false, false, false}};
    Rng rng_;
    Stage stage_ = Stage::NotStarted;
    int handIndex_ = 0;
    int dealer_ = 0;
    int current_ = -1;
    std::array<std::vector<int>, 4> hands_;
    std::vector<BidRecord> bids_;
    std::array<bool, 4> passed_{};
    int highBid_ = 0;
    int highBidder_ = -1;
    int declarer_ = -1;
    int contract_ = 0;
    bool forced_ = false;
    int trump_ = -1;
    bool trumpBroken_ = false;
    int dummy_ = -1;
    std::vector<PlayedCard> trick_;
    int leader_ = -1;
    std::vector<Trick> tricks_;
    std::array<int, 4> tricksWon_{};
    uint64_t played_ = 0;
    std::array<int, 4> totals_{};
    std::vector<HandResult> results_;
    HandResult lastResult_;
    int winnerSide_ = -1;
    std::vector<GameEvent> events_;
    uint64_t matchSeed_ = 0;
    std::vector<LoggedAction> log_;
};

} // namespace batak
