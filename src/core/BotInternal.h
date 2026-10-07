#pragma once
// Internals of the okey bots (101 and klasik okey; see Bot.cpp): helpers shared by the Bot*.cpp files and the
// definition of struct Bot::Impl. The member functions are defined in
//   Bot.cpp (setup, model, discard choice, draw decision, turn planning, the public API),
//   BotOpening.cpp, BotTable.cpp (table work after opening), BotRollout.cpp (Kurt's rollouts),
//   BotClassic.cpp (klasik okey) and BotAnalysis.cpp (hata analizi).
#include "core/Bot.h"
#include "core/OkeyHand.h"
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
extern unsigned teamAwareMask; // tests/sim --team-ab (Bot.cpp)
extern unsigned styleKnobs;    // tests/sim --style-knobs (Bot.cpp)
} // namespace detail

namespace botimpl {

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
constexpr double TWIN_KEEP = 14.0;      // pair potential of a tile whose twin is in hand
constexpr double MELD_KEEP = 40.0;      // opened: keep-score of a tile in a meld that lays next turn
constexpr double FREES_OKEY = 400.0;    // danger of a tile that frees a table okey for the right neighbour
// Expected yandan açma cezası (points, see feedCost) in the heuristic keep-score, and on top of it in Kurt's
// rollouts. Tuned with bot-vs-bot duplicate deals: Kurt gains most when it also counts the indirect cost (a
// neighbour who opens can finish), so its rollouts weigh it above the plain expectation.
constexpr double FEED_WEIGHT = 1.0;
constexpr double FEED_WEIGHT_EASY = 0.4; // Acemi: only a vague "don't hand out big tiles"
constexpr double FEED_ROLL = 2.0;
// Klasik okey hand value: tiles in melds, the expected gain of the next draw and the chance that it finishes (the
// next draw is looked at by Kurt only: worth about 0.27 points a hand in duplicate deals, which is what sets Kurt
// apart from Usta; a second draw of look-ahead and other weights measured no better).
constexpr double CLASSIC_WIN = 30.0;
constexpr int CLASSIC_SAMPLES = 96; // hata analizi: sampled futures per klasik okey discard
// Eşli 101: what the partner's hand is expected to cost the team when the opponents end the hand (pile out or an
// opponent finishes): an unopened partner a share of the unopened score (they may still open), an opened one
// about this much per tile left.
constexpr double PARTNER_UNOPENED_SHARE = 0.4;
constexpr double PARTNER_TILE_POINTS = 3.5;
// Personality (BotStyle::boldness b in -1..1, 0 = the tuned play above). A bold bot fears feeding the right
// neighbour less (x (1 - STYLE_FEED * b) on every feed / danger term), opens with pairs sooner (Kurt: the open-now
// line gets STYLE_PAIR_BIAS points of credit over waiting for a series; Usta/Acemi: wider pile windows), takes the
// left tile / opens into a penalty discard a few tiles earlier at the end of the pile, and values building the hand
// over shedding points; a cautious one does the opposite. Klasik okey: a bold bot leans to the seven-pairs line.
enum : unsigned {
    STYLE_K_FEED = 1,         // feed / danger weights (101)
    STYLE_K_PAIR_WINDOW = 2,  // Acemi / Usta / Kurt-without-wait pair-opening thresholds
    STYLE_K_PAIR_BIAS = 4,    // Kurt open-with-pairs-now credit
    STYLE_K_LATE_OPEN = 8,    // end-of-pile window for opening into a penalty discard
    STYLE_K_POTENTIAL = 16,   // build the hand vs shed points
    STYLE_K_EASY_TAKE = 32,   // Acemi's chance to take a usable left tile
    STYLE_K_CLASSIC_PAIRS = 64,  // klasik okey: seven-pairs lean (and take-left for pairs)
    STYLE_K_CLASSIC_DANGER = 128 // klasik okey: classicDanger weight
};
constexpr double STYLE_FEED = 0.4;
constexpr double STYLE_PAIR_BIAS = 12.0;


using Groups = std::vector<std::vector<int>>;

inline bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

inline bool removeOne(std::vector<int>& v, int x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it == v.end()) return false;
    v.erase(it);
    return true;
}

inline std::vector<int> without(const std::vector<int>& v, int x) {
    std::vector<int> r = v;
    removeOne(r, x);
    return r;
}

inline int groupsTileCount(const Groups& g) {
    int n = 0;
    for (const auto& m : g) n += (int)m.size();
    return n;
}

inline bool groupsContain(const Groups& g, int x) {
    for (const auto& m : g)
        if (has(m, x)) return true;
    return false;
}

inline uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    return h;
}

// Signature of the parts of the state a bot's plan depends on.
inline uint64_t signature(int handIndex, int turn, TurnStage stage, int pending, std::vector<int> hand,
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
    int rTurns = 0;            // turns my right neighbour has played this hand (their discards)
    bool anyOpened = false;    // someone has opened this hand
    bool rOpened = false, rPairs = false, rCanSwapWork = false;
    int rHand = 21;
    int minOpenedOppHand = 99; // smallest hand among opponents who opened (eşli: not counting the partner)
    int anyOppHandMin = 99;
    // Eşli 101 (the bot plays for the team): the partner across.
    bool team = false;
    int partner = -1;
    bool pOpened = false, pPairs = false;
    int pHand = 21;
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

} // namespace botimpl
using namespace botimpl; // (only the Bot*.cpp files include this header)

// ---------------------------------------------------------------------------------------------------------

struct Bot::Impl {
    BotLevel level = BotLevel::Normal;
    uint64_t seed = 1;
    BotStyle style;

    double bold() const { return std::clamp((double)style.boldness, -1.0, 1.0); }
    // boldness as seen by one knob (0 when the knob is switched off by sim --style-knobs)
    double bk(unsigned knob) const { return (detail::styleKnobs & knob) ? bold() : 0.0; }
    // multiplier of every "don't feed the neighbour" / danger weight
    double feedMul() const { return 1.0 - STYLE_FEED * bk(STYLE_K_FEED); }
    // pile count up to which the end-of-pile shortcuts apply (open into a penalty discard, take to open)
    int lateOpenPile() const { return 4 + (int)std::lround(3.0 * bk(STYLE_K_LATE_OPEN)); }

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

    Rng turnRng(const Game& game, int seat, uint64_t salt) const;

    // ---- setup ----
    Model modelOf(const Game& game, int seat) const;
    void buildKnowledge(const Game& game, int seat);

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

    SolveResult greedyPartition(const std::vector<int>& hand) const;

    // ---- model updates (mirror Game's actions) ----
    bool applyToModel(Model& m, const BotAction& a) const;
    bool layLegal(const Model& m, const Groups& melds, bool opening) const;
    bool addLegal(const Model& m, int tile, int meldIdx, AddSide side) const;
    bool swapLegal(const Model& m, int tile, int meldIdx) const;

    // ---- trimming: an opening/lay must leave at least one tile in hand ----
    bool trimToKeepOne(Groups& melds, const std::vector<int>& hand, int must, bool pairs, int minValue,
                       int minCount, int* keptTile) const;

    // ---- opening ----
    bool wantPairs(const SolveResult& pr, int seriesValue, bool forcedByPending) const;

    // Face of a tile as one number (an okey counts as the face it shows, like the sahte okeys).
    int faceOf(int id) const { return ok->faceColor(id) * 16 + ok->faceNumber(id); }
    // The same with the okeys as one face of their own (-1).
    int faceKey(int id) const { return ok->isJoker(id) ? -1 : faceOf(id); }
    // Two real (non-okey) tiles of the same face.
    bool sameFace(int a, int b) const { return !ok->isJoker(a) && !ok->isJoker(b) && faceOf(a) == faceOf(b); }

    std::vector<int> finishTries(const std::vector<int>& hand, int skip, bool dearFirst) const;
    Opening chooseOpening(const Model& m, bool forPending) const;
    bool openingLeavesSafeDiscard(const Model& m, const Opening& op) const;
    int deferralDiscard(const Model& m, const Opening& op) const;

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
    static BotAction addAct(int tile, int meld, AddSide side = AddSide::Auto);
    static BotAction swapAct(int tile, int meld) {
        BotAction a = act(BotAction::Kind::SwapJoker);
        a.tile = tile;
        a.meld = meld;
        return a;
    }

    int jokerSpotSafety(const Meld& before, const Meld& after) const;
    int findAddSpot(const Model& m, int tile, AddSide& side) const;
    bool usePending(Model& m, const Emit& emit) const;
    bool swapOne(Model& m, const Emit& emit) const;
    bool layPairsToo(Model& m, const Emit& emit) const;
    bool layBest(Model& m, const Emit& emit) const;
    bool holdJoker(const Model& m) const;
    bool isleOne(Model& m, const Emit& emit) const;
    bool placeAllExcept(const Model& m, int keep, std::vector<BotAction>& out) const;
    bool tryFinish(Model& m, const Emit& emit) const;
    bool workTable(Model& m, const Emit& emit, Rng& rng) const;
    double outlook(const std::vector<int>& base, const std::vector<int>& fullHand) const;

    // ---- rollouts (Kurt) ----
    struct Faces {
        int cnt[NUM_COLORS][NUM_NUMBERS + 1] = {};
        int jokers = 0;
    };
    void addFace(Faces& f, int id, int d) const {
        if (ok->isJoker(id)) f.jokers += d;
        else f.cnt[ok->faceColor(id)][ok->faceNumber(id)] += d;
    }
    static int pairsOf(const Faces& f);

    // My remaining draws from the pile if everybody draws from it.
    int ownDraws() const { return g->pileCount() / 4; }

    std::vector<std::vector<int>> sampleDraws(int K, int draws, Rng& rng) const;
    static bool splitDraws(const std::vector<std::vector<int>>& both, int D, std::vector<std::vector<int>>& pileD,
                           std::vector<std::vector<int>>& leftD);

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
                Grp gp{m.tiles.front().number, 0, (int)m.tiles.size()};
                for (const PlacedTile& t : m.tiles) gp.mask |= 1 << t.color;
                grps.push_back(gp);
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
            for (Grp& gp : grps) {
                if (gp.size >= 4) continue;
                if (joker) {
                    if (apply) {
                        for (int k = 0; k < NUM_COLORS; ++k)
                            if (!(gp.mask & (1 << k))) {
                                gp.mask |= 1 << k;
                                break;
                            }
                        ++gp.size;
                    }
                    return true;
                }
                if (n != gp.number || (gp.mask & (1 << c))) continue;
                if (apply) {
                    gp.mask |= 1 << c;
                    ++gp.size;
                }
                return true;
            }
            return false;
        }
    };

    TableModel tableModel(const std::vector<Meld>& table) const;
    void rolloutSwaps(std::vector<int>& hand, TableModel& tm) const;
    bool tmPlace(TableModel& tm, int id, bool apply) const {
        const bool j = ok->isJoker(id);
        return tm.place(j ? 0 : ok->faceColor(id), j ? 0 : ok->faceNumber(id), j, apply);
    }

    void placeAll(std::vector<int>& hand, TableModel& tm, bool pairsOpener) const;
    bool usableAfterOpening(const std::vector<int>& hand, int tile, const TableModel& tm, bool pairsOpener) const;

    // How the hand may end before my next turn. `opp`: an opponent finishes (single 101: anybody else), my hand
    // counts (plus `partCost`, the partner's expected hand in eşli); `part`: my partner finishes (eşli), my hand
    // is not counted and the team gets `partWin`.
    struct Hazard {
        double opp = 0.0, part = 0.0;
        double partWin = 0.0, partCost = 0.0;
        double oppMult = 1.0; // (101 kuralları) the kat an opponent's finish is expected to put on my hand
    };
    static double finishChance(const PlayerInfo& p) {
        if (!p.opened) return 0.01;
        const int n = (int)p.hand.size();
        return n <= 1 ? 0.5 : n == 2 ? 0.35 : n == 3 ? 0.25 : n == 4 ? 0.15 : n <= 6 ? 0.08 : 0.04;
    }
    Hazard roundHazard() const;

    // Expected score of one round before my turn when the hand ends during it, given my cost `mine` if it does.
    static double roundEnd(const Hazard& hz, double mine) {
        return hz.opp * (mine + hz.partCost) * hz.oppMult + hz.part * hz.partWin;
    }

    int dearestPlain(const std::vector<int>& hand) const;
    double finishRollout(std::vector<int>& hand, TableModel& tm, bool pairsOpener, int j, int D, double surv,
                         const Hazard& hz, const std::vector<int>& P, const std::vector<int>& L) const;

    std::vector<double> openedRollouts(const Model& m, const std::vector<int>& cands,
                                       const std::vector<std::vector<int>>& pileDraws,
                                       const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const;

    int valueWithout(const SolveResult& r, int x) const;
    std::vector<double> unopenedRollouts(const Model& m, const std::vector<int>& cands,
                                         const std::vector<std::vector<int>>& pileDraws,
                                         const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const;

    double feedCost(int x) const;

    // ---- discard choice ----
    using Scored = std::vector<std::pair<double, int>>; // (keep-score, tile), lowest first

    Scored scoreDiscards(const Model& m, Rng& rng) const;
    bool rolloutDiscard(const Model& m, const Scored& scored, uint64_t budget, bool needEv, int& bestId,
                        double& bestEv) const;

    // Work left for look-ahead in the current plan.
    uint64_t budgetLeft() const {
        const uint64_t used = workUsed();
        return used < LOOKAHEAD_BUDGET ? LOOKAHEAD_BUDGET - used : 0;
    }

    int chooseDiscard(const Model& m, Rng& rng) const;

    // ---- draw decision ----
    BotAction decideDraw(const Game& game, int seat);

    // ---- klasik okey ----
    int freeTileOf(int c, int n, const std::vector<int>& hand) const;

    // Faces that might come next with their weight (unseen copies as this level knows them), okey last.
    struct Draw {
        int id;
        double w;
    };
    std::vector<Draw> classicDraws(const std::vector<int>& hand) const;
    bool relevant(int id, const std::vector<int>& hand) const;
    double classicValue(const std::vector<int>& h14, const std::vector<Draw>& draws, bool lookahead) const;
    double classicDanger(int x) const;
    BotAction classicNext(const Game& game, int seat);

    // ---- hata analizi (Bot::evaluate*) ----
    void prepareAnalysis(const Game& game, int seat);
    std::vector<int> faceReps(const std::vector<int>& hand, int pending) const;
    int repIndex(const std::vector<int>& reps, int id) const;
    std::vector<double> analysisValues(const Model& m, const std::vector<int>& cands, uint64_t budget) const;

    // Score of finishing right after an opening that leaves one tile (elden when nobody else has opened).
    double finishScore(const Model& m, bool pairs) const {
        // (101 kuralları: the katlar stack, double once or not at all — Game::finishMultiplier)
        const int mult = g->finishMultiplier(!m.hand.empty() && ok->isJoker(m.hand[0]), pairs, !know.anyOpened);
        return (double)g->rules().winnerScore * mult;
    }

    double bestAfterDraw(const Model& m, uint64_t budget) const;
    bool evaluateDrawImpl(const Game& game, int seat, double& take, double& pile);
    std::vector<std::pair<int, double>> classicChances(const Game& game, int seat, int must) const;

    // ---- turn planning ----
    std::vector<Step> makePlan(const Game& game, int seat);
};

} // namespace okey
