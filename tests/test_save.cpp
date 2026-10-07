// Tests for the player's files (ui/SaveFile.h and the books that use it): the atomic writer (the file is replaced
// whole, no temporary left, a failed write leaves the old file), the key=value reader, clamped numbers, the day
// numbers, and StatsBook's load (counts clamped and kept consistent) with its save round trip.
// Stats and Memory are pure logic: they are compiled into this test directly (the Makefile links tests with the core
// objects only).
// Build: make BUILD=build/app build/app/test_save
#include "ui/Memory.cpp"
#include "ui/SaveFile.h"
#include "ui/Stats.cpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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

std::string g_dir;

bool exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

void testAtomic() {
    const std::string path = g_dir + "/kitap.txt";
    std::string text;
    CHECK(writeFileAtomic(path, "bir=1\n"));
    CHECK(readFileText(path, text) && text == "bir=1\n");
    CHECK(!exists(path + ".tmp"));
    CHECK(writeFileAtomic(path, "iki=2\n")); // replaced whole
    CHECK(readFileText(path, text) && text == "iki=2\n");
    CHECK(!exists(path + ".tmp"));
    CHECK(writeFileAtomic(path, ""));
    CHECK(readFileText(path, text) && text.empty());
    // a write that cannot happen: false, nothing left behind
    const std::string nowhere = g_dir + "/yok/klasor/kitap.txt";
    CHECK(!writeFileAtomic(nowhere, "x"));
    CHECK(!exists(nowhere) && !exists(nowhere + ".tmp"));
    CHECK(!writeFileAtomic("", "x"));
    CHECK(!readFileText(g_dir + "/olmayan.txt", text));
    // the target is a directory: the rename fails, the temporary goes, the directory stays
    const std::string dir = g_dir + "/klasor";
    CHECK(mkdir(dir.c_str(), 0700) == 0);
    CHECK(!writeFileAtomic(dir, "x"));
    CHECK(!exists(dir + ".tmp"));
    rmdir(dir.c_str());
    std::remove(path.c_str());
}

void testReaders() {
    int n = 0;
    std::string seen;
    forEachKeyValue("# yorum\n\na=1\r\nb = 2\nhatali\nc=x=y\n", [&](const std::string& k, const std::string& v) {
        ++n;
        seen += "[" + k + "|" + v + "]";
    });
    CHECK_EQ(n, 3);
    CHECK(seen == "[a|1][b | 2][c|x=y]");
    CHECK_EQ(parseLong("42", 0, 100), 42L);
    CHECK_EQ(parseLong("420", 0, 100), 100L);
    CHECK_EQ(parseLong("-3", 0, 100), 0L);
    CHECK_EQ(parseLong("abc", 0, 100, 7), 7L);
    CHECK_EQ(parseLong("", -5, 5), 0L);
    CHECK_EQ(parseLong("999999999999999999999999", 0, 100), 100L);
    CHECK_EQ(parseLong("-999999999999999999999999", -10, 100), -10L);
    CHECK_EQ(parseLong("12abc", 0, 100), 12L);
    CHECK_EQ(gameKeyIndex("konken"), 10);
    CHECK_EQ(gameKeyIndex("101"), 0);
    CHECK_EQ(gameKeyIndex("satranc"), -1);
    CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
    CHECK_EQ(daysFromCivil(2000, 3, 1), 11017);
    CHECK_EQ(daysFromCivil(2026, 10, 7), 20733);
    CHECK_EQ(Memory::today(), todayDays());
    CHECK_EQ(daysFromCivil(1969, 12, 31), -1);
}

void testStats() {
    const std::string path = g_dir + "/istatistik.txt";
    CHECK(writeFileAtomic(path, "# elle düzenlenmiş\n"
                                "batak.mac=5\nbatak.galibiyet=9\nbatak.el=20\nbatak.elkazanc=-4\n"
                                "batak.seri=7\nbatak.enuzunseri=2\nbatak.mac1=2\nbatak.galibiyet1=6\n"
                                "batak.rekor=-12\n101.mac=99999999999999\n101.galibiyet=3\n"
                                "satranc.mac=4\nbozuksatir\n"));
    StatsBook b;
    b.load(path);
    const GameRecord& r = b.games[5];
    CHECK_EQ(r.matches, 5);
    CHECK_EQ(r.wins, 5);        // never more wins than matches
    CHECK_EQ(r.hands, 20);
    CHECK_EQ(r.handWins, 0);    // no negative counts
    CHECK_EQ(r.streak, 5);
    CHECK_EQ(r.bestStreak, 5);  // the best streak at least the current one
    CHECK_EQ(r.playedAt[1], 2);
    CHECK_EQ(r.winsAt[1], 2);
    CHECK(r.hasBest && r.best == -12); // (a hand score may be negative)
    CHECK_EQ(b.games[0].matches, 10000000);
    CHECK_EQ(b.games[0].wins, 3);
    // save and load again: the same book
    CHECK(b.save(path));
    StatsBook c;
    c.load(path);
    for (int g = 0; g < STATS_GAMES; ++g) {
        CHECK_EQ(c.games[g].matches, b.games[g].matches);
        CHECK_EQ(c.games[g].wins, b.games[g].wins);
        CHECK_EQ(c.games[g].hands, b.games[g].hands);
        CHECK_EQ(c.games[g].best, b.games[g].best);
    }
    CHECK(!exists(path + ".tmp"));
    std::remove(path.c_str());
    StatsBook none;
    none.load(g_dir + "/olmayan.txt");
    CHECK_EQ(none.total().matches, 0);
    CHECK(!none.save(""));
}

void testMemory() {
    const std::string path = g_dir + "/hafiza.txt";
    Memory m;
    m.beginSession(20000);
    MatchRecord rec = MatchRecord::fromScores(5, {{10, 5, 3, 1}}, 0xFu, false, false);
    m.recordMatch(rec, 20001);
    CHECK(m.save(path));
    Memory n;
    n.load(path);
    CHECK_EQ(n.visits, m.visits);
    CHECK_EQ(n.gameMatches[5], m.gameMatches[5]);
    CHECK_EQ(n.vs[2][5].played, m.vs[2][5].played);
    CHECK(writeFileAtomic(path, "ziyaret=99999999999999999999\ns2.batak.mac=-5\n"));
    Memory o;
    o.load(path);
    CHECK(o.visits >= 0);
    CHECK_EQ(o.vs[2][5].played, 0);
    std::remove(path.c_str());
}

} // namespace

int main() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base && *base ? base : "/tmp") + "/sakli_save_XXXXXX";
    const char* d = mkdtemp(&tmpl[0]);
    if (!d) {
        std::printf("test_save: no temporary directory\n");
        return 1;
    }
    g_dir = d;
    testAtomic();
    testReaders();
    testStats();
    testMemory();
    rmdir(g_dir.c_str());
    std::printf("test_save: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
