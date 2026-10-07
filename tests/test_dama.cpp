// Dama (Türk daması) engine and bot tests for SaklıBahçe: rules on hand-built positions (men, damas, forced capture,
// the majority rule, the 180° rule, pieces taken one by one, crowning only where a capture ends), the end of a game
// and the draws, the match, the action log and its replay, determinism, a move generator cross-checked against an
// independent reference on random positions, a random-game fuzzer and the bots.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Dama.cpp src/core/DamaBot.cpp tests/test_dama.cpp
#include "core/Dama.h"
#include "core/DamaBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace dama;

namespace {

int g_checks = 0;
int g_failures = 0;

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    if (g_failures <= 60) std::printf("%s:%d: CHECK failed: %s\n", file, line, what.c_str());
    else if (g_failures == 61) std::printf("... further failures suppressed\n");
}

template <class T>
std::string show(const T& v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
std::string show(Stage s) { return std::to_string((int)s); }
std::string show(EndReason s) { return std::to_string((int)s); }

} // namespace

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, #cond);                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        const auto va_ = (a);                                                                                    \
        const auto vb_ = (b);                                                                                    \
        if (!(va_ == vb_))                                                                                       \
            reportFailure(__FILE__, __LINE__,                                                                    \
                          std::string(#a " == " #b "   [") + show(va_) + " vs " + show(vb_) + "]");            \
    } while (0)

namespace {

// "d4" -> square
int S(const char* n) { return sq(n[1] - '1', n[0] - 'a'); }

Board boardOf(std::initializer_list<std::pair<const char*, int>> pieces) {
    Board b;
    for (const auto& pc : pieces) b.c[(size_t)S(pc.first)] = (int8_t)pc.second;
    return b;
}

bool hasMove(const std::vector<Move>& ms, const char* from, std::initializer_list<const char*> path) {
    std::vector<int> p;
    for (const char* x : path) p.push_back(S(x));
    for (const Move& m : ms)
        if (m.from == S(from) && m.path == p) return true;
    return false;
}

// ---------------------------------------------------------------- an independent reference generator
// Written from the rules text, without CaptureGen's in-place tricks: every jump makes a new board copy.
struct RefMove {
    int from;
    std::vector<int> path;
    bool operator<(const RefMove& o) const { return from != o.from ? from < o.from : path < o.path; }
};
void refJumps(const Board& b, int p, int s, bool king, int lastDr, int lastDc, std::vector<int>& path, int caps,
              int& best, std::set<RefMove>& out, int from) {
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    bool any = false;
    for (const auto& d : dirs) {
        const int dr = d[0], dc = d[1];
        if (caps > 0 && dr == -lastDr && dc == -lastDc) continue;
        if (!king && dr == -forwardOf(p)) continue;
        // walk to the first piece (a man only looks at the next square)
        int r = rowOf(s) + dr, c = colOf(s) + dc;
        if (king)
            while (onBoard(r, c) && b.empty(sq(r, c))) r += dr, c += dc;
        if (!onBoard(r, c) || b.owner(sq(r, c)) != 1 - p) continue;
        const int victim = sq(r, c);
        int lr = r + dr, lc = c + dc;
        while (onBoard(lr, lc) && b.empty(sq(lr, lc))) {
            Board nb = b;
            nb.c[(size_t)sq(lr, lc)] = nb.c[(size_t)s];
            nb.c[(size_t)s] = 0;
            nb.c[(size_t)victim] = 0;
            path.push_back(sq(lr, lc));
            refJumps(nb, p, sq(lr, lc), king, dr, dc, path, caps + 1, best, out, from);
            path.pop_back();
            any = true;
            if (!king) break;
            lr += dr, lc += dc;
        }
    }
    if (!any && caps > 0) {
        if (caps > best) {
            best = caps;
            out.clear();
        }
        if (caps == best) out.insert({from, path});
    }
}
std::set<RefMove> refMoves(const Board& b, int p) {
    std::set<RefMove> out;
    int best = 0;
    for (int s = 0; s < kSquares; ++s) {
        if (b.owner(s) != p) continue;
        std::vector<int> path;
        refJumps(b, p, s, b.king(s), 0, 0, path, 0, best, out, s);
    }
    if (best > 0) return out;
    for (int s = 0; s < kSquares; ++s) {
        if (b.owner(s) != p) continue;
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
            if (!b.king(s) && d[0] == -forwardOf(p)) continue;
            int r = rowOf(s) + d[0], c = colOf(s) + d[1];
            while (onBoard(r, c) && b.empty(sq(r, c))) {
                out.insert({s, {sq(r, c)}});
                if (!b.king(s)) break;
                r += d[0], c += d[1];
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- tests

void testStart() {
    const Board b = Board::initial();
    CHECK_EQ(b.pieces(0), 16);
    CHECK_EQ(b.pieces(1), 16);
    CHECK_EQ(b.kings(0) + b.kings(1), 0);
    for (int c = 0; c < 8; ++c) {
        CHECK(b.empty(sq(0, c)));
        CHECK(b.empty(sq(7, c)));
        CHECK(b.empty(sq(3, c)) && b.empty(sq(4, c)));
        CHECK(b.owner(sq(1, c)) == 0 && b.owner(sq(2, c)) == 0);
        CHECK(b.owner(sq(5, c)) == 1 && b.owner(sq(6, c)) == 1);
    }
    // only the front men can step, straight ahead
    const std::vector<Move> m0 = generateMoves(b, 0), m1 = generateMoves(b, 1);
    CHECK_EQ(m0.size(), (size_t)8);
    CHECK_EQ(m1.size(), (size_t)8);
    for (const Move& m : m0) CHECK(rowOf(m.from) == 2 && rowOf(m.to()) == 3 && colOf(m.to()) == colOf(m.from));
    for (const Move& m : m1) CHECK(rowOf(m.from) == 5 && rowOf(m.to()) == 4);
    CHECK_EQ(squareName(0), std::string("a1"));
    CHECK_EQ(squareName(63), std::string("h8"));
    CHECK_EQ(S("d4"), sq(3, 3));
}

void testManMoves() {
    // a lone man: forward and both sides, never back or diagonal
    Board b = boardOf({{"d4", 1}, {"h8", -2}});
    std::vector<Move> ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)3);
    CHECK(hasMove(ms, "d4", {"d5"}));
    CHECK(hasMove(ms, "d4", {"c4"}));
    CHECK(hasMove(ms, "d4", {"e4"}));
    CHECK(!hasMove(ms, "d4", {"d3"}));
    // player 1's man goes down the rows
    b = boardOf({{"d5", -1}, {"a1", 2}});
    ms = generateMoves(b, 1);
    CHECK(hasMove(ms, "d5", {"d4"}) && hasMove(ms, "d5", {"c5"}) && hasMove(ms, "d5", {"e5"}) && !hasMove(ms, "d5", {"d6"}));
    // on the edge
    b = boardOf({{"a4", 1}, {"h8", -2}});
    CHECK_EQ(generateMoves(b, 0).size(), (size_t)2);
    // the engine says why a move is wrong
    Game g;
    g.debugSetBoard(boardOf({{"d4", 1}, {"h8", -2}}), 0);
    ActionResult r = g.applyMove(0, S("d4"), {S("d3")});
    CHECK(!r.ok && r.error.find("geri") != std::string::npos);
    r = g.applyMove(0, S("d4"), {S("e5")});
    CHECK(!r.ok && r.error.find("apraz") != std::string::npos);
    r = g.applyMove(0, S("d4"), {S("d6")});
    CHECK(!r.ok && r.error.find("bir kare") != std::string::npos);
    r = g.applyMove(1, S("h8"), {S("h7")});
    CHECK(!r.ok); // not his turn
    CHECK(g.applyMove(0, S("d4"), {S("d5")}).ok);
    CHECK_EQ(g.current(), 1);
}

void testKingMoves() {
    Board b = boardOf({{"d4", 2}, {"d7", 1}, {"a8", -1}});
    std::vector<Move> ms = generateMoves(b, 0);
    int kingMoves = 0;
    for (const Move& m : ms) kingMoves += m.from == S("d4") ? 1 : 0;
    // up d5 d6 (d7 own), down d3 d2 d1, left c4 b4 a4, right e4..h4
    CHECK_EQ(kingMoves, 2 + 3 + 3 + 4);
    CHECK(hasMove(ms, "d4", {"d1"}) && hasMove(ms, "d4", {"h4"}) && !hasMove(ms, "d4", {"d7"}) && !hasMove(ms, "d4", {"e5"}));
    Game g;
    g.debugSetBoard(b, 0);
    const ActionResult r = g.applyMove(0, S("d4"), {S("f6")});
    CHECK(!r.ok && r.error.find("düz") != std::string::npos);
}

void testForcedCapture() {
    // d4 can take d5 (landing d6); any other move is refused
    Board b = boardOf({{"d4", 1}, {"a2", 1}, {"d5", -1}, {"h8", -1}});
    std::vector<Move> ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)1);
    CHECK(hasMove(ms, "d4", {"d6"}));
    CHECK(ms[0].isCapture() && ms[0].captured == std::vector<int>{S("d5")});
    Game g;
    g.debugSetBoard(b, 0);
    ActionResult r = g.applyMove(0, S("a2"), {S("a3")});
    CHECK(!r.ok && r.error.find("zorunlu") != std::string::npos);
    r = g.applyMove(0, S("d4"), {S("c4")});
    CHECK(!r.ok && r.error.find("zorunlu") != std::string::npos);
    CHECK(g.applyMove(0, S("d4"), {S("d6")}).ok);
    CHECK(g.board().empty(S("d5")));
    CHECK_EQ(g.board().pieces(1), 1);
    // a man does not capture backward
    b = boardOf({{"d4", 1}, {"d3", -1}, {"h8", -1}});
    CHECK(!hasCapture(b, 0));
    for (const Move& m : generateMoves(b, 0)) CHECK(!m.isCapture());
    // sideways captures
    b = boardOf({{"d4", 1}, {"e4", -1}, {"c4", -1}, {"h8", -1}});
    ms = generateMoves(b, 0);
    CHECK(hasMove(ms, "d4", {"f4"}) && hasMove(ms, "d4", {"b4"}) && ms.size() == 2);
    // a blocked landing: no capture
    b = boardOf({{"d4", 1}, {"d5", -1}, {"d6", -1}, {"h8", -1}});
    CHECK(!hasCapture(b, 0));
}

void testMajority() {
    // a2 can take 1 (a3 -> a4); d2 can take 2 (d3 -> d4, then d5 -> d6): only d2's capture is legal
    Board b = boardOf({{"a2", 1}, {"a3", -1}, {"d2", 1}, {"d3", -1}, {"d5", -1}, {"h8", -1}});
    std::vector<Move> ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)1);
    CHECK(hasMove(ms, "d2", {"d4", "d6"}));
    Game g;
    g.debugSetBoard(b, 0);
    const ActionResult r = g.applyMove(0, S("a2"), {S("a4")});
    CHECK(!r.ok && r.error.find("En çok") != std::string::npos);
    // a man and a dama count the same: two men here beat one dama there
    b = boardOf({{"a2", 1}, {"a3", -2}, {"d2", 1}, {"d3", -1}, {"d5", -1}, {"h8", -1}});
    ms = generateMoves(b, 0);
    CHECK(ms.size() == 1 && ms[0].from == S("d2"));
    // two ways with the same count: free choice
    b = boardOf({{"d4", 1}, {"c4", -1}, {"e4", -1}, {"h8", -1}});
    CHECK_EQ(generateMoves(b, 0).size(), (size_t)2);
}

void testManChain() {
    // left over c4 to b4, then forward over b5 to b6: taken one by one
    Board b = boardOf({{"d4", 1}, {"c4", -1}, {"b5", -1}, {"h8", -1}});
    std::vector<Move> ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)1);
    CHECK(hasMove(ms, "d4", {"b4", "b6"}));
    CHECK_EQ(ms[0].captured.size(), (size_t)2);
    CHECK_EQ(moveNotation(ms[0]), std::string("d4xb4xb6"));
    // crowning only where the capture ends: c6 takes c7 (lands c8 on the last row) and goes on sideways over d8 as a man
    b = boardOf({{"c6", 1}, {"c7", -1}, {"d8", -1}, {"a5", -1}});
    ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)1);
    CHECK(hasMove(ms, "c6", {"c8", "e8"}));
    CHECK(ms[0].promotes);
    // ... and on the last row it is still a man: no flying capture of f8 from c8
    b = boardOf({{"c6", 1}, {"c7", -1}, {"f8", -1}, {"a5", -1}});
    ms = generateMoves(b, 0);
    CHECK(ms.size() == 1 && hasMove(ms, "c6", {"c8"}) && ms[0].promotes && ms[0].captured.size() == 1);
    // a plain step onto the last row crowns
    b = boardOf({{"c7", 1}, {"a5", -1}});
    ms = generateMoves(b, 0);
    for (const Move& m : ms) CHECK_EQ(m.promotes, m.to() == S("c8"));
    Game g;
    g.debugSetBoard(b, 0);
    CHECK(g.applyMove(0, S("c7"), {S("c8")}).ok);
    CHECK(g.board().king(S("c8")));
    bool promoteEv = false;
    for (const GameEvent& e : g.drainEvents()) promoteEv = promoteEv || e.type == EvType::Promote;
    CHECK(promoteEv);
    // a man sideways along the last row of the other side's men (player 1 crowns on row 0)
    b = boardOf({{"c3", -1}, {"c2", 1}, {"d1", 1}, {"h8", 1}});
    ms = generateMoves(b, 1);
    CHECK(ms.size() == 1 && hasMove(ms, "c3", {"c1", "e1"}) && ms[0].promotes);
}

void testKingCaptures() {
    // a flying capture: d1 takes d6 from afar and may land on d7 or d8
    Board b = boardOf({{"d1", 2}, {"d6", -1}, {"a8", -1}});
    std::vector<Move> ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)2);
    CHECK(hasMove(ms, "d1", {"d7"}) && hasMove(ms, "d1", {"d8"}));
    // two in a row with no gap: no capture that way
    b = boardOf({{"d1", 2}, {"d5", -1}, {"d6", -1}, {"a8", -1}});
    CHECK(!hasCapture(b, 0));
    // an own piece in the way
    b = boardOf({{"d1", 2}, {"d3", 1}, {"d6", -1}, {"a8", -1}});
    CHECK(!hasCapture(b, 0));
    // the 180° rule: from d4, up over d6 and then back down over d2 is not allowed
    b = boardOf({{"d4", 2}, {"d6", -1}, {"d2", -1}});
    ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)3); // d7, d8 (up), d1 (down): one piece each
    for (const Move& m : ms) CHECK_EQ(m.captured.size(), (size_t)1);
    // pieces go one by one: the dama crosses the square of a piece it took earlier in the same move
    // c1 up over c3 to c5, right over e5 to f5, down over f4 to f3, left along row 3 over the now empty c3 and b3 to a3
    b = boardOf({{"c1", 2}, {"c3", -1}, {"e5", -1}, {"f4", -1}, {"b3", -1}});
    ms = generateMoves(b, 0);
    CHECK(!ms.empty());
    for (const Move& m : ms) CHECK_EQ(m.captured.size(), (size_t)4);
    CHECK(hasMove(ms, "c1", {"c5", "f5", "f3", "a3"}));
    // the landing after a capture is free, but the majority rule keeps only the landings that go on capturing
    b = boardOf({{"a1", 2}, {"a4", -1}, {"c6", -1}});
    ms = generateMoves(b, 0);
    CHECK_EQ(ms.size(), (size_t)5); // a6, then over c6 to d6..h6
    CHECK(hasMove(ms, "a1", {"a6", "d6"}) && hasMove(ms, "a1", {"a6", "h6"}));
    for (const Move& m : ms) CHECK_EQ(m.captured.size(), (size_t)2);
}

void testEndings() {
    // the last piece taken
    Game g;
    g.debugSetBoard(boardOf({{"d4", 1}, {"d5", -1}}), 0);
    CHECK(g.applyMove(0, S("d4"), {S("d6")}).ok);
    CHECK(g.stage() == Stage::GameOver || g.stage() == Stage::MatchOver);
    CHECK_EQ(g.lastResult().winner, 0);
    CHECK_EQ(g.lastResult().reason, EndReason::NoPieces);
    // blocked: player 1's man on h2 has h1 below and g2 beside (f2 behind it): no move at all
    g = Game();
    g.debugSetBoard(boardOf({{"h1", 1}, {"h3", 1}, {"g2", 1}, {"f2", 1}, {"a5", 1}, {"h2", -1}}), 0);
    CHECK(g.applyMove(0, S("a5"), {S("a6")}).ok);
    CHECK_EQ(g.lastResult().winner, 0);
    CHECK_EQ(g.lastResult().reason, EndReason::Blocked);
    // one piece each: a1-a4, h8-g8, a4 takes c4 (lands e4); the g8 man cannot take: a draw
    g = Game();
    g.debugSetBoard(boardOf({{"a1", 2}, {"h8", -1}, {"c4", -1}}), 0);
    CHECK(g.applyMove(0, S("a1"), {S("a4")}).ok);
    CHECK(g.applyMove(1, S("h8"), {S("g8")}).ok);
    CHECK(g.applyMove(0, S("a4"), {S("e4")}).ok);
    CHECK_EQ(g.lastResult().reason, EndReason::OneEach);
    CHECK_EQ(g.lastResult().winner, -1);
    // ... but not while the side to move can take: the dama stops under the d5 man, which takes it
    g = Game();
    g.debugSetBoard(boardOf({{"a4", 2}, {"d5", -1}}), 0);
    CHECK(g.applyMove(0, S("a4"), {S("d4")}).ok);
    CHECK(g.stage() == Stage::Playing);
    CHECK(g.applyMove(1, S("d5"), {S("d3")}).ok);
    CHECK_EQ(g.lastResult().winner, 1);
    CHECK_EQ(g.lastResult().reason, EndReason::NoPieces);
    // the old custom (Rules::loneKingWins): a lone dama against a lone man wins
    Rules r;
    r.loneKingWins = true;
    g = Game(r);
    g.debugSetBoard(boardOf({{"a1", 2}, {"h8", -1}, {"c4", -1}}), 0);
    g.applyMove(0, S("a1"), {S("a4")});
    g.applyMove(1, S("h8"), {S("g8")});
    CHECK(g.applyMove(0, S("a4"), {S("e4")}).ok);
    CHECK_EQ(g.lastResult().reason, EndReason::LoneMan);
    CHECK_EQ(g.lastResult().winner, 0);
    // dama against dama: a draw
    g = Game();
    g.debugSetBoard(boardOf({{"a1", 2}, {"h6", -2}, {"c4", -1}}), 0);
    CHECK(g.applyMove(0, S("a1"), {S("a4")}).ok);
    CHECK(g.applyMove(1, S("h6"), {S("h8")}).ok);
    CHECK(g.applyMove(0, S("a4"), {S("e4")}).ok);
    CHECK_EQ(g.lastResult().reason, EndReason::OneEach);
    CHECK_EQ(g.lastResult().winner, -1);
}

void testRepetitionAndNoProgress() {
    Game g;
    const Board b = boardOf({{"a1", 2}, {"c2", 2}, {"h8", -2}, {"f7", -2}});
    g.debugSetBoard(b, 0);
    int moves = 0;
    for (int round = 0; round < 3 && g.stage() == Stage::Playing; ++round) {
        const char* seq[4][3] = {{"a1", "b1", "0"}, {"h8", "h7", "1"}, {"b1", "a1", "0"}, {"h7", "h8", "1"}};
        for (const auto& s : seq) {
            if (g.stage() != Stage::Playing) break;
            CHECK(g.applyMove(s[2][0] - '0', S(s[0]), {S(s[1])}).ok);
            ++moves;
        }
    }
    CHECK_EQ(g.lastResult().reason, EndReason::Repetition);
    CHECK_EQ(moves, 8); // the start position for the third time after 8 plies
    CHECK_EQ(g.lastResult().winner, -1);
    // no progress: with a short limit, damas wandering without taking
    Rules r;
    r.noProgressPlies = 4;
    g = Game(r);
    g.debugSetBoard(b, 0);
    CHECK(g.applyMove(0, S("a1"), {S("b1")}).ok);
    CHECK(g.applyMove(1, S("h8"), {S("h7")}).ok);
    CHECK(g.applyMove(0, S("b1"), {S("d1")}).ok);
    CHECK(g.stage() == Stage::Playing);
    CHECK(g.applyMove(1, S("h7"), {S("h6")}).ok);
    CHECK_EQ(g.lastResult().reason, EndReason::NoProgress);
    // a man stepping forward resets the count; a sideways step does not
    g = Game(r);
    g.debugSetBoard(boardOf({{"a1", 2}, {"d3", 1}, {"h8", -2}, {"e6", -1}}), 0);
    CHECK(g.applyMove(0, S("d3"), {S("c3")}).ok); // sideways: quiet
    CHECK_EQ(g.quietPlies(), 1);
    CHECK(g.applyMove(1, S("e6"), {S("e5")}).ok); // forward: progress
    CHECK_EQ(g.quietPlies(), 0);
}

void testMatchAndLog() {
    // a match to 1 win: MatchOver after the first decisive game
    Rules r;
    r.winsNeeded = 1;
    Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    g.startMatch(77);
    CHECK(g.stage() == Stage::Playing);
    const int starter = g.starter();
    CHECK_EQ(g.current(), starter);
    Bot b0(BotLevel::Normal, 1), b1(BotLevel::Easy, 2);
    for (int i = 0; i < 400 && g.stage() == Stage::Playing; ++i) {
        const int p = g.current();
        CHECK(g.applyMove(p, (p == 0 ? b0 : b1).choose(g, p)).ok);
    }
    CHECK(g.stage() == Stage::MatchOver || g.stage() == Stage::GameOver);
    const std::vector<LoggedAction> log = g.actionLog();
    // encode / decode
    for (const LoggedAction& a : log) {
        LoggedAction d;
        CHECK(LoggedAction::decode(a.encode(), d));
        CHECK(d.encode() == a.encode());
    }
    LoggedAction bad;
    CHECK(!LoggedAction::decode("m 0 99 3", bad));
    CHECK(!LoggedAction::decode("x", bad));
    CHECK(!LoggedAction::decode("m 0 10", bad));
    // the replay reaches the same end
    Game h(r);
    h.setPlayer(0, "Sen", true);
    h.setPlayer(1, "Kel Mahmut", false);
    h.startMatch(77);
    for (const LoggedAction& a : log) CHECK(h.replay(a));
    CHECK(h.board() == g.board());
    CHECK_EQ(h.lastResult().winner, g.lastResult().winner);
    CHECK_EQ(h.stage(), g.stage());
    // a move that does not apply is refused by replay
    LoggedAction junk;
    junk.player = h.current();
    junk.from = 0;
    junk.path = {1};
    CHECK(!h.replay(junk));
    // a match to 3: games alternate who starts, the match ends at 3 wins
    Rules r3;
    Game m(r3);
    m.startMatch(5);
    Bot k(BotLevel::Normal, 9), e(BotLevel::Easy, 10);
    int games = 0, lastStarter = -1;
    for (int i = 0; i < 4000 && m.stage() != Stage::MatchOver; ++i) {
        if (m.stage() == Stage::GameOver) {
            m.startNextGame();
            continue;
        }
        if (m.plies() == 0) {
            if (lastStarter >= 0) CHECK_EQ(m.starter(), 1 - lastStarter);
            lastStarter = m.starter();
            ++games;
        }
        const int p = m.current();
        CHECK(m.applyMove(p, (p == 0 ? k : e).choose(m, p)).ok);
    }
    CHECK(m.stage() == Stage::MatchOver);
    CHECK(m.score(0) == 3 || m.score(1) == 3 || games == r3.maxGames());
    CHECK_EQ(m.matchWinner(), m.score(0) > m.score(1) ? 0 : 1);
    // NextGame in the log
    int nexts = 0;
    for (const LoggedAction& a : m.actionLog()) nexts += a.kind == ActKind::NextGame ? 1 : 0;
    CHECK_EQ(nexts, games - 1);
}

void testMaxGames() {
    // every game a draw: the match stops after maxGames() and is drawn (nobody won a game, nobody took a piece)
    Rules r;
    r.winsNeeded = 1;
    r.repetitions = 2;
    Game g(r);
    g.startMatch(3);
    int games = 0;
    std::array<int, 2> own{};
    for (int i = 0; i < 200 && g.stage() != Stage::MatchOver; ++i) {
        if (g.stage() == Stage::GameOver) g.startNextGame();
        if (g.plies() == 0) {
            ++games;
            own = {0, 0};
        }
        // player 0: a3-a4, then a4-b4-a4...; player 1: h6-h5, then h5-g5-h5...: the position comes back
        const int p = g.current();
        const int k = own[(size_t)p]++;
        ActionResult res;
        if (p == 0) res = k == 0 ? g.applyMove(0, S("a3"), {S("a4")}) : g.applyMove(0, S(k % 2 ? "a4" : "b4"), {S(k % 2 ? "b4" : "a4")});
        else res = k == 0 ? g.applyMove(1, S("h6"), {S("h5")}) : g.applyMove(1, S(k % 2 ? "h5" : "g5"), {S(k % 2 ? "g5" : "h5")});
        CHECK(res.ok);
        if (!res.ok) break;
    }
    CHECK(g.stage() == Stage::MatchOver);
    CHECK_EQ(games, r.maxGames());
    CHECK_EQ(g.lastResult().reason, EndReason::Repetition);
    CHECK_EQ(g.matchWinner(), -1);
}

void testAgainstReference() {
    okey::Rng rng(2024);
    int positions = 0, mismatches = 0;
    for (int t = 0; t < 4000; ++t) {
        Board b;
        const int n0 = 1 + rng.range(10), n1 = 1 + rng.range(10);
        for (int k = 0; k < n0 + n1; ++k) {
            const int s = rng.range(64);
            if (!b.empty(s)) continue;
            const int p = k < n0 ? 0 : 1;
            const bool king = rng.chance(0.3f);
            if (!king && rowOf(s) == promotionRow(p)) continue; // a man never stands on its crowning row
            b.c[(size_t)s] = (int8_t)((p == 0 ? 1 : -1) * (king ? 2 : 1));
        }
        for (int p = 0; p < 2; ++p) {
            ++positions;
            std::set<RefMove> ref = refMoves(b, p);
            std::set<RefMove> got;
            for (const Move& m : generateMoves(b, p)) got.insert({m.from, m.path});
            if (ref.size() != got.size() || !std::equal(ref.begin(), ref.end(), got.begin(), [](const RefMove& a, const RefMove& c) {
                    return !(a < c) && !(c < a);
                }))
                ++mismatches;
            CHECK_EQ(hasCapture(b, p), !ref.empty() && !ref.begin()->path.empty() &&
                                           (generateMoves(b, p).empty() ? false : generateMoves(b, p)[0].isCapture()));
            // generateCompact and toMove agree with generateMoves
            std::vector<CMove> cm;
            generateCompact(b, p, cm);
            const std::vector<Move> gm = generateMoves(b, p);
            CHECK_EQ(cm.size(), gm.size());
            for (size_t i = 0; i < cm.size() && i < gm.size(); ++i) {
                Board x = b, y = b;
                applyCompact(x, p, cm[i]);
                applyMoveTo(y, p, gm[i]);
                CHECK(x == y);
            }
        }
    }
    CHECK_EQ(mismatches, 0);
    std::printf("reference check: %d positions\n", positions);
}

void testFuzz() {
    okey::Rng rng(99);
    long moves = 0;
    for (int t = 0; t < 300; ++t) {
        Game g;
        g.startMatch(1000 + (uint64_t)t);
        for (int ply = 0; ply < 400 && g.stage() == Stage::Playing; ++ply) {
            const std::vector<Move> legal = g.legalMoves();
            CHECK(!legal.empty());
            const int p = g.current();
            // the majority rule: every legal move takes the same number of pieces
            for (const Move& m : legal) CHECK_EQ(m.captured.size(), legal[0].captured.size());
            const Move m = legal[(size_t)rng.range((int)legal.size())];
            const Board before = g.board();
            CHECK(g.applyMove(p, m).ok);
            ++moves;
            const Board& after = g.board();
            CHECK_EQ(after.pieces(1 - p), before.pieces(1 - p) - (int)m.captured.size());
            CHECK_EQ(after.pieces(p), before.pieces(p));
            for (int s : m.captured) CHECK(after.empty(s) || s == m.to()); // (a dama may land where it took earlier)
            CHECK_EQ(after.owner(m.to()), p);
            if (m.from != m.to()) CHECK(after.empty(m.from));
            for (int s = 0; s < kSquares; ++s)
                if (!after.empty(s) && !after.king(s)) CHECK(rowOf(s) != promotionRow(after.owner(s)));
        }
        if (g.stage() == Stage::Playing) CHECK(g.plies() >= 400);
    }
    std::printf("fuzz: %ld random moves\n", moves);
}

void testDeterminism() {
    auto run = [](uint64_t seed) {
        Game g;
        g.startMatch(seed);
        Bot a(BotLevel::Easy, seed), b(BotLevel::Normal, seed + 1);
        std::string trace;
        for (int i = 0; i < 60 && g.stage() == Stage::Playing; ++i) {
            const int p = g.current();
            const Move m = (p == 0 ? a : b).choose(g, p);
            trace += moveNotation(m) + " ";
            g.applyMove(p, m);
        }
        return trace;
    };
    CHECK_EQ(run(41), run(41));
    { // Kurt too: its budget is in nodes, not on the clock
        auto kurt = [](uint64_t seed) {
            Game g;
            g.startMatch(seed);
            Bot a(BotLevel::Hard, seed), b(BotLevel::Hard, seed + 1);
            std::string trace;
            for (int i = 0; i < 16 && g.stage() == Stage::Playing; ++i) {
                const int p = g.current();
                const Move m = (p == 0 ? a : b).choose(g, p);
                trace += moveNotation(m) + " ";
                g.applyMove(p, m);
            }
            return trace;
        };
        CHECK_EQ(kurt(43), kurt(43));
    }
    // the starter of game 0 comes from the seed
    std::set<int> starters;
    for (uint64_t s = 1; s < 20; ++s) {
        Game g;
        g.startMatch(s);
        starters.insert(g.starter());
    }
    CHECK_EQ(starters.size(), (size_t)2);
}

void testBots() {
    // always a legal move, at every level, from many positions
    okey::Rng rng(5);
    for (int lv = 0; lv < 3; ++lv) {
        Bot bot((BotLevel)lv, 3 + (uint64_t)lv);
        for (int t = 0; t < (lv == 2 ? 6 : 30); ++t) {
            Game g;
            g.startMatch(500 + (uint64_t)t);
            for (int ply = 0; ply < 20 + rng.range(30) && g.stage() == Stage::Playing; ++ply) {
                const std::vector<Move>& l = g.legalMoves();
                g.applyMove(g.current(), l[(size_t)rng.range((int)l.size())]);
            }
            if (g.stage() != Stage::Playing) continue;
            const Move m = bot.choose(g, g.current());
            bool legal = false;
            for (const Move& x : g.legalMoves()) legal = legal || x.sameAs(m);
            CHECK(legal);
        }
    }
    // Kurt sees a two-for-one: d4-d5 gives a man (d6 must take it, landing on d4), then the d1 dama takes d4 and,
    // turning at d6, f6 as well
    const Board shot = boardOf({{"d1", 2}, {"d4", 1}, {"d6", -1}, {"f6", -1}, {"h7", -1}});
    {
        Game g;
        g.debugSetBoard(shot, 0);
        Bot kurt(BotLevel::Hard, 1);
        const Move m = kurt.choose(g, 0);
        CHECK(m.from == S("d4") && m.to() == S("d5"));
        CHECK(kurt.lastSearch().score > 50);
    }
    // a won ending is played out: two damas and a man against a lone man
    {
        Game g;
        g.debugSetBoard(boardOf({{"b2", 2}, {"g3", 2}, {"a2", 1}, {"d6", -1}}), 0);
        Bot kurt(BotLevel::Hard, 2), other(BotLevel::Normal, 3);
        for (int i = 0; i < 120 && g.stage() == Stage::Playing; ++i) {
            const int p = g.current();
            g.applyMove(p, (p == 0 ? kurt : other).choose(g, p));
        }
        CHECK(g.stage() != Stage::Playing);
        CHECK_EQ(g.lastResult().winner, 0);
    }
    // Kurt's decision time in the middle game
    {
        Game g;
        g.startMatch(12);
        Bot u(BotLevel::Normal, 4), kurt(BotLevel::Hard, 5);
        for (int i = 0; i < 16; ++i) g.applyMove(g.current(), u.choose(g, g.current()));
        double worst = 0.0;
        for (int i = 0; i < 6 && g.stage() == Stage::Playing; ++i) {
            const std::clock_t t0 = std::clock(); // CPU time: the search is single-threaded, other load does not count
            const Move m = kurt.choose(g, g.current());
            const double ms = 1000.0 * (double)(std::clock() - t0) / CLOCKS_PER_SEC;
            worst = std::max(worst, ms);
            g.applyMove(g.current(), m);
        }
        std::printf("Kurt: worst decision %.0f ms (depth %d)\n", worst, kurt.lastSearch().depth);
        CHECK(worst < 1500.0); // (about 0.15 s at -O2; generous for sanitizer builds)
    }
    // the evaluation is symmetric
    {
        okey::Rng r2(8);
        for (int t = 0; t < 200; ++t) {
            Board b, m;
            for (int k = 0; k < 12; ++k) {
                const int s = r2.range(64);
                const int8_t v = (int8_t)((r2.chance(0.5f) ? 1 : -1) * (r2.chance(0.25f) ? 2 : 1));
                b.c[(size_t)s] = v;
            }
            for (int s = 0; s < 64; ++s) m.c[(size_t)sq(7 - rowOf(s), colOf(s))] = (int8_t)-b.c[(size_t)s];
            CHECK_EQ(botEvaluate(b, 0), botEvaluate(m, 1));
        }
    }
    // the scores of the analysis: the shot scores best
    {
        const std::vector<Move> ms = generateMoves(shot, 0);
        const std::vector<int> sc = botScoreMoves(shot, 0, ms, 5);
        CHECK_EQ(sc.size(), ms.size());
        int best = -WIN_SCORE, d5 = -WIN_SCORE;
        for (size_t i = 0; i < ms.size(); ++i) {
            best = std::max(best, sc[i]);
            if (ms[i].from == S("d4") && ms[i].to() == S("d5")) d5 = sc[i];
        }
        CHECK_EQ(d5, best);
        CHECK(d5 > 50);
    }
}

} // namespace

// The rules change only between matches.
void testSetRulesBetweenMatches() {
    Game g;
    Rules r;
    r.winsNeeded = 5;
    g.setRules(r);
    CHECK_EQ(g.rules().winsNeeded, 5);
    g.startMatch(3);
    r.winsNeeded = 1;
    g.setRules(r); // (ignored: a match is running)
    CHECK_EQ(g.rules().winsNeeded, 5);
}

int main() {
    testSetRulesBetweenMatches();
    testStart();
    testManMoves();
    testKingMoves();
    testForcedCapture();
    testMajority();
    testManChain();
    testKingCaptures();
    testEndings();
    testRepetitionAndNoProgress();
    testMatchAndLog();
    testMaxGames();
    testAgainstReference();
    testFuzz();
    testDeterminism();
    testBots();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
