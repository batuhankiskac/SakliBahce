// Headless Pişti bot-vs-bot simulation for SaklıBahçe.
//
//   pisti_sim --hands N --seed S --levels a,b,c,d [--mode bireysel|esli|ikili] [--duplicate] [--rotate]
//             [--matches M] [--target T] [--verbose]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt, one per SEAT (in ikili only seats 0 and 2 play).
// Every deal is a fresh one-hand game (same seed -> same cards and dealer). --rotate shifts the level
// assignment by one active seat every deal; --duplicate replays every deal once per distinct ARRANGEMENT of
// the levels over the seats (all permutations: in pişti it matters who plays right after whom), so each level
// plays every seat and every neighbour with the same cards; the comparison is then reported as a paired
// difference over deals (removes most of the luck of the deal from the standard error).
// The per-seat-hand score is "own side's hand points minus the mean of the other sides" (zero-sum).
// --matches M additionally plays M full matches to the target (rotating the levels every match, or cycling
// through all arrangements with --duplicate) and reports match wins per level.
// Every bot move goes through Game::playCard; a rejected move is printed, counted, and replaced by
// fallbackCard.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Pisti.cpp src/core/PistiBot.cpp
//        tests/pisti_sim.cpp -o build/pisti/pisti_sim
#include "core/Pisti.h"
#include "core/PistiBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace pisti;

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

double percentile(std::vector<float> v, double q) {
    if (v.empty()) return 0.0;
    const size_t k = std::min(v.size() - 1, (size_t)(q * (double)v.size()));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

struct LevelStats {
    long long seatHands = 0;
    double sum = 0, sq = 0;      // per seat-hand score (own side minus mean of others)
    long long rawPoints = 0;     // own side's raw hand points
    long long pistis = 0;
    long long captured = 0;
    double matchWins = 0;
    long long matchSeats = 0;
    std::vector<float> times;
};

struct Running {
    long long n = 0;
    double sum = 0, sq = 0;
    void add(double x) {
        ++n;
        sum += x;
        sq += x * x;
    }
    double mean() const { return n ? sum / n : 0.0; }
    double se() const {
        if (n < 2) return 0.0;
        const double m = mean();
        return std::sqrt(std::max(0.0, (sq / n - m * m) / (double)(n - 1)));
    }
};

long long g_rejected = 0;

// Plays the current hand to its end with the given bots; records decision times per level.
void playHand(Game& g, std::array<Bot, 4>& bots, const std::array<int, 4>& lv, std::array<LevelStats, 3>& st,
              bool verbose) {
    for (int s = 0; s < 4; ++s) bots[s].resetForHand();
    auto pump = [&]() {
        for (const GameEvent& e : g.drainEvents()) {
            if (verbose) std::printf("  %s\n", e.text.c_str());
            for (int s : g.activeSeats()) bots[s].observe(e, g);
        }
    };
    pump();
    while (g.stage() == Stage::Playing) {
        const int s = g.current();
        const double t0 = nowMs();
        int c = bots[s].next(g, s);
        const double dt = nowMs() - t0;
        st[lv[s]].times.push_back((float)dt);
        ActionResult r = g.playCard(s, c);
        if (!r.ok) {
            ++g_rejected;
            std::printf("REJECTED seat %d (%s) card %s: %s\n", s, levelName(lv[s]), kart::cardNameTR(c).c_str(),
                        r.error.c_str());
            r = g.playCard(s, fallbackCard(g, s));
            if (!r.ok) {
                std::printf("fallback rejected too: %s\n", r.error.c_str());
                std::exit(2);
            }
        }
        pump();
    }
}

} // namespace

int main(int argc, char** argv) {
    int hands = 1000;
    uint64_t seed = 1;
    std::array<int, 4> levels{2, 1, 0, 1};
    Mode mode = Mode::Bireysel;
    bool duplicate = false, rotate = false, verbose = false;
    int matches = 0;
    int target = 101;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--hands") hands = std::atoi(val());
        else if (a == "--seed") seed = std::strtoull(val(), nullptr, 10);
        else if (a == "--levels") {
            const std::string v = val();
            int k = 0;
            for (size_t p = 0; p < v.size() && k < 4; ++p)
                if (v[p] >= '0' && v[p] <= '2') levels[k++] = v[p] - '0';
        } else if (a == "--mode") {
            const std::string m = val();
            mode = m == "esli" ? Mode::Esli : m == "ikili" ? Mode::Ikili : Mode::Bireysel;
        } else if (a == "--duplicate") duplicate = true;
        else if (a == "--rotate") rotate = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--matches") matches = std::atoi(val());
        else if (a == "--target") target = std::atoi(val());
        else {
            std::printf("unknown option %s\n", a.c_str());
            return 1;
        }
    }

    Rules rules;
    rules.mode = mode;
    rules.targetScore = 1000000; // one-hand games for the deal comparison
    Game probe(rules);
    const std::vector<int> active = probe.activeSeats();
    const int nA = (int)active.size();
    std::vector<int> activeLevels;
    for (int s : active) activeLevels.push_back(levels[s]);
    bool present[3] = {false, false, false};
    for (int l : activeLevels) present[l] = true;

    std::printf("Pişti sim: %d deals, seed %llu, mode %s, levels", hands, (unsigned long long)seed,
                mode == Mode::Esli ? "esli" : mode == Mode::Ikili ? "ikili" : "bireysel");
    for (int s : active) std::printf(" s%d=%s", s, levelName(levels[s]));
    std::printf("%s\n", duplicate ? " (duplicate)" : rotate ? " (rotate)" : "");

    std::array<LevelStats, 3> st;
    std::map<std::pair<int, int>, Running> paired; // (A, B) -> per-deal mean(A) - mean(B)
    // --duplicate: every distinct arrangement of the levels over the active seats (all permutations, not just
    // rotations: who plays right after whom matters in pişti — the next player feeds on your discards).
    std::vector<std::vector<int>> arrangements;
    {
        std::vector<int> p = activeLevels;
        std::sort(p.begin(), p.end());
        do arrangements.push_back(p);
        while (std::next_permutation(p.begin(), p.end()));
    }
    const int rotations = duplicate ? (int)arrangements.size() : 1;

    for (int d = 0; d < hands; ++d) {
        double dealSum[3] = {0, 0, 0};
        int dealN[3] = {0, 0, 0};
        for (int rot = 0; rot < rotations; ++rot) {
            const int shift = rotate ? d % nA : 0;
            std::array<int, 4> lv{0, 0, 0, 0};
            for (int i = 0; i < nA; ++i)
                lv[active[i]] = duplicate ? arrangements[rot][i] : activeLevels[(i + shift) % nA];
            Game g(rules);
            for (int s = 0; s < 4; ++s) g.setPlayer(s, std::string("Bot") + char('A' + s), false);
            std::array<Bot, 4> bots{Bot(BotLevel(lv[0]), seed * 1000003ull + d * 17 + 1),
                                    Bot(BotLevel(lv[1]), seed * 1000003ull + d * 17 + 2),
                                    Bot(BotLevel(lv[2]), seed * 1000003ull + d * 17 + 3),
                                    Bot(BotLevel(lv[3]), seed * 1000003ull + d * 17 + 4)};
            g.startMatch(seed * 7919ull + (uint64_t)d);
            if (verbose) std::printf("deal %d rot %d\n", d, rot);
            playHand(g, bots, lv, st, verbose);
            const HandResult& r = g.lastHandResult();
            for (int s : active) {
                const int sd = g.sideOf(s);
                double others = 0;
                for (int i = 0; i < r.numSides; ++i)
                    if (i != sd) others += r.side[i].total;
                const double x = r.side[sd].total - others / (r.numSides - 1);
                LevelStats& L = st[lv[s]];
                ++L.seatHands;
                L.sum += x;
                L.sq += x * x;
                L.rawPoints += r.side[sd].total;
                L.pistis += r.seat[s].pistis + r.seat[s].jackPistis;
                L.captured += r.seat[s].cards;
                dealSum[lv[s]] += x;
                ++dealN[lv[s]];
            }
        }
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                if (a > b && dealN[a] && dealN[b]) paired[{a, b}].add(dealSum[a] / dealN[a] - dealSum[b] / dealN[b]);
    }

    std::printf("\nper seat-hand score = own side's points - mean of the other sides\n");
    std::printf("%-6s %9s %9s %8s %9s %8s %9s | ms p50 %7s %7s %7s\n", "level", "seatHands", "mean", "±se",
                "rawPts", "pişti", "cards", "p99", "max", "mean");
    for (int l = 0; l < 3; ++l) {
        if (!present[l]) continue;
        const LevelStats& L = st[l];
        const double n = (double)std::max(1LL, L.seatHands);
        const double m = L.sum / n;
        const double se = std::sqrt(std::max(0.0, (L.sq / n - m * m) / std::max(1.0, n - 1)));
        double tsum = 0, tmax = 0;
        for (float t : L.times) {
            tsum += t;
            tmax = std::max(tmax, (double)t);
        }
        std::printf("%-6s %9lld %+9.3f %8.3f %9.2f %8.3f %9.2f | %9.3f %7.3f %7.3f %7.3f\n", levelName(l), L.seatHands,
                    m, se, L.rawPoints / n, L.pistis / n, L.captured / n, percentile(L.times, 0.5),
                    percentile(L.times, 0.99), tmax, L.times.empty() ? 0.0 : tsum / L.times.size());
    }
    if (duplicate) {
        std::printf("\npaired over deals (per-deal mean difference):\n");
        for (const auto& kv : paired) {
            const Running& R = kv.second;
            const double z = R.se() > 0 ? R.mean() / R.se() : 0.0;
            std::printf("  %s - %s: %+7.3f ± %.3f  (z = %.1f, %lld deals)\n", levelName(kv.first.first),
                        levelName(kv.first.second), R.mean(), R.se(), z, R.n);
        }
    }

    if (matches > 0) {
        Rules mr = rules;
        mr.targetScore = target;
        long long handsPlayed = 0;
        for (int m = 0; m < matches; ++m) {
            std::array<int, 4> lv{0, 0, 0, 0};
            for (int i = 0; i < nA; ++i)
                lv[active[i]] = duplicate ? arrangements[m % arrangements.size()][i] : activeLevels[(i + m) % nA];
            Game g(mr);
            for (int s = 0; s < 4; ++s) g.setPlayer(s, std::string("Bot") + char('A' + s), false);
            std::array<Bot, 4> bots{Bot(BotLevel(lv[0]), seed + 11 * m + 1), Bot(BotLevel(lv[1]), seed + 11 * m + 2),
                                    Bot(BotLevel(lv[2]), seed + 11 * m + 3), Bot(BotLevel(lv[3]), seed + 11 * m + 4)};
            g.startMatch(seed * 104729ull + (uint64_t)m);
            for (;;) {
                playHand(g, bots, lv, st, false);
                ++handsPlayed;
                if (g.matchOver()) break;
                g.startNextHand();
            }
            const int w = g.winnerSide();
            for (int s : active) {
                ++st[lv[s]].matchSeats;
                if (g.sideOf(s) == w) st[lv[s]].matchWins += 1.0;
            }
        }
        std::printf("\nmatches to %d: %d matches, %.2f hands/match\n", target, matches, (double)handsPlayed / matches);
        for (int l = 0; l < 3; ++l) {
            if (!present[l] || !st[l].matchSeats) continue;
            const double p = st[l].matchWins / st[l].matchSeats;
            std::printf("  %-6s seat win rate %.3f ± %.3f (%lld seat-matches)\n", levelName(l), p,
                        std::sqrt(p * (1 - p) / st[l].matchSeats), st[l].matchSeats);
        }
    }
    std::printf("\nrejected actions: %lld\n", g_rejected);
    return g_rejected == 0 ? 0 : 3;
}
