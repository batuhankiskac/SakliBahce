// The regulars' hands at a card game, close up: the fan held in the left hand, a card pulled out and laid on the felt,
// a trick gathered, a pile picked up, the dealer's riffle and deal. The real room, people and cards (no game engine):
// each scene drives r3d::Characters' card calls and places the cards like CardTableBase does, then saves pictures
// from the player's seat and from across the table.
//
//   build: make build/make/cards_snapshot
//   run:   cards_snapshot [outdir] [scene-substring]     (headless: xvfb-run -a ...)
#include "r3d/Cards3D.h"
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/Room.h"
#include "r3d/Table3D.h"
#include "core/Game.h"
#include "r3d/World.h"
#include "ui/CardRender.h"
#include "ui/Common.h"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace r3d;

namespace {

Camera3D cam(Vector3 pos, Vector3 target, float fovy) {
    Camera3D c{};
    c.position = pos;
    c.target = target;
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}
Camera3D seatCam() {
    const float p = w3d::EYE_PITCH_DEG * DEG2RAD;
    return cam(w3d::EYE, {w3d::EYE.x, w3d::EYE.y + std::sin(p), w3d::EYE.z - std::cos(p)}, w3d::FOVY_DEG);
}
// From the far side of the table, looking at seat s's hands.
Camera3D closeCam(int s) {
    return cam(w3d::seatLocal(s, 0.10f, -0.30f, 1.12f), w3d::seatLocal(s, -0.02f, 0.50f, 0.90f), 40.f);
}
// From the seat's own right, slightly behind: the fan's faces.
Camera3D overShoulder(int s) {
    return cam(w3d::seatLocal(s, 0.45f, 0.95f, 1.45f), w3d::seatLocal(s, -0.05f, 0.25f, 0.90f), 42.f);
}

struct H {
    Renderer R;
    Room room;
    Characters people;
    Cards3D cards;
    okey::Game game;
    Table3D table;
    RenderTexture2D rt{};
    std::string out = "build/cards";
    std::string only;
    std::array<std::vector<int>, 4> hands;
    bool held[4] = {false, false, false, false};

    void frame(float dt) {
        room.update(dt);
        people.update(dt, seatCam());
        for (int s = 1; s < 4; ++s) {
            Matrix f;
            const int n = (int)hands[(size_t)s].size();
            if (held[s] && people.cardFan(s, f))
                for (int i = 0; i < n; ++i) cards.follow(hands[(size_t)s][(size_t)i], cardlayout::fanCard(f, i, n));
        }
        const int n0 = (int)hands[0].size();
        for (int i = 0; i < n0; ++i) cards.place(hands[0][(size_t)i], cardlayout::hand(0, i, n0), true);
        cards.update(dt);
        R.update(dt);
    }
    void run(float seconds) {
        for (float t = 0.f; t < seconds; t += 1.f / 60.f) frame(1.f / 60.f);
    }
    void shot(const std::string& name, const Camera3D& c) {
        if (!only.empty() && name.find(only) == std::string::npos) return;
        BeginDrawing();
        room.submit(R);
        table.submit(R);
        people.submit(R);
        cards.submit(R);
        R.render(c, &rt, Color{20, 14, 10, 255});
        EndDrawing();
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        const std::string path = out + "/" + name + ".png";
        ExportImage(img, path.c_str());
        UnloadImage(img);
        std::printf("wrote %s\n", path.c_str());
    }
};

} // namespace

int main(int argc, char** argv) {
    H h;
    if (argc > 1) h.out = argv[1];
    if (argc > 2) h.only = argv[2];
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(320, 180, "cards_snapshot");
    ui::loadFonts();
    if (!h.R.init()) return 2;
    h.rt = LoadRenderTexture(1600, 900);
    h.R.setRenderSize(1600, 900);
    ui::setCurrentViewport(ui::Viewport{});
    if (!h.room.init(h.R, 11) || !h.people.init(h.R, 11) || !h.cards.init(h.R) || !h.table.init(h.R, &h.game)) return 3;
    h.table.setFurnitureOnly(true);
    h.people.setNames({"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
    h.people.setActiveSeat(-1);
    h.people.banter().setEnabled(false);
    h.run(1.0f);

    // ---- the deal: Rıza shuffles and deals 13 each
    for (int c = 0; c < 52; ++c) h.cards.place(c, cardlayout::deck(1, c), false);
    h.people.shuffleDeck(1, cardlayout::deck(1, 20).pos, 1.45f);
    h.run(0.7f);
    h.shot("deal_1_shuffle_seat", seatCam());
    h.shot("deal_1_shuffle_close", closeCam(1));
    h.run(0.9f);
    std::vector<Vector3> to;
    for (int k = 0; k < 52; ++k) to.push_back(cardlayout::dealtPile((1 + 1 + k) % 4, 0).pos);
    h.people.dealCards(1, cardlayout::deck(1, 52).pos, to, 0.045f);
    std::array<int, 4> cnt{};
    for (int k = 0; k < 52; ++k) {
        const int s = (2 + k) % 4;
        h.cards.place(51 - k, cardlayout::dealtPile(s, cnt[(size_t)s]++), true, 0.045f * (float)k, 0.01f);
        h.hands[(size_t)s].push_back(51 - k);
    }
    h.run(1.2f);
    h.shot("deal_2_dealing_seat", seatCam());
    h.shot("deal_2_dealing_close", closeCam(1));
    h.run(1.8f);
    // ---- the pick-up
    for (int s = 1; s < 4; ++s) {
        const Vector3 at = cardlayout::dealtPile(s, 0).pos;
        h.people.holdCards(s, true, &at);
    }
    h.run(0.36f);
    for (int s = 1; s < 4; ++s) h.held[s] = true;
    h.run(0.2f);
    h.shot("pick_1_seat", seatCam());
    h.shot("pick_1_close2", closeCam(2));
    h.run(1.0f);
    // ---- holding
    h.shot("hold_seat", seatCam());
    for (int s = 1; s < 4; ++s) {
        h.shot("hold_close" + std::to_string(s), closeCam(s));
        h.shot("hold_faces" + std::to_string(s), overShoulder(s));
    }
    h.run(4.f);
    h.shot("hold_later_seat", seatCam());
    // ---- Mahmut plays a card onto the trick, Nuri tosses one
    {
        const int c = h.hands[2].back();
        h.hands[2].pop_back();
        const CardPose p = cardlayout::trick(2, 3);
        h.cards.place(c, p, true, w3d::BOT_GIVE_LEAD, 0.06f);
        h.people.playCard(2, p.pos, false);
        h.run(w3d::BOT_GIVE_LEAD);
        h.shot("play_1_pull_close2", closeCam(2));
        h.run(0.25f);
        h.shot("play_2_carry_seat", seatCam());
        h.shot("play_2_carry_close2", closeCam(2));
        h.run(0.3f);
        h.shot("play_3_down_close2", closeCam(2));
        h.run(1.0f);
    }
    {
        const int c = h.hands[3].back();
        h.hands[3].pop_back();
        const CardPose p = cardlayout::trick(3, 5);
        h.cards.place(c, p, true, w3d::BOT_GIVE_LEAD, 0.09f);
        h.people.playCard(3, p.pos, true);
        h.run(w3d::BOT_GIVE_LEAD + 0.3f);
        h.shot("play_4_toss_seat", seatCam());
        h.run(1.2f);
    }
    // ---- Rıza takes the trick
    {
        const int a = h.hands[1].back();
        h.hands[1].pop_back();
        h.cards.place(a, cardlayout::trick(1, 2), false);
        h.people.gatherCards(1, {0.f, w3d::TABLE_Y, 0.f}, cardlayout::wonPile(1, 0).pos);
        h.run(0.5f);
        h.shot("gather_1_seat", seatCam());
        h.shot("gather_1_close1", closeCam(1));
        h.run(0.4f);
        h.shot("gather_2_close1", closeCam(1));
        h.run(1.0f);
    }
    // ---- the fan put down
    for (int s = 1; s < 4; ++s) h.people.holdCards(s, false);
    for (int s = 1; s < 4; ++s) h.held[s] = false;
    h.run(1.0f);
    h.shot("down_seat", seatCam());

    UnloadRenderTexture(h.rt);
    h.cards.shutdown(h.R);
    h.table.shutdown(h.R);
    h.people.shutdown(h.R);
    h.room.shutdown(h.R);
    h.R.shutdown();
    ui::unloadFonts();
    CloseWindow();
    return 0;
}
