#pragma once
// Altmışaltı (66) computer opponents. Implementation: src/core/AltmisaltiBot.cpp.
//
// Fairness: a bot reads only public information from altmisalti::Game (the plays, the koz card under the stock,
// points, tricks, the shown cards — a declared marriage's other half, the koz taken by exchange or with the last
// draw — and what the strict rules showed the other one cannot hold) plus its own hand, through Game::viewOf().
//
// Levels:
//   Acemi — a loose version of the Usta rules: often just a playable card, never closes the stock.
//   Usta  — heuristics: low leads, koz kept for the big cards (an As or a 10 is taken with a small koz), marriages
//           declared at once, the koz 9 exchanged, the stock closed when the sure tricks reach 66, the strict endgame
//           played by counting the top cards.
//   Kurt  — determinized Monte Carlo: the unseen cards are dealt at random consistent with every public fact; each
//           option (every card, closing) is played out by the Usta rules until the stock is closed or used up, and
//           from there the endgame is solved exactly (alpha-beta over both hands). Once the stock is used up every
//           card is known and Kurt plays the endgame perfectly.
#include "core/Altmisalti.h"
#include "core/BotStyle.h"

#include <memory>
#include <vector>

namespace altmisalti {

enum class BotLevel { Acemi = 0, Usta = 1, Kurt = 2 };
using okey::BotStyle; // personality (core/BotStyle.h)

struct BotAction {
    enum class Kind { Play, Exchange, Close };
    Kind kind = Kind::Play;
    int card = -1; // Play
};

// Applies `a` for player `p` through the public Game API.
ActionResult applyBotAction(Game& g, int p, const BotAction& a);
// Always legal: the lowest legal card.
BotAction fallbackAction(const Game& g, int p);

// Kurt's judgement of an option: the expected game points of the hand for the player (won +, lost -), over sampled
// deals; `se` the standard error of the difference to the best option (paired, 0 for it).
struct ActionValue {
    BotAction action;
    double value = 0.0;
    double se = 0.0;
};

class Bot {
public:
    explicit Bot(BotLevel level = BotLevel::Usta, uint64_t seed = 1);
    ~Bot();
    Bot(Bot&&) noexcept;
    Bot& operator=(Bot&&) noexcept;
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    void setLevel(BotLevel level);
    BotLevel level() const;
    // Personality: a bold bot closes the stock sooner (on fewer sure points), a cautious one later.
    void setStyle(BotStyle style);
    BotStyle style() const;

    // The next action of `p` (g.current() == p): exchanging the koz 9 comes first when it is worth it, then closing
    // or a card. Kurt: well under 0.3 s.
    BotAction next(const Game& g, int p);
    // Hata analizi / İpucu: Kurt's values of every option now (closing the stock when allowed, every legal card),
    // whatever the level; deterministic for the state.
    std::vector<ActionValue> evaluate(const Game& g, int p) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace altmisalti
