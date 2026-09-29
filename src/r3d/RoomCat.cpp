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
// sitting down and curling up are smooth morphs rather than canned clips. The walk is a lateral-sequence gait;
// turning on the spot it steps round with short strides.
//
// Robustness (the rig is solved in updateCat, submitCat only submits it):
//   * the tail's segments are laid at their own lengths toward their targets (follow the leader): it bends, never
//     stretches; it rests on the floor / seat and slides round walls, chair legs and furniture instead of passing
//     through them;
//   * elbows and knees keep their bend direction and swing round at a limited rate (no flips);
//   * a jump is turn - crouch - leap - land: up first, over the seat's rim only once the paws are above it; on the
//     seat the paws and tail stay on it; down it goes out over the rim first, then trots a step clear;
//   * the floor plan follows the chairs when the chair-scrape moves them (a walk that would run into one replans);
//   * a rig with a non-finite or stray part is never drawn, and a non-finite state resets the cat.
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
constexpr float JUMP_DIST = 0.52f;  // take-off / landing point beside a chair, from the seat's centre
constexpr float SEAT_R = 0.18f;     // paws and tail stay inside this radius of the seat (its rim is at 0.2)

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
bool finite3(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finiteM(const Matrix& m) {
    const float* f = &m.m0;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(f[i])) return false;
    return true;
}
// Unit part of v perpendicular to the unit vector `dir` (zero if v is (nearly) parallel to it).
Vector3 perpUnit(Vector3 v, Vector3 dir) {
    Vector3 p = Vector3Subtract(v, Vector3Scale(dir, Vector3DotProduct(v, dir)));
    float l = Vector3Length(p);
    return l > 1e-5f ? Vector3Scale(p, 1.f / l) : Vector3{0, 0, 0};
}
// Two-bone IK: joint between root r and target t (clamped to reach), bending toward `pole`. The bend direction
// persists in `bend` and swings round the root-target line by at most `maxTurn` radians per call: a pole that
// sweeps past that line (a paw passing close to the shoulder) turns the elbow over smoothly instead of flipping it.
Vector3 ik2(Vector3 r, Vector3& t, float a, float b, Vector3 pole, Vector3& bend, float maxTurn) {
    Vector3 d = Vector3Subtract(t, r);
    float L = Vector3Length(d);
    Vector3 dir = L > 1e-6f ? Vector3Scale(d, 1.f / L) : Vector3{0, -1, 0};
    float Lc = std::clamp(L, std::fabs(a - b) + 0.01f, a + b - 1e-3f);
    if (Lc != L) t = Vector3Add(r, Vector3Scale(dir, Lc));
    float x = (a * a - b * b + Lc * Lc) / (2.f * Lc);
    float h = std::sqrt(std::max(0.f, a * a - x * x));
    Vector3 want = perpUnit(pole, dir), cur = perpUnit(bend, dir);
    if (Vector3LengthSqr(want) < 0.5f) want = Vector3LengthSqr(cur) > 0.5f ? cur : perpUnit(Vector3{0, 0, 1}, dir);
    if (Vector3LengthSqr(want) < 0.5f) want = perpUnit(Vector3{1, 0, 0}, dir);
    if (Vector3LengthSqr(cur) < 0.5f) cur = want;
    float ang = std::acos(std::clamp(Vector3DotProduct(cur, want), -1.f, 1.f));
    if (ang > maxTurn) {  // rotate cur about dir toward want (both are perpendicular to dir)
        float s = Vector3DotProduct(Vector3CrossProduct(cur, want), dir) >= 0.f ? 1.f : -1.f;
        float ca = std::cos(maxTurn * s), sa = std::sin(maxTurn * s);
        cur = Vector3Add(Vector3Scale(cur, ca), Vector3Scale(Vector3CrossProduct(dir, cur), sa));
    } else {
        cur = want;
    }
    bend = cur;
    return Vector3Add(r, Vector3Add(Vector3Scale(dir, x), Vector3Scale(cur, h)));
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
        auto cells = [&](float xa, float za, float xb, float zb, auto&& inside) {  // cells whose centres are inside
            const int i0 = std::max(0, ix(xa)), i1 = std::min(nx - 1, ix(xb)), k0 = std::max(0, iz(za)), k1 = std::min(nz - 1, iz(zb));
            for (int k = k0; k <= k1; ++k)
                for (int i = i0; i <= i1; ++i)
                    if (inside(centre(i, k))) blocked[(size_t)(k * nx + i)] = 1;
        };
        auto box = [&](float xa, float za, float xb, float zb) {
            cells(xa - m, za - m, xb + m, zb + m,
                  [&](Vector2 c) { return c.x > xa - m && c.x < xb + m && c.y > za - m && c.y < zb + m; });
        };
        auto disc = [&](float x, float z, float r) {
            cells(x - r - m, z - r - m, x + r + m, z + r + m,
                  [&](Vector2 c) { return (c.x - x) * (c.x - x) + (c.y - z) * (c.y - z) < (r + m) * (r + m); });
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
        for (const Vector3& c : chairs) box(c.x - 0.25f, c.z - 0.25f, c.x + 0.25f, c.z + 0.25f);  // legs splay to 0.29
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
    float air = 0.f;  // 1 = airborne: the paws tuck under the body instead of standing on the surface
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
    Vector2 approach;  // chair spots: floor point it jumps from / to (where the chair stood at night-fall)
    float weight;
    float side = 1.f;  // chair spots: which side of the chair (along the chair's own x) it jumps from
};

enum class Act { Sleep, Loaf, Sit, Groom, Stretch, Walk, Settle, JumpUp, JumpDown, Turn };

}  // namespace

struct Cat {
    Coat coat{};
    Texture2D fur{}, blobTex{};
    Mat mFur, mHead, mEye, mBlob;
    Mesh hips{}, chest{}, belly{}, neck{}, head{}, eyes{}, fUp{}, fLo{}, hUp{}, hLo{}, paw{}, tail{}, blob{};
    FloorGrid grid;
    std::vector<Vector3> gridChairs;  // where the chairs were when the grid was built
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
    float prevYaw = 0.f, turnRate = 0.f, strideK = 1.f;  // turning on the spot steps round with short strides
    Pose cur, tgt;
    float poseRate = 2.5f;
    Vector3 jumpFrom{}, jumpTo{};
    float jumpYaw = 0.f, turnFrom = 0.f, turnDir = 1.f;
    int jumpPh = 0;  // 0 turn to face, 1 crouch, 2 in the air, 3 landing
    float phT = 0.f;
    float lookT = 1.f, lookYaw = 0.f, lookPitch = 0.f;
    bool lookAtCam = false;
    float blinkT = 3.f, blink = 0.f;
    float flickT = 8.f, flick = 0.f, flickSide = 1.f;
    float meowCool = 40.f;
    float breathPh = 0.f;
    Vector3 camPos{0, 1.2f, 0.8f};

    // the posed rig (solved in updateCat, submitted by submitCat)
    struct Part {
        const Mesh* mesh;
        const Mat* mat;
        Matrix xf;
        uint32_t flags;
    };
    std::vector<Part> parts;
    Matrix blobXf = MatrixIdentity();
    float blobAlpha = 0.f;
    Vector3 bend[4]{};  // elbow / knee bend directions (root frame), rate limited
    bool seatClamp = false;  // on a chair: paws and tail keep to the seat
    bool seatNear = false;   // on a chair or leaping from / onto one: the tail keeps above the seat
    Vector2 seatC{};         // the seat's centre in the root frame
    float seatTop = 0.f;     // the seat's top in the root frame
    // What the tail must not pass through (world space): walls, furniture, the other chairs. Discs (r > 0) and
    // boxes, each up to a height. `near` = the ones close to the cat this frame.
    struct Obst {
        float x0, z0, x1, z1;  // box; a disc keeps its centre in (x0, z0) and its radius in r
        float r, top;
        int chair;             // index into Room::Impl::chairs, or -1
    };
    std::vector<Obst> obst, near;
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
    for (const Chair& ch : chairs) c.gridChairs.push_back(Vector3Add(ch.pos, ch.off));
    c.grid.build(c.gridChairs);
    // what the tail keeps out of (see FloorGrid::build for the same furniture on the floor plan)
    {
        const float X0 = w3d::ROOM_X0, X1 = w3d::ROOM_X1, Z0 = w3d::ROOM_Z0, Z1 = w3d::ROOM_Z1;
        auto box = [&](float xa, float za, float xb, float zb, float top) { c.obst.push_back({xa, za, xb, zb, 0.f, top, -1}); };
        auto disc = [&](float x, float z, float r, float top, int chair = -1) { c.obst.push_back({x, z, x, z, r, top, chair}); };
        box(X0 - 1.f, Z0 - 1.f, X0, Z1 + 1.f, 9.f);  // walls
        box(X1, Z0 - 1.f, X1 + 1.f, Z1 + 1.f, 9.f);
        box(X0 - 1.f, Z0 - 1.f, X1 + 1.f, Z0, 9.f);
        box(X0 - 1.f, Z1, X1 + 1.f, Z1 + 1.f, 9.f);
        for (const w3d::BgTable& t : w3d::BG_TABLES)  // table legs
            for (int k = 0; k < 4; ++k) disc(t.x + (k % 2 ? 0.39f : -0.39f), t.z + (k < 2 ? 0.39f : -0.39f), 0.036f, w3d::BG_TABLE_Y);
        box(2.03f, -3.5f, 3.97f, -2.93f, 1.05f);  // counter
        disc(3.8f, 2.55f, 0.32f, 1.1f);             // stove
        disc(-2.95f, 3.25f, 0.2f, 1.9f);            // coat rack
        box(-2.17f, 3.13f, 0.47f, 3.7f, 0.48f);     // wall bench
        disc(0.52f, -3.18f, 0.17f, 0.45f);          // the rubber plant's tin
        for (int i = 0; i < (int)chairs.size(); ++i)  // chairs: legs splay to 0.29, backrest up to 0.98
            disc(c.gridChairs[(size_t)i].x, c.gridChairs[(size_t)i].z, 0.29f, 0.98f, i);
    }

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
    // the chairs are pulled out and turned a little differently every night: a floor spot keeps a curled-up cat's
    // length (head, tail) clear of the nearest chair's splayed legs
    for (Spot& s : c.spots) {
        for (int it = 0; it < 4; ++it) {
            const Chair* near = nullptr;
            float bd = 1e9f;
            for (const Chair& ch : chairs) {
                float d = std::hypot(s.p.x - ch.pos.x, s.p.y - ch.pos.z);
                if (d < bd) bd = d, near = &ch;
            }
            const float need = 0.64f;
            if (!near || bd >= need || bd < 1e-3f) break;
            Vector2 away{(s.p.x - near->pos.x) / bd, (s.p.y - near->pos.z) / bd};
            Vector2 np{near->pos.x + away.x * need, near->pos.z + away.y * need};
            if (!c.grid.freeAt(np)) break;
            s.p = np;
        }
    }
    // an empty chair: it jumps up from the floor beside the seat (never from the front, where the table is, nor over
    // the backrest), from a walkable point on the preferred side if there is one
    auto chairSpot = [&](Vector2 near, Vector2 prefer, float weight) {
        const int ci = emptyChairNear(near);
        if (ci < 0) return;
        const Chair& ch = chairs[(size_t)ci];
        const Vector3 side = ry(Vector3{1.f, 0.f, 0.f}, ch.yaw * DEG2RAD);
        const float sgn0 = side.x * prefer.x + side.z * prefer.y >= 0.f ? 1.f : -1.f;
        for (float sgn : {sgn0, -sgn0}) {
            const Vector2 ap{ch.pos.x + side.x * sgn * JUMP_DIST, ch.pos.z + side.z * sgn * JUMP_DIST};
            if (!c.grid.freeAt(ap)) continue;
            c.spots.push_back({SpotKind::Chair, {}, 0.f, ci, ap, weight, sgn});
            return;
        }
    };
    chairSpot({2.5f, 1.05f}, {1.f, 0.f}, 0.3f);
    chairSpot({-3.25f, -1.8f}, {0.f, 1.f}, 0.06f);

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
    c.prevYaw = c.yaw;
    updateCat(0.f);  // pose the rig for the first frame
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

bool Room::Impl::catNearChair(int chair) const {
    if (!cat || chair < 0 || chair >= (int)chairs.size()) return false;
    if (chair == catChair) return true;
    const Vector3& p = chairs[(size_t)chair].pos;
    return std::hypot(p.x - cat->pos.x, p.z - cat->pos.z) < 0.9f;
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

bool poseFinite(const Pose& p) {
    const float* f = &p.hipH;
    for (int i = 0; i < (int)(sizeof(Pose) / sizeof(float)); ++i)
        if (!std::isfinite(f[i])) return false;
    return true;
}

// Poses the rig from the current pose: every part's world matrix, the contact shadow. Everything is built in the
// root frame (the cat's own: +Z forward, y up from the surface it stands on) and placed by `root` at the end.
void solveRig(Cat& c, float dt) {
    c.parts.clear();
    const Pose& p = c.cur;
    const Matrix root = MatrixMultiply(MatrixRotateY(c.yaw), MatrixTranslate(c.pos.x, c.pos.y, c.pos.z));
    auto put = [&](const Mesh& m, const Mat& mat, const Matrix& local, uint32_t flags = 0) {
        c.parts.push_back({&m, &mat, MatrixMultiply(local, root), flags});
    };
    const float breathe = 1.f + 0.03f * p.breathe * std::sin(c.breathPh);
    const float curl01 = std::clamp(p.bend / 1.5f, 0.f, 1.f);
    const float spreadX = breathe * (1.f + 0.14f * curl01), spreadY = breathe * (1.f - 0.07f * curl01);
    const float air = std::clamp(p.air, 0.f, 1.f);
    const float bob = 0.006f * c.walkBlend * (1.f - air) * std::sin(c.gait * TAU * 2.f);
    const float maxTurn = std::max(7.f * dt, 1e-3f);  // elbows and knees swing round at most 7 rad/s

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
    // on a chair the paws and the tail stay on the seat (a standing cat is longer than the seat is wide)
    auto onSeat = [&](Vector3 q, float r) {
        if (!c.seatClamp) return q;
        float dx = q.x - c.seatC.x, dz = q.z - c.seatC.y, d = std::hypot(dx, dz);
        if (d > r) q.x = c.seatC.x + dx * r / d, q.z = c.seatC.y + dz * r / d;
        return q;
    };
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
    const Vector3 chestFwd = ry(Vector3{0.f, 0.f, 1.f}, cy), chestSide = ry(Vector3{1.f, 0.f, 0.f}, cy);
    const float stride = 0.26f * 0.6f * c.strideK;
    const float step = c.walkBlend * (1.f - air);
    auto gaitOff = [&](float phase, float& lift) {
        float ph = std::fmod(c.gait + phase, 1.f);
        float dz, l = 0.f;
        if (ph < 0.6f) dz = stride * (0.5f - ph / 0.6f);
        else {
            float q = (ph - 0.6f) / 0.4f;
            dz = stride * (-0.5f + q);
            l = 0.035f * std::sin(q * PI);
        }
        lift = l * step;
        return dz * step;
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
        tgt = onSeat(tgt, SEAT_R);
        // airborne: the forelegs reach forward under the chest
        tgt = Vector3Lerp(tgt, at(chestC, chestR, {sd * 0.03f, -0.13f, 0.1f}), air);
        Vector3 pole = Vector3Negate(chestFwd);
        pole.y = 0.3f;
        if (sd > 0 && p.groom > 0.01f) {
            // the paw comes up to the mouth along an arc in front of the chest (not through the shoulder), the elbow
            // turning down and out
            const float g = std::min(p.groom, 1.f);
            Vector3 mouth = at(headP, bodyRot(cy + p.headYaw, 0.f), {0.012f, -0.05f, 0.045f});
            tgt = Vector3Add(Vector3Lerp(tgt, mouth, g), Vector3Scale(chestFwd, 0.06f * std::sin(PI * g)));
            pole = Vector3Lerp(pole, Vector3{chestSide.x * 0.5f, -1.f, chestSide.z * 0.5f}, g);
        }
        Vector3 E = ik2(J, tgt, F_UP, F_LO, pole, c.bend[sd > 0 ? 1 : 0], maxTurn);
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
        ht = onSeat(ht, SEAT_R - 0.02f);
        ht = Vector3Lerp(ht, at(hipsC, hipR, {sd * 0.05f, -0.15f, -0.07f}), air);  // airborne: trailing
        Vector3 hpole = ry(Vector3{sd * 0.3f, -0.1f + 0.3f * (1.f - p.hindFold), 1.f}, hy);
        Vector3 Kn = ik2(K, ht, H_UP, H_LO, hpole, c.bend[sd > 0 ? 3 : 2], maxTurn);
        bone(c.hUp, K, Kn);
        bone(c.hLo, Kn, ht);
        Vector3 foot = Vector3Add(ht, Vector3Scale(hipFwd, 0.045f * p.hindFold));
        pawAt({foot.x, std::max(foot.y - 0.009f - 0.006f * p.hindFold, 0.003f), foot.z}, hy);
    }
    // tail: a free chain (lifted, curved, swaying on the walk) blended with a wrapped one that lies on the surface
    // round the body toward the front paws or, curled up, toward the nose (sit, loaf, curl). The blend only gives
    // each joint a target: the segments are then laid toward the targets at their own lengths (follow the leader),
    // so the tail bends but never stretches, and it rests on the surface instead of sinking into it.
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
            wrapJ[i + 1] = q;  // (not wrapping: the free chain; the blend below reads every joint)
        }
        const float wrap = std::clamp(p.tailWrap, 0.f, 1.f);
        if (wrap > 0.01f) {
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
        Vector3 prev = rump, prevDir = ry(Vector3{0.f, 0.f, -1.f}, hy);
        for (int i = 0; i < TAIL_N; ++i) {
            const float t = (float)i / (TAIL_N - 1);
            const float s = 1.f - 0.045f * i;
            const float segL = TAIL_SEG * s;
            Vector3 aim = Vector3Lerp(freeJ[i + 1], wrapJ[i + 1], wrap);
            if (t > 0.5f) {  // the tip flicks now and then
                float f = c.flickSide * 0.05f * std::sin(c.flick * PI * 2.f) * c.flick * (t - 0.5f) * 2.f;
                aim = Vector3Add(aim, ry(Vector3{f, 0.f, 0.f}, hy));
            }
            aim = onSeat(aim, SEAT_R - 0.045f);
            Vector3 d = Vector3Subtract(aim, prev);
            float dl = Vector3Length(d);
            Vector3 dir = dl > 1e-5f ? Vector3Scale(d, 1.f / dl) : prevDir;
            // resting on the surface: a joint that would sink below it slides along it at the same length (over the
            // seat of the chair it is on or leaping from / onto, the seat is the surface)
            float minY = 0.017f * s + 0.002f;
            if (c.seatNear) {
                Vector3 q0 = Vector3Add(prev, Vector3Scale(dir, segL));
                if (std::hypot(q0.x - c.seatC.x, q0.z - c.seatC.y) < 0.23f) minY = std::max(minY, c.seatTop + 0.017f * s + 0.004f);
            }
            if (prev.y + dir.y * segL < minY) {
                float dy = std::clamp((minY - prev.y) / segL, -1.f, 1.f);
                float hl = std::hypot(dir.x, dir.z);
                Vector3 hd = hl > 1e-4f ? Vector3{dir.x / hl, 0.f, dir.z / hl} : Vector3{prevDir.x, 0.f, prevDir.z};
                float hn = std::hypot(hd.x, hd.z);
                hd = hn > 1e-4f ? Vector3Scale(hd, 1.f / hn) : ry(Vector3{0.f, 0.f, -1.f}, hy);
                float h = std::sqrt(std::max(0.f, 1.f - dy * dy));
                dir = {hd.x * h, dy, hd.z * h};
            }
            Vector3 next = Vector3Add(prev, Vector3Scale(dir, segL));
            float len = segL;
            if (c.seatClamp) {  // (sliding along the seat can carry a joint over its rim: keep it on the seat, shorter)
                Vector3 inSeat = onSeat(next, SEAT_R);
                if (inSeat.x != next.x || inSeat.z != next.z) {
                    Vector3 d2 = Vector3Subtract(inSeat, prev);
                    float l2 = Vector3Length(d2);
                    if (l2 > 0.25f * segL) {  // toward the point on the seat, never longer than the segment
                        dir = Vector3Scale(d2, 1.f / l2);
                        len = std::min(l2, segL);
                        next = Vector3Add(prev, Vector3Scale(dir, len));
                    }
                }
            }
            // walls and furniture close by: the tail swings round them (along them) rather than through them
            if (!c.near.empty()) {
                const float tr = 0.017f * s + 0.006f;  // the segment's radius and a little air
                Vector3 w = Vector3Add(ry(next, c.yaw), c.pos);
                bool moved = false;
                for (const Cat::Obst& o : c.near) {
                    if (w.y - tr > o.top) continue;
                    if (o.r > 0.f) {
                        const float dx = w.x - o.x0, dz = w.z - o.z0, dd = std::hypot(dx, dz), R = o.r + tr;
                        if (dd >= R || dd < 1e-5f) continue;
                        w.x = o.x0 + dx / dd * R, w.z = o.z0 + dz / dd * R;
                    } else {
                        const float px0 = w.x - (o.x0 - tr), px1 = (o.x1 + tr) - w.x, pz0 = w.z - (o.z0 - tr), pz1 = (o.z1 + tr) - w.z;
                        if (px0 <= 0.f || px1 <= 0.f || pz0 <= 0.f || pz1 <= 0.f) continue;
                        const float m = std::min(std::min(px0, px1), std::min(pz0, pz1));
                        if (m == px0) w.x -= px0;
                        else if (m == px1) w.x += px1;
                        else if (m == pz0) w.z -= pz0;
                        else w.z += pz1;
                    }
                    moved = true;
                }
                if (moved) {
                    const Vector3 out = ry(Vector3Subtract(w, c.pos), -c.yaw);
                    const Vector3 d2 = Vector3Subtract(out, prev);
                    const float l2 = Vector3Length(d2);
                    if (l2 > 1e-4f) {
                        dir = Vector3Scale(d2, 1.f / l2);
                        len = std::min(l2, segL);
                        next = Vector3Add(prev, Vector3Scale(dir, len));
                    }
                }
            }
            put(c.tail, c.mFur, MatrixMultiply(MatrixMultiply(MatrixScale(s, len / TAIL_SEG, s), alignY(dir)), MatrixTranslate(prev.x, prev.y, prev.z)));
            prev = next;
            prevDir = dir;
        }
    }
    // soft contact shadow, on the surface the cat is on (floor or seat), fading out while it is in the air
    {
        Vector3 mc = Vector3Scale(Vector3Add(hipsC, chestC), 0.5f);
        float curl = std::clamp(p.bend / 1.9f, 0.f, 1.f);
        float lenS = 0.46f * (1.f - curl * 0.35f), widS = 0.22f + 0.14f * curl;
        Matrix w = MatrixMultiply(MatrixMultiply(MatrixScale(widS, 1.f, lenS), MatrixTranslate(mc.x, 0.f, mc.z)), root);
        const float surf = c.onChair ? w3d::CHAIR_SEAT_Y + 0.006f : 0.004f;
        w.m13 = surf;
        c.blobXf = w;
        c.blobAlpha = std::clamp(1.f - (c.pos.y - (c.onChair ? w3d::CHAIR_SEAT_Y : 0.f)) * 6.f, 0.f, 1.f);
    }
    // never draw a broken rig: every part must be finite and on the cat
    for (const Cat::Part& part : c.parts)
        if (!finiteM(part.xf) || Vector3Distance(Vector3{part.xf.m12, part.xf.m13, part.xf.m14}, c.pos) > 0.8f) {
            c.parts.clear();
            c.blobAlpha = 0.f;
            for (Vector3& b : c.bend) b = Vector3{0, 0, 0};
            break;
        }
    if (!finiteM(c.blobXf)) c.blobAlpha = 0.f;
}

}  // namespace

void Room::Impl::updateCat(float dt) {
    if (!cat) return;
    Cat& c = *cat;
    // (defensive) a cat that somehow lost its footing starts over, asleep by the stove
    if (!finite3(c.pos) || !std::isfinite(c.yaw) || !poseFinite(c.cur) || !poseFinite(c.tgt) || c.spots.empty()) {
        c.spot = 0;
        c.target = -1;
        c.onChair = false;
        catChair = -1;
        c.pos = {c.spots.empty() ? 3.05f : c.spots[0].p.x, 0.f, c.spots.empty() ? 2.8f : c.spots[0].p.y};
        c.yaw = c.prevYaw = c.spots.empty() ? 0.f : c.spots[0].yaw;
        c.act = Act::Sleep;
        c.actT = 0.f;
        c.actDur = 60.f;
        c.cur = c.tgt = poseCurl();
        c.speed = c.turnRate = 0.f;
        for (Vector3& b : c.bend) b = Vector3{0, 0, 0};
    }
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
    auto spotChair = [&](int spot) -> const Chair& { return chairs[(size_t)c.spots[(size_t)spot].chair]; };
    auto seatCentre = [&](int spot) {
        const Chair& ch = spotChair(spot);
        return Vector3{ch.pos.x + ch.off.x, w3d::CHAIR_SEAT_Y + 0.004f, ch.pos.z + ch.off.z};
    };
    auto seatPos = [&]() {  // a little forward of the seat's centre, clear of the backrest
        const Chair& ch = spotChair(c.spot);
        const float a = (ch.yaw + ch.yawOff) * DEG2RAD;
        Vector3 fwd = ry(Vector3{0.f, 0.f, -1.f}, a);
        Vector3 sc = seatCentre(c.spot);
        return Vector3{sc.x + fwd.x * 0.05f, sc.y, sc.z + fwd.z * 0.05f};
    };
    // Where the root goes on the seat: the body's middle (not the root) stays on the seat point. Curling up bends the
    // spine sideways, which moves the middle of the body off the root (solveRig: hips and chest swing out about the
    // spine's middle), so the root slides a little as it curls up.
    auto seatRoot = [&]() {
        const float h = 0.5f * SPINE * std::cos(c.cur.pitch);
        const Vector3 mid = ry(Vector3{h * std::sin(0.5f * c.cur.bend), 0.f, c.cur.hipZ + h}, c.yaw);
        const Vector3 sp = seatPos();
        return Vector3{sp.x - mid.x, sp.y, sp.z - mid.z};
    };
    // the floor point beside a chair it jumps from / lands on (from where the chair is now: it may have been moved)
    auto takeOff = [&](int spot) {
        const Spot& s = c.spots[(size_t)spot];
        const Chair& ch = spotChair(spot);
        const Vector3 side = ry(Vector3{1.f, 0.f, 0.f}, (ch.yaw + ch.yawOff) * DEG2RAD);
        Vector2 ap{ch.pos.x + ch.off.x + side.x * s.side * JUMP_DIST, ch.pos.z + ch.off.z + side.z * s.side * JUMP_DIST};
        return c.grid.freeAt(ap) ? ap : s.approach;
    };
    auto startWalk = [&](int to) {
        c.target = to;
        const Spot& s = c.spots[(size_t)to];
        // a chair it is heading for is kept where it is (the chair-scrape animation leaves it alone)
        catChair = s.kind == SpotKind::Chair ? s.chair : -1;
        Vector2 goal = s.kind == SpotKind::Chair ? takeOff(to) : s.p;
        const Vector2 from{c.pos.x, c.pos.z};
        c.path = c.grid.path(from, goal);
        if (s.kind == SpotKind::Chair) {
            // the last few steps run straight at the seat: it arrives facing it (turning on the spot right beside the
            // chair would sweep its tail through the chair's and the table's legs)
            const Vector3 sc = seatCentre(to);
            Vector2 away{goal.x - sc.x, goal.y - sc.z};
            const float al = std::hypot(away.x, away.y);
            if (al > 1e-3f) {
                const Vector2 lead{goal.x + away.x / al * 0.3f, goal.y + away.y / al * 0.3f};
                if (c.grid.freeAt(lead) && c.grid.lineFree(lead, goal) && std::hypot(from.x - goal.x, from.y - goal.y) > 0.35f) {
                    c.path = c.grid.path(from, lead);
                    c.path.push_back(goal);
                }
            }
        }
        // just down from a chair: it trots a step clear of it before heading off (its tail still trails back over the
        // chair: turning right beside it would sweep the tail through the chair's legs)
        if (c.act == Act::JumpDown && !c.path.empty()) {
            const Vector2 ahead{from.x + std::sin(c.yaw) * 0.28f, from.y + std::cos(c.yaw) * 0.28f};
            if (c.grid.lineFree(from, ahead)) {
                std::vector<Vector2> p2 = c.grid.path(ahead, goal);
                p2.insert(p2.begin(), ahead);
                c.path = p2;
            }
        }
        c.pathI = 0;
        c.act = Act::Walk;
        c.actT = 0.f;
    };
    auto leave = [&]() {  // off to another spot
        int to = pickSpot(c, rng, c.spot);
        c.target = to;
        if (c.onChair) {
            const Vector2 ap = takeOff(c.spot);
            c.jumpTo = {ap.x, 0.f, ap.y};
            c.act = Act::JumpDown;
            c.jumpPh = 0;
            c.phT = 0.f;
            c.actT = 0.f;
        } else {
            startWalk(to);
        }
    };
    auto turnToward = [&](float yawTo, float rate) {
        float err = wrapPi(yawTo - c.yaw);
        c.yaw = wrapPi(c.yaw + std::clamp(err, -rate * dt, rate * dt));
        return std::fabs(wrapPi(yawTo - c.yaw));
    };
    auto nextPhase = [&]() {
        ++c.jumpPh;
        c.phT = 0.f;
    };

    // a chair was pulled out or pushed in (the chair-scrape): the floor plan follows it, and a walk that would now
    // run into it is replanned
    {
        bool moved = c.gridChairs.size() != chairs.size();
        for (size_t i = 0; !moved && i < chairs.size(); ++i) {
            const Vector3 p = Vector3Add(chairs[i].pos, chairs[i].off);
            moved = std::fabs(p.x - c.gridChairs[i].x) > 0.01f || std::fabs(p.z - c.gridChairs[i].z) > 0.01f;
        }
        if (moved) {
            c.gridChairs.clear();
            for (const Chair& ch : chairs) c.gridChairs.push_back(Vector3Add(ch.pos, ch.off));
            c.grid.build(c.gridChairs);
            for (Cat::Obst& o : c.obst)
                if (o.chair >= 0 && o.chair < (int)c.gridChairs.size())
                    o.x0 = o.x1 = c.gridChairs[(size_t)o.chair].x, o.z0 = o.z1 = c.gridChairs[(size_t)o.chair].z;
            if (c.act == Act::Walk && c.pathI < c.path.size()) {
                Vector2 a{c.pos.x, c.pos.z};
                bool clear = true;
                for (size_t i = c.pathI; clear && i < c.path.size(); ++i) clear = c.grid.lineFree(a, c.path[i]), a = c.path[i];
                if (!clear) {
                    const Vector2 goal = c.path.back();
                    c.path = c.grid.path({c.pos.x, c.pos.z}, goal);
                    c.pathI = 0;
                }
            }
        }
    }

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
        c.tgt.groom = smooth01(0.2f, 1.f, c.actT);  // the paw comes up once it has sat up
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
            c.actT = 0.f;
            c.turnFrom = c.yaw;
            if (s.kind == SpotKind::Chair) {
                c.act = Act::JumpUp;
                c.jumpPh = 0;
                c.phT = 0.f;
            } else {
                c.act = Act::Settle;
            }
            break;
        }
        Vector2 w = c.path[c.pathI];
        float dx = w.x - c.pos.x, dz = w.y - c.pos.z, d = std::hypot(dx, dz);
        if (d < 0.06f) {
            ++c.pathI;
            break;
        }
        const float err = turnToward(std::atan2(dx, dz), 2.4f);
        // a sharp corner: it stops and steps round on the spot rather than swinging wide into the furniture
        float sp = err > 0.9f ? 0.f : 0.34f * (1.f - 0.7f * err);
        if (c.pathI + 1 == c.path.size()) {
            sp = std::min(sp, 0.1f + d * 0.8f);
        } else {  // slows down ahead of a sharp corner in the path, so it doesn't overshoot it
            const Vector2 n = c.path[c.pathI + 1];
            const float corner = std::fabs(wrapPi(std::atan2(n.x - w.x, n.y - w.y) - std::atan2(dx, dz)));
            if (corner > 0.6f) sp = std::min(sp, 0.07f + d * 1.2f);
        }
        c.speed = approach(c.speed, sp, sp < c.speed ? 10.f : 5.f, dt);  // (it stops quicker than it sets off)
        c.pos.x += std::sin(c.yaw) * c.speed * dt;
        c.pos.z += std::cos(c.yaw) * c.speed * dt;
        break;
    }
    case Act::Settle: {  // arrived at a floor spot: turn to face its way, then sit or lie down
        const Spot& s = c.spots[(size_t)c.spot];
        c.speed = approach(c.speed, 0.f, 6.f, dt);
        const float err = turnToward(s.yaw, 2.f);
        c.tgt = poseStand();
        if (err < 0.05f && c.actT > 0.4f) {
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
    case Act::JumpUp: {
        // beside the chair: turn to the seat, crouch and wiggle, then leap - up first, over the seat's rim only once
        // the paws are above it - and land low (standing tall on the seat it would poke through the backrest)
        c.phT += dt;
        c.speed = approach(c.speed, 0.f, 8.f, dt);
        c.jumpTo = seatRoot();
        if (c.jumpPh == 0) {
            const Vector3 sc = seatCentre(c.spot);
            c.jumpYaw = std::atan2(sc.x - c.pos.x, sc.z - c.pos.z);
            const float err = turnToward(c.jumpYaw, 2.4f);
            c.tgt = poseStand();
            c.tgt.headPitch = -0.2f;
            c.poseRate = 5.f;
            if ((err < 0.04f && c.phT > 0.3f) || c.phT > 3.f) nextPhase();
        } else if (c.jumpPh == 1) {
            c.tgt = poseStand();
            c.tgt.hipH -= 0.07f;
            c.tgt.pitch = 0.1f;
            c.tgt.headPitch = -0.35f;
            c.tgt.tailLift = 0.25f;
            c.tgt.tailCurve = 0.3f;
            c.tgt.bend = 0.1f * std::sin(c.phT * 26.f) * smooth01(0.1f, 0.2f, c.phT) * (1.f - smooth01(0.38f, 0.48f, c.phT));
            c.poseRate = 8.f;
            if (c.phT > 0.55f) {
                c.jumpFrom = c.pos;
                nextPhase();
            }
        } else if (c.jumpPh == 2) {
            const float T = 0.42f, t = std::min(1.f, c.phT / T);
            const float hf = t * t, vf = 1.f - (1.f - t) * (1.f - t);
            c.pos = {c.jumpFrom.x + (c.jumpTo.x - c.jumpFrom.x) * hf, c.jumpFrom.y + (c.jumpTo.y - c.jumpFrom.y) * vf + 0.1f * std::sin(t * PI),
                     c.jumpFrom.z + (c.jumpTo.z - c.jumpFrom.z) * hf};
            c.tgt = poseStand();
            c.tgt.air = 1.f;
            c.tgt.pitch = 0.5f * (1.f - t) - 0.05f;
            c.tgt.headPitch = -0.1f;
            c.tgt.tailLift = 0.5f;
            c.tgt.tailCurve = 0.2f;
            c.poseRate = 12.f;
            if (t >= 1.f) {
                c.onChair = true;  // touchdown: from here on it is on the seat
                nextPhase();
            }
        } else {
            c.tgt = poseStand();
            c.tgt.hipH = 0.13f;
            c.tgt.frontFold = 0.3f;
            c.tgt.hindFold = 0.5f;
            c.tgt.bend = 0.35f;
            c.tgt.tailLift = 0.3f;
            c.poseRate = 7.f;
            if (c.phT > 0.4f) {
                c.act = Act::Turn;
                c.actT = 0.f;
                c.actDur = 1.5f;
                c.turnFrom = c.yaw;
                // half a turn, swinging round through the seat's open front: its head never goes near the backrest
                const Chair& ch = spotChair(c.spot);
                const Vector3 front = ry(Vector3{0.f, 0.f, -1.f}, (ch.yaw + ch.yawOff) * DEG2RAD);
                c.turnDir = wrapPi(std::atan2(front.x, front.z) - c.yaw) >= 0.f ? 1.f : -1.f;
            }
        }
        break;
    }
    case Act::JumpDown: {
        // turn round on the seat to face the way down (crouched), a look down, then out over the rim first and down
        c.phT += dt;
        c.speed = approach(c.speed, 0.f, 8.f, dt);
        if (c.jumpPh == 0) {
            c.jumpYaw = std::atan2(c.jumpTo.x - c.pos.x, c.jumpTo.z - c.pos.z);
            const float err = turnToward(c.jumpYaw, 2.2f);
            c.tgt = poseStand();
            c.tgt.hipH = 0.14f;
            c.tgt.frontFold = 0.25f;
            c.tgt.hindFold = 0.5f;
            c.tgt.bend = 0.5f * std::min(1.f, err);
            c.tgt.tailLift = 0.4f;
            c.tgt.tailCurve = 0.5f;
            c.poseRate = 5.f;
            if ((err < 0.04f && c.phT > 0.4f) || c.phT > 3.f) nextPhase();
        } else if (c.jumpPh == 1) {
            c.tgt = poseStand();
            c.tgt.hipH = 0.15f;
            c.tgt.pitch = -0.22f;
            c.tgt.headPitch = 0.45f;
            c.tgt.frontFold = 0.1f;
            c.tgt.hindFold = 0.45f;
            c.tgt.tailLift = 0.3f;
            c.poseRate = 7.f;
            if (c.phT > 0.4f) {
                c.jumpFrom = c.pos;
                c.onChair = false;  // take-off
                nextPhase();
            }
        } else if (c.jumpPh == 2) {
            const float T = 0.4f, t = std::min(1.f, c.phT / T);
            const float hf = 1.f - (1.f - t) * (1.f - t), vf = t * t;
            c.pos = {c.jumpFrom.x + (c.jumpTo.x - c.jumpFrom.x) * hf, c.jumpFrom.y + (c.jumpTo.y - c.jumpFrom.y) * vf + 0.11f * std::sin(t * PI),
                     c.jumpFrom.z + (c.jumpTo.z - c.jumpFrom.z) * hf};
            c.tgt = poseStand();
            c.tgt.air = 1.f;
            c.tgt.pitch = -0.1f - 0.3f * t;
            c.tgt.headPitch = 0.2f;
            c.tgt.tailLift = 0.7f;
            c.poseRate = 12.f;
            if (t >= 1.f) nextPhase();
        } else {
            c.pos = c.jumpTo;
            c.tgt = poseStand();
            c.tgt.hipH -= 0.05f * std::max(0.f, 1.f - c.phT / 0.3f);
            c.tgt.pitch = -0.1f;
            c.poseRate = 8.f;
            if (c.phT > 0.35f) startWalk(c.target);
        }
        break;
    }
    case Act::Turn: {  // turns round on the seat, crouching (a standing cat is longer than the seat), then down it goes
        float t = std::min(1.f, c.actT / c.actDur);
        c.yaw = wrapPi(c.turnFrom + c.turnDir * easeInOut(t) * PI);
        c.speed = 0.f;
        c.tgt = poseLoaf();
        c.tgt.hipH += 0.03f;
        c.tgt.bend = 1.1f;
        c.tgt.frontFold = 0.5f;
        c.tgt.hindFold = 0.6f;
        c.poseRate = 5.f;
        if (c.actT > c.actDur) {
            c.act = Act::Sleep;
            c.actT = 0.f;
            c.actDur = rng.uniform(50.f, 170.f);
        }
        break;
    }
    }
    if (c.onChair && c.spot >= 0 && c.spots[(size_t)c.spot].kind == SpotKind::Chair) c.pos = seatRoot();

    // ---- small life: breathing, blinking, looking around, tail flicks
    c.breathPh = std::fmod(c.breathPh + dt * TAU * (c.act == Act::Sleep ? 0.38f : 0.55f), TAU * 100.f);
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
    // the walk cycle; turning on the spot it steps round with short strides instead of spinning on planted paws
    if (dt > 0.f) {
        c.turnRate = approach(c.turnRate, std::fabs(wrapPi(c.yaw - c.prevYaw)) / dt, 12.f, dt);
        c.prevYaw = c.yaw;
    }
    const float stepSpeed = c.speed + 0.1f * c.turnRate;
    c.walkBlend = approach(c.walkBlend, stepSpeed > 0.03f ? 1.f : 0.f, 6.f, dt);
    c.strideK = approach(c.strideK, std::clamp(c.speed / 0.2f, 0.3f, 1.f), 6.f, dt);
    c.gait = std::fmod(c.gait + stepSpeed / 0.26f * dt, 1.f);

    // ---- pose the rig (on a chair, its paws and tail keep to the seat)
    c.seatClamp = c.onChair && c.spot >= 0 && c.spots[(size_t)c.spot].kind == SpotKind::Chair;
    // in the air between the floor and its seat, the seat is the surface below the tail; on the floor beside the
    // chair, the chair is an obstacle like any other
    c.seatNear = c.seatClamp || ((c.act == Act::JumpUp || c.act == Act::JumpDown) && c.jumpPh == 2 && c.spot >= 0 &&
                                 c.spots[(size_t)c.spot].kind == SpotKind::Chair);
    if (c.seatNear) {
        Vector3 l = ry(Vector3Subtract(seatCentre(c.spot), c.pos), -c.yaw);
        c.seatC = {l.x, l.z};
        c.seatTop = w3d::CHAIR_SEAT_Y + 0.002f - c.pos.y;
    }
    c.near.clear();
    const int ownChair = c.seatNear ? c.spots[(size_t)c.spot].chair : -1;
    for (const Cat::Obst& o : c.obst) {
        if (o.chair >= 0 && o.chair == ownChair) continue;
        const float dx = o.r > 0.f ? std::fabs(c.pos.x - o.x0) - o.r : std::max(o.x0 - c.pos.x, c.pos.x - o.x1);
        const float dz = o.r > 0.f ? std::fabs(c.pos.z - o.z0) - o.r : std::max(o.z0 - c.pos.z, c.pos.z - o.z1);
        if (dx < 0.75f && dz < 0.75f) c.near.push_back(o);  // (the tail reaches ~0.55 m from the root)
    }
    solveRig(c, dt);
}

// ============================================================================ drawing
void Room::Impl::submitCat(Renderer& r) {
    if (!cat) return;
    Cat& c = *cat;
    c.camPos = r.lastCamera().position;
    for (const Cat::Part& q : c.parts) r.submit(q.mesh, q.mat, q.xf, q.flags);
    if (c.blobAlpha > 0.05f && !c.parts.empty()) {
        c.mBlob.material.maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)(120.f * c.blobAlpha);
        r.submit(&c.blob, &c.mBlob, c.blobXf, Transparent);
    }
}

}  // namespace r3d
