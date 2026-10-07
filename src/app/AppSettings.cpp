// App: the settings file and the command line's session-only overrides (AppInternal.h).
#include "app/AppInternal.h"

namespace app {
namespace detail {

// ---------------------------------------------------------------- settings file (interactive runs only)
std::string supportDir() {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/Library/Application Support/";
#else
    // XDG: $XDG_DATA_HOME, else ~/.local/share
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg == '/') return std::string(xdg) + "/";
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.local/share/";
#endif
}
std::string statsPath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/istatistik.txt";
}
std::string memoryPath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/hafiza.txt";
}
std::string settingsPath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/ayarlar.txt";
}
// The game was called "Kıraathane 101" before: its settings are read until the new file has been written once.
std::string legacySettingsPath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "Kiraathane101/ayarlar.txt";
}

void loadSettings(ui::Settings& s) {
    std::string path = settingsPath();
    if (path.empty()) return;
    if (!FileExists(path.c_str())) path = legacySettingsPath();
    std::string text;
    if (!ui::readFileText(path, text)) return;
    parseSettingsText(s, text);
}

bool ensureDirOf(const std::string& path) {
    const std::string dir = path.substr(0, path.find_last_of('/'));
    return DirectoryExists(dir.c_str()) || MakeDirectory(dir.c_str()) == 0;
}

void saveSettings(const ui::Settings& s) {
    const std::string path = settingsPath();
    if (path.empty() || !ensureDirOf(path)) return;
    ui::writeFileAtomic(path, "# SaklıBahçe ayarları\n" + settingsText(s));
}

void App::applySettings() {
    const ui::Settings& st = screens_.settings();
    if (audioOn_) {
        audio_.setSfxEnabled(st.sfx);
        audio_.setAmbientEnabled(st.ambient);
        audio_.setMusicEnabled(st.music);
        audio_.setVoicesEnabled(st.voices);
    }
    table_.setAnimationSpeed(st.animSpeed);
    characters_.setAnimationSpeed(st.animSpeed);
    table_.setHints(st.hints);
    ui::setHudTextScale(st.bigText ? 1.25f : 1.f);
    ui::tilegfx::setColorBlind(st.colorBlind);  // repainted on the next refresh()
    ui::cardgfx::setFourColour(st.colorBlind);
    {   // Sen: the player's own hands (Ayarlar "Sen")
        r3d::PlayerLook pl;
        pl.kol = st.kol;
        pl.kolRenk = st.kolRenk;
        pl.ten = st.ten;
        pl.yuzuk = st.yuzuk;
        pl.saat = st.saat;
        pl.tespih = st.tespih;
        pl.bardak = st.bardak;
        pl.sigara = st.sigara;
        hands_.setLook(pl);
        hands_.setEnabled(st.hands);
        hands_.setAnimationSpeed(st.animSpeed);
    }
    // The level: only a change the player makes now re-levels the match in play (a resumed match keeps the level it
    // was saved with while other settings are changed), and the match's own record follows it.
    const bool levelChanged = shownDifficulty_ >= 0 && st.difficulty != shownDifficulty_;
    shownDifficulty_ = st.difficulty;
    if (flow_ == Flow::Title) {
        characters_.setNames(names());
        return;
    }
    cancelThink();  // nothing may touch a bot while it is thinking on the worker thread
    if (levelChanged) {
        matchSettings_.difficulty = std::clamp(st.difficulty, 0, 2);
        for (int s = 1; s < 4; ++s)
            if (bots_[s]) bots_[s]->setLevel((okey::BotLevel)matchSettings_.difficulty);
        if (other_) other_->setLevel(matchSettings_.difficulty);
    }
    if (other_) {
        other_->setAnimationSpeed(st.animSpeed);
        other_->setHints(st.hints);
    }
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
    o.game = o.game && st.game == cliSettings_.game;
    o.sets.erase(std::remove_if(o.sets.begin(), o.sets.end(),
                                [&](const std::string& k) {
                                    std::string now, cli;
                                    return !settingValue(st, k, now) || !settingValue(cliSettings_, k, cli) || now != cli;
                                }),
                 o.sets.end());
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
    if (overridden_.game) s.game = loadedSettings_.game;
    for (const std::string& k : overridden_.sets) { // --set: the loaded value of that key
        std::string v;
        if (settingValue(loadedSettings_, k, v)) applySettingLine(s, k, v);
    }
    saveSettings(s);
}

} // namespace detail
} // namespace app
