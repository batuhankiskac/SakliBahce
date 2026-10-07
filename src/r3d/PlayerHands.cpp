// The player's own hands and forearms (PlayerHands.h). Seat-local frame ("local"): origin on the floor under the
// player's chair, +X the player's right, +Y up, -Z forward (toward the table); the okey seat and the tavla seat have the
// same table-edge distance (World.h), so every spot here fits both. world = local * root_.
//
// Each arm is driven, in order of priority, by a table cue (reach for the object the table names and hold it while it
// moves: HandCue::where), the card fan (the left hand), a key track (tea, the tespih, a cigarette, a fidget, the dice,
// letting go) or its rest key; the regulars' tools do the rest (chr::evalTrack's Hermite tracks, two-bone IK, the hand
// meshes). The shoulders sit under the camera and slide forward into a long reach (the body leans in; nothing of it is
// in view), so the hand gets to the middle of the table.
#include "r3d/PlayerHands.h"

#include "r3d/Characters.h"
#include "r3d/CharactersState.h"
#include "r3d/World.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace r3d {

using chr::HandPose;
using chr::Key;
using chr::Track;

namespace {

constexpr float TY = w3d::TABLE_Y;
constexpr float kUpper = 0.30f, kFore = 0.28f;   // the player's arm (metres)
constexpr float kHS = 1.0f;                       // hand scale
constexpr Vector3 kShoulderR{0.19f, 1.03f, 0.05f}; // right shoulder joint (the left mirrored), under and behind the eye
constexpr float kReach = (kUpper + kFore) * 0.93f; // the shoulder leans in past this
constexpr float kMaxLean = 0.58f;
constexpr float kGripH = 0.055f;                  // grip height above the glass base (as the regulars hold theirs)
constexpr Vector3 kTespihGrip{0.004f, -0.024f, -0.084f};
constexpr float kBeadR = 0.0056f;
constexpr int kChainN = 12;                       // tespih loop: points (11 segments x 3 beads = 33)
constexpr float kChainSeg = 0.0195f;

enum ArmEvent { EV_None = 0, EV_GrabGlass = 1, EV_ReleaseGlass = 2, EV_Sip = 3, EV_Drag = 4, EV_Exhale = 5, EV_DiceGo = 6 };

const Color kCloth[5] = {{38, 46, 74, 255}, {92, 62, 42, 255}, {96, 96, 100, 255}, {108, 32, 42, 255}, {214, 202, 172, 255}};
const Color kSkin[4] = {{234, 192, 160, 255}, {206, 152, 114, 255}, {168, 112, 78, 255}, {112, 74, 52, 255}};
const Color kBeadCol[5] = {{0, 0, 0, 255}, {214, 128, 28, 255}, {34, 28, 28, 255}, {36, 104, 66, 255}, {190, 56, 46, 255}};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float smoother(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
Color scaleC(Color c, float k) {
    return Color{(unsigned char)clampf(c.r * k, 0, 255), (unsigned char)clampf(c.g * k, 0, 255),
                 (unsigned char)clampf(c.b * k, 0, 255), c.a};
}
Color mixC(Color a, Color b, float t) {
    t = clampf(t, 0.f, 1.f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// Lathe around +Y with a colour per vertex (u around 0..1, y along); profile (radius, y).
void latheC(MeshBuilder& b, const std::vector<Vector2>& prof, int seg, const std::function<Color(float u, float y)>& col) {
    const int np = (int)prof.size();
    if (np < 2) return;
    std::vector<Vector2> nrm((size_t)np);
    for (int i = 0; i < np; ++i) {
        const Vector2 a = prof[(size_t)std::max(i - 1, 0)], c = prof[(size_t)std::min(i + 1, np - 1)];
        const Vector2 t{c.x - a.x, c.y - a.y};
        Vector2 n{t.y, -t.x};
        const float l = std::sqrt(n.x * n.x + n.y * n.y);
        nrm[(size_t)i] = l > 1e-6f ? Vector2{n.x / l, n.y / l} : Vector2{1, 0};
    }
    const int base = b.vertexCount();
    for (int i = 0; i < np; ++i)
        for (int j = 0; j <= seg; ++j) {
            const float a = 2.f * PI * (float)j / (float)seg;
            const float s = std::sin(a), c = std::cos(a);
            const Vector2 p = prof[(size_t)i], n = nrm[(size_t)i];
            b.vertex({p.x * s, p.y, p.x * c}, {n.x * s, n.y, n.x * c}, {(float)j / (float)seg, p.y}, col((float)j / (float)seg, p.y));
        }
    for (int i = 0; i + 1 < np; ++i)
        for (int j = 0; j < seg; ++j) {
            const int a = base + i * (seg + 1) + j, bb = a + 1, cc = a + (seg + 1) + 1, d = a + (seg + 1);
            b.quad(a, bb, cc, d);
        }
}

// A rounded start (a hemisphere below y0 of radius r0) as profile points.
void capStart(std::vector<Vector2>& p, float y0, float r0) {
    for (int i = 0; i <= 5; ++i) {
        const float t = -PI / 2 + (PI / 2) * (float)i / 5.f;
        p.push_back({r0 * std::cos(t), y0 + r0 * std::sin(t)});
    }
}

// Ideal glass-in-hand transform (glass base at the origin, axis +Y) for a grip (as CharactersAnim's glassInHand).
Matrix glassInHand(bool left, float hs) {
    const float inv = 1.f / hs;
    Vector3 gx, gy, gz;
    if (left) {
        gy = {1, 0, 0};
        gx = {0, 1, 0};
        gz = {0, 0, -1};
    } else {
        gy = {-1, 0, 0};
        gx = {0, 1, 0};
        gz = {0, 0, 1};
    }
    const Vector3 grip = chr::mirrorL(chr::GRIP_CENTER, left);
    const Vector3 t = Vector3Subtract(grip, Vector3Scale(gy, kGripH * inv));
    return chr::basisMatrix(Vector3Scale(gx, inv), Vector3Scale(gy, inv), Vector3Scale(gz, inv), t);
}

} // namespace

// ============================================================================ state
struct PlayerHands::Impl {
    struct Arm {
        bool left = false;
        // a table cue
        bool follow = false;
        HandCue cue;
        float ft = 0.f, reachDur = 0.25f;
        Key start;
        Vector3 startVel{0, 0, 0};
        bool attached = false;
        // a key track (tracks with `rate` < 0 run at the animation speed)
        Track track;
        float rate = 1.f;
        Key hold;  // where it stays when nothing else runs
        // the evaluated hand (local) and how it moves
        Key cur;
        Vector3 vel{0, 0, 0};
        bool velInit = false;
        // body
        float lean = 0.f, leanV = 0.f;
        Vector3 leanDir{0, 0, -1};
        Vector3 pole{0, 0, 0};
        // results (world)
        Vector3 shoulder{}, elbow{}, wrist{};
        Matrix upper = MatrixIdentity(), fore = MatrixIdentity(), hand = MatrixIdentity();
        HandPose pose = HandPose::Rest;
        bool glass = false;  // holds the tea glass
        int idleKind = 0;    // what the running track is: 0 none / action, 1 sip, 2 tespih, 3 smoke, 4 fidget, 5 dice
    };

    Renderer* R = nullptr;
    Characters* people = nullptr;
    std::function<void(ui::Sfx)>* sfx = nullptr;  // PlayerHands::playSfx
    bool ready = false;
    chr::Rng rng{1};
    PlayerLook look;
    bool lookBuilt = false;
    bool enabled = true;
    Scene scene = Hidden;
    bool lowered = false, myTurn = false;
    float speed = 1.f;
    float low = 1.f, lowV = 0.f;  // 0 shown .. 1 sunk out of view
    float time = 0.f;
    Matrix root = MatrixIdentity(), rootInv = MatrixIdentity();
    Matrix headLocal = MatrixIdentity();  // camera space (x right, y up, -z forward, origin at the eye) -> local
    Camera3D eye{};
    Arm arm[2];  // 0 right, 1 left
    // the fan held in the left hand (card games)
    bool fan = false;
    Matrix fanFrame = MatrixIdentity();
    // idles
    float sipIn = 18.f, tespihIn = 7.f, smokeIn = 9.f, fidgetIn[2] = {6.f, 9.f};
    std::string debugAsk;
    // the cigarette
    float drag = 0.f, exhaleT = -1.f, smokeAcc = 0.f;
    // the tespih (Verlet loop + the imame's tail)
    std::vector<Vector3> chain, chainPrev, tail, tailPrev;
    bool chainInit = false;
    float flickKick = 0.f;
    // meshes and materials (the hands are Characters')
    Mesh upperMesh{}, foreMesh{}, foreSkin{}, ring{}, watch{}, bead{}, imame{}, tassel{}, cig{}, ember{};
    Mat clothMat{}, skinMat{}, goldMat{}, watchMat{}, beadMat{}, tasselMat{}, cigMat{}, emberMat{};
    bool hasForeSkin = false;

    // ---- setup
    void buildStatic();
    void buildLook();
    void freeLook();
    void setRoot();
    // ---- keys
    Vector3 shoulderBase(bool left) const { return {left ? -kShoulderR.x : kShoulderR.x, kShoulderR.y, kShoulderR.z}; }
    Key restKey(bool left, int variant) const;
    Key fanKey() const;
    void pickDirs(bool left, Vector3 targetL, int style, Vector3& f, Vector3& p) const;
    Key pinchKey(bool left, Vector3 gripL, int style) const;
    void startTrack(Arm& A, std::vector<Key> keys, int idleKind, bool atAnimSpeed);
    void releaseTo(Arm& A, float lift);
    void dropGlass(Arm& A);
    // ---- behaviours
    void startCue(const HandCue& c);
    void startDice(Arm& A, const HandCue& c);
    void startSip(Arm& A);
    void startTespih(Arm& A);
    void startSmoke(Arm& A);
    void startFidget(Arm& A);
    void idle(float dt);
    // ---- per frame
    Key evalArm(Arm& A, float dt);
    void fireEvents(Arm& A, float before, float after);
    void solveArm(Arm& A, Key k, float dt);
    void updateTespih(float dt);
    void updateSmoke(float dt);
    Vector3 mouthWorld() const { return xfPoint(root, xfPoint(headLocal, {0.f, -0.075f, -0.03f})); }
    static Vector3 xfPoint(const Matrix& m, Vector3 p) { return Vector3Transform(p, m); }
    static Vector3 xfDir(const Matrix& m, Vector3 d) { return chr::xfDir(m, d); }
    bool active() const { return ready && enabled && scene != Hidden; }
};

// ============================================================================ meshes
void PlayerHands::Impl::buildStatic() {
    // a gold band (around +Y), built as a swept circle
    {
        MeshBuilder b;
        std::vector<Vector3> path;
        for (int i = 0; i <= 20; ++i) {
            const float a = 2.f * PI * (float)i / 20.f;
            path.push_back({0.0098f * std::cos(a), 0.f, 0.0098f * std::sin(a)});
        }
        b.tube(path, 0.0021f, 8, WHITE);
        ring = b.build();
    }
    // the watch (around the wrist, along hand-space Z): a strap, the case and its face on the back of the wrist (+Y)
    {
        MeshBuilder b;
        const Color strap{58, 36, 24, 255}, steel{196, 196, 200, 255}, face{236, 228, 206, 255}, hands{30, 30, 30, 255};
        std::vector<Vector3> loop;
        for (int i = 0; i <= 24; ++i) {
            const float a = 2.f * PI * (float)i / 24.f;
            loop.push_back({0.0285f * std::cos(a), 0.0215f * std::sin(a), 0.f});
        }
        b.tube(loop, 0.0032f, 6, strap);
        b.setTransform(MatrixMultiply(MatrixRotateX(0.f), MatrixTranslate(0.f, 0.0205f, 0.f)));
        b.cylinder({0.f, 0.f, 0.f}, 0.0135f, 0.0062f, 20, steel);
        b.cylinder({0.f, 0.0062f, 0.f}, 0.0112f, 0.0006f, 20, face);
        b.box({0.f, 0.0070f, -0.0035f}, {0.0012f, 0.0004f, 0.0070f}, hands);
        b.box({0.0025f, 0.0070f, 0.f}, {0.0050f, 0.0004f, 0.0011f}, hands);
        b.resetTransform();
        watch = b.build(true);
    }
    bead = [] {
        MeshBuilder b;
        b.sphere({0, 0, 0}, kBeadR, 6, 8, WHITE);
        return b.build();
    }();
    imame = [] {
        MeshBuilder b;
        b.lathe({{0.f, -0.032f}, {0.0040f, -0.030f}, {0.0062f, -0.020f}, {0.0058f, -0.006f}, {0.0034f, 0.f}, {0.f, 0.001f}}, 10,
                false, false, WHITE);
        return b.build();
    }();
    tassel = [] {
        MeshBuilder b;
        b.lathe({{0.0016f, 0.f}, {0.0048f, -0.010f}, {0.0074f, -0.034f}, {0.f, -0.036f}}, 10, true, false, WHITE);
        return b.build();
    }();
    // a cigarette along +Y from the filter's end (0) to the tip (0.075), and its ember
    {
        MeshBuilder b;
        latheC(b, {{0.f, 0.f}, {0.0040f, 0.f}, {0.0042f, 0.002f}, {0.0042f, 0.022f}, {0.0041f, 0.0235f}, {0.0041f, 0.0735f}, {0.0036f, 0.075f}},
               10, [](float, float y) {
                   if (y < 0.0225f) return Color{206, 140, 70, 255};          // the filter (cork paper)
                   return y < 0.0245f ? Color{220, 196, 120, 255} : Color{238, 236, 230, 255};
               });
        cig = b.build(true);
        MeshBuilder e;
        e.cylinder({0.f, 0.0735f, 0.f}, 0.0038f, 0.0025f, 10, WHITE);
        ember = e.build();
    }
    goldMat = R->makeMat(Color{226, 180, 82, 255}, Texture2D{}, 0.9f, 90.f, 0.f, 0.2f);
    watchMat = R->makeMat(WHITE, Texture2D{}, 0.7f, 70.f, 0.f, 0.1f);
    tasselMat = R->makeMat(Color{150, 30, 34, 255}, Texture2D{}, 0.05f, 8.f, 0.f, 0.3f);
    cigMat = R->makeMat(WHITE, Texture2D{}, 0.1f, 10.f, 0.f, 0.1f);
    emberMat = R->makeMat(Color{255, 120, 40, 255}, Texture2D{}, 0.f, 8.f, 1.f, 0.f);
}

void PlayerHands::Impl::freeLook() {
    if (!lookBuilt) return;
    for (Mesh* m : {&upperMesh, &foreMesh, &foreSkin})
        if (m->vertexCount > 0) {
            UnloadMesh(*m);
            *m = Mesh{};
        }
    for (Mat* m : {&clothMat, &skinMat, &beadMat})
        if (m->material.maps) {
            R->unloadMat(*m);
            *m = Mat{};
        }
    hasForeSkin = false;
    lookBuilt = false;
}

// The sleeves for the look: the upper arm (shoulder -> elbow) and the forearm (elbow -> wrist) along +Y, cloth in
// vertex colours (a woven grain, a crease at the elbow), the skin forearm of rolled-up shirt sleeves apart (skin mat).
void PlayerHands::Impl::buildLook() {
    freeLook();
    const int kol = std::clamp(look.kol, 0, 2);
    const Color cloth = kCloth[std::clamp(look.kolRenk, 0, 4)];
    const Color shirt = look.kolRenk == 4 ? Color{238, 236, 228, 255} : Color{232, 230, 222, 255};
    const uint32_t seed = (uint32_t)(kol * 31 + look.kolRenk * 7 + 1);
    auto weave = [&](Color c, float u, float y, float amount) {
        const float n = chr::noise3({u * 9.f, y * 70.f, 0.f}, seed) - 0.5f;
        const float rib = kol == 2 ? 0.05f * std::sin(u * 2.f * PI * 26.f) : 0.f;  // knit
        return scaleC(c, 1.f + amount * n + rib);
    };
    {
        MeshBuilder b;
        std::vector<Vector2> p;
        capStart(p, 0.f, 0.050f);
        for (int i = 1; i <= 8; ++i) {
            const float t = (float)i / 8.f;
            p.push_back({0.050f + (0.045f - 0.050f) * t + 0.003f * std::sin(PI * t), kUpper * t});
        }
        for (int i = 1; i <= 5; ++i) {
            const float t = (PI / 2) * (float)i / 5.f;
            p.push_back({0.045f * std::cos(t), kUpper + 0.045f * std::sin(t)});
        }
        latheC(b, p, 18, [&](float u, float y) { return weave(kol == 1 ? shirt : cloth, u, y, 0.10f); });
        upperMesh = b.build(true);
    }
    {
        MeshBuilder b;
        std::vector<Vector2> p;
        if (kol == 1) {
            // rolled up below the elbow: a fat roll of shirt cloth, the bare forearm comes out of it
            const Color sc = look.kolRenk == 4 ? shirt : mixC(cloth, WHITE, 0.25f);
            capStart(p, 0.f, 0.047f);
            p.push_back({0.048f, 0.030f});
            p.push_back({0.053f, 0.050f});
            p.push_back({0.057f, 0.070f});
            p.push_back({0.056f, 0.090f});
            p.push_back({0.050f, 0.104f});
            p.push_back({0.043f, 0.108f});
            p.push_back({0.040f, 0.090f});
            latheC(b, p, 18, [&](float u, float y) {
                const float fold = y > 0.05f ? 0.92f + 0.08f * std::sin((y - 0.05f) * 160.f) : 1.f;
                return scaleC(weave(sc, u, y, 0.08f), fold);
            });
            foreMesh = b.build(true);
            MeshBuilder s;
            std::vector<Vector2> q;
            for (int i = 0; i <= 10; ++i) {
                const float t = (float)i / 10.f;
                const float y = 0.06f + (kFore + 0.004f - 0.06f) * t;
                q.push_back({0.039f + (0.0262f - 0.039f) * t + 0.004f * std::sin(PI * std::min(1.f, t * 1.6f)), y});
            }
            latheC(s, q, 16, [](float, float) { return WHITE; });
            foreSkin = s.build();
            hasForeSkin = true;
        } else {
            const float end = kFore - (kol == 2 ? 0.006f : 0.020f);  // the sleeve's end (a kazak's cuff reaches the wrist)
            capStart(p, 0.f, 0.048f);
            for (int i = 1; i <= 8; ++i) {
                const float t = (float)i / 8.f;
                p.push_back({0.048f + (0.0425f - 0.048f) * t, end * t});
            }
            if (kol == 0) {
                // the jacket sleeve's edge, then the shirt's cuff a finger's width out of it, then its inside
                p.push_back({0.0440f, end + 0.002f});
                p.push_back({0.0390f, end + 0.003f});
                p.push_back({0.0372f, end + 0.004f});
                p.push_back({0.0372f, kFore + 0.006f});
                p.push_back({0.0330f, kFore + 0.008f});
                p.push_back({0.0300f, kFore - 0.010f});
                latheC(b, p, 18, [&](float u, float y) {
                    if (y > end + 0.0025f) return weave(shirt, u, y, 0.04f);
                    return weave(cloth, u, y, 0.10f);
                });
            } else {
                // a knitted cuff: ribbed, a little tighter
                p.push_back({0.0410f, end - 0.030f});
                p.push_back({0.0388f, end - 0.026f});
                p.push_back({0.0372f, end});
                p.push_back({0.0340f, end + 0.004f});
                p.push_back({0.0300f, end - 0.012f});
                latheC(b, p, 24, [&](float u, float y) {
                    if (y > end - 0.028f) return scaleC(cloth, 0.86f + 0.16f * (std::sin(u * 2.f * PI * 22.f) > 0.f ? 1.f : 0.f));
                    return weave(cloth, u, y, 0.10f);
                });
            }
            foreMesh = b.build(true);
        }
    }
    clothMat = R->makeMat(WHITE, Texture2D{}, kol == 0 ? 0.10f : 0.05f, kol == 0 ? 14.f : 8.f, 0.f, 0.32f);
    skinMat = R->makeMat(kSkin[std::clamp(look.ten, 0, 3)], Texture2D{}, 0.22f, 18.f, 0.f, 0.22f);
    const Color bc = kBeadCol[std::clamp(look.tespih, 0, 4)];
    beadMat = R->makeMat(bc, Texture2D{}, look.tespih == 1 ? 0.8f : 0.6f, 60.f, look.tespih == 1 ? 0.06f : 0.f, 0.25f);
    if (people) people->setPlayerGlassStyle(look.bardak);
    lookBuilt = true;
}

void PlayerHands::Impl::setRoot() {
    if (scene == Tavla) root = MatrixMultiply(MatrixTranslate(0.f, 0.f, w3d::TAVLA_SEAT_DIST), w3d::tavlaFrame());
    else root = MatrixTranslate(w3d::seatPos(0).x, 0.f, w3d::seatPos(0).z);
    rootInv = MatrixInvert(root);
}

// ============================================================================ keys
// Rest spots on the table edge: the right hand beside the istaka's right end (clear of the tiles and of Hacı Rıza's
// saucer), the left one lying along the edge in front of the own glass. At the tavla table both lie outside the box.
// variant: 0 rest, 1 shifted a little (fidget), 2 the tespih held over the edge (left), 3 the cigarette (right).
Key PlayerHands::Impl::restKey(bool left, int variant) const {
    const float sd = left ? -1.f : 1.f;
    const bool tavla = scene == Tavla;
    if (variant == 2 && left) {
        const Vector3 palmC{-0.285f, TY + 0.070f, -0.295f};
        const Vector3 f = Vector3Normalize({0.42f, -0.30f, -0.86f}), p = Vector3Normalize({0.75f, -0.62f, 0.12f});
        Key k = chr::mk(0.f, chr::wristFor(palmC, f, p, chr::PALM_CENTER, kHS, true), f, p, HandPose::Hold);
        k.relax = 0.f;
        return k;
    }
    if (variant == 3 && !left) {
        const Vector3 palmC{tavla ? 0.40f : 0.37f, TY + 0.034f, -0.345f};
        const Vector3 f = Vector3Normalize({-0.30f, 0.06f, -0.95f}), p = Vector3Normalize({-0.80f, -0.58f, 0.05f});
        Key k = chr::mk(0.f, chr::wristFor(palmC, f, p, chr::PALM_CENTER, kHS, false), f, p, HandPose::Cig);
        k.relax = 0.f;
        return k;
    }
    const float shift = variant == 1 ? 0.025f : 0.f;
    if (left && !tavla) {
        // along the table edge, fingers toward the istaka's left end, below its front (the palm on the raised rim)
        Key k = chr::onFelt(-0.345f + shift, -0.300f - shift * 0.5f, {0.86f, 0.f, -0.50f}, {0.f, -1.f, 0.f}, HandPose::Rest, kHS, true);
        k.pos.y += w3d::RIM_H;
        return k;
    }
    if (tavla) return chr::onFelt(sd * (0.415f - shift), -0.325f, {-sd * 0.30f, 0.f, -0.95f}, {0.f, -1.f, 0.f}, HandPose::Rest, kHS, left);
    return chr::onFelt(0.372f - shift, -0.352f + shift * 0.4f, {-0.36f, 0.f, -0.93f}, {0.f, -1.f, 0.f}, HandPose::Rest, kHS, false);
}

// The left hand holding the fan (cardlayout's frame: x across, y the faces, -z up the cards): a book held by its
// spine, as the regulars hold theirs (CharactersCards.cpp cardHoldKey), the thumb on the faces at the pivot.
Key PlayerHands::Impl::fanKey() const {
    const Matrix F = MatrixMultiply(fanFrame, rootInv);
    const Vector3 r = Vector3Normalize(chr::mX(F)), n = Vector3Normalize(chr::mY(F)), u = Vector3Normalize(Vector3Negate(chr::mZ(F)));
    const Vector3 pivot = chr::mPos(F);
    Vector3 f = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(u, 0.55f), Vector3Scale(r, 0.30f)), Vector3Scale(n, -0.58f)));
    Vector3 p = Vector3Normalize(Vector3Add(Vector3Scale(r, 0.80f), Vector3Scale(n, 0.42f)));
    p = Vector3Normalize(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
    const Vector3 palmC = Vector3Subtract(Vector3Subtract(pivot, Vector3Scale(r, 0.034f)), Vector3Scale(u, 0.010f));
    Key k = chr::mk(0.f, chr::wristFor(palmC, f, p, chr::PALM_CENTER, kHS, true), f, p, HandPose::Hold);
    k.relax = 0.f;
    return k;
}

// Fingers toward the object (from the shoulder, flat), tipped down by the style's angle; the palm down.
void PlayerHands::Impl::pickDirs(bool left, Vector3 t, int style, Vector3& f, Vector3& p) const {
    const Vector3 sh = shoulderBase(left);
    Vector3 d{t.x - sh.x, 0.f, t.z - sh.z};
    d = Vector3Length(d) > 1e-4f ? Vector3Normalize(d) : Vector3{0, 0, -1};
    const float a = (style == 2 ? 62.f : style == 1 ? 38.f : 50.f) * DEG2RAD;
    f = Vector3Normalize(Vector3Add(Vector3Scale(d, std::cos(a)), {0.f, -std::sin(a), 0.f}));
    // the palm down, turned a little toward the body's middle (the thumb side comes up)
    const Vector3 side = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, d));  // to the left of the reach
    p = Vector3Add({0.f, -1.f, 0.f}, Vector3Scale(side, left ? -0.25f : 0.25f));
    p = Vector3Normalize(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
}

Key PlayerHands::Impl::pinchKey(bool left, Vector3 g, int style) const {
    Vector3 f, p;
    pickDirs(left, g, style, f, p);
    Key k = chr::mk(0.f, chr::wristFor(Vector3Add(g, {0.f, 0.003f, 0.f}), f, p, chr::PINCH_POINT, kHS, left), f, p, HandPose::Pinch);
    k.relax = 0.f;
    return k;
}

void PlayerHands::Impl::startTrack(Arm& A, std::vector<Key> keys, int idleKind, bool atAnimSpeed) {
    if (keys.empty()) return;
    keys[0] = A.cur;
    keys[0].t = 0.f;
    keys[0].event = 0;
    keys[0].headRel = false;
    keys[0].ease = 0;
    keys[0].lift = 0.f;
    A.track.keys = std::move(keys);
    A.track.t = 0.f;
    A.track.on = true;
    A.track.nextKey = 1;
    auto cap = [](Vector3 v, float m) {
        const float l = Vector3Length(v);
        return l > m ? Vector3Scale(v, m / l) : v;
    };
    A.track.v0 = A.velInit ? cap(A.vel, 1.6f) : Vector3{0, 0, 0};
    A.track.f0 = A.track.p0 = Vector3{0, 0, 0};
    A.rate = atAnimSpeed ? -1.f : 1.f;
    A.idleKind = idleKind;
}

// Let go of what the hand holds: up and back off it, then home.
void PlayerHands::Impl::releaseTo(Arm& A, float lift) {
    A.follow = false;
    A.attached = false;
    A.cue = HandCue{};
    Key off = A.cur;
    off.t = 0.16f;
    off.pos = Vector3Add(off.pos, {0.f, lift, 0.028f});
    off.pose = HandPose::Open;
    off.ease = 0;
    Key home = A.hold;
    home.t = 0.80f;
    startTrack(A, {Key{}, off, home}, 0, true);
}

void PlayerHands::Impl::dropGlass(Arm& A) {
    if (!A.glass) return;
    A.glass = false;
    if (people) people->holdPlayerGlass(nullptr);
}

// ============================================================================ behaviours
void PlayerHands::Impl::startCue(const HandCue& c) {
    if (c.kind == HandCueKind::CardFan) {
        fan = c.on;
        if (c.on) fanFrame = c.frame;
        Arm& L = arm[1];
        dropGlass(L);
        if (L.follow) L.follow = false;
        Key k = fan ? fanKey() : restKey(true, look.tespih > 0 ? 2 : 0);
        L.hold = k;
        k.t = 0.55f;
        startTrack(L, {Key{}, k}, 0, true);
        return;
    }
    // the right hand does the work; the left one takes what lies on the left (the left neighbour's discard), when it
    // is free (not holding the cards or the tea)
    const Vector3 fromL = xfPoint(rootInv, c.from);
    const bool leftFree = !fan && !arm[1].glass && !(arm[1].track.on && arm[1].idleKind == 1);
    Arm& A = c.kind == HandCueKind::Take && fromL.x < -0.15f && leftFree ? arm[1] : arm[0];
    if (&A == &arm[1] && arm[0].follow && arm[0].cue.kind == HandCueKind::Take) releaseTo(arm[0], 0.03f);
    dropGlass(A);
    if (c.kind == HandCueKind::Dice) {
        startDice(A, c);
        return;
    }
    if (!c.where) return;
    Vector3 g;
    HandCue cc = c;
    if (!cc.where(g)) return;
    const Vector3 gL = xfPoint(rootInv, g);
    // already holding something right there (a dragged tile let go into its flight): carry straight on
    const Vector3 pinchNow = xfPoint(rootInv, xfPoint(A.hand, chr::mirrorL(chr::PINCH_POINT, A.left)));
    const bool near = A.follow && A.attached && Vector3Distance(xfPoint(root, pinchNow), g) < 0.07f;
    A.follow = true;
    A.cue = cc;
    A.ft = 0.f;
    A.attached = false;
    A.start = A.cur;
    A.startVel = A.velInit ? A.vel : Vector3{0, 0, 0};
    A.track.on = false;
    A.idleKind = 0;
    const float dist = Vector3Distance(A.cur.pos, pinchKey(A.left, gL, c.style).pos);
    const float leadReal = c.lead / std::max(0.25f, speed);
    if (near) A.reachDur = 0.06f;
    else if (c.kind == HandCueKind::Carry) A.reachDur = clampf(0.10f + dist * 0.25f, 0.12f, 0.24f);
    else A.reachDur = clampf(std::max(leadReal, 0.12f + dist * 0.18f / std::max(0.5f, speed)), 0.10f, 0.6f);
}

// The dice in the fist, a shake or two over the near edge of the board, then flung out of the opening hand when the
// table lets them go (the cue's lead), the hand following through, then home.
void PlayerHands::Impl::startDice(Arm& A, const HandCue& c) {
    A.follow = false;
    const float s = 1.f / std::max(0.25f, speed);
    const float go = std::max(0.30f, c.lead * s);
    const Vector3 from = xfPoint(rootInv, c.from);
    const Vector3 f = Vector3Normalize({-0.25f, -0.35f, -0.90f});
    const Vector3 p = Vector3Normalize({-0.30f, -0.90f, 0.30f});
    const Vector3 w = chr::wristFor(Vector3Add(from, {0.02f, 0.015f, 0.05f}), f, p, chr::FIST_TOP, kHS, false);
    std::vector<Key> k;
    k.push_back(Key{});
    k.push_back(chr::mk(go * 0.45f, Vector3Add(w, {0.f, 0.02f, 0.03f}), f, p, HandPose::Fist, 0.03f));
    k.push_back(chr::mk(go * 0.62f, Vector3Add(w, {0.012f, 0.045f, 0.02f}), f, p, HandPose::Fist, 0.f, 1));
    k.push_back(chr::mk(go * 0.80f, Vector3Add(w, {-0.006f, 0.015f, 0.035f}), f, p, HandPose::Fist, 0.f, 1));
    const Vector3 f2 = Vector3Normalize({-0.15f, -0.55f, -0.82f});
    k.push_back(chr::mk(go, w, f, p, HandPose::Fist, 0.f, 2, EV_DiceGo));
    k.push_back(chr::mk(go + 0.14f * s, Vector3Add(w, {-0.03f, -0.015f, -0.07f}), f2, p, HandPose::Open, 0.f, 1));
    Key home = A.hold;
    home.t = go + 0.75f * s;
    k.push_back(home);
    for (Key& kk : k) kk.relax = 0.f;
    startTrack(A, k, 5, false);
}

// The glass from its saucer to the lips and back (as the regulars sip, CharactersAnim startSip): the hand on its side
// round the glass, a lift, two tilts at the mouth (head space: it follows where the player looks), down again.
void PlayerHands::Impl::startSip(Arm& A) {
    Vector3 baseW;
    float level = 0.f;
    if (!people || !people->playerGlass(baseW, level) || level < 0.08f) return;
    const bool left = A.left;
    const float sd = left ? -1.f : 1.f;
    const Vector3 base = xfPoint(rootInv, baseW);
    Vector3 f0, p0;
    chr::gripDirs({0, 1, 0}, Vector3Normalize({-sd * 0.35f, 0.f, -1.f}), left, f0, p0);
    const Vector3 grip = Vector3Add(base, {0.f, kGripH, 0.f});
    const Vector3 wGrip = chr::wristFor(grip, f0, p0, chr::GRIP_CENTER, kHS, left);
    const Vector3 wPre = chr::wristFor(Vector3Add(grip, {sd * 0.035f, 0.02f, 0.035f}), f0, p0, chr::GRIP_CENTER, kHS, left);
    const Vector3 wLift = Vector3Add(wGrip, {-sd * 0.02f, 0.08f, 0.05f});
    const float tulip = people->glassHeight();
    auto atMouth = [&](float tiltDeg, float t, int ev) {
        const float th = tiltDeg * DEG2RAD;
        const Vector3 ax{0.f, std::cos(th), std::sin(th)};
        const Vector3 rim{0.f, -0.082f, -0.040f};  // under the lips (head space; the eye at the origin)
        const Vector3 b = Vector3Subtract(rim, Vector3Scale(ax, tulip));
        const Vector3 gp = Vector3Add(b, Vector3Scale(ax, kGripH));
        Vector3 f, p;
        chr::gripDirs(ax, {-sd * 0.25f, std::sin(th), -std::cos(th)}, left, f, p);
        Key k = chr::mk(t, chr::wristFor(gp, f, p, chr::GRIP_CENTER, kHS, left), f, p, HandPose::Grip, 0.f, 0, ev);
        k.headRel = true;
        k.relax = 0.f;
        return k;
    };
    std::vector<Key> k;
    k.push_back(Key{});
    k.push_back(chr::mk(0.50f, wPre, f0, p0, HandPose::Hold, 0.03f));
    k.push_back(chr::mk(0.75f, wGrip, f0, p0, HandPose::Grip, 0.f, 0, EV_GrabGlass));
    k.push_back(chr::mk(1.05f, wLift, f0, p0, HandPose::Grip, 0.f, 1));
    k.push_back(atMouth(28.f, 1.60f, 0));
    k.push_back(atMouth(56.f, 2.10f, EV_Sip));
    k.push_back(atMouth(36.f, 2.45f, 0));
    k.push_back(chr::mk(3.00f, wLift, f0, p0, HandPose::Grip, 0.f, 0));
    k.push_back(chr::mk(3.30f, wGrip, f0, p0, HandPose::Grip, 0.f, 1, EV_ReleaseGlass));
    k.push_back(chr::mk(3.52f, wPre, f0, p0, HandPose::Hold, 0.f, 1));
    Key rest = A.hold;
    rest.t = 4.0f;
    k.push_back(rest);
    for (size_t i = 1; i < k.size(); ++i) k[i].relax = 0.f;
    startTrack(A, k, 1, false);
}

// The tespih hand comes up and flicks the beads over (CharactersAnim startTespihFlip).
void PlayerHands::Impl::startTespih(Arm& A) {
    const Key base = restKey(true, 2);
    Key up = base;
    up.t = 0.35f;
    up.pos = Vector3Add(base.pos, {-0.015f, 0.08f, -0.01f});
    up.palm = Vector3Normalize({0.6f, 0.4f, 0.3f});
    up.ease = 1;
    Key flick = base;
    flick.t = 0.62f;
    flick.pos = Vector3Add(base.pos, {0.02f, 0.025f, -0.05f});
    flick.palm = Vector3Normalize({0.7f, -0.7f, -0.2f});
    flick.ease = 2;
    up.pole = flick.pole = Vector3Normalize({-0.45f, -0.85f, 0.25f});
    Key back = base;
    back.t = 1.25f;
    startTrack(A, {Key{}, up, flick, back}, 2, false);
    flickKick = 1.f;
}

// A draw on the cigarette: the filter to the corner of the mouth (head space), a moment, away, the smoke out.
void PlayerHands::Impl::startSmoke(Arm& A) {
    const Vector3 c = Vector3Normalize({-0.30f, -0.20f, -1.f});  // the cigarette's axis = hand +Y (head space)
    const Vector3 p = Vector3Negate(c);
    // the fingers up past the cheek, the hand under the face and the elbow out to the side: the hand stays at the
    // bottom edge of the view instead of filling it
    Vector3 f = Vector3Normalize({0.15f, 1.f, 0.35f});
    f = Vector3Normalize(Vector3Subtract(f, Vector3Scale(p, Vector3DotProduct(f, p))));
    const Vector3 lips{0.026f, -0.086f, -0.018f};
    const Vector3 filterOff = Vector3Add(chr::CIG_HOLD, {0.f, -0.020f, 0.f});
    const Vector3 w = chr::wristFor(lips, f, p, filterOff, kHS, false);
    Key at = chr::mk(0.72f, w, f, p, HandPose::Cig, 0.f, 0, EV_Drag);
    at.headRel = true;
    at.relax = 0.f;
    at.pole = Vector3Normalize({1.f, -0.55f, 0.35f});
    Key hold = at;
    hold.t = 1.35f;
    hold.event = 0;
    Key away = at;
    away.t = 1.65f;
    away.pos = Vector3Add(w, {0.06f, -0.07f, -0.06f});
    away.event = EV_Exhale;
    // (up along the side of the view and back, so the hand doesn't sweep through the middle of it)
    Key side = chr::mk(0.30f, Vector3Add(A.hold.pos, {0.14f, 0.16f, 0.16f}), A.hold.fingers, A.hold.palm, HandPose::Cig, 0.f);
    side.relax = 0.f;
    side.pole = at.pole;
    Key side2 = side;
    side2.t = 2.0f;
    Key rest = A.hold;
    rest.t = 2.55f;
    startTrack(A, {Key{}, side, at, hold, away, side2, rest}, 3, false);
}

void PlayerHands::Impl::startFidget(Arm& A) {
    const bool shifted = A.hold.pos.x != restKey(A.left, 0).pos.x;
    if (A.left && look.tespih > 0) return;
    if (!A.left && look.sigara) return;
    Key to = restKey(A.left, shifted ? 0 : 1);
    A.hold = to;
    to.t = 0.7f;
    startTrack(A, {Key{}, to}, 4, false);
}

void PlayerHands::Impl::idle(float dt) {
    const bool calm = !lowered && low < 0.2f;
    sipIn -= dt;
    tespihIn -= dt;
    smokeIn -= dt;
    fidgetIn[0] -= dt;
    fidgetIn[1] -= dt;
    auto freeArm = [&](Arm& A) { return !A.follow && !A.track.on && !(A.left && fan); };
    // the glass is drunk with the hand on its side (the right one if the left holds the cards)
    Vector3 gb;
    float lvl;
    const bool glassOk = people && people->playerGlass(gb, lvl) && lvl > 0.1f;
    const bool glassLeft = glassOk && xfPoint(rootInv, gb).x < 0.f;
    Arm& sipArm = glassLeft && !fan ? arm[1] : arm[0];
    const std::string ask = debugAsk;
    debugAsk.clear();
    if (ask == "sip" || (calm && !myTurn && sipIn <= 0.f && glassOk && freeArm(sipArm))) {
        sipIn = rng.f(28.f, 55.f);
        if (glassOk) startSip(sipArm);
        return;
    }
    if (look.tespih > 0 && !fan && (ask == "tespih" || (calm && tespihIn <= 0.f && freeArm(arm[1])))) {
        tespihIn = rng.f(7.f, 15.f);
        startTespih(arm[1]);
    }
    if (look.sigara && (ask == "smoke" || (calm && !myTurn && smokeIn <= 0.f && freeArm(arm[0])))) {
        smokeIn = rng.f(14.f, 26.f);
        startSmoke(arm[0]);
    }
    for (int a = 0; a < 2; ++a)
        if (calm && fidgetIn[a] <= 0.f) {
            fidgetIn[a] = rng.f(7.f, 16.f);
            if (freeArm(arm[a])) startFidget(arm[a]);
        }
}

// ============================================================================ per frame
void PlayerHands::Impl::fireEvents(Arm& A, float before, float after) {
    (void)before;
    while (A.track.nextKey < (int)A.track.keys.size() && A.track.keys[(size_t)A.track.nextKey].t <= after) {
        const int ev = A.track.keys[(size_t)A.track.nextKey].event;
        ++A.track.nextKey;
        switch (ev) {
        case EV_GrabGlass: A.glass = true; break;
        case EV_ReleaseGlass: dropGlass(A); break;
        case EV_Sip:
            if (A.glass && people) {
                people->sipPlayerGlass();
                if (sfx && *sfx) (*sfx)(ui::Sfx::TeaSip);
            }
            break;
        case EV_Drag: drag = 1.f; break;
        case EV_Exhale: exhaleT = 0.f; break;
        default: break;
        }
    }
}

Key PlayerHands::Impl::evalArm(Arm& A, float dt) {
    auto resolve = [&](Key k) {
        if (k.headRel) {
            k.pos = xfPoint(headLocal, k.pos);
            k.fingers = Vector3Normalize(xfDir(headLocal, k.fingers));
            k.palm = Vector3Normalize(xfDir(headLocal, k.palm));
            k.headRel = false;
        }
        return k;
    };
    Key k;
    if (A.follow) {
        A.ft += dt;
        Vector3 g;
        if (!A.cue.where || !A.cue.where(g)) {
            releaseTo(A, A.cue.style == 1 ? 0.02f : 0.035f);
        } else {
            const Vector3 gL = xfPoint(rootInv, g);
            Key target = pinchKey(A.left, gL, A.cue.style);
            if (A.ft < A.reachDur && !A.attached) {
                const float u = clampf(A.ft / A.reachDur, 0.f, 1.f), e = smoother(u);
                k = target;
                k.pos = Vector3Lerp(A.start.pos, target.pos, e);
                // the motion the hand had carries on into the reach (Hermite's start tangent), and an arc over things
                const float h10 = u * (1.f - u) * (1.f - u);
                k.pos = Vector3Add(k.pos, Vector3Scale(A.startVel, A.reachDur * h10));
                const float arc = std::min(0.07f, Vector3Distance(A.start.pos, target.pos) * 0.22f);
                k.pos.y += arc * 16.f * u * u * (1.f - u) * (1.f - u);
                k.fingers = Vector3Normalize(Vector3Lerp(A.start.fingers, target.fingers, e));
                k.palm = Vector3Normalize(Vector3Lerp(A.start.palm, target.palm, e));
                k.pose = u < 0.55f ? HandPose::Open : HandPose::Pinch;
            } else {
                A.attached = true;
                k = target;
            }
            return k;
        }
    }
    if (A.track.on) {
        const float before = A.track.t;
        A.track.t += dt * (A.rate < 0.f ? speed : A.rate);
        Track tmp;
        tmp.v0 = A.track.v0;
        tmp.keys.reserve(A.track.keys.size());
        for (const Key& kk : A.track.keys) tmp.keys.push_back(resolve(kk));
        chr::evalTrack(tmp, A.track.t, k);
        fireEvents(A, before, A.track.t);
        if (A.track.t >= A.track.duration()) {
            A.track.on = false;
            A.idleKind = 0;
        }
    } else {
        k = resolve(A.hold);
    }
    return k;
}

void PlayerHands::Impl::solveArm(Arm& A, Key k, float dt) {
    const float sd = A.left ? -1.f : 1.f;
    // how the hand moves (a new track carries this on)
    if (dt > 0.f) {
        if (A.velInit) A.vel = Vector3Scale(Vector3Subtract(k.pos, A.cur.pos), 1.f / dt);
        A.velInit = true;
    }
    A.cur = k;
    A.pose = k.pose;
    // sinking out of view (a screen is up / switched off / no scene): down off the table edge and back
    if (low > 0.001f) {  // back off the table first, then down behind its edge
        const float eb = smoother(low * 1.8f), ed = smoother(low * 1.8f - 0.8f);
        const Vector3 r0 = restKey(A.left, 0).pos;
        k.pos = Vector3Lerp(k.pos, Vector3Add(r0, {sd * 0.05f, 0.03f, 0.20f}), eb);
        k.pos.y = lerpf(k.pos.y, r0.y - 0.34f, ed);
    }
    // the shoulder leans in after a far target (the body bends forward; under the camera, out of view)
    const Vector3 sh0 = shoulderBase(A.left);
    const Vector3 toW = Vector3Subtract(k.pos, sh0);
    const float need = clampf(Vector3Length(toW) - kReach, 0.f, kMaxLean);
    if (need > 0.001f) {
        Vector3 d{toW.x, 0.f, toW.z};
        if (Vector3Length(d) > 1e-3f) A.leanDir = Vector3Normalize(d);
    }
    if (dt > 0.f) chr::spring(A.lean, A.leanV, need, need < A.lean ? 9.f : 11.f, dt);
    else A.lean = need;
    const Vector3 shL = Vector3Add(sh0, Vector3Add(Vector3Scale(A.leanDir, A.lean), {0.f, -0.22f * A.lean, 0.f}));
    // elbows out and down by the sides
    Vector3 poleL = Vector3Normalize({sd * 0.62f, -0.72f, 0.30f});
    if (Vector3Length(k.pole) > 0.01f) poleL = Vector3Normalize(Vector3Add(Vector3Scale(poleL, 0.4f), Vector3Scale(Vector3Normalize(k.pole), 0.6f)));
    if (dt <= 0.f || Vector3Length(A.pole) < 0.5f) A.pole = poleL;
    else A.pole = Vector3Normalize(chr::approachExp(A.pole, poleL, 12.f, dt));
    Vector3 w = k.pos;
    const Vector3 elbow = chr::solveTwoBone(shL, w, kUpper, kFore, A.pole);
    // a relaxed hand follows the forearm a little; no wrist bends further than a wrist can
    Vector3 f = Vector3Normalize(k.fingers), p = Vector3Normalize(k.palm);
    const Vector3 fd = Vector3Normalize(Vector3Subtract(w, elbow));
    const bool held = k.pose == HandPose::Grip || k.pose == HandPose::Hold || k.pose == HandPose::Cig || k.pose == HandPose::Fist;
    if (!held) {
        const float maxBend = 74.f * DEG2RAD;
        const float c = clampf(Vector3DotProduct(f, fd), -1.f, 1.f);
        if (std::acos(c) > maxBend) {
            Vector3 side = Vector3Subtract(f, Vector3Scale(fd, c));
            if (Vector3Length(side) > 1e-4f) {
                side = Vector3Normalize(side);
                f = Vector3Normalize(Vector3Add(Vector3Scale(fd, std::cos(maxBend)), Vector3Scale(side, std::sin(maxBend))));
            }
        }
    }
    const Vector3 back = xfDir(root, {0, 0, 1});
    A.shoulder = xfPoint(root, shL);
    A.elbow = xfPoint(root, elbow);
    A.wrist = xfPoint(root, w);
    A.upper = chr::boneMatrix(A.shoulder, A.elbow, back);
    A.fore = chr::boneMatrix(A.elbow, A.wrist, back);
    A.hand = chr::handMatrix(A.wrist, xfDir(root, f), xfDir(root, p), kHS);
}

// The tespih: a loop of beads hanging from the left hand's fingers (Verlet, as Hacı Rıza's), the imame and its tassel
// below the grip; the table edge and the felt hold them up.
void PlayerHands::Impl::updateTespih(float dt) {
    const Arm& A = arm[1];
    const Matrix& H = A.hand;
    const Vector3 grip = xfPoint(H, chr::mirrorL(kTespihGrip, true));
    const Vector3 side = Vector3Normalize(xfDir(H, {1, 0, 0}));
    const Vector3 endA = Vector3Add(grip, Vector3Scale(side, 0.006f)), endB = Vector3Subtract(grip, Vector3Scale(side, 0.006f));
    const int N = kChainN;
    if (!chainInit) {
        chain.assign((size_t)N, grip);
        for (int i = 0; i < N; ++i) {
            const float t = (float)i / (float)(N - 1);
            chain[(size_t)i] = Vector3Add(Vector3Lerp(endA, endB, t), {0.f, -std::sin(t * PI) * 0.09f, 0.f});
        }
        chainPrev = chain;
        tail = {grip, Vector3Add(grip, {0, -0.03f, 0}), Vector3Add(grip, {0, -0.065f, 0})};
        tailPrev = tail;
        chainInit = true;
    }
    const int sub = 3;
    const float h = std::min(dt, 0.05f) / (float)sub;
    const Vector3 grav{0.f, -9.8f, 0.f};
    auto collide = [&](Vector3& q) {
        // the table top (felt + rim) of whichever table we sit at
        const Vector3 l = scene == Tavla ? w3d::tavlaToLocal(q) : q;
        const float hx = scene == Tavla ? w3d::TAVLA_HALF_W : w3d::FELT_HALF + w3d::RIM_W;
        const float hz = scene == Tavla ? w3d::TAVLA_HALF_D : w3d::FELT_HALF + w3d::RIM_W;
        if (std::fabs(l.x) < hx && std::fabs(l.z) < hz && q.y < TY + kBeadR + 0.01f && q.y > TY - 0.06f)
            q.y = std::max(q.y, TY + kBeadR + (scene != Tavla && (std::fabs(l.x) > w3d::FELT_HALF || std::fabs(l.z) > w3d::FELT_HALF) ? w3d::RIM_H : 0.f));
    };
    const float kick = flickKick;
    flickKick = std::max(0.f, flickKick - dt * 3.f);
    for (int s = 0; s < sub; ++s) {
        for (int i = 1; i < N - 1; ++i) {
            Vector3& q = chain[(size_t)i];
            const Vector3 v = Vector3Scale(Vector3Subtract(q, chainPrev[(size_t)i]), 0.985f);
            chainPrev[(size_t)i] = q;
            q = Vector3Add(Vector3Add(q, v), Vector3Scale(grav, h * h));
        }
        for (size_t i = 1; i < tail.size(); ++i) {
            Vector3& q = tail[i];
            const Vector3 v = Vector3Scale(Vector3Subtract(q, tailPrev[i]), 0.98f);
            tailPrev[i] = q;
            q = Vector3Add(Vector3Add(q, v), Vector3Scale(grav, h * h));
        }
        chain[0] = endA;
        chain[(size_t)N - 1] = endB;
        tail[0] = grip;
        for (int it = 0; it < 4; ++it) {
            for (int i = 0; i + 1 < N; ++i) {
                Vector3& a = chain[(size_t)i];
                Vector3& b = chain[(size_t)i + 1];
                const Vector3 d = Vector3Subtract(b, a);
                const float l = Vector3Length(d);
                if (l < 1e-6f) continue;
                const float diff = (l - kChainSeg) / l;
                const float wa = i == 0 ? 0.f : 0.5f, wb = i + 1 == N - 1 ? 0.f : 0.5f;
                const float ws = wa + wb > 0.f ? wa + wb : 1.f;
                a = Vector3Add(a, Vector3Scale(d, diff * wa / ws));
                b = Vector3Subtract(b, Vector3Scale(d, diff * wb / ws));
            }
            for (size_t i = 0; i + 1 < tail.size(); ++i) {
                Vector3& a = tail[i];
                Vector3& b = tail[i + 1];
                const Vector3 d = Vector3Subtract(b, a);
                const float l = Vector3Length(d);
                const float seg = i == 0 ? 0.034f : 0.036f;
                if (l < 1e-6f) continue;
                const Vector3 corr = Vector3Scale(d, (l - seg) / l);
                if (i == 0) b = Vector3Subtract(b, corr);
                else {
                    a = Vector3Add(a, Vector3Scale(corr, 0.5f));
                    b = Vector3Subtract(b, Vector3Scale(corr, 0.5f));
                }
            }
            for (int i = 1; i < N - 1; ++i) collide(chain[(size_t)i]);
            for (size_t i = 1; i < tail.size(); ++i) collide(tail[i]);
        }
    }
    (void)kick;
}

void PlayerHands::Impl::updateSmoke(float dt) {
    drag = std::max(0.f, drag - dt * 0.9f);
    if (!look.sigara || low > 0.9f) return;
    const Matrix cigW = MatrixMultiply(MatrixTranslate(chr::CIG_HOLD.x, chr::CIG_HOLD.y - 0.020f, chr::CIG_HOLD.z), arm[0].hand);
    const Vector3 tip = xfPoint(cigW, {0.f, 0.076f, 0.f});
    smokeAcc += dt;
    if (smokeAcc > 0.30f) {  // a thin ribbon from the tip
        smokeAcc = rng.f(0.f, 0.08f);
        SmokeParams sp;
        sp.velocity = {rng.f(-0.004f, 0.004f), 0.05f, rng.f(-0.004f, 0.004f)};
        sp.size0 = 0.008f;
        sp.size1 = 0.07f;
        sp.life = 3.0f;
        sp.alpha = 0.10f;
        sp.turbulence = 0.7f;
        sp.buoyancy = 0.02f;
        R->emitSmoke(tip, sp);
    }
    if (exhaleT >= 0.f) {  // breathed out in front of the eyes
        const float before = exhaleT;
        exhaleT += dt;
        const Vector3 fwd = Vector3Normalize(Vector3Subtract(eye.target, eye.position));
        for (float t = 0.f; t < 0.9f; t += 0.06f)
            if (before < t && exhaleT >= t) {
                SmokeParams sp;
                sp.velocity = Vector3Add(Vector3Scale(fwd, 0.22f + rng.f(0.f, 0.08f)), {rng.f(-0.02f, 0.02f), 0.03f, rng.f(-0.02f, 0.02f)});
                sp.size0 = 0.02f;
                sp.size1 = 0.22f;
                sp.life = 2.6f;
                sp.alpha = 0.16f;
                sp.turbulence = 1.f;
                sp.buoyancy = 0.03f;
                R->emitSmoke(Vector3Add(mouthWorld(), Vector3Scale(fwd, 0.10f)), sp);
            }
        if (exhaleT > 1.0f) exhaleT = -1.f;
    }
}

// ============================================================================ public
PlayerHands::PlayerHands() : impl_(new Impl) {
    impl_->sfx = &playSfx;
    impl_->arm[0].left = false;
    impl_->arm[1].left = true;
}
PlayerHands::~PlayerHands() { delete impl_; }

bool PlayerHands::init(Renderer& r, Characters* people, uint64_t seed) {
    Impl& m = *impl_;
    m.R = &r;
    m.people = people;
    m.rng = chr::Rng(seed ^ 0x48A2D5ull);
    m.sipIn = m.rng.f(14.f, 30.f);
    m.buildStatic();
    m.buildLook();
    m.setRoot();
    for (Impl::Arm& A : m.arm) {
        A.hold = m.restKey(A.left, 0);
        A.cur = A.hold;
    }
    if (const char* e = std::getenv("SAKLI_ELLER")) m.debugAsk = e;
    m.ready = true;
    return true;
}

void PlayerHands::shutdown(Renderer& r) {
    Impl& m = *impl_;
    if (!m.ready) return;
    m.R = &r;
    reset();
    for (Impl::Arm& A : m.arm) m.dropGlass(A);
    m.freeLook();
    for (Mesh* x : {&m.ring, &m.watch, &m.bead, &m.imame, &m.tassel, &m.cig, &m.ember})
        if (x->vertexCount > 0) {
            UnloadMesh(*x);
            *x = Mesh{};
        }
    for (Mat* x : {&m.goldMat, &m.watchMat, &m.tasselMat, &m.cigMat, &m.emberMat})
        if (x->material.maps) {
            r.unloadMat(*x);
            *x = Mat{};
        }
    m.ready = false;
}

void PlayerHands::setLook(const PlayerLook& look) {
    Impl& m = *impl_;
    if (m.lookBuilt && look == m.look) return;
    const bool restChange = look.tespih != m.look.tespih || look.sigara != m.look.sigara || !m.lookBuilt;
    m.look = look;
    if (!m.ready) return;
    m.buildLook();
    if (restChange) {
        m.chainInit = false;
        m.arm[0].hold = m.restKey(false, look.sigara ? 3 : 0);
        if (!m.fan) m.arm[1].hold = m.restKey(true, look.tespih > 0 ? 2 : 0);
    }
}

void PlayerHands::setEnabled(bool on) {
    Impl& m = *impl_;
    if (on == m.enabled) return;
    m.enabled = on;
    if (!on) {
        reset();
        for (Impl::Arm& A : m.arm) m.dropGlass(A);
    }
}

void PlayerHands::setScene(Scene s) {
    Impl& m = *impl_;
    if (s == m.scene) return;
    reset();
    for (Impl::Arm& A : m.arm) m.dropGlass(A);
    m.scene = s;
    if (s == Hidden || s == Okey) m.fan = false;  // (a card table keeps the fan it may have announced already)
    m.setRoot();
    m.chainInit = false;
    for (Impl::Arm& A : m.arm) {
        A.track.on = false;
        A.hold = A.left && m.fan ? m.fanKey() : m.restKey(A.left, A.left ? (m.look.tespih > 0 ? 2 : 0) : (m.look.sigara ? 3 : 0));
        A.cur = A.hold;
        A.velInit = false;
        A.lean = A.leanV = 0.f;
    }
    m.low = 1.f;  // (they come up from below at the new table)
    m.lowV = 0.f;
}

void PlayerHands::setLowered(bool lowered) { impl_->lowered = lowered; }
void PlayerHands::setMyTurn(bool mine) { impl_->myTurn = mine; }
void PlayerHands::setAnimationSpeed(float s) { impl_->speed = std::clamp(s, 0.25f, 4.f); }

void PlayerHands::reset() {
    Impl& m = *impl_;
    if (m.fan) {  // (the cards went with the table)
        m.fan = false;
        m.arm[1].hold = m.restKey(true, m.look.tespih > 0 ? 2 : 0);
        if (m.ready && !m.arm[1].follow) {
            Key home = m.arm[1].hold;
            home.t = 0.6f;
            m.startTrack(m.arm[1], {Key{}, home}, 0, false);
        }
    }
    for (Impl::Arm& A : m.arm) {
        if (A.follow) {
            A.follow = false;
            A.attached = false;
            A.cue = HandCue{};
            if (m.ready) {
                Key home = A.hold;
                home.t = 0.6f;
                m.startTrack(A, {Key{}, home}, 0, false);
            }
        }
    }
}

void PlayerHands::cue(const HandCue& c) {
    Impl& m = *impl_;
    if (c.kind == HandCueKind::CardFan && !m.active()) {  // (remembered: the fan is there when they are shown)
        m.fan = c.on;
        if (c.on) m.fanFrame = c.frame;
        return;
    }
    if (!m.active()) return;
    m.startCue(c);
}

float PlayerHands::lead(HandCueKind k) const {
    const Impl& m = *impl_;
    if (!m.active() || m.lowered) return 0.f;
    switch (k) {
    case HandCueKind::Take: return 0.20f;
    case HandCueKind::Give: return 0.13f;
    case HandCueKind::Dice: return 0.45f;
    default: return 0.f;
    }
}

void PlayerHands::debugIdle(const char* what) { impl_->debugAsk = what ? what : ""; }

bool PlayerHands::shown() const {
    const Impl& m = *impl_;
    return m.ready && m.low < 0.985f;
}

void PlayerHands::update(float dt, const Camera3D& eye) {
    Impl& m = *impl_;
    if (!m.ready) return;
    dt = std::min(dt, 0.1f);
    m.time += dt;
    m.eye = eye;
    // the head: the camera's frame in seat-local space
    {
        const Vector3 fw = Vector3Normalize(Vector3Subtract(eye.target, eye.position));
        Vector3 rt = Vector3CrossProduct(fw, {0, 1, 0});
        rt = Vector3Length(rt) > 1e-4f ? Vector3Normalize(rt) : Vector3{1, 0, 0};
        const Vector3 up = Vector3CrossProduct(rt, fw);
        const Matrix headW = chr::basisMatrix(rt, up, Vector3Negate(fw), eye.position);
        m.headLocal = MatrixMultiply(headW, m.rootInv);
    }
    const bool want = m.active() && !m.lowered;
    if (!m.active()) {
        for (Impl::Arm& A : m.arm) m.dropGlass(A);
    }
    // a sip cut short by a screen: the glass goes back down first
    if (m.lowered)
        for (Impl::Arm& A : m.arm)
            if (A.glass) {
                m.dropGlass(A);
                Key home = A.hold;
                home.t = 0.5f;
                m.startTrack(A, {Key{}, home}, 0, false);
            }
    chr::spring(m.low, m.lowV, want ? 0.f : 1.f, want ? 5.f : 7.f, dt);
    m.low = clampf(m.low, 0.f, 1.f);
    if (m.active()) m.idle(dt);
    // the card fan: the left hand stays on it
    if (m.fan && !m.arm[1].follow) m.arm[1].hold = m.fanKey();
    for (Impl::Arm& A : m.arm) {
        const Key k = m.evalArm(A, dt);
        m.solveArm(A, k, dt);
        if (A.glass && m.people) {
            const Matrix g = MatrixMultiply(glassInHand(A.left, kHS), A.hand);
            if (!m.people->holdPlayerGlass(&g)) A.glass = false;
        }
    }
    if (m.look.tespih > 0) m.updateTespih(dt);
    m.updateSmoke(dt);
}

void PlayerHands::submit(Renderer& r) {
    Impl& m = *impl_;
    if (!shown() || !m.lookBuilt || !m.people) return;
    for (const Impl::Arm& A : m.arm) {
        r.submit(&m.upperMesh, &m.clothMat, A.upper, CastShadow | NoFog);
        r.submit(&m.foreMesh, &m.clothMat, A.fore, CastShadow | NoFog);
        if (m.hasForeSkin) r.submit(&m.foreSkin, &m.skinMat, A.fore, CastShadow | NoFog);
        if (const Mesh* h = m.people->handMesh(A.left, (int)A.pose)) r.submit(h, &m.skinMat, A.hand, CastShadow | NoFog);
    }
    const Impl::Arm& R = m.arm[0];
    const Impl::Arm& L = m.arm[1];
    if (m.look.yuzuk) {
        // on the ring finger's first joint (finger 2 of the hand mesh), around the finger's line
        const Vector3 knuckle{0.0088f, -0.0005f, -0.092f};
        const Vector3 tip = chr::handTip(R.pose, 2);
        const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, knuckle));
        const Vector3 at = Vector3Add(knuckle, Vector3Scale(dir, 0.013f));
        Vector3 x = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0}));
        const Vector3 z = Vector3CrossProduct(x, dir);
        r.submit(&m.ring, &m.goldMat, MatrixMultiply(chr::basisMatrix(x, dir, z, at), R.hand), NoFog);
    }
    if (m.look.saat) r.submit(&m.watch, &m.watchMat, MatrixMultiply(MatrixTranslate(0.f, 0.f, 0.006f), L.hand), CastShadow | NoFog);
    if (m.look.sigara) {
        const Matrix cigW = MatrixMultiply(MatrixTranslate(chr::CIG_HOLD.x, chr::CIG_HOLD.y - 0.020f, chr::CIG_HOLD.z), R.hand);
        r.submit(&m.cig, &m.cigMat, cigW, NoFog);
        r.submit(&m.ember, &m.emberMat, cigW, NoFog);
        const Vector3 tip = Vector3Transform({0.f, 0.0748f, 0.f}, cigW);
        const float flick = 0.75f + 0.25f * std::sin(m.time * 23.f) * std::sin(m.time * 7.3f);
        r.submitGlow(tip, 0.010f + 0.018f * m.drag, Color{255, 120, 40, 255}, (0.35f + 0.65f * m.drag) * flick);
    }
    if (m.look.tespih > 0 && m.chainInit) {
        for (int i = 0; i + 1 < kChainN; ++i)
            for (int k = 0; k < 3; ++k) {
                const Vector3 q = Vector3Lerp(m.chain[(size_t)i], m.chain[(size_t)i + 1], (float)k / 3.f + 1.f / 6.f);
                r.submit(&m.bead, &m.beadMat, MatrixTranslate(q.x, q.y, q.z), NoFog);
            }
        // the imame (the long end piece) and the tassel below it
        const Vector3 hint{0.f, 0.f, 1.f};
        const Matrix im = chr::boneMatrix(m.tail[1], m.tail[0], hint);
        r.submit(&m.imame, &m.beadMat, MatrixMultiply(MatrixTranslate(0.f, 0.032f, 0.f), im), NoFog);
        const Matrix ts = chr::boneMatrix(m.tail[1], Vector3Add(m.tail[1], Vector3Subtract(m.tail[1], m.tail[2])), hint);
        r.submit(&m.tassel, &m.tasselMat, ts, NoFog);
    }
}

} // namespace r3d
