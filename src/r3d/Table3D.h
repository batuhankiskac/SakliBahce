#pragma once
// Our okey table in 3D and everything the players manipulate: table furniture, green felt, the four
// istakas, all 106 tiles (3D, face textures from ui::tilegfx's atlas), pile, indicator, discard piles,
// melds, tile flight animations, the human's interaction by mouse ray picking, and the 2D HUD (buttons,
// status line, live meld counters, toasts, name plates projected above heads, confirm modal).
// PUBLIC API FROZEN. Implementation: src/r3d/Table3D*.cpp — table owner (struct Table3D::Impl).
#include "core/Game.h"
#include "r3d/Gfx.h"
#include "ui/Audio.h"
#include "ui/Common.h"
#include <array>
#include <functional>
#include <string>

namespace r3d {

class Table3D {
public:
    Table3D();
    ~Table3D();
    Table3D(const Table3D&) = delete;
    Table3D& operator=(const Table3D&) = delete;

    bool init(Renderer& r, okey::Game* game, int humanSeat = 0); // after InitWindow + ui::loadFonts
    void shutdown(Renderer& r);
    void onHandStart();                     // rebuild racks from the game (after a HandStart event)
    void onEvent(const okey::GameEvent& e); // every game event: animations, toasts, sounds

    // cam: the human's current camera (ray picking). mouse: ui::virtualMouse(). humanInput: false while bots
    // move, during overlays, or in autoplay (the rack can still be rearranged only when true).
    void update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput);
    void submit(Renderer& r);               // table + racks + tiles (+ highlights) before Renderer::render
    void drawHUD(const Renderer& r);        // 2D HUD in ui virtual coords (inside App's BeginMode2D)

    bool isAnimating() const;               // tiles in flight — App waits before the next bot action
    void toast(const std::string& text, Color c = ui::pal::TextLight, float seconds = 2.6f);
    bool consumeMenuRequest();              // "Menü" button pressed (true once)
    void setAnimationSpeed(float speed);    // 1 = normal
    void setHints(bool on);                 // live meld totals / valid-group highlights (default on)
    void setHeadAnchors(const std::array<Vector3, 4>& heads); // name plates float above these
    bool mouseBusy() const;                 // mouse is over/dragging a tile, button or modal (no mouse-look)
    std::function<void(ui::Sfx)> playSfx;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// The human's first-person seated camera (table owner, src/r3d/PlayerCamera.cpp).
class PlayerCamera {
public:
    void reset();                            // default seated view (w3d::EYE, EYE_PITCH_DEG, FOVY_DEG)
    // allowLook: right-mouse drag looks around (yaw ±110°, pitch -75..+35), wheel zooms (FOV 35..70),
    // R or double right-click recentres (smoothly). Subtle breathing sway. Title mode: slow cinematic
    // drift around the room instead of player control.
    void update(float dt, bool allowLook);
    void setTitleMode(bool on);
    void glanceAt(Vector3 target, float seconds); // gentle involuntary glance (e.g. at a speaker); optional use
    Camera3D camera() const;
    float yawDeg() const;
    float pitchDeg() const;

private:
    float yaw_ = 0.f, pitch_ = -27.f, fov_ = 66.f;
    float targetYaw_ = 0.f, targetPitch_ = -27.f, targetFov_ = 66.f;
    float time_ = 0.f, titleT_ = 0.f;
    bool title_ = false;
    Vector3 glance_{0, 0, 0};
    float glanceT_ = 0.f;
    Vector2 lastMouse_{0, 0};
    float lastRightClick_ = -10.f;
};

} // namespace r3d
