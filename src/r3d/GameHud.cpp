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
constexpr float TOAST_Y = STATUS_Y - 36.f;

} // namespace

void GameHud::update(float dt) {
    now_ += dt;
    for (Toast& t : toasts_) t.age += dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.age >= t.dur; }),
                  toasts_.end());
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

void GameHud::plates(const Renderer& r, const std::array<Vector3, 4>& heads, const std::array<Plate, 4>& p) {
    const Camera3D& view = r.lastCamera();
    const Vector3 fwd = Vector3Normalize(Vector3Subtract(view.target, view.position));
    const Vector3 camRight = Vector3Normalize(Vector3CrossProduct(fwd, view.up));
    for (int seat = 0; seat < 4; ++seat) {
        const Plate& pl = p[(size_t)seat];
        if (!pl.show) continue;
        const Vector3 head = heads[(size_t)seat];
        Vector2 hc, he;
        if (Vector3DotProduct(Vector3Subtract(head, view.position), fwd) < 0.05f) continue;
        if (!r.projectToVirtual(head, hc) || !r.projectToVirtual(Vector3Add(head, Vector3Scale(camRight, 0.1f)), he)) continue;
        const float rad = std::clamp(std::fabs(he.x - hc.x), 16.f, 140.f);
        float ns = 17.f;
        const std::string name = fitText(FontId::UiBold, pl.name, 150.f, ns, 12.f);
        const std::string lbl = pl.label.empty() ? std::string() : pl.label + " ";
        const float nameW = ui::measureText(FontId::UiBold, name, ns).x;
        const float lblW = ui::measureText(FontId::Ui, lbl, 12.f).x;
        const float valW = lblW + ui::measureText(FontId::UiBold, pl.value, 15.f).x;
        float bw = 0.f;
        for (const Badge& b : pl.badges) bw += ui::measureText(FontId::UiBold, b.text, 12.f).x + 14.f + 4.f;
        const float w = std::max({nameW + valW + 34.f, bw + 12.f, 96.f});
        const float h = pl.badges.empty() ? 28.f : 46.f;
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
        const float ty = rc.y + 14.f;
        ui::drawText(FontId::UiBold, name, {rc.x + 10.f, ty - ns * 0.56f}, ns, pl.turn ? pal::Highlight : pal::TextLight);
        if (!lbl.empty()) ui::drawText(FontId::Ui, lbl, {rc.x + rc.width - 10.f - valW, ty - 12.f * 0.56f + 1.f}, 12.f, fadeC(pal::Brass, 0.7f));
        ui::drawText(FontId::UiBold, pl.value, {rc.x + rc.width - 10.f - valW + lblW, ty - 15.f * 0.56f}, 15.f, pal::Brass);
        if (!pl.badges.empty()) {
            float x = c.x - (bw - 4.f) * 0.5f;
            for (const Badge& b : pl.badges) {
                const float bwi = ui::measureText(FontId::UiBold, b.text, 12.f).x + 14.f;
                const Rectangle br{x, rc.y + 26.f, bwi, 15.f};
                ui::tilegfx::roundedRect(br, 7.5f, b.color);
                ui::drawTextCentered(FontId::UiBold, b.text, {br.x + br.width * 0.5f, br.y + 7.5f}, 12.f, rgba(250, 244, 230));
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

void GameHud::status(const std::string& st, Color sc, bool myTurn, const std::vector<std::pair<std::string, Color>>& parts,
                     bool aiTag) {
    if (st.empty() && parts.empty()) return;
    const float fs = 17.f;
    const float stW = st.empty() ? 0.f : ui::measureText(FontId::UiBold, st, fs).x + (myTurn ? 16.f : 0.f);
    float ctW = 0.f;
    for (auto& p : parts) ctW += ui::measureText(FontId::UiBold, p.first, 16.f).x;
    const float sep = (!st.empty() && !parts.empty()) ? 25.f : 0.f;
    const char* tag = "YAPAY ZEKA";
    const float tagFs = 12.f, tagSp = 1.f;
    const float tagW = aiTag ? ui::measureText(FontId::UiBold, tag, tagFs, tagSp).x + 16.f : 0.f;
    const float w = stW + ctW + sep + 30.f + (aiTag ? tagW + 8.f : 0.f);
    const Rectangle rc{std::round(ui::VW * 0.5f - w * 0.5f), STATUS_Y - 13.f, std::round(w), 26.f};
    pill(rc, rgba(14, 9, 5, 200), fadeC(sc, myTurn ? 0.5f : 0.22f));
    float x = rc.x + 15.f;
    if (aiTag) {
        const Rectangle tr{std::round(x - 8.f), STATUS_Y - 8.f, std::round(tagW), 16.f};
        ui::tilegfx::roundedRect(tr, 8.f, rgba(38, 104, 58, 235));
        ui::tilegfx::roundedLines(tr, 8.f, 1.f, rgba(150, 236, 150, 200));
        ui::drawTextCentered(FontId::UiBold, tag, {tr.x + tr.width * 0.5f, STATUS_Y}, tagFs, rgba(214, 255, 214), tagSp);
        x = tr.x + tr.width + 10.f;
    }
    if (!st.empty()) {
        if (myTurn) {
            DrawCircleV({x + 4.f, STATUS_Y}, 3.8f, fadeC(sc, 0.6f + 0.4f * std::sin(now_ * 5.f)));
            x += 16.f;
        }
        const Vector2 m = ui::measureText(FontId::UiBold, st, fs);
        ui::drawText(FontId::UiBold, st, {x, STATUS_Y - m.y * 0.5f}, fs, sc);
        x += m.x;
    }
    if (!parts.empty()) {
        if (sep > 0.f) {
            DrawRectangleRec({x + 12.f, STATUS_Y - 8.f, 1.f, 16.f}, rgba(255, 255, 255, 50));
            x += sep;
        }
        for (auto& p : parts) {
            const Vector2 m = ui::measureText(FontId::UiBold, p.first, 16.f);
            ui::drawText(FontId::UiBold, p.first, {x, STATUS_Y - m.y * 0.5f}, 16.f, p.second);
            x += m.x;
        }
    }
}

void GameHud::buttons(Vector2 m, const std::vector<std::string>& labels, const std::vector<bool>& enabled,
                      const std::vector<bool>& glow, bool aiOn) {
    const int n = (int)labels.size() + 2;
    const float x = ui::VW - EDGE - BTN_W;
    const float y0 = ui::VH - EDGE - (float)n * BTN_H - (float)(n - 1) * BTN_GAP;
    for (int i = 0; i < n; ++i) {
        const Rectangle r{x, y0 + (float)i * (BTN_H + BTN_GAP), BTN_W, BTN_H};
        buttonRects_.push_back(r);
        if (ui::pointInRect(m, {r.x - 3, r.y - 3, r.width + 6, r.height + 6})) hover_ = true;
        if (i < (int)labels.size()) {
            const bool en = i < (int)enabled.size() ? enabled[(size_t)i] : true;
            if (i < (int)glow.size() && glow[(size_t)i] && en) {
                const float a = 0.55f + 0.35f * std::sin(now_ * 4.f);
                ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f,
                                         fadeC(pal::Good, a));
            }
            float fs = 20.f;
            while (fs > 14.f && ui::measureText(FontId::UiBold, labels[(size_t)i], fs).x > r.width - 14.f) fs -= 1.f;
            if (ui::drawButton(r, labels[(size_t)i], m, en, ui::ButtonStyle::Wood, fs)) clicks_.push_back({i, 0});
        } else if (i == n - 2) {
            if (aiOn) {
                const float a = 0.45f + 0.25f * std::sin(now_ * 2.6f);
                ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f,
                                         fadeC(pal::Highlight, a));
            }
            if (ui::drawButton(r, "Yapay Zeka", m, true, ui::ButtonStyle::Wood, 19)) clicks_.push_back({AI_ID, 0});
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
            if (ui::drawButton(r, "Menü", m, true, ui::ButtonStyle::Wood, 20)) clicks_.push_back({MENU_ID, 0});
        }
    }
}

void GameHud::panel(Vector2 m, const std::string& title, const std::string& subtitle, const std::vector<PanelButton>& buttons,
                    int columns, float y, float bw) {
    columns = std::max(1, columns);
    const float bh = 50.f, gap = 10.f;
    const int rows = ((int)buttons.size() + columns - 1) / columns;
    bool anyHint = false;
    for (const PanelButton& b : buttons) anyHint = anyHint || !b.hint.empty();
    const float rowH = bh + (anyHint ? 18.f : 0.f);
    const float gridW = (float)columns * bw + (float)(columns - 1) * gap;
    const float w = std::max(gridW + 60.f, ui::measureText(FontId::UiBold, title, 26.f).x + 60.f);
    const float h = 84.f + (subtitle.empty() ? 0.f : 22.f) + (float)rows * rowH + (float)(rows - 1) * gap + 24.f;
    const Rectangle pr{std::round(ui::VW * 0.5f - w * 0.5f), std::round(y - 20.f), std::round(w), std::round(h)};
    if (ui::pointInRect(m, pr)) hover_ = true;
    ui::tilegfx::roundedRect({pr.x + 3, pr.y + 6, pr.width, pr.height}, 16, rgba(0, 0, 0, 110));
    ui::tilegfx::roundedGradV(pr, 16, rgba(54, 32, 18, 238), rgba(28, 16, 9, 238));
    ui::tilegfx::roundedLines(pr, 16, 1.6f, fadeC(pal::Brass, 0.7f));
    ui::drawTextCentered(FontId::UiBold, title, {ui::VW * 0.5f, pr.y + 36.f}, 26.f, pal::Highlight);
    float gy = pr.y + 66.f;
    if (!subtitle.empty()) {
        ui::drawTextCentered(FontId::Ui, subtitle, {ui::VW * 0.5f, pr.y + 64.f}, 17.f, fadeC(pal::TextLight, 0.8f));
        gy += 22.f;
    }
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
        float fs = 21.f;
        while (fs > 13.f && ui::measureText(FontId::UiBold, b.label, fs).x > r.width - 12.f) fs -= 1.f;
        if (ui::drawButton(r, b.label, m, b.enabled, ui::ButtonStyle::Wood, fs)) clicks_.push_back({PANEL_ID + i, 0});
        if (!b.hint.empty()) {
            float hs = 13.f;
            const std::string t = fitText(FontId::Ui, b.hint, r.width + gap, hs, 10.f);
            ui::drawTextCentered(FontId::Ui, t, {r.x + r.width * 0.5f, r.y + bh + 9.f}, hs,
                                 fadeC(pal::TextLight, b.enabled ? 0.75f : 0.4f));
        }
    }
}

void GameHud::toasts() {
    const float cx = ui::VW * 0.5f;
    const int n = (int)toasts_.size();
    const int first = std::max(0, n - 2);
    float y = TOAST_Y - (float)(n - first - 1) * 30.f;
    for (int i = first; i < n; ++i) {
        const Toast& t = toasts_[(size_t)i];
        const float a = std::min(ui::clamp01(t.age / 0.18f), ui::clamp01((t.dur - t.age) / 0.45f));
        float fs = 17.f;
        const std::string text = fitText(FontId::UiBold, t.text, 620.f, fs, 14.f);
        const Vector2 m = ui::measureText(FontId::UiBold, text, fs);
        const float w = m.x + 32.f;
        const float slide = (1.f - ui::easeOutCubic(ui::clamp01(t.age / 0.25f))) * 8.f;
        const Rectangle r{std::round(cx - w / 2), std::round(y - 13 + slide), std::round(w), 26};
        if (a > 0.01f) {
            ui::tilegfx::roundedRect({r.x + 2, r.y + 3, r.width, r.height}, 13, fadeC(rgba(0, 0, 0, 100), a));
            ui::tilegfx::roundedRect(r, 13, fadeC(rgba(22, 14, 8, 232), a));
            ui::tilegfx::roundedLines(r, 13, 1.3f, fadeC(t.color, 0.55f * a));
            ui::drawTextCentered(FontId::UiBold, text, {cx, r.y + 13}, fs, fadeC(t.color, a));
        }
        y += 30.f;
    }
}

void GameHud::label3D(const Renderer& r, Vector3 world, const std::string& text, Color c, float size) {
    Vector2 p;
    if (!r.projectToVirtual(world, p) || text.empty()) return;
    const Vector2 m = ui::measureText(FontId::UiBold, text, size);
    const Rectangle rc{std::round(p.x - m.x * 0.5f - 10.f), std::round(p.y - size * 0.5f - 5.f), std::round(m.x + 20.f), size + 10.f};
    pill(rc, rgba(14, 9, 5, 200), fadeC(c, 0.4f));
    ui::drawTextCentered(FontId::UiBold, text, {p.x, rc.y + rc.height * 0.5f}, size, c);
}

} // namespace r3d
