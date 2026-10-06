// Tests for the regulars' memory (ui::Memory) and the banter that uses it: Turkish case endings, match recording
// (solo, two-player, eşli, standings sheets), persistence, rank-ups, and the memory remarks (first meeting, long time
// no see, rematch / teasing, mars, streak, favourite) with their rate limits and template filling.
// Banter and Memory are pure logic: they are compiled into this test directly (the Makefile links tests with the core
// objects only).
// Build: make BUILD=build/voice build/voice/test_banter
#include "ui/Banter.cpp"
#include "ui/Memory.cpp"

#include <cstdio>
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
            std::printf("%s:%d: CHECK_EQ failed: %s\n", __FILE__, __LINE__, #a " == " #b);                             \
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

void testSuffixes() {
    // vowel harmony, buffer letters, d/t
    CHECK_STR(trSuffix("tavla", TrCase::Loc, false), "tavlada");
    CHECK_STR(trSuffix("tavla", TrCase::Acc, false), "tavlayı");
    CHECK_STR(trSuffix("tavla", TrCase::Dat, false), "tavlaya");
    CHECK_STR(trSuffix("tavla", TrCase::Abl, false), "tavladan");
    CHECK_STR(trSuffix("tavla", TrCase::Gen, false), "tavlanın");
    CHECK_STR(trSuffix("pişti", TrCase::Loc, false), "piştide");
    CHECK_STR(trSuffix("pişti", TrCase::Acc, false), "piştiyi");
    CHECK_STR(trSuffix("okey", TrCase::Loc, false), "okeyde");
    CHECK_STR(trSuffix("okey", TrCase::Acc, false), "okeyi");
    CHECK_STR(trSuffix("king", TrCase::Loc, false), "kingde");
    CHECK_STR(trSuffix("king", TrCase::Acc, false), "kingi");
    // hardening after a voiceless consonant, softening of a common noun before a vowel
    CHECK_STR(trSuffix("batak", TrCase::Loc, false), "batakta");
    CHECK_STR(trSuffix("batak", TrCase::Abl, false), "bataktan");
    CHECK_STR(trSuffix("batak", TrCase::Acc, false), "batağı");
    CHECK_STR(trSuffix("batak", TrCase::Dat, false), "batağa");
    CHECK_STR(trSuffix("evlat", TrCase::Acc, false), "evladı");
    CHECK_STR(trSuffix("kitap", TrCase::Gen, false), "kitabın");
    CHECK_STR(trSuffix("top", TrCase::Acc, false), "topu"); // one syllable: no softening
    CHECK_STR(trSuffix("ağaç", TrCase::Acc, false), "ağacı");
    CHECK_STR(trSuffix("çelenk", TrCase::Dat, false), "çelenge");
    CHECK_STR(trSuffix("delikanlı", TrCase::Acc, false), "delikanlıyı");
    CHECK_STR(trSuffix("göz", TrCase::Acc, false), "gözü");
    CHECK_STR(trSuffix("okul", TrCase::Dat, false), "okula");
    // proper nouns: an apostrophe, no softening
    CHECK_STR(trSuffix("Batuhan", TrCase::Acc, true), "Batuhan'ı");
    CHECK_STR(trSuffix("Mehmet", TrCase::Acc, true), "Mehmet'i");
    CHECK_STR(trSuffix("Mehmet", TrCase::Loc, true), "Mehmet'te");
    CHECK_STR(trSuffix("Ayşe", TrCase::Gen, true), "Ayşe'nin");
    CHECK_STR(trSuffix("Ayşe", TrCase::Dat, true), "Ayşe'ye");
    CHECK_STR(trSuffix("Burak", TrCase::Acc, true), "Burak'ı");
    CHECK_STR(trSuffix("Özgür", TrCase::Acc, true), "Özgür'ü");
    CHECK_STR(trSuffix("Oğuz", TrCase::Dat, true), "Oğuz'a");
    CHECK_STR(trSuffix("IŞIK", TrCase::Acc, true), "IŞIK'ı");      // upper case: I is ı
    CHECK_STR(trSuffix("İPEK", TrCase::Loc, true), "İPEK'te");     // İ is i
    CHECK_STR(trSuffix("Kel Mahmut", TrCase::Dat, true), "Kel Mahmut'a");
    // numbers are read aloud
    CHECK_STR(trSuffix("101", TrCase::Loc, false), "101'de");
    CHECK_STR(trSuffix("101", TrCase::Acc, false), "101'i");
    CHECK_STR(trSuffix("eşli 101", TrCase::Loc, false), "eşli 101'de");
    CHECK_STR(trSuffix("3", TrCase::Loc, false), "3'te");
    CHECK_STR(trSuffix("6", TrCase::Acc, false), "6'yı");
    CHECK_STR(trSuffix("40", TrCase::Abl, false), "40'tan");
    CHECK_STR(trSuffix("100", TrCase::Dat, false), "100'e");
    CHECK_STR(trSuffix("1000", TrCase::Loc, false), "1000'de");
    CHECK_STR(trSuffix("0", TrCase::Acc, false), "0'ı");
    CHECK_STR(trSuffix("", TrCase::Acc, false), "");
}

void testRecording() {
    Memory m;
    const int day = 20000;
    CHECK(m.firstMeeting());
    CHECK_EQ(m.favouriteGame(), -1);
    // 101 (lower is better): the player wins, Mahmut last
    m.recordMatch(MatchRecord::fromScores(0, {{120, 300, 500, 250}}, 0xF, true, false), day);
    CHECK(!m.firstMeeting());
    CHECK_EQ(m.with(2, 0).played, 1);
    CHECK_EQ(m.with(2, 0).playerWins, 1);
    CHECK(m.with(2, 0).last == MatchResult::PlayerWon);
    CHECK_EQ(m.with(2, 0).bestWin, 380);
    CHECK_EQ(m.with(2, 0).lastDay, day);
    // Nuri wins the next one
    m.recordMatch(MatchRecord::fromScores(0, {{300, 280, 400, 90}}, 0xF, true, false), day + 1);
    CHECK(m.with(3, 0).last == MatchResult::TheyWon);
    CHECK(m.with(1, 0).last == MatchResult::Other);
    CHECK_EQ(m.with(3, 0).theirWins, 1);
    CHECK_EQ(m.with(3, 0).worstLoss, 210);
    CHECK_EQ(m.with(1, 0).played, 2);
    // a shared first place: nobody won
    m.recordMatch(MatchRecord::fromScores(0, {{100, 100, 300, 400}}, 0xF, true, false), day + 1);
    CHECK(m.with(1, 0).last == MatchResult::Other);
    CHECK(m.with(2, 0).last == MatchResult::Other);
    // tavla (two players, higher is better): only Mahmut is there
    m.recordMatch(MatchRecord::fromScores(3, {{5, 0, 3, 0}}, 1u | 4u, false, false), day + 2);
    CHECK_EQ(m.with(2, 3).played, 1);
    CHECK_EQ(m.with(1, 3).played, 0);
    CHECK(m.with(2, 3).last == MatchResult::PlayerWon);
    // eşli 101: Mahmut is the partner, the team (0+2) has the lower total
    m.recordMatch(MatchRecord::fromScores(1, {{100, 300, 150, 200}}, 0xF, true, true), day + 2);
    CHECK_EQ(m.with(2, 1).together, 1);
    CHECK_EQ(m.with(2, 1).togetherWins, 1);
    CHECK_EQ(m.with(2, 1).played, 0);
    CHECK(m.with(1, 1).last == MatchResult::PlayerWon);
    CHECK(m.with(3, 1).last == MatchResult::PlayerWon);
    // eşli lost: both opponents won
    m.recordMatch(MatchRecord::fromScores(1, {{400, 100, 300, 120}}, 0xF, true, true), day + 2);
    CHECK(m.with(1, 1).last == MatchResult::TheyWon);
    CHECK(m.with(3, 1).last == MatchResult::TheyWon);
    CHECK(m.with(2, 1).last == MatchResult::Other);
    // a standings sheet: King, 4 columns = seats, Rıza first
    m.recordMatch(MatchRecord::fromStandings(6, {0, 1, 2, 3}, 4, {1, 0, 3, 2}, {1, 2, 3, 4}, {"-120", "450", "10", "-340"}),
                  day + 3);
    CHECK(m.with(1, 6).last == MatchResult::TheyWon);
    CHECK_EQ(m.with(1, 6).worstLoss, 570);
    // eşli batak sheet: 2 columns (teams), the player's side second
    m.recordMatch(MatchRecord::fromStandings(5, {0, 1, 2, 3}, 2, {1, 0}, {1, 2}, {"52", "31"}), day + 3);
    CHECK(m.with(1, 5).last == MatchResult::TheyWon);
    CHECK(m.with(3, 5).last == MatchResult::TheyWon);
    CHECK_EQ(m.with(2, 5).together, 1);
    // tavla sheet: columns = seats {0, 2}
    m.recordMatch(MatchRecord::fromStandings(3, {0, 2}, 2, {1, 0}, {1, 2}, {"3", "5"}), day + 4);
    CHECK(m.with(2, 3).last == MatchResult::TheyWon);
    CHECK_EQ(m.with(2, 3).played, 2);
    CHECK_EQ(m.with(2, 3).worstLoss, 2);
    CHECK_EQ(m.gameMatches[0], 3);
    CHECK_EQ(m.favouriteGame(), 0);
    CHECK_EQ(m.matchesTotal(), 9);
    // moments
    m.noteMoment(MomentKind::Mars, 0, 2, 3, 2, day + 4);
    CHECK(m.moment[2].kind == MomentKind::Mars && m.moment[2].byPlayer && m.moment[2].game == 3);
    m.noteMoment(MomentKind::Mars, 1, 3, 3, 0, day);  // neither side is the player: ignored
    CHECK(m.moment[1].kind == MomentKind::None && m.moment[3].kind == MomentKind::None);
    // rank
    CHECK(!m.noteRank(2)); // the first sighting is not a rise
    CHECK(!m.noteRank(2));
    CHECK(m.noteRank(3));
    CHECK(!m.noteRank(1)); // never goes down
    CHECK_EQ(m.rank, 3);
    // visits
    Memory v;
    CHECK_EQ(v.daysAway(10), -1);
    v.beginSession(10);
    CHECK_EQ(v.daysAway(10), -1);
    v.beginSession(17);
    CHECK_EQ(v.daysAway(17), 7);
    CHECK_EQ(v.visits, 2);
    CHECK_EQ(v.firstDay, 10);
    // the calendar day
    const int t = Memory::today();
    CHECK(t > 20000 && t < 40000);
}

void testPersistence() {
    Memory m;
    m.beginSession(19990);
    m.recordMatch(MatchRecord::fromScores(0, {{120, 300, 500, 250}}, 0xF, true, false), 20000);
    m.recordMatch(MatchRecord::fromScores(3, {{1, 0, 5, 0}}, 5u, false, false), 20001);
    m.recordMatch(MatchRecord::fromScores(1, {{100, 300, 150, 200}}, 0xF, true, true), 20001);
    m.noteMoment(MomentKind::OkeyFinish, 0, 1, 0, 0, 20000);
    m.noteMoment(MomentKind::Mars, 2, 0, 3, 4, 20001);
    m.noteRank(1);
    const std::string path = "build/test_banter_hafiza.txt";
    m.save(path);
    Memory r;
    r.load(path);
    std::remove(path.c_str());
    CHECK_EQ(r.firstDay, m.firstDay);
    CHECK_EQ(r.lastVisit, m.lastVisit);
    CHECK_EQ(r.visits, m.visits);
    CHECK_EQ(r.rank, 1);
    CHECK(r.gameMatches == m.gameMatches);
    for (int s = 1; s <= 3; ++s) {
        for (int g = 0; g < MEMORY_GAMES; ++g) {
            const VsRecord &a = m.with(s, g), &b = r.with(s, g);
            CHECK(a.played == b.played && a.playerWins == b.playerWins && a.theirWins == b.theirWins &&
                  a.together == b.together && a.togetherWins == b.togetherWins && a.last == b.last &&
                  a.lastDay == b.lastDay && a.bestWin == b.bestWin && a.worstLoss == b.worstLoss);
        }
        CHECK(r.moment[s].kind == m.moment[s].kind && r.moment[s].byPlayer == m.moment[s].byPlayer &&
              r.moment[s].game == m.moment[s].game && r.moment[s].day == m.moment[s].day &&
              r.moment[s].value == m.moment[s].value);
    }
    CHECK(r.moment[2].kind == MomentKind::Mars && !r.moment[2].byPlayer);
    // a missing file or an empty path changes nothing; junk lines are ignored
    Memory e;
    e.load("build/does_not_exist_hafiza.txt");
    e.load("");
    CHECK(e.firstMeeting() && e.visits == 0);
    {
        FILE* f = std::fopen(path.c_str(), "w");
        std::fputs("# x\nzzz\ns9.101.mac=5\ns1.nope.mac=3\ns1.101.mac=4\ns2.an=7,1,0,0,0\n101.mac=-3\n", f);
        std::fclose(f);
    }
    Memory j;
    j.load(path);
    std::remove(path.c_str());
    CHECK_EQ(j.with(1, 0).played, 4);
    CHECK(j.moment[2].kind == MomentKind::None);
    CHECK_EQ(j.gameMatches[0], 0);
}

// pops every queued line after letting time pass
std::vector<BanterLine> drain(Banter& b, float seconds = 15.f) {
    std::vector<BanterLine> out;
    for (float t = 0.f; t < seconds; t += 0.1f) {
        b.update(0.1f, false, true);
        BanterLine l;
        while (b.pop(l)) {
            out.push_back(l);
            b.spoke(l.seat);
        }
    }
    return out;
}

bool contains(const std::vector<BanterLine>& v, const std::string& part) {
    for (const BanterLine& l : v)
        if (l.text.find(part) != std::string::npos) return true;
    return false;
}

bool templatesFilled(const std::vector<BanterLine>& v) {
    for (const BanterLine& l : v)
        if (l.text.find('{') != std::string::npos || l.text.find('}') != std::string::npos) return false;
    return true;
}

Banter makeBanter(uint64_t seed, const std::string& player = "Batuhan") {
    Banter b;
    b.reset(seed);
    b.setNames({player, "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"});
    return b;
}

void testMemoryBanter() {
    const int day = 20100;
    // the first ever meeting
    {
        Memory m;
        m.beginSession(day);
        int hits = 0;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            Banter b = makeBanter(seed);
            b.setMemory(&m);
            const bool q = b.matchStart(0, 0, day);
            hits += q ? 1 : 0;
            const auto lines = drain(b);
            CHECK(templatesFilled(lines));
            CHECK(!q || !lines.empty());
        }
        CHECK_EQ(hits, 40);
    }
    // long time no see: the number of days is in the line (when the speaker's line has one)
    {
        Memory m;
        m.beginSession(day - 12);
        m.recordMatch(MatchRecord::fromScores(0, {{100, 200, 300, 400}}, 0xF, true, false), day - 12);
        m.beginSession(day);
        CHECK_EQ(m.daysAway(day), 12);
        bool sawDays = false;
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Banter b = makeBanter(seed);
            b.setMemory(&m);
            CHECK(b.matchStart(0, 0, day));
            const auto lines = drain(b);
            CHECK(templatesFilled(lines));
            sawDays = sawDays || contains(lines, "12 gün");
        }
        CHECK(sawDays);
    }
    // a rematch: the player beat Mahmut at tavla last time; a mars is remembered
    {
        Memory m;
        m.beginSession(day - 1);
        m.recordMatch(MatchRecord::fromScores(3, {{5, 0, 2, 0}}, 5u, false, false), day - 1);
        m.noteMoment(MomentKind::Mars, 0, 2, 3, 2, day - 1);
        m.beginSession(day);
        int mahmut = 0, total = 0;
        bool sawMars = false, sawGame = false;
        for (uint64_t seed = 1; seed <= 80; ++seed) {
            Banter b = makeBanter(seed);
            b.setMemory(&m);
            if (!b.matchStart(3, 0, day)) continue;
            const auto lines = drain(b);
            CHECK(templatesFilled(lines));
            ++total;
            for (const BanterLine& l : lines) mahmut += l.seat == 2 ? 1 : 0;
            sawMars = sawMars || contains(lines, "mars");
            sawGame = sawGame || contains(lines, "avla");
        }
        CHECK(total > 50);
        CHECK(mahmut >= total);   // Mahmut (the only tavla opponent) speaks every time
        CHECK(sawMars);
        (void)sawGame;
    }
    // teasing: Nuri won the last 101 match; the game name is filled with its ending
    {
        Memory m;
        m.beginSession(day);
        m.recordMatch(MatchRecord::fromScores(0, {{300, 280, 400, 90}}, 0xF, true, false), day);
        bool nuri = false, game = false;
        for (uint64_t seed = 1; seed <= 80; ++seed) {
            Banter b = makeBanter(seed);
            b.setMemory(&m);
            b.matchStart(0, 0, day); // not the first match of the run any more: same session
            const auto lines = drain(b);
            CHECK(templatesFilled(lines));
            for (const BanterLine& l : lines) nuri = nuri || l.seat == 3;
            game = game || contains(lines, "101'de") || contains(lines, "101 maçı");
        }
        CHECK(nuri);
        CHECK(game);
    }
    // a streak (no memory needed) and the rank-up line
    {
        bool streak = false, rank = false;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            Banter b = makeBanter(seed, "Sen");
            b.matchStart(4, 5, day);
            const auto lines = drain(b);
            CHECK(templatesFilled(lines));
            streak = streak || contains(lines, "5 ma") || contains(lines, "5 gal") || contains(lines, "5 kere");
            Banter c = makeBanter(seed, "Sen");
            CHECK(c.rankUp("Müdavim"));
            const auto rl = drain(c);
            CHECK(templatesFilled(rl));
            rank = rank || contains(rl, "Müdavim");
            CHECK(!contains(rl, "Sen ")); // the default name is never used as an address
        }
        CHECK(streak);
        CHECK(rank);
    }
    // the favourite game, at most once a run
    {
        Memory m;
        m.beginSession(day);
        for (int i = 0; i < 6; ++i) m.recordMatch(MatchRecord::fromScores(4, {{10, 10, 5, 1}}, 0xF, false, false), day); // shared first: no rematch talk
        int fav = 0;
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Banter b = makeBanter(seed);
            b.setMemory(&m);
            int said = 0;
            for (int k = 0; k < 6; ++k) {
                b.matchStart(4, 0, day);
                const auto lines = drain(b, 30.f);
                CHECK(templatesFilled(lines));
                said += (contains(lines, "pişti") || contains(lines, "Pişti")) ? 1 : 0;
            }
            CHECK(said <= 1);
            fav += said;
        }
        CHECK(fav > 0);
    }
    // disabled banter (title mode) says nothing
    {
        Memory m;
        Banter b = makeBanter(3);
        b.setMemory(&m);
        b.setEnabled(false);
        CHECK(!b.matchStart(0, 9, day));
        CHECK(!b.rankUp("Efsane"));
    }
    // every memory table has lines for the three regulars, at least four each for the situations of the brief
    std::printf("test_banter: %d distinct banter lines\n", Banter::lineCount());
    const Tbl* rows[] = {kTables[S_MemFirst], kTables[S_MemLongAway], kTables[S_MemRematch], kTables[S_MemTease],
                         kTables[S_MemStreak], kTables[S_MemFavourite], kTables[S_MemRankUp], kTables[S_MemOkey]};
    for (const Tbl* row : rows)
        for (int s = 0; s < 3; ++s) CHECK(row[s].n >= 4);
}

} // namespace

int main() {
    testSuffixes();
    testRecording();
    testPersistence();
    testMemoryBanter();
    std::printf("test_banter: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
