#pragma once
// What the player's own hand is doing at the table, as the tables tell it (r3d::PlayerHands listens). A table emits a
// cue when the player's tile / card / checker starts to move or is picked up with the mouse, and may hold the object
// back by the hand's lead so the fingers are on it when it leaves (the same idea as World.h's BOT_*_LEAD for the
// regulars). The tables know nothing else about the hands: two std::function hooks, both optional.
#include <raylib.h>

#include <functional>

namespace r3d {

enum class HandCueKind {
    Take,    // an object comes from the table to the player (okey: the pile / the left discard to the istaka)
    Give,    // the player puts an object down (a discard, a meld, a card played, a checker moved)
    Carry,   // the player drags an object with the mouse: the hand follows it loosely while `where` says so
    Dice,    // tavla: the dice are thrown from `from` (they leave the hand after `lead`)
    CardFan, // card games: the player holds the fan (`on`, its frame `frame`) or puts it down
};

struct HandCue {
    HandCueKind kind = HandCueKind::Give;
    Vector3 from{0, 0, 0};    // world: where the object is picked up (Take / Give / Dice)
    float lead = 0.f;         // seconds (at animation speed 1) the table holds the object back for the hand
    // The object's grip point now (world: the top of a tile, a card's near corner, a checker's top); false once it has
    // landed or is no longer the hand's business. Called every frame while the cue runs (the table must outlive it:
    // PlayerHands::reset() drops every cue).
    std::function<bool(Vector3&)> where;
    int style = 0;            // how it is held: 0 a tile, 1 a card, 2 a checker
    bool on = true;           // CardFan: held / put down
    Matrix frame{};           // CardFan: the fan's frame (Cards3D cardlayout::humanFanFrame)
};

using HandCueFn = std::function<void(const HandCue&)>;
// The lead a table should give the player's object (0 when the hands are hidden or switched off).
using HandLeadFn = std::function<float(HandCueKind)>;

} // namespace r3d
