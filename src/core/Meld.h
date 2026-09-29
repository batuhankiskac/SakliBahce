#pragma once
// Melds ("per"): runs (seri), groups (aynı sayı farklı renk) and pairs (çift).
// PUBLIC API FROZEN. Implementation: src/core/Meld.cpp (engine owner). You may add internal helpers
// in the .cpp only.
//
// Rules (see DESIGN.md §2):
//  * Run   : >= 3 tiles, same color, consecutive numbers 1..13; a "1" may follow 13 (12-13-1, represented
//            as number 14 = ACE_HIGH_NUMBER, worth ACE_HIGH_VALUE). No wrap-around (13-1-2 is invalid).
//            Max length 14 (1..13 + high ace is impossible since the same '1' can't be at both ends, but
//            two different physical '1's could: 1..13,1 is legal, length 14).
//  * Group : 3 or 4 tiles, same number, all different colors.
//  * Pair  : exactly 2 tiles with identical face (same color AND number).
//  * Okey (wild, ok.isJoker(id)) can stand for any tile. Fake jokers are NOT wild: they play as the okey face.
//  * Value : sum of the represented numbers (joker counts as the number it represents; a high ace = 14).
#include "core/Tile.h"
#include <string>
#include <vector>

namespace okey {

enum class MeldKind { Run, Group, Pair };

// A tile as it sits inside a meld: physical id plus the face it represents in this meld.
struct PlacedTile {
    int id = -1;
    int color = 0;      // represented color
    int number = 1;     // represented number (runs: 1..14, 14 = '1' after 13)
    bool joker = false; // physical wild okey tile
};

struct Meld {
    MeldKind kind = MeldKind::Run;
    int owner = -1;                // seat that laid it
    std::vector<PlacedTile> tiles; // Run: ascending by number. Group: color order. Pair: 2 tiles.

    int value() const;             // sum of represented values (number; 14 -> ACE_HIGH_VALUE)
    bool hasJoker() const;
    std::vector<int> ids() const;  // physical ids in display order
    int size() const { return (int)tiles.size(); }
};

// ---- Building / validating from an ordered selection (e.g. a contiguous group on the player's rack) ----
// `why` (optional) receives a short Turkish reason on failure, e.g. "Seri aynı renkten olmalı".
//
// makeRun: positional semantics — the i-th tile has number start+i (ascending) or start-i (descending;
//   result is normalised to ascending). Jokers take the number of their position.
//   If `lenient` is true and the strict positional reading fails, jokers are re-placed freely:
//   non-jokers sorted, internal gaps filled with jokers, remaining jokers appended at the high end
//   (while <= 14 and legal), then at the low end. Returns the highest-value legal reading.
bool makeRun(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, bool lenient = true,
             std::string* why = nullptr);
// makeGroup: 3..4 tiles; non-jokers share the number and have distinct colors; jokers take the missing
//   colors (lowest color index first). Order of `out.tiles`: by represented color.
bool makeGroup(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, std::string* why = nullptr);
// makePair: exactly 2 tiles; identical face, or at least one joker (joker copies the other tile's face;
//   two jokers = pair of the okey face).
bool makePair(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, std::string* why = nullptr);
// makeMeld: pairMode -> makePair. Otherwise tries makeGroup and makeRun (lenient) and returns the
//   higher-value valid interpretation.
bool makeMeld(const std::vector<int>& ids, const OkeyInfo& ok, Meld& out, bool pairMode,
              std::string* why = nullptr);

// ---- İşleme (adding to melds already on the table) ----
enum class AddSide { Auto, Front, Back };
// Add tile `id` to run/group `m` (pairs never accept tiles). Runs: Back = after the highest tile,
// Front = before the lowest. Auto = Back if legal else Front (a joker prefers Back unless the run already
// ends at 14). Groups ignore `side`. Owner is preserved. Returns false if illegal.
bool tryAddTile(const Meld& m, int id, const OkeyInfo& ok, AddSide side, Meld& out);
// Replace a joker in run/group `m` with the real tile `id` whose face matches what that joker represents
// (groups: any missing color of the group's number is accepted when the group has a joker).
// On success `out` is the updated meld and `freedJoker` the joker's physical id.
bool trySwapJoker(const Meld& m, int id, const OkeyInfo& ok, Meld& out, int& freedJoker);
// True if `id` could be added (not swapped) to at least one run/group in `table` ("işlek taş").
bool fitsAnyMeld(const std::vector<Meld>& table, int id, const OkeyInfo& ok);

} // namespace okey
