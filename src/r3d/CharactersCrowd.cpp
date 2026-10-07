// Characters module: the room's other people — background patrons playing tavla, okey and cards or chatting
// over tea at w3d::BG_TABLES, and the çaycı who walks a waypoint graph around the tables with his hanging
// tray, serving our table (refilling every glass) and sometimes the others.
#include "r3d/CharactersState.h"

#include <algorithm>
#include <queue>

namespace r3d {
namespace chr {

namespace {

// (wristFor, mkOrtho, mirrorL, BY: CharactersState.h)

// ---------------------------------------------------------------------------- the çaycı's walkable graph
struct Node {
    float x, z;
};
const Node kNodes[] = {
    {3.05f, -2.60f},  // 0 counter stand
    {3.78f, -2.30f},  // 1
    {3.78f, -0.05f},  // 2
    {1.62f, 0.00f},   // 3 right of seat 1
    {0.92f, -0.92f},  // 4 serve glass 2 (Mahmut)
    {0.92f, 0.92f},   // 5 serve glass 1 (Rıza)
    {-0.85f, 1.12f},  // 6 serve glass 0 (the human)
    {-0.92f, -0.92f}, // 7 serve glass 3 (Nuri)
    {0.00f, -1.45f},  // 8 behind Mahmut
    {0.25f, 1.50f},   // 9 behind the human
    {-1.62f, 0.00f},  // 10 left of seat 3
    {-3.72f, 0.10f},  // 11 left aisle
    {3.42f, 1.02f},   // 12 T3 corner
    {3.32f, -1.05f},  // 13 T1 corner
    {-3.45f, -0.98f}, // 14 T0 corner
    {-3.48f, 0.92f},  // 15 T2 corner
    {-0.22f, 1.52f},  // 16 the tavla table: serve the opponent's glass (node 9 serves the player's)
};
constexpr int kNodeCount = (int)(sizeof(kNodes) / sizeof(kNodes[0]));
const int kEdges[][2] = {{0, 1},  {1, 13}, {13, 2}, {1, 2},   {2, 12}, {2, 3},   {3, 4},   {3, 5},
                         {4, 8},  {8, 7},  {5, 9},  {9, 6},   {6, 10}, {7, 10},  {10, 11}, {11, 14},
                         {11, 15}, {9, 16}};
const int kServeNode[4] = {6, 5, 4, 7};     // glass index -> node (at the okey table)
const int kBgNode[4] = {14, 13, 15, 12};    // bg table -> node

std::vector<int> shortestPath(int from, int to) {
    std::vector<float> dist(kNodeCount, 1e9f);
    std::vector<int> prev(kNodeCount, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[from] = 0.f;
    q.push({0.f, from});
    while (!q.empty()) {
        auto [d, u] = q.top();
        q.pop();
        if (d > dist[u]) continue;
        for (const auto& e : kEdges) {
            int v = e[0] == u ? e[1] : (e[1] == u ? e[0] : -1);
            if (v < 0) continue;
            float w = std::hypot(kNodes[u].x - kNodes[v].x, kNodes[u].z - kNodes[v].z);
            if (d + w < dist[v]) {
                dist[v] = d + w;
                prev[v] = u;
                q.push({dist[v], v});
            }
        }
    }
    std::vector<int> path;
    for (int v = to; v >= 0; v = prev[v]) {
        path.push_back(v);
        if (v == from) break;
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != from) path = {from, to};
    return path;
}

int nearestNode(Vector3 p) {
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < kNodeCount; ++i) {
        float d = std::hypot(kNodes[i].x - p.x, kNodes[i].z - p.z);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

const char* const kBoyLines[] = {"Taze çaylar geldi abiler!", "Buyrun, tavşan kanı!", "Afiyet olsun!",
                                 "Demli demli, buyrun!", "Çaylar tazelendi!"};
const char* const kBoyNuri = "Oraletin de sıcak, Nuri Amca!";

} // namespace

// ============================================================================ setup
void Cast::setupCrowd() {
    patrons.clear();
    bgTables.clear();
    const int variantOrder[11] = {0, 4, 2, 1, 5, 3, 3, 0, 5, 1, 2};
    int vi = 0;
    for (int t = 0; t < 4; ++t) {
        const w3d::BgTable& bt = w3d::BG_TABLES[t];
        BgTable T;
        T.kind = bt.kind;
        T.c = {bt.x, 0.f, bt.z};
        T.timer = rng.f(1.f, 4.f);
        int role = 0;
        for (int side = 0; side < 4; ++side) {
            if (!(bt.sides & (1u << side))) continue;
            Patron p;
            p.who = 10 + (int)patrons.size();
            p.table = t;
            p.side = side;
            p.role = role++;
            p.variant = variantOrder[vi++ % 11] % Meshes::PATRON_VARIANTS;
            p.L = lookFor(10 + p.variant);
            p.pm = &M.patron[p.variant];
            p.pos = {bt.x + w3d::SEAT_DIR[side].x * w3d::BG_CHAIR_DIST, 0.f, bt.z + w3d::SEAT_DIR[side].z * w3d::BG_CHAIR_DIST};
            p.yawDeg = w3d::seatYawDeg(side) + rng.f(-6.f, 6.f);
            p.root = trsYaw(p.pos, p.yawDeg, 1.f);
            p.rootInv = MatrixInvert(p.root);
            p.L.hipPivot.y = 0.60f;
            p.leanBase = rng.f(0.10f, 0.3f);
            p.lean = p.leanBase;
            p.breath = rng.f(0.f, 6.f);
            p.breathRate = rng.f(0.2f, 0.28f);
            p.headStiff = rng.f(4.5f, 6.5f);
            p.seed = (uint32_t)rng.next();
            p.gaze = p.gazeGoal = {bt.x, BY, bt.z};
            if (T.kind == 2) p.prop = 1;
            if (T.kind == 3 && p.role == 0) p.prop = 2;
            for (int a = 0; a < 2; ++a) {
                const bool left = a == 1;
                const float sd = left ? -1.f : 1.f;
                Vector3 f{-sd * 0.4f, 0, -0.92f}, pl{0, -1, 0};
                Vector3 palm{sd * 0.19f, BY + 0.014f, -(w3d::BG_CHAIR_DIST - 0.40f)};
                Key k = mkOrtho(0, wristFor(palm, f, pl, PALM_CENTER, 1.f, left), f, pl, HandPose::Rest);
                if (p.prop == 1 && left) {  // card fan held up in front of the chest
                    Vector3 ff{0.3f, 0.75f, -0.6f}, pp{0.8f, -0.1f, 0.5f};
                    k = mkOrtho(0, wristFor({-0.10f, 0.93f, -0.30f}, ff, pp, PALM_CENTER, 1.f, true), ff, pp, HandPose::Hold);
                }
                if (p.prop == 2 && !left) {  // tea glass in hand, resting on the table edge
                    Vector3 ff{-0.35f, 0, -0.94f}, pp{-0.94f, 0, 0.35f};
                    k = mkOrtho(0, wristFor({0.20f, BY + 0.06f, -(w3d::BG_CHAIR_DIST - 0.40f)}, ff, pp, GRIP_CENTER, 1.f, false), ff, pp,
                           HandPose::Grip);
                }
                p.arm[a].hold = k;
                p.arm[a].cur = k;
            }
            T.patrons.push_back((int)patrons.size());
            patrons.push_back(p);
        }
        bgTables.push_back(T);
    }
    // the çaycı
    boy = Cayci{};
    boy.L = lookFor(3);
    boy.pm = &M.person[0];
    boy.pos = {kNodes[0].x, 0.f, kNodes[0].z};
    boy.yaw = 0.f;  // facing -Z (the counter)
    boy.nextOurs = rng.f(22.f, 30.f);
    boy.nextBg = rng.f(8.f, 14.f);
    boy.gaze = Vector3Add(w3d::COUNTER_POS, {0, 1.0f, -0.3f});
}

// ============================================================================ patrons
void Cast::updatePatron(Patron& p, BgTable& t, float dt) {
    // gaze: mostly the game, sometimes the other players, the room or our table
    p.gazeHold -= dt;
    if (p.gazeHold <= 0.f) {
        float r = rng.f();
        Vector3 c{t.c.x, BY, t.c.z};
        if (r < 0.5f) p.gazeGoal = Vector3Add(c, {rng.f(-0.15f, 0.15f), 0, rng.f(-0.15f, 0.15f)});
        else if (r < 0.78f && t.patrons.size() > 1) {
            int o = t.patrons[rng.i((int)t.patrons.size())];
            p.gazeGoal = xfPoint(patrons[o].root, {0, 1.2f, -0.1f});
        } else if (r < 0.9f) p.gazeGoal = {0, 1.1f, 0};  // a curious look at our table
        else p.gazeGoal = {rng.f(-3.f, 3.f), rng.f(1.3f, 2.3f), rng.f(-3.f, 3.f)};
        p.gazeHold = rng.f(1.5f, 4.5f);
        if (p.reading) p.gazeGoal = xfPoint(p.root, {0, 1.12f, -0.6f});
    }
    p.gaze = approachExp(p.gaze, p.gazeGoal, 6.f, dt);
    // talking bob
    const float nt = time + (float)(p.seed % 997u);
    p.nodPitch = 0.04f * (noise1(nt * 0.19f, p.seed) - 0.5f);
    p.nodYaw = 0.05f * (noise1(nt * 0.23f, p.seed + 3u) - 0.5f);
    if (p.talk > 0.f) {
        p.nodPitch += 0.05f * std::sin(time * 9.f + (float)(p.seed % 31u)) * p.talk;
        p.nodYaw += 0.04f * std::sin(time * 5.f + (float)(p.seed % 17u)) * p.talk;
    }
    patronReact(p, dt);  // a big moment at our table (CharactersLife.cpp)
    // arm tracks
    for (int a = 0; a < 2; ++a) {
        Arm& A = p.arm[a];
        if (!A.track.on) continue;
        A.track.t += dt;
        if (A.track.t >= A.track.duration()) {
            A.track.on = false;
            A.hold = A.track.keys.back();
            A.hold.t = 0.f;
        }
    }
    poseSeated(p, dt, p.headStiff);
    for (int a = 0; a < 2; ++a) resolveArm(p, a, dt, 1.f);
}

void Cast::updateCrowd(float dt) {
    for (size_t ti = 0; ti < bgTables.size(); ++ti) {
        BgTable& T = bgTables[ti];
        if (T.patrons.empty() || !patrons[T.patrons[0]].present) continue;  // nobody at this table at this hour
        T.timer -= dt;
        const Vector3 c{T.c.x, BY, T.c.z};
        auto P = [&](int i) -> Patron& { return patrons[T.patrons[(size_t)i % T.patrons.size()]]; };
        auto restKey = [&](Patron& p, int a, float t) {
            // canonical rest (table edge)
            const bool left = a == 1;
            const float sd = left ? -1.f : 1.f;
            Vector3 f{-sd * 0.4f, 0, -0.92f}, pl{0, -1, 0};
            Key r = mkOrtho(t, wristFor({sd * 0.19f, BY + 0.014f, -(w3d::BG_CHAIR_DIST - 0.40f)}, f, pl, PALM_CENTER, 1.f, left), f, pl,
                   HandPose::Rest);
            if (p.prop == 1 && left) {
                Vector3 ff{0.3f, 0.75f, -0.6f}, pp{0.8f, -0.1f, 0.5f};
                r = mkOrtho(t, wristFor({-0.10f, 0.93f, -0.30f}, ff, pp, PALM_CENTER, 1.f, true), ff, pp, HandPose::Hold);
            }
            if (p.prop == 2 && !left) {
                Vector3 ff{-0.35f, 0, -0.94f}, pp{-0.94f, 0, 0.35f};
                r = mkOrtho(t, wristFor({0.20f, BY + 0.06f, -(w3d::BG_CHAIR_DIST - 0.40f)}, ff, pp, GRIP_CENTER, 1.f, false), ff, pp,
                       HandPose::Grip);
            }
            return r;
        };
        auto reachKeys = [&](Patron& p, int a, Vector3 world, float tr, HandPose pose, bool back) {
            const bool left = a == 1;
            Vector3 tL = xfPoint(p.rootInv, world);
            Vector3 f = vnorm({tL.x * 0.6f, -0.5f, -0.86f}), pl{0, -1, 0};
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(tr * 0.75f, wristFor(Vector3Add(tL, {0, 0.05f, 0}), f, pl, PINCH_POINT, 1.f, left), f, pl, pose, 0.04f, 1));
            k.push_back(mkOrtho(tr, wristFor(Vector3Add(tL, {0, 0.015f, 0}), f, pl, PINCH_POINT, 1.f, left), f, pl, pose));
            if (back) k.push_back(mkOrtho(tr + 0.35f, wristFor({left ? -0.1f : 0.1f, BY + 0.06f, -0.4f}, f, pl, PINCH_POINT, 1.f, left), f, pl, pose, 0.03f));
            k.push_back(restKey(p, a, (back ? tr + 0.35f : tr) + 0.45f));
            startTrack(p, a, TK_Reach, k);
            p.gazeGoal = world;
            p.gazeHold = tr + 0.4f;
        };
        switch (T.kind) {
        case 0: {  // tavla
            Patron& p = P(T.turn);
            if (T.timer > 0.f) break;
            if (T.step == 0) {  // shake the dice in the fist
                Vector3 f{-0.2f, 0.2f, -0.95f}, pl{-0.9f, -0.3f, 0.1f};
                Vector3 base = wristFor({0.08f, BY + 0.16f, -0.42f}, f, pl, PALM_CENTER, 1.f, false);
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.35f, base, f, pl, HandPose::Fist, 0.f, 1));
                for (int i = 0; i < 4; ++i) {
                    k.push_back(mkOrtho(0.45f + 0.16f * i, Vector3Add(base, {0.01f, 0.035f, -0.01f}), f, pl, HandPose::Fist, 0.f, 3));
                    k.push_back(mkOrtho(0.53f + 0.16f * i, base, f, pl, HandPose::Fist, 0.f, 3));
                }
                startTrack(p, 0, TK_Gesture, k);
                p.gazeGoal = c;
                p.gazeHold = 2.f;
                T.step = 1;
                T.timer = 1.15f + rng.f(0.f, 0.3f);
            } else if (T.step == 1) {  // throw
                Vector3 f{0.1f, -0.3f, -0.95f}, pl{0, -1, 0.2f};
                Vector3 relL = xfPoint(p.rootInv, Vector3Add(c, {0, 0.12f, 0}));
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.28f, wristFor(relL, f, pl, PALM_CENTER, 1.f, false), f, pl, HandPose::Open, 0.02f, 1));
                k.push_back(restKey(p, 0, 0.9f));
                startTrack(p, 0, TK_Gesture, k);
                Vector3 hand = mPos(p.arm[0].hand);
                for (int i = 0; i < 2; ++i) {
                    T.diceFrom[i] = hand;
                    T.dicePos[i] = Vector3Add(c, {rng.f(-0.12f, 0.12f), 0.0078f, rng.f(-0.08f, 0.08f)});
                    T.diceRot[i] = {0, 0, 0};
                    T.diceSpin[i] = {rng.f(-900.f, 900.f), rng.f(-900.f, 900.f), rng.f(-900.f, 900.f)};
                }
                T.diceT = -0.22f;
                T.diceVisible = true;
                T.diceSound = rng.chance(0.4f);
                T.step = 2;
                T.timer = 1.0f;
            } else if (T.step == 2) {  // move a checker (once or twice)
                Vector3 spot = Vector3Add(c, {rng.f(-0.28f, 0.28f), 0, rng.f(-0.15f, 0.15f)});
                reachKeys(p, 0, spot, 0.5f, HandPose::Pinch, false);
                T.step = rng.chance(0.5f) ? 2 : 3;
                T.timer = 0.9f + rng.f(0.f, 0.3f);
            } else {
                T.turn = (T.turn + 1) % (int)T.patrons.size();
                T.step = 0;
                T.timer = rng.f(0.8f, 2.2f);
                if (rng.chance(0.3f)) P(T.turn + 1).talk = 1.f;
            }
            break;
        }
        case 1: {  // okey: draw from the centre, think, discard to the right corner
            Patron& p = P(T.turn);
            if (T.timer > 0.f) break;
            if (T.step == 0) {
                reachKeys(p, 0, Vector3Add(c, {rng.f(-0.03f, 0.03f), 0, rng.f(-0.03f, 0.03f)}), 0.45f, HandPose::Pinch, true);
                T.step = 1;
                T.timer = 1.1f + rng.f(0.5f, 2.5f);
            } else {
                Vector3 corner = xfPoint(p.root, {0.28f, BY, -(w3d::BG_CHAIR_DIST - 0.28f)});
                reachKeys(p, 0, corner, 0.4f, HandPose::Pinch, false);
                T.turn = (T.turn + 1) % (int)T.patrons.size();
                T.step = 0;
                T.timer = rng.f(1.0f, 2.2f);
                if (rng.chance(0.2f)) P(rng.i(4)).talk = 1.f;
            }
            break;
        }
        case 2: {  // cards: slap a card on the pile
            Patron& p = P(T.turn);
            if (T.timer > 0.f) break;
            Vector3 f{0.1f, -0.4f, -0.9f}, pl{0, -1, 0.3f};
            Vector3 fan = xfPoint(p.rootInv, mPos(p.arm[1].hand));
            Vector3 dst = xfPoint(p.rootInv, Vector3Add(c, {0, 0.03f, 0}));
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.35f, wristFor(Vector3Add(fan, {0.02f, 0.06f, -0.02f}), f, pl, PINCH_POINT, 1.f, false), f, pl, HandPose::Pinch, 0.02f));
            k.push_back(mkOrtho(0.62f, wristFor(Vector3Add(dst, {0, 0.07f, 0}), f, pl, PALM_CENTER, 1.f, false), f, pl, HandPose::Open, 0.05f, 1));
            k.push_back(mkOrtho(0.70f, wristFor(Vector3Add(dst, {0, 0.012f, 0}), f, pl, PALM_CENTER, 1.f, false), f, pl, HandPose::Open, 0.f, 2));
            k.push_back(restKey(p, 0, 1.25f));
            startTrack(p, 0, TK_Reach, k);
            float rot = rng.f(-40.f, 40.f) * DEG2RAD;
            T.pileCards.push_back(mul(RY(rot), chr::T(Vector3Add(c, {rng.f(-0.04f, 0.04f), 0.001f + 0.0008f * (float)T.pileCards.size(), rng.f(-0.04f, 0.04f)}))));
            if (T.pileCards.size() > 5) T.pileCards.erase(T.pileCards.begin());
            p.gazeGoal = c;
            p.gazeHold = 1.2f;
            T.turn = (T.turn + 1) % (int)T.patrons.size();
            T.timer = rng.f(1.4f, 3.2f);
            if (rng.chance(0.25f)) P(rng.i(3)).talk = 1.f;
            break;
        }
        default: {  // tea & chat
            T.talkTimer -= dt;
            if (T.talkTimer <= 0.f) {
                T.speaker = (T.speaker + 1) % (int)T.patrons.size();
                T.talkTimer = rng.f(2.5f, 6.f);
                for (size_t i = 0; i < T.patrons.size(); ++i) patrons[T.patrons[i]].talk = (int)i == T.speaker ? 1.f : 0.f;
                Patron& sp = P(T.speaker);
                Patron& other = P(T.speaker + 1);
                sp.gazeGoal = xfPoint(other.root, {0, 1.18f, -0.1f});
                sp.gazeHold = T.talkTimer;
                other.gazeGoal = xfPoint(sp.root, {0, 1.18f, -0.1f});
                other.gazeHold = T.talkTimer * 0.7f;
                // hand gesture while talking (the glass holder uses his free hand)
                int a = sp.prop == 2 ? 1 : 0;
                if (!sp.arm[a].track.on && !sp.reading) {
                    const bool left = a == 1;
                    const float sd = left ? -1.f : 1.f;
                    Vector3 f{sd * 0.2f, 0.35f, -0.9f}, pl{0, 1, 0.2f};
                    std::vector<Key> k{Key{}};
                    Vector3 base = wristFor({sd * 0.16f, 0.93f, -0.33f}, f, pl, PALM_CENTER, 1.f, left);
                    k.push_back(mkOrtho(0.5f, base, f, pl, HandPose::Open, 0.02f));
                    float tt = 0.5f;
                    int beats = 2 + rng.i(3);
                    for (int b = 0; b < beats; ++b) {
                        tt += rng.f(0.3f, 0.5f);
                        k.push_back(mkOrtho(tt, Vector3Add(base, {sd * rng.f(-0.03f, 0.05f), rng.f(-0.03f, 0.04f), rng.f(-0.03f, 0.02f)}), f, pl,
                                       b % 2 ? HandPose::Open : HandPose::Point));
                    }
                    k.push_back(restKey(sp, a, tt + 0.6f));
                    startTrack(sp, a, TK_Gesture, k);
                }
                // the listener sometimes sips his tea
                if (other.prop == 2 && !other.arm[0].track.on && rng.chance(0.5f)) {
                    const float th = 50.f * DEG2RAD;
                    Vector3 ax{0, std::cos(th), std::sin(th)};
                    Vector3 rim = Vector3Add(other.pm->face.mouth, {0, -0.012f, -0.012f});
                    Vector3 b = Vector3Subtract(rim, Vector3Scale(ax, M.tulipH));
                    Vector3 gp = Vector3Add(b, Vector3Scale(ax, 0.055f));
                    Vector3 f = vnorm({0.f, std::sin(th), -std::cos(th)});
                    Vector3 pl = Vector3CrossProduct(ax, f);
                    Key m1 = mkOrtho(0.9f, wristFor(gp, f, pl, GRIP_CENTER, 1.f, false), f, pl, HandPose::Grip, 0.f, 0);
                    m1.headRel = true;
                    m1.pole = {0.6f, -0.8f, 0.1f};
                    Key m2 = m1;
                    m2.t = 1.6f;
                    startTrack(other, 0, TK_Sip, {Key{}, m1, m2, restKey(other, 0, 2.4f)});
                }
            }
            // the newspaper reader
            for (int i : T.patrons) {
                Patron& p = patrons[i];
                if (p.prop == 2) continue;
                p.nextLook -= dt;
                if (p.nextLook > 0.f) continue;
                p.reading = !p.reading && rng.chance(0.6f);
                p.nextLook = p.reading ? rng.f(14.f, 30.f) : rng.f(10.f, 22.f);
                for (int a = 0; a < 2; ++a) {
                    const bool left = a == 1;
                    const float sd = left ? -1.f : 1.f;
                    Key k;
                    if (p.reading) {
                        Vector3 f{-sd * 0.15f, 0.9f, -0.3f}, pl{-sd * 0.2f, 0.f, 1.f};
                        k = mkOrtho(0.9f, wristFor({sd * 0.25f, 1.02f, -0.36f}, f, pl, PALM_CENTER, 1.f, left), f, pl, HandPose::Hold, 0.03f);
                    } else {
                        k = restKey(p, a, 0.9f);
                    }
                    startTrack(p, a, TK_Idle, {Key{}, k});
                }
            }
            break;
        }
        }
        // dice roll animation (the clatter as they land)
        if (T.diceVisible && T.diceT < 0.6f) {
            if (T.diceSound && T.diceT < 0.25f && T.diceT + dt >= 0.25f && !titleMode) sfx(ui::Sfx::Dice);
            T.diceT += dt;
            for (int i = 0; i < 2; ++i) {
                float u = clampf(T.diceT / 0.55f, 0.f, 1.f);
                T.diceRot[i] = Vector3Add(T.diceRot[i], Vector3Scale(T.diceSpin[i], dt * (1.f - u)));
            }
        }
        for (int i : T.patrons) updatePatron(patrons[i], T, dt);
    }
    updateCayci(dt);
}

// ============================================================================ the çaycı
// Where the çaycı stands to serve glass `gi`: by the okey table, or by the tavla table for the two playing there.
static int serveNode(int gi, int tavlaSeat) {
    if (tavlaSeat > 0 && gi == 0) return 9;
    if (tavlaSeat > 0 && gi == tavlaSeat) return 16;
    return kServeNode[gi];
}

void Cast::planTrip(bool ours, int bg, const std::vector<int>* onlyGlasses) {
    Cayci& b = boy;
    int from = nearestNode(b.pos);
    std::vector<int> nodes;
    b.tour.clear();
    if (ours) {
        // visit the four glasses around our table, entering from the right side (with tavla on: the tavla table's two
        // first, then the two still at the okey table)
        std::vector<int> order{2, 1, 0, 3};
        if (tavlaSeat > 0) {
            order = {tavlaSeat, 0};
            for (int gi : {2, 1, 3})
                if (gi != tavlaSeat) order.push_back(gi);
        }
        int cur = from;
        for (int gi : order) {
            if (onlyGlasses && std::find(onlyGlasses->begin(), onlyGlasses->end(), gi) == onlyGlasses->end()) continue;
            std::vector<int> seg = shortestPath(cur, serveNode(gi, tavlaSeat));
            if (!nodes.empty() && !seg.empty()) seg.erase(seg.begin());
            nodes.insert(nodes.end(), seg.begin(), seg.end());
            cur = serveNode(gi, tavlaSeat);
            b.tour.push_back(gi);
        }
        b.plan = 1;
    } else {
        nodes = shortestPath(from, kBgNode[bg]);
        b.plan = 2;
        b.bgTarget = bg;
    }
    b.path.clear();
    for (int n : nodes) b.path.push_back({kNodes[n].x + rng.f(-0.04f, 0.04f), 0.f, kNodes[n].z + rng.f(-0.04f, 0.04f)});
    b.pathI = 0;
    b.state = 1;
    b.serveIdx = 0;
    b.called = false;
}

// Every glass of our table served: on to the other tables of a round for everyone, or back to the counter.
void Cast::finishOurTour() {
    Cayci& b = boy;
    banter.teaServed();
    sfx(ui::Sfx::TeaClink);
    if (!b.roundBg.empty()) {  // the round goes on to the other tables
        const int next = b.roundBg.front();
        b.roundBg.erase(b.roundBg.begin());
        b.roundReply = true;
        planTrip(false, next);
        return;
    }
    cayciGoHome();
}

// The tavla table came or went (Characters::setTavlaTable) while he was on a round of our table: the glasses moved
// and his serve stops with them. The round is planned again from where he stands, for the glasses not yet poured;
// a glass in his hand goes back onto its saucer and what his arm was about to do to it is forgotten.
void Cast::replanOurTrip() {
    Cayci& b = boy;
    if (b.plan != 1 || (b.state != 1 && b.state != 2)) return;
    std::vector<int> left;
    for (int i = b.serveIdx; i < (int)b.tour.size(); ++i) {
        const bool poured = i == b.serveIdx && b.state == 2 && b.serveStep == 2 && b.serveT > 0.58f;
        if (!poured) left.push_back(b.tour[(size_t)i]);
    }
    for (TeaGlass& g : glass)
        if (g.holder == 4) setGlassHolder(g, -1);
    for (Arm& A : b.arm)
        for (Key& k : A.track.keys) k.event = 0;
    if (left.empty()) {
        finishOurTour();
        return;
    }
    planTrip(true, -1, &left);
}

// One frame of the çaycı: what his serving and his body share.
struct CayciFrame {
    bool atStop = false;          // standing at a glass or a table, serving
    bool hasFaceTarget = false;
    Vector3 faceTarget{};         // what he turns to while standing
    Vector3 fwd{};                // his heading
    Matrix root = MatrixIdentity(), rootInv = MatrixIdentity();
    float walk = 0.f, ph = 0.f;   // 0..1 how much he walks; the gait phase (radians)
    Vector3 hip{};
    Vector3 trayHold{};           // the tray hand (character-local)
};

namespace {

// A keyframed move of one of his arms, starting from where the hand is now.
void startBoyTrack(Arm& A, std::vector<Key> keys) {
    keys[0] = A.cur;
    keys[0].t = 0.f;
    A.track.keys = std::move(keys);
    A.track.t = 0.f;
    A.track.on = true;
    A.track.nextKey = 1;
}

} // namespace

// Back to the counter along the graph.
void Cast::cayciGoHome() {
    Cayci& b = boy;
    std::vector<int> nodes = shortestPath(nearestNode(b.pos), 0);
    b.path.clear();
    for (int n : nodes) b.path.push_back({kNodes[n].x, 0.f, kNodes[n].z});
    b.pathI = 1;
    b.plan = 0;
    b.state = 1;
}

void Cast::updateCayci(float dt) {
    CayciFrame F;
    cayciDecide(dt, F);
    cayciWalk(dt, F);
    cayciServe(dt, F);
    cayciBody(dt, F);
    cayciFace(dt);
}

// ---- the brain: when to set out (our table, a round for everyone, a background table), the replies from the counter
void Cast::cayciDecide(float dt, CayciFrame& F) {
    Cayci& b = boy;
    b.nextOurs -= dt;
    b.nextBg -= dt;
    b.timer -= dt;
    if (b.state == 0) {  // at the counter
        F.faceTarget = Vector3Add(b.pos, {0, 0, -1.f});
        F.hasFaceTarget = true;
        // (Ocakçı) every trip starts from the ocakçı's hands: ocakTrayGate() holds it until he filled the tray
        if (b.roundQueued && b.timer <= 0.f && ocakTrayGate()) {  // "Çaylar benden!": our table, then everybody
            planTrip(true, -1);
            b.roundQueued = false;
            b.roundBg.clear();
            for (int t = 0; t < (int)bgTables.size(); ++t)
                if (!bgTables[(size_t)t].patrons.empty() && patrons[bgTables[(size_t)t].patrons[0]].present) b.roundBg.push_back(t);
            b.nextOurs = rng.f(70.f, 100.f);
        } else if ((b.nextOurs <= 0.f || (b.called && b.nextOurs < 40.f)) && b.timer <= 0.f && ocakTrayGate()) {
            planTrip(true, -1);
            b.nextOurs = rng.f(60.f, 90.f);
        } else if (b.nextBg <= 0.f && b.timer <= 0.f && ocakTrayGate()) {
            planTrip(false, pickBgTable());
            b.nextBg = rng.f(26.f, 48.f);
        }
    }
    if (b.called && b.state == 0 && b.nextOurs > 4.f) b.nextOurs = rng.f(2.f, 4.f);
    ocakBoyAtCounter(dt);  // (Ocakçı) he steps over to the counter's corner for the tray
    // shortly before a scheduled round somebody at our table calls for tea (so the visit feels asked for)
    if (b.state == 0 && !b.called && !titleMode && b.nextOurs <= 5.f && b.nextOurs + dt > 5.f && rng.chance(0.65f))
        banter.orderTea();
    if (b.replyIn >= 0.f) {
        b.replyIn -= dt;
        if (b.replyIn < 0.f && !titleMode) {
            static const char* const kReplies[] = {"Geliyor abi!", "Hemen, hemen!", "Geldi geliyor!", "Demleniyor, geliyorum!"};
            pushLine(4, kReplies[rng.i(4)], 1.8f, 2.5f);
        }
    }
}

// ---- along the path, stopping at each glass of the tour; then where his body stands this frame
void Cast::cayciWalk(float dt, CayciFrame& F) {
    Cayci& b = boy;
    float targetSpeed = 0.f;
    if (b.state == 1) {
        if (b.pathI < b.path.size()) {
            Vector3 tgt = b.path[b.pathI];
            Vector3 d = Vector3Subtract(tgt, b.pos);
            d.y = 0.f;
            float dist = Vector3Length(d);
            bool last = b.pathI + 1 == b.path.size();
            // arriving at a serve stop of the tour?
            bool isStop = false;
            if (b.plan == 1 && b.serveIdx < (int)b.tour.size()) {
                const int n = serveNode(b.tour[b.serveIdx], tavlaSeat);
                const Vector3 s{kNodes[n].x, 0.f, kNodes[n].z};
                isStop = std::hypot(s.x - tgt.x, s.z - tgt.z) < 0.1f;
            }
            float slow = (last || isStop) ? clampf(dist / 0.55f, 0.25f, 1.f) : 1.f;
            targetSpeed = 1.08f * slow;
            if (dist < (last || isStop ? 0.05f : 0.22f)) {
                if (isStop) {
                    b.state = 2;
                    b.serveStep = 0;
                    b.serveT = 0.f;
                    ++b.pathI;
                } else {
                    ++b.pathI;
                    if (b.pathI >= b.path.size()) {
                        if (b.plan == 2) {
                            b.state = 3;
                            b.serveT = 0.f;
                            b.serveStep = 0;
                        } else {
                            b.state = 0;
                            b.plan = 0;
                            b.timer = rng.f(3.f, 6.f);
                        }
                    }
                }
            } else {
                Vector3 dir = Vector3Scale(d, 1.f / dist);
                float want = std::atan2(-dir.x, -dir.z);
                float diff = wrapAngle(want - b.yaw);
                b.yaw = wrapAngle(b.yaw + clampf(diff, -5.f * dt, 5.f * dt));
                if (std::fabs(diff) > 1.2f) targetSpeed *= 0.35f;
            }
        } else {
            b.state = 0;
        }
    }
    b.speed = approachExp(b.speed, targetSpeed, 5.f, dt);
    F.fwd = {-std::sin(b.yaw), 0, -std::cos(b.yaw)};
    b.pos = Vector3Add(b.pos, Vector3Scale(F.fwd, b.speed * dt));
    b.phase += b.speed * dt / 1.35f;
    b.phase -= std::floor(b.phase);

    const float yawDeg = b.yaw * RAD2DEG;
    F.root = trsYaw(b.pos, yawDeg, 1.f);
    F.rootInv = MatrixInvert(F.root);
    F.ph = b.phase * 2.f * PI_F;
    F.walk = clampf(b.speed / 0.9f, 0.f, 1.f);
    const float bob = -0.012f * std::cos(2.f * F.ph) * F.walk;
    F.hip = Vector3Add(b.L.hipPivot, {0, bob - 0.01f * F.walk, 0});
}

// ---- the serving choreography: a glass of our table, a glass set down at a background table, idling at the counter
void Cast::cayciServe(float dt, CayciFrame& F) {
    Cayci& b = boy;
    const float hs = b.L.handScale;
    // the askılı tepsi hangs from a nearly straight arm, the tray swinging around knee height
    F.trayHold = F.walk > 0.05f ? Vector3{-0.30f, 0.80f, -0.03f} : Vector3{-0.30f, 0.82f, -0.05f};
    if (b.state == 2) {
        F.atStop = true;
        int gi = b.tour[b.serveIdx];
        TeaGlass& g = glass[gi];
        F.faceTarget = g.rest;
        F.hasFaceTarget = true;
        F.trayHold = {-0.30f, 0.86f, -0.10f};
        b.serveT += dt;
        // on to the next glass of the round (or back to the counter when all are done)
        auto nextStop = [&]() {
            b.serveStep = 3;
            b.serveT = 0.f;
            ++b.serveIdx;
            if (b.serveIdx >= (int)b.tour.size()) {
                finishOurTour();
            } else {
                b.state = 1;
            }
        };
        Vector3 gl = xfPoint(F.rootInv, g.rest);
        Vector3 f0, p0;
        {
            Vector3 fl = vnorm({gl.x + 0.1f, 0, gl.z});
            Vector3 a{0, 1, 0};
            fl = vnorm(Vector3Subtract(fl, Vector3Scale(a, Vector3DotProduct(fl, a))));
            f0 = fl;
            p0 = Vector3CrossProduct(a, fl);
        }
        Vector3 gripP = Vector3Add(gl, {0, 0.055f, 0});
        Vector3 wGrip = wristFor(gripP, f0, p0, GRIP_CENTER, hs, false);
        Vector3 trayTop = xfPoint(F.rootInv, xfPoint(b.trayW, {0, -0.30f + 0.02f, 0}));
        Vector3 wTray = wristFor(Vector3Add(trayTop, {0.03f, 0.055f + 0.01f, 0.02f}), f0, p0, GRIP_CENTER, hs, false);
        switch (b.serveStep) {
        case 0:
            if (g.holder >= 0 && b.serveT > 4.f) {  // he's still drinking it: leave him be
                nextStop();
            } else if (b.serveT > 0.35f && g.holder < 0) {
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.5f, Vector3Add(wGrip, {0.02f, 0.04f, 0.03f}), f0, p0, HandPose::Hold, 0.03f));
                k.push_back(mkOrtho(0.7f, wGrip, f0, p0, HandPose::Grip, 0.f, 0, KE_GrabGlass));
                startBoyTrack(b.arm[0], k);
                b.serveStep = 1;
                b.serveT = 0.f;
                if (b.serveIdx == 0 && !titleMode && rng.chance(0.6f)) {
                    bool nuri = std::find(b.tour.begin(), b.tour.end(), 3) != b.tour.end() && rng.chance(0.3f);
                    pushLine(4, nuri ? kBoyNuri : kBoyLines[rng.i(5)], 2.6f, 3.f);
                }
            }
            break;
        case 1:
            if (b.serveT > 0.75f && g.holder != 4) {  // somebody picked it up first (the player): on to the next one
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.45f, Vector3Add(wGrip, {0.03f, 0.05f, 0.04f}), f0, p0, HandPose::Rest, 0.f, 1));
                startBoyTrack(b.arm[0], k);
                nextStop();
            } else if (b.serveT > 0.75f) {
                std::vector<Key> k{Key{}};
                k.push_back(mkOrtho(0.5f, wTray, f0, p0, HandPose::Grip, 0.06f));
                k.push_back(mkOrtho(0.65f, wTray, f0, p0, HandPose::Grip));
                k.push_back(mkOrtho(1.15f, wGrip, f0, p0, HandPose::Grip, 0.06f, 0, KE_ReleaseGlass));
                k.push_back(mkOrtho(1.4f, Vector3Add(wGrip, {0.03f, 0.05f, 0.04f}), f0, p0, HandPose::Rest, 0.f, 1));
                startBoyTrack(b.arm[0], k);
                b.serveStep = 2;
                b.serveT = 0.f;
            }
            break;
        case 2:
            if (b.serveT > 0.58f && b.serveT - dt <= 0.58f) {
                g.level = gi == 3 ? 0.9f : rng.f(0.86f, 0.95f);  // the fresh glass from the tray
                g.steamAcc = 0.7f;
                teaServed.push_back(gi);
                if (teaServed.size() > 8) teaServed.pop_front();
                ocakTrayServed();  // (Ocakçı) a glass off the tray
            }
            if (b.serveT > 1.5f) nextStop();
            break;
        default: break;
        }
        if (gi == 0 && b.serveStep >= 1) b.gaze = viewerPos();  // eye contact while serving the human
    }
    if (b.state == 3) {  // at a background table: set a glass down
        F.atStop = true;
        const BgTable& T = bgTables[(size_t)std::clamp(b.bgTarget, 0, 3)];
        F.faceTarget = T.c;
        F.hasFaceTarget = true;
        F.trayHold = {-0.30f, 0.86f, -0.10f};
        b.serveT += dt;
        if (b.serveStep == 0 && b.serveT > 0.4f) {
            Vector3 tl = xfPoint(F.rootInv, Vector3Add(T.c, {0, BY, 0}));
            Vector3 dst = Vector3Add(Vector3Scale(vnorm({tl.x, 0, tl.z}), 0.55f), {0, BY + 0.03f, 0});
            Vector3 f = vnorm({dst.x, -0.4f, dst.z}), pl{0, -1, 0};
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.55f, wristFor(dst, f, pl, PALM_CENTER, hs, false), f, pl, HandPose::Grip, 0.05f, 0, 0));
            Key back;
            back = mkOrtho(1.2f, wristFor({0.22f, 0.86f, -0.05f}, {0, -1, -0.1f}, {-1, 0, 0}, PALM_CENTER, hs, false), {0, -1, -0.1f},
                      {-1, 0, 0}, HandPose::Rest);
            k.push_back(back);
            startBoyTrack(b.arm[0], k);
            b.serveStep = 1;
            sfx(ui::Sfx::GlassSet);
            ocakTrayServed();  // (Ocakçı) a glass off the tray
            // a round on somebody: the men at this table thank him
            if (!titleMode && b.roundReply && crowdLineCd <= 0.f && !T.patrons.empty()) {
                static const char* const kThanks[] = {"Sağ ol!", "Eksik olma!", "Sağlığına!", "Bereket versin!", "Eline sağlık!"};
                const Patron& tp = patrons[T.patrons[(size_t)rng.i((int)T.patrons.size())]];
                crowdLines.push_back({kThanks[rng.i(5)], xfPoint(tp.headW, {0, 0.12f, 0})});
                crowdLineCd = 1.5f;
            }
        }
        if (b.serveT > 1.9f) {
            if (!b.roundBg.empty()) {  // on to the next table of the round
                const int next = b.roundBg.front();
                b.roundBg.erase(b.roundBg.begin());
                planTrip(false, next);
            } else {
                b.roundReply = false;
                cayciGoHome();
            }
        }
    }
    if (b.state == 0) {
        // idle at the counter: now and then pour/arrange a glass
        b.counterIdleT -= dt;
        if (b.counterIdleT <= 0.f && !b.arm[0].track.on) {
            b.counterIdleT = rng.f(3.f, 7.f);
            Vector3 f{-0.2f, -0.3f, -0.93f}, pl{0, -1, 0};
            Vector3 w = wristFor({0.12f, 1.02f, -0.34f}, f, pl, PALM_CENTER, hs, false);
            std::vector<Key> k{Key{}};
            k.push_back(mkOrtho(0.6f, w, f, pl, HandPose::Hold, 0.02f));
            k.push_back(mkOrtho(1.4f, Vector3Add(w, {-0.06f, 0.01f, 0}), f, pl, HandPose::Grip));
            k.push_back(mkOrtho(2.1f, wristFor({0.22f, 0.86f, -0.05f}, {0, -1, -0.1f}, {-1, 0, 0}, PALM_CENTER, hs, false), {0, -1, -0.1f},
                           {-1, 0, 0}, HandPose::Rest));
            startBoyTrack(b.arm[0], k);
        }
    }
    // face the serve target while standing
    if (F.hasFaceTarget && (F.atStop || b.state == 0)) {
        Vector3 d = Vector3Subtract(F.faceTarget, b.pos);
        float want = std::atan2(-d.x, -d.z);
        float diff = wrapAngle(want - b.yaw);
        b.yaw = wrapAngle(b.yaw + clampf(diff, -3.5f * dt, 3.5f * dt));
    }
}

// ---- the body: torso, head, legs, arms (the events of their tracks), the tray's swing
void Cast::cayciBody(float dt, CayciFrame& F) {
    Cayci& b = boy;
    const PersonLook& L = b.L;
    const float hs = L.handScale;
    Arm& R = b.arm[0];
    Arm& Lh = b.arm[1];
    // ---- torso / head
    float leanGoal = 0.05f * F.walk + (F.atStop ? 0.16f : 0.f);
    spring(b.lean, b.leanV, leanGoal, 5.f, dt);
    float twist = 0.06f * std::cos(F.ph) * F.walk;
    Matrix tl = mul(RY(twist), RX(-b.lean));
    Matrix torsoLocal = mul(tl, T(F.hip));
    b.torsoW = mul(torsoLocal, F.root);
    // gaze: ahead while walking, at the glass / table while serving
    Vector3 gz;
    if (b.state == 1) gz = Vector3Add(b.pos, Vector3Add(Vector3Scale(F.fwd, 2.2f), {0, 1.2f, 0}));
    else if (F.atStop) gz = Vector3Add(F.faceTarget, {0, 0.05f, 0});
    else gz = Vector3Add(b.pos, {0, 1.05f, -0.5f});
    if (b.state == 2 && b.tour[std::min(b.serveIdx, (int)b.tour.size() - 1)] == 0 && b.serveStep >= 1) gz = viewerPos();
    b.gaze = approachExp(b.gaze, gz, 6.f, dt);
    headLookSpring(b.torsoW, L, b.gaze, 0.7f, -0.7f, 6.f, dt, b.hYaw, b.hYawV, b.hPitch, b.hPitchV);
    Matrix headT = mul(mul(RX(b.hPitch + 0.02f * std::sin(F.ph * 2.f) * F.walk), RY(b.hYaw)), T({0, L.spineLen, L.headZ}));
    b.headW = mul(headT, b.torsoW);

    // ---- legs
    walkLegs(F.root, F.hip, b.phase, F.walk, b.thighW, b.shinW);

    // ---- arms
    // right (free) arm swings while walking unless a track runs
    for (int a = 0; a < 2; ++a) {
        Arm& A = b.arm[a];
        if (A.track.on) {
            A.track.t += dt;
            while (A.track.nextKey < (int)A.track.keys.size() && A.track.keys[A.track.nextKey].t <= A.track.t) {
                int ev = A.track.keys[A.track.nextKey].event;
                if (ev == KE_GrabGlass && b.state == 2 && glass[b.tour[b.serveIdx]].holder < 0) {
                    TeaGlass& g = glass[b.tour[b.serveIdx]];
                    setGlassHolder(g, 4, a);
                    // right-hand grip
                    const float inv = 1.f / hs;
                    Vector3 ay{-1, 0, 0}, ax{0, 1, 0}, az{0, 0, 1};
                    Vector3 t0 = Vector3Subtract(GRIP_CENTER, Vector3Scale(ay, 0.055f * inv));
                    g.inHand = basisMatrix(Vector3Scale(ax, inv), Vector3Scale(ay, inv), Vector3Scale(az, inv), t0);
                } else if (ev == KE_ReleaseGlass && b.state == 2 && glass[b.tour[b.serveIdx]].holder == 4) {
                    setGlassHolder(glass[b.tour[b.serveIdx]], -1);
                    sfx(ui::Sfx::GlassSet);
                }
                ++A.track.nextKey;
            }
            if (A.track.t >= A.track.duration()) {
                A.track.on = false;
                A.hold = A.track.keys.back();
            }
        }
    }
    const float sw = -0.32f * std::cos(F.ph) * F.walk;
    Key rk;
    if (R.track.on) {
        evalTrack(R.track, R.track.t, rk);
    } else if (F.walk > 0.05f || b.state == 1) {
        Vector3 pl{-1, 0, 0};
        Vector3 hand{0.215f, 1.40f - 0.57f * std::cos(sw), -0.57f * std::sin(sw) + 0.02f};
        rk = mkOrtho(0, hand, {0, -std::cos(sw), -std::sin(sw)}, pl, HandPose::Rest);
        R.hold = rk;
    } else {
        rk = R.hold;
    }
    R.cur = rk;
    Key lk = mkOrtho(0, F.trayHold, {0.25f, -0.25f, -0.93f}, {0.9f, -0.35f, 0.25f}, HandPose::Grip);
    lk.pos = wristFor(Vector3Add(F.trayHold, {0, 0.01f, 0}), lk.fingers, lk.palm, GRIP_CENTER, hs, true);
    ocakBoyTrayHand(lk, F.rootInv, hs);  // (Ocakçı) reaching for the tray / the hand free while the ocakçı has it
    Lh.cur = lk;
    Lh.cur.pos = approachExp(Lh.localWrist, lk.pos, 3.f, dt);
    Lh.localWrist = Lh.cur.pos;
    Key keys[2] = {R.cur, Lh.cur};
    for (int a = 0; a < 2; ++a) {
        Arm& A = b.arm[a];
        const float sd = a == 0 ? 1.f : -1.f;
        Key k = keys[a];
        A.localWrist = k.pos;
        A.shoulder = xfPoint(b.torsoW, {sd * L.shoulderW, L.shoulderY, 0.012f});
        Vector3 w = xfPoint(F.root, k.pos);
        // the tray arm hangs with its elbow tucked back; the free arm bends outward a little
        Vector3 pole = xfDir(F.root, a == 1 ? vnorm({-0.25f, -0.25f, 1.f}) : vnorm({sd * 0.45f, -0.7f, 0.4f}));
        A.elbow = solveTwoBone(A.shoulder, w, L.upperArm, L.foreArm, pole);
        A.wrist = w;
        Vector3 back = xfDir(F.root, {0, 0, 1});
        A.upper = boneMatrix(A.shoulder, A.elbow, back);
        A.fore = boneMatrix(A.elbow, A.wrist, back);
        A.hand = handMatrix(A.wrist, xfDir(F.root, k.fingers), xfDir(F.root, k.palm), hs);
        A.pose = k.pose;
    }

    // ---- the askılı tepsi: a pendulum hanging from the left hand's grip. A çaycı steadies it, so it answers
    // only to the smoothed motion of his hand, swings at most ~16° (the tea stays in the glasses, and it
    // doesn't look like spilling), settles quickly when he stops, and its rim never swings into his thigh
    Vector3 pivot = xfPoint(Lh.hand, mirrorL(GRIP_CENTER, true));
    const float Lr = 0.30f;
    if (!b.trayInit) {
        b.trayPivotF = pivot;
        b.trayP = b.trayPrev = Vector3Add(pivot, {0, -Lr, 0});
        b.trayInit = true;
    }
    {  // Verlet at a fixed step (a varying h would bend the velocity it keeps)
        constexpr float h = 1.f / 240.f;
        const float damp = std::exp(-(F.walk > 0.05f ? 8.f : 12.f) * h);
        b.trayAcc = std::min(b.trayAcc + dt, 0.05f);
        for (; b.trayAcc >= h - 1e-6f; b.trayAcc -= h) {
            b.trayPivotF = approachExp(b.trayPivotF, pivot, 10.f, h);
            const Vector3 v = Vector3Scale(Vector3Subtract(b.trayP, b.trayPrev), damp);
            b.trayPrev = b.trayP;
            b.trayP = Vector3Add(Vector3Add(b.trayP, v), {0, -9.81f * h * h, 0});
            Vector3 d = vnorm(Vector3Subtract(b.trayP, b.trayPivotF));
            const float maxSwing = 16.f * DEG2RAD;
            if (-d.y < std::cos(maxSwing)) {
                const Vector3 side = vnorm({d.x, 0.f, d.z});
                d = Vector3Add(Vector3Scale(side, std::sin(maxSwing)), {0.f, -std::cos(maxSwing), 0.f});
            }
            b.trayP = Vector3Add(b.trayPivotF, Vector3Scale(d, Lr));
        }
        b.trayAcc = std::max(b.trayAcc, 0.f);
    }
    Vector3 down = vnorm(Vector3Subtract(b.trayP, b.trayPivotF));
    {
        // keep the tray (radius 0.14) clear of the left thigh (axis 0.088 off the middle, radius ~0.07)
        Vector3 cl = xfPoint(F.rootInv, Vector3Add(pivot, Vector3Scale(down, Lr)));
        const float xMax = -(0.088f + 0.07f + 0.14f + 0.012f);
        if (cl.x > xMax) {
            cl.x = xMax;
            down = vnorm(Vector3Subtract(xfPoint(F.root, cl), pivot));
        }
    }
    const Vector3 up = Vector3Negate(down);
    Vector3 bk = xfDir(F.root, {0, 0, 1});
    b.trayW = boneMatrix(pivot, Vector3Add(pivot, up), bk);
}

// ---- the face: blinks, eyes, lips following his voice
void Cast::cayciFace(float dt) {
    Cayci& b = boy;
    // ---- face
    const float lidClose = blinkStep(rng, b.blinkIn, b.blinkT, dt, 5.f, 0.15f);
    const FaceGeo& fg = b.pm->face;
    standingEyes(fg, b.headW, b.gaze, -0.40f, lidClose, -0.05f, dt, b.eYaw, b.ePitch, b.eyeW, b.lidW, b.browW);
    float jawGoal = 0.f;
    if (b.talkT >= 0.f) {
        b.talkT += dt;
        // (Yüz) his lips follow his voice, or the text's syllables when there is none (CharactersFace.cpp)
        jawGoal = std::max(0.f, talkOpen(b.tm, 4, b.talkT, dt)) * 0.85f;
        if (b.talkT > b.talkDur) b.talkT = -1.f;
    }
    b.jaw = approachExp(b.jaw, clampf(jawGoal, 0.f, 1.f), 26.f, dt);
    standingMouth(fg, b.headW, b.jaw, b.mouthW, b.lipW);
}


} // namespace chr
} // namespace r3d
