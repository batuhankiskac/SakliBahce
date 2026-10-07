// Headless bot-vs-bot simulation for Konken.
//
//   konken_sim --hands N --seed S --levels a,b,c,d [--duplicate] [--styles] [--open M] [--verbose]
//   konken_sim --matches N --seed S --levels a,b,c,d [--limit L]
//
// levels: 0 = Acemi, 1 = Usta, 2 = Kurt, one per seat. --hands plays N independent hands (a match whose limit is never
// reached) and reports each level's mean hand points (lower is better), how often it finished, opened and made konken.
// --duplicate plays every deal four times, the level assignment rotated by one seat each time, so each level holds
// every seat's cards; the level comparison is then also a paired difference per deal (mean ± standard error), which
// removes most of the luck. --matches plays full matches (to --limit) and counts wins per level.
// Every bot action goes through applyBotAction; a rejected action is printed, counted and replaced by fallbackAction.
// The slowest decision per level is reported (Kurt must stay well under 0.3 s).
//
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Konken.cpp src/core/KonkenBot.cpp tests/konken_sim.cpp
#include "core/Konken.h"
#include "core/KonkenBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace konken;

namespace {

const char* levelName(int l) { return l == 0 ? "Acemi" : l == 1 ? "Usta" : "Kurt"; }

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct LevelStats {
    int hands = 0;
    long points = 0;
    int finished = 0, konken = 0, opened = 0;
    int matches = 0, wins = 0;
    double slowest = 0.0;
    long decisions = 0;
    double ms = 0.0;
};

int rejected = 0;
long actions = 0;
long turnsSum = 0, handsCount = 0, stockOut = 0;

// Plays the current hand to its end; levels per seat.
void playHand(Game& g, std::array<std::unique_ptr<Bot>, 4>& bots, const std::array<int, 4>& lv,
              std::array<LevelStats, 3>& st, bool verbose) {
    int guard = 0;
    while (g.stage() == Stage::Draw || g.stage() == Stage::Play) {
        if (++guard > 20000) {
            std::printf("STUCK: hand %d\n", g.handIndex());
            ++rejected;
            return;
        }
        const int s = g.current();
        const double t0 = nowMs();
        BotAction a = bots[(size_t)s]->next(g, s);
        const double dt = nowMs() - t0;
        LevelStats& L = st[(size_t)lv[(size_t)s]];
        L.slowest = std::max(L.slowest, dt);
        L.ms += dt;
        ++L.decisions;
        const ActionResult r = applyBotAction(g, s, a);
        ++actions;
        if (!r.ok) {
            ++rejected;
            std::printf("REJECTED %s seat %d: %s (%s)\n", levelName(lv[(size_t)s]), s, describeAction(g, s, a).c_str(), r.error.c_str());
            const ActionResult f = applyBotAction(g, s, fallbackAction(g, s));
            if (!f.ok) {
                std::printf("FALLBACK REJECTED: %s\n", f.error.c_str());
                return;
            }
        }
        if (verbose)
            for (const GameEvent& e : g.drainEvents())
                if (!e.text.empty() && e.type != EvType::TurnStart) std::printf("  %s\n", e.text.c_str());
    }
    g.drainEvents();
    const HandRecord& r = g.sheet().back();
    turnsSum += r.turns;
    ++handsCount;
    stockOut += r.finisher < 0 ? 1 : 0;
    for (int s = 0; s < 4; ++s) {
        LevelStats& L = st[(size_t)lv[(size_t)s]];
        ++L.hands;
        L.points += r.points[(size_t)s];
        L.finished += r.finisher == s ? 1 : 0;
        L.konken += r.finisher == s && r.konken ? 1 : 0;
        L.opened += r.opened[(size_t)s] ? 1 : 0;
    }
    if (verbose) std::printf("hand %d: %d %d %d %d\n", r.index + 1, r.points[0], r.points[1], r.points[2], r.points[3]);
}

} // namespace

int main(int argc, char** argv) {
    int hands = 0, matches = 0, openMin = 51, limit = 151;
    uint64_t seed = 1;
    std::array<int, 4> levels{{2, 1, 0, 1}};
    bool duplicate = false, verbose = false, styles = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--hands") hands = std::atoi(val());
        else if (a == "--matches") matches = std::atoi(val());
        else if (a == "--seed") seed = (uint64_t)std::atoll(val());
        else if (a == "--open") openMin = std::atoi(val());
        else if (a == "--limit") limit = std::atoi(val());
        else if (a == "--duplicate") duplicate = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--styles") styles = true;
        else if (a == "--levels") {
            const char* s = val();
            for (int k = 0; k < 4 && *s; ++k) {
                levels[(size_t)k] = std::clamp(std::atoi(s), 0, 2);
                while (*s && *s != ',') ++s;
                if (*s == ',') ++s;
            }
        }
    }
    if (hands <= 0 && matches <= 0) hands = 100;
    std::array<LevelStats, 3> st{};
    // paired differences per deal: sum of points per level over the four rotations
    std::vector<std::array<double, 3>> perDeal;
    std::vector<std::array<int, 3>> perDealN;
    const int rotations = duplicate ? 4 : 1;
    const int units = hands > 0 ? hands : matches;
    for (int u = 0; u < units; ++u) {
        std::array<double, 3> sum{};
        std::array<int, 3> n{};
        for (int rot = 0; rot < rotations; ++rot) {
            std::array<int, 4> lv{};
            for (int s = 0; s < 4; ++s) lv[(size_t)s] = levels[(size_t)((s + rot + (duplicate ? 0 : u)) % 4)];
            Rules R;
            R.openMin = openMin;
            R.limit = hands > 0 ? 1000000 : limit;
            R.maxHands = hands > 0 ? 1 : 40;
            Game g(R);
            std::array<std::unique_ptr<Bot>, 4> bots;
            for (int s = 0; s < 4; ++s) {
                bots[(size_t)s] = std::make_unique<Bot>((BotLevel)lv[(size_t)s], seed * 131 + (uint64_t)u * 7 + (uint64_t)s);
                if (styles) bots[(size_t)s]->setStyle(BotStyle::forSeat(s));
                g.setPlayer(s, std::string("P") + std::to_string(s), false);
            }
            g.startMatch(seed * 1000003ull + (uint64_t)u);
            const size_t before = g.sheet().size();
            while (true) {
                playHand(g, bots, lv, st, verbose);
                if (g.stage() != Stage::HandOver) break;
                g.startNextHand();
            }
            for (size_t h = before; h < g.sheet().size(); ++h)
                for (int s = 0; s < 4; ++s) {
                    sum[(size_t)lv[(size_t)s]] += g.sheet()[h].points[(size_t)s];
                    n[(size_t)lv[(size_t)s]] += 1;
                }
            if (matches > 0) {
                const std::array<int, 4> rk = g.ranking();
                for (int s = 0; s < 4; ++s) ++st[(size_t)lv[(size_t)s]].matches;
                // ties for the lowest total share the win
                for (int s = 0; s < 4; ++s)
                    if (g.total(s) == g.total(rk[0])) ++st[(size_t)lv[(size_t)s]].wins;
            }
        }
        perDeal.push_back(sum);
        perDealN.push_back(n);
    }
    std::printf("konken_sim: %d %s%s, levels %d,%d,%d,%d, açma %d\n", units, hands > 0 ? "hands" : "matches",
                duplicate ? " x4 (duplicate)" : "", levels[0], levels[1], levels[2], levels[3], openMin);
    for (int l = 0; l < 3; ++l) {
        const LevelStats& L = st[(size_t)l];
        if (L.hands == 0) continue;
        std::printf("  %-5s hands %5d  points/hand %6.2f  finished %5.1f%%  konken %4.1f%%  opened %5.1f%%", levelName(l), L.hands,
                    (double)L.points / L.hands, 100.0 * L.finished / L.hands, 100.0 * L.konken / L.hands, 100.0 * L.opened / L.hands);
        if (L.matches) std::printf("  wins %5.1f%%", 100.0 * L.wins / L.matches);
        std::printf("  decision avg %.2f ms, max %.1f ms\n", L.decisions ? L.ms / (double)L.decisions : 0.0, L.slowest);
    }
    if (duplicate) {
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b) {
                std::vector<double> d;
                for (size_t i = 0; i < perDeal.size(); ++i)
                    if (perDealN[i][(size_t)a] && perDealN[i][(size_t)b])
                        d.push_back(perDeal[i][(size_t)a] / perDealN[i][(size_t)a] - perDeal[i][(size_t)b] / perDealN[i][(size_t)b]);
                if (d.size() < 2) continue;
                double m = 0.0, s2 = 0.0;
                for (double x : d) m += x;
                m /= (double)d.size();
                for (double x : d) s2 += (x - m) * (x - m);
                const double se = std::sqrt(s2 / (double)(d.size() - 1) / (double)d.size());
                std::printf("  %s - %s: %+.2f ± %.2f points/hand (paired, %zu deals)\n", levelName(a), levelName(b), m, se, d.size());
            }
    }
    std::printf("  turns/hand %.1f, stock ran out %.1f%%\n", handsCount ? (double)turnsSum / handsCount : 0.0, handsCount ? 100.0 * stockOut / handsCount : 0.0);
    std::printf("  actions %ld, rejected %d\n", actions, rejected);
    return rejected == 0 ? 0 : 1;
}
