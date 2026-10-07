// Characters module: the three regulars at our table — procedural body (breathing, leaning, look-at with
// eye saccades and blinks), two-bone IK arms driven by eased keyframe tracks (reaching for tiles, slapping
// melds, sipping tea, smoking, the tespih), expressions, and reactions to game events.
#include "r3d/CharactersState.h"
#include "ui/Common.h"

#include <algorithm>

namespace r3d {
namespace chr {

namespace {

constexpr float TY = w3d::TABLE_Y;
constexpr float GRIP_H = 0.055f;          // grip height above the glass base
constexpr Vector3 TESPIH_GRIP{0.004f, -0.024f, -0.084f};
constexpr float kBeadR = 0.0064f;  // a tespih bead's radius (buildBead4)

float rackZ() { return -(w3d::SEAT_DIST - w3d::RACK_DIST); }

// The bots' istaka, as Table3D builds it (its geometry header is private to the table module, so the few
// numbers are mirrored here): the plank leans 16° toward the table from its base 8 mm behind RACK_DIST and
// 19 mm above the felt, is 9 mm thick, and the upper row's tiles (11 mm thick, 0.4 mm off the plank) reach
// 2 tiles + 7.6 mm up it. Rack coordinates: s along the plank from its base, o out of it toward the owner.
constexpr float kRackLean = 16.f * DEG2RAD;
constexpr float kRackOutB = 0.008f, kRackBaseY = TY + 0.019f, kRackPlankT = 0.009f;
constexpr float kRackTopS = 2.f * w3d::TILE_H + 0.0076f;   // top of the upper row
constexpr float kRackFrontO = w3d::TILE_T + 0.0004f;       // the tiles' faces

// character-local point at rack coordinates (x along the istaka)
Vector3 rackPoint(float x, float s, float o) {
    const float sn = std::sin(kRackLean), cs = std::cos(kRackLean);
    return {x, kRackBaseY + s * cs + o * sn, rackZ() + kRackOutB - s * sn + o * cs};
}
// Is a character-local point (with a margin) inside the plank + tiles slab of the owner's istaka?
bool insideRack(Vector3 l, float margin) {
    const float sn = std::sin(kRackLean), cs = std::cos(kRackLean);
    const float out = l.z - rackZ() - kRackOutB, y = l.y - kRackBaseY;
    const float s = -out * sn + y * cs, o = out * cs + y * sn;
    return std::fabs(l.x) < w3d::RACK_LEN * 0.5f + margin && s > -0.01f && s < kRackTopS + margin &&
           o > -kRackPlankT - margin && o < kRackFrontO + margin;
}

} // namespace

// Shoulder joint (character-local, upright seat frame) of side sd for a forward lean.
Vector3 shoulderAt(const PersonLook& L, float sd, float lean) {
    return {sd * L.shoulderW, L.hipPivot.y + L.shoulderY * std::cos(lean), L.hipPivot.z - L.shoulderY * std::sin(lean)};
}
// Extra lean (0..kMaxExtraLean) the body needs so that shoulder sd reaches the local wrist target w.
float leanNeeded(const PersonLook& L, float leanBase, float sd, Vector3 w) {
    const float reach = (L.upperArm + L.foreArm) * kReachFrac + kProtract;
    if (w.z > shoulderAt(L, sd, leanBase).z + 0.05f) return 0.f;  // not in front of the body
    auto out = [&](float extra) { return Vector3Distance(w, shoulderAt(L, sd, leanBase + extra)) > reach; };
    if (!out(0.f)) return 0.f;
    if (out(kMaxExtraLean)) return kMaxExtraLean;
    float lo = 0.f, hi = kMaxExtraLean;
    for (int i = 0; i < 8; ++i) {
        const float m = 0.5f * (lo + hi);
        (out(m) ? lo : hi) = m;
    }
    return hi;
}

// Fingertips of every pose (hand space, right hand, scale 1): [pose][0..3 index..little, 4 thumb].
const std::array<std::array<Vector3, 5>, HAND_POSES>& handTips() {
    static const std::array<std::array<Vector3, 5>, HAND_POSES> tips = [] {
        std::array<std::array<Vector3, 5>, HAND_POSES> t{};
        for (int p = 0; p < HAND_POSES; ++p)
            for (int f = 0; f < 5; ++f) t[(size_t)p][(size_t)f] = handTip((HandPose)p, f);
        return t;
    }();
    return tips;
}

void handBasis(Vector3 fingers, Vector3 palm, Vector3& X, Vector3& Y, Vector3& Z) {
    Z = vnorm(Vector3Negate(fingers));
    Y = Vector3Negate(palm);
    Y = Vector3Subtract(Y, Vector3Scale(Z, Vector3DotProduct(Y, Z)));
    if (Vector3Length(Y) < 1e-4f) Y = std::fabs(Z.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Y = vnorm(Y);
    X = Vector3CrossProduct(Y, Z);
}

// Wrist position that puts hand-space point `off` at `point`.
Vector3 wristFor(Vector3 point, Vector3 fingers, Vector3 palm, Vector3 off, float scale, bool left) {
    Vector3 X, Y, Z;
    handBasis(fingers, palm, X, Y, Z);
    if (left) off.x = -off.x;
    Vector3 w = Vector3Add(Vector3Add(Vector3Scale(X, off.x), Vector3Scale(Y, off.y)), Vector3Scale(Z, off.z));
    return Vector3Subtract(point, Vector3Scale(w, scale));
}

// Hand orientation gripping a cylinder with axis `a` (glass up), fingers wrapping along `f`.
void gripDirs(Vector3 a, Vector3 f, bool left, Vector3& fingers, Vector3& palm) {
    a = vnorm(a);
    f = vnorm(Vector3Subtract(f, Vector3Scale(a, Vector3DotProduct(f, a))));
    fingers = f;
    palm = left ? Vector3CrossProduct(f, a) : Vector3CrossProduct(a, f);
}

Key mk(float t, Vector3 pos, Vector3 fingers, Vector3 palm, HandPose pose, float lift, int ease, int ev) {
    Key k;
    k.t = t;
    k.pos = pos;
    k.fingers = vnorm(fingers);
    k.palm = vnorm(palm);
    k.pose = pose;
    k.lift = lift;
    k.ease = ease;
    k.event = ev;
    return k;
}

Key touching(Key k) {
    k.touch = true;
    return k;
}

Vector3 mirrorL(Vector3 v, bool left) { return left ? Vector3{-v.x, v.y, v.z} : v; }

namespace {

Matrix blendMatrix(const Matrix& A, const Matrix& B, float t) {
    Vector3 x = Vector3Lerp(mX(A), mX(B), t), y = Vector3Lerp(mY(A), mY(B), t);
    Vector3 p = Vector3Lerp(mPos(A), mPos(B), t);
    float sx = lerpf(Vector3Length(mX(A)), Vector3Length(mX(B)), t);
    x = vnorm(x);
    y = vnorm(Vector3Subtract(y, Vector3Scale(x, Vector3DotProduct(y, x))));
    Vector3 z = Vector3CrossProduct(x, y);
    return basisMatrix(Vector3Scale(x, sx), Vector3Scale(y, sx), Vector3Scale(z, sx), p);
}

// Ideal glass-in-hand transform (glass base at the origin, axis +Y) for a grip.
Matrix glassInHand(bool left, float handScale) {
    float inv = 1.f / handScale;
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
    Vector3 grip = mirrorL(GRIP_CENTER, left);
    Vector3 t = Vector3Subtract(grip, Vector3Scale(gy, GRIP_H * inv));
    return basisMatrix(Vector3Scale(gx, inv), Vector3Scale(gy, inv), Vector3Scale(gz, inv), t);
}

} // namespace

// ============================================================================ setup
Vector3 Cast::viewerPos() const { return viewerSet ? viewer.position : w3d::EYE; }

Vector3 Cast::headTarget(int seat) const {
    if (seat <= 0 || seat > 3) return viewerPos();
    const Opponent& o = opp[seat];
    return xfPoint(o.headW, {0, 0.1f, -0.07f});
}

void Cast::setupOpponents() {
    for (int s = 1; s <= 3; ++s) {
        Opponent& o = opp[s];
        o = Opponent{};
        o.seat = s;
        o.kind = s - 1;
        o.L = lookFor(o.kind);
        o.pm = &M.person[s];
        o.irisMesh = o.kind == 0 ? 0 : (o.kind == 1 ? 1 : 2);
        o.seed = (uint32_t)(seed * 131u + (uint64_t)s * 977u);
        o.pos = w3d::seatPos(s);
        o.yawDeg = w3d::seatYawDeg(s);
        o.root = trsYaw(o.pos, o.yawDeg, 1.f);
        o.rootInv = MatrixInvert(o.root);
        // the side players hunch over the table a little more (keeps their faces inside the human's view)
        o.leanBase = o.kind == 1 ? 0.18f : (o.kind == 2 ? 0.34f : 0.30f);
        o.lean = o.leanBase;
        o.breathRate = o.kind == 1 ? 0.21f : (o.kind == 2 ? 0.27f : 0.24f);
        o.breathDepth = o.kind == 1 ? 1.4f : 1.f;
        o.breath = rng.f(0.f, 6.28f);
        o.headStiff = o.kind == 2 ? 5.5f : 7.f;
        o.sipIn = rng.f(6.f, 22.f);
        o.smokeIn = rng.f(3.f, 9.f);
        o.tespihIn = rng.f(3.f, 8.f);
        o.fidgetIn = rng.f(8.f, 20.f);
        o.blinkIn = rng.f(0.5f, 3.f);
        o.gaze = o.gazeGoal = Vector3{0, TY, 0};
        o.who = s;
        for (int a = 0; a < 2; ++a) {
            restPose(o, a, a == 0 ? 0 : 0, o.arm[a].hold);
            o.arm[a].cur = o.arm[a].hold;
            o.arm[a].idleIn = rng.f(2.f, 7.f);
        }
        if (o.kind == 0) restPose(o, 1, 10, o.arm[1].hold);  // tespih hand
        if (o.kind == 1) restPose(o, 0, 11, o.arm[0].hold);  // cigarette hand
        o.arm[0].cur = o.arm[0].hold;
        o.arm[1].cur = o.arm[1].hold;
        Face base;
        if (o.kind == 1) base.smile = 0.25f;
        if (o.kind == 2) {
            base.smile = -0.3f;
            base.browTilt[0] = base.browTilt[1] = 0.08f;
        }
        if (o.kind == 0) {
            base.smile = 0.15f;
            base.lidOpen = 0.92f;
        }
        o.face = o.faceGoal = base;
    }
    // tea glasses (and the human's)
    for (int s = 0; s < 4; ++s) {
        TeaGlass& g = glass[s];
        g = TeaGlass{};
        // Composition (integration): the two glasses nearest the human's eye sit a few cm further from the
        // camera, inside their GLASS_KEEPOUT circle, so they don't loom in the frame corners / under the HUD buttons.
        constexpr Vector3 kNudge[4] = {{-0.030f, 0.f, -0.034f}, {0.f, 0.f, -0.046f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
        g.saucer = Vector3Add(w3d::GLASS_POS[s], kNudge[s]);
        glassHome[(size_t)s] = g.saucer;
        g.rest = Vector3Add(g.saucer, {0, SAUCER_TOP, 0});
        g.yaw = rng.f(0.f, 6.28f);
        g.level = rng.f(0.55f, 0.9f);
        g.oralet = s == 3;
        g.spoonYaw = w3d::seatYawDeg(s) * DEG2RAD + PI_F * 0.5f + rng.f(-0.3f, 0.3f);
        if (s == 1) g.spoonYaw -= 1.13f;  // nudged glass: the spoon points toward Rıza, clear of his istaka's end
        g.world = mul(RY(g.yaw), T(g.rest));
    }
}

// A hand lying on the felt with its palm centre over (x, z): lifted so the lowest point of the hand in this
// pose (a curled fingertip, a knuckle, the heel of the palm) just touches the cloth.
Key onFelt(float x, float z, Vector3 f, Vector3 p, HandPose pose, float hs, bool left) {
    f = vnorm(f);
    p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
    Vector3 X, Y, Z;
    handBasis(f, p, X, Y, Z);
    auto relY = [&](Vector3 h, float r) {  // height of hand point h (radius r) below/above the palm centre
        Vector3 o = Vector3Subtract(h, PALM_CENTER);
        if (left) o.x = -o.x;
        return (X.y * o.x + Y.y * o.y + Z.y * o.z) * hs - r * hs;
    };
    float low = relY(PALM_CENTER, 0.012f);
    for (const Vector3& t : handTips()[(size_t)pose]) low = std::min(low, relY(t, 0.006f));
    const Vector3 palmC{x, TY + 0.002f - low, z};
    Key k = mk(0.f, wristFor(palmC, f, p, PALM_CENTER, hs, left), f, p, pose);
    k.relax = 0.f;  // no relaxed wrist: following the forearm would push the fingers into the table
    return k;
}

// Rest spots. variant: 0 beside the rack, 1 fingers on the rack, 2 near the edge, 3 relaxed fist,
// 4 near own glass, 10 Rıza's tespih hand, 11 Mahmut's cigarette hand. The hands on the felt stay clear of
// the istaka (its end caps at x = ±0.28, its base board reaching z = rackZ() + 0.029).
void Cast::restPose(Opponent& o, int a, int variant, Key& k) {
    const bool left = a == 1;
    const float sd = left ? -1.f : 1.f;
    const float hs = o.L.handScale;
    Vector3 palmC, f, p;
    HandPose pose = HandPose::Rest;
    // Nuri's right hand beside his istaka would sit exactly behind the player's own tea glass (seen from the
    // player's eye): he keeps it in front of the rack's right part instead, turned in
    if (variant == 0 && o.seat == 3 && a == 0) variant = 2;
    switch (variant) {
    case 1: {
        // fingers on the tiles: from the owner's side, the curled fingertips rest against the faces of the upper
        // row just under its top edge, the wrist low over the table edge; seen from across the table only the
        // knuckles show over the tiles, and nothing ever goes through or behind the plank
        f = vnorm({-sd * 0.16f, 0.30f, -0.94f});
        p = vnorm(Vector3Subtract({0, -1, -0.1f}, Vector3Scale(f, Vector3DotProduct({0, -1, -0.1f}, f))));
        const Vector3 tip = handTips()[(size_t)HandPose::Rest][1];
        const Vector3 onFace = rackPoint(sd * 0.105f, kRackTopS - 0.013f, kRackFrontO + 0.0065f * hs);
        k = mk(0.f, wristFor(onFace, f, p, tip, hs, left), f, p, pose);
        k.relax = 0.f;
        k.touch = true;
        return;
    }
    case 2:  // forearms on the table edge, hands turned in toward the middle, in front of the rack
        k = onFelt(sd * 0.215f, -0.300f, {-sd * 0.84f, 0, -0.54f}, {0, -1, 0}, HandPose::Rest, hs, left);
        return;
    case 3:  // a relaxed fist on its side
        k = onFelt(sd * 0.25f, -0.300f, {-sd * 0.62f, -0.1f, -0.78f}, {-sd * 0.7f, -0.7f, 0}, HandPose::Fist, hs, left);
        return;
    case 4:  // beside the rack's end, fingers toward the own glass
        k = onFelt(sd * 0.355f, -0.315f, {-sd * 0.06f, 0, -1.f}, {0, -1, 0}, HandPose::Rest, hs, left);
        return;
    case 10:  // tespih: loose hold, hand lifted over the table edge
        palmC = {-0.215f, 0.85f, -0.275f};
        f = {0.32f, -0.25f, -0.91f};
        p = {0.8f, -0.6f, 0.08f};
        pose = HandPose::Hold;
        break;
    case 11:  // cigarette between the fingers, hand on its side near the ashtray (clear of the istaka's end)
        palmC = {0.366f, TY + 0.034f, -0.288f};
        f = {-0.15f, 0.08f, -0.985f};
        p = {-0.82f, -0.57f, 0.f};
        pose = HandPose::Cig;
        break;
    default:  // beside the rack's end
        k = onFelt(sd * 0.345f, -0.325f, {-sd * 0.20f, 0, -0.98f}, {0, -1, 0}, HandPose::Rest, hs, left);
        return;
    }
    k = mk(0.f, wristFor(palmC, f, p, PALM_CENTER, hs, left), f, p, pose);
}

void Cast::startTrack(Seated& s, int a, int kind, std::vector<Key> keys) {
    Arm& A = s.arm[a];
    if (keys.empty()) return;
    // the left hand holding the cards keeps them (no sips, gestures, tespih flips or idles from it)
    if (a == 1 && kind != TK_CardHold && s.who >= 1 && s.who <= 3 && opp[s.who].cards) return;
    // a sip cut short: the glass goes back onto its saucer instead of staying glued to the hand
    if (s.who >= 1 && s.who <= 3) {
        TeaGlass& g = glass[s.who];
        if (g.holder == s.who && g.holderArm == a) {
            g.holder = -1;
            g.from = g.world;
            g.blend = 0.f;
        }
    }
    keys[0] = A.cur;
    keys[0].t = 0.f;
    keys[0].event = 0;
    keys[0].headRel = false;
    keys[0].ease = 0;
    keys[0].lift = 0.f;
    // fingers resting on the tiles come up off them first (up and back toward the owner), then go
    if (keys[0].touch && keys.size() > 1 && !keys[1].touch && !keys[1].headRel && keys[1].t > 0.15f) {
        Key off = keys[0];
        off.t = std::min(0.12f, keys[1].t * 0.35f);
        off.pos = Vector3Add(off.pos, {0.f, 0.025f, 0.02f});
        off.touch = false;
        keys.insert(keys.begin() + 1, off);
    }
    A.track.keys = std::move(keys);
    A.track.t = 0.f;
    A.track.on = true;
    A.track.kind = kind;
    A.track.nextKey = 1;
    // the hand keeps the motion it had (capped: a new track bends the path, it doesn't fling the arm)
    auto cap = [](Vector3 v, float m) {
        const float l = Vector3Length(v);
        return l > m ? Vector3Scale(v, m / l) : v;
    };
    A.track.v0 = A.velInit ? cap(A.vel, 1.6f) : Vector3{0, 0, 0};
    A.track.f0 = A.velInit ? cap(A.fVel, 6.f) : Vector3{0, 0, 0};
    A.track.p0 = A.velInit ? cap(A.pVel, 6.f) : Vector3{0, 0, 0};
    if (s.who >= 1 && s.who <= 3) clearRack(opp[s.who], a, A.track);
}

// Raises the arcs between keys until the fingertips pass over the owner's istaka instead of through it
// (moving onto the tiles, back to the table, off to the discard pile...). Head-relative keys are left alone.
void Cast::clearRack(const Opponent& o, int a, Track& tr) const {
    const bool left = a == 1;
    const float hs = o.L.handScale;
    auto pokes = [&](const Key& k) {
        const Matrix H = handMatrix(k.pos, vnorm(k.fingers), vnorm(k.palm), hs);
        for (const Vector3& t : handTips()[(size_t)k.pose])
            if (insideRack(xfPoint(H, mirrorL(t, left)), 0.007f)) return true;
        return false;
    };
    // test the curve the hand really follows (head-relative keys placed where the head is now: a hand coming
    // down from the face must clear the rack too)
    Track r = tr;
    for (Key& k : r.keys)
        if (k.headRel) {
            k.pos = xfPoint(o.headLocal, k.pos);
            k.fingers = vnorm(xfDir(o.headLocal, k.fingers));
            k.palm = vnorm(xfDir(o.headLocal, k.palm));
            k.headRel = false;
        }
    for (size_t i = 1; i < r.keys.size(); ++i) {
        if (r.keys[i - 1].touch && r.keys[i].touch) continue;  // working on the rack itself
        const float t0 = r.keys[i - 1].t, t1 = r.keys[i].t;
        // an arc steeper than this would read as a twitch (the bump's peak speed is ~3 lift / span)
        const float maxLift = 0.4f * (t1 - t0);
        // (next to a key on the rack the fingers are on the tiles anyway: only the middle of the span must clear)
        const int j0 = r.keys[i - 1].touch ? 3 : 1, j1 = r.keys[i].touch ? 7 : 9;
        for (int attempt = 0; attempt < 12 && r.keys[i].lift < maxLift; ++attempt) {
            bool hit = false;
            for (int j = j0; j <= j1 && !hit; ++j) {
                Key k;
                evalTrack(r, lerpf(t0, t1, (float)j / 10.f), k);
                hit = pokes(k);
            }
            if (!hit) break;
            r.keys[i].lift = std::min(r.keys[i].lift + 0.02f, std::max(maxLift, r.keys[i].lift));
            tr.keys[i].lift = r.keys[i].lift;
        }
    }
}

void Cast::setMood(Opponent& o, Mood m, float seconds) {
    o.mood = m;
    o.moodT = seconds;
}

// ============================================================================ actions
// Reaches on the tiles' timeline (w3d::BOT_*_LEAD; Table3D holds the flights back until the fingers are there):
//  mode 0 (draw / take): reach the tile at `world`, pinch it as it lifts off, carry it along back to the rack;
//  mode 1 (discard / add to a meld): pick the tile off the rack, carry it to `world` as it flies, let go, return;
//  mode 2 (swap a joker): carry a tile to `world`, then bring the joker back to the rack;
//  mode 3 (give the tile back): a reluctant push toward `world`.
void Cast::reachTo(Opponent& o, int a, Vector3 world, int mode) {
    const bool left = a == 1;
    const float sd = left ? -1.f : 1.f;
    const float hs = o.L.handScale;
    Vector3 tL = xfPoint(o.rootInv, world);
    Vector3 flat = vnorm({tL.x - sd * 0.12f, 0, tL.z + 0.05f});
    Vector3 f = vnorm(Vector3Add(Vector3Scale(flat, 0.8f), {0, -0.6f, 0}));
    Vector3 p = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(f, Vector3DotProduct({0, -1, 0}, f))));
    // out of reach even leaning in (a meld across the table): the hand goes as far as it can and lets the tile
    // slide on from there
    const Vector3 shMax = shoulderAt(o.L, sd, o.leanBase + kMaxExtraLean);
    const float reach = (o.L.upperArm + o.L.foreArm) * kReachFrac + kProtract - 0.01f;
    bool far = false;
    for (int it = 0; it < 12; ++it) {
        const float d = Vector3Distance(wristFor(Vector3Add(tL, {0, 0.014f, 0}), f, p, PINCH_POINT, hs, left), shMax);
        if (d <= reach) break;
        const Vector3 back = vnorm({shMax.x - tL.x, 0.f, shMax.z - tL.z});
        tL = Vector3Add(tL, Vector3Scale(back, d - reach + 0.005f));
        far = true;
    }
    Vector3 over = wristFor(Vector3Add(tL, {0, 0.050f, 0}), f, p, PINCH_POINT, hs, left);
    Vector3 at = wristFor(Vector3Add(tL, {0, 0.014f, 0}), f, p, PINCH_POINT, hs, left);
    // the rack: a slot on the hand's own side, the tile held from the owner's side against the upper row's
    // face (the body still leans far over the rack after a long reach: a hand coming down onto the tiles from
    // above would pass through the face; this way it stays low, under the chin)
    Vector3 rf = vnorm({-sd * 0.15f, -0.28f, -0.95f}), rp = vnorm({0, -0.95f, 0.28f});
    const float slotX = sd * clampf(sd * tL.x * 0.5f + 0.05f, 0.125f, 0.22f);
    Vector3 rack = wristFor(rackPoint(slotX, kRackTopS - 0.014f, kRackFrontO + 0.008f), rf, rp, PINCH_POINT, hs, left);
    Vector3 rackUp = Vector3Add(rack, {0, 0.035f, 0.01f});
    Key rest;
    restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), rest);
    constexpr float TAKE = w3d::BOT_TAKE_LEAD, GIVE = w3d::BOT_GIVE_LEAD, FLY = w3d::BOT_TILE_FLIGHT;
    std::vector<Key> k;
    k.push_back(Key{});
    float tAt = 0.f;
    // from the rack out to the table: the pinch on the rack (the hand settles on the tile), up out of the
    // istaka, over to the target as the tile flies, and down with it
    auto carryOut = [&](HandPose arrive) {
        k.push_back(touching(mk(GIVE, rack, rf, rp, HandPose::Pinch, 0.f, 1)));
        k.push_back(touching(mk(GIVE + 0.10f, rackUp, rf, rp, HandPose::Pinch)));
        k.push_back(mk(GIVE + 0.36f, over, f, p, HandPose::Pinch, 0.04f));
        k.push_back(mk(GIVE + FLY, at, f, p, arrive, 0.f, 1));
    };
    // and back: lifted with the tile, over the rack as it lands, fingers opening on it
    auto carryBack = [&](float t0) {
        k.push_back(mk(t0 + 0.12f, Vector3Add(at, {0, 0.045f, 0}), f, p, HandPose::Pinch));
        k.push_back(touching(mk(t0 + 0.34f, rackUp, rf, rp, HandPose::Pinch, 0.03f)));
        k.push_back(touching(mk(t0 + 0.46f, rack, rf, rp, HandPose::Pinch, 0.f, 1)));
        k.push_back(touching(mk(t0 + 0.60f, Vector3Add(rack, {0, 0.012f, 0}), rf, rp, HandPose::Rest)));
    };
    switch (mode) {
    case 0:
        k.push_back(mk(TAKE - 0.11f, over, f, p, HandPose::Pinch, 0.02f));
        k.push_back(mk(TAKE, at, f, p, HandPose::Pinch, 0.f, 1));
        carryBack(TAKE);
        rest.t = TAKE + 1.05f;
        tAt = TAKE;
        break;
    case 1:
        carryOut(far ? HandPose::Open : HandPose::Pinch);
        k.push_back(mk(GIVE + FLY + 0.14f, Vector3Add(at, {0, 0.035f, 0.02f}), f, p, HandPose::Rest));
        rest.t = GIVE + FLY + 0.6f;
        tAt = GIVE + FLY;
        break;
    case 2: {
        carryOut(HandPose::Pinch);
        const float back = w3d::BOT_SWAPBACK_LEAD;  // the joker lifts off: the fingers are on it
        k.push_back(mk(back, Vector3Add(at, {0, 0.004f, 0}), f, p, HandPose::Pinch));
        carryBack(back);
        rest.t = back + 1.05f;
        tAt = GIVE + FLY;
        break;
    }
    default:
        carryOut(far ? HandPose::Open : HandPose::Pinch);
        k.push_back(mk(GIVE + FLY + 0.2f, Vector3Add(at, {0, 0.04f, 0.03f}), f, p, HandPose::Open));
        rest.t = GIVE + FLY + 0.75f;
        tAt = GIVE + FLY;
        break;
    }
    k.push_back(rest);
    startTrack(o, a, TK_Reach, k);
    o.gazeGoal = world;
    o.gaze = Vector3Lerp(o.gaze, world, 0.35f);
    o.gazeHold = tAt + 0.45f;
}

void Cast::slamMelds(Opponent& o, int zoneSeat, bool proud) {
    const w3d::RectXZ& z = w3d::MELD_ZONE[zoneSeat];
    Vector3 c{(z.x0 + z.x1) * 0.5f, TY, (z.z0 + z.z1) * 0.5f};
    Vector3 cL = xfPoint(o.rootInv, c);
    for (int a = 0; a < 2; ++a) {
        if (o.arm[a].track.on && o.arm[a].track.kind == TK_Sip) continue;  // that hand holds the tea glass
        const bool left = a == 1;
        const float sd = left ? -1.f : 1.f;
        const float hs = o.L.handScale;
        Vector3 tgt{cL.x + sd * 0.10f, TY, cL.z};
        Vector3 f = vnorm({-sd * 0.25f, -0.25f, -0.93f}), p{0, -1, 0};
        p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
        // pick the tiles up by their tops (thumb and fingers from above, nothing behind the plank; the body is
        // upright here, so the head is well above)
        const Vector3 gf = vnorm({-sd * 0.15f, -0.55f, -0.82f}), gp = vnorm({0, -0.82f, 0.55f});
        Vector3 grab = wristFor(rackPoint(sd * 0.14f, kRackTopS - 0.004f, kRackFrontO * 0.5f), gf, gp, PINCH_POINT, hs, left);
        Vector3 above = wristFor(Vector3Add(tgt, {0, 0.11f, 0}), f, p, PALM_CENTER, hs, left);
        Vector3 down = wristFor(Vector3Add(tgt, {0, 0.018f, 0}), f, p, PALM_CENTER, hs, left);
        Key rest;
        restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), rest);
        std::vector<Key> k;
        k.push_back(Key{});
        // the tiles leave the rack at BOT_MELD_LEAD and land about a flight later: the slap lands with them
        constexpr float M = w3d::BOT_MELD_LEAD, FLY = w3d::BOT_TILE_FLIGHT;
        k.push_back(touching(mk(M - 0.03f + 0.03f * a, grab, gf, gp, HandPose::Pinch, 0.02f, 1)));
        k.push_back(mk(M + 0.30f + 0.03f * a, above, f, p, HandPose::Hold, 0.05f, 0));
        k.push_back(mk(M + FLY + 0.03f * a, down, f, p, HandPose::Open, 0.f, 2, a == 0 ? KE_Slam : 0));
        k.push_back(mk(M + FLY + 0.35f, Vector3Add(down, {0, 0.004f, 0}), f, p, HandPose::Open));
        rest.t = M + FLY + (proud ? 0.8f : 0.85f);
        k.push_back(rest);
        startTrack(o, a, TK_Reach, k);
    }
    o.gazeGoal = c;
    o.gazeHold = 1.2f;
    if (proud) startGesture(o, G_Proud, 3.2f);
}

void Cast::startSip(Opponent& o) {
    TeaGlass& g = glass[o.seat];
    if (g.holder >= 0 || o.cards) return;
    const int a = 1;  // glasses stand at everyone's left
    const bool left = true;
    const float hs = o.L.handScale;
    Vector3 base = xfPoint(o.rootInv, g.rest);
    Vector3 axisUp{0, 1, 0};
    Vector3 f0, p0;
    gripDirs(axisUp, vnorm({0.35f, 0, -1.f}), left, f0, p0);
    Vector3 grip = Vector3Add(base, {0, GRIP_H, 0});
    Vector3 wGrip = wristFor(grip, f0, p0, GRIP_CENTER, hs, left);
    Vector3 wPre = Vector3Add(wristFor(Vector3Add(grip, {-0.035f, 0.02f, 0.03f}), f0, p0, GRIP_CENTER, hs, left), {0, 0, 0});
    Vector3 wLift = Vector3Add(wGrip, {0.01f, 0.07f, 0.03f});
    // at the lips (head space): the glass tilts toward the face
    const float tulip = M.tulipH;
    auto atMouth = [&](float tiltDeg, Key& k, float t, int ev) {
        float th = tiltDeg * DEG2RAD;
        Vector3 ax{0, std::cos(th), std::sin(th)};
        Vector3 rim = Vector3Add(o.pm->face.mouth, {0, -0.012f, -0.012f});
        Vector3 b = Vector3Subtract(rim, Vector3Scale(ax, tulip));
        Vector3 gp = Vector3Add(b, Vector3Scale(ax, GRIP_H));
        Vector3 f, p;
        gripDirs(ax, {0.25f, std::sin(th), -std::cos(th)}, left, f, p);
        k = mk(t, wristFor(gp, f, p, GRIP_CENTER, hs, left), f, p, HandPose::Grip, 0.f, 0, ev);
        k.headRel = true;
        k.pole = {-0.5f, -0.8f, 0.2f};
    };
    Key rest;
    restPose(o, a, o.kind == 0 ? 10 : 4, rest);
    std::vector<Key> k;
    k.push_back(Key{});
    k.push_back(mk(0.55f, wPre, f0, p0, HandPose::Hold, 0.03f));
    k.push_back(mk(0.80f, wGrip, f0, p0, HandPose::Grip, 0.f, 0, KE_GrabGlass));
    k.push_back(mk(1.10f, wLift, f0, p0, HandPose::Grip, 0.f, 1));
    Key m1, m2, m3;
    atMouth(30.f, m1, 1.65f, 0);
    atMouth(58.f, m2, 2.15f, KE_Sip);
    atMouth(40.f, m3, 2.45f, 0);
    k.push_back(m1);
    k.push_back(m2);
    k.push_back(m3);
    k.push_back(mk(3.00f, wLift, f0, p0, HandPose::Grip, 0.f, 0));
    k.push_back(mk(3.25f, wGrip, f0, p0, HandPose::Grip, 0.f, 0, KE_ReleaseGlass));
    k.push_back(mk(3.45f, wPre, f0, p0, HandPose::Hold, 0.f, 1));
    rest.t = 3.9f;
    k.push_back(rest);
    // head-relative keys must be resolved each frame; mark by index
    startTrack(o, a, TK_Sip, k);
    o.gazeGoal = g.rest;
    o.gazeHold = 1.0f;
    startGesture(o, G_Drink, 3.4f);
}

void Cast::startSmoke(Opponent& o, bool ashTap) {
    const int a = 0;
    const float hs = o.L.handScale;
    Key rest;
    restPose(o, a, 11, rest);
    std::vector<Key> k;
    k.push_back(Key{});
    if (ashTap) {
        Vector3 ash = xfPoint(o.rootInv, Vector3Add(ashtrayFor(o), {0, 0.07f, 0}));
        // cigarette (hand +Y) pointing down-forward over the ashtray: back of the hand faces down-forward
        Vector3 p = vnorm({-0.2f, 0.75f, 0.6f});
        Vector3 f = vnorm({-0.3f, 0.6f, -0.75f});
        f = vnorm(Vector3Subtract(f, Vector3Scale(p, Vector3DotProduct(f, p))));
        Vector3 w = wristFor(ash, f, p, CIG_HOLD, hs, false);
        k.push_back(mk(0.55f, w, f, p, HandPose::Cig, 0.03f));
        k.push_back(mk(0.66f, Vector3Add(w, {0, -0.015f, 0}), f, p, HandPose::Cig, 0.f, 2, KE_AshTap));
        k.push_back(mk(0.78f, w, f, p, HandPose::Cig, 0.f, 1));
        k.push_back(mk(0.90f, Vector3Add(w, {0, -0.015f, 0}), f, p, HandPose::Cig, 0.f, 2, KE_AshTap));
        k.push_back(mk(1.02f, w, f, p, HandPose::Cig, 0.f, 1));
        rest.t = 1.55f;
        k.push_back(rest);
        startTrack(o, a, TK_Smoke, k);
        o.gazeGoal = ashtrayFor(o);
        o.gazeHold = 1.2f;
        return;
    }
    // filter at the corner of the lips, cigarette pointing away (head space); the two fingers holding it point up
    // and out past his cheek, so the hand sits by the mouth and the chin instead of over the nose and eyes
    Vector3 c = vnorm({0.34f, -0.18f, -1.f});           // cigarette axis = hand +Y
    Vector3 p = Vector3Negate(c);                        // palm faces the face
    Vector3 f = vnorm({0.42f, 1.f, 0.f});
    f = vnorm(Vector3Subtract(f, Vector3Scale(p, Vector3DotProduct(f, p))));
    Vector3 lips = Vector3Add(o.pm->face.mouth, {0.012f, -0.003f, -0.008f});  // the corner of the mouth
    Vector3 filterOff = Vector3Add(CIG_HOLD, {0, -0.020f, 0});
    Vector3 w = wristFor(lips, f, p, filterOff, hs, false);
    Key at = mk(0.62f, w, f, p, HandPose::Cig, 0.f, 0, KE_Drag);
    at.headRel = true;
    at.pole = {0.6f, -0.8f, -0.1f};
    Key hold = at;
    hold.t = 1.35f;
    hold.event = 0;
    Key away = at;
    away.t = 1.7f;
    away.pos = Vector3Add(w, {0.05f, -0.06f, -0.08f});
    away.event = KE_Exhale;
    k.push_back(at);
    k.push_back(hold);
    k.push_back(away);
    rest.t = 2.35f;
    k.push_back(rest);
    startTrack(o, a, TK_Smoke, k);
}

void Cast::startTespihFlip(Opponent& o) {
    Key base;
    restPose(o, 1, 10, base);
    std::vector<Key> k;
    k.push_back(Key{});
    Key up = base;
    up.t = 0.35f;
    up.pos = Vector3Add(base.pos, {0.02f, 0.11f, -0.02f});
    up.palm = vnorm({0.6f, 0.4f, 0.3f});
    up.ease = 1;
    Key flick = base;
    flick.t = 0.62f;
    flick.pos = Vector3Add(base.pos, {-0.02f, 0.03f, -0.06f});
    flick.palm = vnorm({0.7f, -0.7f, -0.2f});
    flick.ease = 2;
    // the elbow stays down by his side (following the hand's line it would wing up over the shoulder)
    up.pole = flick.pole = vnorm({-0.35f, -0.85f, 0.35f});
    Key back = base;
    back.t = 1.25f;
    k.push_back(up);
    k.push_back(flick);
    k.push_back(back);
    startTrack(o, 1, TK_Tespih, k);
}

// ============================================================================ gestures
void Cast::startGesture(Opponent& o, int g, float dur) {
    o.gest = g;
    o.gestT = 0.f;
    o.gestDur = dur;
    const float hs = o.L.handScale;
    auto both = [&](const std::function<std::vector<Key>(int a, bool left, float sd)>& fn) {
        for (int a = 0; a < 2; ++a) {
            if (o.arm[a].track.on && (o.arm[a].track.kind == TK_Sip || o.arm[a].track.kind == TK_Reach)) continue;
            startTrack(o, a, TK_Gesture, fn(a, a == 1, a == 1 ? -1.f : 1.f));
        }
    };
    auto restKey = [&](int a, float t) {
        Key r;
        restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), r);
        r.t = t;
        return r;
    };
    switch (g) {
    case G_Celebrate:
        if (o.kind == 1) {  // Mahmut: fists up, then a fist on the table
            both([&](int a, bool left, float sd) {
                std::vector<Key> k{Key{}};
                Vector3 f{0, 1, 0}, p{0, 0, -1};
                k.push_back(mk(0.35f, {sd * 0.30f, 1.55f, -0.10f}, f, p, HandPose::Fist, 0.f, 1));
                k.push_back(mk(0.65f, {sd * 0.34f, 1.50f, -0.08f}, f, p, HandPose::Fist));
                k.push_back(mk(0.95f, {sd * 0.30f, 1.58f, -0.10f}, f, p, HandPose::Fist));
                if (!left) {
                    Vector3 ff{-0.4f, -0.2f, -0.9f}, pp{-0.7f, -0.7f, 0};
                    Vector3 w = wristFor({0.16f, TY + 0.035f, -0.31f}, ff, pp, PALM_CENTER, hs, false);
                    k.push_back(mk(1.35f, Vector3Add(w, {0, 0.12f, 0}), ff, pp, HandPose::Fist));
                    k.push_back(mk(1.47f, w, ff, pp, HandPose::Fist, 0.f, 2, KE_Slam));
                    k.push_back(mk(1.9f, Vector3Add(w, {0, 0.01f, 0}), ff, pp, HandPose::Fist));
                }
                k.push_back(restKey(a, 2.5f));
                return k;
            });
        } else if (o.kind == 0) {  // Rıza: palms up in thanks, then wipes his face (amin)
            both([&](int a, bool left, float sd) {
                std::vector<Key> k{Key{}};
                Vector3 f{-sd * 0.2f, 0.45f, -0.87f}, p{0, 1, 0};
                p = vnorm(Vector3Subtract(p, Vector3Scale(vnorm(f), Vector3DotProduct(p, vnorm(f)))));
                Vector3 w = wristFor({sd * 0.10f, 1.02f, -0.30f}, f, p, PALM_CENTER, hs, left);
                k.push_back(mk(0.5f, w, f, p, HandPose::Open, 0.02f));
                k.push_back(mk(1.5f, Vector3Add(w, {0, 0.015f, 0}), f, p, HandPose::Open));
                // wipe the face upward with both palms (head space)
                Vector3 fp{0, 1, 0}, pp{0, 0, 1};
                Key a1 = mk(1.9f, wristFor({sd * 0.03f, 0.02f, -0.11f}, fp, pp, PALM_CENTER, hs, left), fp, pp, HandPose::Open);
                a1.headRel = true;
                Key a2 = mk(2.4f, wristFor({sd * 0.035f, 0.13f, -0.115f}, fp, pp, PALM_CENTER, hs, left), fp, pp, HandPose::Open);
                a2.headRel = true;
                k.push_back(a1);
                k.push_back(a2);
                k.push_back(restKey(a, 3.1f));
                return k;
            });
        } else {  // Nuri: a satisfied double tap on the table
            std::vector<Key> k{Key{}};
            Vector3 f{-0.4f, -0.3f, -0.87f}, p{-0.6f, -0.8f, 0};
            Vector3 w = wristFor({0.14f, TY + 0.035f, -0.31f}, f, p, PALM_CENTER, hs, false);
            k.push_back(mk(0.45f, Vector3Add(w, {0, 0.07f, 0}), f, p, HandPose::Fist));
            k.push_back(mk(0.55f, w, f, p, HandPose::Fist, 0.f, 2, KE_Slam));
            k.push_back(mk(0.75f, Vector3Add(w, {0, 0.06f, 0}), f, p, HandPose::Fist, 0.f, 1));
            k.push_back(mk(0.85f, w, f, p, HandPose::Fist, 0.f, 2, KE_Slam));
            k.push_back(mk(1.2f, Vector3Add(w, {0, 0.01f, 0}), f, p, HandPose::Fist));
            k.push_back(restKey(0, 1.8f));
            startTrack(o, 0, TK_Gesture, k);
        }
        break;
    case G_TvCheer:
        both([&](int a, bool, float sd) {
            std::vector<Key> k{Key{}};
            Vector3 f{0, 1, 0}, p{0, 0, -1};
            k.push_back(mk(0.3f, {sd * 0.30f, 1.5f, -0.1f}, f, p, HandPose::Fist, 0.f, 1));
            k.push_back(mk(0.8f, {sd * 0.32f, 1.55f, -0.08f}, f, p, HandPose::Fist));
            k.push_back(restKey(a, 1.5f));
            return k;
        });
        break;
    case G_Facepalm: {
        std::vector<Key> k{Key{}};
        Vector3 f = vnorm({-0.35f, 0.94f, 0.f}), p{0, -0.2f, 1.f};
        p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
        Key a1 = mk(0.45f, wristFor({0.0f, 0.13f, -0.118f}, f, p, PALM_CENTER, hs, false), f, p, HandPose::Open, 0.f, 1);
        a1.headRel = true;
        a1.pole = {0.6f, -0.7f, -0.2f};
        Key a2 = a1;
        a2.t = 1.5f;
        a2.ease = 0;
        k.push_back(a1);
        k.push_back(a2);
        k.push_back(restKey(0, 2.1f));
        startTrack(o, 0, TK_Gesture, k);
        break;
    }
    case G_CallTea: {  // a raised hand toward the counter, a little wave
        const int a = o.kind == 1 ? 1 : 0;  // Mahmut keeps his cigarette
        if (o.arm[a].track.on && o.arm[a].track.kind == TK_Sip) break;
        const bool left = a == 1;
        const float sd = left ? -1.f : 1.f;
        Vector3 hp{sd * 0.30f, 1.42f, -0.10f};
        Vector3 toCounter = vnorm(Vector3Subtract(xfPoint(o.rootInv, Vector3Add(w3d::COUNTER_POS, {0, 1.3f, 0})), hp));
        Vector3 f = vnorm({sd * 0.12f, 1.f, -0.1f});
        Vector3 p = vnorm(Vector3Subtract(toCounter, Vector3Scale(f, Vector3DotProduct(toCounter, f))));
        std::vector<Key> k{Key{}};
        Key up = mk(0.40f, wristFor(hp, f, p, PALM_CENTER, hs, left), f, p, HandPose::Open, 0.f, 1);
        up.pole = {sd * 0.8f, -0.5f, 0.3f};
        k.push_back(up);
        for (int i = 0; i < 2; ++i) {
            Key w = up;
            w.t = 0.62f + 0.3f * i;
            w.pos = Vector3Add(up.pos, {sd * 0.035f, 0.01f, 0.f});
            w.fingers = vnorm(Vector3Add(f, {sd * 0.25f, 0.f, 0.f}));
            k.push_back(w);
            Key b = up;
            b.t = 0.77f + 0.3f * i;
            k.push_back(b);
        }
        k.push_back(restKey(a, 1.75f));
        startTrack(o, a, TK_Gesture, k);
        break;
    }
    case G_Disbelief:  // both hands on the head
        both([&](int a, bool left, float sd) {
            std::vector<Key> k{Key{}};
            Vector3 f = vnorm({-sd * 0.3f, 0.3f, 0.9f}), p{sd * 1.f, -0.3f, 0.f};
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            Key a1 = mk(0.5f, wristFor({sd * 0.075f, 0.19f, -0.01f}, f, p, PALM_CENTER, hs, left), f, p, HandPose::Open, 0.f, 1);
            a1.headRel = true;
            a1.pole = {sd * 0.9f, -0.2f, 0.3f};
            Key a2 = a1;
            a2.t = 1.8f;
            k.push_back(a1);
            k.push_back(a2);
            k.push_back(restKey(a, 2.5f));
            return k;
        });
        break;
    case G_Shrug:
        both([&](int a, bool left, float sd) {
            std::vector<Key> k{Key{}};
            Vector3 f{sd * 0.45f, 0.1f, -0.88f}, p{0, 1, 0};
            f = vnorm(f);
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            Vector3 w = wristFor({sd * 0.30f, 0.93f, -0.26f}, f, p, PALM_CENTER, hs, left);
            k.push_back(mk(0.4f, w, f, p, HandPose::Open, 0.03f));
            k.push_back(mk(1.1f, Vector3Add(w, {0, 0.01f, 0}), f, p, HandPose::Open));
            k.push_back(restKey(a, 1.7f));
            return k;
        });
        break;
    case G_Clap:
        both([&](int a, bool left, float sd) {
            std::vector<Key> k{Key{}};
            Vector3 f = vnorm({-sd * 0.3f, 0.5f, -0.8f}), p{-sd * 1.f, 0, 0};
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            float t = 0.35f;
            for (int i = 0; i < 4; ++i) {
                Vector3 apart = wristFor({sd * 0.10f, 0.94f, -0.37f}, f, p, PALM_CENTER, hs, left);
                Vector3 touch = wristFor({sd * 0.012f, 0.94f, -0.37f}, f, p, PALM_CENTER, hs, left);
                k.push_back(mk(t, apart, f, p, HandPose::Open, 0.f, 1));
                k.push_back(mk(t + 0.16f, touch, f, p, HandPose::Open, 0.f, 2));
                t += 0.34f;
            }
            k.push_back(restKey(a, t + 0.4f));
            return k;
        });
        break;
    case G_Point: {  // laughing, pointing at the human
        Arm& A = o.arm[0];
        Vector3 vw = viewerPos();
        Vector3 sh = A.shoulder;
        Vector3 dir = vnorm(Vector3Subtract(vw, sh));
        Vector3 tgtW = Vector3Add(sh, Vector3Scale(dir, 0.50f));
        Vector3 tL = xfPoint(o.rootInv, tgtW);
        Vector3 f = xfDir(o.rootInv, dir), p{0, -1, 0};
        p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
        std::vector<Key> k{Key{}};
        k.push_back(mk(0.4f, tL, f, p, HandPose::Point, 0.f, 1));
        k.push_back(mk(0.7f, Vector3Add(tL, {0, 0.02f, 0}), f, p, HandPose::Point));
        k.push_back(mk(1.0f, tL, f, p, HandPose::Point));
        k.push_back(mk(1.4f, Vector3Add(tL, {0, 0.02f, 0}), f, p, HandPose::Point));
        k.push_back(restKey(0, 2.1f));
        startTrack(o, 0, TK_Gesture, k);
        break;
    }
    case G_Grumble:
        if (o.kind == 2) {  // dismissive wave
            std::vector<Key> k{Key{}};
            Vector3 f{-0.2f, 0.4f, -0.9f}, p{0, -0.3f, 1.f};
            f = vnorm(f);
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            Vector3 w = wristFor({0.20f, 1.0f, -0.28f}, f, p, PALM_CENTER, hs, false);
            k.push_back(mk(0.4f, w, f, p, HandPose::Open, 0.02f));
            Vector3 f2 = vnorm({0.7f, 0.2f, -0.7f});
            Vector3 p2 = vnorm(Vector3Subtract({0.2f, -0.2f, 1.f}, Vector3Scale(f2, Vector3DotProduct({0.2f, -0.2f, 1.f}, f2))));
            k.push_back(mk(0.62f, wristFor({0.34f, 0.96f, -0.24f}, f2, p2, PALM_CENTER, hs, false), f2, p2, HandPose::Open, 0.f, 1));
            k.push_back(restKey(0, 1.3f));
            startTrack(o, 0, TK_Gesture, k);
        }
        break;
    case G_Proud:
        both([&](int a, bool left, float sd) {
            std::vector<Key> k{Key{}};
            Vector3 f{sd * 0.35f, 0.2f, -0.9f}, p{0, 1, 0};
            f = vnorm(f);
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            Vector3 w = wristFor({sd * 0.27f, 0.90f, -0.30f}, f, p, PALM_CENTER, hs, left);
            k.push_back(mk(1.55f, restKey(a, 0).pos, restKey(a, 0).fingers, restKey(a, 0).palm, restKey(a, 0).pose));
            k.push_back(mk(1.95f, w, f, p, HandPose::Open, 0.03f));
            k.push_back(mk(2.6f, Vector3Add(w, {0, 0.01f, 0}), f, p, HandPose::Open));
            k.push_back(restKey(a, 3.2f));
            return k;
        });
        break;
    default:
        break;
    }
}

// ============================================================================ events
void Cast::onArmEvent(Seated& s, int a, int ev) {
    if (s.who >= 1 && s.who <= 3) {
        Opponent& o = opp[s.who];
        TeaGlass& g = glass[o.seat];
        switch (ev) {
        case KE_GrabGlass:
            if (g.holder >= 0) break;  // the çaycı has it
            g.holder = o.seat;
            g.holderArm = a;
            g.inHand = glassInHand(a == 1, o.L.handScale);
            g.from = g.world;
            g.blend = 0.f;
            break;
        case KE_ReleaseGlass:
            if (g.holder != o.seat) break;
            g.holder = -1;
            g.from = g.world;
            g.blend = 0.f;
            sfx(ui::Sfx::GlassSet);
            break;
        case KE_Sip:
            if (g.holder != o.seat) break;
            g.level = std::max(0.f, g.level - rng.f(0.10f, 0.16f));
            sfx(ui::Sfx::TeaSip);
            break;
        case KE_Drag: o.drag = 1.f; break;
        case KE_Exhale: o.exhaleT = 0.f; break;
        case KE_Slam:
            for (TeaGlass& t : glass)
                if (t.holder < 0) t.blend = std::min(t.blend, 0.999f);
            break;
        case KE_Clink: sfx(ui::Sfx::TeaClink); break;
        default: break;
        }
    }
}

void Cast::react(const okey::GameEvent& e, const okey::Game& g) {
    using okey::EvType;
    const int p = e.player;
    const bool bot = p >= 1 && p <= 3;
    auto lookAll = [&](Vector3 w, float hold, int except) {
        for (int s = 1; s <= 3; ++s) {
            if (s == except) continue;
            Opponent& o = opp[s];
            if (rng.chance(0.85f)) {
                o.gazeGoal = w;
                o.gazeHold = hold + rng.f(0.f, 0.8f);
            }
        }
    };
    auto moodAll = [&](Mood m, float t, int except) {
        for (int s = 1; s <= 3; ++s)
            if (s != except) setMood(opp[s], m, t + rng.f(0.f, 0.8f));
    };
    // the hand on the target's side — unless it is busy with the tea glass (then the other one)
    auto handFor = [&](Opponent& o, Vector3 world) {
        Vector3 l = xfPoint(o.rootInv, world);
        int pref = l.x >= 0.f ? 0 : 1;
        // Rıza keeps the tespih in his left hand, Mahmut the cigarette in his right: those hands only
        // reach for things well over on their own side
        if (o.kind == 0 && pref == 1 && l.x > -0.3f) pref = 0;
        if (o.kind == 1 && pref == 0 && l.x < 0.3f) pref = 1;
        if (o.arm[pref].track.on && o.arm[pref].track.kind == TK_Sip) return 1 - pref;
        return pref;
    };
    switch (e.type) {
    case EvType::MatchStart:
        for (int s = 1; s <= 3; ++s) setMood(opp[s], Mood::Neutral, 0.f);
        break;
    case EvType::HandStart:
        for (int s = 1; s <= 3; ++s) {
            Opponent& o = opp[s];
            setMood(o, Mood::Neutral, 0.f);
            o.gazeGoal = Vector3Add(xfPoint(o.root, {0, 0.84f, rackZ()}), {0, 0, 0});
            o.gazeHold = rng.f(1.5f, 3.f);
            o.leanAdd = 0.f;
            // arrange tiles on the rack for a moment
            for (int a = 0; a < 2; ++a) {
                if (o.arm[a].track.on) continue;
                if ((o.kind == 0 && a == 1) || (o.kind == 1 && a == 0)) continue;
                Key k1;
                restPose(o, a, 1, k1);
                k1.t = rng.f(0.6f, 1.0f);
                Key k2 = k1;
                k2.t = k1.t + rng.f(1.2f, 2.2f);
                k2.pos = Vector3Add(k1.pos, {a == 0 ? -0.06f : 0.06f, 0.f, 0.f});
                Key k3;
                restPose(o, a, 0, k3);
                k3.t = k2.t + 0.7f;
                startTrack(o, a, TK_Idle, {Key{}, k1, k2, k3});
            }
        }
        if (rng.chance(0.6f)) sfx(ui::Sfx::Chair);
        break;
    case EvType::TurnStart:
        activeSeat = p;
        lastTurnSeat = p;
        humanWait = 0.f;
        if (p >= 0 && p < 4) {
            if (p == 0) {
                lookAll(rng.chance(0.5f) ? viewerPos() : Vector3{0, 0.84f, 0.45f}, 1.8f, -1);
            } else {
                lookAll(headTarget(p), 1.2f, p);
                Opponent& o = opp[p];
                o.gazeGoal = xfPoint(o.root, {0, 0.84f, rackZ()});
                o.gazeHold = 1.5f;
                o.turnT = 0.f;
            }
        }
        break;
    case EvType::DrawPile:
        if (bot) {
            Opponent& o = opp[p];
            reachTo(o, handFor(o, w3d::PILE_POS), w3d::PILE_POS, 0);
            o.chinRest = false;
        } else if (p == 0) {
            lookAll(w3d::PILE_POS, 0.8f, -1);
        }
        break;
    case EvType::TakeLeft:
        if (bot) {
            Opponent& o = opp[p];
            reachTo(o, handFor(o, w3d::DISCARD_POS[okey::Game::leftOf(p)]), w3d::DISCARD_POS[okey::Game::leftOf(p)], 0);
            setMood(o, Mood::Happy, 2.f);
            o.chinRest = false;
        } else if (p == 0) {
            lookAll(w3d::DISCARD_POS[3], 1.f, -1);
            if (rng.chance(0.5f)) setMood(opp[3], Mood::Grumpy, 2.f);
        }
        break;
    case EvType::ReturnLeft:
        if (bot) {
            Opponent& o = opp[p];
            reachTo(o, handFor(o, w3d::DISCARD_POS[okey::Game::leftOf(p)]), w3d::DISCARD_POS[okey::Game::leftOf(p)], 3);
            setMood(o, Mood::Sad, 3.f);
        }
        break;
    case EvType::Discard:
        if (bot) {
            Opponent& o = opp[p];
            reachTo(o, handFor(o, w3d::DISCARD_POS[p]), w3d::DISCARD_POS[p], 1);
            o.chinRest = false;
        } else if (p == 0) {
            lookAll(w3d::DISCARD_POS[0], 0.9f, -1);
        }
        // the next player eyes the discard
        {
            int nx = okey::Game::rightOf(p);
            if (nx >= 1 && nx <= 3) {
                opp[nx].gazeGoal = w3d::DISCARD_POS[p];
                opp[nx].gazeHold = 1.1f;
            }
        }
        break;
    case EvType::Open:
        if (bot) {
            slamMelds(opp[p], p, true);
            setMood(opp[p], Mood::Smug, 5.f);
            moodAll(Mood::Surprised, 1.6f, p);
            const w3d::RectXZ& z = w3d::MELD_ZONE[p];
            lookAll({(z.x0 + z.x1) * 0.5f, TY, (z.z0 + z.z1) * 0.5f}, 1.6f, p);
        } else if (p == 0) {
            moodAll(Mood::Surprised, 1.8f, -1);
            lookAll({0, TY, 0.27f}, 1.8f, -1);
        }
        break;
    case EvType::LayMelds:
        if (bot) slamMelds(opp[p], p, false);
        break;
    case EvType::AddToMeld:
    case EvType::SwapJoker: {
        int owner = p;
        if (e.meld >= 0 && e.meld < (int)g.table().size()) owner = g.table()[e.meld].owner;
        owner = std::clamp(owner, 0, 3);
        const w3d::RectXZ& z = w3d::MELD_ZONE[owner];
        Vector3 c{(z.x0 + z.x1) * 0.5f, TY, (z.z0 + z.z1) * 0.5f};
        if (bot) {
            Opponent& o = opp[p];
            reachTo(o, handFor(o, c), c, e.type == EvType::SwapJoker ? 2 : 1);
            if (e.type == EvType::SwapJoker) setMood(o, Mood::Happy, 3.f);
        }
        lookAll(c, 1.f, p);
        if (e.type == EvType::SwapJoker) moodAll(Mood::Surprised, 1.4f, p);
        break;
    }
    case EvType::Penalty:
        if (bot) {
            Opponent& o = opp[p];
            if (o.kind == 0) {
                startGesture(o, G_Shrug, 1.8f);
                setMood(o, Mood::Sad, 3.f);
            } else if (o.kind == 1) {
                startGesture(o, G_Facepalm, 2.2f);
                setMood(o, Mood::Sad, 3.5f);
            } else {
                startGesture(o, G_HeadShake, 1.4f);
                setMood(o, Mood::Grumpy, 4.f);
            }
            for (int s = 1; s <= 3; ++s) {
                if (s == p) continue;
                Opponent& q = opp[s];
                q.gazeGoal = headTarget(p);
                q.gazeHold = 1.6f;
                if (q.kind == 1) {
                    setMood(q, Mood::Laugh, 2.2f);
                    startGesture(q, G_Laugh, 1.8f);
                } else {
                    setMood(q, q.kind == 2 ? Mood::Smug : Mood::Content, 2.5f);
                }
            }
        } else if (p == 0) {
            for (int s = 1; s <= 3; ++s) {
                Opponent& q = opp[s];
                q.gazeGoal = viewerPos();
                q.gazeHold = 2.2f;
                if (q.kind == 1) {
                    setMood(q, Mood::Laugh, 2.6f);
                    if (!q.arm[0].track.on) startGesture(q, G_Point, 2.2f);
                    else startGesture(q, G_Laugh, 1.8f);
                } else if (q.kind == 2) {
                    setMood(q, Mood::Smug, 2.5f);
                    startGesture(q, G_HeadShake, 1.2f);
                } else {
                    setMood(q, Mood::Sad, 2.f);
                }
            }
        }
        break;
    case EvType::HandEnd: {
        activeSeat = -1;
        lastTurnSeat = -1;
        const int w = p;
        for (int s = 1; s <= 3; ++s) {
            Opponent& o = opp[s];
            o.chinRest = false;
            if (s == w) {
                startGesture(o, G_Celebrate, 3.0f);
                setMood(o, o.kind == 1 ? Mood::Laugh : Mood::Happy, 6.f);
                continue;
            }
            o.gazeGoal = w == 0 ? viewerPos() : (w > 0 ? headTarget(w) : Vector3{0, TY, 0});
            o.gazeHold = 2.5f + rng.f(0.f, 1.f);
            if (w < 0) {
                startGesture(o, G_Shrug, 1.8f);
                setMood(o, Mood::Neutral, 2.f);
            } else if (w == 0) {
                if (o.kind == 0) {
                    startGesture(o, G_Clap, 2.0f);
                    setMood(o, Mood::Content, 4.f);
                } else if (o.kind == 1) {
                    startGesture(o, G_Disbelief, 2.5f);
                    setMood(o, Mood::Surprised, 3.f);
                } else {
                    startGesture(o, G_HeadShake, 1.4f);
                    setMood(o, Mood::Grumpy, 4.f);
                }
            } else {
                if (o.kind == 1) startGesture(o, G_Disbelief, 2.5f);
                else if (o.kind == 2) startGesture(o, G_Grumble, 1.5f);
                else startGesture(o, G_Nod, 1.0f);
                setMood(o, o.kind == 0 ? Mood::Content : Mood::Grumpy, 4.5f);
            }
        }
        break;
    }
    case EvType::MatchEnd: {
        const int w = p;
        int leaders = 0;
        for (int s = 0; w >= 0 && w <= 3 && s <= 3; ++s) leaders += g.player(s).totalScore == g.player(w).totalScore;
        for (int s = 1; s <= 3; ++s) {
            Opponent& o = opp[s];
            if (leaders > 1 && g.player(s).totalScore == g.player(w).totalScore) {
                startGesture(o, G_Nod, 1.2f); // a shared first place: pleased, no victory dance
                setMood(o, Mood::Happy, 6.f);
            } else if (s == w) {
                startGesture(o, G_Celebrate, 3.0f);
                setMood(o, Mood::Laugh, 8.f);
            } else if (w == 0) {
                o.gazeGoal = viewerPos();
                o.gazeHold = 3.f;
                if (o.kind == 0) startGesture(o, G_Clap, 2.2f);
                setMood(o, o.kind == 2 ? Mood::Grumpy : Mood::Content, 6.f);
            } else {
                setMood(o, Mood::Grumpy, 6.f);
                if (o.kind == 1) startGesture(o, G_Disbelief, 2.5f);
            }
        }
        break;
    }
    default:
        break;
    }
}

// ============================================================================ idle behaviour
void Cast::pickGaze(Opponent& o) {
    // weights: own rack, active player, table centre, the human, another regular, room, own glass
    float w[7] = {0.30f, 0.22f, 0.10f, 0.10f, 0.12f, 0.10f, 0.06f};
    if (activeSeat == o.seat) {
        w[0] = 0.75f;
        w[3] = 0.04f;
    }
    if (activeSeat == 0 && humanWait > 8.f) w[3] = 0.35f;
    if (titleMode) {
        w[0] = 0.05f;
        w[1] = 0.f;
        w[4] = 0.35f;
        w[5] = 0.25f;
        w[3] = 0.05f;
    }
    float sum = 0.f;
    for (float v : w) sum += v;
    float r = rng.f() * sum;
    int pick = 0;
    for (; pick < 6; ++pick) {
        if (r < w[pick]) break;
        r -= w[pick];
    }
    Vector3 tgt;
    float hold = rng.f(1.4f, 3.8f);
    switch (pick) {
    case 0:
        tgt = xfPoint(o.root, {rng.f(-0.2f, 0.2f), 0.84f, rackZ()});
        break;
    case 1:
        tgt = activeSeat >= 1 && activeSeat != o.seat ? headTarget(activeSeat)
              : activeSeat == 0 ? Vector3{rng.f(-0.2f, 0.2f), 0.84f, 0.45f}
                                : Vector3{0, TY, 0};
        break;
    case 2: tgt = {rng.f(-0.15f, 0.15f), TY, rng.f(-0.15f, 0.15f)}; break;
    case 3:
        tgt = viewerPos();
        hold = rng.f(1.0f, 2.2f);
        break;
    case 4: {
        int other = 1 + rng.i(3);
        if (other == o.seat) other = other % 3 + 1;
        tgt = headTarget(other);
        break;
    }
    case 5: {
        // somewhere in the room: the counter, the çaycı, the windows, the TV side
        const Vector3 spots[4] = {Vector3Add(w3d::COUNTER_POS, {0, 1.2f, 0}), Vector3Add(boy.pos, {0, 1.5f, 0}),
                                  {w3d::ROOM_X0 + 0.2f, 1.5f, 0.5f}, kTvPos};
        tgt = spots[rng.i(4)];
        hold = rng.f(0.8f, 2.f);
        break;
    }
    default: tgt = glass[o.seat].rest; break;
    }
    o.gazeGoal = tgt;
    o.gazeHold = hold;
}

void Cast::idleOpponent(Opponent& o, float dt) {
    // mood decay
    if (o.moodT > 0.f) {
        o.moodT -= dt;
        if (o.moodT <= 0.f) o.mood = Mood::Neutral;
    }
    // gaze
    o.gazeHold -= dt;
    if (o.gazeHold <= 0.f) pickGaze(o);
    // turn / thinking
    const bool myTurn = activeSeat == o.seat && !titleMode;
    o.think = approachExp(o.think, myTurn ? 1.f : 0.f, 2.f, dt);
    if (myTurn) {
        o.turnT += dt;
        if (o.turnT > 1.4f && !o.chinRest && !o.arm[0].track.on && o.kind != 1 && rng.chance(dt * 0.8f)) {
            // chin resting on the knuckles while thinking (head space: fist pointing up under the chin)
            const float hs = o.L.handScale;
            Vector3 f = vnorm({-0.12f, 0.95f, -0.28f}), p = vnorm({0.15f, 0.28f, 0.95f});
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            // the knuckles tuck under the jaw, just behind the point of the chin (placed exactly: a relaxed wrist
            // would tip the fist up over the mouth)
            Key k1 = mk(0.7f, wristFor(Vector3Add(o.pm->face.chin, {0.004f, -0.010f, 0.006f}), f, p, FIST_TOP, hs, false), f, p,
                        HandPose::Fist);
            k1.headRel = true;
            k1.relax = 0.f;
            k1.pole = {0.30f, -0.9f, -0.2f};
            startTrack(o, 0, TK_Chin, {Key{}, k1});
            o.chinRest = true;
        }
        if (o.kind == 1 && o.turnT > 1.2f && !o.arm[1].track.on && rng.chance(dt * 0.7f)) {
            // Mahmut drums his fingers on the table while he thinks
            const float hs = o.L.handScale;
            Vector3 f = vnorm({0.28f, -0.10f, -0.95f}), p{0, -1, 0};
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            Vector3 w = wristFor({-0.35f, TY + 0.02f, -0.29f}, f, p, PALM_CENTER, hs, true);
            std::vector<Key> k{Key{}};
            k.push_back(mk(0.45f, w, f, p, HandPose::Rest, 0.02f));
            float t = 0.45f;
            for (int i = 0; i < 7; ++i) {
                t += 0.11f;
                k.push_back(mk(t, Vector3Add(w, {0.f, 0.012f, 0.f}), f, p, i % 2 ? HandPose::Rest : HandPose::Point, 0.f, 3));
                t += 0.09f;
                k.push_back(mk(t, w, f, p, HandPose::Rest, 0.f, 2));
            }
            Key r;
            restPose(o, 1, 0, r);
            r.t = t + 0.6f;
            k.push_back(r);
            startTrack(o, 1, TK_Idle, k);
        }
        if (o.mood == Mood::Neutral && o.turnT > 0.6f) o.mood = Mood::Thinking;
    } else {
        o.turnT = 0.f;
        if (o.mood == Mood::Thinking) o.mood = Mood::Neutral;
        if (o.chinRest && o.arm[0].track.kind == TK_Chin && !o.arm[0].track.on) {
            Key r;
            restPose(o, 0, 0, r);
            r.t = 0.8f;
            startTrack(o, 0, TK_Idle, {Key{}, r});
            o.chinRest = false;
        }
    }
    // sipping
    o.sipIn -= dt;
    TeaGlass& g = glass[o.seat];
    if (o.sipIn <= 0.f) {
        bool armFree = (!o.arm[1].track.on || o.arm[1].track.kind == TK_Idle || o.arm[1].track.kind == TK_Tespih) &&
                       !(o.arm[0].track.on && o.arm[0].track.kind == TK_Smoke);  // one hand at the face at a time
        // (not while the çaycı is on his round to our table: fresh tea is coming)
        const bool teaComing = boy.plan == 1 && (boy.state == 1 || boy.state == 2);
        if (armFree && g.level > 0.04f && g.holder < 0 && o.gest == G_None && !teaComing && !(myTurn && o.turnT < 0.5f) &&
            o.talkT < 0.f) {  // (Yüz: not in the middle of a sentence)
            startSip(o);
            o.sipIn = (o.kind == 1 ? rng.f(14.f, 28.f) : rng.f(18.f, 36.f)) * (titleMode ? 0.8f : 1.f);
        } else {
            o.sipIn = rng.f(1.f, 3.f);
        }
    }
    // Mahmut smokes
    if (o.kind == 1) {
        o.smokeIn -= dt;
        if (o.smokeIn <= 0.f) {
            if (!o.arm[0].track.on && o.gest == G_None && !(o.arm[1].track.on && o.arm[1].track.kind == TK_Sip) &&
                o.talkT < 0.f) {  // (Yüz: not while he talks)
                startSmoke(o, rng.chance(0.3f));
                o.smokeIn = rng.f(7.f, 16.f);
            } else {
                o.smokeIn = rng.f(1.f, 2.5f);
            }
        }
    }
    // Rıza flips his tespih
    if (o.kind == 0) {
        o.tespihIn -= dt;
        if (o.tespihIn <= 0.f) {
            if (!o.arm[1].track.on) startTespihFlip(o);
            o.tespihIn = rng.f(6.f, 13.f);
        }
    }
    // other fidgets
    o.fidgetIn -= dt;
    if (o.fidgetIn <= 0.f) {
        o.fidgetIn = rng.f(12.f, 26.f);
        const float hs = o.L.handScale;
        if (o.kind == 2 && !o.arm[0].track.on) {
            // Nuri pushes his spectacles up: the index fingertip comes in from the front onto the frame's
            // bridge (just above the nose), a quick nudge up, and away again — never along or under the nose
            const Vector3 f = vnorm({0.05f, 0.45f, 0.89f});
            Vector3 p = vnorm({0.f, -0.95f, 0.35f});
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            const Vector3 tip = handTips()[(size_t)HandPose::Point][0];
            const Vector3 bridge{0.f, 0.1075f, -0.0995f};  // the front of the spectacles' bridge (head space)
            auto at = [&](float t, Vector3 pt, int ease) {
                Key k = mk(t, wristFor(pt, f, p, tip, hs, false), f, p, HandPose::Point, 0.f, ease);
                k.headRel = true;
                k.relax = 0.f;
                k.pole = {0.35f, -0.55f, -0.75f};  // the elbow forward and low: the forearm rises to the face
                return k;
            };
            Key k0 = at(0.55f, Vector3Add(bridge, {0.f, -0.012f, -0.055f}), 1);  // in front of the face
            Key k1 = at(0.78f, bridge, 0);
            Key k2 = at(0.98f, Vector3Add(bridge, {0.f, 0.0045f, 0.001f}), 0);  // the nudge (0.2 s on the frame)
            Key k3 = at(1.18f, Vector3Add(bridge, {0.f, -0.006f, -0.06f}), 1);
            Key r;
            restPose(o, 0, 0, r);
            r.t = 1.75f;
            startTrack(o, 0, TK_Idle, {Key{}, k0, k1, k2, k3, r});
        } else if (o.kind == 1 && !o.arm[1].track.on) {
            // Mahmut rubs his bald head
            Vector3 f = vnorm({0.3f, 0.2f, -0.93f}), p{0, -1, 0};
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            std::vector<Key> k{Key{}};
            for (int i = 0; i < 4; ++i) {
                Key kk = mk(0.6f + 0.22f * i, wristFor({-0.02f, 0.222f, -0.01f + (i % 2 ? 0.03f : -0.01f)}, f, p, PALM_CENTER, hs, true), f, p,
                            HandPose::Open, 0.f, i == 0 ? 1 : 0);
                kk.headRel = true;
                kk.pole = {-0.9f, -0.2f, 0.2f};
                k.push_back(kk);
            }
            Key r;
            restPose(o, 1, 0, r);
            r.t = 2.1f;
            k.push_back(r);
            startTrack(o, 1, TK_Idle, k);
            if (!titleMode && rng.chance(0.4f)) setMood(o, Mood::Thinking, 1.5f);
        } else if (o.kind == 0 && !o.arm[0].track.on) {
            // Rıza twirls the end of his mustache: the hand comes from the side, fingers pointing in toward the
            // tip, palm down, thumb behind the whiskers and index in front (the other fingers curl under, in
            // front of the jaw — nothing over the nose or the mouth); then he draws it outward and down
            const Vector3 f = vnorm({-0.88f, 0.22f, -0.30f});
            Vector3 p = vnorm({0.05f, -0.92f, -0.35f});
            p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
            const Vector3 end{0.036f, 0.0445f, -0.0905f};  // just outside the mustache's right tip (head space)
            auto at = [&](float t, Vector3 pt, int ease) {
                Key k = mk(t, wristFor(pt, f, p, PINCH_POINT, hs, false), f, p, HandPose::Pinch, 0.f, ease);
                k.headRel = true;
                k.relax = 0.f;
                k.pole = {0.7f, -0.7f, 0.f};
                return k;
            };
            Key k0 = at(0.45f, Vector3Add(end, {0.035f, -0.01f, -0.03f}), 1);
            Key k1 = at(0.68f, end, 0);
            Key k2 = at(1.15f, Vector3Add(end, {0.014f, -0.008f, 0.004f}), 0);
            Key k3 = at(1.35f, Vector3Add(end, {0.04f, -0.02f, -0.02f}), 1);
            Key r;
            restPose(o, 0, 0, r);
            r.t = 1.9f;
            startTrack(o, 0, TK_Idle, {Key{}, k0, k1, k2, k3, r});
        }
    }
    // idle arm shuffles
    for (int a = 0; a < 2; ++a) {
        Arm& A = o.arm[a];
        A.idleIn -= dt;
        if (A.idleIn > 0.f || A.track.on) continue;
        A.idleIn = rng.f(4.f, 11.f);
        if ((o.kind == 0 && a == 1) || (o.kind == 1 && a == 0)) continue;  // tespih / cigarette hand
        if (o.chinRest && a == 0) continue;
        int variants[5] = {0, 1, 2, 3, 4};
        int v = variants[rng.i(a == 1 ? 5 : 4)];
        if (myTurn) v = rng.chance(0.6f) ? 1 : 0;
        Key r;
        restPose(o, a, v, r);
        r.t = rng.f(0.7f, 1.2f);
        r.lift = 0.02f;
        startTrack(o, a, TK_Idle, {Key{}, r});
    }
}

// ============================================================================ per-frame posing
void Cast::poseSeated(Seated& s, float dt, float headOmega) {
    const PersonLook& L = s.L;
    // auto lean/twist so the hands can reach their targets — for where a reach is going, too, so the body is
    // already leaning in when the hand gets there (a hand stuck at arm's length, then pulled along by a late
    // lean, reads as a jerk)
    float extraLean = 0.f, extraTwist = 0.f;
    for (int a = 0; a < 2; ++a) {
        const float sd = a == 0 ? 1.f : -1.f;
        const Vector3 sh = shoulderAt(L, sd, s.leanBase);
        const float reach = (L.upperArm + L.foreArm) * 0.86f;
        for (const Vector3& w : {s.arm[a].localWrist, s.arm[a].aheadWrist}) {
            extraLean = std::max(extraLean, leanNeeded(L, s.leanBase, sd, w));
            const float d = Vector3Distance(w, sh);
            if (d > reach && w.z < sh.z + 0.05f)
                extraTwist += 0.5f * clampf(-(w.x - sd * 0.1f) * 0.5f, -0.3f, 0.3f) * clampf((d - reach) * 6.f, 0.f, 1.f);
        }
    }
    float leanGoal = s.leanBase + s.leanAdd + extraLean;
    // straightening up after a long reach is quicker than bending into it (the hand coming back must not meet
    // a head still low over the rack)
    spring(s.lean, s.leanV, leanGoal, leanGoal < s.lean ? 8.f : 6.5f, dt);
    spring(s.twist, s.twistV, clampf(s.twistAdd + extraTwist + s.gazeTwist, -0.6f, 0.6f), 5.f, dt);
    spring(s.roll, s.rollV, s.rollAdd, 5.f, dt);
    spring(s.shrug, s.shrugV, s.shrugGoal, 9.f, dt);
    s.breath += dt * s.breathRate * 2.f * PI_F;
    float b = 0.5f + 0.5f * std::sin(s.breath);
    float bd = s.breathDepth;

    Matrix tl = mul(RY(s.twist), RX(-(s.lean + 0.012f * b * bd)), RZ(s.roll));
    Matrix torsoLocal = mul(S3(1.f + 0.006f * b * bd, 1.f + 0.002f * b, 1.f + 0.014f * b * bd), tl, T(L.hipPivot));
    s.torsoW = mul(torsoLocal, s.root);

    // head look-at (torso space)
    Matrix torsoInv = MatrixInvert(mul(tl, T(L.hipPivot), s.root));
    Vector3 tgt = xfPoint(torsoInv, s.gaze);
    Vector3 pivot{0, L.spineLen, L.headZ};
    Vector3 eyes = Vector3Add(pivot, {0, 0.10f, -0.07f});
    Vector3 d = Vector3Subtract(tgt, eyes);
    float yawNeed = std::atan2(-d.x, -d.z);
    float pitchNeed = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
    float yawGoal = clampf(yawNeed * 0.82f, -1.25f, 1.25f);
    float pitchGoal = clampf(pitchNeed * 0.72f, -0.8f, 0.5f);
    // something behind them (the TV, the çaycı): the shoulders turn too
    s.gazeTwist = approachExp(s.gazeTwist, clampf((std::fabs(yawNeed) - 1.1f) * 0.6f, 0.f, 0.45f) * (yawNeed < 0.f ? -1.f : 1.f), 3.f, dt);
    spring(s.hYaw, s.hYawV, yawGoal, headOmega, dt);
    spring(s.hPitch, s.hPitchV, pitchGoal, headOmega, dt);
    spring(s.hRoll, s.hRollV, clampf(-s.hYaw * 0.08f, -0.2f, 0.2f), headOmega * 0.7f, dt);
    Matrix headRot = mul(RZ(s.hRoll + s.nodRoll), RX(s.hPitch + s.nodPitch), RY(s.hYaw + s.nodYaw));
    Matrix headT = mul(headRot, T(pivot));
    s.headW = mul(headT, mul(tl, T(L.hipPivot)), s.root);
    s.headLocal = mul(headT, mul(tl, T(L.hipPivot)));
    s.torsoW = mul(torsoLocal, s.root);
}

void Cast::resolveArm(Seated& s, int a, float dt, float handScale) {
    Arm& A = s.arm[a];
    const PersonLook& L = s.L;
    const bool left = a == 1;
    const float sd = left ? -1.f : 1.f;
    auto resolve = [&](Key k) {
        if (k.headRel) {
            k.pos = xfPoint(s.headLocal, k.pos);
            k.fingers = vnorm(xfDir(s.headLocal, k.fingers));
            k.palm = vnorm(xfDir(s.headLocal, k.palm));
            k.headRel = false;
        }
        return k;
    };
    Key k;
    if (A.track.on) {
        Track tmp;
        tmp.v0 = A.track.v0;
        tmp.f0 = A.track.f0;
        tmp.p0 = A.track.p0;
        tmp.keys.reserve(A.track.keys.size());
        for (const Key& kk : A.track.keys) tmp.keys.push_back(resolve(kk));
        evalTrack(tmp, A.track.t, k);
        Key ahead;
        evalTrack(tmp, A.track.t + 0.45f, ahead);
        A.aheadWrist = ahead.pos;
    } else {
        k = resolve(A.hold);
        A.aheadWrist = k.pos;
    }
    // how the hand moves (a track started next frame carries this on)
    if (dt > 0.f) {
        if (A.velInit) {
            const float inv = 1.f / dt;
            A.vel = Vector3Scale(Vector3Subtract(k.pos, A.cur.pos), inv);
            A.fVel = Vector3Scale(Vector3Subtract(k.fingers, A.cur.fingers), inv);
            A.pVel = Vector3Scale(Vector3Subtract(k.palm, A.cur.palm), inv);
        }
        A.velInit = true;
    }
    A.cur = k;
    A.localWrist = k.pos;
    A.pose = k.pose;
    // shoulders (torso space), slightly raised by a shrug, rolled forward into a long reach
    Vector3 w = xfPoint(s.root, k.pos);
    {
        const Vector3 sh0 = xfPoint(s.torsoW, {sd * L.shoulderW, L.shoulderY, 0.012f});
        const float over = Vector3Distance(w, sh0) - (L.upperArm + L.foreArm) * kReachFrac;
        A.protract = dt > 0.f ? approachExp(A.protract, clampf(over / kProtract, 0.f, 1.f), 10.f, dt) : 0.f;
    }
    Vector3 shT{sd * L.shoulderW, L.shoulderY + 0.035f * s.shrug, 0.012f - kProtract * A.protract};
    A.shoulder = xfPoint(s.torsoW, shT);
    Vector3 poleL = vnorm({sd * (0.40f + k.elbowOut), -0.80f, 0.42f});
    float pw = clampf(Vector3Length(k.pole), 0.f, 1.f);
    if (pw > 0.01f) {
        Vector3 pp = vnorm(k.pole);
        poleL = vnorm(Vector3Add(Vector3Scale(poleL, 1.f - pw), Vector3Scale(pp, pw)));
    }
    Vector3 poleW = xfDir(s.root, poleL);
    const Vector3 w0 = w;
    A.elbow = solveTwoBone(A.shoulder, w, L.upperArm, L.foreArm, poleW);
    Vector3 desiredPole = poleW;
    Vector3 f = xfDir(s.root, k.fingers), p = xfDir(s.root, k.palm);
    const bool held = k.pose == HandPose::Grip || k.pose == HandPose::Hold || k.pose == HandPose::Cig;
    // (eased, so the elbow never pops when a track switches between holding and free poses)
    A.heldW = dt > 0.f ? approachExp(A.heldW, held ? 1.f : 0.f, 20.f, dt) : (held ? 1.f : 0.f);
    if (A.heldW > 0.01f) {
        // hands holding things keep their exact orientation, so the forearm follows the hand instead: the
        // elbow swings (around the shoulder-wrist line) toward where the forearm would continue the hand's
        // line, as much as the wrist is bent past a comfortable ~35°
        const float bend = std::acos(clampf(Vector3DotProduct(vnorm(f), vnorm(Vector3Subtract(w, A.elbow))), -1.f, 1.f));
        // (a key with its own elbow pole — the glass at the lips — already says where the elbow goes)
        const float wgt = A.heldW * clampf((bend - 35.f * DEG2RAD) / (45.f * DEG2RAD), 0.f, 0.8f) * (1.f - pw);
        if (wgt > 0.f) {
            const Vector3 ideal = Vector3Subtract(w, Vector3Scale(vnorm(f), L.foreArm));  // elbow on the hand's line
            const Vector3 axis = vnorm(Vector3Subtract(w, A.shoulder));
            Vector3 toIdeal = Vector3Subtract(ideal, A.shoulder);
            toIdeal = Vector3Subtract(toIdeal, Vector3Scale(axis, Vector3DotProduct(toIdeal, axis)));
            if (Vector3Length(toIdeal) > 1e-3f) {
                Vector3 pw = vnorm(Vector3Add(Vector3Scale(vnorm(poleW), 1.f - wgt), Vector3Scale(vnorm(toIdeal), wgt)));
                // ... but never up past the shoulder or back behind the body
                const Vector3 upW = xfDir(s.root, {0, 1, 0}), backW = xfDir(s.root, {0, 0, 1});
                if (Vector3DotProduct(pw, upW) > 0.2f) pw = vnorm(Vector3Subtract(pw, Vector3Scale(upW, Vector3DotProduct(pw, upW) - 0.2f)));
                if (Vector3DotProduct(pw, backW) > 0.6f) pw = vnorm(Vector3Subtract(pw, Vector3Scale(backW, Vector3DotProduct(pw, backW) - 0.6f)));
                desiredPole = pw;
            }
        }
    }
    // the elbow's swing plane follows its goal quickly but never in one frame (a new track, a glass tipping
    // up to the lips or a pole key would otherwise flip the elbow)
    if (dt <= 0.f || Vector3Length(A.pole) < 0.5f) A.pole = vnorm(desiredPole);
    else A.pole = vnorm(approachExp(A.pole, vnorm(desiredPole), 16.f, dt));
    w = w0;
    A.elbow = solveTwoBone(A.shoulder, w, L.upperArm, L.foreArm, A.pole);
    // With the hand close to the shoulder (a glass at the lips, a hand on the chin) the elbow's side of the
    // shoulder-wrist line can flip within a frame or two: it swings over at a human pace instead.
    {
        const Vector3 axis = vnorm(Vector3Subtract(w, A.shoulder));
        const float dist = Vector3Distance(w, A.shoulder);
        const float along = (L.upperArm * L.upperArm - L.foreArm * L.foreArm + dist * dist) / (2.f * std::max(dist, 1e-4f));
        const Vector3 foot = Vector3Add(A.shoulder, Vector3Scale(axis, along));
        const Vector3 off = Vector3Subtract(A.elbow, foot);
        const float hh = Vector3Length(off);
        if (hh > 1e-4f) {
            const Vector3 want = Vector3Scale(off, 1.f / hh);
            Vector3 prev = Vector3Subtract(A.swing, Vector3Scale(axis, Vector3DotProduct(A.swing, axis)));
            if (dt <= 0.f || Vector3Length(prev) < 0.2f) {
                A.swing = want;
            } else {
                prev = vnorm(prev);
                const float ang = std::acos(clampf(Vector3DotProduct(prev, want), -1.f, 1.f));
                // eased, and never faster than ~290 deg/s
                const float step = std::min(ang * (1.f - std::exp(-18.f * dt)), 5.f * dt);
                if (ang > 1e-4f) {
                    Vector3 side = Vector3Subtract(want, Vector3Scale(prev, std::cos(ang)));
                    if (Vector3Length(side) < 1e-5f) side = Vector3CrossProduct(axis, prev);  // exactly opposite
                    side = vnorm(side);
                    A.swing = vnorm(Vector3Add(Vector3Scale(prev, std::cos(step)), Vector3Scale(side, std::sin(step))));
                } else {
                    A.swing = want;
                }
            }
            A.elbow = Vector3Add(foot, Vector3Scale(A.swing, hh));
        }
    }
    A.wrist = w;
    Vector3 back = xfDir(s.root, {0, 0, 1});
    A.upper = boneMatrix(A.shoulder, A.elbow, back);
    A.fore = boneMatrix(A.elbow, A.wrist, back);
    // relaxed poses let the hand follow the forearm a little, and no wrist bends past what a wrist can do
    // (hands holding things, or placed exactly, keep their orientation)
    const Vector3 fd = vnorm(Vector3Subtract(A.wrist, A.elbow));
    // (eased too: switching between a pinch and a relaxed hand must not snap the wrist)
    const bool loose = k.pose == HandPose::Rest || k.pose == HandPose::Open || k.pose == HandPose::Fist;
    A.looseW = dt > 0.f ? approachExp(A.looseW, loose ? 1.f : 0.f, 12.f, dt) : (loose ? 1.f : 0.f);
    if (A.looseW > 0.001f) {
        Vector3 fdp = Vector3Subtract(fd, Vector3Scale(p, Vector3DotProduct(fd, p)));
        const float r = 0.38f * clampf(k.relax, 0.f, 1.f) * A.looseW;
        if (Vector3Length(fdp) > 0.1f && r > 0.f) f = vnorm(Vector3Add(Vector3Scale(f, 1.f - r), Vector3Scale(vnorm(fdp), r)));
    }
    if (A.heldW < 0.99f) {  // (a held hand is never clamped: it must match the glass, the cigarette, the tespih)
        const float maxBend = lerpf(lerpf(76.f, 62.f, clampf(k.relax, 0.f, 1.f)), 180.f, A.heldW) * DEG2RAD;
        float c = clampf(Vector3DotProduct(f, fd), -1.f, 1.f);
        float ang = std::acos(c);
        if (ang > maxBend) {
            Vector3 side = Vector3Subtract(f, Vector3Scale(fd, c));  // component of f across the forearm
            if (Vector3Length(side) > 1e-4f) {
                side = vnorm(side);
                f = vnorm(Vector3Add(Vector3Scale(fd, std::cos(maxBend)), Vector3Scale(side, std::sin(maxBend))));
            }
        }
    }
    A.hand = handMatrix(A.wrist, f, p, handScale);
    if (s.who >= 1 && s.who <= 3) {  // debug check (read by the harness): fingertips inside the own istaka
        for (const Vector3& t : handTips()[(size_t)k.pose]) {
            const Vector3 l = xfPoint(s.rootInv, xfPoint(A.hand, mirrorL(t, left)));
            if (insideRack(l, -0.002f)) ++rackPokes;
        }
    }
}

// ============================================================================ glasses, tespih
void Cast::updateGlasses(float dt) {
    for (int s = 0; s < 4; ++s) {
        TeaGlass& g = glass[s];
        Matrix restM = mul(RY(g.yaw), T(g.rest));
        Matrix target = restM;
        if (g.holder >= 1 && g.holder <= 3) target = mul(g.inHand, opp[g.holder].arm[g.holderArm].hand);
        else if (g.holder == 4) target = mul(g.inHand, boy.arm[g.holderArm].hand);
        else if (g.holder == 0) target = playerGlassW;  // the player's own hand (PlayerHands)
        if (g.blend < 1.f) {
            g.blend = std::min(1.f, g.blend + dt / 0.16f);
            g.world = blendMatrix(g.from, target, smoother01(g.blend));
        } else {
            g.world = target;
        }
    }
}

void Cast::emitSteam(float dt) {
    if (!renderer) return;
    for (int s = 0; s < 4; ++s) {
        TeaGlass& g = glass[s];
        if (g.level < 0.15f) continue;
        g.steamAcc += dt * (0.9f + 0.8f * g.level);
        if (g.steamAcc < 0.75f) continue;
        g.steamAcc = rng.f(0.f, 0.25f);
        float h = lerpf(0.0098f + 0.002f, 0.08f, g.level) * glassScale();
        Vector3 top = xfPoint(g.world, {rng.f(-0.006f, 0.006f), h + 0.004f, rng.f(-0.006f, 0.006f)});
        SmokeParams sp;
        sp.velocity = {rng.f(-0.004f, 0.004f), 0.03f, rng.f(-0.004f, 0.004f)};
        sp.size0 = 0.010f;
        sp.size1 = 0.055f;
        sp.life = 2.4f;
        sp.alpha = 0.06f;
        sp.color = Color{236, 234, 230, 255};
        sp.turbulence = 0.5f;
        sp.buoyancy = 0.012f;
        renderer->emitSmoke(top, sp);
    }
}

void Cast::updateTespih(Opponent& o, float dt) {
    Chain& c = o.tespih;
    const int N = 9;
    Matrix H = o.arm[1].hand;
    Vector3 grip = xfPoint(H, mirrorL(TESPIH_GRIP, true));
    Vector3 side = vnorm(xfDir(H, {1, 0, 0}));
    Vector3 endA = Vector3Add(grip, Vector3Scale(side, 0.006f));
    Vector3 endB = Vector3Subtract(grip, Vector3Scale(side, 0.006f));
    if (!c.init) {
        c.p.resize(N);
        c.prev.resize(N);
        for (int i = 0; i < N; ++i) {
            float t = (float)i / (N - 1);
            float sag = std::sin(t * PI_F) * 0.12f;
            c.p[i] = Vector3Add(Vector3Lerp(endA, endB, t), {0.01f * std::sin(t * PI_F), -sag, -0.02f * std::sin(t * PI_F)});
            c.prev[i] = c.p[i];
        }
        c.tail = {grip, Vector3Add(grip, {0, -0.045f, 0}), Vector3Add(grip, {0, -0.09f, 0})};
        c.tailPrev = c.tail;
        c.init = true;
    }
    const int sub = 3;
    const float h = std::min(dt, 0.05f) / sub;
    // what the beads rest on or slide off: the felt and the raised rim, his own saucer (and the glass standing
    // on it), his istaka, and his belly and chest (spheres in torso space)
    const float r = kBeadR;
    const TeaGlass& g = glass[o.seat];
    const Vector3 belly = xfPoint(o.torsoW, {0.f, 0.14f, -0.05f}), chest = xfPoint(o.torsoW, {0.f, 0.29f, -0.01f});
    auto collide = [&](Vector3& p) {
        const float ax = std::fabs(p.x), az = std::fabs(p.z);
        if (ax < w3d::FELT_HALF + w3d::RIM_W && az < w3d::FELT_HALF + w3d::RIM_W && p.y > TY - 0.05f) {
            float floorY = TY + r;
            if (ax > w3d::FELT_HALF || az > w3d::FELT_HALF) floorY = TY + w3d::RIM_H + r;
            const float rs = std::hypot(p.x - g.saucer.x, p.z - g.saucer.z);
            if (rs < 0.067f)  // the saucer: its well, then the rising rim
                floorY = std::max(floorY, TY + (rs < 0.054f ? 0.0068f : lerpf(0.0068f, 0.013f, (rs - 0.054f) / 0.013f)) + r);
            if (p.y < floorY) p.y = floorY;
        }
        if (g.holder < 0) {  // the glass on its saucer
            const Vector3 d{p.x - g.rest.x, 0.f, p.z - g.rest.z};
            const float rg = Vector3Length(d), lim = 0.027f + r;
            if (rg < lim && p.y > g.rest.y - r && p.y < g.rest.y + 0.105f) {
                const Vector3 n = rg > 1e-5f ? Vector3Scale(d, 1.f / rg) : Vector3{1, 0, 0};
                p = Vector3Add(p, Vector3Scale(n, lim - rg));
            }
        }
        {  // the istaka (a box around the plank, tiles and base board)
            Vector3 l = xfPoint(o.rootInv, p);
            const float hx = w3d::RACK_LEN * 0.5f + 0.005f + r, z0 = rackZ() - 0.035f - r, z1 = rackZ() + 0.030f + r,
                        top = TY + 0.113f + r;
            if (std::fabs(l.x) < hx && l.z > z0 && l.z < z1 && l.y < top && l.y > TY) {
                const float px = hx - std::fabs(l.x), pf = z1 - l.z, pb = l.z - z0, pu = top - l.y;
                const float m = std::min(std::min(px, pu), std::min(pf, pb));
                if (m == px) l.x = l.x < 0.f ? -hx : hx;
                else if (m == pu) l.y = top;
                else if (m == pf) l.z = z1;
                else l.z = z0;
                p = xfPoint(o.root, l);
            }
        }
        for (const auto& [c0, R] : {std::pair<Vector3, float>{belly, 0.125f}, std::pair<Vector3, float>{chest, 0.11f}}) {
            const Vector3 d = Vector3Subtract(p, c0);
            const float dl = Vector3Length(d);
            if (dl < R + r && dl > 1e-5f) p = Vector3Add(c0, Vector3Scale(d, (R + r) / dl));
        }
    };
    // beads never pass through each other: runs of the chain that aren't neighbours keep two radii apart
    auto separate = [&]() {
        for (int i = 0; i + 1 < N; ++i)
            for (int j = i + 2; j + 1 < N; ++j) {
                const Vector3 a0 = c.p[i], a1 = c.p[i + 1], b0 = c.p[j], b1 = c.p[j + 1];
                // closest points of the two segments (clamped parameters)
                const Vector3 u = Vector3Subtract(a1, a0), v = Vector3Subtract(b1, b0), w0 = Vector3Subtract(a0, b0);
                const float A = Vector3DotProduct(u, u), B = Vector3DotProduct(u, v), Cc = Vector3DotProduct(v, v);
                const float D = Vector3DotProduct(u, w0), E = Vector3DotProduct(v, w0), den = A * Cc - B * B;
                float sN = den > 1e-9f ? clampf((B * E - Cc * D) / den, 0.f, 1.f) : 0.f;
                float tN = Cc > 1e-9f ? clampf((B * sN + E) / Cc, 0.f, 1.f) : 0.f;
                sN = A > 1e-9f ? clampf((B * tN - D) / A, 0.f, 1.f) : 0.f;
                const Vector3 pa = Vector3Add(a0, Vector3Scale(u, sN)), pb = Vector3Add(b0, Vector3Scale(v, tN));
                const Vector3 d = Vector3Subtract(pa, pb);
                const float dl = Vector3Length(d);
                if (dl >= 2.f * r || dl < 1e-6f) continue;
                const Vector3 push = Vector3Scale(d, 0.5f * (2.f * r - dl) / dl);
                // move the free end points of each run (the two held at the hand stay put)
                auto mv = [&](int k, Vector3 by, float wgt) {
                    if (k > 0 && k < N - 1) c.p[k] = Vector3Add(c.p[k], Vector3Scale(by, wgt));
                };
                mv(i, push, 1.f - sN);
                mv(i + 1, push, sN);
                mv(j, Vector3Negate(push), 1.f - tN);
                mv(j + 1, Vector3Negate(push), tN);
            }
    };
    for (int st = 0; st < sub; ++st) {
        for (int i = 1; i < N - 1; ++i) {
            Vector3 v = Vector3Scale(Vector3Subtract(c.p[i], c.prev[i]), 0.985f);
            c.prev[i] = c.p[i];
            c.p[i] = Vector3Add(Vector3Add(c.p[i], v), {0, -9.81f * h * h, 0});
        }
        for (int i = 1; i < 3; ++i) {  // the imame and the tassel: heavier damping, they hang rather than fly
            Vector3 v = Vector3Scale(Vector3Subtract(c.tail[i], c.tailPrev[i]), 0.93f);
            c.tailPrev[i] = c.tail[i];
            c.tail[i] = Vector3Add(Vector3Add(c.tail[i], v), {0, -9.81f * h * h, 0});
        }
        c.p[0] = endA;
        c.p[N - 1] = endB;
        c.tail[0] = grip;
        for (int it = 0; it < 6; ++it) {
            for (int i = 0; i + 1 < N; ++i) {
                Vector3 d = Vector3Subtract(c.p[i + 1], c.p[i]);
                float len = Vector3Length(d);
                if (len < 1e-6f) continue;
                float diff = (len - c.seg) / len;
                Vector3 corr = Vector3Scale(d, 0.5f * diff);
                if (i > 0) c.p[i] = Vector3Add(c.p[i], corr);
                if (i + 1 < N - 1) c.p[i + 1] = Vector3Subtract(c.p[i + 1], corr);
                if (i == 0) c.p[i + 1] = Vector3Subtract(c.p[i + 1], corr);
                if (i + 1 == N - 1) c.p[i] = Vector3Add(c.p[i], corr);
            }
            const float tl[2] = {0.042f, 0.044f};
            for (int i = 0; i < 2; ++i) {
                Vector3 d = Vector3Subtract(c.tail[i + 1], c.tail[i]);
                float len = Vector3Length(d);
                if (len < 1e-6f) continue;
                Vector3 corr = Vector3Scale(d, (len - tl[i]) / len);
                c.tail[i + 1] = Vector3Subtract(c.tail[i + 1], corr);
            }
            {  // the tassel bends at most ~40° off the imame
                const Vector3 a = vnorm(Vector3Subtract(c.tail[1], c.tail[0]));
                Vector3 b = vnorm(Vector3Subtract(c.tail[2], c.tail[1]));
                const float maxBend = 40.f * DEG2RAD, cb = Vector3DotProduct(a, b);
                if (cb < std::cos(maxBend)) {
                    Vector3 side = Vector3Subtract(b, Vector3Scale(a, cb));
                    side = Vector3Length(side) > 1e-5f ? vnorm(side) : Vector3{0, -1, 0};
                    b = Vector3Add(Vector3Scale(a, std::cos(maxBend)), Vector3Scale(side, std::sin(maxBend)));
                    c.tail[2] = Vector3Add(c.tail[1], Vector3Scale(b, tl[1]));
                }
            }
            separate();
            for (int i = 1; i < N - 1; ++i) collide(c.p[i]);
            collide(c.tail[1]);
            collide(c.tail[2]);
            c.p[0] = endA;
            c.p[N - 1] = endB;
        }
    }
}

// ============================================================================ the opponent frame
void Cast::updateOpponent(Opponent& o, float dt) {
    idleOpponent(o, dt);

    // gestures: body channel offsets, on top of a slowly wandering posture and a living head (never
    // perfectly still: tiny drifts of the head, now and then a new way of sitting)
    o.postureIn -= dt;
    if (o.postureIn <= 0.f) {
        o.postureIn = rng.f(9.f, 24.f);
        o.postureGoal = rng.f(-0.05f, 0.05f);
    }
    o.posture = approachExp(o.posture, o.postureGoal, 0.8f, dt);
    o.leanAdd = o.posture;
    o.twistAdd = 0.f;
    o.rollAdd = 0.f;
    o.shrugGoal = 0.f;
    const float nt = time + (float)(o.seed % 997u);
    o.nodYaw = 0.05f * (noise1(nt * 0.21f, o.seed) - 0.5f);
    o.nodPitch = 0.04f * (noise1(nt * 0.17f, o.seed + 7u) - 0.5f);
    o.nodRoll = 0.035f * (noise1(nt * 0.13f, o.seed + 13u) - 0.5f);
    float jawGest = 0.f;
    if (o.gest != G_None) {
        o.gestT += dt;
        float t = o.gestT, D = o.gestDur;
        float env = smooth01(t / 0.3f) * smooth01((D - t) / 0.4f);
        switch (o.gest) {
        case G_Proud:
            o.leanAdd = -0.16f * env;
            o.nodPitch += 0.12f * env;
            o.shrugGoal = 0.3f * env;
            break;
        case G_Celebrate:
            o.leanAdd = (o.kind == 1 ? -0.2f : -0.08f) * env;
            o.nodPitch += (o.kind == 0 ? 0.35f : 0.18f) * env + 0.05f * std::sin(t * 12.f) * env * (o.kind == 1);
            o.rollAdd = 0.06f * std::sin(t * 7.f) * env * (o.kind == 1);
            jawGest = o.kind == 1 ? (0.5f + 0.4f * std::sin(t * 9.f)) * env : 0.f;
            break;
        case G_Laugh:
            o.leanAdd = -0.10f * env;
            o.nodPitch += (0.18f + 0.06f * std::sin(t * 16.f)) * env;
            o.shrugGoal = (0.4f + 0.4f * std::sin(t * 16.f)) * env;
            jawGest = (0.45f + 0.35f * std::sin(t * 16.f)) * env;
            break;
        case G_Facepalm:
            o.nodPitch += -0.35f * env;
            o.nodYaw += 0.12f * std::sin(t * 5.f) * env;
            o.leanAdd = 0.08f * env;
            break;
        case G_Shrug:
            o.shrugGoal = env;
            o.nodRoll += 0.12f * env;
            break;
        case G_Nod: o.nodPitch += -0.14f * std::sin(t * 13.f) * env; break;
        case G_HeadShake:
            o.nodYaw += 0.22f * std::sin(t * 12.f) * env;
            o.nodPitch += -0.08f * env;
            break;
        case G_Drink: {
            float drink = smooth01((t - 1.5f) / 0.3f) * smooth01((2.6f - t) / 0.3f);
            o.nodPitch += 0.16f * drink;
            // face the glass while it comes up and while drinking (no glancing around mid-sip)
            if (t > 0.9f && t < 2.6f) {
                o.gazeGoal = xfPoint(o.root, {0.f, 1.22f, -1.0f});
                o.gazeHold = std::max(o.gazeHold, 0.3f);
            }
            break;
        }
        case G_TvCheer:
            o.leanAdd = -0.12f * env;
            o.nodPitch += 0.1f * env;
            jawGest = 0.7f * env;
            break;
        case G_Clap: o.nodPitch += -0.05f * std::sin(t * 18.f) * env; break;
        case G_Point:
            o.leanAdd = -0.06f * env;
            o.nodPitch += (0.12f + 0.05f * std::sin(t * 15.f)) * env;
            o.shrugGoal = 0.3f * (0.5f + 0.5f * std::sin(t * 15.f)) * env;
            jawGest = (0.4f + 0.35f * std::sin(t * 15.f)) * env;
            break;
        case G_Disbelief:
            o.leanAdd = -0.18f * env;
            o.nodPitch += 0.2f * env;
            jawGest = 0.35f * env;
            break;
        case G_Grumble:
            o.leanAdd = -0.1f * env;
            o.nodYaw += 0.1f * std::sin(t * 6.f) * env;
            break;
        default: break;
        }
        if (o.gestT >= o.gestDur) o.gest = G_None;
    }
    // thinking: lean in a little, head tilted
    o.leanAdd += 0.07f * o.think;
    o.nodRoll += 0.06f * o.think * (o.kind == 0 ? 1.f : -1.f);

    // arm tracks + key events
    for (int a = 0; a < 2; ++a) {
        Arm& A = o.arm[a];
        if (!A.track.on) continue;
        A.track.t += (A.track.kind == TK_Reach || A.track.kind == TK_CardHold) ? dt * animSpeed : dt;
        while (A.track.nextKey < (int)A.track.keys.size() && A.track.keys[A.track.nextKey].t <= A.track.t) {
            int ev = A.track.keys[A.track.nextKey].event;
            if (ev) onArmEvent(o, a, ev);
            ++A.track.nextKey;
        }
        if (A.track.t >= A.track.duration()) {
            A.track.on = false;
            A.hold = A.track.keys.back();
            A.hold.t = 0.f;
            A.hold.event = 0;
        }
    }

    if (o.cards) updateCardHold(o);
    // gaze follows its goal (quick but not instant); the head springs follow the gaze
    o.gaze = approachExp(o.gaze, o.gazeGoal, 9.f, dt);
    faceBody(o, dt);  // (Yüz) the head goes with the expression and the syllables
    poseSeated(o, dt, o.headStiff);
    for (int a = 0; a < 2; ++a) resolveArm(o, a, dt, o.L.handScale);

    // ---- face: expressions, blinks, eyes, the talking mouth, the mustache (CharactersFace.cpp)
    updateFace(o, dt, jawGest);

    // Mahmut's cigarette
    if (o.kind == 1) {
        const float inv = 1.f / o.L.handScale;
        Vector3 filt = Vector3Add(CIG_HOLD, {0, -0.020f, 0});
        Matrix cigInHand = basisMatrix({inv, 0, 0}, {0, inv, 0}, {0, 0, inv}, filt);
        o.cigW = mul(cigInHand, o.arm[0].hand);
        o.drag = std::max(0.f, o.drag - dt * 0.9f);
        if (renderer) {
            o.cigSmokeAcc += dt;
            if (o.cigSmokeAcc > 0.16f) {
                o.cigSmokeAcc = rng.f(0.f, 0.05f);
                SmokeParams sp;
                sp.velocity = {rng.f(-0.004f, 0.004f), 0.05f, rng.f(-0.004f, 0.004f)};
                sp.size0 = 0.006f;
                sp.size1 = 0.07f;
                sp.life = 3.6f;
                sp.alpha = 0.16f;
                sp.color = Color{206, 204, 198, 255};
                sp.turbulence = 1.1f;
                sp.buoyancy = 0.02f;
                renderer->emitSmoke(xfPoint(o.cigW, {0, 0.076f, 0}), sp);
            }
            if (o.exhaleT >= 0.f) {
                o.exhaleT += dt;
                if (o.exhaleT < 1.0f && rng.chance(dt * 26.f)) {
                    Vector3 mouth = xfPoint(o.headW, Vector3Add(o.pm->face.mouth, {0, -0.004f, -0.02f}));
                    Vector3 fwd = vnorm(xfDir(o.headW, {0.05f, -0.25f, -1.f}));
                    SmokeParams sp;
                    sp.velocity = Vector3Scale(fwd, rng.f(0.16f, 0.28f) * (1.f - o.exhaleT * 0.6f));
                    sp.size0 = 0.02f;
                    sp.size1 = 0.20f;
                    sp.life = 3.2f;
                    sp.alpha = 0.22f;
                    sp.color = Color{214, 212, 206, 255};
                    sp.turbulence = 1.3f;
                    sp.buoyancy = 0.025f;
                    renderer->emitSmoke(mouth, sp);
                }
                if (o.exhaleT > 1.2f) o.exhaleT = -1.f;
            }
        }
    }
    if (o.kind == 0) updateTespih(o, dt);
}

} // namespace chr
} // namespace r3d
