#pragma once
// Hand solver: best partition of tiles into melds. Used by bots and by the UI "Seri Diz"/"Çift Diz"
// buttons. PUBLIC API FROZEN. Implementation: src/core/Solver.cpp (AI owner).
#include "core/Meld.h"
#include <vector>

namespace okey {

struct SolveResult {
    bool feasible = true;                // false only when mustUse can't be placed in any meld
    std::vector<std::vector<int>> melds; // each accepted by makeMeld(ids, ok, m, pairMode) in THIS order
                                         // (runs ascending with jokers at their positions)
    std::vector<int> leftovers;          // tiles not used in melds
    int value = 0;                       // series: total meld value; pairs: number of pairs
    int tilesUsed = 0;
};

// Maximum total VALUE partition into runs/groups (ties: more tiles used, then fewer jokers used).
// mustUse >= 0: only partitions that place that tile in a meld (else feasible=false).
// Performance target: <= 5 ms typical, <= 50 ms worst case for 23 tiles with 2 jokers (-O2).
SolveResult solveSeries(const std::vector<int>& tiles, const OkeyInfo& ok, int mustUse = -1);

// Maximum number of pairs (identical faces; a joker pairs with anything, preferring the highest leftover).
SolveResult solvePairs(const std::vector<int>& tiles, const OkeyInfo& ok, int mustUse = -1);

// Rack display order for the UI buttons: melds first (each group = one contiguous block on the rack),
// then leftovers sorted by color then number (jokers last).
struct RackArrangement {
    std::vector<std::vector<int>> melds;
    std::vector<int> leftovers;
};
RackArrangement arrangeSeries(const std::vector<int>& tiles, const OkeyInfo& ok);
RackArrangement arrangePairs(const std::vector<int>& tiles, const OkeyInfo& ok);

} // namespace okey
