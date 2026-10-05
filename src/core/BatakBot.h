#pragma once
// Computer opponents for İhaleli / Eşli Batak. Pure logic, deterministic for a given seed and call sequence.
//
// Driving a turn: the seat to act is g.current(); the one who decides is g.controllerOf(g.current()) (the
// declarer also plays the open dummy in eşli). Call bots[controller].next(g, g.current()) and apply it with
// applyAction(g, g.current(), action). If an action were ever rejected, fallbackAction() is always legal.
//
// Fairness: a bot reads only public information from Game (bids, the koz, played cards and tricks, hand
// sizes, the open dummy's cards) plus its OWN hand. Voids and "could not beat / could not over-trump"
// inferences come from the public trick history.
//
// Levels:
//   Acemi  counts high cards for the ihale, plays the highest winner or the lowest card.
//   Usta   hand evaluation (honours, koz length, voids/singletons), card counting, void inference,
//          partner-aware play in eşli.
//   Kurt   Monte Carlo: samples the unseen cards consistently with voids and bids, plays the hand out with
//          the Usta policy for every candidate card (card play) and every koz (ihale) and picks the best
//          expected score.
#include "core/Batak.h"
#include <memory>

namespace batak {

enum class Level { Acemi = 0, Usta = 1, Kurt = 2 };

const char* levelNameTR(Level l); // "Acemi" / "Usta" / "Kurt"

struct Action {
    enum class Kind { Bid, Pass, ChooseTrump, Play };
    Kind kind = Kind::Pass;
    int value = 0; // Bid
    int suit = -1; // ChooseTrump
    int card = -1; // Play
};

// Applies `a` for `seat` (== g.current()) through the public Game API.
ActionResult applyAction(Game& g, int seat, const Action& a);
// Always-legal action for g.current(): pas / the longest suit as koz / the lowest legal card.
Action fallbackAction(const Game& g, int seat);
// Usta's estimate of the tricks a 13-card hand takes as declarer with `trump` (tekli; eşli adds the partner).
double estimateTricks(uint64_t hand, int trump);
// The koz with the best estimate.
int bestTrumpFor(uint64_t hand);

class Bot {
public:
    explicit Bot(Level level = Level::Usta, uint64_t seed = 1);
    ~Bot();
    Bot(Bot&&) noexcept;
    Bot& operator=(Bot&&) noexcept;
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    void setLevel(Level level);
    Level level() const;
    void resetForHand();                             // call at every HandStart (optional: bots are stateless)
    void observe(const GameEvent& e, const Game& g); // optional; the bot rebuilds its memory from Game

    // Next action for `seat` (== g.current(); the bot must be controllerOf(seat)): a bid or pas while
    // Bidding, a koz while ChoosingTrump, a card while Playing. < 30 ms typical.
    Action next(const Game& g, int seat);

    // tests/tools only: tuning knobs. 0 Usta ihale margin (tekli), 1 Kurt ihale margin (tekli), 2 Kurt rollouts
    // per card decision, 3 Kurt deals per ihale decision, 4 Kurt deals per koz choice, 5 eşli partner share,
    // 6 Usta ihale margin (eşli), 7 Kurt ihale margin (eşli).
    void debugTune(int key, double value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace batak
