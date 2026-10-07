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

// One "key=value" line of the settings file (also --set on the command line).
void applySettingLine(ui::Settings& s, const std::string& k, const std::string& v) {
    const int iv = std::atoi(v.c_str());
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
    else if (k == "rehbergoruldu") s.guideSeen = iv;
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
    else if (k == "oyun") s.game = ui::gameAvailable((ui::GameKind)std::clamp(iv, 0, (int)ui::GameKind::Count - 1)) ? std::clamp(iv, 0, (int)ui::GameKind::Count - 1) : 0;
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
    else if (k == "hiz") s.animSpeed = std::clamp((float)std::atof(v.c_str()), 0.5f, 2.f);
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

void loadSettings(ui::Settings& s) {
    std::string path = settingsPath();
    if (path.empty()) return;
    if (!FileExists(path.c_str())) path = legacySettingsPath();
    if (!FileExists(path.c_str())) return;
    char* text = LoadFileText(path.c_str());
    if (!text) return;
    std::istringstream in(text);
    UnloadFileText(text);
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
                  "pistimasa=%d\ntavlakatlama=%d\ntavlakatmerli=%d\ntavlarakip=%d\nokeyrenkli=%d\nbatakkoz=%d\nking12=%d\nrehber=%d\nrehbergoruldu=%d\nvakit=%d\nmevsim=%d\nkonusma=%d\nrenkkorlugu=%d\nbuyukyazi=%d\nhiz=%.2f\nisim=%s\n",
                  s.game, s.numHands, s.difficulty, s.sfx ? 1 : 0, s.ambient ? 1 : 0, s.music ? 1 : 0, s.hints ? 1 : 0,
                  s.katlamali ? 1 : 0, s.yandanCeza ? 1 : 0, s.okeyStart, s.tavlaPoints, s.batakEsli ? 1 : 0,
                  s.batakTarget, s.pistiTarget, s.pistiMode, s.tavlaDoubling ? 1 : 0, s.tavlaKatmerli ? 1 : 0, s.tavlaRakip, s.okeyRenkli ? 1 : 0,
                  s.batakKozKirilmadan ? 1 : 0, s.king12 ? 1 : 0, s.guide ? 1 : 0, s.guideSeen, s.dayTime, s.season,
                  s.voices ? 1 : 0, s.colorBlind ? 1 : 0, s.bigText ? 1 : 0, (double)s.animSpeed, s.playerName.c_str());
    std::string out = buf;
    out += "mekan=" + std::to_string(s.venue) + "\n";  // Mekân (the garden)
    out += std::string("ozelgun=") + (s.ozelGun ? "1" : "0") + "\n";  // Özel günler
    out += "tavlacesit=" + std::to_string(s.tavlaCesit) + "\n"; // Tavla çeşidi (also a match's save / replay line)
    out += "damarakip=" + std::to_string(s.damaRakip) + "\ndamahedef=" + std::to_string(s.damaWins) + "\n"; // Dama
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

bool ensureDirOf(const std::string& path) {
    const std::string dir = path.substr(0, path.find_last_of('/'));
    return DirectoryExists(dir.c_str()) || MakeDirectory(dir.c_str()) == 0;
}

void saveSettings(const ui::Settings& s) {
    const std::string path = settingsPath();
    if (path.empty() || !ensureDirOf(path)) return;
    SaveFileText(path.c_str(), ("# SaklıBahçe ayarları\n" + settingsText(s)).c_str());
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
    if (flow_ == Flow::Title) {
        characters_.setNames(names());
        return;
    }
    cancelThink();  // nothing may touch a bot while it is thinking on the worker thread
    for (int s = 1; s < 4; ++s)
        if (bots_[s]) bots_[s]->setLevel((okey::BotLevel)std::clamp(st.difficulty, 0, 2));
    if (other_) {
        other_->setLevel(std::clamp(st.difficulty, 0, 2));
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
    saveSettings(s);
}

} // namespace detail
} // namespace app
