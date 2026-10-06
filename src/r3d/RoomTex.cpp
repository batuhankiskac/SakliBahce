// Room module: procedural textures (floor, walls, ceiling, glass, dust) and the static canvases (art atlas with
// pictures/signs/labels, the night street outside, the window lettering). Room owner.
#include "r3d/RoomInternal.h"
#include "ui/Common.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace r3d {
namespace rm {

// ============================================================================ helpers
Color scaleRgb(Color c, float k) {
    return Color{(unsigned char)std::clamp(c.r * k, 0.f, 255.f), (unsigned char)std::clamp(c.g * k, 0.f, 255.f),
                 (unsigned char)std::clamp(c.b * k, 0.f, 255.f), c.a};
}
Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
Color alpha(Color c, float a01) {
    c.a = (unsigned char)std::clamp(a01 * 255.f, 0.f, 255.f);
    return c;
}
static inline uint32_t hsh(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hash1(uint32_t x) { return (hsh(x) & 0xffffff) / 16777216.f; }
static inline float h2(int x, int y, uint32_t s) {
    return (hsh((uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ s * 0xcb1ab31fu) & 0xffffff) / 16777215.f;
}
float vnoise(float x, float y, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.f - 2.f * tx);
    ty = ty * ty * (3.f - 2.f * ty);
    float a = h2(ix, iy, seed), b = h2(ix + 1, iy, seed), c = h2(ix, iy + 1, seed), d = h2(ix + 1, iy + 1, seed);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}
float fbm(float x, float y, int octaves, uint32_t seed) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoise(x, y, seed + (uint32_t)o * 977u);
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}
float noise1(float t, uint32_t seed) {
    float f = std::floor(t);
    float u = t - f;
    u = u * u * (3.f - 2.f * u);
    float a = hash1((uint32_t)(int)f * 747796405u + seed), b = hash1((uint32_t)((int)f + 1) * 747796405u + seed);
    return a + (b - a) * u;
}
float smooth01(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// ---------------------------------------------------------------------------- 2D drawing
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (z > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}
void quad2(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}
void ellipse(Vector2 c, float rx, float ry, Color col, int seg) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.f * PI * i / seg, a1 = 2.f * PI * (i + 1) / seg;
        tri(c, {c.x + std::cos(a0) * rx, c.y + std::sin(a0) * ry}, {c.x + std::cos(a1) * rx, c.y + std::sin(a1) * ry},
            col);
    }
}
static void gtri(Vector2 a, Color ca, Vector2 b, Color cb, Vector2 c, Color cc) {
    float z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (z > 0) {
        std::swap(b, c);
        std::swap(cb, cc);
    }
    rlBegin(RL_TRIANGLES);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a);
    rlVertex2f(a.x, a.y);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a);
    rlVertex2f(b.x, b.y);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a);
    rlVertex2f(c.x, c.y);
    rlEnd();
}
void ellipseGrad(Vector2 c, float rx, float ry, Color inner, Color outer, int seg) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.f * PI * i / seg, a1 = 2.f * PI * (i + 1) / seg;
        gtri(c, inner, {c.x + std::cos(a0) * rx, c.y + std::sin(a0) * ry}, outer,
             {c.x + std::cos(a1) * rx, c.y + std::sin(a1) * ry}, outer);
    }
}
void beginRoomCanvas(RenderTexture2D& rt) {
    beginCanvas(rt);
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}
void endRoomCanvas(RenderTexture2D& rt) {
    EndBlendMode();
    endCanvas(rt);
}
void rectGradV(Rectangle r, Color top, Color bottom) { DrawRectangleGradientEx(r, top, bottom, bottom, top); }
void rectGradH(Rectangle r, Color left, Color right) { DrawRectangleGradientEx(r, left, left, right, right); }
void frameRect(Rectangle r, float t, Color c) { DrawRectangleLinesEx(r, t, c); }

// Chalk writing: a few jittered passes give the dusty, uneven stroke.
void chalkText(ui::FontId f, const std::string& s, Vector2 pos, float size, Color c, okey::Rng& rng) {
    for (int k = 0; k < 4; ++k) {
        Vector2 j{rng.uniform(-1.4f, 1.4f) * size / 60.f, rng.uniform(-1.2f, 1.2f) * size / 60.f};
        ui::drawText(f, s, {pos.x + j.x, pos.y + j.y}, size, alpha(c, 0.34f + 0.1f * (k == 0)));
    }
}
void chalkSmudges(Rectangle r, Color chalk, okey::Rng& rng, int n) {
    for (int i = 0; i < n; ++i) {
        Vector2 c{rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)};
        float rx = rng.uniform(0.08f, 0.3f) * r.width, ry = rx * rng.uniform(0.25f, 0.6f);
        ellipseGrad(c, rx, ry, alpha(chalk, rng.uniform(0.03f, 0.075f)), alpha(chalk, 0.f), 28);
    }
    // eraser swipes: long faint horizontal arcs
    for (int i = 0; i < n / 2 + 1; ++i) {
        float y = rng.uniform(r.y + r.height * 0.1f, r.y + r.height * 0.9f);
        float x0 = rng.uniform(r.x, r.x + r.width * 0.5f), w = rng.uniform(0.3f, 0.6f) * r.width;
        rectGradV({x0, y, w, rng.uniform(10.f, 26.f) * r.width / 1000.f + 6.f}, alpha(chalk, 0.0f),
                  alpha(chalk, rng.uniform(0.025f, 0.05f)));
    }
}
void chalkSpeckle(Rectangle r, Color board, okey::Rng& rng, int n) {
    for (int i = 0; i < n; ++i) {
        float s = rng.uniform(0.7f, 2.2f);
        DrawRectangleV({rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)}, {s, s * 0.7f},
                       alpha(board, rng.uniform(0.35f, 0.8f)));
    }
}

// ============================================================================ image textures
namespace {

Texture2D upload(Image& img) {
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// Bilinear sampler over a float grid (for low-frequency fields computed at reduced resolution).
struct Grid {
    int w = 0, h = 0;
    std::vector<float> v;
    float at(float x, float y) const {
        x = std::clamp(x, 0.f, (float)w - 1.001f);
        y = std::clamp(y, 0.f, (float)h - 1.001f);
        int ix = (int)x, iy = (int)y;
        float fx = x - ix, fy = y - iy;
        const float* p = &v[iy * w + ix];
        return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[w] * (1 - fx) + p[w + 1] * fx) * fy;
    }
};

inline Color px3(float r, float g, float b) {
    return Color{(unsigned char)std::clamp(r, 0.f, 255.f), (unsigned char)std::clamp(g, 0.f, 255.f),
                 (unsigned char)std::clamp(b, 0.f, 255.f), 255};
}

}  // namespace

// Old cement tiles (karo): quarter-circle rosettes meet at the grout crossings, a diamond in each centre.
// 1024 px = 8 x 8 tiles of 20 cm (the floor maps it every 1.6 m).
Texture2D genFloorTexture(uint32_t seed) {
    const int S = 1024, T = 128;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    // muted, decades-worn pigments (the pattern should whisper, not shout, under the tables)
    const Color cream{172, 160, 136, 255}, terra{132, 80, 58, 255}, coal{84, 76, 68, 255}, ochre{150, 122, 84, 255},
        grout{66, 60, 54, 255};
    Grid dirt;
    dirt.w = dirt.h = 129;
    dirt.v.resize(dirt.w * dirt.h);
    for (int y = 0; y < dirt.h; ++y)
        for (int x = 0; x < dirt.w; ++x) dirt.v[y * dirt.w + x] = fbm(x / 128.f * 6.f, y / 128.f * 6.f, 4, seed + 11);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            int tx = x / T, ty = y / T;
            float fx = (x % T + 0.5f) / T, fy = (y % T + 0.5f) / T;
            uint32_t tid = (uint32_t)(tx * 31 + ty * 17);
            float tileTone = 0.9f + 0.16f * hash1(tid * 7u + seed);
            float wear = 0.4f + 0.55f * hash1(tid * 13u + seed + 3);
            // corner rosettes
            float dx = std::min(fx, 1.f - fx), dy = std::min(fy, 1.f - fy);
            float dc = std::sqrt(dx * dx + dy * dy);
            float u = fx - 0.5f, v = fy - 0.5f;
            float dd = std::fabs(u) + std::fabs(v);
            Color c = cream;
            if (dc < 0.40f) c = terra;
            if (dc < 0.335f) c = cream;
            if (dc < 0.31f) c = ochre;
            if (dc < 0.17f) c = coal;
            if (dc < 0.075f) c = cream;
            if (dd < 0.19f) c = coal;
            if (dd < 0.13f) c = terra;
            if (dd < 0.05f) c = cream;
            // thin border line inside each tile
            float edge = std::min(std::min(fx, 1.f - fx), std::min(fy, 1.f - fy));
            if (edge > 0.045f && edge < 0.06f && dc > 0.42f) c = scaleRgb(coal, 1.2f);
            // worn pattern: pigment fades toward the base cement
            float fade = wear * (0.35f + 0.65f * fbm(x / 60.f, y / 60.f, 3, seed + 5));
            c = mix(c, cream, fade * 0.6f);
            float grain = (h2(x, y, seed + 7) - 0.5f) * 0.10f;
            float speck = h2(x * 3 + 1, y * 5 + 2, seed + 9) > 0.985f ? -0.18f : 0.f;
            float dk = dirt.at(x / 8.f, y / 8.f);
            float k = tileTone * (1.f + grain + speck) * (1.05f - 0.42f * dk * dk);
            // chipped / dirty tile edges and grout
            float e = std::min(std::min(x % T, T - 1 - x % T), std::min(y % T, T - 1 - y % T));
            if (e < 2.f) {
                c = grout;
                k *= 0.75f + 0.25f * h2(x, y, seed + 21);
            } else if (e < 6.f) {
                k *= 0.8f + 0.2f * (e / 6.f) + 0.06f * h2(x / 2, y / 2, seed + 23);
            }
            px[y * S + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    // a few hairline cracks across tiles
    okey::Rng rng(seed * 7 + 1);
    for (int i = 0; i < 7; ++i) {
        Vector2 p{rng.uniform(0.f, (float)S), rng.uniform(0.f, (float)S)};
        float ang = rng.uniform(0.f, 2.f * PI);
        int n = rng.range(10, 26);
        for (int s = 0; s < n; ++s) {
            ang += rng.uniform(-0.6f, 0.6f);
            Vector2 q{p.x + std::cos(ang) * 6.f, p.y + std::sin(ang) * 6.f};
            ImageDrawLineV(&img, p, q, Color{40, 34, 30, 255});
            p = q;
        }
    }
    return upload(img);
}

WallFrame wallFrame(int wall) {
    using namespace w3d;
    switch (wall) {
    case 0: return {{ROOM_X0, 0, ROOM_Z0}, {1, 0, 0}, {0, 0, 1}, ROOM_X1 - ROOM_X0};
    case 1: return {{ROOM_X1, 0, ROOM_Z1}, {-1, 0, 0}, {0, 0, -1}, ROOM_X1 - ROOM_X0};
    case 2: return {{ROOM_X0, 0, ROOM_Z1}, {0, 0, -1}, {1, 0, 0}, ROOM_Z1 - ROOM_Z0};
    default: return {{ROOM_X1, 0, ROOM_Z0}, {0, 0, 1}, {-1, 0, 0}, ROOM_Z1 - ROOM_Z0};
    }
}

// Nicotine-yellowed wallpaper, one unique 2048 x 512 row per wall (y from the wainscot to the ceiling).
Texture2D genWallAtlas(uint32_t seed, const std::vector<Opening>& openings, const std::vector<WallMark>& marks) {
    const int S = WALL_TEX, RH = WALL_ROW;
    const float H = w3d::CEILING_Y - WAINSCOT_H;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    const Color paper{202, 170, 112, 255}, smoke{104, 74, 44, 255};
    for (int wall = 0; wall < 4; ++wall) {
        const float L = wallFrame(wall).length;
        const float mpp = L / S;  // metres per pixel (horizontal)
        Grid blot;
        blot.w = S / 4 + 1;
        blot.h = RH / 4 + 1;
        blot.v.resize(blot.w * blot.h);
        for (int y = 0; y < blot.h; ++y)
            for (int x = 0; x < blot.w; ++x) {
                float a = x * 4 * mpp, yy = y * 4.f / RH * H;
                blot.v[y * blot.w + x] =
                    fbm(a * 1.3f, yy * 1.3f, 5, seed + wall * 101u) * 0.7f + fbm(a * 5.f, yy * 5.f, 2, seed + wall * 7u) * 0.3f;
            }
        const float seamPitch = 0.53f, seamOff = hash1(seed + wall) * 0.5f;
        for (int py = 0; py < RH; ++py) {
            const float y = w3d::CEILING_Y - (py + 0.5f) / RH * H;
            const float topSmoke = smooth01(1.45f, 3.1f, y);
            for (int x = 0; x < S; ++x) {
                const float a = (x + 0.5f) * mpp;
                // wallpaper: soft vertical stripes, pinstripes, tiny diamonds
                float s = std::fmod(a + 10.f, 0.064f) / 0.064f;
                float k = s < 0.5f ? 1.0f : 0.955f;
                if (std::fabs(s - 0.5f) < 0.035f || s < 0.035f) k *= 0.9f;
                float my = std::fmod(y + 10.f, 0.09f) / 0.09f;
                float dm = std::fabs(s - 0.75f) * 2.2f + std::fabs(my - 0.5f) * 1.4f;
                if (dm < 0.18f) k *= 1.05f;
                // seams
                float sp = std::fmod(a + seamOff, seamPitch);
                if (sp < mpp * 1.2f) k *= 0.78f;
                else if (sp < mpp * 2.4f) k *= 1.06f;
                float b = blot.at(x / 4.f, py / 4.f);
                Color c = mix(paper, smoke, topSmoke * 0.62f + std::max(0.f, b - 0.52f) * 0.9f);
                k *= 1.04f - 0.30f * b + (h2(x, py + wall * 1000, seed) - 0.5f) * 0.06f;
                // darker in the corners, under the cornice and just above the rail
                k *= 1.f - 0.35f * std::exp(-a / 0.18f) - 0.35f * std::exp(-(L - a) / 0.18f);
                k *= 1.f - 0.35f * std::exp(-(w3d::CEILING_Y - y) / 0.10f);
                k *= 1.f - 0.25f * std::exp(-(y - WAINSCOT_H) / 0.05f);
                px[(wall * RH + py) * S + x] = px3(c.r * k, c.g * k, c.b * k);
            }
        }
        // grime around openings
        for (const Opening& o : openings) {
            if (o.wall != wall) continue;
            int x0 = std::max(0, (int)((o.a0 - 0.35f) / mpp)), x1 = std::min(S - 1, (int)((o.a1 + 0.35f) / mpp));
            for (int py = 0; py < RH; ++py) {
                float y = w3d::CEILING_Y - (py + 0.5f) / RH * H;
                for (int x = x0; x <= x1; ++x) {
                    float a = (x + 0.5f) * mpp;
                    float dx = std::max(std::max(o.a0 - a, a - o.a1), 0.f), dy = std::max(std::max(o.y0 - y, y - o.y1), 0.f);
                    float d = std::sqrt(dx * dx + dy * dy);
                    float g = 0.22f * std::exp(-d / 0.08f) * (0.6f + 0.4f * h2(x / 3, py / 3, seed + 31));
                    Color& p = px[(wall * RH + py) * S + x];
                    p = scaleRgb(p, 1.f - g);
                }
            }
        }
        // marks
        for (const WallMark& m : marks) {
            if (m.wall != wall) continue;
            float margin = m.kind == 2 ? 0.9f : 0.3f;
            int x0 = std::max(0, (int)((m.a0 - margin) / mpp)), x1 = std::min(S - 1, (int)((m.a1 + margin) / mpp));
            for (int py = 0; py < RH; ++py) {
                float y = w3d::CEILING_Y - (py + 0.5f) / RH * H;
                if (m.kind != 2 && (y < m.y0 - margin || y > m.y1 + margin)) continue;
                for (int x = x0; x <= x1; ++x) {
                    float a = (x + 0.5f) * mpp;
                    Color& p = px[(wall * RH + py) * S + x];
                    float cx = (m.a0 + m.a1) * 0.5f;
                    if (m.kind == 0) {  // soft contact shadow around a frame
                        float dx = std::max(std::max(m.a0 - a, a - m.a1), 0.f);
                        float dy = std::max(std::max(m.y0 - y, y - m.y1), 0.f);
                        float d = std::sqrt(dx * dx + dy * dy * 1.6f);
                        float inside = (dx == 0.f && dy == 0.f) ? 1.f : 0.f;
                        p = scaleRgb(p, 1.f - (inside ? 0.45f : 0.4f * std::exp(-d / 0.045f)) * (y < m.y0 ? 1.25f : 1.f));
                    } else if (m.kind == 1) {  // ghost of a removed picture: cleaner paper, faint outline
                        if (a > m.a0 && a < m.a1 && y > m.y0 && y < m.y1) {
                            float e = std::min(std::min(a - m.a0, m.a1 - a), std::min(y - m.y0, m.y1 - y));
                            float lift = e < 0.012f ? 0.9f : 1.13f;
                            p = px3(p.r * lift, p.g * lift * 1.01f, p.b * lift * 1.06f);
                        }
                    } else if (m.kind == 2) {  // soot plume rising from (cx, y0)
                        if (y < m.y0) continue;
                        float h = y - m.y0;
                        float w = (m.a1 - m.a0) * 0.5f + h * 0.35f;
                        float t = std::fabs(a - cx) / w;
                        float s = std::exp(-t * t * 2.5f) * std::exp(-h / 1.3f) * 0.75f;
                        s *= 0.75f + 0.25f * h2(x / 4, py / 4, seed + 41);
                        p = mix(p, rgba(38, 26, 18), s);
                    } else {  // water stain with a tide-mark rim
                        float rx = (m.a1 - m.a0) * 0.5f, ry = (m.y1 - m.y0) * 0.5f;
                        float cy = (m.y0 + m.y1) * 0.5f;
                        float nx = (a - cx) / rx, ny = (y - cy) / ry;
                        float r = std::sqrt(nx * nx + ny * ny) + (fbm(a * 6.f, y * 6.f, 3, seed + 55) - 0.5f) * 0.55f;
                        if (r < 1.f) {
                            float rim = smooth01(0.8f, 0.97f, r) * (1.f - smooth01(0.97f, 1.0f, r));
                            p = mix(p, rgba(120, 84, 40), 0.18f + rim * 0.45f);
                        }
                    }
                }
            }
        }
    }
    // cracks: random walks, dark core with a lighter lip
    okey::Rng rng(seed ^ 0xC4AC4u);
    for (int i = 0; i < 16; ++i) {
        int wall = i % 4;
        Vector2 p{rng.uniform(40.f, S - 40.f), (float)(wall * RH) + rng.uniform(4.f, 140.f)};
        float ang = PI * 0.5f + rng.uniform(-0.7f, 0.7f);
        int n = rng.range(8, 30);
        for (int s = 0; s < n; ++s) {
            ang += rng.uniform(-0.5f, 0.5f);
            ang = std::clamp(ang, 0.3f, PI - 0.3f);
            Vector2 q{p.x + std::cos(ang) * 5.f, p.y + std::sin(ang) * 5.f};
            if (q.y > (wall + 1) * RH - 2) break;
            ImageDrawLineV(&img, {p.x + 1, p.y}, {q.x + 1, q.y}, Color{214, 186, 132, 255});
            ImageDrawLineV(&img, p, q, Color{70, 50, 30, 255});
            if (rng.chance(0.12f)) {  // branch
                Vector2 r2{q.x + rng.uniform(-12.f, 12.f), q.y + rng.uniform(4.f, 14.f)};
                ImageDrawLineV(&img, q, r2, Color{90, 66, 40, 255});
            }
            p = q;
        }
    }
    return upload(img);
}

Texture2D genCeilingTexture(uint32_t seed) {
    const int S = 512;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float)x / S, v = (float)y / S;
            // tileable: sample on a torus via periodic lattice of 8
            float n = 0.f, amp = 0.5f, norm = 0.f;
            for (int o = 0; o < 4; ++o) {
                int per = 4 << o;
                float fx = u * per, fy = v * per;
                int ix = (int)fx, iy = (int)fy;
                float tx = fx - ix, ty = fy - iy;
                tx = tx * tx * (3 - 2 * tx);
                ty = ty * ty * (3 - 2 * ty);
                auto H = [&](int a, int b) { return h2(a % per, b % per, seed + o * 17u); };
                float a0 = H(ix, iy), b0 = H(ix + 1, iy), c0 = H(ix, iy + 1), d0 = H(ix + 1, iy + 1);
                n += amp * (a0 + (b0 - a0) * tx + (c0 - a0) * ty + (a0 - b0 - c0 + d0) * tx * ty);
                norm += amp;
                amp *= 0.5f;
            }
            n /= norm;
            float k = 0.86f + 0.3f * n + (h2(x, y, seed + 3) - 0.5f) * 0.05f;
            px[y * S + x] = px3(150 * k, 124 * k, 88 * k);
        }
    return upload(img);
}

// Fogged window glass, one texture per pane (u, v = 0..1 across the glass, v = 0 at the top): the mist creeps in
// from the frame and pools toward the bottom, with droplets and clear runnels; the middle of the pane stays nearly
// clear, so the night street reads through it.
Texture2D genCondensationTexture(uint32_t seed) {
    const int W = 256, H = 512;
    Image img = GenImageColor(W, H, Color{0, 0, 0, 0});
    Color* px = (Color*)img.data;
    okey::Rng rng(seed);
    std::vector<float> a(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float edge = std::min(std::min(u, 1.f - u) * 0.8f, std::min(v, 1.f - v));  // distance to the frame (~aspect)
            float mist = 0.02f + 0.24f * (1.f - smooth01(0.f, 0.16f, edge)) + 0.2f * smooth01(0.6f, 1.f, v) +
                         (fbm(x / 40.f, y / 40.f, 4, seed) - 0.5f) * 0.14f;
            a[y * W + x] = std::clamp(mist, 0.01f, 0.45f);
        }
    // runnels: a drop slid down and cleared the mist
    for (int i = 0; i < 22; ++i) {
        float x = rng.uniform(4.f, W - 4.f), y = rng.uniform(0.f, H * 0.6f);
        float len = rng.uniform(60.f, 300.f), w = rng.uniform(1.2f, 2.6f);
        for (float t = 0; t < len; t += 1.f) {
            int yy = (int)(y + t);
            if (yy >= H) break;
            x += rng.uniform(-0.35f, 0.35f);
            for (int dx = -4; dx <= 4; ++dx) {
                int xx = (int)x + dx;
                if (xx < 0 || xx >= W) continue;
                float d = std::fabs(dx) / w;
                if (d < 1.f) a[yy * W + xx] *= 0.12f + 0.88f * d;
            }
        }
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float al = a[y * W + x];
            px[y * W + x] = Color{176, 186, 198, (unsigned char)(al * 255.f)};
        }
    // droplets: bright rim, dark clear centre
    for (int i = 0; i < 420; ++i) {  // they bead where the mist is (edges, bottom), not in the clear middle
        float cx = rng.uniform(0.f, (float)W), cy = rng.uniform(0.f, (float)H);
        if (rng.uniform(0.f, 1.f) > std::min(1.f, a[std::min((int)cy, H - 1) * W + std::min((int)cx, W - 1)] * 3.f)) continue;
        float r = rng.uniform(0.8f, 2.8f) * (0.6f + 0.6f * cy / H);
        for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); ++y)
            for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); ++x) {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
                if (d > 1.f) continue;
                Color& p = px[y * W + x];
                if (d > 0.7f) p = Color{214, 222, 232, 90};
                else p = Color{150, 160, 176, 20};
            }
    }
    return upload(img);
}

// Machine lace for the half curtains (u repeats across the width, v = top..bottom of the curtain).
Texture2D genLaceTexture(uint32_t seed) {
    const int W = 256, H = 512;
    Image img = GenImageColor(W, H, Color{255, 250, 240, 0});
    Color* px = (Color*)img.data;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            // fine net: two diagonal thread families
            float d1 = std::fmod((x + y) * 0.25f, 1.f), d2 = std::fmod((x - y + 4096) * 0.25f, 1.f);
            float net = 0.12f + 0.18f * (std::fabs(d1 - 0.5f) > 0.38f ? 1.f : 0.f) + 0.18f * (std::fabs(d2 - 0.5f) > 0.38f ? 1.f : 0.f);
            float a = net;
            // flowers: rings of petals on a staggered lattice, a denser vine band above the hem
            for (int row = 0; row < 3; ++row) {
                float cy = 0.2f + row * 0.24f, cx = std::fmod(0.25f + (row % 2) * 0.5f, 1.f);
                float dx = u - cx;
                dx -= std::round(dx);
                float dy = (v - cy) * 0.5f;
                float r = std::sqrt(dx * dx + dy * dy);
                float ang = std::atan2(dy, dx);
                float petal = 0.07f + 0.018f * std::cos(ang * 6.f);
                if (r < petal && r > petal - 0.016f) a = 0.9f;
                if (r < 0.02f) a = 0.85f;
                if (r < petal - 0.016f && r > 0.02f) a = std::max(a, 0.45f);
            }
            float vine = 0.84f + 0.015f * std::sin(u * 2.f * PI * 4.f);
            if (std::fabs(v - vine) < 0.006f) a = 0.9f;
            if (v > 0.8f && v < 0.9f) a = std::max(a, 0.3f);
            // heading tape and a scalloped hem
            if (v < 0.035f) a = 0.95f;
            float sc = 0.93f + 0.05f * std::sqrt(std::max(0.f, 1.f - std::pow((std::fmod(u * 8.f, 1.f) - 0.5f) * 2.f, 2.f)));
            if (v > sc) a = 0.f;
            else if (v > sc - 0.012f) a = 0.95f;
            a *= 0.92f + 0.08f * h2(x, y, seed);
            px[y * W + x].a = (unsigned char)std::clamp(a * 255.f, 0.f, 255.f);
        }
    return upload(img);
}

// Soft light shaft: bright core, dusty speckles, fading downward.
Texture2D genDustTexture(uint32_t seed) {
    const int W = 128, H = 256;
    Image img = GenImageColor(W, H, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float u = (x + 0.5f) / W * 2.f - 1.f, v = (y + 0.5f) / H;
            float chord = std::sqrt(std::max(0.f, 1.f - u * u));
            float fall = std::pow(1.f - v, 1.6f) * smooth01(0.f, 0.06f, v);
            float dust = 0.8f + 0.4f * fbm(x / 10.f, y / 18.f, 3, seed);
            float a = chord * chord * fall * dust;
            if (h2(x, y, seed + 5) > 0.992f) a = std::min(1.f, a * 2.6f + 0.1f * fall);
            px[y * W + x] = Color{255, 255, 255, (unsigned char)std::clamp(a * 255.f, 0.f, 255.f)};
        }
    return upload(img);
}

Texture2D genVarnishTexture(uint32_t seed) {
    const int S = 256;
    Image img = GenImageColor(S, S, WHITE);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float streak = fbm(x / 64.f, y / 3.f, 3, seed);
            float k = 0.86f + 0.2f * streak + (h2(x, y, seed + 1) - 0.5f) * 0.05f;
            px[y * S + x] = px3(255 * k, 250 * k, 244 * k);
        }
    return upload(img);
}

// Periodic value noise on a (px x py) lattice: tiles seamlessly when x, y wrap at px, py.
static float pnoise(float x, float y, int px, int py, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.f - 2.f * tx);
    ty = ty * ty * (3.f - 2.f * ty);
    auto H = [&](int a, int b) { return h2(((a % px) + px) % px, ((b % py) + py) % py, seed); };
    float a = H(ix, iy), b = H(ix + 1, iy), c = H(ix, iy + 1), d = H(ix + 1, iy + 1);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

Texture2D genBoardTexture(int size, Color light, Color dark, uint32_t seed) {
    const int S = size;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const float u = (x + 0.5f) / S, v = (y + 0.5f) / S;
            // growth rings seen edge-on: nearly straight lines along u that wander slowly
            float wob = (pnoise(u * 3.f, v * 2.f, 3, 2, seed + 1) - 0.5f) * 0.09f + (pnoise(u * 7.f, v * 5.f, 7, 5, seed + 2) - 0.5f) * 0.025f;
            float r = (v + wob) * 22.f;
            float fr = r - std::floor(r);
            float late = smooth01(0.55f, 0.8f, fr) * (1.f - smooth01(0.88f, 1.f, fr));  // darker latewood band
            // fine fibres: noise stretched hard along the grain
            float fib = pnoise(u * 4.f, v * 160.f, 4, 160, seed + 3) * 0.6f + pnoise(u * 9.f, v * 320.f, 9, 320, seed + 4) * 0.4f;
            // board-to-board / sap colour drift
            float drift = pnoise(u * 2.f, v * 3.f, 2, 3, seed + 5);
            // pores: short dark dashes along the grain
            float pore = (h2((int)(u * S / 9.f), (int)(v * S / 1.5f), seed + 6) > 0.93f) ? 1.f : 0.f;
            float t = 0.34f * late + 0.34f * fib + 0.22f * drift + 0.16f * pore;
            Color c = mix(light, dark, std::clamp(t, 0.f, 1.f));
            float k = 0.97f + (h2(x, y, seed + 7) - 0.5f) * 0.05f;
            px[y * S + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    return upload(img);
}

// ============================================================================ art atlas (static canvas)
namespace {

using ui::FontId;

void textC(FontId f, const std::string& s, Vector2 c, float size, Color col, float spacing = 0.f) {
    ui::drawTextCentered(f, s, c, size, col, spacing);
}
Vector2 ctr(Rectangle r) { return {r.x + r.width * 0.5f, r.y + r.height * 0.5f}; }
Rectangle inset(Rectangle r, float d) { return {r.x + d, r.y + d, r.width - 2 * d, r.height - 2 * d}; }

// Painterly overlay: short translucent strokes that break up flat fills.
void brushStrokes(Rectangle r, okey::Rng& rng, int n, float len, float alphaMax) {
    for (int i = 0; i < n; ++i) {
        Vector2 p{rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)};
        float ang = rng.uniform(-0.4f, 0.4f);
        float l = rng.uniform(0.4f, 1.f) * len;
        Color c = rng.chance(0.5f) ? Color{255, 240, 210, 0} : Color{20, 10, 30, 0};
        c.a = (unsigned char)(rng.uniform(0.2f, 1.f) * alphaMax * 255.f);
        DrawLineEx(p, {p.x + std::cos(ang) * l, p.y + std::sin(ang) * l}, rng.uniform(1.5f, 4.f), c);
    }
}
void filmGrain(Rectangle r, okey::Rng& rng, int n, float a) {
    for (int i = 0; i < n; ++i) {
        Color c = rng.chance(0.5f) ? Color{255, 250, 235, 0} : Color{0, 0, 0, 0};
        c.a = (unsigned char)(rng.uniform(0.3f, 1.f) * a * 255.f);
        DrawRectangleV({rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)}, {1.5f, 1.5f}, c);
    }
}
void vignette(Rectangle r, Color c, float edge) {
    rectGradV({r.x, r.y, r.width, r.height * edge}, c, alpha(c, 0.f));
    rectGradV({r.x, r.y + r.height * (1 - edge), r.width, r.height * edge}, alpha(c, 0.f), c);
    rectGradH({r.x, r.y, r.width * edge, r.height}, c, alpha(c, 0.f));
    rectGradH({r.x + r.width * (1 - edge), r.y, r.width * edge, r.height}, alpha(c, 0.f), c);
}

void drawPriceBoard(Rectangle r, okey::Rng& rng) {
    const Color board{34, 46, 40, 255}, chalk{236, 232, 216, 255};
    DrawRectangleRec(r, board);
    rectGradV(r, alpha(Color{60, 72, 60, 255}, 0.4f), alpha(Color{10, 16, 12, 255}, 0.4f));
    chalkSmudges(r, chalk, rng, 9);
    // The board is seen from about 4.5 m away (a fifth of its canvas size on screen), so the prices are written
    // large and pressed hard: a second, slightly offset pass thickens every stroke enough to survive mipmapping.
    auto boldChalk = [&](const char* s, Vector2 p, float size) {
        chalkText(FontId::Chalk, s, p, size, chalk, rng);
        chalkText(FontId::Chalk, s, {p.x + size * 0.025f, p.y + size * 0.012f}, size, chalk, rng);
    };
    boldChalk("FİYAT LİSTESİ", {r.x + 146, r.y + 18}, 54);
    DrawLineEx({r.x + 140, r.y + 80}, {r.x + 500, r.y + 84}, 4.f, alpha(chalk, 0.6f));
    const char* items[4] = {"Çay", "Oralet", "Kahve", "Soda"};
    const char* prices[4] = {"15 TL", "15 TL", "40 TL", "20 TL"};
    const float fs = 72.f;
    for (int i = 0; i < 4; ++i) {
        float y = r.y + 96 + i * 80.f;
        boldChalk(items[i], {r.x + 40, y}, fs);
        Vector2 pm = ui::measureText(FontId::Chalk, prices[i], fs);
        boldChalk(prices[i], {r.x + r.width - 40 - pm.x, y}, fs);
        float x0 = r.x + 40 + ui::measureText(FontId::Chalk, items[i], fs).x + 14, x1 = r.x + r.width - 54 - pm.x;
        for (float x = x0; x < x1; x += 17.f) ellipse({x, y + fs * 0.77f}, 3.f, 2.6f, alpha(chalk, 0.65f), 8);
    }
    // a little chalk doodle of an ince belli glass
    Vector2 g{r.x + 70, r.y + 50};
    Color dc = alpha(chalk, 0.6f);
    DrawLineEx({g.x - 12, g.y - 26}, {g.x - 7, g.y - 6}, 2.5f, dc);
    DrawLineEx({g.x - 7, g.y - 6}, {g.x - 12, g.y + 14}, 2.5f, dc);
    DrawLineEx({g.x + 12, g.y - 26}, {g.x + 7, g.y - 6}, 2.5f, dc);
    DrawLineEx({g.x + 7, g.y - 6}, {g.x + 12, g.y + 14}, 2.5f, dc);
    DrawLineEx({g.x - 12, g.y + 14}, {g.x + 12, g.y + 14}, 2.5f, dc);
    DrawEllipseLinesV({g.x, g.y + 18}, 22, 5, dc);
    for (int k = 0; k < 3; ++k)
        DrawLineEx({g.x - 4 + k * 5.f, g.y - 32}, {g.x - 1 + k * 5.f, g.y - 44}, 2.f, alpha(chalk, 0.4f));
    chalkSpeckle(r, board, rng, 5200);
}

void drawClockFace(Rectangle r) {
    Vector2 c = ctr(r);
    float R = r.width * 0.5f;
    DrawRectangleRec(r, Color{40, 30, 20, 255});
    DrawCircleGradient(c, R, Color{242, 232, 206, 255}, Color{206, 190, 152, 255});
    DrawRing(c, R * 0.93f, R * 0.95f, 0, 360, 96, Color{60, 50, 40, 255});
    for (int i = 0; i < 60; ++i) {
        float a = i / 60.f * 2 * PI;
        Vector2 d{std::sin(a), -std::cos(a)};
        float l = (i % 5 == 0) ? 0.1f : 0.045f, w = (i % 5 == 0) ? 7.f : 2.5f;
        DrawLineEx({c.x + d.x * R * 0.9f, c.y + d.y * R * 0.9f}, {c.x + d.x * R * (0.9f - l), c.y + d.y * R * (0.9f - l)}, w,
                   Color{34, 28, 24, 255});
    }
    for (int h = 1; h <= 12; ++h) {
        float a = h / 12.f * 2 * PI;
        char buf[4];
        std::snprintf(buf, sizeof buf, "%d", h);
        textC(FontId::Sign, buf, {c.x + std::sin(a) * R * 0.66f, c.y - std::cos(a) * R * 0.66f}, 62, Color{30, 24, 20, 255});
    }
    textC(FontId::Sign, "VAKİT", {c.x, c.y - R * 0.3f}, 26, Color{120, 30, 24, 255}, 3.f);
    textC(FontId::Ui, "İSTANBUL", {c.x, c.y + R * 0.3f}, 18, Color{90, 80, 66, 255}, 3.f);
    // aged: faint yellowing at the rim
    DrawRing(c, R * 0.7f, R * 0.93f, 0, 360, 96, Color{140, 110, 50, 26});
}

void drawSkyline(Rectangle r, float base, Color col, float sc) {
    // Galata tower
    float gx = r.x + r.width * 0.2f;
    DrawRectangleRec({gx - 13 * sc, base - 110 * sc, 26 * sc, 110 * sc}, col);
    tri({gx - 17 * sc, base - 108 * sc}, {gx + 17 * sc, base - 108 * sc}, {gx, base - 152 * sc}, col);
    DrawRectangleRec({gx - 16 * sc, base - 114 * sc, 32 * sc, 6 * sc}, col);
    // houses
    okey::Rng rng(9);
    for (float x = r.x; x < r.x + r.width; x += 18 * sc) {
        float h = rng.uniform(18.f, 46.f) * sc;
        DrawRectangleRec({x, base - h, 19 * sc, h}, col);
    }
    // mosque: main dome, half domes, minarets
    float mx = r.x + r.width * 0.64f;
    DrawRectangleRec({mx - 70 * sc, base - 50 * sc, 140 * sc, 50 * sc}, col);
    DrawCircleSector({mx, base - 50 * sc}, 48 * sc, 180, 360, 32, col);
    DrawCircleSector({mx - 55 * sc, base - 44 * sc}, 26 * sc, 180, 360, 24, col);
    DrawCircleSector({mx + 55 * sc, base - 44 * sc}, 26 * sc, 180, 360, 24, col);
    DrawRectangleRec({mx - 2 * sc, base - 108 * sc, 4 * sc, 14 * sc}, col);
    for (int k = -1; k <= 1; k += 2)
        for (int j = 0; j < 2; ++j) {
            float x = mx + k * (84 + j * 26) * sc;
            float h = (j == 0 ? 150 : 124) * sc;
            DrawRectangleRec({x - 3.5f * sc, base - h, 7 * sc, h}, col);
            tri({x - 4.5f * sc, base - h}, {x + 4.5f * sc, base - h}, {x, base - h - 22 * sc}, col);
            DrawRectangleRec({x - 6 * sc, base - h * 0.72f, 12 * sc, 3 * sc}, col);
        }
}

void drawBosphorus(Rectangle r, okey::Rng& rng) {
    float horizon = r.y + r.height * 0.62f;
    rectGradV({r.x, r.y, r.width, horizon - r.y}, Color{48, 36, 86, 255}, Color{236, 128, 58, 255});
    rectGradV({r.x, r.y + (horizon - r.y) * 0.55f, r.width, (horizon - r.y) * 0.45f}, Color{200, 90, 80, 0},
              Color{255, 176, 90, 180});
    DrawCircleGradient({r.x + r.width * 0.42f, horizon - 28}, 60, Color{255, 226, 150, 255}, Color{255, 170, 80, 0});
    ellipse({r.x + r.width * 0.42f, horizon - 22}, 22, 22, Color{255, 232, 170, 255});
    drawSkyline(r, horizon + 4, Color{36, 22, 40, 255}, 1.f);
    rectGradV({r.x, horizon + 4, r.width, r.y + r.height - horizon - 4}, Color{120, 60, 60, 255}, Color{26, 26, 54, 255});
    for (int i = 0; i < 120; ++i) {  // sun glitter on the water
        float y = rng.uniform(horizon + 6, r.y + r.height - 4);
        float spread = (y - horizon) * 0.9f + 10;
        float x = r.x + r.width * 0.42f + rng.uniform(-spread, spread);
        DrawRectangleV({x, y}, {rng.uniform(6.f, 18.f), 2.f}, Color{255, 190, 110, (unsigned char)rng.range(60, 160)});
    }
    // ferry (vapur)
    float fx = r.x + r.width * 0.72f, fy = r.y + r.height * 0.82f;
    quad2({fx - 70, fy}, {fx + 74, fy}, {fx + 64, fy + 14}, {fx - 62, fy + 14}, Color{20, 14, 20, 255});
    DrawRectangleRec({fx - 52, fy - 16, 100, 16}, Color{210, 190, 170, 255});
    DrawRectangleRec({fx - 40, fy - 26, 70, 10}, Color{220, 200, 180, 255});
    for (int k = 0; k < 9; ++k) DrawRectangleRec({fx - 46 + k * 11.f, fy - 12, 6, 5}, Color{255, 210, 120, 255});
    DrawRectangleRec({fx - 4, fy - 46, 12, 22}, Color{30, 26, 30, 255});
    DrawRectangleRec({fx - 4, fy - 42, 12, 5}, Color{240, 240, 240, 255});
    for (int k = 0; k < 5; ++k)
        ellipse({fx + 6 - k * 14.f, fy - 58 - k * 7.f}, 9.f + k * 3, 6.f + k * 2, Color{90, 70, 80, (unsigned char)(150 - k * 25)});
    for (int k = 0; k < 3; ++k) {  // gulls
        Vector2 g{r.x + 80 + k * 46.f, r.y + 60 + (k % 2) * 22.f};
        DrawLineEx({g.x - 9, g.y - 3}, g, 2.2f, Color{40, 30, 50, 255});
        DrawLineEx(g, {g.x + 9, g.y - 4}, 2.2f, Color{40, 30, 50, 255});
    }
    brushStrokes(r, rng, 900, 22.f, 0.10f);
    vignette(r, Color{20, 10, 0, 90}, 0.12f);
}

void drawCalendar(Rectangle r, const std::tm& lt) {
    static const char* months[12] = {"OCAK",  "ŞUBAT", "MART",   "NİSAN",  "MAYIS", "HAZİRAN",
                                     "TEMMUZ", "AĞUSTOS", "EYLÜL", "EKİM", "KASIM", "ARALIK"};
    static const char* days[7] = {"PAZAR", "PAZARTESİ", "SALI", "ÇARŞAMBA", "PERŞEMBE", "CUMA", "CUMARTESİ"};
    DrawRectangleRec(r, Color{234, 228, 214, 255});
    // picture: Cappadocia balloons at dawn
    Rectangle pic{r.x + 10, r.y + 26, r.width - 20, r.height * 0.42f};
    rectGradV(pic, Color{120, 160, 210, 255}, Color{250, 200, 150, 255});
    for (int k = 0; k < 4; ++k) {
        float x = pic.x + 30 + k * 60.f, y = pic.y + pic.height - 20 - (k % 3) * 8.f;
        tri({x - 30, y + 20}, {x + 10, y - 26 - k * 6.f}, {x + 40, y + 20}, Color{190, 130, 90, 255});
    }
    DrawRectangleRec({pic.x, pic.y + pic.height - 14, pic.width, 14}, Color{170, 110, 76, 255});
    Color bal[3] = {{220, 60, 50, 255}, {240, 190, 50, 255}, {60, 120, 200, 255}};
    Vector2 bp[3] = {{pic.x + 60, pic.y + 50}, {pic.x + 150, pic.y + 32}, {pic.x + 200, pic.y + 90}};
    float bs[3] = {22, 16, 11};
    for (int k = 0; k < 3; ++k) {
        ellipse(bp[k], bs[k], bs[k] * 1.15f, bal[k]);
        DrawRectangleRec({bp[k].x - bs[k] * 0.25f, bp[k].y + bs[k] * 1.25f, bs[k] * 0.5f, bs[k] * 0.35f},
                         Color{90, 60, 40, 255});
    }
    DrawRectangleRec({r.x, r.y, r.width, 20}, Color{160, 30, 30, 255});
    textC(FontId::Sign, "TAKVİM", {r.x + r.width / 2, r.y + 10}, 16, Color{250, 230, 200, 255}, 2.f);
    // tear-off block
    Rectangle pad{r.x + 16, pic.y + pic.height + 12, r.width - 32, r.y + r.height - pic.y - pic.height - 26};
    for (int k = 4; k >= 0; --k) DrawRectangleRec({pad.x + k, pad.y + k * 1.5f, pad.width, pad.height}, Color{200, 194, 180, 255});
    DrawRectangleRec(pad, Color{248, 246, 238, 255});
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s %d", months[lt.tm_mon % 12], lt.tm_year + 1900);
    textC(FontId::Sign, buf, {pad.x + pad.width / 2, pad.y + 22}, 24, Color{40, 40, 44, 255});
    std::snprintf(buf, sizeof buf, "%d", lt.tm_mday);
    Color dayCol = lt.tm_wday == 0 ? Color{190, 30, 30, 255} : Color{30, 30, 34, 255};
    textC(FontId::Sign, buf, {pad.x + pad.width / 2, pad.y + pad.height * 0.55f}, 118, dayCol);
    textC(FontId::Sign, days[lt.tm_wday % 7], {pad.x + pad.width / 2, pad.y + pad.height - 20}, 22, dayCol, 1.f);
}

void drawTeamPhoto(Rectangle r, okey::Rng& rng) {
    const Color sep0{226, 206, 170, 255}, sep1{120, 92, 62, 255}, sep2{64, 46, 30, 255};
    DrawRectangleRec(r, sep0);
    Rectangle ph{r.x + 22, r.y + 20, r.width - 44, r.height - 90};
    rectGradV(ph, Color{170, 146, 110, 255}, Color{140, 116, 84, 255});
    // stands in the background
    for (int row = 0; row < 5; ++row)
        for (float x = ph.x; x < ph.x + ph.width; x += 7.f)
            ellipse({x + rng.uniform(-2.f, 2.f), ph.y + 12 + row * 11.f}, 3.f, 3.5f,
                    mix(sep1, sep0, rng.uniform(0.f, 0.5f)), 8);
    DrawRectangleRec({ph.x, ph.y + ph.height * 0.62f, ph.width, ph.height * 0.38f}, Color{150, 130, 96, 255});
    // two rows of players
    for (int row = 0; row < 2; ++row) {
        int n = row == 0 ? 6 : 5;
        float y = ph.y + (row == 0 ? 92.f : 150.f);
        float sp = ph.width / (n + 1);
        for (int i = 0; i < n; ++i) {
            float x = ph.x + sp * (i + 1) + (row == 1 ? sp * 0.1f : 0.f);
            float s = row == 0 ? 1.f : 1.08f;
            // shirt with vertical stripes
            Rectangle shirt{x - 17 * s, y, 34 * s, 42 * s};
            DrawRectangleRounded(shirt, 0.3f, 6, sep2);
            for (int k = 0; k < 3; ++k) DrawRectangleRec({shirt.x + 5 + k * 10 * s, shirt.y + 3, 5 * s, shirt.height - 6}, sep0);
            DrawRectangleRec({x - 15 * s, y + 42 * s, 30 * s, 16 * s}, Color{236, 226, 204, 255});
            if (row == 0) {
                DrawRectangleRec({x - 12 * s, y + 58 * s, 9 * s, 30 * s}, sep2);
                DrawRectangleRec({x + 3 * s, y + 58 * s, 9 * s, 30 * s}, sep2);
            }
            ellipse({x, y - 14 * s}, 11 * s, 13 * s, Color{176, 138, 100, 255});
            ellipse({x, y - 22 * s}, 11 * s, 6 * s, sep2);
            DrawRectangleRec({x - 7 * s, y - 10 * s, 14 * s, 3 * s}, Color{80, 56, 36, 200});  // mustache
        }
    }
    ellipse({ph.x + ph.width / 2, ph.y + ph.height - 14}, 12, 12, Color{240, 236, 226, 255});  // ball
    filmGrain(ph, rng, 5000, 0.18f);
    vignette(ph, Color{60, 36, 16, 150}, 0.16f);
    DrawLineEx({ph.x + ph.width * 0.7f, ph.y}, {ph.x + ph.width * 0.64f, ph.y + ph.height}, 2.f, Color{246, 232, 206, 120});
    textC(FontId::Hand, "1977 - 78  Şampiyon Kadro", {r.x + r.width / 2, r.y + r.height - 38}, 36, Color{60, 40, 28, 255});
}

void drawPennant(Rectangle r) {
    // triangle: left edge vertical (pole side), point at the right
    Vector2 a{r.x, r.y}, b{r.x, r.y + r.height}, c{r.x + r.width, r.y + r.height * 0.5f};
    DrawRectangleRec(r, Color{30, 40, 90, 255});
    tri(a, b, c, Color{250, 200, 30, 255});
    tri({r.x, r.y + r.height * 0.5f}, b, c, Color{24, 40, 110, 255});
    tri({r.x, r.y + 14}, {r.x, r.y + r.height - 14}, {r.x + r.width - 40, r.y + r.height * 0.5f}, alpha(WHITE, 0.0f));
    DrawRectangleRec({r.x, r.y, 26, r.height}, Color{240, 240, 236, 255});
    textC(FontId::Sign, "BGZ", {r.x + r.width * 0.36f, r.y + r.height * 0.36f}, 64, Color{24, 40, 110, 255}, 4.f);
    textC(FontId::Sign, "BOĞAZ S.K.", {r.x + r.width * 0.32f, r.y + r.height * 0.66f}, 26, Color{250, 210, 60, 255});
    ellipse({r.x + r.width * 0.62f, r.y + r.height * 0.5f}, 12, 12, WHITE);
}

void drawTavla(Rectangle r, okey::Rng& rng) {
    const Color woodL{196, 150, 96, 255}, woodD{120, 76, 40, 255}, ptA{130, 30, 26, 255}, ptB{236, 222, 190, 255};
    DrawRectangleRec(r, woodD);
    for (int i = 0; i < 90; ++i) {
        float y = rng.uniform(r.y, r.y + r.height);
        DrawLineEx({r.x, y}, {r.x + r.width, y + rng.uniform(-4.f, 4.f)}, rng.uniform(1.f, 3.f), alpha(Color{80, 50, 26, 255}, 0.25f));
    }
    float bar = 26.f;
    for (int half = 0; half < 2; ++half) {
        Rectangle h{r.x + 14 + half * (r.width / 2 + bar / 2 - 14 + 0.f), r.y + 14, r.width / 2 - bar / 2 - 14, r.height - 28};
        DrawRectangleRec(h, woodL);
        for (int i = 0; i < 120; ++i) {
            float y = rng.uniform(h.y, h.y + h.height);
            DrawLineEx({h.x, y}, {h.x + h.width, y + rng.uniform(-3.f, 3.f)}, 1.f, alpha(Color{150, 100, 56, 255}, 0.35f));
        }
        float w = h.width / 6.f;
        for (int k = 0; k < 6; ++k) {
            float x0 = h.x + k * w;
            Color c = (k % 2) ? ptA : ptB;
            tri({x0 + 2, h.y}, {x0 + w - 2, h.y}, {x0 + w / 2, h.y + h.height * 0.42f}, (k % 2) ? ptB : ptA);
            tri({x0 + 2, h.y + h.height}, {x0 + w - 2, h.y + h.height}, {x0 + w / 2, h.y + h.height * 0.58f}, c);
        }
        frameRect(h, 3.f, Color{70, 44, 22, 255});
    }
    DrawRectangleRec({r.x + r.width / 2 - bar / 2, r.y, bar, r.height}, Color{80, 50, 26, 255});
}

void drawEnamelSign(Rectangle r, const char* l1, const char* l2, Color bg, Color fg, Color border, okey::Rng& rng) {
    DrawRectangleRounded(r, 0.12f, 8, border);
    DrawRectangleRounded(inset(r, 8), 0.1f, 8, bg);
    DrawRectangleRoundedLinesEx(inset(r, 14), 0.1f, 8, 2.f, alpha(fg, 0.6f));
    if (l2) {
        textC(FontId::Sign, l1, {r.x + r.width / 2, r.y + r.height * 0.36f}, 40, fg, 2.f);
        textC(FontId::Sign, l2, {r.x + r.width / 2, r.y + r.height * 0.68f}, 40, fg, 2.f);
    } else {
        textC(FontId::Sign, l1, ctr(r), 50, fg, 3.f);
    }
    for (int k = 0; k < 4; ++k) {  // screws
        Vector2 p{k % 2 ? r.x + r.width - 16 : r.x + 16, k < 2 ? r.y + 16 : r.y + r.height - 16};
        ellipse(p, 5, 5, Color{150, 150, 150, 255});
    }
    for (int i = 0; i < 26; ++i) {  // chipped enamel & rust
        Vector2 p{rng.uniform(r.x, r.x + r.width), rng.chance(0.5f) ? rng.uniform(r.y, r.y + 18) : rng.uniform(r.y + r.height - 18, r.y + r.height)};
        ellipse(p, rng.uniform(1.5f, 5.f), rng.uniform(1.5f, 4.f), Color{60, 40, 30, (unsigned char)rng.range(120, 230)}, 10);
    }
    rectGradV(r, alpha(WHITE, 0.08f), alpha(Color{60, 40, 20, 255}, 0.18f));
}

void drawWoodPlaque(Rectangle r, const char* l1, const char* l2, okey::Rng& rng) {
    DrawRectangleRounded(r, 0.2f, 8, Color{92, 56, 30, 255});
    for (int i = 0; i < 40; ++i) {
        float y = rng.uniform(r.y + 4, r.y + r.height - 4);
        DrawLineEx({r.x + 6, y}, {r.x + r.width - 6, y + rng.uniform(-2.f, 2.f)}, 1.5f, alpha(Color{60, 34, 16, 255}, 0.4f));
    }
    DrawRectangleRoundedLinesEx(inset(r, 8), 0.2f, 8, 3.f, Color{206, 170, 96, 255});
    textC(FontId::Sign, l1, {r.x + r.width / 2, r.y + r.height * (l2 ? 0.37f : 0.5f)}, 38, Color{236, 206, 140, 255}, 2.f);
    if (l2) textC(FontId::Sign, l2, {r.x + r.width / 2, r.y + r.height * 0.7f}, 38, Color{236, 206, 140, 255}, 2.f);
}

void drawCounterTiles(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{90, 96, 100, 255});
    int cols = 12, rows = 6;
    float tw = r.width / cols, th = r.height / rows;
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            Rectangle t{r.x + i * tw + 1.5f, r.y + j * th + 1.5f, tw - 3, th - 3};
            float k = rng.uniform(0.9f, 1.02f) * (1.f - 0.25f * j / rows);
            Color base = scaleRgb(Color{226, 230, 224, 255}, k);
            DrawRectangleRec(t, base);
            if (j == 1) {  // decorative blue band (Kütahya-style)
                DrawRectangleRec(t, Color{232, 236, 240, 255});
                ellipse(ctr(t), tw * 0.32f, th * 0.32f, Color{30, 80, 160, 255}, 16);
                ellipse(ctr(t), tw * 0.16f, th * 0.16f, Color{60, 170, 180, 255}, 12);
                for (int q = 0; q < 4; ++q) {
                    float a = q * PI / 2 + PI / 4;
                    ellipse({t.x + t.width / 2 + std::cos(a) * tw * 0.38f, t.y + t.height / 2 + std::sin(a) * th * 0.38f}, 3, 3,
                            Color{200, 40, 40, 255}, 8);
                }
            }
            rectGradV(t, alpha(WHITE, 0.25f), alpha(WHITE, 0.f));
        }
    rectGradV({r.x, r.y + r.height * 0.6f, r.width, r.height * 0.4f}, alpha(Color{60, 44, 26, 255}, 0.f),
              alpha(Color{60, 44, 26, 255}, 0.55f));
}

void drawMirror(Rectangle r, okey::Rng& rng) {
    rectGradV(r, Color{64, 52, 40, 255}, Color{30, 24, 20, 255});
    // blurred reflection of the room: warm lamp blobs, dark silhouettes
    for (int k = 0; k < 5; ++k)
        DrawCircleGradient({r.x + rng.uniform(40.f, r.width - 40), r.y + rng.uniform(60.f, r.height * 0.4f)}, rng.uniform(30.f, 70.f),
                           Color{255, 200, 120, 120}, Color{255, 180, 100, 0});
    for (int k = 0; k < 4; ++k) {
        float x = r.x + rng.uniform(30.f, r.width - 30);
        ellipse({x, r.y + r.height * 0.58f}, 24, 28, Color{24, 18, 14, 160});
        DrawRectangleRounded({x - 40, r.y + r.height * 0.62f, 80, r.height * 0.4f}, 0.4f, 6, Color{24, 18, 14, 150});
    }
    rectGradH({r.x, r.y, r.width * 0.5f, r.height}, alpha(WHITE, 0.10f), alpha(WHITE, 0.f));
    // desilvering at the edges
    for (int i = 0; i < 160; ++i) {
        float e = rng.uniform(0.f, 1.f);
        Vector2 p = e < 0.5f ? Vector2{rng.chance(0.5f) ? r.x + rng.uniform(0.f, 26.f) : r.x + r.width - rng.uniform(0.f, 26.f),
                                       rng.uniform(r.y, r.y + r.height)}
                             : Vector2{rng.uniform(r.x, r.x + r.width),
                                       rng.chance(0.5f) ? r.y + rng.uniform(0.f, 26.f) : r.y + r.height - rng.uniform(0.f, 26.f)};
        ellipse(p, rng.uniform(2.f, 9.f), rng.uniform(2.f, 7.f), Color{12, 10, 8, (unsigned char)rng.range(90, 220)}, 10);
    }
}

void drawShip(Rectangle r, okey::Rng& rng) {
    float hz = r.y + r.height * 0.55f;
    rectGradV({r.x, r.y, r.width, hz - r.y}, Color{110, 160, 210, 255}, Color{200, 214, 222, 255});
    ellipse({r.x + 120, r.y + 70}, 60, 16, Color{240, 244, 246, 200});
    ellipse({r.x + 330, r.y + 50}, 80, 18, Color{240, 244, 246, 170});
    // Maiden's tower on its islet
    float tx = r.x + r.width * 0.78f;
    ellipse({tx, hz + 4}, 46, 9, Color{120, 110, 96, 255});
    DrawRectangleRec({tx - 30, hz - 24, 60, 26}, Color{226, 216, 196, 255});
    DrawRectangleRec({tx - 11, hz - 70, 22, 48}, Color{232, 222, 202, 255});
    tri({tx - 14, hz - 70}, {tx + 14, hz - 70}, {tx, hz - 100}, Color{70, 90, 96, 255});
    // far shore
    for (float x = r.x; x < r.x + r.width * 0.6f; x += 10)
        DrawRectangleRec({x, hz - rng.uniform(6.f, 20.f), 11, 22}, Color{120, 130, 140, 255});
    rectGradV({r.x, hz, r.width, r.y + r.height - hz}, Color{60, 110, 150, 255}, Color{24, 58, 90, 255});
    for (int i = 0; i < 160; ++i)
        DrawRectangleV({rng.uniform(r.x, r.x + r.width), rng.uniform(hz + 2, r.y + r.height)}, {rng.uniform(6.f, 20.f), 2.f},
                       Color{220, 236, 250, (unsigned char)rng.range(40, 110)});
    // vapur, larger, sailing right
    float fx = r.x + r.width * 0.36f, fy = r.y + r.height * 0.74f;
    quad2({fx - 110, fy}, {fx + 118, fy}, {fx + 100, fy + 22}, {fx - 96, fy + 22}, Color{24, 22, 26, 255});
    DrawRectangleRec({fx - 92, fy - 24, 176, 24}, Color{244, 240, 230, 255});
    DrawRectangleRec({fx - 64, fy - 40, 110, 16}, Color{244, 240, 230, 255});
    for (int k = 0; k < 12; ++k) DrawRectangleRec({fx - 84 + k * 14.f, fy - 18, 8, 7}, Color{40, 60, 80, 255});
    DrawRectangleRec({fx - 6, fy - 76, 20, 36}, Color{230, 230, 226, 255});
    DrawRectangleRec({fx - 6, fy - 76, 20, 10}, Color{24, 22, 26, 255});
    for (int k = 0; k < 5; ++k)
        ellipse({fx + 4 - k * 18.f, fy - 88 - k * 5.f}, 11.f + k * 3, 7.f + k, Color{80, 80, 86, (unsigned char)(170 - k * 30)});
    brushStrokes(r, rng, 700, 18.f, 0.08f);
    vignette(r, Color{40, 30, 10, 80}, 0.1f);
}

void drawNews(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{226, 220, 202, 255});
    textC(FontId::Sign, "MAHALLE POSTASI", {r.x + r.width / 2, r.y + 26}, 30, Color{30, 30, 30, 255});
    DrawLineEx({r.x + 12, r.y + 46}, {r.x + r.width - 12, r.y + 46}, 2.f, Color{40, 40, 40, 255});
    textC(FontId::Sign, "DERBİDE", {r.x + r.width / 2, r.y + 78}, 40, Color{170, 30, 26, 255});
    textC(FontId::Sign, "NEFESLER TUTULDU", {r.x + r.width / 2, r.y + 116}, 30, Color{30, 30, 30, 255});
    DrawRectangleRec({r.x + 14, r.y + 140, r.width * 0.55f, 120}, Color{110, 104, 96, 255});
    for (int k = 0; k < 5; ++k) ellipse({r.x + 40 + k * 26.f, r.y + 220}, 8, 18, Color{60, 56, 52, 255});
    for (int col = 0; col < 2; ++col)
        for (int i = 0; i < 18; ++i) {
            float x = col == 0 ? r.x + r.width * 0.6f + 10 : r.x + 14;
            float y = col == 0 ? r.y + 146 + i * 7.f : r.y + 272 + i * 7.f;
            float w = col == 0 ? r.width * 0.38f - 18 : r.width - 28;
            if (y > r.y + r.height - 10) break;
            DrawRectangleRec({x, y, w * rng.uniform(0.7f, 1.f), 3}, Color{90, 88, 84, 255});
        }
}

// House rules, hand-painted on a cream board with a red border (tongue in cheek, like every such board).
void drawRules(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{226, 214, 184, 255});
    rectGradV(r, alpha(Color{255, 246, 220, 255}, 0.3f), alpha(Color{120, 90, 50, 255}, 0.35f));
    frameRect(inset(r, 10), 5.f, Color{150, 36, 30, 255});
    frameRect(inset(r, 20), 1.5f, Color{150, 36, 30, 200});
    textC(FontId::Sign, "KIRAATHANE ADABI", {r.x + r.width / 2, r.y + 50}, 40, Color{150, 36, 30, 255}, 2.f);
    DrawLineEx({r.x + 90, r.y + 78}, {r.x + r.width - 90, r.y + 78}, 2.f, Color{60, 44, 30, 200});
    const char* rules[4] = {"1. Masaya taş vurulmaz.", "2. Siyaset konuşulmaz.", "3. Kaybeden çayları ısmarlar.",
                            "4. Veresiye defteri dolmuştur."};
    for (int i = 0; i < 4; ++i)
        ui::drawText(FontId::Hand, rules[i], {r.x + 44, r.y + 96 + i * 44.f}, 32, Color{44, 34, 28, 255});
    // fly specks and a brown tide line from years of smoke
    for (int i = 0; i < 60; ++i)
        ellipse({rng.uniform(r.x + 12, r.x + r.width - 12), rng.uniform(r.y + 12, r.y + r.height - 12)}, 1.2f, 1.2f,
                Color{60, 44, 30, 140}, 6);
    rectGradV({r.x, r.y, r.width, r.height * 0.35f}, Color{110, 76, 36, 70}, Color{110, 76, 36, 0});
}

// Kilim: bands of göz / diamond motifs in madder red, indigo and ochre.
void drawKilim(Rectangle r, okey::Rng& rng) {
    const Color red{150, 40, 34, 255}, indigo{40, 44, 84, 255}, ochre{196, 146, 60, 255}, cream{220, 204, 170, 255};
    DrawRectangleRec(r, red);
    const float bandH = r.height / 5.f;
    for (int b = 0; b < 5; ++b) {
        Rectangle band{r.x, r.y + b * bandH, r.width, bandH};
        Color bg = b % 2 ? indigo : red, fg = b % 2 ? ochre : cream;
        DrawRectangleRec(band, bg);
        const float dw = b == 2 ? 58.f : 30.f;
        for (float x = band.x; x < band.x + band.width; x += dw) {
            Vector2 c{x + dw * 0.5f, band.y + band.height * 0.5f};
            float hw = dw * 0.45f, hh = band.height * 0.42f;
            quad2({c.x - hw, c.y}, {c.x, c.y - hh}, {c.x + hw, c.y}, {c.x, c.y + hh}, fg);
            quad2({c.x - hw * 0.5f, c.y}, {c.x, c.y - hh * 0.5f}, {c.x + hw * 0.5f, c.y}, {c.x, c.y + hh * 0.5f}, bg);
            if (b == 2) quad2({c.x - hw * 0.2f, c.y}, {c.x, c.y - hh * 0.2f}, {c.x + hw * 0.2f, c.y}, {c.x, c.y + hh * 0.2f}, ochre);
        }
        DrawLineEx({band.x, band.y}, {band.x + band.width, band.y}, 3.f, Color{30, 24, 22, 255});
    }
    // worn patches where people sit
    for (int i = 0; i < 5; ++i)
        ellipseGrad({rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)}, rng.uniform(40.f, 90.f), rng.uniform(20.f, 40.f),
                    Color{200, 180, 150, 60}, Color{200, 180, 150, 0});
}

// Faded olive-oil tin label: now a flower pot, as in every kahvehane.
void drawTeneke(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{206, 176, 60, 255});
    rectGradV({r.x, r.y, r.width, r.height * 0.22f}, Color{150, 30, 26, 255}, Color{170, 40, 30, 255});
    rectGradV({r.x, r.y + r.height * 0.78f, r.width, r.height * 0.22f}, Color{170, 40, 30, 255}, Color{150, 30, 26, 255});
    textC(FontId::Sign, "ZEYTİNYAĞI", {r.x + r.width / 2, r.y + r.height * 0.5f}, 40, Color{40, 70, 30, 255}, 2.f);
    textC(FontId::Sign, "AYVALIK", {r.x + r.width / 2, r.y + r.height * 0.11f}, 24, Color{240, 220, 160, 255}, 3.f);
    textC(FontId::Sign, "5 KG", {r.x + r.width / 2, r.y + r.height * 0.89f}, 24, Color{240, 220, 160, 255}, 3.f);
    for (int k = 0; k < 5; ++k)  // olive branch
        ellipse({r.x + 70 + k * 36.f, r.y + r.height * 0.68f + (k % 2 ? -6.f : 6.f)}, 14, 6, Color{70, 100, 40, 255}, 10);
    // rust and dents
    for (int i = 0; i < 26; ++i)
        ellipseGrad({rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)}, rng.uniform(6.f, 26.f), rng.uniform(4.f, 16.f),
                    Color{110, 60, 30, 150}, Color{110, 60, 30, 0});
}

void drawRadio(Rectangle r, okey::Rng& rng) {
    DrawRectangleRounded(r, 0.15f, 8, Color{96, 58, 30, 255});
    Rectangle grille{r.x + 12, r.y + 14, r.width * 0.45f, r.height - 28};
    DrawRectangleRounded(grille, 0.2f, 6, Color{150, 124, 90, 255});
    for (float x = grille.x + 4; x < grille.x + grille.width; x += 5) DrawLineEx({x, grille.y + 3}, {x, grille.y + grille.height - 3}, 1.5f, Color{110, 88, 60, 255});
    Rectangle dial{r.x + r.width * 0.52f, r.y + 18, r.width * 0.42f, r.height * 0.42f};
    DrawRectangleRounded(dial, 0.2f, 6, Color{236, 196, 120, 255});
    for (int k = 0; k < 14; ++k) DrawLineEx({dial.x + 8 + k * (dial.width - 16) / 13.f, dial.y + 6}, {dial.x + 8 + k * (dial.width - 16) / 13.f, dial.y + (k % 2 ? 14 : 20)}, 1.5f, Color{80, 50, 20, 255});
    DrawLineEx({dial.x + dial.width * 0.62f, dial.y + 3}, {dial.x + dial.width * 0.62f, dial.y + dial.height - 3}, 2.f, Color{200, 30, 20, 255});
    textC(FontId::Ui, "UZUN  ORTA  KISA", {dial.x + dial.width / 2, dial.y + dial.height - 10}, 11, Color{80, 50, 20, 255});
    for (int k = 0; k < 2; ++k) ellipse({dial.x + 20 + k * (dial.width - 40), r.y + r.height - 26}, 13, 13, Color{50, 34, 20, 255});
    (void)rng;
}

void drawLabels(okey::Rng& rng) {
    Rectangle t = art::TEABOX;
    DrawRectangleRec(t, Color{190, 30, 30, 255});
    DrawRectangleRec({t.x, t.y + t.height * 0.62f, t.width, t.height * 0.38f}, Color{240, 190, 40, 255});
    textC(FontId::Sign, "ÇAY", {t.x + t.width / 2, t.y + 52}, 50, Color{250, 230, 170, 255}, 2.f);
    textC(FontId::Ui, "RİZE", {t.x + t.width / 2, t.y + 92}, 20, Color{250, 230, 170, 255}, 3.f);
    ellipse({t.x + t.width / 2, t.y + t.height * 0.8f}, 22, 11, Color{40, 120, 50, 255});
    Rectangle s = art::SODA_LABEL;
    DrawRectangleRec(s, Color{30, 110, 60, 255});
    DrawRectangleRec({s.x, s.y + 30, s.width, 34}, Color{240, 236, 220, 255});
    textC(FontId::UiBold, "MADEN SUYU", {s.x + s.width / 2, s.y + 47}, 17, Color{20, 80, 40, 255});
    Rectangle g = art::GAZOZ_LABEL;
    DrawRectangleRec(g, Color{240, 140, 30, 255});
    textC(FontId::Sign, "GAZOZ", {g.x + g.width / 2, g.y + g.height / 2}, 30, Color{255, 246, 220, 255});
    (void)rng;
}

void drawDice() {
    Rectangle r = art::DICE;
    const int pips[7][9] = {{0},
                            {0, 0, 0, 0, 1, 0, 0, 0, 0},
                            {1, 0, 0, 0, 0, 0, 0, 0, 1},
                            {1, 0, 0, 0, 1, 0, 0, 0, 1},
                            {1, 0, 1, 0, 0, 0, 1, 0, 1},
                            {1, 0, 1, 0, 1, 0, 1, 0, 1},
                            {1, 0, 1, 1, 0, 1, 1, 0, 1}};
    for (int f = 1; f <= 6; ++f) {
        Rectangle c{r.x + (f - 1) * 96.f, r.y, 96, 96};
        DrawRectangleRec(c, Color{244, 238, 222, 255});
        for (int k = 0; k < 9; ++k)
            if (pips[f][k]) ellipse({c.x + 24 + (k % 3) * 24.f, c.y + 24 + (k / 3) * 24.f}, 8.5f, 8.5f, f == 1 ? Color{190, 20, 20, 255} : Color{20, 20, 24, 255}, 14);
        frameRect(c, 3.f, Color{210, 200, 180, 255});
    }
}

void suit(Vector2 c, float s, int kind, Color col) {
    if (kind == 0) {  // heart
        ellipse({c.x - s * 0.5f, c.y - s * 0.2f}, s * 0.55f, s * 0.55f, col, 16);
        ellipse({c.x + s * 0.5f, c.y - s * 0.2f}, s * 0.55f, s * 0.55f, col, 16);
        tri({c.x - s * 1.02f, c.y}, {c.x + s * 1.02f, c.y}, {c.x, c.y + s * 1.1f}, col);
    } else if (kind == 1) {  // diamond
        quad2({c.x, c.y - s}, {c.x + s * 0.75f, c.y}, {c.x, c.y + s}, {c.x - s * 0.75f, c.y}, col);
    } else if (kind == 2) {  // spade
        ellipse({c.x - s * 0.5f, c.y + s * 0.15f}, s * 0.55f, s * 0.55f, col, 16);
        ellipse({c.x + s * 0.5f, c.y + s * 0.15f}, s * 0.55f, s * 0.55f, col, 16);
        tri({c.x - s * 1.02f, c.y}, {c.x + s * 1.02f, c.y}, {c.x, c.y - s * 1.1f}, col);
        tri({c.x, c.y + s * 0.2f}, {c.x - s * 0.4f, c.y + s}, {c.x + s * 0.4f, c.y + s}, col);
    } else {  // club
        ellipse({c.x, c.y - s * 0.45f}, s * 0.45f, s * 0.45f, col, 16);
        ellipse({c.x - s * 0.5f, c.y + s * 0.15f}, s * 0.45f, s * 0.45f, col, 16);
        ellipse({c.x + s * 0.5f, c.y + s * 0.15f}, s * 0.45f, s * 0.45f, col, 16);
        tri({c.x, c.y}, {c.x - s * 0.35f, c.y + s}, {c.x + s * 0.35f, c.y + s}, col);
    }
}

void drawCards() {
    Rectangle b = art::CARD_BACK;
    DrawRectangleRec(b, Color{250, 248, 240, 255});
    DrawRectangleRec(inset(b, 6), Color{170, 30, 36, 255});
    for (float y = b.y + 10; y < b.y + b.height - 8; y += 8)
        for (float x = b.x + 10; x < b.x + b.width - 8; x += 8) ellipse({x, y}, 2.f, 2.f, Color{230, 170, 160, 255}, 6);
    const char* ranks[3] = {"K", "7", "A"};
    int suits[3] = {0, 2, 1};
    for (int i = 0; i < 3; ++i) {
        Rectangle f{art::CARD_FACE.x + i * 104.f, art::CARD_FACE.y, 96, 140};
        DrawRectangleRec(f, Color{250, 248, 240, 255});
        Color col = suits[i] == 2 ? Color{20, 20, 24, 255} : Color{200, 24, 30, 255};
        ui::drawText(FontId::UiBold, ranks[i], {f.x + 8, f.y + 4}, 30, col);
        suit({f.x + 48, f.y + 76}, 20, suits[i], col);
        frameRect(f, 1.5f, Color{190, 186, 176, 255});
    }
}

void drawCertificate(Rectangle r) {
    DrawRectangleRec(r, Color{240, 230, 204, 255});
    frameRect(inset(r, 10), 5.f, Color{190, 150, 60, 255});
    frameRect(inset(r, 20), 1.5f, Color{190, 150, 60, 255});
    textC(FontId::Sign, "BAŞARI BELGESİ", {r.x + r.width / 2, r.y + 58}, 36, Color{130, 30, 24, 255}, 2.f);
    textC(FontId::Hand, "Mahalle Okey Turnuvası", {r.x + r.width / 2, r.y + 118}, 32, Color{40, 34, 30, 255});
    textC(FontId::Sign, "BİRİNCİLİK", {r.x + r.width / 2, r.y + 170}, 40, Color{40, 34, 30, 255}, 3.f);
    textC(FontId::Hand, "2019", {r.x + r.width / 2, r.y + 222}, 30, Color{40, 34, 30, 255});
    ellipse({r.x + r.width - 70, r.y + r.height - 60}, 28, 28, Color{170, 30, 30, 220});
    ellipse({r.x + r.width - 70, r.y + r.height - 60}, 20, 20, Color{200, 60, 50, 255});
}

void drawOldPhoto(Rectangle r, okey::Rng& rng) {
    const Color s0{214, 196, 160, 255}, s1{150, 124, 90, 255}, s2{70, 54, 38, 255};
    rectGradV(r, s0, s1);
    // street in perspective with buildings and a tram
    for (int k = 0; k < 6; ++k) {
        float x0 = r.x + k * 32.f, w = 34.f, h = 190.f - k * 18.f;
        DrawRectangleRec({x0, r.y + r.height - 60 - h, w, h}, mix(s2, s1, k * 0.12f));
        for (int j = 0; j < 5; ++j) DrawRectangleRec({x0 + 8, r.y + r.height - 50 - h + j * 30.f, 8, 14}, s0);
        DrawRectangleRec({r.x + r.width - x0 + r.x - w, r.y + r.height - 60 - h, w, h}, mix(s2, s1, k * 0.12f));
    }
    quad2({r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}, {r.x + r.width * 0.56f, r.y + r.height - 60},
          {r.x + r.width * 0.44f, r.y + r.height - 60}, Color{120, 100, 76, 255});
    Rectangle tram{r.x + r.width * 0.36f, r.y + r.height - 150, 120, 100};
    DrawRectangleRounded(tram, 0.15f, 6, Color{110, 70, 50, 255});
    for (int k = 0; k < 4; ++k) DrawRectangleRec({tram.x + 10 + k * 27.f, tram.y + 14, 20, 30}, s0);
    DrawLineEx({tram.x + 60, tram.y}, {tram.x + 40, r.y + 20}, 2.f, s2);
    DrawLineEx({r.x, r.y + 30}, {r.x + r.width, r.y + 16}, 1.5f, s2);
    filmGrain(r, rng, 4200, 0.2f);
    vignette(r, Color{50, 30, 10, 160}, 0.18f);
}

void drawCigPack(Rectangle r) {
    DrawRectangleRec(r, Color{244, 240, 232, 255});
    tri({r.x, r.y + 60}, {r.x + r.width, r.y + 60}, {r.x + r.width / 2, r.y + 110}, Color{200, 30, 36, 255});
    DrawRectangleRec({r.x, r.y + 40, r.width, 22}, Color{200, 30, 36, 255});
    textC(FontId::Sign, "YAPRAK", {r.x + r.width / 2, r.y + 20}, 24, Color{60, 40, 30, 255});
    DrawRectangleRec({r.x, r.y + r.height - 40, r.width, 40}, Color{20, 20, 20, 255});
    textC(FontId::Ui, "SİGARA ÖLDÜRÜR", {r.x + r.width / 2, r.y + r.height - 20}, 13, WHITE);
}

void drawScorePad(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{246, 242, 226, 255});
    for (float y = r.y + 30; y < r.y + r.height; y += 22) DrawLineEx({r.x, y}, {r.x + r.width, y}, 1.f, Color{150, 170, 200, 255});
    DrawLineEx({r.x + r.width / 3, r.y}, {r.x + r.width / 3, r.y + r.height}, 1.5f, Color{200, 90, 90, 255});
    DrawLineEx({r.x + 2 * r.width / 3, r.y}, {r.x + 2 * r.width / 3, r.y + r.height}, 1.5f, Color{200, 90, 90, 255});
    for (int i = 0; i < 7; ++i)
        for (int c = 0; c < 3; ++c) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%d", rng.range(0, 99));
            ui::drawText(FontId::Hand, buf, {r.x + 8 + c * r.width / 3, r.y + 30 + i * 22.f}, 20, Color{50, 50, 60, 255});
        }
}

void drawTileFaces() {
    Rectangle r = art::TILEFACE;
    Color inks[4] = {{222, 146, 12, 255}, {28, 86, 186, 255}, {28, 28, 30, 255}, {200, 28, 38, 255}};
    for (int i = 0; i < 8; ++i) {
        Rectangle f{r.x + (i % 4) * 60.f, r.y + (i / 4) * 60.f, 60, 60};
        DrawRectangleRec(f, Color{246, 238, 216, 255});
        char buf[4];
        std::snprintf(buf, sizeof buf, "%d", 1 + (i * 5) % 13);
        textC(FontId::Tile, buf, {f.x + 30, f.y + 26}, 34, inks[i % 4]);
        ellipse({f.x + 30, f.y + 48}, 4, 4, inks[i % 4], 8);
    }
}

void drawPoster(Rectangle r, okey::Rng& rng) {
    DrawRectangleRec(r, Color{246, 236, 210, 255});
    DrawRectangleRec({r.x, r.y, r.width, 70}, Color{30, 50, 120, 255});
    textC(FontId::Sign, "BU AKŞAM", {r.x + r.width / 2, r.y + 36}, 42, Color{250, 210, 60, 255}, 2.f);
    textC(FontId::Sign, "BGZ - HLÇ", {r.x + r.width / 2, r.y + 130}, 58, Color{160, 24, 24, 255}, 2.f);
    textC(FontId::Sign, "DERBİ 21:00", {r.x + r.width / 2, r.y + 196}, 36, Color{30, 30, 34, 255}, 1.f);
    textC(FontId::Hand, "Maçımız dev ekranda!", {r.x + r.width / 2, r.y + 250}, 26, Color{30, 50, 120, 255});
    for (int k = 0; k < 4; ++k) {  // tape
        Vector2 p{k % 2 ? r.x + r.width - 16 : r.x + 16, k < 2 ? r.y + 8 : r.y + r.height - 8};
        DrawRectanglePro({p.x, p.y, 44, 16}, {22, 8}, k % 3 ? 30.f : -30.f, Color{230, 220, 180, 170});
    }
    (void)rng;
}

}  // namespace

void drawArtAtlas(RenderTexture2D& rt, uint32_t seed) {
    okey::Rng rng(seed ^ 0xA27u);
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    beginRoomCanvas(rt);
    rlDisableBackfaceCulling();
    ClearBackground(Color{120, 100, 80, 255});
    drawPriceBoard(art::PRICE, rng);
    drawClockFace(art::CLOCK);
    drawBosphorus(art::BOSPHORUS, rng);
    drawCalendar(art::CALENDAR, lt);
    drawTeamPhoto(art::TEAM, rng);
    drawPennant(art::PENNANT);
    drawTavla(art::TAVLA, rng);
    drawEnamelSign(art::SIGN_KUMAR, "KUMAR OYNAMAK", "YASAKTIR", Color{246, 244, 236, 255}, Color{30, 30, 36, 255},
                   Color{170, 30, 30, 255}, rng);
    drawWoodPlaque(art::SIGN_VERESIYE, "BUGÜN PEŞİN", "YARIN VERESİYE", rng);
    drawEnamelSign(art::SIGN_WC, "WC", nullptr, Color{30, 70, 150, 255}, WHITE, Color{240, 240, 240, 255}, rng);
    drawWoodPlaque(art::SIGN_WELCOME, "HOŞ GELDİNİZ", nullptr, rng);
    drawCounterTiles(art::TILES, rng);
    drawMirror(art::MIRROR, rng);
    drawShip(art::SHIP, rng);
    drawNews(art::NEWS, rng);
    drawRadio(art::RADIO, rng);
    drawLabels(rng);
    drawDice();
    drawCards();
    drawCertificate(art::CERT);
    drawOldPhoto(art::OLDPHOTO, rng);
    drawCigPack(art::CIGPACK);
    drawScorePad(art::SCOREPAD, rng);
    drawTileFaces();
    drawPoster(art::PAPER_TV, rng);
    drawRules(art::RULES, rng);
    drawKilim(art::KILIM, rng);
    drawTeneke(art::TENEKE, rng);
    DrawRectangleRec(art::PLAIN_WHITE, WHITE);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    endRoomCanvas(rt);
}

// ============================================================================ the street outside (by day or by night)
namespace {

struct StreetMap {  // world (a, y) in metres -> canvas pixels inside a panel rect
    Rectangle px;
    float a0, a1, y0, y1;  // a increases to the right; y0 bottom, y1 top
    int phase = 3, season = 2;  // w3d::DayPhase / w3d::Season (0 sabah .. 3 gece, 0 ilkbahar .. 3 kış)
    bool day() const { return phase <= 1; }         // morning, noon: sunlit facades, shops open, no lamps
    bool lampsOn() const { return phase >= 2; }     // evening, night: street lamps, lit windows, neon
    bool snowy() const { return season == 3; }
    // a night colour, or its daylight counterpart (at dusk the night painting keeps a little of the day)
    Color dn(Color night, Color dayc) const { return day() ? dayc : (phase == 2 ? mix(night, dayc, 0.18f) : night); }
    Vector2 P(float a, float y) const {
        return {px.x + (a - a0) / (a1 - a0) * px.width, px.y + (y1 - y) / (y1 - y0) * px.height};
    }
    Rectangle R(float a, float y, float w, float h) const {  // (a, y) = bottom-left corner in metres
        Vector2 p = P(a, y + h), q = P(a + w, y);
        return {p.x, p.y, q.x - p.x, q.y - p.y};
    }
    float S() const { return px.width / (a1 - a0); }  // pixels per metre (horizontal)
};

void windowLit(const StreetMap& m, float a, float y, float w, float h, int style, okey::Rng& rng) {
    Rectangle r = m.R(a, y, w, h);
    if (m.day()) {  // sky reflected in the glass, lace curtains, a closed shutter here and there
        const uint32_t hsh = (uint32_t)(a * 131.f) * 2654435761u ^ (uint32_t)(y * 17.f) * 40503u;
        DrawRectangleRec({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, Color{150, 142, 132, 255});
        rectGradV(r, Color{168, 190, 212, 255}, Color{88, 104, 124, 255});
        DrawLineEx({r.x + 3, r.y + r.height * 0.8f}, {r.x + r.width * 0.5f, r.y + 3}, 3.f, Color{236, 242, 250, 90});
        if (style == 1 || (hsh >> 7) % 3 == 0) {  // lace
            DrawRectangleRec({r.x, r.y, r.width * 0.32f, r.height}, Color{232, 228, 216, 230});
            DrawRectangleRec({r.x + r.width * 0.68f, r.y, r.width * 0.32f, r.height}, Color{232, 228, 216, 230});
        } else if (style == 2) {  // wooden shutters (panjur) closed against the sun
            DrawRectangleRec(r, Color{96, 120, 96, 255});
            for (float yy = r.y + 4; yy < r.y + r.height; yy += 7) DrawLineEx({r.x, yy}, {r.x + r.width, yy}, 2.f, Color{70, 92, 72, 255});
        }
        DrawLineEx({r.x + r.width / 2, r.y}, {r.x + r.width / 2, r.y + r.height}, 3.f, Color{120, 112, 104, 255});
        DrawRectangleRec({r.x - 8, r.y + r.height + 3, r.width + 16, 6}, Color{200, 194, 184, 255});
        if (m.season == 0 || m.season == 1) {  // geraniums in pots on the sill (spring, summer)
            if ((hsh >> 3) % 2 == 0)
                for (int k = 0; k < 3; ++k) {
                    Vector2 pp{r.x + r.width * (0.2f + 0.3f * k), r.y + r.height - 2.f};
                    DrawRectangleRec({pp.x - 5, pp.y, 10, 7}, Color{170, 90, 60, 255});
                    ellipse({pp.x, pp.y - 3}, 8, 6, Color{60, 110, 50, 255}, 10);
                    ellipse({pp.x + 2, pp.y - 6}, 3.5f, 3.f, (k + (int)(hsh >> 5)) % 2 ? Color{214, 40, 50, 255} : Color{236, 110, 150, 255}, 8);
                }
        }
        if (m.snowy()) DrawRectangleRec({r.x - 9, r.y + r.height, r.width + 18, 5}, Color{240, 243, 248, 255});
        (void)rng;
        return;
    }
    DrawRectangleRec({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, Color{34, 36, 48, 255});
    if (style == 0) {  // dark window, faint reflection
        rectGradV(r, Color{18, 24, 44, 255}, Color{10, 14, 26, 255});
        DrawLineEx({r.x + 3, r.y + r.height * 0.8f}, {r.x + r.width * 0.5f, r.y + 3}, 2.f, Color{60, 70, 100, 80});
    } else if (style == 1) {  // warm light behind curtains
        rectGradV(r, Color{255, 196, 110, 255}, Color{220, 140, 60, 255});
        for (float x = r.x; x < r.x + r.width; x += 6) DrawLineEx({x, r.y}, {x + 2, r.y + r.height}, 2.f, Color{200, 120, 50, 90});
        DrawRectangleRec({r.x, r.y, r.width * 0.3f, r.height}, Color{230, 200, 170, 200});
        DrawRectangleRec({r.x + r.width * 0.7f, r.y, r.width * 0.3f, r.height}, Color{230, 200, 170, 200});
        DrawCircleGradient({r.x + r.width / 2, r.y + r.height / 2}, r.width * 0.9f, Color{255, 200, 120, 40}, Color{255, 200, 120, 0});
    } else {  // blue TV flicker
        rectGradV(r, Color{110, 150, 230, 255}, Color{50, 80, 170, 255});
        DrawRectangleRec({r.x, r.y + r.height * 0.55f, r.width, r.height * 0.45f}, Color{40, 50, 90, 200});
    }
    // mullion + sill
    DrawLineEx({r.x + r.width / 2, r.y}, {r.x + r.width / 2, r.y + r.height}, 3.f, Color{40, 40, 50, 255});
    DrawRectangleRec({r.x - 8, r.y + r.height + 3, r.width + 16, 6}, Color{70, 72, 84, 255});
    if (m.snowy()) DrawRectangleRec({r.x - 9, r.y + r.height, r.width + 18, 4}, Color{150, 158, 186, 255});
    (void)rng;
}

void sign(const StreetMap& m, Rectangle r, const char* text, Color bg, Color fg, float size) {
    if (m.lampsOn()) DrawCircleGradient({r.x + r.width / 2, r.y + r.height / 2}, r.width * 0.8f, alpha(bg, 0.35f), alpha(bg, 0.f));
    DrawRectangleRec(r, bg);
    frameRect(r, 3.f, scaleRgb(bg, 0.6f));
    textC(FontId::Sign, text, {r.x + r.width / 2, r.y + r.height / 2}, size, fg, 3.f);
}

// Night-time stucco: slate blue to a dull mauve (the street lamps add the warmth where they reach).
Color nightWall(okey::Rng& rng) { return mix(Color{40, 42, 58, 255}, Color{60, 52, 54, 255}, rng.uniform(0.f, 1.f)); }
// By day: the old plastered blocks in faded pastels.
Color dayWall(float a) {
    static const Color kWalls[5] = {{214, 192, 160, 255}, {190, 170, 152, 255}, {196, 202, 188, 255}, {222, 204, 178, 255}, {204, 176, 160, 255}};
    return kWalls[((int)std::floor(a * 3.7f + 100.f)) % 5];
}

// Apartment block body from the pavement to `top`: cornice bands and the upper floors' windows (no ground floor).
void apartment(const StreetMap& m, float a, float w, float top, int floors, Color wallc, okey::Rng& rng) {
    const float storey = 3.1f, groundH = 3.4f;
    wallc = m.dn(wallc, dayWall(a));
    rectGradV(m.R(a, 0, w, top), scaleRgb(wallc, 0.7f), wallc);
    if (m.snowy()) DrawRectangleRec(m.R(a - 0.1f, top - 0.05f, w + 0.2f, 0.16f), m.dn(Color{150, 158, 186, 255}, Color{242, 244, 248, 255}));
    for (int f = 0; f <= floors; ++f) DrawRectangleRec(m.R(a, groundH + f * storey - 0.12f, w, 0.18f), scaleRgb(wallc, 1.25f));
    int nw = std::max(2, (int)(w / 1.6f));
    for (int f = 0; f < floors; ++f)
        for (int k = 0; k < nw; ++k) {
            float wa = a + (k + 0.5f) * w / nw - 0.45f, wy = groundH + f * storey + 0.8f;
            float r = rng.uniform(0.f, 1.f);
            int style = r < 0.35f ? 1 : (r < 0.42f ? 2 : 0);
            windowLit(m, wa, wy, 0.9f, 1.5f, style, rng);
            if (rng.chance(0.3f)) {  // balcony railing
                Rectangle rail = m.R(wa - 0.3f, wy - 0.15f, 1.5f, 0.9f);
                const Color rc = m.dn(Color{30, 30, 36, 255}, Color{60, 58, 58, 255});
                frameRect(rail, 3.f, rc);
                for (float x = rail.x; x < rail.x + rail.width; x += 8) DrawLineEx({x, rail.y}, {x, rail.y + rail.height}, 1.5f, rc);
                if (m.day() && m.season <= 1)  // flowers spilling over the railing
                    for (float x = rail.x + 6; x < rail.x + rail.width - 4; x += 13) {
                        ellipse({x, rail.y + 2}, 7, 5, Color{70, 120, 56, 255}, 8);
                        ellipse({x + 2, rail.y - 1}, 3, 3, ((int)x / 13) % 2 ? Color{220, 50, 70, 255} : Color{250, 200, 70, 255}, 6);
                    }
                if (m.snowy()) DrawRectangleRec({rail.x - 2, rail.y - 3, rail.width + 4, 5}, m.dn(Color{150, 158, 186, 255}, Color{242, 244, 248, 255}));
            }
            if (rng.chance(0.1f)) {  // satellite dish
                Vector2 p = m.P(wa + 1.0f, wy + 1.3f);
                ellipse(p, 10, 12, m.dn(Color{150, 150, 158, 255}, Color{226, 226, 230, 255}));
            }
        }
}

// Ground-floor shop front in [ga, ga + gw]: 0 bakkal (open, lit), 1 tailor behind a rolled-down shutter,
// 2 pharmacy (closed; its lit "E" still hangs out over the pavement), 3 apartment door + barber (closed).
// `k` scales every height: 1 under an apartment block (3.4 m ground floor), less in the low shop row.
void shopFront(const StreetMap& m, float ga, float gw, int kind, float k, okey::Rng& rng) {
    if (kind == 0) {  // bakkal: lit shop window + awning + crates
        Rectangle shop = m.R(ga, 0.2f * k, gw, 2.5f * k);
        rectGradV(shop, Color{178, 140, 86, 255}, Color{122, 88, 50, 255});
        for (int s = 0; s < 3; ++s) DrawRectangleRec(m.R(ga + 0.2f, (0.7f + s * 0.6f) * k, gw - 0.4f, 0.06f), Color{120, 80, 40, 255});
        for (int s = 0; s < 40; ++s) {
            Vector2 p = m.P(ga + rng.uniform(0.3f, gw - 0.3f), (0.8f + rng.range(0, 2) * 0.6f + 0.12f) * k);
            ellipse(p, 5, 7, rng.chance(0.5f) ? Color{220, 60, 40, 255} : Color{60, 140, 60, 255}, 8);
        }
        Rectangle aw = m.R(ga - 0.2f, 2.7f * k, gw + 0.4f, 0.6f * k);
        for (int s = 0; s < 12; ++s)
            DrawRectangleRec({aw.x + s * aw.width / 12, aw.y, aw.width / 12, aw.height}, s % 2 ? Color{176, 36, 36, 255} : Color{206, 196, 178, 255});
        sign(m, m.R(ga + gw * 0.2f, 3.35f * k, gw * 0.6f, 0.6f * k), "BAKKAL", Color{200, 30, 30, 255}, Color{255, 240, 200, 255}, 44 * k);
        for (int c = 0; c < 3; ++c) DrawRectangleRec(m.R(ga + 0.3f + c * 0.7f, 0.0f, 0.6f, 0.4f * k), Color{150, 110, 60, 255});
    } else if (kind == 1 && m.day()) {  // the tailor is open by day: jackets in the window
        Rectangle shop = m.R(ga, 0.2f * k, gw, 2.5f * k);
        rectGradV(shop, Color{150, 140, 126, 255}, Color{104, 94, 84, 255});
        for (int j = 0; j < 3; ++j) {
            const float jx = ga + 0.25f + j * (gw - 0.5f) / 3.f;
            const Color jc = j == 0 ? Color{60, 64, 80, 255} : j == 1 ? Color{110, 84, 60, 255} : Color{70, 72, 70, 255};
            DrawRectangleRounded(m.R(jx, 0.8f * k, (gw - 0.6f) / 3.2f, 1.1f * k), 0.25f, 4, jc);
            ellipse(m.P(jx + (gw - 0.6f) / 6.4f, 2.0f * k), m.S() * 0.07f, m.S() * 0.06f, Color{40, 38, 36, 255}, 8);
        }
        frameRect(shop, 4.f, Color{70, 66, 60, 255});
        DrawRectangleRec(m.R(ga, 2.75f * k, gw, 0.08f * k), Color{60, 62, 70, 255});  // the shutter rolled up in its box
        sign(m, m.R(ga + gw * 0.15f, 3.0f * k, gw * 0.7f, 0.5f * k), "TERZİ", Color{34, 34, 40, 255}, Color{176, 168, 150, 255}, 34 * k);
    } else if (kind == 1) {  // closed shop: rolled-down shutter (kepenk) with graffiti
        Rectangle sh = m.R(ga, 0.f, gw, 2.8f * k);
        rectGradV(sh, Color{50, 52, 62, 255}, Color{30, 32, 40, 255});
        for (float y = sh.y; y < sh.y + sh.height; y += 7) DrawLineEx({sh.x, y}, {sh.x + sh.width, y}, 1.5f, Color{22, 24, 32, 255});
        textC(FontId::Hand, "Aşk  bu  sokakta", {sh.x + sh.width * 0.5f, sh.y + sh.height * 0.5f}, 36, Color{150, 50, 110, 200});
        sign(m, m.R(ga + gw * 0.15f, 3.0f * k, gw * 0.7f, 0.5f * k), "TERZİ", Color{34, 34, 40, 255}, Color{176, 168, 150, 255}, 34 * k);
    } else if (kind == 2) {  // pharmacy, closed for the night (the next one is on duty)
        Rectangle shop = m.R(ga, 0.2f * k, gw, 2.5f * k);
        if (m.day()) rectGradV(shop, Color{214, 226, 222, 255}, Color{170, 186, 182, 255});  // open: bright shelves
        else rectGradV(shop, Color{34, 46, 50, 255}, Color{18, 24, 28, 255});
        for (int s = 0; s < 3; ++s) DrawRectangleRec(m.R(ga + 0.2f, (0.7f + s * 0.6f) * k, gw - 0.4f, 0.05f), m.dn(Color{64, 72, 82, 255}, Color{140, 150, 158, 255}));
        // the duty-pharmacy notice taped inside the glass door, lit by the night light
        Rectangle note = m.R(ga + 0.35f, 1.1f * k, 0.42f, 0.55f * k);
        DrawRectangleRec(note, Color{150, 156, 150, 255});
        for (int l = 0; l < 4; ++l) DrawRectangleRec(m.R(ga + 0.4f, (1.2f + l * 0.1f) * k, 0.32f, 0.03f), Color{90, 60, 64, 255});
        sign(m, m.R(ga + gw * 0.15f, 2.9f * k, gw * 0.7f, 0.6f * k), "ECZANE", Color{58, 60, 66, 255}, Color{130, 30, 36, 255}, 44 * k);
        Rectangle e = m.R(ga + gw - 0.4f, 2.3f * k, 0.6f * k, 0.6f * k);
        if (m.lampsOn()) DrawCircleGradient({e.x + e.width / 2, e.y + e.height / 2}, e.width * 1.3f, Color{255, 50, 60, 110}, Color{255, 50, 60, 0});
        DrawRectangleRec(e, Color{220, 26, 36, 255});
        frameRect(e, 3.f, Color{255, 120, 120, 255});
        textC(FontId::Sign, "E", {e.x + e.width / 2, e.y + e.height / 2}, 48 * k, WHITE);
    } else {  // apartment door with a lamp, barber next to it
        Rectangle door = m.R(ga + 0.4f, 0.f, 1.3f, 2.4f * k);
        DrawRectangleRec(door, Color{48, 32, 26, 255});
        DrawRectangleRec(inset(door, 8), Color{90, 72, 50, 255});
        if (m.lampsOn()) DrawCircleGradient(m.P(ga + 1.05f, 2.7f * k), 40, Color{255, 220, 150, 200}, Color{255, 200, 120, 0});
        ellipse(m.P(ga + 1.05f, 2.7f * k), 7, 9, m.lampsOn() ? Color{255, 240, 200, 255} : Color{200, 196, 186, 255});
        Rectangle shop = m.R(ga + 2.1f, 0.2f * k, gw - 2.2f, 2.4f * k);
        if (m.day()) {  // the barber is open: mirror, chair
            rectGradV(shop, Color{186, 182, 172, 255}, Color{140, 134, 126, 255});
            DrawRectangleRec(m.R(ga + 2.4f, 1.2f * k, gw - 2.8f, 0.8f * k), Color{200, 214, 222, 255});
            DrawRectangleRounded(m.R(ga + 2.1f + (gw - 2.2f) * 0.4f, 0.3f * k, 0.6f, 0.75f * k), 0.3f, 4, Color{120, 40, 36, 255});
        } else {
            rectGradV(shop, Color{40, 38, 42, 255}, Color{24, 22, 26, 255});  // the barber has closed for the night
            DrawCircleGradient(m.P(ga + 2.1f + (gw - 2.2f) * 0.3f, 1.6f * k), m.S() * 0.6f, Color{120, 140, 170, 50}, Color{120, 140, 170, 0});
        }
        sign(m, m.R(ga + 2.2f, 2.8f * k, gw - 2.4f, 0.5f * k), "BERBER", Color{26, 34, 80, 255}, Color{220, 222, 236, 255}, 36 * k);
        Rectangle pole = m.R(ga + 1.9f, 1.0f * k, 0.18f, 1.1f * k);
        DrawRectangleRec(pole, Color{140, 140, 148, 255});
        for (float y = pole.y; y < pole.y + pole.height; y += 14) tri({pole.x, y}, {pole.x + pole.width, y + 6}, {pole.x, y + 8}, Color{170, 30, 30, 255});
    }
}

// Sodium street lamp on our side of the street: a warm pool on the facade and on the pavement under it.
void lampPool(const StreetMap& m, float a, float y, float radius) {
    if (!m.lampsOn()) return;
    Vector2 lp = m.P(a, y);
    DrawCircleGradient(lp, m.S() * radius, Color{255, 160, 70, 64}, Color{255, 150, 60, 0});
    ellipseGrad(m.P(a, 0.05f), m.S() * radius * 0.9f, m.S() * 0.35f, Color{255, 170, 80, 70}, Color{255, 160, 70, 0});
}

void overheadCables(const StreetMap& m) {
    for (int k = 0; k < 3; ++k) {
        Vector2 p0 = m.P(m.a0, 5.2f + k * 0.5f), p1 = m.P(m.a1, 4.8f + k * 0.7f), c = m.P((m.a0 + m.a1) * 0.5f, 4.2f + k * 0.4f);
        Vector2 pts[3] = {p0, c, p1};
        DrawSplineBezierQuadratic(pts, 3, 2.f, m.dn(Color{10, 10, 16, 255}, Color{40, 40, 44, 255}));
    }
}

// ---- the street outside the left windows: a night view with depth. Across the narrow street, opposite both
// windows, stands a row of old single-storey shops, so each pane shows navy sky over their flat roofs: the far side
// of the city on a hill (window lights), a floodlit mosque, and a lit büfe with its neon sign still on.

// Deep navy sky, a little lighter and warmer where the city's glow sits on the horizon.
void nightSky(const StreetMap& m, float yHorizon, okey::Rng& rng) {
    const Rectangle sky = m.R(m.a0, yHorizon, m.a1 - m.a0, m.y1 - yHorizon);
    if (m.phase == 3) {
        rectGradV(sky, Color{3, 5, 14, 255}, Color{18, 24, 52, 255});
        rectGradV(m.R(m.a0, yHorizon, m.a1 - m.a0, 1.1f), Color{28, 34, 70, 0}, Color{34, 38, 74, 200});
    } else {
        // zenith -> horizon: morning pale gold, noon blue, evening an orange glow under a violet sky; winter greyer
        Color top, hor;
        if (m.phase == 0) top = {112, 156, 212, 255}, hor = {246, 218, 176, 255};
        else if (m.phase == 1) top = {66, 128, 214, 255}, hor = {184, 214, 240, 255};
        else top = {44, 46, 104, 255}, hor = {246, 136, 80, 255};
        if (m.snowy()) top = mix(top, Color{160, 166, 178, 255}, 0.55f), hor = mix(hor, Color{214, 216, 222, 255}, 0.55f);
        rectGradV(sky, top, hor);
        if (m.phase == 2) rectGradV(m.R(m.a0, yHorizon, m.a1 - m.a0, 0.9f), Color{255, 150, 90, 0}, Color{255, 170, 100, 160});
        // a few soft clouds (their own random stream: the night's layout stays as it is)
        okey::Rng cr(0xC10D + (uint32_t)m.px.y);
        const Color cc = m.phase == 2 ? Color{250, 170, 140, 150} : (m.snowy() ? Color{228, 230, 236, 170} : Color{250, 250, 252, 150});
        for (int i = 0; i < 9; ++i) {
            const float a = cr.uniform(m.a0, m.a1), y = cr.uniform(yHorizon + 1.2f, m.y1 - 0.4f), w = cr.uniform(0.8f, 2.2f);
            for (int k = 0; k < 5; ++k)
                ellipse(m.P(a + (k - 2) * w * 0.22f, y + cr.uniform(-0.05f, 0.12f)), m.S() * w * cr.uniform(0.18f, 0.3f),
                        m.S() * w * cr.uniform(0.07f, 0.12f), cc, 18);
        }
    }
    for (int i = 0; i < 30; ++i) {  // a few faint stars: the city lets through no more
        Vector2 p = m.P(rng.uniform(m.a0, m.a1), rng.uniform(yHorizon + 1.6f, m.y1));
        unsigned char a = (unsigned char)rng.range(50, 150);
        if (m.phase == 3) DrawRectangleV(p, {2.f, 2.f}, Color{190, 200, 255, a});
    }
}

// The far side of the city on a hill: a dark silhouette sprinkled with windows and street lamps.
void cityHill(const StreetMap& m, float a0, float a1, float yBase, float yTop, okey::Rng& rng) {
    auto h = [&](float t) {
        return yBase + (yTop - yBase) * (0.62f + 0.24f * std::sin(t * 4.3f + 0.6f) + 0.14f * std::sin(t * 11.7f + 2.f));
    };
    // far away in the haze by day, a dark ridge against the dusk
    const Color hillC = m.day() ? (m.phase == 0 ? Color{150, 150, 166, 255} : Color{132, 146, 170, 255})
                                : (m.phase == 2 ? Color{40, 34, 58, 255} : Color{11, 13, 26, 255});
    const int N = 64;
    for (int i = 0; i < N; ++i) {
        float t0 = (float)i / N, t1 = (float)(i + 1) / N;
        quad2(m.P(a0 + (a1 - a0) * t0, yBase), m.P(a0 + (a1 - a0) * t1, yBase), m.P(a0 + (a1 - a0) * t1, h(t1)),
              m.P(a0 + (a1 - a0) * t0, h(t0)), hillC);
    }
    // blocky rooftops on the ridge
    for (float a = a0; a < a1 - 0.2f; a += rng.uniform(0.25f, 0.6f)) {
        float t = (a - a0) / (a1 - a0), w = rng.uniform(0.18f, 0.4f);
        const float rh = h(t) - yBase + rng.uniform(0.02f, 0.09f);
        DrawRectangleRec(m.R(a, yBase, w, rh), hillC);
        if (m.snowy()) DrawRectangleRec(m.R(a, yBase + rh - 0.025f, w, 0.025f), m.day() ? Color{236, 238, 244, 255} : Color{96, 102, 130, 255});
    }
    for (int i = 0; i < 150; ++i) {
        float t = rng.uniform(0.f, 1.f), a = a0 + (a1 - a0) * t;
        float y = rng.uniform(yBase, h(t) - 0.03f);
        float r = rng.uniform(0.f, 1.f);
        Color c = r < 0.6f ? Color{255, 196, 120, 255} : (r < 0.85f ? Color{255, 160, 70, 255} : Color{190, 214, 255, 255});
        c.a = (unsigned char)rng.range(110, 255);
        if (m.day()) c = Color{110, 118, 138, (unsigned char)(c.a / 2)};
        DrawRectangleV(m.P(a, y), {rng.uniform(1.5f, 3.f), rng.uniform(1.5f, 2.5f)}, c);
    }
}

// A distant mosque, floodlit against the night: a lead dome washed gold from below, half domes, a row of lit
// windows on the drum, and two pencil minarets with rings of light on their balconies.
void distantMosque(const StreetMap& m, float a, float yBase, float s) {
    auto P = [&](float x, float y) { return m.P(a + x * s, yBase + y * s); };
    const float px = m.S() * s;  // canvas pixels per unit of the mosque's own scale
    if (m.day()) {  // pale stone and lead domes in the haze
        const Color stone{196, 192, 184, 255}, stoneD{168, 164, 158, 255}, lead{128, 136, 148, 255};
        DrawRectangleRec(m.R(a - 0.75f * s, yBase, 1.5f * s, 0.42f * s), stoneD);
        for (int side = -1; side <= 1; side += 2) DrawCircleSector(P(side * 0.52f, 0.42f), px * 0.24f, 180.f, 360.f, 20, lead);
        DrawRectangleRec(m.R(a - 0.42f * s, yBase + 0.42f * s, 0.84f * s, 0.12f * s), stone);
        DrawCircleSector(P(0.f, 0.54f), px * 0.44f, 180.f, 360.f, 32, lead);
        DrawLineEx(P(0.f, 0.98f), P(0.f, 1.1f), 2.f, Color{180, 170, 130, 255});
        for (int side = -1; side <= 1; side += 2) {
            float x = side * 0.95f;
            DrawRectangleRec(m.R(a + (x - 0.04f) * s, yBase, 0.08f * s, 1.7f * s), stone);
            tri(P(x - 0.055f, 1.7f), P(x + 0.055f, 1.7f), P(x, 2.05f), lead);
            for (float by : {0.95f, 1.38f}) DrawRectangleRec(m.R(a + (x - 0.075f) * s, yBase + by * s, 0.15f * s, 0.03f * s), stoneD);
        }
        return;
    }
    ellipseGrad(P(0.f, 0.45f), px * 1.5f, px * 0.75f, Color{255, 180, 100, 44}, Color{255, 180, 100, 0});
    rectGradV(m.R(a - 0.75f * s, yBase, 1.5f * s, 0.42f * s), Color{112, 92, 68, 255}, Color{150, 120, 80, 255});
    for (int side = -1; side <= 1; side += 2) DrawCircleSector(P(side * 0.52f, 0.42f), px * 0.24f, 180.f, 360.f, 20, Color{104, 88, 70, 255});
    rectGradV(m.R(a - 0.42f * s, yBase + 0.42f * s, 0.84f * s, 0.12f * s), Color{120, 100, 74, 255}, Color{156, 126, 84, 255});
    for (int k = 0; k < 7; ++k) DrawRectangleRec(m.R(a + (-0.33f + k * 0.11f) * s, yBase + 0.45f * s, 0.04f * s, 0.06f * s), Color{255, 214, 140, 255});
    // the dome in horizontal slices: brighter toward the floodlights below, lead grey at the crown
    const Vector2 dc = P(0.f, 0.54f);
    const float R = px * 0.44f;
    for (int i = 0; i < 16; ++i) {
        float y0 = R * i / 16.f, y1 = R * (i + 1) / 16.f;
        float hw = std::sqrt(std::max(0.f, R * R - y0 * y0));
        Color c = mix(Color{170, 138, 90, 255}, Color{70, 64, 66, 255}, (float)i / 15.f);
        DrawRectangleRec({dc.x - hw, dc.y - y1, 2.f * hw, y1 - y0 + 0.5f}, c);
    }
    DrawLineEx(P(0.f, 0.98f), P(0.f, 1.1f), 2.f, Color{210, 180, 110, 255});
    for (int side = -1; side <= 1; side += 2) {  // minarets
        float x = side * 0.95f;
        rectGradV(m.R(a + (x - 0.04f) * s, yBase, 0.08f * s, 1.7f * s), Color{120, 104, 84, 255}, Color{150, 122, 84, 255});
        tri(P(x - 0.055f, 1.7f), P(x + 0.055f, 1.7f), P(x, 2.05f), Color{78, 72, 72, 255});
        for (float by : {0.95f, 1.38f}) {  // şerefe with its ring of lights
            DrawRectangleRec(m.R(a + (x - 0.075f) * s, yBase + by * s, 0.15f * s, 0.03f * s), Color{255, 226, 160, 255});
            ellipseGrad(P(x, by + 0.015f), px * 0.14f, px * 0.05f, Color{255, 230, 170, 110}, Color{255, 230, 170, 0});
        }
    }
}

// Neon lettering: a soft coloured bloom, the tube itself, a hot pale core.
void neonText(const char* s, Vector2 c, float size, Color tube, bool lit) {
    if (!lit) {  // switched off by day: just the glass tube
        textC(FontId::Hand, s, c, size, mix(tube, Color{200, 190, 190, 255}, 0.55f));
        return;
    }
    Vector2 ms = ui::measureText(FontId::Hand, s, size);
    ellipseGrad(c, ms.x * 0.8f, size * 0.95f, alpha(tube, 0.3f), alpha(tube, 0.f));
    for (int k = 0; k < 8; ++k) {
        float ang = k * PI / 4.f;
        textC(FontId::Hand, s, {c.x + std::cos(ang) * 3.f, c.y + std::sin(ang) * 3.f}, size, alpha(tube, 0.22f));
    }
    textC(FontId::Hand, s, c, size, tube);
    textC(FontId::Hand, s, c, size, alpha(mix(tube, WHITE, 0.7f), 0.55f));
}

// The büfe (newsagent / tobacconist) is still open: fluorescent light, packed shelves, the owner with his paper.
void bufe(const StreetMap& m, float ga, float gw, okey::Rng& rng) {
    Rectangle win = m.R(ga, 0.35f, gw * 0.62f, 1.5f);
    rectGradV(win, Color{140, 166, 154, 255}, Color{96, 118, 110, 255});
    for (int s = 0; s < 4; ++s) {  // shelves of cigarettes, sweets, lighters
        float y = 0.6f + s * 0.3f;
        DrawRectangleRec(m.R(ga + 0.08f, y, gw * 0.62f - 0.16f, 0.035f), Color{60, 64, 62, 255});
        for (float x = ga + 0.12f; x < ga + gw * 0.62f - 0.18f; x += rng.uniform(0.07f, 0.12f)) {
            Color c = mix(Color{200, 60, 50, 255}, Color{220, 200, 90, 255}, rng.uniform(0.f, 1.f));
            if (rng.chance(0.35f)) c = mix(Color{60, 110, 170, 255}, WHITE, rng.uniform(0.f, 0.5f));
            DrawRectangleRec(m.R(x, y + 0.035f, 0.055f, rng.uniform(0.09f, 0.18f)), c);
        }
    }
    // the owner behind the counter, reading
    Vector2 head = m.P(ga + gw * 0.45f, 1.55f);
    ellipse(head, m.S() * 0.11f, m.S() * 0.13f, Color{34, 36, 40, 255});
    DrawRectangleRounded(m.R(ga + gw * 0.45f - 0.26f, 0.95f, 0.52f, 0.52f), 0.5f, 6, Color{34, 36, 40, 255});
    DrawRectangleRec(m.R(ga + gw * 0.45f - 0.3f, 1.15f, 0.36f, 0.3f), Color{200, 204, 196, 255});
    DrawRectangleRec(m.R(ga, 0.35f, gw * 0.62f, 0.62f), Color{70, 60, 50, 255});  // counter front
    frameRect(win, 5.f, Color{40, 44, 50, 255});
    // glass door, the light spilling out onto the pavement
    Rectangle door = m.R(ga + gw * 0.66f, 0.f, gw * 0.3f, 1.9f);
    rectGradV(door, Color{96, 118, 112, 255}, Color{64, 80, 76, 255});
    frameRect(door, 5.f, Color{40, 44, 50, 255});
    if (m.lampsOn()) ellipseGrad(m.P(ga + gw * 0.4f, 0.05f), m.S() * gw * 0.6f, m.S() * 0.3f, Color{170, 210, 190, 70}, Color{170, 210, 190, 0});
    // sign band: dark board, red neon script, a blue neon tube under it
    Rectangle band = m.R(ga - 0.1f, 1.98f, gw + 0.2f, 0.66f);
    DrawRectangleRec(band, Color{22, 22, 28, 255});
    neonText("Büfe", {band.x + band.width * 0.36f, band.y + band.height * 0.5f}, 84.f, Color{255, 56, 72, 255}, m.lampsOn());
    Vector2 u0 = m.P(ga + gw * 0.68f, 2.12f), u1 = m.P(ga + gw - 0.05f, 2.12f);
    const Color tekel = m.lampsOn() ? Color{150, 200, 255, 255} : Color{176, 196, 214, 255};
    if (m.lampsOn()) ellipseGrad({(u0.x + u1.x) * 0.5f, u0.y - 14.f}, (u1.x - u0.x) * 0.75f, 34.f, Color{80, 150, 255, 80}, Color{80, 150, 255, 0});
    DrawLineEx(u0, u1, 4.f, tekel);
    textC(FontId::Sign, "TEKEL", {(u0.x + u1.x) * 0.5f, u0.y - 26.f}, 38.f, tekel, 3.f);
}

// The row of single-storey shops opposite our windows (a flat roof with a parapet, stove pipes, an antenna).
void lowShopRow(const StreetMap& m, float a0, float a1, float roofY, okey::Rng& rng) {
    const Color wallc = m.dn(Color{44, 42, 52, 255}, Color{200, 186, 166, 255});
    rectGradV(m.R(a0, 0.f, a1 - a0, roofY), scaleRgb(wallc, 1.1f), scaleRgb(wallc, 0.7f));
    DrawRectangleRec(m.R(a0 - 0.05f, roofY - 0.13f, a1 - a0 + 0.1f, 0.13f), m.dn(Color{58, 56, 66, 255}, Color{182, 170, 154, 255}));  // cornice
    DrawRectangleRec(m.R(a0 - 0.05f, roofY - 0.16f, a1 - a0 + 0.1f, 0.03f), m.dn(Color{24, 24, 30, 255}, Color{120, 112, 104, 255}));
    if (m.snowy()) DrawRectangleRec(m.R(a0 - 0.05f, roofY - 0.02f, a1 - a0 + 0.1f, 0.1f), m.dn(Color{150, 158, 186, 255}, Color{242, 244, 248, 255}));
    const bool stoves = m.season >= 2;  // the stoves on the roofs smoke in autumn and winter
    for (float x : {a0 + 1.3f, a0 + 5.1f, a1 - 1.6f}) {  // stove pipes (soba borusu), each with a thread of smoke
        float h = rng.uniform(0.45f, 0.75f);
        DrawRectangleRec(m.R(x, roofY, 0.07f, h), Color{20, 20, 26, 255});
        DrawRectangleRec(m.R(x - 0.05f, roofY + h, 0.17f, 0.04f), Color{20, 20, 26, 255});
        for (int k = 0; k < 7; ++k)
        {
            const Vector2 sp = m.P(x + 0.04f + k * 0.07f + rng.uniform(-0.03f, 0.03f), roofY + h + 0.08f + k * 0.1f);
            const Color smoke = m.day() ? Color{220, 220, 226, (unsigned char)(70 - k * 8)} : Color{90, 92, 116, (unsigned char)(46 - k * 5)};
            if (stoves) ellipse(sp, m.S() * (0.05f + k * 0.018f), m.S() * (0.04f + k * 0.012f), smoke, 12);
        }
    }
    Vector2 ant = m.P(a0 + 3.2f, roofY);  // TV aerial
    Vector2 top = m.P(a0 + 3.2f, roofY + 0.9f);
    const Color antC = m.dn(Color{24, 24, 30, 255}, Color{70, 70, 76, 255});
    DrawLineEx(ant, top, 2.f, antC);
    for (int k = 0; k < 4; ++k) {
        float y = roofY + 0.5f + k * 0.12f, w = 0.36f - k * 0.06f;
        DrawLineEx(m.P(a0 + 3.2f - w, y), m.P(a0 + 3.2f + w, y), 2.f, antC);
    }
    // shop fronts: the büfe opposite the near window, the shuttered tailor between, the pharmacy opposite the far one
    const float k = 0.82f;
    bufe(m, a0 + 0.5f, 3.6f, rng);
    shopFront(m, a0 + 4.55f, 1.75f, 1, k, rng);
    shopFront(m, a0 + 6.6f, a1 - a0 - 7.0f, 2, k, rng);
    for (float x : {a0 + 4.35f, a0 + 6.4f}) DrawRectangleRec(m.R(x, 0.f, 0.12f, roofY - 0.16f), scaleRgb(wallc, 0.8f));  // pilasters
}

void leftStreet(const StreetMap& m, okey::Rng& rng) {
    const float roofY = 2.98f;  // the shop row's parapet: every left pane shows sky above it from our table
    nightSky(m, 2.6f, rng);
    cityHill(m, 1.5f, 11.4f, 2.6f, 3.32f, rng);
    distantMosque(m, 12.4f, 2.84f, 0.5f);
    // apartment blocks at either end of the shop row
    apartment(m, -1.f, 5.7f, 9.6f, 2, nightWall(rng), rng);
    shopFront(m, -0.6f, 4.9f, 0, 1.f, rng);  // the bakkal opposite our street door, closing up:
    if (m.phase == 3) {
        Rectangle shut = m.R(-0.6f, 1.2f, 4.9f, 1.5f);  // its shutter is half down, the light spills out under it
        rectGradV(shut, Color{48, 50, 60, 255}, Color{34, 36, 44, 255});
        for (float y = shut.y; y < shut.y + shut.height; y += 7) DrawLineEx({shut.x, y}, {shut.x + shut.width, y}, 1.5f, Color{22, 24, 32, 255});
        DrawRectangleRec({shut.x, shut.y + shut.height - 7, shut.width, 7}, Color{74, 76, 86, 255});
    }
    apartment(m, 14.8f, 4.2f, 9.6f, 2, nightWall(rng), rng);
    shopFront(m, 15.2f, 3.4f, 3, 1.f, rng);
    lowShopRow(m, 4.7f, 14.8f, roofY, rng);
}

}  // namespace

void drawStreetCanvas(RenderTexture2D& rt, uint32_t seed, int phase, int season) {
    okey::Rng rng(seed ^ 0x57EE7u);
    beginRoomCanvas(rt);
    rlDisableBackfaceCulling();
    ClearBackground(Color{8, 12, 26, 255});
    // --- left street: the facade across the street (a = distance along the panel, left to right)
    {
        StreetMap m{street::LEFT, 0.f, 18.f, -1.f, 7.f};
        m.phase = phase;
        m.season = season;
        leftStreet(m, rng);
        // parked car at the far kerb (Murat 131 silhouette), in front of the bakkal
        Rectangle body = m.R(0.4f, 0.25f, 4.3f, 0.75f);
        DrawRectangleRounded(body, 0.3f, 6, m.dn(Color{36, 40, 52, 255}, Color{176, 64, 52, 255}));
        quad2(m.P(1.3f, 1.0f), m.P(3.7f, 1.0f), m.P(3.2f, 1.45f), m.P(1.9f, 1.45f), m.dn(Color{30, 34, 46, 255}, Color{160, 56, 46, 255}));
        quad2(m.P(1.5f, 1.03f), m.P(3.5f, 1.03f), m.P(3.1f, 1.38f), m.P(2.0f, 1.38f), m.dn(Color{70, 90, 120, 255}, Color{150, 176, 200, 255}));
        if (m.snowy()) DrawRectangleRounded(m.R(1.85f, 1.42f, 1.4f, 0.08f), 0.5f, 4, m.dn(Color{150, 158, 186, 255}, Color{244, 246, 250, 255}));
        ellipse(m.P(1.2f, 0.25f), m.S() * 0.33f, m.S() * 0.33f, Color{14, 14, 18, 255});
        ellipse(m.P(3.9f, 0.25f), m.S() * 0.33f, m.S() * 0.33f, Color{14, 14, 18, 255});
        DrawLineEx(m.P(0.5f, 0.95f), m.P(4.6f, 0.95f), 3.f, m.dn(Color{140, 150, 170, 160}, Color{230, 230, 236, 200}));
        // pavement kerb
        DrawRectangleRec(m.R(-1.f, -1.f, 20.f, 1.0f), m.snowy() ? m.dn(Color{110, 118, 146, 255}, Color{232, 236, 242, 255}) : m.dn(Color{40, 42, 50, 255}, Color{150, 146, 140, 255}));
        DrawRectangleRec(m.R(-1.f, -0.02f, 20.f, 0.06f), m.dn(Color{90, 90, 96, 255}, Color{196, 192, 186, 255}));
        // a cat on the step
        Vector2 cp = m.P(12.7f, 0.1f);
        ellipse(cp, 14, 9, Color{20, 18, 18, 255});
        ellipse({cp.x + 12, cp.y - 8}, 7, 6, Color{20, 18, 18, 255});
        tri({cp.x + 8, cp.y - 12}, {cp.x + 11, cp.y - 20}, {cp.x + 13, cp.y - 12}, Color{20, 18, 18, 255});
        tri({cp.x + 13, cp.y - 12}, {cp.x + 17, cp.y - 19}, {cp.x + 18, cp.y - 10}, Color{20, 18, 18, 255});
        ellipse({cp.x + 14, cp.y - 9}, 1.5f, 1.2f, Color{200, 220, 90, 255}, 6);
        lampPool(m, 8.6f, 2.2f, 2.6f);  // the street lamp between our windows
        overheadCables(m);
        // night haze over everything (a light summer haze by day, the dusk's orange)
        if (m.phase == 3) rectGradV(street::LEFT, Color{16, 22, 56, 44}, Color{30, 32, 52, 22});
        else if (m.phase == 2) rectGradV(street::LEFT, Color{60, 30, 70, 40}, Color{255, 140, 80, 26});
        else rectGradV(street::LEFT, Color{230, 236, 245, 22}, Color{240, 236, 226, 34});
    }
    // --- back window: across the side street a low bakery (the oven is already lit for the morning bread); over
    // its roof the neighbourhood's roofs and a minaret against the city glow
    {
        StreetMap m{street::BACK, -9.f, 2.f, -1.f, 7.f};
        m.phase = phase;
        m.season = season;
        nightSky(m, 2.4f, rng);
        for (float a = -9.f; a < 2.f; a += rng.uniform(0.35f, 0.8f)) {  // distant roofs, a few lit windows
            float w = rng.uniform(0.4f, 0.9f), top = rng.uniform(2.9f, 3.3f);
            DrawRectangleRec(m.R(a, 2.4f, w, top - 2.4f), m.day() ? Color{132, 132, 148, 255} : m.dn(Color{13, 14, 27, 255}, Color{70, 60, 80, 255}));
            if (m.snowy()) DrawRectangleRec(m.R(a, top - 0.03f, w, 0.03f), m.dn(Color{110, 116, 146, 255}, Color{240, 242, 248, 255}));
            if (rng.chance(0.55f))
                DrawRectangleV(m.P(a + rng.uniform(0.05f, w - 0.12f), top - rng.uniform(0.1f, 0.35f)), {5.f, 6.f},
                               m.day() ? Color{96, 100, 116, 255} : Color{255, 196, 120, 210});
        }
        {  // the minaret: shaft over the roofs, a lit balcony, a lead cone
            const float x = -6.55f;
            DrawRectangleRec(m.R(x - 0.05f, 2.4f, 0.1f, 1.4f), m.dn(Color{24, 22, 38, 255}, Color{200, 196, 188, 255}));
            tri(m.P(x - 0.07f, 3.8f), m.P(x + 0.07f, 3.8f), m.P(x, 4.25f), m.dn(Color{24, 22, 38, 255}, Color{124, 132, 144, 255}));
            DrawRectangleRec(m.R(x - 0.11f, 3.1f, 0.22f, 0.04f), m.dn(Color{40, 38, 56, 255}, Color{176, 172, 164, 255}));
            if (m.lampsOn()) ellipseGrad(m.P(x, 3.13f), 30.f, 12.f, Color{150, 240, 170, 170}, Color{150, 240, 170, 0});
        }
        // the bakery (fırın): a single storey, dark shop, the oven's glow at the back, a warm lit doorway
        const float roofY = 2.72f;
        rectGradV(m.R(-10.f, 0.f, 6.4f, roofY), m.dn(Color{50, 44, 50, 255}, Color{212, 188, 156, 255}), m.dn(Color{32, 30, 36, 255}, Color{176, 154, 128, 255}));
        DrawRectangleRec(m.R(-10.05f, roofY - 0.12f, 6.5f, 0.12f), m.dn(Color{62, 58, 66, 255}, Color{190, 170, 142, 255}));
        if (m.snowy()) DrawRectangleRec(m.R(-10.05f, roofY - 0.02f, 6.5f, 0.1f), m.dn(Color{150, 158, 186, 255}, Color{242, 244, 248, 255}));
        Rectangle shop = m.R(-8.7f, 0.3f, 3.4f, 1.75f);
        rectGradV(shop, m.dn(Color{44, 32, 28, 255}, Color{120, 92, 70, 255}), m.dn(Color{26, 20, 20, 255}, Color{90, 66, 50, 255}));
        ellipseGrad(m.P(-7.0f, 0.75f), m.S() * 1.4f, m.S() * 0.7f, Color{255, 130, 50, (unsigned char)(m.day() ? 60 : 120)}, Color{255, 120, 40, 0});
        for (int k = 0; k < 3; ++k) {  // racks of loaves catching the oven light
            DrawRectangleRec(m.R(-8.5f, 0.75f + k * 0.4f, 3.0f, 0.03f), Color{70, 50, 40, 255});
            for (float x = -8.4f; x < -5.6f; x += 0.22f)
                ellipse(m.P(x + rng.uniform(-0.03f, 0.03f), 0.84f + k * 0.4f), m.S() * 0.09f, m.S() * 0.05f,
                        mix(Color{150, 96, 50, 255}, Color{200, 140, 70, 255}, rng.uniform(0.f, 1.f)), 10);
        }
        frameRect(shop, 5.f, Color{30, 26, 28, 255});
        Rectangle door = m.R(-5.0f, 0.f, 0.9f, 2.0f);
        rectGradV(door, m.dn(Color{200, 140, 70, 255}, Color{110, 82, 58, 255}), m.dn(Color{150, 96, 50, 255}, Color{80, 60, 44, 255}));
        frameRect(door, 6.f, Color{40, 32, 30, 255});
        if (m.lampsOn()) ellipseGrad(m.P(-4.55f, 0.05f), m.S() * 1.1f, m.S() * 0.25f, Color{255, 170, 90, 80}, Color{255, 160, 80, 0});
        sign(m, m.R(-8.3f, 2.12f, 2.6f, 0.46f), "FIRIN", Color{72, 24, 24, 255}, Color{206, 188, 150, 255}, 40);
        // an apartment block beyond the bakery, the barber and a doorway under its lamp
        apartment(m, -3.6f, 5.8f, 6.5f + rng.uniform(-0.6f, 0.6f), 1, nightWall(rng), rng);
        shopFront(m, -3.2f, 5.0f, 3, 1.f, rng);
        lampPool(m, -5.9f, 2.4f, 2.4f);
        DrawRectangleRec(m.R(-9.f, -1.f, 11.f, 1.0f), m.snowy() ? m.dn(Color{110, 118, 146, 255}, Color{232, 236, 242, 255}) : m.dn(Color{40, 42, 50, 255}, Color{150, 146, 140, 255}));
        if (m.phase == 3) rectGradV(street::BACK, Color{16, 22, 56, 40}, Color{30, 32, 52, 20});
        else if (m.phase == 2) rectGradV(street::BACK, Color{60, 30, 70, 36}, Color{255, 140, 80, 24});
    }
    // --- road seen from above: wet cobbles (Arnavut kaldırımı) with lamp reflections
    {
        Rectangle r = street::ROAD;
        const bool day = phase <= 1, lamps = phase >= 2, snowy = season == 3;
        DrawRectangleRec(r, day ? Color{98, 96, 94, 255} : Color{12, 13, 18, 255});
        for (float y = r.y + 2; y < r.y + r.height * 0.78f; y += 9)
            for (float x = r.x + rng.uniform(0.f, 8.f); x < r.x + r.width; x += rng.uniform(9.f, 13.f)) {
                const float t = rng.uniform(0.f, 1.f);
                Color c = day ? mix(Color{112, 110, 106, 255}, Color{146, 142, 136, 255}, t) : mix(Color{22, 24, 32, 255}, Color{40, 42, 52, 255}, t);
                DrawRectangleRounded({x, y, rng.uniform(7.f, 11.f), 7}, 0.5f, 3, c);
            }
        DrawRectangleRec({r.x, r.y + r.height * 0.8f, r.width, r.height * 0.2f}, day ? Color{160, 156, 150, 255} : Color{44, 44, 50, 255});  // our pavement
        DrawRectangleRec({r.x, r.y + r.height * 0.78f, r.width, 5}, day ? Color{192, 188, 182, 255} : Color{80, 80, 88, 255});
        if (snowy) {  // trodden snow, two dark ruts where the cars go
            const Color sn = day ? Color{226, 230, 238, 235} : Color{92, 100, 130, 225};
            DrawRectangleRec(r, sn);
            for (float f : {0.3f, 0.52f}) DrawRectangleRec({r.x, r.y + r.height * f, r.width, 9}, day ? Color{150, 152, 160, 200} : Color{40, 44, 60, 200});
        }
        if (season == 2) {  // fallen leaves along the kerb and on the pavement
            okey::Rng lr(seed ^ 0x1EAFu);
            for (int i = 0; i < 260; ++i) {
                const float y = r.y + r.height * (lr.chance(0.6f) ? lr.uniform(0.72f, 1.f) : lr.uniform(0.f, 0.72f));
                Color c = mix(Color{190, 96, 30, 255}, Color{150, 110, 40, 255}, lr.uniform(0.f, 1.f));
                if (!day) c = scaleRgb(c, 0.35f);
                ellipse({r.x + lr.uniform(0.f, r.width), y}, lr.uniform(2.f, 3.5f), lr.uniform(1.2f, 2.2f), c, 6);
            }
        }
        if (lamps)
            for (int k = 0; k < 3; ++k)
                DrawCircleGradient({r.x + r.width * (0.2f + k * 0.35f), r.y + r.height * 0.45f}, 110, Color{255, 170, 80, 60}, Color{255, 170, 80, 0});
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    endRoomCanvas(rt);
}

// Gold leaf lettering painted on the inside of the street window (seen mirrored from inside): the name on an arch,
// "KIRAATHANESİ" straight beneath it, the year in a hand script. Every line is fitted to the canvas.
void drawLetteringCanvas(RenderTexture2D& rt) {
    beginRoomCanvas(rt);
    rlDisableBackfaceCulling();
    ClearBackground(Color{214, 170, 80, 0});
    const float W = (float)rt.texture.width;
    const Color shade{60, 30, 10, 220}, gold{226, 182, 90, 255};
    const Font& f = ui::font(ui::FontId::Sign);
    auto glyphsOf = [](const std::string& s) {  // UTF-8 characters
        std::vector<std::string> g;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = (unsigned char)s[i];
            size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
            g.push_back(s.substr(i, n));
            i += n;
        }
        return g;
    };
    // the name, glyph by glyph along an arc (apex at `apexY`), shrunk if it would not fit in `maxW`
    auto arched = [&](const std::string& text, float size, float apexY, float maxW) {
        std::vector<std::string> glyphs = glyphsOf(text);
        const float track = 0.045f;  // letter spacing, in font sizes
        auto widthAt = [&](float sz) {
            float t = 0.f;
            for (const std::string& g : glyphs) t += ui::measureText(ui::FontId::Sign, g, sz).x + track * sz;
            return t;
        };
        float total = widthAt(size);
        if (total > maxW) size *= maxW / total, total = widthAt(size);
        const float R = 1400.f;
        const Vector2 c{W / 2, apexY + R};
        float ang = -total / R * 0.5f;
        for (const std::string& g : glyphs) {
            const float w = ui::measureText(ui::FontId::Sign, g, size).x + track * size;
            const float mid = ang + w * 0.5f / R;
            const Vector2 p{c.x + std::sin(mid) * R, c.y - std::cos(mid) * R};
            const Vector2 m = ui::measureText(ui::FontId::Sign, g, size);
            DrawTextPro(f, g.c_str(), {p.x + 3, p.y + 3}, {m.x / 2, m.y / 2}, mid * RAD2DEG, size, 0.f, shade);
            DrawTextPro(f, g.c_str(), p, {m.x / 2, m.y / 2}, mid * RAD2DEG, size, 0.f, gold);
            ang += w / R;
        }
    };
    auto straight = [&](const std::string& text, float size, float y, float maxW) {
        float sp = size * 0.12f;  // spaced capitals
        Vector2 m = MeasureTextEx(f, text.c_str(), size, sp);
        if (m.x > maxW) {
            const float k = maxW / m.x;
            size *= k, sp *= k;
            m = MeasureTextEx(f, text.c_str(), size, sp);
        }
        const Vector2 p{W / 2 - m.x / 2, y - m.y / 2};
        DrawTextEx(f, text.c_str(), {p.x + 2, p.y + 2}, size, sp, shade);
        DrawTextEx(f, text.c_str(), p, size, sp, gold);
    };
    arched("SAKLI BAHÇE", 104.f, 76.f, W * 0.86f);
    straight("KIRAATHANESİ", 50.f, 170.f, W * 0.6f);
    ui::drawTextCentered(ui::FontId::Hand, "~ 1974'ten beri ~", {W / 2, 226}, 38, Color{230, 190, 100, 240});
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    endRoomCanvas(rt);
}

}  // namespace rm
}  // namespace r3d
