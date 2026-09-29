// Meld validation and işleme for 101 Okey. See Meld.h for the exact semantics.
#include "core/Meld.h"

#include <algorithm>
#include <utility>

namespace okey {

namespace {

int numberValue(int number) { return number == ACE_HIGH_NUMBER ? ACE_HIGH_VALUE : number; }

void setWhy(std::string* why, const char* msg) {
    if (why) *why = msg;
}

// Shared sanity checks: every id is a real tile and no physical tile appears twice.
bool checkIds(const std::vector<int>& ids, std::string* why) {
    for (size_t i = 0; i < ids.size(); ++i) {
        if (!isValidTile(ids[i])) {
            setWhy(why, "Geçersiz taş");
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (ids[j] == ids[i]) {
                setWhy(why, "Aynı taş iki kez kullanılamaz");
                return false;
            }
        }
    }
    return true;
}

PlacedTile place(int id, int color, int number, bool joker) {
    PlacedTile t;
    t.id = id;
    t.color = color;
    t.number = number;
    t.joker = joker;
    return t;
}

// Can a non-wild tile with face number `face` stand at run position `pos` (1..14)?
bool faceFitsPosition(int face, int pos) {
    return face == pos || (face == 1 && pos == ACE_HIGH_NUMBER);
}

// Strict positional ascending reading: the i-th tile is number start+i. Returns the best valid reading.
bool readAscending(const std::vector<int>& ids, const OkeyInfo& ok, int color, Meld& out) {
    const int n = (int)ids.size();
    int first = -1;
    for (int i = 0; i < n; ++i) {
        if (!ok.isJoker(ids[i])) {
            first = i;
            break;
        }
    }
    int starts[2];
    int numStarts = 0;
    if (first < 0) {
        starts[numStarts++] = ACE_HIGH_NUMBER - n + 1; // only wild tiles: highest possible reading
    } else {
        const int f = ok.faceNumber(ids[first]);
        starts[numStarts++] = f - first;
        if (f == 1) starts[numStarts++] = ACE_HIGH_NUMBER - first; // the '1' read as a high ace
    }

    bool found = false;
    int bestValue = -1;
    for (int k = 0; k < numStarts; ++k) {
        const int s = starts[k];
        if (s < 1 || s + n - 1 > ACE_HIGH_NUMBER) continue;
        Meld m;
        m.kind = MeldKind::Run;
        bool legal = true;
        for (int i = 0; i < n && legal; ++i) {
            const int id = ids[i];
            const int pos = s + i;
            const bool joker = ok.isJoker(id);
            if (!joker && !faceFitsPosition(ok.faceNumber(id), pos)) legal = false;
            m.tiles.push_back(place(id, color, pos, joker));
        }
        if (legal && m.value() > bestValue) {
            bestValue = m.value();
            out = std::move(m);
            found = true;
        }
    }
    return found;
}

// Lenient reading: non-jokers sorted by number, jokers fill the internal gaps, then extend the high end
// (up to 14), then the low end. Each '1' may be read as 1 or as the high ace. Best value wins.
bool readLenient(const std::vector<int>& ids, const OkeyInfo& ok, int color, Meld& out) {
    std::vector<int> plain, jokers, ones;
    for (int id : ids) {
        if (ok.isJoker(id)) {
            jokers.push_back(id);
        } else {
            if (ok.faceNumber(id) == 1) ones.push_back((int)plain.size());
            plain.push_back(id);
        }
    }
    if (plain.empty() || ones.size() > 3) return false;

    bool found = false;
    int bestValue = -1;
    const int combos = 1 << ones.size();
    for (int mask = 0; mask < combos; ++mask) {
        std::vector<std::pair<int, int>> numbered; // (represented number, id)
        for (size_t i = 0; i < plain.size(); ++i) numbered.push_back({ok.faceNumber(plain[i]), plain[i]});
        for (size_t b = 0; b < ones.size(); ++b)
            if (mask & (1 << b)) numbered[ones[b]].first = ACE_HIGH_NUMBER;
        std::sort(numbered.begin(), numbered.end());

        bool distinct = true;
        for (size_t i = 1; i < numbered.size(); ++i)
            if (numbered[i].first == numbered[i - 1].first) distinct = false;
        if (!distinct) continue;

        int lo = numbered.front().first;
        int hi = numbered.back().first;
        const int gaps = hi - lo + 1 - (int)numbered.size();
        if (gaps > (int)jokers.size()) continue;
        int spare = (int)jokers.size() - gaps;
        while (spare > 0 && hi < ACE_HIGH_NUMBER) {
            ++hi;
            --spare;
        }
        while (spare > 0 && lo > 1) {
            --lo;
            --spare;
        }
        if (spare > 0) continue;

        Meld m;
        m.kind = MeldKind::Run;
        size_t next = 0, nextJoker = 0;
        for (int pos = lo; pos <= hi; ++pos) {
            if (next < numbered.size() && numbered[next].first == pos) {
                m.tiles.push_back(place(numbered[next].second, color, pos, false));
                ++next;
            } else {
                m.tiles.push_back(place(jokers[nextJoker++], color, pos, true));
            }
        }
        if (m.value() > bestValue) {
            bestValue = m.value();
            out = std::move(m);
            found = true;
        }
    }
    return found;
}

bool containsId(const Meld& m, int id) {
    for (const PlacedTile& t : m.tiles)
        if (t.id == id) return true;
    return false;
}

// Non-allocating equivalent of tryAddTile(m, id, ok, AddSide::Auto, out) for a valid meld.
bool canAdd(const Meld& m, int id, const OkeyInfo& ok) {
    if (!isValidTile(id) || m.tiles.empty() || m.kind == MeldKind::Pair || containsId(m, id)) return false;
    const bool joker = ok.isJoker(id);
    if (m.kind == MeldKind::Group) {
        if (m.size() >= NUM_COLORS) return false;
        if (joker) return true;
        if (ok.faceNumber(id) != m.tiles.front().number) return false;
        const int c = ok.faceColor(id);
        for (const PlacedTile& t : m.tiles)
            if (!t.joker && t.color == c) return false;
        return true;
    }
    const int backPos = m.tiles.back().number + 1;
    const int frontPos = m.tiles.front().number - 1;
    if (joker) return backPos <= ACE_HIGH_NUMBER || frontPos >= 1;
    if (ok.faceColor(id) != m.tiles.front().color) return false;
    const int f = ok.faceNumber(id);
    return (backPos <= ACE_HIGH_NUMBER && faceFitsPosition(f, backPos)) || (frontPos >= 1 && f == frontPos);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------

int Meld::value() const {
    int v = 0;
    for (const PlacedTile& t : tiles) v += numberValue(t.number);
    return v;
}

bool Meld::hasJoker() const {
    for (const PlacedTile& t : tiles)
        if (t.joker) return true;
    return false;
}

std::vector<int> Meld::ids() const {
    std::vector<int> v;
    v.reserve(tiles.size());
    for (const PlacedTile& t : tiles) v.push_back(t.id);
    return v;
}

bool makeRun(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, bool lenient, std::string* why) {
    if (!checkIds(ids, why)) return false;
    const int n = (int)ids.size();
    if (n < 3) {
        setWhy(why, "Seri en az 3 taş olmalı");
        return false;
    }
    if (n > ACE_HIGH_NUMBER) {
        setWhy(why, "Seri en fazla 14 taş olabilir");
        return false;
    }

    int color = -1;
    int count[ACE_HIGH_NUMBER + 1] = {};
    for (int id : ids) {
        if (ok.isJoker(id)) continue;
        const int c = ok.faceColor(id);
        if (color < 0) {
            color = c;
        } else if (c != color) {
            setWhy(why, "Seri aynı renkten olmalı");
            return false;
        }
        ++count[ok.faceNumber(id)];
    }
    for (int num = 1; num <= NUM_NUMBERS; ++num) {
        // Two '1's are fine (1 at the start, high ace at the end); anything else must be unique.
        if (count[num] > (num == 1 ? 2 : 1)) {
            setWhy(why, "Seride aynı sayı iki kez olamaz");
            return false;
        }
    }
    if (color < 0) color = ok.color;

    Meld m;
    bool okRun = readAscending(ids, ok, color, m);
    if (!okRun) {
        std::vector<int> rev(ids.rbegin(), ids.rend());
        okRun = readAscending(rev, ok, color, m);
    }
    if (!okRun && lenient) okRun = readLenient(ids, ok, color, m);
    if (!okRun) {
        setWhy(why, "Sayılar ardışık değil");
        return false;
    }
    m.kind = MeldKind::Run;
    m.owner = -1;
    out = std::move(m);
    return true;
}

bool makeGroup(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, std::string* why) {
    if (!checkIds(ids, why)) return false;
    const int n = (int)ids.size();
    if (n < 3) {
        setWhy(why, "Grup en az 3 taş olmalı");
        return false;
    }
    if (n > NUM_COLORS) {
        setWhy(why, "Grup en fazla 4 taş olabilir");
        return false;
    }
    int number = -1;
    for (int id : ids) {
        if (ok.isJoker(id)) continue;
        const int num = ok.faceNumber(id);
        if (number < 0) {
            number = num;
        } else if (num != number) {
            setWhy(why, "Gruptaki sayılar aynı olmalı");
            return false;
        }
    }
    bool used[NUM_COLORS] = {};
    for (int id : ids) {
        if (ok.isJoker(id)) continue;
        const int c = ok.faceColor(id);
        if (used[c]) {
            setWhy(why, "Grupta aynı renk iki kez olamaz");
            return false;
        }
        used[c] = true;
    }
    if (number < 0) number = ok.number;

    Meld m;
    m.kind = MeldKind::Group;
    int nextColor = 0;
    for (int id : ids) {
        if (!ok.isJoker(id)) {
            m.tiles.push_back(place(id, ok.faceColor(id), number, false));
        } else {
            while (nextColor < NUM_COLORS && used[nextColor]) ++nextColor;
            // n <= 4 guarantees a free color for every joker.
            m.tiles.push_back(place(id, nextColor, number, true));
            used[nextColor] = true;
        }
    }
    std::stable_sort(m.tiles.begin(), m.tiles.end(),
                     [](const PlacedTile& a, const PlacedTile& b) { return a.color < b.color; });
    out = std::move(m);
    return true;
}

bool makePair(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, std::string* why) {
    if (!checkIds(ids, why)) return false;
    if (ids.size() != 2) {
        setWhy(why, "Çift iki taştan oluşmalı");
        return false;
    }
    const int a = ids[0], b = ids[1];
    const bool ja = ok.isJoker(a), jb = ok.isJoker(b);
    int color, number;
    if (ja && jb) {
        color = ok.color;
        number = ok.number;
    } else if (ja) {
        color = ok.faceColor(b);
        number = ok.faceNumber(b);
    } else {
        color = ok.faceColor(a);
        number = ok.faceNumber(a);
        if (!jb && (ok.faceColor(b) != color || ok.faceNumber(b) != number)) {
            setWhy(why, "Çiftin taşları aynı olmalı");
            return false;
        }
    }
    Meld m;
    m.kind = MeldKind::Pair;
    m.tiles.push_back(place(a, color, number, ja));
    m.tiles.push_back(place(b, color, number, jb));
    out = std::move(m);
    return true;
}

bool makeMeld(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, bool pairMode, std::string* why) {
    if (pairMode) return makePair(ids, ok, out, why);
    if (!checkIds(ids, why)) return false;
    if (ids.size() < 3) {
        setWhy(why, "Per en az 3 taş olmalı");
        return false;
    }
    Meld group, run;
    std::string whyGroup, whyRun;
    const bool okGroup = makeGroup(ids, ok, group, &whyGroup);
    const bool okRun = makeRun(ids, ok, run, true, &whyRun);
    if (okGroup && okRun) {
        out = (group.value() > run.value()) ? std::move(group) : std::move(run);
        return true;
    }
    if (okGroup) {
        out = std::move(group);
        return true;
    }
    if (okRun) {
        out = std::move(run);
        return true;
    }
    if (why) {
        // Report the reason of the interpretation the player most likely meant.
        int number = -1;
        bool sameNumber = true;
        for (int id : ids) {
            if (ok.isJoker(id)) continue;
            if (number < 0) number = ok.faceNumber(id);
            else if (ok.faceNumber(id) != number) sameNumber = false;
        }
        *why = sameNumber ? whyGroup : whyRun;
    }
    return false;
}

bool tryAddTile(const Meld& m, int id, const OkeyInfo& ok, AddSide side, Meld& out) {
    if (!isValidTile(id) || m.tiles.empty() || m.kind == MeldKind::Pair) return false;
    if (containsId(m, id)) return false;

    if (m.kind == MeldKind::Group) {
        if (m.size() >= NUM_COLORS) return false;
        std::vector<int> ids = m.ids();
        ids.push_back(id);
        Meld g;
        if (!makeGroup(ids, ok, g)) return false;
        g.owner = m.owner;
        out = std::move(g);
        return true;
    }

    // Run
    const int color = m.tiles.front().color;
    const bool joker = ok.isJoker(id);
    const int backPos = m.tiles.back().number + 1;
    const int frontPos = m.tiles.front().number - 1;
    const bool canBack = backPos <= ACE_HIGH_NUMBER &&
                         (joker || (ok.faceColor(id) == color && faceFitsPosition(ok.faceNumber(id), backPos)));
    const bool canFront = frontPos >= 1 && (joker || (ok.faceColor(id) == color && ok.faceNumber(id) == frontPos));

    bool toBack;
    if (side == AddSide::Back) {
        if (!canBack) return false;
        toBack = true;
    } else if (side == AddSide::Front) {
        if (!canFront) return false;
        toBack = false;
    } else {
        if (!canBack && !canFront) return false;
        toBack = canBack;
    }
    Meld r = m;
    if (toBack) r.tiles.push_back(place(id, color, backPos, joker));
    else r.tiles.insert(r.tiles.begin(), place(id, color, frontPos, joker));
    out = std::move(r);
    return true;
}

bool trySwapJoker(const Meld& m, int id, const OkeyInfo& ok, Meld& out, int& freedJoker) {
    if (!isValidTile(id) || ok.isJoker(id) || m.kind == MeldKind::Pair || !m.hasJoker()) return false;
    if (containsId(m, id)) return false;
    const int fc = ok.faceColor(id);
    const int fn = ok.faceNumber(id);

    if (m.kind == MeldKind::Run) {
        if (fc != m.tiles.front().color) return false;
        for (size_t i = 0; i < m.tiles.size(); ++i) {
            const PlacedTile& t = m.tiles[i];
            if (t.joker && faceFitsPosition(fn, t.number)) {
                Meld r = m;
                freedJoker = t.id;
                r.tiles[i] = place(id, t.color, t.number, false);
                out = std::move(r);
                return true;
            }
        }
        return false;
    }

    // Group: the tile must be a missing color of the group's number.
    if (fn != m.tiles.front().number) return false;
    int pick = -1;
    for (size_t i = 0; i < m.tiles.size(); ++i) {
        const PlacedTile& t = m.tiles[i];
        if (!t.joker && t.color == fc) return false; // that color is already a real tile
        if (t.joker && t.color == fc) pick = (int)i;  // the joker standing for exactly this color
    }
    if (pick < 0) {
        for (size_t i = 0; i < m.tiles.size(); ++i) {
            if (m.tiles[i].joker) {
                pick = (int)i;
                break;
            }
        }
    }
    std::vector<int> ids = m.ids();
    const int joker = ids[pick];
    ids[pick] = id;
    Meld g;
    if (!makeGroup(ids, ok, g)) return false;
    g.owner = m.owner;
    out = std::move(g);
    freedJoker = joker;
    return true;
}

bool fitsAnyMeld(const std::vector<Meld>& table, int id, const OkeyInfo& ok) {
    for (const Meld& m : table)
        if (canAdd(m, id, ok)) return true;
    return false;
}

} // namespace okey
