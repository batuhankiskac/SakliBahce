// Konken engine (see Konken.h, docs/kurallar_konken.md).
#include "core/Konken.h"

#include <algorithm>
#include <cstdlib>

namespace konken {

// ---------------------------------------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------------------------------------

std::string cardNameTR(int c) {
    if (isJoker(c)) return "Joker";
    if (!isValidCard(c)) return "?";
    return kart::cardNameTR(faceOf(c));
}

std::string cardAccusativeTR(int c) {
    if (isJoker(c)) return "Jokeri";
    if (!isValidCard(c)) return "?";
    return kart::cardAccusativeTR(faceOf(c));
}

int handPoints(int c, int jokerPoints) {
    if (isJoker(c)) return jokerPoints;
    const int r = rankOf(c);
    if (r == kart::As) return 11;
    return r >= kart::Vale ? 10 : r;
}

int rankValue(int rank) {
    if (rank <= 1) return 1;
    if (rank >= 14) return 11;
    return rank >= 11 ? 10 : rank;
}

// ---------------------------------------------------------------------------------------------------------
// Melds
// ---------------------------------------------------------------------------------------------------------

int Meld::jokers() const {
    int n = 0;
    for (int c : cards) n += isJoker(c) ? 1 : 0;
    return n;
}

int Meld::value() const {
    int v = 0;
    for (int i = 0; i < size(); ++i) v += rankValue(rankAt(i));
    return v;
}

std::vector<int> Meld::missingSuits() const {
    std::vector<int> v;
    if (kind != MeldKind::Set) return v;
    bool has[4] = {false, false, false, false};
    for (int c : cards)
        if (!isJoker(c)) has[suitOf(c)] = true;
    for (int s = 0; s < 4; ++s)
        if (!has[s]) v.push_back(s);
    return v;
}

namespace {

// Does a real card show `rank` (1..14)? The As is both 1 and 14.
bool showsRank(int c, int rank) {
    const int r = rankOf(c);
    return r == rank || (r == 14 && rank == 1);
}

// `cards` exactly as ordered as a run starting at `start` (jokers where they are).
bool runAsOrdered(const std::vector<int>& cards, int start, int suit) {
    const int n = (int)cards.size();
    if (start < 1 || start + n - 1 > 14) return false;
    for (int i = 0; i < n; ++i) {
        const int c = cards[(size_t)i];
        if (isJoker(c)) continue;
        if (suitOf(c) != suit || !showsRank(c, start + i)) return false;
    }
    return true;
}

bool tryOrderedRun(const std::vector<int>& cards, int suit, Meld& out) {
    // the first real card fixes the start (an As may be the low or the high one)
    int i0 = -1;
    for (int i = 0; i < (int)cards.size(); ++i)
        if (!isJoker(cards[(size_t)i])) {
            i0 = i;
            break;
        }
    if (i0 < 0) return false;
    const int r = rankOf(cards[(size_t)i0]);
    const int starts[2] = {r - i0, r == 14 ? 1 - i0 : -100};
    for (int st : starts) {
        if (runAsOrdered(cards, st, suit)) {
            out.kind = MeldKind::Run;
            out.suit = suit;
            out.rank = st;
            out.cards = cards;
            return true;
        }
    }
    return false;
}

// Real cards sorted, jokers filling the gaps, spare jokers on the high end (then the low one). `aceLow`: every As is 1
// (else 14; with two Aces of the suit one may be each, for the full A..A run).
bool canonicalRun(const std::vector<int>& nat, int jokers, int suit, bool aceLow, Meld& out) {
    std::vector<std::pair<int, int>> rk; // (rank, card)
    int aces = 0;
    for (int c : nat) {
        int r = rankOf(c);
        if (r == 14) {
            ++aces;
            r = aceLow ? 1 : 14;
            if (aces == 2) r = aceLow ? 14 : 1; // the second As at the other end
        }
        rk.push_back({r, c});
    }
    std::sort(rk.begin(), rk.end());
    for (size_t i = 1; i < rk.size(); ++i)
        if (rk[i].first == rk[i - 1].first) return false;
    const int lo0 = rk.front().first, hi0 = rk.back().first;
    const int gaps = (hi0 - lo0 + 1) - (int)rk.size();
    if (gaps > jokers) return false;
    int spare = jokers - gaps;
    const int hi = std::min(14, hi0 + spare);
    spare -= hi - hi0;
    const int lo = lo0 - spare;
    if (lo < 1) return false;
    out.kind = MeldKind::Run;
    out.suit = suit;
    out.rank = lo;
    out.cards.clear();
    size_t k = 0;
    for (int r = lo; r <= hi; ++r) {
        if (k < rk.size() && rk[k].first == r) out.cards.push_back(rk[k++].second);
        else out.cards.push_back(-1); // a joker's place (the caller fills it)
    }
    return true;
}

} // namespace

bool makeMeld(const std::vector<int>& cards, Meld& out, std::string* why) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    out = Meld{};
    if (cards.size() < 3) return fail("Bir per en az üç kâğıttır");
    std::vector<int> nat, jk;
    for (int c : cards) {
        if (!isValidCard(c)) return fail("Geçersiz kâğıt");
        (isJoker(c) ? jk : nat).push_back(c);
    }
    for (size_t i = 0; i < cards.size(); ++i)
        for (size_t j = i + 1; j < cards.size(); ++j)
            if (cards[i] == cards[j]) return fail("Aynı kâğıt iki kez olmaz");
    if (nat.size() < 2) return fail("Bir perde en az iki gerçek kâğıt olmalı");
    // a set: one rank, different suits
    bool sameRank = true, sameSuit = true;
    for (int c : nat) {
        sameRank = sameRank && rankOf(c) == rankOf(nat[0]);
        sameSuit = sameSuit && suitOf(c) == suitOf(nat[0]);
    }
    if (sameRank) {
        if (cards.size() > 4) return fail("Bir grup en çok dört kâğıttır");
        bool seen[4] = {false, false, false, false};
        for (int c : nat) {
            if (seen[suitOf(c)]) return fail("Bir grupta aynı renkten iki kâğıt olmaz");
            seen[suitOf(c)] = true;
        }
        out.kind = MeldKind::Set;
        out.rank = rankOf(nat[0]);
        out.cards = cards;
        return true;
    }
    if (!sameSuit) return fail("Bu kâğıtlar ne grup ne seri oluyor");
    const int suit = suitOf(nat[0]);
    if (cards.size() > 14) return fail("Bir seri en çok on dört kâğıttır");
    // the order given (or reversed), jokers where they stand
    if (tryOrderedRun(cards, suit, out)) return true;
    {
        std::vector<int> rev(cards.rbegin(), cards.rend());
        if (tryOrderedRun(rev, suit, out)) return true;
    }
    // the engine's arrangement: the As low or high, whichever is worth more
    Meld best;
    bool found = false;
    for (int low = 0; low < 2; ++low) {
        Meld m;
        if (!canonicalRun(nat, (int)jk.size(), suit, low == 1, m)) continue;
        size_t j = 0;
        for (int& c : m.cards)
            if (c < 0) c = jk[j++];
        if (!found || m.value() > best.value()) {
            best = m;
            found = true;
        }
    }
    if (!found) return fail("Seri sıralı olmalı (aradaki boşluklar jokerle dolabilir)");
    out = best;
    return true;
}

bool canAdd(const Meld& m, int card, Side side, Side* used) {
    if (!isValidCard(card)) return false;
    for (int c : m.cards)
        if (c == card) return false;
    if (m.kind == MeldKind::Set) {
        if (m.size() >= 4) return false;
        if (!isJoker(card)) {
            if (rankOf(card) != m.rank) return false;
            for (int c : m.cards)
                if (!isJoker(c) && suitOf(c) == suitOf(card)) return false;
        }
        if (used) *used = Side::Back;
        return true;
    }
    auto fits = [&](int rank) {
        if (rank < 1 || rank > 14) return false;
        return isJoker(card) || (suitOf(card) == m.suit && showsRank(card, rank));
    };
    if ((side == Side::Back || side == Side::Auto) && fits(m.high() + 1)) {
        if (used) *used = Side::Back;
        return true;
    }
    if ((side == Side::Front || side == Side::Auto) && fits(m.low() - 1)) {
        if (used) *used = Side::Front;
        return true;
    }
    return false;
}

void addCard(Meld& m, int card, Side side) {
    Side s = side;
    if (!canAdd(m, card, side, &s)) return;
    if (m.kind == MeldKind::Run && s == Side::Front) {
        m.cards.insert(m.cards.begin(), card);
        m.rank -= 1;
    } else {
        m.cards.push_back(card);
    }
}

int swapIndex(const Meld& m, int card) {
    if (!isValidCard(card) || isJoker(card)) return -1;
    if (m.kind == MeldKind::Set) {
        if (rankOf(card) != m.rank) return -1;
        const std::vector<int> miss = m.missingSuits();
        if (std::find(miss.begin(), miss.end(), suitOf(card)) == miss.end()) return -1;
        for (int i = 0; i < m.size(); ++i)
            if (isJoker(m.cards[(size_t)i])) return i;
        return -1;
    }
    if (suitOf(card) != m.suit) return -1;
    for (int i = 0; i < m.size(); ++i)
        if (isJoker(m.cards[(size_t)i]) && showsRank(card, m.rankAt(i))) return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------------------
// Partition search
// ---------------------------------------------------------------------------------------------------------

namespace {

// The partition DP's memo: (covered, jokers left) -> (best score, choice). An open-addressing table kept per thread
// and reused by every call (bestPartition runs thousands of times a decision); a generation stamp empties it at once.
class PartitionMemo {
public:
    struct Entry {
        int score = 0, choice = 0;
    };
    void reset() {
        if (slots_.empty()) slots_.resize(4096);
        if (++gen_ == 0) { // (the stamp wrapped: clear the stale ones for real)
            for (Slot& s : slots_) s.gen = 0;
            gen_ = 1;
        }
        count_ = 0;
    }
    const Entry* find(uint64_t key) const {
        const size_t mask = slots_.size() - 1;
        for (size_t i = hashOf(key) & mask;; i = (i + 1) & mask) {
            const Slot& s = slots_[i];
            if (s.gen != gen_) return nullptr;
            if (s.key == key) return &s.e;
        }
    }
    void put(uint64_t key, Entry e) {
        if ((count_ + 1) * 2 > slots_.size()) grow();
        Slot& s = slotFor(key);
        if (s.gen != gen_) {
            s.gen = gen_;
            s.key = key;
            ++count_;
        }
        s.e = e;
    }

private:
    struct Slot {
        uint64_t key = 0;
        uint32_t gen = 0;
        Entry e;
    };
    std::vector<Slot> slots_;
    uint32_t gen_ = 0;
    size_t count_ = 0;
    static size_t hashOf(uint64_t k) { return (size_t)((k * 0x9E3779B97F4A7C15ull) >> 20); }
    Slot& slotFor(uint64_t key) {
        const size_t mask = slots_.size() - 1;
        for (size_t i = hashOf(key) & mask;; i = (i + 1) & mask) {
            Slot& s = slots_[i];
            if (s.gen != gen_ || s.key == key) return s;
        }
    }
    void grow() {
        std::vector<Slot> old;
        old.swap(slots_);
        slots_.assign(old.size() * 2, Slot());
        const uint32_t g = gen_;
        gen_ = 1;
        count_ = 0;
        for (const Slot& s : old)
            if (s.gen == g) put(s.key, s.e);
    }
};

PartitionMemo& partitionMemo() {
    thread_local PartitionMemo m;
    return m;
}

struct Cand {
    uint32_t mask = 0;           // real cards (indices into the naturals)
    int jokers = 0;
    int score = 0;               // the goal's score of the meld (jokers included)
    int value = 0;
    std::vector<int> layout;     // natural indices in meld order, -1 for a joker
};

struct Search {
    std::vector<int> nat;
    int J = 0;
    Goal goal = Goal::Value;
    int jokerPoints = 25;
    int must = -1;                               // natural index that must be laid
    std::vector<std::vector<int>> byLowest;      // candidate indices by their lowest real card
    std::vector<Cand> cands;
    PartitionMemo& memo = partitionMemo(); // (covered, jokers left) -> (best score, choice)
    int nodes = 0;

    // Ties go to the meld with more real cards: a joker not needed inside a meld is spare, and spare jokers are put on
    // the melds afterwards (or kept), which is never worse. (Shed counts a joker as nothing for the same reason.)
    int score(const Cand& c) const {
        if (goal == Goal::Value) return c.value * 64 + 2 * __builtin_popcount(c.mask) + c.jokers;
        int p = 0;
        for (int i = 0; i < (int)nat.size(); ++i)
            if (c.mask >> i & 1u) p += handPoints(nat[(size_t)i], jokerPoints);
        return p * 64 + 2 * __builtin_popcount(c.mask) - c.jokers;
    }

    void add(Cand c) {
        int low = 0;
        while (!(c.mask >> low & 1u)) ++low;
        c.score = score(c);
        cands.push_back(std::move(c));
        byLowest[(size_t)low].push_back((int)cands.size() - 1);
    }

    void enumRun(Cand cur, int r, int lo, int hi, uint32_t used) {
        if (r > hi) {
            if (__builtin_popcount(cur.mask) < 2) return;
            int v = 0;
            for (int q = lo; q <= hi; ++q) v += rankValue(q);
            cur.value = v;
            add(std::move(cur));
            return;
        }
        std::vector<int> opts;
        for (int i : runAt_[(size_t)r])
            if (!(used >> i & 1u)) opts.push_back(i);
        if (opts.empty()) {
            if (cur.jokers + 1 > J) return;
            cur.jokers += 1;
            cur.layout.push_back(-1);
            enumRun(std::move(cur), r + 1, lo, hi, used);
            return;
        }
        // two copies of one face are the same for the meld: try the second only when it is a different index (the DP
        // needs both: the other copy may be taken by another meld)
        for (int i : opts) {
            Cand c = cur;
            c.mask |= 1u << i;
            c.layout.push_back(i);
            enumRun(std::move(c), r + 1, lo, hi, used | (1u << i));
        }
    }
    std::array<std::vector<int>, 15> runAt_;

    // the best score of the cards not in `covered` with `j` jokers left
    int best(uint32_t covered, int j) {
        const uint32_t all = nat.empty() ? 0u : (uint32_t)((1ull << nat.size()) - 1);
        if (covered == all) return 0;
        const uint64_t key = (uint64_t)covered | ((uint64_t)j << 32);
        if (const PartitionMemo::Entry* e = memo.find(key)) return e->score;
        ++nodes;
        int low = 0;
        while (covered >> low & 1u) ++low;
        int bestScore = -1000000000, choice = -2;
        if (low != must) { // the card stays in hand
            bestScore = best(covered | (1u << low), j);
            choice = -1;
        }
        for (int ci : byLowest[(size_t)low]) {
            const Cand& c = cands[(size_t)ci];
            if ((c.mask & covered) || c.jokers > j) continue;
            const int sub = best(covered | c.mask, j - c.jokers);
            if (sub <= -1000000000) continue;
            const int v = c.score + sub;
            if (v > bestScore) {
                bestScore = v;
                choice = ci;
            }
        }
        memo.put(key, {bestScore, choice});
        return bestScore;
    }
};

} // namespace

Partition bestPartition(const std::vector<int>& cards, Goal goal, int mustUse, bool attachJokers, int jokerPoints) {
    Partition out;
    partitionMemo().reset();
    Search s;
    std::vector<int> jk;
    for (int c : cards) {
        if (!isValidCard(c)) continue;
        if (isJoker(c)) jk.push_back(c);
        else s.nat.push_back(c);
    }
    if (s.nat.size() > 30) s.nat.resize(30);
    s.J = (int)jk.size();
    s.goal = goal;
    s.jokerPoints = jokerPoints;
    for (size_t i = 0; i < s.nat.size(); ++i)
        if (s.nat[i] == mustUse) s.must = (int)i;
    // the candidate melds: sets, then runs suit by suit (enumRun reads runAt_ of the suit at hand)
    {
        const int n = (int)s.nat.size();
        s.byLowest.assign((size_t)n, {});
        Search& S = s;
        S.cands.clear();
        // sets: for every rank, every choice of 2..4 suits present (each copy tried), jokers up to three cards
        for (int r = 2; r <= 14; ++r) {
            std::array<std::vector<int>, 4> bySuit;
            for (int i = 0; i < n; ++i)
                if (rankOf(S.nat[(size_t)i]) == r) bySuit[(size_t)suitOf(S.nat[(size_t)i])].push_back(i);
            for (int sm = 1; sm < 16; ++sm) {
                const int k = __builtin_popcount((unsigned)sm);
                if (k < 2) continue;
                bool ok = true;
                for (int q = 0; q < 4; ++q)
                    if ((sm >> q & 1) && bySuit[(size_t)q].empty()) ok = false;
                if (!ok) continue;
                const int need = std::max(0, 3 - k);
                if (need > S.J) continue;
                std::vector<int> suits;
                for (int q = 0; q < 4; ++q)
                    if (sm >> q & 1) suits.push_back(q);
                std::vector<int> pick(suits.size(), 0);
                while (true) {
                    Cand c;
                    for (size_t q = 0; q < suits.size(); ++q) {
                        const int idx = bySuit[(size_t)suits[q]][(size_t)pick[q]];
                        c.mask |= 1u << idx;
                        c.layout.push_back(idx);
                    }
                    for (int j = 0; j < need; ++j) c.layout.push_back(-1);
                    c.jokers = need;
                    c.value = rankValue(r) * (k + need);
                    S.add(std::move(c));
                    size_t q = 0;
                    while (q < suits.size() && ++pick[q] >= (int)bySuit[(size_t)suits[q]].size()) pick[q++] = 0;
                    if (q == suits.size()) break;
                }
            }
        }
        for (int st = 0; st < 4; ++st) {
            for (auto& v : S.runAt_) v.clear();
            int count = 0;
            for (int i = 0; i < n; ++i)
                if (suitOf(S.nat[(size_t)i]) == st) {
                    const int r = rankOf(S.nat[(size_t)i]);
                    S.runAt_[(size_t)r].push_back(i);
                    if (r == 14) S.runAt_[1].push_back(i);
                    ++count;
                }
            if (count < 2) continue;
            for (int lo = 1; lo <= 12; ++lo) {
                if (S.runAt_[(size_t)lo].empty()) continue;
                int empty = 0, present = 1;
                for (int hi = lo + 1; hi <= 14; ++hi) {
                    if (S.runAt_[(size_t)hi].empty()) {
                        if (++empty > S.J) break;
                        continue;
                    }
                    ++present;
                    if (hi - lo < 2 || present < 2) continue;
                    // short real windows plus jokers on an end: 2 real cards + 1 joker beyond the end are made by
                    // the windows below (lo..hi with a gap) or by attaching spare jokers to a meld afterwards; a
                    // 2-card + joker run needs its joker inside or at an end: allow the ends here
                    S.enumRun(Cand{}, lo, lo, hi, 0u);
                }
            }
            // two real cards next to each other (or one apart) and a joker at an end: x x J, J x x
            if (S.J > 0)
                for (int lo = 1; lo <= 13; ++lo) {
                    if (S.runAt_[(size_t)lo].empty()) continue;
                    for (int gap = 1; gap <= 2; ++gap) {
                        const int hi = lo + gap;
                        if (hi > 14 || S.runAt_[(size_t)hi].empty()) continue;
                        if (gap == 2) continue; // lo, J, hi is a window above
                        // lo hi J (the joker high) or J lo hi (the joker low)
                        for (int i : S.runAt_[(size_t)lo])
                            for (int k2 : S.runAt_[(size_t)hi]) {
                                if (i == k2) continue;
                                if (hi + 1 <= 14) {
                                    Cand c;
                                    c.mask = (1u << i) | (1u << k2);
                                    c.layout = {i, k2, -1};
                                    c.jokers = 1;
                                    c.value = rankValue(lo) + rankValue(hi) + rankValue(hi + 1);
                                    S.add(std::move(c));
                                } else if (lo - 1 >= 1) {
                                    Cand c;
                                    c.mask = (1u << i) | (1u << k2);
                                    c.layout = {-1, i, k2};
                                    c.jokers = 1;
                                    c.value = rankValue(lo - 1) + rankValue(lo) + rankValue(hi);
                                    S.add(std::move(c));
                                }
                            }
                    }
                }
        }
    }
    // Every number of jokers inside the melds (0..J; the DP's memo serves them all), the rest spare: put on the melds
    // with room (attachJokers) or kept. The best result counts.
    const uint32_t all = s.nat.empty() ? 0u : (uint32_t)((1ull << s.nat.size()) - 1);
    bool have = false;
    long bestScore = 0;
    for (int jUse = 0; jUse <= s.J; ++jUse) {
        if (s.best(0u, jUse) <= -1000000000) continue;
        std::vector<std::vector<int>> melds;
        std::vector<int> rest;
        size_t jNext = 0;
        uint32_t covered = 0;
        int j = jUse;
        while (covered != all) {
            const PartitionMemo::Entry* e = s.memo.find((uint64_t)covered | ((uint64_t)j << 32));
            if (!e) break;
            const int ch = e->choice;
            int low = 0;
            while (covered >> low & 1u) ++low;
            if (ch < 0) {
                rest.push_back(s.nat[(size_t)low]);
                covered |= 1u << low;
                continue;
            }
            const Cand& c = s.cands[(size_t)ch];
            std::vector<int> m;
            for (int idx : c.layout) m.push_back(idx < 0 ? jk[jNext++] : s.nat[(size_t)idx]);
            melds.push_back(m);
            covered |= c.mask;
            j -= c.jokers;
        }
        // spare jokers: on the meld with room that gains the most (a run's high end, else its low end, else a set)
        for (int q = 0; q < s.J - jUse + j; ++q) {
            const int joker = jk[jNext++];
            int bestM = -1, bestGain = -1;
            Side bestSide = Side::Back;
            if (attachJokers)
                for (size_t mi = 0; mi < melds.size(); ++mi) {
                    Meld m;
                    if (!makeMeld(melds[mi], m)) continue;
                    Side used = Side::Back;
                    if (!canAdd(m, joker, Side::Auto, &used)) continue;
                    const int gain = m.kind == MeldKind::Set ? rankValue(m.rank)
                                     : used == Side::Back ? rankValue(m.high() + 1) : rankValue(m.low() - 1);
                    if (gain > bestGain) {
                        bestGain = gain;
                        bestM = (int)mi;
                        bestSide = used;
                    }
                }
            if (bestM < 0) {
                rest.push_back(joker);
                continue;
            }
            std::vector<int>& mv = melds[(size_t)bestM];
            Meld m;
            makeMeld(mv, m);
            mv = m.cards;
            if (m.kind == MeldKind::Run && bestSide == Side::Front) mv.insert(mv.begin(), joker);
            else mv.push_back(joker);
        }
        if (s.must >= 0 && std::find(rest.begin(), rest.end(), mustUse) != rest.end()) continue;
        Partition p;
        p.melds = melds;
        p.rest = rest;
        int natLaid = 0, jokLaid = 0, natPts = 0;
        for (const auto& mv : p.melds) {
            Meld m;
            if (makeMeld(mv, m)) p.value += m.value();
            p.laid += (int)mv.size();
            for (int c : mv) {
                if (isJoker(c)) ++jokLaid;
                else {
                    ++natLaid;
                    natPts += handPoints(c, jokerPoints);
                }
            }
        }
        for (int c : p.rest) p.restPoints += handPoints(c, jokerPoints);
        const long sc = goal == Goal::Value ? (long)p.value * 64 + 2 * natLaid + jokLaid
                                            : (long)(natPts + (attachJokers ? jokerPoints * jokLaid : 0)) * 64 + 2 * natLaid - jokLaid;
        if (!have || sc > bestScore) {
            have = true;
            bestScore = sc;
            out = p;
        }
    }
    if (!have) {
        out = Partition();
        out.rest = cards;
        for (int c : cards) out.restPoints += handPoints(c, jokerPoints);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// Log lines
// ---------------------------------------------------------------------------------------------------------

std::string LoggedAction::encode() const {
    std::string s = std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(card) + " " +
                    std::to_string(meld) + " " + std::to_string(side);
    for (const auto& m : melds) {
        s += " |";
        for (int c : m) s += " " + std::to_string(c);
    }
    return s;
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    std::vector<std::string> parts;
    size_t p = 0;
    while (true) {
        const size_t q = line.find('|', p);
        parts.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    int v[5] = {0, 0, 0, 0, 0};
    if (!kart::parseInts(parts[0], v, 5)) return false;
    if (v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)v[0];
    out.seat = v[1];
    out.card = v[2];
    out.meld = v[3];
    out.side = v[4];
    for (size_t i = 1; i < parts.size(); ++i) {
        std::vector<int> m;
        const char* s = parts[i].c_str();
        while (*s) {
            while (*s == ' ' || *s == '\r' || *s == '\n') ++s;
            if (!*s) break;
            char* end = nullptr;
            const long x = std::strtol(s, &end, 10);
            if (end == s) return false;
            m.push_back((int)x);
            s = end;
        }
        if (m.empty()) return false;
        out.melds.push_back(m);
    }
    if (out.kind == LogKind::Lay && out.melds.empty()) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

Game::Game(const Rules& r) : rules_(r) {
    static const char* const names[4] = {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"};
    for (int s = 0; s < 4; ++s) {
        seats_[(size_t)s].name = names[s];
        seats_[(size_t)s].human = s == 0;
    }
}

void Game::setRules(const Rules& r) {
    if (stage_ == Stage::NotStarted || stage_ == Stage::MatchOver) rules_ = r;
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat > 3) return;
    seats_[(size_t)seat].name = name;
    seats_[(size_t)seat].human = human;
}

std::string Game::says(int s, const std::string& third, const std::string& second) const {
    return seats_[(size_t)s].human ? second : seats_[(size_t)s].name + " " + third;
}

void Game::push(GameEvent e) { kart::pushEvent(events_, std::move(e)); }

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> v;
    v.swap(events_);
    return v;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed ^ 0x6B6F6E6B656Eull); // "konken"
    matchSeed_ = seed;
    log_.clear();
    sheet_.clear();
    events_.clear();
    for (SeatInfo& s : seats_) {
        s.total = 0;
        s.hand.clear();
        s.opened = false;
        s.openValue = 0;
        s.out = false;
        s.outHand = -1;
        s.place = 0;
    }
    handIndex_ = 0;
    starter_ = rng_.range(4);
    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = "Konken başlıyor: " + std::to_string(rules_.limit) + " olan yanar" +
             (rules_.lastStanding ? ", son kalan kazanır" : "");
    push(e);
    dealHand();
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    ++handIndex_;
    starter_ = nextActive(starter_); // (the burned are skipped: the deal goes round the ones still playing)
    LoggedAction a;
    a.kind = LogKind::NextHand;
    log_.push_back(a);
    dealHand();
}

void Game::dealHand() {
    std::vector<int> deck((size_t)NUM_CARDS);
    for (int i = 0; i < NUM_CARDS; ++i) deck[(size_t)i] = i;
    rng_.shuffle(deck);
    for (SeatInfo& s : seats_) {
        s.hand.clear();
        s.opened = false;
        s.openValue = 0;
    }
    table_.clear();
    discard_.clear();
    discards_.clear();
    taken_ = -1;
    mustDrawStock_ = false;
    turn_ = 0;
    for (int i = 0; i < rules_.handSize; ++i)
        for (int j = 0; j < 4; ++j) {
            const int s = (starter_ + j) % 4;
            if (!active(s)) continue; // (burned: no cards)
            seats_[(size_t)s].hand.push_back(deck.back());
            deck.pop_back();
        }
    stock_ = deck; // top = back
    // the first discard: the next card, a joker goes under the stock
    while (!stock_.empty() && isJoker(stock_.back())) {
        const int j = stock_.back();
        stock_.pop_back();
        stock_.insert(stock_.begin(), j);
    }
    if (!stock_.empty()) {
        discard_.push_back(stock_.back());
        stock_.pop_back();
        discards_.push_back({-1, discard_.back(), 0, -1});
    }
    GameEvent hs;
    hs.type = EvType::HandStart;
    hs.seat = starter_;
    hs.amount = handIndex_;
    hs.text = std::to_string(handIndex_ + 1) + ". el: " + (seats_[(size_t)starter_].human ? std::string("sen başlıyorsun")
                                                                                         : seats_[(size_t)starter_].name + " başlıyor");
    push(hs);
    GameEvent d;
    d.type = EvType::Deal;
    d.card = discardTop();
    d.amount = rules_.handSize;
    d.text = "Yere " + cardNameTR(discardTop()) + " açıldı";
    push(d);
    beginTurn(starter_);
}

void Game::beginTurn(int seat) {
    current_ = seat;
    stage_ = Stage::Draw;
    taken_ = -1;
    mustDrawStock_ = false;
    openedAtTurnStart_ = seats_[(size_t)seat].opened;
    openedNow_ = false;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = seat;
    push(e);
}

bool Game::inHand(int seat, int card) const {
    const auto& h = seats_[(size_t)seat].hand;
    return std::find(h.begin(), h.end(), card) != h.end();
}

bool Game::removeFromHand(int seat, int card) {
    auto& h = seats_[(size_t)seat].hand;
    const auto it = std::find(h.begin(), h.end(), card);
    if (it == h.end()) return false;
    h.erase(it);
    return true;
}

bool Game::checkTurn(int seat, Stage st, std::string* why) const {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (stage_ != Stage::Draw && stage_ != Stage::Play) return fail("El bitti");
    if (seat < 0 || seat > 3 || seat != current_) return fail("Sıra sende değil");
    if (stage_ != st) return fail(st == Stage::Draw ? "Bu tur kâğıdını çektin" : "Önce desteden ya da yerden kâğıt çek");
    return true;
}

bool Game::canTakeDiscard(int seat, std::string* why) const {
    if (!checkTurn(seat, Stage::Draw, why)) return false;
    if (mustDrawStock_) {
        if (why) *why = "Geri verdiğin kâğıdı yeniden alamazsın: desteden çek";
        return false;
    }
    if (discard_.empty()) {
        if (why) *why = "Yerde kâğıt yok";
        return false;
    }
    return true;
}

bool Game::checkLay(int seat, const std::vector<std::vector<int>>& melds, int* value, std::string* why) const {
    if (!checkTurn(seat, Stage::Play, why)) return false;
    if (melds.empty()) {
        if (why) *why = "Per seçmedin";
        return false;
    }
    std::vector<int> used;
    int v = 0;
    for (const auto& mv : melds) {
        Meld m;
        std::string w;
        if (!makeMeld(mv, m, &w)) {
            if (why) *why = w;
            return false;
        }
        for (int c : mv) {
            if (!inHand(seat, c)) {
                if (why) *why = cardNameTR(c) + " elinde değil";
                return false;
            }
            if (std::find(used.begin(), used.end(), c) != used.end()) {
                if (why) *why = "Bir kâğıt iki pere giremez";
                return false;
            }
            used.push_back(c);
        }
        v += m.value();
    }
    if (value) *value = v;
    if (!seats_[(size_t)seat].opened && v < rules_.openMin) {
        if (why) *why = "Açmak için en az " + std::to_string(rules_.openMin) + " gerekir (bu perler " + std::to_string(v) + ")";
        return false;
    }
    return true;
}

bool Game::checkAdd(int seat, int card, int meld, Side side, std::string* why) const {
    if (!checkTurn(seat, Stage::Play, why)) return false;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (!seats_[(size_t)seat].opened) return fail("İşlemek için önce açmalısın");
    if (!inHand(seat, card)) return fail("Bu kâğıt elinde değil");
    if (meld < 0 || meld >= (int)table_.size()) return fail("Masada böyle bir per yok");
    if (!canAdd(table_[(size_t)meld], card, side)) return fail(cardNameTR(card) + " bu pere gitmiyor");
    return true;
}

bool Game::checkSwap(int seat, int card, int meld, std::string* why) const {
    if (!checkTurn(seat, Stage::Play, why)) return false;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (!seats_[(size_t)seat].opened) return fail("Joker almak için önce açmalısın");
    if (!inHand(seat, card)) return fail("Bu kâğıt elinde değil");
    if (meld < 0 || meld >= (int)table_.size()) return fail("Masada böyle bir per yok");
    if (swapIndex(table_[(size_t)meld], card) < 0) return fail("Bu perdeki joker " + cardNameTR(card) + " yerine geçmiyor");
    return true;
}

bool Game::canDiscard(int seat, int card, std::string* why) const {
    if (!checkTurn(seat, Stage::Play, why)) return false;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (!inHand(seat, card)) return fail("Bu kâğıt elinde değil");
    if (taken_ >= 0) return fail("Yerden aldığın " + cardAccusativeTR(taken_) + " bu tur masada kullanmalısın (ya da geri ver)");
    if (isJoker(card) && !mayDiscardJoker(seat)) return fail("Joker atılmaz");
    return true;
}

bool Game::mayDiscardJoker(int seat) const {
    const SeatInfo& si = seats_[(size_t)seat];
    if (si.hand.size() <= 1) return true;
    for (int c : si.hand)
        if (!isJoker(c)) return false;
    return !(si.opened && fitsTable(si.hand.front()));
}

bool Game::fitsTable(int card) const {
    for (const Meld& m : table_)
        if (canAdd(m, card, Side::Auto) || swapIndex(m, card) >= 0) return true;
    return false;
}

int Game::pointsInHand(int s) const {
    const SeatInfo& si = seats_[(size_t)s];
    if (si.out) return 0; // (burned: no longer writes)
    if (!si.opened) return rules_.unopenedPoints;
    int p = 0;
    for (int c : si.hand) p += handPoints(c, rules_.jokerPoints);
    return p;
}

std::array<int, 4> Game::ranking() const {
    std::array<int, 4> r{{0, 1, 2, 3}};
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
        const SeatInfo &A = seats_[(size_t)a], &B = seats_[(size_t)b];
        if (A.out != B.out) return !A.out;                                  // still playing first
        if (A.out && A.outHand != B.outHand) return A.outHand > B.outHand;  // burned later = better
        return A.total < B.total;
    });
    return r;
}

int Game::activeCount() const {
    int n = 0;
    for (const SeatInfo& s : seats_) n += s.out ? 0 : 1;
    return n;
}

int Game::nextActive(int s) const {
    for (int k = 1; k <= 4; ++k) {
        const int t = (s + k) % 4;
        if (active(t)) return t;
    }
    return s;
}

int Game::prevActive(int s) const {
    for (int k = 1; k <= 4; ++k) {
        const int t = (s + 4 - k) % 4;
        if (active(t)) return t;
    }
    return s;
}

ActionResult Game::drawStock(int seat) {
    std::string why;
    if (!checkTurn(seat, Stage::Draw, &why)) return ActionResult::fail(why);
    if (stock_.empty()) return ActionResult::fail("Destede kâğıt kalmadı");
    const int c = stock_.back();
    stock_.pop_back();
    seats_[(size_t)seat].hand.push_back(c);
    stage_ = Stage::Play;
    mustDrawStock_ = false;
    LoggedAction a;
    a.kind = LogKind::Draw;
    a.seat = seat;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::DrawStock;
    e.seat = seat;
    e.card = c;
    e.text = says(seat, "desteden çekti", "Desteden " + cardNameTR(c) + " çektin");
    push(e);
    return ActionResult::success();
}

ActionResult Game::takeDiscard(int seat) {
    std::string why;
    if (!canTakeDiscard(seat, &why)) return ActionResult::fail(why);
    const int c = discard_.back();
    discard_.pop_back();
    for (auto it = discards_.rbegin(); it != discards_.rend(); ++it)
        if (it->card == c && it->takenBy < 0) {
            it->takenBy = seat;
            break;
        }
    seats_[(size_t)seat].hand.push_back(c);
    taken_ = c;
    stage_ = Stage::Play;
    LoggedAction a;
    a.kind = LogKind::Take;
    a.seat = seat;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::TakeDiscard;
    e.seat = seat;
    e.card = c;
    e.text = says(seat, "yerden " + cardAccusativeTR(c) + " aldı", "Yerden " + cardAccusativeTR(c) + " aldın");
    push(e);
    return ActionResult::success();
}

ActionResult Game::returnDiscard(int seat) {
    std::string why;
    if (!checkTurn(seat, Stage::Play, &why)) return ActionResult::fail(why);
    if (taken_ < 0 || !inHand(seat, taken_)) return ActionResult::fail("Geri verecek kâğıt yok");
    const int c = taken_;
    removeFromHand(seat, c);
    discard_.push_back(c);
    for (auto it = discards_.rbegin(); it != discards_.rend(); ++it)
        if (it->card == c && it->takenBy == seat) {
            it->takenBy = -1;
            break;
        }
    taken_ = -1;
    mustDrawStock_ = true;
    stage_ = Stage::Draw;
    LoggedAction a;
    a.kind = LogKind::Return;
    a.seat = seat;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::ReturnDiscard;
    e.seat = seat;
    e.card = c;
    e.text = says(seat, cardAccusativeTR(c) + " geri verdi", cardAccusativeTR(c) + " geri verdin");
    push(e);
    return ActionResult::success();
}

void Game::afterTableAction(int seat) {
    if (taken_ >= 0 && !inHand(seat, taken_)) taken_ = -1;
    if (seats_[(size_t)seat].hand.empty()) endHand(seat);
}

ActionResult Game::layMelds(int seat, const std::vector<std::vector<int>>& melds) {
    std::string why;
    int v = 0;
    if (!checkLay(seat, melds, &v, &why)) return ActionResult::fail(why);
    const bool opening = !seats_[(size_t)seat].opened;
    GameEvent e;
    e.type = opening ? EvType::Open : EvType::LayMelds;
    e.seat = seat;
    e.amount = v;
    for (const auto& mv : melds) {
        Meld m;
        makeMeld(mv, m);
        m.owner = seat;
        for (int c : mv) removeFromHand(seat, c);
        table_.push_back(m);
        e.melds.push_back((int)table_.size() - 1);
    }
    if (opening) {
        seats_[(size_t)seat].opened = true;
        seats_[(size_t)seat].openValue = v;
        openedNow_ = true;
        e.text = says(seat, "açtı (" + std::to_string(v) + ")", "Açtın (" + std::to_string(v) + ")");
    } else {
        e.text = says(seat, melds.size() > 1 ? "per indirdi" : "bir per daha indirdi",
                      melds.size() > 1 ? "Per indirdin" : "Bir per daha indirdin");
    }
    LoggedAction a;
    a.kind = LogKind::Lay;
    a.seat = seat;
    a.melds = melds;
    log_.push_back(a);
    push(e);
    afterTableAction(seat);
    return ActionResult::success();
}

ActionResult Game::addToMeld(int seat, int card, int meld, Side side) {
    std::string why;
    if (!checkAdd(seat, card, meld, side, &why)) return ActionResult::fail(why);
    Meld& m = table_[(size_t)meld];
    Side used = Side::Back;
    canAdd(m, card, side, &used);
    addCard(m, card, used);
    removeFromHand(seat, card);
    LoggedAction a;
    a.kind = LogKind::Add;
    a.seat = seat;
    a.card = card;
    a.meld = meld;
    a.side = (int)side;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::AddToMeld;
    e.seat = seat;
    e.card = card;
    e.meld = meld;
    e.text = says(seat, cardAccusativeTR(card) + " işledi", cardAccusativeTR(card) + " işledin");
    push(e);
    afterTableAction(seat);
    return ActionResult::success();
}

ActionResult Game::swapJoker(int seat, int card, int meld) {
    std::string why;
    if (!checkSwap(seat, card, meld, &why)) return ActionResult::fail(why);
    Meld& m = table_[(size_t)meld];
    const int i = swapIndex(m, card);
    const int joker = m.cards[(size_t)i];
    m.cards[(size_t)i] = card;
    removeFromHand(seat, card);
    seats_[(size_t)seat].hand.push_back(joker);
    LoggedAction a;
    a.kind = LogKind::Swap;
    a.seat = seat;
    a.card = card;
    a.meld = meld;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::SwapJoker;
    e.seat = seat;
    e.card = card;
    e.card2 = joker;
    e.meld = meld;
    e.text = says(seat, "jokeri aldı, yerine " + cardAccusativeTR(card) + " koydu",
                  "Jokeri aldın, yerine " + cardAccusativeTR(card) + " koydun");
    push(e);
    afterTableAction(seat);
    return ActionResult::success();
}

ActionResult Game::discard(int seat, int card) {
    std::string why;
    if (!canDiscard(seat, card, &why)) return ActionResult::fail(why);
    removeFromHand(seat, card);
    discard_.push_back(card);
    discards_.push_back({seat, card, turn_, -1});
    LoggedAction a;
    a.kind = LogKind::Discard;
    a.seat = seat;
    a.card = card;
    log_.push_back(a);
    GameEvent e;
    e.type = EvType::Discard;
    e.seat = seat;
    e.card = card;
    e.text = says(seat, cardAccusativeTR(card) + " attı", cardAccusativeTR(card) + " attın");
    push(e);
    if (seats_[(size_t)seat].hand.empty()) {
        endHand(seat);
    } else if (stock_.empty()) {
        endHand(-1);
    } else {
        ++turn_;
        beginTurn(nextActive(seat));
    }
    return ActionResult::success();
}

void Game::endHand(int finisher) {
    HandRecord r;
    r.index = handIndex_;
    r.starter = starter_;
    r.finisher = finisher;
    r.turns = turn_ + 1;
    r.konken = finisher >= 0 && !openedAtTurnStart_;
    const int mult = r.konken && rules_.konkenDouble ? 2 : 1;
    GameEvent e;
    e.type = EvType::HandEnd;
    e.seat = finisher;
    e.amount = r.konken ? 1 : 0;
    for (int s = 0; s < 4; ++s) {
        const SeatInfo& si = seats_[(size_t)s];
        r.playing[(size_t)s] = !si.out;
        r.totals[(size_t)s] = si.total;
        if (si.out) continue; // (burned earlier: writes nothing, no line)
        r.opened[(size_t)s] = si.opened;
        r.cardsLeft[(size_t)s] = (int)si.hand.size();
        r.points[(size_t)s] = s == finisher ? 0 : pointsInHand(s) * mult;
        seats_[(size_t)s].total += r.points[(size_t)s];
        r.totals[(size_t)s] = seats_[(size_t)s].total;
        e.points[(size_t)s] = r.points[(size_t)s];
        std::string line = si.name + ": ";
        if (s == finisher) line += r.konken ? "konken (0)" : "bitirdi (0)";
        else if (!si.opened) line += "açmadı (" + std::to_string(r.points[(size_t)s]) + ")";
        else line += std::to_string(r.points[(size_t)s]) + " (" + std::to_string(si.hand.size()) + " kâğıt)";
        e.lines.push_back(line);
    }
    if (finisher < 0) e.text = "Deste bitti, el kimse bitirmeden kapandı";
    else if (r.konken) e.text = "Konken! " + says(finisher, "elden bitirdi", "Elden bitirdin");
    else e.text = says(finisher, "eli bitirdi", "Eli bitirdin");
    // who burns
    std::vector<int> burn;
    for (int s = 0; s < 4; ++s)
        if (active(s) && seats_[(size_t)s].total >= rules_.limit) burn.push_back(s);
    bool over = handIndex_ + 1 >= rules_.maxHands;
    std::vector<GameEvent> burnEvents;
    if (!rules_.lastStanding) {
        over = over || !burn.empty();
        for (int s : burn) r.burned[(size_t)s] = true;
    } else if (!burn.empty()) {
        // the worst total goes first (the same hand: the higher total is the lower place); if everyone still playing
        // burns at once, the lowest of them stays and wins
        std::sort(burn.begin(), burn.end(), [&](int a, int b) {
            const int ta = seats_[(size_t)a].total, tb = seats_[(size_t)b].total;
            return ta != tb ? ta > tb : a > b; // (ties: the higher seat is the lower place, as in ranking())
        });
        if ((int)burn.size() >= activeCount()) burn.pop_back();
        int place = activeCount();
        for (int s : burn) {
            SeatInfo& si = seats_[(size_t)s];
            si.out = true;
            si.outHand = handIndex_;
            si.place = place--;
            r.burned[(size_t)s] = true;
            GameEvent b;
            b.type = EvType::Burn;
            b.seat = s;
            b.amount = si.place;
            for (int q = 0; q < 4; ++q) b.points[(size_t)q] = seats_[(size_t)q].total;
            b.text = si.human ? "Yandın! " + std::to_string(si.total) + " ile masadan kalkıyorsun"
                              : si.name + " yandı (" + std::to_string(si.total) + "), masadan kalktı";
            burnEvents.push_back(b);
        }
        over = over || activeCount() <= 1;
    }
    sheet_.push_back(r);
    push(e);
    for (GameEvent& b : burnEvents) push(std::move(b));
    stage_ = over ? Stage::MatchOver : Stage::HandOver;
    if (over) {
        const std::array<int, 4> fin = ranking();
        for (int i = 0; i < 4; ++i) {
            SeatInfo& si = seats_[(size_t)fin[(size_t)i]];
            if (si.place == 0) si.place = i + 1;
        }
        const std::array<int, 4> rk = ranking();
        GameEvent m;
        m.type = EvType::MatchEnd;
        m.seat = rk[0];
        for (int s = 0; s < 4; ++s) m.points[(size_t)s] = seats_[(size_t)s].total;
        for (int i = 0; i < 4; ++i)
            m.lines.push_back(std::to_string(i + 1) + ". " + seats_[(size_t)rk[(size_t)i]].name + " " +
                              std::to_string(seats_[(size_t)rk[(size_t)i]].total));
        m.text = says(rk[0], "partiyi kazandı", "Partiyi kazandın!");
        push(m);
    }
}

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Draw: return drawStock(a.seat).ok;
    case LogKind::Take: return takeDiscard(a.seat).ok;
    case LogKind::Return: return returnDiscard(a.seat).ok;
    case LogKind::Lay: return layMelds(a.seat, a.melds).ok;
    case LogKind::Add:
        if (a.side < 0 || a.side > 2) return false;
        return addToMeld(a.seat, a.card, a.meld, (Side)a.side).ok;
    case LogKind::Swap: return swapJoker(a.seat, a.card, a.meld).ok;
    case LogKind::Discard: return discard(a.seat, a.card).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

void Game::debugSetup(const std::array<std::vector<int>, 4>& hands, const std::vector<int>& stock,
                      const std::vector<int>& discardPile, const std::vector<Meld>& table) {
    for (int s = 0; s < 4; ++s) seats_[(size_t)s].hand = hands[(size_t)s];
    stock_ = stock;
    discard_ = discardPile;
    discards_.clear();
    for (int c : discard_) discards_.push_back({-1, c, 0, -1});
    table_ = table;
    taken_ = -1;
    mustDrawStock_ = false;
    stage_ = Stage::Draw;
    openedAtTurnStart_ = seats_[(size_t)current_].opened;
    openedNow_ = false;
}

} // namespace konken
