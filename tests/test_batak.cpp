// Batak engine + bot tests: deal, ihale, koz, every play rule, trick winner, scoring, match end, event texts,
// eşli (open dummy), determinism, bot legality / fairness / timing, and a random fuzzer.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Batak.cpp src/core/BatakBot.cpp tests/test_batak.cpp
//        -o build/batak/test_batak
// Run:   build/batak/test_batak [scale]   (scale multiplies the fuzzer size, default 1)
#include "core/Batak.h"
#include "core/BatakBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace batak;
using kart::makeCard;

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
std::string show(bool b) { return b ? "true" : "false"; }
std::string show(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}
std::string show(Stage s) { return std::to_string((int)s); }

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

// Card shorthands: M(14) = Maça As, K(2) = Kupa 2, ...
int M(int r) { return makeCard(kart::Maca, r); }
int H(int r) { return makeCard(kart::Kupa, r); }
int D(int r) { return makeCard(kart::Karo, r); }
int C(int r) { return makeCard(kart::Sinek, r); }

bool hasEventText(const std::vector<GameEvent>& ev, const std::string& text) {
    for (const auto& e : ev) {
        if (e.text == text) return true;
        for (const auto& l : e.lines)
            if (l == text) return true;
    }
    return false;
}

Game makeGame(const Rules& r = Rules(), bool human0 = false) {
    Game g(r);
    g.setPlayer(0, human0 ? "Batuhan" : "Hacı Rıza", human0);
    g.setPlayer(1, "Kel Mahmut", false);
    g.setPlayer(2, "Cemal", false);
    g.setPlayer(3, "Rıfat", false);
    return g;
}

// A full deal: each seat gets one whole suit (seat s: suit s).
std::array<std::vector<int>, 4> suitDeal() {
    std::array<std::vector<int>, 4> h;
    for (int s = 0; s < 4; ++s)
        for (int r = 2; r <= 14; ++r) h[s].push_back(makeCard(s, r));
    return h;
}

std::array<std::vector<int>, 4> randomDeal(uint64_t seed) {
    okey::Rng rng(seed);
    std::vector<int> d = kart::shuffledDeck(rng);
    std::array<std::vector<int>, 4> h;
    for (int i = 0; i < 52; ++i) h[i % 4].push_back(d[i]);
    return h;
}

// ---------------------------------------------------------------------------------------------------------

void testHelpers() {
    CHECK_EQ(aboveMask(M(12)), bit(M(13)) | bit(M(14)));
    CHECK_EQ(aboveMask(H(14)), 0ull);
    CHECK_EQ(popcount(suitMask(kart::Karo)), 13);
    CHECK_EQ(cardsOf(bit(5) | bit(40)), (std::vector<int>{5, 40}));
    CHECK_EQ(maskOf({1, 2, 3}), 14ull);
    // trick winner
    {
        std::vector<PlayedCard> t = {{0, H(10)}, {1, H(12)}, {2, D(14)}, {3, H(4)}};
        TrickView v = viewTrick(t, kart::Maca);
        CHECK_EQ(v.winner, 1);
        CHECK_EQ(v.ledSuit, (int)kart::Kupa);
        CHECK_EQ(v.bestLed, H(12));
        CHECK_EQ(v.bestTrump, -1);
    }
    {
        std::vector<PlayedCard> t = {{2, H(14)}, {3, M(2)}, {0, M(5)}, {1, H(13)}};
        TrickView v = viewTrick(t, kart::Maca);
        CHECK_EQ(v.winner, 0); // higher koz
        CHECK_EQ(v.bestTrump, M(5));
        CHECK_EQ(v.bestLed, H(14));
    }
    {
        std::vector<PlayedCard> t = {{1, M(3)}, {2, M(14)}, {3, H(14)}, {0, M(13)}};
        TrickView v = viewTrick(t, kart::Maca); // koz led: the highest koz
        CHECK_EQ(v.winner, 2);
    }
    CHECK_EQ(sidePoints(Rules(), true, 8, 7), 8);
    CHECK_EQ(sidePoints(Rules(), true, 6, 7), -7);
    CHECK_EQ(sidePoints(Rules(), false, 3, 7), 3);
    CHECK_EQ(sidePoints(Rules(), false, 0, 7), -7);
    Rules r10;
    r10.multiplier = 10;
    CHECK_EQ(sidePoints(r10, true, 9, 7), 90);
    CHECK_EQ(sidePoints(r10, false, 0, 6), -60);
}

void testDeal() {
    Rules r;
    r.firstDealer = 2;
    Game g = makeGame(r);
    g.startMatch(42);
    CHECK_EQ(g.stage(), Stage::Bidding);
    CHECK_EQ(g.dealer(), 2);
    CHECK_EQ(g.firstBidder(), 3);
    CHECK_EQ(g.current(), 3);
    std::set<int> all;
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ((int)g.hand(s).size(), 13);
        CHECK(std::is_sorted(g.hand(s).begin(), g.hand(s).end()));
        for (int c : g.hand(s)) all.insert(c);
    }
    CHECK_EQ((int)all.size(), 52);
    auto ev = g.drainEvents();
    CHECK(ev.size() >= 4);
    CHECK(ev[0].type == EvType::MatchStart);
    CHECK(ev[1].type == EvType::HandStart && ev[1].seat == 2 && ev[1].value == 0);
    CHECK(ev[2].type == EvType::Deal);
    CHECK(ev[3].type == EvType::TurnStart && ev[3].seat == 3 && ev[3].stage == Stage::Bidding);
    CHECK_EQ(ev[1].text, std::string("Yeni el (1.): kağıtları Cemal dağıtıyor"));

    // random first dealer is reproducible
    Game a = makeGame(), b = makeGame();
    a.startMatch(7);
    b.startMatch(7);
    CHECK_EQ(a.dealer(), b.dealer());
    for (int s = 0; s < 4; ++s) CHECK(a.hand(s) == b.hand(s));
    // dealer rotates to the right
    Rules q;
    q.firstDealer = 3;
    Game h = makeGame(q);
    h.startMatch(1);
    h.debugStartPlay({std::vector<int>{H(2)}, {H(3)}, {H(4)}, {H(5)}}, 0, 1, kart::Maca);
    CHECK(h.playCard(0, H(2)).ok);
    CHECK(h.playCard(1, H(3)).ok);
    CHECK(h.playCard(2, H(4)).ok);
    CHECK(h.playCard(3, H(5)).ok);
    CHECK_EQ(h.stage(), Stage::HandOver);
    h.startNextHand();
    CHECK_EQ(h.dealer(), 0);
    CHECK_EQ(h.handIndex(), 1);
    CHECK_EQ(h.current(), 1);
}

void testBidding() {
    Rules r;
    r.firstDealer = 0;
    Game g = makeGame(r, true);
    g.startMatch(3);
    g.drainEvents();
    CHECK_EQ(g.current(), 1);
    CHECK(!g.bid(2, 6).ok);
    CHECK_EQ(g.bid(2, 6).error, std::string("Sıra sende değil"));
    CHECK_EQ(g.bid(1, 4).error, std::string("İhale en az 5 ile açılır"));
    CHECK_EQ(g.bid(1, 14).error, std::string("En fazla 13 denebilir"));
    CHECK_EQ(g.legalBids(1).front(), 5);
    CHECK_EQ(g.legalBids(1).back(), 13);
    CHECK(g.legalBids(2).empty());
    CHECK(g.canPass(1));
    CHECK(!g.canPass(2));
    CHECK(g.bid(1, 5).ok);
    CHECK_EQ(g.highBid(), 5);
    CHECK_EQ(g.highBidder(), 1);
    CHECK_EQ(g.current(), 2);
    CHECK_EQ(g.bid(2, 5).error, std::string("İhaleyi geçmek için en az 6 demelisin"));
    CHECK_EQ(g.legalBids(2).front(), 6);
    CHECK(g.pass(2).ok);
    CHECK(g.hasPassed(2));
    CHECK(g.bid(3, 7).ok);
    CHECK(g.bid(0, 8).ok); // the human
    CHECK_EQ(g.current(), 1); // round two
    CHECK(g.bid(1, 9).ok);
    CHECK_EQ(g.current(), 3); // 2 passed: skipped
    CHECK(g.pass(3).ok);
    CHECK_EQ(g.stage(), Stage::Bidding);
    CHECK(g.pass(0).ok);
    CHECK_EQ(g.stage(), Stage::ChoosingTrump);
    CHECK_EQ(g.declarer(), 1);
    CHECK_EQ(g.contract(), 9);
    CHECK(!g.forcedContract());
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.bids().size(), 7);
    CHECK_EQ(g.lastBidOf(0), 0);
    CHECK_EQ(g.lastBidOf(1), 9);
    CHECK_EQ(g.bid(1, 10).error, std::string("İhale bitti"));
    auto ev = g.drainEvents();
    CHECK(hasEventText(ev, "Kel Mahmut 5 dedi"));
    CHECK(hasEventText(ev, "Cemal pas dedi"));
    CHECK(hasEventText(ev, "8 dedin"));
    CHECK(hasEventText(ev, "Pas dedin"));
    CHECK(hasEventText(ev, "Kel Mahmut ihaleyi 9 ile aldı"));
    CHECK(hasEventText(ev, "İhale sırası sende"));
    CHECK(hasEventText(ev, "Kel Mahmut kozu seçiyor"));
    bool won = false;
    for (auto& e : ev)
        if (e.type == EvType::BiddingWon) won = e.seat == 1 && e.value == 9 && !e.forced;
    CHECK(won);

    // a 13 ends the ihale at once
    Game k = makeGame(r, true);
    k.startMatch(3);
    CHECK(k.bid(1, 6).ok);
    CHECK(k.bid(2, 13).ok);
    CHECK_EQ(k.stage(), Stage::ChoosingTrump);
    CHECK_EQ(k.declarer(), 2);
    CHECK_EQ(k.contract(), 13);

    // the human wins it: second person
    Game hw = makeGame(r, true);
    hw.startMatch(3);
    CHECK(hw.pass(1).ok);
    CHECK(hw.pass(2).ok);
    CHECK(hw.pass(3).ok);
    CHECK_EQ(hw.stage(), Stage::Bidding); // the human still speaks
    CHECK(hw.bid(0, 7).ok);
    CHECK_EQ(hw.stage(), Stage::ChoosingTrump);
    CHECK(hasEventText(hw.drainEvents(), "İhaleyi 7 ile aldın"));
    CHECK(hw.chooseTrump(0, kart::Maca).ok);
    CHECK(hasEventText(hw.drainEvents(), "Koz maça dedin"));
}

void testAllPass() {
    // default: the first bidder (dealer's right) takes it with 4
    {
        Rules r;
        r.firstDealer = 3;
        Game g = makeGame(r, true);
        g.startMatch(5);
        g.drainEvents();
        for (int s : {0, 1, 2, 3}) CHECK(g.pass(s).ok);
        CHECK_EQ(g.stage(), Stage::ChoosingTrump);
        CHECK_EQ(g.declarer(), 0);
        CHECK_EQ(g.contract(), 4);
        CHECK(g.forcedContract());
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "Herkes pas dedi: ihaleyi 4 ile almak zorundasın"));
        bool forced = false;
        for (auto& e : ev)
            if (e.type == EvType::BiddingWon) forced = e.forced && e.value == 4;
        CHECK(forced);
    }
    {
        Rules r;
        r.firstDealer = 0;
        r.allPass = AllPass::Dealer;
        Game g = makeGame(r);
        g.startMatch(5);
        for (int s : {1, 2, 3, 0}) CHECK(g.pass(s).ok);
        CHECK_EQ(g.declarer(), 0);
        CHECK_EQ(g.contract(), 4);
        CHECK(hasEventText(g.drainEvents(), "Herkes pas dedi: Hacı Rıza ihaleyi 4 ile almak zorunda"));
    }
    {
        Rules r;
        r.firstDealer = 0;
        r.allPass = AllPass::Redeal;
        Game g = makeGame(r);
        g.startMatch(5);
        const std::vector<int> before = g.hand(0);
        g.drainEvents();
        for (int s : {1, 2, 3, 0}) CHECK(g.pass(s).ok);
        CHECK_EQ(g.stage(), Stage::Bidding);
        CHECK_EQ(g.dealer(), 1);
        CHECK_EQ(g.handIndex(), 0);
        CHECK_EQ(g.current(), 2);
        CHECK(g.bids().empty());
        CHECK(g.hand(0) != before);
        auto ev = g.drainEvents();
        bool redeal = false;
        for (auto& e : ev) redeal = redeal || (e.type == EvType::Redeal && e.text == "Herkes pas dedi, kağıtlar yeniden dağıtılıyor");
        CHECK(redeal);
    }
    // eşli: minimum 8, all pass -> 7
    {
        Rules r = Rules::esliBatak();
        r.firstDealer = 0;
        Game g = makeGame(r);
        g.startMatch(5);
        CHECK_EQ(g.legalBids(1).front(), 8);
        CHECK_EQ(g.bid(1, 7).error, std::string("İhale en az 8 ile açılır"));
        for (int s : {1, 2, 3, 0}) CHECK(g.pass(s).ok);
        CHECK_EQ(g.declarer(), 1);
        CHECK_EQ(g.contract(), 7);
    }
}

void testTrumpChoice() {
    Rules r;
    r.firstDealer = 0;
    Game g = makeGame(r);
    g.startMatch(9);
    CHECK_EQ(g.chooseTrump(1, 0).error, std::string("Şimdi koz seçilmez"));
    CHECK(g.bid(1, 6).ok);
    for (int s : {2, 3, 0}) CHECK(g.pass(s).ok);
    g.drainEvents();
    CHECK_EQ(g.chooseTrump(2, 0).error, std::string("Sıra sende değil"));
    CHECK_EQ(g.chooseTrump(1, 7).error, std::string("Geçersiz koz"));
    CHECK_EQ(g.playCard(1, g.hand(1)[0]).error, std::string("Şimdi kart oynanmaz"));
    CHECK(g.chooseTrump(1, kart::Kupa).ok);
    CHECK_EQ(g.trump(), (int)kart::Kupa);
    CHECK(!g.trumpBroken());
    CHECK_EQ(g.stage(), Stage::Playing);
    CHECK_EQ(g.current(), 1); // the declarer leads
    CHECK_EQ(g.trickLeader(), 1);
    CHECK_EQ(g.exposedSeat(), -1);
    auto ev = g.drainEvents();
    CHECK(hasEventText(ev, "Kel Mahmut koz kupa dedi"));
    CHECK(!ev.empty() && ev.back().type == EvType::TurnStart && ev.back().stage == Stage::Playing);
}

void testPlayRules() {
    const Rules r;
    // follow suit
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{H(10), D(3)}, {H(2), C(5)}, {D(4), D(5)}, {C(6), C(7)}}, 0, 1, kart::Maca);
        CHECK(g.playCard(0, H(10)).ok);
        CHECK_EQ(g.checkPlay(1, C(5)).error, std::string("Elinde kupa varken başka renk atamazsın"));
        CHECK_EQ(g.legalCards(1), std::vector<int>{H(2)}); // cannot beat: any kupa
        CHECK(g.checkPlay(1, H(2)).ok);
        CHECK_EQ(g.checkPlay(1, D(4)).error, std::string("Bu kart elinde yok"));
        CHECK_EQ(g.checkPlay(2, D(4)).error, std::string("Sıra sende değil"));
        CHECK(g.legalCards(2).empty());
    }
    // must beat
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{H(10), D(3)}, {H(2), H(12)}, {H(11), H(13)}, {C(6), C(7)}}, 0, 1, kart::Maca);
        CHECK(g.playCard(0, H(10)).ok);
        CHECK_EQ(g.legalCards(1), std::vector<int>{H(12)});
        CHECK_EQ(g.checkPlay(1, H(2)).error, std::string("Yükseltmen gerek"));
        CHECK(g.playCard(1, H(12)).ok);
        CHECK_EQ(g.legalCards(2), std::vector<int>{H(13)}); // must beat the Kız, not just the 10
        CHECK_EQ(g.checkPlay(2, H(11)).error, std::string("Yükseltmen gerek"));
        // mustBeat off
        Rules nr;
        nr.mustBeat = false;
        Game h = makeGame(nr);
        h.debugStartPlay({std::vector<int>{H(10), D(3)}, {H(2), H(12)}, {H(11), H(13)}, {C(6), C(7)}}, 0, 1, kart::Maca);
        CHECK(h.playCard(0, H(10)).ok);
        CHECK_EQ(h.legalCards(1), (std::vector<int>{H(2), H(12)}));
    }
    // must trump, must over-trump, may under-trump only when it cannot over-trump
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{H(10), D(3)}, {M(5), C(2)}, {M(3), M(9)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK(g.playCard(0, H(10)).ok);
        CHECK_EQ(g.checkPlay(1, C(2)).error, std::string("Koz çakmalısın"));
        CHECK_EQ(g.legalCards(1), std::vector<int>{M(5)});
        CHECK(g.playCard(1, M(5)).ok);
        auto ev = g.drainEvents();
        bool broke = false;
        for (auto& e : ev)
            if (e.type == EvType::TrumpBroken) broke = e.seat == 1 && e.card == M(5) && e.text == "Koz çakıldı, koz açıldı!";
        CHECK(broke);
        CHECK(g.trumpBroken());
        CHECK_EQ(g.legalCards(2), std::vector<int>{M(9)});
        CHECK_EQ(g.checkPlay(2, M(3)).error, std::string("Daha büyük koz atmalısın"));
        CHECK(g.playCard(2, M(9)).ok);
        // seat 3 cannot over-trump the 9: must still play a koz (the 4)
        CHECK_EQ(g.legalCards(3), std::vector<int>{M(4)});
        CHECK_EQ(g.checkPlay(3, D(9)).error, std::string("Koz çakmalısın"));
        CHECK(g.playCard(3, M(4)).ok);
        CHECK_EQ(g.tricksWon(2), 1);
        CHECK_EQ(g.current(), 2); // the winner leads
        CHECK_EQ(g.trickLeader(), 2);
    }
    // after a koz on the trick, following suit no longer needs to beat (default)
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{H(10), D(3)}, {M(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca, 0, true);
        CHECK(g.playCard(0, H(10)).ok);
        CHECK(g.playCard(1, M(5)).ok);
        CHECK_EQ(g.legalCards(2), (std::vector<int>{H(2), H(14)}));
        Rules rr;
        rr.mustBeatEvenIfTrumped = true;
        Game h = makeGame(rr);
        h.debugStartPlay({std::vector<int>{H(10), D(3)}, {M(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca, 0, true);
        CHECK(h.playCard(0, H(10)).ok);
        CHECK(h.playCard(1, M(5)).ok);
        CHECK_EQ(h.legalCards(2), std::vector<int>{H(14)});
    }
    // void and no koz: anything
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{H(10), D(3)}, {D(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK(g.playCard(0, H(10)).ok);
        CHECK_EQ(g.legalCards(1), (std::vector<int>{D(5), C(2)}));
        // mustTrump off: seat 3 may discard later
        Rules rr;
        rr.mustTrump = false;
        Game h = makeGame(rr);
        h.debugStartPlay({std::vector<int>{H(10), D(3)}, {M(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK(h.playCard(0, H(10)).ok);
        CHECK_EQ(h.legalCards(1), (std::vector<int>{M(5), C(2)}));
    }
    // koz cannot be led before it is broken, unless only koz is left
    {
        Game g = makeGame();
        g.debugStartPlay({std::vector<int>{M(14), D(3)}, {D(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK_EQ(g.checkPlay(0, M(14)).error, std::string("Koz henüz açılmadı"));
        CHECK_EQ(g.legalCards(0), std::vector<int>{D(3)});
        Game h = makeGame();
        h.debugStartPlay({std::vector<int>{M(14), M(3)}, {D(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK_EQ(h.legalCards(0), (std::vector<int>{M(3), M(14)}));
        CHECK(h.playCard(0, M(14)).ok);
        CHECK(h.trumpBroken());
        bool broke = false;
        for (auto& e : h.drainEvents())
            if (e.type == EvType::TrumpBroken) broke = e.text == "Koz açıldı!";
        CHECK(broke);
        CHECK_EQ(h.legalCards(1), (std::vector<int>{D(5), C(2)})); // void in maça, no koz: anything
        // after the break koz may be led
        Game k = makeGame();
        k.debugStartPlay({std::vector<int>{M(14), D(3)}, {D(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca, 0, true);
        CHECK(k.checkPlay(0, M(14)).ok);
        Rules rr;
        rr.trumpMustBeBroken = false;
        Game q = makeGame(rr);
        q.debugStartPlay({std::vector<int>{M(14), D(3)}, {D(5), C(2)}, {H(2), H(14)}, {M(4), D(9)}}, 0, 1, kart::Maca);
        CHECK(q.checkPlay(0, M(14)).ok);
    }
    (void)r;
}

void testTrickFlow() {
    Game g = makeGame(Rules(), true);
    g.debugStartPlay({std::vector<int>{H(10), C(3)}, {H(13), D(2)}, {D(14), H(3)}, {H(12), C(4)}}, 1, 1, kart::Maca, 1);
    g.drainEvents();
    CHECK(g.playCard(1, H(13)).ok);
    CHECK_EQ(g.ledSuit(), (int)kart::Kupa);
    CHECK_EQ(g.trickWinnerSoFar(), 1);
    CHECK(g.playCard(2, H(3)).ok);
    CHECK(g.playCard(3, H(12)).ok);
    CHECK_EQ((int)g.trick().size(), 3);
    CHECK(g.playCard(0, H(10)).ok);
    CHECK(g.trick().empty());
    CHECK_EQ((int)g.tricks().size(), 1);
    CHECK(g.lastTrick() && g.lastTrick()->winner == 1 && g.lastTrick()->leader == 1);
    CHECK_EQ((int)g.lastTrick()->cards.size(), 4);
    CHECK_EQ(g.tricksWon(1), 1);
    CHECK_EQ(g.current(), 1);
    CHECK_EQ(g.trickNumber(), 2);
    auto ev = g.drainEvents();
    CHECK(hasEventText(ev, "H(10)") == false);
    CHECK(hasEventText(ev, "Kupa 10 attın"));
    CHECK(hasEventText(ev, "Kel Mahmut Kupa Papaz attı"));
    CHECK(hasEventText(ev, "Kel Mahmut eli aldı"));
    bool tw = false;
    for (auto& e : ev)
        if (e.type == EvType::TrickWon) tw = e.seat == 1 && e.value == 1 && e.cards.size() == 4 && e.cards[0].seat == 1;
    CHECK(tw);
    // an off-suit card never wins, even if higher
    CHECK(g.playCard(1, D(2)).ok);
    CHECK(g.playCard(2, D(14)).ok);
    CHECK(g.playCard(3, C(4)).ok);
    CHECK(g.playCard(0, C(3)).ok);
    CHECK_EQ(g.tricksWon(2), 1);
    CHECK_EQ(g.stage(), Stage::HandOver);
    CHECK_EQ(g.current(), -1);
}

void testScoring() {
    // tekli, 1-card hands: declarer 0 made 1, the others took nothing -> batak
    {
        Game g = makeGame(Rules(), true);
        g.debugStartPlay({std::vector<int>{H(14)}, {H(2)}, {H(3)}, {H(4)}}, 0, 1, kart::Karo);
        g.drainEvents();
        for (int s = 0; s < 4; ++s) CHECK(g.playCard(s, g.hand(s)[0]).ok);
        const HandResult& r = g.lastHandResult();
        CHECK_EQ((int)r.sides.size(), 4);
        CHECK_EQ(r.sides[0].points, 1);
        CHECK(r.sides[0].declarer && r.sides[0].made);
        CHECK_EQ(r.sides[1].points, -1);
        CHECK(!r.sides[1].made);
        CHECK_EQ(g.total(0), 1);
        CHECK_EQ(g.total(3), -1);
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "İhaleyi yaptın: 1 el, +1"));
        CHECK(hasEventText(ev, "Kel Mahmut hiç el alamadı, battı: -1"));
    }
    // declarer fails; a defender scores its tricks
    {
        Game g = makeGame(Rules(), true);
        g.debugStartPlay({std::vector<int>{H(2), D(2)}, {H(14), D(14)}, {H(3), D(3)}, {H(4), D(4)}}, 0, 2, kart::Sinek);
        g.drainEvents();
        CHECK(g.playCard(0, H(2)).ok);
        CHECK(g.playCard(1, H(14)).ok);
        CHECK(g.playCard(2, H(3)).ok);
        CHECK(g.playCard(3, H(4)).ok);
        CHECK(g.playCard(1, D(14)).ok);
        CHECK(g.playCard(2, D(3)).ok);
        CHECK(g.playCard(3, D(4)).ok);
        CHECK(g.playCard(0, D(2)).ok);
        const HandResult& r = g.lastHandResult();
        CHECK_EQ(r.points[0], -2);
        CHECK_EQ(r.points[1], 2);
        CHECK_EQ(r.points[2], -2);
        CHECK_EQ(r.tricks[1], 2);
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "Battın! 0 el, -2"));
        CHECK(hasEventText(ev, "Kel Mahmut 2 el aldı: +2"));
    }
    // bot declarer texts, multiplier 10, defender minimum 2
    {
        Rules rr;
        rr.multiplier = 10;
        rr.defenderMinTricks = 2;
        Game g = makeGame(rr);
        g.debugStartPlay({std::vector<int>{H(2), D(2)}, {H(14), D(3)}, {H(3), D(14)}, {H(4), D(4)}}, 1, 2, kart::Sinek, 0);
        g.drainEvents();
        for (int t = 0; t < 2; ++t)
            for (int k = 0; k < 4; ++k) {
                const int s = g.current();
                CHECK(g.playCard(s, g.legalCards(s)[0]).ok);
            }
        const HandResult& r = g.lastHandResult();
        CHECK_EQ(r.tricks[1], 1);
        CHECK_EQ(r.tricks[2], 1);
        CHECK_EQ(r.points[1], -20);
        CHECK_EQ(r.points[2], -20); // 1 trick < 2
        CHECK_EQ(r.points[0], -20);
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "Kel Mahmut battı! 1 el, -20"));
        CHECK(hasEventText(ev, "Cemal 1 el yetmedi, battı: -20"));
    }
    // eşli: partners' tricks combine; sides 0 (0 & 2) and 1 (1 & 3)
    {
        Rules rr = Rules::esliBatak();
        rr.openDummy = false;
        Game g = makeGame(rr, true);
        g.debugStartPlay({std::vector<int>{H(14), D(2)}, {H(2), D(3)}, {H(3), D(14)}, {H(4), D(4)}}, 0, 2, kart::Sinek);
        g.drainEvents();
        for (int t = 0; t < 2; ++t)
            for (int k = 0; k < 4; ++k) {
                const int s = g.current();
                CHECK(g.playCard(s, g.legalCards(s)[0]).ok);
            }
        const HandResult& r = g.lastHandResult();
        CHECK_EQ((int)r.sides.size(), 2);
        CHECK_EQ(r.sides[0].tricks, 2);
        CHECK_EQ(r.sides[0].points, 2);
        CHECK_EQ(r.sides[1].points, -2);
        CHECK_EQ(r.points[2], 2);
        CHECK_EQ(g.total(2), 2);
        CHECK_EQ(g.sideTotal(1), -2);
        CHECK_EQ(g.sideTricks(0), 2);
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "İhaleyi yaptınız: 2 el, +2"));
        CHECK(hasEventText(ev, "Kel Mahmut ile Rıfat hiç el alamadı, battı: -2"));
    }
}

void testEsliDummy() {
    Rules rr = Rules::esliBatak();
    rr.firstDealer = 2;
    Game g = makeGame(rr, true);
    g.startMatch(11);
    CHECK_EQ(g.current(), 3);
    CHECK(g.bid(3, 8).ok);
    CHECK(g.pass(0).ok);
    CHECK(g.pass(2 - 1).ok); // seat 1 = partner of 3
    CHECK(g.pass(2).ok);
    CHECK_EQ(g.declarer(), 3);
    g.drainEvents();
    CHECK(g.chooseTrump(3, kart::Karo).ok);
    CHECK_EQ(g.exposedSeat(), 1);
    CHECK_EQ(g.controllerOf(1), 3);
    CHECK_EQ(g.controllerOf(0), 0);
    auto ev = g.drainEvents();
    bool opened = false;
    for (auto& e : ev)
        if (e.type == EvType::DummyOpen) opened = e.seat == 1 && e.cards.size() == 13 &&
                                                  e.text == "Kel Mahmut kağıtlarını açtı; Rıfat onun yerine oynayacak";
    CHECK(opened);
    // play until the dummy's turn and check the texts
    int guard = 0;
    while (g.current() != 1 && ++guard < 10) {
        const int s = g.current();
        CHECK(g.playCard(s, g.legalCards(s)[0]).ok);
    }
    g.drainEvents();
    const int c = g.legalCards(1)[0];
    CHECK(g.playCard(1, c).ok);
    CHECK(hasEventText(g.drainEvents(), "Rıfat açık elden " + kart::cardNameTR(c) + " attı"));

    // the human is the dummy
    Rules r2 = Rules::esliBatak();
    r2.firstDealer = 1;
    Game h = makeGame(r2, true);
    h.startMatch(12);
    CHECK_EQ(h.current(), 2);
    CHECK(h.bid(2, 9).ok);
    for (int s : {3, 0, 1}) CHECK(h.pass(s).ok);
    h.drainEvents();
    CHECK(h.chooseTrump(2, kart::Maca).ok);
    CHECK(hasEventText(h.drainEvents(), "Kağıtlarını açtın; Cemal senin yerine oynayacak"));
    CHECK_EQ(h.controllerOf(0), 2);
}

void testMatchEnd() {
    // target score
    {
        Rules r;
        r.targetScore = 51;
        Game g = makeGame(r, true);
        g.startMatch(1);
        g.debugSetTotals({50, 10, 3, 49});
        g.debugStartPlay({std::vector<int>{H(14)}, {H(2)}, {H(3)}, {H(4)}}, 0, 1, kart::Karo);
        g.drainEvents();
        for (int s = 0; s < 4; ++s) CHECK(g.playCard(s, g.hand(s)[0]).ok);
        CHECK_EQ(g.stage(), Stage::MatchOver);
        CHECK_EQ(g.winnerSide(), 0);
        CHECK_EQ(g.total(0), 51);
        auto ev = g.drainEvents();
        CHECK(hasEventText(ev, "Oyunu kazandın! (51)"));
        g.startNextHand(); // no-op after MatchOver
        CHECK_EQ(g.stage(), Stage::MatchOver);
    }
    // tied at the top past the target: play on
    {
        Rules r;
        Game g = makeGame(r);
        g.startMatch(1);
        g.debugSetTotals({50, 52, 3, 49});
        g.debugStartPlay({std::vector<int>{H(14)}, {H(2)}, {H(3)}, {H(4)}}, 0, 1, kart::Karo);
        for (int s = 0; s < 4; ++s) CHECK(g.playCard(s, g.hand(s)[0]).ok);
        // 0: 51, 1: 51 -> tie
        CHECK_EQ(g.stage(), Stage::HandOver);
    }
    // hand limit, a bot wins; tie at the limit
    {
        Rules r;
        r.targetScore = 0;
        r.numHands = 3;
        Game g = makeGame(r, true);
        g.startMatch(1);
        g.debugSetHandIndex(2);
        g.debugSetTotals({5, 20, 3, 4});
        g.debugStartPlay({std::vector<int>{H(14)}, {H(2)}, {H(3)}, {H(4)}}, 0, 1, kart::Karo);
        g.drainEvents();
        for (int s = 0; s < 4; ++s) CHECK(g.playCard(s, g.hand(s)[0]).ok);
        CHECK_EQ(g.stage(), Stage::MatchOver);
        CHECK_EQ(g.winnerSide(), 1);
        CHECK(hasEventText(g.drainEvents(), "Oyunu Kel Mahmut kazandı (19)"));
        Game t = makeGame(r);
        t.startMatch(1);
        t.debugSetHandIndex(2);
        t.debugSetTotals({5, 7, 3, 4}); // -> 6, 6, 2, 3
        t.debugStartPlay({std::vector<int>{H(14)}, {H(2)}, {H(3)}, {H(4)}}, 0, 1, kart::Karo);
        for (int s = 0; s < 4; ++s) CHECK(t.playCard(s, t.hand(s)[0]).ok);
        CHECK_EQ(t.stage(), Stage::MatchOver);
        CHECK_EQ(t.winnerSide(), -1);
        CHECK(hasEventText(t.drainEvents(), "Oyun berabere bitti"));
    }
    // king: 13 bid and made wins at once
    {
        Rules r;
        r.firstDealer = 3;
        Game g = makeGame(r);
        g.startMatch(1);
        g.debugRedeal(3, suitDeal());
        CHECK(g.bid(0, 13).ok);
        CHECK(g.chooseTrump(0, kart::Maca).ok);
        CHECK_EQ(g.legalCards(0).size(), 13u); // only koz in hand: may lead koz
        for (int t = 0; t < 13; ++t)
            for (int k = 0; k < 4; ++k) {
                const int s = g.current();
                CHECK(g.playCard(s, g.legalCards(s)[0]).ok);
            }
        CHECK_EQ(g.tricksWon(0), 13);
        CHECK(g.lastHandResult().king);
        CHECK_EQ(g.stage(), Stage::MatchOver);
        CHECK_EQ(g.winnerSide(), 0);
        CHECK(hasEventText(g.drainEvents(), "King! Oyunu Hacı Rıza kazandı (13)"));
    }
    // totals = sum of the hand results over a whole bot match
    {
        Rules r;
        r.targetScore = 30;
        Game g = makeGame(r);
        g.startMatch(77);
        std::vector<Bot> bots;
        for (int s = 0; s < 4; ++s) bots.emplace_back(Level::Usta, 100 + s);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && ++guard < 100000) {
            if (g.stage() == Stage::HandOver) {
                g.startNextHand();
                continue;
            }
            const int s = g.current();
            CHECK(applyAction(g, s, bots[g.controllerOf(s)].next(g, s)).ok);
        }
        CHECK_EQ(g.stage(), Stage::MatchOver);
        std::array<int, 4> sum{};
        for (const HandResult& h : g.handResults())
            for (const SideResult& sr : h.sides) sum[sr.side] += sr.points;
        for (int s = 0; s < 4; ++s) CHECK_EQ(sum[s], g.sideTotal(s));
        CHECK(g.sideTotal(g.winnerSide()) >= 30);
        CHECK_EQ((int)g.handResults().size(), g.handIndex() + 1);
    }
}

// ---------------------------------------------------------------------------------------------------------
// random fuzzer

void checkInvariants(const Game& g, int handCards) {
    uint64_t seen = 0;
    int n = 0;
    for (int s = 0; s < 4; ++s) {
        const uint64_t m = g.handMask(s);
        CHECK((seen & m) == 0);
        seen |= m;
        n += (int)g.hand(s).size();
    }
    if (g.stage() == Stage::Playing || g.stage() == Stage::HandOver || g.stage() == Stage::MatchOver) {
        CHECK((seen & g.playedMask()) == 0);
        CHECK_EQ(n + popcount(g.playedMask()), handCards);
        uint64_t inTricks = 0;
        for (const Trick& t : g.tricks()) {
            CHECK_EQ((int)t.cards.size(), 4);
            for (int i = 0; i < 4; ++i) {
                CHECK_EQ(t.cards[(size_t)i].seat, (t.leader + i) % 4);
                inTricks |= bit(t.cards[(size_t)i].card);
            }
            CHECK_EQ(viewTrick(t.cards, g.trump()).winner, t.winner);
        }
        for (const PlayedCard& pc : g.trick()) inTricks |= bit(pc.card);
        CHECK_EQ(inTricks, g.playedMask());
        int won = 0;
        for (int s = 0; s < 4; ++s) won += g.tricksWon(s);
        CHECK_EQ(won, (int)g.tricks().size());
    }
}

void fuzz(const char* name, Rules r, int matches, uint64_t seed0) {
    okey::Rng rng(seed0);
    long long hands = 0, cards = 0;
    for (int m = 0; m < matches; ++m) {
        Game g(r);
        for (int s = 0; s < 4; ++s) g.setPlayer(s, "P" + std::to_string(s), s == 0);
        g.startMatch(seed0 + (uint64_t)m);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && ++guard < 200000) {
            if (g.stage() == Stage::HandOver) {
                ++hands;
                g.startNextHand();
                continue;
            }
            const int s = g.current();
            CHECK(s >= 0 && s < 4);
            if (g.stage() == Stage::Bidding) {
                const auto lb = g.legalBids(s);
                CHECK(g.canPass(s));
                for (int o = 0; o < 4; ++o)
                    if (o != s) CHECK(g.legalBids(o).empty());
                if (!lb.empty() && rng.chance(0.25f)) {
                    const int v = lb[(size_t)rng.range((int)std::min<size_t>(lb.size(), 4))];
                    CHECK(g.bid(s, v).ok);
                } else {
                    if (rng.chance(0.1f)) CHECK(!g.bid(s, std::max(1, g.highBid())).ok);
                    CHECK(g.pass(s).ok);
                }
            } else if (g.stage() == Stage::ChoosingTrump) {
                CHECK_EQ(s, g.declarer());
                CHECK(g.chooseTrump(s, rng.range(4)).ok);
            } else {
                CHECK(g.stage() == Stage::Playing);
                const auto legal = g.legalCards(s);
                CHECK(!legal.empty());
                for (int c : legal) CHECK(g.checkPlay(s, c).ok);
                for (int c : g.hand(s)) {
                    const bool isLegal = std::find(legal.begin(), legal.end(), c) != legal.end();
                    CHECK_EQ(g.checkPlay(s, c).ok, isLegal);
                }
                // an illegal attempt is rejected and changes nothing
                if (legal.size() < g.hand(s).size()) {
                    for (int c : g.hand(s))
                        if (std::find(legal.begin(), legal.end(), c) == legal.end()) {
                            CHECK(!g.playCard(s, c).ok);
                            break;
                        }
                }
                CHECK(g.playCard(s, legal[(size_t)rng.range((int)legal.size())]).ok);
                ++cards;
                checkInvariants(g, 52);
            }
        }
        CHECK_EQ(g.stage(), Stage::MatchOver);
        // scores consistent
        std::array<int, 4> sum{};
        for (const HandResult& h : g.handResults()) {
            int tr = 0;
            for (int s = 0; s < 4; ++s) tr += h.tricks[s];
            CHECK_EQ(tr, 13);
            for (const SideResult& sr : h.sides) {
                sum[sr.side] += sr.points;
                CHECK_EQ(sr.points, sidePoints(r, sr.declarer, sr.tricks, h.contract));
                int t = 0;
                for (int s : sr.seats) t += h.tricks[s];
                CHECK_EQ(t, sr.tricks);
            }
        }
        for (int s = 0; s < g.numSides(); ++s) CHECK_EQ(sum[s], g.sideTotal(s));
        ++hands;
    }
    std::printf("  fuzz %-22s %d matches, %lld hands, %lld cards\n", name, matches, hands, cards);
}

void testFuzz(int scale) {
    Rules t;
    t.targetScore = 25;
    fuzz("tekli", t, 60 * scale, 1000);
    Rules e = Rules::esliBatak();
    e.targetScore = 40;
    fuzz("eşli", e, 60 * scale, 2000);
    Rules v;
    v.mustBeat = false;
    v.trumpMustBeBroken = false;
    v.allPass = AllPass::Redeal;
    v.numHands = 6;
    v.targetScore = 0;
    fuzz("varyant (redeal, no beat)", v, 40 * scale, 3000);
    Rules w;
    w.mustOvertrump = false;
    w.mustBeatEvenIfTrumped = true;
    w.allPass = AllPass::Dealer;
    w.multiplier = 10;
    w.targetScore = 300;
    fuzz("varyant (dealer, x10)", w, 40 * scale, 4000);
}

// ---------------------------------------------------------------------------------------------------------
// determinism and bots

std::vector<std::string> playMatchWithBots(uint64_t seed, Level a, Level b, bool esli) {
    Rules r = esli ? Rules::esliBatak() : Rules::tekli();
    r.numHands = 3;
    r.targetScore = 0;
    Game g(r);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, "B" + std::to_string(s), false);
    std::vector<Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(s % 2 ? b : a, seed + (uint64_t)s);
    g.startMatch(seed);
    std::vector<std::string> log;
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 10000) {
        if (g.stage() == Stage::HandOver) g.startNextHand();
        else {
            const int s = g.current();
            const int ctl = g.controllerOf(s);
            const Action act = bots[ctl].next(g, s);
            const ActionResult res = applyAction(g, s, act);
            CHECK(res.ok);
            if (!res.ok) {
                std::printf("    rejected: %s\n", res.error.c_str());
                applyAction(g, s, fallbackAction(g, s));
            }
        }
        for (const GameEvent& e : g.drainEvents()) {
            log.push_back(e.text);
            if (e.type == EvType::HandStart) for (auto& bt : bots) bt.resetForHand();
            for (auto& bt : bots) bt.observe(e, g);
        }
    }
    CHECK_EQ(g.stage(), Stage::MatchOver);
    return log;
}

void testDeterminism() {
    CHECK(playMatchWithBots(5, Level::Kurt, Level::Usta, false) == playMatchWithBots(5, Level::Kurt, Level::Usta, false));
    CHECK(playMatchWithBots(6, Level::Acemi, Level::Kurt, true) == playMatchWithBots(6, Level::Acemi, Level::Kurt, true));
    CHECK(playMatchWithBots(5, Level::Kurt, Level::Usta, false) != playMatchWithBots(8, Level::Kurt, Level::Usta, false));
}

void testBotsLegal() {
    for (int lv = 0; lv < 3; ++lv)
        for (int e = 0; e < 2; ++e) playMatchWithBots(40 + (uint64_t)lv * 3 + (uint64_t)e, (Level)lv, (Level)((lv + 1) % 3), e == 1);
    // fallbackAction is legal in every stage
    Rules r;
    r.firstDealer = 0;
    Game g = makeGame(r);
    g.startMatch(4);
    CHECK(applyAction(g, 1, fallbackAction(g, 1)).ok); // pas
    CHECK(g.bid(2, 5).ok);
    for (int s : {3, 0}) CHECK(g.pass(s).ok); // seat 1 already passed: skipped
    CHECK(applyAction(g, 2, fallbackAction(g, 2)).ok); // koz
    for (int i = 0; i < 52; ++i) CHECK(applyAction(g, g.current(), fallbackAction(g, g.current())).ok);
    CHECK_EQ(g.stage(), Stage::HandOver);
}

// Bots must not peek: shuffling the cards between the OTHER hidden hands must not change their decision.
void testFairness() {
    for (int lv = 0; lv < 3; ++lv) {
        for (int trial = 0; trial < 6; ++trial) {
            // bidding: seat 1 to speak
            auto hands = randomDeal(500 + (uint64_t)trial);
            Game a = makeGame(), b = makeGame();
            a.startMatch(1);
            b.startMatch(1);
            a.debugRedeal(0, hands);
            auto other = hands;
            // swap the hands of seats 2 and 3, and mix cards between seats 0 and 3
            std::swap(other[2], other[3]);
            std::swap(other[0][0], other[3][5]);
            b.debugRedeal(0, other);
            Bot x((Level)lv, 99), y((Level)lv, 99);
            const Action ax = x.next(a, 1), ay = y.next(b, 1);
            CHECK(ax.kind == ay.kind && ax.value == ay.value);

            // play: seat 3 (declarer) leads, seat 0 decides; seats 1 and 2 exchange their hidden hands
            Game c = makeGame(), e = makeGame();
            c.debugStartPlay(hands, 3, 5, kart::Kupa, 3);
            e.debugStartPlay({hands[0], hands[2], hands[1], hands[3]}, 3, 5, kart::Kupa, 3);
            const int lead = c.legalCards(3)[0];
            CHECK(c.playCard(3, lead).ok);
            CHECK(e.playCard(3, lead).ok);
            Bot p((Level)lv, 1234), q((Level)lv, 1234);
            CHECK_EQ(p.next(c, 0).card, q.next(e, 0).card);
            // and after a few tricks (voids / inferences come only from the public history)
            Bot u(Level::Usta, 7);
            for (int k = 0; k < 15 && c.stage() == Stage::Playing; ++k) {
                const int s = c.current();
                const Action m = u.next(c, s);
                CHECK(applyAction(c, s, m).ok);
            }
            (void)ay;
        }
    }
}

void testBotTiming() {
    // Kurt decisions on random positions: p50 / p99 / max
    std::vector<double> t;
    for (int m = 0; m < 6; ++m) {
        Rules r = m % 2 ? Rules::esliBatak() : Rules::tekli();
        r.numHands = 1;
        r.targetScore = 0;
        Game g(r);
        g.startMatch(900 + (uint64_t)m);
        std::vector<Bot> bots;
        for (int s = 0; s < 4; ++s) bots.emplace_back(Level::Kurt, (uint64_t)s + 1);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && g.stage() != Stage::HandOver && ++guard < 1000) {
            const int s = g.current();
            const auto t0 = std::chrono::steady_clock::now();
            const Action a = bots[g.controllerOf(s)].next(g, s);
            t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            CHECK(applyAction(g, s, a).ok);
        }
    }
    std::sort(t.begin(), t.end());
    const double p50 = t[t.size() / 2], p99 = t[std::min(t.size() - 1, t.size() * 99 / 100)], mx = t.back();
    std::printf("  Kurt decision time over %zu decisions: p50 %.2f ms, p99 %.2f ms, max %.2f ms\n", t.size(), p50, p99, mx);
#ifndef __OPTIMIZE__
    (void)mx;
#else
    CHECK(mx < 150.0);
#endif
}

void testEstimate() {
    // a long strong koz suit is worth more than a weak hand
    const uint64_t strong = maskOf({M(14), M(13), M(12), M(11), M(10), M(9), H(14), H(13), D(14), C(2), C(3), D(2), H(2)});
    const uint64_t weak = maskOf({M(2), M(3), M(5), H(4), H(6), H(8), D(3), D(7), D(9), C(4), C(6), C(9), C(11)});
    CHECK(estimateTricks(strong, kart::Maca) > 7.0);
    CHECK(estimateTricks(weak, kart::Maca) < 2.0);
    CHECK_EQ(bestTrumpFor(strong), (int)kart::Maca);
    CHECK(std::string(levelNameTR(Level::Kurt)) == "Kurt");
}

} // namespace

// Personalities: on the same opening bid (same seed, same hand) a bold bot bids whenever a neutral one does and a
// cautious one only when a neutral one does; a bold bot sometimes jumps one above the minimum; the presets differ.
void testStyles() {
    CHECK(BotStyle::forSeat(0).neutral());
    CHECK(BotStyle::forSeat(1).neutral());
    CHECK(BotStyle::forSeat(2).boldness > 0.f);
    CHECK(BotStyle::forSeat(3).boldness < 0.f);
    const BotStyle styles[3] = {BotStyle{}, BotStyle::bold(), BotStyle::cautious()};
    for (int lvl = 1; lvl < 3; ++lvl) {
        int bids[3] = {0, 0, 0}, jumps = 0, monotone = 0;
        const int deals = lvl == 1 ? 200 : 40;
        for (int d = 0; d < deals; ++d) {
            int val[3];
            for (int k = 0; k < 3; ++k) {
                Game g = makeGame();
                g.startMatch(1);
                g.debugRedeal(3, randomDeal(9000 + (uint64_t)d));
                g.drainEvents();
                const int seat = g.current();
                Bot b((Level)lvl, 50 + (uint64_t)d);
                b.setStyle(styles[k]);
                const Action a = b.next(g, seat);
                CHECK(applyAction(g, seat, a).ok);
                val[k] = a.kind == Action::Kind::Bid ? a.value : 0;
                bids[k] += val[k] > 0;
            }
            if (val[1] > Rules().minBid) ++jumps;
            if (val[1] >= val[0] && val[0] >= val[2]) ++monotone;
        }
        CHECK_EQ(monotone, deals);
        CHECK(bids[1] > bids[0]);
        CHECK(bids[0] > bids[2]);
        if (lvl == 1) CHECK(jumps > 0);
        std::printf("  styles, %s: opening bids on %d deals: bold %d (%d jumps), neutral %d, cautious %d\n",
                    levelNameTR((Level)lvl), deals, bids[1], jumps, bids[0], bids[2]);
    }
}

// Save / resume: the action log, written as text lines and replayed on a freshly started game with the same seed,
// reproduces the state (checked every few actions through whole matches: tekli, eşli, koz açık).
void sameBatak(const Game& a, const Game& b) {
    CHECK_EQ((int)a.stage(), (int)b.stage());
    CHECK_EQ(a.handIndex(), b.handIndex());
    CHECK_EQ(a.dealer(), b.dealer());
    CHECK_EQ(a.current(), b.current());
    CHECK_EQ(a.declarer(), b.declarer());
    CHECK_EQ(a.contract(), b.contract());
    CHECK_EQ(a.trump(), b.trump());
    CHECK_EQ(a.trumpBroken(), b.trumpBroken());
    CHECK_EQ(a.highBid(), b.highBid());
    CHECK_EQ(a.bids().size(), b.bids().size());
    CHECK_EQ(a.trick().size(), b.trick().size());
    CHECK_EQ(a.tricks().size(), b.tricks().size());
    CHECK_EQ(a.handResults().size(), b.handResults().size());
    CHECK_EQ(a.actionLog().size(), b.actionLog().size());
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ(a.hand(s), b.hand(s));
        CHECK_EQ(a.tricksWon(s), b.tricksWon(s));
        CHECK_EQ(a.total(s), b.total(s));
    }
}

void testReplay() {
    for (int variant = 0; variant < 3; ++variant) {
        Rules r = variant == 1 ? Rules::esliBatak() : Rules::tekli();
        if (variant == 2) r.trumpMustBeBroken = false;
        const uint64_t seed = 70 + (uint64_t)variant;
        Game g = makeGame(r, true);
        g.startMatch(seed);
        std::vector<Bot> bots;
        for (int s = 0; s < 4; ++s) bots.emplace_back(Level::Usta, seed * 4 + (uint64_t)s);
        int steps = 0, replays = 0;
        while (g.stage() != Stage::MatchOver && steps < 5000) {
            if (g.stage() == Stage::HandOver) {
                g.startNextHand();
            } else {
                const int seat = g.current();
                const Action a = bots[(size_t)g.controllerOf(seat)].next(g, seat);
                if (!applyAction(g, seat, a).ok) CHECK(applyAction(g, seat, fallbackAction(g, seat)).ok);
            }
            ++steps;
            if (steps % 29 == 0 || g.stage() == Stage::MatchOver || g.stage() == Stage::HandOver) {
                std::vector<std::string> lines;
                for (const LoggedAction& la : g.actionLog()) lines.push_back(la.encode());
                Game h = makeGame(r, true);
                h.startMatch(g.matchSeed());
                bool ok = true;
                for (const std::string& l : lines) {
                    LoggedAction la;
                    ok = ok && LoggedAction::decode(l, la) && h.replay(la);
                }
                CHECK(ok);
                sameBatak(g, h);
                ++replays;
            }
        }
        CHECK_EQ((int)g.stage(), (int)Stage::MatchOver);
        CHECK(replays > 10);
    }
    LoggedAction x;
    CHECK(!LoggedAction::decode("", x));
    CHECK(!LoggedAction::decode("1 2", x));
    CHECK(!LoggedAction::decode("9 0 0", x));
    CHECK(!LoggedAction::decode("0 1 x", x));
    CHECK(LoggedAction::decode("0 1 7", x) && x.kind == LogKind::Bid && x.seat == 1 && x.value == 7);
    Game h = makeGame();
    h.startMatch(3);
    CHECK(!h.replay({LogKind::NextHand, -1, -1})); // not between hands
    CHECK(!h.replay({LogKind::Play, h.current(), h.hand(h.current())[0]})); // bidding, not play
    CHECK(h.actionLog().empty());
}

int main(int argc, char** argv) {
    const int scale = argc > 1 ? std::max(1, std::atoi(argv[1])) : 1;
    testHelpers();
    testDeal();
    testBidding();
    testAllPass();
    testTrumpChoice();
    testPlayRules();
    testTrickFlow();
    testScoring();
    testEsliDummy();
    testMatchEnd();
    testEstimate();
    testFuzz(scale);
    testDeterminism();
    testBotsLegal();
    testFairness();
    testBotTiming();
    testStyles();
    testReplay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
