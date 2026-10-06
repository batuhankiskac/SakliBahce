#pragma once
// Computer opponents. PUBLIC API FROZEN. Implementation: src/core/Bot.cpp (AI owner), which defines
// struct Bot::Impl.
//
// Fairness: a bot may read ONLY public information from Game (table, discard piles, discard history,
// pile count, okey/indicator, players' opened flags/scores) plus its OWN hand (g.player(seat).hand).
#include "core/BotStyle.h"
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
    // Personality (core/BotStyle.h); the default neutral style is the level's tuned play.
    void setStyle(BotStyle style);
    BotStyle style() const;
    void resetForHand();                            // call at every HandStart
    void observe(const GameEvent& e, const Game& g); // every event, for memory (optional use)

    // Next single action for `seat` (== g.current()). Called repeatedly during the bot's turn; after each
    // successful application the state changes and next() continues. A turn always ends with Discard.
    // Must be fast (< 30 ms typical).
    BotAction next(const Game& g, int seat);

    // ---- Hata analizi (src/app/Analysis): the bot's judgement of a decision of `seat` (== g.current()), from what
    //      that seat knew (public information + its own hand). Meant for a Kurt kept for the analysis; deterministic
    //      for the bot's seed and the state, and independent of next() (no plan is touched). ----
    struct TileValue {
        int tile = -1;
        // 101: expected score of the hand for the seat (lower is better) when this tile is discarded now: Kurt's
        // rollouts, plus the penalty it costs at once (okey / işlek) and the expected yandan açma cezası.
        // Klasik okey: the chance (0..1) that the hand is finished with the draws left in the pile.
        double value = 0.0;
        double se = 0.0; // klasik okey: the standard error of `value` (sampling)
    };
    // 101, Play stage, no left tile pending, more than one tile in hand: every tile of the hand (one entry per id).
    std::vector<TileValue> evaluateDiscards(const Game& g, int seat);
    // 101, not opened yet, Play stage: the opening Kurt would lay now (melds valid for Game::openHand). False: none.
    bool openingNow(const Game& g, int seat, std::vector<std::vector<int>>& melds);
    // 101, NeedDraw, not opened, a left tile to take: expected hand score after the best play that follows taking
    // the left tile (it must be used to open) and after drawing from the pile (averaged over sampled unseen tiles).
    bool evaluateDraw(const Game& g, int seat, double& takeLeft, double& drawPile);
    // Klasik okey, Play stage: the most promising discards (and `mustInclude`), each with its finishing chance.
    std::vector<TileValue> evaluateClassicDiscards(const Game& g, int seat, int mustInclude = -1);
    // Klasik okey, Play stage: Kurt's quick value of the hand left after discarding `tile` (tiles in melds, the next
    // draw's chances, the danger of feeding the right neighbour): the screen before evaluateClassicDiscards.
    double classicKeepValue(const Game& g, int seat, int tile);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace okey
