// Hata analizi: the bot's judgement of a decision (Bot::evaluate*, see Bot.h; internals in BotInternal.h).
#include "core/BotInternal.h"

namespace okey {

// ---- hata analizi (Bot::evaluate*) ----
void Bot::Impl::prepareAnalysis(const Game& game, int seat) {
    g = &game;
    ok = &game.okey();
    seatNow = seat;
    buildKnowledge(game, seat);
    workStart = detail::solverWork();
}

// One tile per face of the hand (okeys as one face), the pending left tile excluded.
std::vector<int> Bot::Impl::faceReps(const std::vector<int>& hand, int pending) const {
    std::vector<int> reps;
    for (int id : hand) {
        if (id == pending) continue;
        bool dup = false;
        for (int r : reps) {
            dup = faceKey(r) == faceKey(id);
            if (dup) break;
        }
        if (!dup) reps.push_back(id);
    }
    return reps;
}

int Bot::Impl::repIndex(const std::vector<int>& reps, int id) const {
    for (size_t i = 0; i < reps.size(); ++i) {
        const int r = reps[i];
        if (r == id || faceKey(r) == faceKey(id)) return (int)i;
    }
    return -1;
}

// Expected final score of the hand after discarding each candidate now: the rollouts of the level's look-ahead
// over every candidate (no pre-selection), plus the penalties the discard writes at once (okey, işlek tile) and
// the expected yandan açma cezası.
std::vector<double> Bot::Impl::analysisValues(const Model& m, const std::vector<int>& cands, uint64_t budget) const {
    const RulesConfig& rc = g->rules();
    const int D = ownDraws();
    std::vector<std::vector<int>> pileD, leftD;
    if (D >= 1) {
        Rng mc = turnRng(*g, m.seat, m.opened ? 0xA5A1 : 0xA5A2);
        const auto both = sampleDraws(m.opened ? OPENED_SAMPLES : UNOPENED_SAMPLES, 2 * D, mc);
        if (!splitDraws(both, D, pileD, leftD)) {
            pileD.clear();
            leftD.clear();
        }
    }
    std::vector<double> ev = m.opened ? openedRollouts(m, cands, pileD, leftD, budget)
                                      : unopenedRollouts(m, cands, pileD, leftD, budget);
    for (size_t i = 0; i < cands.size(); ++i) {
        const int x = cands[i];
        if (ok->isJoker(x)) {
            if (rc.penaltyJokerDiscard) ev[i] += rc.penalty;
        } else {
            if (rc.penaltyPlayableDiscard && fitsAnyMeld(m.table, x, *ok)) ev[i] += rc.penalty;
            ev[i] += feedCost(x);
        }
    }
    return ev;
}

// The best line after a draw (no pending tile): the best discard, or Kurt's opening now and the best discard.
double Bot::Impl::bestAfterDraw(const Model& m, uint64_t budget) const {
    double best = 1e18;
    if (m.hand.size() > 1) {
        const std::vector<double> v = analysisValues(m, faceReps(m.hand, -1), budget);
        for (double x : v) best = std::min(best, x);
    }
    if (!m.opened) {
        const Opening op = chooseOpening(m, false);
        if (op.valid) {
            Model mo = m;
            BotAction a = act(BotAction::Kind::Open);
            a.melds = op.melds;
            if (applyToModel(mo, a)) {
                if (mo.hand.size() <= 1) {
                    best = std::min(best, finishScore(mo, op.pairs));
                } else {
                    const std::vector<double> v = analysisValues(mo, faceReps(mo.hand, -1), budget);
                    for (double x : v) best = std::min(best, x);
                }
            }
        }
    }
    return best == 1e18 ? (double)g->rules().unopenedScore : best;
}

bool Bot::Impl::evaluateDrawImpl(const Game& game, int seat, double& take, double& pile) {
    const int L = game.topDiscard(Game::leftOf(seat));
    if (L < 0 || !game.canTakeFromLeft(seat) || game.player(seat).opened || game.stage() != TurnStage::NeedDraw)
        return false;
    const RulesConfig& rc = game.rules();
    Model base = modelOf(game, seat);
    base.stage = TurnStage::Play;
    base.canWork = false;
    // the pile: a few sampled unseen tiles
    constexpr int PILE_SAMPLES = 6;
    Rng r = turnRng(game, seat, 0xD4A3);
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k < PILE_SAMPLES && !know.unseen.empty(); ++k) {
        Model m = base;
        m.hand.push_back(know.unseen[(size_t)r.range((int)know.unseen.size())]);
        sum += bestAfterDraw(m, LOOKAHEAD_BUDGET / 2);
        ++n;
    }
    pile = n > 0 ? sum / n : (double)rc.unopenedScore;
    // the left tile: it must go into the opening (else it is given back)
    Model m = base;
    m.hand.push_back(L);
    m.pending = L;
    const Opening op = chooseOpening(m, true);
    take = pile + (rc.penaltyReturnLeft ? rc.penalty : 0);
    if (op.valid) {
        BotAction a = act(BotAction::Kind::Open);
        a.melds = op.melds;
        if (applyToModel(m, a) && m.pending < 0) {
            if (m.hand.size() <= 1) {
                take = finishScore(m, op.pairs);
            } else {
                take = 1e18;
                for (double x : analysisValues(m, faceReps(m.hand, -1), LOOKAHEAD_BUDGET)) take = std::min(take, x);
            }
        }
    }
    return true;
}

// Klasik okey: chance to finish with the draws left, after discarding each candidate (the most promising faces by
// the bot's own hand value, plus `must`). Each sampled future draws the seat's tiles from the unseen ones; the
// hand keeps the best tiles (cover / pairs) after every draw.
std::vector<std::pair<int, double>> Bot::Impl::classicChances(const Game& game, int seat, int must) const {
    const std::vector<int>& hand = game.player(seat).hand;
    const std::vector<Draw> draws = classicDraws(hand);
    std::vector<std::pair<double, int>> ranked;
    for (int x : faceReps(hand, -1)) {
        if (ok->isJoker(x)) continue;
        const double v = classicValue(without(hand, x), draws, true) - classicDanger(x);
        ranked.push_back({-v, x});
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<int> cands;
    for (size_t i = 0; i < ranked.size() && cands.size() < 4; ++i) cands.push_back(ranked[i].second);
    if (must >= 0 && has(hand, must)) {
        bool in = false;
        for (int c : cands) in = in || repIndex({c}, must) == 0;
        if (!in) cands.push_back(must);
    }
    const int pile = game.pileCount();
    const int H = pile <= 0 ? 0 : std::max(1, std::min(6, pile / 4));
    std::vector<std::pair<int, double>> out;
    if (H == 0 || know.unseen.empty()) {
        for (int c : cands) out.push_back({c, 0.0});
        return out;
    }
    constexpr int S = CLASSIC_SAMPLES;
    Rng r = turnRng(game, seat, 0xC1A55);
    const std::vector<std::vector<int>> futures = sampleDraws(S, H, r);
    auto canFinish = [&](const std::vector<int>& h15) {
        return classicCoverFast(h15, *ok) >= 14 || classicPairCount(h15, *ok) >= 7;
    };
    for (int c : cands) {
        int wins = 0;
        for (const std::vector<int>& fut : futures) {
            std::vector<int> h = without(hand, c);
            for (int t : fut) {
                if (!relevant(t, h)) continue; // a tile that changes nothing goes straight back
                h.push_back(t);
                if (canFinish(h)) {
                    ++wins;
                    break;
                }
                double best = -1e18;
                int drop = -1;
                for (int y : faceReps(h, -1)) {
                    if (ok->isJoker(y)) continue;
                    const std::vector<int> rest = without(h, y);
                    const double v = classicValue(rest, draws, false) - 0.002 * ok->faceNumber(y);
                    if (v > best) {
                        best = v;
                        drop = y;
                    }
                }
                if (drop < 0) drop = h.front();
                removeOne(h, drop);
            }
        }
        out.push_back({c, (double)wins / (double)futures.size()});
    }
    return out;
}


std::vector<Bot::TileValue> Bot::evaluateDiscards(const Game& g, int seat) {
    std::vector<TileValue> out;
    if (g.classic() || g.handState() != HandState::Playing || g.current() != seat || g.stage() != TurnStage::Play ||
        g.pendingLeftTile() >= 0 || g.player(seat).hand.size() <= 1)
        return out;
    Impl& im = *impl_;
    im.prepareAnalysis(g, seat);
    const Model m = im.modelOf(g, seat);
    const std::vector<int> reps = im.faceReps(m.hand, -1);
    const std::vector<double> v = im.analysisValues(m, reps, 3 * LOOKAHEAD_BUDGET);
    for (int id : m.hand) {
        const int i = im.repIndex(reps, id);
        out.push_back({id, i >= 0 ? v[(size_t)i] : 0.0});
    }
    return out;
}

bool Bot::openingNow(const Game& g, int seat, std::vector<std::vector<int>>& melds) {
    melds.clear();
    if (g.classic() || g.handState() != HandState::Playing || g.current() != seat || g.stage() != TurnStage::Play ||
        g.player(seat).opened)
        return false;
    Impl& im = *impl_;
    im.prepareAnalysis(g, seat);
    const Model m = im.modelOf(g, seat);
    const Opening op = im.chooseOpening(m, m.pending >= 0);
    if (!op.valid || !g.checkOpen(seat, op.melds).valid) return false;
    melds = op.melds;
    return true;
}

bool Bot::evaluateDraw(const Game& g, int seat, double& takeLeft, double& drawPile) {
    if (g.classic() || g.handState() != HandState::Playing || g.current() != seat) return false;
    Impl& im = *impl_;
    im.prepareAnalysis(g, seat);
    return im.evaluateDrawImpl(g, seat, takeLeft, drawPile);
}

std::vector<Bot::TileValue> Bot::evaluateClassicDiscards(const Game& g, int seat, int mustInclude) {
    std::vector<TileValue> out;
    if (!g.classic() || g.handState() != HandState::Playing || g.current() != seat || g.stage() != TurnStage::Play)
        return out;
    Impl& im = *impl_;
    im.prepareAnalysis(g, seat);
    for (const auto& cv : im.classicChances(g, seat, mustInclude))
        out.push_back({cv.first, cv.second, std::sqrt(cv.second * (1.0 - cv.second) / CLASSIC_SAMPLES)});
    return out;
}

double Bot::classicKeepValue(const Game& g, int seat, int tile) {
    if (!g.classic() || g.handState() != HandState::Playing || seat < 0 || seat >= NUM_PLAYERS) return 0.0;
    Impl& im = *impl_;
    im.prepareAnalysis(g, seat);
    const std::vector<int>& hand = g.player(seat).hand;
    if (!has(hand, tile)) return 0.0;
    const double danger = (1.0 - STYLE_FEED * im.bk(STYLE_K_CLASSIC_DANGER)) * im.classicDanger(tile);
    return im.classicValue(without(hand, tile), im.classicDraws(hand), true) - danger;
}


} // namespace okey
