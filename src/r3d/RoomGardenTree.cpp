// Room module: the garden's trees (room owner).
//   * The çınar: a massive mottled trunk with buttress roots (flattened where the scoreboard hangs), four great limbs
//     and a leader, branching twice more; its leaves (five-lobed, one batched mesh per sway group, rebuilt for the
//     season: fresh green in spring, deep green in summer, yellow, orange and brown in autumn, bare in winter) hang
//     over the whole garden and dapple the sun (they cast into the key light's shadow map). Each group sways a little
//     about the fork, so the dapples on the tables drift.
//   * The vine on the trellis (asma): two gnarled stems climbing the posts, canes along the rafters and battens, leaves
//     and, from late summer, bunches of grapes.
//   * The erguvan (Judas tree) by the parapet: magenta blossom in spring, heart-shaped leaves in summer and autumn.
//   * Fallen leaves on the cobbles in autumn.
#include "r3d/RoomGardenInternal.h"
#include "r3d/Daytime.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace r3d {

using namespace rm;

namespace {

// A tube whose radius tapers from r0 to r1 along the path (bark UVs: u around, v along in metres / 2.4).
void taperTube(MeshBuilder& b, const std::vector<Vector3>& path, float r0, float r1, int seg, Color col, float v0 = 0.f) {
    const int n = (int)path.size();
    if (n < 2) return;
    float along = v0;
    Vector3 prevN{1, 0, 0};
    int base = b.vertexCount();
    for (int i = 0; i < n; ++i) {
        Vector3 t = i == 0 ? Vector3Subtract(path[1], path[0])
                           : (i == n - 1 ? Vector3Subtract(path[i], path[i - 1]) : Vector3Subtract(path[i + 1], path[i - 1]));
        t = Vector3Normalize(t);
        // a normal frame that turns as little as possible from the previous ring
        Vector3 nn = Vector3Subtract(prevN, Vector3Scale(t, Vector3DotProduct(prevN, t)));
        if (Vector3Length(nn) < 1e-3f) nn = std::fabs(t.y) < 0.9f ? Vector3CrossProduct(t, {0, 1, 0}) : Vector3CrossProduct(t, {1, 0, 0});
        nn = Vector3Normalize(nn);
        prevN = nn;
        const Vector3 bb = Vector3CrossProduct(t, nn);
        if (i > 0) along += Vector3Distance(path[i], path[i - 1]);
        const float r = r0 + (r1 - r0) * (float)i / (n - 1);
        for (int k = 0; k <= seg; ++k) {
            const float a = 2.f * PI * k / seg;
            const Vector3 d = Vector3Add(Vector3Scale(nn, std::cos(a)), Vector3Scale(bb, std::sin(a)));
            b.vertex(Vector3Add(path[i], Vector3Scale(d, r)), d, {(float)k / seg * std::max(1.f, r * 6.f), along / 2.4f}, col);
        }
    }
    for (int i = 0; i + 1 < n; ++i)
        for (int k = 0; k < seg; ++k) {
            const int a = base + i * (seg + 1) + k, c = a + seg + 1;
            b.quad(a, c, c + 1, a + 1);
        }
}

// One palmate (five-lobed) leaf: a fan of 10 triangles round its centre, `n` its face normal, `fwd` the midrib.
void leaf5(MeshBuilder& b, Vector3 c, Vector3 n, Vector3 fwd, float size, Color col, float lobes = 1.f) {
    n = Vector3Normalize(n);
    fwd = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(n, Vector3DotProduct(fwd, n))));
    const Vector3 side = Vector3CrossProduct(n, fwd);
    const int ci = b.vertex(c, n, {0.5f, 0.5f}, scaleRgb(col, 1.12f));
    int first = -1, prev = -1;
    for (int i = 0; i <= 10; ++i) {
        const int k = i % 10;
        const float a = PI * 0.5f + 2.f * PI * k / 10.f;  // k = 0: the tip
        float r = (k % 2 == 0) ? 0.5f : 0.5f - 0.24f * lobes;
        if (k == 5) r = 0.16f;                // the stalk's notch
        if (k == 4 || k == 6) r *= 0.8f;      // the base lobes are smaller
        const Vector3 p = Vector3Add(c, Vector3Add(Vector3Scale(fwd, std::sin(a) * r * size), Vector3Scale(side, std::cos(a) * r * size)));
        int vi;
        if (i < 10) {
            // the lobes curl a little toward the face
            vi = b.vertex(Vector3Add(p, Vector3Scale(n, -0.06f * size * (k % 2 == 0 ? 1.f : 0.f))), n, {0.5f + std::cos(a) * r, 0.5f + std::sin(a) * r},
                          col);
            if (i == 0) first = vi;
        } else {
            vi = first;
        }
        if (prev >= 0) b.triangle(ci, prev, vi);
        prev = vi;
    }
}

// Season colours of the çınar's leaves.
Color canopyColor(int season, okey::Rng& rng) {
    switch (season) {
    case w3d::Ilkbahar: return mix(Color{128, 172, 70, 255}, Color{96, 150, 58, 255}, rng.uniform());
    case w3d::Yaz: return mix(Color{70, 116, 48, 255}, Color{92, 134, 56, 255}, rng.uniform());
    case w3d::Sonbahar: {
        const Color c[5] = {{214, 168, 56, 255}, {196, 116, 40, 255}, {164, 82, 38, 255}, {150, 140, 60, 255}, {120, 84, 50, 255}};
        return scaleRgb(c[rng.range(0, 4)], rng.uniform(0.85f, 1.08f));
    }
    default: return Color{110, 90, 60, 255};
    }
}

struct Branch {
    std::vector<Vector3> path;
    float r0, r1;
    int depth;
};

}  // namespace

// ============================================================================ trunk and branches (static)
void Room::Impl::buildTree() {
    Garden& G = *gd;
    okey::Rng rng(seed + 5151);
    MeshBuilder bark, barkCast;
    const Vector3 T = G.trunkBase;
    const Color bc = WHITE;
    // ---- the trunk: rings with roots at the foot, lumps, a flat front where the scoreboard hangs
    {
        const int rings = 26, seg = 32;
        const float yTop = 4.4f;
        auto prof = [](float y) {
            const float ys[] = {-0.05f, 0.25f, 0.7f, 1.3f, 2.5f, 3.5f, 4.4f};
            const float rs[] = {1.08f, 0.9f, 0.78f, 0.74f, 0.71f, 0.67f, 0.62f};
            for (int i = 0; i + 1 < 7; ++i)
                if (y <= ys[i + 1]) {
                    float t = (y - ys[i]) / (ys[i + 1] - ys[i]);
                    return rs[i] + (rs[i + 1] - rs[i]) * t;
                }
            return rs[6];
        };
        const float rootA[6] = {0.4f, 1.3f, 2.2f, 3.3f, 4.3f, 5.4f};
        std::vector<Vector3> pts((size_t)(rings + 1) * (seg + 1));
        for (int j = 0; j <= rings; ++j) {
            const float y = -0.05f + (yTop + 0.05f) * j / rings;
            for (int i = 0; i <= seg; ++i) {
                const float a = 2.f * PI * (i % seg) / seg;
                float r = prof(y);
                for (float ra : rootA) {
                    float d = std::cos(a - ra);
                    if (d > 0.f) r += 0.42f * std::exp(-y / 0.32f) * std::pow(d, 8.f);
                }
                r *= 1.f + 0.07f * (fbm(a * 1.3f, y * 0.9f, 3, seed + 61) - 0.5f) * 2.f;
                // (the front, toward +Z: worn flat where the board hangs, its face at the board's plane)
                const float sz = std::sin(a);
                if (y > 0.8f && y < 2.5f && sz > 0.3f) r = std::min(r, (w3d::SCOREBOARD_POS.z - 0.012f - T.z) / sz);
                pts[(size_t)j * (seg + 1) + i] = {T.x + std::cos(a) * r, y, T.z + std::sin(a) * r};
            }
        }
        const int base = barkCast.vertexCount();
        for (int j = 0; j <= rings; ++j)
            for (int i = 0; i <= seg; ++i) {
                const Vector3 p = pts[(size_t)j * (seg + 1) + i];
                const Vector3 pl = pts[(size_t)j * (seg + 1) + (i + seg - 1) % seg], pr = pts[(size_t)j * (seg + 1) + (i + 1) % seg];
                const Vector3 pd = pts[(size_t)std::max(j - 1, 0) * (seg + 1) + i], pu = pts[(size_t)std::min(j + 1, rings) * (seg + 1) + i];
                Vector3 nrm = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(pr, pl), Vector3Subtract(pu, pd)));
                if (Vector3DotProduct(nrm, Vector3Subtract(p, {T.x, p.y, T.z})) < 0.f) nrm = Vector3Negate(nrm);
                barkCast.vertex(p, nrm, {(float)i / seg * 3.f, p.y / 2.4f}, bc);
            }
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < seg; ++i) {
                const int a = base + j * (seg + 1) + i, c = a + seg + 1;
                barkCast.quad(a, c, c + 1, a + 1);
            }
        // the nails and battens that hold the scoreboard (two cleats behind it)
        for (float dy : {-0.22f, 0.22f}) {
            const Vector3 p{w3d::SCOREBOARD_POS.x, w3d::SCOREBOARD_POS.y + dy, w3d::SCOREBOARD_POS.z - 0.05f};
            rbox(barkCast, p, {w3d::SCOREBOARD_W * 0.9f, 0.05f, 0.1f}, 0.01f, 1, Color{150, 110, 80, 255});
        }
    }
    // ---- limbs and branches
    const Vector3 fork{T.x, 4.1f, T.z};
    struct Limb {
        Vector3 h;
        float el, len, r;
    };
    const Limb limbs[] = {{{0.5f, 0, 0.86f}, 34.f, 4.6f, 0.4f}, {{-0.62f, 0, 0.78f}, 38.f, 4.4f, 0.38f}, {{0.94f, 0, 0.1f}, 36.f, 4.2f, 0.36f},
                          {{-0.75f, 0, -0.55f}, 44.f, 3.0f, 0.32f}, {{0.1f, 0, 0.3f}, 74.f, 3.6f, 0.36f}, {{0.25f, 0, 0.95f}, 22.f, 3.8f, 0.3f}};
    std::vector<Branch> all;
    std::vector<Vector3> tips;
    std::function<void(Vector3, Vector3, float, float, int)> grow = [&](Vector3 p, Vector3 d, float len, float r, int depth) {
        Branch br;
        br.depth = depth;
        br.r0 = r;
        br.r1 = r * 0.62f;
        const int n = 6;
        Vector3 cur = p, dir = Vector3Normalize(d);
        br.path.push_back(cur);
        for (int i = 1; i <= n; ++i) {
            // limbs arch: out first, then up and a little down at the end for the long ones
            Vector3 bend{rng.uniform(-0.12f, 0.12f), (depth == 0 ? 0.05f : 0.08f) - 0.02f * i, rng.uniform(-0.12f, 0.12f)};
            dir = Vector3Normalize(Vector3Add(dir, bend));
            cur = Vector3Add(cur, Vector3Scale(dir, len / n));
            br.path.push_back(cur);
        }
        all.push_back(br);
        if (depth >= 2) {
            tips.push_back(cur);
            tips.push_back(br.path[(size_t)n / 2 + 1]);
            return;
        }
        const int kids = depth == 0 ? 3 : 2;
        for (int k = 0; k < kids; ++k) {
            const float at = depth == 0 ? (k == 0 ? 1.f : (k == 1 ? 0.55f : 0.8f)) : (k == 0 ? 1.f : 0.6f);
            const Vector3 sp = br.path[(size_t)std::lround(at * n)];
            const float yaw = rng.uniform(25.f, 50.f) * (k % 2 ? -1.f : 1.f) * DEG2RAD;
            Vector3 nd{dir.x * std::cos(yaw) - dir.z * std::sin(yaw), dir.y + rng.uniform(0.05f, 0.3f), dir.x * std::sin(yaw) + dir.z * std::cos(yaw)};
            grow(sp, nd, len * rng.uniform(0.55f, 0.7f), br.r1 * (k == 0 ? 0.85f : 0.7f), depth + 1);
        }
    };
    for (const Limb& L : limbs) {
        const Vector3 d = Vector3Normalize(Vector3Add(Vector3Scale(Vector3Normalize(L.h), std::cos(L.el * DEG2RAD)), {0, std::sin(L.el * DEG2RAD), 0}));
        grow(Vector3Add(fork, Vector3Scale(d, 0.1f)), d, L.len, L.r, 0);
    }
    for (const Branch& br : all) taperTube(br.depth == 0 ? barkCast : bark, br.path, br.r0, br.r1, br.depth == 0 ? 14 : (br.depth == 1 ? 9 : 6), bc);
    // twigs to the leaf clusters
    G.canopy.clear();
    // store the cluster centres (the leaves are rebuilt per season) in the sway groups' pivots: kept in `tips`
    std::vector<Vector3> centres;
    for (const Vector3& t : tips) {
        Vector3 c = t;
        c.y = std::max(c.y, 5.2f);
        if (c.x > 3.3f) c.x = 3.3f;
        centres.push_back(c);
        // a couple of extra clusters around each tip fill the crown
        for (int k = 0; k < 1; ++k)
            centres.push_back({c.x + rng.uniform(-1.f, 1.f), std::max(5.f, c.y + rng.uniform(-0.6f, 0.5f)), c.z + rng.uniform(-1.f, 1.f)});
    }
    // and the rest of the crown: clusters through a broad dome over the garden (the leaves hide the twigs)
    for (int i = 0; i < 190; ++i) {
        const float a = rng.uniform(0.f, 2.f * PI), rr = std::sqrt(rng.uniform()) ;
        Vector3 c{-0.9f + std::cos(a) * rr * 5.6f, 0.f, -1.6f + std::sin(a) * rr * 5.4f};
        const float dome = std::sqrt(std::max(0.f, 1.f - rr * rr));
        c.y = 5.0f + 2.6f * dome + rng.uniform(-0.5f, 0.5f);
        if (c.x > 3.3f || c.z < -7.5f) continue;
        centres.push_back(c);
    }
    // keep them for buildFoliage (in the bird spots too: they perch in the low branches)
    G.canopy.resize(6);
    for (size_t i = 0; i < G.canopy.size(); ++i) {
        G.canopy[i].pivot = fork;
        G.canopy[i].amp = rng.uniform(0.35f, 0.6f);
        G.canopy[i].phase = rng.uniform(0.f, 6.28f);
        G.canopy[i].freq = rng.uniform(0.5f, 0.8f);
    }
    G.canopyCentres = centres;
    // ---- the vine's stems: up the back-left post and the post by the gate, twisting
    {
        const Vector3 roots[2] = {{w3d::ROOM_X0 + 0.1f, 0.f, -3.22f}, {w3d::ROOM_X0 + 0.12f, 1.0f, -0.28f}};
        for (const Vector3& r0 : roots) {
            std::vector<Vector3> p;
            for (int i = 0; i <= 12; ++i) {
                const float t = (float)i / 12;
                p.push_back({r0.x + 0.06f * std::sin(t * 9.f), r0.y + (w3d::CEILING_Y - 0.05f - r0.y) * t, r0.z + 0.06f * std::cos(t * 7.f)});
            }
            taperTube(barkCast, p, 0.05f, 0.035f, 7, Color{150, 120, 96, 255});
        }
    }
    // ---- the erguvan's trunk by the parapet (back left)
    {
        const Vector3 e{-3.55f, 0.f, -4.85f};
        std::vector<Vector3> p{e, {e.x + 0.05f, 0.8f, e.z}, {e.x + 0.12f, 1.5f, e.z + 0.05f}};
        taperTube(barkCast, p, 0.1f, 0.075f, 9, Color{120, 110, 104, 255});
        const Vector3 dirs[4] = {{0.6f, 1.f, 0.2f}, {-0.5f, 1.f, 0.3f}, {0.1f, 1.f, -0.6f}, {0.2f, 1.2f, 0.7f}};
        G.erguvanTips.clear();
        for (const Vector3& d0 : dirs) {
            const Vector3 d = Vector3Normalize(d0);
            std::vector<Vector3> q{p.back(), Vector3Add(p.back(), Vector3Scale(d, 0.7f)), Vector3Add(p.back(), Vector3Scale(d, 1.5f))};
            taperTube(bark, q, 0.06f, 0.025f, 6, Color{120, 110, 104, 255});
            G.erguvanTips.push_back(q[1]);
            G.erguvanTips.push_back(q[2]);
        }
    }
    staticVenues = 2;
    addStatic(barkCast, &G.mBark, CastShadow);
    addStatic(bark, &G.mBark, CastShadow);
    staticVenues = 1;
}

// ============================================================================ leaves (per season)
void Room::Impl::buildFoliage() {
    Garden& G = *gd;
    const int se = seasonNow;
    G.foliageSeason = se;
    okey::Rng rng(seed + 6161);
    auto um = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    for (Garden::Sway& s : G.canopy) um(s.mesh);
    for (Garden::Sway& s : G.vines) um(s.mesh);
    um(G.erguvan);
    um(G.groundLeaves);
    const Vector3 T = G.trunkBase;
    // ---- the çınar: clusters of leaves round each branch tip, bare in winter, thinner in autumn
    if (se != w3d::Kis) {
        std::vector<MeshBuilder> mb(G.canopy.size());
        const int perCluster = se == w3d::Sonbahar ? 34 : (se == w3d::Ilkbahar ? 42 : 50);
        for (const Vector3& c : G.canopyCentres) {
            const float az = std::atan2(c.x - T.x, c.z - T.z);
            const int gi = std::clamp((int)((az + PI) / (2.f * PI) * (float)mb.size()), 0, (int)mb.size() - 1);
            MeshBuilder& b = mb[(size_t)gi];
            const float R = rng.uniform(1.0f, 1.5f);
            for (int i = 0; i < perCluster; ++i) {
                // on a squashed shell, more on the outside
                Vector3 d{rng.uniform(-1.f, 1.f), rng.uniform(-0.6f, 1.f), rng.uniform(-1.f, 1.f)};
                if (Vector3Length(d) < 0.05f) d = {0, 1, 0};
                d = Vector3Normalize(d);
                const float rr = R * std::sqrt(rng.uniform(0.25f, 1.f));
                Vector3 p{c.x + d.x * rr, c.y + d.y * rr * 0.6f, c.z + d.z * rr};
                if (p.y < 4.6f) p.y = 4.6f + rng.uniform(0.f, 0.3f);
                Vector3 n = Vector3Normalize(Vector3Add(Vector3Scale(d, 0.6f), {rng.uniform(-0.4f, 0.4f), 1.f, rng.uniform(-0.4f, 0.4f)}));
                Vector3 fwd{rng.uniform(-1.f, 1.f), -0.3f, rng.uniform(-1.f, 1.f)};
                leaf5(b, p, n, fwd, rng.uniform(0.22f, 0.32f), canopyColor(se, rng));
            }
        }
        for (size_t i = 0; i < mb.size(); ++i) G.canopy[i].mesh = mb[i].build(true);
    }
    // ---- the vine: leaves along the rafters and battens, thicker near its stems; grapes from late summer
    {
        G.vines.assign(2, Garden::Sway{});
        MeshBuilder mb[2];
        const float y = w3d::CEILING_Y + 0.1f;
        const Color vg = se == w3d::Ilkbahar ? Color{120, 170, 70, 255} : (se == w3d::Sonbahar ? Color{196, 150, 60, 255} : Color{76, 130, 52, 255});
        auto vineLeaf = [&](int g, Vector3 p) {
            Vector3 n{rng.uniform(-0.3f, 0.3f), 1.f, rng.uniform(-0.3f, 0.3f)};
            Color c = scaleRgb(vg, rng.uniform(0.82f, 1.1f));
            if (se == w3d::Sonbahar && rng.chance(0.3f)) c = Color{170, 70, 40, 255};
            leaf5(mb[g], p, n, {rng.uniform(-1.f, 1.f), 0.f, rng.uniform(-1.f, 1.f)}, rng.uniform(0.15f, 0.22f), c, 0.6f);
        };
        if (se != w3d::Kis) {
            const float density = se == w3d::Ilkbahar ? 0.55f : 1.f;
            // along the rafters (x = -2.55, 0, 2.45) and the side beams, and along the battens nearer the stems
            for (float x : {w3d::ROOM_X0 - 0.05f, -2.55f, 0.f, 2.45f}) {
                for (float z = -3.3f; z < w3d::ROOM_Z1 - 0.1f; z += 0.11f) {
                    const float near = std::exp(-std::fabs(x - w3d::ROOM_X0) / 4.f);
                    if (!rng.chance(density * (0.45f + 0.5f * near))) continue;
                    const int g = z < 0.f ? 0 : 1;
                    vineLeaf(g, {x + rng.uniform(-0.18f, 0.18f), y + rng.uniform(-0.06f, 0.08f), z});
                    // tendrils of leaves hanging off the beams now and then
                    if (rng.chance(0.06f))
                        for (int k = 1; k <= 3; ++k) vineLeaf(g, {x + rng.uniform(-0.1f, 0.1f), y - 0.12f * k, z + rng.uniform(-0.08f, 0.08f)});
                }
            }
            for (float z = -3.0f; z < w3d::ROOM_Z1 - 0.2f; z += 0.72f)
                for (float x = w3d::ROOM_X0; x < w3d::ROOM_X1 - 0.2f; x += 0.1f) {
                    const float near = std::exp(-(x - w3d::ROOM_X0) / 2.6f);
                    if (!rng.chance(density * (0.12f + 0.55f * near))) continue;
                    vineLeaf(z < 0.f ? 0 : 1, {x, y + 0.03f + rng.uniform(-0.04f, 0.06f), z + rng.uniform(-0.15f, 0.15f)});
                }
            // grapes (late summer and autumn): bunches hanging under the trellis away from our table
            if (se == w3d::Yaz || se == w3d::Sonbahar) {
                const Color grape = se == w3d::Yaz ? Color{150, 176, 80, 255} : Color{86, 40, 74, 255};
                for (int k = 0; k < 16; ++k) {
                    Vector3 p{rng.uniform(w3d::ROOM_X0 + 0.2f, -1.2f), y - 0.12f, rng.uniform(-3.2f, 3.3f)};
                    if (k % 3 == 0) p.x = rng.uniform(1.4f, 3.6f);
                    const int g = p.z < 0.f ? 0 : 1;
                    mb[g].capsule({p.x, y, p.z}, p, 0.004f, 4, Color{110, 100, 60, 255});
                    for (int i = 0; i < 18; ++i) {
                        const float t = (float)i / 18, rr = 0.06f * (1.f - t);
                        const float a = i * 2.4f;
                        mb[g].sphere({p.x + std::cos(a) * rr, p.y - 0.02f - t * 0.2f, p.z + std::sin(a) * rr}, 0.014f, 3, 5,
                                     scaleRgb(grape, rng.uniform(0.85f, 1.1f)));
                    }
                }
            }
        }
        for (int g = 0; g < 2; ++g) {
            G.vines[(size_t)g].mesh = mb[g].build(true);
            G.vines[(size_t)g].pivot = {w3d::ROOM_X0, y, g == 0 ? -1.5f : 1.8f};
            G.vines[(size_t)g].amp = 0.25f;
            G.vines[(size_t)g].phase = g * 1.7f;
            G.vines[(size_t)g].freq = 0.9f;
        }
    }
    // ---- the erguvan: blossom in spring, leaves in summer / autumn
    if (se != w3d::Kis) {
        MeshBuilder b;
        for (const Vector3& tp : G.erguvanTips)
            for (int i = 0; i < (se == w3d::Ilkbahar ? 120 : 70); ++i) {
                Vector3 d = Vector3Normalize({rng.uniform(-1.f, 1.f), rng.uniform(-0.5f, 1.f), rng.uniform(-1.f, 1.f)});
                const float rr = rng.uniform(0.1f, 0.75f);
                Vector3 p = Vector3Add(tp, Vector3Scale(d, rr));
                if (se == w3d::Ilkbahar) {
                    const Color pk = mix(Color{196, 72, 150, 255}, Color{226, 120, 180, 255}, rng.uniform());
                    b.sphere(p, rng.uniform(0.03f, 0.05f), 3, 5, pk);
                } else {
                    const Color lc = se == w3d::Yaz ? Color{84, 130, 60, 255} : Color{214, 170, 60, 255};
                    leaf5(b, p, Vector3Add(d, {0, 1, 0}), {rng.uniform(-1.f, 1.f), 0, rng.uniform(-1.f, 1.f)}, rng.uniform(0.09f, 0.13f),
                          scaleRgb(lc, rng.uniform(0.85f, 1.1f)), 0.f);
                }
            }
        G.erguvan = b.build(true);
    }
    // ---- fallen leaves on the cobbles (autumn), a few petals in spring
    if (se == w3d::Sonbahar || se == w3d::Ilkbahar) {
        MeshBuilder b;
        const int n = se == w3d::Sonbahar ? 520 : 140;
        for (int i = 0; i < n; ++i) {
            Vector3 p{rng.uniform(w3d::ROOM_X0, w3d::ROOM_X1 - 0.3f), 0.03f, rng.uniform(-5.4f, w3d::ROOM_Z1 - 0.1f)};
            if (std::fabs(p.x) < 1.05f && std::fabs(p.z) < 1.05f && rng.chance(0.7f)) continue;  // swept round our table
            if (se == w3d::Sonbahar) {
                // drifts along the walls
                if (rng.chance(0.4f)) p.x = w3d::ROOM_X0 + rng.uniform(0.f, 0.5f);
                leaf5(b, p, {rng.uniform(-0.2f, 0.2f), 1.f, rng.uniform(-0.2f, 0.2f)}, {rng.uniform(-1.f, 1.f), 0, rng.uniform(-1.f, 1.f)},
                      rng.uniform(0.14f, 0.22f), canopyColor(se, rng));
            } else {
                if (p.z > -3.5f && rng.chance(0.6f)) continue;  // petals mostly under the erguvan
                b.sphere(p, 0.012f, 2, 4, Color{214, 110, 170, 255});
            }
        }
        G.groundLeaves = b.build(true);
    }
}

void Room::Impl::submitTree(Renderer& r) {
    Garden& G = *gd;
    // gentle sway about the fork (the dapples on the tables drift with it); stronger in gusts
    const float w = 0.6f + 0.8f * G.wind;
    for (const Garden::Sway& s : G.canopy) {
        if (s.mesh.vertexCount == 0) continue;
        const float ax = s.amp * w * std::sin(time * s.freq + s.phase) * DEG2RAD;
        const float az = s.amp * w * 0.7f * std::sin(time * s.freq * 1.31f + s.phase * 1.7f) * DEG2RAD;
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixTranslate(-s.pivot.x, -s.pivot.y, -s.pivot.z), MatrixMultiply(MatrixRotateX(ax), MatrixRotateZ(az))),
                                        MatrixTranslate(s.pivot.x, s.pivot.y, s.pivot.z));
        r.submit(&s.mesh, &G.mLeaf, M, CastShadow | DoubleSided);
    }
    for (const Garden::Sway& s : G.vines) {
        if (s.mesh.vertexCount == 0) continue;
        const float d = 0.012f * w * std::sin(time * s.freq + s.phase);
        r.submit(&s.mesh, &G.mLeaf, MatrixTranslate(d, 0.f, d * 0.6f), CastShadow | DoubleSided);
    }
    if (G.erguvan.vertexCount > 0) r.submit(&G.erguvan, seasonNow == w3d::Ilkbahar ? &G.mBlossom : &G.mLeaf, MatrixIdentity(), CastShadow | DoubleSided);
    if (G.groundLeaves.vertexCount > 0) r.submit(&G.groundLeaves, &G.mLeaf, MatrixIdentity(), DoubleSided);
}

}  // namespace r3d
