// Gfx owner's demo + verification harness. Builds a stand-in kıraathane around our okey table (felt, rim,
// racks with standing tiles, melds lying on the felt, tea glasses, ashtray with smoke, pendant lamp, walls,
// background tables, simple seated figures) and renders it with r3d::Renderer:
//   build/gfx/seat.png        human seat (w3d::EYE)          build/gfx/close_felt.png  tiles lying on felt
//   build/gfx/close_rack.png  standing tiles in the rack     build/gfx/back.png        behind seat 2
//   build/gfx/corner.png      room corner                    build/gfx/lamp.png        looking up at the lamp
//   build/gfx/meshes.png      MeshBuilder primitives, back-face culling ON (inside-out = visible bug)
//   build/gfx/textures.png    generated textures tiled 2x2 (seams) + sprite textures
//   build/gfx/window.png      same seat view through the default (MSAA) framebuffer path
// It also checks winding/normals numerically, projectToVirtual/rayFromVirtual round trips (target and
// window paths, with a letterboxed viewport) and times render() with ~600 submissions + ~1000 particles.
#include "r3d/Gfx.h"
#include "r3d/World.h"
#include "ui/Common.h"
#include "ui/TileRender.h"

#include <raymath.h>
#include <rlgl.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace r3d;
using namespace w3d;

namespace {

constexpr int W = 1600, H = 900;

struct Item {
    const Mesh* mesh;
    const Mat* mat;
    Matrix xf;
    uint32_t flags;
};

struct Demo {
    Renderer R;
    std::vector<Mesh*> meshes;
    std::vector<Mat*> mats;
    std::vector<Texture2D> textures;
    std::vector<Item> items;  // static scene
    std::vector<std::pair<Vector3, SmokeParams>> emitters;
    std::vector<float> emitAcc;
    float emitRate[64]{};
    Mat* bulbMat = nullptr;
    Mat* tileMat = nullptr;

    Mesh* keep(Mesh m) {
        meshes.push_back(new Mesh(m));
        return meshes.back();
    }
    Mat* mat(Color c, Texture2D t = {}, float spec = 0.15f, float shin = 24.f, float emis = 0.f, float rim = 0.f) {
        mats.push_back(new Mat(R.makeMat(c, t, spec, shin, emis, rim)));
        return mats.back();
    }
    Texture2D tex(Texture2D t) {
        textures.push_back(t);
        return t;
    }
    void add(const Mesh* m, const Mat* mt, Matrix xf, uint32_t flags = CastShadow) { items.push_back({m, mt, xf, flags}); }
};

Matrix T(float x, float y, float z) { return MatrixTranslate(x, y, z); }
Matrix mul(Matrix a, Matrix b) { return MatrixMultiply(a, b); }

// Tile mesh whose front face (+Z) samples `key` from the 3D face atlas and whose body uses the plain ivory cell.
Mesh tileMesh(int key) {
    MeshBuilder b;
    b.roundedBox({0, 0, 0}, {TILE_W, TILE_H, TILE_T}, 0.0026f, 3);
    Mesh m = b.build();
    Rectangle face = ui::tilegfx::faceUV3D(key), body = ui::tilegfx::faceUV3D(ui::tilegfx::KEY_BODY);
    Rectangle back = ui::tilegfx::faceUV3D(ui::tilegfx::KEY_BACK);
    for (int i = 0; i < m.vertexCount; ++i) {
        float nz = m.normals[i * 3 + 2];
        float* uv = &m.texcoords[i * 2];
        if (nz > 0.55f) {
            uv[0] = face.x + uv[0] * face.width;
            uv[1] = face.y + uv[1] * face.height;
        } else if (nz < -0.55f) {
            uv[0] = back.x + uv[0] * back.width;
            uv[1] = back.y + uv[1] * back.height;
        } else {
            uv[0] = body.x + body.width * 0.5f;
            uv[1] = body.y + body.height * 0.5f;
        }
    }
    UpdateMeshBuffer(m, 1, m.texcoords, m.vertexCount * 2 * sizeof(float), 0);
    return m;
}

void buildScene(Demo& D, bool withTileFaces) {
    // ---------------------------------------------------------------- textures & materials
    Texture2D felt = D.tex(genFeltTexture(512, ui::pal::Felt, 3));
    Texture2D wood = D.tex(genWoodTexture(512, Color{150, 96, 56, 255}, Color{74, 40, 20, 255}, 5, 14));
    Texture2D rackWood = D.tex(genWoodTexture(512, Color{168, 112, 64, 255}, Color{96, 56, 28, 255}, 12, 10));
    Texture2D floorWood = D.tex(genWoodTexture(512, Color{104, 70, 44, 255}, Color{48, 30, 18, 255}, 21, 8));
    Texture2D plaster = D.tex(genPlasterTexture(512, Color{176, 146, 96, 255}, 9));
    Texture2D ceilingTex = D.tex(genPlasterTexture(512, Color{150, 126, 90, 255}, 31));
    Texture2D shirt = D.tex(genNoiseTexture(256, Color{120, 128, 150, 255}, Color{80, 86, 104, 255}, 6, 44));
    Texture2D tiles = withTileFaces ? ui::tilegfx::faceAtlas3D() : Texture2D{};

    Mat* mFelt = D.mat(WHITE, felt, 0.04f, 6.f, 0.f, 0.15f);
    Mat* mWood = D.mat(WHITE, wood, 0.30f, 36.f);
    Mat* mRack = D.mat(WHITE, rackWood, 0.28f, 30.f);
    Mat* mFloor = D.mat(WHITE, floorWood, 0.22f, 24.f);
    Mat* mWall = D.mat(WHITE, plaster, 0.03f, 8.f);
    Mat* mWainscot = D.mat(Color{200, 170, 140, 255}, floorWood, 0.20f, 20.f);
    Mat* mCeiling = D.mat(WHITE, ceilingTex, 0.02f, 8.f);
    Mat* mBeam = D.mat(Color{150, 120, 100, 255}, floorWood, 0.1f, 12.f);
    Mat* mTile = D.mat(withTileFaces ? WHITE : ui::pal::TileFace, tiles, 0.45f, 70.f);
    D.tileMat = mTile;
    Mat* mGlass = D.mat(Color{226, 236, 246, 46}, {}, 0.9f, 150.f, 0.f, 0.7f);
    Mat* mTea = D.mat(Color{150, 42, 10, 215}, {}, 0.6f, 90.f);
    Mat* mOralet = D.mat(Color{236, 118, 24, 225}, {}, 0.5f, 80.f);
    Mat* mSaucer = D.mat(Color{238, 234, 226, 255}, {}, 0.5f, 60.f);
    Mat* mSkin = D.mat(Color{206, 150, 112, 255}, {}, 0.28f, 22.f, 0.f, 0.5f);
    Mat* mShirt = D.mat(WHITE, shirt, 0.04f, 8.f, 0.f, 0.55f);
    Mat* mVest = D.mat(Color{58, 48, 42, 255}, {}, 0.05f, 10.f, 0.f, 0.5f);
    Mat* mCardigan = D.mat(Color{120, 96, 70, 255}, {}, 0.04f, 8.f, 0.f, 0.6f);
    Mat* mHair = D.mat(Color{220, 216, 206, 255}, {}, 0.15f, 12.f, 0.f, 0.5f);
    Mat* mMust = D.mat(Color{40, 32, 28, 255}, {}, 0.1f, 10.f, 0.f, 0.4f);
    Mat* mCap = D.mat(Color{70, 64, 58, 255}, {}, 0.05f, 10.f, 0.f, 0.4f);
    Mat* mShadeOut = D.mat(Color{44, 86, 64, 255}, {}, 0.55f, 60.f);
    Mat* mShadeIn = D.mat(Color{250, 244, 226, 255}, {}, 0.2f, 20.f, 0.25f);
    Mat* mBulb = D.mat(Color{255, 236, 190, 255}, {}, 0.f, 8.f, 1.f);
    Mat* mCord = D.mat(Color{30, 26, 24, 255}, {}, 0.2f, 20.f);
    Mat* mMetal = D.mat(Color{150, 140, 128, 255}, {}, 0.7f, 80.f);
    Mat* mWindow = D.mat(Color{40, 62, 110, 255}, {}, 0.8f, 120.f, 0.55f);
    Mat* mPaper = D.mat(Color{236, 232, 222, 255}, {}, 0.05f, 8.f);
    Mat* mEmber = D.mat(Color{255, 120, 40, 255}, {}, 0.f, 8.f, 1.f);
    Mat* mStove = D.mat(Color{36, 32, 30, 255}, {}, 0.5f, 40.f);
    Mat* mBottle = D.mat(Color{60, 120, 70, 150}, {}, 0.9f, 120.f, 0.f, 0.6f);
    D.bulbMat = mBulb;

    MeshBuilder b;
    // ---------------------------------------------------------------- room shell
    const float RW = ROOM_X1 - ROOM_X0, RD = ROOM_Z1 - ROOM_Z0, cx = (ROOM_X0 + ROOM_X1) * 0.5f, cz = (ROOM_Z0 + ROOM_Z1) * 0.5f;
    b.clear();
    b.plane({cx, 0, cz}, {RW, RD}, {0, 1, 0}, {RW / 1.4f, RD / 1.4f});
    Mesh* floor = D.keep(b.build());
    D.add(floor, mFloor, MatrixIdentity(), 0);
    b.clear();
    b.plane({cx, CEILING_Y, cz}, {RW, RD}, {0, -1, 0}, {3, 3});
    D.add(D.keep(b.build()), mCeiling, MatrixIdentity(), 0);
    // upper walls (plaster) and wainscot (wood) on all four sides, facing inward
    struct WallDef {
        Vector3 c;
        float len;
        Vector3 n;
    };
    WallDef walls[4] = {{{cx, 0, ROOM_Z0}, RW, {0, 0, 1}}, {{cx, 0, ROOM_Z1}, RW, {0, 0, -1}},
                        {{ROOM_X0, 0, cz}, RD, {1, 0, 0}}, {{ROOM_X1, 0, cz}, RD, {-1, 0, 0}}};
    b.clear();
    for (const WallDef& w : walls) b.plane({w.c.x, 2.05f, w.c.z}, {w.len, 2.1f}, w.n, {w.len / 2.2f, 1.f});
    D.add(D.keep(b.build()), mWall, MatrixIdentity(), 0);
    b.clear();
    for (const WallDef& w : walls) {
        b.plane({w.c.x + w.n.x * 0.02f, 0.5f, w.c.z + w.n.z * 0.02f}, {w.len, 1.0f}, w.n, {w.len / 1.5f, 1.f});
        Vector3 sz = std::fabs(w.n.x) > 0.5f ? Vector3{0.05f, 0.05f, w.len} : Vector3{w.len, 0.05f, 0.05f};
        b.roundedBox({w.c.x + w.n.x * 0.03f, 1.02f, w.c.z + w.n.z * 0.03f}, sz, 0.012f, 2);
    }
    D.add(D.keep(b.build()), mWainscot, MatrixIdentity(), 0);
    b.clear();
    for (float x = -3.3f; x <= 3.4f; x += 1.65f) b.roundedBox({x, CEILING_Y - 0.09f, cz}, {0.16f, 0.18f, RD}, 0.02f, 2);
    D.add(D.keep(b.build()), mBeam, MatrixIdentity(), 0);
    // window on the left wall
    b.clear();
    b.plane({ROOM_X0 + 0.03f, 1.75f, 0.6f}, {1.2f, 1.1f}, {1, 0, 0});
    D.add(D.keep(b.build()), mWindow, MatrixIdentity(), 0);
    b.clear();
    b.roundedBox({ROOM_X0 + 0.05f, 1.75f, 0.6f}, {0.06f, 1.2f, 0.06f}, 0.01f, 2);
    b.roundedBox({ROOM_X0 + 0.05f, 1.75f, 0.6f}, {0.06f, 0.06f, 1.24f}, 0.01f, 2);
    b.roundedBox({ROOM_X0 + 0.05f, 2.32f, 0.6f}, {0.08f, 0.06f, 1.3f}, 0.01f, 2);
    b.roundedBox({ROOM_X0 + 0.05f, 1.18f, 0.6f}, {0.1f, 0.05f, 1.34f}, 0.01f, 2);
    D.add(D.keep(b.build()), mWood, MatrixIdentity(), CastShadow);
    // stove
    b.clear();
    b.cylinder({0, 0, 0}, 0.24f, 0.75f, 28);
    b.cylinder({0, 0.75f, 0}, 0.07f, 1.9f, 16);
    D.add(D.keep(b.build()), mStove, T(-3.5f, 0, -2.7f));

    // ---------------------------------------------------------------- our table
    b.clear();
    b.roundedBox({0, TABLE_Y - 0.024f, 0}, {1.24f, 0.036f, 1.24f}, 0.012f, 3);  // top slab
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.roundedBox({sx * 0.55f, (TABLE_Y - 0.04f) * 0.5f, sz * 0.55f}, {0.06f, TABLE_Y - 0.04f, 0.06f}, 0.01f, 2);
    b.roundedBox({0, TABLE_Y - 0.08f, 0.56f}, {1.1f, 0.08f, 0.025f}, 0.006f, 2);
    b.roundedBox({0, TABLE_Y - 0.08f, -0.56f}, {1.1f, 0.08f, 0.025f}, 0.006f, 2);
    b.roundedBox({0.56f, TABLE_Y - 0.08f, 0}, {0.025f, 0.08f, 1.1f}, 0.006f, 2);
    b.roundedBox({-0.56f, TABLE_Y - 0.08f, 0}, {0.025f, 0.08f, 1.1f}, 0.006f, 2);
    // raised rim
    const float rimY = TABLE_Y - 0.006f + (RIM_H + 0.006f) * 0.5f, rimH = RIM_H + 0.006f, o = FELT_HALF + RIM_W * 0.5f;
    b.roundedBox({0, rimY, o}, {1.24f, rimH, RIM_W}, 0.008f, 3);
    b.roundedBox({0, rimY, -o}, {1.24f, rimH, RIM_W}, 0.008f, 3);
    b.roundedBox({o, rimY, 0}, {RIM_W, rimH, FELT_HALF * 2.f}, 0.008f, 3);
    b.roundedBox({-o, rimY, 0}, {RIM_W, rimH, FELT_HALF * 2.f}, 0.008f, 3);
    D.add(D.keep(b.build()), mWood, MatrixIdentity());
    b.clear();
    b.box({0, TABLE_Y - 0.003f, 0}, {FELT_HALF * 2.f, 0.006f, FELT_HALF * 2.f});
    D.add(D.keep(b.build()), mFelt, MatrixIdentity());

    // ---------------------------------------------------------------- racks + tiles
    b.clear();
    b.roundedBox({0, 0.010f, 0.012f}, {RACK_LEN, 0.020f, RACK_DEPTH - 0.024f}, 0.006f, 2);   // front shelf
    b.roundedBox({0, 0.030f, -0.030f}, {RACK_LEN, 0.060f, 0.026f}, 0.007f, 2);             // back tier
    b.roundedBox({0, 0.004f, 0.0f}, {RACK_LEN + 0.01f, 0.008f, RACK_DEPTH}, 0.004f, 2);     // base
    Mesh* rack = D.keep(b.build());
    std::vector<Mesh*> faces;
    for (int k = 0; k < 13; ++k) faces.push_back(D.keep(withTileFaces ? tileMesh(k + (k % 4) * 13) : genRoundedBox(TILE_W, TILE_H, TILE_T, 0.0026f, 3)));
    Mesh* back = D.keep(withTileFaces ? tileMesh(ui::tilegfx::KEY_BACK) : genRoundedBox(TILE_W, TILE_H, TILE_T, 0.0026f, 3));
    for (int s = 0; s < 4; ++s) {
        Vector3 rc = seatLocal(s, 0.f, RACK_DIST, TABLE_Y);
        D.add(rack, mRack, trsYaw(rc, seatYawDeg(s)));
        const int n = s == 0 ? 22 : 21 - s;
        for (int i = 0; i < n; ++i) {
            int row = i / 12, col = i % 12;
            if (s == 0 && (col == 4 || col == 9)) continue;  // gaps between groups
            float right = -RACK_LEN * 0.5f + 0.03f + col * (TILE_W + 0.0035f) + row * 0.012f;
            float out = RACK_DIST + (row == 0 ? 0.012f : -0.028f);
            float y = TABLE_Y + (row == 0 ? 0.020f : 0.060f) + TILE_H * 0.5f;
            Vector3 p = seatLocal(s, right, out, y);
            Matrix m = mul(MatrixRotateX(-16.f * DEG2RAD), MatrixRotateY(seatYawDeg(s) * DEG2RAD));
            D.add(s == 0 ? faces[(i * 5) % 13] : back, mTile, mul(m, T(p.x, p.y, p.z)));
        }
    }
    // melds lying on the felt (readable from the human seat) + discard stacks + pile + indicator
    auto flat = [&](Mesh* m, float x, float z, float yawDeg, float lift = 0.f, bool faceDown = false) {
        Matrix r = mul(MatrixRotateX((faceDown ? 90.f : -90.f) * DEG2RAD), MatrixRotateY(yawDeg * DEG2RAD));
        D.add(m, mTile, mul(r, T(x, TABLE_Y + TILE_T * 0.5f + lift, z)));
    };
    for (int i = 0; i < 5; ++i) flat(faces[(3 + i) % 13], -0.20f + i * (TILE_W + 0.002f), 0.22f, 0.f);
    for (int i = 0; i < 4; ++i) flat(faces[(8 + i) % 13], 0.02f + i * (TILE_W + 0.002f), 0.30f, 0.f);
    for (int i = 0; i < 6; ++i) flat(faces[i % 13], -0.12f + i * (TILE_W + 0.002f), -0.24f, 0.f);
    for (int i = 0; i < 3; ++i) flat(faces[(i + 6) % 13], 0.24f, -0.12f + i * (TILE_W + 0.002f), 90.f);
    for (int s = 0; s < 4; ++s)
        for (int k = 0; k < 3; ++k)
            flat(faces[(s * 3 + k) % 13], DISCARD_POS[s].x + k * 0.004f, DISCARD_POS[s].z - k * 0.003f, (k - 1) * 7.f, k * TILE_T);
    for (int layer = 0; layer < 2; ++layer)
        for (int i = 0; i < 5; ++i) flat(back, PILE_POS.x, PILE_POS.z - 0.09f + i * (TILE_H + 0.002f), 90.f, layer * TILE_T, true);
    b.clear();
    b.roundedBox({0, 0.006f, 0}, {0.05f, 0.012f, 0.06f}, 0.004f, 2);
    D.add(D.keep(b.build()), mRack, T(INDICATOR_POS.x, TABLE_Y, INDICATOR_POS.z));
    {
        Matrix r = mul(MatrixRotateX(-70.f * DEG2RAD), MatrixIdentity());
        D.add(faces[6], mTile, mul(r, T(INDICATOR_POS.x, TABLE_Y + 0.012f + TILE_H * 0.5f * 0.35f, INDICATOR_POS.z)));
    }

    // ---------------------------------------------------------------- tea glasses (ince belli) + saucers
    Mesh* glass = D.keep(genLathe({{0.0f, 0.0f}, {0.019f, 0.0f}, {0.022f, 0.006f}, {0.024f, 0.022f}, {0.0165f, 0.052f},
                                   {0.019f, 0.075f}, {0.0232f, 0.094f}, {0.0238f, 0.096f}},
                                  36, false, false));
    Mesh* tea = D.keep(genLathe({{0.0f, 0.004f}, {0.019f, 0.005f}, {0.0225f, 0.022f}, {0.0158f, 0.052f}, {0.0180f, 0.072f}}, 36, false, true));
    Mesh* saucer = D.keep(genLathe({{0.0f, 0.0f}, {0.035f, 0.0f}, {0.052f, 0.006f}, {0.058f, 0.011f}, {0.056f, 0.012f}, {0.040f, 0.006f}, {0.0f, 0.005f}}, 40, false, false));
    for (int s = 0; s < 4; ++s) {
        Vector3 g = GLASS_POS[s];
        D.add(saucer, mSaucer, T(g.x, g.y, g.z));
        D.add(tea, s == 3 ? mOralet : mTea, T(g.x, g.y + 0.004f, g.z), CastShadow | Transparent);
        D.add(glass, mGlass, T(g.x, g.y + 0.006f, g.z), Transparent | DoubleSided);
        D.emitters.push_back({{g.x, g.y + 0.078f, g.z}, SmokeParams{{0, 0.03f, 0}, 0.010f, 0.055f, 2.4f, 0.10f, {236, 234, 230, 255}, 0.5f, 0.012f}});
    }
    // ashtray + cigarette with ember
    Mesh* ash = D.keep(genLathe({{0.0f, 0.0f}, {0.045f, 0.0f}, {0.05f, 0.018f}, {0.047f, 0.022f}, {0.036f, 0.022f}, {0.034f, 0.008f}, {0.0f, 0.008f}}, 36, false, false));
    D.add(ash, mGlass, T(ASHTRAY_POS.x, ASHTRAY_POS.y, ASHTRAY_POS.z), Transparent | DoubleSided);
    D.add(ash, mMetal, mul(MatrixScale(0.98f, 0.5f, 0.98f), T(ASHTRAY_POS.x, ASHTRAY_POS.y, ASHTRAY_POS.z)));
    b.clear();
    b.cylinder({0, 0, 0}, 0.0042f, 0.07f, 12);
    D.add(D.keep(b.build()), mPaper, mul(MatrixRotateZ(-75.f * DEG2RAD), T(ASHTRAY_POS.x - 0.02f, ASHTRAY_POS.y + 0.02f, ASHTRAY_POS.z)));
    b.clear();
    b.sphere({0, 0, 0}, 0.0045f, 8, 12);
    Mesh* ember = D.keep(b.build());
    Vector3 emberPos{ASHTRAY_POS.x + 0.048f, ASHTRAY_POS.y + 0.038f, ASHTRAY_POS.z};
    D.add(ember, mEmber, T(emberPos.x, emberPos.y, emberPos.z), 0);
    D.emitters.push_back({emberPos, SmokeParams{{0.0f, 0.07f, 0}, 0.006f, 0.08f, 3.8f, 0.3f, {196, 202, 214, 255}, 0.45f, 0.03f}});

    // ---------------------------------------------------------------- pendant lamp over our table
    Mesh* shade = D.keep(genLathe({{0.21f, -0.10f}, {0.195f, -0.085f}, {0.13f, -0.02f}, {0.06f, 0.03f}, {0.028f, 0.05f}, {0.02f, 0.07f}}, 48, false, false));
    b.clear();
    b.lathe({{0.0f, -0.075f}, {0.03f, -0.065f}, {0.036f, -0.04f}, {0.03f, -0.015f}, {0.014f, 0.0f}, {0.014f, 0.02f}}, 28, false, true);
    Mesh* bulb = D.keep(b.build());
    b.clear();
    b.cylinder({0, 0, 0}, 0.004f, CEILING_Y - TABLE_LAMP.y - 0.06f, 8);
    Mesh* cord = D.keep(b.build());
    auto lamp = [&](Vector3 p) {
        D.add(shade, mShadeOut, T(p.x, p.y + 0.08f, p.z), CastShadow);
        D.add(bulb, mBulb, T(p.x, p.y + 0.06f, p.z), 0);
        D.add(cord, mCord, T(p.x, p.y + 0.14f, p.z), 0);
    };
    lamp(TABLE_LAMP);
    // inner white enamel as a separate slightly smaller shell so the inside reads bright
    D.add(shade, mShadeIn, mul(MatrixScale(0.985f, 0.985f, 0.985f), T(TABLE_LAMP.x, TABLE_LAMP.y + 0.078f, TABLE_LAMP.z)), DoubleSided);

    // ---------------------------------------------------------------- background tables, chairs, lamps
    b.clear();
    b.roundedBox({0, BG_TABLE_Y - 0.02f, 0}, {0.9f, 0.04f, 0.9f}, 0.012f, 3);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.roundedBox({sx * 0.38f, (BG_TABLE_Y - 0.04f) * 0.5f, sz * 0.38f}, {0.05f, BG_TABLE_Y - 0.04f, 0.05f}, 0.01f, 2);
    Mesh* bgTable = D.keep(b.build());
    b.clear();
    b.roundedBox({0, CHAIR_SEAT_Y - 0.02f, 0}, {0.42f, 0.04f, 0.42f}, 0.012f, 2);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.roundedBox({sx * 0.18f, (CHAIR_SEAT_Y - 0.04f) * 0.5f, sz * 0.18f}, {0.035f, CHAIR_SEAT_Y - 0.04f, 0.035f}, 0.008f, 2);
    b.roundedBox({0, CHAIR_SEAT_Y + 0.26f, 0.19f}, {0.40f, 0.34f, 0.03f}, 0.01f, 2);
    Mesh* chair = D.keep(b.build());
    for (const BgTable& t : BG_TABLES) {
        D.add(bgTable, mWood, T(t.x, 0, t.z));
        for (int s = 0; s < 4; ++s) {
            Vector3 p{t.x + SEAT_DIR[s].x * BG_CHAIR_DIST, 0, t.z + SEAT_DIR[s].z * BG_CHAIR_DIST};
            D.add(chair, mWood, trsYaw(p, seatYawDeg(s)));
        }
        D.add(glass, mGlass, T(t.x + 0.15f, BG_TABLE_Y + 0.006f, t.z + 0.1f), Transparent | DoubleSided);
        D.add(tea, mTea, T(t.x + 0.15f, BG_TABLE_Y + 0.004f, t.z + 0.1f), CastShadow | Transparent);
        lamp({t.x, 1.93f, t.z});
    }
    for (int s = 1; s < 4; ++s) D.add(chair, mWood, trsYaw(seatPos(s), seatYawDeg(s)));
    // shelves with bottles behind the counter (adds realistic submission count)
    b.clear();
    b.roundedBox({0, 0.5f, 0}, {1.6f, 1.0f, 0.5f}, 0.02f, 2);
    D.add(D.keep(b.build()), mWood, T(COUNTER_POS.x, 0, COUNTER_POS.z - 0.1f));
    Mesh* bottle = D.keep(genLathe({{0.0f, 0.0f}, {0.03f, 0.0f}, {0.032f, 0.01f}, {0.032f, 0.16f}, {0.014f, 0.21f}, {0.012f, 0.26f}}, 20, false, true));
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 18; ++i)
            D.add(bottle, mBottle, T(COUNTER_POS.x - 0.8f + i * 0.09f, 1.35f + row * 0.4f, ROOM_Z0 + 0.14f), Transparent);

    // ---------------------------------------------------------------- seated figures (seats 1..3) + bg patrons
    b.clear();
    b.ellipsoid({0, 0.86f, 0.02f}, {0.22f, 0.30f, 0.14f}, 14, 20);           // torso
    b.capsule({-0.18f, 1.02f, 0.0f}, {-0.22f, 0.86f, -0.18f}, 0.05f, 12);   // upper arms
    b.capsule({0.18f, 1.02f, 0.0f}, {0.22f, 0.86f, -0.18f}, 0.05f, 12);
    b.capsule({-0.22f, 0.86f, -0.18f}, {-0.12f, 0.80f, -0.38f}, 0.042f, 12); // forearms onto the table edge
    b.capsule({0.22f, 0.86f, -0.18f}, {0.12f, 0.80f, -0.38f}, 0.042f, 12);
    Mesh* body = D.keep(b.build());
    b.clear();
    b.sphere({0, 0, 0}, 0.105f, 16, 24);
    b.ellipsoid({0, -0.01f, -0.1f}, {0.022f, 0.03f, 0.025f}, 8, 10);  // nose
    b.ellipsoid({-0.1f, 0, 0}, {0.015f, 0.03f, 0.02f}, 6, 8);         // ears
    b.ellipsoid({0.1f, 0, 0}, {0.015f, 0.03f, 0.02f}, 6, 8);
    b.sphere({-0.14f, 0.80f - 1.26f, -0.40f}, 0.045f, 10, 14);        // hands (relative to head)
    b.sphere({0.14f, 0.80f - 1.26f, -0.40f}, 0.045f, 10, 14);
    b.capsule({0, -0.1f, 0.02f}, {0, -0.16f, 0.03f}, 0.055f, 10);    // neck
    Mesh* head = D.keep(b.build());
    b.clear();
    b.ellipsoid({0, -0.045f, -0.085f}, {0.055f, 0.014f, 0.02f}, 6, 12);
    Mesh* must = D.keep(b.build());
    b.clear();
    b.lathe({{0.0f, 0.0f}, {0.112f, 0.0f}, {0.11f, 0.04f}, {0.085f, 0.07f}, {0.0f, 0.075f}}, 28, true, false);
    b.setTransform(T(0, 0.0f, -0.1f));
    b.ellipsoid({0, 0.0f, 0}, {0.1f, 0.008f, 0.07f}, 6, 16);   // peak
    b.resetTransform();
    Mesh* cap = D.keep(b.build());
    b.clear();
    b.ellipsoid({0, 0.02f, 0.01f}, {0.108f, 0.09f, 0.108f}, 10, 20);
    Mesh* hair = D.keep(b.build());
    struct Person {
        int seat;
        const Mat* cloth;
        bool bald, capOn, whiteHair;
    };
    Person people[3] = {{1, mVest, false, true, false}, {2, mShirt, true, false, false}, {3, mCardigan, false, false, true}};
    for (const Person& p : people) {
        Vector3 sp = seatPos(p.seat);
        float yaw = seatYawDeg(p.seat);
        D.add(body, p.cloth, trsYaw(sp, yaw));
        Matrix hm = trsYaw({sp.x, 1.26f, sp.z}, yaw);
        D.add(head, mSkin, hm);
        D.add(must, p.whiteHair ? mHair : mMust, hm);
        if (p.capOn) D.add(cap, mCap, mul(T(0, 0.05f, 0), hm));
        if (p.whiteHair) D.add(hair, mHair, mul(MatrixScale(1.0f, 0.9f, 1.0f), mul(T(0, 0.035f, 0.01f), hm)));
    }
    for (int k = 0; k < 4; ++k) {  // background patrons
        const BgTable& t = BG_TABLES[k];
        for (int s = 0; s < 4; ++s) {
            if (!(t.sides & (1u << s))) continue;
            Vector3 p{t.x + SEAT_DIR[s].x * BG_CHAIR_DIST, 0, t.z + SEAT_DIR[s].z * BG_CHAIR_DIST};
            D.add(body, (s + k) % 2 ? mShirt : mVest, trsYaw(p, seatYawDeg(s)));
            D.add(head, mSkin, trsYaw({p.x, 1.26f, p.z}, seatYawDeg(s)));
        }
    }

    // ---------------------------------------------------------------- smoke: Mahmut's cigarette + an exhale
    D.emitters.push_back({{0.12f, 0.98f, -0.62f}, SmokeParams{{0.f, 0.05f, 0.f}, 0.006f, 0.07f, 3.6f, 0.16f, {206, 204, 198, 255}, 1.1f, 0.02f}});
    D.emitters.push_back({{0.02f, 1.2f, -0.72f}, SmokeParams{{0.f, -0.05f, 0.2f}, 0.02f, 0.2f, 3.2f, 0.18f, {214, 212, 206, 255}, 1.3f, 0.025f}});
}

// Lighting and atmosphere mirror Room::Impl::submitLights and Table3D's fill light.
void submitScene(Demo& D, float t) {
    Renderer& R = D.R;
    Ambient a;
    a.sky = Color{96, 80, 64, 255};
    a.ground = Color{44, 34, 28, 255};
    a.intensity = 0.5f;
    R.setAmbient(a);
    Fog f;
    f.color = Color{86, 72, 58, 255};
    f.density = 0.075f;
    f.hazeStart = 1.7f;
    f.hazeTop = CEILING_Y;
    f.hazeDensity = 0.62f;
    R.setFog(f);
    KeyLight k;
    k.position = TABLE_LAMP;
    k.target = {TABLE_LAMP.x, TABLE_Y, TABLE_LAMP.z};
    k.color = Color{255, 204, 148, 255};
    k.intensity = 3.2f * (1.f + 0.012f * std::sin(t * 13.f));
    k.innerDeg = 40.f;
    k.outerDeg = 68.f;
    k.range = 6.f;
    R.setKeyLight(k);
    for (const BgTable& bt : BG_TABLES) R.addPointLight({{bt.x, 1.83f, bt.z}, Color{255, 184, 112, 255}, 1.25f, 4.4f});
    R.addPointLight({{3.05f, 2.11f, -3.02f}, Color{255, 190, 120, 255}, 1.0f, 3.4f});          // counter lamp
    R.addPointLight({{ROOM_X1 - 0.3f, 2.3f, 0.2f}, Color{150, 176, 255, 255}, 0.6f, 3.4f});    // TV
    R.addPointLight({{-3.8f, 1.85f, 0.6f}, Color{116, 146, 210, 255}, 0.55f, 3.8f});           // window
    R.addPointLight({{-3.5f, 0.45f, -2.45f}, Color{255, 128, 56, 255}, 0.7f, 2.3f});          // stove
    R.addPointLight({{0.f, 1.40f, 1.22f}, Color{255, 220, 182, 255}, 0.55f, 1.9f});            // Table3D fill
    R.addPointLight({{-0.40f + 0.048f, TABLE_Y + 0.04f, -0.49f}, Color{255, 110, 40, 255}, 0.05f, 0.5f});  // ember
    for (const Item& it : D.items) R.submit(it.mesh, it.mat, it.xf, it.flags);
    R.submitGlow({TABLE_LAMP.x, TABLE_LAMP.y, TABLE_LAMP.z}, 0.22f, Color{255, 214, 160, 255}, 0.75f);
    for (const BgTable& bt : BG_TABLES) R.submitGlow({bt.x, 1.93f, bt.z}, 0.2f, Color{255, 200, 140, 255}, 0.7f);
    R.submitGlow({-0.40f + 0.048f, TABLE_Y + 0.038f, -0.49f}, 0.02f, Color{255, 120, 50, 255}, 0.9f);
    R.submitGlow({-3.5f, 0.45f, -2.45f}, 0.25f, Color{255, 120, 50, 255}, 0.35f);
}

// Room-like ambient smoke (see Room.cpp ambientPuff/ambientPos): under the ceiling, in lamp cones, over tables.
uint32_t g_rng = 12345u;
float frand(float a, float b) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return a + (b - a) * ((g_rng >> 8) & 0xffffff) / 16777215.f;
}
SmokeParams ambientPuff(int kind) {
    SmokeParams p;
    p.color = Color{200, 194, 184, 255};
    if (kind == 0) {
        p.velocity = {frand(-0.015f, 0.015f), frand(-0.004f, 0.006f), frand(-0.015f, 0.015f)};
        p.size0 = frand(0.5f, 0.9f);
        p.size1 = frand(1.3f, 2.1f);
        p.life = frand(16.f, 26.f);
        p.alpha = frand(0.05f, 0.09f);
        p.turbulence = 0.5f;
        p.buoyancy = 0.f;
    } else if (kind == 1) {
        p.velocity = {frand(-0.02f, 0.02f), frand(0.0f, 0.02f), frand(-0.02f, 0.02f)};
        p.size0 = frand(0.25f, 0.4f);
        p.size1 = frand(0.8f, 1.2f);
        p.life = frand(10.f, 15.f);
        p.alpha = frand(0.06f, 0.1f);
        p.turbulence = 0.7f;
        p.buoyancy = 0.004f;
    } else {
        p.velocity = {frand(-0.02f, 0.02f), frand(0.02f, 0.05f), frand(-0.02f, 0.02f)};
        p.size0 = frand(0.15f, 0.25f);
        p.size1 = frand(0.6f, 0.9f);
        p.life = frand(8.f, 12.f);
        p.alpha = frand(0.05f, 0.08f);
        p.turbulence = 0.9f;
        p.buoyancy = 0.01f;
    }
    return p;
}
Vector3 ambientPos(int kind) {
    for (int tries = 0; tries < 8; ++tries) {
        Vector3 p;
        if (kind == 0) {
            p = {frand(ROOM_X0 + 0.3f, ROOM_X1 - 0.3f), frand(2.05f, 2.9f), frand(ROOM_Z0 + 0.3f, ROOM_Z1 - 0.3f)};
        } else if (kind == 1) {
            const BgTable& t = BG_TABLES[(int)frand(0.f, 3.99f)];
            p = {t.x + frand(-0.4f, 0.4f), 1.93f - frand(0.05f, 0.55f), t.z + frand(-0.4f, 0.4f)};
        } else {
            const BgTable& t = BG_TABLES[(int)frand(0.f, 3.99f)];
            p = {t.x + frand(-0.5f, 0.5f), frand(1.25f, 1.65f), t.z + frand(-0.5f, 0.5f)};
        }
        if (std::fabs(p.x) < 1.0f && std::fabs(p.z) < 1.0f && p.y < 1.75f) continue;
        return p;
    }
    return {2.f, 2.6f, 2.f};
}
float g_ambientAcc = 0.f;
void ambientSmoke(Demo& D, float dt) {
    g_ambientAcc += dt * 5.5f;
    while (g_ambientAcc >= 1.f) {
        g_ambientAcc -= 1.f;
        float r = frand(0.f, 1.f);
        int kind = r < 0.55f ? 0 : (r < 0.82f ? 1 : 2);
        D.R.emitSmoke(ambientPos(kind), ambientPuff(kind));
    }
}
void warmUpSmoke(Demo& D) {
    for (int i = 0; i < 90; ++i) {
        float r = frand(0.f, 1.f);
        int kind = r < 0.6f ? 0 : (r < 0.85f ? 1 : 2);
        SmokeParams sp = ambientPuff(kind);
        float sz = sp.size0 + (sp.size1 - sp.size0) * frand(0.3f, 0.9f);
        sp.size0 = sz;
        sp.size1 = sz * 1.15f;
        sp.life *= frand(0.35f, 1.f);
        D.R.emitSmoke(ambientPos(kind), sp);
    }
}

void simulate(Demo& D, float seconds, float& t) {
    const float dt = 1.f / 60.f;
    if (D.emitAcc.size() != D.emitters.size()) D.emitAcc.assign(D.emitters.size(), 0.f);
    for (float s = 0; s < seconds; s += dt) {
        t += dt;
        ambientSmoke(D, dt);
        for (size_t i = 0; i < D.emitters.size(); ++i) {
            const SmokeParams& sp = D.emitters[i].second;
            float rate = sp.size1 <= 0.12f ? 7.f : 1.2f;
            D.emitAcc[i] += rate * dt;
            while (D.emitAcc[i] >= 1.f) {
                D.emitAcc[i] -= 1.f;
                D.R.emitSmoke(D.emitters[i].first, sp);
            }
        }
        D.R.update(dt);
    }
}

Camera3D lookCam(Vector3 pos, Vector3 target, float fov) {
    Camera3D c{};
    c.position = pos;
    c.target = target;
    c.up = {0, 1, 0};
    c.fovy = fov;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}
Camera3D seatCam() {
    float p = EYE_PITCH_DEG * DEG2RAD;
    return lookCam(EYE, {EYE.x, EYE.y + std::sin(p), EYE.z - std::cos(p)}, FOVY_DEG);
}

void exportRT(RenderTexture2D& rt, const char* path) {
    Image im = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&im);
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    ExportImage(im, path);
    UnloadImage(im);
}

// ---------------------------------------------------------------- MeshBuilder winding test
struct PrimResult {
    const char* name;
    int tris, bad;
};
PrimResult checkMesh(const char* name, const Mesh& m) {
    PrimResult r{name, m.triangleCount, 0};
    for (int t = 0; t < m.triangleCount; ++t) {
        int ia = m.indices ? m.indices[t * 3] : t * 3, ib = m.indices ? m.indices[t * 3 + 1] : t * 3 + 1,
            ic = m.indices ? m.indices[t * 3 + 2] : t * 3 + 2;
        Vector3 a{m.vertices[ia * 3], m.vertices[ia * 3 + 1], m.vertices[ia * 3 + 2]};
        Vector3 b{m.vertices[ib * 3], m.vertices[ib * 3 + 1], m.vertices[ib * 3 + 2]};
        Vector3 c{m.vertices[ic * 3], m.vertices[ic * 3 + 1], m.vertices[ic * 3 + 2]};
        Vector3 fn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (Vector3Length(fn) < 1e-5f * Vector3Length(Vector3Subtract(b, a)) * Vector3Length(Vector3Subtract(c, a)) + 1e-14f)
            continue;  // degenerate (zero area up to float error)
        Vector3 vn{m.normals[ia * 3] + m.normals[ib * 3] + m.normals[ic * 3],
                   m.normals[ia * 3 + 1] + m.normals[ib * 3 + 1] + m.normals[ic * 3 + 1],
                   m.normals[ia * 3 + 2] + m.normals[ib * 3 + 2] + m.normals[ic * 3 + 2]};
        if (Vector3DotProduct(fn, vn) <= 0.f) ++r.bad;
    }
    return r;
}

void meshTest(Renderer& R, RenderTexture2D& rt) {
    struct P {
        const char* name;
        Mesh mesh;
    };
    std::vector<P> prims;
    MeshBuilder b;
    auto one = [&](const char* n, auto fn) {
        b.clear();
        b.resetTransform();
        fn(b);
        prims.push_back({n, b.build()});
    };
    one("box", [](MeshBuilder& m) { m.box({0, 0.15f, 0}, {0.3f, 0.3f, 0.2f}); });
    one("roundedBox", [](MeshBuilder& m) { m.roundedBox({0, 0.15f, 0}, {0.3f, 0.3f, 0.2f}, 0.06f, 4); });
    one("roundedBox r=h", [](MeshBuilder& m) { m.roundedBox({0, 0.15f, 0}, {0.3f, 0.3f, 0.3f}, 0.15f, 5); });
    one("lathe cup caps", [](MeshBuilder& m) { m.lathe({{0.12f, 0.0f}, {0.15f, 0.12f}, {0.1f, 0.25f}, {0.13f, 0.3f}}, 32, true, true); });
    one("lathe open glass", [](MeshBuilder& m) { m.lathe({{0.0f, 0.0f}, {0.1f, 0.0f}, {0.13f, 0.08f}, {0.09f, 0.18f}, {0.12f, 0.3f}}, 32, false, false); });
    one("cylinder", [](MeshBuilder& m) { m.cylinder({0, 0, 0}, 0.12f, 0.3f, 24); });
    one("sphere", [](MeshBuilder& m) { m.sphere({0, 0.15f, 0}, 0.15f, 12, 20); });
    one("ellipsoid", [](MeshBuilder& m) { m.ellipsoid({0, 0.15f, 0}, {0.18f, 0.12f, 0.1f}, 12, 20); });
    one("capsule tilted", [](MeshBuilder& m) { m.capsule({-0.1f, 0.05f, 0}, {0.12f, 0.28f, 0.05f}, 0.07f, 16); });
    one("tube open (caps)", [](MeshBuilder& m) {
        m.tube({{-0.15f, 0.05f, 0}, {-0.05f, 0.25f, 0.05f}, {0.08f, 0.2f, -0.05f}, {0.15f, 0.05f, 0}}, 0.04f, 14);
    });
    one("tube closed ring", [](MeshBuilder& m) {
        std::vector<Vector3> ring;
        for (int i = 0; i <= 24; ++i) ring.push_back({0.13f * std::cos(i * 6.2832f / 24), 0.15f + 0.13f * std::sin(i * 6.2832f / 24), 0});
        ring.back() = ring.front();
        m.tube(ring, 0.03f, 12);
    });
    one("plane", [](MeshBuilder& m) { m.plane({0, 0.15f, 0}, {0.3f, 0.3f}, Vector3Normalize({0.3f, 0.2f, 1.f})); });
    one("mirrored rbox", [](MeshBuilder& m) {
        m.setTransform(MatrixMultiply(MatrixScale(-1, 1, 1), MatrixTranslate(0, 0, 0)));
        m.roundedBox({0.02f, 0.15f, 0}, {0.3f, 0.3f, 0.2f}, 0.06f, 3);
        m.cylinder({0.1f, 0.0f, 0.1f}, 0.04f, 0.1f, 12);
    });
    one("transformed capsule", [](MeshBuilder& m) {
        m.setTransform(MatrixMultiply(MatrixRotateZ(0.6f), MatrixTranslate(0, 0.1f, 0)));
        m.capsule({0, 0, 0}, {0, 0.2f, 0}, 0.06f, 14);
    });
    prims.push_back({"genRoundedBox", genRoundedBox(0.3f, 0.2f, 0.25f, 0.05f, 3)});
    prims.push_back({"genCanvasQuad", genCanvasQuad(0.3f, 0.25f)});

    int bad = 0;
    for (const P& p : prims) {
        PrimResult r = checkMesh(p.name, p.mesh);
        bad += r.bad;
        std::printf("  mesh %-20s tris %5d  inverted %d\n", r.name, r.tris, r.bad);
    }
    std::printf("MESH CHECK: %s (%d inverted triangles)\n", bad == 0 ? "OK" : "FAIL", bad);

    Mat mat = R.makeMat(Color{200, 170, 130, 255}, genWoodTexture(256, Color{200, 160, 110, 255}, Color{120, 80, 50, 255}, 2, 6), 0.4f, 40.f);
    Mat floorM = R.makeMat(Color{70, 90, 80, 255});
    MeshBuilder fb;
    fb.plane({0, 0, 0}, {6, 6}, {0, 1, 0});
    Mesh floor = fb.build();
    Ambient a;
    a.intensity = 0.8f;
    R.setAmbient(a);
    Fog f;
    f.density = 0.f;
    f.hazeDensity = 0.f;
    R.setFog(f);
    KeyLight k;
    k.position = {0.6f, 2.6f, 1.6f};
    k.target = {0, 0, 0};
    k.intensity = 5.f;
    k.innerDeg = 50;
    k.outerDeg = 70;
    R.setKeyLight(k);
    R.addPointLight({{-2, 1.5f, 1.5f}, Color{150, 180, 255, 255}, 1.5f, 6.f});
    R.submit(&floor, &floorM, MatrixIdentity(), 0);
    int cols = 6;
    for (int i = 0; i < (int)prims.size(); ++i) {
        float x = (i % cols - (cols - 1) * 0.5f) * 0.42f, z = (i / cols - 1) * 0.5f;
        R.submit(&prims[i].mesh, &mat, MatrixMultiply(MatrixRotateY(0.5f), MatrixTranslate(x, 0.001f, z)));
    }
    Camera3D cam = lookCam({0.2f, 1.35f, 1.75f}, {0, 0.1f, -0.05f}, 50);
    R.render(cam, &rt, Color{30, 30, 34, 255});
    exportRT(rt, "build/gfx/meshes.png");
    for (P& p : prims) UnloadMesh(p.mesh);
    UnloadMesh(floor);
    UnloadTexture(mat.material.maps[0].texture);
    R.unloadMat(mat);
    R.unloadMat(floorM);
}

void textureSheet(RenderTexture2D& rt, Renderer& R) {
    std::vector<std::pair<const char*, Texture2D>> tx = {
        {"wood", genWoodTexture(512, Color{150, 96, 56, 255}, Color{74, 40, 20, 255}, 5, 14)},
        {"floor wood", genWoodTexture(512, Color{104, 70, 44, 255}, Color{48, 30, 18, 255}, 21, 8)},
        {"felt", genFeltTexture(512, ui::pal::Felt, 3)},
        {"plaster", genPlasterTexture(512, Color{176, 146, 96, 255}, 9)},
        {"noise", genNoiseTexture(256, Color{120, 128, 150, 255}, Color{80, 86, 104, 255}, 6, 44)},
    };
    BeginTextureMode(rt);
    ClearBackground(Color{40, 40, 44, 255});
    const float cell = 300.f;
    for (int i = 0; i < (int)tx.size(); ++i) {
        float x0 = 10 + i * (cell + 18), y0 = 40;
        // 2x2 tiling to check seams
        DrawTexturePro(tx[i].second, {0, 0, (float)tx[i].second.width * 2, (float)tx[i].second.height * 2}, {x0, y0, cell, cell}, {0, 0}, 0, WHITE);
        DrawText(tx[i].first, (int)x0, 12, 20, RAYWHITE);
        // a 1:1 crop of the texture centre
        DrawTexturePro(tx[i].second, {128, 128, 256, 256}, {x0, y0 + cell + 30, cell, cell}, {0, 0}, 0, WHITE);
    }
    Texture2D sprites[3] = {R.puffTexture(), R.glowTexture(), R.whiteTexture()};
    for (int i = 0; i < 2; ++i) {
        DrawRectangle(10 + i * 170, 690, 160, 160, Color{20, 20, 24, 255});
        DrawTexturePro(sprites[i], {0, 0, (float)sprites[i].width, (float)sprites[i].height}, {10.f + i * 170, 690, 160, 160}, {0, 0}, 0, WHITE);
    }
    EndTextureMode();
    exportRT(rt, "build/gfx/textures.png");
    for (auto& t : tx) UnloadTexture(t.second);
}

// Projects a few points and re-picks them: returns the worst distance (m) between a point and its ray.
float pickRoundTrip(const Renderer& R, const std::vector<Vector3>& pts) {
    float worst = 0.f;
    for (Vector3 p : pts) {
        Vector2 v;
        if (!R.projectToVirtual(p, v)) continue;
        Ray ray = R.rayFromVirtual(v);
        Vector3 d = Vector3Subtract(p, ray.position);
        float along = Vector3DotProduct(d, ray.direction);
        float dist = Vector3Length(Vector3Subtract(d, Vector3Scale(ray.direction, along)));
        worst = std::max(worst, dist);
    }
    return worst;
}

// Brightest-pixel check: renders a bright marker and compares its pixel with projectToVirtual.
float markerError(Renderer& R, Demo& D, RenderTexture2D* rt, const Camera3D& cam, Vector3 at, bool window) {
    static Mesh ball{};
    static Mat* white = nullptr;
    if (!white) {
        MeshBuilder b;
        b.sphere({0, 0, 0}, 0.004f, 8, 12);
        ball = b.build();
        white = D.mat(Color{255, 255, 255, 255}, {}, 0.f, 8.f, 1.f);
    }
    R.setAmbient(Ambient{{0, 0, 0, 255}, {0, 0, 0, 255}, 0.f});
    Fog f;
    f.density = 0;
    f.hazeDensity = 0;
    R.setFog(f);
    KeyLight k;
    k.intensity = 0;
    R.setKeyLight(k);
    R.submit(&ball, white, MatrixTranslate(at.x, at.y, at.z), 0);
    Image im;
    if (window) {
        BeginDrawing();
        ClearBackground(BLACK);
        R.render(cam);
        im = LoadImageFromScreen();
        EndDrawing();
    } else {
        R.render(cam, rt, BLACK);
        im = LoadImageFromTexture(rt->texture);
        ImageFlipVertical(&im);
    }
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* px = (Color*)im.data;
    double sx = 0, sy = 0, sw = 0;
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x) {
            Color c = px[y * im.width + x];
            float l = (c.r + c.g + c.b) / 765.f;
            if (l > 0.5f) {
                sx += (x + 0.5) * l;
                sy += (y + 0.5) * l;
                sw += l;
            }
        }
    int iw = im.width;
    UnloadImage(im);
    if (sw <= 0) return 1e9f;
    // pixel -> points (HiDPI framebuffers are larger than the window) -> virtual
    float pxScale = (float)iw / (float)R.renderWidth();
    Vector2 screen{(float)(sx / sw) / pxScale, (float)(sy / sw) / pxScale};
    const ui::Viewport& vp = ui::currentViewport();
    Vector2 virt{(screen.x - vp.offset.x) / vp.scale, (screen.y - vp.offset.y) / vp.scale};
    Vector2 proj;
    if (!R.projectToVirtual(at, proj)) return 1e9f;
    return Vector2Distance(virt, proj);
}

} // namespace

int main(int argc, char** argv) {
    bool doWindow = true, probe = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-window") doWindow = false;
        if (std::string(argv[i]) == "--probe") probe = true;
    }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "gfx demo");  // must fit the display; snapshots use 1600x900 render textures
    ui::loadFonts();
    ui::tilegfx::init();
    Demo D;
    if (!D.R.init()) {
        std::printf("renderer init failed\n");
        return 1;
    }
    RenderTexture2D rt = LoadRenderTexture(W, H);
    D.R.setRenderSize(W, H);
    ui::setCurrentViewport(ui::Viewport{});

    auto t0 = std::chrono::steady_clock::now();
    buildScene(D, ui::tilegfx::ready());
    float tBuild = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    float t = 0.f;
    warmUpSmoke(D);
    simulate(D, 24.f, t);
    std::printf("scene: %zu submissions, %d particles, build %.0f ms\n", D.items.size(), D.R.particleCount(), tBuild);

    struct View {
        const char* file;
        Camera3D cam;
    };
    std::vector<View> views = {
        {"build/gfx/seat.png", seatCam()},
        {"build/gfx/close_felt.png", lookCam({0.02f, TABLE_Y + 0.13f, 0.43f}, {-0.07f, TABLE_Y, 0.24f}, 42)},
        {"build/gfx/close_rack.png", lookCam({0.13f, 0.94f, 0.74f}, {0.02f, TABLE_Y + 0.04f, 0.47f}, 38)},
        {"build/gfx/back.png", lookCam({0.35f, 1.5f, -1.75f}, {0.f, 0.85f, 0.35f}, 60)},
        {"build/gfx/corner.png", lookCam({3.7f, 2.3f, 3.25f}, {-0.6f, 1.0f, -1.2f}, 62)},
        {"build/gfx/lamp.png", lookCam(EYE, {0.f, 1.75f, 0.f}, 66)},
        {"build/gfx/close_nuri.png", lookCam({-0.15f, 1.15f, 0.25f}, {-0.88f, 1.15f, 0.f}, 40)},
    };
    for (const View& v : views) {
        submitScene(D, t);
        D.R.render(v.cam, &rt, Color{12, 9, 7, 255});
        exportRT(rt, v.file);
        std::printf("wrote %s\n", v.file);
    }

    // ---- projection round trips (target path), including a letterboxed viewport
    {
        std::vector<Vector3> pts = {PILE_POS, INDICATOR_POS, DISCARD_POS[0], DISCARD_POS[3], GLASS_POS[0], {0.2f, 0.9f, -0.5f}};
        submitScene(D, t);
        D.R.render(seatCam(), &rt, BLACK);
        float e1 = pickRoundTrip(D.R, pts);
        ui::setCurrentViewport(ui::Viewport{0.8f, {40.f, 30.f}});
        float e2 = pickRoundTrip(D.R, pts);
        ui::setCurrentViewport(ui::Viewport{});
        float m1 = markerError(D.R, D, &rt, seatCam(), DISCARD_POS[0], false);
        float m2 = markerError(D.R, D, &rt, lookCam({0.5f, 1.4f, 0.9f}, {-0.1f, 0.8f, 0.f}, 50), GLASS_POS[3], false);
        std::printf("PICK target: ray miss %.2e m (viewport 1) %.2e m (letterboxed); marker %.2f / %.2f px\n", e1, e2, m1, m2);
    }

    // ---- MeshBuilder + textures
    meshTest(D.R, rt);
    textureSheet(rt, D.R);

    // ---- performance: ~600 submissions + ~1000 particles (the budget case)
    {
        std::vector<Item> extra;
        std::vector<const Item*> tileItems;
        for (const Item& it : D.items)
            if (it.mat == D.tileMat) tileItems.push_back(&it);
        // pad up to 600 submissions with distinct tiles lying on the background tables (a busy evening)
        for (int i = 0; !tileItems.empty() && (int)(D.items.size() + extra.size()) < 600; ++i) {
            const BgTable& bt = BG_TABLES[i % 4];
            int k = i / 4;
            float x = bt.x - 0.4f + 0.034f * (float)(k % 24), z = bt.z - 0.38f + 0.05f * (float)(k / 24);
            Matrix m = mul(MatrixRotateX(-90.f * DEG2RAD), T(x, BG_TABLE_Y + TILE_T * 0.5f, z));
            extra.push_back({tileItems[(size_t)i % tileItems.size()]->mesh, D.tileMat, m, CastShadow});
        }
        std::vector<Item> saved = D.items;
        D.items.insert(D.items.end(), extra.begin(), extra.end());
        // a smoky evening: fill up to ~1000 live particles with ambient puffs, cigarette threads and steam
        while (D.R.particleCount() < 1150) {
            int kind = (int)frand(0.f, 2.99f);
            SmokeParams sp = ambientPuff(kind);
            sp.life *= 4.f;
            D.R.emitSmoke(ambientPos(kind), sp);
            SmokeParams thread = D.emitters.empty() ? sp : D.emitters[(size_t)frand(0.f, D.emitters.size() - 0.01f)].second;
            thread.life *= 4.f;
            D.R.emitSmoke({frand(-0.5f, 0.5f), TABLE_Y + frand(0.05f, 0.5f), frand(-0.5f, 0.5f)}, thread);
        }
        Camera3D cam = seatCam();
        const int N = 120;
        // (1) CPU time spent inside render() and GPU time per frame (timer query, frames serialised)
        double cpu = 0, cpuMax = 0, gpu = 0;
        GLuint q = 0;
        glGenQueries(1, &q);
        for (int f = 0; f < N; ++f) {
            D.R.update(1.f / 60.f);
            submitScene(D, t);
            auto a = std::chrono::steady_clock::now();
            glBeginQuery(GL_TIME_ELAPSED, q);
            D.R.render(cam, &rt, BLACK);
            glEndQuery(GL_TIME_ELAPSED);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            GLuint64 ns = 0;
            glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
            gpu += ns / 1e6;
            cpu += ms;
            cpuMax = std::max(cpuMax, ms);
        }
        glDeleteQueries(1, &q);
        // (2) throughput: frames back to back (CPU and GPU overlap, clocks stay up), i.e. the real frame cost
        glFinish();
        auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < N; ++f) {
            D.R.update(1.f / 60.f);
            submitScene(D, t);
            D.R.render(cam, &rt, BLACK);
        }
        glFinish();
        double thr = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / N;
        std::printf("PERF: %zu submissions, %d particles: render() CPU %.2f ms avg (max %.2f), GPU %.2f ms "
                    "(serialised), throughput %.2f ms/frame (1600x900 target)\n",
                    D.items.size(), D.R.particleCount(), cpu / N, cpuMax, gpu / N, thr);
        auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 60; ++i) D.R.update(1.f / 60.f);
        double up = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count() / 60.0;
        std::printf("PERF: update() %.3f ms with %d particles\n", up, D.R.particleCount());
        D.items = saved;
    }

    if (probe) {
        GLuint q = 0;
        glGenQueries(1, &q);
        auto measure = [&](const char* name, auto tweak) {
            // back-to-back frames keep the GPU clocked up; throughput = max(CPU, GPU) per frame
            const int N = 150;
            double cpu = 0;
            for (int f = 0; f < 20; ++f) {
                submitScene(D, t);
                tweak();
                D.R.render(seatCam(), &rt, BLACK);
            }
            glFinish();
            auto t0 = std::chrono::steady_clock::now();
            for (int f = 0; f < N; ++f) {
                D.R.update(1.f / 60.f);
                submitScene(D, t);
                tweak();
                auto a = std::chrono::steady_clock::now();
                D.R.render(seatCam(), &rt, BLACK);
                cpu += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            }
            glFinish();
            double tot = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("PROBE %-28s render() CPU %.2f ms   frame throughput %.2f ms\n", name, cpu / N, tot / N);
        };
        for (int div : {10, 4, 2}) {
            RenderTexture2D small = LoadRenderTexture(W / div, H / div);
            std::swap(small, rt);
            D.R.setRenderSize(W / div, H / div);
            char name[64];
            std::snprintf(name, sizeof name, "full @%dx%d", W / div, H / div);
            measure(name, [] {});
            D.R.setRenderSize(W, H);
            std::swap(small, rt);
            UnloadRenderTexture(small);
        }
        measure("full", [] {});
        measure("no point lights", [&] { D.R.setAmbient(Ambient{}); });
        measure("no shadows", [&] { KeyLight k; k.shadows = false; D.R.setKeyLight(k); });
        measure("no fog", [&] { Fog f; f.density = 0; f.hazeDensity = 0; D.R.setFog(f); });
        std::vector<Item> saved = D.items;
        D.items.clear();
        measure("no meshes (particles only)", [] {});
        D.items = saved;
        glDeleteQueries(1, &q);
    }

    // ---- the window framebuffer path (MSAA 4x default framebuffer)
    if (doWindow) {
        D.R.setRenderSize(GetScreenWidth(), GetScreenHeight());
        ui::setCurrentViewport(ui::computeViewport());
        simulate(D, 1.f, t);
        submitScene(D, t);
        BeginDrawing();
        ClearBackground(Color{12, 9, 7, 255});
        D.R.render(seatCam());
        Image im = LoadImageFromScreen();
        EndDrawing();
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        ExportImage(im, "build/gfx/window.png");
        std::printf("wrote build/gfx/window.png (%dx%d, screen %dx%d)\n", im.width, im.height, GetScreenWidth(), GetScreenHeight());
        UnloadImage(im);
        std::vector<Vector3> pts = {PILE_POS, INDICATOR_POS, DISCARD_POS[0], GLASS_POS[2]};
        float e = pickRoundTrip(D.R, pts);
        float m = markerError(D.R, D, nullptr, seatCam(), INDICATOR_POS, true);
        // timing in the window path
        double total = 0;
        for (int f = 0; f < 30; ++f) {
            simulate(D, 1.f / 60.f, t);
            submitScene(D, t);
            glFinish();
            auto a = std::chrono::steady_clock::now();
            BeginDrawing();
            ClearBackground(BLACK);
            D.R.render(seatCam());
            glFinish();
            EndDrawing();
            total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        }
        std::printf("PICK window: ray miss %.2e m, marker %.2f px; window frame %.2f ms avg\n", e, m, total / 30);
    }

    UnloadRenderTexture(rt);
    for (Mat* m : D.mats) {
        D.R.unloadMat(*m);
        delete m;
    }
    for (Mesh* m : D.meshes) {
        UnloadMesh(*m);
        delete m;
    }
    for (Texture2D& tx : D.textures) UnloadTexture(tx);
    D.R.shutdown();
    ui::tilegfx::shutdown();
    ui::unloadFonts();
    CloseWindow();
    return 0;
}
