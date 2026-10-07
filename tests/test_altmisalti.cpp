// Altmışaltı (66) engine + bot tests: the deck and the deal, card points and trick order, the open-stock and strict
// rules, drawing, marriages (and the first-trick rule), the koz 9 exchange, closing (and its penalty), the game point
// schedule, the last trick, the match end, event texts, the public facts the bots read, determinism, the action log
// (encode / decode / replay), the bots (legal play, Kurt's endgame, timing) and a random fuzzer.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Altmisalti.cpp src/core/AltmisaltiBot.cpp
//        tests/test_altmisalti.cpp -o build/altmisalti/test_altmisalti
#include "core/Altmisalti.h"
#include "core/AltmisaltiBot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace altmisalti;

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
std::string show(Stage s) { return std::to_string((int)s); }
std::string show(EndReason s) { return std::to_string((int)s); }

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

constexpr int MACA = kart::Maca, KUPA = kart::Kupa, KARO = kart::Karo, SINEK = kart::Sinek;
constexpr int VALE = kart::Vale, KIZ = kart::Kiz, PAPAZ = kart::Papaz, AS = kart::As;
int C(int suit, int rank) { return kart::makeCard(suit, rank); }

bool hasEvent(const std::vector<GameEvent>& ev, EvType t) {
    for (const GameEvent& e : ev)
        if (e.type == t) return true;
    return false;
}
const GameEvent* findEvent(const std::vector<GameEvent>& ev, EvType t) {
    for (const GameEvent& e : ev)
        if (e.type == t) return &e;
    return nullptr;
}

Game freshGame(uint64_t seed = 5) {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(seed);
    g.drainEvents();
    return g;
}

// A stock with the koz card `koz` at the bottom and `rest` (bottom up, the last is the top).
std::vector<int> stockOf(int koz, std::vector<int> rest) {
    std::vector<int> v{koz};
    v.insert(v.end(), rest.begin(), rest.end());
    return v;
}

void testCardsAndDeal() {
    int sum = 0, n = 0;
    for (int c = 0; c < kart::NUM_CARDS; ++c)
        if (inDeck(c)) {
            sum += cardPoints(c);
            ++n;
            CHECK(DECK_MASK & cardBit(c));
        } else {
            CHECK(!(DECK_MASK & cardBit(c)));
        }
    CHECK_EQ(n, DECK_SIZE);
    CHECK_EQ(sum, 120);
    CHECK_EQ(popcount(DECK_MASK), 24);
    CHECK_EQ(cardPoints(C(KUPA, AS)), 11);
    CHECK_EQ(cardPoints(C(KUPA, 10)), 10);
    CHECK_EQ(cardPoints(C(KUPA, PAPAZ)), 4);
    CHECK_EQ(cardPoints(C(KUPA, KIZ)), 3);
    CHECK_EQ(cardPoints(C(KUPA, VALE)), 2);
    CHECK_EQ(cardPoints(C(KUPA, 9)), 0);
    // order: 9 < V < K < P < 10 < A
    CHECK(cardOrder(C(MACA, 10)) > cardOrder(C(MACA, PAPAZ)));
    CHECK(cardOrder(C(MACA, AS)) > cardOrder(C(MACA, 10)));
    CHECK(cardOrder(C(MACA, VALE)) > cardOrder(C(MACA, 9)));
    CHECK(beats(C(MACA, PAPAZ), C(MACA, 10), KUPA));
    CHECK(!beats(C(MACA, 10), C(MACA, PAPAZ), KUPA));
    CHECK(beats(C(MACA, AS), C(KUPA, 9), KUPA));     // a koz takes
    CHECK(!beats(C(MACA, 9), C(KARO, AS), KUPA));    // another suit does not
    CHECK(!beats(C(KUPA, 9), C(MACA, AS), KUPA));

    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(11);
    const std::vector<GameEvent> ev = g.drainEvents();
    CHECK(hasEvent(ev, EvType::MatchStart));
    CHECK(hasEvent(ev, EvType::HandStart));
    CHECK(hasEvent(ev, EvType::Deal));
    CHECK_EQ(g.stage(), Stage::Playing);
    CHECK_EQ(g.handSize(0), 6);
    CHECK_EQ(g.handSize(1), 6);
    CHECK_EQ(g.stockCount(), 12);
    CHECK_EQ(g.trumpSuit(), kart::suitOf(g.trumpCard()));
    CHECK_EQ(g.faceUpTrump(), g.trumpCard());
    CHECK_EQ(g.current(), 1 - g.dealer()); // the non-dealer leads
    CardMask all = maskOf(g.hand(0)) | maskOf(g.hand(1)) | maskOf(g.stockCards());
    CHECK_EQ(all, DECK_MASK);
    CHECK_EQ(popcount(maskOf(g.hand(0)) & maskOf(g.hand(1))), 0);
    const GameEvent* d = findEvent(ev, EvType::Deal);
    CHECK(d && d->card == g.trumpCard());
    CHECK(d && d->text.find("Koz ") == 0);
    // the dealer alternates
    const int d0 = g.dealer();
    g.debugSetDeal({C(MACA, AS)}, {C(MACA, 9)}, {}, 0);
    g.playCard(0, C(MACA, AS));
    g.playCard(1, C(MACA, 9));
    CHECK_EQ(g.stage(), Stage::HandOver);
    g.startNextHand();
    CHECK_EQ(g.dealer(), 1 - d0);
    CHECK_EQ(g.handIndex(), 1);
}

void testOpenStockPlay() {
    Game g = freshGame();
    // koz Kupa; player 0 leads Maça 9, player 1 may answer anything (no obligation while the stock is open)
    g.debugSetDeal({C(MACA, 9), C(MACA, AS), C(KARO, 9), C(KARO, VALE), C(SINEK, 9), C(SINEK, VALE)},
                   {C(MACA, 10), C(KARO, AS), C(KUPA, 9), C(SINEK, AS), C(SINEK, 10), C(KARO, 10)},
                   stockOf(C(KUPA, VALE), {C(MACA, VALE), C(MACA, KIZ), C(MACA, PAPAZ), C(KARO, KIZ), C(KARO, PAPAZ),
                                           C(SINEK, KIZ), C(SINEK, PAPAZ), C(KUPA, KIZ), C(KUPA, PAPAZ), C(KUPA, 10), C(KUPA, AS)}),
                   0);
    CHECK_EQ(g.trumpSuit(), KUPA);
    CHECK(!g.strict());
    CHECK(g.playCard(1, C(MACA, 10)).ok == false); // not his turn
    CHECK(g.playCard(0, C(KUPA, AS)).ok == false); // not in hand
    CHECK(g.playCard(0, C(MACA, 9)).ok);
    CHECK_EQ((int)g.legalCards(1).size(), 6);        // anything
    CHECK(g.playCard(1, C(KARO, AS)).ok);             // a discard: the leader takes it
    CHECK_EQ(g.tricks(0), 1);
    CHECK_EQ(g.points(0), 11);
    CHECK_EQ(g.current(), 0);
    // the winner drew the top (Kupa As), the other the next (Kupa 10)
    CHECK(maskOf(g.hand(0)) & cardBit(C(KUPA, AS)));
    CHECK(maskOf(g.hand(1)) & cardBit(C(KUPA, 10)));
    CHECK_EQ(g.stockCount(), 10);
    // a koz takes a side card
    CHECK(g.playCard(0, C(MACA, AS)).ok);
    CHECK(g.playCard(1, C(KUPA, 9)).ok);
    CHECK_EQ(g.tricks(1), 1);
    CHECK_EQ(g.points(1), 11);
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.wonCards(1).size(), 2);
    const std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* t = findEvent(ev, EvType::TrickWon);
    CHECK(t && t->seat == 0 && t->amount == 11);
}

void testMarriage() {
    Game g = freshGame();
    const std::vector<int> stock = stockOf(C(KUPA, 9), {C(MACA, VALE), C(MACA, 9), C(KARO, KIZ), C(KARO, PAPAZ), C(SINEK, KIZ),
                                                         C(SINEK, PAPAZ), C(SINEK, VALE), C(KARO, VALE), C(KARO, 9), C(SINEK, 9), C(MACA, 10)});
    // player 0: Maça Kız + Papaz (20) and Kupa Kız + Papaz (40, koz); no trick yet: the points wait
    g.debugSetDeal({C(MACA, KIZ), C(MACA, PAPAZ), C(KUPA, KIZ), C(KUPA, PAPAZ), C(KARO, AS), C(SINEK, AS)},
                   {C(MACA, AS), C(KUPA, AS), C(KUPA, 10), C(KUPA, VALE), C(KARO, 10), C(SINEK, 10)}, stock, 0);
    CHECK_EQ(g.marriageValue(0, C(MACA, KIZ)), 20);
    CHECK_EQ(g.marriageValue(0, C(KUPA, PAPAZ)), 40);
    CHECK_EQ(g.marriageValue(0, C(KARO, AS)), 0);
    CHECK_EQ(g.marriageValue(1, C(KUPA, AS)), 0);
    CHECK(g.playCard(0, C(MACA, KIZ)).ok);
    CHECK_EQ(g.points(0), 0);
    CHECK_EQ(g.pendingMarriage(0), 20);
    CHECK(g.shownCards(0) & cardBit(C(MACA, PAPAZ))); // everybody knows the Papaz is there now
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* m = findEvent(ev, EvType::Marriage);
    CHECK(m && m->amount == 20 && m->suit == MACA);
    CHECK(m && m->text.find("ilk elini alınca") != std::string::npos);
    // the follower may not declare (only a lead declares)
    CHECK_EQ(g.marriageValue(1, C(KUPA, VALE)), 0);
    CHECK(g.playCard(1, C(MACA, AS)).ok); // player 1 takes it: the 20 still waits
    CHECK_EQ(g.points(1), 14);
    CHECK_EQ(g.pendingMarriage(0), 20);
    // player 1 leads, player 0 takes it: now the 20 is written
    CHECK(g.playCard(1, C(KARO, 10)).ok);
    CHECK(g.playCard(0, C(KARO, AS)).ok);
    CHECK_EQ(g.tricks(0), 1);
    CHECK_EQ(g.points(0), 21 + 20);
    CHECK_EQ(g.pendingMarriage(0), 0);
    // the koz marriage (40) with a trick: 41 + 40 = 81 >= 66 -> the hand is over at once
    CHECK(g.playCard(0, C(KUPA, KIZ)).ok);
    CHECK_EQ(g.stage(), Stage::HandOver);
    CHECK_EQ(g.sheet().back().winner, 0);
    CHECK_EQ(g.sheet().back().reason, EndReason::Marriage);
    CHECK_EQ(g.sheet().back().gamePoints, 2); // player 1 has 14 < 33
    CHECK_EQ(g.sheet().back().marriages[0], 60);
    ev = g.drainEvents();
    const GameEvent* he = findEvent(ev, EvType::HandEnd);
    CHECK(he && he->text.find("66'yı buldun") == 0);
}

void testExchange() {
    Game g = freshGame();
    const std::vector<int> stock = stockOf(C(KUPA, AS), {C(MACA, VALE), C(MACA, 9), C(KARO, KIZ), C(KARO, PAPAZ), C(SINEK, KIZ),
                                                          C(SINEK, PAPAZ), C(SINEK, VALE), C(KARO, VALE), C(KARO, 9), C(SINEK, 9), C(MACA, 10)});
    g.debugSetDeal({C(KUPA, 9), C(MACA, AS), C(KUPA, KIZ), C(KUPA, PAPAZ), C(KARO, AS), C(SINEK, AS)},
                   {C(MACA, KIZ), C(MACA, PAPAZ), C(KUPA, 10), C(KUPA, VALE), C(KARO, 10), C(SINEK, 10)}, stock, 0);
    CHECK(!g.canExchange(0));                   // no trick yet
    CHECK(!g.exchangeNine(0).ok);
    CHECK(g.playCard(0, C(MACA, AS)).ok);
    CHECK(!g.canExchange(1));                   // not on lead
    CHECK(g.playCard(1, C(MACA, KIZ)).ok);
    CHECK(g.canExchange(0));
    CHECK(g.exchangeNine(0).ok);
    CHECK(maskOf(g.hand(0)) & cardBit(C(KUPA, AS)));
    CHECK(!(maskOf(g.hand(0)) & cardBit(C(KUPA, 9))));
    CHECK_EQ(g.trumpCard(), C(KUPA, 9));
    CHECK_EQ(g.faceUpTrump(), C(KUPA, 9));
    CHECK(g.shownCards(0) & cardBit(C(KUPA, AS)));
    CHECK(!g.canExchange(0));
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* e = findEvent(ev, EvType::Exchange);
    CHECK(e && e->card == C(KUPA, 9) && e->amount == C(KUPA, AS));
    // not when the stock is down to its last two cards
    Game h = freshGame();
    h.debugSetDeal({C(KUPA, 9), C(MACA, AS)}, {C(MACA, KIZ), C(MACA, PAPAZ)}, stockOf(C(KUPA, AS), {C(MACA, 9)}), 0);
    h.debugSetPoints(0, 10, 1);
    CHECK(!h.canExchange(0));
    // nor after the stock was closed
    Game k = freshGame();
    k.debugSetDeal({C(KUPA, 9), C(MACA, AS)}, {C(MACA, KIZ), C(MACA, PAPAZ)},
                   stockOf(C(KUPA, AS), {C(MACA, 9), C(KARO, 9), C(KARO, VALE)}), 0);
    k.debugSetPoints(0, 10, 1);
    CHECK(k.canExchange(0));
    CHECK(k.closeStock(0).ok);
    CHECK(!k.canExchange(0));
}

void testCloseAndStrict() {
    Game g = freshGame();
    const std::vector<int> stock = stockOf(C(KUPA, 9), {C(MACA, VALE), C(MACA, 9), C(KARO, KIZ), C(KARO, PAPAZ), C(SINEK, KIZ),
                                                         C(SINEK, PAPAZ), C(SINEK, VALE), C(KARO, VALE), C(KARO, 9), C(SINEK, 9), C(MACA, 10)});
    g.debugSetDeal({C(MACA, AS), C(KUPA, AS), C(KUPA, 10), C(KARO, AS), C(SINEK, AS), C(SINEK, 10)},
                   {C(MACA, KIZ), C(MACA, PAPAZ), C(KUPA, KIZ), C(KUPA, VALE), C(KARO, 10), C(KUPA, PAPAZ)}, stock, 1);
    // player 1 leads; player 0 may not close (not on lead)
    CHECK(!g.canClose(0));
    CHECK(g.canClose(1));
    CHECK(g.closeStock(1).ok);
    CHECK(g.closed());
    CHECK(g.strict());
    CHECK_EQ(g.faceUpTrump(), -1);
    CHECK(!g.closeStock(1).ok);
    // player 1 leads Maça Kız: player 0 must head it with the Maça As
    CHECK(g.playCard(1, C(MACA, KIZ)).ok);
    std::vector<int> l = g.legalCards(0);
    CHECK_EQ((int)l.size(), 1);
    CHECK(!l.empty() && l[0] == C(MACA, AS));
    ActionResult r = g.playCard(0, C(KUPA, 10));
    CHECK(!r.ok);
    CHECK(r.error.find("renge uymalısın") != std::string::npos);
    CHECK(g.playCard(0, C(MACA, AS)).ok);
    CHECK_EQ(g.stockCount(), 12); // nobody draws from a closed stock
    CHECK_EQ(g.handSize(0), 5);
    // player 0 leads Karo As: player 1 has the Karo 10 (must follow; cannot beat)
    CHECK(g.playCard(0, C(KARO, AS)).ok);
    l = g.legalCards(1);
    CHECK_EQ((int)l.size(), 1);
    CHECK(g.playCard(1, C(KARO, 10)).ok);
    // Sinek As: player 1 has no Sinek -> must play a koz
    CHECK(g.playCard(0, C(SINEK, AS)).ok);
    l = g.legalCards(1);
    for (int c : l) CHECK_EQ(kart::suitOf(c), KUPA);
    r = g.playCard(1, C(MACA, PAPAZ));
    CHECK(!r.ok);
    CHECK(r.error.find("koz çakmalısın") != std::string::npos);
    CHECK(g.playCard(1, C(KUPA, VALE)).ok);
    // the deductions: after failing Sinek with a koz, nothing; after failing to beat a Karo As - nothing higher
    CHECK(g.cannotHold(1) & cardBit(C(SINEK, 9)));
    CHECK(!(g.cannotHold(1) & cardBit(C(KUPA, 9))));
    // player 1 took the Sinek trick (14 + 11 + 2 ... ) and leads; play the rest out: player 1 cannot reach 66
    int guard = 0;
    while (g.stage() == Stage::Playing && ++guard < 50) {
        const int p = g.current();
        g.playCard(p, g.legalCards(p).front());
    }
    CHECK(g.stage() != Stage::Playing);
    const HandRecord& h = g.sheet().back();
    CHECK_EQ(h.closer, 1);
    if (h.winner == 0) {
        CHECK_EQ(h.reason, EndReason::CloserFailed);
        CHECK(h.gamePoints >= 2);
        CHECK_EQ(h.gamePoints, 3); // player 0 had no trick when player 1 closed
    }

    // the closer fails while the other one had a trick at the closing: 2
    Game k = freshGame();
    k.debugSetDeal({C(MACA, 9), C(KARO, 9)}, {C(MACA, AS), C(KARO, AS)},
                   stockOf(C(KUPA, 9), {C(SINEK, 9), C(SINEK, VALE), C(SINEK, KIZ)}), 0);
    k.debugSetPoints(0, 30, 2);
    k.debugSetPoints(1, 40, 1);
    CHECK(k.closeStock(0).ok);
    CHECK(k.playCard(0, C(MACA, 9)).ok);
    CHECK(k.playCard(1, C(MACA, AS)).ok);  // 51
    CHECK(k.playCard(1, C(KARO, AS)).ok);
    CHECK(k.playCard(0, C(KARO, 9)).ok);   // 62: hands empty, nobody at 66, closed: the closer failed
    CHECK_EQ(k.stage(), Stage::HandOver);
    CHECK_EQ(k.sheet().back().winner, 1);
    CHECK_EQ(k.sheet().back().reason, EndReason::CloserFailed);
    CHECK_EQ(k.sheet().back().gamePoints, 2);
    CHECK_EQ(k.points(1), 62); // no last-trick bonus in a closed hand
    const std::vector<GameEvent> ev = k.drainEvents();
    const GameEvent* he = findEvent(ev, EvType::HandEnd);
    CHECK(he && he->text.find("Kapattın ama tutturamadın") == 0);

    // the closer makes it: the normal schedule
    Game m = freshGame();
    m.debugSetDeal({C(MACA, AS), C(KARO, AS)}, {C(MACA, 9), C(KARO, 9)},
                   stockOf(C(KUPA, 9), {C(SINEK, 9), C(SINEK, VALE), C(SINEK, KIZ)}), 0);
    m.debugSetPoints(0, 50, 2);
    m.debugSetPoints(1, 40, 1);
    CHECK(m.closeStock(0).ok);
    CHECK(m.playCard(0, C(MACA, AS)).ok);
    CHECK(m.playCard(1, C(MACA, 9)).ok); // 61
    CHECK(m.playCard(0, C(KARO, AS)).ok);
    CHECK(m.playCard(1, C(KARO, 9)).ok); // 72
    CHECK_EQ(m.sheet().back().winner, 0);
    CHECK_EQ(m.sheet().back().reason, EndReason::Reached);
    CHECK_EQ(m.sheet().back().gamePoints, 1);
}

void testGamePoints() {
    // 3: the loser took no trick
    {
        Game g = freshGame();
        g.debugSetDeal({C(MACA, AS), C(MACA, 10)}, {C(MACA, 9), C(MACA, VALE)}, {}, 0);
        g.debugSetPoints(0, 55, 3);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        CHECK(g.playCard(1, C(MACA, 9)).ok);
        CHECK_EQ(g.sheet().back().gamePoints, 3);
        CHECK_EQ(g.total(0), 3);
    }
    // 2: the loser below 33
    {
        Game g = freshGame();
        g.debugSetDeal({C(MACA, AS), C(MACA, 10)}, {C(MACA, 9), C(MACA, VALE)}, {}, 0);
        g.debugSetPoints(0, 55, 3);
        g.debugSetPoints(1, 32, 1);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        CHECK(g.playCard(1, C(MACA, 9)).ok);
        CHECK_EQ(g.sheet().back().gamePoints, 2);
    }
    // 1: the loser has 33
    {
        Game g = freshGame();
        g.debugSetDeal({C(MACA, AS), C(MACA, 10)}, {C(MACA, 9), C(MACA, VALE)}, {}, 0);
        g.debugSetPoints(0, 55, 3);
        g.debugSetPoints(1, 33, 1);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        CHECK(g.playCard(1, C(MACA, 9)).ok);
        CHECK_EQ(g.sheet().back().gamePoints, 1);
        CHECK_EQ(g.sheet().back().reason, EndReason::Reached);
    }
    // the last trick: +10, and nobody at 66 -> its winner takes 1
    {
        Game g = freshGame();
        g.debugSetDeal({C(MACA, AS)}, {C(MACA, 9)}, {}, 0);
        g.debugSetPoints(0, 40, 3);
        g.debugSetPoints(1, 50, 3);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        CHECK(g.playCard(1, C(MACA, 9)).ok);
        CHECK_EQ(g.points(0), 61);
        CHECK_EQ(g.sheet().back().winner, 0);
        CHECK_EQ(g.sheet().back().reason, EndReason::LastTrick);
        CHECK_EQ(g.sheet().back().gamePoints, 1);
    }
    {
        Game g = freshGame();
        g.debugSetDeal({C(MACA, AS)}, {C(MACA, 9)}, {}, 0);
        g.debugSetPoints(0, 50, 3);
        g.debugSetPoints(1, 30, 3);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        CHECK(g.playCard(1, C(MACA, 9)).ok);
        CHECK_EQ(g.points(0), 71);
        CHECK_EQ(g.sheet().back().reason, EndReason::Reached);
        CHECK_EQ(g.sheet().back().gamePoints, 2);
    }
    // the last-trick bonus can be switched off
    {
        Rules r;
        r.lastTrickBonus = false;
        Game g(r);
        g.startMatch(3);
        g.debugSetDeal({C(MACA, AS)}, {C(MACA, 9)}, {}, 0);
        g.debugSetPoints(0, 50, 3);
        g.playCard(0, C(MACA, AS));
        g.playCard(1, C(MACA, 9));
        CHECK_EQ(g.points(0), 61);
    }
}

void testMatchEnd() {
    Game g = freshGame();
    g.debugSetTotals(6, 4);
    g.debugSetDeal({C(MACA, AS)}, {C(MACA, 9)}, {}, 0);
    g.debugSetPoints(0, 60, 3);
    g.debugSetPoints(1, 40, 3);
    g.playCard(0, C(MACA, AS));
    g.playCard(1, C(MACA, 9));
    CHECK_EQ(g.stage(), Stage::MatchOver);
    CHECK_EQ(g.matchWinner(), 0);
    CHECK_EQ(g.total(0), 7);
    const std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* me = findEvent(ev, EvType::MatchEnd);
    CHECK(me && me->seat == 0 && me->text.find("Maçı kazandın") == 0);
    g.startNextHand(); // ignored
    CHECK_EQ(g.stage(), Stage::MatchOver);
}

void testDrawAndStockOut() {
    Game g = freshGame();
    g.debugSetDeal({C(MACA, AS), C(KARO, 9)}, {C(MACA, 9), C(KARO, VALE)}, stockOf(C(KUPA, 9), {C(SINEK, AS)}), 0);
    g.debugSetPoints(0, 20, 1);
    g.debugSetPoints(1, 20, 1);
    CHECK(g.playCard(0, C(MACA, AS)).ok);
    CHECK(g.playCard(1, C(MACA, 9)).ok);
    // the winner took the top card (Sinek As), the other the face-up koz: known to everybody
    CHECK(maskOf(g.hand(0)) & cardBit(C(SINEK, AS)));
    CHECK(maskOf(g.hand(1)) & cardBit(C(KUPA, 9)));
    CHECK(g.shownCards(1) & cardBit(C(KUPA, 9)));
    CHECK(!(g.shownCards(0) & cardBit(C(SINEK, AS))));
    CHECK_EQ(g.stockCount(), 0);
    CHECK(g.strict());
    const std::vector<GameEvent> ev = g.drainEvents();
    CHECK(hasEvent(ev, EvType::StockOut));
    int draws = 0;
    for (const GameEvent& e : ev)
        if (e.type == EvType::Draw) {
            ++draws;
            if (e.seat == 1) CHECK_EQ(e.card, C(KUPA, 9));
            else CHECK_EQ(e.card, -1);
        }
    CHECK_EQ(draws, 2);
    // the view of player 0: the other hand shows only the koz 9
    const Deal v = g.viewOf(0);
    CHECK_EQ(v.hand[1], cardBit(C(KUPA, 9)));
    CHECK_EQ(v.hand[0], g.dealState().hand[0]);
}

void testViewHidesStock() {
    Game g = freshGame(21);
    const Deal v = g.viewOf(0);
    CHECK_EQ(v.hand[1], (CardMask)0);
    CHECK_EQ(v.stock[0], g.trumpCard());
    for (int i = 1; i < v.stockN; ++i) CHECK_EQ(v.stock[(size_t)i], -1);
}

// Plays a match with random legal actions (exchanges and closes now and then); checks invariants on the way.
bool fuzzMatch(uint64_t seed, okey::Rng& rng, std::vector<LoggedAction>* log = nullptr) {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 5000) {
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int p = g.current();
        const Deal& d = g.dealState();
        const CardMask inPlay = d.hand[0] | d.hand[1] | maskOf(g.stockCards()) | d.played;
        CHECK_EQ(inPlay, DECK_MASK);
        CHECK_EQ(popcount(d.hand[0] | d.hand[1]) + d.stockN + popcount(d.played), 24);
        CHECK(d.points[0] + d.points[1] <= 130 + 160);
        if (g.canExchange(p) && rng.chance(0.5f)) {
            CHECK(g.exchangeNine(p).ok);
            continue;
        }
        if (g.canClose(p) && rng.chance(0.08f)) {
            CHECK(g.closeStock(p).ok);
            continue;
        }
        const std::vector<int> l = g.legalCards(p);
        CHECK(!l.empty());
        if (l.empty()) return false;
        // an illegal card is refused and changes nothing
        for (int c : g.hand(p))
            if (std::find(l.begin(), l.end(), c) == l.end()) {
                const size_t before = g.actionLog().size();
                CHECK(!g.playCard(p, c).ok);
                CHECK_EQ(g.actionLog().size(), before);
                break;
            }
        CHECK(g.playCard(p, l[(size_t)rng.range((int)l.size())]).ok);
        g.drainEvents();
    }
    CHECK_EQ(g.stage(), Stage::MatchOver);
    CHECK(g.total(0) >= 7 || g.total(1) >= 7);
    int sum = 0;
    for (const HandRecord& h : g.sheet()) {
        CHECK(h.gamePoints >= 1 && h.gamePoints <= 3);
        sum += h.gamePoints;
    }
    CHECK_EQ(sum, g.total(0) + g.total(1));
    if (log) *log = g.actionLog();
    return true;
}

void testDeterminismAndReplay() {
    Game a = freshGame(77), b = freshGame(77);
    CHECK(a.hand(0) == b.hand(0));
    CHECK(a.stockCards() == b.stockCards());
    Game c = freshGame(78);
    CHECK(!(a.hand(0) == c.hand(0) && a.stockCards() == c.stockCards()));
    okey::Rng rng(5);
    for (int m = 0; m < 30; ++m) {
        std::vector<LoggedAction> log;
        const uint64_t seed = 1000 + (uint64_t)m;
        fuzzMatch(seed, rng, &log);
        // encode / decode / replay gives the same match
        Game r;
        r.setPlayer(0, "Sen", true);
        r.setPlayer(1, "Kel Mahmut", false);
        r.startMatch(seed);
        bool ok = true;
        for (const LoggedAction& la : log) {
            LoggedAction d;
            ok = ok && LoggedAction::decode(la.encode(), d) && d.kind == la.kind && d.player == la.player && d.card == la.card;
            ok = ok && r.replay(d);
        }
        CHECK(ok);
        CHECK_EQ(r.stage(), Stage::MatchOver);
        CHECK_EQ(r.actionLog().size(), log.size());
        Game f;
        f.startMatch(seed);
        for (const LoggedAction& la : log) f.replay(la);
        CHECK_EQ(f.total(0) + f.total(1), r.total(0) + r.total(1));
    }
    LoggedAction x;
    CHECK(!LoggedAction::decode("", x));
    CHECK(!LoggedAction::decode("9 0 1", x));
    CHECK(!LoggedAction::decode("0 5 1", x));
    CHECK(!LoggedAction::decode("0 1", x));
    CHECK(LoggedAction::decode("3 -1 -1", x) && x.kind == LogKind::NextHand);
    // a replayed action that does not apply is refused
    Game g = freshGame(9);
    CHECK(!g.replay({LogKind::NextHand, -1, -1}));
    CHECK(!g.replay({LogKind::Play, 1 - g.current(), g.hand(1 - g.current()).front()}));
}

void testFuzz() {
    okey::Rng rng(99);
    for (int m = 0; m < 300; ++m) fuzzMatch(5000 + (uint64_t)m, rng);
}

void testBots() {
    // every level plays legal moves to the end of a match, against each other
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            Game g;
            g.setPlayer(0, "A", false);
            g.setPlayer(1, "B", false);
            Bot ba((BotLevel)a, 11), bb((BotLevel)b, 12);
            g.startMatch(300 + (uint64_t)(a * 3 + b));
            int guard = 0, rejected = 0;
            while (g.stage() != Stage::MatchOver && ++guard < 5000) {
                if (g.stage() == Stage::HandOver) {
                    g.startNextHand();
                    continue;
                }
                const int p = g.current();
                const BotAction act = (p == 0 ? ba : bb).next(g, p);
                if (!applyBotAction(g, p, act).ok) {
                    ++rejected;
                    applyBotAction(g, p, fallbackAction(g, p));
                }
            }
            CHECK_EQ(rejected, 0);
            CHECK_EQ(g.stage(), Stage::MatchOver);
        }
    // Kurt plays a known endgame exactly: stock used up, player 0 to lead with the koz As and a side 10; player 1
    // holds the koz 10 and the side As. Leading the koz As draws the 10 (forced to follow), then the side 10 loses
    // to the As... the best line is found by the search: check that Kurt's choice is worth at least the others.
    {
        Game g = freshGame();
        g.debugSetDeal({C(KUPA, AS), C(MACA, 10)}, {C(KUPA, 10), C(MACA, AS)}, {}, 0);
        g.debugSetPoints(0, 50, 2);
        g.debugSetPoints(1, 40, 2);
        Bot k(BotLevel::Kurt, 3);
        const std::vector<ActionValue> v = k.evaluate(g, 0);
        CHECK_EQ((int)v.size(), 2);
        double best = -10;
        int bestCard = -1;
        for (const ActionValue& a : v)
            if (a.value > best) {
                best = a.value;
                bestCard = a.action.card;
            }
        // Kupa As first: 50 + 21 = 71 -> wins at once (player 1 has 40 >= 33: 1 point)
        CHECK_EQ(bestCard, C(KUPA, AS));
        CHECK(best > 0.9);
        const BotAction a = k.next(g, 0);
        CHECK(a.kind == BotAction::Kind::Play && a.card == C(KUPA, AS));
    }
    // Kurt exchanges the koz 9 when it may
    {
        Game g = freshGame();
        g.debugSetDeal({C(KUPA, 9), C(MACA, AS), C(KARO, 9), C(SINEK, 9), C(MACA, VALE), C(KARO, VALE)},
                       {C(MACA, KIZ), C(MACA, PAPAZ), C(KUPA, 10), C(KUPA, VALE), C(KARO, 10), C(SINEK, 10)},
                       stockOf(C(KUPA, AS), {C(MACA, 9), C(KARO, KIZ), C(KARO, PAPAZ), C(SINEK, KIZ), C(SINEK, PAPAZ)}), 0);
        g.debugSetPoints(0, 11, 1);
        Bot k(BotLevel::Kurt, 4);
        CHECK(k.next(g, 0).kind == BotAction::Kind::Exchange);
        Bot u(BotLevel::Usta, 4);
        CHECK(u.next(g, 0).kind == BotAction::Kind::Exchange);
    }
    // Kurt's decision time on fresh deals (the slowest: the first leads with the full stock)
    {
        double worst = 0;
        for (int s = 0; s < 12; ++s) {
            Game g = freshGame(400 + (uint64_t)s);
            Bot k(BotLevel::Kurt, (uint64_t)s);
            const auto t0 = std::chrono::steady_clock::now();
            k.next(g, g.current());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms);
        }
        std::printf("  Kurt first-lead decision: worst %.1f ms\n", worst);
#if defined(__has_feature)
#if !__has_feature(address_sanitizer)
        CHECK(worst < 300.0);
#endif
#elif !defined(__SANITIZE_ADDRESS__)
        CHECK(worst < 300.0);
#endif
    }
    // evaluate() is deterministic and does not disturb next()
    {
        Game g = freshGame(31);
        Bot k(BotLevel::Kurt, 8);
        const std::vector<ActionValue> a = k.evaluate(g, g.current()), b = k.evaluate(g, g.current());
        CHECK_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].value == b[i].value);
    }
}

} // namespace

int main() {
    testCardsAndDeal();
    testOpenStockPlay();
    testMarriage();
    testExchange();
    testCloseAndStrict();
    testGamePoints();
    testMatchEnd();
    testDrawAndStockOut();
    testViewHidesStock();
    testDeterminismAndReplay();
    testFuzz();
    testBots();
    std::printf("test_altmisalti: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
