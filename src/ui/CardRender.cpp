// Playing card faces, painted once into a supersampled atlas (see CardRender.h).
#include "ui/CardRender.h"

#include "ui/Common.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace ui {
namespace cardgfx {

namespace {

constexpr int SS = 4;                     // atlas pixels per millimetre
constexpr int PAD = 6;                    // pixels of padding around every cell
constexpr int CW = (int)W * SS, CH = (int)H * SS;
constexpr int CELL_W = CW + 2 * PAD, CELL_H = CH + 2 * PAD;
constexpr int COLS = 8, ROWS = (NUM_KEYS + COLS - 1) / COLS;
constexpr float RADIUS = 3.6f;            // corner radius (mm)

const Color kStock{247, 242, 230, 255};   // card stock
const Color kStockEdge{214, 204, 184, 255};
const Color kRed{184, 30, 38, 255};
const Color kBlack{26, 24, 30, 255};
const Color kGold{214, 164, 60, 255};
const Color kGoldDark{150, 104, 30, 255};
const Color kBlue{34, 64, 140, 255};
const Color kSkin{240, 210, 176, 255};
const Color kBordeaux{118, 22, 32, 255};

const Color kBlueSuit{20, 86, 196, 255};  // four-colour deck: karo
const Color kGreenSuit{18, 128, 62, 255};  // four-colour deck: sinek

Texture2D gAtlas{};
bool gReady = false;
bool gFour = false, gWantFour = false;

// Ink of a suit: maça black, kupa red, karo red / blue, sinek black / green (four-colour deck).
Color suitInk(int st) {
    if (gFour) {
        switch (st) {
        case 1: return kRed;
        case 2: return kBlueSuit;
        case 3: return kGreenSuit;
        default: return kBlack;
        }
    }
    return (st == 1 || st == 2) ? kRed : kBlack;
}

Color mix(Color a, Color b, float t) {
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    // raylib wants counter-clockwise order on screen (y down): try both
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, col);
    else DrawTriangle(a, c, b, col);
}

// A filled ellipse made of triangles (DrawEllipse works in integer pixels, too coarse in mm units).
void fillEllipse(Vector2 c, float rx, float ry, Color col, int seg = 36) {
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2.f * PI * (float)i / (float)seg, a1 = 2.f * PI * (float)(i + 1) / (float)seg;
        tri(c, {c.x + std::cos(a0) * rx, c.y + std::sin(a0) * ry}, {c.x + std::cos(a1) * rx, c.y + std::sin(a1) * ry}, col);
    }
}

void fillCircle(Vector2 c, float r, Color col) { fillEllipse(c, r, r, col); }

// Suit symbol centred at c, `s` mm tall. `up` = false draws it upside down (pips in the lower half).
void suit(int st, Vector2 c, float s, Color col, bool up = true) {
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0.f);
    if (!up) rlRotatef(180.f, 0.f, 0.f, 1.f);
    const float r = s * 0.26f;
    switch (st) {
    case 1: // kupa (heart)
        fillCircle({-r * 0.98f, -r * 0.6f}, r * 1.04f, col);
        fillCircle({r * 0.98f, -r * 0.6f}, r * 1.04f, col);
        tri({-r * 1.98f, -r * 0.38f}, {0.f, s * 0.5f}, {r * 1.98f, -r * 0.38f}, col);
        tri({-r * 0.5f, -r * 0.9f}, {r * 0.5f, -r * 0.9f}, {0.f, 0.f}, col);
        break;
    case 2: // karo (diamond), slightly concave sides
        tri({0.f, -s * 0.5f}, {-s * 0.37f, 0.f}, {s * 0.37f, 0.f}, col);
        tri({-s * 0.37f, 0.f}, {0.f, s * 0.5f}, {s * 0.37f, 0.f}, col);
        break;
    case 3: // sinek (club)
        fillCircle({0.f, -r * 1.02f}, r * 0.98f, col);
        fillCircle({-r * 1.05f, r * 0.36f}, r * 0.98f, col);
        fillCircle({r * 1.05f, r * 0.36f}, r * 0.98f, col);
        fillCircle({0.f, 0.f}, r * 0.6f, col);
        tri({0.f, -r * 0.2f}, {-r * 0.85f, s * 0.5f}, {r * 0.85f, s * 0.5f}, col);
        break;
    default: // maça (spade)
        fillCircle({-r * 0.98f, r * 0.42f}, r * 1.04f, col);
        fillCircle({r * 0.98f, r * 0.42f}, r * 1.04f, col);
        tri({-r * 1.98f, r * 0.2f}, {r * 1.98f, r * 0.2f}, {0.f, -s * 0.5f}, col);
        tri({0.f, r * 0.1f}, {-r * 0.85f, s * 0.5f}, {r * 0.85f, s * 0.5f}, col);
        break;
    }
    rlPopMatrix();
}

struct Painter {
    Font font{};

    void text(const std::string& s, Vector2 center, float size, Color col) const {
        const Vector2 m = MeasureTextEx(font, s.c_str(), size, 0.f);
        DrawTextEx(font, s.c_str(), {center.x - m.x * 0.5f, center.y - m.y * 0.5f}, size, 0.f, col);
    }

    void stock() const {
        DrawRectangleRounded({0.f, 0.f, W, H}, RADIUS * 2.f / W, 10, kStockEdge);
        DrawRectangleRounded({0.25f, 0.25f, W - 0.5f, H - 0.5f}, RADIUS * 2.f / W, 10, kStock);
    }

    // rank letter + a small suit under it, top-left (and rotated, bottom-right)
    void corners(int st, int rank) const {
        static const char* const idx[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "V", "K", "P", "A"};
        const std::string s = idx[rank - 2];
        const Color col = suitInk(st);
        for (int k = 0; k < 2; ++k) {
            rlPushMatrix();
            if (k == 1) {
                rlTranslatef(W, H, 0.f);
                rlRotatef(180.f, 0.f, 0.f, 1.f);
            }
            text(s, {6.6f, 7.4f}, s.size() > 1 ? 8.4f : 9.4f, col);
            suit(st, {6.6f, 15.f}, 5.2f, col);
            rlPopMatrix();
        }
    }

    void pips(int st, int n) const {
        const Color col = suitInk(st);
        const float L = 21.f, C = 31.5f, R = 42.f;
        auto p = [&](float x, float y) { suit(st, {x, y}, 10.f, col, y <= 44.5f); };
        const float top = 20.f, bot = 68.f;
        switch (n) {
        case 2: p(C, top); p(C, bot); break;
        case 3: p(C, top); p(C, 44.f); p(C, bot); break;
        case 4: p(L, top); p(R, top); p(L, bot); p(R, bot); break;
        case 5: p(L, top); p(R, top); p(C, 44.f); p(L, bot); p(R, bot); break;
        case 6: for (float y : {top, 44.f, bot}) { p(L, y); p(R, y); } break;
        case 7: for (float y : {top, 44.f, bot}) { p(L, y); p(R, y); } p(C, 32.f); break;
        case 8: for (float y : {top, 44.f, bot}) { p(L, y); p(R, y); } p(C, 32.f); p(C, 56.f); break;
        case 9: for (float y : {top, 36.f, 52.f, bot}) { p(L, y); p(R, y); } p(C, 44.f); break;
        case 10: for (float y : {top, 36.f, 52.f, bot}) { p(L, y); p(R, y); } p(C, 28.f); p(C, 60.f); break;
        default: break;
        }
    }

    void ace(int st) const {
        const Color col = suitInk(st);
        if (st == 0) { // the Maça As wears an ornament
            for (int i = 0; i < 3; ++i)
                DrawRing({W * 0.5f, H * 0.5f}, 15.f + 2.2f * (float)i, 15.6f + 2.2f * (float)i, 0.f, 360.f, 72,
                         mix(kGold, kStock, 0.25f * (float)i));
        }
        suit(st, {W * 0.5f, H * 0.5f + (st == 0 ? 0.f : 0.5f)}, st == 0 ? 22.f : 26.f, col);
    }

    // One half of a court figure (the top half; the card draws it twice, the second turned 180 degrees).
    void courtHalf(int st, int rank) const {
        const bool red = st == 1 || st == 2;
        const Color robe = red ? kRed : kBlue;
        const Color robe2 = red ? kBlue : kRed;
        const Color ink = suitInk(st);
        const float cx = 31.5f;
        // robe: shoulders down to the divider
        tri({cx - 15.f, 44.f}, {cx + 15.f, 44.f}, {cx - 11.f, 31.f}, robe);
        tri({cx + 15.f, 44.f}, {cx + 11.f, 31.f}, {cx - 11.f, 31.f}, robe);
        // a panel down the chest and a gold collar
        tri({cx - 4.5f, 44.f}, {cx + 4.5f, 44.f}, {cx - 3.f, 31.f}, robe2);
        tri({cx + 4.5f, 44.f}, {cx + 3.f, 31.f}, {cx - 3.f, 31.f}, robe2);
        for (int i = 0; i < 3; ++i) fillCircle({cx, 35.f + 3.f * (float)i}, 0.75f, kGold);
        DrawRectangleRounded({cx - 11.5f, 29.5f, 23.f, 3.f}, 0.8f, 6, kGold);
        // head
        if (rank == 12) { // Kız: long hair behind the face
            fillEllipse({cx, 24.5f}, 7.f, 7.5f, Color{110, 66, 32, 255});
        }
        fillEllipse({cx, 23.5f}, 4.6f, 5.4f, kSkin);
        fillCircle({cx - 1.8f, 22.6f}, 0.55f, kBlack);
        fillCircle({cx + 1.8f, 22.6f}, 0.55f, kBlack);
        if (rank == 12) {
            fillEllipse({cx, 26.6f}, 1.3f, 0.6f, Color{200, 60, 70, 255});
        } else {
            // mustache
            fillEllipse({cx - 1.4f, 25.6f}, 1.6f, 0.6f, Color{70, 42, 26, 255});
            fillEllipse({cx + 1.4f, 25.6f}, 1.6f, 0.6f, Color{70, 42, 26, 255});
        }
        if (rank == 13) { // Papaz: beard and a crown
            tri({cx - 4.4f, 25.f}, {cx + 4.4f, 25.f}, {cx, 30.6f}, Color{120, 76, 40, 255});
            fillEllipse({cx - 1.4f, 25.6f}, 1.6f, 0.6f, Color{70, 42, 26, 255});
            fillEllipse({cx + 1.4f, 25.6f}, 1.6f, 0.6f, Color{70, 42, 26, 255});
            DrawRectangleRec({cx - 5.f, 16.2f, 10.f, 2.4f}, kGold);
            for (int i = 0; i < 4; ++i) {
                const float x = cx - 5.f + 3.33f * (float)i;
                tri({x, 16.4f}, {x + 3.33f, 16.4f}, {x + 1.66f, 12.4f}, kGold);
                fillCircle({x + 1.66f, 12.4f}, 0.6f, kGoldDark);
            }
            // sceptre
            DrawLineEx({cx + 12.f, 41.f}, {cx + 12.f, 15.f}, 1.1f, kGoldDark);
            fillCircle({cx + 12.f, 14.f}, 1.6f, kGold);
        } else if (rank == 12) { // Kız: a tiara and a flower
            DrawRectangleRec({cx - 4.2f, 17.f, 8.4f, 1.6f}, kGold);
            for (int i = 0; i < 3; ++i) {
                const float x = cx - 4.2f + 2.8f * (float)i;
                tri({x, 17.2f}, {x + 2.8f, 17.2f}, {x + 1.4f, 14.4f}, kGold);
            }
            DrawLineEx({cx - 11.f, 42.f}, {cx - 11.f, 24.f}, 0.8f, Color{60, 120, 60, 255});
            for (int i = 0; i < 5; ++i) {
                const float a = 2.f * PI * (float)i / 5.f;
                fillCircle({cx - 11.f + std::cos(a) * 1.6f, 22.5f + std::sin(a) * 1.6f}, 1.1f, Color{214, 70, 90, 255});
            }
            fillCircle({cx - 11.f, 22.5f}, 0.9f, kGold);
        } else { // Vale: a cap with a feather and a staff
            fillEllipse({cx, 18.2f}, 6.2f, 2.6f, robe2);
            DrawRectangleRec({cx - 4.6f, 18.f, 9.2f, 1.5f}, kGold);
            tri({cx + 3.f, 17.f}, {cx + 9.f, 10.5f}, {cx + 5.f, 18.f}, Color{232, 226, 210, 255});
            DrawLineEx({cx - 12.f, 43.f}, {cx - 12.f, 16.f}, 1.f, Color{120, 80, 40, 255});
            tri({cx - 13.4f, 17.f}, {cx - 10.6f, 17.f}, {cx - 12.f, 12.5f}, Color{170, 170, 180, 255});
        }
        // the suit, small, in the panel's corner
        suit(st, {16.6f, 14.6f}, 5.4f, ink);
    }

    void court(int st, int rank) const {
        const bool red = st == 1 || st == 2;
        const Rectangle fr{12.f, 10.f, 39.f, 68.f};
        DrawRectangleRec(fr, mix(kStock, red ? Color{250, 226, 210, 255} : Color{222, 230, 246, 255}, 0.8f));
        courtHalf(st, rank);
        rlPushMatrix();
        rlTranslatef(W, H, 0.f);
        rlRotatef(180.f, 0.f, 0.f, 1.f);
        courtHalf(st, rank);
        rlPopMatrix();
        DrawLineEx({fr.x, 44.f}, {fr.x + fr.width, 44.f}, 0.5f, kGoldDark);
        DrawRectangleLinesEx(fr, 0.6f, kGoldDark);
        DrawRectangleLinesEx({fr.x - 1.f, fr.y - 1.f, fr.width + 2.f, fr.height + 2.f}, 0.35f, suitInk(st));
    }

    void back() const {
        DrawRectangleRounded({0.f, 0.f, W, H}, RADIUS * 2.f / W, 10, kStockEdge);
        DrawRectangleRounded({0.25f, 0.25f, W - 0.5f, H - 0.5f}, RADIUS * 2.f / W, 10, kStock);
        const Rectangle in{3.2f, 3.2f, W - 6.4f, H - 6.4f};
        DrawRectangleRounded(in, 0.06f, 8, kBordeaux);
        // a fine diamond lattice
        const Color line = Color{168, 52, 58, 255};
        for (float d = -H; d < W + H; d += 3.4f) {
            const Vector2 a{in.x + d, in.y}, b{in.x + d - in.height, in.y + in.height};
            const Vector2 c{in.x + d, in.y}, e{in.x + d + in.height, in.y + in.height};
            auto clipLine = [&](Vector2 p, Vector2 q) {
                // clip to the inner rectangle (parametric)
                float t0 = 0.f, t1 = 1.f;
                const float dx = q.x - p.x, dy = q.y - p.y;
                const float pp[4] = {-dx, dx, -dy, dy};
                const float qq[4] = {p.x - (in.x + 0.6f), (in.x + in.width - 0.6f) - p.x, p.y - (in.y + 0.6f),
                                     (in.y + in.height - 0.6f) - p.y};
                for (int i = 0; i < 4; ++i) {
                    if (pp[i] == 0.f) {
                        if (qq[i] < 0.f) return;
                        continue;
                    }
                    const float r = qq[i] / pp[i];
                    if (pp[i] < 0.f) t0 = std::max(t0, r);
                    else t1 = std::min(t1, r);
                }
                if (t0 >= t1) return;
                DrawLineEx({p.x + dx * t0, p.y + dy * t0}, {p.x + dx * t1, p.y + dy * t1}, 0.32f, line);
            };
            clipLine(a, b);
            clipLine(c, e);
        }
        DrawRectangleRoundedLinesEx({in.x + 1.4f, in.y + 1.4f, in.width - 2.8f, in.height - 2.8f}, 0.06f, 8, 0.45f, kGold);
        // the medallion
        fillEllipse({W * 0.5f, H * 0.5f}, 12.5f, 16.f, kGold);
        fillEllipse({W * 0.5f, H * 0.5f}, 11.4f, 14.9f, kBordeaux);
        fillEllipse({W * 0.5f, H * 0.5f}, 10.f, 13.4f, Color{96, 16, 26, 255});
        text("SB", {W * 0.5f, H * 0.5f + 0.4f}, 12.f, kGold);
        suit(1, {W * 0.5f, H * 0.5f - 10.f}, 3.6f, kGold);
        suit(0, {W * 0.5f, H * 0.5f + 10.f}, 3.6f, kGold);
    }

    // Konken: the joker — a jester's head under a three-pointed cap with bells, "JOKER" down the corners.
    void joker() const {
        stock();
        const float cx = W * 0.5f;
        DrawRectangleRoundedLinesEx({4.f, 4.f, W - 8.f, H - 8.f}, 0.06f, 8, 0.45f, kGold);
        // the cap: three points (red, black, red) with gold bells
        const Color capC[3] = {kRed, kBlack, kRed};
        const Vector2 tip[3] = {{cx - 15.f, 27.f}, {cx, 15.5f}, {cx + 15.f, 27.f}};
        for (int i = 0; i < 3; ++i) {
            const float bx = cx - 9.f + 9.f * (float)i;
            tri({bx - 5.5f, 42.f}, {bx + 5.5f, 42.f}, tip[i], capC[i]);
            fillCircle(tip[i], 1.9f, kGold);
            fillCircle(tip[i], 0.8f, kGoldDark);
        }
        DrawRectangleRounded({cx - 14.f, 40.5f, 28.f, 3.6f}, 0.8f, 6, kGold);
        // the face
        fillEllipse({cx, 50.5f}, 8.6f, 9.2f, kSkin);
        fillCircle({cx - 3.2f, 48.6f}, 0.9f, kBlack);
        fillCircle({cx + 3.2f, 48.6f}, 0.9f, kBlack);
        fillCircle({cx - 5.4f, 52.4f}, 1.4f, Color{236, 150, 140, 255});
        fillCircle({cx + 5.4f, 52.4f}, 1.4f, Color{236, 150, 140, 255});
        for (int i = 0; i < 8; ++i) { // a wide grin
            const float a0 = (20.f + 17.5f * (float)i) * DEG2RAD, a1 = (20.f + 17.5f * (float)(i + 1)) * DEG2RAD;
            DrawLineEx({cx + std::cos(a0) * 4.2f, 52.2f + std::sin(a0) * 2.8f}, {cx + std::cos(a1) * 4.2f, 52.2f + std::sin(a1) * 2.8f},
                       0.7f, Color{150, 30, 40, 255});
        }
        // a zigzag collar
        for (int i = 0; i < 6; ++i) {
            const float x = cx - 15.f + 5.f * (float)i;
            tri({x, 59.f}, {x + 5.f, 59.f}, {x + 2.5f, 68.f}, i % 2 ? kGold : kBlue);
        }
        // "JOKER" down the top-left corner and (turned) the bottom-right one
        static const char* const letters[5] = {"J", "O", "K", "E", "R"};
        for (int k = 0; k < 2; ++k) {
            rlPushMatrix();
            if (k == 1) {
                rlTranslatef(W, H, 0.f);
                rlRotatef(180.f, 0.f, 0.f, 1.f);
            }
            for (int i = 0; i < 5; ++i) text(letters[i], {6.4f, 7.f + 6.2f * (float)i}, 6.6f, k ? kBlack : kRed);
            rlPopMatrix();
        }
    }

    void paint(int key) const {
        if (key == KEY_JOKER) {
            joker();
            return;
        }
        if (key == KEY_BODY) {
            DrawRectangleRec({0.f, 0.f, W, H}, kStock);
            return;
        }
        if (key == KEY_BACK) {
            back();
            return;
        }
        const int st = key / 13, rank = key % 13 + 2;
        stock();
        corners(st, rank);
        if (rank == 14) ace(st);
        else if (rank >= 11) court(st, rank);
        else pips(st, rank);
    }
};

Font loadCardFont() {
    const char* files[] = {"/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
                           "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
                           "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"};
    const char* glyphs = "0123456789VKPASBJOER"; // (J O E R: the joker's "JOKER")
    int cps[20];
    for (int i = 0; i < 20; ++i) cps[i] = glyphs[i];
    for (const char* f : files) {
        if (!FileExists(f)) continue;
        Font font = LoadFontEx(f, 160, cps, 20);
        if (font.texture.id != 0 && font.glyphCount > 0) {
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            return font;
        }
    }
    return Font{};
}

Rectangle cellRect(int key) {
    const int col = key % COLS, row = key / COLS;
    return {(float)(col * CELL_W), (float)(row * CELL_H), (float)CELL_W, (float)CELL_H};
}

Image paintAtlas() {
    RenderTexture2D rt = LoadRenderTexture(COLS * CELL_W, ROWS * CELL_H);
    if (rt.id == 0) return Image{};
    Painter p;
    p.font = loadCardFont();
    const bool ownFont = p.font.texture.id != 0;
    if (!ownFont) p.font = ui::font(FontId::UiBold);

    BeginTextureMode(rt);
    ClearBackground(Color{247, 242, 230, 0});
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    for (int key = 0; key < NUM_KEYS; ++key) {
        const Rectangle cr = cellRect(key);
        rlPushMatrix();
        rlTranslatef(cr.x + (float)PAD, cr.y + (float)PAD, 0.f);
        rlScalef((float)SS, (float)SS, 1.f);
        p.paint(key);
        rlPopMatrix();
    }
    EndBlendMode();
    EndTextureMode();
    if (ownFont) UnloadFont(p.font);

    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    UnloadRenderTexture(rt);
    return img;
}

} // namespace

void init() {
    if (gReady) return;
    gFour = gWantFour;
    Image img = paintAtlas();
    if (!img.data) return;
    gAtlas = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&gAtlas);
    SetTextureFilter(gAtlas, TEXTURE_FILTER_TRILINEAR);
    rlTextureParameters(gAtlas.id, RL_TEXTURE_FILTER_ANISOTROPIC, 16);
    SetTextureWrap(gAtlas, TEXTURE_WRAP_CLAMP);
    gReady = gAtlas.id != 0;
}

void shutdown() {
    if (gAtlas.id) UnloadTexture(gAtlas);
    gAtlas = Texture2D{};
    gReady = false;
}

bool ready() { return gReady; }

void setFourColour(bool on) { gWantFour = on; }
bool fourColour() { return gWantFour; }

void refresh() {
    if (!gReady || gFour == gWantFour) return;
    gFour = gWantFour;
    Image img = paintAtlas();
    if (!img.data) return;
    UpdateTexture(gAtlas, img.data);
    UnloadImage(img);
    GenTextureMipmaps(&gAtlas);
}

Texture2D atlas() { return gAtlas; }

Rectangle uv(int key) {
    key = std::clamp(key, 0, NUM_KEYS - 1);
    const Rectangle c = cellRect(key);
    const float aw = (float)(COLS * CELL_W), ah = (float)(ROWS * CELL_H);
    return {(c.x + (float)PAD) / aw, (c.y + (float)PAD) / ah, (float)CW / aw, (float)CH / ah};
}

void drawCard(int key, Vector2 c, float w, float rotDeg, Color tint) {
    if (!gReady) return;
    key = std::clamp(key, 0, NUM_KEYS - 1);
    const Rectangle cr = cellRect(key);
    const Rectangle src{cr.x + (float)PAD, cr.y + (float)PAD, (float)CW, (float)CH};
    const float h = w * H / W;
    DrawTexturePro(gAtlas, src, {c.x, c.y, w, h}, {w * 0.5f, h * 0.5f}, rotDeg, tint);
}

} // namespace cardgfx
} // namespace ui
