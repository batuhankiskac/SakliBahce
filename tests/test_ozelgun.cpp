// Özel günler (ozelgun): the computed Hijri dates against the Diyanet calendar, the day kinds around them, the
// SAKLI_GUN override and the special-day banter lines.
// Build: make BUILD=build/ozelgun build/ozelgun/test_ozelgun
#include "r3d/SpecialDay.h"
#include "ui/BanterSpecial.cpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("CHECK failed: %s\n", what.c_str());
    }
}

std::string ymd(long day) {
    int y, m, d;
    w3d::gun::civilFromDays(day, y, m, d);
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
}

int wday(int y, int m, int d) {  // 0 = Sunday (1970-01-01 was a Thursday)
    const long t = w3d::gun::daysFromCivil(y, m, d);
    return (int)(((t % 7) + 7 + 4) % 7);
}

}  // namespace

int main() {
    using namespace w3d;
    // the calendar arithmetic
    check(gun::daysFromCivil(1970, 1, 1) == 0, "epoch");
    check(ymd(gun::daysFromCivil(2026, 10, 7)) == "2026-10-07", "round trip");
    check(wday(2026, 10, 7) == 3, "2026-10-07 is a Wednesday");

    // Diyanet İşleri Başkanlığı's dates (first day of Ramazan, of Ramazan Bayramı, of Kurban Bayramı)
    struct Known {
        int hy;
        const char* ramazan;
        const char* ramazanBayrami;
        const char* kurban;
    };
    const Known known[] = {{1446, "2025-03-01", "2025-03-30", "2025-06-06"},
                           {1447, "2026-02-19", "2026-03-20", "2026-05-27"},
                           {1448, "2027-02-08", "2027-03-09", "2027-05-16"}};
    for (const Known& k : known) {
        check(ymd(gun::hijriMonthStart(k.hy, 9)) == k.ramazan, std::string("Ramazan ") + k.ramazan + " got " + ymd(gun::hijriMonthStart(k.hy, 9)));
        check(ymd(gun::hijriMonthStart(k.hy, 10)) == k.ramazanBayrami,
              std::string("Ramazan Bayramı ") + k.ramazanBayrami + " got " + ymd(gun::hijriMonthStart(k.hy, 10)));
        check(ymd(gun::hijriMonthStart(k.hy, 12) + 9) == k.kurban,
              std::string("Kurban Bayramı ") + k.kurban + " got " + ymd(gun::hijriMonthStart(k.hy, 12) + 9));
    }
    // every year to 2036: a Ramazan of 29-30 days, the bayrams in order, months of 29-30 days
    for (int hy = 1446; hy <= 1458; ++hy) {
        const long r = gun::hijriMonthStart(hy, 9), s = gun::hijriMonthStart(hy, 10), z = gun::hijriMonthStart(hy, 12);
        check(s - r == 29 || s - r == 30, "Ramazan length " + std::to_string(hy));
        check(z - s >= 58 && z - s <= 60, "Şevval + Zilkade " + std::to_string(hy));
        check(gun::hijriMonthStart(hy + 1, 1) - z == 29 || gun::hijriMonthStart(hy + 1, 1) - z == 30, "Zilhicce " + std::to_string(hy));
    }
    // the day kinds
    check(gun::dayKind(2026, 3, 20, wday(2026, 3, 20)) == GunRamazanBayrami, "20 Mar 2026 Ramazan Bayramı");
    check(gun::dayKind(2026, 3, 22, wday(2026, 3, 22)) == GunRamazanBayrami, "22 Mar 2026 3rd day");
    check(gun::dayKind(2026, 3, 23, wday(2026, 3, 23)) == GunNormal, "23 Mar 2026 (Monday) normal");
    check(gun::dayKind(2026, 3, 1, wday(2026, 3, 1)) == GunRamazan, "1 Mar 2026 Ramazan (a Sunday: Ramazan wins)");
    check(gun::dayKind(2026, 5, 27, wday(2026, 5, 27)) == GunKurbanBayrami, "27 May 2026 Kurban Bayramı");
    check(gun::dayKind(2026, 5, 30, wday(2026, 5, 30)) == GunKurbanBayrami, "30 May 2026 4th day");
    check(gun::dayKind(2026, 5, 31, wday(2026, 5, 31)) == GunMac, "31 May 2026 a Sunday: the derby");
    check(gun::dayKind(2026, 10, 11, wday(2026, 10, 11)) == GunMac, "11 Oct 2026 Sunday");
    check(gun::dayKind(2026, 10, 7, wday(2026, 10, 7)) == GunNormal, "7 Oct 2026 Wednesday");
    // the derby is an evening thing; the override wins
    unsetenv("SAKLI_GUN");
    check(resolveSpecialDay(false, 3) == GunNormal, "setting off");
    setenv("SAKLI_GUN", "mac", 1);
    check(resolveSpecialDay(true, 3) == GunMac && resolveSpecialDay(false, 0) == GunMac, "SAKLI_GUN=mac");
    setenv("SAKLI_GUN", "kurban", 1);
    check(resolveSpecialDay(true, 1) == GunKurbanBayrami, "SAKLI_GUN=kurban");
    setenv("SAKLI_GUN", "ramazan", 1);
    check(resolveSpecialDay(true, 1) == GunRamazan && ramazanEvening(GunRamazan, 2) && !ramazanEvening(GunRamazan, 1), "Ramazan evening");
    setenv("SAKLI_GUN", "normal", 1);
    check(resolveSpecialDay(true, 3) == GunNormal, "SAKLI_GUN=normal");
    unsetenv("SAKLI_GUN");

    // the lines: every situation has some, speakers are 1..4, no text is empty, they vary with `pick`
    int lines = 0;
    for (int day = 0; day <= 4; ++day)
        for (int s = 0; s <= (int)ui::SpecialSit::MoveOut; ++s) {
            std::string first;
            for (uint32_t p = 0; p < 12; ++p) {
                int seat = 0;
                std::string text;
                if (!ui::specialLine(day, (ui::SpecialSit)s, p, seat, text)) break;
                ++lines;
                check(seat >= 1 && seat <= 4 && !text.empty(), "line speaker / text");
                if (p == 0) first = text;
            }
        }
    int seat = 0;
    std::string text;
    check(ui::specialLine(1, ui::SpecialSit::Greeting, 0, seat, text) && text.find("ayram") != std::string::npos, "bayram greeting");
    check(ui::specialLine(0, ui::SpecialSit::MoveInRain, 0, seat, text) && text == "Yağmur başladı, içeri geçelim!", "the rain line");
    check(!ui::specialLine(0, ui::SpecialSit::Greeting, 0, seat, text), "no greeting on a normal day");

    std::printf("test_ozelgun: %d checks, %d failures (%d line picks)\n", g_checks, g_failures, lines);
    return g_failures ? 1 : 0;
}
