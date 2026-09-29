// Characters owner's harness: the real Room + Table3D + a real okey::Game driven by bots, with
// r3d::Characters reacting to the game's events. Renders PNGs into build/characters/shots/:
// the human's seat view, face close-ups, mid-animation contact sheets (reaching for tiles, discarding,
// slapping melds, sipping tea, smoking, the tespih, penalty/celebration), speech bubbles, and wide shots
// of the room with the background patrons and the çaycı serving tea.
//
//   usage: characters_snapshot [outdir] [only-substring | soak]
//   (soak: ten minutes of bot play without rendering; checks for NaNs and stuck glasses/tea rounds)
// Build: sh build/characters/build.sh, then (caffeinate -u -t 3 &); sleep 1; ./build/characters/harness
// Define CHARS_STANDALONE to replace Room/Table3D by placeholder boxes.
#include "core/Bot.h"
#include "core/Game.h"
#include "r3d/Characters.h"
#include "r3d/CharactersState.h"
#include "r3d/Gfx.h"
#include "r3d/World.h"
#include "ui/Common.h"
#ifndef CHARS_STANDALONE
#include "r3d/Room.h"
#include "r3d/Table3D.h"
#endif

#include <raymath.h>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace r3d;

namespace {

Camera3D makeCam(Vector3 pos, Vector3 target, float fovy) {
    Camera3D c{};
    c.position = pos;
    c.target = target;
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}
Camera3D seatCam(float yawDeg = 0.f, float pitchDeg = w3d::EYE_PITCH_DEG, float fovy = w3d::FOVY_DEG) {
    float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    Vector3 f{-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    return makeCam(w3d::EYE, Vector3Add(w3d::EYE, f), fovy);
}

// ---------------------------------------------------------------- placeholder room/table (standalone build)
struct Placeholder {
    Mesh floor{}, top{}, felt{}, leg{}, rack{}, bgTop{}, bgLeg{};
    Mat mFloor{}, mWood{}, mFelt{}, mRack{};
    void init(Renderer& R) {
        floor = genRoundedBox(8.4f, 0.02f, 7.0f, 0.005f, 1);
        top = genRoundedBox(1.24f, 0.05f, 1.24f, 0.02f, 3);
        felt = genRoundedBox(1.1f, 0.01f, 1.1f, 0.003f, 1);
        leg = genRoundedBox(0.07f, 0.72f, 0.07f, 0.01f, 2);
        rack = genRoundedBox(w3d::RACK_LEN, 0.06f, w3d::RACK_DEPTH, 0.008f, 2);
        bgTop = genRoundedBox(0.9f, 0.04f, 0.9f, 0.01f, 2);
        bgLeg = genRoundedBox(0.06f, 0.73f, 0.06f, 0.01f, 2);
        mFloor = R.makeMat(Color{92, 70, 52, 255}, Texture2D{}, 0.1f, 12.f);
        mWood = R.makeMat(Color{110, 66, 36, 255}, Texture2D{}, 0.3f, 30.f);
        mFelt = R.makeMat(Color{30, 88, 56, 255}, Texture2D{}, 0.03f, 6.f);
        mRack = R.makeMat(Color{150, 96, 52, 255}, Texture2D{}, 0.3f, 30.f);
    }
    void submit(Renderer& R) {
        Ambient a;
        a.intensity = 0.5f;
        R.setAmbient(a);
        Fog f;
        f.density = 0.08f;
        R.setFog(f);
        KeyLight k;
        R.setKeyLight(k);
        R.submit(&floor, &mFloor, MatrixTranslate(0, -0.01f, 0.1f), 0);
        R.submit(&top, &mWood, MatrixTranslate(0, w3d::TABLE_Y - 0.03f, 0));
        R.submit(&felt, &mFelt, MatrixTranslate(0, w3d::TABLE_Y - 0.004f, 0));
        for (int i = 0; i < 4; ++i)
            R.submit(&leg, &mWood, MatrixTranslate(i % 2 ? 0.52f : -0.52f, 0.36f, i < 2 ? 0.52f : -0.52f));
        for (int s = 0; s < 4; ++s) {
            Vector3 p = Vector3Scale(w3d::SEAT_DIR[s], w3d::RACK_DIST);
            R.submit(&rack, &mRack, trsYaw({p.x, w3d::TABLE_Y + 0.03f, p.z}, w3d::seatYawDeg(s)));
        }
        for (const w3d::BgTable& t : w3d::BG_TABLES) {
            R.submit(&bgTop, &mWood, MatrixTranslate(t.x, w3d::BG_TABLE_Y - 0.02f, t.z));
            for (int i = 0; i < 4; ++i)
                R.submit(&bgLeg, &mWood, MatrixTranslate(t.x + (i % 2 ? 0.38f : -0.38f), 0.365f, t.z + (i < 2 ? 0.38f : -0.38f)));
        }
        PointLight pl;
        pl.position = {2.5f, 2.2f, 0.f};
        pl.intensity = 0.8f;
        R.addPointLight(pl);
        pl.position = {-2.5f, 2.2f, 0.f};
        R.addPointLight(pl);
    }
};

// ---------------------------------------------------------------- the harness
struct Harness {
    std::string out = "build/characters/shots";
    std::string only;
    Renderer R;
    RenderTexture2D rt{}, sheet{}, small{};
    okey::Game game;
    Characters chars;
    std::array<std::unique_ptr<okey::Bot>, 4> bots;
#ifndef CHARS_STANDALONE
    Room room;
    Table3D table;
#else
    Placeholder ph;
#endif
    Camera3D viewer = seatCam();
    int sfx[(int)ui::Sfx::Count] = {};
    double charMs = 0.0;
    int charFrames = 0;
    int maxSubmits = 0;

    chr::Cast& cast() { return *chr::debugLastCast(); }

    bool init() {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
        InitWindow(320, 180, "characters_snapshot");
        if (!IsWindowReady()) return false;
        ui::loadFonts();
        if (!R.init()) return false;
        rt = LoadRenderTexture(1600, 900);
        sheet = LoadRenderTexture(1600, 900);
        small = LoadRenderTexture(320, 180);
        R.setRenderSize(1600, 900);
        ui::setCurrentViewport(ui::Viewport{});
        auto t0 = std::chrono::steady_clock::now();
        if (!chars.init(R, 0xC0FFEEull)) return false;
        auto t1 = std::chrono::steady_clock::now();
        std::printf("characters init: %.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
        chars.setNames({"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
        chars.playSfx = [this](ui::Sfx s) { ++sfx[(int)s]; };
        for (int s = 0; s < 4; ++s) game.setPlayer(s, s == 0 ? "Sen" : (s == 1 ? "Hacı Rıza" : (s == 2 ? "Kel Mahmut" : "Emekli Nuri")), s == 0);
#ifndef CHARS_STANDALONE
        if (!room.init(R, 1234u)) return false;
        room.setScoreboard("El 1 / 5", {"Sen .......... 0", "Hacı Rıza .... 0", "Kel Mahmut ... 0", "Emekli Nuri .. 0"});
        if (!table.init(R, &game, 0)) return false;
#else
        ph.init(R);
#endif
        for (int s = 1; s < 4; ++s) bots[s] = std::make_unique<okey::Bot>(okey::BotLevel::Normal, 1000u + (unsigned)s);
        return true;
    }

    void pump() {
        for (const okey::GameEvent& e : game.drainEvents()) {
#ifndef CHARS_STANDALONE
            table.onEvent(e);
#endif
            chars.onEvent(e, game);
            if (e.type == okey::EvType::TurnStart) chars.setActiveSeat(e.player);
            for (int s = 1; s < 4; ++s) bots[s]->observe(e, game);
            if (e.type == okey::EvType::HandStart) {
                for (int s = 1; s < 4; ++s) bots[s]->resetForHand();
#ifndef CHARS_STANDALONE
                table.onHandStart();
#endif
            }
        }
    }
    // Inject an event directly (characters only).
    void inject(const okey::GameEvent& e) { chars.onEvent(e, game); }

    void step(float dt = 1.f / 60.f) {
        pump();
#ifndef CHARS_STANDALONE
        room.update(dt);
        if (room.consumeTvGoal()) chars.onTvGoal();
        table.update(dt, viewer, Vector2{-10000, -10000}, false);
#endif
        auto t0 = std::chrono::steady_clock::now();
        chars.update(dt, viewer);
        auto t1 = std::chrono::steady_clock::now();
        charMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        ++charFrames;
        R.update(dt);
    }
    void run(float seconds) {
        int n = (int)std::lround(seconds * 60.f);
        for (int i = 0; i < n; ++i) step();
    }
    // Simulate with rendering into a tiny target (so per-submit particles get emitted and aged like in play).
    void runRendered(float seconds) {
        R.setRenderSize(320, 180);
        int n = (int)std::lround(seconds * 60.f);
        for (int i = 0; i < n; ++i) {
            step();
            submitAll();
            R.render(viewer, &small, BLACK);
        }
        R.setRenderSize(1600, 900);
    }

    void submitAll() {
#ifndef CHARS_STANDALONE
        room.submit(R);
#else
        ph.submit(R);
#endif
        auto t0 = std::chrono::steady_clock::now();
        chars.submit(R);
        auto t1 = std::chrono::steady_clock::now();
        charMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        maxSubmits = std::max(maxSubmits, cast().submitCount);
#ifndef CHARS_STANDALONE
        table.submit(R);
#endif
    }

    void renderTo(const Camera3D& cam, bool overlay) {
        submitAll();
        R.render(cam, &rt, Color{16, 12, 9, 255});
        if (overlay) {
            BeginTextureMode(rt);
            ui::uiBeginFrame();
#ifndef CHARS_STANDALONE
            std::array<Vector3, 4> heads;
            for (int s = 0; s < 4; ++s) heads[s] = chars.headPosition(s);
            table.setHeadAnchors(heads);
#endif
            chars.drawOverlay(R);
#ifndef CHARS_STANDALONE
            table.drawHUD(R);
#endif
            EndTextureMode();
        }
    }
    bool wanted(const std::string& name) const { return only.empty() || name.find(only) != std::string::npos; }
    void save(RenderTexture2D& t, const std::string& name) {
        Image im = LoadImageFromTexture(t.texture);
        ImageFlipVertical(&im);
        std::string path = out + "/" + name + ".png";
        ExportImage(im, path.c_str());
        UnloadImage(im);
        std::printf("wrote %s (character submits %d, particles %d)\n", path.c_str(), cast().submitCount, R.particleCount());
    }
    void shot(const std::string& name, const Camera3D& cam, bool overlay = true) {
        if (!wanted(name)) return;
        renderTo(cam, overlay);
        save(rt, name);
    }
    // Contact sheet: `count` frames `gap` seconds apart (3 columns), simulating in between.
    void sheetShot(const std::string& name, const std::function<Camera3D()>& cam, int count, float gap,
                   bool overlay = false) {
        const bool want = wanted(name);
        const int cols = 3, rows = (count + cols - 1) / cols;
        const float cw = 1600.f / cols, chh = 900.f / rows;
        const float scale = std::min(cw / 1600.f, chh / 900.f);
        if (want) {
            BeginTextureMode(sheet);
            ClearBackground(Color{8, 6, 5, 255});
            EndTextureMode();
        }
        for (int i = 0; i < count; ++i) {
            if (want) {
                renderTo(cam(), overlay);
                BeginTextureMode(sheet);
                Rectangle src{0, 0, 1600.f, -900.f};
                Rectangle dst{(i % cols) * cw + (cw - 1600.f * scale) * 0.5f, (i / cols) * chh + (chh - 900.f * scale) * 0.5f,
                              1600.f * scale - 4.f, 900.f * scale - 4.f};
                DrawTexturePro(rt.texture, src, dst, {0, 0}, 0.f, WHITE);
                char lab[32];
                std::snprintf(lab, sizeof lab, "+%.2fs", i * gap);
                ui::drawTextShadow(ui::FontId::UiBold, lab, {dst.x + 8, dst.y + 6}, 20.f, WHITE);
                EndTextureMode();
            }
            if (i + 1 < count) runRendered(gap);
        }
        if (want) save(sheet, name);
    }

    // one bot action (false if not a bot's turn)
    bool botStep() {
        if (game.handState() != okey::HandState::Playing) return false;
        const int s = game.current();
        if (s == 0) return false;
        okey::BotAction a = bots[s]->next(game, s);
        okey::ActionResult r = okey::applyBotAction(game, s, a);
        if (!r.ok) okey::applyBotAction(game, s, okey::fallbackAction(game, s));
        pump();
        return true;
    }
    void humanAutoTurn() {
        if (game.handState() != okey::HandState::Playing || game.current() != 0) return;
        for (int guard = 0; guard < 8 && game.current() == 0 && game.handState() == okey::HandState::Playing; ++guard) {
            okey::BotAction a = okey::fallbackAction(game, 0);
            okey::ActionResult r = okey::applyBotAction(game, 0, a);
            pump();
            if (!r.ok) break;
            runRendered(0.5f);
        }
    }
};

Vector3 headOf(Harness& h, int s) { return h.chars.headPosition(s); }

// Camera in front of an opponent's face (from the table side), slightly above eye level.
Camera3D faceCam(Harness& h, int s, float dist = 0.55f, float side = 0.f, float fov = 34.f) {
    Vector3 head = headOf(h, s);
    Vector3 toTable = Vector3Negate(w3d::SEAT_DIR[s]);
    Vector3 right = w3d::SEAT_RIGHT[s];
    Vector3 eye = Vector3Add(Vector3Add(head, Vector3Scale(toTable, dist)), Vector3Scale(right, side));
    eye.y += 0.02f;
    return makeCam(eye, Vector3Add(head, {0, -0.05f, 0}), fov);
}

}  // namespace

int main(int argc, char** argv) {
    Harness h;
    if (argc > 1) h.out = argv[1];
    if (argc > 2) h.only = argv[2];
    if (!h.init()) {
        std::printf("init failed\n");
        return 1;
    }
    chr::Cast& C = h.cast();
    h.game.startMatch(20260929u);
    h.pump();
    if (h.only == "soak") {
        // ten minutes of play with bots (the human's turns auto-played): look for stuck states and NaNs
        int bad = 0, heldTooLong = 0, served = 0, bubbles = 0, lastState = 0;
        float held[4] = {0, 0, 0, 0}, botWait = 0.f;
        auto finite = [](const Matrix& m) { return std::isfinite(m.m0 + m.m5 + m.m10 + m.m12 + m.m13 + m.m14); };
        for (int f = 0; f < 60 * 600; ++f) {
            if (h.game.handState() == okey::HandState::Playing) {
                botWait += 1.f / 60.f;
                if (botWait > 0.9f) {
                    botWait = 0.f;
                    if (h.game.current() == 0) {
                        okey::applyBotAction(h.game, 0, okey::fallbackAction(h.game, 0));
                        h.pump();
                    } else {
                        h.botStep();
                    }
                }
            } else if (h.game.handState() == okey::HandState::HandOver) {
                botWait += 1.f / 60.f;
                if (botWait > 4.f) {
                    botWait = 0.f;
                    h.game.startNextHand();
                    h.pump();
                }
            } else if (h.game.handState() == okey::HandState::MatchOver) {
                h.game.startMatch(7u + (unsigned)f);
                h.pump();
            }
            h.step();
            for (int s = 1; s <= 3; ++s) {
                const chr::Opponent& o = C.opp[s];
                if (!finite(o.headW) || !finite(o.arm[0].hand) || !finite(o.arm[1].hand) || !finite(o.torsoW)) ++bad;
            }
            for (const chr::Patron& p : C.patrons)
                if (!finite(p.headW) || !finite(p.arm[0].hand)) ++bad;
            if (!finite(C.boy.headW) || !finite(C.boy.trayW)) ++bad;
            for (int g = 0; g < 4; ++g) {
                held[g] = C.glass[g].holder >= 0 ? held[g] + 1.f / 60.f : 0.f;
                if (held[g] > 8.f) ++heldTooLong;
            }
            if (lastState == 2 && C.boy.state == 1 && C.boy.plan == 0) ++served;
            lastState = C.boy.state;
            for (int w = 1; w <= 4; ++w)
                if (C.bubbles.on[(size_t)w] && C.bubbles.cur[(size_t)w].t == 0.f) ++bubbles;
        }
        std::printf("soak: bad=%d heldTooLong=%d rounds=%d bubbles=%d boyState=%d glasses %.2f %.2f %.2f %.2f\n", bad,
                    heldTooLong, served, bubbles, C.boy.state, C.glass[0].level, C.glass[1].level, C.glass[2].level,
                    C.glass[3].level);
        std::printf("sfx:");
        for (int i = 0; i < (int)ui::Sfx::Count; ++i) std::printf(" %d", h.sfx[i]);
        std::printf("\n");
        return 0;
    }
    h.runRendered(6.f);  // deal, settle, let smoke build up

    // ---- 1. the seat view, idle
    h.shot("01_seat", seatCam());
    h.shot("02_seat_level", seatCam(0.f, -8.f));
    h.shot("03_seat_right", seatCam(-55.f, -12.f));
    h.shot("04_seat_left", seatCam(55.f, -12.f));

    // ---- 2. faces
    for (int s = 1; s <= 3; ++s) {
        C.opp[s].gazeGoal = h.viewer.position;  // look at the camera for the portrait
        C.opp[s].gazeHold = 5.f;
    }
    h.run(1.2f);
    const char* faceNames[4] = {"", "10_face_riza", "11_face_mahmut", "12_face_nuri"};
    for (int s = 1; s <= 3; ++s) {
        Camera3D fc = faceCam(h, s);
        C.opp[s].gazeGoal = fc.position;
        C.opp[s].gazeHold = 5.f;
    }
    h.run(0.8f);
    for (int s = 1; s <= 3; ++s) h.shot(faceNames[s], faceCam(h, s), false);
    h.shot("13_face_riza_3q", faceCam(h, 1, 0.6f, 0.3f), false);
    h.shot("14_face_nuri_3q", faceCam(h, 3, 0.6f, -0.3f), false);
    // expressions sheet (Mahmut)
    {
        const chr::Mood moods[6] = {chr::Mood::Neutral, chr::Mood::Happy, chr::Mood::Laugh,
                                    chr::Mood::Grumpy, chr::Mood::Surprised, chr::Mood::Sad};
        int mi = 0;
        for (int s = 2; s <= 2; ++s) {
            const bool want = h.wanted("15_moods");
            if (want) {
                BeginTextureMode(h.sheet);
                ClearBackground(BLACK);
                EndTextureMode();
            }
            for (mi = 0; mi < 6; ++mi) {
                C.setMood(C.opp[s], moods[mi], 5.f);
                Camera3D fc = faceCam(h, s, 0.5f);
                C.opp[s].gazeGoal = fc.position;
                C.opp[s].gazeHold = 5.f;
                h.run(0.7f);
                if (!want) continue;
                h.renderTo(fc, false);
                BeginTextureMode(h.sheet);
                Rectangle dst{(mi % 3) * 533.3f, (mi / 3) * 450.f + 75.f, 531.f, 298.f};
                DrawTexturePro(h.rt.texture, {0, 0, 1600.f, -900.f}, dst, {0, 0}, 0.f, WHITE);
                EndTextureMode();
            }
            if (want) h.save(h.sheet, "15_moods");
        }
        for (int s = 1; s <= 3; ++s) C.setMood(C.opp[s], chr::Mood::Neutral, 0.f);
    }

    // ---- 3. game: bots act; capture reaches mid-flight
    auto untilBot = [&](int seat) {
        for (int guard = 0; guard < 60; ++guard) {
            if (h.game.handState() != okey::HandState::Playing) return false;
            if (h.game.current() == seat) return true;
            if (h.game.current() == 0) h.humanAutoTurn();
            else {
                h.botStep();
                h.runRendered(0.9f);
            }
        }
        return false;
    };
    if (untilBot(1)) {
        h.botStep();  // Rıza draws (DrawPile or TakeLeft)
        h.sheetShot("20_riza_draw", [] { return seatCam(-30.f, -18.f); }, 6, 0.12f);
        h.runRendered(0.6f);
        h.botStep();  // ... and discards (or opens)
        h.sheetShot("21_riza_discard", [] { return seatCam(-30.f, -18.f); }, 6, 0.12f);
    }
    if (untilBot(2)) {
        h.botStep();
        h.sheetShot("22_mahmut_draw", [] { return seatCam(0.f, -18.f); }, 6, 0.12f);
        h.runRendered(0.6f);
        h.botStep();
        h.sheetShot("23_mahmut_discard", [] { return seatCam(0.f, -18.f); }, 6, 0.12f);
    }
    if (untilBot(3)) {
        h.botStep();
        h.sheetShot("24_nuri_draw", [] { return seatCam(30.f, -18.f); }, 6, 0.12f);
        h.runRendered(0.6f);
        h.botStep();
        h.sheetShot("25_nuri_discard", [] { return seatCam(30.f, -18.f); }, 6, 0.12f);
    }
    h.runRendered(1.f);

    // ---- 4. scripted gestures (injected events)
    auto ev = [](okey::EvType t, int p, int meld = -1, int amount = 0) {
        okey::GameEvent e;
        e.type = t;
        e.player = p;
        e.meld = meld;
        e.amount = amount;
        return e;
    };
    h.inject(ev(okey::EvType::Open, 2, -1, 124));
    h.sheetShot("30_mahmut_open", [] { return seatCam(0.f, -14.f); }, 9, 0.2f);
    h.runRendered(1.5f);
    h.inject(ev(okey::EvType::Penalty, 1, -1, 101));
    h.sheetShot("31_riza_penalty", [] { return seatCam(0.f, -10.f); }, 6, 0.3f);
    h.runRendered(1.5f);
    h.inject(ev(okey::EvType::Penalty, 2, -1, 101));
    h.sheetShot("32_mahmut_penalty", [] { return seatCam(0.f, -10.f); }, 6, 0.3f);
    h.runRendered(1.5f);
    h.inject(ev(okey::EvType::Penalty, 0, -1, 101));
    h.sheetShot("33_human_penalty", [] { return seatCam(0.f, -8.f); }, 6, 0.3f, true);
    h.runRendered(2.f);
    h.inject(ev(okey::EvType::HandEnd, 2));
    h.sheetShot("34_mahmut_wins", [] { return seatCam(0.f, -6.f); }, 9, 0.3f, true);
    h.runRendered(2.f);
    h.inject(ev(okey::EvType::HandEnd, 1));
    h.sheetShot("35_riza_wins", [] { return seatCam(-20.f, -8.f); }, 9, 0.35f);
    h.runRendered(2.f);
    h.inject(ev(okey::EvType::HandEnd, 0));
    h.sheetShot("36_human_wins", [] { return seatCam(0.f, -6.f); }, 9, 0.3f, true);
    h.runRendered(2.5f);

    h.chars.onTvGoal();
    h.sheetShot("37_tv_goal", [] { return seatCam(0.f, -6.f); }, 6, 0.3f, true);
    h.runRendered(2.f);
    h.inject(ev(okey::EvType::MatchEnd, 3));
    h.sheetShot("38_nuri_match", [] { return seatCam(25.f, -8.f); }, 6, 0.4f, true);
    h.runRendered(3.f);

    // ---- 5. idle business: sipping, smoking, tespih, thinking
    for (int s = 1; s <= 3; ++s) C.opp[s].sipIn = 100.f;
    C.startSip(C.opp[1]);
    h.sheetShot("40_riza_sip", [&] { return faceCam(h, 1, 0.9f, 0.2f, 40.f); }, 9, 0.42f);
    C.startSip(C.opp[3]);
    h.sheetShot("41_nuri_sip", [&] { return faceCam(h, 3, 0.9f, -0.2f, 40.f); }, 9, 0.42f);
    C.startSip(C.opp[2]);
    h.sheetShot("42_mahmut_sip", [&] { return faceCam(h, 2, 0.9f, 0.f, 40.f); }, 9, 0.42f);
    C.opp[2].smokeIn = 100.f;
    C.startSmoke(C.opp[2], false);
    h.sheetShot("43_mahmut_smoke", [&] { return faceCam(h, 2, 0.8f, 0.2f, 42.f); }, 9, 0.3f);
    C.startSmoke(C.opp[2], true);
    h.sheetShot("44_mahmut_ash", [] { return seatCam(0.f, -22.f); }, 6, 0.26f);
    C.startTespihFlip(C.opp[1]);
    h.sheetShot("45_riza_tespih", [&] { return faceCam(h, 1, 0.8f, -0.25f, 44.f); }, 6, 0.2f);
    h.chars.setActiveSeat(3);
    h.sheetShot("46_nuri_thinking", [&] { return faceCam(h, 3, 0.9f, 0.1f, 40.f); }, 6, 0.6f);
    h.chars.setActiveSeat(0);
    for (int s = 1; s <= 3; ++s) C.opp[s].sipIn = 20.f + 5.f * s;

    // ---- 6. speech bubbles
    h.chars.say(2, "Hadi be, çay soğuyacak! Oyna artık şu taşı!", 4.f);
    h.run(0.6f);
    h.chars.say(3, "Bizim zamanımızda böyle mi oynanırdı...", 4.f);
    h.run(0.7f);
    h.shot("50_bubbles", seatCam());
    h.shot("51_bubbles_up", seatCam(10.f, -6.f));
    h.run(0.4f);
    h.shot("52_bubbles_turned", seatCam(-70.f, -12.f));
    h.run(4.f);
    h.chars.say(1, "Sabreden derviş muradına ermiş evlat.", 3.5f);
    h.run(0.5f);
    h.shot("53_bubble_riza", seatCam());
    h.run(4.f);

    // ---- 7. somebody calls the çaycı; he answers from the counter, then serves our table
    C.boy.nextOurs = 100.f;
    h.runRendered(0.5f);
    C.pushLine(1, "Çaycı! Üç çay, bir oralet!", 2.6f, 4.f, true);
    h.sheetShot("59_tea_order", [] { return seatCam(-25.f, -4.f); }, 6, 0.4f, true);
    C.boy.nextOurs = 0.f;
    C.boy.timer = 0.f;
    for (int i = 0; i < 4; ++i) C.glass[i].level = 0.2f;
    int guard = 0;
    while (C.boy.state != 2 && guard++ < 60 * 30) h.step();
    h.runRendered(0.3f);
    h.sheetShot("60_cayci_serve", [] { return seatCam(-35.f, -15.f); }, 9, 0.5f, true);
    h.shot("61_cayci_wide", makeCam({2.4f, 1.9f, 2.4f}, {0.4f, 0.9f, 0.f}, 55.f), false);
    guard = 0;
    while (C.boy.state == 2 || (C.boy.state == 1 && C.boy.plan == 1)) {
        if (guard++ > 60 * 40) break;
        h.step();
        if (C.boy.state == 2 && C.boy.tour[(size_t)std::min(C.boy.serveIdx, (int)C.boy.tour.size() - 1)] == 0 &&
            C.boy.serveStep == 2 && h.wanted("62_cayci_human")) {
            h.shot("62_cayci_human", seatCam(70.f, -8.f));
            break;
        }
    }
    h.runRendered(8.f);
    std::printf("glass levels after the tour: %.2f %.2f %.2f %.2f\n", C.glass[0].level, C.glass[1].level,
                C.glass[2].level, C.glass[3].level);

    // ---- 8. wide shots
    h.shot("70_wide_corner", makeCam({3.6f, 2.3f, 3.1f}, {-0.6f, 0.8f, -0.9f}, 62.f), false);
    h.shot("71_wide_back", makeCam({-3.5f, 2.2f, -2.9f}, {1.0f, 0.8f, 1.2f}, 62.f), false);
    h.shot("72_behind_mahmut", makeCam({0.f, 1.55f, -2.0f}, {0.f, 0.95f, 0.5f}, 55.f), false);
    h.shot("73_patrons_left", makeCam({-0.9f, 1.5f, 0.2f}, {-2.6f, 0.9f, -0.2f}, 60.f), false);
    h.shot("74_patrons_right", makeCam({0.9f, 1.5f, 0.2f}, {2.5f, 0.9f, -0.3f}, 60.f), false);
    h.shot("75_seat_back", seatCam(170.f, -8.f), false);
    h.sheetShot("76_tavla", [] { return makeCam({-1.6f, 1.6f, -1.1f}, {-2.5f, 0.8f, -1.8f}, 45.f); }, 6, 0.5f);
    h.sheetShot("77_okey_table", [] { return makeCam({1.5f, 1.7f, -0.8f}, {2.4f, 0.8f, -1.9f}, 48.f); }, 6, 0.6f);
    h.sheetShot("78_cards", [] { return makeCam({-1.6f, 1.6f, 0.9f}, {-2.6f, 0.8f, 1.7f}, 48.f); }, 6, 0.6f);
    h.sheetShot("79_tea_chat", [] { return makeCam({1.5f, 1.6f, 0.9f}, {2.5f, 0.85f, 1.8f}, 48.f); }, 6, 0.8f);
    // çaycı walking
    C.boy.nextBg = 0.f;
    C.boy.timer = 0.f;
    guard = 0;
    while (C.boy.state != 1 && guard++ < 60 * 20) h.step();
    h.runRendered(1.2f);
    h.sheetShot("80_cayci_walk", [&] {
        Vector3 p = C.boy.pos;
        return makeCam({p.x + 1.6f, 1.5f, p.z + 1.3f}, {p.x, 0.9f, p.z}, 45.f);
    }, 6, 0.18f);

    // ---- 9. title mode (menu backdrop): relaxed idles, no bubbles, a slow camera through the room
    h.chars.setTitleMode(true);
    h.chars.say(2, "Bu görünmemeli.", 3.f);
    h.runRendered(6.f);
    h.sheetShot("90_title", [] { return makeCam({-1.9f, 1.7f, 2.2f}, {0.2f, 0.95f, -0.2f}, 50.f); }, 6, 1.2f, true);
    h.chars.setTitleMode(false);
    h.runRendered(1.f);

    // ---- 10. GPU cost: full frames at 1600x900 with and without the characters (glFinish-synced)
    {
        long tris = 0;
        auto count = [&](const Mesh& m) { tris += m.triangleCount; };
        const chr::Meshes& M = C.M;
        for (int s = 1; s <= 3; ++s) {
            const chr::PersonMeshes& pm = *C.opp[s].pm;
            count(pm.lower), count(pm.torso), count(pm.head), count(pm.stache);
            for (int a = 0; a < 2; ++a) count(pm.upper[a]), count(pm.fore[a]), count(pm.brow[a]), count(M.hand[a][(int)C.opp[s].arm[a].pose]);
            count(M.eye[0]), count(M.eye[0]), count(M.lidUpper), count(M.lidUpper), count(M.lowerLip[2]);
        }
        for (const chr::Patron& p : C.patrons) {
            count(p.pm->lower), count(p.pm->torso), count(p.pm->head);
            for (int a = 0; a < 2; ++a) count(p.pm->upper[a]), count(p.pm->fore[a]), count(M.hand[a][(int)p.arm[a].pose]);
        }
        std::printf("character triangles (approx.): %ld\n", tris);
        const chr::PersonMeshes& o1 = M.person[1];
        std::printf("  riza: head %d torso %d lower %d stache %d upper %d fore %d brow %d | hand rest %d grip %d | eye %d lid %d lip %d\n",
                    o1.head.triangleCount, o1.torso.triangleCount, o1.lower.triangleCount, o1.stache.triangleCount,
                    o1.upper[0].triangleCount, o1.fore[0].triangleCount, o1.brow[0].triangleCount, M.hand[0][0].triangleCount,
                    M.hand[0][1].triangleCount, M.eye[0].triangleCount, M.lidUpper.triangleCount, M.lowerLip[2].triangleCount);
        const chr::PersonMeshes& p0 = M.patron[0];
        std::printf("  patron: head %d torso %d lower %d | chair %d seat %d glass %d saucer %d tea %d tray %d/%d/%d dice %d\n",
                    p0.head.triangleCount, p0.torso.triangleCount, p0.lower.triangleCount, M.chair.triangleCount,
                    M.chairSeat.triangleCount, M.glass.triangleCount, M.saucer.triangleCount, M.tea[10].triangleCount,
                    M.trayHanger.triangleCount, M.trayGlasses.triangleCount, M.trayTea.triangleCount, M.dice.triangleCount);
        auto timeFrames = [&](bool withChars) {
            glFinish();
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 60; ++i) {
                h.step();
#ifndef CHARS_STANDALONE
                h.room.submit(h.R);
                h.table.submit(h.R);
#else
                h.ph.submit(h.R);
#endif
                if (withChars) h.chars.submit(h.R);
                h.R.render(seatCam(), &h.rt, BLACK);
                glFinish();
            }
            auto t1 = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(t1 - t0).count() / 60.0;
        };
        double without = timeFrames(false), with = timeFrames(true);
        std::printf("frame 1600x900 (seat view): %.2f ms without characters, %.2f ms with (+%.2f ms)\n", without, with,
                    with - without);
    }

    // ---- 11. a longer run for stats
    h.charMs = 0.0;
    h.charFrames = 0;
    for (int i = 0; i < 600; ++i) {
        h.step();
        h.submitAll();
        h.R.render(h.viewer, &h.small, BLACK);
    }
    std::printf("characters: %.3f ms/frame (update+submit), max submits %d\n", h.charMs / std::max(1, h.charFrames),
                h.maxSubmits);
    std::printf("sfx:");
    for (int i = 0; i < (int)ui::Sfx::Count; ++i) std::printf(" %d", h.sfx[i]);
    std::printf("\n");
    h.shot("99_seat_final", seatCam());
    h.chars.shutdown(h.R);
    return 0;
}
