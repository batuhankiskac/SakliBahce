// Room module: life in the garden (room owner).
//   * Sparrows hop about the tree pit, the wall tops, the parapet and the trellis, peck, look round and flit from one
//     place to another; pigeons walk with their heads bobbing behind the tree and sit on the ocak's roof.
//   * Gulls wheel slowly over the sea, gliding with a wing beat now and then.
//   * Autumn: the çınar's leaves come down over the garden, tumbling; spring: the erguvan's petals drift across.
#include "r3d/RoomGardenInternal.h"
#include "r3d/Daytime.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace rm;

namespace {

// Where the birds may be: ground (y 0) or a ledge; a box to pick points from.
struct Area {
    float x0, x1, z0, z1, y;
    bool ground;
    int who;  // bit 0 sparrows, bit 1 pigeons
};
const Area kAreas[] = {
    {-3.0f, 0.6f, -5.35f, -3.75f, 0.015f, true, 3},                      // behind the tree, round the pit
    {w3d::ROOM_X0 - 0.22f, w3d::ROOM_X0 - 0.12f, -5.4f, 1.7f, 1.08f, false, 1},  // the left wall's coping
    {-4.2f, 4.0f, -5.83f, -5.73f, 0.93f, false, 3},                      // the parapet
    {-4.3f, 4.0f, 3.76f, 3.8f, 2.04f, false, 1},                         // the front wall's cap
    {0.9f, 4.0f, -3.62f, -3.55f, 2.87f, false, 3},                       // the ocak's roof, at the wall
    {0.85f, 1.22f, -3.3f, -2.95f, 0.015f, true, 1},                       // crumbs by the counter (Ocakçı: clear of him and his crate)
    {-2.6f, -2.5f, -3.0f, 3.0f, 3.2f, false, 1},                         // the trellis' rafter
};
constexpr int kAreaN = (int)(sizeof(kAreas) / sizeof(kAreas[0]));

Vector3 pickIn(const Area& a, okey::Rng& rng) { return {rng.uniform(a.x0, a.x1), a.y, rng.uniform(a.z0, a.z1)}; }

Matrix trsQ(Vector3 t, float yaw, float pitch, float roll, float s) {
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateZ(roll)), MatrixRotateX(pitch)), MatrixRotateY(yaw)),
                          MatrixTranslate(t.x, t.y, t.z));
}

}  // namespace

void Room::Impl::initGardenLife() {
    Garden& G = *gd;
    okey::Rng rng(seed + 7171);
    // ---- meshes: body (with tail), head (with beak), one wing (the other mirrored); forward is -Z
    for (int k = 0; k < 2; ++k) {
        const bool pig = k == 1;
        const Color body = pig ? Color{150, 154, 168, 255} : Color{140, 106, 76, 255};
        const Color belly = pig ? Color{170, 172, 184, 255} : Color{196, 186, 166, 255};
        const Color head = pig ? Color{120, 128, 146, 255} : Color{120, 110, 104, 255};
        const Color wing = pig ? Color{132, 136, 150, 255} : Color{120, 84, 56, 255};
        MeshBuilder b;
        b.ellipsoid({0, 0.05f, 0.f}, {0.032f, 0.03f, 0.055f}, 6, 10, body);
        b.ellipsoid({0, 0.04f, -0.01f}, {0.028f, 0.022f, 0.04f}, 5, 8, belly);
        // tail
        b.setTransform(MatrixMultiply(MatrixRotateX(-0.35f), MatrixTranslate(0, 0.058f, 0.07f)));
        b.roundedBox({0, 0, 0}, {0.034f, 0.006f, 0.05f}, 0.003f, 1, scaleRgb(wing, 0.8f));
        b.resetTransform();
        // legs
        for (int s = -1; s <= 1; s += 2) b.capsule({s * 0.01f, 0.025f, 0.f}, {s * 0.011f, 0.0f, -0.004f}, 0.0025f, 4, Color{170, 120, 100, 255});
        if (pig) b.ellipsoid({0, 0.072f, -0.035f}, {0.022f, 0.022f, 0.02f}, 5, 8, Color{110, 140, 130, 255});  // the green-purple neck
        G.birdBody[k] = b.build(true);
        MeshBuilder h;
        h.sphere({0, 0, 0}, 0.021f, 6, 8, head);
        if (!pig) h.ellipsoid({0, -0.006f, 0.004f}, {0.016f, 0.012f, 0.016f}, 4, 6, Color{70, 60, 56, 255});  // the cheek / bib
        h.capsule({0, -0.002f, -0.016f}, {0, -0.005f, -0.03f}, 0.006f, 5, pig ? Color{60, 56, 60, 255} : Color{70, 60, 50, 255});
        for (int s = -1; s <= 1; s += 2) h.sphere({s * 0.014f, 0.006f, -0.01f}, 0.004f, 3, 4, Color{20, 18, 18, 255});
        G.birdHead[k] = h.build(true);
        MeshBuilder w;  // hinged at the origin, spreading along +X
        w.ellipsoid({0.045f, 0.f, 0.01f}, {0.045f, 0.004f, 0.03f}, 4, 8, wing);
        G.birdWing[k] = w.build(true);
    }
    {
        MeshBuilder b;
        b.ellipsoid({0, 0, 0}, {0.07f, 0.065f, 0.24f}, 6, 10, Color{238, 238, 236, 255});
        b.sphere({0, 0.04f, -0.2f}, 0.06f, 6, 8, Color{244, 244, 242, 255});
        b.capsule({0, 0.03f, -0.26f}, {0, 0.02f, -0.31f}, 0.012f, 5, Color{230, 190, 60, 255});
        b.setTransform(MatrixTranslate(0, 0.01f, 0.24f));
        b.roundedBox({0, 0, 0}, {0.1f, 0.015f, 0.1f}, 0.006f, 1, Color{230, 230, 230, 255});
        b.resetTransform();
        G.gullBody = b.build(true);
        MeshBuilder w;  // a gull's wing: an inner and a bent outer part, grey with a black tip
        w.setTransform(MatrixTranslate(0.2f, 0.f, 0.f));
        w.roundedBox({0, 0, 0}, {0.4f, 0.012f, 0.16f}, 0.005f, 1, Color{176, 182, 190, 255});
        w.setTransform(MatrixMultiply(MatrixRotateZ(-0.25f), MatrixTranslate(0.56f, -0.04f, 0.02f)));
        w.roundedBox({0, 0, 0}, {0.34f, 0.01f, 0.12f}, 0.005f, 1, Color{170, 176, 184, 255});
        w.setTransform(MatrixMultiply(MatrixRotateZ(-0.25f), MatrixTranslate(0.78f, -0.1f, 0.03f)));
        w.roundedBox({0, 0, 0}, {0.12f, 0.009f, 0.09f}, 0.004f, 1, Color{30, 30, 34, 255});
        w.resetTransform();
        G.gullWing = w.build(true);
    }
    // ---- the flock
    G.birds.clear();
    for (int i = 0; i < 9; ++i) {
        Garden::Bird b;
        b.kind = i < 6 ? 0 : 1;
        int a;
        do a = rng.range(0, kAreaN - 1);
        while (!(kAreas[a].who & (b.kind == 0 ? 1 : 2)));
        b.spot = a;
        b.pos = b.from = b.to = pickIn(kAreas[a], rng);
        b.yaw = rng.uniform(0.f, 2.f * PI);
        b.timer = rng.uniform(0.5f, 4.f);
        b.scale = b.kind == 0 ? rng.uniform(0.95f, 1.08f) : rng.uniform(1.65f, 1.85f);
        G.birds.push_back(b);
    }
    G.gulls.clear();
    for (int i = 0; i < 3; ++i) {
        Garden::Gull g;
        g.c = {rng.uniform(-6.f, 4.f), 0.f, rng.uniform(-30.f, -20.f)};
        g.r = rng.uniform(6.f, 11.f);
        g.ang = rng.uniform(0.f, 2.f * PI);
        g.speed = rng.uniform(0.08f, 0.13f) * (rng.chance(0.5f) ? 1.f : -1.f);
        g.h = rng.uniform(9.f, 16.f);
        g.flapT = rng.uniform(1.f, 6.f);
        G.gulls.push_back(g);
    }
    // petals: a soft pink blob (the leaf texture serves the autumn leaves)
    {
        const int N = 32;
        Image img = GenImageColor(N, N, Color{255, 255, 255, 0});
        Color* px = (Color*)img.data;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = (x + 0.5f) / N * 2.f - 1.f, v = (y + 0.5f) / N * 2.f - 1.f;
                const float d = std::sqrt(u * u * 1.6f + v * v);
                const float a = std::clamp((1.f - d) * 4.f, 0.f, 1.f);
                px[y * N + x] = Color{255, 255, 255, (unsigned char)(a * 255.f)};
            }
        G.texPetal = LoadTextureFromImage(img);
        SetTextureFilter(G.texPetal, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }
    G.falls.resize(44);
    for (Garden::Fall& f : G.falls) {
        f.p = {rng.uniform(-4.f, 4.f), rng.uniform(0.2f, 7.f), rng.uniform(-5.f, 3.4f)};
        f.speed = rng.uniform(0.35f, 0.7f);
        f.sway = rng.uniform(0.3f, 0.7f);
        f.phase = rng.uniform(0.f, 6.28f);
        f.size = rng.uniform(0.09f, 0.14f);
        f.kind = rng.range(0, 3);
    }
}

void Room::Impl::freeGardenLife(Renderer& r) {
    (void)r;
    Garden& G = *gd;
    auto um = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    for (int k = 0; k < 2; ++k) um(G.birdBody[k]), um(G.birdHead[k]), um(G.birdWing[k]);
    um(G.gullBody);
    um(G.gullWing);
    if (G.texPetal.id) UnloadTexture(G.texPetal);
    G.texPetal = Texture2D{};
    G.birds.clear();
    G.gulls.clear();
    G.falls.clear();
}

void Room::Impl::updateGardenLife(float dt) {
    Garden& G = *gd;
    if (dt <= 0.f) return;
    const bool night = phase == w3d::Gece;
    // ---- birds (they roost at night: nobody about, the gulls too)
    for (Garden::Bird& b : G.birds) {
        b.flap += dt * (b.state == 2 ? (b.kind == 0 ? 38.f : 22.f) : 0.f);
        b.peck = std::max(0.f, b.peck - dt * 4.f);
        switch (b.state) {
        case 0: {  // idle: look round, peck
            b.timer -= dt;
            b.headYaw += (std::sin(time * 3.1f + b.yaw * 5.f) * 0.6f - b.headYaw) * std::min(1.f, dt * 6.f);
            if (kAreas[b.spot].ground && rng.chance(dt * 1.6f)) b.peck = 1.f;
            if (b.timer > 0.f || night) break;
            const Area& A = kAreas[b.spot];
            if (rng.chance(b.kind == 0 ? 0.18f : 0.12f)) {  // fly to another place
                int a;
                do a = rng.range(0, kAreaN - 1);
                while (!(kAreas[a].who & (b.kind == 0 ? 1 : 2)));
                b.spot = a;
                b.from = b.pos;
                b.to = pickIn(kAreas[a], rng);
                b.t = 0.f;
                b.dur = std::max(0.8f, Vector3Distance(b.from, b.to) / (b.kind == 0 ? 3.2f : 2.6f));
                b.state = 2;
            } else {
                b.from = b.pos;
                Vector3 to = Vector3Add(b.pos, {rng.uniform(-0.3f, 0.3f), 0.f, rng.uniform(-0.3f, 0.3f)});
                to.x = std::clamp(to.x, A.x0, A.x1), to.z = std::clamp(to.z, A.z0, A.z1);
                b.to = to;
                b.t = 0.f;
                if (b.kind == 0) {
                    b.dur = 0.16f;
                    b.state = 1;
                } else {
                    b.dur = std::max(0.3f, Vector3Distance(b.from, b.to) / 0.3f);
                    b.state = 3;
                }
            }
            const Vector3 d = Vector3Subtract(b.to, b.from);
            if (d.x * d.x + d.z * d.z > 1e-5f) b.yaw = std::atan2(-d.x, -d.z);
            break;
        }
        default: {  // moving: hop / fly / walk
            b.t = std::min(1.f, b.t + dt / b.dur);
            Vector3 p = Vector3Lerp(b.from, b.to, b.state == 2 ? b.t * b.t * (3.f - 2.f * b.t) : b.t);
            if (b.state == 1) p.y += 0.06f * 4.f * b.t * (1.f - b.t);
            if (b.state == 2) p.y += (0.6f + 0.2f * Vector3Distance(b.from, b.to)) * 4.f * b.t * (1.f - b.t);
            if (b.state == 3) b.bob += dt * 9.f;
            b.pos = p;
            if (b.t >= 1.f) {
                b.state = 0;
                b.timer = b.kind == 0 ? rng.uniform(0.3f, 2.5f) : rng.uniform(1.f, 5.f);
                if (b.kind == 0 && rng.chance(0.5f)) b.timer = 0.05f;  // hop, hop, hop
            }
            break;
        }
        }
    }
    // ---- gulls
    for (Garden::Gull& g : G.gulls) {
        g.ang += g.speed * dt;
        g.flapT -= dt;
        if (g.flapT <= 0.f) {
            g.flap = 1.f;
            g.flapT = rng.uniform(3.f, 9.f);
        }
        g.flap = std::max(0.f, g.flap - dt * 0.6f);
        g.bank = g.speed > 0.f ? -0.35f : 0.35f;
    }
    // ---- falling leaves (autumn) / petals (spring)
    const int se = seasonNow;
    if (se == w3d::Sonbahar || se == w3d::Ilkbahar) {
        const float wind = 0.15f + 0.35f * G.wind;
        for (Garden::Fall& f : G.falls) {
            f.p.y -= f.speed * dt * (0.7f + 0.3f * std::sin(time * 2.1f + f.phase));
            f.p.x += (std::sin(time * 1.3f + f.phase) * f.sway + wind) * dt;
            f.p.z += std::cos(time * 0.9f + f.phase) * f.sway * 0.7f * dt;
            f.phase += dt * 2.6f;
            const bool overTable = std::fabs(f.p.x) < 0.75f && std::fabs(f.p.z) < 0.75f && f.p.y < 1.3f;
            if (f.p.y < 0.03f || f.p.x > 4.1f || overTable) {
                if (se == w3d::Sonbahar) f.p = {rng.uniform(-4.f, 3.5f), rng.uniform(4.6f, 6.5f), rng.uniform(-5.2f, 3.2f)};
                else f.p = {rng.uniform(-4.6f, -1.5f), rng.uniform(2.f, 3.6f), rng.uniform(-5.4f, -3.f)};  // from the erguvan
            }
        }
    }
}

void Room::Impl::submitGardenLife(Renderer& r) {
    Garden& G = *gd;
    const bool night = phase == w3d::Gece;
    if (!night) {
        for (const Garden::Bird& b : G.birds) {
            const int k = b.kind;
            const float s = b.scale;
            const float bob = b.state == 3 ? 0.012f * std::sin(b.bob) : 0.f;
            const float pitch = b.state == 2 ? -0.15f : 0.f;
            const Matrix body = trsQ(b.pos, b.yaw, pitch, 0.f, s);
            r.submit(&G.birdBody[k], &G.mBird, body, 0);
            // head: on the body's front, turned and pecking (pigeons bob it back and forth as they walk)
            const Vector3 hl{0.f, 0.085f - 0.04f * b.peck, -0.045f - 0.025f * b.peck + bob};
            const Vector3 hw = Vector3Transform(hl, body);
            r.submit(&G.birdHead[k], &G.mBird, trsQ(hw, b.yaw + b.headYaw, pitch + 0.9f * b.peck, 0.f, s), 0);
            // wings: folded on the back, beating in flight
            const float beat = b.state == 2 ? std::sin(b.flap) * 1.1f : 0.f;
            for (int side = -1; side <= 1; side += 2) {
                const Vector3 sh = Vector3Transform({side * 0.022f, 0.065f, -0.005f}, body);
                Matrix W;
                if (b.state == 2) {
                    W = MatrixMultiply(MatrixMultiply(MatrixScale((float)side * s, s, s), MatrixRotateZ(side * (0.2f + beat))),
                                       MatrixMultiply(MatrixRotateX(pitch), MatrixMultiply(MatrixRotateY(b.yaw), MatrixTranslate(sh.x, sh.y, sh.z))));
                } else {  // folded along the body
                    W = MatrixMultiply(MatrixMultiply(MatrixScale((float)side * s * 0.6f, s, s * 1.2f), MatrixRotateY(side * 1.35f)),
                                       MatrixMultiply(MatrixRotateY(b.yaw), MatrixTranslate(sh.x, sh.y, sh.z)));
                }
                r.submit(&G.birdWing[k], &G.mBird, W, DoubleSided);
            }
        }
        for (const Garden::Gull& g : G.gulls) {
            const Vector3 p{g.c.x + std::cos(g.ang) * g.r, g.h + 0.6f * std::sin(time * 0.3f + g.r), g.c.z + std::sin(g.ang) * g.r};
            // heading along the circle
            const Vector3 v{-std::sin(g.ang) * g.speed, 0.f, std::cos(g.ang) * g.speed};
            const float yaw = std::atan2(-v.x, -v.z);
            const Matrix body = trsQ(p, yaw, 0.f, g.bank, 1.f);
            r.submit(&G.gullBody, &G.mBird, body, 0);
            const float flap = g.flap > 0.f ? std::sin(time * 7.f) * 0.6f * g.flap : 0.f;
            for (int side = -1; side <= 1; side += 2) {
                const Matrix W = MatrixMultiply(MatrixMultiply(MatrixScale((float)side, 1.f, 1.f), MatrixRotateZ(side * (0.12f + flap))), body);
                r.submit(&G.gullWing, &G.mBird, W, DoubleSided);
            }
        }
    }
    const int se = seasonNow;
    if (se == w3d::Sonbahar || se == w3d::Ilkbahar) {
        static const Color kLeaf[4] = {{214, 160, 56, 255}, {190, 104, 40, 255}, {170, 80, 36, 255}, {150, 120, 60, 255}};
        const bool petals = se == w3d::Ilkbahar;
        const Texture2D tex = petals ? G.texPetal : texLeaf;
        const Rectangle src{0.f, 0.f, (float)tex.width, (float)tex.height};
        const float light = 0.35f + 0.65f * dayK;
        for (const Garden::Fall& f : G.falls) {
            const Color c = scaleRgb(petals ? Color{226, 120, 180, 255} : kLeaf[f.kind & 3], light);
            const float tumble = 0.25f + 0.75f * std::fabs(std::cos(f.phase));
            const float sz = petals ? f.size * 0.35f : f.size;
            r.submitBillboard(tex, src, f.p, {sz * tumble, sz}, c, false);
        }
    }
}

}  // namespace r3d
