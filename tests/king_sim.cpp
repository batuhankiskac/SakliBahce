// Headless bot-vs-bot simulation for King.
//
//   king_sim --games N --seed S --levels a,b,c,d [--duplicate] [--rotate] [--verbose] [--slow MS]
//            [--styles | --styles-fixed] [--king12]
//   king_sim --baselines N [--seed S]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt, one per seat. A game is a full match (20 hands; --king12: the short King,
// 12 hands, each player 1 koz and 2 cezas, whose sum is not zero). --rotate shifts the
// level assignment by one seat every match. --duplicate (implies rotation) plays every deal sequence four
// times, once per rotation, so each level holds every seat's cards; the level comparison is then also
// reported as a paired difference per deal (mean ± standard error), which removes most of the luck.
// Every bot action goes through applyBotAction; a rejected action is printed, counted and replaced by
// fallbackAction. Every match must sum to zero (default numbers) and finish all hands.
// --baselines N: mean points of the chooser (seat 0) for each contract over N random deals, all Usta
// (the numbers behind kKurtBaseline in KingBot.cpp).
// --styles gives slot k (the k-th entry of --levels, moving with the rotation) the personality of seat k
// (BotStyle::forSeat: 0, 1 neutral, 2 bold Kel Mahmut, 3 cautious Emekli Nuri) and prints per-slot contract and
// card-play stats and, with --duplicate, every slot's paired difference against slot 0 (slot 1 is a neutral
// control); --styles-fixed ties the presets to the physical seats (as in the game) for the level comparison.
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/King.cpp src/core/KingBot.cpp tests/king_sim.cpp
//        -o build/king/king_sim
#include "core/King.h"
#include "core/KingBot.h"

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

using namespace king;

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
    long long seatMatches = 0;
    double scoreSum = 0, scoreSq = 0;
    double matchWins = 0; // shared on ties
    std::vector<float> chooseTimes, playTimes;
    std::array<long long, NUM_CONTRACTS> chosen{};
    std::array<double, NUM_CONTRACTS> chosenPts{}; // chooser's points in the contracts it chose
};

// Per persona (slot with --styles, seat with --styles-fixed).
struct StyleStats {
    long long seatMatches = 0, choices = 0, koz = 0, kozOrderSum = 0, kozEarly = 0;
    double scoreSum = 0, scoreSq = 0;
    long long cezaHands = 0, cezaTricks = 0, cezaPts = 0, kozHands = 0, kozTricks = 0;
    long long chooserPts = 0;
};

struct Totals {
    long long matches = 0, hands = 0, rejected = 0, nonZeroSum = 0, unfinished = 0, earlyEnds = 0;
};

struct MatchResult {
    std::array<int, 4> total{};
    bool ok = true;
};

bool gKing12 = false; // --king12

Rules matchRules() {
    Rules r;
    if (gKing12) {
        r.kozPerPlayer = 1;
        r.cezaPerPlayer = 2;
    }
    return r;
}

MatchResult playMatch(uint64_t seed, const std::array<int, 4>& lv, std::array<LevelStats, 3>& st, Totals& tot,
                      bool verbose, double slowMs, const int* persona = nullptr, StyleStats* sty = nullptr) {
    Game g(matchRules());
    static const char* const names[4] = {"Bot0", "Bot1", "Bot2", "Bot3"};
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[s], false);
    std::array<Bot, 4> bots = {Bot((BotLevel)lv[0], seed * 4 + 1), Bot((BotLevel)lv[1], seed * 4 + 2),
                               Bot((BotLevel)lv[2], seed * 4 + 3), Bot((BotLevel)lv[3], seed * 4 + 4)};
    if (persona)
        for (int s = 0; s < 4; ++s) bots[s].setStyle(BotStyle::forSeat(persona[s]));
    g.startMatch(seed);
    int guard = 0;
    while (g.stage() != Stage::MatchOver && guard++ < 5000) {
        for (const GameEvent& e : g.drainEvents()) {
            if (e.type == EvType::HandEndEarly) ++tot.earlyEnds;
            if (verbose && (e.type == EvType::ContractChosen || e.type == EvType::HandEnd || e.type == EvType::MatchEnd)) {
                std::printf("  %s\n", e.text.c_str());
                for (const std::string& l : e.lines) std::printf("    %s\n", l.c_str());
            }
        }
        if (g.stage() == Stage::HandOver) {
            const HandRecord& r = g.sheet().back();
            LevelStats& cs = st[lv[r.chooser]];
            ++cs.chosen[(int)r.contract];
            cs.chosenPts[(int)r.contract] += r.points[r.chooser];
            g.startNextHand();
            continue;
        }
        const bool choosing = g.stage() == Stage::Choosing;
        const int seat = choosing ? g.chooser() : g.current();
        const double t0 = nowMs();
        BotAction a = bots[seat].next(g, seat);
        const double dt = nowMs() - t0;
        (choosing ? st[lv[seat]].chooseTimes : st[lv[seat]].playTimes).push_back((float)dt);
        if (slowMs > 0 && dt > slowMs)
            std::printf("  slow %s %.1f ms (%s, trick %d)\n", levelName(lv[seat]), dt, choosing ? "choose" : "play",
                        g.trickNumber());
        ActionResult r = applyBotAction(g, seat, a);
        if (!r.ok) {
            ++tot.rejected;
            std::printf("  REJECTED seat %d (%s) %s: %s\n", seat, levelName(lv[seat]),
                        a.kind == BotAction::Kind::Choose ? contractNameTR(a.contract) : kart::cardNameTR(a.card).c_str(),
                        r.error.c_str());
            a = fallbackAction(g, seat);
            r = applyBotAction(g, seat, a);
            if (!r.ok) {
                std::printf("  FALLBACK REJECTED: %s\n", r.error.c_str());
                break;
            }
        }
    }
    g.drainEvents();
    tot.hands += (long long)g.sheet().size();
    MatchResult res;
    if (g.stage() != Stage::MatchOver || (int)g.sheet().size() != g.numHands()) {
        ++tot.unfinished;
        res.ok = false;
    }
    int sum = 0;
    for (int s = 0; s < 4; ++s) {
        res.total[s] = g.total(s);
        sum += g.total(s);
    }
    if (sum != 0 && !gKing12) ++tot.nonZeroSum;
    ++tot.matches;
    // last hand's chooser stats
    if (!g.sheet().empty()) {
        const HandRecord& r = g.sheet().back();
        LevelStats& cs = st[lv[r.chooser]];
        ++cs.chosen[(int)r.contract];
        cs.chosenPts[(int)r.contract] += r.points[r.chooser];
    }
    const int best = *std::max_element(res.total.begin(), res.total.end());
    int nb = 0;
    for (int s = 0; s < 4; ++s) nb += res.total[s] == best;
    for (int s = 0; s < 4; ++s) {
        LevelStats& ls = st[lv[s]];
        ++ls.seatMatches;
        ls.scoreSum += res.total[s];
        ls.scoreSq += (double)res.total[s] * res.total[s];
        if (res.total[s] == best) ls.matchWins += 1.0 / nb;
    }
    if (persona && sty) {
        int nth[4] = {0, 0, 0, 0};
        for (const HandRecord& r : g.sheet()) {
            StyleStats& cs = sty[persona[r.chooser]];
            ++nth[r.chooser];
            ++cs.choices;
            cs.chooserPts += r.points[r.chooser];
            if (r.contract == Contract::Koz) {
                ++cs.koz;
                cs.kozOrderSum += nth[r.chooser];
                if (nth[r.chooser] <= 2) ++cs.kozEarly;
            }
            for (int s = 0; s < 4; ++s) {
                StyleStats& ss = sty[persona[s]];
                if (r.contract == Contract::Koz) {
                    ++ss.kozHands;
                    ss.kozTricks += r.tricks[s];
                } else {
                    ++ss.cezaHands;
                    ss.cezaTricks += r.tricks[s];
                    ss.cezaPts += r.points[s];
                }
            }
        }
        for (int s = 0; s < 4; ++s) {
            StyleStats& ss = sty[persona[s]];
            ++ss.seatMatches;
            ss.scoreSum += res.total[s];
            ss.scoreSq += (double)res.total[s] * res.total[s];
        }
    }
    if (verbose)
        std::printf("match %llu: %d %d %d %d\n", (unsigned long long)seed, res.total[0], res.total[1], res.total[2],
                    res.total[3]);
    return res;
}

int runBaselines(int n, uint64_t seed) {
    std::array<double, NUM_CONTRACTS> sum{}, sq{};
    std::array<int, NUM_CONTRACTS> cnt{};
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < NUM_CONTRACTS; ++c) {
            Game g;
            for (int s = 0; s < 4; ++s) g.setPlayer(s, "B", false);
            g.startMatch(seed + (uint64_t)i);
            g.debugSetChooser(0);
            int trump = -1;
            if (c == (int)Contract::Koz) { // longest suit, ties by honours
                double bv = -1;
                for (int T = 0; T < 4; ++T) {
                    double v = 0;
                    for (int x : g.hand(0))
                        if (kart::suitOf(x) == T) v += 1.0 + (kart::rankOf(x) >= 11 ? 0.3 * (kart::rankOf(x) - 10) : 0);
                    if (v > bv) {
                        bv = v;
                        trump = T;
                    }
                }
            }
            if (!g.chooseContract(0, (Contract)c, trump).ok) continue;
            std::array<Bot, 4> bots = {Bot(BotLevel::Usta, 1), Bot(BotLevel::Usta, 2), Bot(BotLevel::Usta, 3),
                                       Bot(BotLevel::Usta, 4)};
            while (g.stage() == Stage::Playing) {
                const int s = g.current();
                BotAction a = bots[s].next(g, s);
                if (!applyBotAction(g, s, a).ok) applyBotAction(g, s, fallbackAction(g, s));
            }
            g.drainEvents();
            const double v = g.sheet().back().points[0];
            sum[c] += v;
            sq[c] += v * v;
            ++cnt[c];
        }
    }
    std::printf("Chooser (seat 0) mean points over %d random deals, all Usta:\n", n);
    for (int c = 0; c < NUM_CONTRACTS; ++c) {
        const double m = sum[c] / std::max(1, cnt[c]);
        const double sd = std::sqrt(std::max(0.0, sq[c] / std::max(1, cnt[c]) - m * m));
        std::printf("  %-12s %8.1f  ± %.1f\n", contractNameTR((Contract)c), m, sd / std::sqrt(std::max(1, cnt[c])));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    int games = 100;
    uint64_t seed = 1;
    std::array<int, 4> levels{{2, 1, 0, 1}};
    bool duplicate = false, rotate = false, verbose = false;
    double slowMs = 0;
    int baselines = 0;
    int styleMode = 0; // 1 --styles (by slot), 2 --styles-fixed (by seat)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--games") games = std::atoi(val());
        else if (a == "--seed") seed = std::strtoull(val(), nullptr, 10);
        else if (a == "--levels") {
            const char* v = val();
            int k = 0;
            for (const char* p = v; *p && k < 4; ++p)
                if (*p >= '0' && *p <= '2') levels[k++] = *p - '0';
        } else if (a == "--duplicate") duplicate = true;
        else if (a == "--rotate") rotate = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--slow") slowMs = std::atof(val());
        else if (a == "--baselines") baselines = std::atoi(val());
        else if (a == "--styles") styleMode = 1;
        else if (a == "--styles-fixed") styleMode = 2;
        else if (a == "--king12") gKing12 = true;
        else {
            std::printf("unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (baselines > 0) return runBaselines(baselines, seed);

    std::array<LevelStats, 3> st;
    Totals tot;
    // Paired per-deal level means (duplicate mode).
    std::vector<std::array<double, 3>> dealMean;
    std::vector<std::array<int, 3>> dealCnt;
    StyleStats sty[4];
    std::vector<std::array<double, 4>> dealStyle; // per deal: mean total per persona
    const double t0 = nowMs();
    for (int gi = 0; gi < games; ++gi) {
        const uint64_t ms = seed * 1000003ull + (uint64_t)gi;
        const int rots = duplicate ? 4 : 1;
        std::array<double, 3> m{};
        std::array<int, 3> c{};
        std::array<double, 4> ps{};
        for (int r = 0; r < rots; ++r) {
            const int shift = duplicate ? r : (rotate ? gi % 4 : 0);
            std::array<int, 4> lv;
            int persona[4];
            for (int s = 0; s < 4; ++s) {
                lv[s] = levels[(s + shift) % 4];
                persona[s] = styleMode == 1 ? (s + shift) % 4 : s;
            }
            const MatchResult res =
                playMatch(ms, lv, st, tot, verbose, slowMs, styleMode ? persona : nullptr, styleMode ? sty : nullptr);
            for (int s = 0; s < 4; ++s) {
                m[lv[s]] += res.total[s];
                ++c[lv[s]];
                ps[persona[s]] += res.total[s] / (double)rots;
            }
        }
        dealStyle.push_back(ps);
        dealMean.push_back(m);
        dealCnt.push_back(c);
    }
    const double elapsed = nowMs() - t0;

    std::printf("\nKing sim: %lld matches (%lld hands), levels %s,%s,%s,%s%s, %.1f s\n", tot.matches, tot.hands,
                levelName(levels[0]), levelName(levels[1]), levelName(levels[2]), levelName(levels[3]),
                duplicate ? " [duplicate]" : (rotate ? " [rotate]" : ""), elapsed / 1000.0);
    std::printf("rejected actions: %lld   unfinished matches: %lld   non-zero-sum matches: %lld   early hand ends: %lld\n",
                tot.rejected, tot.unfinished, tot.nonZeroSum, tot.earlyEnds);
    std::printf("\n%-6s %8s %10s %9s %9s\n", "level", "seats", "mean", "±stderr", "win%");
    for (int l = 0; l < 3; ++l) {
        const LevelStats& s = st[l];
        if (!s.seatMatches) continue;
        const double n = (double)s.seatMatches;
        const double mean = s.scoreSum / n;
        const double sd = std::sqrt(std::max(0.0, s.scoreSq / n - mean * mean));
        std::printf("%-6s %8lld %10.1f %9.1f %8.1f%%\n", levelName(l), s.seatMatches, mean, sd / std::sqrt(n),
                    100.0 * s.matchWins / n);
    }
    if (duplicate && games > 1) {
        std::printf("\nPaired per-deal differences (mean ± stderr over %d deals):\n", games);
        for (int a = 2; a >= 0; --a)
            for (int b = a - 1; b >= 0; --b) {
                std::vector<double> d;
                for (size_t i = 0; i < dealMean.size(); ++i)
                    if (dealCnt[i][a] && dealCnt[i][b])
                        d.push_back(dealMean[i][a] / dealCnt[i][a] - dealMean[i][b] / dealCnt[i][b]);
                if (d.size() < 2) continue;
                double m = 0, q = 0;
                for (double x : d) m += x;
                m /= d.size();
                for (double x : d) q += (x - m) * (x - m);
                const double se = std::sqrt(q / (d.size() - 1) / d.size());
                std::printf("  %-5s - %-5s: %8.1f ± %6.1f   (z = %.1f)\n", levelName(a), levelName(b), m, se,
                            se > 0 ? m / se : 0.0);
            }
    }
    std::printf("\nDecision times (ms): level  choose p50/p99/max   play p50/p99/max\n");
    for (int l = 0; l < 3; ++l) {
        const LevelStats& s = st[l];
        if (!s.seatMatches) continue;
        auto mx = [](const std::vector<float>& v) { return v.empty() ? 0.0 : (double)*std::max_element(v.begin(), v.end()); };
        std::printf("  %-6s %7.3f %7.3f %7.3f   %7.3f %7.3f %7.3f\n", levelName(l), percentile(s.chooseTimes, 0.5),
                    percentile(s.chooseTimes, 0.99), mx(s.chooseTimes), percentile(s.playTimes, 0.5),
                    percentile(s.playTimes, 0.99), mx(s.playTimes));
    }
    std::printf("\nContracts chosen (count, chooser's mean points):\n");
    for (int l = 0; l < 3; ++l) {
        const LevelStats& s = st[l];
        if (!s.seatMatches) continue;
        std::printf("  %-6s", levelName(l));
        for (int c = 0; c < NUM_CONTRACTS; ++c)
            std::printf(" %s %lld/%.0f;", contractNameTR((Contract)c), s.chosen[c],
                        s.chosen[c] ? s.chosenPts[c] / s.chosen[c] : 0.0);
        std::printf("\n");
    }
    if (styleMode) {
        std::printf("\nStyles (%s): 0, 1 neutral (Hacı Rıza), 2 bold (Kel Mahmut), 3 cautious (Emekli Nuri)\n",
                    styleMode == 1 ? "by slot" : "by seat");
        std::printf("  %-8s %16s %7s %10s %10s %13s %13s %11s\n", "style", "match total", "koz%", "kozOrder",
                    "kozIn1-2", "cezaTrk/hand", "cezaPts/hand", "kozTrk/hand");
        for (int p = 0; p < 4; ++p) {
            const StyleStats& s = sty[p];
            if (!s.seatMatches) continue;
            const double n = (double)s.seatMatches, mean = s.scoreSum / n;
            const double se = std::sqrt(std::max(0.0, s.scoreSq / n - mean * mean) / n);
            std::printf("  %d (%+.0f) %9.1f ± %5.1f %6.1f%% %10.2f %9.1f%% %13.3f %13.1f %11.3f\n", p,
                        BotStyle::forSeat(p).boldness, mean, se, s.choices ? 100.0 * s.koz / s.choices : 0.0,
                        s.koz ? (double)s.kozOrderSum / s.koz : 0.0, s.koz ? 100.0 * s.kozEarly / s.koz : 0.0,
                        s.cezaHands ? (double)s.cezaTricks / s.cezaHands : 0.0,
                        s.cezaHands ? (double)s.cezaPts / s.cezaHands : 0.0,
                        s.kozHands ? (double)s.kozTricks / s.kozHands : 0.0);
        }
        if (styleMode == 1 && duplicate && dealStyle.size() > 1)
            for (int p = 1; p < 4; ++p) {
                double m = 0, q = 0;
                for (const auto& d : dealStyle) m += d[p] - d[0];
                m /= dealStyle.size();
                for (const auto& d : dealStyle) q += (d[p] - d[0] - m) * (d[p] - d[0] - m);
                const double se = std::sqrt(q / (dealStyle.size() - 1) / dealStyle.size());
                std::printf("  paired style %d - style 0: %8.1f ± %6.1f per match  (z = %.1f, %zu deals)\n", p, m, se,
                            se > 0 ? m / se : 0.0, dealStyle.size());
            }
    }
    return (tot.rejected || tot.unfinished || tot.nonZeroSum) ? 1 : 0;
}
