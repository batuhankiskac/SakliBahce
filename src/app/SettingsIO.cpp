// The settings' lines and a saved match's text (SettingsIO.h): pure functions, no raylib, no files.
#include "app/SettingsIO.h"
#include "app/Rules101.h" // 101 kuralları
#include "ui/SaveFile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace app {
namespace detail {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- the settings file
// One "key=value" line of the settings file (also --set on the command line).
void applySettingLine(ui::Settings& s, const std::string& k, const std::string& v) {
    // (a number out of int's range, or junk, never overflows: clamped first, then each key clamps to its own range)
    const int iv = (int)ui::parseLong(v, -1000000000L, 1000000000L);
    if (k == "el") s.numHands = std::clamp(iv, 1, 11);
    else if (k == "seviye") s.difficulty = std::clamp(iv, 0, 2);
    else if (k == "efekt") s.sfx = iv != 0;
    else if (k == "ortam") s.ambient = iv != 0;
    else if (k == "muzik") s.music = iv != 0;
    else if (k == "ipucu") s.hints = iv != 0;
    else if (k == "rehber") s.guide = iv != 0;
    else if (k == "vakit") s.dayTime = std::clamp(iv, 0, 4);
    else if (k == "mevsim") s.season = std::clamp(iv, 0, 4);
    else if (k == "mekan") s.venue = std::clamp(iv, 0, 2);  // 0 içerisi, 1 bahçe, 2 otomatik
    else if (k == "ozelgun") s.ozelGun = iv != 0;           // Özel günler: açık / kapalı
    else if (k == "konusma") s.voices = iv != 0;
    else if (k == "renkkorlugu") s.colorBlind = iv != 0;
    else if (k == "buyukyazi") s.bigText = iv != 0;
    else if (k == "rehbergoruldu") s.guideSeen = iv & ((1 << (int)ui::GameKind::Count) - 1); // a bit per game
    else if (k == "katlamali") s.katlamali = iv != 0;
    else if (k == "yandanceza") s.yandanCeza = iv != 0;
    // 101 kuralları (app/Rules101.h)
    else if (k == "y101acma") s.y101Acma = iv <= 66 ? 51 : iv <= 91 ? 81 : iv <= 111 ? 101 : 121;
    else if (k == "y101kat") s.y101Kat = std::clamp(iv, 0, 2);
    else if (k == "y101acmayan") s.y101Acmayan = iv >= 303 ? 404 : 202;
    else if (k == "y101okeyceza") s.y101OkeyCeza = iv != 0;
    else if (k == "y101islekceza") s.y101IslekCeza = iv != 0;
    else if (k == "y101geriver") s.y101GeriVer = iv != 0;
    else if (k == "y101bekle") s.y101Bekle = iv != 0;
    else if (k == "oyun") {
        const int g = std::clamp(iv, 0, (int)ui::GameKind::Count - 1);
        s.game = ui::gameAvailable((ui::GameKind)g) ? g : 0;
    }
    else if (k == "okeypuan") s.okeyStart = std::clamp(iv, 1, 99);
    else if (k == "tavla") s.tavlaPoints = std::clamp(iv, 1, 15);
    else if (k == "tavlakatlama") s.tavlaDoubling = iv != 0;
    else if (k == "tavlakatmerli") s.tavlaKatmerli = iv != 0;
    else if (k == "rakip") s.rakip = std::clamp(iv, 0, 3);           // Rakip: every two-player game (0 = an old save)
    else if (k == "tavlarakip") s.tavlaRakip = std::clamp(iv, 0, 3); // (old key: ui::twoPlayerOpponent)
    else if (k == "tavlacesit") s.tavlaCesit = std::clamp(iv, 0, 2); // Tavla çeşidi
    else if (k == "okeyrenkli") s.okeyRenkli = iv != 0;
    else if (k == "batakkoz") s.batakKozKirilmadan = iv != 0;
    else if (k == "king12") s.king12 = iv != 0;
    else if (k == "batakesli") s.batakEsli = iv != 0;
    else if (k == "batakhedef") s.batakTarget = std::clamp(iv, 11, 151);
    else if (k == "pistihedef") s.pistiTarget = std::clamp(iv, 51, 301);
    else if (k == "pistimasa") s.pistiMode = std::clamp(iv, 0, 2);
    else if (k == "damarakip") s.damaRakip = std::clamp(iv, 0, 3); // Dama (old key: ui::twoPlayerOpponent)
    else if (k == "damahedef") s.damaWins = std::clamp(iv, 1, 9);  // Dama
    else if (k == "bezikhedef") s.bezikTarget = iv <= 500 ? 500 : iv >= 1500 ? 1500 : 1000; // Bezik
    else if (k == "konkenacma") s.konkenOpen = iv <= 45 ? 40 : iv >= 61 ? 71 : 51;         // Konken
    else if (k == "konkenyanma") s.konkenLimit = iv <= 125 ? 101 : iv >= 176 ? 201 : 151; // Konken
    else if (k == "konkenbitis") s.konkenLastStanding = iv != 0;                            // Konken bitiş (1 son kalan)
    else if (k == "hiz") s.animSpeed = std::clamp((float)std::strtod(v.c_str(), nullptr), 0.5f, 2.f);
    else if (k == "isim" && !v.empty() && v.size() <= 64) s.playerName = v;
    // Sen: the player's own hands (r3d::PlayerHands)
    else if (k == "eller") s.hands = iv != 0;
    else if (k == "kol") s.kol = std::clamp(iv, 0, 2);
    else if (k == "kolrenk") s.kolRenk = std::clamp(iv, 0, 4);
    else if (k == "ten") s.ten = std::clamp(iv, 0, 3);
    else if (k == "yuzuk") s.yuzuk = iv != 0;
    else if (k == "saat") s.saat = iv != 0;
    else if (k == "tespih") s.tespih = std::clamp(iv, 0, 4);
    else if (k == "bardak") s.bardak = std::clamp(iv, 0, 3);
    else if (k == "sigara") s.sigara = iv != 0;
}

void parseSettingsText(ui::Settings& s, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    const int rakip = s.rakip; // Rakip: a file from before the shared key keeps its tavla (or dama) opponent
    s.rakip = 0;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applySettingLine(s, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    if (s.rakip == 0) s.rakip = s.tavlaRakip >= 1 ? s.tavlaRakip : s.damaRakip >= 1 ? s.damaRakip : rakip;
}

std::string settingsText(const ui::Settings& s) {
    char buf[1100];
    std::snprintf(buf, sizeof buf,
                  "oyun=%d\nel=%d\nseviye=%d\nefekt=%d\nortam=%d\nmuzik=%d\nipucu=%d\n"
                  "katlamali=%d\nyandanceza=%d\nokeypuan=%d\ntavla=%d\nbatakesli=%d\nbatakhedef=%d\npistihedef=%d\n"
                  "pistimasa=%d\ntavlakatlama=%d\ntavlakatmerli=%d\nokeyrenkli=%d\nbatakkoz=%d\nking12=%d\nrehber=%d\nrehbergoruldu=%d\nvakit=%d\nmevsim=%d\nkonusma=%d\nrenkkorlugu=%d\nbuyukyazi=%d\nhiz=%.2f\nisim=%s\n",
                  s.game, s.numHands, s.difficulty, s.sfx ? 1 : 0, s.ambient ? 1 : 0, s.music ? 1 : 0, s.hints ? 1 : 0,
                  s.katlamali ? 1 : 0, s.yandanCeza ? 1 : 0, s.okeyStart, s.tavlaPoints, s.batakEsli ? 1 : 0,
                  s.batakTarget, s.pistiTarget, s.pistiMode, s.tavlaDoubling ? 1 : 0, s.tavlaKatmerli ? 1 : 0, s.okeyRenkli ? 1 : 0,
                  s.batakKozKirilmadan ? 1 : 0, s.king12 ? 1 : 0, s.guide ? 1 : 0, s.guideSeen, s.dayTime, s.season,
                  s.voices ? 1 : 0, s.colorBlind ? 1 : 0, s.bigText ? 1 : 0, (double)s.animSpeed, s.playerName.c_str());
    std::string out = buf;
    out += "mekan=" + std::to_string(s.venue) + "\n";  // Mekân (the garden)
    out += std::string("ozelgun=") + (s.ozelGun ? "1" : "0") + "\n";  // Özel günler
    out += "tavlacesit=" + std::to_string(s.tavlaCesit) + "\n"; // Tavla çeşidi (also a match's save / replay line)
    out += "damahedef=" + std::to_string(s.damaWins) + "\n"; // Dama (the old tavlarakip / damarakip keys are read, not written)
    out += "rakip=" + std::to_string(s.rakip) + "\n"; // Rakip (two-player games; also a match's save / replay line)
    out += "bezikhedef=" + std::to_string(s.bezikTarget) + "\n"; // Bezik
    out += "konkenacma=" + std::to_string(s.konkenOpen) + "\nkonkenyanma=" + std::to_string(s.konkenLimit) + "\n"; // Konken
    {   // 101 kuralları (also a match's save / replay line, so a resumed match keeps its rules)
        char y[200];
        std::snprintf(y, sizeof y, "y101acma=%d\ny101kat=%d\ny101acmayan=%d\ny101okeyceza=%d\ny101islekceza=%d\ny101geriver=%d\ny101bekle=%d\n",
                      s.y101Acma, s.y101Kat, s.y101Acmayan, s.y101OkeyCeza ? 1 : 0, s.y101IslekCeza ? 1 : 0,
                      s.y101GeriVer ? 1 : 0, s.y101Bekle ? 1 : 0);
        out += y;
    }
    out += std::string("konkenbitis=") + (s.konkenLastStanding ? "1" : "0") + "\n";                              // Konken bitiş
    // Sen: the player's own hands
    char sen[160];
    std::snprintf(sen, sizeof sen, "eller=%d\nkol=%d\nkolrenk=%d\nten=%d\nyuzuk=%d\nsaat=%d\ntespih=%d\nbardak=%d\nsigara=%d\n",
                  s.hands ? 1 : 0, s.kol, s.kolRenk, s.ten, s.yuzuk ? 1 : 0, s.saat ? 1 : 0, s.tespih, s.bardak, s.sigara ? 1 : 0);
    out += sen;
    return out;
}

bool settingValue(const ui::Settings& s, const std::string& key, std::string& out) {
    std::istringstream in(settingsText(s));
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line.compare(0, eq, key) == 0 && eq == key.size()) {
            out = line.substr(eq + 1);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- a saved match
bool parseSave(const std::string& text, SavedMatch& sv) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2 || line[1] != ' ') continue;
        const std::string v = line.substr(2);
        switch (line[0]) {
        case 'g': sv.game = (int)ui::parseLong(v, -1, 1000, -1); break;
        case 's': sv.seed = std::strtoull(v.c_str(), nullptr, 10); break;
        case 'y': sv.aiTouched = v == "1"; break;
        case 'l': sv.label = v; break;
        case 'd': sv.date = v; break;
        case 'r': sv.result = v; break;
        case 'k': sv.settings.push_back(v); break;
        case 'a': sv.actions.push_back(v); break;
        default: break;
        }
    }
    return sv.game >= 0 && sv.game < (int)ui::GameKind::Count && ui::gameAvailable((ui::GameKind)sv.game);
}

void keepPlayerPrefs(ui::Settings& rules, const ui::Settings& mine) {
    rules.sfx = mine.sfx, rules.ambient = mine.ambient, rules.music = mine.music, rules.voices = mine.voices;
    rules.animSpeed = mine.animSpeed, rules.hints = mine.hints, rules.playerName = mine.playerName;
    rules.guide = mine.guide, rules.guideSeen = mine.guideSeen;
    rules.colorBlind = mine.colorBlind, rules.bigText = mine.bigText;
    rules.dayTime = mine.dayTime, rules.season = mine.season, rules.venue = mine.venue, rules.ozelGun = mine.ozelGun;
    // Sen: the player's own hands
    rules.hands = mine.hands, rules.kol = mine.kol, rules.kolRenk = mine.kolRenk, rules.ten = mine.ten;
    rules.yuzuk = mine.yuzuk, rules.saat = mine.saat, rules.tespih = mine.tespih, rules.bardak = mine.bardak;
    rules.sigara = mine.sigara;
}

ui::Settings rulesFromSave(const SavedMatch& sv, const ui::Settings& mine) {
    ui::Settings rules = mine;
    rules.tavlaCesit = 0; // Tavla çeşidi: a save from before the çeşitler has no line for it, it was klasik
    rules.konkenLastStanding = false; // Konken bitiş: a save from before it has no line for it, it was "ilk yanan"
    reset101Rules(rules); // 101 kuralları: a save from before them has no lines for them, it was played by the defaults
    rules.rakip = rules.tavlaRakip = rules.damaRakip = 0; // Rakip: only what the save says (ui::twoPlayerOpponent)
    for (const std::string& kv : sv.settings) {
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) applySettingLine(rules, trim(kv.substr(0, eq)), trim(kv.substr(eq + 1)));
    }
    keepPlayerPrefs(rules, mine); // the match's rules and level; the player's own preferences stay
    rules.game = sv.game;
    return rules;
}

} // namespace detail
} // namespace app
