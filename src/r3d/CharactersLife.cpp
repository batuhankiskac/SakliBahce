// Characters module: life around our table.
//   * Time of day / season (Characters::setTimeOfDay / setSeason, r3d/Daytime.h): which background tables are
//     busy at this hour (the room's lamps over the empty ones are off by day), scarves in winter.
//   * The crowd reacts to big moments at our table (Characters::crowdReact): heads turn, arms go up, hands clap,
//     somebody half rises from his chair, holds his head or laughs; a shout or two for App ("Vay be!").
//   * Bystanders (Characters::setSpectatorMatch): during a long match one, later two men come in from the street,
//     stand behind Kel Mahmut's chair with their hands behind their backs, follow the game and leave after it.
//   * Tea rounds (Characters::orderTeaRound / serveTea): "Çaylar benden!" — the çaycı brings a round to everyone.
#include "r3d/CharactersState.h"
#include "r3d/Daytime.h"

#include <algorithm>
#include <cstdlib>

namespace r3d {

using namespace chr;

namespace chr {
namespace {


const char* const kCheer[] = {"Vay be!", "Helal olsun!", "Oha!", "Maşallah!", "Bravo!", "Vay anasını!", "Ustaya bak, usta!", "Oooo!"};
const char* const kGroan[] = {"Yazık oldu…", "Tüh!", "Hay aksi!", "Vah vah…", "Olmadı be…", "Eyvah eyvah…"};
const char* const kLaugh[] = {"Ha ha ha!", "Yuh be!", "Oldu mu şimdi?", "Gülmekten öldük!", "Hah hah hah!"};
const char* const kTreat[] = {"Çaylar benden!", "Ocakçı! Herkese çay, benden!", "Bu sefer çaylar benden, beyler!"};
const char* const kForPlayer[] = {"Ocakçı, çayları tazele! Kazanan ısmarlıyor!", "Çaylar beyden! Herkese birer çay!",
                                  "Ocakçı! Bizim beyden herkese çay!"};
const char* const kBoyRound[] = {"Hemen abi, herkese geliyor!", "Emredersin abi, demliği tazeledim!", "Geliyor, geliyor!"};

// Bystanders: in from the street door, along the aisle past the tavla table, to behind Kel Mahmut's chair.
const Vector3 kWayIn[] = {{-4.75f, 0.f, 2.8f}, {-3.9f, 0.f, 2.75f}, {-3.88f, 0.f, 1.7f}, {-3.7f, 0.f, 0.05f},
                          {-1.65f, 0.f, -0.02f}, {-1.3f, 0.f, -0.92f}};
const Vector3 kSpot[2] = {{-0.58f, 0.f, -1.56f}, {0.62f, 0.f, -1.62f}};
const Vector3 kBehind{-0.55f, 0.f, -2.08f};  // the second one walks round behind the first
// At the tavla table (w3d::TAVLA_TABLE): south of the card players, then up behind the opponent's chair.
const Vector3 kTavlaWayIn[] = {{-4.75f, 0.f, 2.8f}, {-3.9f, 0.f, 2.75f}, {-3.88f, 0.f, 1.7f}, {-3.6f, 0.f, 0.55f},
                               {-1.75f, 0.f, 0.55f}, {-1.4f, 0.f, 1.35f}};
const Vector3 kTavlaSpot[2] = {{-1.24f, 0.f, 2.02f}, {-1.30f, 0.f, 2.66f}};
const int kWatchVariant[2] = {4, 1};

}  // namespace

// Standing / walking legs (the çaycı's gait; the bystanders' too).
void walkLegs(const Matrix& root, Vector3 hip, float phase, float walk, Matrix thighW[2], Matrix shinW[2]) {
    const float ph = phase * 2.f * PI_F;
    for (int i = 0; i < 2; ++i) {
        const float sd = i == 0 ? 1.f : -1.f;
        float lp = ph + (i == 0 ? 0.f : PI_F);
        float u = std::fmod(phase + (i == 0 ? 0.f : 0.5f), 1.f);
        float thigh = 0.38f * std::cos(lp) * walk + 0.02f;
        auto bump = [](float x, float c, float w) { return std::exp(-((x - c) / w) * ((x - c) / w)); };
        float knee = 0.06f + (0.12f * bump(u, 0.12f, 0.08f) + 0.95f * bump(u, 0.72f, 0.13f) + 0.95f * bump(u, -0.28f, 0.13f)) * walk;
        Vector3 hipJ = xfPoint(root, Vector3Add(hip, {sd * 0.088f, -0.03f, 0.01f}));
        Vector3 thighDir = xfDir(root, {0.012f * sd, -std::cos(thigh), -std::sin(thigh)});
        Vector3 kneeP = Vector3Add(hipJ, Vector3Scale(vnorm(thighDir), 0.44f));
        float sh = thigh - knee;
        Vector3 shinDir = xfDir(root, {0.f, -std::cos(sh), -std::sin(sh)});
        Vector3 ankle = Vector3Add(kneeP, Vector3Scale(vnorm(shinDir), 0.43f));
        Vector3 back = xfDir(root, {0, 0, 1});
        thighW[i] = boneMatrix(hipJ, kneeP, back);
        shinW[i] = boneMatrix(kneeP, ankle, back);
    }
}

// ============================================================================ init / look
void Cast::initLife(Renderer& r) {
    // a knitted scarf round the neck with its two ends over the chest (torso space of a patron)
    const Color cols[3][2] = {{{132, 34, 42, 255}, {214, 200, 176, 255}},
                              {{40, 54, 92, 255}, {170, 150, 96, 255}},
                              {{92, 98, 80, 255}, {150, 60, 46, 255}}};
    for (int v = 0; v < 3; ++v) {
        MeshBuilder b;
        std::vector<Vector3> ring;
        for (int i = 0; i <= 28; ++i) {
            const float a = 2.f * PI_F * (float)i / 28.f;
            const float front = std::max(0.f, -std::cos(a));  // -Z is the front
            ring.push_back({std::sin(a) * 0.078f, 0.452f - 0.03f * front, std::cos(a) * 0.072f - 0.006f - 0.012f * front});
        }
        b.tube(ring, 0.026f, 10, cols[v][0]);
        b.tube({{0.035f, 0.43f, -0.082f}, {0.05f, 0.37f, -0.108f}, {0.058f, 0.29f, -0.118f}, {0.06f, 0.24f, -0.12f}}, 0.021f, 8, cols[v][0]);
        b.tube({{0.07f, 0.425f, -0.07f}, {0.088f, 0.36f, -0.098f}, {0.094f, 0.30f, -0.108f}}, 0.019f, 8, cols[v][0]);
        b.tube({{0.06f, 0.255f, -0.121f}, {0.061f, 0.238f, -0.122f}}, 0.0225f, 8, cols[v][1]);  // a stripe near the end
        b.tube({{0.094f, 0.312f, -0.109f}, {0.095f, 0.296f, -0.11f}}, 0.0205f, 8, cols[v][1]);
        scarf[v] = b.build(true);
    }
    scarfMat = r.makeMat(WHITE, Texture2D{}, 0.04f, 8.f, 0.f, 0.35f);
    for (int i = 0; i < 2; ++i) {
        Watcher& w = watchers[i];
        w = Watcher{};
        w.variant = kWatchVariant[i];
        w.L = lookFor(10 + w.variant);
        w.L.hipPivot = {0.f, 0.94f, 0.f};
        w.pm = &M.patron[w.variant];
    }
    evalLook(true);
}

void Cast::freeLife(Renderer& r) {
    for (Mesh& m : scarf)
        if (m.vertexCount > 0) {
            UnloadMesh(m);
            m = Mesh{};
        }
    r.unloadMat(scarfMat);
    crowdLines.clear();
    teaServed.clear();
}

void Cast::evalLook(bool force) {
    const int ph = w3d::resolveDayPhase(dayMode), se = w3d::resolveSeason(seasonMode);
    if (!force && ph == phase && se == seasonNow) return;
    phase = ph;
    seasonNow = se;
    for (Patron& p : patrons) {
        p.present = w3d::bgTableBusy(p.table, phase);
        p.scarf = seasonNow == w3d::Kis && p.seed % 3u != 0u;
        if (!p.present) p.reactIn = p.reactT = -1.f;
    }
}

int Cast::pickBgTable() {
    int busy[4], n = 0;
    for (int t = 0; t < (int)bgTables.size() && n < 4; ++t)
        if (!bgTables[(size_t)t].patrons.empty() && patrons[bgTables[(size_t)t].patrons[0]].present) busy[n++] = t;
    return n ? busy[rng.i(n)] : 0;
}

// ============================================================================ crowd reactions
Key Cast::patronRest(const Patron& p, int a, float t) const {
    const bool left = a == 1;
    const float sd = left ? -1.f : 1.f;
    Vector3 f{-sd * 0.4f, 0, -0.92f}, pl{0, -1, 0};
    Key r = mkOrtho(t, wristFor({sd * 0.19f, BY + 0.014f, -(w3d::BG_CHAIR_DIST - 0.40f)}, f, pl, PALM_CENTER, 1.f, left), f, pl, HandPose::Rest);
    if (p.prop == 1 && left) {
        Vector3 ff{0.3f, 0.75f, -0.6f}, pp{0.8f, -0.1f, 0.5f};
        r = mkOrtho(t, wristFor({-0.10f, 0.93f, -0.30f}, ff, pp, PALM_CENTER, 1.f, true), ff, pp, HandPose::Hold);
    }
    if (p.prop == 2 && !left) {
        Vector3 ff{-0.35f, 0, -0.94f}, pp{-0.94f, 0, 0.35f};
        r = mkOrtho(t, wristFor({0.20f, BY + 0.06f, -(w3d::BG_CHAIR_DIST - 0.40f)}, ff, pp, GRIP_CENTER, 1.f, false), ff, pp, HandPose::Grip);
    }
    return r;
}

void Cast::crowdShout(int kind, Vector3 where) {
    if (titleMode || crowdLineCd > 0.f) return;
    const char* t = kind == Characters::CrowdCheer ? kCheer[rng.i(8)] : kind == Characters::CrowdGroan ? kGroan[rng.i(6)] : kLaugh[rng.i(5)];
    crowdLines.push_back({t, where});
    while (crowdLines.size() > 6) crowdLines.pop_front();
    crowdLineCd = 2.2f;
}

void Cast::startPatronReaction(Patron& p) {
    p.reactT = 0.f;
    p.reactDur = rng.f(1.7f, 2.5f);
    p.gazeGoal = p.reactAt;
    p.gazeHold = p.reactDur + 0.4f;
    if (p.shout) crowdShout(p.reactKind, xfPoint(p.headW, {0, 0.12f, 0}));
    p.shout = false;
    if (p.reading) {  // the newspaper comes down a little: a look over it is all
        p.reactStyle = 9;
        return;
    }
    const bool freeR = p.prop != 2, freeL = p.prop != 1;
    const int arm = freeR ? 0 : 1;
    const float sd = arm == 0 ? 1.f : -1.f;
    switch (p.reactKind) {
    case Characters::CrowdCheer: {
        const float r = rng.f();
        p.reactStyle = r < 0.4f ? 0 : (r < 0.72f ? 1 : 2);
        if (p.reactStyle == 1 && (!freeR || !freeL)) p.reactStyle = 0;
        if (p.reactStyle == 0) {  // fists up
            for (int a = 0; a < 2; ++a) {
                if ((a == 0 && !freeR) || (a == 1 && !freeL)) continue;
                const float s = a == 0 ? 1.f : -1.f;
                Vector3 f{0, 1, 0}, pp{0, 0, -1};
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.3f, {s * 0.30f, 1.47f, -0.12f}, f, pp, HandPose::Fist, 0.f, 1));
                k.push_back(mkOrtho(0.65f, {s * 0.33f, 1.52f, -0.10f}, f, pp, HandPose::Fist));
                k.push_back(mkOrtho(1.0f, {s * 0.30f, 1.45f, -0.12f}, f, pp, HandPose::Fist));
                k.push_back(patronRest(p, a, p.reactDur));
                startTrack(p, a, TK_Gesture, k);
            }
        } else if (p.reactStyle == 1) {  // applause
            for (int a = 0; a < 2; ++a) {
                const bool left = a == 1;
                const float s = left ? -1.f : 1.f;
                Vector3 f = vnorm({-s * 0.3f, 0.5f, -0.8f}), pp{-s, 0, 0};
                std::vector<Key> k{Key{}};
                float t = 0.3f;
                for (int i = 0; i < 4; ++i) {
                    k.push_back(mkOrtho(t, wristFor({s * 0.10f, 0.97f, -0.36f}, f, pp, PALM_CENTER, 1.f, left), f, pp, HandPose::Open, 0.f, 1));
                    k.push_back(mkOrtho(t + 0.15f, wristFor({s * 0.012f, 0.97f, -0.36f}, f, pp, PALM_CENTER, 1.f, left), f, pp, HandPose::Open, 0.f, 2));
                    t += 0.31f;
                }
                k.push_back(patronRest(p, a, std::max(t + 0.35f, p.reactDur)));
                startTrack(p, a, TK_Gesture, k);
            }
        } else {  // half rises, hands pushing on the table edge, one arm thrown up
            Vector3 f{-sd * 0.3f, 0, -0.95f}, pl{0, -1, 0};
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.35f, wristFor({sd * 0.22f, BY + 0.016f, -0.36f}, f, pl, PALM_CENTER, 1.f, arm == 1), f, pl, HandPose::Open, 0.02f, 1));
            k.push_back(mkOrtho(1.2f, wristFor({sd * 0.22f, BY + 0.016f, -0.37f}, f, pl, PALM_CENTER, 1.f, arm == 1), f, pl, HandPose::Open));
            k.push_back(patronRest(p, arm, p.reactDur));
            startTrack(p, arm, TK_Gesture, k);
            const int other = 1 - arm;
            if ((other == 0 && freeR) || (other == 1 && freeL)) {
                const float s = other == 0 ? 1.f : -1.f;
                std::vector<Key> k2{Key{}};
                k2.push_back(mkOrtho(0.45f, {s * 0.26f, 1.55f, -0.22f}, {s * 0.1f, 1, -0.2f}, {0, 0, -1}, HandPose::Open, 0.f, 1));
                k2.push_back(mkOrtho(1.1f, {s * 0.28f, 1.52f, -0.2f}, {s * 0.1f, 1, -0.2f}, {0, 0, -1}, HandPose::Open));
                k2.push_back(patronRest(p, other, p.reactDur));
                startTrack(p, other, TK_Gesture, k2);
            }
        }
        break;
    }
    case Characters::CrowdGroan: {
        p.reactStyle = rng.chance(0.55f) ? 0 : 1;
        if (p.reactStyle == 0) {  // a hand to the forehead
            const bool left = arm == 1;
            Vector3 f = vnorm({-sd * 0.35f, 0.94f, 0.f}), pl{0, -0.2f, 1.f};
            pl = vnorm(Vector3Subtract(pl, Vector3Scale(f, Vector3DotProduct(pl, f))));
            Key a1 = mkOrtho(0.45f, wristFor({0.0f, 0.13f, -0.118f}, f, pl, PALM_CENTER, 1.f, left), f, pl, HandPose::Open, 0.f, 1);
            a1.headRel = true;
            a1.pole = {sd * 0.6f, -0.7f, -0.2f};
            Key a2 = a1;
            a2.t = 1.3f;
            a2.ease = 0;
            startTrack(p, arm, TK_Gesture, {Key{}, a1, a2, patronRest(p, arm, p.reactDur)});
        } else {  // "boşver": a flick of the open hand down at it
            Vector3 f{sd * 0.2f, 0.2f, -0.95f}, pl{0, -1, 0.1f};
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.35f, wristFor({sd * 0.2f, 1.05f, -0.34f}, f, pl, PALM_CENTER, 1.f, arm == 1), f, pl, HandPose::Open, 0.02f, 1));
            k.push_back(mkOrtho(0.6f, wristFor({sd * 0.24f, 0.86f, -0.4f}, f, pl, PALM_CENTER, 1.f, arm == 1), f, pl, HandPose::Open, 0.f, 2));
            k.push_back(patronRest(p, arm, std::min(p.reactDur, 1.5f)));
            startTrack(p, arm, TK_Gesture, k);
        }
        break;
    }
    default: {  // laugh: a slap on the table, or leaning back holding his belly
        p.reactStyle = rng.chance(0.5f) ? 0 : 1;
        const bool left = arm == 1;
        if (p.reactStyle == 0) {
            Vector3 f{-sd * 0.4f, -0.3f, -0.87f}, pl{-sd * 0.6f, -0.8f, 0};
            Vector3 w = wristFor({sd * 0.16f, BY + 0.03f, -0.33f}, f, pl, PALM_CENTER, 1.f, left);
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.35f, Vector3Add(w, {0, 0.1f, 0}), f, pl, HandPose::Open, 0.f, 1));
            k.push_back(mkOrtho(0.45f, w, f, pl, HandPose::Open, 0.f, 2));
            k.push_back(mkOrtho(0.7f, Vector3Add(w, {0, 0.09f, 0}), f, pl, HandPose::Open, 0.f, 1));
            k.push_back(mkOrtho(0.8f, w, f, pl, HandPose::Open, 0.f, 2));
            k.push_back(patronRest(p, arm, p.reactDur));
            startTrack(p, arm, TK_Gesture, k);
        } else {
            Vector3 f{-sd, 0.f, -0.1f}, pl{0, 0, 1};
            Key a1 = mkOrtho(0.45f, wristFor({sd * 0.05f, 0.72f, -0.2f}, f, pl, PALM_CENTER, 1.f, left), f, pl, HandPose::Open, 0.f, 1);
            Key a2 = a1;
            a2.t = 1.4f;
            startTrack(p, arm, TK_Gesture, {Key{}, a1, a2, patronRest(p, arm, p.reactDur)});
        }
        break;
    }
    }
}

// Every frame for each present patron (from updatePatron, after the talking bob).
void Cast::patronReact(Patron& p, float dt) {
    if (p.reactIn >= 0.f) {
        p.reactIn -= dt;
        if (p.reactIn < 0.f) startPatronReaction(p);
    }
    p.leanAdd = 0.f;
    p.shrugGoal = 0.f;
    if (p.reactT < 0.f) return;
    p.reactT += dt;
    const float t = p.reactT, u = clampf(t / std::max(p.reactDur, 0.1f), 0.f, 1.f);
    const float env = std::sin(PI_F * u);
    switch (p.reactKind) {
    case Characters::CrowdCheer:
        p.nodPitch += -0.12f * env;
        p.shrugGoal = 0.35f * env;
        if (p.reactStyle == 2) p.leanAdd = 0.38f * env;  // up off the chair
        else p.leanAdd = -0.05f * env;
        break;
    case Characters::CrowdGroan:
        p.leanAdd = (p.reactStyle == 0 ? 0.12f : -0.1f) * env;
        p.nodPitch += 0.1f * env;
        if (p.reactStyle == 1) p.nodYaw += 0.13f * std::sin(t * 9.f) * env;
        break;
    default:
        p.leanAdd = -0.16f * env;
        p.nodPitch += (-0.12f + 0.07f * std::sin(t * 15.f)) * env;
        p.shrugGoal = 0.22f * (0.5f + 0.5f * std::sin(t * 15.f)) * env;
        break;
    }
    if (p.reactStyle == 9) p.nodPitch += 0.06f * env;  // over the newspaper
    if (t >= p.reactDur) p.reactT = -1.f;
}

// ============================================================================ bystanders
void Cast::updateWatcher(Watcher& w, float dt) {
    const PersonLook& L = w.L;
    // ---- walking the path / standing at his spot
    float targetSpeed = 0.f;
    if (w.state == 0 || w.state == 2) {
        if (w.pathI < w.path.size()) {
            Vector3 d = Vector3Subtract(w.path[w.pathI], w.pos);
            d.y = 0.f;
            const float dist = Vector3Length(d);
            const bool last = w.pathI + 1 == w.path.size();
            targetSpeed = 0.95f * (last ? clampf(dist / 0.5f, 0.25f, 1.f) : 1.f);
            if (dist < (last ? 0.05f : 0.25f)) {
                ++w.pathI;
            } else {
                const float want = std::atan2(-d.x, -d.z);
                const float diff = wrapAngle(want - w.yaw);
                w.yaw = wrapAngle(w.yaw + clampf(diff, -4.f * dt, 4.f * dt));
                if (std::fabs(diff) > 1.2f) targetSpeed *= 0.35f;
            }
        }
        if (w.pathI >= w.path.size()) {
            if (w.state == 0) {
                w.state = 1;
                w.wait = 0.f;
            } else {
                w.state = -1;  // out of the door
                return;
            }
        }
    }
    if (w.state == 1) {  // face the table
        const Vector3 d = Vector3Subtract(Vector3{tableFocus.x, 0, tableFocus.z}, w.pos);
        const float want = std::atan2(-d.x, -d.z);
        w.yaw = wrapAngle(w.yaw + clampf(wrapAngle(want - w.yaw), -2.5f * dt, 2.5f * dt));
        w.wait += dt;
    }
    w.speed = approachExp(w.speed, targetSpeed, 5.f, dt);
    const Vector3 fwd{-std::sin(w.yaw), 0, -std::cos(w.yaw)};
    w.pos = Vector3Add(w.pos, Vector3Scale(fwd, w.speed * dt));
    w.phase += w.speed * dt / 1.35f;
    w.phase -= std::floor(w.phase);
    const float walk = clampf(w.speed / 0.8f, 0.f, 1.f);

    // ---- reaction to a big moment
    if (w.reactIn >= 0.f) {
        w.reactIn -= dt;
        if (w.reactIn < 0.f) {
            w.reactT = 0.f;
            w.reactDur = rng.f(1.6f, 2.3f);
        }
    }
    float env = 0.f;
    if (w.reactT >= 0.f) {
        w.reactT += dt;
        env = std::sin(PI_F * clampf(w.reactT / w.reactDur, 0.f, 1.f));
        if (w.reactT >= w.reactDur) w.reactT = -1.f;
    }
    const bool cheer = env > 0.f && w.reactKind == Characters::CrowdCheer;
    const bool groan = env > 0.f && w.reactKind == Characters::CrowdGroan;
    const bool laugh = env > 0.f && w.reactKind == Characters::CrowdLaugh;

    // ---- body
    const Matrix root = trsYaw(w.pos, w.yaw * RAD2DEG, 1.f);
    const float ph = w.phase * 2.f * PI_F;
    const float bob = -0.012f * std::cos(2.f * ph) * walk;
    const Vector3 hip = Vector3Add(L.hipPivot, {0, bob - 0.01f * walk, 0});
    float leanGoal = 0.04f * walk + (w.state == 1 ? 0.1f : 0.f) + (laugh ? -0.14f * env : 0.f) + (groan ? 0.1f * env : 0.f);
    spring(w.lean, w.leanV, leanGoal, 5.f, dt);
    const Matrix tl = mul(RY(0.06f * std::cos(ph) * walk), RX(-w.lean));
    w.torsoW = mul(mul(tl, T(hip)), root);
    // gaze: ahead while walking, the board while watching (now and then a player)
    w.gazeHold -= dt;
    Vector3 gz = w.gaze;
    if (w.state != 1) gz = Vector3Add(w.pos, Vector3Add(Vector3Scale(fwd, 2.2f), {0, 1.3f, 0}));
    else if (w.gazeHold <= 0.f) {
        w.gazeHold = rng.f(2.f, 5.f);
        const float r = rng.f();
        gz = r < 0.65f ? Vector3Add(tableFocus, {rng.f(-0.2f, 0.2f), 0.f, rng.f(-0.15f, 0.2f)})
                       : (r < 0.85f ? headTarget(tavlaSeat > 0 ? tavlaSeat : 2) : viewerPos());
    }
    w.gaze = approachExp(w.gaze, gz, 5.f, dt);
    headLookSpring(w.torsoW, L, w.gaze, 0.7f, -0.75f, 5.f, dt, w.hYaw, w.hYawV, w.hPitch, w.hPitchV);
    float nod = 0.f, shake = 0.f;
    if (laugh) nod = (-0.1f + 0.08f * std::sin(w.reactT * 15.f)) * env;
    if (groan) shake = 0.12f * std::sin(w.reactT * 9.f) * env;
    if (cheer) nod = -0.1f * env;
    const Matrix headT = mul(mul(RX(w.hPitch + nod), RY(w.hYaw + shake)), T({0, L.spineLen, L.headZ}));
    w.headW = mul(headT, w.torsoW);
    walkLegs(root, hip, w.phase, walk, w.thighW, w.shinW);

    // ---- arms: swinging while walking, hands behind the back while watching, up / on the head / on the belly
    const float hs = L.handScale;
    for (int a = 0; a < 2; ++a) {
        const float sd = a == 0 ? 1.f : -1.f;
        const bool left = a == 1;
        Vector3 tgt, f, pl;
        HandPose pose = HandPose::Rest;
        const float sw = -0.3f * std::cos(ph + (left ? PI_F : 0.f)) * walk;
        if (cheer) {
            f = {0, 1, 0}, pl = {0, 0, -1}, pose = HandPose::Fist;
            tgt = {sd * 0.3f, 1.86f + 0.04f * std::sin(w.reactT * 9.f), -0.08f};
        } else if (groan) {
            f = {-sd, 0.1f, 0.3f}, pl = {0, -1, 0}, pose = HandPose::Open;
            tgt = xfPoint(MatrixInvert(root), xfPoint(w.headW, {sd * 0.09f, 0.21f, -0.02f}));
        } else if (laugh) {
            f = {-sd, 0.f, -0.1f}, pl = {0, 0, 1}, pose = HandPose::Open;
            tgt = wristFor({sd * 0.07f, 1.06f, -0.17f}, f, pl, PALM_CENTER, hs, left);
        } else if (w.state == 1 && w.wait > 0.8f) {
            f = {-sd * 0.3f, -0.85f, 0.4f}, pl = {-sd, 0, 0}, pose = HandPose::Grip;
            tgt = {sd * 0.07f, 0.9f, 0.17f};
        } else {
            f = {0, -std::cos(sw), -std::sin(sw)}, pl = {-sd, 0, 0};
            tgt = {sd * 0.215f, 1.34f - 0.57f * std::cos(sw), -0.57f * std::sin(sw) + 0.02f};
        }
        if (!w.wristInit) w.wristL[a] = tgt;
        w.wristL[a] = approachExp(w.wristL[a], tgt, env > 0.f ? 9.f : 6.f, dt);
        w.pose[a] = pose;
        const Vector3 shoulder = xfPoint(w.torsoW, {sd * L.shoulderW, L.shoulderY, 0.012f});
        Vector3 wr = xfPoint(root, w.wristL[a]);
        const Vector3 pole = xfDir(root, vnorm({sd * 0.5f, -0.6f, 0.5f}));
        const Vector3 elbow = solveTwoBone(shoulder, wr, L.upperArm, L.foreArm, pole);
        const Vector3 back = xfDir(root, {0, 0, 1});
        w.upperW[a] = boneMatrix(shoulder, elbow, back);
        w.foreW[a] = boneMatrix(elbow, wr, back);
        w.handW[a] = handMatrix(wr, vnorm(xfDir(root, f)), vnorm(xfDir(root, pl)), hs);
    }
    w.wristInit = true;
}

void Cast::updateLife(float dt) {
    lookCheckT -= dt;
    if (lookCheckT <= 0.f) {
        lookCheckT = 20.f;
        evalLook(false);
    }
    crowdLineCd = std::max(0.f, crowdLineCd - dt);
    // developer demo for snapshots (SAKLI_KALABALIK=1): bystanders at once, a reaction every 5 s, a tea round
    static const bool demo = std::getenv("SAKLI_KALABALIK") != nullptr;
    if (demo && !titleMode) {
        if (!spectate) {
            spectate = true;
            spectateT = 74.f;
        }
        static float demoT = 0.f;
        const float prev = demoT;
        demoT += dt;
        if (prev < 1.f && demoT >= 1.f) owner->orderTeaRound(0);
        if (std::fmod(prev, 5.f) > std::fmod(demoT, 5.f) && demoT > 4.f) owner->crowdReact((int)(demoT / 5.f) % 3);
    }
    // bystanders come during a long match and leave after it
    if (spectate && !titleMode) {
        spectateT += dt;
        for (int i = 0; i < 2; ++i) {
            Watcher& w = watchers[i];
            const float due = i == 0 ? 40.f : 75.f;
            if (w.state != -1 || spectateT < due || w.came) continue;
            w.came = true;
            w.state = 0;
            if (tavlaSeat > 0) {
                w.path.assign(std::begin(kTavlaWayIn), std::end(kTavlaWayIn));
                w.path.push_back(kTavlaSpot[i]);
            } else {
                w.path.assign(std::begin(kWayIn), std::end(kWayIn));
                if (i == 1) w.path.push_back(kBehind);
                w.path.push_back(kSpot[i]);
            }
            for (Vector3& p : w.path) p = Vector3Add(p, {rng.f(-0.04f, 0.04f), 0.f, rng.f(-0.04f, 0.04f)});
            w.pathI = 1;
            w.pos = w.path[0];
            w.yaw = -PI_F * 0.5f;  // facing +X, into the room
            w.speed = 0.f;
            w.wristInit = false;
            w.gaze = {0, 0.8f, 0};
        }
    }
    for (Watcher& w : watchers) {
        if (w.state == -1) continue;
        if ((!spectate || titleMode) && w.state != 2) {  // the match is over: a last look, then off
            w.wait = std::min(w.wait, 0.f) - dt;
            if (w.wait < -rng.f(1.5f, 3.5f) || w.state == 0) {
                std::vector<Vector3> back;
                back.push_back(w.pos);
                const size_t upto = w.state == 0 ? std::min(w.pathI, w.path.size()) : w.path.size() - 1;
                for (size_t k = upto; k-- > 0;) back.push_back(w.path[k]);
                w.path = back;
                w.pathI = 1;
                w.state = 2;
            }
        }
        updateWatcher(w, dt);
    }
}

// ============================================================================ submission
void Cast::submitLife(Renderer& r) {
    if (seasonNow == w3d::Kis)
        for (const Patron& p : patrons)
            if (p.present && p.scarf) {
                r.submit(&scarf[p.variant % 3], &scarfMat, p.torsoW, 0);
                ++submitCount;
            }
    for (const Watcher& w : watchers) {
        if (w.state == -1) continue;
        const PersonMeshes& pm = *w.pm;
        for (int i = 0; i < 2; ++i) {
            r.submit(&M.walkThigh[i], &pm.cloth, w.thighW[i], CastShadow);
            r.submit(&M.walkShin[i], &pm.cloth, w.shinW[i], CastShadow);
        }
        r.submit(&pm.torso, &pm.cloth, w.torsoW, CastShadow);
        r.submit(&pm.head, &pm.headMat, w.headW, 0);
        for (int a = 0; a < 2; ++a) {
            r.submit(&pm.upper[a], &pm.cloth, w.upperW[a], 0);
            r.submit(&pm.fore[a], &pm.cloth, w.foreW[a], 0);
            r.submit(&M.handLo[a][(int)w.pose[a]], &pm.skin, w.handW[a], 0);
        }
        if (seasonNow == w3d::Kis) r.submit(&scarf[(w.variant + 1) % 3], &scarfMat, w.torsoW, 0);
        submitCount += 11;
    }
}

}  // namespace chr

// ============================================================================ public API
void Characters::setTimeOfDay(int mode) {
    Impl& m = *impl_;
    if (mode == m.dayMode) return;
    m.dayMode = mode;
    if (m.ready) m.evalLook(false);
}

void Characters::setSeason(int mode) {
    Impl& m = *impl_;
    if (mode == m.seasonMode) return;
    m.seasonMode = mode;
    if (m.ready) m.evalLook(false);
}

void Characters::crowdReact(int kind, Vector3 where, float strength) {
    Impl& m = *impl_;
    if (!m.ready) return;
    kind = std::clamp(kind, 0, 2);
    strength = std::clamp(strength, 0.f, 1.f);
    static const float kJoin[3] = {0.75f, 0.5f, 0.55f};
    Patron* first = nullptr;
    for (Patron& p : m.patrons) {
        if (!p.present || p.reactT >= 0.f || p.reactIn >= 0.f) continue;
        const float d = Vector3Distance(p.pos, where);
        if (!m.rng.chance(kJoin[kind] * strength * (d < 3.f ? 1.f : 0.75f))) {
            if (m.rng.chance(0.6f)) {  // the others at least look over
                p.gazeGoal = where;
                p.gazeHold = m.rng.f(1.f, 2.2f);
            }
            continue;
        }
        p.reactIn = m.rng.f(0.08f, 0.5f) + d * 0.04f;
        p.reactKind = kind;
        p.reactAt = where;
        p.shout = false;
        if (!first || p.reactIn < first->reactIn) first = &p;
    }
    if (first) first->shout = true;
    if (!m.titleMode) m.faceCrowd(kind);  // (Yüz) the regulars' faces too
    for (Watcher& w : m.watchers) {
        if (w.state != 1) continue;
        w.reactIn = m.rng.f(0.05f, 0.35f);
        w.reactKind = kind;
        w.gaze = where;
        w.gazeHold = 2.5f;
    }
    m.specialCrowdReact(kind, where);  // (ozelgun) the men standing by the TV on a derby night
    // a bystander shouts when no patron does (or as the second voice of a big cheer)
    if ((!first || (kind == CrowdCheer && m.rng.chance(0.4f))) && m.watchers[0].state == 1) {
        m.crowdLineCd = first ? 0.f : m.crowdLineCd;
        if (!first) m.crowdShout(kind, xfPoint(m.watchers[0].headW, {0, 0.12f, 0}));
    }
}

bool Characters::consumeCrowdLine(std::string& text, Vector3& where) {
    Impl& m = *impl_;
    if (m.crowdLines.empty()) return false;
    text = m.crowdLines.front().text;
    where = m.crowdLines.front().where;
    m.crowdLines.pop_front();
    return true;
}

void Characters::setSpectatorMatch(bool on) {
    Impl& m = *impl_;
    if (on == m.spectate) return;
    m.spectate = on;
    if (on) {
        m.spectateT = 0.f;
        for (Watcher& w : m.watchers) w.came = false;
    }
}

void Characters::serveTea(bool round) {
    Impl& m = *impl_;
    if (!m.ready) return;
    if (round) {
        m.boy.roundQueued = true;
        if (m.boy.state == 0) m.boy.timer = std::min(m.boy.timer, 1.2f);
    } else {
        m.boy.called = true;
    }
}

void Characters::orderTeaRound(int payerSeat) {
    Impl& m = *impl_;
    if (!m.ready) return;
    serveTea(true);
    if (m.titleMode) return;
    const int caller = (payerSeat >= 1 && payerSeat <= 3) ? payerSeat : 1 + m.rng.i(3);
    const char* line = (payerSeat >= 1 && payerSeat <= 3) ? kTreat[m.rng.i(3)] : kForPlayer[m.rng.i(3)];
    m.pushLine(caller, line, 2.8f, 4.f, true);  // a tea order: he waves toward the counter
    m.pushLine(4, kBoyRound[m.rng.i(3)], 2.2f, 5.f);
}

bool Characters::consumeTeaServed(int& seat) {
    Impl& m = *impl_;
    if (m.teaServed.empty()) return false;
    seat = m.teaServed.front();
    m.teaServed.pop_front();
    return true;
}

}  // namespace r3d
