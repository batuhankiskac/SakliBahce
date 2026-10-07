// Tavla bots: evaluation, play choice (Acemi / Usta / Kurt) and plan-and-replay of single steps.
#include "core/TavlaBot.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tavla {

namespace {

// ---------------------------------------------------------------------------------------------------------
// Evaluation weights. Three sets, one per level:
//   kClassic (Usta): hand-set kahvehane heuristics.
//   kTuned   (Kurt): the same features re-weighted by paired self-play (1-ply duplicate matches,
//                    20-30k games per step: hit risk proportional to the pips lost, stronger points and
//                    primes, cheaper bar). Worth about +0.2 points per game over kClassic at 1-ply.
//   kSloppy  (Acemi): underestimates hits, no idea of primes or anchors.
struct Weights {
    double pip = 1.0;
    double blotBase = 5.0, blotHome = 2.0, blotPip = 1.0; // cost of a hit blot: pips lost * blotPip + ...
    double oppBlot = 0.25;  // the opponent's blots count this much (he moves before I can hit them)
    double point = 1.0;     // scale of kPointVal for my points (except anchors)
    double prime = 1.0;
    double barBase = 6.0, barHome = 2.5; // a checker on the bar vs the opponent's made home points
    double stack = 1.0;
    double off = 1.5;       // per checker off in a contact bear-off
    double mars = 0.5;      // mars-risk term in contact positions
    double anchor = 1.0;    // scale of kPointVal for points in the opponent's half (anchors)
};
Weights tunedWeights() {
    Weights w;
    w.blotBase = 0.0;
    w.blotHome = 0.0;
    w.blotPip = 1.4;
    w.oppBlot = 0.5;
    w.point = 1.6;
    w.prime = 1.4;
    w.barBase = 3.0;
    w.barHome = 2.0;
    return w;
}
Weights sloppyWeights() {
    Weights w = tunedWeights();
    w.blotPip = 0.6;
    w.oppBlot = 0.0;
    w.prime = 0.0;
    w.anchor = 0.3;
    w.point = 0.7;
    w.barHome = 0.5;
    return w;
}
const Weights kClassic;
const Weights kTuned = tunedWeights();
const Weights kSloppy = sloppyWeights();
// Acemi: noise on its evaluation, and with this probability one of its 4 best-looking plays at random.
constexpr float kEasyRandom = 0.25f, kEasyNoise = 4.0f;
// Kurt: 2-ply on up to this many 1-ply candidates (contact / race), skipping candidates far behind the best.
constexpr size_t kKurtCandidates = 6, kKurtRaceCandidates = 3;
constexpr double kKurtMargin = 25.0;
// Kurt: cap on leaf evaluations per decision (keeps the worst case well under the time budget; candidates
// are searched best-first, so running out only skips the weakest ones).
constexpr long kKurtEvalBudget = 30000;

// Evaluation in the frame of the side that just moved ("me"):
//   rel r = 0..23, r = 0 is my 1-point; I move down (towards r = -1 = off), my bar is r = 24.
//   The opponent moves UP in this frame: he enters from r = -1 onto r = die-1 (my home board) and bears off
//   past r = 23; his home board is r = 18..23.
struct Frame {
    int me[24];
    int op[24];
    int meBar, opBar, meOff, opOff;
};

Frame makeFrame(const Position& pos, int p) {
    Frame f;
    for (int r = 0; r < 24; ++r) {
        const int i = p == 0 ? r : 23 - r;
        const int v = pos.pts[i];
        const int mine = p == 0 ? (v > 0 ? v : 0) : (v < 0 ? -v : 0);
        const int his = p == 0 ? (v < 0 ? -v : 0) : (v > 0 ? v : 0);
        f.me[r] = mine;
        f.op[r] = his;
    }
    f.meBar = pos.bar[p];
    f.opBar = pos.bar[1 - p];
    f.meOff = pos.off[p];
    f.opOff = pos.off[1 - p];
    return f;
}

Frame flip(const Frame& f) {
    Frame g;
    for (int r = 0; r < 24; ++r) {
        g.me[r] = f.op[23 - r];
        g.op[r] = f.me[23 - r];
    }
    g.meBar = f.opBar;
    g.opBar = f.meBar;
    g.meOff = f.opOff;
    g.opOff = f.meOff;
    return g;
}

int pipsMe(const Frame& f) {
    int s = f.meBar * 25;
    for (int r = 0; r < 24; ++r) s += f.me[r] * (r + 1);
    return s;
}

// Keith-style effective pip count for a race (bear-off wastage).
double keith(const Frame& f) {
    double k = pipsMe(f);
    k += 2.0 * std::max(0, f.me[0] - 1);
    k += 1.0 * std::max(0, f.me[1] - 1);
    k += 1.0 * std::max(0, f.me[2] - 3);
    for (int r = 3; r <= 5; ++r) k += f.me[r] == 0 ? 1.0 : 0.0;
    return k * 7.0 / 6.0;
}

bool contact(const Frame& f) {
    if (f.meBar || f.opBar) return true;
    int maxMe = -1, minOp = 24;
    for (int r = 23; r >= 0; --r)
        if (f.me[r]) { maxMe = r; break; }
    for (int r = 0; r < 24; ++r)
        if (f.op[r]) { minOp = r; break; }
    return maxMe > minOp;
}

// Pips still needed before I can bear off my first checker (to avoid mars).
double marsSavePips(const Frame& f) {
    if (f.meOff > 0) return 0;
    double s = f.meBar * 20.0;
    int low = 24;
    for (int r = 0; r < 24; ++r) {
        if (!f.me[r]) continue;
        if (r >= 6) s += f.me[r] * (r - 5);
        low = std::min(low, r);
    }
    s += low < 6 ? (low + 1) * 0.5 : 3.0;
    return s;
}

double marsTerm(const Frame& f, double opRolls) {
    // Risk that I am marsed: my rolls to first bear-off vs the opponent's rolls to finish.
    if (f.meOff > 0) return 0;
    const double myRolls = marsSavePips(f) / 8.17;
    const double x = (myRolls - opRolls + 1.5) / 2.0;
    return 14.0 * std::min(1.0, std::max(0.0, x));
}

// Expected loss (pips) of my blots to the opponent's next roll: for every roll the most expensive blot he
// can hit (direct, combination shots through open points, bar entry constraints).
double blotRisk(const Frame& f, int opHomeMade, const Weights& W) {
    double cost[24];
    bool anyBlot = false;
    for (int t = 0; t < 24; ++t) {
        if (f.me[t] == 1) {
            anyBlot = true;
            cost[t] = W.blotPip * (24 - t) + W.blotBase + W.blotHome * opHomeMade;
        } else {
            cost[t] = 0;
        }
    }
    if (!anyBlot) return 0;
    // the opponent must have something behind a blot
    int minOp = 24;
    if (f.opBar) minOp = -1;
    else
        for (int r = 0; r < 24; ++r)
            if (f.op[r]) { minOp = r; break; }
    bool exposed = false;
    for (int t = minOp + 1; t < 24 && !exposed; ++t) exposed = cost[t] > 0;
    if (!exposed) return 0;

    auto blocked = [&](int x) { return f.me[x] >= 2; };
    double total = 0;
    for (int a = 1; a <= 6; ++a) {
        for (int b = a; b <= 6; ++b) {
            const int w = a == b ? 1 : 2;
            double best = 0;
            auto check = [&](int t) {
                if (t >= 0 && t < 24 && cost[t] > best) best = cost[t];
            };
            if (a != b) {
                if (f.opBar >= 2) {
                    if (!blocked(a - 1)) check(a - 1);
                    if (!blocked(b - 1)) check(b - 1);
                } else if (f.opBar == 1) {
                    const int e[2] = {a, b}, o[2] = {b, a};
                    for (int k = 0; k < 2; ++k) {
                        const int t = e[k] - 1;
                        if (blocked(t)) continue;
                        check(t);
                        const int t2 = t + o[k];
                        if (t2 < 24 && !blocked(t2)) check(t2);
                        for (int s = 0; s < 24; ++s) {
                            if (!f.op[s]) continue;
                            const int u = s + o[k];
                            if (u < 24 && !blocked(u)) check(u);
                        }
                    }
                } else {
                    for (int s = minOp; s < 24; ++s) {
                        if (!f.op[s]) continue;
                        const int t1 = s + a, t2 = s + b;
                        if (t1 < 24 && !blocked(t1)) {
                            check(t1);
                            if (t1 + b < 24 && !blocked(t1 + b)) check(t1 + b);
                        }
                        if (t2 < 24 && !blocked(t2)) {
                            check(t2);
                            if (t2 + a < 24 && !blocked(t2 + a)) check(t2 + a);
                        }
                    }
                }
            } else {
                const int d = a;
                int remaining = 4;
                bool entered = false;
                if (f.opBar > 0) {
                    if (blocked(d - 1)) {
                        total += 0;
                        continue;
                    }
                    check(d - 1);
                    entered = true;
                    remaining = std::max(0, 4 - f.opBar);
                }
                if (remaining > 0) {
                    auto walk = [&](int s) {
                        for (int j = 1; j <= remaining; ++j) {
                            const int t = s + j * d;
                            if (t >= 24 || blocked(t)) break;
                            check(t);
                        }
                    };
                    if (entered) walk(d - 1);
                    for (int s = 0; s < 24; ++s)
                        if (f.op[s]) walk(s);
                }
            }
            total += w * best;
        }
    }
    return total / 36.0;
}

// Value of a made point (>= 2 checkers) at my rel r.
const double kPointVal[24] = {
    0.8, 1.6, 2.6, 4.2, 5.6, 5.6, // home board 1..6
    4.6, 3.2, 2.2, 1.6, 1.2, 0.8, // outer board 7..12 (bar point = 7)
    0.4, 0.4, 0.4, 0.4, 0.4, 2.6, // 13..18 (18 = opponent's bar point)
    1.8, 4.2, 3.4, 2.4, 1.4, 0.8, // opponent's home: anchors (his 6..1 point)
};
const double kPrimeVal[7] = {0, 0, 1.0, 3.0, 6.5, 11.0, 17.0};

struct SideFeat {
    double points = 0;
    double prime = 0;
    double stack = 0;
    int homeMade = 0;     // made points in my home board
};

SideFeat sideFeatures(const Frame& f, const Weights& W) {
    SideFeat s;
    int minOp = f.opBar ? -1 : 24;
    if (!f.opBar)
        for (int r = 0; r < 24; ++r)
            if (f.op[r]) { minOp = r; break; }
    int run = 0;
    for (int r = 0; r <= 24; ++r) {
        const bool made = r < 24 && f.me[r] >= 2;
        if (made) {
            s.points += kPointVal[r] * (r >= 17 ? W.anchor : W.point);
            if (r < 6) ++s.homeMade;
            if (f.me[r] > 3) s.stack += W.stack * (f.me[r] - 3) * (r < 6 ? 0.6 : 0.9);
            ++run;
        } else {
            if (run >= 2) {
                const int start = r - run;
                const bool trapping = minOp < start;
                s.prime += W.prime * kPrimeVal[std::min(run, 6)] * (trapping ? 1.0 : 0.25);
            }
            run = 0;
        }
    }
    return s;
}

double evalFrame(const Frame& f, const Weights& W) {
    if (f.meOff == kCheckers) return 1000.0 * (f.opOff == 0 ? 2 : 1);
    if (f.opOff == kCheckers) return -1000.0 * (f.meOff == 0 ? 2 : 1);
    const Frame g = flip(f);
    if (!contact(f)) {
        const double km = keith(f), ko = keith(g);
        double v = ko - km;
        v -= marsTerm(f, ko / 8.17);
        v += marsTerm(g, km / 8.17);
        return v;
    }
    const SideFeat me = sideFeatures(f, W);
    const SideFeat op = sideFeatures(g, W);
    const int myPip = pipsMe(f), opPip = pipsMe(g);
    double v = (opPip - myPip) * W.pip;
    v += me.points - op.points;
    v += me.prime - op.prime;
    v -= me.stack - op.stack;
    // checkers on the bar: worse against a strong home board
    v -= f.meBar * (W.barBase + W.barHome * op.homeMade);
    v += f.opBar * (W.barBase + W.barHome * me.homeMade);
    // my blots face the opponent's roll now; his blots only after his move (he may tidy up)
    v -= blotRisk(f, op.homeMade, W);
    v += W.oppBlot * blotRisk(g, me.homeMade, W);
    // checkers off in a contact bear-off
    v += (f.meOff - f.opOff) * W.off;
    v -= marsTerm(f, pipsMe(g) / 8.17) * W.mars;
    v += marsTerm(g, pipsMe(f) / 8.17) * W.mars;
    return v;
}

// ---------------------------------------------------------------------------------------------------------
// Gülbahar / Fevga (Variant::sameWay): both sides run the same way round, nobody is hit, one checker holds a
// point. The frame is then two OWN frames: me[r] at my rel r, op[s] at HIS rel s (he moves down his rel too);
// my rel r and his rel (r + 12) % 24 are the same point. No bar. keith / pipsMe / marsSavePips read `me` only,
// so they work on these frames as they are.
//
// The evaluation: the pip race, the blocks each side puts in the other's way (runs of held points in front of
// his checkers, a 6-run a full prime; the dice values his checkers lose to my points), stacking, checkers off and
// the mars risk. A pure race (nobody can be blocked any more) is the wastage-adjusted race as in klasik.
struct SameWeights {
    double pip = 1.0;
    double block = 1.0;  // scale of kBlockVal for my runs in front of his checkers
    double mob = 1.0;    // per die value (of 6) one of his points loses to my points, per checker there (capped)
    double stack = 0.6;  // per checker above 3 on a point
    double off = 1.5;
    double mars = 0.5;
    double start = 0.0;  // per checker still on my start point after the opening (getting them out)
};
SameWeights sameClassic() { return SameWeights(); }
// Kurt, per çeşit, from paired self-play against Usta (2-3k games a setting, Kurt's 2-ply on top): in Gülbahar the
// ladder's huge doubles make blocks count less and a flexible, unstacked army more; in Fevga the blocks are the game.
SameWeights sameTuned(Variant v) {
    SameWeights w;
    if (v == Variant::Gulbahar) {
        w.block = 0.8;
        w.mob = 0.5;
        w.stack = 1.2;
    } else {
        w.block = 2.0;
        w.mob = 1.5;
        w.stack = 1.2;
    }
    return w;
}
SameWeights sameSloppy() {
    SameWeights w;
    w.block = 0.2;
    w.mob = 0.0;
    w.stack = 0.2;
    w.mars = 0.0;
    return w;
}
const SameWeights kSameClassic = sameClassic();
const SameWeights kSameTunedGulbahar = sameTuned(Variant::Gulbahar);
const SameWeights kSameTunedFevga = sameTuned(Variant::Fevga);
const SameWeights& sameTunedFor(Variant v) { return v == Variant::Gulbahar ? kSameTunedGulbahar : kSameTunedFevga; }
const SameWeights kSameSloppy = sameSloppy();
// Value of a run of held points (length 1..6) in front of the other side's checkers.
const double kBlockVal[7] = {0, 0.6, 1.8, 4.0, 7.5, 12.5, 20.0};

inline int sw(int r) { return (r + 12) % 24; } // my rel <-> his rel of the same point

Frame makeFrameSame(const Position& pos, int p, Variant v) {
    Frame f;
    for (int r = 0; r < 24; ++r) {
        const int vi = pos.pts[absPoint(v, p, r)], vo = pos.pts[absPoint(v, 1 - p, r)];
        f.me[r] = p == 0 ? (vi > 0 ? vi : 0) : (vi < 0 ? -vi : 0);
        f.op[r] = p == 0 ? (vo < 0 ? -vo : 0) : (vo > 0 ? vo : 0);
    }
    f.meBar = f.opBar = 0;
    f.meOff = pos.off[p];
    f.opOff = pos.off[1 - p];
    return f;
}

Frame flipSame(const Frame& f) {
    Frame g = f;
    for (int r = 0; r < 24; ++r) {
        g.me[r] = f.op[r];
        g.op[r] = f.me[r];
    }
    g.meOff = f.opOff;
    g.opOff = f.meOff;
    return g;
}

// Can either side still be blocked by the other? (one of his checkers on the way ahead of one of mine, or the
// other way round)
bool contactSame(const Frame& f) {
    int maxMe = -1, maxOp = -1;
    for (int r = 23; r >= 0 && maxMe < 0; --r)
        if (f.me[r]) maxMe = r;
    for (int r = 23; r >= 0 && maxOp < 0; --r)
        if (f.op[r]) maxOp = r;
    for (int r = 0; r < maxMe; ++r)
        if (f.op[sw(r)]) return true;
    for (int r = 0; r < maxOp; ++r)
        if (f.me[sw(r)]) return true;
    return false;
}

// What my held points cost the other side (f.me against f.op).
double blockValue(const Frame& f, const SameWeights& W) {
    int behind[25];
    behind[24] = 0;
    for (int s = 23; s >= 0; --s) behind[s] = behind[s + 1] + f.op[s]; // behind[s]: his checkers at his rel >= s
    double v = 0;
    int run = 0;
    for (int s = 0; s <= 24; ++s) {
        const bool held = s < 24 && f.me[sw(s)] > 0;
        if (held) {
            ++run;
            continue;
        }
        if (run > 0) {
            const int nb = behind[s]; // his checkers behind the run (rel above its top s - 1)
            if (nb > 0) v += W.block * kBlockVal[std::min(run, 6)] * (0.35 + 0.65 * std::min(nb, 8) / 8.0);
        }
        run = 0;
    }
    if (W.mob > 0) {
        for (int s = 1; s < 24; ++s) {
            if (!f.op[s]) continue;
            int lost = 0;
            for (int d = 1; d <= 6 && s - d >= 0; ++d) lost += f.me[sw(s - d)] > 0 ? 1 : 0;
            v += W.mob * lost * std::min(f.op[s], 3) / 6.0;
        }
    }
    return v;
}

double stackCost(const Frame& f, const SameWeights& W) {
    double c = 0;
    for (int r = 0; r < 23; ++r)
        if (f.me[r] > 3) c += W.stack * (f.me[r] - 3) * (r < 6 ? 0.5 : 1.0);
    // the start point: stacked there anyway at first; what still sits there later costs
    if (W.start > 0 && f.me[23] < kCheckers) c += W.start * f.me[23];
    return c;
}

double evalSame(const Frame& f, const SameWeights& W) {
    if (f.meOff == kCheckers) return 1000.0 * (f.opOff == 0 ? 2 : 1);
    if (f.opOff == kCheckers) return -1000.0 * (f.meOff == 0 ? 2 : 1);
    const Frame g = flipSame(f);
    if (!contactSame(f)) {
        const double km = keith(f), ko = keith(g);
        double v = ko - km;
        v -= marsTerm(f, ko / 8.17);
        v += marsTerm(g, km / 8.17);
        return v;
    }
    const int myPip = pipsMe(f), opPip = pipsMe(g);
    double v = (opPip - myPip) * W.pip;
    v += blockValue(f, W) - blockValue(g, W);
    v -= stackCost(f, W) - stackCost(g, W);
    v += (f.meOff - f.opOff) * W.off;
    v -= marsTerm(f, opPip / 8.17) * W.mars;
    v += marsTerm(g, myPip / 8.17) * W.mars;
    return v;
}

// A level's evaluation of `pos` for p (p has just moved), in any çeşit. set: 0 Acemi, 1 Usta, 2 Kurt.
double evalLevel(const Position& pos, int p, Variant v, int set) {
    if (v == Variant::Klasik) return evalFrame(makeFrame(pos, p), set == 0 ? kSloppy : set == 1 ? kClassic : kTuned);
    return evalSame(makeFrameSame(pos, p, v), set == 0 ? kSameSloppy : set == 1 ? kSameClassic : sameTunedFor(v));
}
bool contactIn(const Position& pos, int p, Variant v) {
    return v == Variant::Klasik ? contact(makeFrame(pos, p)) : contactSame(makeFrameSame(pos, p, v));
}

// ---------------------------------------------------------------------------------------------------------
// Katlama zarı: the chances of the side to roll ("me" of the frame).

constexpr double kRollPips = 8.17;     // an average roll
constexpr double kContactScale = 33.0; // logistic scale of evalFrame (pips) -> win probability in contact
constexpr double kOnRoll = 4.0;        // ... and what being on roll is worth there (pips)
// Kurt's cube: doubles from this win chance unless too good (cubeless equity per cube unit above kTooGood:
// playing on for the mars is worth more than the opponent's drop); takes while the equity of taking (twice
// the stake, plus the cube's own value to its owner) beats the drop.
constexpr double kKurtDouble = 0.68, kKurtTooGood = 0.95, kKurtTake = -0.56;
// Usta's cube on the pip race estimate.
constexpr double kUstaDouble = 0.70, kUstaTooGood = 0.90, kUstaTake = 0.25;

double normalCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

// Race with me to roll: a normal approximation over the rolls each side still needs (the spread grows with the
// square root of the rolls to go); being on roll is worth half a roll. The spread and kContactScale / kOnRoll
// were fitted on Usta self-play (predicted vs. realised win rates agree within ~2% from 15% to 85%).
// Gülbahar: the ladder makes an average roll much longer and far more uneven (a 1-1 may run 84 pips): the race and
// the contact estimates are spread out accordingly (fitted so the cube's take / drop rates look like klasik's).
constexpr double kGulRollPips = 12.0, kGulSpread = 1.8, kGulContactScale = 55.0;

double raceWin(double myPips, double opPips, Variant v = Variant::Klasik) {
    const bool gul = v == Variant::Gulbahar;
    const double rp = gul ? kGulRollPips : kRollPips;
    const double lead = (opPips - myPips) / rp + 0.5;
    const double sd = (0.36 * std::sqrt(std::max(1.0, (myPips + opPips) / rp)) + 0.1) * (gul ? kGulSpread : 1.0);
    return normalCdf(lead / sd);
}

struct Chances {
    double win = 0.5;
    double marsWin = 0.0, marsLoss = 0.0; // of all games
    double equity() const { return win + marsWin - (1.0 - win) - marsLoss; } // cubeless, per cube unit
};

// Kurt: its evaluation turned into chances. In a race the wastage-adjusted counts go into raceWin; in contact
// the evaluation of the position as the opponent sees it (he just moved, I roll) through a logistic.
// Usta: the pip race alone; with contact it trusts the counts less (shrunk toward even).
double ustaWin(const Position& pos, int p, Variant v) {
    const double w = raceWin(pipCount(pos, p, v), pipCount(pos, 1 - p, v), v);
    return contactIn(pos, p, v) ? 0.5 + (w - 0.5) * 0.6 : w;
}

Chances kurtChances(const Position& pos, int p, Variant v) {
    const bool klasik = v == Variant::Klasik;
    const Frame f = klasik ? makeFrame(pos, p) : makeFrameSame(pos, p, v);
    const Frame g = klasik ? flip(f) : flipSame(f);
    Chances c;
    if (f.meOff == kCheckers || g.meOff == kCheckers) {
        c.win = f.meOff == kCheckers ? 1.0 : 0.0;
        return c;
    }
    if (!(klasik ? contact(f) : contactSame(f))) c.win = raceWin(keith(f) * 6.0 / 7.0, keith(g) * 6.0 / 7.0, v);
    else if (klasik) c.win = 1.0 / (1.0 + std::exp((evalFrame(g, kTuned) - kOnRoll) / kContactScale));
    else c.win = 1.0 / (1.0 + std::exp((evalSame(g, sameTunedFor(v)) - kOnRoll) /
                                       (v == Variant::Gulbahar ? kGulContactScale : kContactScale)));
    c.win = std::min(0.995, std::max(0.005, c.win));
    // mars: the side at risk needs its first checker off before the other finishes (marsTerm's 0..14 ramp)
    c.marsWin = c.win * marsTerm(g, pipsMe(f) / kRollPips) / 14.0;
    c.marsLoss = (1.0 - c.win) * marsTerm(f, pipsMe(g) / kRollPips) / 14.0;
    return c;
}

inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint64_t stateSig(const Position& pos, const std::vector<int>& left, int p, int turn) {
    uint64_t w[4] = {0, 0, 0, 0};
    std::memcpy(w, &pos, sizeof(Position));
    uint64_t h = mix64((uint64_t)turn * 31 + (uint64_t)p);
    for (uint64_t x : w) h = mix64(h ^ x);
    std::vector<int> l = left;
    std::sort(l.begin(), l.end());
    for (int d : l) h = mix64(h ^ (uint64_t)(d + 11));
    return h;
}

} // namespace

double botEvaluate(const Position& pos, int p, Variant v) { return evalLevel(pos, p, v, 2); }
double botWinProbability(const Position& pos, int p, Variant v) { return kurtChances(pos, p, v).win; }

double botEquityToRoll(const Position& pos, int p, double* win, Variant v) {
    if (pos.off[0] == kCheckers || pos.off[1] == kCheckers) { // a finished game: 1 a game, 2 a mars
        const bool won = pos.off[p] == kCheckers;
        if (win) *win = won ? 1.0 : 0.0;
        const int base = pos.off[won ? 1 - p : p] == 0 ? 2 : 1;
        return won ? base : -base;
    }
    const Chances c = kurtChances(pos, p, v);
    if (win) *win = c.win;
    return c.equity();
}

double botEquityAfterMove(const Position& pos, int p, int depth, double* win, Variant v) {
    const int opp = 1 - p;
    if (pos.off[p] == kCheckers || pos.off[opp] == kCheckers || depth <= 1) {
        double w = 0.0;
        const double e = -botEquityToRoll(pos, opp, &w, v);
        if (win) *win = 1.0 - w;
        return e;
    }
    std::vector<Position> replies;
    double sum = 0.0, wsum = 0.0;
    for (int a = 1; a <= 6; ++a) {
        for (int b = a; b <= 6; ++b) {
            const double weight = a == b ? 1.0 : 2.0;
            generateResults(pos, opp, a, b, replies, v);
            const Position* best = &pos;
            double his = -1e18;
            for (const Position& y : replies) {
                const double e = evalLevel(y, opp, v, 2);
                if (e > his) {
                    his = e;
                    best = &y;
                }
            }
            double w = 0.0;
            sum += weight * botEquityToRoll(*best, p, &w, v);
            wsum += weight * w;
        }
    }
    if (win) *win = wsum / 36.0;
    return sum / 36.0;
}


ActionResult applyBotAction(Game& g, int p, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::OpeningRoll: return g.rollOpening();
    case BotAction::Kind::Roll: return g.roll(p);
    case BotAction::Kind::Step: return a.die ? g.applyStep(p, a.from, a.to, a.die) : g.applyStep(p, a.from, a.to);
    case BotAction::Kind::EndTurn: return g.endTurn(p);
    case BotAction::Kind::Double: return g.offerDouble(p);
    case BotAction::Kind::Take: return g.acceptDouble(p);
    case BotAction::Kind::Drop: return g.declineDouble(p);
    case BotAction::Kind::None: break;
    }
    return ActionResult::fail("Yapılacak bir şey yok");
}

BotAction fallbackAction(const Game& g, int p) {
    BotAction a;
    if (g.stage() == Stage::OpeningRoll) {
        a.kind = BotAction::Kind::OpeningRoll;
        return a;
    }
    if (g.responder() == p) {
        a.kind = BotAction::Kind::Take;
        return a;
    }
    if (g.current() != p) return a;
    if (g.stage() == Stage::NeedRoll) {
        a.kind = BotAction::Kind::Roll;
        return a;
    }
    if (g.stage() == Stage::Moving) {
        const std::vector<Step> steps = g.legalSteps();
        if (steps.empty()) {
            a.kind = BotAction::Kind::EndTurn;
        } else {
            a.kind = BotAction::Kind::Step;
            a.from = steps[0].from;
            a.to = steps[0].to;
            a.die = steps[0].die;
        }
    }
    return a;
}

// ---------------------------------------------------------------------------------------------------------

struct Bot::Impl {
    BotLevel level = BotLevel::Normal;
    uint64_t seed = 1;

    struct Planned {
        Step step;
        uint64_t sig = 0;
    };
    std::vector<Planned> plan;
    size_t planPos = 0;

    okey::Rng turnRng(const Game& g, int p, uint64_t salt) const {
        uint64_t h = mix64(seed ^ 0xA5A5A5A5ull);
        h = mix64(h ^ (uint64_t)g.gameIndex());
        h = mix64(h ^ (uint64_t)g.turnNumber());
        h = mix64(h ^ (uint64_t)p);
        h = mix64(h ^ salt);
        return okey::Rng(h);
    }

    // Index of the chosen play.
    size_t choose(const Game& g, int p, const std::vector<Play>& plays) {
        if (plays.size() == 1) return 0;
        const Variant var = g.variant();
        if (level == BotLevel::Easy) {
            okey::Rng rng = turnRng(g, p, plays.size());
            // sloppy evaluation with noise; now and then just one of its few best-looking plays
            std::vector<std::pair<double, size_t>> sc(plays.size());
            for (size_t i = 0; i < plays.size(); ++i)
                sc[i] = {evalLevel(plays[i].result, p, var, 0) + rng.uniform(-kEasyNoise, kEasyNoise), i};
            std::stable_sort(sc.begin(), sc.end(), [](const std::pair<double, size_t>& x, const std::pair<double, size_t>& y) {
                return x.first > y.first;
            });
            if (rng.chance(kEasyRandom)) return sc[(size_t)rng.range((int)std::min<size_t>(4, sc.size()))].second;
            return sc[0].second;
        }
        const int set = level == BotLevel::Normal ? 1 : 2;
        std::vector<double> one(plays.size());
        for (size_t i = 0; i < plays.size(); ++i) one[i] = evalLevel(plays[i].result, p, var, set);
        const size_t best1 = (size_t)(std::max_element(one.begin(), one.end()) - one.begin());
        if (level == BotLevel::Normal) return best1;

        // Kurt: expectimax over the opponent's 21 rolls for the best 1-ply candidates; for every roll the
        // opponent plays his best reply by the same evaluation.
        const int opp = 1 - p;
        std::vector<size_t> order(plays.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return one[a] > one[b]; });
        const bool isContact = contactIn(plays[best1].result, p, var);
        const size_t K = std::min(order.size(), isContact ? kKurtCandidates : kKurtRaceCandidates);
        std::vector<Position> replies;
        long evals = 0;
        size_t best = order[0];
        double bv = -1e18;
        for (size_t c = 0; c < K; ++c) {
            const size_t i = order[c];
            if (c > 0 && (one[i] < one[order[0]] - kKurtMargin || evals > kKurtEvalBudget)) break;
            const Position& x = plays[i].result;
            double v;
            if (x.off[p] == kCheckers) {
                v = one[i];
            } else {
                double sum = 0;
                for (int a = 1; a <= 6; ++a) {
                    for (int b = a; b <= 6; ++b) {
                        generateResults(x, opp, a, b, replies, var);
                        double his;
                        if (replies.empty()) {
                            his = evalLevel(x, opp, var, 2);
                            ++evals;
                        } else {
                            his = -1e18;
                            for (const Position& y : replies) his = std::max(his, evalLevel(y, opp, var, 2));
                            evals += (long)replies.size();
                        }
                        sum -= (a == b ? 1 : 2) * his;
                    }
                }
                v = sum / 36.0;
            }
            if (v > bv) { bv = v; best = i; }
        }
        return best;
    }

    void makePlan(const Game& g, int p) {
        plan.clear();
        planPos = 0;
        std::vector<Play> plays = g.allTurnPlays();
        if (plays.empty()) return;
        const Play& pl = plays[choose(g, p, plays)];
        Position pos = g.position();
        std::vector<int> left = g.dice().left();
        for (const Step& s : pl.steps) {
            plan.push_back(Planned{s, stateSig(pos, left, p, g.turnNumber())});
            applyStepTo(pos, p, s, g.variant());
            auto it = std::find(left.begin(), left.end(), s.die);
            if (it != left.end()) left.erase(it);
        }
    }

    // ---- katlama zarı ----
    // Should p (to roll, g.canDouble(p)) offer a double?
    bool wantsDouble(const Game& g, int p) {
        const int opp = 1 - p, mp = g.matchPoints(), cube = g.cubeValue();
        if (level == BotLevel::Easy) { // a feeling for the pip lead, now and then
            okey::Rng rng = turnRng(g, p, 0xC0BEull);
            const int lead = g.pipCount(opp) - g.pipCount(p);
            return lead > 16 + rng.range(0, 30) && rng.chance(0.5f);
        }
        if (g.score(p) + cube >= mp) return false; // the cube already wins the match: a dead double
        if (level == BotLevel::Normal) {
            const double w = ustaWin(g.position(), p, g.variant());
            return w >= kUstaDouble && w <= kUstaTooGood;
        }
        // Kurt: after the Crawford game the trailer doubles at once (the leader would always drop late)
        if (g.score(opp) == mp - 1 && g.score(p) < mp - 1) return true;
        const Chances c = kurtChances(g.position(), p, g.variant());
        return c.win >= kKurtDouble && c.equity() <= kKurtTooGood;
    }
    // p was offered a double: take it?
    bool wantsTake(const Game& g, int p) {
        const int opp = 1 - p, mp = g.matchPoints(), cube = g.cubeValue();
        if (level == BotLevel::Easy) { // takes almost anything, drops some hopeless ones
            okey::Rng rng = turnRng(g, p, 0x7A4Eull);
            const int behind = g.pipCount(p) - g.pipCount(opp);
            return behind < 40 + rng.range(0, 40) || rng.chance(0.3f);
        }
        if (g.score(opp) + cube >= mp) return true; // dropping loses the match: a free take
        // (the opponent is to roll in these estimates: he offered before his roll)
        if (level == BotLevel::Normal) return 1.0 - ustaWin(g.position(), opp, g.variant()) >= kUstaTake;
        return -kurtChances(g.position(), opp, g.variant()).equity() >= kKurtTake;
    }

    BotAction next(const Game& g, int p) {
        BotAction a;
        if (g.stage() == Stage::OpeningRoll) {
            a.kind = BotAction::Kind::OpeningRoll;
            return a;
        }
        if (g.responder() == p) {
            a.kind = wantsTake(g, p) ? BotAction::Kind::Take : BotAction::Kind::Drop;
            return a;
        }
        if (g.current() != p) return a;
        if (g.stage() == Stage::NeedRoll) {
            plan.clear();
            planPos = 0;
            a.kind = g.canDouble(p) && wantsDouble(g, p) ? BotAction::Kind::Double : BotAction::Kind::Roll;
            return a;
        }
        if (g.stage() != Stage::Moving) return a;
        const uint64_t sig = stateSig(g.position(), g.dice().left(), p, g.turnNumber());
        if (planPos >= plan.size() || plan[planPos].sig != sig) {
            if (g.legalSteps().empty()) {
                a.kind = BotAction::Kind::EndTurn;
                return a;
            }
            makePlan(g, p);
            if (plan.empty() || plan[0].sig != sig) return fallbackAction(g, p);
        }
        const Step s = plan[planPos++].step;
        a.kind = BotAction::Kind::Step;
        a.from = s.from;
        a.to = s.to;
        a.die = s.die;
        return a;
    }
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(new Impl) {
    impl_->level = level;
    impl_->seed = seed;
}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::resetForGame() {
    impl_->plan.clear();
    impl_->planPos = 0;
}
void Bot::observe(const GameEvent& e, const Game&) {
    if (e.type == EvType::GameStart || e.type == EvType::Undo) resetForGame();
}
BotAction Bot::next(const Game& g, int p) { return impl_->next(g, p); }

} // namespace tavla
