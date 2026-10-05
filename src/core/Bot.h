#pragma once
// Computer opponents. PUBLIC API FROZEN. Implementation: src/core/Bot.cpp (AI owner), which defines
// struct Bot::Impl.
//
// Fairness: a bot may read ONLY public information from Game (table, discard piles, discard history,
// pile count, okey/indicator, players' opened flags/scores) plus its OWN hand (g.player(seat).hand).
#include "core/Game.h"
#include <memory>

namespace okey {

enum class BotLevel { Easy = 0, Normal = 1, Hard = 2 }; // Acemi, Usta, Kurt

struct BotAction {
    enum class Kind { DrawPile, TakeLeft, ReturnLeft, Open, LayMelds, AddToMeld, SwapJoker, Discard,
                      Finish,          // klasik okey: finish by putting `tile` down (Game::finishHand)
                      ShowIndicator }; // klasik okey: show the gösterge's twin
    Kind kind = Kind::DrawPile;
    std::vector<std::vector<int>> melds; // Open / LayMelds
    int tile = -1;                       // AddToMeld / SwapJoker / Discard / Finish
    int meld = -1;                       // AddToMeld / SwapJoker target table index
    AddSide side = AddSide::Auto;
};

// Applies `a` for `seat` through the public Game API (convenience for App/tests).
ActionResult applyBotAction(Game& g, int seat, const BotAction& a);
// Always-legal fallback when a bot action was rejected: DrawPile if stage==NeedDraw (or ReturnLeft if a
// left tile is pending and unusable), otherwise Discard of the highest-value non-joker tile that is
// not işlek if possible.
BotAction fallbackAction(const Game& g, int seat);

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
    void resetForHand();                            // call at every HandStart
    void observe(const GameEvent& e, const Game& g); // every event, for memory (optional use)

    // Next single action for `seat` (== g.current()). Called repeatedly during the bot's turn; after each
    // successful application the state changes and next() continues. A turn always ends with Discard.
    // Must be fast (< 30 ms typical).
    BotAction next(const Game& g, int seat);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace okey
