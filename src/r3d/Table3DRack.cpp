// Istaka model (pure logic, ported from the proven 2D TableView): slots, groups, arrangement, moves.
#include "r3d/Table3DInternal.h"

#include <algorithm>

namespace r3d {
namespace t3d {

std::vector<RackGroup> parseGroups(const Slots& s) {
    std::vector<RackGroup> out;
    for (int row = 0; row < ROWS; ++row) {
        int col = 0;
        while (col < COLS) {
            if (s[row * COLS + col] < 0) {
                ++col;
                continue;
            }
            RackGroup g;
            g.row = row;
            g.col = col;
            while (col < COLS && s[row * COLS + col] >= 0) g.ids.push_back(s[row * COLS + col++]);
            g.len = (int)g.ids.size();
            out.push_back(std::move(g));
        }
    }
    return out;
}

namespace {
bool slotFree(const Slots& s, int row, int col) { return col < 0 || col >= COLS || s[row * COLS + col] < 0; }
bool freeAt(const int* r, int c) { return c < 0 || c >= COLS || r[c] < 0; }
} // namespace

bool layoutArrangement(const std::vector<std::vector<int>>& melds, const std::vector<int>& leftovers, Slots& out) {
    int total = (int)leftovers.size();
    for (const std::vector<int>& m : melds) total += (int)m.size();
    if (total > SLOTS) return false;
    Slots s;
    s.fill(-1);
    std::array<bool, SLOTS> meldCell{};
    // melds as blocks with one gap after each; a meld that would cross the row end starts the next row
    // (a row of 16 holds at most 5 separated pairs, so 11 pairs cannot all stay apart: what does not fit
    // is placed with the leftovers below)
    std::vector<int> rest;
    int row = 0, col = 0;
    for (const std::vector<int>& m : melds) {
        const int n = (int)m.size();
        if (n > COLS) { // longer than a row (cannot happen in 101): it goes with the rest, wrapping
            for (int id : m) rest.push_back(id);
            continue;
        }
        if (col + n > COLS) {
            if (row + 1 >= ROWS) {
                for (int id : m) rest.push_back(id);
                continue;
            }
            ++row;
            col = 0;
        }
        for (int id : m) {
            s[row * COLS + col] = id;
            meldCell[row * COLS + col] = true;
            ++col;
        }
        if (col < COLS) ++col; // the gap
    }
    for (int id : leftovers) rest.push_back(id);
    // the usual case: the rest in one run right after the last meld (wrapping onto the next row)
    {
        Slots t = s;
        int r = row, c = col;
        bool ok = true;
        for (int id : rest) {
            if (c >= COLS) {
                ++r;
                c = 0;
            }
            if (r >= ROWS || t[r * COLS + c] >= 0) {
                ok = false;
                break;
            }
            t[r * COLS + c++] = id;
        }
        if (ok) {
            out = t;
            return true;
        }
    }
    // no room for that: every remaining tile goes to a free cell that does not touch a meld (so no
    // meld grows or merges), else to any free cell
    auto touchesMeld = [&](int i) {
        const int c = i % COLS;
        return (c > 0 && meldCell[i - 1]) || (c + 1 < COLS && meldCell[i + 1]);
    };
    size_t k = 0;
    for (int pass = 0; pass < 2 && k < rest.size(); ++pass)
        for (int i = 0; i < SLOTS && k < rest.size(); ++i)
            if (s[i] < 0 && (pass == 1 || !touchesMeld(i))) s[i] = rest[k++];
    if (k < rest.size()) return false;
    out = s;
    return true;
}

int chooseFreeSlot(const Slots& s, int pref) {
    if (pref >= 0 && pref < SLOTS && s[pref] < 0) return pref;
    for (int row = 0; row < ROWS; ++row)
        for (int col = COLS - 1; col >= 0; --col)
            if (s[row * COLS + col] < 0 && slotFree(s, row, col - 1) && slotFree(s, row, col + 1))
                return row * COLS + col;
    for (int row = 0; row < ROWS; ++row)
        for (int col = COLS - 1; col >= 0; --col)
            if (s[row * COLS + col] < 0) return row * COLS + col;
    return -1;
}

bool insertBefore(Slots& s, int row, int p, int id) {
    if (row < 0 || row >= ROWS) return false;
    p = std::clamp(p, 0, COLS);
    int* r = &s[row * COLS];
    auto shiftRight = [&](int R) { // r[R] free, R >= p: [p, R) moves one to the right
        for (int k = R; k > p; --k) r[k] = r[k - 1];
        r[p] = id;
    };
    auto shiftLeft = [&](int L) { // r[L] free, L < p: (L, p) moves one to the left
        for (int k = L; k < p - 1; ++k) r[k] = r[k + 1];
        r[p - 1] = id;
    };
    // 1. push toward a free slot that is followed by another free slot (or the row end): every group on
    //    the way moves together with the gap after it, so no two groups merge
    for (int R = p; R < COLS; ++R)
        if (r[R] < 0 && freeAt(r, R + 1)) {
            shiftRight(R);
            return true;
        }
    for (int L = p - 1; L >= 0; --L)
        if (r[L] < 0 && freeAt(r, L - 1)) {
            shiftLeft(L);
            return true;
        }
    // 2. a crowded row: any free slot (neighbouring groups may touch)
    for (int R = p; R < COLS; ++R)
        if (r[R] < 0) {
            shiftRight(R);
            return true;
        }
    for (int L = p - 1; L >= 0; --L)
        if (r[L] < 0) {
            shiftLeft(L);
            return true;
        }
    return false;
}

bool moveTile(Slots& s, int from, int to, bool after) {
    if (from < 0 || from >= SLOTS || to < 0 || to >= SLOTS || s[from] < 0) return false;
    if (from == to) return true;
    const int id = s[from];
    if (s[to] < 0) {
        s[from] = -1;
        s[to] = id;
        return true;
    }
    const int row = to / COLS, p = to % COLS + (after ? 1 : 0);
    // inside one unbroken stretch of the same row: a list move (the tiles in between close up, no hole)
    if (from / COLS == row) {
        int* r = &s[row * COLS];
        const int fc = from % COLS;
        bool full = true;
        if (p > fc) {
            for (int k = fc + 1; k < p; ++k) full = full && r[k] >= 0;
            if (full) {
                for (int k = fc; k < p - 1; ++k) r[k] = r[k + 1];
                r[p - 1] = id;
                return true;
            }
        } else {
            for (int k = p; k < fc; ++k) full = full && r[k] >= 0;
            if (full) {
                for (int k = fc; k > p; --k) r[k] = r[k - 1];
                r[p] = id;
                return true;
            }
        }
    }
    s[from] = -1;
    if (insertBefore(s, row, p, id)) return true;
    s[from] = s[to]; // the target row is full: swap
    s[to] = id;
    return true;
}

} // namespace t3d
} // namespace r3d
