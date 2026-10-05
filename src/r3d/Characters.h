#pragma once
// Every person in the kıraathane: the three opponents at our table (seats 1..3) with their chairs and
// personal props (tea glasses / oralet, cigarette, tespih, the human's own tea glass at GLASS_POS[0] too),
// background patrons at w3d::BG_TABLES, and the tea boy (çaycı) who walks around serving tea.
// Animations react to game events (reaching for tiles, slapping melds down, sipping tea, smoking,
// talking, celebrating). Speech bubbles use ui::Banter and are drawn as a 2D overlay. PUBLIC API FROZEN.
// Implementation: src/r3d/Characters*.cpp — characters owner (struct Characters::Impl).
#include "core/Game.h"
#include "r3d/Gfx.h"
#include "ui/Audio.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace r3d {

class Characters {
public:
    Characters();
    ~Characters();
    Characters(const Characters&) = delete;
    Characters& operator=(const Characters&) = delete;

    bool init(Renderer& r, uint64_t seed);
    void shutdown(Renderer& r);
    void setNames(const std::array<std::string, 4>& names); // seat 0 = the human (never drawn, never talks)
    void setActiveSeat(int seat);                           // whose turn (-1 none)
    void onEvent(const okey::GameEvent& e, const okey::Game& g);
    void onTvGoal();                                        // someone reacts to the TV
    void onCatMeow(Vector3 where);                          // heads turn to the kahvehane cat
    void say(int seat, const std::string& text, float seconds = 3.f);
    // The other games (tavla, the card games), which have no okey events: seat 1..3 reaches out — mode 0 takes
    // something from `target` to their hand, 1 puts something from their hand down at `target` (timed like the
    // tiles: w3d::BOT_TAKE_LEAD / BOT_GIVE_LEAD). `mood`: 0 none, 1 happy, 2 grumpy, 3 surprised.
    void reach(int seat, Vector3 target, int mode);
    void react(int seat, int mood, Vector3 lookAt);
    // A line for the banter system's rate limits (the other games' reactions): queued like Banter lines.
    bool chat(int seat, const std::string& text, bool important = false);
    void setTitleMode(bool on);                             // menu backdrop: no bubbles, relaxed idles
    void setAnimationSpeed(float speed);                    // Table3D's tile speed: reaches for tiles keep pace

    // `viewer` is the human's camera (for eye contact / glances toward the player).
    void update(float dt, const Camera3D& viewer);
    void submit(Renderer& r);             // all people + chairs at our table + personal props, before render()
    void drawOverlay(const Renderer& r);  // speech bubbles in ui virtual coords (inside App's BeginMode2D)
    Vector3 headPosition(int seat) const; // world position of each head (seat 0: approx. the camera)
    std::function<void(ui::Sfx)> playSfx;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace r3d
