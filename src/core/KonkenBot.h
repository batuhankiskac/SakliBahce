#pragma once
// Konken computer opponents. Implementation: src/core/KonkenBot.cpp.
//
// Fairness: a bot reads ONLY public information from konken::Game (the table, the discard pile and its history — who
// threw what and who took it —, the stock size, hand sizes, opened flags, totals) plus its OWN hand (g.hand(seat)).
//
// Levels (the turn is the same machinery: draw, open / lay / add / take jokers, discard — every step checked against the
// engine's own rules first, so a bot never sends a rejected action):
//   Acemi — lays what a greedy look finds, rarely takes the discard, throws its biggest loose card (now and then a
//           worse one), never thinks about the next player.
//   Usta  — the exact best meld partition of its hand (bestPartition), takes the discard whenever it can use it, keeps
//           cards by their live chances (pairs and near-runs counted against the cards still unseen), sheds points as
//           the hand gets dangerous and does not hand the next player a card that fits the table.
//   Kurt  — Usta's machinery, plus: remembers what everyone threw and picked up (what the next player wants or let go),
//           and chooses its discard by rollouts: it samples its future draws from the unseen cards and plays each
//           candidate to the end of the hand (open at the first chance, lay and add everything, shed the dearest loose
//           card) against opponents who finish with a hazard that grows as their hands shrink. Deterministic for a
//           seed; a decision takes a few tens of milliseconds.
#include "core/BotStyle.h"
#include "core/Konken.h"

#include <memory>

namespace konken {

enum class BotLevel { Acemi = 0, Usta = 1, Kurt = 2 };
using okey::BotStyle;

struct BotAction {
    enum class Kind { DrawStock, TakeDiscard, ReturnDiscard, Lay, Add, Swap, Discard };
    Kind kind = Kind::DrawStock;
    std::vector<std::vector<int>> melds; // Lay
    int card = -1;                       // Add / Swap / Discard
    int meld = -1;                       // Add / Swap
    Side side = Side::Auto;              // Add
};

// Applies `a` for `seat` through the public Game API.
ActionResult applyBotAction(Game& g, int seat, const BotAction& a);
// Always legal: draw from the stock (Draw), give an unusable discard back, else discard the dearest card that may go.
BotAction fallbackAction(const Game& g, int seat);
// A short Turkish description of a bot action ("Kupa 7'yi at", "Yerden al", "Aç (63)").
std::string describeAction(const Game& g, int seat, const BotAction& a);

// Hata analizi: Kurt's value of each possible discard — the seat's expected points of the hand (lower is better) when it
// throws that card now, from rollouts, plus what feeding the next player is expected to cost. `se`: the standard error
// of the difference to the best one (paired samples).
struct DiscardValue {
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
    // Personality: a bold bot fears feeding the next player less and keeps more cards for big melds; a cautious one sheds
    // points sooner and guards the next player more. Neutral (the default) = the level's tuned play.
    void setStyle(BotStyle style);
    BotStyle style() const;

    // The next single action for `seat` (== g.current(), stage Draw or Play). Called again after every applied action;
    // a turn always ends with a discard (or a finish).
    BotAction next(const Game& g, int seat);

    // ---- Hata analizi (Kurt's judgement whatever the level; deterministic for the state, next() is not disturbed) ----
    // Play stage, nothing pending: every card `seat` may discard now.
    std::vector<DiscardValue> evaluateDiscards(const Game& g, int seat) const;
    // Not opened yet, Play stage: the opening Kurt would lay now (false: none possible).
    bool openingNow(const Game& g, int seat, std::vector<std::vector<int>>& melds) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace konken
