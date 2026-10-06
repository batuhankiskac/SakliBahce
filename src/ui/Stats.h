#pragma once
// The player's record at the kahvehane: per game the matches and hands played and won, streaks and the wins at each
// level, and a rank (rütbe) earned with the wins. App records finished matches the player played himself (a match in
// which the Yapay Zeka mode took over is not counted); the İstatistikler screen shows it. Kept in a small key=value
// text file next to the settings.
#include <array>
#include <string>

namespace ui {

constexpr int STATS_GAMES = 7; // GameKind::Count

struct GameRecord {
    int matches = 0, wins = 0;     // finished matches / won (eşli: the team won)
    int hands = 0, handWins = 0;   // hands (tavla: games, King: deals) / the ones the player won
    int streak = 0, bestStreak = 0; // matches won in a row (now / best)
    std::array<int, 3> winsAt{};   // match wins against Acemi / Usta / Kurt
    std::array<int, 3> playedAt{}; // matches against Acemi / Usta / Kurt
    int best = 0;                  // the game's best hand (see bestLabel); 0 = none yet
    bool hasBest = false;
};

struct Rank {
    const char* name;   // "Müdavim"
    const char* line;   // what the regulars say about it
    int points;         // rank points needed
};

class StatsBook {
public:
    std::array<GameRecord, STATS_GAMES> games{};

    // ---- recording (game = GameKind index, level 0..2)
    void hand(int game, bool won, bool hasScore = false, int score = 0, bool lowerIsBetter = false);
    void match(int game, int level, bool won);

    // ---- totals and rank: a match win is worth 1 / 2 / 3 rank points against Acemi / Usta / Kurt
    GameRecord total() const;
    int rankPoints() const;
    static const Rank& rankFor(int points);
    static const Rank* nextRank(int points); // null at the top
    static const char* bestLabel(int game);  // "En düşük el" (101), "En çok el" (batak), ... or null

    // ---- file (the path is chosen by App; empty: nothing is read or written)
    void load(const std::string& path);
    void save(const std::string& path) const;
};

} // namespace ui
