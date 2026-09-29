// Room module: the kahvehane cat (room owner).
//
// A street cat that has adopted the place: it naps curled up by the stove or on an empty chair, wakes, looks
// around (now and then at you), grooms, stretches, and strolls to another favourite spot: the floor under the
// scoreboard, the doormat by the street door (where it watches the rain), or another chair it jumps onto.
// Very rarely a soft "miyav". It never comes near our table (Table3D's) and keeps off the furniture: walks are
// planned on a 10 cm occupancy grid of the floor (A* + string pulling).
//
// The body is a small procedural rig (all meshes built once): two torso ellipsoids that pitch (sitting) and
// bend sideways (curling up), a neck, a head with ears and muzzle, two-bone IK legs with paws and an
// eight-segment tail. Every behaviour is a target pose; the current pose eases toward it, so waking up,
// sitting down and curling up are smooth morphs rather than canned clips. The walk is a lateral-sequence gait.
#include "r3d/RoomInternal.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace r3d {
namespace rm {

namespace {

constexpr float TAU = 2.f * PI;
constexpr float SPINE = 0.21f;                   // hip joint to shoulder joint
constexpr float F_UP = 0.1f, F_LO = 0.1f;        // front leg: upper arm, forearm
constexpr float H_UP = 0.11f, H_LO = 0.12f;      // hind leg: thigh, shank (+ hock)
constexpr int TAIL_N = 8;
constexpr float TAIL_SEG = 0.039f;

float approach(float cur, float tgt, float rate, float dt) { return cur + (tgt - cur) * (1.f - std::exp(-rate * dt)); }
float wrapPi(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
float easeInOut(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
Vector3 ry(Vector3 v, float a) {  // rotate about +Y (raylib MatrixRotateY convention: +Z -> (sin a, 0, cos a))
    float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}
Matrix bodyRot(float yaw, float pitchUp, float roll = 0.f) {
    return MatrixMultiply(MatrixMultiply(MatrixRotateZ(roll), MatrixRotateX(-pitchUp)), MatrixRotateY(yaw));
}
// Rotation taking +Y to `dir` (unit).
Matrix alignY(Vector3 dir) {
    Vector3 y{0, 1, 0};
    Vector3 axis = Vector3CrossProduct(y, dir);
    float s = Vector3Length(axis), c = Vector3DotProduct(y, dir);
    if (s < 1e-6f) return c > 0.f ? MatrixIdentity() : MatrixRotateX(PI);
    return MatrixRotate(Vector3Scale(axis, 1.f / s), std::atan2(s, c));
}
// Two-bone IK: joint between root r and target t (clamped to reach), bending toward `pole`.
Vector3 ik2(Vector3 r, Vector3& t, float a, float b, Vector3 pole) {
    Vector3 d = Vector3Subtract(t, r);
    float L = Vector3Length(d);
    Vector3 dir = L > 1e-6f ? Vector3Scale(d, 1.f / L) : Vector3{0, -1, 0};
    float Lc = std::clamp(L, std::fabs(a - b) + 1e-3f, a + b - 1e-3f);
    if (Lc != L) t = Vector3Add(r, Vector3Scale(dir, Lc));
    float x = (a * a - b * b + Lc * Lc) / (2.f * Lc);
    float h = std::sqrt(std::max(0.f, a * a - x * x));
    Vector3 pp = Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir)));
    float pl = Vector3Length(pp);
    pp = pl > 1e-6f ? Vector3Scale(pp, 1.f / pl) : Vector3{0, 0, 1};
    return Vector3Add(r, Vector3Add(Vector3Scale(dir, x), Vector3Scale(pp, h)));
}
// Rewrites a built mesh's texture coordinates (u, v) -> (u * su + ou, v * sv + ov) and re-uploads them.
void remapUV(Mesh& m, float su, float sv, float ou, float ov) {
    if (!m.texcoords) return;
    for (int i = 0; i < m.vertexCount; ++i) {
        m.texcoords[2 * i] = m.texcoords[2 * i] * su + ou;
        m.texcoords[2 * i + 1] = m.texcoords[2 * i + 1] * sv + ov;
    }
    UpdateMeshBuffer(m, 1, m.texcoords, m.vertexCount * 2 * (int)sizeof(float), 0);
}

// ---------------------------------------------------------------------------- coats
struct Coat {
    Color base, stripe, belly, muzzle, paw, nose, eye;
    float stripes;  // 0 = none (tuxedo)
    bool whiteLegs;
};
const Coat kCoats[3] = {
    // sarman: ginger mackerel tabby
    {{206, 124, 58, 255}, {146, 70, 30, 255}, {238, 200, 150, 255}, {240, 212, 172, 255}, {228, 178, 124, 255}, {214, 128, 118, 255}, {178, 164, 58, 255}, 0.85f, false},
    // tekir: grey-brown tabby
    {{126, 118, 102, 255}, {50, 46, 40, 255}, {200, 190, 170, 255}, {210, 200, 182, 255}, {150, 140, 124, 255}, {170, 112, 108, 255}, {150, 166, 72, 255}, 0.9f, false},
    // black & white (tuxedo)
    {{34, 32, 35, 255}, {34, 32, 35, 255}, {236, 234, 228, 255}, {238, 236, 230, 255}, {236, 234, 228, 255}, {196, 128, 128, 255}, {186, 172, 64, 255}, 0.f, true},
};

// Fur: u around the body (0 = belly, 0.5 = spine), v along it (front to back). Mackerel stripes run around the
// body and fade toward the pale belly; a dark line runs down the spine; a light bib at the throat.
Texture2D genFurTexture(const Coat& c, uint32_t seed) {
    const int N = 256;
    Image img = GenImageColor(N, N, WHITE);
    Color* px = (Color*)img.data;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float dorsal = std::cos(TAU * (u - 0.5f));  // 1 on the back, -1 under the belly
            Color col = c.base;
            float belly = smooth01(0.05f, c.stripes > 0.f ? 0.75f : 0.45f, -dorsal);
            float bib = (1.f - smooth01(0.1f, 0.3f, v)) * smooth01(-0.4f, 0.2f, -dorsal);
            col = mix(col, c.belly, std::max(belly, bib));
            if (c.stripes > 0.f) {
                float w = v * 8.f + 0.1f * std::sin(TAU * u * 3.f + v * 5.f) + (fbm(u * 7.f, v * 7.f, 3, seed) - 0.5f) * 0.35f;
                float s = 0.5f + 0.5f * std::cos(TAU * w);
                float stripe = smooth01(0.6f, 0.9f, s) * smooth01(-0.45f, 0.25f, dorsal) * c.stripes;
                stripe = std::max(stripe, smooth01(0.88f, 0.98f, dorsal) * 0.75f * c.stripes);
                col = mix(col, c.stripe, stripe);
            }
            float k = 0.9f + 0.16f * hash1((uint32_t)(x * 7919 + y * 104729) ^ seed) + 0.06f * (fbm(u * 30.f, v * 30.f, 2, seed + 9) - 0.5f);
            px[y * N + x] = scaleRgb(col, k);
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// Soft contact shadow under the cat.
Texture2D genBlobTexture() {
    const int N = 64;
    Image img = GenImageColor(N, N, Color{0, 0, 0, 0});
    Color* px = (Color*)img.data;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float dx = (x + 0.5f) / N * 2.f - 1.f, dy = (y + 0.5f) / N * 2.f - 1.f;
            float r2 = dx * dx + dy * dy;
            float a = r2 >= 1.f ? 0.f : (1.f - r2) * (1.f - r2);
            px[y * N + x] = Color{0, 0, 0, (unsigned char)(a * 255.f)};
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// ---------------------------------------------------------------------------- the floor the cat walks on
struct FloorGrid {
    static constexpr float CELL = 0.1f;
    int nx = 0, nz = 0;
    float x0 = w3d::ROOM_X0, z0 = w3d::ROOM_Z0;
    std::vector<uint8_t> blocked;

    int ix(float x) const { return (int)std::floor((x - x0) / CELL); }
    int iz(float z) const { return (int)std::floor((z - z0) / CELL); }
    bool bad(int i, int k) const { return i < 0 || k < 0 || i >= nx || k >= nz || blocked[(size_t)(k * nx + i)]; }
    bool freeAt(Vector2 p) const { return !bad(ix(p.x), iz(p.y)); }
    Vector2 centre(int i, int k) const { return {x0 + (i + 0.5f) * CELL, z0 + (k + 0.5f) * CELL}; }

    void build(const std::vector<Vector3>& chairs) {
        nx = (int)std::ceil((w3d::ROOM_X1 - w3d::ROOM_X0) / CELL);
        nz = (int)std::ceil((w3d::ROOM_Z1 - w3d::ROOM_Z0) / CELL);
        blocked.assign((size_t)(nx * nz), 0);
        const float m = 0.12f;  // half the cat's width plus a little clearance
        auto box = [&](float xa, float za, float xb, float zb) {
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) {
                    Vector2 c = centre(i, k);
                    if (c.x > xa - m && c.x < xb + m && c.y > za - m && c.y < zb + m) blocked[(size_t)(k * nx + i)] = 1;
                }
        };
        auto disc = [&](float x, float z, float r) {
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) {
                    Vector2 c = centre(i, k);
                    if ((c.x - x) * (c.x - x) + (c.y - z) * (c.y - z) < (r + m) * (r + m)) blocked[(size_t)(k * nx + i)] = 1;
                }
        };
        // walls
        box(-100.f, -100.f, w3d::ROOM_X0 + 0.04f, 100.f);
        box(w3d::ROOM_X1 - 0.04f, -100.f, 100.f, 100.f);
        box(-100.f, -100.f, 100.f, w3d::ROOM_Z0 + 0.06f);
        box(-100.f, w3d::ROOM_Z1 - 0.06f, 100.f, 100.f);
        // our table, our four chairs and the players' legs (Table3D's and Characters' ground): a wide berth
        disc(0.f, 0.f, 1.35f);
        // background tables and every chair (with their sitters' legs)
        for (const w3d::BgTable& t : w3d::BG_TABLES) box(t.x - 0.47f, t.z - 0.47f, t.x + 0.47f, t.z + 0.47f);
        for (const Vector3& c : chairs) box(c.x - 0.22f, c.z - 0.22f, c.x + 0.22f, c.z + 0.22f);
        // counter, stove, coat rack, wall bench, rubber plant
        box(2.03f, -3.5f, 3.97f, -2.93f);
        disc(3.8f, 2.55f, 0.42f);
        disc(-2.95f, 3.25f, 0.26f);
        box(-2.17f, 3.13f, 0.47f, 3.7f);
        disc(0.52f, -3.18f, 0.24f);
    }

    bool lineFree(Vector2 a, Vector2 b) const {
        float d = std::hypot(b.x - a.x, b.y - a.y);
        int n = std::max(1, (int)(d / 0.04f));
        for (int i = 0; i <= n; ++i) {
            float t = (float)i / n;
            if (!freeAt({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) return false;
        }
        return true;
    }
    // nearest free cell (breadth-first)
    bool nearestFree(int& i, int& k) const {
        if (!bad(i, k)) return true;
        for (int r = 1; r < 12; ++r)
            for (int dk = -r; dk <= r; ++dk)
                for (int di = -r; di <= r; ++di) {
                    if (std::max(std::abs(di), std::abs(dk)) != r) continue;
                    if (!bad(i + di, k + dk)) {
                        i += di;
                        k += dk;
                        return true;
                    }
                }
        return false;
    }
    // A* over the 8-connected grid (no corner cutting), then string pulling. Returns waypoints after `from`.
    std::vector<Vector2> path(Vector2 from, Vector2 to) const {
        int si = ix(from.x), sk = iz(from.y), gi = ix(to.x), gk = iz(to.y);
        if (!nearestFree(si, sk) || !nearestFree(gi, gk)) return {to};
        const int N = nx * nz;
        std::vector<float> g((size_t)N, 1e9f);
        std::vector<int> prev((size_t)N, -1);
        using QE = std::pair<float, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
        auto hfn = [&](int i, int k) {
            float dx = (float)std::abs(i - gi), dz = (float)std::abs(k - gk);
            return std::max(dx, dz) + 0.4142f * std::min(dx, dz);
        };
        const int s = sk * nx + si, goal = gk * nx + gi;
        g[(size_t)s] = 0.f;
        open.push({hfn(si, sk), s});
        while (!open.empty()) {
            auto [f, c] = open.top();
            open.pop();
            if (c == goal) break;
            int ci = c % nx, ck = c / nx;
            if (f - hfn(ci, ck) > g[(size_t)c] + 1e-4f) continue;
            for (int dk = -1; dk <= 1; ++dk)
                for (int di = -1; di <= 1; ++di) {
                    if (!di && !dk) continue;
                    int ni = ci + di, nk = ck + dk;
                    if (bad(ni, nk)) continue;
                    if (di && dk && (bad(ci + di, ck) || bad(ci, ck + dk))) continue;
                    float ng = g[(size_t)c] + ((di && dk) ? 1.4142f : 1.f);
                    int n = nk * nx + ni;
                    if (ng < g[(size_t)n]) {
                        g[(size_t)n] = ng;
                        prev[(size_t)n] = c;
                        open.push({ng + hfn(ni, nk), n});
                    }
                }
        }
        if (prev[(size_t)goal] < 0 && goal != s) return {to};
        std::vector<Vector2> cells;
        for (int c = goal; c >= 0; c = prev[(size_t)c]) {
            cells.push_back(centre(c % nx, c / nx));
            if (c == s) break;
        }
        std::reverse(cells.begin(), cells.end());
        cells.front() = from;
        cells.back() = to;
        std::vector<Vector2> out;
        size_t i = 0;
        while (i + 1 < cells.size()) {
            size_t j = cells.size() - 1;
            while (j > i + 1 && !lineFree(cells[i], cells[j])) --j;
            out.push_back(cells[j]);
            i = j;
        }
        return out;
    }
};

// ---------------------------------------------------------------------------- pose
struct Pose {
    float hipH = 0.205f, hipZ = -0.1f, pitch = -0.03f, bend = 0.f;
    float neckUp = 0.085f, headFwd = 0.11f, headYaw = 0.f, headPitch = 0.1f, headRoll = 0.f;
    float frontFold = 0.f, hindFold = 0.f, reach = 0.f;
    float tailLift = 1.0f, tailCurve = 0.9f, tailSide = 0.f, tailWrap = 0.f;
    float eyes = 1.f, breathe = 0.f, groom = 0.f;
};
Pose poseStand() { return Pose{}; }
Pose poseSit() {
    Pose p;
    p.hipH = 0.08f, p.pitch = 0.78f, p.neckUp = 0.1f, p.headFwd = 0.075f, p.headPitch = -0.05f;
    p.hindFold = 1.f, p.tailLift = -0.3f, p.tailCurve = 0.f, p.tailSide = 2.3f, p.tailWrap = 1.f;
    return p;
}
Pose poseLoaf() {
    Pose p;
    p.hipH = 0.088f, p.pitch = -0.02f, p.bend = 0.12f, p.neckUp = 0.072f, p.headFwd = 0.1f, p.headPitch = 0.f;
    p.frontFold = 1.f, p.hindFold = 1.f, p.tailLift = -0.2f, p.tailCurve = 0.f, p.tailSide = 1.4f, p.tailWrap = 0.85f, p.breathe = 0.5f;
    return p;
}
Pose poseCurl() {
    Pose p;
    p.hipH = 0.076f, p.pitch = -0.02f, p.bend = 1.55f, p.neckUp = -0.012f, p.headFwd = 0.085f;
    p.headYaw = 0.95f, p.headPitch = 0.45f, p.headRoll = 0.35f;
    p.frontFold = 1.f, p.hindFold = 1.f, p.tailLift = -0.15f, p.tailCurve = 0.f, p.tailSide = 2.6f, p.tailWrap = 1.f;
    p.eyes = 0.f, p.breathe = 1.f;
    return p;
}
Pose poseBow() {  // the front half of a stretch: forelegs reaching, chest down, hips up
    Pose p;
    p.hipH = 0.21f, p.pitch = -0.42f, p.neckUp = 0.055f, p.headFwd = 0.12f, p.headPitch = -0.25f;
    p.reach = 1.f, p.tailLift = 1.25f, p.tailCurve = 0.3f, p.eyes = 0.4f;
    return p;
}

// ---------------------------------------------------------------------------- spots
enum class SpotKind { FloorSleep, FloorSit, Chair };
struct Spot {
    SpotKind kind;
    Vector2 p;         // floor spots: where it sits / sleeps
    float yaw;         // facing when it settles
    int chair = -1;    // chair spots: index into Room::Impl::chairs
    Vector2 approach;  // chair spots: floor point it jumps from / to
    float weight;
};

enum class Act { Sleep, Loaf, Sit, Groom, Stretch, Walk, Settle, JumpUp, JumpDown, Turn };

}  // namespace

struct Cat {
    Coat coat{};
    Texture2D fur{}, blobTex{};
    Mat mFur, mHead, mEye, mBlob;
    Mesh hips{}, chest{}, belly{}, neck{}, head{}, eyes{}, fUp{}, fLo{}, hUp{}, hLo{}, paw{}, tail{}, blob{};
    FloorGrid grid;
    std::vector<Spot> spots;

    // state
    Vector3 pos{};  // root: centre of the body on the surface it stands on
    float yaw = 0.f;
    Act act = Act::Sleep;
    float actT = 0.f, actDur = 30.f;
    int spot = 0, target = -1;
    bool onChair = false;
    std::vector<Vector2> path;
    size_t pathI = 0;
    float speed = 0.f, gait = 0.f, walkBlend = 0.f;
    Pose cur, tgt;
    float poseRate = 2.5f;
    Vector3 jumpFrom{}, jumpTo{};
    float jumpYaw = 0.f, turnFrom = 0.f;
    float lookT = 1.f, lookYaw = 0.f, lookPitch = 0.f;
    bool lookAtCam = false;
    float blinkT = 3.f, blink = 0.f;
    float flickT = 8.f, flick = 0.f, flickSide = 1.f;
    float meowCool = 40.f;
    float breathPh = 0.f;
    Vector3 camPos{0, 1.2f, 0.8f};
    int stepPhase = 0;
};

}  // namespace rm

using namespace rm;

namespace {

// ---------------------------------------------------------------------------- meshes
void buildCatMeshes(Cat& c) {
    const Coat& k = c.coat;
    MeshBuilder b;
    // torso: two ellipsoids with their poles along +Z (u = 0 under the belly, v from the front pole backward)
    b.setTransform(MatrixRotateX(PI * 0.5f));
    b.ellipsoid({0, 0, 0}, {0.071f, 0.132f, 0.079f}, 14, 18, WHITE);  // (x, along, height) before the rotation
    b.resetTransform();
    c.hips = b.build(true);
    remapUV(c.hips, 1.f, 0.5f, 0.f, 0.5f);  // the back half of the fur (stripes continue from the chest)
    b.clear();
    b.setTransform(MatrixRotateX(PI * 0.5f));
    b.ellipsoid({0, 0, 0}, {0.064f, 0.13f, 0.077f}, 14, 18, WHITE);
    b.resetTransform();
    c.chest = b.build(true);
    remapUV(c.chest, 1.f, 0.5f, 0.f, 0.f);
    b.clear();
    // the waist: fills the outside of the bend when it curls up
    b.setTransform(MatrixRotateX(PI * 0.5f));
    b.ellipsoid({0, 0, 0}, {0.066f, 0.1f, 0.074f}, 12, 16, WHITE);
    b.resetTransform();
    c.belly = b.build(true);
    remapUV(c.belly, 1.f, 0.4f, 0.f, 0.3f);
    b.clear();
    b.capsule({0, 0, 0}, {0, 0.07f, 0}, 0.04f, 12, WHITE);
    c.neck = b.build(true);
    remapUV(c.neck, 0.f, 0.f, 0.3f, 0.2f);  // plain flank colour, no stripe ring (it read as a collar)
    b.clear();
    // head (vertex colours, plain material): skull, cheeks, whisker pads, chin, nose, ears
    {
        const Color base = k.base, muzzle = k.muzzle;
        b.ellipsoid({0, 0, 0}, {0.05f, 0.044f, 0.047f}, 12, 16, base);
        b.ellipsoid({0, -0.012f, 0.012f}, {0.056f, 0.033f, 0.04f}, 10, 16, base);
        for (int sd = -1; sd <= 1; sd += 2) b.ellipsoid({sd * 0.013f, -0.019f, 0.043f}, {0.017f, 0.013f, 0.014f}, 8, 10, muzzle);
        b.ellipsoid({0, -0.031f, 0.031f}, {0.014f, 0.01f, 0.014f}, 6, 8, muzzle);
        b.ellipsoid({0, -0.006f, 0.052f}, {0.0085f, 0.006f, 0.006f}, 6, 8, k.nose);
        if (k.stripes > 0.f)  // forehead "M": three dark streaks
            for (int s = -1; s <= 1; ++s)
                b.ellipsoid({s * 0.011f, 0.036f, 0.026f - std::abs(s) * 0.004f}, {0.0022f, 0.01f, 0.011f}, 5, 6, mix(k.stripe, base, 0.35f));
        // ears: a pyramid each, pink inside
        for (int sd = -1; sd <= 1; sd += 2) {
            Vector3 fo{sd * 0.018f, 0.036f, 0.012f}, fi{sd * 0.043f, 0.024f, 0.008f}, bk{sd * 0.032f, 0.034f, -0.018f};
            Vector3 apex{sd * 0.041f, 0.084f, -0.006f};
            Vector3 innerApex = Vector3Add(apex, {-sd * 0.004f, -0.008f, 0.004f});
            auto face = [&](Vector3 a, Vector3 bb, Vector3 cc, Color col) {
                Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(bb, a), Vector3Subtract(cc, a)));
                int i0 = b.vertex(a, n, {0, 0}, col), i1 = b.vertex(bb, n, {1, 0}, col), i2 = b.vertex(cc, n, {0.5f, 1}, col);
                b.triangle(i0, i1, i2);
            };
            // outward winding depends on the side
            if (sd > 0) {
                face(fi, bk, apex, base);
                face(bk, fo, apex, base);
                face(fo, fi, apex, base);
                face(Vector3Lerp(fo, fi, 0.15f), Vector3Lerp(fi, fo, 0.15f), innerApex, scaleRgb(k.nose, 0.8f));
            } else {
                face(bk, fi, apex, base);
                face(fo, bk, apex, base);
                face(fi, fo, apex, base);
                face(Vector3Lerp(fi, fo, 0.15f), Vector3Lerp(fo, fi, 0.15f), innerApex, scaleRgb(k.nose, 0.8f));
            }
        }
        c.head = b.build(true);
        b.clear();
        for (int sd = -1; sd <= 1; sd += 2) {
            b.setTransform(MatrixMultiply(MatrixRotateY(sd * 0.4f), MatrixTranslate(sd * 0.022f, 0.008f, 0.042f)));
            b.ellipsoid({0, 0, 0}, {0.012f, 0.0095f, 0.0075f}, 8, 10, WHITE);
            b.resetTransform();
        }
        c.eyes = b.build(true);
        b.clear();
    }
    // legs (built along +Y from the joint): tapered
    auto seg = [&](float len, float r0, float r1) {
        std::vector<Vector2> prof;
        const int n = 6;
        for (int i = 0; i <= n; ++i) {
            float t = (float)i / n;
            float r = r0 + (r1 - r0) * t;
            float capA = i == 0 ? 0.6f : 1.f, capB = i == n ? 0.6f : 1.f;
            prof.push_back({r * std::min(capA, capB), len * t});
        }
        prof.insert(prof.begin(), {0.f, -r0 * 0.5f});
        prof.push_back({0.f, len + r1 * 0.5f});
        MeshBuilder m;
        m.lathe(prof, 12, false, false, WHITE);
        return m.build(true);
    };
    c.fUp = seg(F_UP, 0.024f, 0.018f);
    c.fLo = seg(F_LO, 0.016f, 0.013f);
    c.hUp = seg(H_UP, 0.041f, 0.023f);
    c.hLo = seg(H_LO, 0.017f, 0.012f);
    // legs take the fur's spine colour (u = 0.5) with a couple of stripes; white socks for the tuxedo
    remapUV(c.fUp, 0.f, 0.25f, 0.24f, 0.1f);
    remapUV(c.hUp, 0.f, 0.25f, 0.3f, 0.3f);
    remapUV(c.fLo, 0.f, 0.25f, k.whiteLegs ? 0.f : 0.22f, 0.6f);
    remapUV(c.hLo, 0.f, 0.25f, k.whiteLegs ? 0.f : 0.22f, 0.8f);
    b.ellipsoid({0, 0.009f, 0.012f}, {0.019f, 0.012f, 0.025f}, 6, 10, k.paw);
    c.paw = b.build(true);
    b.clear();
    // tail segment (+Y), ringed: one stripe per segment
    b.capsule({0, 0, 0}, {0, TAIL_SEG, 0}, 0.019f, 10, WHITE);
    c.tail = b.build(true);
    remapUV(c.tail, 0.f, 0.125f, 0.3f, 0.0625f);  // a ring mid-segment, plain fur at the joints
    b.clear();
    b.plane({0, 0, 0}, {1.f, 1.f}, {0, 1, 0}, {1, 1}, WHITE);
    c.blob = b.build(true);
}

}  // namespace

// ============================================================================ init / free
void Room::Impl::initCat() {
    cat = new Cat;
    Cat& c = *cat;
    okey::Rng cr(seed * 2246822519u + 0xCA7u);
    float pick = cr.uniform();
    c.coat = kCoats[pick < 0.45f ? 0 : (pick < 0.8f ? 1 : 2)];
    c.fur = genFurTexture(c.coat, seed + 777);
    c.blobTex = genBlobTexture();
    c.mFur = R->makeMat(WHITE, c.fur, 0.1f, 12.f, 0.f, 0.35f);
    c.mHead = R->makeMat(WHITE, Texture2D{}, 0.1f, 12.f, 0.f, 0.35f);
    c.mEye = R->makeMat(c.coat.eye, Texture2D{}, 0.9f, 90.f, 0.32f, 0.f);  // a faint glint: eyes in the dark
    c.mBlob = R->makeMat(Color{0, 0, 0, 120}, c.blobTex, 0.f, 1.f, 0.f, 0.f);
    buildCatMeshes(c);

    // the walkable floor
    std::vector<Vector3> chairPos;
    for (const Chair& ch : chairs) chairPos.push_back(ch.pos);
    c.grid.build(chairPos);

    // favourite spots
    auto emptyChairNear = [&](Vector2 p) {
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < (int)chairs.size(); ++i) {
            if (chairs[i].occupied) continue;
            float d = std::hypot(chairs[i].pos.x - p.x, chairs[i].pos.z - p.y);
            if (d < bd) bd = d, best = i;
        }
        return bd < 0.4f ? best : -1;
    };
    c.spots.push_back({SpotKind::FloorSleep, {3.05f, 2.8f}, -2.3f, -1, {}, 0.22f});    // by the stove, in its glow
    c.spots.push_back({SpotKind::FloorSit, {-1.75f, -3.08f}, 0.2f, -1, {}, 0.2f});     // under the scoreboard, facing us
    c.spots.push_back({SpotKind::FloorSit, {-3.8f, 2.8f}, -PI * 0.5f, -1, {}, 0.06f});  // doormat, watching the street
    c.spots.push_back({SpotKind::FloorSit, {-3.97f, -0.45f}, PI * 0.5f, -1, {}, 0.2f});  // under the windows
    int ch3 = emptyChairNear({2.5f, 1.05f}), ch0 = emptyChairNear({-3.25f, -1.8f});
    if (ch3 >= 0) c.spots.push_back({SpotKind::Chair, {}, 0.f, ch3, {2.94f, 1.0f}, 0.3f});
    if (ch0 >= 0) c.spots.push_back({SpotKind::Chair, {}, 0.f, ch0, {-3.28f, -1.36f}, 0.06f});

    // tonight it starts asleep somewhere
    float tot = 0.f;
    for (const Spot& s : c.spots) tot += s.weight;
    float r = cr.uniform(0.f, tot);
    c.spot = 0;
    for (int i = 0; i < (int)c.spots.size(); ++i) {
        r -= c.spots[i].weight;
        if (r <= 0.f) {
            c.spot = i;
            break;
        }
    }
    const Spot& s = c.spots[(size_t)c.spot];
    if (s.kind == SpotKind::Chair) {
        const Chair& ch = chairs[(size_t)s.chair];
        c.pos = {ch.pos.x, w3d::CHAIR_SEAT_Y + 0.004f, ch.pos.z};
        c.yaw = cr.uniform(-PI, PI);
        c.onChair = true;
        catChair = s.chair;
    } else {
        c.pos = {s.p.x, 0.f, s.p.y};
        c.yaw = s.yaw;
    }
    c.act = s.kind == SpotKind::FloorSit && cr.chance(0.5f) ? Act::Loaf : Act::Sleep;
    c.actDur = cr.uniform(25.f, 110.f);
    c.cur = c.tgt = c.act == Act::Sleep ? poseCurl() : poseLoaf();
    c.breathPh = cr.uniform(0.f, TAU);
    c.meowCool = cr.uniform(30.f, 90.f);
}

void Room::Impl::freeCat(Renderer& r) {
    if (!cat) return;
    Cat& c = *cat;
    for (Mesh* m : {&c.hips, &c.chest, &c.belly, &c.neck, &c.head, &c.eyes, &c.fUp, &c.fLo, &c.hUp, &c.hLo, &c.paw, &c.tail, &c.blob})
        if (m->vertexCount > 0) UnloadMesh(*m);
    for (Mat* m : {&c.mFur, &c.mHead, &c.mEye, &c.mBlob}) r.unloadMat(*m);
    for (Texture2D* t : {&c.fur, &c.blobTex})
        if (t->id) UnloadTexture(*t);
    delete cat;
    cat = nullptr;
    catChair = -1;
}

// ============================================================================ behaviour
namespace {

int pickSpot(const Cat& c, okey::Rng& rng, int exclude) {
    float tot = 0.f;
    for (int i = 0; i < (int)c.spots.size(); ++i)
        if (i != exclude) tot += c.spots[(size_t)i].weight;
    float r = rng.uniform(0.f, tot);
    for (int i = 0; i < (int)c.spots.size(); ++i) {
        if (i == exclude) continue;
        r -= c.spots[(size_t)i].weight;
        if (r <= 0.f) return i;
    }
    return exclude == 0 ? 1 : 0;
}

}  // namespace

void Room::Impl::updateCat(float dt) {
    if (!cat) return;
    Cat& c = *cat;
    c.actT += dt;
    c.meowCool -= dt;
    auto meow = [&](float p) {
        if (c.meowCool <= 0.f && rng.chance(p)) {
            sfx(ui::Sfx::Meow);
            c.meowCool = rng.uniform(80.f, 200.f);
            catMeowed = true;
            catMeowAt = Vector3Add(c.pos, {0.f, 0.18f, 0.f});
        }
    };
    auto seatPos = [&]() {  // a little forward of the seat's centre, clear of the backrest
        const Chair& ch = chairs[(size_t)c.spots[(size_t)c.spot].chair];
        const float a = (ch.yaw + ch.yawOff) * DEG2RAD;
        Vector3 fwd = ry(Vector3{0.f, 0.f, -1.f}, a);
        return Vector3{ch.pos.x + ch.off.x + fwd.x * 0.05f, w3d::CHAIR_SEAT_Y + 0.004f, ch.pos.z + ch.off.z + fwd.z * 0.05f};
    };
    auto startWalk = [&](int to) {
        c.target = to;
        const Spot& s = c.spots[(size_t)to];
        Vector2 goal = s.kind == SpotKind::Chair ? s.approach : s.p;
        c.path = c.grid.path({c.pos.x, c.pos.z}, goal);
        c.pathI = 0;
        c.act = Act::Walk;
        c.actT = 0.f;
    };
    auto leave = [&]() {  // off to another spot
        int to = pickSpot(c, rng, c.spot);
        if (c.onChair) {
            const Spot& s = c.spots[(size_t)c.spot];
            c.jumpFrom = c.pos;
            c.jumpTo = {s.approach.x, 0.f, s.approach.y};
            c.jumpYaw = std::atan2(c.jumpTo.x - c.pos.x, c.jumpTo.z - c.pos.z);
            c.act = Act::JumpDown;
            c.actT = 0.f;
            c.target = to;
        } else {
            startWalk(to);
        }
    };

    // ---- the behaviour itself
    switch (c.act) {
    case Act::Sleep:
        c.tgt = poseCurl();
        c.poseRate = 1.6f;
        if (c.actT > c.actDur) {  // wakes: lifts its head and looks around
            c.act = Act::Loaf;
            c.actT = 0.f;
            c.actDur = rng.uniform(4.f, 10.f);
            meow(0.12f);
        }
        break;
    case Act::Loaf:
        c.tgt = poseLoaf();
        c.poseRate = 2.f;
        if (c.actT > c.actDur) {
            float r = rng.uniform();
            c.actT = 0.f;
            if (r < 0.3f) {
                c.act = Act::Groom;
                c.actDur = rng.uniform(6.f, 12.f);
            } else if (r < 0.62f) {
                c.act = Act::Sleep;
                c.actDur = rng.uniform(40.f, 150.f);
            } else if (c.onChair) {
                leave();  // it stretches down on the floor, not on the seat
            } else {
                c.act = Act::Stretch;
                c.actDur = 3.2f;
            }
        }
        break;
    case Act::Sit:
        c.tgt = poseSit();
        c.poseRate = 2.6f;
        if (c.actT > c.actDur) {
            float r = rng.uniform();
            c.actT = 0.f;
            if (r < 0.4f) {
                c.act = Act::Groom;
                c.actDur = rng.uniform(6.f, 12.f);
            } else if (r < 0.85f) {
                c.act = Act::Loaf;
                c.actDur = rng.uniform(8.f, 20.f);
            } else {
                c.act = Act::Sleep;
                c.actDur = rng.uniform(40.f, 150.f);
            }
        }
        break;
    case Act::Groom: {
        c.tgt = poseSit();
        // lick the paw, then wipe it over the face: the paw rises to the mouth, the head bobs
        float g = 0.5f + 0.5f * std::sin(c.actT * 1.1f);
        c.tgt.groom = 1.f;
        c.tgt.headPitch = 0.35f + 0.12f * std::sin(c.actT * 9.f) * g;
        c.tgt.headYaw = 0.25f * std::sin(c.actT * 0.7f);
        c.tgt.headRoll = 0.2f * std::sin(c.actT * 1.1f + 1.f);
        c.tgt.eyes = 0.35f;
        c.poseRate = 5.f;
        if (c.actT > c.actDur) {
            c.act = Act::Sit;
            c.actT = 0.f;
            c.actDur = rng.uniform(3.f, 7.f);
        }
        break;
    }
    case Act::Stretch: {
        // bow (forelegs out, rear up), then stand; the tail quivers
        float t = c.actT / c.actDur;
        c.tgt = t < 0.55f ? poseBow() : poseStand();
        if (t < 0.55f) c.tgt.tailLift += 0.1f * std::sin(c.actT * 20.f);
        c.poseRate = 3.f;
        if (c.actT > c.actDur) {
            meow(0.2f);
            leave();
        }
        break;
    }
    case Act::Walk: {
        c.tgt = poseStand();
        c.poseRate = 4.f;
        if (c.pathI >= c.path.size()) {
            const Spot& s = c.spots[(size_t)c.target];
            c.spot = c.target;
            c.act = s.kind == SpotKind::Chair ? Act::JumpUp : Act::Settle;
            c.actT = 0.f;
            if (s.kind == SpotKind::Chair) {
                c.jumpFrom = c.pos;
                c.jumpTo = seatPos();
                c.jumpYaw = std::atan2(c.jumpTo.x - c.pos.x, c.jumpTo.z - c.pos.z);
            }
            c.turnFrom = c.yaw;
            break;
        }
        Vector2 w = c.path[c.pathI];
        float dx = w.x - c.pos.x, dz = w.y - c.pos.z, d = std::hypot(dx, dz);
        if (d < 0.06f) {
            ++c.pathI;
            break;
        }
        float want = std::atan2(dx, dz), err = wrapPi(want - c.yaw);
        c.yaw = wrapPi(c.yaw + std::clamp(err, -2.6f * dt, 2.6f * dt));
        float sp = std::fabs(err) > 1.f ? 0.06f : 0.34f * (1.f - 0.6f * std::fabs(err));
        if (c.pathI + 1 == c.path.size()) sp = std::min(sp, 0.12f + d * 0.8f);
        c.speed = approach(c.speed, sp, 5.f, dt);
        c.pos.x += std::sin(c.yaw) * c.speed * dt;
        c.pos.z += std::cos(c.yaw) * c.speed * dt;
        break;
    }
    case Act::Settle: {  // arrived at a floor spot: turn to face its way, then sit or lie down
        const Spot& s = c.spots[(size_t)c.spot];
        c.speed = approach(c.speed, 0.f, 6.f, dt);
        float err = wrapPi(s.yaw - c.yaw);
        c.yaw = wrapPi(c.yaw + std::clamp(err, -2.2f * dt, 2.2f * dt));
        c.tgt = poseStand();
        if (std::fabs(err) < 0.05f && c.actT > 0.4f) {
            c.actT = 0.f;
            if (s.kind == SpotKind::FloorSleep) {
                c.act = Act::Loaf;
                c.actDur = rng.uniform(3.f, 6.f);
            } else {
                c.act = Act::Sit;
                c.actDur = rng.uniform(6.f, 16.f);
                if (c.spot == 2 && rainBase > 0.f) meow(0.35f);  // at the door, and it is raining
            }
        }
        break;
    }
    case Act::JumpUp:
    case Act::JumpDown: {
        const bool up = c.act == Act::JumpUp;
        if (up) c.jumpTo = seatPos();
        const float crouch = 0.3f, fly = up ? 0.38f : 0.34f, land = 0.3f;
        float err = wrapPi(c.jumpYaw - c.yaw);
        c.yaw = wrapPi(c.yaw + std::clamp(err, -4.f * dt, 4.f * dt));
        c.speed = approach(c.speed, 0.f, 8.f, dt);
        if (c.actT < crouch) {
            c.tgt = poseStand();
            c.tgt.hipH -= 0.05f;
            c.tgt.pitch = up ? 0.12f : -0.25f;
            c.tgt.tailLift = 0.2f;
            c.poseRate = 8.f;
        } else if (c.actT < crouch + fly) {
            float t = easeInOut((c.actT - crouch) / fly);
            Vector3 p = Vector3Lerp(c.jumpFrom, c.jumpTo, t);
            p.y += std::sin(t * PI) * (up ? 0.16f : 0.08f);
            c.pos = p;
            c.tgt = poseStand();
            c.tgt.reach = up ? 0.8f : 0.4f;
            c.tgt.pitch = up ? 0.3f - 0.5f * t : -0.35f;
            c.tgt.tailLift = 0.5f;
            c.poseRate = 10.f;
        } else {
            c.pos = c.jumpTo;
            c.tgt = poseStand();
            c.tgt.hipH -= 0.03f * (1.f - (c.actT - crouch - fly) / land);
            if (up) {  // lands in a crouch: standing tall it would poke through the backrest
                c.tgt.hipH -= 0.06f;
                c.tgt.bend = 0.6f;
            }
            c.poseRate = 6.f;
            if (c.actT > crouch + fly + land) {
                c.actT = 0.f;
                if (up) {
                    c.onChair = true;
                    catChair = c.spots[(size_t)c.spot].chair;
                    c.act = Act::Turn;
                    c.actDur = 1.3f;
                    c.turnFrom = c.yaw;
                } else {
                    c.onChair = false;
                    catChair = -1;
                    startWalk(c.target);
                }
            }
        }
        break;
    }
    case Act::Turn: {  // turns round on the seat, crouching (a standing cat is longer than the seat), then down it goes
        c.pos = seatPos();
        float t = std::min(1.f, c.actT / c.actDur);
        c.yaw = wrapPi(c.turnFrom + easeInOut(t) * TAU * 0.55f);
        c.speed = 0.1f;
        c.tgt = poseLoaf();
        c.tgt.hipH += 0.03f;
        c.tgt.bend = 1.1f;
        c.tgt.frontFold = 0.5f;
        c.tgt.hindFold = 0.6f;
        c.poseRate = 5.f;
        if (c.actT > c.actDur) {
            c.speed = 0.f;
            c.act = Act::Sleep;
            c.actT = 0.f;
            c.actDur = rng.uniform(50.f, 170.f);
        }
        break;
    }
    }
    if (c.onChair && c.act != Act::JumpDown && c.spot >= 0) c.pos = seatPos();

    // ---- small life: breathing, blinking, looking around, tail flicks
    c.breathPh += dt * TAU * (c.act == Act::Sleep ? 0.38f : 0.55f);
    c.blinkT -= dt;
    if (c.blinkT <= 0.f) {
        c.blink = 0.13f;
        c.blinkT = rng.uniform(2.5f, 7.f);
    }
    c.blink = std::max(0.f, c.blink - dt);
    c.lookT -= dt;
    if (c.lookT <= 0.f) {
        c.lookT = rng.uniform(1.2f, 4.f);
        c.lookAtCam = rng.chance(0.3f);
        c.lookYaw = rng.uniform(-0.9f, 0.9f);
        c.lookPitch = rng.uniform(-0.25f, 0.2f);
    }
    if (c.act == Act::Loaf || c.act == Act::Sit) {
        float ly = c.lookYaw, lp = c.lookPitch;
        if (c.lookAtCam) {  // it notices you
            Vector3 hp{c.pos.x, c.pos.y + 0.3f, c.pos.z};
            Vector3 d = Vector3Subtract(c.camPos, hp);
            ly = std::clamp(wrapPi(std::atan2(d.x, d.z) - c.yaw), -1.3f, 1.3f);
            lp = -std::atan2(d.y, std::hypot(d.x, d.z));
        }
        c.tgt.headYaw = ly;
        c.tgt.headPitch = std::clamp(lp, -0.5f, 0.4f);
    }
    c.flickT -= dt;
    if (c.flickT <= 0.f) {
        c.flick = 1.f;
        c.flickSide = rng.chance(0.5f) ? 1.f : -1.f;
        c.flickT = rng.uniform(c.act == Act::Sleep ? 6.f : 2.5f, c.act == Act::Sleep ? 18.f : 7.f);
    }
    c.flick = std::max(0.f, c.flick - dt * 1.8f);
    // ease the pose toward its target
    {
        const float* pt = &c.tgt.hipH;
        float* pc = &c.cur.hipH;
        const int n = (int)(sizeof(Pose) / sizeof(float));
        for (int i = 0; i < n; ++i) pc[i] = approach(pc[i], pt[i], c.poseRate, dt);
    }
    c.walkBlend = approach(c.walkBlend, c.speed > 0.03f ? 1.f : 0.f, 6.f, dt);
    c.gait = std::fmod(c.gait + c.speed / 0.26f * dt, 1.f);
}

// ============================================================================ drawing
void Room::Impl::submitCat(Renderer& r) {
    if (!cat) return;
    Cat& c = *cat;
    c.camPos = r.lastCamera().position;
    const Pose& p = c.cur;
    const Matrix root = MatrixMultiply(MatrixRotateY(c.yaw), MatrixTranslate(c.pos.x, c.pos.y, c.pos.z));
    auto put = [&](const Mesh& m, const Mat& mat, const Matrix& local, uint32_t flags = 0) {
        r.submit(&m, &mat, MatrixMultiply(local, root), flags);
    };
    const float breathe = 1.f + 0.03f * p.breathe * std::sin(c.breathPh);
    const float curl01 = std::clamp(p.bend / 1.5f, 0.f, 1.f);
    const float spreadX = breathe * (1.f + 0.14f * curl01), spreadY = breathe * (1.f - 0.07f * curl01);
    const float bob = 0.006f * c.walkBlend * std::sin(c.gait * TAU * 2.f);

    // spine: pitch about the hips, then curl sideways about the spine's middle
    Vector3 H{0.f, p.hipH + bob, p.hipZ};
    Vector3 S0 = Vector3Add(H, Vector3{0.f, SPINE * std::sin(p.pitch), SPINE * std::cos(p.pitch)});
    Vector3 mid{0.f, 0.f, (H.z + S0.z) * 0.5f};
    auto bendAbout = [&](Vector3 v, float a) {
        Vector3 d = ry(Vector3{v.x - mid.x, 0.f, v.z - mid.z}, a);
        return Vector3{mid.x + d.x, v.y, mid.z + d.z};
    };
    const float hy = -p.bend * 0.5f, cy = p.bend * 0.5f;
    Vector3 hipsC = bendAbout(H, hy), chestC = bendAbout(S0, cy);
    const float hipPitch = p.pitch * 0.55f;
    Matrix hipR = bodyRot(hy, hipPitch), chestR = bodyRot(cy, p.pitch);
    auto at = [](Vector3 o, const Matrix& R, Vector3 l) { return Vector3Add(o, Vector3Transform(l, R)); };
    // torso
    Vector3 hc = at(hipsC, hipR, {0, 0.004f, -0.018f});
    Matrix hipM = MatrixMultiply(MatrixMultiply(MatrixScale(spreadX, spreadY, 1.f), hipR), MatrixTranslate(hc.x, hc.y, hc.z));
    put(c.hips, c.mFur, hipM, CastShadow);
    Vector3 chestCtr = at(chestC, chestR, {0, 0.f, -0.006f});
    put(c.chest, c.mFur, MatrixMultiply(MatrixMultiply(MatrixScale(spreadX, spreadY, 1.f), chestR), MatrixTranslate(chestCtr.x, chestCtr.y, chestCtr.z)),
        CastShadow);
    {   // the waist only fills out when the spine bends; standing it tucks inside the torso (no caterpillar bumps)
        const float k = 0.86f + 0.14f * curl01;
        Vector3 bc{mid.x, (H.y + S0.y) * 0.5f - 0.01f * (1.f - curl01), mid.z + (H.z - mid.z) * 0.1f};
        put(c.belly, c.mFur, MatrixMultiply(MatrixMultiply(MatrixScale(spreadX * k, spreadY * k, k), bodyRot(0.f, p.pitch * 0.8f)), MatrixTranslate(bc.x, bc.y, bc.z)));
    }
    // head and neck
    const float headYawW = cy + p.headYaw;
    Vector3 headP = Vector3Add(chestC, ry(Vector3{0.f, p.neckUp, p.headFwd}, cy));
    headP.y += 0.004f * std::sin(c.breathPh) * p.breathe;
    Matrix headR = bodyRot(headYawW, -p.headPitch, p.headRoll);
    put(c.head, c.mHead, MatrixMultiply(headR, MatrixTranslate(headP.x, headP.y, headP.z)), CastShadow);
    const float eyeOpen = p.eyes * (c.blink > 0.f ? 0.f : 1.f);
    if (eyeOpen > 0.15f)
        put(c.eyes, c.mEye, MatrixMultiply(MatrixMultiply(MatrixScale(1.f, eyeOpen, 1.f), headR), MatrixTranslate(headP.x, headP.y, headP.z)));
    {
        Vector3 n0 = at(chestC, chestR, {0.f, 0.03f, 0.075f});
        Vector3 n1 = at(headP, headR, {0.f, -0.012f, -0.025f});
        Vector3 d = Vector3Subtract(n1, n0);
        float len = Vector3Length(d);
        if (len > 1e-4f) {
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(1.f, len / 0.07f, 1.f), alignY(Vector3Scale(d, 1.f / len))), MatrixTranslate(n0.x, n0.y, n0.z));
            put(c.neck, c.mFur, m);
        }
    }
    // legs
    auto bone = [&](const Mesh& m, Vector3 a, Vector3 b2) {
        Vector3 d = Vector3Subtract(b2, a);
        float len = Vector3Length(d);
        if (len < 1e-5f) return;
        put(m, c.mFur, MatrixMultiply(alignY(Vector3Scale(d, 1.f / len)), MatrixTranslate(a.x, a.y, a.z)));
    };
    auto pawAt = [&](Vector3 q, float yawL) {
        put(c.paw, c.mHead, MatrixMultiply(MatrixRotateY(yawL), MatrixTranslate(q.x, q.y, q.z)));
    };
    const Vector3 chestFwd = ry(Vector3{0.f, 0.f, 1.f}, cy);
    const float stride = 0.26f * 0.6f;
    auto gaitOff = [&](float phase, float& lift) {
        float ph = std::fmod(c.gait + phase, 1.f);
        float dz, l = 0.f;
        if (ph < 0.6f) dz = stride * (0.5f - ph / 0.6f);
        else {
            float q = (ph - 0.6f) / 0.4f;
            dz = stride * (-0.5f + q);
            l = 0.035f * std::sin(q * PI);
        }
        lift = l * c.walkBlend;
        return dz * c.walkBlend;
    };
    for (int sd = -1; sd <= 1; sd += 2) {
        // --- front leg
        Vector3 J = at(chestC, chestR, {sd * 0.034f, -0.03f, 0.03f});
        Vector3 stand{J.x, 0.f, J.z + 0.012f};
        Vector3 fold = at(chestC, bodyRot(cy, 0.f), {sd * 0.03f, 0.f, -0.03f});
        fold.y = 0.012f;
        Vector3 tgt = Vector3Lerp(stand, fold, p.frontFold);
        tgt = Vector3Add(tgt, Vector3Scale(chestFwd, 0.17f * p.reach));
        float lift = 0.f;
        float dz = gaitOff(sd > 0 ? 0.25f : 0.75f, lift);
        tgt = Vector3Add(tgt, Vector3Scale(chestFwd, dz));
        tgt.y += lift;
        if (sd > 0 && p.groom > 0.01f) {  // the paw comes up to the mouth
            Vector3 mouth = at(headP, bodyRot(cy + p.headYaw, 0.f), {0.012f, -0.05f, 0.045f});
            tgt = Vector3Lerp(tgt, mouth, p.groom);
        }
        Vector3 pole = Vector3Negate(chestFwd);
        pole.y = 0.3f;
        Vector3 E = ik2(J, tgt, F_UP, F_LO, pole);
        bone(c.fUp, J, E);
        bone(c.fLo, E, tgt);
        pawAt({tgt.x, tgt.y - 0.009f + 0.004f * p.frontFold, tgt.z}, cy);
        // --- hind leg: the IK target is the heel (hock); folded (sitting, lying) the heel rests under the hip and
        //     the foot lies forward of it, so the thigh swings forward along the side: the haunch
        Vector3 K = at(hipsC, hipR, {sd * 0.044f, -0.022f, -0.02f});
        const Matrix hipFlat = bodyRot(hy, 0.f);
        Vector3 hstand = at(hipsC, hipFlat, {sd * 0.045f, 0.f, -0.045f});
        hstand.y = 0.f;
        Vector3 hfold = at(hipsC, hipFlat, {sd * 0.06f, 0.f, -0.01f});
        hfold.y = 0.016f;
        Vector3 ht = Vector3Lerp(hstand, hfold, p.hindFold);
        dz = gaitOff(sd > 0 ? 0.f : 0.5f, lift);
        const Vector3 hipFwd = ry(Vector3{0, 0, 1}, hy);
        ht = Vector3Add(ht, Vector3Scale(hipFwd, dz));
        ht.y += lift;
        Vector3 hpole = ry(Vector3{sd * 0.3f, -0.1f + 0.3f * (1.f - p.hindFold), 1.f}, hy);
        Vector3 Kn = ik2(K, ht, H_UP, H_LO, hpole);
        bone(c.hUp, K, Kn);
        bone(c.hLo, Kn, ht);
        Vector3 foot = Vector3Add(ht, Vector3Scale(hipFwd, 0.045f * p.hindFold));
        pawAt({foot.x, std::max(foot.y - 0.009f - 0.006f * p.hindFold, 0.003f), foot.z}, hy);
    }
    // tail: a free chain (lifted, curved, swaying on the walk) blended with a wrapped one that lies on the surface
    // round the body toward the front paws or, curled up, toward the nose (sit, loaf, curl)
    {
        const Vector3 rump = at(hipsC, hipR, {0.f, 0.03f, -0.128f});
        Vector3 freeJ[TAIL_N + 1], wrapJ[TAIL_N + 1];
        freeJ[0] = wrapJ[0] = rump;
        const float swayAmp = 0.35f * c.walkBlend + 0.05f;
        Vector3 q = rump;
        for (int i = 0; i < TAIL_N; ++i) {
            float t = (float)i / (TAIL_N - 1);
            float pitch = p.tailLift * (1.f - t * 0.6f) - p.tailCurve * t * t * 1.4f;
            float yawL = swayAmp * std::sin(c.breathPh * 0.9f + c.gait * TAU - i * 0.55f) * t;
            Vector3 d{std::sin(yawL) * std::cos(pitch), std::sin(pitch), -std::cos(yawL) * std::cos(pitch)};
            q = Vector3Add(q, Vector3Scale(ry(d, hy), TAIL_SEG * (1.f - 0.045f * i)));
            freeJ[i + 1] = q;
        }
        if (p.tailWrap > 0.01f) {
            // round the body's footprint, from the rump the short way to the nose (curled) or the chest front
            Vector3 C = Vector3Scale(Vector3Add(hipsC, chestC), 0.5f);
            const float R = p.bend > 0.5f ? 0.15f : 0.125f;
            Vector3 toward = p.bend > 0.5f ? headP : Vector3Add(C, Vector3Scale(chestFwd, 1.f));
            float a0 = std::atan2(rump.x - C.x, rump.z - C.z), a1 = std::atan2(toward.x - C.x, toward.z - C.z);
            float da = wrapPi(a1 - a0);
            if (std::fabs(da) > PI * 0.92f) da = PI * 0.92f;  // straight ahead: go round the cat's left
            float sweep = std::copysign(std::min(std::fabs(da), p.tailSide), da);
            float acc = 0.f;
            for (int i = 0; i < TAIL_N; ++i) {
                acc += TAIL_SEG * (1.f - 0.045f * i);
                float a = a0 + std::copysign(std::min(acc / R, std::fabs(sweep)), sweep);
                Vector3 onRing{C.x + R * std::sin(a), 0.f, C.z + R * std::cos(a)};
                float k = std::min(1.f, (float)(i + 1) / 2.5f);  // ease off the rump onto the ring
                wrapJ[i + 1] = Vector3Lerp(rump, onRing, k);
                wrapJ[i + 1].y = 0.f;
            }
        }
        Vector3 prev = rump;
        for (int i = 0; i < TAIL_N; ++i) {
            float t = (float)i / (TAIL_N - 1);
            float s = 1.f - 0.045f * i;
            Vector3 q2 = Vector3Lerp(freeJ[i + 1], wrapJ[i + 1], p.tailWrap);
            if (t > 0.5f) {  // the tip flicks now and then
                float f = c.flickSide * 0.05f * std::sin(c.flick * PI * 2.f) * c.flick * (t - 0.5f) * 2.f;
                q2 = Vector3Add(q2, ry(Vector3{f, 0.f, 0.f}, hy));
            }
            const float minY = 0.017f * s + 0.002f;
            if (q2.y < minY) q2.y = minY;
            Vector3 dd = Vector3Subtract(q2, prev);
            float len = Vector3Length(dd);
            if (len > 1e-5f)
                put(c.tail, c.mFur, MatrixMultiply(MatrixMultiply(MatrixScale(s, len / TAIL_SEG, s), alignY(Vector3Scale(dd, 1.f / len))),
                                                   MatrixTranslate(prev.x, prev.y, prev.z)));
            prev = q2;
        }
    }
    // soft contact shadow
    {
        Vector3 mc = Vector3Scale(Vector3Add(hipsC, chestC), 0.5f);
        float curl = std::clamp(p.bend / 1.9f, 0.f, 1.f);
        float lenS = 0.46f * (1.f - curl * 0.35f), widS = 0.22f + 0.14f * curl;
        Matrix w = MatrixMultiply(MatrixMultiply(MatrixScale(widS, 1.f, lenS), MatrixTranslate(mc.x, 0.f, mc.z)), root);
        // keep the shadow on the surface the cat is on (floor or seat), not under a jump
        float surf = c.onChair ? w3d::CHAIR_SEAT_Y + 0.006f : 0.004f;
        w.m13 = surf;
        float k = std::clamp(1.f - (c.pos.y - (c.onChair ? w3d::CHAIR_SEAT_Y : 0.f)) * 6.f, 0.f, 1.f);
        if (k > 0.05f) {
            c.mBlob.material.maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)(120.f * k);
            r.submit(&c.blob, &c.mBlob, w, Transparent);
        }
    }
}

}  // namespace r3d
