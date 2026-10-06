#pragma once
// Audio: the sound effects and the room ambience are synthesised at init; the old radio plays real recordings
// from assets/music/ (listed with their credits in assets/music/liste.tsv), falling back to synthesised makam
// melodies when that folder is missing. PUBLIC API FROZEN (additions only).
// Implementation: src/ui/Audio.cpp (audio owner) defines struct Audio::Impl.
// App calls InitAudioDevice() before Audio::init() and CloseAudioDevice() after shutdown().
#include "core/Game.h"

#include <string>

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
    CarPass,      // a car hissing past on the street outside (wet tyres on rainy nights), from the left
    Meow,         // the kahvehane cat, from across the room ("mrrp", "miyav")
    CardPlace,    // a playing card put down on the felt (papery "fft-tap")
    CardSlap,     // a card slapped down hard (pişti, a trick taken with a flourish)
    CardShuffle,  // shuffling and dealing a deck (riffle, bridge)
    DiceThrow,    // our own tavla dice thrown on the board (close, dry)
    Checker,      // a tavla checker (pul) set down on the board (wooden "tok")
    CardSlide,    // a dealt card sliding across the felt (a soft papery hiss)
    CardGather,   // a trick / the middle pushed together and squared (a shuffle of cards, a tap)
    CardSnap,     // a tossed card landing flat with a snap
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
    void setMusicEnabled(bool on);    // the old radio on the wall (recordings from assets/music)
    void setMasterVolume(float v01);
    void setRain(float amount01);     // rain outside the windows (ambience bus): 0 = a dry night
    // A regular's murmur for a speech bubble that just appeared: friendly gibberish synthesised from the Turkish text
    // (its vowels, consonants, pauses and punctuation; at most ~2.5 s), in the speaker's own voice, panned to where he
    // sits. `voice`: 1 Hacı Rıza, 2 Kel Mahmut, 3 Emekli Nuri (the seats), 4 a patron in the crowd, 5 the çaycı.
    // A new line of the same voice fades the previous one out. Main thread; renders the line at once (< 1 ms).
    void speak(int voice, const std::string& text);
    void setVoicesEnabled(bool on);   // Settings::voices
    // True once when the radio starts a new recording: `text` = "Title — Artist" (for a "now playing" note).
    bool consumeNowPlaying(std::string& text);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ui
