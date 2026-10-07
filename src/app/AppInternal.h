#pragma once
// App's insides, shared by the files the application is split into (one class, by topic; no behaviour lives here):
//   App.cpp          setup, the frame loop, the 3D world and the 2D layer, the command line, run()
//   AppFlow.cpp      the match flow: starting / ending matches and hands, the table we sit at, screen actions, the
//                    engine's events, audio routing, the scoreboard, the room's reactions, rehber and İpucu
//   AppBots.cpp      the okey bots' pacing and the Yapay Zeka mode (with its table messages)
//   AppRecord.cpp    the player's record, save / resume ("Devam Et") and Tekrarlar
//   AppSettings.cpp  the settings file and the command line's session-only overrides
//   AppAnalysis.cpp  "Hatalarım" on a worker thread
//   AppDebug.cpp     snapshots and their extra views, frame statistics, --ai-chaos
#include "app/App.h"
#include "app/Analysis.h"
#include "app/TableGame.h"

#include "core/Bot.h"
#include "core/Game.h"
#include "core/Rng.h"
#include "r3d/Characters.h"
#include "r3d/Gfx.h"
#include "r3d/PlayerHands.h"
#include "r3d/Room.h"
#include "r3d/Daytime.h"     // (ozelgun)
#include "r3d/SpecialDay.h"  // (ozelgun)
#include "r3d/Table3D.h"
#include "r3d/World.h"
#include "ui/Audio.h"
#include "ui/Banter.h"
#include "ui/Common.h"
#include "ui/Memory.h"
#include "ui/CardRender.h"
#include "ui/TileRender.h"
#include "ui/Screens.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

namespace app {
namespace detail {

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
constexpr float AI_SUMMARY = 7.0f;      // Yapay Zeka mode: the score sheet presses its button by itself after this
constexpr float AI_MATCHOVER = 12.0f;   // ... and the final standings "Yeni Oyun"
constexpr float AI_BANTER_GAP = 40.f;   // the regulars remark on the mode at most this often (seconds)
constexpr float LANDING_LAG = 0.30f;    // tile flight minus Audio's own 0.12 s gap: clacks land with the tile
constexpr int MAX_BOT_ACTIONS = 60;     // watchdog: a bot turn never takes more actions than this

// ---- shared helpers (defined in the file of their topic)
uint64_t mix64(uint64_t x);                                            // App.cpp
uint64_t clockSeed();
const char* actionName(okey::BotAction::Kind k);
Camera3D fitToCanvas(Camera3D c, float aspect);
Camera2D windowCamera2D(const ui::Viewport& vp);
std::string trim(const std::string& s);
Camera3D viewCamera(const std::string& view, const Camera3D& seat, bool tavla); // AppDebug.cpp
std::string watchText(const okey::GameEvent& e);                       // AppBots.cpp
std::string supportDir();                                              // AppSettings.cpp
std::string statsPath();
std::string memoryPath();
std::string achievementsPath();                                        // AppAchievements.cpp
std::string settingsPath();
std::string legacySettingsPath();
void applySettingLine(ui::Settings& s, const std::string& k, const std::string& v);
void loadSettings(ui::Settings& s);
std::string settingsText(const ui::Settings& s);
bool ensureDirOf(const std::string& path);
void saveSettings(const ui::Settings& s);

// ---------------------------------------------------------------- the match left unfinished (kayit.txt)
struct SavedMatch {
    int game = -1;
    uint64_t seed = 0;
    bool aiTouched = false;
    std::string label;                 // "101 · 3. el" for the title
    std::string date, result;          // tekrarlar: when it was played, how it ended
    std::vector<std::string> settings; // "key=value"
    std::vector<std::string> actions;  // the engine's own lines
};

std::string savePath();                                                // AppRecord.cpp
std::string replayDir();
bool readSave(SavedMatch& sv, const std::string& from = std::string());

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
    enum class SnapState { Title, Game, Summary, MatchOver, Rules, Settings, Games, Stats, Achievements };

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
    bool reportStats() const { return opt_.perf || opt_.autoplay || opt_.aiChaos || opt_.maxFrames > 0; }
    // runs that play by themselves never read or write the player's settings file
    bool unattended() const { return snapshot_ || opt_.autoplay || opt_.aiChaos; }
    void recordStats(float cpuMs);
    bool snapshotDone();
    bool snapshotRenderNow() const;
    bool exportSnapshot();
    void saveWindowShot();
    // flow
    void handleScreenAction(ui::ScreenAction a);
    void startMatch();
    void startNextHand();
    void startOtherMatch(ui::GameKind kind);
    void toTitle();
    void setTitleMode(bool on);
    // Which table we sit at: 0 our okey table, 1 the tavla table (with `seat` the regular across). The camera, the
    // opponent, the glasses and the key light move there behind a short fade.
    void setLocation(int loc, int seat);
    int location_ = 0, locationSeat_ = 0;
    float fade_ = 0.f; // 1 black .. 0 clear (a table change)
    void applySettings();
    void releaseOverrides();
    void persistSettings();
    void updateFlow(float simDt);
    bool recording() const;
    void saveMatch();
    std::string matchText(const std::string& label, const std::vector<std::string>& actions) const;
    void archiveMatch(const std::string& result);
    // "Hatalarım": Kurt replays a finished match on a worker thread (analysis::analyzeMatch) and names the player's
    // most expensive mistakes; the screens get them when it is done (pollAnalysis, every frame).
    void startAnalysis(int game, const ui::Settings& rules, uint64_t seed, std::vector<std::string> actions,
                       const std::string& title);
    void analyzeFinishedMatch();
    void analyzeReplay(int index);
    void stopAnalysis();
    void pollAnalysis();
    std::vector<std::string> listReplays() const;
    void refreshReplays();
    void startReplay(int index);
    void updateReplay(float simDt);
    void drawReplayBadge();
    void crowdAtHandEnd();             // the room reacts to a big finish, a mars, a match lost at the wire
    void drawCrowdShouts();
    void resumeSaved();
    void deleteSave();
    void refreshResumable();
    void maybeShowGuide(ui::GameKind kind);
    void updateFirstTurnTip();
    void showOkeyHint();
    void recordOkeyHand();
    void recordOtherHand();
    void noteRankUp();
    // Başarımlar (AppAchievements.cpp)
    void initAchievements();
    void saveAchievements();
    void celebrate(const std::vector<int>& opened);
    // --achievement-test (AppAchievements.cpp): what the room did with the badges that opened in this run
    struct AchTest {
        std::vector<std::string> names;  // badges opened
        bool celebrated = false;
        long bannerFrames = 0;           // frames the banner was up
        bool congratulated = false;      // a regular said a line naming one of them
        bool applause = false;           // the room shouted (crowdReact)
        long doneAt = -1;                // frame all of it had been seen
    } achTest_;
    void achievementTestSpeech(int who, const std::string& text);
    void achievementTestFrame();
    void achievementTestReport();
    float achHold_ = 0.f;          // seconds the score sheet waits after a hand so a badge's celebration is seen
    void achievementsOkeyEvent(const okey::GameEvent& e);
    void updateAchievements();
    void achievementsAtHandEnd();
    void achievementsAnalysis(int game, int mistakes);
    void updateAutoScreens(float simDt);
    void pressBetweenHands();
    void pumpEvents();
    void routeAudio(const okey::GameEvent& e);
    void updateDelayedAudio(float simDt);
    void updateScoreboard();
    std::array<std::string, 4> names();
    float animSpeed() { return std::max(0.25f, screens_.settings().animSpeed); }
    // the Yapay Zeka mode: an AI plays the human's seat (as --autoplay's bot does for a whole run)
    bool aiSeat() const { return opt_.autoplay || aiMode_; }
    void setAiMode(bool on);
    void updateChaos(float simDt, bool beforeTable);
    // bots
    bool isBotSeat(int s) const { return s != HUMAN || aiSeat(); }
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
    r3d::PlayerHands hands_;   // the player's own hands and forearms at the bottom of the view (App::updateHands)
    void updateHands(float simDt, bool blocked);
    // ---- Özel günler and the rain in the garden (AppSpecial.cpp, ozelgun): the day (r3d/SpecialDay.h) for the room and
    // the people, the regulars' lines for it, the sahur davul; with Mekân = otomatik the move between the garden and the
    // room happens between hands behind a short fade (the room holds its wish meanwhile: Room::venuePending)
    void updateSpecialDay(float dt);
    void derbyGoal();               // a goal on the TV on a derby night: louder reactions
    int specialDay_ = 0;
    bool specialOn_ = true;
    float specialCheckT_ = 0.f, specialChatT_ = 90.f, davulT_ = 25.f, specialGreetT_ = 0.f;
    uint64_t specialGreetSeed_ = 0;  // the match the greeting was said in
    float venueMoveT_ = -1.f;        // >= 0: fading out to change places
    int venueMoveWhy_ = 0;
    okey::Rng specialRng_;
    ui::Audio audio_;
    ui::Screens screens_;
    okey::Game game_;
    std::array<std::unique_ptr<okey::Bot>, 4> bots_;
    // the other games (tavla, the card games): the active one, or null while an okey game is played
    std::unique_ptr<TableGame> other_;
    ui::GameKind otherKind_ = ui::GameKind::Yuzbir;
    bool otherInGame() const { return other_ != nullptr && flow_ != Flow::Title; }
    RenderTexture2D rt_{};

    Flow flow_ = Flow::Title;
    // maç tekrarı: a saved match played back action by action
    bool replayMode_ = false, replayPaused_ = false;
    float replaySpeed_ = 1.f, replayWait_ = 0.f;
    std::vector<std::string> replayActions_;
    size_t replayPos_ = 0;
    std::vector<std::string> replayFiles_;
    struct Shout {
        std::string text;
        Vector3 at;
        float t = 0.f;
    };
    std::vector<Shout> shouts_;        // the other tables' shouts ("Vay be!", "Sağ ol!"), drawn where they came from
    std::thread anaThread_;
    std::atomic<bool> anaCancel_{false}, anaDone_{false};
    std::vector<analysis::Mistake> anaResult_;
    std::string anaTitle_;
    std::optional<uint64_t> forcedSeed_;  // resume: the saved match's seed
    bool resuming_ = false;
    uint64_t matchSeed_ = 0;              // the match in play: its seed, game and settings (for the save)
    int matchGame_ = 0;
    ui::Settings matchSettings_;
    std::string firstTurnTip_;            // rehber: said when the player's first turn comes
    bool hintShown_ = false;              // --hint-demo
    std::unique_ptr<okey::Bot> hintBot_; // İpucu: a Kurt asked what it would do in the player's seat
    ui::Memory memory_;            // what the regulars remember of the player (hafiza.txt)
    ui::StatsBook record_;         // the player's record (İstatistik screen)
    bool matchAiTouched_ = false;  // the Yapay Zeka mode played in this match (it is not recorded)
    // Başarımlar: the badges (basarimlar.txt); the match the Yapay Zeka has played from its first move; the analysis
    // running belongs to a match the player played himself (and of which game)
    ui::Achievements achievements_;
    int achMatch_ = -1;
    bool aiWholeMatch_ = false;
    bool anaCounts_ = false;
    int anaGame_ = -1;
    bool aiMode_ = false;          // Yapay Zeka mode (off at every launch; Title "Yapay Zekayı İzle", --ai)
    int aiSwitches_ = 0;
    double aiBanterAt_ = -1e9;     // when the regulars last remarked on the mode (simClock_)
    double simClock_ = 0.0;        // game time since launch (--speed included)
    uint64_t baseSeed_ = 0;
    int matchCount_ = 0, matchesDone_ = 0;
    okey::Rng paceRng_;
    Think think_;
    std::vector<DelayedEvent> delayed_;
    float handOverT_ = 0.f, screenT_ = 0.f;  // screenT_: how long the current screen has been up (auto-advance)
    ui::ScreenId autoScreen_ = ui::ScreenId::Title;
    bool lookDrag_ = false;
    bool settingsDirty_ = false;
    // --hands, --level and --no-audio shape this session only. The settings screen shows the values in force, but
    // the file keeps the loaded ones for every setting the player has not moved away from its command-line value.
    struct Overridden {
        bool hands = false, level = false, sfx = false, ambient = false, music = false, game = false;
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

    // --ai-chaos: flips the Yapay Zeka mode at random moments and stands in for the player while it is off
    struct Chaos {
        okey::Rng rng;
        std::unique_ptr<okey::Bot> standIn;  // plays the seat (through the same Game API as the table) while off
        float nextFlip = 1.f;                // seconds until the next flip of the mode
        float nextPause = 8.f;               // ... until the pause menu is opened
        float pauseLeft = -1.f;              // > 0: the pause menu is up
        bool pauseFlips = false;             // leave the pause menu through "Yapay Zeka Oynasın / Kontrolü Geri Al"
        float actIn = 0.f;                   // the stand-in's reaction time before its next move
        float pressIn = -1.f;                // ... before it presses a between-hands button
        float titleIn = -1.f;                // ... before it starts a match from the title screen
        int flips = 0, pauseFlipCount = 0, pauses = 0, moves = 0, movesRejected = 0, presses = 0, titleStarts = 0;
        uint64_t sig = 0;                    // progress watchdog: anything that should move keeps moving
        double sigAt = 0.0, worstStall = 0.0;
    } chaos_;
};

} // namespace detail
} // namespace app
