// Pişti engine + bot tests for SaklıBahçe: dealing, captures, piştiler, closed cards, last capture, scoring,
// modes, match end, event texts, determinism, a random fuzzer and bot sanity/fairness checks.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Pisti.cpp src/core/PistiBot.cpp tests/test_pisti.cpp
//        -o build/pisti/test_pisti
#include "core/Pisti.h"
#include "core/PistiBot.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace pisti;
using kart::makeCard;
using kart::rankOf;

namespace {

int g_checks = 0;
int g_failures = 0;

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    if (g_failures <= 60) std::printf("%s:%d: CHECK failed: %s\n", file, line, what.c_str());
    else if (g_failures == 61) std::printf("... further failures suppressed\n");
}

template <class T>
std::string show(const T& v) {
    std::ostringstream o;
    if constexpr (std::is_enum_v<T>) o << (int)v;
    else o << v;
    return o.str();
}
std::string show(bool b) { return b ? "true" : "false"; }
std::string show(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

} // namespace

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, #cond);                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        const auto va_ = (a);                                                                                    \
        const auto vb_ = (b);                                                                                    \
        if (!(va_ == vb_))                                                                                       \
            reportFailure(__FILE__, __LINE__,                                                                    \
                          std::string(#a " == " #b "   [") + show(va_) + " vs " + show(vb_) + "]");            \
    } while (0)

namespace {

using V = std::vector<int>;
using kart::Karo;
using kart::Kupa;
using kart::Maca;
using kart::Sinek;
constexpr int VALE = kart::Vale, KIZ = kart::Kiz, PAPAZ = kart::Papaz, AS = kart::As;

int C(int suit, int rank) { return makeCard(suit, rank); }

bool contains(const V& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::vector<GameEvent> eventsOf(const std::vector<GameEvent>& ev, EvType t) {
    std::vector<GameEvent> out;
    for (const GameEvent& e : ev)
        if (e.type == t) out.push_back(e);
    return out;
}

// Every card exactly once across hands, table (open + closed), deck and captured piles.
bool allCardsOnce(Game& g, std::string* why = nullptr) {
    std::array<int, 52> n{};
    auto add = [&](const V& v) {
        for (int c : v)
            if (c >= 0 && c < 52) ++n[c];
    };
    for (int s = 0; s < 4; ++s) {
        add(g.debugHand(s));
        add(g.debugCaptured(s));
    }
    add(g.debugOpen());
    add(g.debugClosed());
    add(g.debugDeck());
    for (int c = 0; c < 52; ++c)
        if (n[c] != 1) {
            if (why) *why = kart::cardNameTR(c) + " x" + std::to_string(n[c]);
            return false;
        }
    return true;
}

// A started game with everything emptied, ready to be filled by a test (turn to `turn`).
void blank(Game& g, int turn = 0, int dealer = 3) {
    g.startMatch(1);
    g.drainEvents();
    for (int s = 0; s < 4; ++s) {
        g.debugHand(s).clear();
        g.debugCaptured(s).clear();
    }
    g.debugOpen().clear();
    g.debugClosed().clear();
    g.debugDeck().clear();
    for (int c = 0; c < 52; ++c) g.debugMarkSeen(c, false);
    g.debugSetLastCapturer(-1);
    g.debugSetDealer(dealer);
    g.debugSetTurn(turn);
}

// Puts every card not placed anywhere into `seat`'s captured pile (keeps the 52 consistent).
void restTo(Game& g, int seat) {
    std::array<bool, 52> used{};
    auto mark = [&](const V& v) {
        for (int c : v) used[c] = true;
    };
    for (int s = 0; s < 4; ++s) {
        mark(g.debugHand(s));
        mark(g.debugCaptured(s));
    }
    mark(g.debugOpen());
    mark(g.debugClosed());
    mark(g.debugDeck());
    for (int c = 0; c < 52; ++c)
        if (!used[c]) g.debugCaptured(seat).push_back(c);
}

// ---------------------------------------------------------------------------------------------------------

void testDealing() {
    {
        Game g;
        g.startMatch(42);
        const auto ev = g.drainEvents();
        CHECK_EQ(g.stage(), Stage::Playing);
        for (int s = 0; s < 4; ++s) CHECK_EQ(g.handCount(s), 4);
        CHECK_EQ(g.tableCount(), 4);
        CHECK_EQ(g.closedCount(), 3);
        CHECK_EQ((int)g.tableCards().size(), 1);
        CHECK(rankOf(g.tableTop()) != VALE);
        CHECK_EQ(g.deckCount(), 32);
        CHECK_EQ(g.dealRound(), 0);
        CHECK_EQ(g.dealRoundsPerHand(), 3);
        CHECK_EQ(g.current(), g.nextActive(g.dealer()));
        CHECK(allCardsOnce(g));
        // Event order: MatchStart, HandStart, Deal(table), TableTurnUp..., 4 x Deal(seat), TurnStart.
        CHECK(ev.size() >= 9);
        CHECK_EQ(ev[0].type, EvType::MatchStart);
        CHECK_EQ(ev[1].type, EvType::HandStart);
        CHECK_EQ(ev[1].seat, g.dealer());
        CHECK_EQ(ev[2].type, EvType::Deal);
        CHECK_EQ(ev[2].seat, -1);
        CHECK_EQ(ev[2].count, 4);
        const auto deals = eventsOf(ev, EvType::Deal);
        CHECK_EQ((int)deals.size(), 5);
        CHECK_EQ(deals[1].seat, g.nextActive(g.dealer()));
        CHECK_EQ(deals[4].seat, g.dealer()); // the dealer gets his cards last
        CHECK_EQ(deals[4].deckLeft, 32);
        for (size_t i = 1; i < deals.size(); ++i) {
            CHECK_EQ(deals[i].count, 4);
            // Only the human's own cards travel in the event.
            CHECK_EQ(deals[i].cards.size(), g.isHuman(deals[i].seat) ? (size_t)4 : (size_t)0);
        }
        const auto deal0 = std::find_if(deals.begin(), deals.end(), [](const GameEvent& e) { return e.seat == 0; });
        CHECK(deal0 != deals.end() && deal0->cards == g.hand(0));
        const auto ups = eventsOf(ev, EvType::TableTurnUp);
        CHECK(!ups.empty());
        CHECK_EQ(ups.back().card, g.tableTop());
        CHECK(!ups.back().jack);
        CHECK_EQ(ev.back().type, EvType::TurnStart);
        CHECK(g.isSeen(g.tableTop()));
        CHECK(!g.isSeen(g.debugClosed()[0]));

        // Redeal when the hands are empty: three deals of four in a four-player hand.
        int deals2 = 0;
        while (g.stage() == Stage::Playing) {
            const int before = g.deckCount();
            g.playCard(g.current(), g.hand(g.current()).front());
            if (g.deckCount() == before - 16) {
                ++deals2;
                for (int s = 0; s < 4; ++s) CHECK_EQ(g.handCount(s), 4);
            }
            CHECK(allCardsOnce(g));
        }
        CHECK_EQ(deals2, 2);
        CHECK_EQ(g.deckCount(), 0);
    }
    {
        Rules r;
        r.mode = Mode::Ikili;
        Game g(r);
        g.startMatch(7);
        g.drainEvents();
        CHECK_EQ(g.activeSeats(), (V{0, 2}));
        CHECK_EQ(g.handCount(0), 4);
        CHECK_EQ(g.handCount(2), 4);
        CHECK_EQ(g.handCount(1), 0);
        CHECK_EQ(g.handCount(3), 0);
        CHECK_EQ(g.deckCount(), 40);
        CHECK_EQ(g.dealRoundsPerHand(), 6);
        CHECK(g.dealer() == 0 || g.dealer() == 2);
        CHECK_EQ(g.nextActive(0), 2);
        CHECK_EQ(g.nextActive(2), 0);
        int rounds = 1;
        while (g.stage() == Stage::Playing) {
            const int before = g.deckCount();
            g.playCard(g.current(), g.hand(g.current()).front());
            if (g.deckCount() == before - 8) ++rounds;
        }
        CHECK_EQ(rounds, 6);
        CHECK(allCardsOnce(g));
    }
}

// Builds a deck (back() = top) whose 4th card from the top is `open` and the rest follows.
V deckWithOpening(const V& topCards) {
    V d;
    std::array<bool, 52> used{};
    for (int c : topCards) used[c] = true;
    for (int c = 0; c < 52; ++c)
        if (!used[c]) d.push_back(c);
    for (auto it = topCards.rbegin(); it != topCards.rend(); ++it) d.push_back(*it);
    return d; // topCards[0] is the top
}

void testOpeningJack() {
    const int mv = C(Maca, VALE), kv = C(Kupa, VALE), k9 = C(Karo, 9);
    {
        Game g;
        // closed: 3 cards, then the turned card is Maça Vale, then Kupa Vale, then Karo 9.
        g.debugSetNextDeck(deckWithOpening({C(Kupa, 2), C(Kupa, 3), C(Kupa, 4), mv, kv, k9}));
        g.startMatch(5);
        const auto ev = g.drainEvents();
        const auto ups = eventsOf(ev, EvType::TableTurnUp);
        CHECK_EQ((int)ups.size(), 3);
        CHECK(ups[0].jack && ups[0].card == mv);
        CHECK(ups[1].jack && ups[1].card == kv);
        CHECK(!ups[2].jack && ups[2].card == k9);
        CHECK_EQ(g.tableTop(), k9);
        CHECK_EQ(g.closedCount(), 3);
        CHECK_EQ(g.buriedCards(), (V{mv, kv}));
        CHECK_EQ(g.debugDeck()[0], kv); // bottom of the deck
        CHECK_EQ(g.debugDeck()[1], mv);
        CHECK(!g.isSeen(mv));
        CHECK(ups[0].text.find("destenin altına") != std::string::npos);
        CHECK(allCardsOnce(g));
        // The buried Valeler are the last cards of the deck: they reach the dealer in the last deal.
        const int dealer = g.dealer();
        while (g.stage() == Stage::Playing) {
            if (g.deckCount() == 0 && g.dealRound() == 2 && g.handCount(dealer) == 4) {
                CHECK(contains(g.hand(dealer), mv));
                CHECK(contains(g.hand(dealer), kv));
                break;
            }
            g.playCard(g.current(), g.hand(g.current()).front());
        }
    }
    {
        Rules r;
        r.reTurnOpeningJack = false;
        Game g(r);
        g.debugSetNextDeck(deckWithOpening({C(Kupa, 2), C(Kupa, 3), C(Kupa, 4), mv, kv}));
        g.startMatch(5);
        g.drainEvents();
        CHECK_EQ(g.tableTop(), mv);
        CHECK(g.buriedCards().empty());
    }
}

void testCaptures() {
    // Capture by rank.
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7), C(Maca, 3)};
        g.debugHand(0) = {C(Sinek, 3), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK(g.wouldCapture(C(Sinek, 3)));
        CHECK(!g.wouldCapture(C(Karo, 5)));
        CHECK_EQ(g.wouldPisti(C(Sinek, 3)), 0);
        const int before = g.capturedCount(0);
        CHECK(g.playCard(0, C(Sinek, 3)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ(g.capturedCount(0), before + 3);
        CHECK_EQ(g.tableCount(), 0);
        CHECK_EQ(g.tableTop(), -1);
        CHECK_EQ(g.lastCapturer(), 0);
        const auto caps = eventsOf(ev, EvType::Capture);
        CHECK_EQ((int)caps.size(), 1);
        CHECK_EQ(caps[0].count, 3);
        CHECK(!caps[0].pisti);
        CHECK(!caps[0].jack);
        CHECK_EQ(caps[0].text, std::string("Yerdeki 2 kartı aldın"));
        CHECK(eventsOf(ev, EvType::Pisti).empty());
        CHECK_EQ(g.current(), 1);
        CHECK(allCardsOnce(g));
    }
    // No capture: the card goes on top.
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7)};
        g.debugHand(0) = {C(Karo, 5), C(Karo, 6)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK(g.playCard(0, C(Karo, 5)).ok);
        CHECK_EQ(g.tableTop(), C(Karo, 5));
        CHECK_EQ(g.tableCount(), 2);
        CHECK(eventsOf(g.drainEvents(), EvType::Capture).empty());
    }
    // Vale takes any pile (no pişti by default, even on a single card).
    {
        Game g;
        g.setPlayer(1, "Hacı Rıza", false);
        blank(g, 1);
        g.debugOpen() = {C(Kupa, 7), C(Maca, 3), C(Karo, AS)};
        g.debugHand(1) = {C(Karo, VALE), C(Kupa, 9)};
        g.debugHand(2) = {C(Kupa, 10)};
        restTo(g, 3);
        CHECK(g.wouldCapture(C(Karo, VALE)));
        CHECK(g.playCard(1, C(Karo, VALE)).ok);
        const auto caps = eventsOf(g.drainEvents(), EvType::Capture);
        CHECK_EQ((int)caps.size(), 1);
        CHECK(caps[0].jack);
        CHECK(!caps[0].pisti);
        CHECK_EQ(caps[0].count, 4);
        CHECK_EQ(caps[0].text, std::string("Hacı Rıza Vale ile yerdeki 3 kartı aldı"));
    }
    // Vale on an empty table just lies there.
    {
        Game g;
        blank(g, 0);
        g.debugHand(0) = {C(Karo, VALE), C(Kupa, 9)};
        g.debugHand(1) = {C(Kupa, 10)};
        restTo(g, 3);
        CHECK(!g.wouldCapture(C(Karo, VALE)));
        CHECK(g.playCard(0, C(Karo, VALE)).ok);
        CHECK_EQ(g.tableTop(), C(Karo, VALE));
        CHECK_EQ(g.lastCapturer(), -1);
    }
}

void testPisti() {
    // Ordinary pişti: 10.
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7)};
        g.debugHand(0) = {C(Sinek, 7), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK_EQ(g.wouldPisti(C(Sinek, 7)), 10);
        CHECK_EQ(g.wouldPisti(C(Karo, 5)), 0);
        CHECK(g.playCard(0, C(Sinek, 7)).ok);
        const auto ev = g.drainEvents();
        const auto ps = eventsOf(ev, EvType::Pisti);
        CHECK_EQ((int)ps.size(), 1);
        CHECK_EQ(ps[0].points, 10);
        CHECK(!ps[0].jack);
        CHECK_EQ(ps[0].text, std::string("Pişti yaptın! +10"));
        CHECK(eventsOf(ev, EvType::Capture)[0].pisti);
        CHECK_EQ(g.pistiCount(0), 1);
        CHECK_EQ(g.pistiPointsOf(0), 10);
    }
    // A bot's pişti, third person with the name.
    {
        Game g;
        g.setPlayer(1, "Hacı Rıza", false);
        blank(g, 1);
        g.debugOpen() = {C(Kupa, PAPAZ)};
        g.debugHand(1) = {C(Maca, PAPAZ), C(Karo, 5)};
        g.debugHand(2) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK(g.playCard(1, C(Maca, PAPAZ)).ok);
        const auto ps = eventsOf(g.drainEvents(), EvType::Pisti);
        CHECK_EQ((int)ps.size(), 1);
        CHECK_EQ(ps[0].text, std::string("Hacı Rıza pişti yaptı! +10"));
    }
    // Vale on a single Vale: 20.
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, VALE)};
        g.debugHand(0) = {C(Maca, VALE), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK_EQ(g.wouldPisti(C(Maca, VALE)), 20);
        CHECK(g.playCard(0, C(Maca, VALE)).ok);
        const auto ev = g.drainEvents();
        const auto ps = eventsOf(ev, EvType::Pisti);
        CHECK_EQ((int)ps.size(), 1);
        CHECK_EQ(ps[0].points, 20);
        CHECK(ps[0].jack);
        CHECK_EQ(ps[0].text, std::string("Valeyle pişti yaptın! +20"));
        CHECK_EQ(eventsOf(ev, EvType::Capture)[0].text, std::string("Yerdeki 1 kartı aldın"));
    }
    // Vale on a single non-Vale: a capture, not a pişti (variant jackPistiOnAny: 10).
    for (int variant = 0; variant < 2; ++variant) {
        Rules r;
        r.jackPistiOnAny = variant == 1;
        Game g(r);
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7)};
        g.debugHand(0) = {C(Maca, VALE), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK_EQ(g.wouldPisti(C(Maca, VALE)), variant ? 10 : 0);
        CHECK(g.playCard(0, C(Maca, VALE)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ((int)eventsOf(ev, EvType::Capture).size(), 1);
        CHECK_EQ((int)eventsOf(ev, EvType::Pisti).size(), variant);
    }
    // Two cards of the same rank on the table: not a pişti.
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7), C(Karo, 7)};
        g.debugHand(0) = {C(Sinek, 7), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK_EQ(g.wouldPisti(C(Sinek, 7)), 0);
        CHECK(g.playCard(0, C(Sinek, 7)).ok);
        CHECK(eventsOf(g.drainEvents(), EvType::Pisti).empty());
    }
    // A single open card over closed cards is not a pişti (first capture of the hand).
    {
        Game g;
        blank(g, 0);
        g.debugOpen() = {C(Kupa, 7)};
        g.debugClosed() = {C(Maca, 2), C(Maca, 4), C(Maca, 6)};
        g.debugHand(0) = {C(Sinek, 7), C(Karo, 5)};
        g.debugHand(1) = {C(Kupa, 9)};
        restTo(g, 3);
        CHECK_EQ(g.wouldPisti(C(Sinek, 7)), 0);
    }
}

void testClosedCards() {
    Game g;
    g.setPlayer(1, "Hacı Rıza", false);
    blank(g, 1);
    const V closed = {C(Maca, 2), C(Kupa, AS), C(Karo, 10)};
    g.debugOpen() = {C(Kupa, 7)};
    g.debugClosed() = closed;
    g.debugHand(1) = {C(Sinek, 7), C(Karo, 5)};
    g.debugHand(2) = {C(Kupa, 9)};
    restTo(g, 3);
    CHECK_EQ(g.tableCount(), 4);
    CHECK_EQ(g.closedCount(), 3);
    for (int c : closed) CHECK(!g.isSeen(c));
    CHECK(g.playCard(1, C(Sinek, 7)).ok);
    const auto caps = eventsOf(g.drainEvents(), EvType::Capture);
    CHECK_EQ((int)caps.size(), 1);
    CHECK_EQ(caps[0].count, 5);
    CHECK_EQ(caps[0].closed, 3);
    for (int c : closed) {
        CHECK(contains(caps[0].cards, c));
        CHECK(g.isSeen(c)); // revealed to everyone at the capture
        CHECK(contains(g.capturedCards(1), c));
    }
    CHECK_EQ(g.closedCount(), 0);
    CHECK(!caps[0].pisti);
    CHECK(caps[0].text.find("kapalılar: Maça 2, Kupa As, Karo 10") != std::string::npos);
    CHECK(allCardsOnce(g));
}

// End of a hand: seat 0 to play its last card, deck empty; table holds `open`.
void setupLastTrick(Game& g, const V& open, int lastCapturer) {
    blank(g, 2, 1);
    g.debugOpen() = open;
    g.debugHand(2) = {C(Kupa, 4)};
    g.debugHand(3) = {C(Kupa, 5)};
    g.debugHand(0) = {C(Kupa, 6)};
    g.debugSetLastCapturer(lastCapturer);
}

void testLastCapture() {
    {
        Game g;
        g.setPlayer(1, "Hacı Rıza", false);
        setupLastTrick(g, {C(Maca, 9), C(Karo, AS)}, 1);
        g.debugHand(1) = {C(Kupa, 7)};
        g.debugSetTurn(1);
        restTo(g, 3);
        CHECK(g.playCard(1, C(Kupa, 7)).ok);
        CHECK(g.playCard(2, C(Kupa, 4)).ok);
        CHECK(g.playCard(3, C(Kupa, 5)).ok);
        CHECK(g.isLastCardOfHand());
        CHECK(g.playCard(0, C(Kupa, 6)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ(g.stage(), Stage::HandOver);
        const auto lc = eventsOf(ev, EvType::LastCapture);
        CHECK_EQ((int)lc.size(), 1);
        CHECK_EQ(lc[0].seat, 1);
        CHECK_EQ(lc[0].count, 6);
        CHECK_EQ(lc[0].text, std::string("Yerde kalan 6 kart son alan Hacı Rıza'ya gitti"));
        CHECK_EQ(g.lastHandResult().lastCapturer, 1);
        CHECK_EQ(g.lastHandResult().leftoverCards, 6);
        CHECK(contains(g.capturedCards(1), C(Karo, AS)));
        CHECK_EQ(g.tableCount(), 0);
        CHECK(allCardsOnce(g));
        CHECK_EQ(eventsOf(ev, EvType::HandEnd).size(), (size_t)1);
    }
    // The human was the last capturer.
    {
        Game g;
        setupLastTrick(g, {C(Maca, 9)}, 0);
        restTo(g, 3);
        g.playCard(2, C(Kupa, 4));
        g.playCard(3, C(Kupa, 5));
        g.playCard(0, C(Kupa, 6));
        const auto lc = eventsOf(g.drainEvents(), EvType::LastCapture);
        CHECK_EQ((int)lc.size(), 1);
        CHECK_EQ(lc[0].text, std::string("Yerde kalan 4 kart sana kaldı (son alan sendin)"));
    }
    // Nobody captured at all: the dealer gets them.
    {
        Game g;
        setupLastTrick(g, {C(Maca, 9)}, -1);
        restTo(g, 3);
        g.playCard(2, C(Kupa, 4));
        g.playCard(3, C(Kupa, 5));
        g.playCard(0, C(Kupa, 6));
        CHECK_EQ(g.lastHandResult().lastCapturer, 1); // dealer
    }
    // The last card of the hand cannot make a pişti (variant: it can).
    for (int variant = 0; variant < 2; ++variant) {
        Rules r;
        r.lastCardPisti = variant == 1;
        Game g(r);
        blank(g, 0, 0);
        g.debugOpen() = {C(Maca, 6)};
        g.debugHand(0) = {C(Kupa, 6)};
        restTo(g, 3);
        CHECK(g.isLastCardOfHand());
        CHECK_EQ(g.wouldPisti(C(Kupa, 6)), variant ? 10 : 0);
        CHECK(g.playCard(0, C(Kupa, 6)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ((int)eventsOf(ev, EvType::Capture).size(), 1);
        CHECK_EQ((int)eventsOf(ev, EvType::Pisti).size(), variant);
        CHECK(eventsOf(ev, EvType::LastCapture).empty());
        CHECK_EQ(g.lastHandResult().seat[0].pistiPoints, variant ? 10 : 0);
    }
}

void testScoring() {
    // Bireysel. Seat 0 holds all aces and Sinek 2; seat 1 Karo 10 and two Valeler; seat 2 the other two
    // Valeler; seat 3 the most cards. One pişti for seat 2 and one Vale pişti for seat 1 recorded earlier.
    Game g;
    blank(g, 0, 3);
    g.debugCaptured(0) = {C(Maca, AS), C(Kupa, AS), C(Karo, AS), C(Sinek, AS), C(Sinek, 2)};
    g.debugCaptured(1) = {C(Karo, 10), C(Maca, VALE), C(Kupa, VALE)};
    g.debugCaptured(2) = {C(Karo, VALE), C(Sinek, VALE), C(Maca, 3)};
    g.debugHand(0) = {C(Maca, 4)};
    restTo(g, 3); // the other 40 cards; Maça 4 joins them as the leftover
    g.debugSetLastCapturer(3);
    CHECK(g.playCard(0, C(Maca, 4)).ok); // last card: lands on the empty table, goes to seat 3 (last capturer)
    const HandResult& r = g.lastHandResult();
    CHECK_EQ(r.numSides, 4);
    CHECK_EQ(r.seat[0].aces, 4);
    CHECK_EQ(r.seat[0].sinek2, 2);
    CHECK_EQ(r.seat[0].jacks, 0);
    CHECK_EQ(r.seat[0].karo10, 0);
    CHECK_EQ(r.seat[0].total, 6);
    CHECK_EQ(r.seat[1].karo10, 3);
    CHECK_EQ(r.seat[1].jacks, 2);
    CHECK_EQ(r.seat[1].total, 5);
    CHECK_EQ(r.seat[2].jacks, 2);
    CHECK_EQ(r.seat[2].total, 2);
    CHECK_EQ(r.seat[3].cards, 52 - 5 - 3 - 3);
    CHECK_EQ(r.seat[3].majority, 3);
    CHECK_EQ(r.seat[3].total, 3);
    CHECK_EQ(r.majoritySide, 3);
    int totalCards = 0, totalPts = 0;
    for (int s = 0; s < 4; ++s) {
        totalCards += r.seat[s].cards;
        totalPts += r.seat[s].total;
        CHECK_EQ(r.side[s].total, r.seat[s].total);
        CHECK_EQ(g.total(s), r.seat[s].total);
        CHECK_EQ(r.totalsAfter[s], r.seat[s].total);
    }
    CHECK_EQ(totalCards, 52);
    CHECK_EQ(totalPts, 13 + 3);
    CHECK_EQ(g.stage(), Stage::HandOver);
    const auto he = eventsOf(g.drainEvents(), EvType::HandEnd);
    CHECK_EQ((int)he.size(), 1);
    CHECK_EQ(he[0].text, std::string("1. el bitti · Sen 6, Hacı Rıza 5, Kel Mahmut 2, Emekli Nuri 3"));

    // Piştiler in the breakdown: a real pişti and a Vale pişti during a hand, then the end.
    Game h;
    blank(h, 0, 3);
    h.debugOpen() = {C(Kupa, 7)};
    h.debugHand(0) = {C(Sinek, 7)};
    h.debugHand(1) = {C(Kupa, VALE)};
    h.debugHand(2) = {C(Maca, VALE)};
    h.debugHand(3) = {C(Karo, 2)};
    restTo(h, 3);
    CHECK(h.playCard(0, C(Sinek, 7)).ok);   // pişti +10 for seat 0
    CHECK(h.playCard(1, C(Kupa, VALE)).ok); // lands on the empty table
    CHECK(h.playCard(2, C(Maca, VALE)).ok); // Vale pişti +20 for seat 2
    CHECK(h.playCard(3, C(Karo, 2)).ok);    // last card, stays -> seat 2 (last capturer)
    const HandResult& q = h.lastHandResult();
    CHECK_EQ(q.seat[0].pistis, 1);
    CHECK_EQ(q.seat[0].jackPistis, 0);
    CHECK_EQ(q.seat[0].pistiPoints, 10);
    CHECK_EQ(q.seat[2].pistis, 0);
    CHECK_EQ(q.seat[2].jackPistis, 1);
    CHECK_EQ(q.seat[2].pistiPoints, 20);
    CHECK_EQ(q.seat[2].jacks, 2);
    CHECK_EQ(q.seat[2].cards, 3);
    CHECK_EQ(q.seat[0].total, 10);
    CHECK_EQ(q.seat[2].total, 22);
}

void testMajority() {
    // Eşli: 26 - 26 -> nobody gets the majority.
    {
        Rules r;
        r.mode = Mode::Esli;
        Game g(r);
        blank(g, 0, 3);
        for (int c = 0; c < 52; ++c) {
            if (c == C(Maca, 2)) continue;
            g.debugCaptured(c < 26 ? 0 : (c < 39 ? 1 : 3)).push_back(c);
        }
        // seat 0: 25 cards (without Maça 2), seats 1 + 3: 26; playing Maça 2 -> table -> last capturer seat 2.
        g.debugHand(0) = {C(Maca, 2)};
        g.debugSetLastCapturer(2);
        CHECK(g.playCard(0, C(Maca, 2)).ok);
        const HandResult& h = g.lastHandResult();
        CHECK_EQ(h.side[0].cards, 26);
        CHECK_EQ(h.side[1].cards, 26);
        CHECK_EQ(h.majoritySide, -1);
        CHECK_EQ(h.side[0].majority, 0);
        CHECK_EQ(h.side[1].majority, 0);
    }
    // Eşli: partners' cards count together; 27 beats 25.
    {
        Rules r;
        r.mode = Mode::Esli;
        Game g(r);
        blank(g, 0, 3);
        for (int c = 0; c < 52; ++c) {
            if (c == C(Maca, 2)) continue;
            g.debugCaptured(c < 14 ? 0 : c < 27 ? 2 : c < 40 ? 1 : 3).push_back(c);
        }
        g.debugHand(0) = {C(Maca, 2)};
        g.debugSetLastCapturer(2);
        CHECK(g.playCard(0, C(Maca, 2)).ok);
        const HandResult& h = g.lastHandResult();
        CHECK_EQ(h.side[0].cards, 27);
        CHECK_EQ(h.majoritySide, 0);
        CHECK_EQ(h.side[0].majority, 3);
        CHECK_EQ(h.seat[0].majority, 0); // eşli: majority is on the team line only
        CHECK_EQ(h.side[0].cards, h.seat[0].cards + h.seat[2].cards);
        CHECK_EQ(h.side[0].total + h.side[1].total, 16);
        CHECK_EQ(g.total(0), h.side[0].total);
        CHECK_EQ(g.sideName(0), std::string("Sen ve Kel Mahmut"));
        CHECK_EQ(g.sideName(1), std::string("Hacı Rıza ve Emekli Nuri"));
    }
    // Bireysel: a tie for the most between two players -> nobody.
    {
        Game g;
        blank(g, 0, 3);
        for (int c = 0; c < 52; ++c) {
            if (c == C(Maca, 2)) continue;
            g.debugCaptured(c < 17 ? 0 : c < 34 ? 1 : c < 44 ? 2 : 3).push_back(c);
        }
        // seat 0: 16 cards + Maça 2 back as the last capturer = 17, seat 1: 17, seat 2: 10, seat 3: 8.
        g.debugHand(0) = {C(Maca, 2)};
        g.debugSetLastCapturer(0);
        CHECK(g.playCard(0, C(Maca, 2)).ok);
        const HandResult& h = g.lastHandResult();
        int best = 0;
        for (int s = 0; s < 4; ++s) best = std::max(best, h.seat[s].cards);
        int nBest = 0;
        for (int s = 0; s < 4; ++s) nBest += h.seat[s].cards == best;
        CHECK_EQ(nBest, 2);
        CHECK_EQ(h.majoritySide, -1);
        for (int s = 0; s < 4; ++s) CHECK_EQ(h.seat[s].majority, 0);
    }
}

void testModesAndErrors() {
    {
        Rules r;
        r.mode = Mode::Ikili;
        Game g(r);
        g.setPlayer(2, "Kel Mahmut", false);
        g.startMatch(3);
        g.drainEvents();
        CHECK_EQ(g.numSides(), 2);
        CHECK_EQ(g.sideOf(0), 0);
        CHECK_EQ(g.sideOf(2), 1);
        CHECK_EQ(g.sideOf(1), -1);
        CHECK(!g.isActive(1));
        CHECK(!g.playCard(1, 0).ok);
        CHECK_EQ(g.playCard(1, 0).error, std::string("Bu oyuncu bu oyunda yok"));
        CHECK_EQ(g.sideName(1), std::string("Kel Mahmut"));
    }
    {
        Rules r;
        r.mode = Mode::Esli;
        Game g(r);
        g.startMatch(3);
        CHECK_EQ(g.numSides(), 2);
        CHECK(g.sameSide(0, 2));
        CHECK(g.sameSide(1, 3));
        CHECK(!g.sameSide(0, 1));
        CHECK_EQ(g.seatsOfSide(1), (V{1, 3}));
    }
    {
        Game g;
        g.setPlayer(1, "Hacı Rıza", false);
        CHECK_EQ(g.playCard(0, 0).error, std::string("Şu an kart oynanmıyor"));
        blank(g, 1);
        g.debugHand(0) = {C(Kupa, 2)};
        g.debugHand(1) = {C(Kupa, 3)};
        restTo(g, 3);
        CHECK_EQ(g.playCard(0, C(Kupa, 2)).error, std::string("Sıra Hacı Rıza'da"));
        g.debugSetTurn(0);
        CHECK_EQ(g.playCard(0, C(Kupa, 3)).error, std::string("Bu kart elinde yok"));
        CHECK_EQ(g.playCard(0, 99).error, std::string("Bu kart elinde yok"));
        CHECK(g.playCard(0, C(Kupa, 2)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ(eventsOf(ev, EvType::Play)[0].text, std::string("Kupa 2 attın"));
        CHECK_EQ(eventsOf(ev, EvType::TurnStart)[0].text, std::string("Sıra Hacı Rıza'da"));
        CHECK(g.playCard(1, C(Kupa, 3)).ok);
        CHECK_EQ(eventsOf(g.drainEvents(), EvType::Play)[0].text, std::string("Hacı Rıza Kupa 3 attı"));
    }
    {
        // Deal / TurnStart texts.
        Game g;
        g.setPlayer(1, "Hacı Rıza", false);
        g.setPlayer(2, "Kel Mahmut", false);
        g.setPlayer(3, "Emekli Nuri", false);
        g.startMatch(11);
        for (const GameEvent& e : g.drainEvents()) {
            if (e.type == EvType::Deal && e.seat == 0) CHECK_EQ(e.text, std::string("Sana 4 kart geldi"));
            if (e.type == EvType::Deal && e.seat == 1) CHECK_EQ(e.text, std::string("Hacı Rıza'ya 4 kart"));
            if (e.type == EvType::Deal && e.seat == 2) CHECK_EQ(e.text, std::string("Kel Mahmut'a 4 kart"));
            if (e.type == EvType::Deal && e.seat == 3) CHECK_EQ(e.text, std::string("Emekli Nuri'ye 4 kart"));
            if (e.type == EvType::TurnStart && e.seat == 0) CHECK_EQ(e.text, std::string("Sıra sende"));
            if (e.type == EvType::TurnStart && e.seat == 2) CHECK_EQ(e.text, std::string("Sıra Kel Mahmut'ta"));
            if (e.type == EvType::TurnStart && e.seat == 3) CHECK_EQ(e.text, std::string("Sıra Emekli Nuri'de"));
            if (e.type == EvType::Deal && e.seat == -1) CHECK_EQ(e.text, std::string("Yere 4 kart: 3 kapalı, 1 açık"));
        }
    }
}

void testMatchEnd() {
    // Reaching the target with a clear lead ends the match.
    {
        Rules r;
        r.mode = Mode::Ikili;
        r.targetScore = 101;
        Game g(r);
        g.setPlayer(2, "Kel Mahmut", false);
        blank(g, 0, 2);
        g.debugSetTotal(0, 95);
        g.debugSetTotal(1, 50);
        g.debugCaptured(0) = {C(Maca, AS), C(Kupa, AS), C(Karo, AS), C(Sinek, AS), C(Sinek, 2)};
        g.debugHand(0) = {C(Maca, 4)};
        g.debugSetLastCapturer(0);
        restTo(g, 2);
        CHECK(g.playCard(0, C(Maca, 4)).ok);
        CHECK_EQ(g.stage(), Stage::MatchOver);
        CHECK(g.matchOver());
        CHECK_EQ(g.total(0), 101);
        CHECK_EQ(g.winnerSide(), 0);
        CHECK(g.lastHandResult().matchOver);
        const auto me = eventsOf(g.drainEvents(), EvType::MatchEnd);
        CHECK_EQ((int)me.size(), 1);
        CHECK_EQ(me[0].text, std::string("Maç bitti! Kazandın! (101)"));
        g.startNextHand(); // ignored after the match
        CHECK_EQ(g.stage(), Stage::MatchOver);
    }
    // A bot wins.
    {
        Rules r;
        r.mode = Mode::Ikili;
        Game g(r);
        g.setPlayer(2, "Kel Mahmut", false);
        blank(g, 0, 2);
        g.debugSetTotal(1, 150);
        g.debugHand(0) = {C(Maca, 4)};
        g.debugSetLastCapturer(2);
        restTo(g, 2);
        g.playCard(0, C(Maca, 4));
        const auto me = eventsOf(g.drainEvents(), EvType::MatchEnd);
        CHECK_EQ((int)me.size(), 1);
        CHECK(me[0].text.rfind("Maç bitti! Kel Mahmut kazandı (", 0) == 0);
        CHECK_EQ(me[0].side, 1);
        CHECK_EQ(me[0].seat, 2);
    }
    // A tie on top above the target: one more hand.
    {
        Rules r;
        r.mode = Mode::Ikili;
        Game g(r);
        blank(g, 0, 2);
        // seat 0 gets 4 aces + Sinek 2 = 6, seat 2 gets the rest: Valeler 4 + Karo 10 3 + majority 3 = 10.
        g.debugCaptured(0) = {C(Maca, AS), C(Kupa, AS), C(Karo, AS), C(Sinek, AS), C(Sinek, 2)};
        g.debugHand(0) = {C(Maca, 4)};
        g.debugSetLastCapturer(2);
        restTo(g, 2);
        g.debugSetTotal(0, 104);
        g.debugSetTotal(1, 100);
        g.playCard(0, C(Maca, 4));
        CHECK_EQ(g.total(0), 110);
        CHECK_EQ(g.total(1), 110);
        CHECK_EQ(g.stage(), Stage::HandOver);
        CHECK(!g.lastHandResult().matchOver);
        g.drainEvents();
        g.startNextHand();
        CHECK_EQ(g.stage(), Stage::Playing);
        CHECK_EQ(g.handIndex(), 1);
        CHECK(g.dealer() == 0); // dealer moved on from seat 2
        CHECK(allCardsOnce(g));
    }
    // A full match with random plays ends, totals = sum of hand results, dealer rotates.
    {
        Game g;
        g.startMatch(99);
        okey::Rng rng(5);
        std::array<int, 4> sum{};
        int prevDealer = g.dealer();
        int hands = 0;
        while (true) {
            while (g.stage() == Stage::Playing) {
                const V& h = g.hand(g.current());
                g.playCard(g.current(), h[rng.range((int)h.size())]);
            }
            ++hands;
            for (int i = 0; i < 4; ++i) sum[i] += g.lastHandResult().side[i].total;
            if (g.matchOver()) break;
            g.startNextHand();
            CHECK_EQ(g.dealer(), g.nextActive(prevDealer));
            prevDealer = g.dealer();
        }
        for (int i = 0; i < 4; ++i) CHECK_EQ(sum[i], g.total(i));
        CHECK(g.total(g.winnerSide()) >= 101);
        CHECK(hands >= 2);
        g.drainEvents();
    }
}

std::vector<std::string> transcript(uint64_t seed, Mode mode) {
    Rules r;
    r.mode = mode;
    Game g(r);
    for (int s = 1; s < 4; ++s) g.setPlayer(s, "Bot" + std::to_string(s), false);
    g.startMatch(seed);
    std::array<Bot, 4> bots{Bot(BotLevel::Usta, 1), Bot(BotLevel::Acemi, 2), Bot(BotLevel::Kurt, 3),
                            Bot(BotLevel::Usta, 4)};
    std::vector<std::string> out;
    for (int hand = 0; hand < 3 && !g.matchOver(); ++hand) {
        if (hand) g.startNextHand();
        while (g.stage() == Stage::Playing) {
            for (const GameEvent& e : g.drainEvents()) {
                out.push_back(e.text);
                for (int s : g.activeSeats()) bots[s].observe(e, g);
            }
            const int s = g.current();
            g.playCard(s, bots[s].next(g, s));
        }
        for (const GameEvent& e : g.drainEvents()) out.push_back(e.text);
        out.push_back(std::to_string(g.total(0)) + "/" + std::to_string(g.total(1)));
    }
    return out;
}

void testDeterminism() {
    CHECK(transcript(1234, Mode::Bireysel) == transcript(1234, Mode::Bireysel));
    CHECK(transcript(77, Mode::Esli) == transcript(77, Mode::Esli));
    CHECK(transcript(5, Mode::Ikili) == transcript(5, Mode::Ikili));
    CHECK(transcript(1234, Mode::Bireysel) != transcript(1235, Mode::Bireysel));
    // Same seed -> same deck and dealer.
    Game a, b;
    a.startMatch(9);
    b.startMatch(9);
    CHECK_EQ(a.dealer(), b.dealer());
    for (int s = 0; s < 4; ++s) CHECK(a.hand(s) == b.hand(s));
    CHECK(a.debugDeck() == b.debugDeck());
}

void testFuzz() {
    okey::Rng rng(2024);
    int hands = 0, pistis = 0, jackPistis = 0, buried = 0;
    for (int m = 0; m < 400; ++m) {
        Rules r;
        r.mode = (Mode)(m % 3);
        r.jackPistiOnAny = (m / 3) % 2;
        r.lastCardPisti = (m / 6) % 2;
        r.reTurnOpeningJack = (m / 12) % 4 != 0;
        r.targetScore = 51 + 50 * (m % 3);
        Game g(r);
        g.startMatch(1000 + m);
        while (true) {
            if (!g.buriedCards().empty()) ++buried;
            std::string why;
            CHECK(allCardsOnce(g, &why));
            while (g.stage() == Stage::Playing) {
                const int s = g.current();
                CHECK(g.isActive(s));
                const V h = g.hand(s);
                CHECK(!h.empty());
                // An illegal attempt first: a card from another hand / a wrong seat.
                const int other = g.nextActive(s);
                if (!g.hand(other).empty()) CHECK(!g.playCard(s, g.hand(other)[0]).ok);
                CHECK(!g.playCard(other, h[0]).ok);
                const int c = h[rng.range((int)h.size())];
                const int expectPisti = g.wouldPisti(c);
                const bool expectCap = g.wouldCapture(c);
                const int capBefore = g.capturedCount(s);
                const int tableBefore = g.tableCount();
                CHECK(g.playCard(s, c).ok);
                const auto ev = g.drainEvents();
                const auto caps = eventsOf(ev, EvType::Capture);
                CHECK_EQ(!caps.empty(), expectCap);
                if (expectCap) CHECK_EQ(g.capturedCount(s) - capBefore >= tableBefore + 1, true);
                const auto ps = eventsOf(ev, EvType::Pisti);
                CHECK_EQ(ps.empty() ? 0 : ps[0].points, expectPisti);
                for (const auto& p : ps) (p.jack ? jackPistis : pistis)++;
                if (!allCardsOnce(g, &why)) {
                    CHECK(false);
                    std::printf("   fuzz: %s\n", why.c_str());
                }
                for (const GameEvent& e : ev) CHECK(!e.text.empty());
                // seen() never contains a card still hidden in a hand or the deck
                for (int q = 0; q < 4; ++q)
                    for (int hc : g.hand(q)) CHECK(!g.isSeen(hc));
                for (int dc : g.debugDeck()) CHECK(!g.isSeen(dc));
            }
            ++hands;
            const HandResult& hr = g.lastHandResult();
            int cards = 0, base = 0, maj = 0;
            for (int i = 0; i < hr.numSides; ++i) {
                const ScoreLine& L = hr.side[i];
                cards += L.cards;
                base += L.aces + L.jacks + L.sinek2 + L.karo10;
                maj += L.majority;
                CHECK_EQ(L.total, L.aces + L.jacks + L.sinek2 + L.karo10 + L.majority + L.pistiPoints);
                CHECK_EQ(L.pistiPoints, L.pistis * r.pistiPoints + L.jackPistis * r.jackPistiPoints);
                int seatCards = 0;
                for (int s : g.seatsOfSide(i)) seatCards += hr.seat[s].cards;
                CHECK_EQ(seatCards, L.cards);
            }
            CHECK_EQ(cards, 52);
            CHECK_EQ(base, 13);
            CHECK(maj == 0 || maj == 3);
            CHECK_EQ(maj == 3, hr.majoritySide >= 0);
            CHECK(allCardsOnce(g));
            CHECK_EQ(g.tableCount(), 0);
            CHECK_EQ(g.deckCount(), 0);
            if (g.matchOver()) {
                const int w = g.winnerSide();
                CHECK(g.total(w) >= r.targetScore);
                for (int i = 0; i < g.numSides(); ++i)
                    if (i != w) CHECK(g.total(i) < g.total(w));
                break;
            }
            g.startNextHand();
        }
    }
    CHECK(hands > 1000);
    CHECK(pistis > 100);
    CHECK(jackPistis > 0);
    CHECK(buried > 0);
    std::printf("  fuzz: %d hands, %d piştiler, %d Vale piştileri\n", hands, pistis, jackPistis);
}

// ---------------------------------------------------------------------------------------------------------
// bots

void testBotsLegalAndFair() {
    // Every level always returns a card of its own hand, in every mode.
    for (int lv = 0; lv < 3; ++lv)
        for (int m = 0; m < 3; ++m) {
            Rules r;
            r.mode = (Mode)m;
            Game g(r);
            g.startMatch(300 + lv * 3 + m);
            std::array<Bot, 4> bots{Bot(BotLevel(lv), 1), Bot(BotLevel(lv), 2), Bot(BotLevel(lv), 3), Bot(BotLevel(lv), 4)};
            for (int hand = 0; hand < (lv == 2 ? 1 : 4) && !g.matchOver(); ++hand) {
                if (hand) g.startNextHand();
                while (g.stage() == Stage::Playing) {
                    for (const GameEvent& e : g.drainEvents())
                        for (int s : g.activeSeats()) bots[s].observe(e, g);
                    const int s = g.current();
                    const int c = bots[s].next(g, s);
                    CHECK(contains(g.hand(s), c));
                    CHECK(g.playCard(s, c).ok);
                }
            }
        }
    // Not its turn -> -1; fallbackCard is legal.
    {
        Game g;
        g.startMatch(4);
        Bot b(BotLevel::Usta, 1);
        const int notTurn = g.nextActive(g.current());
        CHECK_EQ(b.next(g, notTurn), -1);
        CHECK(contains(g.hand(g.current()), fallbackCard(g, g.current())));
        CHECK_EQ(fallbackCard(g, notTurn), -1);
    }
    // Fairness: the choice does not depend on hidden cards. Swap the other players' hands and the deck
    // contents (same counts, same public info): every level picks the same card.
    for (int lv = 0; lv < 3; ++lv) {
        for (int trial = 0; trial < 6; ++trial) {
            Game a;
            a.startMatch(800 + trial);
            okey::Rng rng(trial + 1);
            // play a few random cards to reach a mid-hand position
            for (int k = 0; k < 5 + trial; ++k) {
                const V& h = a.hand(a.current());
                a.playCard(a.current(), h[rng.range((int)h.size())]);
            }
            a.drainEvents();
            Game b = a;
            const int me = a.current();
            // Shuffle every hidden card (other hands, deck, closed) among their places.
            V hidden;
            for (int s = 0; s < 4; ++s)
                if (s != me) hidden.insert(hidden.end(), b.debugHand(s).begin(), b.debugHand(s).end());
            hidden.insert(hidden.end(), b.debugDeck().begin(), b.debugDeck().end());
            hidden.insert(hidden.end(), b.debugClosed().begin(), b.debugClosed().end());
            rng.shuffle(hidden);
            size_t i = 0;
            for (int s = 0; s < 4; ++s)
                if (s != me)
                    for (int& c : b.debugHand(s)) c = hidden[i++];
            for (int& c : b.debugDeck()) c = hidden[i++];
            for (int& c : b.debugClosed()) c = hidden[i++];
            CHECK(allCardsOnce(b));
            Bot ba(BotLevel(lv), 77), bb(BotLevel(lv), 77);
            CHECK_EQ(ba.next(a, me), bb.next(b, me));
        }
    }
}

void testBotSense() {
    // Usta and Kurt take a pişti when they can; Acemi takes it too (it always matches by rank).
    for (int lv = 0; lv < 3; ++lv) {
        Game g;
        blank(g, 1, 0);
        g.debugOpen() = {C(Kupa, 7)};
        g.debugHand(1) = {C(Sinek, 7), C(Karo, 5), C(Maca, KIZ), C(Kupa, 2)};
        g.debugHand(0) = {C(Maca, 3), C(Maca, 4), C(Maca, 6), C(Maca, 8)};
        g.debugHand(2) = {C(Kupa, 3), C(Kupa, 4), C(Kupa, 6), C(Kupa, 8)};
        g.debugHand(3) = {C(Karo, 3), C(Karo, 4), C(Karo, 6), C(Karo, 8)};
        for (int c = 0; c < 52; ++c) {
            bool placed = false;
            for (int s = 0; s < 4; ++s) placed = placed || contains(g.debugHand(s), c);
            if (!placed && c != C(Kupa, 7)) g.debugDeck().push_back(c);
        }
        while (g.debugDeck().size() > 16) {
            g.debugCaptured(0).push_back(g.debugDeck().back());
            g.debugDeck().pop_back();
        }
        for (int c : g.debugCaptured(0)) g.debugMarkSeen(c);
        g.debugMarkSeen(C(Kupa, 7));
        CHECK(allCardsOnce(g));
        Bot b(BotLevel(lv), 3);
        CHECK_EQ(b.next(g, 1), C(Sinek, 7));
    }
    // Usta keeps its Vale on a worthless 2-card pile when a safe card exists. (Kurt decides this by
    // simulation and may legitimately cash the Vale in.)
    for (int lv = 1; lv < 2; ++lv) {
        Game g;
        blank(g, 1, 0);
        g.debugOpen() = {C(Kupa, 3), C(Maca, 4)};
        // Two Kız already seen: Karo Kız is fairly safe to lay.
        g.debugHand(1) = {C(Sinek, VALE), C(Karo, KIZ), C(Karo, 8), C(Kupa, 9)};
        g.debugCaptured(2) = {C(Maca, KIZ), C(Kupa, KIZ)};
        for (int c : g.debugCaptured(2)) g.debugMarkSeen(c);
        g.debugMarkSeen(C(Kupa, 3));
        g.debugMarkSeen(C(Maca, 4));
        V rest;
        for (int c = 0; c < 52; ++c) {
            if (contains(g.debugOpen(), c) || contains(g.debugHand(1), c) || contains(g.debugCaptured(2), c)) continue;
            rest.push_back(c);
        }
        size_t i = 0;
        for (int s : {0, 2, 3})
            for (int k = 0; k < 4; ++k) g.debugHand(s).push_back(rest[i++]);
        while (i < rest.size()) g.debugDeck().push_back(rest[i++]);
        CHECK(allCardsOnce(g));
        Bot b(BotLevel(lv), 5);
        const int c = b.next(g, 1);
        CHECK(rankOf(c) != VALE);
    }
    // On an empty table Usta lays a "dead" rank (all other copies seen) rather than a live one.
    {
        Game g;
        blank(g, 1, 0);
        g.debugHand(1) = {C(Karo, 8), C(Kupa, 5), C(Karo, 9)};
        // all three other 5s seen
        g.debugCaptured(3) = {C(Maca, 5), C(Karo, 5), C(Sinek, 5)};
        for (int c : g.debugCaptured(3)) g.debugMarkSeen(c);
        V rest;
        for (int c = 0; c < 52; ++c)
            if (!contains(g.debugHand(1), c) && !contains(g.debugCaptured(3), c)) rest.push_back(c);
        size_t i = 0;
        for (int s : {0, 2, 3})
            for (int k = 0; k < 3; ++k) g.debugHand(s).push_back(rest[i++]);
        while (i < rest.size()) g.debugDeck().push_back(rest[i++]);
        CHECK(allCardsOnce(g));
        Bot b(BotLevel::Usta, 1);
        CHECK_EQ(b.next(g, 1), C(Kupa, 5));
    }
}

} // namespace

// Save / resume: the action log, written as text lines and replayed on a freshly started game with the same seed,
// reproduces the state (checked every few plays through whole matches in all three modes).
void samePisti(const Game& a, const Game& b) {
    CHECK_EQ((int)a.stage(), (int)b.stage());
    CHECK_EQ(a.handIndex(), b.handIndex());
    CHECK_EQ(a.dealer(), b.dealer());
    CHECK_EQ(a.current(), b.current());
    CHECK_EQ(a.dealRound(), b.dealRound());
    CHECK_EQ(a.tableCards(), b.tableCards());
    CHECK_EQ(a.closedCardsForDisplay(), b.closedCardsForDisplay());
    CHECK_EQ(a.deckCardsForDisplay(), b.deckCardsForDisplay());
    CHECK_EQ(a.lastCapturer(), b.lastCapturer());
    CHECK_EQ(a.actionLog().size(), b.actionLog().size());
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ(a.hand(s), b.hand(s));
        CHECK_EQ(a.capturedCards(s), b.capturedCards(s));
        CHECK_EQ(a.pistiCount(s), b.pistiCount(s));
        CHECK_EQ(a.total(s), b.total(s));
    }
}

void testReplay() {
    for (int mode = 0; mode < 3; ++mode) {
        Rules r;
        r.mode = (Mode)mode;
        r.targetScore = 51;
        const uint64_t seed = 110 + (uint64_t)mode;
        Game g(r);
        for (int s = 0; s < 4; ++s) g.setPlayer(s, s == 0 ? "Sen" : "Bot", s == 0);
        g.startMatch(seed);
        std::vector<Bot> bots;
        for (int s = 0; s < 4; ++s) bots.emplace_back(BotLevel::Usta, seed * 4 + (uint64_t)s);
        int steps = 0, replays = 0;
        while (g.stage() != Stage::MatchOver && steps < 5000) {
            if (g.stage() == Stage::HandOver) {
                g.startNextHand();
            } else {
                const int seat = g.current();
                const int c = bots[(size_t)seat].next(g, seat);
                if (!g.playCard(seat, c).ok) CHECK(g.playCard(seat, fallbackCard(g, seat)).ok);
            }
            for (const GameEvent& e : g.drainEvents())
                for (Bot& b : bots) b.observe(e, g);
            ++steps;
            if (steps % 23 == 0 || g.stage() == Stage::MatchOver || g.stage() == Stage::HandOver) {
                Game h(r);
                for (int s = 0; s < 4; ++s) h.setPlayer(s, s == 0 ? "Sen" : "Bot", s == 0);
                h.startMatch(g.matchSeed());
                bool ok = true;
                for (const LoggedAction& la : g.actionLog()) {
                    LoggedAction back;
                    ok = ok && LoggedAction::decode(la.encode(), back) && h.replay(back);
                }
                CHECK(ok);
                samePisti(g, h);
                ++replays;
            }
        }
        CHECK_EQ((int)g.stage(), (int)Stage::MatchOver);
        CHECK(replays > 10);
    }
    LoggedAction x;
    CHECK(!LoggedAction::decode("0 1", x));
    CHECK(!LoggedAction::decode("5 0 0", x));
    CHECK(LoggedAction::decode("0 2 17", x) && x.kind == LogKind::Play && x.seat == 2 && x.card == 17);
    Game h;
    h.startMatch(5);
    CHECK(!h.replay({LogKind::NextHand, -1, -1}));
    CHECK(!h.replay({LogKind::Play, (h.current() + 1) % 4, 0}));
    CHECK(h.actionLog().empty());
}

int main() {
    testDealing();
    testOpeningJack();
    testCaptures();
    testPisti();
    testClosedCards();
    testLastCapture();
    testScoring();
    testMajority();
    testModesAndErrors();
    testMatchEnd();
    testDeterminism();
    testFuzz();
    testBotsLegalAndFair();
    testBotSense();
    testReplay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
