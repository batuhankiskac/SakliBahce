// Konken computer opponents (see KonkenBot.h).
#include "core/KonkenBot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace konken {

namespace {

// ---------------------------------------------------------------------------------------------------------
// What a seat knows
// ---------------------------------------------------------------------------------------------------------

constexpr int JOKER_SLOT = NUM_FACES; // unseen[52]: jokers

struct View {
    std::array<int, NUM_FACES + 1> unseen{}; // copies of each face nobody has shown (0..2), jokers (0..4)
    int unseenTotal = 0;
    std::vector<int> pool;                   // the unseen cards themselves (ids), for sampling
};

View viewOf(const Game& g, int seat) {
    std::array<bool, NUM_CARDS> known{};
    for (int c : g.hand(seat)) known[(size_t)c] = true;
    for (const Meld& m : g.table())
        for (int c : m.cards) known[(size_t)c] = true;
    for (int c : g.discardPile()) known[(size_t)c] = true;
    View v;
    for (int c = 0; c < NUM_CARDS; ++c) {
        if (known[(size_t)c]) continue;
        v.pool.push_back(c);
        v.unseen[(size_t)(isJoker(c) ? JOKER_SLOT : faceOf(c))] += 1;
    }
    v.unseenTotal = (int)v.pool.size();
    return v;
}

int faceAt(int suit, int rank) { // rank 1..14 (1 = the As)
    if (rank < 1 || rank > 14) return -1;
    return kart::makeCard(suit, rank == 1 ? 14 : rank);
}

int unseenAt(const View& v, int suit, int rank) {
    const int f = faceAt(suit, rank);
    return f < 0 ? 0 : v.unseen[(size_t)f];
}

// Live cards that would turn the two real cards a, b into a meld (0: they do not belong together).
int outsBetween(int a, int b, const View& v) {
    if (isJoker(a) || isJoker(b)) return 0;
    const int ra = rankOf(a), rb = rankOf(b), sa = suitOf(a), sb = suitOf(b);
    const int J = v.unseen[JOKER_SLOT];
    if (ra == rb) {
        if (sa == sb) return 0;
        int o = J;
        for (int s = 0; s < 4; ++s)
            if (s != sa && s != sb) o += v.unseen[(size_t)kart::makeCard(s, ra)];
        return o;
    }
    if (sa != sb) return 0;
    // the As both low and high: the nearer reading
    auto lowHigh = [&](int x, int y, int& lo, int& hi) {
        lo = std::min(x, y);
        hi = std::max(x, y);
    };
    int best = 0;
    for (int alt = 0; alt < 2; ++alt) {
        int xa = ra, xb = rb;
        if (alt == 1) {
            if (xa == 14) xa = 1;
            else if (xb == 14) xb = 1;
            else continue;
        }
        int lo, hi;
        lowHigh(xa, xb, lo, hi);
        int o = 0;
        if (hi - lo == 1) o = J + unseenAt(v, sa, lo - 1) + unseenAt(v, sa, hi + 1);
        else if (hi - lo == 2) o = J + unseenAt(v, sa, lo + 1);
        best = std::max(best, o);
    }
    return best;
}

// Cards of `cards` that form a near-meld with `c` (structure only).
int links(int c, const std::vector<int>& cards) {
    if (isJoker(c)) return 3;
    int n = 0;
    for (int d : cards) {
        if (d == c || isJoker(d)) continue;
        if (rankOf(d) == rankOf(c) && suitOf(d) != suitOf(c)) ++n;
        else if (suitOf(d) == suitOf(c)) {
            int diff = std::abs(rankOf(d) - rankOf(c));
            if (rankOf(d) == 14 || rankOf(c) == 14) diff = std::min(diff, std::abs((rankOf(d) == 14 ? 1 : rankOf(d)) - (rankOf(c) == 14 ? 1 : rankOf(c))));
            if (diff >= 1 && diff <= 2) ++n;
        }
    }
    return n;
}

std::vector<int> without(const std::vector<int>& v, int c) {
    std::vector<int> o = v;
    const auto it = std::find(o.begin(), o.end(), c);
    if (it != o.end()) o.erase(it);
    return o;
}

int handPointsOf(const std::vector<int>& cards, const Rules& r) {
    int p = 0;
    for (int c : cards) p += handPoints(c, r.jokerPoints);
    return p;
}

// Where `card` can go on the table: (meld, side) of the first fit, or a joker swap (swap = true). -1: nowhere.
int tableFit(const std::vector<Meld>& table, int card, bool* swap = nullptr) {
    for (size_t i = 0; i < table.size(); ++i)
        if (!isJoker(card) && swapIndex(table[i], card) >= 0) {
            if (swap) *swap = true;
            return (int)i;
        }
    for (size_t i = 0; i < table.size(); ++i)
        if (canAdd(table[i], card, Side::Auto)) {
            if (swap) *swap = false;
            return (int)i;
        }
    return -1;
}

// How dangerous the hand is getting: 0.25 (early, nobody close) .. 1 (someone about to finish or the stock nearly out).
double riskOf(const Game& g, int seat) {
    double threat = 0.15;
    for (int s = 0; s < 4; ++s) {
        if (s == seat) continue;
        if (!g.opened(s)) continue;
        const int k = g.handSize(s);
        threat = std::max(threat, k <= 2 ? 1.0 : k <= 4 ? 0.75 : k <= 7 ? 0.5 : 0.3);
    }
    const double stock = 1.0 - (double)g.stockSize() / 51.0;
    return std::clamp(std::max(threat, stock), 0.25, 1.0);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------------------

ActionResult applyBotAction(Game& g, int seat, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::DrawStock: return g.drawStock(seat);
    case BotAction::Kind::TakeDiscard: return g.takeDiscard(seat);
    case BotAction::Kind::ReturnDiscard: return g.returnDiscard(seat);
    case BotAction::Kind::Lay: return g.layMelds(seat, a.melds);
    case BotAction::Kind::Add: return g.addToMeld(seat, a.card, a.meld, a.side);
    case BotAction::Kind::Swap: return g.swapJoker(seat, a.card, a.meld);
    case BotAction::Kind::Discard: return g.discard(seat, a.card);
    }
    return ActionResult::fail("?");
}

BotAction fallbackAction(const Game& g, int seat) {
    BotAction a;
    if (g.stage() == Stage::Draw) {
        a.kind = BotAction::Kind::DrawStock;
        return a;
    }
    const std::vector<int>& h = g.hand(seat);
    if (g.takenCard() >= 0) {
        const int t = g.takenCard();
        bool sw = false;
        const int m = g.opened(seat) ? tableFit(g.table(), t, &sw) : -1;
        if (m >= 0) {
            a.kind = sw ? BotAction::Kind::Swap : BotAction::Kind::Add;
            a.card = t;
            a.meld = m;
            return a;
        }
        a.kind = BotAction::Kind::ReturnDiscard;
        return a;
    }
    // the dearest card that may go (a joker only as the last card); only jokers left: put one on the table
    int best = -1;
    for (int c : h)
        if ((!isJoker(c) || h.size() == 1) && (best < 0 || handPoints(c) > handPoints(best))) best = c;
    if (best < 0 && g.opened(seat))
        for (int c : h) {
            const int m = tableFit(g.table(), c);
            if (m >= 0) {
                a.kind = BotAction::Kind::Add;
                a.card = c;
                a.meld = m;
                return a;
            }
        }
    a.kind = BotAction::Kind::Discard;
    a.card = best >= 0 ? best : (h.empty() ? -1 : h.front());
    return a;
}

std::string describeAction(const Game& g, int seat, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::DrawStock: return "Desteden çek";
    case BotAction::Kind::TakeDiscard: return "Yerden " + cardAccusativeTR(g.discardTop()) + " al";
    case BotAction::Kind::ReturnDiscard: return "Yerden aldığını geri ver";
    case BotAction::Kind::Lay: {
        int v = 0;
        for (const auto& mv : a.melds) {
            Meld m;
            if (makeMeld(mv, m)) v += m.value();
        }
        return g.opened(seat) ? std::string("Per indir") : "Aç (" + std::to_string(v) + ")";
    }
    case BotAction::Kind::Add: return cardAccusativeTR(a.card) + " işle";
    case BotAction::Kind::Swap: return "Jokeri al (" + cardNameTR(a.card) + " koy)";
    case BotAction::Kind::Discard: return cardAccusativeTR(a.card) + " at";
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------
// Bot
// ---------------------------------------------------------------------------------------------------------

struct Bot::Impl {
    BotLevel level = BotLevel::Usta;
    BotStyle style;
    Rng rng{1};
    uint64_t seed = 1;

    double bold() const { return std::clamp((double)style.boldness, -1.0, 1.0); }

    // ---- the hand's worth (higher is better) for Usta's discard choice ----
    double handEval(const Game& g, const std::vector<int>& hand, bool opened, const View& v, double risk) const {
        const Rules& R = g.rules();
        const Partition P = bestPartition(hand, opened ? Goal::Shed : Goal::Value, -1, false, R.jokerPoints);
        std::vector<int> dead, jk;
        for (int c : P.rest) (isJoker(c) ? jk : dead).push_back(c);
        double partial = 0.0;
        for (size_t i = 0; i < dead.size(); ++i)
            for (size_t j = i + 1; j < dead.size(); ++j) {
                const int o = outsBetween(dead[i], dead[j], v);
                if (o <= 0) continue;
                const double w = opened ? 2.0 : 0.6 + (handPoints(dead[i]) + handPoints(dead[j])) / 16.0;
                partial += std::min(o, 5) * w;
            }
        // a loose card next to a meld of the hand (5-6-7 and 9: the 8 makes it longer)
        for (int c : dead)
            for (const auto& mv : P.melds) {
                Meld m;
                if (!makeMeld(mv, m) || m.kind != MeldKind::Run || suitOf(c) != m.suit) continue;
                const int r = rankOf(c);
                if (r == m.high() + 2) partial += unseenAt(v, m.suit, m.high() + 1) * (opened ? 1.5 : 1.0);
                if (r == m.low() - 2 || (r == 14 && m.low() == 3)) partial += unseenAt(v, m.suit, m.low() - 1) * (opened ? 1.5 : 1.0);
            }
        const double styleKeep = 1.0 + 0.25 * bold();
        if (opened) {
            const int deadPts = handPointsOf(dead, R);
            return -risk * (1.0 - 0.15 * bold()) * deadPts + (1.0 - 0.6 * risk) * styleKeep * partial + jk.size() * (14.0 - 22.0 * risk);
        }
        const double progress = std::min(P.value, R.openMin) + (P.value >= R.openMin ? 25.0 : 0.0);
        return progress + styleKeep * partial + jk.size() * 16.0;
    }

    // What handing `card` to the next player is expected to cost (Usta: only a card that fits the table for an opened
    // neighbour; Kurt also reads what the neighbour picked up and let go).
    double danger(const Game& g, int seat, int card) const {
        const int nx = g.nextActive(seat); // (the burned are skipped)
        const double styleW = 1.0 - 0.4 * bold();
        double d = 0.0;
        if (g.opened(nx) && g.fitsTable(card)) d += 10.0 + 2.5 * std::max(0, 8 - g.handSize(nx));
        if (level == BotLevel::Kurt) {
            for (const DiscardRecord& r : g.discards()) {
                if (isJoker(r.card) || isJoker(card)) continue;
                const bool sameRank = rankOf(r.card) == rankOf(card);
                const bool near = suitOf(r.card) == suitOf(card) && std::abs(rankOf(r.card) - rankOf(card)) <= 2;
                if (r.takenBy == nx && (sameRank || near)) d += 4.0; // it wanted cards like this
                if (r.seat == nx && sameRank) d -= 2.5;              // it threw that rank away
                if (r.seat == nx && near && !sameRank) d -= 1.0;
            }
            if (!g.opened(nx)) d += 0.12 * handPoints(card); // a big card helps an opening more
        }
        return std::max(0.0, d) * styleW;
    }

    std::vector<int> discardable(const Game& g, int seat) const {
        std::vector<int> v;
        const std::vector<int>& h = g.hand(seat);
        for (int c : h) {
            if (c == g.takenCard()) continue;
            if (isJoker(c) && h.size() > 1) continue;
            if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
        }
        return v;
    }

    // Usta's score of every candidate discard (higher = throw it).
    std::vector<std::pair<double, int>> ustaRank(const Game& g, int seat) const {
        const View v = viewOf(g, seat);
        const double risk = riskOf(g, seat);
        std::vector<std::pair<double, int>> out;
        std::vector<int> faces; // one per face (the second copy is the same card for the hand)
        for (int c : discardable(g, seat)) {
            const int f = isJoker(c) ? JOKER_SLOT : faceOf(c);
            if (std::find(faces.begin(), faces.end(), f) != faces.end()) continue;
            faces.push_back(f);
            const double e = handEval(g, without(g.hand(seat), c), g.opened(seat), v, risk) - danger(g, seat, c);
            out.push_back({e, c});
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
        return out;
    }

    // ---- Kurt's rollouts ----
    static double finishProb(int k) {
        static const double p[] = {1.0, 0.55, 0.33, 0.2, 0.12, 0.08, 0.05, 0.04, 0.03};
        return k < 9 ? p[k] : 0.02;
    }

    // The seat's points at the end of the hand after throwing `card` now (one sampled future).
    int rolloutOnce(const Game& g, int seat, int card, const std::vector<int>& pool, Rng& r) const {
        const Rules& R = g.rules();
        std::vector<int> hand = without(g.hand(seat), card);
        bool opened = g.opened(seat);
        std::vector<Meld> table = g.table();
        std::array<int, 4> k{};
        std::array<bool, 4> op{};
        for (int s = 0; s < 4; ++s) {
            k[(size_t)s] = g.handSize(s);
            op[(size_t)s] = g.opened(s);
        }
        int stock = g.stockSize();
        std::vector<int> deck = pool;
        r.shuffle(deck);
        size_t pos = 0;
        auto mine = [&]() { return opened ? handPointsOf(hand, R) : R.unopenedPoints; };
        const int nx = g.nextActive(seat); // (the burned are skipped)
        // the next player picks the card up when it fits the table and they have opened
        if (op[(size_t)nx] && tableFit(table, card) >= 0) k[(size_t)nx] = std::max(1, k[(size_t)nx] - 1);
        int turn = g.turn();
        for (int round = 0; round < 40; ++round) {
            for (int j = 1; j <= 3; ++j) {
                const int o = (seat + j) % 4;
                if (!g.active(o)) continue; // (burned: not at the table)
                ++turn;
                if (op[(size_t)o]) {
                    if (r.uniform() < finishProb(k[(size_t)o])) return mine();
                    if (r.uniform() < 0.3f) k[(size_t)o] = std::max(1, k[(size_t)o] - 1);
                } else {
                    if (r.uniform() < 0.004f) return mine(); // konken
                    if (r.uniform() < 0.05f + 0.006f * (float)turn / 4.f) {
                        op[(size_t)o] = true;
                        k[(size_t)o] = std::max(2, k[(size_t)o] - 7);
                    }
                }
                if (--stock <= 0) return mine();
            }
            // my turn: draw, open / lay / add, shed the dearest loose card
            if (pos >= deck.size()) return mine();
            hand.push_back(deck[pos++]);
            --stock;
            Partition P = bestPartition(hand, opened ? Goal::Shed : Goal::Value, -1, opened, R.jokerPoints);
            if (!opened && P.value >= R.openMin) {
                opened = true;
                P = bestPartition(hand, Goal::Shed, -1, true, R.jokerPoints);
                if (P.value < R.openMin) P = bestPartition(hand, Goal::Value, -1, true, R.jokerPoints);
            }
            if (opened) {
                for (const auto& mv : P.melds) {
                    Meld m;
                    if (makeMeld(mv, m)) table.push_back(m);
                }
                hand = P.rest;
                for (size_t i = 0; i < hand.size();) {
                    const int m = tableFit(table, hand[i]);
                    if (m >= 0 && !(isJoker(hand[i]) && swapIndex(table[(size_t)m], hand[i]) >= 0)) {
                        addCard(table[(size_t)m], hand[i], Side::Auto);
                        hand.erase(hand.begin() + (std::ptrdiff_t)i);
                    } else {
                        ++i;
                    }
                }
                if (hand.empty()) return 0;
            }
            // discard
            int best = -1;
            double bestS = -1e9;
            const std::vector<int>& looseFrom = opened ? hand : P.rest;
            for (int c : hand) {
                if (isJoker(c) && hand.size() > 1) continue;
                const bool loose = std::find(looseFrom.begin(), looseFrom.end(), c) != looseFrom.end();
                const double s = handPoints(c) - 5.0 * links(c, looseFrom) + (loose ? 30.0 : 0.0);
                if (s > bestS) {
                    bestS = s;
                    best = c;
                }
            }
            if (best < 0) return mine();
            hand = without(hand, best);
            if (hand.empty()) return 0;
            if (--stock <= 0) return mine();
        }
        return mine();
    }

    // Expected points (rollouts) + the feeding danger, for each candidate; common random numbers per sample.
    std::vector<DiscardValue> rollouts(const Game& g, int seat, const std::vector<int>& cands, int samples, uint64_t salt) const {
        const View v = viewOf(g, seat);
        std::vector<DiscardValue> out;
        std::vector<std::vector<double>> vals(cands.size());
        for (int smp = 0; smp < samples; ++smp) {
            for (size_t i = 0; i < cands.size(); ++i) {
                Rng r(salt * 0x9E3779B97F4A7C15ull + (uint64_t)smp * 7919u + 1u);
                vals[i].push_back((double)rolloutOnce(g, seat, cands[i], v.pool, r));
            }
        }
        size_t bestI = 0;
        std::vector<double> mean(cands.size(), 0.0);
        for (size_t i = 0; i < cands.size(); ++i) {
            for (double x : vals[i]) mean[i] += x;
            mean[i] = mean[i] / (double)std::max(1, samples) + danger(g, seat, cands[i]) * 0.8;
            if (mean[i] < mean[bestI]) bestI = i;
        }
        for (size_t i = 0; i < cands.size(); ++i) {
            double s2 = 0.0, m = 0.0;
            for (int smp = 0; smp < samples; ++smp) m += vals[i][(size_t)smp] - vals[bestI][(size_t)smp];
            m /= (double)std::max(1, samples);
            for (int smp = 0; smp < samples; ++smp) {
                const double d = vals[i][(size_t)smp] - vals[bestI][(size_t)smp] - m;
                s2 += d * d;
            }
            const double se = samples > 1 ? std::sqrt(s2 / (double)(samples - 1) / (double)samples) : 0.0;
            out.push_back({cands[i], mean[i], i == bestI ? 0.0 : se});
        }
        return out;
    }

    uint64_t stateSalt(const Game& g, int seat) const {
        uint64_t h = seed ^ 0xC0FFEEull;
        auto mix = [&](uint64_t x) { h = (h ^ x) * 0x100000001B3ull; };
        mix((uint64_t)seat);
        mix((uint64_t)g.actionLog().size());
        mix((uint64_t)g.handIndex());
        for (int c : g.hand(seat)) mix((uint64_t)c + 1);
        return h;
    }

    // ---- the decisions ----
    bool canUse(const Game& g, int seat, int d) const {
        const Rules& R = g.rules();
        std::vector<int> h = g.hand(seat);
        h.push_back(d);
        if (!g.opened(seat)) {
            const Partition P = bestPartition(h, Goal::Value, d, true, R.jokerPoints);
            return !P.melds.empty() && P.value >= R.openMin;
        }
        if (tableFit(g.table(), d) >= 0) return true;
        const Partition P = bestPartition(h, Goal::Shed, d, false, R.jokerPoints);
        return !P.melds.empty();
    }

    bool keepJokers(const Game& g, int seat) const {
        if (level == BotLevel::Acemi) return false;
        return riskOf(g, seat) < 0.5 + 0.15 * bold();
    }

    BotAction drawDecision(const Game& g, int seat) {
        BotAction a;
        a.kind = BotAction::Kind::DrawStock;
        const int d = g.discardTop();
        if (g.mustDrawStock() || d < 0 || !g.canTakeDiscard(seat)) return a;
        if (!canUse(g, seat, d)) return a;
        if (level == BotLevel::Acemi) {
            // sees only the obvious: a card for a meld already on the table, and now and then an opening
            const bool obvious = g.opened(seat) && tableFit(g.table(), d) >= 0;
            if (!obvious && rng.uniform() > 0.45f) return a;
        }
        a.kind = BotAction::Kind::TakeDiscard;
        return a;
    }

    BotAction openingAction(const Game& g, int seat, int must) const {
        const Rules& R = g.rules();
        BotAction a;
        Partition P = bestPartition(g.hand(seat), Goal::Value, must, !keepJokers(g, seat), R.jokerPoints);
        if (P.value < R.openMin) P = bestPartition(g.hand(seat), Goal::Value, must, true, R.jokerPoints);
        if (P.melds.empty() || P.value < R.openMin) {
            a.kind = BotAction::Kind::Discard; // (caller: no opening)
            return a;
        }
        // never left with two jokers and nothing else (a joker goes only as the last card)
        int jk = 0;
        for (int c : P.rest) jk += isJoker(c) ? 1 : 0;
        if (jk >= 2 && jk == (int)P.rest.size()) P = bestPartition(g.hand(seat), Goal::Value, must, true, R.jokerPoints);
        a.kind = BotAction::Kind::Lay;
        a.melds = P.melds;
        return a;
    }

    BotAction playDecision(const Game& g, int seat) {
        const Rules& R = g.rules();
        const std::vector<int>& h = g.hand(seat);
        const int t = g.takenCard();
        BotAction a;
        // the discard taken this turn must be used
        if (t >= 0) {
            if (!g.opened(seat)) {
                BotAction o = openingAction(g, seat, t);
                if (o.kind == BotAction::Kind::Lay) return o;
                a.kind = BotAction::Kind::ReturnDiscard;
                return a;
            }
            bool sw = false;
            const int m = tableFit(g.table(), t, &sw);
            if (m >= 0) {
                a.kind = sw ? BotAction::Kind::Swap : BotAction::Kind::Add;
                a.card = t;
                a.meld = m;
                return a;
            }
            const Partition P = bestPartition(h, Goal::Shed, t, true, R.jokerPoints);
            if (!P.melds.empty()) {
                a.kind = BotAction::Kind::Lay;
                a.melds = P.melds;
                return a;
            }
            a.kind = BotAction::Kind::ReturnDiscard;
            return a;
        }
        if (!g.opened(seat)) {
            const bool sees = level != BotLevel::Acemi || rng.uniform() < 0.75f;
            if (sees) {
                BotAction o = openingAction(g, seat, -1);
                if (o.kind == BotAction::Kind::Lay) return o;
            }
            return discardDecision(g, seat);
        }
        // opened: take jokers off the table, lay new melds, add the rest
        if (level != BotLevel::Acemi)
            for (int c : h) {
                if (isJoker(c)) continue;
                for (size_t i = 0; i < g.table().size(); ++i)
                    if (swapIndex(g.table()[i], c) >= 0) {
                        a.kind = BotAction::Kind::Swap;
                        a.card = c;
                        a.meld = (int)i;
                        return a;
                    }
            }
        const bool keep = keepJokers(g, seat);
        {
            Partition P = bestPartition(h, Goal::Shed, -1, !keep, R.jokerPoints);
            int jk = 0;
            for (int c : P.rest) jk += isJoker(c) ? 1 : 0;
            if (jk >= 2 && jk == (int)P.rest.size()) P = bestPartition(h, Goal::Shed, -1, true, R.jokerPoints);
            if (!P.melds.empty() && (level != BotLevel::Acemi || P.melds.size() < 3 || rng.uniform() < 0.8f)) {
                a.kind = BotAction::Kind::Lay;
                a.melds = P.melds;
                return a;
            }
        }
        int jokersInHand = 0;
        for (int c : h) jokersInHand += isJoker(c) ? 1 : 0;
        const bool onlyJokers = jokersInHand == (int)h.size() && h.size() > 1;
        for (int c : h) {
            if (isJoker(c) && keep && !onlyJokers) continue;
            const int m = tableFit(g.table(), c);
            if (m < 0) continue;
            if (!isJoker(c) && swapIndex(g.table()[(size_t)m], c) >= 0) continue; // (swaps above)
            a.kind = BotAction::Kind::Add;
            a.card = c;
            a.meld = m;
            return a;
        }
        return discardDecision(g, seat);
    }

    BotAction discardDecision(const Game& g, int seat) {
        BotAction a;
        a.kind = BotAction::Kind::Discard;
        const std::vector<int> cands = discardable(g, seat);
        if (cands.empty()) return fallbackAction(g, seat);
        if (cands.size() == 1 || g.hand(seat).size() == 1) {
            a.card = cands.front();
            return a;
        }
        if (level == BotLevel::Acemi) {
            const Partition P = bestPartition(g.hand(seat), g.opened(seat) ? Goal::Shed : Goal::Value, -1, false);
            std::vector<int> loose;
            for (int c : P.rest)
                if (std::find(cands.begin(), cands.end(), c) != cands.end()) loose.push_back(c);
            if (loose.empty()) loose = cands;
            std::sort(loose.begin(), loose.end(), [](int x, int y) { return handPoints(x) > handPoints(y) || (handPoints(x) == handPoints(y) && x < y); });
            const int pickI = rng.uniform() < 0.3f ? rng.range((int)loose.size()) : 0;
            a.card = loose[(size_t)pickI];
            return a;
        }
        const std::vector<std::pair<double, int>> ranked = ustaRank(g, seat);
        if (level == BotLevel::Usta || ranked.size() == 1) {
            a.card = ranked.front().second;
            return a;
        }
        // Kurt: rollouts over Usta's best few
        std::vector<int> top;
        for (size_t i = 0; i < ranked.size() && top.size() < 5; ++i) top.push_back(ranked[i].second);
        const std::vector<DiscardValue> vals = rollouts(g, seat, top, 20, stateSalt(g, seat));
        size_t b = 0;
        for (size_t i = 1; i < vals.size(); ++i)
            if (vals[i].value < vals[b].value) b = i;
        a.card = vals[b].card;
        return a;
    }
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(new Impl) {
    impl_->level = level;
    impl_->seed = seed;
    impl_->rng.reseed(seed);
}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::setStyle(BotStyle style) { impl_->style = style; }
BotStyle Bot::style() const { return impl_->style; }

BotAction Bot::next(const Game& g, int seat) {
    if (g.current() != seat) return fallbackAction(g, seat);
    if (g.stage() == Stage::Draw) return impl_->drawDecision(g, seat);
    if (g.stage() == Stage::Play) return impl_->playDecision(g, seat);
    return fallbackAction(g, seat);
}

std::vector<DiscardValue> Bot::evaluateDiscards(const Game& g, int seat) const {
    if (g.stage() != Stage::Play || g.current() != seat || g.takenCard() >= 0) return {};
    const std::vector<int> cands = impl_->discardable(g, seat);
    if (cands.size() < 2) return {};
    // one card per face (two copies are the same choice)
    std::vector<int> faces;
    std::vector<int> uniq;
    for (int c : cands) {
        const int f = isJoker(c) ? JOKER_SLOT : faceOf(c);
        if (std::find(faces.begin(), faces.end(), f) != faces.end()) continue;
        faces.push_back(f);
        uniq.push_back(c);
    }
    Impl k = Impl();
    k.level = BotLevel::Kurt;
    k.seed = impl_->seed;
    return k.rollouts(g, seat, uniq, 32, k.stateSalt(g, seat) ^ 0xA11A11ull);
}

bool Bot::openingNow(const Game& g, int seat, std::vector<std::vector<int>>& melds) const {
    if (g.stage() != Stage::Play || g.opened(seat)) return false;
    Impl k = Impl();
    k.level = BotLevel::Kurt;
    const BotAction a = k.openingAction(g, seat, g.takenCard());
    if (a.kind != BotAction::Kind::Lay) return false;
    melds = a.melds;
    return true;
}

} // namespace konken
