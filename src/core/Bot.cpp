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
//
// The shared internals are in BotInternal.h; this file holds the turn machinery and the public API.
#include "core/BotInternal.h"

namespace okey {

namespace detail {
// tests/sim --team-ab: seats (bit per seat) whose bots play eşli 101 as a team (default: all).
unsigned teamAwareMask = 0xF;
// tests/sim --style-knobs: which personality knobs (STYLE_* bits) a styled bot uses (default: all).
unsigned styleKnobs = ~0u;
} // namespace detail

Rng Bot::Impl::turnRng(const Game& game, int seat, uint64_t salt) const {
    uint64_t h = mix64(seed, (uint64_t)game.handIndex());
    h = mix64(h, (uint64_t)game.turnNumber());
    h = mix64(h, (uint64_t)seat);
    h = mix64(h, salt);
    return Rng(h);
}

// ---- setup ----
Model Bot::Impl::modelOf(const Game& game, int seat) const {
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

void Bot::Impl::buildKnowledge(const Game& game, int seat) {
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
    if (!easy())
        for (int id : r.discards) see(id, know.rDiscarded); // the pile right beside me
    for (const DiscardRecord& rec : game.discardHistory())
        if (rec.player == know.right) ++know.rTurns;
    for (int s = 0; s < NUM_PLAYERS; ++s)
        if (game.player(s).opened) know.anyOpened = true;
    know.team = game.teams() && (detail::teamAwareMask >> seat & 1u);
    if (know.team) {
        know.partner = Game::partnerOf(seat);
        const PlayerInfo& pp = game.player(know.partner);
        know.pOpened = pp.opened;
        know.pPairs = pp.openedWithPairs;
        know.pHand = (int)pp.hand.size();
    }
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        if (s == seat || s == know.partner) continue;
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

// Easy bots look for melds greedily (no okey planning) when deciding to open on their own.
SolveResult Bot::Impl::greedyPartition(const std::vector<int>& hand) const {
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
bool Bot::Impl::applyToModel(Model& m, const BotAction& a) const {
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
bool Bot::Impl::layLegal(const Model& m, const Groups& melds, bool opening) const {
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

bool Bot::Impl::addLegal(const Model& m, int tile, int meldIdx, AddSide side) const {
    if (!m.canWork || meldIdx < 0 || meldIdx >= (int)m.table.size() || !has(m.hand, tile)) return false;
    if (m.table[meldIdx].kind == MeldKind::Pair) return false;
    Meld out;
    if (!tryAddTile(m.table[meldIdx], tile, *ok, side, out)) return false;
    if (m.hand.size() <= 1) return false;
    if (m.pending >= 0 && tile != m.pending && m.hand.size() <= 2) return false;
    return true;
}

bool Bot::Impl::swapLegal(const Model& m, int tile, int meldIdx) const {
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
bool Bot::Impl::trimToKeepOne(Groups& melds, const std::vector<int>& hand, int must, bool pairs, int minValue,
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

// Heuristic keep-score of every tile that may be discarded (okeys and a pending left tile excluded): meld
// membership, potential with the other tiles, points in hand, and danger (the işlek penalty, feeding
// the right neighbour).
Bot::Impl::Scored Bot::Impl::scoreDiscards(const Model& m, Rng& rng) const {
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
    potW *= 1.0 + 0.15 * bk(STYLE_K_POTENTIAL); // bold: build the hand; cautious: shed points

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
        if (twins > 0) return TWIN_KEEP;
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
            else keep += (pairPotential(x) >= TWIN_KEEP ? 2.0 : 0.0);
        } else if (!pairOpener) {
            if (has(inMeld, x)) keep = MELD_KEEP; // lays next turn anyway (only when it could not be laid now)
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
                if (trySwapJoker(tm, x, *ok, out, freed)) danger += FREES_OKEY;
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
            danger += feed * feedMul();
        }
        danger += (easy() ? FEED_WEIGHT_EASY : FEED_WEIGHT) * feedMul() * feedCost(x); // yandan açma cezası
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
bool Bot::Impl::rolloutDiscard(const Model& m, const Scored& scored, uint64_t budget, bool needEv, int& bestId,
                    double& bestEv) const {
    const int D = ownDraws();
    if (!m.opened && D < 1) return false;
    Scored top;
    std::vector<int> faces;
    const size_t maxCands = m.opened ? scored.size() : (size_t)UNOPENED_CANDIDATES;
    for (const auto& sc : scored) {
        if (sc.first >= ISLEK_KEEP / 2 || top.size() >= maxCands) continue;
        if (has(faces, faceOf(sc.second))) continue;
        faces.push_back(faceOf(sc.second));
        top.push_back(sc);
    }
    if (top.empty() || (top.size() == 1 && !needEv)) return false;
    std::vector<int> ids;
    for (const auto& sc : top) ids.push_back(sc.second);
    Rng mc = turnRng(*g, m.seat, m.opened ? 0x5A : 0x6B);
    const auto both = sampleDraws(m.opened ? OPENED_SAMPLES : UNOPENED_SAMPLES, 2 * D, mc);
    std::vector<std::vector<int>> pileD, leftD;
    if (!splitDraws(both, D, pileD, leftD)) {
        if (!m.opened) return false;
        pileD.clear(); // opened: judge by the points left in hand
        leftD.clear();
    }
    const std::vector<double> ev = m.opened ? openedRollouts(m, ids, pileD, leftD, budget)
                                            : unopenedRollouts(m, ids, pileD, leftD, budget);
    double best = 1e18;
    for (size_t i = 0; i < top.size(); ++i) {
        const double score =
            ev[i] + DANGER_WEIGHT * (top[i].first - top.front().first) + FEED_ROLL * feedMul() * feedCost(top[i].second);
        if (score < best) {
            best = score;
            bestId = top[i].second;
            bestEv = ev[i];
        }
    }
    return true;
}

int Bot::Impl::chooseDiscard(const Model& m, Rng& rng) const {
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
BotAction Bot::Impl::decideDraw(const Game& game, int seat) {
    BotAction a = act(BotAction::Kind::DrawPile);
    const int L = game.topDiscard(Game::leftOf(seat));
    if (L < 0 || !game.canTakeFromLeft(seat)) return a;
    Model m = modelOf(game, seat);
    m.hand.push_back(L);
    m.pending = L;
    m.stage = TurnStage::Play;
    m.canWork = m.opened; // it will be a later turn than the opening
    bool usable = false;
    if (!m.opened) {
        // Taking a tile only to open straight into a penalty discard is not worth it.
        const Opening op = chooseOpening(m, true);
        usable = op.valid && (easy() || game.pileCount() <= lateOpenPile() || openingLeavesSafeDiscard(m, op));
    } else {
        Model probe = m;
        usable = usePending(probe, [](const BotAction&) {});
    }
    if (!usable) return a;
    if (easy()) {
        Rng r = turnRng(game, seat, 0x51);
        if (!r.chance(0.55f + 0.1f * (float)bk(STYLE_K_EASY_TAKE))) return a;
    }
    return act(BotAction::Kind::TakeLeft);
}

// ---- turn planning ----
std::vector<Bot::Impl::Step> Bot::Impl::makePlan(const Game& game, int seat) {
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
        if (op.valid && !easy() && m.pending < 0 && game.pileCount() > lateOpenPile() && !openingLeavesSafeDiscard(m, op)) {
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
                if (haveWait && evWait + STYLE_PAIR_BIAS * bk(STYLE_K_PAIR_BIAS) < evOpen) {
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

// ---------------------------------------------------------------------------------------------------------
// Validation of a single action against the current game (mirrors the engine's checks).

namespace {

bool validAction(const Game& g, int seat, const BotAction& a) {
    if (g.handState() != HandState::Playing || g.current() != seat) return false;
    const PlayerInfo& p = g.player(seat);
    const int pending = g.pendingLeftTile();
    switch (a.kind) {
    case BotAction::Kind::DrawPile: return g.stage() == TurnStage::NeedDraw && g.pileCount() > 0;
    case BotAction::Kind::TakeLeft: return g.canTakeFromLeft(seat);
    case BotAction::Kind::ReturnLeft: return g.stage() == TurnStage::Play && pending >= 0;
    case BotAction::Kind::Open: return g.checkOpen(seat, a.melds).valid;
    case BotAction::Kind::LayMelds: return g.checkLay(seat, a.melds).valid;
    case BotAction::Kind::AddToMeld: return g.checkAdd(seat, a.tile, a.meld, a.side).ok;
    case BotAction::Kind::SwapJoker: return g.checkSwap(seat, a.tile, a.meld).ok;
    case BotAction::Kind::Discard:
        return g.stage() == TurnStage::Play && pending < 0 && has(p.hand, a.tile);
    case BotAction::Kind::Finish: return g.finishKind(seat, a.tile) > 0;
    case BotAction::Kind::ShowIndicator: return g.indicatorTwin(seat) >= 0;
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
    case BotAction::Kind::Finish: return g.finishHand(seat, a.tile);
    case BotAction::Kind::ShowIndicator: return g.showIndicator(seat);
    }
    return ActionResult::fail("Geçersiz hamle");
}

BotAction fallbackAction(const Game& g, int seat) {
    BotAction a;
    if (seat < 0 || seat >= NUM_PLAYERS) return a;
    const OkeyInfo& ok = g.okey();
    if (g.stage() == TurnStage::NeedDraw) {
        // (play never reaches a draw with an empty pile; a hand-built test position can, and then only the
        // left tile is legal)
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

void Bot::setStyle(BotStyle style) {
    impl_->style = style;
    impl_->plan.clear();
    impl_->planPos = 0;
    impl_->haveLast = false;
}

BotStyle Bot::style() const { return impl_->style; }

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
    if (g.classic()) {
        // klasik okey: one decision at a time from the public state (no multi-step table plans)
        im.seatNow = seat;
        im.buildKnowledge(g, seat);
        BotAction a = im.classicNext(g, seat);
        if (!validAction(g, seat, a)) a = fallbackAction(g, seat);
        return a;
    }

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
