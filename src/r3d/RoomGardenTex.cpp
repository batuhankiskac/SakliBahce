// Room module: the garden's textures, its signs and the view around it (room owner).
//   * Image textures: the sandy mortar between the cobbles, the cobble grain, the rubble-stone wall, the çınar's
//     mottled bark, terracotta roof tiles, the striped awning, a louvred shutter.
//   * The view (a canvas on a cylinder around the garden): the sea with the far shore, a mosque, the bridge, ferries and
//     boats; the neighbourhood's houses, roofs and cypresses toward the front. By day, at dusk and by night (the far
//     shore's lights), overcast in rain. The sky dome's colours (gardenSky) are shared with the horizon haze.
#include "r3d/RoomGardenInternal.h"
#include "r3d/Daytime.h"

#include <algorithm>
#include <cmath>

namespace r3d {
namespace rm {

namespace {

inline Color px3(float r, float g, float b, float a = 255.f) {
    return Color{(unsigned char)std::clamp(r, 0.f, 255.f), (unsigned char)std::clamp(g, 0.f, 255.f),
                 (unsigned char)std::clamp(b, 0.f, 255.f), (unsigned char)std::clamp(a, 0.f, 255.f)};
}
Texture2D upload(Image& img) {
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}
// Tileable value noise / fbm with an integer period.
float pv(float x, float y, int p, uint32_t s) {
    auto h = [&](int ix, int iy) {
        ix = ((ix % p) + p) % p;
        iy = ((iy % p) + p) % p;
        return hash1((uint32_t)ix * 73856093u ^ (uint32_t)iy * 19349663u ^ (s * 83492791u));
    };
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = h(x0, y0), b = h(x0 + 1, y0), c = h(x0, y0 + 1), d = h(x0 + 1, y0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
float pfbm(float x, float y, int p, int oct, uint32_t s) {
    float v = 0.f, a = 0.5f, n = 0.f;
    for (int o = 0; o < oct; ++o) {
        v += a * pv(x, y, p, s + (uint32_t)o * 31u);
        n += a;
        x *= 2.f, y *= 2.f, p *= 2, a *= 0.5f;
    }
    return v / n;
}

}  // namespace

// ============================================================================ image textures
Texture2D genGroundTexture(uint32_t seed) {
    const int S = 512;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float)x / S, v = (float)y / S;
            float n = pfbm(u * 8.f, v * 8.f, 8, 4, seed);
            float d = pfbm(u * 3.f, v * 3.f, 3, 3, seed + 5);
            float g = hash1((uint32_t)(x * 7 + y * 131) ^ seed) - 0.5f;
            float k = 0.78f + 0.3f * n + 0.08f * g;
            Color base = mix(Color{150, 138, 116, 255}, Color{118, 112, 92, 255}, d);  // sand, earth
            float moss = std::clamp((d - 0.55f) * 3.f, 0.f, 1.f) * 0.5f;
            base = mix(base, Color{96, 108, 70, 255}, moss);
            px[y * S + x] = px3(base.r * k, base.g * k, base.b * k);
        }
    okey::Rng rng(seed * 3u + 1u);
    for (int i = 0; i < 900; ++i) {  // pebbles
        float cx = rng.uniform(0.f, (float)S), cy = rng.uniform(0.f, (float)S), r = rng.uniform(1.f, 3.2f);
        Color c = mix(Color{196, 186, 166, 255}, Color{90, 84, 76, 255}, rng.uniform());
        ImageDrawCircleV(&img, {cx, cy}, (int)r, c);
    }
    return upload(img);
}

Texture2D genCobbleTexture(uint32_t seed) {
    const int S = 256;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float)x / S, v = (float)y / S;
            float n = pfbm(u * 6.f, v * 6.f, 6, 4, seed);
            float sp = hash1((uint32_t)(x * 31 + y * 977) ^ seed);
            float k = 0.8f + 0.32f * n + (sp > 0.97f ? -0.15f : 0.f) + (sp < 0.02f ? 0.12f : 0.f);
            px[y * S + x] = px3(222 * k, 216 * k, 204 * k);
        }
    return upload(img);
}

// Dry-laid rubble: irregular stones (jittered cells, flattened horizontally), deep mortar joints, each stone its own
// tone of limestone; 512 px = 1.6 m.
Texture2D genRubbleTexture(uint32_t seed) {
    const int S = 512, NX = 7, NY = 10;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    struct P {
        float x, y;
        Color c;
    };
    std::vector<P> pts;
    okey::Rng rng(seed);
    const Color tones[5] = {{196, 184, 160, 255}, {176, 168, 150, 255}, {204, 176, 136, 255}, {150, 146, 136, 255}, {186, 160, 128, 255}};
    for (int j = 0; j < NY; ++j)
        for (int i = 0; i < NX; ++i)
            pts.push_back({((float)i + 0.5f + rng.uniform(-0.32f, 0.32f) + (j % 2) * 0.5f) / NX,
                           ((float)j + 0.5f + rng.uniform(-0.25f, 0.25f)) / NY,
                           scaleRgb(tones[rng.range(0, 4)], rng.uniform(0.85f, 1.08f))});
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (x + 0.5f) / S, v = (y + 0.5f) / S;
            float d1 = 9.f, d2 = 9.f;
            int best = 0;
            for (int k = 0; k < (int)pts.size(); ++k) {
                float dx = u - pts[k].x, dy = v - pts[k].y;
                dx -= std::round(dx), dy -= std::round(dy);
                float d = std::sqrt(dx * dx * 0.55f + dy * dy * 1.6f);
                if (d < d1) d2 = d1, d1 = d, best = k;
                else if (d < d2) d2 = d;
            }
            float edge = d2 - d1;
            float n = pfbm(u * 16.f, v * 16.f, 16, 3, seed + 3);
            Color c;
            if (edge < 0.0075f) {
                float m = 0.6f + 0.3f * n;
                c = px3(92 * m, 86 * m, 76 * m);
            } else {
                float round = std::clamp(edge / 0.05f, 0.f, 1.f);
                float k = (0.72f + 0.28f * round) * (0.85f + 0.3f * n);
                Color b = pts[(size_t)best].c;
                float lich = std::clamp((pfbm(u * 5.f, v * 5.f, 5, 3, seed + 9) - 0.62f) * 4.f, 0.f, 0.6f);
                b = mix(b, Color{150, 150, 110, 255}, lich);
                c = px3(b.r * k, b.g * k, b.b * k);
            }
            px[y * S + x] = c;
        }
    return upload(img);
}

// Plane-tree bark: the outer bark flakes off in plates and leaves a camouflage of cream, olive and grey (u around the
// trunk, v up it; 1 repeat ~ 1.4 m around, 2.4 m up).
Texture2D genBarkTexture(uint32_t seed) {
    const int S = 512;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    const Color c0{206, 198, 168, 255}, c1{150, 146, 112, 255}, c2{118, 110, 92, 255}, c3{174, 174, 140, 255};
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float)x / S, v = (float)y / S;
            float wob = pfbm(u * 4.f, v * 2.f, 4, 2, seed + 7) * 0.6f;
            float a = pfbm(u * 6.f + wob, v * 3.f, 6, 3, seed);
            float b = pfbm(u * 9.f, v * 4.f + wob, 9, 3, seed + 11);
            Color c = a < 0.42f ? c2 : (a < 0.52f ? c1 : (b < 0.5f ? c0 : c3));
            float e = std::min(std::fabs(a - 0.42f), std::min(std::fabs(a - 0.52f), std::fabs(b - 0.5f) + (a < 0.52f ? 1.f : 0.f)));
            float k = 0.9f + 0.2f * pfbm(u * 32.f, v * 16.f, 32, 2, seed + 21);
            if (e < 0.012f) k *= 0.72f;  // the rim of a flaking plate
            float streak = pv(u * 64.f, v * 3.f, 64, seed + 33);
            k *= 0.94f + 0.08f * streak;
            px[y * S + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    return upload(img);
}

// Alaturka tiles: u across the roof (one tile 0.16 m: a channel and a cap), v down the slope (courses of 0.32 m).
// 256 px = 4 tiles across, 2 courses.
Texture2D genRoofTileTexture(uint32_t seed) {
    const int S = 256;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float)x / S * 4.f, v = (float)y / S * 2.f;
            int tile = (int)u, course = (int)v;
            float fu = u - tile, fv = v - course;
            float prof = std::sin(fu * PI);  // channel / cap
            uint32_t id = (uint32_t)(tile * 7 + course * 13);
            float tone = 0.85f + 0.25f * hash1(id ^ seed);
            float k = (0.62f + 0.45f * prof) * tone;
            if (fv > 0.9f) k *= 0.55f + 0.45f * (1.f - (fv - 0.9f) / 0.1f);  // shadow under the next course
            float n = pfbm(x / 32.f, y / 32.f, 8, 3, seed + 5);
            k *= 0.88f + 0.22f * n;
            Color c = mix(Color{178, 88, 52, 255}, Color{150, 98, 70, 255}, hash1(id * 3u + seed));
            float lich = std::clamp((pfbm(x / 20.f, y / 20.f, 12, 2, seed + 9) - 0.66f) * 5.f, 0.f, 0.7f);
            c = mix(c, Color{96, 92, 70, 255}, lich);
            px[y * S + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    return upload(img);
}

// The awning: broad green and cream stripes along v (u = across the stripes), a canvas weave, a few dark rain stains.
Texture2D genAwningTexture(uint32_t seed) {
    const int S = 256;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const bool g = ((x / 32) % 2) == 0;
            Color c = g ? Color{44, 104, 70, 255} : Color{230, 220, 192, 255};
            float weave = ((x + y) % 3 == 0 ? 0.94f : 1.f) * (((x * 3 + y) % 5 == 0) ? 0.97f : 1.f);
            float n = pfbm(x / 64.f, y / 64.f, 4, 3, seed);
            float stain = std::clamp((pfbm(x / 40.f, y / 40.f, 6, 3, seed + 7) - 0.6f) * 3.f, 0.f, 0.3f);
            float k = weave * (0.9f + 0.15f * n) * (1.f - stain);
            px[y * S + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    return upload(img);
}

// A louvred shutter (panjur) in faded green: stiles and rails, slanting slats with a shadow under each.
Texture2D genShutterTexture(uint32_t seed) {
    const int W = 128, H = 256;
    Image img = GenImageColor(W, H, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const bool stile = x < 12 || x >= W - 12 || y < 12 || y >= H - 12 || (y > 122 && y < 134);
            float k;
            if (stile) {
                k = 1.f;
            } else {
                float f = std::fmod((float)y, 9.f) / 9.f;
                k = 0.62f + 0.5f * f;  // each slat lighter at its lower edge, in the shadow of the next at the top
            }
            float n = pfbm(x / 32.f, y / 32.f, 4, 3, seed);
            float peel = pfbm(x / 12.f, y / 12.f, 10, 2, seed + 3) > 0.7f ? 1.f : 0.f;
            Color c = mix(Color{78, 122, 98, 255}, Color{150, 140, 118, 255}, peel * 0.8f);
            k *= 0.88f + 0.2f * n;
            px[y * W + x] = px3(c.r * k, c.g * k, c.b * k);
        }
    return upload(img);
}

// Woven straw: strands crossing in a basket weave (u, v), warm yellow, a little uneven.
Texture2D genStrawTexture(uint32_t seed) {
    const int S = 256, C = 16;
    Image img = GenImageColor(S, S, BLACK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const int cu = x / C, cv = y / C;
            const float fu = (x % C + 0.5f) / C, fv = (y % C + 0.5f) / C;
            const bool horiz = ((cu + cv) & 1) == 0;
            const float across = horiz ? fv : fu, along = horiz ? fu : fv;
            const float k = 0.55f + 0.45f * std::sin(across * PI) * (0.85f + 0.15f * std::sin(along * PI));
            const float g = hash1((uint32_t)(x * 7 + y * 131) ^ seed) * 0.12f;
            px[y * S + x] = px3((206.f + 20.f * g) * k, (170.f + 10.f * g) * k, 100.f * k);
        }
    return upload(img);
}

// ============================================================================ sky
Color gardenSky(int phase, bool overcast, float e) {
    const float t = std::clamp(e / 70.f, 0.f, 1.f);  // 0 horizon .. 1 near the zenith
    const float s = std::sqrt(t);
    Color hz, mid, zen;
    switch (phase) {
    case w3d::Sabah: hz = {238, 222, 200, 255}, mid = {170, 196, 226, 255}, zen = {92, 140, 204, 255}; break;
    case w3d::Ogle: hz = {214, 228, 240, 255}, mid = {150, 190, 232, 255}, zen = {70, 128, 206, 255}; break;
    case w3d::Aksam: hz = {255, 176, 104, 255}, mid = {214, 132, 128, 255}, zen = {66, 70, 134, 255}; break;
    default: hz = {40, 46, 78, 255}, mid = {22, 28, 56, 255}, zen = {8, 12, 30, 255}; break;
    }
    Color c = s < 0.5f ? mix(hz, mid, s * 2.f) : mix(mid, zen, (s - 0.5f) * 2.f);
    if (overcast) {
        const Color g = phase == w3d::Gece ? Color{30, 32, 40, 255} : (phase == w3d::Aksam ? Color{150, 136, 130, 255} : Color{176, 180, 186, 255});
        c = mix(c, scaleRgb(g, 1.f - 0.15f * t), 0.85f);
    }
    return c;
}

// ============================================================================ the view
namespace {

struct View {
    float W = PANO_W, H = PANO_H;
    // azimuth (degrees, 0 = -Z, +90 = +X) and elevation (degrees) -> canvas pixels
    float x(float az) const {
        az = std::fmod(az, 360.f);
        if (az < 0.f) az += 360.f;
        return az / 360.f * W;
    }
    float y(float e) const { return (kPanoHi - e) / (kPanoHi - kPanoLo) * H; }
    float pxPerDeg() const { return W / 360.f; }
};

// A run of the azimuth range [a0, a1] (may wrap past 360) as pixel spans.
template <class F>
void spans(const View& v, float a0, float a1, F f) {
    float x0 = v.x(a0), x1 = x0 + (a1 - a0) * v.pxPerDeg();
    if (x1 <= v.W) {
        f(x0, x1, 0.f);
    } else {
        f(x0, v.W, 0.f);
        f(0.f, x1 - v.W, v.W);
    }
}

}  // namespace

void drawGardenView(RenderTexture2D& rt, uint32_t seed, int phase, int season, bool overcast, float sunAz) {
    const View V;
    const bool night = phase == w3d::Gece, dusk = phase == w3d::Aksam;
    okey::Rng rng(seed * 2654435761u + 77u);
    beginRoomCanvas(rt);
    ClearBackground(Color{0, 0, 0, 0});
    const float eh = 0.4f;  // the sea's horizon (world y ~0.3 at the band's radius)
    const float yh = V.y(eh);
    const Color horizon = gardenSky(phase, overcast, 0.f);
    auto hazeMix = [&](Color c, float k) { return mix(c, horizon, std::clamp(k, 0.f, 1.f)); };

    // ---- the sky's lowest band: haze over the horizon all round (fades out upward, the dome above)
    {
        Color top = horizon;
        top.a = 0;
        rectGradV({0, V.y(9.f), V.W, yh - V.y(9.f) + 1}, top, horizon);
        // a few soft clouds low over the sea and the hills
        if (!night) {
            const int n = overcast ? 70 : 26;
            for (int i = 0; i < n; ++i) {
                float cx = rng.uniform(0.f, V.W), ce = rng.uniform(2.f, overcast ? 26.f : 18.f);
                float w = rng.uniform(90.f, 300.f), h = w * rng.uniform(0.12f, 0.22f);
                Color c = dusk ? Color{255, 196, 160, 255} : (overcast ? Color{200, 202, 206, 255} : Color{250, 250, 252, 255});
                c.a = (unsigned char)(overcast ? rng.range(60, 140) : rng.range(30, 90));
                for (int k = 0; k < 5; ++k)
                    ellipse({cx + rng.uniform(-w * 0.4f, w * 0.4f), V.y(ce) + rng.uniform(-h * 0.3f, h * 0.3f)}, w * rng.uniform(0.25f, 0.45f),
                            h * rng.uniform(0.6f, 1.f), c, 24);
            }
        }
    }

    // ---- the sea: from the far-left over the back to the right (azimuths -110 .. +95)
    const float seaA0 = -110.f, seaA1 = 95.f;
    {
        Color far = hazeMix(night ? Color{24, 32, 56, 255} : (dusk ? Color{176, 120, 120, 255} : Color{110, 150, 182, 255}), 0.35f);
        Color nearC = night ? Color{8, 12, 26, 255} : (dusk ? Color{60, 50, 90, 255} : Color{36, 82, 122, 255});
        if (overcast) far = mix(far, Color{120, 128, 136, 255}, 0.5f), nearC = mix(nearC, Color{50, 60, 70, 255}, 0.5f);
        spans(V, seaA0, seaA1, [&](float x0, float x1, float) {
            rectGradV({x0, yh, x1 - x0, V.H - yh}, far, nearC);
        });
        // ripples: thin lighter streaks, denser toward the horizon
        for (int i = 0; i < 2600; ++i) {
            float a = rng.uniform(seaA0, seaA1), e = eh - std::pow(rng.uniform(), 1.6f) * 13.f;
            float w = (2.f + (eh - e) * 1.8f) * rng.uniform(0.6f, 1.4f);
            Color c = night ? Color{60, 70, 110, 255} : (dusk ? Color{230, 170, 150, 255} : Color{170, 200, 220, 255});
            c.a = (unsigned char)rng.range(20, 60);
            DrawRectangleV({V.x(a), V.y(e)}, {w, 1.f + (eh - e) * 0.08f}, c);
        }
        // glitter: under the sun by day / at dusk, a moon path at night
        const float ga = night ? 12.f : sunAz;
        if (!overcast && ga > seaA0 && ga < seaA1) {
            const int n = night ? 500 : 1200;
            for (int i = 0; i < n; ++i) {
                float e = eh - std::pow(rng.uniform(), 1.3f) * 13.f;
                float spread = 2.f + (eh - e) * 1.1f;
                float a = ga + rng.uniform(-spread, spread) * rng.uniform(0.3f, 1.f);
                Color c = night ? Color{220, 220, 200, 255} : (dusk ? Color{255, 196, 110, 255} : Color{255, 250, 230, 255});
                c.a = (unsigned char)rng.range(70, 220);
                DrawRectangleV({V.x(a), V.y(e)}, {rng.uniform(2.f, 7.f) + (eh - e) * 0.6f, 1.5f}, c);
            }
        }
    }

    // ---- the far shore: hills from -80 to +70, the city along the water, a mosque and the bridge
    {
        const float a0 = -80.f, a1 = 72.f;
        auto ridge = [&](float a) {
            float t = (a - a0) / (a1 - a0);
            float h = 1.1f + 2.6f * std::sin(t * PI) * (0.6f + 0.5f * fbm(t * 6.f, 0.5f, 3, seed + 41));
            h += 0.8f * std::exp(-((a + 30.f) * (a + 30.f)) / 300.f);
            return eh + h;
        };
        Color hill = night ? Color{16, 20, 36, 255} : (dusk ? Color{120, 84, 104, 255} : Color{122, 142, 150, 255});
        if (overcast) hill = mix(hill, Color{130, 136, 142, 255}, 0.5f);
        hill = hazeMix(hill, night ? 0.1f : 0.25f);
        for (float a = a0; a < a1; a += 0.25f) {
            float e = ridge(a);
            DrawRectangleV({V.x(a), V.y(e)}, {V.pxPerDeg() * 0.25f + 1.f, yh - V.y(e) + 1.f}, hill);
        }
        // a second, nearer and darker ridge on the left (a headland)
        Color head = night ? Color{12, 16, 28, 255} : (dusk ? Color{92, 64, 84, 255} : Color{96, 118, 112, 255});
        head = hazeMix(head, night ? 0.05f : 0.12f);
        for (float a = -110.f; a < -52.f; a += 0.25f) {
            float t = (a + 110.f) / 58.f;
            float e = eh + 0.4f + 3.4f * std::pow(1.f - t, 0.8f) * (0.8f + 0.3f * fbm(t * 5.f, 2.f, 3, seed + 43));
            DrawRectangleV({V.x(a), V.y(e)}, {V.pxPerDeg() * 0.25f + 1.f, yh - V.y(e) + 2.f}, head);
        }
        // houses along the shore and up the hills: little light blocks with roofs (lit windows at night)
        for (int i = 0; i < 900; ++i) {
            float a = rng.uniform(a0 + 3.f, a1 - 3.f);
            float top = ridge(a);
            float e = eh + 0.1f + std::pow(rng.uniform(), 1.7f) * (top - eh - 0.2f);
            float w = rng.uniform(2.f, 5.f), h = rng.uniform(1.5f, 3.f);
            float x = V.x(a), y = V.y(e);
            if (night) {
                if (rng.chance(0.45f)) {
                    Color l = rng.chance(0.8f) ? Color{255, 200, 120, 255} : Color{220, 230, 255, 255};
                    l.a = (unsigned char)rng.range(120, 255);
                    DrawRectangleV({x, y}, {1.6f, 1.6f}, l);
                }
            } else {
                Color c = rng.chance(0.5f) ? Color{226, 220, 208, 255} : (rng.chance(0.5f) ? Color{214, 190, 160, 255} : Color{200, 170, 150, 255});
                if (dusk) c = mix(c, Color{255, 170, 120, 255}, 0.45f);
                c = hazeMix(c, 0.45f);
                DrawRectangleV({x, y - h}, {w, h}, c);
                DrawRectangleV({x, y - h - 1.f}, {w, 1.2f}, hazeMix(Color{170, 90, 70, 255}, 0.5f));
            }
        }
        // the mosque on the far shore: dome, half domes, four minarets
        {
            const float ma = -22.f, base = V.y(ridge(ma) - 0.6f), sc = 0.42f;
            const float mx = V.x(ma);
            Color m = night ? Color{40, 44, 66, 255} : (dusk ? Color{110, 76, 90, 255} : Color{176, 182, 186, 255});
            m = hazeMix(m, night ? 0.f : 0.3f);
            DrawRectangleRec({mx - 70 * sc, base - 40 * sc, 140 * sc, 40 * sc}, m);
            DrawCircleSector({mx, base - 40 * sc}, 44 * sc, 180, 360, 24, m);
            DrawCircleSector({mx - 52 * sc, base - 36 * sc}, 24 * sc, 180, 360, 16, m);
            DrawCircleSector({mx + 52 * sc, base - 36 * sc}, 24 * sc, 180, 360, 16, m);
            for (int k = -1; k <= 1; k += 2)
                for (int j = 0; j < 2; ++j) {
                    float x = mx + k * (82 + j * 22) * sc, h = (j == 0 ? 132 : 112) * sc;
                    DrawRectangleRec({x - 3.f * sc, base - h, 6.f * sc, h}, m);
                    tri({x - 4.f * sc, base - h}, {x + 4.f * sc, base - h}, {x, base - h - 20 * sc}, m);
                    if (night) DrawRectangleRec({x - 3.5f * sc, base - h * 0.75f, 7.f * sc, 2.f}, Color{255, 220, 150, 200});
                }
            if (night) DrawCircleGradient({mx, base - 50 * sc}, 30.f * sc, Color{255, 210, 140, 70}, Color{255, 210, 140, 0});
        }
        // the bridge far to the right: two towers, the deck, the cables (a necklace of lights at night)
        {
            const float ba0 = 28.f, ba1 = 58.f;
            const float x0 = V.x(ba0), x1 = V.x(ba1), deck = V.y(eh + 1.5f), top = V.y(eh + 5.2f);
            Color b = night ? Color{30, 34, 50, 255} : (dusk ? Color{110, 80, 96, 255} : Color{150, 162, 170, 255});
            b = hazeMix(b, night ? 0.f : 0.35f);
            const float t0 = x0 + (x1 - x0) * 0.2f, t1 = x0 + (x1 - x0) * 0.8f;
            DrawRectangleRec({x0, deck, x1 - x0, 3.f}, b);
            for (float tx : {t0, t1}) DrawRectangleRec({tx - 2.f, top, 4.f, deck - top + 3.f}, b);
            auto cable = [&](float xa, float xb, float ya, float yb, float sag) {
                Vector2 prev{xa, ya};
                for (int i = 1; i <= 24; ++i) {
                    float t = i / 24.f;
                    Vector2 p{xa + (xb - xa) * t, ya + (yb - ya) * t + sag * 4.f * t * (1.f - t)};
                    DrawLineEx(prev, p, 1.6f, b);
                    if (night && i % 2 == 0) DrawCircleV(p, 1.4f, Color{255, 230, 170, 230});
                    prev = p;
                }
            };
            cable(t0, t1, top, top, deck - top - 4.f);
            cable(x0, t0, deck, top, -4.f);
            cable(t1, x1, top, deck, -4.f);
            if (night)
                for (float x = x0; x < x1; x += 5.f) DrawCircleV({x, deck + 1.f}, 1.2f, Color{255, 200, 120, 200});
        }
    }

    // ---- ships: a ferry (vapur), a tanker far out, a few fishing boats
    {
        auto ferry = [&](float a, float e, float s) {
            float x = V.x(a), y = V.y(e);
            Color hull = night ? Color{20, 22, 34, 255} : Color{40, 40, 46, 255};
            Color body = night ? Color{50, 52, 66, 255} : (dusk ? Color{240, 200, 170, 255} : Color{236, 236, 230, 255});
            quad2({x - 18 * s, y - 4 * s}, {x + 18 * s, y - 4 * s}, {x + 15 * s, y}, {x - 16 * s, y}, hull);
            DrawRectangleRec({x - 13 * s, y - 8 * s, 26 * s, 4 * s}, body);
            DrawRectangleRec({x - 9 * s, y - 11 * s, 16 * s, 3 * s}, body);
            DrawRectangleRec({x - 1 * s, y - 16 * s, 3 * s, 5 * s}, Color{30, 30, 30, 255});
            DrawRectangleRec({x - 1 * s, y - 15 * s, 3 * s, 1.2f * s}, Color{240, 240, 240, 255});
            if (night)
                for (int k = 0; k < 8; ++k) DrawRectangleRec({x - 12 * s + k * 3.2f * s, y - 7 * s, 1.5f * s, 1.5f * s}, Color{255, 210, 130, 255});
            // wake
            for (int k = 0; k < 6; ++k)
                DrawRectangleV({x + (18 + k * 7) * s, y - 0.5f * s + k * 0.3f}, {6 * s, 0.8f}, Color{230, 240, 245, (unsigned char)(night ? 30 : 110 - k * 15)});
        };
        ferry(-8.f, eh - 1.2f, 1.6f);
        ferry(41.f, eh - 0.35f, 0.8f);
        // tanker on the horizon
        {
            float x = V.x(-45.f), y = V.y(eh + 0.05f);
            Color c = hazeMix(night ? Color{20, 22, 30, 255} : Color{90, 70, 64, 255}, 0.4f);
            DrawRectangleRec({x, y - 3.f, 42.f, 3.f}, c);
            DrawRectangleRec({x + 34.f, y - 8.f, 6.f, 5.f}, c);
            if (night) DrawCircleV({x + 2.f, y - 4.f}, 1.2f, Color{255, 80, 60, 255}), DrawCircleV({x + 38.f, y - 9.f}, 1.2f, Color{255, 255, 220, 255});
        }
        for (int i = 0; i < 6; ++i) {  // fishing boats
            float a = rng.uniform(-60.f, 60.f), e = eh - rng.uniform(1.f, 6.f);
            float x = V.x(a), y = V.y(e), s = 0.6f + (eh - e) * 0.25f;
            Color c = night ? Color{14, 16, 26, 255} : (rng.chance(0.5f) ? Color{230, 226, 216, 255} : Color{70, 110, 160, 255});
            quad2({x - 6 * s, y - 2 * s}, {x + 7 * s, y - 2.5f * s}, {x + 5 * s, y}, {x - 5 * s, y}, c);
            DrawRectangleRec({x - 1 * s, y - 6 * s, 3 * s, 4 * s}, night ? c : Color{240, 240, 236, 255});
            if (night) DrawCircleV({x, y - 6.5f * s}, 1.3f, Color{255, 230, 170, 255});
        }
    }

    // ---- the neighbourhood: from the right-front round to the left (azimuths 95 .. 265): roofs, walls, cypresses
    {
        const float a0 = 92.f, a1 = 268.f;
        // a hill behind the houses
        Color hill = night ? Color{14, 18, 30, 255} : (dusk ? Color{128, 96, 96, 255} : Color{120, 140, 116, 255});
        hill = hazeMix(hill, night ? 0.05f : 0.25f);
        for (float a = a0; a < a1; a += 0.25f) {
            float e = 14.f + 6.f * fbm(a / 30.f, 1.f, 3, seed + 51);
            DrawRectangleV({V.x(a), V.y(e)}, {V.pxPerDeg() * 0.25f + 1.f, V.H - V.y(e)}, hill);
        }
        // houses: three rows, the nearest biggest
        for (int row = 0; row < 3; ++row) {
            const float base = 10.f + row * 2.5f, scale = 1.f - row * 0.25f;
            for (float a = a0 + rng.uniform(0.f, 3.f); a < a1;) {
                float w = rng.uniform(5.f, 11.f) * scale, h = rng.uniform(4.f, 9.f) * scale;
                float e0 = base - rng.uniform(4.f, 8.f) + row * 1.f;
                const Color walls[5] = {{232, 224, 210, 255}, {222, 196, 150, 255}, {214, 170, 150, 255}, {196, 210, 196, 255}, {236, 214, 180, 255}};
                Color c = walls[rng.range(0, 4)];
                if (dusk) c = mix(c, Color{255, 170, 120, 255}, 0.35f);
                if (night) c = Color{26, 28, 40, 255};
                c = hazeMix(c, night ? 0.f : 0.12f + row * 0.12f);
                float x = V.x(a), wpx = w * V.pxPerDeg();
                float yb = V.y(e0), yt = V.y(e0 + h);
                spans(V, a, a + w, [&](float xa, float xb, float off) {
                    (void)off;
                    DrawRectangleRec({xa, yt, xb - xa, yb - yt}, c);
                });
                // roof
                Color rf = night ? Color{20, 18, 26, 255} : hazeMix(Color{168, 84, 56, 255}, 0.1f + row * 0.12f);
                float rh = h * 0.35f * V.pxPerDeg();
                if (x + wpx < V.W) tri({x - 3.f, yt}, {x + wpx + 3.f, yt}, {x + wpx * 0.5f, yt - rh}, rf);
                // windows
                int nw = std::max(1, (int)(w / 2.2f));
                for (int k = 0; k < nw; ++k)
                    for (int fl = 0; fl < (h > 6.f ? 2 : 1); ++fl) {
                        float wx = x + (k + 0.5f) * wpx / nw - 4.f * scale, wy = yt + (yb - yt) * (0.2f + fl * 0.42f);
                        if (wx + 8 > V.W) continue;
                        Color wc = night ? (rng.chance(0.55f) ? Color{255, 196, 110, 255} : Color{30, 30, 44, 255}) : hazeMix(Color{60, 66, 76, 255}, 0.2f + row * 0.1f);
                        DrawRectangleRec({wx, wy, 8.f * scale, 12.f * scale}, wc);
                    }
                a += w + rng.uniform(0.2f, 2.f);
            }
        }
        // cypresses and a pine or two in front of the houses
        for (int i = 0; i < 26; ++i) {
            float a = rng.uniform(a0 + 4.f, a1 - 4.f), e0 = rng.uniform(4.f, 9.f), h = rng.uniform(7.f, 14.f);
            Color c = night ? Color{8, 12, 14, 255} : hazeMix(Color{40, 64, 44, 255}, 0.1f);
            float x = V.x(a), w = rng.uniform(1.f, 1.6f) * V.pxPerDeg();
            for (int k = 0; k < 10; ++k) {
                float t = k / 10.f;
                ellipse({x, V.y(e0 + h * (0.15f + t * 0.8f))}, w * (1.f - t * 0.75f) * 0.5f + 1.f, h * V.pxPerDeg() * 0.09f, c, 14);
            }
        }
        // a minaret above the roofs
        {
            float x = V.x(196.f), yb = V.y(9.f), yt = V.y(27.f);
            Color c = night ? Color{30, 32, 46, 255} : hazeMix(Color{226, 222, 214, 255}, 0.15f);
            DrawRectangleRec({x - 5.f, yt, 10.f, yb - yt}, c);
            DrawRectangleRec({x - 8.f, yt + (yb - yt) * 0.25f, 16.f, 5.f}, c);
            tri({x - 7.f, yt}, {x + 7.f, yt}, {x, yt - 34.f}, night ? c : Color{110, 120, 128, 255});
            if (night) DrawRectangleRec({x - 8.f, yt + (yb - yt) * 0.25f, 16.f, 2.f}, Color{255, 230, 170, 255});
        }
    }
    (void)season;
    endRoomCanvas(rt);
}

// ============================================================================ signs
void drawGardenSigns(RenderTexture2D& rt, uint32_t seed) {
    using ui::FontId;
    okey::Rng rng(seed + 501u);
    beginRoomCanvas(rt);
    ClearBackground(Color{0, 0, 0, 0});
    // the shop sign over the door: a dark green enamel board, cream letters, a thin gold rule
    {
        const Rectangle r = gsign::SIGN;
        DrawRectangleRec(r, Color{30, 70, 52, 255});
        rectGradV(r, Color{60, 100, 80, 120}, Color{10, 30, 20, 120});
        frameRect({r.x + 10, r.y + 10, r.width - 20, r.height - 20}, 5.f, Color{204, 168, 88, 255});
        ui::drawTextCentered(FontId::Sign, "SAKLIBAHÇE", {r.x + r.width * 0.5f, r.y + 82}, 104.f, Color{240, 230, 204, 255}, 6.f);
        ui::drawTextCentered(FontId::Sign, "KIRAATHANESİ  ·  1952", {r.x + r.width * 0.5f, r.y + 156}, 40.f, Color{214, 182, 110, 255}, 4.f);
        for (int i = 0; i < 40; ++i)  // chips in the enamel
            ellipse({rng.uniform(r.x + 14, r.x + r.width - 14), rng.uniform(r.y + 14, r.y + r.height - 14)}, rng.uniform(1.5f, 4.f),
                    rng.uniform(1.f, 3.f), Color{40, 36, 30, 200}, 8);
    }
    // the chalk board by the gate
    {
        const Rectangle r = gsign::MENU;
        const Color board{34, 46, 40, 255}, chalk{236, 232, 216, 255};
        DrawRectangleRec(r, board);
        chalkSmudges(r, chalk, rng, 6);
        chalkText(FontId::Chalk, "Bahçemiz", {r.x + 52, r.y + 26}, 58, chalk, rng);
        chalkText(FontId::Chalk, "açıktır", {r.x + 84, r.y + 92}, 58, chalk, rng);
        chalkText(FontId::Chalk, "Çay · Kahve", {r.x + 46, r.y + 174}, 44, Color{246, 214, 140, 255}, rng);
        chalkText(FontId::Chalk, "Okey · Tavla", {r.x + 40, r.y + 226}, 44, Color{200, 226, 240, 255}, rng);
        chalkSpeckle(r, board, rng, 1800);
    }
    // a lit window seen from the garden: warm room, lace, a hanging lamp, a shadow of a man at a table
    {
        const Rectangle r = gsign::WINDOW;
        rectGradV(r, Color{196, 140, 80, 255}, Color{120, 76, 44, 255});
        DrawCircleGradient({r.x + r.width * 0.55f, r.y + 70}, 120, Color{255, 220, 150, 220}, Color{255, 200, 120, 0});
        DrawRectangleRec({r.x + r.width * 0.55f - 1, r.y, 2, 46}, Color{40, 30, 20, 255});
        ellipse({r.x + r.width * 0.55f, r.y + 58}, 26, 12, Color{60, 90, 70, 255});
        // a silhouette at a table
        ellipse({r.x + 90, r.y + 150}, 22, 26, Color{70, 44, 30, 255});
        ellipse({r.x + 92, r.y + 205}, 40, 46, Color{70, 44, 30, 255});
        DrawRectangleRec({r.x + 30, r.y + 236, 240, 14}, Color{80, 50, 32, 255});
        // lace café curtain over the lower half
        for (int y = (int)(r.y + r.height * 0.55f); y < r.y + r.height; y += 6)
            for (int x = (int)r.x; x < r.x + r.width; x += 6)
                if (((x / 6) + (y / 6)) % 2 == 0) DrawRectangle(x, y, 4, 4, Color{240, 236, 226, 150});
        DrawRectangleRec({r.x, r.y + r.height * 0.55f - 4, r.width, 4}, Color{200, 160, 80, 255});
    }
    // the street plate
    {
        const Rectangle r = gsign::PLATE;
        DrawRectangleRec(r, Color{30, 60, 130, 255});
        frameRect({r.x + 6, r.y + 6, r.width - 12, r.height - 12}, 3.f, Color{240, 240, 240, 255});
        ui::drawTextCentered(FontId::Sign, "Çınaraltı Sk.", {r.x + r.width * 0.5f, r.y + 46}, 40.f, WHITE, 1.f);
        ui::drawTextCentered(FontId::Sign, "No: 7", {r.x + r.width * 0.5f, r.y + 88}, 32.f, WHITE, 1.f);
    }
    endRoomCanvas(rt);
}

}  // namespace rm
}  // namespace r3d
