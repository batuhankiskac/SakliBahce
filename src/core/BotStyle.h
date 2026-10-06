#pragma once
// Playing personality of a computer opponent, on top of its level (Acemi / Usta / Kurt). Shared by every card and
// tile game's bots (okey::Bot, batak::Bot, king::Bot take it through setStyle()).
//
// boldness: -1 cautious .. 0 neutral .. +1 bold. The neutral style is exactly the tuned behaviour of the level; a
// style only moves a few visible decision knobs (how readily a bot takes a tile or a contract, how much it fears
// feeding the next player, how high it bids), so strength stays within a few percent of neutral.
//
// The three people at the table:
//   seat 1 Hacı Rıza   calm, solid, balanced    (neutral, the reference)
//   seat 2 Kel Mahmut  bold, aggressive         (takes risks, wants to finish fast, overbids)
//   seat 3 Emekli Nuri cautious, defensive      (avoids risk, avoids feeding the next player, underbids)
// Seat 0 is the human; when the AI plays it, it plays neutral.

namespace okey {

struct BotStyle {
    float boldness = 0.f; // -1 cautious .. +1 bold

    bool neutral() const { return boldness == 0.f; }
    static BotStyle bold() { return BotStyle{1.f}; }
    static BotStyle cautious() { return BotStyle{-1.f}; }

    // The preset of the person sitting at `seat` (0 = the human's seat: neutral).
    static BotStyle forSeat(int seat) {
        switch (seat) {
        case 2: return bold();     // Kel Mahmut
        case 3: return cautious(); // Emekli Nuri
        default: return BotStyle{}; // Hacı Rıza, and the AI in the human's seat
        }
    }
};

} // namespace okey
