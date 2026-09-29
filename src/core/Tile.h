#pragma once
// Tiles of a 101 Okey set. PUBLIC API FROZEN — shared by every module.
//
// 106 physical tiles: 4 colors x 13 numbers x 2 copies (ids 0..103) + 2 "sahte okey" (ids 104, 105).
//   id < 104 : color = id / 26, number = (id % 26) / 2 + 1, copy = id % 2
//   id 104/105: fake joker (sahte okey). It is NOT wild: it plays as the okey's face (color/number).
// The okey (wild joker) of a hand = the tile one above the indicator (gösterge), same color; 13 -> 1.
// The two physical tiles with the okey's face are wild ("okey"); they can stand in for any tile.
#include <string>

namespace okey {

constexpr int NUM_COLORS = 4;
constexpr int NUM_NUMBERS = 13;
constexpr int NUM_TILES = 106;
constexpr int FAKE_JOKER_A = 104;
constexpr int FAKE_JOKER_B = 105;
constexpr int NUM_PLAYERS = 4;

// A "1" placed after 13 in a run (12-13-1) is represented as number 14 and is worth this many points.
constexpr int ACE_HIGH_NUMBER = 14;
constexpr int ACE_HIGH_VALUE = 14;
// Value of an unplayed okey (wild) left in an opened player's hand when the hand is scored.
constexpr int JOKER_IN_HAND_VALUE = 101;

enum TileColor : int { Yellow = 0, Blue = 1, Black = 2, Red = 3 }; // Sarı, Mavi, Siyah, Kırmızı

inline bool isValidTile(int id) { return id >= 0 && id < NUM_TILES; }
inline bool isFakeJoker(int id) { return id >= FAKE_JOKER_A; }
// Physical color/number printed on a numbered tile (only for id < 104).
inline int printedColor(int id) { return id / 26; }
inline int printedNumber(int id) { return (id % 26) / 2 + 1; }
inline int makeTileId(int color, int number, int copy) { return color * 26 + (number - 1) * 2 + copy; }

struct OkeyInfo {
    int indicatorId = -1; // gösterge (never a fake joker)
    int color = 0;        // okey color
    int number = 1;       // okey number

    static OkeyInfo fromIndicator(int indicatorId) {
        OkeyInfo o;
        o.indicatorId = indicatorId;
        o.color = printedColor(indicatorId);
        int n = printedNumber(indicatorId);
        o.number = (n == 13) ? 1 : n + 1;
        return o;
    }
    // True for the two physical wild tiles of this hand.
    bool isJoker(int id) const {
        return id >= 0 && id < FAKE_JOKER_A && printedColor(id) == color && printedNumber(id) == number;
    }
    // Face a non-wild tile plays as: fake jokers take the okey's face.
    int faceColor(int id) const { return isFakeJoker(id) ? color : printedColor(id); }
    int faceNumber(int id) const { return isFakeJoker(id) ? number : printedNumber(id); }
    // Points of a tile left in hand at scoring time.
    int handValue(int id) const { return isJoker(id) ? JOKER_IN_HAND_VALUE : faceNumber(id); }
};

inline const char* colorNameTR(int c) {
    switch (c) {
    case Yellow: return "Sarı";
    case Blue: return "Mavi";
    case Black: return "Siyah";
    case Red: return "Kırmızı";
    default: return "?";
    }
}

// e.g. "Kırmızı 7", "Okey", "Sahte Okey (Mavi 4)"
inline std::string tileNameTR(int id, const OkeyInfo& ok) {
    if (!isValidTile(id)) return "?";
    if (ok.isJoker(id)) return "Okey";
    if (isFakeJoker(id))
        return std::string("Sahte Okey (") + colorNameTR(ok.color) + " " + std::to_string(ok.number) + ")";
    return std::string(colorNameTR(printedColor(id))) + " " + std::to_string(printedNumber(id));
}

} // namespace okey
