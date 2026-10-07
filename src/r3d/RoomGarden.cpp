// Room module: the garden kahvehane, the second place to play (room owner). See RoomGardenInternal.h for the plan.
//   * Venue (Room::setVenue): içerisi / bahçe / otomatik (the garden on fair spring and summer days and evenings).
//   * Static geometry, built on first use: cobbles (one batched mesh), the rubble wall with the gate, the parapet over
//     the sea, the whitewashed front wall, the kahvehane's front, the ocak's little tiled roof, the vine trellis, pots
//     of geraniums and basil, the bench, the mangal, the string lights, the awning, the view and the sky dome.
//   * Light: by day the sun is the shadow-casting key light (25 m away: nearly parallel, and the leaves of the çınar
//     and the vines on the trellis dapple it); at dusk it is low and golden and the string lights come on; at night
//     our table's pendant is the key light again, the string bulbs, the other pendants and the lit windows the rest.
#include "r3d/RoomGardenInternal.h"
#include "r3d/Daytime.h"
#include "r3d/SpecialDay.h"  // (ozelgun)

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace r3d {

using namespace rm;

namespace {

constexpr float GX0 = w3d::ROOM_X0, GX1 = w3d::ROOM_X1;  // inner face of the left wall / the kahvehane's front
constexpr float GZB = -5.6f;                              // inner face of the parapet over the sea
constexpr float GZF = w3d::ROOM_Z1;                       // inner face of the front wall
constexpr float WALL_T = 0.35f;
constexpr float TRELLIS_Y = w3d::CEILING_Y;               // the pendants' cords end here
constexpr float GATE_Z0 = 2.35f, GATE_Z1 = 3.25f;         // where the room's street door is (the bystanders' way in)
constexpr float KIOSK_X0 = 0.7f;                          // the ocak's back wall runs from here to the facade
constexpr float SUN_DIST = 25.f;

Matrix translate(Vector3 p) { return MatrixTranslate(p.x, p.y, p.z); }

// Box with world UVs on the sides (texScale metres per repeat), any orientation of the grain.
void wbox(MeshBuilder& b, Vector3 c, Vector3 s, float ts, Color col) { boxW(b, c, s, ts, col, s.x >= s.z ? 0 : 2); }

// A wall face (axis-aligned plane) with rectangular holes: rect [a0,a1] x [y0,y1] along `right` from `origin`.
struct Hole {
    float a0, a1, y0, y1;
};
void wallFace(MeshBuilder& b, Vector3 origin, Vector3 right, Vector3 normal, float a0, float a1, float y0, float y1,
              const std::vector<Hole>& holes, float ts, Color col) {
    std::vector<float> xs{a0, a1};
    for (const Hole& h : holes) xs.push_back(std::clamp(h.a0, a0, a1)), xs.push_back(std::clamp(h.a1, a0, a1));
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    auto P = [&](float a, float y) { return Vector3{origin.x + right.x * a, y, origin.z + right.z * a}; };
    auto T = [&](float a, float y) { return Vector2{a / ts, -y / ts}; };
    for (size_t i = 0; i + 1 < xs.size(); ++i) {
        const float xa = xs[i], xb = xs[i + 1], mid = (xa + xb) * 0.5f;
        if (xb - xa < 1e-4f) continue;
        std::vector<std::pair<float, float>> blocked;
        for (const Hole& h : holes)
            if (mid > h.a0 && mid < h.a1) blocked.push_back({std::max(h.y0, y0), std::min(h.y1, y1)});
        std::sort(blocked.begin(), blocked.end());
        float y = y0;
        auto emit = [&](float ya, float yb) {
            Vector3 p0 = P(xa, ya), p1 = P(xb, ya), p2 = P(xb, yb), p3 = P(xa, yb);
            int i0 = b.vertex(p0, normal, T(xa, ya), col), i1 = b.vertex(p1, normal, T(xb, ya), col);
            int i2 = b.vertex(p2, normal, T(xb, yb), col), i3 = b.vertex(p3, normal, T(xa, yb), col);
            // counter-clockwise seen from the normal side
            Vector3 n = Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0));
            if (Vector3DotProduct(n, normal) >= 0.f) b.quad(i0, i1, i2, i3);
            else b.quad(i0, i3, i2, i1);
        };
        for (auto& bl : blocked) {
            if (bl.second <= bl.first) continue;
            if (bl.first > y + 1e-4f) emit(y, bl.first);
            y = std::max(y, bl.second);
        }
        if (y1 > y + 1e-4f) emit(y, y1);
    }
}

// A low dry-stone wall along X or Z (from p0 to p1 on the floor), height h, with a flat coping.
void stoneWall(GardenBuilders& G, Vector3 p0, Vector3 p1, float h, float t, okey::Rng& rng) {
    const bool alongX = std::fabs(p1.x - p0.x) > std::fabs(p1.z - p0.z);
    const Vector3 c{(p0.x + p1.x) * 0.5f, h * 0.5f, (p0.z + p1.z) * 0.5f};
    const float len = alongX ? std::fabs(p1.x - p0.x) : std::fabs(p1.z - p0.z);
    const Vector3 s = alongX ? Vector3{len, h, t} : Vector3{t, h, len};
    boxW(G.stoneCast, c, s, 1.6f, WHITE, alongX ? 0 : 2);
    // coping: flat slabs of a lighter stone, each a little different
    const int n = std::max(1, (int)(len / 0.55f));
    for (int i = 0; i < n; ++i) {
        const float a = -len * 0.5f + (i + 0.5f) * len / n;
        const Vector3 cc = alongX ? Vector3{c.x + a, h + 0.035f, c.z} : Vector3{c.x, h + 0.035f, c.z + a};
        const Vector3 cs = alongX ? Vector3{len / n - 0.012f, 0.07f, t + 0.06f} : Vector3{t + 0.06f, 0.07f, len / n - 0.012f};
        rbox(G.stone, cc, cs, 0.012f, 1, scaleRgb(Color{226, 216, 196, 255}, rng.uniform(0.88f, 1.04f)));
    }
}

// Terracotta pot with a plant: 0 red geranium, 1 pink geranium, 2 basil, 3 white geranium. `tin`: an old olive-oil tin.
void pottedPlant(GardenBuilders& G, Vector3 base, float s, int kind, bool tin, okey::Rng& rng) {
    const float r = 0.11f * s, h = 0.17f * s;
    if (tin) {
        boxW(G.metal, {base.x, base.y + h * 0.6f, base.z}, {r * 1.8f, h * 1.2f, r * 1.8f}, 0.5f, Color{190, 160, 70, 255});
        canvasQuad(G.art, {base.x, base.y + h * 0.6f, base.z + r * 0.9f + 0.002f}, r * 1.7f, h * 0.8f, {0, 0, 1}, art::TENEKE, ART_W, ART_H);
        canvasQuad(G.art, {base.x - r * 0.9f - 0.002f, base.y + h * 0.6f, base.z}, r * 1.7f, h * 0.8f, {-1, 0, 0}, art::TENEKE, ART_W, ART_H);
    } else {
        latheAt(G.clay, base, {{0.f, 0.f}, {r * 0.72f, 0.f}, {r * 0.95f, h * 0.9f}, {r * 1.08f, h * 0.92f}, {r * 1.08f, h}, {r * 0.92f, h},
                               {r * 0.88f, h * 0.92f}, {0.f, h * 0.92f}},
                16, false, false, scaleRgb(Color{186, 98, 62, 255}, rng.uniform(0.9f, 1.05f)));
    }
    const float top = base.y + (tin ? h * 1.2f : h * 0.92f);
    const Color leafA = kind == 2 ? Color{86, 150, 56, 255} : Color{64, 110, 52, 255};
    const Color leafB = kind == 2 ? Color{110, 170, 70, 255} : Color{82, 128, 60, 255};
    if (kind == 2) {  // basil: a dense round bush of small bright leaves
        for (int i = 0; i < 34; ++i) {
            float a = rng.uniform(0.f, 2.f * PI), rr = rng.uniform(0.f, r * 1.1f), yy = rng.uniform(0.02f, 0.2f) * s;
            Vector3 p{base.x + std::cos(a) * rr, top + yy, base.z + std::sin(a) * rr};
            G.leafy.ellipsoid(p, {0.022f * s, 0.009f * s, 0.016f * s}, 3, 5, mix(leafA, leafB, rng.uniform()));
        }
        return;
    }
    // geranium: round scalloped leaves low, flower heads on stalks above
    for (int i = 0; i < 16; ++i) {
        float a = rng.uniform(0.f, 2.f * PI), rr = rng.uniform(0.f, r * 1.25f), yy = rng.uniform(0.01f, 0.12f) * s;
        Vector3 p{base.x + std::cos(a) * rr, top + yy, base.z + std::sin(a) * rr};
        G.leafy.ellipsoid(p, {0.04f * s, 0.012f * s, 0.04f * s}, 3, 6, mix(leafA, leafB, rng.uniform()));
    }
    const Color fl = kind == 0 ? Color{214, 34, 40, 255} : (kind == 1 ? Color{232, 96, 150, 255} : Color{242, 236, 236, 255});
    const int heads = 3 + rng.range(0, 3);
    for (int k = 0; k < heads; ++k) {
        float a = rng.uniform(0.f, 2.f * PI), rr = rng.uniform(0.f, r * 0.9f);
        Vector3 hp{base.x + std::cos(a) * rr, top + rng.uniform(0.14f, 0.24f) * s, base.z + std::sin(a) * rr};
        G.leafy.capsule({hp.x, top, hp.z}, hp, 0.003f, 4, leafA);
        for (int f = 0; f < 7; ++f)
            G.leafy.sphere(Vector3Add(hp, {rng.uniform(-0.025f, 0.025f) * s, rng.uniform(-0.01f, 0.02f) * s, rng.uniform(-0.025f, 0.025f) * s}),
                           0.014f * s, 3, 5, scaleRgb(fl, rng.uniform(0.85f, 1.1f)));
    }
}

// Cobbles (Arnavut kaldırımı): one batched mesh of domed, irregular stones on a jittered grid, skipping `skip`.
template <class Skip>
void cobbles(MeshBuilder& b, float x0, float z0, float x1, float z1, Skip skip, okey::Rng& rng) {
    const float step = 0.135f;
    const Color tones[5] = {{196, 190, 178, 255}, {170, 168, 160, 255}, {150, 146, 140, 255}, {186, 172, 150, 255}, {132, 134, 136, 255}};
    int row = 0;
    for (float z = z0 + step * 0.5f; z < z1; z += step * 0.9f, ++row) {
        for (float x = x0 + step * 0.5f + (row % 2) * step * 0.5f; x < x1; x += step) {
            const float cx = x + rng.uniform(-0.018f, 0.018f), cz = z + rng.uniform(-0.016f, 0.016f);
            if (skip(cx, cz)) continue;
            const float r = step * rng.uniform(0.40f, 0.5f), h = rng.uniform(0.012f, 0.026f);
            const Color c = scaleRgb(tones[rng.range(0, 4)], rng.uniform(0.85f, 1.08f));
            const int n = 7;
            const float rot = rng.uniform(0.f, 2.f * PI);
            Vector3 top{cx, h, cz};
            int ci = b.vertex(top, {0, 1, 0}, {cx * 3.f, cz * 3.f}, c);
            int first = -1, prevTop = -1, prevBot = -1;
            int firstBot = -1;
            for (int i = 0; i <= n; ++i) {
                const int k = i % n;
                const float a = rot + 2.f * PI * k / n;
                const float rr = r * (0.82f + 0.18f * hash1((uint32_t)(k * 977 + (int)(cx * 1000) * 13 + (int)(cz * 1000) * 7)));
                Vector3 e{cx + std::cos(a) * rr, h * 0.55f, cz + std::sin(a) * rr};
                Vector3 g{cx + std::cos(a) * rr * 1.06f, 0.f, cz + std::sin(a) * rr * 1.06f};
                Vector3 n1 = Vector3Normalize({std::cos(a) * 0.6f, 0.8f, std::sin(a) * 0.6f});
                Vector3 n2 = Vector3Normalize({std::cos(a), 0.15f, std::sin(a)});
                int ti, bi;
                if (i < n) {
                    ti = b.vertex(e, n1, {e.x * 3.f, e.z * 3.f}, c);
                    bi = b.vertex(g, n2, {g.x * 3.f, g.z * 3.f}, scaleRgb(c, 0.55f));
                    if (i == 0) first = ti, firstBot = bi;
                } else {
                    ti = first, bi = firstBot;
                }
                if (prevTop >= 0) {
                    b.triangle(ci, ti, prevTop);   // counter-clockwise from above
                    b.quad(prevBot, prevTop, ti, bi);
                }
                prevTop = ti, prevBot = bi;
            }
        }
    }
}

// Catenary-ish sag between two points.
Vector3 sagPoint(Vector3 a, Vector3 b, float t, float sag) {
    Vector3 p = Vector3Lerp(a, b, t);
    p.y -= sag * 4.f * t * (1.f - t);
    return p;
}

// The garden's chair: turned wooden legs, a straw seat (the same kind as at our table), sitter faces -Z.
void buildGardenChair(Mesh& frame, Mesh& seat) {
    MeshBuilder b;
    const Color c{150, 104, 66, 255};
    std::vector<Vector2> leg = {{0.0f, 0.0f}, {0.0150f, 0.0f}, {0.0160f, 0.012f}, {0.0135f, 0.03f}, {0.0150f, 0.13f}, {0.0185f, 0.15f},
                                {0.0150f, 0.17f}, {0.0170f, 0.33f}, {0.0205f, 0.36f}, {0.0180f, 0.38f}, {0.0190f, 0.44f}, {0.0f, 0.44f}};
    const float fx = 0.185f, fz = -0.170f, bz = 0.175f;
    for (int i = 0; i < 2; ++i) {
        const float x = i == 0 ? -fx : fx;
        b.setTransform(MatrixTranslate(x, 0, fz));
        b.lathe(leg, 10, false, false, c);
        b.setTransform(MatrixTranslate(x, 0, bz));
        b.lathe(leg, 10, false, false, c);
    }
    b.resetTransform();
    b.roundedBox({0, 0.440f, fz - 0.005f}, {0.41f, 0.036f, 0.030f}, 0.008f, 2, c);
    b.roundedBox({0, 0.440f, bz + 0.005f}, {0.41f, 0.036f, 0.030f}, 0.008f, 2, c);
    b.roundedBox({-fx, 0.440f, 0.0f}, {0.030f, 0.036f, 0.38f}, 0.008f, 2, c);
    b.roundedBox({fx, 0.440f, 0.0f}, {0.030f, 0.036f, 0.38f}, 0.008f, 2, c);
    for (int i = 0; i < 2; ++i) {
        const float x = i == 0 ? -fx : fx;
        b.capsule({x, 0.44f, bz}, {x * 1.02f, 0.935f, bz + 0.055f}, 0.0165f, 8, c);
        b.sphere({x * 1.02f, 0.955f, bz + 0.057f}, 0.020f, 6, 8, c);
    }
    b.setTransform(MatrixMultiply(MatrixRotateX(-8.f * DEG2RAD), MatrixTranslate(0, 0.865f, bz + 0.048f)));
    b.roundedBox({0, 0, 0}, {0.39f, 0.080f, 0.022f}, 0.009f, 2, c);
    b.setTransform(MatrixMultiply(MatrixRotateX(-6.f * DEG2RAD), MatrixTranslate(0, 0.70f, bz + 0.030f)));
    b.roundedBox({0, 0, 0}, {0.38f, 0.042f, 0.018f}, 0.007f, 2, c);
    b.resetTransform();
    b.capsule({-fx, 0.16f, fz}, {-fx, 0.16f, bz}, 0.0085f, 6, c);
    b.capsule({fx, 0.16f, fz}, {fx, 0.16f, bz}, 0.0085f, 6, c);
    b.capsule({-fx, 0.12f, fz}, {fx, 0.12f, fz}, 0.0085f, 6, c);
    b.capsule({-fx, 0.20f, bz}, {fx, 0.20f, bz}, 0.0085f, 6, c);
    frame = b.build(true);
    MeshBuilder s;
    s.roundedBox({0, 0.451f, 0.0f}, {0.345f, 0.018f, 0.325f}, 0.009f, 2, WHITE);
    seat = s.build(true);
}

}  // namespace

// ============================================================================ venue
void Room::setVenue(int mode) {
    Impl& I = *impl_;
    mode = std::clamp(mode, 0, 2);
    if (mode == I.venueMode) return;
    I.venueMode = mode;
    if (I.ready) I.evalVenue(true);  // (ozelgun) the player's own choice: at once
}
int Room::venue() const { return impl_->garden ? 1 : 0; }

bool Room::Impl::resolveGarden() const {
    if (venueMode == 0) return false;
    if (venueMode == 1) return true;
    if (const char* e = std::getenv("SAKLI_MEKAN")) {
        const int v = std::atoi(e);
        if (v == 1) return false;
        if (v == 2) return true;
    }
    // otomatik: the garden is open on fair spring and summer days and evenings
    const int se = w3d::resolveSeason(seasonMode), ph = w3d::resolveDayPhase(dayMode);
    if (se != w3d::Ilkbahar && se != w3d::Yaz) return false;
    if (ph == w3d::Gece) return false;
    if (specialDay == w3d::GunMac) return false;                 // (ozelgun) a derby night: everyone at the TV inside
    if (rainLatch && rainLatchPhase == phase) return false;      // (ozelgun) the rain sent us in: in until the phase changes
    return rainTarget <= 0.f;
}

void Room::Impl::evalVenue(bool force) {
    const bool g = resolveGarden();
    // (ozelgun) while App holds the venue (a hand in play) otomatik only notes where it wants to be; App moves us between
    // hands (Room::applyVenue). Why: 1 rain, 2 the derby, 3 the clock / the season.
    if (!force && venueHold) {
        venuePend = g == garden ? 0 : (!g && rainTarget > 0.f) ? 1 : (!g && specialDay == w3d::GunMac) ? 2 : 3;
        return;
    }
    venuePend = 0;
    if (g == garden) return;
    if (garden && !g && rainTarget > 0.f && venueMode == 2) {  // (ozelgun)
        rainLatch = true;
        rainLatchPhase = phase;
    }
    garden = g;
    if (garden && !gd) initGarden();
    if (!garden) warmUpSmoke();  // back inside: the air under the ceiling is smoky again
}

// ============================================================================ init / free
void Room::Impl::initGarden() {
    if (gd) return;
    gd = new Garden;
    Garden& G = *gd;
    Renderer& r = *R;
    G.texGround = genGroundTexture(seed + 301);
    G.texCobble = genCobbleTexture(seed + 302);
    G.texRubble = genRubbleTexture(seed + 303);
    G.texWhite = genPlasterTexture(512, Color{238, 234, 222, 255}, seed + 304);
    G.texFacade = genPlasterTexture(512, Color{222, 190, 140, 255}, seed + 305);
    G.texBark = genBarkTexture(seed + 306);
    G.texRoof = genRoofTileTexture(seed + 307);
    G.texAwning = genAwningTexture(seed + 308);
    G.texShutter = genShutterTexture(seed + 309);
    G.texStraw = genStrawTexture(seed + 310);
    G.cvPano = makeCanvas(PANO_W, PANO_H);
    G.cvSigns = makeCanvas(GSIGN_W, GSIGN_H);
    drawGardenSigns(G.cvSigns, seed);

    G.mGround = r.makeMat(WHITE, G.texGround, 0.05f, 8.f);
    G.mCobble = r.makeMat(WHITE, G.texCobble, 0.22f, 30.f);
    G.mRubble = r.makeMat(WHITE, G.texRubble, 0.06f, 10.f);
    G.mWhite = r.makeMat(WHITE, G.texWhite, 0.04f, 8.f);
    G.mFacade = r.makeMat(WHITE, G.texFacade, 0.05f, 8.f);
    G.mBark = r.makeMat(WHITE, G.texBark, 0.08f, 12.f, 0.f, 0.1f);
    G.mRoof = r.makeMat(WHITE, G.texRoof, 0.12f, 16.f);
    G.mAwning = r.makeMat(WHITE, G.texAwning, 0.05f, 8.f, 0.f, 0.3f);
    G.mShutter = r.makeMat(WHITE, G.texShutter, 0.15f, 16.f);
    G.mPano = r.makeMat(WHITE, G.cvPano.texture, 0.f, 1.f, 1.f);
    G.mSky = r.makeMat(WHITE, Texture2D{}, 0.f, 1.f, 1.f);
    G.mSigns = r.makeMat(WHITE, G.cvSigns.texture, 0.3f, 30.f, 0.05f);
    G.mLeaf = r.makeMat(WHITE, Texture2D{}, 0.18f, 20.f, 0.f, 0.35f);   // the çınar's and the vines' leaves (vertex colours)
    G.mBlossom = r.makeMat(WHITE, Texture2D{}, 0.05f, 8.f, 0.1f, 0.3f);
    G.mWindow = r.makeMat(WHITE, G.cvSigns.texture, 0.85f, 120.f, 0.f);
    G.mStrBulb = r.makeMat(Color{255, 236, 196, 255}, Texture2D{}, 0.6f, 60.f, 0.f, 0.3f);
    G.mClay = r.makeMat(WHITE, Texture2D{}, 0.1f, 12.f);
    G.mBird = r.makeMat(WHITE, Texture2D{}, 0.12f, 14.f, 0.f, 0.3f);
    G.mCoal = r.makeMat(Color{255, 120, 40, 255}, Texture2D{}, 0.f, 1.f, 1.f);
    G.mStraw = r.makeMat(WHITE, G.texStraw, 0.08f, 10.f, 0.f, 0.1f);
    G.mChairWood = r.makeMat(WHITE, texWoodDark, 0.25f, 28.f, 0.f, 0.08f);
    G.mStar = r.makeMat(WHITE, Texture2D{}, 0.f, 1.f, 1.f);
    buildGardenChair(G.chair, G.chairSeat);
    buildGarden();
    buildTree();
    buildFoliage();
    initGardenLife();
    // more rain streaks: the garden sees much more of the sky than the windows do
    {
        okey::Rng wr(seed * 31u + 0x9a7du);
        const size_t n0 = streaks.size();
        if (n0 < 340) {
            streaks.resize(340);
            for (size_t i = n0; i < streaks.size(); ++i) placeStreak(streaks[i], wr, true);
        }
    }
    G.built = true;
    G.sunDir = G.sunDirTarget;
    updateGarden(0.f);
}

void Room::Impl::freeGarden(Renderer& r) {
    if (!gd) return;
    Garden& G = *gd;
    freeGardenLife(r);
    auto um = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    for (Mesh* m : {&G.sky, &G.pano, &G.chair, &G.chairSeat, &G.awning, &G.windows, &G.strBulbs, &G.coals, &G.erguvan, &G.groundLeaves})
        um(*m);
    for (Garden::Sway& s : G.canopy) um(s.mesh);
    for (Garden::Sway& s : G.vines) um(s.mesh);
    for (Mat* m : {&G.mGround, &G.mCobble, &G.mRubble, &G.mWhite, &G.mFacade, &G.mBark, &G.mRoof, &G.mAwning, &G.mShutter, &G.mPano, &G.mSky,
                   &G.mSigns, &G.mLeaf, &G.mBlossom, &G.mWindow, &G.mStrBulb, &G.mClay, &G.mBird, &G.mCoal, &G.mStraw, &G.mChairWood, &G.mStar})
        r.unloadMat(*m);
    for (Texture2D* t : {&G.texGround, &G.texCobble, &G.texRubble, &G.texWhite, &G.texFacade, &G.texBark, &G.texRoof, &G.texAwning,
                         &G.texShutter, &G.texStraw})
        if (t->id) UnloadTexture(*t);
    for (RenderTexture2D* c : {&G.cvPano, &G.cvSigns})
        if (c->id) UnloadRenderTexture(*c);
    // (the garden's statics live in `statics` and are freed with the room's)
    delete gd;
    gd = nullptr;
}

// ============================================================================ static geometry
void Room::Impl::buildGarden() {
    Garden& Gd = *gd;
    GardenBuilders G;
    okey::Rng rng(seed + 4242);
    const float AW = ART_W, AH = ART_H;
    const Vector3 trunk{w3d::SCOREBOARD_POS.x, 0.f, -4.12f};
    Gd.trunkBase = trunk;

    // ---- ground: sandy mortar, cobbles over it (not in the tree pit, not under the counter)
    {
        const float x0 = GX0, x1 = GX1, z0 = GZB, z1 = GZF;
        quadUV(G.ground, {x0, 0.f, z1}, {x1, 0.f, z1}, {x1, 0.f, z0}, {x0, 0.f, z0}, {x0, z1}, {x1, z1}, {x1, z0}, {x0, z0}, WHITE);
        // the gate's threshold out to the lane
        quadUV(G.ground, {GX0 - WALL_T - 0.2f, 0.f, GATE_Z1}, {GX0, 0.f, GATE_Z1}, {GX0, 0.f, GATE_Z0}, {GX0 - WALL_T - 0.2f, 0.f, GATE_Z0},
               {0, 0}, {1, 0}, {1, 1}, {0, 1}, WHITE);
        auto skip = [&](float x, float z) {
            if (std::hypot(x - trunk.x, z - trunk.z) < 1.18f) return true;                // the tree pit
            if (x > 2.0f && x < 4.0f && z > -3.52f && z < -2.9f) return true;              // the counter
            if (x > KIOSK_X0 - 0.1f && z < -3.38f) return true;                            // behind the kiosk wall
            return false;
        };
        cobbles(G.cobble, x0, z0, x1, -1.0f, skip, rng);  // (two meshes: each stays below 65536 vertices)
        cobbles(G.cobble2, x0, -1.0f, x1, z1, skip, rng);
        // the tree pit's kerb: a ring of upright stones
        for (int i = 0; i < 26; ++i) {
            const float a = 2.f * PI * i / 26.f;
            const Vector3 p{trunk.x + std::cos(a) * 1.22f, 0.05f, trunk.z + std::sin(a) * 1.22f};
            G.stone.setTransform(MatrixMultiply(MatrixRotateY(-a), translate(p)));
            G.stone.roundedBox({0, 0, 0}, {0.1f, 0.1f, 0.27f}, 0.025f, 1, scaleRgb(Color{200, 190, 170, 255}, rng.uniform(0.85f, 1.05f)));
            G.stone.resetTransform();
        }
        // a low stone seat round the back half of the trunk (seki)
        for (int i = 0; i < 9; ++i) {
            const float a = PI * 1.08f + PI * 0.84f * (i + 0.5f) / 9.f;  // behind (toward -Z)
            const Vector3 p{trunk.x + std::cos(a) * 1.55f, 0.22f, trunk.z + std::sin(a) * 1.55f};
            G.stoneCast.setTransform(MatrixMultiply(MatrixRotateY(-a), translate(p)));
            G.stoneCast.roundedBox({0, 0, 0}, {0.42f, 0.44f, 0.56f}, 0.03f, 1, scaleRgb(Color{214, 204, 184, 255}, rng.uniform(0.9f, 1.03f)));
            G.stoneCast.resetTransform();
        }
    }

    // ---- the low rubble wall on the left with the gate, the parapet over the sea
    {
        const float xw = GX0 - WALL_T * 0.5f;
        stoneWall(G, {xw, 0, GZB - WALL_T}, {xw, 0, 2.0f}, 1.0f, WALL_T, rng);
        stoneWall(G, {xw, 0, GATE_Z1 + 0.35f}, {xw, 0, GZF + WALL_T}, 1.0f, WALL_T, rng);
        for (float z : {2.0f + 0.175f, GATE_Z1 + 0.175f}) {  // gate piers with caps
            boxW(G.stoneCast, {xw, 0.8f, z}, {0.42f, 1.6f, 0.36f}, 1.6f, WHITE, 2);
            rbox(G.stone, {xw, 1.64f, z}, {0.5f, 0.08f, 0.44f}, 0.015f, 1, Color{228, 220, 200, 255});
            pottedPlant(G, {xw, 1.68f, z}, 1.1f, z < 2.5f ? 0 : 1, false, rng);
        }
        // the gate: two plank leaves swung open against the outside of the wall
        for (int k = 0; k < 2; ++k) {
            const float zh = k == 0 ? 2.35f : 3.25f, dir = k == 0 ? -1.f : 1.f;
            const float yaw = dir * 78.f;
            Matrix M = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), translate({GX0 - WALL_T - 0.03f, 0.f, zh}));
            G.woodCast.setTransform(M);
            for (int p = 0; p < 4; ++p)
                G.woodCast.roundedBox({-0.06f - p * 0.105f, 0.62f, 0.f}, {0.1f, 1.12f, 0.03f}, 0.008f, 1,
                                      scaleRgb(Color{120, 132, 118, 255}, rng.uniform(0.85f, 1.05f)));
            for (float y : {0.25f, 0.98f}) G.woodCast.roundedBox({-0.22f, y, 0.025f}, {0.44f, 0.08f, 0.025f}, 0.006f, 1, Color{104, 116, 102, 255});
            G.woodCast.resetTransform();
        }
        // the chalk board on the gate pier, facing the garden
        canvasQuad(G.signs, {GX0 + 0.012f, 1.12f, 2.175f}, 0.3f, 0.25f, {1, 0, 0}, gsign::MENU, GSIGN_W, GSIGN_H);
        rbox(G.wood, {GX0 + 0.006f, 1.12f, 2.175f}, {0.012f, 0.27f, 0.32f}, 0.004f, 1, Color{90, 60, 40, 255});
        // the parapet
        stoneWall(G, {GX0 - WALL_T, 0, GZB - WALL_T * 0.5f}, {GX1, 0, GZB - WALL_T * 0.5f}, 0.85f, WALL_T, rng);
        // pots along the walls
        for (float z : {-4.9f, -2.7f, -1.3f, 0.6f, 1.55f}) pottedPlant(G, {xw, 1.07f, z}, rng.uniform(0.9f, 1.15f), rng.range(0, 3), rng.chance(0.25f), rng);
        for (float x : {-3.6f, -2.9f, 0.6f, 1.6f, 2.6f, 3.5f})
            pottedPlant(G, {x, 0.92f, GZB - WALL_T * 0.5f}, rng.uniform(0.9f, 1.2f), rng.range(0, 3), rng.chance(0.2f), rng);
    }

    // ---- the front wall: whitewashed, tall (a hidden garden), a tiled cap; pots at its foot; the bench
    {
        const float zc = GZF + WALL_T * 0.5f, h = 1.9f;
        boxW(G.whiteCast, {(GX0 - WALL_T + GX1) * 0.5f, h * 0.5f, zc}, {GX1 - GX0 + WALL_T, h, WALL_T}, 2.f, WHITE, 0);
        // tiled coping: a little gable of tiles along the top
        for (int s = -1; s <= 1; s += 2) {
            const float x0 = GX0 - WALL_T, x1 = GX1;
            Vector3 a{x0, h + 0.13f, zc}, b{x1, h + 0.13f, zc}, c{x1, h - 0.02f, zc + s * (WALL_T * 0.5f + 0.08f)},
                d{x0, h - 0.02f, zc + s * (WALL_T * 0.5f + 0.08f)};
            if (s > 0) quadUV(G.roof, a, d, c, b, {0, 0}, {0, 0.5f}, {(x1 - x0) / 0.64f, 0.5f}, {(x1 - x0) / 0.64f, 0}, WHITE);
            else quadUV(G.roof, a, b, c, d, {0, 0}, {(x1 - x0) / 0.64f, 0}, {(x1 - x0) / 0.64f, 0.5f}, {0, 0.5f}, WHITE);
        }
        // a skirting of grey limewash at the foot (splashes)
        boxW(G.paint, {(GX0 + GX1) * 0.5f, 0.12f, GZF - 0.004f}, {GX1 - GX0, 0.24f, 0.01f}, 1.f, Color{176, 172, 162, 255}, 0);
        // the bench along the wall (the cat's map knows it: x -2.17 .. 0.47)
        {
            const float bx0 = -2.12f, bx1 = 0.42f, bz = 3.4f, sy = 0.45f;
            rbox(G.woodCast, {(bx0 + bx1) * 0.5f, sy - 0.02f, bz}, {bx1 - bx0, 0.05f, 0.42f}, 0.01f, 2, Color{132, 90, 58, 255});
            for (float x : {bx0 + 0.08f, (bx0 + bx1) * 0.5f, bx1 - 0.08f})
                for (float z : {bz - 0.16f, bz + 0.16f}) rbox(G.woodCast, {x, (sy - 0.045f) * 0.5f, z}, {0.06f, sy - 0.045f, 0.06f}, 0.01f, 2, Color{110, 74, 48, 255});
            // kilim cushions
            for (int k = 0; k < 3; ++k) {
                const float cx = bx0 + (k + 0.5f) * (bx1 - bx0) / 3.f;
                rbox(G.cloth, {cx, sy + 0.035f, bz}, {(bx1 - bx0) / 3.f - 0.04f, 0.07f, 0.38f}, 0.03f, 2, Color{150, 60, 50, 255});
                canvasQuadUp(G.art, {cx, sy + 0.071f, bz}, (bx1 - bx0) / 3.f - 0.08f, 0.34f, art::KILIM, AW, AH, 0.f);
            }
        }
        for (float x : {-3.7f, -3.25f, 1.0f, 1.45f, 2.2f, 3.0f})
            pottedPlant(G, {x, 0.f, GZF - 0.18f}, rng.uniform(1.2f, 1.6f), rng.range(0, 3), rng.chance(0.3f), rng);
        // geraniums on the cap
        for (float x : {-3.9f, -1.6f, 0.9f, 3.4f}) pottedPlant(G, {x, h + 0.13f, zc}, 0.9f, rng.range(0, 1), false, rng);
    }

    // ---- the kahvehane's front (x = GX1, facing -X): plinth, plaster, door, windows with shutters, the sign, the eave
    {
        const Vector3 n{-1, 0, 0}, right{0, 0, 1};
        const float z0 = GZB - WALL_T, z1 = GZF + WALL_T, top = 6.3f;
        struct Op {
            float z0, z1, y0, y1;
            int kind;  // 0 window, 1 door, 2 upper window (shutters closed or open)
        };
        const Op ops[] = {{-2.25f, -1.25f, 0.95f, 2.3f, 0}, {-0.62f, 0.56f, 0.f, 2.3f, 1}, {1.32f, 2.32f, 0.95f, 2.3f, 0},
                          {-2.2f, -1.3f, 3.6f, 4.95f, 2}, {-0.48f, 0.42f, 3.6f, 4.95f, 2}, {1.37f, 2.27f, 3.6f, 4.95f, 2},
                          {-4.75f, -3.85f, 3.6f, 4.95f, 2}};
        std::vector<Hole> holes;
        for (const Op& o : ops) holes.push_back({o.z0, o.z1, o.y0, o.y1});
        // plaster above the plinth, the plinth in stone
        wallFace(G.facade, {GX1, 0, 0}, right, n, z0, z1, 0.45f, top, holes, 2.2f, WHITE);
        std::vector<Hole> lo;
        for (const Op& o : ops) lo.push_back({o.z0, o.z1, o.y0, o.y1});
        {
            // the plinth stands a little proud of the wall
            MeshBuilder& S = G.stone;
            wallFace(S, {GX1 - 0.03f, 0, 0}, right, n, z0, z1, 0.f, 0.45f, lo, 1.6f, WHITE);
            rbox(G.stone, {GX1 - 0.03f, 0.46f, (z0 + z1) * 0.5f}, {0.08f, 0.04f, z1 - z0}, 0.01f, 1, Color{220, 212, 194, 255});
        }
        // cornice and eave
        rbox(G.white, {GX1 - 0.08f, top - 0.08f, (z0 + z1) * 0.5f}, {0.18f, 0.16f, z1 - z0 + 0.2f}, 0.02f, 1, Color{236, 230, 216, 255});
        rbox(G.white, {GX1 - 0.03f, 3.28f, (z0 + z1) * 0.5f}, {0.08f, 0.06f, z1 - z0}, 0.01f, 1, Color{236, 230, 216, 255});  // floor band
        {
            const float ex = GX1 - 0.45f, ey = top + 0.02f, rx = GX1 + 1.4f, ry = top + 1.15f;
            quadUV(G.roofCast, {ex, ey, z0 - 0.3f}, {ex, ey, z1 + 0.3f}, {rx, ry, z1 + 0.3f}, {rx, ry, z0 - 0.3f}, {0, 0}, {(z1 - z0 + 0.6f) / 0.64f, 0},
                   {(z1 - z0 + 0.6f) / 0.64f, 1.2f}, {0, 1.2f}, WHITE);
            // underside of the eave (boards)
            quadUV(G.wood, {ex, ey - 0.02f, z0 - 0.3f}, {GX1, ey + 0.25f, z0 - 0.3f}, {GX1, ey + 0.25f, z1 + 0.3f}, {ex, ey - 0.02f, z1 + 0.3f},
                   {0, 0}, {1, 0}, {1, 6}, {0, 6}, Color{150, 120, 90, 255});
            rbox(G.wood, {ex, ey - 0.04f, (z0 + z1) * 0.5f}, {0.04f, 0.1f, z1 - z0 + 0.6f}, 0.01f, 1, Color{120, 90, 64, 255});  // fascia
        }
        // drainpipe
        G.metal.cylinder({GX1 - 0.08f, 0.f, GZF + 0.05f}, 0.04f, top, 10, Color{120, 124, 126, 255});
        MeshBuilder winGlass;
        for (const Op& o : ops) {
            const float zc = (o.z0 + o.z1) * 0.5f, yc = (o.y0 + o.y1) * 0.5f, W = o.z1 - o.z0, H = o.y1 - o.y0;
            const float rev = 0.24f;
            // reveals (plaster) and the sill / lintel
            quadUV(G.white, {GX1, o.y0, o.z0}, {GX1 + rev, o.y0, o.z0}, {GX1 + rev, o.y1, o.z0}, {GX1, o.y1, o.z0}, {0, 0}, {0.2f, 0}, {0.2f, 1}, {0, 1},
                   Color{228, 220, 204, 255});
            quadUV(G.white, {GX1 + rev, o.y0, o.z1}, {GX1, o.y0, o.z1}, {GX1, o.y1, o.z1}, {GX1 + rev, o.y1, o.z1}, {0, 0}, {0.2f, 0}, {0.2f, 1}, {0, 1},
                   Color{228, 220, 204, 255});
            quadUV(G.white, {GX1, o.y1, o.z0}, {GX1 + rev, o.y1, o.z0}, {GX1 + rev, o.y1, o.z1}, {GX1, o.y1, o.z1}, {0, 0}, {0.2f, 0}, {0.2f, 1}, {0, 1},
                   Color{214, 206, 190, 255});
            rbox(G.stone, {GX1 - 0.02f, o.y1 + 0.06f, zc}, {0.06f, 0.12f, W + 0.16f}, 0.01f, 1, Color{224, 214, 194, 255});  // lintel
            if (o.kind != 1) rbox(G.stoneCast, {GX1 - 0.04f, o.y0 - 0.03f, zc}, {0.12f, 0.06f, W + 0.16f}, 0.01f, 1, Color{226, 218, 200, 255});
            const float gx = GX1 + rev * 0.6f;
            if (o.kind == 1) {
                // the doorway: the warm room seen past a curtain of coloured plastic strips
                canvasQuad(winGlass, {GX1 + rev, yc, zc}, W, H, n, gsign::WINDOW, GSIGN_W, GSIGN_H);
                rbox(G.stone, {GX1 + 0.02f, 0.02f, zc}, {0.4f, 0.04f, W + 0.1f}, 0.01f, 1, Color{210, 204, 190, 255});  // threshold
                const Color strips[4] = {{200, 40, 36, 255}, {230, 190, 40, 255}, {50, 130, 70, 255}, {40, 90, 170, 255}};
                const int ns = (int)(W / 0.06f);
                for (int i = 0; i < ns; ++i) {
                    const float z = o.z0 + (i + 0.5f) * W / ns;
                    G.paint.box({GX1 + 0.1f, (o.y1 - 0.1f) * 0.5f + 0.06f, z}, {0.006f, o.y1 - 0.12f, W / ns - 0.006f}, strips[i % 4]);
                }
                rbox(G.wood, {GX1 + 0.08f, o.y1 - 0.04f, zc}, {0.04f, 0.06f, W}, 0.01f, 1, Color{100, 70, 46, 255});
                // door leaves folded back into the reveal
                for (int s = 0; s < 2; ++s)
                    rbox(G.woodCast, {GX1 + rev * 0.5f, H * 0.5f, s ? o.z1 - 0.03f : o.z0 + 0.03f}, {rev - 0.02f, H - 0.04f, 0.04f}, 0.01f, 1,
                         Color{96, 66, 44, 255});
                continue;
            }
            const bool lit = o.kind == 0 || (int)(zc * 10.f) % 3 == 0;
            if (lit) canvasQuad(winGlass, {gx, yc, zc}, W, H, n, gsign::WINDOW, GSIGN_W, GSIGN_H);
            else wbox(G.paint, {gx + 0.02f, yc, zc}, {0.02f, H, W}, 1.f, Color{40, 44, 52, 255});
            // the frame: cream bars, a cross
            const Color fc{226, 220, 204, 255};
            const float bw = 0.05f;
            rbox(G.paint, {gx - 0.02f, yc, o.z0 + bw * 0.5f}, {0.04f, H, bw}, 0.006f, 1, fc);
            rbox(G.paint, {gx - 0.02f, yc, o.z1 - bw * 0.5f}, {0.04f, H, bw}, 0.006f, 1, fc);
            rbox(G.paint, {gx - 0.02f, o.y0 + bw * 0.5f, zc}, {0.04f, bw, W}, 0.006f, 1, fc);
            rbox(G.paint, {gx - 0.02f, o.y1 - bw * 0.5f, zc}, {0.04f, bw, W}, 0.006f, 1, fc);
            rbox(G.paint, {gx - 0.02f, yc, zc}, {0.035f, H, bw * 0.7f}, 0.006f, 1, fc);
            rbox(G.paint, {gx - 0.02f, o.y0 + H * 0.68f, zc}, {0.035f, bw * 0.7f, W}, 0.006f, 1, fc);
            // shutters: open against the wall on both sides (upper floor: one window closed)
            const bool closed = o.kind == 2 && !lit;
            for (int s = 0; s < 2; ++s) {
                const float sw = W * 0.5f;
                if (closed) {
                    const float zz = s ? zc + sw * 0.5f : zc - sw * 0.5f;
                    canvasQuad(G.shutter, {GX1 + 0.05f, yc, zz}, sw - 0.01f, H, n, {0, 0, 128, 256}, 128, 256);
                } else {
                    const float zz = s ? o.z1 + sw * 0.5f + 0.02f : o.z0 - sw * 0.5f - 0.02f;
                    canvasQuad(G.shutter, {GX1 - 0.025f, yc, zz}, sw, H, n, {0, 0, 128, 256}, 128, 256);
                    rbox(G.paint, {GX1 - 0.012f, yc, zz}, {0.02f, H, sw}, 0.004f, 1, Color{70, 110, 88, 255});
                }
            }
            // flower box under the upper windows that stand open
            if (o.kind == 2 && lit) {
                rbox(G.woodCast, {GX1 - 0.12f, o.y0 - 0.06f, zc}, {0.2f, 0.16f, W}, 0.01f, 1, Color{110, 70, 44, 255});
                for (int k = 0; k < 4; ++k) pottedPlant(G, {GX1 - 0.12f, o.y0 + 0.0f, o.z0 + (k + 0.5f) * W / 4.f}, 0.7f, k % 2, true, rng);
            }
        }
        Gd.windows = winGlass.build(true);
        // the shop sign over the door and the street plate
        canvasQuad(G.signs, {GX1 - 0.035f, 2.64f, -0.03f}, 2.2f, 0.43f, n, gsign::SIGN, GSIGN_W, GSIGN_H);
        rbox(G.woodCast, {GX1 - 0.02f, 2.64f, -0.03f}, {0.03f, 0.47f, 2.26f}, 0.008f, 1, Color{40, 60, 46, 255});
        canvasQuad(G.signs, {GX1 - 0.012f, 2.05f, 0.95f}, 0.36f, 0.14f, n, gsign::PLATE, GSIGN_W, GSIGN_H);
        // big pots by the door (clear of the çaycı's way along the front)
        pottedPlant(G, {GX1 - 0.2f, 0.f, 0.86f}, 1.6f, 0, false, rng);
        pottedPlant(G, {GX1 - 0.2f, 0.f, 1.18f}, 1.3f, 2, false, rng);
    }

    // ---- the ocak's corner: a whitewashed back wall (the counter's shelves and boards hang on it) and a tiled roof
    {
        const float zb = w3d::ROOM_Z0, h = 3.05f;
        boxW(G.whiteCast, {(KIOSK_X0 + GX1) * 0.5f, h * 0.5f, zb - 0.11f}, {GX1 - KIOSK_X0, h, 0.22f}, 2.f, WHITE, 0);
        boxW(G.whiteCast, {KIOSK_X0 - 0.11f, h * 0.5f, zb - 0.11f + 0.0f}, {0.22f, h, 0.22f}, 2.f, WHITE, 2);
        boxW(G.paint, {(KIOSK_X0 + GX1) * 0.5f, 0.15f, zb + 0.004f}, {GX1 - KIOSK_X0, 0.3f, 0.01f}, 1.f, Color{150, 120, 96, 255}, 0);
        // the lean-to roof over the counter, on two posts
        const float zr0 = zb - 0.22f, zr1 = -2.42f, yr0 = 2.86f, yr1 = 2.52f;
        quadUV(G.roofCast, {KIOSK_X0 - 0.3f, yr0, zr0}, {KIOSK_X0 - 0.3f, yr1, zr1}, {GX1, yr1, zr1}, {GX1, yr0, zr0}, {0, 0}, {0, 2.f},
               {(GX1 - KIOSK_X0 + 0.3f) / 0.64f, 2.f}, {(GX1 - KIOSK_X0 + 0.3f) / 0.64f, 0}, WHITE);
        quadUV(G.wood, {KIOSK_X0 - 0.3f, yr0 - 0.03f, zr0}, {GX1, yr0 - 0.03f, zr0}, {GX1, yr1 - 0.03f, zr1}, {KIOSK_X0 - 0.3f, yr1 - 0.03f, zr1},
               {0, 0}, {6, 0}, {6, 1}, {0, 1}, Color{140, 110, 80, 255});
        rbox(G.woodCast, {(KIOSK_X0 + GX1) * 0.5f - 0.15f, yr1 - 0.06f, zr1 + 0.02f}, {GX1 - KIOSK_X0 + 0.3f, 0.1f, 0.1f}, 0.01f, 1, Color{110, 80, 56, 255});
        for (float x : {KIOSK_X0 - 0.2f, GX1 - 0.12f})
            rbox(G.woodCast, {x, (yr1 - 0.1f) * 0.5f, zr1 + 0.02f}, {0.1f, yr1 - 0.1f, 0.1f}, 0.01f, 2, Color{110, 80, 56, 255});
    }

    // ---- the vine trellis: posts, beams on them, rafters across (the pendants hang from them), battens on top
    {
        const Color tw{118, 98, 80, 255};
        const float bY = TRELLIS_Y - 0.2f;  // beams
        auto post = [&](float x, float z, float y0) {
            rbox(G.woodCast, {x, (y0 + bY) * 0.5f, z}, {0.12f, bY - y0, 0.12f}, 0.015f, 2, scaleRgb(tw, rng.uniform(0.9f, 1.05f)));
        };
        const float xl = GX0 - 0.05f;
        for (float z : {-3.35f, -0.4f}) post(xl, z, 1.0f);
        post(xl, 2.175f, 1.68f);
        post(xl, 3.425f, 1.68f);
        post(0.45f, -3.35f, 0.f);
        for (float x : {-2.45f, 1.05f}) post(x, GZF - 0.06f, 0.f);
        // beams: left side (along Z), back (along X to the kiosk), front (along X), ledger on the facade
        rbox(G.woodCast, {xl, bY + 0.08f, (-3.35f + GZF) * 0.5f}, {0.14f, 0.16f, GZF + 3.35f + 0.2f}, 0.015f, 2, tw);
        rbox(G.woodCast, {(xl + KIOSK_X0) * 0.5f, bY + 0.08f, -3.35f}, {KIOSK_X0 - xl + 0.2f, 0.16f, 0.14f}, 0.015f, 2, tw);
        rbox(G.woodCast, {(xl + GX1) * 0.5f, bY + 0.08f, GZF - 0.06f}, {GX1 - xl + 0.1f, 0.16f, 0.14f}, 0.015f, 2, tw);
        rbox(G.woodCast, {GX1 - 0.07f, bY + 0.08f, (-3.35f + GZF) * 0.5f}, {0.14f, 0.16f, GZF + 3.6f}, 0.015f, 2, scaleRgb(tw, 0.9f));
        // rafters across the beams (the pendants' cords end in them)
        for (float x : {-2.55f, 0.f, 2.45f})
            rbox(G.woodCast, {x, TRELLIS_Y + 0.0f, (-3.35f + GZF) * 0.5f}, {0.1f, 0.12f, GZF + 3.35f + 0.25f}, 0.012f, 2, tw);
        // battens
        for (float z = -3.0f; z < GZF - 0.2f; z += 0.72f)
            rbox(G.woodCast, {(xl + GX1) * 0.5f, TRELLIS_Y + 0.08f, z}, {GX1 - xl, 0.04f, 0.05f}, 0.008f, 1, scaleRgb(tw, 1.08f));
    }

    // ---- the mangal (copper brazier on a stand) where the room's stove stands: lit in winter under the awning
    {
        const Vector3 p{3.8f, 0.f, 2.55f};
        const Color cu{184, 104, 60, 255};
        for (int k = 0; k < 3; ++k) {
            const float a = k * 2.f * PI / 3.f;
            G.metalCast.capsule({p.x + std::cos(a) * 0.24f, 0.f, p.z + std::sin(a) * 0.24f}, {p.x + std::cos(a) * 0.16f, 0.5f, p.z + std::sin(a) * 0.16f},
                                0.012f, 6, Color{50, 46, 44, 255});
        }
        latheAt(G.brass, {p.x, 0.48f, p.z}, {{0.f, 0.f}, {0.12f, 0.f}, {0.2f, 0.06f}, {0.27f, 0.16f}, {0.3f, 0.17f}, {0.29f, 0.18f}, {0.25f, 0.17f},
                                             {0.18f, 0.08f}, {0.f, 0.08f}},
                24, false, false, cu);
        ringTube(G.brass, {p.x, 0.66f, p.z}, 0.3f, 0.012f, 24, scaleRgb(cu, 1.1f));
        Gd.mangalTop = {p.x, 0.58f, p.z};
        MeshBuilder coal;
        for (int i = 0; i < 26; ++i) {
            const float a = rng.uniform(0.f, 2.f * PI), rr = rng.uniform(0.f, 0.2f);
            coal.sphere({p.x + std::cos(a) * rr, 0.58f + rng.uniform(0.f, 0.03f), p.z + std::sin(a) * rr}, rng.uniform(0.025f, 0.04f), 3, 5, WHITE);
        }
        Gd.coals = coal.build(true);
    }

    // ---- string lights: catenaries of bulbs under the trellis
    {
        MeshBuilder bl;
        const Vector3 runs[][2] = {{{GX0, 2.86f, -3.3f}, {GX1, 2.86f, -2.05f}},
                                   {{GX0, 2.86f, -0.4f}, {GX1, 2.86f, 0.62f}},
                                   {{GX0, 2.86f, 2.15f}, {GX1, 2.86f, 3.3f}},
                                   {{-1.25f, 3.4f, -3.45f}, {0.45f, 2.86f, -3.35f}}};
        for (const auto& run : runs) {
            const float len = Vector3Distance(run[0], run[1]);
            const float sag = 0.06f * len;
            std::vector<Vector3> wire;
            for (int i = 0; i <= 24; ++i) wire.push_back(sagPoint(run[0], run[1], i / 24.f, sag));
            G.paint.tube(wire, 0.004f, 4, Color{30, 30, 30, 255});
            const int nb = std::max(2, (int)(len / 0.5f));
            for (int i = 1; i < nb; ++i) {
                Vector3 p = sagPoint(run[0], run[1], (float)i / nb, sag);
                G.paint.cylinder({p.x, p.y - 0.035f, p.z}, 0.011f, 0.035f, 8, Color{40, 40, 40, 255});
                Vector3 c{p.x, p.y - 0.06f, p.z};
                bl.sphere(c, 0.022f, 6, 8, WHITE);
                Gd.bulbPos.push_back(c);
            }
        }
        Gd.strBulbs = bl.build(true);
    }

    // ---- the lane outside the gate: the street canvas across it (houses by day / night) and the road
    {
        const float SW = STREET_W, SH = STREET_H;
        // (only the shop row: the canvas' sky above its parapet is left to the garden's own sky and view)
        const float topY = 3.15f, pxm = street::LEFT.height / 8.f;
        const Rectangle src{street::LEFT.x, street::LEFT.y + (7.f - topY) * pxm, street::LEFT.width, (topY + 1.f) * pxm};
        canvasQuad(G.street, {-8.2f, (topY - 1.f) * 0.5f, -1.f}, 18.f, topY + 1.f, {1, 0, 0}, src, SW, SH);
        canvasQuadUp(G.street, {-6.3f, -0.03f, -1.f}, 18.f, 3.8f, street::ROAD, SW, SH, 90.f);
    }

    // ---- the awning (tente): striped canvas stretched over the trellis, sagging between the rafters, a scalloped
    //      valance along the open sides
    {
        MeshBuilder aw;
        const float x0 = GX0 - 0.25f, x1 = GX1, z0 = -3.6f, z1 = GZF + 0.2f, y = TRELLIS_Y + 0.13f;
        const int nx = 32, nz = 28;
        const float raft[5] = {x0, -2.55f, 0.f, 2.45f, x1};
        auto height = [&](float x, float z) {
            float sag = 0.f;
            for (int i = 0; i < 4; ++i)
                if (x >= raft[i] && x <= raft[i + 1]) {
                    float t = (x - raft[i]) / (raft[i + 1] - raft[i]);
                    sag = 0.12f * std::sin(t * PI);
                }
            const float edge = std::min(std::min(z - z0, z1 - z), 0.6f) / 0.6f;
            return y - sag * edge;
        };
        for (int j = 0; j <= nz; ++j)
            for (int i = 0; i <= nx; ++i) {
                const float x = x0 + (x1 - x0) * i / nx, z = z0 + (z1 - z0) * j / nz;
                aw.vertex({x, height(x, z), z}, {0, -1, 0}, {(x - x0) / 1.2f, (z - z0) / 2.f}, WHITE);
            }
        for (int j = 0; j < nz; ++j)
            for (int i = 0; i < nx; ++i) {
                const int a = j * (nx + 1) + i;
                aw.quad(a, a + 1, a + nx + 2, a + nx + 1);
            }
        // valance on the left and the back, scalloped
        auto valance = [&](Vector3 a, Vector3 b, Vector3 out) {
            const int n = (int)(Vector3Distance(a, b) / 0.3f);
            for (int i = 0; i < n; ++i) {
                const Vector3 p0 = Vector3Lerp(a, b, (float)i / n), p1 = Vector3Lerp(a, b, (float)(i + 1) / n);
                const Vector3 pm = Vector3Lerp(p0, p1, 0.5f);
                const float d = 0.26f;
                int i0 = aw.vertex(p0, out, {0, 0}, WHITE), i1 = aw.vertex(p1, out, {0.25f, 0}, WHITE);
                int i2 = aw.vertex({p1.x, p1.y - d * 0.7f, p1.z}, out, {0.25f, 0.13f}, WHITE);
                int i3 = aw.vertex({pm.x, pm.y - d, pm.z}, out, {0.125f, 0.13f}, WHITE);
                int i4 = aw.vertex({p0.x, p0.y - d * 0.7f, p0.z}, out, {0.f, 0.13f}, WHITE);
                aw.triangle(i0, i4, i3);
                aw.triangle(i0, i3, i1);
                aw.triangle(i1, i3, i2);
            }
        };
        valance({x0, y, z0}, {x0, y, z1}, {-1, 0, 0});
        valance({x0, y, z0}, {x1, y, z0}, {0, 0, -1});
        Gd.awning = aw.build(true);
    }

    // ---- statics (garden only)
    staticVenues = 2;
    addStatic(G.ground, &Gd.mGround, 0);
    addStatic(G.cobble, &Gd.mCobble, 0);
    addStatic(G.cobble2, &Gd.mCobble, 0);
    addStatic(G.stone, &Gd.mRubble, 0);
    addStatic(G.stoneCast, &Gd.mRubble, CastShadow);
    addStatic(G.white, &Gd.mWhite, 0);
    addStatic(G.whiteCast, &Gd.mWhite, CastShadow);
    addStatic(G.facade, &Gd.mFacade, 0);
    addStatic(G.roof, &Gd.mRoof, 0);
    addStatic(G.roofCast, &Gd.mRoof, CastShadow | DoubleSided);
    addStatic(G.wood, &mWoodDark, 0);
    addStatic(G.woodCast, &mWoodDark, CastShadow);
    addStatic(G.paint, &mPaint, 0);
    addStatic(G.paintCast, &mPaint, CastShadow);
    addStatic(G.metal, &mMetal, 0);
    addStatic(G.metalCast, &mMetal, CastShadow);
    addStatic(G.brass, &mBrass, CastShadow);
    addStatic(G.clay, &Gd.mClay, CastShadow);
    addStatic(G.leafy, &mLeaf, DoubleSided);
    addStatic(G.bark, &Gd.mBark, 0);
    addStatic(G.barkCast, &Gd.mBark, CastShadow);
    addStatic(G.cloth, &mCloth, 0);
    addStatic(G.art, &mArt, 0);
    addStatic(G.signs, &Gd.mSigns, 0);
    addStatic(G.shutter, &Gd.mShutter, 0);
    addStatic(G.street, &mStreet, NoFog | DoubleSided);
    staticVenues = 1;
}

// ============================================================================ per frame
void Room::Impl::updateGarden(float dt) {
    Garden& G = *gd;
    const int ph = phase, se = seasonNow;
    const bool overcast = rainTarget > 0.f || snowTarget > 0.5f;
    // the sun's direction (the way its light travels) for the phase: morning from the left over the lane (side-lit faces),
    // noon high from the left and a little in front, evening low and golden from the left (west)
    static const Vector3 kSun[4] = {{0.78f, -0.52f, 0.34f}, {0.36f, -0.88f, -0.30f}, {0.93f, -0.26f, -0.26f}, {0.2f, -0.9f, 0.3f}};
    G.sunDirTarget = Vector3Normalize(kSun[ph]);
    const float k = dt <= 0.f ? 1.f : 1.f - std::exp(-dt * 0.5f);
    G.sunDir = Vector3Normalize(Vector3Lerp(G.sunDir, G.sunDirTarget, k));
    const float tAwn = (rainTarget > 0.f || se == w3d::Kis) ? 1.f : 0.f;
    G.awningK = dt <= 0.f ? tAwn : G.awningK + (tAwn - G.awningK) * std::min(1.f, dt * 0.8f);
    const float tBulbs = ph >= w3d::Aksam ? 1.f : 0.f;
    G.bulbsK = dt <= 0.f ? tBulbs : G.bulbsK + (tBulbs - G.bulbsK) * std::min(1.f, dt * 1.5f);
    const float tMangal = se == w3d::Kis ? 1.f : 0.f;
    G.mangalK = dt <= 0.f ? tMangal : G.mangalK + (tMangal - G.mangalK) * std::min(1.f, dt * 0.3f);
    G.wind = 0.5f + 0.5f * noise1(time * 0.13f, seed + 901u);
    // the view and the sky follow the light
    const float sunAz = std::atan2(-G.sunDirTarget.x, G.sunDirTarget.z) * RAD2DEG;  // azimuth the sun stands at (0 = -Z)
    if (G.panoPhase != ph || G.panoSeason != se || G.panoOvercast != overcast) {
        drawGardenView(G.cvPano, seed, ph, se, overcast, sunAz);
        G.panoPhase = ph, G.panoSeason = se, G.panoOvercast = overcast;
    }
    if (G.skyPhase != ph || G.skyOvercast != overcast || G.pano.vertexCount == 0) {
        if (G.sky.vertexCount > 0) UnloadMesh(G.sky);
        MeshBuilder b;
        const int rings = 16, segs = 36;
        const float R = kPanoR + 4.f;
        for (int j = 0; j <= rings; ++j) {
            const float e = -12.f + (90.f + 12.f) * j / rings;
            const Color c = gardenSky(ph, overcast, std::max(e, 0.f));
            for (int i = 0; i <= segs; ++i) {
                const float a = 2.f * PI * i / segs;
                const float ce = std::cos(e * DEG2RAD);
                Vector3 p{std::sin(a) * R * ce, std::sin(e * DEG2RAD) * R, -std::cos(a) * R * ce};
                b.vertex(p, Vector3Negate(Vector3Normalize(p)), {0, 0}, c);
            }
        }
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < segs; ++i) {
                const int a = j * (segs + 1) + i;
                b.quad(a, a + segs + 1, a + segs + 2, a + 1);  // seen from inside
            }
        G.sky = b.build(true);
        G.skyPhase = ph, G.skyOvercast = overcast;
        if (G.pano.vertexCount == 0) {  // the view's band
            MeshBuilder p;
            const int n = 96;
            for (int i = 0; i <= n; ++i) {
                const float a = 2.f * PI * i / n, u = (float)i / n;
                for (int j = 0; j < 2; ++j) {
                    const float e = j == 0 ? kPanoLo : kPanoHi;
                    Vector3 v{std::sin(a) * kPanoR, std::tan(e * DEG2RAD) * kPanoR, -std::cos(a) * kPanoR};
                    // canvases are stored upside down: v = 1 at the canvas' top row
                    p.vertex(v, {-std::sin(a), 0, std::cos(a)}, {u, j == 0 ? 0.f : 1.f}, WHITE);
                }
            }
            for (int i = 0; i < n; ++i) {
                const int a = i * 2;
                p.quad(a, a + 2, a + 3, a + 1);
            }
            G.pano = p.build(true);
        }
    }
    if (G.foliageSeason != se) buildFoliage();
    // leaves glow a little when the sun shines through them (by day), none at night
    G.mLeaf.emissive = 0.10f * dayK * (1.f - 0.6f * G.awningK);
    updateGardenLife(dt);
    // the mangal's coals
    if (G.mangalK > 0.3f && dt > 0.f) {
        G.coalAcc += dt * 1.2f * G.mangalK;
        while (G.coalAcc >= 1.f) {
            G.coalAcc -= 1.f;
            SmokeParams p;
            p.velocity = {rng.uniform(-0.02f, 0.02f), rng.uniform(0.16f, 0.24f), rng.uniform(-0.02f, 0.02f)};
            p.size0 = 0.08f;
            p.size1 = rng.uniform(0.35f, 0.5f);
            p.life = rng.uniform(2.f, 3.f);
            p.alpha = 0.05f;
            p.color = Color{255, 214, 170, 255};
            p.turbulence = 1.1f;
            p.buoyancy = 0.05f;
            emit(Vector3Add(G.mangalTop, {rng.uniform(-0.1f, 0.1f), 0.06f, rng.uniform(-0.1f, 0.1f)}), p);
        }
    }
}

void Room::Impl::submitGardenLights(Renderer& r) {
    Garden& G = *gd;
    const float D = dayK, dusk = duskK, awn = G.awningK;
    const bool overcast = rainTarget > 0.f || snowTarget > 0.5f;
    Ambient a;
    const Color skyDay = overcast ? Color{176, 180, 186, 255} : Color{176, 196, 222, 255};
    a.sky = mix(Color{52, 62, 96, 255}, mix(skyDay, Color{226, 168, 132, 255}, dusk), D);
    a.ground = mix(Color{30, 26, 24, 255}, Color{132, 116, 94, 255}, D);
    // (under the awning the sky's light comes in from the open sides only, a little green from the stripes)
    if (awn > 0.01f) a.sky = mix(a.sky, mix(a.sky, Color{150, 176, 150, 255}, 0.3f), awn);
    a.intensity = (title ? 0.24f : 0.16f) + 0.34f * D * (1.f - 0.3f * awn);
    r.setAmbient(a);
    Fog f;
    f.color = mix(Color{22, 28, 44, 255}, mix(Color{176, 190, 204, 255}, Color{214, 168, 136, 255}, dusk), D);
    if (overcast) f.color = mix(f.color, Color{120, 124, 130, 255}, 0.5f * D);
    f.density = 0.012f + (overcast ? 0.02f : 0.f) + 0.012f * (1.f - D);
    f.hazeStart = 40.f;
    f.hazeTop = 41.f;
    f.hazeDensity = 0.f;
    r.setFog(f);

    // key light: the sun by day (dappled by the leaves), our table's pendant once it is dark
    const float sunAmt = sunK * std::min(1.f, D * 2.6f);  // (the evening's low sun still reaches in, golden)
    const bool sunKey = sunAmt > 0.05f;
    const Lamp& K0 = lamps[0];
    if (sunKey) {
        KeyLight k;
        const Vector3 tgt{0.f, w3d::TABLE_Y, 0.f};
        k.position = Vector3Subtract(tgt, Vector3Scale(G.sunDir, SUN_DIST));
        k.target = tgt;
        k.color = mix(Color{255, 244, 226, 255}, sunColor, 0.8f);
        const float att = 0.66f / (SUN_DIST * SUN_DIST + 0.1225f) * 0.94f;
        k.intensity = 1.4f * sunAmt / att;
        k.innerDeg = 19.f;
        k.outerDeg = 24.f;
        k.range = 60.f;
        k.shadows = true;
        r.setKeyLight(k);
    } else {
        const int keyIdx = keyLamp();
        const Lamp& K = lamps[(size_t)keyIdx];
        KeyLight k;
        k.position = K.cur;
        k.target = {K.cur.x, w3d::TABLE_Y, K.cur.z};
        k.color = Color{255, 204, 148, 255};
        k.intensity = 3.3f * K.level;
        k.innerDeg = 34.f;
        k.outerDeg = 64.f;
        k.range = 6.f;
        k.shadows = true;
        r.setKeyLight(k);
    }
    int n = 0;
    const int budget = tavlaFocus ? 10 : 9;
    auto add = [&](const PointLight& p) {
        if (n >= budget || p.intensity <= 0.01f) return;
        r.addPointLight(p);
        ++n;
    };
    // the pendants (our table's too while the sun is the key light)
    for (size_t i = 0; i < lamps.size(); ++i) {
        if (!sunKey && (int)i == keyLamp()) continue;
        const Lamp& L = lamps[i];
        if (L.level < 0.02f) continue;
        const float s = L.small ? 0.7f : 1.f;
        add({Vector3Add(L.cur, Vector3{0, -0.26f * s, 0}), L.color, L.intensity * L.level * (i == 0 ? 1.6f : 1.f), L.range});
    }
    (void)K0;
    // the string lights: two broad warm sources under the trellis
    if (G.bulbsK > 0.02f) {
        const float k = G.bulbsK * (0.9f + 0.1f * noise1(time * 0.7f, seed + 911u));
        add({{-1.6f, 2.5f, -0.6f}, Color{255, 196, 130, 255}, 0.9f * k, 5.5f});
        add({{1.8f, 2.5f, 1.4f}, Color{255, 196, 130, 255}, 0.8f * k, 5.5f});
    }
    // the mangal in winter
    if (G.mangalK > 0.05f) add({Vector3Add(G.mangalTop, {0, 0.25f, 0}), Color{255, 128, 56, 255}, 0.9f * G.mangalK * stoveLevel, 2.6f});
    // the kahvehane's lit door and windows at night
    if (D < 0.6f) add({{GX1 - 0.6f, 1.6f, -0.05f}, Color{255, 190, 120, 255}, 0.8f * (1.f - D / 0.6f), 3.2f});
    // the picture light over the scoreboard on the trunk
    add({boardLightPos, Color{255, 200, 140, 255}, 0.42f * lamps[1].level * (1.f - 0.7f * D), 1.7f});
}

void Room::Impl::submitGarden(Renderer& r) {
    Garden& G = *gd;
    submitGardenLights(r);
    for (const Static& s : statics)
        if (s.venues & 2u) r.submit(&s.mesh, s.mat, s.xf, s.flags);
    // sky and the view (unfogged, self-lit)
    r.submit(&G.sky, &G.mSky, MatrixIdentity(), NoFog | DoubleSided);
    r.submit(&G.pano, &G.mPano, MatrixIdentity(), NoFog | Transparent | DoubleSided);
    if (dayK < 0.3f) {  // stars and the moon over the sea
        okey::Rng sr(seed + 77u);
        const float k = 1.f - dayK / 0.3f;
        for (int i = 0; i < 70; ++i) {
            const float az = sr.uniform(0.f, 2.f * PI), el = sr.uniform(14.f, 80.f) * DEG2RAD, R = kPanoR;
            const float tw = 0.6f + 0.4f * std::sin(time * sr.uniform(1.f, 3.f) + i);
            r.submitGlow({std::sin(az) * R * std::cos(el), std::sin(el) * R, -std::cos(az) * R * std::cos(el)}, sr.uniform(0.06f, 0.13f),
                         Color{220, 228, 255, 255}, 0.7f * tw * k);
        }
        const float ma = 12.f * DEG2RAD, me = 18.f * DEG2RAD;
        const Vector3 moon{std::sin(ma) * kPanoR * std::cos(me), std::sin(me) * kPanoR, -std::cos(ma) * kPanoR * std::cos(me)};
        r.submitGlow(moon, 0.9f, Color{250, 246, 226, 255}, 1.2f * k);
        r.submitGlow(moon, 3.f, Color{180, 190, 230, 255}, 0.25f * k);
    }
    // chairs (hasır) at the background tables and the tavla table
    for (const Chair& c : chairs) {
        const Matrix M = MatrixMultiply(MatrixRotateY((c.yaw + c.yawOff) * DEG2RAD), translate(Vector3Add(c.pos, c.off)));
        r.submit(&G.chair, &G.mChairWood, M, CastShadow);
        r.submit(&G.chairSeat, &G.mStraw, M, CastShadow);
    }
    submitLamps(r);
    submitProps(r);
    submitWeather(r);
    submitDaylight(r);
    submitCat(r);
    submitTree(r);
    // the kahvehane's windows: the warm room inside at night, dark glass by day
    {
        const float lit = std::clamp(1.f - dayK * 1.3f, 0.f, 1.f);
        G.mWindow.emissive = 0.15f + 0.85f * lit;
        const unsigned char v = (unsigned char)(90.f + 165.f * lit);
        G.mWindow.material.maps[MATERIAL_MAP_ALBEDO].color = Color{v, v, v, 255};
        r.submit(&G.windows, &G.mWindow, MatrixIdentity(), 0);
    }
    // the awning (rain / winter): it slides out over the trellis
    if (G.awningK > 0.01f) {
        const float s = 0.02f + 0.98f * G.awningK;
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixTranslate(-GX1, 0, 0), MatrixScale(s, 1.f, 1.f)), MatrixTranslate(GX1, 0, 0));
        r.submit(&G.awning, &G.mAwning, M, CastShadow | DoubleSided);
    }
    // string lights
    {
        const float k = G.bulbsK;
        G.mStrBulb.emissive = 0.15f + 0.85f * k;
        r.submit(&G.strBulbs, &G.mStrBulb, MatrixIdentity(), 0);
        if (k > 0.02f)
            for (size_t i = 0; i < G.bulbPos.size(); ++i) {
                const float f = 0.85f + 0.15f * noise1(time * 2.f + (float)i * 3.7f, seed + (uint32_t)i);
                r.submitGlow(G.bulbPos[i], 0.07f, Color{255, 220, 160, 255}, 0.75f * k * f);
                if (i % 2 == 0) r.submitGlow(G.bulbPos[i], 0.3f, Color{255, 190, 120, 255}, 0.16f * k * f);  // (a halo for every other)
            }
    }
    // the mangal
    if (G.mangalK > 0.02f) {
        const float g = G.mangalK * stoveLevel;
        G.mCoal.emissive = 1.f;
        G.mCoal.material.maps[MATERIAL_MAP_ALBEDO].color =
            mix(Color{40, 36, 34, 255}, Color{255, (unsigned char)std::clamp(90.f + 70.f * g, 0.f, 255.f), 40, 255}, std::min(1.f, G.mangalK * 1.4f));
        r.submit(&G.coals, &G.mCoal, MatrixIdentity(), 0);
        r.submitGlow(Vector3Add(G.mangalTop, {0, 0.05f, 0}), 0.3f, Color{255, 120, 40, 255}, 0.35f * g);
    } else {
        G.mCoal.emissive = 0.f;
        G.mCoal.material.maps[MATERIAL_MAP_ALBEDO].color = Color{48, 44, 40, 255};
        r.submit(&G.coals, &G.mCoal, MatrixIdentity(), 0);
    }
    submitGardenLife(r);
}

}  // namespace r3d
