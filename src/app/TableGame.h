#pragma once
// The games that are not okey (tavla, Pişti, Batak, King), as App sees them. Each one owns its engine, bots,
// 3D pieces and HUD; App keeps the room, the people, the camera, the screens and the flow (score sheet between
// hands, final standings) and drives the active game through this interface. The okey games keep their own path
// (okey::Game + Table3D).
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
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
    virtual std::vector<int> seats() const { return {0, 1, 2, 3}; } // who plays (tavla: 0 and 2)

    virtual void setLevel(int level) = 0;          // 0 Acemi, 1 Usta, 2 Kurt (the opponents)
    virtual void setAnimationSpeed(float s) = 0;
    virtual void setHints(bool on) = 0;

    // One-shot HUD requests
    virtual bool consumeMenuRequest() = 0;
    virtual bool consumeAiToggleRequest() = 0;
    virtual void toast(const std::string& text, Color c, float seconds) = 0;
    virtual std::string lastLogLine() const { return {}; } // autoplay log after a hand
    // tools/tables_check: a point on the virtual canvas the player could click right now for a legal move (a card,
    // a point of the board, a panel or HUD button), after drawHUD() has run this frame. False: nothing to do.
    virtual bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const { (void)r; (void)out; return false; }
};

// The game behind a ui::GameKind that isn't an okey game (nullptr for the okey games / not built yet).
std::unique_ptr<TableGame> makeTableGame(ui::GameKind kind);

} // namespace app
