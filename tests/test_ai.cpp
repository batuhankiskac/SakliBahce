// AI tests for SaklıBahçe: Solver against a brute-force oracle, rack arrangement, timing, and bot
// behaviour scenarios built with the Game debug hooks.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Meld.cpp src/core/Game.cpp src/core/Solver.cpp
//        src/core/Bot.cpp tests/test_ai.cpp -o build/ai/test_ai
#include "core/Bot.h"
#include "core/Game.h"
#include "core/Meld.h"
#include "core/Solver.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace okey;

namespace {

int g_checks = 0;
int g_failures = 0;

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    if (g_failures <= 40) std::printf("%s:%d: CHECK failed: %s\n", file, line, what.c_str());
    else if (g_failures == 41) std::printf("... further failures suppressed\n");
}

template <class T>
std::string show(const T& v) {
    std::ostringstream o;
    o << std::boolalpha << v;
    return o.str();
}

} // namespace

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, #cond);                                                   \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                                     \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, std::string(#cond) + "  -- " + (msg));                    \
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
using G = std::vector<std::vector<int>>;

constexpr int Y = Yellow, B = Blue, K = Black, R = Red;
int T(int color, int number, int copy = 0) { return makeTileId(color, number, copy); }

bool contains(const V& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::string tilesStr(const V& v, const OkeyInfo& ok) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += tileNameTR(v[i], ok);
    }
    return s + "]";
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------------------------------------
// Brute-force oracle (independent of Meld.cpp and Solver.cpp): best value of a subset read as one meld.

// Returns -1 if the subset is not a meld, else the highest value among its group/run readings.
int oracleMeldValue(const V& ids, const OkeyInfo& ok) {
    const int n = (int)ids.size();
    if (n < 3) return -1;
    V faces, colors; // real tiles only: okeys fit any position, the meld size accounts for them
    for (int id : ids) {
        if (!ok.isJoker(id)) {
            faces.push_back(ok.faceNumber(id));
            colors.push_back(ok.faceColor(id));
        }
    }
    if (faces.empty()) return -1;
    int best = -1;
    // group
    if (n <= 4) {
        bool same = true, distinct = true;
        for (size_t i = 0; i < faces.size(); ++i) {
            if (faces[i] != faces[0]) same = false;
            for (size_t j = 0; j < i; ++j)
                if (colors[j] == colors[i]) distinct = false;
        }
        if (same && distinct) best = std::max(best, faces[0] * n);
    }
    // run
    bool oneColor = true;
    for (int c : colors)
        if (c != colors[0]) oneColor = false;
    if (oneColor && n <= 13) {  // 101: runs end at 13 (no 12-13-1)
        for (int s = 1; s + n - 1 <= 13; ++s) {
            const int e = s + n - 1;
            bool used[14] = {};
            bool okRun = true;
            for (int f : faces) {
                if (f < s || f > e || used[f]) okRun = false;
                else used[f] = true;
            }
            if (!okRun) continue;
            int v = 0;
            for (int p = s; p <= e; ++p) v += p;
            best = std::max(best, v);
        }
    }
    return best;
}

struct OracleResult {
    long long score = -1; // value*1000 + tiles*10 - jokers, or -1 if infeasible
    int value = 0, tiles = 0, jokers = 0;
};

// Exact optimum over all partitions (value, then tiles, then fewer okeys). mustIdx: index that must be in a meld.
OracleResult oracleSeries(const V& ids, const OkeyInfo& ok, int mustIdx) {
    const int n = (int)ids.size();
    const int full = (1 << n) - 1;
    std::vector<int> meldVal(1 << n, -1);
    for (int m = 1; m <= full; ++m) {
        if (__builtin_popcount(m) < 3) continue;
        V sub;
        for (int i = 0; i < n; ++i)
            if (m & (1 << i)) sub.push_back(ids[i]);
        meldVal[m] = oracleMeldValue(sub, ok);
    }
    const long long NEGL = -(1ll << 60);
    std::vector<long long> best(1 << n, NEGL);
    best[0] = 0;
    for (int m = 1; m <= full; ++m) {
        const int low = m & -m;
        const bool lowIsMust = mustIdx >= 0 && low == (1 << mustIdx);
        long long b = NEGL;
        if (!lowIsMust) b = best[m ^ low];
        const int rest = m ^ low;
        for (int sub = rest;; sub = (sub - 1) & rest) {
            const int mm = sub | low;
            if (meldVal[mm] >= 0 && best[m ^ mm] > NEGL) {
                int jok = 0;
                for (int i = 0; i < n; ++i)
                    if ((mm & (1 << i)) && ok.isJoker(ids[i])) ++jok;
                const long long sc = (long long)meldVal[mm] * 1000 + __builtin_popcount(mm) * 10 - jok + best[m ^ mm];
                b = std::max(b, sc);
            }
            if (sub == 0) break;
        }
        best[m] = b;
    }
    OracleResult r;
    if (best[full] <= NEGL) return r;
    r.score = best[full];
    return r;
}

// Maximum number of pairs by brute force over partitions (small hands).
int oraclePairs(const V& ids, const OkeyInfo& ok, int mustIdx) {
    const int n = (int)ids.size();
    std::vector<int> best(1 << n, -1000);
    best[0] = 0;
    for (int m = 1; m < (1 << n); ++m) {
        const int lowIdx = __builtin_ctz(m);
        const int low = 1 << lowIdx;
        int b = (mustIdx == lowIdx) ? -1000 : best[m ^ low];
        for (int j = lowIdx + 1; j < n; ++j) {
            if (!(m & (1 << j))) continue;
            const int a = ids[lowIdx], c = ids[j];
            const bool pairOk = ok.isJoker(a) || ok.isJoker(c) ||
                                (ok.faceColor(a) == ok.faceColor(c) && ok.faceNumber(a) == ok.faceNumber(c));
            if (pairOk && best[m ^ low ^ (1 << j)] >= 0) b = std::max(b, 1 + best[m ^ low ^ (1 << j)]);
        }
        best[m] = b;
    }
    return best[(1 << n) - 1];
}

// Checks structural validity of a series result and returns its (value, tiles, jokers) score.
long long checkSeriesResult(const V& hand, const OkeyInfo& ok, const SolveResult& r, int mustUse, bool& valid,
                            std::string& why) {
    valid = true;
    V all;
    int value = 0, tiles = 0, jokers = 0;
    for (const V& m : r.melds) {
        Meld meld;
        std::string w;
        if (!makeMeld(m, ok, meld, false, &w)) {
            valid = false;
            why = "invalid meld " + tilesStr(m, ok) + ": " + w;
            return -1;
        }
        // The returned order must read exactly as intended: a run is ascending with okeys at their positions.
        if (meld.kind == MeldKind::Run) {
            std::vector<int> order = meld.ids();
            if (order != m) {
                valid = false;
                why = "run not in reading order " + tilesStr(m, ok);
                return -1;
            }
        }
        const int ov = oracleMeldValue(m, ok);
        if (ov != meld.value()) {
            valid = false;
            why = "meld " + tilesStr(m, ok) + " reads as " + std::to_string(meld.value()) + ", best reading " +
                  std::to_string(ov);
            return -1;
        }
        value += meld.value();
        tiles += (int)m.size();
        for (int id : m)
            if (ok.isJoker(id)) ++jokers;
        all.insert(all.end(), m.begin(), m.end());
    }
    if (mustUse >= 0 && !contains(all, mustUse)) {
        valid = false;
        why = "mustUse not placed";
        return -1;
    }
    all.insert(all.end(), r.leftovers.begin(), r.leftovers.end());
    V a = all, h = hand;
    std::sort(a.begin(), a.end());
    std::sort(h.begin(), h.end());
    if (a != h) {
        valid = false;
        why = "tiles not conserved";
        return -1;
    }
    if (value != r.value || tiles != r.tilesUsed) {
        valid = false;
        why = "value/tilesUsed mismatch " + std::to_string(value) + "/" + std::to_string(r.value);
        return -1;
    }
    return (long long)value * 1000 + tiles * 10 - jokers;
}

OkeyInfo randomOkey(Rng& rng) {
    for (;;) {
        const int ind = rng.range(104);
        return OkeyInfo::fromIndicator(ind);
    }
}

// Random small hand biased toward dense meld structure: few colours, clustered numbers, okeys, fakes, aces.
V randomSmallHand(Rng& rng, const OkeyInfo& ok, int size) {
    std::vector<int> candidates;
    const int colors = 1 + rng.range(3);
    int cs[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; --i) std::swap(cs[i], cs[rng.range(i + 1)]);
    const int mode = rng.range(4);
    int lo = 1, hi = 13;
    if (mode == 0) { lo = 9; hi = 13; }        // high end + aces
    else if (mode == 1) { lo = 1; hi = 5; }    // low end
    else if (mode == 2) { lo = rng.range(1, 9); hi = lo + 4; }
    for (int ci = 0; ci < colors; ++ci)
        for (int n = lo; n <= hi; ++n)
            for (int cp = 0; cp < 2; ++cp) candidates.push_back(makeTileId(cs[ci], n, cp));
    if (mode == 0)
        for (int ci = 0; ci < colors; ++ci)
            for (int cp = 0; cp < 2; ++cp) candidates.push_back(makeTileId(cs[ci], 1, cp));
    // okeys and fakes
    for (int cp = 0; cp < 2; ++cp) candidates.push_back(makeTileId(ok.color, ok.number, cp));
    candidates.push_back(FAKE_JOKER_A);
    candidates.push_back(FAKE_JOKER_B);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    rng.shuffle(candidates);
    // Extra chance for okeys to be present.
    V hand;
    if (rng.chance(0.5f)) hand.push_back(makeTileId(ok.color, ok.number, 0));
    if (rng.chance(0.25f)) hand.push_back(makeTileId(ok.color, ok.number, 1));
    for (int id : candidates) {
        if ((int)hand.size() >= size) break;
        if (!contains(hand, id)) hand.push_back(id);
    }
    rng.shuffle(hand);
    return hand;
}

V randomFullHand(Rng& rng, int size) {
    V deck(NUM_TILES);
    for (int i = 0; i < NUM_TILES; ++i) deck[i] = i;
    rng.shuffle(deck);
    deck.resize(size);
    return deck;
}

// ---------------------------------------------------------------------------------------------------------

void testSolverFixed() {
    const OkeyInfo ok = OkeyInfo::fromIndicator(T(K, 4)); // okey = Black 5
    const int J1 = T(K, 5, 0), J2 = T(K, 5, 1);

    // Simple run + group
    {
        V h = {T(R, 3), T(R, 4), T(R, 5), T(Y, 9), T(B, 9), T(R, 9), T(Y, 1)};
        SolveResult r = solveSeries(h, ok);
        CHECK(r.feasible);
        CHECK_EQ(r.value, 12 + 27);
        CHECK_EQ(r.tilesUsed, 6);
        CHECK_EQ((int)r.leftovers.size(), 1);
    }
    // 12-13-1 is not a run in 101
    {
        V h = {T(R, 12), T(R, 13), T(R, 1)};
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 0);
        CHECK(r.melds.empty());
        CHECK_EQ((int)r.leftovers.size(), 3);
    }
    // okey + okey + 1 -> 1-2-3 (6) beats 1-1-1 (3): a 1 never follows 13
    {
        V h = {J1, J2, T(Y, 1)};
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 6);
    }
    // 13 + 2 okeys -> group 13-13-13 (39) beats 11-12-13 (36)
    {
        V h = {J1, J2, T(Y, 13)};
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 39);
        Meld m;
        CHECK(!r.melds.empty() && makeMeld(r.melds[0], ok, m, false) && m.value() == 39);
    }
    // full 1..13 run; a second 1 can't follow the 13
    {
        V h;
        for (int n = 1; n <= 13; ++n) h.push_back(T(B, n));
        h.push_back(T(B, 1, 1));
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 91);
        CHECK_EQ(r.tilesUsed, 13);
    }
    // fake jokers play as Black 5
    {
        V h = {T(K, 3), T(K, 4), FAKE_JOKER_A, T(K, 6)};
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 18);
        CHECK_EQ(r.tilesUsed, 4);
    }
    // mustUse forces a lower-value partition
    {
        // R5 R6 R7 (18) or group 7s (21 with Y7 B7) — R7 in both; must R5 -> run.
        V h = {T(R, 5), T(R, 6), T(R, 7), T(Y, 7), T(B, 7)};
        SolveResult a = solveSeries(h, ok);
        CHECK_EQ(a.value, 21);
        SolveResult b = solveSeries(h, ok, T(R, 5));
        CHECK(b.feasible);
        CHECK_EQ(b.value, 18);
        SolveResult c = solveSeries(h, ok, T(Y, 1)); // not in hand
        CHECK(!c.feasible);
        SolveResult d = solveSeries({T(R, 5), T(Y, 9), T(B, 11)}, ok, T(R, 5));
        CHECK(!d.feasible);
    }
    // ties: equal value -> more tiles; then fewer okeys
    {
        // Y3 Y4 Y5 (12) vs ... just check okey not wasted: Y3 Y4 Y5 + okey => Y3..Y6 (18) uses okey (more value).
        V h = {T(Y, 3), T(Y, 4), T(Y, 5), J1};
        SolveResult r = solveSeries(h, ok);
        CHECK_EQ(r.value, 18);
    }
    // empty and degenerate input
    {
        SolveResult r = solveSeries({}, ok);
        CHECK(r.feasible);
        CHECK_EQ(r.value, 0);
        SolveResult p = solvePairs({}, ok);
        CHECK_EQ(p.value, 0);
        SolveResult j = solveSeries({J1, J2}, ok);
        CHECK_EQ(j.value, 0);
        CHECK_EQ((int)j.leftovers.size(), 2);
    }
}

void testSolverOracle() {
    Rng rng(20260928);
    int cases = 0, withJokers = 0, withAce = 0, withMust = 0, withFake = 0;
    for (int iter = 0; iter < 6000; ++iter) {
        const OkeyInfo ok = randomOkey(rng);
        const int size = 3 + rng.range(8); // 3..10
        V hand = randomSmallHand(rng, ok, size);
        int mustIdx = -1;
        if (rng.chance(0.35f)) mustIdx = rng.range((int)hand.size());
        const int must = mustIdx >= 0 ? hand[mustIdx] : -1;

        const OracleResult o = oracleSeries(hand, ok, mustIdx);
        const SolveResult r = solveSeries(hand, ok, must);
        ++cases;
        for (int id : hand) {
            if (ok.isJoker(id)) { ++withJokers; break; }
        }
        for (int id : hand) {
            if (isFakeJoker(id)) { ++withFake; break; }
        }
        if (must >= 0) ++withMust;
        if (o.score < 0) {
            CHECK_MSG(!r.feasible, tilesStr(hand, ok));
            continue;
        }
        CHECK_MSG(r.feasible, tilesStr(hand, ok));
        if (!r.feasible) continue;
        bool valid = false;
        std::string why;
        const long long sc = checkSeriesResult(hand, ok, r, must, valid, why);
        CHECK_MSG(valid, why + " hand " + tilesStr(hand, ok));
        CHECK_MSG(sc == o.score, "solver " + std::to_string(sc) + " oracle " + std::to_string(o.score) + " hand " +
                                     tilesStr(hand, ok) + " must " + (must >= 0 ? tileNameTR(must, ok) : "-"));
        for (const V& m : r.melds) {  // runs that end at 13 (where plain okey would continue with a 1)
            Meld mm;
            if (makeMeld(m, ok, mm, false) && mm.kind == MeldKind::Run && mm.tiles.back().number == NUM_NUMBERS)
                ++withAce;
        }

        // Pairs against the oracle
        const int op = oraclePairs(hand, ok, mustIdx);
        const SolveResult p = solvePairs(hand, ok, must);
        if (op < 0) {
            CHECK_MSG(!p.feasible, tilesStr(hand, ok));
        } else {
            CHECK_MSG(p.feasible && p.value == op, "pairs " + std::to_string(p.value) + " oracle " +
                                                       std::to_string(op) + " " + tilesStr(hand, ok));
            V all;
            for (const V& m : p.melds) {
                Meld mm;
                CHECK(makeMeld(m, ok, mm, true));
                all.insert(all.end(), m.begin(), m.end());
            }
            if (must >= 0) CHECK(contains(all, must));
            CHECK_EQ(p.tilesUsed, (int)all.size());
            all.insert(all.end(), p.leftovers.begin(), p.leftovers.end());
            V h = hand;
            std::sort(all.begin(), all.end());
            std::sort(h.begin(), h.end());
            CHECK(all == h);
        }
    }
    std::printf("  oracle: %d hands (%d with okeys, %d with fakes, %d mustUse, %d runs ending at 13)\n", cases,
                withJokers, withFake, withMust, withAce);
}

void testPairsFixed() {
    const OkeyInfo ok = OkeyInfo::fromIndicator(T(K, 4)); // okey = Black 5
    const int J1 = T(K, 5, 0), J2 = T(K, 5, 1);
    V h = {T(R, 3, 0), T(R, 3, 1), T(Y, 9, 0), T(Y, 9, 1), T(B, 12), T(B, 2), J1, FAKE_JOKER_A, FAKE_JOKER_B};
    SolveResult p = solvePairs(h, ok);
    CHECK_EQ(p.value, 4); // R3 R3, Y9 Y9, fake fake, B12 + okey
    bool jokerWith12 = false;
    for (const V& m : p.melds)
        if (contains(m, J1) && contains(m, T(B, 12))) jokerWith12 = true;
    CHECK(jokerWith12);
    CHECK(p.leftovers == V({T(B, 2)}));
    // must a single: the okey goes to it
    SolveResult q = solvePairs(h, ok, T(B, 2));
    CHECK(q.feasible && q.value == 4);
    // two okeys and nothing single pair together
    SolveResult r = solvePairs({J1, J2, T(R, 3, 0), T(R, 3, 1)}, ok);
    CHECK_EQ(r.value, 2);
    // single must without okey: infeasible
    SolveResult s = solvePairs({T(R, 3, 0), T(R, 4, 0)}, ok, T(R, 4, 0));
    CHECK(!s.feasible);
    // must okey with only natural pairs: still 1 pair
    SolveResult t = solvePairs({J1, T(R, 3, 0), T(R, 3, 1)}, ok, J1);
    CHECK(t.feasible && t.value == 1);
}

void testArrange() {
    Rng rng(99);
    for (int iter = 0; iter < 300; ++iter) {
        const OkeyInfo ok = randomOkey(rng);
        V hand = randomFullHand(rng, 21 + rng.range(3));
        RackArrangement a = arrangeSeries(hand, ok);
        V all;
        int value = 0;
        for (const V& m : a.melds) {
            Meld mm;
            CHECK(makeMeld(m, ok, mm, false));
            value += mm.value();
            all.insert(all.end(), m.begin(), m.end());
        }
        CHECK_EQ(value, solveSeries(hand, ok).value);
        // leftovers sorted by colour then number, okeys last
        for (size_t i = 1; i < a.leftovers.size(); ++i) {
            const int x = a.leftovers[i - 1], y = a.leftovers[i];
            const bool jx = ok.isJoker(x), jy = ok.isJoker(y);
            CHECK(!(jx && !jy));
            if (!jx && !jy) {
                CHECK(ok.faceColor(x) < ok.faceColor(y) ||
                      (ok.faceColor(x) == ok.faceColor(y) && ok.faceNumber(x) <= ok.faceNumber(y)));
            }
        }
        all.insert(all.end(), a.leftovers.begin(), a.leftovers.end());
        std::sort(all.begin(), all.end());
        V h = hand;
        std::sort(h.begin(), h.end());
        CHECK(all == h);

        RackArrangement p = arrangePairs(hand, ok);
        V allp;
        for (const V& m : p.melds) {
            Meld mm;
            CHECK(makeMeld(m, ok, mm, true));
            allp.insert(allp.end(), m.begin(), m.end());
        }
        CHECK_EQ((int)p.melds.size(), solvePairs(hand, ok).value);
        allp.insert(allp.end(), p.leftovers.begin(), p.leftovers.end());
        std::sort(allp.begin(), allp.end());
        CHECK(allp == h);
    }
}

void testSolverTiming() {
    Rng rng(4242);
    double maxMs = 0, sumMs = 0;
    int n = 0;
    double maxJ = 0, sumJ = 0;
    int nJ = 0;
    for (int iter = 0; iter < 10000; ++iter) {
        const OkeyInfo ok = randomOkey(rng);
        V hand = randomFullHand(rng, 22 + rng.range(2));
        // Every other hand gets both okeys (worst case for the search).
        if (iter % 2 == 0) {
            for (int cp = 0; cp < 2; ++cp) {
                const int j = makeTileId(ok.color, ok.number, cp);
                if (!contains(hand, j)) hand[cp] = j;
            }
        }
        const double t0 = nowMs();
        SolveResult r = solveSeries(hand, ok);
        const double dt = nowMs() - t0;
        (void)r;
        maxMs = std::max(maxMs, dt);
        sumMs += dt;
        ++n;
        if (iter % 2 == 0) {
            maxJ = std::max(maxJ, dt);
            sumJ += dt;
            ++nJ;
        }
        if (iter < 400) {
            bool valid = false;
            std::string why;
            checkSeriesResult(hand, ok, r, -1, valid, why);
            CHECK_MSG(valid, why);
        }
    }
    std::printf("  solveSeries timing over %d random 22-23 tile hands: avg %.3f ms, max %.3f ms\n", n, sumMs / n,
                maxMs);
    std::printf("    (hands with both okeys: avg %.3f ms, max %.3f ms)\n", sumJ / nJ, maxJ);
    CHECK(maxMs < 50.0);
}

} // namespace

// Bot scenario tests live below (declared here, defined after the solver tests).
void runBotTests();

int main(int argc, char** argv) {
    bool onlySolver = argc > 1 && std::strcmp(argv[1], "--solver") == 0;
    const double t0 = nowMs();
    std::printf("solver: fixed cases\n");
    testSolverFixed();
    testPairsFixed();
    std::printf("solver: brute-force oracle\n");
    testSolverOracle();
    std::printf("solver: arrange\n");
    testArrange();
    std::printf("solver: timing\n");
    testSolverTiming();
    if (!onlySolver) {
        std::printf("bots: scenarios\n");
        runBotTests();
    }
    std::printf("%d checks, %d failures (%.1f s)\n", g_checks, g_failures, (nowMs() - t0) / 1000.0);
    return g_failures ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------
// Bot scenarios: positions built with the Game testing hooks, then one bot turn played through the public
// API (every action must be accepted).

namespace {

const BotLevel LEVELS[3] = {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard};
const char* LEVEL_NAMES[3] = {"Acemi", "Usta", "Kurt"};

struct Setup {
    int indicator = T(K, 4); // okey = Black 5
    V hands[4];
    V discards[4];
    std::vector<Meld> table;
    V pileTop; // drawn first: back() is the next tile
    bool opened[4] = {};
    bool pairs[4] = {};
    int seat = 0;
    TurnStage stage = TurnStage::Play;
    RulesConfig cfg;
};

Meld meldOf(const V& ids, const OkeyInfo& ok, int owner, bool pair = false) {
    Meld m;
    CHECK_MSG(makeMeld(ids, ok, m, pair), tilesStr(ids, ok));
    m.owner = owner;
    return m;
}

// Fills the other hands with the unused tiles (21 each where possible) and puts the rest in the pile.
void applySetup(Game& g, const Setup& s) {
    g.setRules(s.cfg);
    g.startMatch(4242);
    g.drainEvents();
    std::vector<int> used(NUM_TILES, 0);
    used[s.indicator]++;
    for (int p = 0; p < 4; ++p) {
        for (int id : s.hands[p]) used[id]++;
        for (int id : s.discards[p]) used[id]++;
    }
    for (const Meld& m : s.table)
        for (int id : m.ids()) used[id]++;
    for (int id : s.pileTop) used[id]++;
    bool unique = true;
    for (int c : used)
        if (c > 1) unique = false;
    CHECK_MSG(unique, "scenario uses a tile twice");
    V free;
    for (int id = 0; id < NUM_TILES; ++id)
        if (!used[id]) free.push_back(id);
    Rng rng(99);
    rng.shuffle(free);
    V hands[4];
    for (int p = 0; p < 4; ++p) {
        hands[p] = s.hands[p];
        if (!hands[p].empty()) continue;
        while ((int)hands[p].size() < 21 && free.size() > 12) {
            hands[p].push_back(free.back());
            free.pop_back();
        }
    }
    free.insert(free.end(), s.pileTop.begin(), s.pileTop.end());
    g.debugPile() = free;
    for (int p = 0; p < 4; ++p) {
        PlayerInfo& pl = g.debugPlayer(p);
        pl.hand = hands[p];
        pl.discards = s.discards[p];
        pl.opened = s.opened[p];
        pl.openedWithPairs = s.pairs[p];
        pl.openedTurn = s.opened[p] ? 0 : -1;
        pl.openValue = s.opened[p] ? (s.pairs[p] ? 5 : 101) : 0;
        pl.handPenalty = 0;
    }
    g.debugTable() = s.table;
    g.debugSetOkey(s.indicator);
    g.debugSetTurn(s.seat, s.stage);
    g.drainEvents();
}

struct TurnLog {
    std::vector<BotAction> acts;
    std::vector<GameEvent> events;
    int rejected = 0;
    bool has(BotAction::Kind k) const {
        for (const BotAction& a : acts)
            if (a.kind == k) return true;
        return false;
    }
    const BotAction* first(BotAction::Kind k) const {
        for (const BotAction& a : acts)
            if (a.kind == k) return &a;
        return nullptr;
    }
    int penalties() const {
        int n = 0;
        for (const GameEvent& e : events)
            if (e.type == EvType::Penalty) ++n;
        return n;
    }
};

// Plays the current player's turn with `bot` until the turn passes (or the hand ends).
TurnLog playTurn(Game& g, Bot& bot) {
    TurnLog log;
    const int seat = g.current();
    const int turn = g.turnNumber();
    for (int guard = 0; guard < 100 && g.handState() == HandState::Playing && g.current() == seat &&
                        g.turnNumber() == turn;
         ++guard) {
        const BotAction a = bot.next(g, seat);
        const ActionResult r = applyBotAction(g, seat, a);
        log.acts.push_back(a);
        if (!r.ok) {
            ++log.rejected;
            std::printf("    rejected: %s\n", r.error.c_str());
            applyBotAction(g, seat, fallbackAction(g, seat));
        }
        std::vector<GameEvent> ev = g.drainEvents();
        log.events.insert(log.events.end(), ev.begin(), ev.end());
    }
    return log;
}

int meldValueOf(const V& ids, const OkeyInfo& ok) {
    Meld m;
    return makeMeld(ids, ok, m, false) ? m.value() : 0;
}

// A 22-tile starter hand holding 115 in series (okey = Black 5) plus unconnected singles.
Setup openingSetup() {
    Setup s;
    s.hands[0] = {T(R, 10), T(R, 11), T(R, 12), T(R, 13), // 46
                  T(B, 11), T(B, 12), T(B, 13),           // 36
                  T(Y, 2), T(Y, 3), T(Y, 4),              // 9
                  T(K, 7), T(K, 8), T(K, 9),              // 24 -> 115
                  T(Y, 7), T(B, 2), T(K, 1), T(B, 6), T(R, 3), T(B, 4), T(Y, 9), T(R, 6), T(B, 8)};
    return s;
}

void testBotOpens() {
    for (int l = 0; l < 3; ++l) {
        Game g;
        const Setup s = openingSetup();
        applySetup(g, s);
        CHECK_EQ(solveSeries(s.hands[0], g.okey()).value, 115);
        Bot bot(LEVELS[l], 7);
        const TurnLog t = playTurn(g, bot);
        CHECK_EQ(t.rejected, 0);
        const BotAction* open = t.first(BotAction::Kind::Open);
        CHECK_MSG(open != nullptr, LEVEL_NAMES[l]);
        if (open) {
            int v = 0;
            for (const V& m : open->melds) v += meldValueOf(m, g.okey());
            CHECK_MSG(v >= 101, LEVEL_NAMES[l]);
            CHECK_MSG(v == 115, std::string(LEVEL_NAMES[l]) + " lays every meld when opening");
        }
        CHECK(g.player(0).opened);
        CHECK(t.has(BotAction::Kind::Discard));
        CHECK_EQ(t.penalties(), 0);
    }
}

// The left neighbour's discard completes an opening: Usta and Kurt take it; a useless one is not taken.
void testBotTakesLeftToOpen() {
    for (int l = 1; l < 3; ++l) {
        for (int useful = 0; useful < 2; ++useful) {
            Game g;
            Setup s;
            s.seat = 1;
            s.stage = TurnStage::NeedDraw;
            s.hands[1] = {T(R, 10), T(R, 11), T(R, 12),  // 33
                          T(Y, 9), T(B, 9), T(R, 9),     // 27
                          T(B, 11), T(B, 12), T(B, 13),  // 36 -> 96
                          T(K, 12), T(K, 13),            // + K11 -> 36
                          T(Y, 2), T(Y, 6), T(Y, 1), T(B, 5), T(K, 8), T(R, 3), T(R, 6), T(B, 3), T(Y, 4), T(K, 2)};
            s.discards[0] = {T(Y, 7), useful ? T(K, 11) : T(R, 1)};
            applySetup(g, s);
            CHECK_EQ(solveSeries(s.hands[1], g.okey()).value, 96);
            Bot bot(LEVELS[l], 3);
            const TurnLog t = playTurn(g, bot);
            CHECK_EQ(t.rejected, 0);
            CHECK_EQ(t.penalties(), 0);
            CHECK(!t.acts.empty());
            if (t.acts.empty()) continue;
            const BotAction::Kind want = useful ? BotAction::Kind::TakeLeft : BotAction::Kind::DrawPile;
            CHECK_MSG(t.acts.front().kind == want,
                      std::string(LEVEL_NAMES[l]) + (useful ? ": should take K11" : ": should draw"));
            if (useful) CHECK_MSG(g.player(1).opened, LEVEL_NAMES[l]);
        }
    }
}

// Unopened, the only tiles without a use fit table melds (işlek) except one; the bot must keep the
// işlek tiles and never throw the okey.
void testBotAvoidsPenaltyDiscards() {
    for (int l = 0; l < 3; ++l) {
        for (int trial = 0; trial < 6; ++trial) {
            Game g;
            Setup s;
            s.seat = 0;
            s.stage = TurnStage::Play;
            const OkeyInfo ok = OkeyInfo::fromIndicator(s.indicator);
            s.table.push_back(meldOf({T(Y, 5), T(Y, 6), T(Y, 7)}, ok, 2));
            s.table.push_back(meldOf({T(B, 10), T(Y, 10), T(R, 10)}, ok, 2));
            s.opened[2] = true;
            // Y4 / Y8 / K10 are işlek; the okey must stay; B13 R1 etc. are free to go.
            s.hands[0] = {T(Y, 4), T(Y, 8), T(K, 10), T(K, 5, 0), T(B, 13), T(R, 1), T(K, 2), T(B, 7),
                          T(R, 12), T(Y, 1), T(B, 3), T(K, 7), T(R, 4), T(Y, 12), T(K, 13), T(B, 8),
                          T(R, 6), T(Y, 2), T(B, 5), T(K, 11), T(R, 8), T(Y, 13)};
            applySetup(g, s);
            Bot bot(LEVELS[l], 11 + trial);
            const TurnLog t = playTurn(g, bot);
            CHECK_EQ(t.rejected, 0);
            const BotAction* d = t.first(BotAction::Kind::Discard);
            CHECK(d != nullptr);
            if (!d) continue;
            CHECK_MSG(!g.okey().isJoker(d->tile), LEVEL_NAMES[l]);
            if (l > 0) CHECK_MSG(t.penalties() == 0, std::string(LEVEL_NAMES[l]) + " threw an işlek tile");
        }
    }
}

// Opened, and everything but one tile can go: the bot finishes, with the okey as the last tile when it
// has one (okeyle bitiş doubles the win).
void testBotFinishes() {
    for (int l = 1; l < 3; ++l) {
        for (int withJoker = 0; withJoker < 2; ++withJoker) {
            Game g;
            Setup s;
            s.seat = 0;
            s.stage = TurnStage::Play;
            const OkeyInfo ok = OkeyInfo::fromIndicator(s.indicator);
            s.table.push_back(meldOf({T(Y, 5), T(Y, 6), T(Y, 7)}, ok, 0));
            s.table.push_back(meldOf({T(B, 10), T(Y, 10), T(R, 10)}, ok, 0));
            s.table.push_back(meldOf({T(R, 1), T(R, 2), T(R, 3)}, ok, 0));
            s.opened[0] = true;
            s.hands[0] = {T(Y, 8), T(K, 10), T(R, 4), T(B, 12), T(B, 13), T(B, 11)};
            if (withJoker) s.hands[0].push_back(T(K, 5, 0));
            else s.hands[0].push_back(T(Y, 1));
            applySetup(g, s);
            Bot bot(LEVELS[l], 5);
            const TurnLog t = playTurn(g, bot);
            CHECK_EQ(t.rejected, 0);
            CHECK_MSG(g.handState() != HandState::Playing, std::string(LEVEL_NAMES[l]) + " should finish");
            const HandResult& hr = g.lastHandResult();
            CHECK_EQ(hr.winner, 0);
            if (withJoker)
                CHECK_MSG(hr.finishedWithJoker, std::string(LEVEL_NAMES[l]) + " should finish with the okey");
        }
    }
}

// Opened with the real tile for a table okey: the bot takes the okey (and plays it).
void testBotSwapsJoker() {
    for (int l = 1; l < 3; ++l) {
        Game g;
        Setup s;
        s.seat = 0;
        s.stage = TurnStage::Play;
        const OkeyInfo ok = OkeyInfo::fromIndicator(s.indicator);
        s.table.push_back(meldOf({T(R, 6), T(K, 5, 0), T(R, 8)}, ok, 2)); // okey stands for R7
        s.opened[0] = true;
        s.opened[2] = true;
        s.hands[0] = {T(R, 7), T(Y, 2), T(B, 9), T(K, 12), T(Y, 13), T(B, 4), T(K, 1), T(R, 11), T(Y, 6)};
        applySetup(g, s);
        Bot bot(LEVELS[l], 5);
        const TurnLog t = playTurn(g, bot);
        CHECK_EQ(t.rejected, 0);
        CHECK_MSG(t.has(BotAction::Kind::SwapJoker), LEVEL_NAMES[l]);
        CHECK_EQ(t.penalties(), 0);
    }
}

// A pair-rich hand without a series opening: every level opens with pairs when the pile is short.
void testBotOpensPairs() {
    for (int l = 0; l < 3; ++l) {
        Game g;
        Setup s;
        s.seat = 0;
        s.stage = TurnStage::Play;
        s.hands[0] = {T(Y, 3, 0), T(Y, 3, 1), T(B, 8, 0), T(B, 8, 1), T(R, 12, 0), T(R, 12, 1), T(K, 1, 0),
                      T(K, 1, 1), T(Y, 10, 0), T(Y, 10, 1), T(B, 6, 0), T(B, 6, 1), T(R, 2), T(K, 9), T(Y, 13),
                      T(B, 11), T(R, 7), T(K, 12), T(Y, 6), T(B, 1), T(R, 4)};
        applySetup(g, s);
        // leave only three tiles in the pile: no time to wait for a series
        V& pile = g.debugPile();
        std::vector<int> spill(pile.begin(), pile.end() - 3);
        pile.erase(pile.begin(), pile.end() - 3);
        g.debugPlayer(1).hand.insert(g.debugPlayer(1).hand.end(), spill.begin(), spill.end());
        CHECK_EQ(solvePairs(s.hands[0], g.okey()).value, 6);
        CHECK(solveSeries(s.hands[0], g.okey()).value < 101);
        Bot bot(LEVELS[l], 9);
        const TurnLog t = playTurn(g, bot);
        CHECK_EQ(t.rejected, 0);
        const BotAction* open = t.first(BotAction::Kind::Open);
        CHECK_MSG(open != nullptr, LEVEL_NAMES[l]);
        if (open) CHECK_MSG(open->melds.size() >= 5 && open->melds[0].size() == 2, LEVEL_NAMES[l]);
        CHECK(g.player(0).openedWithPairs);
    }
}

// next() repeats its action when the state did not change (the caller did not apply it yet).
void testBotRepeatsUnappliedAction() {
    Game g;
    applySetup(g, openingSetup());
    Bot bot(BotLevel::Hard, 1);
    const BotAction a = bot.next(g, 0);
    const BotAction b = bot.next(g, 0);
    CHECK(a.kind == b.kind && a.melds == b.melds && a.tile == b.tile && a.meld == b.meld);
    CHECK(applyBotAction(g, 0, b).ok);
}

// Whole matches between bots: no rejected action, fallbackAction always legal, the same seed replays the
// same game, and Kurt stays within its time budget.
struct MatchLog {
    std::vector<std::string> acts;
    int rejected = 0;
    int fallbackRejected = 0;
    double maxMs[3] = {};
};

MatchLog playMatch(uint64_t seed, const int levels[4], int hands, bool useFallbackSometimes) {
    RulesConfig cfg;
    cfg.numHands = hands;
    Game g(cfg);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, "Bot" + std::to_string(s), false);
    std::vector<Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(LEVELS[levels[s]], seed * 4 + (uint64_t)s);
    MatchLog log;
    Rng pick(seed ^ 0xABCDEFull);
    g.startMatch(seed);
    while (g.handState() != HandState::MatchOver) {
        if (g.handState() == HandState::HandOver) {
            g.startNextHand();
            for (Bot& b : bots) b.resetForHand();
        }
        for (const GameEvent& e : g.drainEvents())
            for (int s = 0; s < 4; ++s) bots[s].observe(e, g);
        if (g.handState() != HandState::Playing) continue;
        const int seat = g.current();
        BotAction a;
        if (useFallbackSometimes && pick.chance(0.05f)) {
            a = fallbackAction(g, seat);
            const ActionResult r = applyBotAction(g, seat, a);
            if (!r.ok) ++log.fallbackRejected;
            log.acts.push_back("F" + std::to_string((int)a.kind) + ":" + std::to_string(a.tile));
            continue;
        }
        const double t0 = nowMs();
        a = bots[seat].next(g, seat);
        const double dt = nowMs() - t0;
        double& mx = log.maxMs[levels[seat]];
        mx = std::max(mx, dt);
        std::string key = std::to_string(seat) + ":" + std::to_string((int)a.kind) + ":" + std::to_string(a.tile) +
                          ":" + std::to_string(a.meld);
        for (const V& m : a.melds) key += tilesStr(m, g.okey());
        log.acts.push_back(key);
        const ActionResult r = applyBotAction(g, seat, a);
        if (!r.ok) {
            ++log.rejected;
            const ActionResult r2 = applyBotAction(g, seat, fallbackAction(g, seat));
            if (!r2.ok) ++log.fallbackRejected;
        }
    }
    return log;
}

void testBotMatches() {
    const int mixes[4][4] = {{2, 1, 2, 1}, {0, 1, 2, 0}, {2, 2, 2, 2}, {0, 0, 0, 0}};
    double kurtMax = 0;
    for (int mi = 0; mi < 4; ++mi) {
        for (uint64_t seed = 1; seed <= 3; ++seed) {
            const MatchLog a = playMatch(seed * 31 + (uint64_t)mi, mixes[mi], 3, false);
            CHECK_EQ(a.rejected, 0);
            CHECK_EQ(a.fallbackRejected, 0);
            kurtMax = std::max(kurtMax, a.maxMs[2]);
            if (seed == 1) {
                const MatchLog b = playMatch(seed * 31 + (uint64_t)mi, mixes[mi], 3, false);
                CHECK_MSG(a.acts == b.acts, "same seed must replay the same match");
            }
            const MatchLog f = playMatch(seed * 97 + (uint64_t)mi, mixes[mi], 2, true);
            CHECK_EQ(f.rejected, 0);
            CHECK_EQ(f.fallbackRejected, 0);
        }
    }
    std::printf("  Kurt max decision time in the test matches: %.1f ms\n", kurtMax);
    CHECK(kurtMax < 100.0); // generous: the budget targets ~15 ms; timing under a loaded machine varies
}

} // namespace

void runBotTests() {
    testBotOpens();
    testBotTakesLeftToOpen();
    testBotAvoidsPenaltyDiscards();
    testBotFinishes();
    testBotSwapsJoker();
    testBotOpensPairs();
    testBotRepeatsUnappliedAction();
    testBotMatches();
}
