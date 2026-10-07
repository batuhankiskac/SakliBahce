#include "ui/Stats.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ui {

namespace {

constexpr Rank kRanks[] = {
    {"Yeni Gelen", "Kapıdan yeni girdi, çayını kendisi söylüyor.", 0},
    {"Çaylak", "Masaya oturmasına ses çıkarmıyorlar artık.", 3},
    {"Müdavim", "Ocakçı çayını sormadan getiriyor.", 10},
    {"Masanın Ustası", "Oynarken arkasında seyirci birikiyor.", 25},
    {"Kahvehane Ağası", "Masası ayrılıyor, adı duvarda yazılı.", 50},
    {"Efsane", "Mahallede adı anılıyor; Nuri Abi bile hak veriyor.", 100},
};
constexpr int kRankCount = (int)(sizeof kRanks / sizeof kRanks[0]);

// File keys: "<game>.<field>"
const char* kGameKey[STATS_GAMES] = {"101", "esli101", "okey", "tavla", "pisti", "batak", "king", "dama", "altmisalti", "bezik", "konken"};

} // namespace

void StatsBook::hand(int game, bool won, bool hasScore, int score, bool lowerIsBetter) {
    if (game < 0 || game >= STATS_GAMES) return;
    GameRecord& r = games[game];
    ++r.hands;
    if (won) ++r.handWins;
    if (hasScore && bestLabel(game)) {
        if (!r.hasBest || (lowerIsBetter ? score < r.best : score > r.best)) r.best = score;
        r.hasBest = true;
    }
}

void StatsBook::match(int game, int level, bool won) {
    if (game < 0 || game >= STATS_GAMES) return;
    GameRecord& r = games[game];
    level = std::clamp(level, 0, 2);
    ++r.matches;
    ++r.playedAt[level];
    if (won) {
        ++r.wins;
        ++r.winsAt[level];
        ++r.streak;
        r.bestStreak = std::max(r.bestStreak, r.streak);
    } else {
        r.streak = 0;
    }
}

GameRecord StatsBook::total() const {
    GameRecord t;
    for (const GameRecord& r : games) {
        t.matches += r.matches;
        t.wins += r.wins;
        t.hands += r.hands;
        t.handWins += r.handWins;
        t.bestStreak = std::max(t.bestStreak, r.bestStreak);
        for (int l = 0; l < 3; ++l) {
            t.winsAt[l] += r.winsAt[l];
            t.playedAt[l] += r.playedAt[l];
        }
    }
    return t;
}

int StatsBook::rankPoints() const {
    const GameRecord t = total();
    return t.winsAt[0] + 2 * t.winsAt[1] + 3 * t.winsAt[2];
}

const Rank& StatsBook::rankFor(int points) {
    int i = 0;
    while (i + 1 < kRankCount && points >= kRanks[i + 1].points) ++i;
    return kRanks[i];
}

const Rank* StatsBook::nextRank(int points) {
    for (const Rank& r : kRanks)
        if (r.points > points) return &r;
    return nullptr;
}

const char* StatsBook::bestLabel(int game) {
    switch (game) {
    case 0:
    case 1: return "En iyi el";       // the lowest hand score (a finish with a multiplier)
    case 3: return "En büyük oyun";   // points of one game (mars, the cube)
    case 4: return "En çok puan";     // one hand's points
    case 5: return "En iyi el";       // one hand's score
    case 6: return "En iyi el";       // one deal's score
    case 7: return "En temiz oyun";   // Dama: the most pieces left standing in a won game
    case 8: return "En çok puan";     // Altmışaltı: one hand's card points (marriages in)
    case 9: return "En iyi el";       // Bezik: one deal's points (combinations, brisks, the last trick)
    case 10: return "En büyük bitiş"; // Konken: what the others wrote in a hand the player finished (a konken doubles it)
    default: return nullptr;
    }
}

void StatsBook::load(const std::string& path) {
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('='), dot = line.find('.');
        if (eq == std::string::npos || dot == std::string::npos || dot > eq) continue;
        const std::string g = line.substr(0, dot), k = line.substr(dot + 1, eq - dot - 1);
        const int v = std::atoi(line.c_str() + eq + 1);
        int gi = -1;
        for (int i = 0; i < STATS_GAMES; ++i)
            if (g == kGameKey[i]) gi = i;
        if (gi < 0) continue;
        GameRecord& r = games[gi];
        if (k == "mac") r.matches = v;
        else if (k == "galibiyet") r.wins = v;
        else if (k == "el") r.hands = v;
        else if (k == "elkazanc") r.handWins = v;
        else if (k == "seri") r.streak = v;
        else if (k == "enuzunseri") r.bestStreak = v;
        else if (k == "rekor") {
            r.best = v;
            r.hasBest = true;
        } else {
            for (int l = 0; l < 3; ++l) {
                if (k == "galibiyet" + std::to_string(l)) r.winsAt[l] = v;
                if (k == "mac" + std::to_string(l)) r.playedAt[l] = v;
            }
        }
    }
}

void StatsBook::save(const std::string& path) const {
    if (path.empty()) return;
    std::ostringstream o;
    o << "# SaklıBahçe istatistikleri\n";
    for (int i = 0; i < STATS_GAMES; ++i) {
        const GameRecord& r = games[i];
        if (!r.matches && !r.hands) continue;
        const std::string g = kGameKey[i];
        o << g << ".mac=" << r.matches << "\n" << g << ".galibiyet=" << r.wins << "\n" << g << ".el=" << r.hands << "\n"
          << g << ".elkazanc=" << r.handWins << "\n" << g << ".seri=" << r.streak << "\n" << g
          << ".enuzunseri=" << r.bestStreak << "\n";
        for (int l = 0; l < 3; ++l)
            o << g << ".mac" << l << "=" << r.playedAt[l] << "\n" << g << ".galibiyet" << l << "=" << r.winsAt[l] << "\n";
        if (r.hasBest) o << g << ".rekor=" << r.best << "\n";
    }
    std::ofstream out(path);
    out << o.str();
}

} // namespace ui
