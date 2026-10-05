// 3D playing cards (see Cards3D.h).
#include "r3d/Cards3D.h"

#include "ui/CardRender.h"
#include "ui/Common.h"

#include <algorithm>
#include <cmath>

namespace r3d {

namespace {

constexpr float RADIUS = 0.0036f; // corner radius (the atlas cards have 3.6 mm corners)
constexpr int CORNER_SEG = 5;

float qdot(Quaternion a, Quaternion b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

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
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
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
    for (int i = 0; i < CARD_COUNT; ++i) {
        meshes_[i] = buildCard(i);
        mats_[i] = r.makeMat(WHITE, ui::cardgfx::atlas(), 0.18f, 30.f);
    }
    {
        MeshBuilder mb;
        mb.plane({0, 0, 0}, {1.f, 1.f}, {0, 1, 0});
        halo_ = mb.build();
        haloTex_ = buildHalo();
        haloMat_ = r.makeMat(Color{255, 210, 120, 255}, haloTex_, 0.f, 4.f, 1.f);
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
    r.unloadMat(haloMat_);
    if (haloTex_.id) UnloadTexture(haloTex_);
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
    const float dist = Vector3Distance(c.from.pos, c.to.pos);
    c.dur = std::clamp(0.22f + dist * 0.55f, 0.25f, 0.75f);
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
void Cards3D::setGlow(int card, float amount) {
    if (card >= 0 && card < CARD_COUNT) cards_[card].glow = amount;
}
void Cards3D::setSpeed(float s) { speed_ = std::clamp(s, 0.25f, 4.f); }

void Cards3D::update(float dt) {
    for (Card& c : cards_) {
        if (!c.visible) continue;
        c.liftCur += (c.lift - c.liftCur) * std::min(1.f, dt * 14.f);
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

void Cards3D::submit(Renderer& r) {
    if (!ready_) return;
    for (int i = 0; i < CARD_COUNT; ++i) {
        const Card& c = cards_[i];
        if (!c.visible) continue;
        CardPose p = c.cur;
        if (c.liftCur > 1e-5f) {
            const Vector3 n = Vector3RotateByQuaternion({0, 1, 0}, p.rot);
            p.pos = Vector3Add(p.pos, Vector3Scale(n, c.liftCur));
        }
        mats_[i].material.maps[MATERIAL_MAP_DIFFUSE].color = c.tint;
        r.submit(&meshes_[i], &mats_[i], cardMatrix(p), CastShadow);
        if (c.glowCur > 0.03f) {
            // the halo lies just under the card, a little larger
            const Matrix s = MatrixScale(CARD_W * 1.5f, 1.f, CARD_H * 1.35f);
            CardPose h = p;
            const Vector3 n = Vector3RotateByQuaternion({0, 1, 0}, p.rot);
            h.pos = Vector3Subtract(h.pos, Vector3Scale(n, CARD_T));
            Mat& hm = haloMat_;
            hm.material.maps[MATERIAL_MAP_DIFFUSE].color =
                Color{255, 214, 130, (unsigned char)std::clamp(c.glowCur * 200.f, 0.f, 255.f)};
            r.submit(&halo_, &hm, MatrixMultiply(s, cardMatrix(h)), Transparent | Additive | DoubleSided | NoFog);
        }
    }
}

int Cards3D::pick(const Ray& ray, const std::vector<int>* among) const {
    int best = -1;
    float bestT = 1e9f;
    auto test = [&](int i) {
        const Card& c = cards_[i];
        if (!c.visible) return;
        CardPose p = c.cur;
        const Vector3 n = Vector3RotateByQuaternion({0, 1, 0}, p.rot);
        p.pos = Vector3Add(p.pos, Vector3Scale(n, c.liftCur));
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
        if (c.visible && c.t < 1.f) return true;
    return false;
}

// ---------------------------------------------------------------- layouts
namespace cardlayout {

CardPose hand(int seat, int i, int n) {
    CardPose p;
    n = std::max(1, n);
    const float mid = (float)(n - 1) * 0.5f;
    const float k = (float)i - mid;
    if (seat == 0) {
        // the human's fan: an arc near the table edge, standing toward the eye (the camera is at z 0.8, y 1.2)
        const float spread = std::min(0.034f, 0.42f / (float)n);
        const float x = k * spread;
        const float fanDeg = -k * std::min(3.2f, 30.f / (float)n);
        p.pos = {x, w3d::TABLE_Y + 0.052f + 0.0011f * (float)i - std::fabs(k) * 0.0015f, 0.43f + std::fabs(k) * 0.0012f};
        Quaternion q = cardStanding(0.f, 62.f, true);
        q = QuaternionMultiply(q, QuaternionFromAxisAngle({0, 1, 0}, fanDeg * DEG2RAD)); // fan about the card normal
        p.rot = q;
        return p;
    }
    // the others: an upright fan at their istaka spot, faces toward them (backs to the middle)
    const float spread = std::min(0.026f, 0.30f / (float)n);
    const float right = -k * spread;
    p.pos = w3d::seatLocal(seat, right, 0.50f, w3d::TABLE_Y + 0.07f + std::fabs(k) * -0.0012f);
    // held like the player's own fan, turned to the seat: faces toward the owner, backs toward the middle
    Quaternion q = cardStanding(w3d::seatYawDeg(seat), 72.f, true);
    q = QuaternionMultiply(q, QuaternionFromAxisAngle({0, 1, 0}, k * std::min(3.f, 30.f / (float)n) * DEG2RAD));
    p.rot = q;
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

CardPose middle(int i, bool faceUp) {
    CardPose p;
    const float a = (float)((i * 53) % 17 - 8);
    p.pos = {0.006f * (float)((i * 29) % 7 - 3) * 0.3f, w3d::TABLE_Y + CARD_T * (0.5f + (float)i), 0.f};
    p.rot = cardFlat(a * 1.8f, faceUp);
    return p;
}

} // namespace cardlayout

} // namespace r3d
