// Headless bot-vs-bot simulation for Batak.
//
//   batak_sim --hands N --seed S --levels a,b,c,d [--esli] [--no-dup] [--verbose] [--slow MS]
//             [--tune SLOT:KEY=VALUE ...]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt, one per seat. By default every deal is played four times
// (duplicate), with the level assignment rotated one seat each time, so every level holds every hand from
// every seat; the level comparison is reported per level and as paired differences over deals (the luck of
// the deal cancels out of the standard error). N = number of deals (each played 4x). --no-dup plays each
// deal once with a rotation per deal instead.
//
// Metric: hand utility = own side's points - mean points of the other sides (eşli: own team - other team),
// i.e. the bots' own objective; raw points are reported too. A "slot" is a position in --levels; it moves
// around the table with the rotation, and --tune changes one Bot::debugTune knob for the slot's bot, so two
// variants of the same level can be compared (per-slot results are paired over deals as well). Every bot action goes through applyAction; a
// rejected action is printed, counted and replaced by fallbackAction.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Batak.cpp src/core/BatakBot.cpp
//        tests/batak_sim.cpp -o build/batak/batak_sim
#include "core/Batak.h"
#include "core/BatakBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

using namespace batak;

namespace {

const char* levelName(int l) { return levelNameTR((Level)l); }

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// CPU time of this thread: decision cost without the scheduler noise of a loaded machine.
double cpuMs() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

double percentile(std::vector<float> v, double q) {
    if (v.empty()) return 0.0;
    const size_t k = std::min(v.size() - 1, (size_t)(q * (double)v.size()));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

struct Acc {
    double n = 0, sum = 0, sq = 0;
    void add(double x) {
        n += 1;
        sum += x;
        sq += x * x;
    }
    double mean() const { return n > 0 ? sum / n : 0.0; }
    double se() const {
        if (n < 2) return 0.0;
        const double m = mean();
        return std::sqrt(std::max(0.0, (sq / n - m * m)) / (n - 1));
    }
};

struct LevelStats {
    Acc util, points;
    long long declared = 0, made = 0, forced = 0, forcedMade = 0;
    long long contractSum = 0, trickSumDecl = 0, overSum = 0;
    long long defenderHands = 0, defenderBatak = 0;
    std::vector<float> tBid, tTrump, tPlay; // thread CPU ms
    double wallMax = 0;
};

} // namespace

int main(int argc, char** argv) {
    int deals = 200;
    uint64_t seed = 1;
    int levels[4] = {2, 1, 0, 1};
    bool esli = false, dup = true, verbose = false;
    double slowMs = -1;
    struct Tune { int slot, key; double value; };
    std::vector<Tune> tunes;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hands") && i + 1 < argc) deals = std::atoi(argv[++i]);
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
        } else if (!std::strcmp(argv[i], "--esli")) esli = true;
        else if (!std::strcmp(argv[i], "--no-dup")) dup = false;
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--slow") && i + 1 < argc) slowMs = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--tune") && i + 1 < argc) {
            Tune t{};
            if (std::sscanf(argv[++i], "%d:%d=%lf", &t.slot, &t.key, &t.value) == 3) tunes.push_back(t);
        }
        else {
            std::printf("usage: batak_sim --hands N --seed S --levels a,b,c,d [--esli] [--no-dup] [--verbose] [--slow MS]\n");
            return 2;
        }
    }

    Rules rules = esli ? Rules::esliBatak() : Rules::tekli();
    rules.targetScore = 0;
    rules.numHands = 1 << 30;
    Game g(rules);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, "Bot" + std::to_string(s), false);
    g.startMatch(seed);
    g.drainEvents();

    std::array<LevelStats, 3> st;
    bool present[3] = {false, false, false};
    for (int s = 0; s < 4; ++s) present[levels[s]] = true;
    // paired: per deal, mean utility per level
    std::map<std::pair<int, int>, Acc> paired;
    Acc slotAcc[4];
    long long rejected = 0, handsPlayed = 0, allPass = 0, kings = 0;
    okey::Rng dealRng(seed * 7919 + 13);
    const int rotations = dup ? 4 : 1;
    const double t0 = nowMs();

    for (int d = 0; d < deals; ++d) {
        std::vector<int> deck = kart::shuffledDeck(dealRng);
        std::array<std::vector<int>, 4> hands;
        for (int i = 0; i < 52; ++i) hands[i % 4].push_back(deck[i]);
        const int dealer = dealRng.range(4);
        double levelUtil[3] = {0, 0, 0};
        int levelCnt[3] = {0, 0, 0};
        double slotUtil[4] = {0, 0, 0, 0};
        for (int rot = 0; rot < rotations; ++rot) {
            const int shift = dup ? rot : d % 4;
            int lv[4], slotOf[4];
            for (int s = 0; s < 4; ++s) {
                slotOf[s] = (s + shift) % 4;
                lv[s] = levels[slotOf[s]];
            }
            std::vector<Bot> bots;
            for (int s = 0; s < 4; ++s) {
                bots.emplace_back((Level)lv[s], seed * 1000003ull + (uint64_t)d * 31 + (uint64_t)s * 7 + 1);
                for (const Tune& t : tunes)
                    if (t.slot == slotOf[s]) bots.back().debugTune(t.key, t.value);
            }
            g.debugRedeal(dealer, hands);
            g.drainEvents();
            int guard = 0;
            while (g.stage() == Stage::Bidding || g.stage() == Stage::ChoosingTrump || g.stage() == Stage::Playing) {
                if (++guard > 2000) {
                    std::printf("stuck!\n");
                    return 1;
                }
                const int seat = g.current();
                const int ctl = g.controllerOf(seat);
                const Stage stage = g.stage();
                const double a0 = nowMs(), c0 = cpuMs();
                Action a = bots[ctl].next(g, seat);
                const double dt = cpuMs() - c0, wall = nowMs() - a0;
                LevelStats& ls = st[lv[ctl]];
                ls.wallMax = std::max(ls.wallMax, wall);
                (stage == Stage::Bidding ? ls.tBid : stage == Stage::ChoosingTrump ? ls.tTrump : ls.tPlay).push_back((float)dt);
                if (slowMs >= 0 && dt > slowMs)
                    std::printf("slow %.1f ms: level %s stage %d trick %d\n", dt, levelName(lv[ctl]), (int)stage, g.trickNumber());
                ActionResult r = applyAction(g, seat, a);
                if (!r.ok) {
                    ++rejected;
                    std::printf("REJECTED seat %d level %s: %s (kind %d card %d)\n", seat, levelName(lv[ctl]), r.error.c_str(),
                                (int)a.kind, a.card);
                    r = applyAction(g, seat, fallbackAction(g, seat));
                    if (!r.ok) {
                        std::printf("fallback rejected too: %s\n", r.error.c_str());
                        return 1;
                    }
                }
                for (const GameEvent& e : g.drainEvents()) {
                    if (verbose && e.type != EvType::TurnStart) std::printf("  %s\n", e.text.c_str());
                    if (e.type == EvType::Redeal) ++allPass;
                }
            }
            ++handsPlayed;
            const HandResult& hr = g.lastHandResult();
            if (hr.forced) ++allPass;
            if (hr.king) ++kings;
            const int ns = g.numSides();
            int pts[4] = {0, 0, 0, 0};
            for (const SideResult& sr : hr.sides) pts[sr.side] = sr.points;
            for (int s = 0; s < 4; ++s) {
                const int side = g.sideOf(s);
                double others = 0;
                for (int o = 0; o < ns; ++o)
                    if (o != side) others += pts[o];
                const double u = pts[side] - others / (ns - 1);
                LevelStats& ls = st[lv[s]];
                ls.util.add(u);
                ls.points.add(pts[side]);
                levelUtil[lv[s]] += u;
                slotUtil[slotOf[s]] += u;
                ++levelCnt[lv[s]];
                const bool decl = side == g.sideOf(hr.declarer);
                if (s == hr.declarer) {
                    ++ls.declared;
                    ls.contractSum += hr.contract;
                    const int t = hr.sides[side].tricks;
                    ls.trickSumDecl += t;
                    if (t >= hr.contract) {
                        ++ls.made;
                        ls.overSum += t - hr.contract;
                    }
                    if (hr.forced) {
                        ++ls.forced;
                        if (t >= hr.contract) ++ls.forcedMade;
                    }
                } else if (!decl) {
                    ++ls.defenderHands;
                    if (!hr.sides[side].made) ++ls.defenderBatak;
                }
            }
        }
        for (int k = 0; k < 4; ++k) slotAcc[k].add(slotUtil[k] / rotations);
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b)
                if (levelCnt[a] && levelCnt[b])
                    paired[{b, a}].add(levelUtil[b] / levelCnt[b] - levelUtil[a] / levelCnt[a]);
    }
    const double elapsed = nowMs() - t0;

    std::printf("Batak sim: %s, %d deals x %d = %lld hands, levels %s,%s,%s,%s, seed %llu (%.1f s)\n",
                esli ? "eşli" : "tekli", deals, rotations, handsPlayed, levelName(levels[0]), levelName(levels[1]),
                levelName(levels[2]), levelName(levels[3]), (unsigned long long)seed, elapsed / 1000.0);
    std::printf("rejected actions: %lld, forced (all-pass) contracts: %lld, kings: %lld\n", rejected, allPass, kings);
    std::printf("\n%-6s %16s %14s %8s %9s %8s %8s %8s %10s\n", "level", "utility/hand", "points/hand", "declared",
                "made%", "avgBid", "avgTrk", "over", "defBatak%");
    for (int l = 0; l < 3; ++l) {
        if (!present[l]) continue;
        const LevelStats& s = st[l];
        std::printf("%-6s %+7.3f ± %5.3f %+6.3f ± %5.3f %8lld %8.1f%% %8.2f %8.2f %8.2f %9.1f%%\n", levelName(l),
                    s.util.mean(), s.util.se(), s.points.mean(), s.points.se(), s.declared,
                    s.declared ? 100.0 * s.made / s.declared : 0.0, s.declared ? (double)s.contractSum / s.declared : 0.0,
                    s.declared ? (double)s.trickSumDecl / s.declared : 0.0, s.made ? (double)s.overSum / s.made : 0.0,
                    s.defenderHands ? 100.0 * s.defenderBatak / s.defenderHands : 0.0);
    }
    std::printf("\nforced contracts: ");
    for (int l = 0; l < 3; ++l)
        if (present[l]) std::printf("%s %lld (made %lld)  ", levelName(l), st[l].forced, st[l].forcedMade);
    std::printf("\n\npaired differences of mean utility per deal (± stderr, z):\n");
    for (const auto& kv : paired) {
        const Acc& a = kv.second;
        const double z = a.se() > 0 ? a.mean() / a.se() : 0.0;
        std::printf("  %s - %s: %+.3f ± %.3f  (z = %.1f, %d deals)\n", levelName(kv.first.first), levelName(kv.first.second),
                    a.mean(), a.se(), z, (int)a.n);
    }
    if (!tunes.empty()) {
        std::printf("\nper slot (mean utility per deal ± stderr):\n");
        for (int k = 0; k < 4; ++k) {
            std::printf("  slot %d %-6s %+.3f ± %.3f", k, levelName(levels[k]), slotAcc[k].mean(), slotAcc[k].se());
            for (const Tune& t : tunes)
                if (t.slot == k) std::printf("  [%d=%g]", t.key, t.value);
            std::printf("\n");
        }
    }
    std::printf("\ndecision times (thread CPU ms): p50 / p99 / max   [max wall ms, inflated on a loaded machine]\n");
    for (int l = 0; l < 3; ++l) {
        if (!present[l]) continue;
        const LevelStats& s = st[l];
        std::printf("  %-6s bid %.3f / %.3f / %.3f   koz %.3f / %.3f / %.3f   card %.3f / %.3f / %.3f\n", levelName(l),
                    percentile(s.tBid, 0.5), percentile(s.tBid, 0.99), s.tBid.empty() ? 0.0 : *std::max_element(s.tBid.begin(), s.tBid.end()),
                    percentile(s.tTrump, 0.5), percentile(s.tTrump, 0.99), s.tTrump.empty() ? 0.0 : *std::max_element(s.tTrump.begin(), s.tTrump.end()),
                    percentile(s.tPlay, 0.5), percentile(s.tPlay, 0.99), s.tPlay.empty() ? 0.0 : *std::max_element(s.tPlay.begin(), s.tPlay.end()));
        std::printf("         [wall max %.3f]\n", s.wallMax);
    }
    return rejected == 0 ? 0 : 1;
}
