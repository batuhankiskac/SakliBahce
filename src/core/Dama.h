#pragma once
// Dama (Türk daması) game engine: board, legal moves (forced capture, the majority rule, the 180° rule), turn flow,
// draws, the match. Pure logic: no raylib, no I/O, deterministic for a given seed (same seed + same actions -> same
// game). The rules are documented in docs/kurallar_dama.md.
//
// ------------------------------------------------------------------------------------------------------
// BOARD INDEX CONVENTION (absolute, the same for both players and for the UI)
//
//   Squares 0..63, s = row * 8 + col, seen from player 0 (the human, at the bottom of the board):
//     row 0 is player 0's own back row (nearest to him), row 7 the far one; col 0 is his left, col 7 his right.
//     Notation: column letters a..h (left to right), row numbers 1..8 (bottom to top): square 0 = "a1".
//   * Player 0's men move UP the rows (+1) and promote on row 7; player 1's men move DOWN (-1) and promote on row 0.
//   * Start: player 0's 16 men on rows 1 and 2, player 1's on rows 6 and 5; rows 0 and 7 are empty.
//   * Who starts a game plays the light pieces ("beyaz başlar"): game 0's starter comes from the seed, then the
//     starter alternates. The colour is only a picture; the engine knows players 0 and 1.
// ------------------------------------------------------------------------------------------------------
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dama {

constexpr int kSize = 8;
constexpr int kSquares = 64;
constexpr int kMen = 16;

inline int sq(int row, int col) { return row * kSize + col; }
inline int rowOf(int s) { return s / kSize; }
inline int colOf(int s) { return s % kSize; }
inline bool onBoard(int row, int col) { return row >= 0 && row < kSize && col >= 0 && col < kSize; }
inline int forwardOf(int p) { return p == 0 ? +1 : -1; } // the row step of p's men
inline int promotionRow(int p) { return p == 0 ? kSize - 1 : 0; }
// How far p's man on `row` has come from p's own back row (0..7).
inline int advance(int p, int row) { return p == 0 ? row : kSize - 1 - row; }

// The board. c[s]: 0 empty, +1 a man of player 0, +2 a dama (king) of player 0, -1 / -2 the same for player 1.
struct Board {
    std::array<int8_t, kSquares> c{};

    static Board initial();
    int owner(int s) const { return c[(size_t)s] > 0 ? 0 : (c[(size_t)s] < 0 ? 1 : -1); } // -1 = empty
    bool king(int s) const { return c[(size_t)s] == 2 || c[(size_t)s] == -2; }
    bool empty(int s) const { return c[(size_t)s] == 0; }
    int pieces(int p) const; // men + kings of p
    int kings(int p) const;
    int men(int p) const { return pieces(p) - kings(p); }
    uint64_t hash(int toMove) const; // Zobrist key of the board with `toMove` to play
    bool operator==(const Board& o) const { return c == o.c; }
    bool operator!=(const Board& o) const { return c != o.c; }
};

// One whole move: the piece on `from` goes through the landing squares `path` (one square for a plain move, one per
// jump for a capture), taking the pieces on `captured` (in order, removed one by one as they are jumped).
struct Move {
    int from = -1;
    std::vector<int> path;
    std::vector<int> captured;
    bool promotes = false; // a man ends the move on its promotion row and becomes a dama
    int to() const { return path.empty() ? from : path.back(); }
    bool isCapture() const { return !captured.empty(); }
    bool sameAs(const Move& o) const { return from == o.from && path == o.path; }
};

// Every legal move of player p on `b`: if any capture is possible only the captures that take the most pieces (men
// and damas count the same), else every plain move. Deterministic order. Empty: p cannot move (he has lost).
std::vector<Move> generateMoves(const Board& b, int p);
// The same moves in a compact, allocation-free form (the bots' search): `n` landing squares, `ncap` captured squares.
struct CMove {
    int8_t from = -1, n = 0, ncap = 0;
    bool promotes = false;
    std::array<int8_t, 16> path{}, cap{};
    int to() const { return n > 0 ? path[(size_t)n - 1] : from; }
};
void generateCompact(const Board& b, int p, std::vector<CMove>& out); // same set and order as generateMoves
// A capture or a man's step forward (on `b`, before the move): the positions before it can never come back (a man's
// sideways step can be undone by the next one). The engine's repetition / no-progress rules and the bots share it.
inline bool isIrreversible(const Board& b, int from, int to, bool capture) {
    return capture || (!b.king(from) && rowOf(to) != rowOf(from));
}
inline bool isIrreversible(const Board& b, const CMove& m) { return isIrreversible(b, m.from, m.to(), m.ncap > 0); }
Move toMove(const CMove& m);
void applyCompact(Board& b, int p, const CMove& m);
// The same, keeping `key` (b.hash(p) before) the Zobrist key of the new board with 1 - p to move.
void applyCompact(Board& b, int p, const CMove& m, uint64_t& key);
// Only whether p has a capture (cheaper than generateMoves).
bool hasCapture(const Board& b, int p);
// Applies a move from generateMoves (no legality check) to b.
void applyMoveTo(Board& b, int p, const Move& m);

std::string squareName(int s);         // "a1".."h8"
std::string moveNotation(const Move& m); // "c3-c4", "d3xd5xf5" (x = a jump), "...=D" when it promotes

struct Rules {
    int winsNeeded = 3;            // the match: first to win this many games (1 / 3 / 5)
    // One piece each (and no capture to make): a draw ("birer taş kaldı"). With loneKingWins a lone dama against a lone
    // man is a win for the dama's side instead (an old kahvehane custom; off by default).
    bool loneKingWins = false;
    int noProgressPlies = 50;      // this many plies with no capture and no man stepping forward: a draw (25 moves each)
    int repetitions = 3;           // the same position with the same player to move this many times: a draw
    // The match also ends after maxGames() games (draws do not count for anybody): the one with more wins, then the one
    // who took more pieces over the match, takes it; still level: the match is drawn.
    int maxGames() const { return 2 * winsNeeded + 1; }
};

struct PlayerInfo {
    std::string name;
    bool human = false;
    int score = 0;    // games won in this match
    int taken = 0;    // pieces taken over the match
};

enum class Stage {
    NotStarted, // before startMatch
    Playing,    // current() must move
    GameOver,   // see lastResult(); call startNextGame()
    MatchOver   // see matchWinner()
};

enum class EndReason {
    None,
    NoPieces,   // the loser has no piece left
    Blocked,    // the loser cannot move
    LoneMan,    // a lone dama against a lone man (Rules::loneKingWins)
    OneEach,    // one piece each: a draw
    Repetition, // the same position for the third time: a draw
    NoProgress  // Rules::noProgressPlies without a capture or a man stepping forward: a draw
};

struct GameResult {
    int winner = -1;              // -1: a draw
    EndReason reason = EndReason::None;
    std::array<int, 2> left{};    // pieces left on the board
    std::array<int, 2> kingsLeft{};
    int plies = 0;
    int gameIndex = 0;
};

enum class EvType {
    MatchStart, // amount = winsNeeded
    GameStart,  // amount = game index (0-based); player = the starter (light pieces)
    Move,       // player moved from -> to along path, taking `captured` (THE event to animate)
    Promote,    // follows Move: the piece on `to` became a dama
    GameEnd,    // player = winner (-1 a draw), amount = (int)EndReason
    MatchEnd    // player = match winner (-1 a drawn match)
};

struct GameEvent {
    EvType type = EvType::Move;
    int player = -1;
    int from = -1, to = -1;
    std::vector<int> path, captured;
    bool promotes = false;
    int amount = 0;
    std::string text; // short Turkish text; second person for the human ("3 taş aldın")
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing ("Almak zorundasın")
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// One line of the game's record.
struct MoveRecord {
    int player = -1;
    Move move;
};

// ---- save / resume ----
enum class ActKind { Move, NextGame };
struct LoggedAction {
    ActKind kind = ActKind::Move;
    int player = -1;
    int from = -1;
    std::vector<int> path;
    std::string encode() const; // "m 0 10 18", "m 1 45 29 13" (a capture), "n"
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);                 // only between matches (ignored while a match is running)
    const Rules& rules() const { return rules_; }
    void setPlayer(int p, const std::string& name, bool human); // p = 0 (bottom) or 1 (across)
    void startMatch(uint64_t seed);                // scores 0, game 0 -> Playing
    void startNextGame();                          // after GameOver (not MatchOver)

    // ---- queries ----
    Stage stage() const { return stage_; }
    int current() const { return current_; }       // the player to move
    int starter() const { return starter_; }       // who started this game (plays the light pieces)
    int gameIndex() const { return gameIndex_; }   // 0-based
    const PlayerInfo& player(int p) const { return players_[(size_t)p]; }
    int score(int p) const { return players_[(size_t)p].score; }
    int winsNeeded() const { return rules_.winsNeeded; }
    int matchWinner() const { return matchWinner_; } // -1 until MatchOver (and for a drawn match)
    const Board& board() const { return board_; }
    const GameResult& lastResult() const { return lastResult_; }
    int plies() const { return plies_; }            // plies of this game
    int quietPlies() const { return quiet_; }       // plies since the last capture or man's move
    int turnNumber() const { return turnNumber_; }  // increments at every move of the match
    // Legal moves of the player to move (cached; empty unless Playing).
    const std::vector<Move>& legalMoves() const { return legal_; }
    // The current game's moves, oldest first.
    const std::vector<MoveRecord>& gameLog() const { return log_; }
    // Position keys since the last irreversible move (a capture or a man's step forward), oldest first, the current one last.
    const std::vector<uint64_t>& history() const { return hist_; }

    // ---- actions ----
    // Plays one of legalMoves() (matched by from + path). The error says why another move is not legal.
    ActionResult applyMove(int p, const Move& m);
    // The same for a move given by its squares only: `from` and the landing squares.
    ActionResult applyMove(int p, int from, const std::vector<int>& path);

    // ---- save / resume ----
    const std::vector<LoggedAction>& actionLog() const { return actions_; } // since startMatch
    bool replay(const LoggedAction& a); // applies one logged action (false: it does not apply here)

    // ---- events ----
    std::vector<GameEvent> drainEvents();
    const std::vector<GameEvent>& pendingEvents() const { return events_; }

    // ---- testing hooks: tests/ and tools/ only ----
    // Replaces the board and the player to move (Playing; the history restarts there).
    void debugSetBoard(const Board& b, int toMove);

private:
    void beginGame();
    void afterMove(int p, const Move& m);
    void endGame(int winner, EndReason why);
    std::string says(int p, const std::string& third, const std::string& second) const;
    std::string nameOf(int p) const { return players_[(size_t)p].name; }
    void push(GameEvent e);
    std::string moveError(int p, const Move& m) const;

    Rules rules_;
    std::array<PlayerInfo, 2> players_;
    okey::Rng rng_;
    Stage stage_ = Stage::NotStarted;
    int current_ = 0, starter_ = 0, firstStarter_ = 0;
    int gameIndex_ = 0, plies_ = 0, quiet_ = 0, turnNumber_ = 0;
    int matchWinner_ = -1;
    Board board_;
    std::vector<Move> legal_;
    GameResult lastResult_;
    std::vector<GameEvent> events_;
    std::vector<MoveRecord> log_;
    std::vector<LoggedAction> actions_;
    std::vector<uint64_t> hist_;
};

const char* endReasonText(EndReason r); // "taşı kalmadı", "hamlesi kalmadı", ...
std::string genitive(const std::string& name); // "Kel Mahmut'un", "Hacı Rıza'nın", "Emekli Nuri'nin"

} // namespace dama
