// Room module: the weather and the street outside the windows (room owner).
//   * Some nights it rains (decided by the seed, the intensity drifts over minutes): drops bead on the outside of
//     the window glass, grow, merge and run down leaving clear trails (an animated canvas, one slot per window);
//     rain streaks fall past the street lamp (upright additive billboards outside the windows and the door).
//   * Passers-by walk along the pavement (left street and the back street): painted silhouettes with a four-frame
//     walk cycle on upright billboards; on rainy nights they carry umbrellas or hurry under a newspaper. From the
//     seat you mostly see the umbrellas and caps gliding past above the café curtains.
#include "r3d/RoomInternal.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace rm;

namespace {

// ---------------------------------------------------------------------------- raindrops canvas layout
constexpr float DROP_PX_PER_M = 200.f;
constexpr int DROPS_W = 688, DROPS_H = 304;

// ---------------------------------------------------------------------------- passer-by atlas layout
constexpr int WALK_CELL_W = 144, WALK_CELL_H = 336, WALK_COLS = 7;
constexpr int WALK_TYPES = 7, WALK_FRAMES = 4;
constexpr int WALK_W = WALK_CELL_W * WALK_COLS, WALK_H = WALK_CELL_H * 4;
constexpr float WALK_PX_PER_M = 140.f;   // cell = 1.03 m x 2.4 m (room for an open umbrella), feet at the bottom
constexpr float WALK_BOARD_W = WALK_CELL_W / WALK_PX_PER_M, WALK_BOARD_H = WALK_CELL_H / WALK_PX_PER_M;

enum class Head { Bare, Cap, Scarf };
enum class Arms { Swing, Umbrella, Pockets, Paper, Cane };
struct FigSpec {
    Head head;
    Arms arms;
    Color brolly;    // umbrella canopy
    float hem;       // coat hem height (m): long coat ~0.5, jacket ~0.85
    float lean;      // forward lean (degrees): hunched against the rain, or old age
    float stride;    // step angle scale (running > 1)
    bool smoking;
};
// 0 umbrella man, 1 cap & coat (smoking), 2 woman under a burgundy umbrella, 3 runner under a newspaper,
// 4 old man with a cane, 5 young man in a jacket, 6 man in a cap under a navy umbrella
const FigSpec kFigs[WALK_TYPES] = {
    {Head::Bare, Arms::Umbrella, {24, 24, 30, 255}, 0.52f, 3.f, 1.f, false},
    {Head::Cap, Arms::Pockets, {0, 0, 0, 0}, 0.5f, 7.f, 0.9f, true},
    {Head::Scarf, Arms::Umbrella, {92, 26, 34, 255}, 0.34f, 2.f, 0.85f, false},
    {Head::Bare, Arms::Paper, {0, 0, 0, 0}, 0.86f, 12.f, 1.4f, false},
    {Head::Cap, Arms::Cane, {0, 0, 0, 0}, 0.48f, 11.f, 0.7f, false},
    {Head::Bare, Arms::Swing, {0, 0, 0, 0}, 0.84f, 2.f, 1.f, false},
    {Head::Cap, Arms::Umbrella, {30, 38, 70, 255}, 0.56f, 4.f, 1.f, false},
};
constexpr int kRainyFigs[] = {0, 2, 6, 0, 3, 1, 2};
constexpr int kDryFigs[] = {1, 4, 5, 1, 5, 4, 1};

Rectangle walkCell(int type, int frame) {
    int i = type * WALK_FRAMES + frame;
    return {(float)(i % WALK_COLS) * WALK_CELL_W, (float)(i / WALK_COLS) * WALK_CELL_H, (float)WALK_CELL_W, (float)WALK_CELL_H};
}

// Painter for one figure cell: metres, feet centre at the origin, facing +x (walking to the right).
struct Pen {
    float cx, by;
    Vector2 P(float x, float y) const { return {cx + x * WALK_PX_PER_M, by - y * WALK_PX_PER_M}; }
    float px(float m) const { return m * WALK_PX_PER_M; }
    void limb(Vector2 a, Vector2 b, float wa, float wb, Color c) const {
        // tapered segment: quad + round ends
        Vector2 pa = P(a.x, a.y), pb = P(b.x, b.y);
        Vector2 d{pb.x - pa.x, pb.y - pa.y};
        float l = std::sqrt(d.x * d.x + d.y * d.y);
        if (l < 1e-3f) return;
        Vector2 n{-d.y / l, d.x / l};
        float ra = px(wa) * 0.5f, rb = px(wb) * 0.5f;
        quad2({pa.x + n.x * ra, pa.y + n.y * ra}, {pb.x + n.x * rb, pb.y + n.y * rb}, {pb.x - n.x * rb, pb.y - n.y * rb},
              {pa.x - n.x * ra, pa.y - n.y * ra}, c);
        DrawCircleV(pa, ra, c);
        DrawCircleV(pb, rb, c);
    }
    void blob(float x, float y, float rx, float ry, Color c, int seg = 20) const { ellipse(P(x, y), px(rx), px(ry), c, seg); }
};

Vector2 rot2(Vector2 v, float deg) {
    float a = deg * DEG2RAD, c = std::cos(a), s = std::sin(a);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
Vector2 add2(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
// End of a bone of length l hanging from `from`, rotated `deg` from straight down (positive = forward, +x).
Vector2 bone(Vector2 from, float l, float deg) { return add2(from, rot2({0.f, -l}, deg)); }

// `day`: painted in colour (coats, faces) instead of the night's silhouette against the lit street.
void drawFigure(const FigSpec& f, int type, int frame, Pen pen, bool day) {
    const Color sil{17, 19, 27, 255}, silFar{12, 13, 19, 255}, rimC{58, 64, 84, 255};
    static const Color kCoat[WALK_TYPES] = {{74, 70, 66, 255}, {98, 76, 54, 255}, {124, 44, 52, 255}, {84, 84, 90, 255},
                                            {72, 62, 52, 255}, {58, 74, 100, 255}, {64, 68, 60, 255}};
    const Color coat = day ? kCoat[type] : sil, coatFar = day ? scaleRgb(kCoat[type], 0.75f) : silFar;
    const Color legNear = day ? Color{58, 58, 64, 255} : sil, legFar = day ? Color{46, 46, 52, 255} : silFar;
    const Color skin = day ? Color{186, 140, 108, 255} : sil;
    const Color hat = day ? (f.head == Head::Scarf ? Color{150, 60, 70, 255} : Color{52, 48, 46, 255}) : sil;
    const Color stick = day ? Color{40, 36, 34, 255} : sil;
    const bool run = f.stride > 1.2f;
    // walk cycle: thigh angle (forward +) and knee bend for the near (A) and far (B) leg; frames 2/3 swap them
    struct LegPose {
        float thigh, knee;
    };
    const LegPose c0A{24.f, 6.f}, c0B{-20.f, 14.f}, c1A{-4.f, 4.f}, c1B{12.f, 52.f};
    LegPose A, B;
    switch (frame) {
    case 0: A = c0A, B = c0B; break;
    case 1: A = c1A, B = c1B; break;
    case 2: A = c0B, B = c0A; break;
    default: A = c1B, B = c1A; break;
    }
    const float st = f.stride;
    A.thigh *= st, B.thigh *= st;
    if (run) A.knee *= 1.6f, B.knee *= 1.6f;
    const float bob = (frame % 2) ? 0.018f : 0.f;
    const float legL = 0.44f;
    const float hipY = 0.9f - (run ? 0.05f : 0.f) + bob;
    const Vector2 hip{0.f, hipY};
    auto leg = [&](LegPose p, Color c, float dx) {
        Vector2 h{hip.x + dx, hip.y};
        Vector2 knee = bone(h, legL, p.thigh);
        Vector2 ankle = bone(knee, legL, p.thigh - p.knee);
        float ay = std::max(ankle.y, 0.035f);
        if (ankle.y < 0.035f) {  // planted: slide the ankle up to the ground line
            knee.y += 0.035f - ankle.y;
            ankle.y = ay;
        }
        pen.limb(h, knee, 0.15f, 0.11f, c);
        pen.limb(knee, ankle, 0.11f, 0.08f, c);
        pen.limb({ankle.x - 0.03f, ankle.y - 0.015f}, {ankle.x + 0.1f, ankle.y - 0.02f}, 0.06f, 0.05f, c);  // shoe
    };
    leg(B, legFar, -0.02f);
    // the body leans about the hips
    const float lean = f.lean + (run ? 6.f : 0.f);
    auto L = [&](Vector2 p) { return add2(hip, rot2({p.x, p.y - hipY}, -lean)); };
    const float shY = 1.45f + bob, neckY = 1.52f + bob;
    const Vector2 shoulder = L({0.0f, shY - 0.03f});
    // far arm (behind the body)
    float swing = (frame == 0) ? -18.f : (frame == 2 ? 18.f : 0.f);
    if (run) swing *= 1.8f;
    if (f.arms == Arms::Swing || f.arms == Arms::Cane) {
        Vector2 e = bone(shoulder, 0.3f, -swing);
        pen.limb(shoulder, e, 0.1f, 0.085f, coatFar);
        pen.limb(e, bone(e, 0.27f, -swing + 22.f), 0.085f, 0.07f, coatFar);
    }
    leg(A, legNear, 0.02f);
    // coat: shoulders to hem, flaring a little, split at the back when walking
    {
        const float hem = f.hem + bob * 0.5f;
        std::vector<Vector2> poly{L({0.13f, shY - 0.02f}), L({0.16f, 1.15f + bob}), L({0.15f + 0.04f * (hem < 0.7f), hem}),
                                  L({-0.17f - 0.05f * (hem < 0.7f), hem - 0.02f}), L({-0.17f, 1.1f + bob}), L({-0.12f, shY - 0.03f}),
                                  L({-0.02f, shY + 0.02f})};
        Vector2 c = L({0.f, (shY + hem) * 0.5f});
        for (size_t i = 0; i < poly.size(); ++i) tri(pen.P(c.x, c.y), pen.P(poly[i].x, poly[i].y), pen.P(poly[(i + 1) % poly.size()].x, poly[(i + 1) % poly.size()].y), coat);
        // collar turned up
        Vector2 c0 = L({0.06f, shY + 0.01f}), c1 = L({-0.05f, shY + 0.05f});
        pen.limb(c0, c1, 0.06f, 0.05f, coat);
    }
    // head
    const Vector2 head = L({0.035f, neckY + 0.1f});
    pen.limb(L({0.0f, neckY - 0.04f}), L({0.02f, neckY + 0.04f}), 0.09f, 0.08f, skin);
    pen.blob(head.x, head.y, 0.093f, 0.112f, skin);
    pen.blob(head.x + 0.085f, head.y - 0.005f, 0.026f, 0.03f, skin, 10);  // nose
    if (day && f.head == Head::Bare) pen.blob(head.x - 0.025f, head.y + 0.05f, 0.08f, 0.07f, Color{60, 50, 44, 255});  // hair
    switch (f.head) {
    case Head::Cap:
        pen.blob(head.x - 0.005f, head.y + 0.075f, 0.108f, 0.05f, hat);
        pen.limb({head.x + 0.05f, head.y + 0.06f}, {head.x + 0.15f, head.y + 0.045f}, 0.03f, 0.018f, hat);
        break;
    case Head::Scarf:
        pen.blob(head.x - 0.01f, head.y + 0.015f, 0.108f, 0.128f, hat);
        if (day) pen.blob(head.x + 0.02f, head.y - 0.005f, 0.07f, 0.085f, skin);  // the face inside the headscarf
        pen.limb({head.x - 0.07f, head.y - 0.05f}, L({-0.12f, shY - 0.1f}), 0.07f, 0.05f, hat);  // scarf tail
        break;
    default: break;
    }
    // near arm and whatever it carries
    const float rim = 0.9f;
    if (f.arms == Arms::Umbrella) {
        Vector2 e = bone(shoulder, 0.27f, 28.f);
        Vector2 hand = add2(e, rot2({0.f, 0.24f}, -40.f));
        pen.limb(shoulder, e, 0.1f, 0.085f, coat);
        pen.limb(e, hand, 0.085f, 0.07f, coat);
        const Vector2 top = L({0.09f, 2.1f + bob * 0.6f});
        pen.limb(hand, top, 0.018f, 0.018f, stick);  // the stick
        pen.limb({hand.x - 0.01f, hand.y}, {hand.x - 0.03f, hand.y - 0.06f}, 0.022f, 0.02f, stick);  // crook handle
        // canopy: a dome with scalloped edges between the ribs
        const float cy = top.y - 0.17f, rx = 0.46f, ry = 0.19f;
        std::vector<Vector2> pts;
        for (int i = 0; i <= 20; ++i) {
            float a = PI * i / 20.f;
            pts.push_back({top.x - std::cos(a) * rx, cy + std::sin(a) * ry});
        }
        const int ribs = 6;
        for (int i = ribs; i >= 0; --i) {
            float x = top.x - rx + 2.f * rx * i / ribs;
            pts.push_back({x, cy - 0.012f});
            if (i > 0) pts.push_back({x - rx / ribs, cy + 0.028f});
        }
        Vector2 cc{top.x, cy + ry * 0.4f};
        for (size_t i = 0; i + 1 < pts.size(); ++i) tri(pen.P(cc.x, cc.y), pen.P(pts[i].x, pts[i].y), pen.P(pts[i + 1].x, pts[i + 1].y), f.brolly);
        // the street lamp catches the top of the wet canopy
        Color hl = mix(f.brolly, Color{150, 160, 190, 255}, 0.45f);
        for (int i = 3; i < 18; ++i) {
            float a0 = PI * i / 20.f, a1 = PI * (i + 1) / 20.f;
            Vector2 p0{top.x - std::cos(a0) * rx * rim, cy + std::sin(a0) * ry * rim}, p1{top.x - std::cos(a1) * rx * rim, cy + std::sin(a1) * ry * rim};
            DrawLineEx(pen.P(p0.x, p0.y), pen.P(p1.x, p1.y), 2.2f, alpha(hl, 0.5f + 0.4f * std::sin(a0)));
        }
        for (int i = 1; i < ribs; ++i) {  // ribs
            float x = top.x - rx + 2.f * rx * i / ribs;
            float a = std::acos(std::clamp((top.x - x) / rx, -1.f, 1.f));
            DrawLineEx(pen.P(top.x, top.y - 0.01f), pen.P(x, cy + std::sin(a) * ry * 0.2f), 1.f, alpha(hl, 0.25f));
        }
        pen.limb(top, {top.x, top.y + 0.05f}, 0.012f, 0.01f, f.brolly);  // ferrule
    } else if (f.arms == Arms::Paper) {
        // both hands hold a newspaper over the head
        Vector2 e = bone(shoulder, 0.26f, 150.f);
        Vector2 hand = bone(e, 0.24f, 175.f);
        pen.limb(shoulder, e, 0.1f, 0.085f, coat);
        pen.limb(e, hand, 0.085f, 0.07f, coat);
        Vector2 a = {head.x - 0.3f, head.y + 0.2f}, b = {head.x + 0.26f, head.y + 0.24f};
        Color paper{132, 134, 136, 255};
        quad2(pen.P(a.x, a.y), pen.P(b.x, b.y), pen.P(b.x, b.y - 0.02f), pen.P(a.x, a.y - 0.06f), paper);
        tri(pen.P(a.x, a.y - 0.06f), pen.P(a.x + 0.06f, a.y - 0.01f), pen.P(a.x - 0.03f, a.y - 0.14f), scaleRgb(paper, 0.8f));
        DrawLineEx(pen.P(a.x + 0.05f, a.y + 0.004f), pen.P(b.x - 0.04f, b.y - 0.002f), 1.2f, Color{90, 92, 96, 255});
    } else if (f.arms == Arms::Pockets) {
        Vector2 e = bone(shoulder, 0.28f, -6.f);
        pen.limb(shoulder, e, 0.1f, 0.085f, coat);
        pen.limb(e, add2(e, {0.1f, 0.02f}), 0.085f, 0.075f, coat);
    } else if (f.arms == Arms::Cane) {
        Vector2 e = bone(shoulder, 0.28f, 16.f);
        Vector2 hand = bone(e, 0.25f, 38.f);
        pen.limb(shoulder, e, 0.1f, 0.085f, coat);
        pen.limb(e, hand, 0.085f, 0.07f, coat);
        float tipX = hand.x + (frame % 2 ? 0.02f : 0.12f);
        pen.limb(hand, {tipX, 0.02f}, 0.022f, 0.018f, Color{30, 26, 24, 255});
        pen.limb({hand.x - 0.05f, hand.y + 0.02f}, {hand.x + 0.02f, hand.y + 0.03f}, 0.025f, 0.02f, Color{30, 26, 24, 255});
    } else {
        Vector2 e = bone(shoulder, 0.3f, swing);
        pen.limb(shoulder, e, 0.1f, 0.085f, coat);
        pen.limb(e, bone(e, 0.27f, swing + 22.f), 0.085f, 0.07f, coat);
    }
    // a faint rim of street light on the head and shoulders (the umbrella keeps the heads under it dark)
    if (!day && f.arms != Arms::Umbrella && f.arms != Arms::Paper) {
        for (int i = 0; i < 7; ++i) {
            float a0 = PI * (0.2f + 0.08f * i), a1 = PI * (0.2f + 0.08f * (i + 1));
            float ry = f.head == Head::Cap ? 0.05f : 0.112f, cyh = f.head == Head::Cap ? head.y + 0.075f : head.y;
            float rxh = f.head == Head::Cap ? 0.108f : 0.093f;
            DrawLineEx(pen.P(head.x + std::cos(a0) * rxh, cyh + std::sin(a0) * ry), pen.P(head.x + std::cos(a1) * rxh, cyh + std::sin(a1) * ry),
                       1.6f, alpha(rimC, 0.8f));
        }
    }
    if (!day)
        DrawLineEx(pen.P(L({-0.1f, shY}).x, L({-0.1f, shY}).y), pen.P(L({0.1f, shY - 0.01f}).x, L({0.1f, shY - 0.01f}).y), 1.4f, alpha(rimC, 0.6f));
    if (f.smoking) {  // a cigarette glowing at the lips
        Vector2 m = {head.x + 0.1f, head.y - 0.05f};
        ellipseGrad(pen.P(m.x + 0.03f, m.y), 7.f, 7.f, Color{255, 120, 40, 110}, Color{255, 90, 30, 0}, 16);
        DrawLineEx(pen.P(m.x, m.y), pen.P(m.x + 0.04f, m.y - 0.005f), 1.6f, Color{190, 186, 176, 255});
        DrawCircleV(pen.P(m.x + 0.045f, m.y - 0.005f), 1.6f, Color{255, 150, 60, 255});
    }
}

void drawWalkerAtlas(RenderTexture2D& rt, bool day) {
    beginRoomCanvas(rt);
    for (int t = 0; t < WALK_TYPES; ++t)
        for (int f = 0; f < WALK_FRAMES; ++f) {
            Rectangle c = walkCell(t, f);
            drawFigure(kFigs[t], t, f, Pen{c.x + c.width * 0.43f, c.y + c.height - 2.f}, day);
        }
    endRoomCanvas(rt);
}

// Rain streaks: a few thin motion-blurred lines on a transparent strip (u across, v down; tiles vertically).
Texture2D genStreakTexture(uint32_t seed) {
    const int W = 64, H = 256;
    Image img = GenImageColor(W, H, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    okey::Rng rng(seed);
    for (int k = 0; k < 6; ++k) {
        float x = 5.f + k * 10.f + rng.uniform(-3.f, 3.f);
        float y0 = rng.uniform(0.f, H * 0.5f), len = rng.uniform(70.f, 190.f), a = rng.uniform(0.45f, 1.f);
        float slant = rng.uniform(-0.03f, 0.03f);
        for (int y = 0; y < H; ++y) {
            float t = (y - y0) / len;
            if (t < 0.f || t > 1.f) continue;
            float along = std::sin(t * PI) * (0.6f + 0.4f * t);  // brighter toward the leading (lower) end
            float cx = x + slant * (y - y0);
            for (int dx = -2; dx <= 2; ++dx) {
                int xx = (int)std::floor(cx) + dx;
                if (xx < 0 || xx >= W) continue;
                float d = std::fabs(xx + 0.5f - cx);
                float w = std::max(0.f, 1.f - d / 1.4f);
                Color& p = px[y * W + xx];
                p.a = (unsigned char)std::clamp(p.a + w * along * a * 255.f, 0.f, 255.f);
            }
        }
    }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}


// A raindrop on night glass, seen from inside (32 x 32, straight alpha): a darker rim where it bends the dark sky,
// a nearly clear body, a bright crescent low in the drop where it refracts the lit street, a small glint up top.
Texture2D genDropSprite() {
    const int N = 32;
    Image img = GenImageColor(N, N, Color{0, 0, 0, 0});
    Color* px = (Color*)img.data;
    const Vector3 shade{16, 22, 34}, body{196, 210, 230}, glint{250, 252, 255};
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N * 2.f - 1.f, v = (y + 0.5f) / N * 2.f - 1.f;
            float r = std::sqrt(u * u + v * v);
            float cover = std::clamp((1.f - r) * N * 0.5f, 0.f, 1.f);  // anti-aliased edge
            if (cover <= 0.f) continue;
            float rim = smooth01(0.62f, 0.95f, r);
            float cres = smooth01(0.1f, 0.45f, v) * (1.f - smooth01(0.55f, 0.8f, r));
            float gl = 1.f - smooth01(0.0f, 0.22f, std::sqrt((u + 0.35f) * (u + 0.35f) + (v + 0.4f) * (v + 0.4f)));
            // accumulate "over": shade rim, then the clear body, the crescent and the glint
            float a = 0.26f * rim;
            Vector3 c = shade;
            auto over = [&](Vector3 col, float al) {
                float na = al + a * (1.f - al);
                if (na > 1e-4f) c = Vector3Scale(Vector3Add(Vector3Scale(col, al), Vector3Scale(c, a * (1.f - al))), 1.f / na);
                a = na;
            };
            over(body, 0.1f * (1.f - rim));
            over(body, 0.42f * cres);
            over(glint, 0.65f * gl);
            px[y * N + x] = Color{(unsigned char)c.x, (unsigned char)c.y, (unsigned char)c.z, (unsigned char)std::clamp(a * cover * 255.f, 0.f, 255.f)};
        }
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return t;
}

}  // namespace

// ============================================================================ init / free
void Room::Impl::initWeather() {
    okey::Rng wr(seed * 7919u + 0x5eedu);
    // whether it rains depends on the season and the hour too (Room::Impl::evalLook): the seed decides how wet a
    // day this is and how hard it rains
    rainSeedU = wr.uniform(0.f, 1.f);
    rainSeedI = wr.uniform(0.5f, 1.f);
    evalLook(true);
    rainBase = rainTarget;
    rain = rainBase;
    walkerT = wr.uniform(2.f, 9.f);

    // passers-by (every night)
    cvWalkers = makeCanvas(WALK_W, WALK_H);
    walkersDay = phase <= 1;
    drawWalkerAtlas(cvWalkers, walkersDay);

    // the rain's resources exist even on a dry night: the weather turns with the hour and the season
    texStreak = genStreakTexture(seed + 1234);
    texDropSprite = genDropSprite();
    cvDrops[0] = makeCanvas(DROPS_W, DROPS_H);
    cvDrops[1] = makeCanvas(DROPS_W, DROPS_H);
    // the drops refract the lit street behind them: mostly self-lit, glossy
    mDrops = R->makeMat(WHITE, cvDrops[0].texture, 0.9f, 120.f, 0.6f, 0.f);

    // one slot per window pane, laid out left to right on the canvas
    float x = 4.f;
    for (const Opening& o : openings) {
        if (o.kind != 0) continue;
        const float W = o.a1 - o.a0, H = o.y1 - o.y0;
        DropSlot s;
        s.rect = {x, 2.f, std::round(W * DROP_PX_PER_M), std::round(H * DROP_PX_PER_M)};
        x += s.rect.width + 8.f;
        // start with the glass already beaded
        for (int i = 0; i < (int)(120 * rainBase); ++i)
            s.beads.push_back({wr.uniform(0.f, s.rect.width), wr.uniform(0.f, s.rect.height), wr.uniform(0.7f, 2.3f)});
        for (int i = 0; i < 4; ++i) {
            Trail t;
            float tx = wr.uniform(4.f, s.rect.width - 4.f), ty = wr.uniform(0.f, s.rect.height * 0.5f);
            for (float y = ty; y < s.rect.height; y += 8.f) {
                tx += wr.uniform(-0.8f, 0.8f);
                t.pts.push_back({tx, y});
            }
            t.w = wr.uniform(1.2f, 2.2f);
            t.age = wr.uniform(0.f, 6.f);
            s.trails.push_back(t);
        }
        dropSlots.push_back(s);
        // the drops sit on the outside of the glass: a quad a little further out than the misted pane
        const int w = o.wall;
        const float d = 0.13f;
        const float am = (o.a0 + o.a1) * 0.5f, ym = (o.y0 + o.y1) * 0.5f;
        Vector3 pc = wallPoint(w, am, ym, -d - 0.022f);
        Vector3 n = wallFrame(w).normal, r = wallRight(n);
        Vector3 hr = Vector3Scale(r, W * 0.5f), hu{0, H * 0.5f, 0};
        const Rectangle& sr = dropSlots.back().rect;
        float u0 = sr.x / DROPS_W, u1 = (sr.x + sr.width) / DROPS_W;
        float vb = 1.f - (sr.y + sr.height) / DROPS_H, vt = 1.f - sr.y / DROPS_H;
        MeshBuilder pm;
        quadUV(pm, Vector3Subtract(Vector3Negate(hr), hu), Vector3Subtract(hr, hu), Vector3Add(hr, hu), Vector3Add(Vector3Negate(hr), hu),
               {u0, vb}, {u1, vb}, {u1, vt}, {u0, vt}, WHITE);
        dropPanes.push_back({pm.build(true), pc, &mDrops});
    }
    drawDrops();

    // rain outside: the left street (seen through both windows and the door) and the back street
    streaks.resize(170);
    for (Streak& s : streaks) placeStreak(s, wr, true);
}

void Room::Impl::refreshWalkers() {
    const bool day = phase <= 1;
    if (day == walkersDay || !cvWalkers.id) return;
    walkersDay = day;
    drawWalkerAtlas(cvWalkers, day);
}

void Room::Impl::freeWeather(Renderer& r) {
    for (Pane& p : dropPanes)
        if (p.mesh.vertexCount > 0) UnloadMesh(p.mesh);
    dropPanes.clear();
    dropSlots.clear();
    streaks.clear();
    walkers.clear();
    r.unloadMat(mDrops);
    for (Texture2D* t : {&texStreak, &texDropSprite})
        if (t->id) {
            UnloadTexture(*t);
            *t = Texture2D{};
        }
    for (RenderTexture2D* c : {&cvDrops[0], &cvDrops[1], &cvWalkers})
        if (c->id) {
            UnloadRenderTexture(*c);
            *c = RenderTexture2D{};
        }
}

// A rain streak board somewhere outside: the left street (seen through both windows and the door) or the back
// street (through the back window). Streaks near the street lamps catch more light.
void Room::Impl::placeStreak(Streak& s, okey::Rng& rng, bool anywhereY) {
    const bool back = rng.chance(0.24f);
    if (garden) {
        // in the garden: everywhere beyond the awning, and drips running off its edges
        const float u = rng.uniform();
        if (u < 0.16f) {
            const bool left = rng.chance(0.55f);
            s.p = left ? Vector3{w3d::ROOM_X0 - 0.27f, 0.f, rng.uniform(-3.55f, 3.75f)} : Vector3{rng.uniform(-4.4f, 4.1f), 0.f, -3.63f};
            s.p.y = anywhereY ? rng.uniform(-0.4f, 2.9f) : 2.9f;
        } else {
            if (u < 0.5f) s.p = {rng.uniform(-7.5f, -4.6f), 0.f, rng.uniform(-6.f, 5.f)};
            else if (u < 0.85f) s.p = {rng.uniform(-6.f, 4.f), 0.f, rng.uniform(-8.f, -3.8f)};
            else s.p = {rng.uniform(-5.f, 4.f), 0.f, rng.uniform(4.f, 6.f)};
            s.p.y = anywhereY ? rng.uniform(-0.4f, 6.5f) : rng.uniform(5.f, 6.8f);
        }
    } else if (back) s.p = {rng.uniform(-5.2f, -1.6f), 0.f, rng.uniform(-6.2f, -3.75f)};
    else s.p = {rng.uniform(-6.8f, -4.55f), 0.f, rng.uniform(-4.6f, 4.4f)};
    if (!garden) s.p.y = anywhereY ? rng.uniform(-0.4f, 3.8f) : rng.uniform(3.5f, 4.1f);
    s.speed = rng.uniform(6.2f, 8.2f);
    s.var = rng.range(0, 2);
    auto lampK = [&](Vector3 l, float k) {
        float dx = s.p.x - l.x, dz = s.p.z - l.z, dy = (s.p.y - l.y) * 0.5f;
        return k * std::exp(-(dx * dx + dz * dz + dy * dy) / 3.5f);
    };
    s.bright = rng.uniform(0.35f, 0.75f) + lampK(streetLampGlow, 1.4f) + lampK(backLampGlow, 1.1f);
}

// ============================================================================ raindrops on the glass
void Room::Impl::simDrops(DropSlot& s, float dt) {
    const float W = s.rect.width, H = s.rect.height;
    // new drops land: when one lands on another they merge
    s.beadAcc += dt * 16.f * rain;
    while (s.beadAcc >= 1.f) {
        s.beadAcc -= 1.f;
        Bead b{rng.uniform(0.f, W), rng.uniform(0.f, H), rng.uniform(0.6f, 1.9f)};
        bool merged = false;
        for (Bead& o : s.beads) {
            float dx = o.x - b.x, dy = o.y - b.y;
            if (dx * dx + dy * dy < (o.r + b.r) * (o.r + b.r)) {
                o.r = std::sqrt(o.r * o.r + b.r * b.r);
                merged = true;
                break;
            }
        }
        if (!merged) s.beads.push_back(b);
    }
    // a heavy bead starts to run
    for (size_t i = 0; i < s.beads.size();) {
        if (s.beads[i].r > 2.7f && s.runners.size() < 7) {
            Runner rn{s.beads[i].x, s.beads[i].y, s.beads[i].r * 1.05f, 0.f, 0.f, rng.uniform(0.f, 10.f), {}};
            rn.trail.push_back({rn.x, rn.y});
            s.runners.push_back(rn);
            s.beads[i] = s.beads.back();
            s.beads.pop_back();
        } else {
            ++i;
        }
    }
    if ((int)s.beads.size() > 230) s.beads.erase(s.beads.begin(), s.beads.begin() + ((int)s.beads.size() - 230));
    // runners: stop-and-go, wandering a little, swallowing the beads in their way, shedding tiny ones
    for (size_t i = 0; i < s.runners.size();) {
        Runner& rn = s.runners[i];
        if (rn.pause > 0.f) {
            rn.pause -= dt;
        } else {
            float target = 22.f + 30.f * std::min(1.f, (rn.r - 2.f) / 2.5f);
            rn.v += (target - rn.v) * std::min(1.f, dt * 4.f);
            rn.y += rn.v * dt;
            rn.wob += dt;
            rn.x += std::sin(rn.wob * 2.3f) * 3.f * dt;
            if (rng.chance(dt * 0.5f)) {
                rn.pause = rng.uniform(0.2f, 1.2f);
                rn.v = 0.f;
            }
            if (rng.chance(dt * 1.5f) && rn.r > 1.8f) {
                s.beads.push_back({rn.x + rng.uniform(-0.5f, 0.5f), rn.y - rn.r * 1.5f, rng.uniform(0.5f, 0.9f)});
                rn.r = std::sqrt(std::max(rn.r * rn.r - 0.5f, 1.f));
            }
        }
        for (size_t k = 0; k < s.beads.size();) {
            float dx = s.beads[k].x - rn.x, dy = s.beads[k].y - rn.y;
            if (dy > -rn.r && dy < rn.r * 1.6f && std::fabs(dx) < rn.r + s.beads[k].r) {
                rn.r = std::min(4.2f, std::sqrt(rn.r * rn.r + s.beads[k].r * s.beads[k].r * 0.6f));
                s.beads[k] = s.beads.back();
                s.beads.pop_back();
            } else {
                ++k;
            }
        }
        if (rn.trail.empty() || rn.y - rn.trail.back().y > 8.f) rn.trail.push_back({rn.x, rn.y});
        if (rn.y > H + rn.r * 2.f) {
            s.trails.push_back({rn.trail, rn.r * 0.75f, 0.f});
            s.runners[i] = s.runners.back();
            s.runners.pop_back();
        } else {
            ++i;
        }
    }
    for (Trail& t : s.trails) t.age += dt;
    s.trails.erase(std::remove_if(s.trails.begin(), s.trails.end(), [](const Trail& t) { return t.age > 9.f; }), s.trails.end());
}

void Room::Impl::drawDrops() {
    // double-buffered: the pane samples the other canvas while this one is redrawn
    RenderTexture2D& cv = cvDrops[1 - dropsFront];
    beginRoomCanvas(cv);
    const Color body{196, 210, 230, 255};
    // one textured quad per drop (see genDropSprite)
    const Rectangle src{0.f, 0.f, (float)texDropSprite.width, (float)texDropSprite.height};
    auto drop = [&](Vector2 p, float r, float k, float stretch = 1.f) {
        DrawTexturePro(texDropSprite, src, {p.x - r, p.y - r * stretch, 2.f * r, 2.f * r * stretch}, {0.f, 0.f}, 0.f,
                       Color{255, 255, 255, (unsigned char)(255.f * k)});
    };
    for (const DropSlot& s : dropSlots) {
        const Vector2 o{s.rect.x, s.rect.y};
        for (const Trail& t : s.trails) {
            float k = 1.f - t.age / 9.f;
            for (size_t i = 1; i < t.pts.size(); ++i)
                DrawLineEx({o.x + t.pts[i - 1].x, o.y + t.pts[i - 1].y}, {o.x + t.pts[i].x, o.y + t.pts[i].y}, t.w, alpha(body, 0.22f * k));
        }
        for (const Runner& rn : s.runners) {
            for (size_t i = 1; i < rn.trail.size(); ++i)
                DrawLineEx({o.x + rn.trail[i - 1].x, o.y + rn.trail[i - 1].y}, {o.x + rn.trail[i].x, o.y + rn.trail[i].y}, rn.r * 0.75f,
                           alpha(body, 0.2f));
            if (!rn.trail.empty())
                DrawLineEx({o.x + rn.trail.back().x, o.y + rn.trail.back().y}, {o.x + rn.x, o.y + rn.y}, rn.r * 0.75f, alpha(body, 0.2f));
            drop({o.x + rn.x, o.y + rn.y - rn.r * 0.3f}, rn.r, 1.f, 1.35f);  // a running drop is elongated
        }
        for (const Bead& b : s.beads) drop({o.x + b.x, o.y + b.y}, b.r, 1.f);
    }
    endRoomCanvas(cv);
    dropsFront = 1 - dropsFront;
    if (mDrops.material.maps) mDrops.material.maps[MATERIAL_MAP_ALBEDO].texture = cv.texture;
}

// ============================================================================ per frame
void Room::Impl::updateWeather(float dt) {
    updateShower(dt);  // (ozelgun) a passing shower on a fair day (RoomSpecial.cpp)
    // tonight's rain eases off and picks up again over minutes (and comes and goes with the hour and the season)
    rainBase += (rainTarget - rainBase) * std::min(1.f, dt * 0.35f);
    if (rainBase < 0.005f && rainTarget <= 0.f) rainBase = 0.f;
    rain = rainBase * (0.55f + 0.45f * noise1(time / 75.f, seed + 311u));
    if (rainBase > 0.f) {
        for (DropSlot& s : dropSlots) simDrops(s, dt);
        dropRedraw -= dt;
        if (garden) dropRedraw = std::max(dropRedraw, 0.01f);  // (no windows out there: no canvas redraws)
        // 12 Hz is plenty for drops that creep down the glass; every render-to-texture pass makes the driver wait
        // for the GPU (the CPU time is absorbed by the 60 Hz frame, but there is no reason to spend it more often)
        if (dropRedraw <= 0.f) {
            drawDrops();
            dropRedraw = 1.f / 12.f;
        }
        for (Streak& s : streaks) {
            s.p.y -= s.speed * dt;
            if (s.p.y < -0.5f) placeStreak(s, rng, false);
        }
    }
    // passers-by
    walkerT -= dt;
    if (walkerT <= 0.f) {
        // the street is busier by day
        walkerT = (rainBase > 0.3f ? rng.uniform(9.f, 30.f) : rng.uniform(7.f, 24.f)) * (1.f - 0.6f * dayK);
        if (walkers.size() < (dayK > 0.5f ? 3u : 2u)) {
            Walker w;
            const int* pool = (rainBase > 0.3f || snow > 0.6f) ? kRainyFigs : kDryFigs;
            w.type = pool[rng.range(0, 6)];
            const FigSpec& f = kFigs[w.type];
            w.speed = f.stride > 1.2f ? rng.uniform(2.6f, 3.1f) : (f.arms == Arms::Cane ? rng.uniform(0.75f, 0.9f) : rng.uniform(1.15f, 1.45f));
            const bool back = !garden && rng.chance(0.3f);  // (the garden has the sea behind it, no back street)
            const float sgn = rng.chance(0.5f) ? 1.f : -1.f;
            if (back) {
                w.p = {sgn > 0 ? -7.6f : 0.6f, 0.f, -4.0f + rng.uniform(-0.12f, 0.12f)};
                w.dir = {sgn, 0.f, 0.f};
                w.left = 8.2f;
            } else {
                w.p = {-4.88f + rng.uniform(-0.08f, 0.12f), 0.f, sgn > 0 ? -6.8f : 6.8f};
                w.dir = {0.f, 0.f, sgn};
                w.left = 13.6f;
            }
            w.dist = rng.uniform(0.f, 3.f);
            walkers.push_back(w);
        }
    }
    for (Walker& w : walkers) {
        w.p = Vector3Add(w.p, Vector3Scale(w.dir, w.speed * dt));
        w.dist += w.speed * dt;
        w.left -= w.speed * dt;
    }
    walkers.erase(std::remove_if(walkers.begin(), walkers.end(), [](const Walker& w) { return w.left <= 0.f; }), walkers.end());
}

void Room::Impl::submitWeather(Renderer& r) {
    const Camera3D& cam = r.lastCamera();
    Vector3 fwd = Vector3Subtract(cam.target, cam.position);
    Vector3 camR = Vector3Normalize(Vector3CrossProduct(fwd, Vector3{0, 1, 0}));
    // passers-by
    for (const Walker& w : walkers) {
        const FigSpec& f = kFigs[w.type];
        const float step = 0.62f * f.stride;  // metres per frame of the cycle (two steps per four frames)
        int frame = (int)(w.dist / (step * 0.5f)) % WALK_FRAMES;
        Rectangle c = walkCell(w.type, frame);
        // canvas textures are stored bottom-up: flip v; mirror when walking leftward on screen
        bool right = Vector3DotProduct(w.dir, camR) >= 0.f;
        Rectangle src{c.x, (float)WALK_H - c.y - c.height, right ? c.width : -c.width, -c.height};
        float bounce = 0.008f * std::sin(w.dist / step * 2.f * PI);
        r.submitBillboard(cvWalkers.texture, src, {w.p.x, WALK_BOARD_H * 0.5f + bounce, w.p.z}, {WALK_BOARD_W, WALK_BOARD_H}, WHITE, false);
    }
    if (rainBase <= 0.01f) return;
    if (!garden)
        for (const Pane& p : dropPanes) r.submit(&p.mesh, p.mat, MatrixTranslate(p.at.x, p.at.y, p.at.z), Transparent | DoubleSided);
    const float tw = (float)texStreak.width;
    const size_t nStreaks = garden ? streaks.size() : std::min(streaks.size(), (size_t)170);  // (the garden adds more)
    for (size_t si = 0; si < nStreaks; ++si) {
        const Streak& s = streaks[si];
        // three horizontal crops of the streak strip keep neighbouring boards from repeating
        Rectangle src{0.f, (float)s.var * 40.f, tw, (float)texStreak.height - 80.f};
        float a = std::clamp(95.f * rain * s.bright, 0.f, 255.f);
        r.submitBillboard(texStreak, src, s.p, {0.3f, 1.05f}, Color{176, 190, 214, (unsigned char)a}, true);
    }
}

float Room::rainAmount() const { return impl_->ready ? impl_->rain : 0.f; }

}  // namespace r3d
