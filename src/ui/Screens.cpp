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

std::vector<RuleBlock> buildRules() {
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
      "yeni bir perde, işlerken ya da okey alırken. Kullanamazsan *Geri Ver*: taş yerine döner, sana "
      "^101 ceza^ yazılır ve desteden çekersin. Aynı turda bir daha yandan alamazsın.");

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
    B("Yandan aldığın taşı geri vermek.");

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
    B("*Geri Ver:* yandan aldığın taşı geri verir (101 ceza).");
    B("İşlek taş ya da okey atarken oyun seni uyarır ve onay ister.");
    B("Okey taşlarının köşesinde küçük bir yıldız bulunur.");
    B("İpuçları açıkken *işlek* taşların köşesinde yeşil bir *+* görünür: bunları atarsan ^101 ceza^ yersin.");
    B("*Yapay Zeka:* *Y* tuşu ya da *Yapay Zeka* düğmesi taşlarını yapay zekaya bırakır; o senin yerine "
      "oynar, sen izlersin. Yeniden basınca kontrol, turun kaldığı yerden sana döner.");
    B("*ESC* ya da *Menü* düğmesi oyunu duraklatır; menülerde bir önceki ekrana döner.");
    return v;
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
constexpr Rectangle TitleWatchAi{660, 556, 280, 56};
constexpr Rectangle TitleRules{660, 626, 280, 56};
constexpr Rectangle TitleSettings{660, 696, 280, 56};
constexpr Rectangle TitleQuit{660, 766, 280, 56};
// settings
constexpr Rectangle SetPanel{330, 40, 940, 820};
constexpr float SetLabelX = 385.f;
constexpr float SetCtrlX = 720.f;
// name, hands, level, anim, hints, katlamalı | sfx, ambient, music
constexpr float SetRowY[9] = {212, 268, 324, 380, 436, 492, 598, 652, 706};
constexpr Rectangle SetName{720, 189, 340, 46};
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
constexpr Rectangle MatchNew{530, 736, 250, 64};
constexpr Rectangle MatchMenu{820, 736, 250, 64};
} // namespace L

enum ClickId {
    C_None = 0,
    C_Play, C_WatchAi, C_TitleRules, C_TitleSettings, C_Quit,
    C_Back, C_Defaults, C_Hands, C_Level, C_Anim, C_Sfx, C_Ambient, C_Music, C_Hints, C_Katlamali, C_Name,
    C_Resume, C_PauseAi, C_PauseRules, C_PauseSettings, C_PauseMenu, C_ConfirmYes, C_ConfirmNo,
    C_Next, C_NewGame, C_MatchMenu, C_RulesTab,
};

// Rules section tabs: label and the heading they jump to.
const char* const kRuleTabs[][2] = {
    {"Taşlar", "Taşlar"},          {"Okey", "Gösterge ve okey"}, {"Tur", "Sıra ve tur"},
    {"Perler", "Perler: seri, grup, çift"}, {"El açmak", "Elini açmak"}, {"Cezalar", "Cezalar"},
    {"Puanlama", "Puanlama"},      {"Kontroller", "Kontroller"},
};
constexpr int kRuleTabCount = 8;

constexpr int kScreenCount = 7;

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
    float toggleAnim[5] = {1, 1, 1, 1, 0}; // sfx, ambient, music, hints, katlamalı

    // rules
    std::vector<RuleBlock> rules;
    bool rulesLaidOut = false;
    float rulesContentH = 0.f;
    float scroll = 0.f, scrollTarget = 0.f;
    float tabY[kRuleTabCount] = {};   // content y of each tab's heading
    bool draggingThumb = false;
    float dragOffset = 0.f;

    // pause
    bool confirmQuit = false;
    float confirmAt = 0.f;

    // Yapay Zeka mode (an AI plays the human's seat) and the self-pressing between-hands buttons
    bool aiMode = false;
    float autoLeft = -1.f;

    // match over
    std::vector<Confetti> confetti;
    uint32_t rngState = 0x2545F491u;

    // What the pause menu, the score sheet and the final standings show, copied when they appear. App acts on
    // their buttons at once (deals the next hand, restarts the match, or leaves for the title and passes a
    // null game) while the screen is still fading out, so the fading layer is drawn from this copy.
    std::optional<okey::Game> frozen;
    bool freezePending = false;

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
    static std::vector<int> coLeaders(const okey::Game& g) {
        int best = g.player(0).totalScore;
        for (int s = 1; s < okey::NUM_PLAYERS; ++s) best = std::min(best, g.player(s).totalScore);
        std::vector<int> v;
        for (int s = 0; s < okey::NUM_PLAYERS; ++s)
            if (g.player(s).totalScore == best) v.push_back(s);
        return v;
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
            show(ScreenId::None);
            return ScreenAction::StartMatch;
        case C_WatchAi:
            show(ScreenId::None);
            return ScreenAction::StartAiMatch;
        case C_PauseAi:
            show(ScreenId::None);
            return ScreenAction::ToggleAiMode;
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
            const Settings d;
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
            if (isLastHand(g)) {
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
            if (rulesLaidOut && c.value >= 0 && c.value < kRuleTabCount)
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
        }

        // 3. animations
        if (cur == ScreenId::MatchOver || (prev == ScreenId::MatchOver && fade < 1.f)) updateConfetti(dt);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pressedId = -1;
        return act;
    }

    ScreenAction updateSettings(float dt, Vector2 m, bool clicked) {
        const bool on[5] = {settings.sfx, settings.ambient, settings.music, settings.hints, settings.katlamali};
        for (int i = 0; i < 5; ++i) toggleAnim[i] = approach(toggleAnim[i], on[i] ? 1.f : 0.f, 16.f, dt);

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
        if (rulesLaidOut) return;
        if (rules.empty()) rules = buildRules();
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
                for (int t = 0; t < kRuleTabCount; ++t)
                    if (b.text == kRuleTabs[t][1]) tabY[t] = y;
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
        if (drawButton(L::TitleWatchAi, "Yapay Zekayı İzle", m, true, ButtonStyle::Wood, 25.f)) click(C_WatchAi);
        if (drawButton(L::TitleRules, "Kurallar", m, true, ButtonStyle::Wood, 27.f)) click(C_TitleRules);
        if (drawButton(L::TitleSettings, "Ayarlar", m, true, ButtonStyle::Wood, 27.f)) click(C_TitleSettings);
        if (drawButton(L::TitleQuit, "Çıkış", m, true, ButtonStyle::Wood, 27.f)) click(C_Quit);

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
        drawNeonText("101", {0.f, 198.f}, 100.f, 12.f, neonFlicker(time));

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

    // ------------------------------------------------------------ settings
    void settingRow(int row, const char* label, const std::string& hint) {
        const float cy = L::SetRowY[row];
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

    void drawSettings(Vector2 m) {
        drawDim(0.55f);
        drawPanel(L::SetPanel, PanelStyle::Wood);
        drawHeader("Ayarlar", {800.f, 94.f}, 54.f, kGold);
        brassRule(L::SetPanel.x + 50.f, L::SetPanel.x + L::SetPanel.width - 50.f, 136.f);

        auto section = [&](const char* s, float y) {
            drawText(FontId::UiBold, s, {L::SetLabelX, y}, 17.f, alphaMul(pal::Brass, 0.9f), 4.f);
            const float w = measureText(FontId::UiBold, s, 17.f, 4.f).x;
            DrawLineEx({L::SetLabelX + w + 14.f, y + 10.f}, {L::SetPanel.x + L::SetPanel.width - 55.f, y + 10.f}, 1.f,
                       alphaMul(pal::Brass, 0.3f));
        };
        section("OYUN", 156.f);
        section("SES", 544.f);

        // player name
        settingRow(0, "Oyuncu adı", "En fazla 12 harf");
        {
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

        settingRow(1, "El sayısı", "Maç kaç el sürsün?");
        chipRow(C_Hands, {"1", "3", "5", "7", "9", "11"}, selectedHands(), L::SetRowY[1], 66.f, 12.f, m, 25.f);

        const int lvl = std::clamp(settings.difficulty, 0, 2);
        settingRow(2, "Rakip seviyesi", kLevelHints[lvl]);
        chipRow(C_Level, {kLevelNames[0], kLevelNames[1], kLevelNames[2]}, lvl, L::SetRowY[2], 146.f, 12.f, m, 24.f);

        settingRow(3, "Animasyon hızı", "Taşların ve rakiplerin hızı");
        chipRow(C_Anim, {"Yavaş", "Normal", "Hızlı"}, selectedAnim(), L::SetRowY[3], 146.f, 12.f, m, 24.f);

        settingRow(4, "İpuçları", "Geçerli perler, açma sayacı ve işlek taşlar");
        toggle(C_Hints, 3, settings.hints, L::SetRowY[4], m);
        settingRow(5, "Katlamalı oyun", "Her açan, öncekinden en az 1 fazlasıyla açar");
        toggle(C_Katlamali, 4, settings.katlamali, L::SetRowY[5], m);

        settingRow(6, "Efekt sesleri", "Taş, çay kaşığı ve düğme sesleri");
        toggle(C_Sfx, 0, settings.sfx, L::SetRowY[6], m);
        settingRow(7, "Ortam sesi", "Kalabalık, vantilatör, televizyon");
        toggle(C_Ambient, 1, settings.ambient, L::SetRowY[7], m);
        settingRow(8, "Radyo", "Türküler ve eski plaklar");
        toggle(C_Music, 2, settings.music, L::SetRowY[8], m);

        if (backSettings != ScreenId::Title) {
            drawTextWrapped(FontId::Ui, "El sayısı ve katlamalı oyun yeni maçta geçerli olur.", {640.f, 778.f, 330.f, 60.f},
                            17.f, alphaMul(pal::TextLight, 0.6f), 2.f);
        }
        if (drawButton(L::SetDefaults, "Varsayılanlar", m, true, ButtonStyle::Wood, 22.f)) click(C_Defaults);
        if (drawButton(L::SetBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_Back);
    }

    // ------------------------------------------------------------ rules
    void drawRules(Vector2 m) {
        ensureRulesLayout();
        drawDim(0.6f);
        drawPanel(L::RulesPanel, PanelStyle::Wood);
        drawHeader("Nasıl Oynanır?", {800.f, 56.f}, 46.f, kGold);
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
        if (maxScroll() > 0.f && scroll >= maxScroll() - 1.f) return kRuleTabCount - 1;
        int a = 0;
        for (int t = 0; t < kRuleTabCount; ++t)
            if (tabY[t] <= scroll + 60.f) a = t;
        return a;
    }

    void drawRuleTabs(Vector2 m) {
        const float fs = 19.f, h = 32.f, gap = 8.f, pad = 28.f;
        float total = 0.f;
        float w[kRuleTabCount];
        for (int t = 0; t < kRuleTabCount; ++t) {
            w[t] = measureText(FontId::Chalk, kRuleTabs[t][0], fs).x + pad;
            total += w[t] + (t ? gap : 0.f);
        }
        float x = 800.f - total * 0.5f;
        const int active = activeRuleTab();
        for (int t = 0; t < kRuleTabCount; ++t) {
            const Rectangle r{x, L::RulesTabsY - h * 0.5f, w[t], h};
            if (t == active) {
                DrawRectangleRounded({r.x - 2.f, r.y - 2.f, r.width + 4.f, r.height + 4.f}, 0.35f, 8,
                                     alphaMul(pal::Highlight, 0.85f));
            }
            if (drawButton(r, kRuleTabs[t][0], m, true, ButtonStyle::Chalk, fs)) click(C_RulesTab, t);
            x += w[t] + gap;
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
            const std::string s = "El " + std::to_string(std::min(g->handIndex() + 1, g->numHands())) + " / " +
                                  std::to_string(g->numHands());
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
            handText("maç: " + std::to_string(handsDone) + " / " + std::to_string(g->numHands()) + " el",
                     {labelX, S.y + 34.f}, 30.f, alphaMul(kRedPencil, 0.85f * a));
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
                line = "Taşlar bitti, eli bitiren olmadı.";
            }
            handTextCentered(line, {800.f, S.y + 118.f}, 42.f, alphaMul(kInk, a));
            std::vector<std::string> tags;
            if (r.finishedWithJoker) tags.push_back("okeyle bitiş \xC3\x97" "2");
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
                tagLine += "   (cezalar hariç tüm puanlar)";
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
                pencilLine({x, headY - 26.f}, {x, rowY0 + rowH * 3.f + 18.f}, 1.2f, alphaMul(kGraphite, 0.35f * a),
                           20u + (uint32_t)s, 0.8f);
            }
        }
        // this hand, per player. "Hesap" spells out the score before penalties, so Hesap + Ceza = Bu el can be
        // checked by eye: bitiren -101, açmayan 202, seriyle açan elde kalan, çiftle açan elde kalan x 2, and the
        // hand's multiplier (okeyle / çiftten / elden bitiş) on all of them.
        const std::string times = " \xC3\x97 ";
        const std::string multTail = r.multiplier > 1 ? times + std::to_string(r.multiplier) : std::string();
        const okey::RulesConfig& rc = g->rules();
        const char* rowLabels[4] = {"Durum", "Hesap", "Ceza", "Bu el"};
        for (int row = 0; row < 4; ++row) {
            const float cy = rowY0 + rowH * (float)row;
            const float a = ink(0.62f + 0.1f * (float)row);
            handText(rowLabels[row], {labelX, cy - 17.f}, 30.f, alphaMul(kGraphite, a));
            for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
                const okey::PlayerInfo& p = g->player(s);
                const size_t si = (size_t)s;
                Cell c;
                const bool won = s == r.winner;
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
        const float sepY = rowY0 + rowH * 3.f + 24.f;
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
        const float aNote = ink(tTot + 0.6f);
        if (aNote > 0.f) {
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
            if (!last) note += "  (düşük puan iyidir)";
            handText(note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
        }

        float bfs = 36.f;
        const std::string blabel = autoLabel(last ? "Sonuçlar" : "Sonraki El", FontId::Hand, btn.width - 22.f, bfs);
        if (drawButton(btn, blabel, m, true, ButtonStyle::Paper, bfs)) click(C_Next);
    }

    // ------------------------------------------------------------ match over
    void drawMatchOver(Vector2 m, const okey::Game* g) {
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

        drawHeader("Maç Bitti", {800.f, 56.f}, 60.f, kGold);
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
            const std::string best = scoreText(g->player(leader).totalScore);
            const bool humanFirst = humanAmong(*g, leaders);
            const float a1 = clamp01((age - 0.35f) / 0.4f);
            if (humanFirst && leaders.size() == 1) {
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
                if (g->player(a).totalScore != g->player(b).totalScore)
                    return g->player(a).totalScore < g->player(b).totalScore;
                return a < b;
            });
            static const Color medal[4] = {Color{232, 184, 64, 255}, Color{196, 198, 206, 255},
                                           Color{190, 118, 64, 255}, Color{118, 88, 62, 255}};
            int rank = 1;
            for (int i = 0; i < 4; ++i) {
                const int s = order[(size_t)i];
                const okey::PlayerInfo& p = g->player(s);
                if (i > 0 && p.totalScore != g->player(order[(size_t)i - 1]).totalScore) rank = i + 1;
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
                const std::string tot = scoreText(p.totalScore);
                const float tw = measureText(FontId::UiBold, tot, 34.f).x;
                drawText(FontId::UiBold, tot, {row.x + row.width - 22.f - tw, row.y + 13.f}, 34.f,
                         alphaMul(rank == 1 ? pal::Highlight : pal::TextLight, ra));
            }
        }

        float nfs = 30.f;
        const std::string nlabel = autoLabel("Yeni Oyun", FontId::UiBold, L::MatchNew.width - 24.f, nfs);
        if (drawButton(L::MatchNew, nlabel, m, true, ButtonStyle::Wood, nfs)) click(C_NewGame);
        if (drawButton(L::MatchMenu, "Ana Menü", m, true, ButtonStyle::Wood, 30.f)) click(C_MatchMenu);
        drawConfetti();
    }
};

// ============================================================================ Screens (public API)

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
    I.rules = buildRules();
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

} // namespace ui
