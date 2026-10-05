#pragma once
// Klasik (düz) okey hands: is a hand finished, and how close is it? Used by the engine (Game::finishHand), the
// UI (rack arrangement, the "Bitir" check) and the bots. Pure logic.
//
// Klasik okey melds (unlike 101):
//  * Run   : 3+ tiles of one color with consecutive numbers; 1 may also follow 13 (12-13-1), but only as the
//            last tile (13-1-2 is not a run).
//  * Group : 3 or 4 tiles of one number, all different colors.
//  * A finished hand is 14 tiles split completely into runs/groups, or 7 pairs (identical faces).
//  * The okey (wild) stands for any tile; sahte okeys play as the okey's face.
#include "core/Tile.h"
#include <vector>

namespace okey {

// Makes `ids` a klasik run (any order; jokers fill gaps and ends). Returns the ids in run order, or empty.
std::vector<int> classicRun(const std::vector<int>& ids, const OkeyInfo& ok);
bool classicMeldValid(const std::vector<int>& ids, const OkeyInfo& ok); // a run or a group

// 14 tiles split completely into runs/groups. `melds` (optional) receives one split, each meld in display order.
bool classicSetsComplete(const std::vector<int>& tiles, const OkeyInfo& ok,
                         std::vector<std::vector<int>>* melds = nullptr);
// Number of pairs (identical faces; an okey pairs with any tile). `pairs` (optional) receives them.
int classicPairCount(const std::vector<int>& tiles, const OkeyInfo& ok, std::vector<std::vector<int>>* pairs = nullptr);
bool classicPairsComplete(const std::vector<int>& tiles, const OkeyInfo& ok); // 14 tiles = 7 pairs

// Most tiles of `tiles` that fit in runs/groups at once (the rest are left over). `melds` receives that split.
int classicCover(const std::vector<int>& tiles, const OkeyInfo& ok, std::vector<std::vector<int>>* melds = nullptr);

// The same count, fast (a cache shared by all calls on this thread); okeys left after the real tiles are counted as
// joining some meld. For evaluation (bots); classicCover/classicSetsComplete stay exact.
int classicCoverFast(const std::vector<int>& tiles, const OkeyInfo& ok);

// How a 15-tile hand can finish by discarding `tile`: 0 = it can't; 1 = sets; 2 = pairs.
int classicFinishKind(const std::vector<int>& hand15, int tile, const OkeyInfo& ok);

} // namespace okey
