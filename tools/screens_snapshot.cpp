// Screens owner's visual + behavioural self-check.
//
// Opens a hidden window, loads the fonts, plays real okey::Game matches with a small greedy scripted
// policy, drives ui::Screens through raylib's automation events (real mouse clicks, wheel and ESC go
// through the same input path as the game) and renders every screen over a dark placeholder room into
// build/screens/*.png (or $SCREENS_OUT/*.png). State-machine expectations are checked along the way; exit code 1
// on failure.
//
//   build:  clang++ -std=c++17 -O2 -Wall -Wextra -Isrc -I/opt/homebrew/include src/core/Meld.cpp
//           src/core/Game.cpp src/ui/Common.cpp src/ui/Screens.cpp tools/screens_snapshot.cpp
//           /opt/homebrew/lib/libraylib.a -framework ... -o build/screens/screens_snapshot
#include "core/Game.h"
#include "ui/Common.h"
#include "ui/Screens.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using okey::Game;
using V = std::vector<int>;
using G = std::vector<V>;

namespace {

int g_failures = 0;
int g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

const char* actionName(ui::ScreenAction a) {
    switch (a) {
    case ui::ScreenAction::None: return "None";
    case ui::ScreenAction::StartMatch: return "StartMatch";
    case ui::ScreenAction::Resume: return "Resume";
    case ui::ScreenAction::NextHand: return "NextHand";
    case ui::ScreenAction::ShowMatchResult: return "ShowMatchResult";
    case ui::ScreenAction::ToTitle: return "ToTitle";
    case ui::ScreenAction::Quit: return "Quit";
    case ui::ScreenAction::SettingsChanged: return "SettingsChanged";
    case ui::ScreenAction::StartAiMatch: return "StartAiMatch";
    case ui::ScreenAction::ToggleAiMode: return "ToggleAiMode";
    }
    return "?";
}

// ============================================================================ scripted players

// Plain runs / groups first, then melds completed with jokers. Every meld is validated with makeMeld.
G findSeries(const V& hand, const okey::OkeyInfo& ok) {
    V jokers, plain;
    for (int id : hand) (ok.isJoker(id) ? jokers : plain).push_back(id);
    std::vector<char> used(plain.size(), 0);
    G melds;
    auto runs = [&](bool withJokers) {
        for (int c = 0; c < okey::NUM_COLORS; ++c) {
            int slot[16];
            std::fill(slot, slot + 16, -1);
            for (int i = 0; i < (int)plain.size(); ++i)
                if (!used[(size_t)i] && ok.faceColor(plain[(size_t)i]) == c && slot[ok.faceNumber(plain[(size_t)i])] < 0)
                    slot[ok.faceNumber(plain[(size_t)i])] = i;
            int n = 1;
            while (n <= 13) {
                if (slot[n] < 0) {
                    ++n;
                    continue;
                }
                int e = n;
                while (e + 1 <= 13 && slot[e + 1] >= 0) ++e;
                V m;
                if (e - n + 1 >= 3) {
                    for (int k = n; k <= e; ++k) m.push_back(plain[(size_t)slot[k]]);
                } else if (withJokers && !jokers.empty() && e - n + 1 == 2) {
                    m = {plain[(size_t)slot[n]], plain[(size_t)slot[e]], jokers.back()};
                }
                if (!m.empty()) {
                    for (int id : m) {
                        if (ok.isJoker(id)) jokers.pop_back();
                        else
                            for (size_t i = 0; i < plain.size(); ++i)
                                if (plain[i] == id) used[i] = 1;
                    }
                    melds.push_back(m);
                }
                n = e + 1;
            }
        }
    };
    auto groups = [&](bool withJokers) {
        for (int n = 1; n <= 13; ++n) {
            int pick[4] = {-1, -1, -1, -1};
            for (int i = 0; i < (int)plain.size(); ++i)
                if (!used[(size_t)i] && ok.faceNumber(plain[(size_t)i]) == n && pick[ok.faceColor(plain[(size_t)i])] < 0)
                    pick[ok.faceColor(plain[(size_t)i])] = i;
            V m;
            for (int c = 0; c < 4; ++c)
                if (pick[c] >= 0) m.push_back(plain[(size_t)pick[c]]);
            if (m.size() == 2 && withJokers && !jokers.empty()) {
                m.push_back(jokers.back());
                jokers.pop_back();
            }
            if (m.size() < 3) continue;
            for (int c = 0; c < 4; ++c)
                if (pick[c] >= 0) used[(size_t)pick[c]] = 1;
            melds.push_back(m);
        }
    };
    runs(false);
    groups(false);
    runs(true);
    groups(true);
    G valid;
    for (const V& m : melds) {
        okey::Meld out;
        if (okey::makeMeld(m, ok, out, false)) valid.push_back(m);
    }
    return valid;
}

G findPairs(const V& hand, const okey::OkeyInfo& ok) {
    G pairs;
    std::vector<char> used(hand.size(), 0);
    for (size_t i = 0; i < hand.size(); ++i) {
        if (used[i] || ok.isJoker(hand[i])) continue;
        for (size_t j = i + 1; j < hand.size(); ++j) {
            if (used[j] || ok.isJoker(hand[j])) continue;
            if (ok.faceColor(hand[i]) == ok.faceColor(hand[j]) && ok.faceNumber(hand[i]) == ok.faceNumber(hand[j])) {
                used[i] = used[j] = 1;
                pairs.push_back({hand[i], hand[j]});
                break;
            }
        }
    }
    return pairs;
}

int tileCount(const G& g) {
    int n = 0;
    for (const V& v : g) n += (int)v.size();
    return n;
}

struct Player {
    okey::Rng rng;
    float mistakeRate = 0.08f; // sometimes discard an işlek tile / the okey (penalties on the sheet)

    void turn(Game& g, int seat) {
        const okey::OkeyInfo& ok = g.okey();
        if (g.stage() == okey::TurnStage::NeedDraw) {
            if (!g.drawFromPile(seat).ok) return;
        }
        const okey::PlayerInfo& me = g.player(seat);
        if (!me.opened) tryOpen(g, seat);
        if (g.canWorkTable(seat)) workTable(g, seat);
        if (g.handState() != okey::HandState::Playing || g.current() != seat) return;
        // discard
        const V hand = g.player(seat).hand;
        int best = -1, bestVal = -1;
        const bool blunder = hand.size() > 1 && rng.chance(mistakeRate);
        for (int t : hand) {
            if (blunder) {
                if (g.isPlayableOnTable(t) || ok.isJoker(t)) {
                    best = t;
                    break;
                }
                continue;
            }
            if (hand.size() > 1 && (ok.isJoker(t) || g.isPlayableOnTable(t))) continue;
            const int v = ok.faceNumber(t) * 8 + rng.range(8);
            if (v > bestVal) {
                bestVal = v;
                best = t;
            }
        }
        if (best < 0) best = hand[(size_t)rng.range((int)hand.size())];
        if (!g.discard(seat, best).ok) g.discard(seat, hand.front());
    }

    void tryOpen(Game& g, int seat) {
        const okey::OkeyInfo& ok = g.okey();
        const V hand = g.player(seat).hand;
        G ser = findSeries(hand, ok);
        while (!ser.empty() && tileCount(ser) >= (int)hand.size()) ser.pop_back();
        if (!ser.empty() && g.checkOpen(seat, ser).valid) {
            g.openHand(seat, ser);
            return;
        }
        G prs = findPairs(hand, ok);
        while (!prs.empty() && tileCount(prs) >= (int)hand.size()) prs.pop_back();
        if ((int)prs.size() >= g.rules().minPairsToOpen && g.checkOpen(seat, prs).valid) g.openHand(seat, prs);
    }

    void workTable(Game& g, int seat) {
        const okey::OkeyInfo& ok = g.okey();
        {
            const V hand = g.player(seat).hand;
            G more = g.player(seat).openedWithPairs ? findPairs(hand, ok) : findSeries(hand, ok);
            while (!more.empty() && tileCount(more) >= (int)hand.size()) more.pop_back();
            if (!more.empty() && g.checkLay(seat, more).valid) g.layMelds(seat, more);
        }
        for (int guard = 0; guard < 40 && g.player(seat).hand.size() > 1; ++guard) {
            bool did = false;
            const V hand = g.player(seat).hand;
            for (int t : hand) {
                if (ok.isJoker(t)) continue;
                for (int mi = 0; mi < (int)g.table().size() && !did; ++mi) {
                    okey::Meld out;
                    if (!okey::tryAddTile(g.table()[(size_t)mi], t, ok, okey::AddSide::Auto, out)) continue;
                    did = g.addToMeld(seat, t, mi).ok;
                }
                if (did) break;
            }
            if (!did) break;
        }
    }
};

struct Sim {
    Game game;
    std::array<Player, 4> players;
    void setup(uint64_t seed, int numHands, const std::string& humanName = "Sen") {
        okey::RulesConfig cfg;
        cfg.numHands = numHands;
        game.setRules(cfg);
        game.setPlayer(0, humanName, true);
        game.setPlayer(1, "Hacı Rıza", false);
        game.setPlayer(2, "Kel Mahmut", false);
        game.setPlayer(3, "Emekli Nuri", false);
        for (int i = 0; i < 4; ++i) players[(size_t)i].rng.reseed(seed * 977 + (uint64_t)i * 31 + 5);
        game.startMatch(seed);
        game.drainEvents();
    }
    // Plays `maxTurns` turns (or until the hand ends).
    void play(int maxTurns = 100000) {
        for (int t = 0; t < maxTurns && game.handState() == okey::HandState::Playing; ++t) {
            const int seat = game.current();
            const int before = game.turnNumber();
            players[(size_t)seat].turn(game, seat);
            if (game.handState() == okey::HandState::Playing && game.turnNumber() == before) {
                // policy got stuck: force a legal discard
                const V& h = game.player(seat).hand;
                if (!h.empty()) game.discard(seat, h.front());
            }
            game.drainEvents();
        }
    }
};

// How good a match is for the screenshots (hand 1 with a finisher and multipliers, penalties...).
struct Pick {
    uint64_t seed = 1;
    int score = -1;
};
int interest(uint64_t seed, int hands, bool wantHumanWin) {
    Sim s;
    s.setup(seed, hands);
    int score = 0;
    for (int h = 0; h < hands; ++h) {
        s.play();
        const okey::HandResult& r = s.game.lastHandResult();
        if (h == 0) {
            if (r.winner >= 0) score += 10;
            if (r.multiplier > 1) score += 6;
            for (int p = 0; p < 4; ++p) {
                if (s.game.player(p).opened && s.game.player(p).openedWithPairs) score += 3;
                if (!s.game.player(p).opened) score += 1;
                if (r.penalties[(size_t)p] > 0) score += 2;
            }
        }
        if (h == hands - 1 && r.winner >= 0) score += 3;
        if (s.game.handState() == okey::HandState::HandOver) s.game.startNextHand();
    }
    const bool humanWon = s.game.leaderSeat() == 0;
    if (humanWon != wantHumanWin) return -1;
    return score;
}

// ============================================================================ rendering / input

constexpr unsigned EV_KEY_UP = 1, EV_KEY_DOWN = 2, EV_MOUSE_UP = 5, EV_MOUSE_DOWN = 6, EV_WHEEL = 8;
void inject(unsigned type, int p0, int p1 = 0) {
    AutomationEvent e{};
    e.type = type;
    e.params[0] = p0;
    e.params[1] = p1;
    PlayAutomationEvent(e);
}

struct Harness {
    RenderTexture2D rt{};
    ui::Screens screens;
    const Game* game = nullptr;
    Vector2 mouse{-1000, -1000};
    std::vector<ui::ScreenAction> actions; // non-None actions since the last take()
    bool titleMode = true;

    void drawPlaceholder() {
        ClearBackground(Color{36, 24, 15, 255});
        DrawRectangleGradientV(0, 0, 1600, 200, Color{104, 80, 50, 255}, Color{72, 52, 32, 255});
        for (int x = 0; x < 1600; x += 48) DrawRectangle(x, 0, 2, 200, Color{0, 0, 0, 30});
        DrawRectangle(30, 18, 330, 160, Color{60, 40, 24, 255});
        DrawRectangle(40, 28, 310, 140, ui::pal::ChalkBoard);
        DrawRectangleRounded(ui::layout::TABLE_RIM, 0.04f, 8, ui::pal::WoodDark);
        DrawRectangleRounded(ui::layout::FELT, 0.03f, 8, ui::pal::Felt);
        DrawCircleGradient({800, 560}, 700, Color{60, 130, 80, 120}, Color{0, 0, 0, 0});
        DrawRectangle(392, 728, 816, 172, ui::pal::RackWood);
        for (int i = 0; i < 12; ++i) {
            DrawRectangleRounded({420.f + 60.f * (float)i, 745.f, 44.f, 60.f}, 0.2f, 6, ui::pal::TileFace);
            DrawRectangleRounded({420.f + 60.f * (float)i, 815.f, 44.f, 60.f}, 0.2f, 6, ui::pal::TileFace);
        }
        DrawRectangleRounded({700, 430, 80, 60}, 0.2f, 6, ui::pal::TileBack);
        for (int i = 0; i < 6; ++i)
            DrawCircleGradient({200.f + 240.f * (float)i, 120.f + 30.f * (float)(i % 2)}, 160.f, Color{205, 200, 190, 40},
                               Color{205, 200, 190, 0});
    }

    void frame(float dt = 1.f / 60.f) {
        const ui::ScreenAction a = screens.update(dt, mouse, game);
        if (a != ui::ScreenAction::None) actions.push_back(a);
        BeginDrawing();
        ui::uiBeginFrame();
        BeginTextureMode(rt);
        drawPlaceholder();
        screens.draw(game);
        EndTextureMode();
        ui::uiEndFrame();
        EndDrawing();
    }
    void run(float seconds) {
        const int n = std::max(1, (int)std::lround(seconds * 60.f));
        for (int i = 0; i < n; ++i) frame();
    }
    std::vector<ui::ScreenAction> take() {
        std::vector<ui::ScreenAction> out;
        out.swap(actions);
        return out;
    }
    // A real click: hover, press, release, then one frame for update() to turn it into an action.
    void clickAt(Vector2 p) {
        mouse = p;
        frame();
        inject(EV_MOUSE_DOWN, MOUSE_BUTTON_LEFT);
        frame();
        inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
        frame();
        frame();
    }
    void click(Rectangle r) { clickAt({r.x + r.width * 0.5f, r.y + r.height * 0.5f}); }
    void key(int k) {
        inject(EV_KEY_DOWN, k);
        frame();
        inject(EV_KEY_UP, k);
        frame();
    }
    void wheel(int notches) {
        for (int i = 0; i < std::abs(notches); ++i) {
            inject(EV_WHEEL, 0, notches < 0 ? -1 : 1);
            frame();
        }
    }
    void shot(const std::string& name) {
        Image im = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&im);
        // translucent draws leave partial alpha in the render target; the window's backbuffer ignores it
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        const char* dir = std::getenv("SCREENS_OUT");
        const std::string path = std::string(dir && *dir ? dir : "build/screens") + "/" + name + ".png";
        ExportImage(im, path.c_str());
        UnloadImage(im);
        std::printf("  wrote %s\n", path.c_str());
    }
};

bool has(const std::vector<ui::ScreenAction>& v, ui::ScreenAction a) { return std::find(v.begin(), v.end(), a) != v.end(); }
std::string list(const std::vector<ui::ScreenAction>& v) {
    std::string s;
    for (ui::ScreenAction a : v) s += std::string(s.empty() ? "" : ",") + actionName(a);
    return s.empty() ? "-" : s;
}

// Layout rectangles (mirrors of Screens.cpp's layout; the harness clicks through the real input path).
constexpr Rectangle kTitlePlay{640, 470, 320, 68};
constexpr Rectangle kTitleWatchAi{660, 556, 280, 56};
constexpr Rectangle kTitleRules{660, 626, 280, 56};
constexpr Rectangle kTitleSettings{660, 696, 280, 56};
constexpr Rectangle kSetName{720, 183, 340, 46};
constexpr Vector2 kSetHands7{720.f + 3 * 78.f + 33.f, 270.f};
constexpr Vector2 kSetLevelKurt{720.f + 2 * 158.f + 73.f, 322.f};
constexpr Vector2 kSetMusicToggle{760.f, 716.f};
constexpr Rectangle kSetBack{985, 776, 230, 58};
// Devam, Yapay Zeka Oynasın / Kontrolü Geri Al, Kurallar, Ayarlar, Ana Menü
constexpr Rectangle kPauseBtn[5] = {{650, 328, 300, 58}, {650, 400, 300, 58}, {650, 472, 300, 58}, {650, 544, 300, 58},
                                    {650, 616, 300, 58}};
constexpr Rectangle kConfirmYes{575, 478, 220, 58};
constexpr Rectangle kConfirmNo{805, 478, 220, 58};
constexpr Rectangle kSheetNext{170 + 1260 - 252, 16 + 866 - 68, 216, 54};
constexpr Rectangle kMatchNew{530, 736, 250, 64};
constexpr Rectangle kMatchMenu{820, 736, 250, 64};

void finishHand(Sim& s) { s.play(); }

// Mirror of Screens.cpp's rules tab layout (font Chalk 19, padding 28, gap 8, centred at x = 800, y = 106).
Vector2 ruleTabCenter(int idx) {
    const char* labels[8] = {"Taşlar", "Okey", "Tur", "Perler", "El açmak", "Cezalar", "Puanlama", "Kontroller"};
    float w[8], total = 0.f;
    for (int t = 0; t < 8; ++t) {
        w[t] = ui::measureText(ui::FontId::Chalk, labels[t], 19.f).x + 28.f;
        total += w[t] + (t ? 8.f : 0.f);
    }
    float x = 800.f - total * 0.5f;
    for (int t = 0; t < idx; ++t) x += w[t] + 8.f;
    return {x + w[idx] * 0.5f, 106.f};
}

} // namespace

int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 180, "screens snapshot");
    if (!IsWindowReady()) {
        std::printf("could not open a (hidden) window: no display available?\n");
        return 2;
    }
    SetExitKey(KEY_NULL);
    ui::loadFonts();
    ui::Viewport vp;
    vp.scale = 1.f;
    vp.offset = {0, 0};
    ui::setCurrentViewport(vp);

    Harness H;
    H.rt = LoadRenderTexture(1600, 900);
    SetTextureFilter(H.rt.texture, TEXTURE_FILTER_BILINEAR);
    H.screens.init();
    int sfxCount = 0;
    H.screens.playSfx = [&](ui::Sfx) { ++sfxCount; };
    const std::string sfx;

    // ---------------------------------------------------------------- pick interesting matches
    Pick lose, win, longM;
    for (uint64_t seed = 1; seed <= 400; ++seed) {
        const int a = interest(seed, 3, false);
        if (a > lose.score) lose = {seed, a};
        const int b = interest(seed, 3, true);
        if (b > win.score) win = {seed, b};
    }
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        const int c = interest(seed, 11, false);
        if (c > longM.score) longM = {seed, c};
    }
    std::printf("seeds: lose=%llu (%d) win=%llu (%d) long=%llu (%d)\n", (unsigned long long)lose.seed, lose.score,
                (unsigned long long)win.seed, win.score, (unsigned long long)longM.seed, longM.score);

    // ---------------------------------------------------------------- title
    std::printf("title\n");
    check(H.screens.current() == ui::ScreenId::Title, "starts on Title");
    H.screens.show(ui::ScreenId::Title); // idempotent
    check(H.screens.current() == ui::ScreenId::Title, "show(current) is a no-op");
    check(H.screens.blocksGame(), "Title blocks the game");
    H.run(0.4);
    H.shot("title_swing" + sfx);
    H.run(2.6);
    H.shot("title" + sfx);
    H.mouse = {800, 504};
    H.run(0.2);
    H.shot("title_hover" + sfx);

    // ---------------------------------------------------------------- settings (from title)
    std::printf("settings\n");
    H.click(kTitleSettings);
    check(H.screens.current() == ui::ScreenId::Settings, "Ayarlar opens Settings");
    H.run(0.08);
    H.shot("fade_title_settings" + sfx);
    H.run(0.5);
    H.shot("settings" + sfx);
    H.take();
    H.clickAt(kSetHands7);
    auto acts = H.take();
    check(has(acts, ui::ScreenAction::SettingsChanged), "El sayısı chip returns SettingsChanged (" + list(acts) + ")");
    check(H.screens.settings().numHands == 7, "El sayısı = 7 (got " + std::to_string(H.screens.settings().numHands) + ")");
    H.clickAt(kSetLevelKurt);
    acts = H.take();
    check(H.screens.settings().difficulty == 2 && has(acts, ui::ScreenAction::SettingsChanged), "Bot seviyesi = Kurt");
    H.clickAt(kSetMusicToggle);
    acts = H.take();
    check(!H.screens.settings().music && has(acts, ui::ScreenAction::SettingsChanged), "Radyo toggled off");
    H.click(kSetName);
    H.run(0.3);
    H.shot("settings_edit" + sfx);
    H.key(KEY_BACKSPACE);
    acts = H.take();
    check(H.screens.settings().playerName == "Se" && has(acts, ui::ScreenAction::SettingsChanged),
          "backspace edits the name live (name='" + H.screens.settings().playerName + "')");
    H.key(KEY_ESCAPE); // cancels editing, stays in Settings
    check(H.screens.current() == ui::ScreenId::Settings, "ESC while editing only ends editing");
    check(H.screens.settings().playerName == "Sen", "ESC restores the name ('" + H.screens.settings().playerName + "')");
    H.click(kSetName);
    for (int i = 0; i < 3; ++i) H.key(KEY_BACKSPACE);
    H.key(KEY_ENTER); // empty name -> falls back to the previous one
    check(H.screens.settings().playerName == "Sen", "empty name falls back ('" + H.screens.settings().playerName + "')");
    H.click(kSetName);
    H.key(KEY_BACKSPACE);
    H.clickAt({500, 600}); // click outside commits
    check(H.screens.settings().playerName == "Se", "click outside commits ('" + H.screens.settings().playerName + "')");
    H.screens.settings().playerName = "Sen";
    H.key(KEY_ESCAPE);
    check(H.screens.current() == ui::ScreenId::Title, "ESC closes Settings back to Title");
    H.screens.settings().music = true;
    H.screens.settings().numHands = 3;
    H.screens.settings().difficulty = 1;
    H.run(0.4);

    // ---------------------------------------------------------------- rules
    std::printf("rules\n");
    H.click(kTitleRules);
    check(H.screens.current() == ui::ScreenId::Rules, "Kurallar opens Rules");
    H.run(0.5);
    H.shot("rules_top" + sfx);
    H.wheel(-7);
    H.run(0.6);
    H.shot("rules_scrolled" + sfx);
    H.wheel(-9);
    H.run(0.6);
    H.shot("rules_scrolled2" + sfx);
    H.wheel(-9);
    H.run(0.6);
    H.shot("rules_scrolled3" + sfx);
    H.key(KEY_END);
    H.run(0.8);
    H.shot("rules_bottom" + sfx);
    H.wheel(5);
    H.run(0.8);
    H.shot("rules_scoring" + sfx);
    H.key(KEY_HOME);
    H.run(0.8);
    H.shot("rules_home" + sfx);
    H.clickAt(ruleTabCenter(5)); // "Cezalar" tab
    H.run(0.9);
    H.shot("rules_tab_cezalar" + sfx);
    check(H.screens.current() == ui::ScreenId::Rules, "tab click stays in Rules");
    H.key(KEY_HOME);
    H.run(0.8);
    // click on the scrollbar track below the thumb pages down; dragging the thumb scrolls
    H.clickAt({1248.f, 700.f});
    H.run(0.8);
    H.shot("rules_trackclick" + sfx);
    H.mouse = {1248.f, 300.f};
    H.frame();
    inject(EV_MOUSE_DOWN, MOUSE_BUTTON_LEFT);
    H.frame();
    for (int i = 0; i < 20; ++i) {
        H.mouse.y += 15.f;
        H.frame();
    }
    inject(EV_MOUSE_UP, MOUSE_BUTTON_LEFT);
    H.frame();
    H.run(0.3);
    H.shot("rules_dragged" + sfx);
    H.key(KEY_ESCAPE);
    check(H.screens.current() == ui::ScreenId::Title, "ESC closes Rules back to Title");
    H.run(0.4);

    // ---------------------------------------------------------------- match 1 (human loses)
    std::printf("match (seed %llu)\n", (unsigned long long)lose.seed);
    Sim sim;
    H.click(kTitleRules); // lands on Rules...
    H.take();
    H.clickAt({800.f, 836.f}); // ...an immediate second click where Rules' "Geri" is must not go through
    check(H.screens.current() == ui::ScreenId::Rules, "no click-through while a screen fades in");
    H.run(0.3);
    H.key(KEY_ESCAPE);
    H.run(0.3);
    H.take();
    H.key(KEY_ENTER);
    acts = H.take();
    check(has(acts, ui::ScreenAction::StartMatch) && H.screens.current() == ui::ScreenId::None,
          "Enter on Title starts a match (" + list(acts) + ")");
    H.screens.show(ui::ScreenId::Title);
    H.run(0.4);
    H.take();
    H.click(kTitlePlay);
    acts = H.take();
    check(has(acts, ui::ScreenAction::StartMatch), "Oyna returns StartMatch (" + list(acts) + ")");
    check(H.screens.current() == ui::ScreenId::None && !H.screens.blocksGame(), "Oyna switches to None");
    sim.setup(lose.seed, 3);
    H.game = &sim.game;
    H.titleMode = false;
    H.run(0.4);
    sim.play(6);
    H.key(KEY_ESCAPE);
    check(H.screens.current() == ui::ScreenId::Paused, "ESC in game opens Paused");
    H.run(0.5);
    H.shot("paused" + sfx);
    H.click(kPauseBtn[4]);
    H.run(0.4);
    H.shot("paused_confirm" + sfx);
    H.click(kConfirmNo);
    check(H.screens.current() == ui::ScreenId::Paused, "Vazgeç keeps Paused");
    H.click(kPauseBtn[3]);
    check(H.screens.current() == ui::ScreenId::Settings, "Paused > Ayarlar");
    H.run(0.5);
    H.shot("settings_ingame" + sfx);
    H.click(kSetBack);
    check(H.screens.current() == ui::ScreenId::Paused, "Settings Geri returns to Paused");
    H.run(0.3);
    H.click(kPauseBtn[2]);
    check(H.screens.current() == ui::ScreenId::Rules, "Paused > Kurallar");
    H.key(KEY_ESCAPE);
    check(H.screens.current() == ui::ScreenId::Paused, "Rules ESC returns to Paused");
    H.take();
    H.key(KEY_ESCAPE);
    acts = H.take();
    check(H.screens.current() == ui::ScreenId::None && has(acts, ui::ScreenAction::Resume),
          "ESC in Paused resumes (" + list(acts) + ")");

    for (int hand = 1; hand <= 3; ++hand) {
        finishHand(sim);
        check(sim.game.handState() != okey::HandState::Playing, "hand " + std::to_string(hand) + " ended");
        H.run(0.3);
        H.screens.show(ui::ScreenId::HandSummary);
        H.run(0.1);
        if (hand == 1) H.shot("hand1_writing" + sfx);
        H.run(2.9);
        H.shot("hand" + std::to_string(hand) + sfx);
        const okey::HandResult& r = sim.game.lastHandResult();
        std::printf("  hand %d: winner=%d mult=%d scores=%d,%d,%d,%d pen=%d,%d,%d,%d\n", hand, r.winner, r.multiplier,
                    r.score[0], r.score[1], r.score[2], r.score[3], r.penalties[0], r.penalties[1], r.penalties[2],
                    r.penalties[3]);
        H.take();
        if (hand == 2) H.key(KEY_SPACE);
        else H.click(kSheetNext);
        acts = H.take();
        if (hand < 3) {
            check(has(acts, ui::ScreenAction::NextHand) && H.screens.current() == ui::ScreenId::None,
                  "Sonraki El -> NextHand (" + list(acts) + ")");
            sim.game.startNextHand();
            sim.game.drainEvents();
        } else {
            check(has(acts, ui::ScreenAction::ShowMatchResult) && H.screens.current() == ui::ScreenId::MatchOver,
                  "Sonuçlar -> ShowMatchResult + MatchOver (" + list(acts) + ")");
        }
    }
    H.run(0.6);
    H.shot("match_over_early" + sfx);
    H.run(2.4);
    H.shot("match_over" + sfx);
    H.take();
    H.click(kMatchMenu);
    acts = H.take();
    check(has(acts, ui::ScreenAction::ToTitle) && H.screens.current() == ui::ScreenId::Title, "Ana Menü -> ToTitle");

    // ---------------------------------------------------------------- crafted ×8 finish (debug hooks)
    std::printf("crafted x8 finish\n");
    {
        static Sim s4;
        s4.setup(11, 5, "Şükrü Öğütçü");
        s4.play(9);
        Game& g = s4.game;
        const okey::OkeyInfo ok = g.okey();
        const int okeyId = okey::makeTileId(ok.color, ok.number, 0);
        g.debugSetTurn(0, okey::TurnStage::Play);
        okey::PlayerInfo& me = g.debugPlayer(0);
        me.hand = {okeyId};
        me.opened = true;
        me.openedWithPairs = true;
        me.openValue = 6;
        me.openedTurn = g.turnNumber();
        g.debugPlayer(2).handPenalty += 202;
        const okey::ActionResult ar = g.discard(0, okeyId);
        g.drainEvents();
        check(ar.ok && g.lastHandResult().multiplier == 8 && g.lastHandResult().winner == 0,
              "crafted finish: okeyle + çiftten + elden = x8 (" + ar.error + ")");
        H.game = &g;
        H.screens.show(ui::ScreenId::HandSummary);
        H.run(3.0);
        H.shot("hand_x8" + sfx);
        H.screens.show(ui::ScreenId::None);
        H.run(0.4);
        H.game = nullptr;
        H.screens.show(ui::ScreenId::Title);
        H.run(0.4);
    }

    // ---------------------------------------------------------------- match 2 (human wins), Yeni Oyun
    std::printf("match (seed %llu, human wins)\n", (unsigned long long)win.seed);
    Sim sim2;
    H.game = nullptr;
    H.click(kTitlePlay);
    sim2.setup(win.seed, 3);
    H.game = &sim2.game;
    for (int hand = 1; hand <= 3; ++hand) {
        finishHand(sim2);
        if (hand < 3) {
            sim2.game.startNextHand();
            sim2.game.drainEvents();
        }
    }
    H.screens.show(ui::ScreenId::HandSummary);
    H.run(3.0);
    H.shot("hand3_win" + sfx);
    H.screens.show(ui::ScreenId::MatchOver);
    H.run(3.0);
    H.shot("match_over_win" + sfx);
    H.take();
    H.click(kMatchNew);
    acts = H.take();
    check(has(acts, ui::ScreenAction::StartMatch) && H.screens.current() == ui::ScreenId::None, "Yeni Oyun -> StartMatch");

    // ---------------------------------------------------------------- match 3 (11 hands, long sheet) + confirm quit
    std::printf("match (seed %llu, 11 hands)\n", (unsigned long long)longM.seed);
    Sim sim3;
    sim3.setup(longM.seed, 11, "Batuhan");
    H.game = &sim3.game;
    for (int hand = 1; hand <= 11; ++hand) {
        finishHand(sim3);
        if (hand < 11) {
            sim3.game.startNextHand();
            sim3.game.drainEvents();
        }
    }
    H.screens.show(ui::ScreenId::HandSummary);
    H.run(3.5);
    H.shot("hand11" + sfx);
    H.screens.show(ui::ScreenId::MatchOver);
    H.run(3.0);
    H.shot("match_over_11" + sfx);

    // ---------------------------------------------------------------- the Yapay Zeka mode
    std::printf("Yapay Zeka mode\n");
    H.screens.setAiMode(true);
    H.screens.show(ui::ScreenId::HandSummary);
    H.screens.setAutoAdvance(4.3f);
    H.run(3.5);
    H.shot("hand11_ai" + sfx);
    H.screens.show(ui::ScreenId::MatchOver);
    H.screens.setAutoAdvance(9.6f);
    H.run(3.0);
    H.shot("match_over_11_ai" + sfx);
    H.screens.show(ui::ScreenId::None);
    H.run(0.3);
    H.key(KEY_ESCAPE);
    H.run(0.5);
    H.shot("paused_ai" + sfx);
    H.take();
    H.click(kPauseBtn[1]);
    acts = H.take();
    check(has(acts, ui::ScreenAction::ToggleAiMode) && H.screens.current() == ui::ScreenId::None,
          "Kontrolü Geri Al -> ToggleAiMode + None (" + list(acts) + ")");
    H.screens.setAiMode(false);
    H.key(KEY_ESCAPE);
    H.run(0.4);
    H.shot("paused_ai_off" + sfx);
    H.take();
    H.click(kPauseBtn[1]);
    acts = H.take();
    check(has(acts, ui::ScreenAction::ToggleAiMode) && H.screens.current() == ui::ScreenId::None,
          "Yapay Zeka Oynasın -> ToggleAiMode + None (" + list(acts) + ")");
    H.game = nullptr;
    H.screens.show(ui::ScreenId::Title);
    H.run(0.5);
    H.take();
    H.click(kTitleWatchAi);
    acts = H.take();
    check(has(acts, ui::ScreenAction::StartAiMatch) && H.screens.current() == ui::ScreenId::None,
          "Yapay Zekayı İzle -> StartAiMatch + None (" + list(acts) + ")");
    H.game = &sim3.game;
    H.run(0.3);
    H.screens.show(ui::ScreenId::Paused);
    H.run(0.4);
    H.click(kPauseBtn[4]);
    H.run(0.3);
    H.take();
    H.click(kConfirmYes);
    acts = H.take();
    check(has(acts, ui::ScreenAction::ToTitle) && H.screens.current() == ui::ScreenId::Title,
          "Ana Menü confirm -> ToTitle (" + list(acts) + ")");
    check(sfxCount > 10, "button clicks request Sfx::Button");

    H.screens.shutdown();
    UnloadRenderTexture(H.rt);
    ui::unloadFonts();
    CloseWindow();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
