// King computer opponents (see KingBot.h). Pure logic, deterministic for a given seed and call sequence.
#include "core/KingBot.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace king {

using namespace kart;

namespace {

constexpr int NUM_TRICKS = 13;

inline int rankIdx(int card) { return card % 13; } // 0 = "2" .. 12 = As
inline CardMask aboveMask(int card) { return suitMask(suitOf(card)) & ~((cardBit(card) << 1) - 1); }
inline CardMask belowMask(int card) { return suitMask(suitOf(card)) & (cardBit(card) - 1); }

const CardMask* penaltyTable() {
    static CardMask t[NUM_CONTRACTS];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int i = 0; i < NUM_CONTRACTS; ++i) t[i] = penaltyCards((Contract)i);
    });
    return t;
}

// 2/3 ^ k, for "a given opponent holds none of k cards" style estimates.
inline double twoThirdsPow(int k) {
    static const double t[14] = {1.0,          0.6666666667, 0.4444444444, 0.2962962963, 0.1975308642,
                                 0.1316872428, 0.0877914952, 0.0585276635, 0.0390184423, 0.0260122949,
                                 0.0173415299, 0.0115610199, 0.0077073466, 0.0051382311};
    return k < 14 ? t[k < 0 ? 0 : k] : 0.0;
}

// ---------------------------------------------------------------------------------------------------------
// A compact hand state on card masks: used to replay the public history (inference) and for rollouts.
// ---------------------------------------------------------------------------------------------------------
struct Sim {
    const Rules* R = nullptr;
    Contract c = Contract::ElAlmaz;
    int trump = -1;
    CardMask hand[4] = {0, 0, 0, 0};
    int handCount[4] = {13, 13, 13, 13}; // public hand sizes
    CardMask played = 0;
    CardMask cannot[4] = {0, 0, 0, 0}; // public: cards a seat cannot hold (inferred from forced plays)
    int leader = 0;
    int n = 0;
    int cards[4] = {-1, -1, -1, -1};
    bool heartsBroken = false;
    bool trumpBroken = false;
    int trickIdx = 0;
    int pts[4] = {0, 0, 0, 0};
    bool done = false;

    int toMove() const { return (leader + n) & 3; }
    TrickContext ctx() const {
        TrickContext t;
        t.contract = c;
        t.trump = trump;
        t.n = n;
        for (int i = 0; i < n; ++i) t.cards[i] = cards[i];
        t.heartsBroken = heartsBroken;
        t.trumpBroken = trumpBroken;
        return t;
    }
    CardMask legal(int seat) const { return legalMask(*R, ctx(), hand[seat]); }

    void play(int card, bool infer) {
        const int s = toMove();
        const CardMask b = cardBit(card);
        if (infer) {
            PlayFilter f[MAX_FILTERS];
            const int k = playFilters(*R, ctx(), f);
            CardMask ex = 0;
            for (int i = 0; i < k; ++i) {
                if (f[i].mask & b) break;
                ex |= f[i].mask;
            }
            cannot[s] |= ex & ~played & ~b;
        }
        hand[s] &= ~b;
        --handCount[s];
        played |= b;
        cards[n++] = card;
        if (suitOf(card) == Kupa) heartsBroken = true;
        if (c == Contract::Koz && suitOf(card) == trump) trumpBroken = true;
        if (n == 4) {
            const int w = (leader + trickWinnerIndex(cards, 4, c, trump)) & 3;
            const CardMask m = cardBit(cards[0]) | cardBit(cards[1]) | cardBit(cards[2]) | cardBit(cards[3]);
            pts[w] += trickPoints(*R, c, m, trickIdx);
            ++trickIdx;
            leader = w;
            n = 0;
            if (trickIdx >= NUM_TRICKS || (R->endEarly && isCardCeza(c) && (penaltyTable()[(int)c] & ~played) == 0))
                done = true;
        }
    }
};

// Public state of the current hand from `me`'s seat: own hand known, others unknown (0).
Sim simFromGame(const Game& g, int me) {
    Sim s;
    s.R = &g.rules();
    s.c = g.contract();
    s.trump = g.trump();
    CardMask mine = maskOf(g.hand(me));
    for (const PlayRecord& p : g.plays())
        if (p.seat == me) mine |= cardBit(p.card);
    s.hand[me] = mine;
    s.handCount[0] = s.handCount[1] = s.handCount[2] = s.handCount[3] = 13;
    const std::vector<PlayRecord>& plays = g.plays();
    s.leader = g.chooser(); // the chooser leads the first trick
    for (const PlayRecord& p : plays) s.play(p.card, true);
    // Defensive: trust the engine for whose turn it is and the points.
    for (int i = 0; i < 4; ++i) {
        s.pts[i] = g.seat(i).handPoints;
        s.handCount[i] = g.handSize(i);
    }
    s.hand[me] = maskOf(g.hand(me));
    for (int i = 0; i < 4; ++i)
        if (i != me) s.cannot[i] |= s.hand[me];
    return s;
}

// ---------------------------------------------------------------------------------------------------------
// Usta card play (also the rollout policy of Kurt). Uses only seat's own hand + public state.
// ---------------------------------------------------------------------------------------------------------

int cheapestKoz(CardMask m, int trump) {
    const CardMask plain = m & ~suitMask(trump);
    if (plain) {
        int best = -1;
        for (CardMask t = plain; t; t &= t - 1) {
            const int c = __builtin_ctzll(t);
            if (best < 0 || rankIdx(c) < rankIdx(best)) best = c;
        }
        return best;
    }
    return lowestCard(m);
}

// Probability that no player after `seat` in this trick beats `x` (koz game).
double kozHold(const Sim& s, int seat, int x, int followers, CardMask U) {
    const int T = s.trump;
    const int led = s.n > 0 ? suitOf(s.cards[0]) : suitOf(x);
    const int sx = suitOf(x);
    double p = 1.0;
    for (int i = 1; i <= followers; ++i) {
        const int f = (seat + i) & 3;
        const CardMask Uf = U & ~s.cannot[f];
        double pb;
        const int higherTrumps = popcount(Uf & suitMask(T) & (sx == T ? aboveMask(x) : suitMask(T)));
        const double pTrumpHigher = 1.0 - twoThirdsPow(higherTrumps);
        if (sx == T && led == T) {
            pb = pTrumpHigher;
        } else {
            const int ledCnt = popcount(Uf & suitMask(led));
            const double pVoid = ledCnt == 0 ? 1.0 : twoThirdsPow(ledCnt);
            if (sx == T) { // my ruff: beaten only by a void player with a higher trump
                pb = pVoid * pTrumpHigher;
            } else {
                const int hi = popcount(Uf & aboveMask(x));
                pb = (1.0 - pVoid) * (1.0 - twoThirdsPow(hi)) + pVoid * pTrumpHigher;
            }
        }
        p *= 1.0 - std::min(1.0, pb);
    }
    return p;
}

int kozDiscard(const Sim& s, int seat, CardMask legal, CardMask U) {
    const CardMask my = s.hand[seat];
    const bool hasTrumps = (my & suitMask(s.trump)) != 0;
    int best = -1;
    double bestKeep = 1e9;
    for (CardMask t = legal; t; t &= t - 1) {
        const int x = __builtin_ctzll(t);
        const int sx = suitOf(x);
        double keep;
        if (sx == s.trump)
            keep = 100 + rankIdx(x);
        else if ((U & aboveMask(x)) == 0 && (U & suitMask(sx)))
            keep = 50 + rankIdx(x);
        else
            keep = rankIdx(x) + (hasTrumps ? 1.5 * popcount(my & suitMask(sx)) : 0.0);
        if (keep < bestKeep) {
            bestKeep = keep;
            best = x;
        }
    }
    return best;
}

int ustaKoz(const Sim& s, int seat, CardMask legal) {
    const int T = s.trump;
    const CardMask my = s.hand[seat];
    const CardMask U = ALL_CARDS & ~s.played & ~my;
    if (s.n > 0) {
        const int followers = 3 - s.n;
        const int w = s.cards[trickWinnerIndex(s.cards, s.n, Contract::Koz, T)];
        CardMask win = 0;
        for (CardMask t = legal; t; t &= t - 1) {
            const int x = __builtin_ctzll(t);
            const bool beats = (suitOf(x) == suitOf(w)) ? x > w : suitOf(x) == T;
            if (beats) win |= cardBit(x);
        }
        if (!win) return kozDiscard(s, seat, legal, U);
        if (followers == 0) return cheapestKoz(win, T);
        int sure = -1, bestX = -1;
        double bestP = -1;
        // Cheapest card that holds with high probability; else the most likely winner.
        for (CardMask t = win; t; t &= t - 1) {
            const int x = __builtin_ctzll(t);
            const double p = kozHold(s, seat, x, followers, U);
            if (p >= 0.7) {
                const int key = (suitOf(x) == T ? 100 : 0) + rankIdx(x);
                const int skey = sure < 0 ? 1 << 20 : (suitOf(sure) == T ? 100 : 0) + rankIdx(sure);
                if (key < skey) sure = x;
            }
            if (p > bestP + 1e-9) {
                bestP = p;
                bestX = x;
            }
        }
        if (sure >= 0) return sure;
        if (bestP >= 0.35) return bestX;
        const CardMask lose = legal & ~win;
        return lose ? kozDiscard(s, seat, lose, U) : cheapestKoz(win, T);
    }
    // Leading.
    const CardMask myT = my & suitMask(T);
    const CardMask UT = U & suitMask(T);
    if (legal & myT) {
        const int top = highestCard(myT);
        if (UT && (UT & aboveMask(top)) == 0 && (popcount(myT) >= popcount(UT) || popcount(UT) <= 2)) return top;
    }
    int bestBoss = -1;
    double bestP = 0;
    for (int su = 0; su < 4; ++su) {
        if (su == T) continue;
        const CardMask mine = legal & suitMask(su);
        if (!mine) continue;
        const int top = highestCard(mine);
        if ((U & aboveMask(top)) != 0) continue;
        double p = (U & suitMask(su)) ? kozHold(s, seat, top, 3, U) : (UT ? 0.3 : 1.0);
        if (p > bestP) {
            bestP = p;
            bestBoss = top;
        }
    }
    if (bestBoss >= 0 && bestP >= 0.55) return bestBoss;
    if ((legal & myT) && !UT) return lowestCard(legal & myT); // nobody else has trumps: every trump wins
    // Low lead: from the shortest side suit when holding trumps (make a void to ruff), else the longest.
    int bestSuit = -1, bestLen = 0;
    for (int su = 0; su < 4; ++su) {
        if (su == T) continue;
        const int len = popcount(legal & suitMask(su));
        if (!len) continue;
        const bool better = bestSuit < 0 || (myT ? len < bestLen : len > bestLen);
        if (better) {
            bestSuit = su;
            bestLen = len;
        }
    }
    if (bestSuit >= 0) return lowestCard(legal & suitMask(bestSuit));
    return lowestCard(legal);
}

int ustaCeza(const Sim& s, int seat, CardMask legal) {
    const Rules& R = *s.R;
    const Contract c = s.c;
    const double unit = R.unit[(int)c];
    const CardMask P = penaltyTable()[(int)c];
    const CardMask my = s.hand[seat];
    const CardMask U = ALL_CARDS & ~s.played & ~my;
    const int tricksLeft = NUM_TRICKS - s.trickIdx;
    const bool cardCeza = isCardCeza(c);
    const bool forceDrop = R.forceDropUnderHigher &&
                           (c == Contract::KizAlmaz || c == Contract::ErkekAlmaz || c == Contract::Rifki);

    double perTrick;
    if (c == Contract::ElAlmaz)
        perTrick = unit;
    else if (c == Contract::SonIki)
        perTrick = tricksLeft <= 2 ? unit : unit * 3.0 / tricksLeft;
    else
        perTrick = unit * popcount(P & ~s.played) / std::max(1, tricksLeft) * 1.3;

    const int led = s.n > 0 ? suitOf(s.cards[0]) : -1;
    int hiLed = -1;
    double trickPen = 0;
    for (int i = 0; i < s.n; ++i) {
        if (suitOf(s.cards[i]) == led && s.cards[i] > hiLed) hiLed = s.cards[i];
        if (P & cardBit(s.cards[i])) trickPen += unit;
    }
    const int followers = 3 - s.n;

    int best = -1;
    double bestScore = 1e18;
    for (CardMask t = legal; t; t &= t - 1) {
        const int x = __builtin_ctzll(t);
        const int sx = suitOf(x);
        const CardMask Us = U & suitMask(sx);
        const double penX = (P & cardBit(x)) ? unit : 0.0;
        // Chance this card takes the trick.
        double pw;
        if (s.n > 0 && (sx != led || x < hiLed)) {
            pw = 0.0;
        } else {
            pw = 1.0;
            const int Uc = popcount(Us);
            const double m = std::max(1.0, Uc / 3.0);
            for (int i = 1; i <= followers; ++i) {
                const int f = (seat + i) & 3;
                const CardMask Uf = Us & ~s.cannot[f];
                const int Hf = popcount(Uf & aboveMask(x));
                if (!Hf) continue;
                pw *= 1.0 - std::pow((double)Hf / popcount(Uf), m);
            }
        }
        // Expected cost when it does.
        double cw = 0;
        if (pw > 0) {
            if (c == Contract::ElAlmaz)
                cw = unit;
            else if (c == Contract::SonIki)
                cw = s.trickIdx >= NUM_TRICKS - 2 ? unit : (s.trickIdx == NUM_TRICKS - 3 ? 0.35 * unit : 0.0);
            else {
                cw = trickPen + penX;
                const int ledSuit = s.n > 0 ? led : sx;
                for (int i = 1; i <= followers; ++i) {
                    const int f = (seat + i) & 3;
                    const CardMask canHold = U & ~s.cannot[f];
                    const CardMask Usf = canHold & suitMask(ledSuit);
                    const double pFollow = Usf ? 1.0 - twoThirdsPow(popcount(Usf)) : 0.0;
                    double add;
                    if (c == Contract::KupaAlmaz)
                        add = pFollow * (ledSuit == Kupa ? unit : 0.0);
                    else if (forceDrop && (Usf & P & belowMask(x)))
                        add = unit * popcount(Usf & P & belowMask(x)) / 3.0;
                    else
                        add = pFollow * unit * popcount(Usf & P) / std::max(1, popcount(Usf)) * 0.5;
                    double dump = 0;
                    const CardMask Pf = canHold & P;
                    if (Pf) {
                        const double frac = (double)popcount(Pf) / std::max(1, popcount(canHold));
                        dump = unit * (1.0 - std::pow(1.0 - frac, std::max(1, s.handCount[f])));
                    }
                    cw += add + (1.0 - pFollow) * dump;
                }
            }
        }
        // Expected future cost of keeping this card (it may win a trick later / I may take it myself).
        const int L = popcount(Us & belowMask(x)), H = popcount(Us & aboveMask(x));
        const double pEv = (L + H == 0) ? 0.6 : (double)L / (L + H) * std::pow(0.6, H);
        double dang = pEv * perTrick;
        if (penX > 0) dang += penX * std::max(pEv, 0.15);
        if (!cardCeza && c == Contract::SonIki) dang *= tricksLeft <= 4 ? 1.5 : 1.0;
        const double score = pw * cw - dang;
        if (score < bestScore - 1e-9 || (std::fabs(score - bestScore) <= 1e-9 && x > best)) {
            bestScore = score;
            best = x;
        }
    }
    return best;
}

int ustaCard(const Sim& s, int seat) {
    const CardMask legal = s.legal(seat);
    if (popcount(legal) <= 1) return lowestCard(legal);
    return s.c == Contract::Koz ? ustaKoz(s, seat, legal) : ustaCeza(s, seat, legal);
}

// ---------------------------------------------------------------------------------------------------------
// Acemi card play: simple, a bit careless.
// ---------------------------------------------------------------------------------------------------------
int acemiCard(const Sim& s, int seat, Rng& rng) {
    const CardMask legal = s.legal(seat);
    if (popcount(legal) <= 1) return lowestCard(legal);
    if (rng.chance(0.2f)) {
        const std::vector<int> v = cardsOf(legal);
        return v[rng.range((int)v.size())];
    }
    auto byRank = [&](CardMask m, bool high) {
        int best = -1;
        for (CardMask t = m; t; t &= t - 1) {
            const int c = __builtin_ctzll(t);
            if (best < 0 || (high ? rankIdx(c) > rankIdx(best) : rankIdx(c) < rankIdx(best))) best = c;
        }
        return best;
    };
    if (s.c == Contract::Koz) {
        if (s.n == 0) return byRank(legal, true);
        const int w = s.cards[trickWinnerIndex(s.cards, s.n, Contract::Koz, s.trump)];
        CardMask win = 0;
        for (CardMask t = legal; t; t &= t - 1) {
            const int x = __builtin_ctzll(t);
            if (suitOf(x) == suitOf(w) ? x > w : suitOf(x) == s.trump) win |= cardBit(x);
        }
        return win ? byRank(win, true) : byRank(legal, false);
    }
    const CardMask P = penaltyTable()[(int)s.c];
    if (s.n == 0) return byRank(legal, false);
    const int led = suitOf(s.cards[0]);
    if (!(legal & suitMask(led))) { // discarding: penalty card first, else the highest card
        if (legal & P) return byRank(legal & P, true);
        return byRank(legal, true);
    }
    int hi = -1;
    for (int i = 0; i < s.n; ++i)
        if (suitOf(s.cards[i]) == led && s.cards[i] > hi) hi = s.cards[i];
    const CardMask under = legal & belowMask(hi);
    if (under) return highestCard(under);
    return s.n == 3 ? highestCard(legal) : lowestCard(legal);
}

// ---------------------------------------------------------------------------------------------------------
// Contract evaluation (Usta): rough expected points of the chooser for a hand, per contract.
// ---------------------------------------------------------------------------------------------------------
const double kHighW[13] = {0.02, 0.02, 0.02, 0.03, 0.04, 0.05, 0.08, 0.12, 0.18, 0.28, 0.42, 0.6, 0.8};

double kozTricks(CardMask h, int T) {
    const CardMask tm = h & suitMask(T);
    const int tl = popcount(tm);
    auto has = [&](int suit, int rank) { return (h & cardBit(makeCard(suit, rank))) != 0; };
    double tr = 0;
    if (has(T, As)) tr += 1.0;
    if (has(T, Papaz)) tr += tl >= 2 ? 0.9 : 0.4;
    if (has(T, Kiz)) tr += tl >= 3 ? 0.75 : 0.25;
    if (has(T, Vale)) tr += tl >= 4 ? 0.5 : 0.1;
    tr += std::max(0.0, tl - 3.5) * 0.85;
    for (int su = 0; su < 4; ++su) {
        if (su == T) continue;
        const int l = popcount(h & suitMask(su));
        if (has(su, As)) tr += l <= 6 ? 0.85 : 0.5;
        if (has(su, Papaz)) tr += l >= 2 ? (has(su, As) ? 0.75 : 0.5) : 0.15;
        if (has(su, Kiz) && l >= 3) tr += 0.25;
        if (tl >= 3) tr += l == 0 ? 0.9 : (l == 1 ? 0.55 : (l == 2 ? 0.25 : 0.0));
    }
    return tr;
}

double estimate(CardMask h, Contract c, int T) {
    auto has = [&](int suit, int rank) { return (h & cardBit(makeCard(suit, rank))) != 0; };
    auto len = [&](int su) { return popcount(h & suitMask(su)); };
    double e = 0;
    switch (c) {
    case Contract::Koz: return 50.0 * kozTricks(h, T);
    case Contract::ElAlmaz: {
        for (CardMask t = h; t; t &= t - 1) {
            const int x = __builtin_ctzll(t);
            e += kHighW[rankIdx(x)] * (1.0 + 0.06 * (len(suitOf(x)) - 3));
        }
        return -50.0 * e;
    }
    case Contract::KupaAlmaz: {
        for (CardMask t = h; t; t &= t - 1) {
            const int x = __builtin_ctzll(t);
            e += suitOf(x) == Kupa ? kHighW[rankIdx(x)] * 2.6 : kHighW[rankIdx(x)] * 0.7;
        }
        e -= 0.15 * std::max(0, len(Kupa) - 3) * (has(Kupa, As) || has(Kupa, Papaz) ? 0.0 : 1.0);
        return -30.0 * std::max(0.0, e);
    }
    case Contract::KizAlmaz: {
        for (int su = 0; su < 4; ++su) {
            const int l = len(su);
            if (has(su, Kiz)) {
                double p = l == 1 ? 0.45 : (l >= 4 ? 0.2 : 0.3);
                if (has(su, Papaz)) p += 0.25;
                if (has(su, As)) p += 0.25;
                e += std::min(0.95, p);
            } else {
                if (has(su, As)) e += l <= 2 ? 0.3 : 0.18;
                if (has(su, Papaz)) e += l <= 2 ? 0.22 : 0.12;
            }
        }
        return -100.0 * e;
    }
    case Contract::ErkekAlmaz: {
        for (int su = 0; su < 4; ++su) {
            const int l = len(su);
            if (has(su, Papaz)) e += has(su, As) ? 0.65 : (l == 1 ? 0.55 : 0.4);
            if (has(su, Vale)) e += (has(su, Kiz) || has(su, Papaz) || has(su, As)) ? 0.4 : 0.25;
            if (has(su, As)) e += 0.3;
            if (has(su, Kiz)) e += 0.15;
        }
        return -60.0 * e;
    }
    case Contract::Rifki: {
        const int l = len(Kupa);
        if (has(Kupa, Papaz))
            e = has(Kupa, As) ? 0.6 : (l == 1 ? 0.7 : std::max(0.12, 0.55 - 0.08 * (l - 1)));
        else {
            e = has(Kupa, As) ? 0.25 : 0.12;
            for (int su = 0; su < 4; ++su)
                if (su != Kupa && has(su, As)) e += 0.03;
        }
        return -320.0 * e;
    }
    case Contract::SonIki: {
        for (int su = 0; su < 4; ++su) e += 0.18 * std::max(0, len(su) - 4);
        for (CardMask t = h; t; t &= t - 1) e += 0.12 * kHighW[rankIdx(__builtin_ctzll(t))];
        return -180.0 * e;
    }
    }
    return 0;
}

// Average estimate over random hands (the "opportunity cost" of using a contract now).
const std::array<double, NUM_CONTRACTS>& ustaBaselines() {
    static std::array<double, NUM_CONTRACTS> b{};
    static std::once_flag once;
    std::call_once(once, [] {
        Rng rng(0x6B696E67ull);
        const int N = 600;
        for (int i = 0; i < N; ++i) {
            const std::vector<int> d = shuffledDeck(rng);
            CardMask h = 0;
            for (int k = 0; k < 13; ++k) h |= cardBit(d[k]);
            for (int c = 0; c < NUM_CEZA; ++c) b[c] += estimate(h, (Contract)c, -1);
            double bestKoz = -1e9;
            for (int T = 0; T < 4; ++T) bestKoz = std::max(bestKoz, estimate(h, Contract::Koz, T));
            b[(int)Contract::Koz] += bestKoz;
        }
        for (double& v : b) v /= N;
    });
    return b;
}

// Kurt's baselines: mean chooser points for a random hand with all-Usta play (from tests/king_sim
// --baselines, 4000 deals per contract; koz = best suit by kozTricks).
const double kKurtBaseline[NUM_CONTRACTS] = {-203.1, -95.5, -134.6, -116.5, -82.8, -85.1, 217.8};

int bestKozSuit(CardMask h) {
    int best = 0;
    double bv = -1;
    for (int T = 0; T < 4; ++T) {
        const double v = kozTricks(h, T) + 0.01 * popcount(h & suitMask(T));
        if (v > bv) {
            bv = v;
            best = T;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------
// Determinization: deal the unknown cards to the other seats consistently with `cannot` and hand sizes.
// ---------------------------------------------------------------------------------------------------------
// kozChooser >= 0: koz game chosen by that (other) seat, who already played `chooserTrumps` trumps.
bool sampleHands(const Sim& pub, int me, Rng& rng, Sim& out, int kozChooser, int chooserTrumps) {
    const CardMask unknown = ALL_CARDS & ~pub.played & ~pub.hand[me];
    std::vector<int> cards = cardsOf(unknown);
    int need = 0;
    for (int s = 0; s < 4; ++s)
        if (s != me) need += pub.handCount[s];
    if (need != (int)cards.size()) return false;
    struct Item {
        int card;
        int allowed; // seat bits
        int nAllowed;
    };
    std::vector<Item> items(cards.size());
    Sim best;
    bool have = false;
    for (int attempt = 0; attempt < 24; ++attempt) {
        rng.shuffle(cards);
        for (size_t i = 0; i < cards.size(); ++i) {
            int a = 0;
            for (int s = 0; s < 4; ++s)
                if (s != me && !(pub.cannot[s] & cardBit(cards[i]))) a |= 1 << s;
            const bool relax = attempt >= 16; // last resort: ignore the inference
            if (relax || !a)
                for (int s = 0; s < 4; ++s)
                    if (s != me) a |= 1 << s;
            items[i] = Item{cards[i], a, __builtin_popcount((unsigned)a)};
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.nAllowed < y.nAllowed; });
        int cap[4];
        for (int s = 0; s < 4; ++s) cap[s] = s == me ? 0 : pub.handCount[s];
        CardMask h[4] = {0, 0, 0, 0};
        bool ok = true;
        for (const Item& it : items) {
            int tot = 0;
            for (int s = 0; s < 4; ++s)
                if ((it.allowed >> s) & 1) tot += cap[s];
            if (tot <= 0) {
                ok = false;
                break;
            }
            int r = rng.range(tot);
            for (int s = 0; s < 4; ++s) {
                if (!((it.allowed >> s) & 1)) continue;
                if (r < cap[s]) {
                    h[s] |= cardBit(it.card);
                    --cap[s];
                    break;
                }
                r -= cap[s];
            }
        }
        if (!ok) continue;
        Sim s = pub;
        for (int i = 0; i < 4; ++i)
            if (i != me) s.hand[i] = h[i];
        // A koz chooser usually holds a long trump suit: prefer such samples.
        if (kozChooser >= 0 && kozChooser != me && attempt < 8) {
            const int want = std::min(4 - chooserTrumps, popcount(unknown & suitMask(pub.trump)));
            if (popcount(h[kozChooser] & suitMask(pub.trump)) < want) {
                if (!have) {
                    best = s;
                    have = true;
                }
                continue;
            }
        }
        out = s;
        return true;
    }
    if (have) {
        out = best;
        return true;
    }
    return false;
}

double rollout(Sim s, int me) {
    while (!s.done) {
        const int seat = s.toMove();
        const int card = ustaCard(s, seat);
        if (card < 0) break;
        s.play(card, true);
    }
    return s.pts[me];
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------------------------------------

ActionResult applyBotAction(Game& g, int seat, const BotAction& a) {
    if (a.kind == BotAction::Kind::Choose) return g.chooseContract(seat, a.contract, a.trump);
    return g.playCard(seat, a.card);
}

BotAction fallbackAction(const Game& g, int seat) {
    BotAction a;
    if (g.stage() == Stage::Choosing) {
        a.kind = BotAction::Kind::Choose;
        for (const ContractOption& o : g.options(seat)) {
            if (!o.allowed) continue;
            a.contract = o.contract;
            a.trump = o.contract == Contract::Koz ? bestKozSuit(maskOf(g.hand(seat))) : -1;
            return a;
        }
        a.contract = Contract::Koz;
        a.trump = 0;
        return a;
    }
    a.kind = BotAction::Kind::Play;
    const std::vector<int> legal = g.legalCards(seat);
    a.card = legal.empty() ? (g.hand(seat).empty() ? -1 : g.hand(seat).front()) : legal.front();
    return a;
}

// ---------------------------------------------------------------------------------------------------------
// Bot
// ---------------------------------------------------------------------------------------------------------

// Personality (Bot::setStyle), boldness b in -1..1:
//   contract choice: a koz game gets STYLE_KOZ * b points of credit (Usta / Kurt; Acemi a higher koz chance), and
//   Kurt adds STYLE_RISK * b * the spread (standard deviation) of each option's simulated points;
//   card play in penalty games: Kurt adds STYLE_PLAY_RISK * b * the spread of each candidate's rollouts (a cautious
//   bot prefers the card with the steadier outcome). Usta's card play stays the tuned one: weighing the trick cost
//   by only 1 -+ 0.05 moved a styled Usta by about 100 points a match (sim, 100 deals), far from "modest".
constexpr double STYLE_KOZ = 40.0;
constexpr double STYLE_RISK = 0.15;
constexpr double STYLE_PLAY_RISK = 0.1;

struct Bot::Impl {
    BotLevel level;
    Rng rng;
    BotStyle style;
    Impl(BotLevel l, uint64_t seed) : level(l), rng(seed) {}
    double bold() const { return std::clamp((double)style.boldness, -1.0, 1.0); }

    BotAction choose(const Game& g, int seat);
    int playUsta(const Sim& s, int seat) { return ustaCard(s, seat); }
    int playKurt(const Game& g, const Sim& s, int seat);
};

BotAction Bot::Impl::choose(const Game& g, int seat) {
    BotAction a;
    a.kind = BotAction::Kind::Choose;
    const CardMask h = maskOf(g.hand(seat));
    std::vector<Contract> allowed;
    for (const ContractOption& o : g.options(seat))
        if (o.allowed) allowed.push_back(o.contract);
    if (allowed.empty()) return fallbackAction(g, seat);
    const bool kozOk = std::find(allowed.begin(), allowed.end(), Contract::Koz) != allowed.end();

    if (level == BotLevel::Acemi) {
        // A long suit tempts a koz; otherwise any allowed penalty.
        int longest = 0;
        for (int su = 0; su < 4; ++su) longest = std::max(longest, popcount(h & suitMask(su)));
        std::vector<Contract> cezas;
        for (Contract c : allowed)
            if (c != Contract::Koz) cezas.push_back(c);
        const float kb = 0.15f * (float)bold();
        if (kozOk && (cezas.empty() || (longest >= 5 && rng.chance(0.7f + kb)) || rng.chance(0.2f + kb))) {
            a.contract = Contract::Koz;
            int bestLen = -1;
            for (int su = 0; su < 4; ++su) {
                const int l = popcount(h & suitMask(su));
                if (l > bestLen) {
                    bestLen = l;
                    a.trump = su;
                }
            }
            return a;
        }
        a.contract = cezas[rng.range((int)cezas.size())];
        return a;
    }

    if (level == BotLevel::Usta) {
        const auto& base = ustaBaselines();
        double bestAdv = -1e18;
        for (Contract c : allowed) {
            double adv;
            int T = -1;
            if (c == Contract::Koz) {
                T = bestKozSuit(h);
                adv = estimate(h, c, T) - base[(int)c] + STYLE_KOZ * bold();
            } else {
                adv = estimate(h, c, -1) - base[(int)c];
            }
            if (adv > bestAdv) {
                bestAdv = adv;
                a.contract = c;
                a.trump = T;
            }
        }
        return a;
    }

    // Kurt: simulate each option with random deals of the other 39 cards, all-Usta play.
    struct Opt {
        Contract c;
        int trump;
    };
    std::vector<Opt> opts;
    for (Contract c : allowed) {
        if (c != Contract::Koz) {
            opts.push_back({c, -1});
            continue;
        }
        // Two most promising trump suits by the hand estimate.
        int order[4] = {0, 1, 2, 3};
        std::sort(order, order + 4, [&](int x, int y) {
            const double vx = kozTricks(h, x) + 0.01 * popcount(h & suitMask(x));
            const double vy = kozTricks(h, y) + 0.01 * popcount(h & suitMask(y));
            return vx > vy || (vx == vy && x < y);
        });
        opts.push_back({Contract::Koz, order[0]});
        opts.push_back({Contract::Koz, order[1]});
    }
    if (opts.size() == 1) {
        a.contract = opts[0].c;
        a.trump = opts[0].trump;
        return a;
    }
    const int S = 28;
    std::vector<double> sum(opts.size(), 0.0), sq(opts.size(), 0.0);
    const CardMask others = ALL_CARDS & ~h;
    std::vector<int> rest = cardsOf(others);
    for (int k = 0; k < S; ++k) {
        rng.shuffle(rest);
        Sim base;
        base.R = &g.rules();
        base.leader = seat;
        base.hand[seat] = h;
        int idx = 0;
        for (int s = 0; s < 4; ++s) {
            if (s == seat) continue;
            for (int j = 0; j < 13; ++j) base.hand[s] |= cardBit(rest[idx++]);
        }
        for (size_t o = 0; o < opts.size(); ++o) {
            Sim sm = base;
            sm.c = opts[o].c;
            sm.trump = opts[o].trump;
            const double v = rollout(sm, seat);
            sum[o] += v;
            sq[o] += v * v;
        }
    }
    double bestAdv = -1e18;
    for (size_t o = 0; o < opts.size(); ++o) {
        double adv = sum[o] / S - kKurtBaseline[(int)opts[o].c];
        if (bold() != 0.0) {
            const double mean = sum[o] / S, sd = std::sqrt(std::max(0.0, sq[o] / S - mean * mean));
            adv += bold() * (STYLE_RISK * sd + (opts[o].c == Contract::Koz ? STYLE_KOZ : 0.0));
        }
        if (adv > bestAdv) {
            bestAdv = adv;
            a.contract = opts[o].c;
            a.trump = opts[o].trump;
        }
    }
    return a;
}

int Bot::Impl::playKurt(const Game& g, const Sim& pub, int seat) {
    const CardMask legal = pub.legal(seat);
    if (popcount(legal) <= 1) return lowestCard(legal);
    // Candidates: merge cards that are equivalent (adjacent in the suit once played/own cards are skipped,
    // same penalty value).
    const CardMask P = penaltyTable()[(int)pub.c];
    const CardMask gone = pub.played | pub.hand[seat];
    std::vector<int> cands;
    int prev = -1;
    for (CardMask t = legal; t; t &= t - 1) {
        const int x = __builtin_ctzll(t);
        bool same = false;
        if (prev >= 0 && suitOf(prev) == suitOf(x) && ((P >> prev) & 1) == ((P >> x) & 1)) {
            const CardMask between = belowMask(x) & aboveMask(prev);
            same = (between & ~gone) == 0;
        }
        if (!same) cands.push_back(x);
        prev = x;
    }
    const int usta = ustaCard(pub, seat);
    if (cands.size() == 1) return usta >= 0 && (legal & cardBit(usta)) ? usta : cands[0];
    const int remaining = 52 - popcount(pub.played);
    const int budget = 26000; // simulated plays per decision
    int S = budget / std::max(1, (int)cands.size() * remaining);
    S = std::max(10, std::min(60, S));
    std::vector<double> sum(cands.size(), 0.0), sq(cands.size(), 0.0);
    const int kozChooser = pub.c == Contract::Koz ? g.chooser() : -1;
    int chooserTrumps = 0;
    for (const PlayRecord& p : g.plays())
        if (p.seat == kozChooser && suitOf(p.card) == pub.trump) ++chooserTrumps;
    int done = 0;
    for (int k = 0; k < S; ++k) {
        Sim det;
        if (!sampleHands(pub, seat, rng, det, kozChooser, chooserTrumps)) break;
        for (size_t i = 0; i < cands.size(); ++i) {
            Sim s = det;
            s.play(cands[i], false);
            const double v = rollout(s, seat);
            sum[i] += v;
            sq[i] += v * v;
        }
        ++done;
    }
    if (done == 0) return usta;
    int best = -1;
    double bv = -1e18;
    for (size_t i = 0; i < cands.size(); ++i) {
        // Small preference for the Usta choice on ties (and its equivalents).
        const bool isUsta = cands[i] == usta;
        double v = sum[i] / done + (isUsta ? 0.5 : 0.0);
        if (bold() != 0.0 && pub.c != Contract::Koz) {
            const double mean = sum[i] / done;
            v += bold() * STYLE_PLAY_RISK * std::sqrt(std::max(0.0, sq[i] / done - mean * mean));
        }
        if (v > bv) {
            bv = v;
            best = cands[i];
        }
    }
    return best;
}

Bot::Bot(BotLevel level, uint64_t seed) : impl_(new Impl(level, seed)) {}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::setStyle(BotStyle style) { impl_->style = style; }
BotStyle Bot::style() const { return impl_->style; }
void Bot::resetForHand() {}
void Bot::observe(const GameEvent&, const Game&) {}

namespace {
uint64_t stateSeed(const Game& g, int seat, uint64_t salt) {
    uint64_t z = salt ^ ((uint64_t)g.handIndex() << 40) ^ ((uint64_t)seat << 32) ^ g.playedMask();
    for (int c : g.hand(seat)) z = z * 0x9E3779B97F4A7C15ull + (uint64_t)c + 1;
    return z ? z : 1;
}

// Means of nc options over S paired samples, and the standard error of each one's difference to the best.
void pairedStats(const std::vector<double>& val, int nc, int S, std::vector<double>& mean, std::vector<double>& se,
                 const std::vector<double>* offset = nullptr) {
    mean.assign((size_t)nc, 0.0);
    se.assign((size_t)nc, 0.0);
    if (S <= 0) return;
    for (int i = 0; i < nc; ++i) {
        for (int k = 0; k < S; ++k) mean[(size_t)i] += val[(size_t)i * S + k];
        mean[(size_t)i] = mean[(size_t)i] / S - (offset ? (*offset)[(size_t)i] : 0.0);
    }
    int best = 0;
    for (int i = 1; i < nc; ++i)
        if (mean[(size_t)i] > mean[(size_t)best]) best = i;
    for (int i = 0; i < nc && S > 1; ++i) {
        if (i == best) continue;
        double m = 0, q = 0;
        for (int k = 0; k < S; ++k) {
            const double d = val[(size_t)i * S + k] - val[(size_t)best * S + k];
            m += d;
            q += d * d;
        }
        m /= S;
        se[(size_t)i] = std::sqrt(std::max(0.0, (q / S - m * m) / (S - 1)));
    }
}
} // namespace

std::vector<CardValue> Bot::evaluateCards(const Game& g, int seat) const {
    std::vector<CardValue> out;
    if (g.stage() != Stage::Playing || g.current() != seat) return out;
    const Sim pub = simFromGame(g, seat);
    const CardMask legal = pub.legal(seat);
    if (!legal) return out;
    // one candidate per class of equivalent cards (as Kurt plays), every legal card mapped to its class
    const CardMask P = penaltyTable()[(int)pub.c];
    const CardMask gone = pub.played | pub.hand[seat];
    std::vector<int> cands, classOf;
    std::vector<int> cards;
    int prev = -1;
    for (CardMask t = legal; t; t &= t - 1) {
        const int x = __builtin_ctzll(t);
        bool same = false;
        if (prev >= 0 && suitOf(prev) == suitOf(x) && ((P >> prev) & 1) == ((P >> x) & 1)) {
            const CardMask between = belowMask(x) & aboveMask(prev);
            same = (between & ~gone) == 0;
        }
        if (!same) cands.push_back(x);
        cards.push_back(x);
        classOf.push_back((int)cands.size() - 1);
        prev = x;
    }
    const int nc = (int)cands.size();
    const int remaining = 52 - popcount(pub.played);
    int S = nc == 1 ? 0 : std::max(24, std::min(120, 2 * 26000 / std::max(1, nc * remaining)));
    Rng rng(stateSeed(g, seat, 0x4196A1ull));
    const int kozChooser = pub.c == Contract::Koz ? g.chooser() : -1;
    int chooserTrumps = 0;
    for (const PlayRecord& p : g.plays())
        if (p.seat == kozChooser && suitOf(p.card) == pub.trump) ++chooserTrumps;
    std::vector<double> val((size_t)nc * (size_t)std::max(1, S), 0.0);
    int done = 0;
    for (int k = 0; k < S; ++k) {
        Sim det;
        if (!sampleHands(pub, seat, rng, det, kozChooser, chooserTrumps)) break;
        for (int i = 0; i < nc; ++i) {
            Sim s = det;
            s.play(cands[(size_t)i], false);
            val[(size_t)i * S + k] = rollout(s, seat);
        }
        ++done;
    }
    if (done < S) { // compact to the samples taken
        std::vector<double> v2((size_t)nc * (size_t)std::max(1, done), 0.0);
        for (int i = 0; i < nc; ++i)
            for (int k = 0; k < done; ++k) v2[(size_t)i * done + k] = val[(size_t)i * S + k];
        val.swap(v2);
        S = done;
    }
    std::vector<double> mean, se;
    pairedStats(val, nc, S, mean, se);
    for (size_t j = 0; j < cards.size(); ++j)
        out.push_back({cards[j], mean[(size_t)classOf[j]], se[(size_t)classOf[j]]});
    return out;
}

std::vector<ContractValue> Bot::evaluateContracts(const Game& g, int seat) const {
    std::vector<ContractValue> out;
    if (g.stage() != Stage::Choosing || g.chooser() != seat) return out;
    const CardMask h = maskOf(g.hand(seat));
    for (const ContractOption& o : g.options(seat)) {
        if (!o.allowed) continue;
        if (o.contract != Contract::Koz) {
            out.push_back({o.contract, -1, 0.0, 0.0});
            continue;
        }
        for (int su = 0; su < 4; ++su) out.push_back({Contract::Koz, su, 0.0, 0.0});
    }
    const int nc = (int)out.size();
    if (nc <= 1) return out;
    constexpr int S = 96;
    std::vector<double> val((size_t)nc * S, 0.0), offset((size_t)nc, 0.0);
    for (int i = 0; i < nc; ++i) offset[(size_t)i] = kKurtBaseline[(int)out[(size_t)i].contract];
    Rng rng(stateSeed(g, seat, 0xC0417ull));
    std::vector<int> rest = cardsOf(ALL_CARDS & ~h);
    for (int k = 0; k < S; ++k) {
        rng.shuffle(rest);
        Sim base;
        base.R = &g.rules();
        base.leader = seat;
        base.hand[seat] = h;
        int idx = 0;
        for (int s = 0; s < 4; ++s) {
            if (s == seat) continue;
            for (int j = 0; j < 13; ++j) base.hand[s] |= cardBit(rest[(size_t)idx++]);
        }
        for (int i = 0; i < nc; ++i) {
            Sim sm = base;
            sm.c = out[(size_t)i].contract;
            sm.trump = out[(size_t)i].trump;
            val[(size_t)i * S + k] = rollout(sm, seat);
        }
    }
    std::vector<double> mean, se;
    pairedStats(val, nc, S, mean, se, &offset);
    for (int i = 0; i < nc; ++i) {
        out[(size_t)i].value = mean[(size_t)i];
        out[(size_t)i].se = se[(size_t)i];
    }
    return out;
}

BotAction Bot::next(const Game& g, int seat) {
    if (g.stage() == Stage::Choosing && g.chooser() == seat) return impl_->choose(g, seat);
    if (g.stage() != Stage::Playing || g.current() != seat) return fallbackAction(g, seat);
    const Sim s = simFromGame(g, seat);
    BotAction a;
    a.kind = BotAction::Kind::Play;
    switch (impl_->level) {
    case BotLevel::Acemi: a.card = acemiCard(s, seat, impl_->rng); break;
    case BotLevel::Usta: a.card = impl_->playUsta(s, seat); break;
    case BotLevel::Kurt: a.card = impl_->playKurt(g, s, seat); break;
    }
    if (!g.isLegal(seat, a.card)) return fallbackAction(g, seat);
    return a;
}

} // namespace king
