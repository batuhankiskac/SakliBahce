#pragma once
// What a regular's hand does with a piece on the board or a loose card (Characters::carry, CharactersBoard.cpp): the
// fingers come down on it at path[0] at `lead` seconds, carry it through the rest of `path` (each point `seg` seconds
// after the one before, lifted `hop` metres between them; a dama disc hopping over the ones it takes) and let it go at
// the last point; then each of `takes` (the discs taken off the board) is picked up at `from` at `at` and dropped at
// `to` `dur` later. World space; times at animation speed 1 from the call (they follow setAnimationSpeed), on the same
// clock as the thing carried (Dama3D::playMove fills one in for a hand-paced move).
#include <raylib.h>

#include <vector>

namespace r3d {

struct PieceCarry {
    std::vector<Vector3> path; // the grip point (the top of the disc / the card) where it is taken, then every landing
    float lead = 0.40f;        // the fingers are on it (w3d::BOT_TAKE_LEAD): it starts moving then
    float seg = 0.34f;         // seconds per segment of the path
    float hop = 0.03f;         // lifted this high between two landings
    bool card = false;         // a card (a flatter pinch, it slides off the fingers) rather than a disc
    struct Take {
        Vector3 from{}, to{};
        float at = 0.f, dur = 0.4f;
    };
    std::vector<Take> takes;
};

} // namespace r3d
