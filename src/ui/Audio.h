#pragma once
// Procedural audio: every sound is synthesised at init (no asset files). PUBLIC API FROZEN.
// Implementation: src/ui/Audio.cpp (audio owner) defines struct Audio::Impl.
// App calls InitAudioDevice() before Audio::init() and CloseAudioDevice() after shutdown().
#include "core/Game.h"

namespace ui {

enum class Sfx {
    TileClick,    // picking up / dropping a tile on the rack (light plastic tick)
    TileDraw,     // sliding a tile off the pile
    TileDiscard,  // tile put down on the felt (clack)
    TileSlam,     // opening a hand — tiles slapped on the table (heavy "şak!")
    Shuffle,      // dealing / shuffling rattle (~1.2 s)
    TeaClink,     // spoon stirring in an ince belli glass
    TeaSip,       // soft slurp
    GlassSet,     // tea glass set on its saucer
    Penalty,      // disappointed low "tsk" / thud
    Win,          // short cheerful flourish (plucked strings)
    Lose,         // short falling phrase
    Button,       // UI button tick
    Error,        // soft negative blip
    Dice,         // backgammon dice from a neighbouring table (tavla zarı)
    Chair,        // chair scrape / creak
    Count
};

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init();          // synthesise sounds + start ambience streams; false if audio unavailable
    void shutdown();
    void update(float dt); // feed streams (ambience, radio), random background events
    void play(Sfx s, float volume = 1.f, float pitch = 1.f);
    void onEvent(const okey::GameEvent& e); // map game events to sounds

    void setSfxEnabled(bool on);
    void setAmbientEnabled(bool on);  // crowd murmur, ceiling fan, distant dice/tavla, TV football
    void setMusicEnabled(bool on);    // old radio: synthesized Turkish folk (makam) melodies, lo-fi
    void setMasterVolume(float v01);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ui
