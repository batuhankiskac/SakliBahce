// Bezik engine + bot tests: the deck, the deal, tricks, every combination and its reuse rules, the koz 7, the second
// stage's follow / win rules, the deal's points, the match end, determinism, the action log and its replay, the bots
// (legal, Kurt's speed and the exact second stage) and a random fuzzer.
#include "core/Bezik.h"
#include "core/BezikBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace bezik;

namespace {

int g_checks = 0;
int g_failures = 0;

void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    if (g_failures <= 60) std::printf("%s:%d: CHECK failed: %s\n", file, line, what.c_str());
}

int C(int suit, int rank, int copy = 0) { return kart::makeCard(suit, rank) + copy * kart::NUM_CARDS; }
using kart::As;
using kart::Karo;
using kart::Kiz;
using kart::Kupa;
using kart::Maca;
using kart::Papaz;
using kart::Sinek;
using kart::Vale;

bool hasMeld(const std::vector<Meld>& ms, MeldKind k) {
    for (const Meld& m : ms)
        if (m.kind == k) return true;
    return false;
}
Meld meldOf(const std::vector<Meld>& ms, MeldKind k) {
    for (const Meld& m : ms)
        if (m.kind == k) return m;
    return Meld{};
}

// Every card is somewhere exactly once.
bool conserved(const Game& g) {
    std::vector<int> all;
    for (int s = 0; s < 2; ++s) {
        all.insert(all.end(), g.hand(s).begin(), g.hand(s).end());
        all.insert(all.end(), g.tableCards(s).begin(), g.tableCards(s).end());
        all.insert(all.end(), g.won(s).begin(), g.won(s).end());
    }
    for (const TrickCard& t : g.currentTrick().cards) all.push_back(t.card);
    if (g.turnUp() >= 0) all.push_back(g.turnUp());
    // the stock is not public: count it
    const size_t n = all.size() + (size_t)g.stockSize();
    std::sort(all.begin(), all.end());
    return n == 64 && std::adjacent_find(all.begin(), all.end()) == all.end();
}

// A deal set up by hand: seat 0 leads, filler stock of the cards not given.
Game setup(const std::vector<int>& h0, const std::vector<int>& h1, int turnUp, int stockCards = -1) {
    Game g;
    g.startMatch(1);
    g.drainEvents();
    std::set<int> used(h0.begin(), h0.end());
    used.insert(h1.begin(), h1.end());
    used.insert(turnUp);
    std::vector<int> stock;
    for (int c : fullDeck())
        if (!used.count(c)) stock.push_back(c);
    std::vector<int> rest;
    if (stockCards >= 0) {
        rest.assign(stock.begin() + stockCards, stock.end()); // (as if taken in earlier tricks)
        stock.resize((size_t)stockCards);
    }
    g.debugSetDeal({h0, h1}, stock, turnUp);
    g.debugSetWon(1, rest);
    g.debugSetCurrent(0);
    return g;
}

} // namespace

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) fail(__FILE__, __LINE__, #cond);                                                            \
    } while (0)
#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        const auto va_ = (a);                                                                                    \
        const auto vb_ = (b);                                                                                    \
        if (!(va_ == vb_)) fail(__FILE__, __LINE__, std::string(#a " == " #b ": ") + std::to_string(va_) + " vs " + std::to_string(vb_)); \
    } while (0)

static void testDeck() {
    const std::vector<int> d = fullDeck();
    CHECK_EQ((int)d.size(), 64);
    std::set<int> s(d.begin(), d.end());
    CHECK_EQ((int)s.size(), 64);
    std::set<int> idx;
    for (int c : d) {
        CHECK(isCard(c));
        CHECK(rankOf(c) >= 7);
        CHECK_EQ(idOf(idxOf(c)), c);
        idx.insert(idxOf(c));
        CHECK_EQ(twinOf(twinOf(c)), c);
        CHECK(sameFace(c, twinOf(c)));
    }
    CHECK_EQ((int)idx.size(), 64);
    CHECK(!isCard(C(Maca, 6)));
    CHECK(!isCard(C(Maca, 2, 1)));
    // A > 10 > P > K > V > 9 > 8 > 7
    const int order[8] = {As, 10, Papaz, Kiz, Vale, 9, 8, 7};
    for (int i = 0; i + 1 < 8; ++i) CHECK(strength(C(Kupa, order[i])) > strength(C(Kupa, order[i + 1])));
    CHECK(beats(C(Kupa, 10), C(Kupa, Papaz), Maca));
    CHECK(!beats(C(Kupa, Papaz), C(Kupa, 10), Maca));
    CHECK(!beats(C(Kupa, As, 1), C(Kupa, As), Maca));   // the same card: the first wins
    CHECK(beats(C(Maca, 7), C(Kupa, As), Maca));        // a trump
    CHECK(!beats(C(Sinek, As), C(Kupa, 7), Maca));      // another suit, not trump
    CHECK(cardName(C(Maca, Kiz, 1)) == std::string("Maça Kız"));
    LoggedAction la;
    CHECK(LoggedAction::decode("1 0 0 10 63", la));
    CHECK_EQ((int)la.cards.size(), 2);
}

static void testDeal() {
    Game g;
    g.startMatch(42);
    CHECK(g.stage() == Stage::Playing);
    CHECK_EQ((int)g.hand(0).size(), 8);
    CHECK_EQ((int)g.hand(1).size(), 8);
    CHECK_EQ(g.stockSize(), 47);
    CHECK(g.turnUp() >= 0);
    CHECK_EQ(g.trump(), suitOf(g.turnUp()));
    CHECK_EQ(g.current(), 1 - g.dealer());
    CHECK(conserved(g));
    // a 7 turned up: the dealer writes 10
    bool found = false;
    for (uint64_t seed = 1; seed < 400 && !found; ++seed) {
        Game h;
        h.startMatch(seed);
        if (rankOf(h.turnUp()) == 7) {
            found = true;
            CHECK_EQ(h.meldPoints(h.dealer()), 10);
            CHECK_EQ(h.meldPoints(1 - h.dealer()), 0);
            CHECK(!h.kozSevenSwaps()); // the next koz 7 is only shown
        }
    }
    CHECK(found);
}

static void testTricksAndDraw() {
    Game g = setup({C(Kupa, 7), C(Kupa, 8), C(Sinek, 9), C(Karo, 9), C(Sinek, 7), C(Karo, 7), C(Kupa, 9), C(Sinek, 8)},
                   {C(Kupa, As), C(Maca, 8), C(Sinek, 10), C(Karo, 10), C(Sinek, 10, 1), C(Karo, 8), C(Kupa, 9, 1), C(Karo, 8, 1)},
                   C(Maca, 9));
    CHECK_EQ(g.trump(), (int)Maca);
    // stage 1: no need to follow suit
    CHECK(g.playCard(1, C(Kupa, 7)).ok == false); // not their turn
    CHECK(g.playCard(0, C(Kupa, 7)).ok);
    CHECK_EQ((int)g.legalCards(1).size(), 8);
    CHECK(g.playCard(1, C(Maca, 8)).ok);          // a small trump wins
    CHECK_EQ(g.tricks().back().winner, 1);
    CHECK(g.stage() == Stage::Playing);           // nothing to declare: the draws happen at once
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.hand(0).size(), 8);
    CHECK_EQ((int)g.hand(1).size(), 8);
    CHECK(conserved(g));
    // the leader's card wins when the reply neither follows higher nor trumps
    CHECK(g.playCard(1, C(Kupa, As)).ok);
    CHECK(g.playCard(0, C(Kupa, 9)).ok);
    CHECK_EQ(g.tricks().back().winner, 1);
}

static void testMelds() {
    // bezik, then the double bezik (500) with the declared pair on the table
    {
        Game g = setup({C(Maca, Kiz), C(Karo, Vale), C(Maca, Kiz, 1), C(Karo, Vale, 1), C(Kupa, As), C(Sinek, 7), C(Kupa, 8), C(Sinek, 8)},
                       {C(Kupa, 7), C(Sinek, 9), C(Kupa, 9), C(Sinek, 9, 1), C(Kupa, 7, 1), C(Karo, 8), C(Karo, 7), C(Karo, 9)},
                       C(Sinek, 10));
        CHECK(g.playCard(0, C(Kupa, As)).ok);
        CHECK(g.playCard(1, C(Kupa, 7)).ok);
        CHECK(g.stage() == Stage::Declare);
        CHECK_EQ(g.current(), 0);
        std::vector<Meld> ms = g.availableMelds(0);
        CHECK(hasMeld(ms, MeldKind::Bezik));
        CHECK(hasMeld(ms, MeldKind::CiftBezik));
        CHECK(ms[0].kind == MeldKind::CiftBezik); // the biggest first
        CHECK(g.declare(1, meldOf(ms, MeldKind::Bezik)).ok == false); // only the trick's winner
        CHECK(g.declare(0, meldOf(ms, MeldKind::Bezik)).ok);
        CHECK_EQ(g.meldPoints(0), 40);
        CHECK_EQ((int)g.tableCards(0).size(), 2);
        CHECK(g.stage() == Stage::Playing);          // one declaration per trick: the draws followed
        CHECK_EQ((int)g.hand(0).size() + (int)g.tableCards(0).size(), 8);
        CHECK(conserved(g));
        // win another trick: double bezik from the two on the table + the two in hand
        CHECK(g.playCard(0, C(Sinek, 8)).ok);
        CHECK(g.playCard(1, C(Sinek, 9)).ok);       // 1 wins
        CHECK_EQ(g.tricks().back().winner, 1);
        // let 0 win: 1 leads small, 0 beats
        int lead = -1;
        for (int c : g.hand(1))
            if (suitOf(c) == Karo && rankOf(c) == 7) lead = c;
        if (lead < 0) lead = g.hand(1)[0];
        CHECK(g.playCard(1, lead).ok);
        int win = -1;
        for (int c : g.playable(0))
            if (beats(c, lead, g.trump()) && faceOf(c) != BEZIK_QUEEN && faceOf(c) != BEZIK_JACK) win = c;
        if (win < 0) // a bezik card wins then (Karo Vale beats Karo 7)
            for (int c : g.playable(0))
                if (beats(c, lead, g.trump())) win = c;
        CHECK(win >= 0);
        if (win >= 0 && faceOf(win) != BEZIK_QUEEN && faceOf(win) != BEZIK_JACK) {
            CHECK(g.playCard(0, win).ok);
            CHECK(g.stage() == Stage::Declare);
            ms = g.availableMelds(0);
            CHECK(hasMeld(ms, MeldKind::CiftBezik));
            CHECK(!hasMeld(ms, MeldKind::Bezik) || meldOf(ms, MeldKind::Bezik).cards.size() == 2);
            // a second single bezik must use two new cards: the new pair is in hand
            const Meld cb = meldOf(ms, MeldKind::CiftBezik);
            CHECK(g.declare(0, cb).ok);
            CHECK_EQ(g.meldPoints(0), 540);
        }
    }
    // koz evliliği, then the koz serisi with the same Papaz and Kız (40 + 250); the other way round is not allowed
    {
        Game g = setup({C(Kupa, Papaz), C(Kupa, Kiz), C(Kupa, As), C(Kupa, 10), C(Kupa, Vale), C(Sinek, As), C(Sinek, 7), C(Karo, 7)},
                       {C(Sinek, 8), C(Sinek, 9), C(Karo, 8), C(Karo, 9), C(Maca, 7), C(Maca, 8), C(Maca, 9), C(Karo, 8, 1)},
                       C(Kupa, 8));
        CHECK(g.playCard(0, C(Sinek, As)).ok);
        CHECK(g.playCard(1, C(Sinek, 8)).ok);
        std::vector<Meld> ms = g.availableMelds(0);
        CHECK(hasMeld(ms, MeldKind::Seri));
        CHECK(hasMeld(ms, MeldKind::KozEvlilik));
        CHECK(g.declare(0, meldOf(ms, MeldKind::KozEvlilik)).ok);
        CHECK_EQ(g.meldPoints(0), 40);
        // next trick: 0 leads and wins with the Sinek... lead a card 1 cannot beat
        CHECK(g.playCard(0, C(Sinek, 7)).ok == true);
        int reply = g.hand(1)[0];
        for (int c : g.hand(1))
            if (!beats(c, C(Sinek, 7), g.trump()) && suitOf(c) != Sinek) reply = c;
        CHECK(g.playCard(1, reply).ok);
        if (g.tricks().back().winner == 0) {
            CHECK(g.stage() == Stage::Declare);
            ms = g.availableMelds(0);
            CHECK(hasMeld(ms, MeldKind::Seri));
            CHECK(!hasMeld(ms, MeldKind::KozEvlilik)); // the pair already married
            CHECK(g.declare(0, meldOf(ms, MeldKind::Seri)).ok);
            CHECK_EQ(g.meldPoints(0), 290);
        }
        Game h = setup({C(Kupa, Papaz), C(Kupa, Kiz), C(Kupa, As), C(Kupa, 10), C(Kupa, Vale), C(Sinek, As), C(Sinek, 7), C(Karo, 7)},
                       {C(Sinek, 8), C(Sinek, 9), C(Karo, 8), C(Karo, 9), C(Maca, 7), C(Maca, 8), C(Maca, 9), C(Karo, 8, 1)},
                       C(Kupa, 8));
        CHECK(h.playCard(0, C(Sinek, As)).ok);
        CHECK(h.playCard(1, C(Sinek, 8)).ok);
        CHECK(h.declare(0, meldOf(h.availableMelds(0), MeldKind::Seri)).ok);
        CHECK_EQ(h.meldPoints(0), 250);
        Meld km;
        km.kind = MeldKind::KozEvlilik;
        km.cards = {C(Kupa, Papaz), C(Kupa, Kiz)};
        std::string why;
        CHECK(!h.meldValid(0, km, &why)); // the sequence's pair cannot marry afterwards
    }
    // four aces (any suits), a meld needs a new card from the hand, marriages, four of a kind of the same rank twice
    {
        Game g = setup({C(Kupa, As), C(Kupa, As, 1), C(Sinek, As), C(Karo, As), C(Karo, Papaz), C(Karo, Kiz), C(Sinek, 7), C(Kupa, 7)},
                       {C(Sinek, 8), C(Sinek, 9), C(Karo, 8), C(Karo, 9), C(Kupa, 8), C(Kupa, 9), C(Kupa, 9, 1), C(Karo, 8, 1)},
                       C(Maca, 9));
        CHECK(g.playCard(0, C(Sinek, 7)).ok);
        CHECK(g.playCard(1, C(Kupa, 8)).ok);       // 0 wins (no trump, other suit)
        CHECK(g.stage() == Stage::Declare);
        std::vector<Meld> ms = g.availableMelds(0);
        CHECK(hasMeld(ms, MeldKind::DortAs));
        CHECK(hasMeld(ms, MeldKind::Evlilik));
        CHECK_EQ(meldOf(ms, MeldKind::DortAs).points, 100);
        CHECK_EQ(meldOf(ms, MeldKind::Evlilik).points, 20);
        Meld bad;
        bad.kind = MeldKind::DortAs;
        bad.cards = {C(Kupa, As), C(Kupa, As, 1), C(Sinek, As), C(Kupa, 7)};
        CHECK(!g.meldValid(0, bad));
        bad.kind = MeldKind::Evlilik;
        bad.cards = {C(Karo, Papaz), C(Kupa, Kiz)};
        CHECK(!g.meldValid(0, bad));
        CHECK(g.declare(0, meldOf(ms, MeldKind::DortAs)).ok);
        CHECK_EQ(g.meldPoints(0), 100);
        CHECK_EQ((int)g.tableCards(0).size(), 4);
        // the aces lie on the table and can be played from there
        CHECK_EQ((int)g.playable(0).size(), 8);
        // all from the table: not allowed (none new)
        Meld again = ms[0];
        CHECK(!g.meldValid(0, again));
    }
}

static void testKoz7() {
    Game g = setup({C(Kupa, 7), C(Kupa, 7, 1), C(Sinek, As), C(Sinek, 8), C(Karo, 8), C(Karo, 9), C(Sinek, 9), C(Karo, 7)},
                   {C(Sinek, 7), C(Maca, 9), C(Karo, 8, 1), C(Karo, 9, 1), C(Maca, 7), C(Maca, 8), C(Maca, 8, 1), C(Sinek, 7, 1)},
                   C(Kupa, Papaz));
    CHECK(g.playCard(0, C(Sinek, As)).ok);
    CHECK(g.playCard(1, C(Sinek, 7)).ok);
    CHECK(g.stage() == Stage::Declare);
    CHECK(g.canKoz7(0));
    CHECK(g.kozSevenSwaps());
    CHECK(g.koz7(0).ok);
    CHECK_EQ(g.meldPoints(0), 10);
    CHECK_EQ(g.turnUp(), C(Kupa, 7));
    CHECK(std::find(g.hand(0).begin(), g.hand(0).end(), C(Kupa, Papaz)) != g.hand(0).end());
    CHECK(std::find(g.exposed(0).begin(), g.exposed(0).end(), C(Kupa, Papaz)) != g.exposed(0).end());
    // the second 7: shown (the turn-up is a 7 now), still in the same window
    CHECK(g.stage() == Stage::Declare);
    CHECK(g.canKoz7(0));
    CHECK(!g.kozSevenSwaps());
    CHECK(g.koz7(0).ok);
    CHECK_EQ(g.meldPoints(0), 20);
    CHECK(g.stage() == Stage::Playing); // nothing left to declare: the draws happened
    CHECK(conserved(g));
}

static void testSecondStage() {
    // a deal at the end of the stock: 1 face-down card + the turn-up
    Game g = setup({C(Kupa, As), C(Kupa, 10), C(Sinek, 7), C(Sinek, 8), C(Karo, 7), C(Karo, 8), C(Maca, 7), C(Kupa, 7)},
                   {C(Kupa, Papaz), C(Kupa, 8), C(Sinek, As), C(Sinek, 9), C(Karo, As), C(Karo, 9), C(Maca, 8), C(Maca, 9)},
                   C(Maca, 10), 1);
    CHECK(g.playCard(0, C(Sinek, 7)).ok);
    CHECK(g.playCard(1, C(Kupa, 8)).ok);      // 0 wins
    CHECK(g.stage() == Stage::Declare);       // (it holds the koz 7)
    CHECK(g.pass(0).ok);
    CHECK(g.secondStage());
    CHECK_EQ(g.stockSize(), 0);
    CHECK_EQ(g.turnUp(), -1);
    CHECK(std::find(g.hand(1).begin(), g.hand(1).end(), C(Maca, 10)) != g.hand(1).end()); // the loser takes the turn-up
    CHECK_EQ((int)g.hand(0).size(), 8);
    CHECK_EQ(g.current(), 0);
    // follow suit and win if possible
    CHECK(g.playCard(0, C(Kupa, 7)).ok);
    std::vector<int> l = g.legalCards(1);
    CHECK_EQ((int)l.size(), 1);
    CHECK_EQ(l[0], C(Kupa, Papaz));         // the only kupa that wins
    CHECK(g.playCard(1, C(Sinek, As)).ok == false);
    CHECK(g.playCard(1, C(Kupa, Papaz)).ok);
    // 1 leads: Sinek As; 0 has no higher sinek: any sinek
    CHECK(g.playCard(1, C(Sinek, As)).ok);
    l = g.legalCards(0);
    CHECK_EQ((int)l.size(), 1);              // its one sinek (it does not win)
    CHECK(g.playCard(0, C(Sinek, 8)).ok);
    // 1 leads a karo; 0 must beat: none higher (7, 8 vs Karo As), plays a karo
    CHECK(g.playCard(1, C(Karo, 9)).ok);
    l = g.legalCards(0);
    for (int c : l) CHECK(suitOf(c) == Karo);
    // void: must trump
    Game h = setup({C(Maca, 7), C(Kupa, 10), C(Kupa, 7), C(Sinek, 8), C(Kupa, 8), C(Kupa, 9), C(Maca, 8), C(Kupa, Vale)},
                   {C(Sinek, As), C(Sinek, 9), C(Karo, As), C(Karo, 9), C(Karo, 10), C(Karo, Kiz), C(Sinek, 10), C(Sinek, Vale)},
                   C(Maca, 10), 1);
    CHECK(h.playCard(0, C(Kupa, 7)).ok);
    CHECK(h.playCard(1, C(Sinek, 9)).ok);
    if (h.stage() == Stage::Declare) CHECK(h.pass(0).ok);
    CHECK(h.secondStage());
    CHECK(h.playCard(0, C(Kupa, 8)).ok);
    l = h.legalCards(1);
    CHECK_EQ((int)l.size(), 1);
    CHECK_EQ(l[0], C(Maca, 10)); // the turn-up it drew is its only trump
}

// Plays a whole match with bots; returns the game.
static Game playMatch(uint64_t seed, BotLevel a, BotLevel b, int target = 1000, int* rejects = nullptr) {
    Rules r;
    r.target = target;
    Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    Bot bots[2] = {Bot(a, seed * 3 + 1), Bot(b, seed * 3 + 2)};
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 20000) {
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int s = g.current();
        const BotAction act = bots[s].next(g, s);
        if (!applyBotAction(g, s, act).ok) {
            if (rejects) ++*rejects;
            applyBotAction(g, s, fallbackAction(g, s));
        }
        if (!conserved(g)) {
            std::printf("not conserved: stage %d trick %d stock %d up %d h %zu %zu t %zu %zu w %zu %zu\n", (int)g.stage(), g.trickNumber(), g.stockSize(), g.turnUp(), g.hand(0).size(), g.hand(1).size(), g.tableCards(0).size(), g.tableCards(1).size(), g.won(0).size(), g.won(1).size());
            CHECK(false);
            break;
        }
    }
    return g;
}

static void testWholeDealAndMatch() {
    int rej = 0;
    Game g = playMatch(5, BotLevel::Usta, BotLevel::Usta, 1000, &rej);
    CHECK_EQ(rej, 0);
    CHECK(g.stage() == Stage::MatchOver);
    CHECK(std::max(g.total(0), g.total(1)) >= 1000);
    CHECK(g.total(0) != g.total(1));
    int sum0 = 0, sum1 = 0;
    for (const HandRecord& h : g.sheet()) {
        // every deal: 16 brisks (160) + the last trick (10) between the two
        CHECK_EQ(h.brisques[0] + h.brisques[1], 160);
        CHECK_EQ(h.last[0] + h.last[1], 10);
        CHECK_EQ(h.points[0], h.melds[0] + h.brisques[0] + h.last[0]);
        sum0 += h.points[0];
        sum1 += h.points[1];
        CHECK_EQ(h.totals[0], sum0);
        CHECK_EQ(h.totals[1], sum1);
    }
    CHECK_EQ(g.winner(), g.total(0) > g.total(1) ? 0 : 1);
}

static void testDeterminismAndReplay() {
    Game a = playMatch(9, BotLevel::Kurt, BotLevel::Usta, 500);
    Game b = playMatch(9, BotLevel::Kurt, BotLevel::Usta, 500);
    CHECK_EQ(a.total(0), b.total(0));
    CHECK_EQ(a.total(1), b.total(1));
    CHECK_EQ(a.actionLog().size(), b.actionLog().size());
    // replay the log (through its text form) into a fresh game
    Rules r;
    r.target = 500;
    Game c(r);
    c.startMatch(a.matchSeed());
    bool ok = true;
    for (const LoggedAction& x : a.actionLog()) {
        LoggedAction y;
        ok = ok && LoggedAction::decode(x.encode(), y) && y.encode() == x.encode() && c.replay(y);
    }
    CHECK(ok);
    CHECK(c.stage() == Stage::MatchOver);
    CHECK_EQ(c.total(0), a.total(0));
    CHECK_EQ(c.total(1), a.total(1));
    CHECK_EQ(c.actionLog().size(), a.actionLog().size());
    LoggedAction z;
    CHECK(!LoggedAction::decode("x 1 2", z));
    CHECK(!LoggedAction::decode("0 1 5 6", z)); // a play has no cards list
    CHECK(!LoggedAction::decode("9 0 0", z));
}

static void testBots() {
    // Kurt's time per decision, legal actions at every level
    Game g;
    g.startMatch(77);
    Bot kurt(BotLevel::Kurt, 3), usta(BotLevel::Usta, 4);
    double worst = 0.0;
    int decisions = 0;
    for (int guard = 0; guard < 400 && g.stage() != Stage::HandOver && g.stage() != Stage::MatchOver; ++guard) {
        const int s = g.current();
        // (CPU time: the machine may be busy with other work; the bot itself is single-threaded)
        const std::clock_t t0 = std::clock();
        const BotAction a = s == 0 ? kurt.next(g, s) : usta.next(g, s);
        const double dt = (double)(std::clock() - t0) / CLOCKS_PER_SEC;
        if (s == 0) {
            worst = std::max(worst, dt);
            ++decisions;
        }
        CHECK(applyBotAction(g, s, a).ok);
    }
    std::printf("Kurt: %d decisions, slowest %.3f s\n", decisions, worst);
#ifndef __SANITIZE_ADDRESS__
    CHECK(worst < 0.3);
#endif
    // the exact second stage agrees with itself: the value of the best card is the max
    Game h = setup({C(Kupa, As), C(Kupa, 10), C(Sinek, 7), C(Sinek, 8), C(Karo, 7), C(Karo, 8), C(Maca, 7), C(Kupa, 7)},
                   {C(Kupa, Papaz), C(Kupa, 8), C(Sinek, As), C(Sinek, 9), C(Karo, As), C(Karo, 9), C(Maca, 8), C(Maca, 9)},
                   C(Maca, 10), 1);
    CHECK(h.playCard(0, C(Sinek, 7)).ok);
    CHECK(h.playCard(1, C(Kupa, 8)).ok);
    CHECK(h.pass(0).ok);
    const std::vector<CardValue> v = kurt.evaluateCards(h, 0);
    CHECK(!v.empty());
    // play it out with the exact values for both: the predicted value is what happens
    double best = -1e9;
    for (const CardValue& cv : v) best = std::max(best, cv.value);
    const int before = h.brisquePoints(0);
    (void)before;
    Game k = h;
    int guard = 0;
    while (k.stage() == Stage::Playing && ++guard < 40) {
        const int s = k.current();
        std::vector<CardValue> vv = kurt.evaluateCards(k, s);
        size_t bi = 0;
        for (size_t i = 1; i < vv.size(); ++i)
            if (vv[i].value > vv[bi].value) bi = i;
        CHECK(k.playCard(s, vv[bi].card).ok);
    }
    // brisks taken in the second stage + the last trick, from seat 0's side
    int got0 = 0, got1 = 0;
    for (const Trick& t : k.tricks())
        if (t.second)
            for (const TrickCard& tc : t.cards) (t.winner == 0 ? got0 : got1) += isBrisque(tc.card) ? 10 : 0;
    const HandRecord& hr = k.sheet().back();
    got0 += hr.last[0];
    got1 += hr.last[1];
    CHECK_EQ((int)best, got0 - got1);
    // the bold / careful styles still play legal cards
    Bot bold(BotLevel::Kurt, 5);
    bold.setStyle(BotStyle::bold());
    Game m;
    m.startMatch(8);
    for (int i = 0; i < 30 && m.stage() == Stage::Playing; ++i) {
        const int s = m.current();
        CHECK(applyBotAction(m, s, bold.next(m, s)).ok);
        if (m.stage() == Stage::Declare) CHECK(applyBotAction(m, m.current(), bold.next(m, m.current())).ok);
    }
    CHECK(keepValue(m, 0, m.playable(0)[0]) >= 0.0);
}

static void testFuzz() {
    okey::Rng rng(99);
    for (int match = 0; match < 40; ++match) {
        Rules r;
        r.target = 400;
        Game g(r);
        g.startMatch(1000 + (uint64_t)match);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && ++guard < 6000) {
            g.drainEvents();
            if (g.stage() == Stage::HandOver) {
                g.startNextHand();
                continue;
            }
            const int s = g.current();
            // a bad action first: must fail and change nothing
            const int before = g.handCount(s);
            CHECK(!g.playCard(1 - s, g.playable(1 - s).empty() ? 0 : g.playable(1 - s)[0]).ok);
            CHECK_EQ(g.handCount(s), before);
            if (g.stage() == Stage::Declare) {
                const std::vector<Meld> ms = g.availableMelds(s);
                for (const Meld& m : ms) CHECK(g.meldValid(s, m));
                const int pick = rng.range(4);
                if (pick == 0 && g.canKoz7(s)) CHECK(g.koz7(s).ok);
                else if (pick <= 2 && !ms.empty()) CHECK(g.declare(s, ms[(size_t)rng.range((int)ms.size())]).ok);
                else CHECK(g.pass(s).ok);
                CHECK(conserved(g));
                continue;
            }
            const std::vector<int> l = g.legalCards(s);
            CHECK(!l.empty());
            if (l.empty()) break;
            // every card not legal is refused
            for (int c : g.playable(s))
                if (std::find(l.begin(), l.end(), c) == l.end()) CHECK(!g.isLegal(s, c));
            CHECK(g.playCard(s, l[(size_t)rng.range((int)l.size())]).ok);
            CHECK(conserved(g));
        }
        CHECK(g.stage() == Stage::MatchOver);
        // its log replays
        Game c(r);
        c.startMatch(g.matchSeed());
        bool ok = true;
        for (const LoggedAction& a : g.actionLog()) ok = ok && c.replay(a);
        CHECK(ok);
        CHECK_EQ(c.total(0), g.total(0));
    }
}

static void testTexts() {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(3);
    const std::vector<GameEvent> ev = g.drainEvents();
    bool deal = false;
    for (const GameEvent& e : ev)
        if (e.type == EvType::Deal) deal = e.text.find("Koz ") == 0;
    CHECK(deal);
    const int s = g.current();
    CHECK(g.playCard(s, g.legalCards(s)[0]).ok);
    bool said = false;
    for (const GameEvent& e : g.drainEvents())
        if (e.type == EvType::Play) said = s == 0 ? e.text.find("attın") != std::string::npos : e.text.find("Kel Mahmut") == 0;
    CHECK(said);
    CHECK(std::string(meldNameTR(MeldKind::KozEvlilik)) == "Koz evliliği");
}

int main() {
    testDeck();
    testDeal();
    testTricksAndDraw();
    testMelds();
    testKoz7();
    testSecondStage();
    testWholeDealAndMatch();
    testDeterminismAndReplay();
    testBots();
    testFuzz();
    testTexts();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
