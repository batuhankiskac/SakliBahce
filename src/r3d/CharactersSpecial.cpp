// Characters module: özel günler (ozelgun agent; Characters::setSpecialDay, r3d/SpecialDay.h).
//   * Bayram: the regulars in their bayramlık: Hacı Rıza and Emekli Nuri put on a tie, Kel Mahmut a red carnation on his
//     chest; the patrons a tie or a carnation with a white pocket square.
//   * Maç gecesi: scarves in the teams' colours (the TV's fictional BGZ red-white and HLÇ yellow-navy) on Kel Mahmut and
//     on half of the patrons, and three more men standing in front of the TV (in the garden: by the portable TV).
//   * Ramazan evenings (after iftar): the kahvehane fills up: two men stand by the card players' table and watch.
// The accessories are draped on each torso mesh: their points follow the mesh's own front surface (sampled once from
// the torso's vertices), so a tie lies on Rıza's shirt between the vest's lapels and a scarf's ends follow Mahmut's belly.
#include "r3d/CharactersState.h"
#include "r3d/Daytime.h"
#include "r3d/SpecialDay.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace chr;

namespace chr {

namespace {

const Color kTeamA[2] = {{206, 36, 40, 255}, {245, 245, 245, 255}};  // BGZ (RoomCanvas.cpp kShirt[0] / kShirt2[0])
const Color kTeamB[2] = {{250, 214, 40, 255}, {24, 40, 110, 255}};   // HLÇ

// The front-most z of the torso surface around (x, y) (torso space; front is -Z), or `fallback`.
float frontZ(const Mesh& m, float x, float y, float fallback) {
    if (!m.vertices || m.vertexCount <= 0) return fallback;
    float best = 1e9f;
    for (float wy : {0.006f, 0.012f})
        for (int i = 0; i < m.vertexCount; ++i) {
            const float vx = m.vertices[3 * i], vy = m.vertices[3 * i + 1], vz = m.vertices[3 * i + 2];
            if (std::fabs(vx - x) < 0.012f && std::fabs(vy - y) < wy && vz < best) best = vz;
        }
    return best < 1e8f ? best : fallback;
}

// How far the surface reaches from the neck's axis (x = 0, z = cz) in the direction `a` (0 = +Z, PI = the front) at y.
float reach(const Mesh& m, float a, float y, float cz, float fallback) {
    if (!m.vertices || m.vertexCount <= 0) return fallback;
    float best = 0.f;
    const Vector2 d{std::sin(a), std::cos(a)};
    for (int i = 0; i < m.vertexCount; ++i) {
        const float vx = m.vertices[3 * i], vy = m.vertices[3 * i + 1], vz = m.vertices[3 * i + 2] - cz;
        if (std::fabs(vy - y) > 0.012f) continue;
        const float along = vx * d.x + vz * d.y, across = std::fabs(-vx * d.y + vz * d.x);
        if (along > 0.f && across < 0.02f && along < 0.16f) best = std::max(best, along);
    }
    return best > 0.03f ? best : fallback;
}

// A necktie on the torso: knot at yTop, blade down to yTip, striped.
void buildTie(MeshBuilder& b, const Mesh& torso, float yTop, float yTip, Color c, Color stripe) {
    const int n = 12;
    int pl = -1, pr = -1;
    for (int k = 0; k <= n; ++k) {
        const float t = (float)k / n, y = yTop - 0.012f - (yTop - 0.012f - yTip) * t;
        const float z = frontZ(torso, 0.f, y, -0.11f) - 0.0045f;
        const float w = (0.011f + 0.016f * t) * (k == n ? 0.08f : 1.f);  // widens down, pointed tip
        const Color col = (k / 2) % 2 ? stripe : c;
        const int l = b.vertex({-w, y, z}, {0, 0, -1}, {0, t}, col), r = b.vertex({w, y, z}, {0, 0, -1}, {1, t}, col);
        if (pl >= 0) b.quad(pl, pr, r, l);
        pl = l, pr = r;
    }
    const float zk = frontZ(torso, 0.f, yTop - 0.008f, -0.1f) - 0.008f;
    b.ellipsoid({0.f, yTop - 0.006f, zk}, {0.011f, 0.012f, 0.008f}, 4, 8, c);  // the knot
}

// A carnation (karanfil) on the left side of the chest (-x), its stem and a leaf; a white pocket square below.
void buildCarnation(MeshBuilder& b, const Mesh& torso, float x, float y, bool pocket) {
    const float z = frontZ(torso, x, y, -0.11f) - 0.012f;
    const Color red{200, 26, 40, 255};
    for (int k = 0; k < 5; ++k) {
        const float a = (float)k * 2.f * PI_F / 5.f;
        b.sphere({x + std::cos(a) * 0.013f, y + std::sin(a) * 0.013f, z}, 0.012f, 4, 6, k % 2 ? red : Color{224, 40, 56, 255});
    }
    b.sphere({x, y, z - 0.005f}, 0.012f, 4, 6, red);
    b.capsule({x, y - 0.012f, z + 0.002f}, {x + 0.006f, y - 0.04f, frontZ(torso, x + 0.006f, y - 0.04f, z) - 0.004f}, 0.0022f, 4,
              Color{60, 110, 50, 255});
    if (pocket) {
        const float py = y - 0.06f, pz = frontZ(torso, x, py, z) - 0.004f;
        b.box({x, py, pz}, {0.034f, 0.016f, 0.004f}, Color{246, 244, 238, 255});
    }
}

// A knitted scarf round the neck with its two ends down the chest, draped on this torso: a soft roll round the neck in
// blocks of the team's two colours, the ends flat bands lying on the chest (on Mahmut's, on his belly) with stripes and
// a fringe.
void buildScarf(MeshBuilder& b, const Mesh& torso, float yNeck, const Color c[2], float scale) {
    const float cz = -0.006f, rr0 = 0.015f;
    std::vector<Vector3> ring;
    for (int i = 0; i <= 28; ++i) {
        const float a = 2.f * PI_F * (float)i / 28.f;
        const float front = std::max(0.f, -std::cos(a));
        const float y = yNeck - 0.022f * front;
        const float rr = reach(torso, a, y, cz, 0.07f * scale) + rr0 * 0.6f;
        ring.push_back({std::sin(a) * rr, y, cz + std::cos(a) * rr});
    }
    for (int k = 0; k < 7; ++k) {  // blocks of colour round the neck
        std::vector<Vector3> seg(ring.begin() + k * 4, ring.begin() + std::min(28, k * 4 + 4) + 1);
        b.tube(seg, rr0, 8, c[k % 2]);
    }
    auto band = [&](float x0, float x1, float yEnd, float w) {
        const int n = 10;
        int pl = -1, pr = -1;
        const float y0 = yNeck - 0.03f;
        for (int k = 0; k <= n; ++k) {
            const float t = (float)k / n, x = x0 + (x1 - x0) * t, y = y0 - (y0 - yEnd) * t;
            const float z = std::min(frontZ(torso, x - w * 0.4f, y, -0.11f), frontZ(torso, x + w * 0.4f, y, -0.11f)) - 0.007f;
            const float along = (y0 - y);
            const bool stripe = t > 0.62f && ((int)(along / 0.022f)) % 2 == 1;
            const Color col = stripe ? c[1] : c[0];
            const int l = b.vertex({x - w * 0.5f, y, z}, {0, 0, -1}, {0, t}, col), r = b.vertex({x + w * 0.5f, y, z}, {0, 0, -1}, {1, t}, col);
            if (pl >= 0) b.quad(pl, pr, r, l);
            pl = l, pr = r;
            if (k == n)  // the fringe
                for (int f = 0; f < 6; ++f) {
                    const float fx = x - w * 0.42f + w * 0.84f * (float)f / 5.f;
                    b.capsule({fx, y, z}, {fx, y - 0.022f, z - 0.002f}, 0.0022f, 4, c[1]);
                }
        }
    };
    band(0.03f * scale, 0.045f * scale, yNeck - 0.24f, 0.058f);
    band(0.075f * scale, 0.085f * scale, yNeck - 0.17f, 0.054f);
}

// Standing men for the day (Watcher bodies, posed by Cast::updateWatcher with their own focus).
struct StandSpot {
    Vector3 pos, focus;
    int variant;
};
// Maç: in front of the TV inside (high in the back-left corner) / around the portable TV in the garden.
const StandSpot kTvIn[3] = {{{-2.05f, 0.f, -0.55f}, {-3.7f, 2.4f, -2.9f}, 2},
                            {{-3.05f, 0.f, -0.45f}, {-3.7f, 2.4f, -2.9f}, 5},
                            {{-3.55f, 0.f, -1.15f}, {-3.7f, 2.4f, -2.9f}, 0}};
const StandSpot kTvOut[3] = {{{0.35f, 0.f, -2.25f}, {1.42f, 1.1f, -2.95f}, 2},  // (clear of our line of sight to the set)
                             {{1.95f, 0.f, -2.45f}, {1.42f, 1.1f, -2.95f}, 5},
                             {{-0.2f, 0.f, -2.6f}, {1.42f, 1.1f, -2.95f}, 0}};
// Ramazan evenings: two men watching the card players (w3d::BG_TABLES[2]).
const StandSpot kRamazan[2] = {{{-1.75f, 0.f, 0.85f}, {-2.6f, 0.8f, 1.7f}, 3}, {{-3.1f, 0.f, 0.55f}, {-2.6f, 0.8f, 1.7f}, 1}};

}  // namespace

struct Special {
    // per torso: [0..5] patron variants, [6..8] seats 1..3
    Mesh bayram[9]{}, scarf[9]{};
    Mat mat{};
    Watcher stand[3];
    int standN = 0, standKind = -1;  // 0 the derby inside, 1 the derby in the garden, 2 Ramazan
    const StandSpot* spots = nullptr;
};

void Cast::initSpecial(Renderer& r) {
    if (sp) return;
    sp = new Special;
    Special& S = *sp;
    S.mat = r.makeMat(WHITE, Texture2D{}, 0.06f, 10.f, 0.f, 0.3f);
    const Color ties[6][2] = {{{110, 20, 30, 255}, {150, 40, 50, 255}}, {{30, 40, 90, 255}, {150, 130, 70, 255}},
                              {{40, 70, 50, 255}, {30, 50, 36, 255}},   {{90, 30, 60, 255}, {120, 60, 90, 255}},
                              {{20, 24, 30, 255}, {70, 70, 76, 255}},   {{120, 90, 40, 255}, {90, 64, 30, 255}}};
    for (int v = 0; v < Meshes::PATRON_VARIANTS; ++v) {
        const Mesh& t = M.patron[v].torso;
        MeshBuilder b;
        if (v % 2 == 0) buildTie(b, t, 0.40f, 0.235f, ties[v][0], ties[v][1]);
        else buildCarnation(b, t, -0.085f, 0.31f, true);
        S.bayram[v] = b.build(true);
        MeshBuilder s;
        buildScarf(s, t, 0.44f, v % 2 ? kTeamB : kTeamA, 1.f);
        S.scarf[v] = s.build(true);
    }
    {  // the regulars: Rıza and Nuri a tie, Mahmut (open collar) a carnation; Mahmut's scarf for the derby
        MeshBuilder b1, b2, b3, s2;
        buildTie(b1, M.person[1].torso, 0.405f, 0.245f, Color{120, 22, 34, 255}, Color{160, 120, 60, 255});
        buildCarnation(b2, M.person[2].torso, -0.1f, 0.33f, false);
        buildTie(b3, M.person[3].torso, 0.405f, 0.25f, Color{36, 46, 92, 255}, Color{120, 130, 160, 255});
        buildScarf(s2, M.person[2].torso, 0.435f, kTeamA, 1.1f);
        S.bayram[6] = b1.build(true);
        S.bayram[7] = b2.build(true);
        S.bayram[8] = b3.build(true);
        S.scarf[7] = s2.build(true);
    }
    for (Watcher& w : S.stand) w = Watcher{};
}

void Cast::freeSpecial(Renderer& r) {
    if (!sp) return;
    for (Mesh* m : {sp->bayram, sp->scarf})
        for (int i = 0; i < 9; ++i)
            if (m[i].vertexCount > 0) {
                UnloadMesh(m[i]);
                m[i] = Mesh{};
            }
    r.unloadMat(sp->mat);
    delete sp;
    sp = nullptr;
}

void Cast::updateSpecial(float dt) {
    if (!sp) return;
    Special& S = *sp;
    // who stands today
    int kind = -1;
    if (specialDay == w3d::GunMac) kind = specialGarden ? 1 : 0;
    else if (w3d::ramazanEvening(specialDay, phase)) kind = 2;
    if (kind != S.standKind) {
        S.standKind = kind;
        S.spots = kind == 0 ? kTvIn : kind == 1 ? kTvOut : kind == 2 ? kRamazan : nullptr;
        S.standN = kind < 0 ? 0 : kind == 2 ? 2 : 3;
        for (int i = 0; i < S.standN; ++i) {
            Watcher& w = S.stand[i];
            const StandSpot& s = S.spots[i];
            w = Watcher{};
            w.variant = s.variant;
            w.L = lookFor(10 + w.variant);
            w.L.hipPivot = {0.f, 0.94f, 0.f};
            w.pm = &M.patron[w.variant];
            w.state = 1;
            w.came = true;
            w.pos = s.pos;
            const Vector3 d = Vector3Subtract(s.focus, s.pos);
            w.yaw = std::atan2(-d.x, -d.z);
            w.wait = rng.f(1.f, 3.f);
            w.gaze = s.focus;
        }
    }
    // pose them: Cast::updateWatcher faces and watches tableFocus, so lend it each man's own focus
    const Vector3 keep = tableFocus;
    for (int i = 0; i < S.standN; ++i) {
        tableFocus = S.spots[i].focus;
        updateWatcher(S.stand[i], dt);
    }
    tableFocus = keep;
}

void Cast::specialCrowdReact(int kind, Vector3 where) {
    if (!sp) return;
    for (int i = 0; i < sp->standN; ++i) {
        Watcher& w = sp->stand[i];
        w.reactIn = rng.f(0.05f, 0.4f);
        w.reactKind = kind;
        w.gaze = where;
        w.gazeHold = 2.5f;
    }
}

// (duzelt) the day's standing men, for the cat's floor plan (Characters::floorPeople): x, radius, z
void Cast::specialFloorPeople(std::vector<Vector3>& out) const {
    if (!sp) return;
    for (int i = 0; i < sp->standN; ++i) out.push_back({sp->stand[i].pos.x, 0.32f, sp->stand[i].pos.z});
}

void Cast::submitSpecial(Renderer& r) {
    if (!sp) return;
    Special& S = *sp;
    const bool bayram = w3d::isBayram(specialDay), mac = specialDay == w3d::GunMac;
    if (bayram || mac) {
        Mesh* set = bayram ? S.bayram : S.scarf;
        for (int s = 1; s <= 3; ++s)
            if (set[5 + s].vertexCount > 0) {
                r.submit(&set[5 + s], &S.mat, opp[s].torsoW, DoubleSided);
                ++submitCount;
            }
        for (size_t i = 0; i < patrons.size(); ++i) {
            const Patron& p = patrons[i];
            if (!p.present || (mac && (p.scarf || i % 2 == 1))) continue;  // (a winter scarf already; half are fans)
            r.submit(&set[p.variant % Meshes::PATRON_VARIANTS], &S.mat, p.torsoW, DoubleSided);
            ++submitCount;
        }
    }
    for (int i = 0; i < S.standN; ++i) {
        const Watcher& w = S.stand[i];
        const PersonMeshes& pm = *w.pm;
        for (int k = 0; k < 2; ++k) {
            r.submit(&M.walkThigh[k], &pm.cloth, w.thighW[k], CastShadow);
            r.submit(&M.walkShin[k], &pm.cloth, w.shinW[k], CastShadow);
        }
        r.submit(&pm.torso, &pm.cloth, w.torsoW, CastShadow);
        r.submit(&pm.head, &pm.headMat, w.headW, 0);
        for (int a = 0; a < 2; ++a) {
            r.submit(&pm.upper[a], &pm.cloth, w.upperW[a], 0);
            r.submit(&pm.fore[a], &pm.cloth, w.foreW[a], 0);
            r.submit(&M.handLo[a][(int)w.pose[a]], &pm.skin, w.handW[a], 0);
        }
        if (mac) r.submit(&S.scarf[w.variant % Meshes::PATRON_VARIANTS], &S.mat, w.torsoW, DoubleSided);
        else if (seasonNow == w3d::Kis) r.submit(&scarf[(w.variant + 1) % 3], &scarfMat, w.torsoW, 0);
        submitCount += 11;
    }
}

}  // namespace chr

// ============================================================================ public API
void Characters::setSpecialDay(int day, bool garden) {
    Impl& m = *impl_;
    m.specialDay = std::clamp(day, 0, 4);
    m.specialGarden = garden;
}

}  // namespace r3d
