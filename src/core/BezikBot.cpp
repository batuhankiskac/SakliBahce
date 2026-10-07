// Bezik bots (see BezikBot.h).
#include "core/BezikBot.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace bezik {

namespace {

constexpr uint16_t bit(MeldKind k) { return (uint16_t)(1u << (int)k); }

MeldKind fourKind(int rank) {
    switch (rank) {
    case kart::As: return MeldKind::DortAs;
    case kart::Papaz: return MeldKind::DortPapaz;
    case kart::Kiz: return MeldKind::DortKiz;
    default: return MeldKind::DortVale;
    }
}

int brisk(int c) { return isBrisque(c) ? 10 : 0; }

// ---------------------------------------------------------------- the second stage, solved
// Everything is known once the stock is gone: the remaining tricks are a small perfect-information game. Hands are
// masks over the compact index (idxOf); the value is seat 0's points minus seat 1's from here on (brisks, last trick).
struct Solver {
    int trump = 0;
    std::map<std::pair<uint64_t, uint64_t>, int> memo[2];

    static uint64_t m(int c) { return 1ull << idxOf(c); }
    static int cardAt(int i) { return idOf(i); }

    // Legal replies (as masks) to `lead` from `h`.
    uint64_t legal(uint64_t h, int lead) const {
        if (lead < 0) return h;
        const int ls = suitOf(lead);
        uint64_t same = 0, higher = 0, tr = 0;
        for (uint64_t x = h; x; x &= x - 1) {
            const int c = cardAt(__builtin_ctzll(x));
            if (suitOf(c) == ls) {
                same |= m(c);
                if (strength(c) > strength(lead)) higher |= m(c);
            } else if (suitOf(c) == trump) {
                tr |= m(c);
            }
        }
        if (same) return higher ? higher : same;
        if (tr) return tr;
        return h;
    }
    // One card per face (the two copies are the same card here).
    static uint64_t distinctFaces(uint64_t h) {
        uint64_t out = 0;
        for (uint64_t x = h; x; x &= x - 1) {
            const int i = __builtin_ctzll(x);
            const int other = i < 32 ? i + 32 : i - 32;
            if (i >= 32 && (h >> other & 1)) continue; // keep the first copy
            out |= 1ull << i;
        }
        return out;
    }
    // Value (seat 0 minus seat 1) of the rest, `leader` to lead, hands h0 / h1.
    int solve(uint64_t h0, uint64_t h1, int leader) {
        if (!h0 && !h1) return 0;
        const std::pair<uint64_t, uint64_t> key{h0, h1};
        auto it = memo[leader].find(key);
        if (it != memo[leader].end()) return it->second;
        const uint64_t hl = leader == 0 ? h0 : h1;
        int best = leader == 0 ? -100000 : 100000;
        for (uint64_t x = distinctFaces(hl); x; x &= x - 1) {
            const int a = cardAt(__builtin_ctzll(x));
            const int v = reply(h0, h1, leader, a);
            best = leader == 0 ? std::max(best, v) : std::min(best, v);
        }
        memo[leader][key] = best;
        return best;
    }
    // The follower answers `a` (already out of the leader's hand? no: removed here).
    int reply(uint64_t h0, uint64_t h1, int leader, int a) {
        if (leader == 0) h0 &= ~m(a);
        else h1 &= ~m(a);
        const int fol = 1 - leader;
        const uint64_t hf = fol == 0 ? h0 : h1;
        int best = fol == 0 ? -100000 : 100000;
        for (uint64_t y = distinctFaces(legal(hf, a)); y; y &= y - 1) {
            const int b = cardAt(__builtin_ctzll(y));
            const int v = finish(h0, h1, leader, a, b);
            best = fol == 0 ? std::max(best, v) : std::min(best, v);
        }
        return best;
    }
    int finish(uint64_t h0, uint64_t h1, int leader, int a, int b) {
        const int fol = 1 - leader;
        if (fol == 0) h0 &= ~m(b);
        else h1 &= ~m(b);
        const int w = beats(b, a, trump) ? fol : leader;
        int pts = brisk(a) + brisk(b) + ((!h0 && !h1) ? 10 : 0);
        const int v = (w == 0 ? pts : -pts) + solve(h0, h1, w);
        return v;
    }
};

uint64_t idMaskOf(const std::vector<int>& v) { // (bit = idxOf: the 48-card ids)
    uint64_t r = 0;
    for (int c : v) r |= 1ull << idxOf(c);
    return r;
}

} // namespace

// ---------------------------------------------------------------- Usta's card values
namespace {

// What the keep values need about a seat's cards (hand + table), counted once per decision.
struct KeepCtx {
    int trump = 0;
    bool second = false;
    int face[32] = {};        // available cards by face (suit * 8 + rank - 7)
    int fourFree[15] = {};    // by rank: available cards not used in that rank's four of a kind
    int marFree[4][2] = {};   // by suit: Papaz (0) / Kız (1) not used in a marriage
    int seriFaces = 0;        // distinct koz A 10 P K V available, not used in a sequence
};

int faceIdx(int c) { return suitOf(c) * 8 + rankOf(c) - 7; }

KeepCtx keepCtx(const Game& g, int seat) {
    KeepCtx k;
    k.trump = g.trump();
    k.second = g.secondStage();
    if (k.second) return k;
    const uint16_t mb = bit(MeldKind::Evlilik) | bit(MeldKind::KozEvlilik);
    bool seri[15] = {};
    auto add = [&](int c) {
        ++k.face[faceIdx(c)];
        const int r = rankOf(c), s = suitOf(c);
        const uint16_t u = g.usedKinds(c);
        if (r == kart::As || r == kart::Papaz || r == kart::Kiz || r == kart::Vale)
            if (!(u & bit(fourKind(r)))) ++k.fourFree[r];
        if ((r == kart::Papaz || r == kart::Kiz) && !(u & mb)) ++k.marFree[s][r == kart::Papaz ? 0 : 1];
        if (s == k.trump && (r == kart::As || r == 10 || r == kart::Papaz || r == kart::Kiz || r == kart::Vale) &&
            !(u & bit(MeldKind::Seri)))
            seri[r] = true;
    };
    for (int c : g.hand(seat)) add(c);
    for (int c : g.tableCards(seat)) add(c);
    for (bool b : seri) k.seriFaces += b ? 1 : 0;
    return k;
}

double keepValue(const KeepCtx& k, const Game& g, int c) {
    const int s = suitOf(c), r = rankOf(c);
    double v = strength(c) * 1.2;
    if (s == k.trump) v += 8.0 + strength(c);
    if (isBrisque(c)) v += 6.0;
    if (k.second) return v;
    if (s == k.trump && r == 7) return g.sevenScored(c) ? 0.5 : 12.0;
    const uint16_t used = g.usedKinds(c);
    // bezik
    const bool qs = faceOf(c) == BEZIK_QUEEN, jd = faceOf(c) == BEZIK_JACK;
    if ((qs || jd) && !(used & (bit(MeldKind::Bezik) | bit(MeldKind::CiftBezik)))) {
        v += 6.0;
        if (k.face[faceIdx(qs ? BEZIK_JACK : BEZIK_QUEEN)] > 0) v += 6.0;
        if (k.face[faceIdx(c)] >= 2) v += 8.0; // double bezik hope
    }
    // four of a kind
    if (r == kart::As || r == kart::Papaz || r == kart::Kiz || r == kart::Vale) {
        if (!(used & bit(fourKind(r)))) {
            static const double w[5] = {0, 0, 1.0, 3.0, 8.0};
            v += w[std::min(k.fourFree[r], 4)] * (r == kart::As ? 1.5 : 1.0);
        }
    }
    // marriages
    if (r == kart::Papaz || r == kart::Kiz) {
        const uint16_t mb = bit(MeldKind::Evlilik) | bit(MeldKind::KozEvlilik);
        if (!(used & mb)) {
            const bool has = k.marFree[s][r == kart::Papaz ? 1 : 0] > 0;
            v += has ? (s == k.trump ? 10.0 : 5.0) : 1.0;
        }
    }
    // the koz sequence
    if (s == k.trump && (r == kart::As || r == 10 || r == kart::Papaz || r == kart::Kiz || r == kart::Vale) &&
        !(used & bit(MeldKind::Seri)))
        v += k.seriFaces * 2.5;
    return v;
}

} // namespace

double keepValue(const Game& g, int seat, int card) { return keepValue(keepCtx(g, seat), g, card); }

namespace {

// ---------------------------------------------------------------- Usta's policy
int ustaPlay(const Game& g, int seat, double boldness) {
    const std::vector<int> legal = g.legalCards(seat);
    if (legal.empty()) return -1;
    const Trick& t = g.currentTrick();
    const int trump = g.trump();
    if (g.secondStage()) {
        if (t.cards.empty()) {
            // lead: the lowest of the plain suits, keeping trumps and brisks
            int best = legal[0];
            double bv = 1e9;
            for (int c : legal) {
                const double v = strength(c) + (suitOf(c) == trump ? 10.0 : 0.0) + brisk(c) * 0.3;
                if (v < bv) {
                    bv = v;
                    best = c;
                }
            }
            return best;
        }
        const int lead = t.cards[0].card;
        int win = -1, lose = -1;
        double wv = 1e9, lv = 1e9;
        for (int c : legal) {
            const double v = strength(c) + (suitOf(c) == trump ? 10.0 : 0.0) + brisk(c) * 0.3;
            if (beats(c, lead, trump)) {
                if (v < wv) wv = v, win = c;
            } else if (v < lv) {
                lv = v, lose = c;
            }
        }
        return win >= 0 ? win : lose;
    }
    // stage 1
    const KeepCtx kc = keepCtx(g, seat);
    if (t.cards.empty()) {
        int best = legal[0];
        double bv = 1e9;
        for (int c : legal) {
            const double v = keepValue(kc, g, c) + (isBrisque(c) ? 4.0 : 0.0);
            if (v < bv) {
                bv = v;
                best = c;
            }
        }
        return best;
    }
    const int lead = t.cards[0].card;
    const double meldNow = g.bestMeldPoints(seat) * 0.7;
    const double drawBonus = 3.0 + 4.0 * boldness;
    int best = legal[0];
    double bv = -1e9;
    for (int c : legal) {
        const double keep = keepValue(kc, g, c);
        const int b = brisk(lead) + brisk(c);
        double v;
        if (beats(c, lead, trump)) v = b + meldNow + drawBonus - keep;
        else v = -b - keep;
        if (v > bv) {
            bv = v;
            best = c;
        }
    }
    return best;
}

// The declaration for the trick's winner: the koz 7 first, then a combination.
BotAction declareChoice(const Game& g, int seat, BotLevel lv, Rng& rng) {
    BotAction a;
    if (g.canKoz7(seat) && (lv != BotLevel::Acemi || rng.chance(0.7f))) {
        a.kind = BotAction::Kind::Koz7;
        return a;
    }
    std::vector<Meld> ms = g.availableMelds(seat);
    if (ms.empty() || (lv == BotLevel::Acemi && rng.chance(0.2f))) {
        a.kind = BotAction::Kind::Pass;
        return a;
    }
    size_t pick = 0; // the biggest (availableMelds is sorted)
    if (lv == BotLevel::Kurt && g.stockSize() >= 6) {
        // a smaller combination first when its cards make the bigger one later (both score)
        auto find = [&](MeldKind k) {
            for (size_t i = 0; i < ms.size(); ++i)
                if (ms[i].kind == k) return (int)i;
            return -1;
        };
        const int seri = find(MeldKind::Seri), kozE = find(MeldKind::KozEvlilik);
        const int cift = find(MeldKind::CiftBezik), bez = find(MeldKind::Bezik);
        if (cift >= 0 && bez >= 0 && (size_t)cift == pick) pick = (size_t)bez;
        else if (seri >= 0 && kozE >= 0 && (size_t)seri == pick) pick = (size_t)kozE;
    }
    a.kind = BotAction::Kind::Declare;
    a.meld = ms[pick];
    return a;
}

// Plays the deal out with Usta's policy for both (the bots' rollouts).
void rollout(Game& g, Rng& rng) {
    for (int guard = 0; guard < 400; ++guard) {
        const Stage st = g.stage();
        if (st != Stage::Playing && st != Stage::Declare) return;
        const int s = g.current();
        if (st == Stage::Declare) {
            const BotAction a = declareChoice(g, s, BotLevel::Usta, rng);
            if (!applyBotAction(g, s, a).ok) g.pass(s);
            continue;
        }
        const int c = ustaPlay(g, s, 0.0);
        if (c < 0 || !g.playCard(s, c).ok) return;
    }
}

// A deal consistent with what `me` knows: the unseen cards go into the opponent's concealed hand (besides the cards
// everyone saw go there) and the stock. Written into `g` (assigned from `base`: a reused Game keeps its buffers).
void determinize(Game& g, const Game& base, int me, Rng& rng) {
    g = base;
    const int opp = 1 - me;
    std::array<bool, NUM_IDS> seen{};
    auto mark = [&](const std::vector<int>& v) {
        for (int c : v) seen[(size_t)c] = true;
    };
    mark(base.hand(me));
    mark(base.tableCards(me));
    mark(base.tableCards(opp));
    mark(base.won(0));
    mark(base.won(1));
    mark(base.exposed(opp));
    for (const TrickCard& tc : base.currentTrick().cards) seen[(size_t)tc.card] = true;
    if (base.turnUp() >= 0) seen[(size_t)base.turnUp()] = true;
    std::vector<int> pool;
    for (int c : fullDeck())
        if (!seen[(size_t)c]) pool.push_back(c);
    rng.shuffle(pool);
    std::vector<int> oh = base.exposed(opp);
    const size_t need = base.hand(opp).size();
    size_t k = 0;
    while (oh.size() < need && k < pool.size()) oh.push_back(pool[k++]);
    std::vector<int> stock(pool.begin() + (std::ptrdiff_t)k, pool.end());
    stock.resize(std::min(stock.size(), (size_t)base.stockSize()));
    g.debugSetHand(opp, oh);
    g.debugSetStock(stock);
}

// Candidate cards: one per (face, where it lies, how it was used).
std::vector<int> candidates(const Game& g, int seat) {
    std::vector<int> legal = g.legalCards(seat), out;
    const std::vector<int>& table = g.tableCards(seat);
    for (int c : legal) {
        bool dup = false;
        const bool tc = std::find(table.begin(), table.end(), c) != table.end();
        for (int o : out) {
            const bool to = std::find(table.begin(), table.end(), o) != table.end();
            if (sameFace(c, o) && tc == to && g.usedKinds(c) == g.usedKinds(o)) dup = true;
        }
        if (!dup) out.push_back(c);
    }
    return out;
}

} // namespace

ActionResult applyBotAction(Game& g, int seat, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::Play: return g.playCard(seat, a.card);
    case BotAction::Kind::Declare: return g.declare(seat, a.meld);
    case BotAction::Kind::Koz7: return g.koz7(seat);
    case BotAction::Kind::Pass: return g.pass(seat);
    }
    return ActionResult::fail("?");
}

BotAction fallbackAction(const Game& g, int seat) {
    BotAction a;
    if (g.stage() == Stage::Declare) {
        a.kind = BotAction::Kind::Pass;
        return a;
    }
    const std::vector<int> l = g.legalCards(seat);
    a.card = l.empty() ? -1 : l[0];
    return a;
}

struct Bot::Impl {
    BotLevel level = BotLevel::Usta;
    BotStyle style;
    Rng rng;

    std::vector<CardValue> monteCarlo(const Game& g, int seat, int samples, uint64_t seed) const {
        std::vector<int> cands = candidates(g, seat);
        std::vector<CardValue> out;
        for (int c : cands) out.push_back({c, 0.0, 0.0});
        if (cands.size() <= 1) return out;
        Game base = g;
        base.stripForSimulation();
        const int opp = 1 - seat;
        std::vector<std::vector<double>> vals(cands.size());
        Rng r(seed);
        Game d, sim; // (reused across the samples)
        for (int k = 0; k < samples; ++k) {
            determinize(d, base, seat, r);
            const uint64_t rs = r.next();
            for (size_t i = 0; i < cands.size(); ++i) {
                sim = d;
                Rng rr(rs);
                if (!sim.playCard(seat, cands[i]).ok) continue;
                rollout(sim, rr);
                vals[i].push_back((double)(sim.handPoints(seat) - sim.handPoints(opp)));
            }
        }
        for (size_t i = 0; i < cands.size(); ++i) {
            double sum = 0;
            for (double v : vals[i]) sum += v;
            out[i].value = vals[i].empty() ? -1e9 : sum / (double)vals[i].size();
        }
        size_t bi = 0;
        for (size_t i = 1; i < out.size(); ++i)
            if (out[i].value > out[bi].value) bi = i;
        for (size_t i = 0; i < out.size(); ++i) {
            if (i == bi || vals[i].size() != vals[bi].size() || vals[i].size() < 2) continue;
            double m = 0, q = 0;
            const size_t n = vals[i].size();
            for (size_t k = 0; k < n; ++k) m += vals[bi][k] - vals[i][k];
            m /= (double)n;
            for (size_t k = 0; k < n; ++k) {
                const double d = vals[bi][k] - vals[i][k] - m;
                q += d * d;
            }
            out[i].se = std::sqrt(q / (double)(n - 1) / (double)n);
        }
        return out;
    }

    // The second stage: exact values of every legal card (seat's points minus the opponent's, from here).
    std::vector<CardValue> exact(const Game& g, int seat) const {
        Solver sv;
        sv.trump = g.trump();
        // everything not played is in the two hands: the opponent's is what `seat` has not seen
        std::array<bool, NUM_IDS> gone{};
        for (int s = 0; s < 2; ++s)
            for (int c : g.won(s)) gone[(size_t)c] = true;
        for (const TrickCard& tc : g.currentTrick().cards) gone[(size_t)tc.card] = true;
        for (int c : g.hand(seat)) gone[(size_t)c] = true;
        std::vector<int> oppHand;
        for (int c : fullDeck())
            if (!gone[(size_t)c]) oppHand.push_back(c);
        if (oppHand.size() != g.hand(1 - seat).size()) return {}; // (not a whole deal: tests' hand-made ones)
        uint64_t hm[2];
        hm[seat] = idMaskOf(g.hand(seat));
        hm[1 - seat] = idMaskOf(oppHand);
        const Trick& t = g.currentTrick();
        std::vector<CardValue> out;
        for (int c : candidates(g, seat)) {
            int v0;
            if (t.cards.empty()) {
                v0 = sv.reply(hm[0], hm[1], seat, c);
            } else {
                uint64_t h0 = hm[0], h1 = hm[1];
                v0 = sv.finish(h0, h1, t.cards[0].seat, t.cards[0].card, c);
            }
            out.push_back({c, (double)(seat == 0 ? v0 : -v0), 0.0});
        }
        return out;
    }
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(std::make_unique<Impl>()) {
    impl_->level = level;
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
    Impl& m = *impl_;
    if (g.stage() == Stage::Declare) return declareChoice(g, seat, m.level, m.rng);
    BotAction a;
    a.kind = BotAction::Kind::Play;
    const std::vector<int> legal = g.legalCards(seat);
    if (legal.empty()) return fallbackAction(g, seat);
    const double bold = m.style.boldness;
    switch (m.level) {
    case BotLevel::Acemi:
        a.card = m.rng.chance(0.4f) ? legal[(size_t)m.rng.range((int)legal.size())] : ustaPlay(g, seat, bold);
        break;
    case BotLevel::Usta: a.card = ustaPlay(g, seat, bold); break;
    case BotLevel::Kurt: {
        std::vector<CardValue> v;
        if (g.secondStage()) {
            v = m.exact(g, seat);
        } else {
            const int n = (int)candidates(g, seat).size();
            const int samples = std::clamp(1200 / std::max(1, n), 40, 150);
            v = m.monteCarlo(g, seat, samples, m.rng.next());
        }
        if (v.empty()) {
            a.card = ustaPlay(g, seat, bold);
            break;
        }
        // a bold Kurt leans to the card that wins the trick, a careful one to the one that keeps its hand, when the
        // values are close
        size_t bi = 0;
        auto score = [&](const CardValue& cv) {
            double s = cv.value;
            if (bold != 0.0 && !g.currentTrick().cards.empty())
                s += bold * (beats(cv.card, g.currentTrick().cards[0].card, g.trump()) ? 1.5 : 0.0);
            return s;
        };
        for (size_t i = 1; i < v.size(); ++i)
            if (score(v[i]) > score(v[bi])) bi = i;
        a.card = v[bi].card;
        break;
    }
    }
    if (std::find(legal.begin(), legal.end(), a.card) == legal.end()) a.card = legal[0];
    return a;
}

std::vector<CardValue> Bot::evaluateCards(const Game& g, int seat, int samples) const {
    if (g.stage() != Stage::Playing || g.current() != seat) return {};
    if (g.secondStage()) return impl_->exact(g, seat);
    uint64_t seed = 0x5EED ^ (uint64_t)g.trickNumber() * 7919u ^ (uint64_t)g.handIndex() * 104729u;
    for (int c : g.hand(seat)) seed = seed * 31 + (uint64_t)c;
    return impl_->monteCarlo(g, seat, samples, seed);
}

} // namespace bezik
