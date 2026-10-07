// Headless tavla bot-vs-bot simulation for SaklıBahçe.
//
//   tavla_sim --games N --seed S --levels a,b [--match P] [--doubling] [--variant klasik|gulbahar|fevga]
//             [--threads T] [--verbose]
//
// --variant: the tavla çeşidi (Rules::variant), klasik by default.
// levels: 0 = Acemi (Easy), 1 = Usta (Normal), 2 = Kurt (Hard). Games are played in duplicate pairs: every
// deal (seed) is played twice with the sides swapped. The dice sequence depends only on the seed (each roll
// draws from the game Rng in the same order whatever is played), so both levels get the same dice from the
// same seat; the paired difference over deals removes most of the dice luck from the standard error.
// Without --match every game is a 1-point "match" (one game: 1 point, mars 2). With --match P whole matches
// to P points are played and the match win rate is reported as well.
// --doubling plays with the katlama zarı: single games are then "money" games (worth base x cube, no match
// score to protect: played as the first game of a long match); the cube actions are counted per level.
// Every bot action goes through applyBotAction; a rejected action is printed (state + error), counted and
// replaced by fallbackAction (the count must be 0). Decision times are measured on the first step of every
// turn (when the bot computes its play) and reported as p50 / p99 / max per level.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Tavla.cpp src/core/TavlaBot.cpp tests/tavla_sim.cpp
//        -o build/tavla/tavla_sim
#include "core/Tavla.h"
#include "core/TavlaBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace tavla;

namespace {

const char* levelName(int l) {
    switch (l) {
    case 0: return "Acemi";
    case 1: return "Usta";
    default: return "Kurt";
    }
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

uint64_t mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string posStr(const Position& pos) {
    std::string s;
    for (int i = 0; i < 24; ++i) s += std::to_string(pos.pts[i]) + (i == 23 ? "" : " ");
    s += " | bar " + std::to_string(pos.bar[0]) + "/" + std::to_string(pos.bar[1]);
    s += " off " + std::to_string(pos.off[0]) + "/" + std::to_string(pos.off[1]);
    return s;
}

struct GameOut {
    int winnerSeat = -1;
    int points = 0;
    bool mars = false;
    int turns = 0;
    int cube = 1;
    bool dropped = false;
    bool stuck = false;
};

struct Stats {
    // per level index 0..2
    std::array<std::vector<float>, 3> times;
    long long rejected = 0;
    long long stuck = 0;
    long long games = 0;
    std::array<long long, 2> wins{};      // by side A / B of --levels
    std::array<long long, 2> points{};
    std::array<long long, 2> mars{};      // mars wins
    long long turns = 0;
    std::vector<double> dealNet;          // per deal: (points A - points B) summed over both games
    std::array<long long, 2> matchWins{};
    long long matches = 0;
    // katlama: per level index 0..2
    std::array<long long, 3> offers{}, takes{}, drops{};
    long long cubeSum = 0, droppedGames = 0;
};

// Plays one match (one game when matchPoints == 1). seatLevel[s] = level of seat s. Returns per-game outcomes.
Variant gVariant = Variant::Klasik;

std::vector<GameOut> playMatch(uint64_t seed, const std::array<int, 2>& seatLevel, int matchPoints, bool doubling,
                               Stats& st, bool verbose, int& matchWinnerSeat) {
    Rules r;
    r.variant = gVariant;
    // a single game (matchPoints 0) is the first game of a long match: no score to protect
    const bool single = matchPoints == 0;
    r.matchPoints = single ? 1000 : matchPoints;
    r.doubling = doubling;
    Game g(r);
    g.setPlayer(0, std::string(levelName(seatLevel[0])) + "-0", false);
    g.setPlayer(1, std::string(levelName(seatLevel[1])) + "-1", false);
    std::array<Bot, 2> bots = {Bot((BotLevel)seatLevel[0], mix64(seed ^ 1)), Bot((BotLevel)seatLevel[1], mix64(seed ^ 2))};
    g.startMatch(seed);
    std::vector<GameOut> outs;
    GameOut cur;
    int actions = 0;
    while (true) {
        for (const GameEvent& e : g.drainEvents()) {
            if (verbose) std::printf("  %s\n", e.text.c_str());
            if (e.type == EvType::GameStart) {
                bots[0].resetForGame();
                bots[1].resetForGame();
                cur = GameOut();
                actions = 0;
            }
            if (e.type == EvType::TurnEnd) ++cur.turns;
            if (e.type == EvType::DoubleOffer) ++st.offers[seatLevel[e.player]];
            if (e.type == EvType::DoubleTake) ++st.takes[seatLevel[e.player]];
            if (e.type == EvType::DoubleDrop) ++st.drops[seatLevel[e.player]];
        }
        const Stage s = g.stage();
        if (s == Stage::MatchOver || s == Stage::GameOver) {
            const GameResult& res = g.lastResult();
            cur.winnerSeat = res.winner;
            cur.points = res.points;
            cur.mars = res.mars;
            cur.cube = res.cube;
            cur.dropped = res.dropped;
            outs.push_back(cur);
            if (s == Stage::MatchOver) break;
            if (single) {
                matchWinnerSeat = res.winner;
                return outs;
            }
            g.startNextGame();
            continue;
        }
        if (++actions > 20000) { // safety: a game never needs this many actions
            cur.stuck = true;
            ++st.stuck;
            std::printf("STUCK seed %llu: %s\n", (unsigned long long)seed, posStr(g.position()).c_str());
            matchWinnerSeat = -1;
            outs.push_back(cur);
            return outs;
        }
        const int p = s == Stage::OpeningRoll ? 0 : (g.responder() >= 0 ? g.responder() : g.current());
        const bool decision = s == Stage::Moving && g.turnSteps().empty();
        const double t0 = nowMs();
        BotAction a = bots[p].next(g, p);
        const double dt = nowMs() - t0;
        if (decision && a.kind == BotAction::Kind::Step) st.times[seatLevel[p]].push_back((float)dt);
        ActionResult res = applyBotAction(g, p, a);
        if (!res.ok) {
            ++st.rejected;
            std::printf("REJECTED seed %llu seat %d kind %d %d->%d die %d: %s\n    %s dice %d-%d\n",
                        (unsigned long long)seed, p, (int)a.kind, a.from, a.to, a.die, res.error.c_str(),
                        posStr(g.position()).c_str(), g.dice().d1, g.dice().d2);
            a = fallbackAction(g, p);
            res = applyBotAction(g, p, a);
            if (!res.ok) {
                std::printf("FALLBACK REJECTED too: %s\n", res.error.c_str());
                ++st.stuck;
                matchWinnerSeat = -1;
                return outs;
            }
        }
    }
    matchWinnerSeat = g.matchWinner();
    return outs;
}

double percentile(std::vector<float> v, double q) {
    if (v.empty()) return 0.0;
    const size_t k = std::min(v.size() - 1, (size_t)(q * (double)v.size()));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

} // namespace

int main(int argc, char** argv) {
    long long games = 1000;
    uint64_t seed = 1;
    int la = 2, lb = 1;
    int matchPoints = 0;
    bool doubling = false;
    int threads = (int)std::max(1u, std::thread::hardware_concurrency());
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--games") && i + 1 < argc) games = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--levels") && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &la, &lb) != 2) { std::printf("--levels a,b\n"); return 2; }
        } else if (!std::strcmp(argv[i], "--match") && i + 1 < argc) matchPoints = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--doubling")) doubling = true;
        else if (!std::strcmp(argv[i], "--variant") && i + 1 < argc) {
            const std::string v = argv[++i];
            if (v == "klasik") gVariant = Variant::Klasik;
            else if (v == "gulbahar") gVariant = Variant::Gulbahar;
            else if (v == "fevga") gVariant = Variant::Fevga;
            else { std::printf("--variant klasik|gulbahar|fevga\n"); return 2; }
        } else {
            std::printf("usage: tavla_sim --games N --seed S --levels a,b [--match P] [--doubling] [--variant V] [--threads T] [--verbose]\n");
            return 2;
        }
    }
    if (verbose) threads = 1;
    const long long deals = std::max(1LL, games / 2);
    std::vector<Stats> per((size_t)threads);
    std::vector<std::thread> pool;
    const double t0 = nowMs();
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t]() {
            Stats& st = per[(size_t)t];
            for (long long d = t; d < deals; d += threads) {
                const uint64_t s = mix64(seed * 1000003ull + (uint64_t)d);
                double net = 0;
                for (int swap = 0; swap < 2; ++swap) {
                    // side A (= level la) sits in seat `swap`
                    std::array<int, 2> seatLevel = swap ? std::array<int, 2>{lb, la} : std::array<int, 2>{la, lb};
                    const int seatA = swap;
                    int mw = -1;
                    const std::vector<GameOut> outs = playMatch(s, seatLevel, matchPoints, doubling, st, verbose, mw);
                    for (const GameOut& o : outs) {
                        if (o.stuck || o.winnerSeat < 0) continue;
                        const int side = o.winnerSeat == seatA ? 0 : 1;
                        ++st.games;
                        ++st.wins[side];
                        st.points[side] += o.points;
                        st.mars[side] += o.mars;
                        st.turns += o.turns;
                        st.cubeSum += o.cube;
                        st.droppedGames += o.dropped;
                        net += side == 0 ? o.points : -o.points;
                    }
                    if (matchPoints && mw >= 0) {
                        ++st.matches;
                        ++st.matchWins[mw == seatA ? 0 : 1];
                    }
                }
                st.dealNet.push_back(net);
            }
        });
    }
    for (std::thread& th : pool) th.join();
    const double wall = nowMs() - t0;

    Stats all;
    for (Stats& s : per) {
        for (int l = 0; l < 3; ++l) all.times[l].insert(all.times[l].end(), s.times[l].begin(), s.times[l].end());
        all.rejected += s.rejected;
        all.stuck += s.stuck;
        all.games += s.games;
        all.turns += s.turns;
        all.matches += s.matches;
        all.cubeSum += s.cubeSum;
        all.droppedGames += s.droppedGames;
        for (int l = 0; l < 3; ++l) {
            all.offers[l] += s.offers[l];
            all.takes[l] += s.takes[l];
            all.drops[l] += s.drops[l];
        }
        for (int k = 0; k < 2; ++k) {
            all.wins[k] += s.wins[k];
            all.points[k] += s.points[k];
            all.mars[k] += s.mars[k];
            all.matchWins[k] += s.matchWins[k];
        }
        all.dealNet.insert(all.dealNet.end(), s.dealNet.begin(), s.dealNet.end());
    }

    const double n = (double)all.games;
    const double wr = n > 0 ? all.wins[0] / n : 0;
    const double wrSe = n > 0 ? std::sqrt(wr * (1 - wr) / n) : 0;
    // points per game for A minus B, paired by deal
    double mean = 0, sq = 0;
    for (double x : all.dealNet) mean += x;
    const double nd = (double)all.dealNet.size();
    mean /= std::max(1.0, nd);
    for (double x : all.dealNet) sq += (x - mean) * (x - mean);
    const double sd = nd > 1 ? std::sqrt(sq / (nd - 1)) : 0;
    const double gamesPerDeal = nd > 0 ? n / nd : 2;
    const double ppg = mean / gamesPerDeal;
    const double ppgSe = nd > 0 ? sd / std::sqrt(nd) / gamesPerDeal : 0;

    std::printf("tavla_sim: %s (A) vs %s (B), %lld games (%lld duplicate deals), seed %llu, %s%s%s, %d threads, %.1f s\n",
                levelName(la), levelName(lb), all.games, (long long)nd, (unsigned long long)seed,
                matchPoints ? ("matches to " + std::to_string(matchPoints)).c_str() : "single games",
                doubling ? " with the cube" : "",
                gVariant == Variant::Klasik ? "" : (std::string(", ") + variantName(gVariant)).c_str(), threads, wall / 1000.0);
    std::printf("  A game win rate     : %.2f%% +- %.2f%%\n", 100 * wr, 100 * wrSe);
    std::printf("  A net points / game : %+.4f +- %.4f (paired by deal, z = %.1f)\n", ppg, ppgSe, ppgSe > 0 ? ppg / ppgSe : 0.0);
    std::printf("  points / game       : A %.3f  B %.3f\n", all.points[0] / std::max(1.0, n), all.points[1] / std::max(1.0, n));
    std::printf("  mars rate (of wins) : A %.2f%%  B %.2f%%   (all games %.2f%%)\n",
                100.0 * all.mars[0] / std::max(1LL, all.wins[0]), 100.0 * all.mars[1] / std::max(1LL, all.wins[1]),
                100.0 * (all.mars[0] + all.mars[1]) / std::max(1.0, n));
    std::printf("  turns / game        : %.1f\n", all.turns / std::max(1.0, n));
    if (doubling) {
        std::printf("  cube                : mean value %.2f, %.1f%% of games dropped\n", all.cubeSum / std::max(1.0, n),
                    100.0 * all.droppedGames / std::max(1.0, n));
        for (int l : {la, lb}) {
            if (l == lb && la == lb) break;
            std::printf("  %-5s cube actions  : %lld doubles, %lld takes, %lld drops\n", levelName(l), all.offers[l],
                        all.takes[l], all.drops[l]);
        }
    }
    if (matchPoints) {
        const double m = (double)all.matches;
        const double mw = m > 0 ? all.matchWins[0] / m : 0;
        std::printf("  A match win rate    : %.2f%% +- %.2f%% (%lld matches)\n", 100 * mw,
                    100 * (m > 0 ? std::sqrt(mw * (1 - mw) / m) : 0), all.matches);
    }
    for (int l : {la, lb}) {
        if (l == lb && la == lb) break;
        const std::vector<float>& t = all.times[l];
        float mx = 0;
        double sum = 0;
        for (float x : t) { mx = std::max(mx, x); sum += x; }
        std::printf("  %-5s decision ms  : mean %.3f  p50 %.3f  p99 %.3f  max %.3f  (%zu decisions)\n", levelName(l),
                    t.empty() ? 0.0 : sum / (double)t.size(), percentile(t, 0.5), percentile(t, 0.99), (double)mx, t.size());
    }
    std::printf("  rejected actions    : %lld   stuck games: %lld\n", all.rejected, all.stuck);
    return (all.rejected || all.stuck) ? 1 : 0;
}
