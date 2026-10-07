#pragma once
// Dama computer opponents. Implementation: src/core/DamaBot.cpp.
//
// Fairness: everything in dama is public; a bot reads the public Game state only (board, player to move, the game's
// position history for repetitions).
//
// All levels search with alpha-beta (negamax, iterative deepening, a transposition table, captures searched on past
// the horizon) over a material + position evaluation:
//   man 100, dama 300 (+ its open lines), the men's advance (more the nearer they are to crowning), the centre files, a
//   man guarded from behind or beside, the home row kept while the other side still has men, trades when ahead.
//   Acemi (Easy)  : 2 plies, every root move's score blurred by +-45 and now and then just one of its 3 best moves.
//   Usta (Normal) : up to 6 plies, 40 000 nodes.
//   Kurt (Hard)   : up to 24 plies within 400 000 nodes (about 0.15 s on an M-series Mac).
// Every budget is counted in nodes, never on the clock: a seed always gives the same game, on any machine and load.
// BotStyle (okey::BotStyle): bold (Kel Mahmut) pushes its men and trades more readily, careful (Emekli Nuri) keeps
// its home row; neutral is the tuned evaluation.
#include "core/BotStyle.h"
#include "core/Dama.h"

#include <memory>

namespace dama {

enum class BotLevel { Easy = 0, Normal = 1, Hard = 2 }; // Acemi, Usta, Kurt

constexpr int WIN_SCORE = 100000; // a won position (minus the plies to it)

struct SearchInfo {
    int depth = 0;     // the last completed iteration
    long nodes = 0;
    double ms = 0.0;
    int score = 0;     // of the chosen move, centi-pieces from the mover's side
};

// The static evaluation of `b` for player p (centi-pieces: a man = 100), neutral style.
int botEvaluate(const Board& b, int p);
// Scores of `moves` (legal moves of p on `b`) after a fixed-depth search each (p's view, centi-pieces; +-WIN_SCORE
// for a forced result). For the analysis and hints. `nodeCap` bounds the work per move.
std::vector<int> botScoreMoves(const Board& b, int p, const std::vector<Move>& moves, int depth, long nodeCap = 200000);

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
    void setStyle(const okey::BotStyle& s);
    // The move for player p (p == g.current(), Stage::Playing): always one of g.legalMoves(). A single legal move is
    // played without searching.
    Move choose(const Game& g, int p);
    const SearchInfo& lastSearch() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dama
