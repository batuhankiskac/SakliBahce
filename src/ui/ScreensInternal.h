#pragma once
// Screens internals (screens owner): the helpers, tables and layout every part of the screens shares, and
// Screens::Impl itself. Not a public API. The parts: Screens.cpp (the state machine, title, game list, guide, stats,
// analysis, replays, badges, pause, match over, the public API), ScreensRules.cpp (the rules pages: the embedded
// markdown, the okey and 101 pages, layout, scrolling, drawing), ScreensSettings.cpp (Ayarlar: logic and pages),
// ScreensSheets.cpp (the score sheets: the okey hand summary, the other games' sheet and its match over).
#include "ui/Screens.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
namespace screens_detail {

// ============================================================================ constants & small helpers

constexpr float kFadeTime = 0.25f;
constexpr float kPi = 3.14159265f;
constexpr Vector2 kNoMouse{-100000.f, -100000.f};

constexpr Color kInk{44, 32, 24, 255};        // dark ink on paper
constexpr Color kGraphite{58, 58, 70, 255};   // pencil
constexpr Color kBluePen{30, 58, 138, 255};   // the player's own column
constexpr Color kRedPencil{190, 34, 30, 255};
constexpr Color kGridBlue{104, 146, 196, 255};
constexpr Color kMarginRed{214, 96, 92, 255};
constexpr Color kSignRed{132, 42, 22, 255};
constexpr Color kGold{238, 198, 112, 255};

const int kHandChoices[] = {1, 3, 5, 7, 9, 11};
const int kOkeyStartChoices[] = {6, 12, 20};
const int kTavlaChoices[] = {3, 5, 7};
const int kBatakTargets[] = {31, 51, 71};
const int kPistiTargets[] = {101, 151};
const int kDamaWins[] = {1, 3, 5}; // Dama: games to win the match
const int kBezikTargets[] = {500, 1000, 1500}; // Bezik: the match's points
const int kY101Acma[] = {51, 81, 101, 121}; // 101 kuralları: açma sınırı (app/Rules101.h)
const int kKonkenOpens[] = {40, 51, 71};    // Konken: the opening's least value
const int kKonkenLimits[] = {101, 151, 201}; // Konken: "yanar"
// The closest choice's index (settings loaded from an older file may hold any value).
template <size_t N>
int closest(const int (&choices)[N], int v) {
    int best = 0;
    for (int i = 1; i < (int)N; ++i)
        if (std::abs(choices[i] - v) < std::abs(choices[best] - v)) best = i;
    return best;
}
const float kAnimChoices[] = {0.6f, 1.f, 1.7f};
const char* const kLevelNames[] = {"Acemi", "Usta", "Kurt"};
const char* const kLevelHints[] = {"Rahat rakipler, ara sıra hata yapar", "Sağlam, dikkatli oyuncular",
                                   "Taş sayar, açığını kollar, affetmez"};
const char* const kDash = "\xE2\x80\x94"; // —

inline Color alphaMul(Color c, float a) {
    c.a = (unsigned char)std::lround((float)c.a * clamp01(a));
    return c;
}
inline Color rgba(int r, int g, int b, float a) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)std::lround(255.f * clamp01(a))};
}

inline uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float hash01(uint32_t x) { return (float)(hashU(x) & 0xFFFFFFu) / 16777216.f; }

inline Vector2 vadd(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 vlerp(Vector2 a, Vector2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
inline Vector2 rotateDeg(Vector2 p, float deg) {
    const float r = deg * kPi / 180.f, c = std::cos(r), s = std::sin(r);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

// Scores use an en dash as the minus sign (the typographic minus is not in the font atlas).
inline std::string scoreText(int v) { return v < 0 ? std::string("\xE2\x80\x93") + std::to_string(-v) : std::to_string(v); }

// ---------------------------------------------------------------- UTF-8 (player name input)
inline void utf8PopBack(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}
inline void utf8Append(std::string& s, int cp) {
    int n = 0;
    const char* bytes = CodepointToUTF8(cp, &n);
    s.append(bytes, (size_t)n);
}
// Codepoints the UI fonts can draw and that make sense in a name.
inline bool nameCodepointOk(int cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) return true;
    switch (cp) {
    case ' ': case '.': case '-': case '_':
    case 0xC7: case 0xE7: case 0x11E: case 0x11F: case 0x130: case 0x131: case 0xD6: case 0xF6:
    case 0x15E: case 0x15F: case 0xDC: case 0xFC: case 0xC2: case 0xE2: case 0xCE: case 0xEE:
    case 0xDB: case 0xFB: case 0xE9:
        return true;
    default:
        return false;
    }
}
inline std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------- alpha shader for cross-fades
const char* const kFadeFs330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float fade;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    finalColor = vec4(c.rgb, c.a * fade);\n"
    "}\n";
const char* const kFadeFs120 =
    "#version 120\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float fade;\n"
    "void main() {\n"
    "    vec4 c = texture2D(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    gl_FragColor = vec4(c.rgb, c.a * fade);\n"
    "}\n";

struct FadeShader {
    Shader shader{};
    int loc = -1;
    bool ok = false;
    bool tried = false;

    void load() {
        if (ok || tried || !IsWindowReady()) return;
        tried = true;
        const int ver = rlGetVersion();
        const char* fs = (ver == RL_OPENGL_33 || ver == RL_OPENGL_43) ? kFadeFs330
                         : (ver == RL_OPENGL_21)                      ? kFadeFs120
                                                                      : nullptr;
        if (!fs) return;
        shader = LoadShaderFromMemory(nullptr, fs);
        ok = shader.id != 0 && shader.id != rlGetShaderIdDefault();
        loc = ok ? GetShaderLocation(shader, "fade") : -1;
        if (ok && loc < 0) {
            unload();
            tried = true; // no "fade" uniform: don't recompile every frame, draw without the shader
        }
    }
    void unload() {
        // after CloseWindow there is no GL context left to delete the program in
        if (IsWindowReady() && shader.id != 0 && shader.id != rlGetShaderIdDefault()) UnloadShader(shader);
        shader = Shader{};
        ok = false;
        tried = false;
        loc = -1;
    }
    // Uniform values are read when the batch flushes, so every layer is wrapped in its own
    // Begin/EndShaderMode (EndShaderMode flushes with this layer's value).
    void begin(float alpha) {
        SetShaderValue(shader, loc, &alpha, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(shader);
    }
    void end() { EndShaderMode(); }
};

// ============================================================================ drawing primitives

// raylib culls clockwise triangles; make sure every triangle is emitted in its preferred winding.
inline void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

inline void drawStar(Vector2 c, float r, Color col, float rotDeg = -90.f) {
    Vector2 pts[10];
    for (int k = 0; k < 10; ++k) {
        const float a = (rotDeg + 36.f * (float)k) * kPi / 180.f;
        const float rad = (k % 2 == 0) ? r : r * 0.45f;
        pts[k] = {c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
    }
    for (int k = 0; k < 10; ++k) tri(c, pts[k], pts[(k + 1) % 10], col);
}

inline void drawClover(Vector2 c, float s, Color col) {
    DrawLineEx(c, {c.x + s * 0.28f, c.y + s * 0.95f}, s * 0.16f, col);
    for (int i = 0; i < 4; ++i) {
        const float a = (45.f + 90.f * (float)i) * kPi / 180.f;
        DrawCircleV({c.x + std::cos(a) * s * 0.40f, c.y + std::sin(a) * s * 0.40f}, s * 0.34f, col);
    }
    DrawCircleV(c, s * 0.22f, col);
}

// A hand-drawn line: a few segments with a slight perpendicular wobble.
inline void pencilLine(Vector2 a, Vector2 b, float thick, Color col, uint32_t seed, float wobble = 1.1f) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const Vector2 n{-dy / len, dx / len};
    const int segs = std::max(2, (int)(len / 36.f));
    Vector2 prev = a;
    for (int i = 1; i <= segs; ++i) {
        const float t = (float)i / (float)segs;
        const float w = (i < segs) ? (hash01(seed * 131u + (uint32_t)i) - 0.5f) * 2.f * wobble : 0.f;
        const Vector2 p{a.x + dx * t + n.x * w, a.y + dy * t + n.y * w};
        DrawLineEx(prev, p, thick, col);
        if (i < segs) DrawCircleV(p, thick * 0.5f, col);
        prev = p;
    }
}

// A hand-drawn ellipse drawn progressively (0..1), slightly overshooting like a real pencil circle.
inline void pencilEllipse(Vector2 c, float rx, float ry, float progress, float thick, Color col, uint32_t seed) {
    progress = clamp01(progress);
    if (progress <= 0.f) return;
    const int segs = 72;
    const float turn = 2.f * kPi * 1.1f;
    const float a0 = -2.4f + hash01(seed) * 0.5f;
    const int n = std::max(1, (int)((float)segs * progress));
    auto at = [&](int i) {
        const float t = (float)i / (float)segs;
        const float a = a0 - turn * t;
        const float wob = 1.f + 0.035f * std::sin(t * 9.f + (float)seed) + 0.07f * t;
        return Vector2{c.x + std::cos(a) * rx * wob, c.y + std::sin(a) * ry * wob};
    };
    Vector2 prev = at(0);
    for (int i = 1; i <= n; ++i) {
        const Vector2 p = at(i);
        const float taper = (i < 6) ? 0.55f + 0.09f * (float)i : 1.f;
        DrawLineEx(prev, p, thick * taper, col);
        DrawCircleV(p, thick * taper * 0.5f, col);
        prev = p;
    }
}

// Glowing neon text (tube + halo). `on` 0..1 (flicker).
inline void drawNeonText(const std::string& s, Vector2 center, float size, float spacing, float on) {
    const Vector2 ms = measureText(FontId::Sign, s, size, spacing);
    const Vector2 pos{center.x - ms.x * 0.5f, center.y - ms.y * 0.5f};
    rlPushMatrix();
    rlTranslatef(center.x, center.y, 0.f);
    rlScalef(ms.x * 0.0085f, size * 0.0075f, 1.f);
    DrawCircleGradient({0, 0}, 100.f, rgba(255, 70, 36, 0.30f * on), rgba(255, 70, 36, 0.f));
    rlPopMatrix();
    drawText(FontId::Sign, s, {pos.x + 2, pos.y + 3}, size, rgba(0, 0, 0, 0.45f), spacing);
    drawText(FontId::Sign, s, pos, size, Color{96, 34, 26, 255}, spacing); // unlit glass tube
    for (int i = 0; i < 12; ++i) {
        const float a = (float)i * kPi / 6.f;
        drawText(FontId::Sign, s, {pos.x + std::cos(a) * 5.f, pos.y + std::sin(a) * 5.f}, size,
                 rgba(255, 64, 32, 0.10f * on), spacing);
    }
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * kPi / 4.f;
        drawText(FontId::Sign, s, {pos.x + std::cos(a) * 2.2f, pos.y + std::sin(a) * 2.2f}, size,
                 rgba(255, 96, 56, 0.28f * on), spacing);
    }
    drawText(FontId::Sign, s, pos, size, lerpColor(Color{120, 40, 30, 255}, Color{255, 226, 200, 255}, on), spacing);
}

inline float neonFlicker(float t) {
    float f = 0.94f + 0.06f * std::sin(t * 17.f) * std::sin(t * 5.3f);
    const float c = std::fmod(t, 7.3f);
    if (c > 6.35f && c < 6.8f && std::sin(t * 61.f) > 0.15f) f *= 0.3f;
    return f;
}

// A chain hanging from a ceiling hook to an eyelet.
inline void drawChain(Vector2 a, Vector2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    const int links = std::max(2, (int)(len / 9.f));
    for (int i = 0; i <= links; ++i) {
        const Vector2 p = vlerp(a, b, (float)i / (float)links);
        if (i % 2 == 0) {
            DrawRing(p, 2.2f, 4.4f, 0, 360, 14, Color{60, 48, 30, 255});
            DrawRing(p, 2.6f, 3.6f, 200, 300, 6, alphaMul(pal::Brass, 0.8f));
        } else {
            DrawRectangleRec({p.x - 1.4f, p.y - 4.5f, 2.8f, 9.f}, Color{70, 56, 34, 255});
        }
    }
}

inline void drawRivet(Vector2 p, float r) {
    DrawCircleV({p.x + 1, p.y + 1.5f}, r, rgba(0, 0, 0, 0.45f));
    DrawCircleV(p, r, Color{130, 96, 44, 255});
    DrawCircleV({p.x - r * 0.2f, p.y - r * 0.2f}, r * 0.72f, pal::Brass);
    DrawCircleV({p.x - r * 0.4f, p.y - r * 0.4f}, r * 0.28f, rgba(255, 250, 230, 0.7f));
}

// ---------------------------------------------------------------- handwriting (score sheet)
// Noteworthy draws "1" as a bare stroke that reads like "l"; add the little flag people write.
inline void handText(const std::string& s, Vector2 pos, float size, Color c) {
    const Font& f = font(FontId::Hand);
    if (f.glyphCount <= 0 || f.baseSize <= 0) {
        drawText(FontId::Hand, s, pos, size, c);
        return;
    }
    const float scale = size / (float)f.baseSize;
    float x = pos.x;
    const char* p = s.c_str();
    while (*p) {
        int n = 0;
        const int cp = GetCodepointNext(p, &n);
        const int idx = GetGlyphIndex(f, cp);
        const GlyphInfo& gi = f.glyphs[idx];
        const Rectangle rc = f.recs[idx];
        const bool one = cp == '1';
        if (one) x += size * 0.07f;
        if (cp != ' ' && cp != '\t') DrawTextCodepoint(f, cp, {x, pos.y}, size, c);
        if (one) {
            const Vector2 top{x + ((float)gi.offsetX + rc.width * 0.60f) * scale, pos.y + (float)gi.offsetY * scale + size * 0.03f};
            DrawLineEx(top, {top.x - size * 0.13f, top.y + size * 0.12f}, std::max(1.2f, size * 0.05f), c);
            x += size * 0.02f;
        }
        x += (gi.advanceX != 0) ? (float)gi.advanceX * scale : rc.width * scale;
        p += (n > 0 ? n : 1);
    }
}
inline Vector2 handMeasure(const std::string& s, float size) {
    Vector2 m = measureText(FontId::Hand, s, size);
    for (char ch : s)
        if (ch == '1') m.x += size * 0.09f;
    return m;
}
inline void handTextCentered(const std::string& s, Vector2 center, float size, Color c) {
    const Vector2 m = handMeasure(s, size);
    handText(s, {std::round(center.x - m.x * 0.5f), std::round(center.y - m.y * 0.5f)}, size, c);
}

// ---------------------------------------------------------------- mini tiles (rules illustrations)
struct MiniTile {
    int color = 0;
    int number = 1;
    int kind = 0;      // 0 numbered, 1 okey (wild), 2 sahte okey
    bool mark = false; // highlighted (e.g. the tile being işlendi)
};
constexpr float kMiniW = 37.f, kMiniH = 50.f, kMiniGap = 4.f;

inline void drawMiniTile(Rectangle r, const MiniTile& t) {
    const float rd = 0.22f;
    if (t.mark) DrawRectangleRounded({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, rd, 6, alphaMul(pal::Good, 0.75f));
    DrawRectangleRounded({r.x + 1.5f, r.y + 3.f, r.width, r.height}, rd, 6, rgba(0, 0, 0, 0.28f));
    DrawRectangleRounded(r, rd, 6, pal::TileEdge);
    const Rectangle face{r.x, r.y, r.width, r.height - 3.5f};
    DrawRectangleRounded(face, rd, 6, pal::TileFace);
    DrawRectangleRounded({face.x + 2, face.y + face.height * 0.62f, face.width - 4, face.height * 0.34f}, 0.4f, 6,
                         alphaMul(pal::TileFaceShade, 0.45f));
    DrawRectangleRoundedLinesEx(face, rd, 6, 1.f, alphaMul(pal::TileEdge, 0.7f));
    const float cx = r.x + r.width * 0.5f;
    if (t.kind == 2) {
        drawClover({cx, r.y + r.height * 0.40f}, r.width * 0.30f, Color{36, 112, 64, 255});
        return;
    }
    const Color ink = tileInk(t.color);
    drawTextCentered(FontId::Tile, std::to_string(t.number), {cx, r.y + r.height * 0.37f}, r.height * 0.47f, ink);
    DrawCircleV({cx, r.y + r.height * 0.73f}, r.width * 0.085f, ink);
    if (t.kind == 1) {
        const Vector2 sc{r.x + r.width - 7.f, r.y + 7.f};
        drawStar(sc, 6.5f, Color{120, 70, 0, 255});
        drawStar(sc, 5.f, Color{250, 190, 30, 255});
    }
}

// ---------------------------------------------------------------- confetti
struct Confetti {
    Vector2 p{0, 0};
    Vector2 v{0, 0};
    float rot = 0.f, vrot = 0.f;
    float flip = 0.f, vflip = 0.f;
    float size = 8.f;
    float phase = 0.f;
    Color color = WHITE;
    bool alive = true;
};

// ---------------------------------------------------------------- tea glass (match-over "trophy")
inline float glassWidthAt(float t) { // ince belli: wide rim, narrow waist, round belly
    if (t < 0.52f) return 1.f - 0.40f * std::sin(t / 0.52f * kPi * 0.5f);
    const float u = (t - 0.52f) / 0.48f;
    return 0.60f + 0.34f * std::sin(u * kPi * 0.78f);
}

inline void drawSteam(Vector2 base, float time, float scale, float alpha, float rise = 90.f) {
    for (int i = 0; i < 7; ++i) {
        const float ph = std::fmod(time * 0.32f + (float)i / 7.f, 1.f);
        const float y = base.y - ph * rise * scale;
        const float x = base.x + std::sin(ph * 5.5f + (float)i * 1.7f) * 10.f * scale + ((float)(i % 3) - 1.f) * 6.f;
        const float r = (7.f + ph * 16.f) * scale;
        const Color c = alphaMul(pal::Smoke, std::sin(ph * kPi) * 0.30f * alpha);
        DrawCircleGradient({x, y}, r, c, alphaMul(c, 0.f));
    }
}

inline void drawTeaGlass(Vector2 base, float s, float time, float steamRise = 90.f) {
    // saucer
    rlPushMatrix();
    rlTranslatef(base.x, base.y, 0.f);
    rlScalef(1.f, 0.26f, 1.f);
    DrawCircleV({0, 10}, 54.f * s, rgba(0, 0, 0, 0.35f));
    DrawCircleV({0, 0}, 52.f * s, Color{176, 34, 34, 255});
    DrawCircleV({0, -3}, 44.f * s, Color{238, 232, 222, 255});
    DrawCircleV({0, -3}, 22.f * s, Color{214, 206, 192, 255});
    rlPopMatrix();
    // glass body, sliced
    const float h = 84.f * s, halfW = 29.f * s;
    const float top = base.y - 4.f * s - h;
    for (int i = 0; i < (int)h; ++i) {
        const float t = (float)i / h;
        const float w = glassWidthAt(t) * halfW;
        Color c;
        if (t < 0.13f) c = rgba(255, 246, 232, 0.22f);
        else c = lerpColor(Color{206, 84, 26, 235}, Color{120, 34, 10, 250}, (t - 0.13f) / 0.87f);
        DrawRectangleRec({base.x - w, top + (float)i, 2.f * w, 1.25f}, c);
    }
    // outline + highlights
    Vector2 prevL{0, 0}, prevR{0, 0};
    for (int i = 0; i <= 24; ++i) {
        const float t = (float)i / 24.f;
        const float w = glassWidthAt(t) * halfW;
        const Vector2 L{base.x - w, top + t * h}, R{base.x + w, top + t * h};
        if (i > 0) {
            DrawLineEx(prevL, L, 1.6f, rgba(255, 250, 240, 0.55f));
            DrawLineEx(prevR, R, 1.6f, rgba(255, 250, 240, 0.35f));
        }
        prevL = L;
        prevR = R;
    }
    DrawLineEx({base.x - halfW * 0.62f, top + h * 0.2f}, {base.x - halfW * 0.44f, top + h * 0.42f}, 3.f,
               rgba(255, 255, 255, 0.45f));
    DrawLineEx({base.x - halfW * 0.52f, top + h * 0.66f}, {base.x - halfW * 0.62f, top + h * 0.86f}, 3.f,
               rgba(255, 255, 255, 0.35f));
    DrawLineEx({base.x - halfW, top}, {base.x + halfW, top}, 2.f, rgba(255, 250, 240, 0.7f));
    DrawLineEx({base.x - halfW * 0.98f, top + h * 0.13f}, {base.x + halfW * 0.98f, top + h * 0.13f}, 1.5f,
               rgba(255, 190, 120, 0.6f));
    drawSteam({base.x, top - 4.f}, time, s, 1.f, steamRise);
}

// A yellow pencil lying on the score sheet.
inline void drawPencil(Vector2 p, float angleDeg) {
    rlPushMatrix();
    rlTranslatef(p.x, p.y, 0.f);
    rlRotatef(angleDeg, 0.f, 0.f, 1.f);
    const float L = 230.f, W = 15.f;
    DrawRectangleRec({-L * 0.5f + 6.f, -W * 0.5f + 6.f, L, W}, rgba(0, 0, 0, 0.22f));
    DrawRectangleRec({-L * 0.5f + 40.f, -W * 0.5f, L - 78.f, W}, Color{232, 178, 36, 255});
    DrawRectangleRec({-L * 0.5f + 40.f, -W * 0.5f + W * 0.36f, L - 78.f, W * 0.28f}, Color{248, 204, 70, 255});
    DrawRectangleRec({-L * 0.5f + 40.f, W * 0.5f - 3.f, L - 78.f, 3.f}, Color{190, 136, 20, 255});
    // ferrule + eraser
    DrawRectangleRec({L * 0.5f - 38.f, -W * 0.5f - 0.5f, 16.f, W + 1.f}, Color{178, 178, 170, 255});
    DrawLineEx({L * 0.5f - 33.f, -W * 0.5f}, {L * 0.5f - 33.f, W * 0.5f}, 1.2f, Color{120, 120, 112, 255});
    DrawLineEx({L * 0.5f - 27.f, -W * 0.5f}, {L * 0.5f - 27.f, W * 0.5f}, 1.2f, Color{120, 120, 112, 255});
    DrawRectangleRounded({L * 0.5f - 23.f, -W * 0.5f, 22.f, W}, 0.4f, 6, Color{226, 128, 130, 255});
    // sharpened wood + graphite tip
    tri({-L * 0.5f + 40.f, -W * 0.5f}, {-L * 0.5f + 40.f, W * 0.5f}, {-L * 0.5f + 6.f, 0.f}, Color{226, 190, 140, 255});
    tri({-L * 0.5f + 16.f, -W * 0.18f}, {-L * 0.5f + 16.f, W * 0.18f}, {-L * 0.5f + 5.f, 0.f}, Color{50, 50, 56, 255});
    rlPopMatrix();
}

// ---------------------------------------------------------------- the games
const GameInfo kGames[(int)GameKind::Count] = {
    {"101", "101", "4 kişi", "Per aç, 101'i geç, elini bitir. Düşük puan kazanır."},
    {"Eşli 101", "EŞLİ 101", "4 kişi \xC2\xB7 eşli", "Karşındaki ortağın: biriniz bitirince ötekinin eli silinir."},
    {"Okey", "OKEY", "4 kişi", "14 taşı per ya da yedi çift yap, 15.'yü atıp bit. 20'den geri sayılır."},
    {"Tavla", "TAVLA", "2 kişi", "Zarı at, pullarını topla. Mars iki sayı."},
    {"Pişti", "PİŞTİ", "4 kişi", "Aynı kağıt ya da vale yerdekileri alır. Tek kağıda pişti!"},
    {"Batak", "BATAK", "4 kişi", "İhaleyi al, kozu sen söyle; sözünü tutamazsan batarsın."},
    {"King", "KING", "4 kişi", "Yirmi el: cezalardan kaç, kozda el topla."},
    {"Dama", "DAMA", "2 kişi", "Taşını ileri ve yana sür, alabildiğini al. Sona varan dama olur."},
    {"Altmışaltı", "66", "2 kişi", "24 kâğıt, koz ve evlilikler. 66'yı ilk bulan eli alır."},
    {"Bezik", "BEZİK", "2 kişi", "Çift deste: bezik, seri, dörtlü topla, löveleri al."},
    {"Konken", "KONKEN", "4 kişi", "Per ve seri yap, yerdekilere işle, elinde kâğıt kalmasın."},
};

// ---------------------------------------------------------------- small drawings (game cards)
// A card suit (0 maça, 1 kupa, 2 karo, 3 sinek) centred at c, about `s` tall.
inline void drawSuit(Vector2 c, float s, int suit, Color col) {
    const float r = s * 0.27f;
    switch (suit) {
    case 1: // kupa (heart)
        DrawCircleV({c.x - r * 0.95f, c.y - r * 0.55f}, r, col);
        DrawCircleV({c.x + r * 0.95f, c.y - r * 0.55f}, r, col);
        tri({c.x - r * 1.9f, c.y - r * 0.35f}, {c.x, c.y + s * 0.5f}, {c.x + r * 1.9f, c.y - r * 0.35f}, col);
        break;
    case 2: // karo (diamond)
        tri({c.x, c.y - s * 0.5f}, {c.x - s * 0.36f, c.y}, {c.x + s * 0.36f, c.y}, col);
        tri({c.x - s * 0.36f, c.y}, {c.x, c.y + s * 0.5f}, {c.x + s * 0.36f, c.y}, col);
        break;
    case 3: // sinek (club)
        DrawCircleV({c.x, c.y - r * 1.05f}, r, col);
        DrawCircleV({c.x - r * 1.05f, c.y + r * 0.3f}, r, col);
        DrawCircleV({c.x + r * 1.05f, c.y + r * 0.3f}, r, col);
        tri({c.x, c.y}, {c.x - r * 0.9f, c.y + s * 0.5f}, {c.x + r * 0.9f, c.y + s * 0.5f}, col);
        break;
    default: // maça (spade)
        DrawCircleV({c.x - r * 0.95f, c.y + r * 0.35f}, r, col);
        DrawCircleV({c.x + r * 0.95f, c.y + r * 0.35f}, r, col);
        tri({c.x - r * 1.9f, c.y + r * 0.15f}, {c.x + r * 1.9f, c.y + r * 0.15f}, {c.x, c.y - s * 0.5f}, col);
        tri({c.x, c.y + r * 0.2f}, {c.x - r * 0.9f, c.y + s * 0.5f}, {c.x + r * 0.9f, c.y + s * 0.5f}, col);
        break;
    }
}

// A playing card face (corner index + a big suit), tilted by `deg` around its centre.
inline void drawMiniCard(Vector2 c, float w, float deg, const char* idx, int suit) {
    const float h = w * 1.42f;
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0.f);
    rlRotatef(deg, 0.f, 0.f, 1.f);
    DrawRectangleRounded({-w * 0.5f + 2.f, -h * 0.5f + 4.f, w, h}, 0.14f, 6, rgba(0, 0, 0, 0.35f));
    DrawRectangleRounded({-w * 0.5f, -h * 0.5f, w, h}, 0.14f, 6, Color{246, 240, 226, 255});
    DrawRectangleRoundedLinesEx({-w * 0.5f, -h * 0.5f, w, h}, 0.14f, 6, 1.f, Color{150, 132, 110, 255});
    const Color col = (suit == 1 || suit == 2) ? Color{186, 32, 36, 255} : Color{30, 28, 34, 255};
    drawText(FontId::UiBold, idx, {-w * 0.5f + 5.f, -h * 0.5f + 3.f}, w * 0.3f, col);
    drawSuit({-w * 0.5f + 5.f + w * 0.1f, -h * 0.5f + w * 0.42f}, w * 0.16f, suit, col);
    drawSuit({0.f, h * 0.06f}, w * 0.5f, suit, col);
    rlPopMatrix();
}

// A die showing `n` pips.
inline void drawMiniDie(Vector2 c, float s, int n, float deg) {
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0.f);
    rlRotatef(deg, 0.f, 0.f, 1.f);
    DrawRectangleRounded({-s * 0.5f + 2.f, -s * 0.5f + 4.f, s, s}, 0.25f, 6, rgba(0, 0, 0, 0.35f));
    DrawRectangleRounded({-s * 0.5f, -s * 0.5f, s, s}, 0.25f, 6, Color{244, 238, 222, 255});
    DrawRectangleRoundedLinesEx({-s * 0.5f, -s * 0.5f, s, s}, 0.25f, 6, 1.f, Color{150, 132, 110, 255});
    const float o = s * 0.26f, pr = s * 0.085f;
    const Color pc{40, 30, 26, 255};
    auto pip = [&](float x, float y) { DrawCircleV({x, y}, pr, pc); };
    if (n % 2 == 1) pip(0, 0);
    if (n >= 2) { pip(-o, -o); pip(o, o); }
    if (n >= 4) { pip(o, -o); pip(-o, o); }
    if (n == 6) { pip(-o, 0); pip(o, 0); }
    rlPopMatrix();
}

// ---------------------------------------------------------------- clipping in virtual units
inline void beginClip(Rectangle r) {
    const Viewport& vp = currentViewport();
    const float s = vp.scale > 0.f ? vp.scale : 1.f;
    const int x = (int)std::floor(vp.offset.x + r.x * s);
    const int y = (int)std::floor(vp.offset.y + r.y * s);
    const int w = (int)std::ceil(r.width * s);
    const int h = (int)std::ceil(r.height * s);
    BeginScissorMode(x, y, w, h);
}

// ============================================================================ layout constants

namespace L {
// title
constexpr Rectangle TitlePlay{640, 470, 320, 68};
// the rows under "Oyna" (see titleRows): 70 apart from y 556, 280 wide (a row of two: 136 each)
constexpr float TitleRowY = 556.f, TitleRowStep = 70.f, TitleRowX = 660.f, TitleRowW = 280.f, TitleRowH = 56.f;
// stats
constexpr Rectangle StatsPanel{250, 40, 1100, 820};
constexpr Rectangle StatsBack{690, 776, 220, 58};
// settings
constexpr Rectangle SetPanel{330, 40, 940, 820};
constexpr float SetLabelX = 385.f;
constexpr float SetCtrlX = 720.f;
// name, hands, level, anim, hints, katlamalı, yandan ceza | sfx, ambient, music
constexpr float SetRowH = 50.f;     // rows of the settings list; section headings take SetHeadH
constexpr float SetHeadH = 40.f;
constexpr float SetTopY = 150.f;    // the first section heading
constexpr Rectangle SetName{720, 183, 340, 46};
constexpr Rectangle SetDefaults{385, 778, 230, 54};
constexpr Rectangle SetBack{985, 776, 230, 58};
// rules
constexpr Rectangle RulesPanel{290, 16, 1020, 868};
constexpr Rectangle RulesSheet{322, 134, 956, 656};
constexpr Rectangle RulesView{372, 150, 842, 624};  // text column (scissored)
constexpr Rectangle RulesTrack{1242, 156, 12, 612}; // scrollbar track
constexpr float RulesTabsY = 106.f;                 // section tabs under the title
constexpr Rectangle RulesBack{690, 806, 220, 58};
// pause
constexpr Rectangle PausePanel{590, 104, 420, 652};
constexpr Rectangle PauseBtn[5] = {{650, 328, 300, 58}, {650, 400, 300, 58}, {650, 472, 300, 58}, {650, 544, 300, 58},
                                   {650, 616, 300, 58}};
constexpr Rectangle ConfirmPanel{530, 318, 540, 250};
constexpr Rectangle ConfirmYes{575, 478, 220, 58};
constexpr Rectangle ConfirmNo{805, 478, 220, 58};
// hand summary sheet
constexpr Rectangle Sheet{170, 16, 1260, 866};
// match over
constexpr Rectangle MatchCard{450, 402, 700, 300};
// game list: four cards on the first row, three on the second
constexpr float SelCardW = 236.f, SelCardH = 268.f, SelGap = 16.f;
constexpr float SelRowY[2] = {178.f, 476.f};
constexpr Rectangle SelBack{690, 790, 220, 58};
constexpr Rectangle MatchNew{530, 736, 250, 64};
constexpr Rectangle MatchMenu{820, 736, 250, 64};
constexpr Rectangle MatchAnalysis{1100, 744, 190, 50};
} // namespace L

enum ClickId {
    C_None = 0,
    C_Play, C_WatchAi, C_TitleRules, C_TitleSettings, C_Quit,
    C_Back, C_Defaults, C_Hands, C_Level, C_Anim, C_Sfx, C_Ambient, C_Music, C_Hints, C_Katlamali, C_YandanCeza, C_Name,
    C_Resume, C_PauseAi, C_PauseRules, C_PauseSettings, C_PauseMenu, C_ConfirmYes, C_ConfirmNo,
    C_Next, C_NewGame, C_MatchMenu, C_RulesTab, C_GameCard, C_SelBack, C_OkeyStart, C_TavlaPoints,
    C_BatakEsli, C_BatakTarget, C_PistiTarget, C_PistiMode, C_TitleStats, C_Continue, C_StatsBack, C_GuideOk, C_Guide, C_TavlaDoubling, C_TavlaKatmerli, C_TavlaRakip,
    C_OkeyRenkli, C_BatakKoz, C_King12, C_SetPage, C_DayTime, C_Season, C_Voices, C_ColorBlind, C_BigText, C_StatsReplays, C_ReplayWatch,
    C_ReplaysBack, C_ReplayAnalyze, C_ShowAnalysis, C_AnalysisBack,
    C_StatsAchievements, C_AchievementsBack, C_AchievementsTab, // Başarımlar
    C_Venue,  // Mekân: içerisi / bahçe / otomatik
    C_OzelGun, // Özel günler: açık / kapalı (ozelgun)
    C_TavlaCesit, // Tavla çeşidi: klasik / Gülbahar / Fevga
    C_DamaRakip, C_DamaWins, // Dama: who sits across, games to win
    C_BezikTarget, // Bezik: the match's points
    C_KonkenOpen, C_KonkenLimit, // Konken: the opening's least value, the limit
    // 101 kuralları (Ayarlar page 3)
    C_Y101Page, C_Y101Acma, C_Y101Kat, C_Y101Acmayan, C_Y101OkeyCeza, C_Y101IslekCeza, C_Y101GeriVer, C_Y101Bekle,
    C_KonkenEnd,                 // Konken bitiş: İlk yanan / Son kalan
    // Sen: the player's own hands (Ayarlar "Sen" page)
    C_SenHands, C_SenKol, C_SenRenk, C_SenTen, C_SenYuzuk, C_SenSaat, C_SenTespih, C_SenBardak, C_SenSigara,
};

constexpr int kScreenCount = 16; // > the last ScreenId (room for a few more)
static_assert((int)ScreenId::Achievements < kScreenCount, "kScreenCount");

// ---------------------------------------------------------------- rich text (rules)
// Markup: *bold*  ^accent (red, bold)^  \n = line break.
struct Piece {
    std::string text;
    FontId font = FontId::Ui;
    Color color = kInk;
    Vector2 pos{0, 0};
};
struct RichText {
    std::vector<Piece> pieces;
    float height = 0.f;
};

// ---------------------------------------------------------------- rules content
struct RuleItem {
    std::vector<MiniTile> tiles;
    std::string caption;
    bool wrong = false;
    Rectangle box{}; // layout, relative to the block
};
struct RuleBlock {
    enum Kind { Heading, Para, Bullet, Tiles } kind = Para;
    std::string text;
    std::string label; // bullets: "" = dot, otherwise e.g. "1."
    std::vector<RuleItem> items;
    float y = 0.f, h = 0.f;
    RichText rich;
};

inline MiniTile T(int c, int n) { return MiniTile{c, n, 0, false}; }
inline MiniTile TM(int c, int n) { return MiniTile{c, n, 0, true}; }
inline MiniTile TOkey() { return MiniTile{1, 7, 1, false}; } // the okey of the examples (gösterge Mavi 6)
inline MiniTile TFake() { return MiniTile{1, 7, 2, false}; }

// A section tab on the rules page: its label and the heading it jumps to.
struct RuleTab {
    std::string label, heading;
};

// (ScreensRules.cpp)
RichText layoutRich(const std::string& src, float width, float size, float lineH);
std::vector<RuleBlock> buildRules(GameKind g);
std::vector<RuleTab> ruleTabsFor(GameKind g);

} // namespace screens_detail
using namespace screens_detail;

// ============================================================================ Screens::Impl

// The settings' switches, in the order of their animation slots (Impl::toggleAnim, senAnim, y101Anim): show() snaps
// the switches to these, updateSettings() eases them towards these.
constexpr bool Settings::*const kToggleFields[16] = {
    &Settings::sfx,           &Settings::ambient,       &Settings::music,      &Settings::hints,
    &Settings::katlamali,     &Settings::yandanCeza,    &Settings::batakEsli,  &Settings::guide,
    &Settings::tavlaDoubling, &Settings::tavlaKatmerli, &Settings::okeyRenkli, &Settings::batakKozKirilmadan,
    &Settings::king12,        &Settings::voices,        &Settings::colorBlind, &Settings::bigText};
constexpr bool Settings::*const kSenFields[2] = {&Settings::hands, &Settings::sigara}; // (Sen)
constexpr bool Settings::*const kY101Fields[4] = {&Settings::y101OkeyCeza, &Settings::y101IslekCeza, // (101 kuralları)
                                                  &Settings::y101GeriVer, &Settings::y101Bekle};

struct Screens::Impl {
    Screens* owner = nullptr;
    Settings settings;

    ScreenId cur = ScreenId::Title;
    ScreenId prev = ScreenId::Title;
    float fade = 1.f;                     // progress of the current cross-fade (1 = done)
    float shownAt[kScreenCount] = {};     // time each screen was last shown (entry animations)
    ScreenId backSettings = ScreenId::Title;
    ScreenId backRules = ScreenId::Title;
    float time = 0.f;
    Vector2 mouse = kNoMouse;

    struct Click {
        ScreenId screen;
        int id;
        int value;
    };
    std::vector<Click> clicks;
    int pressedId = -1; // custom (non-drawButton) widgets: id that received the mouse press
    bool settingsDirty = false; // a setting changed outside update() (reported by the next update)

    // settings
    bool nameEditing = false;
    std::string nameBuf;
    std::string nameBefore; // name when editing started (ESC restores it)
    // sfx, ambient, music, hints, katlamalı, yandan ceza, eşli batak, rehber, katlama zarı, katmerli mars, renkli okey,
    // batak koz, king 12, konuşma, renk körü, büyük yazı
    float toggleAnim[16] = {1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0};
    int settingsPage = 0; // 0 Oyun, 1 Görünüm · Ses, 2 Sen
    float senAnim[2] = {1, 0}; // Sen page toggles: Ellerimi göster, Sigara
    float y101Anim[4] = {1, 1, 0, 1}; // 101 kuralları toggles: okey cezası, işlek cezası, geri verme cezası, bekle

    // rules
    std::vector<RuleBlock> rules;
    bool rulesLaidOut = false;
    float rulesContentH = 0.f;
    float scroll = 0.f, scrollTarget = 0.f;
    int rulesGame = -1;               // the game the page was built for
    std::vector<RuleTab> tabs;
    std::vector<float> tabY;          // content y of each tab's heading
    bool draggingThumb = false;
    float dragOffset = 0.f;

    // pause
    bool confirmQuit = false;
    float confirmAt = 0.f;

    // Yapay Zeka mode (an AI plays the human's seat) and the self-pressing between-hands buttons
    bool aiMode = false;
    const StatsBook* stats = nullptr;   // App's record (İstatistik screen)
    bool canResume = false;             // "Devam Et" on the title
    std::string resumeLabel;
    std::vector<ReplayEntry> replays;
    bool analysisAvailable = false, analysisReady = false;
    std::string analysisTitle;
    std::vector<MistakeView> analysis;
    ScreenId analysisBack = ScreenId::MatchOver;
    int chosen = -1;
    std::string guideTitle;
    std::vector<std::string> guideLines;
    std::vector<float> guideLineH; // drawGuide's per-line heights
    float autoLeft = -1.f;
    // Başarımlar: App's badges, the page shown, the banners waiting (badge indices) and the one up since bannerAt
    const Achievements* achievements = nullptr;
    int achTab = 0;
    std::vector<int> bannerQueue;
    int bannerIdx = -1;
    float bannerAt = 0.f;

    // match over
    std::vector<Confetti> confetti;
    uint32_t rngState = 0x2545F491u;

    // What the pause menu, the score sheet and the final standings show, copied when they appear. App acts on
    // their buttons at once (deals the next hand, restarts the match, or leaves for the title and passes a
    // null game) while the screen is still fading out, so the fading layer is drawn from this copy.
    std::optional<okey::Game> frozen;
    bool freezePending = false;
    std::optional<SheetModel> sheet; // the other games' sheet (set by App), drawn instead of the okey game's

    FadeShader fadeShader;

    // ------------------------------------------------------------ general
    float ageOf(ScreenId id) const { return time - shownAt[(int)id]; }
    float rnd();
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    void sfx(Sfx s);
    void click(int id, int value = 0) { clicks.push_back({cur, id, value}); }

    // Press + release over the same custom widget (mirrors drawButton's behaviour).
    bool hit(Rectangle r, int id, Vector2 m);

    void show(ScreenId id);

    void goBack();

    static bool drawsGame(ScreenId id);
    void freeze(const okey::Game* g);
    // The game a layer is drawn from: the outgoing screen uses its copy, the game may have moved on.
    const okey::Game* gameFor(ScreenId id, const okey::Game* g) const;

    // Seats sharing the lowest match total. Game::leaderSeat() alone settles a tie by seat order, which would
    // crown one of them silently; the sheet and the final standings show a shared first place instead.
    // Klasik okey: the highest total leads; eşli 101: both seats of the team with the lower combined total.
    static int standing(const okey::Game& g, int s);
    static std::vector<int> coLeaders(const okey::Game& g);
    // "Sen ve Kel Mahmut" for a team (eşli 101)
    std::string teamName(const okey::Game& g, int s) const;
    // A seat's name on the sheet and the final standings: the human's seat says who played it in the Yapay Zeka
    // mode ("Yapay Zeka (Sen)").
    std::string seatName(const okey::Game& g, int s) const;
    // "A", "A ve B", "A, B ve C" (the human's seat left out when `skipHuman`)
    std::string joinNames(const okey::Game& g, const std::vector<int>& seats, bool skipHuman) const;
    static bool humanAmong(const okey::Game& g, const std::vector<int>& seats);

    // A button label with the self-press countdown ("Sonraki El (4)"), shrunk to fit the button.
    std::string autoLabel(const std::string& base, FontId f, float maxW, float& size) const;

    static bool isLastHand(const okey::Game* g);

    // ------------------------------------------------------------ settings logic
    void startEditing();
    // Enter / click outside. An empty name falls back to the name before editing. True if settings changed.
    bool commitName();
    // ESC: undo the live edits.
    bool cancelName();
    bool applyLiveName();
    int selectedHands() const;
    int selectedAnim() const;

    // ------------------------------------------------------------ click handling (update)
    ScreenAction handleClick(const Click& c, const okey::Game* g);

    // ------------------------------------------------------------ per-frame logic
    ScreenAction update(float dt, Vector2 m, const okey::Game* g);

    ScreenAction updateSettings(float dt, Vector2 m, bool clicked);

    // ------------------------------------------------------------ rules layout & scrolling
    void ensureRulesLayout();
    float maxScroll() const { return std::max(0.f, rulesContentH - L::RulesView.height); }
    Rectangle thumbRect() const;

    void updateRules(float dt, Vector2 m);

    // ------------------------------------------------------------ confetti
    void spawnConfetti();
    void updateConfetti(float dt);
    void drawConfetti() const;

    // ============================================================ drawing
    void draw(const okey::Game* g);

    void drawLayer(ScreenId id, float alpha, Vector2 m, const okey::Game* g);

    void drawScreen(ScreenId id, Vector2 m, const okey::Game* g);

    // Big title in the Sign font with a soft shadow.
    static void drawHeader(const std::string& s, Vector2 c, float size, Color col);
    static void brassRule(float x0, float x1, float y);

    // ------------------------------------------------------------ title
    // The buttons under "Oyna": "Devam Et" first when a match was left unfinished, then the AI, rules and record,
    // settings and quit (two rows share a line when "Devam Et" needs the room).
    struct TitleButton {
        Rectangle r;
        const char* label;
        int id;
        float fs;
    };
    // (built once per layout: the title draws them every frame)
    const std::vector<TitleButton>& titleButtons() const;
    mutable std::vector<TitleButton> titleButtonsCache;
    mutable int titleButtonsKey = -1;

    void drawTitle(Vector2 m);

    void drawSign(float age);

    // Axis-aligned clip of the upper half of a rotated text box (the rotation is tiny; good enough for a
    // highlight band).
    static void beginClipLocal(Vector2 pos, Vector2 ms, float angDeg, Vector2 pivot);

    // ------------------------------------------------------------ game list
    Rectangle gameCardRect(int k) const;

    // The little picture on top of a game's card.
    void drawGameEmblem(GameKind k, Vector2 c);

    void drawGameSelect(Vector2 m);

    // ------------------------------------------------------------ a game's guide card
    void drawGuide(Vector2 m);

    // ------------------------------------------------------------ stats (kahvehane defteri)
    void drawStats(Vector2 m);

    // ------------------------------------------------------------ hatalarım: the analysis of a match
    void drawAnalysis(Vector2 m);

    // ------------------------------------------------------------ tekrarlar: the last finished matches
    void drawReplays(Vector2 m);

    // ------------------------------------------------------------ başarımlar: the badges
    // A badge's picture inside its medallion: centre c, about `s` across, alpha a.
    void drawBadgeIcon(BadgeIcon icon, Vector2 c, float s, float a) const;

    // The round medallion: open = brass ring, a deep red ground and the picture; locked = dark, a "?".
    void drawBadge(int i, Vector2 c, float r, bool open, float a) const;

    void drawAchievements(Vector2 m);

    // "Başarım: Pişti Üstüne Pişti!" at the top, sliding in and out; the next one waits its turn.
    static constexpr float kBannerShow = 4.2f;
    // (update(): the queue moves and the chime rings on the game's clock, not on a draw)
    void advanceAchievementBanner();
    void drawAchievementBanner();

    // ------------------------------------------------------------ settings
    void settingRow(float cy, const char* label, const std::string& hint);

    static void drawSelectedChip(Rectangle r, const std::string& label, float fs);

    void chipRow(int id, const std::vector<std::string>& labels, int selected, float cy, float w, float gap,
                 Vector2 m, float fs);

    void toggle(int id, int animIdx, bool on, float cy, Vector2 m);

    int selectedOkeyStart() const;

    void drawSettings(Vector2 m);

    // ---- Sen: the player's own hands at the table (r3d::PlayerHands): the sleeves, the skin, a ring and a watch, the
    // tespih, the own tea glass, a cigarette.
    // ---- 101 kuralları (Ayarlar page 3): the variants of 101 and eşli 101 (docs/kurallar_101.md)
    std::string summary101Settings() const;
    mutable std::string summary101Key = "\x01", summary101Val; // (a key no summary equals)
    template <class Section, class Row> void drawSettings101(Vector2 m, Section& section, Row& row);

    void senToggle(int id, float a, bool on, float cy, Vector2 m);
    // a thin colour bar along the bottom of chip i of a chipRow (w, gap as given to it)
    static void chipSwatch(int i, float cy, float w, float gap, Color c);
    template <class Section, class Row>
    void drawSettingsSen(Vector2 m, Section& section, Row& row);

    // The second page of the settings: the time of day and season of the room, colour-blind / big text, sounds.
    template <class Section, class Row>
    void drawSettingsLooks(Vector2 m, Section& section, Row& row);

    // ------------------------------------------------------------ rules
    void drawRules(Vector2 m);

    int activeRuleTab() const;

    void drawRuleTabs(Vector2 m);

    static void drawRich(const RichText& rt, float x, float y, float size);

    void drawRuleBlock(const RuleBlock& b, float x, float y) const;

    // ------------------------------------------------------------ pause
    void drawPaused(Vector2 m, const okey::Game* g);

    // ------------------------------------------------------------ hand summary (hesap kağıdı)
    static void drawGridPaper(Rectangle S);

    struct Cell {
        std::string text;
        Color color = kGraphite;
        float size = 30.f;
    };

    void drawHandSummary(Vector2 m, const okey::Game* g);

    // ------------------------------------------------------------ the other games' sheet
    void drawSheetSummary(Vector2 m, const SheetModel& sm);

    void drawSheetMatchOver(Vector2 m, const SheetModel& sm);

    // ------------------------------------------------------------ match over
    void drawMatchOver(Vector2 m, const okey::Game* g);
};

} // namespace ui
