#pragma once
// Özel günler: the kahvehane on a bayram, on Ramazan evenings and on a derby night (ozelgun agent). Header-only, no raylib:
// used by Room (decorations), Characters (better clothes, scarves, more people), App (banter, the sahur davul) and tests.
//
//   Bayram    Ramazan Bayramı (1-3 Şevval) and Kurban Bayramı (10-13 Zilhicce): flags and bunting, lokum and candy
//   Ramazan   the month of Ramazan; its evenings (after iftar) fill the kahvehane, güllaç on the tables, the davul at night
//   Maç       every Sunday evening and night (Pazar akşamı) a derby on the TV
// A bayram wins over Ramazan (they never overlap anyway), both win over the Sunday derby.
//
// The Hijri dates are computed, not tabled: the start of a Hijri month is taken from the astronomical new moon (Jean Meeus,
// "Astronomical Algorithms", 2nd ed., ch. 49, the periodic terms without the small planetary ones: a minute or so) with the
// rule the Diyanet İşleri Başkanlığı's calendar follows in practice: the month begins the day after the conjunction when
// the conjunction falls before 12:00 UTC (the crescent is then visible somewhere that evening), otherwise a day later. The
// tabular (arithmetical) Islamic calendar picks which conjunction. Checked against the Diyanet calendar for 2025-2027
// (tests/test_ozelgun.cpp): Ramazan 1 Mar 2025, 19 Feb 2026, 8 Feb 2027; Ramazan Bayramı 30 Mar 2025, 20 Mar 2026,
// 9 Mar 2027; Kurban Bayramı 6 Jun 2025, 27 May 2026, 16 May 2027. For later years expect the odd day of difference when a
// conjunction falls close to noon UTC; the bayram decorations go up on the arife afternoon anyway.
//
// Developer override for snapshots and tests: SAKLI_GUN=bayram|kurban|ramazan|mac|normal wins over the date (also with the
// setting off, so a snapshot never depends on the player's file).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>

namespace w3d {

enum SpecialDay { GunNormal = 0, GunRamazanBayrami = 1, GunKurbanBayrami = 2, GunRamazan = 3, GunMac = 4 };
inline bool isBayram(int d) { return d == GunRamazanBayrami || d == GunKurbanBayrami; }

namespace gun {

// days since 1970-01-01 of a Gregorian date (Howard Hinnant's days_from_civil)
inline long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = (long)y - era * 400;
    const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline void civilFromDays(long z, int& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}
constexpr double kJdUnixEpoch = 2440587.5;  // Julian day of 1970-01-01 00:00 UTC

// Julian Ephemeris Day of the k-th new moon after 6 Jan 2000 (Meeus ch. 49).
inline double newMoonJde(double k) {
    const double T = k / 1236.85, T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    const double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * T2 - 0.000000150 * T3 + 0.00000000073 * T4;
    const double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    const double rad = 3.14159265358979323846 / 180.0;
    const double M = (2.5534 + 29.10535670 * k - 0.0000014 * T2 - 0.00000011 * T3) * rad;
    const double Mp = (201.5643 + 385.81693528 * k + 0.0107582 * T2 + 0.00001238 * T3 - 0.000000058 * T4) * rad;
    const double F = (160.7108 + 390.67050284 * k - 0.0016118 * T2 - 0.00000227 * T3 + 0.000000011 * T4) * rad;
    const double O = (124.7746 - 1.56375588 * k + 0.0020672 * T2 + 0.00000215 * T3) * rad;
    using std::sin;
    const double c = -0.40720 * sin(Mp) + 0.17241 * E * sin(M) + 0.01608 * sin(2 * Mp) + 0.01039 * sin(2 * F) +
                     0.00739 * E * sin(Mp - M) - 0.00514 * E * sin(Mp + M) + 0.00208 * E * E * sin(2 * M) -
                     0.00111 * sin(Mp - 2 * F) - 0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M) -
                     0.00042 * sin(3 * Mp) + 0.00042 * E * sin(M + 2 * F) + 0.00038 * E * sin(M - 2 * F) -
                     0.00024 * E * sin(2 * Mp - M) - 0.00017 * sin(O) - 0.00007 * sin(Mp + 2 * M) +
                     0.00004 * sin(2 * Mp - 2 * F) + 0.00004 * sin(3 * M) + 0.00003 * sin(Mp + M - 2 * F) +
                     0.00003 * sin(2 * Mp + 2 * F) - 0.00003 * sin(Mp + M + 2 * F) + 0.00003 * sin(Mp - M + 2 * F) -
                     0.00002 * sin(Mp - M - 2 * F) - 0.00002 * sin(3 * Mp + M) + 0.00002 * sin(4 * Mp);
    return jde + c;
}

// Day number (days since 1970-01-01, as daysFromCivil) of the first day of Hijri month `hm` (1..12) of year `hy`.
inline long hijriMonthStart(int hy, int hm) {
    // the tabular calendar's first day (a Julian day number, i.e. its noon) tells which conjunction it is
    const long tab = 1 + (long)std::ceil(29.5 * (hm - 1)) + (long)(hy - 1) * 354 + (3 + 11L * hy) / 30 + 1948439;
    const double approx = (double)tab - 1.0;  // the conjunction is about a day before
    const double k0 = std::floor((approx - 2451550.09766) / 29.530588861 + 0.5);
    double best = newMoonJde(k0);
    for (double k : {k0 - 1.0, k0 + 1.0}) {
        const double j = newMoonJde(k);
        if (std::fabs(j - approx) < std::fabs(best - approx)) best = j;
    }
    const double utc = best - 69.0 / 86400.0;          // TT -> UTC (ΔT about 69 s)
    const double daysUtc = utc - kJdUnixEpoch;
    const long day = (long)std::floor(daysUtc);
    const double hour = (daysUtc - (double)day) * 24.0;
    return day + (hour < 12.0 ? 1 : 2);
}

// The Hijri year whose Ramazan falls in (or nearest to) Gregorian year y.
inline int hijriYearFor(int gy) { return (int)std::floor((gy - 622) * 33.0 / 32.0) + 1; }

// What kind of day the date is (Gregorian; wday 0 = Sunday), ignoring the hour: Bayram, Ramazan, or GunMac on a Sunday.
inline int dayKind(int y, int m, int d, int wday) {
    const long t = daysFromCivil(y, m, d);
    for (int hy = hijriYearFor(y) - 1; hy <= hijriYearFor(y) + 1; ++hy) {
        const long ram = hijriMonthStart(hy, 9), sev = hijriMonthStart(hy, 10), zil = hijriMonthStart(hy, 12);
        if (t >= sev && t < sev + 3) return GunRamazanBayrami;  // three days
        if (t >= zil + 9 && t < zil + 13) return GunKurbanBayrami;  // four days
        if (t >= ram && t < sev) return GunRamazan;
    }
    return wday == 0 ? GunMac : GunNormal;
}

inline int envDay() {
    const char* v = std::getenv("SAKLI_GUN");
    if (!v || !*v) return -1;
    if (!std::strcmp(v, "bayram") || !std::strcmp(v, "ramazanbayrami")) return GunRamazanBayrami;
    if (!std::strcmp(v, "kurban")) return GunKurbanBayrami;
    if (!std::strcmp(v, "ramazan")) return GunRamazan;
    if (!std::strcmp(v, "mac") || !std::strcmp(v, "maç")) return GunMac;
    return GunNormal;
}

}  // namespace gun

// Today's special day for the setting `on` (ui::Settings::ozelGun) and the resolved day phase (w3d::DayPhase, Daytime.h):
// the derby is an evening thing (akşam, gece); Ramazan is resolved all day (its life is in the evening, callers check).
inline int resolveSpecialDay(bool on, int phase) {
    const int e = gun::envDay();
    if (e >= 0) return e;
    if (!on) return GunNormal;
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    const int k = gun::dayKind(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_wday);
    if (k == GunMac && phase < 2) return GunNormal;  // (Pazar sabahı / öğlesi: no derby yet)
    return k;
}

// Ramazan's evening (after iftar: akşam and gece): the kahvehane fills up.
inline bool ramazanEvening(int day, int phase) { return day == GunRamazan && phase >= 2; }

}  // namespace w3d
