// Tests for the settings' lines and a saved match's rules (app/SettingsIO.h): applySettingLine's clamping and its
// junk / overflow handling, settingsText -> parseSettingsText round trip, the legacy keys (tavlarakip / damarakip:
// read, never written) and Rakip's migration, a save's text (parseSave) and the rules a save is resumed / replayed /
// analysed with (rulesFromSave: the save's rules and level, the old defaults for rules a save has no line for, the
// player's own preferences).
// SettingsIO is pure logic: it is compiled into this test directly (the Makefile links tests with the core objects
// only); ui::gameAvailable (Screens.cpp, raylib) is stubbed below.
// Build: make BUILD=build/app build/app/test_settings
#include "app/SettingsIO.cpp"

#include <cstdio>
#include <string>

namespace ui {
bool gameAvailable(GameKind k) { return k != GameKind::Count; } // (this build: every game is on)
} // namespace ui

using namespace app::detail;

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_failures;                                                                                              \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                                          \
        }                                                                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        const auto va_ = (a);                                                                                          \
        const auto vb_ = (b);                                                                                          \
        if (!(va_ == vb_)) {                                                                                           \
            ++g_failures;                                                                                              \
            std::printf("%s:%d: CHECK_EQ failed: %s (%lld vs %lld)\n", __FILE__, __LINE__, #a " == " #b,               \
                        (long long)va_, (long long)vb_);                                                               \
        }                                                                                                              \
    } while (0)

bool hasLine(const std::string& text, const std::string& line) {
    return ("\n" + text).find("\n" + line + "\n") != std::string::npos;
}

void testLines() {
    ui::Settings s;
    applySettingLine(s, "el", "7");
    CHECK_EQ(s.numHands, 7);
    applySettingLine(s, "el", "99");
    CHECK_EQ(s.numHands, 11);
    applySettingLine(s, "el", "-5");
    CHECK_EQ(s.numHands, 1);
    applySettingLine(s, "seviye", "99999999999999999999"); // (atoi: undefined; now clamped)
    CHECK_EQ(s.difficulty, 2);
    applySettingLine(s, "seviye", "-99999999999999999999");
    CHECK_EQ(s.difficulty, 0);
    applySettingLine(s, "okeypuan", "abc");                 // junk reads as 0, then the key's own range
    CHECK_EQ(s.okeyStart, 1);
    applySettingLine(s, "rehbergoruldu", "999999999");      // a bit per game, nothing more
    CHECK(s.guideSeen >= 0 && s.guideSeen < (1 << (int)ui::GameKind::Count));
    applySettingLine(s, "rehbergoruldu", "5");
    CHECK_EQ(s.guideSeen, 5);
    applySettingLine(s, "hiz", "7.5");
    CHECK(s.animSpeed == 2.f);
    applySettingLine(s, "hiz", "0.75");
    CHECK(s.animSpeed == 0.75f);
    applySettingLine(s, "y101acma", "80");
    CHECK_EQ(s.y101Acma, 81);
    applySettingLine(s, "oyun", "42");
    CHECK_EQ(s.game, (int)ui::GameKind::Count - 1);
    const std::string name = s.playerName;
    applySettingLine(s, "isim", "");
    CHECK(s.playerName == name);
    applySettingLine(s, "isim", "Batu");
    CHECK(s.playerName == "Batu");
    const ui::Settings before = s;
    applySettingLine(s, "boylebiranahtaryok", "3");         // an unknown key changes nothing
    CHECK(settingsText(s) == settingsText(before));
}

void testRoundTrip() {
    ui::Settings s;
    s.game = 7, s.numHands = 9, s.difficulty = 2, s.sfx = false, s.music = false, s.animSpeed = 1.5f;
    s.hints = false, s.guide = false, s.guideSeen = 0x55, s.katlamali = true, s.yandanCeza = false;
    s.y101Acma = 121, s.y101Kat = 2, s.y101Acmayan = 404, s.y101OkeyCeza = false, s.y101GeriVer = true;
    s.okeyStart = 12, s.okeyRenkli = true, s.tavlaPoints = 7, s.tavlaDoubling = true, s.tavlaKatmerli = true;
    s.rakip = 3, s.tavlaCesit = 2, s.batakEsli = true, s.batakTarget = 71, s.pistiTarget = 151, s.pistiMode = 2;
    s.damaWins = 5, s.bezikTarget = 1500, s.konkenOpen = 71, s.konkenLimit = 201, s.konkenLastStanding = false;
    s.dayTime = 3, s.season = 4, s.venue = 1, s.ozelGun = false, s.voices = false, s.colorBlind = true;
    s.bigText = true, s.playerName = "Batuhan", s.hands = false, s.kol = 2, s.kolRenk = 3, s.ten = 0;
    s.yuzuk = true, s.saat = false, s.tespih = 4, s.bardak = 2, s.sigara = true;
    const std::string text = settingsText(s);
    ui::Settings r;
    parseSettingsText(r, text);
    CHECK(settingsText(r) == text);
    CHECK_EQ(r.rakip, 3);
    CHECK(r.playerName == "Batuhan");
    // the legacy keys are no longer written ...
    CHECK(text.find("tavlarakip=") == std::string::npos);
    CHECK(text.find("damarakip=") == std::string::npos);
    CHECK(hasLine(text, "rakip=3"));
    std::string v;
    CHECK(settingValue(s, "rakip", v) && v == "3");
    CHECK(settingValue(s, "el", v) && v == "9");
    CHECK(!settingValue(s, "tavlarakip", v));
    CHECK(!settingValue(s, "rak", v));
}

void testLegacy() {
    // a file from before `rakip`: its tavla opponent becomes everyone's
    ui::Settings s;
    parseSettingsText(s, "# eski\nel=3\ntavlarakip=1\n");
    CHECK_EQ(s.rakip, 1);
    CHECK_EQ(s.tavlaRakip, 1);
    CHECK_EQ(ui::twoPlayerOpponent(s, ui::GameKind::Tavla), 1);
    ui::Settings d;
    parseSettingsText(d, "damarakip=3\n");
    CHECK_EQ(d.rakip, 3);
    ui::Settings both;
    parseSettingsText(both, "rakip=2\ntavlarakip=1\n"); // the shared key wins
    CHECK_EQ(both.rakip, 2);
    ui::Settings none;
    parseSettingsText(none, "el=5\n"); // no key at all: the default stays
    CHECK_EQ(none.rakip, ui::Settings{}.rakip);
    ui::Settings spaced;
    parseSettingsText(spaced, "  el =  3 \r\nseviye=0\r\n");
    CHECK_EQ(spaced.numHands, 3);
    CHECK_EQ(spaced.difficulty, 0);
}

void testSave() {
    const std::string text = "# SaklıBahçe: yarım kalan maç\n"
                             "g 3\ns 12345678901234\ny 1\nl Tavla  \xC2\xB7  2 - 1\n"
                             "k tavla=3\nk tavlacesit=1\nk seviye=0\nk efekt=0\nk hiz=2.00\nk isim=Eski\nk rakip=1\n"
                             "a R 3 5\na M 1 2\n";
    SavedMatch sv;
    CHECK(parseSave(text, sv));
    CHECK_EQ(sv.game, 3);
    CHECK(sv.seed == 12345678901234ull);
    CHECK(sv.aiTouched);
    CHECK_EQ((int)sv.settings.size(), 7);
    CHECK_EQ((int)sv.actions.size(), 2);
    SavedMatch bad;
    CHECK(!parseSave("g 99\ns 1\n", bad));
    SavedMatch none;
    CHECK(!parseSave("s 1\n", none));
    SavedMatch crlf;
    CHECK(parseSave("g 0\r\ns 7\r\na X\r\n", crlf));
    CHECK(crlf.seed == 7 && crlf.actions.size() == 1 && crlf.actions[0] == "X");

    // the rules it is played with
    ui::Settings mine;
    mine.sfx = true, mine.animSpeed = 1.25f, mine.playerName = "Batu", mine.difficulty = 2, mine.tavlaPoints = 5;
    mine.tavlaCesit = 2, mine.konkenLastStanding = true, mine.katlamali = true, mine.y101Acma = 51, mine.rakip = 3;
    mine.venue = 0, mine.kol = 2, mine.guideSeen = 0x7;
    const ui::Settings r = rulesFromSave(sv, mine);
    CHECK_EQ(r.game, 3);
    CHECK_EQ(r.tavlaPoints, 3);   // the save's rules ...
    CHECK_EQ(r.tavlaCesit, 1);
    CHECK_EQ(r.difficulty, 0);    // ... and level
    CHECK_EQ(r.rakip, 1);
    CHECK(r.sfx);                 // the player's own preferences
    CHECK(r.animSpeed == 1.25f);
    CHECK(r.playerName == "Batu");
    CHECK_EQ(r.venue, 0);
    CHECK_EQ(r.kol, 2);
    CHECK_EQ(r.guideSeen, 0x7);
    CHECK(!r.konkenLastStanding); // no line for it: the save is from before it ("ilk yanan")
    CHECK(!r.katlamali);          // 101 kuralları: the defaults
    CHECK_EQ(r.y101Acma, 101);

    // an old save without the çeşit / rakip lines: klasik, and the opponent from its old key (or Kel Mahmut)
    SavedMatch old;
    CHECK(parseSave("g 3\ns 1\nk tavla=5\nk tavlarakip=3\n", old));
    const ui::Settings ro = rulesFromSave(old, mine);
    CHECK_EQ(ro.tavlaCesit, 0);
    CHECK_EQ(ro.rakip, 0);
    CHECK_EQ(ui::twoPlayerOpponent(ro, ui::GameKind::Tavla), 3);
    SavedMatch older;
    CHECK(parseSave("g 7\ns 1\n", older));
    CHECK_EQ(ui::twoPlayerOpponent(rulesFromSave(older, mine), ui::GameKind::Dama), 2);

    // a save written today reads back to the same rules
    ui::Settings played = mine;
    played.game = 10, played.konkenOpen = 40, played.konkenLimit = 101, played.konkenLastStanding = true;
    played.y101Kat = 1, played.katlamali = true;
    SavedMatch now;
    std::string saved = "g 10\ns 5\n";
    std::istringstream lines(settingsText(played));
    std::string line;
    while (std::getline(lines, line)) saved += "k " + line + "\n";
    CHECK(parseSave(saved, now));
    const ui::Settings back = rulesFromSave(now, mine);
    CHECK(settingsText(back) == settingsText([&] {
              ui::Settings e = played;
              keepPlayerPrefs(e, mine);
              return e;
          }()));
}

} // namespace

int main() {
    testLines();
    testRoundTrip();
    testLegacy();
    testSave();
    std::printf("test_settings: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
