#pragma once
// The settings' "key=value" lines and a saved match's text (kayit.txt, tekrarlar), as pure functions: no raylib, no
// files. App reads and writes the files (AppSettings.cpp, AppRecord.cpp); tests/test_settings.cpp checks these.
#include "ui/Screens.h"

#include <cstdint>
#include <string>
#include <vector>

namespace app {
namespace detail {

std::string trim(const std::string& s);

// ---- the settings file (ayarlar.txt; also --set and a saved match's "k" lines)
// One line: an unknown key changes nothing; a value is clamped to the setting's range (junk reads as 0).
void applySettingLine(ui::Settings& s, const std::string& k, const std::string& v);
// A whole file: its lines, then Rakip's migration (a file from before `rakip` keeps its tavla, or dama, opponent).
void parseSettingsText(ui::Settings& s, const std::string& text);
// Every setting as "key=value\n" lines (what the file and a saved match hold).
std::string settingsText(const ui::Settings& s);
// The value `key` has in settingsText(s); false when settingsText writes no such key.
bool settingValue(const ui::Settings& s, const std::string& key, std::string& out);

// ---- a saved match (kayit.txt) or a finished one (tekrarlar/*.txt)
struct SavedMatch {
    int game = -1;
    uint64_t seed = 0;
    bool aiTouched = false;
    std::string label;                 // "101 · 3. el" for the title
    std::string date, result;          // tekrarlar: when it was played, how it ended
    std::vector<std::string> settings; // "key=value"
    std::vector<std::string> actions;  // the engine's own lines
};
// Reads the text's lines into `sv`; false unless it names a game of this build (ui::gameAvailable).
bool parseSave(const std::string& text, SavedMatch& sv);
// The player's own preferences (sound, speed, name, the room, accessibility, the hands): a resumed / replayed match
// takes its rules from the save and these from the player.
void keepPlayerPrefs(ui::Settings& rules, const ui::Settings& mine);
// The settings a saved match is played (resumed, replayed, analysed) with: the rules and level from its lines, each
// rule a save from before it has no line for at its old value, and the player's own preferences from `mine`.
ui::Settings rulesFromSave(const SavedMatch& sv, const ui::Settings& mine);

} // namespace detail
} // namespace app
