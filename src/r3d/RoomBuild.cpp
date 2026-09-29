// Room module: all static geometry, built once at init and merged per material (room owner).
#include "r3d/RoomInternal.h"

#include <algorithm>
#include <cmath>

namespace r3d {
namespace rm {

// ============================================================================ geometry helpers
Vector3 wallRight(Vector3 n) { return {n.z, 0.f, -n.x}; }

void quadUV(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector2 t0, Vector2 t1, Vector2 t2,
            Vector2 t3, Color c) {
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)));
    int a = b.vertex(p0, n, t0, c), i1 = b.vertex(p1, n, t1, c), i2 = b.vertex(p2, n, t2, c), i3 = b.vertex(p3, n, t3, c);
    b.quad(a, i1, i2, i3);
}

void boxW(MeshBuilder& b, Vector3 c, Vector3 s, float ts, Color col, int grain) {
    const Vector3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float h[3] = {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
    for (int f = 0; f < 6; ++f) {
        int a = f / 2;
        float sg = (f % 2) ? -1.f : 1.f;
        Vector3 n = Vector3Scale(ax[a], sg);
        int iu = (a + 1) % 3, iv = (a + 2) % 3;
        if (grain == iv) std::swap(iu, iv);
        Vector3 u = ax[iu], v = ax[iv];
        if (Vector3DotProduct(Vector3CrossProduct(u, v), n) < 0) v = Vector3Negate(v);
        Vector3 fc = Vector3Add(c, Vector3Scale(n, h[a]));
        auto P = [&](float su, float sv) {
            return Vector3Add(fc, Vector3Add(Vector3Scale(u, su * h[iu]), Vector3Scale(v, sv * h[iv])));
        };
        Vector3 p0 = P(-1, -1), p1 = P(1, -1), p2 = P(1, 1), p3 = P(-1, 1);
        auto T = [&](Vector3 p) { return Vector2{Vector3DotProduct(p, u) / ts, Vector3DotProduct(p, v) / ts}; };
        quadUV(b, p0, p1, p2, p3, T(p0), T(p1), T(p2), T(p3), col);
    }
}

void rbox(MeshBuilder& b, Vector3 c, Vector3 s, float r, int seg, Color col, const Matrix* base) {
    if (s.x >= s.y && s.x >= s.z) {
        b.roundedBox(c, s, r, seg, col);
        return;
    }
    Matrix rot;
    Vector3 ls;
    if (s.y >= s.z) {
        rot = MatrixRotateZ(PI * 0.5f);  // local X -> world Y
        ls = {s.y, s.x, s.z};
    } else {
        rot = MatrixRotateY(-PI * 0.5f);  // local X -> world Z
        ls = {s.z, s.y, s.x};
    }
    Matrix m = MatrixMultiply(rot, MatrixTranslate(c.x, c.y, c.z));
    if (base) m = MatrixMultiply(m, *base);
    b.setTransform(m);
    b.roundedBox({0, 0, 0}, ls, r, seg, col);
    if (base) b.setTransform(*base);
    else b.resetTransform();
}

void latheAt(MeshBuilder& b, Vector3 pos, const std::vector<Vector2>& prof, int seg, bool capB, bool capT, Color col,
             const Matrix* base, float yawDeg, float scale) {
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY(yawDeg * DEG2RAD)),
                              MatrixTranslate(pos.x, pos.y, pos.z));
    if (base) m = MatrixMultiply(m, *base);
    b.setTransform(m);
    b.lathe(prof, seg, capB, capT, col);
    if (base) b.setTransform(*base);
    else b.resetTransform();
}

void ringTube(MeshBuilder& b, Vector3 c, float R, float r, int seg, Color col, float rz) {
    if (rz < 0) rz = R;
    std::vector<Vector3> path;
    for (int i = 0; i <= seg; ++i) {
        float a = 2.f * PI * i / seg;
        path.push_back({c.x + std::cos(a) * R, c.y, c.z + std::sin(a) * rz});
    }
    b.tube(path, r, 8, col);
}

void canvasQuad(MeshBuilder& b, Vector3 c, float w, float h, Vector3 n, Rectangle src, float cw, float ch, Color col) {
    Vector3 r = wallRight(n), up{0, 1, 0};
    Vector3 hr = Vector3Scale(r, w * 0.5f), hu = Vector3Scale(up, h * 0.5f);
    float u0 = src.x / cw, u1 = (src.x + src.width) / cw;
    float vb = 1.f - (src.y + src.height) / ch, vt = 1.f - src.y / ch;
    quadUV(b, Vector3Subtract(Vector3Subtract(c, hr), hu), Vector3Subtract(Vector3Add(c, hr), hu),
           Vector3Add(Vector3Add(c, hr), hu), Vector3Add(Vector3Subtract(c, hr), hu), {u0, vb}, {u1, vb}, {u1, vt}, {u0, vt},
           col);
}

void canvasQuadUp(MeshBuilder& b, Vector3 c, float w, float d, Rectangle src, float cw, float ch, float yawDeg, Color col) {
    Matrix m = MatrixRotateY(yawDeg * DEG2RAD);
    auto P = [&](float x, float z) {
        Vector3 p = Vector3Transform({x, 0, z}, m);
        return Vector3{c.x + p.x, c.y, c.z + p.z};
    };
    float u0 = src.x / cw, u1 = (src.x + src.width) / cw;
    float vb = 1.f - (src.y + src.height) / ch, vt = 1.f - src.y / ch;
    quadUV(b, P(-w / 2, d / 2), P(w / 2, d / 2), P(w / 2, -d / 2), P(-w / 2, -d / 2), {u0, vb}, {u1, vb}, {u1, vt},
           {u0, vt}, col);
}

// ============================================================================ builders
struct Builders {
    MeshBuilder floor, wall, ceil, wood, woodCast, woodDark, varnish, varnishCast, paint, paintCast, metal, metalCast,
        brass, ceramic, ceramicCast, cloth, clothCast, felt, marble, art, artGloss, artChalk, leaf, street, tea, score, tv, bulbs,
        bottle;
};

}  // namespace rm

using namespace rm;
namespace {

constexpr float X0 = w3d::ROOM_X0, X1 = w3d::ROOM_X1, Z0 = w3d::ROOM_Z0, Z1 = w3d::ROOM_Z1, CY = w3d::CEILING_Y;
constexpr float REVEAL = 0.26f;  // wall thickness at openings

// Framed wall decorations: (wall, a centre, y centre, width, height). a = distance along the wall (wallFrame).
struct Deco {
    int wall;
    float a, y, w, h;
};
const Deco D_SCORE{0, w3d::SCOREBOARD_POS.x - X0, w3d::SCOREBOARD_POS.y, w3d::SCOREBOARD_W, w3d::SCOREBOARD_H};
const Deco D_KUMAR{0, w3d::SCOREBOARD_POS.x - X0, 1.99f, 0.5f, 0.145f};
const Deco D_PRICE{0, 1.12f - X0, 1.33f, 0.62f, 0.417f};
const Deco D_CALENDAR{0, 1.76f - X0, 1.47f, 0.24f, 0.37f};
const Deco D_CLOCK{0, 0.f - X0, 2.3f, 0.42f, 0.42f};
const Deco D_SHIP{0, -2.45f - X0, 2.04f, 0.62f, 0.44f};
const Deco D_VERESIYE{0, 3.0f - X0, 2.52f, 0.6f, 0.174f};
const Deco D_BOSPHORUS{2, 4.05f, 1.86f, 0.64f, 0.434f};
const Deco D_POSTER{2, 6.2f, 1.5f, 0.3f, 0.23f};
const Deco D_WELCOME{2, 0.8f, 2.5f, 0.46f, 0.2f};
const Deco D_TEAM{3, 2.0f, 1.84f, 0.62f, 0.42f};
const Deco D_PENNANT{3, 4.5f, 2.02f, 0.45f, 0.28f};
const Deco D_CERT{3, 3.45f, 1.45f, 0.4f, 0.286f};
const Deco D_MIRROR{1, 3.3f, 1.64f, 0.6f, 0.84f};
const Deco D_OLDPHOTO{1, 5.4f, 1.8f, 0.5f, 0.357f};
const Deco D_RULES{1, 4.45f, 1.6f, 0.6f, 0.352f};

Vector3 decoCenter(const Deco& d, float out) { return wallPoint(d.wall, d.a, d.y, out); }
Vector3 wallN(int wall) { return wallFrame(wall).normal; }
Vector3 n0(int wall) { return wallFrame(wall).normal; }

// Axis-aligned world box for something lying flat on a wall: along = extent along the wall, depth = out of it.
Vector3 wallBoxSize(int wall, float along, float h, float depth) {
    Vector3 r = wallFrame(wall).right;
    return std::fabs(r.x) > 0.5f ? Vector3{along, h, depth} : Vector3{depth, h, along};
}
int wallGrain(int wall) { return std::fabs(wallFrame(wall).right.x) > 0.5f ? 0 : 2; }

// Rectangle subtraction: cover [A0,A1]x[Y0,Y1] minus the openings of one wall.
struct Rect2 {
    float a0, a1, y0, y1;
};
std::vector<Rect2> cutBand(float A0, float A1, float Y0, float Y1, const std::vector<Opening>& ops, int wall) {
    std::vector<float> xs{A0, A1};
    for (const Opening& o : ops)
        if (o.wall == wall) {
            xs.push_back(std::clamp(o.a0, A0, A1));
            xs.push_back(std::clamp(o.a1, A0, A1));
        }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::vector<Rect2> out;
    for (size_t i = 0; i + 1 < xs.size(); ++i) {
        float a0 = xs[i], a1 = xs[i + 1];
        if (a1 - a0 < 1e-4f) continue;
        float mid = (a0 + a1) * 0.5f;
        std::vector<std::pair<float, float>> blocked;
        for (const Opening& o : ops)
            if (o.wall == wall && mid > o.a0 && mid < o.a1) blocked.push_back({std::max(o.y0, Y0), std::min(o.y1, Y1)});
        std::sort(blocked.begin(), blocked.end());
        float y = Y0;
        for (auto& bl : blocked) {
            if (bl.second <= bl.first) continue;
            if (bl.first > y + 1e-4f) out.push_back({a0, a1, y, bl.first});
            y = std::max(y, bl.second);
        }
        if (Y1 > y + 1e-4f) out.push_back({a0, a1, y, Y1});
    }
    return out;
}

// Bentwood (Thonet No. 14 style) café chair, sitter faces -Z, seat top at CHAIR_SEAT_Y.
Mesh buildChairMesh() {
    MeshBuilder b;
    const Color wood{104, 64, 40, 255};  // dark walnut stain, worn lighter on the seat rim
    const float sy = w3d::CHAIR_SEAT_Y;
    b.lathe({{0.001f, sy - 0.032f}, {0.188f, sy - 0.032f}, {0.203f, sy - 0.024f}, {0.207f, sy - 0.011f}, {0.2f, sy - 0.002f},
             {0.17f, sy + 0.002f}, {0.001f, sy}},
            32, false, false, Color{128, 84, 54, 255});  // the seat is polished lighter by use
    ringTube(b, {0, sy - 0.04f, 0}, 0.192f, 0.012f, 28, wood);
    for (int sx = -1; sx <= 1; sx += 2)
        b.tube({{sx * 0.13f, sy - 0.03f, -0.12f}, {sx * 0.15f, sy - 0.2f, -0.145f}, {sx * 0.172f, 0.12f, -0.172f},
                {sx * 0.19f, 0.0f, -0.19f}},
               0.0145f, 8, wood);
    // back legs rising into the backrest hoop (one bent piece)
    std::vector<Vector3> hoop;
    auto side = [&](float sx, bool up) {
        std::vector<Vector3> p{{sx * 0.19f, 0.0f, 0.215f},  {sx * 0.172f, 0.16f, 0.182f}, {sx * 0.152f, sy - 0.04f, 0.15f},
                               {sx * 0.152f, sy + 0.1f, 0.162f}, {sx * 0.16f, sy + 0.24f, 0.19f}, {sx * 0.166f, sy + 0.36f, 0.214f}};
        if (up) hoop.insert(hoop.end(), p.begin(), p.end());
        else hoop.insert(hoop.end(), p.rbegin(), p.rend());
    };
    side(1.f, true);
    for (int i = 1; i < 16; ++i) {
        float t = PI * i / 16.f;
        hoop.push_back({0.166f * std::cos(t), sy + 0.36f + 0.12f * std::sin(t), 0.214f + 0.022f * std::sin(t)});
    }
    side(-1.f, false);
    b.tube(hoop, 0.0135f, 8, wood);
    // inner loop of the backrest
    std::vector<Vector3> loop;
    for (int i = 0; i <= 22; ++i) {
        float t = (-0.35f + 1.7f * i / 22.f) * PI;
        loop.push_back({0.085f * std::cos(t), sy + 0.2f + 0.14f * std::sin(t), 0.19f + 0.015f * std::sin(t)});
    }
    b.tube(loop, 0.0095f, 8, wood);
    ringTube(b, {0, 0.2f, 0.005f}, 0.198f, 0.009f, 28, wood, 0.2f);
    return b.build(true);
}

// Ince belli tea glass profile (outer), base at y = 0.
const std::vector<Vector2> kGlassProfile{{0.0185f, 0.f},    {0.0212f, 0.006f}, {0.0238f, 0.018f}, {0.0228f, 0.03f},
                                         {0.0186f, 0.045f}, {0.0166f, 0.055f}, {0.0180f, 0.068f}, {0.0212f, 0.082f},
                                         {0.0232f, 0.095f}};
std::vector<Vector2> teaProfile(float level) {
    std::vector<Vector2> p;
    for (const Vector2& v : kGlassProfile) {
        if (v.y > level) break;
        p.push_back({v.x * 0.9f, std::max(v.y, 0.004f)});
    }
    // interpolate the surface radius at the level
    for (size_t i = 0; i + 1 < kGlassProfile.size(); ++i)
        if (kGlassProfile[i].y <= level && kGlassProfile[i + 1].y > level) {
            float t = (level - kGlassProfile[i].y) / (kGlassProfile[i + 1].y - kGlassProfile[i].y);
            p.push_back({(kGlassProfile[i].x + (kGlassProfile[i + 1].x - kGlassProfile[i].x) * t) * 0.9f, level});
        }
    return p;
}
const std::vector<Vector2> kSaucer{{0.0f, 0.0f},    {0.028f, 0.0f},   {0.030f, 0.003f}, {0.046f, 0.008f}, {0.052f, 0.013f},
                                   {0.050f, 0.0145f}, {0.044f, 0.0105f}, {0.030f, 0.0065f}, {0.0f, 0.0065f}};
const std::vector<Vector2> kTinAshtray{{0.f, 0.f},       {0.044f, 0.f},    {0.05f, 0.004f}, {0.053f, 0.017f},
                                       {0.049f, 0.018f}, {0.045f, 0.006f}, {0.f, 0.006f}};

void butt(MeshBuilder& b, Vector3 p, float yawDeg, float len, bool bent) {
    float a = yawDeg * DEG2RAD;
    Vector3 d{std::cos(a), 0.f, std::sin(a)};
    Vector3 q = Vector3Add(p, Vector3Scale(d, len * 0.55f));
    b.capsule(p, q, 0.0038f, 8, Color{206, 150, 84, 255});
    Vector3 e = Vector3Add(q, Vector3Scale(bent ? Vector3{d.z, 0.2f, -d.x} : d, len * 0.45f));
    b.capsule(q, e, 0.0036f, 8, Color{236, 232, 222, 255});
    b.sphere(e, 0.0037f, 4, 6, Color{60, 56, 52, 255});
}

void glassOnSaucer(Builders& B, MeshBuilder& glassLocal, Vector3 tableC, Vector3 p, float level, bool oralet, okey::Rng& rng) {
    latheAt(B.ceramicCast, p, kSaucer, 24, false, false, Color{246, 244, 238, 255});
    ringTube(B.ceramicCast, {p.x, p.y + 0.0135f, p.z}, 0.049f, 0.0012f, 24, Color{190, 150, 60, 255});
    Vector3 g{p.x, p.y + 0.0065f, p.z};
    latheAt(glassLocal, Vector3Subtract(g, Vector3{tableC.x, 0, tableC.z}), kGlassProfile, 24, true, false,
            Color{255, 255, 255, 255});
    if (level > 0.01f)
        latheAt(B.tea, g, teaProfile(level), 20, true, true, oralet ? Color{250, 132, 30, 255} : Color{255, 255, 255, 255});
    // spoon resting on the saucer
    float a = rng.uniform(0.f, 2.f * PI);
    Vector3 s0{p.x + std::cos(a) * 0.028f, p.y + 0.012f, p.z + std::sin(a) * 0.028f};
    Vector3 s1{p.x + std::cos(a + 0.5f) * 0.075f, p.y + 0.015f, p.z + std::sin(a + 0.5f) * 0.075f};
    B.metalCast.capsule(s0, s1, 0.0017f, 6, Color{200, 200, 196, 255});
    B.metalCast.ellipsoid(s0, {0.007f, 0.002f, 0.007f}, 4, 8, Color{200, 200, 196, 255});
}

// A rubber plant (kauçuk) in an old olive-oil tin: a few woody stems with big waxy leaves.
void rubberPlant(Builders& B, Vector3 base, okey::Rng& rng) {
    const float AW = ART_W, AH = ART_H;
    const float pw = 0.25f, ph = 0.3f;
    // the tin: box body, rolled rims, label on the two faces that show
    boxW(B.metal, {base.x, ph * 0.5f, base.z}, {pw, ph, pw}, 0.5f, Color{190, 160, 70, 255});
    for (float y : {0.006f, ph - 0.006f}) B.metal.roundedBox({base.x, y, base.z}, {pw + 0.012f, 0.012f, pw + 0.012f}, 0.004f, 1, Color{150, 130, 70, 255});
    canvasQuad(B.art, {base.x, ph * 0.5f, base.z + pw * 0.5f + 0.001f}, pw * 0.96f, ph * 0.64f, {0, 0, 1}, art::TENEKE, AW, AH);
    canvasQuad(B.art, {base.x - pw * 0.5f - 0.001f, ph * 0.5f, base.z}, pw * 0.96f, ph * 0.64f, {-1, 0, 0}, art::TENEKE, AW, AH);
    B.cloth.box({base.x, ph - 0.02f, base.z}, {pw - 0.02f, 0.01f, pw - 0.02f}, Color{50, 36, 26, 255});  // soil
    // stems and leaves
    const Color leafC[3] = {{50, 92, 48, 255}, {62, 106, 52, 255}, {44, 80, 44, 255}};
    const Color stemC{96, 84, 56, 255};
    struct Stem {
        float lean, dir, h;
    };
    const Stem stems[4] = {{0.05f, 0.4f, 1.08f}, {0.16f, 2.6f, 0.9f}, {0.14f, 4.4f, 0.74f}, {0.2f, 5.6f, 0.62f}};
    for (const Stem& st : stems) {
        std::vector<Vector3> path;
        const int n = 8;
        for (int i = 0; i <= n; ++i) {
            float t = (float)i / n;
            float off = st.lean * t * t;
            path.push_back({base.x + std::cos(st.dir) * off, ph - 0.02f + (st.h - ph) * t, base.z + std::sin(st.dir) * off});
        }
        B.varnish.tube(path, 0.009f, 6, stemC);
        // alternate leaves up the stem, bigger in the middle, the newest ones near the tip point upward
        const int leaves = 9;
        for (int k = 0; k < leaves; ++k) {
            float t = 0.2f + 0.8f * (k + 0.5f) / leaves;
            Vector3 at = path[(size_t)std::min(n, (int)std::round(t * n))];
            float yaw = st.dir + k * 2.4f + rng.uniform(-0.3f, 0.3f);
            float len = (0.17f + 0.11f * std::sin(t * PI)) * rng.uniform(0.9f, 1.1f), wid = len * 0.48f;
            float pitch = (k == leaves - 1) ? 70.f : rng.uniform(15.f, 40.f);  // above the horizontal
            Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateZ(pitch * DEG2RAD), MatrixRotateY(-yaw)), MatrixTranslate(at.x, at.y, at.z));
            // leaf in local space: +X along the midrib, +Z across, folded slightly along the midrib, tip drooping
            const int seg = 8;
            Color lc = leafC[rng.range(0, 2)];
            bool started = false;
            for (int i = 0; i <= seg; ++i) {
                float u = (float)i / seg;
                float x = 0.03f + u * len, w = wid * 0.5f * std::pow(std::sin(PI * std::min(0.98f, 0.08f + u * 0.92f)), 0.7f);
                float droop = -0.35f * len * u * u;
                Vector3 c = Vector3Transform({x, droop, 0.f}, M);
                Vector3 l = Vector3Transform({x, droop - w * 0.25f, -w}, M), r = Vector3Transform({x, droop - w * 0.25f, w}, M);
                Vector3 up = Vector3Normalize(Vector3Subtract(Vector3Transform({x, droop + 1.f, 0.f}, M), c));
                int a = B.leaf.vertex(l, up, {0, u}, scaleRgb(lc, 0.9f));
                B.leaf.vertex(c, up, {0.5f, u}, scaleRgb(lc, 1.15f));
                B.leaf.vertex(r, up, {1, u}, scaleRgb(lc, 0.9f));
                if (started) {
                    B.leaf.quad(a - 3, a - 2, a + 1, a);  // counter-clockwise seen from the leaf's upper side
                    B.leaf.quad(a - 2, a - 1, a + 2, a + 1);
                }
                started = true;
            }
            B.varnish.capsule(at, Vector3Transform({0.035f, 0.f, 0.f}, M), 0.004f, 4, stemC);  // petiole
        }
    }
}

}  // namespace

// ============================================================================ Room::Impl builders
void Room::Impl::addStatic(const MeshBuilder& b, const Mat* m, uint32_t flags, Matrix xf) {
    if (b.vertexCount() == 0) return;
    Static s;
    s.mesh = b.build(true);
    s.mat = m;
    s.flags = flags;
    s.xf = xf;
    statics.push_back(s);
}

void Room::Impl::planWalls() {
    openings = {
        {0, 0.35f, 1.25f, 0.95f, 2.25f, 0},  // back wall window (x -3.85 .. -2.95)
        {2, 4.70f, 5.90f, 0.95f, 2.45f, 0},  // left wall window 1 (z -2.3 .. -1.1)
        {2, 2.20f, 3.40f, 0.95f, 2.45f, 0},  // left wall window 2 (z 0.2 .. 1.4)
        {2, 0.35f, 1.25f, 0.00f, 2.25f, 1},  // street door (z 2.35 .. 3.25)
        {1, 0.45f, 1.25f, 0.00f, 2.08f, 2},  // WC door leaf (front wall, x 2.95 .. 3.75)
    };
    marks.clear();
    auto frameMark = [&](const Deco& d, float margin) {
        marks.push_back({d.wall, d.a - d.w / 2 - margin, d.a + d.w / 2 + margin, d.y - d.h / 2 - margin, d.y + d.h / 2 + margin, 0});
    };
    frameMark(D_SCORE, 0.05f);
    frameMark(D_KUMAR, 0.01f);
    frameMark(D_PRICE, 0.04f);
    frameMark(D_CALENDAR, 0.0f);
    frameMark(D_CLOCK, 0.02f);
    frameMark(D_SHIP, 0.04f);
    frameMark(D_VERESIYE, 0.01f);
    frameMark(D_BOSPHORUS, 0.05f);
    frameMark(D_POSTER, 0.0f);
    frameMark(D_WELCOME, 0.01f);
    frameMark(D_TEAM, 0.03f);
    frameMark(D_CERT, 0.02f);
    frameMark(D_MIRROR, 0.07f);
    frameMark(D_OLDPHOTO, 0.03f);
    frameMark(D_RULES, 0.03f);
    marks.push_back({0, 6.30f, 8.10f, 1.47f, 1.52f, 0});  // counter shelves
    marks.push_back({0, 6.30f, 8.10f, 1.87f, 1.92f, 0});
    marks.push_back({0, 0.0f, 0.42f, 2.31f, 2.83f, 0});   // TV in the corner
    marks.push_back({2, 6.58f, 7.0f, 2.31f, 2.83f, 0});
    marks.push_back({3, 3.0f, 3.9f, 1.89f, 1.94f, 0});    // trophy shelf
    marks.push_back({0, 4.62f, 4.92f, 1.66f, 2.05f, 1});  // ghosts of removed pictures
    marks.push_back({2, 1.52f, 1.92f, 1.5f, 1.95f, 1});
    marks.push_back({1, 1.9f, 2.3f, 1.55f, 1.9f, 1});
    marks.push_back({3, 5.81f, 6.09f, 2.45f, 2.45f, 2});  // stove pipe soot
    marks.push_back({0, 6.45f, 7.25f, 1.95f, 1.95f, 2});  // above the tea stove
    marks.push_back({0, 1.2f, 2.2f, 2.55f, 3.08f, 3});    // water stains
    marks.push_back({3, 5.9f, 6.8f, 2.6f, 3.08f, 3});
    marks.push_back({1, 6.4f, 7.5f, 2.45f, 3.0f, 3});
    marks.push_back({2, 3.5f, 4.3f, 2.7f, 3.08f, 3});
}

void Room::Impl::buildAll() {
    Builders B;
    buildArchitecture(B);
    buildWallDecor(B);
    buildCounter(B);
    buildBgTables(B);
    buildLamps(B);
    buildStoveTvFan(B);
    buildStreetAndWindows(B);
    buildAshtray();
    buildCurtain();
    chairMesh = buildChairMesh();

    // opaque statics (order: big architecture first)
    addStatic(B.floor, &mFloor, 0);
    addStatic(B.wall, &mWall, 0);
    addStatic(B.ceil, &mCeil, 0);
    addStatic(B.wood, &mWood, 0);
    addStatic(B.woodCast, &mWood, CastShadow);
    addStatic(B.woodDark, &mWoodDark, 0);
    addStatic(B.varnish, &mVarnish, 0);
    addStatic(B.varnishCast, &mVarnish, CastShadow);
    addStatic(B.paint, &mPaint, 0);
    addStatic(B.paintCast, &mPaint, CastShadow);
    addStatic(B.metal, &mMetal, 0);
    addStatic(B.metalCast, &mMetal, CastShadow);
    addStatic(B.brass, &mBrass, 0);
    addStatic(B.ceramic, &mCeramic, 0);
    addStatic(B.ceramicCast, &mCeramic, CastShadow);
    addStatic(B.cloth, &mCloth, 0);
    addStatic(B.clothCast, &mCloth, CastShadow);
    addStatic(B.felt, &mFelt, 0);
    addStatic(B.marble, &mMarble, CastShadow);
    addStatic(B.art, &mArt, 0);
    addStatic(B.artGloss, &mArtGloss, 0);
    addStatic(B.artChalk, &mArtChalk, 0);
    addStatic(B.leaf, &mLeaf, DoubleSided);
    addStatic(B.tea, &mTea, 0);
    addStatic(B.bottle, &mBottle, 0);
    addStatic(B.score, &mScore, 0);
    addStatic(B.tv, &mTv, NoFog);
    addStatic(B.bulbs, &mBulb, 0);
    addStatic(B.street, &mStreet, NoFog | DoubleSided);
}

// ---------------------------------------------------------------------------- walls, floor, ceiling, wainscot
void Room::Impl::buildArchitecture(Builders& B) {
    const float H = CY - WAINSCOT_H;
    // --- plaster above the wainscot (unique wall atlas rows)
    for (int w = 0; w < 4; ++w) {
        WallFrame f = wallFrame(w);
        auto uv = [&](float a, float y) {
            return Vector2{a / f.length, (w * WALL_ROW + (CY - std::clamp(y, WAINSCOT_H, CY)) / H * WALL_ROW) / (float)WALL_TEX};
        };
        for (const Rect2& r : cutBand(0.f, f.length, WAINSCOT_H, CY, openings, w)) {
            quadUV(B.wall, wallPoint(w, r.a0, r.y0), wallPoint(w, r.a1, r.y0), wallPoint(w, r.a1, r.y1), wallPoint(w, r.a0, r.y1),
                   uv(r.a0, r.y0), uv(r.a1, r.y0), uv(r.a1, r.y1), uv(r.a0, r.y1), WHITE);
        }
        // reveals of windows and the street door (plaster), sills (marble)
        for (const Opening& o : openings) {
            if (o.wall != w || o.kind == 2) continue;
            auto P = [&](float a, float y, float d) { return wallPoint(w, a, y, -d); };
            Color rc{226, 214, 196, 255};
            float yb = std::max(o.y0, WAINSCOT_H);
            // jambs (facing into the opening)
            quadUV(B.wall, P(o.a0, o.y0, 0), P(o.a0, o.y0, REVEAL), P(o.a0, o.y1, REVEAL), P(o.a0, o.y1, 0), uv(o.a0, yb),
                   uv(o.a0 - 0.05f, yb), uv(o.a0 - 0.05f, o.y1), uv(o.a0, o.y1), rc);
            quadUV(B.wall, P(o.a1, o.y0, REVEAL), P(o.a1, o.y0, 0), P(o.a1, o.y1, 0), P(o.a1, o.y1, REVEAL), uv(o.a1 + 0.05f, yb),
                   uv(o.a1, yb), uv(o.a1, o.y1), uv(o.a1 + 0.05f, o.y1), rc);
            // head (faces down)
            quadUV(B.wall, P(o.a0, o.y1, REVEAL), P(o.a1, o.y1, REVEAL), P(o.a1, o.y1, 0), P(o.a0, o.y1, 0), uv(o.a0, o.y1 + 0.05f),
                   uv(o.a1, o.y1 + 0.05f), uv(o.a1, o.y1), uv(o.a0, o.y1), rc);
            if (o.kind == 0) {
                Vector3 sc = wallPoint(w, (o.a0 + o.a1) * 0.5f, o.y0 - 0.02f, -REVEAL * 0.5f + 0.03f);
                boxW(B.marble, sc, wallBoxSize(w, o.a1 - o.a0 + 0.1f, 0.04f, REVEAL + 0.06f), 0.6f, Color{236, 232, 224, 255},
                     wallGrain(w));
            } else {
                // threshold step
                Vector3 sc = wallPoint(w, (o.a0 + o.a1) * 0.5f, 0.012f, -REVEAL * 0.5f + 0.02f);
                boxW(B.marble, sc, wallBoxSize(w, o.a1 - o.a0, 0.024f, REVEAL + 0.04f), 0.6f, Color{200, 196, 190, 255}, wallGrain(w));
            }
        }
    }

    // --- wainscot: back boards, raised panels, cap rail, skirting (varnished dark wood)
    okey::Rng wr(seed + 3);
    for (int w = 0; w < 4; ++w) {
        WallFrame f = wallFrame(w);
        int g = wallGrain(w);
        for (const Rect2& r : cutBand(0.f, f.length, 0.f, WAINSCOT_H, openings, w)) {
            if (r.y1 < 0.5f) continue;  // tiny pieces (above door heads don't exist below 1 m)
            float len = r.a1 - r.a0, mid = (r.a0 + r.a1) * 0.5f;
            float top = r.y1;
            Color back{150, 100, 66, 255}, panel{170, 116, 76, 255}, rail{128, 82, 52, 255}, skirt{84, 56, 38, 255};
            boxW(B.wood, wallPoint(w, mid, top * 0.5f, 0.012f), wallBoxSize(w, len, top, 0.024f), 0.9f, back, g);
            boxW(B.wood, wallPoint(w, mid, 0.065f, 0.03f), wallBoxSize(w, len, 0.13f, 0.036f), 0.9f, skirt, g);
            if (top > WAINSCOT_H - 0.03f) {
                Vector3 rc = wallPoint(w, mid, WAINSCOT_H - 0.008f, 0.034f);
                rbox(B.wood, rc, wallBoxSize(w, len, 0.056f, 0.05f), 0.012f, 2, rail);
                boxW(B.wood, wallPoint(w, mid, WAINSCOT_H - 0.045f, 0.03f), wallBoxSize(w, len, 0.02f, 0.03f), 0.9f, rail, g);
            }
            int n = std::max(1, (int)std::round((len - 0.08f) / 0.56f));
            float pw = (len - 0.09f * (n + 1)) / n;
            float py0 = 0.2f, py1 = std::min(top - 0.12f, 0.86f);
            if (pw < 0.12f || py1 - py0 < 0.2f) continue;
            for (int k = 0; k < n; ++k) {
                float a = r.a0 + 0.09f + k * (pw + 0.09f) + pw * 0.5f;
                Color pc = scaleRgb(panel, wr.uniform(0.9f, 1.06f));
                Vector3 c = wallPoint(w, a, (py0 + py1) * 0.5f, 0.03f);
                Vector3 s = wallBoxSize(w, pw, py1 - py0, 0.014f);
                B.wood.roundedBox(c, s, 0.012f, 2, pc);
                Vector3 c2 = wallPoint(w, a, (py0 + py1) * 0.5f, 0.036f);
                B.wood.roundedBox(c2, wallBoxSize(w, pw - 0.07f, py1 - py0 - 0.07f, 0.01f), 0.01f, 2, scaleRgb(pc, 1.04f));
            }
        }
    }

    // --- door casings (street door and WC door)
    for (const Opening& o : openings) {
        if (o.kind == 0) continue;
        int w = o.wall, g = wallGrain(w);
        Color cc{120, 78, 50, 255};
        float t = 0.075f;
        boxW(B.woodCast, wallPoint(w, o.a0 - t / 2, o.y1 * 0.5f + 0.02f, 0.015f), wallBoxSize(w, t, o.y1 + 0.04f, 0.03f), 0.8f, cc, 1);
        boxW(B.woodCast, wallPoint(w, o.a1 + t / 2, o.y1 * 0.5f + 0.02f, 0.015f), wallBoxSize(w, t, o.y1 + 0.04f, 0.03f), 0.8f, cc, 1);
        boxW(B.woodCast, wallPoint(w, (o.a0 + o.a1) * 0.5f, o.y1 + t / 2, 0.015f), wallBoxSize(w, o.a1 - o.a0 + 2 * t, t, 0.03f),
             0.8f, cc, g);
    }

    // --- floor (vertex-coloured grid: grime near the walls and under the tables)
    {
        const int nx = 34, nz = 28;
        int base = B.floor.vertexCount();
        for (int j = 0; j <= nz; ++j)
            for (int i = 0; i <= nx; ++i) {
                float x = X0 + (X1 - X0) * i / nx, z = Z0 + (Z1 - Z0) * j / nz;
                float dw = std::min(std::min(x - X0, X1 - x), std::min(z - Z0, Z1 - z));
                float k = 1.f - 0.45f * std::exp(-dw / 0.35f);
                for (const w3d::BgTable& t : w3d::BG_TABLES) {
                    float d2 = (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z);
                    k *= 1.f - 0.18f * std::exp(-d2 / 0.5f);
                }
                k *= 1.f - 0.2f * std::exp(-(x * x + z * z) / 1.2f);                           // under our table
                k *= 1.f + 0.08f * std::exp(-((x - 2.2f) * (x - 2.2f) + (z + 1.f) * (z + 1.f)) / 1.5f);  // çaycı path wear
                k *= 0.92f + 0.12f * fbm(x * 0.8f, z * 0.8f, 3, seed + 77);
                Color c = scaleRgb(WHITE, k);
                B.floor.vertex({x, 0, z}, {0, 1, 0}, {x / 1.6f, z / 1.6f}, c);
            }
        for (int j = 0; j < nz; ++j)
            for (int i = 0; i < nx; ++i) {
                int a = base + j * (nx + 1) + i;
                B.floor.quad(a, a + nx + 1, a + nx + 2, a + 1);
            }
    }
    // --- ceiling (smoke rings above the lamps)
    {
        const int nx = 28, nz = 24;
        int base = B.ceil.vertexCount();
        std::vector<Vector2> lampsXZ{{0, 0}, {3.05f, -3.0f}};
        for (const w3d::BgTable& t : w3d::BG_TABLES) lampsXZ.push_back({t.x, t.z});
        for (int j = 0; j <= nz; ++j)
            for (int i = 0; i <= nx; ++i) {
                float x = X0 + (X1 - X0) * i / nx, z = Z0 + (Z1 - Z0) * j / nz;
                float k = 0.95f;
                for (Vector2 l : lampsXZ) {
                    float d2 = (x - l.x) * (x - l.x) + (z - l.y) * (z - l.y);
                    k *= 1.f - 0.35f * std::exp(-d2 / 0.35f);
                }
                float dw = std::min(std::min(x - X0, X1 - x), std::min(z - Z0, Z1 - z));
                k *= 1.f - 0.3f * std::exp(-dw / 0.3f);
                B.ceil.vertex({x, CY, z}, {0, -1, 0}, {x / 2.f, z / 2.f}, scaleRgb(WHITE, k));
            }
        for (int j = 0; j < nz; ++j)
            for (int i = 0; i < nx; ++i) {
                int a = base + j * (nx + 1) + i;
                B.ceil.quad(a, a + 1, a + nx + 2, a + nx + 1);
            }
        // cornice
        Color cc{214, 196, 160, 255};
        for (int w = 0; w < 4; ++w) {
            WallFrame f = wallFrame(w);
            boxW(B.ceil, wallPoint(w, f.length * 0.5f, CY - 0.05f, 0.05f), wallBoxSize(w, f.length, 0.1f, 0.1f), 1.f, cc, wallGrain(w));
            boxW(B.ceil, wallPoint(w, f.length * 0.5f, CY - 0.115f, 0.02f), wallBoxSize(w, f.length, 0.03f, 0.04f), 1.f, cc, wallGrain(w));
        }
        // beams across the room
        for (float z : {-2.5f, -0.9f, 0.9f, 2.55f})
            boxW(B.woodDark, {0, CY - 0.09f, z}, {X1 - X0 - 0.02f, 0.18f, 0.17f}, 1.1f, Color{120, 90, 64, 255}, 0);
    }
}

// ---------------------------------------------------------------------------- pictures, boards, signs, clock, shelves
void Room::Impl::buildWallDecor(Builders& B) {
    const float AW = ART_W, AH = ART_H;
    auto frame = [&](MeshBuilder& mb, const Deco& d, float t, float depth, Color c) {
        Vector3 n = wallN(d.wall);
        int w = d.wall;
        float hw = d.w * 0.5f + t * 0.5f, hh = d.h * 0.5f + t * 0.5f;
        Vector3 top = decoCenter(Deco{w, d.a, d.y + hh, 0, 0}, depth * 0.5f);
        Vector3 bot = decoCenter(Deco{w, d.a, d.y - hh, 0, 0}, depth * 0.5f);
        Vector3 lef = decoCenter(Deco{w, d.a - hw, d.y, 0, 0}, depth * 0.5f);
        Vector3 rig = decoCenter(Deco{w, d.a + hw, d.y, 0, 0}, depth * 0.5f);
        rbox(mb, top, wallBoxSize(w, d.w + 2 * t, t, depth), t * 0.3f, 2, c);
        rbox(mb, bot, wallBoxSize(w, d.w + 2 * t, t, depth), t * 0.3f, 2, c);
        rbox(mb, lef, wallBoxSize(w, t, d.h, depth), t * 0.3f, 2, c);
        rbox(mb, rig, wallBoxSize(w, t, d.h, depth), t * 0.3f, 2, c);
        (void)n;
    };
    auto pic = [&](MeshBuilder& mb, const Deco& d, Rectangle src, float out) {
        canvasQuad(mb, decoCenter(d, out), d.w, d.h, wallN(d.wall), src, AW, AH);
    };
    // scoreboard: slate in a wooden frame with a chalk ledge
    frame(B.woodCast, D_SCORE, 0.055f, 0.035f, Color{112, 72, 44, 255});
    canvasQuad(B.score, decoCenter(D_SCORE, 0.012f), D_SCORE.w, D_SCORE.h, wallN(0), {0, 0, 1024, 696}, 1024, 696);
    {
        Vector3 ledge = decoCenter(Deco{0, D_SCORE.a, D_SCORE.y - D_SCORE.h * 0.5f - 0.05f, 0, 0}, 0.05f);
        rbox(B.woodCast, ledge, {D_SCORE.w * 0.9f, 0.02f, 0.07f}, 0.006f, 2, Color{120, 78, 48, 255});
        B.paint.capsule({ledge.x - 0.25f, ledge.y + 0.016f, ledge.z + 0.01f}, {ledge.x - 0.17f, ledge.y + 0.016f, ledge.z + 0.015f},
                        0.0055f, 8, Color{240, 238, 228, 255});
        B.paint.capsule({ledge.x + 0.05f, ledge.y + 0.016f, ledge.z}, {ledge.x + 0.09f, ledge.y + 0.016f, ledge.z + 0.004f}, 0.005f,
                        8, Color{230, 226, 200, 255});
        rbox(B.cloth, {ledge.x + 0.26f, ledge.y + 0.028f, ledge.z}, {0.12f, 0.035f, 0.05f}, 0.008f, 2, Color{60, 50, 44, 255});
        rbox(B.woodCast, {ledge.x + 0.26f, ledge.y + 0.052f, ledge.z}, {0.12f, 0.016f, 0.05f}, 0.005f, 2, Color{170, 130, 90, 255});
    }
    // brass picture light over the scoreboard: wall plate, swan-neck arm, half-round hood with a bulb strip
    {
        const float bx = w3d::SCOREBOARD_POS.x, zw = Z0;
        const Color brass{200, 158, 80, 255};
        const float hy = D_SCORE.y + D_SCORE.h * 0.5f + 0.105f, hz = zw + 0.17f;
        B.brass.roundedBox({bx, hy + 0.05f, zw + 0.008f}, {0.09f, 0.05f, 0.016f}, 0.006f, 2, brass);
        B.brass.tube({{bx, hy + 0.05f, zw + 0.012f}, {bx, hy + 0.07f, zw + 0.08f}, {bx, hy + 0.045f, hz - 0.03f}, {bx, hy + 0.028f, hz}},
                     0.0065f, 6, brass);
        // hood: half cylinder along X, open toward the board and down
        MeshBuilder& hb = B.brass;
        const int seg = 10;
        const float r = 0.036f, half = 0.2f;
        for (int k = 0; k <= seg; ++k) {
            float t = PI * 0.15f + PI * 0.95f * k / seg;  // from the front-bottom lip over the top to the back
            Vector3 n{0.f, std::sin(t), std::cos(t)};
            Vector3 p0{bx - half, hy + n.y * r, hz + n.z * r}, p1{bx + half, hy + n.y * r, hz + n.z * r};
            hb.vertex(p0, n, {0.f, (float)k / seg}, brass);
            hb.vertex(p1, n, {1.f, (float)k / seg}, brass);
            if (k > 0) {
                int i = hb.vertexCount() - 4;
                hb.quad(i, i + 2, i + 3, i + 1);
            }
        }
        for (int sx = -1; sx <= 1; sx += 2) {  // end caps
            Vector3 c{bx + sx * half, hy, hz};
            hb.setTransform(MatrixMultiply(MatrixRotateZ(PI * 0.5f), MatrixTranslate(c.x + (sx < 0 ? 0.f : 0.004f), c.y, c.z)));
            hb.cylinder({0, 0, 0}, r * 1.02f, 0.004f, 12, brass);
            hb.resetTransform();
        }
        B.bulbs.capsule({bx - half + 0.03f, hy - 0.004f, hz + 0.004f}, {bx + half - 0.03f, hy - 0.004f, hz + 0.004f}, 0.011f, 8,
                        Color{255, 236, 200, 255});
        boardGlowPos = {bx, hy - 0.015f, hz + 0.01f};
        boardLightPos = {bx, hy - 0.1f, zw + 0.36f};
    }
    frame(B.wood, D_PRICE, 0.04f, 0.03f, Color{96, 60, 36, 255});
    pic(B.artChalk, D_PRICE, art::PRICE, 0.01f);
    // enamel & wooden signs (slightly raised plates)
    auto plate = [&](const Deco& d, Rectangle src, bool gloss) {
        Vector3 c = decoCenter(d, 0.006f);
        B.paint.roundedBox(c, wallBoxSize(d.wall, d.w, d.h, 0.008f), 0.004f, 2, Color{60, 50, 40, 255});
        canvasQuad(gloss ? B.artGloss : B.art, decoCenter(d, 0.0112f), d.w, d.h, wallN(d.wall), src, AW, AH);
    };
    plate(D_KUMAR, art::SIGN_KUMAR, true);
    plate(D_VERESIYE, art::SIGN_VERESIYE, false);
    plate(D_WELCOME, art::SIGN_WELCOME, false);
    // calendar (paper on a nail)
    pic(B.art, D_CALENDAR, art::CALENDAR, 0.004f);
    B.metal.sphere(decoCenter(Deco{0, D_CALENDAR.a, D_CALENDAR.y + D_CALENDAR.h * 0.5f + 0.012f, 0, 0}, 0.006f), 0.005f, 4, 6,
                   Color{120, 110, 100, 255});
    // paintings and photos
    frame(B.brass, D_SHIP, 0.05f, 0.035f, Color{200, 160, 80, 255});
    pic(B.art, D_SHIP, art::SHIP, 0.008f);
    frame(B.brass, D_BOSPHORUS, 0.06f, 0.04f, Color{206, 164, 84, 255});
    pic(B.art, D_BOSPHORUS, art::BOSPHORUS, 0.01f);
    frame(B.varnish, D_TEAM, 0.035f, 0.025f, Color{40, 30, 26, 255});
    pic(B.artGloss, D_TEAM, art::TEAM, 0.008f);
    frame(B.varnish, D_OLDPHOTO, 0.035f, 0.025f, Color{50, 36, 28, 255});
    pic(B.artGloss, D_OLDPHOTO, art::OLDPHOTO, 0.008f);
    frame(B.wood, D_RULES, 0.03f, 0.022f, Color{80, 50, 30, 255});
    pic(B.art, D_RULES, art::RULES, 0.006f);
        frame(B.brass, D_CERT, 0.025f, 0.02f, Color{190, 150, 70, 255});
    pic(B.artGloss, D_CERT, art::CERT, 0.006f);
    pic(B.art, D_POSTER, art::PAPER_TV, 0.003f);
    // mirror with a gilded frame + ornaments
    frame(B.brass, D_MIRROR, 0.07f, 0.05f, Color{214, 172, 86, 255});
    pic(B.artGloss, D_MIRROR, art::MIRROR, 0.01f);
    for (int k = 0; k < 4; ++k) {
        float da = (k % 2 ? 1 : -1) * (D_MIRROR.w * 0.5f + 0.035f), dy = (k < 2 ? 1 : -1) * (D_MIRROR.h * 0.5f + 0.035f);
        B.brass.ellipsoid(wallPoint(1, D_MIRROR.a + da, D_MIRROR.y + dy, 0.05f), {0.05f, 0.05f, 0.05f}, 6, 10, Color{220, 180, 90, 255});
    }
    B.brass.ellipsoid(wallPoint(1, D_MIRROR.a, D_MIRROR.y + D_MIRROR.h * 0.5f + 0.1f, 0.03f), {0.14f, 0.06f, 0.03f}, 6, 12,
                      Color{220, 180, 90, 255});
    // pennant: triangle with a stick and a string to a nail
    {
        const Deco& d = D_PENNANT;
        Vector3 n = wallN(3), r = wallRight(n);
        Vector3 c = decoCenter(d, 0.012f);
        Vector3 pl = Vector3Subtract(c, Vector3Scale(r, d.w * 0.5f));
        Vector3 p0 = Vector3Add(pl, Vector3{0, -d.h * 0.5f, 0}), p1 = Vector3Add(pl, Vector3{0, d.h * 0.5f, 0});
        Vector3 p2 = Vector3Add(Vector3Add(c, Vector3Scale(r, d.w * 0.5f)), Vector3{0, -0.06f, 0});
        Rectangle s = art::PENNANT;
        Vector2 t0{s.x / AW, 1.f - (s.y + s.height) / AH}, t1{s.x / AW, 1.f - s.y / AH},
            t2{(s.x + s.width) / AW, 1.f - (s.y + s.height * 0.5f) / AH};
        Vector3 nn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p2, p0), Vector3Subtract(p1, p0)));
        int a = B.art.vertex(p0, nn, t0), bb = B.art.vertex(p2, nn, t2), cc = B.art.vertex(p1, nn, t1);
        B.art.triangle(a, bb, cc);
        B.varnish.capsule(Vector3Add(p0, Vector3{0, -0.03f, 0}), Vector3Add(p1, Vector3{0, 0.03f, 0}), 0.006f, 6, Color{150, 110, 70, 255});
        Vector3 nail = Vector3Add(Vector3Add(pl, Vector3Scale(r, 0.08f)), Vector3{0, d.h * 0.5f + 0.14f, 0});
        B.paint.tube({Vector3Add(p1, Vector3{0, 0.03f, 0}), nail, Vector3Add(Vector3Add(p1, Vector3Scale(r, 0.16f)), Vector3{0, 0.01f, 0})},
                     0.0015f, 4, Color{200, 190, 170, 255});
        B.metal.sphere(nail, 0.005f, 4, 6, Color{110, 100, 90, 255});
    }
    // trophy shelf with brass cups (right wall)
    {
        Vector3 sc = wallPoint(3, 3.45f, 1.9f, 0.11f);
        rbox(B.woodCast, sc, wallBoxSize(3, 0.9f, 0.03f, 0.22f), 0.008f, 2, Color{110, 70, 42, 255});
        for (int k = 0; k < 2; ++k) {
            Vector3 br = wallPoint(3, 3.15f + k * 0.6f, 1.84f, 0.06f);
            B.woodCast.roundedBox(br, wallBoxSize(3, 0.025f, 0.1f, 0.12f), 0.006f, 1, Color{100, 64, 40, 255});
        }
        float hs[3] = {0.26f, 0.2f, 0.16f};
        for (int k = 0; k < 3; ++k) {
            Vector3 p = wallPoint(3, 3.17f + k * 0.26f, 1.915f, 0.11f);
            float h = hs[k];
            std::vector<Vector2> cup{{0.045f, 0.f},        {0.045f, 0.02f},       {0.018f, 0.03f},      {0.012f, 0.3f * h},
                                     {0.03f, 0.42f * h},   {0.06f, 0.62f * h},   {0.07f, 0.85f * h},   {0.072f, h},
                                     {0.066f, h},          {0.05f, 0.7f * h}};
            latheAt(B.brass, p, cup, 20, true, false, Color{226, 184, 90, 255}, nullptr, 0.f, h / 0.26f + 0.2f);
            for (int s = -1; s <= 1; s += 2) {
                std::vector<Vector3> hp;
                for (int i = 0; i <= 8; ++i) {
                    float t = PI * i / 8.f;
                    hp.push_back({p.x, p.y + (0.55f + 0.2f * std::sin(t)) * h * (h / 0.26f + 0.2f),
                                  p.z + s * (0.06f + 0.035f * std::sin(t)) * (h / 0.26f + 0.2f)});
                }
                B.brass.tube(hp, 0.004f, 6, Color{226, 184, 90, 255});
            }
            B.woodDark.roundedBox({p.x, p.y + 0.012f, p.z}, {0.1f, 0.024f, 0.1f}, 0.004f, 1, Color{40, 30, 24, 255});
        }
    }
    // wall clock: wooden case ring, face, glass rim (hands are animated separately)
    {
        Vector3 c = decoCenter(D_CLOCK, 0.0f);
        clockPos = decoCenter(D_CLOCK, 0.045f);
        float R = D_CLOCK.w * 0.5f;
        Matrix m = MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(c.x, c.y, c.z));  // lathe axis -> +Z
        B.varnish.setTransform(m);
        B.varnish.lathe({{R + 0.035f, 0.0f}, {R + 0.04f, 0.02f}, {R + 0.03f, 0.05f}, {R + 0.005f, 0.055f}, {R - 0.005f, 0.045f}},
                        48, false, false, Color{90, 56, 34, 255});
        B.varnish.resetTransform();
        canvasQuad(B.art, decoCenter(D_CLOCK, 0.03f), D_CLOCK.w * 0.98f, D_CLOCK.h * 0.98f, wallN(0), art::CLOCK, AW, AH);
    }
}

// ---------------------------------------------------------------------------- the tea counter (ocak)
void Room::Impl::buildCounter(Builders& B) {
    const float AW = ART_W, AH = ART_H;
    const float x0 = 2.05f, x1 = 3.95f, zf = w3d::COUNTER_POS.z, zb = Z0, top = 0.98f;
    const float cx = (x0 + x1) * 0.5f;
    // body: wooden carcass, tiled front, dark kick plinth, marble top
    boxW(B.woodCast, {cx, top * 0.5f, (zf + zb) * 0.5f}, {x1 - x0, top, zf - zb - 0.02f}, 0.9f, Color{110, 70, 44, 255}, 0);
    boxW(B.woodDark, {cx, 0.05f, zf - 0.02f}, {x1 - x0 + 0.02f, 0.1f, 0.05f}, 0.9f, Color{60, 44, 32, 255}, 0);
    canvasQuad(B.artGloss, {cx, 0.54f, zf + 0.002f}, x1 - x0 - 0.04f, 0.86f, {0, 0, 1}, art::TILES, AW, AH);
    for (float x : {x0 + 0.01f, x1 - 0.01f})
        rbox(B.woodCast, {x, top * 0.5f, zf - 0.01f}, {0.04f, top, 0.04f}, 0.01f, 2, Color{96, 60, 38, 255});
    boxW(B.marble, {cx, top + 0.02f, (zf + zb) * 0.5f + 0.02f}, {x1 - x0 + 0.08f, 0.04f, zf - zb + 0.06f}, 0.7f,
         Color{236, 234, 228, 255}, 0);
    // splashback tiles behind the counter
    canvasQuad(B.artGloss, {cx, 1.24f, zb + 0.004f}, x1 - x0, 0.46f, {0, 0, 1}, art::TILES, AW, AH, Color{230, 230, 226, 255});
    // wall shelves with brackets
    for (float y : {1.5f, 1.9f}) {
        rbox(B.woodCast, {cx, y, zb + 0.11f}, {x1 - x0, 0.03f, 0.22f}, 0.008f, 2, Color{120, 78, 48, 255});
        for (float x : {x0 + 0.15f, cx, x1 - 0.15f})
            B.woodCast.roundedBox({x, y - 0.06f, zb + 0.07f}, {0.025f, 0.1f, 0.12f}, 0.006f, 1, Color{100, 64, 40, 255});
    }
    okey::Rng rng(seed + 91);
    // shelf 1: soda & gazoz bottles, tea boxes
    std::vector<Vector2> bottle{{0.0f, 0.f},     {0.029f, 0.f},    {0.031f, 0.01f},  {0.031f, 0.13f}, {0.026f, 0.155f},
                                {0.012f, 0.19f}, {0.011f, 0.215f}, {0.013f, 0.222f}, {0.0f, 0.224f}};
    for (int i = 0; i < 9; ++i) {
        float x = x0 + 0.12f + i * 0.068f;
        Vector3 p{x, 1.515f, zb + 0.1f};
        latheAt(B.bottle, p, bottle, 14, false, false, Color{40, 110, 60, 255}, nullptr, 0.f, 0.75f);
        B.metal.cylinder({x, p.y + 0.224f * 0.75f - 0.004f, p.z}, 0.0105f, 0.012f, 10, Color{200, 40, 36, 255});
        canvasQuad(B.art, {x, p.y + 0.06f, p.z + 0.0235f}, 0.04f, 0.035f, {0, 0, 1}, art::SODA_LABEL, AW, AH);
    }
    for (int i = 0; i < 6; ++i) {
        float x = x0 + 0.8f + i * 0.07f;
        Vector3 p{x, 1.515f, zb + 0.1f};
        latheAt(B.bottle, p, bottle, 14, false, false, Color{190, 200, 196, 255}, nullptr, 0.f, 0.85f);
        B.metal.cylinder({x, p.y + 0.224f * 0.85f - 0.004f, p.z}, 0.0115f, 0.012f, 10, Color{220, 180, 60, 255});
        canvasQuad(B.art, {x, p.y + 0.07f, p.z + 0.0267f}, 0.045f, 0.03f, {0, 0, 1}, art::GAZOZ_LABEL, AW, AH);
    }
    for (int i = 0; i < 3; ++i) {
        Vector3 p{x0 + 1.33f + i * 0.14f, 1.515f + 0.09f, zb + 0.1f};
        boxW(B.paint, p, {0.12f, 0.18f, 0.07f}, 0.3f, Color{190, 30, 30, 255});
        canvasQuad(B.art, {p.x, p.y, p.z + 0.0355f}, 0.12f, 0.17f, {0, 0, 1}, art::TEABOX, AW, AH);
    }
    // shelf 2: radio, coffee cups, jars, a spare teapot
    {
        Vector3 rp{x0 + 0.35f, 1.915f + 0.085f, zb + 0.11f};
        rbox(B.woodCast, rp, {0.3f, 0.17f, 0.15f}, 0.02f, 3, Color{110, 66, 36, 255});
        canvasQuad(B.art, {rp.x, rp.y, rp.z + 0.0755f}, 0.27f, 0.137f, {0, 0, 1}, art::RADIO, AW, AH);
        radioDialPos = {rp.x + 0.05f, rp.y + 0.02f, rp.z + 0.08f};
        B.metal.capsule({rp.x + 0.12f, rp.y + 0.085f, rp.z - 0.04f}, {rp.x + 0.02f, rp.y + 0.36f, rp.z - 0.06f}, 0.0025f, 5,
                        Color{180, 180, 180, 255});  // antenna
    }
    std::vector<Vector2> cup{{0.f, 0.f}, {0.022f, 0.f}, {0.026f, 0.01f}, {0.03f, 0.055f}, {0.027f, 0.056f}, {0.023f, 0.012f}, {0.f, 0.012f}};
    for (int i = 0; i < 5; ++i)
        for (int s = 0; s < (i % 2 ? 2 : 3); ++s)
            latheAt(B.ceramic, {x0 + 0.62f + i * 0.075f, 1.93f + s * 0.05f, zb + 0.1f}, cup, 16, false, false, Color{246, 244, 238, 255});
    std::vector<Vector2> jar{{0.f, 0.f}, {0.045f, 0.f}, {0.048f, 0.01f}, {0.048f, 0.15f}, {0.038f, 0.16f}, {0.036f, 0.175f}, {0.f, 0.175f}};
    for (int i = 0; i < 3; ++i) {
        latheAt(B.bottle, {x0 + 1.12f + i * 0.12f, 1.915f, zb + 0.1f}, jar, 16, false, false,
                i == 1 ? Color{200, 150, 60, 255} : Color{170, 150, 110, 255});
        B.metal.cylinder({x0 + 1.12f + i * 0.12f, 1.915f + 0.172f, zb + 0.1f}, 0.04f, 0.02f, 16, Color{190, 40, 30, 255});
    }
    // --- on the counter: gas stove with a double teapot (çaydanlık), second teapot, samovar urn
    const float ct = top + 0.04f;
    boxW(B.paintCast, {2.62f, ct + 0.04f, zf - 0.2f}, {0.62f, 0.08f, 0.34f}, 0.4f, Color{40, 40, 44, 255});
    for (int k = 0; k < 2; ++k) {
        Vector3 bp{2.47f + k * 0.3f, ct + 0.08f, zf - 0.2f};
        ringTube(B.metal, {bp.x, bp.y + 0.012f, bp.z}, 0.07f, 0.008f, 20, Color{60, 60, 64, 255});
        B.metal.cylinder(bp, 0.03f, 0.015f, 14, Color{90, 90, 92, 255});
        for (int s = 0; s < 4; ++s) {
            float a = s * PI * 0.5f + PI * 0.25f;
            B.metal.capsule({bp.x + std::cos(a) * 0.03f, bp.y + 0.02f, bp.z + std::sin(a) * 0.03f},
                            {bp.x + std::cos(a) * 0.09f, bp.y + 0.02f, bp.z + std::sin(a) * 0.09f}, 0.006f, 5, Color{50, 50, 54, 255});
        }
        B.paintCast.capsule({2.47f + k * 0.3f, ct + 0.02f, zf - 0.02f}, {2.47f + k * 0.3f, ct + 0.02f, zf + 0.01f}, 0.012f, 8,
                            Color{30, 30, 30, 255});  // knobs
    }
    flamePos = {2.47f, ct + 0.1f, zf - 0.2f};
    auto teapot = [&](Vector3 p, float s, bool withTop) {
        const Color chrome{214, 216, 220, 255};
        std::vector<Vector2> kettle{{0.f, 0.f},       {0.09f, 0.f},     {0.105f, 0.015f}, {0.11f, 0.06f}, {0.105f, 0.11f},
                                    {0.085f, 0.15f},  {0.055f, 0.17f},  {0.052f, 0.175f}, {0.0f, 0.176f}};
        latheAt(B.metalCast, p, kettle, 28, false, false, chrome, nullptr, 0.f, s);
        // spout and handle
        B.metalCast.tube({{p.x + 0.09f * s, p.y + 0.05f * s, p.z}, {p.x + 0.14f * s, p.y + 0.1f * s, p.z},
                          {p.x + 0.17f * s, p.y + 0.15f * s, p.z}},
                         0.012f * s, 8, chrome);
        std::vector<Vector3> h;
        for (int i = 0; i <= 10; ++i) {
            float t = PI * i / 10.f;
            h.push_back({p.x - (0.09f + 0.07f * std::sin(t)) * s, p.y + (0.03f + 0.12f * (1 - std::cos(t)) * 0.5f + 0.02f) * s, p.z});
        }
        B.metalCast.tube(h, 0.009f * s, 6, Color{40, 36, 34, 255});
        if (withTop) {
            Vector3 q{p.x, p.y + 0.176f * s, p.z};
            std::vector<Vector2> pot{{0.f, 0.f},      {0.05f, 0.f},     {0.066f, 0.02f}, {0.072f, 0.055f}, {0.064f, 0.09f},
                                     {0.045f, 0.105f}, {0.043f, 0.112f}, {0.02f, 0.118f}, {0.012f, 0.13f},  {0.0f, 0.134f}};
            latheAt(B.metalCast, q, pot, 24, false, false, chrome, nullptr, 0.f, s);
            B.metalCast.tube({{q.x + 0.06f * s, q.y + 0.03f * s, q.z}, {q.x + 0.1f * s, q.y + 0.065f * s, q.z},
                              {q.x + 0.12f * s, q.y + 0.1f * s, q.z}},
                             0.008f * s, 6, chrome);
            std::vector<Vector3> h2;
            for (int i = 0; i <= 8; ++i) {
                float t = PI * i / 8.f;
                h2.push_back({q.x - (0.062f + 0.045f * std::sin(t)) * s, q.y + (0.02f + 0.07f * (1 - std::cos(t)) * 0.5f) * s, q.z});
            }
            B.metalCast.tube(h2, 0.006f * s, 6, Color{40, 36, 34, 255});
        }
    };
    teapot({2.47f, ct + 0.095f, zf - 0.2f}, 1.f, true);
    teapot({2.77f, ct + 0.095f, zf - 0.2f}, 0.85f, true);
    emitters.push_back({{2.47f + 0.12f, ct + 0.095f + 0.176f + 0.1f, zf - 0.2f}, 5.f, 0.f, 0});
    emitters.push_back({{2.47f, ct + 0.095f + 0.176f + 0.14f, zf - 0.2f}, 2.2f, 0.f, 0});
    emitters.push_back({{2.77f + 0.14f, ct + 0.095f + 0.15f + 0.09f, zf - 0.2f}, 1.6f, 0.f, 0});
    // samovar urn (brass) with a tap
    {
        Vector3 p{3.74f, ct, zf - 0.2f};
        std::vector<Vector2> urn{{0.f, 0.f},      {0.08f, 0.f},    {0.07f, 0.03f},  {0.075f, 0.06f}, {0.12f, 0.1f},
                                 {0.13f, 0.2f},   {0.12f, 0.3f},   {0.09f, 0.34f},  {0.06f, 0.36f},  {0.065f, 0.38f},
                                 {0.03f, 0.4f},   {0.012f, 0.44f}, {0.0f, 0.45f}};
        latheAt(B.brass, p, urn, 32, false, false, Color{214, 160, 80, 255});
        B.brass.tube({{p.x - 0.12f, p.y + 0.12f, p.z + 0.03f}, {p.x - 0.17f, p.y + 0.11f, p.z + 0.05f}, {p.x - 0.18f, p.y + 0.08f, p.z + 0.06f}},
                     0.01f, 8, Color{214, 160, 80, 255});
        for (int s = -1; s <= 1; s += 2)
            B.brass.tube({{p.x, p.y + 0.3f, p.z + s * 0.12f}, {p.x, p.y + 0.36f, p.z + s * 0.16f}, {p.x, p.y + 0.3f, p.z + s * 0.17f}},
                         0.008f, 6, Color{160, 110, 50, 255});
    }
    // glasses tray: rows of ince belli glasses on saucers, ready to be filled
    {
        Vector3 tc{3.22f, ct, zf - 0.17f};
        latheAt(B.metalCast, tc, {{0.f, 0.f}, {0.2f, 0.f}, {0.215f, 0.012f}, {0.21f, 0.014f}, {0.195f, 0.004f}, {0.f, 0.004f}}, 32,
                false, false, Color{196, 190, 170, 255}, nullptr, 0.f, 1.f);
        MeshBuilder gl;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) {
                Vector3 p{tc.x - 0.12f + c * 0.08f + (r % 2) * 0.04f, ct + 0.004f, tc.z - 0.1f + r * 0.085f};
                if (std::hypot(p.x - tc.x, p.z - tc.z) > 0.16f) continue;
                latheAt(B.ceramicCast, p, kSaucer, 20, false, false, Color{246, 244, 238, 255}, nullptr, 0.f, 0.8f);
                latheAt(gl, {p.x - tc.x, p.y + 0.005f, p.z - tc.z}, kGlassProfile, 20, true, false, WHITE);
            }
        glassSets.push_back({gl.build(true), {tc.x, 0.f, tc.z}});
        // sugar bowl + spoon glass
        std::vector<Vector2> bowl{{0.f, 0.f}, {0.03f, 0.f}, {0.045f, 0.02f}, {0.05f, 0.05f}, {0.046f, 0.052f}, {0.04f, 0.024f}, {0.f, 0.02f}};
        latheAt(B.ceramicCast, {3.47f, ct, zf - 0.08f}, bowl, 20, false, false, Color{240, 236, 226, 255});
        for (int k = 0; k < 7; ++k)
            B.ceramic.roundedBox({3.47f + rng.uniform(-0.025f, 0.025f), ct + 0.045f + rng.uniform(0.f, 0.01f), zf - 0.08f + rng.uniform(-0.025f, 0.025f)},
                                 {0.014f, 0.012f, 0.014f}, 0.002f, 1, Color{250, 250, 246, 255});
    }
}

// ---------------------------------------------------------------------------- background tables, chairs, props
void Room::Impl::buildBgTables(Builders& B) {
    const float AW = ART_W, AH = ART_H;
    okey::Rng rng(seed + 17);
    const float TY = w3d::BG_TABLE_Y;
    for (int ti = 0; ti < 4; ++ti) {
        const w3d::BgTable& t = w3d::BG_TABLES[ti];
        const Vector3 c{t.x, 0.f, t.z};
        const Color topC = scaleRgb(Color{150, 98, 60, 255}, rng.uniform(0.9f, 1.05f));
        // table: top, apron, legs, stretchers
        rbox(B.woodCast, {t.x, TY - 0.0175f, t.z}, {0.9f, 0.035f, 0.9f}, 0.012f, 2, topC);
        for (int s = 0; s < 4; ++s) {
            Vector3 p = Vector3Add(c, Vector3Scale(w3d::SEAT_DIR[s], 0.39f));
            Vector3 sz = std::fabs(w3d::SEAT_DIR[s].x) > 0.5f ? Vector3{0.022f, 0.08f, 0.76f} : Vector3{0.76f, 0.08f, 0.022f};
            rbox(B.woodCast, {p.x, TY - 0.075f, p.z}, sz, 0.005f, 1, scaleRgb(topC, 0.85f));
        }
        for (int k = 0; k < 4; ++k) {
            float lx = (k % 2 ? 1 : -1) * 0.39f, lz = (k < 2 ? 1 : -1) * 0.39f;
            rbox(B.woodCast, {t.x + lx, (TY - 0.035f) * 0.5f, t.z + lz}, {0.048f, TY - 0.035f, 0.048f}, 0.008f, 2, scaleRgb(topC, 0.8f));
        }
        const bool feltTop = t.kind == 1 || t.kind == 2;
        if (feltTop) boxW(B.felt, {t.x, TY + 0.0015f, t.z}, {0.8f, 0.003f, 0.8f}, 0.8f, WHITE, 0);
        const float PY = TY + (feltTop ? 0.003f : 0.f);

        // chairs at all four sides (only empty ones get pulled out / turned a little)
        for (int s = 0; s < 4; ++s) {
            Chair ch;
            ch.occupied = (t.sides >> s) & 1u;
            ch.pos = Vector3Add(c, Vector3Scale(w3d::SEAT_DIR[s], w3d::BG_CHAIR_DIST));
            ch.yaw = w3d::seatYawDeg(s);
            if (!ch.occupied) {
                ch.pos = Vector3Add(ch.pos, Vector3Scale(w3d::SEAT_DIR[s], rng.uniform(0.02f, 0.1f)));
                ch.pos = Vector3Add(ch.pos, Vector3Scale(w3d::SEAT_RIGHT[s], rng.uniform(-0.06f, 0.06f)));
                ch.yaw += rng.uniform(-9.f, 9.f);
            }
            chairs.push_back(ch);
        }

        // props
        MeshBuilder glasses;
        auto sidePt = [&](int s, float out, float right, float y) {
            return Vector3{t.x + w3d::SEAT_DIR[s].x * out + w3d::SEAT_RIGHT[s].x * right, y,
                           t.z + w3d::SEAT_DIR[s].z * out + w3d::SEAT_RIGHT[s].z * right};
        };
        for (int s = 0; s < 4; ++s)
            if ((t.sides >> s) & 1u)
                glassOnSaucer(B, glasses, c, sidePt(s, 0.3f, 0.24f, PY), rng.uniform(0.045f, 0.078f), false, rng);
        auto ashtray = [&](Vector3 p, int butts, bool burning) {
            latheAt(B.metalCast, p, kTinAshtray, 20, false, false, Color{178, 176, 168, 255});
            B.paint.cylinder({p.x, p.y + 0.005f, p.z}, 0.04f, 0.003f, 16, Color{110, 106, 100, 255});
            for (int k = 0; k < butts; ++k) {
                float a = rng.uniform(0.f, 2.f * PI);
                butt(B.paint, {p.x + std::cos(a) * 0.02f, p.y + 0.011f, p.z + std::sin(a) * 0.02f}, rng.uniform(0.f, 360.f),
                     0.028f, rng.chance(0.4f));
            }
            if (burning) {
                Vector3 e{p.x - 0.03f, p.y + 0.02f, p.z};
                B.paint.capsule({p.x + 0.035f, p.y + 0.022f, p.z}, e, 0.0038f, 8, Color{240, 236, 228, 255});
                emitters.push_back({e, 3.f, 0.f, 2});
            }
        };

        if (t.kind == 0) {
            // --- tavla: open board with checkers and dice
            tavlaCenter = {t.x, PY, t.z};
            const float bw = 0.52f, bd = 0.42f, bh = 0.028f;
            Color bc{120, 76, 42, 255};
            boxW(B.woodCast, {t.x, PY + 0.004f, t.z}, {bw, 0.008f, bd}, 0.5f, bc, 0);
            for (int k = 0; k < 2; ++k) {
                float zz = t.z + (k ? 1 : -1) * (bd * 0.5f - 0.012f);
                rbox(B.woodCast, {t.x, PY + bh * 0.5f, zz}, {bw, bh, 0.024f}, 0.006f, 2, scaleRgb(bc, 1.1f));
            }
            for (int k = 0; k < 3; ++k) {
                float xx = t.x + (k - 1) * (bw * 0.5f - 0.012f);
                rbox(B.woodCast, {xx, PY + bh * 0.5f, t.z}, {0.024f, bh, bd}, 0.006f, 2, scaleRgb(bc, k == 1 ? 0.8f : 1.1f));
            }
            canvasQuadUp(B.art, {t.x, PY + 0.0085f, t.z}, bw - 0.03f, bd - 0.03f, art::TAVLA, AW, AH);
            // checkers: (half, point, far?, count, dark?)
            struct St {
                int half, pt;
                bool far;
                int n;
                bool dark;
            };
            const St st[] = {{0, 0, true, 2, false}, {0, 5, true, 5, true},   {1, 1, true, 3, true},   {1, 4, true, 1, false},
                             {1, 0, false, 5, false}, {1, 5, false, 2, true}, {0, 2, false, 3, false}, {0, 4, false, 4, true},
                             {0, 3, true, 4, false}, {1, 3, false, 1, true}};
            const float hw = (bw - 0.03f) * 0.5f - 0.013f;  // half width (minus bar)
            for (const St& s : st)
                for (int i = 0; i < s.n; ++i) {
                    float x = t.x + (s.half ? 0.013f : -0.013f - hw) + (s.pt + 0.5f) * hw / 6.f;
                    float z = t.z + (s.far ? -1 : 1) * ((bd - 0.03f) * 0.5f - 0.017f - i * 0.031f);
                    Color cc = s.dark ? Color{80, 30, 26, 255} : Color{238, 228, 204, 255};
                    B.ceramicCast.cylinder({x, PY + 0.009f, z}, 0.0152f, 0.007f, 18, cc);
                    ringTube(B.ceramicCast, {x, PY + 0.016f, z}, 0.009f, 0.0012f, 12, scaleRgb(cc, 0.8f));
                }
            ashtray(sidePt(1, 0.3f, 0.1f, PY), 4, false);
            // closed cigarette pack and lighter
            boxW(B.paint, sidePt(3, 0.3f, -0.1f, PY + 0.011f), {0.055f, 0.022f, 0.088f}, 0.2f, Color{244, 240, 232, 255});
            canvasQuadUp(B.art, sidePt(3, 0.3f, -0.1f, PY + 0.0222f), 0.052f, 0.085f, art::CIGPACK, AW, AH, 90.f);
        } else if (t.kind == 1) {
            // --- okey: four racks with tiles, centre pile, indicator, discards
            for (int s = 0; s < 4; ++s) {
                float yaw = w3d::seatYawDeg(s);
                Vector3 rc = sidePt(s, 0.335f, 0.f, PY);
                Matrix rm = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(rc.x, rc.y, rc.z));
                B.woodCast.setTransform(rm);
                B.woodCast.roundedBox({0, 0.006f, 0}, {0.42f, 0.012f, 0.06f}, 0.004f, 1, Color{150, 100, 60, 255});
                B.woodCast.roundedBox({0, 0.02f, -0.022f}, {0.42f, 0.03f, 0.016f}, 0.004f, 1, Color{140, 92, 54, 255});
                B.woodCast.resetTransform();
                int n = 12 + rng.range(0, 3);
                for (int i = 0; i < n; ++i) {
                    Matrix tm = MatrixMultiply(MatrixMultiply(MatrixRotateX(-14.f * DEG2RAD), MatrixTranslate((i - (n - 1) * 0.5f) * 0.0265f, 0.029f, -0.002f)), rm);
                    B.ceramicCast.setTransform(tm);
                    B.ceramicCast.roundedBox({0, 0, 0}, {0.024f, 0.034f, 0.009f}, 0.002f, 1, Color{240, 232, 208, 255});
                    B.ceramicCast.resetTransform();
                    B.art.setTransform(tm);
                    int f = rng.range(0, 7);
                    Rectangle src{art::TILEFACE.x + (f % 4) * 60.f + 4, art::TILEFACE.y + (f / 4) * 60.f + 4, 52, 52};
                    canvasQuad(B.art, {0, 0, 0.0047f}, 0.021f, 0.03f, {0, 0, 1}, src, AW, AH);
                    B.art.resetTransform();
                }
            }
            for (int k = 0; k < 2; ++k)
                for (int j = 0; j < 4; ++j)
                    B.ceramicCast.roundedBox({t.x - 0.03f + k * 0.032f, PY + 0.0045f + j * 0.0092f, t.z}, {0.024f, 0.009f, 0.034f}, 0.002f,
                                             1, Color{240, 232, 208, 255});
            B.ceramicCast.roundedBox({t.x + 0.06f, PY + 0.0045f, t.z}, {0.024f, 0.009f, 0.034f}, 0.002f, 1, Color{240, 232, 208, 255});
            canvasQuadUp(B.art, {t.x + 0.06f, PY + 0.0091f, t.z}, 0.021f, 0.03f, {art::TILEFACE.x + 64, art::TILEFACE.y + 4, 52, 52}, AW, AH);
            for (int s = 0; s < 4; ++s)
                for (int k = 0; k < 1 + rng.range(0, 2); ++k) {
                    Vector3 p = sidePt(s, 0.2f, -0.2f, PY + 0.0045f + k * 0.0092f);
                    float yaw = w3d::seatYawDeg(s) + rng.uniform(-10.f, 10.f);
                    B.ceramicCast.setTransform(MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(p.x, p.y, p.z)));
                    B.ceramicCast.roundedBox({0, 0, 0}, {0.024f, 0.009f, 0.034f}, 0.002f, 1, Color{240, 232, 208, 255});
                    B.ceramicCast.resetTransform();
                    int f = rng.range(0, 7);
                    canvasQuadUp(B.art, {p.x, p.y + 0.0046f, p.z}, 0.021f, 0.03f,
                                 {art::TILEFACE.x + (f % 4) * 60.f + 4, art::TILEFACE.y + (f / 4) * 60.f + 4, 52, 52}, AW, AH, yaw + 180.f);
                }
            ashtray({t.x + 0.3f, PY, t.z + 0.3f}, 6, false);
        } else if (t.kind == 2) {
            // --- iskambil: deck, face-up cards, piles in front of the players, score pad
            boxW(B.paint, {t.x, PY + 0.009f, t.z}, {0.063f, 0.018f, 0.088f}, 0.2f, Color{240, 238, 230, 255});
            canvasQuadUp(B.art, {t.x, PY + 0.0182f, t.z}, 0.063f, 0.088f, art::CARD_BACK, AW, AH, 20.f);
            for (int k = 0; k < 3; ++k) {
                Rectangle src{art::CARD_FACE.x + k * 104.f, art::CARD_FACE.y, 96, 140};
                canvasQuadUp(B.art, {t.x + 0.1f + k * 0.03f, PY + 0.0005f + k * 0.0004f, t.z + 0.05f - k * 0.01f}, 0.063f, 0.088f, src, AW, AH,
                             -20.f + k * 18.f);
            }
            for (int s = 0; s < 4; ++s) {
                if (!((t.sides >> s) & 1u)) continue;
                Vector3 p = sidePt(s, 0.25f, -0.05f, PY);
                float yaw = w3d::seatYawDeg(s) + rng.uniform(-12.f, 12.f);
                B.paint.setTransform(MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(p.x, p.y + 0.003f, p.z)));
                B.paint.box({0, 0, 0}, {0.063f, 0.006f, 0.088f}, Color{240, 238, 230, 255});
                B.paint.resetTransform();
                canvasQuadUp(B.art, {p.x, p.y + 0.0062f, p.z}, 0.063f, 0.088f, art::CARD_BACK, AW, AH, yaw);
            }
            Vector3 sp = sidePt(3, 0.26f, 0.05f, PY + 0.0015f);
            canvasQuadUp(B.art, sp, 0.1f, 0.13f, art::SCOREPAD, AW, AH, 270.f + 8.f);
            B.paint.capsule({sp.x + 0.02f, sp.y + 0.004f, sp.z - 0.07f}, {sp.x + 0.03f, sp.y + 0.004f, sp.z + 0.08f}, 0.0038f, 6,
                            Color{230, 190, 40, 255});
            ashtray(sidePt(2, 0.25f, 0.28f, PY), 5, true);
        } else {
            // --- tea & chat: newspaper, cigarettes, sugar bowl
            Vector3 np = sidePt(0, 0.18f, 0.05f, PY + 0.001f);
            canvasQuadUp(B.art, np, 0.27f, 0.36f, art::NEWS, AW, AH, 14.f);
            std::vector<Vector2> bowl{{0.f, 0.f}, {0.03f, 0.f}, {0.045f, 0.02f}, {0.05f, 0.05f}, {0.046f, 0.052f}, {0.04f, 0.024f}, {0.f, 0.02f}};
            latheAt(B.ceramicCast, {t.x - 0.05f, PY, t.z - 0.12f}, bowl, 20, false, false, Color{240, 236, 226, 255});
            boxW(B.paint, sidePt(2, 0.2f, 0.22f, PY + 0.011f), {0.088f, 0.022f, 0.055f}, 0.2f, Color{244, 240, 232, 255});
            canvasQuadUp(B.art, sidePt(2, 0.2f, 0.22f, PY + 0.0222f), 0.052f, 0.085f, art::CIGPACK, AW, AH, 90.f);
            B.paint.roundedBox(sidePt(2, 0.2f, 0.12f, PY + 0.006f), {0.024f, 0.012f, 0.07f}, 0.004f, 1, Color{40, 90, 170, 255});
            ashtray(sidePt(3, 0.26f, 0.18f, PY), 3, false);
        }
        glassSets.push_back({glasses.build(true), c});
    }
}

// ---------------------------------------------------------------------------- lamps (meshes + placement)
void Room::Impl::buildLamps(Builders& B) {
    {
        MeshBuilder o, in, bl, cd;
        std::vector<Vector2> prof{{0.225f, -0.075f}, {0.205f, -0.05f}, {0.16f, -0.012f}, {0.1f, 0.035f}, {0.058f, 0.075f},
                                  {0.04f, 0.1f},     {0.034f, 0.12f}};
        o.lathe(prof, 36, false, false, Color{44, 82, 62, 255});
        ringTube(o, {0, -0.075f, 0}, 0.225f, 0.0055f, 36, Color{30, 36, 32, 255});
        o.cylinder({0, 0.115f, 0}, 0.026f, 0.05f, 16, Color{50, 46, 42, 255});
        std::vector<Vector2> inner;
        for (auto it = prof.rbegin(); it != prof.rend(); ++it) inner.push_back({it->x - 0.003f, it->y});
        in.lathe(inner, 36, false, false, WHITE);
        bl.lathe({{0.f, -0.058f}, {0.016f, -0.054f}, {0.03f, -0.036f}, {0.034f, -0.012f}, {0.028f, 0.012f}, {0.017f, 0.028f},
                  {0.0145f, 0.042f}, {0.0f, 0.043f}},
                 20, false, false, WHITE);
        in.cylinder({0, 0.04f, 0}, 0.018f, 0.05f, 12, Color{160, 150, 130, 255});  // socket (lit from inside)
        cd.cylinder({0, 0, 0}, 0.0042f, 1.f, 6, Color{36, 30, 26, 255});
        shadeOut = o.build(true);
        shadeIn = in.build(true);
        bulbMesh = bl.build(true);
        cordMesh = cd.build(true);
    }
    auto addLamp = [&](Vector3 bulb, bool key, bool small, Color col, float inten, float range) {
        Lamp L;
        L.bulb = bulb;
        L.anchor = {bulb.x, CY, bulb.z};
        L.key = key;
        L.small = small;
        L.color = col;
        L.intensity = inten;
        L.range = range;
        L.phase = rng.uniform(0.f, 50.f);
        L.swingAmp = key ? 0.08f : rng.uniform(0.35f, 0.7f);
        L.cur = bulb;
        lamps.push_back(L);
        // ceiling rose
        latheAt(B.paint, {bulb.x, CY - 0.035f, bulb.z}, {{0.0f, 0.0f}, {0.03f, 0.004f}, {0.05f, 0.02f}, {0.055f, 0.035f}}, 16, false,
                false, Color{200, 190, 170, 255});
    };
    addLamp(w3d::TABLE_LAMP, true, false, Color{255, 204, 146, 255}, 1.f, 6.f);
    for (int i = 0; i < 4; ++i) {
        const w3d::BgTable& t = w3d::BG_TABLES[i];
        addLamp({t.x, 1.93f, t.z}, false, false, Color{255, 184, 112, 255}, 1.0f, 3.3f);
    }
    lamps[2].faulty = 1.f;  // the okey table's bulb stutters now and then
    addLamp({3.05f, 2.18f, -3.02f}, false, true, Color{255, 190, 120, 255}, 0.95f, 3.0f);
    // light shafts (camera-facing quads, local origin at the shade rim, +Y up)
    for (const Lamp& L : lamps) {
        float s = L.small ? 0.7f : 1.f;
        float rimY = L.bulb.y - 0.075f * s;
        float bottom = L.key ? 1.52f : (L.small ? 1.0f : w3d::BG_TABLE_Y);  // our table's air stays clear (y < 1.5)
        float h = rimY - bottom;
        float wTop = 0.38f * s, wBot = wTop + 2.f * h * std::tan(52.f * DEG2RAD);
        MeshBuilder q;
        int a = q.vertex({-wBot * 0.5f, -h, 0}, {0, 0, 1}, {0, 1});
        int b = q.vertex({wBot * 0.5f, -h, 0}, {0, 0, 1}, {1, 1});
        int c = q.vertex({wTop * 0.5f, 0, 0}, {0, 0, 1}, {1, 0});
        int d = q.vertex({-wTop * 0.5f, 0, 0}, {0, 0, 1}, {0, 0});
        q.quad(a, b, c, d);
        dustMeshes.push_back(q.build());
    }
    // motes drifting in the cones
    for (int li = 0; li < (int)lamps.size(); ++li) {
        int n = lamps[li].key ? 22 : (lamps[li].small ? 6 : 12);
        for (int k = 0; k < n; ++k) {
            Mote m;
            m.lamp = li;
            m.p = {lamps[li].bulb.x + rng.uniform(-0.3f, 0.3f), lamps[li].bulb.y - rng.uniform(0.15f, 0.7f),
                   lamps[li].bulb.z + rng.uniform(-0.3f, 0.3f)};
            m.v = {rng.uniform(-0.01f, 0.01f), rng.uniform(-0.004f, 0.006f), rng.uniform(-0.01f, 0.01f)};
            m.tw = rng.uniform(0.f, 10.f);
            motes.push_back(m);
        }
    }
}

// ---------------------------------------------------------------------------- stove, TV, fan, coat rack, WC door, clock hands
void Room::Impl::buildStoveTvFan(Builders& B) {
    const float AW = ART_W, AH = ART_H;
    // --- cast-iron stove (soba) on a zinc tray in the front-right corner (clear of the çaycı's aisle along
    //     the right wall), its door turned toward the room; pipe straight up and into the right wall
    {
        const Vector3 p{3.8f, 0.f, 2.55f};
        const float doorYaw = -34.f;  // local -X (the door) faces the middle of the room
        const Matrix D = MatrixMultiply(MatrixRotateY(doorYaw * DEG2RAD), MatrixTranslate(p.x, 0.f, p.z));
        auto at = [&](Vector3 l) { return Vector3Transform(l, D); };
        B.metal.setTransform(D);
        B.metal.box({-0.05f, 0.004f, 0.f}, {0.72f, 0.008f, 0.64f}, Color{120, 120, 118, 255});
        B.metal.resetTransform();
        for (int k = 0; k < 4; ++k) {
            float a = PI * 0.25f + k * PI * 0.5f;
            B.paintCast.capsule({p.x + std::cos(a) * 0.15f, 0.01f, p.z + std::sin(a) * 0.15f},
                                {p.x + std::cos(a) * 0.13f, 0.13f, p.z + std::sin(a) * 0.13f}, 0.014f, 6, Color{30, 28, 28, 255});
        }
        std::vector<Vector2> body{{0.f, 0.12f},  {0.19f, 0.12f}, {0.2f, 0.14f},  {0.2f, 0.66f},  {0.215f, 0.68f},
                                  {0.215f, 0.7f}, {0.17f, 0.72f}, {0.12f, 0.73f}, {0.0f, 0.735f}};
        latheAt(B.paintCast, p, body, 32, false, false, Color{38, 36, 36, 255});
        ringTube(B.paintCast, {p.x, 0.3f, p.z}, 0.203f, 0.008f, 32, Color{60, 56, 52, 255});
        ringTube(B.paintCast, {p.x, 0.52f, p.z}, 0.203f, 0.008f, 32, Color{60, 56, 52, 255});
        // door with a glowing slot and a brass handle
        B.paintCast.setTransform(D);
        B.paintCast.roundedBox({-0.21f, 0.38f, 0.f}, {0.03f, 0.17f, 0.2f}, 0.01f, 2, Color{50, 46, 44, 255});
        B.paintCast.resetTransform();
        B.bulbs.setTransform(D);
        B.bulbs.box({-0.227f, 0.335f, 0.f}, {0.004f, 0.025f, 0.13f}, Color{255, 120, 40, 255});
        B.bulbs.resetTransform();
        B.brass.capsule(at({-0.23f, 0.42f, 0.06f}), at({-0.25f, 0.42f, 0.06f}), 0.008f, 6, Color{190, 150, 80, 255});
        stoveGlowPos = at({-0.26f, 0.34f, 0.f});
        // pipe: straight up, elbow, into the wall
        std::vector<Vector3> pipe{{p.x, 0.73f, p.z}, {p.x, 2.3f, p.z}, {p.x + 0.03f, 2.4f, p.z}, {p.x + 0.12f, 2.45f, p.z}, {X1 + 0.02f, 2.45f, p.z}};
        B.paintCast.tube(pipe, 0.062f, 18, Color{52, 50, 50, 255});
        for (float y : {1.2f, 1.8f}) ringTube(B.paintCast, {p.x, y, p.z}, 0.064f, 0.006f, 18, Color{70, 66, 62, 255});
        B.paint.setTransform(MatrixMultiply(MatrixRotateZ(PI * 0.5f), MatrixTranslate(X1 - 0.002f, 2.45f, p.z)));  // wall rosette
        B.paint.lathe({{0.1f, 0.f}, {0.1f, 0.006f}, {0.068f, 0.012f}, {0.062f, 0.012f}}, 18, false, false, Color{60, 56, 52, 255});
        B.paint.resetTransform();
        // a copper kettle warming on top (güğüm)
        std::vector<Vector2> kettle{{0.f, 0.f}, {0.07f, 0.f}, {0.085f, 0.04f}, {0.08f, 0.11f}, {0.05f, 0.15f}, {0.035f, 0.2f}, {0.04f, 0.21f}, {0.f, 0.21f}};
        latheAt(B.brass, at({0.02f, 0.735f, -0.1f}), kettle, 20, false, false, Color{190, 100, 60, 255}, nullptr, 0.f, 0.8f);
    }
    // --- CRT television on a corner bracket, back-left corner, facing the room
    {
        const Vector3 c{-3.72f, 2.56f, -2.93f};  // platform clears the back window's head
        const float yaw = std::atan2(0.5f - c.x, 0.4f - c.z) * RAD2DEG;
        tvXf = MatrixMultiply(MatrixMultiply(MatrixRotateX(9.f * DEG2RAD), MatrixRotateY(yaw * DEG2RAD)), MatrixTranslate(c.x, c.y, c.z));
        Matrix nx = MatrixMultiply(MatrixRotateX(9.f * DEG2RAD), MatrixRotateY(yaw * DEG2RAD));
        tvNormal = Vector3Transform({0, 0, 1}, nx);
        tvFront = Vector3Add(c, Vector3Scale(tvNormal, 0.45f));
        B.paintCast.setTransform(tvXf);
        const Color caseC{58, 44, 36, 255}, bezel{28, 26, 26, 255};
        B.paintCast.roundedBox({0, 0, 0}, {0.6f, 0.46f, 0.36f}, 0.04f, 3, caseC);
        B.paintCast.roundedBox({0, 0.0f, -0.28f}, {0.42f, 0.34f, 0.22f}, 0.06f, 2, scaleRgb(caseC, 0.9f));
        B.paintCast.roundedBox({-0.06f, 0.0f, 0.176f}, {0.44f, 0.36f, 0.02f}, 0.03f, 2, bezel);
        B.paintCast.roundedBox({0.22f, 0.0f, 0.176f}, {0.1f, 0.38f, 0.016f}, 0.01f, 1, Color{40, 36, 34, 255});
        B.paintCast.resetTransform();
        B.metal.setTransform(tvXf);
        for (int k = 0; k < 2; ++k) {
            Matrix km = MatrixMultiply(MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(0.22f, 0.1f - k * 0.09f, 0.184f)), tvXf);
            B.metal.setTransform(km);
            B.metal.lathe({{0.0f, 0.0f}, {0.02f, 0.0f}, {0.02f, 0.018f}, {0.016f, 0.024f}, {0.0f, 0.024f}}, 14, false, false, Color{170, 170, 166, 255});
        }
        B.metal.setTransform(tvXf);
        for (int k = 0; k < 7; ++k) B.metal.box({0.22f, -0.08f - k * 0.012f, 0.186f}, {0.07f, 0.004f, 0.004f}, Color{30, 30, 30, 255});
        // rabbit-ear antenna
        B.metal.ellipsoid({0.02f, 0.235f, -0.05f}, {0.05f, 0.02f, 0.04f}, 6, 10, Color{40, 40, 40, 255});
        B.metal.capsule({0.02f, 0.24f, -0.05f}, {-0.17f, 0.5f, -0.12f}, 0.0035f, 6, Color{200, 200, 200, 255});
        B.metal.capsule({0.02f, 0.24f, -0.05f}, {0.22f, 0.47f, -0.02f}, 0.0035f, 6, Color{200, 200, 200, 255});
        B.metal.resetTransform();
        // screen (canvas, slightly curved look is painted)
        B.tv.setTransform(tvXf);
        canvasQuad(B.tv, {-0.06f, 0.0f, 0.1875f}, 0.38f, 0.29f, {0, 0, 1}, {0, 0, 320, 240}, 320, 240);
        B.tv.resetTransform();
        // bracket: platform + struts into both walls
        Vector3 pl{c.x, c.y - 0.245f, c.z};
        B.paintCast.setTransform(MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(pl.x, pl.y, pl.z)));
        B.paintCast.roundedBox({0, 0, -0.02f}, {0.56f, 0.02f, 0.42f}, 0.005f, 1, Color{40, 40, 42, 255});
        B.paintCast.resetTransform();
        // angle brackets: a flat arm from each rear corner of the platform into its wall, a diagonal brace
        // down to a screwed wall plate
        {
            const float cs = std::cos(yaw * DEG2RAD), sn = std::sin(yaw * DEG2RAD);
            auto L2W = [&](float lx, float lz, float y) { return Vector3{pl.x + lx * cs + lz * sn, y, pl.z - lx * sn + lz * cs}; };
            const Color iron{38, 38, 40, 255};
            const float yu = pl.y - 0.016f, yb = pl.y - 0.34f;
            // back wall (z = Z0) from the rear-right corner, left wall (x = X0) from the rear-left corner
            Vector3 ca = L2W(0.26f, -0.2f, yu), cb = L2W(-0.26f, -0.2f, yu);
            Vector3 wa{ca.x, yu, Z0 + 0.012f}, wb{X0 + 0.012f, yu, cb.z};  // arm ends at the walls
            Vector3 ma = L2W(0.2f, 0.02f, yu), mb = L2W(-0.2f, 0.02f, yu);
            B.paintCast.box({(ca.x + wa.x) * 0.5f, yu, (ca.z + wa.z) * 0.5f}, {0.03f, 0.016f, std::fabs(ca.z - wa.z) + 0.03f}, iron);
            B.paintCast.box({(cb.x + wb.x) * 0.5f, yu, (cb.z + wb.z) * 0.5f}, {std::fabs(cb.x - wb.x) + 0.03f, 0.016f, 0.03f}, iron);
            // only the left wall gets a diagonal brace: the back wall has the window right below
            (void)ma;
            B.paintCast.tube({{mb.x, yu - 0.008f, mb.z}, {X0 + 0.014f, yb, mb.z}}, 0.009f, 6, iron);
            B.paintCast.box({X0 + 0.006f, (yu + yb) * 0.5f, mb.z}, {0.012f, yu - yb + 0.06f, 0.05f}, iron);
            Vector3 mc = L2W(-0.05f, -0.2f, yu);  // second arm from the rear edge into the corner
            B.paintCast.box({(mc.x + X0) * 0.5f, yu, mc.z}, {mc.x - X0 + 0.02f, 0.016f, 0.03f}, iron);
            B.paintCast.box({wa.x, yu + 0.01f, Z0 + 0.006f}, {0.05f, 0.07f, 0.012f}, iron);
            B.paintCast.box({X0 + 0.006f, yu - 0.03f, wb.z}, {0.012f, 0.08f, 0.05f}, iron);
        }
        // cable hanging down the wall
        B.paint.tube({{c.x - 0.1f, c.y - 0.1f, c.z - 0.2f}, {X0 + 0.03f, c.y - 0.4f, -3.1f}, {X0 + 0.02f, 1.1f, -3.2f}, {X0 + 0.02f, 0.05f, -3.25f}},
                     0.004f, 5, Color{30, 30, 30, 255});
    }
    // --- ceiling fan: brass downrod + motor (static), wooden blades (rotating)
    {
        fanPos = {-0.3f, 2.64f, -1.7f};
        B.brass.cylinder({fanPos.x, 2.74f, fanPos.z}, 0.012f, CY - 2.74f, 10, Color{190, 150, 80, 255});
        latheAt(B.brass, {fanPos.x, CY - 0.05f, fanPos.z}, {{0.0f, 0.0f}, {0.06f, 0.01f}, {0.075f, 0.04f}, {0.0f, 0.05f}}, 20, false, false,
                Color{190, 150, 80, 255});
        latheAt(B.brass, {fanPos.x, 2.6f, fanPos.z},
                {{0.0f, -0.02f}, {0.05f, -0.015f}, {0.11f, 0.0f}, {0.12f, 0.04f}, {0.1f, 0.1f}, {0.05f, 0.14f}, {0.0f, 0.145f}}, 24, false,
                false, Color{200, 160, 90, 255});
        MeshBuilder fb;
        for (int k = 0; k < 5; ++k) {
            float a = k * 2.f * PI / 5.f;
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateX(12.f * DEG2RAD), MatrixTranslate(0.35f, 0.0f, 0.f)), MatrixRotateY(a));
            fb.setTransform(m);
            fb.roundedBox({0, 0, 0}, {0.46f, 0.008f, 0.12f}, 0.004f, 2, Color{120, 74, 44, 255});
            fb.box({-0.2f, 0.008f, 0}, {0.14f, 0.008f, 0.03f}, Color{60, 50, 40, 255});
        }
        fb.resetTransform();
        fanBlades = fb.build(true);
    }
    // --- coat rack by the front wall with coats and caps
    {
        const Vector3 p{-2.95f, 0.f, 3.25f};
        latheAt(B.varnishCast, p, {{0.f, 0.f}, {0.12f, 0.f}, {0.12f, 0.02f}, {0.03f, 0.05f}, {0.018f, 0.1f}, {0.016f, 1.7f}, {0.03f, 1.76f}, {0.0f, 1.8f}},
                14, false, false, Color{80, 50, 30, 255});
        for (int k = 0; k < 6; ++k) {
            float a = k * PI / 3.f;
            B.varnishCast.tube({{p.x, 1.66f, p.z}, {p.x + std::cos(a) * 0.1f, 1.68f, p.z + std::sin(a) * 0.1f},
                                {p.x + std::cos(a) * 0.16f, 1.76f, p.z + std::sin(a) * 0.16f}},
                               0.009f, 6, Color{80, 50, 30, 255});
        }
        // coats: draped shapes hanging from hooks
        struct Coat {
            float a;
            Color c;
            float len;
        };
        const Coat coats[3] = {{0.3f, {52, 50, 54, 255}, 1.0f}, {2.4f, {84, 62, 44, 255}, 0.9f}, {4.3f, {36, 40, 52, 255}, 1.06f}};
        for (const Coat& ct : coats) {
            Vector3 h{p.x + std::cos(ct.a) * 0.19f, 1.74f, p.z + std::sin(ct.a) * 0.19f};
            // hung by the collar loop: narrow at the hook, shoulders slumping, widening to the hem; flattened
            // against the pole (local +X points away from it), the hem swinging slightly outward
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(0.36f, 1.f, 1.f), MatrixRotateY(-ct.a)), MatrixTranslate(h.x, h.y - ct.len, h.z));
            const float L = ct.len;
            // hem flares a little, body hangs straight, shoulders drop sharply to the collar loop on the hook
            std::vector<Vector2> prof{{0.0f, 0.0f},        {0.225f, 0.0f},      {0.232f, 0.02f},     {0.215f, 0.3f * L},
                                      {0.205f, 0.62f * L}, {0.212f, 0.8f * L},  {0.2f, 0.86f * L},   {0.15f, 0.92f * L},
                                      {0.07f, 0.965f * L}, {0.03f, 0.99f * L},  {0.0f, L}};
            latheAt(B.clothCast, {0.05f, 0.f, 0.f}, prof, 16, true, false, ct.c, &m);
            // collar and a sleeve hanging down the front
            B.clothCast.setTransform(m);
            B.clothCast.ellipsoid({0.06f, 0.94f * L, 0.f}, {0.06f, 0.045f, 0.12f}, 6, 10, scaleRgb(ct.c, 0.8f));  // collar
            for (int sd = -1; sd <= 1; sd += 2)  // sleeves hanging at the sides
                B.clothCast.capsule({0.1f, 0.84f * L, sd * 0.2f}, {0.12f, 0.36f * L, sd * 0.215f}, 0.05f, 8, scaleRgb(ct.c, 0.92f));
            B.clothCast.box({0.235f, 0.5f * L, 0.f}, {0.01f, 0.8f * L, 0.012f}, scaleRgb(ct.c, 0.7f));  // front opening
            B.clothCast.resetTransform();
        }
        // flat caps (kasket) on two hooks
        for (float a : {1.3f, 3.4f}) {
            Vector3 h{p.x + std::cos(a) * 0.17f, 1.8f, p.z + std::sin(a) * 0.17f};
            B.clothCast.ellipsoid(h, {0.1f, 0.035f, 0.09f}, 6, 12, Color{70, 66, 60, 255});
            B.clothCast.ellipsoid(Vector3Add(h, Vector3{std::cos(a) * 0.06f, -0.015f, std::sin(a) * 0.06f}), {0.07f, 0.01f, 0.06f}, 4, 10,
                                  Color{60, 56, 50, 255});
        }
        // umbrella leaning on the rack
        B.paintCast.capsule({p.x + 0.13f, 0.02f, p.z - 0.1f}, {p.x + 0.05f, 0.85f, p.z - 0.04f}, 0.025f, 8, Color{24, 24, 30, 255});
        B.varnishCast.tube({{p.x + 0.05f, 0.85f, p.z - 0.04f}, {p.x + 0.045f, 0.93f, p.z - 0.035f}, {p.x + 0.0f, 0.95f, p.z - 0.03f}}, 0.008f, 6,
                           Color{120, 80, 40, 255});
    }
    // --- WC door on the front wall (closed, panelled, with a sign)
    {
        const float a0 = 0.45f, a1 = 1.25f, h = 2.06f, am = (a0 + a1) * 0.5f;
        boxW(B.wood, wallPoint(1, am, h * 0.5f, 0.02f), wallBoxSize(1, a1 - a0, h, 0.04f), 0.9f, Color{150, 120, 90, 255}, 1);
        for (int k = 0; k < 2; ++k)
            B.wood.roundedBox(wallPoint(1, am, k ? 1.5f : 0.6f, 0.045f), wallBoxSize(1, 0.56f, k ? 0.62f : 0.8f, 0.015f), 0.01f, 2,
                              Color{160, 130, 98, 255});
        Vector3 hp = wallPoint(1, a0 + 0.1f, 1.02f, 0.07f);
        B.brass.capsule(hp, wallPoint(1, a0 + 0.2f, 1.02f, 0.07f), 0.009f, 6, Color{210, 170, 90, 255});
        B.brass.ellipsoid(wallPoint(1, a0 + 0.1f, 1.02f, 0.045f), {0.03f, 0.03f, 0.03f}, 4, 10, Color{200, 160, 84, 255});
        canvasQuad(B.artGloss, wallPoint(1, am, 1.78f, 0.056f), 0.16f, 0.1f, wallN(1), art::SIGN_WC, AW, AH);
    }
    // --- clock hands (local: pointing +Y from the pivot, facing +Z)
    {
        MeshBuilder h, m, s;
        h.box({0, 0.04f, 0}, {0.014f, 0.1f, 0.003f}, Color{30, 26, 22, 255});
        h.box({0, 0.095f, 0}, {0.022f, 0.022f, 0.003f}, Color{30, 26, 22, 255});
        m.box({0, 0.06f, 0}, {0.009f, 0.15f, 0.003f}, Color{30, 26, 22, 255});
        s.box({0, 0.055f, 0}, {0.003f, 0.19f, 0.002f}, Color{180, 30, 24, 255});
        s.cylinder({0, 0, -0.002f}, 0.008f, 0.004f, 10, Color{180, 30, 24, 255});
        handHour = h.build(true);
        handMin = m.build(true);
        handSec = s.build(true);
    }
    // --- a die (6 faces from the art atlas) for the tavla table
    {
        MeshBuilder d;
        const float e = 0.0075f;
        auto face = [&](Vector3 n, Vector3 u, int pips) {
            Vector3 v = Vector3CrossProduct(n, u);
            Rectangle src{art::DICE.x + (pips - 1) * 96.f + 3, art::DICE.y + 3, 90, 90};
            Vector3 c = Vector3Scale(n, e);
            Vector3 hu = Vector3Scale(u, e), hv = Vector3Scale(v, e);
            float u0 = src.x / ART_W, u1 = (src.x + src.width) / ART_W, vb = 1.f - (src.y + src.height) / ART_H, vt = 1.f - src.y / ART_H;
            quadUV(d, Vector3Subtract(Vector3Subtract(c, hu), hv), Vector3Subtract(Vector3Add(c, hu), hv), Vector3Add(Vector3Add(c, hu), hv),
                   Vector3Add(Vector3Subtract(c, hu), hv), {u0, vb}, {u1, vb}, {u1, vt}, {u0, vt}, WHITE);
        };
        face({0, 1, 0}, {1, 0, 0}, 1);
        face({0, -1, 0}, {1, 0, 0}, 6);
        face({1, 0, 0}, {0, 0, -1}, 3);
        face({-1, 0, 0}, {0, 0, 1}, 4);
        face({0, 0, 1}, {1, 0, 0}, 2);
        face({0, 0, -1}, {-1, 0, 0}, 5);
        dieMesh = d.build(true);
    }
    // --- a doormat inside the street door
    boxW(B.cloth, {X0 + 0.4f, 0.006f, 2.8f}, {0.5f, 0.012f, 0.8f}, 0.5f, Color{110, 60, 40, 255}, 2);
    // --- nazar boncuğu over the street door, on a string from a nail
    {
        const Vector3 c{X0 + 0.018f, 2.47f, 2.8f};
        const Color layers[4] = {{24, 52, 150, 255}, {236, 236, 240, 255}, {110, 170, 220, 255}, {14, 14, 20, 255}};
        const float rad[4] = {0.045f, 0.03f, 0.02f, 0.009f};
        for (int k = 0; k < 4; ++k) {
            B.ceramic.setTransform(MatrixMultiply(MatrixRotateZ(-PI * 0.5f), MatrixTranslate(c.x - 0.006f, c.y, c.z)));
            B.ceramic.cylinder({0, 0, 0}, rad[k], 0.012f + 0.002f * k, 20, layers[k]);
            B.ceramic.resetTransform();
        }
        B.paint.tube({{c.x, c.y + 0.045f, c.z}, {X0 + 0.012f, c.y + 0.13f, c.z}}, 0.0015f, 4, Color{200, 40, 40, 255});
        B.metal.sphere({X0 + 0.01f, c.y + 0.13f, c.z}, 0.005f, 4, 6, Color{120, 110, 100, 255});
    }
    // --- a rubber plant in an olive-oil tin by the back wall (between the scoreboard and the price board)
    {
        okey::Rng prng(seed + 404);
        rubberPlant(B, {0.52f, 0.f, Z0 + 0.22f}, prng);
    }
    // --- a long wall bench (peyke) behind our table with kilim cushions, a newspaper and a forgotten cap
    {
        const float x0 = -2.15f, x1 = 0.45f, zb = Z1 - 0.05f, zf = Z1 - 0.45f, sy = 0.44f;
        const float cx = (x0 + x1) * 0.5f, cz = (zb + zf) * 0.5f, len = x1 - x0;
        const Color bw{112, 72, 44, 255};
        rbox(B.woodCast, {cx, sy - 0.022f, cz}, {len, 0.045f, zb - zf}, 0.01f, 2, bw);
        rbox(B.woodCast, {cx, sy - 0.085f, zf + 0.02f}, {len - 0.04f, 0.08f, 0.024f}, 0.006f, 1, scaleRgb(bw, 0.85f));
        for (float lx : {x0 + 0.06f, cx, x1 - 0.06f})
            for (float lz : {zf + 0.04f, zb - 0.04f}) rbox(B.woodCast, {lx, (sy - 0.045f) * 0.5f, lz}, {0.05f, sy - 0.045f, 0.05f}, 0.008f, 1, scaleRgb(bw, 0.8f));
        rbox(B.woodCast, {cx, 0.1f, cz}, {len - 0.1f, 0.03f, 0.03f}, 0.006f, 1, scaleRgb(bw, 0.8f));
        // seat cushions (kilim on top) and back cushions leaning on the wainscot
        const Color kr{140, 44, 36, 255};
        for (int k = 0; k < 2; ++k) {
            float ccx = x0 + 0.1f + (k + 0.5f) * (len - 0.2f) * 0.5f, cw = (len - 0.2f) * 0.5f - 0.03f;
            B.clothCast.roundedBox({ccx, sy + 0.03f, cz - 0.01f}, {cw, 0.06f, 0.36f}, 0.025f, 2, kr);
            canvasQuadUp(B.art, {ccx, sy + 0.0605f, cz - 0.01f}, cw - 0.06f, 0.3f, art::KILIM, ART_W, ART_H, 0.f);
            Matrix m = MatrixMultiply(MatrixRotateX(14.f * DEG2RAD), MatrixTranslate(ccx, sy + 0.22f, zb - 0.07f));
            B.clothCast.setTransform(m);
            B.clothCast.roundedBox({0, 0, 0}, {cw, 0.3f, 0.08f}, 0.03f, 2, kr);
            B.clothCast.resetTransform();
            B.art.setTransform(m);
            canvasQuad(B.art, {0, 0, -0.0405f}, cw - 0.06f, 0.24f, {0, 0, -1}, art::KILIM, ART_W, ART_H);
            B.art.resetTransform();
        }
        canvasQuadUp(B.art, {-0.35f, sy + 0.062f, cz + 0.02f}, 0.2f, 0.27f, art::NEWS, ART_W, ART_H, 200.f);
        const Vector3 cap{-1.62f, sy + 0.085f, cz - 0.03f};
        B.clothCast.ellipsoid(cap, {0.1f, 0.035f, 0.09f}, 6, 12, Color{66, 62, 56, 255});
        B.clothCast.ellipsoid({cap.x, cap.y - 0.016f, cap.z - 0.07f}, {0.075f, 0.01f, 0.055f}, 4, 10, Color{58, 54, 48, 255});
    }
}

// ---------------------------------------------------------------------------- windows, the street outside
void Room::Impl::buildStreetAndWindows(Builders& B) {
    const float SW = STREET_W, SH = STREET_H;
    // panels across the street (emissive, unfogged) and the wet road
    canvasQuad(B.street, {-8.2f, 3.f, -1.f}, 18.f, 8.f, {1, 0, 0}, street::LEFT, SW, SH);
    canvasQuad(B.street, {-3.5f, 3.f, -7.4f}, 11.f, 8.f, {0, 0, 1}, street::BACK, SW, SH);
    canvasQuadUp(B.street, {-6.2f, -0.03f, -1.f}, 18.f, 4.f, street::ROAD, SW, SH, 90.f);
    canvasQuadUp(B.street, {-3.5f, -0.036f, -5.4f}, 11.f, 4.f, street::ROAD, SW, SH, 0.f);
    streetLampGlow = {-5.35f, 3.35f, -0.35f};
    backLampGlow = {-5.2f, 3.1f, -6.2f};
    neighbourWindows = {{-8.15f, 4.6f, -3.3f}, {-8.15f, 4.6f, 3.1f}, {-7.35f, 3.4f, -3.0f}};
    // window frames (painted wood), glass with condensation, lettering
    for (size_t oi = 0; oi < openings.size(); ++oi) {
        const Opening& o = openings[oi];
        if (o.kind != 0) continue;
        int w = o.wall, g = wallGrain(w);
        const float d = 0.13f;  // frame depth into the reveal
        const Color fc{196, 186, 164, 255};
        float am = (o.a0 + o.a1) * 0.5f, ym = (o.y0 + o.y1) * 0.5f, bw = 0.055f;
        float W = o.a1 - o.a0, H = o.y1 - o.y0;
        auto bar = [&](float a, float y, float sa, float sy) {
            boxW(B.paint, wallPoint(w, a, y, -d), wallBoxSize(w, sa, sy, 0.06f), 0.5f, fc, (sy > sa) ? 1 : g);
        };
        bar(o.a0 + bw * 0.5f, ym, bw, H);
        bar(o.a1 - bw * 0.5f, ym, bw, H);
        bar(am, o.y0 + bw * 0.5f, W, bw);
        bar(am, o.y1 - bw * 0.5f, W, bw);
        float transom = o.y0 + H * 0.72f;
        bar(am, transom, W, bw * 0.8f);
        bar(am, (o.y0 + transom) * 0.5f, bw * 0.7f, transom - o.y0);
        // latch
        B.brass.capsule(wallPoint(w, am + 0.03f, o.y0 + H * 0.4f, -d + 0.04f), wallPoint(w, am + 0.03f, o.y0 + H * 0.48f, -d + 0.04f), 0.006f, 6,
                        Color{200, 160, 80, 255});
        // glass pane (local mesh centred at the pane, submitted transparent)
        Vector3 pc = wallPoint(w, am, ym, -d - 0.005f);
        MeshBuilder pm;
        Vector3 n = wallN(w), r = wallRight(n);
        Vector3 hr = Vector3Scale(r, W * 0.5f), hu{0, H * 0.5f, 0};
        quadUV(pm, Vector3Subtract(Vector3Negate(hr), hu), Vector3Subtract(hr, hu), Vector3Add(hr, hu), Vector3Add(Vector3Negate(hr), hu),
               {0, 1}, {1, 1}, {1, 0}, {0, 0}, WHITE);  // the mist texture spans the whole pane once
        panes.push_back({pm.build(true), pc, &mCondense});
        if (oi == 1) {  // painted lettering on the big street window (mirrored: read from the street)
            MeshBuilder lm;
            // in the fanlight above the transom: clear glass there (no mullion, above the café curtain)
            float lw = W * 0.9f, lh = lw * 0.25f;
            const float fanLo = transom + bw * 0.4f, fanHi = o.y1 - bw;
            if (lh > fanHi - fanLo) lh = fanHi - fanLo, lw = lh * 4.f;
            Vector3 lc{0, (fanLo + fanHi) * 0.5f - ym, 0};
            Vector3 lr = Vector3Scale(r, lw * 0.5f), lu{0, lh * 0.5f, 0};
            quadUV(lm, Vector3Add(Vector3Subtract(lc, lr), Vector3Negate(lu)), Vector3Add(Vector3Add(lc, lr), Vector3Negate(lu)),
                   Vector3Add(Vector3Add(lc, lr), lu), Vector3Add(Vector3Subtract(lc, lr), lu), {1, 0}, {0, 0}, {0, 1}, {1, 1}, WHITE);
            panes.push_back({lm.build(true), Vector3Add(pc, Vector3Scale(n, 0.004f)), &mLetter});
        }
        // lace café curtain on a brass rod across the lower half (the street only sees the patrons' caps)
        const float rodY = o.y0 + H * 0.5f;
        B.brass.capsule(wallPoint(w, o.a0 - 0.05f, rodY, 0.04f), wallPoint(w, o.a1 + 0.05f, rodY, 0.04f), 0.006f, 6,
                        Color{190, 150, 80, 255});
        for (float ea : {o.a0 - 0.05f, o.a1 + 0.05f})
            B.brass.sphere(wallPoint(w, ea, rodY, 0.04f), 0.012f, 6, 8, Color{200, 160, 84, 255});
        {
            MeshBuilder cm;
            const float ca0 = o.a0 - 0.02f, ca1 = o.a1 + 0.02f, top = rodY + 0.01f, bot = o.y0 + 0.01f;
            const int n = std::max(8, (int)((ca1 - ca0) / 0.035f));
            Vector3 cc = wallPoint(w, (ca0 + ca1) * 0.5f, (top + bot) * 0.5f, 0.035f);
            for (int i = 0; i <= n; ++i) {
                float t = (float)i / n, aa = ca0 + (ca1 - ca0) * t;
                float fold = (i % 2 ? 1.f : -1.f) * 0.012f;                     // gathered pleats
                Vector3 pt = wallPoint(w, aa, top, 0.035f + fold), pb = wallPoint(w, aa, bot, 0.035f + fold * 0.6f);
                Vector3 nn = Vector3Normalize(Vector3Add(n0(w), Vector3Scale(wallRight(n0(w)), (i % 2 ? 0.35f : -0.35f))));
                float uu = (aa - ca0) / 0.34f;
                cm.vertex(Vector3Subtract(pt, cc), nn, {uu, 0.f});
                cm.vertex(Vector3Subtract(pb, cc), nn, {uu, 1.f});
                if (i > 0) {
                    int k = cm.vertexCount() - 4;
                    cm.quad(k + 1, k + 3, k + 2, k);
                }
            }
            panes.push_back({cm.build(true), cc, &mLace});
        }
    }
}

// ---------------------------------------------------------------------------- the ashtray on our table
void Room::Impl::buildAshtray() {
    // heavy pressed-glass ashtray (transparent) at ASHTRAY_POS; contents opaque. Local origin = ASHTRAY_POS.
    MeshBuilder g, in, cig, em;
    g.lathe({{0.0f, 0.0f},
             {0.043f, 0.0f},
             {0.049f, 0.005f},
             {0.05f, 0.022f},
             {0.047f, 0.027f},
             {0.037f, 0.027f},
             {0.034f, 0.012f},
             {0.0f, 0.012f}},
            40, false, false, WHITE);
    in.cylinder({0, 0.012f, 0}, 0.031f, 0.002f, 20, Color{120, 116, 110, 255});
    okey::Rng r2(seed + 5);
    for (int k = 0; k < 4; ++k) {
        float a = k * 1.7f + 0.4f;
        butt(in, {std::cos(a) * 0.014f, 0.017f + (k % 2) * 0.004f, std::sin(a) * 0.014f}, a * RAD2DEG + r2.uniform(60.f, 120.f), 0.03f,
             k == 2);
    }
    for (int k = 0; k < 10; ++k)
        in.sphere({r2.uniform(-0.022f, 0.022f), 0.0145f, r2.uniform(-0.022f, 0.022f)}, r2.uniform(0.003f, 0.006f), 3, 5,
                  Color{(unsigned char)r2.range(80, 150), (unsigned char)r2.range(78, 146), (unsigned char)r2.range(74, 140), 255});
    // the smouldering cigarette: filter outside the rim (toward the table corner), lit end over the bowl
    Vector3 out = Vector3Normalize({w3d::ASHTRAY_POS.x, 0.f, w3d::ASHTRAY_POS.z});
    Vector3 filt = Vector3Add(Vector3Scale(out, 0.077f), Vector3{0, 0.036f, 0});
    Vector3 mid = Vector3Add(Vector3Scale(out, 0.052f), Vector3{0, 0.0325f, 0});
    Vector3 lit = Vector3Add(Vector3Scale(out, -0.012f), Vector3{0, 0.022f, 0});
    cig.capsule(filt, mid, 0.0041f, 10, Color{214, 152, 86, 255});
    Vector3 ashStart = Vector3Add(lit, Vector3Scale(Vector3Normalize(Vector3Subtract(mid, lit)), 0.007f));
    cig.capsule(mid, ashStart, 0.0040f, 10, Color{242, 240, 234, 255});
    cig.capsule(ashStart, lit, 0.0039f, 8, Color{150, 146, 140, 255});
    em.sphere(lit, 0.0036f, 5, 8, WHITE);
    ashGlass = g.build(true);
    ashInside = in.build(true);
    ashCig = cig.build(true);
    ashEmber = em.build(true);
    emberPos = Vector3Add(w3d::ASHTRAY_POS, lit);
    emitters.push_back({Vector3Add(emberPos, Vector3{0, 0.004f, 0}), 11.f, 0.f, 2});
}

// ---------------------------------------------------------------------------- bead curtain in the street door
void Room::Impl::buildCurtain() {
    const Opening& o = openings[3];
    const float top = o.y1 - 0.02f, bottom = 0.1f;
    curtainPivot = wallPoint(2, (o.a0 + o.a1) * 0.5f, top, 0.03f);
    const Color pal[4] = {{150, 90, 44, 255}, {120, 30, 30, 255}, {214, 170, 80, 255}, {236, 226, 200, 255}};
    const int strands = 18;
    for (int grp = 0; grp < 3; ++grp) {
        MeshBuilder b;
        for (int s = grp; s < strands; s += 3) {
            float a = o.a0 + 0.03f + (o.a1 - o.a0 - 0.06f) * s / (strands - 1);
            Vector3 p = wallPoint(2, a, 0, 0.03f);
            // local to the pivot
            Vector3 lp{p.x - curtainPivot.x, 0, p.z - curtainPivot.z};
            b.cylinder({lp.x, -0.002f, lp.z}, 0.0012f, 0.0f, 3, WHITE);
            int k = 0;
            for (float y = top - 0.02f; y > bottom; y -= 0.034f, ++k) {
                int pat = (std::abs(k - 12) + s) % 7;
                Color c = pal[pat == 0 ? 1 : pat < 3 ? 0 : pat < 5 ? 2 : 3];
                b.ellipsoid({lp.x, y - top, lp.z}, {0.0065f, 0.0135f, 0.0065f}, 4, 6, c);
            }
            b.capsule({lp.x, bottom - top, lp.z}, {lp.x, 0.f, lp.z}, 0.0012f, 3, Color{60, 50, 40, 255});
        }
        curtainMesh[grp] = b.build(true);
    }
}

}  // namespace r3d
