// SaklıBahçe: the other table games (Tavla, Pişti, Batak, King) played by the mouse. A hidden window renders the
// table from the player's seat; every decision of the player's seat is made with a real click (raylib automation
// events through the same input path as the game) on what the game draws: a card in the fan, a point of the tavla
// board, a button of the ihale / koz / contract panel, "Zar At". The game must keep moving; a few frames are saved.
//
//   build: make tools/tables_check (see the Makefile) or link against build/make/src/{core,ui,r3d}/*.o and
//          build/make/src/app/{CardTable,BatakTable,KingTable,PistiTable,TavlaTable,TableGames}.o
//   run:   tables_check [--hands N] [--out DIR]
#include "app/TableGame.h"
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/World.h"
#include "ui/Common.h"

#include <raylib.h>
#include <raymath.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr unsigned EV_MOUSE_UP = 5, EV_MOUSE_DOWN = 6;
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

} // namespace

int main(int argc, char** argv) {
    int hands = 2;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hands") && i + 1 < argc) hands = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
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
    const Camera3D cam = seatCamera();
    int failures = 0;

    const ui::GameKind kinds[] = {ui::GameKind::Tavla, ui::GameKind::Pisti, ui::GameKind::Batak, ui::GameKind::King};
    const char* names[] = {"tavla", "pisti", "batak", "king"};
    for (int k = 0; k < 4; ++k) {
        std::unique_ptr<app::TableGame> g = app::makeTableGame(kinds[k]);
        app::TableContext ctx;
        ctx.renderer = &R;
        if (!g || !g->init(ctx)) {
            std::printf("%s: init failed\n", names[k]);
            ++failures;
            continue;
        }
        ui::Settings st;
        st.tavlaPoints = 3;
        g->setAnimationSpeed(3.f);
        g->startMatch(st, {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, 1234 + (uint64_t)k);
        Vector2 mouse{-1000, -1000};
        int clicks = 0, handsDone = 0, idle = 0, frames = 0;
        bool shot = false;
        int pressPhase = 0; // 0 none, 1 pressed this frame (release next)
        while (handsDone < hands && frames < 60000) {
            ++frames;
            const float dt = 1.f / 30.f;
            g->update(dt, cam, mouse, true, false);
            BeginDrawing();
            g->submit(R);
            R.render(cam, &rt, Color{20, 14, 10, 255});
            BeginTextureMode(rt);
            g->drawHUD(R, mouse, false);
            EndTextureMode();
            EndDrawing();
            if (pressPhase == 1) {
                inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
                pressPhase = 0;
            } else {
                Vector2 p;
                if (g->debugHumanClick(R, p)) {
                    mouse = p;
                    inject(EV_MOUSE_DOWN, MOUSE_BUTTON_LEFT);
                    pressPhase = 1;
                    ++clicks;
                    idle = 0;
                    if (!shot && clicks == 6 && !out.empty()) {
                        Image img = LoadImageFromTexture(rt.texture);
                        ImageFlipVertical(&img);
                        ExportImage(img, (out + "/" + names[k] + "_click.png").c_str());
                        UnloadImage(img);
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
                std::printf("%s: STUCK (frame %d, %d clicks)\n", names[k], frames, clicks);
                ++failures;
                break;
            }
        }
        const bool ok = handsDone >= std::min(hands, 1) && clicks > 3;
        std::printf("%s: %d hands, %d clicks by the player's mouse, %d frames — %s\n", names[k], handsDone, clicks, frames,
                    ok ? "ok" : "FAILED");
        if (!ok) ++failures;
        g->shutdown();
    }
    UnloadRenderTexture(rt);
    R.shutdown();
    ui::unloadFonts();
    CloseWindow();
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
