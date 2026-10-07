// Headless Altmışaltı (66) bot-vs-bot simulation for SaklıBahçe.
//
//   altmisalti_sim --matches N --seed S --levels a,b [--styles] [--threads T] [--verbose]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt. Matches (to 7 game points) are played in duplicate pairs: every seed is
// played twice with the players swapped. The deals depend only on the seed (the deck is shuffled from the game's Rng
// and nothing else draws from it), so both levels get the same cards from the same seat. Reported per level: matches
// won, game points per hand (the paired difference over seeds with its standard error), how the hands were won, how
// often each level closed the stock and how often that worked, and Kurt's decision times (p50 / p99 / max).
// Every bot action goes through applyBotAction; a rejected one is printed, counted and replaced by fallbackAction (the
// count must be 0). --styles plays a bold bot (Kel Mahmut) against a cautious one (Emekli Nuri), both at level a.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Altmisalti.cpp src/core/AltmisaltiBot.cpp
//        tests/altmisalti_sim.cpp -o build/altmisalti/altmisalti_sim
#include "core/Altmisalti.h"
#include "core/AltmisaltiBot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace altmisalti;

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

struct SideStats {
    int matches = 0, wins = 0, hands = 0;
    long gamePoints = 0, gamePointsLost = 0;
    int won1 = 0, won2 = 0, won3 = 0;
    int closes = 0, closesWon = 0, exchanges = 0, marriages = 0;
    std::vector<double> times;
};

struct MatchOut {
    std::array<SideStats, 2> side;   // by slot (0 = level a, 1 = level b)
    int rejected = 0;
    int hands = 0;
};

MatchOut playMatch(uint64_t seed, const std::array<int, 2>& slotLevel, bool swap, bool styles, bool verbose) {
    MatchOut out;
    Game g;
    // player p is played by slot (p ^ swap)
    std::array<std::unique_ptr<Bot>, 2> bots;
    for (int p = 0; p < 2; ++p) {
        const int slot = p ^ (swap ? 1 : 0);
        g.setPlayer(p, slot == 0 ? "A" : "B", false);
        bots[(size_t)p] = std::make_unique<Bot>((BotLevel)slotLevel[(size_t)slot], mix64(seed * 2 + (uint64_t)p));
        if (styles) bots[(size_t)p]->setStyle(slot == 0 ? BotStyle::bold() : BotStyle::cautious());
    }
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && ++guard < 20000) {
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int p = g.current();
        const int slot = p ^ (swap ? 1 : 0);
        const double t0 = nowMs();
        BotAction a = bots[(size_t)p]->next(g, p);
        const double dt = nowMs() - t0;
        if (slotLevel[(size_t)slot] == 2) out.side[(size_t)slot].times.push_back(dt);
        if (a.kind == BotAction::Kind::Close) ++out.side[(size_t)slot].closes;
        if (a.kind == BotAction::Kind::Exchange) ++out.side[(size_t)slot].exchanges;
        if (a.kind == BotAction::Kind::Play && g.marriageValue(p, a.card) > 0) ++out.side[(size_t)slot].marriages;
        const ActionResult r = applyBotAction(g, p, a);
        if (!r.ok) {
            ++out.rejected;
            std::printf("REJECTED seed %llu player %d kind %d card %d: %s\n", (unsigned long long)seed, p, (int)a.kind,
                        a.card, r.error.c_str());
            applyBotAction(g, p, fallbackAction(g, p));
        }
        g.drainEvents();
    }
    for (const HandRecord& h : g.sheet()) {
        ++out.hands;
        for (int p = 0; p < 2; ++p) {
            SideStats& s = out.side[(size_t)(p ^ (swap ? 1 : 0))];
            ++s.hands;
            if (h.winner == p) {
                s.gamePoints += h.gamePoints;
                if (h.gamePoints == 1) ++s.won1;
                else if (h.gamePoints == 2) ++s.won2;
                else ++s.won3;
            } else {
                s.gamePointsLost += h.gamePoints;
            }
            if (h.closer == p && h.winner == p) ++s.closesWon;
        }
        if (verbose)
            std::printf("  seed %llu hand %d: winner %d +%d reason %d closer %d pts %d-%d\n", (unsigned long long)seed,
                        h.index, h.winner, h.gamePoints, (int)h.reason, h.closer, h.points[0], h.points[1]);
    }
    const int w = g.matchWinner();
    for (int p = 0; p < 2; ++p) {
        SideStats& s = out.side[(size_t)(p ^ (swap ? 1 : 0))];
        ++s.matches;
        if (w == p) ++s.wins;
    }
    return out;
}

double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[(size_t)std::min<double>((double)v.size() - 1, q * (double)(v.size() - 1))];
}

} // namespace

int main(int argc, char** argv) {
    int matches = 40;
    uint64_t seed = 1;
    std::array<int, 2> levels{{2, 1}};
    bool verbose = false, styles = false;
    int threads = (int)std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--matches") matches = std::atoi(val());
        else if (a == "--seed") seed = std::strtoull(val(), nullptr, 10);
        else if (a == "--levels") {
            const char* v = val();
            int k = 0;
            for (const char* p = v; *p && k < 2; ++p)
                if (*p >= '0' && *p <= '2') levels[(size_t)k++] = *p - '0';
        } else if (a == "--threads") threads = std::max(1, std::atoi(val()));
        else if (a == "--verbose") verbose = true;
        else if (a == "--styles") styles = true;
        else {
            std::printf("unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (styles) levels[1] = levels[0];
    const double t0 = nowMs();
    std::vector<std::array<MatchOut, 2>> res((size_t)matches);
    std::atomic<int> nextIdx{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&]() {
            for (int i; (i = nextIdx++) < matches;) {
                const uint64_t s = mix64(seed * 7919 + (uint64_t)i);
                res[(size_t)i][0] = playMatch(s, levels, false, styles, verbose);
                res[(size_t)i][1] = playMatch(s, levels, true, styles, verbose);
            }
        });
    for (std::thread& t : pool) t.join();

    std::array<SideStats, 2> tot;
    int rejected = 0;
    std::vector<double> pairDiff; // per seed: (gp won - gp lost) per hand of slot 0, over both games
    for (const auto& pr : res) {
        double num = 0, den = 0;
        for (const MatchOut& m : pr) {
            rejected += m.rejected;
            for (int s = 0; s < 2; ++s) {
                SideStats& T = tot[(size_t)s];
                const SideStats& S = m.side[(size_t)s];
                T.matches += S.matches;
                T.wins += S.wins;
                T.hands += S.hands;
                T.gamePoints += S.gamePoints;
                T.gamePointsLost += S.gamePointsLost;
                T.won1 += S.won1;
                T.won2 += S.won2;
                T.won3 += S.won3;
                T.closes += S.closes;
                T.closesWon += S.closesWon;
                T.exchanges += S.exchanges;
                T.marriages += S.marriages;
                T.times.insert(T.times.end(), S.times.begin(), S.times.end());
            }
            num += (double)(m.side[0].gamePoints - m.side[0].gamePointsLost);
            den += (double)m.hands;
        }
        pairDiff.push_back(den > 0 ? num / den : 0.0);
    }
    double mean = 0, m2 = 0;
    for (double d : pairDiff) mean += d;
    mean /= (double)std::max<size_t>(1, pairDiff.size());
    for (double d : pairDiff) m2 += (d - mean) * (d - mean);
    const double se = pairDiff.size() > 1 ? std::sqrt(m2 / (double)(pairDiff.size() - 1) / (double)pairDiff.size()) : 0.0;

    std::printf("Altmışaltı sim: %d seeds x 2 (duplicate), levels %s vs %s%s, %.1f s\n", matches, levelName(levels[0]),
                levelName(levels[1]), styles ? " (bold vs cautious)" : "", (nowMs() - t0) / 1000.0);
    for (int s = 0; s < 2; ++s) {
        const SideStats& T = tot[(size_t)s];
        std::printf("  %-5s%s matches %d/%d (%.1f%%)  hands %d  game pts won %ld lost %ld  (1:%d 2:%d 3:%d)  closes %d (won %d)"
                    "  exchanges %d  marriages %d\n",
                    levelName(levels[(size_t)s]), styles ? (s == 0 ? " bold" : " caut") : "", T.wins, T.matches,
                    100.0 * T.wins / std::max(1, T.matches), T.hands, T.gamePoints, T.gamePointsLost, T.won1, T.won2, T.won3,
                    T.closes, T.closesWon, T.exchanges, T.marriages);
        if (!T.times.empty())
            std::printf("        Kurt decision ms: p50 %.1f  p99 %.1f  max %.1f  (%zu decisions)\n", pct(T.times, 0.5),
                        pct(T.times, 0.99), pct(T.times, 1.0), T.times.size());
    }
    std::printf("  paired game points per hand (%s - %s): %+.3f +- %.3f\n", levelName(levels[0]), levelName(levels[1]), mean, se);
    std::printf("  rejected actions: %d\n", rejected);
    return rejected == 0 ? 0 : 1;
}
