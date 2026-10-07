#pragma once
// Pişti rules engine: dealing, captures, piştiler, scoring, match. Pure logic: no raylib, no I/O,
// deterministic for a given seed and sequence of actions. Rules (Turkish): docs/kurallar_pisti.md.
//
// Cards and seats come from core/Cards.h: ids 0..51, seat 0 = bottom (human), play 0 -> 1 -> 2 -> 3,
// partners across (0 & 2, 1 & 3). Only the ACTIVE seats play: all four (Bireysel / Esli) or seats 0 and 2
// (Ikili: you vs the player across; seats 1 and 3 just watch).
//
// Scores are kept per SIDE: in Bireysel every active seat is its own side, in Esli the two teams are the
// sides (side 0 = seats 0 & 2, side 1 = seats 1 & 3), in Ikili side 0 = seat 0 and side 1 = seat 2.
//
// Visibility (what is public): every played card, the table's face-up cards, the opening card (and any
// Vale that was turned up and put under the deck), and the three closed table cards once they are
// captured (the engine reveals them to everyone at that moment: see GameEvent::cards of the Capture).
// Hidden: the other players' hands, the deck order, the closed cards before the first capture.
#include "core/Cards.h"
#include "core/Rng.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pisti {

using okey::Rng;

enum class Mode {
    Bireysel = 0, // 4 players, everyone for themselves (default at our table)
    Esli = 1,     // 4 players, partners across (0 & 2 vs 1 & 3)
    Ikili = 2     // 2 players: seat 0 vs seat 2
};

struct Rules {
    Mode mode = Mode::Bireysel;
    int targetScore = 101;          // match ends after the hand in which a side reaches this (51/101/151 common)
    int pistiPoints = 10;           // pişti: capturing a single-card pile with a card of the same rank
    int jackPistiPoints = 20;       // Vale on a single Vale ("valeli pişti", "çift pişti")
    int majorityPoints = 3;         // most cards (strictly most; a tie for the most gives nobody the 3)
    bool jackPistiOnAny = false;    // variant: Vale on a single NON-Vale card also counts as a pişti (10)
    bool lastCardPisti = false;     // variant: the very last card of the hand may make a pişti (usually not)
    bool reTurnOpeningJack = true;  // opening card is a Vale -> it goes under the deck and the next card is
                                    // turned up (repeat). Off: a Vale may be the opening card.
};

enum class Stage { NotStarted, Playing, HandOver, MatchOver };

// One line of the score sheet for one seat or one side. Every field except the counters (cards, pistis,
// jackPistis) is POINTS: aces = number of aces (1 each), sinek2 = 0 or 2, karo10 = 0 or 3, majority 0 or 3.
struct ScoreLine {
    int cards = 0;        // cards captured this hand (for the majority)
    int aces = 0;         // "Aslar"      1 point each
    int jacks = 0;        // "Valeler"    1 point each
    int sinek2 = 0;       // "Sinek 2"    2 points
    int karo10 = 0;       // "Karo 10"    3 points
    int majority = 0;     // "Çoğunluk"   3 points (side lines only; in Bireysel/Ikili also on the seat line)
    int pistis = 0;       // number of ordinary piştiler
    int jackPistis = 0;   // number of Vale-on-Vale piştiler
    int pistiPoints = 0;  // "Piştiler"   points from both kinds
    int total = 0;        // sum of the point fields
};

struct HandResult {
    int handIndex = -1;
    int dealer = -1;
    int lastCapturer = -1;              // took the cards left on the table at the end (-1: none left)
    int leftoverCards = 0;              // how many cards that was
    int numSides = 0;
    std::array<ScoreLine, 4> seat{};    // by seat (inactive seats: zeros). Majority only when side == seat.
    std::array<ScoreLine, 4> side{};    // by side (index < numSides) — what is added to the totals
    std::array<int, 4> totalsAfter{};   // running match totals by side after this hand
    int majoritySide = -1;              // side that got the majority points (-1: tie, nobody)
    bool matchOver = false;
    int winnerSide = -1;                // when matchOver
};

enum class EvType {
    MatchStart,   // new match. text
    HandStart,    // seat = dealer, count = hand index (0-based)
    Deal,         // seat = recipient (-1 = table), count = cards dealt from the deck, deckLeft = deck after.
                  //   Table deal: count 4 (3 closed + 1 face up after TableTurnUp). Human seats also get `cards`
                  //   (their own new cards); other seats' cards are never in events.
    TableTurnUp,  // card = the card turned face up on the table. jack = true: it is a Vale that goes under the
                  //   deck (reTurnOpeningJack) and another TableTurnUp follows.
    TurnStart,    // seat to play
    Play,         // seat played card (always face up)
    Capture,      // seat took the table: count = cards taken incl. the played one, card = the played card,
                  //   pisti flag, jack = taken with a Vale, cards = every card taken (incl. revealed closed cards), closed = how
                  //   many of them were the closed cards (first capture of the hand)
    Pisti,        // seat made a pişti: points (10/20), jack = Vale-on-Vale
    LastCapture,  // end of the deal: seat = who gets the rest of the table (last capturer), count, cards
    HandEnd,      // see lastHandResult(); count = hand index, side = majority side (-1 none), text = summary
    MatchEnd      // side = winner side, seat = first seat of that side
};

struct GameEvent {
    EvType type = EvType::TurnStart;
    int seat = -1;
    int side = -1;
    int card = -1;
    int count = 0;
    int points = 0;
    int deckLeft = -1;
    int closed = 0;
    bool pisti = false;
    bool jack = false;
    std::vector<int> cards;
    std::string text; // Turkish: "Pişti yaptın! +10" / "Hacı Rıza pişti yaptı! +10"
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One successful action of the match, as given to Game (a saved match is its seed plus these, replayed).
enum class LogKind { Play, NextHand };
struct LoggedAction {
    LogKind kind = LogKind::Play;
    int seat = -1;
    int card = -1;
    std::string encode() const;  // one text line: "kind seat card"
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& rules = Rules());

    // ---- setup ----
    void setRules(const Rules& r);                     // only between matches (ignored while one is running; resets seat set)
    const Rules& rules() const { return rules_; }
    void setPlayer(int seat, const std::string& name, bool human);
    const std::string& name(int seat) const { return names_[seat]; }
    bool isHuman(int seat) const { return human_[seat]; }
    void startMatch(uint64_t seed);                    // resets totals; deals hand 0
    void startNextHand();                              // after HandOver (not MatchOver); dealer moves on

    // ---- seats and sides ----
    const std::vector<int>& activeSeats() const { return active_; }
    bool isActive(int seat) const;
    int nextActive(int seat) const;                    // next active seat in play order
    int numSides() const { return numSides_; }
    int sideOf(int seat) const;                        // -1 for an inactive seat
    std::vector<int> seatsOfSide(int side) const;
    std::string sideName(int side) const;              // "Sen", "Hacı Rıza", "Sen ve Kel Mahmut"
    bool sameSide(int a, int b) const { return sideOf(a) >= 0 && sideOf(a) == sideOf(b); }

    // ---- state (everything here is public information except hand() of other seats: the UI shows only the
    //      human's hand, bots read only their own) ----
    Stage stage() const { return stage_; }
    bool matchOver() const { return stage_ == Stage::MatchOver; }
    int handIndex() const { return handIndex_; }
    int dealer() const { return dealer_; }
    int current() const { return current_; }           // seat to play (-1 when not Playing)
    int turnNumber() const { return turnNumber_; }     // increments at every TurnStart
    int dealRound() const { return dealRound_; }       // 0-based deal of 4 cards within the hand
    int dealRoundsPerHand() const { return 48 / (4 * (int)active_.size()); } // 3 (four players) or 6 (two)
    const std::vector<int>& hand(int seat) const { return hands_[seat]; }
    int handCount(int seat) const { return (int)hands_[seat].size(); }
    const std::vector<int>& tableCards() const { return open_; } // face-up pile, back() = top
    int tableTop() const { return open_.empty() ? -1 : open_.back(); }
    int closedCount() const { return (int)closed_.size(); }      // face-down cards under the pile (3 or 0)
    int tableCount() const { return (int)(open_.size() + closed_.size()); }
    int deckCount() const { return (int)deck_.size(); }
    // For drawing only (the UI shows these face down; bots must not read them): the closed cards under the pile
    // and the deck (back() = the next card dealt).
    const std::vector<int>& closedCardsForDisplay() const { return closed_; }
    const std::vector<int>& deckCardsForDisplay() const { return deck_; }
    const std::vector<int>& buriedCards() const { return buried_; } // opening Vales put under the deck (public;
                                                       // they are the deck's bottom cards -> dealt to the dealer)
    int capturedCount(int seat) const { return (int)captured_[seat].size(); }
    const std::vector<int>& capturedCards(int seat) const { return captured_[seat]; } // all were seen face up
    int sideCapturedCount(int side) const;
    int pistiCount(int seat) const { return pistis_[seat]; }     // this hand, both kinds
    int pistiPointsOf(int seat) const { return pistiPts_[seat]; }// this hand
    int lastCapturer() const { return lastCapturer_; }
    bool isSeen(int card) const { return kart::isValidCard(card) && seen_[card]; } // publicly seen this hand
    const std::array<bool, kart::NUM_CARDS>& seen() const { return seen_; }
    bool isLastCardOfHand() const;                     // the next play is the final card of the hand
    // UI/bot hints for `card` played now: takes the table? what pişti points (0 if none)?
    bool wouldCapture(int card) const;
    int wouldPisti(int card) const;
    const HandResult& lastHandResult() const { return lastResult_; }
    int total(int side) const { return side >= 0 && side < 4 ? totals_[side] : 0; }
    const std::array<int, 4>& totals() const { return totals_; }
    int winnerSide() const { return winnerSide_; }      // when matchOver()
    int leaderSide() const;                            // highest total (ties: lower side)

    // ---- action ----
    ActionResult playCard(int seat, int card);

    // ---- save / resume: the seed of startMatch and every successful action since (startNextHand included) ----
    uint64_t matchSeed() const { return seed_; }
    const std::vector<LoggedAction>& actionLog() const { return log_; }
    bool replay(const LoggedAction& a); // applies one logged action (false: it does not apply here)

    // ---- events ----
    std::vector<GameEvent> drainEvents();

    // ---- testing hooks: tests/ and tools/ only (never UI or bots). The caller keeps the 52 cards consistent
    //      (hands + table open/closed + deck + captured). ----
    std::vector<int>& debugHand(int seat) { return hands_[seat]; }
    std::vector<int>& debugOpen() { return open_; }
    std::vector<int>& debugClosed() { return closed_; }
    std::vector<int>& debugDeck() { return deck_; }    // back() = top (next card dealt)
    std::vector<int>& debugCaptured(int seat) { return captured_[seat]; }
    void debugSetTurn(int seat) { current_ = seat; stage_ = Stage::Playing; ++turnNumber_; }
    void debugSetLastCapturer(int seat) { lastCapturer_ = seat; }
    void debugSetDealer(int seat) { dealer_ = seat; }
    void debugSetTotal(int side, int v) { totals_[side] = v; }
    void debugMarkSeen(int card, bool s = true) { seen_[card] = s; }
    // The next startMatch/startNextHand uses this deck (back() = top) instead of shuffling.
    void debugSetNextDeck(const std::vector<int>& deck) { nextDeck_ = deck; }

private:
    ActionResult playCardImpl(int seat, int card);
    void setupSeats();
    void beginHand();
    void dealCards();                                  // one deal of 4 to every active seat
    void beginTurn(int seat);
    void endHand();
    void push(GameEvent e);
    std::string says(int seat, const std::string& third, const std::string& second) const;
    bool humanOnSide(int side) const;

    Rules rules_;
    std::array<std::string, 4> names_;
    std::array<bool, 4> human_{};
    std::vector<int> active_;
    std::array<int, 4> side_{};
    int numSides_ = 0;

    uint64_t seed_ = 0;
    int firstDealer_ = 0;
    Stage stage_ = Stage::NotStarted;
    int handIndex_ = 0;
    int dealer_ = 0;
    int current_ = -1;
    int turnNumber_ = 0;
    int dealRound_ = 0;
    std::array<std::vector<int>, 4> hands_;
    std::array<std::vector<int>, 4> captured_;
    std::array<int, 4> pistis_{};
    std::array<int, 4> jackPistis_{};
    std::array<int, 4> pistiPts_{};
    std::vector<int> open_;
    std::vector<int> closed_;
    std::vector<int> deck_;
    std::vector<int> buried_;
    std::vector<int> nextDeck_;
    std::array<bool, kart::NUM_CARDS> seen_{};
    int lastCapturer_ = -1;
    std::array<int, 4> totals_{};
    int winnerSide_ = -1;
    HandResult lastResult_;
    std::vector<GameEvent> events_;
    std::vector<LoggedAction> log_;
};

// Point value of one card in the hand score (As 1, Vale 1, Sinek 2 = 2, Karo 10 = 3, others 0).
int cardPoints(int card);

} // namespace pisti
