// Tests for the badges (ui::Achievements): the list itself, unlocking by events / counts / matches / days, no badge
// opening twice, the Yapay Zeka / replay / unattended exclusion (the live gate App sets from App::recording()), the
// file round trip, and the regulars' congratulation (Banter::achievement).
// Achievements and Banter are pure logic: they are compiled into this test directly (the Makefile links tests with the
// core objects only).
// Build: make BUILD=build/basarim build/basarim/test_achievements
#include "ui/Achievements.cpp"
#include "ui/Banter.cpp"
#include "ui/Memory.cpp"

#include <cstdio>
#include <set>
#include <string>

using namespace ui;

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
#define CHECK_STR(a, b)                                                                                                \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        const std::string va_ = (a), vb_ = (b);                                                                        \
        if (va_ != vb_) {                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, va_.c_str(), vb_.c_str());                    \
        }                                                                                                              \
    } while (0)

constexpr int DAY = 20367; // 06.10.2025 .. any day number

Achievements live() {
    Achievements a;
    a.setLive(true, true);
    return a;
}

bool opened(const std::vector<int>& v, const char* id) {
    for (int i : v)
        if (std::string(Achievements::def(i).id) == id) return true;
    return false;
}

void testList() {
    const int n = Achievements::count();
    CHECK(n >= 30 && n <= 40);
    std::set<std::string> ids;
    int secret = 0;
    for (int i = 0; i < n; ++i) {
        const AchievementDef& d = Achievements::def(i);
        ids.insert(d.id);
        CHECK(std::string(d.name).size() > 2);
        CHECK(std::string(d.desc).size() > 8);
        CHECK(std::string(d.desc).size() < 110); // one line on the screen
        CHECK(d.goal >= 0);
        CHECK(d.group >= 0 && d.group <= 2);
        CHECK_EQ(Achievements::find(d.id), i);
        secret += d.secret ? 1 : 0;
    }
    CHECK_EQ((int)ids.size(), n); // unique ids
    CHECK(secret >= 1 && secret <= 3);
    CHECK_EQ(Achievements::find("yok_boyle_bir_sey"), -1);
    // every game has something: the new games and the tavla variants too
    for (const char* id : {"okey_bitis", "mars", "katmerli", "pisti_ustune", "batak13", "king_cezasiz", "dama", "66_kapat",
                           "bezik", "konken_elden", "gulbahar", "fevga", "ziyaret7", "her_oyun", "hatasiz", "yz_izle",
                           "mudavimler", "cay10", "dort_mevsim", "gece_sabah", "seri10"})
        CHECK(Achievements::find(id) >= 0);
}

void testEvents() {
    Achievements a = live();
    const int p = Achievements::find("pisti_ustune");
    CHECK(!a.unlocked(p));
    std::vector<int> v = a.event("pisti_ustune", 1, DAY);
    CHECK_EQ((int)v.size(), 1);
    CHECK(opened(v, "pisti_ustune"));
    CHECK(a.unlocked(p));
    CHECK_EQ(a.unlockedDay(p), DAY);
    CHECK_STR(a.unlockedDate(p), Achievements::dateText(DAY));
    // no double unlock
    CHECK(a.event("pisti_ustune", 1, DAY + 3).empty());
    CHECK_EQ(a.unlockedDay(p), DAY);
    CHECK_EQ(a.unlockedCount(), 1);
    // unknown events and the badges App counts itself are not opened by a table
    CHECK(a.event("bilinmeyen").empty());
    CHECK(a.event("mac100").empty());
    CHECK(a.event("esli5").empty());
    CHECK(a.event("kurt_mars").empty());
    // a mars against Usta is a mars; against Kurt it also opens "Kurt'u Marsla Yen"
    v = a.event("mars", 1, DAY);
    CHECK(opened(v, "mars") && !opened(v, "kurt_mars"));
    v = a.event("mars", 2, DAY);
    CHECK_EQ((int)v.size(), 1);
    CHECK(opened(v, "kurt_mars"));
    CHECK(a.event("mars", 2, DAY).empty());
    // a count: King's clean ceza deals, 10 needed
    const int k = Achievements::find("king_cezasiz");
    for (int i = 0; i < 9; ++i) CHECK(a.event("king_cezasiz", 1, DAY).empty());
    CHECK_EQ(a.progress(k), 9);
    CHECK_EQ(a.goal(k), 10);
    v = a.event("king_cezasiz", 1, DAY);
    CHECK(opened(v, "king_cezasiz"));
    CHECK(a.event("king_cezasiz", 1, DAY).empty());
    CHECK_EQ(a.progress(k), 10); // capped at the goal
}

void testMatches() {
    Achievements a = live();
    a.setGames(0x7Fu); // seven games playable
    MatchEndInfo m;
    m.game = 4; // pişti
    m.level = 1;
    m.won = true;
    m.beaten = 0xEu;
    m.phase = 2;
    m.season = 1;
    m.hour = 20;
    std::vector<int> v = a.matchEnd(m, DAY);
    CHECK(opened(v, "ilk_galibiyet"));
    CHECK(opened(v, "mudavimler")); // all three regulars sat against the player
    CHECK(!opened(v, "kurt_yen"));
    CHECK_EQ(a.progress(Achievements::find("her_oyun")), 1);
    CHECK_EQ(a.goal(Achievements::find("her_oyun")), 7);
    CHECK_EQ(a.progress(Achievements::find("cay10")), 1);
    CHECK_EQ(a.progress(Achievements::find("mac100")), 1);
    CHECK_EQ(a.progress(Achievements::find("dort_mevsim")), 1);
    // the same season again does not count twice
    a.matchEnd(m, DAY);
    CHECK_EQ(a.progress(Achievements::find("dort_mevsim")), 1);
    CHECK_EQ(a.progress(Achievements::find("seri10")), 2);
    // a win in every playable game
    for (int g = 0; g < 7; ++g) {
        m.game = g;
        v = a.matchEnd(m, DAY);
        if (g < 6) CHECK(!opened(v, "her_oyun"));
    }
    CHECK(opened(v, "her_oyun"));
    CHECK_EQ(a.progress(Achievements::find("esli5")), 1); // one eşli 101 win
    CHECK_EQ(a.progress(Achievements::find("cay10")), 9);
    // Kurt: the tenth win in a row
    m.level = 2;
    v = a.matchEnd(m, DAY);
    CHECK(opened(v, "kurt_yen"));
    CHECK(opened(v, "cay10"));
    CHECK(opened(v, "seri10"));
    // night, morning, the small hours, the four seasons
    m.phase = 3;
    m.hour = 23;
    v = a.matchEnd(m, DAY);
    CHECK(!opened(v, "gece_sabah"));
    m.phase = 0;
    m.hour = 4; // (a made-up clock: morning and the small hours at once, for the test)
    v = a.matchEnd(m, DAY);
    CHECK(opened(v, "gece_sabah"));
    CHECK(opened(v, "sabaha_karsi"));
    for (int s = 0; s < 4; ++s) {
        m.season = s;
        v = a.matchEnd(m, DAY);
    }
    CHECK(a.unlocked(Achievements::find("dort_mevsim")));
    // the losing streak (secret), broken by a win
    m.won = false;
    for (int i = 0; i < 4; ++i) CHECK(!opened(a.matchEnd(m, DAY), "kara_gun"));
    m.won = true;
    a.matchEnd(m, DAY);
    m.won = false;
    for (int i = 0; i < 4; ++i) CHECK(!opened(a.matchEnd(m, DAY), "kara_gun"));
    CHECK(opened(a.matchEnd(m, DAY), "kara_gun"));
    // the regulars one by one
    Achievements b = live();
    m.won = true;
    m.beaten = 1u << 2; // tavla against Kel Mahmut
    b.matchEnd(m, DAY);
    m.beaten = 1u << 1 | 1u << 3; // eşli: Hacı Rıza and Emekli Nuri
    CHECK_EQ(b.progress(Achievements::find("mudavimler")), 1);
    CHECK(opened(b.matchEnd(m, DAY), "mudavimler"));
    // a hundred matches
    Achievements c = live();
    m.won = false;
    for (int i = 0; i < 99; ++i) CHECK(!opened(c.matchEnd(m, DAY), "mac100"));
    CHECK(opened(c.matchEnd(m, DAY), "mac100"));
}

void testDays() {
    Achievements a = live();
    const int z = Achievements::find("ziyaret7");
    for (int d = 0; d < 6; ++d) {
        CHECK(a.handPlayed(DAY + d).empty());
        a.handPlayed(DAY + d); // more hands the same day: still one day
    }
    CHECK_EQ(a.progress(z), 6);
    // a day missed: the streak starts again (the best stays as the progress)
    a.handPlayed(DAY + 8);
    CHECK_EQ(a.progress(z), 6);
    for (int d = 9; d < 14; ++d) CHECK(a.handPlayed(DAY + d).empty());
    CHECK(opened(a.handPlayed(DAY + 14), "ziyaret7"));
    CHECK(a.handPlayed(DAY + 15).empty());
}

void testAnalysisAndWatching() {
    Achievements a = live();
    CHECK(a.analysis(2, DAY).empty());
    CHECK(opened(a.analysis(0, DAY), "hatasiz"));
    CHECK(a.analysis(0, DAY).empty());
    // watching the Yapay Zeka: the match is not the player's (not playing), but he sat and watched (interactive)
    Achievements w;
    w.setLive(false, true);
    CHECK(opened(w.watchedAi(DAY), "yz_izle"));
    CHECK(w.watchedAi(DAY).empty());
    // ... an unattended run (--autoplay, a snapshot) never
    Achievements u;
    u.setLive(false, false);
    CHECK(u.watchedAi(DAY).empty());
}

// The rule of App::recording(): no unattended run, no match the Yapay Zeka touched, no replay. App passes
// setLive(recording(), !unattended()); nothing is counted while it is off, and the counters do not move either.
void testExclusion() {
    struct Case {
        bool unattended, aiTouched, replay;
    };
    const Case cases[] = {{true, false, false}, {false, true, false}, {false, false, true}, {true, true, true}};
    for (const Case& cs : cases) {
        Achievements a;
        const bool recording = !cs.unattended && !cs.aiTouched && !cs.replay;
        a.setLive(recording, !cs.unattended);
        CHECK(!a.live());
        CHECK(a.event("pisti").empty());
        CHECK(a.event("mars", 2).empty());
        CHECK(a.add("king_cezasiz", 5).empty());
        CHECK(a.reach("ziyaret7", 7).empty());
        CHECK(a.handPlayed(DAY).empty());
        MatchEndInfo m;
        m.won = true;
        m.level = 2;
        m.beaten = 0xEu;
        CHECK(a.matchEnd(m, DAY).empty());
        CHECK(a.analysis(0, DAY).empty());
        CHECK_EQ(a.unlockedCount(), 0);
        CHECK_EQ(a.progress(Achievements::find("king_cezasiz")), 0);
        CHECK_EQ(a.progress(Achievements::find("mac100")), 0);
        // the streaks did not move: the first counted win afterwards is a streak of one
        a.setLive(true, true);
        a.matchEnd(m, DAY);
        CHECK_EQ(a.progress(Achievements::find("seri10")), 1);
    }
    // a match the player plays himself counts
    Achievements ok;
    ok.setLive(true, true);
    CHECK(opened(ok.event("pisti"), "pisti"));
}

void testFile() {
    Achievements a = live();
    a.setGames(0x7FFu);
    a.event("pisti_ustune", 1, DAY);
    a.event("king_cezasiz", 1, DAY);
    a.event("king_cezasiz", 1, DAY);
    a.handPlayed(DAY);
    a.handPlayed(DAY + 1);
    MatchEndInfo m;
    m.game = 3;
    m.won = true;
    m.beaten = 1u << 2;
    m.phase = 3;
    m.season = 2;
    a.matchEnd(m, DAY + 1);
    a.matchEnd(m, DAY + 1);
    const std::string t = a.text();
    Achievements b;
    b.parse(t);
    CHECK_STR(b.text(), t);
    for (int i = 0; i < Achievements::count(); ++i) {
        CHECK_EQ(b.unlocked(i), a.unlocked(i));
        CHECK_EQ(b.progress(i), a.progress(i));
        CHECK_EQ(b.unlockedDay(i), a.unlockedDay(i));
    }
    CHECK_EQ(b.unlockedDay(Achievements::find("pisti_ustune")), DAY);
    CHECK_EQ(b.progress(Achievements::find("king_cezasiz")), 2);
    // the counters behind the counts survive too: the streaks go on after a restart
    b.setLive(true, true);
    b.setGames(0x7FFu);
    b.handPlayed(DAY + 2);
    CHECK_EQ(b.progress(Achievements::find("ziyaret7")), 3);
    b.matchEnd(m, DAY + 2);
    CHECK_EQ(b.progress(Achievements::find("seri10")), 3);
    // a badge already open stays open (no second unlock after loading)
    CHECK(b.event("pisti_ustune", 1, DAY + 2).empty());
    CHECK_EQ(b.unlockedDay(Achievements::find("pisti_ustune")), DAY);
    // garbage and unknown keys are ignored
    Achievements c;
    c.parse("# yorum\nbozuk satir\nyok_boyle=3,4\npisti=1,20000\ndurum.yenilenler=255\n");
    CHECK(c.unlocked(Achievements::find("pisti")));
    CHECK_STR(c.unlockedDate(Achievements::find("pisti")), Achievements::dateText(20000));
    CHECK_EQ(c.unlockedCount(), 1);
    // through a real file
    const std::string path = "/tmp/sakli_basarim_test.txt";
    a.save(path);
    Achievements d;
    d.load(path);
    CHECK_STR(d.text(), t);
    std::remove(path.c_str());
    Achievements e;
    e.load("/tmp/sakli_basarim_yok_boyle_dosya.txt"); // missing: empty book
    CHECK_EQ(e.unlockedCount(), 0);
    e.load(""); // no path: nothing
}

void testDates() {
    CHECK_STR(Achievements::dateText(0), "01.01.1970");
    CHECK_STR(Achievements::dateText(20367), "06.10.2025");
    CHECK_STR(Achievements::dateText(20732), "06.10.2026");
    CHECK_STR(Achievements::dateText(-1), "");
    CHECK(Achievements::today() > 20000);
    CHECK_STR(Achievements::dateText(Achievements::today()).substr(2, 1), ".");
}

void testBanter() {
    Banter b;
    b.reset(7);
    b.setNames({"Batuhan", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
    CHECK(b.achievement("Pişti Üstüne Pişti"));
    std::string said;
    int who = 0;
    for (int i = 0; i < 200; ++i) {
        b.update(0.1f, false, true);
        BanterLine l;
        while (b.pop(l)) {
            if (l.text.find("Pişti Üstüne Pişti") != std::string::npos) {
                said = l.text;
                who = l.seat;
            }
            b.spoke(l.seat);
        }
    }
    CHECK(!said.empty());
    CHECK(who >= 1 && who <= 3);
    CHECK(said.find('{') == std::string::npos); // every template filled
    b.setEnabled(false);
    CHECK(!b.achievement("Mars!"));
    b.setEnabled(true);
    CHECK(!b.achievement(""));
    // every line of every regular fills
    for (uint64_t seed = 1; seed < 40; ++seed) {
        Banter c;
        c.reset(seed);
        c.setNames({"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
        CHECK(c.achievement("Kusursuz"));
        for (int i = 0; i < 200; ++i) {
            c.update(0.1f, false, true);
            BanterLine l;
            while (c.pop(l)) {
                CHECK(l.text.find('{') == std::string::npos);
                CHECK(!l.text.empty());
                c.spoke(l.seat);
            }
        }
    }
}

} // namespace

int main() {
    testList();
    testEvents();
    testMatches();
    testDays();
    testAnalysisAndWatching();
    testExclusion();
    testFile();
    testDates();
    testBanter();
    std::printf("test_achievements: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
