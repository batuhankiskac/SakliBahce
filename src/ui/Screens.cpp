// Screens: title menu, settings, rules ("Nasıl Oynanır?"), pause menu, the end-of-hand score sheet
// ("hesap kağıdı") and the match-over celebration.
//
// Immediate mode: draw() lays out and draws the widgets and records clicks; the next update() turns the
// recorded clicks (plus keyboard / wheel input) into ScreenActions. Screen changes cross-fade: both
// screens are drawn through a tiny alpha shader, so the shared Common.h widgets fade like everything else.
#include "ui/Screens.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui {

namespace {

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

Color alphaMul(Color c, float a) {
    c.a = (unsigned char)std::lround((float)c.a * clamp01(a));
    return c;
}
Color rgba(int r, int g, int b, float a) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)std::lround(255.f * clamp01(a))};
}

uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hash01(uint32_t x) { return (float)(hashU(x) & 0xFFFFFFu) / 16777216.f; }

Vector2 vadd(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 vlerp(Vector2 a, Vector2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
Vector2 rotateDeg(Vector2 p, float deg) {
    const float r = deg * kPi / 180.f, c = std::cos(r), s = std::sin(r);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

// Scores use an en dash as the minus sign (the typographic minus is not in the font atlas).
std::string scoreText(int v) { return v < 0 ? std::string("\xE2\x80\x93") + std::to_string(-v) : std::to_string(v); }

// ---------------------------------------------------------------- UTF-8 (player name input)
int utf8Count(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}
void utf8PopBack(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}
void utf8Append(std::string& s, int cp) {
    int n = 0;
    const char* bytes = CodepointToUTF8(cp, &n);
    s.append(bytes, (size_t)n);
}
// Codepoints the UI fonts can draw and that make sense in a name.
bool nameCodepointOk(int cp) {
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
std::string trimmed(const std::string& s) {
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
        if (ok && loc < 0) unload();
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
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

void drawStar(Vector2 c, float r, Color col, float rotDeg = -90.f) {
    Vector2 pts[10];
    for (int k = 0; k < 10; ++k) {
        const float a = (rotDeg + 36.f * (float)k) * kPi / 180.f;
        const float rad = (k % 2 == 0) ? r : r * 0.45f;
        pts[k] = {c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
    }
    for (int k = 0; k < 10; ++k) tri(c, pts[k], pts[(k + 1) % 10], col);
}

void drawClover(Vector2 c, float s, Color col) {
    DrawLineEx(c, {c.x + s * 0.28f, c.y + s * 0.95f}, s * 0.16f, col);
    for (int i = 0; i < 4; ++i) {
        const float a = (45.f + 90.f * (float)i) * kPi / 180.f;
        DrawCircleV({c.x + std::cos(a) * s * 0.40f, c.y + std::sin(a) * s * 0.40f}, s * 0.34f, col);
    }
    DrawCircleV(c, s * 0.22f, col);
}

// A hand-drawn line: a few segments with a slight perpendicular wobble.
void pencilLine(Vector2 a, Vector2 b, float thick, Color col, uint32_t seed, float wobble = 1.1f) {
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
void pencilEllipse(Vector2 c, float rx, float ry, float progress, float thick, Color col, uint32_t seed) {
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
void drawNeonText(const std::string& s, Vector2 center, float size, float spacing, float on) {
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

float neonFlicker(float t) {
    float f = 0.94f + 0.06f * std::sin(t * 17.f) * std::sin(t * 5.3f);
    const float c = std::fmod(t, 7.3f);
    if (c > 6.35f && c < 6.8f && std::sin(t * 61.f) > 0.15f) f *= 0.3f;
    return f;
}

// A chain hanging from a ceiling hook to an eyelet.
void drawChain(Vector2 a, Vector2 b) {
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

void drawRivet(Vector2 p, float r) {
    DrawCircleV({p.x + 1, p.y + 1.5f}, r, rgba(0, 0, 0, 0.45f));
    DrawCircleV(p, r, Color{130, 96, 44, 255});
    DrawCircleV({p.x - r * 0.2f, p.y - r * 0.2f}, r * 0.72f, pal::Brass);
    DrawCircleV({p.x - r * 0.4f, p.y - r * 0.4f}, r * 0.28f, rgba(255, 250, 230, 0.7f));
}

// ---------------------------------------------------------------- handwriting (score sheet)
// Noteworthy draws "1" as a bare stroke that reads like "l"; add the little flag people write.
void handText(const std::string& s, Vector2 pos, float size, Color c) {
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
Vector2 handMeasure(const std::string& s, float size) {
    Vector2 m = measureText(FontId::Hand, s, size);
    for (char ch : s)
        if (ch == '1') m.x += size * 0.09f;
    return m;
}
void handTextCentered(const std::string& s, Vector2 center, float size, Color c) {
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

void drawMiniTile(Rectangle r, const MiniTile& t) {
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

RichText layoutRich(const std::string& src, float width, float size, float lineH) {
    struct Tok {
        std::string s;
        bool bold = false, accent = false, space = false, newline = false;
    };
    std::vector<Tok> toks;
    bool bold = false, accent = false, pendingSpace = false;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        toks.push_back({cur, bold, accent, pendingSpace, false});
        cur.clear();
        pendingSpace = false;
    };
    for (char ch : src) {
        if (ch == '*') {
            flush();
            bold = !bold;
        } else if (ch == '^') {
            flush();
            accent = !accent;
        } else if (ch == ' ') {
            flush();
            pendingSpace = true;
        } else if (ch == '\n') {
            flush();
            toks.push_back({"", bold, accent, false, true});
            pendingSpace = false;
        } else {
            cur += ch;
        }
    }
    flush();

    RichText out;
    const float spaceW = measureText(FontId::Ui, " ", size).x;
    float x = 0.f, y = 0.f;
    bool lineEmpty = true;
    size_t i = 0;
    while (i < toks.size()) {
        if (toks[i].newline) {
            x = 0.f;
            y += lineH;
            lineEmpty = true;
            ++i;
            continue;
        }
        // a word = this token plus following tokens glued to it (no space before them)
        size_t j = i + 1;
        while (j < toks.size() && !toks[j].newline && !toks[j].space) ++j;
        float ww = 0.f;
        for (size_t k = i; k < j; ++k) {
            const FontId f = (toks[k].bold || toks[k].accent) ? FontId::UiBold : FontId::Ui;
            ww += measureText(f, toks[k].s, size).x;
        }
        if (!lineEmpty && x + spaceW + ww > width) {
            x = 0.f;
            y += lineH;
            lineEmpty = true;
        }
        if (!lineEmpty) x += spaceW;
        for (size_t k = i; k < j; ++k) {
            Piece p;
            p.text = toks[k].s;
            p.font = (toks[k].bold || toks[k].accent) ? FontId::UiBold : FontId::Ui;
            p.color = toks[k].accent ? kRedPencil : kInk;
            p.pos = {x, y};
            x += measureText(p.font, p.text, size).x;
            out.pieces.push_back(std::move(p));
        }
        lineEmpty = false;
        i = j;
    }
    out.height = y + lineH;
    return out;
}

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

MiniTile T(int c, int n) { return MiniTile{c, n, 0, false}; }
MiniTile TM(int c, int n) { return MiniTile{c, n, 0, true}; }
MiniTile TOkey() { return MiniTile{1, 7, 1, false}; } // the okey of the examples (gösterge Mavi 6)
MiniTile TFake() { return MiniTile{1, 7, 2, false}; }

// A section tab on the rules page: its label and the heading it jumps to.
struct RuleTab {
    std::string label, heading;
};

std::vector<RuleBlock> buildRules101(bool esli) {
    std::vector<RuleBlock> v;
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto P = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Para; b.text = s; v.push_back(b); };
    auto B = [&](const char* s, const char* label = "") {
        RuleBlock b;
        b.kind = RuleBlock::Bullet;
        b.text = s;
        b.label = label;
        v.push_back(b);
    };
    auto X = [&](std::vector<RuleItem> items) {
        RuleBlock b;
        b.kind = RuleBlock::Tiles;
        b.items = std::move(items);
        v.push_back(b);
    };
    enum { Y = 0, M = 1, K = 2, R = 3 };

    H("Oyunun amacı");
    P("101, dört kişiyle oynanan bir okey oyunudur. Taşlarını seri, grup ya da çift olarak masaya açar, "
      "sonra kalanları işleyerek elini bitirmeye çalışırsın. Her elin puanı hesap kağıdına yazılır ve "
      "*düşük puan iyidir*: maçın sonunda toplamı en düşük olan kazanır.");
    if (esli) {
        H("Eşli oyun");
        P("Eşli 101'de karşında oturan oyuncu *ortağındır*: sen ve Kel Mahmut bir takım, Hacı Rıza ile Emekli Nuri "
          "öbür takım. Kurallar tekli 101'le aynıdır, şu farklarla:");
        B("Ortaklardan biri elini bitirince *ötekinin eli silinir*: o el elinde kalan taşlar sayılmaz, açmamış olsa "
          "bile 202 yazılmaz. Yazılmış cezaları (işlek taş, okey atmak) yine de sayılır.");
        B("Takımın puanı iki ortağın toplamıdır; maçın sonunda *toplamı düşük olan takım* kazanır.");
        B("Attığın taşı her zaman bir rakip alır: sağındaki de solundaki de karşı takımdandır. Ortağının perlerine "
          "de herkesinki gibi taş işleyebilirsin.");
        B("*Yandan açma cezası* karşı takıma yazılır: senin attığın taşla açılırsa ceza sana gelir.");
    }

    H("Taşlar");
    P("Toplam 106 taş vardır: Sarı, Mavi, Siyah ve Kırmızı renklerde 1'den 13'e kadar sayılar, her taştan "
      "iki tane. Bunlara iki *sahte okey* eklenir.");
    X({{{T(Y, 3), T(M, 7), T(K, 11), T(R, 13)}, "Dört renk"}, {{TFake()}, "Sahte okey"}});

    H("Gösterge ve okey");
    P("Taşlar karıldıktan sonra bir taş açılır: bu *gösterge*dir ve oyundan çıkar. Göstergenin aynı renkte "
      "bir üst sayısı o elin *okey*idir (13'ün üstü 1'dir). İki okey taşı da joker gibidir, istediğin taşın "
      "yerine geçer. *Sahte okey* joker değildir; okeyin rengi ve sayısı yerine oynanır.");
    X({{{T(M, 6)}, "Gösterge: Mavi 6"}, {{TOkey()}, "Okey: Mavi 7 (joker)"}, {{TFake()}, "Sahte okey = Mavi 7"}});

    H("Dağıtım");
    P("Eli başlatan oyuncuya 22, diğerlerine 21 taş dağıtılır; kalan 20 taş ortada kapalı durur. Başlayan "
      "oyuncu ilk turunda taş çekmez, doğrudan oynar ve bir taş atar. İlk eli kimin başlatacağını kura "
      "belirler; sonraki her elde bu görev bir sağdaki oyuncuya geçer.");

    H("Sıra ve tur");
    P("Sıra sağa doğru döner: sen, sağındaki, karşındaki, solundaki. Sıra sana geldiğinde:");
    B("*Taş al:* ortadaki desteden bir taş çek ya da solundaki oyuncunun en son attığı taşı al.", "1.");
    B("*Oyna (isteğe bağlı):* elini aç, yeni per indir, masadaki perlere taş işle, okey al.", "2.");
    B("*Taş at:* bir taşı kendi atık yerine at. Bu taşı sağındaki oyuncu alabilir.", "3.");

    H("Yandan taş almak");
    P("Solundan aldığın taşı aynı turda masada kullanmak zorundasın: henüz açmadıysan açılışında, açtıysan "
      "yeni bir perde, işlerken ya da okey alırken. Kullanamazsan *Geri Ver*: taş cezasız yerine döner ve "
      "desteden çekersin. Aynı turda bir daha yandan alamazsın.");
    P("*Yandan açma cezası:* yandan aldığın taşla elini açarsan, o taşı atan oyuncuya taşın sayısının "
      "seri açılışta ^10 katı^, çift açılışta ^20 katı^ ceza yazılır (yandan 7 alıp seriyle açtın: atana 70). "
      "Kahveden kahveye değişen bir kural olduğu için ayarlardan kapatılabilir.");

    H("Perler: seri, grup, çift");
    B("*Seri:* aynı renkten en az 3 ardışık sayı. Seri 13'te biter: normal okeyin aksine 101'de 1, 13'ün "
      "arkasından gelmez (12-13-1 per değildir) ve başa dönülmez (13-1-2 olmaz). 1 yalnızca serinin başında "
      "olur: 1-2-3.");
    {
        std::vector<RuleItem> it = {{{T(R, 5), T(R, 6), T(R, 7)}, "Kırmızı 5-6-7"},
                                    {{T(M, 12), T(M, 13), T(M, 1)}, "12-13-1 olmaz"},
                                    {{T(K, 13), T(K, 1), T(K, 2)}, "13-1-2 olmaz"}};
        it[1].wrong = true;
        it[2].wrong = true;
        X(it);
    }
    B("*Grup:* aynı sayının farklı renkleri, 3 ya da 4 taş. Bir renk iki kez kullanılamaz.");
    X({{{T(Y, 9), T(M, 9), T(K, 9)}, "Üçlü grup"}, {{T(Y, 9), T(M, 9), T(K, 9), T(R, 9)}, "Dörtlü grup"}});
    B("*Çift:* birebir aynı iki taş (aynı renk, aynı sayı).");
    X({{{T(K, 4), T(K, 4)}, "Siyah 4 çifti"}, {{T(Y, 11), T(Y, 11)}, "Sarı 11 çifti"}});
    B("*Okey* eksik taşın yerine geçer ve yerine geçtiği sayı kadar değerlidir.");
    X({{{T(R, 5), TOkey(), T(R, 7)}, "Kırmızı 5, okey, 7 = 18 puan"}});

    H("Elini açmak");
    P("Masaya ilk kez per indirmeye *el açmak* denir. İki yolu vardır:");
    B("*Seriyle açmak:* indirdiğin seri ve grupların toplamı en az ^101^ olmalı. Toplam, taşların üzerindeki "
      "sayılarla hesaplanır; okey, yerine geçtiği sayı kadar sayılır.");
    B("*Çiftle açmak:* en az ^5 çift^ indirerek.");
    X({{{T(R, 10), T(R, 11), T(R, 12), T(R, 13)}, "46"},
       {{T(Y, 12), T(M, 12), T(K, 12)}, "36"},
       {{T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}, "26"}});
    P("46 + 36 + 26 = 108, yani 101'i geçiyor: bu açılış geçerli. Açılışta seri ile çift karıştırılamaz. "
      "Açarken istediğin kadar per indirebilirsin, ama açtığın turda işleyemez, yeni per indiremez, okey "
      "alamazsın; bunlar için bir sonraki turunu beklersin. Taş atabilmek için elinde her zaman en az bir "
      "taş kalmalı.");
    P("Çiftle açan sonradan yeni seri indiremez, yalnızca çift indirebilir. Seriyle açan sonradan seri ve "
      "grup indirir; masada çiftle açmış başka biri varsa elindeki çiftleri de indirebilir.");

    H("Katlamalı oyun");
    P("Ayarlar'dan açılan bir oyun türüdür. Katlamalı oyunda senden önce biri seriyle açtıysa, sen onun "
      "toplamından *en az 1 fazlasıyla* açmak zorundasın: önceki açılış 116 ise sana en az ^117^ gerekir, "
      "sen 128'le açarsan sonrakine 129 gerekir. Çiftte de aynısı: önce biri 5 çiftle açtıysa sana en az "
      "^6 çift^ gerekir. Seri ve çift ayrı sayılır. Masadaki sayaç o anki gereken sayıyı gösterir.");

    H("İşlemek ve okey almak");
    P("Elini açtıktan sonraki turlarında masadaki seri ve gruplara, kimin olursa olsun, taş ekleyebilirsin: "
      "serinin başına ya da sonuna, gruba da eksik renk olarak (en fazla 4 taş). Buna *işlemek* denir. "
      "Çiftlere taş işlenmez.");
    X({{{T(R, 5), T(R, 6), T(R, 7), TM(R, 8)}, "Kırmızı 8 işlenir"},
       {{T(Y, 9), T(M, 9), T(K, 9), TM(R, 9)}, "Kırmızı 9 işlenir"}});
    P("Masadaki bir seri ya da grupta okey varsa, onun yerine geçtiği gerçek taşı koyup okeyi eline "
      "alabilirsin (*okey almak*). Üç taşlı bir grupta eksik renklerin herhangi biri okeyin yerine konabilir.");
    X({{{T(R, 5), TOkey(), T(R, 7)}, "Önce"}, {{T(R, 5), TM(R, 6), T(R, 7)}, "Kırmızı 6'yı koy, okeyi al"}});

    H("Cezalar");
    P("Aşağıdakilerin her biri için o elin puanına ^101 ceza^ eklenir:");
    B("Okey atmak (elini bitirdiğin son taş hariç).");
    B("*İşlek taş* atmak: masadaki bir seri ya da gruba işlenebilecek bir taşı atmak (son taş hariç). "
      "Okey attığında yalnızca okey cezası yazılır.");
    P("*Yandan açma cezası* (ayarlardan kapatılabilir): attığın taşı sağındaki oyuncu alıp onunla elini açarsa "
      "taşın sayısının ^10 katı^ (çiftle açtıysa ^20 katı^) sana yazılır.");

    H("El sonu");
    P("Elindeki son taşı atan oyuncu eli *bitirir*. Ortadaki taşlar tükenirse, son taş çekilip atıldığında el "
      "kimse bitirmeden sona erer.");

    H("Puanlama");
    P("Her elin sonunda herkesin puanı hesap kağıdına yazılır. Düşük puan iyidir.");
    B("Eli bitiren: *\xE2\x80\x93" "101*");
    B("Hiç açmayan: *202*");
    B("Seriyle açan: elinde kalan taşların toplamı");
    B("Çiftle açan: elinde kalanın *iki katı*");
    B("Açmış bir oyuncunun elinde okey kaldıysa her okey için ^101 ceza^");
    B("Bunlara o elin cezaları eklenir.");
    P("*Katlar:* Bitiren oyuncu son taş olarak okeyi attıysa (*okeyle bitiş*), çiftle açmışsa (*çiftten "
      "bitiş*) ya da henüz kimse açmamışken bütün elini tek seferde açıp bitirdiyse (*elden bitiş*) diğer "
      "oyuncuların puanları ikiye katlanır, bitirenin puanı da (\xE2\x80\x93" "202); cezalar katlanmaz. Katlar "
      "birbiriyle çarpılır: okeyle ve elden birlikte \xC3\x97" "4 olur. Taşlar tükenerek biten elde kat yoktur.");

    H("Maçın sonu");
    P("Maç, ayarlardan seçtiğin el sayısı kadar (1 ile 11 arası) sürer. Son elden sonra toplam puanı en düşük "
      "olan maçı kazanır. En düşük toplam birden fazla oyuncudaysa birincilik paylaşılır.");

    H("Kontroller");
    B("*İpucu:* takılırsan *İpucu* düğmesi (ya da *H*) Kurt'un senin yerinde ne yapacağını gösterir.");
    B("*Etrafa bakmak:* farenin sağ tuşunu basılı tutup sürükle. *Fare tekerleği* yakınlaştırır, *R* ya da sağ "
      "tuşa çift tıklamak bakışını yeniden masaya ortalar.");
    B("*Istaka:* taşları sürükleyerek 2 sıra \xC3\x97 16 yuvaya dilediğin gibi diz. Yan yana duran taşlar "
      "bir grup sayılır; araya boşluk bırakınca gruplar ayrılır.");
    B("*Taş çekmek:* ortadaki desteye ya da sol alttaki atık taşına tıkla veya onu ıstakana sürükle.");
    B("*Taş atmak:* taşı sağ alttaki atık yerine sürükle ya da taşa çift tıkla.");
    B("*El Aç / Per Aç:* ıstakadaki grupları masaya indirir. İpuçları açıkken geçerli gruplar parlar ve bir "
      "sayaç gösterilir: \xE2\x80\x9CSeri 87/101 \xC2\xB7 Çift 3/5\xE2\x80\x9D.");
    B("*İşlemek:* taşı masadaki bir perin üzerine sürükle; sol yarısına bırakırsan başa, sağ yarısına "
      "bırakırsan sona eklenir. Perdeki okeyin üzerine bırakırsan okeyi almayı dener.");
    B("*Seri Diz / Çift Diz:* ıstakanı seri ya da çift düzenine göre kendiliğinden dizer. Kısayollar: *S* "
      "Seri Diz, *C* Çift Diz, *Enter* El Aç / Per Aç.");
    B("Masanın uzak ucundaki bir perin ya da bir atık yığınının üzerinde fareyle biraz beklersen büyütülmüş "
      "hâli açılır.");
    B("*Geri Ver:* yandan aldığın taşı cezasız geri verir; sonra desteden çekersin.");
    B("İşlek taş ya da okey atarken oyun seni uyarır ve onay ister.");
    B("Okey taşlarının köşesinde küçük bir yıldız bulunur.");
    B("İpuçları açıkken *işlek* taşların köşesinde yeşil bir *+* görünür: bunları atarsan ^101 ceza^ yersin.");
    B("*Yapay Zeka:* *Y* tuşu ya da *Yapay Zeka* düğmesi taşlarını yapay zekaya bırakır; o senin yerine "
      "oynar, sen izlersin. Yeniden basınca kontrol, turun kaldığı yerden sana döner.");
    B("*ESC* ya da *Menü* düğmesi oyunu duraklatır; menülerde bir önceki ekrana döner.");
    return v;
}

std::vector<RuleBlock> buildRulesOkey() {
    std::vector<RuleBlock> v;
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto P = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Para; b.text = s; v.push_back(b); };
    auto B = [&](const char* s, const char* label = "") {
        RuleBlock b;
        b.kind = RuleBlock::Bullet;
        b.text = s;
        b.label = label;
        v.push_back(b);
    };
    auto X = [&](std::vector<RuleItem> items) {
        RuleBlock b;
        b.kind = RuleBlock::Tiles;
        b.items = std::move(items);
        v.push_back(b);
    };
    enum { Y = 0, M = 1, K = 2, R = 3 };

    H("Oyunun amacı");
    P("Okey, kahvenin en çok oynanan oyunudur. Elindeki 14 taşı *per*lere (seri ya da grup) ya da *yedi çifte* "
      "diz; sıran gelince bir taş çek, fazla taşı atıp elini göster: *bittin*. 101'deki gibi masaya per açılmaz, "
      "el bitene kadar taşlar ıstakanda kalır.");

    H("Taşlar ve okey");
    P("106 taşla oynanır: dört renkte 1'den 13'e kadar her taştan iki tane ve iki *sahte okey*. Dağıtımdan "
      "sonra açılan taş *gösterge*dir; aynı renkte bir üst sayısı o elin *okey*idir ve her taşın yerine geçer. "
      "Sahte okey joker değildir, okeyin rengi ve sayısı yerine oynanır.");
    X({{{T(M, 6)}, "Gösterge: Mavi 6"}, {{TOkey()}, "Okey: Mavi 7 (joker)"}, {{TFake()}, "Sahte okey = Mavi 7"}});

    H("Dağıtım ve tur");
    P("Herkese 14 taş dağıtılır; eli başlatan oyuncu 15 taşla başlar, ilk turunda taş çekmeden bir taş atar. "
      "Sıra sağa döner. Sıran gelince:");
    B("*Taş al:* ortadan bir taş çek ya da solundakinin attığı son taşı al. 101'den farklı olarak yandan aldığın "
      "taşı kullanma zorunluluğu yoktur.", "1.");
    B("*Taş at:* bir taşı kendi atık yerine at. Sağındaki oyuncu onu alabilir.", "2.");

    H("Perler");
    B("*Seri:* aynı renkten en az 3 ardışık sayı. Okeyde 1, 13'ün arkasından da gelebilir (*12-13-1*), ama "
      "serinin son taşı olur: 13-1-2 olmaz.");
    {
        std::vector<RuleItem> it = {{{T(R, 5), T(R, 6), T(R, 7)}, "Kırmızı 5-6-7"},
                                    {{T(M, 12), T(M, 13), T(M, 1)}, "12-13-1 olur"},
                                    {{T(K, 13), T(K, 1), T(K, 2)}, "13-1-2 olmaz"}};
        it[2].wrong = true;
        X(it);
    }
    B("*Grup:* aynı sayının farklı renkleri, 3 ya da 4 taş.");
    X({{{T(Y, 9), T(M, 9), T(K, 9)}, "Üçlü grup"}, {{T(R, 5), TOkey(), T(R, 7)}, "Okey eksiği tamamlar"}});
    B("*Çift:* birebir aynı iki taş. Elinde *yedi çift* toplarsan çiftten bitersin (*çifte gitmek*).");

    H("Bitmek");
    P("Taşını çektikten sonra elindeki 15 taştan 14'ü perlere ya da yedi çifte oturuyorsa, fazla taşı atıp "
      "elini gösterirsin: *bittin*. Masada bunu yapmak için taşı ortaya, *Bitir* yerine sürükle (ya da taşı "
      "seçip *Bitir*'e bas). Oyun ıstakanı kendisi kontrol eder; perlerin dizili olmasa da olur.");
    P("Elin bittiği hâlde okeyi fazla taş olarak atabiliyorsan *okey atarak* bitersin (*okeye dönmek*): "
      "puan iki katına çıkar.");

    H("Gösterge");
    P("Elinde göstergenin eşi (aynı renk ve sayıdaki öbür taş) varsa, ilk taşını atmadan önce onu "
      "gösterebilirsin: *Göster* düğmesine bas. Diğer herkesin puanından ^1^ düşülür. Bir elde gösterge bir "
      "kez gösterilir.");

    H("Puanlama");
    P("Herkes oyuna aynı puanla başlar (ayarlardan *6, 12 ya da 20*; çoğu kahvede 20). Puanlar geriye sayılır, "
      "*yüksek puan iyidir*:");
    B("Biri bitince diğer üç oyuncunun puanından ^2^ düşülür.");
    B("*Okey atarak* bitişte ^4^, *çiftten* bitişte ^4^, ikisi birden olursa ^8^ düşülür.");
    B("Gösterge gösterilince diğerlerinden ^1^ düşülür.");
    B("Ortadaki taşlar biter ve kimse bitemezse el berabere biter; yalnızca gösterge sayılır.");
    B("*Renkli okey* (ayarlardan): gösterge kırmızı ya da siyahsa o elde bütün puanlar iki katına çıkar.");
    P("Puanı sıfıra (ya da altına) inen biri olunca oyun biter; *en yüksek puanda kalan* kazanır.");

    H("Kontroller");
    B("*Taş çekmek:* ortadaki desteye ya da sol alttaki atık taşına tıkla veya onu ıstakana sürükle.");
    B("*Taş atmak:* taşı sağ alttaki atık yerine sürükle ya da taşa çift tıkla.");
    B("*Bitmek:* fazla taşı masanın ortasına (*Bitir* yazan yere) sürükle. El bitmeye hazırsa ipucu açıkken "
      "*Bitir* parlar.");
    B("*Seri Diz / Çift Diz:* ıstakanı seri ya da çift düzenine göre kendiliğinden dizer (*S* / *C*).");
    B("*Göster:* göstergenin eşini gösterir (yalnızca ilk taşını atmadan önce).");
    B("*İpucu:* Kurt'un senin yerinde ne yapacağını gösterir (*H*).");
    B("*Etrafa bakmak:* sağ tuşla sürükle; *Y* yapay zekaya bırakır, *ESC* duraklatır.");
    return v;
}

#include "ui/RulesText.inc"

// The other games' rules pages, from their embedded docs (light markdown, see tools/gen_rules.py).
std::vector<RuleBlock> parseRulesMd(const char* md) {
    std::vector<RuleBlock> v;
    std::string para;
    auto bold = [](std::string t) { // **x** -> *x* (the rich text's bold)
        size_t p;
        while ((p = t.find("**")) != std::string::npos) t.replace(p, 2, "*");
        return t;
    };
    auto flush = [&]() {
        if (para.empty()) return;
        RuleBlock b;
        b.kind = RuleBlock::Para;
        b.text = bold(para);
        v.push_back(b);
        para.clear();
    };
    std::string line;
    const std::string src = md;
    size_t pos = 0;
    bool tableHeader = true;
    while (pos <= src.size()) {
        const size_t nl = src.find('\n', pos);
        line = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? src.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        if (line.empty()) {
            flush();
            tableHeader = true;
            continue;
        }
        if (line.rfind("## ", 0) == 0) {
            flush();
            RuleBlock b;
            b.kind = RuleBlock::Heading;
            b.text = line.substr(3);
            v.push_back(b);
            continue;
        }
        if (line[0] == '|') {
            flush();
            std::vector<std::string> cells;
            size_t a = 1;
            while (a < line.size()) {
                const size_t e = line.find('|', a);
                if (e == std::string::npos) break;
                std::string c = line.substr(a, e - a);
                while (!c.empty() && c.front() == ' ') c.erase(c.begin());
                while (!c.empty() && c.back() == ' ') c.pop_back();
                cells.push_back(c);
                a = e + 1;
            }
            const bool sep = !cells.empty() && cells[0].find_first_not_of("-: ") == std::string::npos;
            if (sep || tableHeader) { // the header row and the |---| row
                tableHeader = false;
                continue;
            }
            if (cells.size() >= 2) {
                RuleBlock b;
                b.kind = RuleBlock::Bullet;
                b.text = "*" + bold(cells[0]) + "*: " + bold(cells[1]);
                for (size_t i = 2; i < cells.size(); ++i) b.text += " \xC2\xB7 " + bold(cells[i]);
                v.push_back(b);
            }
            continue;
        }
        size_t digits = 0;
        while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
        if (line.rfind("- ", 0) == 0 || (digits > 0 && line.compare(digits, 2, ". ") == 0)) {
            flush();
            RuleBlock b;
            b.kind = RuleBlock::Bullet;
            if (line[0] == '-') {
                b.text = bold(line.substr(2));
            } else {
                b.label = line.substr(0, digits + 1);
                b.text = bold(line.substr(digits + 2));
            }
            v.push_back(b);
            continue;
        }
        if (!v.empty() && v.back().kind == RuleBlock::Bullet && para.empty() && line[0] == ' ') { // a bullet's 2nd line
            v.back().text += " " + bold(line.substr(line.find_first_not_of(' ')));
            continue;
        }
        para += (para.empty() ? "" : " ") + line;
    }
    flush();
    return v;
}

void addControls(std::vector<RuleBlock>& v, GameKind g) {
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto B = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Bullet; b.text = s; v.push_back(b); };
    H("Kontroller");
    if (g == GameKind::Tavla) {
        B("*Zar At* düğmesi (ya da *boşluk*) zarları atar; ilk elde kimin başlayacağını da böyle belirlersiniz.");
        B("Oynamak için önce pulunun durduğu haneye tıkla, sonra yeşil yanan haneye. Kırık pulun varsa önce o seçilir.");
        B("Pul toplarken sağdaki toplama alanına tıkla. Yanlış oynadıysan *Geri Al* (ya da *geri tuşu*) son hamleni geri alır.");
        B("Sağ tık seçimi bırakır. İpuçları açıkken oynayabileceğin pullar parlar, pip sayıları da altta görünür.");
    } else {
        B("Sıra sende olunca oynayabileceğin kâğıtlar parlak, oynayamayacakların soluk görünür. Kâğıdın üstüne gelip tıkla.");
        if (g == GameKind::Batak) B("İhalede ortada açılan panelden sayını ya da *Pas*'ı, ihaleyi alınca kozu seç.");
        if (g == GameKind::King) B("Seçme sırası sendeyken ortadaki panelden oyunu seç; koz seçersen hangi rengin koz olacağını da sorar.");
        B("Masaya atılan kâğıtlar bir an ortada kalır, sonra eli alanın önüne gider.");
    }
    B("*Yapay Zeka* düğmesi ya da *Y* seni yapay zekaya bırakır; *ESC* ya da *Menü* oyunu duraklatır.");
}

std::vector<RuleBlock> buildRules(GameKind g) {
    switch (g) {
    case GameKind::Tavla:
    case GameKind::Pisti:
    case GameKind::Batak:
    case GameKind::King: {
        const char* md = g == GameKind::Tavla ? kRules_tavla : g == GameKind::Pisti ? kRules_pisti
                         : g == GameKind::Batak ? kRules_batak : kRules_king;
        std::vector<RuleBlock> v = parseRulesMd(md);
        addControls(v, g);
        return v;
    }
    case GameKind::Okey: return buildRulesOkey();
    case GameKind::YuzbirEsli: return buildRules101(true);
    default: return buildRules101(false);
    }
}

std::vector<RuleTab> ruleTabsFor(GameKind g) {
    if (g == GameKind::Tavla || g == GameKind::Pisti || g == GameKind::Batak || g == GameKind::King) {
        static const char* const shortNames[][2] = {
            {"Masadan masaya değişenler", "Farklar"}, {"Kahvehane usulleri (değişebilenler)", "Usuller"},
            {"Kapı, açık pul, kırık pul", "Kırık pul"}, {"Zarların adları", "Zar adları"}, {"Dağıtım ve ihale", "İhale"},
            {"Koz ve ilk el", "Koz"}, {"Oyun kuralları", "Kurallar"}, {"Kimlerle oynanır?", "Kimlerle"},
            {"Kim başlar?", "Başlangıç"}, {"Kim seçer?", "Kim seçer"}, {"Puanlar ve maç", "Puanlar"},
            {"Diziliş ve yön", "Diziliş"}, {"Zarı oynamak", "Zar"}, {"Pul toplamak", "Toplamak"},
            {"Cezalar (12 el)", "Cezalar"}, {"Koz (8 el)", "Koz"}, {"Elde oyun", "Oyun"}, {"Oyunun sonu", "Son"},
            {"Eşli batak", "Eşli"}};
        std::vector<RuleTab> tabs;
        for (const RuleBlock& b : buildRules(g)) {
            if (b.kind != RuleBlock::Heading) continue;
            std::string label = b.text;
            for (const auto& sn : shortNames)
                if (b.text == sn[0]) label = sn[1];
            if (label.size() > 14) label = label.substr(0, label.find(' '));
            tabs.push_back({label, b.text});
        }
        if (tabs.size() > 8) tabs.erase(tabs.begin() + 7, tabs.end() - 1); // keep the controls tab
        return tabs;
    }
    switch (g) {
    case GameKind::Okey:
        return {{"Taşlar", "Taşlar ve okey"}, {"Tur", "Dağıtım ve tur"}, {"Perler", "Perler"}, {"Bitmek", "Bitmek"},
                {"Gösterge", "Gösterge"},    {"Puanlama", "Puanlama"},     {"Kontroller", "Kontroller"}};
    case GameKind::YuzbirEsli:
        return {{"Eşli", "Eşli oyun"},   {"Taşlar", "Taşlar"},     {"Tur", "Sıra ve tur"},
                {"Perler", "Perler: seri, grup, çift"}, {"El açmak", "Elini açmak"}, {"Cezalar", "Cezalar"},
                {"Puanlama", "Puanlama"}, {"Kontroller", "Kontroller"}};
    default:
        return {{"Taşlar", "Taşlar"},      {"Okey", "Gösterge ve okey"}, {"Tur", "Sıra ve tur"},
                {"Perler", "Perler: seri, grup, çift"}, {"El açmak", "Elini açmak"}, {"Cezalar", "Cezalar"},
                {"Puanlama", "Puanlama"}, {"Kontroller", "Kontroller"}};
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
float glassWidthAt(float t) { // ince belli: wide rim, narrow waist, round belly
    if (t < 0.52f) return 1.f - 0.40f * std::sin(t / 0.52f * kPi * 0.5f);
    const float u = (t - 0.52f) / 0.48f;
    return 0.60f + 0.34f * std::sin(u * kPi * 0.78f);
}

void drawSteam(Vector2 base, float time, float scale, float alpha, float rise = 90.f) {
    for (int i = 0; i < 7; ++i) {
        const float ph = std::fmod(time * 0.32f + (float)i / 7.f, 1.f);
        const float y = base.y - ph * rise * scale;
        const float x = base.x + std::sin(ph * 5.5f + (float)i * 1.7f) * 10.f * scale + ((float)(i % 3) - 1.f) * 6.f;
        const float r = (7.f + ph * 16.f) * scale;
        const Color c = alphaMul(pal::Smoke, std::sin(ph * kPi) * 0.30f * alpha);
        DrawCircleGradient({x, y}, r, c, alphaMul(c, 0.f));
    }
}

void drawTeaGlass(Vector2 base, float s, float time, float steamRise = 90.f) {
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
void drawPencil(Vector2 p, float angleDeg) {
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
};

// ---------------------------------------------------------------- small drawings (game cards)
// A card suit (0 maça, 1 kupa, 2 karo, 3 sinek) centred at c, about `s` tall.
void drawSuit(Vector2 c, float s, int suit, Color col) {
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
void drawMiniCard(Vector2 c, float w, float deg, const char* idx, int suit) {
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
void drawMiniDie(Vector2 c, float s, int n, float deg) {
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
void beginClip(Rectangle r) {
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
constexpr float SelCardW = 300.f, SelCardH = 268.f, SelGap = 28.f;
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
    C_BatakEsli, C_BatakTarget, C_PistiTarget, C_PistiMode, C_TitleStats, C_Continue, C_StatsBack, C_GuideOk, C_Guide, C_TavlaDoubling, C_TavlaKatmerli,
    C_OkeyRenkli, C_BatakKoz, C_King12, C_SetPage, C_DayTime, C_Season, C_Voices, C_ColorBlind, C_BigText, C_StatsReplays, C_ReplayWatch,
    C_ReplaysBack, C_ReplayAnalyze, C_ShowAnalysis, C_AnalysisBack,
};



constexpr int kScreenCount = 12;

} // namespace

// ============================================================================ Screens::Impl

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
    int settingsPage = 0; // 0 Oyun, 1 Görünüm · Ses

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
    float autoLeft = -1.f;

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
    float rnd() {
        rngState = hashU(rngState + 0x9E3779B9u);
        return (float)(rngState & 0xFFFFFFu) / 16777216.f;
    }
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    void sfx(Sfx s) {
        if (owner && owner->playSfx) owner->playSfx(s);
    }
    void click(int id, int value = 0) { clicks.push_back({cur, id, value}); }

    // Press + release over the same custom widget (mirrors drawButton's behaviour).
    bool hit(Rectangle r, int id, Vector2 m) {
        const bool hover = pointInRect(m, r);
        if (hover) requestHandCursor();
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pressedId = id;
        bool clicked = false;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && pressedId == id) {
            clicked = hover;
            pressedId = -1;
        }
        return clicked;
    }

    void show(ScreenId id) {
        if (id == cur) return;
        if (cur == ScreenId::Settings && nameEditing && commitName()) settingsDirty = true;
        if (id == ScreenId::Settings || id == ScreenId::Rules) {
            ScreenId back = cur;
            if (cur == ScreenId::Settings) back = backSettings;
            else if (cur == ScreenId::Rules) back = backRules;
            (id == ScreenId::Settings ? backSettings : backRules) = back;
        }
        // keep a running fade continuous: the screen that was fading in starts fading out from where it was
        fade = (fade < 1.f) ? 1.f - fade : 0.f;
        if (freezePending) frozen.reset(); // left before its copy was taken: it fades out from the live game
        freezePending = drawsGame(id);     // copied by the next update()/draw() that has the game
        prev = cur;
        cur = id;
        shownAt[(int)id] = time;
        autoLeft = -1.f;
        clicks.clear();
        pressedId = -1;
        switch (id) {
        case ScreenId::Settings:
            nameEditing = false;
            toggleAnim[0] = settings.sfx ? 1.f : 0.f;
            toggleAnim[1] = settings.ambient ? 1.f : 0.f;
            toggleAnim[2] = settings.music ? 1.f : 0.f;
            toggleAnim[3] = settings.hints ? 1.f : 0.f;
            toggleAnim[4] = settings.katlamali ? 1.f : 0.f;
            toggleAnim[5] = settings.yandanCeza ? 1.f : 0.f;
            toggleAnim[6] = settings.batakEsli ? 1.f : 0.f;
            toggleAnim[7] = settings.guide ? 1.f : 0.f;
            toggleAnim[8] = settings.tavlaDoubling ? 1.f : 0.f;
            toggleAnim[9] = settings.tavlaKatmerli ? 1.f : 0.f;
            toggleAnim[10] = settings.okeyRenkli ? 1.f : 0.f;
            toggleAnim[11] = settings.batakKozKirilmadan ? 1.f : 0.f;
            toggleAnim[12] = settings.king12 ? 1.f : 0.f;
            toggleAnim[13] = settings.voices ? 1.f : 0.f;
            toggleAnim[14] = settings.colorBlind ? 1.f : 0.f;
            toggleAnim[15] = settings.bigText ? 1.f : 0.f;
            break;
        case ScreenId::Rules:
            scroll = scrollTarget = 0.f;
            draggingThumb = false;
            break;
        case ScreenId::Paused:
            confirmQuit = false;
            break;
        case ScreenId::MatchOver:
            spawnConfetti();
            break;
        default:
            break;
        }
    }

    void goBack() {
        if (cur == ScreenId::Settings) show(backSettings);
        else if (cur == ScreenId::Rules) show(backRules);
    }

    static bool drawsGame(ScreenId id) {
        return id == ScreenId::Paused || id == ScreenId::HandSummary || id == ScreenId::MatchOver;
    }
    void freeze(const okey::Game* g) {
        if (!freezePending || !g || !drawsGame(cur)) return;
        frozen = *g;
        freezePending = false;
    }
    // The game a layer is drawn from: the outgoing screen uses its copy, the game may have moved on.
    const okey::Game* gameFor(ScreenId id, const okey::Game* g) const {
        return (id == prev && id != cur && drawsGame(id) && frozen) ? &*frozen : g;
    }

    // Seats sharing the lowest match total. Game::leaderSeat() alone settles a tie by seat order, which would
    // crown one of them silently; the sheet and the final standings show a shared first place instead.
    // Klasik okey: the highest total leads; eşli 101: both seats of the team with the lower combined total.
    static int standing(const okey::Game& g, int s) {
        if (g.classic()) return -g.player(s).totalScore;
        if (g.teams()) return g.teamTotal(s);
        return g.player(s).totalScore;
    }
    static std::vector<int> coLeaders(const okey::Game& g) {
        int best = standing(g, 0);
        for (int s = 1; s < okey::NUM_PLAYERS; ++s) best = std::min(best, standing(g, s));
        std::vector<int> v;
        for (int s = 0; s < okey::NUM_PLAYERS; ++s)
            if (standing(g, s) == best) v.push_back(s);
        return v;
    }
    // "Sen ve Kel Mahmut" for a team (eşli 101)
    std::string teamName(const okey::Game& g, int s) const {
        const int a = std::min(s, okey::Game::partnerOf(s)), b = std::max(s, okey::Game::partnerOf(s));
        return seatName(g, a) + " ve " + seatName(g, b);
    }
    // A seat's name on the sheet and the final standings: the human's seat says who played it in the Yapay Zeka
    // mode ("Yapay Zeka (Sen)").
    std::string seatName(const okey::Game& g, int s) const {
        const okey::PlayerInfo& p = g.player(s);
        return (aiMode && p.human) ? "Yapay Zeka (" + p.name + ")" : p.name;
    }
    // "A", "A ve B", "A, B ve C" (the human's seat left out when `skipHuman`)
    std::string joinNames(const okey::Game& g, const std::vector<int>& seats, bool skipHuman) const {
        std::vector<std::string> names;
        for (int s : seats)
            if (!(skipHuman && g.player(s).human)) names.push_back(seatName(g, s));
        std::string out;
        for (size_t i = 0; i < names.size(); ++i)
            out += (i == 0 ? "" : i + 1 == names.size() ? " ve " : ", ") + names[i];
        return out;
    }
    static bool humanAmong(const okey::Game& g, const std::vector<int>& seats) {
        for (int s : seats)
            if (g.player(s).human) return true;
        return false;
    }

    // A button label with the self-press countdown ("Sonraki El (4)"), shrunk to fit the button.
    std::string autoLabel(const std::string& base, FontId f, float maxW, float& size) const {
        const std::string label = autoLeft >= 0.f ? base + " (" + std::to_string((int)std::ceil(autoLeft)) + ")" : base;
        while (size > 18.f && measureText(f, label, size, 0.5f).x > maxW) size -= 1.f;
        return label;
    }

    static bool isLastHand(const okey::Game* g) {
        if (!g) return false;
        if (g->classic()) return g->handState() == okey::HandState::MatchOver;
        return g->handState() == okey::HandState::MatchOver ||
               (int)g->player(0).handScores.size() >= g->numHands();
    }

    // ------------------------------------------------------------ settings logic
    void startEditing() {
        nameEditing = true;
        nameBuf = settings.playerName;
        nameBefore = settings.playerName;
    }
    // Enter / click outside. An empty name falls back to the name before editing. True if settings changed.
    bool commitName() {
        nameEditing = false;
        std::string t = trimmed(nameBuf);
        if (t.empty()) t = nameBefore.empty() ? std::string("Sen") : nameBefore;
        nameBuf = t;
        if (t == settings.playerName) return false;
        settings.playerName = t;
        return true;
    }
    // ESC: undo the live edits.
    bool cancelName() {
        nameBuf = nameBefore.empty() ? settings.playerName : nameBefore;
        nameEditing = false;
        if (nameBuf == settings.playerName) return false;
        settings.playerName = nameBuf;
        return true;
    }
    bool applyLiveName() {
        const std::string t = trimmed(nameBuf);
        if (t.empty() || t == settings.playerName) return false;
        settings.playerName = t;
        return true;
    }
    int selectedHands() const {
        int best = 0;
        for (int i = 0; i < 6; ++i)
            if (std::abs(kHandChoices[i] - settings.numHands) < std::abs(kHandChoices[best] - settings.numHands)) best = i;
        return best;
    }
    int selectedAnim() const {
        int best = 0;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(kAnimChoices[i] - settings.animSpeed) < std::fabs(kAnimChoices[best] - settings.animSpeed))
                best = i;
        return best;
    }

    // ------------------------------------------------------------ click handling (update)
    ScreenAction handleClick(const Click& c, const okey::Game* g) {
        sfx(Sfx::Button);
        switch (c.id) {
        case C_Play:
            show(ScreenId::GameSelect);
            return ScreenAction::None;
        case C_GameCard:
            if (c.value < 0 || c.value >= (int)GameKind::Count || !gameAvailable((GameKind)c.value)) return ScreenAction::None;
            if (settings.game != c.value) {
                settings.game = c.value;
                rulesLaidOut = false; // the rules page follows the game
                settingsDirty = true; // remembered (reported as SettingsChanged by the next update)
            }
            show(ScreenId::None);
            return ScreenAction::StartMatch;
        case C_SelBack:
            show(ScreenId::Title);
            return ScreenAction::None;
        case C_OkeyStart:
            settings.okeyStart = kOkeyStartChoices[std::clamp(c.value, 0, 2)];
            return ScreenAction::SettingsChanged;
        case C_TavlaPoints:
            settings.tavlaPoints = kTavlaChoices[std::clamp(c.value, 0, 2)];
            return ScreenAction::SettingsChanged;
        case C_BatakEsli:
            settings.batakEsli = !settings.batakEsli;
            return ScreenAction::SettingsChanged;
        case C_BatakTarget:
            settings.batakTarget = kBatakTargets[std::clamp(c.value, 0, 2)];
            return ScreenAction::SettingsChanged;
        case C_PistiTarget:
            settings.pistiTarget = kPistiTargets[std::clamp(c.value, 0, 1)];
            return ScreenAction::SettingsChanged;
        case C_PistiMode:
            settings.pistiMode = std::clamp(c.value, 0, 2);
            return ScreenAction::SettingsChanged;
        case C_Guide:
            settings.guide = !settings.guide;
            if (settings.guide) settings.guideSeen = 0; // switched on again: every game's guide once more
            return ScreenAction::SettingsChanged;
        case C_TavlaDoubling:
            settings.tavlaDoubling = !settings.tavlaDoubling;
            return ScreenAction::SettingsChanged;
        case C_TavlaKatmerli:
            settings.tavlaKatmerli = !settings.tavlaKatmerli;
            return ScreenAction::SettingsChanged;
        case C_OkeyRenkli:
            settings.okeyRenkli = !settings.okeyRenkli;
            return ScreenAction::SettingsChanged;
        case C_BatakKoz:
            settings.batakKozKirilmadan = !settings.batakKozKirilmadan;
            return ScreenAction::SettingsChanged;
        case C_King12:
            settings.king12 = !settings.king12;
            return ScreenAction::SettingsChanged;
        case C_SetPage:
            if (nameEditing) commitName();
            settingsPage = std::clamp(c.value, 0, 1);
            return ScreenAction::None;
        case C_DayTime:
            settings.dayTime = std::clamp(c.value, 0, 4);
            return ScreenAction::SettingsChanged;
        case C_Season:
            settings.season = std::clamp(c.value, 0, 4);
            return ScreenAction::SettingsChanged;
        case C_Voices:
            settings.voices = !settings.voices;
            return ScreenAction::SettingsChanged;
        case C_ColorBlind:
            settings.colorBlind = !settings.colorBlind;
            return ScreenAction::SettingsChanged;
        case C_BigText:
            settings.bigText = !settings.bigText;
            return ScreenAction::SettingsChanged;
        case C_WatchAi:
            show(ScreenId::None);
            return ScreenAction::StartAiMatch;
        case C_PauseAi:
            show(ScreenId::None);
            return ScreenAction::ToggleAiMode;
        case C_TitleStats:
            show(ScreenId::Stats);
            return ScreenAction::None;
        case C_StatsBack:
            show(ScreenId::Title);
            return ScreenAction::None;
        case C_StatsReplays:
            show(ScreenId::Replays);
            return ScreenAction::None;
        case C_ReplaysBack:
            show(ScreenId::Stats);
            return ScreenAction::None;
        case C_ReplayAnalyze:
            chosen = c.value;
            analysisBack = ScreenId::Replays;
            show(ScreenId::Analysis);
            return ScreenAction::AnalyzeReplay;
        case C_ShowAnalysis:
            analysisBack = ScreenId::MatchOver;
            show(ScreenId::Analysis);
            return ScreenAction::None;
        case C_AnalysisBack:
            show(analysisBack);
            return ScreenAction::None;
        case C_ReplayWatch:
            chosen = c.value;
            show(ScreenId::None);
            return ScreenAction::WatchReplay;
        case C_GuideOk:
            show(ScreenId::None);
            return ScreenAction::None;
        case C_Continue:
            show(ScreenId::None);
            return ScreenAction::ResumeSaved;
        case C_TitleRules:
        case C_PauseRules:
            show(ScreenId::Rules);
            return ScreenAction::None;
        case C_TitleSettings:
        case C_PauseSettings:
            show(ScreenId::Settings);
            return ScreenAction::None;
        case C_Quit:
            return ScreenAction::Quit;
        case C_Back: {
            const bool changed = (cur == ScreenId::Settings && nameEditing) ? commitName() : false;
            goBack();
            return changed ? ScreenAction::SettingsChanged : ScreenAction::None;
        }
        case C_Defaults: {
            Settings d;
            d.game = settings.game; // "Varsayılanlar" resets the options, not the game being played
            settings = d;
            nameEditing = false;
            nameBuf = settings.playerName;
            return ScreenAction::SettingsChanged;
        }
        case C_Hands:
            settings.numHands = kHandChoices[std::clamp(c.value, 0, 5)];
            return ScreenAction::SettingsChanged;
        case C_Level:
            settings.difficulty = std::clamp(c.value, 0, 2);
            return ScreenAction::SettingsChanged;
        case C_Anim:
            settings.animSpeed = kAnimChoices[std::clamp(c.value, 0, 2)];
            return ScreenAction::SettingsChanged;
        case C_Sfx:
            settings.sfx = !settings.sfx;
            return ScreenAction::SettingsChanged;
        case C_Ambient:
            settings.ambient = !settings.ambient;
            return ScreenAction::SettingsChanged;
        case C_Music:
            settings.music = !settings.music;
            return ScreenAction::SettingsChanged;
        case C_Hints:
            settings.hints = !settings.hints;
            return ScreenAction::SettingsChanged;
        case C_Katlamali:
            settings.katlamali = !settings.katlamali;
            return ScreenAction::SettingsChanged;
        case C_YandanCeza:
            settings.yandanCeza = !settings.yandanCeza;
            return ScreenAction::SettingsChanged;
        case C_Name:
            if (!nameEditing) startEditing();
            return ScreenAction::None;
        case C_Resume:
            show(ScreenId::None);
            return ScreenAction::Resume;
        case C_PauseMenu:
            confirmQuit = true;
            confirmAt = time;
            return ScreenAction::None;
        case C_ConfirmYes:
            confirmQuit = false;
            show(ScreenId::Title);
            return ScreenAction::ToTitle;
        case C_ConfirmNo:
            confirmQuit = false;
            return ScreenAction::None;
        case C_Next:
            if (sheet ? sheet->last : isLastHand(g)) {
                show(ScreenId::MatchOver);
                return ScreenAction::ShowMatchResult;
            }
            show(ScreenId::None);
            return ScreenAction::NextHand;
        case C_NewGame:
            show(ScreenId::None);
            return ScreenAction::StartMatch;
        case C_MatchMenu:
            show(ScreenId::Title);
            return ScreenAction::ToTitle;
        case C_RulesTab:
            if (rulesLaidOut && c.value >= 0 && c.value < (int)tabY.size())
                scrollTarget = std::clamp(tabY[c.value] - 6.f, 0.f, maxScroll());
            return ScreenAction::None;
        default:
            return ScreenAction::None;
        }
    }

    // ------------------------------------------------------------ per-frame logic
    ScreenAction update(float dt, Vector2 m, const okey::Game* g) {
        dt = std::clamp(dt, 0.f, 0.1f);
        time += dt;
        if (fade < 1.f) fade = std::min(1.f, fade + dt / kFadeTime);
        mouse = m;
        freeze(g);
        if (!fadeShader.ok) fadeShader.load();

        ScreenAction act = ScreenAction::None;
        auto merge = [&act](ScreenAction a) {
            if (a == ScreenAction::None) return;
            if (act == ScreenAction::None || act == ScreenAction::SettingsChanged) act = a;
        };
        if (settingsDirty) {
            settingsDirty = false;
            act = ScreenAction::SettingsChanged;
        }

        // 1. clicks recorded by the previous draw()
        std::vector<Click> pending;
        pending.swap(clicks);
        bool clicked = false;
        for (const Click& c : pending) {
            if (c.screen != cur) continue; // screen changed since the click was recorded
            merge(handleClick(c, g));
            clicked = true;
        }

        // 2. keyboard / wheel / text input of the current screen
        const bool settled = fade >= 1.f;
        const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        switch (cur) {
        case ScreenId::Title:
            if (!clicked && settled && enter) merge(handleClick({cur, C_Play, 0}, g));
            break;
        case ScreenId::Settings:
            merge(updateSettings(dt, m, clicked));
            break;
        case ScreenId::Rules:
            updateRules(dt, m);
            if (!clicked && IsKeyPressed(KEY_ESCAPE)) goBack();
            break;
        case ScreenId::None:
            if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Paused);
            break;
        case ScreenId::Paused:
            if (!clicked && IsKeyPressed(KEY_ESCAPE)) {
                if (confirmQuit) confirmQuit = false;
                else merge(handleClick({cur, C_Resume, 0}, g));
            }
            break;
        case ScreenId::HandSummary:
            if (!clicked && settled && (enter || IsKeyPressed(KEY_SPACE))) merge(handleClick({cur, C_Next, 0}, g));
            break;
        case ScreenId::MatchOver:
            if (!clicked && settled && enter) merge(handleClick({cur, C_NewGame, 0}, g));
            break;
        case ScreenId::GameSelect:
            if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Title);
            else if (!clicked && settled && enter) merge(handleClick({cur, C_GameCard, settings.game}, g));
            break;
        case ScreenId::Stats:
            if (!clicked && (IsKeyPressed(KEY_ESCAPE) || (settled && enter))) show(ScreenId::Title);
            break;
        case ScreenId::Replays:
            if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Stats);
            break;
        case ScreenId::Analysis:
            if (!clicked && (IsKeyPressed(KEY_ESCAPE) || (settled && enter))) show(analysisBack);
            break;
        case ScreenId::Guide:
            if (!clicked && settled && (IsKeyPressed(KEY_ESCAPE) || enter || IsKeyPressed(KEY_SPACE))) show(ScreenId::None);
            break;
        }

        // 3. animations
        if (cur == ScreenId::MatchOver || (prev == ScreenId::MatchOver && fade < 1.f)) updateConfetti(dt);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pressedId = -1;
        return act;
    }

    ScreenAction updateSettings(float dt, Vector2 m, bool clicked) {
        const bool on[16] = {settings.sfx,        settings.ambient,   settings.music,         settings.hints,
                             settings.katlamali,  settings.yandanCeza, settings.batakEsli,    settings.guide,
                             settings.tavlaDoubling, settings.tavlaKatmerli, settings.okeyRenkli,
                             settings.batakKozKirilmadan, settings.king12, settings.voices, settings.colorBlind,
                             settings.bigText};
        for (int i = 0; i < 16; ++i) toggleAnim[i] = approach(toggleAnim[i], on[i] ? 1.f : 0.f, 16.f, dt);

        ScreenAction act = ScreenAction::None;
        if (nameEditing) {
            // a press anywhere outside the field ends editing
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !pointInRect(m, L::SetName) && commitName())
                act = ScreenAction::SettingsChanged;
        }
        if (nameEditing) {
            bool changed = false;
            for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
                if (!nameCodepointOk(cp) || utf8Count(nameBuf) >= 12) continue;
                if (cp == ' ' && (nameBuf.empty() || nameBuf.back() == ' ')) continue;
                utf8Append(nameBuf, cp);
                changed = true;
            }
            if (!nameBuf.empty() && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))) {
                utf8PopBack(nameBuf);
                changed = true;
            }
            if (changed && applyLiveName()) act = ScreenAction::SettingsChanged;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (commitName()) act = ScreenAction::SettingsChanged;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                if (cancelName()) act = ScreenAction::SettingsChanged;
            }
            return act;
        }
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) goBack();
        return act;
    }

    // ------------------------------------------------------------ rules layout & scrolling
    void ensureRulesLayout() {
        const int game = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
        if (rulesLaidOut && rulesGame == game) return;
        rulesGame = game;
        rules = buildRules((GameKind)game);
        tabs = ruleTabsFor((GameKind)game);
        tabY.assign(tabs.size(), 0.f);
        const float width = L::RulesView.width;
        const float size = 21.f, lineH = 29.f;
        float y = 6.f;
        for (size_t i = 0; i < rules.size(); ++i) {
            RuleBlock& b = rules[i];
            switch (b.kind) {
            case RuleBlock::Heading:
                if (i > 0) y += 22.f;
                b.y = y;
                b.h = 42.f;
                for (size_t t = 0; t < tabs.size(); ++t)
                    if (b.text == tabs[t].heading) tabY[t] = y;
                y += b.h + 6.f;
                break;
            case RuleBlock::Para:
                if (i > 0 && rules[i - 1].kind == RuleBlock::Bullet) y += 8.f;
                b.rich = layoutRich(b.text, width, size, lineH);
                b.y = y;
                b.h = b.rich.height;
                y += b.h + 8.f;
                break;
            case RuleBlock::Bullet:
                b.rich = layoutRich(b.text, width - 34.f, size, lineH);
                b.y = y;
                b.h = b.rich.height;
                y += b.h + 4.f;
                break;
            case RuleBlock::Tiles: {
                float x = 34.f, rowY = 4.f, rowH = 0.f;
                for (RuleItem& it : b.items) {
                    const float n = (float)it.tiles.size();
                    const float tilesW = n * kMiniW + (n - 1.f) * kMiniGap;
                    const float capW = measureText(FontId::Ui, it.caption, 17.f).x;
                    const float w = std::max(tilesW, capW);
                    const float h = kMiniH + 30.f;
                    if (x > 34.f && x + w > width) {
                        x = 34.f;
                        rowY += rowH + 10.f;
                        rowH = 0.f;
                    }
                    it.box = {x, rowY, w, h};
                    x += w + 38.f;
                    rowH = std::max(rowH, h);
                }
                b.y = y;
                b.h = rowY + rowH + 4.f;
                y += b.h + 10.f;
                break;
            }
            }
        }
        rulesContentH = y + 24.f;
        rulesLaidOut = true;
    }
    float maxScroll() const { return std::max(0.f, rulesContentH - L::RulesView.height); }
    Rectangle thumbRect() const {
        const Rectangle tr = L::RulesTrack;
        const float ms = maxScroll();
        const float frac = rulesContentH > 0.f ? std::min(1.f, L::RulesView.height / rulesContentH) : 1.f;
        const float th = std::max(48.f, tr.height * frac);
        const float t = ms > 0.f ? scroll / ms : 0.f;
        return {tr.x - 3.f, tr.y + (tr.height - th) * t, tr.width + 6.f, th};
    }

    void updateRules(float dt, Vector2 m) {
        if (!rulesLaidOut) return;
        const float ms = maxScroll();
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.f) scrollTarget -= wheel * 90.f;
        const float page = L::RulesView.height * 0.85f;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) scrollTarget += 70.f;
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) scrollTarget -= 70.f;
        if (IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) scrollTarget += page;
        if (IsKeyPressed(KEY_PAGE_UP) || IsKeyPressedRepeat(KEY_PAGE_UP)) scrollTarget -= page;
        if (IsKeyPressed(KEY_HOME)) scrollTarget = 0.f;
        if (IsKeyPressed(KEY_END)) scrollTarget = ms;

        const Rectangle thumb = thumbRect();
        const Rectangle track{L::RulesTrack.x - 8.f, L::RulesTrack.y, L::RulesTrack.width + 16.f, L::RulesTrack.height};
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ms > 0.f) {
            if (pointInRect(m, thumb)) {
                draggingThumb = true;
                dragOffset = m.y - thumb.y;
            } else if (pointInRect(m, track)) {
                scrollTarget += (m.y < thumb.y ? -page : page);
            }
        }
        if (draggingThumb) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const float range = L::RulesTrack.height - thumb.height;
                const float t = range > 0.f ? (m.y - dragOffset - L::RulesTrack.y) / range : 0.f;
                scrollTarget = clamp01(t) * ms;
                scroll = scrollTarget;
            } else {
                draggingThumb = false;
            }
        }
        scrollTarget = std::clamp(scrollTarget, 0.f, ms);
        scroll = approach(scroll, scrollTarget, 16.f, dt);
        if (std::fabs(scroll - scrollTarget) < 0.25f) scroll = scrollTarget;
    }

    // ------------------------------------------------------------ confetti
    void spawnConfetti() {
        confetti.clear();
        static const Color cols[] = {pal::Paper, pal::TileFace, Color{255, 255, 250, 255}, pal::Paper,
                                     pal::InkRed, pal::InkBlue, pal::InkYellow, pal::Brass, pal::Oralet};
        for (int i = 0; i < 110; ++i) {
            Confetti c;
            c.p = {rnd(0.f, VW), rnd(-700.f, -10.f)};
            c.v = {rnd(-18.f, 18.f), rnd(55.f, 115.f)};
            c.rot = rnd(0.f, 360.f);
            c.vrot = rnd(-200.f, 200.f);
            c.flip = rnd(0.f, 6.f);
            c.vflip = rnd(3.f, 8.f);
            c.size = rnd(6.f, 11.f);
            c.phase = rnd(0.f, 6.28f);
            c.color = cols[(int)rnd(0.f, 8.999f)];
            confetti.push_back(c);
        }
    }
    void updateConfetti(float dt) {
        const float age = ageOf(ScreenId::MatchOver);
        for (Confetti& c : confetti) {
            if (!c.alive) continue;
            c.p.x += (c.v.x + 26.f * std::sin(time * 1.6f + c.phase)) * dt;
            c.p.y += c.v.y * dt;
            c.rot += c.vrot * dt;
            c.flip += c.vflip * dt;
            if (c.p.y > VH + 20.f) {
                if (age < 7.f) {
                    c.p = {rnd(0.f, VW), rnd(-60.f, -10.f)};
                } else {
                    c.alive = false;
                }
            }
        }
    }
    void drawConfetti() const {
        for (const Confetti& c : confetti) {
            if (!c.alive) continue;
            const float cf = std::cos(c.flip);
            const float w = c.size * std::fabs(cf) + 1.2f, h = c.size * 0.62f;
            const Color col = cf < 0 ? lerpColor(c.color, Color{60, 40, 30, 255}, 0.28f) : c.color;
            DrawRectanglePro({c.p.x, c.p.y, w, h}, {w * 0.5f, h * 0.5f}, c.rot, col);
        }
    }

    // ============================================================ drawing
    void draw(const okey::Game* g) {
        if (!fadeShader.ok) fadeShader.load();
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pressedId = -1;
        freeze(g);
        const bool fading = fade < 1.f && prev != cur;
        const float e = easeInOutCubic(fade);
        if (fading) drawLayer(prev, 1.f - e, kNoMouse, gameFor(prev, g));
        // the incoming screen takes input once it is mostly visible (no click-through on double clicks)
        const Vector2 m = (fading && fade < 0.6f) ? kNoMouse : mouse;
        drawLayer(cur, fading ? e : 1.f, m, g);
    }

    void drawLayer(ScreenId id, float alpha, Vector2 m, const okey::Game* g) {
        if (id == ScreenId::None || alpha <= 0.004f) return;
        if (alpha >= 0.996f) {
            drawScreen(id, m, g);
            return;
        }
        if (!fadeShader.ok) { // no shader: hard cut half way
            if (alpha >= 0.5f) drawScreen(id, m, g);
            return;
        }
        fadeShader.begin(alpha);
        drawScreen(id, m, g);
        fadeShader.end();
    }

    void drawScreen(ScreenId id, Vector2 m, const okey::Game* g) {
        switch (id) {
        case ScreenId::Title: drawTitle(m); break;
        case ScreenId::Settings: drawSettings(m); break;
        case ScreenId::Rules: drawRules(m); break;
        case ScreenId::Paused: drawPaused(m, g); break;
        case ScreenId::HandSummary: drawHandSummary(m, g); break;
        case ScreenId::MatchOver: drawMatchOver(m, g); break;
        case ScreenId::GameSelect: drawGameSelect(m); break;
        case ScreenId::Stats: drawStats(m); break;
        case ScreenId::Guide: drawGuide(m); break;
        case ScreenId::Replays: drawReplays(m); break;
        case ScreenId::Analysis: drawAnalysis(m); break;
        case ScreenId::None: break;
        }
    }

    // Big title in the Sign font with a soft shadow.
    static void drawHeader(const std::string& s, Vector2 c, float size, Color col) {
        drawTextCentered(FontId::Sign, s, {c.x + 3.f, c.y + 4.f}, size, rgba(0, 0, 0, 0.55f), 1.f);
        drawTextCentered(FontId::Sign, s, c, size, col, 1.f);
    }
    static void brassRule(float x0, float x1, float y) {
        DrawLineEx({x0, y}, {x1, y}, 2.f, alphaMul(pal::Brass, 0.55f));
        const float cx = (x0 + x1) * 0.5f;
        tri({cx, y - 6.f}, {cx - 7.f, y}, {cx, y + 6.f}, pal::Brass);
        tri({cx, y - 6.f}, {cx, y + 6.f}, {cx + 7.f, y}, pal::Brass);
    }

    // ------------------------------------------------------------ title
    // The buttons under "Oyna": "Devam Et" first when a match was left unfinished, then the AI, rules and record,
    // settings and quit (two rows share a line when "Devam Et" needs the room).
    struct TitleButton {
        Rectangle r;
        const char* label;
        int id;
        float fs;
    };
    std::vector<TitleButton> titleButtons() const {
        std::vector<std::vector<std::pair<const char*, int>>> rows;
        if (canResume) rows.push_back({{"Devam Et", C_Continue}});
        rows.push_back({{"Yapay Zekayı İzle", C_WatchAi}});
        rows.push_back({{"Kurallar", C_TitleRules}, {"İstatistik", C_TitleStats}});
        if (canResume) rows.push_back({{"Ayarlar", C_TitleSettings}, {"Çıkış", C_Quit}});
        else {
            rows.push_back({{"Ayarlar", C_TitleSettings}});
            rows.push_back({{"Çıkış", C_Quit}});
        }
        std::vector<TitleButton> out;
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = L::TitleRowY + (float)i * L::TitleRowStep;
            const float gap = 8.f, w = rows[i].size() == 1 ? L::TitleRowW : (L::TitleRowW - gap) * 0.5f;
            for (size_t j = 0; j < rows[i].size(); ++j) {
                const char* label = rows[i][j].first;
                const float fs = rows[i].size() > 1 ? 23.f : std::string(label).size() > 12 ? 25.f : 27.f;
                out.push_back({{L::TitleRowX + (float)j * (w + gap), y, w, L::TitleRowH}, label, rows[i][j].second, fs});
            }
        }
        return out;
    }

    void drawTitle(Vector2 m) {
        const float age = ageOf(ScreenId::Title);
        // let the room show through; darken the edges so the sign and the menu read
        DrawRectangleGradientV(0, 0, (int)VW, 470, rgba(10, 6, 3, 0.62f), rgba(10, 6, 3, 0.f));
        DrawRectangleGradientV(0, 660, (int)VW, 240, rgba(10, 6, 3, 0.f), rgba(10, 6, 3, 0.7f));
        DrawRectangleGradientH(0, 0, 300, (int)VH, rgba(10, 6, 3, 0.55f), rgba(10, 6, 3, 0.f));
        DrawRectangleGradientH((int)VW - 300, 0, 300, (int)VH, rgba(10, 6, 3, 0.f), rgba(10, 6, 3, 0.55f));
        rlPushMatrix();
        rlTranslatef(800.f, 612.f, 0.f);
        rlScalef(1.55f, 1.f, 1.f);
        DrawCircleGradient({0, 0}, 250.f, rgba(10, 6, 3, 0.62f), rgba(10, 6, 3, 0.f));
        rlPopMatrix();

        drawSign(age);

        { // the primary button breathes a little warm light
            const float gl = 0.5f + 0.5f * std::sin(time * 2.1f);
            const Rectangle r = L::TitlePlay;
            DrawRectangleRounded({r.x - 10.f, r.y - 9.f, r.width + 20.f, r.height + 20.f}, 0.4f, 12,
                                 alphaMul(pal::Highlight, 0.07f + 0.07f * gl));
            DrawRectangleRounded({r.x - 5.f, r.y - 4.f, r.width + 10.f, r.height + 10.f}, 0.35f, 12,
                                 alphaMul(pal::Highlight, 0.10f + 0.08f * gl));
        }
        if (drawButton(L::TitlePlay, "Oyna", m, true, ButtonStyle::Wood, 34.f)) click(C_Play);
        for (const TitleButton& b : titleButtons())
            if (drawButton(b.r, b.label, m, true, ButtonStyle::Wood, b.fs)) click(b.id);
        if (canResume && !resumeLabel.empty()) {
            const Rectangle r = titleButtons().front().r;
            drawTextCentered(FontId::Ui, resumeLabel, {r.x + r.width * 0.5f, r.y + r.height + 9.f}, 15.f,
                             alphaMul(pal::TextLight, 0.6f));
        }

        drawTextCentered(FontId::Ui, "SaklıBahçe  \xC2\xB7  sürüm 1.1  \xC2\xB7  radyoda Turku (CC BY 4.0) ve 1920'lerin plakları", {800.f, 872.f}, 17.f,
                         alphaMul(pal::TextLight, 0.55f));
    }

    void drawSign(float age) {
        const float settle = std::exp(-age * 1.5f);
        const float ang = -3.2f * settle * std::cos(age * 3.3f) + 0.3f * std::sin(time * 0.7f);
        const Vector2 pivot{800.f, 30.f};
        const float W = 880.f, H = 258.f;
        auto xf = [&](Vector2 p) { return vadd(rotateDeg(p, ang), pivot); };

        drawChain({490.f, -8.f}, xf({-310.f, 10.f}));
        drawChain({1110.f, -8.f}, xf({310.f, 10.f}));

        rlPushMatrix();
        rlTranslatef(pivot.x, pivot.y, 0.f);
        rlRotatef(ang, 0.f, 0.f, 1.f);

        // --- main board: lacquered walnut, gilt frame
        const Rectangle board{-W * 0.5f, 0.f, W, H};
        DrawRectangleRounded({board.x + 10.f, board.y + 16.f, W, H}, 0.07f, 10, rgba(0, 0, 0, 0.45f));
        DrawRectangleRounded(board, 0.07f, 10, Color{40, 20, 10, 255});
        const Rectangle in{board.x + 12.f, board.y + 12.f, W - 24.f, H - 24.f};
        DrawRectangleRounded(in, 0.06f, 10, Color{84, 44, 22, 255});
        for (int i = 0; i < 22; ++i) {
            const float y = in.y + 6.f + (float)i * (in.height - 12.f) / 21.f;
            const float wob = std::sin((float)i * 1.7f) * 4.f;
            DrawLineEx({in.x + 14.f, y + wob}, {in.x + in.width - 14.f, y - wob}, 1.2f,
                       (i % 3 == 0) ? rgba(120, 70, 38, 0.35f) : rgba(40, 18, 8, 0.22f));
        }
        DrawRectangleGradientV((int)in.x + 6, (int)in.y + 4, (int)in.width - 12, (int)(in.height * 0.45f),
                               rgba(255, 196, 120, 0.13f), rgba(255, 196, 120, 0.f));
        const Rectangle gilt{in.x + 10.f, in.y + 10.f, in.width - 20.f, in.height - 20.f};
        DrawRectangleRoundedLinesEx(gilt, 0.05f, 10, 2.5f, alphaMul(kGold, 0.85f));
        DrawRectangleRoundedLinesEx({gilt.x + 6.f, gilt.y + 6.f, gilt.width - 12.f, gilt.height - 12.f}, 0.05f, 10,
                                    1.f, alphaMul(kGold, 0.4f));
        drawRivet({in.x + 4.f, in.y + 4.f}, 6.f);
        drawRivet({in.x + in.width - 4.f, in.y + 4.f}, 6.f);
        drawRivet({in.x + 4.f, in.y + in.height - 4.f}, 6.f);
        drawRivet({in.x + in.width - 4.f, in.y + in.height - 4.f}, 6.f);
        for (float ex : {-310.f, 310.f}) {
            DrawRing({ex, 10.f}, 5.f, 8.5f, 0, 360, 20, Color{70, 56, 34, 255});
            DrawRing({ex, 10.f}, 6.f, 7.5f, 200, 320, 8, pal::Brass);
        }

        // --- "SAKLI BAHÇE", gilt letters
        {
            const std::string word = "SAKLI BAHÇE";
            const float sp = 7.f;
            float size = 108.f;
            const float fitW = measureText(FontId::Sign, word, size, sp).x;
            if (fitW > 720.f) size *= 720.f / fitW;
            const Vector2 ms = measureText(FontId::Sign, word, size, sp);
            const Vector2 pos{-ms.x * 0.5f, 34.f};
            drawText(FontId::Sign, word, {pos.x + 4.f, pos.y + 6.f}, size, rgba(0, 0, 0, 0.55f), sp);
            drawText(FontId::Sign, word, {pos.x, pos.y + 2.5f}, size, Color{112, 70, 22, 255}, sp);
            drawText(FontId::Sign, word, pos, size, kGold, sp);
            // warm top-light on the gilding
            beginClipLocal(pos, ms, ang, pivot);
            drawText(FontId::Sign, word, pos, size, Color{255, 236, 180, 255}, sp);
            EndScissorMode();
        }
        // ornament: — ◆ —
        DrawLineEx({-360.f, 150.f}, {-120.f, 150.f}, 2.f, alphaMul(kGold, 0.7f));
        DrawLineEx({120.f, 150.f}, {360.f, 150.f}, 2.f, alphaMul(kGold, 0.7f));
        // --- "101" neon
        {
            const std::string neon = gameInfo((GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1)).neon;
            float ns = 100.f;
            const float nw = measureText(FontId::Sign, neon, ns, 12.f).x;
            if (nw > 640.f) ns *= 640.f / nw;
            drawNeonText(neon, {0.f, 198.f}, ns, 12.f, neonFlicker(time));
        }

        // --- enamel plaque "Kıraathane" hanging below
        const float py = H + 26.f;
        for (float ex : {-150.f, 150.f}) {
            drawChain({ex, H - 4.f}, {ex, py + 6.f});
        }
        const Rectangle plaque{-215.f, py, 430.f, 86.f};
        DrawRectangleRounded({plaque.x + 6.f, plaque.y + 9.f, plaque.width, plaque.height}, 0.3f, 10,
                             rgba(0, 0, 0, 0.4f));
        DrawRectangleRounded(plaque, 0.3f, 10, Color{232, 222, 196, 255});
        DrawRectangleRoundedLinesEx({plaque.x + 5.f, plaque.y + 5.f, plaque.width - 10.f, plaque.height - 10.f},
                                    0.3f, 10, 2.f, kSignRed);
        // enamel chips at the corners
        DrawCircleV({plaque.x + 14.f, plaque.y + plaque.height - 12.f}, 4.f, rgba(60, 40, 30, 0.55f));
        DrawCircleV({plaque.x + plaque.width - 20.f, plaque.y + 12.f}, 3.f, rgba(60, 40, 30, 0.45f));
        drawTextCentered(FontId::Sign, "Kıraathane", {0.f, py + 32.f}, 36.f, kSignRed, 1.f);
        drawTextCentered(FontId::UiBold, "KURULUŞ 1974  \xC2\xB7  TAVLA  \xC2\xB7  OKEY  \xC2\xB7  ÇAY",
                         {0.f, py + 64.f}, 15.f, Color{80, 60, 48, 255}, 2.f);
        rlPopMatrix();
    }

    // Axis-aligned clip of the upper half of a rotated text box (the rotation is tiny; good enough for a
    // highlight band).
    static void beginClipLocal(Vector2 pos, Vector2 ms, float angDeg, Vector2 pivot) {
        const Vector2 c = vadd(rotateDeg({pos.x + ms.x * 0.5f, pos.y + ms.y * 0.36f}, angDeg), pivot);
        beginClip({c.x - ms.x * 0.5f - 20.f, c.y - ms.y * 0.36f - 10.f, ms.x + 40.f, ms.y * 0.36f + 10.f});
    }

    // ------------------------------------------------------------ game list
    Rectangle gameCardRect(int k) const {
        const int row = k < 4 ? 0 : 1;
        const int n = row == 0 ? 4 : 3;
        const int i = row == 0 ? k : k - 4;
        const float total = (float)n * L::SelCardW + (float)(n - 1) * L::SelGap;
        return {800.f - total * 0.5f + (float)i * (L::SelCardW + L::SelGap), L::SelRowY[row], L::SelCardW, L::SelCardH};
    }

    // The little picture on top of a game's card.
    void drawGameEmblem(GameKind k, Vector2 c) {
        enum { Y = 0, M = 1, K = 2, R = 3 };
        auto tiles = [&](std::vector<MiniTile> ts, float y) {
            const float w = kMiniW * 1.05f, h = kMiniH * 1.05f;
            const float tw = (float)ts.size() * w + (float)(ts.size() - 1) * kMiniGap;
            float x = c.x - tw * 0.5f;
            for (const MiniTile& t : ts) {
                drawMiniTile({x, y - h * 0.5f, w, h}, t);
                x += w + kMiniGap;
            }
        };
        switch (k) {
        case GameKind::Yuzbir: tiles({T(R, 10), T(R, 11), T(R, 12), T(R, 13)}, c.y); break;
        case GameKind::YuzbirEsli: tiles({T(M, 8), T(Y, 8), T(K, 8), T(R, 8)}, c.y); break;
        case GameKind::Okey: tiles({T(K, 5), TOkey(), T(K, 7), TFake()}, c.y); break;
        case GameKind::Tavla:
            DrawCircleV({c.x - 70.f, c.y + 14.f}, 22.f, Color{232, 222, 198, 255});
            DrawRing({c.x - 70.f, c.y + 14.f}, 14.f, 16.f, 0, 360, 24, Color{180, 160, 130, 255});
            DrawCircleV({c.x + 70.f, c.y + 14.f}, 22.f, Color{70, 28, 22, 255});
            DrawRing({c.x + 70.f, c.y + 14.f}, 14.f, 16.f, 0, 360, 24, Color{120, 60, 44, 255});
            drawMiniDie({c.x - 24.f, c.y - 4.f}, 46.f, 6, -12.f);
            drawMiniDie({c.x + 28.f, c.y + 2.f}, 46.f, 5, 9.f);
            break;
        case GameKind::Pisti:
            drawMiniCard({c.x - 34.f, c.y + 4.f}, 52.f, -14.f, "V", 3);
            drawMiniCard({c.x + 34.f, c.y + 4.f}, 52.f, 12.f, "V", 2);
            break;
        case GameKind::Batak:
            drawMiniCard({c.x - 46.f, c.y + 8.f}, 50.f, -16.f, "A", 0);
            drawMiniCard({c.x, c.y}, 50.f, 0.f, "P", 0);
            drawMiniCard({c.x + 46.f, c.y + 8.f}, 50.f, 16.f, "K", 0);
            break;
        case GameKind::King:
            drawMiniCard({c.x - 30.f, c.y + 6.f}, 52.f, -10.f, "K", 1);
            drawMiniCard({c.x + 30.f, c.y + 6.f}, 52.f, 10.f, "P", 1);
            break;
        default: break;
        }
    }

    void drawGameSelect(Vector2 m) {
        const float age = ageOf(ScreenId::GameSelect);
        drawDim(0.6f);
        drawHeader("Ne oynayalım?", {800.f, 84.f}, 58.f, kGold);
        drawTextCentered(FontId::Ui, "Masaya otur, oyunu seç. Kurallar her oyunun kendi sayfasında.", {800.f, 134.f}, 20.f,
                         alphaMul(pal::TextLight, 0.7f));
        const int sel = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
        for (int k = 0; k < (int)GameKind::Count; ++k) {
            const GameKind gk = (GameKind)k;
            const GameInfo& info = gameInfo(gk);
            const bool avail = gameAvailable(gk);
            const float a = clamp01((age - 0.05f * (float)k) / 0.3f);
            Rectangle r = gameCardRect(k);
            r.y += (1.f - easeOutCubic(a)) * 30.f;
            const bool hover = avail && pointInRect(m, r) && a > 0.9f;
            if (hover) {
                r.y -= 4.f;
                requestHandCursor();
            }
            // the board: dark walnut with a brass edge; the last game played glows
            if (k == sel && avail)
                DrawRectangleRounded({r.x - 7.f, r.y - 7.f, r.width + 14.f, r.height + 14.f}, 0.1f, 10,
                                     alphaMul(pal::Highlight, (0.16f + 0.06f * std::sin(time * 2.2f)) * a));
            DrawRectangleRounded({r.x + 5.f, r.y + 9.f, r.width, r.height}, 0.08f, 10, rgba(0, 0, 0, 0.45f * a));
            DrawRectangleRounded(r, 0.08f, 10, alphaMul(Color{58, 32, 18, 255}, a));
            const Rectangle in{r.x + 8.f, r.y + 8.f, r.width - 16.f, r.height - 16.f};
            DrawRectangleRounded(in, 0.07f, 10, alphaMul(hover ? Color{104, 58, 30, 255} : Color{86, 46, 24, 255}, a));
            DrawRectangleRoundedLinesEx(in, 0.07f, 10, hover ? 2.5f : 1.5f, alphaMul(kGold, (hover ? 0.95f : 0.55f) * a));
            drawGameEmblem(gk, {r.x + r.width * 0.5f, r.y + 66.f});
            drawTextCentered(FontId::Sign, info.name, {r.x + r.width * 0.5f + 2.f, r.y + 140.f}, 40.f, rgba(0, 0, 0, 0.5f * a));
            drawTextCentered(FontId::Sign, info.name, {r.x + r.width * 0.5f, r.y + 137.f}, 40.f, alphaMul(kGold, a));
            drawTextCentered(FontId::UiBold, info.players, {r.x + r.width * 0.5f, r.y + 172.f}, 17.f,
                             alphaMul(pal::Brass, 0.9f * a), 2.f);
            drawTextWrapped(FontId::Ui, info.blurb, {r.x + 24.f, r.y + 192.f, r.width - 48.f, 64.f}, 17.f,
                            alphaMul(pal::TextLight, 0.78f * a), 2.f);
            if (!avail) {
                DrawRectangleRounded(in, 0.07f, 10, rgba(20, 10, 6, 0.55f * a));
                const Rectangle stamp{r.x + r.width * 0.5f - 74.f, r.y + 46.f, 148.f, 40.f};
                rlPushMatrix();
                rlTranslatef(stamp.x + stamp.width * 0.5f, stamp.y + stamp.height * 0.5f, 0.f);
                rlRotatef(-8.f, 0.f, 0.f, 1.f);
                DrawRectangleRoundedLinesEx({-stamp.width * 0.5f, -stamp.height * 0.5f, stamp.width, stamp.height}, 0.3f, 8,
                                            2.5f, alphaMul(kMarginRed, 0.9f * a));
                drawTextCentered(FontId::UiBold, "YAKINDA", {0.f, -2.f}, 24.f, alphaMul(kMarginRed, 0.95f * a), 3.f);
                rlPopMatrix();
            }
            if (avail && hit(r, C_GameCard + 1000 + k, m)) click(C_GameCard, k);
        }
        if (drawButton(L::SelBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_SelBack);
        drawText(FontId::Ui, "Kurallar için ana menüdeki Kurallar düğmesi", {L::SelBack.x + L::SelBack.width + 40.f, 806.f},
                 17.f, alphaMul(pal::TextLight, 0.5f));
    }

    // ------------------------------------------------------------ a game's guide card
    void drawGuide(Vector2 m) {
        const float a = clamp01(ageOf(ScreenId::Guide) / 0.3f);
        drawDim(0.45f * a);
        const float W = 860.f;
        float h = 150.f;
        std::vector<float> lineH;
        for (const std::string& l : guideLines) {
            // (measured by a transparent pass far off screen: the same wrap as below)
            lineH.push_back(drawTextWrapped(FontId::Ui, l, {-4000.f, -4000.f, W - 150.f, 400.f}, 22.f, Color{0, 0, 0, 0}, 6.f) + 12.f);
            h += lineH.back();
        }
        h += 90.f;
        const Rectangle P{800.f - W * 0.5f, 450.f - h * 0.5f + (1.f - easeOutCubic(a)) * 24.f, W, h};
        drawPanel(P, PanelStyle::Wood);
        drawHeader(guideTitle, {800.f, P.y + 62.f}, 46.f, alphaMul(kGold, a));
        brassRule(P.x + 60.f, P.x + P.width - 60.f, P.y + 104.f);
        float y = P.y + 130.f;
        for (size_t i = 0; i < guideLines.size(); ++i) {
            const float la = clamp01((ageOf(ScreenId::Guide) - 0.12f - 0.08f * (float)i) / 0.3f);
            DrawCircleV({P.x + 70.f, y + 15.f}, 5.f, alphaMul(pal::Brass, la));
            drawTextWrapped(FontId::Ui, guideLines[i], {P.x + 92.f, y, W - 150.f, lineH[i]}, 22.f,
                            alphaMul(pal::TextLight, 0.92f * la), 6.f);
            y += lineH[i];
        }
        const Rectangle ok{800.f - 120.f, P.y + P.height - 82.f, 240.f, 58.f};
        if (drawButton(ok, "Anladım", m, true, ButtonStyle::Wood, 28.f)) click(C_GuideOk);
        drawText(FontId::Ui, "Kurallar: Menü > Kurallar", {P.x + 40.f, P.y + P.height - 46.f}, 15.f,
                 alphaMul(pal::TextLight, 0.45f * a));
    }

    // ------------------------------------------------------------ stats (kahvehane defteri)
    void drawStats(Vector2 m) {
        const float age = ageOf(ScreenId::Stats);
        static const StatsBook kEmpty;
        const StatsBook& book = stats ? *stats : kEmpty;
        drawDim(0.6f);
        const Rectangle P = L::StatsPanel;
        drawPanel(P, PanelStyle::Wood);
        drawHeader("Kahvehane Defteri", {800.f, 94.f}, 54.f, kGold);
        brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);

        // --- rank: the title the regulars give you, and how far the next one is
        const int pts = book.rankPoints();
        const Rank& rank = StatsBook::rankFor(pts);
        const Rank* next = StatsBook::nextRank(pts);
        const float a = clamp01(age / 0.35f);
        drawText(FontId::UiBold, "RÜTBEN", {P.x + 70.f, 162.f}, 17.f, alphaMul(pal::Brass, 0.9f * a), 4.f);
        drawTextShadow(FontId::Sign, rank.name, {P.x + 70.f, 184.f}, 46.f, alphaMul(kGold, a));
        drawText(FontId::Ui, rank.line, {P.x + 72.f, 238.f}, 19.f, alphaMul(pal::TextLight, 0.72f * a));
        {
            const float bx = P.x + 640.f, by = 196.f, bw = 380.f, bh = 16.f;
            const int lo = rank.points, hi = next ? next->points : rank.points;
            const float f = next ? clamp01((float)(pts - lo) / (float)std::max(1, hi - lo)) : 1.f;
            DrawRectangleRounded({bx, by, bw, bh}, 1.f, 8, rgba(20, 10, 6, 0.8f));
            if (f > 0.f) DrawRectangleRounded({bx, by, std::max(bh, bw * f * easeOutCubic(a)), bh}, 1.f, 8, pal::Brass);
            DrawRectangleRoundedLinesEx({bx, by, bw, bh}, 1.f, 8, 1.2f, alphaMul(kGold, 0.6f));
            const std::string ptsLine = std::to_string(pts) + " puan";
            drawText(FontId::UiBold, ptsLine, {bx, by - 30.f}, 20.f, alphaMul(pal::TextLight, a));
            const std::string nextLine = next ? std::string(next->name) + " için " + std::to_string(next->points - pts) +
                                                    " puan daha"
                                              : std::string("En yüksek rütbe!");
            drawText(FontId::Ui, nextLine, {bx + bw - measureText(FontId::Ui, nextLine, 17.f).x, by - 27.f}, 17.f,
                     alphaMul(pal::TextLight, 0.7f * a));
            drawText(FontId::Ui, "Maç galibiyeti: Acemi'ye karşı 1, Usta'ya 2, Kurt'a 3 puan", {bx, by + 26.f}, 15.f,
                     alphaMul(pal::TextLight, 0.5f * a));
        }

        // --- the table: one line per game, the total last
        const float x0 = P.x + 60.f, top = 296.f, rowH = 46.f;
        struct Col {
            const char* title;
            float x;    // right edge of the numbers (the game name: left edge)
        };
        const Col cols[] = {{"Oyun", x0 + 10.f},        {"Maç", x0 + 300.f},        {"Galibiyet", x0 + 420.f},
                            {"Oran", x0 + 520.f},       {"El / Oyun", x0 + 660.f},  {"En uzun seri", x0 + 800.f},
                            {"Rekor", x0 + 970.f}};
        for (size_t c = 0; c < sizeof cols / sizeof cols[0]; ++c) {
            const float w = measureText(FontId::UiBold, cols[c].title, 16.f, 2.f).x;
            drawText(FontId::UiBold, cols[c].title, {c == 0 ? cols[c].x : cols[c].x - w, top}, 16.f,
                     alphaMul(pal::Brass, 0.9f), 2.f);
        }
        DrawLineEx({x0, top + 28.f}, {x0 + 980.f, top + 28.f}, 1.2f, alphaMul(pal::Brass, 0.45f));
        auto num = [&](float right, float y, const std::string& t, Color c, FontId f = FontId::Ui) {
            drawText(f, t, {right - measureText(f, t, 21.f).x, y}, 21.f, c);
        };
        auto pct = [](int a, int b) { return b > 0 ? std::to_string((a * 100 + b / 2) / b) + "%" : std::string("-"); };
        const int sel = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
        for (int k = 0; k <= STATS_GAMES; ++k) {
            const bool totalRow = k == STATS_GAMES;
            const GameRecord r = totalRow ? book.total() : book.games[(size_t)k];
            const float ra = clamp01((age - 0.04f * (float)k) / 0.3f);
            const float y = top + 40.f + (float)k * rowH + (totalRow ? 10.f : 0.f) + (1.f - easeOutCubic(ra)) * 14.f;
            if (totalRow) DrawLineEx({x0, y - 8.f}, {x0 + 980.f, y - 8.f}, 1.2f, alphaMul(pal::Brass, 0.45f * ra));
            else if (k % 2 == 0)
                DrawRectangleRounded({x0 - 10.f, y - 8.f, 1000.f, rowH - 4.f}, 0.3f, 6, rgba(255, 220, 160, 0.04f * ra));
            const bool none = r.matches == 0 && r.hands == 0;
            const Color tc = alphaMul(none ? alphaMul(pal::TextLight, 0.45f) : pal::TextLight, ra);
            const std::string name = totalRow ? "Toplam" : gameInfo((GameKind)k).name;
            drawText(FontId::UiBold, name, {cols[0].x, y}, 22.f, alphaMul(totalRow || k == sel ? kGold : pal::TextLight, ra));
            if (none && !totalRow) {
                drawText(FontId::Ui, "henüz oynanmadı", {cols[1].x - 60.f, y + 2.f}, 18.f, alphaMul(pal::TextLight, 0.4f * ra));
                continue;
            }
            num(cols[1].x, y, std::to_string(r.matches), tc);
            num(cols[2].x, y, std::to_string(r.wins), tc, FontId::UiBold);
            num(cols[3].x, y, pct(r.wins, r.matches), tc);
            num(cols[4].x, y, std::to_string(r.handWins) + " / " + std::to_string(r.hands), tc);
            num(cols[5].x, y, std::to_string(r.bestStreak) + (r.streak > 1 ? "  (şimdi " + std::to_string(r.streak) + ")" : ""), tc);
            if (!totalRow && r.hasBest && StatsBook::bestLabel(k)) {
                const std::string b = std::to_string(r.best);
                num(cols[6].x, y, b, alphaMul(kGold, ra), FontId::UiBold);
                const char* bl = StatsBook::bestLabel(k);
                drawText(FontId::Ui, bl, {cols[6].x - measureText(FontId::Ui, bl, 13.f).x, y + 24.f}, 13.f,
                         alphaMul(pal::TextLight, 0.45f * ra));
            } else if (!totalRow) {
                num(cols[6].x, y, "-", alphaMul(pal::TextLight, 0.4f * ra));
            } else {
                const std::string lv = "Acemi " + std::to_string(r.winsAt[0]) + "/" + std::to_string(r.playedAt[0]) +
                                       "   Usta " + std::to_string(r.winsAt[1]) + "/" + std::to_string(r.playedAt[1]) +
                                       "   Kurt " + std::to_string(r.winsAt[2]) + "/" + std::to_string(r.playedAt[2]);
                drawText(FontId::Ui, lv, {x0 + 10.f, y + 34.f}, 17.f, alphaMul(pal::TextLight, 0.65f * ra));
            }
        }
        drawTextCentered(FontId::Ui, "Yalnızca kendin oynadığın, sonuna kadar biten maçlar deftere yazılır.",
                         {800.f, 752.f}, 16.f, alphaMul(pal::TextLight, 0.5f));
        if (drawButton({L::StatsBack.x - 130.f, L::StatsBack.y, L::StatsBack.width, L::StatsBack.height}, "Tekrarlar", m,
                       true, ButtonStyle::Wood, 26.f))
            click(C_StatsReplays);
        if (drawButton({L::StatsBack.x + 130.f, L::StatsBack.y, L::StatsBack.width, L::StatsBack.height}, "Geri", m, true,
                       ButtonStyle::Wood, 28.f))
            click(C_StatsBack);
    }

    // ------------------------------------------------------------ hatalarım: the analysis of a match
    void drawAnalysis(Vector2 m) {
        const float age = ageOf(ScreenId::Analysis);
        drawDim(0.62f);
        const Rectangle P = L::StatsPanel;
        drawPanel(P, PanelStyle::Wood);
        drawHeader("Hatalarım", {800.f, 94.f}, 54.f, kGold);
        brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);
        drawTextCentered(FontId::Ui, analysisTitle, {800.f, 160.f}, 18.f, alphaMul(pal::TextLight, 0.6f));
        if (!analysisReady) {
            const std::string dots(1 + (int)(time * 2.f) % 3, '.');
            drawTextCentered(FontId::UiBold, "Kurt maçı inceliyor" + dots, {800.f, 420.f}, 28.f, pal::TextLight);
            drawTextCentered(FontId::Ui, "Her hamlende onun yerinde ne yapacağına bakıyor.", {800.f, 462.f}, 18.f,
                             alphaMul(pal::TextLight, 0.6f));
        } else if (analysis.empty()) {
            drawTextCentered(FontId::UiBold, "Kayda değer bir hata yok!", {800.f, 400.f}, 30.f, kGold);
            drawTextCentered(FontId::Ui, "Kurt da bu maçı senin gibi oynardı. Helal olsun.", {800.f, 446.f}, 19.f,
                             alphaMul(pal::TextLight, 0.7f));
        } else {
            for (size_t i = 0; i < analysis.size() && i < 3; ++i) {
                const MistakeView& e = analysis[i];
                const float a = clamp01((age - 0.1f * (float)i) / 0.35f);
                const Rectangle c{P.x + 70.f, 196.f + (float)i * 182.f + (1.f - easeOutCubic(a)) * 16.f, P.width - 140.f, 166.f};
                DrawRectangleRounded(c, 0.08f, 8, rgba(20, 10, 6, 0.45f * a));
                DrawRectangleRoundedLinesEx(c, 0.08f, 8, 1.2f, alphaMul(pal::Brass, 0.5f * a));
                drawText(FontId::Sign, std::to_string(i + 1) + ".", {c.x + 22.f, c.y + 16.f}, 44.f, alphaMul(kGold, a));
                drawText(FontId::UiBold, e.when, {c.x + 80.f, c.y + 16.f}, 18.f, alphaMul(pal::Brass, a), 2.f);
                drawText(FontId::UiBold, e.played, {c.x + 80.f, c.y + 46.f}, 24.f, alphaMul(pal::TextLight, a));
                drawText(FontId::Ui, e.better, {c.x + 80.f, c.y + 82.f}, 21.f, alphaMul(pal::Good, a));
                if (!e.why.empty())
                    drawTextWrapped(FontId::Ui, e.why, {c.x + 80.f, c.y + 114.f, c.width - 320.f, 48.f}, 17.f,
                                    alphaMul(pal::TextLight, 0.65f * a), 2.f);
                if (!e.cost.empty()) {
                    const float w = measureText(FontId::UiBold, e.cost, 22.f).x + 30.f;
                    const Rectangle b{c.x + c.width - w - 24.f, c.y + 20.f, w, 40.f};
                    DrawRectangleRounded(b, 0.5f, 8, alphaMul(kMarginRed, 0.85f * a));
                    drawTextCentered(FontId::UiBold, e.cost, {b.x + b.width * 0.5f, b.y + b.height * 0.5f - 1.f}, 22.f,
                                     alphaMul(Color{255, 246, 230, 255}, a));
                }
            }
        }
        if (drawButton(L::StatsBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_AnalysisBack);
    }

    // ------------------------------------------------------------ tekrarlar: the last finished matches
    void drawReplays(Vector2 m) {
        const float age = ageOf(ScreenId::Replays);
        drawDim(0.6f);
        const Rectangle P = L::StatsPanel;
        drawPanel(P, PanelStyle::Wood);
        drawHeader("Maç Tekrarları", {800.f, 94.f}, 54.f, kGold);
        brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);
        if (replays.empty()) {
            drawTextCentered(FontId::Ui, "Henüz biten bir maç yok. Bir maçı sonuna kadar oyna, burada izleyebilirsin.",
                             {800.f, 420.f}, 21.f, alphaMul(pal::TextLight, 0.7f));
        }
        const float x0 = P.x + 70.f, top = 168.f, rowH = 58.f;
        const int shown = std::min<int>((int)replays.size(), 10);
        for (int i = 0; i < shown; ++i) {
            const ReplayEntry& e = replays[(size_t)i];
            const float a = clamp01((age - 0.03f * (float)i) / 0.3f);
            const float y = top + (float)i * rowH + (1.f - easeOutCubic(a)) * 12.f;
            if (i % 2 == 0) DrawRectangleRounded({x0 - 14.f, y - 6.f, P.width - 112.f, rowH - 6.f}, 0.3f, 6, rgba(255, 220, 160, 0.04f * a));
            drawText(FontId::UiBold, e.game, {x0, y + 6.f}, 24.f, alphaMul(kGold, a));
            drawText(FontId::Ui, e.date, {x0 + 200.f, y + 9.f}, 19.f, alphaMul(pal::TextLight, 0.7f * a));
            const bool won = e.result == "Kazandın";
            drawText(FontId::UiBold, e.result, {x0 + 420.f, y + 8.f}, 21.f, alphaMul(won ? pal::Good : pal::TextLight, a));
            drawText(FontId::Ui, std::to_string(e.actions) + " hamle", {x0 + 690.f, y + 10.f}, 17.f, alphaMul(pal::TextLight, 0.5f * a));
            const Rectangle b{P.x + P.width - 210.f, y, 130.f, 42.f};
            if (drawButton(b, "İzle", m, true, ButtonStyle::Wood, 22.f)) click(C_ReplayWatch, i);
            const Rectangle an{b.x - 140.f, y, 130.f, 42.f};
            if (drawButton(an, "Analiz", m, true, ButtonStyle::Wood, 22.f)) click(C_ReplayAnalyze, i);
        }
        drawTextCentered(FontId::Ui, "İzlerken: Boşluk durdurur, ok tuşları hızı değiştirir, ESC menüyü açar.", {800.f, 752.f},
                         16.f, alphaMul(pal::TextLight, 0.5f));
        if (drawButton(L::StatsBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_ReplaysBack);
    }

    // ------------------------------------------------------------ settings
    void settingRow(float cy, const char* label, const std::string& hint) {
        if (hint.empty()) {
            drawTextShadow(FontId::UiBold, label, {L::SetLabelX, cy - 15.f}, 27.f, pal::TextLight);
        } else {
            drawTextShadow(FontId::UiBold, label, {L::SetLabelX, cy - 26.f}, 27.f, pal::TextLight);
            drawText(FontId::Ui, hint, {L::SetLabelX, cy + 5.f}, 17.f, alphaMul(pal::TextLight, 0.62f));
        }
    }

    static void drawSelectedChip(Rectangle r, const std::string& label, float fs) {
        DrawRectangleRounded({r.x - 4.f, r.y - 4.f, r.width + 8.f, r.height + 8.f}, 0.3f, 8,
                             alphaMul(pal::Highlight, 0.16f));
        DrawRectangleRounded({r.x + 2.f, r.y + 4.f, r.width, r.height}, 0.25f, 8, rgba(0, 0, 0, 0.43f));
        DrawRectangleRounded(r, 0.25f, 8, Color{150, 108, 44, 255});
        const Rectangle inner{r.x + 2.f, r.y + 2.f, r.width - 4.f, r.height - 6.f};
        DrawRectangleRounded(inner, 0.25f, 8, pal::Brass);
        DrawLineEx({inner.x + 8.f, inner.y + 3.f}, {inner.x + inner.width - 8.f, inner.y + 3.f}, 2.f,
                   rgba(255, 244, 200, 0.55f));
        DrawRectangleRoundedLinesEx(inner, 0.25f, 8, 1.5f, alphaMul(pal::Highlight, 0.9f));
        drawTextCentered(FontId::UiBold, label, {r.x + r.width * 0.5f, r.y + r.height * 0.5f - 2.f}, fs,
                         pal::WoodDark, 0.5f);
    }

    void chipRow(int id, const std::vector<std::string>& labels, int selected, float cy, float w, float gap,
                 Vector2 m, float fs) {
        const float h = 46.f;
        for (int i = 0; i < (int)labels.size(); ++i) {
            const Rectangle r{L::SetCtrlX + (float)i * (w + gap), cy - h * 0.5f, w, h};
            if (i == selected) drawSelectedChip(r, labels[(size_t)i], fs);
            else if (drawButton(r, labels[(size_t)i], m, true, ButtonStyle::Wood, fs)) click(id, i);
        }
    }

    void toggle(int id, int animIdx, bool on, float cy, Vector2 m) {
        const float a = toggleAnim[animIdx];
        const Rectangle track{L::SetCtrlX, cy - 18.f, 78.f, 36.f};
        const Rectangle area{L::SetCtrlX - 6.f, cy - 24.f, 230.f, 48.f};
        const bool hover = pointInRect(m, area);
        DrawRectangleRounded({track.x, track.y + 2.f, track.width, track.height}, 1.f, 16, rgba(0, 0, 0, 0.4f));
        DrawRectangleRounded(track, 1.f, 16, lerpColor(Color{44, 24, 12, 255}, Color{54, 128, 78, 255}, a));
        DrawRectangleRoundedLinesEx(track, 1.f, 16, 1.5f, alphaMul(pal::Brass, hover ? 0.9f : 0.5f));
        const Vector2 k{track.x + 18.f + a * (track.width - 36.f), cy};
        DrawCircleV({k.x + 1.f, k.y + 3.f}, 14.f, rgba(0, 0, 0, 0.4f));
        DrawCircleV(k, 14.f, hover ? pal::Highlight : pal::Brass);
        DrawCircleV({k.x - 4.f, k.y - 4.f}, 5.f, rgba(255, 250, 230, 0.5f));
        drawText(FontId::UiBold, on ? "Açık" : "Kapalı", {track.x + track.width + 16.f, cy - 13.f}, 24.f,
                 on ? pal::Highlight : alphaMul(pal::TextLight, 0.6f));
        if (hit(area, id, m)) click(id);
    }

    int selectedOkeyStart() const {
        int best = 0;
        for (int i = 0; i < 3; ++i)
            if (std::abs(kOkeyStartChoices[i] - settings.okeyStart) < std::abs(kOkeyStartChoices[best] - settings.okeyStart))
                best = i;
        return best;
    }

    void drawSettings(Vector2 m) {
        drawDim(0.55f);
        drawPanel(L::SetPanel, PanelStyle::Wood);
        drawHeader("Ayarlar", {800.f, 94.f}, 54.f, kGold);
        brassRule(L::SetPanel.x + 50.f, L::SetPanel.x + L::SetPanel.width - 50.f, 136.f);

        // two pages: the game (player, level, the game's rules) | looks and sound
        {
            const char* tabs[2] = {"Oyun", "Görünüm \xC2\xB7 Ses"};
            const float tw[2] = {100.f, 170.f};
            float tx = L::SetPanel.x + L::SetPanel.width - 55.f - (tw[0] + tw[1] + 8.f); // top right, clear of the title
            for (int i = 0; i < 2; ++i) {
                const Rectangle r{tx, 70.f, tw[i], 44.f};
                if (i == settingsPage) drawSelectedChip(r, tabs[i], 21.f);
                else if (drawButton(r, tabs[i], m, true, ButtonStyle::Wood, 21.f)) click(C_SetPage, i);
                tx += tw[i] + 8.f;
            }
        }

        // a running list: section headings and rows, the game's own rules in the middle
        float y = L::SetTopY;
        auto section = [&](const std::string& s) {
            drawText(FontId::UiBold, s, {L::SetLabelX, y}, 17.f, alphaMul(pal::Brass, 0.9f), 4.f);
            const float w = measureText(FontId::UiBold, s, 17.f, 4.f).x;
            DrawLineEx({L::SetLabelX + w + 14.f, y + 10.f}, {L::SetPanel.x + L::SetPanel.width - 55.f, y + 10.f}, 1.f,
                       alphaMul(pal::Brass, 0.3f));
            y += L::SetHeadH;
        };
        auto row = [&]() {
            const float cy = y + 16.f;
            y += L::SetRowH;
            return cy;
        };
        if (settingsPage == 1) {
            drawSettingsLooks(m, section, row);
        } else {
        section("OYUN");

        // player name
        {
            const float cy = row();
            settingRow(cy, "Oyuncu adı", "En fazla 12 harf");
            const Rectangle f = L::SetName;
            const bool hover = pointInRect(m, f);
            DrawRectangleRounded({f.x, f.y + 2.f, f.width, f.height}, 0.2f, 8, rgba(0, 0, 0, 0.35f));
            DrawRectangleRounded(f, 0.2f, 8, Color{34, 20, 12, 240});
            DrawRectangleRoundedLinesEx(f, 0.2f, 8, nameEditing ? 2.5f : 1.5f,
                                        nameEditing ? pal::Highlight : alphaMul(pal::Brass, hover ? 0.95f : 0.55f));
            const std::string& shown = nameEditing ? nameBuf : settings.playerName;
            const float tw = measureText(FontId::UiBold, shown, 27.f).x;
            drawText(FontId::UiBold, shown, {f.x + 16.f, f.y + 9.f}, 27.f, pal::TextLight);
            if (nameEditing) {
                if (std::fmod(time, 1.f) < 0.55f)
                    DrawRectangleRec({f.x + 18.f + tw, f.y + 10.f, 2.5f, 27.f}, pal::Highlight);
                const std::string cnt = std::to_string(utf8Count(nameBuf)) + "/12";
                drawText(FontId::Ui, cnt, {f.x + f.width - 16.f - measureText(FontId::Ui, cnt, 16.f).x, f.y + 15.f},
                         16.f, alphaMul(pal::TextLight, 0.5f));
            } else {
                drawText(FontId::Ui, "düzenle", {f.x + f.width + 14.f, f.y + 13.f}, 17.f,
                         alphaMul(pal::TextLight, hover ? 0.8f : 0.4f));
            }
            if (hit(f, C_Name, m)) click(C_Name);
        }

        const int lvl = std::clamp(settings.difficulty, 0, 2);
        float cy = row();
        settingRow(cy, "Rakip seviyesi", kLevelHints[lvl]);
        chipRow(C_Level, {kLevelNames[0], kLevelNames[1], kLevelNames[2]}, lvl, cy, 146.f, 12.f, m, 24.f);

        cy = row();
        settingRow(cy, "Animasyon hızı", "Taşların ve rakiplerin hızı");
        chipRow(C_Anim, {"Yavaş", "Normal", "Hızlı"}, selectedAnim(), cy, 146.f, 12.f, m, 24.f);

        cy = row();
        settingRow(cy, "İpuçları", "Oynanabilir hamleler ve İpucu düğmesi");
        toggle(C_Hints, 3, settings.hints, cy, m);

        cy = row();
        settingRow(cy, "Oyun rehberi", "Her oyunun ilk maçında kısa anlatım");
        toggle(C_Guide, 7, settings.guide, cy, m);

        // the selected game's own rules
        const GameKind game = (GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1);
        std::string upper = gameInfo(game).neon;
        section(upper + " KURALLARI");
        switch (game) {
        case GameKind::Yuzbir:
        case GameKind::YuzbirEsli:
            cy = row();
            settingRow(cy, "El sayısı", "Maç kaç el sürsün?");
            chipRow(C_Hands, {"1", "3", "5", "7", "9", "11"}, selectedHands(), cy, 66.f, 12.f, m, 25.f);
            cy = row();
            settingRow(cy, "Katlamalı oyun", "Her açan, öncekinden en az 1 fazlasıyla açar");
            toggle(C_Katlamali, 4, settings.katlamali, cy, m);
            cy = row();
            settingRow(cy, "Yandan açma cezası", "Atana taşın sayısı \xC3\x97" "10, çiftte \xC3\x97" "20");
            toggle(C_YandanCeza, 5, settings.yandanCeza, cy, m);
            break;
        case GameKind::Okey:
            cy = row();
            settingRow(cy, "Başlangıç puanı", "Herkes bununla başlar, sıfıra inen oyunu bitirir");
            chipRow(C_OkeyStart, {"6", "12", "20"}, selectedOkeyStart(), cy, 66.f, 12.f, m, 25.f);
            cy = row();
            settingRow(cy, "Renkli okey", "Kırmızı, siyah göstergede puan iki kat");
            toggle(C_OkeyRenkli, 10, settings.okeyRenkli, cy, m);
            break;
        case GameKind::Tavla:
            cy = row();
            settingRow(cy, "Maç", "Kaç sayıya oynansın? (mars iki sayı)");
            chipRow(C_TavlaPoints, {"3", "5", "7"}, closest(kTavlaChoices, settings.tavlaPoints), cy, 66.f, 12.f, m, 25.f);
            cy = row();
            settingRow(cy, "Katlama zarı", "Oyunun değerini ikiye katlayabilirsin");
            toggle(C_TavlaDoubling, 8, settings.tavlaDoubling, cy, m);
            cy = row();
            settingRow(cy, "Katmerli mars", "Kırık pulu ya da evinde pulu kalan marsa 3 sayı");
            toggle(C_TavlaKatmerli, 9, settings.tavlaKatmerli, cy, m);
            break;
        case GameKind::Batak:
            cy = row();
            settingRow(cy, "Eşli batak", "Karşındaki ortağın, puanlar takıma");
            toggle(C_BatakEsli, 6, settings.batakEsli, cy, m);
            cy = row();
            settingRow(cy, "Oyun sonu", "Bu puana ilk ulaşan kazanır");
            chipRow(C_BatakTarget, {"31", "51", "71"}, closest(kBatakTargets, settings.batakTarget), cy, 66.f, 12.f, m, 25.f);
            cy = row();
            settingRow(cy, "Önce koz açılmalı", "Koz çakılmadan ele kozla başlanmaz");
            toggle(C_BatakKoz, 11, settings.batakKozKirilmadan, cy, m);
            break;
        case GameKind::Pisti:
            cy = row();
            settingRow(cy, "Masa", "Dört kişi tek tek, eşli ya da sen ve Kel Mahmut");
            chipRow(C_PistiMode, {"4 kişi", "Eşli", "2 kişi"}, std::clamp(settings.pistiMode, 0, 2), cy, 108.f, 12.f, m, 22.f);
            cy = row();
            settingRow(cy, "Oyun sonu", "Bu puana ilk ulaşan kazanır");
            chipRow(C_PistiTarget, {"101", "151"}, closest(kPistiTargets, settings.pistiTarget), cy, 78.f, 12.f, m, 25.f);
            break;
        case GameKind::King:
            cy = row();
            settingRow(cy, "Kısa King (12 el)", settings.king12 ? "Herkes 1 koz, 2 ceza seçer"
                                                                : "Kapalıyken 20 el: herkes 2 koz, 3 ceza");
            toggle(C_King12, 12, settings.king12, cy, m);
            break;
        default: break;
        }

        }

        if (backSettings != ScreenId::Title) {
            drawTextWrapped(FontId::Ui, "Oyunun kuralları yeni maçta geçerli olur.", {640.f, 778.f, 330.f, 60.f},
                            17.f, alphaMul(pal::TextLight, 0.6f), 2.f);
        }
        if (drawButton(L::SetDefaults, "Varsayılanlar", m, true, ButtonStyle::Wood, 22.f)) click(C_Defaults);
        if (drawButton(L::SetBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_Back);
    }

    // The second page of the settings: the time of day and season of the room, colour-blind / big text, sounds.
    template <class Section, class Row>
    void drawSettingsLooks(Vector2 m, Section& section, Row& row) {
        section("GÖRÜNÜM");
        float cy = row();
        settingRow(cy, "Vakit", "Otomatik: bilgisayarın saatine göre");
        chipRow(C_DayTime, {"Otomatik", "Sabah", "Öğle", "Akşam", "Gece"}, std::clamp(settings.dayTime, 0, 4), cy, 96.f,
                8.f, m, 19.f);
        cy = row();
        settingRow(cy, "Mevsim", "Kışın soba yanar, yazın kapı açık");
        chipRow(C_Season, {"Otomatik", "İlkbahar", "Yaz", "Sonbahar", "Kış"}, std::clamp(settings.season, 0, 4), cy, 96.f,
                8.f, m, 19.f);
        cy = row();
        settingRow(cy, "Renk körü modu", "Taşlarda şekil işareti, dört renkli deste");
        toggle(C_ColorBlind, 14, settings.colorBlind, cy, m);
        cy = row();
        settingRow(cy, "Büyük yazı", "Masadaki yazılar daha büyük");
        toggle(C_BigText, 15, settings.bigText, cy, m);

        section("SES");
        cy = row();
        settingRow(cy, "Sesler", "Efektler, kahvehanenin uğultusu ve radyo");
        {
            // three switches as chips: lit = on
            const char* labels[3] = {"Efekt", "Ortam", "Radyo"};
            const bool ons[3] = {settings.sfx, settings.ambient, settings.music};
            const int ids[3] = {C_Sfx, C_Ambient, C_Music};
            const float w = 108.f, gap = 12.f, h = 46.f;
            for (int i = 0; i < 3; ++i) {
                const Rectangle r{L::SetCtrlX + (float)i * (w + gap), cy - h * 0.5f, w, h};
                if (ons[i]) {
                    drawSelectedChip(r, labels[i], 22.f);
                    if (hit(r, ids[i] + 3000, m)) click(ids[i]);
                } else if (drawButton(r, labels[i], m, true, ButtonStyle::Wood, 22.f)) {
                    click(ids[i]);
                }
            }
        }
        cy = row();
        settingRow(cy, "Konuşma sesleri", "Rakipler konuşurken mırıldanır");
        toggle(C_Voices, 13, settings.voices, cy, m);
    }

    // ------------------------------------------------------------ rules
    void drawRules(Vector2 m) {
        ensureRulesLayout();
        drawDim(0.6f);
        drawPanel(L::RulesPanel, PanelStyle::Wood);
        drawHeader(std::string(gameInfo((GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1)).name) +
                       " Nasıl Oynanır?",
                   {800.f, 56.f}, 46.f, kGold);
        drawRuleTabs(m);
        const Rectangle S = L::RulesSheet;
        drawPanel(S, PanelStyle::Paper);

        const Rectangle V = L::RulesView;
        beginClip({S.x + 2.f, S.y + 2.f, S.width - 4.f, S.height - 4.f});
        for (const RuleBlock& b : rules) {
            const float top = V.y + b.y - scroll;
            if (top + b.h < S.y - 20.f || top > S.y + S.height + 20.f) continue;
            drawRuleBlock(b, V.x, top);
        }
        EndScissorMode();
        // soft fade at the sheet's top and bottom edges
        const Color paper0 = alphaMul(pal::Paper, 0.f);
        DrawRectangleGradientV((int)S.x + 2, (int)S.y + 1, (int)S.width - 34, 22, pal::Paper, paper0);
        DrawRectangleGradientV((int)S.x + 2, (int)(S.y + S.height - 23.f), (int)S.width - 34, 22, paper0, pal::Paper);

        // scrollbar
        if (maxScroll() > 0.f) {
            const Rectangle tr = L::RulesTrack;
            DrawRectangleRounded(tr, 1.f, 8, rgba(120, 100, 80, 0.22f));
            const Rectangle th = thumbRect();
            const bool hover = pointInRect(m, th) || draggingThumb;
            if (hover) requestHandCursor();
            DrawRectangleRounded(th, 1.f, 8, hover ? pal::Wood : alphaMul(pal::WoodLight, 0.85f));
            DrawRectangleRoundedLinesEx(th, 1.f, 8, 1.f, alphaMul(pal::WoodDark, 0.6f));
        }
        drawText(FontId::Ui, "Fare tekerleği, ok tuşları ya da kaydırma çubuğu", {L::RulesPanel.x + 40.f, 826.f},
                 16.f, alphaMul(pal::TextLight, 0.5f));
        if (drawButton(L::RulesBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_Back);
    }

    int activeRuleTab() const {
        if (maxScroll() > 0.f && scroll >= maxScroll() - 1.f) return (int)tabs.size() - 1;
        int a = 0;
        for (int t = 0; t < (int)tabs.size(); ++t)
            if (tabY[(size_t)t] <= scroll + 60.f) a = t;
        return a;
    }

    void drawRuleTabs(Vector2 m) {
        const float fs = 19.f, h = 32.f, gap = 8.f, pad = 28.f;
        float total = 0.f;
        std::vector<float> w(tabs.size());
        for (size_t t = 0; t < tabs.size(); ++t) {
            w[t] = measureText(FontId::Chalk, tabs[t].label, fs).x + pad;
            total += w[t] + (t ? gap : 0.f);
        }
        float x = 800.f - total * 0.5f;
        const int active = activeRuleTab();
        for (int t = 0; t < (int)tabs.size(); ++t) {
            const Rectangle r{x, L::RulesTabsY - h * 0.5f, w[t], h};
            if (t == active) {
                DrawRectangleRounded({r.x - 2.f, r.y - 2.f, r.width + 4.f, r.height + 4.f}, 0.35f, 8,
                                     alphaMul(pal::Highlight, 0.85f));
            }
            if (drawButton(r, tabs[(size_t)t].label, m, true, ButtonStyle::Chalk, fs)) click(C_RulesTab, t);
            x += w[(size_t)t] + gap;
        }
    }

    static void drawRich(const RichText& rt, float x, float y, float size) {
        for (const Piece& p : rt.pieces) drawText(p.font, p.text, {x + p.pos.x, y + p.pos.y}, size, p.color);
    }

    void drawRuleBlock(const RuleBlock& b, float x, float y) const {
        switch (b.kind) {
        case RuleBlock::Heading: {
            const float fs = 31.f;
            const Vector2 ms = measureText(FontId::Sign, b.text, fs);
            DrawRectangleRec({x - 18.f, y + 11.f, 6.f, 20.f}, kSignRed);
            drawText(FontId::Sign, b.text, {x, y + 4.f}, fs, kSignRed);
            DrawLineEx({x + ms.x + 16.f, y + 24.f}, {x + L::RulesView.width, y + 24.f}, 1.5f, rgba(150, 100, 60, 0.35f));
            break;
        }
        case RuleBlock::Para:
            drawRich(b.rich, x, y, 21.f);
            break;
        case RuleBlock::Bullet:
            if (b.label.empty()) {
                DrawCircleV({x + 14.f, y + 14.f}, 4.f, kSignRed);
            } else {
                drawText(FontId::UiBold, b.label, {x + 4.f, y + 2.f}, 21.f, kSignRed);
            }
            drawRich(b.rich, x + 34.f, y, 21.f);
            break;
        case RuleBlock::Tiles:
            for (const RuleItem& it : b.items) {
                const float n = (float)it.tiles.size();
                const float tilesW = n * kMiniW + (n - 1.f) * kMiniGap;
                float tx = x + it.box.x + (it.box.width - tilesW) * 0.5f;
                const float ty = y + it.box.y;
                for (const MiniTile& t : it.tiles) {
                    drawMiniTile({tx, ty - (t.mark ? 3.f : 0.f), kMiniW, kMiniH}, t);
                    tx += kMiniW + kMiniGap;
                }
                if (it.wrong) {
                    const float x0 = x + it.box.x + (it.box.width - tilesW) * 0.5f - 4.f;
                    pencilLine({x0, ty - 2.f}, {x0 + tilesW + 8.f, ty + kMiniH + 2.f}, 3.f, alphaMul(kRedPencil, 0.9f), 7u);
                    pencilLine({x0, ty + kMiniH + 2.f}, {x0 + tilesW + 8.f, ty - 2.f}, 3.f, alphaMul(kRedPencil, 0.9f), 9u);
                }
                drawTextCentered(FontId::Ui, it.caption, {x + it.box.x + it.box.width * 0.5f, ty + kMiniH + 16.f}, 17.f,
                                 it.wrong ? kRedPencil : kGraphite);
            }
            break;
        }
    }

    // ------------------------------------------------------------ pause
    void drawPaused(Vector2 m, const okey::Game* g) {
        drawDim(0.5f);
        const Rectangle P = L::PausePanel;
        drawPanel(P, PanelStyle::Wood);
        drawTeaGlass({800.f, P.y + 104.f}, 0.62f, time, 70.f);
        drawHeader("Çay Molası", {800.f, P.y + 148.f}, 46.f, kGold);
        if (g && g->handState() != okey::HandState::NotStarted) {
            const std::string s = g->classic() ? "Okey \xC2\xB7 " + std::to_string(g->handIndex() + 1) + ". el"
                                               : "El " + std::to_string(std::min(g->handIndex() + 1, g->numHands())) +
                                                     " / " + std::to_string(g->numHands());
            drawTextCentered(FontId::Ui, s, {800.f, P.y + 191.f}, 20.f, alphaMul(pal::TextLight, 0.7f));
        }
        const Vector2 bm = confirmQuit ? kNoMouse : m;
        const char* labels[5] = {"Devam", aiMode ? "Kontrolü Geri Al" : "Yapay Zeka Oynasın", "Kurallar", "Ayarlar",
                                 "Ana Menü"};
        const int ids[5] = {C_Resume, C_PauseAi, C_PauseRules, C_PauseSettings, C_PauseMenu};
        for (int i = 0; i < 5; ++i) {
            if (drawButton(L::PauseBtn[i], labels[i], bm, true, ButtonStyle::Wood, i == 0 ? 30.f : 26.f)) click(ids[i]);
        }
        drawTextCentered(FontId::Ui, "ESC ile oyuna dön  \xC2\xB7  Y ile yapay zeka aç / kapa",
                         {800.f, P.y + P.height - 40.f}, 17.f, alphaMul(pal::TextLight, 0.5f));

        if (confirmQuit) {
            const float ca = easeOutBack(clamp01((time - confirmAt) / 0.25f));
            drawDim(0.62f * clamp01((time - confirmAt) / 0.15f));
            Rectangle C = L::ConfirmPanel;
            const float sc = 0.92f + 0.08f * ca;
            C = {800.f - C.width * sc * 0.5f, C.y + C.height * 0.5f - C.height * sc * 0.5f, C.width * sc, C.height * sc};
            drawPanel(C, PanelStyle::Paper);
            DrawRectangleRec({C.x, C.y, C.width, 8.f}, alphaMul(kRedPencil, 0.8f));
            handTextCentered("Maç bitecek, emin misin?", {800.f, C.y + 66.f}, 46.f, kInk);
            drawTextCentered(FontId::Ui, "Bu maçın hesabı silinir, ana menüye dönülür.", {800.f, C.y + 116.f}, 19.f,
                             kGraphite);
            if (ca > 0.9f) {
                if (drawButton(L::ConfirmYes, "Evet, bitir", m, true, ButtonStyle::Paper, 32.f)) click(C_ConfirmYes);
                if (drawButton(L::ConfirmNo, "Vazgeç", m, true, ButtonStyle::Paper, 32.f)) click(C_ConfirmNo);
            }
        }
    }

    // ------------------------------------------------------------ hand summary (hesap kağıdı)
    static void drawGridPaper(Rectangle S) {
        drawPanel(S, PanelStyle::Paper);
        const float step = 30.f;
        for (float x = S.x + step; x < S.x + S.width - 4.f; x += step)
            DrawLineEx({x, S.y + 3.f}, {x, S.y + S.height - 3.f}, 1.f, alphaMul(kGridBlue, 0.13f));
        for (float y = S.y + step; y < S.y + S.height - 4.f; y += step)
            DrawLineEx({S.x + 3.f, y}, {S.x + S.width - 3.f, y}, 1.f, alphaMul(kGridBlue, 0.13f));
        DrawLineEx({S.x + 86.f, S.y + 3.f}, {S.x + 86.f, S.y + S.height - 3.f}, 1.5f, alphaMul(kMarginRed, 0.5f));
        // a tea glass left a ring on the sheet
        const Vector2 st{S.x + S.width - 92.f, S.y + 78.f};
        DrawCircleV(st, 47.f, rgba(160, 100, 40, 0.05f));
        DrawRing(st, 43.f, 48.f, 20.f, 330.f, 40, rgba(150, 90, 36, 0.16f));
        DrawRing(st, 45.f, 47.f, 90.f, 250.f, 30, rgba(130, 70, 24, 0.14f));
    }

    struct Cell {
        std::string text;
        Color color = kGraphite;
        float size = 30.f;
    };

    void drawHandSummary(Vector2 m, const okey::Game* g) {
        if (sheet) {
            drawSheetSummary(m, *sheet);
            return;
        }
        const float age = ageOf(ScreenId::HandSummary);
        drawDim(0.5f);
        const float slide = (1.f - easeOutCubic(age / 0.45f)) * 46.f;
        Rectangle S = L::Sheet;
        S.y += slide;
        drawGridPaper(S);
        drawPencil({S.x + 700.f, S.y + S.height - 40.f}, -4.f);

        const int handsDone = g ? (int)g->player(0).handScores.size() : 0;
        const bool last = isLastHand(g);
        const Rectangle btn{S.x + S.width - 252.f, S.y + S.height - 68.f, 216.f, 54.f};
        if (age < 0.46f) m = kNoMouse;
        if (!g || handsDone == 0) {
            handTextCentered("Henüz yazılacak bir el yok.", {800.f, S.y + 300.f}, 44.f, kGraphite);
            if (drawButton(btn, "Devam", m, true, ButtonStyle::Paper, 34.f)) click(C_Next);
            return;
        }
        auto ink = [&](float t0) { return clamp01((age - t0) / 0.28f); };
        const okey::HandResult& r = g->lastHandResult();
        int human = 0;
        for (int s = 0; s < okey::NUM_PLAYERS; ++s)
            if (g->player(s).human) human = s;
        const float labelX = S.x + 108.f;

        // --- title
        {
            const std::string title = "El " + std::to_string(handsDone) + " Sonucu";
            const float a = ink(0.12f);
            handTextCentered(title, {800.f, S.y + 52.f}, 62.f, alphaMul(kInk, a));
            const float w = handMeasure(title, 62.f).x;
            pencilLine({800.f - w * 0.5f - 18.f, S.y + 86.f}, {800.f - w * 0.5f - 18.f + (w + 36.f) * a, S.y + 84.f},
                       2.2f, alphaMul(kInk, 0.8f * a), 3u);
            const std::string sub = g->classic() ? "okey: " + std::to_string(g->rules().okeyStartPoints) + "'den geriye"
                                    : std::string(g->teams() ? "eşli maç: " : "maç: ") + std::to_string(handsDone) + " / " +
                                          std::to_string(g->numHands()) + " el";
            handText(sub, {labelX, S.y + 34.f}, 30.f, alphaMul(kRedPencil, 0.85f * a));
        }
        // --- who finished + multipliers
        {
            const float a = ink(0.3f);
            std::string line;
            if (r.reason == okey::HandEndReason::PlayerFinished && r.winner >= 0) {
                line = !g->player(r.winner).human ? g->player(r.winner).name + " eli bitirdi!"
                       : aiMode                    ? std::string("Eli yapay zeka bitirdi!")
                                                   : std::string("Eli sen bitirdin!");
            } else {
                line = g->classic() ? "Taşlar bitti, el berabere." : "Taşlar bitti, eli bitiren olmadı.";
            }
            handTextCentered(line, {800.f, S.y + 118.f}, 42.f, alphaMul(kInk, a));
            std::vector<std::string> tags;
            if (r.finishedWithJoker) tags.push_back(g->classic() ? "okey atarak \xC3\x97" "2" : "okeyle bitiş \xC3\x97" "2");
            if (r.finishedWithPairs) tags.push_back("çiftten bitiş \xC3\x97" "2");
            if (r.finishedInOneGo) tags.push_back("elden bitiş \xC3\x97" "2");
            std::string tagLine;
            Color tagCol = kRedPencil;
            if (tags.empty()) {
                tagLine = "kat yok"; // every column's "Hesap" cell shows its own formula
                tagCol = kGraphite;
            } else {
                for (size_t i = 0; i < tags.size(); ++i) tagLine += (i ? "  \xC2\xB7  " : "") + tags[i];
                if (tags.size() > 1) tagLine += "  =  \xC3\x97" + std::to_string(r.multiplier);
                tagLine += g->classic() ? "   (herkesten " + std::to_string(2 * r.multiplier) + " düşülür)"
                                        : std::string("   (cezalar hariç tüm puanlar)");
            }
            handTextCentered(tagLine, {800.f, S.y + 158.f}, 32.f, alphaMul(tagCol, ink(0.42f)));
        }

        // --- grid: one column per player
        const float colX0 = S.x + 300.f, colW = 222.f;
        const float gridR = colX0 + colW * 4.f;
        auto colC = [&](int s) { return colX0 + colW * ((float)s + 0.5f); };
        auto fitSize = [&](const std::string& t, float size) {
            const float maxW = colW - 18.f;
            const float w = handMeasure(t, size).x;
            return w > maxW ? size * maxW / w : size;
        };
        auto cellText = [&](const std::string& t, int s, float cy, float size, Color c) {
            handTextCentered(t, {colC(s), cy}, fitSize(t, size), c);
        };
        const float headY = S.y + 210.f;
        const float rowY0 = headY + 48.f, rowH = 35.f;
        const bool classic = g->classic();
        const int nRows = classic ? 3 : 4;
        {
            const float a = ink(0.5f);
            for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
                const std::string name = seatName(*g, s);
                cellText(name, s, headY, 36.f, alphaMul(s == human ? kBluePen : kInk, a));
                if (s == r.winner) {
                    const float w = handMeasure(name, fitSize(name, 36.f)).x;
                    drawStar({colC(s) - w * 0.5f - 16.f, headY - 1.f}, 10.f, alphaMul(kRedPencil, a));
                }
            }
            pencilLine({labelX - 8.f, headY + 25.f}, {gridR, headY + 25.f}, 2.f, alphaMul(kGraphite, 0.8f * a), 11u);
            for (int s = 0; s <= okey::NUM_PLAYERS; ++s) {
                const float x = colX0 + colW * (float)s;
                pencilLine({x, headY - 26.f}, {x, rowY0 + rowH * (float)(nRows - 1) + 18.f}, 1.2f,
                           alphaMul(kGraphite, 0.35f * a), 20u + (uint32_t)s, 0.8f);
            }
        }
        // this hand, per player. "Hesap" spells out the score before penalties, so Hesap + Ceza = Bu el can be
        // checked by eye: bitiren -101, açmayan 202, seriyle açan elde kalan, çiftle açan elde kalan x 2, and the
        // hand's multiplier (okeyle / çiftten / elden bitiş) on all of them.
        const std::string times = " \xC3\x97 ";
        const std::string multTail = r.multiplier > 1 ? times + std::to_string(r.multiplier) : std::string();
        const okey::RulesConfig& rc = g->rules();
        const char* rowLabels[4] = {"Durum", "Hesap", "Ceza", "Bu el"};
        if (classic) {
            rowLabels[1] = "Gösterge";
            rowLabels[2] = "Bu el";
        }
        const int partnerOfWinner = (g->teams() && r.winner >= 0) ? okey::Game::partnerOf(r.winner) : -1;
        for (int row = 0; row < nRows; ++row) {
            const float cy = rowY0 + rowH * (float)row;
            const float a = ink(0.62f + 0.1f * (float)row);
            handText(rowLabels[row], {labelX, cy - 17.f}, 30.f, alphaMul(kGraphite, a));
            for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
                const okey::PlayerInfo& p = g->player(s);
                const size_t si = (size_t)s;
                Cell c;
                const bool won = s == r.winner;
                if (classic) {
                    if (row == 0) c = won ? Cell{"Bitirdi", kRedPencil, 31.f} : Cell{kDash, alphaMul(kGraphite, 0.6f), 30.f};
                    else if (row == 1)
                        c = r.indicatorShownBy == s ? Cell{"Gösterdi", kGraphite, 30.f}
                            : r.indicatorShownBy >= 0 ? Cell{scoreText(-r.penalties[si]), kRedPencil, 31.f}
                                                      : Cell{kDash, alphaMul(kGraphite, 0.6f), 30.f};
                    else c = {r.score[si] == 0 ? std::string("0") : scoreText(r.score[si]), s == human ? kBluePen : kInk, 36.f};
                    cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
                    continue;
                }
                if (s == partnerOfWinner && row < 2) { // eşli: the finisher's partner's hand is wiped
                    c = row == 0 ? Cell{"Ortağı bitirdi", kRedPencil, 28.f} : Cell{"silindi", kGraphite, 30.f};
                    cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
                    continue;
                }
                switch (row) {
                case 0:
                    if (won) c = {"Bitirdi", kRedPencil, 31.f};
                    else if (!p.opened) c = {"Açmadı", kGraphite, 30.f};
                    else if (p.openedWithPairs) c = {"Çiftle açtı (" + std::to_string(p.openValue) + " çift)", kGraphite, 28.f};
                    else c = {"Seriyle açtı (" + std::to_string(p.openValue) + ")", kGraphite, 28.f};
                    break;
                case 1:
                    if (won) c = {scoreText(rc.winnerScore) + multTail, kGraphite, 31.f};
                    else if (!p.opened) c = {std::to_string(rc.unopenedScore) + multTail, kGraphite, 31.f};
                    else if (p.openedWithPairs)
                        c = {std::to_string(r.remaining[si]) + times + "2" + multTail, kGraphite, 31.f};
                    else c = {std::to_string(r.remaining[si]) + multTail, kGraphite, 31.f};
                    break;
                case 2:
                    if (r.penalties[si] > 0) c = {"+" + std::to_string(r.penalties[si]), kRedPencil, 31.f};
                    else c = {kDash, alphaMul(kGraphite, 0.6f), 30.f};
                    break;
                default:
                    c = {scoreText(r.score[si]), s == human ? kBluePen : kInk, 36.f};
                    break;
                }
                cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
            }
        }
        const float sepY = rowY0 + rowH * (float)(nRows - 1) + 24.f;
        {
            const float a = ink(1.0f);
            pencilLine({labelX - 8.f, sepY}, {gridR, sepY}, 1.8f, alphaMul(kGraphite, 0.8f * a), 31u);
            pencilLine({labelX - 8.f, sepY + 5.f}, {gridR, sepY + 5.f}, 1.8f, alphaMul(kGraphite, 0.8f * a), 37u);
        }

        // --- running sheet: one row per hand, totals underlined, the leader circled in red pencil
        const int n = handsDone;
        const float histTop = sepY + 12.f;
        const float totYMax = btn.y - 40.f; // the leader's circle stays clear of the button
        const float hRow = std::min(36.f, (totYMax - 32.f - histTop) / (float)n);
        const float hFont = std::min(32.f, hRow * 1.02f + 2.f);
        const float t0 = 1.05f;
        for (int h = 0; h < n; ++h) {
            const float cy = histTop + hRow * ((float)h + 0.5f);
            const float a = ink(t0 + 0.05f * (float)h);
            if (h == n - 1) { // highlighter over this hand's row
                DrawRectangleRounded({labelX - 12.f, cy - hRow * 0.42f, (gridR - labelX + 20.f) * a, hRow * 0.84f}, 0.4f,
                                     6, rgba(255, 226, 70, 0.33f));
            }
            handText(std::to_string(h + 1) + ". el", {labelX, cy - hFont * 0.55f}, hFont, alphaMul(kGraphite, a));
            for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
                const std::vector<int>& hs = g->player(s).handScores;
                if (h >= (int)hs.size()) continue;
                cellText(scoreText(hs[(size_t)h]), s, cy, hFont, alphaMul(s == human ? kBluePen : kGraphite, a));
            }
        }
        const float sumY = histTop + hRow * (float)n + 4.f;
        const float totY = sumY + 28.f;
        for (int s = 0; s <= okey::NUM_PLAYERS; ++s) {
            const float x = colX0 + colW * (float)s;
            pencilLine({x, histTop}, {x, totY + 26.f}, 1.2f, alphaMul(kGraphite, 0.35f * ink(t0)), 50u + (uint32_t)s, 0.8f);
        }
        const float tTot = t0 + 0.05f * (float)n + 0.12f;
        pencilLine({colX0 + 10.f, sumY}, {gridR - 10.f, sumY}, 2.f, alphaMul(kInk, 0.85f * ink(tTot)), 61u);
        handText("Toplam", {labelX, totY - 20.f}, 36.f, alphaMul(kInk, ink(tTot)));
        const std::vector<int> leaders = coLeaders(*g);
        for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
            const std::string t = scoreText(g->player(s).totalScore);
            const float a = ink(tTot + 0.06f * (float)s);
            cellText(t, s, totY, 40.f, alphaMul(s == human ? kBluePen : kInk, a));
            const float w = handMeasure(t, 40.f).x;
            pencilLine({colC(s) - w * 0.5f - 6.f, totY + 20.f}, {colC(s) + w * 0.5f + 6.f, totY + 20.f}, 1.8f,
                       alphaMul(kInk, 0.8f * a), 70u + (uint32_t)s);
            pencilLine({colC(s) - w * 0.5f - 6.f, totY + 25.f}, {colC(s) + w * 0.5f + 6.f, totY + 25.f}, 1.8f,
                       alphaMul(kInk, 0.8f * a), 80u + (uint32_t)s);
            if (std::find(leaders.begin(), leaders.end(), s) != leaders.end()) { // every co-leader is circled
                const float cp = clamp01((age - tTot - 0.35f) / 0.55f);
                pencilEllipse({colC(s), totY + 2.f}, w * 0.5f + 30.f, 28.f, cp, 2.6f, alphaMul(kRedPencil, 0.9f),
                              (uint32_t)(handsDone * 7 + s));
            }
        }
        if (g->teams()) { // the two teams' totals, under the sheet
            const float a = ink(tTot + 0.4f);
            const std::string t = teamName(*g, 0) + ": " + scoreText(g->teamTotal(0)) + "      " + teamName(*g, 1) + ": " +
                                  scoreText(g->teamTotal(1));
            handTextCentered(t, {800.f, totY + 58.f}, 30.f, alphaMul(kInk, a));
        }
        const float aNote = ink(tTot + 0.6f);
        if (aNote > 0.f && g->teams()) {
            const bool you = humanAmong(*g, leaders);
            std::string note;
            if (leaders.size() == 4) note = last ? "Maç berabere bitti" : "Takımlar başa baş";
            else if (you) note = last ? "Maçı takımınız kazandı!" : "Takımınız önde!";
            else note = last ? "Maçı " + teamName(*g, leaders[0]) + " kazandı" : teamName(*g, leaders[0]) + " önde";
            if (!last) note += "  (düşük puan iyidir)";
            handText(note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
        } else if (aNote > 0.f) {
            const bool you = humanAmong(*g, leaders);
            std::string note;
            if (leaders.size() == 1) {
                const std::string who = seatName(*g, leaders[0]);
                if (you && aiMode) note = last ? "Maçı yapay zeka kazandı!" : "Yapay zeka önde!";
                else note = last ? (you ? std::string("Maçı sen kazandın!") : "Maçı " + who + " kazandı")
                                 : (you ? std::string("Önde sensin!") : who + " önde");
            } else if (leaders.size() == (size_t)okey::NUM_PLAYERS) {
                note = last ? "Maç berabere bitti, birincilik herkesin" : "Herkes başa baş";
            } else if (you) {
                const std::string others = joinNames(*g, leaders, true);
                if (aiMode)
                    note = last ? "Berabere! Yapay zeka birinciliği " + others + " ile paylaştı"
                                : "Yapay zeka " + others + " ile başa baş önde";
                else
                    note = last ? "Berabere! Birinciliği " + others + " ile paylaştın" : others + " ile başa baş öndesin";
            } else {
                const std::string all = joinNames(*g, leaders, false);
                note = last ? "Birinciliği " + all + " paylaştı" : all + " başa baş önde";
            }
            if (!last) note += classic ? "  (yüksek puan iyidir)" : "  (düşük puan iyidir)";
            handText(note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
        }

        float bfs = 36.f;
        const std::string blabel = autoLabel(last ? "Sonuçlar" : "Sonraki El", FontId::Hand, btn.width - 22.f, bfs);
        if (drawButton(btn, blabel, m, true, ButtonStyle::Paper, bfs)) click(C_Next);
    }

    // ------------------------------------------------------------ the other games' sheet
    void drawSheetSummary(Vector2 m, const SheetModel& sm) {
        const float age = ageOf(ScreenId::HandSummary);
        drawDim(0.5f);
        const float slide = (1.f - easeOutCubic(age / 0.45f)) * 46.f;
        Rectangle S = L::Sheet;
        S.y += slide;
        drawGridPaper(S);
        drawPencil({S.x + 700.f, S.y + S.height - 40.f}, -4.f);
        const Rectangle btn{S.x + S.width - 252.f, S.y + S.height - 68.f, 216.f, 54.f};
        if (age < 0.46f) m = kNoMouse;
        auto ink = [&](float t0) { return clamp01((age - t0) / 0.28f); };
        const float labelX = S.x + 108.f;
        const int nc = std::max(1, (int)sm.columns.size());

        {
            const float a = ink(0.12f);
            handTextCentered(sm.title, {800.f, S.y + 52.f}, 62.f, alphaMul(kInk, a));
            const float w = handMeasure(sm.title, 62.f).x;
            pencilLine({800.f - w * 0.5f - 18.f, S.y + 86.f}, {800.f - w * 0.5f - 18.f + (w + 36.f) * a, S.y + 84.f}, 2.2f,
                       alphaMul(kInk, 0.8f * a), 3u);
            if (!sm.corner.empty()) handText(sm.corner, {labelX, S.y + 34.f}, 30.f, alphaMul(kRedPencil, 0.85f * a));
        }
        {
            float hs = 42.f;
            while (hs > 26.f && handMeasure(sm.headline, hs).x > S.width - 160.f) hs -= 1.f;
            handTextCentered(sm.headline, {800.f, S.y + 118.f}, hs, alphaMul(kInk, ink(0.3f)));
            if (!sm.tagline.empty())
                handTextCentered(sm.tagline, {800.f, S.y + 158.f}, 32.f,
                                 alphaMul(sm.taglineRed ? kRedPencil : kGraphite, ink(0.42f)));
        }
        // columns (2..4) over the same 888-unit grid as the okey sheet
        const float gridL = S.x + 300.f, gridW = 222.f * 4.f;
        const float colW = gridW / (float)nc;
        const float colX0 = gridL;
        const float gridR = colX0 + colW * (float)nc;
        auto colC = [&](int c) { return colX0 + colW * ((float)c + 0.5f); };
        auto fitSize = [&](const std::string& t, float size) {
            const float maxW = colW - 18.f;
            const float w = handMeasure(t, size).x;
            return w > maxW ? size * maxW / w : size;
        };
        auto cellText = [&](const std::string& t, int c, float cy, float size, Color col) {
            handTextCentered(t, {colC(c), cy}, fitSize(t, size), col);
        };
        const float headY = S.y + 210.f;
        const float rowY0 = headY + 48.f, rowH = 35.f;
        const int nRows = (int)sm.rowLabels.size();
        {
            const float a = ink(0.5f);
            for (int c = 0; c < nc; ++c) {
                const std::string& name = sm.columns[(size_t)c];
                cellText(name, c, headY, 36.f, alphaMul(c == sm.humanCol ? kBluePen : kInk, a));
                if (c == sm.starCol) {
                    const float w = handMeasure(name, fitSize(name, 36.f)).x;
                    drawStar({colC(c) - w * 0.5f - 16.f, headY - 1.f}, 10.f, alphaMul(kRedPencil, a));
                }
            }
            pencilLine({labelX - 8.f, headY + 25.f}, {gridR, headY + 25.f}, 2.f, alphaMul(kGraphite, 0.8f * a), 11u);
            for (int c = 0; c <= nc; ++c) {
                const float x = colX0 + colW * (float)c;
                pencilLine({x, headY - 26.f}, {x, rowY0 + rowH * (float)std::max(0, nRows - 1) + 18.f}, 1.2f,
                           alphaMul(kGraphite, 0.35f * a), 20u + (uint32_t)c, 0.8f);
            }
        }
        for (int row = 0; row < nRows; ++row) {
            const float cy = rowY0 + rowH * (float)row;
            const float a = ink(0.62f + 0.1f * (float)row);
            const bool lastRow = row == nRows - 1;
            handText(sm.rowLabels[(size_t)row], {labelX, cy - 17.f}, 30.f, alphaMul(kGraphite, a));
            for (int c = 0; c < nc; ++c) {
                if (row >= (int)sm.cells.size() || c >= (int)sm.cells[(size_t)row].size()) continue;
                const std::string& t = sm.cells[(size_t)row][(size_t)c];
                const bool red = row < (int)sm.red.size() && c < (int)sm.red[(size_t)row].size() && sm.red[(size_t)row][(size_t)c];
                const Color col = red ? kRedPencil : lastRow ? (c == sm.humanCol ? kBluePen : kInk) : kGraphite;
                cellText(t.empty() ? std::string(kDash) : t, c, cy, lastRow ? 36.f : 30.f,
                         alphaMul(t.empty() ? alphaMul(kGraphite, 0.6f) : col, a));
            }
        }
        const float sepY = rowY0 + rowH * (float)std::max(0, nRows - 1) + 24.f;
        {
            const float a = ink(1.0f);
            pencilLine({labelX - 8.f, sepY}, {gridR, sepY}, 1.8f, alphaMul(kGraphite, 0.8f * a), 31u);
            pencilLine({labelX - 8.f, sepY + 5.f}, {gridR, sepY + 5.f}, 1.8f, alphaMul(kGraphite, 0.8f * a), 37u);
        }
        const int n = std::max(1, (int)sm.history.size());
        const float histTop = sepY + 12.f;
        const float totYMax = btn.y - 40.f;
        const float hRow = std::min(36.f, (totYMax - 32.f - histTop) / (float)n);
        const float hFont = std::min(32.f, hRow * 1.02f + 2.f);
        const float t0 = 1.05f;
        for (int h = 0; h < (int)sm.history.size(); ++h) {
            const float cy = histTop + hRow * ((float)h + 0.5f);
            const float a = ink(t0 + 0.05f * (float)std::min(h, 12));
            if (h == (int)sm.history.size() - 1)
                DrawRectangleRounded({labelX - 12.f, cy - hRow * 0.42f, (gridR - labelX + 20.f) * a, hRow * 0.84f}, 0.4f, 6,
                                     rgba(255, 226, 70, 0.33f));
            const std::string lbl = h < (int)sm.historyLabels.size() ? sm.historyLabels[(size_t)h] : std::to_string(h + 1) + ". el";
            float ls = hFont;
            while (ls > 12.f && handMeasure(lbl, ls).x > colX0 - labelX - 10.f) ls -= 1.f;
            handText(lbl, {labelX, cy - ls * 0.55f}, ls, alphaMul(kGraphite, a));
            for (int c = 0; c < nc && c < (int)sm.history[(size_t)h].size(); ++c)
                cellText(sm.history[(size_t)h][(size_t)c], c, cy, hFont, alphaMul(c == sm.humanCol ? kBluePen : kGraphite, a));
        }
        const float sumY = histTop + hRow * (float)n + 4.f;
        const float totY = sumY + 28.f;
        for (int c = 0; c <= nc; ++c) {
            const float x = colX0 + colW * (float)c;
            pencilLine({x, histTop}, {x, totY + 26.f}, 1.2f, alphaMul(kGraphite, 0.35f * ink(t0)), 50u + (uint32_t)c, 0.8f);
        }
        const float tTot = t0 + 0.05f * (float)std::min(n, 12) + 0.12f;
        pencilLine({colX0 + 10.f, sumY}, {gridR - 10.f, sumY}, 2.f, alphaMul(kInk, 0.85f * ink(tTot)), 61u);
        handText(sm.totalLabel, {labelX, totY - 20.f}, 36.f, alphaMul(kInk, ink(tTot)));
        for (int c = 0; c < nc && c < (int)sm.totals.size(); ++c) {
            const std::string& t = sm.totals[(size_t)c];
            const float a = ink(tTot + 0.06f * (float)c);
            cellText(t, c, totY, 40.f, alphaMul(c == sm.humanCol ? kBluePen : kInk, a));
            const float w = handMeasure(t, fitSize(t, 40.f)).x;
            pencilLine({colC(c) - w * 0.5f - 6.f, totY + 20.f}, {colC(c) + w * 0.5f + 6.f, totY + 20.f}, 1.8f,
                       alphaMul(kInk, 0.8f * a), 70u + (uint32_t)c);
            pencilLine({colC(c) - w * 0.5f - 6.f, totY + 25.f}, {colC(c) + w * 0.5f + 6.f, totY + 25.f}, 1.8f,
                       alphaMul(kInk, 0.8f * a), 80u + (uint32_t)c);
            if (std::find(sm.leaders.begin(), sm.leaders.end(), c) != sm.leaders.end()) {
                const float cp = clamp01((age - tTot - 0.35f) / 0.55f);
                pencilEllipse({colC(c), totY + 2.f}, w * 0.5f + 30.f, 28.f, cp, 2.6f, alphaMul(kRedPencil, 0.9f),
                              (uint32_t)(n * 7 + c));
            }
        }
        const float aNote = ink(tTot + 0.6f);
        if (aNote > 0.f && !sm.note.empty()) handText(sm.note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
        float bfs = 36.f;
        const std::string blabel = autoLabel(sm.last ? "Sonuçlar" : "Sonraki El", FontId::Hand, btn.width - 22.f, bfs);
        if (drawButton(btn, blabel, m, true, ButtonStyle::Paper, bfs)) click(C_Next);
    }

    void drawSheetMatchOver(Vector2 m, const SheetModel& sm) {
        const float age = ageOf(ScreenId::MatchOver);
        drawDim(0.62f);
        rlPushMatrix();
        rlTranslatef(800.f, 200.f, 0.f);
        rlScalef(1.6f, 1.f, 1.f);
        DrawCircleGradient({0, 0}, 260.f, rgba(8, 5, 3, 0.7f), rgba(8, 5, 3, 0.f));
        rlPopMatrix();
        const float pulse = 0.85f + 0.15f * std::sin(time * 1.4f);
        DrawCircleGradient({800.f, 250.f}, 520.f, rgba(255, 170, 80, 0.26f * pulse), rgba(255, 170, 80, 0.f));
        DrawCircleGradient({800.f, 250.f}, 230.f, rgba(255, 210, 140, 0.22f * pulse), rgba(255, 210, 140, 0.f));
        for (int i = 0; i < 12; ++i) {
            const float a = (float)i * kPi / 6.f + time * 0.05f;
            const Vector2 c{800.f, 230.f};
            tri(c, {c.x + std::cos(a - 0.07f) * 420.f, c.y + std::sin(a - 0.07f) * 420.f},
                {c.x + std::cos(a + 0.07f) * 420.f, c.y + std::sin(a + 0.07f) * 420.f}, rgba(255, 200, 120, 0.045f * pulse));
        }
        drawHeader(sm.matchHeader, {800.f, 56.f}, 60.f, kGold);
        if (!sm.playedLine.empty())
            drawTextCentered(FontId::Ui, sm.playedLine, {800.f, 98.f}, 20.f, alphaMul(pal::TextLight, 0.7f));
        const float pop = easeOutBack(clamp01((age - 0.15f) / 0.5f));
        rlPushMatrix();
        rlTranslatef(800.f, 270.f, 0.f);
        rlScalef(pop, pop, 1.f);
        drawTeaGlass({0.f, 0.f}, 1.25f, time, 42.f);
        rlPopMatrix();
        const float a1 = clamp01((age - 0.35f) / 0.4f);
        drawHeader(sm.resultTitle, {800.f, 324.f}, sm.humanWon ? 50.f : 46.f,
                   alphaMul(sm.humanWon ? pal::Highlight : pal::TextLight, a1));
        drawTextCentered(FontId::Ui, sm.resultSub, {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));

        const int rows = (int)sm.ranking.size();
        Rectangle card = L::MatchCard;
        card.height = 32.f + 68.f * (float)rows - 6.f;
        drawPanel(card, PanelStyle::Dark);
        static const Color medal[4] = {Color{232, 184, 64, 255}, Color{196, 198, 206, 255}, Color{190, 118, 64, 255},
                                       Color{118, 88, 62, 255}};
        for (int i = 0; i < rows; ++i) {
            const int c = sm.ranking[(size_t)i];
            const int rank = i < (int)sm.rank.size() ? sm.rank[(size_t)i] : i + 1;
            const float ra = clamp01((age - 0.55f - 0.12f * (float)i) / 0.3f);
            const float slideX = (1.f - easeOutCubic(ra)) * 40.f;
            const Rectangle row{card.x + 18.f + slideX, card.y + 16.f + 68.f * (float)i, card.width - 36.f, 62.f};
            if (rank == 1) {
                DrawRectangleRounded(row, 0.3f, 8, rgba(255, 200, 110, 0.16f * ra));
                DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, alphaMul(pal::Highlight, 0.7f * ra));
            } else {
                DrawRectangleRounded(row, 0.3f, 8, rgba(255, 255, 255, 0.04f * ra));
            }
            const Vector2 mc{row.x + 36.f, row.y + row.height * 0.5f};
            DrawCircleV({mc.x + 1.f, mc.y + 2.f}, 22.f, rgba(0, 0, 0, 0.4f * ra));
            DrawCircleV(mc, 22.f, alphaMul(medal[std::clamp(rank, 1, 4) - 1], ra));
            const std::string digit = std::to_string(rank);
            const float dw = measureText(FontId::UiBold, digit, 25.f).x;
            drawTextCentered(FontId::UiBold, digit, {mc.x, mc.y - 1.f}, 25.f, alphaMul(pal::TextDark, ra));
            drawText(FontId::UiBold, ".", {mc.x + dw * 0.5f, mc.y - 15.f}, 25.f, alphaMul(pal::TextDark, ra));
            const Color nameCol = c == sm.humanCol ? pal::Highlight : pal::TextLight;
            const std::string name = c < (int)sm.columns.size() ? sm.columns[(size_t)c] : std::string();
            drawText(FontId::UiBold, name, {row.x + 76.f, row.y + 7.f}, 28.f, alphaMul(nameCol, ra));
            if (c < (int)sm.rankSub.size()) {
                float fs = 15.f;
                const float maxW = row.width - 76.f - 150.f;
                const float w = measureText(FontId::Ui, sm.rankSub[(size_t)c], fs).x;
                if (w > maxW) fs *= maxW / w;
                drawText(FontId::Ui, sm.rankSub[(size_t)c], {row.x + 77.f, row.y + 40.f}, fs, alphaMul(pal::TextLight, 0.55f * ra));
            }
            const std::string tot = c < (int)sm.totals.size() ? sm.totals[(size_t)c] : std::string();
            const float tw = measureText(FontId::UiBold, tot, 34.f).x;
            drawText(FontId::UiBold, tot, {row.x + row.width - 22.f - tw, row.y + 13.f}, 34.f,
                     alphaMul(rank == 1 ? pal::Highlight : pal::TextLight, ra));
        }
        float nfs = 30.f;
        const std::string nlabel = autoLabel("Yeni Oyun", FontId::UiBold, L::MatchNew.width - 24.f, nfs);
        if (drawButton(L::MatchNew, nlabel, m, true, ButtonStyle::Wood, nfs)) click(C_NewGame);
        if (drawButton(L::MatchMenu, "Ana Menü", m, true, ButtonStyle::Wood, 30.f)) click(C_MatchMenu);
        if (analysisAvailable && drawButton(L::MatchAnalysis, "Hatalarım", m, true, ButtonStyle::Wood, 22.f))
            click(C_ShowAnalysis);
        drawConfetti();
    }

    // ------------------------------------------------------------ match over
    void drawMatchOver(Vector2 m, const okey::Game* g) {
        if (sheet) {
            drawSheetMatchOver(m, *sheet);
            return;
        }
        const float age = ageOf(ScreenId::MatchOver);
        drawDim(0.62f);
        rlPushMatrix();
        rlTranslatef(800.f, 200.f, 0.f);
        rlScalef(1.6f, 1.f, 1.f);
        DrawCircleGradient({0, 0}, 260.f, rgba(8, 5, 3, 0.7f), rgba(8, 5, 3, 0.f));
        rlPopMatrix();
        const float pulse = 0.85f + 0.15f * std::sin(time * 1.4f);
        DrawCircleGradient({800.f, 250.f}, 520.f, rgba(255, 170, 80, 0.26f * pulse), rgba(255, 170, 80, 0.f));
        DrawCircleGradient({800.f, 250.f}, 230.f, rgba(255, 210, 140, 0.22f * pulse), rgba(255, 210, 140, 0.f));
        // slow light rays
        for (int i = 0; i < 12; ++i) {
            const float a = (float)i * kPi / 6.f + time * 0.05f;
            const Vector2 c{800.f, 230.f};
            const Vector2 p1{c.x + std::cos(a - 0.07f) * 420.f, c.y + std::sin(a - 0.07f) * 420.f};
            const Vector2 p2{c.x + std::cos(a + 0.07f) * 420.f, c.y + std::sin(a + 0.07f) * 420.f};
            tri(c, p1, p2, rgba(255, 200, 120, 0.045f * pulse));
        }

        drawHeader(g && g->classic() ? "Oyun Bitti" : "Maç Bitti", {800.f, 56.f}, 60.f, kGold);
        if (!g || g->player(0).handScores.empty()) {
            drawTextCentered(FontId::Ui, "Sonuç yok.", {800.f, 400.f}, 24.f, pal::TextLight);
        } else {
            const int hands = (int)g->player(0).handScores.size();
            drawTextCentered(FontId::Ui, std::to_string(hands) + " el oynandı", {800.f, 98.f}, 20.f,
                             alphaMul(pal::TextLight, 0.7f));
            const float pop = easeOutBack(clamp01((age - 0.15f) / 0.5f));
            rlPushMatrix();
            rlTranslatef(800.f, 270.f, 0.f);
            rlScalef(pop, pop, 1.f);
            drawTeaGlass({0.f, 0.f}, 1.25f, time, 42.f);
            rlPopMatrix();

            const std::vector<int> leaders = coLeaders(*g);
            const int leader = leaders[0];
            const bool teams = g->teams();
            const std::string best = scoreText(teams ? g->teamTotal(leader) : g->player(leader).totalScore);
            const bool humanFirst = humanAmong(*g, leaders);
            const float a1 = clamp01((age - 0.35f) / 0.4f);
            if (teams && humanFirst && leaders.size() == 2) {
                drawHeader(aiMode ? "Yapay zekanın takımı kazandı!" : "Kazandınız! Çaylar onlardan!", {800.f, 324.f}, 50.f,
                           alphaMul(pal::Highlight, a1));
                drawTextCentered(FontId::Ui, teamName(*g, leader) + ", toplam " + best + " puanla masanın kurtları.",
                                 {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
            } else if (teams) {
                drawHeader(leaders.size() == 4 ? "Berabere!" : "Bu sefer olmadı, bir maç daha?", {800.f, 324.f}, 46.f,
                           alphaMul(pal::TextLight, a1));
                drawTextCentered(FontId::Ui,
                                 leaders.size() == 4 ? "İki takım da " + best + " puanda kaldı."
                                                     : "Kazananlar: " + teamName(*g, leader) + " (" + best + ")",
                                 {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
            } else if (humanFirst && leaders.size() == 1) {
                drawHeader(aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!", {800.f, 324.f},
                           50.f, alphaMul(pal::Highlight, a1));
                drawTextCentered(FontId::Ui, "Toplam " + best + (aiMode ? " puanla masanın kurdu oldu." : " puanla masanın kurdu oldun."),
                                 {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
            } else if (humanFirst) {
                drawHeader("Berabere!", {800.f, 324.f}, 50.f, alphaMul(pal::Highlight, a1));
                drawTextCentered(FontId::Ui,
                                 (aiMode ? "Yapay zeka toplam " : "Toplam ") + best + " puanla birinciliği " +
                                     joinNames(*g, leaders, true) + (aiMode ? " ile paylaştı." : " ile paylaştın."),
                                 {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
            } else {
                drawHeader("Bu sefer olmadı, bir maç daha?", {800.f, 324.f}, 46.f, alphaMul(pal::TextLight, a1));
                static const char* const quotes[4] = {"", "Sabreden derviş muradına ermiş.",
                                                      "Bu maç benim demiştim abi!",
                                                      "Bizim zamanımızda da hep ben kazanırdım."};
                std::string sub;
                if (leaders.size() > 1) {
                    sub = "Birinciliği " + joinNames(*g, leaders, false) + " paylaştı (" + best + ")";
                } else {
                    sub = "Kazanan: " + seatName(*g, leader) + " (" + best + ")";
                    if (leader >= 1 && leader <= 3)
                        sub += "  \xE2\x80\x94  \xE2\x80\x9C" + std::string(quotes[leader]) + "\xE2\x80\x9D";
                }
                drawTextCentered(FontId::Ui, sub, {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
            }

            // ranking card
            drawPanel(L::MatchCard, PanelStyle::Dark);
            std::vector<int> order = {0, 1, 2, 3};
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                if (standing(*g, a) != standing(*g, b)) return standing(*g, a) < standing(*g, b);
                if (teams && std::min(a, okey::Game::partnerOf(a)) != std::min(b, okey::Game::partnerOf(b)))
                    return std::min(a, okey::Game::partnerOf(a)) < std::min(b, okey::Game::partnerOf(b));
                return a < b;
            });
            static const Color medal[4] = {Color{232, 184, 64, 255}, Color{196, 198, 206, 255},
                                           Color{190, 118, 64, 255}, Color{118, 88, 62, 255}};
            int rank = 1;
            for (int i = 0; i < 4; ++i) {
                const int s = order[(size_t)i];
                const okey::PlayerInfo& p = g->player(s);
                if (i > 0 && standing(*g, s) != standing(*g, order[(size_t)i - 1])) rank = teams ? 2 : i + 1;
                const float ra = clamp01((age - 0.55f - 0.12f * (float)i) / 0.3f);
                const float slideX = (1.f - easeOutCubic(ra)) * 40.f;
                const Rectangle row{L::MatchCard.x + 18.f + slideX, L::MatchCard.y + 16.f + 68.f * (float)i,
                                    L::MatchCard.width - 36.f, 62.f};
                if (rank == 1) { // every co-leader's row is lit
                    DrawRectangleRounded(row, 0.3f, 8, rgba(255, 200, 110, 0.16f * ra));
                    DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, alphaMul(pal::Highlight, 0.7f * ra));
                } else {
                    DrawRectangleRounded(row, 0.3f, 8, rgba(255, 255, 255, 0.04f * ra));
                }
                const Vector2 mc{row.x + 36.f, row.y + row.height * 0.5f};
                DrawCircleV({mc.x + 1.f, mc.y + 2.f}, 22.f, rgba(0, 0, 0, 0.4f * ra));
                DrawCircleV(mc, 22.f, alphaMul(medal[std::min(rank, 4) - 1], ra));
                DrawRing(mc, 17.f, 19.f, 200.f, 290.f, 10, rgba(255, 255, 255, 0.35f * ra));
                const std::string digit = std::to_string(rank);
                const float dw = measureText(FontId::UiBold, digit, 25.f).x;
                drawTextCentered(FontId::UiBold, digit, {mc.x, mc.y - 1.f}, 25.f, alphaMul(pal::TextDark, ra));
                drawText(FontId::UiBold, ".", {mc.x + dw * 0.5f, mc.y - 15.f}, 25.f, alphaMul(pal::TextDark, ra));
                const Color nameCol = p.human ? pal::Highlight : pal::TextLight;
                drawText(FontId::UiBold, seatName(*g, s), {row.x + 76.f, row.y + 7.f}, 28.f, alphaMul(nameCol, ra));
                std::string hs;
                for (size_t h = 0; h < p.handScores.size(); ++h) hs += (h ? ", " : "") + scoreText(p.handScores[h]);
                const std::string sub = "El puanları: " + hs;
                float fs = 15.f;
                const float maxW = row.width - 76.f - 150.f;
                const float w = measureText(FontId::Ui, sub, fs).x;
                if (w > maxW) fs *= maxW / w;
                drawText(FontId::Ui, sub, {row.x + 77.f, row.y + 40.f}, fs, alphaMul(pal::TextLight, 0.55f * ra));
                std::string tot = scoreText(p.totalScore);
                if (teams && (i % 2 == 1)) tot = "= " + scoreText(g->teamTotal(s)); // the team total on its second row
                const float tw = measureText(FontId::UiBold, tot, 34.f).x;
                drawText(FontId::UiBold, tot, {row.x + row.width - 22.f - tw, row.y + 13.f}, 34.f,
                         alphaMul(rank == 1 ? pal::Highlight : pal::TextLight, ra));
            }
        }

        float nfs = 30.f;
        const std::string nlabel = autoLabel("Yeni Oyun", FontId::UiBold, L::MatchNew.width - 24.f, nfs);
        if (drawButton(L::MatchNew, nlabel, m, true, ButtonStyle::Wood, nfs)) click(C_NewGame);
        if (drawButton(L::MatchMenu, "Ana Menü", m, true, ButtonStyle::Wood, 30.f)) click(C_MatchMenu);
        if (analysisAvailable && drawButton(L::MatchAnalysis, "Hatalarım", m, true, ButtonStyle::Wood, 22.f))
            click(C_ShowAnalysis);
        drawConfetti();
    }
};

// ============================================================================ Screens (public API)

const GameInfo& gameInfo(GameKind k) { return kGames[std::clamp((int)k, 0, (int)GameKind::Count - 1)]; }

bool gameAvailable(GameKind k) { return k >= GameKind::Yuzbir && k < GameKind::Count; }

Screens::Screens() : impl_(new Impl) { impl_->owner = this; }

Screens::~Screens() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void Screens::init() {
    Impl& I = *impl_;
    I.owner = this;
    I.fadeShader.load();
    I.rulesLaidOut = false;
    I.nameBuf = I.settings.playerName;
    I.cur = I.prev = ScreenId::Title;
    I.fade = 1.f;
    I.frozen.reset();
    I.freezePending = false;
    for (float& t : I.shownAt) t = I.time;
}

void Screens::shutdown() {
    if (!impl_) return;
    impl_->fadeShader.unload();
    impl_->frozen.reset();
}

void Screens::show(ScreenId id) { impl_->show(id); }

ScreenId Screens::current() const { return impl_->cur; }

bool Screens::blocksGame() const { return impl_->cur != ScreenId::None; }

ScreenAction Screens::update(float dt, Vector2 mouse, const okey::Game* game) { return impl_->update(dt, mouse, game); }

void Screens::draw(const okey::Game* game) { impl_->draw(game); }

Settings& Screens::settings() { return impl_->settings; }

void Screens::setAiMode(bool on) { impl_->aiMode = on; }

void Screens::setAutoAdvance(float secondsLeft) { impl_->autoLeft = secondsLeft; }

void Screens::setSheet(const SheetModel& m) { impl_->sheet = m; }

void Screens::clearSheet() { impl_->sheet.reset(); }

void Screens::setStats(const StatsBook* stats) { impl_->stats = stats; }

void Screens::setReplays(const std::vector<ReplayEntry>& rows) { impl_->replays = rows; }
int Screens::chosenReplay() const { return impl_->chosen; }

void Screens::setAnalysis(bool available, bool ready, const std::string& title, const std::vector<MistakeView>& rows) {
    impl_->analysisAvailable = available;
    impl_->analysisReady = ready;
    impl_->analysisTitle = title;
    impl_->analysis = rows;
}

void Screens::setGuide(const std::string& title, const std::vector<std::string>& lines) {
    impl_->guideTitle = title;
    impl_->guideLines = lines;
}

void Screens::setResumable(bool on, const std::string& label) {
    impl_->canResume = on;
    impl_->resumeLabel = on ? label : std::string();
}

} // namespace ui
