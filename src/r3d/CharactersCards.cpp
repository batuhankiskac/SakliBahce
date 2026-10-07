// Characters module: the regulars at a card game — the hand held fanned in the left hand (the fan follows the hand),
// the right hand pulling a card out of it and laying it on the felt, a trick gathered and pushed to the own pile,
// picking up the dealt cards, and the dealer's riffle shuffle and dealing. Times are at animation speed 1 (the tracks
// run at Cast::animSpeed, like the tile reaches).
#include "r3d/CharactersState.h"
#include "r3d/Cards3D.h"

#include <algorithm>

namespace r3d {
namespace chr {

namespace {

constexpr float TY = w3d::TABLE_Y;

// The fan held in the left hand (character-local, the holder looks along -z): the palm a hand's breadth above the
// table edge, left of the chest; the cards' faces turned up toward the eyes.
constexpr Vector3 kFanPalm{-0.085f, TY + 0.090f, -0.365f};
constexpr Vector3 kFanFace{0.17f, 0.48f, 0.86f};

struct FanAxes {
    Vector3 n, u, r;  // faces' normal (toward the holder), up the cards, the holder's right across them
};
FanAxes fanAxes() {
    FanAxes a;
    a.n = vnorm(kFanFace);
    a.u = vnorm(Vector3Subtract({0, 1, 0}, Vector3Scale(a.n, a.n.y)));
    a.r = vnorm(Vector3CrossProduct(a.u, a.n));
    return a;
}

// Palm down over the felt, fingers pointing along `dir` (flat), slightly cupped.
void palmDown(Vector3 dir, Vector3& f, Vector3& p) {
    f = vnorm({dir.x, -0.18f, dir.z});
    p = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(f, -f.y)));
}

} // namespace

Key Cast::cardHoldKey(const Opponent& o, Matrix& fanRel) const {
    const float hs = o.L.handScale;
    const FanAxes ax = fanAxes();
    // a book held by its spine: fingers up behind the cards (toward the table), the palm against their left edge,
    // the thumb on the faces at the pivot
    Vector3 f = vnorm(Vector3Add(Vector3Add(Vector3Scale(ax.u, 0.62f), Vector3Scale(ax.r, 0.22f)), Vector3Scale(ax.n, -0.58f)));
    Vector3 p = vnorm(Vector3Add(Vector3Scale(ax.r, 0.80f), Vector3Scale(ax.n, 0.42f)));
    p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
    Key k = mk(0.f, wristFor(kFanPalm, f, p, PALM_CENTER, hs, true), f, p, HandPose::Hold);
    k.relax = 0.f;
    k.pole = vnorm({-0.55f, -0.80f, 0.25f}); // the elbow hangs down by the side
    const Vector3 pivot = Vector3Add(Vector3Add(kFanPalm, Vector3Scale(ax.r, 0.030f)), Vector3Scale(ax.u, 0.004f));
    // card space: x across (the holder's right), y the faces (toward the holder), z down the cards
    const Matrix F = basisMatrix(ax.r, ax.n, Vector3Negate(ax.u), pivot);
    const Matrix H = handMatrix(k.pos, k.fingers, k.palm, hs);
    fanRel = MatrixMultiply(F, MatrixInvert(H));
    return k;
}

void Cast::holdCards(Opponent& o, bool on, const Vector3* pickUpAt) {
    if (on == o.cards) return;
    const float hs = o.L.handScale;
    if (!on) {
        o.cards = false;
        Key rest;
        restPose(o, 1, o.kind == 0 ? 10 : 0, rest);
        rest.t = 0.6f;
        startTrack(o, 1, TK_CardHold, {Key{}, rest});
        return;
    }
    o.cards = true;
    o.fanKey = cardHoldKey(o, o.fanRel);
    Key hold = o.fanKey;
    std::vector<Key> k;
    k.push_back(Key{});
    if (pickUpAt) {
        // down onto the little pile dealt in front of them, the fingers sliding under its edge, and up into the fan
        Vector3 tL = xfPoint(o.rootInv, *pickUpAt);
        Vector3 f, p;
        palmDown({tL.x + 0.10f, 0.f, tL.z + 0.02f}, f, p);
        f = vnorm({f.x, -0.55f, f.z});
        p = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(f, -f.y)));
        tL = clampReach(o, 1, Vector3Add(tL, {0, 0.012f, 0}), f, p, PINCH_POINT);
        const Vector3 at = wristFor(tL, f, p, PINCH_POINT, hs, true);
        k.push_back(mk(0.22f, Vector3Add(at, {0, 0.05f, 0}), f, p, HandPose::Pinch, 0.03f));
        k.push_back(mk(0.34f, at, f, p, HandPose::Pinch, 0.f, 1));
        k.push_back(mk(0.48f, Vector3Add(at, {0, 0.06f, 0.03f}), f, p, HandPose::Pinch));
        hold.t = 0.90f;
        o.gazeGoal = *pickUpAt;
        o.gazeHold = 0.6f;
    } else {
        hold.t = 0.6f;
    }
    k.push_back(hold);
    startTrack(o, 1, TK_CardHold, k);
}

// Leaning in over the table (thinking, a long reach of the other hand) the fan goes along about half way, so the head
// never comes down onto the cards; the hand only moves (the fan keeps its place in it).
void Cast::updateCardHold(Opponent& o) {
    Arm& A = o.arm[1];
    if (A.track.on) return;
    const Vector3 d = Vector3Subtract(shoulderAt(o.L, -1.f, o.lean), shoulderAt(o.L, -1.f, o.leanBase));
    A.hold = o.fanKey;
    A.hold.pos = Vector3Add(o.fanKey.pos, {0.f, d.y * 0.5f, d.z * 0.6f});
}

bool Cast::cardFan(const Opponent& o, Matrix& frame) const {
    if (!o.cards) return false;
    frame = MatrixMultiply(o.fanRel, o.arm[1].hand);
    return true;
}

void Cast::playCardFromFan(Opponent& o, Vector3 world, bool toss) {
    const int a = 0;
    const float hs = o.L.handScale;
    const FanAxes ax = fanAxes();
    Key rest;
    restPose(o, a, o.kind == 1 ? 11 : 0, rest);
    // a card's top edge on the right of the fan, pinched from above (palm down, fingers forward and to the left: the
    // elbow stays low by the side), then drawn up out of the fan
    Vector3 top = Vector3Add(kFanPalm, Vector3Add(Vector3Scale(ax.u, 0.080f), Vector3Scale(ax.r, 0.050f)));
    Matrix fan;
    if (cardFan(o, fan)) top = xfPoint(o.rootInv, xfPoint(fan, {0.026f, 0.002f, -0.074f}));
    const Vector3 fp = vnorm({-0.42f, -0.42f, -0.80f});
    const Vector3 pp = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(fp, -fp.y)));
    const Vector3 pick = wristFor(top, fp, pp, PINCH_POINT, hs, false);
    const Vector3 pulled = Vector3Add(pick, {0.01f, 0.06f, 0.f});
    // out over the felt and down
    Vector3 tL = xfPoint(o.rootInv, world);
    const Vector3 flat = vnorm({tL.x - 0.12f, 0, tL.z + 0.05f});
    const Vector3 f = vnorm(Vector3Add(Vector3Scale(flat, 0.8f), {0, -0.6f, 0}));
    const Vector3 p = vnorm(Vector3Subtract({0, -1, 0}, Vector3Scale(f, -f.y)));
    tL = clampReach(o, a, Vector3Add(tL, {0, 0.006f, 0}), f, p, PINCH_POINT);
    const Vector3 at = wristFor(tL, f, p, PINCH_POINT, hs, false);
    const Vector3 over = Vector3Add(at, {0, toss ? 0.10f : 0.05f, 0});
    constexpr float GIVE = w3d::BOT_GIVE_LEAD, FLY = w3d::BOT_TILE_FLIGHT;
    std::vector<Key> k;
    k.push_back(Key{});
    Key kPick = mk(GIVE - 0.03f, pick, fp, pp, HandPose::Pinch, 0.02f, 1);
    Key kPulled = mk(GIVE + 0.10f, pulled, fp, pp, HandPose::Pinch);
    kPick.pole = kPulled.pole = vnorm({0.45f, -0.85f, 0.25f}); // across the own body: the elbow stays down
    k.push_back(kPick);
    k.push_back(kPulled);
    if (toss) {
        k.push_back(mk(GIVE + FLY * 0.55f, over, f, p, HandPose::Pinch, 0.03f));
        k.push_back(mk(GIVE + FLY * 0.75f, Vector3Add(over, {0, 0.01f, -0.03f}), f, p, HandPose::Open, 0.f, 2));
        k.push_back(mk(GIVE + FLY + 0.15f, Vector3Add(over, {0, 0.02f, 0.03f}), f, p, HandPose::Rest));
    } else {
        k.push_back(mk(GIVE + 0.34f, over, f, p, HandPose::Pinch, 0.03f));
        k.push_back(mk(GIVE + FLY, at, f, p, HandPose::Pinch, 0.f, 1));
        k.push_back(mk(GIVE + FLY + 0.14f, Vector3Add(at, {0, 0.035f, 0.02f}), f, p, HandPose::Rest));
    }
    rest.t = GIVE + FLY + 0.6f;
    k.push_back(rest);
    startTrack(o, a, TK_Reach, k);
    o.gazeGoal = world;
    o.gaze = Vector3Lerp(o.gaze, world, 0.35f);
    o.gazeHold = GIVE + FLY + 0.4f;
}

void Cast::gatherCards(Opponent& o, Vector3 from, Vector3 to) {
    const float hs = o.L.handScale;
    Vector3 fL = xfPoint(o.rootInv, from), tL = xfPoint(o.rootInv, to);
    int a = fL.x >= 0.f ? 0 : 1;
    if (o.cards || (o.arm[1].track.on && o.arm[1].track.kind == TK_Sip)) a = 0;
    const bool left = a == 1;
    // fingers point away from the holder (toward where the cards are pushed from), palm down on them
    Vector3 f, p;
    palmDown({fL.x - tL.x, 0.f, fL.z - tL.z + 0.001f}, f, p);
    fL = clampReach(o, a, Vector3Add(fL, {0, 0.020f, 0}), f, p, PALM_CENTER);
    tL = clampReach(o, a, Vector3Add(tL, {0, 0.020f, 0}), f, p, PALM_CENTER);
    const Vector3 down = wristFor(fL, f, p, PALM_CENTER, hs, left);
    const Vector3 there = wristFor(tL, f, p, PALM_CENTER, hs, left);
    Key rest;
    restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), rest);
    std::vector<Key> k;
    k.push_back(Key{});
    k.push_back(mk(0.30f, Vector3Add(down, {0, 0.06f, 0}), f, p, HandPose::Open, 0.04f));
    k.push_back(mk(0.45f, down, f, p, HandPose::Open, 0.f, 1));
    k.push_back(mk(0.80f, there, f, p, HandPose::Open, 0.f, 1));
    k.push_back(mk(0.95f, Vector3Add(there, {0, 0.05f, 0.02f}), f, p, HandPose::Rest));
    rest.t = 1.45f;
    k.push_back(rest);
    startTrack(o, a, TK_Reach, k);
    o.gazeGoal = from;
    o.gazeHold = 0.9f;
}

void Cast::shuffleDeck(Opponent& o, Vector3 at, float seconds) {
    const float hs = o.L.handScale;
    const Vector3 c = xfPoint(o.rootInv, at);
    seconds = std::max(seconds, 0.8f);
    for (int a = 0; a < 2; ++a) {
        const bool left = a == 1;
        const float sd = left ? -1.f : 1.f;
        // each hand holds a half by its outer end, thumbs on top; the halves bend and riffle into each other
        const Vector3 f = vnorm({-sd * 0.85f, -0.25f, -0.45f});
        Vector3 p = vnorm({-sd * 0.3f, -0.9f, 0.f});
        p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
        Vector3 side = clampReach(o, a, {c.x + sd * 0.075f, c.y + 0.03f, c.z}, f, p, PINCH_POINT);
        const Vector3 w = wristFor(side, f, p, PINCH_POINT, hs, left);
        Key rest;
        restPose(o, a, (o.kind == 1 && a == 0) ? 11 : (o.kind == 0 && a == 1 ? 10 : 0), rest);
        std::vector<Key> k;
        k.push_back(Key{});
        k.push_back(mk(0.30f, w, f, p, HandPose::Pinch, 0.03f, 1));
        const float riffle = seconds * 0.62f;
        // the thumbs lift and let the halves fall: the hands roll in a little, come together for the bridge
        k.push_back(mk(0.30f + riffle * 0.5f, Vector3Add(w, {0, 0.012f, 0}), vnorm(Vector3Add(f, {0, 0.25f, 0})), p, HandPose::Pinch));
        k.push_back(mk(0.30f + riffle, Vector3Add(w, {-sd * 0.025f, 0.004f, 0}), f, p, HandPose::Pinch));
        k.push_back(mk(seconds, Vector3Add(w, {-sd * 0.035f, -0.004f, 0}), f, p, HandPose::Open, 0.f, 1));
        rest.t = seconds + 0.45f;
        k.push_back(rest);
        startTrack(o, a, TK_Reach, k);
    }
    o.gazeGoal = at;
    o.gazeHold = seconds;
}

void Cast::dealFromDeck(Opponent& o, Vector3 at, const std::vector<Vector3>& to, float interval) {
    if (to.empty()) return;
    const float hs = o.L.handScale;
    const Vector3 c = xfPoint(o.rootInv, at);
    const float total = interval * (float)to.size();
    {   // the left hand holds the deck, a little off the felt
        const Vector3 f = vnorm({0.55f, -0.3f, -0.78f});
        Vector3 p = vnorm({0.6f, -0.75f, 0.2f});
        p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
        const Vector3 g = clampReach(o, 1, Vector3Add(c, {-0.02f, 0.035f, 0.02f}), f, p, GRIP_CENTER);
        const Vector3 w = wristFor(g, f, p, GRIP_CENTER, hs, true);
        Key rest;
        restPose(o, 1, o.kind == 0 ? 10 : 0, rest);
        std::vector<Key> k;
        k.push_back(Key{});
        k.push_back(mk(std::min(0.3f, interval * 2.f), w, f, p, HandPose::Grip, 0.02f, 1));
        k.push_back(mk(total + 0.1f, w, f, p, HandPose::Grip));
        rest.t = total + 0.6f;
        k.push_back(rest);
        startTrack(o, 1, TK_Reach, k);
    }
    // the right thumb pushes each card off the top toward its player
    std::vector<Key> k;
    k.push_back(Key{});
    Vector3 top = clampReach(o, 0, Vector3Add(c, {0.01f, 0.05f, 0.f}), vnorm({-0.3f, -0.5f, -0.8f}), {0, -1, 0}, PINCH_POINT);
    for (size_t i = 0; i < to.size(); ++i) {
        const float t0 = interval * (float)i + std::min(0.3f, interval * 2.f);
        const Vector3 d = xfPoint(o.rootInv, to[i]);
        const Vector3 dir = vnorm({d.x - c.x, 0.f, d.z - c.z});
        Vector3 f, p;
        palmDown(Vector3Add(Vector3Scale(dir, 0.6f), {-0.3f, 0.f, -0.5f}), f, p);
        const Vector3 w0 = wristFor(top, f, p, PINCH_POINT, hs, false);
        const Vector3 w1 = Vector3Add(w0, Vector3Add(Vector3Scale(dir, 0.07f), {0, -0.01f, 0}));
        k.push_back(mk(t0, w0, f, p, HandPose::Pinch, 0.f, 0));
        k.push_back(mk(t0 + interval * 0.55f, w1, f, p, HandPose::Open, 0.f, 2));
    }
    Key rest;
    restPose(o, 0, o.kind == 1 ? 11 : 0, rest);
    rest.t = total + std::min(0.3f, interval * 2.f) + 0.5f;
    k.push_back(rest);
    startTrack(o, 0, TK_Reach, k);
    o.gazeGoal = at;
    o.gazeHold = total;
}

} // namespace chr

// ---------------------------------------------------------------- public API
void Characters::holdCards(int seat, bool on, const Vector3* pickUpAt) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.holdCards(m.opp[seat], on, pickUpAt);
}

bool Characters::cardFan(int seat, Matrix& frame) const {
    const Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return false;
    return m.cardFan(m.opp[seat], frame);
}

void Characters::playCard(int seat, Vector3 target, bool toss) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.playCardFromFan(m.opp[seat], target, toss);
    m.opp[seat].chinRest = false;
}

void Characters::gatherCards(int seat, Vector3 from, Vector3 to) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.gatherCards(m.opp[seat], from, to);
    m.opp[seat].chinRest = false;
}

void Characters::shuffleDeck(int seat, Vector3 at, float seconds) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.shuffleDeck(m.opp[seat], at, seconds);
    m.opp[seat].chinRest = false;
}

void Characters::dealCards(int seat, Vector3 at, const std::vector<Vector3>& to, float interval) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.dealFromDeck(m.opp[seat], at, to, interval);
    m.opp[seat].chinRest = false;
}

} // namespace r3d
