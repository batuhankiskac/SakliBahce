// The dama board in 3D (see Dama3D.h).
#include "r3d/Dama3D.h"

#include "r3d/World.h"
#include "ui/Common.h"

#include <algorithm>
#include <cmath>

namespace r3d {

namespace {

constexpr float Y0 = w3d::TABLE_Y;
constexpr float SQ = 0.055f;                // a square
constexpr float HALF = SQ * 4.f;            // the playing field spans |x|, |z| <= HALF
constexpr float RIM = 0.024f;               // the frame around it
constexpr float OUT = HALF + RIM;
constexpr float BASE_H = 0.012f;            // the board's thickness
constexpr float FIELD_Y = Y0 + BASE_H + 0.0006f;
constexpr float RIM_H = BASE_H + 0.006f;    // the frame stands a little proud of the squares
constexpr float PR = 0.0225f, PH = 0.009f;   // a disc's radius / height

float easeInOut(float t) { return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f; }

// A soft square glow: bright along the border, a light wash inside (stripes: diagonal bands, for the colour-blind).
Texture2D makeSquareGlow(bool stripes) {
    const int n = 64;
    Image img = GenImageColor(n, n, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = ((float)x + 0.5f) / (float)n, v = ((float)y + 0.5f) / (float)n;
            const float edge = std::min(std::min(u, 1.f - u), std::min(v, 1.f - v)); // 0 at the border .. 0.5 centre
            float a = 0.38f + 0.62f * std::clamp(1.f - edge / 0.16f, 0.f, 1.f);
            if (stripes) {
                const float k = (u + v) * 4.f;
                a *= k - std::floor(k) < 0.5f ? 1.f : 0.25f;
                a = std::max(a, 0.9f * std::clamp(1.f - edge / 0.1f, 0.f, 1.f));
            }
            px[y * n + x] = Color{255, 255, 255, (unsigned char)std::clamp(a * 255.f, 0.f, 255.f)};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}

} // namespace

bool Dama3D::init(Renderer& r) {
    if (ready_) return true;
    frameTex_ = genWoodTexture(512, Color{120, 72, 40, 255}, Color{70, 38, 18, 255}, 0xDA3Au, 20.f);
    sqTex_[0] = genWoodTexture(256, Color{98, 58, 32, 255}, Color{62, 34, 16, 255}, 0xDA3Bu, 14.f);   // walnut
    sqTex_[1] = genWoodTexture(256, Color{232, 204, 156, 255}, Color{196, 160, 108, 255}, 0xDA3Cu, 10.f); // maple
    pieceTex_[0] = genWoodTexture(256, Color{70, 34, 20, 255}, Color{38, 16, 10, 255}, 0xDA3Du, 26.f);
    pieceTex_[1] = genWoodTexture(256, Color{240, 222, 186, 255}, Color{210, 180, 132, 255}, 0xDA3Eu, 26.f);
    {
        MeshBuilder mb;
        mb.roundedBox({0, Y0 + BASE_H * 0.5f, 0}, {OUT * 2.f, BASE_H, OUT * 2.f}, 0.004f, 2);
        // the frame's four sides
        mb.roundedBox({0, Y0 + RIM_H * 0.5f, HALF + RIM * 0.5f}, {OUT * 2.f, RIM_H, RIM}, 0.004f, 2);
        mb.roundedBox({0, Y0 + RIM_H * 0.5f, -HALF - RIM * 0.5f}, {OUT * 2.f, RIM_H, RIM}, 0.004f, 2);
        mb.roundedBox({HALF + RIM * 0.5f, Y0 + RIM_H * 0.5f, 0}, {RIM, RIM_H, HALF * 2.f}, 0.003f, 2);
        mb.roundedBox({-HALF - RIM * 0.5f, Y0 + RIM_H * 0.5f, 0}, {RIM, RIM_H, HALF * 2.f}, 0.003f, 2);
        boardMesh_ = mb.build();
        matFrame_ = r.makeMat(WHITE, frameTex_, 0.4f, 46.f);
    }
    // the squares: dark (0) and light (1) parquet, the grain turned on every other square, each its own piece of wood
    for (int col = 0; col < 2; ++col) {
        MeshBuilder mb;
        for (int s = 0; s < 64; ++s) {
            const int rr = s / 8, cc = s % 8;
            const int light = (rr + cc) % 2; // a1 (bottom left) is dark, as on a chess board
            if (light != col) continue;
            const Vector3 c = squareLocal(s);
            const float h = SQ * 0.5f - 0.0004f;
            const float u0 = 0.13f * (float)((s * 7) % 8), v0 = 0.11f * (float)((s * 5) % 9);
            const bool turn = (rr + cc / 2) % 2 == 0;
            auto uv = [&](float a, float b) { return turn ? Vector2{u0 + b * 0.35f, v0 + a * 0.35f} : Vector2{u0 + a * 0.35f, v0 + b * 0.35f}; };
            const int a = mb.vertex({c.x - h, FIELD_Y, c.z + h}, {0, 1, 0}, uv(0, 0));
            const int b = mb.vertex({c.x + h, FIELD_Y, c.z + h}, {0, 1, 0}, uv(1, 0));
            const int d = mb.vertex({c.x + h, FIELD_Y, c.z - h}, {0, 1, 0}, uv(1, 1));
            const int e = mb.vertex({c.x - h, FIELD_Y, c.z - h}, {0, 1, 0}, uv(0, 1));
            mb.quad(a, b, d, e);
        }
        sqMesh_[col] = mb.build();
        matSq_[col] = r.makeMat(WHITE, sqTex_[col], col ? 0.22f : 0.3f, 30.f);
    }
    {
        // a disc: bevelled edge, a groove ring on top
        const std::vector<Vector2> prof = {{0.f, 0.f},          {PR * 0.93f, 0.f},   {PR, PH * 0.2f},  {PR, PH * 0.8f},
                                           {PR * 0.93f, PH},    {PR * 0.78f, PH},    {PR * 0.74f, PH * 0.86f},
                                           {PR * 0.66f, PH * 0.86f}, {PR * 0.62f, PH}, {0.f, PH}};
        pieceMesh_ = genLathe(prof, 30, false, false);
        matPiece_[0] = r.makeMat(WHITE, pieceTex_[0], 0.5f, 60.f);
        matPiece_[1] = r.makeMat(WHITE, pieceTex_[1], 0.45f, 60.f);
        matPieceHi_[0] = r.makeMat(Color{255, 190, 110, 255}, pieceTex_[0], 0.5f, 60.f, 0.42f);
        matPieceHi_[1] = r.makeMat(Color{255, 214, 120, 255}, pieceTex_[1], 0.5f, 60.f, 0.3f);
        matPieceSel_ = r.makeMat(Color{255, 176, 40, 255}, Texture2D{}, 0.5f, 60.f, 0.55f);
        // a thin brass ring between the two discs of a dama
        const std::vector<Vector2> ring = {{PR * 0.98f, 0.f}, {PR * 1.02f, 0.f}, {PR * 1.02f, 0.0014f}, {PR * 0.98f, 0.0014f}};
        ringMesh_ = genLathe(ring, 30, false, false);
        matRing_ = r.makeMat(Color{212, 168, 72, 255}, Texture2D{}, 0.8f, 90.f, 0.15f);
    }
    {
        MeshBuilder mb;
        const int a = mb.vertex({-0.5f, 0, 0.5f}, {0, 1, 0}, {0, 1});
        const int b = mb.vertex({0.5f, 0, 0.5f}, {0, 1, 0}, {1, 1});
        const int c = mb.vertex({0.5f, 0, -0.5f}, {0, 1, 0}, {1, 0});
        const int d = mb.vertex({-0.5f, 0, -0.5f}, {0, 1, 0}, {0, 0});
        mb.quad(a, b, c, d);
        glowMesh_ = mb.build();
        glowTex_ = makeSquareGlow(false);
        stripeTex_ = makeSquareGlow(true);
        matGlowTarget_ = r.makeMat(Color{40, 210, 90, 210}, glowTex_, 0.f, 4.f, 1.f);
        matGlowTargetCB_ = r.makeMat(Color{40, 170, 255, 235}, stripeTex_, 0.f, 4.f, 1.f);
        matGlowFocus_ = r.makeMat(Color{235, 250, 255, 240}, glowTex_, 0.f, 4.f, 1.f);
        matDoomed_ = r.makeMat(Color{240, 70, 50, 200}, glowTex_, 0.f, 4.f, 1.f);
        matShadowRim_ = r.makeMat(Color{250, 120, 40, 230}, stripeTex_, 0.f, 4.f, 1.f); // (colour-blind: doomed, striped)
    }
    for (int i = 0; i < PIECES; ++i) {
        Piece& p = pcs_[(size_t)i];
        p = Piece();
        p.light = i < 16 ? 1 : 0;
        p.tray = p.light ? 1 : 0;
        p.slot = i % 16;
        p.cur = trayLocal(p.tray, p.slot);
        p.keys = {p.cur};
        p.hops.clear();
    }
    ready_ = true;
    return true;
}

void Dama3D::shutdown(Renderer& r) {
    if (!ready_) return;
    for (Mesh* m : {&boardMesh_, &sqMesh_[0], &sqMesh_[1], &pieceMesh_, &glowMesh_, &ringMesh_}) UnloadMesh(*m);
    for (Mat* m : {&matFrame_, &matSq_[0], &matSq_[1], &matPiece_[0], &matPiece_[1], &matPieceHi_[0], &matPieceHi_[1],
                   &matPieceSel_, &matGlowTarget_, &matGlowTargetCB_, &matGlowFocus_, &matDoomed_, &matRing_, &matShadowRim_})
        r.unloadMat(*m);
    for (Texture2D* t : {&frameTex_, &sqTex_[0], &sqTex_[1], &pieceTex_[0], &pieceTex_[1], &glowTex_, &stripeTex_})
        if (t->id) UnloadTexture(*t);
    ready_ = false;
}

Vector3 Dama3D::squareLocal(int s) const {
    const int r = s / 8, c = s % 8;
    return {-HALF + SQ * ((float)c + 0.5f), FIELD_Y, HALF - SQ * ((float)r + 0.5f)};
}

// The captured discs stand in short stacks beside the board, on the capturer's right: tray 0 (the player) at +x near
// him, tray 1 (the opponent) at -x near the far side.
Vector3 Dama3D::trayLocal(int tray, int slot) const {
    const int stack = slot / 4, layer = slot % 4;
    const float side = tray == 0 ? 1.f : -1.f;
    const float x = side * (OUT + 0.034f + (float)(stack % 2) * 0.05f);
    const float z = side * (0.16f - (float)(stack / 2) * 0.05f);
    return {x, Y0 + (float)layer * PH, z};
}

void Dama3D::setFrame(const Matrix& frame) {
    frame_ = frame;
    frameInv_ = MatrixInvert(frame);
}

int Dama3D::pieceAt(int square) const {
    for (int i = 0; i < PIECES; ++i)
        if (pcs_[(size_t)i].square == square) return i;
    return -1;
}

int Dama3D::trayCount(int tray) const {
    int n = 0;
    for (const Piece& p : pcs_) n += p.square < 0 && p.tray == tray ? 1 : 0;
    return n;
}

void Dama3D::glide(Piece& p, Vector3 to, float hop, float delay) {
    p.keys = {p.cur, to};
    p.hops = {hop};
    p.t = 0.f;
    p.delay = delay;
    p.segDur = std::clamp(0.28f + Vector3Distance(p.cur, to) * 0.9f, 0.3f, 0.75f);
}

void Dama3D::playMove(int player, int from, const std::vector<int>& path, const std::vector<int>& captured, bool promotes,
                      float delay, PieceCarry* hand) {
    const int idx = pieceAt(from);
    if (idx < 0 || path.empty()) return;
    Piece& p = pcs_[(size_t)idx];
    const bool jumps = !captured.empty();
    const float top = PH * (p.king ? 2.f : 1.f); // (where the fingers hold it: its top)
    p.keys = {p.cur};
    p.hops.clear();
    for (int s : path) {
        p.keys.push_back(squareLocal(s));
        // (by hand: lifted clear of the board, and over the disc it jumps)
        p.hops.push_back(hand ? (jumps ? 0.040f : 0.020f) : (jumps ? 0.032f : 0.007f));
    }
    p.t = 0.f;
    p.delay = delay;
    p.segDur = jumps ? 0.34f : std::clamp(0.3f + Vector3Distance(p.keys[0], p.keys[1]) * 0.8f, 0.32f, 0.6f);
    if (hand) {
        p.segDur += 0.06f; // (a hand is a little slower than a slide)
        hand->path.clear();
        for (const Vector3& k : p.keys) hand->path.push_back(toWorld({k.x, k.y + top, k.z}));
        hand->lead = delay;
        hand->seg = p.segDur;
        hand->hop = p.hops.empty() ? 0.f : p.hops[0];
        hand->card = false;
        hand->takes.clear();
    }
    p.square = path.back();
    if (promotes) p.king = true; // (the crown settles once it has landed)
    // the taken discs leave one by one, right after the jump over them (by hand: after the landing, as the hand takes
    // each one off)
    const int base = trayCount(player);
    float handAt = delay + p.segDur * (float)path.size() + 0.30f;
    for (size_t k = 0; k < captured.size(); ++k) {
        const int ci = pieceAt(captured[k]);
        if (ci < 0 || ci == idx) continue;
        Piece& c = pcs_[(size_t)ci];
        c.square = -1;
        c.tray = player;
        c.slot = base + (int)k;
        const Vector3 to = trayLocal(player, c.slot);
        if (!hand) {
            glide(c, to, 0.07f, delay + p.segDur * (float)(k + 1) + 0.04f);
            continue;
        }
        const Vector3 at = c.cur;
        glide(c, to, 0.06f, handAt);
        const float ctop = PH * (c.king ? 2.f : 1.f);
        PieceCarry::Take t;
        t.from = toWorld({at.x, at.y + ctop, at.z});
        t.to = toWorld({to.x, to.y + ctop, to.z});
        t.at = handAt;
        t.dur = c.segDur;
        hand->takes.push_back(t);
        handAt += c.segDur + 0.42f; // (the hand lets go, comes back up and goes for the next one)
    }
}

void Dama3D::setBoard(const std::array<int8_t, 64>& cells, int lightPlayer, bool snap) {
    lightPlayer_ = lightPlayer;
    for (int light = 0; light < 2; ++light) {
        const int owner = light ? lightPlayer : 1 - lightPlayer;
        auto mine = [&](int s) { return owner == 0 ? cells[(size_t)s] > 0 : cells[(size_t)s] < 0; };
        std::vector<int> spare; // this colour's discs not where they belong: on-board ones first
        std::vector<int> trayed;
        for (int i = 0; i < PIECES; ++i) {
            Piece& p = pcs_[(size_t)i];
            if (p.light != light) continue;
            if (p.square >= 0 && mine(p.square)) continue;
            (p.square >= 0 ? spare : trayed).push_back(i);
        }
        spare.insert(spare.end(), trayed.begin(), trayed.end());
        size_t next = 0;
        for (int s = 0; s < 64; ++s) {
            bool here = false; // (a disc of the other colour still leaving this square does not count)
            for (const Piece& q : pcs_) here = here || (q.square == s && q.light == light);
            if (!mine(s) || here) continue;
            if (next >= spare.size()) break;
            Piece& p = pcs_[(size_t)spare[next++]];
            p.square = s;
            p.tray = -1;
            if (snap) {
                p.cur = squareLocal(s);
                p.keys = {p.cur};
                p.t = 1.f;
            } else {
                glide(p, squareLocal(s), 0.05f, 0.f);
            }
        }
        // the rest go to the tray of the side that took them
        const int tray = 1 - owner;
        for (; next < spare.size(); ++next) {
            Piece& p = pcs_[(size_t)spare[next]];
            if (p.square < 0 && p.tray == tray) continue; // already there
            p.square = -1;
            p.tray = tray;
            p.slot = trayCount(tray);
            if (snap) {
                p.cur = trayLocal(tray, p.slot);
                p.keys = {p.cur};
                p.t = 1.f;
            } else {
                glide(p, trayLocal(tray, p.slot), 0.06f, 0.f);
            }
        }
    }
    // damas and men; discs at rest that are not where they should be (a snap) go there
    for (Piece& p : pcs_) {
        if (p.square >= 0) {
            const int v = cells[(size_t)p.square];
            const bool king = v == 2 || v == -2;
            if (!king) p.crown = 0.f;
            if (king && !p.king && snap) p.crown = 1.f;
            p.king = king;
            if (snap) {
                p.crown = king ? 1.f : 0.f;
                p.cur = squareLocal(p.square);
                p.keys = {p.cur};
                p.t = 1.f;
                p.delay = 0.f;
            }
        } else {
            p.king = false;
            p.crown = 0.f;
            if (snap) {
                p.cur = trayLocal(p.tray, p.slot);
                p.keys = {p.cur};
                p.t = 1.f;
                p.delay = 0.f;
            }
        }
    }
}

void Dama3D::setHighlights(const std::vector<int>& movable, int selected, const std::vector<int>& targets) {
    movable_ = movable;
    selected_ = selected;
    targets_ = targets;
}

void Dama3D::setDrag(int square, Vector3 local) {
    drag_ = square;
    dragLocal_ = local;
}

void Dama3D::setSpeed(float s) { speed_ = std::clamp(s, 0.25f, 4.f); }

void Dama3D::update(float dt) {
    time_ += dt;
    const float sdt = dt * speed_;
    for (Piece& p : pcs_) {
        const float segs = (float)p.keys.size() - 1.f;
        if (p.t < segs) {
            if (p.delay > 0.f) {
                p.delay -= sdt;
                continue;
            }
            p.t = std::min(segs, p.t + sdt / std::max(0.05f, p.segDur));
            const int seg = std::min((int)p.t, (int)segs - 1);
            const float f = p.t - (float)seg;
            // one plain step / a long glide eases in and out; jumps keep a steady rhythm with a little ease
            const float e = segs > 1.f ? 0.15f * f + 0.85f * easeInOut(f) : easeInOut(f);
            p.cur = Vector3Lerp(p.keys[(size_t)seg], p.keys[(size_t)seg + 1], e);
            p.cur.y += std::sin(f * PI) * (seg < (int)p.hops.size() ? p.hops[(size_t)seg] : 0.f);
        } else if (!p.keys.empty()) {
            p.cur = p.keys.back();
        }
        if (p.king && p.t >= segs && p.crown < 1.f) p.crown = std::min(1.f, p.crown + sdt / 0.45f);
    }
}

bool Dama3D::animating() const {
    for (const Piece& p : pcs_) {
        if (p.t < (float)p.keys.size() - 1.f) return true;
        if (p.king && p.crown < 1.f) return true;
    }
    return false;
}

void Dama3D::submit(Renderer& r) {
    if (!ready_) return;
    const Matrix& F = frame_;
    r.submit(&boardMesh_, &matFrame_, F, CastShadow);
    r.submit(&sqMesh_[0], &matSq_[0], F, 0);
    r.submit(&sqMesh_[1], &matSq_[1], F, 0);
    const float bob = 0.0016f * std::sin(time_ * 5.f);
    for (const Piece& p : pcs_) {
        const bool movable = p.square >= 0 && std::find(movable_.begin(), movable_.end(), p.square) != movable_.end();
        const bool sel = p.square >= 0 && p.square == selected_;
        Vector3 at = p.cur;
        if (p.square >= 0 && p.square == drag_) at = {dragLocal_.x, FIELD_Y + 0.022f, dragLocal_.z};
        else if (sel) at.y += 0.012f + bob;
        else if (movable) at.y += 0.002f;
        const Mat* m = sel ? &matPieceSel_ : movable ? &matPieceHi_[p.light] : &matPiece_[p.light];
        r.submit(&pieceMesh_, m, MatrixMultiply(MatrixTranslate(at.x, at.y, at.z), F), CastShadow);
        if (movable && !sel && !(p.king && p.crown > 0.f)) // a gold ring on top: movable, by shape as well as colour
            r.submit(&ringMesh_, &matRing_, MatrixMultiply(MatrixMultiply(MatrixScale(0.72f, 1.f, 0.72f), MatrixTranslate(at.x, at.y + PH, at.z)), F), 0);
        if (p.king && p.crown > 0.f) {
            const float e = easeInOut(p.crown);
            const float y = at.y + PH + 0.0014f + (1.f - e) * 0.05f;
            r.submit(&ringMesh_, &matRing_, MatrixMultiply(MatrixTranslate(at.x, at.y + PH, at.z), F), 0);
            r.submit(&pieceMesh_, m, MatrixMultiply(MatrixTranslate(at.x, y, at.z), F), CastShadow);
            if (movable && !sel) // (the dama's ring sits on its upper disc)
                r.submit(&ringMesh_, &matRing_, MatrixMultiply(MatrixMultiply(MatrixScale(0.72f, 1.f, 0.72f), MatrixTranslate(at.x, y + PH, at.z)), F), 0);
        }
    }
    auto glowSquare = [&](int s, Mat& m, float scale) {
        const Vector3 c = squareLocal(s);
        const Matrix mx = MatrixMultiply(MatrixScale(SQ * scale, 1.f, SQ * scale), MatrixTranslate(c.x, FIELD_Y + 0.0009f, c.z));
        r.submit(&glowMesh_, &m, MatrixMultiply(mx, F), Transparent | DoubleSided | NoFog);
    };
    const bool cb = ui::colorBlind();
    for (int s : targets_) glowSquare(s, cb ? matGlowTargetCB_ : matGlowTarget_, 0.94f);
    if (keyFocus_ >= 0 && std::find(targets_.begin(), targets_.end(), keyFocus_) != targets_.end()) {
        glowSquare(keyFocus_, matGlowFocus_, 0.98f);
        glowSquare(keyFocus_, cb ? matGlowTargetCB_ : matGlowTarget_, 0.98f);
    }
    for (int s : doomed_) glowSquare(s, cb ? matShadowRim_ : matDoomed_, 0.9f);
}

bool Dama3D::pickLocal(const Ray& worldRay, Vector3& local) const {
    Ray ray;
    ray.position = Vector3Transform(worldRay.position, frameInv_);
    ray.direction = Vector3Subtract(Vector3Transform(Vector3Add(worldRay.position, worldRay.direction), frameInv_), ray.position);
    if (std::fabs(ray.direction.y) < 1e-5f) return false;
    const float t = (FIELD_Y - ray.position.y) / ray.direction.y;
    if (t <= 0.f) return false;
    local = {ray.position.x + ray.direction.x * t, FIELD_Y, ray.position.z + ray.direction.z * t};
    return true;
}

int Dama3D::pick(const Ray& ray) const {
    Vector3 l;
    if (!pickLocal(ray, l)) return -1;
    if (std::fabs(l.x) >= HALF || std::fabs(l.z) >= HALF) return -1;
    const int c = std::clamp((int)((l.x + HALF) / SQ), 0, 7);
    const int r = std::clamp((int)((HALF - l.z) / SQ), 0, 7);
    return r * 8 + c;
}

} // namespace r3d
