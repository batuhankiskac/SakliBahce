// Pişti computer opponents (Acemi / Usta / Kurt). See PistiBot.h for the levels and the fairness contract.
#include "core/PistiBot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace pisti {

using namespace kart;

namespace {

constexpr double CLOSED_CARD_POINTS = 13.0 / 52.0; // expected points of an unknown card (13 points in 52)
// Kurt: simulated plays per decision, sample bounds, and how sure the samples must be to overrule Usta
// (compile-time knobs for tuning with tests/pisti_sim; 400k plays = ~9 ms per decision on an M-series core).
#ifndef PISTI_ROLLOUT_PLAYS
#define PISTI_ROLLOUT_PLAYS 400000
#endif
#ifndef PISTI_SWITCH_Z
#define PISTI_SWITCH_Z 1.0
#endif
constexpr int ROLLOUT_PLAYS = PISTI_ROLLOUT_PLAYS;
constexpr int MIN_SAMPLES = 200;
constexpr int MAX_SAMPLES = 3000;
constexpr double SWITCH_Z = PISTI_SWITCH_Z;

// Points of each card id (local table: cardPoints() lives in another translation unit and is hot here).
struct PointsTable {
    int8_t p[NUM_CARDS];
    int8_t rank[NUM_CARDS];
    PointsTable() {
        for (int c = 0; c < NUM_CARDS; ++c) {
            p[c] = (int8_t)pisti::cardPoints(c);
            rank[c] = (int8_t)kart::rankOf(c);
        }
    }
};
const PointsTable kPts;
inline int cpts(int c) { return kPts.p[c]; }
inline int rk(int c) { return kPts.rank[c]; }

struct Params {
    int pisti = 10;
    int jackPisti = 20;
    bool jackOnAny = false;
    bool lastCardPisti = false;
    double cardWeight = 0.12;  // value of one more captured card (towards the 3-point majority)
    double jackKeep = 2.0;     // value of keeping a Vale for a later pile
    bool teamAware = false;    // eşli: the player after next is my partner
};

Params paramsFor(const Game& g) {
    Params p;
    const Rules& r = g.rules();
    p.pisti = r.pistiPoints;
    p.jackPisti = r.jackPistiPoints;
    p.jackOnAny = r.jackPistiOnAny;
    p.lastCardPisti = r.lastCardPisti;
    // Majority among two sides is decided around 26 cards: every card counts more than among four.
    p.cardWeight = g.numSides() == 2 ? 0.16 : 0.10;
    p.cardWeight *= r.majorityPoints / 3.0;
    p.teamAware = r.mode == Mode::Esli;
    return p;
}

// P(none of k special cards among h cards drawn from u unknown cards).
double hypNoneSlow(int u, int k, int h) {
    if (k <= 0 || h <= 0 || u <= 0) return 1.0;
    double p = 1.0;
    for (int i = 0; i < h; ++i) {
        const int num = u - k - i;
        if (num <= 0) return 0.0;
        p *= (double)num / (double)(u - i);
    }
    return p;
}

struct HypTable {
    float t[53][9][9]; // u, k (<= 8: a rank plus the Valeler), h (<= 8)
    HypTable() {
        for (int u = 0; u <= 52; ++u)
            for (int k = 0; k <= 8; ++k)
                for (int h = 0; h <= 8; ++h) t[u][k][h] = (float)hypNoneSlow(u, k, h);
    }
};
const HypTable kHyp;

inline double hypNone(int u, int k, int h) {
    if (u < 0 || u > 52 || k < 0 || k > 8 || h < 0 || h > 8) return hypNoneSlow(u, k, h);
    return kHyp.t[u][k][h];
}

// What one player knows when choosing a card (all from its own point of view).
struct View {
    const int8_t* hand = nullptr;
    int handN = 0;
    const int8_t* open = nullptr; // face-up table pile, last = top
    int openN = 0;
    int closedN = 0;
    double pileVal = 0.0;         // points on the table (closed cards at their expected value)
    int unk[15] = {};             // unknown copies per rank (not seen, not in my hand)
    int unkTotal = 0;
    int nextHandN = 0;            // cards in the next player's hand
    uint32_t nextExcl = 0;        // ranks the next player is believed not to hold (Kurt's inference)
    int afterNextHandN = 0;
    bool partnerAfterNext = false;
    bool moreOpponentsAfter = false; // individual play with 3+ players: the one after next is also an opponent
    int futurePlays = 0;          // my plays after this one in the hand
    bool lastCard = false;        // this is the final card of the hand
};

int pistiPointsOf(const View& v, const Params& P, int card) {
    if (v.openN != 1 || v.closedN != 0) return 0;
    if (v.lastCard && !P.lastCardPisti) return 0;
    const int top = v.open[0];
    if (rk(top) == rk(card)) return rk(card) == Vale ? P.jackPisti : P.pisti;
    if (rk(card) == Vale && P.jackOnAny) return P.pisti;
    return 0;
}

// Usta one-ply evaluation of playing `card` (expected point change for me, roughly).
double evalCard(const View& v, const Params& P, int card) {
    const double pileVal = v.pileVal;
    const int n = v.openN + v.closedN;
    const int top = v.openN ? v.open[v.openN - 1] : -1;
    const int r = rk(card);
    const bool rankMatch = top >= 0 && rk(top) == r;
    const bool capture = n > 0 && (r == Vale || rankMatch);

    if (capture) {
        double val = pileVal + cpts(card) + (n + 1) * P.cardWeight + pistiPointsOf(v, P, card);
        if (r == Vale && !rankMatch && v.futurePlays > 0) val -= P.jackKeep;
        return val;
    }

    // The card stays on the table on top of the pile: what can the next players do with it?
    const double val = pileVal + cpts(card) + (n + 1) * P.cardWeight;
    const int kr = v.unk[r];
    const int kv = v.unk[Vale];
    const int u = v.unkTotal;
    const int krNext = (v.nextExcl >> r & 1u) ? 0 : kr;
    const double pNoRank = hypNone(u, krNext, v.nextHandN);
    const double pNoAny = r == Vale ? pNoRank : hypNone(u, krNext + kv, v.nextHandN);
    const double pRank = 1.0 - pNoRank;
    const double pJackOnly = pNoRank - pNoAny;
    double loss;
    if (n == 0) {
        int pp = r == Vale ? P.jackPisti : P.pisti;
        if (v.nextHandN == 0) pp = 0;
        const double jackPp = (P.jackOnAny && r != Vale) ? P.pisti : 0.0;
        loss = pRank * (val + pp) + pJackOnly * (jackPp > 0 ? val + jackPp : 0.35 * val);
    } else {
        const double useJack = std::min(1.0, val / 2.5);
        loss = pRank * val + pJackOnly * useJack * val;
    }
    // Later players get their chance only if the next one did not change the top.
    const double pass = pNoAny;
    const int u2 = std::max(0, u - v.nextHandN);
    const double p2 = 1.0 - hypNone(u2, kr + (r == Vale ? 0 : kv), v.afterNextHandN);
    if (v.partnerAfterNext) loss -= 0.5 * pass * p2 * val;
    else if (v.moreOpponentsAfter) loss += 0.35 * pass * p2 * val;

    double gain = 0.0;
    if (r != Vale && v.futurePlays > 0) {
        // Holding another card of this rank: if nobody takes it, I take it back (maybe as a pişti later).
        int mine = 0;
        for (int i = 0; i < v.handN; ++i)
            if (v.hand[i] != card && rk(v.hand[i]) == r) ++mine;
        if (mine > 0) gain += 0.55 * pass * val;
    }
    return gain - loss;
}

int bestCard(const View& v, const Params& P) {
    int best = -1;
    double bestV = -1e18;
    for (int i = 0; i < v.handN; ++i) {
        const int c = v.hand[i];
        // Tie-break: spend the cheaper card.
        const double e = evalCard(v, P, c) - 1e-3 * cpts(c) - 1e-5 * rk(c);
        if (e > bestV) {
            bestV = e;
            best = c;
        }
    }
    return best;
}

// -------------------------------------------------------------------------------------------------------
// Compact simulation of the rest of a hand (Kurt rollouts).

struct Sim {
    int8_t hand[4][8];
    int handN[4];
    int8_t open[52];
    int openN;
    int openPts;
    int8_t closed[3];
    int closedN;
    int8_t deck[52]; // back = top
    int deckN;
    int8_t seenRank[15];
    int seenTotal;
    int sideCards[4];
    double sidePts[4];
    int lastCap;
    int cur;
};

struct Table {
    int active[4];
    int nActive;
    int next[4];
    int side[4];
    int numSides;
    int dealer;
    bool esli;
};

void fillView(const Sim& s, const Table& t, int p, View& v) {
    v.hand = s.hand[p];
    v.handN = s.handN[p];
    v.open = s.open;
    v.openN = s.openN;
    v.closedN = s.closedN;
    v.pileVal = s.openPts + CLOSED_CARD_POINTS * s.closedN;
    for (int r = 2; r <= 14; ++r) v.unk[r] = 4 - s.seenRank[r];
    for (int i = 0; i < s.handN[p]; ++i) --v.unk[rk(s.hand[p][i])];
    v.unkTotal = 52 - s.seenTotal - s.handN[p];
    const int nx = t.next[p];
    const int nx2 = t.next[nx];
    v.nextHandN = s.handN[nx];
    v.afterNextHandN = nx2 == p ? 0 : s.handN[nx2];
    v.partnerAfterNext = t.esli && nx2 != p;
    v.moreOpponentsAfter = !t.esli && nx2 != p;
    v.futurePlays = s.handN[p] - 1 + s.deckN / t.nActive;
    int total = 0;
    for (int i = 0; i < t.nActive; ++i) total += s.handN[t.active[i]];
    v.lastCard = s.deckN == 0 && total == 1;
}

// Plays `card` for s.cur (it must be in the hand) and advances to the next player; returns false at hand end.
bool simPlay(Sim& s, const Table& t, const Params& P, int card) {
    const int p = s.cur;
    int idx = 0;
    while (idx < s.handN[p] && s.hand[p][idx] != card) ++idx;
    if (idx == s.handN[p]) return false; // inconsistent position (debug setups): stop the rollout
    s.hand[p][idx] = s.hand[p][--s.handN[p]];
    int total = 0;
    for (int i = 0; i < t.nActive; ++i) total += s.handN[t.active[i]];
    const bool lastCard = s.deckN == 0 && total == 0;

    ++s.seenRank[rk(card)];
    ++s.seenTotal;
    const int n = s.openN + s.closedN;
    const int top = s.openN ? s.open[s.openN - 1] : -1;
    const bool rankMatch = top >= 0 && rk(top) == rk(card);
    if (n > 0 && (rankMatch || rk(card) == Vale)) {
        const int sd = t.side[p];
        double pts = cpts(card);
        if (s.openN == 1 && s.closedN == 0 && (!lastCard || P.lastCardPisti)) {
            if (rankMatch) pts += rk(card) == Vale ? P.jackPisti : P.pisti;
            else if (P.jackOnAny) pts += P.pisti;
        }
        pts += s.openPts;
        for (int i = 0; i < s.closedN; ++i) {
            pts += cpts(s.closed[i]);
            ++s.seenRank[rk(s.closed[i])];
            ++s.seenTotal;
        }
        s.sidePts[sd] += pts;
        s.sideCards[sd] += n + 1;
        s.openN = 0;
        s.openPts = 0;
        s.closedN = 0;
        s.lastCap = p;
    } else {
        s.open[s.openN++] = (int8_t)card;
        s.openPts += cpts(card);
    }

    if (total == 0) {
        if (s.deckN == 0) {
            // The rest of the table to the last capturer (nobody: the dealer).
            const int to = s.lastCap >= 0 ? s.lastCap : t.dealer;
            double pts = s.openPts;
            for (int i = 0; i < s.closedN; ++i) pts += cpts(s.closed[i]);
            s.sidePts[t.side[to]] += pts;
            s.sideCards[t.side[to]] += s.openN + s.closedN;
            s.openN = s.closedN = 0;
            return false;
        }
        int q = t.next[t.dealer];
        for (int k = 0; k < t.nActive; ++k, q = t.next[q])
            for (int i = 0; i < 4 && s.deckN > 0; ++i) s.hand[q][s.handN[q]++] = s.deck[--s.deckN];
    }
    s.cur = t.next[p];
    return true;
}

// Final value for `mySide`: future points incl. majority, minus the mean of the other sides.
double simValue(const Sim& s, const Table& t, int majorityPoints, int mySide) {
    double pts[4];
    int best = -1, bestCards = -1;
    bool tie = false;
    for (int i = 0; i < t.numSides; ++i) {
        pts[i] = s.sidePts[i];
        if (s.sideCards[i] > bestCards) {
            best = i;
            bestCards = s.sideCards[i];
            tie = false;
        } else if (s.sideCards[i] == bestCards) {
            tie = true;
        }
    }
    if (!tie && best >= 0) pts[best] += majorityPoints;
    double others = 0;
    for (int i = 0; i < t.numSides; ++i)
        if (i != mySide) others += pts[i];
    return pts[mySide] - others / (t.numSides - 1);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------

int fallbackCard(const Game& g, int seat) {
    if (seat < 0 || seat >= 4 || g.stage() != Stage::Playing || g.current() != seat) return -1;
    const std::vector<int>& h = g.hand(seat);
    if (h.empty()) return -1;
    int best = h[0];
    auto cost = [&](int c) { return (g.wouldCapture(c) ? -100 : 0) + cpts(c) * 10 + (rk(c) == Vale ? 20 : 0); };
    for (int c : h)
        if (cost(c) < cost(best)) best = c;
    return best;
}

struct Bot::Impl {
    BotLevel level = BotLevel::Usta;
    Rng rng;
    // Inference from public plays: excl[s] bit r = seat s passed on taking a pile topped by rank r with a card
    // of the same rank since its last deal, so (a player who can take the table nearly always does) it is
    // assumed not to hold rank r. Cleared when the seat gets new cards.
    std::array<uint32_t, 4> excl{};
    int top = -1; // the face-up top of the table as followed through the events
    explicit Impl(BotLevel l, uint64_t seed) : level(l), rng(seed) {}

    void reset() {
        excl.fill(0);
        top = -1;
    }

    void observe(const GameEvent& e) {
        switch (e.type) {
        case EvType::HandStart: reset(); break;
        case EvType::Deal:
            if (e.seat >= 0 && e.seat < 4) excl[e.seat] = 0;
            break;
        case EvType::TableTurnUp:
            if (!e.jack) top = e.card;
            break;
        case EvType::Play:
            if (e.seat >= 0 && e.seat < 4 && top >= 0 && isValidCard(e.card) && rk(e.card) != rk(top) &&
                rk(e.card) != Vale)
                excl[e.seat] |= 1u << rk(top);
            top = e.card;
            break;
        case EvType::Capture:
        case EvType::LastCapture: top = -1; break;
        default: break;
        }
    }

    View viewOf(const Game& g, int seat, std::array<int8_t, 8>& handBuf, std::array<int8_t, 52>& openBuf) const {
        View v;
        const std::vector<int>& h = g.hand(seat);
        v.handN = (int)std::min<size_t>(h.size(), 8);
        for (int i = 0; i < v.handN; ++i) handBuf[i] = (int8_t)h[i];
        v.hand = handBuf.data();
        const std::vector<int>& o = g.tableCards();
        v.openN = (int)o.size();
        for (int i = 0; i < v.openN; ++i) openBuf[i] = (int8_t)o[i];
        v.open = openBuf.data();
        v.closedN = g.closedCount();
        v.pileVal = CLOSED_CARD_POINTS * v.closedN;
        for (int i = 0; i < v.openN; ++i) v.pileVal += cpts(o[i]);
        for (int r = 2; r <= 14; ++r) v.unk[r] = 4;
        int seenN = 0;
        for (int c = 0; c < NUM_CARDS; ++c)
            if (g.isSeen(c)) {
                --v.unk[rk(c)];
                ++seenN;
            }
        for (int i = 0; i < v.handN; ++i) --v.unk[rk(handBuf[i])];
        v.unkTotal = NUM_CARDS - seenN - v.handN;
        const int nx = g.nextActive(seat);
        const int nx2 = g.nextActive(nx);
        v.nextHandN = g.handCount(nx);
        v.afterNextHandN = nx2 == seat ? 0 : g.handCount(nx2);
        const bool esli = g.rules().mode == Mode::Esli;
        v.partnerAfterNext = esli && nx2 != seat;
        v.moreOpponentsAfter = !esli && nx2 != seat;
        v.futurePlays = v.handN - 1 + g.deckCount() / (int)g.activeSeats().size();
        v.lastCard = g.isLastCardOfHand();
        return v;
    }

    int acemi(const Game& g, int seat) {
        const std::vector<int>& h = g.hand(seat);
        for (int c : h)
            if (rk(c) != Vale && g.wouldCapture(c)) return c;
        std::vector<int> jacks, others;
        for (int c : h) (rk(c) == Vale ? jacks : others).push_back(c);
        if (!jacks.empty() && (others.empty() || (g.tableCount() > 0 && rng.chance(0.75f)))) return jacks[0];
        if (rng.chance(0.6f)) {
            int lo = 99;
            for (int c : others) lo = std::min(lo, cpts(c));
            std::vector<int> low;
            for (int c : others)
                if (cpts(c) == lo) low.push_back(c);
            return low[rng.range((int)low.size())];
        }
        return others[rng.range((int)others.size())];
    }

    int usta(const Game& g, int seat) {
        std::array<int8_t, 8> hb{};
        std::array<int8_t, 52> ob{};
        const View v = viewOf(g, seat, hb, ob);
        return bestCard(v, paramsFor(g));
    }

    // Kurt's determinized Monte Carlo: the value (own side minus the others' mean, majority and last capture
    // included) of each candidate in S sampled worlds, val[ci * S + k] (common worlds: paired comparisons).
    // Returns S.
    int sampleValues(const Game& g, int seat, const std::vector<int>& cands, Rng& rng, int plays,
                     std::vector<double>& val) const {
        const std::vector<int>& h = g.hand(seat);
        const Params P = paramsFor(g);
        Table t{};
        t.nActive = (int)g.activeSeats().size();
        for (int i = 0; i < t.nActive; ++i) t.active[i] = g.activeSeats()[i];
        for (int s = 0; s < 4; ++s) {
            t.next[s] = g.nextActive(s);
            t.side[s] = std::max(0, g.sideOf(s));
        }
        t.numSides = g.numSides();
        t.dealer = g.dealer();
        t.esli = g.rules().mode == Mode::Esli;
        const int mySide = g.sideOf(seat);

        // The public base state.
        Sim base{};
        base.cur = seat;
        base.lastCap = g.lastCapturer();
        const std::vector<int>& o = g.tableCards();
        base.openN = (int)o.size();
        for (int i = 0; i < base.openN; ++i) {
            base.open[i] = (int8_t)o[i];
            base.openPts += cpts(o[i]);
        }
        for (int c = 0; c < NUM_CARDS; ++c)
            if (g.isSeen(c)) {
                ++base.seenRank[rk(c)];
                ++base.seenTotal;
            }
        for (int i = 0; i < t.numSides; ++i) base.sideCards[i] = g.sideCapturedCount(i);

        // Unknown cards and where they must go.
        std::vector<int> unknown, forcedDeckBottom, forcedDealer;
        std::array<bool, NUM_CARDS> mine{};
        for (int c : h) mine[c] = true;
        std::array<bool, NUM_CARDS> buried{};
        for (int c : g.buriedCards()) buried[c] = true;
        for (int c = 0; c < NUM_CARDS; ++c) {
            if (g.isSeen(c) || mine[c]) continue;
            if (buried[c] && g.deckCount() > 0) forcedDeckBottom.push_back(c);
            else if (buried[c] && g.dealer() != seat && g.handCount(g.dealer()) > 0 && g.deckCount() == 0)
                forcedDealer.push_back(c);
            else unknown.push_back(c);
        }

        const int nc = (int)cands.size();
        // Sample budget: about `plays` simulated plays per decision (a simulated play costs ~0.05 µs at -O2).
        int remaining = g.deckCount();
        for (int s : g.activeSeats()) remaining += g.handCount(s);
        const int S = std::max(MIN_SAMPLES, std::min(MAX_SAMPLES, plays / std::max(1, nc * remaining)));
        val.assign((size_t)nc * S, 0.0);
        std::vector<int> pool;
        std::vector<char> used;
        for (int k = 0; k < S; ++k) {
            pool = unknown;
            rng.shuffle(pool);
            Sim w = base;
            // Hands first for the players known to lack some ranks (they passed on a capture), then the rest.
            used.assign(pool.size(), 0);
            for (int sIdx = 0; sIdx < t.nActive; ++sIdx) {
                const int s = t.active[sIdx];
                w.handN[s] = 0;
                if (s == seat) {
                    for (int c : h) w.hand[s][w.handN[s]++] = (int8_t)c;
                    continue;
                }
                if (s == g.dealer())
                    for (int c : forcedDealer) w.hand[s][w.handN[s]++] = (int8_t)c;
                if (!excl[s]) continue;
                int got = 0;
                const int need = g.handCount(s) - w.handN[s];
                for (size_t i = 0; i < pool.size() && got < need; ++i)
                    if (!used[i] && !(excl[s] >> rk(pool[i]) & 1u)) {
                        used[i] = 2;
                        ++got;
                    }
                const bool ok = got == need;
                for (size_t i = 0; i < pool.size(); ++i)
                    if (used[i] == 2) {
                        if (ok) w.hand[s][w.handN[s]++] = (int8_t)pool[i];
                        used[i] = ok ? 1 : 0;
                    }
            }
            size_t pi = 0;
            auto take = [&]() {
                while (used[pi]) ++pi;
                used[pi] = 1;
                return pool[pi];
            };
            w.closedN = g.closedCount();
            for (int i = 0; i < w.closedN; ++i) w.closed[i] = (int8_t)take();
            for (int sIdx = 0; sIdx < t.nActive; ++sIdx) {
                const int s = t.active[sIdx];
                while (w.handN[s] < g.handCount(s)) w.hand[s][w.handN[s]++] = (int8_t)take();
            }
            w.deckN = 0;
            for (int c : forcedDeckBottom) w.deck[w.deckN++] = (int8_t)c;
            while (w.deckN < g.deckCount()) w.deck[w.deckN++] = (int8_t)take();

            for (int ci = 0; ci < nc; ++ci) {
                Sim x = w;
                View v;
                bool going = simPlay(x, t, P, cands[ci]);
                while (going) {
                    fillView(x, t, x.cur, v);
                    going = simPlay(x, t, P, bestCard(v, P));
                }
                val[(size_t)ci * S + k] = simValue(x, t, g.rules().majorityPoints, mySide);
            }
        }
        return S;
    }

    // Equivalent cards (same rank, same points) give the same result: one of each class.
    static std::vector<int> candidates(const std::vector<int>& h) {
        std::vector<int> cands;
        for (int c : h) {
            bool dup = false;
            for (int d : cands)
                if (rk(d) == rk(c) && cpts(d) == cpts(c)) dup = true;
            if (!dup) cands.push_back(c);
        }
        return cands;
    }

    int kurt(const Game& g, int seat) {
        const std::vector<int> cands = candidates(g.hand(seat));
        if (cands.size() == 1) return cands[0];
        const Params P = paramsFor(g);
        const int nc = (int)cands.size();
        std::vector<double> val;
        const int S = sampleValues(g, seat, cands, rng, ROLLOUT_PLAYS, val);

        // Start from the Usta choice and switch only to a candidate that is better by a margin the samples
        // support (paired over the same worlds): sampling noise must not override sound judgment.
        std::array<int8_t, 8> hb{};
        std::array<int8_t, 52> ob{};
        View mv = viewOf(g, seat, hb, ob);
        mv.nextExcl = excl[g.nextActive(seat)];
        const int ustaCard = bestCard(mv, P);
        int base0 = 0;
        for (int ci = 0; ci < nc; ++ci)
            if (rk(cands[ci]) == rk(ustaCard) && cpts(cands[ci]) == cpts(ustaCard)) base0 = ci;
        const double zNeed = SWITCH_Z;
        int best = base0;
        double bestMean = 0.0;
        for (int ci = 0; ci < nc; ++ci) {
            if (ci == base0) continue;
            double m = 0, q = 0;
            for (int k = 0; k < S; ++k) {
                const double d = val[(size_t)ci * S + k] - val[(size_t)base0 * S + k];
                m += d;
                q += d * d;
            }
            m /= S;
            const double se = std::sqrt(std::max(0.0, (q / S - m * m) / std::max(1, S - 1)));
            if (m > zNeed * se && m > bestMean) {
                bestMean = m;
                best = ci;
            }
        }
        return cands[best];
    }
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(new Impl(level, seed)) {}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::resetForHand() { impl_->reset(); }
void Bot::observe(const GameEvent& e, const Game&) { impl_->observe(e); }

std::vector<CardValue> Bot::evaluate(const Game& g, int seat) const {
    std::vector<CardValue> out;
    if (seat < 0 || seat >= 4 || g.stage() != Stage::Playing || g.current() != seat || g.hand(seat).empty()) return out;
    const std::vector<int>& h = g.hand(seat);
    const std::vector<int> cands = Impl::candidates(h);
    const int nc = (int)cands.size();
    std::vector<double> val;
    // its own random numbers (from the state), so the bot's play is not disturbed and the result is reproducible
    uint64_t z = 0x9157A11ull ^ ((uint64_t)g.handIndex() << 32) ^ ((uint64_t)g.turnNumber() << 8) ^ (uint64_t)seat;
    for (int c : h) z = z * 0x9E3779B97F4A7C15ull + (uint64_t)c + 1;
    Rng r(z);
    const int S = nc > 1 ? impl_->sampleValues(g, seat, cands, r, 2 * ROLLOUT_PLAYS, val) : 0;
    std::vector<double> mean((size_t)nc, 0.0);
    for (int ci = 0; ci < nc && S > 0; ++ci) {
        for (int k = 0; k < S; ++k) mean[(size_t)ci] += val[(size_t)ci * S + k];
        mean[(size_t)ci] /= S;
    }
    int best = 0;
    for (int ci = 1; ci < nc; ++ci)
        if (mean[(size_t)ci] > mean[(size_t)best]) best = ci;
    std::vector<double> se((size_t)nc, 0.0);
    for (int ci = 0; ci < nc && S > 1; ++ci) {
        if (ci == best) continue;
        double m = 0, q = 0;
        for (int k = 0; k < S; ++k) {
            const double d = val[(size_t)ci * S + k] - val[(size_t)best * S + k];
            m += d;
            q += d * d;
        }
        m /= S;
        se[(size_t)ci] = std::sqrt(std::max(0.0, (q / S - m * m) / (S - 1)));
    }
    for (int c : h)
        for (int ci = 0; ci < nc; ++ci)
            if (rk(cands[(size_t)ci]) == rk(c) && cpts(cands[(size_t)ci]) == cpts(c)) {
                out.push_back({c, mean[(size_t)ci], se[(size_t)ci]});
                break;
            }
    return out;
}

int Bot::next(const Game& g, int seat) {
    if (seat < 0 || seat >= 4 || g.stage() != Stage::Playing || g.current() != seat || g.hand(seat).empty())
        return -1;
    switch (impl_->level) {
    case BotLevel::Acemi: return impl_->acemi(g, seat);
    case BotLevel::Usta: return impl_->usta(g, seat);
    case BotLevel::Kurt: return impl_->kurt(g, seat);
    }
    return fallbackCard(g, seat);
}

} // namespace pisti
