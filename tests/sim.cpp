// Headless bot-vs-bot simulation for SaklıBahçe.
//
//   sim --hands N --seed S --levels a,b,c,d [--verbose] [--rotate] [--duplicate] [--no-wait] [--katlamali]
//       [--match H]
//       [--slow MS]
//
// levels: 0 = Acemi (Easy), 1 = Usta (Normal), 2 = Kurt (Hard), one per seat. --rotate shifts the level
// assignment by one seat every match so seat position does not bias the level comparison. --duplicate
// (implies --rotate) replays every deal four times, once per rotation, so each level plays every seat with
// the same cards; the level comparison is then also reported as a paired difference over deals, which
// removes most of the luck of the deal from the standard error.
// Every bot action goes through applyBotAction; a rejected action is printed (state + error), counted,
// and replaced by fallbackAction. --slow MS prints every bot decision that took longer than MS milliseconds
// (with the position), to investigate the decision-time budget.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Meld.cpp src/core/Game.cpp src/core/Solver.cpp
//        src/core/Bot.cpp tests/sim.cpp -o build/ai/sim
#include "core/Bot.h"
#include "core/Game.h"
#include "core/Solver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okey;

namespace {

const char* levelName(int l) {
    switch (l) {
    case 0: return "Acemi";
    case 1: return "Usta";
    default: return "Kurt";
    }
}

const char* kindName(BotAction::Kind k) {
    switch (k) {
    case BotAction::Kind::DrawPile: return "DrawPile";
    case BotAction::Kind::TakeLeft: return "TakeLeft";
    case BotAction::Kind::ReturnLeft: return "ReturnLeft";
    case BotAction::Kind::Open: return "Open";
    case BotAction::Kind::LayMelds: return "LayMelds";
    case BotAction::Kind::AddToMeld: return "AddToMeld";
    case BotAction::Kind::SwapJoker: return "SwapJoker";
    case BotAction::Kind::Discard: return "Discard";
    }
    return "?";
}

std::string tilesStr(const std::vector<int>& v, const OkeyInfo& ok) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += tileNameTR(v[i], ok);
    }
    return s + "]";
}

std::string actionStr(const BotAction& a, const OkeyInfo& ok) {
    std::string s = kindName(a.kind);
    if (!a.melds.empty()) {
        s += " ";
        for (const auto& m : a.melds) s += tilesStr(m, ok);
    }
    if (a.tile >= 0) s += " tile=" + tileNameTR(a.tile, ok);
    if (a.meld >= 0) s += " meld=" + std::to_string(a.meld);
    return s;
}

void dumpState(const Game& g, int seat) {
    const OkeyInfo& ok = g.okey();
    std::printf("    turn %d seat %d stage %s pending %s pile %d okey %s\n", g.turnNumber(), seat,
                g.stage() == TurnStage::NeedDraw ? "NeedDraw" : "Play",
                g.pendingLeftTile() >= 0 ? tileNameTR(g.pendingLeftTile(), ok).c_str() : "-", g.pileCount(),
                tileNameTR(makeTileId(ok.color, ok.number, 0), ok).c_str());
    const PlayerInfo& p = g.player(seat);
    std::printf("    hand (%zu, opened=%d pairs=%d openedTurn=%d): %s\n", p.hand.size(), (int)p.opened,
                (int)p.openedWithPairs, p.openedTurn, tilesStr(p.hand, ok).c_str());
    for (size_t i = 0; i < g.table().size(); ++i)
        std::printf("    table[%zu] owner %d: %s\n", i, g.table()[i].owner, tilesStr(g.table()[i].ids(), ok).c_str());
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct LevelStats {
    long long seatHands = 0;   // seat-hands played at this level
    long long scoreSum = 0;
    double scoreSq = 0;
    long long wins = 0;        // hands finished by this level
    long long opened = 0;
    long long openedPairs = 0;
    long long openTurnSum = 0; // own-turn index of the opening
    long long penalties = 0;
    long long matchSeats = 0;
    double matchWins = 0;      // shared on ties
    long long decisions = 0;
    double timeSum = 0;
    double timeMax = 0;
    long long unopened = 0;       // seat-hands ending unopened (202)
    long long openedLost = 0;     // opened, did not win
    long long openedLostScore = 0;
    long long unopenedMaxSeries = 0; // solver value of the final hand when unopened
    long long unopenedPairs = 0;
    long long seriesScore = 0, seriesN = 0, seriesWins = 0;
    long long pairScore = 0, pairN = 0, pairWins = 0;
    long long jokerLeft = 0;       // opened non-winners caught with an okey in hand
    long long tilesLeft = 0;       // tiles left in hand by opened non-winners
    long long winScore = 0;        // sum of winning scores (multipliers included)
    std::vector<float> times;      // every decision time (ms), for percentiles
};

double percentile(std::vector<float> v, double q) {
    if (v.empty()) return 0.0;
    const size_t k = std::min(v.size() - 1, (size_t)(q * (double)v.size()));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

} // namespace

int main(int argc, char** argv) {
    int hands = 1000;
    uint64_t seed = 1;
    int levels[4] = {1, 1, 1, 1};
    bool verbose = false, rotate = false, duplicate = false;
    double slowMs = -1.0;
    RulesConfig rules;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hands") && i + 1 < argc) hands = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--levels") && i + 1 < argc) {
            const char* s = argv[++i];
            for (int k = 0; k < 4; ++k) {
                levels[k] = std::max(0, std::min(2, std::atoi(s)));
                const char* comma = std::strchr(s, ',');
                if (!comma) {
                    for (int r = k + 1; r < 4; ++r) levels[r] = levels[k];
                    break;
                }
                s = comma + 1;
            }
        } else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--rotate")) rotate = true;
        else if (!std::strcmp(argv[i], "--duplicate")) duplicate = rotate = true;
        else if (!std::strcmp(argv[i], "--no-wait")) rules.waitTurnAfterOpening = false;
        else if (!std::strcmp(argv[i], "--katlamali")) rules.katlamali = true;
        else if (!std::strcmp(argv[i], "--match") && i + 1 < argc) rules.numHands = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--slow") && i + 1 < argc) slowMs = std::atof(argv[++i]);
        else {
            std::fprintf(stderr, "usage: sim --hands N --seed S --levels a,b,c,d [--verbose] [--rotate] [--duplicate] "
                                 "[--no-wait] [--katlamali] [--match H] [--slow MS]\n");
            return 2;
        }
    }

    Game g(rules);
    std::array<Bot, 4> bots = {Bot(BotLevel::Normal, seed * 4 + 1), Bot(BotLevel::Normal, seed * 4 + 2),
                               Bot(BotLevel::Normal, seed * 4 + 3), Bot(BotLevel::Normal, seed * 4 + 4)};
    int seatLevel[4];

    LevelStats lv[3];
    long long seatScore[4] = {};
    long long handsPlayed = 0, finished = 0, exhausted = 0, turnsSum = 0;
    long long rejected = 0, fallbackRejected = 0;
    long long penOkey = 0, penIslek = 0, penReturn = 0;
    long long finishOkey = 0, finishPairs = 0, finishElden = 0;
    long long takeLeft = 0, returnLeft = 0, swaps = 0, adds = 0, lays = 0;
    long long openedSeatHands = 0;
    long long stuckTurns = 0;
    double timeMax = 0, timeSum = 0;
    long long decisions = 0;
    int matchIndex = 0;
    const double t0 = nowMs();
    // Duplicate mode: per-block (one deal, four rotations) level sums, and paired level differences.
    double blockSum[3] = {}, blockCnt[3] = {};
    long long pairN[3][3] = {};
    double pairSum[3][3] = {}, pairSq[3][3] = {};

    // Duplicate mode always completes the current block of four rotations.
    while (handsPlayed < hands || (duplicate && matchIndex % 4 != 0)) {
        for (int s = 0; s < 4; ++s) {
            seatLevel[s] = rotate ? levels[(s + matchIndex) % 4] : levels[s];
            bots[s].setLevel((BotLevel)seatLevel[s]);
            g.setPlayer(s, std::string("Bot") + std::to_string(s), false);
        }
        const uint64_t deal = duplicate ? (uint64_t)(matchIndex / 4) : (uint64_t)matchIndex;
        g.startMatch(seed * 1000003ull + deal);
        for (Bot& b : bots) b.resetForHand();

        for (;;) {
            // ---- one hand ----
            std::vector<GameEvent> evs = g.drainEvents();
            const int handStartTurn = g.turnNumber();
            int ownTurns[4] = {0, 0, 0, 0};
            int openTurn[4] = {-1, -1, -1, -1};
            bool lastWasDiscardJoker = false;
            int lastEvType = -1;
            for (Bot& b : bots) b.resetForHand();
            int actionsThisTurn = 0;
            int lastTurn = g.turnNumber();
            ownTurns[g.current()] = 1;
            while (g.handState() == HandState::Playing) {
                const int seat = g.current();
                const double a0 = nowMs();
                BotAction a = bots[seat].next(g, seat);
                const double dt = nowMs() - a0;
                timeMax = std::max(timeMax, dt);
                timeSum += dt;
                ++decisions;
                LevelStats& L = lv[seatLevel[seat]];
                L.decisions++;
                L.timeSum += dt;
                L.timeMax = std::max(L.timeMax, dt);
                L.times.push_back((float)dt);
                if (slowMs >= 0.0 && dt > slowMs) {
                    std::printf("SLOW %.1f ms hand %lld seat %d (%s): %s\n", dt, handsPlayed, seat,
                                levelName(seatLevel[seat]), actionStr(a, g.okey()).c_str());
                    dumpState(g, seat);
                }
                if (verbose) std::printf("  [%d] seat %d (%s): %s\n", g.turnNumber(), seat, levelName(seatLevel[seat]),
                                         actionStr(a, g.okey()).c_str());
                ActionResult r = applyBotAction(g, seat, a);
                if (!r.ok) {
                    ++rejected;
                    std::printf("REJECTED hand %lld seat %d (%s): %s -> %s\n", handsPlayed, seat,
                                levelName(seatLevel[seat]), actionStr(a, g.okey()).c_str(), r.error.c_str());
                    dumpState(g, seat);
                    BotAction f = fallbackAction(g, seat);
                    ActionResult r2 = applyBotAction(g, seat, f);
                    if (!r2.ok) {
                        ++fallbackRejected;
                        std::printf("  FALLBACK REJECTED: %s -> %s\n", actionStr(f, g.okey()).c_str(),
                                    r2.error.c_str());
                    }
                }
                if (g.turnNumber() != lastTurn) {
                    lastTurn = g.turnNumber();
                    actionsThisTurn = 0;
                } else if (++actionsThisTurn > 200) {
                    ++stuckTurns;
                    std::printf("STUCK hand %lld seat %d: too many actions in one turn\n", handsPlayed, seat);
                    dumpState(g, seat);
                    // Force progress
                    BotAction f = fallbackAction(g, seat);
                    applyBotAction(g, seat, f);
                    actionsThisTurn = 0;
                }
                std::vector<GameEvent> ev = g.drainEvents();
                for (const GameEvent& e : ev) {
                    for (int s = 0; s < 4; ++s) bots[s].observe(e, g);
                    switch (e.type) {
                    case EvType::TurnStart: ownTurns[e.player]++; break;
                    case EvType::Open:
                        openTurn[e.player] = ownTurns[e.player];
                        break;
                    case EvType::TakeLeft: ++takeLeft; break;
                    case EvType::ReturnLeft: ++returnLeft; break;
                    case EvType::SwapJoker: ++swaps; break;
                    case EvType::AddToMeld: ++adds; break;
                    case EvType::LayMelds: ++lays; break;
                    case EvType::Discard: lastWasDiscardJoker = g.okey().isJoker(e.tile); break;
                    case EvType::Penalty:
                        if (lastEvType == (int)EvType::ReturnLeft) ++penReturn;
                        else if (lastWasDiscardJoker) ++penOkey;
                        else ++penIslek;
                        lv[seatLevel[e.player]].penalties++;
                        if (verbose) {
                            std::printf("    PENALTY seat %d: %s\n", e.player, e.text.c_str());
                            dumpState(g, e.player);
                        }
                        break;
                    default: break;
                    }
                    if (verbose && (e.type == EvType::Open || e.type == EvType::HandEnd))
                        std::printf("    %s\n", e.text.c_str());
                    lastEvType = (int)e.type;
                }
            }
            // ---- hand over ----
            const HandResult& hr = g.lastHandResult();
            ++handsPlayed;
            turnsSum += g.turnNumber() - handStartTurn + 1;
            if (hr.reason == HandEndReason::PlayerFinished) {
                ++finished;
                lv[seatLevel[hr.winner]].wins++;
                if (hr.finishedWithJoker) ++finishOkey;
                if (hr.finishedWithPairs) ++finishPairs;
                if (hr.finishedInOneGo) ++finishElden;
            } else {
                ++exhausted;
            }
            for (int s = 0; s < 4; ++s) {
                LevelStats& L = lv[seatLevel[s]];
                L.seatHands++;
                L.scoreSum += hr.score[s];
                L.scoreSq += (double)hr.score[s] * hr.score[s];
                seatScore[s] += hr.score[s];
                blockSum[seatLevel[s]] += hr.score[s];
                blockCnt[seatLevel[s]] += 1;
                const PlayerInfo& p = g.player(s);
                if (!p.opened) {
                    L.unopened++;
                    L.unopenedMaxSeries += solveSeries(p.hand, g.okey()).value;
                    L.unopenedPairs += solvePairs(p.hand, g.okey()).value;
                } else if (s != hr.winner) {
                    L.openedLost++;
                    L.openedLostScore += hr.score[s];
                    L.tilesLeft += (long long)p.hand.size();
                    for (int id : p.hand)
                        if (g.okey().isJoker(id)) {
                            L.jokerLeft++;
                            break;
                        }
                }
                if (p.opened && !p.openedWithPairs) {
                    L.seriesN++;
                    L.seriesScore += hr.score[s];
                    if (s == hr.winner) L.seriesWins++;
                    if (s == hr.winner) L.winScore += hr.score[s];
                } else if (p.opened) {
                    L.pairN++;
                    L.pairScore += hr.score[s];
                    if (s == hr.winner) L.pairWins++;
                    if (s == hr.winner) L.winScore += hr.score[s];
                }
                if (p.opened) {
                    ++openedSeatHands;
                    L.opened++;
                    if (p.openedWithPairs) L.openedPairs++;
                    L.openTurnSum += std::max(1, openTurn[s]);
                }
            }
            if (verbose)
                std::printf("HAND %lld: %s | scores %d %d %d %d\n", handsPlayed,
                            hr.reason == HandEndReason::PlayerFinished ? "finished" : "pile out", hr.score[0],
                            hr.score[1], hr.score[2], hr.score[3]);
            if (handsPlayed >= hands && !duplicate) break;
            if (g.handState() == HandState::MatchOver) break;
            g.startNextHand();
        }
        if (g.handState() == HandState::MatchOver) {
            // match winner(s): lowest total, ties shared
            int best = 1 << 30;
            for (int s = 0; s < 4; ++s) best = std::min(best, g.player(s).totalScore);
            int nb = 0;
            for (int s = 0; s < 4; ++s)
                if (g.player(s).totalScore == best) ++nb;
            for (int s = 0; s < 4; ++s) {
                lv[seatLevel[s]].matchSeats++;
                if (g.player(s).totalScore == best) lv[seatLevel[s]].matchWins += 1.0 / nb;
            }
        }
        ++matchIndex;
        if (duplicate && matchIndex % 4 == 0) {
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    if (a == b || !blockCnt[a] || !blockCnt[b]) continue;
                    const double d = blockSum[a] / blockCnt[a] - blockSum[b] / blockCnt[b];
                    pairN[a][b]++;
                    pairSum[a][b] += d;
                    pairSq[a][b] += d * d;
                }
            }
            for (int l = 0; l < 3; ++l) blockSum[l] = blockCnt[l] = 0;
        }
    }

    const double elapsed = (nowMs() - t0) / 1000.0;
    const char* mode = duplicate ? " duplicate" : (rotate ? " rotated" : "");
    std::printf("=== sim: %lld hands, seed %llu, levels %d,%d,%d,%d%s%s (%.1f s) ===\n", handsPlayed,
                (unsigned long long)seed, levels[0], levels[1], levels[2], levels[3], mode,
                rules.waitTurnAfterOpening ? "" : " no-wait", elapsed);
    std::printf("finished: %.1f%%  pile exhausted: %.1f%%  avg turns/hand: %.1f\n", 100.0 * finished / handsPlayed,
                100.0 * exhausted / handsPlayed, (double)turnsSum / handsPlayed);
    std::printf("finishes: okey %lld, pairs %lld, elden %lld | take-left %lld, return-left %lld, swaps %lld, "
                "işle %lld, lay %lld\n",
                finishOkey, finishPairs, finishElden, takeLeft, returnLeft, swaps, adds, lays);
    std::printf("opening rate: %.1f%% of seat-hands\n", 100.0 * openedSeatHands / (4.0 * handsPlayed));
    std::printf("penalties: okey %lld, işlek %lld, returned %lld\n", penOkey, penIslek, penReturn);
    std::printf("avg hand score per seat: %.1f %.1f %.1f %.1f\n", (double)seatScore[0] / handsPlayed,
                (double)seatScore[1] / handsPlayed, (double)seatScore[2] / handsPlayed,
                (double)seatScore[3] / handsPlayed);
    for (int l = 0; l < 3; ++l) {
        const LevelStats& L = lv[l];
        if (!L.seatHands) continue;
        std::printf("%-6s avg score %7.1f | hand wins %5.1f%% | opened %5.1f%% (pairs %4.1f%%) at own turn %.1f | "
                    "penalties/hand %.3f | match wins %5.1f%% | decision avg %.3f ms max %.1f ms\n",
                    levelName(l), (double)L.scoreSum / L.seatHands, 100.0 * L.wins / L.seatHands,
                    100.0 * L.opened / L.seatHands, 100.0 * L.openedPairs / L.seatHands,
                    L.opened ? (double)L.openTurnSum / L.opened : 0.0, (double)L.penalties / L.seatHands,
                    L.matchSeats ? 100.0 * L.matchWins / L.matchSeats : 0.0, L.timeSum / std::max(1LL, L.decisions),
                    L.timeMax);
        std::printf("       unopened %5.1f%% (final hand: series %.0f, pairs %.1f) | opened non-winner avg %.1f\n",
                    100.0 * L.unopened / L.seatHands, L.unopened ? (double)L.unopenedMaxSeries / L.unopened : 0.0,
                    L.unopened ? (double)L.unopenedPairs / L.unopened : 0.0,
                    L.openedLost ? (double)L.openedLostScore / L.openedLost : 0.0);
        std::printf("       series openers: avg %.1f, win %.1f%% | pair openers: avg %.1f, win %.1f%%\n",
                    L.seriesN ? (double)L.seriesScore / L.seriesN : 0.0,
                    L.seriesN ? 100.0 * L.seriesWins / L.seriesN : 0.0,
                    L.pairN ? (double)L.pairScore / L.pairN : 0.0, L.pairN ? 100.0 * L.pairWins / L.pairN : 0.0);
        std::printf("       avg win score %.1f | opened non-winners: %.1f tiles left, okey caught in hand %.1f%%\n",
                    L.wins ? (double)L.winScore / L.wins : 0.0, L.openedLost ? (double)L.tilesLeft / L.openedLost : 0.0,
                    L.openedLost ? 100.0 * L.jokerLeft / L.openedLost : 0.0);
    }
    for (int l = 0; l < 3; ++l) {
        const LevelStats& L = lv[l];
        if (!L.seatHands) continue;
        std::printf("%-6s decision time: median %.3f ms, p99 %.2f ms, p99.9 %.2f ms, max %.2f ms (n=%lld)\n",
                    levelName(l), percentile(L.times, 0.5), percentile(L.times, 0.99), percentile(L.times, 0.999),
                    L.timeMax, L.decisions);
    }
    for (int l = 0; l < 3; ++l) {
        const LevelStats& L = lv[l];
        if (!L.seatHands) continue;
        const double mean = (double)L.scoreSum / L.seatHands;
        const double var = std::max(0.0, L.scoreSq / L.seatHands - mean * mean);
        std::printf("%-6s mean hand score %.2f +- %.2f (standard error, n=%lld seat-hands)\n", levelName(l), mean,
                    std::sqrt(var / L.seatHands), L.seatHands);
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            if (!pairN[b][a]) continue;
            const double n = (double)pairN[b][a];
            const double mean = pairSum[b][a] / n;
            const double var = std::max(0.0, pairSq[b][a] / n - mean * mean);
            std::printf("paired (duplicate deals): %s - %s = %.2f +- %.2f per seat-hand (n=%lld deals)\n",
                        levelName(b), levelName(a), mean, std::sqrt(var / std::max(1.0, n - 1)), pairN[b][a]);
        }
    }
    for (int l = 0; l < 3; ++l) {
        const LevelStats& L = lv[l];
        if (L.seatHands)
            std::printf("STAT level=%d n=%lld sum=%lld sq=%.0f wins=%lld opened=%lld\n", l, L.seatHands, L.scoreSum,
                        L.scoreSq, L.wins, L.opened);
    }
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            if (pairN[a][b])
                std::printf("PAIR a=%d b=%d n=%lld sum=%.4f sq=%.4f\n", a, b, pairN[a][b], pairSum[a][b], pairSq[a][b]);
    std::printf("decisions: %lld, avg %.3f ms, max %.2f ms\n", decisions, timeSum / std::max(1LL, decisions), timeMax);
    std::printf("rejected actions: %lld (fallback rejected: %lld, stuck turns: %lld)\n", rejected, fallbackRejected,
                stuckTurns);
    return (rejected || fallbackRejected || stuckTurns) ? 1 : 0;
}
