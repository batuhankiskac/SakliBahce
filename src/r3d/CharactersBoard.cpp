// Characters module (Rakip / round 5): a regular's hand on the board — a dama disc taken between thumb and fingers,
// carried along its way (hopping over the discs it takes) and put down, the taken discs lifted off one by one and
// dropped on his side; a loose card carried the same way (from the declared row onto the trick, the koz turned over);
// and a card drawn off the stock into the held fan. Times are at animation speed 1 (the tracks run at Cast::animSpeed,
// like the tile reaches), on the same clock as what is carried (r3d/PieceCarry.h).
#include "r3d/CharactersState.h"

#include <algorithm>

namespace r3d {
namespace chr {

namespace {

constexpr float TY = w3d::TABLE_Y;

// As far as the hand can go toward `tL` (character-local) with the body leaning in (as CharactersCards' clampReach).
Vector3 reachable(const Opponent& o, int arm, Vector3 tL, Vector3 f, Vector3 p, Vector3 off) {
    const bool left = arm == 1;
    const float sd = left ? -1.f : 1.f;
    const Vector3 shMax = shoulderAt(o.L, sd, o.leanBase + kMaxExtraLean);
    const float reach = (o.L.upperArm + o.L.foreArm) * kReachFrac + kProtract - 0.01f;
    for (int it = 0; it < 12; ++it) {
        const float d = Vector3Distance(wristFor(tL, f, p, off, o.L.handScale, left), shMax);
        if (d <= reach) break;
        const Vector3 back = vnorm({shMax.x - tL.x, 0.f, shMax.z - tL.z});
        tL = Vector3Add(tL, Vector3Scale(back, d - reach + 0.005f));
    }
    return tL;
}

// The pinch over a point of the table (character-local): fingers forward and down onto it, the palm facing down.
struct Grip {
    Vector3 f, p;
};
Grip gripOver(Vector3 tL, int arm, bool card) {
    const float sd = arm == 1 ? -1.f : 1.f;
    const Vector3 flat = vnorm({tL.x - sd * 0.12f, 0.f, tL.z + 0.05f});
    Grip g;
    g.f = vnorm(Vector3Add(Vector3Scale(flat, card ? 0.8f : 0.55f), {0.f, card ? -0.6f : -0.85f, 0.f}));
    g.p = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(g.f, -g.f.y)));
    return g;
}

} // namespace

void Cast::carryPiece(Opponent& o, const PieceCarry& c) {
    if (c.path.empty()) return;
    const float hs = o.L.handScale;
    // the hand on the piece's side (no arm across the chest over the board), unless that hand has the tea glass or the
    // cigarette at the mouth just now
    const Vector3 l0 = xfPoint(o.rootInv, c.path[0]);
    int a = l0.x >= 0.f ? 0 : 1;
    if (o.arm[a].track.on && (o.arm[a].track.kind == TK_Sip || o.arm[a].track.kind == TK_Smoke)) a = 1 - a;
    if (o.cards) a = 0; // the left hand holds the cards
    const bool left = a == 1;
    // the wrist that puts the pinch on world point w (raised by `up`)
    auto wrist = [&](Vector3 w, float up, Vector3& f, Vector3& p) {
        Vector3 tL = Vector3Add(xfPoint(o.rootInv, w), {0.f, up, 0.f});
        const Grip g = gripOver(tL, a, c.card);
        f = g.f;
        p = g.p;
        tL = reachable(o, a, tL, f, p, PINCH_POINT);
        return wristFor(tL, f, p, PINCH_POINT, hs, left);
    };
    Vector3 f, p;
    std::vector<Key> k;
    k.push_back(Key{});
    const float L = std::max(0.2f, c.lead);
    const float pinchUp = c.card ? 0.003f : 0.002f;
    Vector3 w = wrist(c.path[0], 0.05f, f, p);
    k.push_back(mk(std::max(0.1f, L - 0.15f), w, f, p, HandPose::Pinch, 0.02f));
    w = wrist(c.path[0], pinchUp, f, p);
    k.push_back(mk(L, w, f, p, HandPose::Pinch, 0.f, 1));
    // along the way: over each segment's top, down at every landing (it stops only at the last)
    const size_t n = c.path.size();
    for (size_t i = 1; i < n; ++i) {
        const float t0 = L + c.seg * (float)(i - 1);
        const Vector3 mid = Vector3Lerp(c.path[i - 1], c.path[i], 0.5f);
        w = wrist(mid, pinchUp + c.hop, f, p);
        k.push_back(mk(t0 + c.seg * 0.5f, w, f, p, HandPose::Pinch));
        w = wrist(c.path[i], pinchUp, f, p);
        k.push_back(mk(t0 + c.seg, w, f, p, HandPose::Pinch, 0.f, i + 1 == n ? 1 : 0));
    }
    float t = L + c.seg * (float)(n - 1);
    // let go: the fingers open and the hand comes up off it
    w = wrist(c.path[n - 1], 0.04f, f, p);
    k.push_back(mk(t + 0.14f, w, f, p, HandPose::Open));
    t += 0.14f;
    // the taken ones: each lifted off and dropped on the own side
    for (const PieceCarry::Take& tk : c.takes) {
        const float over = std::max(t + 0.08f, tk.at - 0.16f);
        w = wrist(tk.from, 0.05f, f, p);
        k.push_back(mk(over, w, f, p, HandPose::Pinch, 0.03f));
        const float at = std::max(over + 0.08f, tk.at);
        w = wrist(tk.from, pinchUp, f, p);
        k.push_back(mk(at, w, f, p, HandPose::Pinch, 0.f, 1));
        w = wrist(Vector3Lerp(tk.from, tk.to, 0.5f), pinchUp + 0.06f, f, p);
        k.push_back(mk(at + tk.dur * 0.5f, w, f, p, HandPose::Pinch));
        w = wrist(tk.to, pinchUp, f, p);
        k.push_back(mk(at + tk.dur, w, f, p, HandPose::Pinch, 0.f, 1));
        w = wrist(tk.to, 0.035f, f, p);
        k.push_back(mk(at + tk.dur + 0.12f, w, f, p, HandPose::Open));
        t = at + tk.dur + 0.12f;
    }
    Key rest;
    restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), rest);
    rest.t = t + 0.55f;
    k.push_back(rest);
    startTrack(o, a, TK_Reach, k);
    o.gazeGoal = c.path[0];
    o.gaze = Vector3Lerp(o.gaze, c.path[0], 0.35f);
    o.gazeHold = t;
}

void Cast::drawIntoFan(Opponent& o, Vector3 from, float seconds) {
    Matrix fan;
    if (!cardFan(o, fan)) { // no fan: the card comes to the hand's side of the table edge
        PieceCarry c;
        c.card = true;
        c.lead = w3d::BOT_TAKE_LEAD;
        c.seg = seconds;
        c.hop = 0.05f;
        c.path = {from, xfPoint(o.root, {0.14f, TY, -0.30f})};
        carryPiece(o, c);
        return;
    }
    const int a = 0;
    const float hs = o.L.handScale;
    constexpr float TAKE = w3d::BOT_TAKE_LEAD;
    Vector3 tL = xfPoint(o.rootInv, from);
    const Grip g = gripOver(tL, a, true);
    tL = reachable(o, a, Vector3Add(tL, {0.f, 0.003f, 0.f}), g.f, g.p, PINCH_POINT);
    const Vector3 at = wristFor(tL, g.f, g.p, PINCH_POINT, hs, false);
    const Vector3 over = Vector3Add(at, {0.f, 0.05f, 0.f});
    // into the fan from above on its right (where playCard pulls one out), palm down, the elbow low by the side
    const Vector3 top = xfPoint(o.rootInv, xfPoint(fan, {0.026f, 0.002f, -0.074f}));
    const Vector3 fp = vnorm({-0.42f, -0.42f, -0.80f});
    const Vector3 pp = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(fp, -fp.y)));
    const Vector3 in = wristFor(top, fp, pp, PINCH_POINT, hs, false);
    const Vector3 above = Vector3Add(in, {0.01f, 0.06f, 0.f});
    seconds = std::max(0.25f, seconds);
    std::vector<Key> k;
    k.push_back(Key{});
    k.push_back(mk(TAKE - 0.13f, over, g.f, g.p, HandPose::Pinch, 0.02f));
    k.push_back(mk(TAKE, at, g.f, g.p, HandPose::Pinch, 0.f, 1));
    k.push_back(mk(TAKE + 0.12f, Vector3Add(at, {0.f, 0.045f, 0.f}), g.f, g.p, HandPose::Pinch));
    Key kAbove = mk(TAKE + seconds * 0.75f, above, fp, pp, HandPose::Pinch, 0.03f);
    Key kIn = mk(TAKE + seconds, in, fp, pp, HandPose::Pinch, 0.f, 1);
    Key kOut = mk(TAKE + seconds + 0.14f, Vector3Add(in, {0.02f, 0.03f, 0.02f}), fp, pp, HandPose::Rest);
    kAbove.pole = kIn.pole = kOut.pole = vnorm({0.45f, -0.85f, 0.25f}); // across the own body: the elbow stays down
    k.push_back(kAbove);
    k.push_back(kIn);
    k.push_back(kOut);
    Key rest;
    restPose(o, a, o.kind == 1 ? 11 : 0, rest);
    rest.t = TAKE + seconds + 0.65f;
    k.push_back(rest);
    startTrack(o, a, TK_Reach, k);
    o.gazeGoal = from;
    o.gaze = Vector3Lerp(o.gaze, from, 0.35f);
    o.gazeHold = TAKE + 0.3f;
}

} // namespace chr

// ---------------------------------------------------------------- public API
void Characters::carry(int seat, const PieceCarry& c) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.carryPiece(m.opp[seat], c);
    m.opp[seat].chinRest = false;
}

void Characters::drawCard(int seat, Vector3 from, float seconds) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.drawIntoFan(m.opp[seat], from, seconds);
    m.opp[seat].chinRest = false;
}

} // namespace r3d
