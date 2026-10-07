#include "ui/Memory.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace ui {

namespace {

// File keys (the same as the record's, istatistik.txt)
const char* const kGameKey[MEMORY_GAMES] = {"101", "esli101", "okey", "tavla", "pisti", "batak", "king", "dama", "altmisalti", "bezik", "konken"};
const char* const kGameName[MEMORY_GAMES] = {"101", "eşli 101", "okey", "tavla", "pişti", "batak", "king", "dama", "altmışaltı", "bezik", "konken"};

bool validSeat(int s) { return s >= 1 && s <= 3; }
bool validGame(int g) { return g >= 0 && g < MEMORY_GAMES; }

// places from per-seat values (better first); seats outside the mask get -1
std::array<int, 4> placesOf(const std::array<int, 4>& v, unsigned mask, bool lowerIsBetter) {
    std::array<int, 4> p{{-1, -1, -1, -1}};
    for (int s = 0; s < 4; ++s) {
        if (!(mask & (1u << s))) continue;
        int better = 0;
        for (int o = 0; o < 4; ++o)
            if (o != s && (mask & (1u << o)) && (lowerIsBetter ? v[o] < v[s] : v[o] > v[s])) ++better;
        p[s] = better;
    }
    return p;
}

} // namespace

// ---------------------------------------------------------------- MatchRecord
MatchRecord MatchRecord::fromScores(int game, const std::array<int, 4>& totals, unsigned seatMask, bool lowerIsBetter,
                                    bool teams) {
    MatchRecord m;
    m.game = game;
    m.hasScore = true;
    if (teams) {
        seatMask = 0xFu;
        m.partner = 2;
        for (int s = 0; s < 4; ++s) m.score[s] = totals[s] + totals[(s + 2) % 4];
    } else {
        m.score = totals;
    }
    m.place = placesOf(m.score, seatMask, lowerIsBetter);
    return m;
}

MatchRecord MatchRecord::fromStandings(int game, const std::vector<int>& seats, int columns, const std::vector<int>& ranking,
                                       const std::vector<int>& rank, const std::vector<std::string>& totals) {
    MatchRecord m;
    m.game = game;
    const bool teamCols = columns > 0 && columns < (int)seats.size();
    auto seatsOf = [&](int col) {
        std::vector<int> out;
        if (!teamCols) {
            if (col >= 0 && col < (int)seats.size()) out.push_back(seats[(size_t)col]);
        } else {
            for (int s : seats)
                if (s >= 0 && s < 4 && s % 2 == col) out.push_back(s);
        }
        return out;
    };
    if (teamCols) m.partner = 2;
    for (size_t i = 0; i < ranking.size(); ++i) {
        const int place = i < rank.size() ? std::max(0, rank[i] - 1) : (int)i;
        for (int s : seatsOf(ranking[i]))
            if (s >= 0 && s < 4) m.place[s] = place;
    }
    bool numbers = !totals.empty() && (int)totals.size() == columns;
    for (const std::string& t : totals) {
        char* end = nullptr;
        std::strtol(t.c_str(), &end, 10);
        numbers = numbers && end != t.c_str();
    }
    if (numbers) {
        m.hasScore = true;
        for (int c = 0; c < columns; ++c)
            for (int s : seatsOf(c))
                if (s >= 0 && s < 4) m.score[s] = std::atoi(totals[(size_t)c].c_str());
    }
    return m;
}

// ---------------------------------------------------------------- recording
void Memory::beginSession(int day) {
    prevVisit = lastVisit;
    if (firstDay < 0) firstDay = day;
    lastVisit = day;
    ++visits;
}

void Memory::recordMatch(const MatchRecord& m, int day) {
    if (!validGame(m.game) || m.place[0] < 0) return;
    ++gameMatches[m.game];
    if (firstDay < 0) firstDay = day;
    if (lastVisit < day) lastVisit = day;
    // who won: a unique first place; eşli: a side (the player + partner against seats 1 + 3); a shared first: nobody
    bool playerWon = false, othersWon = false;
    int winner = -1;
    if (m.partner >= 0) {
        const bool us = m.place[0] == 0, them = m.place[1] == 0 || m.place[3] == 0;
        playerWon = us && !them;
        othersWon = them && !us;
    } else {
        int firsts = 0;
        for (int s = 0; s < 4; ++s)
            if (m.place[s] == 0) {
                ++firsts;
                winner = s;
            }
        if (firsts != 1) winner = -1;
        playerWon = winner == 0;
        othersWon = winner > 0;
    }
    for (int s = 1; s <= 3; ++s) {
        if (m.place[s] < 0) continue;
        VsRecord& r = vs[s][m.game];
        r.lastDay = day;
        if (s == m.partner) {
            ++r.together;
            if (playerWon) ++r.togetherWins;
            r.last = playerWon ? MatchResult::PlayerWon : MatchResult::Other; // lost together: nobody to tease
            continue;
        }
        ++r.played;
        // in eşli the opponents win as a team: both of them won
        const bool heWon = m.partner >= 0 ? othersWon : winner == s;
        if (playerWon) ++r.playerWins;
        else if (heWon) ++r.theirWins;
        r.last = playerWon ? MatchResult::PlayerWon : heWon ? MatchResult::TheyWon : MatchResult::Other;
        if (m.hasScore) {
            const int margin = std::abs(m.score[0] - m.score[s]);
            if (playerWon) r.bestWin = std::max(r.bestWin, margin);
            else if (heWon) r.worstLoss = std::max(r.worstLoss, margin);
        }
    }
}

void Memory::noteMoment(MomentKind k, int by, int to, int game, int value, int day) {
    if (k == MomentKind::None || !validGame(game)) return;
    const bool byPlayer = by == 0;
    const int seat = byPlayer ? to : by;
    if (byPlayer == (to == 0) || !validSeat(seat)) return; // exactly one side must be the player
    moment[seat] = Moment{k, byPlayer, game, day, value};
}

bool Memory::noteRank(int key) {
    const bool risen = rank >= 0 && key > rank;
    if (key > rank) rank = key;
    return risen;
}

// ---------------------------------------------------------------- questions
const VsRecord& Memory::with(int seat, int game) const {
    static const VsRecord kNone{};
    if (!validSeat(seat) || !validGame(game)) return kNone;
    return vs[seat][game];
}

int Memory::matchesTotal() const {
    int n = 0;
    for (int c : gameMatches) n += c;
    return n;
}

int Memory::matchesWith(int seat) const {
    if (!validSeat(seat)) return 0;
    int n = 0;
    for (const VsRecord& r : vs[seat]) n += r.played + r.together;
    return n;
}

int Memory::daysAway(int day) const { return prevVisit < 0 ? -1 : std::max(0, day - prevVisit); }

int Memory::favouriteGame() const {
    int best = -1;
    for (int g = 0; g < MEMORY_GAMES; ++g)
        if (gameMatches[g] > 0 && (best < 0 || gameMatches[g] > gameMatches[best])) best = g;
    return best;
}

int Memory::today() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    // days from the civil date (Howard Hinnant's days_from_civil), independent of the time zone's offset
    int y = lt.tm_year + 1900;
    const unsigned mo = (unsigned)lt.tm_mon + 1, d = (unsigned)lt.tm_mday;
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

const char* Memory::gameName(int game) { return validGame(game) ? kGameName[game] : ""; }

// ---------------------------------------------------------------- file
// Lines: "ilkgun=…", "songun=…", "ziyaret=…", "rutbe=…", "<game>.mac=…", "s<seat>.<game>.<field>=…",
// "s<seat>.an=<kind>,<byPlayer>,<game>,<day>,<value>".
void Memory::load(const std::string& path) {
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    auto gameIndex = [](const std::string& g) {
        for (int i = 0; i < MEMORY_GAMES; ++i)
            if (g == kGameKey[i]) return i;
        return -1;
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int iv = std::atoi(v.c_str());
        if (k == "ilkgun") firstDay = iv;
        else if (k == "songun") lastVisit = iv;
        else if (k == "ziyaret") visits = std::max(0, iv);
        else if (k == "rutbe") rank = iv;
        else if (k.size() > 4 && k.compare(k.size() - 4, 4, ".mac") == 0 && k[0] != 's') {
            const int g = gameIndex(k.substr(0, k.size() - 4));
            if (g >= 0) gameMatches[g] = std::max(0, iv);
        } else if (k.size() > 3 && k[0] == 's' && k[1] >= '1' && k[1] <= '3' && k[2] == '.') {
            const int seat = k[1] - '0';
            const std::string rest = k.substr(3);
            if (rest == "an") {
                int f[5] = {0, 1, -1, -1, 0};
                std::istringstream ss(v);
                std::string part;
                for (int i = 0; i < 5 && std::getline(ss, part, ','); ++i) f[i] = std::atoi(part.c_str());
                if (f[0] >= 1 && f[0] <= 2 && validGame(f[2]))
                    moment[seat] = Moment{(MomentKind)f[0], f[1] != 0, f[2], f[3], f[4]};
                continue;
            }
            const size_t dot = rest.find('.');
            if (dot == std::string::npos) continue;
            const int g = gameIndex(rest.substr(0, dot));
            if (g < 0) continue;
            const std::string fld = rest.substr(dot + 1);
            VsRecord& r = vs[seat][g];
            if (fld == "mac") r.played = std::max(0, iv);
            else if (fld == "kazandi") r.playerWins = std::max(0, iv);
            else if (fld == "kaybetti") r.theirWins = std::max(0, iv);
            else if (fld == "ortak") r.together = std::max(0, iv);
            else if (fld == "ortakkazanc") r.togetherWins = std::max(0, iv);
            else if (fld == "son") r.last = (MatchResult)std::clamp(iv, 0, 3);
            else if (fld == "songun") r.lastDay = iv;
            else if (fld == "enbuyukfark") r.bestWin = std::max(0, iv);
            else if (fld == "enagirfark") r.worstLoss = std::max(0, iv);
        }
    }
}

void Memory::save(const std::string& path) const {
    if (path.empty()) return;
    std::ostringstream o;
    o << "# SaklıBahçe: müdavimlerin hafızası\n";
    o << "ilkgun=" << firstDay << "\nsongun=" << lastVisit << "\nziyaret=" << visits << "\nrutbe=" << rank << "\n";
    for (int g = 0; g < MEMORY_GAMES; ++g)
        if (gameMatches[g]) o << kGameKey[g] << ".mac=" << gameMatches[g] << "\n";
    for (int s = 1; s <= 3; ++s) {
        for (int g = 0; g < MEMORY_GAMES; ++g) {
            const VsRecord& r = vs[s][g];
            if (!r.played && !r.together) continue;
            const std::string p = "s" + std::to_string(s) + "." + kGameKey[g] + ".";
            o << p << "mac=" << r.played << "\n" << p << "kazandi=" << r.playerWins << "\n" << p << "kaybetti=" << r.theirWins
              << "\n" << p << "ortak=" << r.together << "\n" << p << "ortakkazanc=" << r.togetherWins << "\n" << p
              << "son=" << (int)r.last << "\n" << p << "songun=" << r.lastDay << "\n" << p << "enbuyukfark=" << r.bestWin
              << "\n" << p << "enagirfark=" << r.worstLoss << "\n";
        }
        const Moment& mo = moment[s];
        if (mo.kind != MomentKind::None)
            o << "s" << s << ".an=" << (int)mo.kind << "," << (mo.byPlayer ? 1 : 0) << "," << mo.game << "," << mo.day << ","
              << mo.value << "\n";
    }
    std::ofstream out(path);
    out << o.str();
}

} // namespace ui
