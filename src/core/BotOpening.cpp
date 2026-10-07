// The 101 bots' opening: when and how to open (see Bot.cpp, BotInternal.h).
#include "core/BotInternal.h"

namespace okey {

// ---- opening ----
bool Bot::Impl::wantPairs(const SolveResult& pr, int seriesValue, bool forcedByPending) const {
    const RulesConfig& rc = g->rules();
    if (pr.value < g->pairsOpenNeed()) return false;
    if (forcedByPending) return true;
    const int pile = g->pileCount();
    std::vector<int> rest = pr.leftovers;
    const int restPts = handPts(rest);
    const int pc = pr.value;
    const double pb = bk(STYLE_K_PAIR_WINDOW);
    // Late in the hand anything that beats the unopened score is worth it.
    if (pile <= 3) return 2 * restPts < rc.unopenedScore + 40;
    switch (level) {
    case BotLevel::Easy: return pc >= g->pairsOpenNeed() + 1 || pile <= 10 + (int)std::lround(4 * pb);
    case BotLevel::Normal:
        return pc >= g->pairsOpenNeed() + 1 || seriesValue < g->seriesOpenNeed() - 30 + (int)std::lround(12 * pb) ||
               pile <= 14 + (int)std::lround(6 * pb);
    case BotLevel::Hard: {
        // With the wait-a-turn rule Kurt rolls both futures out in makePlan (open now or keep the hand).
        if (g->rules().waitTurnAfterOpening) return true;
        if (pc >= g->pairsOpenNeed() + 1) return true;
        // Doubled points of what stays in hand against the chance of a series opening soon.
        const bool seriesClose = seriesValue >= g->seriesOpenNeed() - 22 + (int)std::lround(8 * pb) && pile >= 14;
        if (seriesClose) return false;
        return 2 * restPts < rc.unopenedScore + 20 + (int)std::lround(20 * pb) || pile <= 16 + (int)std::lround(4 * pb);
    }
    }
    return false;
}

// Tiles to try as the one left over by a finish, one per face (okeys as one face): the okeys first (finishing
// with one doubles), then the rest by id or, `dearFirst`, dearest first. `skip` is never tried.
std::vector<int> Bot::Impl::finishTries(const std::vector<int>& hand, int skip, bool dearFirst) const {
    std::vector<int> cands, others;
    for (int id : hand)
        if (ok->isJoker(id) && id != skip) cands.push_back(id);
    for (int id : hand)
        if (!ok->isJoker(id) && id != skip) others.push_back(id);
    if (dearFirst)
        std::sort(others.begin(), others.end(), [&](int a, int b) {
            if (ok->handValue(a) != ok->handValue(b)) return ok->handValue(a) > ok->handValue(b);
            return a < b;
        });
    else std::sort(others.begin(), others.end());
    cands.insert(cands.end(), others.begin(), others.end());
    std::vector<int> out, keys;
    for (int t : cands) {
        if (has(keys, faceKey(t))) continue;
        keys.push_back(faceKey(t));
        out.push_back(t);
    }
    return out;
}

Opening Bot::Impl::chooseOpening(const Model& m, bool forPending) const {
    Opening op;
    const int thr = g->seriesOpenNeed();
    const int P = m.pending;
    const std::vector<int>& H = m.hand;

    // 1. elden: lay everything but one tile and finish right away (prefer finishing with the okey).
    if (!easy() || P >= 0) {
        SolveResult rt = solve(H, P, OBJ_TILES);
        if (rt.feasible && rt.tilesUsed + 1 >= (int)H.size() && rt.value >= thr) {
            for (int t : finishTries(H, P, true)) {
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
bool Bot::Impl::openingLeavesSafeDiscard(const Model& m, const Opening& op) const {
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
int Bot::Impl::deferralDiscard(const Model& m, const Opening& op) const {
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

} // namespace okey
