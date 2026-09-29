#pragma once
// Kıraathane 101 — the application: window, main loop, match flow, bot pacing and the wiring between the
// engine (okey::Game / okey::Bot), the 3D world (Room, Characters, Table3D, PlayerCamera), procedural audio
// and the menu screens. Integration owner. Frame structure: DESIGN3D.md §2.
#include <cstdint>
#include <string>

namespace app {

struct Options {
    bool hasSeed = false;       // --seed N: deterministic deals, bots, room and characters
    uint64_t seed = 0;
    bool autoplay = false;      // --autoplay: seat 0 is played by an Usta bot (implies --start)
    float speed = 1.f;          // --speed X: fast-forward the game (flights, bot pacing, the room keeps up)
    int matches = 1;            // --matches N (autoplay): N matches back to back, every other one via the title screen
    int hands = 0;              // --hands N (1..11), 0 = from the settings
    int level = -1;             // --level 0..2 (Acemi / Usta / Kurt), -1 = from the settings
    bool start = false;         // --start: skip the title screen
    bool noAudio = false;       // --no-audio
    long maxFrames = 0;         // --max-frames N: quit after N frames (0 = never)
    bool perf = false;          // --perf: no vsync / frame cap, print frame-time statistics at exit
    std::string screenshot;     // --screenshot PATH: save the window's own framebuffer on the last frame

    // --snapshot PATH: render one frame into a 1600x900 PNG from a hidden window and exit
    std::string snapshot;
    int frames = -1;            // --frames N: frames to simulate before the capture (-1 = per state default)
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
