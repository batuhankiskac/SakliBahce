#pragma once
// Özel günler: the kahvehane on a bayram, on Ramazan evenings and on a derby night (ozelgun agent). Header-only, no raylib:
// used by Room (decorations), Characters (better clothes, scarves, more people), App (banter, the sahur davul) and tests.
//
//   Bayram    Ramazan Bayramı (1-3 Şevval) and Kurban Bayramı (10-13 Zilhicce): flags and bunting, lokum and candy
//   Ramazan   the month of Ramazan; its evenings (after iftar) fill the kahvehane, güllaç on the tables, the davul at night
//   Maç       every Sunday evening and night (Pazar akşamı) a derby on the TV
// A bayram wins over Ramazan (they never overlap anyway), both win over the Sunday derby.
//
// The Hijri dates are computed, not tabled: the conjunction from the astronomical new moon (Jean Meeus, "Astronomical
// Algorithms", 2nd ed., ch. 49, the periodic terms without the small planetary ones: a minute or so), then the criterion of
// the 2016 Istanbul unified Hijri calendar that the Diyanet İşleri Başkanlığı follows: the month begins the next day when,
// before 24:00 UTC, the Moon stands at least 5 degrees high and 8 degrees from the Sun at sunset somewhere on Earth
// (the Moon and the Sun from Meeus ch. 47 / 25); or when that happens only in the Americas' evening, provided the
// conjunction came before dawn in New Zealand. The tabular (arithmetical) Islamic calendar picks which conjunction.
// Checked against the Diyanet calendar for 2023-2030 (tests/test_ozelgun.cpp), e.g. Ramazan 1 Mar 2025, 19 Feb 2026,
// 8 Feb 2027; Ramazan Bayramı 30 Mar 2025, 20 Mar 2026, 9 Mar 2027; Kurban Bayramı 16 Jun 2024, 6 Jun 2025, 27 May 2026.
// A day of difference stays possible when the Moon only just reaches the limits; the decorations simply follow the
// computed bayram days (there is no arife of its own).
//
// Developer override for snapshots and tests: SAKLI_GUN=bayram|kurban|ramazan|mac|normal wins over the date (also with the
// setting off, so a snapshot never depends on the player's file).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <map>

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

// The Moon's apparent geocentric ecliptic longitude / latitude (degrees) and distance (km) at Julian Ephemeris Day
// `jde` (Meeus ch. 47 with the larger periodic terms: a few thousandths of a degree, plenty for the crescent).
inline void moonPosition(double jde, double& lon, double& lat, double& distKm) {
    struct TermLR { signed char d, m, mp, f; int l, r; };
    struct TermB { signed char d, m, mp, f; int b; };
    static constexpr TermLR LR[] = {
        {0, 0, 1, 0, 6288774, -20905355}, {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
        {0, 0, 2, 0, 213618, -569925},    {0, 1, 0, 0, -185116, 48888},     {0, 0, 0, 2, -114332, -3149},
        {2, 0, -2, 0, 58793, 246158},     {2, -1, -1, 0, 57066, -152138},   {2, 0, 1, 0, 53322, -170733},
        {2, -1, 0, 0, 45758, -204586},    {0, 1, -1, 0, -40923, -129620},   {1, 0, 0, 0, -34720, 108743},
        {0, 1, 1, 0, -30383, 104755},     {2, 0, 0, -2, 15327, 10321},      {0, 0, 1, 2, -12528, 0},
        {0, 0, 1, -2, 10980, 79661},      {4, 0, -1, 0, 10675, -34782},     {0, 0, 3, 0, 10034, -23210},
        {4, 0, -2, 0, 8548, -21636},      {2, 1, -1, 0, -7888, 24208},      {2, 1, 0, 0, -6766, 30824},
        {1, 0, -1, 0, -5163, -8379},      {1, 1, 0, 0, 4987, -16675},       {2, -1, 1, 0, 4036, -12831},
        {2, 0, 2, 0, 3994, -10445},       {4, 0, 0, 0, 3861, -11650},       {2, 0, -3, 0, 3665, 14403},
        {0, 1, -2, 0, -2689, -7003},      {2, 0, -1, 2, -2602, 0},          {2, -1, -2, 0, 2390, 10056},
        {1, 0, 1, 0, -2348, 6322},        {2, -2, 0, 0, 2236, -9884}};
    static constexpr TermB B[] = {
        {0, 0, 0, 1, 5128122}, {0, 0, 1, 1, 280602}, {0, 0, 1, -1, 277693}, {2, 0, 0, -1, 173237}, {2, 0, -1, 1, 55413},
        {2, 0, -1, -1, 46271}, {2, 0, 0, 1, 32573},  {0, 0, 2, 1, 17198},   {2, 0, 1, -1, 9266},    {0, 0, 2, -1, 8822},
        {2, -1, 0, -1, 8216},  {2, 0, -2, -1, 4324}, {2, 0, 1, 1, 4200},    {2, 1, 0, -1, -3359},   {2, -1, -1, 1, 2463},
        {2, -1, 0, 1, 2211},   {2, -1, -1, -1, 2065}, {0, 1, -1, -1, -1870}, {4, 0, -1, -1, 1828},  {0, 1, 0, 1, -1794}};
    const double rad = 3.14159265358979323846 / 180.0;
    const double T = (jde - 2451545.0) / 36525.0, T2 = T * T;
    const double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2;
    const double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2;
    const double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2;
    const double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2;
    const double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2;
    const double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    const double A1 = 119.75 + 131.849 * T, A2 = 53.09 + 479264.290 * T, A3 = 313.45 + 481266.484 * T;
    double sl = 0, sr = 0, sb = 0;
    for (const TermLR& t : LR) {
        const double a = (t.d * D + t.m * M + t.mp * Mp + t.f * F) * rad;
        const double e = t.m == 0 ? 1.0 : (t.m == 1 || t.m == -1) ? E : E * E;
        sl += t.l * e * std::sin(a);
        sr += t.r * e * std::cos(a);
    }
    for (const TermB& t : B) {
        const double a = (t.d * D + t.m * M + t.mp * Mp + t.f * F) * rad;
        sb += t.b * (t.m == 0 ? 1.0 : (t.m == 1 || t.m == -1) ? E : E * E) * std::sin(a);
    }
    sl += 3958 * std::sin(A1 * rad) + 1962 * std::sin((Lp - F) * rad) + 318 * std::sin(A2 * rad);
    sb += -2235 * std::sin(Lp * rad) + 382 * std::sin(A3 * rad) + 175 * std::sin((A1 - F) * rad) + 175 * std::sin((A1 + F) * rad) +
          127 * std::sin((Lp - Mp) * rad) - 115 * std::sin((Lp + Mp) * rad);
    lon = std::fmod(Lp + sl / 1e6, 360.0);
    lat = sb / 1e6;
    distKm = 385000.56 + sr / 1000.0;
}
// The Sun's apparent ecliptic longitude (degrees; Meeus ch. 25, low accuracy: 0.01 degree). Nutation in longitude is
// left out of both bodies: it shifts them together.
inline double sunLongitude(double jde) {
    const double rad = 3.14159265358979323846 / 180.0;
    const double T = (jde - 2451545.0) / 36525.0;
    const double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    const double M = (357.52911 + 35999.05029 * T - 0.0001537 * T * T) * rad;
    const double C = (1.914602 - 0.004817 * T) * std::sin(M) + (0.019993 - 0.000101 * T) * std::sin(2 * M) + 0.000289 * std::sin(3 * M);
    return std::fmod(L0 + C - 0.00569, 360.0);
}
constexpr double kDeltaT = 69.0 / 86400.0;  // TT - UTC (about 69 s in the 2020s-2030s), days

// The Diyanet's (the 2016 Istanbul unified Hijri calendar's) crescent criterion at one instant `jd` (a UTC Julian
// day): somewhere on Earth the Sun is setting while the Moon stands at least 5 degrees high (topocentric, airless)
// and at least 8 degrees from the Sun. Only sunsets between longitudes lonMin..lonMax (degrees east) count.
// `elongOut` (if given) receives the Moon's elongation, degrees.
inline bool crescentSeen(double jd, double lonMin, double lonMax, double* elongOut = nullptr) {
    const double rad = 3.14159265358979323846 / 180.0;
    const double jde = jd + kDeltaT, T = (jde - 2451545.0) / 36525.0;
    double lm, bm, dist;
    moonPosition(jde, lm, bm, dist);
    const double ls = sunLongitude(jde);
    const double elong = std::acos(std::cos(bm * rad) * std::cos((lm - ls) * rad)) / rad;
    if (elongOut) *elongOut = elong;
    if (elong < 8.0) return false;
    const double eps = (23.439291 - 0.0130042 * T) * rad;
    auto equatorial = [&](double lonDeg, double latDeg, double& ra, double& dec) {
        const double l = lonDeg * rad, b = latDeg * rad;
        ra = std::atan2(std::sin(l) * std::cos(eps) - std::tan(b) * std::sin(eps), std::cos(l));
        dec = std::asin(std::sin(b) * std::cos(eps) + std::cos(b) * std::sin(eps) * std::sin(l));
    };
    double rm, dm, rs, ds;
    equatorial(lm, bm, rm, dm);
    equatorial(ls, 0.0, rs, ds);
    const double parallax = std::asin(6378.14 / dist);
    const double gmst = (280.46061837 + 360.98564736629 * (jd - 2451545.0)) * rad;
    const double sunsetAlt = std::sin(-0.8333 * rad);
    for (int latDeg = -60; latDeg <= 60; latDeg += 2) {  // (the inhabited latitudes)
        const double p = latDeg * rad;
        const double c = (sunsetAlt - std::sin(p) * std::sin(ds)) / (std::cos(p) * std::cos(ds));
        if (c < -1.0 || c > 1.0) continue;  // no sunset there today
        const double lst = rs + std::acos(c);  // local sidereal time where the Sun sets now
        double lonE = std::fmod((lst - gmst) / rad, 360.0);
        if (lonE < -180.0) lonE += 360.0;
        if (lonE >= 180.0) lonE -= 360.0;
        if (lonE < lonMin || lonE > lonMax) continue;
        double alt = std::asin(std::sin(p) * std::sin(dm) + std::cos(p) * std::cos(dm) * std::cos(lst - rm));
        alt -= parallax * std::cos(alt);
        if (alt >= 5.0 * rad) return true;
    }
    return false;
}

// Dawn (fajr: the Sun 18 degrees below the horizon) in New Zealand (Auckland) on the morning that falls in UTC day `jd0`
// (the Julian day of that day's 00:00 UTC), as a UTC Julian day.
inline double fajrNewZealand(double jd0) {
    const double rad = 3.14159265358979323846 / 180.0;
    const double phi = -36.85 * rad, lonE = 174.76;
    auto alt = [&](double jd) {
        const double jde = jd + kDeltaT, T = (jde - 2451545.0) / 36525.0, eps = (23.439291 - 0.0130042 * T) * rad;
        const double l = sunLongitude(jde) * rad;
        const double ra = std::atan2(std::sin(l) * std::cos(eps), std::cos(l)), dec = std::asin(std::sin(eps) * std::sin(l));
        const double H = (280.46061837 + 360.98564736629 * (jd - 2451545.0) + lonE) * rad - ra;
        return std::asin(std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(H));
    };
    double a = jd0 + 12.0 / 24.0, b = jd0 + 20.0 / 24.0;  // local midnight .. well after dawn: the Sun only rises
    for (int i = 0; i < 30; ++i) {
        const double m = 0.5 * (a + b);
        (alt(m) < -18.0 * rad ? a : b) = m;
    }
    return 0.5 * (a + b);
}

// Day number (days since 1970-01-01, as daysFromCivil) of the first day of Hijri month `hm` (1..12) of year `hy`.
inline long hijriMonthStart(int hy, int hm) {
    // (memoised: dayKind asks for the same few months again and again; the search below takes milliseconds)
    static std::map<int, long> memo;
    const int memoKey = hy * 16 + hm;
    if (const auto it = memo.find(memoKey); it != memo.end()) return it->second;
    // the tabular calendar's first day (a Julian day number, i.e. its noon) tells which conjunction it is
    const long tab = 1 + (long)std::ceil(29.5 * (hm - 1)) + (long)(hy - 1) * 354 + (3 + 11L * hy) / 30 + 1948439;
    const double approx = (double)tab - 1.0;  // the conjunction is about a day before
    const double k0 = std::floor((approx - 2451550.09766) / 29.530588861 + 0.5);
    double best = newMoonJde(k0);
    for (double k : {k0 - 1.0, k0 + 1.0}) {
        const double j = newMoonJde(k);
        if (std::fabs(j - approx) < std::fabs(best - approx)) best = j;
    }
    const double conj = best - kDeltaT;  // UTC
    // The month begins the day after the first evening (UTC day D, up to 24:00 UTC) on which the crescent criterion
    // holds somewhere. The rule's exception: when it holds only later in the Americas' evening, that still counts if the
    // conjunction came before dawn in New Zealand.
    // The elongation grows by at most ~0.7 degrees an hour: while it is short of 8 degrees the scan jumps ahead.
    const double step = 5.0 / 1440.0;
    auto scan = [&](double t0, double t1, double lonMin, double lonMax) {
        for (double t = t0; t <= t1;) {
            double el = 0.0;
            if (crescentSeen(t, lonMin, lonMax, &el)) return true;
            t += el < 8.0 ? std::max(step, (8.0 - el) / 0.7 / 24.0) : step;
        }
        return false;
    };
    long D = (long)std::floor(conj - 0.5);  // the UTC day of the conjunction: [D + 0.5, D + 1.5)
    long start = D + 3 - 2440587;          // (never reached: by the third evening the crescent is always up)
    for (int tries = 0; tries < 3; ++tries, ++D) {
        const double day0 = (double)D + 0.5, day1 = day0 + 1.0;
        bool seen = scan(std::max(conj, day0), day1, -180.0, 180.0);
        if (!seen && conj < fajrNewZealand(day0)) seen = scan(day1, day1 + 0.375, -130.0, -30.0);
        if (seen) {
            start = D + 1 - 2440587;  // (the Julian day number D + 1 is day D's date; the month starts the day after)
            break;
        }
    }
    memo[memoKey] = start;
    return start;
}

// The Hijri year (tabular arithmetic; a day or two off at a new year, enough to pick the year around a date) of day
// number `day` (days since 1970-01-01).
inline int hijriYearOfDay(long day) {
    const long jdn = day + 2440588;
    return (int)((30 * (jdn - 1948440) + 10646) / 10631);
}

// What kind of day the date is (Gregorian; wday 0 = Sunday), ignoring the hour: Bayram, Ramazan, or GunMac on a Sunday.
inline int dayKind(int y, int m, int d, int wday) {
    const long t = daysFromCivil(y, m, d);
    const int hy0 = hijriYearOfDay(t);
    for (int hy = hy0 - 1; hy <= hy0 + 1; ++hy) {
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
