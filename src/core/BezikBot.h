#pragma once
// Bezik computer opponents. Implementation: src/core/BezikBot.cpp.
//
// Fairness: a bot reads only public information from bezik::Game (both players' declared cards on the table, the
// tricks, the turned-up koz card, the stock's size, the cards everyone saw go into a hand: Game::exposed) and its OWN
// concealed hand.
//
// Levels:
//   Acemi — plays a loose version of Usta's policy (often a random card), declares the biggest combination but now
//           and then forgets to.
//   Usta  — a "keep value" for every card (koz, brisks, the combinations it can still make), wins a trick when what it
//           brings (brisks, a combination waiting to be declared, the first draw) is worth more than the card it
//           costs, throws its least useful card otherwise; declares the biggest combination; plays the second stage
//           greedily (the cheapest card that wins, else the lowest).
//   Kurt  — determinized Monte Carlo in the first stage: deals the unseen cards into the opponent's hand and the
//           stock many times and plays every candidate card out to the end of the deal with Usta's policy; declares in
//           the order that scores most (koz evliliği before the koz serisi, a single bezik before the double while the
//           stock lasts); solves the second stage exactly (everything is known then).
#include "core/Bezik.h"
#include "core/BotStyle.h"

#include <memory>
#include <string>
#include <vector>

namespace bezik {

enum class BotLevel { Acemi = 0, Usta = 1, Kurt = 2 };
using okey::BotStyle;

struct BotAction {
    enum class Kind { Play, Declare, Koz7, Pass };
    Kind kind = Kind::Play;
    int card = -1;   // Play
    Meld meld;       // Declare
};

ActionResult applyBotAction(Game& g, int seat, const BotAction& a);
BotAction fallbackAction(const Game& g, int seat); // always legal: the first legal card / pass

// Hata analizi: Kurt's value of each card the seat may play now (the seat's points minus the opponent's for the rest
// of the deal, sampled deals played out by Usta's policy; exact in the second stage). `se`: standard error of the
// difference to the best card (paired samples; 0 for the best).
struct CardValue {
    int card = -1;
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
    // Personality: bold (Kel Mahmut) spends a card to win a trick more readily, cautious keeps its cards.
    void setStyle(BotStyle style);
    BotStyle style() const;

    // The next action for `seat` (g.current() == seat, stage Playing or Declare). Kurt: under ~0.3 s.
    BotAction next(const Game& g, int seat);

    // Hata analizi (Kurt's judgement whatever the level; deterministic for the state).
    std::vector<CardValue> evaluateCards(const Game& g, int seat, int samples = 160) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Usta's "keep value" of a card for `seat` now (exposed for tests and the analysis' explanations).
double keepValue(const Game& g, int seat, int card);

} // namespace bezik
