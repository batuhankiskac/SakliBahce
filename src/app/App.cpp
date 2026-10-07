// SaklıBahçe — the application: window, main loop, match flow, bot pacing, and the wiring between the engine
// (okey::Game / okey::Bot), the 3D world (Room, Characters, Table3D, PlayerCamera), audio and the menu screens.
// Frame structure: DESIGN3D.md §2. This file: setup, the frame loop, the world and the 2D layer, the command line;
// the rest of App is split by topic (AppInternal.h lists the files).
#include "app/AppInternal.h"

namespace app {
namespace detail {

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
    case K::Finish: return "Finish";
    case K::ShowIndicator: return "ShowIndicator";
    }
    return "?";
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

// ---------------------------------------------------------------- setup
void App::initWindow() {
    SetTraceLogLevel(LOG_WARNING);
    if (snapshot_) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
        InitWindow(320, 180, "SaklıBahçe");
        return;
    }
    if (opt_.aiChaos) { // a long unattended test: a small hidden window, as fast as it goes with --perf
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(480, 270, "SaklıBahçe");
        SetExitKey(KEY_NULL);
        SetTargetFPS(opt_.perf ? 0 : 60);
        return;
    }
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI;
    if (!opt_.perf) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    InitWindow(1440, 810, "SaklıBahçe");
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
                   : s == "games" ? SnapState::Games
                   : s == "stats" ? SnapState::Stats
                   : s == "basarimlar" ? SnapState::Achievements
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
            case SnapState::Games:
            case SnapState::Stats:
            case SnapState::Achievements:
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
    if (!unattended()) {
        loadSettings(st);
        record_.load(statsPath());
        memory_.load(memoryPath());
        memory_.beginSession();
        memory_.noteRank(ui::StatsBook::rankFor(record_.rankPoints()).points);
        memory_.save(memoryPath());
    }
    characters_.banter().setMemory(&memory_);
    screens_.setStats(&record_);
    initAchievements(); // Başarımlar
    refreshResumable();
    refreshReplays();
    loadedSettings_ = st;
    if (opt_.hands > 0) {
        st.numHands = std::clamp(opt_.hands, 1, 11);
        overridden_.hands = true;
    }
    if (opt_.level >= 0) {
        st.difficulty = std::clamp(opt_.level, 0, 2);
        overridden_.level = true;
    }
    if (opt_.game >= 0) {
        st.game = opt_.game;
        overridden_.game = true;
    }
    for (const std::string& kv : opt_.sets) { // --set key=value (this run only: never saved when unattended)
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) applySettingLine(st, trim(kv.substr(0, eq)), trim(kv.substr(eq + 1)));
    }
    if (opt_.noAudio) {
        st.sfx = st.ambient = st.music = false;
        overridden_.sfx = overridden_.ambient = overridden_.music = true;
    }
    cliSettings_ = st;

    const double t0 = GetTime();
    room_.setTimeOfDay(st.dayTime);  // the place and its light from the first frame (the garden is built if needed)
    room_.setSeason(st.season);
    room_.setVenue(st.venue);
    room_.setSpecialDay(w3d::resolveSpecialDay(st.ozelGun, w3d::resolveDayPhase(st.dayTime)));  // (ozelgun) a derby night starts inside
    specialRng_.reseed(mix64(baseSeed_ ^ 0x0DE1ull));
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
    characters_.playSfxVol = [this](ui::Sfx s, float v, float p) {  // (Ocakçı) his quieter sounds at the counter
        if (audioOn_) audio_.play(s, v, p);
    };
    characters_.speak = [this](int who, const std::string& t) {
        if (audioOn_) audio_.speak(who == 4 ? 5 : who, t);
        if (opt_.achievementTest) achievementTestSpeech(who, t); // (Başarımlar check)
    };
    // Yüz: the talking mouths follow the murmur (-1: no voice, they time themselves from the text)
    characters_.voiceMouth = [this](int who) { return audioOn_ ? audio_.mouthOpen(who == 4 ? 5 : who) : -1.f; };
    table_.playSfx = sfx;
    // the player's own hands (they borrow Characters' hand meshes and the player's tea glass); the tables cue them
    hands_.init(renderer_, &characters_, mix64(baseSeed_ ^ 0xE11E5ull));
    hands_.playSfx = sfx;
    table_.handCue = [this](const r3d::HandCue& c) { hands_.cue(c); };
    table_.handLead = [this](r3d::HandCueKind k) { return hands_.lead(k); };
    screens_.init();
    screens_.playSfx = sfx;
    characters_.setNames(names());
    if (opt_.speed != 1.f || opt_.autoplay || opt_.aiChaos || snapshot_)
        std::printf("[init] world ready in %.0f ms (seed %llu)\n", (GetTime() - t0) * 1000.0, (unsigned long long)baseSeed_);

    pcam_.reset();
    setTitleMode(true);
    applySettings();
    // --screenshot (the real window) honours --state title|game|rules|settings too, for checking the HiDPI path
    const bool windowShot = !snapshot_ && !opt_.screenshot.empty();
    if (opt_.aiChaos) chaos_.rng.reseed(mix64(baseSeed_ ^ 0xC4A05ull));
    table_.setAiMode(aiSeat());
    screens_.setAiMode(aiSeat());
    if (opt_.watch && !unattended() && !replayFiles_.empty()) {
        startReplay(0);
        lastFrameT_ = GetTime();
        return true;
    }
    if (opt_.analyze && !unattended() && !replayFiles_.empty()) {
        screens_.show(ui::ScreenId::Analysis);
        analyzeReplay(0);
    }
    if (opt_.resume && !unattended() && screens_.current() == ui::ScreenId::Title) {
        SavedMatch probe;
        if (readSave(probe)) {
            if (opt_.ai) setAiMode(true);
            resumeSaved();
            lastFrameT_ = GetTime();
            return true;
        }
    }
    if (opt_.autoplay || opt_.start || (windowShot && opt_.state == "game") ||
        (snapshot_ && snapState_ != SnapState::Title && snapState_ != SnapState::Rules && snapState_ != SnapState::Settings &&
         snapState_ != SnapState::Games && snapState_ != SnapState::Stats && snapState_ != SnapState::Achievements)) {
        screens_.show(ui::ScreenId::None);
        if (opt_.ai || opt_.aiChaos) setAiMode(true);
        startMatch();
    } else if ((snapshot_ && snapState_ == SnapState::Rules) || (windowShot && opt_.state == "rules")) {
        screens_.show(ui::ScreenId::Rules);
    } else if ((snapshot_ && snapState_ == SnapState::Settings) || (windowShot && opt_.state == "settings")) {
        screens_.show(ui::ScreenId::Settings);
    } else if ((snapshot_ && snapState_ == SnapState::Games) || (windowShot && opt_.state == "games")) {
        screens_.show(ui::ScreenId::GameSelect);
    } else if (snapshot_ && snapState_ == SnapState::Stats) {
        // a made-up record, to see the page filled in
        const int lv[] = {1, 2, 2, 0, 1, 2, 1};
        for (int g = 0; g < 7; ++g) {
            if (g == 4) continue;
            for (int m = 0; m < 3 + g; ++m) {
                for (int h = 0; h < 4; ++h) record_.hand(g, (m + h + g) % 3 == 0, true, (g < 2 ? -40 : 10) * (h + m % 3), g < 2);
                record_.match(g, lv[g], (m * 7 + g) % 3 != 0);
            }
        }
        screens_.show(ui::ScreenId::Stats);
    } else if (snapshot_ && snapState_ == SnapState::Achievements) {
        screens_.show(ui::ScreenId::Achievements); // (a made-up book: initAchievements)
    }
    lastFrameT_ = GetTime();
    return true;
}

void App::shutdown() {
    stopAnalysis();
    saveMatch(); // a match left in play is kept for "Devam Et"
    cancelThink();
    if (other_) {
        other_->shutdown();
        other_.reset();
    }
    if (settingsDirty_ && !unattended()) persistSettings();
    table_.shutdown(renderer_);
    hands_.shutdown(renderer_);
    characters_.shutdown(renderer_);
    room_.shutdown(renderer_);
    if (rt_.id) UnloadRenderTexture(rt_);
    renderer_.shutdown();
    screens_.shutdown();
    if (!unattended()) memory_.save(memoryPath());
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
        ui::tilegfx::refresh();  // colour-blind tiles / four-colour cards repaint at once, also on the menus
        ui::cardgfx::refresh();

        BeginDrawing();
        const bool seatView = !snapshot_ || opt_.view == "seat";
        if (snapshot_) {
            if (snapshotRenderNow()) {
                submitWorld();
                renderer_.render(renderCam_, &rt_, CLEAR_COLOR);
            } else {
                renderer_.discardFrame(renderCam_);
                BeginTextureMode(rt_);
                ClearBackground(CLEAR_COLOR);
                EndTextureMode();
            }
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
        updateFirstTurnTip();
        if (opt_.hintDemo && !hintShown_ && !otherInGame() && game_.handState() == okey::HandState::Playing &&
            game_.current() == HUMAN && !table_.isAnimating()) {
            hintShown_ = true;
            showOkeyHint();
        }

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
    if (opt_.achievementTest) achievementTestReport(); // (Başarımlar check)
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
    if (opt_.aiChaos) {
        const Chaos& c = chaos_;
        std::printf("[aichaos] %s after %ld frames, %.2f h of game time: %d of %d matches, %d hands | mode flips %d "
                    "(%d via the pause menu of %d pauses), mode %s at the end | stand-in moves %d, rejected %d | "
                    "button presses %d, title starts %d | bot/AI rejections %d, fallback rejections %d, forced moves %d | "
                    "longest stall %.1f s\n",
                    exitCode_ == 0 ? "finished" : "FAILED", frame_, simClock_ / 3600.0, matchesDone_, opt_.matches,
                    handsPlayed_, c.flips, c.pauseFlipCount, c.pauses, aiMode_ ? "on" : "off", c.moves, c.movesRejected, c.presses,
                    c.titleStarts, rejected_, fallbackRejected_, forced_, c.worstStall);
        if (exitCode_ == 0 && (rejected_ || fallbackRejected_ || forced_ || c.movesRejected)) exitCode_ = 3;
    }
    if (reportStats()) stats_.print();
    shutdown();
    return exitCode_;
}

void App::tick(float dt) {
    fade_ = std::max(0.f, fade_ - dt / 0.7f);
    const float simDt = dt * std::max(0.05f, opt_.speed);
    simClock_ += simDt;
    const bool blockedAtStart = screens_.blocksGame();
    pollAnalysis();
    const Vector2 mouse = (snapshot_ || opt_.aiChaos) ? NO_MOUSE : ui::virtualMouse();

    // menus and overlays first: they own the keyboard (ESC, Enter) and the mouse while they are up
    handleScreenAction(screens_.update(dt, mouse, flow_ == Flow::Title ? nullptr : &game_));
    if (quit_) return;
    if (opt_.aiChaos) updateChaos(simDt, true);
    if (quit_) return;
    bool blocked = screens_.blocksGame();
    const bool inGame = flow_ != Flow::Title;

    // the human's eyes
    if (!inGame) {
        pcam_.update(dt, false);  // cinematic drift
        lookDrag_ = false;
    } else if (!blocked) {
        const bool lookOk = !snapshot_ && !(otherInGame() ? other_->mouseBusy() : table_.mouseBusy());
        // a right-drag that started as a look keeps looking even when it crosses a tile or a button
        lookDrag_ = lookDrag_ ? IsMouseButtonDown(MOUSE_BUTTON_RIGHT) : (lookOk && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));
        pcam_.update(dt, lookOk || lookDrag_);
    }
    cam_ = fitToCanvas(pcam_.camera(), (float)renderer_.renderWidth() / (float)std::max(1, renderer_.renderHeight()));
    renderCam_ = (snapshot_ && opt_.view != "seat") ? viewCamera(opt_.view, cam_, location_ == 1) : cam_;

    // the table: the human acts through it (never while a screen is up, while an AI plays the seat or in snapshots);
    // in the Yapay Zeka mode the HUD's buttons still take the mouse (switching the mode off, the menu)
    const bool tableUp = inGame && !blocked && !blockedAtStart && !snapshot_;
    const bool human = tableUp && !aiSeat() && !replayMode_;
    if (otherInGame()) {
        // the game runs only while no screen covers it (the bots wait behind the menus too)
        if (!blocked) other_->update(simDt, cam_, tableUp && !opt_.autoplay ? mouse : NO_MOUSE, human, aiSeat());
        if (other_->consumeMenuRequest() && !blocked) {
            screens_.show(ui::ScreenId::Paused);
            blocked = true;
        }
        const bool aiKey = tableUp && IsKeyPressed(KEY_Y);
        if ((other_->consumeAiToggleRequest() || aiKey) && !blocked) setAiMode(!aiMode_);
    } else {
        table_.update(simDt, cam_, tableUp && !opt_.autoplay ? mouse : NO_MOUSE, human);
        if (table_.consumeMenuRequest() && inGame && !blocked) {
            screens_.show(ui::ScreenId::Paused);
            blocked = true;
        }
        const bool aiKey = tableUp && !opt_.aiChaos && IsKeyPressed(KEY_Y);
        if ((table_.consumeAiToggleRequest() || aiKey) && inGame && !blocked) setAiMode(!aiMode_);
        if ((table_.consumeHintRequest() || (tableUp && human && IsKeyPressed(KEY_H))) && inGame && !blocked) showOkeyHint();
    }
    if (opt_.aiChaos && !blocked) updateChaos(simDt, false); // the stand-in plays where the player's clicks would
    pumpEvents();

    // the match: bots, the pause after a hand; frozen while any screen is up
    if (inGame && !blocked) updateFlow(simDt);
    updateAutoScreens(simDt);
    pumpEvents();
    updateAchievements(); // Başarımlar: the table's events of this frame

    updateWorld(dt, simDt, cam_, screens_.blocksGame());
}

void App::updateWorld(float dt, float simDt, const Camera3D& cam, bool blocked) {
    const bool playing = otherInGame() ? flow_ == Flow::Playing && !other_->handOver()
                                       : flow_ == Flow::Playing && game_.handState() == okey::HandState::Playing;
    std::array<Vector3, 4> heads;
    for (int s = 0; s < 4; ++s) heads[s] = characters_.headPosition(s);
    table_.setHeadAnchors(heads);
    characters_.setActiveSeat(playing && !blocked ? (otherInGame() ? other_->activeSeat() : game_.current()) : -1);
    Vector3 meowAt;
    if (room_.consumeCatMeow(meowAt)) characters_.onCatMeow(meowAt);
    { // (duzelt) the cat walks round the people standing on the floor
        static std::vector<Vector3> people;
        characters_.floorPeople(people);
        room_.setFloorPeople(people);
    }
    if (room_.consumeTvGoal()) {
        characters_.onTvGoal();
        derbyGoal();  // (ozelgun)
        // the room turns to the TV and so do we, unless we are busy with our own tiles
        if (playing && !blocked && game_.current() != HUMAN && !table_.mouseBusy() && !lookDrag_) pcam_.glanceAt(room_.venue() == 1 ? Vector3{1.42f, 1.15f, -2.95f} : TV_POS, 1.8f);  // (ozelgun: the garden's TV on a derby night)
    }
    const ui::Settings& st = screens_.settings();
    room_.setTimeOfDay(st.dayTime);
    room_.setSeason(st.season);
    room_.setVenue(st.venue);  // Mekân: içerisi / bahçe / otomatik
    if (audioOn_) audio_.setVenue(room_.venue(), room_.dayPhase() == 3);  // (Bahçe) the garden's sounds, crickets at night
    updateSpecialDay(dt);  // (ozelgun) özel günler; otomatik changes places between hands
    characters_.setTimeOfDay(st.dayTime);
    characters_.setSeason(st.season);
    characters_.setVenue(room_.venue());  // (Ocakçı) the TV is inside only
    characters_.setSpectatorMatch(flow_ != Flow::Title && flow_ != Flow::MatchOver && !blocked);
    // the room lives at game speed; modules clamp large steps, so fast-forward is split into small ones
    updateHands(simDt, blocked);  // (before the people: the glass in the player's hand is theirs to draw)
    const int steps = std::clamp((int)std::ceil(simDt / (1.f / 50.f)), 1, 10);
    const float sub = simDt / (float)steps;
    for (int i = 0; i < steps; ++i) {
        room_.update(sub);
        characters_.update(sub, cam);
        renderer_.update(sub);
    }
    std::string shout;
    Vector3 shoutAt;
    while (characters_.consumeCrowdLine(shout, shoutAt)) {
        if (opt_.achievementTest && achTest_.celebrated && !achTest_.applause && !screens_.blocksGame()) { // (Başarımlar check) the room claps
            achTest_.applause = true;
            std::printf("[basarim-test] the room: \"%s\"\n", shout.c_str());
        }
        if (shouts_.size() >= 4) shouts_.erase(shouts_.begin());
        shouts_.push_back({shout, shoutAt, 0.f});
        if (audioOn_) audio_.speak(4, shout);
    }
    for (Shout& sh : shouts_) sh.t += dt;
    shouts_.erase(std::remove_if(shouts_.begin(), shouts_.end(), [](const Shout& sh) { return sh.t > 2.4f; }), shouts_.end());
    int served;
    while (characters_.consumeTeaServed(served))
        if (audioOn_) audio_.play(ui::Sfx::GlassSet);
    updateDelayedAudio(simDt);
    if (audioOn_) audio_.setRain(room_.rainAmount());  // the rain bed follows tonight's weather outside
    if (audioOn_) audio_.update(dt);
    // the radio starts a new record: a quiet note at the table (it also credits the recording)
    std::string song;
    if (audioOn_ && audio_.consumeNowPlaying(song) && flow_ != Flow::Title) {
        if (otherInGame()) other_->toast("Radyoda: " + song, Color{214, 200, 170, 230}, 3.2f);
        else table_.toast("Radyoda: " + song, Color{214, 200, 170, 230}, 3.2f);
    }
    updateScoreboard();
    if (settingsDirty_ && screens_.current() != ui::ScreenId::Settings && !unattended()) {
        persistSettings();
        settingsDirty_ = false;
    }
}

void App::submitWorld() {
    room_.submit(renderer_);
    characters_.submit(renderer_);
    table_.submit(renderer_);
    if (otherInGame()) other_->submit(renderer_);
    if (!(snapshot_ && opt_.view != "seat")) hands_.submit(renderer_); // (arms without a body: only from the seat)
}

// The player's own hands: the table we sit at, a screen over it (they sink out of view), whose turn it is.
void App::updateHands(float simDt, bool blocked) {
    using S = r3d::PlayerHands::Scene;
    const bool inGame = flow_ != Flow::Title;
    hands_.setScene(!inGame ? S::Hidden : location_ == 1 ? S::Tavla : otherInGame() ? S::Cards : S::Okey);
    hands_.setLowered(blocked || fade_ > 0.4f);
    const bool mine = otherInGame() ? other_->activeSeat() == HUMAN
                                    : game_.handState() == okey::HandState::Playing && game_.current() == HUMAN;
    hands_.setMyTurn(inGame && mine && !aiSeat());
    hands_.update(simDt, cam_);
}

void App::drawOverlays(bool hud) {
    characters_.drawOverlay(renderer_);
    drawCrowdShouts();
    // the table's HUD (buttons, plates, status) steps aside while a menu or the score sheet covers the table
    if (hud && flow_ != Flow::Title && !screens_.blocksGame()) {
        if (otherInGame()) other_->drawHUD(renderer_, (snapshot_ || opt_.aiChaos) ? NO_MOUSE : ui::virtualMouse(), aiSeat());
        else table_.drawHUD(renderer_);
        drawReplayBadge();
    }
    screens_.draw(flow_ == Flow::Title ? nullptr : &game_);
    if (fade_ > 0.f) { // a table change: in from black (eased)
        const float a = fade_ * fade_ * (3.f - 2.f * fade_);
        DrawRectangle(-200, -200, (int)ui::VW + 400, (int)ui::VH + 400, Color{8, 5, 3, (unsigned char)(a * 255.f)});
    }
}

} // namespace detail

// ================================================================ command line
void printUsage(const char* argv0) {
    std::printf("SaklıBahçe — 101 Okey, dumanaltı bir kahvehanede\n\n"
                "Kullanım: %s [seçenekler]\n"
                "  --seed N          aynı dağıtımlar ve aynı rakipler (tekrar oynatılabilir)\n"
                "  --start           giriş ekranını atla, doğrudan oyuna başla\n"
                "  --hands N         el sayısı (1-11)\n"
                "  --level L         rakip seviyesi: 0 Acemi, 1 Usta, 2 Kurt\n"
                "  --ai              Yapay Zeka modunda başla: senin yerine yapay zeka oynar (oyunda Y ile aç/kapa)\n"
                "  --katlamali       katlamalı oyun: her açan, öncekinden en az 1 fazlasıyla açar\n"
                "  --game G          oyun: 101, esli, okey, tavla, pisti, batak, king\n"
                "  --set K=V         bir ayar (ayarlar.txt anahtarları, ör. batakesli=1, pistimasa=2, tavla=3)\n"
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
                "      --render-last N  3B dünyayı yalnızca son N karede çiz (yazılımsal GL'de hızlı)\n"
                "      --state S     title | game | summary | matchover | rules | settings | games\n"
                "      --view V      seat | left | right | back | corner | up | wide\n"
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
        } else if (a == "--ai") {
            o.ai = true;
        } else if (a == "--katlamali") {
            o.katlamali = true;
        } else if (a == "--watch") {
            o.watch = true;
        } else if (a == "--analyze") {
            o.analyze = true;
        } else if (a == "--resume") {
            o.resume = true;
        } else if (a == "--guide-demo") {
            o.guideDemo = true;
        } else if (a == "--hint-demo") {
            o.hintDemo = true;
        } else if (a == "--achievement-demo") {
            o.achievementDemo = true;
        } else if (a == "--achievement-test") { // (developer check: Başarımlar end to end)
            o.achievementTest = true;
            o.autoplay = true;
        } else if (a == "--set") {
            if (!(s = need(i, "--set"))) return false;
            if (!std::strchr(s, '=')) {
                error = "--set anahtar=değer bekliyor (ör. --set batakesli=1)";
                return false;
            }
            o.sets.push_back(s);
        } else if (a == "--game") {
            if (!(s = need(i, "--game"))) return false;
            static const char* const names[] = {"101", "esli", "okey", "tavla", "pisti", "batak", "king", "dama", "66", "bezik", "konken"};
            static_assert(sizeof names / sizeof names[0] == (size_t)ui::GameKind::Count, "--game names");
            o.game = -1;
            for (int k = 0; k < (int)ui::GameKind::Count; ++k)
                if (std::string(s) == names[k]) o.game = k;
            if (o.game < 0 || !ui::gameAvailable((ui::GameKind)o.game)) {
                error = "--game: 101, esli, okey, tavla, pisti, batak, king, dama, 66, bezik, konken (bu sürümde hazır olanlar)";
                return false;
            }
        } else if (a == "--ai-chaos") {
            o.aiChaos = true;
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
        } else if (a == "--render-last") {
            if (!(s = need(i, "--render-last")) || !toInt(s, v, "--render-last")) return false;
            o.renderLast = (int)std::max(1L, v);
        } else if (a == "--frames") {
            if (!(s = need(i, "--frames")) || !toInt(s, v, "--frames")) return false;
            o.frames = (int)std::clamp(v, 1L, 1000000L);
        } else if (a == "--state") {
            if (!(s = need(i, "--state"))) return false;
            o.state = s;
            static const char* const ok[] = {"title", "game", "summary", "matchover", "rules", "settings", "games", "stats",
                                             "basarimlar"};
            if (std::none_of(std::begin(ok), std::end(ok), [&](const char* k) { return o.state == k; })) {
                error = "--state: title, game, summary, matchover, rules, settings, games, stats ya da basarimlar";
                return false;
            }
        } else if (a == "--view") {
            if (!(s = need(i, "--view"))) return false;
            o.view = s;
            static const char* const ok[] = {"seat", "left", "right", "back", "corner", "up", "wide", "tv", "ocak", "ocakci"};  // (up, wide: Bahçe; tv, ocak: ozelgun; ocakci: Ocakçı)
            if (std::none_of(std::begin(ok), std::end(ok), [&](const char* k) { return o.view == k; })) {
                error = "--view: seat, left, right, back, corner, up ya da wide";
                return false;
            }
        } else {
            error = "bilinmeyen seçenek: " + a;
            return false;
        }
    }
    if (o.autoplay || o.ai || o.aiChaos) o.start = true;
    if (o.aiChaos) o.noAudio = true;
    return true;
}

int run(const Options& options) {
    // The world is large (meshes, textures, a renderer) — keep it off the stack.
    std::unique_ptr<detail::App> app = std::make_unique<detail::App>(options);
    return app->run();
}

} // namespace app
