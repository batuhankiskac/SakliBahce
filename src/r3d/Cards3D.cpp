// 3D playing cards (see Cards3D.h).
#include "r3d/Cards3D.h"

#include "ui/CardRender.h"
#include "ui/Common.h"

#include <algorithm>
#include <cmath>

namespace r3d {

static_assert(CARD_JOKER_KEY == ui::cardgfx::KEY_JOKER, "Cards3D: the joker's atlas key");

namespace {

constexpr float RADIUS = 0.0036f; // corner radius (the atlas cards have 3.6 mm corners)
constexpr int CORNER_SEG = 5;

float qdot(Quaternion a, Quaternion b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// Matrix with basis columns X, Y, Z and origin T.
Matrix basisFrame(Vector3 X, Vector3 Y, Vector3 Z, Vector3 T) {
    Matrix m = MatrixIdentity();
    m.m0 = X.x, m.m1 = X.y, m.m2 = X.z;
    m.m4 = Y.x, m.m5 = Y.y, m.m6 = Y.z;
    m.m8 = Z.x, m.m9 = Z.y, m.m10 = Z.z;
    m.m12 = T.x, m.m13 = T.y, m.m14 = T.z;
    return m;
}

float easeInOut(float t) { return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f; }

// Outline of the rounded card in its local XZ plane, counter-clockwise seen from +Y.
std::vector<Vector2> outline() {
    std::vector<Vector2> pts;
    const float hx = CARD_W * 0.5f - RADIUS, hz = CARD_H * 0.5f - RADIUS;
    const Vector2 centres[4] = {{hx, hz}, {hx, -hz}, {-hx, -hz}, {-hx, hz}};
    const float start[4] = {0.f, -90.f, -180.f, -270.f};
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= CORNER_SEG; ++i) {
            const float a = (start[c] + 90.f * (float)i / (float)CORNER_SEG) * DEG2RAD;
            pts.push_back({centres[c].x + std::cos(a) * RADIUS, centres[c].y + std::sin(a) * RADIUS});
        }
    }
    // (the loop above runs clockwise in x/z; reverse it so the fan is counter-clockwise seen from +Y)
    std::reverse(pts.begin(), pts.end());
    return pts;
}

Mesh buildCard(int key) {
    const Rectangle f = ui::cardgfx::uv(key);
    const Rectangle b = ui::cardgfx::uv(ui::cardgfx::KEY_BACK);
    const Rectangle body = ui::cardgfx::uv(ui::cardgfx::KEY_BODY);
    const Vector2 bodyUV{body.x + body.width * 0.5f, body.y + body.height * 0.5f};
    const std::vector<Vector2> pts = outline();
    MeshBuilder mb;
    const float y = CARD_T * 0.5f;
    // face (+Y): u from x, v from z (the card's top at -z)
    auto faceUV = [&](Vector2 p) {
        return Vector2{f.x + (p.x / CARD_W + 0.5f) * f.width, f.y + (p.y / CARD_H + 0.5f) * f.height};
    };
    // back (-Y): seen from below the card is mirrored across x
    auto backUV = [&](Vector2 p) {
        return Vector2{b.x + (0.5f - p.x / CARD_W) * b.width, b.y + (p.y / CARD_H + 0.5f) * b.height};
    };
    {
        const int c = mb.vertex({0, y, 0}, {0, 1, 0}, faceUV({0, 0}));
        std::vector<int> ring;
        for (const Vector2& p : pts) ring.push_back(mb.vertex({p.x, y, p.y}, {0, 1, 0}, faceUV(p)));
        for (size_t i = 0; i < ring.size(); ++i) mb.triangle(c, ring[(i + 1) % ring.size()], ring[i]);
    }
    {
        const int c = mb.vertex({0, -y, 0}, {0, -1, 0}, backUV({0, 0}));
        std::vector<int> ring;
        for (const Vector2& p : pts) ring.push_back(mb.vertex({p.x, -y, p.y}, {0, -1, 0}, backUV(p)));
        for (size_t i = 0; i < ring.size(); ++i) mb.triangle(c, ring[i], ring[(i + 1) % ring.size()]);
    }
    // the edge band
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vector2 p = pts[i], q = pts[(i + 1) % pts.size()];
        Vector2 n{q.y - p.y, -(q.x - p.x)};
        const float l = std::sqrt(n.x * n.x + n.y * n.y);
        if (l > 1e-9f) n = {n.x / l, n.y / l};
        // outward normal: flip if it points to the centre
        if (n.x * (p.x + q.x) + n.y * (p.y + q.y) < 0.f) n = {-n.x, -n.y};
        const Vector3 nn{n.x, 0, n.y};
        const int a = mb.vertex({p.x, -y, p.y}, nn, bodyUV), bb = mb.vertex({q.x, -y, q.y}, nn, bodyUV);
        const int c = mb.vertex({q.x, y, q.y}, nn, bodyUV), d = mb.vertex({p.x, y, p.y}, nn, bodyUV);
        // wind so the outward side is the front
        const Vector3 e1{q.x - p.x, 0, q.y - p.y}, e2{0, 2.f * y, 0};
        const Vector3 cr = Vector3CrossProduct(e1, e2);
        if (Vector3DotProduct(cr, nn) > 0.f) mb.quad(a, bb, c, d);
        else mb.quad(a, d, c, bb);
    }
    return mb.build();
}

// A soft round halo (white, alpha falloff) for the glow under highlighted cards.
Texture2D buildHalo() {
    const int n = 64;
    Image img = GenImageColor(n, n, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float dx = ((float)x + 0.5f) / (float)n * 2.f - 1.f, dy = ((float)y + 0.5f) / (float)n * 2.f - 1.f;
            // a rounded rectangle falloff (cards are taller than wide: the quad is scaled)
            const float d = std::max(std::fabs(dx), std::fabs(dy));
            const float a = std::clamp(1.f - (d - 0.55f) / 0.45f, 0.f, 1.f);
            px[y * n + x] = Color{255, 255, 255, (unsigned char)(a * a * 255.f)};
        }
    Texture2D t = uploadMipmapped(img, false);
    UnloadImage(img);
    return t;
}

} // namespace

Quaternion cardFlat(float yawDeg, bool faceUp) {
    Quaternion q = QuaternionFromAxisAngle({0, 1, 0}, yawDeg * DEG2RAD);
    if (!faceUp) q = QuaternionMultiply(q, QuaternionFromAxisAngle({0, 0, 1}, PI));
    return q;
}

Quaternion cardStanding(float yawDeg, float tiltDeg, bool faceUp) {
    Quaternion q = QuaternionFromAxisAngle({0, 1, 0}, yawDeg * DEG2RAD);
    // the top edge (-z) rises: rotate about +x by +tilt
    q = QuaternionMultiply(q, QuaternionFromAxisAngle({1, 0, 0}, tiltDeg * DEG2RAD));
    if (!faceUp) q = QuaternionMultiply(q, QuaternionFromAxisAngle({0, 0, 1}, PI));
    return q;
}

Matrix cardMatrix(const CardPose& p) {
    return MatrixMultiply(QuaternionToMatrix(p.rot), MatrixTranslate(p.pos.x, p.pos.y, p.pos.z));
}

bool Cards3D::init(Renderer& r) {
    if (ready_) return true;
    ui::cardgfx::init();
    if (!ui::cardgfx::ready()) return false;
    setTextureOpaque(ui::cardgfx::atlas(), false);  // (the faces' rounded corners are transparent: no depth pre-pass)
    for (int i = 0; i < CARD_COUNT; ++i) {
        meshes_[i] = buildCard(cardFace(i)); // (the second deck shows the same faces)
        mats_[i] = r.makeMat(WHITE, ui::cardgfx::atlas(), 0.18f, 30.f);
    }
    {
        MeshBuilder mb;
        mb.plane({0, 0, 0}, {1.f, 1.f}, {0, 1, 0});
        halo_ = mb.build();
        haloTex_ = buildHalo();
        // one halo material per card: each glowing card has its own alpha (the renderer keeps the pointer until render())
        for (Mat& hm : haloMats_) hm = r.makeMat(Color{255, 210, 120, 255}, haloTex_, 0.f, 4.f, 1.f);
        hintMat_ = r.makeMat(Color{110, 255, 130, 255}, haloTex_, 0.f, 4.f, 1.f);
    }
    hideAll();
    ready_ = true;
    return true;
}

void Cards3D::shutdown(Renderer& r) {
    if (!ready_) return;
    for (int i = 0; i < CARD_COUNT; ++i) {
        UnloadMesh(meshes_[i]);
        r.unloadMat(mats_[i]);
    }
    UnloadMesh(halo_);
    for (Mat& hm : haloMats_) r.unloadMat(hm);
    r.unloadMat(hintMat_);
    if (haloTex_.id) unloadTexture(haloTex_);
    ui::cardgfx::shutdown();
    ready_ = false;
}

void Cards3D::hideAll() {
    for (Card& c : cards_) c = Card{};
}

void Cards3D::place(int card, const CardPose& p, bool animate, float delay, float arc) {
    if (card < 0 || card >= CARD_COUNT) return;
    Card& c = cards_[card];
    const bool same = c.visible && Vector3Distance(c.to.pos, p.pos) < 1e-5f &&
                      std::fabs(qdot(c.to.rot, p.rot)) > 0.99999f;
    if (same) return;
    if (!animate || !c.visible) { // (a card that appears is put down where it belongs: place it first, then fly it)
        c.visible = true;
        c.from = c.to = c.cur = p;
        c.t = 1.f;
        c.dur = 0.f;
        return;
    }
    c.from = c.cur;
    c.to = p;
    c.t = 0.f;
    c.delay = delay;
    c.arc = arc;
    c.soft = false;
    const float dist = Vector3Distance(c.from.pos, c.to.pos);
    c.dur = std::clamp(0.22f + dist * 0.55f, 0.25f, 0.75f);
}

void Cards3D::follow(int card, const CardPose& p) {
    if (card < 0 || card >= CARD_COUNT) return;
    Card& c = cards_[card];
    if (!c.visible) {
        place(card, p, false);
        return;
    }
    if (c.t < 1.f) { // in flight: it lands where the pose is now
        c.to = p;
        return;
    }
    if (Vector3Distance(c.cur.pos, p.pos) > 0.004f || std::fabs(qdot(c.cur.rot, p.rot)) < 0.9995f) {
        c.from = c.cur;
        c.to = p;
        c.t = 0.f;
        c.delay = 0.f;
        c.arc = 0.f;
        c.dur = 0.22f;
        c.soft = true;
        return;
    }
    c.from = c.to = c.cur = p;
}

void Cards3D::hide(int card) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].visible = false;
}

bool Cards3D::visible(int card) const { return card >= 0 && card < CARD_COUNT && cards_[card].visible; }
CardPose Cards3D::pose(int card) const { return cards_[std::clamp(card, 0, CARD_COUNT - 1)].cur; }
CardPose Cards3D::target(int card) const { return cards_[std::clamp(card, 0, CARD_COUNT - 1)].to; }
bool Cards3D::flying(int card) const {
    return card >= 0 && card < CARD_COUNT && cards_[card].visible && cards_[card].t < 1.f;
}

void Cards3D::setTint(int card, Color c) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].tint = c;
}
void Cards3D::setLift(int card, float metres) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].lift = metres;
}
void Cards3D::setRaise(int card, float metres) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].raise = metres;
}
void Cards3D::setTilt(int card, float deg) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].tilt = deg;
}
void Cards3D::setGlow(int card, float amount) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].glow = amount;
}
void Cards3D::setHintGlow(int card, bool on) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].hint = on;
}
void Cards3D::setSpeed(float s) { speed_ = std::clamp(s, 0.25f, 4.f); }

void Cards3D::update(float dt) {
    ui::cardgfx::refresh(); // (the four-colour deck switched on / off)
    for (Card& c : cards_) {
        if (!c.visible) continue;
        c.liftCur += (c.lift - c.liftCur) * std::min(1.f, dt * 14.f);
        c.raiseCur += (c.raise - c.raiseCur) * std::min(1.f, dt * 14.f);
        c.tiltCur += (c.tilt - c.tiltCur) * std::min(1.f, dt * 14.f);
        c.glowCur += (c.glow - c.glowCur) * std::min(1.f, dt * 10.f);
        if (c.t >= 1.f) {
            c.cur = c.to;
            continue;
        }
        if (c.delay > 0.f) {
            c.delay -= dt * speed_;
            continue;
        }
        c.t = std::min(1.f, c.t + dt * speed_ / std::max(0.05f, c.dur));
        const float e = easeInOut(c.t);
        c.cur.pos = Vector3Lerp(c.from.pos, c.to.pos, e);
        c.cur.pos.y += std::sin(c.t * PI) * c.arc;
        c.cur.rot = QuaternionSlerp(c.from.rot, c.to.rot, e);
    }
}

// Where a card is drawn: its pose, raised along its top edge, tilted back about its bottom edge, lifted off its face.
CardPose Cards3D::drawn(const Card& c) {
    CardPose p = c.cur;
    if (c.raiseCur > 1e-5f) p.pos = Vector3Add(p.pos, Vector3Scale(Vector3RotateByQuaternion({0, 0, -1}, p.rot), c.raiseCur));
    if (std::fabs(c.tiltCur) > 1e-3f) {
        const Vector3 bottom = Vector3Add(p.pos, Vector3RotateByQuaternion({0, 0, CARD_H * 0.5f}, p.rot));
        p.rot = QuaternionMultiply(p.rot, QuaternionFromAxisAngle({1, 0, 0}, c.tiltCur * DEG2RAD));
        p.pos = Vector3Subtract(bottom, Vector3RotateByQuaternion({0, 0, CARD_H * 0.5f}, p.rot));
    }
    if (c.liftCur > 1e-5f) p.pos = Vector3Add(p.pos, Vector3Scale(Vector3RotateByQuaternion({0, 1, 0}, p.rot), c.liftCur));
    return p;
}

void Cards3D::submit(Renderer& r) {
    if (!ready_) return;
    for (int i = 0; i < CARD_COUNT; ++i) {
        const Card& c = cards_[i];
        if (!c.visible) continue;
        const CardPose p = drawn(c);
        mats_[i].material.maps[MATERIAL_MAP_DIFFUSE].color = c.tint;
        r.submit(&meshes_[i], &mats_[i], cardMatrix(p), CastShadow);
        if (c.glowCur > 0.03f) {
            // the halo lies just under the card, a little larger
            const Matrix s = MatrixScale(CARD_W * 1.5f, 1.f, CARD_H * 1.35f);
            CardPose h = p;
            const Vector3 n = Vector3RotateByQuaternion({0, 1, 0}, p.rot);
            h.pos = Vector3Subtract(h.pos, Vector3Scale(n, CARD_T));
            Mat& hm = haloMats_[(size_t)i];
            hm.material.maps[MATERIAL_MAP_DIFFUSE].color =
                Color{255, 214, 130, (unsigned char)std::clamp(c.glowCur * 200.f, 0.f, 255.f)};
            r.submit(&halo_, &hm, MatrixMultiply(s, cardMatrix(h)), Transparent | Additive | DoubleSided | NoFog);
        }
        if (c.hint) {
            // the İpucu card: a larger green halo (its own material: the renderer keeps the pointer until render())
            const Matrix s = MatrixScale(CARD_W * 1.9f, 1.f, CARD_H * 1.6f);
            CardPose h = p;
            const Vector3 n = Vector3RotateByQuaternion({0, 1, 0}, p.rot);
            h.pos = Vector3Subtract(h.pos, Vector3Scale(n, CARD_T * 1.5f));
            // (colour-blind mode: sky blue, never confused with the warm gold of the legal / hovered cards)
            hintMat_.material.maps[MATERIAL_MAP_DIFFUSE].color =
                ui::colorBlind() ? Color{90, 190, 255, 245} : Color{110, 255, 130, 235};
            r.submit(&halo_, &hintMat_, MatrixMultiply(s, cardMatrix(h)), Transparent | Additive | DoubleSided | NoFog);
        }
    }
}

int Cards3D::pick(const Ray& ray, const std::vector<int>* among) const {
    int best = -1;
    float bestT = 1e9f;
    auto test = [&](int i) {
        const Card& c = cards_[i];
        if (!c.visible) return;
        const CardPose p = drawn(c);
        const Quaternion inv = QuaternionInvert(p.rot);
        const Vector3 o = Vector3RotateByQuaternion(Vector3Subtract(ray.position, p.pos), inv);
        const Vector3 d = Vector3RotateByQuaternion(ray.direction, inv);
        if (std::fabs(d.y) < 1e-6f) return;
        const float t = -o.y / d.y;
        if (t <= 0.f || t >= bestT) return;
        const float x = o.x + d.x * t, z = o.z + d.z * t;
        if (std::fabs(x) <= CARD_W * 0.5f && std::fabs(z) <= CARD_H * 0.5f) {
            bestT = t;
            best = i;
        }
    };
    if (among) {
        for (int i : *among)
            if (i >= 0 && i < CARD_COUNT) test(i);
    } else {
        for (int i = 0; i < CARD_COUNT; ++i) test(i);
    }
    return best;
}

bool Cards3D::animating() const {
    for (const Card& c : cards_)
        if (c.visible && c.t < 1.f && !c.soft) return true;
    return false;
}

// ---------------------------------------------------------------- layouts
namespace cardlayout {

// The player's fan: held low and close in front of the eye (the camera is at z 0.8, y 1.2), the cards radiating from
// a pivot near the bottom of the view, their faces turned up toward the eye but not flat to it.
namespace {
constexpr Vector3 kHumanPivot{0.f, w3d::TABLE_Y + 0.112f, 0.592f};
constexpr float kHumanFanTiltDeg = 47.f;   // the fan's plane leans back from upright by this much
} // namespace

Matrix humanFanFrame() {
    const float tilt = kHumanFanTiltDeg * DEG2RAD;
    const Vector3 up{0.f, std::cos(tilt), -std::sin(tilt)};
    const Vector3 face{0.f, std::sin(tilt), std::cos(tilt)};
    return basisFrame({1.f, 0.f, 0.f}, face, Vector3Negate(up), kHumanPivot);
}

CardPose hand(int seat, int i, int n) {
    CardPose p;
    n = std::max(1, n);
    const float mid = (float)(n - 1) * 0.5f;
    const float k = (float)i - mid;
    if (seat == 0) {
        // frame: x across, y the faces (toward the eye), -z up the cards
        const float tilt = kHumanFanTiltDeg * DEG2RAD;
        const Vector3 up{0.f, std::cos(tilt), -std::sin(tilt)};
        const Vector3 face{0.f, std::sin(tilt), std::cos(tilt)};
        const Matrix frame = basisFrame({1.f, 0.f, 0.f}, face, Vector3Negate(up), kHumanPivot);
        // a wider, flatter fan than the regulars' (the eye is close): the cards spread more, the radius is longer
        const float step = std::min(13.f, 76.f / (float)n) * DEG2RAD;
        const float th = -k * step;
        const Quaternion spin = QuaternionFromAxisAngle({0, 1, 0}, th);
        const Vector3 local = Vector3Add(Vector3RotateByQuaternion({0.f, 0.f, -0.040f}, spin), {0.f, 0.0006f * (float)i, 0.f});
        p.pos = Vector3Transform(local, frame);
        p.rot = QuaternionMultiply(QuaternionFromMatrix(frame), spin);
        return p;
    }
    // the others (no people to hold them): an upright fan where their left hand would be, faces toward them
    const float spread = std::min(0.026f, 0.30f / (float)n);
    const float right = -k * spread;
    p.pos = w3d::seatLocal(seat, right, 0.50f, w3d::TABLE_Y + 0.07f + std::fabs(k) * -0.0012f);
    Quaternion q = cardStanding(w3d::seatYawDeg(seat), 72.f, true);
    q = QuaternionMultiply(q, QuaternionFromAxisAngle({0, 1, 0}, k * std::min(3.f, 30.f / (float)n) * DEG2RAD));
    p.rot = q;
    return p;
}

CardPose fanCard(const Matrix& frame, int i, int n) {
    n = std::max(1, n);
    const float k = (float)i - (float)(n - 1) * 0.5f;
    const float step = std::min(9.f, 84.f / (float)n) * DEG2RAD;
    const Quaternion spin = QuaternionFromAxisAngle({0, 1, 0}, -k * step);
    // the pivot sits a little above the cards' bottom edge; later cards lie on top (toward the holder)
    const Vector3 local = Vector3Add(Vector3RotateByQuaternion({0.f, 0.f, -0.034f}, spin), {0.f, 0.0005f * (float)i, 0.f});
    CardPose p;
    p.pos = Vector3Transform(local, frame);
    p.rot = QuaternionMultiply(QuaternionFromMatrix(frame), spin);
    return p;
}

CardPose dealtPile(int seat, int i) {
    CardPose p;
    const float out = seat == 0 ? 0.30f : 0.29f;
    p.pos = w3d::seatLocal(seat, seat == 0 ? -0.05f : 0.03f, out, w3d::TABLE_Y + CARD_T * (0.5f + (float)i));
    p.rot = cardFlat(w3d::seatYawDeg(seat) + (float)((i * 37) % 9 - 4) * 1.3f, false);
    p.pos.x += 0.0012f * (float)((i * 13) % 5 - 2);
    p.pos.z += 0.0012f * (float)((i * 7) % 5 - 2);
    return p;
}

CardPose gathered(int seat, int i) {
    CardPose p;
    p.pos = w3d::seatLocal(seat, 0.f, 0.05f, w3d::TABLE_Y + CARD_T * (0.5f + (float)i) + 0.0004f);
    p.rot = cardFlat(w3d::seatYawDeg(seat) + (float)(i % 3 - 1) * 2.f, true);
    return p;
}

CardPose trick(int seat, int k) {
    CardPose p;
    const float jitter = (float)((k * 37 + seat * 11) % 9 - 4);
    p.pos = w3d::seatLocal(seat, 0.f, 0.11f, w3d::TABLE_Y + 0.0006f + 0.0005f * (float)(k % 4));
    p.rot = cardFlat(jitter * 1.6f + (seat % 2 ? 90.f : 0.f), true);
    return p;
}

CardPose wonPile(int seat, int i) {
    // every trick (four cards) is its own little face-down bundle, laid in a row beside the player: they can be counted
    const int trick = i / 4, k = i % 4;
    CardPose p;
    const float right = 0.17f + (float)(trick % 7) * 0.018f;
    const float out = 0.31f - (float)(trick / 7) * 0.05f;
    p.pos = w3d::seatLocal(seat, right, out, w3d::TABLE_Y + CARD_T * (0.5f + (float)k));
    p.pos.y += CARD_T * (float)(trick % 7) * 4.f; // each bundle lies a little on the previous one
    p.rot = cardFlat(w3d::seatYawDeg(seat) + 90.f + (float)((trick * 7 + k) % 5 - 2), false);
    return p;
}

CardPose deck(int seat, int i) {
    CardPose p;
    p.pos = w3d::seatLocal(seat, -0.24f, 0.30f, w3d::TABLE_Y + CARD_T * (0.5f + (float)i));
    p.rot = cardFlat(w3d::seatYawDeg(seat) + 8.f, false);
    return p;
}

CardPose middle(int i, bool faceUp, int card) {
    CardPose p;
    const float a = (float)((i * 53) % 17 - 8);
    p.pos = {0.006f * (float)((i * 29) % 7 - 3) * 0.3f, w3d::TABLE_Y + CARD_T * (0.5f + (float)i), 0.f};
    p.rot = cardFlat(a * 1.8f, faceUp);
    if (card >= 0) { // thrown on the pile: up to ~25° off and a centimetre or two aside
        uint32_t h = (uint32_t)(card * 2654435761u) ^ (uint32_t)(i * 40503u);
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        const float r0 = (float)(h & 1023u) / 1023.f - 0.5f, r1 = (float)((h >> 10) & 1023u) / 1023.f - 0.5f;
        const float r2 = (float)((h >> 20) & 1023u) / 1023.f - 0.5f;
        p.pos.x += r0 * 0.028f;
        p.pos.z += r1 * 0.024f;
        p.rot = cardFlat(r2 * 50.f, faceUp);
    }
    return p;
}

} // namespace cardlayout

} // namespace r3d
