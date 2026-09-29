#pragma once
// Internal to the characters module (src/r3d/Characters*.cpp). Not a public API.
//
// Coordinate frames used here:
//  * character-local ("local"): origin on the floor under the hip centre (the chair seat centre for seated
//    people), +X = the person's right, +Y up, -Z = forward (toward the table). world = local * root.
//  * torso space: origin at the hip pivot, same axes when upright.
//  * head space: origin at the head pivot (top of the neck); eyes ~0.105 above it, face toward -Z.
//  * hand space: wrist at the origin, fingers toward -Z, palm facing -Y, the right thumb on -X (left hands
//    use a mirrored mesh, thumb on +X).
//  * limb meshes run along +Y from their joint.
#include "r3d/Characters.h"
#include "r3d/World.h"
#include "ui/Banter.h"

#include <raymath.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace r3d {
namespace chr {

// ============================================================================ small math
constexpr float PI_F = 3.14159265358979f;
// Where the room's CRT TV hangs (back-left corner, high on its bracket): a look-at target only.
constexpr Vector3 kTvPos{-3.70f, 2.50f, -2.90f};
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline float smoother01(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
inline float easeOut3(float t) {
    t = clampf(t, 0.f, 1.f);
    float u = 1.f - t;
    return 1.f - u * u * u;
}
inline float easeIn2(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t;
}
inline Vector3 V3(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector3 vlerp(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }
inline Vector3 vnorm(Vector3 v) {
    float l = Vector3Length(v);
    return l > 1e-6f ? Vector3Scale(v, 1.f / l) : Vector3{0, 0, -1};
}
// exponential approach (frame-rate independent)
inline float approachExp(float cur, float target, float rate, float dt) {
    return target + (cur - target) * std::exp(-rate * dt);
}
inline Vector3 approachExp(Vector3 cur, Vector3 target, float rate, float dt) {
    float k = std::exp(-rate * dt);
    return Vector3{target.x + (cur.x - target.x) * k, target.y + (cur.y - target.y) * k,
                   target.z + (cur.z - target.z) * k};
}
// critically damped spring (value + velocity)
inline void spring(float& x, float& v, float target, float omega, float dt) {
    float f = 1.f + 2.f * dt * omega;
    float oo = omega * omega;
    float hoo = dt * oo;
    float hhoo = dt * hoo;
    float detInv = 1.f / (f + hhoo);
    float detX = f * x + dt * v + hhoo * target;
    float detV = v + hoo * (target - x);
    x = detX * detInv;
    v = detV * detInv;
}
inline float wrapAngle(float a) {
    while (a > PI_F) a -= 2.f * PI_F;
    while (a < -PI_F) a += 2.f * PI_F;
    return a;
}

// Matrix from basis columns + translation (raylib column-major fields).
inline Matrix basisMatrix(Vector3 X, Vector3 Y, Vector3 Z, Vector3 T) {
    Matrix m{};
    m.m0 = X.x;
    m.m1 = X.y;
    m.m2 = X.z;
    m.m4 = Y.x;
    m.m5 = Y.y;
    m.m6 = Y.z;
    m.m8 = Z.x;
    m.m9 = Z.y;
    m.m10 = Z.z;
    m.m12 = T.x;
    m.m13 = T.y;
    m.m14 = T.z;
    m.m15 = 1.f;
    return m;
}
inline Vector3 mPos(const Matrix& m) { return Vector3{m.m12, m.m13, m.m14}; }
inline Vector3 mX(const Matrix& m) { return Vector3{m.m0, m.m1, m.m2}; }
inline Vector3 mY(const Matrix& m) { return Vector3{m.m4, m.m5, m.m6}; }
inline Vector3 mZ(const Matrix& m) { return Vector3{m.m8, m.m9, m.m10}; }
inline Vector3 xfPoint(const Matrix& m, Vector3 p) { return Vector3Transform(p, m); }
inline Vector3 xfDir(const Matrix& m, Vector3 d) {
    return Vector3{m.m0 * d.x + m.m4 * d.y + m.m8 * d.z, m.m1 * d.x + m.m5 * d.y + m.m9 * d.z,
                   m.m2 * d.x + m.m6 * d.y + m.m10 * d.z};
}
// "a then b" (raymath convention)
inline Matrix mul(const Matrix& a, const Matrix& b) { return MatrixMultiply(a, b); }
inline Matrix mul(const Matrix& a, const Matrix& b, const Matrix& c) { return MatrixMultiply(MatrixMultiply(a, b), c); }
inline Matrix T(Vector3 t) { return MatrixTranslate(t.x, t.y, t.z); }
inline Matrix S3(float x, float y, float z) { return MatrixScale(x, y, z); }
inline Matrix RX(float a) { return MatrixRotateX(a); }
inline Matrix RY(float a) { return MatrixRotateY(a); }
inline Matrix RZ(float a) { return MatrixRotateZ(a); }
// Bone frame for a limb mesh built along +Y: origin at a, +Y toward b; `hint` picks the twist.
Matrix boneMatrix(Vector3 a, Vector3 b, Vector3 hint);
// Hand frame: fingers toward `fingers`, palm facing `palm`, wrist at `wrist`, uniform scale.
Matrix handMatrix(Vector3 wrist, Vector3 fingers, Vector3 palm, float scale);

// ============================================================================ random
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    explicit Rng(uint64_t seed = 1) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float f() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }
    float f(float lo, float hi) { return lo + (hi - lo) * f(); }
    int i(int n) { return n <= 0 ? 0 : (int)(next() % (uint64_t)n); }
    bool chance(float p) { return f() < p; }
};
float hashf(uint32_t x);                       // 0..1
float noise1(float x, uint32_t seed);          // smooth 1D value noise 0..1
float noise3(Vector3 p, uint32_t seed);        // smooth 3D value noise 0..1

// ============================================================================ SDF modelling (init only)
// A shape is a smooth union of primitives (each blended into everything before it with radius k), with
// optional subtractions. Colours come from the primitive whose surface is nearest (soft blend), then
// an optional paint function. Meshed with surface nets and gradient normals.
class Sdf {
public:
    void ellipsoid(Vector3 c, Vector3 r, Color col, float k = 0.f, Vector3 rotDeg = {0, 0, 0});
    void cone(Vector3 a, Vector3 b, float ra, float rb, Color col, float k = 0.f); // rounded cone / capsule
    void box(Vector3 c, Vector3 half, float round, Color col, float k = 0.f, Vector3 rotDeg = {0, 0, 0});
    void torus(Vector3 c, float R, float r, Color col, float k = 0.f, Vector3 rotDeg = {0, 0, 0});
    // subtractive versions (carve)
    void cutEllipsoid(Vector3 c, Vector3 r, Color col, float k = 0.f, Vector3 rotDeg = {0, 0, 0});
    void cutCone(Vector3 a, Vector3 b, float ra, float rb, Color col, float k = 0.f);
    void cutBox(Vector3 c, Vector3 half, float round, Color col, float k = 0.f, Vector3 rotDeg = {0, 0, 0});

    // Starts a new layer: later primitives (and cuts) form a separate smooth union that is hard-unioned
    // with the earlier layers, so a cut only carves its own layer (e.g. the flat underside of a cap).
    void newLayer() { ++layer_; }

    float eval(Vector3 p) const;
    Color color(Vector3 p) const;
    void bounds(Vector3& lo, Vector3& hi) const;
    int count() const { return (int)prims_.size(); }

    std::function<Color(Vector3 p, Vector3 n, Color base)> paint;

private:
    struct Prim {
        int type = 0;  // 0 ellipsoid, 1 cone, 2 box, 3 torus
        int layer = 0;
        bool sub = false;
        bool rotated = false;
        Matrix inv{};  // world -> prim (rotation only, about the centre)
        Vector3 c{}, a{}, b{}, r{};
        float ra = 0, rb = 0, k = 0;
        Vector3 bc{};  // bounding sphere
        float br = 0;
        Color col{};
    };
    std::vector<Prim> prims_;
    int layer_ = 0;
    float primDist(const Prim& p, Vector3 x) const;
    float evalLayer(Vector3 x, int layer) const;
    void add(Prim p);
};
// CPU-side mesh (positions, normals, colours, triangles).
struct RawMesh {
    std::vector<Vector3> p, n;
    std::vector<Color> c;
    std::vector<unsigned> idx;
};
// Surface-nets mesh of `s` with cell size `cell` (metres).
RawMesh meshSdf(const Sdf& s, float cell);
// Quadric-error decimation down to `targetTris` (colour boundaries are expensive to collapse). With
// `colorEdge` > 0, vertices touching an edge whose end colours differ by more than that (squared RGB distance,
// 0..3) are never collapsed: painted boundaries keep their full resolution (the result may stay above target).
void simplifyMesh(RawMesh& m, int targetTris, float colorWeight = 10.f, float colorEdge = 0.f);
Mesh uploadRaw(const RawMesh& m);
// meshSdf -> simplifyMesh (when targetTris > 0) -> upload (with vertex colours).
Mesh buildSdf(const Sdf& s, float cell, int targetTris = 0);
// Runs fn(i) for i in [0, n) on the machine's cores (init only; nested calls run serially).
void parallelFor(int n, const std::function<void(int)>& fn);
// Init-time batch: meshes are made and simplified on worker threads, then uploaded on the GL thread.
struct MeshJobs {
    struct Job {
        std::function<RawMesh()> make;
        int target = 0, targetLo = 0;
        float colorEdge = 0.f;  // see simplifyMesh
        Mesh *out = nullptr, *mirror = nullptr, *outLo = nullptr, *mirrorLo = nullptr;
        RawMesh raw, rawLo;
    };
    std::vector<Job> jobs;
    // `mirror` (optional) receives an x-mirrored copy; `outLo` an extra, coarser level of detail.
    void add(std::function<RawMesh()> make, int target, Mesh* out, Mesh* mirror = nullptr, int targetLo = 0,
             Mesh* outLo = nullptr, Mesh* mirrorLo = nullptr);
    void run();
};
// A mirrored copy (x -> -x) with fixed winding; uploads it.
Mesh mirrorMeshX(const Mesh& m);

// ============================================================================ meshes & materials
enum class HandPose { Rest, Grip, Fist, Open, Pinch, Cig, Point, Hold, Count };
constexpr int HAND_POSES = (int)HandPose::Count;

// Offsets in hand space (scale 1) used to place objects in the hand / hands on objects.
constexpr Vector3 GRIP_CENTER{0.004f, -0.030f, -0.066f}; // glass axis passes here (along hand X)
constexpr Vector3 PINCH_POINT{-0.018f, -0.018f, -0.105f}; // between thumb and index tips
constexpr Vector3 PALM_CENTER{0.0f, -0.016f, -0.060f};
constexpr Vector3 FINGERTIPS{0.0f, -0.022f, -0.150f};
constexpr Vector3 CIG_HOLD{0.010f, -0.004f, -0.118f};     // between index and middle fingers
constexpr Vector3 FIST_TOP{0.0f, -0.014f, -0.102f};      // the curled fingers' top when making a fist
// The very end of a finger of the hand mesh in `pose` (hand space, right hand, scale 1): 0..3 index..little,
// 4 thumb.
Vector3 handTip(HandPose pose, int finger);

// Per-person face rig (only the detailed people have one).
struct FaceGeo {
    Vector3 eye[2];       // eye centres (head space)
    float eyeR = 0.0135f;
    Vector3 brow[2];      // brow centres
    Vector3 mouth;        // mouth centre (in front of the recess)
    Vector3 mouthOut;     // a point just in front of the lips (smoke, sip target)
    Vector3 chin;         // under the chin (chin rest)
    Vector3 forehead;     // facepalm
    Vector3 nose;         // nose tip
    float headTop = 0.215f;
};

struct PersonLook {
    // --- identity
    int kind = 0;          // 0 Rıza, 1 Mahmut, 2 Nuri, 3 çaycı, 10+ patrons
    Color skin{200, 150, 110, 255};
    Color lip{150, 80, 70, 255};
    Color hair{120, 110, 100, 255};
    Color browCol{100, 90, 80, 255};
    Color iris{80, 52, 30, 255};
    Color sleeve{200, 190, 170, 255};  // upper arm cloth
    Color cuff{200, 190, 170, 255};    // forearm cloth (skin = bare forearm)
    bool bareForearm = false;
    Color trousers{60, 56, 52, 255};
    Color shoes{40, 30, 24, 255};
    float headSpec = 0.22f;
    // --- body proportions
    float bodyScale = 1.f;   // uniform scale of the body parts
    float shoulderW = 0.185f;
    float upperArm = 0.29f, foreArm = 0.27f;
    float armR = 0.050f;     // sleeve radius at the shoulder
    float handScale = 1.f;
    float spineLen = 0.50f;  // hip pivot -> head pivot (upright), torso space
    float shoulderY = 0.37f; // shoulder joints (torso space)
    float headZ = -0.006f;   // head pivot z (torso space; negative = hunched forward)
    Vector3 hipPivot{0.f, 0.60f, 0.035f};
};

struct Meshes;  // all meshes & materials, built once
struct PersonMeshes {
    Mesh lower{}, torso{}, head{};
    Mesh upper[2]{}, fore[2]{};
    Mesh brow[2]{};
    Mesh stache{};            // mustache (hair material); empty for patrons (baked into the head)
    bool hasFace = false;
    Mat cloth{}, skin{}, headMat{}, hairMat{};
    FaceGeo face;
};

struct Meshes {
    // shared
    Mesh hand[2][HAND_POSES]{};        // [0] right, [1] left (white; tinted by the skin material)
    Mesh handLo[2][HAND_POSES]{};      // low-detail hands for the background patrons
    Mesh eye[3]{};                     // iris variants: brown, dark, grey-blue
    Mesh lidUpper{};                   // upper eyelid shell (white; skin material)
    Mesh mouthCavity{};
    Mesh lowerLip[5]{};                // frown2 .. smile2 (white; lip material via vertex colour)
    Mesh chair{}, chairSeat{};
    Mesh glass{}, saucer{};            // saucer: with its teaspoon
    static constexpr int TEA_LEVELS = 16;
    Mesh tea[TEA_LEVELS]{};            // liquid at fill levels 1/16 .. 16/16
    Mesh cigarette{}, ember{};
    Mesh bead4{}, imame{}, tassel{};   // tespih: a run of 4 beads, the long end piece, tassel
    Mesh spectacles{}, lenses{};
    Mesh trayHanger{}, trayGlasses{}, trayTea{};
    Mesh dice{}, cardFan{}, card{}, newspaper{};
    Mesh walkThigh[2]{}, walkShin[2]{}; // çaycı legs (built per look, see çaycı)

    Mat eyeMat{}, mouthMat{}, wood{}, straw{}, glassMat{}, teaMat{}, oraletMat{}, porcelain{},
        cigMat{}, emberMat{}, amber{}, tassleMat{}, lensMat{}, frameMat{}, trayMat{}, diceMat{}, cardMat{},
        paperMat{}, lipMat{};
    Texture2D woodTex{}, strawTex{}, paperTex{};
    RenderTexture2D paperCanvas{};
    bool paperCanvasOk = false;

    PersonMeshes person[4];            // [1..3] opponents, [0] the çaycı
    static constexpr int PATRON_VARIANTS = 6;
    PersonMeshes patron[PATRON_VARIANTS];
    float tulipH = 0.0f;               // glass height (scaled)
};

// Builders (CharactersMesh.cpp)
PersonLook lookFor(int kind);
void buildAll(Meshes& M, Renderer& r, uint64_t seed);
void freeAll(Meshes& M, Renderer& r);
// tea glass geometry helpers
float glassScale();
float glassInnerRadius(float y); // inner radius at height y (glass space, scaled)
constexpr float SAUCER_TOP = 0.0065f;  // glass base height above the table when on its saucer
constexpr float TESPIH_SEG = 0.046f;   // one tespih chain segment (4 beads)

// ============================================================================ animation primitives
// A key on an arm track: wrist target in character-local space (or head space when headRel).
struct Key {
    float t = 0.f;            // seconds from track start
    Vector3 pos{0, 0, 0};
    Vector3 fingers{0, 0, -1};
    Vector3 palm{0, -1, 0};
    HandPose pose = HandPose::Rest;
    float lift = 0.f;         // arc height of the motion *into* this key
    int ease = 0;             // 0 in-out, 1 out (fast start), 2 in (slam), 3 linear
    bool headRel = false;     // pos/fingers/palm are in head space
    int event = 0;            // fired when this key is reached (see KeyEvent)
    float elbowOut = 0.f;     // extra outward elbow bias
    Vector3 pole{0, 0, 0};    // explicit elbow pole direction (local); zero = default
    float relax = 1.f;        // 1: a relaxed hand follows the forearm a little; 0: placed exactly (fingertips on
                              // something), interpolated between keys so the wrist never pops
    bool touch = false;       // the fingers are on the owner's istaka here on purpose (picking / placing a tile):
                              // the rack clearance leaves the ends of the spans into and out of it alone
};
enum KeyEvent {
    KE_None = 0,
    KE_GrabGlass = 1,
    KE_ReleaseGlass = 2,
    KE_Sip = 3,
    KE_Drag = 4,        // cigarette drag starts
    KE_Exhale = 5,
    KE_Slam = 6,
    KE_AshTap = 7,
    KE_Clink = 8,
    KE_GrabSpoon = 9,
    KE_ReleaseSpoon = 10,
};

// A track is a C1 curve through its keys (cubic Hermite per span): the hand passes through intermediate keys
// without stopping, comes to rest at turning points, at `ease` 1 keys and at the last key, and hits `ease` 2
// keys at speed (slams, taps). It starts with the motion the hand already had (v0/f0/p0), so a new track
// never jerks the arm out of the one it replaces.
struct Track {
    std::vector<Key> keys;
    float t = 0.f;
    bool on = false;
    int kind = 0;          // what the track does (TrackKind)
    int nextKey = 1;       // next key whose event hasn't fired
    Vector3 v0{0, 0, 0};   // velocity of the position / fingers / palm at the first key (per second)
    Vector3 f0{0, 0, 0}, p0{0, 0, 0};
    float duration() const { return keys.empty() ? 0.f : keys.back().t; }
};
enum TrackKind {
    TK_None = 0,
    TK_Idle,
    TK_Reach,     // tiles
    TK_Sip,
    TK_Smoke,
    TK_Gesture,
    TK_Chin,
    TK_Tespih,
    TK_Serve,
};

struct Arm {
    Track track;
    Key hold;              // where the hand stays when no track runs
    // computed every frame (world)
    Vector3 shoulder{}, elbow{}, wrist{};
    Matrix upper = MatrixIdentity(), fore = MatrixIdentity(), hand = MatrixIdentity();
    Vector3 localWrist{};  // resolved local target (for auto lean)
    Vector3 aheadWrist{};  // where the track takes it a moment later (the body leans into a reach in time)
    HandPose pose = HandPose::Rest;
    Key cur;               // last evaluated key (local space) — new tracks start here
    Vector3 vel{0, 0, 0}, fVel{0, 0, 0}, pVel{0, 0, 0};  // how cur moves (per second) — new tracks keep it
    bool velInit = false;
    float idleIn = 3.f;
    float heldW = 0.f;     // 0..1, eases toward 1 while the hand holds something (the elbow follows the hand)
    float looseW = 0.f;    // 0..1, eases toward 1 in relaxed poses (the hand droops along the forearm)
    Vector3 pole{0, 0, 0}; // smoothed elbow pole (world): the elbow swings, it never pops
    float protract = 0.f;  // 0..1 the shoulder rolls forward into a long reach
    Vector3 swing{0, 0, 0}; // unit direction of the elbow off the shoulder-wrist line (world), rate limited
};

// Two-bone IK: returns the elbow for shoulder s, wrist target w (clamped to reach), pole direction.
Vector3 solveTwoBone(Vector3 s, Vector3& w, float l1, float l2, Vector3 pole);
// Evaluates a track at time t: fills the target (local or head-relative resolved by the caller).
void evalTrack(const Track& tr, float t, Key& out);

// ============================================================================ expressions
enum class Mood { Neutral, Happy, Laugh, Grumpy, Surprised, Sad, Thinking, Smug, Content };
struct Face {
    float browRaise[2] = {0, 0};  // metres up (+) / down (-)
    float browTilt[2] = {0, 0};   // radians, + = inner end down (angry)
    float lidOpen = 1.f;          // 1 normal, <1 squint, >1 wide
    float smile = 0.f;            // -1 frown .. 1 smile
    float jaw = 0.f;              // 0 closed .. 1 open
    float mouthWide = 0.f;        // 0 .. 1 (surprised O)
};

// ============================================================================ speech bubbles
struct Bubble {
    int who = 1;              // 1..3 opponents, 4 the çaycı
    std::string text;
    std::vector<std::string> lines;
    float w = 0, h = 0;
    float t = 0, dur = 3.f;
    float queued = 0, maxWait = 4.f;
    Vector2 pos{0, 0};        // smoothed top-left (virtual)
    Vector2 goal{0, 0};       // last chosen spot (placement hysteresis)
    bool placed = false;
    bool teaOrder = false;    // the line calls the çaycı (the speaker waves toward the counter)
};

} // namespace chr
} // namespace r3d
