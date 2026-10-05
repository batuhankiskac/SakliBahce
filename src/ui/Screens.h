#pragma once
// Full-screen and overlay screens: title/menu, settings, rules (nasıl oynanır), pause, end-of-hand score
// sheet (hesap kağıdı), match over. PUBLIC API FROZEN. Implementation: src/ui/Screens.cpp owned by the
// screens owner, who defines struct Screens::Impl.
#include "core/Game.h"
#include "ui/Audio.h"
#include "ui/Common.h"
#include <functional>
#include <string>

namespace ui {

// The games of the kahvehane. The okey games share the okey engine and table; the others have their own.
enum class GameKind { Yuzbir = 0, YuzbirEsli, Okey, Tavla, Pisti, Batak, King, Count };
struct GameInfo {
    const char* name;      // "101", "Eşli 101", "Okey", ...
    const char* neon;      // the title sign's neon line: "101", "OKEY", "TAVLA", ...
    const char* players;   // "4 kişi", "2 kişi", "4 kişi · eşli"
    const char* blurb;     // one line for the game list
};
const GameInfo& gameInfo(GameKind k);
bool gameAvailable(GameKind k);   // playable in this build (the others show "Yakında")
inline bool isOkeyFamily(GameKind k) { return k == GameKind::Yuzbir || k == GameKind::YuzbirEsli || k == GameKind::Okey; }

enum class ScreenId {
    Title,        // main menu over the (title-mode) scene
    Settings,     // el sayısı, bot seviyesi, ses/müzik/ortam, animasyon hızı, oyuncu adı
    Rules,        // how to play 101 (Turkish), scrollable
    None,         // in game, no overlay
    Paused,       // in-game menu: devam, kurallar, ayarlar, ana menü
    HandSummary,  // after each hand: this hand's result + the running score sheet
    MatchOver,    // final standings, winner celebration, "Yeni Oyun" / "Ana Menü"
    GameSelect    // Title "Oyna": which game (101, Eşli 101, Okey, Tavla, ...)
};

struct Settings {
    int game = 0;              // GameKind of the next match (the last one picked)
    int numHands = 5;          // 1, 3, 5, 7, 9, 11
    int difficulty = 1;        // 0 Acemi, 1 Usta, 2 Kurt  (okey::BotLevel)
    bool sfx = true;
    bool ambient = true;
    bool music = true;
    float animSpeed = 1.f;     // 0.5 .. 2
    bool hints = true;
    bool katlamali = false;    // Katlamalı oyun (okey::RulesConfig::katlamali), from the next match
    bool yandanCeza = true;    // yandan alıp açma cezası (okey::RulesConfig::leftOpenPenalty), next match
    int okeyStart = 20;        // klasik okey: starting points (6, 12, 20), counted down
    bool batakEsli = false;    // batak: partners across (eşli ihaleli batak)
    int batakTarget = 51;      // batak: the match is won at this score (31, 51, 71)
    int tavlaPoints = 5;       // tavla: the match goes to this many points (3, 5, 7)
    int pistiTarget = 101;     // pişti: the match is won at this score (101, 151)
    int pistiMode = 0;         // pişti: 0 four players, 1 eşli, 2 two players (you and Kel Mahmut)
    std::string playerName = "Sen";
};

// The score sheet and the final standings of the games that are not okey (tavla, the card games): the game fills
// this in and the screens draw it in the same hand-written look as the okey sheet.
struct SheetModel {
    // ---- the hand's sheet (HandSummary)
    std::string title;                             // "El 3 Sonucu"
    std::string corner;                            // red pencil top left: "batak: 51'e kadar"
    std::string headline;                          // "Kel Mahmut ihaleyi 7 ile aldı, battı!"
    std::string tagline;                           // smaller line under it ("koz: Maça")
    bool taglineRed = false;
    std::vector<std::string> columns;              // 2..4 names (the human's says "Sen")
    int humanCol = 0;
    int starCol = -1;                              // this hand's winner gets a star by the name
    std::vector<std::string> rowLabels;            // this hand's rows ("İhale", "Aldığı", "Bu el")
    std::vector<std::vector<std::string>> cells;   // [row][col]
    std::vector<std::vector<bool>> red;            // [row][col] written in red pencil (optional)
    std::vector<std::string> historyLabels;        // "1. el", "Rıfkı (Nuri)"
    std::vector<std::vector<std::string>> history; // [hand][col]
    std::vector<std::string> totals;               // per column ("Toplam")
    std::string totalLabel = "Toplam";
    std::vector<int> leaders;                      // columns circled in red
    std::string note;                              // red note at the bottom left
    bool last = false;                             // the button says "Sonuçlar" (then the final standings)
    // ---- the final standings (MatchOver)
    std::string matchHeader = "Maç Bitti";
    std::string playedLine;                        // "9 el oynandı"
    std::string resultTitle;                       // "Kazandın! Çaylar onlardan!"
    std::string resultSub;
    bool humanWon = false;
    std::vector<int> ranking;                      // column order, best first
    std::vector<int> rank;                         // place shown for each row of `ranking` (ties share)
    std::vector<std::string> rankSub;              // per column, small line ("El puanları: ...")
};

enum class ScreenAction {
    None,
    StartMatch,       // GameSelect / MatchOver "Yeni Oyun": a match of settings().game
    Resume,           // Paused: "Devam"
    NextHand,         // HandSummary: "Sonraki El"
    ShowMatchResult,  // HandSummary of the last hand: "Sonuçlar"
    ToTitle,          // Paused/MatchOver: "Ana Menü"
    Quit,             // Title: "Çıkış"
    SettingsChanged,  // any setting changed (App re-applies audio/anim settings)
    StartAiMatch,     // Title: "Yapay Zekayı İzle" — a new match with the Yapay Zeka mode on
    ToggleAiMode      // Paused: "Yapay Zeka Oynasın" / "Kontrolü Geri Al" (the menu closes, the game resumes)
};

class Screens {
public:
    Screens();
    ~Screens();
    Screens(const Screens&) = delete;
    Screens& operator=(const Screens&) = delete;

    void init();
    void shutdown();
    void show(ScreenId id);           // Settings/Rules remember where to go back to
    ScreenId current() const;
    bool blocksGame() const;          // true for every screen except None
    // mouse: ui::virtualMouse(). `game` may be null on the title screen. ESC closes Rules/Settings,
    // toggles Paused <-> None.
    ScreenAction update(float dt, Vector2 mouse, const okey::Game* game);
    void draw(const okey::Game* game);
    Settings& settings();
    // The Yapay Zeka mode (an AI plays the human's seat): the pause menu offers the way back, the score sheet
    // and the final standings speak about the AI. Set by App whenever the mode changes.
    void setAiMode(bool on);
    // The score sheet / final standings will press their main button by themselves in this many seconds (the
    // Yapay Zeka mode and --autoplay move on alone); the button shows the countdown. < 0: no countdown. App sets
    // it every frame; a screen change clears it.
    void setAutoAdvance(float secondsLeft);
    // The other games' score sheet: while set, HandSummary / MatchOver draw this instead of the okey game (App sets it
    // whenever a sheet is shown; clearSheet() for the okey games).
    void setSheet(const SheetModel& m);
    void clearSheet();
    std::function<void(Sfx)> playSfx; // set by App

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ui
