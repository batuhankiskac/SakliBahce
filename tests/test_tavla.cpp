// Tavla engine tests for SaklıBahçe: rules with hand-built positions, events, undo, determinism, a brute-force
// cross-check of the move generator against an independent reference implementation, a random-game fuzzer
// and bot sanity checks.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Tavla.cpp src/core/TavlaBot.cpp tests/test_tavla.cpp
//        -o build/tavla/test_tavla
#include "core/Tavla.h"
#include "core/TavlaBot.h"
#include "core/TurkishText.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace tavla;

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

using StepKey = std::tuple<int, int, int>; // from, to, die

// ---- position building ----

// Empty board; place(p, idx, n) adds n checkers of p. Remaining checkers of each side are put "off" by fill().
struct PB {
    Position pos;
    PB() { pos = Position(); }
    PB& at(int p, int idx, int n) {
        pos.pts[idx] = (int8_t)(pos.pts[idx] + (p == 0 ? n : -n));
        return *this;
    }
    PB& bar(int p, int n) {
        pos.bar[p] = (int8_t)n;
        return *this;
    }
    // remaining checkers of each side are borne off
    Position fill() {
        for (int p = 0; p < 2; ++p) pos.off[p] = (int8_t)(kCheckers - pos.onBoard(p) - pos.bar[p]);
        return pos;
    }
};

int total(const Position& pos, int p) { return pos.onBoard(p) + pos.bar[p] + pos.off[p]; }

// Game set up at Moving for player p with the given position and dice (names: 0 = "Sen" human, 1 = bot).
Game moving(const Position& pos, int p, int d1, int d2, Rules r = Rules()) {
    Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(7);
    g.drainEvents();
    g.debugSetPosition(pos);
    g.debugSetTurn(p, Stage::Moving);
    g.debugSetDice(d1, d2);
    return g;
}

std::set<StepKey> keys(const std::vector<Step>& v) {
    std::set<StepKey> s;
    for (const Step& x : v) s.insert({x.from, x.to, x.die});
    return s;
}

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

// ---------------------------------------------------------------------------------------------------------
// Independent reference implementation of the move rules (written separately from the engine, in absolute
// coordinates with explicit direction), used to cross-check legalSteps / generatePlays.

namespace ref {

int own(const Position& pos, int p, int i) { return p == 0 ? std::max(0, (int)pos.pts[i]) : std::max(0, -(int)pos.pts[i]); }

bool allHome(const Position& pos, int p) {
    if (pos.bar[p]) return false;
    for (int i = 0; i < 24; ++i) {
        if (!own(pos, p, i)) continue;
        if (p == 0 && i > 5) return false;
        if (p == 1 && i < 18) return false;
    }
    return true;
}

std::vector<Step> singles(const Position& pos, int p, int d) {
    std::vector<Step> out;
    const int opp = 1 - p;
    if (pos.bar[p] > 0) {
        const int t = p == 0 ? 24 - d : d - 1;
        if (own(pos, opp, t) < 2) out.push_back(Step{BAR, t, d, own(pos, opp, t) == 1});
        return out;
    }
    for (int i = 0; i < 24; ++i) {
        if (!own(pos, p, i)) continue;
        const int t = p == 0 ? i - d : i + d;
        if (t >= 0 && t < 24) {
            if (own(pos, opp, t) < 2) out.push_back(Step{i, t, d, own(pos, opp, t) == 1});
            continue;
        }
        if (!allHome(pos, p)) continue;
        const bool exact = p == 0 ? (i - d == -1) : (i + d == 24);
        bool farther = false;
        for (int j = 0; j < 24; ++j)
            if (own(pos, p, j) && (p == 0 ? j > i : j < i)) farther = true;
        if (exact || !farther) out.push_back(Step{i, OFF, d, false});
    }
    return out;
}

void apply(Position& pos, int p, const Step& s) {
    const int sg = p == 0 ? 1 : -1;
    if (s.from == BAR) --pos.bar[p];
    else pos.pts[s.from] = (int8_t)(pos.pts[s.from] - sg);
    if (s.to == OFF) { ++pos.off[p]; return; }
    if (own(pos, 1 - p, s.to) == 1) {
        pos.pts[s.to] = 0;
        ++pos.bar[1 - p];
    }
    pos.pts[s.to] = (int8_t)(pos.pts[s.to] + sg);
}

struct Seq {
    std::vector<Step> steps;
    Position end;
    bool won;
};

void rec(const Position& pos, int p, std::vector<int> dice, std::vector<Step>& path, std::vector<Seq>& out) {
    bool any = false;
    std::set<int> tried;
    for (size_t k = 0; k < dice.size(); ++k) {
        if (!tried.insert(dice[k]).second) continue;
        std::vector<int> rest = dice;
        rest.erase(rest.begin() + (long)k);
        for (const Step& s : singles(pos, p, dice[k])) {
            any = true;
            Position q = pos;
            apply(q, p, s);
            path.push_back(s);
            if (q.off[p] == 15) out.push_back(Seq{path, q, true});
            else rec(q, p, rest, path, out);
            path.pop_back();
        }
    }
    if (!any) out.push_back(Seq{path, pos, false});
}

// Maximal sequences per the rules.
std::vector<Seq> maximal(const Position& pos, int p, int d1, int d2) {
    std::vector<int> dice = d1 == d2 ? std::vector<int>{d1, d1, d1, d1} : std::vector<int>{d1, d2};
    std::vector<Seq> all;
    std::vector<Step> path;
    rec(pos, p, dice, path, all);
    size_t mx = 0;
    for (const Seq& s : all) mx = std::max(mx, s.won ? dice.size() : s.steps.size());
    std::vector<Seq> out;
    if (mx == 0) return out;
    for (const Seq& s : all)
        if (s.won || s.steps.size() == mx) out.push_back(s);
    if (mx == 1 && d1 != d2) {
        const int big = std::max(d1, d2);
        bool bigOk = false;
        for (const Seq& s : out) bigOk = bigOk || s.steps[0].die == big;
        if (bigOk) {
            std::vector<Seq> f;
            for (const Seq& s : out)
                if (s.steps[0].die == big) f.push_back(s);
            out = f;
        }
    }
    return out;
}

} // namespace ref

std::string posKey(const Position& p) {
    std::string s(reinterpret_cast<const char*>(&p), sizeof(Position));
    return s;
}

// Random legal-looking position: 15 checkers per side on disjoint points / bar / off.
Position randomPosition(okey::Rng& rng) {
    Position pos;
    const int mode = rng.range(4); // 0 = anywhere, 1 = bear-off race, 2 = with bar, 3 = mixed home-heavy
    for (int p = 0; p < 2; ++p) {
        int left = kCheckers;
        if (mode == 1 || mode == 3) {
            const int off = rng.range(0, mode == 1 ? 12 : 6);
            pos.off[p] = (int8_t)off;
            left -= off;
        }
        if (mode == 2) {
            const int b = rng.range(0, 3);
            pos.bar[p] = (int8_t)b;
            left -= b;
        }
        int guard = 0;
        while (left > 0 && guard++ < 1000) {
            int idx;
            if (mode == 1 || (mode == 3 && rng.chance(0.7f))) idx = p == 0 ? rng.range(0, 5) : rng.range(18, 23);
            else idx = rng.range(0, 23);
            const int ownerHere = pos.owner(idx);
            if (ownerHere == 1 - p) continue;
            const int n = std::min(left, rng.range(1, 3));
            pos.pts[idx] = (int8_t)(pos.pts[idx] + (p == 0 ? n : -n));
            left -= n;
        }
        if (left > 0) pos.off[p] = (int8_t)(pos.off[p] + left);
    }
    // a finished side would make the position terminal
    for (int p = 0; p < 2; ++p)
        if (pos.off[p] == kCheckers) {
            pos.off[p] = 14;
            const int idx = p == 0 ? 0 : 23;
            if (pos.owner(idx) == 1 - p) {
                pos.off[1 - p] = (int8_t)(pos.off[1 - p] + pos.count(idx));
                pos.pts[idx] = 0;
                if (pos.off[1 - p] == kCheckers) { pos.off[1 - p] = 14; pos.pts[p == 0 ? 23 : 0] = (int8_t)(p == 0 ? -1 : 1); }
            }
            pos.pts[idx] = (int8_t)(pos.pts[idx] + (p == 0 ? 1 : -1));
        }
    return pos;
}

// ---------------------------------------------------------------------------------------------------------

void testStartPosition() {
    const Position pos = Position::initial();
    CHECK_EQ(total(pos, 0), 15);
    CHECK_EQ(total(pos, 1), 15);
    CHECK_EQ(pipCount(pos, 0), 167);
    CHECK_EQ(pipCount(pos, 1), 167);
    CHECK_EQ((int)pos.pts[23], 2);
    CHECK_EQ((int)pos.pts[12], 5);
    CHECK_EQ((int)pos.pts[7], 3);
    CHECK_EQ((int)pos.pts[5], 5);
    CHECK_EQ((int)pos.pts[0], -2);
    CHECK_EQ((int)pos.pts[11], -5);
    CHECK_EQ((int)pos.pts[16], -3);
    CHECK_EQ((int)pos.pts[18], -5);
    for (int i = 0; i < 24; ++i) CHECK_EQ((int)pos.pts[i], -(int)pos.pts[23 - i]); // mirror image
    CHECK_EQ(pointNumber(0, 5), 6);
    CHECK_EQ(pointNumber(1, 18), 6);
    CHECK_EQ(entryPoint(0, 6), 18);
    CHECK_EQ(entryPoint(1, 6), 5);
    CHECK(inHome(0, 0) && inHome(0, 5) && !inHome(0, 6));
    CHECK(inHome(1, 18) && inHome(1, 23) && !inHome(1, 17));
}

void testOpeningRoll() {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.debugQueueDice(3, 3);
    g.debugQueueDice(5, 2);
    g.startMatch(42);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(hasEvent(ev, EvType::MatchStart));
    CHECK(hasEvent(ev, EvType::GameStart));
    CHECK_EQ(g.stage(), Stage::OpeningRoll);
    CHECK_EQ(g.current(), -1);
    CHECK(!g.roll(0).ok);
    CHECK(g.rollOpening().ok);
    ev = g.drainEvents();
    const GameEvent* o = findEvent(ev, EvType::OpeningRoll);
    CHECK(o != nullptr);
    if (o) {
        CHECK_EQ(o->player, -1);
        CHECK_EQ(o->d1, 3);
        CHECK(o->text.find("Eşit") != std::string::npos);
    }
    CHECK_EQ(g.stage(), Stage::OpeningRoll);
    CHECK(g.rollOpening().ok);
    ev = g.drainEvents();
    o = findEvent(ev, EvType::OpeningRoll);
    CHECK(o && o->player == 0 && o->d1 == 5 && o->d2 == 2);
    if (o) CHECK_EQ(o->text, std::string("Başlangıç zarı: sen 5, Kel Mahmut 2. Sen başlıyorsun, zarları at!"));
    CHECK_EQ(g.openingDie(0), 5);
    CHECK_EQ(g.openingDie(1), 2);
    // Türk usulü: the starter throws both dice again
    CHECK_EQ(g.stage(), Stage::NeedRoll);
    CHECK_EQ(g.current(), 0);
    CHECK(!g.rollOpening().ok);
    CHECK(!g.roll(1).ok);
    CHECK_EQ(g.roll(1).error, std::string("Sıra sende değil"));
    g.debugQueueDice(6, 6);
    CHECK(g.roll(0).ok);
    CHECK_EQ(g.stage(), Stage::Moving);
    CHECK(g.dice().isDouble());
    CHECK_EQ(g.dice().n, 4);
    CHECK_EQ(g.dice().leftCount(), 4);

    // International variant: the starter plays the two opening dice.
    Rules r;
    r.openingReroll = false;
    Game h(r);
    h.setPlayer(0, "Sen", true);
    h.setPlayer(1, "Kel Mahmut", false);
    h.debugQueueDice(2, 5);
    h.startMatch(1);
    CHECK(h.rollOpening().ok);
    CHECK_EQ(h.stage(), Stage::Moving);
    CHECK_EQ(h.current(), 1);
    CHECK_EQ(h.dice().d1, 5); // starter's own die first
    CHECK_EQ(h.dice().d2, 2);
    ev = h.drainEvents();
    CHECK(hasEvent(ev, EvType::Roll));
}

void testRollAndDiceNames() {
    std::set<std::string> names;
    for (int a = 1; a <= 6; ++a)
        for (int b = 1; b <= a; ++b) {
            const std::string n = diceName(a, b);
            CHECK(!n.empty());
            CHECK_EQ(n, diceName(b, a));
            names.insert(n);
        }
    CHECK_EQ((int)names.size(), 21);
    CHECK_EQ(diceName(6, 6), std::string("düşeş"));
    CHECK_EQ(diceName(5, 6), std::string("şeşbeş"));
    CHECK_EQ(diceName(1, 1), std::string("hepyek"));
    CHECK_EQ(diceName(2, 2), std::string("dübara"));
    CHECK_EQ(diceName(5, 5), std::string("dübeş"));

    // Roll event texts: second person for the human, third person with the name for the bot.
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(3);
    g.debugSetTurn(0, Stage::NeedRoll);
    g.drainEvents();
    g.debugQueueDice(5, 6);
    CHECK(g.roll(0).ok);
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* r = findEvent(ev, EvType::Roll);
    CHECK(r && r->text == "Zarı attın: 6-5, şeşbeş");
    g.debugSetTurn(1, Stage::NeedRoll);
    g.debugQueueDice(5, 5);
    CHECK(g.roll(1).ok);
    ev = g.drainEvents();
    r = findEvent(ev, EvType::Roll);
    CHECK(r && r->text == "Kel Mahmut zarı attı: 5-5, dübeş!");
    CHECK(r && r->player == 1 && r->d1 == 5 && r->d2 == 5);
    CHECK(!g.roll(1).ok); // already rolled
}

void testBarEntry() {
    // Player 0 has a checker on the bar; player 1 holds idx 18 (entry with 6) with two checkers.
    const Position pos = PB().bar(0, 1).at(0, 12, 5).at(0, 5, 9).at(1, 18, 2).at(1, 0, 13).fill();
    Game g = moving(pos, 0, 6, 5);
    std::vector<Step> st = g.legalSteps();
    CHECK(!st.empty());
    for (const Step& s : st) CHECK(s.from == BAR || g.turnSteps().size() > 0);
    CHECK(keys(st) == (std::set<StepKey>{{BAR, 19, 5}}));
    ActionResult r = g.applyStep(0, 12, 6);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Önce kırık pulunu girmelisin"));
    r = g.applyStep(0, BAR, 18);
    CHECK_EQ(r.error, std::string("O kapı kapalı"));
    CHECK(g.applyStep(0, BAR, 19).ok);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(hasEvent(ev, EvType::EnterFromBar));
    const GameEvent* e = findEvent(ev, EvType::Step);
    CHECK(e && e->from == BAR && e->to == 19 && e->die == 5);
    CHECK(e && e->text == "Kırık pulunu 20'ye girdin");
    // now the 6 can be played by any checker
    st = g.legalSteps();
    CHECK(!st.empty());
    for (const Step& s : st) CHECK_EQ(s.die, 6);

    // Both entry points closed -> NoMove, turn passes.
    const Position blocked = PB().bar(0, 1).at(0, 5, 14).at(1, 18, 2).at(1, 19, 2).at(1, 0, 11).fill();
    Game h = moving(blocked, 0, 1, 1);
    h.debugSetTurn(0, Stage::NeedRoll);
    h.debugQueueDice(6, 5);
    h.drainEvents();
    CHECK(h.roll(0).ok);
    ev = h.drainEvents();
    const GameEvent* nm = findEvent(ev, EvType::NoMove);
    CHECK(nm != nullptr);
    if (nm) CHECK_EQ(nm->text, std::string("Oynayacak yerin yok, sıra geçti"));
    CHECK(hasEvent(ev, EvType::TurnEnd));
    CHECK_EQ(h.current(), 1);
    CHECK_EQ(h.stage(), Stage::NeedRoll);

    // Two on the bar with a double: both enter, then the other two moves are free.
    const Position two = PB().bar(0, 2).at(0, 12, 13).at(1, 0, 15).fill();
    Game d = moving(two, 0, 3, 3);
    st = d.legalSteps();
    CHECK(keys(st) == (std::set<StepKey>{{BAR, 21, 3}}));
    CHECK(d.applyStep(0, BAR, 21).ok);
    CHECK(d.applyStep(0, 12, 9).ok == false); // second checker still on the bar
    CHECK(d.applyStep(0, BAR, 21).ok);
    CHECK(d.applyStep(0, 12, 9).ok);
    CHECK(d.applyStep(0, 21, 18).ok);
    CHECK_EQ(d.current(), 1); // four moves -> turn over
}

void testHitting() {
    const Position pos = PB().at(0, 12, 14).at(0, 5, 1).at(1, 9, 1).at(1, 2, 1).at(1, 20, 13).fill();
    Game g = moving(pos, 0, 3, 1);
    CHECK(g.applyStep(0, 12, 9).ok);
    CHECK_EQ(g.position().bar[1], 1);
    CHECK_EQ((int)g.position().pts[9], 1);
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* s = findEvent(ev, EvType::Step);
    CHECK(s && s->hit);
    CHECK(s && s->text == "13'ten 10'a oynadın, pul kırdın!");
    const GameEvent* h = findEvent(ev, EvType::Hit);
    CHECK(h && h->amount == 1 && h->to == 9 && h->text == "Pul kırdın!");
    CHECK_EQ(total(g.position(), 1), 15);
    // Undo restores the hit checker
    CHECK(g.undoStep(0).ok);
    CHECK_EQ(g.position().bar[1], 0);
    CHECK_EQ((int)g.position().pts[9], -1);
    CHECK(g.position() == pos);
    CHECK_EQ(g.dice().leftCount(), 2);
    ev = g.drainEvents();
    CHECK(hasEvent(ev, EvType::Undo));
    CHECK(!g.undoStep(0).ok);
    CHECK_EQ(g.undoStep(0).error, std::string("Geri alınacak hamle yok"));

    // Bot hitting the human: "Kel Mahmut pulunu kırdı!"
    const Position p2 = PB().at(0, 9, 1).at(0, 3, 14).at(1, 6, 15).fill();
    Game b = moving(p2, 1, 3, 2);
    CHECK(b.applyStep(1, 6, 9).ok);
    ev = b.drainEvents();
    h = findEvent(ev, EvType::Hit);
    CHECK(h && h->text == "Kel Mahmut pulunu kırdı!");
    s = findEvent(ev, EvType::Step);
    CHECK(s && s->text == "Kel Mahmut 18'den 15'e oynadı, pul kırdı!");
}

void testClosedPointsAndErrors() {
    const Position pos = PB().at(0, 12, 15).at(1, 7, 2).at(1, 20, 13).fill();
    Game g = moving(pos, 0, 5, 2);
    CHECK_EQ(g.applyStep(0, 12, 7).error, std::string("O kapı kapalı"));
    CHECK_EQ(g.applyStep(0, 12, 14).error, std::string("Pullar geri gitmez"));
    CHECK_EQ(g.applyStep(0, 11, 9).error, std::string("Orada senin pulun yok"));
    CHECK_EQ(g.applyStep(0, 12, 8).error, std::string("Zarlarda 4 yok"));
    CHECK_EQ(g.applyStep(0, 12, 5).error, std::string("Her seferinde bir zar oyna: önce birini, sonra ötekini"));
    CHECK_EQ(g.applyStep(0, 12, OFF).error, std::string("Pul toplamak için bütün pulların evde olmalı"));
    CHECK_EQ(g.applyStep(1, 7, 9).error, std::string("Sıra sende değil"));
    // 5 then 2 is blocked at 7 (12-5 = 7 closed); 2 then 5 works: 12 -> 10 -> 5
    CHECK(keys(g.legalSteps()) == (std::set<StepKey>{{12, 10, 2}}));
    CHECK(g.applyStep(0, 12, 10).ok);
    CHECK(g.applyStep(0, 10, 5).ok);
    CHECK_EQ(g.current(), 1);
    g.drainEvents();
    CHECK_EQ(g.applyStep(1, 7, 9).error, std::string("Önce zar atmalısın"));
}

void testMustUseBothAndLarger() {
    // Must play the larger die when only one can be played: single checker on 10, dice 6-3,
    // idx 1 closed: 10-6-3 and 10-3-6 both end on 1, so only one die is playable -> the 6.
    {
        const Position pos = PB().at(0, 10, 1).at(1, 1, 2).at(1, 20, 13).fill();
        Game g = moving(pos, 0, 6, 3);
        CHECK(keys(g.legalSteps()) == (std::set<StepKey>{{10, 4, 6}}));
        const ActionResult r = g.applyStep(0, 10, 7);
        CHECK(!r.ok);
        CHECK(r.error.find("büyük zarı") != std::string::npos);
        CHECK(g.applyStep(0, 10, 4).ok);
        CHECK_EQ(g.current(), 1); // the 3 is unplayable (4 -> 1 closed): turn over
        CHECK_EQ(generatePlays(pos, 0, 6, 3).size(), (size_t)1);
    }
    // ... unless the larger cannot be played at all: idx 4 closed as well -> the 3.
    {
        const Position pos = PB().at(0, 10, 1).at(1, 1, 2).at(1, 4, 2).at(1, 20, 11).fill();
        Game g = moving(pos, 0, 6, 3);
        CHECK(keys(g.legalSteps()) == (std::set<StepKey>{{10, 7, 3}}));
    }
    // Checker A on 13, checker B on 2 (B can never move: not all home), p1 closes 9 and 4.
    // 5 first: 13 -> 8, then the 4 (8 -> 4) is closed; 4 first: 13 -> 9 closed. One die only -> the 5.
    {
        const Position pos = PB().at(0, 13, 1).at(0, 2, 1).at(1, 9, 2).at(1, 4, 2).at(1, 20, 11).fill();
        Game g = moving(pos, 0, 5, 4);
        CHECK(keys(g.legalSteps()) == (std::set<StepKey>{{13, 8, 5}}));
    }
    // Must use both dice: a step that is legal on its own is refused when the other die then cannot be played.
    {
        const Position pos = PB().at(0, 13, 1).at(0, 2, 1).at(0, 20, 1).at(1, 4, 2).at(1, 16, 2).at(1, 21, 9).fill();
        Game g = moving(pos, 0, 5, 4);
        // 20 -> 16 (4) is closed; 20 -> 15 (5) ok then 4: 13 -> 9 ok or 15 -> 11 ok.
        // 13 -> 9 (4) then 5: 20 -> 15 ok. 13 -> 8 (5) then 4: 8 -> 4 closed, 20 -> 16 closed -> dead end!
        const std::set<StepKey> legal = keys(g.legalSteps());
        CHECK(legal.count({20, 15, 5}) == 1);
        CHECK(legal.count({13, 9, 4}) == 1);
        CHECK(legal.count({13, 8, 5}) == 0); // legal on its own, but then the 4 cannot be played
        const ActionResult r = g.applyStep(0, 13, 8);
        CHECK(!r.ok);
        CHECK(r.error.find("hepsi oynanamıyor") != std::string::npos);
    }
}

void testDoubles() {
    const Position pos = PB().at(0, 12, 5).at(0, 5, 10).at(1, 20, 15).fill();
    Game g = moving(pos, 0, 2, 2);
    CHECK_EQ(g.dice().n, 4);
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(g.current(), 0);
        CHECK_EQ(g.dice().leftCount(), 4 - k);
        CHECK(g.applyStep(0, 12 - 2 * k, 10 - 2 * k).ok); // one checker 12 -> 4 in four steps
    }
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.position().pts[4], 1);
    // generatePlays dedups permutations: e.g. the double 2-2 from the start position
    const std::vector<Play> plays = generatePlays(Position::initial(), 0, 2, 2);
    std::set<std::string> uniq;
    for (const Play& p : plays) {
        CHECK_EQ(p.steps.size(), (size_t)4);
        uniq.insert(posKey(p.result));
    }
    CHECK_EQ(uniq.size(), plays.size());
    CHECK(plays.size() > 10);
}

void testBearOff() {
    // all home: checkers on 4 (idx 4) and 2 (idx 2)
    {
        const Position pos = PB().at(0, 4, 1).at(0, 2, 1).at(1, 20, 2).at(1, 22, 3).fill();
        Game g = moving(pos, 0, 6, 4);
        const std::set<StepKey> legal = keys(g.legalSteps());
        // 6: bear off from idx 4 (highest, higher die) ; idx 2 with 6 not allowed (checker on a higher point)
        CHECK(legal.count({4, OFF, 6}) == 1);
        CHECK(legal.count({2, OFF, 6}) == 0);
        // 4: 4 -> 0 move, or not off from 2 (higher die while 4 is occupied)
        CHECK(legal.count({4, 0, 4}) == 1);
        CHECK(legal.count({2, OFF, 4}) == 0);
        CHECK(g.applyStep(0, 4, OFF).ok); // die inferred: 6 is the only die that bears off from idx 4
        CHECK_EQ(g.turnSteps().back().die, 6);
        // now idx 2 is highest: 4 bears it off -> game over (mars? p1 has 10 off -> no)
        CHECK(g.applyStep(0, 2, OFF).ok);
        CHECK_EQ(g.stage(), Stage::GameOver);
        CHECK_EQ(g.lastResult().winner, 0);
        CHECK_EQ(g.lastResult().points, 1);
        CHECK(!g.lastResult().mars);
        CHECK_EQ(g.score(0), 1);
        std::vector<GameEvent> ev = g.drainEvents();
        const GameEvent* b = findEvent(ev, EvType::BearOff);
        CHECK(b && b->text == "Pul topladın (14/15)");
        const GameEvent* e = findEvent(ev, EvType::GameEnd);
        CHECK(e && e->text == "Oyunu kazandın! (+1)");
    }
    // exact die required / smaller die moves inside
    {
        const Position pos = PB().at(0, 5, 2).at(0, 1, 1).at(1, 20, 2).fill();
        Game g = moving(pos, 0, 3, 1);
        const std::set<StepKey> legal = keys(g.legalSteps());
        CHECK(legal.count({1, OFF, 3}) == 0); // checkers on idx 5
        CHECK(legal.count({5, 2, 3}) == 1);
        CHECK(legal.count({1, 0, 1}) == 1);
        CHECK_EQ(g.applyStep(0, 1, OFF).error, std::string("Bu zarlarla o puldan toplanmaz; önce arkadaki pulları oyna"));
    }
    // a checker outside home: no bearing off
    {
        const Position pos = PB().at(0, 6, 1).at(0, 0, 3).at(1, 20, 2).fill();
        Game g = moving(pos, 0, 6, 1);
        const std::set<StepKey> legal = keys(g.legalSteps());
        CHECK(legal.count({0, OFF, 1}) == 0 || legal.count({6, 5, 1}) == 1);
        CHECK(legal.count({6, 0, 6}) == 1);
        // after 6->5 with the 1 everything is home and the 6 bears off from 5
        CHECK(g.applyStep(0, 6, 5).ok);
        CHECK(keys(g.legalSteps()).count({5, OFF, 6}) == 1);
    }
    // player 1 bearing off (moves up, off past 23)
    {
        const Position pos = PB().at(1, 22, 1).at(1, 19, 1).at(0, 3, 5).fill();
        Game g = moving(pos, 1, 5, 2);
        const std::set<StepKey> legal = keys(g.legalSteps());
        CHECK(legal.count({19, OFF, 5}) == 1);
        CHECK(legal.count({22, OFF, 2}) == 1);
        CHECK(legal.count({19, 21, 2}) == 1);
    }
    // Bearing off with a die that is larger than needed while two dice fit: the smaller is inferred
    {
        const Position pos = PB().at(0, 2, 3).at(1, 20, 2).fill();
        Game g = moving(pos, 0, 6, 5);
        CHECK(g.applyStep(0, 2, OFF).ok);
        CHECK_EQ(g.turnSteps().back().die, 5);
        CHECK(g.applyStep(0, 2, OFF, 6).ok);
    }
}

void testMarsAndMatch() {
    Rules r;
    r.matchPoints = 3;
    // mars: p1 has borne off nothing
    const Position pos = PB().at(0, 0, 1).at(1, 12, 15).fill();
    Game g = moving(pos, 0, 1, 2, r);
    CHECK(g.applyStep(0, 0, OFF).ok);
    CHECK_EQ(g.stage(), Stage::GameOver);
    CHECK(g.lastResult().mars);
    CHECK(!g.lastResult().katmerli);
    CHECK_EQ(g.lastResult().points, 2);
    CHECK_EQ(g.score(0), 2);
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* e = findEvent(ev, EvType::GameEnd);
    CHECK(e && e->mars && e->amount == 2 && e->text == "Mars ettin! (+2)");
    CHECK(!hasEvent(ev, EvType::MatchEnd));

    // next game: the winner starts (no opening roll)
    g.startNextGame();
    CHECK_EQ(g.gameIndex(), 1);
    CHECK_EQ(g.stage(), Stage::NeedRoll);
    CHECK_EQ(g.current(), 0);
    CHECK(g.position() == Position::initial());
    ev = g.drainEvents();
    e = findEvent(ev, EvType::GameStart);
    CHECK(e && e->player == 0 && e->text == "2. oyun: önceki oyunu sen aldın, sen başlıyorsun");

    // bot wins the second game with a katmerli mars (human checker in the bot's home) -> 2 points (Türk usulü)
    const Position p2 = PB().at(1, 23, 1).at(0, 20, 15).fill();
    g.debugSetPosition(p2);
    g.debugSetTurn(1, Stage::Moving);
    g.debugSetDice(3, 1);
    CHECK(g.applyStep(1, 23, OFF).ok);
    CHECK(g.lastResult().katmerli);
    CHECK_EQ(g.lastResult().points, 2);
    CHECK_EQ(g.score(1), 2);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::GameEnd);
    CHECK(e && e->text == "Kel Mahmut katmerli mars etti! (+2)");
    CHECK_EQ(g.stage(), Stage::GameOver);

    // third game: plain win of the bot -> 3 points -> match over
    g.startNextGame();
    CHECK_EQ(g.current(), 1);
    const Position p3 = PB().at(1, 23, 1).at(0, 3, 5).fill();
    g.debugSetPosition(p3);
    g.debugSetTurn(1, Stage::Moving);
    g.debugSetDice(4, 2);
    CHECK(g.applyStep(1, 23, OFF).ok);
    CHECK_EQ(g.lastResult().points, 1);
    CHECK_EQ(g.stage(), Stage::MatchOver);
    CHECK_EQ(g.matchWinner(), 1);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::MatchEnd);
    CHECK(e && e->player == 1 && e->text == "Maçı Kel Mahmut kazandı (3-2)");
    g.startNextGame(); // ignored after MatchOver
    CHECK_EQ(g.stage(), Stage::MatchOver);

    // katmerliMars option: 3 points
    Rules k;
    k.katmerliMars = true;
    const Position p4 = PB().at(0, 0, 1).bar(1, 1).at(1, 12, 14).fill();
    Game h = moving(p4, 0, 1, 2, k);
    CHECK(h.applyStep(0, 0, OFF).ok);
    CHECK(h.lastResult().katmerli);
    CHECK_EQ(h.lastResult().points, 3);

    // winnerStarts = false: every game opens with an opening roll
    Rules w;
    w.winnerStarts = false;
    Game m = moving(pos, 0, 1, 2, w);
    CHECK(m.applyStep(0, 0, OFF).ok);
    m.startNextGame();
    CHECK_EQ(m.stage(), Stage::OpeningRoll);
}

void testUndoAndConfirm() {
    Rules r;
    r.confirmTurn = true;
    Game g = moving(Position::initial(), 0, 6, 1, r);
    CHECK(g.applyStep(0, 12, 6).ok);
    CHECK(g.applyStep(0, 7, 6).ok);
    CHECK_EQ(g.current(), 0);
    CHECK(g.turnComplete());
    CHECK(g.canUndo());
    CHECK(g.legalSteps().empty());
    CHECK(g.undoStep(0).ok);
    CHECK(!g.turnComplete());
    CHECK_EQ(g.endTurn(0).error, std::string("Daha oynaman gereken zar var"));
    CHECK(g.undoStep(0).ok);
    CHECK(g.position() == Position::initial());
    CHECK_EQ(g.dice().leftCount(), 2);
    CHECK(g.applyStep(0, 23, 17).ok);
    CHECK_EQ(g.applyStep(0, 17, 16).error, std::string("O kapı kapalı"));
    CHECK(g.applyStep(0, 23, 22).ok);
    CHECK(g.endTurn(0).ok);
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* t = findEvent(ev, EvType::TurnEnd);
    CHECK(t && t->text == "Sıra Kel Mahmut'ta" && t->amount == 1);
    CHECK_EQ(g.current(), 1);
    CHECK(!g.undoStep(0).ok); // not his turn any more

    // automatic end of turn (default): no undo after the last step
    Game a = moving(Position::initial(), 0, 6, 1);
    CHECK(a.applyStep(0, 12, 6).ok);
    CHECK(a.applyStep(0, 7, 6).ok);
    CHECK_EQ(a.current(), 1);
    CHECK_EQ(a.stage(), Stage::NeedRoll);
    CHECK(!a.undoStep(0).ok);
}

void testAllTurnPlaysConsistency() {
    // From the start position every roll: every play replays through applyStep, ends the turn, and every
    // random legal-step sequence ends in one of the plays.
    okey::Rng rng(99);
    for (int a = 1; a <= 6; ++a)
        for (int b = 1; b <= a; ++b) {
            Game g = moving(Position::initial(), 0, a, b);
            const std::vector<Play> plays = g.allTurnPlays();
            CHECK(!plays.empty());
            std::set<std::string> results;
            for (const Play& p : plays) results.insert(posKey(p.result));
            CHECK_EQ(results.size(), plays.size());
            for (const Play& p : plays) {
                Game t = g;
                bool ok = true;
                for (const Step& s : p.steps) ok = ok && t.applyStep(0, s.from, s.to, s.die).ok;
                CHECK(ok);
                CHECK(t.position() == p.result);
                CHECK_EQ(t.current(), 1);
            }
            for (int k = 0; k < 30; ++k) {
                Game t = g;
                Position last = t.position();
                int guard = 0;
                while (t.current() == 0 && t.stage() == Stage::Moving && guard++ < 8) {
                    const std::vector<Step> st = t.legalSteps();
                    CHECK(!st.empty());
                    if (st.empty()) break;
                    const Step s = st[(size_t)rng.range((int)st.size())];
                    CHECK(t.applyStep(0, s.from, s.to, s.die).ok);
                    last = t.position();
                }
                CHECK(results.count(posKey(last)) == 1);
            }
            // partial turn: allTurnPlays continues from the current state
            Game t = g;
            const Step s0 = t.legalSteps()[0];
            CHECK(t.applyStep(0, s0.from, s0.to, s0.die).ok);
            if (t.current() == 0) {
                const std::vector<Play> rest = t.allTurnPlays();
                CHECK(!rest.empty());
                for (const Play& p : rest) CHECK(results.count(posKey(p.result)) == 1);
            }
        }
}

// Engine vs reference on many random positions and dice.
void testAgainstReference() {
    okey::Rng rng(2024);
    int positions = 0, compared = 0;
    for (int n = 0; n < 6000; ++n) {
        const Position pos = randomPosition(rng);
        if (total(pos, 0) != 15 || total(pos, 1) != 15) continue;
        ++positions;
        const int p = rng.range(2);
        const int d1 = rng.range(1, 6), d2 = rng.range(1, 6);
        const std::vector<ref::Seq> seqs = ref::maximal(pos, p, d1, d2);
        std::set<StepKey> refFirst;
        std::set<std::string> refEnds;
        for (const ref::Seq& s : seqs) {
            refFirst.insert({s.steps[0].from, s.steps[0].to, s.steps[0].die});
            refEnds.insert(posKey(s.end));
        }
        // generatePlays: same set of resulting positions
        const std::vector<Play> plays = generatePlays(pos, p, d1, d2);
        std::set<std::string> ends;
        for (const Play& pl : plays) ends.insert(posKey(pl.result));
        CHECK_EQ(ends.size(), plays.size());
        if (ends != refEnds) {
            reportFailure(__FILE__, __LINE__, "generatePlays != reference (case " + std::to_string(n) + ")");
            continue;
        }
        std::vector<Position> res;
        generateResults(pos, p, d1, d2, res);
        CHECK_EQ(res.size(), plays.size());
        // legalSteps: the first steps of the maximal sequences
        Game g = moving(pos, p, d1, d2);
        const std::set<StepKey> legal = keys(g.legalSteps());
        if (legal != refFirst) {
            reportFailure(__FILE__, __LINE__, "legalSteps != reference (case " + std::to_string(n) + ")");
            continue;
        }
        // each play replays through the engine
        for (const Play& pl : plays) {
            Game t = g;
            bool ok = true;
            for (const Step& s : pl.steps) ok = ok && t.applyStep(p, s.from, s.to, s.die).ok;
            CHECK(ok);
            CHECK(t.position() == pl.result);
        }
        ++compared;
    }
    CHECK(positions > 5000);
    CHECK(compared == positions);
    std::printf("  reference cross-check: %d random positions\n", compared);
}

// Random games with random legal steps: invariants and termination.
void testFuzz() {
    okey::Rng rng(77);
    int games = 0, mars = 0, doubles = 0;
    long long steps = 0;
    for (int n = 0; n < 3000; ++n) {
        Rules r;
        r.matchPoints = 1 + rng.range(3);
        r.openingReroll = rng.chance(0.7f);
        r.winnerStarts = rng.chance(0.7f);
        r.confirmTurn = rng.chance(0.3f);
        r.katmerliMars = rng.chance(0.2f);
        r.doubling = rng.chance(0.5f);
        Game g(r);
        g.setPlayer(0, "Sen", rng.chance(0.5f));
        g.setPlayer(1, "Kel Mahmut", false);
        g.startMatch(1000 + (uint64_t)n);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && guard++ < 20000) {
            const Position& pos = g.position();
            for (int p = 0; p < 2; ++p) {
                CHECK_EQ(total(pos, p), 15);
                CHECK(pos.bar[p] >= 0 && pos.off[p] >= 0 && pos.off[p] <= 15);
            }
            for (const GameEvent& e : g.drainEvents()) CHECK(!e.text.empty());
            switch (g.stage()) {
            case Stage::OpeningRoll: CHECK(g.rollOpening().ok); break;
            case Stage::NeedRoll:
                if (g.canDouble(g.current()) && rng.chance(0.1f)) {
                    CHECK(!g.acceptDouble(g.current()).ok);
                    CHECK(g.offerDouble(g.current()).ok);
                    ++doubles;
                } else {
                    CHECK(!g.offerDouble(1 - g.current()).ok);
                    CHECK(g.roll(g.current()).ok);
                }
                break;
            case Stage::DoubleOffered: {
                const int q = g.responder();
                CHECK_EQ(q, 1 - g.current());
                CHECK(!g.roll(g.current()).ok);
                CHECK(!g.acceptDouble(g.current()).ok);
                if (rng.chance(0.75f)) CHECK(g.acceptDouble(q).ok);
                else CHECK(g.declineDouble(q).ok);
                break;
            }
            case Stage::Moving: {
                const int p = g.current();
                const std::vector<Step> st = g.legalSteps();
                if (st.empty()) {
                    CHECK(r.confirmTurn);
                    CHECK(g.turnComplete());
                    CHECK(g.endTurn(p).ok);
                    break;
                }
                if (g.canUndo() && rng.chance(0.05f)) {
                    CHECK(g.undoStep(p).ok);
                    break;
                }
                const Step s = st[(size_t)rng.range((int)st.size())];
                // the opponent may never step during my turn
                CHECK(!g.applyStep(1 - p, s.from, s.to).ok);
                CHECK(g.applyStep(p, s.from, s.to, s.die).ok);
                ++steps;
                break;
            }
            case Stage::GameOver:
                ++games;
                mars += g.lastResult().mars;
                CHECK(g.lastResult().base >= 1 && g.lastResult().base <= 3);
                CHECK_EQ(g.lastResult().points, g.lastResult().base * g.lastResult().cube);
                CHECK(g.lastResult().dropped || g.position().off[g.lastResult().winner] == 15);
                g.startNextGame();
                break;
            default: break;
            }
        }
        CHECK_EQ(g.stage(), Stage::MatchOver);
        ++games;
        CHECK(g.score(g.matchWinner()) >= r.matchPoints);
        CHECK(g.score(1 - g.matchWinner()) < r.matchPoints);
    }
    std::printf("  fuzz: %d games, %lld random steps, %d mars, %d doubles\n", games, steps, mars, doubles);
}

void testDeterminism() {
    auto run = [](uint64_t seed) {
        Game g;
        g.setPlayer(0, "Sen", true);
        g.setPlayer(1, "Kel Mahmut", false);
        g.startMatch(seed);
        Bot a(BotLevel::Normal, 5), b(BotLevel::Hard, 6);
        std::string log;
        int guard = 0;
        while (g.stage() != Stage::MatchOver && guard++ < 20000) {
            for (const GameEvent& e : g.drainEvents()) log += e.text + "\n";
            if (g.stage() == Stage::GameOver) {
                g.startNextGame();
                continue;
            }
            const int p = g.stage() == Stage::OpeningRoll ? 0 : g.current();
            const BotAction act = (p == 0 ? a : b).next(g, p);
            if (!applyBotAction(g, p, act).ok) log += "REJECTED\n";
        }
        for (const GameEvent& e : g.drainEvents()) log += e.text + "\n";
        return log;
    };
    const std::string l1 = run(12345), l2 = run(12345), l3 = run(54321);
    CHECK(l1 == l2);
    CHECK(l1 != l3);
    CHECK(l1.find("REJECTED") == std::string::npos);
    CHECK(l1.find("Maç") != std::string::npos);
}

void testDoubling() {
    // off by default: no offers
    {
        Game g = moving(Position::initial(), 0, 6, 5);
        g.debugSetTurn(0, Stage::NeedRoll);
        CHECK(!g.canDouble(0));
        CHECK(g.offerDouble(0).error == "Bu masada katlama yok");
    }
    Rules r;
    r.doubling = true;
    r.matchPoints = 5;
    Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(3);
    CHECK_EQ(g.cubeValue(), 1);
    CHECK_EQ(g.cubeOwner(), -1);
    CHECK(!g.canDouble(0) && !g.canDouble(1));
    CHECK(g.offerDouble(0).error == "Önce başlangıç zarları atılmalı");
    g.debugQueueDice(5, 3);
    CHECK(g.rollOpening().ok);
    CHECK_EQ(g.stage(), Stage::NeedRoll);
    CHECK(!g.canDouble(0)); // the game's first turn
    CHECK(g.offerDouble(0).error == "İlk hamleden önce katlanmaz");
    g.drainEvents();
    // a later turn: the cube is in the middle, either player may double on his turn
    g.debugSetTurn(0, Stage::NeedRoll);
    CHECK(g.canDouble(0));
    CHECK(!g.canDouble(1));
    CHECK(g.offerDouble(1).error == "Sıra sende değil");
    CHECK(g.offerDouble(0).ok);
    CHECK_EQ(g.stage(), Stage::DoubleOffered);
    CHECK_EQ(g.responder(), 1);
    CHECK_EQ(g.cubeValue(), 1); // not yet taken
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* e = findEvent(ev, EvType::DoubleOffer);
    CHECK(e && e->player == 0 && e->amount == 2 && e->text == "Katladın: 2");
    CHECK(g.roll(0).error == "Önce katlamaya cevap verilmeli");
    CHECK(g.offerDouble(0).error == "Katlama zaten teklif edildi");
    CHECK(g.acceptDouble(0).error == "Katlamayı sen teklif ettin; cevap rakibinden");
    CHECK(g.acceptDouble(1).ok);
    CHECK_EQ(g.stage(), Stage::NeedRoll);
    CHECK_EQ(g.current(), 0); // the offerer rolls
    CHECK_EQ(g.cubeValue(), 2);
    CHECK_EQ(g.cubeOwner(), 1);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::DoubleTake);
    CHECK(e && e->player == 1 && e->amount == 2 && e->text == "Kel Mahmut katlamayı kabul etti");
    CHECK(!g.gameLog().empty() && g.gameLog().back().note == "katladı: 2, kabul");
    CHECK(g.acceptDouble(1).error == "Cevap verilecek bir katlama yok");
    // the cube is Mahmut's now: only he may redouble
    CHECK(!g.canDouble(0));
    CHECK(g.offerDouble(0).error == "Katlama zarı rakibinde; sadece o katlayabilir");
    CHECK(g.roll(0).ok);
    g.debugSetTurn(1, Stage::NeedRoll);
    CHECK(g.canDouble(1));
    CHECK(g.offerDouble(1).ok);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::DoubleOffer);
    CHECK(e && e->text == "Kel Mahmut katladı: 4");
    // dropping: Mahmut wins the cube's current value (2), no mars counted
    CHECK(g.declineDouble(0).ok);
    CHECK_EQ(g.stage(), Stage::GameOver);
    CHECK_EQ(g.lastResult().winner, 1);
    CHECK(g.lastResult().dropped);
    CHECK(!g.lastResult().mars);
    CHECK_EQ(g.lastResult().cube, 2);
    CHECK_EQ(g.lastResult().points, 2);
    CHECK_EQ(g.score(1), 2);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::DoubleDrop);
    CHECK(e && e->player == 0 && e->text == "Pes ettin");
    e = findEvent(ev, EvType::GameEnd);
    CHECK(e && e->player == 1 && e->amount == 2 && e->text == "Kel Mahmut oyunu aldı (+2, katlama 2)");
    CHECK(g.offerDouble(1).error == "Oyun bitti");

    // game 2: the cube goes back to the middle; a mars on a 4-cube is 2 x 4 = 8
    g.startNextGame();
    CHECK_EQ(g.cubeValue(), 1);
    CHECK_EQ(g.cubeOwner(), -1);
    CHECK(!g.crawfordGame());
    g.debugSetTurn(1, Stage::NeedRoll);
    CHECK(g.offerDouble(1).ok && g.acceptDouble(0).ok);
    g.debugSetTurn(0, Stage::NeedRoll);
    CHECK(g.offerDouble(0).ok && g.acceptDouble(1).ok);
    CHECK_EQ(g.cubeValue(), 4);
    g.debugSetPosition(PB().at(0, 0, 1).at(1, 12, 15).fill());
    g.debugSetTurn(0, Stage::Moving);
    g.debugSetDice(1, 2);
    g.drainEvents();
    CHECK(g.applyStep(0, 0, OFF).ok);
    CHECK(g.lastResult().mars);
    CHECK_EQ(g.lastResult().base, 2);
    CHECK_EQ(g.lastResult().points, 8);
    ev = g.drainEvents();
    e = findEvent(ev, EvType::GameEnd);
    CHECK(e && e->amount == 8 && e->text == "Mars ettin! (2 × 4 = +8)");
    CHECK_EQ(g.stage(), Stage::MatchOver); // 8 >= 5

    // Crawford: 3-point match; Sen reaches 2 -> the next game has no cube, the one after it has
    Rules c;
    c.doubling = true;
    c.matchPoints = 3;
    Game h(c);
    h.setPlayer(0, "Sen", true);
    h.setPlayer(1, "Kel Mahmut", false);
    h.startMatch(9);
    h.debugSetPosition(PB().at(0, 0, 1).at(1, 12, 15).fill());
    h.debugSetTurn(0, Stage::Moving);
    h.debugSetDice(1, 2);
    CHECK(h.applyStep(0, 0, OFF).ok); // mars: 2-0
    CHECK_EQ(h.score(0), 2);
    h.drainEvents();
    h.startNextGame();
    CHECK(h.crawfordGame());
    ev = h.drainEvents();
    e = findEvent(ev, EvType::GameStart);
    CHECK(e && e->text.find("Crawford") != std::string::npos);
    h.debugSetTurn(1, Stage::NeedRoll);
    CHECK(!h.canDouble(1));
    CHECK(h.offerDouble(1).error == "Crawford oyununda katlama yapılmaz");
    h.debugSetPosition(PB().at(1, 23, 1).at(0, 3, 5).fill());
    h.debugSetTurn(1, Stage::Moving);
    h.debugSetDice(4, 2);
    CHECK(h.applyStep(1, 23, OFF).ok); // 2-1
    h.startNextGame();
    CHECK(!h.crawfordGame()); // post-Crawford: the cube is back
    h.debugSetTurn(1, Stage::NeedRoll);
    CHECK(h.canDouble(1));

    // the cube stops at 64
    {
        Rules big;
        big.doubling = true;
        big.matchPoints = 1000;
        Game m(big);
        m.startMatch(4);
        int side = 0;
        while (m.cubeValue() < 64) {
            m.debugSetTurn(side, Stage::NeedRoll);
            CHECK(m.offerDouble(side).ok && m.acceptDouble(1 - side).ok);
            side = 1 - side;
        }
        m.debugSetTurn(side, Stage::NeedRoll);
        CHECK(!m.canDouble(side));
        CHECK(m.offerDouble(side).error == "Katlama zarı en yüksek değerde");
    }
}

void testHistory() {
    CHECK_EQ(stepNotation(0, Step{23, 17, 6, false}), std::string("24/18"));
    CHECK_EQ(stepNotation(0, Step{17, 12, 5, true}), std::string("18/13*"));
    CHECK_EQ(stepNotation(0, Step{BAR, 21, 3, false}), std::string("bar/22"));
    CHECK_EQ(stepNotation(0, Step{5, OFF, 6, false}), std::string("6/çıktı"));
    CHECK_EQ(stepNotation(1, Step{0, 6, 6, true}), std::string("24/18*")); // in Mahmut's own numbering
    CHECK_EQ(stepNotation(1, Step{BAR, 2, 3, false}), std::string("bar/22"));
    TurnRecord t;
    t.player = 0;
    t.d1 = 5;
    t.d2 = 6;
    t.steps = {Step{23, 17, 6, false}, Step{17, 12, 5, true}};
    CHECK_EQ(turnNotation(t), std::string("6-5 şeşbeş: 24/18 18/13*"));
    t.steps.clear();
    CHECK_EQ(turnNotation(t), std::string("6-5 şeşbeş: oynayamadı"));

    // the game's record: whole turns as they end, undone steps forgotten, the winning turn too
    Game g = moving(Position::initial(), 0, 6, 5);
    CHECK(g.gameLog().empty());
    CHECK(g.applyStep(0, 23, 17).ok);
    CHECK(g.undoStep(0).ok);
    CHECK(g.applyStep(0, 23, 17).ok);
    CHECK(g.applyStep(0, 17, 12).ok);
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.gameLog().size(), 1);
    CHECK_EQ(turnNotation(g.gameLog()[0]), std::string("6-5 şeşbeş: 24/18 18/13"));
    // no move: recorded as such
    Game n = moving(PB().bar(0, 1).at(1, 18, 2).at(1, 19, 2).at(1, 20, 2).at(1, 21, 2).at(1, 22, 2).at(1, 23, 2).at(0, 3, 14).fill(),
                    0, 1, 1);
    n.debugSetTurn(0, Stage::NeedRoll);
    n.debugQueueDice(6, 6);
    CHECK(n.roll(0).ok);
    CHECK_EQ((int)n.gameLog().size(), 1);
    CHECK_EQ(turnNotation(n.gameLog()[0]), std::string("6-6 düşeş: oynayamadı"));
    // the winning turn ends the game and stays in the record
    Game w = moving(PB().at(0, 0, 1).at(1, 12, 15).fill(), 0, 1, 2);
    CHECK(w.applyStep(0, 0, OFF).ok);
    CHECK_EQ((int)w.gameLog().size(), 1);
    CHECK_EQ(turnNotation(w.gameLog()[0]), std::string("2-1 yeki dü: 1/çıktı"));
    // a new game starts a new record
    w.startNextGame();
    CHECK(w.gameLog().empty());
}

void testBotCube() {
    Rules r;
    r.doubling = true;
    r.matchPoints = 1000; // money-like: no score to protect
    // a race, 105 pips against 117 (about 84%): Usta and Kurt double; that far behind they drop
    const Position race = PB().at(0, 5, 5).at(0, 6, 5).at(0, 7, 5).at(1, 18, 1).at(1, 17, 5).at(1, 16, 5).at(1, 15, 4).fill();
    for (BotLevel lv : {BotLevel::Normal, BotLevel::Hard}) {
        Game g = moving(race, 0, 1, 2, r);
        g.debugSetTurn(0, Stage::NeedRoll);
        Bot me(lv, 1), him(lv, 2);
        CHECK(me.next(g, 0).kind == BotAction::Kind::Double);
        CHECK(g.offerDouble(0).ok);
        CHECK(him.next(g, 1).kind == BotAction::Kind::Drop);
    }
    CHECK(botWinProbability(race, 0) > 0.8 && botWinProbability(race, 0) < 0.9);
    // far too good (75 against 105, the other has nothing off): play on for the mars
    {
        const Position tooGood = PB().at(0, 2, 5).at(0, 3, 5).at(0, 4, 5).at(1, 18, 5).at(1, 17, 5).at(1, 16, 5).fill();
        CHECK(botWinProbability(tooGood, 0) > 0.98);
        Game g = moving(tooGood, 0, 1, 2, r);
        g.debugSetTurn(0, Stage::NeedRoll);
        Bot kurt(BotLevel::Hard, 1);
        CHECK(kurt.next(g, 0).kind == BotAction::Kind::Roll);
    }
    // the start position: nobody doubles, a double is taken
    for (BotLevel lv : {BotLevel::Normal, BotLevel::Hard}) {
        Game g = moving(Position::initial(), 0, 1, 2, r);
        g.debugSetTurn(0, Stage::NeedRoll);
        Bot me(lv, 1), him(lv, 2);
        CHECK(me.next(g, 0).kind == BotAction::Kind::Roll);
        CHECK(g.offerDouble(0).ok);
        CHECK(him.next(g, 1).kind == BotAction::Kind::Take);
    }
    const double p0 = botWinProbability(Position::initial(), 0);
    CHECK(p0 > 0.5 && p0 < 0.6); // being on roll is worth a little
    // a closer race (105 against 111, about 74%): a double, and it is taken
    {
        const Position close = PB().at(0, 5, 5).at(0, 6, 5).at(0, 7, 5).at(1, 18, 3).at(1, 17, 5).at(1, 16, 5).at(1, 15, 2).fill();
        const double pc = botWinProbability(close, 0);
        CHECK(pc > 0.68 && pc < 0.78);
        Game g = moving(close, 0, 1, 2, r);
        g.debugSetTurn(0, Stage::NeedRoll);
        CHECK(g.offerDouble(0).ok);
        Bot him(BotLevel::Hard, 2);
        CHECK(him.next(g, 1).kind == BotAction::Kind::Take);
    }
    // match score (3 points): Mahmut 2 - 0 (mars), the Crawford game goes to Sen (1-2), then: the trailer doubles
    // at once, the leader never doubles (one point wins him the match), and he takes anything (a drop loses it)
    {
        Rules m;
        m.doubling = true;
        m.matchPoints = 3;
        Game g(m);
        g.setPlayer(0, "Sen", true);
        g.setPlayer(1, "Kel Mahmut", false);
        g.startMatch(5);
        g.debugSetPosition(PB().at(1, 23, 1).at(0, 12, 15).fill());
        g.debugSetTurn(1, Stage::Moving);
        g.debugSetDice(1, 2);
        CHECK(g.applyStep(1, 23, OFF).ok); // 0-2
        g.startNextGame();                  // Crawford
        CHECK(g.crawfordGame());
        g.debugSetPosition(PB().at(0, 0, 1).at(1, 20, 5).fill());
        g.debugSetTurn(0, Stage::Moving);
        g.debugSetDice(4, 2);
        CHECK(g.applyStep(0, 0, OFF).ok);  // 1-2
        g.startNextGame();                  // post-Crawford
        CHECK(!g.crawfordGame());
        g.debugSetTurn(0, Stage::NeedRoll);
        Bot kurt(BotLevel::Hard, 7), usta(BotLevel::Normal, 8);
        CHECK(kurt.next(g, 0).kind == BotAction::Kind::Double); // even from the start position
        // Mahmut in a won race: no double (dead cube)
        const Position won = PB().at(1, 18, 5).at(1, 17, 5).at(1, 16, 5).at(0, 5, 1).at(0, 6, 5).at(0, 7, 5).at(0, 8, 4).fill();
        g.debugSetPosition(won);
        g.debugSetTurn(1, Stage::NeedRoll);
        CHECK(g.canDouble(1));
        CHECK(kurt.next(g, 1).kind == BotAction::Kind::Roll);
        CHECK(usta.next(g, 1).kind == BotAction::Kind::Roll);
        // a hopeless race for Sen, but dropping would lose the match (2 + 1 >= 3): free take
        CHECK(g.offerDouble(1).ok);
        CHECK(kurt.next(g, 0).kind == BotAction::Kind::Take);
        CHECK(usta.next(g, 0).kind == BotAction::Kind::Take);
    }
}

void testReplay() {
    // Bots play matches (with and without the cube, an undo now and then); at many points the match is rebuilt
    // from the seed plus the action log written as text: the same position, dice, scores, cube, record and stage.
    for (int n = 0; n < 24; ++n) {
        Rules r;
        r.matchPoints = 3 + n % 3;
        r.doubling = n % 2 == 0;
        r.confirmTurn = n % 4 == 1;
        r.katmerliMars = n % 5 == 0;
        const uint64_t seed = 500 + (uint64_t)n;
        Game g(r);
        g.setPlayer(0, "Sen", true);
        g.setPlayer(1, "Kel Mahmut", false);
        g.startMatch(seed);
        Bot a((BotLevel)(n % 3), 1), b(BotLevel::Hard, 2);
        okey::Rng rng(seed);
        int guard = 0, checksDone = 0;
        auto compare = [&]() {
            std::vector<std::string> lines;
            for (const LoggedAction& x : g.actionLog()) lines.push_back(x.encode());
            Game h(r);
            h.setPlayer(0, "Sen", true);
            h.setPlayer(1, "Kel Mahmut", false);
            h.startMatch(seed);
            bool ok = true;
            for (const std::string& ln : lines) {
                LoggedAction x;
                ok = ok && LoggedAction::decode(ln, x) && x.encode() == ln && h.replay(x);
            }
            CHECK(ok);
            CHECK(h.position() == g.position());
            CHECK_EQ(h.stage(), g.stage());
            CHECK_EQ(h.current(), g.current());
            CHECK_EQ(h.score(0), g.score(0));
            CHECK_EQ(h.score(1), g.score(1));
            CHECK_EQ(h.cubeValue(), g.cubeValue());
            CHECK_EQ(h.cubeOwner(), g.cubeOwner());
            CHECK_EQ(h.gameIndex(), g.gameIndex());
            CHECK_EQ(h.dice().leftCount(), g.dice().leftCount());
            CHECK_EQ(h.dice().d1, g.dice().d1);
            CHECK_EQ(h.gameLog().size(), g.gameLog().size());
            CHECK_EQ(h.actionLog().size(), g.actionLog().size());
            // and both go on alike: the next roll is the same
            if (g.stage() == Stage::NeedRoll) {
                Game g2 = g;
                CHECK(g2.roll(g2.current()).ok && h.roll(h.current()).ok);
                CHECK_EQ(g2.dice().d1 * 10 + g2.dice().d2, h.dice().d1 * 10 + h.dice().d2);
            }
            ++checksDone;
        };
        while (g.stage() != Stage::MatchOver && guard++ < 20000) {
            g.drainEvents();
            if (rng.chance(0.03f)) compare();
            if (g.stage() == Stage::GameOver) {
                g.startNextGame();
                continue;
            }
            const int p = g.stage() == Stage::OpeningRoll ? 0 : (g.responder() >= 0 ? g.responder() : g.current());
            if (g.canUndo() && g.current() == p && rng.chance(0.05f)) {
                CHECK(g.undoStep(p).ok);
                continue;
            }
            const BotAction act = (p == 0 ? a : b).next(g, p);
            if (!applyBotAction(g, p, act).ok) applyBotAction(g, p, fallbackAction(g, p));
        }
        compare();
        CHECK(checksDone > 1);
    }
    // bad lines
    LoggedAction x;
    CHECK(!LoggedAction::decode("", x));
    CHECK(!LoggedAction::decode("2 0 23 17", x));
    CHECK(!LoggedAction::decode("1 0 x", x));
    CHECK(!LoggedAction::decode("99 0", x));
    CHECK(!LoggedAction::decode("1 5", x));
    CHECK(LoggedAction::decode("2 1 0 6 6", x) && x.kind == ActKind::Step && x.from == 0 && x.to == 6 && x.die == 6);
    CHECK(LoggedAction::decode("8 -1", x) && x.kind == ActKind::NextGame);
    // a step that does not apply is refused
    Game g;
    g.startMatch(1);
    CHECK(!g.replay(x));
    LoggedAction st;
    st.kind = ActKind::Step;
    st.player = 0;
    st.from = 3;
    st.to = 1;
    st.die = 2;
    CHECK(!g.replay(st));
}

void testBots() {
    // Every level plays complete matches against every level with zero rejected actions.
    for (int la = 0; la < 3; ++la)
        for (int lb = 0; lb < 3; ++lb) {
            Rules r;
            r.confirmTurn = (la + lb) % 2 == 1;
            r.doubling = (la * 3 + lb) % 2 == 0; // and with the cube
            Game g(r);
            g.setPlayer(0, "A", false);
            g.setPlayer(1, "B", false);
            g.startMatch(100 + (uint64_t)(la * 3 + lb));
            Bot a((BotLevel)la, 1), b((BotLevel)lb, 2);
            int rejected = 0, guard = 0;
            while (g.stage() != Stage::MatchOver && guard++ < 50000) {
                g.drainEvents();
                if (g.stage() == Stage::GameOver) {
                    g.startNextGame();
                    a.resetForGame();
                    b.resetForGame();
                    continue;
                }
                const int p = g.stage() == Stage::OpeningRoll ? 0 : (g.responder() >= 0 ? g.responder() : g.current());
                const BotAction act = (p == 0 ? a : b).next(g, p);
                if (!applyBotAction(g, p, act).ok) {
                    ++rejected;
                    applyBotAction(g, p, fallbackAction(g, p));
                }
            }
            CHECK_EQ(rejected, 0);
            CHECK_EQ(g.stage(), Stage::MatchOver);
        }

    // Plan recovery: the caller undoes the bot's step (confirmTurn) -> the bot re-plans, no rejection.
    {
        Rules r;
        r.confirmTurn = true;
        Game g = moving(Position::initial(), 1, 6, 4, r);
        Bot bot(BotLevel::Hard, 3);
        BotAction a = bot.next(g, 1);
        CHECK(a.kind == BotAction::Kind::Step);
        CHECK(applyBotAction(g, 1, a).ok);
        CHECK(g.undoStep(1).ok);
        int guard = 0;
        while (g.current() == 1 && guard++ < 10) {
            a = bot.next(g, 1);
            CHECK(a.kind == BotAction::Kind::Step || a.kind == BotAction::Kind::EndTurn);
            CHECK(applyBotAction(g, 1, a).ok);
        }
        CHECK_EQ(g.current(), 0);
        // and a bot asked out of turn does nothing
        CHECK(bot.next(g, 1).kind == BotAction::Kind::None);
    }

    // Sanity: with 3-1 from the start Usta and Kurt make their 5-point (8/5 6/5), as every kahvehane player does.
    {
        Game g = moving(Position::initial(), 0, 3, 1);
        for (BotLevel lv : {BotLevel::Normal, BotLevel::Hard}) {
            Game t = g;
            Bot bot(lv, 9);
            int guard = 0;
            while (t.current() == 0 && guard++ < 5) applyBotAction(t, 0, bot.next(t, 0));
            CHECK_EQ((int)t.position().pts[4], 2); // the 5-point is made
        }
    }

    // Fallback is always legal.
    {
        Game g = moving(Position::initial(), 0, 5, 2);
        BotAction f = fallbackAction(g, 0);
        CHECK(f.kind == BotAction::Kind::Step);
        CHECK(applyBotAction(g, 0, f).ok);
        CHECK(fallbackAction(g, 1).kind == BotAction::Kind::None);
    }

    // Kurt's decision time on a few heavy positions (double rolls in an open middle game).
    {
        okey::Rng rng(5);
        double worst = 0;
        for (int n = 0; n < 40; ++n) {
            const Position pos = randomPosition(rng);
            if (total(pos, 0) != 15 || total(pos, 1) != 15) continue;
            const int d = 1 + n % 6;
            Game g = moving(pos, 0, d, d);
            if (g.legalSteps().empty()) continue;
            Bot bot(BotLevel::Hard, 1);
            const auto t0 = std::chrono::steady_clock::now();
            bot.next(g, 0);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms);
        }
        std::printf("  Kurt worst decision on random double positions: %.1f ms\n", worst);
    }
}


// ---------------------------------------------------------------------------------------------------------
// Çeşitler: Gülbahar and Fevga (Rules::variant)

// Game at Moving for p in a çeşit (as moving()).
Game movingV(Variant v, const Position& pos, int p, int d1, int d2, Rules r = Rules()) {
    r.variant = v;
    return moving(pos, p, d1, d2, r);
}

namespace refsw {
// An independent model of the same-way rules: each side walks its own path of 24 places, k = 0 its start point,
// k = 23 its last home point; k + d >= 24 bears off. Player 0's path is index 23 - k, player 1's (11 - k) mod 24.
int idx(int p, int k) { return p == 0 ? 23 - k : (11 - k + 24) % 24; }
int own(const Position& pos, int p, int i) { return ref::own(pos, p, i); }

bool trapsAll(const Position& q, int p) { // p holds 6 in a row on the opponent's path with all of his behind
    const int o = 1 - p;
    if (q.off[o] > 0) return false;
    int minK = 99;
    for (int k = 0; k < 24; ++k)
        if (own(q, o, idx(o, k))) minK = std::min(minK, k);
    (void)minK;
    for (int a = 0; a + 6 <= 24; ++a) {
        bool run = true;
        for (int k = a; k < a + 6; ++k) run = run && own(q, p, idx(o, k)) > 0;
        if (!run) continue;
        bool ahead = false;
        for (int k = a + 6; k < 24; ++k) ahead = ahead || own(q, o, idx(o, k)) > 0;
        if (!ahead) return true;
    }
    return false;
}

std::vector<Step> singles(const Position& pos, int p, int d, Variant v) {
    std::vector<Step> out;
    std::vector<int> mine; // path places of p's checkers
    for (int k = 0; k < 24; ++k)
        if (own(pos, p, idx(p, k))) mine.push_back(k);
    bool allHome = true;
    for (int k : mine) allHome = allHome && k >= 18;
    int onlyK = -1;
    if (v == Variant::Fevga && own(pos, p, idx(p, 0)) == 14) {
        for (int k : mine)
            if (k > 0 && k <= 12) onlyK = k; // not yet past the opponent's start (path place 12)
    }
    for (int k : mine) {
        if (onlyK >= 0 && k != onlyK) continue;
        const int i = idx(p, k);
        if (k + d < 24) {
            const int t = idx(p, k + d);
            if (own(pos, 1 - p, t)) continue;
            if (v == Variant::Fevga && own(pos, p, t) == 0) {
                Position q = pos;
                q.pts[i] = (int8_t)(q.pts[i] - (p == 0 ? 1 : -1));
                q.pts[t] = (int8_t)(q.pts[t] + (p == 0 ? 1 : -1));
                if (trapsAll(q, p) && !trapsAll(pos, p)) continue;
            }
            out.push_back(Step{i, t, d, false});
            continue;
        }
        if (!allHome) continue;
        bool farther = false;
        for (int j : mine) farther = farther || j < k;
        if (k + d == 24 || !farther) out.push_back(Step{i, OFF, d, false});
    }
    return out;
}

void rec(const Position& pos, int p, std::vector<int> dice, std::vector<Step>& path, std::vector<ref::Seq>& out, Variant v) {
    bool any = false;
    std::set<int> tried;
    for (size_t k = 0; k < dice.size(); ++k) {
        if (!tried.insert(dice[k]).second) continue;
        std::vector<int> rest = dice;
        rest.erase(rest.begin() + (long)k);
        for (const Step& s : singles(pos, p, dice[k], v)) {
            any = true;
            Position q = pos;
            ref::apply(q, p, s);
            path.push_back(s);
            if (q.off[p] == 15) out.push_back(ref::Seq{path, q, true});
            else rec(q, p, rest, path, out, v);
            path.pop_back();
        }
    }
    if (!any) out.push_back(ref::Seq{path, pos, false});
}

std::set<std::string> maximalEnds(const Position& pos, int p, int d1, int d2, Variant v, std::set<StepKey>& first) {
    std::vector<int> dice = d1 == d2 ? std::vector<int>{d1, d1, d1, d1} : std::vector<int>{d1, d2};
    std::vector<ref::Seq> all;
    std::vector<Step> path;
    rec(pos, p, dice, path, all, v);
    size_t mx = 0;
    for (const ref::Seq& s : all) mx = std::max(mx, s.won ? dice.size() : s.steps.size());
    std::set<std::string> ends;
    first.clear();
    if (mx == 0) return ends;
    const int big = std::max(d1, d2);
    bool bigOk = false;
    if (mx == 1 && d1 != d2)
        for (const ref::Seq& s : all) bigOk = bigOk || (s.steps.size() == 1 && s.steps[0].die == big);
    for (const ref::Seq& s : all) {
        if (!s.won && s.steps.size() != mx) continue;
        if (bigOk && s.steps[0].die != big) continue;
        ends.insert(posKey(s.end));
        first.insert({s.steps[0].from, s.steps[0].to, s.steps[0].die});
    }
    return ends;
}
} // namespace refsw

// Random same-way position: no shared points, no bar; some with 14 still on the start (Fevga's first checker), some
// races at home, some crowded (near primes).
Position randomSameWay(okey::Rng& rng, Variant v) {
    Position pos;
    const int mode = rng.range(4);
    for (int p = 0; p < 2; ++p) {
        int left = kCheckers;
        if (mode == 1) {
            const int off = rng.range(0, 10);
            pos.off[p] = (int8_t)off;
            left -= off;
        }
        if (mode == 2 && rng.chance(0.7f)) { // the start pile and one checker out
            pos.pts[startPoint(v, p)] = (int8_t)(p == 0 ? 14 : -14);
            left = 1;
        }
        int guard = 0;
        while (left > 0 && guard++ < 1000) {
            const int r = mode == 1 ? rng.range(0, 5) : rng.range(0, 23);
            const int i = absPoint(v, p, r);
            if (pos.owner(i) == 1 - p) continue;
            const int n = std::min(left, mode == 3 ? 1 : rng.range(1, 3));
            pos.pts[i] = (int8_t)(pos.pts[i] + (p == 0 ? n : -n));
            left -= n;
        }
        if (left > 0) pos.off[p] = (int8_t)(pos.off[p] + left);
        if (pos.off[p] == kCheckers) { // not a finished game
            for (int r = 0; r < 24; ++r) {
                const int i = absPoint(v, p, r);
                if (pos.owner(i) == 1 - p) continue;
                pos.pts[i] = (int8_t)(pos.pts[i] + (p == 0 ? 1 : -1));
                pos.off[p] = 14;
                break;
            }
        }
    }
    return pos;
}

void testVariantStart() {
    for (Variant v : {Variant::Gulbahar, Variant::Fevga}) {
        const Position pos = Position::initial(v);
        CHECK_EQ((int)pos.pts[23], 15);
        CHECK_EQ((int)pos.pts[11], -15);
        CHECK_EQ(pipCount(pos, 0, v), 360);
        CHECK_EQ(pipCount(pos, 1, v), 360);
        CHECK_EQ(startPoint(v, 0), 23);
        CHECK_EQ(startPoint(v, 1), 11);
        CHECK_EQ(pointNumber(v, 1, 11), 24);
        CHECK_EQ(pointNumber(v, 1, 12), 1);
        CHECK_EQ(pointNumber(v, 1, 0), 13);
        CHECK_EQ(pointNumber(v, 1, 23), 12);
        CHECK_EQ(pointNumber(v, 0, 5), 6);
        CHECK(inHome(v, 1, 12) && inHome(v, 1, 17) && !inHome(v, 1, 18) && !inHome(v, 1, 11));
        Rules r;
        r.variant = v;
        Game g(r);
        g.startMatch(3);
        CHECK(g.position() == pos);
        CHECK(g.variant() == v);
        CHECK_EQ(g.pipCount(1), 360);
    }
    // klasik is untouched
    CHECK(Position::initial(Variant::Klasik) == Position::initial());
    CHECK_EQ(pointNumber(Variant::Klasik, 1, 18), pointNumber(1, 18));
    CHECK_EQ(std::string(variantName(Variant::Gulbahar)), std::string("Gülbahar"));
    // notation in the mover's numbering: Fevga, player 1 from his start (index 11) to index 5
    CHECK_EQ(stepNotation(1, Step{11, 5, 6, false}, Variant::Fevga), std::string("24/18"));
    CHECK_EQ(stepNotation(1, Step{13, OFF, 2, false}, Variant::Fevga), std::string("2/çıktı"));
}

void testVariantRules() {
    // no hitting: a single opposing checker holds its point (both çeşitler)
    for (Variant v : {Variant::Gulbahar, Variant::Fevga}) {
        const Position pos = PB().at(0, 23, 10).at(0, 20, 5).at(1, 17, 1).at(1, 11, 14).fill();
        Game g = movingV(v, pos, 0, 6, 3);
        for (const Step& s : g.legalSteps()) {
            CHECK(s.to != 17);
            CHECK(!s.hit);
        }
        const ActionResult r = g.applyStep(0, 23, 17, 6);
        CHECK(!r.ok);
        CHECK(r.error.find("tek pul") != std::string::npos);
        // ... and it is no stop on the way either: 20 -> 17 -> 14 with 3-3 is closed at 17
        Game d = movingV(v, pos, 0, 3, 3);
        for (const Step& s : d.legalSteps()) CHECK(!(s.from == 20 && s.to == 17));
    }
    // Fevga: the first checker out must pass the opponent's start point before a second leaves the corner
    {
        Game g = movingV(Variant::Fevga, Position::initial(Variant::Fevga), 0, 6, 5);
        const std::vector<Play> plays = g.allTurnPlays();
        CHECK_EQ(plays.size(), (size_t)1); // one checker 11 pips: 24/13
        CHECK_EQ((int)plays[0].result.pts[23], 14);
        CHECK_EQ((int)plays[0].result.pts[12], 1);
        CHECK(g.applyStep(0, 23, 17, 6).ok);
        CHECK(!g.applyStep(0, 23, 18, 5).ok); // a second checker may not leave yet
        Game h = movingV(Variant::Fevga, Position::initial(Variant::Fevga), 0, 6, 5);
        CHECK(h.applyStep(0, 23, 17, 6).ok);
        const ActionResult r = h.applyStep(0, 23, 18, 5);
        CHECK(!r.ok && r.error.find("başlangıç") != std::string::npos);
        // Gülbahar has no such rule: two checkers may leave
        Game b = movingV(Variant::Gulbahar, Position::initial(Variant::Gulbahar), 0, 6, 5);
        CHECK_EQ(b.allTurnPlays().size(), (size_t)2); // 24/13 (24/18/13 = 24/19/13), 24/18 24/19
    }
    {
        // runner on 14 (index 13), past nothing yet; 3-1: the 3 takes it past the opponent's start (index 11) to
        // index 10; then the 1 is free for any checker
        const Position pos = PB().at(0, 23, 14).at(0, 13, 1).at(1, 11, 15).fill();
        Game g = movingV(Variant::Fevga, pos, 0, 3, 1);
        std::set<int> from;
        for (const Step& s : g.legalSteps()) from.insert(s.from);
        CHECK(from == std::set<int>{13});
        CHECK(g.applyStep(0, 13, 10, 3).ok);
        std::set<int> from2;
        for (const Step& s : g.legalSteps()) from2.insert(s.from);
        CHECK(from2.count(23) == 1 && from2.count(10) == 1);
        // the runner blocked: nothing at all can be played
        const Position stuck = PB().at(0, 23, 14).at(0, 13, 1).at(1, 11, 9).at(1, 12, 1).at(1, 10, 1).at(1, 9, 1).at(1, 8, 1).at(1, 7, 1).at(1, 2, 1).fill();
        Game s = movingV(Variant::Fevga, stuck, 0, 2, 1);
        CHECK(s.allTurnPlays().empty());
    }
    // Fevga: no 6-prime with all opposing checkers behind it (in front of his start pile: indices 10..5)
    {
        const Position pos = PB().at(0, 23, 9).at(0, 10, 1).at(0, 9, 1).at(0, 8, 1).at(0, 7, 2).at(0, 6, 1).at(1, 11, 15).fill();
        Game g = movingV(Variant::Fevga, pos, 0, 2, 1);
        for (const Step& s : g.legalSteps()) CHECK(!(s.from == 7 && s.to == 5)); // 10..5 all held: illegal
        const ActionResult r = g.applyStep(0, 7, 5, 2);
        CHECK(!r.ok && r.error.find("Altı") != std::string::npos);
        CHECK(g.applyStep(0, 6, 4, 2).ok); // (moving the 6-point's single checker keeps the run short)
        // the same in Gülbahar is allowed
        Game b = movingV(Variant::Gulbahar, pos, 0, 2, 1);
        CHECK(b.applyStep(0, 7, 5, 2).ok);
        // and in Fevga once one of his checkers is past the run
        const Position past = PB().at(0, 23, 9).at(0, 10, 1).at(0, 9, 1).at(0, 8, 1).at(0, 7, 2).at(0, 6, 1).at(1, 11, 14).at(1, 3, 1).fill();
        Game f = movingV(Variant::Fevga, past, 0, 2, 1);
        CHECK(f.applyStep(0, 7, 5, 2).ok);
    }
    // bearing off and mars: no katmerli in the çeşitler (even with the rule on and his checker in my home)
    for (Variant v : {Variant::Gulbahar, Variant::Fevga}) {
        Rules r;
        r.katmerliMars = true;
        const Position pos = PB().at(0, 0, 1).at(1, 3, 5).at(1, 11, 10).fill();
        Game g = movingV(v, pos, 0, 2, 1, r);
        CHECK(g.applyStep(0, 0, OFF).ok);
        CHECK(g.stage() == Stage::GameOver || g.stage() == Stage::MatchOver);
        CHECK(g.lastResult().mars);
        CHECK(!g.lastResult().katmerli);
        CHECK_EQ(g.lastResult().points, 2);
        // player 1 bears off past index 12, from his home 12..17
        const Position p1 = PB().at(1, 13, 1).at(1, 15, 1).at(0, 20, 3).fill();
        Game h = movingV(v, p1, 1, 4, 2);
        CHECK(h.applyStep(1, 15, OFF, 4).ok);
        CHECK(h.applyStep(1, 13, OFF, 2).ok);
        CHECK(h.lastResult().winner == 1);
    }
}

// Plays the current player's moves by the first legal step until the turn passes (or the game ends).
void playOut(Game& g) {
    const int p = g.current();
    int guard = 0;
    while (g.stage() == Stage::Moving && g.current() == p && guard++ < 64) {
        const std::vector<Step> st = g.legalSteps();
        if (st.empty()) {
            CHECK(g.endTurn(p).ok);
            break;
        }
        CHECK(g.applyStep(p, st[0].from, st[0].to, st[0].die).ok);
    }
}

void testGulbaharLadder() {
    Rules r;
    r.variant = Variant::Gulbahar;
    Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(9);
    g.debugQueueDice(5, 2); // opening: player 0 starts
    CHECK(g.rollOpening().ok);
    const int seq[][2] = {{2, 1}, {2, 2}, {4, 1}, {4, 1}, {3, 3}, {5, 1}};
    for (const auto& d : seq) { // p0: 2-1, 4-1, 3-3 (his 3rd roll: a plain double); p1: 2-2, 4-1, 5-1
        g.debugQueueDice(d[0], d[1]);
        CHECK(g.roll(g.current()).ok);
        CHECK_EQ(g.ladder(), 0);
        playOut(g);
    }
    CHECK_EQ(g.rollsInGame(0), 3);
    CHECK_EQ(g.rollsInGame(1), 3);
    CHECK_EQ(g.current(), 0);
    // an open board for the 4th roll: 3-3 climbs 3, 4, 5, 6
    g.debugSetPosition(PB().at(0, 23, 15).at(1, 13, 1).fill());
    g.drainEvents();
    g.debugQueueDice(3, 3);
    CHECK(g.roll(0).ok);
    CHECK_EQ(g.ladder(), 3);
    const size_t logBefore = g.gameLog().size();
    int rungs = 0;
    for (int rung = 3; rung <= 6; ++rung) {
        CHECK_EQ(g.current(), 0);
        CHECK_EQ(g.dice().d1, rung);
        CHECK_EQ(g.dice().leftCount(), 4);
        for (int k = 0; k < 4; ++k) {
            const std::vector<Step> st = g.legalSteps();
            CHECK(!st.empty());
            if (st.empty()) break;
            CHECK(g.applyStep(0, st[0].from, st[0].to, st[0].die).ok);
        }
        ++rungs;
        if (rung < 6) {
            bool turned = false;
            for (const GameEvent& e : g.drainEvents())
                if (e.type == EvType::Roll && e.amount == 1 && e.d1 == rung + 1 && e.d2 == rung + 1) turned = true;
            CHECK(turned);
            CHECK(!g.canUndo()); // a rung played is final
        }
    }
    CHECK_EQ(rungs, 4);
    CHECK_EQ(g.current(), 1); // after 6-6 the turn passes
    CHECK_EQ(g.ladder(), 0);
    CHECK_EQ(g.gameLog().size() - logBefore, (size_t)4); // one record line per rung
    CHECK_EQ(pipCount(g.position(), 0, Variant::Gulbahar), 15 * 24 - 4 * (3 + 4 + 5 + 6));
    CHECK(turnNotation(g.gameLog().back()).rfind("6-6 düşeş:", 0) == 0);

    // a rung that cannot be played in full ends the ladder (his single checker on index 15 holds that point)
    g.debugSetPosition(PB().at(0, 23, 1).at(1, 15, 1).fill());
    g.debugSetTurn(0, Stage::NeedRoll);
    g.debugQueueDice(1, 1);
    CHECK(g.roll(0).ok);
    CHECK_EQ(g.ladder(), 1);
    playOut(g);
    CHECK_EQ(g.current(), 1);
    CHECK_EQ((int)g.position().pts[17], 1); // the 1s: 24/20; the 2s: 20/18, then 18/16 is his point: the ladder ends
}

void testVariantAgainstReference() {
    for (Variant v : {Variant::Gulbahar, Variant::Fevga}) {
        okey::Rng rng(v == Variant::Fevga ? 99 : 98);
        int compared = 0;
        for (int n = 0; n < 4000; ++n) {
            const Position pos = randomSameWay(rng, v);
            if (total(pos, 0) != 15 || total(pos, 1) != 15) continue;
            const int p = rng.range(2);
            const int d1 = rng.range(1, 6), d2 = rng.range(1, 6);
            // (a 6-run already trapping all of his checkers never arises in play: no step may make one)
            if (v == Variant::Fevga && (refsw::trapsAll(pos, 0) || refsw::trapsAll(pos, 1))) continue;
            std::set<StepKey> refFirst;
            const std::set<std::string> refEnds = refsw::maximalEnds(pos, p, d1, d2, v, refFirst);
            const std::vector<Play> plays = generatePlays(pos, p, d1, d2, v);
            std::set<std::string> ends;
            for (const Play& pl : plays) ends.insert(posKey(pl.result));
            CHECK_EQ(ends.size(), plays.size());
            if (ends != refEnds) {
                reportFailure(__FILE__, __LINE__, std::string(variantName(v)) + ": generatePlays != reference (case " + std::to_string(n) + ")");
                continue;
            }
            Game g = movingV(v, pos, p, d1, d2);
            if (keys(g.legalSteps()) != refFirst) {
                reportFailure(__FILE__, __LINE__, std::string(variantName(v)) + ": legalSteps != reference (case " + std::to_string(n) + ")");
                continue;
            }
            for (const Play& pl : plays) {
                Game t = g;
                bool ok = true;
                for (const Step& s : pl.steps) ok = ok && t.applyStep(p, s.from, s.to, s.die).ok;
                CHECK(ok);
                CHECK(t.position() == pl.result);
            }
            ++compared;
        }
        CHECK(compared > 3500);
        std::printf("  %s reference cross-check: %d random positions\n", variantName(v), compared);
    }
}

void testVariantFuzzAndReplay() {
    okey::Rng rng(4242);
    int games = 0, ladders = 0;
    for (int n = 0; n < 600; ++n) {
        Rules r;
        r.variant = n % 2 ? Variant::Fevga : Variant::Gulbahar;
        r.matchPoints = 1 + rng.range(3);
        r.confirmTurn = rng.chance(0.3f);
        r.doubling = rng.chance(0.4f);
        r.openingReroll = rng.chance(0.7f);
        Game g(r);
        g.startMatch(7000 + (uint64_t)n);
        int guard = 0;
        while (g.stage() != Stage::MatchOver && guard++ < 30000) {
            const Position& pos = g.position();
            for (int p = 0; p < 2; ++p) {
                CHECK_EQ(total(pos, p), 15);
                CHECK_EQ((int)pos.bar[p], 0);
            }
            for (const GameEvent& e : g.drainEvents()) {
                CHECK(e.type != EvType::Hit);
                ladders += e.type == EvType::Roll && e.amount == 1;
            }
            if (g.ladder() > 0) CHECK(r.variant == Variant::Gulbahar && g.dice().d1 == g.ladder() && g.dice().d2 == g.ladder());
            switch (g.stage()) {
            case Stage::OpeningRoll: CHECK(g.rollOpening().ok); break;
            case Stage::NeedRoll:
                if (g.canDouble(g.current()) && rng.chance(0.05f)) CHECK(g.offerDouble(g.current()).ok);
                else CHECK(g.roll(g.current()).ok);
                break;
            case Stage::DoubleOffered: CHECK(g.acceptDouble(g.responder()).ok); break;
            case Stage::Moving: {
                const int p = g.current();
                const std::vector<Step> st = g.legalSteps();
                if (st.empty()) {
                    CHECK(g.endTurn(p).ok);
                    break;
                }
                if (g.canUndo() && rng.chance(0.05f)) {
                    CHECK(g.undoStep(p).ok);
                    break;
                }
                const Step s = st[(size_t)rng.range((int)st.size())];
                CHECK(g.applyStep(p, s.from, s.to, s.die).ok);
                break;
            }
            case Stage::GameOver:
                ++games;
                CHECK(!g.lastResult().katmerli);
                g.startNextGame();
                break;
            default: break;
            }
        }
        CHECK_EQ(g.stage(), Stage::MatchOver);
        ++games;
        // the action log rebuilds the match exactly
        if (n % 5 == 0) {
            Game h(r);
            h.startMatch(7000 + (uint64_t)n);
            bool ok = true;
            for (const LoggedAction& a : g.actionLog()) {
                LoggedAction x;
                ok = ok && LoggedAction::decode(a.encode(), x) && h.replay(x);
            }
            CHECK(ok);
            CHECK(h.position() == g.position());
            CHECK_EQ(h.score(0), g.score(0));
            CHECK_EQ(h.score(1), g.score(1));
        }
    }
    CHECK(ladders > 50);
    std::printf("  çeşitler fuzz: %d games, %d ladder rungs\n", games, ladders);

    // bots in every çeşit and level: complete matches, nothing rejected; replays from the log mid-match
    for (Variant v : {Variant::Gulbahar, Variant::Fevga})
        for (int la = 0; la < 3; ++la) {
            Rules r;
            r.variant = v;
            r.matchPoints = 3;
            r.doubling = la != 1;
            r.confirmTurn = la == 2;
            Game g(r);
            g.setPlayer(0, "Sen", true);
            g.setPlayer(1, "Kel Mahmut", false);
            g.startMatch(300 + (uint64_t)la);
            Bot a((BotLevel)la, 1), b(BotLevel::Hard, 2);
            int rejected = 0, guard = 0, compared = 0;
            while (g.stage() != Stage::MatchOver && guard++ < 50000) {
                g.drainEvents();
                if (g.stage() == Stage::GameOver) {
                    g.startNextGame();
                    continue;
                }
                if (guard % 97 == 0) {
                    Game h(r);
                    h.setPlayer(0, "Sen", true);
                    h.setPlayer(1, "Kel Mahmut", false);
                    h.startMatch(300 + (uint64_t)la);
                    bool ok = true;
                    for (const LoggedAction& x : g.actionLog()) ok = ok && h.replay(x);
                    CHECK(ok);
                    CHECK(h.position() == g.position());
                    CHECK_EQ(h.ladder(), g.ladder());
                    CHECK_EQ(h.dice().leftCount(), g.dice().leftCount());
                    CHECK_EQ(h.gameLog().size(), g.gameLog().size());
                    ++compared;
                }
                const int p = g.stage() == Stage::OpeningRoll ? 0 : (g.responder() >= 0 ? g.responder() : g.current());
                const BotAction act = (p == 0 ? a : b).next(g, p);
                if (!applyBotAction(g, p, act).ok) {
                    ++rejected;
                    applyBotAction(g, p, fallbackAction(g, p));
                }
            }
            CHECK_EQ(rejected, 0);
            CHECK_EQ(g.stage(), Stage::MatchOver);
            CHECK(compared > 0);
        }

    // Kurt's decision time on crowded same-way positions with doubles
    for (Variant v : {Variant::Gulbahar, Variant::Fevga}) {
        okey::Rng r2(6);
        double worst = 0;
        for (int n = 0; n < 60; ++n) {
            const Position pos = randomSameWay(r2, v);
            if (total(pos, 0) != 15 || total(pos, 1) != 15) continue;
            const int d = 1 + n % 6;
            Game g = movingV(v, pos, 0, d, d);
            if (g.legalSteps().empty()) continue;
            Bot bot(BotLevel::Hard, 1);
            const auto t0 = std::chrono::steady_clock::now();
            bot.next(g, 0);
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        CHECK(worst < 300.0);
        std::printf("  Kurt worst decision, %s doubles: %.1f ms\n", variantName(v), worst);
    }
}
} // namespace

// The shared Turkish text helpers (core/TurkishText.h) the engines' event texts use.
void testTurkishText() {
    using namespace trtext;
    CHECK_EQ(capitalizeFirst("ısmarladın"), std::string("Ismarladın"));
    CHECK_EQ(capitalizeFirst("iki pul kırdın"), std::string("İki pul kırdın"));
    CHECK_EQ(capitalizeFirst("çift attın"), std::string("Çift attın"));
    CHECK_EQ(capitalizeFirst("şeşbeş"), std::string("Şeşbeş"));
    CHECK_EQ(capitalizeFirst("ğ"), std::string("Ğ"));
    CHECK_EQ(capitalizeFirst("öndesin"), std::string("Öndesin"));
    CHECK_EQ(capitalizeFirst("üç"), std::string("Üç"));
    CHECK_EQ(capitalizeFirst("Kel"), std::string("Kel"));
    CHECK_EQ(capitalizeFirst("4 kart"), std::string("4 kart"));
    CHECK_EQ(capitalizeFirst(""), std::string(""));
    CHECK_EQ(upperTR("istanbul ılık çay, şöyle güzel"), std::string("İSTANBUL ILIK ÇAY, ŞÖYLE GÜZEL"));
    CHECK_EQ(lowerTR("İSTANBUL IŞIK ÇAĞ ÖĞÜN"), std::string("istanbul ışık çağ öğün"));
    CHECK_EQ(locative("Kel Mahmut"), std::string("Kel Mahmut'ta"));
    CHECK_EQ(locative("Hacı Rıza"), std::string("Hacı Rıza'da"));
    CHECK_EQ(locative("Emekli Nuri"), std::string("Emekli Nuri'de"));
    CHECK_EQ(locative("Yücel"), std::string("Yücel'de"));
    CHECK_EQ(locative("Ağaç"), std::string("Ağaç'ta"));
    CHECK_EQ(dative("Hacı Rıza"), std::string("Hacı Rıza'ya"));
    CHECK_EQ(dative("Kel Mahmut"), std::string("Kel Mahmut'a"));
    CHECK_EQ(dative("Emekli Nuri"), std::string("Emekli Nuri'ye"));
    CHECK_EQ(genitive("Kel Mahmut"), std::string("Kel Mahmut'un"));
    CHECK_EQ(genitive("Hacı Rıza"), std::string("Hacı Rıza'nın"));
    CHECK_EQ(genitive("Emekli Nuri"), std::string("Emekli Nuri'nin"));
    CHECK_EQ(genitive("Ömür"), std::string("Ömür'ün"));
    CHECK_EQ(genitive("ALİ"), std::string("ALİ'nin"));
    CHECK_EQ(genitive("ALI"), std::string("ALI'nın"));
}

int main() {
    testTurkishText();
    testStartPosition();
    testOpeningRoll();
    testRollAndDiceNames();
    testBarEntry();
    testHitting();
    testClosedPointsAndErrors();
    testMustUseBothAndLarger();
    testDoubles();
    testBearOff();
    testMarsAndMatch();
    testUndoAndConfirm();
    testAllTurnPlaysConsistency();
    testAgainstReference();
    testFuzz();
    testDeterminism();
    testDoubling();
    testHistory();
    testBotCube();
    testReplay();
    testBots();
    // çeşitler
    testVariantStart();
    testVariantRules();
    testGulbaharLadder();
    testVariantAgainstReference();
    testVariantFuzzAndReplay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
