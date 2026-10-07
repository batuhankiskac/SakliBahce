// The 101 bots' table work after opening: lay, işle, swap okeys, finish (see Bot.cpp, BotInternal.h).
#include "core/BotInternal.h"

namespace okey {

BotAction Bot::Impl::addAct(int tile, int meld, AddSide side) {
    BotAction a = act(BotAction::Kind::AddToMeld);
    a.tile = tile;
    a.meld = meld;
    a.side = side;
    return a;
}

// How attractive it is to leave an okey at this spot (fewer live copies of the tile it stands for =
// fewer opponents able to swap it out).
int Bot::Impl::jokerSpotSafety(const Meld& before, const Meld& after) const {
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
int Bot::Impl::findAddSpot(const Model& m, int tile, AddSide& side) const {
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

bool Bot::Impl::usePending(Model& m, const Emit& emit) const {
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
            if (sameFace(id, L)) {
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

bool Bot::Impl::swapOne(Model& m, const Emit& emit) const {
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
bool Bot::Impl::layPairsToo(Model& m, const Emit& emit) const {
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

bool Bot::Impl::layBest(Model& m, const Emit& emit) const {
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
bool Bot::Impl::holdJoker(const Model& m) const {
    if (!hard()) return false;
    int nonJ = 0, j = 0;
    for (int id : m.hand) (ok->isJoker(id) ? j : nonJ)++;
    if (j == 0 || nonJ > 3) return false;
    // not when the hand may end before the next turn
    return g->pileCount() >= 4 && know.minOpenedOppHand > 2;
}

bool Bot::Impl::isleOne(Model& m, const Emit& emit) const {
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
bool Bot::Impl::placeAllExcept(const Model& m, int keep, std::vector<BotAction>& out) const {
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

bool Bot::Impl::tryFinish(Model& m, const Emit& emit) const {
    if (m.hand.size() <= 1) return false;
    for (int t : finishTries(m.hand, -1, false)) {
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
bool Bot::Impl::workTable(Model& m, const Emit& emit, Rng& rng) const {
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
double Bot::Impl::outlook(const std::vector<int>& base, const std::vector<int>& fullHand) const {
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
            const int id = freeTileOf(c, n, fullHand);
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

} // namespace okey
