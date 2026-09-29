// Okey tile rendering: supersampled face atlas + soft shadow texture. See TileRender.h.
#include "ui/TileRender.h"

#include "ui/Common.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ui {
namespace tilegfx {

namespace {

constexpr float PI_F = 3.14159265358979f;
constexpr int SS = 4;          // atlas pixels per tile unit (supersampling)
constexpr float PADU = 6.f;    // transparent margin around each cell, in tile units
constexpr float CELL_UW = W + 2 * PADU;
constexpr float CELL_UH = H + 2 * PADU;
constexpr int CELL_PW = (int)(CELL_UW * SS);
constexpr int CELL_PH = (int)(CELL_UH * SS);
constexpr int KEY_STAR = 54;
constexpr int NUM_CELLS = 55;
constexpr int ATLAS_COLS = 8;
constexpr float STAR_R = 24.f; // star outer radius (incl. outline) inside its cell, in units

// soft box texture: a W x H box with SOFT_M units of blur margin, SOFT_PPU pixels per unit
constexpr float SOFT_M = 12.f;
constexpr int SOFT_PPU = 2;

// 3D face atlas: face = F3W x F3H units, SS3 pixels per unit, P3 pixels of border padding per side
constexpr float F3W = 50.f;
constexpr float F3H = 70.f;
constexpr int SS3 = 3;
constexpr int P3 = 8;
constexpr int CELL3_PW = (int)F3W * SS3 + 2 * P3;
constexpr int CELL3_PH = (int)F3H * SS3 + 2 * P3;
constexpr int NUM_CELLS3 = 55;
constexpr int ATLAS3_COLS = 8;
constexpr int ATLAS3_ROWS = (NUM_CELLS3 + ATLAS3_COLS - 1) / ATLAS3_COLS;
constexpr int STAR3_PX = 128;

struct State {
    bool ready = false;
    Texture2D atlas{};
    Texture2D soft{};
    Texture2D atlas3{};
    Texture2D star3{};
};
State g;

Color rgba(int r, int gr, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)gr, (unsigned char)b, (unsigned char)a};
}
Color mulAlpha(Color c, float a) {
    c.a = (unsigned char)std::clamp(c.a * a + 0.5f, 0.f, 255.f);
    return c;
}
Color lighten(Color c, float t) { return lerpColor(c, WHITE, t); }
Color darken(Color c, float t) { return lerpColor(c, BLACK, t); }

Rectangle cellRect(int key) {
    const int col = key % ATLAS_COLS, row = key / ATLAS_COLS;
    return Rectangle{(float)(col * CELL_PW), (float)(row * CELL_PH), (float)CELL_PW, (float)CELL_PH};
}

// ------------------------------------------------------------------ geometry helpers
// Outline of a rounded rectangle, visually counter-clockwise on a y-down screen (front-facing in raylib).
void roundedOutline(Rectangle r, float radius, int seg, std::vector<Vector2>& out) {
    out.clear();
    radius = std::max(0.f, std::min(radius, std::min(r.width, r.height) * 0.5f));
    const Vector2 cs[4] = {{r.x + radius, r.y + radius},                         // top-left
                           {r.x + radius, r.y + r.height - radius},              // bottom-left
                           {r.x + r.width - radius, r.y + r.height - radius},    // bottom-right
                           {r.x + r.width - radius, r.y + radius}};              // top-right
    const float start[4] = {270.f, 180.f, 90.f, 0.f};                            // degrees, decreasing
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i <= seg; ++i) {
            const float a = (start[k] - 90.f * (float)i / (float)seg) * PI_F / 180.f;
            out.push_back({cs[k].x + std::cos(a) * radius, cs[k].y + std::sin(a) * radius});
        }
    }
}

// Triangle fan with a per-vertex colour function of y (linear gradients stay exact).
void fanGradV(Vector2 c, const std::vector<Vector2>& pts, float y0, float y1, Color top, Color bottom) {
    auto colAt = [&](float y) { return lerpColor(top, bottom, (y - y0) / std::max(0.001f, y1 - y0)); };
    const Color cc = colAt(c.y);
    rlBegin(RL_TRIANGLES);
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Vector2 a = pts[i], b = pts[(i + 1) % n];
        const Color ca = colAt(a.y), cb = colAt(b.y);
        rlColor4ub(cc.r, cc.g, cc.b, cc.a);
        rlVertex2f(c.x, c.y);
        rlColor4ub(ca.r, ca.g, ca.b, ca.a);
        rlVertex2f(a.x, a.y);
        rlColor4ub(cb.r, cb.g, cb.b, cb.a);
        rlVertex2f(b.x, b.y);
    }
    rlEnd();
}

// Closed polyline (thick) along pts.
void polyLine(const std::vector<Vector2>& pts, float thick, Color col) {
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) DrawLineEx(pts[i], pts[(i + 1) % n], thick, col);
}

// Star-shaped polygon (fan from centre); pts must go visually counter-clockwise.
void fanSolid(Vector2 c, const std::vector<Vector2>& pts, Color inner, Color outer) {
    rlBegin(RL_TRIANGLES);
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Vector2 a = pts[i], b = pts[(i + 1) % n];
        rlColor4ub(inner.r, inner.g, inner.b, inner.a);
        rlVertex2f(c.x, c.y);
        rlColor4ub(outer.r, outer.g, outer.b, outer.a);
        rlVertex2f(a.x, a.y);
        rlVertex2f(b.x, b.y);
    }
    rlEnd();
}

void starPoints(Vector2 c, float rOut, float rIn, int points, float rotDeg, std::vector<Vector2>& out) {
    out.clear();
    const int n = points * 2;
    for (int i = 0; i < n; ++i) {
        // decreasing angle = visually counter-clockwise
        const float a = (rotDeg - 90.f - 360.f * (float)i / (float)n) * PI_F / 180.f;
        const float r = (i % 2 == 0) ? rOut : rIn;
        out.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

void circle(Vector2 c, float r, Color col) { DrawCircleSector(c, r, 0.f, 360.f, 48, col); }

// ------------------------------------------------------------------ atlas painting (tile units)
struct Painter {
    Font font{};
    bool ownFont = false;
    std::vector<Vector2> tmp;

    void body(bool back) {
        // thickness (front side) with a darker lower edge
        roundedOutline({0, 3, W, H - 3}, 7.5f, 10, tmp);
        fanGradV({W / 2, (H + 3) / 2}, tmp, 3, H, rgba(222, 208, 176), rgba(168, 146, 110));
        polyLine(tmp, 0.7f, rgba(120, 98, 68, 150));
        // face rim (bevel): slightly darker ring, then the face itself inset
        const Color rimTop = back ? rgba(232, 220, 192) : rgba(240, 232, 210);
        const Color rimBot = back ? rgba(206, 192, 160) : rgba(214, 200, 170);
        roundedOutline({0, 0, W, FACE_H}, 7.5f, 10, tmp);
        fanGradV({W / 2, FACE_H / 2}, tmp, 0, FACE_H, rimTop, rimBot);
        const Color faceTop = back ? rgba(240, 231, 206) : rgba(252, 247, 232);
        const Color faceBot = back ? rgba(226, 214, 186) : rgba(240, 231, 208);
        roundedOutline({1.6f, 1.4f, W - 3.2f, FACE_H - 3.4f}, 6.2f, 10, tmp);
        fanGradV({W / 2, FACE_H / 2}, tmp, 1.4f, FACE_H - 2.f, faceTop, faceBot);
        // top-edge highlight and a thin shade line where face meets the side
        DrawLineEx({6.5f, 0.9f}, {W - 6.5f, 0.9f}, 0.9f, rgba(255, 255, 255, 190));
        DrawLineEx({6.5f, FACE_H - 0.4f}, {W - 6.5f, FACE_H - 0.4f}, 0.8f, rgba(150, 128, 94, 170));
    }

    // Vertical extent of a digit string in the font (for exact visual centring).
    void digitExtent(const std::string& s, float size, float& top, float& bottom) {
        const float scale = size / (float)font.baseSize;
        top = 1e9f;
        bottom = -1e9f;
        for (char ch : s) {
            const int gi = GetGlyphIndex(font, ch);
            const float t = font.glyphs[gi].offsetY * scale;
            const float b = t + font.recs[gi].height * scale;
            top = std::min(top, t);
            bottom = std::max(bottom, b);
        }
        if (top > bottom) {
            top = 0;
            bottom = size;
        }
    }

    void number(int color, int num) {
        const Color ink = tileInk(color);
        const std::string s = std::to_string(num);
        const float size = 40.f;
        const Vector2 m = MeasureTextEx(font, s.c_str(), size, 0.f);
        float top, bottom;
        digitExtent(s, size, top, bottom);
        const float maxW = (s.size() > 1) ? 40.f : 32.f;
        const float sx = m.x > maxW ? maxW / m.x : 1.f;
        const float cx = W / 2, cy = 26.f;
        const float gh = bottom - top;
        rlPushMatrix();
        rlTranslatef(cx, cy, 0);
        rlScalef(sx, 1.f, 1.f);
        const Vector2 p{-m.x / 2, -gh / 2 - top};
        // engraved: light rim below-right, soft dark rim above-left, then the ink
        DrawTextEx(font, s.c_str(), {p.x + 0.7f, p.y + 0.9f}, size, 0.f, rgba(255, 255, 255, 200));
        DrawTextEx(font, s.c_str(), {p.x - 0.35f, p.y - 0.45f}, size, 0.f, mulAlpha(darken(ink, 0.45f), 0.55f));
        DrawTextEx(font, s.c_str(), p, size, 0.f, ink);
        rlPopMatrix();
        // the small dot under the number
        const Vector2 dc{W / 2, 50.5f};
        circle({dc.x + 0.35f, dc.y + 0.5f}, 3.5f, rgba(255, 255, 255, 190));
        circle(dc, 3.4f, darken(ink, 0.12f));
        circle({dc.x - 0.8f, dc.y - 0.9f}, 1.1f, mulAlpha(lighten(ink, 0.55f), 0.8f));
    }

    void clover() {
        const Vector2 c{W / 2, 27.f};
        const Color dark = rgba(18, 74, 38);
        const Color leaf = rgba(40, 136, 72);
        const Color leafLight = rgba(96, 186, 112);
        // stem
        const Vector2 st[4] = {{c.x, c.y}, {c.x + 1.5f, c.y + 8.f}, {c.x + 5.5f, c.y + 13.f}, {c.x + 7.5f, c.y + 19.f}};
        DrawSplineBezierCubic(st, 4, 3.4f, dark);
        DrawSplineBezierCubic(st, 4, 1.8f, leaf);
        // four heart-shaped leaves, diagonal
        for (int pass = 0; pass < 2; ++pass) {
            const float grow = pass == 0 ? 0.9f : 0.f;
            const Color col = pass == 0 ? dark : leaf;
            for (int k = 0; k < 4; ++k) {
                const float a = (45.f + 90.f * k) * PI_F / 180.f;
                const Vector2 dir{std::cos(a), std::sin(a)};
                const Vector2 perp{-dir.y, dir.x};
                const Vector2 lc{c.x + dir.x * 6.8f, c.y + dir.y * 6.8f};
                const float lr = 4.3f + grow;
                circle({lc.x + perp.x * 2.6f, lc.y + perp.y * 2.6f}, lr, col);
                circle({lc.x - perp.x * 2.6f, lc.y - perp.y * 2.6f}, lr, col);
                // point toward the centre
                const Vector2 tip{c.x - dir.x * grow, c.y - dir.y * grow};
                const Vector2 l1{lc.x + perp.x * (6.4f + grow) + dir.x * 1.2f, lc.y + perp.y * (6.4f + grow) + dir.y * 1.2f};
                const Vector2 l2{lc.x - perp.x * (6.4f + grow) + dir.x * 1.2f, lc.y - perp.y * (6.4f + grow) + dir.y * 1.2f};
                DrawTriangle(tip, l1, l2, col);
                DrawTriangle(tip, l2, l1, col);
            }
        }
        // veins + light
        for (int k = 0; k < 4; ++k) {
            const float a = (45.f + 90.f * k) * PI_F / 180.f;
            const Vector2 dir{std::cos(a), std::sin(a)};
            DrawLineEx({c.x + dir.x * 2.0f, c.y + dir.y * 2.0f}, {c.x + dir.x * 7.5f, c.y + dir.y * 7.5f}, 0.6f,
                       mulAlpha(leafLight, 0.55f));
        }
        circle(c, 1.7f, leafLight);
        // small dot like the numbered tiles
        const Vector2 dc{W / 2, 50.5f};
        circle({dc.x + 0.35f, dc.y + 0.5f}, 3.2f, rgba(255, 255, 255, 190));
        circle(dc, 3.1f, dark);
    }

    void backEmblem() {
        const Vector2 c{W / 2, FACE_H / 2};
        const Color ink = rgba(156, 70, 46);
        const Color inkDark = rgba(110, 44, 28);
        // inset border
        roundedOutline({4.5f, 4.5f, W - 9.f, FACE_H - 9.f}, 4.f, 6, tmp);
        polyLine(tmp, 0.7f, mulAlpha(ink, 0.45f));
        // ring
        DrawRing(c, 10.6f, 11.6f, 0.f, 360.f, 64, mulAlpha(ink, 0.8f));
        // eight-pointed star (two squares)
        std::vector<Vector2> pts;
        starPoints(c, 10.4f, 5.4f, 8, 0.f, pts);
        fanSolid(c, pts, rgba(206, 120, 80), ink);
        polyLine(pts, 0.6f, inkDark);
        circle(c, 3.0f, rgba(246, 236, 212));
        circle(c, 1.4f, ink);
        // engraved highlight on the ring
        DrawRing({c.x + 0.4f, c.y + 0.6f}, 11.6f, 12.1f, 0.f, 180.f, 32, rgba(255, 255, 255, 120));
    }

    void star() {
        const Vector2 c{CELL_UW / 2 - PADU, CELL_UH / 2 - PADU};
        std::vector<Vector2> pts;
        starPoints(c, STAR_R, STAR_R * 0.47f, 5, 0.f, pts);
        fanSolid(c, pts, rgba(110, 60, 8), rgba(90, 48, 6));
        starPoints(c, STAR_R - 3.2f, (STAR_R - 3.2f) * 0.45f, 5, 0.f, pts);
        fanSolid({c.x - 2.f, c.y - 2.f}, pts, rgba(255, 238, 150), rgba(236, 160, 30));
        // shine
        starPoints({c.x, c.y}, (STAR_R - 3.2f) * 0.42f, (STAR_R - 3.2f) * 0.2f, 5, 0.f, pts);
        fanSolid({c.x, c.y}, pts, rgba(255, 252, 220, 220), rgba(255, 236, 160, 60));
    }

    void paint(int key) {
        if (key == KEY_STAR) {
            star();
            return;
        }
        body(key == KEY_BACK);
        if (key == KEY_BACK) backEmblem();
        else if (key == KEY_FAKE) clover();
        else number(key / 13, key % 13 + 1);
    }
};

Font loadTileFont() {
    // Same face as FontId::Tile (Arial Black), loaded large for the supersampled atlas.
    const char* files[] = {"/System/Library/Fonts/Supplemental/Arial Black.ttf",
                           "/System/Library/Fonts/Supplemental/Arial Bold.ttf"};
    int cps[10];
    for (int i = 0; i < 10; ++i) cps[i] = '0' + i;
    for (const char* f : files) {
        if (!FileExists(f)) continue;
        Font font = LoadFontEx(f, 180, cps, 10);
        if (font.texture.id != 0 && font.glyphCount > 0) {
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            return font;
        }
    }
    return Font{};
}

Texture2D buildSoftTexture() {
    const int w = (int)((W + 2 * SOFT_M) * SOFT_PPU), h = (int)((H + 2 * SOFT_M) * SOFT_PPU);
    Image img = GenImageColor(w, h, BLANK);
    Color* px = (Color*)img.data;
    const float hx = W / 2, hy = H / 2, rad = 8.f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float ux = (x + 0.5f) / SOFT_PPU - SOFT_M - hx;
            const float uy = (y + 0.5f) / SOFT_PPU - SOFT_M - hy;
            const float qx = std::fabs(ux) - (hx - rad), qy = std::fabs(uy) - (hy - rad);
            const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
            const float sdf = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - rad;
            float t = (sdf + 3.f) / (SOFT_M + 3.f); // 0 at 3 units inside, 1 at the margin
            t = std::clamp(t, 0.f, 1.f);
            const float a = (1.f - t) * (1.f - t) * (1.f + 2.f * t) * 0.5f + (1.f - t) * (1.f - t) * 0.5f;
            px[y * w + x] = Color{255, 255, 255, (unsigned char)std::clamp(a * 255.f, 0.f, 255.f)};
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

Texture2D buildAtlas() {
    const int rows = (NUM_CELLS + ATLAS_COLS - 1) / ATLAS_COLS;
    RenderTexture2D rt = LoadRenderTexture(ATLAS_COLS * CELL_PW, rows * CELL_PH);
    Painter p;
    p.font = loadTileFont();
    p.ownFont = p.font.texture.id != 0;
    if (!p.ownFont) p.font = ui::font(FontId::Tile);

    BeginTextureMode(rt);
    // warm dark brown with zero alpha: mip-mapped edges fade into a thin warm outline, not black
    ClearBackground(Color{104, 88, 64, 0});
    // premultiplied-safe blending: rgb as usual, alpha accumulates (keeps the tile body opaque)
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD,
                              RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    for (int key = 0; key < NUM_CELLS; ++key) {
        const Rectangle cr = cellRect(key);
        rlPushMatrix();
        rlTranslatef(cr.x + PADU * SS, cr.y + PADU * SS, 0);
        rlScalef((float)SS, (float)SS, 1.f);
        p.paint(key);
        rlPopMatrix();
    }
    EndBlendMode();
    EndTextureMode();

    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    UnloadRenderTexture(rt);
    if (p.ownFont) UnloadFont(p.font);

    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

// ------------------------------------------------------------------ 3D face atlas painting (face units)
Color ink3D(int color) {
    // a touch deeper than the 2D inks: the 3D faces are lit by warm tungsten light
    switch (color) {
    case okey::Yellow: return rgba(222, 154, 0); // golden: stays yellow under the warm lamp
    case okey::Blue: return rgba(20, 76, 188);
    case okey::Black: return rgba(24, 24, 28);
    case okey::Red: return rgba(204, 22, 32);
    default: return rgba(24, 24, 28);
    }
}

struct Painter3D {
    Font font{};
    std::vector<Vector2> tmp;

    static Color rimColor(bool back) { return back ? rgba(226, 214, 186) : rgba(234, 224, 198); }

    void face(bool back) {
        // the outer band lies on the 3D bevel: shade it a little toward the rim colour (the cell padding)
        const Color rim = rimColor(back);
        const Color mid = back ? rgba(234, 224, 198) : rgba(244, 237, 216);
        const Color top = back ? rgba(242, 233, 210) : rgba(253, 249, 238);
        const Color bot = back ? rgba(232, 221, 195) : rgba(244, 237, 218);
        roundedOutline({0.f, 0.f, F3W, F3H}, 7.f, 10, tmp);
        fanGradV({F3W / 2, F3H / 2}, tmp, 0.f, F3H, lerpColor(rim, mid, 0.35f), rim);
        roundedOutline({1.2f, 1.2f, F3W - 2.4f, F3H - 2.4f}, 6.f, 10, tmp);
        fanGradV({F3W / 2, F3H / 2}, tmp, 1.2f, F3H - 1.2f, lerpColor(mid, top, 0.4f), lerpColor(rim, bot, 0.6f));
        roundedOutline({2.6f, 2.6f, F3W - 5.2f, F3H - 5.2f}, 5.f, 10, tmp);
        fanGradV({F3W / 2, F3H / 2}, tmp, 2.6f, F3H - 2.6f, top, bot);
    }

    void digitExtent(const std::string& s, float size, float& top, float& bottom) {
        const float scale = size / (float)font.baseSize;
        top = 1e9f;
        bottom = -1e9f;
        for (char ch : s) {
            const int gi = GetGlyphIndex(font, ch);
            const float t = font.glyphs[gi].offsetY * scale;
            const float b = t + font.recs[gi].height * scale;
            top = std::min(top, t);
            bottom = std::max(bottom, b);
        }
        if (top > bottom) {
            top = 0;
            bottom = size;
        }
    }

    void number(int color, int num) {
        const Color ink = ink3D(color);
        const std::string s = std::to_string(num);
        const float size = 47.f;
        const Vector2 m = MeasureTextEx(font, s.c_str(), size, 0.f);
        float top, bottom;
        digitExtent(s, size, top, bottom);
        const float maxW = (s.size() > 1) ? 43.f : 34.f;
        const float sx = m.x > maxW ? maxW / m.x : 1.f;
        const float cx = F3W / 2, cy = 29.f;
        const float gh = bottom - top;
        rlPushMatrix();
        rlTranslatef(cx, cy, 0);
        rlScalef(sx, 1.f, 1.f);
        const Vector2 p{-m.x / 2, -gh / 2 - top};
        // engraved: a light lip below-right, a soft dark lip above-left, then the ink
        DrawTextEx(font, s.c_str(), {p.x + 0.8f, p.y + 1.0f}, size, 0.f, rgba(255, 255, 255, 210));
        DrawTextEx(font, s.c_str(), {p.x - 0.45f, p.y - 0.55f}, size, 0.f, mulAlpha(darken(ink, 0.5f), 0.6f));
        DrawTextEx(font, s.c_str(), p, size, 0.f, ink);
        rlPopMatrix();
        const Vector2 dc{F3W / 2, 57.5f};
        circle({dc.x + 0.45f, dc.y + 0.6f}, 4.6f, rgba(255, 255, 255, 200));
        circle(dc, 4.4f, darken(ink, 0.1f));
        circle({dc.x - 1.1f, dc.y - 1.2f}, 1.4f, mulAlpha(lighten(ink, 0.55f), 0.8f));
    }

    void backEmblem() {
        // plain ivory back with a small, softly debossed emblem (a busy back would crowd the table)
        const Vector2 c{F3W / 2, F3H / 2};
        const Color ink = rgba(178, 132, 96);
        const Color inkDark = rgba(150, 104, 72);
        roundedOutline({6.f, 6.f, F3W - 12.f, F3H - 12.f}, 4.5f, 8, tmp);
        polyLine(tmp, 0.7f, mulAlpha(ink, 0.30f));
        DrawRing(c, 9.2f, 10.0f, 0.f, 360.f, 64, mulAlpha(ink, 0.55f));
        std::vector<Vector2> pts;
        starPoints(c, 8.6f, 4.5f, 8, 0.f, pts);
        fanSolid(c, pts, mulAlpha(rgba(206, 150, 110), 0.75f), mulAlpha(ink, 0.8f));
        polyLine(pts, 0.5f, mulAlpha(inkDark, 0.6f));
        circle(c, 2.4f, rgba(242, 232, 208));
        DrawRing({c.x + 0.5f, c.y + 0.7f}, 10.0f, 10.5f, 0.f, 180.f, 36, rgba(255, 255, 255, 110));
    }

    void clover(Painter& p2d) {
        // the 2D clover (designed around (24, 27) with its dot at (24, 50.5)) scaled onto the 3D face
        const float s = 1.19f;
        rlPushMatrix();
        rlTranslatef(F3W / 2 - 24.f * s, 29.f - 27.f * s, 0.f);
        rlScalef(s, s, 1.f);
        p2d.clover();
        rlPopMatrix();
    }

    void paint(int key, Painter& p2d) {
        if (key == KEY_BODY) return; // padding colour only
        const bool back = key == KEY_BACK;
        face(back);
        if (back) backEmblem();
        else if (key == KEY_FAKE) clover(p2d);
        else number(key / 13, key % 13 + 1);
    }
};

Rectangle cell3Rect(int key) {
    const int col = key % ATLAS3_COLS, row = key / ATLAS3_COLS;
    return Rectangle{(float)(col * CELL3_PW), (float)(row * CELL3_PH), (float)CELL3_PW, (float)CELL3_PH};
}

Texture2D finishTexture(RenderTexture2D& rt, bool anisotropic) {
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    UnloadRenderTexture(rt);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    // Trilinear first: raylib's ANISOTROPIC filters only set the anisotropy level and would leave the
    // texture at its load-time GL_NEAREST min/mag filters (aliased, broken digits on tiles seen at a grazing
    // angle, e.g. the melds across the table). Anisotropy is capped at the GPU's maximum by rlgl.
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    if (anisotropic) rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, 16);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

Texture2D buildAtlas3D() {
    RenderTexture2D rt = LoadRenderTexture(ATLAS3_COLS * CELL3_PW, ATLAS3_ROWS * CELL3_PH);
    Painter p2d;
    Painter3D p;
    p.font = loadTileFont();
    const bool ownFont = p.font.texture.id != 0;
    if (!ownFont) p.font = ui::font(FontId::Tile);
    p2d.font = p.font;

    BeginTextureMode(rt);
    ClearBackground(Painter3D::rimColor(false));
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD,
                              RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    for (int key = 0; key < NUM_CELLS3; ++key) {
        const Rectangle cr = cell3Rect(key);
        // padding = the cell's own border colour (no bleeding into neighbours at any mip level)
        const Color pad = key == KEY_BODY ? rgba(232, 222, 196) : Painter3D::rimColor(key == KEY_BACK);
        DrawRectangleRec(cr, pad);
        rlPushMatrix();
        rlTranslatef(cr.x + (float)P3, cr.y + (float)P3, 0);
        rlScalef((float)SS3, (float)SS3, 1.f);
        p.paint(key, p2d);
        rlPopMatrix();
    }
    EndBlendMode();
    EndTextureMode();
    if (ownFont) UnloadFont(p.font);
    return finishTexture(rt, true);
}

Texture2D buildStar3D() {
    RenderTexture2D rt = LoadRenderTexture(STAR3_PX, STAR3_PX);
    Painter p;
    BeginTextureMode(rt);
    ClearBackground(Color{150, 90, 20, 0});
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD,
                              RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    const float s = (STAR3_PX * 0.5f - 3.f) / STAR_R;
    rlPushMatrix();
    rlTranslatef(STAR3_PX * 0.5f, STAR3_PX * 0.5f, 0.f);
    rlScalef(s, s, 1.f);
    rlTranslatef(-(CELL_UW / 2 - PADU), -(CELL_UH / 2 - PADU), 0.f);
    p.star();
    rlPopMatrix();
    EndBlendMode();
    EndTextureMode();
    return finishTexture(rt, false);
}

// Draws a padded atlas cell so that the tile body (W x H units) maps to w x heightFor(w) at `c`.
void drawCell(int key, Vector2 c, float w, float sx, float rotDeg, Color tint) {
    const float s = w / W;
    const Rectangle src = cellRect(key);
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    if (rotDeg != 0.f) rlRotatef(rotDeg, 0, 0, 1);
    rlScalef(s * sx, s, 1.f);
    DrawTexturePro(g.atlas, src, {-W / 2 - PADU, -H / 2 - PADU, CELL_UW, CELL_UH}, {0, 0}, 0.f, tint);
    rlPopMatrix();
}

} // namespace

// ================================================================== public
int faceKeyOf(int tileId) {
    if (!okey::isValidTile(tileId)) return KEY_BACK;
    if (okey::isFakeJoker(tileId)) return KEY_FAKE;
    return faceKey(okey::printedColor(tileId), okey::printedNumber(tileId));
}

void init() {
    if (g.ready) return;
    g.atlas = buildAtlas();
    g.soft = buildSoftTexture();
    g.atlas3 = buildAtlas3D();
    g.star3 = buildStar3D();
    g.ready = g.atlas.id != 0;
}

void shutdown() {
    if (g.atlas.id) UnloadTexture(g.atlas);
    if (g.soft.id) UnloadTexture(g.soft);
    if (g.atlas3.id) UnloadTexture(g.atlas3);
    if (g.star3.id) UnloadTexture(g.star3);
    g = State{};
}

bool ready() { return g.ready; }
Texture2D atlasTexture() { return g.atlas; }
Texture2D faceAtlas3D() { return g.atlas3; }
Texture2D starTexture3D() { return g.star3; }

Rectangle faceUV3D(int key) {
    key = std::clamp(key, 0, NUM_CELLS3 - 1);
    const Rectangle c = cell3Rect(key);
    const float aw = (float)(ATLAS3_COLS * CELL3_PW), ah = (float)(ATLAS3_ROWS * CELL3_PH);
    return Rectangle{(c.x + (float)P3) / aw, (c.y + (float)P3) / ah, F3W * SS3 / aw, F3H * SS3 / ah};
}

void roundedRect(Rectangle r, float radius, Color c, int segPerCorner) {
    const float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRounded(r, std::clamp(radius * 2.f / m, 0.f, 1.f), segPerCorner, c);
}

void roundedLines(Rectangle r, float radius, float thick, Color c, int segPerCorner) {
    const float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRoundedLinesEx(r, std::clamp(radius * 2.f / m, 0.f, 1.f), segPerCorner, thick, c);
}

void roundedGradV(Rectangle r, float radius, Color top, Color bottom, int segPerCorner) {
    if (r.width <= 0 || r.height <= 0) return;
    std::vector<Vector2> pts;
    roundedOutline(r, radius, segPerCorner, pts);
    fanGradV({r.x + r.width / 2, r.y + r.height / 2}, pts, r.y, r.y + r.height, top, bottom);
}

void drawSoftBox(Vector2 c, float w, float h, float rotDeg, Color col) {
    if (!g.ready || col.a == 0) return;
    const float sx = w / W, sy = h / H;
    const float dw = (W + 2 * SOFT_M) * sx, dh = (H + 2 * SOFT_M) * sy;
    DrawTexturePro(g.soft, {0, 0, (float)g.soft.width, (float)g.soft.height}, {c.x, c.y, dw, dh}, {dw / 2, dh / 2},
                   rotDeg, col);
}

void drawStar(Vector2 c, float radius, float rotDeg, float alpha) {
    if (!g.ready) return;
    // the star cell: star centred in the cell, outer radius STAR_R units
    const float s = radius / STAR_R;
    const Rectangle src = cellRect(KEY_STAR);
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    if (rotDeg != 0.f) rlRotatef(rotDeg, 0, 0, 1);
    rlScalef(s, s, 1.f);
    DrawTexturePro(g.atlas, src, {-CELL_UW / 2, -CELL_UH / 2, CELL_UW, CELL_UH}, {0, 0}, 0.f,
                   mulAlpha(WHITE, alpha));
    rlPopMatrix();
}

void drawKey(int key, Vector2 c, float w, float rotDeg, const Fx& fx) {
    if (!g.ready || w <= 0.5f || fx.alpha <= 0.003f) return;
    const float s = w / W;
    const float flip = std::clamp(fx.flip, 0.f, 1.f);
    const bool showFace = flip >= 0.5f;
    const float sx = std::max(0.02f, std::fabs(flip * 2.f - 1.f));
    const float h = heightFor(w);

    // shadow: offset down-right, grows and softens with lift
    if (fx.shadow) {
        const float lift = std::max(0.f, fx.lift);
        const float a = fx.alpha * fx.shadowAlpha * (0.42f - std::min(0.2f, lift * 0.12f));
        const Vector2 off{(1.5f + lift * 7.f) * s, (3.0f + lift * 12.f) * s};
        drawSoftBox({c.x + off.x, c.y + off.y}, w * sx * (1.f + lift * 0.1f) + 2 * s, h * (1.f + lift * 0.08f),
                    rotDeg, Color{20, 12, 6, (unsigned char)std::clamp(a * 255.f, 0.f, 255.f)});
    }
    if (fx.glow > 0.003f) {
        Color gc = fx.glowColor;
        gc.a = (unsigned char)std::clamp(gc.a * fx.glow * fx.alpha, 0.f, 255.f);
        drawSoftBox(c, w * 1.12f + 10 * s, h * 1.08f + 10 * s, rotDeg, gc);
    }

    const float lum = 1.f - std::clamp(fx.dim, 0.f, 1.f) * 0.55f;
    Color tint{(unsigned char)(fx.tint.r * lum), (unsigned char)(fx.tint.g * lum), (unsigned char)(fx.tint.b * lum),
               (unsigned char)std::clamp(fx.tint.a * fx.alpha, 0.f, 255.f)};
    drawCell(showFace ? key : KEY_BACK, c, w, sx, rotDeg, tint);

    // overlays in tile units
    const bool badge = showFace && fx.okeyBadge;
    if (badge || fx.outline > 0.003f) {
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        if (rotDeg != 0.f) rlRotatef(rotDeg, 0, 0, 1);
        rlScalef(s * sx, s, 1.f);
        if (fx.outline > 0.003f) {
            Color oc = fx.outlineColor;
            oc.a = (unsigned char)std::clamp(oc.a * fx.outline * fx.alpha, 0.f, 255.f);
            roundedLines({-W / 2 - 1.2f, -H / 2 - 1.2f, W + 2.4f, H + 2.4f}, 8.5f, 2.4f, oc, 8);
        }
        rlPopMatrix();
        if (badge) {
            // star in the top-right corner of the face (computed in world space so it isn't squashed twice)
            const float ang = rotDeg * PI_F / 180.f;
            const Vector2 local{(W / 2 - 8.5f) * s * sx, (-H / 2 + 8.5f) * s};
            const Vector2 p{c.x + local.x * std::cos(ang) - local.y * std::sin(ang),
                            c.y + local.x * std::sin(ang) + local.y * std::cos(ang)};
            drawStar(p, 6.2f * s * std::max(0.2f, sx), rotDeg, fx.alpha);
        }
    }
}

void drawTile(int tileId, const okey::OkeyInfo& ok, Vector2 c, float w, float rotDeg, Fx fx) {
    if (ok.isJoker(tileId)) fx.okeyBadge = true;
    drawKey(faceKeyOf(tileId), c, w, rotDeg, fx);
}

void drawBack(Vector2 c, float w, float rotDeg, const Fx& fx) {
    Fx f = fx;
    f.flip = 0.f;
    f.okeyBadge = false;
    drawKey(KEY_BACK, c, w, rotDeg, f);
}

} // namespace tilegfx
} // namespace ui
