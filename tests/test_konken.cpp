// Konken engine + bot tests: melds (sets, runs, the As low and high, jokers), adding and joker swaps, the partition
// search, the deal, the turn (stock / discard, the taken discard that must be used, giving it back), opening, discards,
// finishing and konken, the stock running out, scoring and the match limit, event texts, the action log and replay,
// bots (never rejected, fast), determinism and a random fuzzer.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Konken.cpp src/core/KonkenBot.cpp tests/test_konken.cpp
#include "core/Konken.h"
#include "core/KonkenBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace konken;

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
    o << v;
    return o.str();
}
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
        if (!(va_ == vb_)) reportFailure(__FILE__, __LINE__, std::string(#a " == " #b " (") + show(va_) + " vs " + show(vb_) + ")"); \
    } while (0)

namespace {

// A card: suit (kart::Maca..Sinek), rank 2..14, deck 0 / 1.
int C(int suit, int rank, int deck = 0) { return deck * NUM_FACES + kart::makeCard(suit, rank); }
int JK(int k) { return FIRST_JOKER + k; }
constexpr int Ma = kart::Maca, Ku = kart::Kupa, Ka = kart::Karo, Si = kart::Sinek;

bool meld(const std::vector<int>& cards, Meld* out = nullptr, std::string* why = nullptr) {
    Meld m;
    const bool ok = makeMeld(cards, m, why);
    if (out) *out = m;
    return ok;
}

// The 108 cards minus `used`, in a fixed order (a stock for debugSetup).
std::vector<int> restOf(const std::vector<int>& used) {
    std::vector<int> v;
    for (int c = 0; c < NUM_CARDS; ++c)
        if (std::find(used.begin(), used.end(), c) == used.end()) v.push_back(c);
    return v;
}

void testCards() {
    CHECK_EQ(NUM_CARDS, 108);
    CHECK(isJoker(104) && isJoker(107) && !isJoker(103) && !isJoker(0));
    CHECK_EQ(faceOf(C(Ku, 7, 1)), kart::makeCard(Ku, 7));
    CHECK_EQ(rankOf(C(Ku, 7, 1)), 7);
    CHECK_EQ(suitOf(C(Si, 14, 1)), (int)Si);
    CHECK_EQ(cardNameTR(C(Ku, 7, 1)), std::string("Kupa 7"));
    CHECK_EQ(cardNameTR(JK(2)), std::string("Joker"));
    CHECK_EQ(cardAccusativeTR(JK(0)), std::string("Jokeri"));
    CHECK_EQ(cardAccusativeTR(C(Ma, 14)), std::string("Maça Ası"));
    CHECK_EQ(handPoints(C(Ma, 14)), 11);
    CHECK_EQ(handPoints(C(Ma, 13)), 10);
    CHECK_EQ(handPoints(C(Ma, 11)), 10);
    CHECK_EQ(handPoints(C(Ma, 9)), 9);
    CHECK_EQ(handPoints(JK(0)), 25);
    CHECK_EQ(handPoints(JK(0), 30), 30);
    CHECK_EQ(rankValue(1), 1);
    CHECK_EQ(rankValue(14), 11);
    CHECK_EQ(rankValue(12), 10);
    CHECK_EQ(rankValue(5), 5);
}

void testMelds() {
    Meld m;
    std::string why;
    // sets
    CHECK(meld({C(Ma, 7), C(Ku, 7), C(Ka, 7)}, &m));
    CHECK(m.kind == MeldKind::Set);
    CHECK_EQ(m.rank, 7);
    CHECK_EQ(m.value(), 21);
    CHECK(meld({C(Ma, 7), C(Ku, 7), C(Ka, 7), C(Si, 7, 1)}, &m));
    CHECK_EQ(m.value(), 28);
    CHECK(!meld({C(Ma, 7), C(Ku, 7), C(Ka, 7), C(Si, 7), C(Ma, 7, 1)}));       // five
    CHECK(!meld({C(Ma, 7), C(Ma, 7, 1), C(Ka, 7)}, nullptr, &why));            // two spades
    CHECK(why.find("aynı renk") != std::string::npos);
    CHECK(meld({C(Ma, 14), C(Ku, 14), JK(0)}, &m));
    CHECK_EQ(m.value(), 33);                                                    // aces 11 each, the joker too
    CHECK_EQ((int)m.missingSuits().size(), 2);
    CHECK(!meld({C(Ma, 14), JK(0), JK(1)}, nullptr, &why));                    // one real card
    CHECK(why.find("iki gerçek") != std::string::npos);
    CHECK(!meld({C(Ma, 7), C(Ku, 7)}));                                        // two cards
    // runs
    CHECK(meld({C(Ku, 5), C(Ku, 6), C(Ku, 7)}, &m));
    CHECK(m.kind == MeldKind::Run);
    CHECK_EQ(m.low(), 5);
    CHECK_EQ(m.high(), 7);
    CHECK_EQ(m.value(), 18);
    CHECK(meld({C(Ku, 7), C(Ku, 5), C(Ku, 6)}, &m));                           // any order
    CHECK_EQ(m.cards[0], C(Ku, 5));
    CHECK(meld({C(Ku, 14), C(Ku, 2), C(Ku, 3)}, &m));                          // A-2-3: the As low
    CHECK_EQ(m.low(), 1);
    CHECK_EQ(m.value(), 6);
    CHECK(meld({C(Ku, 12), C(Ku, 13), C(Ku, 14)}, &m));                        // Q-K-A: the As high
    CHECK_EQ(m.high(), 14);
    CHECK_EQ(m.value(), 31);
    CHECK(!meld({C(Ku, 13), C(Ku, 14), C(Ku, 2)}));                            // no K-A-2
    CHECK(!meld({C(Ku, 5), C(Ku, 6), C(Ka, 7)}));                              // mixed suits
    CHECK(!meld({C(Ku, 5), C(Ku, 5, 1), C(Ku, 6)}));                           // a rank twice
    CHECK(!meld({C(Ku, 5), C(Ku, 6), C(Ku, 8)}));                              // a gap
    CHECK(meld({C(Ku, 5), JK(0), C(Ku, 7)}, &m));                              // the joker fills the gap
    CHECK_EQ(m.cards[1], JK(0));
    CHECK_EQ(m.value(), 18);
    CHECK(meld({C(Ku, 5), C(Ku, 6), JK(0)}, &m));                              // as ordered: the joker is the 7
    CHECK_EQ(m.high(), 7);
    CHECK(meld({JK(0), C(Ku, 5), C(Ku, 6)}, &m));                              // ... or the 4 when put first
    CHECK_EQ(m.low(), 4);
    CHECK(meld({C(Ku, 5), JK(0), C(Ku, 6)}, &m));                              // not ordered: the engine's choice
    CHECK_EQ(m.low(), 5);
    CHECK_EQ(m.high(), 7);
    CHECK(meld({C(Ku, 13), C(Ku, 12), JK(1)}, &m));                            // reversed order K Q J: joker = J? no:
    CHECK_EQ(m.size(), 3);
    CHECK(meld({C(Ku, 13), C(Ku, 14), JK(1)}, &m));                            // K A and a joker: only Q-K-A
    CHECK_EQ(m.low(), 12);
    CHECK(meld({C(Ku, 2), JK(0), JK(1), C(Ku, 5)}, &m));                       // two jokers inside
    CHECK_EQ(m.value(), 14);
    // the full A..A run (two Aces)
    std::vector<int> full;
    for (int r = 2; r <= 13; ++r) full.push_back(C(Si, r));
    full.push_back(C(Si, 14));
    full.push_back(C(Si, 14, 1));
    CHECK(meld(full, &m));
    CHECK_EQ(m.size(), 14);
    CHECK_EQ(m.low(), 1);
    CHECK_EQ(m.high(), 14);
}

void testAddSwap() {
    Meld run, set;
    CHECK(makeMeld({C(Ka, 5), C(Ka, 6), C(Ka, 7)}, run));
    CHECK(canAdd(run, C(Ka, 8)));
    CHECK(canAdd(run, C(Ka, 4, 1)));
    CHECK(!canAdd(run, C(Ka, 9)));
    CHECK(!canAdd(run, C(Ku, 8)));
    CHECK(canAdd(run, JK(0)));
    Side used = Side::Auto;
    CHECK(canAdd(run, JK(0), Side::Front, &used));
    CHECK(used == Side::Front);
    CHECK(!canAdd(run, C(Ka, 8), Side::Front));
    addCard(run, C(Ka, 4), Side::Auto);
    CHECK_EQ(run.low(), 4);
    CHECK_EQ(run.cards.front(), C(Ka, 4));
    // the As at the ends
    Meld low;
    CHECK(makeMeld({C(Ka, 2), C(Ka, 3), C(Ka, 4)}, low));
    CHECK(canAdd(low, C(Ka, 14)));
    addCard(low, C(Ka, 14), Side::Auto);
    CHECK_EQ(low.low(), 1);
    CHECK(!canAdd(low, JK(0), Side::Front));
    Meld high;
    CHECK(makeMeld({C(Ka, 11), C(Ka, 12), C(Ka, 13)}, high));
    CHECK(canAdd(high, C(Ka, 14, 1)));
    addCard(high, C(Ka, 14, 1), Side::Auto);
    CHECK_EQ(high.high(), 14);
    CHECK(!canAdd(high, JK(1), Side::Back));
    CHECK(canAdd(high, JK(1)));                     // (on the low end)
    // sets
    CHECK(makeMeld({C(Ma, 9), C(Ku, 9), C(Ka, 9)}, set));
    CHECK(canAdd(set, C(Si, 9)));
    CHECK(!canAdd(set, C(Ma, 9, 1)));
    CHECK(canAdd(set, JK(2)));
    addCard(set, C(Si, 9), Side::Auto);
    CHECK(!canAdd(set, JK(2)));                     // four is the most
    // joker swaps
    Meld rj;
    CHECK(makeMeld({C(Ma, 5), JK(0), C(Ma, 7)}, rj));
    CHECK_EQ(swapIndex(rj, C(Ma, 6, 1)), 1);
    CHECK_EQ(swapIndex(rj, C(Ma, 8)), -1);
    CHECK_EQ(swapIndex(rj, C(Ku, 6)), -1);
    Meld sj;
    CHECK(makeMeld({C(Ma, 12), C(Ku, 12), JK(3)}, sj));
    CHECK_EQ(swapIndex(sj, C(Ka, 12)), 2);
    CHECK_EQ(swapIndex(sj, C(Si, 12)), 2);
    CHECK_EQ(swapIndex(sj, C(Ma, 12, 1)), -1);
    Meld sj4;
    CHECK(makeMeld({C(Ma, 12), C(Ku, 12), C(Ka, 12), JK(3)}, sj4));
    CHECK_EQ(swapIndex(sj4, C(Si, 12)), 3);
    CHECK_EQ(swapIndex(sj4, C(Ka, 12, 1)), -1);
    Meld aj; // the joker as the low As
    CHECK(makeMeld({JK(0), C(Ku, 2), C(Ku, 3)}, aj));
    CHECK_EQ(aj.low(), 1);
    CHECK_EQ(swapIndex(aj, C(Ku, 14)), 0);
}

void testPartition() {
    // a clean hand: two runs, a set, a loose card
    const std::vector<int> h = {C(Ma, 2), C(Ma, 3), C(Ma, 4), C(Ku, 10), C(Ku, 11), C(Ku, 12), C(Ka, 8), C(Si, 8), C(Ma, 8), C(Si, 13)};
    Partition P = bestPartition(h, Goal::Value);
    CHECK_EQ((int)P.melds.size(), 3);
    CHECK_EQ(P.value, 9 + 30 + 24);
    CHECK_EQ(P.rest, std::vector<int>{C(Si, 13)});
    CHECK_EQ(P.restPoints, 10);
    // a card wanted by both a run and a set: 5-6-7 of spades and three 7s; best value keeps the 7 in one of them
    const std::vector<int> h2 = {C(Ma, 5), C(Ma, 6), C(Ma, 7), C(Ku, 7), C(Ka, 7)};
    P = bestPartition(h2, Goal::Value);
    CHECK_EQ(P.laid, 3);
    CHECK_EQ(P.value, 21); // 7-7-7 (21) beats 5-6-7 (18)
    // with a joker both melds work
    std::vector<int> h3 = h2;
    h3.push_back(JK(0));
    P = bestPartition(h3, Goal::Value);
    CHECK_EQ(P.laid, 6);
    CHECK(P.rest.empty());
    // spare jokers are attached (or kept when asked)
    const std::vector<int> h4 = {C(Ku, 5), C(Ku, 6), C(Ku, 7), JK(0), C(Si, 2)};
    P = bestPartition(h4, Goal::Value, -1, true);
    CHECK_EQ(P.laid, 4);
    P = bestPartition(h4, Goal::Value, -1, false);
    CHECK_EQ(P.laid, 3);
    CHECK_EQ((int)P.rest.size(), 2);
    // mustUse: a card that fits nowhere -> no melds
    P = bestPartition(h4, Goal::Value, C(Si, 2));
    CHECK(P.melds.empty());
    P = bestPartition(h4, Goal::Value, C(Ku, 6));
    CHECK(!P.melds.empty());
    // two real cards and a joker on an end
    P = bestPartition({C(Ka, 9), C(Ka, 10), JK(1)}, Goal::Value);
    CHECK_EQ(P.laid, 3);
    CHECK_EQ(P.value, 9 + 10 + 10);
    // Shed: a cheap run vs dear loose cards: the fewest points left
    P = bestPartition({C(Ma, 13), C(Ku, 13), C(Ka, 13), C(Ka, 11), C(Ka, 12)}, Goal::Shed);
    CHECK_EQ(P.laid, 3);
    // every partition is made of valid melds
    okey::Rng r(5);
    for (int t = 0; t < 300; ++t) {
        std::vector<int> deck((size_t)NUM_CARDS);
        for (int i = 0; i < NUM_CARDS; ++i) deck[(size_t)i] = i;
        r.shuffle(deck);
        deck.resize(15);
        for (int goal = 0; goal < 2; ++goal) {
            const Partition Q = bestPartition(deck, (Goal)goal);
            int n = (int)Q.rest.size();
            std::set<int> seen(Q.rest.begin(), Q.rest.end());
            for (const auto& mv : Q.melds) {
                Meld m;
                CHECK(makeMeld(mv, m));
                n += (int)mv.size();
                for (int c : mv) seen.insert(c);
            }
            CHECK_EQ(n, 15);
            CHECK_EQ((int)seen.size(), 15);
        }
    }
}

// A game with set-up hands for seat 0's turn.
Game setupGame(const std::array<std::vector<int>, 4>& hands, const std::vector<int>& discard, Rules R = Rules()) {
    if (R.limit == Rules().limit) R.limit = 100000; // (a test hand never ends the match unless it asks to)
    Game g(R);
    g.startMatch(77);
    g.drainEvents();
    g.debugSetCurrent(0);
    std::vector<int> used;
    for (const auto& h : hands) used.insert(used.end(), h.begin(), h.end());
    used.insert(used.end(), discard.begin(), discard.end());
    g.debugSetup(hands, restOf(used), discard);
    return g;
}

void testDeal() {
    Game g;
    g.startMatch(1234);
    CHECK(g.stage() == Stage::Draw);
    std::set<int> all;
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ(g.handSize(s), 14);
        for (int c : g.hand(s)) all.insert(c);
    }
    CHECK_EQ(g.stockSize(), 108 - 56 - 1);
    CHECK_EQ((int)g.discardPile().size(), 1);
    CHECK(!isJoker(g.discardTop()));
    all.insert(g.discardTop());
    CHECK_EQ((int)all.size(), 57);
    CHECK_EQ(g.current(), g.starter());
    CHECK_EQ(g.dealer(), (g.starter() + 3) % 4);
    bool deal = false, start = false;
    for (const GameEvent& e : g.drainEvents()) {
        deal = deal || e.type == EvType::Deal;
        start = start || e.type == EvType::HandStart;
    }
    CHECK(deal && start);
    // the same seed deals the same cards
    Game h;
    h.startMatch(1234);
    for (int s = 0; s < 4; ++s) CHECK_EQ(h.hand(s), g.hand(s));
}

void testTurn() {
    // seat 0 holds 5-6 of hearts; the 7 of hearts lies on the discard pile
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ku, 5), C(Ku, 6), C(Ma, 2), C(Ka, 9)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ku, 7)});
    std::string why;
    CHECK(!g.discard(0, C(Ma, 2)).ok);                 // draw first
    CHECK(!g.drawStock(1).ok);                         // not their turn
    CHECK(g.takeDiscard(0).ok);
    CHECK_EQ(g.takenCard(), C(Ku, 7));
    CHECK(g.stage() == Stage::Play);
    // not opened: 5-6-7 (18) is far from 51
    CHECK(!g.layMelds(0, {{C(Ku, 5), C(Ku, 6), C(Ku, 7)}}).ok);
    // the taken card can't be thrown, nor anything else while it is pending
    const ActionResult r = g.discard(0, C(Ma, 2));
    CHECK(!r.ok);
    CHECK(r.error.find("kullanmalısın") != std::string::npos);
    CHECK(g.returnDiscard(0).ok);
    CHECK_EQ(g.discardTop(), C(Ku, 7));
    CHECK(g.stage() == Stage::Draw);
    CHECK(g.mustDrawStock());
    CHECK(!g.takeDiscard(0).ok);                       // not again
    const int stock = g.stockSize();
    CHECK(g.drawStock(0).ok);
    CHECK_EQ(g.stockSize(), stock - 1);
    CHECK_EQ(g.handSize(0), 5);
    CHECK(g.discard(0, C(Ma, 2)).ok);
    CHECK_EQ(g.current(), 1);
    CHECK(g.stage() == Stage::Draw);
    CHECK_EQ(g.discardTop(), C(Ma, 2));
    CHECK_EQ(g.discards().back().seat, 0);
}

void testOpening() {
    // 51+: K-K-K (30) and 9-10-J of diamonds (29) = 59; the taken Karo Vale completes the run
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ma, 13), C(Ku, 13), C(Si, 13), C(Ka, 9), C(Ka, 10), C(Ma, 3), C(Ku, 8)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ka, 11)});
    CHECK(g.takeDiscard(0).ok);
    int v = 0;
    CHECK(!g.checkLay(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}}, &v)); // 30 < 51
    CHECK_EQ(v, 30);
    CHECK(g.checkLay(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}, &v));
    CHECK_EQ(v, 59);
    CHECK(!g.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ma, 13), C(Ka, 10), C(Ka, 11)}}).ok); // a card twice
    CHECK(g.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}).ok);
    CHECK(g.opened(0));
    CHECK_EQ(g.takenCard(), -1);
    CHECK_EQ((int)g.table().size(), 2);
    CHECK_EQ(g.table()[0].owner, 0);
    // once opened: more melds of any value, adding (also to others' melds)
    CHECK(!g.addToMeld(0, C(Ma, 3), 1).ok);
    CHECK(g.discard(0, C(Ma, 3)).ok);
    // seat 1 has not opened: cannot add
    CHECK(g.drawStock(1).ok);
    std::string why;
    CHECK(!g.checkAdd(1, C(Si, 3), 0, Side::Auto, &why));
    CHECK(why.find("açmalısın") != std::string::npos);
}

void testFinishAndScore() {
    // seat 0 has opened; one card left that it adds -> finish without a discard
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ka, 12)};
    hands[1] = {C(Si, 3), C(Si, 4), JK(0)};                 // opened: 3 + 4 + 25
    hands[2] = {C(Si, 8), C(Si, 9)};                        // never opened: 100
    hands[3] = {C(Ma, 14), C(Ma, 12)};                      // opened: 21
    Game g = setupGame(hands, {C(Ku, 2)});
    Meld run;
    makeMeld({C(Ka, 9), C(Ka, 10), C(Ka, 11)}, run);
    run.owner = 0;
    g.debugSetOpened(0, true);
    g.debugSetOpened(1, true);
    g.debugSetOpened(3, true);
    g.debugSetup(hands, {C(Ku, 2, 1)}, {C(Ku, 2)}, {run});
    CHECK(g.drawStock(0).ok);
    CHECK(g.addToMeld(0, C(Ka, 12), 0).ok);
    CHECK_EQ(g.handSize(0), 1);
    CHECK(g.stage() == Stage::Play);
    CHECK(g.discard(0, C(Ku, 2, 1)).ok);
    CHECK(g.stage() == Stage::HandOver);
    const HandRecord& r = g.sheet().back();
    CHECK_EQ(r.finisher, 0);
    CHECK(!r.konken);
    CHECK_EQ(r.points[0], 0);
    CHECK_EQ(r.points[1], 32);
    CHECK_EQ(r.points[2], 100);
    CHECK_EQ(r.points[3], 21);
    CHECK_EQ(g.total(2), 100);
    bool end = false;
    for (const GameEvent& e : g.drainEvents())
        if (e.type == EvType::HandEnd) {
            end = true;
            CHECK_EQ(e.text, std::string("Eli bitirdin"));
            CHECK_EQ((int)e.lines.size(), 4);
        }
    CHECK(end);
    // the next hand: the starter moves on
    const int st = g.starter();
    g.startNextHand();
    CHECK_EQ(g.starter(), (st + 1) % 4);
    CHECK(g.stage() == Stage::Draw);
    CHECK_EQ(g.handIndex(), 1);
}

void testKonken() {
    // not opened, the whole hand goes down in one turn: konken, everybody else doubled
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ma, 13), C(Ku, 13), C(Si, 13), C(Ka, 9), C(Ka, 10), C(Ka, 11), C(Ku, 4)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ma, 2)});
    g.debugSetOpened(1, true);
    CHECK(g.drawStock(0).ok);
    const int drawn = g.hand(0).back();
    CHECK(g.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}).ok);
    CHECK(g.openedThisTurn());
    CHECK(g.discard(0, C(Ku, 4)).ok);
    CHECK_EQ(g.handSize(0), 1);
    (void)drawn;
    // (still one card: not finished; the next test lays everything)
    Game k = setupGame(hands, {C(Ku, 5)});
    CHECK(k.takeDiscard(0).ok);
    // Kupa 4-5 and ... not a meld: give it back, draw, lay the two melds and throw the last card
    CHECK(k.returnDiscard(0).ok);
    CHECK(k.drawStock(0).ok);
    const int extra = k.hand(0).back();
    CHECK(k.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}).ok);
    // two loose cards left (Kupa 4 and the drawn one): add the drawn one if it fits, else a konken is not possible
    bool added = false;
    for (int m = 0; m < (int)k.table().size(); ++m)
        if (k.checkAdd(0, extra, m)) {
            CHECK(k.addToMeld(0, extra, m).ok);
            added = true;
            break;
        }
    if (added) {
        CHECK(k.discard(0, C(Ku, 4)).ok);
        CHECK(k.stage() == Stage::HandOver);
        CHECK(k.sheet().back().konken);
    }
    // a clean konken: seven cards, two melds and a last discard
    std::array<std::vector<int>, 4> h2 = hands;
    h2[0] = {C(Ma, 13), C(Ku, 13), C(Si, 13), C(Ka, 9), C(Ka, 10), C(Ka, 11)};
    Game q = setupGame(h2, {C(Ku, 4)});
    q.debugSetOpened(1, true);
    q.debugSetOpened(3, true);
    CHECK(q.takeDiscard(0).ok);
    CHECK(q.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}).ok);
    // only the taken Kupa 4 is left and it fits nowhere: it can't be thrown, so it goes back and a card is drawn
    CHECK(!q.discard(0, C(Ku, 4)).ok);
    CHECK(q.returnDiscard(0).ok);
    CHECK_EQ(q.handSize(0), 0);
    CHECK(q.stage() == Stage::Draw);
    CHECK(q.drawStock(0).ok);
    CHECK(q.discard(0, q.hand(0)[0]).ok);
    CHECK(q.stage() == Stage::HandOver);
    CHECK(q.sheet().back().konken);
    // the simplest konken: lay everything at once
    std::array<std::vector<int>, 4> h3 = hands;
    h3[0] = {C(Ma, 13), C(Ku, 13), C(Si, 13), C(Ka, 9), C(Ka, 10)};
    Game z = setupGame(h3, {C(Ka, 11)});
    z.debugSetOpened(1, true);
    z.debugSetOpened(3, true);
    CHECK(z.takeDiscard(0).ok);
    CHECK(z.layMelds(0, {{C(Ma, 13), C(Ku, 13), C(Si, 13)}, {C(Ka, 9), C(Ka, 10), C(Ka, 11)}}).ok);
    CHECK(z.stage() == Stage::HandOver);
    const HandRecord& r = z.sheet().back();
    CHECK(r.konken);
    CHECK_EQ(r.finisher, 0);
    CHECK_EQ(r.points[1], 2 * (3 + 4));
    CHECK_EQ(r.points[2], 2 * 100);
    CHECK_EQ(r.points[3], 2 * 20);
    bool txt = false;
    for (const GameEvent& e : z.drainEvents())
        if (e.type == EvType::HandEnd) txt = e.text.find("Konken!") == 0 && e.amount == 1;
    CHECK(txt);
}

void testJokerRules() {
    std::array<std::vector<int>, 4> hands;
    hands[0] = {JK(0), C(Ma, 2), C(Ka, 6)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ku, 9)});
    Meld run;
    makeMeld({C(Ka, 5), JK(1), C(Ka, 7)}, run);
    run.owner = 1;
    std::vector<int> used = {JK(0), C(Ma, 2), C(Ka, 6), C(Si, 3), C(Si, 4), C(Si, 8), C(Si, 9), C(Ma, 11), C(Ma, 12), C(Ku, 9), C(Ka, 5), JK(1), C(Ka, 7)};
    g.debugSetup(hands, restOf(used), {C(Ku, 9)}, {run});
    CHECK(g.drawStock(0).ok);
    std::string why;
    CHECK(!g.canDiscard(0, JK(0), &why));
    CHECK(why.find("Joker atılmaz") != std::string::npos);
    CHECK(!g.swapJoker(0, C(Ka, 6), 0).ok);           // not opened
    g.debugSetOpened(0, true);
    CHECK(g.swapJoker(0, C(Ka, 6), 0).ok);
    CHECK_EQ(g.table()[0].cards[1], C(Ka, 6));
    CHECK(std::find(g.hand(0).begin(), g.hand(0).end(), JK(1)) != g.hand(0).end());
    // the jokers go on the run's ends; the last card may be a joker discard
    CHECK(g.addToMeld(0, JK(1), 0, Side::Back).ok);
    CHECK_EQ(g.table()[0].high(), 8);
    CHECK(g.addToMeld(0, JK(0), 0, Side::Front).ok);
    CHECK_EQ(g.table()[0].low(), 4);
    CHECK(g.fitsTable(C(Ka, 9)));
    CHECK(!g.fitsTable(C(Ku, 9)));
}

void testStockOut() {
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ma, 2), C(Ka, 6)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ku, 9)});
    g.debugSetup(hands, {C(Ku, 3)}, {C(Ku, 9)});
    g.debugSetOpened(3, true);
    CHECK(g.drawStock(0).ok);
    CHECK_EQ(g.stockSize(), 0);
    CHECK(g.discard(0, C(Ku, 3)).ok);
    CHECK(g.stage() == Stage::HandOver);
    const HandRecord& r = g.sheet().back();
    CHECK_EQ(r.finisher, -1);
    CHECK_EQ(r.points[0], 100);
    CHECK_EQ(r.points[3], 20);
}

void testMatchEnd() {
    Rules R;
    R.limit = 101;
    R.lastStanding = false; // "ilk yanan": the first burn ends the match
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ma, 2), C(Ka, 6)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    Game g = setupGame(hands, {C(Ku, 9)}, R);
    g.debugSetup(hands, {C(Ku, 3)}, {C(Ku, 9)});
    g.debugSetTotal(1, 50);
    g.debugSetOpened(0, true);
    g.debugSetOpened(2, true);
    g.debugSetOpened(3, true);
    CHECK(g.drawStock(0).ok);
    CHECK(g.discard(0, C(Ku, 3)).ok);
    // seat 1 never opened: 50 + 100 = 150 >= 101
    CHECK(g.stage() == Stage::MatchOver);
    CHECK_EQ(g.total(1), 150);
    const std::array<int, 4> rk = g.ranking();
    CHECK_EQ(rk[3], 1);
    bool me = false;
    for (const GameEvent& e : g.drainEvents())
        if (e.type == EvType::MatchEnd) me = e.seat == rk[0] && (int)e.lines.size() == 4;
    CHECK(me);
}

// ---- Konken bitiş "son kalan" (Rules::lastStanding, the default): the burned leave, the table shrinks 4 -> 3 -> 2
// Plays on with bots from where `g` is until the match is over; checks the turn never reaches a burned seat, the
// burned hold no cards, every discard goes to the next active seat.
void playOutEliminating(Game& g, uint64_t seed, int& bad) {
    std::array<std::unique_ptr<Bot>, 4> bots;
    for (int s = 0; s < 4; ++s) bots[(size_t)s] = std::make_unique<Bot>(BotLevel::Usta, seed * 5 + (uint64_t)s);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 200000) {
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            for (int s = 0; s < 4; ++s)
                if (g.isOut(s) ? g.handSize(s) != 0 : g.handSize(s) != g.rules().handSize) ++bad;
            if (g.isOut(g.current()) || g.isOut(g.dealer()) || g.isOut(g.starter())) ++bad;
            continue;
        }
        const int s = g.current();
        if (g.isOut(s)) {
            ++bad;
            break;
        }
        const BotAction a = bots[(size_t)s]->next(g, s);
        if (!applyBotAction(g, s, a).ok) applyBotAction(g, s, fallbackAction(g, s));
        if ((g.stage() == Stage::Draw || g.stage() == Stage::Play) && g.current() != s && g.current() != g.nextActive(s)) ++bad;
        g.drainEvents();
    }
    if (g.stage() != Stage::MatchOver) ++bad;
}

void testElimination() {
    // two burn in the same hand: they leave, the worse total takes the lower place; the other two play on
    {
        Rules R;
        R.limit = 101;
        std::array<std::vector<int>, 4> hands;
        hands[0] = {C(Ma, 2), C(Ka, 6)};
        hands[1] = {C(Si, 3), C(Si, 4)};
        hands[2] = {C(Si, 8), C(Si, 9)};
        hands[3] = {C(Ma, 11), C(Ma, 12)};
        Game g = setupGame(hands, {C(Ku, 9)}, R);
        CHECK(g.rules().lastStanding);
        g.debugSetup(hands, {C(Ku, 3)}, {C(Ku, 9)});
        g.debugSetTotal(1, 50);
        g.debugSetTotal(3, 95);
        g.debugSetOpened(0, true);
        g.debugSetOpened(2, true);
        g.debugSetOpened(3, true);
        CHECK(g.drawStock(0).ok);
        CHECK(g.discard(0, C(Ku, 3)).ok);
        // seat 1: 50 + 100 = 150, seat 3: 95 + 20 = 115 -> both burn; 0 and 2 go on
        CHECK(g.stage() == Stage::HandOver);
        CHECK(g.isOut(1) && g.isOut(3) && g.active(0) && g.active(2));
        CHECK_EQ(g.activeCount(), 2);
        CHECK_EQ(g.place(1), 4);
        CHECK_EQ(g.place(3), 3);
        CHECK_EQ(g.place(0), 0);
        const HandRecord& r = g.sheet().back();
        CHECK(r.burned[1] && r.burned[3] && !r.burned[0] && !r.burned[2]);
        CHECK(r.playing[0] && r.playing[1] && r.playing[2] && r.playing[3]);
        int burns = 0;
        bool order = true;
        for (const GameEvent& e : g.drainEvents()) {
            if (e.type == EvType::Burn) {
                order = order && (burns == 0 ? e.seat == 1 && e.amount == 4 : e.seat == 3 && e.amount == 3);
                ++burns;
            }
            if (e.type == EvType::MatchEnd) order = false;
        }
        CHECK_EQ(burns, 2);
        CHECK(order);
        const std::array<int, 4> rk = g.ranking();
        CHECK_EQ(rk[2], 3);
        CHECK_EQ(rk[3], 1);
        // the next hand is dealt to the two left; 28 cards in hands, one up, the rest in the stock
        g.startNextHand();
        CHECK_EQ(g.handSize(1), 0);
        CHECK_EQ(g.handSize(3), 0);
        CHECK_EQ(g.handSize(0), 14);
        CHECK_EQ(g.handSize(2), 14);
        CHECK_EQ(g.stockSize(), NUM_CARDS - 28 - 1);
        CHECK(g.active(g.current()));
        CHECK_EQ(g.nextActive(0), 2);
        CHECK_EQ(g.nextActive(2), 0);
        CHECK_EQ(g.prevActive(0), 2);
        CHECK(g.pointsInHand(1) == 0);
        int bad = 0;
        playOutEliminating(g, 5, bad);
        CHECK_EQ(bad, 0);
        // one of 0 / 2 burned at last: places 1..4 all different
        std::set<int> places;
        for (int s = 0; s < 4; ++s) places.insert(g.place(s));
        CHECK_EQ((int)places.size(), 4);
        CHECK(g.place(1) == 4 && g.place(3) == 3);
        const std::array<int, 4> fin = g.ranking();
        for (int i = 0; i < 4; ++i) CHECK_EQ(g.place(fin[(size_t)i]), i + 1);
        // the burned write nothing after they left
        for (size_t h = 1; h < g.sheet().size(); ++h) {
            CHECK(!g.sheet()[h].playing[1] && !g.sheet()[h].playing[3]);
            CHECK_EQ(g.sheet()[h].points[1], 0);
            CHECK_EQ(g.sheet()[h].totals[1], 150);
        }
    }
    // everyone still playing burns at once: the lowest of them stays and wins
    {
        Rules R;
        R.limit = 101;
        std::array<std::vector<int>, 4> hands;
        hands[0] = {C(Ma, 2), C(Ka, 6)};
        hands[1] = {C(Si, 3), C(Si, 4)};
        hands[2] = {C(Si, 8), C(Si, 9)};
        hands[3] = {C(Ma, 11), C(Ma, 12)};
        Game g = setupGame(hands, {C(Ku, 9)}, R);
        g.debugSetup(hands, {C(Ku, 3)}, {C(Ku, 9)});
        for (int s = 0; s < 4; ++s) g.debugSetTotal(s, 99);
        CHECK(g.drawStock(0).ok);
        CHECK(g.discard(0, C(Ku, 3)).ok);
        // nobody opened: all write 100 -> 199 each; seat 0 (lowest seat of the tie) stays
        CHECK(g.stage() == Stage::MatchOver);
        CHECK_EQ(g.activeCount(), 1);
        CHECK(g.active(0));
        CHECK_EQ(g.place(0), 1);
        CHECK_EQ(g.ranking()[0], 0);
    }
    // whole bot matches: the table shrinks to one, the log replays to the same places (save / resume, maç tekrarı)
    int bad = 0;
    for (uint64_t seed = 21; seed <= 26; ++seed) {
        Game g;
        g.startMatch(seed);
        playOutEliminating(g, seed, bad);
        CHECK(g.activeCount() == 1 || (int)g.sheet().size() >= g.rules().maxHands);
        int prevPlaying = 4;
        for (const HandRecord& r : g.sheet()) {
            int n = 0;
            for (int s = 0; s < 4; ++s) n += r.playing[(size_t)s] ? 1 : 0;
            CHECK(n <= prevPlaying && n >= 2);
            prevPlaying = n;
        }
        Game rp;
        rp.startMatch(g.matchSeed());
        bool ok = true;
        for (const LoggedAction& a : g.actionLog()) {
            LoggedAction b;
            ok = ok && LoggedAction::decode(a.encode(), b) && rp.replay(b);
        }
        CHECK(ok);
        CHECK(rp.stage() == Stage::MatchOver);
        for (int s = 0; s < 4; ++s) {
            CHECK_EQ(rp.place(s), g.place(s));
            CHECK_EQ(rp.total(s), g.total(s));
        }
        CHECK(rp.ranking() == g.ranking());
    }
    CHECK_EQ(bad, 0);
}

void testLogLines() {
    LoggedAction a;
    a.kind = LogKind::Lay;
    a.seat = 2;
    a.melds = {{1, 2, 3}, {104, 40, 41}};
    LoggedAction b;
    CHECK(LoggedAction::decode(a.encode(), b));
    CHECK(b.kind == LogKind::Lay);
    CHECK_EQ(b.seat, 2);
    CHECK_EQ((int)b.melds.size(), 2);
    CHECK_EQ(b.melds[1], (std::vector<int>{104, 40, 41}));
    a = LoggedAction();
    a.kind = LogKind::Add;
    a.seat = 1;
    a.card = 33;
    a.meld = 4;
    a.side = 1;
    CHECK(LoggedAction::decode(a.encode(), b));
    CHECK(b.kind == LogKind::Add && b.card == 33 && b.meld == 4 && b.side == 1);
    CHECK(!LoggedAction::decode("", b));
    CHECK(!LoggedAction::decode("9 0 0 0 0", b));
    CHECK(!LoggedAction::decode("3 0 -1 -1 0", b)); // a lay without melds
    CHECK(!LoggedAction::decode("3 0 -1 -1 0 | 1 x", b));
}

// Plays a match with bots; returns the final totals. Every action must be accepted.
std::array<int, 4> playMatch(uint64_t seed, const std::array<int, 4>& levels, int& rejected, Game* out = nullptr,
                             double* slowest = nullptr) {
    Rules R;
    R.limit = 151;
    Game g(R);
    std::array<std::unique_ptr<Bot>, 4> bots;
    for (int s = 0; s < 4; ++s) {
        bots[(size_t)s] = std::make_unique<Bot>((BotLevel)levels[(size_t)s], seed * 17 + (uint64_t)s);
        bots[(size_t)s]->setStyle(BotStyle::forSeat(s));
    }
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 200000) {
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int s = g.current();
        const auto t0 = std::chrono::steady_clock::now();
        const BotAction a = bots[(size_t)s]->next(g, s);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (slowest && levels[(size_t)s] == 2) *slowest = std::max(*slowest, ms);
        if (!applyBotAction(g, s, a).ok) {
            ++rejected;
            applyBotAction(g, s, fallbackAction(g, s));
        }
        g.drainEvents();
    }
    CHECK(g.stage() == Stage::MatchOver);
    std::array<int, 4> t{};
    for (int s = 0; s < 4; ++s) t[(size_t)s] = g.total(s);
    if (out) *out = g;
    return t;
}

void testBotsAndReplay() {
    int rejected = 0;
    double slowest = 0.0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Game g;
        const std::array<int, 4> t = playMatch(seed, {{2, 1, 0, 2}}, rejected, &g, &slowest);
        // at least one total reached the limit
        CHECK(*std::max_element(t.begin(), t.end()) >= 151 || (int)g.sheet().size() >= g.rules().maxHands);
        // the log replays to the same totals (save / resume), also through its text lines
        Game r;
        r.startMatch(g.matchSeed());
        bool ok = true;
        for (const LoggedAction& a : g.actionLog()) {
            LoggedAction b;
            ok = ok && LoggedAction::decode(a.encode(), b) && r.replay(b);
        }
        CHECK(ok);
        CHECK(r.stage() == Stage::MatchOver);
        for (int s = 0; s < 4; ++s) CHECK_EQ(r.total(s), t[(size_t)s]);
        CHECK_EQ(r.sheet().size(), g.sheet().size());
        // the same seeds give the same match
        Game d;
        const std::array<int, 4> t2 = playMatch(seed, {{2, 1, 0, 2}}, rejected, &d);
        CHECK(t2 == t);
        CHECK_EQ(d.actionLog().size(), g.actionLog().size());
    }
    CHECK_EQ(rejected, 0);
    std::printf("  Kurt's slowest decision: %.1f ms\n", slowest);
    CHECK(slowest < 300.0);
}

void testBotDecisions() {
    // Usta takes the discard that opens, and opens with it
    std::array<std::vector<int>, 4> hands;
    hands[0] = {C(Ma, 13), C(Ku, 13), C(Si, 13), C(Ka, 9), C(Ka, 10), C(Ma, 3), C(Ku, 8), C(Si, 5)};
    hands[1] = {C(Si, 3), C(Si, 4)};
    hands[2] = {C(Si, 8), C(Si, 9)};
    hands[3] = {C(Ma, 11), C(Ma, 12)};
    for (int lv = 1; lv <= 2; ++lv) {
        Game g = setupGame(hands, {C(Ka, 11)});
        Bot b((BotLevel)lv, 3);
        BotAction a = b.next(g, 0);
        CHECK(a.kind == BotAction::Kind::TakeDiscard);
        CHECK(applyBotAction(g, 0, a).ok);
        a = b.next(g, 0);
        CHECK(a.kind == BotAction::Kind::Lay);
        CHECK(applyBotAction(g, 0, a).ok);
        CHECK(g.opened(0));
        // then it throws something (never the joker, never a card it just laid)
        int guard = 0;
        while (g.current() == 0 && g.stage() == Stage::Play && ++guard < 20) {
            a = b.next(g, 0);
            CHECK(applyBotAction(g, 0, a).ok);
        }
        CHECK_EQ(g.current(), 1);
        std::vector<std::vector<int>> melds;
        CHECK(!b.openingNow(g, 0, melds)); // (not its turn any more)
    }
    // a useless discard is not taken
    {
        Game g = setupGame(hands, {C(Ku, 2)});
        Bot b(BotLevel::Kurt, 3);
        CHECK(b.next(g, 0).kind == BotAction::Kind::DrawStock);
    }
    // Usta / Kurt do not hand an opened neighbour a card that fits the table when a safe one exists
    {
        std::array<std::vector<int>, 4> h = hands;
        h[0] = {C(Ka, 8), C(Ma, 2), C(Ku, 3), C(Si, 6), C(Ma, 9)};
        Game g = setupGame(h, {C(Ku, 2)});
        Meld run;
        makeMeld({C(Ka, 9, 1), C(Ka, 10, 1), C(Ka, 11, 1)}, run);
        run.owner = 1;
        g.debugSetup(h, restOf({}), {C(Ku, 2)}, {run});
        g.debugSetOpened(1, true);
        CHECK(g.drawStock(0).ok);
        for (int lv = 1; lv <= 2; ++lv) {
            Bot b((BotLevel)lv, 9);
            const BotAction a = b.next(g, 0);
            CHECK(a.kind == BotAction::Kind::Discard);
            CHECK(a.card != C(Ka, 8));
        }
        // the analysis' values: one per card face, the best first is the bot's taste
        Bot k(BotLevel::Kurt, 9);
        const std::vector<DiscardValue> vals = k.evaluateDiscards(g, 0);
        CHECK(vals.size() >= 4);
    }
}

void testFuzz() {
    // random (but legal) human-like actions mixed with bots: the engine never breaks its invariants
    okey::Rng r(99);
    int bad = 0;
    for (int m = 0; m < 30; ++m) {
        Game g;
        g.startMatch(1000 + (uint64_t)m);
        Bot bot(BotLevel::Usta, 5);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && ++guard < 50000) {
            if (g.stage() == Stage::HandOver) {
                g.startNextHand();
                continue;
            }
            const int s = g.current();
            // now and then a random action (may be rejected: then nothing must change)
            if (r.chance(0.3f)) {
                const size_t logBefore = g.actionLog().size();
                const int hs = g.handSize(s), st = g.stockSize();
                const std::vector<int> h = g.hand(s);
                ActionResult res;
                const int pick = h.empty() ? -1 : h[(size_t)r.range((int)h.size())];
                switch (r.range(6)) {
                case 0: res = g.takeDiscard(s); break;
                case 1: res = g.discard(s, pick); break;
                case 2: res = g.addToMeld(s, pick, r.range(std::max(1, (int)g.table().size()))); break;
                case 3: res = g.swapJoker(s, pick, r.range(std::max(1, (int)g.table().size()))); break;
                case 4: res = g.returnDiscard(s); break;
                default: res = g.drawStock(s); break;
                }
                if (!res.ok && (g.actionLog().size() != logBefore || g.handSize(s) != hs || g.stockSize() != st)) ++bad;
                continue;
            }
            const BotAction a = bot.next(g, s);
            if (!applyBotAction(g, s, a).ok) applyBotAction(g, s, fallbackAction(g, s));
            // 108 cards always
            int n = g.stockSize() + (int)g.discardPile().size();
            for (int q = 0; q < 4; ++q) n += g.handSize(q);
            for (const Meld& mm : g.table()) n += mm.size();
            if (g.stage() == Stage::Draw || g.stage() == Stage::Play) {
                if (n != NUM_CARDS) ++bad;
                for (const Meld& mm : g.table()) {
                    Meld chk;
                    if (!makeMeld(mm.cards, chk)) ++bad;
                }
            }
            g.drainEvents();
        }
        if (g.stage() != Stage::MatchOver) ++bad;
        // and the log replays
        Game rp;
        rp.startMatch(g.matchSeed());
        for (const LoggedAction& a : g.actionLog())
            if (!rp.replay(a)) {
                ++bad;
                break;
            }
        for (int s = 0; s < 4; ++s)
            if (rp.total(s) != g.total(s)) ++bad;
    }
    CHECK_EQ(bad, 0);
}

} // namespace

int main() {
    testCards();
    testMelds();
    testAddSwap();
    testPartition();
    testDeal();
    testTurn();
    testOpening();
    testFinishAndScore();
    testKonken();
    testJokerRules();
    testStockOut();
    testMatchEnd();
    testElimination();
    testLogLines();
    testBotDecisions();
    testBotsAndReplay();
    testFuzz();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
