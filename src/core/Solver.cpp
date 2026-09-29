// Hand solver for 101 Okey: exact best partition into runs/groups, maximum pairs, rack arrangement.
//
// Series search: the hand is reduced to counts per face slot (4 colours x 13 numbers) plus the number of
// wild okeys. A DFS always resolves the lowest occupied slot (colour-major order): leave one copy over, or
// start a meld whose lowest real tile is that copy (runs of every length up to 13 with okeys anywhere — 101
// has no '1' after 13 — and groups with every subset of the other colours). Results are memoised on the
// full count state, so the search is exact.
#include "core/Solver.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace okey {

namespace detail {
// objective: 0 = max meld value (public solveSeries), 1 = max tiles used then value,
//            2 = max hand points removed (okey = 101) then tiles then value. Used by Bot.cpp.
SolveResult solveSeriesObjective(const std::vector<int>& tiles, const OkeyInfo& ok, int mustUse, int objective);
// Search states expanded by series solves on this thread so far (a deterministic measure of solver work,
// used by Bot.cpp to budget its look-ahead independently of machine speed).
uint64_t solverWork();
} // namespace detail

namespace {

constexpr int SLOTS_PER_COLOR = NUM_NUMBERS; // numbers 1..13
constexpr int NEG = -1000000000;
constexpr int JOKER_HAND = JOKER_IN_HAND_VALUE;

int slotOf(int color, int number) { return color * SLOTS_PER_COLOR + number - 1; }

struct Weights {
    int value, tiles, joker, hand;
};

Weights weightsFor(int objective) {
    switch (objective) {
    case 1: return {1000, 1000000, -1, 0};
    case 2: return {1, 1000, 0, 100000};
    default: return {1000, 10, -1, 0};
    }
}

// Choice encoding stored per memo state.
enum ChoiceType : uint32_t { C_LEFT = 0, C_RUN = 2, C_GROUP = 3 };

uint32_t encodeRun(int s0, int e, uint32_t jokerMask) {
    return C_RUN | ((uint32_t)s0 << 2) | ((uint32_t)e << 6) | (jokerMask << 10);
}
uint32_t encodeGroup(uint32_t colorMask, int jokers) { return C_GROUP | (colorMask << 2) | ((uint32_t)jokers << 6); }

struct Entry {
    uint64_t k0 = 0, k1 = 0;
    int32_t score = 0;
    uint32_t choice = 0;
    uint32_t gen = 0;
};

// Open-addressing memo reused across calls (generation stamps avoid clearing).
class Memo {
public:
    void begin() {
        if (table_.empty()) resize(1u << 12);
        ++gen_;
        if (gen_ == 0) {
            for (Entry& e : table_) e.gen = 0;
            gen_ = 1;
        }
        used_ = 0;
    }
    const Entry* find(uint64_t k0, uint64_t k1) const {
        size_t i = hash(k0, k1) & mask_;
        for (;;) {
            const Entry& e = table_[i];
            if (e.gen != gen_) return nullptr;
            if (e.k0 == k0 && e.k1 == k1) return &e;
            i = (i + 1) & mask_;
        }
    }
    void insert(uint64_t k0, uint64_t k1, int score, uint32_t choice) {
        if ((used_ + 1) * 2 > table_.size()) grow();
        place(k0, k1, score, choice);
        ++used_;
    }
    size_t size() const { return used_; }

private:
    static size_t hash(uint64_t a, uint64_t b) {
        uint64_t h = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 32;
        return (size_t)h;
    }
    void place(uint64_t k0, uint64_t k1, int score, uint32_t choice) {
        size_t i = hash(k0, k1) & mask_;
        while (table_[i].gen == gen_) i = (i + 1) & mask_;
        Entry& e = table_[i];
        e.k0 = k0;
        e.k1 = k1;
        e.score = score;
        e.choice = choice;
        e.gen = gen_;
    }
    void resize(size_t n) {
        table_.assign(n, Entry());
        mask_ = n - 1;
    }
    void grow() {
        std::vector<Entry> old;
        old.swap(table_);
        const uint32_t g = gen_;
        resize(old.size() * 2);
        gen_ = 1;
        for (const Entry& e : old)
            if (e.gen == g) place(e.k0, e.k1, e.score, e.choice);
    }

    std::vector<Entry> table_;
    size_t mask_ = 0;
    uint32_t gen_ = 0;
    size_t used_ = 0;
};

Memo& scratchMemo() {
    thread_local Memo memo;
    return memo;
}

thread_local uint64_t g_work = 0;

// Abstract meld found by the search (faces only; physical ids are assigned afterwards).
struct AbsMeld {
    bool run = true;
    int color = 0;           // run colour / lowest group colour
    int number = 0;          // run: lowest real number (slot number); group: number
    int s0 = 0, e = 0;       // run positions
    uint32_t jokerMask = 0;  // run: bit (p-1) set = okey at position p
    uint32_t colorMask = 0;  // group: other colours
    int groupJokers = 0;
};

class SeriesSearch {
public:
    SeriesSearch(Memo& memo, Weights w) : memo_(memo), w_(w) {}

    uint64_t cnt_[2] = {0, 0}; // 2 bits per slot, slots 0..27 in word 0, 28..55 in word 1
    int jokers_ = 0;
    int initJokers_ = 0;
    bool mustDone_ = true;
    int mustA_ = -1;              // the slot whose use satisfies a mustUse face
    bool mustJoker_ = false;

    int get(int s) const { return (int)((cnt_[s / 28] >> ((s % 28) * 2)) & 3u); }
    void dec(int s) { cnt_[s / 28] -= 1ull << ((s % 28) * 2); }
    void inc(int s) { cnt_[s / 28] += 1ull << ((s % 28) * 2); }

    int solve() {
        if (cnt_[0] == 0 && cnt_[1] == 0) return terminal();
        const uint64_t k0 = cnt_[0] | ((uint64_t)jokers_ << 56) | ((uint64_t)mustDone_ << 58);
        const uint64_t k1 = cnt_[1];
        if (const Entry* e = memo_.find(k0, k1)) return e->score;

        const int s = lowestSlot();
        const int c = s / SLOTS_PER_COLOR;
        const int n = s % SLOTS_PER_COLOR + 1;
        Best b;

        dec(s);
        // 1. leave this copy over
        if (mustDone_ || !isMustSlot(s) || mustCopiesLeft() > 0) b.offer(solve(), C_LEFT);
        // 2. runs whose lowest real tile is this copy
        runExtend(b, c, n, n, 1, 0u, n, n, 0, isMustSlot(s));
        // 3. groups
        groups(b, c, n);
        inc(s);

        memo_.insert(k0, k1, b.score, b.choice);
        return b.score;
    }

    // Replays the memoised choices from the current state, appending the chosen melds.
    void reconstruct(std::vector<AbsMeld>& out) {
        while (cnt_[0] != 0 || cnt_[1] != 0) {
            const uint64_t k0 = cnt_[0] | ((uint64_t)jokers_ << 56) | ((uint64_t)mustDone_ << 58);
            const Entry* e = memo_.find(k0, cnt_[1]);
            if (!e) return; // cannot happen: every visited state is memoised
            const uint32_t ch = e->choice;
            const int s = lowestSlot();
            const int c = s / SLOTS_PER_COLOR;
            const int n = s % SLOTS_PER_COLOR + 1;
            dec(s);
            switch (ch & 3u) {
            case C_LEFT: break;
            case C_RUN: {
                AbsMeld m;
                m.run = true;
                m.color = c;
                m.number = n;
                m.s0 = (int)((ch >> 2) & 15u);
                m.e = (int)((ch >> 6) & 15u);
                m.jokerMask = (ch >> 10) & 0x3FFFu;
                for (int p = m.s0; p <= m.e; ++p) {
                    if (m.jokerMask & (1u << (p - 1))) --jokers_;
                    else if (p != n) dec(slotOf(c, p));
                }
                if (isMustSlot(s)) mustDone_ = true;
                for (int p = n + 1; p <= m.e; ++p)
                    if (!(m.jokerMask & (1u << (p - 1))) && isMustSlot(slotOf(c, p))) mustDone_ = true;
                out.push_back(m);
                break;
            }
            case C_GROUP: {
                AbsMeld m;
                m.run = false;
                m.color = c;
                m.number = n;
                m.colorMask = (ch >> 2) & 15u;
                m.groupJokers = (int)((ch >> 6) & 3u);
                if (isMustSlot(s)) mustDone_ = true;
                for (int c2 = 0; c2 < NUM_COLORS; ++c2) {
                    if (m.colorMask & (1u << c2)) {
                        dec(slotOf(c2, n));
                        if (isMustSlot(slotOf(c2, n))) mustDone_ = true;
                    }
                }
                jokers_ -= m.groupJokers;
                out.push_back(m);
                break;
            }
            }
        }
    }

private:
    int terminal() const {
        if (!mustDone_) return NEG;
        if (mustJoker_ && jokers_ == initJokers_) return NEG;
        return 0;
    }
    int lowestSlot() const {
        if (cnt_[0]) return __builtin_ctzll(cnt_[0]) / 2;
        return 28 + __builtin_ctzll(cnt_[1]) / 2;
    }
    bool isMustSlot(int s) const { return s == mustA_; }
    int mustCopiesLeft() const { return mustA_ >= 0 ? get(mustA_) : 0; }

    struct Best {
        int score = NEG;
        uint32_t choice = C_LEFT;
        void offer(int v, uint32_t ch) {
            if (v > score) {
                score = v;
                choice = ch;
            }
        }
    };

    int meldScore(int value, int tiles, int jokers, int hand) const {
        return w_.value * value + w_.tiles * tiles + w_.joker * jokers + w_.hand * hand;
    }

    // Scores the rest of the hand after laying a meld worth `meldScoreValue`.
    void consider(Best& b, int meldScoreValue, bool usedMust, uint32_t choice) {
        const bool saved = mustDone_;
        if (usedMust) mustDone_ = true;
        const int sub = solve();
        mustDone_ = saved;
        if (sub <= NEG) return;
        b.offer(sub + meldScoreValue, choice);
    }

    // Positions n..p are placed (n = lowest real tile). `value` = sum of represented values,
    // `hand` = hand points of the placed tiles, `jUsed` = okeys used so far.
    void runExtend(Best& b, int c, int n, int p, int len, uint32_t jmask, int value, int hand, int jUsed,
                   bool usedMust) {
        if (len >= 3) consider(b, meldScore(value, len, jUsed, hand), usedMust, encodeRun(n, p, jmask));
        if (p == NUM_NUMBERS) {
            // The run cannot grow upwards any more: okeys below the lowest real tile become useful.
            // (While it can still grow, an okey on top is always worth more than one below.)
            int v = value, h = hand;
            uint32_t m = jmask;
            for (int kb = 1; kb <= jokers_ && n - kb >= 1; ++kb) {
                const int pos = n - kb;
                v += pos;
                h += JOKER_HAND;
                m |= 1u << (pos - 1);
                if (len + kb >= 3) {
                    jokers_ -= kb;
                    consider(b, meldScore(v, len + kb, jUsed + kb, h), usedMust, encodeRun(pos, p, m));
                    jokers_ += kb;
                }
            }
            return;
        }
        const int q = p + 1;
        const int qs = slotOf(c, q);
        if (get(qs) > 0) {
            dec(qs);
            runExtend(b, c, n, q, len + 1, jmask, value + q, hand + q, jUsed, usedMust || isMustSlot(qs));
            inc(qs);
        }
        if (jokers_ > 0) {
            --jokers_;
            runExtend(b, c, n, q, len + 1, jmask | (1u << (q - 1)), value + q, hand + JOKER_HAND, jUsed + 1,
                      usedMust);
            ++jokers_;
        }
    }

    void groups(Best& b, int c, int n) {
        int others[3];
        int k = 0;
        for (int c2 = c + 1; c2 < NUM_COLORS; ++c2)
            if (get(slotOf(c2, n)) > 0) others[k++] = c2;
        const bool selfMust = isMustSlot(slotOf(c, n));
        for (int mask = 0; mask < (1 << k); ++mask) {
            int size = 1;
            uint32_t colorMask = 0;
            bool usedMust = selfMust;
            for (int i = 0; i < k; ++i) {
                if (mask & (1 << i)) {
                    ++size;
                    colorMask |= 1u << others[i];
                    if (isMustSlot(slotOf(others[i], n))) usedMust = true;
                }
            }
            for (int j = 0; j <= 2 && j <= jokers_; ++j) {
                const int total = size + j;
                if (total < 3 || total > 4) continue;
                for (int i = 0; i < k; ++i)
                    if (mask & (1 << i)) dec(slotOf(others[i], n));
                jokers_ -= j;
                consider(b, meldScore(n * total, total, j, n * size + JOKER_HAND * j), usedMust,
                         encodeGroup(colorMask, j));
                jokers_ += j;
                for (int i = 0; i < k; ++i)
                    if (mask & (1 << i)) inc(slotOf(others[i], n));
            }
        }
    }

    Memo& memo_;
    Weights w_;
};

bool faceLess(int a, int b, const OkeyInfo& ok) {
    const bool ja = ok.isJoker(a), jb = ok.isJoker(b);
    if (ja != jb) return jb; // okeys last
    if (!ja) {
        const int ca = ok.faceColor(a), cb = ok.faceColor(b);
        if (ca != cb) return ca < cb;
        const int na = ok.faceNumber(a), nb = ok.faceNumber(b);
        if (na != nb) return na < nb;
    }
    return a < b;
}

void sortTiles(std::vector<int>& v, const OkeyInfo& ok) {
    std::sort(v.begin(), v.end(), [&ok](int a, int b) { return faceLess(a, b, ok); });
}

// Removes invalid and duplicate ids (defensive; callers pass real hands).
std::vector<int> cleanTiles(const std::vector<int>& tiles, std::vector<int>& rejected) {
    std::vector<int> out;
    out.reserve(tiles.size());
    bool seen[NUM_TILES] = {};
    for (int id : tiles) {
        if (!isValidTile(id) || seen[id]) {
            rejected.push_back(id);
            continue;
        }
        seen[id] = true;
        out.push_back(id);
    }
    return out;
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

} // namespace

// ---------------------------------------------------------------------------------------------------------

SolveResult detail::solveSeriesObjective(const std::vector<int>& input, const OkeyInfo& ok, int mustUse,
                                         int objective) {
    SolveResult res;
    std::vector<int> rejected;
    const std::vector<int> tiles = cleanTiles(input, rejected);

    // Physical tiles per face (fake jokers play as the okey face) and the wild okeys. A real set holds at most
    // two copies of a face; further copies (defensive) and okeys beyond two stay leftovers.
    struct FacePool {
        int ids[2];
        int n = 0;
    };
    FacePool pool[NUM_COLORS][NUM_NUMBERS + 1];
    std::vector<int> extra;
    int jokerPool[2];
    int jokerCount = 0;
    for (int id : tiles) {
        if (ok.isJoker(id)) {
            if (jokerCount < 2) jokerPool[jokerCount++] = id;
            else extra.push_back(id);
            continue;
        }
        FacePool& fp = pool[ok.faceColor(id)][ok.faceNumber(id)];
        if (fp.n < 2) fp.ids[fp.n++] = id;
        else extra.push_back(id);
    }

    const bool hasMust = mustUse >= 0;
    if (hasMust && !contains(tiles, mustUse)) {
        res.feasible = false;
        res.leftovers = input;
        return res;
    }

    // The must tile has to be one of the searched copies, and is used first.
    auto moveToFront = [&](int* ids, int& n, int id) {
        for (int i = 0; i < n; ++i) {
            if (ids[i] != id) continue;
            std::swap(ids[0], ids[i]);
            return;
        }
        // an extra copy: swap it in for the searched one
        for (int& x : extra) {
            if (x != id) continue;
            x = ids[0];
            ids[0] = id;
            return;
        }
    };
    if (hasMust) {
        if (ok.isJoker(mustUse)) {
            moveToFront(jokerPool, jokerCount, mustUse);
        } else {
            FacePool& fp = pool[ok.faceColor(mustUse)][ok.faceNumber(mustUse)];
            moveToFront(fp.ids, fp.n, mustUse);
        }
    }

    Memo& memo = scratchMemo();
    memo.begin();
    SeriesSearch search(memo, weightsFor(objective));
    for (int c = 0; c < NUM_COLORS; ++c)
        for (int n = 1; n <= NUM_NUMBERS; ++n)
            for (int i = 0; i < pool[c][n].n; ++i) search.inc(slotOf(c, n));
    const int jokers = jokerCount;
    search.jokers_ = jokers;
    search.initJokers_ = jokers;
    if (hasMust) {
        search.mustDone_ = false;
        if (ok.isJoker(mustUse)) {
            search.mustJoker_ = true;
            search.mustDone_ = true;
        } else {
            const int c = ok.faceColor(mustUse), n = ok.faceNumber(mustUse);
            search.mustA_ = slotOf(c, n);
        }
    }

    const uint64_t start0 = search.cnt_[0], start1 = search.cnt_[1];
    const int best = search.solve();
    g_work += memo.size() + 1;
    if (best <= NEG) {
        res.feasible = false;
        res.leftovers = input;
        return res;
    }

    std::vector<AbsMeld> abs;
    search.cnt_[0] = start0;
    search.cnt_[1] = start1;
    search.jokers_ = jokers;
    search.mustDone_ = !hasMust || (hasMust && ok.isJoker(mustUse));
    search.reconstruct(abs);

    // Assign physical ids: the must tile sits at the front of its pool, so it is used first.
    int poolPos[NUM_COLORS][NUM_NUMBERS + 1] = {};
    int jokerPos = 0;
    auto takeFace = [&](int c, int n) { return pool[c][n].ids[poolPos[c][n]++]; };
    res.melds.reserve(abs.size());
    for (const AbsMeld& m : abs) {
        std::vector<int> ids;
        if (m.run) {
            for (int p = m.s0; p <= m.e; ++p) {
                if (m.jokerMask & (1u << (p - 1))) ids.push_back(jokerPool[jokerPos++]);
                else ids.push_back(takeFace(m.color, p));
            }
            int v = 0;
            for (int p = m.s0; p <= m.e; ++p) v += p;
            res.value += v;
        } else {
            ids.push_back(takeFace(m.color, m.number));
            for (int c2 = 0; c2 < NUM_COLORS; ++c2)
                if (m.colorMask & (1u << c2)) ids.push_back(takeFace(c2, m.number));
            for (int j = 0; j < m.groupJokers; ++j) ids.push_back(jokerPool[jokerPos++]);
            res.value += m.number * (int)ids.size();
        }
        res.tilesUsed += (int)ids.size();
        res.melds.push_back(std::move(ids));
    }
    for (int c = 0; c < NUM_COLORS; ++c)
        for (int n = 1; n <= NUM_NUMBERS; ++n)
            for (int i = poolPos[c][n]; i < pool[c][n].n; ++i) res.leftovers.push_back(pool[c][n].ids[i]);
    for (int i = jokerPos; i < jokerCount; ++i) res.leftovers.push_back(jokerPool[i]);
    res.leftovers.insert(res.leftovers.end(), extra.begin(), extra.end());
    for (int id : rejected) res.leftovers.push_back(id);
    return res;
}

uint64_t detail::solverWork() { return g_work; }

SolveResult solveSeries(const std::vector<int>& tiles, const OkeyInfo& ok, int mustUse) {
    return detail::solveSeriesObjective(tiles, ok, mustUse, 0);
}

SolveResult solvePairs(const std::vector<int>& input, const OkeyInfo& ok, int mustUse) {
    SolveResult res;
    std::vector<int> rejected;
    const std::vector<int> tiles = cleanTiles(input, rejected);
    if (mustUse >= 0 && !contains(tiles, mustUse)) {
        res.feasible = false;
        res.leftovers = input;
        return res;
    }

    std::vector<int> pool[NUM_COLORS][NUM_NUMBERS + 1];
    std::vector<int> jokers;
    for (int id : tiles) {
        if (ok.isJoker(id)) jokers.push_back(id);
        else pool[ok.faceColor(id)][ok.faceNumber(id)].push_back(id);
    }
    const bool mustJoker = mustUse >= 0 && ok.isJoker(mustUse);
    if (mustJoker) {
        auto it = std::find(jokers.begin(), jokers.end(), mustUse);
        std::rotate(jokers.begin(), it, it + 1);
    }
    int mustC = -1, mustN = -1;
    if (mustUse >= 0 && !mustJoker) {
        mustC = ok.faceColor(mustUse);
        mustN = ok.faceNumber(mustUse);
        auto& pl = pool[mustC][mustN];
        auto it = std::find(pl.begin(), pl.end(), mustUse);
        std::rotate(pl.begin(), it, it + 1);
    }

    std::vector<std::vector<int>> pairs;
    std::vector<int> singles; // one per face, the must tile first when it is single
    for (int c = 0; c < NUM_COLORS; ++c) {
        for (int n = 1; n <= NUM_NUMBERS; ++n) {
            auto& pl = pool[c][n];
            size_t i = 0;
            for (; i + 1 < pl.size(); i += 2) pairs.push_back({pl[i], pl[i + 1]});
            for (; i < pl.size(); ++i) singles.push_back(pl[i]);
        }
    }
    // Okeys pair with the highest singles (they would cost the most points left in hand).
    std::stable_sort(singles.begin(), singles.end(), [&ok](int a, int b) {
        const int na = ok.faceNumber(a), nb = ok.faceNumber(b);
        if (na != nb) return na > nb;
        return ok.faceColor(a) < ok.faceColor(b);
    });
    if (mustUse >= 0 && !mustJoker) {
        auto it = std::find(singles.begin(), singles.end(), mustUse);
        if (it != singles.end()) {
            if (jokers.empty()) {
                res.feasible = false;
                res.leftovers = input;
                return res;
            }
            std::rotate(singles.begin(), it, it + 1);
        }
    }
    size_t jk = 0;
    std::vector<int> leftover;
    for (int s : singles) {
        if (jk < jokers.size()) pairs.push_back({s, jokers[jk++]});
        else leftover.push_back(s);
    }
    if (jokers.size() - jk >= 2) {
        pairs.push_back({jokers[jk], jokers[jk + 1]});
        jk += 2;
    }
    if (mustJoker && jk == 0) {
        // A lone okey with nothing single to pair: break the lowest natural pair (same pair count).
        int bestIdx = -1;
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (bestIdx < 0 || ok.faceNumber(pairs[i][0]) < ok.faceNumber(pairs[bestIdx][0])) bestIdx = (int)i;
        }
        if (bestIdx < 0) {
            res.feasible = false;
            res.leftovers = input;
            return res;
        }
        leftover.push_back(pairs[bestIdx][1]);
        pairs[bestIdx][1] = jokers[jk++];
    }
    for (; jk < jokers.size(); ++jk) leftover.push_back(jokers[jk]);
    for (int id : rejected) leftover.push_back(id);

    std::sort(pairs.begin(), pairs.end(), [&ok](const std::vector<int>& a, const std::vector<int>& b) {
        return faceLess(a[0], b[0], ok);
    });
    res.melds = std::move(pairs);
    res.value = (int)res.melds.size();
    res.tilesUsed = 2 * res.value;
    res.leftovers = std::move(leftover);
    return res;
}

RackArrangement arrangeSeries(const std::vector<int>& tiles, const OkeyInfo& ok) {
    SolveResult r = solveSeries(tiles, ok);
    RackArrangement a;
    struct Keyed {
        int kind, color, number;
        std::vector<int> ids;
    };
    std::vector<Keyed> keyed;
    for (auto& ids : r.melds) {
        Meld m;
        Keyed k{0, 0, 0, ids};
        if (makeMeld(ids, ok, m, false) && !m.tiles.empty()) {
            k.kind = m.kind == MeldKind::Run ? 0 : 1;
            k.color = m.kind == MeldKind::Run ? m.tiles.front().color : 0;
            k.number = m.tiles.front().number;
        }
        keyed.push_back(std::move(k));
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& x, const Keyed& y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        if (x.color != y.color) return x.color < y.color;
        return x.number < y.number;
    });
    for (Keyed& k : keyed) a.melds.push_back(std::move(k.ids));
    a.leftovers = std::move(r.leftovers);
    sortTiles(a.leftovers, ok);
    return a;
}

RackArrangement arrangePairs(const std::vector<int>& tiles, const OkeyInfo& ok) {
    SolveResult r = solvePairs(tiles, ok);
    RackArrangement a;
    a.melds = std::move(r.melds);
    a.leftovers = std::move(r.leftovers);
    sortTiles(a.leftovers, ok);
    return a;
}

} // namespace okey
