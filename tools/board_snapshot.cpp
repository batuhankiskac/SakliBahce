// The opponent's hands at the two-seat tavla table, close up (Rakip / round 5): a dama disc taken up, carried hopping
// over two discs and put down, the two taken discs lifted off the board by hand; a card drawn off the stock into the
// held fan; a card carried from the declared row onto the trick (Bezik). The real room, people, board and cards (no
// game engine): each scene drives Characters::carry / drawCard like DamaTable and the card tables do and saves pictures
// from the player's tavla seat and from the side.
//
//   build: make build/make/board_snapshot      (or: make BUILD=... boardsnapshot)
//   run:   board_snapshot [outdir] [opponent seat 1..3] [scene-substring]     (headless: xvfb-run -a ...)
#include "r3d/Cards3D.h"
#include "r3d/Characters.h"
#include "r3d/Dama3D.h"
#include "r3d/Gfx.h"
#include "r3d/Room.h"
#include "r3d/Table3D.h"
#include "core/Game.h"
#include "r3d/World.h"
#include "ui/CardRender.h"
#include "ui/Common.h"

#include <raylib.h>
#include <raymath.h>

#include <array>
#include <cstdio>
#include <cstdlib>
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
Vector3 tw(float x, float y, float z) { return w3d::tavlaToWorld({x, y, z}); }
Camera3D seatCam() { return cam(tw(0.f, 1.18f, 0.62f), tw(0.f, w3d::TABLE_Y + 0.05f, -0.12f), 52.f); }
Camera3D sideCam() { return cam(tw(0.95f, 1.12f, -0.15f), tw(0.f, w3d::TABLE_Y + 0.08f, -0.25f), 44.f); }

struct H {
    Renderer R;
    Room room;
    Characters people;
    Cards3D cards;
    Dama3D board;
    okey::Game game;
    Table3D table;
    RenderTexture2D rt{};
    std::string out = "build/board";
    std::string only;
    int opp = 2;
    bool showBoard = true;
    std::vector<int> fan; // the opponent's held cards

    void frame(float dt) {
        room.update(dt);
        people.update(dt, seatCam());
        Matrix f;
        const int n = (int)fan.size();
        if (n > 0 && people.cardFan(opp, f))
            for (int i = 0; i < n; ++i) cards.follow(fan[(size_t)i], cardlayout::fanCard(f, i, n));
        cards.update(dt);
        board.update(dt);
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
        if (showBoard) board.submit(R);
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
    void both(const std::string& name) {
        shot(name + "_seat", seatCam());
        shot(name + "_side", sideCam());
    }
};

} // namespace

int main(int argc, char** argv) {
    H h;
    if (argc > 1) h.out = argv[1];
    if (argc > 2) h.opp = std::atoi(argv[2]) >= 1 && std::atoi(argv[2]) <= 3 ? std::atoi(argv[2]) : 2;
    if (argc > 3) h.only = argv[3];
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(320, 180, "board_snapshot");
    ui::loadFonts();
    if (!h.R.init()) return 2;
    h.rt = LoadRenderTexture(1600, 900);
    h.R.setRenderSize(1600, 900);
    ui::setCurrentViewport(ui::Viewport{});
    if (!h.room.init(h.R, 11) || !h.people.init(h.R, 11) || !h.cards.init(h.R) || !h.table.init(h.R, &h.game) ||
        !h.board.init(h.R))
        return 3;
    h.table.setFurnitureOnly(true);
    h.people.setNames({"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
    h.people.setActiveSeat(-1);
    h.people.banter().setEnabled(false);
    h.room.setTavlaFocus(true);
    h.people.setTavlaTable(true, h.opp);
    h.board.setFrame(w3d::tavlaFrame());
    // ---- dama: the opponent (player 1, dark, rows 5..6 at the far side) takes two of the player's discs in a chain
    std::array<int8_t, 64> cells{};
    for (int c = 0; c < 8; ++c) cells[(size_t)(6 * 8 + c)] = -1;
    for (int c = 0; c < 8; ++c) cells[(size_t)(1 * 8 + c)] = 1;
    cells[5 * 8 + 2] = -1; // the one that moves
    cells[4 * 8 + 2] = 1;  // taken
    cells[2 * 8 + 2] = 1;  // taken
    cells[1 * 8 + 2] = 0;  // (the landing after the second jump is the row behind: free it)
    cells[2 * 8 + 5] = 1;
    h.board.setBoard(cells, 0, true);
    h.run(1.0f);
    {
        PieceCarry hand;
        h.board.playMove(1, 5 * 8 + 2, {3 * 8 + 2, 1 * 8 + 2}, {4 * 8 + 2, 2 * 8 + 2}, false, w3d::BOT_TAKE_LEAD, &hand);
        h.people.carry(h.opp, hand);
        h.run(w3d::BOT_TAKE_LEAD - 0.12f);
        h.both("dama_1_reach");
        h.run(0.12f);
        h.both("dama_2_take");
        h.run(hand.seg * 0.5f);
        h.both("dama_3_hop1");
        h.run(hand.seg * 0.5f);
        h.both("dama_4_land1");
        h.run(hand.seg * 0.5f);
        h.both("dama_5_hop2");
        h.run(hand.seg * 0.5f + 0.05f);
        h.both("dama_6_down");
        if (!hand.takes.empty()) {
            const float t0 = w3d::BOT_TAKE_LEAD + hand.seg * 2.f + 0.05f;
            h.run(hand.takes[0].at - t0);
            h.both("dama_7_pick1");
            h.run(hand.takes[0].dur * 0.5f);
            h.both("dama_8_carry1");
            h.run(hand.takes[0].dur * 0.5f + 0.05f);
            h.both("dama_9_drop1");
            if (hand.takes.size() > 1) {
                h.run(hand.takes[1].at - hand.takes[0].at - hand.takes[0].dur - 0.05f);
                h.both("dama_10_pick2");
                h.run(hand.takes[1].dur + 0.6f);
            }
        }
        h.run(1.5f);
        h.both("dama_11_after");
    }
    // ---- cards: he holds a fan; draws the top card of the stock into it
    h.showBoard = false;
    const Vector3 stockAt = tw(-0.30f, w3d::TABLE_Y + r3d::CARD_T * 4.f, 0.f);
    for (int c = 0; c < 6; ++c) h.fan.push_back(c * 3);
    for (int c : h.fan) h.cards.place(c, cardlayout::hand(2, 0, 1), false);
    h.people.holdCards(h.opp, true);
    for (int i = 0; i < 5; ++i) {
        CardPose p;
        p.pos = Vector3Add(stockAt, {0.f, -r3d::CARD_T * (float)(4 - i), 0.f});
        p.rot = QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, w3d::TAVLA_YAW_DEG * DEG2RAD), cardFlat(4.f, false));
        h.cards.place(40 + i, p, false);
    }
    h.run(1.5f);
    h.both("draw_0_hold");
    {
        const int c = 44; // the top one
        Matrix f;
        CardPose to = h.cards.pose(c);
        if (h.people.cardFan(h.opp, f)) to = cardlayout::fanCard(f, 3, 7);
        h.cards.place(c, to, true, w3d::BOT_TAKE_LEAD, 0.05f);
        const float fly = std::clamp(0.22f + Vector3Distance(stockAt, to.pos) * 0.55f, 0.25f, 0.75f);
        h.people.drawCard(h.opp, stockAt, fly);
        h.run(w3d::BOT_TAKE_LEAD);
        h.both("draw_1_take");
        h.fan.insert(h.fan.begin() + 3, c); // (in the fan from now on: followed while in flight)
        h.run(fly * 0.5f);
        h.both("draw_2_up");
        h.run(fly * 0.5f + 0.1f);
        h.both("draw_3_in");
        h.run(1.0f);
    }
    // ---- Bezik: a card of his declared row carried onto the trick
    {
        const int c = 30;
        CardPose row;
        row.pos = tw(0.02f, w3d::TABLE_Y + r3d::CARD_T, -0.30f * w3d::TAVLA_HALF_D / w3d::FELT_HALF);
        row.rot = QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, w3d::TAVLA_YAW_DEG * DEG2RAD), cardFlat(0.f, true));
        h.cards.place(c, row, false);
        h.run(0.5f);
        CardPose trick = row;
        trick.pos = tw(0.03f, w3d::TABLE_Y + r3d::CARD_T, -0.04f);
        h.cards.place(c, trick, true, w3d::BOT_TAKE_LEAD, 0.06f);
        const float fly = std::clamp(0.22f + Vector3Distance(row.pos, trick.pos) * 0.55f, 0.25f, 0.75f);
        PieceCarry pc;
        pc.card = true;
        pc.lead = w3d::BOT_TAKE_LEAD;
        pc.seg = fly;
        pc.hop = 0.06f;
        pc.path = {row.pos, trick.pos};
        h.people.carry(h.opp, pc);
        h.run(w3d::BOT_TAKE_LEAD);
        h.both("row_1_take");
        h.run(fly * 0.5f);
        h.both("row_2_carry");
        h.run(fly * 0.5f + 0.05f);
        h.both("row_3_down");
    }

    UnloadRenderTexture(h.rt);
    h.board.shutdown(h.R);
    h.cards.shutdown(h.R);
    h.table.shutdown(h.R);
    h.people.shutdown(h.R);
    h.room.shutdown(h.R);
    h.R.shutdown();
    ui::unloadFonts();
    CloseWindow();
    return 0;
}
