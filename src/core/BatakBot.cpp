// Batak computer opponents. See BatakBot.h for the levels and the fairness contract.
#include "core/BatakBot.h"

#include <algorithm>
#include <cmath>

namespace batak {

namespace {

constexpr uint64_t ALL_CARDS = (1ull << kart::NUM_CARDS) - 1;

inline int sOf(int c) { return c / 13; }
inline int rOf(int c) { return c % 13 + 2; }

void addToView(TrickView& v, int seat, int card, int trump) {
    if (v.ledSuit < 0) {
        v.ledSuit = sOf(card);
        v.bestLed = card;
        v.winner = seat;
        v.winningCard = card;
        return;
    }
    if (beatsTrick(card, v, trump)) {
        v.winner = seat;
        v.winningCard = card;
    }
    if (sOf(card) == v.ledSuit) {
        if (card > v.bestLed) v.bestLed = card;
    } else if (sOf(card) == trump && card > v.bestTrump) {
        v.bestTrump = card;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Public knowledge of one seat (built from Game every call: the bot needs no event memory).

struct Know {
    int seat = -1;   // acting seat
    int me = -1;     // controller (== seat unless playing the open dummy)
    int trump = -1;
    bool broken = false;
    bool esli = false;
    int declarer = -1, contract = 0, dummy = -1;
    uint64_t played = 0;
    uint64_t visible[4] = {0, 0, 0, 0}; // known hands (own, open dummy)
    bool known[4] = {false, false, false, false};
    uint64_t forbid[4] = {0, 0, 0, 0};   // cards the seat cannot hold (voids, could not beat / over-trump)
    uint64_t playedBy[4] = {0, 0, 0, 0};
    int count[4] = {0, 0, 0, 0};
    int maxBid[4] = {0, 0, 0, 0};        // highest bid of the seat this hand (0 = none)
    int passNeed[4] = {0, 0, 0, 0};      // the bid it would have needed when it passed without bidding
    uint8_t voids[4] = {0, 0, 0, 0};     // suit bits
};

Know buildKnow(const Game& g, int seat) {
    const Rules& r = g.rules();
    Know k;
    k.seat = seat;
    k.me = g.controllerOf(seat);
    k.trump = g.trump();
    k.broken = g.trumpBroken();
    k.esli = r.esli;
    k.declarer = g.declarer();
    k.contract = g.contract();
    k.dummy = g.exposedSeat();
    k.played = g.playedMask();
    k.known[k.me] = true;
    k.visible[k.me] = g.handMask(k.me);
    if (k.dummy >= 0) {
        k.known[k.dummy] = true;
        k.visible[k.dummy] = g.handMask(k.dummy);
    }
    for (int s = 0; s < 4; ++s) k.count[s] = (int)g.hand(s).size();

    int high = 0;
    for (const BidRecord& b : g.bids()) {
        if (b.value > 0) {
            k.maxBid[b.seat] = std::max(k.maxBid[b.seat], b.value);
            high = b.value;
        } else if (k.maxBid[b.seat] == 0) {
            k.passNeed[b.seat] = std::max(r.minBid, high + 1);
        }
    }

    const int trump = k.trump;
    bool broken = false;
    auto process = [&](const std::vector<PlayedCard>& cards) {
        TrickView v;
        for (size_t i = 0; i < cards.size(); ++i) {
            const int s = cards[i].seat, c = cards[i].card, su = sOf(c);
            k.playedBy[s] |= bit(c);
            if (i == 0) {
                if (trump >= 0 && su == trump && r.trumpMustBeBroken && !broken) k.forbid[s] |= ALL_CARDS & ~suitMask(trump);
            } else if (su != v.ledSuit) {
                k.forbid[s] |= suitMask(v.ledSuit);
                if (trump >= 0 && r.mustTrump && su != trump) k.forbid[s] |= suitMask(trump);
                else if (su == trump && r.mustOvertrump && v.bestTrump >= 0 && c < v.bestTrump)
                    k.forbid[s] |= aboveMask(v.bestTrump);
            } else if (r.mustBeat && (v.bestTrump < 0 || r.mustBeatEvenIfTrumped) && c < v.bestLed) {
                k.forbid[s] |= aboveMask(v.bestLed);
            }
            if (su == trump) broken = true;
            addToView(v, s, c, trump);
        }
    };
    for (const Trick& t : g.tricks()) process(t.cards);
    process(g.trick());
    for (int s = 0; s < 4; ++s)
        for (int q = 0; q < 4; ++q)
            if ((k.forbid[s] & suitMask(q)) == suitMask(q)) k.voids[s] |= (uint8_t)(1u << q);
    return k;
}

// ---------------------------------------------------------------------------------------------------------
// Hand evaluation (Usta): expected tricks of a 13-card hand as declarer with `trump`, tekli.

double evalTricks(uint64_t h, int trump) {
    double est = 0;
    const uint64_t tm = h & suitMask(trump);
    const int tl = popcount(tm);
    auto has = [&](int suit, int rank) { return (h & bit(kart::makeCard(suit, rank))) != 0; };
    // koz: honours + length
    double honours = 0;
    if (has(trump, 14)) honours += 1.0;
    if (has(trump, 13)) honours += tl >= 2 ? 0.9 : 0.35;
    if (has(trump, 12)) honours += tl >= 3 ? 0.7 : 0.2;
    if (has(trump, 11)) honours += tl >= 4 ? 0.45 : 0.1;
    if (has(trump, 10)) honours += tl >= 5 ? 0.3 : 0.0;
    const double length = std::max(0, tl - 4) * 0.85 + (tl >= 4 ? 0.3 : 0.0);
    est += std::min((double)tl, honours + length);
    // side suits
    double ruff = 0;
    for (int s = 0; s < 4; ++s) {
        if (s == trump) continue;
        const int len = popcount(h & suitMask(s));
        const bool a = has(s, 14), kk = has(s, 13), q = has(s, 12);
        if (a) est += len <= 5 ? 0.95 : 0.8;
        if (kk) est += len >= 2 ? (a ? 0.8 : 0.45) - (len >= 6 ? 0.2 : 0.0) : 0.1;
        if (q && len >= 3) est += (a && kk) ? 0.45 : ((a || kk) ? 0.25 : 0.1);
        if (len == 0) ruff += 1.0;
        else if (len == 1) ruff += 0.6;
        else if (len == 2) ruff += 0.25;
    }
    if (tl >= 2) est += std::min(ruff, std::max(0, tl - 3) * 0.7 + 0.3);
    return est;
}

// Expected tricks of the partner's unseen hand for the declaring team (eşli), by the partner's bids.
double partnerShare(const Know& k, int seat, double base) {
    if (!k.esli) return 0.0;
    const int p = kart::partnerOf(seat);
    if (k.maxBid[p] > 0) return base + 0.5 * std::max(0.0, (k.maxBid[p] - base) - 5.0);
    if (k.passNeed[p] > 0) return base - 0.4;
    return base;
}

// ---------------------------------------------------------------------------------------------------------
// Card play policy (Usta; also the rollout policy of Kurt). Uses only the seat's own hand, the played cards,
// the trick and the void bits.

struct PolicyIn {
    int seat = 0;
    uint64_t hand = 0;
    uint64_t legal = 0;
    TrickView v;
    int n = 0; // cards already on the trick
    int trump = -1;
    uint64_t played = 0; // all played cards incl. the trick
    bool esli = false;
    int declarer = -1;
    const uint8_t* voids = nullptr; // per seat suit bits
    int variant = 0;                // experiment bits (tests/tools)
};

int policyCard(const PolicyIn& p) {
    const uint64_t L = p.legal;
    if (popcount(L) <= 1) return lowestCard(L);
    const uint64_t others = ALL_CARDS & ~p.played & ~p.hand;
    const int partner = p.esli ? kart::partnerOf(p.seat) : -1;
    const bool declSide = p.declarer >= 0 && (p.seat == p.declarer || partner == p.declarer);
    const uint64_t trumpM = p.trump >= 0 ? suitMask(p.trump) : 0;
    const bool othersTrumps = (others & trumpM) != 0;
    auto isMaster = [&](int c) { return (aboveMask(c) & others) == 0; };
    auto isOpp = [&](int s) { return s != p.seat && s != partner; };
    auto voidIn = [&](int s, int suit) { return (p.voids[s] >> suit) & 1; };
    // an opponent among the next `cnt` seats may ruff a card of `suit`
    auto ruffRisk = [&](int suit, int cnt) {
        if (p.trump < 0 || suit == p.trump || !othersTrumps) return false;
        for (int k = 1; k <= cnt; ++k) {
            const int s = (p.seat + k) % 4;
            if (isOpp(s) && voidIn(s, suit) && !voidIn(s, p.trump)) return true;
        }
        return false;
    };
    // the least valuable card of `m`: low ranks, no koz, keep master cards, prefer emptying a short suit
    auto cheapest = [&](uint64_t m) {
        int best = -1;
        double bestCost = 1e9;
        for (; m; m &= m - 1) {
            const int c = __builtin_ctzll(m), su = sOf(c);
            double cost = rOf(c);
            if (su == p.trump) cost += 20;
            if (isMaster(c) && (others & suitMask(su))) cost += 12;
            const int len = popcount(p.hand & suitMask(su));
            if (su != p.trump && len == 1 && (p.hand & trumpM)) cost -= 3;
            if (cost < bestCost) {
                bestCost = cost;
                best = c;
            }
        }
        return best;
    };

    if (p.n == 0) { // ---- lead
        double bestScore = -1e9;
        int best = lowestCard(L);
        for (int su = 0; su < 4; ++su) {
            const uint64_t cs = L & suitMask(su);
            if (!cs) continue;
            const int top = highestCard(cs), low = lowestCard(cs);
            const int othersS = popcount(others & suitMask(su));
            const int len = popcount(p.hand & suitMask(su));
            double score;
            int card;
            if (su == p.trump) {
                if (isMaster(top) && othersS > 0) {
                    score = declSide ? 12.0 : 8.0;
                    card = top;
                } else {
                    score = 1.0;
                    card = low;
                }
            } else {
                const bool risk = ruffRisk(su, 3);
                if (othersS == 0) {
                    score = othersTrumps ? 0.5 : 10.0;
                    card = top;
                } else if (isMaster(top)) {
                    score = 10.0 + std::min(othersS, 6) * 0.3 - (risk ? 9.0 : 0.0);
                    card = top;
                } else if (partner >= 0 && voidIn(partner, su) && !voidIn(partner, p.trump) && othersTrumps && !risk) {
                    score = 9.0;
                    card = low;
                } else {
                    // low lead: prefer suits the others still hold plenty of (no cheap ruff)
                    score = 3.0 - (risk ? 2.0 : 0.0) + 0.25 * std::min(othersS, 8);
                    if ((p.hand & trumpM) && len <= 2) score += (3 - len);
                    // keep a guarded king/queen: lead from the suit without an honour to protect
                    if (len >= 2 && rOf(top) >= 12) score -= 1.0;
                    card = low;
                }
            }
            if (score > bestScore) {
                bestScore = score;
                best = card;
            }
        }
        return best;
    }

    // ---- follow
    uint64_t W = 0;
    for (uint64_t m = L; m; m &= m - 1) {
        const int c = __builtin_ctzll(m);
        if (beatsTrick(c, p.v, p.trump)) W |= bit(c);
    }
    const int after = 3 - p.n; // players still to play after me
    if (partner >= 0 && p.v.winner == partner) {
        const int wc = p.v.winningCard;
        bool safe = after == 0;
        if (!safe) safe = isMaster(wc) && (sOf(wc) == p.trump || !ruffRisk(sOf(wc), after));
        if (safe) {
            const uint64_t nonWin = L & ~W;
            return cheapest(nonWin ? nonWin : L);
        }
    }
    if (!W) return cheapest(L);
    if (after == 0) return lowestCard(W);
    uint64_t masters = 0;
    for (uint64_t m = W; m; m &= m - 1) {
        const int c = __builtin_ctzll(m);
        if (isMaster(c) && (sOf(c) == p.trump || !ruffRisk(sOf(c), after))) masters |= bit(c);
    }
    if (masters) return lowestCard(masters);
    return lowestCard(W); // second and third hand: the cheapest winner
}

// ---------------------------------------------------------------------------------------------------------
// Fast world for Monte Carlo rollouts.

struct World {
    uint64_t hand[4] = {0, 0, 0, 0};
    uint64_t played = 0;
    int trump = -1;
    bool broken = false;
    int n = 0, leader = 0;
    TrickView v;
    int tricks[4] = {0, 0, 0, 0};
    uint8_t voids[4] = {0, 0, 0, 0};
    int declarer = -1;
    bool esli = false;
};

inline int toAct(const World& w) { return (w.leader + w.n) % 4; }

void worldPlay(World& w, int seat, int card, const Rules& r) {
    const int su = sOf(card);
    if (w.n > 0 && su != w.v.ledSuit) {
        w.voids[seat] |= (uint8_t)(1u << w.v.ledSuit);
        if (w.trump >= 0 && su != w.trump && r.mustTrump) w.voids[seat] |= (uint8_t)(1u << w.trump);
    }
    w.hand[seat] &= ~bit(card);
    w.played |= bit(card);
    if (su == w.trump) w.broken = true;
    addToView(w.v, seat, card, w.trump);
    if (++w.n == 4) {
        ++w.tricks[w.v.winner];
        w.leader = w.v.winner;
        w.n = 0;
        w.v = TrickView();
    }
}

int worldPolicy(const World& w, int seat, const Rules& r) {
    PolicyIn p;
    p.seat = seat;
    p.hand = w.hand[seat];
    p.legal = legalMask(p.hand, w.v, w.trump, w.broken, r);
    p.v = w.v;
    p.n = w.n;
    p.trump = w.trump;
    p.played = w.played;
    p.esli = w.esli;
    p.declarer = w.declarer;
    p.voids = w.voids;
    return policyCard(p);
}

void rollout(World& w, const Rules& r) {
    while (w.hand[0] | w.hand[1] | w.hand[2] | w.hand[3]) {
        const int s = toAct(w);
        const int c = worldPolicy(w, s, r);
        if (c < 0) break; // inconsistent world (never expected)
        worldPlay(w, s, c, r);
    }
}

// Score-difference utility of a finished hand for `mySide`.
double utility(const Rules& r, int mySide, const int tricks[4], int declarer, int contract) {
    const int nSides = r.esli ? 2 : 4;
    const int declSide = r.esli ? declarer % 2 : declarer;
    int pts[4] = {0, 0, 0, 0};
    for (int side = 0; side < nSides; ++side) {
        const int t = r.esli ? tricks[side] + tricks[side + 2] : tricks[side];
        pts[side] = sidePoints(r, side == declSide, t, contract);
    }
    if (r.esli) return pts[mySide] - pts[1 - mySide];
    double others = 0;
    for (int side = 0; side < 4; ++side)
        if (side != mySide) others += pts[side];
    return pts[mySide] - others / 3.0;
}

// ---------------------------------------------------------------------------------------------------------
// Sampling the unseen cards.

// Deals `unknown` to the hidden seats (count[s] each) respecting forbid[]. False if it got stuck.
bool dealHidden(Rng& rng, const Know& k, uint64_t unknown, uint64_t out[4]) {
    int cards[52];
    int nc = 0;
    for (uint64_t m = unknown; m; m &= m - 1) cards[nc++] = __builtin_ctzll(m);
    for (int i = nc - 1; i > 0; --i) std::swap(cards[i], cards[rng.range(i + 1)]);
    int cap[4];
    uint8_t allowed[52];
    int nAllowed[52];
    for (int s = 0; s < 4; ++s) cap[s] = k.known[s] ? 0 : k.count[s];
    for (int i = 0; i < nc; ++i) {
        allowed[i] = 0;
        for (int s = 0; s < 4; ++s)
            if (!k.known[s] && cap[s] > 0 && !(k.forbid[s] & bit(cards[i]))) allowed[i] |= (uint8_t)(1u << s);
        nAllowed[i] = popcount(allowed[i]);
    }
    // most constrained first (stable: the shuffle breaks ties randomly)
    int order[52];
    for (int i = 0; i < nc; ++i) order[i] = i;
    std::stable_sort(order, order + nc, [&](int a, int b) { return nAllowed[a] < nAllowed[b]; });
    for (int s = 0; s < 4; ++s) out[s] = 0;
    for (int oi = 0; oi < nc; ++oi) {
        const int i = order[oi];
        int total = 0;
        for (int s = 0; s < 4; ++s)
            if ((allowed[i] >> s & 1) && cap[s] > 0) total += cap[s];
        if (total == 0) return false;
        int pick = rng.range(total);
        for (int s = 0; s < 4; ++s) {
            if (!((allowed[i] >> s & 1) && cap[s] > 0)) continue;
            if (pick < cap[s]) {
                out[s] |= bit(cards[i]);
                --cap[s];
                break;
            }
            pick -= cap[s];
        }
    }
    return true;
}

double bestEval(uint64_t h, int* trumpOut = nullptr) {
    double best = -1;
    int bt = 0;
    for (int t = 0; t < 4; ++t) {
        const double e = evalTricks(h, t);
        if (e > best) {
            best = e;
            bt = t;
        }
    }
    if (trumpOut) *trumpOut = bt;
    return best;
}

// How badly a sampled deal contradicts the bids (0 = plausible).
double bidViolation(const Know& k, const uint64_t hands[4], const Rules& r) {
    double viol = 0;
    const double team = k.esli ? 3.0 : 0.0;
    for (int s = 0; s < 4; ++s) {
        if (k.known[s]) continue;
        if (k.maxBid[s] == 0 && k.passNeed[s] == 0) continue;
        const uint64_t orig = hands[s] | k.playedBy[s];
        if (popcount(orig) != 13) continue;
        double e;
        if (s == k.declarer && k.trump >= 0) e = std::max(evalTricks(orig, k.trump), bestEval(orig) - 0.5);
        else e = bestEval(orig);
        if (k.maxBid[s] > 0) {
            const int bidV = (s == k.declarer && k.contract > 0) ? std::max(k.maxBid[s], 0) : k.maxBid[s];
            const double lb = bidV - team - 1.6;
            if (e < lb) viol += lb - e;
        } else if (k.passNeed[s] > 0) {
            const double ub = k.passNeed[s] - team + 1.2;
            if (e > ub) viol += 0.5 * (e - ub);
        }
    }
    (void)r;
    return viol;
}

// One plausible world for the hidden seats: hard constraints from voids, soft ones from the bids.
void sampleWorld(Rng& rng, const Know& k, const Rules& r, bool useBids, uint64_t out[4]) {
    uint64_t unknown = ALL_CARDS & ~k.played;
    for (int s = 0; s < 4; ++s)
        if (k.known[s]) unknown &= ~k.visible[s];
    uint64_t best[4] = {0, 0, 0, 0};
    double bestV = 1e9;
    bool any = false;
    const int tries = useBids ? 8 : 1;
    for (int t = 0; t < tries; ++t) {
        uint64_t h[4];
        bool ok = false;
        for (int a = 0; a < 6 && !ok; ++a) ok = dealHidden(rng, k, unknown, h);
        if (!ok) { // contradictory inference (should not happen): deal ignoring the constraints
            Know k2 = k;
            for (auto& f : k2.forbid) f = 0;
            dealHidden(rng, k2, unknown, h);
        }
        for (int s = 0; s < 4; ++s)
            if (k.known[s]) h[s] = k.visible[s];
        const double v = useBids ? bidViolation(k, h, r) : 0.0;
        if (!any || v < bestV) {
            any = true;
            bestV = v;
            for (int s = 0; s < 4; ++s) best[s] = h[s];
        }
        if (v <= 0) break;
    }
    for (int s = 0; s < 4; ++s) out[s] = best[s];
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// public helpers

const char* levelNameTR(Level l) {
    switch (l) {
    case Level::Acemi: return "Acemi";
    case Level::Usta: return "Usta";
    default: return "Kurt";
    }
}

double estimateTricks(uint64_t hand, int trump) { return evalTricks(hand, trump); }

int bestTrumpFor(uint64_t hand) {
    int t = 0;
    bestEval(hand, &t);
    return t;
}

ActionResult applyAction(Game& g, int seat, const Action& a) {
    switch (a.kind) {
    case Action::Kind::Bid: return g.bid(seat, a.value);
    case Action::Kind::Pass: return g.pass(seat);
    case Action::Kind::ChooseTrump: return g.chooseTrump(seat, a.suit);
    case Action::Kind::Play: return g.playCard(seat, a.card);
    }
    return ActionResult::fail("?");
}

Action fallbackAction(const Game& g, int seat) {
    Action a;
    switch (g.stage()) {
    case Stage::ChoosingTrump: {
        a.kind = Action::Kind::ChooseTrump;
        const uint64_t h = g.handMask(seat);
        int best = 0;
        for (int s = 1; s < 4; ++s)
            if (popcount(h & suitMask(s)) > popcount(h & suitMask(best))) best = s;
        a.suit = best;
        break;
    }
    case Stage::Playing: {
        a.kind = Action::Kind::Play;
        const std::vector<int> l = g.legalCards(seat);
        a.card = l.empty() ? -1 : l.front();
        break;
    }
    default: a.kind = Action::Kind::Pass; break;
    }
    return a;
}

// ---------------------------------------------------------------------------------------------------------
// Bot

struct Bot::Impl {
    Level level;
    Rng rng;
    // tuning knobs (see Bot::debugTune)
    double tune[9] = {-0.6, 0.4, 1200, 64, 96, 2.2, 0.4, 0.6, 0};
    Impl(Level l, uint64_t seed) : level(l), rng(seed) {}

    Action next(const Game& g, int seat);

    // bidding
    Action bidAcemi(const Game& g, int seat, const Know& k);
    Action bidUsta(const Game& g, int seat, const Know& k);
    Action bidKurt(const Game& g, int seat, const Know& k);
    int trumpAcemi(const Game& g, int seat);
    int trumpKurt(const Game& g, int seat, const Know& k);
    // play
    int playAcemi(const Game& g, int seat);
    int playUsta(const Game& g, int seat, const Know& k);
    int playKurt(const Game& g, int seat, const Know& k);

    World worldFromGame(const Game& g, const Know& k, const uint64_t hands[4]) const;
    // Kurt: mean utility for `seat` of declaring with each koz (and of passing) over sampled deals.
    void simulateBidding(const Game& g, int seat, const Know& k, int contract, int samples, double declU[4],
                         double* passU);
};

static Action makeBid(int v) {
    Action a;
    a.kind = Action::Kind::Bid;
    a.value = v;
    return a;
}
static Action makePass() { return Action(); }

Action Bot::Impl::next(const Game& g, int seat) {
    const Know k = buildKnow(g, seat);
    switch (g.stage()) {
    case Stage::Bidding: {
        if (g.legalBids(seat).empty()) return makePass();
        switch (level) {
        case Level::Acemi: return bidAcemi(g, seat, k);
        case Level::Usta: return bidUsta(g, seat, k);
        default: return bidKurt(g, seat, k);
        }
    }
    case Stage::ChoosingTrump: {
        Action a;
        a.kind = Action::Kind::ChooseTrump;
        switch (level) {
        case Level::Acemi: a.suit = trumpAcemi(g, seat); break;
        case Level::Usta: a.suit = bestTrumpFor(g.handMask(seat)); break;
        default: a.suit = trumpKurt(g, seat, k); break;
        }
        return a;
    }
    case Stage::Playing: {
        Action a;
        a.kind = Action::Kind::Play;
        switch (level) {
        case Level::Acemi: a.card = playAcemi(g, seat); break;
        case Level::Usta: a.card = playUsta(g, seat, k); break;
        default: a.card = playKurt(g, seat, k); break;
        }
        if (a.card < 0) return fallbackAction(g, seat);
        return a;
    }
    default: return fallbackAction(g, seat);
    }
}

// ---- Acemi ----

Action Bot::Impl::bidAcemi(const Game& g, int seat, const Know& k) {
    const uint64_t h = g.handMask(seat);
    double est = 0;
    int longest = 0;
    for (int s = 0; s < 4; ++s) {
        if (h & bit(kart::makeCard(s, 14))) est += 1.0;
        if (h & bit(kart::makeCard(s, 13))) est += 0.6;
        if (h & bit(kart::makeCard(s, 12))) est += 0.3;
        longest = std::max(longest, popcount(h & suitMask(s)));
    }
    est += std::max(0, longest - 3) * 0.8 + 1.0;
    est += rng.uniform(-1.0f, 1.0f);
    if (k.esli) est += 3.5;
    const int need = std::max(g.rules().minBid, g.highBid() + 1);
    if (k.esli && g.highBidder() == kart::partnerOf(seat)) return makePass();
    if (est >= need) return makeBid(need);
    return makePass();
}

int Bot::Impl::trumpAcemi(const Game& g, int seat) {
    const uint64_t h = g.handMask(seat);
    int best = 0, bestScore = -1;
    for (int s = 0; s < 4; ++s) {
        const uint64_t m = h & suitMask(s);
        const int score = popcount(m) * 20 + (m ? rOf(highestCard(m)) : 0);
        if (score > bestScore) {
            bestScore = score;
            best = s;
        }
    }
    return best;
}

int Bot::Impl::playAcemi(const Game& g, int seat) {
    const uint64_t L = legalMask(g.handMask(seat), viewTrick(g.trick(), g.trump()), g.trump(), g.trumpBroken(), g.rules());
    if (!L) return -1;
    const TrickView v = viewTrick(g.trick(), g.trump());
    if (v.ledSuit < 0) {
        // the highest card of the hand (by rank)
        int best = -1;
        for (uint64_t m = L; m; m &= m - 1) {
            const int c = __builtin_ctzll(m);
            if (best < 0 || rOf(c) > rOf(best)) best = c;
        }
        return best;
    }
    uint64_t W = 0;
    for (uint64_t m = L; m; m &= m - 1) {
        const int c = __builtin_ctzll(m);
        if (beatsTrick(c, v, g.trump())) W |= bit(c);
    }
    if (W) return highestCard(W);
    int best = -1;
    for (uint64_t m = L; m; m &= m - 1) {
        const int c = __builtin_ctzll(m);
        if (best < 0 || rOf(c) < rOf(best)) best = c;
    }
    return best;
}

// ---- Usta ----

Action Bot::Impl::bidUsta(const Game& g, int seat, const Know& k) {
    const uint64_t h = g.handMask(seat);
    const double est = bestEval(h) + partnerShare(k, seat, tune[5]);
    const int need = std::max(g.rules().minBid, g.highBid() + 1);
    if (k.esli && g.highBidder() == kart::partnerOf(seat) && est < need + 1.5) return makePass();
    const double margin = k.esli ? tune[6] : tune[0];
    if (est - margin >= need) return makeBid(need);
    return makePass();
}

int Bot::Impl::playUsta(const Game& g, int seat, const Know& k) {
    PolicyIn p;
    p.seat = seat;
    p.hand = g.handMask(seat);
    p.v = viewTrick(g.trick(), g.trump());
    p.legal = legalMask(p.hand, p.v, g.trump(), g.trumpBroken(), g.rules());
    p.n = (int)g.trick().size();
    p.trump = g.trump();
    p.played = g.playedMask();
    p.esli = k.esli;
    p.declarer = g.declarer();
    p.voids = k.voids;
    p.variant = (int)tune[8];
    return policyCard(p);
}

// ---- Kurt ----

World Bot::Impl::worldFromGame(const Game& g, const Know& k, const uint64_t hands[4]) const {
    World w;
    for (int s = 0; s < 4; ++s) {
        w.hand[s] = hands[s];
        w.tricks[s] = g.tricksWon(s);
        w.voids[s] = k.voids[s];
    }
    w.played = g.playedMask();
    w.trump = g.trump();
    w.broken = g.trumpBroken();
    w.n = (int)g.trick().size();
    w.leader = g.trick().empty() ? g.current() : g.trick()[0].seat;
    w.v = viewTrick(g.trick(), g.trump());
    w.declarer = g.declarer();
    w.esli = k.esli;
    return w;
}

int Bot::Impl::playKurt(const Game& g, int seat, const Know& k) {
    const Rules& r = g.rules();
    const uint64_t hand = g.handMask(seat);
    const TrickView v = viewTrick(g.trick(), g.trump());
    const uint64_t L = legalMask(hand, v, g.trump(), g.trumpBroken(), r);
    if (popcount(L) <= 1) return lowestCard(L);
    int cands[13];
    int nc = 0;
    for (uint64_t m = L; m; m &= m - 1) cands[nc++] = __builtin_ctzll(m);
    // budget: rollouts per decision, shared by the candidate cards
    const int budget = (int)tune[2];
    const int worlds = std::max(40, std::min(budget / 6, budget / nc));
    double sum[13] = {0};
    const int mySide = g.sideOf(seat);
    for (int wi = 0; wi < worlds; ++wi) {
        uint64_t hands[4];
        sampleWorld(rng, k, r, true, hands);
        const World base = worldFromGame(g, k, hands);
        for (int i = 0; i < nc; ++i) {
            World w = base;
            worldPlay(w, seat, cands[i], r);
            rollout(w, r);
            sum[i] += utility(r, mySide, w.tricks, g.declarer(), g.contract());
        }
    }
    int best = 0;
    for (int i = 1; i < nc; ++i)
        if (sum[i] > sum[best] + 1e-9) best = i;
    return cands[best];
}

void Bot::Impl::simulateBidding(const Game& g, int seat, const Know& k, int contract, int samples, double declU[4],
                                double* passU) {
    const Rules& r = g.rules();
    const int mySide = g.sideOf(seat);
    for (int t = 0; t < 4; ++t) declU[t] = 0;
    double pu = 0;
    const int partner = k.esli ? kart::partnerOf(seat) : -1;
    for (int si = 0; si < samples; ++si) {
        uint64_t hands[4];
        sampleWorld(rng, k, r, true, hands);
        auto play = [&](int decl, int trump, int cont) {
            World w;
            for (int s = 0; s < 4; ++s) w.hand[s] = hands[s];
            w.trump = trump;
            w.leader = decl;
            w.declarer = decl;
            w.esli = k.esli;
            rollout(w, r);
            return utility(r, mySide, w.tricks, decl, cont);
        };
        for (int t = 0; t < 4; ++t) declU[t] += play(seat, t, contract);
        if (passU) {
            // who would hold the ihale if I pass?
            int decl = -1, cont = 0;
            if (g.highBidder() >= 0 && g.highBidder() != seat) {
                decl = g.highBidder();
                cont = g.highBid();
                // someone still in may outbid; the strongest of them takes it at contract + 1
                double bestE = -1;
                int bestS = -1;
                for (int s = 0; s < 4; ++s) {
                    if (s == seat || s == decl || g.hasPassed(s)) continue;
                    const double e = bestEval(hands[s]) + (k.esli ? 3.0 : 0.0);
                    if (e > bestE) {
                        bestE = e;
                        bestS = s;
                    }
                }
                if (bestS >= 0 && bestE - 0.3 >= cont + 1 && bestE > bestEval(hands[decl]) + (k.esli ? 3.0 : 0.0)) {
                    decl = bestS;
                    cont = cont + 1;
                }
            } else {
                const int need = std::max(r.minBid, g.highBid() + 1);
                double bestE = -1;
                int bestS = -1;
                for (int s = 0; s < 4; ++s) {
                    if (s == seat || g.hasPassed(s)) continue;
                    const double e = bestEval(hands[s]) + (k.esli ? 3.0 : 0.0);
                    if (e > bestE) {
                        bestE = e;
                        bestS = s;
                    }
                }
                if (bestS >= 0 && bestE - 0.3 >= need) {
                    decl = bestS;
                    cont = need;
                } else {
                    decl = r.allPass == AllPass::Dealer ? g.dealer() : g.firstBidder();
                    cont = r.allPassBid;
                    if (r.allPass == AllPass::Redeal) decl = -1;
                }
            }
            if (decl < 0) pu += 0.0;
            else {
                int tr = 0;
                bestEval(hands[decl], &tr);
                pu += play(decl, tr, cont);
            }
            (void)partner;
        }
    }
    for (int t = 0; t < 4; ++t) declU[t] /= samples;
    if (passU) *passU = pu / samples;
}

Action Bot::Impl::bidKurt(const Game& g, int seat, const Know& k) {
    const int need = std::max(g.rules().minBid, g.highBid() + 1);
    // a partner holding the ihale: only take it over with a clearly better hand (checked by simulation)
    double declU[4], passU = 0;
    simulateBidding(g, seat, k, need, (int)tune[3], declU, &passU);
    double best = declU[0];
    for (int t = 1; t < 4; ++t) best = std::max(best, declU[t]);
    const double margin = k.esli ? tune[7] : tune[1];
    if (best > passU + margin) return makeBid(need);
    return makePass();
}

int Bot::Impl::trumpKurt(const Game& g, int seat, const Know& k) {
    double declU[4];
    simulateBidding(g, seat, k, g.contract(), (int)tune[4], declU, nullptr);
    int best = 0;
    for (int t = 1; t < 4; ++t)
        if (declU[t] > declU[best] + 1e-9) best = t;
    return best;
}

// ---------------------------------------------------------------------------------------------------------

Bot::Bot(Level level, uint64_t seed) : impl_(new Impl(level, seed)) {}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;
void Bot::setLevel(Level level) { impl_->level = level; }
Level Bot::level() const { return impl_->level; }
void Bot::resetForHand() {}
void Bot::observe(const GameEvent&, const Game&) {}
Action Bot::next(const Game& g, int seat) { return impl_->next(g, seat); }
void Bot::debugTune(int key, double value) {
    if (key >= 0 && key < 9) impl_->tune[key] = value;
}

} // namespace batak
