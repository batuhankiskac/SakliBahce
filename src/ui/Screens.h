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

enum class ScreenId {
    Title,        // main menu over the (title-mode) scene
    Settings,     // el sayısı, bot seviyesi, ses/müzik/ortam, animasyon hızı, oyuncu adı
    Rules,        // how to play 101 (Turkish), scrollable
    None,         // in game, no overlay
    Paused,       // in-game menu: devam, kurallar, ayarlar, ana menü
    HandSummary,  // after each hand: this hand's result + the running score sheet
    MatchOver     // final standings, winner celebration, "Yeni Oyun" / "Ana Menü"
};

struct Settings {
    int numHands = 5;          // 1, 3, 5, 7, 9, 11
    int difficulty = 1;        // 0 Acemi, 1 Usta, 2 Kurt  (okey::BotLevel)
    bool sfx = true;
    bool ambient = true;
    bool music = true;
    float animSpeed = 1.f;     // 0.5 .. 2
    bool hints = true;
    bool katlamali = false;    // Katlamalı oyun (okey::RulesConfig::katlamali), from the next match
    bool yandanCeza = true;    // yandan alıp açma cezası (okey::RulesConfig::leftOpenPenalty), next match
    std::string playerName = "Sen";
};

enum class ScreenAction {
    None,
    StartMatch,       // Title/MatchOver: "Yeni Oyun"
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
    std::function<void(Sfx)> playSfx; // set by App

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ui
