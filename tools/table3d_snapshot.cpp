// Table owner's harness: drives a real okey::Game (bots via okey::Bot), renders the 3D table from the
// human's seat and from other angles into build/table3d/shots/*.png (build/table3d/shots_room/ when the
// real Room is linked), and asserts ray picking, rack logic, the human's actions through the mouse and
// the table/game consistency. A simple stand-in room + placeholder opponents are drawn when the Room /
// Characters modules are not linked.
//
//   sh build/table3d/build.sh && (caffeinate -u -t 3 &); sleep 1; ./build/table3d/harness
//   WITH_ROOM=1 [WITH_CHARS=1] sh build/table3d/build.sh   (-> harness_room / harness_room_chars)
#include "core/Bot.h"
#include "core/Game.h"
#include "r3d/Gfx.h"
#include "r3d/Table3D.h"
#include "r3d/Table3DTest.h"
#include "r3d/World.h"
#include "ui/Common.h"
#include "ui/TileRender.h"
#ifdef T3D_ROOM
#include "r3d/Room.h"
#endif
#ifdef T3D_CHARS
#include "r3d/Characters.h"
#endif

#include <raymath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace r3d;
namespace tt = r3d::table3dtest;

namespace {

#if defined(T3D_ROOM) && defined(T3D_CHARS)
const char* const OUT_DIR = "build/table3d/shots_full/";
#elif defined(T3D_ROOM)
const char* const OUT_DIR = "build/table3d/shots_room/";
#else
const char* const OUT_DIR = "build/table3d/shots/";
#endif

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

Camera3D makeCam(Vector3 pos, Vector3 target, float fovy) {
    Camera3D c{};
    c.position = pos;
    c.target = target;
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

// ---------------------------------------------------------------- stand-in room & people
struct StandIn {
    Mesh floor{}, wall{}, sideWall{}, ceiling{}, head{}, body{}, chair{}, shade{}, bulb{};
    Texture2D floorTex{}, wallTex{};
    Mat mFloor{}, mWall{}, mSkin[3]{}, mShirt[3]{}, mChair{}, mShade{}, mBulb{};

    void init(Renderer& R) {
        floorTex = genWoodTexture(512, Color{96, 64, 40, 255}, Color{52, 32, 20, 255}, 7, 30.f);
        wallTex = genPlasterTexture(512, Color{168, 136, 90, 255}, 9);
        mFloor = R.makeMat(WHITE, floorTex, 0.2f, 20.f);
        mWall = R.makeMat(WHITE, wallTex, 0.05f, 8.f);
        const Color skin[3] = {{214, 168, 132, 255}, {200, 150, 116, 255}, {222, 180, 150, 255}};
        const Color shirt[3] = {{92, 84, 70, 255}, {70, 86, 120, 255}, {120, 96, 70, 255}};
        for (int i = 0; i < 3; ++i) {
            mSkin[i] = R.makeMat(skin[i], Texture2D{}, 0.1f, 12.f, 0.f, 0.3f);
            mShirt[i] = R.makeMat(shirt[i], Texture2D{}, 0.05f, 8.f, 0.f, 0.2f);
        }
        mChair = R.makeMat(Color{84, 52, 28, 255}, Texture2D{}, 0.2f, 20.f);
        mShade = R.makeMat(Color{40, 70, 52, 255}, Texture2D{}, 0.4f, 30.f);
        mBulb = R.makeMat(Color{255, 230, 170, 255}, Texture2D{}, 0.f, 8.f, 1.f);
        MeshBuilder b;
        b.plane({0, 0, 0}, {w3d::ROOM_X1 - w3d::ROOM_X0, w3d::ROOM_Z1 - w3d::ROOM_Z0}, {0, 1, 0}, {6, 5});
        floor = b.build();
        b.clear();
        b.plane({0, w3d::CEILING_Y * 0.5f, 0}, {w3d::ROOM_X1 - w3d::ROOM_X0, w3d::CEILING_Y}, {0, 0, 1}, {3, 1.2f});
        wall = b.build();
        b.clear();
        b.plane({0, w3d::CEILING_Y * 0.5f, 0}, {w3d::ROOM_Z1 - w3d::ROOM_Z0, w3d::CEILING_Y}, {0, 0, 1}, {3, 1.2f});
        sideWall = b.build();
        b.clear();
        b.plane({0, 0, 0}, {w3d::ROOM_X1 - w3d::ROOM_X0, w3d::ROOM_Z1 - w3d::ROOM_Z0}, {0, -1, 0}, {3, 3});
        ceiling = b.build();
        b.clear();
        b.ellipsoid({0, 0, 0}, {0.085f, 0.11f, 0.095f}, 16, 20);
        head = b.build();
        b.clear();
        b.capsule({0, 0.62f, 0}, {0, 1.0f, 0}, 0.19f, 18);
        body = b.build();
        b.clear();
        b.roundedBox({0, 0.44f, 0}, {0.44f, 0.04f, 0.42f}, 0.01f, 2);
        b.roundedBox({0, 0.22f, 0.19f}, {0.04f, 0.44f, 0.04f}, 0.01f, 1);
        chair = b.build();
        shade = genLathe({{0.24f, -0.02f}, {0.16f, 0.07f}, {0.05f, 0.14f}, {0.02f, 0.16f}}, 36, false, false);
        bulb = genLathe({{0.0f, -0.07f}, {0.04f, -0.04f}, {0.045f, 0.0f}, {0.02f, 0.03f}}, 24, false, false);
    }

    void lights(Renderer& R) {
        Ambient a;
        a.sky = Color{120, 96, 72, 255};
        a.ground = Color{46, 34, 26, 255};
        a.intensity = 0.5f;
        R.setAmbient(a);
        Fog f;
        f.density = 0.09f;
        R.setFog(f);
        KeyLight k;
        k.position = w3d::TABLE_LAMP;
        k.target = {0, w3d::TABLE_Y, 0};
        k.intensity = 3.2f;
        R.setKeyLight(k);
        R.addPointLight({{-2.5f, 2.3f, -1.8f}, Color{255, 186, 120, 255}, 1.1f, 4.5f});
        R.addPointLight({{2.4f, 2.3f, -1.9f}, Color{255, 186, 120, 255}, 1.1f, 4.5f});
        R.addPointLight({{0.f, 2.4f, 2.4f}, Color{255, 196, 140, 255}, 0.7f, 4.f});
    }

    void submit(Renderer& R, bool people, bool roomToo = true, bool chairs = true) {
        if (!roomToo) {
            for (int s = 0; s < 4 && chairs; ++s) {
                const Vector3 p = w3d::seatPos(s);
                R.submit(&chair, &mChair, trsYaw(p, w3d::seatYawDeg(s)), CastShadow);
                if (s == 0 || !people) continue;
                R.submit(&body, &mShirt[s - 1], trsYaw({p.x * 1.02f, 0.f, p.z * 1.02f}, w3d::seatYawDeg(s)), CastShadow);
                R.submit(&head, &mSkin[s - 1], MatrixTranslate(p.x * 0.98f, w3d::HEAD_Y + 0.02f, p.z * 0.98f), CastShadow);
            }
            return;
        }
        R.submit(&floor, &mFloor, MatrixIdentity(), 0);
        R.submit(&wall, &mWall, MatrixTranslate(0, 0, w3d::ROOM_Z0), 0);
        R.submit(&wall, &mWall, MatrixMultiply(MatrixRotateY(PI), MatrixTranslate(0, 0, w3d::ROOM_Z1)), 0);
        R.submit(&sideWall, &mWall, MatrixMultiply(MatrixRotateY(PI * 0.5f), MatrixTranslate(w3d::ROOM_X0, 0, (w3d::ROOM_Z0 + w3d::ROOM_Z1) * 0.5f)), 0);
        R.submit(&sideWall, &mWall, MatrixMultiply(MatrixRotateY(-PI * 0.5f), MatrixTranslate(w3d::ROOM_X1, 0, (w3d::ROOM_Z0 + w3d::ROOM_Z1) * 0.5f)), 0);
        R.submit(&ceiling, &mWall, MatrixTranslate(0, w3d::CEILING_Y, 0), 0);
        R.submit(&shade, &mShade, MatrixTranslate(0, w3d::TABLE_LAMP.y + 0.02f, 0), DoubleSided);
        R.submit(&bulb, &mBulb, MatrixTranslate(0, w3d::TABLE_LAMP.y, 0), 0);
        R.submitGlow({0, w3d::TABLE_LAMP.y - 0.03f, 0}, 0.22f, Color{255, 210, 150, 255}, 0.7f);
        for (int s = 0; s < 4 && chairs; ++s) {
            const Vector3 p = w3d::seatPos(s);
            R.submit(&chair, &mChair, trsYaw(p, w3d::seatYawDeg(s)), CastShadow);
            if (s == 0 || !people) continue;
            R.submit(&body, &mShirt[s - 1], trsYaw({p.x * 1.02f, 0.f, p.z * 1.02f}, w3d::seatYawDeg(s)), CastShadow);
            R.submit(&head, &mSkin[s - 1], MatrixTranslate(p.x * 0.98f, w3d::HEAD_Y + 0.02f, p.z * 0.98f), CastShadow);
        }
    }
};

// ---------------------------------------------------------------- the harness
struct Harness {
    Renderer R;
    RenderTexture2D rt{};
    okey::Game game;
    Table3D table;
    PlayerCamera pcam;
    StandIn room;
    std::array<std::unique_ptr<okey::Bot>, 4> bots;
    int sfxCount = 0;
#ifdef T3D_ROOM
    Room room3d;
#endif
#ifdef T3D_CHARS
    Characters chars;
#endif

    bool init() {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
        InitWindow(320, 180, "table3d");
        if (!IsWindowReady()) return false;
        ui::loadFonts();
        if (!R.init()) return false;
        rt = LoadRenderTexture(1600, 900);
        R.setRenderSize(1600, 900);
        ui::setCurrentViewport(ui::Viewport{});
        room.init(R);
#ifdef T3D_ROOM
        if (!room3d.init(R, 1234u)) return false;
        room3d.setScoreboard("El 1 / 5", {"Sen .......... 0", "Hacı Rıza .... 0", "Kel Mahmut ... 0", "Emekli Nuri .. 0"});
        for (int i = 0; i < 240; ++i) room3d.update(1.f / 60.f), R.update(1.f / 60.f);
#endif
#ifdef T3D_CHARS
        if (!chars.init(R, 1234u)) return false;
        chars.setNames({"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
#endif
        table.playSfx = [this](ui::Sfx) { ++sfxCount; };
        const double t0 = GetTime();
        if (!table.init(R, &game, 0)) return false;
        std::printf("Table3D::init %.0f ms\n", (GetTime() - t0) * 1000.0);
        pcam.reset();
        for (int s = 1; s < 4; ++s) bots[s] = std::make_unique<okey::Bot>(okey::BotLevel::Normal, 1000u + (unsigned)s);
        return true;
    }

    Camera3D seat() const { return pcam.camera(); }

    void pump() {
        for (const okey::GameEvent& e : game.drainEvents()) {
            table.onEvent(e);
#ifdef T3D_CHARS
            chars.onEvent(e, game);
            if (e.type == okey::EvType::TurnStart) chars.setActiveSeat(e.player);
#endif
            for (auto& b : bots)
                if (b) b->observe(e, game);
            if (e.type == okey::EvType::HandStart) {
                for (auto& b : bots)
                    if (b) b->resetForHand();
                table.onHandStart();
            }
        }
    }

    void frame(float dt = 1.f / 60.f, Vector2 mouse = {-10000, -10000}, int button = 0, int key = 0, bool human = true) {
        pump();
        tt::input(table, dt, mouse, button, key, human, seat());
#ifdef T3D_ROOM
        room3d.update(dt);
#endif
#ifdef T3D_CHARS
        chars.update(dt, seat());
#endif
        R.update(dt);
    }

    void settle() {
        pump();
        tt::settle(table, seat());
    }

    // One bot action (returns false if it isn't a bot's turn).
    bool botStep() {
        if (game.handState() != okey::HandState::Playing) return false;
        const int s = game.current();
        if (s == 0) return false;
        okey::BotAction a = bots[s]->next(game, s);
        okey::ActionResult r = okey::applyBotAction(game, s, a);
        if (!r.ok) {
            okey::BotAction f = okey::fallbackAction(game, s);
            r = okey::applyBotAction(game, s, f);
            check(r.ok, "bot fallback failed: " + r.error);
        }
        pump();
        return true;
    }

    void botsUntilHuman(bool animate) {
        int guard = 0;
        while (game.handState() == okey::HandState::Playing && game.current() != 0 && guard++ < 400) {
            botStep();
            if (animate) settle();
        }
        settle();
    }

    void render(const Camera3D& cam, bool hud, bool people = true) {
#ifdef T3D_ROOM
        room3d.submit(R);
#else
        room.lights(R);
#endif
        bool chairs = true;
#ifdef T3D_CHARS
        chars.submit(R); // the opponents, their chairs and props
        people = false;
        chairs = false;
#endif
#ifdef T3D_ROOM
        room.submit(R, people, false, chairs);
#else
        room.submit(R, people, true, chairs);
#endif
        table.submit(R);
        R.render(cam, &rt, Color{18, 13, 9, 255});
        if (hud) {
            BeginTextureMode(rt);
            ui::uiBeginFrame();
            std::array<Vector3, 4> heads;
            for (int s = 0; s < 4; ++s) {
                const Vector3 p = w3d::seatPos(s);
                heads[s] = {p.x * 0.98f, w3d::HEAD_Y + 0.02f, p.z * 0.98f};
#ifdef T3D_CHARS
                heads[s] = chars.headPosition(s);
#endif
            }
            table.setHeadAnchors(heads);
#ifdef T3D_CHARS
            chars.drawOverlay(R);
#endif
            table.drawHUD(R);
            EndTextureMode();
        }
    }

    void shot(const std::string& name, const Camera3D& cam, bool hud = true, bool people = true) {
        render(cam, hud, people);
        Image im = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&im);
        const std::string path = OUT_DIR + name + ".png";
        ExportImage(im, path.c_str());
        UnloadImage(im);
        std::printf("wrote %s (submits %d)\n", path.c_str(), tt::submitCount(table));
    }

    // Renders `count` frames, `every` simulation frames apart, into one contact sheet (3 columns).
    void strip(const std::string& name, int count, int every, const Camera3D* camOverride = nullptr) {
        sheet(name, count, [&]() {
            for (int k = 0; k < every; ++k) frame();
            return camOverride ? *camOverride : seat();
        });
    }
    // Contact sheet (3 columns): `advance` moves the world on and returns the camera for the next cell.
    void sheet(const std::string& name, int count, const std::function<Camera3D()>& advance) {
        const int cols = 3, rows = (count + cols - 1) / cols, cw = 640, ch = 360;
        Image sheet = GenImageColor(cols * cw, rows * ch, BLACK);
        for (int i = 0; i < count; ++i) {
            const Camera3D cam = advance();
            render(cam, false);
            Image im = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&im);
            ImageResize(&im, cw, ch);
            ImageDraw(&sheet, im, {0, 0, (float)cw, (float)ch}, {(float)((i % cols) * cw), (float)((i / cols) * ch), (float)cw, (float)ch}, WHITE);
            UnloadImage(im);
        }
        const std::string path = OUT_DIR + name + ".png";
        ExportImage(sheet, path.c_str());
        UnloadImage(sheet);
        std::printf("wrote %s\n", path.c_str());
    }

    Vector2 project(Vector3 p) {
        Vector2 v{0, 0};
        R.projectToVirtual(p, v);
        return v;
    }
};

// ---------------------------------------------------------------- rack logic unit checks (ported from the 2D harness)
std::vector<int> emptySlots() { return std::vector<int>(32, -1); }
#define CHECK(cond) check((cond), std::string(#cond) + " @" + std::to_string(__LINE__))

void unitRackLogic() {
    {
        std::vector<int> s = emptySlots();
        s[0] = 10; s[1] = 11; s[2] = 12; s[4] = 20; s[5] = 21; s[15] = 30; s[16] = 40; s[17] = 41; s[31] = 50;
        auto g = tt::parseGroups(s);
        CHECK(g.size() == 5);
        CHECK(g.size() > 0 && g[0] == std::vector<int>({10, 11, 12}));
        CHECK(g.size() > 1 && g[1] == std::vector<int>({20, 21}));
        CHECK(g.size() > 2 && g[2] == std::vector<int>({30}));
        CHECK(g.size() > 3 && g[3] == std::vector<int>({40, 41}));
        CHECK(g.size() > 4 && g[4] == std::vector<int>({50}));
        CHECK(tt::parseGroups(emptySlots()).empty());
        std::vector<int> full(32);
        for (int i = 0; i < 32; ++i) full[i] = i;
        auto gf = tt::parseGroups(full);
        CHECK(gf.size() == 2 && gf[0].size() == 16 && gf[1].size() == 16);
    }
    {
        std::vector<int> s;
        CHECK(tt::layoutArrangement({{1, 2, 3}, {4, 5, 6, 7}}, {8, 9, 10}, s));
        const std::vector<int> want = {1, 2, 3, -1, 4, 5, 6, 7, -1, 8, 9, 10};
        for (int i = 0; i < 12; ++i) CHECK(s[i] == want[i]);
        for (int i = 12; i < 32; ++i) CHECK(s[i] == -1);
        CHECK(tt::parseGroups(s).size() == 3);
        CHECK(tt::layoutArrangement({{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, {11, 12, 13, 14, 15}}, {}, s));
        CHECK(s[12] == -1 && s[16] == 11 && s[20] == 15);
        std::vector<std::vector<int>> melds;
        int id = 0;
        for (int m = 0; m < 7; ++m) {
            melds.push_back({id, id + 1, id + 2});
            id += 3;
        }
        CHECK(tt::layoutArrangement(melds, {id++}, s));
        int placed = 0;
        for (int v : s) placed += v >= 0;
        CHECK(placed == 22);
        melds.clear();
        id = 0;
        for (int m = 0; m < 11; ++m) {
            melds.push_back({id, id + 1});
            id += 2;
        }
        CHECK(tt::layoutArrangement(melds, {}, s));
        placed = 0;
        for (int v : s) placed += v >= 0;
        CHECK(placed == 22);
        std::vector<int> many(33);
        for (int i = 0; i < 33; ++i) many[i] = i;
        CHECK(!tt::layoutArrangement({}, many, s));
    }
    {
        std::vector<int> s = emptySlots();
        CHECK(tt::chooseFreeSlot(s, -1) == 15);
        CHECK(tt::chooseFreeSlot(s, 7) == 7);
        s[15] = 1;
        CHECK(tt::chooseFreeSlot(s, -1) == 13);
        s[7] = 2;
        CHECK(tt::chooseFreeSlot(s, 7) == 13);
        std::vector<int> top = emptySlots();
        for (int i = 0; i < 16; ++i) top[i] = 100 + i;
        CHECK(tt::chooseFreeSlot(top, -1) == 31);
        std::vector<int> full(32, 5);
        CHECK(tt::chooseFreeSlot(full, -1) == -1);
    }
    {
        std::vector<int> s = emptySlots();
        s[0] = 1; s[1] = 2; s[2] = 3;
        CHECK(tt::moveTile(s, 0, 5, false) && s[0] == -1 && s[5] == 1);
        s = emptySlots();
        s[0] = 1; s[1] = 2; s[2] = 3;
        CHECK(tt::moveTile(s, 0, 2, false));            // 1 before 3 inside the group: a list move, no hole
        CHECK(s[0] == 2 && s[1] == 1 && s[2] == 3 && s[3] == -1);
        s = emptySlots();
        s[0] = 1; s[1] = 2; s[2] = 3;
        CHECK(tt::moveTile(s, 2, 0, false));
        CHECK(s[0] == 3 && s[1] == 1 && s[2] == 2);
        s = emptySlots();
        s[0] = 1; s[1] = 2; s[2] = 3; s[3] = 4;
        CHECK(tt::moveTile(s, 3, 1, true));
        CHECK(s[0] == 1 && s[1] == 2 && s[2] == 4 && s[3] == 3);
        s = emptySlots();
        for (int i = 0; i < 16; ++i) s[i] = 100 + i;
        s[17] = 7;
        CHECK(tt::moveTile(s, 17, 5, false));
        CHECK(s[5] == 7 && s[17] == 105);
        s = emptySlots();
        for (int i = 1; i < 16; ++i) s[i] = 100 + i;
        s[20] = 9;
        CHECK(tt::moveTile(s, 20, 8, false));
        CHECK(s[7] == 9 && s[0] == 101 && s[8] == 108 && s[20] == -1);
        CHECK(!tt::moveTile(s, 3, 40, false));
        CHECK(!tt::moveTile(s, 20, 3, false));
    }
    // inserting keeps the groups apart: the tiles on the way move together with their gaps
    {
        auto row0 = [](const std::vector<int>& s) { return std::vector<int>(s.begin(), s.begin() + 16); };
        auto rowOf = [](std::initializer_list<int> v) {
            std::vector<int> r(v);
            r.resize(16, -1);
            return r;
        };
        std::vector<int> base = emptySlots();
        const int shape[] = {20, 21, 22, -1, 30, 31, 32, -1, 40, 41, 42};
        for (int i = 0; i < 11; ++i) base[i] = shape[i];
        base[16] = 99;
        std::vector<int> s = base;
        CHECK(tt::moveTile(s, 16, 4, false));           // 99 before 30
        CHECK(row0(s) == rowOf({20, 21, 22, -1, 99, 30, 31, 32, -1, 40, 41, 42}) && s[16] == -1);
        CHECK(tt::parseGroups(s).size() == 3);
        s = base;
        CHECK(tt::moveTile(s, 16, 2, true));            // 99 after 22
        CHECK(row0(s) == rowOf({20, 21, 22, 99, -1, 30, 31, 32, -1, 40, 41, 42}));
        s = base;
        CHECK(tt::moveTile(s, 16, 0, false));           // 99 before 20 (a drawn tile dropped on slot 0 too)
        CHECK(row0(s) == rowOf({99, 20, 21, 22, -1, 30, 31, 32, -1, 40, 41, 42}));
        // moves inside one group
        std::vector<int> g = emptySlots();
        for (int i = 0; i < 6; ++i) g[i] = 10 + i;
        s = g;
        CHECK(tt::moveTile(s, 2, 3, false) && s == g);  // 12 on the left half of 13: nothing changes
        s = g;
        CHECK(tt::moveTile(s, 2, 1, true) && s == g);   // 12 on the right half of 11: nothing changes
        s = g;
        CHECK(tt::moveTile(s, 1, 3, true));             // 11 after 13
        CHECK(row0(s) == rowOf({10, 12, 13, 11, 14, 15}));
        s = g;
        CHECK(tt::moveTile(s, 4, 1, false));            // 14 before 11
        CHECK(row0(s) == rowOf({10, 14, 11, 12, 13, 15}));
        // across a gap: the source slot empties, the target group opens up
        std::vector<int> h = emptySlots();
        h[0] = 10; h[1] = 11; h[3] = 20; h[4] = 21;
        CHECK(tt::moveTile(h, 0, 4, false));
        CHECK(row0(h) == rowOf({-1, 11, -1, 20, 10, 21}));
    }
    // "Çift Diz" with 10 or more pairs: no pair is split across the rows or merged with a neighbour
    {
        auto intactPairs = [](const std::vector<int>& s) {
            int n = 0;
            for (const auto& gr : tt::parseGroups(s))
                if (gr.size() == 2 && gr[0] / 2 == gr[1] / 2 && gr[0] % 2 == 0 && gr[0] < 100) ++n;
            return n;
        };
        for (int np : {9, 10, 11}) {
            std::vector<std::vector<int>> melds;
            for (int i = 0; i < np; ++i) melds.push_back({2 * i, 2 * i + 1});
            std::vector<int> left;
            for (int i = 2 * np; i < 22; ++i) left.push_back(100 + i); // leftovers never look like a pair
            std::vector<int> s;
            CHECK(tt::layoutArrangement(melds, left, s));
            int placed = 0;
            for (int v : s) placed += v >= 0;
            CHECK(placed == 22);
            CHECK(intactPairs(s) == std::min(np, 10));
        }
    }
}

// ---------------------------------------------------------------- scripted mouse
void clickAt(Harness& h, Vector2 v) {
    h.frame(1.f / 60.f, v, 0);
    h.frame(1.f / 60.f, v, 1);
    h.frame(1.f / 60.f, v, 3);
}
void doubleClickAt(Harness& h, Vector2 v) {
    h.frame(1.f / 60.f, v, 0);
    h.frame(1.f / 60.f, v, 1);
    h.frame(1.f / 60.f, v, 3);
    h.frame(1.f / 60.f, v, 1);
    h.frame(1.f / 60.f, v, 3);
}
// press at a, move toward b in `steps` frames (left held), no release
void moveHeld(Harness& h, Vector2 a, Vector2 b, int steps) {
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        h.frame(1.f / 60.f, {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, 2);
    }
    for (int i = 0; i < 12; ++i) h.frame(1.f / 60.f, b, 2);
}
void dragTo(Harness& h, Vector2 a, Vector2 b, int steps) {
    h.frame(1.f / 60.f, a, 0);
    h.frame(1.f / 60.f, a, 1);
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        h.frame(1.f / 60.f, {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, 2);
    }
    for (int i = 0; i < 12; ++i) h.frame(1.f / 60.f, b, 2); // let the tile catch up
}
void release(Harness& h, Vector2 b) { h.frame(1.f / 60.f, b, 3); }

int slotOf(Harness& h, int id) {
    const std::vector<int> s = tt::rackSlots(h.table);
    for (int i = 0; i < (int)s.size(); ++i)
        if (s[i] == id) return i;
    return -1;
}

// the human's turn through the mouse: click the pile, double-click a safe tile
void humanTurnUI(Harness& h) {
    if (h.game.current() != 0 || h.game.handState() != okey::HandState::Playing) return;
    const size_t before = h.game.player(0).hand.size();
    if (h.game.stage() == okey::TurnStage::NeedDraw) {
        h.render(h.seat(), false);
        clickAt(h, h.project(tt::pileTopPoint(h.table)));
        check(h.game.stage() == okey::TurnStage::Play, "click on the pile draws");
        check(h.game.player(0).hand.size() == before + 1, "drawn tile is in hand");
        h.settle();
    }
    const okey::BotAction a = okey::fallbackAction(h.game, 0);
    if (a.kind != okey::BotAction::Kind::Discard) {
        okey::applyBotAction(h.game, 0, a);
        h.settle();
        return;
    }
    h.render(h.seat(), false);
    doubleClickAt(h, h.project(tt::tileFaceCenter(h.table, a.tile)));
    if (tt::confirmActive(h.table)) tt::pressButton(h.table, 6);
    const auto& d = h.game.player(0).discards;
    check(!d.empty() && d.back() == a.tile, "double-click discards the tile");
    h.settle();
}

// melds for every seat on the table (debug hooks), keeping all 106 tiles consistent
void buildCrowded(okey::Game& g) {
    const okey::OkeyInfo ok = g.okey();
    auto take = [&](int id) {
        for (int s = 0; s < 4; ++s) {
            auto& hd = g.debugPlayer(s).hand;
            hd.erase(std::remove(hd.begin(), hd.end(), id), hd.end());
            auto& dd = g.debugPlayer(s).discards;
            dd.erase(std::remove(dd.begin(), dd.end(), id), dd.end());
        }
        auto& p = g.debugPile();
        p.erase(std::remove(p.begin(), p.end(), id), p.end());
    };
    auto usable = [&](int id) {
        if (id == ok.indicatorId || ok.isJoker(id)) return false;
        for (const okey::Meld& m : g.table())
            for (const okey::PlacedTile& t : m.tiles)
                if (t.id == id) return false;
        return true;
    };
    auto addMeld = [&](int owner, std::vector<int> ids, bool pair) {
        for (int id : ids)
            if (!usable(id)) return;
        okey::Meld m;
        if (!okey::makeMeld(ids, ok, m, pair)) return;
        for (int id : ids) take(id);
        m.owner = owner;
        g.debugTable().push_back(m);
        g.debugPlayer(owner).opened = true;
        g.debugPlayer(owner).openValue += pair ? 1 : m.value();
        g.debugPlayer(owner).openedWithPairs = pair;
    };
    using okey::makeTileId;
    // human: three runs + a group
    addMeld(0, {makeTileId(3, 9, 0), makeTileId(3, 10, 0), makeTileId(3, 11, 0), makeTileId(3, 12, 0)}, false);
    addMeld(0, {makeTileId(1, 3, 0), makeTileId(1, 4, 0), makeTileId(1, 5, 0)}, false);
    addMeld(0, {makeTileId(0, 7, 0), makeTileId(1, 7, 0), makeTileId(2, 7, 0)}, false);
    // seat 1: runs incl. a joker
    const int j = ok.isJoker(makeTileId(ok.color, ok.number, 0)) ? makeTileId(ok.color, ok.number, 0) : -1;
    addMeld(1, {makeTileId(2, 1, 0), makeTileId(2, 2, 0), makeTileId(2, 3, 0), makeTileId(2, 4, 0), makeTileId(2, 5, 0)}, false);
    addMeld(1, {makeTileId(0, 11, 1), makeTileId(1, 11, 1), makeTileId(2, 11, 1), makeTileId(3, 11, 1)}, false);
    bool jokerOnTable = false;
    for (const okey::Meld& m : g.table())
        for (const okey::PlacedTile& t : m.tiles)
            if (t.id == j) jokerOnTable = true;
    if (j >= 0 && !jokerOnTable) {
        okey::Meld m;
        std::vector<int> ids = {makeTileId(0, 2, 1), j, makeTileId(0, 4, 1)};
        bool free = true;
        for (int id : ids)
            if (id != j && !usable(id)) free = false;
        if (free && okey::makeMeld(ids, ok, m, false)) {
            for (int id : ids) take(id);
            m.owner = 1;
            g.debugTable().push_back(m);
        }
    }
    // seat 2: many melds (auto-scaling)
    addMeld(2, {makeTileId(1, 9, 1), makeTileId(1, 10, 1), makeTileId(1, 11, 0), makeTileId(1, 12, 0), makeTileId(1, 13, 0)}, false);
    addMeld(2, {makeTileId(0, 3, 0), makeTileId(0, 4, 0), makeTileId(0, 5, 0), makeTileId(0, 6, 0)}, false);
    addMeld(2, {makeTileId(3, 1, 0), makeTileId(3, 2, 0), makeTileId(3, 3, 0)}, false);
    addMeld(2, {makeTileId(0, 13, 1), makeTileId(2, 13, 1), makeTileId(3, 13, 1)}, false);
    addMeld(2, {makeTileId(2, 8, 0), makeTileId(2, 9, 0), makeTileId(2, 10, 0), makeTileId(2, 11, 0)}, false);
    // seat 3: pairs
    addMeld(3, {makeTileId(1, 6, 0), makeTileId(1, 6, 1)}, true);
    addMeld(3, {makeTileId(3, 5, 0), makeTileId(3, 5, 1)}, true);
    addMeld(3, {makeTileId(0, 9, 0), makeTileId(0, 9, 1)}, true);
    addMeld(3, {makeTileId(2, 6, 0), makeTileId(2, 6, 1)}, true);
    addMeld(3, {makeTileId(0, 12, 0), makeTileId(0, 12, 1)}, true);
    for (int s = 0; s < 4; ++s) g.debugPlayer(s).openedTurn = -5;
}

// ---------------------------------------------------------------- positions for scripted scenarios
// Where a tile is: 0..3 that seat's hand, 4 pile, 5 a discard pile, 6 a table meld, 7 the indicator.
int whereIs(okey::Game& g, int id) {
    if (id == g.okey().indicatorId) return 7;
    for (const okey::Meld& m : g.table())
        for (const okey::PlacedTile& t : m.tiles)
            if (t.id == id) return 6;
    for (int s = 0; s < 4; ++s) {
        const auto& hd = g.player(s).hand;
        if (std::find(hd.begin(), hd.end(), id) != hd.end()) return s;
        const auto& dd = g.player(s).discards;
        if (std::find(dd.begin(), dd.end(), id) != dd.end()) return 5;
    }
    return 4;
}
// A copy of (color, number) held by someone or lying in the pile (never a joker, a discard or a meld).
int freeCopy(okey::Game& g, int color, int number, const std::vector<int>& exclude) {
    if (number == 14) number = 1;
    for (int copy = 0; copy < 2; ++copy) {
        const int id = okey::makeTileId(color, number, copy);
        if (g.okey().isJoker(id) || std::find(exclude.begin(), exclude.end(), id) != exclude.end()) continue;
        if (whereIs(g, id) <= 4) return id;
    }
    return -1;
}
std::vector<int> freeRun(okey::Game& g, int color, int from, int to, const std::vector<int>& exclude) {
    std::vector<int> r;
    for (int n = from; n <= to; ++n) {
        const int id = freeCopy(g, color, n, exclude);
        if (id < 0) return {};
        r.push_back(id);
    }
    return r;
}
// Moves `ids` into the human's hand; each one trades places with a human tile that isn't wanted, so all
// 106 tiles stay consistent (the pile and the bots' hands keep their sizes).
bool giveHuman(okey::Game& g, const std::vector<int>& ids) {
    for (int id : ids) {
        const int w = whereIs(g, id);
        if (w == 0) continue;
        if (w > 4) return false;
        auto& hand = g.debugPlayer(0).hand;
        int out = -1;
        for (int t : hand)
            if (std::find(ids.begin(), ids.end(), t) == ids.end() && t != g.pendingLeftTile()) out = t;
        if (out < 0) return false;
        std::vector<int>& from = w == 4 ? g.debugPile() : g.debugPlayer(w).hand;
        *std::find(from.begin(), from.end(), id) = out;
        *std::find(hand.begin(), hand.end(), out) = id;
    }
    return true;
}

} // namespace

int main() {
    Harness H;
    if (!H.init()) {
        std::printf("init failed\n");
        return 1;
    }
    Harness& h = H;
    MakeDirectory(OUT_DIR);
    unitRackLogic();
    h.render(h.seat(), false); // projectToVirtual uses the last rendered camera

    // ------------------------------------------------ title backdrop: the heap before any game
    h.frame();
    h.shot("t0_heap_seat", h.seat(), false);

    // ------------------------------------------------ deal
    h.game.startMatch(20260929u);
    h.pump();
    for (int i = 0; i < 70; ++i) h.frame();
    h.shot("t1_shuffle", h.seat());
    for (int i = 0; i < 40; ++i) h.frame();
    h.shot("t2_dealing", h.seat());
    h.settle();
    check(tt::validate(h.table, true).empty(), "after deal: " + tt::validate(h.table, true));
    h.shot("t3_after_deal", h.seat());
    h.shot("t3_across", makeCam({0.25f, 1.75f, -1.45f}, {0.f, 0.78f, 0.25f}, 55.f));
    h.shot("t3_corner", makeCam({2.2f, 1.9f, 2.0f}, {0.f, 0.8f, 0.f}, 50.f));
    h.shot("t3_closeup", makeCam({0.f, 1.21f, 0.85f}, {0.f, 0.82f, 0.47f}, 30.f));

    // ------------------------------------------------ ray picking: every rack tile projects and picks back
    {
        h.render(h.seat(), false);
        const std::vector<int> slots = tt::rackSlots(h.table);
        int tested = 0, ok = 0;
        for (int i = 0; i < (int)slots.size(); ++i) {
            if (slots[i] < 0) continue;
            ++tested;
            const Vector2 v = h.project(tt::tileFaceCenter(h.table, slots[i]));
            const int hit = tt::rackTileUnderRay(h.table, h.R.rayFromVirtual(v));
            // and through the real input path (hover)
            h.frame(1.f / 60.f, v, 0);
            const int hov = tt::hoverTile(h.table);
            if (hit == slots[i] && hov == slots[i]) ++ok;
            else check(false, "pick slot " + std::to_string(i) + " tile " + std::to_string(slots[i]) + " -> ray " +
                                  std::to_string(hit) + " hover " + std::to_string(hov));
        }
        std::printf("ray picking: %d/%d rack tiles picked back\n", ok, tested);
        check(tested >= 21, "rack has the dealt tiles");
    }

    // ------------------------------------------------ play a few rounds (the human through the mouse)
    for (int round = 0; round < 3 && h.game.handState() == okey::HandState::Playing; ++round) {
        h.botsUntilHuman(true);
        humanTurnUI(h);
        const std::string v = tt::validate(h.table, true);
        check(v.empty(), "mid-hand validate: " + v);
    }
    h.botsUntilHuman(true);
    h.shot("t4_midhand", h.seat());

    // hover a rack tile (+ drawing now: pile highlighted)
    {
        const std::vector<int> slots = tt::rackSlots(h.table);
        int id = -1;
        for (int i = 20; i >= 0; --i)
            if (slots[i] >= 0) {
                id = slots[i];
                break;
            }
        h.render(h.seat(), false);
        const Vector2 v = h.project(tt::tileFaceCenter(h.table, id));
        for (int i = 0; i < 20; ++i) h.frame(1.f / 60.f, v, 0);
        check(tt::hoverTile(h.table) == id, "hover picks the tile under the mouse");
        h.shot("t5_hover", h.seat());
        // draw by dragging the pile top onto the rack's bottom row left end
        h.render(h.seat(), false);
        const Vector2 a = h.project(tt::pileTopPoint(h.table));
        const Vector2 b = h.project(tt::slotFaceCenter(h.table, 16));
        const size_t before = h.game.player(0).hand.size();
        dragTo(h, a, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, 14);
        check(tt::draggedTile(h.table) >= 0 && h.table.mouseBusy(), "dragging the pile top");
        h.shot("t6_drag_pile", h.seat());
        moveHeld(h, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, b, 10);
        release(h, b);
        check(h.game.player(0).hand.size() == before + 1, "pile dragged onto the rack draws");
        h.settle();
        const std::vector<int> s2 = tt::rackSlots(h.table);
        check(s2[16] >= 0, "drawn tile placed where it was dropped (slot 16)");
    }
    // drag a rack tile over the felt, then onto the discard pile
    {
        const okey::BotAction a = okey::fallbackAction(h.game, 0);
        const int id = a.tile;
        h.render(h.seat(), false);
        const Vector2 from = h.project(tt::tileFaceCenter(h.table, id));
        const Vector2 mid = h.project({0.12f, w3d::TABLE_Y, 0.22f});
        dragTo(h, from, mid, 16);
        check(tt::draggedTile(h.table) == id, "dragging the tile");
        h.shot("t7_drag_table", h.seat());
        const Vector2 to = h.project({w3d::DISCARD_POS[0].x, w3d::TABLE_Y + 0.045f, w3d::DISCARD_POS[0].z});
        moveHeld(h, mid, to, 12);
        h.shot("t8_drag_discard", h.seat());
        release(h, to);
        if (tt::confirmActive(h.table)) tt::pressButton(h.table, 6);
        check(!h.game.player(0).discards.empty() && h.game.player(0).discards.back() == id, "drop on the discard pile discards");
        const Camera3D zoom = makeCam({0.f, 1.21f, 0.85f}, {0.30f, 0.80f, 0.33f}, 30.f);
        h.strip("f5_drop_discard", 6, 2, &zoom);
        h.settle();
    }
    // flights: a bot discarding, then the human drawing from the pile (contact sheets)
    {
        int guard = 0;
        while (h.game.handState() == okey::HandState::Playing && h.game.current() != 0 && guard++ < 40) {
            const int s = h.game.current();
            okey::BotAction a = h.bots[s]->next(h.game, s);
            if (a.kind == okey::BotAction::Kind::Discard && s == 2) {
                okey::applyBotAction(h.game, s, a);
                h.pump();
                const Camera3D zoom = makeCam({0.f, 1.21f, 0.85f}, {-0.22f, 0.80f, -0.28f}, 30.f);
                h.strip("f1_bot_discard", 9, 3, &zoom);
                h.settle();
                continue;
            }
            okey::ActionResult r = okey::applyBotAction(h.game, s, a);
            if (!r.ok) okey::applyBotAction(h.game, s, okey::fallbackAction(h.game, s));
            h.pump();
            h.settle();
        }
        if (h.game.current() == 0 && h.game.stage() == okey::TurnStage::NeedDraw) {
            h.render(h.seat(), false);
            clickAt(h, h.project(tt::pileTopPoint(h.table)));
            const Camera3D side = makeCam({0.55f, 1.0f, 0.62f}, {0.f, 0.80f, 0.40f}, 45.f);
            h.strip("f2_human_draw", 9, 3, &side);
            h.settle();
        }
    }
    // rearrange within the rack (insert caret)
    {
        h.botsUntilHuman(true);
        const std::vector<int> s = tt::rackSlots(h.table);
        int from = -1;
        for (int i = 0; i < 16; ++i)
            if (s[i] >= 0) {
                from = i;
                break;
            }
        int to = -1;
        for (int i = 20; i < 32; ++i)
            if (s[i] >= 0 && i != from) to = i;
        if (from >= 0 && to >= 0) {
            const int id = s[from];
            h.render(h.seat(), false);
            const Vector2 a = h.project(tt::tileFaceCenter(h.table, id));
            const Vector2 b = h.project(tt::slotFaceCenter(h.table, to));
            dragTo(h, a, {b.x - 8.f, b.y}, 18);
            h.shot("t9_drag_rack", h.seat());
            release(h, {b.x - 8.f, b.y});
            h.settle();
            check(slotOf(h, id) >= 16, "tile moved to the bottom row");
        }
    }

    // ------------------------------------------------ El Aç: runs worth >= 101 on the upper tier, opened with Enter
    {
        h.botsUntilHuman(true);
        if (h.game.handState() == okey::HandState::Playing && h.game.current() == 0 && !h.game.player(0).opened) {
            if (h.game.stage() == okey::TurnStage::NeedDraw) {
                h.render(h.seat(), false);
                clickAt(h, h.project(tt::pileTopPoint(h.table)));
                h.settle();
            }
            // greedy: the most valuable free runs (3..5 long) until the total passes 101
            std::vector<std::vector<int>> runs;
            std::vector<int> used;
            int total = 0;
            while (total < 110 && runs.size() < 5) {
                std::vector<int> best;
                int bestV = 0;
                for (int c = 0; c < 4; ++c)
                    for (int hi = 13; hi >= 3; --hi)
                        for (int len = 5; len >= 3; --len) {
                            if (hi - len + 1 < 1) continue;
                            const std::vector<int> r = freeRun(h.game, c, hi - len + 1, hi, used);
                            const int v = r.empty() ? 0 : len * (2 * hi - len + 1) / 2;
                            if (v > bestV) {
                                bestV = v;
                                best = r;
                            }
                        }
                if (best.empty()) break;
                runs.push_back(best);
                used.insert(used.end(), best.begin(), best.end());
                total += bestV;
            }
            std::printf("opening: %d runs worth %d\n", (int)runs.size(), total);
            check(total >= 101, "runs worth >= 101 found for the opening");
            check(giveHuman(h.game, used), "the opening tiles were handed to the human");
            std::vector<int> rest;
            for (int id : h.game.player(0).hand)
                if (std::find(used.begin(), used.end(), id) == used.end()) rest.push_back(id);
            std::vector<int> slots;
            check(tt::layoutArrangement(runs, rest, slots), "opening rack layout fits");
            tt::setRackSlots(h.table, slots);
            h.settle();
            check(tt::validate(h.table, true).empty(), "open setup: " + tt::validate(h.table, true));
            h.shot("t17_open_ready", h.seat());
            const size_t before = h.game.table().size();
            h.frame(1.f / 60.f, {-10000, -10000}, 0, '\n'); // Enter = El Aç
            check(h.game.player(0).opened, "Enter opens the hand from the valid rack groups");
            check(h.game.table().size() == before + runs.size(), "every valid group went to the table");
            h.strip("f3_open", 9, 3);
            h.settle();
            h.shot("t18_opened", h.seat());
            h.shot("t18_opened_top", makeCam({0.f, 1.55f, 0.72f}, {0.f, 0.76f, 0.12f}, 50.f));
            humanTurnUI(h);
            check(tt::validate(h.table, true).empty(), "after opening: " + tt::validate(h.table, true));
        }
    }
    // ------------------------------------------------ yandan al + Geri Ver (+101)
    {
        h.botsUntilHuman(true);
        if (h.game.handState() == okey::HandState::Playing && h.game.current() == 0 && h.game.canTakeFromLeft(0)) {
            const int top = h.game.topDiscard(okey::Game::leftOf(0));
            const int pen = h.game.player(0).handPenalty;
            h.render(h.seat(), false);
            clickAt(h, h.project(tt::leftTopPoint(h.table)));
            check(h.game.pendingLeftTile() == top, "click on the left pile takes its top tile");
            h.settle();
            h.shot("t19_pending_left", h.seat());
            tt::pressButton(h.table, 2);
            check(h.game.pendingLeftTile() < 0 && h.game.player(0).handPenalty == pen + h.game.rules().penalty,
                  "Geri Ver returns the tile with a penalty");
            check(h.game.topDiscard(okey::Game::leftOf(0)) == top, "the tile is back on the left pile");
            h.settle();
            h.shot("t20_gave_back", h.seat());
            check(tt::validate(h.table, true).empty(), "after Geri Ver: " + tt::validate(h.table, true));
            humanTurnUI(h);
        }
    }

    // ------------------------------------------------ play the hand out (reveal at the end)
    {
        int guard = 0;
        while (h.game.handState() == okey::HandState::Playing && guard++ < 300) {
            if (h.game.current() == 0) humanTurnUI(h);
            else h.botStep();
            h.settle();
        }
        check(h.table.isAnimating() == false, "the table is at rest after the hand-over reveal");
        for (int i = 0; i < 150; ++i) h.frame();
        h.shot("t13_hand_over", h.seat());
        const std::string v = tt::validate(h.table, true);
        check(v.empty(), "hand over validate: " + v);
    }
    // next hand: gather -> shuffle -> deal
    if (h.game.handState() == okey::HandState::HandOver) {
        h.game.startNextHand();
        h.pump();
        for (int i = 0; i < 20; ++i) h.frame();
        h.shot("t14_gather", h.seat());
        h.settle();
        check(tt::validate(h.table, true).empty(), "second deal: " + tt::validate(h.table, true));
        h.shot("t15_second_deal", h.seat());
    }
    // ------------------------------------------------ crowded table (on the human's turn)
    h.botsUntilHuman(true);
    buildCrowded(h.game);
    h.settle();
    {
        const std::string v = tt::validate(h.table, true);
        check(v.empty(), "crowded validate: " + v);
    }
    h.shot("t10_crowded", h.seat());
    h.shot("t10_crowded_top", makeCam({0.f, 1.95f, 0.35f}, {0.f, 0.76f, -0.02f}, 50.f));
    // hover-peek a far meld (Kel Mahmut's) and a discard pile
    for (int i = 0; i < (int)h.game.table().size(); ++i) {
        if (h.game.table()[i].owner != 2) continue;
        h.render(h.seat(), false);
        const Vector2 v = h.project(tt::meldPoint(h.table, i, 0.5f));
        for (int k = 0; k < 40; ++k) h.frame(1.f / 60.f, v, 0);
        h.shot("t10_peek_meld", h.seat());
        break;
    }
    {
        h.render(h.seat(), false);
        const Vector2 v = h.project({w3d::DISCARD_POS[3].x, w3d::TABLE_Y + 0.02f, w3d::DISCARD_POS[3].z});
        for (int k = 0; k < 40; ++k) h.frame(1.f / 60.f, v, 0);
        h.shot("t10_peek_discard", h.seat());
    }
    // drag onto a meld: a tile that doesn't fit (red), one that extends a run (green), one that frees a joker
    {
        if (h.game.current() == 0 && h.game.stage() == okey::TurnStage::NeedDraw) {
            h.render(h.seat(), false);
            clickAt(h, h.project(tt::pileTopPoint(h.table)));
            h.settle();
        }
        check(h.game.current() == 0 && h.game.stage() == okey::TurnStage::Play && h.game.canWorkTable(0),
              "the human may work the table now (hand state " + std::to_string((int)h.game.handState()) + ", seat " +
                  std::to_string(h.game.current()) + ", stage " + std::to_string((int)h.game.stage()) + ")");
        const okey::OkeyInfo ok = h.game.okey();
        // (a) no fit: the first rack tile that fits nowhere, dropped on meld 0
        {
            int bad = -1;
            for (int t : tt::rackSlots(h.table)) {
                if (t < 0 || ok.isJoker(t)) continue;
                okey::Meld out;
                if (!okey::tryAddTile(h.game.table()[0], t, ok, okey::AddSide::Auto, out)) {
                    bad = t;
                    break;
                }
            }
            if (bad >= 0) {
                h.render(h.seat(), false);
                const Vector2 a = h.project(tt::tileFaceCenter(h.table, bad));
                const Vector3 mp = tt::meldPoint(h.table, 0, 0.9f);
                const Vector2 b = h.project({mp.x, w3d::TABLE_Y + 0.045f, mp.z});
                dragTo(h, a, b, 18);
                h.shot("t11a_drag_meld_bad", h.seat());
                const size_t n0 = h.game.table()[0].tiles.size();
                release(h, b);
                check(h.game.table()[0].tiles.size() == n0 && !tt::lastToast(h.table).empty(), "a bad işle is refused with a toast");
                h.settle();
            }
        }
        // (b) işle: the next tile of some run, dropped on its back half
        {
            int meld = -1, tile = -1;
            for (int i = 0; i < (int)h.game.table().size() && tile < 0; ++i) {
                const okey::Meld& m = h.game.table()[i];
                if (m.kind != okey::MeldKind::Run || m.tiles.back().number >= 13) continue;
                const int id = freeCopy(h.game, m.tiles.back().color, m.tiles.back().number + 1, {});
                okey::Meld out;
                if (id >= 0 && okey::tryAddTile(m, id, ok, okey::AddSide::Back, out)) {
                    meld = i;
                    tile = id;
                }
            }
            check(tile >= 0 && giveHuman(h.game, {tile}), "a tile that extends a run was handed to the human");
            h.settle();
            if (tile >= 0) {
                h.render(h.seat(), false);
                const Vector2 a = h.project(tt::tileFaceCenter(h.table, tile));
                const Vector3 mp = tt::meldPoint(h.table, meld, 0.9f);
                const Vector2 b = h.project({mp.x, w3d::TABLE_Y + 0.045f, mp.z});
                dragTo(h, a, b, 18);
                h.shot("t11_drag_meld", h.seat());
                const size_t n0 = h.game.table()[meld].tiles.size();
                release(h, b);
                check(h.game.table()[meld].tiles.size() == n0 + 1 && h.game.table()[meld].tiles.back().id == tile,
                      "dropping on the run's back half adds the tile there");
                h.strip("f4_isle", 6, 3);
                h.settle();
            }
        }
        // (c) okey alma: the real tile a joker stands for, dropped onto the joker
        {
            int joker = -1, tile = -1;
            for (int i = 0; i < (int)h.game.table().size() && tile < 0; ++i) {
                const okey::Meld& m = h.game.table()[i];
                if (m.kind == okey::MeldKind::Pair) continue;
                for (const okey::PlacedTile& pt : m.tiles) {
                    if (!pt.joker) continue;
                    const int id = freeCopy(h.game, pt.color, pt.number, {});
                    okey::Meld out;
                    int freed = -1;
                    if (id >= 0 && okey::trySwapJoker(m, id, ok, out, freed)) {
                        joker = pt.id;
                        tile = id;
                        break;
                    }
                }
            }
            if (tile >= 0 && giveHuman(h.game, {tile})) {
                h.settle();
                h.render(h.seat(), false);
                const Vector2 a = h.project(tt::tileFaceCenter(h.table, tile));
                const Vector3 jc = tt::tileCenter(h.table, joker);
                const Vector2 b = h.project({jc.x, w3d::TABLE_Y + 0.045f, jc.z});
                dragTo(h, a, b, 18);
                h.shot("t11c_drag_swap", h.seat());
                release(h, b);
                const auto& hand = h.game.player(0).hand;
                check(std::find(hand.begin(), hand.end(), joker) != hand.end(), "dropping onto the joker takes it (okey alma)");
                h.settle();
                h.shot("t11d_after_swap", h.seat());
            } else {
                std::printf("note: no joker swap possible in this position\n");
            }
        }
    }
    // confirm modal: discard a tile that fits a table meld
    {
        if (h.game.current() == 0 && h.game.stage() == okey::TurnStage::Play) {
            int islek = -1;
            for (int t : h.game.player(0).hand)
                if (!h.game.okey().isJoker(t) && h.game.isPlayableOnTable(t)) islek = t;
            if (islek < 0) {
                // give the human a tile that fits: the next tile of the first run
                for (int id = 0; id < 104 && islek < 0; ++id) {
                    bool free = true;
                    for (const okey::Meld& m : h.game.table())
                        for (const okey::PlacedTile& pt : m.tiles)
                            if (pt.id == id) free = false;
                    if (!free || id == h.game.okey().indicatorId || h.game.okey().isJoker(id)) continue;
                    if (!h.game.isPlayableOnTable(id)) continue;
                    for (int s = 0; s < 4; ++s) {
                        auto& hd = h.game.debugPlayer(s).hand;
                        hd.erase(std::remove(hd.begin(), hd.end(), id), hd.end());
                        auto& dd = h.game.debugPlayer(s).discards;
                        dd.erase(std::remove(dd.begin(), dd.end(), id), dd.end());
                    }
                    auto& p = h.game.debugPile();
                    p.erase(std::remove(p.begin(), p.end(), id), p.end());
                    h.game.debugPlayer(0).hand.push_back(id);
                    islek = id;
                }
                h.settle();
            }
            if (islek >= 0) {
                h.render(h.seat(), false);
                doubleClickAt(h, h.project(tt::tileFaceCenter(h.table, islek)));
                check(tt::confirmActive(h.table), "işlek discard asks for confirmation");
                for (int i = 0; i < 20; ++i) h.frame(1.f / 60.f, {700, 500}, 0);
                h.shot("t12_confirm", h.seat());
                tt::pressButton(h.table, 7);
                check(!tt::confirmActive(h.table), "Vazgeç closes the modal");
            }
        }
    }

    // per-frame CPU cost of the table (update with a hovered tile + submission)
    {
        h.render(h.seat(), false);
        const std::vector<int> slots = tt::rackSlots(h.table);
        int id = -1;
        for (int t : slots)
            if (t >= 0) id = t;
        const Vector2 v = id >= 0 ? h.project(tt::tileFaceCenter(h.table, id)) : Vector2{800, 700};
        double tu = 0.0, ts = 0.0;
        const int N = 240;
        for (int i = 0; i < N; ++i) {
            const double a = GetTime();
            tt::input(h.table, 1.f / 60.f, v, 0, 0, true, h.seat());
            const double b = GetTime();
            h.table.submit(h.R);
            const double c = GetTime();
            h.R.render(h.seat(), &h.rt, BLACK);
            tu += b - a;
            ts += c - b;
        }
        std::printf("table cpu: update %.3f ms, submit %.3f ms per frame (%d submissions)\n", tu / N * 1000.0,
                    ts / N * 1000.0, tt::submitCount(h.table));
        check(tt::submitCount(h.table) <= 200, "Table3D stays within 200 draw submissions");
    }
    // hand 2 is played out quickly (no shots), then the match state is left as it is
    {
        int guard = 0;
        while (h.game.handState() == okey::HandState::Playing && guard++ < 300) {
            if (h.game.current() == 0) humanTurnUI(h);
            else h.botStep();
            h.settle();
        }
        for (int i = 0; i < 150; ++i) h.frame(); // the bots' istakas turn around
        const std::string v = tt::validate(h.table, true);
        check(v.empty(), "hand 2 over validate: " + v);
    }
    // soak: a fresh match with every seat played by bots (autoplay), the table checked against the game
    // after every single action (events of all kinds: opening, laying, işleme, joker swaps, give-backs)
    {
        h.bots[0] = std::make_unique<okey::Bot>(okey::BotLevel::Hard, 4242u);
        h.game.startMatch(777u);
        h.pump();
        int actions = 0, hands = 0, rejected = 0;
        std::array<int, 16> evCount{};
        while (actions < 8000) {
            if (h.game.handState() == okey::HandState::Playing) {
                const int s = h.game.current();
                okey::ActionResult r = okey::applyBotAction(h.game, s, h.bots[s]->next(h.game, s));
                if (!r.ok) {
                    ++rejected;
                    r = okey::applyBotAction(h.game, s, okey::fallbackAction(h.game, s));
                    check(r.ok, "soak: fallback rejected: " + r.error);
                }
                ++actions;
                for (const okey::GameEvent& e : h.game.drainEvents()) { // (pump, counting the event kinds)
                    ++evCount[(int)e.type];
                    h.table.onEvent(e);
                    for (auto& b : h.bots)
                        if (b) b->observe(e, h.game);
                    if (e.type == okey::EvType::HandStart) {
                        for (auto& b : h.bots)
                            if (b) b->resetForHand();
                        h.table.onHandStart();
                    }
                }
                tt::settle(h.table, h.seat(), false);
                const std::string v = tt::validate(h.table, true);
                if (!v.empty()) {
                    check(false, "soak after action " + std::to_string(actions) + ": " + v);
                    break;
                }
            } else if (h.game.handState() == okey::HandState::HandOver) {
                ++hands;
                h.game.startNextHand();
                h.pump();
            } else {
                ++hands; // MatchOver
                break;
            }
        }
        std::printf("soak: %d hands, %d actions checked, %d bot actions rejected; opens %d, lays %d, işle %d, "
                    "joker swaps %d, left takes %d, give-backs %d, penalties %d\n",
                    hands, actions, rejected, evCount[(int)okey::EvType::Open], evCount[(int)okey::EvType::LayMelds],
                    evCount[(int)okey::EvType::AddToMeld], evCount[(int)okey::EvType::SwapJoker],
                    evCount[(int)okey::EvType::TakeLeft], evCount[(int)okey::EvType::ReturnLeft],
                    evCount[(int)okey::EvType::Penalty]);
        check(h.game.handState() == okey::HandState::MatchOver && hands == h.game.numHands(), "soak played a whole match");
        h.shot("t21_soak_end", h.seat());
        h.bots[0].reset();
    }
    // title camera: one cell every 6.5 s of the loop
    {
        PlayerCamera tc;
        tc.setTitleMode(true);
        h.shot("t16_title_start", tc.camera(), false);
        h.sheet("t16_title_loop", 12, [&]() {
            for (int k = 0; k < 390; ++k) tc.update(1.f / 60.f, false);
            return tc.camera();
        });
    }

    std::printf("%s (%d failures, %d sfx)\n", g_failures ? "FAILED" : "OK", g_failures, h.sfxCount);
    UnloadRenderTexture(h.rt);
    return g_failures ? 2 : 0;
}
