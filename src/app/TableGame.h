#pragma once
// The games that are not okey (tavla, Pişti, Batak, King), as App sees them. Each one owns its engine, bots,
// 3D pieces and HUD; App keeps the room, the people, the camera, the screens and the flow (score sheet between
// hands, final standings) and drives the active game through this interface. The okey games keep their own path
// (okey::Game + Table3D).
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/HandCue.h"
#include "ui/Audio.h"
#include "ui/Screens.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

struct TableContext {
    r3d::Renderer* renderer = nullptr;
    r3d::Characters* characters = nullptr;
    std::function<void(ui::Sfx)> sfx;      // plays a sound now
    // The player's own hands (r3d::PlayerHands, HandCue.h; both optional): the table tells them what the player's
    // card / checker / dice do, and holds the object back by the lead so the fingers are on it first.
    r3d::HandCueFn handCue;
    r3d::HandLeadFn handLead;
};

class TableGame {
public:
    virtual ~TableGame() = default;

    virtual bool init(const TableContext& ctx) = 0;  // meshes, textures (after the window is up)
    virtual void shutdown() = 0;
    // A new match: rules from the settings, seat names (0 = the player), seed for the deals / dice.
    virtual void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) = 0;
    virtual void startNextHand() = 0;

    // `humanInput`: the table takes the player's clicks; `aiSeat`: a bot plays the player's seat (Yapay Zeka mode or
    // --autoplay). Bots are paced here too.
    virtual void update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput, bool aiSeat) = 0;
    virtual void submit(r3d::Renderer& r) = 0;
    virtual void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) = 0;

    virtual bool handOver() const = 0;    // the hand (game, deal) just ended: App shows the sheet
    virtual bool matchOver() const = 0;   // ... and that was the last one
    virtual bool animating() const = 0;   // pieces still moving (App lets the end of a hand be seen)
    virtual ui::SheetModel sheet(bool aiMode) const = 0;
    virtual std::string scoreTitle() const = 0;             // the wall chalkboard
    virtual std::vector<std::string> scoreLines() const = 0;
    virtual int activeSeat() const = 0;   // whose turn (-1 none): the people look at them
    virtual bool mouseBusy() const = 0;   // the mouse is over a card / button (no mouse-look)
    virtual std::vector<int> seats() const { return {0, 1, 2, 3}; } // who plays (tavla: 0 and the opponent)
    // Where it is played: 0 our okey table, 1 the tavla table (w3d::tavlaFrame; App moves the camera, the opponent,
    // the glasses and the key light there).
    virtual int location() const { return 0; }
    // (Konken son kalan) The player is out of the match and watches the rest at speed: App moves the score sheets on by
    // itself, as in the Yapay Zeka mode.
    virtual bool spectating() const { return false; }
    // (Konken son kalan) The player had a seat in the hand that just ended (false: he had burned and watched): the
    // stats count only the hands he played.
    virtual bool humanInHand() const { return true; }

    virtual void setLevel(int level) = 0;          // 0 Acemi, 1 Usta, 2 Kurt (the opponents)
    virtual void setAnimationSpeed(float s) = 0;
    virtual void setHints(bool on) = 0;

    // One-shot HUD requests
    virtual bool consumeMenuRequest() = 0;
    virtual bool consumeAiToggleRequest() = 0;
    virtual void toast(const std::string& text, Color c, float seconds) = 0;
    virtual std::string lastLogLine() const { return {}; } // autoplay log after a hand
    // Save / resume: the match as lines of text (its rules are in App's save; typically the engine's action log), and
    // the same match, just started again by startMatch with the saved seed, brought to that point. False: not supported.
    virtual bool saveState(std::vector<std::string>& lines) const { (void)lines; return false; }
    virtual bool restoreState(const std::vector<std::string>& lines) { (void)lines; return false; }
    // Maç tekrarı: in replay mode no bot and no player moves; replayStep applies one saved action line with the usual
    // animations: 1 done, 0 not now (try again later, e.g. while the sheet is up), -1 does not apply (the replay
    // ends). setReplayMode returns false when the game cannot be replayed.
    virtual bool setReplayMode(bool on) { (void)on; return false; }
    virtual int replayStep(const std::string& line) { (void)line; return -1; }
    // The player's score of the hand that just ended, for the record ("En iyi el"); false: none.
    virtual bool humanHandScore(int& score) const { (void)score; return false; }
    // tools/tables_check: a point on the virtual canvas the player could click right now for a legal move (a card,
    // a point of the board, a panel or HUD button), after drawHUD() has run this frame. False: nothing to do.
    virtual bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const { (void)r; (void)out; return false; }
    // tools/tables_check: a short name for what this frame's HUD shows that is worth a picture ("" = nothing special);
    // with --out the first frame of every new name is saved.
    virtual std::string debugPhase() const { return {}; }

    // ---- Başarımlar: what just happened at the table that may open a badge, by the badge's event name ("pisti",
    // "mars", "batak13", ...: the list is in ui/Achievements.h). App takes them every frame and counts them only in a
    // match the player plays himself (App::recording()); a table just notes what the player did, in play and in
    // replays alike (not while restoring a saved match: that path animates nothing and notes nothing).
    std::vector<std::string> achievementEvents() {
        std::vector<std::string> out;
        out.swap(achievementEvents_);
        return out;
    }

protected:
    void noteAchievement(const char* ev) { achievementEvents_.emplace_back(ev); }

private:
    std::vector<std::string> achievementEvents_;
};

// The game behind a ui::GameKind that isn't an okey game (nullptr for the okey games / not built yet).
std::unique_ptr<TableGame> makeTableGame(ui::GameKind kind);

} // namespace app
