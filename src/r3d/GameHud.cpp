// The HUD kit of the non-okey games (see GameHud.h). Same proportions and colours as Table3DHud.
#include "r3d/GameHud.h"

#include "ui/TileRender.h"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace r3d {

using ui::FontId;
namespace pal = ui::pal;

namespace {

Color rgba(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}
Color fadeC(Color c, float a) {
    c.a = (unsigned char)std::clamp(c.a * a + 0.5f, 0.f, 255.f);
    return c;
}
std::string fitText(FontId f, std::string text, float maxW, float& size, float minSize) {
    while (size > minSize && ui::measureText(f, text, size).x > maxW) size -= 1.f;
    if (ui::measureText(f, text, size).x <= maxW) return text;
    while (!text.empty() && ui::measureText(f, text + "…", size).x > maxW) {
        size_t cut = text.size() - 1;
        while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) --cut;
        text.erase(cut);
    }
    return text + "…";
}
void pill(Rectangle r, Color fill, Color line) {
    ui::tilegfx::roundedRect({r.x + 1, r.y + 2, r.width, r.height}, r.height * 0.5f, rgba(0, 0, 0, 70));
    ui::tilegfx::roundedRect(r, r.height * 0.5f, fill);
    if (line.a) ui::tilegfx::roundedLines(r, r.height * 0.5f, 1.2f, line);
}

constexpr float BTN_W = 124.f, BTN_H = 46.f, BTN_GAP = 9.f, EDGE = 18.f;
constexpr float STATUS_Y = 877.f;

float S() { return ui::hudTextScale(); }
// The status line's centre (it grows upward from the bottom edge with the text).
float statusY() { return STATUS_Y - 13.f * (S() - 1.f); }
float helpY() { return statusY() - 13.f * S() - 17.f * S(); }

bool enterPressed() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE); }

} // namespace

void GameHud::update(float dt) {
    now_ += dt;
    bannerAge_ += dt;
    for (Toast& t : toasts_) t.age += dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.age >= t.dur; }),
                  toasts_.end());
    if (!ui::keyboardNav()) btnFocus_ = -1;
}

void GameHud::toast(const std::string& text, Color c, float seconds) {
    if (text.empty()) return;
    for (Toast& t : toasts_)
        if (t.text == text && t.age < 0.5f) return;
    toasts_.push_back({text, c, 0.f, seconds});
    if (toasts_.size() > 6) toasts_.erase(toasts_.begin());
}

std::vector<GameHud::Click> GameHud::takeClicks() {
    std::vector<Click> out;
    out.swap(clicks_);
    return out;
}

// ---------------------------------------------------------------- keyboard
bool GameHud::keyboard(bool enabled) {
    if (!enabled) {
        btnFocus_ = -1;
        return false;
    }
    if (!panelDrawn_) panelSig_.clear();
    bool used = false;
    // the button column: Tab / Shift+Tab, Enter on the focused one, number keys for the game's own
    const int nb = (int)buttonRects_.size();
    auto enabledAt = [&](int i) { return i >= 0 && i < (int)buttonEnabled_.size() && buttonEnabled_[(size_t)i]; };
    if (IsKeyPressed(KEY_TAB) && nb > 0) {
        ui::noteKeyboardNav();
        const int dir = ui::shiftDown() ? -1 : 1;
        int f = btnFocus_;
        for (int k = 0; k < nb; ++k) {
            f = f < 0 ? (dir > 0 ? 0 : nb - 1) : (f + dir + nb) % nb;
            if (enabledAt(f)) break;
        }
        btnFocus_ = enabledAt(f) ? f : -1;
        used = true;
    }
    if (btnFocus_ >= nb) btnFocus_ = -1;
    if (btnFocus_ >= 0 && !enabledAt(btnFocus_)) btnFocus_ = -1;
    if (btnFocus_ >= 0) {
        if (enterPressed()) {
            clicks_.push_back({buttonIds_[(size_t)btnFocus_], 0});
            ui::noteKeyboardNav();
            return true;
        }
        // an arrow takes the focus back to the table (the game reads the arrow too)
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)) btnFocus_ = -1;
        else used = true;
    }
    for (int k = 0; k < 9 && k < gameButtons_; ++k) {
        if (IsKeyPressed(KEY_ONE + k) || IsKeyPressed(KEY_KP_1 + k)) {
            ui::noteKeyboardNav();
            if (enabledAt(k)) clicks_.push_back({buttonIds_[(size_t)k], 0});
            used = true;
        }
    }
    // a panel: its buttons by the arrows
    if (panelDrawn_ && !panelRects_.empty()) {
        const int n = (int)panelRects_.size();
        auto pen = [&](int i) { return i >= 0 && i < n && panelEnabled_[(size_t)i]; };
        if (!pen(panelFocus_)) {
            panelFocus_ = -1;
            for (int i = 0; i < n && panelFocus_ < 0; ++i)
                if (pen(i)) panelFocus_ = i;
        }
        auto move = [&](int step) {
            int f = panelFocus_;
            for (int k = 0; k < n; ++k) {
                f = (f + step + n * 8) % n;
                if (pen(f)) break;
            }
            if (pen(f)) panelFocus_ = f;
        };
        auto moveRow = [&](int dir) {
            const int f = panelFocus_ + dir * panelColumns_;
            if (pen(f)) panelFocus_ = f;
            else if (f >= 0 && f < n) { // the nearest enabled one in that row
                const int row0 = (f / panelColumns_) * panelColumns_;
                for (int d = 1; d < panelColumns_; ++d) {
                    if (pen(f - d) && f - d >= row0) { panelFocus_ = f - d; break; }
                    if (pen(f + d) && f + d < std::min(n, row0 + panelColumns_)) { panelFocus_ = f + d; break; }
                }
            } else if (dir > 0 && f >= n) { // below the last (short) row: its last button
                for (int i = n - 1; i > panelFocus_; --i)
                    if (pen(i)) { panelFocus_ = i; break; }
            }
        };
        if (ui::keyPressedRepeat(KEY_LEFT)) move(-1);
        if (ui::keyPressedRepeat(KEY_RIGHT)) move(1);
        if (ui::keyPressedRepeat(KEY_UP)) moveRow(-1);
        if (ui::keyPressedRepeat(KEY_DOWN)) moveRow(1);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN))
            ui::noteKeyboardNav();
        if (enterPressed() && pen(panelFocus_)) {
            ui::noteKeyboardNav();
            clicks_.push_back({PANEL_ID + panelFocus_, 0});
        }
        return true;
    }
    return used;
}

void GameHud::focusRing(Rectangle r) {
    if (!ui::keyboardNav()) return;
    const float a = 0.75f + 0.25f * std::sin(now_ * 6.f);
    ui::tilegfx::roundedLines({r.x - 5, r.y - 5, r.width + 10, r.height + 10}, 13.f, 3.f, fadeC(rgba(120, 210, 255), a));
    ui::tilegfx::roundedLines({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 11.f, 1.5f, fadeC(WHITE, 0.8f * a));
}

void GameHud::keyHelp(const std::string& text) {
    if (text.empty() || !ui::keyboardNav()) return;
    helpShown_ = true;
    const float s = S();
    float fs = 14.f * s;
    const std::string t = fitText(FontId::UiBold, text, ui::VW - 420.f, fs, 11.f);
    const Vector2 m = ui::measureText(FontId::UiBold, t, fs);
    const float h = 22.f * s, y = helpY();
    const Rectangle r{std::round(ui::VW * 0.5f - m.x * 0.5f - 14.f), std::round(y - h * 0.5f), std::round(m.x + 28.f), std::round(h)};
    pill(r, rgba(10, 22, 34, 215), rgba(120, 210, 255, 150));
    ui::drawTextCentered(FontId::UiBold, t, {ui::VW * 0.5f, r.y + h * 0.5f}, fs, rgba(200, 236, 255));
}

// ---------------------------------------------------------------- plates
void GameHud::plates(const Renderer& r, const std::array<Vector3, 4>& heads, const std::array<Plate, 4>& p) {
    const Camera3D& view = r.lastCamera();
    const Vector3 fwd = Vector3Normalize(Vector3Subtract(view.target, view.position));
    const Vector3 camRight = Vector3Normalize(Vector3CrossProduct(fwd, view.up));
    const float s = S();
    for (int seat = 0; seat < 4; ++seat) {
        const Plate& pl = p[(size_t)seat];
        if (!pl.show) continue;
        const Vector3 head = heads[(size_t)seat];
        Vector2 hc, he;
        if (Vector3DotProduct(Vector3Subtract(head, view.position), fwd) < 0.05f) continue;
        if (!r.projectToVirtual(head, hc) || !r.projectToVirtual(Vector3Add(head, Vector3Scale(camRight, 0.1f)), he)) continue;
        const float rad = std::clamp(std::fabs(he.x - hc.x), 16.f, 140.f);
        float ns = 17.f * s;
        const std::string name = fitText(FontId::UiBold, pl.name, 150.f * s, ns, 12.f);
        const std::string lbl = pl.label.empty() ? std::string() : pl.label + " ";
        const float lfs = 12.f * s, vfs = 15.f * s, bfs = 12.f * s;
        const float nameW = ui::measureText(FontId::UiBold, name, ns).x;
        const float lblW = ui::measureText(FontId::Ui, lbl, lfs).x;
        const float valW = lblW + ui::measureText(FontId::UiBold, pl.value, vfs).x;
        float bw = 0.f;
        for (const Badge& b : pl.badges) bw += ui::measureText(FontId::UiBold, b.text, bfs).x + 14.f + 4.f;
        const float w = std::max({nameW + valW + 34.f, bw + 12.f, 96.f});
        const float h = (pl.badges.empty() ? 28.f : 46.f) * s;
        const float side = hc.x > ui::VW * 0.5f + 150.f ? -1.f : 1.f;
        Vector2 c{hc.x + side * (rad + 14.f + w * 0.5f), hc.y - rad * 0.15f};
        c.x = std::clamp(c.x, w * 0.5f + 8.f, ui::VW - w * 0.5f - 8.f);
        c.y = std::clamp(c.y, h * 0.5f + 8.f, ui::VH - 220.f);
        const Rectangle rc{std::round(c.x - w * 0.5f), std::round(c.y - h * 0.5f), std::round(w), h};
        if (pl.turn) {
            const float a = 0.45f + 0.25f * std::sin(now_ * 3.2f);
            ui::tilegfx::drawSoftBox({c.x, c.y}, w + 14, h + 16, 0.f, fadeC(pal::Highlight, a));
        }
        ui::tilegfx::roundedRect({rc.x + 1, rc.y + 3, rc.width, rc.height}, 10, rgba(0, 0, 0, 90));
        ui::tilegfx::roundedGradV(rc, 10, rgba(58, 36, 20, 228), rgba(30, 18, 10, 228));
        ui::tilegfx::roundedLines(rc, 10, pl.turn ? 2.f : 1.2f, pl.turn ? pal::Highlight : fadeC(pal::Brass, 0.55f));
        const float ty = rc.y + 14.f * s;
        ui::drawText(FontId::UiBold, name, {rc.x + 10.f, ty - ns * 0.56f}, ns, pl.turn ? pal::Highlight : pal::TextLight);
        if (!lbl.empty()) ui::drawText(FontId::Ui, lbl, {rc.x + rc.width - 10.f - valW, ty - lfs * 0.56f + 1.f}, lfs, fadeC(pal::Brass, 0.7f));
        ui::drawText(FontId::UiBold, pl.value, {rc.x + rc.width - 10.f - valW + lblW, ty - vfs * 0.56f}, vfs, pal::Brass);
        if (!pl.badges.empty()) {
            float x = c.x - (bw - 4.f) * 0.5f;
            for (const Badge& b : pl.badges) {
                const float bwi = ui::measureText(FontId::UiBold, b.text, bfs).x + 14.f;
                const Rectangle br{x, rc.y + 26.f * s, bwi, 15.f * s};
                ui::tilegfx::roundedRect(br, 7.5f * s, b.color);
                ui::drawTextCentered(FontId::UiBold, b.text, {br.x + br.width * 0.5f, br.y + 7.5f * s}, bfs, rgba(250, 244, 230));
                x += bwi + 4.f;
            }
        }
        const float px = side < 0.f ? rc.x + rc.width : rc.x;
        const float py = std::clamp(hc.y, rc.y + 8.f, rc.y + rc.height - 8.f);
        const float dx = -side * 7.f;
        const Color col = pl.turn ? fadeC(pal::Highlight, 0.95f) : rgba(30, 18, 10, 228);
        if (side < 0.f) DrawTriangle({px, py - 6}, {px, py + 6}, {px + dx, py}, col);
        else DrawTriangle({px, py - 6}, {px + dx, py}, {px, py + 6}, col);
    }
}

// ---------------------------------------------------------------- status line
void GameHud::status(const std::string& st, Color sc, bool myTurn, const std::vector<std::pair<std::string, Color>>& parts,
                     bool aiTag) {
    if (st.empty() && parts.empty()) return;
    const float s = S();
    const float sy = statusY();
    const float fs = 17.f * s, pfs = 16.f * s;
    const float stW = st.empty() ? 0.f : ui::measureText(FontId::UiBold, st, fs).x + (myTurn ? 16.f : 0.f);
    float ctW = 0.f;
    for (auto& p : parts) ctW += ui::measureText(FontId::UiBold, p.first, pfs).x;
    const float sep = (!st.empty() && !parts.empty()) ? 25.f : 0.f;
    const char* tag = "YAPAY ZEKA";
    const float tagFs = 12.f * s, tagSp = 1.f;
    const float tagW = aiTag ? ui::measureText(FontId::UiBold, tag, tagFs, tagSp).x + 16.f : 0.f;
    const float w = stW + ctW + sep + 30.f + (aiTag ? tagW + 8.f : 0.f);
    const float h = 26.f * s;
    const Rectangle rc{std::round(ui::VW * 0.5f - w * 0.5f), std::round(sy - h * 0.5f), std::round(w), std::round(h)};
    pill(rc, rgba(14, 9, 5, 200), fadeC(sc, myTurn ? 0.5f : 0.22f));
    float x = rc.x + 15.f;
    if (aiTag) {
        const Rectangle tr{std::round(x - 8.f), std::round(sy - 8.f * s), std::round(tagW), std::round(16.f * s)};
        ui::tilegfx::roundedRect(tr, 8.f * s, rgba(38, 104, 58, 235));
        ui::tilegfx::roundedLines(tr, 8.f * s, 1.f, rgba(150, 236, 150, 200));
        ui::drawTextCentered(FontId::UiBold, tag, {tr.x + tr.width * 0.5f, sy}, tagFs, rgba(214, 255, 214), tagSp);
        x = tr.x + tr.width + 10.f;
    }
    if (!st.empty()) {
        if (myTurn) {
            DrawCircleV({x + 4.f, sy}, 3.8f * s, fadeC(sc, 0.6f + 0.4f * std::sin(now_ * 5.f)));
            x += 16.f;
        }
        const Vector2 m = ui::measureText(FontId::UiBold, st, fs);
        ui::drawText(FontId::UiBold, st, {x, sy - m.y * 0.5f}, fs, sc);
        x += m.x;
    }
    if (!parts.empty()) {
        if (sep > 0.f) {
            DrawRectangleRec({x + 12.f, sy - 8.f * s, 1.f, 16.f * s}, rgba(255, 255, 255, 50));
            x += sep;
        }
        for (auto& p : parts) {
            const Vector2 m = ui::measureText(FontId::UiBold, p.first, pfs);
            ui::drawText(FontId::UiBold, p.first, {x, sy - m.y * 0.5f}, pfs, p.second);
            x += m.x;
        }
    }
}

// ---------------------------------------------------------------- buttons
void GameHud::buttons(Vector2 m, const std::vector<std::string>& labels, const std::vector<bool>& enabled,
                      const std::vector<bool>& glow, bool aiOn) {
    const float s = S();
    const float bwid = BTN_W * s, bh = BTN_H * s;
    const int n = (int)labels.size() + 2;
    gameButtons_ = (int)labels.size();
    const float x = ui::VW - EDGE - bwid;
    const float y0 = ui::VH - EDGE - (float)n * bh - (float)(n - 1) * BTN_GAP;
    for (int i = 0; i < n; ++i) {
        const Rectangle r{x, y0 + (float)i * (bh + BTN_GAP), bwid, bh};
        buttonRects_.push_back(r);
        if (ui::pointInRect(m, {r.x - 3, r.y - 3, r.width + 6, r.height + 6})) hover_ = true;
        if (i < (int)labels.size()) {
            const bool en = i < (int)enabled.size() ? enabled[(size_t)i] : true;
            buttonIds_.push_back(i);
            buttonEnabled_.push_back(en);
            if (i < (int)glow.size() && glow[(size_t)i] && en) {
                const float a = 0.55f + 0.35f * std::sin(now_ * 4.f);
                ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f,
                                         fadeC(pal::Good, a));
            }
            float fs = 20.f * s;
            while (fs > 14.f && ui::measureText(FontId::UiBold, labels[(size_t)i], fs).x > r.width - 14.f) fs -= 1.f;
            if (ui::drawButton(r, labels[(size_t)i], m, en, ui::ButtonStyle::Wood, fs)) clicks_.push_back({i, 0});
            // the number key that presses it, small in the corner while the keyboard is in use
            if (ui::keyboardNav() && i < 9 && en) {
                const std::string k = std::to_string(i + 1);
                ui::drawText(FontId::UiBold, k, {r.x + 7.f, r.y + 4.f}, 12.f * s, fadeC(rgba(170, 220, 255), 0.85f));
            }
        } else if (i == n - 2) {
            buttonIds_.push_back(AI_ID);
            buttonEnabled_.push_back(true);
            if (aiOn) {
                const float a = 0.45f + 0.25f * std::sin(now_ * 2.6f);
                ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f,
                                         fadeC(pal::Highlight, a));
            }
            if (ui::drawButton(r, "Yapay Zeka", m, true, ui::ButtonStyle::Wood, 19.f * s)) clicks_.push_back({AI_ID, 0});
            const Vector2 lamp{r.x + r.width - 10.f, r.y + 9.f};
            if (aiOn) {
                ui::tilegfx::roundedLines({r.x + 2, r.y + 2, r.width - 4, r.height - 6}, 10.f, 2.f, fadeC(pal::Highlight, 0.9f));
                DrawCircleV(lamp, 7.f, rgba(120, 255, 120, 60));
                DrawCircleV(lamp, 4.f, rgba(120, 236, 110));
            } else {
                DrawCircleV(lamp, 4.f, rgba(40, 30, 22));
                DrawCircleLinesV(lamp, 4.f, fadeC(pal::Brass, 0.35f));
            }
        } else {
            buttonIds_.push_back(MENU_ID);
            buttonEnabled_.push_back(true);
            if (ui::drawButton(r, "Menü", m, true, ui::ButtonStyle::Wood, 20.f * s)) clicks_.push_back({MENU_ID, 0});
        }
        if (i == btnFocus_) focusRing(r);
    }
}

// ---------------------------------------------------------------- the choice panel
void GameHud::panel(Vector2 m, const std::string& title, const std::string& subtitle, const std::vector<PanelButton>& buttons,
                    int columns, float y, float bw) {
    const float s = S();
    columns = std::max(1, columns);
    bw *= s;
    const float bh = 50.f * s, gap = 10.f;
    const float tfs = 26.f * s, sfs = 17.f * s;
    const int rows = ((int)buttons.size() + columns - 1) / columns;
    bool anyHint = false;
    for (const PanelButton& b : buttons) anyHint = anyHint || !b.hint.empty();
    const float rowH = bh + (anyHint ? 18.f * s : 0.f);
    const float gridW = (float)columns * bw + (float)(columns - 1) * gap;
    const float w = std::max({gridW + 60.f, ui::measureText(FontId::UiBold, title, tfs).x + 60.f,
                              subtitle.empty() ? 0.f : ui::measureText(FontId::Ui, subtitle, sfs).x + 48.f});
    const float head = 84.f * s + (subtitle.empty() ? 0.f : 22.f * s);
    const float h = head + (float)rows * rowH + (float)(rows - 1) * gap + 24.f;
    // (bigger text: the panel may not run into the status line and the cards)
    y = std::min(y, ui::VH - 150.f - h + 20.f);
    const Rectangle pr{std::round(ui::VW * 0.5f - w * 0.5f), std::round(y - 20.f), std::round(w), std::round(h)};
    if (ui::pointInRect(m, pr)) hover_ = true;
    ui::tilegfx::roundedRect({pr.x + 3, pr.y + 6, pr.width, pr.height}, 16, rgba(0, 0, 0, 110));
    ui::tilegfx::roundedGradV(pr, 16, rgba(54, 32, 18, 238), rgba(28, 16, 9, 238));
    ui::tilegfx::roundedLines(pr, 16, 1.6f, fadeC(pal::Brass, 0.7f));
    ui::drawTextCentered(FontId::UiBold, title, {ui::VW * 0.5f, pr.y + 36.f * s}, tfs, pal::Highlight);
    float gy = pr.y + 66.f * s;
    if (!subtitle.empty()) {
        ui::drawTextCentered(FontId::Ui, subtitle, {ui::VW * 0.5f, pr.y + 64.f * s}, sfs, fadeC(pal::TextLight, 0.8f));
        gy += 22.f * s;
    }
    // keyboard focus: a new panel starts on its marked (İpucu) button or its first enabled one
    std::string sig = title;
    int marked = -1;
    for (size_t i = 0; i < buttons.size(); ++i) {
        sig += "|" + buttons[i].label;
        if (buttons[i].selected && buttons[i].enabled && marked < 0) marked = (int)i;
    }
    if (sig != panelSig_) {
        panelSig_ = sig;
        panelFocus_ = marked;
        for (int i = 0; i < (int)buttons.size() && panelFocus_ < 0; ++i)
            if (buttons[(size_t)i].enabled) panelFocus_ = i;
    } else if (marked >= 0 && marked != panelSelected_) {
        panelFocus_ = marked; // an İpucu just marked a button: the focus goes there
    }
    panelSelected_ = marked;
    panelColumns_ = columns;
    panelDrawn_ = true;
    for (int i = 0; i < (int)buttons.size(); ++i) {
        const int row = i / columns, col = i % columns;
        const int inRow = std::min(columns, (int)buttons.size() - row * columns);
        const float rw = (float)inRow * bw + (float)(inRow - 1) * gap;
        const Rectangle r{ui::VW * 0.5f - rw * 0.5f + (float)col * (bw + gap), gy + (float)row * (rowH + gap), bw, bh};
        const PanelButton& b = buttons[(size_t)i];
        panelRects_.push_back(r);
        panelEnabled_.push_back(b.enabled);
        if (b.selected) {
            ui::tilegfx::roundedRect({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 12, fadeC(pal::Highlight, 0.35f));
        }
        float fs = 21.f * s;
        while (fs > 13.f && ui::measureText(FontId::UiBold, b.label, fs).x > r.width - 12.f) fs -= 1.f;
        if (ui::drawButton(r, b.label, m, b.enabled, ui::ButtonStyle::Wood, fs)) clicks_.push_back({PANEL_ID + i, 0});
        if (i == panelFocus_) focusRing(r);
        if (!b.hint.empty()) {
            float hs = 13.f * s;
            const std::string t = fitText(FontId::Ui, b.hint, r.width + gap, hs, 10.f);
            ui::drawTextCentered(FontId::Ui, t, {r.x + r.width * 0.5f, r.y + bh + 9.f * s}, hs,
                                 fadeC(pal::TextLight, b.enabled ? 0.75f : 0.4f));
        }
    }
}

void GameHud::banner(const std::string& text, const std::string& sub, Color c, float seconds) {
    bannerText_ = text;
    bannerSub_ = sub;
    bannerColor_ = c;
    bannerAge_ = 0.f;
    bannerDur_ = seconds;
}

void GameHud::listPanel(Vector2 m, const std::string& title, const std::vector<std::pair<std::string, Color>>& lines,
                        int maxLines, float x, float y, float w) {
    const float s = S();
    w *= 1.f + (s - 1.f) * 0.8f;
    const int first = std::max(0, (int)lines.size() - maxLines);
    const int n = (int)lines.size() - first;
    const float lineH = 22.f * s, top = 40.f * s;
    const float h = top + (float)std::max(1, n) * lineH + 12.f;
    const Rectangle pr{x, y, w, std::round(h)};
    if (ui::pointInRect(m, pr)) hover_ = true;
    ui::tilegfx::roundedRect({pr.x + 2, pr.y + 4, pr.width, pr.height}, 12, rgba(0, 0, 0, 70));
    ui::tilegfx::roundedGradV(pr, 12, rgba(40, 24, 13, 196), rgba(22, 13, 7, 196));
    ui::tilegfx::roundedLines(pr, 12, 1.2f, fadeC(pal::Brass, 0.5f));
    ui::drawText(FontId::UiBold, title, {pr.x + 14.f, pr.y + 10.f * s}, 18.f * s, pal::Highlight);
    DrawRectangleRec({pr.x + 12.f, pr.y + top - 6.f, pr.width - 24.f, 1.f}, fadeC(pal::Brass, 0.35f));
    if (n == 0) {
        ui::drawText(FontId::Ui, "Henüz hamle yok", {pr.x + 14.f, pr.y + top}, 15.f * s, fadeC(pal::TextLight, 0.55f));
        return;
    }
    for (int i = 0; i < n; ++i) {
        const auto& ln = lines[(size_t)(first + i)];
        float fs = 15.f * s;
        const std::string t = fitText(FontId::Ui, ln.first, pr.width - 28.f, fs, 12.f);
        ui::drawText(FontId::Ui, t, {pr.x + 14.f, pr.y + top + (float)i * lineH + (15.f * s - fs) * 0.5f}, fs, ln.second);
    }
}

void GameHud::toasts() {
    const float s = S();
    if (bannerAge_ < bannerDur_ && !bannerText_.empty()) {
        // pops in (overshoot), holds, fades
        const float t = bannerAge_;
        const float a = std::min(ui::clamp01(t / 0.15f), ui::clamp01((bannerDur_ - t) / 0.6f));
        const float pop = t < 0.35f ? 0.6f + 0.55f * ui::easeOutCubic(t / 0.35f) - 0.15f * std::sin(t / 0.35f * PI) : 1.15f - 0.15f * ui::clamp01((t - 0.35f) / 0.3f);
        const float fs = 96.f * pop;
        const Vector2 c{ui::VW * 0.5f, 300.f};
        const Vector2 sz = ui::measureText(FontId::Sign, bannerText_, fs, 4.f);
        ui::tilegfx::drawSoftBox(c, sz.x + 160.f, sz.y + 90.f, 0.f, fadeC(rgba(20, 10, 4, 200), a));
        for (int k = 0; k < 8; ++k) { // a dark rim, then the word
            const float ang = (float)k * PI * 0.25f;
            ui::drawTextCentered(FontId::Sign, bannerText_, {c.x + std::cos(ang) * 3.f, c.y + std::sin(ang) * 3.f + 2.f}, fs,
                                 fadeC(rgba(40, 18, 6), a), 4.f);
        }
        ui::drawTextCentered(FontId::Sign, bannerText_, c, fs, fadeC(bannerColor_, a), 4.f);
        if (!bannerSub_.empty()) {
            const float sfs = 24.f * s;
            const Vector2 ss = ui::measureText(FontId::UiBold, bannerSub_, sfs);
            const Rectangle r{std::round(c.x - ss.x * 0.5f - 18.f), std::round(c.y + sz.y * 0.5f + 8.f), std::round(ss.x + 36.f), 36.f * s};
            pill(r, fadeC(rgba(22, 14, 8, 225), a), fadeC(bannerColor_, 0.6f * a));
            ui::drawTextCentered(FontId::UiBold, bannerSub_, {c.x, r.y + 18.f * s}, sfs, fadeC(rgba(250, 238, 210), a));
        }
    }
    const float cx = ui::VW * 0.5f;
    const int n = (int)toasts_.size();
    const int first = std::max(0, n - 2);
    const float th = 26.f * s, step = th + 4.f;
    // the newest just above the status line (and above the key-help strip while it is shown)
    const float base = (helpShown_ ? helpY() - 11.f * s : statusY() - 13.f * s) - 10.f - th * 0.5f;
    float y = base - (float)(n - first - 1) * step;
    for (int i = first; i < n; ++i) {
        const Toast& t = toasts_[(size_t)i];
        const float a = std::min(ui::clamp01(t.age / 0.18f), ui::clamp01((t.dur - t.age) / 0.45f));
        float fs = 17.f * s;
        const std::string text = fitText(FontId::UiBold, t.text, 620.f * s, fs, 14.f);
        const Vector2 m = ui::measureText(FontId::UiBold, text, fs);
        const float w = m.x + 32.f;
        const float slide = (1.f - ui::easeOutCubic(ui::clamp01(t.age / 0.25f))) * 8.f;
        const Rectangle r{std::round(cx - w / 2), std::round(y - th * 0.5f + slide), std::round(w), std::round(th)};
        if (a > 0.01f) {
            ui::tilegfx::roundedRect({r.x + 2, r.y + 3, r.width, r.height}, th * 0.5f, fadeC(rgba(0, 0, 0, 100), a));
            ui::tilegfx::roundedRect(r, th * 0.5f, fadeC(rgba(22, 14, 8, 232), a));
            ui::tilegfx::roundedLines(r, th * 0.5f, 1.3f, fadeC(t.color, 0.55f * a));
            ui::drawTextCentered(FontId::UiBold, text, {cx, r.y + th * 0.5f}, fs, fadeC(t.color, a));
        }
        y += step;
    }
}

void GameHud::label3D(const Renderer& r, Vector3 world, const std::string& text, Color c, float size) {
    Vector2 p;
    if (!r.projectToVirtual(world, p) || text.empty()) return;
    size *= S();
    const Vector2 m = ui::measureText(FontId::UiBold, text, size);
    const Rectangle rc{std::round(p.x - m.x * 0.5f - 10.f), std::round(p.y - size * 0.5f - 5.f), std::round(m.x + 20.f), size + 10.f};
    pill(rc, rgba(14, 9, 5, 200), fadeC(c, 0.4f));
    ui::drawTextCentered(FontId::UiBold, text, {p.x, rc.y + rc.height * 0.5f}, size, c);
}

} // namespace r3d
