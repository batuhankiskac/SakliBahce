// Computer opponents for 101 Okey (Acemi / Usta / Kurt).
//
// A bot plans its whole turn once (draw decision, then opening / table work / discard), emits the steps one
// by one and re-plans whenever the game state is not the one the plan expected. Every step is checked
// against the same rules the engine applies before it is emitted, so bots never produce rejected actions.
// Bots only read public information (table, discard piles, history, pile size, opened flags, hand sizes)
// and their own hand.
//
// Levels share the rules machinery and differ in judgement:
// * Acemi opens with greedily found melds, rates tiles as if nothing were gone, discards loosely
//   (sometimes the second or third choice, now and then even an işlek tile) and does not plan finishes.
// * Usta solves its hand exactly, keeps tiles by their live potential (table and top discards counted),
//   avoids penalties, swaps okeys, finishes whenever it can and times pair openings with fixed rules.
// * Kurt additionally remembers every discard (dead tiles, what its right neighbour picked up or threw),
//   keeps a lone okey for an okeyle bitiş, and chooses its discards by rollouts: it samples its future pile
//   draws and left tiles from the unseen tiles and plays each candidate discard to the end of the hand
//   (open at the first chance, lay/işle everything, shed the most expensive tile, opponents ending the hand
//   with a hazard that grows as their racks shrink). Whether to open with pairs now or keep the hand for a
//   series is decided the same way. The look-ahead is budgeted in solver work, so decisions stay
//   deterministic for a seed and take a few milliseconds.
#include "core/Bot.h"
#include "core/Solver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace okey {

namespace detail {
SolveResult solveSeriesObjective(const std::vector<int>& tiles, const OkeyInfo& ok, int mustUse, int objective);
uint64_t solverWork();
} // namespace detail

namespace {

constexpr int OBJ_VALUE = 0; // max meld value
constexpr int OBJ_TILES = 1; // max tiles laid
constexpr int OBJ_HAND = 2;  // max hand points removed (okey = 101)

// Kurt's look-ahead. The budget is counted in solver work (search states), which keeps decisions
// deterministic and independent of machine speed; Kurt's heaviest decisions stay around 10 ms on one core
// of the reference Mac (larger budgets were not measurably stronger).
constexpr uint64_t LOOKAHEAD_BUDGET = 200000;
constexpr int UNOPENED_CANDIDATES = 12; // most discardable distinct faces evaluated before opening
constexpr int UNOPENED_SAMPLES = 96;    // sampled futures (my pile draws + left tiles) before opening
constexpr int OPENED_SAMPLES = 64;      // ... after opening
constexpr double DANGER_WEIGHT = 0.3;   // heuristic keep/danger difference added to rollout scores
constexpr double ISLEK_KEEP = 5000.0;   // keep-score added to işlek tiles (discarding one costs 101)

using Groups = std::vector<std::vector<int>>;

bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool removeOne(std::vector<int>& v, int x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it == v.end()) return false;
    v.erase(it);
    return true;
}

std::vector<int> without(const std::vector<int>& v, int x) {
    std::vector<int> r = v;
    removeOne(r, x);
    return r;
}

int groupsTileCount(const Groups& g) {
    int n = 0;
    for (const auto& m : g) n += (int)m.size();
    return n;
}

bool groupsContain(const Groups& g, int x) {
    for (const auto& m : g)
        if (has(m, x)) return true;
    return false;
}

uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    return h;
}

// Signature of the parts of the state a bot's plan depends on.
uint64_t signature(int handIndex, int turn, TurnStage stage, int pending, std::vector<int> hand,
                   const std::vector<Meld>& table) {
    std::sort(hand.begin(), hand.end());
    uint64_t h = 0x1234567ull;
    h = mix64(h, (uint64_t)handIndex);
    h = mix64(h, (uint64_t)turn);
    h = mix64(h, (uint64_t)(stage == TurnStage::Play));
    h = mix64(h, (uint64_t)(pending + 1));
    for (int id : hand) h = mix64(h, (uint64_t)id + 7);
    h = mix64(h, 0xABCDull + table.size());
    for (const Meld& m : table) h = mix64(h, (uint64_t)m.tiles.size());
    return h;
}

// ---------------------------------------------------------------------------------------------------------
// The bot's own view of its turn: its hand plus the public table.

struct Model {
    int seat = 0;
    int handIndex = 0;
    int turn = 0;
    TurnStage stage = TurnStage::Play;
    std::vector<int> hand;
    std::vector<Meld> table;
    bool opened = false;
    bool pairsOpener = false;
    bool pairsOnTable = false;  // someone else opened with pairs: a series opener may lay pairs too
    bool canWork = false;
    int pending = -1;
    uint64_t sig() const { return signature(handIndex, turn, stage, pending, hand, table); }
};

// Public knowledge used for liveness and neighbour modelling.
struct Knowledge {
    int visible[NUM_COLORS][NUM_NUMBERS + 1] = {}; // non-wild faces seen outside my hand
    int mine[NUM_COLORS][NUM_NUMBERS + 1] = {};
    int rTook[NUM_COLORS][NUM_NUMBERS + 1] = {};      // faces my right neighbour took from my discards
    int rDiscarded[NUM_COLORS][NUM_NUMBERS + 1] = {}; // faces my right neighbour discarded
    int right = 0;
    bool rOpened = false, rPairs = false, rCanSwapWork = false;
    int rHand = 21;
    int minOpenedOppHand = 99; // smallest hand among opponents who opened
    int anyOppHandMin = 99;
    int visibleJokers = 0, myJokers = 0;
    std::vector<int> unseen; // Kurt: physical tiles that may still come from the pile

    int live(int c, int n) const {
        if (n < 1 || n > NUM_NUMBERS) return 0;
        return std::max(0, 2 - visible[c][n] - mine[c][n]);
    }
};

struct Opening {
    bool valid = false;
    bool pairs = false;
    Groups melds;
    int finishTile = -1; // discard that finishes the hand right after opening
};

} // namespace

// ---------------------------------------------------------------------------------------------------------

struct Bot::Impl {
    BotLevel level = BotLevel::Normal;
    uint64_t seed = 1;

    struct Step {
        BotAction act;
        uint64_t sig = 0;
    };
    std::vector<Step> plan;
    size_t planPos = 0;
    bool haveLast = false;
    uint64_t lastSig = 0;
    BotAction lastAct;

    // Per-plan context
    int seatNow = 0;
    const Game* g = nullptr;
    const OkeyInfo* ok = nullptr;
    Knowledge know;
    uint64_t workStart = 0; // solver work counter at the start of the current plan

    // Solver work spent on the current plan (deterministic, machine independent).
    uint64_t workUsed() const { return detail::solverWork() - workStart; }

    bool easy() const { return level == BotLevel::Easy; }
    bool hard() const { return level == BotLevel::Hard; }

    Rng turnRng(const Game& game, int seat, uint64_t salt) const {
        uint64_t h = mix64(seed, (uint64_t)game.handIndex());
        h = mix64(h, (uint64_t)game.turnNumber());
        h = mix64(h, (uint64_t)seat);
        h = mix64(h, salt);
        return Rng(h);
    }

    // ---- setup ----
    Model modelOf(const Game& game, int seat) const {
        Model m;
        const PlayerInfo& p = game.player(seat);
        m.seat = seat;
        m.handIndex = game.handIndex();
        m.turn = game.turnNumber();
        m.stage = game.stage();
        m.hand = p.hand;
        m.table = game.table();
        m.opened = p.opened;
        m.pairsOpener = p.openedWithPairs;
        m.pairsOnTable = game.pairsOpenedByOther(seat);
        m.canWork = game.canWorkTable(seat);
        m.pending = game.pendingLeftTile();
        return m;
    }

    void buildKnowledge(const Game& game, int seat) {
        know = Knowledge();
        const OkeyInfo& o = game.okey();
        auto see = [&](int id, int (&arr)[NUM_COLORS][NUM_NUMBERS + 1]) {
            if (!isValidTile(id)) return;
            if (o.isJoker(id)) {
                if (&arr == &know.visible) ++know.visibleJokers;
                else if (&arr == &know.mine) ++know.myJokers;
                return;
            }
            arr[o.faceColor(id)][o.faceNumber(id)]++;
        };
        for (int id : game.player(seat).hand) see(id, know.mine);
        see(o.indicatorId, know.visible);
        for (const Meld& m : game.table())
            for (const PlacedTile& t : m.tiles) see(t.id, know.visible);
        for (int s = 0; s < NUM_PLAYERS; ++s) {
            const std::vector<int>& d = game.player(s).discards;
            if (hard()) {
                for (int id : d) see(id, know.visible);
            } else if (!d.empty()) {
                see(d.back(), know.visible); // only the top tile is in plain sight
            }
        }
        know.right = Game::rightOf(seat);
        const PlayerInfo& r = game.player(know.right);
        know.rOpened = r.opened;
        know.rPairs = r.openedWithPairs;
        know.rHand = (int)r.hand.size(); // public: rack sizes are visible
        for (int s = 0; s < NUM_PLAYERS; ++s) {
            if (s == seat) continue;
            const PlayerInfo& q = game.player(s);
            know.anyOppHandMin = std::min(know.anyOppHandMin, (int)q.hand.size());
            if (q.opened) know.minOpenedOppHand = std::min(know.minOpenedOppHand, (int)q.hand.size());
        }
        if (hard()) {
            // Tiles I discarded that are no longer on my pile were taken by my right neighbour.
            std::vector<int> pile = game.player(seat).discards;
            for (const DiscardRecord& rec : game.discardHistory()) {
                if (rec.player != seat) continue;
                if (!removeOne(pile, rec.tile)) see(rec.tile, know.rTook);
            }
            for (int id : r.discards) see(id, know.rDiscarded);
            // Every tile ever discarded is in a discard pile, on the table or in the hand of whoever took it.
            std::array<char, NUM_TILES> gone{};
            for (int id : game.player(seat).hand) gone[id] = 1;
            gone[o.indicatorId] = 1;
            for (const Meld& m : game.table())
                for (const PlacedTile& t : m.tiles) gone[t.id] = 1;
            for (const DiscardRecord& rec : game.discardHistory()) gone[rec.tile] = 1;
            for (int id = 0; id < NUM_TILES; ++id)
                if (!gone[id]) know.unseen.push_back(id);
        }
    }

    // ---- solver helpers ----
    SolveResult solve(const std::vector<int>& tiles, int must, int objective) const {
        return detail::solveSeriesObjective(tiles, *ok, must, objective);
    }

    int meldValue(const std::vector<int>& ids) const {
        Meld m;
        return makeMeld(ids, *ok, m, false) ? m.value() : 0;
    }
    int groupsValue(const Groups& gr) const {
        int v = 0;
        for (const auto& m : gr) v += meldValue(m);
        return v;
    }
    int handPts(const std::vector<int>& tiles) const {
        int s = 0;
        for (int id : tiles) s += ok->handValue(id);
        return s;
    }

    // Easy bots look for melds greedily (no okey planning) when deciding to open on their own.
    SolveResult greedyPartition(const std::vector<int>& hand) const {
        SolveResult r;
        std::vector<int> rest = hand;
        // runs without okeys: for each colour, longest consecutive stretches
        for (int c = 0; c < NUM_COLORS; ++c) {
            for (;;) {
                int bestLo = 0, bestLen = 0;
                for (int lo = 1; lo <= NUM_NUMBERS; ++lo) {
                    int len = 0;
                    for (int n = lo; n <= NUM_NUMBERS; ++n) {
                        bool found = false;
                        for (int id : rest)
                            if (!ok->isJoker(id) && ok->faceColor(id) == c && ok->faceNumber(id) == n) found = true;
                        if (!found) break;
                        ++len;
                    }
                    if (len > bestLen) {
                        bestLen = len;
                        bestLo = lo;
                    }
                }
                if (bestLen < 3) break;
                std::vector<int> meld;
                for (int n = bestLo; n < bestLo + bestLen; ++n) {
                    for (int id : rest) {
                        if (!ok->isJoker(id) && ok->faceColor(id) == c && ok->faceNumber(id) == n) {
                            meld.push_back(id);
                            break;
                        }
                    }
                }
                for (int id : meld) removeOne(rest, id);
                r.melds.push_back(meld);
            }
        }
        // groups
        for (int n = NUM_NUMBERS; n >= 1; --n) {
            for (int rep = 0; rep < 2; ++rep) {
                std::vector<int> meld;
                bool used[NUM_COLORS] = {};
                for (int id : rest) {
                    if (ok->isJoker(id) || ok->faceNumber(id) != n) continue;
                    const int c = ok->faceColor(id);
                    if (!used[c]) {
                        used[c] = true;
                        meld.push_back(id);
                    }
                }
                if (meld.size() < 3) break;
                for (int id : meld) removeOne(rest, id);
                r.melds.push_back(meld);
            }
        }
        // okeys extend the first run they fit
        for (int id : std::vector<int>(rest)) {
            if (!ok->isJoker(id)) continue;
            for (auto& m : r.melds) {
                std::vector<int> t = m;
                t.push_back(id);
                Meld mm;
                if (makeMeld(t, *ok, mm, false)) {
                    m = mm.ids();
                    removeOne(rest, id);
                    break;
                }
            }
        }
        for (const auto& m : r.melds) {
            r.value += meldValue(m);
            r.tilesUsed += (int)m.size();
        }
        r.leftovers = rest;
        return r;
    }

    // ---- model updates (mirror Game's actions) ----
    bool applyToModel(Model& m, const BotAction& a) const {
        switch (a.kind) {
        case BotAction::Kind::Open:
        case BotAction::Kind::LayMelds: {
            const bool pairs = !a.melds.empty() && a.melds.front().size() == 2;
            for (const auto& ids : a.melds) {
                Meld mm;
                if (!makeMeld(ids, *ok, mm, pairs)) return false;
                mm.owner = m.seat;
                for (int id : ids) {
                    removeOne(m.hand, id);
                    if (id == m.pending) m.pending = -1;
                }
                m.table.push_back(mm);
            }
            if (a.kind == BotAction::Kind::Open) {
                m.opened = true;
                m.pairsOpener = pairs;
                m.canWork = !g->rules().waitTurnAfterOpening;
            }
            return true;
        }
        case BotAction::Kind::AddToMeld: {
            Meld out;
            if (a.meld < 0 || a.meld >= (int)m.table.size()) return false;
            if (!tryAddTile(m.table[a.meld], a.tile, *ok, a.side, out)) return false;
            m.table[a.meld] = out;
            removeOne(m.hand, a.tile);
            if (a.tile == m.pending) m.pending = -1;
            return true;
        }
        case BotAction::Kind::SwapJoker: {
            Meld out;
            int freed = -1;
            if (a.meld < 0 || a.meld >= (int)m.table.size()) return false;
            if (!trySwapJoker(m.table[a.meld], a.tile, *ok, out, freed)) return false;
            m.table[a.meld] = out;
            removeOne(m.hand, a.tile);
            m.hand.push_back(freed);
            if (a.tile == m.pending) m.pending = -1;
            return true;
        }
        case BotAction::Kind::ReturnLeft:
            removeOne(m.hand, m.pending);
            m.pending = -1;
            m.stage = TurnStage::NeedDraw;
            return true;
        case BotAction::Kind::Discard:
            removeOne(m.hand, a.tile);
            return true;
        default: return true;
        }
    }

    // Legality of laying `melds` from the model (mirrors Game::evaluate for layMelds / openHand).
    bool layLegal(const Model& m, const Groups& melds, bool opening) const {
        if (melds.empty()) return false;
        const bool pairs = melds.front().size() == 2;
        std::vector<int> seen;
        int value = 0;
        bool usesPending = false;
        for (const auto& ids : melds) {
            if ((ids.size() == 2) != pairs) return false;
            Meld mm;
            if (!makeMeld(ids, *ok, mm, pairs)) return false;
            value += mm.value();
            for (int id : ids) {
                if (!has(m.hand, id) || has(seen, id)) return false;
                seen.push_back(id);
                if (id == m.pending) usesPending = true;
            }
        }
        if (opening) {
            if (pairs ? (int)melds.size() < g->pairsOpenNeed() : value < g->seriesOpenNeed()) return false;
            if (m.pending >= 0 && !usesPending) return false;
        } else {
            if (pairs != m.pairsOpener && !(pairs && m.pairsOnTable)) return false;
            if (!m.canWork) return false;
        }
        const size_t used = seen.size();
        if (used >= m.hand.size()) return false;
        if (m.pending >= 0 && !usesPending && used + 2 > m.hand.size()) return false;
        return true;
    }

    bool addLegal(const Model& m, int tile, int meldIdx, AddSide side) const {
        if (!m.canWork || meldIdx < 0 || meldIdx >= (int)m.table.size() || !has(m.hand, tile)) return false;
        if (m.table[meldIdx].kind == MeldKind::Pair) return false;
        Meld out;
        if (!tryAddTile(m.table[meldIdx], tile, *ok, side, out)) return false;
        if (m.hand.size() <= 1) return false;
        if (m.pending >= 0 && tile != m.pending && m.hand.size() <= 2) return false;
        return true;
    }

    bool swapLegal(const Model& m, int tile, int meldIdx) const {
        if (!m.canWork || meldIdx < 0 || meldIdx >= (int)m.table.size() || !has(m.hand, tile)) return false;
        if (ok->isJoker(tile)) return false;
        const Meld& t = m.table[meldIdx];
        if (t.kind == MeldKind::Pair || !t.hasJoker()) return false;
        Meld out;
        int freed = -1;
        return trySwapJoker(t, tile, *ok, out, freed);
    }

    // ---- trimming: an opening/lay must leave at least one tile in hand ----
    // Removes one tile from a meld (keeping it valid) or a whole meld so that at least one tile stays in
    // hand; `must` stays laid; `minValue` (series) / `minCount` (pairs) must still be met.
    bool trimToKeepOne(Groups& melds, const std::vector<int>& hand, int must, bool pairs, int minValue,
                       int minCount, int* keptTile) const {
        if (groupsTileCount(melds) < (int)hand.size()) return true;
        if (!pairs) {
            const int total = groupsValue(melds);
            // single tile off an end of a long run / out of a 4-group; prefer keeping an okey back
            int bestMeld = -1, bestPos = -1, bestScore = -1000000;
            for (size_t i = 0; i < melds.size(); ++i) {
                const auto& ids = melds[i];
                if (ids.size() < 4) continue;
                const int v = meldValue(ids);
                for (size_t p = 0; p < ids.size(); ++p) {
                    if (ids[p] == must) continue;
                    std::vector<int> rest = ids;
                    rest.erase(rest.begin() + (long)p);
                    Meld mm;
                    if (!makeMeld(rest, *ok, mm, false)) continue;
                    const int nv = total - v + mm.value();
                    if (nv < minValue) continue;
                    const int score = (ok->isJoker(ids[p]) ? 1000 : 0) + nv;
                    if (score > bestScore) {
                        bestScore = score;
                        bestMeld = (int)i;
                        bestPos = (int)p;
                    }
                }
            }
            if (bestMeld >= 0) {
                auto& ids = melds[bestMeld];
                if (keptTile) *keptTile = ids[bestPos];
                ids.erase(ids.begin() + bestPos);
                Meld mm;
                makeMeld(ids, *ok, mm, false);
                if (mm.kind == MeldKind::Run) ids = mm.ids();
                return true;
            }
            // drop the cheapest whole meld that does not hold `must`
            int drop = -1, dropValue = 1000000;
            for (size_t i = 0; i < melds.size(); ++i) {
                if (has(melds[i], must)) continue;
                const int v = meldValue(melds[i]);
                if (total - v >= minValue && v < dropValue) {
                    dropValue = v;
                    drop = (int)i;
                }
            }
            if (drop < 0) return false;
            melds.erase(melds.begin() + drop);
            return true;
        }
        // pairs: drop the lowest pair that does not hold `must`
        int drop = -1, dropPts = 1000000;
        for (size_t i = 0; i < melds.size(); ++i) {
            if (has(melds[i], must)) continue;
            const int pts = handPts(melds[i]);
            if (pts < dropPts) {
                dropPts = pts;
                drop = (int)i;
            }
        }
        if (drop < 0 || (int)melds.size() - 1 < minCount) return false;
        melds.erase(melds.begin() + drop);
        return true;
    }

    // ---- opening ----
    bool wantPairs(const SolveResult& pr, int seriesValue, bool forcedByPending) const {
        const RulesConfig& rc = g->rules();
        if (pr.value < g->pairsOpenNeed()) return false;
        if (forcedByPending) return true;
        const int pile = g->pileCount();
        std::vector<int> rest = pr.leftovers;
        const int restPts = handPts(rest);
        const int pc = pr.value;
        // Late in the hand anything that beats the unopened score is worth it.
        if (pile <= 3) return 2 * restPts < rc.unopenedScore + 40;
        switch (level) {
        case BotLevel::Easy: return pc >= g->pairsOpenNeed() + 1 || pile <= 10;
        case BotLevel::Normal: return pc >= g->pairsOpenNeed() + 1 || seriesValue < g->seriesOpenNeed() - 30 || pile <= 14;
        case BotLevel::Hard: {
            // With the wait-a-turn rule Kurt rolls both futures out in makePlan (open now or keep the hand).
            if (g->rules().waitTurnAfterOpening) return true;
            if (pc >= g->pairsOpenNeed() + 1) return true;
            // Doubled points of what stays in hand against the chance of a series opening soon.
            const bool seriesClose = seriesValue >= g->seriesOpenNeed() - 22 && pile >= 14;
            if (seriesClose) return false;
            return 2 * restPts < rc.unopenedScore + 20 || pile <= 16;
        }
        }
        return false;
    }

    Opening chooseOpening(const Model& m, bool forPending) const {
        Opening op;
        const int thr = g->seriesOpenNeed();
        const int P = m.pending;
        const std::vector<int>& H = m.hand;

        // 1. elden: lay everything but one tile and finish right away (prefer finishing with the okey).
        if (!easy() || P >= 0) {
            SolveResult rt = solve(H, P, OBJ_TILES);
            if (rt.feasible && rt.tilesUsed + 1 >= (int)H.size() && rt.value >= thr) {
                std::vector<int> cands;
                for (int id : H)
                    if (ok->isJoker(id) && id != P) cands.push_back(id);
                std::vector<int> others;
                for (int id : H)
                    if (!ok->isJoker(id) && id != P) others.push_back(id);
                std::sort(others.begin(), others.end(),
                          [&](int a, int b) {
                              if (ok->handValue(a) != ok->handValue(b)) return ok->handValue(a) > ok->handValue(b);
                              return a < b;
                          });
                cands.insert(cands.end(), others.begin(), others.end());
                std::vector<std::pair<int, int>> triedFaces;
                for (int t : cands) {
                    const std::pair<int, int> face =
                        ok->isJoker(t) ? std::make_pair(-1, -1) : std::make_pair(ok->faceColor(t), ok->faceNumber(t));
                    if (std::find(triedFaces.begin(), triedFaces.end(), face) != triedFaces.end()) continue;
                    triedFaces.push_back(face);
                    const std::vector<int> rest = without(H, t);
                    SolveResult r2 = solve(rest, P, OBJ_TILES);
                    if (r2.feasible && r2.tilesUsed == (int)rest.size() && r2.value >= thr) {
                        op.valid = true;
                        op.melds = r2.melds;
                        op.finishTile = t;
                        return op;
                    }
                }
            }
        }

        // 2. series
        SolveResult rv = (easy() && P < 0 && !forPending) ? greedyPartition(H) : solve(H, P, OBJ_VALUE);
        if (rv.feasible && rv.value >= thr) {
            SolveResult rh = (easy() && P < 0) ? rv : solve(H, P, OBJ_HAND);
            const SolveResult& r = (rh.feasible && rh.value >= thr) ? rh : rv;
            Groups melds = r.melds;
            int kept = -1;
            if (trimToKeepOne(melds, H, P, false, thr, 0, &kept)) {
                Model probe = m;
                if (layLegal(probe, melds, true)) {
                    op.valid = true;
                    op.melds = melds;
                    if (groupsTileCount(melds) + 1 == (int)H.size() && kept >= 0) op.finishTile = kept;
                    return op;
                }
            }
        }

        // 3. pairs
        SolveResult pr = solvePairs(H, *ok, P);
        const int seriesValue = rv.feasible ? rv.value : 0;
        if (pr.feasible && pr.value >= g->pairsOpenNeed() && wantPairs(pr, seriesValue, P >= 0)) {
            Groups melds = pr.melds;
            if (trimToKeepOne(melds, H, P, true, 0, g->pairsOpenNeed(), nullptr) && layLegal(m, melds, true)) {
                op.valid = true;
                op.pairs = true;
                op.melds = melds;
                return op;
            }
        }
        return op;
    }

    // Whether the hand left after `op` still has a discard that costs no penalty (or finishes).
    bool openingLeavesSafeDiscard(const Model& m, const Opening& op) const {
        const RulesConfig& rc = g->rules();
        if (op.finishTile >= 0 || !rc.waitTurnAfterOpening) return true;
        std::vector<int> rest = m.hand;
        std::vector<Meld> table = m.table;
        for (const auto& ids : op.melds) {
            for (int id : ids) removeOne(rest, id);
            Meld mm;
            if (makeMeld(ids, *ok, mm, op.pairs)) table.push_back(mm);
        }
        if (rest.size() <= 1) return true;
        for (int id : rest) {
            if (ok->isJoker(id) && rc.penaltyJokerDiscard) continue;
            if (!rc.penaltyPlayableDiscard || ok->isJoker(id) || !fitsAnyMeld(table, id, *ok)) return true;
        }
        return false;
    }

    // When opening now would force a penalty discard, a discard that keeps the opening intact for the
    // next turn without a penalty (-1 if there is none).
    int deferralDiscard(const Model& m, const Opening& op) const {
        const RulesConfig& rc = g->rules();
        int best = -1, bestValue = -1;
        for (int id : m.hand) {
            if (ok->isJoker(id) || id == m.pending) continue;
            if (rc.penaltyPlayableDiscard && fitsAnyMeld(m.table, id, *ok)) continue;
            const std::vector<int> rest = without(m.hand, id);
            int v;
            if (op.pairs) {
                v = solvePairs(rest, *ok).value;
                if (v < g->pairsOpenNeed()) continue;
            } else {
                v = solve(rest, -1, OBJ_VALUE).value;
                if (v < g->seriesOpenNeed()) continue;
            }
            if (v > bestValue || (v == bestValue && ok->handValue(id) < ok->handValue(best))) {
                bestValue = v;
                best = id;
            }
        }
        return best;
    }

    // ---- table work after opening ----
    using Emit = std::function<void(const BotAction&)>;

    static BotAction act(BotAction::Kind k) {
        BotAction a;
        a.kind = k;
        return a;
    }
    static BotAction layAct(const Groups& melds) {
        BotAction a = act(BotAction::Kind::LayMelds);
        a.melds = melds;
        return a;
    }
    static BotAction addAct(int tile, int meld, AddSide side = AddSide::Auto) {
        BotAction a = act(BotAction::Kind::AddToMeld);
        a.tile = tile;
        a.meld = meld;
        a.side = side;
        return a;
    }
    static BotAction swapAct(int tile, int meld) {
        BotAction a = act(BotAction::Kind::SwapJoker);
        a.tile = tile;
        a.meld = meld;
        return a;
    }

    // How attractive it is to leave an okey at this spot (fewer live copies of the tile it stands for =
    // fewer opponents able to swap it out).
    int jokerSpotSafety(const Meld& before, const Meld& after) const {
        // find the new joker's represented face
        for (const PlacedTile& t : after.tiles) {
            bool isNew = true;
            for (const PlacedTile& u : before.tiles)
                if (u.id == t.id) isNew = false;
            if (!isNew) continue;
            if (after.kind == MeldKind::Group) {
                int missingLive = 0;
                for (int c = 0; c < NUM_COLORS; ++c) {
                    bool present = false;
                    for (const PlacedTile& u : after.tiles)
                        if (!u.joker && u.color == c) present = true;
                    if (!present) missingLive += know.live(c, t.number);
                }
                return -missingLive;
            }
            return -know.live(t.color, t.number);
        }
        return 0;
    }

    // Finds a table spot for `tile` (işleme). Returns meld index or -1; `side` receives the side.
    int findAddSpot(const Model& m, int tile, AddSide& side) const {
        int best = -1, bestScore = -1000000;
        for (int i = 0; i < (int)m.table.size(); ++i) {
            if (m.table[i].kind == MeldKind::Pair) continue;
            for (AddSide s : {AddSide::Back, AddSide::Front}) {
                if (m.table[i].kind == MeldKind::Group && s == AddSide::Front) continue;
                Meld out;
                if (!tryAddTile(m.table[i], tile, *ok, s, out)) continue;
                int score = 0;
                if (ok->isJoker(tile) && !easy()) score = jokerSpotSafety(m.table[i], out) * 10;
                score -= i; // stable preference
                if (score > bestScore) {
                    bestScore = score;
                    best = i;
                    side = s;
                }
            }
        }
        return best;
    }

    bool usePending(Model& m, const Emit& emit) const {
        const int L = m.pending;
        if (L < 0) return true;
        if (!m.pairsOpener) {
            SolveResult r = solve(m.hand, L, OBJ_HAND);
            if (r.feasible && !r.melds.empty()) {
                Groups melds = r.melds;
                if (trimToKeepOne(melds, m.hand, L, false, 0, 0, nullptr) && groupsContain(melds, L) &&
                    layLegal(m, melds, false)) {
                    BotAction a = layAct(melds);
                    emit(a);
                    applyToModel(m, a);
                    return true;
                }
            }
        } else if (!ok->isJoker(L)) {
            for (int id : m.hand) {
                if (id == L || ok->isJoker(id)) continue;
                if (ok->faceColor(id) == ok->faceColor(L) && ok->faceNumber(id) == ok->faceNumber(L)) {
                    Groups melds = {{L, id}};
                    if (layLegal(m, melds, false)) {
                        BotAction a = layAct(melds);
                        emit(a);
                        applyToModel(m, a);
                        return true;
                    }
                }
            }
        }
        AddSide side = AddSide::Auto;
        const int spot = findAddSpot(m, L, side);
        if (spot >= 0 && addLegal(m, L, spot, side)) {
            BotAction a = addAct(L, spot, side);
            emit(a);
            applyToModel(m, a);
            return true;
        }
        for (int i = 0; i < (int)m.table.size(); ++i) {
            if (swapLegal(m, L, i)) {
                BotAction a = swapAct(L, i);
                emit(a);
                applyToModel(m, a);
                return true;
            }
        }
        if (m.pairsOpener) {
            // pair it with an okey as the last resort
            for (int id : m.hand) {
                if (id == L || !ok->isJoker(id)) continue;
                Groups melds = {{L, id}};
                if (layLegal(m, melds, false)) {
                    BotAction a = layAct(melds);
                    emit(a);
                    applyToModel(m, a);
                    return true;
                }
            }
        }
        return false;
    }

    bool swapOne(Model& m, const Emit& emit) const {
        for (int i = 0; i < (int)m.table.size(); ++i) {
            if (!m.table[i].hasJoker() || m.table[i].kind == MeldKind::Pair) continue;
            for (int id : m.hand) {
                if (ok->isJoker(id)) continue;
                if (swapLegal(m, id, i)) {
                    BotAction a = swapAct(id, i);
                    emit(a);
                    applyToModel(m, a);
                    return true;
                }
            }
        }
        return false;
    }

    // A series opener with pairs on the table lays its (okey-free) pairs too: every pair out of the hand.
    bool layPairsToo(Model& m, const Emit& emit) const {
        if (m.pairsOpener || !m.pairsOnTable || m.hand.size() < 3) return false;
        Groups pairs;
        std::vector<int> pool;
        for (int id : m.hand)
            if (!ok->isJoker(id)) pool.push_back(id);
        for (const auto& pr : solvePairs(pool, *ok).melds)
            if (pr.size() == 2) pairs.push_back(pr);
        if (pairs.empty()) return false;
        if (!trimToKeepOne(pairs, m.hand, -1, true, 0, 1, nullptr) || pairs.empty() || !layLegal(m, pairs, false))
            return false;
        BotAction a = layAct(pairs);
        emit(a);
        applyToModel(m, a);
        return true;
    }

    bool layBest(Model& m, const Emit& emit) const {
        Groups melds;
        std::vector<int> pool = m.hand;
        if (holdJoker(m))
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&](int id) { return ok->isJoker(id); }), pool.end());
        if (!m.pairsOpener) {
            SolveResult r = solve(pool, -1, OBJ_HAND);
            melds = r.melds;
        } else {
            SolveResult r = solvePairs(pool, *ok);
            melds = r.melds;
        }
        if (melds.empty()) return false;
        if (!trimToKeepOne(melds, m.hand, -1, m.pairsOpener, 0, 1, nullptr)) return false;
        if (melds.empty() || !layLegal(m, melds, false)) return false;
        BotAction a = layAct(melds);
        emit(a);
        applyToModel(m, a);
        return true;
    }

    // Kurt keeps a lone okey for the finishing discard (okeyle bitiş doubles the win) once the hand is small.
    bool holdJoker(const Model& m) const {
        if (!hard()) return false;
        int nonJ = 0, j = 0;
        for (int id : m.hand) (ok->isJoker(id) ? j : nonJ)++;
        if (j == 0 || nonJ > 3) return false;
        // not when the hand may end before the next turn
        return g->pileCount() >= 4 && know.minOpenedOppHand > 2;
    }

    bool isleOne(Model& m, const Emit& emit) const {
        if (m.hand.size() <= 1) return false;
        const bool hold = holdJoker(m);
        std::vector<int> order = m.hand;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const bool ja = ok->isJoker(a), jb = ok->isJoker(b);
            if (ja != jb) return jb;
            if (ok->handValue(a) != ok->handValue(b)) return ok->handValue(a) > ok->handValue(b);
            return a < b;
        });
        for (int id : order) {
            if (hold && ok->isJoker(id)) continue;
            AddSide side = AddSide::Auto;
            const int spot = findAddSpot(m, id, side);
            if (spot >= 0 && addLegal(m, id, spot, side)) {
                BotAction a = addAct(id, spot, side);
                emit(a);
                applyToModel(m, a);
                return true;
            }
        }
        return false;
    }

    // Tries to lay/işle everything except `keep`, simulated on a copy. Two orders are tried.
    bool placeAllExcept(const Model& m, int keep, std::vector<BotAction>& out) const {
        for (int order = 0; order < 2; ++order) {
            Model s = m;
            std::vector<BotAction> acts;
            const Emit rec = [&acts](const BotAction& a) { acts.push_back(a); };
            auto isleRest = [&]() {
                for (bool progress = true; progress;) {
                    progress = false;
                    for (int id : std::vector<int>(s.hand)) {
                        if (id == keep) continue;
                        AddSide side = AddSide::Auto;
                        const int spot = findAddSpot(s, id, side);
                        if (spot >= 0 && addLegal(s, id, spot, side)) {
                            BotAction a = addAct(id, spot, side);
                            rec(a);
                            applyToModel(s, a);
                            progress = true;
                        }
                    }
                }
            };
            auto layRest = [&]() {
                std::vector<int> rest = without(s.hand, keep);
                Groups melds;
                if (!s.pairsOpener) melds = solve(rest, -1, OBJ_TILES).melds;
                else melds = solvePairs(rest, *ok).melds;
                if (!melds.empty() && layLegal(s, melds, false)) {
                    BotAction a = layAct(melds);
                    rec(a);
                    applyToModel(s, a);
                }
            };
            if (order == 0) {
                layRest();
                isleRest();
            } else {
                isleRest();
                layRest();
                isleRest();
            }
            if (s.hand.size() == 1 && s.hand[0] == keep) {
                out = acts;
                return true;
            }
        }
        return false;
    }

    bool tryFinish(Model& m, const Emit& emit) const {
        if (m.hand.size() <= 1) return false;
        std::vector<int> cands;
        for (int id : m.hand)
            if (ok->isJoker(id)) cands.push_back(id);
        std::vector<int> others;
        for (int id : m.hand)
            if (!ok->isJoker(id)) others.push_back(id);
        std::sort(others.begin(), others.end());
        cands.insert(cands.end(), others.begin(), others.end());
        std::vector<int> tried;
        for (int t : cands) {
            const int key = ok->isJoker(t) ? -1 : ok->faceColor(t) * 16 + ok->faceNumber(t);
            if (has(tried, key)) continue;
            tried.push_back(key);
            std::vector<BotAction> acts;
            if (placeAllExcept(m, t, acts)) {
                for (const BotAction& a : acts) {
                    emit(a);
                    applyToModel(m, a);
                }
                return true;
            }
        }
        return false;
    }

    // Returns false only when a pending left tile could not be used.
    bool workTable(Model& m, const Emit& emit, Rng& rng) const {
        if (m.pending >= 0 && !usePending(m, emit)) return false;
        for (int guard = 0; guard < 64; ++guard) {
            if (!easy() || rng.chance(0.3f)) {
                if (swapOne(m, emit)) continue;
            }
            if (!easy() && tryFinish(m, emit)) return true;
            if (layBest(m, emit)) continue;
            if (isleOne(m, emit)) continue;
            if (layPairsToo(m, emit)) continue;
            break;
        }
        return true;
    }

    // One-draw outlook of an unopened hand: chance that the next drawn tile lets it open (series or
    // pairs) and the expected series value. Every live face is weighted by its unseen copies.
    double outlook(const std::vector<int>& base, const std::vector<int>& fullHand) const {
        std::vector<int> t = base;
        t.push_back(-1);
        double wsum = 0, pOpen = 0, ev = 0;
        auto tryTile = [&](int id, double w) {
            t.back() = id;
            const int v = solve(t, -1, OBJ_VALUE).value;
            bool open = v >= g->seriesOpenNeed();
            if (!open && solvePairs(t, *ok).value >= g->pairsOpenNeed() + 1) open = true;
            wsum += w;
            if (open) pOpen += w;
            ev += w * std::min(v, g->seriesOpenNeed() + 10);
        };
        for (int c = 0; c < NUM_COLORS; ++c) {
            for (int n = 1; n <= NUM_NUMBERS; ++n) {
                const int live = know.live(c, n);
                if (live <= 0) continue;
                int id = -1;
                if (c == ok->color && n == ok->number) {
                    for (int f = FAKE_JOKER_A; f <= FAKE_JOKER_B; ++f)
                        if (!has(fullHand, f)) id = f;
                } else {
                    for (int cp = 0; cp < 2; ++cp)
                        if (!has(fullHand, makeTileId(c, n, cp))) id = makeTileId(c, n, cp);
                }
                if (id >= 0) tryTile(id, live);
            }
        }
        const int liveJ = std::max(0, 2 - know.visibleJokers - know.myJokers);
        if (liveJ > 0) {
            for (int cp = 0; cp < 2; ++cp) {
                const int j = makeTileId(ok->color, ok->number, cp);
                if (!has(fullHand, j)) {
                    tryTile(j, liveJ);
                    break;
                }
            }
        }
        if (wsum <= 0) return 0;
        return 100.0 * pOpen / wsum + 0.35 * ev / wsum;
    }

    // ---- rollouts (Kurt) ----
    struct Faces {
        int cnt[NUM_COLORS][NUM_NUMBERS + 1] = {};
        int jokers = 0;
    };
    void addFace(Faces& f, int id, int d) const {
        if (ok->isJoker(id)) f.jokers += d;
        else f.cnt[ok->faceColor(id)][ok->faceNumber(id)] += d;
    }
    // Maximum number of pairs (an okey pairs with any single, two okeys pair together).
    static int pairsOf(const Faces& f) {
        int pairs = 0, singles = 0;
        for (int c = 0; c < NUM_COLORS; ++c)
            for (int n = 1; n <= NUM_NUMBERS; ++n) {
                pairs += f.cnt[c][n] / 2;
                singles += f.cnt[c][n] % 2;
            }
        const int use = std::min(f.jokers, singles);
        return pairs + use + (f.jokers - use) / 2;
    }

    // My remaining draws from the pile if everybody draws from it.
    int ownDraws() const { return g->pileCount() / 4; }

    // K samples of `draws` tiles drawn without replacement from the unseen tiles (my future pile draws and
    // left tiles; shared by all candidates).
    std::vector<std::vector<int>> sampleDraws(int K, int draws, Rng& rng) const {
        std::vector<std::vector<int>> out;
        std::vector<int> pool = know.unseen;
        const int D = std::min<int>(draws, (int)pool.size());
        out.reserve(K);
        for (int k = 0; k < K; ++k) {
            std::vector<int> s;
            s.reserve(D);
            for (int i = 0; i < D; ++i) {
                const int j = i + rng.range((int)pool.size() - i);
                std::swap(pool[i], pool[j]);
                s.push_back(pool[i]);
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    // Compact model of the runs/groups on the table for fast işleme checks in rollouts.
    struct TableModel {
        struct Run {
            int color, lo, hi; // represented numbers, 1..13
        };
        struct Grp {
            int number, mask, size;
        };
        struct Key {
            int color, number, joker; // a real tile of this face frees `joker`
        };
        std::vector<Run> runs;
        std::vector<Grp> grps;
        std::vector<Key> keys;

        void add(const Meld& m) {
            if (m.tiles.empty() || m.kind == MeldKind::Pair) return;
            if (m.kind == MeldKind::Run) {
                runs.push_back({m.tiles.front().color, m.tiles.front().number, m.tiles.back().number});
            } else {
                Grp g{m.tiles.front().number, 0, (int)m.tiles.size()};
                for (const PlacedTile& t : m.tiles) g.mask |= 1 << t.color;
                grps.push_back(g);
            }
        }
        // Finds a spot for a tile (joker: any open end / short group) and applies it when `apply`.
        bool place(int c, int n, bool joker, bool apply) {
            for (Run& r : runs) {
                if (joker) {
                    if (r.hi < NUM_NUMBERS) {
                        if (apply) ++r.hi;
                        return true;
                    }
                    if (r.lo > 1) {
                        if (apply) --r.lo;
                        return true;
                    }
                    continue;
                }
                if (c != r.color) continue;
                if (n == r.hi + 1 && r.hi < NUM_NUMBERS) {
                    if (apply) r.hi = n;
                    return true;
                }
                if (n == r.lo - 1 && r.lo > 1) {
                    if (apply) r.lo = n;
                    return true;
                }
            }
            for (Grp& g : grps) {
                if (g.size >= 4) continue;
                if (joker) {
                    if (apply) {
                        for (int k = 0; k < NUM_COLORS; ++k)
                            if (!(g.mask & (1 << k))) {
                                g.mask |= 1 << k;
                                break;
                            }
                        ++g.size;
                    }
                    return true;
                }
                if (n != g.number || (g.mask & (1 << c))) continue;
                if (apply) {
                    g.mask |= 1 << c;
                    ++g.size;
                }
                return true;
            }
            return false;
        }
    };

    TableModel tableModel(const std::vector<Meld>& table) const {
        TableModel tm;
        for (const Meld& m : table) {
            tm.add(m);
            if (m.kind == MeldKind::Pair || !m.hasJoker()) continue;
            for (const PlacedTile& t : m.tiles) {
                if (!t.joker) continue;
                if (m.kind == MeldKind::Run) {
                    tm.keys.push_back({t.color, t.number, t.id});
                } else {
                    int present = 0;
                    for (const PlacedTile& u : m.tiles)
                        if (!u.joker) present |= 1 << u.color;
                    for (int c = 0; c < NUM_COLORS; ++c)
                        if (!(present & (1 << c))) tm.keys.push_back({c, t.number, t.id});
                }
            }
        }
        return tm;
    }
    // Rollouts: swap table okeys with real tiles from the hand (each okey once).
    void rolloutSwaps(std::vector<int>& hand, TableModel& tm) const {
        for (size_t k = 0; k < tm.keys.size(); ++k) {
            const TableModel::Key key = tm.keys[k];
            for (int id : hand) {
                if (ok->isJoker(id) || ok->faceColor(id) != key.color || ok->faceNumber(id) != key.number) continue;
                removeOne(hand, id);
                hand.push_back(key.joker);
                for (size_t q = tm.keys.size(); q-- > 0;)
                    if (tm.keys[q].joker == key.joker) tm.keys.erase(tm.keys.begin() + (long)q);
                k = (size_t)-1; // restart: the list changed
                break;
            }
        }
    }
    bool tmPlace(TableModel& tm, int id, bool apply) const {
        const bool j = ok->isJoker(id);
        return tm.place(j ? 0 : ok->faceColor(id), j ? 0 : ok->faceNumber(id), j, apply);
    }

    // Lays and işler everything it can from `hand` (keeping at least one tile), like my own table work.
    void placeAll(std::vector<int>& hand, TableModel& tm, bool pairsOpener) const {
        if (hand.size() <= 1) return;
        SolveResult r = pairsOpener ? solvePairs(hand, *ok) : solve(hand, -1, OBJ_HAND);
        if (!r.melds.empty()) {
            if (r.tilesUsed >= (int)hand.size()) {
                // keep one tile back: drop one tile from a long meld, else the cheapest meld
                if (!pairsOpener) {
                    bool trimmed = false;
                    for (auto& ids : r.melds) {
                        if (ids.size() < 4) continue;
                        Meld mm;
                        std::vector<int> rest(ids.begin(), ids.end() - 1);
                        if (makeMeld(rest, *ok, mm, false)) {
                            r.leftovers.push_back(ids.back());
                            ids = rest;
                            trimmed = true;
                            break;
                        }
                    }
                    if (!trimmed) {
                        r.leftovers.insert(r.leftovers.end(), r.melds.back().begin(), r.melds.back().end());
                        r.melds.pop_back();
                    }
                } else {
                    r.leftovers.insert(r.leftovers.end(), r.melds.back().begin(), r.melds.back().end());
                    r.melds.pop_back();
                }
            }
            for (const auto& ids : r.melds) {
                Meld mm;
                if (makeMeld(ids, *ok, mm, pairsOpener)) tm.add(mm);
            }
            hand = r.leftovers;
        }
        for (bool progress = true; progress && hand.size() > 1;) {
            progress = false;
            for (size_t i = 0; i < hand.size() && hand.size() > 1; ++i) {
                if (tmPlace(tm, hand[i], true)) {
                    hand.erase(hand.begin() + (long)i);
                    progress = true;
                    break;
                }
            }
        }
    }

    // Can an opened player use `tile` this turn (işle, or a new meld with the hand)?
    bool usableAfterOpening(const std::vector<int>& hand, int tile, const TableModel& tm, bool pairsOpener) const {
        TableModel probe = tm;
        if (tmPlace(probe, tile, false)) return true;
        std::vector<int> t = hand;
        t.push_back(tile);
        if (pairsOpener) {
            if (ok->isJoker(tile)) return hand.size() >= 2;
            for (int id : hand)
                if (ok->isJoker(id) ||
                    (ok->faceColor(id) == ok->faceColor(tile) && ok->faceNumber(id) == ok->faceNumber(tile)))
                    return true;
            return false;
        }
        const SolveResult r = solve(t, tile, OBJ_TILES);
        return r.feasible && r.tilesUsed < (int)t.size();
    }

    // Chance that some opponent finishes during the next round (before my next turn).
    double roundHazard() const {
        double survive = 1.0;
        for (int s = 0; s < NUM_PLAYERS; ++s) {
            if (s == seatNow) continue;
            const PlayerInfo& p = g->player(s);
            double h = 0.01;
            if (p.opened) {
                const int n = (int)p.hand.size();
                h = n <= 1 ? 0.5 : n == 2 ? 0.35 : n == 3 ? 0.25 : n == 4 ? 0.15 : n <= 6 ? 0.08 : 0.04;
            }
            survive *= 1.0 - h;
        }
        return 1.0 - survive;
    }

    // Plays my turns j..D-1 of a rollout for an opened hand (left tile taken when usable, else the pile;
    // everything placeable is laid/işlenir; the most expensive tile is discarded). Opponents may end the
    // hand before each of my turns (hazard `hz`). Returns the expected final score from here, weighted
    // by the survival probability `surv` of reaching turn j.
    double finishRollout(std::vector<int>& hand, TableModel& tm, bool pairsOpener, int j, int D, double surv,
                         double hz, const std::vector<int>& P, const std::vector<int>& L) const {
        const double factor = pairsOpener ? 2.0 : 1.0;
        double ev = 0.0;
        for (; j < D; ++j) {
            ev += surv * hz * factor * handPts(hand);
            surv *= 1.0 - hz;
            const int tile = usableAfterOpening(hand, L[j], tm, pairsOpener) ? L[j] : P[j];
            hand.push_back(tile);
            rolloutSwaps(hand, tm);
            placeAll(hand, tm, pairsOpener);
            if (hand.size() == 1) {
                const int mult = (ok->isJoker(hand[0]) ? 2 : 1) * (pairsOpener ? 2 : 1);
                return ev + surv * g->rules().winnerScore * mult;
            }
            int worst = -1;
            for (int id : hand) {
                if (ok->isJoker(id)) continue;
                if (worst < 0 || ok->handValue(id) > ok->handValue(worst)) worst = id;
            }
            if (worst < 0) worst = hand.front();
            removeOne(hand, worst);
        }
        return ev + surv * factor * handPts(hand);
    }

    // Opened hand: expected final score after discarding each candidate (rollouts over sampled draws).
    std::vector<double> openedRollouts(const Model& m, const std::vector<int>& cands,
                                       const std::vector<std::vector<int>>& pileDraws,
                                       const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const {
        const int D = pileDraws.empty() ? 0 : (int)pileDraws.front().size();
        const double hz = roundHazard();
        const double factor = m.pairsOpener ? 2.0 : 1.0;
        const TableModel tm0 = tableModel(m.table);
        std::vector<double> out(cands.size(), 0.0);
        if (pileDraws.empty()) {
            for (size_t ci = 0; ci < cands.size(); ++ci) out[ci] = factor * handPts(without(m.hand, cands[ci]));
            return out;
        }
        const uint64_t work0 = workUsed();
        int done = 0;
        for (size_t k = 0; k < pileDraws.size(); ++k) {
            if (done > 0 && workUsed() - work0 >= budget) break;
            for (size_t ci = 0; ci < cands.size(); ++ci) {
                std::vector<int> hand = without(m.hand, cands[ci]);
                TableModel tm = tm0;
                out[ci] += finishRollout(hand, tm, m.pairsOpener, 0, D, 1.0, hz, pileDraws[k], leftDraws[k]);
            }
            ++done;
        }
        for (double& v : out) v /= (double)done;
        return out;
    }

    // Lower bound of the best series value once `x` is taken out of the partition `r` (x inside a meld):
    // the meld survives without x (a run end or a four-group), or is lost.
    int valueWithout(const SolveResult& r, int x) const {
        for (const auto& ids : r.melds) {
            if (!has(ids, x)) continue;
            const int v = meldValue(ids);
            int rest = 0;
            if (ids.size() >= 4) {
                Meld mm;
                if (makeMeld(without(ids, x), *ok, mm, false)) rest = mm.value();
            }
            return r.value - v + rest;
        }
        return r.value;
    }

    // Unopened hand: expected final score after discarding each candidate. Each rollout looks for the
    // first of my turns at which the left tile or the pile tile lets me open (series first, then pairs),
    // opens with the partition that sheds the most points, and continues as an opened hand. Solutions of
    // the full hand (every candidate still in it) are computed once per sample and shared: a candidate
    // cannot open where the full hand cannot, and opens exactly like the full hand when a tile of its face
    // is left over there. `budget` caps the solver work (samples are processed whole, so every candidate
    // sees the same samples).
    std::vector<double> unopenedRollouts(const Model& m, const std::vector<int>& cands,
                                         const std::vector<std::vector<int>>& pileDraws,
                                         const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const {
        const RulesConfig& rc = g->rules();
        const int thr = g->seriesOpenNeed();
        const int D = pileDraws.empty() ? 0 : (int)pileDraws.front().size();
        const double hz = roundHazard();
        const double unopened = rc.unopenedScore;
        const TableModel tm0 = tableModel(m.table);
        std::vector<double> total(cands.size(), 0.0);
        int done = 0;

        auto sameFace = [&](int a, int b) {
            return !ok->isJoker(a) && !ok->isJoker(b) && ok->faceColor(a) == ok->faceColor(b) &&
                   ok->faceNumber(a) == ok->faceNumber(b);
        };
        // Index of a leftover with the face of `x` in `r`, x itself first (-1 if none).
        auto leftoverTwin = [&](const SolveResult& r, int x) {
            int twin = -1;
            for (size_t i = 0; i < r.leftovers.size(); ++i) {
                if (r.leftovers[i] == x) return (int)i;
                if (twin < 0 && sameFace(r.leftovers[i], x)) twin = (int)i;
            }
            return twin;
        };
        // Per-sample caches of the full hand at (turn j, via v): v = 0 left tile, v = 1 pile tile.
        std::vector<SolveResult> fullValue(2 * (size_t)D), fullOpen(2 * (size_t)D);
        std::vector<char> haveValue(2 * (size_t)D), haveOpen(2 * (size_t)D);
        std::vector<int> tiles;

        const uint64_t work0 = workUsed();
        for (size_t k = 0; k < pileDraws.size(); ++k) {
            if (done > 0 && workUsed() - work0 >= budget) break;
            const std::vector<int>& P = pileDraws[k];
            const std::vector<int>& L = leftDraws[k];
            std::fill(haveValue.begin(), haveValue.end(), 0);
            std::fill(haveOpen.begin(), haveOpen.end(), 0);
            // Tiles held at turn j after taking the left (v = 0) or the pile (v = 1) tile, without `x`.
            auto held = [&](int j, int v, int x) -> std::vector<int>& {
                tiles.assign(m.hand.begin(), m.hand.end());
                if (x >= 0) removeOne(tiles, x);
                tiles.insert(tiles.end(), P.begin(), P.begin() + j);
                tiles.push_back(v == 0 ? L[j] : P[j]);
                return tiles;
            };
            auto fullAt = [&](int j, int v) -> const SolveResult& {
                const size_t i = 2 * (size_t)j + (size_t)v;
                if (!haveValue[i]) {
                    fullValue[i] = solve(held(j, v, -1), -1, OBJ_VALUE);
                    haveValue[i] = 1;
                }
                return fullValue[i];
            };
            for (size_t ci = 0; ci < cands.size(); ++ci) {
                const int x = cands[ci];
                // pairs: first turn with enough pairs
                Faces f;
                for (int id : m.hand)
                    if (id != x) addFace(f, id, 1);
                int pairTurn = D;
                for (int j = 0; j < D; ++j) {
                    addFace(f, L[j], 1);
                    const bool viaLeft = pairsOf(f) >= g->pairsOpenNeed();
                    addFace(f, L[j], -1);
                    addFace(f, P[j], 1);
                    if (viaLeft || pairsOf(f) >= g->pairsOpenNeed()) {
                        pairTurn = j;
                        break;
                    }
                }
                // series: first (turn, tile) that opens, not later than the pairs
                int openTurn = D, openVia = -1;
                for (int j = 0; j < D && j <= pairTurn && openVia < 0; ++j) {
                    for (int v = 0; v < 2; ++v) {
                        const SolveResult& fr = fullAt(j, v);
                        if (fr.value < thr) continue;
                        if (leftoverTwin(fr, x) >= 0 || valueWithout(fr, x) >= thr ||
                            solve(held(j, v, x), -1, OBJ_VALUE).value >= thr) {
                            openTurn = j;
                            openVia = v;
                            break;
                        }
                    }
                }
                bool pairsOpen = false;
                if (pairTurn < openTurn) {
                    openTurn = pairTurn;
                    pairsOpen = true;
                }
                double ev = 0.0, surv = 1.0;
                // the rounds before my opening turn (and before the end of the hand when I never open)
                for (int j = 0; j < std::min(openTurn + 1, D); ++j) {
                    ev += surv * hz * unopened;
                    surv *= 1.0 - hz;
                }
                if (openTurn >= D) {
                    total[ci] += ev + surv * unopened;
                    continue;
                }
                SolveResult op;
                if (pairsOpen) {
                    // the left tile when it completes the pairs, as in the search above
                    Faces fl;
                    for (int id : held(openTurn, 0, x)) addFace(fl, id, 1);
                    openVia = pairsOf(fl) >= g->pairsOpenNeed() ? 0 : 1;
                    op = solvePairs(held(openTurn, openVia, x), *ok);
                } else {
                    const size_t i = 2 * (size_t)openTurn + (size_t)openVia;
                    if (!haveOpen[i]) {
                        fullOpen[i] = solve(held(openTurn, openVia, -1), -1, OBJ_HAND);
                        haveOpen[i] = 1;
                    }
                    const SolveResult& fo = fullOpen[i];
                    const int twin = fo.value >= thr ? leftoverTwin(fo, x) : -1;
                    if (twin >= 0) {
                        // same partition; `x` (or its twin, which then takes x's place) stays out
                        op = fo;
                        const int t = op.leftovers[twin];
                        op.leftovers.erase(op.leftovers.begin() + twin);
                        if (t != x) {
                            for (auto& ids : op.melds)
                                for (int& id : ids)
                                    if (id == x) id = t;
                        }
                    } else {
                        op = solve(held(openTurn, openVia, x), -1, OBJ_HAND);
                        if (op.value < thr) op = solve(held(openTurn, openVia, x), -1, OBJ_VALUE);
                    }
                }
                TableModel tm = tm0;
                for (const auto& ids : op.melds) {
                    Meld mm;
                    if (makeMeld(ids, *ok, mm, pairsOpen)) tm.add(mm);
                }
                std::vector<int> hand = op.leftovers;
                // the turns before the opening each cost one discard: drop that many expensive leftovers,
                // plus this turn's discard
                for (int d = 0; d <= openTurn && hand.size() > 1; ++d) {
                    int worst = -1;
                    for (int id : hand) {
                        if (ok->isJoker(id)) continue;
                        if (worst < 0 || ok->handValue(id) > ok->handValue(worst)) worst = id;
                    }
                    if (worst < 0) break;
                    removeOne(hand, worst);
                }
                total[ci] += ev + finishRollout(hand, tm, pairsOpen, openTurn + 1, D, surv, hz, P, L);
            }
            ++done;
        }
        std::vector<double> out(cands.size(), unopened);
        if (done > 0)
            for (size_t ci = 0; ci < cands.size(); ++ci) out[ci] = total[ci] / done;
        return out;
    }

    // ---- discard choice ----
    using Scored = std::vector<std::pair<double, int>>; // (keep-score, tile), lowest first

    // Heuristic keep-score of every tile that may be discarded (okeys and a pending left tile excluded): meld
    // membership, potential with the other tiles, points in hand, and danger (the işlek penalty, feeding
    // the right neighbour).
    Scored scoreDiscards(const Model& m, Rng& rng) const {
        const RulesConfig& rc = g->rules();
        const int pile = g->pileCount();
        std::vector<int> cands;
        for (int id : m.hand)
            if (!ok->isJoker(id) && id != m.pending) cands.push_back(id);
        Scored scored;
        if (cands.empty()) return scored;

        // Structure of the hand
        SolveResult series = solve(m.hand, -1, OBJ_VALUE);
        SolveResult pairs = solvePairs(m.hand, *ok);
        std::vector<int> inMeld;
        for (const auto& ml : series.melds) inMeld.insert(inMeld.end(), ml.begin(), ml.end());
        std::vector<int> left;
        for (int id : m.hand)
            if (!has(inMeld, id) && !ok->isJoker(id)) left.push_back(id);

        const bool pairTrack = !m.opened && pairs.value >= std::max(3, g->pairsOpenNeed() - 2) &&
                               series.value < g->seriesOpenNeed() - 15;
        const bool pairOpener = m.opened && m.pairsOpener;
        // Endgame: how much points in hand matter against future potential.
        double potW = 1.0;
        if (pile <= 16) potW = 0.75;
        if (pile <= 8) potW = 0.45;
        if (pile <= 4) potW = 0.2;
        if (m.opened && know.minOpenedOppHand <= 3) potW *= 0.5;

        // Runs laid out in the series partition (for extension potential).
        struct RunEnds {
            int color, lo, hi;
        };
        std::vector<RunEnds> runs;
        for (const auto& ml : series.melds) {
            Meld mm;
            if (makeMeld(ml, *ok, mm, false) && mm.kind == MeldKind::Run)
                runs.push_back({mm.tiles.front().color, mm.tiles.front().number, mm.tiles.back().number});
        }

        auto face = [&](int id, int& c, int& n) {
            c = ok->faceColor(id);
            n = ok->faceNumber(id);
        };

        // Liveness as seen by this level (Easy is optimistic: ignores what is gone).
        auto live = [&](int c, int n) -> double {
            if (n < 1 || n > NUM_NUMBERS) return 0.0;
            if (easy()) return 2.0 - know.mine[c][n];
            return know.live(c, n);
        };

        // Series potential of a leftover tile with the other leftovers.
        auto seriesPotential = [&](int x) -> double {
            int c, n;
            face(x, c, n);
            double pot = 0;
            for (int y : left) {
                if (y == x) continue;
                int c2, n2;
                face(y, c2, n2);
                if (c2 == c) {
                    const int d = std::abs(n2 - n);
                    const int lo = std::min(n, n2), hi = std::max(n, n2);
                    if (d == 1) pot += 3.0 * (live(c, lo - 1) + live(c, hi + 1));  // (no 12-13-1 in 101)
                    else if (d == 2) pot += 2.5 * live(c, lo + 1);
                } else if (n2 == n) {
                    double miss = 0;
                    for (int c3 = 0; c3 < NUM_COLORS; ++c3)
                        if (c3 != c && c3 != c2) miss += live(c3, n);
                    pot += 2.2 * miss;
                }
            }
            for (const RunEnds& r : runs) {
                if (r.color != c) continue;
                if (n == r.hi + 2 && r.hi + 1 <= NUM_NUMBERS) pot += 1.6 * live(c, r.hi + 1);
                if (n == r.lo - 2 && r.lo - 1 >= 1) pot += 1.6 * live(c, r.lo - 1);
            }
            return pot * (0.7 + n / 20.0);
        };

        auto pairPotential = [&](int x) -> double {
            int c, n;
            face(x, c, n);
            int twins = 0;
            for (int y : m.hand)
                if (y != x && !ok->isJoker(y) && ok->faceColor(y) == c && ok->faceNumber(y) == n) ++twins;
            if (twins > 0) return 14.0;
            return 3.0 * live(c, n);
        };

        for (int x : cands) {
            int c, n;
            face(x, c, n);
            double keep = 0;
            if (!m.opened) {
                if (has(inMeld, x)) {
                    keep = 60.0 + n;
                } else {
                    keep = seriesPotential(x) * potW + 0.25 * n;
                }
                if (pairTrack) keep += pairPotential(x) * 1.5 * potW;
                else keep += (pairPotential(x) >= 14.0 ? 2.0 : 0.0);
            } else if (!pairOpener) {
                if (has(inMeld, x)) keep = 40.0; // lays next turn anyway (only when it could not be laid now)
                else keep = seriesPotential(x) * potW;
                keep -= ok->handValue(x) * (1.2 - potW * 0.5);
            } else {
                keep = pairPotential(x) * potW - 2.0 * ok->handValue(x) * (1.2 - potW * 0.5);
            }

            // Danger: penalties and feeding the right neighbour.
            double danger = 0;
            const bool islek = rc.penaltyPlayableDiscard && fitsAnyMeld(m.table, x, *ok);
            if (islek) danger += ISLEK_KEEP;
            if (!easy() && know.rOpened) {
                // a tile that frees a table okey hands it to the neighbour
                for (const Meld& tm : m.table) {
                    if (tm.kind == MeldKind::Pair || !tm.hasJoker()) continue;
                    Meld out;
                    int freed;
                    if (trySwapJoker(tm, x, *ok, out, freed)) danger += 400.0;
                }
            }
            if (hard()) {
                double feed = 0;
                for (int c2 = 0; c2 < NUM_COLORS; ++c2) {
                    for (int n2 = 1; n2 <= NUM_NUMBERS; ++n2) {
                        const int took = know.rTook[c2][n2];
                        if (!took) continue;
                        if (c2 == c && std::abs(n2 - n) <= 2) feed += 3.0 * took;
                        if (n2 == n && c2 != c) feed += 2.5 * took;
                    }
                }
                if (know.rDiscarded[c][n]) feed -= 4.0;
                for (int d = -1; d <= 1; d += 2) {
                    const int nn = n + d;
                    if (nn >= 1 && nn <= NUM_NUMBERS && know.rDiscarded[c][nn]) feed -= 1.0;
                }
                if (know.rOpened && know.rPairs) feed += 2.0 * know.live(c, n);
                if (know.rOpened && know.rHand <= 4) feed *= 2.0;
                if (!know.rOpened && pile <= 10) feed *= 1.5;
                // dead tiles (both other copies gone) are safe to let go
                if (know.live(c, n) == 0) feed -= 1.5;
                danger += feed;
            }
            if (easy()) danger += rng.uniform(0.0f, 6.0f);
            scored.push_back({keep + danger, x});
        }
        std::sort(scored.begin(), scored.end(), [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        return scored;
    }

    // Kurt: rollouts over the most discardable candidates (distinct faces, no işlek tile). Picks the lowest
    // expected final score, the heuristic difference acting as a small danger penalty; `bestEv` receives
    // the expected score of the pick. Returns false when no rollout applies (or, unless `needEv`, when there
    // is nothing to choose between).
    bool rolloutDiscard(const Model& m, const Scored& scored, uint64_t budget, bool needEv, int& bestId,
                        double& bestEv) const {
        const int D = ownDraws();
        if (!m.opened && D < 1) return false;
        Scored top;
        std::vector<std::pair<int, int>> faces;
        const size_t maxCands = m.opened ? scored.size() : (size_t)UNOPENED_CANDIDATES;
        for (const auto& sc : scored) {
            if (sc.first >= ISLEK_KEEP / 2 || top.size() >= maxCands) continue;
            const std::pair<int, int> fc(ok->faceColor(sc.second), ok->faceNumber(sc.second));
            if (std::find(faces.begin(), faces.end(), fc) != faces.end()) continue;
            faces.push_back(fc);
            top.push_back(sc);
        }
        if (top.empty() || (top.size() == 1 && !needEv)) return false;
        std::vector<int> ids;
        for (const auto& sc : top) ids.push_back(sc.second);
        Rng mc = turnRng(*g, m.seat, m.opened ? 0x5A : 0x6B);
        const auto both = sampleDraws(m.opened ? OPENED_SAMPLES : UNOPENED_SAMPLES, 2 * D, mc);
        std::vector<std::vector<int>> pileD, leftD;
        bool full = true;
        for (const auto& smp : both) {
            if ((int)smp.size() < 2 * D) full = false;
            pileD.emplace_back(smp.begin(), smp.begin() + std::min<size_t>(D, smp.size()));
            leftD.emplace_back(smp.begin() + std::min<size_t>(D, smp.size()), smp.end());
        }
        if (!full) {
            if (!m.opened) return false;
            pileD.clear(); // opened: judge by the points left in hand
            leftD.clear();
        }
        const std::vector<double> ev = m.opened ? openedRollouts(m, ids, pileD, leftD, budget)
                                                : unopenedRollouts(m, ids, pileD, leftD, budget);
        double best = 1e18;
        for (size_t i = 0; i < top.size(); ++i) {
            const double score = ev[i] + DANGER_WEIGHT * (top[i].first - top.front().first);
            if (score < best) {
                best = score;
                bestId = top[i].second;
                bestEv = ev[i];
            }
        }
        return true;
    }

    // Work left for look-ahead in the current plan.
    uint64_t budgetLeft() const {
        const uint64_t used = workUsed();
        return used < LOOKAHEAD_BUDGET ? LOOKAHEAD_BUDGET - used : 0;
    }

    int chooseDiscard(const Model& m, Rng& rng) const {
        if (m.hand.size() == 1) return m.hand[0];
        const Scored scored = scoreDiscards(m, rng);
        if (scored.empty()) return m.hand[0];
        if (hard()) {
            int id = -1;
            double ev = 0.0;
            if (rolloutDiscard(m, scored, budgetLeft(), false, id, ev)) return id;
            if (!m.opened && g->pileCount() >= 2) {
                // Last draws before the pile runs out: one-draw look-ahead over the most discardable tiles.
                Scored top;
                for (const auto& sc : scored)
                    if (sc.first < ISLEK_KEEP / 2 && top.size() < 6) top.push_back(sc);
                if (top.size() > 1) {
                    double bestScore = -1e18;
                    int bestId = top.front().second;
                    const double base = top.front().first;
                    for (const auto& sc : top) {
                        const double o = outlook(without(m.hand, sc.second), m.hand);
                        // the heuristic difference still breaks near-ties (feeding, liveness beyond one draw)
                        const double score = o - 0.35 * (sc.first - base);
                        if (score > bestScore) {
                            bestScore = score;
                            bestId = sc.second;
                        }
                    }
                    return bestId;
                }
            }
        }
        if (easy()) {
            // Acemi: sometimes ignores the işlek check, and picks loosely among the weakest tiles.
            int pick = 0;
            const float r = rng.uniform();
            if (r < 0.25f && scored.size() > 1) pick = 1;
            else if (r < 0.35f && scored.size() > 2) pick = 2;
            if (scored[pick].first >= ISLEK_KEEP / 2 && pick > 0 && scored[0].first < ISLEK_KEEP / 2) pick = 0;
            if (rng.chance(0.04f)) {
                // forgot to look at the table: plain lowest keep-value ignoring işlek
                double best = 1e18;
                int id = scored[pick].second;
                for (const auto& s : scored) {
                    const double v = s.first >= ISLEK_KEEP / 2 ? s.first - ISLEK_KEEP : s.first;
                    if (v < best) {
                        best = v;
                        id = s.second;
                    }
                }
                return id;
            }
            return scored[pick].second;
        }
        return scored.front().second;
    }

    // ---- draw decision ----
    BotAction decideDraw(const Game& game, int seat) {
        BotAction a = act(BotAction::Kind::DrawPile);
        const int L = game.topDiscard(Game::leftOf(seat));
        if (L < 0 || !game.canTakeFromLeft(seat)) {
            if (game.pileCount() == 0 && L >= 0 && game.canTakeFromLeft(seat)) return act(BotAction::Kind::TakeLeft);
            return a;
        }
        Model m = modelOf(game, seat);
        m.hand.push_back(L);
        m.pending = L;
        m.stage = TurnStage::Play;
        m.canWork = m.opened; // it will be a later turn than the opening
        bool usable = false;
        if (!m.opened) {
            // Taking a tile only to open straight into a penalty discard is not worth it.
            const Opening op = chooseOpening(m, true);
            usable = op.valid && (easy() || game.pileCount() <= 4 || openingLeavesSafeDiscard(m, op));
        } else {
            Model probe = m;
            usable = usePending(probe, [](const BotAction&) {});
        }
        if (!usable) return a;
        if (easy()) {
            Rng r = turnRng(game, seat, 0x51);
            if (!r.chance(0.55f)) return a;
        }
        return act(BotAction::Kind::TakeLeft);
    }

    // ---- turn planning ----
    std::vector<Step> makePlan(const Game& game, int seat) {
        workStart = detail::solverWork();
        std::vector<Step> steps;
        if (game.stage() == TurnStage::NeedDraw) {
            Step s;
            s.act = decideDraw(game, seat);
            s.sig = modelOf(game, seat).sig();
            steps.push_back(s);
            return steps;
        }
        Model m = modelOf(game, seat);
        Rng rng = turnRng(game, seat, 0x77);
        const Emit emit = [&steps, &m](const BotAction& a) {
            Step s;
            s.act = a;
            s.sig = m.sig(); // model state before this step (callers apply after emitting)
            steps.push_back(s);
        };
        auto push = [&](const BotAction& a) {
            emit(a);
            applyToModel(m, a);
        };

        if (!m.opened) {
            const Opening op = chooseOpening(m, false);
            if (op.valid && !easy() && m.pending < 0 && game.pileCount() > 4 && !openingLeavesSafeDiscard(m, op)) {
                const int t = deferralDiscard(m, op);
                if (t >= 0) {
                    BotAction d = act(BotAction::Kind::Discard);
                    d.tile = t;
                    push(d);
                    return steps;
                }
            }
            if (op.valid && op.pairs && hard() && m.pending < 0 && game.rules().waitTurnAfterOpening) {
                // Kurt: open with pairs now, or keep the hand one more turn for a series opening? Both futures
                // are rolled out; the discard comes with the chosen line.
                Model mo = m;
                BotAction open = act(BotAction::Kind::Open);
                open.melds = op.melds;
                applyToModel(mo, open);
                if (mo.hand.size() > 1) {
                    int idOpen = -1, idWait = -1;
                    double evOpen = 0.0, evWait = 0.0;
                    const bool haveOpen =
                        rolloutDiscard(mo, scoreDiscards(mo, rng), budgetLeft() / 2, true, idOpen, evOpen);
                    const bool haveWait =
                        haveOpen && rolloutDiscard(m, scoreDiscards(m, rng), budgetLeft(), true, idWait, evWait);
                    if (haveWait && evWait < evOpen) {
                        BotAction d = act(BotAction::Kind::Discard);
                        d.tile = idWait;
                        push(d);
                        return steps;
                    }
                    if (haveOpen) {
                        push(open);
                        BotAction d = act(BotAction::Kind::Discard);
                        d.tile = idOpen;
                        push(d);
                        return steps;
                    }
                }
            }
            if (op.valid) {
                BotAction a = act(BotAction::Kind::Open);
                a.melds = op.melds;
                push(a);
                if (op.finishTile >= 0 && m.hand.size() == 1) {
                    BotAction d = act(BotAction::Kind::Discard);
                    d.tile = m.hand[0];
                    push(d);
                    return steps;
                }
            } else if (m.pending >= 0) {
                push(act(BotAction::Kind::ReturnLeft));
                return steps;
            }
        }
        if (m.opened && m.canWork) {
            if (!workTable(m, emit, rng)) {
                steps.clear();
                m = modelOf(game, seat);
                push(act(BotAction::Kind::ReturnLeft));
                return steps;
            }
        }
        if (m.pending >= 0) {
            push(act(BotAction::Kind::ReturnLeft));
            return steps;
        }
        BotAction d = act(BotAction::Kind::Discard);
        d.tile = chooseDiscard(m, rng);
        push(d);
        return steps;
    }
};

// ---------------------------------------------------------------------------------------------------------
// Validation of a single action against the current game (mirrors the engine's checks).

namespace {

bool validAction(const Game& g, int seat, const BotAction& a) {
    if (g.handState() != HandState::Playing || g.current() != seat) return false;
    const PlayerInfo& p = g.player(seat);
    const OkeyInfo& ok = g.okey();
    const int pending = g.pendingLeftTile();
    switch (a.kind) {
    case BotAction::Kind::DrawPile: return g.stage() == TurnStage::NeedDraw && g.pileCount() > 0;
    case BotAction::Kind::TakeLeft: return g.canTakeFromLeft(seat);
    case BotAction::Kind::ReturnLeft: return g.stage() == TurnStage::Play && pending >= 0;
    case BotAction::Kind::Open: return g.checkOpen(seat, a.melds).valid;
    case BotAction::Kind::LayMelds: return g.checkLay(seat, a.melds).valid;
    case BotAction::Kind::AddToMeld: {
        if (!g.canWorkTable(seat) || a.meld < 0 || a.meld >= (int)g.table().size() || !has(p.hand, a.tile))
            return false;
        const Meld& m = g.table()[a.meld];
        if (m.kind == MeldKind::Pair) return false;
        Meld out;
        if (!tryAddTile(m, a.tile, ok, a.side, out)) return false;
        if (p.hand.size() <= 1) return false;
        if (pending >= 0 && a.tile != pending && p.hand.size() <= 2) return false;
        return true;
    }
    case BotAction::Kind::SwapJoker: {
        if (!g.canWorkTable(seat) || a.meld < 0 || a.meld >= (int)g.table().size() || !has(p.hand, a.tile))
            return false;
        const Meld& m = g.table()[a.meld];
        if (m.kind == MeldKind::Pair || !m.hasJoker() || ok.isJoker(a.tile)) return false;
        Meld out;
        int freed = -1;
        return trySwapJoker(m, a.tile, ok, out, freed);
    }
    case BotAction::Kind::Discard:
        return g.stage() == TurnStage::Play && pending < 0 && has(p.hand, a.tile);
    }
    return false;
}

} // namespace

ActionResult applyBotAction(Game& g, int seat, const BotAction& a) {
    switch (a.kind) {
    case BotAction::Kind::DrawPile: return g.drawFromPile(seat);
    case BotAction::Kind::TakeLeft: return g.takeFromLeft(seat);
    case BotAction::Kind::ReturnLeft: return g.returnLeftTile(seat);
    case BotAction::Kind::Open: return g.openHand(seat, a.melds);
    case BotAction::Kind::LayMelds: return g.layMelds(seat, a.melds);
    case BotAction::Kind::AddToMeld: return g.addToMeld(seat, a.tile, a.meld, a.side);
    case BotAction::Kind::SwapJoker: return g.swapJoker(seat, a.tile, a.meld);
    case BotAction::Kind::Discard: return g.discard(seat, a.tile);
    }
    return ActionResult::fail("Geçersiz hamle");
}

BotAction fallbackAction(const Game& g, int seat) {
    BotAction a;
    if (seat < 0 || seat >= NUM_PLAYERS) return a;
    const OkeyInfo& ok = g.okey();
    if (g.stage() == TurnStage::NeedDraw) {
        if (g.pileCount() > 0 || !g.canTakeFromLeft(seat)) a.kind = BotAction::Kind::DrawPile;
        else a.kind = BotAction::Kind::TakeLeft;
        return a;
    }
    const PlayerInfo& p = g.player(seat);
    const int pending = g.pendingLeftTile();
    if (pending >= 0) {
        if (g.canWorkTable(seat)) {
            const std::vector<Meld>& table = g.table();
            for (int i = 0; i < (int)table.size(); ++i) {
                BotAction t;
                t.kind = BotAction::Kind::AddToMeld;
                t.tile = pending;
                t.meld = i;
                if (validAction(g, seat, t)) return t;
            }
            for (int i = 0; i < (int)table.size(); ++i) {
                BotAction t;
                t.kind = BotAction::Kind::SwapJoker;
                t.tile = pending;
                t.meld = i;
                if (validAction(g, seat, t)) return t;
            }
        }
        a.kind = BotAction::Kind::ReturnLeft;
        return a;
    }
    // Discard: highest-value non-okey tile that is not işlek; else highest non-okey; else anything.
    a.kind = BotAction::Kind::Discard;
    int best = -1, bestKey = -1;
    for (int id : p.hand) {
        const bool joker = ok.isJoker(id);
        const bool islek = !joker && g.isPlayableOnTable(id);
        const int key = (joker ? 0 : (islek ? 1000 : 2000)) + ok.faceNumber(id) * 4 + (id & 3);
        if (key > bestKey) {
            bestKey = key;
            best = id;
        }
    }
    if (p.hand.size() == 1) best = p.hand[0];
    a.tile = best;
    return a;
}

// ---------------------------------------------------------------------------------------------------------

Bot::Bot(BotLevel level, uint64_t seed) : impl_(new Impl) {
    impl_->level = level;
    impl_->seed = seed;
}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) {
    impl_->level = level;
    impl_->plan.clear();
    impl_->planPos = 0;
    impl_->haveLast = false;
}

BotLevel Bot::level() const { return impl_->level; }

void Bot::resetForHand() {
    impl_->plan.clear();
    impl_->planPos = 0;
    impl_->haveLast = false;
}

void Bot::observe(const GameEvent& e, const Game&) {
    // Memory is rebuilt from the public game state (discard piles and history) on every plan, so the
    // bot also works when events are not forwarded. Hand boundaries reset the plan.
    if (e.type == EvType::HandStart || e.type == EvType::MatchStart) resetForHand();
}

BotAction Bot::next(const Game& g, int seat) {
    Impl& im = *impl_;
    if (g.handState() != HandState::Playing || g.current() != seat) return fallbackAction(g, seat);
    im.g = &g;
    im.ok = &g.okey();

    const PlayerInfo& p = g.player(seat);
    const uint64_t sig = signature(g.handIndex(), g.turnNumber(), g.stage(), g.pendingLeftTile(), p.hand, g.table());
    if (im.haveLast && sig == im.lastSig) {
        // The last action was not applied (or was rejected): repeat it if it is still legal.
        if (validAction(g, seat, im.lastAct)) return im.lastAct;
        im.plan.clear();
        im.planPos = 0;
        return fallbackAction(g, seat);
    }

    BotAction a;
    bool got = false;
    if (im.planPos < im.plan.size() && im.plan[im.planPos].sig == sig) {
        a = im.plan[im.planPos].act;
        ++im.planPos;
        got = validAction(g, seat, a);
    }
    if (!got) {
        im.seatNow = seat;
        im.buildKnowledge(g, seat);
        im.plan = im.makePlan(g, seat);
        im.planPos = 0;
        if (!im.plan.empty()) {
            a = im.plan[0].act;
            im.planPos = 1;
            got = validAction(g, seat, a);
        }
    }
    if (!got) a = fallbackAction(g, seat);
    im.haveLast = true;
    im.lastSig = sig;
    im.lastAct = a;
    return a;
}

} // namespace okey
