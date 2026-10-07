// SaklıBahçe: the table games played without a person. A hidden window renders the table from the player's seat.
//
// 1. By the mouse: the other table games (Tavla, Pişti, Batak, King). Every decision of the player's seat is made with
//    a real click (raylib automation events through the same input path as the game) on what the game draws: a card in
//    the fan, a point of the tavla board, a button of the ihale / koz / contract panel, "Zar At", tavla's katlama zarı
//    (a second tavla match with the cube: "Katla", the offer panel). The game must keep moving; a few frames are saved
//    (--out: the 6th click and the first frame of every TableGame::debugPhase()). In the card games the player also
//    presses H (İpucu) before the 1st and the 7th click (--out: <game>_ipucu1.png / _ipucu2.png, 20 frames later).
// 2. By the keyboard only (no mouse event at all): every game, the okey family too (101, klasik okey), a full hand with
//    key presses through the same automation path: arrows / Enter / Space / Tab in the card games' fans and panels;
//    R, arrows, Enter, U, K at the tavla board; D, A, arrows, Space, Shift-less carrying, O, I, Enter and the confirm
//    modal at the okey istaka. (--out: kb_<game>_*.png.)
//
//   build: make tablescheck (see the Makefile)
//   run:   tables_check [--hands N] [--out DIR] [--keys | --mouse] [--cb] [--big] [--no-3d]
//          --cb: colour-blind mode (tiles with shapes, four-colour cards); --big: HUD text x1.25 (Büyük yazı)
//          --no-3d: skip the 3D pass (picking does not need it; a software GL renders a frame in ~0.1 s)
#include "app/TableGame.h"
#include "core/Bot.h"
#include "core/Game.h"
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/Table3D.h"
#include "r3d/Table3DTest.h"
#include "r3d/World.h"
#include "ui/CardRender.h"
#include "ui/Common.h"
#include "ui/TileRender.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app { // Konken (KonkenTable.cpp): the drop point of the drag the player's next move needs; the draw stage
bool konkenDebugDragTarget(const TableGame* t, const r3d::Renderer& r, Vector2& out);
bool konkenDebugNeedsDraw(const TableGame* t);
void konkenDebugBurn(TableGame* t, int seat, bool watch); // (son kalan) the seat burns when this hand ends
int konkenDebugPlace(const TableGame* t, int seat);
} // namespace app

namespace {

constexpr unsigned EV_KEY_UP = 1, EV_KEY_DOWN = 2, EV_MOUSE_UP = 5, EV_MOUSE_DOWN = 6;
void inject(unsigned type, int p0) {
    AutomationEvent e{};
    e.type = type;
    e.params[0] = p0;
    PlayAutomationEvent(e);
}

Camera3D seatCamera() {
    Camera3D c{};
    c.position = w3d::EYE;
    const float pitch = w3d::EYE_PITCH_DEG * DEG2RAD;
    c.target = {c.position.x, c.position.y + std::sin(pitch), c.position.z - std::cos(pitch)};
    c.up = {0, 1, 0};
    c.fovy = w3d::FOVY_DEG;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

void save(RenderTexture2D& rt, const std::string& path) {
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    ExportImage(img, path.c_str());
    UnloadImage(img);
}

// One key: down now, up after the next frame (a press the game sees exactly once).
struct KeyPress {
    int pending = 0;
    int count = 0;
    bool busy() const { return pending != 0; }
    void press(int key) {
        inject(EV_KEY_DOWN, key);
        pending = key;
        ++count;
    }
    void release() {
        if (pending) inject(EV_KEY_UP, pending);
        pending = 0;
    }
};

const ui::GameKind kKinds[] = {ui::GameKind::Tavla, ui::GameKind::Tavla, ui::GameKind::Pisti, ui::GameKind::Batak, ui::GameKind::King,
                               ui::GameKind::Dama, // Dama: index 5, run on its own below
                               ui::GameKind::Bezik, // Bezik: index 6, run on its own below
                               ui::GameKind::Altmisalti}; // Altmışaltı: index 7, run on its own below
const char* kNames[] = {"tavla", "tavla_katlama", "pisti", "batak", "king", "dama", "bezik", "altmisalti"}; // (not const: the tavla çeşitleri rename 0 / 1)
const uint64_t kSeeds[] = {1234, 1238, 1235, 1236, 1237, 1239, 1240, 1241}; // (Bezik: 1240, Altmışaltı: 1241) // (the seeds the four games always had, then the cube's)
int gTavlaCesit = 0; // Tavla çeşidi of the tavla runs (main runs them again as Gülbahar and Fevga)
int gPistiMode = 0;  // Pişti: main runs pişti again as iki kişilik (2: at the two-seat tavla table)

struct Ctx {
    r3d::Renderer* R = nullptr;
    RenderTexture2D* rt = nullptr;
    Camera3D cam{};      // the seat at our okey table
    Camera3D tavlaCam{}; // the seat at the tavla table (w3d::tavlaFrame)
    std::string out;
    int hands = 2;
    bool no3d = false; // --no-3d: the 3D pass is skipped (a software GL in the cloud); the HUD is still drawn
};

void render3D(const Ctx& c) {
    if (c.no3d) {
        c.R->discardFrame(c.cam);
        BeginTextureMode(*c.rt);
        ClearBackground(Color{20, 14, 10, 255});
        EndTextureMode();
    } else {
        c.R->render(c.cam, c.rt, Color{20, 14, 10, 255});
    }
}

std::unique_ptr<app::TableGame> startGame(const Ctx& c, int k) {
    std::unique_ptr<app::TableGame> g = app::makeTableGame(kKinds[k]);
    app::TableContext ctx;
    ctx.renderer = c.R;
    if (!g || !g->init(ctx)) return nullptr;
    ui::Settings st;
    st.tavlaPoints = 3;
    st.tavlaDoubling = k == 1;
    st.tavlaCesit = kKinds[k] == ui::GameKind::Tavla ? gTavlaCesit : 0;
    st.pistiMode = kKinds[k] == ui::GameKind::Pisti ? gPistiMode : 0;
    g->setAnimationSpeed(3.f);
    g->startMatch(st, {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, kSeeds[k]);
    return g;
}

// The seat the game is played from: tavla has its own table.
Ctx forGame(const Ctx& c, const app::TableGame* g) {
    Ctx out = c;
    if (g && g->location() == 1) out.cam = c.tavlaCam;
    return out;
}

void frame(const Ctx& c, app::TableGame& g, Vector2 mouse) {
    g.update(1.f / 30.f, c.cam, mouse, true, false);
    BeginDrawing();
    g.submit(*c.R);
    render3D(c);
    BeginTextureMode(*c.rt);
    g.drawHUD(*c.R, mouse, false);
    EndTextureMode();
    EndDrawing();
}

std::string gOnly; // --only NAME: run just that game (e.g. "dama", "tavla", "101")

// ---------------------------------------------------------------- 1. the mouse
int runMouse(const Ctx& c0, int k) {
    if (!gOnly.empty() && gOnly != kNames[k]) return 0;
    std::unique_ptr<app::TableGame> g = startGame(c0, k);
    const Ctx c = forGame(c0, g.get());
    if (!g) {
        std::printf("%s: init failed\n", kNames[k]);
        return 1;
    }
    Vector2 mouse{-1000, -1000};
    int clicks = 0, handsDone = 0, idle = 0, frames = 0, failures = 0;
    bool shot = false;
    std::vector<std::string> phases;
    int pressPhase = 0; // 0 none, 1 pressed this frame (release next)
    int hintStep = 0, hintWait = 0; // card games: H before the 1st and the 7th click
    while (handsDone < c.hands && frames < 60000) {
        ++frames;
        frame(c, *g, mouse);
        const std::string ph = g->debugPhase();
        if (!c.out.empty() && !ph.empty() && ph.rfind("klavye", 0) != 0 && std::find(phases.begin(), phases.end(), ph) == phases.end()) {
            phases.push_back(ph);
            save(*c.rt, c.out + "/" + kNames[k] + "_" + ph + ".png");
        }
        if (pressPhase == 1) {
            inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
            pressPhase = 0;
        } else {
            Vector2 p;
            const bool canClick = g->debugHumanClick(*c.R, p);
            if (canClick && k >= 2 && hintStep < 2 && clicks == (hintStep == 0 ? 0 : 6)) {
                if (hintWait == 0) inject(EV_KEY_DOWN, KEY_H);
                else if (hintWait == 1) inject(EV_KEY_UP, KEY_H);
                if (++hintWait > 20) {
                    if (!c.out.empty()) save(*c.rt, c.out + "/" + kNames[k] + "_ipucu" + std::to_string(hintStep + 1) + ".png");
                    ++hintStep;
                    hintWait = 0;
                }
            } else if (canClick) {
                mouse = p;
                inject(EV_MOUSE_DOWN, MOUSE_BUTTON_LEFT);
                pressPhase = 1;
                ++clicks;
                idle = 0;
                if (!shot && clicks == 6 && !c.out.empty()) {
                    save(*c.rt, c.out + "/" + kNames[k] + "_click.png");
                    shot = true;
                }
            } else {
                ++idle;
            }
        }
        if (g->handOver() && !g->animating()) {
            ++handsDone;
            if (g->matchOver()) break;
            g->startNextHand();
        }
        if (idle > 3000) { // 100 s of game time with nothing to click and no progress
            std::printf("%s: STUCK (frame %d, %d clicks)\n", kNames[k], frames, clicks);
            ++failures;
            break;
        }
    }
    const bool ok = handsDone >= std::min(c.hands, 1) && clicks > 3;
    std::printf("%s: %d hands, %d clicks by the player's mouse, %d frames — %s\n", kNames[k], handsDone, clicks, frames,
                ok ? "ok" : "FAILED");
    g->shutdown();
    return failures + (ok ? 0 : 1);
}

// ---------------------------------------------------------------- 2. the keyboard: tavla and the card games
// Whenever the player has something to do (TableGame::debugHumanClick says so, but its point is never used) a key
// is pressed from a per-game cycle; the mouse never moves and never clicks. The cycles always end on Enter, so every
// decision is made by the keyboard's cursor / focus wherever the arrows left it.
int runKeys(const Ctx& c0, int k) {
    if (!gOnly.empty() && gOnly != kNames[k]) return 0;
    std::unique_ptr<app::TableGame> g = startGame(c0, k);
    const Ctx c = forGame(c0, g.get());
    if (!g) {
        std::printf("kb %s: init failed\n", kNames[k]);
        return 1;
    }
    const Vector2 mouse{-1000, -1000};
    KeyPress key;
    int handsDone = 0, idle = 0, frames = 0, failures = 0, wait = 0, step = 0;
    std::vector<std::string> phases;
    const bool tavla = k <= 1;
    // tavla: R rolls, the arrows choose the checker / its place, Enter plays, U takes a step back now and then,
    // K offers the cube (the katlama match); the card games: the arrows walk the fan / the panel, Tab + Enter presses
    // İpucu once in a while, Space plays too.
    const std::vector<int> cycTavla = {KEY_R, KEY_RIGHT, KEY_ENTER, KEY_UP, KEY_ENTER, KEY_LEFT, KEY_SPACE, KEY_ENTER,
                                       KEY_U, KEY_ENTER, KEY_K, KEY_ENTER, KEY_DOWN, KEY_ENTER};
    const std::vector<int> cycCards = {KEY_RIGHT, KEY_ENTER, KEY_LEFT, KEY_LEFT, KEY_SPACE, KEY_TAB, KEY_ENTER, KEY_RIGHT,
                                       KEY_ENTER, KEY_DOWN, KEY_ENTER, KEY_H, KEY_RIGHT, KEY_ENTER};
    const std::vector<int>& cyc = tavla ? cycTavla : cycCards;
    while (handsDone < c.hands && frames < 60000) {
        ++frames;
        frame(c, *g, mouse);
        const std::string ph = g->debugPhase();
        if (!c.out.empty() && ph.rfind("klavye", 0) == 0 && std::find(phases.begin(), phases.end(), ph) == phases.end()) {
            phases.push_back(ph);
            save(*c.rt, c.out + "/kb_" + kNames[k] + "_" + ph + ".png");
        }
        if (key.busy()) {
            key.release();
        } else if (wait > 0) {
            --wait;
        } else {
            Vector2 p;
            if (g->debugHumanClick(*c.R, p)) {
                int kk = cyc[(size_t)(step++ % (int)cyc.size())];
                if (kk == KEY_K && k != 1) kk = KEY_ENTER;
                key.press(kk);
                wait = 3;
                idle = 0;
            } else {
                ++idle;
            }
        }
        if (g->handOver() && !g->animating()) {
            ++handsDone;
            if (g->matchOver()) break;
            g->startNextHand();
        }
        if (idle > 3000) {
            std::printf("kb %s: STUCK (frame %d, %d keys)\n", kNames[k], frames, key.count);
            ++failures;
            break;
        }
    }
    if (!c.out.empty()) save(*c.rt, c.out + "/kb_" + kNames[k] + "_end.png");
    const bool ok = handsDone >= std::min(c.hands, 1) && key.count > 3 && ui::keyboardNav();
    std::printf("kb %s: %d hands, %d keys (no mouse), %d frames — %s\n", kNames[k], handsDone, key.count, frames,
                ok ? "ok" : "FAILED");
    g->shutdown();
    return failures + (ok ? 0 : 1);
}

// ---------------------------------------------------------------- 2b. the keyboard: the okey istaka (Table3D)
// kat: 101 kuralları — katlamalı, tek kat, açma 81, geri verme cezalı ("101kat")
int runOkeyKeys(const Ctx& c, bool classic, bool kat = false) {
    const char* name = classic ? "okey" : kat ? "101kat" : "101";
    if (!gOnly.empty() && gOnly != name) return 0;
    okey::RulesConfig rc;
    if (classic) rc.variant = okey::Variant::Okey;
    if (kat) { // (101 kuralları)
        rc.katlamali = true;
        rc.finishMult = okey::FinishMult::Single;
        rc.openThreshold = 81;
        rc.penaltyReturnLeft = true;
    }
    rc.numHands = 1;
    okey::Game game(rc);
    const char* names[4] = {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"};
    for (int s = 0; s < 4; ++s) game.setPlayer(s, names[s], s == 0);
    r3d::Table3D table;
    if (!table.init(*c.R, &game, 0)) {
        std::printf("kb %s: init failed\n", name);
        return 1;
    }
    table.setAnimationSpeed(3.f);
    std::array<std::unique_ptr<okey::Bot>, 4> bots;
    for (int s = 1; s < 4; ++s) bots[(size_t)s] = std::make_unique<okey::Bot>(okey::BotLevel::Normal, 5150u + (unsigned)s);
    auto pump = [&]() {
        for (const okey::GameEvent& e : game.drainEvents()) {
            table.onEvent(e);
            for (auto& b : bots)
                if (b) b->observe(e, game);
            if (e.type == okey::EvType::HandStart) {
                for (auto& b : bots)
                    if (b) b->resetForHand();
                table.onHandStart();
            }
        }
    };
    game.startMatch(classic ? 4242u : kat ? 4343u : 4141u);
    pump();
    const Vector2 mouse{-1000, -1000};
    KeyPress key;
    int frames = 0, botWait = 0, wait = 0, failures = 0, step = 0, lastTurn = -1, lastStage = -1, humanFrames = 0;
    int turns = 0, discards = 0, confirms = 0, opens = 0, isles = 0;
    std::vector<std::string> shots;
    auto shot = [&](const std::string& n) {
        if (c.out.empty() || std::find(shots.begin(), shots.end(), n) != shots.end()) return;
        shots.push_back(n);
        save(*c.rt, c.out + "/kb_" + name + "_" + n + ".png");
    };
    bool done = false;
    while (frames < 60000 && !done) {
        ++frames;
        pump();
        table.update(1.f / 30.f, c.cam, mouse, true);
        BeginDrawing();
        table.submit(*c.R);
        render3D(c);
        BeginTextureMode(*c.rt);
        table.drawHUD(*c.R);
        EndTextureMode();
        EndDrawing();
        if (game.handState() != okey::HandState::Playing) {
            if (!table.isAnimating()) done = true;
            continue;
        }
        if (key.busy()) {
            key.release();
            continue;
        }
        if (table.isAnimating()) continue;
        const int cur = game.current();
        if (cur != 0) {
            humanFrames = 0;
            if (++botWait < 4) continue;
            botWait = 0;
            okey::BotAction a = bots[(size_t)cur]->next(game, cur);
            if (!okey::applyBotAction(game, cur, a).ok) okey::applyBotAction(game, cur, okey::fallbackAction(game, cur));
            continue;
        }
        // the player's turn: keys only
        if (game.turnNumber() != lastTurn) {
            lastTurn = game.turnNumber();
            step = 0;
            ++turns;
        }
        if ((int)game.stage() != lastStage) { // (each stage starts its own key sequence)
            lastStage = (int)game.stage();
            step = 0;
        }
        if (++humanFrames > 4000) {
            std::printf("kb %s: STUCK on the player's turn (frame %d, %d keys)\n", name, frames, key.count);
            ++failures;
            break;
        }
        if (wait > 0) {
            --wait;
            continue;
        }
        wait = 3;
        if (r3d::table3dtest::confirmActive(table)) { // a costly discard: ← to "Evet, at", Enter
            shot("onay");
            ++confirms;
            key.press(step++ % 2 == 0 ? KEY_LEFT : KEY_ENTER);
            continue;
        }
        if (game.stage() == okey::TurnStage::NeedDraw) {
            // a look along the istaka first, then draw (klasik okey: the left tile every other turn)
            static const int pre[] = {KEY_RIGHT, KEY_RIGHT, KEY_DOWN, KEY_UP};
            if (step < 4) {
                key.press(pre[step++]);
                continue;
            }
            shot("cek");
            key.press(classic && game.canTakeFromLeft(0) && turns % 2 == 0 ? KEY_A : KEY_D);
            continue;
        }
        // Play: carry a tile two places with Space, try to open (O) and to işle (I), then Enter discards the cursor's tile
        static const int play[] = {KEY_LEFT, KEY_SPACE, KEY_RIGHT, KEY_RIGHT, KEY_SPACE, KEY_O, KEY_I, KEY_ENTER};
        if (step < 8) {
            const int kk = play[step++];
            if (kk == KEY_O) ++opens;
            if (kk == KEY_I) ++isles;
            if (kk == KEY_ENTER) ++discards;
            key.press(kk);
            if (step == 3) shot("tasi");
            if (step == 8) shot("at");
            continue;
        }
        ++discards;
        key.press(KEY_ENTER); // (an işle took the first Enter, or a confirm said no: Enter again)
    }
    std::string v = r3d::table3dtest::validate(table, true);
    if (kat && done) { // (101 kuralları) the rules reached the engine: the kat of the hand follows the tek kat rule
        const okey::HandResult& r = game.lastHandResult();
        const int want = r.winner < 0 ? 1 : game.finishMultiplier(r.finishedWithJoker, r.finishedWithPairs, r.finishedInOneGo);
        if (r.multiplier != want || r.multiplier > 2 || game.seriesOpenNeed() < 81) v += " kat/açma rules not applied";
    }
    if (!c.out.empty()) save(*c.rt, c.out + "/kb_" + name + "_end.png");
    const bool ok = done && failures == 0 && turns >= 3 && v.empty();
    std::printf("kb %s: hand %s, %d turns of the player, %d keys (no mouse; %d Enter, %d O, %d I, %d confirms), %d frames%s — %s\n",
                name, done ? "over" : "NOT over", turns, key.count, discards, opens, isles, confirms, frames,
                v.empty() ? "" : (" validate: " + v).c_str(), ok ? "ok" : "FAILED");
    table.shutdown(*c.R);
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- 3. Konken (its own runner: drags and D / A)
// By the mouse, Kurt's plan for the player is played with the mouse: a click on the stock or the discard pile, clicks
// on the cards to mark an opening and the "Aç" / "Geri Ver" button, and a real drag (press on the card, move, let go)
// onto a meld (işle / take its joker) or onto the discard pile. By the keyboard: D / A draw, then arrows, Space, O, I,
// G and Enter until the turn passes.
// `burn` (son kalan): the player burns at the end of the first hand; the next deal brings up the panel, the mouse takes
// "Sonuca geç" (burn 1: the bots play the rest at once) or "Hızlı izle" (burn 2: the table plays on at speed, to a
// limit of 101); the match must be over with the player last.
int runKonken(const Ctx& c, bool byMouse, int burn = 0) {
    const char* name = burn == 2 ? "konken_izle" : burn ? "konken_yandi" : byMouse ? "konken" : "kb_konken";
    if (!gOnly.empty() && gOnly != "konken") return 0;
    std::unique_ptr<app::TableGame> g = app::makeTableGame(ui::GameKind::Konken);
    app::TableContext ctx;
    ctx.renderer = c.R;
    if (!g || !g->init(ctx)) {
        std::printf("%s: init failed\n", name);
        return 1;
    }
    ui::Settings st;
    if (burn == 2) st.konkenLimit = 101;
    g->setAnimationSpeed(3.f);
    g->startMatch(st, {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, 1242);
    if (burn) app::konkenDebugBurn(g.get(), 0, burn == 2);
    Vector2 mouse{-1000, -1000};
    KeyPress key;
    int actions = 0, drags = 0, handsDone = 0, idle = 0, frames = 0, failures = 0, wait = 0, step = 0;
    int dragFrame = -1; // a drag in progress: frames since the press
    Vector2 dragFrom{}, dragTo{};
    bool down = false;
    std::vector<std::string> phases;
    const std::vector<int> cyc = {KEY_RIGHT, KEY_SPACE, KEY_RIGHT, KEY_SPACE, KEY_O, KEY_I, KEY_LEFT, KEY_G, KEY_ENTER, KEY_ENTER};
    while ((burn || handsDone < c.hands) && frames < (burn == 2 ? 400000 : 80000)) {
        ++frames;
        frame(c, *g, mouse);
        const std::string ph = g->debugPhase();
        if (!c.out.empty() && !ph.empty() && std::find(phases.begin(), phases.end(), ph) == phases.end()) {
            phases.push_back(ph);
            save(*c.rt, c.out + "/" + name + "_" + ph + ".png");
        }
        if (byMouse) {
            if (dragFrame >= 0) { // move in six steps, then let go
                ++dragFrame;
                const float t = std::min(1.f, (float)dragFrame / 6.f);
                mouse = Vector2Lerp(dragFrom, dragTo, t);
                if (dragFrame == 8) {
                    inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
                    dragFrame = -1;
                    ++drags;
                    if (drags == 2 && !c.out.empty()) save(*c.rt, c.out + "/" + name + "_drag.png");
                }
                continue;
            }
            if (down) {
                inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
                down = false;
                continue;
            }
            if (wait > 0) {
                --wait;
                continue;
            }
            Vector2 p, q;
            if (g->debugHumanClick(*c.R, p)) {
                mouse = p;
                inject(EV_MOUSE_DOWN, MOUSE_BUTTON_LEFT);
                if (app::konkenDebugDragTarget(g.get(), *c.R, q)) {
                    dragFrom = p;
                    dragTo = q;
                    dragFrame = 0;
                } else {
                    down = true;
                }
                ++actions;
                wait = 4;
                idle = 0;
            } else {
                ++idle;
            }
        } else {
            if (key.busy()) {
                key.release();
            } else if (wait > 0) {
                --wait;
            } else {
                Vector2 p;
                if (g->debugHumanClick(*c.R, p)) {
                    if (app::konkenDebugNeedsDraw(g.get())) key.press(step++ % 3 == 2 ? KEY_A : KEY_D);
                    else key.press(cyc[(size_t)(step++ % (int)cyc.size())]);
                    ++actions;
                    wait = 3;
                    idle = 0;
                } else {
                    ++idle;
                }
            }
        }
        if (g->handOver() && !g->animating()) {
            ++handsDone;
            if (g->matchOver()) break;
            g->startNextHand();
        }
        if (idle > 3000) {
            std::printf("%s: STUCK (frame %d, %d actions)\n", name, frames, actions);
            ++failures;
            break;
        }
    }
    if (!c.out.empty()) save(*c.rt, c.out + "/" + name + "_end.png");
    bool ok = handsDone >= std::min(c.hands, 1) && actions > 5 && (!byMouse || drags > 0) && (byMouse || ui::keyboardNav());
    if (burn) { // (son kalan) over, the player burned first: last place
        const int place = app::konkenDebugPlace(g.get(), 0);
        ok = ok && g->matchOver() && place == 4;
        std::printf("%s: the player burned, finished %d.%s\n", name, place, g->matchOver() ? "" : " (match NOT over)");
    }
    std::printf("%s: %d hands, %d %s, %d drags, %d frames — %s\n", name, handsDone, actions, byMouse ? "mouse actions" : "keys (no mouse)",
                drags, frames, ok ? "ok" : "FAILED");
    g->shutdown();
    return failures + (ok ? 0 : 1);
}

} // namespace

int main(int argc, char** argv) {
    Ctx c;
    bool keys = true, mouse = true, cb = false, big = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hands") && i + 1 < argc) c.hands = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) c.out = argv[++i];
        else if (!std::strcmp(argv[i], "--keys")) mouse = false;
        else if (!std::strcmp(argv[i], "--mouse")) keys = false;
        else if (!std::strcmp(argv[i], "--cb")) cb = true;
        else if (!std::strcmp(argv[i], "--big")) big = true;
        else if (!std::strcmp(argv[i], "--no-3d")) c.no3d = true;
        else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) gOnly = argv[++i];
    }
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(320, 180, "tables_check"); // (hidden; everything renders into a 1600x900 texture)
    ui::loadFonts();
    r3d::Renderer R;
    if (!R.init()) return 2;
    RenderTexture2D rt = LoadRenderTexture(1600, 900);
    R.setRenderSize(1600, 900);
    ui::setCurrentViewport(ui::Viewport{});
    ui::tilegfx::setColorBlind(cb);
    ui::cardgfx::setFourColour(cb);
    ui::setHudTextScale(big ? 1.25f : 1.f);
    c.R = &R;
    c.rt = &rt;
    c.cam = seatCamera();
    {
        r3d::PlayerCamera pc;
        pc.setTavlaSeat();
        c.tavlaCam = pc.camera();
    }
    int failures = 0;
    if (mouse)
        for (int k = 0; k < 5; ++k) failures += runMouse(c, k);
    if (keys) {
        for (int k = 0; k < 5; ++k) failures += runKeys(c, k);
        failures += runOkeyKeys(c, false);
        failures += runOkeyKeys(c, false, true); // 101 kuralları: katlamalı + tek kat
        failures += runOkeyKeys(c, true);
    }
    // Tavla çeşitleri: the tavla runs again as Gülbahar (with the cube) and Fevga, by the mouse and by the keyboard
    for (int cesit = 1; cesit <= 2; ++cesit) {
        gTavlaCesit = cesit;
        const int k = cesit == 1 ? 1 : 0;
        const char* const saved = kNames[k];
        kNames[k] = cesit == 1 ? "tavla_gulbahar" : "tavla_fevga";
        if (mouse) failures += runMouse(c, k);
        if (keys) failures += runKeys(c, k);
        kNames[k] = saved;
    }
    gTavlaCesit = 0;
    // İki kişilik pişti (index 2 again): you and Kel Mahmut at the two-seat tavla table (location 1, the tavla seat's
    // camera), by the mouse and by the keyboard
    {
        gPistiMode = 2;
        const char* const saved = kNames[2];
        kNames[2] = "pisti_ikili";
        if (mouse) failures += runMouse(c, 2);
        if (keys) failures += runKeys(c, 2);
        kNames[2] = saved;
        gPistiMode = 0;
    }
    // Dama (index 5): the mouse clicks a disc, then its square; the keyboard walks the discs / moves with the arrows
    if (mouse) failures += runMouse(c, 5);
    if (keys) failures += runKeys(c, 5);
    // Bezik (index 6): you and Kel Mahmut at the two-seat table; the declaration panel by the mouse and the keyboard
    if (mouse) failures += runMouse(c, 6);
    if (keys) failures += runKeys(c, 6);
    // Altmışaltı (index 7): you and Kel Mahmut at the two-seat table, by the mouse and by the keyboard
    if (mouse) failures += runMouse(c, 7);
    if (keys) failures += runKeys(c, 7);
    // Konken: its own runner (drags onto the melds and the discard pile; D / A to draw)
    if (mouse) failures += runKonken(c, true);
    if (keys) failures += runKonken(c, false);
    if (mouse) failures += runKonken(c, true, 1); // (son kalan) the player burns, then "Sonuca geç"
    if (mouse) failures += runKonken(c, true, 2); // ... then "Hızlı izle" to the end
    UnloadRenderTexture(rt);
    R.shutdown();
    ui::unloadFonts();
    CloseWindow();
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
