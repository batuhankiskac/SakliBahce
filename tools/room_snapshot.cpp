// Room owner's visual check: renders the kıraathane from the human's seat and from other angles into PNGs
// (build/room/*.png). A placeholder table and grey capsules stand in for Table3D and Characters.
//   usage: room_snapshot [outdir] [seconds]
#include "r3d/Gfx.h"
#include "r3d/Room.h"
#include "r3d/World.h"
#include "ui/Common.h"

#include <raymath.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace r3d;

namespace {

Camera3D lookFrom(Vector3 eye, float yawDeg, float pitchDeg, float fovy = w3d::FOVY_DEG) {
    float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    Vector3 f{-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    Camera3D c{};
    c.position = eye;
    c.target = Vector3Add(eye, f);
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}
Camera3D lookAt(Vector3 eye, Vector3 target, float fovy = 60.f) {
    Camera3D c{};
    c.position = eye;
    c.target = target;
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

struct Stand {  // placeholder table + seated people (Table3D and Characters draw the real ones)
    Mesh top{}, felt{}, leg{}, torso{}, shoulders{}, head{}, arm{}, chair{};
    Mat wood, feltM, skin, cloth[5];
    void init(Renderer& R) {
        top = genRoundedBox(1.24f, 0.05f, 1.24f, 0.02f, 3);
        felt = genRoundedBox(1.1f, 0.01f, 1.1f, 0.003f, 1);
        leg = genRoundedBox(0.07f, 0.72f, 0.07f, 0.01f, 2);
        MeshBuilder b;
        // seated figure in its seat frame: faces -Z, hips over the chair seat, head at HEAD_Y
        b.capsule({0, 0.62f, 0.02f}, {0, 0.98f, -0.02f}, 0.155f, 14);
        b.ellipsoid({0, 0.52f, -0.12f}, {0.16f, 0.09f, 0.22f}, 8, 12);  // thighs
        torso = b.build();
        b.clear();
        b.ellipsoid({0, 1.03f, -0.02f}, {0.22f, 0.07f, 0.11f}, 8, 14);
        shoulders = b.build();
        b.clear();
        b.capsule({0.2f, 1.02f, -0.02f}, {0.21f, 0.86f, -0.2f}, 0.05f, 8);
        b.capsule({0.21f, 0.86f, -0.2f}, {0.13f, 0.8f, -0.42f}, 0.045f, 8);
        b.capsule({-0.2f, 1.02f, -0.02f}, {-0.21f, 0.86f, -0.2f}, 0.05f, 8);
        b.capsule({-0.21f, 0.86f, -0.2f}, {-0.13f, 0.8f, -0.42f}, 0.045f, 8);
        arm = b.build();
        b.clear();
        b.cylinder({0, 1.08f, -0.02f}, 0.045f, 0.08f, 10);
        b.ellipsoid({0, 1.22f, -0.03f}, {0.085f, 0.105f, 0.095f}, 10, 14);
        head = b.build();
        b.clear();
        b.roundedBox({0, w3d::CHAIR_SEAT_Y - 0.02f, 0}, {0.4f, 0.04f, 0.4f}, 0.01f, 2);
        b.roundedBox({0, w3d::CHAIR_SEAT_Y + 0.25f, 0.19f}, {0.36f, 0.4f, 0.03f}, 0.01f, 2);
        for (int k = 0; k < 4; ++k)
            b.box({k % 2 ? 0.17f : -0.17f, (w3d::CHAIR_SEAT_Y - 0.04f) * 0.5f, k < 2 ? 0.17f : -0.17f}, {0.03f, w3d::CHAIR_SEAT_Y - 0.04f, 0.03f});
        chair = b.build();
        wood = R.makeMat(Color{110, 66, 36, 255}, Texture2D{}, 0.3f, 30.f);
        feltM = R.makeMat(Color{30, 88, 56, 255}, Texture2D{}, 0.03f, 6.f);
        skin = R.makeMat(Color{196, 146, 112, 255}, Texture2D{}, 0.1f, 10.f, 0.f, 0.25f);
        const Color cc[5] = {{70, 62, 56, 255}, {150, 146, 140, 255}, {52, 58, 72, 255}, {96, 72, 52, 255}, {120, 110, 96, 255}};
        for (int i = 0; i < 5; ++i) cloth[i] = R.makeMat(cc[i], Texture2D{}, 0.05f, 8.f, 0.f, 0.3f);
    }
    void person(Renderer& R, Vector3 p, float yawDeg, int look, bool withChair) {
        Matrix M = MatrixMultiply(MatrixRotateY(yawDeg * DEG2RAD), MatrixTranslate(p.x, 0, p.z));
        const Mat& c = cloth[look % 5];
        R.submit(&torso, &c, M);
        R.submit(&shoulders, &c, M);
        R.submit(&arm, &c, M);
        R.submit(&head, &skin, M);
        if (withChair) R.submit(&chair, &wood, M);
    }
    void submit(Renderer& R, bool withHuman) {
        R.submit(&top, &wood, MatrixTranslate(0, w3d::TABLE_Y - 0.03f, 0));
        R.submit(&felt, &feltM, MatrixTranslate(0, w3d::TABLE_Y - 0.004f, 0));
        for (int k = 0; k < 4; ++k)
            R.submit(&leg, &wood, MatrixTranslate(k % 2 ? 0.52f : -0.52f, 0.36f, k < 2 ? 0.52f : -0.52f));
        for (int s = 0; s < 4; ++s) {
            if (s == 0 && !withHuman) {
                R.submit(&chair, &wood, MatrixTranslate(w3d::seatPos(0).x, 0, w3d::seatPos(0).z));
                continue;
            }
            person(R, w3d::seatPos(s), w3d::seatYawDeg(s), s, true);
        }
        // patrons at the background tables (Room draws their chairs)
        int look = 0;
        for (const w3d::BgTable& t : w3d::BG_TABLES)
            for (int s = 0; s < 4; ++s)
                if ((t.sides >> s) & 1u) {
                    Vector3 p{t.x + w3d::SEAT_DIR[s].x * w3d::BG_CHAIR_DIST, 0, t.z + w3d::SEAT_DIR[s].z * w3d::BG_CHAIR_DIST};
                    person(R, p, w3d::seatYawDeg(s), ++look, false);
                }
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "build/room";
    float seconds = argc > 2 ? (float)std::atof(argv[2]) : 5.f;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
    InitWindow(320, 180, "room_snapshot");
    ui::loadFonts();
    Renderer R;
    if (!R.init()) return 1;
    ui::setCurrentViewport(ui::Viewport{});
    RenderTexture2D rt = LoadRenderTexture(1600, 900);
    RenderTexture2D small = LoadRenderTexture(320, 180);

    auto t0 = std::chrono::steady_clock::now();
    Room room;
    if (!room.init(R, 0xC0FFEEull)) return 2;
    auto t1 = std::chrono::steady_clock::now();
    std::printf("room init: %.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
    int sfxCount = 0;
    room.playSfx = [&](ui::Sfx s) {
        ++sfxCount;
        std::printf("  sfx %d at frame\n", (int)s);
    };
    room.setScoreboard("El 2 / 5", {"Sen ........ 87", "Hacı Rıza ........ 143", "Kel Mahmut ........ 212", "Emekli Nuri ........ -101"});
    Stand stand;
    stand.init(R);

    // simulate (render tiny frames so smoke gets emitted and aged like in the game)
    const float dt = 1.f / 60.f;
    Camera3D seat = lookFrom(w3d::EYE, 0.f, w3d::EYE_PITCH_DEG);
    int frames = (int)(seconds / dt);
    R.setRenderSize(320, 180);
    for (int i = 0; i < frames; ++i) {
        R.update(dt);
        room.update(dt);
        room.submit(R);
        R.render(seat, &small, BLACK);
        if (room.consumeTvGoal()) std::printf("  TV goal at %.1fs\n", i * dt);
    }
    R.setRenderSize(1600, 900);

    struct Shot {
        const char* name;
        Camera3D cam;
        bool human;
    };
    const Vector3 eye = w3d::EYE;
    Shot shots[] = {
        {"01_seat", lookFrom(eye, 0.f, w3d::EYE_PITCH_DEG), false},
        {"02_seat_up", lookFrom(eye, 0.f, -6.f), false},
        {"03_left", lookFrom(eye, 75.f, -10.f), false},
        {"04_right", lookFrom(eye, -75.f, -10.f), false},
        {"05_back", lookFrom(eye, 165.f, -6.f), false},
        {"06_ceiling", lookFrom(eye, 10.f, 32.f), false},
        {"07_corner_fr", lookAt({3.25f, 2.55f, 3.4f}, {-1.0f, 0.9f, -1.4f}, 64.f), true},
        {"08_corner_bl", lookAt({-2.9f, 2.5f, -3.2f}, {1.4f, 0.8f, 2.0f}, 64.f), true},
        {"09_counter", lookAt({1.9f, 1.45f, -1.2f}, {3.0f, 1.2f, -3.2f}, 50.f), true},
        {"10_board", lookAt({-0.6f, 1.35f, -1.6f}, {-1.1f, 1.45f, -3.4f}, 45.f), true},
        {"11_tv", lookAt({-2.3f, 2.1f, -2.3f}, {-3.7f, 2.42f, -2.9f}, 40.f), true},
        {"12_ashtray", lookAt({-0.2f, 1.02f, -0.22f}, {-0.4f, 0.77f, -0.49f}, 45.f), true},
        {"13_tavla", lookAt({-2.1f, 1.45f, -1.2f}, {-2.5f, 0.75f, -1.8f}, 50.f), true},
        {"14_window", lookAt({-2.6f, 1.4f, -0.2f}, {-4.2f, 1.6f, -1.6f}, 55.f), true},
        {"15_door", lookAt({-1.9f, 1.5f, 1.2f}, {-4.2f, 1.2f, 2.9f}, 55.f), true},
        {"16_bench", lookAt({-0.6f, 1.45f, 1.5f}, {-0.9f, 0.9f, 3.6f}, 60.f), true},
        {"17_outside", lookAt({-3.3f, 1.5f, 2.8f}, {-8.f, 0.8f, 2.6f}, 60.f), true},
        {"19_plant", lookAt({0.2f, 1.3f, -1.9f}, {0.55f, 0.7f, -3.2f}, 55.f), true},
    };
    const std::string only = argc > 3 ? argv[3] : "";
    for (Shot& s : shots) {
        if (!only.empty() && std::string(s.name).find(only) == std::string::npos) continue;
        // a few frames per shot so billboards (dust shafts) face this camera
        for (int k = 0; k < 3; ++k) {
            R.update(dt);
            room.update(dt);
            room.submit(R);
            stand.submit(R, s.human);
            auto a = std::chrono::steady_clock::now();
            R.render(s.cam, &rt, BLACK);
            auto b = std::chrono::steady_clock::now();
            if (k == 2 && s.name[1] == '1' && s.name[0] == '0')
                std::printf("render (cpu submit->return): %.2f ms, particles %d\n", std::chrono::duration<double, std::milli>(b - a).count(),
                            R.particleCount());
        }
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        std::string path = out + "/" + s.name + ".png";
        ExportImage(img, path.c_str());
        UnloadImage(img);
    }
    // "goal": fast-forward the TV match to the next goal and grab the screen during the celebration
    if (only == "goal") {
        float t = 0.f;
        bool got = false;
        while (t < 1500.f && !got) {
            room.update(0.05f);
            t += 0.05f;
            got = room.consumeTvGoal();
        }
        for (int i = 0; i < 20; ++i) room.update(0.05f);
        std::printf("goal after %.0f s of play: %s\n", t, got ? "yes" : "no");
        Camera3D tc = lookAt({-2.9f, 2.45f, -2.2f}, {-3.7f, 2.53f, -2.9f}, 38.f);
        for (int k = 0; k < 3; ++k) {
            R.update(dt);
            room.update(dt);
            room.submit(R);
            R.render(tc, &rt, BLACK);
        }
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        ExportImage(img, (out + "/20_goal.png").c_str());
        UnloadImage(img);
    }
    // title mode (menu backdrop): App flies a slow cinematic camera through the room
    if (only.empty() || std::string("18_title").find(only) != std::string::npos) {
        room.setTitleMode(true);
        R.setRenderSize(320, 180);
        for (int i = 0; i < 120; ++i) {
            R.update(dt);
            room.update(dt);
            room.submit(R);
            R.render(seat, &small, BLACK);
        }
        R.setRenderSize(1600, 900);
        Camera3D tc = lookAt({2.9f, 1.75f, 2.6f}, {-0.6f, 1.05f, -1.2f}, 55.f);
        for (int k = 0; k < 3; ++k) {
            R.update(dt);
            room.update(dt);
            room.submit(R);
            stand.submit(R, false);
            R.render(tc, &rt, BLACK);
        }
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        ExportImage(img, (out + "/18_title.png").c_str());
        UnloadImage(img);
        room.setTitleMode(false);
    }
    std::printf("sfx played: %d\n", sfxCount);
    room.shutdown(R);
    UnloadRenderTexture(rt);
    UnloadRenderTexture(small);
    R.shutdown();
    ui::unloadFonts();
    CloseWindow();
    return 0;
}
