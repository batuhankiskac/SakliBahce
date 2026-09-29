#pragma once
// Banter: what the three regulars say at the table (Turkish), with personalities fixed by seat:
//   seat 1 Hacı Rıza   — calm, proverbs;   seat 2 Kel Mahmut — loud football fan, brags;
//   seat 3 Emekli Nuri — grumpy, nostalgic retiree.
// Pure logic (no drawing, no raylib). Owned by the characters module; used by r3d::Characters.
// Rate limited: global gap, per-seat cooldowns and probabilities, so it never spams. Seat 0 (the human)
// never speaks.
#include "core/Game.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>

namespace ui {

enum class BanterCue {
    None,
    TeaOrder,   // somebody called the tea boy
    Goal,       // someone reacted to a goal on the TV
};

struct BanterLine {
    int seat = 1;
    std::string text;
    float delay = 0.f;     // seconds until it should appear
    float seconds = 2.8f;  // bubble duration
    float maxWait = 3.5f;  // drop it if it can't be shown within this time (stale reaction)
    BanterCue cue = BanterCue::None;
};

class Banter {
public:
    void reset(uint64_t seed);
    void setNames(const std::array<std::string, 4>& names);
    void setEnabled(bool on);  // off in title mode (clears pending lines)
    // false while the game is paused or behind a menu: the "hurry up" clock for the human's turn stops (it is
    // not reset — coming back doesn't restart the nagging from the beginning either). Chatter goes on.
    void setGameRunning(bool running);

    // Reactions to the game (only GameEvent fields and inline Game accessors are used).
    void onEvent(const okey::GameEvent& e, const okey::Game& g);
    // Idle chatter, "hurry up" nags, tea orders. `teaLow`: the glasses are nearly empty.
    // `teaBoyBusy`: the tea boy is already on his way.
    void update(float dt, bool teaLow, bool teaBoyBusy);
    // Scene notifications
    void teaServed();                  // the tea boy refilled the glasses
    bool orderTea();                   // someone calls the tea boy (returns true if a line was queued)
    void tvGoal();                     // a goal on the TV
    bool pop(BanterLine& out);         // next line whose delay has elapsed
    void spoke(int seat);              // a bubble actually appeared for `seat`

    // exposed for tests / tools
    static int lineCount();            // total number of distinct lines in the tables

private:
    struct Pending {
        BanterLine line;
    };
    bool enabled_ = true;
    bool gameRunning_ = true;
    uint64_t rng_ = 1;
    std::array<std::string, 4> names_{};
    std::array<float, 4> seatCool_{};     // per-seat cooldown remaining
    float globalCool_ = 0.f;
    float idleIn_ = 18.f;
    float humanWait_ = 0.f;
    int nagLevel_ = 0;
    int turnSeat_ = -1;
    bool handLive_ = false;
    bool lowPileSaid_ = false;
    float teaOrderCool_ = 25.f;
    int lastPick_[128] = {};              // anti-repeat memory per table
    std::deque<std::string> recent_;      // lines said lately (never repeat within a while)
    int lastDialogue_[3] = {-1, -1, -1};
    std::deque<Pending> queue_;

    float rnd();
    int rndInt(int n);
    bool chance(float p) { return rnd() < p; }
    int pickOther(int notSeat);           // a random bot seat != notSeat (1..3)
    int pickBot();
    // Adds a line from table `tableId` for `seat` (templates: {v} value, {p} actor, {h} human address,
    // {n1}/{n2}/{n3} names). `force` ignores cooldowns (important moments).
    bool say(int seat, int tableId, float delay, bool force = false, int value = 0, int actor = -1,
             float maxWait = 3.5f, BanterCue cue = BanterCue::None);
    std::string fill(const std::string& tpl, int speaker, int value, int actor) const;
    bool isRecent(const std::string& text) const;
    void remember(const std::string& text);
    std::string shortName(int seat) const;
    std::string address(int speaker, int target) const;  // how `speaker` calls `target` (0 = the human)
    std::string humanAddress(int speaker) const;
};

} // namespace ui
