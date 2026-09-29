// Table3D geometry: pose math, the table/istaka/stand meshes, 3D tile meshes (faces from the tilegfx 3D
// atlas), resting poses of every place a tile can be, and per-frame submission to the Renderer.
#include "r3d/Table3DInternal.h"

#include <algorithm>
#include <cmath>

namespace r3d {
namespace t3d {

// ================================================================ math
uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hashF(uint32_t x) { return (float)(hashU(x) & 0xFFFFFF) / 16777215.f; }
float hashS(uint32_t x) { return hashF(x) * 2.f - 1.f; }

float smooth01(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}

Matrix poseMatrix(const Pose& p) {
    Matrix m = QuaternionToMatrix(p.rot);
    const float s = p.scale;
    m.m0 *= s, m.m1 *= s, m.m2 *= s;
    m.m4 *= s, m.m5 *= s, m.m6 *= s;
    m.m8 *= s, m.m9 *= s, m.m10 *= s;
    m.m12 = p.pos.x;
    m.m13 = p.pos.y;
    m.m14 = p.pos.z;
    return m;
}

Quaternion quatFromAxes(Vector3 x, Vector3 y, Vector3 z) {
    Matrix m = MatrixIdentity();
    m.m0 = x.x, m.m1 = x.y, m.m2 = x.z;
    m.m4 = y.x, m.m5 = y.y, m.m6 = y.z;
    m.m8 = z.x, m.m9 = z.y, m.m10 = z.z;
    return QuaternionNormalize(QuaternionFromMatrix(m));
}

Vector3 poseAxis(const Pose& p, int axis) {
    const Vector3 a[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    return Vector3RotateByQuaternion(a[std::clamp(axis, 0, 2)], p.rot);
}

Vector3 poseToWorld(const Pose& p, Vector3 local) {
    return Vector3Add(p.pos, Vector3Scale(Vector3RotateByQuaternion(local, p.rot), p.scale));
}

float rayPoseBox(const Ray& r, const Pose& p, Vector3 half) {
    const Quaternion inv = QuaternionInvert(p.rot);
    const float s = p.scale > 1e-5f ? p.scale : 1e-5f;
    const Vector3 o = Vector3Scale(Vector3RotateByQuaternion(Vector3Subtract(r.position, p.pos), inv), 1.f / s);
    const Vector3 d = Vector3Scale(Vector3RotateByQuaternion(r.direction, inv), 1.f / s);
    float t0 = -1e30f, t1 = 1e30f;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, hh[3] = {half.x, half.y, half.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-9f) {
            if (oo[i] < -hh[i] || oo[i] > hh[i]) return -1.f;
            continue;
        }
        float a = (-hh[i] - oo[i]) / dd[i], b = (hh[i] - oo[i]) / dd[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return -1.f;
    }
    if (t1 < 0.f) return -1.f;
    return t0 >= 0.f ? t0 : t1;
}

bool rayPlane(const Ray& r, Vector3 p0, Vector3 n, Vector3& hit, float* dist) {
    const float den = Vector3DotProduct(r.direction, n);
    if (std::fabs(den) < 1e-6f) return false;
    const float t = Vector3DotProduct(Vector3Subtract(p0, r.position), n) / den;
    if (t < 0.f) return false;
    hit = Vector3Add(r.position, Vector3Scale(r.direction, t));
    if (dist) *dist = t;
    return true;
}

// ================================================================ places
namespace {
constexpr float LEDGE_T = RACK_LEDGE_T;
constexpr float LEDGE_D = TT + 0.0045f;     // ledge depth (out of the plank)
constexpr float ROW_GAP = RACK_ROW_GAP;
constexpr float PLANK_T = 0.009f;
constexpr float TILE_OFF = 0.0004f;         // tiles rest a hair in front of the plank
constexpr float RACK_BASE_H = 0.007f;       // base board thickness
constexpr float IND_LEAN = 35.f;            // indicator lean from vertical

const RackProfile kHumanRack{40.f, 0.0232f, w3d::TABLE_Y + 0.0205f};
const RackProfile kBotRack{16.f, 0.0080f, w3d::TABLE_Y + 0.0190f};

float rowS(int row) { return row == 1 ? TH * 0.5f : TH + ROW_GAP + TH * 0.5f; }

// Seat-local (right, out, y) -> world, `out` measured from the table centre.
Vector3 seatPoint(int seat, float right, float out, float y) { return w3d::seatLocal(seat, right, out, y); }
Vector3 seatDir(int seat, float right, float out, float up) {
    const Vector3 r = w3d::SEAT_RIGHT[seat], o = w3d::SEAT_DIR[seat];
    return {r.x * right + o.x * out, up, r.z * right + o.z * out};
}
} // namespace

const RackProfile& rackProfile(bool human) { return human ? kHumanRack : kBotRack; }

float slotRight(int col) { return ((float)col - (COLS - 1) * 0.5f) * PITCH; }

void rackRowFrame(int seat, bool humanRack, int row, Vector3& center, Vector3& normal, Vector3& right, Vector3& up) {
    const RackProfile& P = rackProfile(humanRack);
    const float ph = P.leanDeg * DEG2RAD;
    const float s = rowS(row);
    const float cOut = P.outB - std::sin(ph) * s + std::cos(ph) * (TT * 0.5f + TILE_OFF);
    const float cY = P.yB + std::cos(ph) * s + std::sin(ph) * (TT * 0.5f + TILE_OFF);
    center = seatPoint(seat, 0.f, w3d::RACK_DIST + cOut, cY);
    normal = seatDir(seat, 0.f, std::cos(ph), std::sin(ph));
    up = seatDir(seat, 0.f, -std::sin(ph), std::cos(ph));
    right = w3d::SEAT_RIGHT[seat];
}

Pose rackPose(int seat, bool humanRack, int row, float right, bool faceOwner) {
    Vector3 c, n, rt, up;
    rackRowFrame(seat, humanRack, row, c, n, rt, up);
    Pose p;
    p.pos = Vector3Add(c, Vector3Scale(rt, right));
    p.rot = faceOwner ? quatFromAxes(rt, up, n) : quatFromAxes(Vector3Negate(rt), up, Vector3Negate(n));
    return p;
}

Quaternion flatFaceUp(float yawDeg) {
    const Quaternion base = quatFromAxes({1, 0, 0}, {0, 0, -1}, {0, 1, 0});
    return QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, yawDeg * DEG2RAD), base);
}

Quaternion flatFaceDown(float yawDeg) {
    const Quaternion base = quatFromAxes({-1, 0, 0}, {0, 0, -1}, {0, -1, 0});
    return QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, yawDeg * DEG2RAD), base);
}

Pose pilePose(int index) {
    const int k = index / 5, layer = index % 5;
    // stacks of five: 0 back-right, 1 back-left, 2 front-right, 3 front-left (drawn first), more further back
    const int col = 1 - (k % 2);
    const int rowz = k < 4 ? k / 2 : -(k - 2) / 2;
    const uint32_t h = hashU(0x51ED0000u + (uint32_t)index * 7919u);
    Pose p;
    p.pos = {w3d::PILE_POS.x + ((float)col - 0.5f) * (TW + 0.0045f) + hashS(h) * 0.0008f,
             w3d::TABLE_Y + TT * 0.5f + (float)layer * TT,
             w3d::PILE_POS.z + ((float)rowz - 0.5f) * (TH + 0.0045f) + hashS(h + 1) * 0.0008f};
    p.rot = flatFaceDown(hashS(h + 2) * 1.6f);
    return p;
}

Pose discardPose(int seat, int depth, int n, int id) {
    const int vis = std::min(n, DISC_LAYERS);
    const int layer = depth < vis ? vis - 1 - depth : 0;
    const uint32_t h = hashU((uint32_t)id * 2654435761u + (uint32_t)seat * 97u + 17u);
    const Vector3 b = w3d::DISCARD_POS[seat];
    Pose p;
    p.pos = {b.x + hashS(h) * 0.0018f, w3d::TABLE_Y + TT * 0.5f + (float)layer * TT, b.z + hashS(h + 1) * 0.0018f};
    p.rot = flatFaceUp(hashS(h + 2) * (depth == 0 ? 4.f : 7.f));
    return p;
}

Pose indicatorPose() {
    const float ps = IND_LEAN * DEG2RAD;
    const Vector3 t{0.f, std::cos(ps), -std::sin(ps)}, n{0.f, std::sin(ps), std::cos(ps)};
    const Vector3 base{w3d::INDICATOR_POS.x, w3d::TABLE_Y + 0.0085f, w3d::INDICATOR_POS.z + 0.0105f};
    Pose p;
    p.pos = Vector3Add(base, Vector3Add(Vector3Scale(t, TH * 0.5f), Vector3Scale(n, TT * 0.5f + 0.0004f)));
    p.rot = quatFromAxes({1, 0, 0}, t, n);
    return p;
}

Pose heapPose(int cell, float swirl) {
    // cells of a jittered grid, nearest to the centre first
    static std::vector<Vector2> cells;
    if (cells.empty()) {
        std::vector<Vector2> all;
        for (int gz = -8; gz <= 8; ++gz)
            for (int gx = -9; gx <= 9; ++gx) all.push_back({(float)gx * 0.0365f + (gz & 1) * 0.012f, (float)gz * 0.0485f});
        std::sort(all.begin(), all.end(), [](Vector2 a, Vector2 b) {
            return a.x * a.x + a.y * a.y * 0.9f < b.x * b.x + b.y * b.y * 0.9f;
        });
        all.resize(HEAP_CELLS);
        cells = all;
    }
    cell = std::clamp(cell, 0, HEAP_CELLS - 1);
    const uint32_t h = hashU(0x4EA90000u + (uint32_t)cell * 131u);
    const Vector2 c{cells[cell].x + hashS(h) * 0.0012f, cells[cell].y + hashS(h + 1) * 0.0012f};
    const float r = std::sqrt(c.x * c.x + c.y * c.y);
    const float a = swirl * (0.85f + 0.3f * std::min(1.f, r / 0.25f)) + hashS(h + 3) * std::fabs(swirl) * 0.05f;
    const float ca = std::cos(a), sa = std::sin(a);
    Pose p;
    p.pos = {c.x * ca - c.y * sa, w3d::TABLE_Y + TT * 0.5f, c.x * sa + c.y * ca};
    p.rot = flatFaceDown(hashS(h + 2) * 7.f - a * RAD2DEG);
    return p;
}

} // namespace t3d

// ================================================================ meshes & materials
using namespace t3d;
using okey::Meld;
using okey::MeldKind;

namespace {

// Rounded tile box with per-face UVs: front (+Z) = face cell, back (-Z) = back cell, sides = body patch.
Mesh buildTileMesh(Rectangle uvFront, Rectangle uvBack, Vector2 uvSide) {
    MeshBuilder b;
    const Vector3 h{TW * 0.5f, TH * 0.5f, TT * 0.5f};
    const float r = 0.0022f;
    const int seg = 3;
    const Vector3 hi{h.x - r, h.y - r, h.z - r};
    auto coords = [&](float hh, float hin) {
        std::vector<float> v;
        for (int k = 0; k <= seg; ++k) v.push_back(-hh + (hh - hin) * (float)k / (float)seg);
        for (int k = 0; k <= seg; ++k) v.push_back(hin + (hh - hin) * (float)k / (float)seg);
        return v;
    };
    struct F {
        Vector3 n, u, v;
        int kind; // 0 front, 1 back, 2 side
    };
    const F faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, 1},
                        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, 2}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 2},
                        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, 2}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 2}};
    auto comp = [](Vector3 v, Vector3 a) { return v.x * a.x + v.y * a.y + v.z * a.z; };
    auto absA = [](Vector3 a) { return Vector3{std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; };
    for (const F& f : faces) {
        const float hu = comp(h, absA(f.u)), hv = comp(h, absA(f.v)), hn = comp(h, absA(f.n));
        const float iu = comp(hi, absA(f.u)), iv = comp(hi, absA(f.v));
        const std::vector<float> cu = coords(hu, iu), cv = coords(hv, iv);
        const int nu = (int)cu.size(), nv = (int)cv.size();
        const int base = b.vertexCount();
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const Vector3 p = Vector3Add(Vector3Add(Vector3Scale(f.n, hn), Vector3Scale(f.u, cu[i])),
                                             Vector3Scale(f.v, cv[j]));
                const Vector3 q{std::clamp(p.x, -hi.x, hi.x), std::clamp(p.y, -hi.y, hi.y), std::clamp(p.z, -hi.z, hi.z)};
                const Vector3 d = Vector3Subtract(p, q);
                const Vector3 n = Vector3Length(d) > 1e-7f ? Vector3Normalize(d) : f.n;
                const Vector3 pos = Vector3Add(q, Vector3Scale(n, r));
                const float fu = (cu[i] + hu) / (2.f * hu), fv = 1.f - (cv[j] + hv) / (2.f * hv);
                Vector2 uv;
                if (f.kind == 0) uv = {uvFront.x + fu * uvFront.width, uvFront.y + fv * uvFront.height};
                else if (f.kind == 1) uv = {uvBack.x + fu * uvBack.width, uvBack.y + fv * uvBack.height};
                else uv = uvSide;
                b.vertex(pos, n, uv);
            }
        for (int j = 0; j + 1 < nv; ++j)
            for (int i = 0; i + 1 < nu; ++i) {
                const int a = base + j * nu + i;
                b.quad(a, a + 1, a + nu + 1, a + nu);
            }
    }
    return b.build();
}

// Box whose local X runs along `along` (wood grain follows the long axis), centred at c.
void grainBox(MeshBuilder& b, Vector3 c, Vector3 size, float radius, int seg, const Matrix& rot = MatrixIdentity()) {
    b.setTransform(MatrixMultiply(rot, MatrixTranslate(c.x, c.y, c.z)));
    b.roundedBox({0, 0, 0}, size, radius, seg);
    b.resetTransform();
}

Mesh buildTable() {
    MeshBuilder b;
    const float Y = w3d::TABLE_Y, H = w3d::FELT_HALF, RW = w3d::RIM_W, RH = w3d::RIM_H;
    const float outer = H + RW;
    const float topT = 0.034f; // table top board thickness below the felt
    const float rimTop = Y + RH;
    const float rimBot = Y - topT;
    const float rimH = rimTop - rimBot;
    const Matrix alongZ = MatrixRotateY(PI * 0.5f);
    // raised rim: long pieces along X front/back, along Z left/right (mitre-like overlap at the corners)
    grainBox(b, {0.f, rimBot + rimH * 0.5f, H + RW * 0.5f}, {outer * 2.f, rimH, RW}, 0.009f, 3);
    grainBox(b, {0.f, rimBot + rimH * 0.5f, -H - RW * 0.5f}, {outer * 2.f, rimH, RW}, 0.009f, 3);
    grainBox(b, {H + RW * 0.5f, rimBot + rimH * 0.5f, 0.f}, {H * 2.f + 0.004f, rimH, RW}, 0.009f, 3, alongZ);
    grainBox(b, {-H - RW * 0.5f, rimBot + rimH * 0.5f, 0.f}, {H * 2.f + 0.004f, rimH, RW}, 0.009f, 3, alongZ);
    // a thin moulding line under the rim
    grainBox(b, {0.f, rimBot - 0.004f, 0.f}, {outer * 2.f - 0.02f, 0.010f, outer * 2.f - 0.02f}, 0.004f, 2);
    // board under the felt
    grainBox(b, {0.f, Y - topT * 0.5f - 0.002f, 0.f}, {H * 2.f + 0.01f, topT - 0.004f, H * 2.f + 0.01f}, 0.003f, 1);
    // apron (skirt) under the top
    const float apH = 0.095f, apT = 0.022f, apIn = outer - 0.055f;
    const float apY = rimBot - 0.009f - apH * 0.5f;
    grainBox(b, {0.f, apY, apIn}, {apIn * 2.f, apH, apT}, 0.005f, 2);
    grainBox(b, {0.f, apY, -apIn}, {apIn * 2.f, apH, apT}, 0.005f, 2);
    grainBox(b, {apIn, apY, 0.f}, {apIn * 2.f - apT * 2.f, apH, apT}, 0.005f, 2, alongZ);
    grainBox(b, {-apIn, apY, 0.f}, {apIn * 2.f - apT * 2.f, apH, apT}, 0.005f, 2, alongZ);
    // legs (grain vertical) with a slight taper look via two stacked boxes, and low stretchers
    const Matrix vertical = MatrixRotateZ(PI * 0.5f);
    const float legIn = apIn - 0.012f;
    const float legTop = apY + apH * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float sx = (i & 1) ? 1.f : -1.f, sz = (i & 2) ? 1.f : -1.f;
        const Vector3 c{sx * legIn, 0.f, sz * legIn};
        grainBox(b, {c.x, legTop * 0.5f + 0.2f, c.z}, {legTop - 0.4f, 0.056f, 0.056f}, 0.006f, 2, vertical);
        grainBox(b, {c.x, 0.2f * 0.5f + 0.004f, c.z}, {0.2f, 0.048f, 0.048f}, 0.006f, 2, vertical);
        grainBox(b, {c.x, 0.012f, c.z}, {0.024f, 0.054f, 0.054f}, 0.006f, 2, vertical); // foot
    }
    const float stY = 0.17f;
    grainBox(b, {0.f, stY, legIn}, {legIn * 2.f, 0.026f, 0.020f}, 0.005f, 2);
    grainBox(b, {0.f, stY, -legIn}, {legIn * 2.f, 0.026f, 0.020f}, 0.005f, 2);
    grainBox(b, {legIn, stY + 0.03f, 0.f}, {legIn * 2.f, 0.026f, 0.020f}, 0.005f, 2, alongZ);
    grainBox(b, {-legIn, stY + 0.03f, 0.f}, {legIn * 2.f, 0.026f, 0.020f}, 0.005f, 2, alongZ);
    return b.build();
}

Mesh buildFelt() {
    MeshBuilder b;
    const float H = w3d::FELT_HALF;
    // felt top (slightly padded look: a very soft bevel toward the rim)
    b.plane({0.f, w3d::TABLE_Y, 0.f}, {H * 2.f, H * 2.f}, {0, 1, 0}, {2.2f, 2.2f});
    return b.build();
}

// Istaka in seat-0 local coordinates (x = right, z = out from the rack centre, y absolute).
Mesh buildRack(const RackProfile& P) {
    MeshBuilder b;
    const float ph = P.leanDeg * DEG2RAD;
    const float sn = std::sin(ph), cs = std::cos(ph);
    const Vector3 t{0.f, cs, -sn}, n{0.f, sn, cs};
    const Vector3 B{0.f, P.yB, P.outB};
    const Matrix tilt = MatrixRotateX(-ph); // local Y -> t, local Z -> n
    const float len = w3d::RACK_LEN;
    const float capT = 0.012f;
    const float inner = len - 2.f * capT + 0.002f;
    auto at = [&](float s, float o) { return Vector3Add(B, Vector3Add(Vector3Scale(t, s), Vector3Scale(n, o))); };
    // plank (slanted back board)
    const float s0 = -LEDGE_T - 0.004f, s1 = 2.f * TH + ROW_GAP - 0.012f;
    grainBox(b, at((s0 + s1) * 0.5f, -PLANK_T * 0.5f), {inner, s1 - s0, PLANK_T}, 0.003f, 2, tilt);
    // two ledges with a small front lip each
    for (int row = 0; row < 2; ++row) {
        const float sL = row == 1 ? 0.f : TH + ROW_GAP - 0.0016f;
        grainBox(b, at(sL - LEDGE_T * 0.5f, LEDGE_D * 0.5f - 0.001f), {inner, LEDGE_T, LEDGE_D + 0.002f}, 0.0022f, 2, tilt);
        grainBox(b, at(sL + 0.0012f, LEDGE_D - 0.0016f), {inner, 0.0062f, 0.0032f}, 0.0014f, 2, tilt);
    }
    // end caps: slanted slabs spanning the plank + ledges
    for (int side = -1; side <= 1; side += 2) {
        const float x = (float)side * (len * 0.5f - capT * 0.5f);
        const float sa = s0 - 0.002f, sb = 2.f * TH + ROW_GAP - 0.006f;
        Vector3 c = at((sa + sb) * 0.5f, (LEDGE_D - PLANK_T) * 0.5f);
        c.x = x;
        grainBox(b, c, {capT, sb - sa, LEDGE_D + PLANK_T + 0.002f}, 0.004f, 2, tilt);
    }
    // base board on the felt + a back support strut under the plank
    const float baseTop = w3d::TABLE_Y + RACK_BASE_H;
    const float fz = at(0.f, LEDGE_D).z + 0.006f;
    const Vector3 back = at(s0, -PLANK_T);
    const float bz = std::min(back.z, at(s1 * 0.55f, -PLANK_T).z) - 0.004f;
    grainBox(b, {0.f, w3d::TABLE_Y + RACK_BASE_H * 0.5f, (fz + bz) * 0.5f}, {len, RACK_BASE_H, fz - bz}, 0.0025f, 2);
    // strut from the base to the back of the plank (keeps the plank's lean)
    const Vector3 hiB = at(s1 * 0.55f, -PLANK_T);
    const float strutH = hiB.y - baseTop;
    if (strutH > 0.004f)
        grainBox(b, {0.f, baseTop + strutH * 0.5f, hiB.z - 0.004f}, {inner - 0.03f, strutH, 0.008f}, 0.002f, 1);
    return b.build();
}

// Little wooden easel for the gösterge.
Mesh buildStand() {
    MeshBuilder b;
    const float ps = IND_LEAN * DEG2RAD;
    const Vector3 t{0.f, std::cos(ps), -std::sin(ps)}, n{0.f, std::sin(ps), std::cos(ps)};
    const Vector3 P = w3d::INDICATOR_POS;
    const Vector3 base{P.x, w3d::TABLE_Y + 0.0085f, P.z + 0.0105f};
    const Matrix tilt = MatrixRotateX(-ps);
    auto at = [&](float s, float o) { return Vector3Add(base, Vector3Add(Vector3Scale(t, s), Vector3Scale(n, o))); };
    // foot
    grainBox(b, {P.x, w3d::TABLE_Y + 0.003f, P.z + 0.002f}, {0.046f, 0.006f, 0.046f}, 0.0022f, 2);
    // back support (slanted board behind the tile)
    grainBox(b, at(TH * 0.40f, -0.0025f), {0.036f, TH * 0.86f, 0.005f}, 0.0018f, 2, tilt);
    // front ledge
    grainBox(b, at(-0.0022f, 0.006f), {0.040f, 0.0045f, 0.016f}, 0.0015f, 2, tilt);
    grainBox(b, at(0.0015f, 0.0135f), {0.040f, 0.006f, 0.003f}, 0.0012f, 2, tilt);
    // strut from the foot to the support
    const Vector3 top = at(TH * 0.62f, -0.005f);
    const float h = top.y - (w3d::TABLE_Y + 0.006f);
    grainBox(b, {P.x, w3d::TABLE_Y + 0.006f + h * 0.5f, top.z - 0.004f}, {0.012f, h, 0.006f}, 0.0015f, 1);
    return b.build();
}

Mesh buildQuad(float w, float h) {
    MeshBuilder b;
    const Vector3 n{0, 0, 1};
    const int a = b.vertex({-w * 0.5f, -h * 0.5f, 0}, n, {0, 1});
    const int c1 = b.vertex({w * 0.5f, -h * 0.5f, 0}, n, {1, 1});
    const int c2 = b.vertex({w * 0.5f, h * 0.5f, 0}, n, {1, 0});
    const int d = b.vertex({-w * 0.5f, h * 0.5f, 0}, n, {0, 0});
    b.quad(a, c1, c2, d);
    return b.build();
}

constexpr float HALO_M = 0.011f; // halo margin around a tile (metres)

// Soft glow (filled) or glowing outline ring around a tile-shaped rounded rectangle.
Texture2D buildHaloTexture(bool ring) {
    const float ppm = 2000.f; // pixels per metre
    const int w = (int)std::lround((TW + 2.f * HALO_M) * ppm), h = (int)std::lround((TH + 2.f * HALO_M) * ppm);
    Image img = GenImageColor(w, h, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    const float hx = TW * 0.5f * ppm, hy = TH * 0.5f * ppm, rad = 0.0026f * ppm;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float ux = (float)x + 0.5f - (float)w * 0.5f, uy = (float)y + 0.5f - (float)h * 0.5f;
            const float qx = std::fabs(ux) - (hx - rad), qy = std::fabs(uy) - (hy - rad);
            const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
            const float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - rad; // px, <0 inside
            float a;
            if (ring) {
                // a solid ~3 mm band hugging the tile edge, with a soft outer glow
                const float core = std::exp(-std::pow((d - 2.8f) / 3.3f, 4.f));
                const float glow = d > 0.f ? 0.5f * std::exp(-d / 9.f) : 0.5f * std::exp(d / 2.2f);
                a = std::max(core, glow);
            } else {
                a = d <= 0.f ? 1.f : std::pow(std::max(0.f, 1.f - d / (HALO_M * ppm)), 2.2f);
            }
            px[y * w + x].a = (unsigned char)std::clamp(a * 255.f, 0.f, 255.f);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

Texture2D buildStripTexture() {
    const int w = 128, h = 16;
    Image img = GenImageColor(w, h, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float v = ((float)y + 0.5f) / (float)h - 0.5f;
            const float ex = std::min((float)x + 0.5f, (float)w - (float)x - 0.5f) / 10.f;
            const float a = std::pow(std::clamp((0.5f - std::fabs(v)) / 0.24f, 0.f, 1.f), 1.4f) * std::min(1.f, ex);
            px[y * w + x].a = (unsigned char)std::clamp(a * 255.f, 0.f, 255.f);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

// Felt (çuha) for our table, 1 texel ~ 1 mm over the whole cloth: fine fibres and a slight nap, mottling,
// darker toward the rim, lighter worn lanes where the tiles are slid about, faint tea-glass rings and a
// small old cigarette burn near the ashtray.
float vnoise2(float x, float y, uint32_t seed) {
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    const float fx = x - (float)xi, fy = y - (float)yi;
    auto h = [&](int a, int b) { return hashF((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ seed * 83492791u); };
    const float u = fx * fx * (3.f - 2.f * fx), v = fy * fy * (3.f - 2.f * fy);
    const float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
float fbm2(float x, float y, int oct, uint32_t seed) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int o = 0; o < oct; ++o) {
        sum += amp * vnoise2(x, y, seed + (uint32_t)o * 101u);
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}

Texture2D buildFeltTexture() {
    const int N = 1024;
    const float H = w3d::FELT_HALF;
    Image img = GenImageColor(N, N, BLACK);
    Color* px = (Color*)img.data;
    const Vector3 base{30.f, 94.f, 60.f};
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            // world position of the texel (plane UV: u along +X, v along +Z)
            const float wx = -H + 2.f * H * ((float)x + 0.5f) / (float)N;
            const float wz = -H + 2.f * H * ((float)y + 0.5f) / (float)N;
            float k = 1.f;
            k *= 0.92f + 0.16f * fbm2(wx * 7.f, wz * 7.f, 4, 11u);                   // mottling
            k *= 0.94f + 0.12f * hashF((uint32_t)(x * 7919 + y * 104729));           // fibres
            k *= 0.97f + 0.06f * vnoise2(wx * 900.f, wz * 70.f, 5u);                 // nap streaks
            const float edge = std::max(std::fabs(wx), std::fabs(wz));
            k *= 1.f - 0.16f * smooth01(0.36f, 0.55f, edge);                         // darker by the rim
            float wear = 0.f;                                                        // slid-over lanes
            for (int s = 0; s < 4; ++s) {
                const Vector3 c = w3d::seatLocal(s, 0.f, 0.27f, 0.f);
                const Vector3 r = w3d::SEAT_RIGHT[s];
                const float dx = wx - c.x, dz = wz - c.z;
                const float along = dx * r.x + dz * r.z, across = dx * r.z - dz * r.x;
                wear = std::max(wear, std::exp(-along * along / 0.030f - across * across / 0.010f));
            }
            wear = std::max(wear, 0.8f * std::exp(-(wx * wx + wz * wz) / 0.016f));
            wear *= 0.6f + 0.4f * fbm2(wx * 16.f, wz * 16.f, 3, 29u);
            Vector3 c{base.x * k, base.y * k, base.z * k};
            const float grey = (c.x + c.y + c.z) / 3.f;
            c = {c.x + (grey - c.x) * 0.25f * wear + 8.f * wear, c.y + (grey - c.y) * 0.25f * wear + 9.f * wear,
                 c.z + (grey - c.z) * 0.25f * wear + 6.f * wear};
            // tea-glass rings by the corners
            for (int s = 0; s < 4; ++s) {
                const Vector3 g = w3d::GLASS_POS[s];
                const float ox = g.x * 0.86f - wx + 0.012f * (float)(s % 2), oz = g.z * 0.86f - wz;
                const float d = std::sqrt(ox * ox + oz * oz);
                const float ring = std::exp(-std::pow((d - 0.0235f) / 0.0016f, 2.f)) *
                                   (0.5f + 0.5f * std::sin(std::atan2(oz, ox) * 2.f + (float)s));
                c = {c.x * (1.f - 0.18f * ring), c.y * (1.f - 0.22f * ring), c.z * (1.f - 0.22f * ring)};
            }
            // an old burn mark near the ashtray corner
            {
                const float bx = wx + 0.335f, bz = wz + 0.455f;
                const float d = std::sqrt(bx * bx * 1.6f + bz * bz);
                const float burn = smooth01(0.0065f, 0.0015f, d);
                const float halo = std::exp(-std::pow((d - 0.006f) / 0.0025f, 2.f)) * 0.5f;
                c = {c.x * (1.f - 0.55f * burn) + 18.f * burn - 6.f * halo, c.y * (1.f - 0.75f * burn) - 8.f * halo,
                     c.z * (1.f - 0.8f * burn) - 5.f * halo};
            }
            px[y * N + x] = Color{(unsigned char)std::clamp(c.x, 0.f, 255.f), (unsigned char)std::clamp(c.y, 0.f, 255.f),
                                  (unsigned char)std::clamp(c.z, 0.f, 255.f), 255};
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

const Color kGlowColors[6] = {{255, 186, 56, 255},  {84, 220, 96, 255},  {86, 162, 255, 255},
                              {236, 64, 52, 255},   {255, 128, 36, 255}, {255, 232, 190, 255}};

} // namespace

void TableState::buildGfx(Renderer& r) {
    if (gfxReady) return;
    ui::tilegfx::init();
    const Texture2D atlas = ui::tilegfx::faceAtlas3D();
    const Rectangle body = ui::tilegfx::faceUV3D(ui::tilegfx::KEY_BODY);
    const Vector2 side{body.x + body.width * 0.5f, body.y + body.height * 0.5f};
    const Rectangle back = ui::tilegfx::faceUV3D(ui::tilegfx::KEY_BACK);
    for (int k = 0; k <= ui::tilegfx::KEY_BACK; ++k) tileMesh[k] = buildTileMesh(ui::tilegfx::faceUV3D(k), back, side);
    tableMesh = buildTable();
    feltMesh = buildFelt();
    rackHumanMesh = buildRack(rackProfile(true));
    rackBotMesh = buildRack(rackProfile(false));
    standMesh = buildStand();
    starQuad = buildQuad(0.0105f, 0.0105f);
    haloQuad = buildQuad(TW + 2.f * HALO_M, TH + 2.f * HALO_M);
    ringQuad = buildQuad(TW + 2.f * HALO_M, TH + 2.f * HALO_M);
    stripQuad = buildQuad(1.f, 1.f);

    texWood = genWoodTexture(512, Color{150, 96, 56, 255}, Color{78, 44, 22, 255}, 11u, 16.f);
    texFelt = buildFeltTexture();
    texRack = genWoodTexture(512, Color{182, 124, 72, 255}, Color{112, 68, 36, 255}, 23u, 26.f);
    texHalo = buildHaloTexture(false);
    texRing = buildHaloTexture(true);
    texStrip = buildStripTexture();

    matTile = r.makeMat(WHITE, atlas, 0.32f, 84.f, 0.f, 0.04f);
    matTileHi = r.makeMat(Color{255, 250, 238, 255}, atlas, 0.36f, 84.f, 0.14f, 0.12f);
    matWood = r.makeMat(WHITE, texWood, 0.32f, 38.f);
    matFelt = r.makeMat(WHITE, texFelt, 0.03f, 6.f, 0.f, 0.18f);
    matRack = r.makeMat(WHITE, texRack, 0.36f, 42.f);
    matStar = r.makeMat(WHITE, ui::tilegfx::starTexture3D(), 0.3f, 30.f, 0.35f);
    for (int c = 0; c < G_COUNT; ++c)
        for (int k = 0; k < 16; ++k) {
            const float a = (float)(k + 1) / 16.f;
            matGlow[c][k] = r.makeMat(kGlowColors[c], texHalo, 0.f, 8.f, 1.f);
            matGlow[c][k].alpha = a;
            matRing[c][k] = r.makeMat(kGlowColors[c], texRing, 0.f, 8.f, 1.f);
            matRing[c][k].alpha = a;
            matStrip[c][k] = r.makeMat(kGlowColors[c], texStrip, 0.f, 8.f, 1.f);
            matStrip[c][k].alpha = a;
        }
    gfxReady = true;
}

void TableState::freeGfx(Renderer& r) {
    if (!gfxReady) return;
    for (int k = 0; k <= ui::tilegfx::KEY_BACK; ++k) UnloadMesh(tileMesh[k]);
    for (Mesh* m : {&tableMesh, &feltMesh, &rackHumanMesh, &rackBotMesh, &standMesh, &starQuad, &haloQuad, &ringQuad, &stripQuad})
        UnloadMesh(*m);
    for (Texture2D* t : {&texWood, &texFelt, &texRack, &texHalo, &texRing, &texStrip}) UnloadTexture(*t);
    for (Mat* m : {&matTile, &matTileHi, &matWood, &matFelt, &matRack, &matStar}) r.unloadMat(*m);
    for (int c = 0; c < G_COUNT; ++c)
        for (int k = 0; k < 16; ++k) {
            r.unloadMat(matGlow[c][k]);
            r.unloadMat(matRing[c][k]);
            r.unloadMat(matStrip[c][k]);
        }
    ui::tilegfx::shutdown();
    gfxReady = false;
}

// ================================================================ submission
const Mat& TableState::step16(const Mat* arr, float amount) {
    const int k = std::clamp((int)std::lround(amount * 16.f) - 1, 0, 15);
    return arr[k];
}

// Soft additive glow lying on/under a pose (light pooling on the felt).
void TableState::submitGlow(Renderer& r, const Pose& p, int color, float amount, float grow) {
    if (amount < 0.035f) return;
    Pose h = p;
    h.scale = p.scale * grow;
    r.submit(&haloQuad, &step16(matGlow[std::clamp(color, 0, G_COUNT - 1)], amount), poseMatrix(h),
             Transparent | Additive | DoubleSided | NoFog);
    ++lastSubmits;
}

// Crisp outline ring around a tile-sized pose (the quad lies in the pose's XY plane).
void TableState::submitRing(Renderer& r, const Pose& p, int color, float amount, float grow) {
    if (amount < 0.035f) return;
    Pose h = p;
    h.scale = p.scale * grow;
    r.submit(&ringQuad, &step16(matRing[std::clamp(color, 0, G_COUNT - 1)], amount), poseMatrix(h),
             Transparent | DoubleSided | NoFog);
    ++lastSubmits;
}

// A glowing strip from a to b lying in the plane with `normal`.
void TableState::submitStrip(Renderer& r, Vector3 a, Vector3 b, Vector3 normal, float width, int color, float amount) {
    if (amount < 0.035f) return;
    const Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    if (len < 1e-4f) return;
    const Vector3 x = Vector3Scale(d, 1.f / len);
    const Vector3 z = Vector3Normalize(normal);
    const Vector3 y = Vector3CrossProduct(z, x);
    Matrix m = QuaternionToMatrix(quatFromAxes(x, y, z));
    m = MatrixMultiply(MatrixScale(len, width, 1.f), m);
    const Vector3 c = Vector3Scale(Vector3Add(a, b), 0.5f);
    m.m12 = c.x, m.m13 = c.y, m.m14 = c.z;
    r.submit(&stripQuad, &step16(matStrip[std::clamp(color, 0, G_COUNT - 1)], amount), m, Transparent | DoubleSided | NoFog);
    ++lastSubmits;
}

void TableState::submitTile(Renderer& r, int id) {
    const TileVis& v = vis[id];
    const Target& t = tgt[id];
    const int dragged = draggedTileId();
    if (t.hidden && !v.flying && v.cont == t.cont && id != dragged) return; // buried in a discard stack
    Pose p = v.pose;
    if (!v.flying && id != dragged && v.landT < 0.4f && v.landAmp > 0.f) {
        const float rock = v.landAmp * DEG2RAD * std::exp(-14.f * v.landT) * std::sin(v.landT * 46.f);
        p.rot = QuaternionMultiply(p.rot, QuaternionFromAxisAngle({1, 0, 0}, rock));
    }
    const bool priv = privTile[id];
    const int key = priv ? ui::tilegfx::KEY_BACK : ui::tilegfx::faceKeyOf(id);
    const bool mine = t.cont == C_HAND + human;
    const bool hot = mine && (v.glow > 0.05f || id == game->pendingLeftTile() || (id == newTile && now - newTileTime < 2.2f));
    r.submit(&tileMesh[key], hot || id == dragged ? &matTileHi : &matTile, poseMatrix(p), CastShadow);
    ++lastSubmits;
    if (!priv && ok().isJoker(id)) { // okey badge
        Pose sp = p;
        sp.pos = poseToWorld(p, {TW * 0.5f - 0.0063f, TH * 0.5f - 0.0063f, TT * 0.5f + 0.00035f});
        r.submit(&starQuad, &matStar, poseMatrix(sp), Transparent);
        ++lastSubmits;
    }
}

void TableState::submitAll(Renderer& r) {
    lastSubmits = 0;
    auto sub = [&](const Mesh* m, const Mat* mat, const Matrix& xf, uint32_t flags) {
        r.submit(m, mat, xf, flags);
        ++lastSubmits;
    };
    sub(&tableMesh, &matWood, MatrixIdentity(), CastShadow);
    sub(&feltMesh, &matFelt, MatrixIdentity(), 0);
    sub(&standMesh, &matRack, MatrixIdentity(), CastShadow);
    for (int s = 0; s < 4; ++s) {
        float ang = 0.f;
        if (revealed && s != human) {
            const float u = std::clamp((now - revealAt - 0.18f * (float)((s - human + 4) % 4 - 1)) / 0.85f, 0.f, 1.f);
            ang = ui::easeInOutCubic(u) * PI;
        }
        const Vector3 c{w3d::SEAT_DIR[s].x * w3d::RACK_DIST, 0.f, w3d::SEAT_DIR[s].z * w3d::RACK_DIST};
        const Matrix m = MatrixMultiply(MatrixRotateY(w3d::seatYawDeg(s) * DEG2RAD + ang), MatrixTranslate(c.x, c.y, c.z));
        sub(s == human ? &rackHumanMesh : &rackBotMesh, &matRack, m, CastShadow);
    }
    if (!game) return;
    for (int id = 0; id < NUM_TILES; ++id) submitTile(r, id);
    if (!started()) return;

    const float pulse = 0.5f + 0.5f * std::sin(now * 4.2f);
    const int dragged = draggedTileId();
    // the human's rack: a crisp outline on the hovered / selected / pending / new tile
    for (int i = 0; i < SLOTS; ++i) {
        const int id = slots[i];
        if (id < 0 || id == dragged || tgt[id].cont != C_HAND + human || vis[id].flying) continue;
        const TileVis& v = vis[id];
        float amt = v.glow;
        int col = G_GOLD;
        if (id == game->pendingLeftTile()) {
            amt = std::max(amt, 0.55f + 0.45f * pulse);
            col = G_ORANGE;
        } else if (id == newTile && now - newTileTime < 2.4f && v.glow < 0.3f) {
            amt = 0.9f * (1.f - (now - newTileTime) / 2.4f);
            col = G_WARM;
        }
        Pose rp = v.pose;
        rp.pos = poseToWorld(v.pose, {0.f, 0.f, TT * 0.5f + 0.0004f});
        submitRing(r, rp, col, amt, 1.f);
    }
    // what can be taken now
    const bool drawStage = canAct() && game->stage() == okey::TurnStage::NeedDraw && press.kind == Press::None;
    if (!pileOrder.empty() && (hoverPile || (drawStage && game->pileCount() > 0))) {
        const int top = pileOrder.back();
        Pose p = vis[top].pose;
        p.pos.y += TT * 0.5f + 0.0005f;
        p.rot = flatFaceUp(0.f);
        submitRing(r, p, G_GOLD, hoverPile ? 1.f : 0.5f + 0.4f * pulse, 1.f);
        // warm light pooling on the felt around the whole pile
        Pose g;
        g.pos = {w3d::PILE_POS.x, w3d::TABLE_Y + 0.0004f, w3d::PILE_POS.z};
        g.rot = flatFaceUp(0.f);
        submitGlow(r, g, G_GOLD, hoverPile ? 0.55f : 0.16f + 0.14f * pulse, 1.85f);
    }
    const int leftTop = game->topDiscard(leftSeat());
    if (leftTop >= 0 && (hoverLeft || (drawStage && game->canTakeFromLeft(human)))) {
        Pose p = vis[leftTop].pose;
        p.pos = poseToWorld(p, {0.f, 0.f, TT * 0.5f + 0.0005f});
        submitRing(r, p, G_GOLD, hoverLeft ? 1.f : 0.45f + 0.4f * pulse, 1.f);
        Pose g = p;
        g.pos = {w3d::DISCARD_POS[leftSeat()].x, w3d::TABLE_Y + 0.0004f, w3d::DISCARD_POS[leftSeat()].z};
        submitGlow(r, g, G_GOLD, hoverLeft ? 0.5f : 0.12f + 0.12f * pulse, 1.35f);
    }
    // drop targets while a rack tile is held (or selected)
    const bool holding = (press.dragging && press.kind == Press::RackTile) || (selected >= 0 && press.kind == Press::None);
    if (holding && canAct() && game->stage() == okey::TurnStage::Play) {
        const int held = press.dragging ? press.tile : selected;
        const int top = game->topDiscard(human);
        Pose p;
        p.pos = w3d::DISCARD_POS[human];
        p.pos.y = w3d::TABLE_Y + 0.0006f;
        if (top >= 0) p.pos.y = vis[top].pose.pos.y + TT * 0.5f + 0.0006f;
        p.rot = flatFaceUp(0.f);
        const float amt = aim.overDiscard ? 1.f : (press.dragging ? 0.45f + 0.35f * pulse : 0.25f + 0.25f * pulse);
        submitRing(r, p, G_GOLD, amt, aim.overDiscard ? 1.22f : 1.14f);
        if (aim.overDiscard) {
            p.pos.y = w3d::TABLE_Y + 0.0005f;
            submitGlow(r, p, G_GOLD, 0.6f, 1.25f);
        }
        if (press.dragging && aim.meld >= 0 && aim.meld < (int)boxes.size() && held >= 0) {
            bool swap = false;
            const bool fits = isleFits(held, aim.meld, aim.meldJoker, &swap);
            const bool legal = fits && game->canWorkTable(human);
            // green: goes on now; orange: fits but not yet (not opened / opened this turn); red: never fits
            const int ring = legal ? G_GREEN : (fits ? G_ORANGE : G_RED);
            const MeldBox& b = boxes[aim.meld];
            const Meld& md = game->table()[aim.meld];
            for (int k = 0; k < md.size() && k < (int)b.pos.size(); ++k) {
                if (swap && k != aim.meldJoker) continue;
                Pose tp = vis[md.tiles[k].id].pose;
                tp.pos = poseToWorld(tp, {0.f, 0.f, TT * 0.5f + 0.0005f});
                submitRing(r, tp, ring, swap ? 1.f : 0.8f, 1.f);
            }
            if (legal && !swap && md.kind == MeldKind::Run) {
                const bool front = isleFront(held, aim.meld, aim.meldFront);
                const float x = front ? b.x0 - 0.0065f * b.scale : b.x1 + 0.0065f * b.scale;
                const float y = w3d::TABLE_Y + 0.0008f;
                submitStrip(r, {x, y, b.z0 - 0.003f}, {x, y, b.z1 + 0.003f}, {0, 1, 0}, 0.007f, G_GREEN, 1.f);
            }
        }
    }
    // valid groups on the istaka: glowing strips on the ledge lip under them
    if (hints && playing() && !dealing()) {
        for (size_t i = 0; i < groups.size(); ++i) {
            if (groupKind[i] == 0) continue;
            const RackGroup& g = groups[i];
            bool moving = false;
            for (int id : g.ids)
                if (id == dragged || vis[id].flying || tgt[id].cont != C_HAND + human) moving = true;
            if (moving) continue;
            Vector3 c, n, rt, up;
            rackRowFrame(human, true, g.row, c, n, rt, up);
            const Vector3 base = Vector3Add(c, Vector3Add(Vector3Scale(up, -TH * 0.5f + 0.0012f), Vector3Scale(n, TT * 0.5f + 0.0112f)));
            const Vector3 a = Vector3Add(base, Vector3Scale(rt, slotRight(g.col) - PITCH * 0.5f + 0.002f));
            const Vector3 b = Vector3Add(base, Vector3Scale(rt, slotRight(g.col + g.len - 1) + PITCH * 0.5f - 0.002f));
            submitStrip(r, a, b, n, 0.0058f, groupKind[i] == 1 ? G_GREEN : G_BLUE, 1.f);
        }
    }
    // freshly laid melds glow for a moment; işleme pulses
    const std::vector<Meld>& table = game->table();
    for (int i = 0; i < (int)table.size() && i < (int)boxes.size(); ++i) {
        const float born = i < (int)meldBorn.size() ? now - meldBorn[i] : 100.f;
        const float pt = i < (int)meldPulse.size() ? now - meldPulse[i] : 100.f;
        float amt = 0.f;
        int col = G_GOLD;
        if (born < 2.6f) amt = born < 0.3f ? born / 0.3f : 1.f - (born - 0.3f) / 2.3f;
        if (pt < 0.9f) {
            amt = std::max(amt, 1.f - pt / 0.9f);
            col = G_GREEN;
        }
        if (amt <= 0.03f) continue;
        for (const okey::PlacedTile& pt2 : table[i].tiles) {
            if (vis[pt2.id].flying) continue;
            Pose hp = vis[pt2.id].pose;
            hp.pos.y = w3d::TABLE_Y + 0.0004f;
            hp.rot = flatFaceUp(0.f);
            submitGlow(r, hp, col, amt * 0.7f, 1.f);
        }
    }
    // hover peek: outline the meld / discard pile being inspected
    if (peekKind == 1 && peekIdx >= 0 && peekIdx < (int)table.size()) {
        for (const okey::PlacedTile& pt2 : table[peekIdx].tiles) {
            Pose tp = vis[pt2.id].pose;
            tp.pos = poseToWorld(tp, {0.f, 0.f, TT * 0.5f + 0.0005f});
            submitRing(r, tp, G_WARM, 0.55f, 1.f);
        }
    }
    // a soft fill from above the player's side so the istaka's faces read well under the pendant. It hangs
    // well above the eye: a light near the camera would mirror straight back off the leaning tile faces and
    // wash the numbers out.
    PointLight fill;
    fill.position = {0.f, 2.02f, 1.08f};
    fill.color = Color{255, 220, 182, 255};
    fill.intensity = 0.7f;
    fill.range = 2.6f;
    r.addPointLight(fill);
}

} // namespace r3d
