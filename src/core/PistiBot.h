#pragma once
// Pişti computer opponents. Implementation: src/core/PistiBot.cpp.
//
// Fairness: a bot reads ONLY public information from Game — the table (face-up cards, closed count),
// seen() (every card played or revealed this hand), buriedCards(), deck and hand COUNTS, captured piles,
// dealer, last capturer, scores — plus its OWN hand (g.hand(seat)). Never other hands or the deck order.
//
// Levels:
//   Acemi: takes the table when it can (also wastes Valeler on small piles), otherwise a random-ish low card.
//   Usta:  card counting (unknown copies of each rank), one-ply risk evaluation of every card: keeps Valeler
//          for piles worth it, avoids leaving an easy pişti, protects point cards; team-aware in eşli.
//   Kurt:  determinized Monte Carlo: samples the unseen cards consistently with what is known (hand/deck/
//          closed counts, buried opening Valeler at the bottom of the deck = the dealer's last cards),
//          plays the rest of the hand with the Usta policy for everyone, and picks the card with the best
//          expected point difference (own side vs the others) incl. majority and the last-capture cards.
#include "core/Pisti.h"
#include <memory>

namespace pisti {

enum class BotLevel { Acemi = 0, Usta = 1, Kurt = 2 };

// Always-legal fallback (a card from the seat's hand): the lowest-value card that does not hand over the
// table, -1 if the seat has no card or it is not its turn.
int fallbackCard(const Game& g, int seat);

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
    void resetForHand();                              // call at every HandStart
    void observe(const GameEvent& e, const Game& g);  // every event (optional; keeps its own memory)

    // The card `seat` (== g.current()) plays now; always a card of g.hand(seat). Typical < 30 ms.
    int next(const Game& g, int seat);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pisti
