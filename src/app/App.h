#pragma once
// SaklıBahçe — the application: window, main loop, match flow, bot pacing and the wiring between the
// engine (okey::Game / okey::Bot), the 3D world (Room, Characters, Table3D, PlayerCamera), procedural audio
// and the menu screens. Integration owner. Frame structure: DESIGN3D.md §2.
#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct Options {
    bool hasSeed = false;       // --seed N: deterministic deals, bots, room and characters
    uint64_t seed = 0;
    bool autoplay = false;      // --autoplay: seat 0 is played by an Usta bot (implies --start)
    bool ai = false;            // --ai: start a match in the Yapay Zeka mode (a Kurt plays seat 0; implies --start)
    // --ai-chaos (developer robustness test, not in --help): a hidden, silent window starts a match in the Yapay Zeka
    // mode and flips the mode at random moments (also through the pause menu); while it is off a stand-in plays the
    // seat and presses the between-hands buttons. --matches N matches, then a report (stuck turns, rejections).
    bool aiChaos = false;
    float speed = 1.f;          // --speed X: fast-forward the game (flights, bot pacing, the room keeps up)
    int matches = 1;            // --matches N (autoplay): N matches back to back, every other one via the title screen
    int hands = 0;              // --hands N (1..11), 0 = from the settings
    int level = -1;             // --level 0..2 (Acemi / Usta / Kurt), -1 = from the settings
    bool watch = false;         // --watch: watch the newest finished match (Tekrarlar)
    bool analyze = false;       // --analyze: "Hatalarım" of the newest finished match
    bool resume = false;        // --resume: continue the saved match at once ("Devam Et")
    bool guideDemo = false;     // --guide-demo (snapshots): the guide card shows even in an unattended run
    bool hintDemo = false;      // --hint-demo (snapshots): ask for an İpucu once the player's turn comes
    bool achievementDemo = false; // --achievement-demo (snapshots): a badge opens at frame 120 (banner, a regular, the crowd)
    // --achievement-test (developer check, not in --help; implies --autoplay): the bot's match counts as the player's own
    // for the badges (and the record) in this run only, the book is read and written under $HOME as in play; the run
    // ends once a badge has opened and its banner, a regular's congratulation and the applause were all seen (exit 0),
    // or fails (exit 4) when the match ends first.
    bool achievementTest = false;
    bool katlamali = false;     // --katlamali: this run's matches are katlamalı (else from the settings)
    int game = -1;              // --game 101|esli|okey|...: this run's game (ui::GameKind), -1 = from the settings
    std::vector<std::string> sets; // --set key=value: settings-file keys for this run
    bool start = false;         // --start: skip the title screen
    bool noAudio = false;       // --no-audio
    long maxFrames = 0;         // --max-frames N: quit after N frames (0 = never)
    bool perf = false;          // --perf: no vsync / frame cap, print frame-time statistics at exit
    std::string screenshot;     // --screenshot PATH: save the window's own framebuffer on the last frame

    // --snapshot PATH: render one frame into a 1600x900 PNG from a hidden window and exit
    std::string snapshot;
    int frames = -1;            // --frames N: frames to simulate before the capture (-1 = per state default)
    int renderLast = -1;        // --render-last N: (--snapshot) draw the 3D world only in the last N frames (-1 = all)
    std::string state;          // --state title|game|summary|matchover|rules|settings (snapshots: game by default)
    std::string view = "seat";  // --view seat|left|right|back|corner
};

// Parses the command line. Returns false (with a Turkish/English message in `error`) on bad input;
// `wantHelp` is set for --help.
bool parseArgs(int argc, char** argv, Options& out, std::string& error, bool& wantHelp);
void printUsage(const char* argv0);

// Runs the game (or the snapshot) and returns the process exit code.
int run(const Options& options);

} // namespace app
