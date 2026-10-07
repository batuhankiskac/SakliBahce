#pragma once
// The player's small text files (ayarlar, istatistik, hafiza, basarimlar, kayit, tekrarlar): what their readers and
// writers share. Header only (no raylib), so the tests that compile ui/*.cpp straight in need nothing more.
//  - writeFileAtomic: the text goes to "<path>.tmp", is flushed (and fsync'd) and only then renamed over the file, so a
//    crash or a full disk never leaves a half-written book behind (the old one stays).
//  - forEachKeyValue: "key=value" lines (empty and '#' lines skipped; nothing trimmed).
//  - parseLong: a number from a line, clamped (no overflow, junk gives the fallback).
//  - kGameKeys: the games' keys in istatistik.txt / hafiza.txt (GameKind order).
//  - daysFromCivil / todayDays: a date as a day number (Howard Hinnant's days_from_civil).
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ui {

constexpr int SAVE_GAMES = 11; // GameKind::Count
constexpr const char* kGameKeys[SAVE_GAMES] = {"101", "esli101", "okey", "tavla", "pisti", "batak",
                                               "king", "dama", "altmisalti", "bezik", "konken"};

inline int gameKeyIndex(const std::string& key) {
    for (int i = 0; i < SAVE_GAMES; ++i)
        if (key == kGameKeys[i]) return i;
    return -1;
}

// Writes `text` to `path` through "<path>.tmp" and a rename. False (the old file untouched) on any failure.
inline bool writeFileAtomic(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fflush(f) == 0 && ok;
#if !defined(_WIN32)
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
#if defined(_WIN32)
        std::remove(path.c_str()); // (rename does not replace on Windows)
#endif
        ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    }
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

// The whole file, or false when it cannot be read.
inline bool readFileText(const std::string& path, std::string& out) {
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream all;
    all << in.rdbuf();
    out = all.str();
    return true;
}

// Every "key=value" line of `text` (split at the first '='; empty lines and '#' comments skipped).
inline void forEachKeyValue(const std::string& text, const std::function<void(const std::string&, const std::string&)>& fn) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        fn(line.substr(0, eq), line.substr(eq + 1));
    }
}

// A decimal number clamped to [lo, hi]; `fallback` when there is no number at all.
inline long parseLong(const std::string& v, long lo, long hi, long fallback = 0) {
    errno = 0;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return std::clamp(fallback, lo, hi);
    if (errno == ERANGE) return n < 0 ? lo : hi;
    return std::clamp(n, lo, hi);
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil), independent of the time zone's offset.
inline int daysFromCivil(int y, unsigned mo, unsigned d) {
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

// Today (the local date) as a day number.
inline int todayDays() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return daysFromCivil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday);
}

} // namespace ui
