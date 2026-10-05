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
#include "core/Tavla.h"

#include <memory>

namespace tavla {

enum class BotLevel { Easy = 0, Normal = 1, Hard = 2 }; // Acemi, Usta, Kurt

struct BotAction {
    enum class Kind { None, OpeningRoll, Roll, Step, EndTurn };
    Kind kind = Kind::None; // None: nothing to do for this player now (not his turn / game over)
    int from = -1, to = -1, die = 0; // Step
};

// Applies `a` for player p through the public Game API (convenience for the App/tests).
ActionResult applyBotAction(Game& g, int p, const BotAction& a);
// Always-legal fallback: OpeningRoll / Roll when due, the first legal step while Moving, EndTurn when the
// turn is complete (Rules::confirmTurn), otherwise None.
BotAction fallbackAction(const Game& g, int p);

// Kurt's static evaluation (exposed for tests / hints): score of `pos` for player p, assuming p has just
// moved and the opponent is to roll. Roughly in pips; a finished game is +-1000 per point.
double botEvaluate(const Position& pos, int p);

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

    // Next single action for player p. Called repeatedly; after each successful application the state
    // changes and next() continues. In Moving the bot computes a complete play once (at the first call of
    // the turn) and replays it one Step at a time; if the state is not what the plan expected (rejected or
    // changed by the caller) it re-plans from the current state.
    BotAction next(const Game& g, int p);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tavla
