#pragma once
// Private to the room module (src/r3d/Room*.cpp, room owner). Not part of any public contract.
//
//   Room.cpp       public API, per-frame animation, lights, atmosphere, submission
//   RoomBuild.cpp  all static geometry (architecture, furniture, counter, props, lamps, TV...)
//   RoomTex.cpp    procedural textures (floor, walls, ceiling, glass) and the static canvases
//   RoomCanvas.cpp dynamic canvases (scoreboard, TV football) and the TV match simulation
//   RoomWeather.cpp rainy nights (drops on the glass, rain outside), passers-by on the street
//   RoomCat.cpp    the kahvehane cat: procedural rig, naps, walks, jumps onto an empty chair
#include "core/Rng.h"
#include "r3d/Room.h"
#include "r3d/World.h"
#include "ui/Common.h"

#include <raylib.h>
#include <raymath.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace r3d {
namespace rm {

// ============================================================================ helpers (RoomTex.cpp)
inline Color rgba(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}
Color scaleRgb(Color c, float k);
Color mix(Color a, Color b, float t);
Color alpha(Color c, float a01);
float hash1(uint32_t x);                          // [0,1)
float vnoise(float x, float y, uint32_t seed);    // smooth value noise [0,1]
float fbm(float x, float y, int octaves, uint32_t seed);
float noise1(float t, uint32_t seed);             // smooth 1D noise [0,1]
float smooth01(float e0, float e1, float x);

// Every Room canvas is drawn between these (they wrap beginCanvas / endCanvas): alpha composites "over", so the
// translucent strokes painted onto an opaque panel keep it opaque. (raylib's default blend writes
// dst.a = src.a² + dst.a·(1 − src.a): the chalk, haze and glow layers used to wear the price board, the signs and
// the street view down to 40-80 % opacity, and the wall behind showed through them.)
void beginRoomCanvas(RenderTexture2D& rt);
void endRoomCanvas(RenderTexture2D& rt);
// 2D drawing into canvases (pixel units, y down). Triangles/quads accept any winding.
void tri(Vector2 a, Vector2 b, Vector2 c, Color col);
void quad2(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col);
void ellipse(Vector2 c, float rx, float ry, Color col, int seg = 28);
void ellipseGrad(Vector2 c, float rx, float ry, Color inner, Color outer, int seg = 32);
void rectGradV(Rectangle r, Color top, Color bottom);
void rectGradH(Rectangle r, Color left, Color right);
void frameRect(Rectangle r, float t, Color c);
// Chalk: jittered translucent passes; smudges = erased chalk dust; speckle = board showing through strokes.
void chalkText(ui::FontId f, const std::string& s, Vector2 pos, float size, Color c, okey::Rng& rng);
void chalkSmudges(Rectangle r, Color chalk, okey::Rng& rng, int n);
void chalkSpeckle(Rectangle r, Color board, okey::Rng& rng, int n);

// ============================================================================ geometry helpers (RoomBuild.cpp)
Vector3 wallRight(Vector3 n);  // viewer's right on a wall facing n: cross(up, n)
void quadUV(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector2 t0, Vector2 t1, Vector2 t2,
            Vector2 t3, Color c);
// Box with world-scaled UVs (texScale metres per texture repeat); u follows axis `grain` (0 x, 1 y, 2 z).
void boxW(MeshBuilder& b, Vector3 c, Vector3 size, float texScale, Color col, int grain = 0);
// Rounded box whose grain (texture u) follows its longest axis. `base` = the builder's current transform (if any).
void rbox(MeshBuilder& b, Vector3 c, Vector3 size, float radius, int seg, Color col, const Matrix* base = nullptr);
// Surface of revolution placed at `pos` (yaw about +Y, uniform scale), composed with `base`.
void latheAt(MeshBuilder& b, Vector3 pos, const std::vector<Vector2>& prof, int seg, bool capB, bool capT, Color col,
             const Matrix* base = nullptr, float yawDeg = 0.f, float scale = 1.f);
// Closed ring tube (circle of radius R in the XZ plane around c).
void ringTube(MeshBuilder& b, Vector3 c, float R, float r, int seg, Color col, float rz = -1.f);
// Wall-mounted quad facing the horizontal normal n, centred at c, showing canvas pixel rect src of a cw x ch canvas.
void canvasQuad(MeshBuilder& b, Vector3 c, float w, float h, Vector3 n, Rectangle src, float cw, float ch,
                Color col = WHITE);
// Horizontal quad facing +Y: canvas top edge toward -Z (before yaw about the quad centre).
void canvasQuadUp(MeshBuilder& b, Vector3 c, float w, float d, Rectangle src, float cw, float ch, float yawDeg = 0.f,
                  Color col = WHITE);

// ============================================================================ canvas atlases
constexpr int ART_W = 2048, ART_H = 2048;
namespace art {
constexpr Rectangle PRICE{8, 8, 640, 430};
constexpr Rectangle CLOCK{664, 8, 512, 512};
constexpr Rectangle BOSPHORUS{1192, 8, 560, 380};
constexpr Rectangle CALENDAR{1768, 8, 272, 420};
constexpr Rectangle TEAM{8, 536, 560, 380};
constexpr Rectangle PENNANT{584, 536, 400, 250};
constexpr Rectangle TAVLA{1000, 536, 540, 440};
constexpr Rectangle SIGN_KUMAR{1556, 536, 484, 140};
constexpr Rectangle SIGN_VERESIYE{1556, 692, 484, 140};
constexpr Rectangle SIGN_WC{1556, 848, 200, 120};
constexpr Rectangle SIGN_WELCOME{1772, 848, 268, 120};
constexpr Rectangle TILES{8, 992, 512, 256};
constexpr Rectangle MIRROR{536, 992, 400, 560};
constexpr Rectangle SHIP{952, 992, 480, 340};
constexpr Rectangle NEWS{1448, 992, 300, 400};
constexpr Rectangle RADIO{1764, 992, 276, 140};
constexpr Rectangle TEABOX{1764, 1148, 130, 180};
constexpr Rectangle SODA_LABEL{1910, 1148, 130, 90};
constexpr Rectangle GAZOZ_LABEL{1910, 1244, 130, 90};
constexpr Rectangle DICE{8, 1568, 576, 96};        // six faces, 96 px each (1..6)
constexpr Rectangle CARD_BACK{600, 1568, 96, 140};
constexpr Rectangle CARD_FACE{712, 1568, 96, 140}; // three faces side by side
constexpr Rectangle CERT{1040, 1568, 420, 300};
constexpr Rectangle OLDPHOTO{8, 1680, 420, 300};
constexpr Rectangle CIGPACK{1476, 1568, 120, 170};
constexpr Rectangle SCOREPAD{1612, 1568, 160, 210};
constexpr Rectangle TILEFACE{1788, 1568, 240, 120}; // 4 colour marks for tiny okey tiles
constexpr Rectangle PAPER_TV{444, 1740, 380, 290};  // "tonight's match" poster
constexpr Rectangle PLAIN_WHITE{1788, 1700, 32, 32};
constexpr Rectangle RULES{8, 1256, 504, 296};       // "Kıraathane adabı" house rules, painted board
constexpr Rectangle KILIM{1452, 1396, 580, 160};    // woven cushion cover for the wall bench
constexpr Rectangle TENEKE{956, 1340, 300, 200};    // olive-oil tin label (the rubber plant's pot)
}  // namespace art

constexpr int STREET_W = 2048, STREET_H = 2048;
namespace street {
// Facade across the street seen through the left wall (x = -8.2, z from +8 .. -10, y from -1 .. 7).
constexpr Rectangle LEFT{0, 0, 2048, 900};
// View through the back window (z = -7.4, x from -9 .. 2, y from -1 .. 7).
constexpr Rectangle BACK{0, 912, 2048, 900};
// Wet road / pavement seen from above (outside ground plane).
constexpr Rectangle ROAD{0, 1824, 2048, 224};
}  // namespace street

// Wall atlas rows (image texture, 2048 x 2048): row r = wall r, covers y from WAINSCOT_H to CEILING_Y.
constexpr int WALL_TEX = 2048, WALL_ROW = 512;
constexpr float WAINSCOT_H = 0.9f;  // chair-rail height: the scoreboard's chalk ledge rests on the cap rail

// Walls: 0 back (z0, faces +Z), 1 front (z1, faces -Z), 2 left (x0, faces +X, street), 3 right (x1, faces -X).
struct WallFrame {
    Vector3 origin;  // viewer's left corner on the floor
    Vector3 right;   // along the wall (viewer's right)
    Vector3 normal;  // into the room
    float length;
};
WallFrame wallFrame(int wall);
inline Vector3 wallPoint(int wall, float a, float y, float out = 0.f) {
    WallFrame f = wallFrame(wall);
    return Vector3{f.origin.x + f.right.x * a + f.normal.x * out, y, f.origin.z + f.right.z * a + f.normal.z * out};
}

struct Opening {
    int wall;
    float a0, a1, y0, y1;
    int kind;  // 0 window, 1 door
};
// Decals baked into the wall atlas.
struct WallMark {
    int wall;
    float a0, a1, y0, y1;
    int kind;  // 0 frame shadow (AO halo), 1 ghost (clean patch of a removed picture), 2 soot plume, 3 water stain
};

// ============================================================================ TV match simulation (RoomCanvas.cpp)
struct TvSim {
    struct Player {
        Vector2 home, pos, vel;
        int team;
        bool keeper;
    };
    std::array<Player, 22> pl{};
    Vector2 ball{0.5f, 0.5f}, ballVel{0, 0};
    float ballZ = 0.f, ballVz = 0.f;
    int owner = -1, lastTouchTeam = 0;
    int score[2] = {0, 0};
    float minute = 1.f;
    float kickT = 1.f;
    float goalFlash = 0.f;
    int goalTeam = 0;
    float camX = 0.5f;
    float pauseT = 0.f;      // dead ball (throw-in / kick-off)
    float endT = 0.f;        // "MAÇ SONU" card
    int attackDir[2] = {1, -1};
    bool goalEvent = false;  // set once per goal (consumed by Room)
    float flicker = 1.f;
    float roll = 0.f;
    float brightness = 0.5f; // average screen luminance estimate (drives the TV light)
    okey::Rng rng{7};
    void reset(uint64_t seed);
    void kickoff(int team);
    void step(float dt);
};

void drawTvCanvas(RenderTexture2D& rt, const TvSim& tv, float time);
void drawScoreboardCanvas(RenderTexture2D& rt, const std::string& title, const std::vector<std::string>& lines,
                          uint32_t seed);

// ============================================================================ textures (RoomTex.cpp)
Texture2D genFloorTexture(uint32_t seed);
Texture2D genWallAtlas(uint32_t seed, const std::vector<Opening>& openings, const std::vector<WallMark>& marks);
Texture2D genCeilingTexture(uint32_t seed);
Texture2D genCondensationTexture(uint32_t seed);  // window mist, one per pane (UV 0..1): frame edges and bottom
Texture2D genLaceTexture(uint32_t seed);      // half-height café curtain: net, flowers, scalloped hem (alpha)
Texture2D genDustTexture(uint32_t seed);
Texture2D genVarnishTexture(uint32_t seed);   // subtle streaks for varnished bentwood / painted metal
// Quarter-sawn boards: fine straight grain along u, faint medullary flecks, gentle colour drift (tileable).
Texture2D genBoardTexture(int size, Color light, Color dark, uint32_t seed);
void drawArtAtlas(RenderTexture2D& rt, uint32_t seed);
void drawStreetCanvas(RenderTexture2D& rt, uint32_t seed);
void drawLetteringCanvas(RenderTexture2D& rt);

}  // namespace rm

namespace rm {
struct Builders;  // all static geometry builders, one per material/flag group (RoomBuild.cpp)
struct Cat;       // the kahvehane cat (RoomCat.cpp)
}

// ============================================================================ Room::Impl
struct Room::Impl {
    Renderer* R = nullptr;
    okey::Rng rng{1};
    uint32_t seed = 1;
    bool ready = false;
    bool title = false;
    float time = 0.f;

    // ---- textures & canvases
    Texture2D texFloor{}, texWall{}, texCeil{}, texWood{}, texWoodDark{}, texFelt{}, texCondense{}, texLace{}, texDust{},
        texVarnish{}, texMarble{};
    RenderTexture2D cvArt{}, cvStreet{}, cvScore{}, cvTv{}, cvLetter{};

    // ---- materials
    Mat mFloor, mWall, mCeil, mWood, mWoodDark, mVarnish, mPaint, mCloth, mMetal, mBrass, mCeramic, mFelt, mMarble;
    Mat mShadeIn, mBulb, mGlass, mBottle, mTea, mArt, mArtGloss, mArtChalk, mLeaf, mScore, mTv, mStreet, mCondense, mLace, mLetter,
        mSweep, mEmber;

    // ---- static geometry (world space unless noted)
    struct Static {
        Mesh mesh{};
        const Mat* mat = nullptr;
        Matrix xf = MatrixIdentity();
        uint32_t flags = 0;
    };
    std::vector<Static> statics;       // opaque + transparent statics in submission order

    // ---- lamps (local meshes: origin = bulb centre)
    struct Lamp {
        Vector3 anchor{}, bulb{};
        Color color{255, 190, 120, 255};
        float intensity = 1.f, range = 4.f;
        float level = 1.f, dip = 0.f, phase = 0.f, swingAmp = 0.f, faulty = 0.f;
        bool key = false, small = false;
        Matrix xf = MatrixIdentity();  // swing transform (world)
        Vector3 cur{};                 // current bulb position
        Mat dust{};                    // per-lamp shaft material (alpha follows the flicker)
    };
    std::vector<Lamp> lamps;
    Mesh shadeOut{}, shadeIn{}, bulbMesh{}, cordMesh{};
    std::vector<Mesh> dustMeshes;  // one light shaft per lamp (camera-facing about Y)

    // ---- chairs (bentwood), shared mesh, local frame: sitter faces -Z
    struct Chair {
        Vector3 pos{};
        float yaw = 0.f;
        bool occupied = false;
        Vector3 off{}, offFrom{}, offTo{};
        float yawOff = 0.f, yawFrom = 0.f, yawTo = 0.f, t = 1.f;
    };
    std::vector<Chair> chairs;
    Mesh chairMesh{};

    // ---- small animated things
    Mesh fanBlades{};
    Vector3 fanPos{};
    float fanAngle = 0.f;
    Mesh handHour{}, handMin{}, handSec{};
    Vector3 clockPos{};
    Matrix tvXf = MatrixIdentity();
    Vector3 tvFront{}, tvNormal{};
    Mesh dieMesh{};
    struct Die {
        Vector3 from{}, to{};
        Vector3 axis{1, 0, 0};
        float spin = 0.f, yaw = 0.f;
        int face = 0;
        Matrix rest = MatrixIdentity();
    };
    Die dice[2];
    float diceT = 10.f, diceAnim = 1.f;
    int diceSide = 0;
    Vector3 tavlaCenter{};
    float chairT = 30.f, glassT = 60.f, steamBurst = 0.f;
    int lastSec = -1;
    float secTick = 1.f;
    std::vector<ui::Sfx> sfxQueue;
    Mesh curtainMesh[3]{};
    Vector3 curtainPivot{};
    Mesh sweepQuad{};
    float carT = 12.f, carZ = 0.f, carDir = 1.f, carActive = 0.f;

    // ---- ashtray at our table
    Mesh ashGlass{}, ashInside{}, ashCig{}, ashEmber{};
    Vector3 emberPos{};
    float emberHeat = 1.f;

    // ---- per-table transparent glasses (local meshes, submitted at the table centre)
    struct GlassSet {
        Mesh glass{};
        Vector3 at{};
    };
    std::vector<GlassSet> glassSets;

    // ---- windows (transparent condensation + lettering), local meshes
    struct Pane {
        Mesh mesh{};
        Vector3 at{};
        const Mat* mat = nullptr;
    };
    std::vector<Pane> panes;

    // ---- emitters
    struct Emitter {
        Vector3 pos{};
        float rate = 1.f, acc = 0.f;
        int kind = 0;  // 0 teapot steam, 1 stove, 2 cigarette
    };
    std::vector<Emitter> emitters;
    float ambientAcc = 0.f;
    struct Puff {
        Vector3 pos;
        SmokeParams p;
    };
    std::vector<Puff> pending;

    // ---- dynamic lights
    float tvLight = 0.5f, stoveLevel = 1.f, windowLevel = 1.f, sweepLevel = 0.f;
    Vector3 sweepLightPos{};
    Vector3 stoveGlowPos{}, flamePos{}, radioDialPos{}, streetLampGlow{}, backLampGlow{};
    Vector3 boardLightPos{}, boardGlowPos{};  // brass picture light over the scoreboard
    std::vector<Vector3> neighbourWindows;  // across the street (glows)

    // ---- motes in lamp cones
    struct Mote {
        Vector3 p, v;
        int lamp;
        float tw;
    };
    std::vector<Mote> motes;

    // ---- canvases state
    rm::TvSim tv;
    float tvRedraw = 0.f;
    bool tvGoal = false;
    std::string scoreTitle;
    std::vector<std::string> scoreLines;
    bool scoreDirty = true;
    int lastDay = -1;

    // ---- data used by both the builder and the wall texture
    std::vector<rm::Opening> openings;
    std::vector<rm::WallMark> marks;

    // ---- weather & street life (RoomWeather.cpp): some nights it rains (seed): drops run down the outside of
    //      the window glass, rain streaks fall past the street lamp, passers-by hurry past under umbrellas
    float rainBase = 0.f;         // tonight's rain, 0 = a dry night
    float rain = 0.f;             // current intensity (drifts slowly; the audio's rain bed follows it)
    // raindrops on the glass (animated), one slot per window; double-buffered: redrawing a render texture the GPU
    // may still be reading from the last frame stalls the pipeline for a whole frame
    RenderTexture2D cvDrops[2]{};
    int dropsFront = 0;
    RenderTexture2D cvWalkers{};  // passer-by silhouettes, 4 walk frames per figure (static)
    Texture2D texStreak{};        // rain streaks for the upright billboards outside
    Texture2D texDropSprite{};    // one raindrop, stamped onto the drops canvas
    Mat mDrops;
    struct Bead {
        float x, y, r;
    };
    struct Runner {
        float x, y, r, v, pause, wob;
        std::vector<Vector2> trail;
    };
    struct Trail {
        std::vector<Vector2> pts;
        float w, age;
    };
    struct DropSlot {
        Rectangle rect{};  // canvas pixels
        std::vector<Bead> beads;
        std::vector<Runner> runners;
        std::vector<Trail> trails;
        float beadAcc = 0.f;
    };
    std::vector<DropSlot> dropSlots;
    std::vector<Pane> dropPanes;
    float dropRedraw = 0.f;
    struct Streak {
        Vector3 p;
        float speed, bright;
        int var;
    };
    std::vector<Streak> streaks;
    struct Walker {
        int type = 0;
        Vector3 p{}, dir{};
        float speed = 1.3f, dist = 0.f, left = 0.f;
    };
    std::vector<Walker> walkers;
    float walkerT = 6.f;
    bool carSoundDone = false;

    // ---- the kahvehane cat (RoomCat.cpp)
    rm::Cat* cat = nullptr;
    int catChair = -1;  // the chair the cat is on: the chair-scrape animation leaves it alone
    bool catMeowed = false;  // for Room::consumeCatMeow
    Vector3 catMeowAt{};
    void initCat();
    void updateCat(float dt);
    void submitCat(Renderer& r);
    void freeCat(Renderer& r);

    // RoomWeather.cpp
    void initWeather();
    void updateWeather(float dt);
    void submitWeather(Renderer& r);
    void freeWeather(Renderer& r);
    void simDrops(DropSlot& s, float dt);
    void placeStreak(Streak& s, okey::Rng& rng, bool anywhereY);
    void drawDrops();

    // RoomBuild.cpp
    void planWalls();
    void buildAll();
    void buildArchitecture(rm::Builders& B);
    void buildWallDecor(rm::Builders& B);
    void buildCounter(rm::Builders& B);
    void buildBgTables(rm::Builders& B);
    void buildLamps(rm::Builders& B);
    void buildStoveTvFan(rm::Builders& B);
    void buildAshtray();
    void buildStreetAndWindows(rm::Builders& B);
    void buildCurtain();
    void addStatic(const MeshBuilder& b, const Mat* m, uint32_t flags, Matrix xf = MatrixIdentity());

    // Room.cpp
    void emit(Vector3 p, const SmokeParams& sp) { pending.push_back({p, sp}); }
    std::vector<Vector3> bgLampPos() const {
        std::vector<Vector3> v;
        for (const Lamp& L : lamps)
            if (!L.key) v.push_back(L.bulb);
        return v;
    }
    void updateLamps(float dt);
    void updateProps(float dt);
    void updateSmoke(float dt);
    void warmUpSmoke();
    void submitLights(Renderer& r);
    void submitLamps(Renderer& r);
    void submitProps(Renderer& r);
    void sfx(ui::Sfx s);
    void freeAll(Renderer& r);
};

}  // namespace r3d
