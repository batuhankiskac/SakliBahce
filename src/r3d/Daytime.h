#pragma once
// Time of day and season of the kıraathane, shared by Room (light, street, weather, stove) and Characters (how
// many patrons sit at the background tables, winter scarves). Header-only; room owner.
//
// Modes are ui::Settings::dayTime / season: 0 otomatik (from the computer's clock / date), 1.. a fixed choice.
// Developer override for snapshots: with mode 0, the environment variables SAKLI_SAAT (1 sabah .. 4 gece) and
// SAKLI_MEVSIM (1 ilkbahar .. 4 kış) win over the clock.
#include <cstdlib>
#include <ctime>

namespace w3d {

enum DayPhase { Sabah = 0, Ogle = 1, Aksam = 2, Gece = 3 };
enum Season { Ilkbahar = 0, Yaz = 1, Sonbahar = 2, Kis = 3 };

inline int envMode(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return 0;
    int m = std::atoi(v);
    return m >= 1 && m <= 4 ? m : 0;
}

// 06-11 sabah, 11-17 öğle, 17-21 akşam, 21-06 gece.
inline int resolveDayPhase(int mode) {
    if (mode >= 1 && mode <= 4) return mode - 1;
    if (int e = envMode("SAKLI_SAAT")) return e - 1;
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    const int h = lt.tm_hour;
    if (h >= 6 && h < 11) return Sabah;
    if (h >= 11 && h < 17) return Ogle;
    if (h >= 17 && h < 21) return Aksam;
    return Gece;
}

// Mar-May ilkbahar, Jun-Aug yaz, Sep-Nov sonbahar, Dec-Feb kış.
inline int resolveSeason(int mode) {
    if (mode >= 1 && mode <= 4) return mode - 1;
    if (int e = envMode("SAKLI_MEVSIM")) return e - 1;
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    const int m = lt.tm_mon;  // 0 = January
    if (m >= 2 && m <= 4) return Ilkbahar;
    if (m >= 5 && m <= 7) return Yaz;
    if (m >= 8 && m <= 10) return Sonbahar;
    return Kis;
}

// Which background tables (w3d::BG_TABLES) have their patrons at each time of day: the two old men with their tea
// and the tavla players are there from the morning, the card players come at noon, the okey four in the evening.
inline bool bgTableBusy(int table, int phase) {
    switch (phase) {
    case Sabah: return table == 0 || table == 3;
    case Ogle: return table != 1;
    default: return true;
    }
}

}  // namespace w3d
