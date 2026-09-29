#include "ui/Common.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace ui {

// ---------------------------------------------------------------- fonts
namespace {

struct FontSlot {
    Font font{};
    bool loaded = false; // true when loaded from file (must be unloaded)
};
FontSlot g_fonts[(int)FontId::Count];
Viewport g_viewport;

const char* kFontFiles[(int)FontId::Count][3] = {
    {"/System/Library/Fonts/Supplemental/Trebuchet MS.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
     nullptr},
    {"/System/Library/Fonts/Supplemental/Trebuchet MS Bold.ttf",
     "/System/Library/Fonts/Supplemental/Arial Bold.ttf", nullptr},
    {"/System/Library/Fonts/Supplemental/ChalkboardSE.ttc", "/System/Library/Fonts/Noteworthy.ttc",
     "/System/Library/Fonts/Supplemental/Trebuchet MS.ttf"},
    {"/System/Library/Fonts/Noteworthy.ttc", "/System/Library/Fonts/Supplemental/Bradley Hand Bold.ttf",
     "/System/Library/Fonts/Supplemental/Trebuchet MS.ttf"},
    {"/System/Library/Fonts/Supplemental/Rockwell.ttc", "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
     "/System/Library/Fonts/Supplemental/Trebuchet MS Bold.ttf"},
    {"/System/Library/Fonts/Supplemental/Arial Black.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
     nullptr},
};
const int kFontBaseSize[(int)FontId::Count] = {64, 64, 64, 64, 96, 96};

std::vector<int> fontCodepoints() {
    std::vector<int> cps;
    for (int c = 32; c <= 126; ++c) cps.push_back(c);
    const int extra[] = {
        0x00C7, 0x00E7, // Ç ç
        0x011E, 0x011F, // Ğ ğ
        0x0130, 0x0131, // İ ı
        0x00D6, 0x00F6, // Ö ö
        0x015E, 0x015F, // Ş ş
        0x00DC, 0x00FC, // Ü ü
        0x00C2, 0x00E2, 0x00CE, 0x00EE, 0x00DB, 0x00FB, // Â â Î î Û û
        0x00E9, 0x00B7, 0x00B0, 0x00AB, 0x00BB, 0x00D7, // é · ° « » ×
        0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, // – — ‘ ’ “ ” • …
        0x20BA, // ₺ (only some fonts have it)
    };
    for (int c : extra) cps.push_back(c);
    return cps;
}

float lineHeight(FontId f, float size) {
    (void)f;
    return size * 1.12f;
}

uint32_t readBE32(const unsigned char* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
void writeBE32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

// raylib only reads plain .ttf/.otf, so pull face `index` out of a TrueType collection (.ttc) into a
// standalone sfnt buffer: copy its table directory and tables, rewriting the table offsets.
std::vector<unsigned char> extractTtcFace(const unsigned char* data, int size, int index) {
    std::vector<unsigned char> out;
    if (size < 16 || std::string((const char*)data, 4) != "ttcf") return out;
    uint32_t numFonts = readBE32(data + 8);
    if ((uint32_t)index >= numFonts || 12 + 4 * (index + 1) > size) return out;
    uint32_t fontOff = readBE32(data + 12 + 4 * index);
    if (fontOff + 12 > (uint32_t)size) return out;
    const unsigned char* dir = data + fontOff;
    int numTables = dir[4] << 8 | dir[5];
    uint32_t headerSize = 12 + 16 * (uint32_t)numTables;
    if (fontOff + headerSize > (uint32_t)size) return out;
    out.assign(dir, dir + headerSize);
    for (int t = 0; t < numTables; ++t) {
        const unsigned char* rec = dir + 12 + 16 * t;
        uint32_t off = readBE32(rec + 8), len = readBE32(rec + 12);
        if ((uint64_t)off + len > (uint64_t)size) return {};
        while (out.size() % 4) out.push_back(0);
        writeBE32(out.data() + 12 + 16 * t + 8, (uint32_t)out.size());
        out.insert(out.end(), data + off, data + off + len);
    }
    return out;
}

Font loadFontFile(const char* path, int baseSize, std::vector<int>& cps) {
    if (!IsFileExtension(path, ".ttc")) return LoadFontEx(path, baseSize, cps.data(), (int)cps.size());
    int size = 0;
    unsigned char* data = LoadFileData(path, &size);
    Font f{};
    if (!data) return f;
    std::vector<unsigned char> face = extractTtcFace(data, size, 0);
    UnloadFileData(data);
    if (face.empty()) return f;
    return LoadFontFromMemory(".ttf", face.data(), (int)face.size(), baseSize, cps.data(), (int)cps.size());
}

} // namespace

void loadFonts() {
    std::vector<int> cps = fontCodepoints();
    for (int i = 0; i < (int)FontId::Count; ++i) {
        FontSlot& slot = g_fonts[i];
        slot.loaded = false;
        for (const char* path : kFontFiles[i]) {
            if (!path || !FileExists(path)) continue;
            Font f = loadFontFile(path, kFontBaseSize[i], cps);
            if (f.texture.id != 0 && f.glyphCount > 0) {
                GenTextureMipmaps(&f.texture);
                SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
                slot.font = f;
                slot.loaded = true;
                break;
            }
        }
        if (!slot.loaded) slot.font = GetFontDefault();
    }
}

void unloadFonts() {
    for (FontSlot& slot : g_fonts) {
        if (slot.loaded) UnloadFont(slot.font);
        slot.loaded = false;
        slot.font = Font{};
    }
}

const Font& font(FontId id) {
    FontSlot& slot = g_fonts[(int)id];
    if (slot.font.texture.id == 0) slot.font = GetFontDefault();
    return slot.font;
}

Vector2 measureText(FontId f, const std::string& s, float size, float spacing) {
    return MeasureTextEx(font(f), s.c_str(), size, spacing);
}

void drawText(FontId f, const std::string& s, Vector2 pos, float size, Color c, float spacing) {
    DrawTextEx(font(f), s.c_str(), pos, size, spacing, c);
}

void drawTextCentered(FontId f, const std::string& s, Vector2 center, float size, Color c, float spacing) {
    Vector2 m = measureText(f, s, size, spacing);
    drawText(f, s, {std::round(center.x - m.x * 0.5f), std::round(center.y - m.y * 0.5f)}, size, c, spacing);
}

void drawTextShadow(FontId f, const std::string& s, Vector2 pos, float size, Color c, Color shadow, float offset,
                    float spacing) {
    drawText(f, s, {pos.x + offset, pos.y + offset}, size, shadow, spacing);
    drawText(f, s, pos, size, c, spacing);
}

float drawTextWrapped(FontId f, const std::string& s, Rectangle box, float size, Color c, float lineGap) {
    const float lh = lineHeight(f, size) + lineGap;
    float y = box.y;
    std::string line;
    std::string word;
    auto flushLine = [&]() {
        drawText(f, line, {box.x, y}, size, c);
        y += lh;
        line.clear();
    };
    auto pushWord = [&]() {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measureText(f, candidate, size).x > box.width) {
            flushLine();
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') {
            pushWord();
        } else if (ch == '\n') {
            pushWord();
            flushLine();
        } else {
            word += ch;
        }
    }
    pushWord();
    if (!line.empty()) flushLine();
    return y - box.y;
}

// ---------------------------------------------------------------- virtual canvas
Viewport computeViewport() {
    Viewport vp;
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    if (sw <= 0 || sh <= 0) return vp;
    vp.scale = std::min(sw / VW, sh / VH);
    vp.offset = {std::floor((sw - VW * vp.scale) * 0.5f), std::floor((sh - VH * vp.scale) * 0.5f)};
    return vp;
}

Camera2D viewportCamera(const Viewport& vp) {
    Camera2D cam{};
    cam.offset = vp.offset;
    cam.target = {0, 0};
    cam.rotation = 0.f;
    cam.zoom = vp.scale;
    return cam;
}

Vector2 virtualMouse(const Viewport& vp) {
    Vector2 m = GetMousePosition();
    float s = vp.scale > 0 ? vp.scale : 1.f;
    return {(m.x - vp.offset.x) / s, (m.y - vp.offset.y) / s};
}

void setCurrentViewport(const Viewport& vp) { g_viewport = vp; }
const Viewport& currentViewport() { return g_viewport; }
Vector2 virtualMouse() { return virtualMouse(g_viewport); }

// ---------------------------------------------------------------- palette helpers
Color tileInk(int tileColor) {
    switch (tileColor) {
    case 0: return pal::InkYellow;
    case 1: return pal::InkBlue;
    case 2: return pal::InkBlack;
    case 3: return pal::InkRed;
    default: return pal::InkBlack;
    }
}

Color withAlpha(Color c, float alpha01) {
    c.a = (unsigned char)std::clamp(alpha01 * 255.f, 0.f, 255.f);
    return c;
}

Color lerpColor(Color a, Color b, float t) {
    t = clamp01(t);
    auto l = [t](unsigned char x, unsigned char y) { return (unsigned char)std::lround(x + (y - x) * t); };
    return Color{l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

// ---------------------------------------------------------------- shared widgets
namespace {
// Identifies the button that received the mouse press (by its rectangle) so a release elsewhere
// doesn't click, and a press that started on another button doesn't either.
Rectangle g_pressedRect{-1, -1, 0, 0};
bool sameRect(Rectangle a, Rectangle b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
} // namespace

bool drawButton(Rectangle r, const std::string& label, Vector2 mouse, bool enabled, ButtonStyle style,
                float fontSize) {
    const bool hover = enabled && pointInRect(mouse, r);
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_pressedRect = r;
    const bool held = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && sameRect(g_pressedRect, r);
    bool clicked = false;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && sameRect(g_pressedRect, r)) {
        clicked = hover;
        g_pressedRect = Rectangle{-1, -1, 0, 0};
    }

    Rectangle body = r;
    if (held) body.y += 2;
    const float round = 0.25f;
    switch (style) {
    case ButtonStyle::Wood: {
        DrawRectangleRounded({r.x + 2, r.y + 4, r.width, r.height}, round, 8, Color{0, 0, 0, 110});
        Color top = enabled ? (hover ? pal::WoodLight : pal::Wood) : Color{90, 70, 55, 255};
        Color bottom = enabled ? pal::WoodDark : Color{60, 48, 40, 255};
        DrawRectangleRounded(body, round, 8, bottom);
        Rectangle inner{body.x + 2, body.y + 2, body.width - 4, body.height - 6};
        DrawRectangleRounded(inner, round, 8, top);
        DrawRectangleRoundedLinesEx(inner, round, 8, 1.5f, withAlpha(pal::Brass, enabled ? 0.55f : 0.2f));
        Color ink = enabled ? (hover ? pal::Highlight : pal::Brass) : Color{150, 130, 110, 255};
        drawTextCentered(FontId::UiBold, label, {body.x + body.width / 2, body.y + body.height / 2 - 1},
                         fontSize, Color{0, 0, 0, 120}, 0.5f);
        drawTextCentered(FontId::UiBold, label, {body.x + body.width / 2 - 1, body.y + body.height / 2 - 2},
                         fontSize, ink, 0.5f);
        break;
    }
    case ButtonStyle::Chalk: {
        DrawRectangleRounded(body, round, 8, withAlpha(pal::ChalkBoard, hover ? 0.95f : 0.85f));
        DrawRectangleRoundedLinesEx(body, round, 8, 2.f, withAlpha(pal::Chalk, enabled ? 0.7f : 0.25f));
        Color ink = enabled ? (hover ? pal::Highlight : pal::Chalk) : withAlpha(pal::Chalk, 0.35f);
        drawTextCentered(FontId::Chalk, label, {body.x + body.width / 2, body.y + body.height / 2}, fontSize,
                         ink);
        break;
    }
    case ButtonStyle::Paper: {
        DrawRectangleRounded({r.x + 1, r.y + 3, r.width, r.height}, round, 8, Color{0, 0, 0, 60});
        DrawRectangleRounded(body, round, 8, hover ? Color{255, 248, 226, 255} : pal::Paper);
        DrawRectangleRoundedLinesEx(body, round, 8, 1.5f, withAlpha(pal::PencilGray, enabled ? 0.6f : 0.2f));
        Color ink = enabled ? (hover ? pal::InkRed : pal::PencilGray) : withAlpha(pal::PencilGray, 0.35f);
        drawTextCentered(FontId::Hand, label, {body.x + body.width / 2, body.y + body.height / 2}, fontSize, ink);
        break;
    }
    }
    if (hover) requestHandCursor();
    return clicked && enabled;
}

void drawPanel(Rectangle r, PanelStyle style) {
    switch (style) {
    case PanelStyle::Wood: {
        DrawRectangleRounded({r.x + 6, r.y + 10, r.width, r.height}, 0.04f, 8, Color{0, 0, 0, 120});
        DrawRectangleRounded(r, 0.04f, 8, pal::WoodDark);
        Rectangle in{r.x + 10, r.y + 10, r.width - 20, r.height - 20};
        DrawRectangleRounded(in, 0.03f, 8, pal::Wood);
        // simple grain
        for (float y = in.y + 6; y < in.y + in.height - 4; y += 9.f) {
            float wob = std::sin(y * 0.13f) * 3.f;
            DrawLineEx({in.x + 8, y + wob}, {in.x + in.width - 8, y - wob}, 1.f, withAlpha(pal::WoodDark, 0.18f));
        }
        DrawRectangleRoundedLinesEx(in, 0.03f, 8, 2.f, withAlpha(pal::Brass, 0.45f));
        break;
    }
    case PanelStyle::Paper: {
        DrawRectangleRounded({r.x + 5, r.y + 8, r.width, r.height}, 0.015f, 6, Color{0, 0, 0, 90});
        DrawRectangleRounded(r, 0.015f, 6, pal::Paper);
        DrawRectangleLinesEx(r, 1.f, withAlpha(pal::PencilGray, 0.25f));
        break;
    }
    case PanelStyle::Dark: {
        DrawRectangleRounded(r, 0.06f, 8, Color{18, 12, 8, 215});
        DrawRectangleRoundedLinesEx(r, 0.06f, 8, 1.5f, withAlpha(pal::Brass, 0.5f));
        break;
    }
    }
}

namespace {
bool g_wantHand = false;
int g_cursor = MOUSE_CURSOR_DEFAULT;
} // namespace
void uiBeginFrame() { g_wantHand = false; }
void requestHandCursor() { g_wantHand = true; }
void uiEndFrame() {
    int want = g_wantHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT;
    if (want != g_cursor) {
        SetMouseCursor(want);
        g_cursor = want;
    }
}

void drawDim(float amount01) { DrawRectangle(-2000, -2000, 6000, 6000, Color{8, 5, 3, (unsigned char)(clamp01(amount01) * 255)}); }

// ---------------------------------------------------------------- math helpers
float clamp01(float t) { return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); }
float easeOutCubic(float t) {
    t = clamp01(t);
    float u = 1.f - t;
    return 1.f - u * u * u;
}
float easeInOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
}
float easeOutBack(float t) {
    t = clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1.f;
    float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}
float approach(float current, float target, float speed, float dt) {
    return target + (current - target) * std::exp(-speed * dt);
}
bool pointInRect(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

} // namespace ui
