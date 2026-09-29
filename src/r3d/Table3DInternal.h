#pragma once
// Internal to the table module (src/r3d/Table3D*.cpp): shared state of Table3D::Impl, rack logic, pose math
// and table geometry. Not a public API — nothing outside the table module includes this.
#include "core/Solver.h"
#include "r3d/Table3D.h"
#include "r3d/World.h"
#include "ui/TileRender.h"

#include <raymath.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace r3d {
namespace t3d {

using okey::NUM_TILES;

// ================================================================ rack model (pure, unit-tested by the harness)
constexpr int ROWS = 2, COLS = w3d::RACK_SLOTS, SLOTS = ROWS * COLS; // row 0 = upper (back) tier, row 1 = front
using Slots = std::array<int, SLOTS>;

struct RackGroup {
    int row = 0, col = 0, len = 0;
    std::vector<int> ids;
};
std::vector<RackGroup> parseGroups(const Slots& s);
// Melds as contiguous blocks separated by one gap (a meld never straddles two rows unless it is longer
// than a row), leftovers right after them. Melds that find no room in the two rows (e.g. an 11th pair)
// and leftovers that do not fit in that run go to free cells that touch no meld, so every placed meld
// stays a group of its own. false only if > 32 tiles.
bool layoutArrangement(const std::vector<std::vector<int>>& melds, const std::vector<int>& leftovers, Slots& out);
// Free slot for a newly received tile: `pref` if free, else the right end of the top row away from groups.
int chooseFreeSlot(const Slots& s, int pref);
// Put `id` immediately before column p (0..COLS) of `row`. The tiles from p on move one step toward a gap
// of two free slots (or the row end), so the groups on the way keep their separating gaps; only a crowded
// row lets neighbouring groups touch.
bool insertBefore(Slots& s, int row, int p, int id);
// Moves the tile at `from` to `to` (insert before/after an occupied slot, swap on a full row). A move
// within one unbroken stretch of a row is a list move: the tiles in between close up, no hole is left.
bool moveTile(Slots& s, int from, int to, bool after);

// ================================================================ math
struct Pose {
    Vector3 pos{0, 0, 0};
    Quaternion rot{0, 0, 0, 1};
    float scale = 1.f;
};
Matrix poseMatrix(const Pose& p);
Quaternion quatFromAxes(Vector3 x, Vector3 y, Vector3 z); // images of the local X, Y, Z axes (orthonormal)
Vector3 poseAxis(const Pose& p, int axis);                // world direction of local axis 0/1/2
Vector3 poseToWorld(const Pose& p, Vector3 local);        // local offset (unscaled units * scale)
float hashF(uint32_t x);                                  // 0..1
float hashS(uint32_t x);                                  // -1..1
uint32_t hashU(uint32_t x);
// Ray vs an oriented box around a pose (half extents in the pose's local frame, before scale).
// Returns the hit distance or -1.
float rayPoseBox(const Ray& r, const Pose& p, Vector3 half);
bool rayPlane(const Ray& r, Vector3 p0, Vector3 n, Vector3& hit, float* dist = nullptr);
float smooth01(float e0, float e1, float x);

// ================================================================ table geometry (world units, see World.h)
constexpr float TW = w3d::TILE_W, TH = w3d::TILE_H, TT = w3d::TILE_T;
constexpr float PITCH = 0.0322f;              // slot pitch along an istaka
constexpr float RACK_LEDGE_T = 0.006f;        // ledge thickness (along the plank)
constexpr float RACK_ROW_GAP = RACK_LEDGE_T + 0.0016f; // plank distance between the two rows
constexpr float DRAG_LIFT_Y = 0.045f;         // dragged tiles float this high above the felt
constexpr float HUMAN_MELD_NEAR = 0.33f;      // the human's melds stay this far from the edge (istaka shadow)
constexpr float SIDE_MELD_NEAR = 0.12f;       // side players' melds end here, toward the human (short of zone 0)
constexpr float FAR_MELD_CLEAR = 0.018f;      // the far player's rows keep this far off zone 2's near edge: the
                                              // top of a full pile stays below the first row as seen from the seat
constexpr float MELD_SCALE_FAR = 1.14f;       // meld tiles may grow this much across the table when there is room
constexpr float MELD_SCALE_SIDE = 1.08f;      // ... and on the side players' zones
constexpr float REVEAL_SECONDS = 1.3f;        // hand over: the bots' istakas turn around (staggered)

// Istaka cross-section: tiles lean back against a slanted plank; two ledges carry the two rows.
struct RackProfile {
    float leanDeg;   // plank lean from vertical (toward the table centre)
    float outB;      // plank base point, seat-local "out" relative to RACK_DIST
    float yB;        // plank base height (absolute)
};
const RackProfile& rackProfile(bool human);
// Pose of a tile standing in `seat`'s rack: row 0 upper / 1 front, `right` offset along SEAT_RIGHT
// (0 = rack centre). `faceOwner` = face toward the rack's owner (false: turned around, face to the centre).
Pose rackPose(int seat, bool humanRack, int row, float right, bool faceOwner = true);
float slotRight(int col);                     // right offset of a slot column
// Plane of a rack row (through the tile centres), in world space: point + normal + in-plane axes.
void rackRowFrame(int seat, bool humanRack, int row, Vector3& center, Vector3& normal, Vector3& right,
                  Vector3& up);

Quaternion flatFaceUp(float yawDeg);          // lying face-up, readable from +Z, extra yaw
Quaternion flatFaceDown(float yawDeg);
Pose pilePose(int index);                     // index 0 = bottom of the first stack
Pose discardPose(int seat, int depth, int n, int id);  // depth 0 = top
Pose indicatorPose();
Pose heapPose(int cell, float swirl);         // shuffle heap on the felt; swirl = shuffle phase angle (rad)
constexpr int HEAP_CELLS = 128;
constexpr int DISC_LAYERS = 6;                // visible layers of a discard stack

// ================================================================ state
enum : int { C_NONE = -1, C_HEAP = 0, C_PILE = 1, C_IND = 2, C_HAND = 10, C_DISC = 20, C_MELD = 100 };

struct Target {
    int cont = C_HEAP;
    Pose pose;
    bool hidden = false; // buried in a discard stack: not submitted while resting
};

struct TileVis {
    bool init = false;
    int cont = C_NONE;
    Pose pose;
    bool flying = false;
    bool atSlot = false;  // resting in a rack slot (flights out of it rise first)
    float ft = 0.f, fdur = 0.45f, arc = 0.05f;
    Pose from;
    Vector3 c1{0, 0, 0};    // cubic Bezier: control near the start (absolute)
    Vector3 c2off{0, 0, 0}; // control near the end, relative to the (possibly moving) target
    Vector3 wobAxis{1, 0, 0};
    float wobAng = 0.f;
    int serial = 0;
    float flightStart = 0.f;
    float landT = 10.f;
    float landAmp = 0.f;
    bool forceHop = false;
    bool clickOnLand = false;
    float glow = 0.f;     // smoothed highlight amount (hover/selection)
};

struct MeldBox {
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0; // world rect on the felt
    float scale = 1.f;
    std::vector<Vector3> pos;            // tile centres
};

struct Toast {
    std::string key, text;
    Color color = ui::pal::TextLight;
    float age = 0.f, dur = 2.6f;
};

enum class Btn { None, Open, GiveBack, Series, Pairs, Menu, ConfirmYes, ConfirmNo, AiToggle };
constexpr int NUM_BUTTONS = 6; // El Aç, Geri Ver, Seri Diz, Çift Diz, Yapay Zeka, Menü (bottom-right column)

struct Input {
    Vector2 mouse{-10000, -10000};
    bool pressed = false, down = false, released = false;
    bool keySeries = false, keyPairs = false, keyOpen = false;
};

struct Press {
    enum Kind { None, RackTile, Pile, Left, Empty } kind = None;
    int tile = -1;
    int slot = -1;
    Vector2 at{0, 0};
    bool dragging = false;
};

struct Place {
    bool active = false;
    int slot = -1;
    bool after = false;
};

struct Confirm {
    bool active = false;
    int tile = -1;
    std::string text;
    float t = 0.f;
};

// Where the mouse ray points at (drag surface + drop targets).
struct Aim {
    bool valid = false;
    bool overRack = false;
    int rackRow = -1;
    float rackRight = 0.f;       // along the rack
    int slot = -1;
    bool after = false;
    Vector3 dragPoint{0, 0, 0};  // on the drag surface (rack row plane or the lifted table plane)
    Vector3 feltPoint{0, 0, 0};  // (x, z) under the dragged tile at felt height
    bool overDiscard = false;
    int meld = -1;
    int meldJoker = -1;          // index inside the meld of a joker under the cursor
    bool meldFront = false;
    bool overPile = false, overLeft = false;
    int discardSeat = -1;        // a discard pile under the pointer
};

enum class Phase { Idle, Gather, Shuffle, Deal };

// Turkish case endings for a proper name (vowel harmony, buffer consonant, t/d assimilation):
// locative "Hacı Rıza'da", "Kel Mahmut'ta"; genitive "Hacı Rıza'nın", "Kel Mahmut'un", "Emekli Nuri'nin".
std::string nameLocative(const std::string& name);
std::string nameGenitive(const std::string& name);

} // namespace t3d

// Everything behind Table3D (Table3D::Impl derives from it; a plain struct so the harness hooks can reach it).
struct TableState {
    // ---------------------------------------------------------------- wiring
    Renderer* R = nullptr;
    okey::Game* game = nullptr;
    int human = 0;
    float speed = 1.f;
    bool hints = true;
    std::function<void(ui::Sfx)>* sfx = nullptr;
    float now = 0.f;
    std::array<Vector3, 4> heads{};
    bool headsSet = false;
    Camera3D cam{};
    bool camValid = false;

    // ---------------------------------------------------------------- gfx resources (Table3DGeom.cpp)
    bool gfxReady = false;
    Mesh tileMesh[55]{};
    Mesh tableMesh{}, feltMesh{}, rackHumanMesh{}, rackBotMesh{}, standMesh{};
    Mesh starQuad{}, haloQuad{}, ringQuad{}, stripQuad{};
    Texture2D texWood{}, texFelt{}, texRack{}, texHalo{}, texRing{}, texStrip{};
    Mat matTile{}, matTileHi{}, matWood{}, matFelt{}, matRack{}, matStar{};
    // glows in 16 intensity steps each (a Mat is shared by every draw of a frame): additive soft glows
    // (light on the felt), alpha-blended outline rings and strips (crisp on ivory and wood alike)
    enum GlowColor { G_GOLD, G_GREEN, G_BLUE, G_RED, G_ORANGE, G_WARM, G_COUNT };
    Mat matGlow[G_COUNT][16]{}, matRing[G_COUNT][16]{}, matStrip[G_COUNT][16]{};
    void buildGfx(Renderer& r);
    void freeGfx(Renderer& r);
    void submitAll(Renderer& r);
    void submitTile(Renderer& r, int id);
    void submitGlow(Renderer& r, const t3d::Pose& p, int color, float amount, float grow);
    void submitRing(Renderer& r, const t3d::Pose& p, int color, float amount, float grow);
    void submitStrip(Renderer& r, Vector3 a, Vector3 b, Vector3 normal, float width, int color, float amount);
    static const Mat& step16(const Mat* arr, float amount);

    // ---------------------------------------------------------------- tiles
    t3d::Slots slots{};
    std::array<std::vector<int>, 4> botOrder;
    std::vector<int> pileOrder;               // bottom .. top (UI order; faces unknown)
    std::array<t3d::TileVis, t3d::NUM_TILES> vis{};
    std::array<t3d::Target, t3d::NUM_TILES> tgt{};
    std::array<int, t3d::NUM_TILES> heapCell{};
    std::array<float, t3d::NUM_TILES> dealDelay{};
    std::vector<t3d::MeldBox> boxes;
    std::vector<float> meldBorn, meldPulse;
    int flightCounter = 0;
    float lastLandClick = -10.f;
    int lastSubmits = 0;
    std::array<bool, t3d::NUM_TILES> publicTile{}; // seen face up on the table this hand
    std::array<bool, t3d::NUM_TILES> privTile{};   // face must not be drawn (bots' hidden tiles, pile, heap)

    t3d::Phase phase = t3d::Phase::Idle;
    float phaseT = 0.f;
    bool dealPending = false;
    float dealEndsAt = -1.f;
    bool built = false;
    int builtHand = -1, builtInd = -1, builtTurn = -1;
    bool needRebuild = false;
    bool pendingSync = false;
    bool layingMelds = false;    // the last event was Open / LayMelds (a bot's tiles leave its rack together)
    bool revealed = false;       // bots' racks turned around (hand over)
    float revealAt = -1.f;

    // ---------------------------------------------------------------- interaction
    t3d::Input in;
    bool humanInput = false;
    t3d::Press press;
    t3d::Aim aim;
    int selected = -1;
    int hoverTile = -1;          // rack tile under the mouse (id)
    bool hoverPile = false, hoverLeft = false;
    bool hudHover = false;
    int lastClickTile = -1;
    float lastClickTime = -10.f;
    Vector2 lastMouse{0, 0};
    Vector2 dragVel{0, 0};
    t3d::Pose dragPose;
    bool dragPoseValid = false;
    t3d::Btn queued = t3d::Btn::None;
    bool menuRequested = false;
    // "Yapay Zeka" mode (Table3D::setAiMode)
    bool aiMode = false;
    bool aiToggleRequested = false;
    bool aiArrangePending = false; // the AI's istaka is re-arranged at the next chance (not while dealing)
    bool aiPairs = false;          // the AI's istaka is laid out in pairs (sticky while they hold up)
    t3d::Place place;
    int newTile = -1;
    float newTileTime = -10.f;

    std::vector<t3d::RackGroup> groups;
    std::vector<int> groupKind;  // 0 none, 1 series, 2 pair
    std::vector<int> groupValue;
    std::vector<std::vector<int>> seriesGroups, pairGroups;
    okey::OpenCheck seriesCheck, pairCheck;
    std::vector<char> islek;

    int peekKind = 0;            // hover inspection: 0 none, 1 meld, 2 discard pile
    int peekIdx = -1;            // meld index / seat
    float peekT = 0.f;           // how long it has been hovered
    std::vector<t3d::Toast> toasts;
    t3d::Confirm confirm;
    int pileWarn = 0;

    // ---------------------------------------------------------------- basics (Table3D.cpp)
    TableState();
    const okey::OkeyInfo& ok() const { return game->okey(); }
    bool playing() const { return game && game->handState() == okey::HandState::Playing; }
    bool started() const { return game && game->handState() != okey::HandState::NotStarted; }
    bool myTurn() const { return playing() && game->current() == human; }
    // the human may play: their turn, input is theirs (no screen up) and every dealt tile has landed
    bool canAct() const { return myTurn() && humanInput && !dealing(); }
    bool rackInteractive() const {
        if (aiMode || !game || game->handState() != okey::HandState::Playing || dealing()) return false;
        return humanInput || !myTurn();
    }
    bool canDrawNow() const { return canAct() && game->stage() == okey::TurnStage::NeedDraw && game->pileCount() > 0; }
    bool canTakeLeftNow() const { return canAct() && game->canTakeFromLeft(human); }
    void sound(ui::Sfx s) const;
    void pushToast(const std::string& key, const std::string& text, Color c, float dur);
    void error(const std::string& text);
    int slotOfTile(int id) const;
    bool inHumanHand(int id) const;
    int leftSeat() const { return okey::Game::leftOf(human); }

    // state -> targets -> visuals
    void reconcileRack();
    void reconcilePile();
    bool arrangedSlots(bool pairs, t3d::Slots& out) const;
    void arrange(bool pairs);
    bool aiWantsPairs();         // which layout the AI's istaka shows (updates aiPairs)
    void updateAiArrange();
    void setAiMode(bool on);
    void rebuildForHand();
    void layoutMelds();
    void computeTargets();
    void startFlight(int id, int fromCont, int toCont, const t3d::Pose& to, float delay, bool hop);
    float botLead(int fromCont, int toCont) const;  // start delay of a flight an opponent's hand makes
    void advanceVisuals(float dt);
    bool dealing() const;
    bool anyFlying() const;
    int draggedTileId() const;
    void computeHints();
    t3d::Pose restPose(int id) const;   // target pose without hover lift

    // input (Table3D.cpp)
    Ray mouseRay(Vector2 m) const;
    void computeAim(Vector2 m);
    int pickRackTile(const Ray& ray) const;
    bool pickPile(const Ray& ray) const;
    bool pickLeft(const Ray& ray) const;
    bool isleFits(int tile, int meld, int jokerIdx, bool* swap) const;  // the tile fits (timing aside)
    bool isleLegal(int tile, int meld, int jokerIdx, bool* swap) const; // ... and may be played now
    bool isleFront(int tile, int meld, bool wantFront) const; // which end of a run the tile will go to
    void attemptDraw(bool fromLeft, int slot, bool after);
    void attemptDiscard(int tile, bool confirmed);
    void attemptIsle(int tile, int meld, int jokerIdx, bool front);
    void doOpen();
    void doGiveBack();
    void notYourTurn();          // a neutral hint when the human reaches for the table out of turn
    void runButton(t3d::Btn b);
    void queue(t3d::Btn b);
    void handleInput(float dt);
    void onDrop(const t3d::Press& p);
    void onClick(const t3d::Press& p);
    void updateDragPose(float dt);
    void step(float dt, bool hi);
    void onEvent(const okey::GameEvent& e);

    // HUD (Table3DHud.cpp)
    std::array<Rectangle, t3d::NUM_BUTTONS> buttonRects() const;
    bool overHud(Vector2 m) const;
    void drawHUD(const Renderer& r);
    void drawNameplates(const Renderer& r);
    void drawPileLabel(const Renderer& r);
    void drawRackHints(const Renderer& r);
    void drawStatus();
    void drawButtons();
    void drawToasts();
    void drawDragHints(const Renderer& r);
    void drawConfirm();
    void drawPeek(const Renderer& r);
    std::string statusText(Color& c) const;
};

} // namespace r3d
