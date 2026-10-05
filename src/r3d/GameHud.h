#pragma once
// The 2D HUD kit of the non-okey games (tavla, the card games), in the okey table's look: name plates beside the
// opponents' heads, a status line, toasts, a column of buttons on the right (the game's own + "Yapay Zeka" +
// "Menü") and a centred choice panel (ihale, koz, contract). Drawing records clicks; the game reads them with
// takeClicks() in its next update (the same frame order as Table3D).
#include "r3d/Gfx.h"
#include "ui/Common.h"

#include <array>
#include <string>
#include <vector>

namespace r3d {

class GameHud {
public:
    struct Badge {
        std::string text;
        Color color;
    };
    struct Plate {
        bool show = false;
        std::string name;
        std::string label;           // "Toplam", "Puan", "El"
        std::string value;           // right side, e.g. "12"
        std::vector<Badge> badges;   // second line chips
        bool turn = false;
    };
    struct Click {
        int id;
        int value;
    };

    void update(float dt);
    void toast(const std::string& text, Color c = ui::pal::TextLight, float seconds = 2.6f);
    void clearToasts() { toasts_.clear(); }

    // ---- drawing (inside App's BeginMode2D, after the 3D frame) ----
    void plates(const Renderer& r, const std::array<Vector3, 4>& heads, const std::array<Plate, 4>& p);
    // Status pill at the bottom: main text (highlighted on the player's turn) and optional coloured parts.
    void status(const std::string& text, Color c, bool myTurn, const std::vector<std::pair<std::string, Color>>& parts = {},
                bool aiTag = false);
    // The button column, bottom right. `labels` are the game's own (top to bottom); "Yapay Zeka" (lit while
    // `aiOn`) and "Menü" follow. Clicks: id = index into labels, or AI_ID / MENU_ID.
    static constexpr int AI_ID = 1000, MENU_ID = 1001;
    void buttons(Vector2 mouse, const std::vector<std::string>& labels, const std::vector<bool>& enabled,
                 const std::vector<bool>& glow, bool aiOn);
    // A centred panel with a title, a subtitle and rows of buttons; clicks report id = PANEL_ID + index.
    static constexpr int PANEL_ID = 2000;
    struct PanelButton {
        std::string label;
        bool enabled = true;
        bool selected = false;
        std::string hint;            // small text under the label
    };
    void panel(Vector2 mouse, const std::string& title, const std::string& subtitle,
               const std::vector<PanelButton>& buttons, int columns, float y = 330.f, float buttonW = 120.f);
    void toasts();
    // Text box at a projected point (e.g. a speech-free label over the middle of the table).
    void label3D(const Renderer& r, Vector3 world, const std::string& text, Color c, float size = 16.f);

    std::vector<Click> takeClicks();
    // Where the last drawn frame put its buttons (tools/tables_check clicks them): game buttons in order, then
    // "Yapay Zeka" and "Menü"; the panel's buttons with their enabled flags.
    const std::vector<Rectangle>& buttonRects() const { return buttonRects_; }
    const std::vector<Rectangle>& panelRects() const { return panelRects_; }
    const std::vector<bool>& panelEnabled() const { return panelEnabled_; }
    bool mouseOverHud() const { return hover_; }
    void beginFrame() {
        hover_ = false;
        buttonRects_.clear();
        panelRects_.clear();
        panelEnabled_.clear();
    }

private:
    struct Toast {
        std::string text;
        Color color;
        float age = 0.f, dur = 2.6f;
    };
    std::vector<Toast> toasts_;
    std::vector<Click> clicks_;
    bool hover_ = false;
    float now_ = 0.f;
    std::vector<Rectangle> buttonRects_, panelRects_;
    std::vector<bool> panelEnabled_;
};

} // namespace r3d
