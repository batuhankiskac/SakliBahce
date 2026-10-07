// Headless dama bot-vs-bot simulation for SaklıBahçe: the level ladder (Acemi < Usta < Kurt).
//
//   dama_sim --games N --seed S --levels a,b [--opening K] [--threads T] [--style A,B] [--verbose]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt. Dama has no dice, so every deal starts with K random plies (default 4, the
// same for both games of a pair, chosen from the deal's seed) and is played twice with the sides swapped (duplicate
// pairs): the paired result removes the luck of the opening and of who starts. A game counts 1 / 0.5 / 0 (win, draw,
// loss) for level a. Every bot move goes through Game::applyMove; a rejected move is printed and counted (must be 0).
// Decision times are measured for every search and reported as p50 / p99 / max per level.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Dama.cpp src/core/DamaBot.cpp tests/dama_sim.cpp
#include "core/Dama.h"
#include "core/DamaBot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace dama;

namespace {

const char* levelName(int l) { return l == 0 ? "Acemi" : l == 1 ? "Usta" : "Kurt"; }

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

struct Out {
    int winner = -1; // player
    EndReason reason = EndReason::None;
    int plies = 0;
    bool stuck = false;
};

struct Shared {
    std::mutex mu;
    std::array<std::vector<float>, 3> times;
    long rejected = 0;
    std::array<int, 7> reasons{};
};

Out playGame(uint64_t seed, int opening, int levelP0, int levelP1, float style0, float style1, Shared& sh, bool verbose) {
    Rules r;
    r.winsNeeded = 1;
    Game g(r);
    g.setPlayer(0, "A", false);
    g.setPlayer(1, "B", false);
    g.startMatch(seed);
    Bot bots[2] = {Bot((BotLevel)levelP0, mix64(seed * 2 + 1)), Bot((BotLevel)levelP1, mix64(seed * 2 + 2))};
    bots[0].setStyle(okey::BotStyle{style0});
    bots[1].setStyle(okey::BotStyle{style1});
    okey::Rng rng(mix64(seed ^ 0x0BE41ull));
    const int levels[2] = {levelP0, levelP1};
    std::array<std::vector<float>, 3> times;
    long rejected = 0;
    Out out;
    for (int ply = 0; g.stage() == Stage::Playing && ply < 600; ++ply) {
        const int p = g.current();
        Move m;
        if (ply < opening) {
            const std::vector<Move>& l = g.legalMoves();
            m = l[(size_t)rng.range((int)l.size())];
        } else {
            const double t0 = nowMs();
            m = bots[p].choose(g, p);
            if (g.legalMoves().size() > 1) times[(size_t)levels[p]].push_back((float)(nowMs() - t0));
        }
        const ActionResult res = g.applyMove(p, m);
        if (!res.ok) {
            ++rejected;
            std::printf("REJECTED (%s, seed %llu ply %d): %s %s\n", levelName(levels[p]), (unsigned long long)seed, ply,
                        moveNotation(m).c_str(), res.error.c_str());
            g.applyMove(p, g.legalMoves()[0]);
        }
    }
    if (g.stage() == Stage::Playing) out.stuck = true;
    out.winner = g.lastResult().winner;
    out.reason = g.lastResult().reason;
    out.plies = g.lastResult().plies;
    if (verbose)
        std::printf("seed %llu: %s(P0) vs %s(P1): %s, %s, %d plies\n", (unsigned long long)seed, levelName(levelP0),
                    levelName(levelP1), out.winner < 0 ? "draw" : out.winner == 0 ? "P0" : "P1", endReasonText(out.reason),
                    out.plies);
    std::lock_guard<std::mutex> lk(sh.mu);
    for (int l = 0; l < 3; ++l) sh.times[(size_t)l].insert(sh.times[(size_t)l].end(), times[(size_t)l].begin(), times[(size_t)l].end());
    sh.rejected += rejected;
    sh.reasons[(size_t)out.reason]++;
    return out;
}

float pctl(std::vector<float> v, double q) {
    if (v.empty()) return 0.f;
    std::sort(v.begin(), v.end());
    return v[(size_t)std::min<double>((double)v.size() - 1, std::floor(q * (double)v.size()))];
}

} // namespace

int main(int argc, char** argv) {
    int games = 20, a = 2, b = 1, opening = 4, threads = (int)std::max(1u, std::thread::hardware_concurrency());
    uint64_t seed = 1;
    float styleA = 0.f, styleB = 0.f;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--games") && i + 1 < argc) games = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--levels") && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &a, &b);
        else if (!std::strcmp(argv[i], "--opening") && i + 1 < argc) opening = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--style") && i + 1 < argc) std::sscanf(argv[++i], "%f,%f", &styleA, &styleB);
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    }
    a = std::clamp(a, 0, 2);
    b = std::clamp(b, 0, 2);
    const int pairs = std::max(1, games / 2);
    Shared sh;
    std::vector<double> pairScore((size_t)pairs, 0.0);
    std::atomic<int> nextPair{0}, stuck{0};
    std::array<std::atomic<int>, 3> wdl{}; // a's wins, draws, losses
    std::atomic<long> plies{0};
    auto work = [&]() {
        for (int i; (i = nextPair++) < pairs;) {
            const uint64_t s = mix64(seed * 1000003ull + (uint64_t)i);
            double sc = 0.0;
            for (int side = 0; side < 2; ++side) {
                const Out o = side == 0 ? playGame(s, opening, a, b, styleA, styleB, sh, verbose)
                                        : playGame(s, opening, b, a, styleB, styleA, sh, verbose);
                const int aPlayer = side == 0 ? 0 : 1;
                const double v = o.winner < 0 ? 0.5 : (o.winner == aPlayer ? 1.0 : 0.0);
                sc += v;
                wdl[v == 1.0 ? 0 : v == 0.5 ? 1 : 2]++;
                plies += o.plies;
                if (o.stuck) ++stuck;
            }
            pairScore[(size_t)i] = sc / 2.0;
        }
    };
    const double t0 = nowMs();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(work);
    for (std::thread& t : pool) t.join();
    double mean = 0.0, var = 0.0;
    for (double v : pairScore) mean += v;
    mean /= pairs;
    for (double v : pairScore) var += (v - mean) * (v - mean);
    const double se = pairs > 1 ? std::sqrt(var / (pairs - 1) / pairs) : 0.0;
    std::printf("dama_sim: %s vs %s, %d games (%d duplicate pairs, %d random opening plies), seed %llu, %.1f s\n",
                levelName(a), levelName(b), pairs * 2, pairs, opening, (unsigned long long)seed, (nowMs() - t0) / 1000.0);
    std::printf("  %s: %d won, %d drawn, %d lost: score %.1f%% +- %.1f%% (paired s.e.), %.0f plies a game\n", levelName(a),
                wdl[0].load(), wdl[1].load(), wdl[2].load(), 100.0 * mean, 100.0 * se, (double)plies / (pairs * 2));
    std::printf("  endings:");
    for (int r = 1; r < 7; ++r)
        if (sh.reasons[(size_t)r]) std::printf(" %s %d,", endReasonText((EndReason)r), sh.reasons[(size_t)r]);
    std::printf("\n");
    for (int l = 0; l < 3; ++l) {
        if (sh.times[(size_t)l].empty()) continue;
        std::printf("  %s decisions: %zu, p50 %.1f ms, p99 %.1f ms, max %.1f ms\n", levelName(l), sh.times[(size_t)l].size(),
                    pctl(sh.times[(size_t)l], 0.5), pctl(sh.times[(size_t)l], 0.99), pctl(sh.times[(size_t)l], 1.0));
    }
    std::printf("  rejected moves: %ld, unfinished games: %d\n", sh.rejected, stuck.load());
    return sh.rejected || stuck ? 1 : 0;
}
