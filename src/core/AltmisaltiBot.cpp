// Altmışaltı (66) bots (see AltmisaltiBot.h).
#include "core/AltmisaltiBot.h"

#include <algorithm>
#include <cmath>

namespace altmisalti {

using kart::rankOf;
using kart::suitOf;

namespace {

int partnerOf(int card) { return kart::makeCard(suitOf(card), rankOf(card) == kart::Kiz ? kart::Papaz : kart::Kiz); }
bool isKQ(int c) { return rankOf(c) == kart::Kiz || rankOf(c) == kart::Papaz; }

// Cards p has not seen: not in its hand, not played, not the koz card under the stock.
CardMask unseenFor(const Deal& d, int p) {
    CardMask m = DECK_MASK & ~d.hand[(size_t)p] & ~d.played;
    if (d.stockN > 0 && d.stock[0] >= 0) m &= ~cardBit(d.stock[0]);
    return m;
}

int higherIn(CardMask m, int c) { // cards of m in c's suit that rank above c
    int n = 0;
    for (CardMask x = m & suitMask(suitOf(c)); x; x &= x - 1)
        if (cardOrder(lowestCard(x)) > cardOrder(c)) ++n;
    return n;
}

int highestOf(CardMask m) { // the highest-ranking card of m (any suit: by order)
    int best = -1;
    for (; m; m &= m - 1) {
        const int c = lowestCard(m);
        if (best < 0 || cardOrder(c) > cardOrder(best)) best = c;
    }
    return best;
}

template <class F>
int argminCard(CardMask m, F cost) {
    int best = -1;
    float bc = 1e9f;
    for (; m; m &= m - 1) {
        const int c = lowestCard(m);
        const float v = cost(c);
        if (v < bc) {
            bc = v;
            best = c;
        }
    }
    return best;
}

int randomCard(CardMask m, okey::Rng& rng) {
    const int n = popcount(m);
    if (n == 0) return -1;
    int k = rng.range(n);
    for (; m; m &= m - 1, --k)
        if (k == 0) return lowestCard(m);
    return -1;
}

struct Knobs {
    float closeAt = 66.f;    // close when the sure points reach this
    float randomPlay = 0.f;  // chance of a random legal card (Acemi)
    bool mayClose = true;
    float exchangeP = 1.f;
};

// What p can count on if the stock is closed now: points so far, marriages in hand, and the top cards (sure tricks).
float closeEstimate(const Deal& d, int p) {
    const CardMask hand = d.hand[(size_t)p];
    const CardMask unseen = unseenFor(d, p);
    const int T = d.trumpSuit;
    float pts = (float)(d.points[(size_t)p] + d.pending[(size_t)p]);
    for (int s = 0; s < 4; ++s)
        if ((hand & cardBit(kart::makeCard(s, kart::Kiz))) && (hand & cardBit(kart::makeCard(s, kart::Papaz))))
            pts += s == T ? 40.f : 20.f;
    int sureTrumps = 0;
    for (CardMask m = hand & suitMask(T); m; m &= m - 1) {
        const int c = lowestCard(m);
        if (higherIn(unseen, c) == 0) {
            pts += (float)cardPoints(c) + 3.f;
            ++sureTrumps;
        }
    }
    const int oppTrumps = popcount(unseen & suitMask(T));
    const float side = sureTrumps >= oppTrumps ? 1.f : 0.6f; // the other may ruff a side winner
    for (int s = 0; s < 4; ++s) {
        if (s == T) continue;
        for (CardMask m = hand & suitMask(s); m; m &= m - 1) {
            const int c = lowestCard(m);
            if (higherIn(unseen, c) == 0) pts += side * ((float)cardPoints(c) + 3.f);
        }
    }
    return pts;
}

// The Usta rules (also the rollout policy of Kurt). Uses only p's own hand and public facts of `d`.
BotAction policy(const Deal& d, int p, okey::Rng& rng, const Knobs& k) {
    BotAction a;
    if (d.canExchange(p) && (k.exchangeP >= 1.f || rng.chance(k.exchangeP))) {
        a.kind = BotAction::Kind::Exchange;
        return a;
    }
    const CardMask legal = d.legal(p);
    a.kind = BotAction::Kind::Play;
    if (k.randomPlay > 0.f && rng.chance(k.randomPlay)) {
        a.card = randomCard(legal, rng);
        return a;
    }
    const int T = d.trumpSuit;
    const CardMask hand = d.hand[(size_t)p];
    const CardMask unseen = unseenFor(d, p);
    auto isTrump = [&](int c) { return suitOf(c) == T; };
    auto kqWeight = [&](int c) -> float { // keeping a marriage piece
        if (!isKQ(c)) return 0.f;
        const int q = partnerOf(c);
        if (hand & cardBit(q)) return 12.f;
        return (unseen & cardBit(q)) ? 3.f : 0.f;
    };

    if (d.lead < 0) {
        if (k.mayClose && d.canClose(p) && d.tricks[(size_t)p] > 0 && closeEstimate(d, p) >= k.closeAt) {
            a.kind = BotAction::Kind::Close;
            return a;
        }
        // a marriage: lead its Kız (koz first)
        int mar = -1, mv = 0;
        for (int s = 0; s < 4; ++s) {
            const int q = kart::makeCard(s, kart::Kiz);
            if ((hand & cardBit(q)) && (hand & cardBit(partnerOf(q)))) {
                const int v = s == T ? 40 : 20;
                if (v > mv) {
                    mv = v;
                    mar = q;
                }
            }
        }
        if (mar >= 0) {
            a.card = mar;
            return a;
        }
        if (d.strict()) {
            // pull the koz with the top koz, then cash the top cards, then the cheapest
            const CardMask tr = hand & suitMask(T);
            if (tr && (unseen & suitMask(T))) {
                const int c = highestOf(tr);
                if (higherIn(unseen, c) == 0) {
                    a.card = c;
                    return a;
                }
            }
            int best = -1, bv = -1;
            for (int s = 0; s < 4; ++s) {
                const CardMask m = hand & suitMask(s);
                if (!m) continue;
                const int c = highestOf(m);
                if (higherIn(unseen, c) == 0 && (s == T || !(unseen & suitMask(T)) || (unseen & suitMask(s)))) {
                    if (cardPoints(c) > bv) {
                        bv = cardPoints(c);
                        best = c;
                    }
                }
            }
            if (best >= 0) {
                a.card = best;
                return a;
            }
            a.card = argminCard(hand, [&](int c) { return (float)cardPoints(c) + (isTrump(c) ? 8.f : 0.f) + kqWeight(c) * 0.5f; });
            return a;
        }
        a.card = argminCard(hand, [&](int c) {
            float v = (float)cardPoints(c) + kqWeight(c) * 0.5f;
            if (isTrump(c)) v += 10.f + 3.f * (float)cardOrder(c);
            if (cardPoints(c) >= 10 && higherIn(unseen, c) > 0) v += 5.f;
            return v;
        });
        return a;
    }

    const int L = d.lead;
    CardMask win = 0;
    for (CardMask m = legal; m; m &= m - 1)
        if (beats(L, lowestCard(m), T)) win |= cardBit(lowestCard(m));
    if (d.strict()) {
        if (win) a.card = argminCard(win, [&](int c) { return (float)cardOrder(c) + (isTrump(c) ? 10.f : 0.f); });
        else a.card = argminCard(legal, [&](int c) { return (float)cardPoints(c) * 10.f + (float)cardOrder(c) + kqWeight(c); });
        return a;
    }
    const int base = d.points[(size_t)p] + d.pending[(size_t)p] + cardPoints(L);
    auto cheapestWin = [&](CardMask m) {
        return argminCard(m, [&](int c) { return (float)cardOrder(c) + (isTrump(c) ? 10.f : 0.f) + kqWeight(c); });
    };
    {
        CardMask reach = 0;
        for (CardMask m = win; m; m &= m - 1)
            if (base + cardPoints(lowestCard(m)) >= GOAL) reach |= cardBit(lowestCard(m));
        if (reach) {
            a.card = cheapestWin(reach);
            return a;
        }
    }
    const CardMask sameWin = win & suitMask(suitOf(L));
    if (suitOf(L) != T) {
        if (cardPoints(L) >= 10) {
            if (sameWin) {
                a.card = cheapestWin(sameWin);
                return a;
            }
            const CardMask tr = win & suitMask(T);
            if (tr) {
                a.card = cheapestWin(tr);
                return a;
            }
        } else {
            CardMask bank = 0;
            for (CardMask m = sameWin; m; m &= m - 1)
                if (cardPoints(lowestCard(m)) >= 10) bank |= cardBit(lowestCard(m));
            if (bank) {
                a.card = cheapestWin(bank);
                return a;
            }
            if (cardPoints(L) >= 3) {
                CardMask cheap = 0;
                for (CardMask m = sameWin; m; m &= m - 1)
                    if (cardPoints(lowestCard(m)) <= 4 && kqWeight(lowestCard(m)) < 10.f) cheap |= cardBit(lowestCard(m));
                if (cheap) {
                    a.card = cheapestWin(cheap);
                    return a;
                }
            }
        }
    } else if (cardPoints(L) >= 10 && sameWin) {
        a.card = cheapestWin(sameWin);
        return a;
    }
    a.card = argminCard(legal, [&](int c) {
        float v = (float)cardPoints(c) + kqWeight(c);
        if (isTrump(c)) v += 8.f + 3.f * (float)cardOrder(c);
        return v;
    });
    return a;
}

void applyTo(Deal& d, int p, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::Exchange: d.exchange(p); break;
    case BotAction::Kind::Close: d.close(p); break;
    case BotAction::Kind::Play: d.play(p, a.card); break;
    }
}

// ---- the exact endgame (strict rules: no more drawing, so both hands decide everything) ----
constexpr int SCALE = 1000; // a game point; card points break ties

int leafValue(const Deal& d, int me) { return signedResult(d, me) * SCALE + (d.points[(size_t)me] - d.points[(size_t)(1 - me)]); }

int search(const Deal& d, int me, int alpha, int beta, long& nodes) {
    ++nodes;
    if (d.over) return leafValue(d, me);
    const int q = d.turn();
    const CardMask m = d.legal(q);
    int cards[HAND_SIZE + 1];
    int n = 0;
    for (CardMask x = m; x && n < HAND_SIZE + 1; x &= x - 1) cards[n++] = lowestCard(x);
    // the strong cards first (they decide most tricks)
    std::sort(cards, cards + n, [&](int a, int b) {
        const int va = cardOrder(a) + (suitOf(a) == d.trumpSuit ? 6 : 0), vb = cardOrder(b) + (suitOf(b) == d.trumpSuit ? 6 : 0);
        return va > vb;
    });
    const bool maxi = q == me;
    int best = maxi ? -1000000 : 1000000;
    for (int i = 0; i < n; ++i) {
        Deal x = d;
        x.play(q, cards[i]);
        const int v = search(x, me, alpha, beta, nodes);
        if (maxi) {
            best = std::max(best, v);
            alpha = std::max(alpha, v);
        } else {
            best = std::min(best, v);
            beta = std::min(beta, v);
        }
        if (alpha >= beta) break;
    }
    return best;
}

int solve(const Deal& d, int me) {
    long nodes = 0;
    return search(d, me, -1000000, 1000000, nodes);
}

// Plays a sampled deal out: the Usta rules while the stock is open, the exact search once it is strict.
int playOut(Deal d, int me, okey::Rng& rng) {
    const Knobs k;
    while (!d.over) {
        if (d.strict()) return solve(d, me);
        const int q = d.turn();
        applyTo(d, q, policy(d, q, rng, k));
    }
    return leafValue(d, me);
}

// The unseen cards dealt at random: the other hand (its shown cards kept, nothing it is known not to hold) and the
// face-down stock. False if the facts do not add up (then the constraints are dropped).
void sampleDeal(const Deal& view, int p, int oppSize, CardMask oppCannot, okey::Rng& rng, Deal& out) {
    const int o = 1 - p;
    out = view;
    const CardMask known = view.hand[(size_t)o];
    CardMask stockKnown = view.stockN > 0 && view.stock[0] >= 0 ? cardBit(view.stock[0]) : 0;
    const CardMask U = DECK_MASK & ~(view.hand[(size_t)p] | view.played | known | stockKnown);
    std::vector<int> u = cardsOf(U);
    rng.shuffle(u);
    int need = oppSize - popcount(known);
    std::vector<int> rest;
    rest.reserve(u.size());
    CardMask opp = known;
    for (int c : u) {
        if (need > 0 && !(oppCannot & cardBit(c))) {
            opp |= cardBit(c);
            --need;
        } else {
            rest.push_back(c);
        }
    }
    while (need > 0 && !rest.empty()) { // (inconsistent facts: ignore them)
        opp |= cardBit(rest.back());
        rest.pop_back();
        --need;
    }
    out.hand[(size_t)o] = opp;
    size_t k = 0;
    for (int i = 1; i < out.stockN; ++i) out.stock[(size_t)i] = k < rest.size() ? rest[k++] : -1;
}

struct Evaluated {
    std::vector<BotAction> options;
    std::vector<double> mean, se;
};

// Kurt: every option over `samples` sampled deals (the same deals for every option).
Evaluated evaluateAll(const Game& g, int p, int samples, okey::Rng& rng) {
    Evaluated ev;
    const Deal view = g.viewOf(p);
    if (view.canClose(p)) ev.options.push_back(BotAction{BotAction::Kind::Close, -1});
    for (int c : cardsOf(view.legal(p))) ev.options.push_back(BotAction{BotAction::Kind::Play, c});
    const size_t n = ev.options.size();
    ev.mean.assign(n, 0.0);
    ev.se.assign(n, 0.0);
    if (n == 0) return ev;
    const bool exact = view.strict() && view.stockN == 0; // every card is known now
    if (exact) samples = 1;
    const int oppSize = g.handSize(1 - p);
    const CardMask cannot = g.cannotHold(1 - p);
    std::vector<std::vector<double>> v(n);
    for (int s = 0; s < samples; ++s) {
        Deal d;
        sampleDeal(view, p, oppSize, cannot, rng, d);
        const uint64_t rs = rng.next();
        for (size_t i = 0; i < n; ++i) {
            okey::Rng rr(rs); // (the same play-out luck for every option)
            Deal x = d;
            applyTo(x, p, ev.options[i]);
            v[i].push_back((double)playOut(x, p, rr) / SCALE);
        }
    }
    size_t best = 0;
    for (size_t i = 0; i < n; ++i) {
        double sum = 0;
        for (double x : v[i]) sum += x;
        ev.mean[i] = sum / (double)std::max<size_t>(1, v[i].size());
        if (ev.mean[i] > ev.mean[best]) best = i;
    }
    for (size_t i = 0; i < n; ++i) {
        if (i == best || v[i].size() < 2) continue;
        double m = 0, m2 = 0;
        const size_t k = v[i].size();
        for (size_t j = 0; j < k; ++j) {
            const double dd = v[i][j] - v[best][j];
            m += dd;
            m2 += dd * dd;
        }
        m /= (double)k;
        const double var = std::max(0.0, m2 / (double)k - m * m);
        ev.se[i] = std::sqrt(var / (double)k);
    }
    return ev;
}

} // namespace

ActionResult applyBotAction(Game& g, int p, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::Exchange: return g.exchangeNine(p);
    case BotAction::Kind::Close: return g.closeStock(p);
    case BotAction::Kind::Play: return g.playCard(p, a.card);
    }
    return ActionResult::fail("?");
}

BotAction fallbackAction(const Game& g, int p) {
    BotAction a;
    const std::vector<int> l = g.legalCards(p);
    a.card = l.empty() ? -1 : l.front();
    return a;
}

struct Bot::Impl {
    BotLevel level = BotLevel::Usta;
    BotStyle style;
    okey::Rng rng;
    Knobs knobs() const {
        Knobs k;
        k.closeAt = 66.f - 2.f * style.boldness;
        if (level == BotLevel::Acemi) {
            k.randomPlay = 0.4f;
            k.mayClose = false;
            k.exchangeP = 0.6f;
        }
        return k;
    }
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(std::make_unique<Impl>()) {
    impl_->level = level;
    impl_->rng.reseed(seed ^ 0x5157A66ull);
}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;
void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::setStyle(BotStyle style) { impl_->style = style; }
BotStyle Bot::style() const { return impl_->style; }

BotAction Bot::next(const Game& g, int p) {
    const Deal view = g.viewOf(p);
    if (impl_->level != BotLevel::Kurt) {
        BotAction a = policy(view, p, impl_->rng, impl_->knobs());
        if (a.kind == BotAction::Kind::Play && !(view.legal(p) & cardBit(a.card))) a = fallbackAction(g, p);
        return a;
    }
    if (view.canExchange(p)) return BotAction{BotAction::Kind::Exchange, -1};
    const CardMask legal = view.legal(p);
    if (popcount(legal) == 1 && !view.canClose(p)) return BotAction{BotAction::Kind::Play, lowestCard(legal)};
    const Evaluated ev = evaluateAll(g, p, view.strict() ? 40 : 64, impl_->rng);
    size_t best = 0;
    // closing is judged by the exact endgame on sampled hands (it knows them): it has to beat the rest clearly
    const double margin = 0.12 - 0.08 * (double)impl_->style.boldness;
    auto score = [&](size_t i) { return ev.mean[i] - (ev.options[i].kind == BotAction::Kind::Close ? margin : 0.0); };
    for (size_t i = 1; i < ev.options.size(); ++i)
        if (score(i) > score(best)) best = i;
    if (ev.options.empty()) return fallbackAction(g, p);
    return ev.options[best];
}

std::vector<ActionValue> Bot::evaluate(const Game& g, int p) const {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ g.matchSeed();
    h = h * 1099511628211ull + g.actionLog().size();
    h = h * 1099511628211ull + (uint64_t)p;
    okey::Rng rr(h);
    const Evaluated ev = evaluateAll(g, p, 128, rr);
    std::vector<ActionValue> out;
    for (size_t i = 0; i < ev.options.size(); ++i) out.push_back({ev.options[i], ev.mean[i], ev.se[i]});
    return out;
}

} // namespace altmisalti
