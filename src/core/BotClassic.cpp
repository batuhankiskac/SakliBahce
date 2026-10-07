// The klasik okey bots (see Bot.cpp, BotInternal.h).
#include "core/BotInternal.h"

namespace okey {

// ---- klasik okey ----
// A tile id with face (c, n) that is not in `hand` (a sahte okey for the okey's face), or -1.
int Bot::Impl::freeTileOf(int c, int n, const std::vector<int>& hand) const {
    if (c == ok->color && n == ok->number) {
        for (int f = FAKE_JOKER_A; f <= FAKE_JOKER_B; ++f)
            if (!has(hand, f)) return f;
        return -1;
    }
    for (int cp = 0; cp < 2; ++cp)
        if (!has(hand, makeTileId(c, n, cp))) return makeTileId(c, n, cp);
    return -1;
}

std::vector<Bot::Impl::Draw> Bot::Impl::classicDraws(const std::vector<int>& hand) const {
    std::vector<Draw> out;
    for (int c = 0; c < NUM_COLORS; ++c)
        for (int n = 1; n <= NUM_NUMBERS; ++n) {
            const double live = easy() ? 2.0 - know.mine[c][n] : (double)know.live(c, n);
            if (live <= 0) continue;
            const int id = freeTileOf(c, n, hand);
            if (id >= 0) out.push_back({id, live});
        }
    const int liveJ = std::max(0, 2 - know.visibleJokers - know.myJokers);
    if (liveJ > 0)
        for (int cp = 0; cp < 2; ++cp) {
            const int j = makeTileId(ok->color, ok->number, cp);
            if (!has(hand, j)) {
                out.push_back({j, (double)liveJ});
                break;
            }
        }
    return out;
}

// Can a drawn face change anything for `hand`? (same color within two, wrapping 13-1, or same number)
bool Bot::Impl::relevant(int id, const std::vector<int>& hand) const {
    if (ok->isJoker(id)) return true;
    const int c = ok->faceColor(id), n = ok->faceNumber(id);
    for (int h : hand) {
        if (ok->isJoker(h)) continue;
        const int c2 = ok->faceColor(h), n2 = ok->faceNumber(h);
        if (n2 == n) return true;
        if (c2 != c) continue;
        const int d = std::abs(n2 - n);
        if (d <= 2 || d >= 11) return true;
    }
    return false;
}

// How good a 14-tile hand is: tiles in melds now, the chance that the next draw finishes it, and the expected
// gain of that draw (series), or the same for seven pairs — whichever line is better.
double Bot::Impl::classicValue(const std::vector<int>& h14, const std::vector<Draw>& draws, bool lookahead) const {
    const int cover = classicCoverFast(h14, *ok);
    const int pairs = classicPairCount(h14, *ok);
    const double pairW = 2.0 * (1.0 + 0.15 * bk(STYLE_K_CLASSIC_PAIRS)); // bold leans to the seven-pairs line
    if (!lookahead) return std::max<double>(cover, pairs >= 4 ? pairs * pairW : 0.0);
    double wsum = 0, win = 0, gain = 0, pairWin = 0;
    std::vector<int> t = h14;
    t.push_back(-1);
    for (const Draw& d : draws) {
        wsum += d.w;
        if (!relevant(d.id, h14)) continue;
        t.back() = d.id;
        const int c15 = classicCoverFast(t, *ok);
        if (c15 >= 14) win += d.w;
        else gain += d.w * (c15 - cover);
        if (pairs >= 6 && classicPairCount(t, *ok) >= 7) pairWin += d.w;
    }
    if (wsum <= 0) return cover;
    const double series = cover + 0.8 * gain / wsum + CLASSIC_WIN * win / wsum;
    const double pairLine = pairs >= 4 ? pairs * pairW + CLASSIC_WIN * pairWin / wsum : 0.0;
    return std::max(series, pairLine);
}

// Danger of handing `x` to the right neighbour (Kurt / Usta): faces next to what they took from me.
double Bot::Impl::classicDanger(int x) const {
    if (easy()) return 0.0;
    const int c = ok->faceColor(x), n = ok->faceNumber(x);
    double d = 0;
    for (int c2 = 0; c2 < NUM_COLORS; ++c2)
        for (int n2 = 1; n2 <= NUM_NUMBERS; ++n2) {
            const int took = know.rTook[c2][n2];
            if (!took) continue;
            const int dn = std::abs(n2 - n);
            if (c2 == c && (dn <= 2 || dn >= 12)) d += 0.35 * took;
            if (n2 == n && c2 != c) d += 0.3 * took;
        }
    if (know.rDiscarded[c][n]) d -= 0.3;
    if (know.live(c, n) == 0) d -= 0.1;
    return std::max(0.0, d);
}

BotAction Bot::Impl::classicNext(const Game& game, int seat) {
    const std::vector<int>& hand = game.player(seat).hand;
    Rng rng = turnRng(game, seat, 0xC1A5);
    if (game.indicatorTwin(seat) >= 0) return act(BotAction::Kind::ShowIndicator);
    if (game.stage() == TurnStage::NeedDraw) {
        const int L = game.topDiscard(Game::leftOf(seat));
        // (the pile is never empty at a draw: the discard that empties it ends the hand)
        if (L < 0 || !game.canTakeFromLeft(seat)) return act(BotAction::Kind::DrawPile);
        // take the left tile when it finishes the hand or adds to the melds
        std::vector<int> with = hand;
        with.push_back(L);
        if (classicCoverFast(with, *ok) >= 14 || classicPairCount(with, *ok) >= 7)
            for (int x : with)
                if (classicFinishKind(with, x, *ok)) return act(BotAction::Kind::TakeLeft);
        if (ok->isJoker(L)) return act(BotAction::Kind::TakeLeft);
        const int before = classicCoverFast(hand, *ok), after = classicCoverFast(with, *ok);
        const int pb = classicPairCount(hand, *ok), pa = classicPairCount(with, *ok);
        bool take = after > before;
        // going for pairs (a bold bot from three pairs on, a cautious one only from six)
        const double cp = bk(STYLE_K_CLASSIC_PAIRS);
        if (!take && pb >= (cp > 0.5 ? 3 : cp < -0.5 ? 6 : 5) && pa > pb) take = true;
        if (easy() && take && !rng.chance(0.6f)) take = false;
        return act(take ? BotAction::Kind::TakeLeft : BotAction::Kind::DrawPile);
    }
    // finish when possible: the okey as the last tile doubles, pairs double
    int bestFinish = -1, bestMult = 0;
    const bool mayFinish = classicCoverFast(hand, *ok) >= 14 || classicPairCount(hand, *ok) >= 7;
    for (int x : hand) {
        if (!mayFinish) break;
        const int kind = classicFinishKind(hand, x, *ok);
        if (!kind) continue;
        const int mult = (ok->isJoker(x) ? 2 : 1) * (kind == 2 ? 2 : 1);
        if (mult > bestMult) {
            bestMult = mult;
            bestFinish = x;
        }
    }
    if (bestFinish >= 0) {
        BotAction f = act(BotAction::Kind::Finish);
        f.tile = bestFinish;
        return f;
    }
    // discard: best remaining hand (one draw of look-ahead for Kurt), never the okey
    const std::vector<Draw> draws = classicDraws(hand);
    std::vector<int> faces;
    double best = -1e18;
    int bestId = -1;
    for (int x : hand) {
        if (ok->isJoker(x)) continue;
        if (has(faces, faceOf(x))) continue;
        faces.push_back(faceOf(x));
        const std::vector<int> rest = without(hand, x);
        double v = classicValue(rest, draws, hard());
        v -= (hard() ? 1.0 : 0.6) * (1.0 - STYLE_FEED * bk(STYLE_K_CLASSIC_DANGER)) * classicDanger(x);
        if (easy()) v += rng.uniform(0.f, 1.2f);
        v -= 0.002 * ok->faceNumber(x); // ties: let the lower tile go
        if (v > best) {
            best = v;
            bestId = x;
        }
    }
    BotAction d = act(BotAction::Kind::Discard);
    d.tile = bestId >= 0 ? bestId : hand.front();
    return d;
}

} // namespace okey
