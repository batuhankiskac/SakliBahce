// Headless Bezik bot-vs-bot simulation: the level ladder.
//
//   bezik_sim --hands N --seed S --levels a,b [--style bold|careful] [--threads T] [--verbose]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt. Deals are played in duplicate pairs: every deal (seed) twice with the
// levels swapped between the seats (the cards depend only on the seed: the deck is shuffled once per deal), so the
// paired difference removes most of the luck of the cards. Reported: A's points per deal, B's, the paired net
// difference with its standard error, the share of deals A scored more, combinations per deal and decision times.
// Every bot action goes through applyBotAction; a rejected one is printed, counted and replaced by fallbackAction
// (the count must be 0). --style gives A a personality (Kel Mahmut's bold, Emekli Nuri's careful).
#include "core/Bezik.h"
#include "core/BezikBot.h"

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

using namespace bezik;

namespace {

const char* levelName(int l) { return l == 0 ? "Acemi" : l == 1 ? "Usta" : "Kurt"; }

struct DealResult {
    int pts[2] = {0, 0};      // by level slot: [0] = A, [1] = B
    int melds[2] = {0, 0};
    int rejected = 0;
    std::vector<double> ms[3]; // decision times per level
};

DealResult playDeal(uint64_t seed, int levelSeat0, int levelSeat1, bool aIsSeat0, okey::BotStyle styleA, bool verbose) {
    DealResult r;
    Rules rules;
    rules.target = 100000; // one deal
    Game g(rules);
    g.setPlayer(0, "A", false);
    g.setPlayer(1, "B", false);
    const int lv[2] = {levelSeat0, levelSeat1};
    Bot bots[2] = {Bot((BotLevel)lv[0], seed * 2 + 11), Bot((BotLevel)lv[1], seed * 2 + 12)};
    bots[aIsSeat0 ? 0 : 1].setStyle(styleA);
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::HandOver && g.stage() != Stage::MatchOver && ++guard < 2000) {
        for (const GameEvent& e : g.drainEvents())
            if (verbose && !e.text.empty()) std::printf("  %s\n", e.text.c_str());
        const int s = g.current();
        const auto t0 = std::chrono::steady_clock::now();
        const BotAction a = bots[s].next(g, s);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (g.stage() == Stage::Playing) r.ms[lv[s]].push_back(ms);
        const ActionResult res = applyBotAction(g, s, a);
        if (!res.ok) {
            ++r.rejected;
            std::printf("REJECTED seed %llu seat %d kind %d card %d: %s\n", (unsigned long long)seed, s, (int)a.kind, a.card,
                        res.error.c_str());
            applyBotAction(g, s, fallbackAction(g, s));
        }
    }
    const int sa = aIsSeat0 ? 0 : 1;
    r.pts[0] = g.handPoints(sa);
    r.pts[1] = g.handPoints(1 - sa);
    r.melds[0] = g.meldPoints(sa);
    r.melds[1] = g.meldPoints(1 - sa);
    return r;
}

double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[(size_t)std::min((double)v.size() - 1, q * (double)(v.size() - 1))];
}

} // namespace

int main(int argc, char** argv) {
    long long hands = 100;
    uint64_t seed = 1;
    int la = 2, lb = 1, threads = 4;
    bool verbose = false;
    okey::BotStyle styleA;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hands") && i + 1 < argc) hands = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--levels") && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &la, &lb) != 2) {
                std::printf("--levels a,b\n");
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--style") && i + 1 < argc) {
            const std::string s = argv[++i];
            styleA = s == "bold" ? okey::BotStyle::bold() : s == "careful" ? okey::BotStyle::cautious() : okey::BotStyle{};
        } else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else {
            std::printf("usage: bezik_sim --hands N --seed S --levels a,b [--style bold|careful] [--threads T] [--verbose]\n");
            return 2;
        }
    }
    la = std::clamp(la, 0, 2);
    lb = std::clamp(lb, 0, 2);
    const long long pairs = std::max(1LL, hands / 2);
    std::vector<double> diff((size_t)pairs, 0.0);
    std::vector<DealResult> res((size_t)pairs * 2);
    std::atomic<long long> nextJob{0};
    const auto t0 = std::chrono::steady_clock::now();
    auto worker = [&]() {
        for (long long j; (j = nextJob++) < pairs * 2;) {
            const long long p = j / 2;
            const bool aSeat0 = (j % 2) == 0;
            const uint64_t s = seed * 1000003ull + (uint64_t)p;
            res[(size_t)j] = aSeat0 ? playDeal(s, la, lb, true, styleA, verbose) : playDeal(s, lb, la, false, styleA, verbose);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < (verbose ? 1 : threads); ++t) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    double sa = 0, sb = 0, ma = 0, mb = 0, wins = 0;
    long long rejected = 0;
    std::vector<double> ms[3];
    for (long long p = 0; p < pairs; ++p) {
        double d = 0;
        for (int k = 0; k < 2; ++k) {
            const DealResult& r = res[(size_t)(p * 2 + k)];
            sa += r.pts[0];
            sb += r.pts[1];
            ma += r.melds[0];
            mb += r.melds[1];
            wins += r.pts[0] > r.pts[1] ? 1.0 : r.pts[0] == r.pts[1] ? 0.5 : 0.0;
            d += r.pts[0] - r.pts[1];
            rejected += r.rejected;
            for (int l = 0; l < 3; ++l) ms[l].insert(ms[l].end(), r.ms[l].begin(), r.ms[l].end());
        }
        diff[(size_t)p] = d / 2.0;
    }
    const double n = (double)pairs * 2.0;
    double mean = 0;
    for (double d : diff) mean += d;
    mean /= (double)pairs;
    double var = 0;
    for (double d : diff) var += (d - mean) * (d - mean);
    const double se = pairs > 1 ? std::sqrt(var / (double)(pairs - 1) / (double)pairs) : 0.0;
    std::printf("bezik_sim: %s (A%s) vs %s (B), %lld deals (%lld duplicate pairs), seed %llu, %.1f s\n", levelName(la),
                styleA.neutral() ? "" : styleA.boldness > 0 ? ", bold" : ", careful", levelName(lb), (long long)n,
                pairs, (unsigned long long)seed, secs);
    std::printf("  points / deal        : A %.1f  B %.1f\n", sa / n, sb / n);
    std::printf("  combinations / deal  : A %.1f  B %.1f\n", ma / n, mb / n);
    std::printf("  A net points / deal  : %+.1f +- %.1f (paired, z = %.1f)\n", mean, se, se > 0 ? mean / se : 0.0);
    std::printf("  A deals won          : %.1f%%\n", 100.0 * wins / n);
    for (int l = 0; l < 3; ++l)
        if (!ms[l].empty())
            std::printf("  %-5s decision ms    : p50 %.2f  p99 %.2f  max %.2f  (%zu)\n", levelName(l), pct(ms[l], 0.5),
                        pct(ms[l], 0.99), pct(ms[l], 1.0), ms[l].size());
    std::printf("  rejected actions     : %lld\n", rejected);
    return rejected ? 1 : 0;
}
