#pragma once
// Tavla (klasik Türk tavlası, "Türk usulü") game engine: board, dice, legal moves, turn flow, scoring, match.
// Pure logic: no raylib, no I/O, deterministic for a given seed (same seed + same actions -> same game).
// Rules are documented in docs/kurallar_tavla.md.
//
// ------------------------------------------------------------------------------------------------------
// BOARD INDEX CONVENTION (absolute, the same for both players and for the UI)
//
//   Points are 0..23, seen from player 0 (the human, sitting at the bottom of the board):
//
//        top edge (player 1 sits here)
//     12 13 14 15 16 17 | bar | 18 19 20 21 22 23      <- player 1's home board is 18..23
//     11 10  9  8  7  6 | bar |  5  4  3  2  1  0      <- player 0's home board is 0..5
//        bottom edge (player 0 sits here)                 (off tray next to 0 / 23)
//
//   * Player 0 moves DOWN the indices (23 -> 0), enters from the bar onto 24-die (18..23) and bears off
//     past index 0. Index i is player 0's own "point i+1" (index 5 = his 6-point, 12 = his 13-point).
//   * Player 1 moves UP the indices (0 -> 23), enters onto die-1 (0..5) and bears off past index 23.
//     Index i is player 1's own "point 24-i".
//   * Special step endpoints: BAR (= 24) as `from` means "enter the current player's checker from the bar";
//     OFF (= 25) as `to` means "bear off". Each player has his own bar and off tray (Position::bar/off).
//   * Start position: player 0 has 2 on 23, 5 on 12, 3 on 7, 5 on 5; player 1 mirrored: 2 on 0, 5 on 11,
//     3 on 16, 5 on 18.
//   The UI may mirror the picture left/right (home board on the left) as long as it keeps this mapping
//   between indices and physical triangles; the engine never cares about the drawing.
//
// ÇEŞİTLER (Rules::variant): Gülbahar and Fevga (Moultezim) are played on the same board with the same indices,
// but both players move the SAME way round (counter-clockwise seen from above) from diagonally opposite corners:
//   * Player 0 is unchanged: starts with all 15 on index 23, moves down to 0, home 0..5, bears off past 0.
//   * Player 1 starts with all 15 on index 11 (his far right corner), moves 11 -> 0, then 23 -> 12; his home
//     board is 12..17 and he bears off past index 12. Index i is his own point ((i + 12) % 24) + 1.
//   * No hitting, no bar: a single checker holds a point. relPoint / absPoint / pointNumber(Variant, ...) give
//     the numbering; the klasik helpers below stay as they were.
// ------------------------------------------------------------------------------------------------------
#include "core/Rng.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tavla {

constexpr int kPoints = 24;
constexpr int kCheckers = 15;
constexpr int BAR = 24; // Step::from: enter from the current player's bar ("kırık pul")
constexpr int OFF = 25; // Step::to: bear off ("pul toplamak")

// Board position. pts[i] > 0: that many checkers of player 0 on point i; pts[i] < 0: -pts[i] checkers of
// player 1. bar[p] = p's hit checkers waiting to enter, off[p] = p's checkers already borne off.
// Tavla çeşidi (docs/kurallar_tavla.md): Klasik (Türk usulü, the default), Gülbahar (all 15 on the start point,
// no hitting, doubles climb the ladder up to 6-6 from a player's 4th roll), Fevga / Moultezim (all 15 on the start
// point, no hitting, the first checker must pass the opponent's start point, no 6-prime trapping all 15).
enum class Variant : int { Klasik = 0, Gulbahar = 1, Fevga = 2 };
constexpr int kVariants = 3;
const char* variantName(Variant v); // "Klasik", "Gülbahar", "Fevga"
inline bool sameWay(Variant v) { return v != Variant::Klasik; } // both move the same way, a single checker blocks

struct Position {
    std::array<int8_t, kPoints> pts{};
    std::array<int8_t, 2> bar{};
    std::array<int8_t, 2> off{};

    static Position initial();          // klasik
    static Position initial(Variant v); // Gülbahar / Fevga: 15 on each start point
    int owner(int i) const { return pts[i] > 0 ? 0 : (pts[i] < 0 ? 1 : -1); } // -1 = empty
    int count(int i) const { return pts[i] < 0 ? -pts[i] : pts[i]; }
    int count(int p, int i) const { return p == 0 ? (pts[i] > 0 ? pts[i] : 0) : (pts[i] < 0 ? -pts[i] : 0); }
    int onBoard(int p) const;            // checkers of p on points 0..23
    bool operator==(const Position& o) const { return pts == o.pts && bar == o.bar && off == o.off; }
    bool operator!=(const Position& o) const { return !(*this == o); }
};

// ---- geometry helpers (all in absolute indices) ----
inline int direction(int p) { return p == 0 ? -1 : +1; }
inline int homeLo(int p) { return p == 0 ? 0 : 18; }                 // home board = homeLo..homeLo+5
inline bool inHome(int p, int i) { return i >= homeLo(p) && i < homeLo(p) + 6; }
inline int entryPoint(int p, int die) { return p == 0 ? 24 - die : die - 1; }
inline int pointNumber(int p, int i) { return p == 0 ? i + 1 : 24 - i; } // p's own numbering 1..24
int pipCount(const Position& pos, int p); // bar checkers count 25 pips
// The same helpers for any çeşit (Variant::Klasik gives exactly the ones above). rel 0 = p's own 1-point.
inline int relPoint(Variant v, int p, int i) { return p == 0 ? i : (v == Variant::Klasik ? 23 - i : (i + 12) % 24); }
inline int absPoint(Variant v, int p, int r) { return p == 0 ? r : (v == Variant::Klasik ? 23 - r : (r + 12) % 24); }
inline int pointNumber(Variant v, int p, int i) { return relPoint(v, p, i) + 1; }
inline int homeLo(Variant v, int p) { return v == Variant::Klasik ? homeLo(p) : (p == 0 ? 0 : 12); }
inline bool inHome(Variant v, int p, int i) { return i >= homeLo(v, p) && i < homeLo(v, p) + 6; }
inline int startPoint(Variant v, int p) { return absPoint(v, p, 23); } // where p's 24-point is (Gülbahar / Fevga: all 15 start there)
int pipCount(const Position& pos, int p, Variant v);

struct Step {
    int from = -1; // 0..23 or BAR
    int to = -1;   // 0..23 or OFF
    int die = 0;   // die value used (1..6)
    bool hit = false; // lands on a lone opponent checker ("açık pul") and sends it to his bar
    bool operator==(const Step& o) const { return from == o.from && to == o.to && die == o.die; }
};

// A complete play for the current dice: the steps in order and the resulting position.
struct Play {
    std::vector<Step> steps;
    Position result;
};

// Every distinct complete legal play (maximal number of dice used, larger-die rule applied) of player p
// with dice d1,d2 (d1 == d2 -> four moves) from `pos`, deduplicated by resulting position, in a
// deterministic order. Empty if no step is possible.
// (Gülbahar: one rung of the ladder; a double is four moves of that number, the climb is Game's business.)
std::vector<Play> generatePlays(const Position& pos, int p, int d1, int d2, Variant v = Variant::Klasik);
// Faster variant for search: only the resulting positions (same set and order as generatePlays).
void generateResults(const Position& pos, int p, int d1, int d2, std::vector<Position>& out, Variant v = Variant::Klasik);
// Applies one step to a position without any legality check beyond what is needed to keep it consistent
// (used by bots / tests). Returns false if the step is impossible on this board.
bool applyStepTo(Position& pos, int p, const Step& s, Variant v = Variant::Klasik);

struct Rules {
    int matchPoints = 5;        // maç kaç sayıya (3 / 5 / 7)
    // Türk usulü: in the opening each player throws one die; the higher one throws BOTH dice again and
    // starts. false = international rule: the starter plays the two opening dice as his first roll.
    bool openingReroll = true;
    // Türk usulü: from the second game of a match the winner of the previous game starts (he simply
    // rolls; no opening roll). false = every game begins with an opening roll.
    bool winnerStarts = true;
    // false (Türk usulü): mars = 2 points whether or not it is "katmerli". true: katmerli mars (loser bore
    // off nothing AND still has a checker on the bar or in the winner's home board) = 3 points.
    bool katmerliMars = false;
    // false: the turn ends automatically once no legal step remains. true: the turn waits in Moving with
    // turnComplete() == true until endTurn(p), so the last step can still be undone.
    bool confirmTurn = false;
    // Katlama zarı (doubling cube), off in the classic kahvehane game. Before rolling, the player to move may
    // double if the cube is in the middle or his; the other takes (the game is played for twice as much and
    // the cube is his) or drops (he loses the game for the cube's current value). Game points = base (1 oyun,
    // 2 mars, 3 katmerli) x cube. Crawford rule: no doubling in the game right after a player first reaches
    // matchPoints - 1. No doubling before the first move of a game (the opening turn).
    bool doubling = false;
    // Tavla çeşidi. Gülbahar / Fevga: no katmerli mars (a mars is 2 x cube); the cube works the same in every çeşit.
    Variant variant = Variant::Klasik;
};

struct PlayerInfo {
    std::string name;
    bool human = false;
    int score = 0; // match score (sayı)
};

enum class Stage {
    NotStarted,  // before startMatch
    OpeningRoll, // waiting for rollOpening(): each player throws one die
    NeedRoll,    // current() must roll(current())
    Moving,      // current() plays steps (applyStep / undoStep / endTurn)
    DoubleOffered, // current() offered to double (Rules::doubling); responder() must acceptDouble / declineDouble
    GameOver,    // see lastResult(); call startNextGame()
    MatchOver    // see matchWinner()
};

// Dice of the current turn. For a çift (d1 == d2) there are 4 slots, otherwise 2 (value[0]=d1, value[1]=d2).
// A step with die v marks the first unused slot holding v as used.
struct Dice {
    int d1 = 0, d2 = 0; // as thrown; 0 = not rolled
    int n = 0;          // number of slots (0, 2 or 4)
    std::array<int, 4> value{};
    std::array<bool, 4> used{};
    bool isDouble() const { return n == 4; }
    int leftCount() const;
    std::vector<int> left() const; // unused values, in slot order
};

struct GameResult {
    int winner = -1;
    int points = 0;      // written to the winner: base x cube
    int base = 0;        // 1 = oyun, 2 = mars (3 = katmerli mars when Rules::katmerliMars); 1 when dropped
    int cube = 1;        // the katlama zarı's value the game was played for
    bool dropped = false; // the loser refused a double ("pes etti"): the winner gets the cube's value
    bool mars = false;   // loser bore off no checker
    bool katmerli = false; // mars and loser still had a checker on the bar or in the winner's home board
    int gameIndex = 0;
};

enum class EvType {
    MatchStart,  // amount = matchPoints
    GameStart,   // amount = game index (0-based); player = starter if already known (winnerStarts) else -1
    OpeningRoll, // d1 = player 0's die, d2 = player 1's die; player = starter, or -1 on a tie (throw again)
    Roll,        // player rolled d1,d2 (also emitted when the starter plays the opening dice). amount = 1: no throw,
                 // the next rung of a Gülbahar ladder (the dice are turned to the next double d1 = d2)
    Step,        // player moved from -> to with die; hit = an opponent checker went to his bar.
                 // THE event to animate. The three below are follow-ups of the same step (sound/banter/HUD).
    Hit,         // player hit on point `to`; amount = victim seat
    EnterFromBar,// player entered a checker onto `to`
    BearOff,     // player bore off from `from`; amount = player's checkers off now
    Undo,        // player took back a step: the checker goes from `to` back to `from` (hit -> victim returns)
    NoMove,      // player cannot play any part of the roll; turn passes
    TurnEnd,     // player's turn is over; amount = next player
    DoubleOffer, // player offers to double (katladı); amount = the cube's value if taken
    DoubleTake,  // player took the double; amount = the cube's value now (player owns the cube)
    DoubleDrop,  // player refused the double (pes etti); GameEnd follows, amount = points lost
    GameEnd,     // player = winner, amount = points, mars / katmerli flags (see lastResult())
    MatchEnd     // player = match winner
};

struct GameEvent {
    EvType type = EvType::Step;
    int player = -1;
    int from = -1, to = -1, die = 0;
    int d1 = 0, d2 = 0;
    bool hit = false;
    bool mars = false;
    bool katmerli = false;
    int amount = 0;
    std::string text; // short Turkish text; second person for the human ("Zarı attın: 6-5, şeşbeş")
};

struct ActionResult {
    bool ok = false;
    std::string error; // Turkish, user facing, e.g. "Önce kırık pulunu girmelisin"
    static ActionResult success() { return {true, {}}; }
    static ActionResult fail(std::string e) { return {false, std::move(e)}; }
};

// Turkish name of a roll ("düşeş", "şeşbeş", "hepyek", ...), order of the dice does not matter.
std::string diceName(int d1, int d2);

// ---- hamle geçmişi (move history) ----
// One line of the game's record: a turn (dice and the steps played, none = "oynayamadı") or a cube action
// (d1 == 0, `note` says what happened: "katladı: 2, kabul").
// (Gülbahar: every rung of a double's ladder is its own record: "3-3 düse: ...", "4-4 dört cihar: ...".)
struct TurnRecord {
    int player = -1;
    int d1 = 0, d2 = 0;
    std::vector<Step> steps;
    std::string note;
    Variant variant = Variant::Klasik; // whose numbering the steps are written in
};
// A step in the mover's own numbering: "24/18", "18/13*" (* = hit), "bar/22", "6/çıktı".
std::string stepNotation(int p, const Step& s);
std::string stepNotation(int p, const Step& s, Variant v);
// "6-5 şeşbeş: 24/18 18/13*", "6-6 düşeş: oynayamadı", or the note of a cube action.
std::string turnNotation(const TurnRecord& t);

// ---- save / resume ----
// One successful action of the match as given to Game (a saved match is its seed plus these, replayed: the dice come
// from the seeded Rng in the same order).
enum class ActKind { OpeningRoll, Roll, Step, Undo, EndTurn, Double, Take, Drop, NextGame };
struct LoggedAction {
    ActKind kind = ActKind::Roll;
    int player = -1;
    int from = -1, to = -1, die = 0; // Step
    std::string encode() const;      // one text line, e.g. "2 0 23 17 6"
    static bool decode(const std::string& line, LoggedAction& out);
};

class Game {
public:
    explicit Game(const Rules& r = Rules());

    // ---- setup ----
    void setRules(const Rules& r);                 // only between matches
    const Rules& rules() const { return rules_; }
    void setPlayer(int p, const std::string& name, bool human); // p = 0 (bottom) or 1 (across)
    void startMatch(uint64_t seed);                // scores 0, game 0 -> Stage::OpeningRoll
    void startNextGame();                          // after GameOver (not MatchOver)

    // ---- queries (everything is public in tavla except future dice) ----
    Stage stage() const { return stage_; }
    int current() const { return current_; }       // player to roll / move
    int gameIndex() const { return gameIndex_; }   // 0-based
    const PlayerInfo& player(int p) const { return players_[p]; }
    int score(int p) const { return players_[p].score; }
    int matchPoints() const { return rules_.matchPoints; }
    int matchWinner() const { return matchWinner_; } // -1 until MatchOver
    const Position& position() const { return pos_; }
    int barCount(int p) const { return pos_.bar[p]; }
    int offCount(int p) const { return pos_.off[p]; }
    int pipCount(int p) const { return tavla::pipCount(pos_, p, rules_.variant); }
    Variant variant() const { return rules_.variant; }
    // Gülbahar: the rung of the ladder being played (the double's value, climbing to 6), 0 when no ladder is on.
    int ladder() const { return ladder_; }
    // Gülbahar: p's rolls so far in this game (the ladder starts with his 4th).
    int rollsInGame(int p) const { return rolls_[p]; }
    const Dice& dice() const { return dice_; }
    const GameResult& lastResult() const { return lastResult_; }
    const std::vector<Step>& turnSteps() const { return turnSteps_; } // steps played so far this turn
    int turnNumber() const { return turnNumber_; }   // increments at every new turn (also after NoMove)
    // Opening dice of the current game (0 until thrown; the last throw if there were ties).
    int openingDie(int p) const { return openingDice_[p]; }
    // The current game's record: finished turns and cube actions, oldest first (the turn in progress is
    // turnSteps() with dice()).
    const std::vector<TurnRecord>& gameLog() const { return log_; }

    // ---- katlama zarı (Rules::doubling) ----
    int cubeValue() const { return cubeValue_; }   // 1, 2, 4, ... 64
    int cubeOwner() const { return cubeOwner_; }   // -1 = in the middle (either may double), else who may redouble
    bool crawfordGame() const { return crawford_; } // this game is the Crawford game: no doubling
    // p may offer a double now: Rules::doubling, NeedRoll, p to move, the cube is p's or in the middle, not the
    // Crawford game, not the game's first turn, cube below 64.
    bool canDouble(int p) const;
    // The player who must answer an offer (DoubleOffered), else -1.
    int responder() const { return stage_ == Stage::DoubleOffered ? 1 - current_ : -1; }

    // Single steps the current player may make now. Every step listed keeps the "play as many dice as
    // possible / play the larger die if only one can be played" rule satisfiable. Empty unless Moving.
    // (Recomputed on every call; cache it per state in the UI.)
    std::vector<Step> legalSteps() const;
    std::vector<Step> legalStepsFrom(int from) const; // for drag highlighting
    // All distinct complete plays from the current state with the remaining dice (bots / hints).
    std::vector<Play> allTurnPlays() const;
    // Moving and no legal step remains (only observable with Rules::confirmTurn; endTurn() is due).
    bool turnComplete() const;
    bool canUndo() const { return stage_ == Stage::Moving && !history_.empty(); }

    // ---- actions ----
    ActionResult rollOpening();                     // OpeningRoll: one die each (tie -> stays OpeningRoll)
    ActionResult roll(int p);                       // NeedRoll -> Moving (or NoMove -> next player)
    // Moves one checker. The die is inferred; if several dice fit (bearing off with a higher die) the
    // smallest one that is legal is used. The explicit-die overload lets the UI choose.
    ActionResult applyStep(int p, int from, int to);
    ActionResult applyStep(int p, int from, int to, int die);
    ActionResult undoStep(int p);                   // take back the last step of this turn
    ActionResult endTurn(int p);                    // only when turnComplete()
    ActionResult offerDouble(int p);                // NeedRoll -> DoubleOffered (see canDouble)
    ActionResult acceptDouble(int p);               // responder: cube x2, owner p -> NeedRoll (the offerer rolls)
    ActionResult declineDouble(int p);              // responder: the offerer wins the cube's current value

    // ---- save / resume ----
    const std::vector<LoggedAction>& actionLog() const { return actions_; } // since startMatch
    bool replay(const LoggedAction& a); // applies one logged action (false: it does not apply here)

    // ---- events (UI animation/sound/banter; bots may observe) ----
    std::vector<GameEvent> drainEvents();
    const std::vector<GameEvent>& pendingEvents() const { return events_; }

    // ---- testing hooks: tests/ and tools/ only (never UI or bots) ----
    // Replaces the board (caller keeps 15 checkers per side). Does not change stage/turn.
    void debugSetPosition(const Position& pos) { pos_ = pos; }
    // Sets the player to move and the stage (NeedRoll, Moving, ...), clears the turn history and dice. Counts as
    // a later turn of the game (a double may be offered).
    void debugSetTurn(int p, Stage s);
    // Current player gets dice d1,d2 and Stage::Moving (no Roll event, no NoMove handling).
    void debugSetDice(int d1, int d2);
    // The next roll()/rollOpening() uses these values instead of the Rng (queue, FIFO). For rollOpening
    // d1 is player 0's die and d2 player 1's.
    void debugQueueDice(int d1, int d2);

private:
    struct Snapshot {
        Position pos;
        Dice dice;
    };
    void beginGame();
    void startTurn(int p);              // -> NeedRoll
    void setDice(int d1, int d2);       // -> Moving + constraints; handles NoMove / auto end
    void finishTurn();
    void endOfPlay();        // the dice are played out: the next rung of a Gülbahar ladder, else finishTurn()
    void nextRung();
    void afterStep();
    void endGame(int winner, bool dropped = false);
    void recordTurn();
    void computeConstraints();
    int maxDiceFrom(Position& pos, std::array<int, 4>& left, int nLeft, int cap) const;
    std::vector<Step> rawSteps(const Position& pos, int die) const;
    std::string stepError(int from, int to) const;
    std::string says(int p, const std::string& third, const std::string& second) const;
    std::string nameOf(int p) const { return players_[p].name; }
    std::pair<int, int> throwDice();
    void push(GameEvent e);
    ActionResult checkMoving(int p) const;
    ActionResult logged(ActionResult r, ActKind k, int p, int from = -1, int to = -1, int die = 0);
    ActionResult rollOpeningImpl();
    ActionResult rollImpl(int p);
    ActionResult applyStepImpl(int p, int from, int to, int die, int& used);
    ActionResult undoStepImpl(int p);
    ActionResult endTurnImpl(int p);
    ActionResult offerDoubleImpl(int p);
    ActionResult acceptDoubleImpl(int p);
    ActionResult declineDoubleImpl(int p);

    Rules rules_;
    std::array<PlayerInfo, 2> players_;
    okey::Rng rng_;
    std::vector<std::pair<int, int>> queued_;
    Stage stage_ = Stage::NotStarted;
    int current_ = 0;
    int gameIndex_ = 0;
    int turnNumber_ = 0;
    int matchWinner_ = -1;
    int lastWinner_ = -1;
    std::array<int, 2> openingDice_{};
    Position pos_;
    Dice dice_;
    int turnMax_ = 0;      // dice usable this turn (from the roll)
    int forcedDie_ = 0;    // != 0: only one die can be played and it must be this one (larger die rule)
    std::vector<Step> turnSteps_;
    std::vector<Snapshot> history_;
    GameResult lastResult_;
    std::vector<GameEvent> events_;
    std::vector<TurnRecord> log_;
    std::vector<LoggedAction> actions_;
    bool logActions_ = true; // off in the search copies of allTurnPlays
    int gameTurn_ = 0;     // turns started in this game (doubling from the second one)
    int cubeValue_ = 1;
    int cubeOwner_ = -1;
    bool crawford_ = false, crawfordUsed_ = false;
    std::array<int, 2> rolls_{}; // rolls of each player in this game (Gülbahar's ladder from the 4th)
    int ladder_ = 0;             // Gülbahar: the rung being played, 0 = no ladder
};

} // namespace tavla
