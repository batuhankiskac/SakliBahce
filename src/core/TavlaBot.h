#pragma once
// Tavla computer opponents. Implementation: src/core/TavlaBot.cpp.
//
// Fairness: everything in tavla is public except future dice. A bot reads only the public Game state
// (position, dice of the current turn, steps played this turn, legal steps) and never touches the Rng.
//
// Levels:
//   Easy   (Acemi): legal but loose: a sloppy evaluation (underestimates hits, no prime/anchor sense) with
//                   noise, and now and then just one of its 4 best-looking plays.
//   Normal (Usta) : 1-ply: every complete play is scored by a full heuristic with hand-set weights (pip race,
//                   shot-counted blots, made points, primes, anchors, home board vs the bar, stacking, race
//                   wastage / bear-off efficiency, mars risk).
//   Hard   (Kurt) : the same heuristic with self-play-tuned weights, plus 2-ply: the best candidates are
//                   searched over the opponent's 21 rolls, the opponent answering each with his best reply
//                   (expectimax). Typical decision ~1 ms, p99 ~16 ms, worst < 60 ms at -O2 on an M-series Mac.
//
// Gülbahar / Fevga (Rules::variant): the same three levels on their own evaluation (no hits there): the pip race,
// the blocks each side holds in front of the other's checkers (runs of points up to a full 6-run, the dice values
// his checkers lose), stacking, getting off the start point, checkers off and the mars risk; a pure race as in
// klasik. Acemi sees mostly pips; Usta hand-set weights; Kurt tuned weights plus the same 2-ply (in Gülbahar the
// opponent's doubles are searched as one rung of the ladder). Every bot reads the çeşit from Game::variant().
//
// Katlama zarı (Rules::doubling), offer / take / drop:
//   Acemi: a crude pip-count feeling with noise: doubles on a big pip lead now and then, takes almost anything.
//   Usta : a win probability from the pip race alone (normal approximation over the rolls left): doubles in
//          the 70-90% window, takes from 25%.
//   Kurt : the win probability of its full evaluation (calibrated logistic in contact, the race formula on
//          wastage-adjusted counts), mars chances both ways (too good to double: plays on for the mars), the
//          take point from the cubeless equity, and the match score: no dead doubles, free takes when a drop
//          loses the match, the trailer doubles at once after the Crawford game.
//   Usta and Kurt never double when the cube's current value already wins them the match.
#include "core/Tavla.h"

#include <memory>

namespace tavla {

enum class BotLevel { Easy = 0, Normal = 1, Hard = 2 }; // Acemi, Usta, Kurt

struct BotAction {
    enum class Kind { None, OpeningRoll, Roll, Step, EndTurn, Double, Take, Drop };
    Kind kind = Kind::None; // None: nothing to do for this player now (not his turn / game over)
    int from = -1, to = -1, die = 0; // Step
};

// Applies `a` for player p through the public Game API (convenience for the App/tests).
ActionResult applyBotAction(Game& g, int p, const BotAction& a);
// Always-legal fallback: OpeningRoll / Roll when due, the first legal step while Moving, EndTurn when the
// turn is complete (Rules::confirmTurn), Take when p must answer a double, otherwise None.
BotAction fallbackAction(const Game& g, int p);

// Kurt's static evaluation (exposed for tests / hints): score of `pos` for player p, assuming p has just
// moved and the opponent is to roll. Roughly in pips; a finished game is +-1000 per point.
double botEvaluate(const Position& pos, int p, Variant v = Variant::Klasik);
// Kurt's estimate of p's chance to win the game from `pos` with p to roll (0..1; tests / hints / the sim).
double botWinProbability(const Position& pos, int p, Variant v = Variant::Klasik);
// Hata analizi: Kurt's equity of player p right after he moved to `pos` (the opponent to roll): cubeless, per cube
// unit (+-1 a game, +-2 a mars). depth 1: straight from the position's chances; depth 2: averaged over the opponent's
// 21 rolls, each answered by his best reply (as Kurt searches). `win` (optional) receives p's chance to win the game.
double botEquityAfterMove(const Position& pos, int p, int depth = 2, double* win = nullptr, Variant v = Variant::Klasik);
// Kurt's cubeless equity (and win chance) of p with p to roll.
double botEquityToRoll(const Position& pos, int p, double* win = nullptr, Variant v = Variant::Klasik);

class Bot {
public:
    explicit Bot(BotLevel level = BotLevel::Normal, uint64_t seed = 1);
    ~Bot();
    Bot(Bot&&) noexcept;
    Bot& operator=(Bot&&) noexcept;
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    void setLevel(BotLevel level);
    BotLevel level() const;
    void resetForGame();                              // call at every GameStart (drops any plan)
    void observe(const GameEvent& e, const Game& g);  // optional; currently only drops a stale plan

    // Next single action for player p (also his answer to a double offered to him). Called repeatedly; after
    // each successful application the state changes and next() continues. In Moving the bot computes a complete play once (at the first call of
    // the turn) and replays it one Step at a time; if the state is not what the plan expected (rejected or
    // changed by the caller) it re-plans from the current state.
    BotAction next(const Game& g, int p);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tavla
