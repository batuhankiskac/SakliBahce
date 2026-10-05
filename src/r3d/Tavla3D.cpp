// The tavla board in 3D (see Tavla3D.h).
#include "r3d/Tavla3D.h"

#include "r3d/World.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>

namespace r3d {

namespace {

constexpr float Y0 = w3d::TABLE_Y;
constexpr float FRAME_X = 0.345f, FRAME_Z = 0.265f, FRAME_H = 0.024f;
constexpr float FIELD_Y = Y0 + 0.0105f;         // the playing surface (recessed in the box)
constexpr float HALF_IN = 0.022f, HALF_OUT = 0.31f; // each half spans |x| in [HALF_IN, HALF_OUT]
constexpr float EDGE_Z = 0.236f;                 // the points' bases
constexpr float PW = (HALF_OUT - HALF_IN) / 6.f; // point width
constexpr float PL = 0.19f;                      // point length (base to tip)
constexpr float CR = 0.021f, CH = 0.0082f;       // checker radius / height
constexpr float DIE = 0.02f;

float easeInOut(float t) { return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f; }

float pointX(int i) {
    if (i < 6) return HALF_OUT - PW * ((float)i + 0.5f);
    if (i < 12) return -HALF_IN - PW * ((float)(i - 6) + 0.5f);
    if (i < 18) return -HALF_OUT + PW * ((float)(i - 12) + 0.5f);
    return HALF_IN + PW * ((float)(i - 18) + 0.5f);
}
bool bottomRow(int i) { return i < 12; }

Texture2D makeDieTexture() {
    const int cell = 128;
    RenderTexture2D rt = LoadRenderTexture(cell * 6, cell);
    BeginTextureMode(rt);
    ClearBackground(Color{244, 238, 222, 255});
    for (int v = 1; v <= 6; ++v) {
        const float cx = (float)(v - 1) * cell + cell * 0.5f, cy = cell * 0.5f;
        DrawRectangleLinesEx({(float)(v - 1) * cell + 1.f, 1.f, cell - 2.f, cell - 2.f}, 3.f, Color{214, 204, 184, 255});
        const float o = cell * 0.27f, pr = cell * 0.085f;
        const Color pc = v == 1 ? Color{170, 30, 34, 255} : Color{34, 26, 22, 255};
        auto pip = [&](float x, float y) { DrawCircleV({cx + x, cy + y}, v == 1 ? pr * 1.5f : pr, pc); };
        if (v % 2 == 1) pip(0, 0);
        if (v >= 2) { pip(-o, -o); pip(o, o); }
        if (v >= 4) { pip(o, -o); pip(-o, o); }
        if (v == 6) { pip(-o, 0); pip(o, 0); }
    }
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    UnloadRenderTexture(rt);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

Texture2D makeGlowTexture() {
    const int n = 64;
    Image img = GenImageColor(n, n, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = ((float)x + 0.5f) / (float)n, v = ((float)y + 0.5f) / (float)n;
            const float a = std::sin(u * PI) * (0.35f + 0.65f * v); // brighter toward the base
            px[y * n + x] = Color{255, 255, 255, (unsigned char)std::clamp(a * 255.f, 0.f, 255.f)};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

// A die: a box with each face mapped to its cell of the 6-cell die texture (+Y 1, -Y 6, +X 2, -X 5, +Z 3, -Z 4).
Mesh makeDie() {
    MeshBuilder mb;
    const float h = DIE * 0.5f;
    struct F {
        Vector3 n, u, v;
        int value;
    };
    const F faces[6] = {{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, 1},  {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}, 6},
                        {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}, 2}, {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}, 5},
                        {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}, 3},  {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}, 4}};
    for (const F& f : faces) {
        const float u0 = (float)(f.value - 1) / 6.f, u1 = (float)f.value / 6.f;
        auto P = [&](float a, float b) {
            return Vector3Add(Vector3Scale(f.n, h), Vector3Add(Vector3Scale(f.u, a * h), Vector3Scale(f.v, b * h)));
        };
        const int a = mb.vertex(P(-1, -1), f.n, {u0, 0.f});
        const int b = mb.vertex(P(1, -1), f.n, {u1, 0.f});
        const int c = mb.vertex(P(1, 1), f.n, {u1, 1.f});
        const int d = mb.vertex(P(-1, 1), f.n, {u0, 1.f});
        // wind counter-clockwise seen from outside
        const Vector3 cr = Vector3CrossProduct(Vector3Subtract(P(1, -1), P(-1, -1)), Vector3Subtract(P(-1, 1), P(-1, -1)));
        if (Vector3DotProduct(cr, f.n) > 0.f) mb.quad(a, b, c, d);
        else mb.quad(a, d, c, b);
    }
    return mb.build();
}

} // namespace

bool Tavla3D::init(Renderer& r) {
    if (ready_) return true;
    woodTex_ = genWoodTexture(512, Color{150, 96, 54, 255}, Color{92, 52, 26, 255}, 0x7A71Au, 22.f);
    // the box: base plate, rim walls, the middle bar
    {
        MeshBuilder mb;
        mb.roundedBox({0, Y0 + 0.004f, 0}, {FRAME_X * 2.f, 0.008f, FRAME_Z * 2.f}, 0.003f, 2);
        const float wallT = FRAME_X - HALF_OUT;
        mb.roundedBox({FRAME_X - wallT * 0.5f, Y0 + FRAME_H * 0.5f, 0}, {wallT, FRAME_H, FRAME_Z * 2.f}, 0.004f, 2);
        mb.roundedBox({-FRAME_X + wallT * 0.5f, Y0 + FRAME_H * 0.5f, 0}, {wallT, FRAME_H, FRAME_Z * 2.f}, 0.004f, 2);
        const float wallZ = FRAME_Z - EDGE_Z;
        mb.roundedBox({0, Y0 + FRAME_H * 0.5f, FRAME_Z - wallZ * 0.5f}, {FRAME_X * 2.f, FRAME_H, wallZ}, 0.004f, 2);
        mb.roundedBox({0, Y0 + FRAME_H * 0.5f, -FRAME_Z + wallZ * 0.5f}, {FRAME_X * 2.f, FRAME_H, wallZ}, 0.004f, 2);
        mb.roundedBox({0, Y0 + FRAME_H * 0.5f, 0}, {HALF_IN * 2.f, FRAME_H, EDGE_Z * 2.f}, 0.003f, 2);
        boardMesh_ = mb.build();
        matWood_ = r.makeMat(WHITE, woodTex_, 0.35f, 40.f);
    }
    {
        MeshBuilder mb;
        for (int side = -1; side <= 1; side += 2) {
            const float cx = (float)side * (HALF_IN + HALF_OUT) * 0.5f;
            mb.plane({cx, FIELD_Y, 0}, {HALF_OUT - HALF_IN, EDGE_Z * 2.f}, {0, 1, 0});
        }
        fieldMesh_ = mb.build();
        matField_ = r.makeMat(Color{226, 206, 160, 255}, Texture2D{}, 0.08f, 12.f);
    }
    for (int col = 0; col < 2; ++col) {
        MeshBuilder mb;
        for (int i = 0; i < 24; ++i) {
            if (i % 2 != col) continue;
            const float x = pointX(i);
            const float zb = bottomRow(i) ? EDGE_Z : -EDGE_Z;
            const float zt = bottomRow(i) ? EDGE_Z - PL : -EDGE_Z + PL;
            const float y = FIELD_Y + 0.0004f;
            const int a = mb.vertex({x - PW * 0.46f, y, zb}, {0, 1, 0}, {0, 0});
            const int b = mb.vertex({x + PW * 0.46f, y, zb}, {0, 1, 0}, {1, 0});
            const int c = mb.vertex({x, y, zt}, {0, 1, 0}, {0.5f, 1});
            if (bottomRow(i)) mb.triangle(a, b, c);
            else mb.triangle(a, c, b);
        }
        pointMesh_[col] = mb.build();
        matPoint_[col] = r.makeMat(col == 0 ? Color{120, 34, 30, 255} : Color{36, 50, 44, 255}, Texture2D{}, 0.12f, 16.f);
    }
    {
        // checker: a disc with a bevelled edge and a ring on top
        const std::vector<Vector2> prof = {{0.f, 0.f},          {CR * 0.92f, 0.f}, {CR, CH * 0.18f}, {CR, CH * 0.82f},
                                           {CR * 0.92f, CH},     {CR * 0.74f, CH},  {CR * 0.70f, CH * 0.88f},
                                           {CR * 0.62f, CH * 0.88f}, {CR * 0.58f, CH}, {0.f, CH}};
        checkerMesh_ = genLathe(prof, 28, false, false);
        matChecker_[0] = r.makeMat(Color{236, 226, 204, 255}, Texture2D{}, 0.45f, 60.f);
        matChecker_[1] = r.makeMat(Color{92, 28, 24, 255}, Texture2D{}, 0.45f, 60.f);
        matCheckerHi_ = r.makeMat(Color{255, 210, 105, 255}, Texture2D{}, 0.5f, 60.f, 0.35f);  // playable: warm gold
        matCheckerSel_ = r.makeMat(Color{255, 176, 40, 255}, Texture2D{}, 0.5f, 60.f, 0.6f);   // selected
    }
    dieTex_ = makeDieTexture();
    dieMesh_ = makeDie();
    matDie_ = r.makeMat(WHITE, dieTex_, 0.4f, 50.f);
    {
        MeshBuilder mb;
        // a unit point-shaped glow: base along x at z = 0, tip at z = -1 (scaled per point)
        const int a = mb.vertex({-0.5f, 0, 0}, {0, 1, 0}, {0, 1});
        const int b = mb.vertex({0.5f, 0, 0}, {0, 1, 0}, {1, 1});
        const int c = mb.vertex({0.f, 0, -1.f}, {0, 1, 0}, {0.5f, 0});
        mb.triangle(a, b, c);
        glowMesh_ = mb.build();
        glowTex_ = makeGlowTexture();
        // alpha-blended washes (an additive glow vanishes on the cream field)
        matGlow_ = r.makeMat(Color{255, 196, 40, 120}, glowTex_, 0.f, 4.f, 1.f);
        matGlowSel_ = r.makeMat(Color{255, 186, 30, 225}, glowTex_, 0.f, 4.f, 1.f);
        matGlowTarget_ = r.makeMat(Color{40, 200, 80, 200}, glowTex_, 0.f, 4.f, 1.f);
    }
    for (int i = 0; i < CHECKERS; ++i) {
        chk_[i].player = i < 15 ? 0 : 1;
        chk_[i].where = OFF;
        chk_[i].slot = i % 15;
        chk_[i].cur = chk_[i].to = chk_[i].from = slotWorld(OFF, chk_[i].player, chk_[i].slot);
    }
    ready_ = true;
    return true;
}

void Tavla3D::shutdown(Renderer& r) {
    if (!ready_) return;
    for (Mesh* m : {&boardMesh_, &fieldMesh_, &pointMesh_[0], &pointMesh_[1], &checkerMesh_, &dieMesh_, &glowMesh_}) UnloadMesh(*m);
    for (Mat* m : {&matWood_, &matField_, &matPoint_[0], &matPoint_[1], &matChecker_[0], &matChecker_[1], &matCheckerHi_, &matCheckerSel_,
                   &matDie_, &matGlow_, &matGlowSel_, &matGlowTarget_})
        r.unloadMat(*m);
    for (Texture2D* t : {&woodTex_, &dieTex_, &glowTex_})
        if (t->id) UnloadTexture(*t);
    ready_ = false;
}

Vector3 Tavla3D::slotWorld(int where, int player, int slot) const {
    if (where >= 0 && where < 24) {
        const int k = slot % 5, layer = slot / 5;
        const float z = bottomRow(where) ? EDGE_Z - CR - (float)k * 2.f * CR : -EDGE_Z + CR + (float)k * 2.f * CR;
        // a second / third layer sits half a checker further in
        const float shift = (float)layer * CR * (bottomRow(where) ? -1.f : 1.f);
        return {pointX(where), FIELD_Y + (float)layer * CH, z + shift};
    }
    if (where == BAR) {
        const float z = player == 0 ? 0.04f + (float)slot * 2.f * CR : -0.04f - (float)slot * 2.f * CR;
        return {0.f, Y0 + FRAME_H, z};
    }
    // OFF: borne-off checkers in short stacks on the felt to the right of the box
    const int col = slot / 5, k = slot % 5;
    const float x = FRAME_X + 0.035f;
    const float z = player == 0 ? 0.20f - (float)col * 2.1f * CR : -0.20f + (float)col * 2.1f * CR;
    return {x, Y0 + (float)k * CH, z};
}

Vector3 Tavla3D::pointWorld(int idx, int player) const {
    int n = 0;
    for (const Checker& c : chk_)
        if (c.where == idx && c.player == player) ++n;
    return slotWorld(idx, player, std::max(0, n - 1));
}

void Tavla3D::moveChecker(int player, int from, int to, float delay) {
    // the top checker of `player` at `from`
    int best = -1;
    for (int i = 0; i < CHECKERS; ++i)
        if (chk_[i].player == player && chk_[i].where == from && (best < 0 || chk_[i].slot > chk_[best].slot)) best = i;
    if (best < 0) return;
    int n = 0;
    for (const Checker& c : chk_)
        if (c.where == to && c.player == player) ++n;
    Checker& c = chk_[best];
    c.where = to;
    c.slot = n;
    c.from = c.cur;
    c.to = slotWorld(to, player, n);
    c.t = 0.f;
    c.delay = delay;
    c.hop = 0.035f;
    c.dur = std::clamp(0.25f + Vector3Distance(c.from, c.to) * 0.9f, 0.3f, 0.65f);
}

void Tavla3D::setPosition(const std::array<int8_t, 24>& pts, const std::array<int8_t, 2>& bar,
                          const std::array<int8_t, 2>& off, bool snap) {
    for (int p = 0; p < 2; ++p) {
        std::array<int, 26> want{};
        for (int i = 0; i < 24; ++i) want[(size_t)i] = p == 0 ? std::max<int>(0, pts[(size_t)i]) : std::max<int>(0, -pts[(size_t)i]);
        want[BAR] = bar[(size_t)p];
        want[OFF] = off[(size_t)p];
        std::array<std::vector<int>, 26> at;
        for (int i = 0; i < CHECKERS; ++i) {
            if (chk_[i].player != p) continue;
            const int w = std::clamp(chk_[i].where, 0, 25);
            at[(size_t)w].push_back(i);
        }
        std::vector<int> spare;
        for (int w = 0; w < 26; ++w) {
            auto& v = at[(size_t)w];
            std::sort(v.begin(), v.end(), [&](int a, int b) { return chk_[a].slot < chk_[b].slot; });
            while ((int)v.size() > want[(size_t)w]) {
                spare.push_back(v.back());
                v.pop_back();
            }
        }
        for (int w = 0; w < 26; ++w) {
            auto& v = at[(size_t)w];
            while ((int)v.size() < want[(size_t)w] && !spare.empty()) {
                const int id = spare.back();
                spare.pop_back();
                chk_[id].where = w;
                chk_[id].slot = 1000; // after the ones already there
                v.push_back(id);
            }
            for (int k = 0; k < (int)v.size(); ++k) {
                Checker& c = chk_[v[(size_t)k]];
                c.slot = k;
                const Vector3 target = slotWorld(w, p, k);
                if (snap) {
                    c.from = c.to = c.cur = target;
                    c.t = 1.f;
                    continue;
                }
                if (Vector3Distance(target, c.to) < 1e-5f) continue;
                if (c.t >= 1.f) { // not already moving: slide there (a hit checker hops to the bar)
                    c.from = c.cur;
                    c.t = 0.f;
                    c.delay = 0.f;
                    c.hop = w == BAR ? 0.05f : 0.02f;
                    c.dur = std::clamp(0.25f + Vector3Distance(c.from, target) * 0.9f, 0.3f, 0.7f);
                }
                c.to = target;
            }
        }
    }
}

Quaternion Tavla3D::faceUp(int value, float yawDeg) const {
    Quaternion q = QuaternionIdentity();
    switch (value) {
    case 6: q = QuaternionFromAxisAngle({1, 0, 0}, PI); break;
    case 2: q = QuaternionFromAxisAngle({0, 0, 1}, PI * 0.5f); break;
    case 5: q = QuaternionFromAxisAngle({0, 0, 1}, -PI * 0.5f); break;
    case 3: q = QuaternionFromAxisAngle({1, 0, 0}, -PI * 0.5f); break;
    case 4: q = QuaternionFromAxisAngle({1, 0, 0}, PI * 0.5f); break;
    default: break;
    }
    return QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, yawDeg * DEG2RAD), q);
}

void Tavla3D::throwDice(int player, int d1, int d2, bool opening, float delay) {
    const float side = player == 0 ? 1.f : -1.f;
    for (int i = 0; i < 2; ++i) {
        Die& d = dice_[(size_t)i];
        const int v = i == 0 ? d1 : d2;
        if (v <= 0) {
            d.visible = false;
            continue;
        }
        d.visible = true;
        d.used = false;
        d.value = v;
        int thrower = player;
        Vector3 land{0.12f + 0.06f * (float)i, FIELD_Y + DIE * 0.5f, 0.f};
        if (opening) { // one die each: ours lands in the right half, his in the left
            thrower = i;
            land = {i == 0 ? 0.15f : -0.15f, FIELD_Y + DIE * 0.5f, i == 0 ? 0.004f : -0.004f};
        } else if (player == 1) {
            land.x = -land.x;
        }
        const float ts = thrower == 0 ? 1.f : -1.f;
        d.from = {land.x * 0.6f + 0.04f * ts, Y0 + 0.16f, 0.36f * ts};
        d.to = land;
        d.cur = d.from;
        const float yaw = (float)((v * 37 + i * 53 + (int)(time_ * 100.f)) % 40 - 20);
        d.qTo = faceUp(v, yaw);
        d.spinAxis = Vector3Normalize({0.7f + 0.2f * (float)i, 0.3f, 0.5f * side});
        d.spin = 5.f * PI + (float)i * 1.3f;
        d.t = 0.f;
        d.dur = 0.85f + 0.08f * (float)i;
        d.delay = delay;
    }
}

void Tavla3D::setDiceUsed(const std::array<bool, 4>& used, int n, bool isDouble) {
    if (isDouble) {
        int u = 0;
        for (int i = 0; i < n; ++i) u += used[(size_t)i] ? 1 : 0;
        dice_[0].used = u >= 2;
        dice_[1].used = u >= 4;
    } else {
        dice_[0].used = used[0];
        dice_[1].used = used[1];
    }
}

void Tavla3D::hideDice() {
    dice_[0].visible = dice_[1].visible = false;
}

void Tavla3D::setHighlights(const std::vector<int>& targets, const std::vector<int>& sources, int selected) {
    targets_ = targets;
    sources_ = sources;
    selected_ = selected;
}

void Tavla3D::setSpeed(float s) { speed_ = std::clamp(s, 0.25f, 4.f); }

void Tavla3D::update(float dt) {
    time_ += dt;
    const float sdt = dt * speed_;
    for (Checker& c : chk_) {
        if (c.t >= 1.f) {
            c.cur = c.to;
            continue;
        }
        if (c.delay > 0.f) {
            c.delay -= sdt;
            continue;
        }
        c.t = std::min(1.f, c.t + sdt / std::max(0.05f, c.dur));
        const float e = easeInOut(c.t);
        c.cur = Vector3Lerp(c.from, c.to, e);
        c.cur.y += std::sin(c.t * PI) * c.hop;
    }
    for (Die& d : dice_) {
        if (!d.visible || d.t >= 1.f) {
            if (d.visible) {
                d.cur = d.to;
                d.qCur = d.qTo;
            }
            continue;
        }
        if (d.delay > 0.f) {
            d.delay -= sdt;
            d.cur = d.from;
            continue;
        }
        d.t = std::min(1.f, d.t + sdt / std::max(0.05f, d.dur));
        const float t = d.t;
        // a falling arc with two small bounces at the end
        const float e = 1.f - (1.f - t) * (1.f - t);
        d.cur = Vector3Lerp(d.from, d.to, e);
        float h = 0.f;
        if (t < 0.55f) h = (1.f - t / 0.55f) * (d.from.y - d.to.y) * 0.4f;
        else if (t < 0.8f) h = std::sin((t - 0.55f) / 0.25f * PI) * 0.018f;
        else h = std::sin((t - 0.8f) / 0.2f * PI) * 0.006f;
        d.cur.y = d.to.y + h + (t < 0.55f ? (d.from.y - d.to.y) * (1.f - t / 0.55f) * 0.6f : 0.f);
        const float left = (1.f - t) * (1.f - t);
        d.qCur = QuaternionMultiply(QuaternionFromAxisAngle(d.spinAxis, d.spin * left), d.qTo);
    }
}

bool Tavla3D::animating() const {
    for (const Checker& c : chk_)
        if (c.t < 1.f) return true;
    for (const Die& d : dice_)
        if (d.visible && d.t < 1.f) return true;
    return false;
}

void Tavla3D::submit(Renderer& r) {
    if (!ready_) return;
    r.submit(&boardMesh_, &matWood_, MatrixIdentity(), CastShadow);
    r.submit(&fieldMesh_, &matField_, MatrixIdentity(), 0);
    r.submit(&pointMesh_[0], &matPoint_[0], MatrixIdentity(), 0);
    r.submit(&pointMesh_[1], &matPoint_[1], MatrixIdentity(), 0);
    // the top checker of every playable point is lit (and lifted a little; the selected one more)
    std::array<int, 26> top;
    top.fill(-1);
    for (int i = 0; i < CHECKERS; ++i) {
        const Checker& c = chk_[i];
        if (c.player != 0 || c.where < 0 || c.where > 25) continue;
        if (top[(size_t)c.where] < 0 || chk_[top[(size_t)c.where]].slot < c.slot) top[(size_t)c.where] = i;
    }
    std::array<int, CHECKERS> lit{};
    for (int src : sources_)
        if (src >= 0 && src < 26 && top[(size_t)src] >= 0) lit[(size_t)top[(size_t)src]] = src == selected_ ? 2 : 1;
    const float bob = 0.0015f * std::sin(time_ * 5.f);
    for (int i = 0; i < CHECKERS; ++i) {
        const Checker& c = chk_[i];
        const int l = lit[(size_t)i];
        const float lift = l == 2 ? 0.012f + bob : l == 1 ? 0.003f : 0.f;
        const Mat* m = l == 2 ? &matCheckerSel_ : l == 1 ? &matCheckerHi_ : &matChecker_[c.player];
        r.submit(&checkerMesh_, m, MatrixTranslate(c.cur.x, c.cur.y + lift, c.cur.z), CastShadow);
    }
    for (const Die& d : dice_) {
        if (!d.visible) continue;
        matDie_.material.maps[MATERIAL_MAP_DIFFUSE].color = d.used ? Color{150, 144, 136, 255} : WHITE;
        r.submit(&dieMesh_, &matDie_, MatrixMultiply(QuaternionToMatrix(d.qCur), MatrixTranslate(d.cur.x, d.cur.y, d.cur.z)),
                 CastShadow);
    }
    // highlights: playable sources (gold), the selected one (strong gold), where it can go (green)
    auto glowPoint = [&](int i, Mat& m) {
        Matrix mx;
        if (i >= 0 && i < 24) {
            const bool bottom = bottomRow(i);
            const Matrix s = MatrixScale(PW * 0.95f, 1.f, PL);
            const Matrix rot = bottom ? MatrixIdentity() : MatrixRotateY(PI);
            mx = MatrixMultiply(MatrixMultiply(s, rot), MatrixTranslate(pointX(i), FIELD_Y + 0.0012f, bottom ? EDGE_Z : -EDGE_Z));
        } else if (i == OFF) {
            mx = MatrixMultiply(MatrixScale(0.07f, 1.f, 0.40f), MatrixTranslate(FRAME_X + 0.035f, Y0 + 0.002f, 0.22f));
        } else if (i == BAR) {
            mx = MatrixMultiply(MatrixScale(0.044f, 1.f, 0.2f), MatrixTranslate(0.f, Y0 + FRAME_H + 0.001f, 0.23f));
        } else {
            return;
        }
        r.submit(&glowMesh_, &m, mx, Transparent | DoubleSided | NoFog);
    };
    for (int i : sources_)
        if (i == BAR || i == selected_) glowPoint(i, i == selected_ ? matGlowSel_ : matGlow_);
    for (int i : targets_) glowPoint(i, matGlowTarget_);
}

int Tavla3D::pick(const Ray& ray) const {
    if (std::fabs(ray.direction.y) < 1e-5f) return -1;
    const float t = (FIELD_Y - ray.position.y) / ray.direction.y;
    if (t <= 0.f) return -1;
    const float x = ray.position.x + ray.direction.x * t, z = ray.position.z + ray.direction.z * t;
    if (x > FRAME_X + 0.005f && x < FRAME_X + 0.09f && std::fabs(z) < FRAME_Z) return OFF;
    if (std::fabs(x) <= HALF_IN + 0.004f && std::fabs(z) < EDGE_Z) return BAR;
    if (std::fabs(z) > EDGE_Z + 0.01f || std::fabs(x) > HALF_OUT + 0.01f || std::fabs(x) < HALF_IN) return -1;
    const bool bottom = z > 0.f;
    int col = (int)((std::fabs(x) - HALF_IN) / PW);
    col = std::clamp(col, 0, 5);
    if (bottom) return x > 0.f ? 5 - col : 6 + col;
    return x < 0.f ? 17 - col : 18 + col;
}

} // namespace r3d
