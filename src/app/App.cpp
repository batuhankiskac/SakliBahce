// Kıraathane 101 — the application: window, main loop, match flow, bot pacing, and the wiring between
// the engine (okey::Game / okey::Bot), the 3D world (Room, Characters, Table3D, PlayerCamera), audio and
// the menu screens. Frame structure: DESIGN3D.md §2.
#include "app/App.h"

#include "core/Bot.h"
#include "core/Game.h"
#include "core/Rng.h"
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/Room.h"
#include "r3d/Table3D.h"
#include "r3d/World.h"
#include "ui/Audio.h"
#include "ui/Common.h"
#include "ui/Screens.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace app {

namespace {

constexpr int HUMAN = 0;
constexpr const char* BOT_NAMES[4] = {"", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"};
constexpr Vector2 NO_MOUSE{-10000.f, -10000.f};
constexpr Color CLEAR_COLOR{14, 10, 7, 255};
constexpr int SNAP_W = 1600, SNAP_H = 900;
constexpr Vector3 TV_POS{-3.70f, 2.50f, -2.90f};  // the CRT high in the back-left corner (RoomBuild.cpp)

constexpr float SUMMARY_DELAY = 2.0f;   // seconds after a hand ends before the score sheet
constexpr float AUTO_SUMMARY = 2.5f;    // autoplay: how long the score sheet stays up
constexpr float AUTO_MATCHOVER = 3.0f;  // autoplay: how long the final standings stay up
constexpr float AUTO_TITLE = 2.0f;      // autoplay (--matches): time on the title screen before "Oyna"
constexpr float LANDING_LAG = 0.30f;    // tile flight minus Audio's own 0.12 s gap: clacks land with the tile
constexpr int MAX_BOT_ACTIONS = 60;     // watchdog: a bot turn never takes more actions than this

uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t clockSeed() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return mix64((uint64_t)now);
}

const char* actionName(okey::BotAction::Kind k) {
    using K = okey::BotAction::Kind;
    switch (k) {
    case K::DrawPile: return "DrawPile";
    case K::TakeLeft: return "TakeLeft";
    case K::ReturnLeft: return "ReturnLeft";
    case K::Open: return "Open";
    case K::LayMelds: return "LayMelds";
    case K::AddToMeld: return "AddToMeld";
    case K::SwapJoker: return "SwapJoker";
    case K::Discard: return "Discard";
    }
    return "?";
}

Vector3 lookDir(float yawDeg, float pitchDeg) {
    const float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

// Extra snapshot cameras for judging the space (the seat view is the PlayerCamera itself).
Camera3D viewCamera(const std::string& view, const Camera3D& seat) {
    Camera3D c = seat;
    c.up = {0, 1, 0};
    c.projection = CAMERA_PERSPECTIVE;
    if (view == "left" || view == "right") {
        const float yaw = view == "left" ? -76.f : 76.f;
        const Vector3 d = lookDir(yaw, -13.f);
        c.target = {c.position.x + d.x, c.position.y + d.y, c.position.z + d.z};
        c.fovy = w3d::FOVY_DEG;
    } else if (view == "back") { // behind Kel Mahmut, looking back at our seat and the wall behind it
        c.position = {0.42f, 1.78f, -2.55f};
        c.target = {-0.05f, 0.92f, 0.65f};
        c.fovy = 58.f;
    } else if (view == "corner") { // high in the front-left corner, over the card players
        c.position = {-3.80f, 2.50f, 3.25f};
        c.target = {0.35f, 0.82f, -0.70f};
        c.fovy = 60.f;
    }
    return c;
}

// The HUD lives on a 16:9 canvas letterboxed into the window. In a window narrower than 16:9 the vertical field
// of view grows so that the horizontal one stays as designed: the 16:9 composition (the whole istaka, both side
// players) stays in view and lines up with the HUD, and a taller window just shows more floor and ceiling. Wider
// windows keep the vertical field of view and show more of the room at the sides.
Camera3D fitToCanvas(Camera3D c, float aspect) {
    constexpr float kDesign = 16.f / 9.f;
    if (aspect > 0.f && aspect < kDesign) {
        const float t = std::tan(c.fovy * 0.5f * DEG2RAD) * kDesign / aspect;
        c.fovy = std::min(2.f * std::atan(t) * RAD2DEG, 120.f);
    }
    return c;
}

// The 2D layer's camera for the window. The virtual canvas and the mouse live in window points, but on a
// HiDPI (Retina) framebuffer raylib 6 draws BeginMode2D content in framebuffer pixels (BeginMode2D drops the
// points->pixels screen scale that BeginDrawing sets up), so the canvas transform is scaled up to pixels here.
Camera2D windowCamera2D(const ui::Viewport& vp) {
    Camera2D c = ui::viewportCamera(vp);
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const float kx = sw > 0 ? (float)GetRenderWidth() / (float)sw : 1.f;
    const float ky = sh > 0 ? (float)GetRenderHeight() / (float)sh : 1.f;
    c.offset = {c.offset.x * kx, c.offset.y * ky};
    c.zoom *= kx;
    return c;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- settings file (interactive runs only)
std::string settingsPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/Library/Application Support/Kiraathane101/ayarlar.txt";
}

void loadSettings(ui::Settings& s) {
    const std::string path = settingsPath();
    if (path.empty() || !FileExists(path.c_str())) return;
    char* text = LoadFileText(path.c_str());
    if (!text) return;
    std::istringstream in(text);
    UnloadFileText(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const int iv = std::atoi(v.c_str());
        if (k == "el") s.numHands = std::clamp(iv, 1, 11);
        else if (k == "seviye") s.difficulty = std::clamp(iv, 0, 2);
        else if (k == "efekt") s.sfx = iv != 0;
        else if (k == "ortam") s.ambient = iv != 0;
        else if (k == "muzik") s.music = iv != 0;
        else if (k == "ipucu") s.hints = iv != 0;
        else if (k == "hiz") s.animSpeed = std::clamp((float)std::atof(v.c_str()), 0.5f, 2.f);
        else if (k == "isim" && !v.empty() && v.size() <= 64) s.playerName = v;
    }
}

void saveSettings(const ui::Settings& s) {
    const std::string path = settingsPath();
    if (path.empty()) return;
    const std::string dir = path.substr(0, path.find_last_of('/'));
    if (!DirectoryExists(dir.c_str()) && MakeDirectory(dir.c_str()) != 0) return;
    char buf[512];
    std::snprintf(buf, sizeof buf, "# Kıraathane 101 ayarları\nel=%d\nseviye=%d\nefekt=%d\nortam=%d\nmuzik=%d\nipucu=%d\nhiz=%.2f\nisim=%s\n",
                  s.numHands, s.difficulty, s.sfx ? 1 : 0, s.ambient ? 1 : 0, s.music ? 1 : 0, s.hints ? 1 : 0,
                  (double)s.animSpeed, s.playerName.c_str());
    SaveFileText(path.c_str(), buf);
}

// ---------------------------------------------------------------- frame statistics
struct FrameStats {
    std::vector<float> frameMs, cpuMs;
    std::vector<int> particles;
    void add(float frame, float cpu, int parts) {
        frameMs.push_back(frame);
        cpuMs.push_back(cpu);
        particles.push_back(parts);
    }
    static float pct(std::vector<float> v, float p) {
        if (v.empty()) return 0.f;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, (size_t)(p * (float)(v.size() - 1) + 0.5f))];
    }
    static float avg(const std::vector<float>& v) {
        double s = 0;
        for (float x : v) s += x;
        return v.empty() ? 0.f : (float)(s / (double)v.size());
    }
    void print() const {
        if (frameMs.empty()) return;
        int pmax = 0;
        double psum = 0;
        for (int p : particles) {
            pmax = std::max(pmax, p);
            psum += p;
        }
        int slow = 0;
        for (float f : frameMs)
            if (f > 25.f) ++slow;
        std::printf("[perf] %zu frames | frame avg %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f, >25 ms: %d | "
                    "cpu avg %.2f ms, p99 %.2f, max %.2f | particles avg %.0f, max %d\n",
                    frameMs.size(), (double)avg(frameMs), (double)pct(frameMs, 0.5f), (double)pct(frameMs, 0.95f),
                    (double)pct(frameMs, 0.99f), (double)pct(frameMs, 1.f), slow, (double)avg(cpuMs),
                    (double)pct(cpuMs, 0.99f), (double)pct(cpuMs, 1.f), psum / (double)particles.size(), pmax);
    }
};

// ================================================================ the application
class App {
public:
    explicit App(const Options& o) : opt_(o) {}
    int run();

private:
    enum class Flow { Title, Playing, HandOver, Summary, MatchOver };
    enum class SnapState { Title, Game, Summary, MatchOver, Rules, Settings };

    struct Think {                 // the current bot turn's pacing and (possibly asynchronous) decision
        int seat = -1, turn = -1, actions = 0;
        float wait = 0.f;
        bool pending = false, haveReady = false;
        okey::BotAction ready;
        std::future<okey::BotAction> fut;
    };
    struct DelayedEvent {
        okey::GameEvent e;
        float t = 0.f;
    };

    // setup
    bool init();
    void initWindow();
    void shutdown();
    // frame
    void tick(float dt);
    void updateWorld(float dt, float simDt, const Camera3D& cam, bool blocked);
    void submitWorld();
    void drawOverlays(bool hud);
    // frame statistics are kept (and printed at exit) only for measured runs; a normal session would grow them forever
    bool reportStats() const { return opt_.perf || opt_.autoplay || opt_.maxFrames > 0; }
    void recordStats(float cpuMs);
    bool snapshotDone();
    bool exportSnapshot();
    void saveWindowShot();
    // flow
    void handleScreenAction(ui::ScreenAction a);
    void startMatch();
    void startNextHand();
    void toTitle();
    void setTitleMode(bool on);
    void applySettings();
    void releaseOverrides();
    void persistSettings();
    void updateFlow(float simDt);
    void updateAutoScreens(float simDt);
    void pumpEvents();
    void routeAudio(const okey::GameEvent& e);
    void updateDelayedAudio(float simDt);
    void updateScoreboard();
    std::array<std::string, 4> names();
    float animSpeed() { return std::max(0.25f, screens_.settings().animSpeed); }
    // bots
    bool isBotSeat(int s) const { return s != HUMAN || opt_.autoplay; }
    void updateBots(float simDt);
    void launchThink(int seat);
    bool thinkReady();
    okey::BotAction takeThink();
    void cancelThink();
    void applyBot(int seat, const okey::BotAction& a);
    void forceLegalMove(int seat);

    Options opt_;
    bool snapshot_ = false;
    SnapState snapState_ = SnapState::Game;
    bool audioOn_ = false, audioDevice_ = false;
    bool quit_ = false;
    int exitCode_ = 0;

    r3d::Renderer renderer_;
    r3d::Room room_;
    r3d::Characters characters_;
    r3d::Table3D table_;
    r3d::PlayerCamera pcam_;
    ui::Audio audio_;
    ui::Screens screens_;
    okey::Game game_;
    std::array<std::unique_ptr<okey::Bot>, 4> bots_;
    RenderTexture2D rt_{};

    Flow flow_ = Flow::Title;
    uint64_t baseSeed_ = 0;
    int matchCount_ = 0, matchesDone_ = 0;
    okey::Rng paceRng_;
    Think think_;
    std::vector<DelayedEvent> delayed_;
    float handOverT_ = 0.f, screenT_ = 0.f;
    bool lookDrag_ = false;
    bool settingsDirty_ = false;
    // --hands, --level and --no-audio shape this session only. The settings screen shows the values in force, but
    // the file keeps the loaded ones for every setting the player has not moved away from its command-line value.
    struct Overridden {
        bool hands = false, level = false, sfx = false, ambient = false, music = false;
    } overridden_;
    ui::Settings loadedSettings_;  // as read from disk (or the defaults), before the command line
    ui::Settings cliSettings_;     // the session's starting values: loadedSettings_ plus the command line
    Camera3D cam_{};        // the human's camera this frame
    Camera3D renderCam_{};  // what is rendered (== cam_ except for snapshot views)

    // bookkeeping / verification
    long frame_ = 0;
    long snapReachedAt_ = -1;
    int handsPlayed_ = 0, rejected_ = 0, fallbackRejected_ = 0, forced_ = 0;
    bool matchOverReached_ = false;
    double lastFrameT_ = 0.0;
    FrameStats stats_;
};

// ---------------------------------------------------------------- setup
void App::initWindow() {
    SetTraceLogLevel(LOG_WARNING);
    if (snapshot_) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
        InitWindow(320, 180, "Kıraathane 101");
        return;
    }
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI;
    if (!opt_.perf) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    InitWindow(1440, 810, "Kıraathane 101");
    if (!IsWindowReady()) return;
    // ~85% of the monitor at 16:9, centred
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (mw > 0 && mh > 0) {
        float w = (float)mw * 0.85f, h = w * 9.f / 16.f;
        if (h > (float)mh * 0.85f) {
            h = (float)mh * 0.85f;
            w = h * 16.f / 9.f;
        }
        const int iw = std::max(960, (int)w), ih = std::max(540, (int)h);
        SetWindowSize(iw, ih);
        const Vector2 mp = GetMonitorPosition(mon);
        SetWindowPosition((int)mp.x + (mw - iw) / 2, (int)mp.y + std::max(0, (mh - ih) / 2));
    }
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL);
    SetTargetFPS(opt_.perf ? 0 : 60);
}

bool App::init() {
    snapshot_ = !opt_.snapshot.empty();
    if (snapshot_) {
        const std::string& s = opt_.state;
        snapState_ = s == "title" ? SnapState::Title
                   : s == "summary" ? SnapState::Summary
                   : s == "matchover" ? SnapState::MatchOver
                   : s == "rules" ? SnapState::Rules
                   : s == "settings" ? SnapState::Settings
                                     : SnapState::Game;
        if (snapState_ == SnapState::Summary || snapState_ == SnapState::MatchOver) opt_.autoplay = true;
        if (snapState_ == SnapState::MatchOver && opt_.hands <= 0) opt_.hands = 1;
        if (opt_.frames < 0) {
            switch (snapState_) {
            case SnapState::Title: opt_.frames = 420; break;
            case SnapState::Game: opt_.frames = 300; break;
            case SnapState::Summary: opt_.frames = 80; break;
            case SnapState::MatchOver: opt_.frames = 150; break;
            case SnapState::Rules:
            case SnapState::Settings: opt_.frames = 90; break;
            }
        }
    }
    baseSeed_ = opt_.hasSeed ? opt_.seed : clockSeed();
    paceRng_.reseed(mix64(baseSeed_ ^ 0x7ACE5EEDull));

    initWindow();
    if (!IsWindowReady()) {
        std::fprintf(stderr, "Pencere açılamadı (InitWindow başarısız).\n");
        return false;
    }
    SetTraceLogLevel(LOG_ERROR);  // system fonts lack a glyph or two (₺): expected, not worth a warning
    ui::loadFonts();
    SetTraceLogLevel(LOG_WARNING);
    if (!renderer_.init()) {
        std::fprintf(stderr, "3D çizici başlatılamadı.\n");
        return false;
    }
    if (snapshot_) {
        rt_ = LoadRenderTexture(SNAP_W, SNAP_H);
        if (rt_.id == 0) return false;
    }

    // audio (never in snapshots)
    if (!snapshot_ && !opt_.noAudio) {
        InitAudioDevice();
        audioDevice_ = IsAudioDeviceReady();
        audioOn_ = audioDevice_ && audio_.init();
        if (!audioOn_) TraceLog(LOG_WARNING, "Ses aygıtı açılamadı; sessiz devam ediliyor.");
    }
    auto sfx = [this](ui::Sfx s) {
        if (audioOn_) audio_.play(s);
    };

    // settings: the saved ones (interactive runs), then the command line
    ui::Settings& st = screens_.settings();
    const bool interactive = !snapshot_ && !opt_.autoplay;
    if (interactive) loadSettings(st);
    loadedSettings_ = st;
    if (opt_.hands > 0) {
        st.numHands = std::clamp(opt_.hands, 1, 11);
        overridden_.hands = true;
    }
    if (opt_.level >= 0) {
        st.difficulty = std::clamp(opt_.level, 0, 2);
        overridden_.level = true;
    }
    if (opt_.noAudio) {
        st.sfx = st.ambient = st.music = false;
        overridden_.sfx = overridden_.ambient = overridden_.music = true;
    }
    cliSettings_ = st;

    const double t0 = GetTime();
    if (!room_.init(renderer_, mix64(baseSeed_ ^ 0x5200Aull))) {
        std::fprintf(stderr, "Kahvehane sahnesi kurulamadı.\n");
        return false;
    }
    if (!characters_.init(renderer_, mix64(baseSeed_ ^ 0xC4A2ull))) {
        std::fprintf(stderr, "Karakterler kurulamadı.\n");
        return false;
    }
    if (!table_.init(renderer_, &game_, HUMAN)) {
        std::fprintf(stderr, "Okey masası kurulamadı.\n");
        return false;
    }
    room_.playSfx = sfx;
    characters_.playSfx = sfx;
    table_.playSfx = sfx;
    screens_.init();
    screens_.playSfx = sfx;
    characters_.setNames(names());
    if (opt_.speed != 1.f || opt_.autoplay || snapshot_)
        std::printf("[init] world ready in %.0f ms (seed %llu)\n", (GetTime() - t0) * 1000.0, (unsigned long long)baseSeed_);

    pcam_.reset();
    setTitleMode(true);
    applySettings();
    // --screenshot (the real window) honours --state title|game|rules|settings too, for checking the HiDPI path
    const bool windowShot = !snapshot_ && !opt_.screenshot.empty();
    if (opt_.autoplay || opt_.start || (windowShot && opt_.state == "game") ||
        (snapshot_ && snapState_ != SnapState::Title && snapState_ != SnapState::Rules && snapState_ != SnapState::Settings)) {
        screens_.show(ui::ScreenId::None);
        startMatch();
    } else if ((snapshot_ && snapState_ == SnapState::Rules) || (windowShot && opt_.state == "rules")) {
        screens_.show(ui::ScreenId::Rules);
    } else if ((snapshot_ && snapState_ == SnapState::Settings) || (windowShot && opt_.state == "settings")) {
        screens_.show(ui::ScreenId::Settings);
    }
    lastFrameT_ = GetTime();
    return true;
}

void App::shutdown() {
    cancelThink();
    if (settingsDirty_ && !snapshot_ && !opt_.autoplay) persistSettings();
    table_.shutdown(renderer_);
    characters_.shutdown(renderer_);
    room_.shutdown(renderer_);
    if (rt_.id) UnloadRenderTexture(rt_);
    renderer_.shutdown();
    screens_.shutdown();
    if (audioOn_) audio_.shutdown();
    if (audioDevice_) CloseAudioDevice();
    ui::unloadFonts();
    if (IsWindowReady()) CloseWindow();
}

std::array<std::string, 4> App::names() {
    return {screens_.settings().playerName, BOT_NAMES[1], BOT_NAMES[2], BOT_NAMES[3]};
}

// ---------------------------------------------------------------- the loop
int App::run() {
    if (!init()) {
        shutdown();
        return 1;
    }
    while (!quit_) {
        if (!snapshot_ && WindowShouldClose()) break;
        const double tStart = GetTime();
        const float realDt = (float)(tStart - lastFrameT_);
        lastFrameT_ = tStart;
        // fixed steps for snapshots (deterministic); real time otherwise, with hitches clamped
        const float dt = snapshot_ ? 1.f / 60.f : std::clamp(realDt, 0.f, 0.1f);
        const bool minimized = !snapshot_ && IsWindowMinimized();

        ui::uiBeginFrame();
        const ui::Viewport vp = snapshot_ ? ui::Viewport{} : ui::computeViewport();
        ui::setCurrentViewport(vp);
        renderer_.setRenderSize(snapshot_ ? SNAP_W : GetScreenWidth(), snapshot_ ? SNAP_H : GetScreenHeight());
        if (!minimized) tick(dt);

        BeginDrawing();
        const bool seatView = !snapshot_ || opt_.view == "seat";
        if (snapshot_) {
            submitWorld();
            renderer_.render(renderCam_, &rt_, CLEAR_COLOR);
            BeginTextureMode(rt_);
            BeginMode2D(ui::viewportCamera(vp));
            drawOverlays(seatView);
            EndMode2D();
            EndTextureMode();
        } else {
            ClearBackground(CLEAR_COLOR);
            if (!minimized) {
                submitWorld();
                renderer_.render(renderCam_);
                BeginMode2D(windowCamera2D(vp));
                drawOverlays(true);
                EndMode2D();
            }
        }
        ui::uiEndFrame();
        const float cpuMs = (float)((GetTime() - tStart) * 1000.0);
        const bool lastFrame = opt_.maxFrames > 0 && frame_ + 1 >= opt_.maxFrames;
        if (!snapshot_ && lastFrame && !opt_.screenshot.empty()) saveWindowShot();
        EndDrawing();
        if (reportStats() && frame_ >= 120 && !minimized) recordStats(cpuMs);
        ++frame_;

        if (snapshot_ && snapshotDone()) {
            if (!exportSnapshot()) exitCode_ = 1;
            quit_ = true;
        }
        if (snapshot_ && frame_ > 400000) {
            std::fprintf(stderr, "[snapshot] the requested state was never reached\n");
            exitCode_ = 1;
            quit_ = true;
        }
        if (opt_.maxFrames > 0 && frame_ >= opt_.maxFrames) quit_ = true;
    }
    if (opt_.autoplay && !snapshot_) {
        if (matchOverReached_ && matchesDone_ >= opt_.matches)
            std::printf("[autoplay] MatchOver reached after %ld frames, %d match%s, %d hands | bot rejections %d, fallback "
                        "rejections %d, forced moves %d\n",
                        frame_, matchesDone_, matchesDone_ == 1 ? "" : "es", handsPlayed_, rejected_, fallbackRejected_, forced_);
        else
            std::printf("[autoplay] stopped after %ld frames (%d of %d matches, %d hands played) | bot rejections %d, "
                        "fallback rejections %d, forced moves %d\n",
                        frame_, matchesDone_, opt_.matches, handsPlayed_, rejected_, fallbackRejected_, forced_);
    }
    if (reportStats()) stats_.print();
    shutdown();
    return exitCode_;
}

void App::recordStats(float cpuMs) {
    const float frameMs = GetFrameTime() * 1000.f;
    stats_.add(frameMs, cpuMs, renderer_.particleCount());
}

void App::tick(float dt) {
    const float simDt = dt * std::max(0.05f, opt_.speed);
    const bool blockedAtStart = screens_.blocksGame();
    const Vector2 mouse = snapshot_ ? NO_MOUSE : ui::virtualMouse();

    // menus and overlays first: they own the keyboard (ESC, Enter) and the mouse while they are up
    handleScreenAction(screens_.update(dt, mouse, flow_ == Flow::Title ? nullptr : &game_));
    if (quit_) return;
    bool blocked = screens_.blocksGame();
    const bool inGame = flow_ != Flow::Title;

    // the human's eyes
    if (!inGame) {
        pcam_.update(dt, false);  // cinematic drift
        lookDrag_ = false;
    } else if (!blocked) {
        const bool lookOk = !snapshot_ && !table_.mouseBusy();
        // a right-drag that started as a look keeps looking even when it crosses a tile or a button
        lookDrag_ = lookDrag_ ? IsMouseButtonDown(MOUSE_BUTTON_RIGHT) : (lookOk && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));
        pcam_.update(dt, lookOk || lookDrag_);
    }
    cam_ = fitToCanvas(pcam_.camera(), (float)renderer_.renderWidth() / (float)std::max(1, renderer_.renderHeight()));
    renderCam_ = (snapshot_ && opt_.view != "seat") ? viewCamera(opt_.view, cam_) : cam_;

    // the table: the human acts through it (never while a screen is up, in autoplay or in snapshots)
    const bool human = inGame && !blocked && !blockedAtStart && !opt_.autoplay && !snapshot_;
    table_.update(simDt, cam_, human ? mouse : NO_MOUSE, human);
    if (table_.consumeMenuRequest() && inGame && !blocked) {
        screens_.show(ui::ScreenId::Paused);
        blocked = true;
    }
    pumpEvents();

    // the match: bots, the pause after a hand; frozen while any screen is up
    if (inGame && !blocked) updateFlow(simDt);
    updateAutoScreens(simDt);
    pumpEvents();

    updateWorld(dt, simDt, cam_, screens_.blocksGame());
}

void App::updateWorld(float dt, float simDt, const Camera3D& cam, bool blocked) {
    const bool playing = flow_ == Flow::Playing && game_.handState() == okey::HandState::Playing;
    std::array<Vector3, 4> heads;
    for (int s = 0; s < 4; ++s) heads[s] = characters_.headPosition(s);
    table_.setHeadAnchors(heads);
    characters_.setActiveSeat(playing && !blocked ? game_.current() : -1);
    if (room_.consumeTvGoal()) {
        characters_.onTvGoal();
        // the room turns to the TV and so do we, unless we are busy with our own tiles
        if (playing && !blocked && game_.current() != HUMAN && !table_.mouseBusy() && !lookDrag_) pcam_.glanceAt(TV_POS, 1.8f);
    }
    // the room lives at game speed; modules clamp large steps, so fast-forward is split into small ones
    const int steps = std::clamp((int)std::ceil(simDt / (1.f / 50.f)), 1, 10);
    const float sub = simDt / (float)steps;
    for (int i = 0; i < steps; ++i) {
        room_.update(sub);
        characters_.update(sub, cam);
        renderer_.update(sub);
    }
    updateDelayedAudio(simDt);
    if (audioOn_) audio_.update(dt);
    updateScoreboard();
    if (settingsDirty_ && screens_.current() != ui::ScreenId::Settings && !snapshot_ && !opt_.autoplay) {
        persistSettings();
        settingsDirty_ = false;
    }
}

void App::submitWorld() {
    room_.submit(renderer_);
    characters_.submit(renderer_);
    table_.submit(renderer_);
}

void App::drawOverlays(bool hud) {
    characters_.drawOverlay(renderer_);
    // the table's HUD (buttons, plates, status) steps aside while a menu or the score sheet covers the table
    if (hud && flow_ != Flow::Title && !screens_.blocksGame()) table_.drawHUD(renderer_);
    screens_.draw(flow_ == Flow::Title ? nullptr : &game_);
}

// ---------------------------------------------------------------- snapshots
bool App::snapshotDone() {
    switch (snapState_) {
    case SnapState::Summary:
    case SnapState::MatchOver: {
        const ui::ScreenId want = snapState_ == SnapState::Summary ? ui::ScreenId::HandSummary : ui::ScreenId::MatchOver;
        if (snapReachedAt_ < 0 && screens_.current() == want) snapReachedAt_ = frame_;
        return snapReachedAt_ >= 0 && frame_ - snapReachedAt_ >= opt_.frames;
    }
    default: return frame_ >= opt_.frames;
    }
}

bool App::exportSnapshot() {
    Image img = LoadImageFromTexture(rt_.texture);
    ImageFlipVertical(&img);
    // translucent 2D overlays blended into the render target leave alpha < 1 behind: make it opaque
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const bool ok = ExportImage(img, opt_.snapshot.c_str());
    UnloadImage(img);
    std::printf("[snapshot] %s %s (state %s, view %s, frame %ld, particles %d)\n", ok ? "wrote" : "FAILED to write",
                opt_.snapshot.c_str(), opt_.state.empty() ? "game" : opt_.state.c_str(), opt_.view.c_str(), frame_, renderer_.particleCount());
    return ok;
}

// The real window's back buffer (MSAA resolved, HiDPI size), read before the swap.
void App::saveWindowShot() {
    Image img = LoadImageFromScreen();
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const bool ok = ExportImage(img, opt_.screenshot.c_str());
    std::printf("[screenshot] %s %s (%dx%d pixels, window %dx%d points)\n", ok ? "wrote" : "FAILED to write",
                opt_.screenshot.c_str(), img.width, img.height, GetScreenWidth(), GetScreenHeight());
    UnloadImage(img);
}

// ---------------------------------------------------------------- flow
void App::setTitleMode(bool on) {
    room_.setTitleMode(on);
    characters_.setTitleMode(on);
    pcam_.setTitleMode(on);
    if (!on) pcam_.reset();
}

void App::handleScreenAction(ui::ScreenAction a) {
    using A = ui::ScreenAction;
    switch (a) {
    case A::None:
    case A::Resume: break;
    case A::StartMatch: startMatch(); break;
    case A::NextHand: startNextHand(); break;
    case A::ShowMatchResult: flow_ = Flow::MatchOver; break;
    case A::ToTitle: toTitle(); break;
    case A::Quit: quit_ = true; break;
    case A::SettingsChanged:
        releaseOverrides();
        applySettings();
        settingsDirty_ = true;
        break;
    }
}

void App::startMatch() {
    cancelThink();
    const ui::Settings& st = screens_.settings();
    okey::RulesConfig cfg;
    cfg.numHands = std::clamp(st.numHands, 1, 11);
    game_.setRules(cfg);
    const std::array<std::string, 4> nm = names();
    for (int s = 0; s < 4; ++s) game_.setPlayer(s, nm[s], s == HUMAN);
    const uint64_t matchSeed = opt_.hasSeed ? opt_.seed + (uint64_t)matchCount_ : mix64(baseSeed_ + (uint64_t)matchCount_);
    ++matchCount_;
    for (int s = 0; s < 4; ++s) {
        bots_[s].reset();
        if (!isBotSeat(s)) continue;
        const okey::BotLevel lvl = s == HUMAN ? okey::BotLevel::Normal : (okey::BotLevel)std::clamp(st.difficulty, 0, 2);
        bots_[s] = std::make_unique<okey::Bot>(lvl, mix64(matchSeed * 4u + (uint64_t)s + 0xB07ull));
    }
    characters_.setNames(nm);
    setTitleMode(false);
    table_.setAnimationSpeed(st.animSpeed);
    table_.setHints(st.hints);
    delayed_.clear();
    think_ = Think{};
    handOverT_ = screenT_ = 0.f;
    flow_ = Flow::Playing;
    game_.startMatch(matchSeed);
    pumpEvents();
    if (opt_.autoplay)
        std::printf("[autoplay] match %d: seed %llu, %d hands (frame %ld)\n", matchCount_, (unsigned long long)matchSeed,
                    cfg.numHands, frame_);
}

void App::startNextHand() {
    cancelThink();
    if (game_.handState() != okey::HandState::HandOver) return;
    think_ = Think{};
    handOverT_ = 0.f;
    flow_ = Flow::Playing;
    game_.startNextHand();
    pumpEvents();
}

void App::toTitle() {
    cancelThink();
    think_ = Think{};
    delayed_.clear();
    const okey::RulesConfig cfg = game_.rules();
    game_ = okey::Game(cfg);  // not started: the tiles rest in a heap on the table
    for (auto& b : bots_) b.reset();
    flow_ = Flow::Title;
    setTitleMode(true);
    lookDrag_ = false;
}

void App::applySettings() {
    const ui::Settings& st = screens_.settings();
    if (audioOn_) {
        audio_.setSfxEnabled(st.sfx);
        audio_.setAmbientEnabled(st.ambient);
        audio_.setMusicEnabled(st.music);
    }
    table_.setAnimationSpeed(st.animSpeed);
    table_.setHints(st.hints);
    if (flow_ == Flow::Title) {
        characters_.setNames(names());
        return;
    }
    cancelThink();  // nothing may touch a bot while it is thinking on the worker thread
    for (int s = 1; s < 4; ++s)
        if (bots_[s]) bots_[s]->setLevel((okey::BotLevel)std::clamp(st.difficulty, 0, 2));
    if (game_.player(HUMAN).name != st.playerName) {
        game_.setPlayer(HUMAN, st.playerName, true);
        characters_.setNames(names());
    }
}

// A setting the player moves away from its command-line value is theirs from then on, and is saved as it stands
// (even if they later move it back).
void App::releaseOverrides() {
    const ui::Settings& st = screens_.settings();
    Overridden& o = overridden_;
    o.hands = o.hands && st.numHands == cliSettings_.numHands;
    o.level = o.level && st.difficulty == cliSettings_.difficulty;
    o.sfx = o.sfx && st.sfx == cliSettings_.sfx;
    o.ambient = o.ambient && st.ambient == cliSettings_.ambient;
    o.music = o.music && st.music == cliSettings_.music;
}

// Saves the settings file, keeping the loaded value of every setting that is still only a command-line override:
// a `--no-audio` or `--hands 3` run must not become the player's saved preference.
void App::persistSettings() {
    releaseOverrides();
    ui::Settings s = screens_.settings();
    if (overridden_.hands) s.numHands = loadedSettings_.numHands;
    if (overridden_.level) s.difficulty = loadedSettings_.difficulty;
    if (overridden_.sfx) s.sfx = loadedSettings_.sfx;
    if (overridden_.ambient) s.ambient = loadedSettings_.ambient;
    if (overridden_.music) s.music = loadedSettings_.music;
    saveSettings(s);
}

void App::updateFlow(float simDt) {
    switch (game_.handState()) {
    case okey::HandState::Playing: updateBots(simDt); break;
    case okey::HandState::HandOver:
    case okey::HandState::MatchOver:
        if (flow_ == Flow::HandOver) {
            // let everyone see the revealed istakas before the score sheet covers the table
            handOverT_ += simDt;
            if ((handOverT_ >= SUMMARY_DELAY && !table_.isAnimating()) || handOverT_ >= SUMMARY_DELAY + 5.f) {
                screens_.show(ui::ScreenId::HandSummary);
                flow_ = Flow::Summary;
                screenT_ = 0.f;
            }
        }
        break;
    case okey::HandState::NotStarted: break;
    }
}

// Autoplay presses the score sheet's buttons by itself and leaves after the final standings.
void App::updateAutoScreens(float simDt) {
    if (!opt_.autoplay) return;
    const ui::ScreenId cur = screens_.current();
    if (cur == ui::ScreenId::HandSummary) {
        if (snapshot_ && snapState_ == SnapState::Summary) return;
        screenT_ += simDt;
        if (screenT_ < AUTO_SUMMARY) return;
        screenT_ = 0.f;
        if (game_.handState() == okey::HandState::MatchOver) {
            screens_.show(ui::ScreenId::MatchOver);
            flow_ = Flow::MatchOver;
        } else {
            screens_.show(ui::ScreenId::None);
            startNextHand();
        }
    } else if (cur == ui::ScreenId::MatchOver) {
        matchOverReached_ = true;
        if (snapshot_ && opt_.matches <= 1) return;  // the final standings are the picture
        screenT_ += simDt;
        if (screenT_ < AUTO_MATCHOVER) return;
        screenT_ = 0.f;
        if (++matchesDone_ >= opt_.matches) {
            quit_ = true;
        } else if (matchesDone_ % 2 == 1) { // "Ana Menü", then "Oyna" a little later
            screens_.show(ui::ScreenId::Title);
            toTitle();
        } else {                            // "Yeni Oyun" straight from the final standings
            screens_.show(ui::ScreenId::None);
            startMatch();
        }
    } else if (cur == ui::ScreenId::Title && flow_ == Flow::Title && matchCount_ > 0) {
        screenT_ += simDt;
        if (screenT_ < AUTO_TITLE) return;
        screenT_ = 0.f;
        screens_.show(ui::ScreenId::None);
        startMatch();
    }
}

void App::pumpEvents() {
    const std::vector<okey::GameEvent> events = game_.drainEvents();
    for (const okey::GameEvent& e : events) {
        using okey::EvType;
        table_.onEvent(e);
        characters_.onEvent(e, game_);
        routeAudio(e);
        if (e.type == EvType::HandStart) {
            for (auto& b : bots_)
                if (b) b->resetForHand();
            table_.onHandStart();
        }
        for (auto& b : bots_)
            if (b) b->observe(e, game_);
        switch (e.type) {
        case EvType::TurnStart: characters_.setActiveSeat(e.player); break;
        case EvType::Open:
            // our eyes follow a big moment across the table (never while we handle our own tiles)
            if (e.player != HUMAN && e.player >= 0 && !table_.mouseBusy() && !lookDrag_) {
                const w3d::RectXZ z = w3d::MELD_ZONE[e.player];
                pcam_.glanceAt({(z.x0 + z.x1) * 0.5f, w3d::TABLE_Y, (z.z0 + z.z1) * 0.5f}, 1.1f);
            }
            break;
        case EvType::HandEnd: {
            cancelThink();
            flow_ = Flow::HandOver;
            handOverT_ = 0.f;
            ++handsPlayed_;
            if (opt_.autoplay && !snapshot_) {
                const okey::HandResult& r = game_.lastHandResult();
                std::printf("[autoplay] hand %d/%d: %s | scores %d %d %d %d | totals %d %d %d %d\n", game_.handIndex() + 1,
                            game_.numHands(), e.text.c_str(), r.score[0], r.score[1], r.score[2], r.score[3],
                            game_.player(0).totalScore, game_.player(1).totalScore, game_.player(2).totalScore,
                            game_.player(3).totalScore);
            }
            break;
        }
        case EvType::MatchEnd:
            if (opt_.autoplay && !snapshot_) std::printf("[autoplay] %s\n", e.text.c_str());
            break;
        default: break;
        }
    }
}

// Tile sounds that belong to a tile landing are held back so the clack comes when the tile touches the felt.
void App::routeAudio(const okey::GameEvent& e) {
    if (!audioOn_) return;
    using okey::EvType;
    switch (e.type) {
    case EvType::Discard:
    case EvType::AddToMeld:
    case EvType::SwapJoker:
    case EvType::Open:
    case EvType::LayMelds: delayed_.push_back({e, LANDING_LAG / animSpeed()}); break;
    default: audio_.onEvent(e); break;
    }
}

void App::updateDelayedAudio(float simDt) {
    for (size_t i = 0; i < delayed_.size();) {
        delayed_[i].t -= simDt;
        if (delayed_[i].t <= 0.f) {
            if (audioOn_) audio_.onEvent(delayed_[i].e);
            delayed_.erase(delayed_.begin() + (std::ptrdiff_t)i);
        } else {
            ++i;
        }
    }
}

void App::updateScoreboard() {
    if (flow_ == Flow::Title || game_.handState() == okey::HandState::NotStarted) {
        room_.setScoreboard("", {});
        return;
    }
    const std::string title = game_.handState() == okey::HandState::MatchOver
                                  ? std::string("Maç bitti")
                                  : "El " + std::to_string(std::min(game_.handIndex() + 1, game_.numHands())) + " / " +
                                        std::to_string(game_.numHands());
    std::vector<std::string> lines;
    for (int s = 0; s < 4; ++s) lines.push_back(game_.player(s).name + " ....... " + std::to_string(game_.player(s).totalScore));
    room_.setScoreboard(title, lines);
}

// ---------------------------------------------------------------- bots
void App::updateBots(float simDt) {
    const int s = game_.current();
    if (s < 0 || s > 3 || !isBotSeat(s) || !bots_[s]) return;
    if (s != think_.seat || game_.turnNumber() != think_.turn) { // a new bot turn: think a little first
        cancelThink();
        think_.seat = s;
        think_.turn = game_.turnNumber();
        think_.actions = 0;
        think_.wait = paceRng_.uniform(0.6f, 1.2f) / animSpeed();
    }
    if (!think_.pending) launchThink(s);
    if (table_.isAnimating()) return;  // tiles still in the air: nobody moves
    think_.wait -= simDt;
    if (think_.wait > 0.f || !thinkReady()) return;
    const okey::BotAction a = takeThink();
    if (++think_.actions > MAX_BOT_ACTIONS) {
        TraceLog(LOG_WARNING, "Bot %d: %d hamlede tur bitmedi, zorla bitiriliyor", s, think_.actions);
        forceLegalMove(s);
    } else {
        applyBot(s, a);
    }
    think_.wait = paceRng_.uniform(0.35f, 0.6f) / animSpeed();
}

void App::launchThink(int seat) {
    okey::Bot* bot = bots_[seat].get();
    think_.pending = true;
    if (snapshot_) { // frame-exact determinism: decide right here
        think_.ready = bot->next(game_, seat);
        think_.haveReady = true;
        return;
    }
    // A bot decision can take ~10 ms (Kurt's rollouts): think on a worker thread while the pacing delay runs.
    // Nothing mutates the game until the result is applied on this thread; cancelThink() joins first.
    const okey::Game* g = &game_;
    think_.fut = std::async(std::launch::async, [bot, g, seat] { return bot->next(*g, seat); });
}

bool App::thinkReady() {
    if (think_.haveReady) return true;
    return think_.fut.valid() && think_.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

okey::BotAction App::takeThink() {
    think_.pending = false;
    if (think_.haveReady) {
        think_.haveReady = false;
        return think_.ready;
    }
    return think_.fut.get();
}

void App::cancelThink() {
    if (think_.fut.valid()) think_.fut.wait();
    think_.fut = std::future<okey::BotAction>();
    think_.pending = false;
    think_.haveReady = false;
}

void App::applyBot(int seat, const okey::BotAction& a) {
    okey::ActionResult r = okey::applyBotAction(game_, seat, a);
    if (!r.ok) {
        ++rejected_;
        TraceLog(LOG_WARNING, "Bot %d hamlesi reddedildi (%s): %s", seat, actionName(a.kind), r.error.c_str());
        const okey::BotAction f = okey::fallbackAction(game_, seat);
        r = okey::applyBotAction(game_, seat, f);
        if (!r.ok) {
            ++fallbackRejected_;
            TraceLog(LOG_WARNING, "Bot %d yedek hamlesi de reddedildi (%s): %s", seat, actionName(f.kind), r.error.c_str());
            forceLegalMove(seat);
        }
    }
    pumpEvents();
}

// Last resort: any legal step that moves the turn forward, so a bot can never stall the game.
void App::forceLegalMove(int seat) {
    ++forced_;
    if (game_.handState() != okey::HandState::Playing || game_.current() != seat) return;
    if (game_.stage() == okey::TurnStage::NeedDraw) {
        if (game_.pileCount() > 0 && game_.drawFromPile(seat).ok) return pumpEvents();
        if (game_.takeFromLeft(seat).ok) return pumpEvents();
        return;
    }
    if (game_.pendingLeftTile() >= 0 && game_.returnLeftTile(seat).ok) {
        game_.drawFromPile(seat);
        return pumpEvents();
    }
    const std::vector<int> hand = game_.player(seat).hand;
    for (int t : hand)
        if (game_.discard(seat, t).ok) break;
    pumpEvents();
}

} // namespace

// ================================================================ command line
void printUsage(const char* argv0) {
    std::printf("Kıraathane 101 — 101 Okey, dumanaltı bir kahvehanede\n\n"
                "Kullanım: %s [seçenekler]\n"
                "  --seed N          aynı dağıtımlar ve aynı rakipler (tekrar oynatılabilir)\n"
                "  --start           giriş ekranını atla, doğrudan oyuna başla\n"
                "  --hands N         el sayısı (1-11)\n"
                "  --level L         rakip seviyesi: 0 Acemi, 1 Usta, 2 Kurt\n"
                "  --autoplay        senin yerine de bir Usta bot oynar (izleme modu)\n"
                "  --speed X         oyunu X kat hızlı oynat (ör. 4)\n"
                "  --matches N       --autoplay ile: arka arkaya N maç (her ikincisi giriş ekranından geçer)\n"
                "  --no-audio        sessiz\n"
                "  --max-frames N    N kareden sonra çık\n"
                "  --perf            dikey eşitleme kapalı; çıkışta kare süresi istatistikleri\n"
                "  --screenshot DOSYA  --max-frames ile: son karede pencerenin görüntüsünü PNG olarak kaydet\n"
                "                    (--state title | game | rules | settings ile başlangıç ekranı seçilebilir)\n"
                "  --snapshot DOSYA  gizli pencerede 1600x900 bir kare çizip PNG olarak kaydet ve çık\n"
                "      --frames N    görüntüden önce simüle edilecek kare sayısı\n"
                "      --state S     title | game | summary | matchover | rules | settings\n"
                "      --view V      seat | left | right | back | corner\n"
                "  --help            bu yardım\n",
                argv0);
}

bool parseArgs(int argc, char** argv, Options& o, std::string& error, bool& wantHelp) {
    wantHelp = false;
    auto need = [&](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            error = std::string(flag) + " bir değer bekliyor";
            return nullptr;
        }
        return argv[++i];
    };
    auto toInt = [&](const char* s, long& out, const char* flag) {
        char* end = nullptr;
        out = std::strtol(s, &end, 10);
        if (!end || *end || end == s) {
            error = std::string(flag) + ": sayı bekleniyordu (\"" + s + "\")";
            return false;
        }
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        long v = 0;
        const char* s = nullptr;
        if (a == "--help" || a == "-h") {
            wantHelp = true;
        } else if (a == "--seed") {
            if (!(s = need(i, "--seed"))) return false;
            char* end = nullptr;
            o.seed = std::strtoull(s, &end, 10);
            if (!end || *end || end == s) {
                error = std::string("--seed: sayı bekleniyordu (\"") + s + "\")";
                return false;
            }
            o.hasSeed = true;
        } else if (a == "--autoplay") {
            o.autoplay = true;
        } else if (a == "--speed") {
            if (!(s = need(i, "--speed"))) return false;
            o.speed = (float)std::atof(s);
            if (!(o.speed >= 0.1f && o.speed <= 64.f)) {
                error = "--speed 0.1 ile 64 arasında olmalı";
                return false;
            }
        } else if (a == "--hands") {
            if (!(s = need(i, "--hands")) || !toInt(s, v, "--hands")) return false;
            if (v < 1 || v > 11) {
                error = "--hands 1 ile 11 arasında olmalı";
                return false;
            }
            o.hands = (int)v;
        } else if (a == "--level") {
            if (!(s = need(i, "--level")) || !toInt(s, v, "--level")) return false;
            if (v < 0 || v > 2) {
                error = "--level 0 (Acemi), 1 (Usta) ya da 2 (Kurt) olmalı";
                return false;
            }
            o.level = (int)v;
        } else if (a == "--matches") {
            if (!(s = need(i, "--matches")) || !toInt(s, v, "--matches")) return false;
            if (v < 1 || v > 1000) {
                error = "--matches 1 ile 1000 arasında olmalı";
                return false;
            }
            o.matches = (int)v;
        } else if (a == "--start") {
            o.start = true;
        } else if (a == "--no-audio") {
            o.noAudio = true;
        } else if (a == "--max-frames") {
            if (!(s = need(i, "--max-frames")) || !toInt(s, v, "--max-frames")) return false;
            o.maxFrames = std::max(0L, v);
        } else if (a == "--perf") {
            o.perf = true;
        } else if (a == "--screenshot") {
            if (!(s = need(i, "--screenshot"))) return false;
            o.screenshot = s;
        } else if (a == "--snapshot") {
            if (!(s = need(i, "--snapshot"))) return false;
            o.snapshot = s;
        } else if (a == "--frames") {
            if (!(s = need(i, "--frames")) || !toInt(s, v, "--frames")) return false;
            o.frames = (int)std::clamp(v, 1L, 1000000L);
        } else if (a == "--state") {
            if (!(s = need(i, "--state"))) return false;
            o.state = s;
            static const char* const ok[] = {"title", "game", "summary", "matchover", "rules", "settings"};
            if (std::none_of(std::begin(ok), std::end(ok), [&](const char* k) { return o.state == k; })) {
                error = "--state: title, game, summary, matchover, rules ya da settings";
                return false;
            }
        } else if (a == "--view") {
            if (!(s = need(i, "--view"))) return false;
            o.view = s;
            static const char* const ok[] = {"seat", "left", "right", "back", "corner"};
            if (std::none_of(std::begin(ok), std::end(ok), [&](const char* k) { return o.view == k; })) {
                error = "--view: seat, left, right, back ya da corner";
                return false;
            }
        } else {
            error = "bilinmeyen seçenek: " + a;
            return false;
        }
    }
    if (o.autoplay) o.start = true;
    return true;
}

int run(const Options& options) {
    // The world is large (meshes, textures, a renderer) — keep it off the stack.
    std::unique_ptr<App> app = std::make_unique<App>(options);
    return app->run();
}

} // namespace app
