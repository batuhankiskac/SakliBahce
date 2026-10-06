#pragma once
// Full-screen and overlay screens: title/menu, settings, rules (nasıl oynanır), pause, end-of-hand score
// sheet (hesap kağıdı), match over. PUBLIC API FROZEN. Implementation: src/ui/Screens.cpp owned by the
// screens owner, who defines struct Screens::Impl.
#include "core/Game.h"
#include "ui/Audio.h"
#include "ui/Common.h"
#include "ui/Stats.h"
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
    GameSelect,   // Title "Oyna": which game (101, Eşli 101, Okey, Tavla, ...)
    Stats,        // Title "İstatistik": the player's record and rank (kahvehane defteri)
    Guide,        // a game's short guide card at its first match (setGuide), "Anladım" -> None
    Replays,      // İstatistik "Tekrarlar": the last finished matches, to watch again
    Analysis      // "Hatalarım": the player's most expensive mistakes of a match (setAnalysis)
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
    bool guide = true;         // a game's guide card at its first match (switching it on shows them all again)
    int guideSeen = 0;         // bit per GameKind: its guide was shown
    bool katlamali = false;    // Katlamalı oyun (okey::RulesConfig::katlamali), from the next match
    bool yandanCeza = true;    // yandan alıp açma cezası (okey::RulesConfig::leftOpenPenalty), next match
    int okeyStart = 20;        // klasik okey: starting points (6, 12, 20), counted down
    bool okeyRenkli = false;   // klasik okey: renkli okey (a red / black gösterge doubles the hand)
    bool batakKozKirilmadan = true; // batak: no koz lead before a koz has been played
    bool king12 = false;       // king: the short game, 12 hands (each chooses 1 koz and 2 cezas)
    bool tavlaKatmerli = false; // tavla: katmerli mars counts 3
    bool batakEsli = false;    // batak: partners across (eşli ihaleli batak)
    int batakTarget = 51;      // batak: the match is won at this score (31, 51, 71)
    int tavlaPoints = 5;       // tavla: the match goes to this many points (3, 5, 7)
    bool tavlaDoubling = false; // tavla: katlama zarı (doubling cube) in play
    int tavlaRakip = 2;        // tavla: who sits across at the tavla table (seat 1 Hacı Rıza, 2 Kel Mahmut, 3 Emekli Nuri)
    int pistiTarget = 101;     // pişti: the match is won at this score (101, 151)
    int pistiMode = 0;         // pişti: 0 four players, 1 eşli, 2 two players (you and Kel Mahmut)
    // ---- atmosphere / accessibility
    int dayTime = 0;           // 0 otomatik (the computer's clock), 1 sabah, 2 öğle, 3 akşam, 4 gece
    int season = 0;            // 0 otomatik (the date), 1 ilkbahar, 2 yaz, 3 sonbahar, 4 kış
    bool voices = true;        // the regulars murmur when they speak (Audio)
    bool colorBlind = false;   // tiles / cards / checkers with shapes and colour-blind friendly colours
    bool bigText = false;      // larger HUD and speech text
    std::string playerName = "Sen";
};

// One finished match kept for watching again (Replays screen).
struct ReplayEntry {
    std::string game;    // "Batak"
    std::string date;    // "06.10.2026 21:40"
    std::string result;  // "Kazandın", "Kel Mahmut kazandı"
    int actions = 0;
};

// One mistake of the player as the analysis found it (Analysis screen).
struct MistakeView {
    std::string when;    // "3. el, 14. tur"
    std::string played;  // "Kırmızı 12'yi attın"
    std::string better;  // "Kurt Sarı 3'ü atardı"
    std::string why;     // "Hacı Rıza onu alıp açtı" (may be empty)
    std::string cost;    // "-38 puan"
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
    ToggleAiMode,     // Paused: "Yapay Zeka Oynasın" / "Kontrolü Geri Al" (the menu closes, the game resumes)
    ResumeSaved,      // Title: "Devam Et" — the match left unfinished last time
    WatchReplay,      // Replays: watch chosenReplay()
    AnalyzeReplay     // Replays: "Analiz" of chosenReplay() (App computes, then setAnalysis)
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
    // The record shown by the İstatistik screen (App owns it; null: an empty book).
    void setStats(const StatsBook* stats);
    // A match left unfinished can be resumed: the title shows "Devam Et" with `label` (e.g. "Batak · 3. el").
    void setResumable(bool on, const std::string& label = {});
    // The guide card's text (show(ScreenId::Guide) after it): a title and a few short lines.
    void setGuide(const std::string& title, const std::vector<std::string>& lines);
    // The finished matches to list on the Replays screen (newest first) and the one picked for WatchReplay.
    void setReplays(const std::vector<ReplayEntry>& rows);
    int chosenReplay() const;
    // "Hatalarım": the MatchOver screen offers it while `available`; the Analysis screen shows `rows` once `ready`
    // (until then "Kurt maçı inceliyor…"). `title` e.g. "Batak · 06.10.2026".
    void setAnalysis(bool available, bool ready, const std::string& title, const std::vector<MistakeView>& rows);
    std::function<void(Sfx)> playSfx; // set by App

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ui
