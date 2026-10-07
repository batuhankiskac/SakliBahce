// Kurt's rollouts: sampled futures played to the end of the hand for each candidate discard (see Bot.cpp,
// BotInternal.h).
#include "core/BotInternal.h"

namespace okey {

// Maximum number of pairs (an okey pairs with any single, two okeys pair together).
int Bot::Impl::pairsOf(const Faces& f) {
    int pairs = 0, singles = 0;
    for (int c = 0; c < NUM_COLORS; ++c)
        for (int n = 1; n <= NUM_NUMBERS; ++n) {
            pairs += f.cnt[c][n] / 2;
            singles += f.cnt[c][n] % 2;
        }
    const int use = std::min(f.jokers, singles);
    return pairs + use + (f.jokers - use) / 2;
}

// K samples of `draws` tiles drawn without replacement from the unseen tiles (my future pile draws and
// left tiles; shared by all candidates).
std::vector<std::vector<int>> Bot::Impl::sampleDraws(int K, int draws, Rng& rng) const {
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

// Splits samples of 2*D draws into my pile draws (first D) and the left tiles (the rest). False when some
// sample is short (fewer unseen tiles than draws); the halves are filled either way.
bool Bot::Impl::splitDraws(const std::vector<std::vector<int>>& both, int D, std::vector<std::vector<int>>& pileD,
                       std::vector<std::vector<int>>& leftD) {
    bool full = true;
    for (const auto& smp : both) {
        if ((int)smp.size() < 2 * D) full = false;
        pileD.emplace_back(smp.begin(), smp.begin() + std::min<size_t>(D, smp.size()));
        leftD.emplace_back(smp.begin() + std::min<size_t>(D, smp.size()), smp.end());
    }
    return full;
}

Bot::Impl::TableModel Bot::Impl::tableModel(const std::vector<Meld>& table) const {
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
void Bot::Impl::rolloutSwaps(std::vector<int>& hand, TableModel& tm) const {
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

// Lays and işler everything it can from `hand` (keeping at least one tile), like my own table work.
void Bot::Impl::placeAll(std::vector<int>& hand, TableModel& tm, bool pairsOpener) const {
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
bool Bot::Impl::usableAfterOpening(const std::vector<int>& hand, int tile, const TableModel& tm, bool pairsOpener) const {
    TableModel probe = tm;
    if (tmPlace(probe, tile, false)) return true;
    std::vector<int> t = hand;
    t.push_back(tile);
    if (pairsOpener) {
        if (ok->isJoker(tile)) return hand.size() >= 2;
        for (int id : hand)
            if (ok->isJoker(id) || sameFace(id, tile)) return true;
        return false;
    }
    const SolveResult r = solve(t, tile, OBJ_TILES);
    return r.feasible && r.tilesUsed < (int)t.size();
}

Bot::Impl::Hazard Bot::Impl::roundHazard() const {
    Hazard hz;
    double survive = 1.0;
    double wSum = 0.0, wMult = 0.0;
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        if (s == seatNow || s == know.partner) continue;
        const double f = finishChance(g->player(s));
        survive *= 1.0 - f;
        // 101 kuralları: a pair opener's finish is çiftten (x2 unless the game is katsız)
        wSum += f;
        wMult += f * g->finishMultiplier(false, g->player(s).opened && g->player(s).openedWithPairs, false);
    }
    hz.opp = 1.0 - survive;
    if (wSum > 0.0) hz.oppMult = wMult / wSum;
    if (know.team) {
        const RulesConfig& rc = g->rules();
        const double p = finishChance(g->player(know.partner));
        // the partner finishes when no opponent did first (roughly: half the round comes before them)
        hz.part = p * (1.0 - 0.5 * hz.opp);
        hz.partWin = rc.winnerScore * (double)g->finishMultiplier(false, know.pPairs, false); // (101 kuralları)
        hz.partCost = know.pOpened ? PARTNER_TILE_POINTS * know.pHand * (know.pPairs ? 2.0 : 1.0)
                                   : PARTNER_UNOPENED_SHARE * rc.unopenedScore;
        // the two events are exclusive: keep their sum a probability (opp .75 + part .3125 would make the
        // survival factor 1 - opp - part negative)
        const double sum = hz.opp + hz.part;
        if (sum > 1.0) {
            hz.opp /= sum;
            hz.part /= sum;
        }
    }
    return hz;
}

// The most expensive tile of `hand` that is not an okey (the first of equals), or -1.
int Bot::Impl::dearestPlain(const std::vector<int>& hand) const {
    int worst = -1;
    for (int id : hand) {
        if (ok->isJoker(id)) continue;
        if (worst < 0 || ok->handValue(id) > ok->handValue(worst)) worst = id;
    }
    return worst;
}

// Plays my turns j..D-1 of a rollout for an opened hand (left tile taken when usable, else the pile;
// everything placeable is laid/işlenir; the most expensive tile is discarded). Opponents may end the
// hand before each of my turns (hazard `hz`). Returns the expected final score from here, weighted
// by the survival probability `surv` of reaching turn j.
double Bot::Impl::finishRollout(std::vector<int>& hand, TableModel& tm, bool pairsOpener, int j, int D, double surv,
                     const Hazard& hz, const std::vector<int>& P, const std::vector<int>& L) const {
    const double factor = pairsOpener ? 2.0 : 1.0;
    double ev = 0.0;
    for (; j < D; ++j) {
        ev += surv * roundEnd(hz, factor * handPts(hand));
        surv *= 1.0 - hz.opp - hz.part;
        const int tile = usableAfterOpening(hand, L[j], tm, pairsOpener) ? L[j] : P[j];
        hand.push_back(tile);
        rolloutSwaps(hand, tm);
        placeAll(hand, tm, pairsOpener);
        if (hand.size() == 1) {
            const int mult = g->finishMultiplier(ok->isJoker(hand[0]), pairsOpener, false); // (101 kuralları)
            return ev + surv * g->rules().winnerScore * mult;
        }
        int worst = dearestPlain(hand);
        if (worst < 0) worst = hand.front();
        removeOne(hand, worst);
    }
    return ev + surv * (factor * handPts(hand) + hz.partCost);
}

// Opened hand: expected final score after discarding each candidate (rollouts over sampled draws).
std::vector<double> Bot::Impl::openedRollouts(const Model& m, const std::vector<int>& cands,
                                   const std::vector<std::vector<int>>& pileDraws,
                                   const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const {
    const int D = pileDraws.empty() ? 0 : (int)pileDraws.front().size();
    const Hazard hz = roundHazard();
    const double factor = m.pairsOpener ? 2.0 : 1.0;
    const TableModel tm0 = tableModel(m.table);
    std::vector<double> out(cands.size(), 0.0);
    if (pileDraws.empty()) {
        for (size_t ci = 0; ci < cands.size(); ++ci)
            out[ci] = factor * handPts(without(m.hand, cands[ci])) + hz.partCost;
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
int Bot::Impl::valueWithout(const SolveResult& r, int x) const {
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
std::vector<double> Bot::Impl::unopenedRollouts(const Model& m, const std::vector<int>& cands,
                                     const std::vector<std::vector<int>>& pileDraws,
                                     const std::vector<std::vector<int>>& leftDraws, uint64_t budget) const {
    const RulesConfig& rc = g->rules();
    const int thr = g->seriesOpenNeed();
    const int D = pileDraws.empty() ? 0 : (int)pileDraws.front().size();
    const Hazard hz = roundHazard();
    const double unopened = rc.unopenedScore;
    const TableModel tm0 = tableModel(m.table);
    std::vector<double> total(cands.size(), 0.0);
    int done = 0;

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
                ev += surv * roundEnd(hz, unopened);
                surv *= 1.0 - hz.opp - hz.part;
            }
            if (openTurn >= D) {
                total[ci] += ev + surv * (unopened + hz.partCost);
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
                const int worst = dearestPlain(hand);
                if (worst < 0) break;
                removeOne(hand, worst);
            }
            total[ci] += ev + finishRollout(hand, tm, pairsOpen, openTurn + 1, D, surv, hz, P, L);
        }
        ++done;
    }
    std::vector<double> out(cands.size(), unopened + hz.partCost);
    if (done > 0)
        for (size_t ci = 0; ci < cands.size(); ++ci) out[ci] = total[ci] / done;
    return out;
}

// Yandan açma cezası: expected points written on us when the unopened right neighbour takes `x` and opens
// with it (number x10, x20 for a pair opening). Logistic model fitted on bot-vs-bot games (tests/sim
// --feed-log): about 15% of the tiles thrown to an unopened neighbour get used to open, 7% of the 1s and
// 21% of the 12s/13s; a face they threw away themselves is almost never taken.
double Bot::Impl::feedCost(int x) const {
    const RulesConfig& rc = g->rules();
    if (!rc.leftOpenPenalty || know.rOpened || ok->isJoker(x)) return 0.0;
    const int c = ok->faceColor(x), n = ok->faceNumber(x);
    int near = 0, sameNum = 0;
    for (int n2 = std::max(1, n - 2); n2 <= std::min(NUM_NUMBERS, n + 2); ++n2)
        if (n2 != n) near += know.rDiscarded[c][n2];
    for (int c2 = 0; c2 < NUM_COLORS; ++c2)
        if (c2 != c) sameNum += know.rDiscarded[c2][n];
    const double nn = n / 13.0;
    const int pile = g->pileCount();
    double z = -3.60 + 2.54 * nn - 0.885 * nn * nn;
    if (know.rTurns == 0) z += 0.78;
    z += 0.80 * std::min(know.rTurns, 6) / 6.0;
    z += 0.34 * std::clamp(1.0 - pile / 20.0, 0.0, 1.0);
    if (!know.anyOpened) z += 0.39;
    if (know.rDiscarded[c][n]) z -= 2.85;
    z -= 0.225 * std::min(near, 2) + 0.425 * std::min(sameNum, 2);
    if (know.live(c, n) > 0) z += 0.29; // the other copy may be in their hand (a pair or a group)
    if (g->seriesOpenNeed() > rc.openThreshold) z -= 0.31; // katlamalı: harder to open
    const double p = 1.0 / (1.0 + std::exp(-z));
    // a fifth of these openings are pairs (x20)
    return p * n * (know.rDiscarded[c][n] ? 10.0 : 11.3);
}

} // namespace okey
