#pragma once
// King computer opponents. Implementation: src/core/KingBot.cpp.
//
// Fairness: a bot reads ONLY public information from king::Game (contract, trump, current trick, the hand's
// play history g.plays(), totals, choice counts, hand sizes) plus its OWN hand (g.hand(seat)). Voids and
// "cannot hold" facts are inferred from the play history with the same legality filters the engine uses.
//
// Levels:
//   Acemi — simple heuristics, mostly sensible but loose; contract choice by a rough look at the hand.
//   Usta  — per-contract card play (ducking, shedding high cards and penalty cards on other people's tricks,
//           Rıfkı hunting / avoiding, trump drawing, ruffing), card counting and void inference; contract
//           choice by hand evaluation relative to an average hand.
//   Kurt  — determinized Monte Carlo: samples unseen hands consistent with every inferred void / forced-play
//           fact, rolls each candidate out with the Usta policy; contract choice by simulation.
#include "core/BotStyle.h"
#include "core/King.h"

#include <memory>

namespace king {

enum class BotLevel { Acemi = 0, Usta = 1, Kurt = 2 };
using okey::BotStyle; // personality (core/BotStyle.h)

struct BotAction {
    enum class Kind { Choose, Play };
    Kind kind = Kind::Play;
    Contract contract = Contract::ElAlmaz; // Choose
    int trump = -1;                        // Choose + Koz
    int card = -1;                         // Play
};

// Applies `a` for `seat` through the public Game API.
ActionResult applyBotAction(Game& g, int seat, const BotAction& a);
// Always-legal action: the first allowed contract (koz in the longest suit) or the lowest legal card.
BotAction fallbackAction(const Game& g, int seat);

// Hata analizi: Kurt's values. A card: the seat's expected points for the rest of the hand when it is played now
// (sampled deals consistent with the play so far, played out by the Usta policy). A contract: its expected points for
// the chooser over sampled deals minus what that contract is worth to an average hand (the baseline Kurt chooses by),
// so contracts compare fairly. `se`: the standard error of the difference to the best option (paired, 0 for it).
struct CardValue {
    int card = -1;
    double value = 0.0;
    double se = 0.0;
};
struct ContractValue {
    Contract contract = Contract::ElAlmaz;
    int trump = -1; // Koz: the suit
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
    // Personality: a bold bot picks koz games earlier (more readily) and, at Kurt, gambles on the spread of a
    // contract / card; a cautious one prefers penalty games first and, at Kurt, the steadier card. Neutral (the
    // default) = the level's tuned play.
    void setStyle(BotStyle style);
    BotStyle style() const;
    void resetForHand();                              // call at every HandStart (optional: bots are stateless)
    void observe(const GameEvent& e, const Game& g);  // optional; the bot rebuilds what it needs from g

    // The next action for `seat`: a contract when g.stage() == Choosing and g.chooser() == seat, otherwise a
    // card when g.current() == seat. < 30 ms typical at -O2.
    BotAction next(const Game& g, int seat);

    // ---- Hata analizi (Kurt's judgement whatever the level; deterministic for the state, next() is not disturbed) ----
    std::vector<CardValue> evaluateCards(const Game& g, int seat) const;         // Playing, g.current() == seat
    std::vector<ContractValue> evaluateContracts(const Game& g, int seat) const; // Choosing, g.chooser() == seat

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace king
