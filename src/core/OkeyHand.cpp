// Klasik okey hand analysis (see OkeyHand.h).
#include "core/OkeyHand.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace okey {

namespace {

constexpr int FACES = NUM_COLORS * NUM_NUMBERS; // face index = color * 13 + (number - 1)

inline int faceIndex(int c, int n) { return c * NUM_NUMBERS + (n - 1); }

// A hand as face counts (non-wild tiles) plus the number of okeys.
struct Counts {
    std::array<uint8_t, FACES> n{};
    int jokers = 0;
};

Counts countsOf(const std::vector<int>& tiles, const OkeyInfo& ok) {
    Counts k;
    for (int id : tiles) {
        if (!isValidTile(id)) continue;
        if (ok.isJoker(id)) ++k.jokers;
        else ++k.n[faceIndex(ok.faceColor(id), ok.faceNumber(id))];
    }
    return k;
}

struct Key {
    uint64_t a, b;
    bool operator==(const Key& o) const { return a == o.a && b == o.b; }
};
struct KeyHash {
    size_t operator()(const Key& k) const { return (size_t)(k.a * 0x9E3779B97F4A7C15ull ^ (k.b + (k.a >> 31))); }
};

Key keyOf(const Counts& k) {
    Key key{0, 0};
    for (int f = 0; f < FACES; ++f) {
        const uint64_t v = std::min<uint64_t>(k.n[f], 3);
        if (f < 32) key.a |= v << (2 * f);
        else key.b |= v << (2 * (f - 32));
    }
    key.b |= (uint64_t)k.jokers << 48;
    return key;
}

// A meld as faces (face index or -1 for a joker), in display order.
using FaceMeld = std::vector<int>;

// Candidate melds containing face `f` (the lowest remaining face of its color: no lower number of that color is
// left), built greedily from real tiles, jokers filling what is missing.
void candidates(const Counts& k, int f, std::vector<FaceMeld>& out) {
    out.clear();
    const int c = f / NUM_NUMBERS, n = f % NUM_NUMBERS + 1;
    // groups: n in color c plus 2..3 of the other colors (real or joker)
    for (int mask = 0; mask < 16; ++mask) {
        if (!(mask & (1 << c))) continue;
        const int size = __builtin_popcount((unsigned)mask);
        if (size < 3) continue;
        FaceMeld m;
        int jokersNeeded = 0;
        for (int c2 = 0; c2 < NUM_COLORS; ++c2) {
            if (!(mask & (1 << c2))) continue;
            const int g = faceIndex(c2, n);
            if (c2 == c || k.n[g] > 0) m.push_back(g);
            else {
                m.push_back(-1);
                ++jokersNeeded;
            }
        }
        if (jokersNeeded <= k.jokers) out.push_back(m);
    }
    // runs in color c: positions s..e (14 = the 1 after 13); the tile sits at n (or 14 when n == 1).
    auto addRuns = [&](int pos) {
        for (int s = std::max(1, pos - k.jokers); s <= pos; ++s) {
            int jokersUsed = pos - s; // nothing below `pos` of this color is left: jokers only
            if (jokersUsed > k.jokers) continue;
            FaceMeld m(pos - s, -1);
            m.push_back(f);
            for (int e = pos + 1; e <= 14 && e - s + 1 <= NUM_NUMBERS; ++e) {
                const int num = e == 14 ? 1 : e;
                if (e == 14 && s == 1) break; // 1-...-13-1 is not a run
                const int g = faceIndex(c, num);
                // the same face can't be both the run's tile `f` and a later position (only 1 at 14 repeats)
                const int have = k.n[g] - (g == f ? 1 : 0);
                if (have > 0) m.push_back(g);
                else {
                    if (jokersUsed >= k.jokers) break;
                    ++jokersUsed;
                    m.push_back(-1);
                }
                if (e - s + 1 >= 3) out.push_back(m);
            }
            // runs that end right at the tile (jokers before it)
            if (pos - s + 1 >= 3) out.push_back(FaceMeld(m.begin(), m.begin() + (pos - s + 1)));
        }
    };
    addRuns(n);
    if (n == 1) {
        // 1 as the last tile after 13: positions s..14, with real tiles or jokers at 12, 13, ...
        for (int s = 12; s >= 2; --s) {
            FaceMeld m;
            int jokersUsed = 0;
            bool okRun = true;
            for (int p = s; p <= 13; ++p) {
                const int g = faceIndex(c, p);
                if (k.n[g] > 0) m.push_back(g);
                else if (jokersUsed < k.jokers) {
                    ++jokersUsed;
                    m.push_back(-1);
                } else {
                    okRun = false;
                    break;
                }
            }
            if (!okRun) break;
            m.push_back(f);
            out.push_back(m);
        }
    }
}

bool take(Counts& k, const FaceMeld& m) {
    for (int g : m) {
        if (g < 0) {
            if (k.jokers <= 0) return false;
            --k.jokers;
        } else {
            if (k.n[g] == 0) return false;
            --k.n[g];
        }
    }
    return true;
}

struct Solver {
    std::unordered_map<Key, int, KeyHash> memo;

    // most tiles covered by melds
    int best(const Counts& k) {
        int f = -1;
        for (int i = 0; i < FACES; ++i)
            if (k.n[i]) {
                f = i;
                break;
            }
        if (f < 0) return 0; // only jokers left: they can't form a meld on their own here (they join others)
        const Key key = keyOf(k);
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
        // leave one copy of f over
        Counts r = k;
        --r.n[f];
        int v = best(r);
        std::vector<FaceMeld> cand;
        candidates(k, f, cand);
        for (const FaceMeld& m : cand) {
            Counts q = k;
            if (!take(q, m)) continue;
            v = std::max(v, (int)m.size() + best(q));
        }
        memo.emplace(key, v);
        return v;
    }

    // one split achieving best(k)
    void split(Counts k, std::vector<FaceMeld>& out) {
        for (;;) {
            int f = -1;
            for (int i = 0; i < FACES; ++i)
                if (k.n[i]) {
                    f = i;
                    break;
                }
            if (f < 0) return;
            const int target = best(k);
            std::vector<FaceMeld> cand;
            candidates(k, f, cand);
            bool used = false;
            for (const FaceMeld& m : cand) {
                Counts q = k;
                if (!take(q, m)) continue;
                if ((int)m.size() + best(q) == target) {
                    out.push_back(m);
                    k = q;
                    used = true;
                    break;
                }
            }
            if (!used) --k.n[f];
        }
    }
};

// Maps face melds back to physical ids from `tiles`.
std::vector<std::vector<int>> toIds(const std::vector<FaceMeld>& melds, const std::vector<int>& tiles, const OkeyInfo& ok) {
    std::vector<int> pool = tiles;
    std::vector<std::vector<int>> out;
    for (const FaceMeld& m : melds) {
        std::vector<int> ids;
        for (int g : m) {
            auto it = std::find_if(pool.begin(), pool.end(), [&](int id) {
                if (g < 0) return ok.isJoker(id);
                return !ok.isJoker(id) && faceIndex(ok.faceColor(id), ok.faceNumber(id)) == g;
            });
            if (it == pool.end()) continue;
            ids.push_back(*it);
            pool.erase(it);
        }
        out.push_back(ids);
    }
    return out;
}

// Value-only search with a cache that outlives the call: the state is the face counts, the okeys and whether a meld
// has been made (spare okeys at the end join it).
struct FastSolver {
    std::unordered_map<Key, int, KeyHash> memo;

    int best(const Counts& k, bool meld) {
        int f = -1;
        for (int i = 0; i < FACES; ++i)
            if (k.n[i]) {
                f = i;
                break;
            }
        if (f < 0) return meld ? k.jokers : 0;
        Key key = keyOf(k);
        if (meld) key.b |= 1ull << 60;
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
        Counts r = k;
        --r.n[f];
        int v = best(r, meld);
        std::vector<FaceMeld> cand;
        candidates(k, f, cand);
        for (const FaceMeld& m : cand) {
            Counts q = k;
            if (!take(q, m)) continue;
            v = std::max(v, (int)m.size() + best(q, true));
        }
        if (memo.size() > 250000) memo.clear(); // (bounded: a rehash of a huge table is a long stall)
        memo.emplace(key, v);
        return v;
    }
};

} // namespace

int classicCoverFast(const std::vector<int>& tiles, const OkeyInfo& ok) {
    thread_local FastSolver s; // the state is plain face counts: valid whatever the okey is
    return s.best(countsOf(tiles, ok), false);
}

std::vector<int> classicRun(const std::vector<int>& ids, const OkeyInfo& ok) {
    if (ids.size() < 3 || ids.size() > (size_t)NUM_NUMBERS) return {};
    int color = -1;
    std::vector<std::pair<int, int>> nums; // (number, id)
    for (int id : ids) {
        if (!isValidTile(id)) return {};
        if (ok.isJoker(id)) continue;
        const int c = ok.faceColor(id);
        if (color >= 0 && c != color) return {};
        color = c;
        nums.push_back({ok.faceNumber(id), id});
    }
    const int len = (int)ids.size();
    // try 1 as low, then 1 as high (after 13)
    for (int wrap = 0; wrap < 2; ++wrap) {
        std::vector<std::pair<int, int>> v = nums;
        if (wrap) {
            bool any1 = false;
            for (auto& p : v)
                if (p.first == 1) {
                    p.first = 14;
                    any1 = true;
                }
            if (!any1) continue;
        }
        std::sort(v.begin(), v.end());
        bool dup = false;
        for (size_t i = 1; i < v.size(); ++i) dup = dup || v[i].first == v[i - 1].first;
        if (dup) continue;
        int lo = v.empty() ? 1 : v.front().first, hi = v.empty() ? 3 : v.back().first;
        if (hi - lo + 1 > len) continue;
        // extend with the spare jokers: up first (while <= 13, or 14 when wrapping), then down
        int spare = len - (hi - lo + 1);
        const int top = wrap ? 14 : 13;
        while (spare > 0 && hi < top) {
            ++hi;
            --spare;
        }
        while (spare > 0 && lo > 1) {
            --lo;
            --spare;
        }
        if (spare > 0) continue;
        if (lo == 1 && hi == 14) continue;
        std::vector<int> out;
        std::vector<int> js;
        for (int id : ids)
            if (ok.isJoker(id)) js.push_back(id);
        size_t vi = 0;
        for (int p = lo; p <= hi; ++p) {
            if (vi < v.size() && v[vi].first == p) out.push_back(v[vi++].second);
            else {
                out.push_back(js.back());
                js.pop_back();
            }
        }
        return out;
    }
    return {};
}

bool classicMeldValid(const std::vector<int>& ids, const OkeyInfo& ok) {
    if (ids.size() < 3) return false;
    if (!classicRun(ids, ok).empty()) return true;
    if (ids.size() > 4) return false;
    int number = -1;
    bool used[NUM_COLORS] = {};
    for (int id : ids) {
        if (!isValidTile(id)) return false;
        if (ok.isJoker(id)) continue;
        const int c = ok.faceColor(id), n = ok.faceNumber(id);
        if (number >= 0 && n != number) return false;
        if (used[c]) return false;
        number = n;
        used[c] = true;
    }
    return true;
}

int classicCover(const std::vector<int>& tiles, const OkeyInfo& ok, std::vector<std::vector<int>>* melds) {
    const Counts k = countsOf(tiles, ok);
    Solver s;
    std::vector<FaceMeld> fm;
    s.split(k, fm);
    std::vector<std::vector<int>> ms = toIds(fm, tiles, ok);
    // `best` places okeys only beside real tiles: spare ones join any meld that can still take them
    std::vector<int> spare;
    for (int id : tiles) {
        if (!ok.isJoker(id)) continue;
        bool in = false;
        for (const auto& m : ms) in = in || std::find(m.begin(), m.end(), id) != m.end();
        if (!in) spare.push_back(id);
    }
    for (auto& m : ms) {
        while (!spare.empty()) {
            std::vector<int> t = m;
            t.push_back(spare.back());
            if (!classicMeldValid(t, ok)) break;
            const std::vector<int> r = classicRun(t, ok);
            m = r.empty() ? t : r;
            spare.pop_back();
        }
    }
    int v = 0;
    for (const auto& m : ms) v += (int)m.size();
    if (melds) *melds = std::move(ms);
    return v;
}

bool classicSetsComplete(const std::vector<int>& tiles, const OkeyInfo& ok, std::vector<std::vector<int>>* melds) {
    std::vector<std::vector<int>> m;
    const int v = classicCover(tiles, ok, &m);
    if (v != (int)tiles.size()) return false;
    if (melds) *melds = m;
    return true;
}

int classicPairCount(const std::vector<int>& tiles, const OkeyInfo& ok, std::vector<std::vector<int>>* pairs) {
    std::vector<int> jokers;
    std::array<std::vector<int>, FACES> byFace;
    for (int id : tiles) {
        if (!isValidTile(id)) continue;
        if (ok.isJoker(id)) jokers.push_back(id);
        else byFace[faceIndex(ok.faceColor(id), ok.faceNumber(id))].push_back(id);
    }
    int count = 0;
    std::vector<int> singles;
    for (int f = FACES - 1; f >= 0; --f) { // highest faces first (a joker pairs the highest single)
        auto& v = byFace[f];
        size_t i = 0;
        for (; i + 1 < v.size(); i += 2) {
            ++count;
            if (pairs) pairs->push_back({v[i], v[i + 1]});
        }
        if (i < v.size()) singles.push_back(v[i]);
    }
    std::sort(singles.begin(), singles.end(), [&](int a, int b) { return ok.faceNumber(a) > ok.faceNumber(b); });
    for (int sgl : singles) {
        if (jokers.empty()) break;
        ++count;
        if (pairs) pairs->push_back({sgl, jokers.back()});
        jokers.pop_back();
    }
    while (jokers.size() >= 2) {
        ++count;
        if (pairs) pairs->push_back({jokers[jokers.size() - 2], jokers.back()});
        jokers.resize(jokers.size() - 2);
    }
    return count;
}

bool classicPairsComplete(const std::vector<int>& tiles, const OkeyInfo& ok) {
    return tiles.size() == 14 && classicPairCount(tiles, ok) == 7;
}

int classicFinishKind(const std::vector<int>& hand15, int tile, const OkeyInfo& ok) {
    std::vector<int> rest = hand15;
    auto it = std::find(rest.begin(), rest.end(), tile);
    if (it == rest.end()) return 0;
    rest.erase(it);
    if (rest.size() != 14) return 0;
    if (classicPairsComplete(rest, ok)) return 2;
    if (classicSetsComplete(rest, ok)) return 1;
    return 0;
}

} // namespace okey
