#pragma once
// The regulars' memory of the player (rakiplerin hafızası): per regular (seats 1..3) and per game the matches played
// together, who won them, the last result and its day, the biggest win / loss margins, the last memorable moment
// between them (a mars, an okey finish), plus the visits (first / previous / this one) and the player's rank as last
// seen. Banter reads it (Banter::setMemory) for rematch / teasing / "long time no see" lines. Kept by App in a small
// key=value text file next to the record (like StatsBook); pure logic, no raylib.
#include <array>
#include <string>
#include <vector>

namespace ui {

constexpr int MEMORY_GAMES = 11; // GameKind::Count (101, eşli 101, okey, tavla, pişti, batak, king, dama, 66, bezik, konken)

enum class MatchResult { None = 0, PlayerWon = 1, TheyWon = 2, Other = 3 }; // seen from one regular's record

enum class MomentKind { None = 0, Mars = 1, OkeyFinish = 2 };

// The player and one regular in one game.
struct VsRecord {
    int played = 0;          // matches at the same table (as opponents)
    int playerWins = 0;      // ... the player won the match
    int theirWins = 0;       // ... this regular won it (the rest: somebody else / shared)
    int together = 0;        // eşli: matches as the player's partner
    int togetherWins = 0;
    MatchResult last = MatchResult::None;
    int lastDay = -1;        // Memory::today() of the last match together
    int bestWin = 0;         // the biggest score margin of a match the player won against him
    int worstLoss = 0;       // ... of a match he won against the player
};

// The last memorable moment between the player and a regular.
struct Moment {
    MomentKind kind = MomentKind::None;
    bool byPlayer = true;    // the player did it to him (false: he did it to the player)
    int game = -1;
    int day = -1;
    int value = 0;           // optional (points of the mars, ...)
};

// One finished match as App sees it. Places: 0 = first (ties share a place), -1 = not at the table.
struct MatchRecord {
    int game = 0;
    std::array<int, 4> place{{-1, -1, -1, -1}};
    std::array<int, 4> score{};    // final totals per seat (only with hasScore)
    bool hasScore = false;
    int partner = -1;              // eşli: the player's partner seat (2), -1 none

    // From final totals of the seats in `seatMask` (bit s = seat s plays). `teams`: seats 0+2 against 1+3 (team totals
    // decide). lowerIsBetter: 101.
    static MatchRecord fromScores(int game, const std::array<int, 4>& totals, unsigned seatMask, bool lowerIsBetter,
                                  bool teams);
    // From a final standings sheet (ui::SheetModel): `seats` = the seats at the table (TableGame::seats()), `columns` =
    // the sheet's column count (fewer columns than seats: teams, column c = the seats s with s % 2 == c), `ranking` =
    // column order best first, `rank` = 1-based place of each row of `ranking`, `totals` = per column (numbers; others
    // are ignored, empty: no scores).
    static MatchRecord fromStandings(int game, const std::vector<int>& seats, int columns, const std::vector<int>& ranking,
                                     const std::vector<int>& rank, const std::vector<std::string>& totals = {});
};

class Memory {
public:
    std::array<std::array<VsRecord, MEMORY_GAMES>, 4> vs{}; // [seat 1..3][game] (index 0 unused)
    std::array<Moment, 4> moment{};                          // [seat 1..3]
    std::array<int, MEMORY_GAMES> gameMatches{};             // the player's finished matches per game
    int firstDay = -1;      // the first visit
    int lastVisit = -1;     // the day of the latest visit (this session's day once beginSession ran)
    int prevVisit = -1;     // the visit before this session (not saved: derived at beginSession)
    int visits = 0;
    int rank = -1;          // the player's rank as last seen: any key that grows with the rank (App: the rank's
                            // threshold, StatsBook::rankFor(points).points); -1: never seen

    // ---- recording
    void beginSession(int day = today());            // app start: counts a visit, remembers the previous one
    void recordMatch(const MatchRecord& m, int day = today());
    // `by` did something memorable to `to` (seats 0..3; 0 = the player; one of them must be the player)
    void noteMoment(MomentKind k, int by, int to, int game, int value = 0, int day = today());
    bool noteRank(int key);                          // true when the rank went up (not on the first sighting)

    // ---- questions
    const VsRecord& with(int seat, int game) const;
    int matchesTotal() const;                        // all games
    int matchesWith(int seat) const;                 // with one regular, all games (opponent or partner)
    bool firstMeeting() const { return matchesTotal() == 0; }
    int daysAway(int day = today()) const;           // days since the previous visit; -1 on the first one
    int favouriteGame() const;                       // most played game (-1: none yet)

    static int today();                              // local calendar day number (days since 1970-01-01)
    static const char* gameName(int game);           // "101", "eşli 101", "okey", "tavla", "pişti", "batak", "king"

    // ---- file (the path is chosen by App; empty: nothing is read or written)
    void load(const std::string& path);
    bool save(const std::string& path) const; // atomic (ui/SaveFile.h); false on failure
};

} // namespace ui
