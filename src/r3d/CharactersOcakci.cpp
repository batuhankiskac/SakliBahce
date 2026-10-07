// Characters module: the ocakçı (r3d/CharactersOcakci.h) — the older tea maker at the counter's left end. A small
// scripted loop: every action is a function of its own clock that gives targets for his body, gaze and both hands; the
// hands follow their targets smoothly (two-bone IK like everybody else), and the things he holds (the demlik, the big
// çaydanlık, a glass, the paper, the çaycı's tray) ride in his hands, blending in and out when they change hands.
// The çaycı's trips wait for him (Cast::ocakTrayGate): he takes the boy's tray, fills its glasses from the demlik, tops
// them up from the kettle and hands it back over the counter's corner — the trip starts from his hands.
#include "r3d/CharactersOcakci.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace r3d {
namespace chr {

void freePerson(PersonMeshes& pm, Renderer& r);  // CharactersMesh.cpp

namespace {

enum OcakAct { OA_Idle = 0, OA_Brew, OA_Fill, OA_Rinse, OA_Stoke, OA_Wipe, OA_Paper, OA_Chat, OA_Tv };
// The tray round's timeline (Ocakci::tl): take the tray, set it down, the demlik's pours, the kettle's, the pots back,
// the tray up and over to the çaycı.
enum FillT { F_TAKE = 0, F_SET, F_DGRAB, F_DLIFT, F_DPOUR_END, F_KGRAB, F_KLIFT, F_KPOUR_END, F_KBACK, F_DBACK, F_TGRAB,
             F_GIVE, F_END };

// ---------------------------------------------------------------- places (world, metres; World.h, RoomBuild.cpp)
constexpr float kTop = 1.02f;                         // the counter's marble top
constexpr float kFacingX = -PI_F * 0.5f;              // yaw facing +X (along the counter)
constexpr Vector3 kHome{1.86f, 0.f, -3.08f};          // at the counter's left end
constexpr Vector3 kHandStand{1.87f, 0.f, -3.00f};     // a little step toward the çaycı (his right hand under kMeet)
constexpr Vector3 kMeet{2.40f, 1.43f, -2.90f};        // where the tray's handle changes hands: over the counter's front
                                                      // edge, behind the okey player sitting with his back to it
constexpr Vector3 kBoyStand{2.90f, 0.f, -2.64f};      // the çaycı steps over to here
constexpr Vector3 kBoyHome{3.05f, 0.f, -2.60f};       // his counter stand (CharactersCrowd.cpp, node 0)
constexpr Vector3 kTraySpot{2.165f, kTop, -3.03f};    // the tray stands here while he fills it
constexpr Vector3 kKettleRest{2.47f, 1.115f, -3.15f}; // the big çaydanlık on the left burner
constexpr Vector3 kDemlikRest{2.47f, 1.291f, -3.15f}; // ... and the demlik on top of it
constexpr Vector3 kDemlikDown{2.16f, kTop, -3.06f};   // the demlik set on the counter to be topped up
constexpr Vector3 kKnob{2.355f, 1.13f, -3.045f};      // the burner's tap, under the kettle
constexpr Vector3 kFlame{2.47f, 1.135f, -3.15f};
constexpr Vector3 kCrate{1.50f, 0.f, -3.26f};         // his crate by the wall: a basin and the clean glasses
constexpr float kCrateH = 0.70f;
constexpr Vector3 kBasin{1.41f, kCrateH, -3.26f};
constexpr Vector3 kRinseGlass[2] = {{1.625f, kCrateH, -3.31f}, {1.625f, kCrateH, -3.19f}};
constexpr Vector3 kPaperRest{2.13f, kTop, -3.33f};    // the folded paper at the counter's back corner
// prop-local points (the pots' profiles are RoomBuild.cpp's)
constexpr Vector3 kKettleHandle{-0.16f, 0.11f, 0.f}, kKettleSpout{0.17f, 0.15f, 0.f};
constexpr Vector3 kDemlikHandle{-0.107f, 0.055f, 0.f}, kDemlikSpout{0.12f, 0.10f, 0.f};
constexpr float kDemlikTop = 0.134f;
constexpr float kTrayDrop = 0.300f;  // the tray's floor below its handle's pivot (CharactersProps.cpp buildTray)
constexpr float kTrayGlassR = 0.078f;
constexpr Vector3 kPaperSize{0.20f, 0.004f, 0.14f};

// ---------------------------------------------------------------- small helpers
float win(float t, float a, float b) { return smooth01((t - a) / std::max(b - a, 1e-4f)); }
bool crossed(float t0, float t1, float x) { return t0 < x && t1 >= x; }
float yawTo(Vector3 from, Vector3 to) { return std::atan2(-(to.x - from.x), -(to.z - from.z)); }
float lerpAngle(float a, float b, float t) { return a + wrapAngle(b - a) * t; }
float glassH() { return 0.0915f * glassScale(); }

Matrix blendM(const Matrix& A, const Matrix& B, float t) {
    Vector3 x = Vector3Lerp(mX(A), mX(B), t), y = Vector3Lerp(mY(A), mY(B), t);
    const float sx = lerpf(Vector3Length(mX(A)), Vector3Length(mX(B)), t);
    x = vnorm(x);
    y = vnorm(Vector3Subtract(y, Vector3Scale(x, Vector3DotProduct(y, x))));
    const Vector3 z = Vector3CrossProduct(x, y);
    return basisMatrix(Vector3Scale(x, sx), Vector3Scale(y, sx), Vector3Scale(z, sx), Vector3Lerp(mPos(A), mPos(B), t));
}

// A pot held by its handle H (pot-local): the handle at `h`, turned by `yaw`, tipped spout-down by `tilt` around it.
struct PotPose {
    Vector3 h{};
    float yaw = 0.f, tilt = 0.f;
};
Matrix potMatrix(Vector3 H, const PotPose& p) { return mul(mul(T(Vector3Negate(H)), RZ(-p.tilt)), mul(RY(p.yaw), T(p.h))); }
PotPose potRest(Vector3 H, Vector3 base) { return {Vector3Add(base, H), 0.f, 0.f}; }
// The pose that puts the spout S right at `tip`.
PotPose potOver(Vector3 H, Vector3 S, Vector3 tip, float yaw, float tilt) {
    const Vector3 d = xfDir(mul(RZ(-tilt), RY(yaw)), Vector3Subtract(S, H));
    return {Vector3Subtract(tip, d), yaw, tilt};
}
PotPose mixPose(const PotPose& a, const PotPose& b, float u, float arc = 0.f) {
    PotPose p;
    p.h = Vector3Add(Vector3Lerp(a.h, b.h, u), {0, arc * std::sin(PI_F * u), 0});
    p.yaw = lerpAngle(a.yaw, b.yaw, u);
    p.tilt = lerpf(a.tilt, b.tilt, u);
    return p;
}

// prop-local -> hand-local grips (the hand matrix carries the hand's scale, so they divide it out)
// a pot's handle: fingers toward the spout, thumb up, the palm turned inward
Matrix potInHand(Vector3 H, bool left, float hs) {
    const float k = 1.f / hs;
    const Vector3 X{0, 0, -1}, Y = left ? Vector3{1, 0, 0} : Vector3{-1, 0, 0}, Z = left ? Vector3{0, -1, 0} : Vector3{0, 1, 0};
    const Vector3 g = left ? Vector3{-GRIP_CENTER.x, GRIP_CENTER.y, GRIP_CENTER.z} : GRIP_CENTER;
    const Vector3 rh = Vector3Add(Vector3Add(Vector3Scale(X, H.x * k), Vector3Scale(Y, H.y * k)), Vector3Scale(Z, H.z * k));
    return basisMatrix(Vector3Scale(X, k), Vector3Scale(Y, k), Vector3Scale(Z, k), Vector3Subtract(g, rh));
}
// the tray's handle loop from above, palm down (yaw 0 when the fingers point along +X)
Matrix trayInHand(float hs) {
    const float k = 1.f / hs;
    const Matrix R = RY(PI_F * 0.5f);
    return basisMatrix(Vector3Scale(mX(R), k), Vector3Scale(mY(R), k), Vector3Scale(mZ(R), k),
                       Vector3Subtract(GRIP_CENTER, {0, 0.030f * k, 0}));
}
// a tea glass in the left hand (thumb up)
Matrix glassInLeft(float hs) {
    const float k = 1.f / hs;
    const Vector3 g{-GRIP_CENTER.x, GRIP_CENTER.y, GRIP_CENTER.z};
    return basisMatrix({0, k, 0}, {k, 0, 0}, {0, 0, -k}, Vector3Subtract(g, {0.055f * k, 0, 0}));
}
// the folded paper pinched at its near edge, palm down
Matrix paperInRight(float hs) {
    const float k = 1.f / hs;
    const Vector3 X{0, 0, -1}, Y{0, 1, 0}, Z{1, 0, 0};
    const Vector3 edge{-kPaperSize.x * 0.5f, kPaperSize.y * 0.5f, 0.f};
    const Vector3 re = Vector3Add(Vector3Add(Vector3Scale(X, edge.x * k), Vector3Scale(Y, edge.y * k)), Vector3Scale(Z, edge.z * k));
    return basisMatrix(Vector3Scale(X, k), Vector3Scale(Y, k), Vector3Scale(Z, k), Vector3Subtract(PINCH_POINT, re));
}
// the cloth (bez): over the left shoulder (torso space) or bunched under the right palm (hand space)
Matrix bezOnShoulder(const PersonLook& L) { return mul(RZ(0.12f), T({-(L.shoulderW - 0.075f), L.shoulderY + 0.012f, 0.012f})); }
Matrix bezInHand() { return mul(S3(0.85f, 0.32f, 0.8f), T(Vector3Add(PALM_CENTER, {0, -0.036f, 0}))); }

// ---------------------------------------------------------------- hand targets
struct HT {
    Vector3 w{}, f{0, 0, -1}, p{0, -1, 0};
    HandPose pose = HandPose::Rest;
};
HT fromHand(const Matrix& H, HandPose pose) {
    HT h;
    h.w = mPos(H);
    h.f = vnorm(Vector3Negate(mZ(H)));
    h.p = vnorm(Vector3Negate(mY(H)));
    h.pose = pose;
    return h;
}
HT holdingT(const Matrix& inHand, const Matrix& propW, HandPose pose) { return fromHand(mul(MatrixInvert(inHand), propW), pose); }
HT mixHT(const HT& a, const HT& b, float u) {
    HT h;
    h.w = Vector3Lerp(a.w, b.w, u);
    h.f = vnorm(Vector3Lerp(a.f, b.f, u));
    h.p = vnorm(Vector3Lerp(a.p, b.p, u));
    h.pose = u < 0.5f ? a.pose : b.pose;
    return h;
}
HT handAt(Vector3 point, Vector3 f, Vector3 p, Vector3 off, bool left, float hs, HandPose pose) {
    HT h;
    h.f = vnorm(f);
    h.p = vnorm(Vector3Subtract(p, Vector3Scale(h.f, Vector3DotProduct(p, h.f))));
    h.w = wristFor(point, h.f, h.p, off, hs, left);
    h.pose = pose;
    return h;
}
HT palmOn(Vector3 pt, Vector3 f, bool left, float hs, HandPose pose = HandPose::Rest) {
    return handAt(pt, f, {0, -1, 0}, PALM_CENTER, left, hs, pose);
}
// in the body's frame (character-local, the standing root)
HT localHT(const Matrix& root, Vector3 pt, Vector3 f, Vector3 p, Vector3 off, bool left, float hs, HandPose pose) {
    return handAt(xfPoint(root, pt), xfDir(root, f), xfDir(root, p), off, left, hs, pose);
}
// The hand comes to a grip: first a little back and above it, open, then onto it (u 0..1 over the approach).
HT approachHT(const HT& from, const HT& to, float u) {
    HT lifted = to;
    lifted.w = Vector3Add(Vector3Subtract(to.w, Vector3Scale(to.f, 0.05f)), {0, 0.045f, 0});
    lifted.pose = HandPose::Open;
    if (u < 0.6f) return mixHT(from, lifted, smooth01(u / 0.6f));
    return mixHT(lifted, to, smooth01((u - 0.6f) / 0.4f));
}

// ---------------------------------------------------------------- props
void grab(OcakProp& p, int hand) {
    p.holder = hand;
    p.from = p.world;
    p.blend = 0.f;
}
void putDown(OcakProp& p, const Matrix& rest) {
    p.holder = 0;
    p.from = p.world;
    p.rest = rest;
    p.blend = 0.f;
}
Matrix glassRestW() { return mul(RX(PI_F), T(Vector3Add(kRinseGlass[0], {0, glassH(), 0}))); }  // upside down
Vector3 trayGlassLocal(int k) {
    const float a = PI_F * 0.5f + 2.f * PI_F * (float)k / 3.f + PI_F / 3.f;  // as CharactersProps.cpp lays them out
    return {std::cos(a) * kTrayGlassR, -kTrayDrop + 0.0025f, std::sin(a) * kTrayGlassR};
}
Vector3 trayMouth(const Matrix& W, int k) {
    Vector3 p = trayGlassLocal(k);
    p.y += SAUCER_TOP + glassH();
    return xfPoint(W, p);
}
Matrix traySpotW() { return T(Vector3Add(kTraySpot, {0, kTrayDrop, 0})); }

// ---------------------------------------------------------------- meshes
Mesh buildPot(bool demlik) {
    MeshBuilder b;
    const Color chrome{214, 216, 220, 255}, dark{40, 36, 34, 255};
    if (!demlik) {
        b.lathe({{0.f, 0.f}, {0.09f, 0.f}, {0.105f, 0.015f}, {0.11f, 0.06f}, {0.105f, 0.11f}, {0.085f, 0.15f}, {0.055f, 0.17f},
                 {0.052f, 0.175f}, {0.0f, 0.176f}},
                28, false, false, chrome);
        b.tube({{0.09f, 0.05f, 0}, {0.14f, 0.1f, 0}, {0.17f, 0.15f, 0}}, 0.012f, 8, chrome);
        std::vector<Vector3> h;
        for (int i = 0; i <= 10; ++i) {
            const float t = PI_F * i / 10.f;
            h.push_back({-(0.09f + 0.07f * std::sin(t)), 0.03f + 0.12f * (1 - std::cos(t)) * 0.5f + 0.02f, 0.f});
        }
        b.tube(h, 0.009f, 6, dark);
    } else {
        b.lathe({{0.f, 0.f}, {0.05f, 0.f}, {0.066f, 0.02f}, {0.072f, 0.055f}, {0.064f, 0.09f}, {0.045f, 0.105f}, {0.043f, 0.112f},
                 {0.02f, 0.118f}, {0.012f, 0.13f}, {0.0f, kDemlikTop}},
                24, false, false, chrome);
        b.tube({{0.06f, 0.03f, 0}, {0.1f, 0.065f, 0}, {0.12f, 0.1f, 0}}, 0.008f, 6, chrome);
        std::vector<Vector3> h;
        for (int i = 0; i <= 8; ++i) {
            const float t = PI_F * i / 8.f;
            h.push_back({-(0.062f + 0.045f * std::sin(t)), 0.02f + 0.07f * (1 - std::cos(t)) * 0.5f, 0.f});
        }
        b.tube(h, 0.006f, 6, dark);
    }
    return b.build(true);
}

Mesh buildCrate() {
    MeshBuilder b;
    const Color wood{150, 108, 70, 255}, gap{84, 58, 38, 255};
    b.roundedBox({0, kCrateH * 0.5f, 0}, {0.40f, kCrateH, 0.28f}, 0.012f, 1, wood);
    for (int i = 1; i < 4; ++i) {  // the planks
        const float y = kCrateH * (float)i / 4.f;
        b.box({0, y, -0.1405f}, {0.39f, 0.008f, 0.003f}, gap);
        b.box({-0.2005f, y, 0}, {0.003f, 0.008f, 0.27f}, gap);
        b.box({0.2005f, y, 0}, {0.003f, 0.008f, 0.27f}, gap);
    }
    // a folded drying cloth along the front edge of the top
    b.roundedBox({0.10f, kCrateH + 0.006f, -0.07f}, {0.16f, 0.012f, 0.11f}, 0.004f, 1, Color{232, 228, 216, 255});
    return b.build(true);
}

Mesh buildBasin() {
    MeshBuilder b;
    b.lathe({{0.f, 0.f}, {0.085f, 0.f}, {0.10f, 0.015f}, {0.112f, 0.06f}, {0.119f, 0.078f}, {0.113f, 0.081f}, {0.104f, 0.064f},
             {0.092f, 0.02f}, {0.075f, 0.009f}, {0.f, 0.009f}},
            28, false, false, Color{196, 112, 66, 255});
    return b.build(true);
}

Mesh buildWater() {
    MeshBuilder b;
    b.cylinder({0, 0.048f, 0}, 0.099f, 0.003f, 24, Color{150, 160, 156, 255});
    return b.build(true);
}

// The cloth: a strip folded over (front leg, an arc, the back leg), white with two blue stripes.
Mesh buildBez() {
    MeshBuilder b;
    const float w = 0.06f, R = 0.062f;
    std::vector<Vector3> path, nrm;
    for (int i = 0; i <= 3; ++i) {
        path.push_back({0, -0.12f + 0.12f * (float)i / 3.f, -R});
        nrm.push_back({0, 0, -1});
    }
    for (int i = 1; i < 10; ++i) {
        const float a = PI_F * (float)i / 10.f;
        path.push_back({0, R * std::sin(a), -R * std::cos(a)});
        nrm.push_back({0, std::sin(a), -std::cos(a)});
    }
    for (int i = 0; i <= 3; ++i) {
        path.push_back({0, -0.14f * (float)i / 3.f, R});
        nrm.push_back({0, 0, 1});
    }
    const int n = (int)path.size();
    std::vector<int> l((size_t)n), r((size_t)n);
    for (int i = 0; i < n; ++i) {
        const float u = (float)i / (float)(n - 1);
        const bool stripe = std::fabs(u - 0.12f) < 0.035f || std::fabs(u - 0.88f) < 0.035f;
        const Color c = stripe ? Color{70, 96, 150, 255} : Color{236, 232, 222, 255};
        l[(size_t)i] = b.vertex(Vector3Add(path[(size_t)i], {-w, 0, 0}), nrm[(size_t)i], {0, u}, c);
        r[(size_t)i] = b.vertex(Vector3Add(path[(size_t)i], {w, 0, 0}), nrm[(size_t)i], {1, u}, c);
    }
    for (int i = 0; i + 1 < n; ++i) b.quad(l[(size_t)i], l[(size_t)i + 1], r[(size_t)i + 1], r[(size_t)i]);
    return b.build(true);
}

Mesh buildPaperFolded() {
    MeshBuilder b;
    b.box({0, kPaperSize.y * 0.5f, 0}, kPaperSize, WHITE);
    return b.build(true);
}

void ocakSound(Cast& c, ui::Sfx s, float vol, float pitch = 1.f) {
    if (c.time < 1.5f || !c.owner || !c.owner->playSfxVol) return;  // (not while the poses settle at init)
    c.owner->playSfxVol(s, vol, pitch);
}

// ---------------------------------------------------------------- choosing what to do
// On a derby night in the garden the old portable TV stands where his crate is (RoomSpecial.cpp, ozelgun): the crate
// has gone inside for the night, and so has his rinsing.
bool crateAway(const Cast& c) { return c.gardenTv(); }
// a TV to watch where he stands: inside, or the derby's portable one in the garden
bool tvThere(const Cast& c, const Ocakci& o) { return o.venue == 0 || c.gardenTv(); }

int chatPatron(const Cast& c) {
    for (int i = 0; i < (int)c.patrons.size(); ++i) {
        const Patron& p = c.patrons[(size_t)i];
        if (p.present && p.table == 1 && p.side == 3) return i;  // the okey player nearest the counter's end
    }
    return -2;
}

void startAct(Ocakci& o, int act, float dur) {
    if (std::getenv("SAKLI_OCAKCI_LOG")) std::fprintf(stderr, "[ocakci] act %d dur %.1f\n", act, dur);
    o.act = act;
    o.t = 0.f;
    o.dur = dur;
    o.wait = 0.f;
    o.bezAt = 0;  // every job starts with the cloth over his shoulder (Rinse and Wipe take it in their own time)
}

void startFill(Ocakci& o) {
    o.nFill = 0;
    for (int k = 0; k < 3; ++k)
        if (o.trayLvl[k] < 0.6f) o.fillIdx[o.nFill++] = k;
    float* t = o.tl;
    t[F_TAKE] = 1.3f;
    t[F_SET] = 2.5f;
    const float n = (float)o.nFill;
    if (o.nFill > 0) {
        t[F_DGRAB] = t[F_SET] + 0.6f;
        t[F_DLIFT] = t[F_DGRAB] + 0.55f;
        t[F_DPOUR_END] = t[F_DLIFT] + 0.6f * n;
        t[F_KGRAB] = t[F_DPOUR_END] + 0.35f;
        t[F_KLIFT] = t[F_KGRAB] + 0.55f;
        t[F_KPOUR_END] = t[F_KLIFT] + 0.45f * n;
        t[F_KBACK] = t[F_KPOUR_END] + 0.75f;
        t[F_DBACK] = t[F_KBACK] + 0.8f;
        t[F_TGRAB] = t[F_DBACK] + 0.35f;
    } else {
        for (int i = F_DGRAB; i <= F_DBACK; ++i) t[i] = t[F_SET];
        t[F_TGRAB] = t[F_SET] + 0.8f;
    }
    t[F_GIVE] = t[F_TGRAB] + 1.2f;
    t[F_END] = t[F_GIVE] + 0.9f;
    startAct(o, OA_Fill, t[F_END]);
}

void chooseNext(Cast& c, Ocakci& o) {
    if (o.order == 1) {
        startFill(o);
        return;
    }
    if (o.act != OA_Idle) {  // a breath between two jobs
        startAct(o, OA_Idle, c.rng.f(1.6f, 3.6f));
        return;
    }
    if (!o.forceAct.empty()) {  // SAKLI_OCAKCI: snapshots of one job
        const std::string f = o.forceAct;
        o.forceAct.clear();
        if (f == "demle") return startAct(o, OA_Brew, 7.2f);
        if (f == "yika" && !crateAway(c)) return startAct(o, OA_Rinse, 8.6f);
        if (f == "ocak") return startAct(o, OA_Stoke, 4.4f);
        if (f == "sil") return startAct(o, OA_Wipe, 6.4f);
        if (f == "gazete") return startAct(o, OA_Paper, 14.f);
        if (f == "tv" && tvThere(c, o)) return startAct(o, OA_Tv, 12.f);
        if (f == "sohbet") {
            o.partner = chatPatron(c);
            if (o.partner >= 0) return startAct(o, OA_Chat, 8.f);
        }
    }
    if (o.nextBrew <= 0.f) {
        o.nextBrew = c.rng.f(30.f, 55.f);
        return startAct(o, OA_Brew, 7.2f);
    }
    const bool boyHere = c.boy.state == 0 && o.order == 0;
    const int pat = chatPatron(c);
    float w[9] = {0.f};
    w[OA_Idle] = 2.2f;
    w[OA_Rinse] = crateAway(c) ? 0.f : 2.f;
    w[OA_Wipe] = 1.4f;
    w[OA_Stoke] = 0.9f;
    w[OA_Paper] = 1.1f;
    w[OA_Chat] = (boyHere || pat >= 0) ? 1.5f : 0.f;
    w[OA_Tv] = tvThere(c, o) ? 1.3f : 0.f;
    float sum = 0.f;
    for (float x : w) sum += x;
    float r = c.rng.f(0.f, sum);
    int pick = OA_Idle;
    for (int i = 0; i < 9; ++i) {
        if (r < w[i]) {
            pick = i;
            break;
        }
        r -= w[i];
    }
    switch (pick) {
    case OA_Rinse: startAct(o, OA_Rinse, 8.6f); break;
    case OA_Wipe: startAct(o, OA_Wipe, 6.4f); break;
    case OA_Stoke: startAct(o, OA_Stoke, 4.4f); break;
    case OA_Paper: startAct(o, OA_Paper, c.rng.f(11.f, 16.f)); break;
    case OA_Chat:
        o.partner = boyHere && (pat < 0 || c.rng.chance(0.6f)) ? -1 : pat;
        startAct(o, OA_Chat, c.rng.f(5.f, 9.f));
        break;
    case OA_Tv: startAct(o, OA_Tv, c.rng.f(8.f, 14.f)); break;
    default: startAct(o, OA_Idle, c.rng.f(2.f, 4.f)); break;
    }
}

// ---------------------------------------------------------------- the plan for this frame
struct Plan {
    Vector3 pos = kHome;
    float yaw = kFacingX, lean = 0.12f;
    Vector3 gaze{};
    float talk = 0.f;
    HT h[2];
};

void planFrame(Cast& c, Ocakci& o, float t0, float t, Plan& P) {
    const float hs = o.L.handScale;
    const Matrix& R = o.root;
    const HT restR = palmOn({2.075f, kTop + 0.016f, -2.945f}, {0.92f, -0.12f, -0.35f}, false, hs);
    const HT restL = palmOn({2.075f, kTop + 0.016f, -3.27f}, {0.92f, -0.12f, 0.35f}, true, hs);
    // the arms hanging loose: straight down from the shoulders (a little forward and out), palms in
    auto hang = [&](bool left) {
        const float sd = left ? -1.f : 1.f;
        const Vector3 sh = xfPoint(o.torsoW, {sd * o.L.shoulderW, o.L.shoulderY, 0.012f});
        const Vector3 w = Vector3Add(Vector3Add(sh, {0, -0.53f, 0}), xfDir(R, {sd * 0.035f, 0.f, -0.05f}));
        HT h;
        h.f = vnorm(xfDir(R, {sd * 0.05f, -1.f, -0.12f}));
        h.p = vnorm(xfDir(R, {-sd, 0.f, 0.f}));
        h.w = w;
        h.pose = HandPose::Rest;
        return h;
    };
    const HT hangR = hang(false), hangL = hang(true);
    P.h[0] = restR;
    P.h[1] = restL;
    P.gaze = o.idleGaze;
    const Matrix kettleIn = potInHand(kKettleHandle, false, hs), demlikIn = potInHand(kDemlikHandle, true, hs);
    const PotPose kettleRest = potRest(kKettleHandle, kKettleRest), demlikRest = potRest(kDemlikHandle, kDemlikRest);
    auto bezAtShoulder = [&]() {
        // the right hand comes up to the cloth on the left shoulder
        const Vector3 pt = xfPoint(o.torsoW, {-(o.L.shoulderW - 0.07f), o.L.shoulderY + 0.06f, -0.03f});
        return handAt(pt, xfDir(R, {-0.5f, -0.2f, -0.8f}), xfDir(R, {0, -1, 0}), PALM_CENTER, false, hs, HandPose::Grip);
    };

    switch (o.act) {
    // ------------------------------------------------ the çaycı's tray: take it, fill it, hand it over
    case OA_Fill: {
        const float* tl = o.tl;
        const bool atBoy = t < tl[F_TAKE] + 0.1f || (t >= tl[F_TGRAB] + 0.2f && t < tl[F_GIVE] + 0.4f);
        P.pos = atBoy ? kHandStand : kHome;
        P.lean = 0.18f;
        P.gaze = atBoy ? xfPoint(c.boy.headW, {0, 0.1f, 0}) : Vector3Add(kTraySpot, {0, 0.12f, 0});
        o.boyReach = t < tl[F_TAKE] ? 1 : (t >= tl[F_TGRAB] + 0.1f && t < tl[F_GIVE] ? 1 : (t < tl[F_GIVE] ? 2 : 0));
        const Matrix trayIn = trayInHand(hs);
        const Vector3 spotPivot = Vector3Add(kTraySpot, {0, kTrayDrop, 0});
        // --- events
        if (crossed(t0, t, tl[F_TAKE])) {
            o.trayOwner = 1;
            o.tray.world = c.boy.trayW;
            grab(o.tray, 1);
        }
        if (crossed(t0, t, tl[F_SET])) {
            putDown(o.tray, traySpotW());
            ocakSound(c, ui::Sfx::GlassSet, 0.30f, 0.95f);
        }
        if (o.nFill > 0) {
            if (crossed(t0, t, tl[F_DGRAB])) grab(o.demlik, 2);
            if (crossed(t0, t, tl[F_KGRAB])) grab(o.kettle, 1);
            if (crossed(t0, t, tl[F_KBACK])) putDown(o.kettle, T(kKettleRest));
            if (crossed(t0, t, tl[F_DBACK])) putDown(o.demlik, T(kDemlikRest));
        }
        if (crossed(t0, t, tl[F_TGRAB])) grab(o.tray, 1);
        if (crossed(t0, t, tl[F_GIVE])) {
            o.trayOwner = 0;
            o.tray.holder = 0;
            o.order = 2;  // the çaycı's trip may start (ocakTrayGate)
            o.boyReach = 0;
            ocakSound(c, ui::Sfx::TeaClink, 0.32f, 1.05f);
        }
        // --- right hand: the tray, then the kettle, then the tray again
        if (t < tl[F_TAKE]) {
            const HT g = holdingT(trayIn, T(mPos(c.boy.trayW)), HandPose::Grip);
            P.h[0] = approachHT(hangR, g, win(t, 0.35f, tl[F_TAKE]));
        } else if (t < tl[F_SET]) {
            const float u = win(t, tl[F_TAKE] + 0.05f, tl[F_SET]);
            const Vector3 piv = Vector3Add(Vector3Lerp(kMeet, spotPivot, u), {0, 0.05f * std::sin(PI_F * u), 0});
            P.h[0] = holdingT(trayIn, T(piv), HandPose::Grip);
        } else if (o.nFill > 0 && t >= tl[F_KGRAB] - 0.6f && t < tl[F_KBACK] + 0.05f) {
            const PotPose over0 = potOver(kKettleHandle, kKettleSpout, Vector3Add(trayMouth(o.tray.world, o.fillIdx[0]), {0, 0.05f, 0}),
                                          PI_F * 0.5f, 0.f);
            PotPose kp = kettleRest;
            if (t < tl[F_KGRAB]) {
                P.h[0] = approachHT(restR, holdingT(kettleIn, potMatrix(kKettleHandle, kettleRest), HandPose::Grip),
                                    win(t, tl[F_KGRAB] - 0.6f, tl[F_KGRAB]));
            } else {
                if (t < tl[F_KLIFT]) {
                    kp = mixPose(kettleRest, over0, win(t, tl[F_KGRAB], tl[F_KLIFT]), 0.06f);
                } else if (t < tl[F_KPOUR_END]) {
                    const int j = std::min((int)((t - tl[F_KLIFT]) / 0.45f), o.nFill - 1);
                    const float s = t - tl[F_KLIFT] - 0.45f * (float)j;
                    const int gi = o.fillIdx[j];
                    const Vector3 tip = Vector3Add(trayMouth(o.tray.world, gi), {0, 0.035f, 0});
                    const float tilt = 0.5f * std::min(1.f, 1.6f * std::sin(PI_F * clampf((s - 0.12f) / 0.33f, 0.f, 1.f)));
                    const PotPose here = potOver(kKettleHandle, kKettleSpout, tip, PI_F * 0.5f, tilt);
                    if (j > 0 && s < 0.12f) {
                        const PotPose prev = potOver(kKettleHandle, kKettleSpout,
                                                     Vector3Add(trayMouth(o.tray.world, o.fillIdx[j - 1]), {0, 0.035f, 0}), PI_F * 0.5f, 0.f);
                        kp = mixPose(prev, here, smooth01(s / 0.12f), 0.02f);
                    } else {
                        kp = here;
                    }
                    if (s > 0.15f) o.trayLvl[gi] = std::max(o.trayLvl[gi], 0.5f + 0.45f * clampf((s - 0.15f) / 0.28f, 0.f, 1.f));
                    P.gaze = tip;
                } else {
                    const PotPose last = potOver(kKettleHandle, kKettleSpout,
                                                 Vector3Add(trayMouth(o.tray.world, o.fillIdx[o.nFill - 1]), {0, 0.035f, 0}), PI_F * 0.5f, 0.f);
                    kp = mixPose(last, kettleRest, win(t, tl[F_KPOUR_END], tl[F_KBACK]), 0.07f);
                }
                P.h[0] = holdingT(kettleIn, potMatrix(kKettleHandle, kp), HandPose::Grip);
            }
        } else if (t >= tl[F_TGRAB] - 0.6f && t < tl[F_TGRAB]) {
            P.h[0] = approachHT(hangR, holdingT(trayIn, traySpotW(), HandPose::Grip), win(t, tl[F_TGRAB] - 0.6f, tl[F_TGRAB]));
        } else if (t >= tl[F_TGRAB] && t < tl[F_GIVE] + 0.05f) {
            const float u = win(t, tl[F_TGRAB] + 0.05f, tl[F_GIVE] - 0.15f);
            const Vector3 piv = Vector3Add(Vector3Lerp(spotPivot, kMeet, u), {0, 0.06f * std::sin(PI_F * u), 0});
            P.h[0] = holdingT(trayIn, T(piv), HandPose::Grip);
        } else {
            P.h[0] = hangR;
        }
        // --- left hand: the demlik
        if (o.nFill > 0 && t >= tl[F_DGRAB] - 0.6f && t < tl[F_DBACK] + 0.05f) {
            const PotPose over0 = potOver(kDemlikHandle, kDemlikSpout, Vector3Add(trayMouth(o.tray.world, o.fillIdx[0]), {0, 0.06f, 0}),
                                          -PI_F * 0.5f, 0.f);
            const PotPose aside{{2.03f, 1.31f, -3.34f}, -PI_F * 0.5f, 0.f};
            PotPose dp = demlikRest;
            if (t < tl[F_DGRAB]) {
                P.h[1] = approachHT(restL, holdingT(demlikIn, potMatrix(kDemlikHandle, demlikRest), HandPose::Grip),
                                    win(t, tl[F_DGRAB] - 0.6f, tl[F_DGRAB]));
            } else {
                if (t < tl[F_DLIFT]) {
                    dp = mixPose(demlikRest, over0, win(t, tl[F_DGRAB], tl[F_DLIFT]), 0.08f);
                } else if (t < tl[F_DPOUR_END]) {
                    const int j = std::min((int)((t - tl[F_DLIFT]) / 0.6f), o.nFill - 1);
                    const float s = t - tl[F_DLIFT] - 0.6f * (float)j;
                    const int gi = o.fillIdx[j];
                    const Vector3 tip = Vector3Add(trayMouth(o.tray.world, gi), {0, 0.04f, 0});
                    const float tilt = 0.62f * std::min(1.f, 1.5f * std::sin(PI_F * clampf((s - 0.18f) / 0.42f, 0.f, 1.f)));
                    const PotPose here = potOver(kDemlikHandle, kDemlikSpout, tip, -PI_F * 0.5f, tilt);
                    if (j > 0 && s < 0.18f) {
                        const PotPose prev = potOver(kDemlikHandle, kDemlikSpout,
                                                     Vector3Add(trayMouth(o.tray.world, o.fillIdx[j - 1]), {0, 0.04f, 0}), -PI_F * 0.5f, 0.f);
                        dp = mixPose(prev, here, smooth01(s / 0.18f), 0.02f);
                    } else {
                        dp = here;
                    }
                    if (s > 0.22f) o.trayLvl[gi] = std::max(o.trayLvl[gi], 0.5f * clampf((s - 0.22f) / 0.35f, 0.f, 1.f));
                    P.gaze = tip;
                } else if (t < tl[F_KBACK]) {
                    const PotPose last = potOver(kDemlikHandle, kDemlikSpout,
                                                 Vector3Add(trayMouth(o.tray.world, o.fillIdx[o.nFill - 1]), {0, 0.04f, 0}), -PI_F * 0.5f, 0.f);
                    dp = mixPose(last, aside, win(t, tl[F_DPOUR_END], tl[F_DPOUR_END] + 0.4f), 0.03f);
                } else {
                    dp = mixPose(aside, demlikRest, win(t, tl[F_KBACK], tl[F_DBACK]), 0.05f);
                }
                P.h[1] = holdingT(demlikIn, potMatrix(kDemlikHandle, dp), HandPose::Grip);
            }
        } else if (atBoy) {
            P.h[1] = hangL;
        }
        break;
    }
    // ------------------------------------------------ demlemek: the demlik on the counter, water from the kettle into it
    case OA_Brew: {
        P.lean = 0.17f;
        const PotPose down = potRest(kDemlikHandle, kDemlikDown);
        const Vector3 into = Vector3Add(kDemlikDown, {0, kDemlikTop + 0.035f, 0});
        P.gaze = t < 1.0f ? kDemlikRest : (t < 5.6f ? into : kDemlikRest);
        if (crossed(t0, t, 0.7f)) grab(o.demlik, 2);
        if (crossed(t0, t, 1.6f)) {
            putDown(o.demlik, T(kDemlikDown));
            ocakSound(c, ui::Sfx::GlassSet, 0.16f, 0.72f);
        }
        if (crossed(t0, t, 1.9f)) grab(o.kettle, 1);
        if (crossed(t0, t, 5.3f)) putDown(o.kettle, T(kKettleRest));
        if (crossed(t0, t, 5.6f)) grab(o.demlik, 2);
        if (crossed(t0, t, 6.6f)) putDown(o.demlik, T(kDemlikRest));
        // left: the demlik off the kettle onto the counter, later back
        if (t < 0.7f) {
            P.h[1] = approachHT(restL, holdingT(demlikIn, potMatrix(kDemlikHandle, demlikRest), HandPose::Grip), win(t, 0.05f, 0.7f));
        } else if (t < 1.6f) {
            P.h[1] = holdingT(demlikIn, potMatrix(kDemlikHandle, mixPose(demlikRest, down, win(t, 0.75f, 1.6f), 0.08f)), HandPose::Grip);
        } else if (t >= 5.0f && t < 5.6f) {
            P.h[1] = approachHT(restL, holdingT(demlikIn, potMatrix(kDemlikHandle, down), HandPose::Grip), win(t, 5.0f, 5.6f));
        } else if (t >= 5.6f && t < 6.65f) {
            P.h[1] = holdingT(demlikIn, potMatrix(kDemlikHandle, mixPose(down, demlikRest, win(t, 5.65f, 6.6f), 0.09f)), HandPose::Grip);
        }
        // right: the kettle over it
        if (t >= 1.2f && t < 1.9f) {
            P.h[0] = approachHT(restR, holdingT(kettleIn, potMatrix(kKettleHandle, kettleRest), HandPose::Grip), win(t, 1.2f, 1.9f));
        } else if (t >= 1.9f && t < 5.35f) {
            const PotPose over = potOver(kKettleHandle, kKettleSpout, into, PI_F * 0.5f, 0.f);
            PotPose kp;
            if (t < 2.6f) {
                kp = mixPose(kettleRest, over, win(t, 1.95f, 2.6f), 0.06f);
            } else if (t < 4.4f) {
                const float tilt = 0.52f * std::min(1.f, 1.4f * std::sin(PI_F * clampf((t - 2.6f) / 1.8f, 0.f, 1.f)));
                kp = potOver(kKettleHandle, kKettleSpout, into, PI_F * 0.5f, tilt);
            } else {
                kp = mixPose(over, kettleRest, win(t, 4.4f, 5.3f), 0.06f);
            }
            P.h[0] = holdingT(kettleIn, potMatrix(kKettleHandle, kp), HandPose::Grip);
        }
        break;
    }
    // ------------------------------------------------ a glass from the crate: rinse it in the basin, wipe it, look at it
    case OA_Rinse: {
        const float toCrate = yawTo(kHome, {1.48f, 0.f, -3.30f});
        P.yaw = t < 7.7f ? toCrate : kFacingX;
        P.lean = (t > 0.3f && t < 3.1f) || (t > 6.7f && t < 7.6f) ? 0.45f : 0.14f;
        const Matrix gIn = glassInLeft(hs);
        auto upright = [&](Vector3 base, float tilt) { return mul(RX(tilt), T(base)); };
        const Vector3 overBasin = Vector3Add(kBasin, {0.02f, 0.20f, 0.f});
        const Vector3 chest = xfPoint(R, {-0.05f, 1.10f, -0.30f});
        const Vector3 light = xfPoint(R, {-0.03f, 1.40f, -0.36f});
        if (crossed(t0, t, 0.9f)) grab(o.glass, 2);
        if (crossed(t0, t, 3.6f)) o.bezAt = 1;
        if (crossed(t0, t, 6.6f)) o.bezAt = 0;
        if (crossed(t0, t, 7.4f)) {
            putDown(o.glass, glassRestW());
            ocakSound(c, ui::Sfx::GlassSet, 0.22f, 1.12f);
        }
        Matrix gw = glassRestW();
        if (t < 0.9f) {
            P.h[1] = approachHT(hangL, holdingT(gIn, glassRestW(), HandPose::Grip), win(t, 0.1f, 0.9f));
        } else if (t < 7.45f) {
            if (t < 1.5f) {
                const float u = win(t, 0.9f, 1.5f);
                const Vector3 from = Vector3Add(kRinseGlass[0], {0, glassH(), 0});
                gw = mul(RX(PI_F * (1.f - u)), T(Vector3Add(Vector3Lerp(from, overBasin, u), {0, 0.08f * std::sin(PI_F * u), 0})));
            } else if (t < 3.0f) {
                const float u = (t - 1.5f) / 1.5f;
                const float dip = 0.5f - 0.5f * std::cos(4.f * PI_F * u);
                gw = upright(Vector3Add(overBasin, {0, -0.17f * dip, 0}), 0.35f * dip);
            } else if (t < 3.6f) {
                gw = upright(Vector3Lerp(overBasin, chest, win(t, 3.0f, 3.6f)), 0.f);
            } else if (t < 6.0f) {
                gw = mul(RY(2.6f * (t - 3.6f)), upright(chest, 0.f));
            } else if (t < 6.6f) {
                gw = upright(Vector3Lerp(chest, light, win(t, 6.0f, 6.4f)), -0.2f * win(t, 6.0f, 6.4f));
            } else {
                const float u = win(t, 6.6f, 7.4f);
                const Vector3 to = Vector3Add(kRinseGlass[0], {0, glassH(), 0});
                gw = mul(RX(PI_F * u), T(Vector3Add(Vector3Lerp(light, to, u), {0, 0.06f * std::sin(PI_F * u), 0})));
            }
            P.h[1] = holdingT(gIn, gw, HandPose::Grip);
            P.gaze = mPos(gw);
        } else {
            P.h[1] = hangL;
        }
        P.h[0] = hangR;
        if (t >= 3.0f && t < 3.6f) {
            P.h[0] = approachHT(hangR, bezAtShoulder(), win(t, 3.0f, 3.6f));
        } else if (t >= 3.6f && t < 6.0f) {
            const Vector3 mouth = xfPoint(o.glass.world, {0, glassH(), 0});
            const float a = 9.f * t;
            const Vector3 pt = Vector3Add(mouth, {0.012f * std::cos(a), 0.012f, 0.012f * std::sin(a)});
            const Vector3 toGlass = vnorm({mouth.x - o.pos.x, 0.f, mouth.z - o.pos.z});
            P.h[0] = palmOn(pt, Vector3Add(toGlass, {0, -0.3f, 0}), false, hs, HandPose::Rest);
        } else if (t >= 6.0f && t < 6.6f) {
            P.h[0] = bezAtShoulder();
        }
        break;
    }
    // ------------------------------------------------ the flame: he bends to the burner and turns its tap up
    case OA_Stoke: {
        P.lean = t > 0.3f && t < 3.5f ? 0.44f : 0.14f;
        P.gaze = kFlame;
        const HT tap = handAt(kKnob, {0.75f, -0.45f, -0.3f}, {-0.2f, -0.6f, 0.75f}, PINCH_POINT, false, hs, HandPose::Pinch);
        if (t < 0.9f) {
            P.h[0] = approachHT(restR, tap, win(t, 0.1f, 0.9f));
        } else if (t < 2.6f) {
            HT h = tap;
            const float tw = 0.5f * win(t, 1.0f, 1.6f);  // the tap turns
            h.p = vnorm(xfDir(MatrixRotate(h.f, tw), h.p));
            P.h[0] = h;
            o.flameUp = std::max(o.flameUp, win(t, 1.0f, 1.7f));
        } else if (t < 3.2f) {
            P.h[0] = approachHT(tap, restR, win(t, 2.6f, 3.2f));
        }
        break;
    }
    // ------------------------------------------------ wiping the counter with the cloth from his shoulder
    case OA_Wipe: {
        P.lean = t > 0.9f && t < 5.1f ? 0.30f : 0.12f;
        const Vector3 c0{2.14f, kTop + 0.010f, -3.07f};
        const float a = 2.f * PI_F * 0.75f * (t - 1.2f);
        const Vector3 pt = Vector3Add(c0, {0.07f * std::cos(a), 0.f, 0.11f * std::sin(a)});
        if (crossed(t0, t, 0.6f)) o.bezAt = 1;
        if (crossed(t0, t, 5.6f)) o.bezAt = 0;
        P.gaze = pt;
        if (t < 0.6f) {
            P.h[0] = approachHT(restR, bezAtShoulder(), win(t, 0.f, 0.6f));
        } else if (t < 1.2f) {
            P.h[0] = mixHT(bezAtShoulder(), palmOn(Vector3Add(c0, {0.07f, 0, 0}), {0.95f, -0.1f, -0.2f}, false, hs), win(t, 0.6f, 1.2f));
        } else if (t < 5.0f) {
            P.h[0] = palmOn(pt, {0.95f, -0.1f, -0.2f}, false, hs);
        } else if (t < 5.6f) {
            P.h[0] = mixHT(palmOn(pt, {0.95f, -0.1f, -0.2f}, false, hs), bezAtShoulder(), win(t, 5.0f, 5.6f));
        }
        break;
    }
    // ------------------------------------------------ the paper: picked up, read facing the room, folded and put back
    case OA_Paper: {
        const float D = o.dur;
        const float readYaw = kFacingX - 1.15f;
        P.yaw = (t > 0.9f && t < D - 1.5f) ? readYaw : kFacingX;
        P.lean = 0.06f;
        const Matrix pIn = paperInRight(hs);
        if (crossed(t0, t, 0.8f)) grab(o.paper, 1);
        if (crossed(t0, t, 1.7f)) o.paperOpen = true;
        if (crossed(t0, t, D - 1.7f)) o.paperOpen = false;
        if (crossed(t0, t, D - 0.5f)) putDown(o.paper, T(kPaperRest));
        const float turn = 0.04f * std::sin(t * 0.7f) + (t > D * 0.5f && t < D * 0.5f + 0.8f ? 0.06f * std::sin(PI_F * (t - D * 0.5f) / 0.8f) : 0.f);
        const HT readR = localHT(R, {0.20f + turn, 1.31f, -0.36f}, {-0.15f, 0.95f, -0.15f}, {-0.75f, 0.f, -0.65f}, PALM_CENTER, false, hs,
                                 HandPose::Hold);
        const HT readL = localHT(R, {-0.20f, 1.31f, -0.36f}, {0.15f, 0.95f, -0.15f}, {0.75f, 0.f, -0.65f}, PALM_CENTER, true, hs,
                                 HandPose::Hold);
        const HT pick = holdingT(pIn, T(kPaperRest), HandPose::Pinch);
        if (t < 0.8f) {
            P.h[0] = approachHT(restR, pick, win(t, 0.05f, 0.8f));
        } else if (t < 1.7f) {
            P.h[0] = mixHT(pick, readR, win(t, 0.85f, 1.7f));
        } else if (t < D - 1.7f) {
            P.h[0] = readR;
        } else if (t < D - 0.5f) {
            P.h[0] = mixHT(readR, pick, win(t, D - 1.6f, D - 0.55f));
        }
        if (t >= 1.3f && t < D - 1.7f) P.h[1] = readL;
        else if (t > 0.9f && t < D - 1.0f) P.h[1] = hangL;
        P.gaze = o.paperOpen ? Vector3Lerp(o.arm[0].wrist, o.arm[1].wrist, 0.5f) : kPaperRest;
        break;
    }
    // ------------------------------------------------ a few words with the çaycı or the okey player at the end
    case OA_Chat: {
        Vector3 head = Vector3Add(kBoyHome, {0, 1.55f, 0});
        if (o.partner >= 0 && o.partner < (int)c.patrons.size() && c.patrons[(size_t)o.partner].present)
            head = xfPoint(c.patrons[(size_t)o.partner].headW, {0, 0.10f, -0.03f});
        else if (o.partner < 0)
            head = xfPoint(c.boy.headW, {0, 0.10f, 0});
        P.yaw = lerpAngle(kFacingX, yawTo(o.pos, head), 0.55f);
        P.gaze = head;
        const bool talking = t > 0.4f && t < o.dur - 0.4f && noise1(t * 0.9f, 77u) > 0.38f;
        P.talk = talking ? 1.f : 0.f;
        if (talking || (t > 0.6f && t < o.dur - 0.6f)) {
            const float g = noise1(t * 1.3f, 78u);
            P.h[0] = localHT(R, {0.20f + 0.05f * std::sin(t * 2.1f), 1.00f + 0.08f * g, -0.30f}, {0.15f, 0.2f, -1.f}, {0.f, 1.f, 0.2f},
                             PALM_CENTER, false, hs, HandPose::Open);
        }
        break;
    }
    // ------------------------------------------------ the match on the TV (inside), arms crossed
    case OA_Tv: {
        const bool watching = t < o.dur - 1.2f;
        const Vector3 tv = c.tvPosition();
        P.yaw = watching ? yawTo(kHome, tv) : kFacingX;
        P.lean = 0.01f;
        P.gaze = watching ? tv : o.idleGaze;
        if (t > 0.6f && t < o.dur - 1.0f) {
            P.h[0] = localHT(R, {-0.10f, 1.13f, -0.20f}, {-0.85f, 0.1f, 0.5f}, {0.3f, 0.f, 0.95f}, PALM_CENTER, false, hs, HandPose::Rest);
            P.h[1] = localHT(R, {0.11f, 1.10f, -0.21f}, {0.85f, 0.1f, 0.5f}, {-0.3f, 0.f, 0.95f}, PALM_CENTER, true, hs, HandPose::Rest);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

// ============================================================================ init / shutdown
void Cast::queueOcakci(MeshJobs& J, Renderer& r) {
    if (ocak) return;
    ocak = new Ocakci();
    ocak->L = lookOcakci();
    queueOcakciPerson(J, ocak->pm, ocak->apron, r);
}

void Cast::initOcakci(Renderer& r) {
    if (ocak && ocak->built) return;
    if (!ocak) {  // (not queued with the others)
        MeshJobs J;
        queueOcakci(J, r);
        J.run();
    }
    Ocakci& o = *ocak;
    o.kettle.world = o.kettle.rest = T(kKettleRest);
    o.demlik.world = o.demlik.rest = T(kDemlikRest);
    o.glass.world = o.glass.rest = glassRestW();
    o.paper.world = o.paper.rest = T(kPaperRest);
    o.tray.world = o.tray.rest = traySpotW();
    o.kettle.blend = o.demlik.blend = o.glass.blend = o.paper.blend = o.tray.blend = 1.f;
    // his pots replace the counter's left çaydanlık (RoomBuild.cpp leaves it out)
    o.kettleMesh = buildPot(false);
    o.demlikMesh = buildPot(true);
    o.crate = buildCrate();
    o.basin = buildBasin();
    o.water = buildWater();
    o.bez = buildBez();
    o.paperFolded = buildPaperFolded();
    o.chrome = r.makeMat(WHITE, Texture2D{}, 0.85f, 90.f, 0.f, 0.15f);
    o.crateMat = r.makeMat(WHITE, M.woodTex, 0.10f, 14.f, 0.f, 0.08f);
    o.copper = r.makeMat(WHITE, Texture2D{}, 0.70f, 60.f, 0.f, 0.12f);
    o.waterMat = r.makeMat(WHITE, Texture2D{}, 0.90f, 140.f, 0.f, 0.30f);
    o.bezMat = r.makeMat(WHITE, Texture2D{}, 0.05f, 8.f, 0.f, 0.30f);
    o.pos = kHome;
    o.yaw = kFacingX;
    o.idleGaze = Vector3Add(kTraySpot, {0, 0.1f, 0});
    o.nextBrew = rng.f(10.f, 20.f);
    startAct(o, OA_Idle, 1.5f);
    if (const char* e = std::getenv("SAKLI_OCAKCI")) {  // developer: start with one job (snapshots)
        o.forceAct = e;
        o.dur = 0.2f;
        if (o.forceAct == "tepsi") {  // the çaycı's next trip right away: the tray round
            boy.nextBg = 1.3f;
            boy.nextOurs = 200.f;
        }
    }
    o.built = true;
}

void Cast::freeOcakci(Renderer& r) {
    if (!ocak) return;
    Ocakci& o = *ocak;
    if (o.built) {
        freePerson(o.pm, r);
        for (Mesh* m : {&o.apron, &o.kettleMesh, &o.demlikMesh, &o.crate, &o.basin, &o.water, &o.bez, &o.paperFolded})
            if (m->vertexCount > 0) UnloadMesh(*m);
        for (Mat* m : {&o.chrome, &o.crateMat, &o.copper, &o.waterMat, &o.bezMat}) r.unloadMat(*m);
    }
    delete ocak;
    ocak = nullptr;
}

// ============================================================================ update
void Cast::updateOcakci(float dt) {
    if (!ocak || !ocak->built) return;
    Ocakci& o = *ocak;
    const PersonLook& L = o.L;
    const float hs = L.handScale;
    o.nextBrew -= dt;
    if (o.order == 1) o.orderT += dt;
    // a tray round waits for nobody: what can be put down quickly is put down
    if (o.order == 1 && o.act != OA_Fill) {
        switch (o.act) {
        case OA_Idle:
        case OA_Chat:
        case OA_Tv:
        case OA_Stoke: startFill(o); break;
        case OA_Paper:
            if (o.t < 0.8f) startFill(o);
            else if (o.t < o.dur - 1.7f) o.t = o.dur - 1.7f - 1e-3f;
            break;
        case OA_Wipe:
            if (o.t < 0.6f) startFill(o);
            else if (o.t < 5.0f) o.t = 5.0f - 1e-3f;
            break;
        case OA_Rinse:  // the glass goes back on the crate first
            if (o.t < 0.9f) startFill(o);
            else if (o.t < 6.6f) o.t = 6.6f - 1e-3f;  // just before: crossed() is strict, the cloth goes back at 6.6
            break;
        case OA_Brew:
            if (o.t < 0.7f) startFill(o);
            break;  // else: the pots go back first (a few seconds)
        }
    }
    // the place changed under a job that needs it: no TV out here any more, the man he talked to gone
    if (o.act == OA_Tv && !tvThere(*this, o) && o.t < o.dur - 1.2f) o.t = o.dur - 1.2f;
    if (o.act == OA_Chat && o.partner >= 0 &&
        (o.partner >= (int)patrons.size() || !patrons[(size_t)o.partner].present) && o.t < o.dur - 0.6f)
        o.t = o.dur - 0.6f;
    if (o.t >= o.dur) chooseNext(*this, o);
    const float t0 = o.t;
    float t1 = o.t + dt;
    if (o.act == OA_Fill) {
        // the clock waits at the take and at the give until the çaycı's hand is at the meeting point
        const Vector3 boyGrip = xfPoint(boy.arm[1].hand, {-GRIP_CENTER.x, GRIP_CENTER.y, GRIP_CENTER.z});
        const bool there = boy.state == 0 && Vector3Distance(boyGrip, kMeet) < 0.12f;
        for (int g : {(int)F_TAKE, (int)F_GIVE}) {
            if (!(t0 < o.tl[g] && t1 >= o.tl[g])) continue;
            if (!there && o.wait < 3.f) {
                t1 = o.tl[g] - 1e-4f;
                o.wait += dt;
            } else {
                o.wait = 0.f;
            }
        }
    } else {
        o.boyReach = 0;
    }
    o.t = t1;
    o.flameUp = std::max(0.f, o.flameUp - dt * 0.4f);
    // where his eyes wander between jobs
    o.idleGazeIn -= dt;
    if (o.idleGazeIn <= 0.f) {
        o.idleGazeIn = rng.f(1.5f, 4.f);
        const float r = rng.f();
        if (r < 0.35f) o.idleGaze = Vector3Add(kTraySpot, {rng.f(0.f, 0.45f), 0.1f, rng.f(-0.15f, 0.1f)});
        else if (r < 0.6f) o.idleGaze = {rng.f(-0.6f, 0.6f), rng.f(0.9f, 1.3f), rng.f(-0.4f, 0.6f)};  // our table
        else if (r < 0.78f && boy.state == 0) o.idleGaze = xfPoint(boy.headW, {0, 0.1f, 0});
        else if (r < 0.9f) o.idleGaze = {2.4f, 0.85f, -1.9f};                                       // the okey table by him
        else o.idleGaze = {-4.0f, 1.6f, rng.f(-0.5f, 2.5f)};                                        // the door, the windows
    }

    Plan P;
    planFrame(*this, o, t0, t1, P);

    // ---- feet: half a step to the çaycı and back, or turning on the spot
    {
        Vector3 d = Vector3Subtract(P.pos, o.pos);
        d.y = 0.f;
        const float dist = Vector3Length(d);
        const float step = std::min(dist, 0.5f * dt);
        if (dist > 1e-4f) o.pos = Vector3Add(o.pos, Vector3Scale(d, step / dist));
        o.speed = approachExp(o.speed, dt > 0.f ? step / dt : 0.f, 8.f, dt);
        const float diff = wrapAngle(P.yaw - o.yaw);
        const float dy = clampf(diff * 5.f * dt, -2.4f * dt, 2.4f * dt);
        o.yaw = wrapAngle(o.yaw + dy);
        o.yawRate = approachExp(o.yawRate, dt > 0.f ? std::fabs(dy) / dt : 0.f, 8.f, dt);
    }
    const float walk = clampf(std::max(o.speed / 0.4f, o.yawRate * 0.45f), 0.f, 1.f);
    o.phase += (o.speed / 0.9f + o.yawRate * 0.18f) * dt;
    o.phase -= std::floor(o.phase);
    o.root = trsYaw(o.pos, o.yaw * RAD2DEG, 1.f);
    const Matrix& root = o.root;
    const float ph = o.phase * 2.f * PI_F;
    const float breath = 0.012f * std::sin(time * 1.3f);
    const Vector3 hip = Vector3Add(L.hipPivot, {0, -0.008f * walk * (0.5f + 0.5f * std::cos(2.f * ph)), 0});

    // ---- torso, apron, head
    spring(o.lean, o.leanV, P.lean, 4.5f, dt);
    o.torsoW = mul(mul(RY(0.04f * std::cos(ph) * walk), RX(-o.lean - breath)), mul(T(hip), root));
    o.apronW = mul(mul(T(Vector3Negate(hip)), RX(-o.lean * 0.5f)), mul(T(hip), root));
    o.gaze = approachExp(o.gaze, P.gaze, 5.f, dt);
    headLookSpring(o.torsoW, L, o.gaze, 0.75f, -0.75f, 5.f, dt, o.hYaw, o.hYawV, o.hPitch, o.hPitchV);
    const float nod = P.talk > 0.f ? 0.03f * std::sin(time * 7.f) : 0.f;
    o.headW = mul(mul(RX(o.hPitch + nod), RY(o.hYaw)), mul(T({0, L.spineLen, L.headZ}), o.torsoW));

    // ---- legs (the çaycı's leg meshes: dark trousers, black shoes)
    for (int i = 0; i < 2; ++i) {
        const float sd = i == 0 ? 1.f : -1.f;
        const float lp = ph + (i == 0 ? 0.f : PI_F);
        const float u = std::fmod(o.phase + (i == 0 ? 0.f : 0.5f), 1.f);
        const float thigh = 0.22f * std::cos(lp) * walk + 0.02f;
        auto bump = [](float x, float c0, float w) { return std::exp(-((x - c0) / w) * ((x - c0) / w)); };
        const float knee = 0.06f + (0.1f * bump(u, 0.12f, 0.08f) + 0.7f * bump(u, 0.72f, 0.13f) + 0.7f * bump(u, -0.28f, 0.13f)) * walk;
        const Vector3 hipJ = xfPoint(root, Vector3Add(hip, {sd * 0.092f, -0.03f, 0.01f}));
        const Vector3 kneeP = Vector3Add(hipJ, Vector3Scale(vnorm(xfDir(root, {0.012f * sd, -std::cos(thigh), -std::sin(thigh)})), 0.44f));
        const float sh = thigh - knee;
        const Vector3 ankle = Vector3Add(kneeP, Vector3Scale(vnorm(xfDir(root, {0.f, -std::cos(sh), -std::sin(sh)})), 0.43f));
        const Vector3 back = xfDir(root, {0, 0, 1});
        o.thighW[i] = boneMatrix(hipJ, kneeP, back);
        o.shinW[i] = boneMatrix(kneeP, ankle, back);
    }

    // ---- arms: the hands follow their targets, the elbows hang out and down
    for (int a = 0; a < 2; ++a) {
        Ocakci::Hand& A = o.arm[a];
        const HT& h = P.h[a];
        const float sd = a == 0 ? 1.f : -1.f;
        if (!A.init) {
            A.wrist = h.w;
            A.fingers = h.f;
            A.palm = h.p;
            A.init = true;
        }
        const float rate = 10.f;
        A.wrist = approachExp(A.wrist, h.w, rate, dt);
        A.fingers = vnorm(approachExp(A.fingers, h.f, rate, dt));
        A.palm = vnorm(approachExp(A.palm, h.p, rate, dt));
        A.pose = h.pose;
        A.shoulder = xfPoint(o.torsoW, {sd * L.shoulderW, L.shoulderY, 0.012f});
        Vector3 w = A.wrist;
        A.elbow = solveTwoBone(A.shoulder, w, L.upperArm, L.foreArm, xfDir(root, vnorm({sd * 0.55f, -0.6f, 0.45f})));
        const Vector3 back = xfDir(root, {0, 0, 1});
        A.upper = boneMatrix(A.shoulder, A.elbow, back);
        A.fore = boneMatrix(A.elbow, w, back);
        A.hand = handMatrix(w, A.fingers, A.palm, hs);
    }

    // ---- what he holds rides in his hands
    auto follow = [&](OcakProp& p, const Matrix& inHand) {
        const Matrix target = p.holder == 1 ? mul(inHand, o.arm[0].hand) : (p.holder == 2 ? mul(inHand, o.arm[1].hand) : p.rest);
        if (p.blend < 1.f) {
            p.blend = std::min(1.f, p.blend + dt / 0.2f);
            p.world = blendM(p.from, target, smooth01(p.blend));
        } else {
            p.world = target;
        }
    };
    follow(o.kettle, potInHand(kKettleHandle, false, hs));
    follow(o.demlik, potInHand(kDemlikHandle, true, hs));
    follow(o.glass, glassInLeft(hs));
    follow(o.paper, paperInRight(hs));
    if (o.trayOwner == 1) follow(o.tray, trayInHand(hs));

    // ---- face
    const float lidClose = blinkStep(rng, o.blinkIn, o.blinkT, dt, 5.5f, 0.16f);
    const FaceGeo& fg = o.pm.face;
    // (heavier lids than the çaycı's)
    standingEyes(fg, o.headW, o.gaze, -0.30f, lidClose, 0.04f, dt, o.eYaw, o.ePitch, o.eyeW, o.lidW, o.browW);
    const float jawGoal = P.talk > 0.f ? clampf(0.22f + 0.22f * std::sin(time * 15.f) + 0.18f * std::sin(time * 6.3f), 0.f, 1.f) : 0.f;
    o.jaw = approachExp(o.jaw, jawGoal, 18.f, dt);
    standingMouth(fg, o.headW, o.jaw, o.mouthW, o.lipW);

    // ---- steam: the open kettle, the pouring spouts, the fresh glasses on the counter
    if (renderer) {
        auto puff = [&](Vector3 at, float alpha, float size, float up) {
            SmokeParams sp;
            sp.velocity = {rng.f(-0.01f, 0.01f), up, rng.f(-0.01f, 0.01f)};
            sp.size0 = size * 0.2f;
            sp.size1 = size;
            sp.life = 2.2f;
            sp.alpha = alpha;
            sp.color = Color{236, 234, 230, 255};
            sp.turbulence = 0.6f;
            sp.buoyancy = 0.025f;
            renderer->emitSmoke(at, sp);
        };
        const bool kettleOpen = o.demlik.holder != 0 || Vector3Distance(mPos(o.demlik.rest), kDemlikRest) > 0.01f;
        o.steamAcc += dt * ((kettleOpen ? 7.f : 0.f) + (o.kettle.holder ? 5.f : 0.f));
        while (o.steamAcc >= 1.f) {
            o.steamAcc -= 1.f;
            if (o.kettle.holder && rng.chance(0.5f)) puff(xfPoint(o.kettle.world, kKettleSpout), 0.09f, 0.08f, 0.05f);
            else if (kettleOpen) puff(xfPoint(o.kettle.world, {rng.f(-0.02f, 0.02f), 0.18f, rng.f(-0.02f, 0.02f)}), 0.10f, 0.12f, 0.10f);
        }
        if (o.trayOwner == 1) {
            o.glassSteamAcc += dt * 3.f;
            while (o.glassSteamAcc >= 1.f) {
                o.glassSteamAcc -= 1.f;
                const int k = rng.i(3);
                if (o.trayLvl[k] > 0.3f) puff(trayMouth(o.tray.world, k), 0.06f, 0.055f, 0.03f);
            }
        }
    }
}

// ============================================================================ hooks for the çaycı (CharactersCrowd.cpp)
bool Cast::ocakTrayGate() {
    if (!ocak || !ocak->built) return true;
    Ocakci& o = *ocak;
    if (o.order == 2) {
        if (std::getenv("SAKLI_OCAKCI_LOG")) std::fprintf(stderr, "[ocakci] tray handed over at %.1f s\n", time);
        o.order = 0;
        o.orderT = 0.f;
        return true;
    }
    if (o.order == 0) {
        o.order = 1;
        o.orderT = 0.f;
    }
    if (o.orderT > 30.f) {  // a safety valve: whatever held him up, the çaycı goes with what he has
        if (std::getenv("SAKLI_OCAKCI_LOG")) std::fprintf(stderr, "[ocakci] safety valve at %.1f s\n", time);
        o.order = 0;
        o.orderT = 0.f;
        o.trayOwner = 0;
        o.tray.holder = 0;
        o.boyReach = 0;
        putDown(o.kettle, T(kKettleRest));
        putDown(o.demlik, T(kDemlikRest));
        startAct(o, OA_Idle, 2.f);
        return true;
    }
    return false;
}

void Cast::ocakBoyAtCounter(float dt) {
    if (!ocak || boy.state != 0) return;
    const Vector3 goal = ocak->order == 1 ? kBoyStand : kBoyHome;
    Vector3 d = Vector3Subtract(goal, boy.pos);
    d.y = 0.f;
    const float dist = Vector3Length(d);
    if (dist > 1e-3f) boy.pos = Vector3Add(boy.pos, Vector3Scale(d, std::min(1.f, 0.55f * dt / dist)));
}

void Cast::ocakBoyTrayHand(Key& lk, const Matrix& rootInv, float hs) {
    if (!ocak) return;
    const Ocakci& o = *ocak;
    if (o.trayOwner != 0) boy.trayInit = false;  // once it is back, his tray swings from his own hand again
    if (o.boyReach == 1) {
        lk.pos = wristFor(xfPoint(rootInv, Vector3Add(kMeet, {0, 0.01f, 0})), lk.fingers, lk.palm, GRIP_CENTER, hs, true);
        lk.pose = o.trayOwner != 0 ? HandPose::Open : HandPose::Grip;
    } else if (o.boyReach == 2 || o.trayOwner != 0) {
        lk.fingers = vnorm({0, -1, -0.1f});
        lk.palm = {1, 0, 0};
        lk.pos = wristFor({-0.215f, 0.86f, -0.04f}, lk.fingers, lk.palm, PALM_CENTER, hs, true);
        lk.pose = HandPose::Rest;
    }
}

void Cast::ocakTrayServed() {
    if (!ocak) return;
    for (float& l : ocak->trayLvl)
        if (l > 0.3f) {  // the glass he took from the tray
            l = 0.f;
            return;
        }
}

// ============================================================================ submission
void Cast::submitTray(Renderer& r, const Matrix& boyTrayW) {
    const Ocakci* o = ocak;
    const Matrix W = o && o->trayOwner != 0 ? o->tray.world : boyTrayW;
    r.submit(&M.trayHanger, &M.trayMat, W, CastShadow);
    for (int k = 0; k < 3; ++k) {
        const Vector3 p = trayGlassLocal(k);
        r.submit(&M.saucer, &M.porcelain, mul(T(p), W), 0);
        const Matrix g = mul(T({p.x, p.y + SAUCER_TOP, p.z}), W);
        const float lv = o ? o->trayLvl[k] : 0.93f;
        if (lv > 0.02f) {
            const int li = std::clamp((int)std::lround(lv * Meshes::TEA_LEVELS) - 1, 0, Meshes::TEA_LEVELS - 1);
            r.submit(&M.tea[li], &M.teaMat, g, 0);
            ++submitCount;
        }
        r.submit(&M.glass, &M.glassMat, g, Transparent);
    }
    submitCount += 7;
}

void Cast::submitOcakci(Renderer& r) {
    if (!ocak || !ocak->built) return;
    const Ocakci& o = *ocak;
    const PersonMeshes& pm = o.pm;
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.walkThigh[i], &pm.cloth, o.thighW[i], CastShadow);
        r.submit(&M.walkShin[i], &pm.cloth, o.shinW[i], CastShadow);
    }
    r.submit(&o.apron, &pm.cloth, o.apronW, CastShadow | DoubleSided);
    r.submit(&pm.torso, &pm.cloth, o.torsoW, CastShadow);
    r.submit(&pm.head, &pm.headMat, o.headW, 0);
    for (int a = 0; a < 2; ++a) {
        const Ocakci::Hand& A = o.arm[a];
        r.submit(&pm.upper[a], &pm.cloth, A.upper, CastShadow);
        r.submit(&pm.fore[a], &pm.cloth, A.fore, CastShadow);
        r.submit(&M.hand[a][(int)A.pose], &pm.skin, A.hand, CastShadow);
    }
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.eye[1], &M.eyeMat, o.eyeW[i], 0);
        r.submit(&M.lidUpper, &pm.skin, o.lidW[i], 0);
        r.submit(&pm.brow[i], &pm.hairMat, o.browW[i], 0);
    }
    if (o.jaw > 0.03f) {
        r.submit(&M.mouthCavity, &M.mouthMat, o.mouthW, 0);
        ++submitCount;
    }
    r.submit(&M.lowerLip[2], &M.lipMat, o.lipW, 0);
    r.submit(&pm.stache, &pm.hairMat, o.headW, 0);
    // the stove's left çaydanlık and its demlik, his crate with the basin and the clean glasses
    r.submit(&o.kettleMesh, &o.chrome, o.kettle.world, CastShadow);
    r.submit(&o.demlikMesh, &o.chrome, o.demlik.world, CastShadow);
    if (!crateAway(*this)) {
        r.submit(&o.crate, &o.crateMat, T(kCrate), CastShadow);
        r.submit(&o.basin, &o.copper, T(kBasin), CastShadow);
        r.submit(&o.water, &o.waterMat, T(kBasin), 0);
        r.submit(&M.glass, &M.glassMat, mul(RX(PI_F), T(Vector3Add(kRinseGlass[1], {0, glassH(), 0}))), Transparent);
        r.submit(&M.glass, &M.glassMat, o.glass.world, Transparent);
    }
    // the cloth over his shoulder or in his hand
    r.submit(&o.bez, &o.bezMat, o.bezAt == 0 ? mul(bezOnShoulder(o.L), o.torsoW) : mul(bezInHand(), o.arm[0].hand), DoubleSided);
    // the paper: folded, or open between his hands
    if (o.paperOpen) {
        const Vector3 a = mPos(o.arm[0].hand), b = mPos(o.arm[1].hand);
        const Vector3 mid = Vector3Add(Vector3Lerp(a, b, 0.5f), {0, 0.06f, 0});
        // held up in front of him, tipped back a little toward his eyes
        Vector3 toHead = Vector3Subtract(xfPoint(o.headW, {0, 0.1f, -0.05f}), mid);
        toHead.y = 0.f;
        toHead = vnorm(Vector3Add(vnorm(toHead), {0, 0.45f, 0}));
        const Vector3 xr = vnorm(Vector3Subtract(a, b));
        Vector3 up = vnorm(Vector3CrossProduct(toHead, xr));
        if (up.y < 0) up = Vector3Negate(up);
        const Vector3 xx = Vector3CrossProduct(up, Vector3Negate(toHead));
        r.submit(&M.newspaper, &M.paperMat, basisMatrix(xx, up, Vector3Negate(toHead), Vector3Add(mid, Vector3Scale(toHead, -0.02f))),
                 DoubleSided | CastShadow);
    } else {
        r.submit(&o.paperFolded, &M.paperMat, o.paper.world, CastShadow);
    }
    if (o.flameUp > 0.02f) r.submitGlow(kFlame, 0.05f + 0.05f * o.flameUp, Color{120, 160, 255, 255}, 0.7f * o.flameUp);
    submitCount += 33;
}

} // namespace chr

// The place, for the ocakçı (the TV is inside only). Room::venue(): 0 inside, 1 the garden.
void Characters::setVenue(int venue) {
    if (impl_ && impl_->ocak) impl_->ocak->venue = venue;
}

} // namespace r3d
