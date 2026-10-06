// Hata analizi tests (src/app/Analysis): constructed positions where a clearly bad move is reported with a sensible
// cost and a good one is not; for every game a whole simulated match (bots on all seats, seat 0 a Kurt forced to make a
// few silly moves) whose analysis names those moves among the top mistakes; determinism; timing per game.
#include "app/Analysis.h"
#include "core/BatakBot.h"
#include "core/Bot.h"
#include "core/KingBot.h"
#include "core/PistiBot.h"
#include "core/TavlaBot.h"
#include "ui/Screens.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <thread>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) {                                                                                           \
            ++g_failures;                                                                                        \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                                        \
    } while (0)

using Key = std::pair<int, int>; // (hand, turn) as Mistake reports them
int seat0Level = 2;              // the bot in the player's seat (Kurt; 0 Acemi / 1 Usta for example runs)

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

struct SimMatch {
    std::vector<std::string> lines;
    std::vector<Key> silly; // the forced mistakes
};

// ---------------------------------------------------------------------------------------------------------
// 101 / eşli 101 / klasik okey

SimMatch simOkey(ui::GameKind kind, const ui::Settings& st, uint64_t seed, const std::vector<int>& sillyTurns) {
    okey::RulesConfig cfg;
    cfg.variant = kind == ui::GameKind::Okey ? okey::Variant::Okey : okey::Variant::Yuzbir;
    cfg.teams = kind == ui::GameKind::YuzbirEsli;
    cfg.okeyStartPoints = st.okeyStart;
    cfg.numHands = st.numHands;
    cfg.leftOpenPenalty = st.yandanCeza;
    okey::Game g(cfg);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    std::vector<okey::Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(s == 0 ? (okey::BotLevel)seat0Level : okey::BotLevel::Normal, seed * 7 + (uint64_t)s);
    okey::Bot judge(okey::BotLevel::Hard, 99);
    g.startMatch(seed);
    SimMatch out;
    int handBase = g.turnNumber(), lastHand = 0, myDiscards = 0, sillyDone = -1;
    for (int steps = 0; steps < 200000 && g.handState() != okey::HandState::MatchOver; ++steps) {
        if (g.handState() == okey::HandState::HandOver) {
            g.startNextHand();
            continue;
        }
        if (g.handIndex() != lastHand) {
            lastHand = g.handIndex();
            handBase = g.turnNumber();
            myDiscards = 0;
            for (auto& b : bots) b.resetForHand();
        }
        const int s = g.current();
        okey::BotAction a = bots[(size_t)s].next(g, s);
        if (s == 0 && (a.kind == okey::BotAction::Kind::Discard || a.kind == okey::BotAction::Kind::Finish)) {
            ++myDiscards;
            // one forced mistake per hand, from the given discard on, where it clearly costs something
            const bool target = !sillyTurns.empty() && myDiscards >= sillyTurns[0] && sillyDone != g.handIndex();
            const std::vector<int>& hand = g.player(0).hand;
            if (target && hand.size() > 3 && g.pendingLeftTile() < 0) {
                int bad = -1;
                if (g.classic()) {
                    // not finishing, or the discard with the lowest finishing chance when it is clearly worse
                    double worst = 2.0, best = -1.0;
                    for (const auto& v : judge.evaluateClassicDiscards(g, 0)) {
                        if (v.value < worst) {
                            worst = v.value;
                            bad = v.tile;
                        }
                        best = std::max(best, v.value);
                    }
                    if (best - worst < 0.2) bad = -1;
                    if (a.kind == okey::BotAction::Kind::Finish) {
                        for (int t : hand)
                            if (t != a.tile && !g.okey().isJoker(t)) bad = t;
                    }
                } else {
                    for (int t : hand)
                        if (g.okey().isJoker(t)) bad = t; // throw the okey away
                    if (bad < 0)
                        for (int t : hand)
                            if (g.isPlayableOnTable(t)) bad = t; // an işlek tile
                    if (bad < 0) {
                        double worst = -1e18, best = 1e18;
                        for (const auto& v : judge.evaluateDiscards(g, 0)) {
                            if (v.value > worst) {
                                worst = v.value;
                                bad = v.tile;
                            }
                            best = std::min(best, v.value);
                        }
                        if (worst - best < 25.0) bad = -1;
                    }
                }
                if (bad >= 0 && bad != a.tile) {
                    if (std::getenv("ANALYSIS_VERBOSE")) {
                        analysis::Mistake jm;
                        const bool ok = g.classic() ? analysis::judgeClassicDiscard(g, 0, bad, jm)
                                                    : analysis::judgeOkeyDiscard(g, 0, bad, jm);
                        std::printf("   forced (%d,%d): judged %d: %s [%.2f]\n", g.handIndex() + 1,
                                    (g.turnNumber() - handBase) / 4 + 1, ok, analysis::describeMistake(jm).c_str(), jm.cost);
                    }
                    a.kind = okey::BotAction::Kind::Discard;
                    a.tile = bad;
                    sillyDone = g.handIndex();
                    out.silly.push_back({g.handIndex() + 1, (g.turnNumber() - handBase) / 4 + 1});
                }
            }
        }
        if (!okey::applyBotAction(g, s, a).ok) okey::applyBotAction(g, s, okey::fallbackAction(g, s));
    }
    for (const okey::LoggedAction& la : g.actionLog()) out.lines.push_back(la.encode());
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// tavla

SimMatch simTavla(const ui::Settings& st, uint64_t seed, const std::vector<int>& sillyMoves) {
    tavla::Rules r;
    r.matchPoints = st.tavlaPoints;
    tavla::Game g(r);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Kel Mahmut", false);
    tavla::Bot b0((tavla::BotLevel)seat0Level, seed * 3 + 1), b1(tavla::BotLevel::Normal, seed * 3 + 2);
    g.startMatch(seed);
    SimMatch out;
    int lastGame = -1, moveNo = 0, totalMoves = 0;
    std::vector<tavla::Step> forced;
    for (int steps = 0; steps < 100000 && g.stage() != tavla::Stage::MatchOver; ++steps) {
        if (g.stage() == tavla::Stage::GameOver) {
            g.startNextGame();
            continue;
        }
        if (g.gameIndex() != lastGame) {
            lastGame = g.gameIndex();
            moveNo = 0;
            b0.resetForGame();
            b1.resetForGame();
        }
        const int p = g.stage() == tavla::Stage::OpeningRoll ? 0 : g.responder() >= 0 ? g.responder() : g.current();
        if (p == 0 && g.stage() == tavla::Stage::Moving && g.turnSteps().empty()) {
            ++moveNo;
            ++totalMoves;
            forced.clear();
            if (std::find(sillyMoves.begin(), sillyMoves.end(), totalMoves) != sillyMoves.end()) {
                // the worst play by the 1-ply evaluation
                const std::vector<tavla::Play> plays = g.allTurnPlays();
                if (plays.size() > 2) {
                    size_t worst = 0;
                    double wv = 1e18;
                    for (size_t i = 0; i < plays.size(); ++i) {
                        const double v = tavla::botEquityAfterMove(plays[i].result, 0, 1);
                        if (v < wv) {
                            wv = v;
                            worst = i;
                        }
                    }
                    forced = plays[worst].steps;
                    out.silly.push_back({g.gameIndex() + 1, moveNo});
                }
            }
        }
        if (p == 0 && g.stage() == tavla::Stage::Moving && !forced.empty()) {
            const tavla::Step s = forced.front();
            forced.erase(forced.begin());
            if (g.applyStep(0, s.from, s.to, s.die).ok) continue;
            forced.clear();
        }
        tavla::Bot& b = p == 0 ? b0 : b1;
        tavla::BotAction a = b.next(g, p);
        if (a.kind == tavla::BotAction::Kind::None || !tavla::applyBotAction(g, p, a).ok)
            tavla::applyBotAction(g, p, tavla::fallbackAction(g, p));
    }
    for (const tavla::LoggedAction& la : g.actionLog()) out.lines.push_back(la.encode());
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// pişti: seat 0 sometimes lets a capture go (plays another card when Kurt would take the table)

SimMatch simPisti(const ui::Settings& st, uint64_t seed, int sillyMax) {
    pisti::Rules r;
    r.mode = (pisti::Mode)st.pistiMode;
    r.targetScore = st.pistiTarget;
    pisti::Game g(r);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    std::vector<pisti::Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(s == 0 ? (pisti::BotLevel)seat0Level : pisti::BotLevel::Usta, seed * 5 + (uint64_t)s);
    g.startMatch(seed);
    SimMatch out;
    int lastHand = -1, cardNo = 0;
    for (int steps = 0; steps < 100000 && g.stage() != pisti::Stage::MatchOver; ++steps) {
        for (const pisti::GameEvent& e : g.drainEvents())
            for (auto& b : bots) b.observe(e, g);
        if (g.stage() == pisti::Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        if (g.handIndex() != lastHand) {
            lastHand = g.handIndex();
            cardNo = 0;
        }
        const int s = g.current();
        int card = bots[(size_t)s].next(g, s);
        if (s == 0) {
            ++cardNo;
            // the table is worth taking and Kurt takes it: play a card that does not
            int pts = 0;
            for (int c : g.tableCards()) pts += pisti::cardPoints(c);
            if ((int)out.silly.size() < sillyMax && g.wouldCapture(card) && (pts >= 2 || g.wouldPisti(card) > 0)) {
                for (int c : g.hand(0))
                    if (!g.wouldCapture(c)) {
                        card = c;
                        out.silly.push_back({g.handIndex() + 1, cardNo});
                        break;
                    }
            }
        }
        if (!g.playCard(s, card).ok) g.playCard(s, pisti::fallbackCard(g, s));
    }
    for (const pisti::LoggedAction& la : g.actionLog()) out.lines.push_back(la.encode());
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// batak: seat 0, last to play, gives away a trick Kurt would take for its side

SimMatch simBatak(const ui::Settings& st, uint64_t seed, int sillyMax) {
    batak::Rules r = st.batakEsli ? batak::Rules::esliBatak() : batak::Rules::tekli();
    r.targetScore = st.batakTarget;
    batak::Game g(r);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    std::vector<batak::Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(s == 0 ? (batak::Level)seat0Level : batak::Level::Usta, seed * 11 + (uint64_t)s);
    g.startMatch(seed);
    SimMatch out;
    for (int steps = 0; steps < 100000 && g.stage() != batak::Stage::MatchOver; ++steps) {
        if (g.stage() == batak::Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int cur = g.current();
        const int ctrl = g.controllerOf(cur);
        batak::Action a = bots[(size_t)ctrl].next(g, cur);
        if (ctrl == 0 && cur == 0 && g.stage() == batak::Stage::Bidding && a.kind == batak::Action::Kind::Pass &&
            (int)out.silly.size() < sillyMax && !g.legalBids(0).empty() && g.highBidder() < 0) {
            // a weak hand bids the ihale anyway
            const uint64_t h = g.handMask(0);
            const int need = g.legalBids(0).front();
            if (batak::estimateTricks(h, batak::bestTrumpFor(h)) + (g.rules().esli ? 3.0 : 0.0) < need - 2.5) {
                a.kind = batak::Action::Kind::Bid;
                a.value = need;
                out.silly.push_back({g.handIndex() + 1, -1});
            }
        }
        if (ctrl == 0 && cur == 0 && g.stage() == batak::Stage::ChoosingTrump && (int)out.silly.size() < sillyMax) {
            // the koz in the weakest suit
            const uint64_t h = g.handMask(0);
            int worst = 0;
            for (int su = 1; su < 4; ++su)
                if (batak::estimateTricks(h, su) < batak::estimateTricks(h, worst)) worst = su;
            if (batak::estimateTricks(h, a.suit) - batak::estimateTricks(h, worst) >= 2.0) {
                a.suit = worst;
                out.silly.push_back({g.handIndex() + 1, 0});
            }
        }
        if (ctrl == 0 && cur == 0 && g.stage() == batak::Stage::Playing && a.kind == batak::Action::Kind::Play &&
            !g.trick().empty() && (int)out.silly.size() < sillyMax) {
            const batak::TrickView v = batak::viewTrick(g.trick(), g.trump());
            if (batak::beatsTrick(a.card, v, g.trump()) && g.trick().size() == 3) {
                for (int c : g.legalCards(0))
                    if (!batak::beatsTrick(c, v, g.trump())) {
                        a.card = c;
                        out.silly.push_back({g.handIndex() + 1, g.trickNumber()});
                        break;
                    }
            }
        }
        if (!batak::applyAction(g, cur, a).ok) batak::applyAction(g, cur, batak::fallbackAction(g, cur));
    }
    for (const batak::LoggedAction& la : g.actionLog()) out.lines.push_back(la.encode());
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// king: in a penalty game seat 0, last to play, takes a trick it could duck

SimMatch simKing(const ui::Settings& st, uint64_t seed, int sillyMax) {
    king::Rules rules;
    if (st.king12) {
        rules.kozPerPlayer = 1;
        rules.cezaPerPlayer = 2;
    }
    king::Game g(rules);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    std::vector<king::Bot> bots;
    for (int s = 0; s < 4; ++s) bots.emplace_back(s == 0 ? (king::BotLevel)seat0Level : king::BotLevel::Usta, seed * 13 + (uint64_t)s);
    g.startMatch(seed);
    SimMatch out;
    for (int steps = 0; steps < 100000 && g.stage() != king::Stage::MatchOver; ++steps) {
        if (g.stage() == king::Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        const int s = g.stage() == king::Stage::Choosing ? g.chooser() : g.current();
        king::BotAction a = bots[(size_t)s].next(g, s);
        if (s == 0 && g.stage() == king::Stage::Playing && a.kind == king::BotAction::Kind::Play &&
            king::isCeza(g.contract()) && g.currentTrick().cards.size() == 3 && (int)out.silly.size() < sillyMax) {
            // which legal cards would take the trick?
            const king::Trick& t = g.currentTrick();
            auto takes = [&](int c) {
                int cards[4];
                for (int i = 0; i < 3; ++i) cards[i] = t.cards[(size_t)i].card;
                cards[3] = c;
                return king::trickWinnerIndex(cards, 4, g.contract(), g.trump()) == 3;
            };
            int cost = 0;
            for (const king::TrickCard& tc : t.cards) cost += king::cardPoints(g.rules(), g.contract(), tc.card);
            int pick = -1, pickPts = 1;
            for (int c : g.legalCards(0)) // the card I would throw on it counts too
                if (takes(c) && king::cardPoints(g.rules(), g.contract(), c) < pickPts) {
                    pickPts = king::cardPoints(g.rules(), g.contract(), c);
                    pick = c;
                }
            const bool trickCeza = g.contract() == king::Contract::ElAlmaz ||
                                   (g.contract() == king::Contract::SonIki && g.trickNumber() >= 11);
            if (!takes(a.card) && pick >= 0 && (trickCeza || cost + pickPts <= -50)) {
                for (int c : {pick})
                    if (takes(c)) {
                        if (std::getenv("ANALYSIS_VERBOSE")) {
                            analysis::Mistake jm;
                            const bool ok = analysis::judgeKingCard(g, 0, c, jm);
                            std::printf("   forced (%d,%d): judged %d: %s [%.2f]\n", g.handIndex() + 1, g.trickNumber() + 1,
                                        ok, analysis::describeMistake(jm).c_str(), jm.cost);
                        }
                        a.card = c;
                        out.silly.push_back({g.handIndex() + 1, g.trickNumber() + 1});
                        break;
                    }
            }
        }
        if (!king::applyBotAction(g, s, a).ok) king::applyBotAction(g, s, king::fallbackAction(g, s));
    }
    for (const king::LoggedAction& la : g.actionLog()) out.lines.push_back(la.encode());
    return out;
}

// ---------------------------------------------------------------------------------------------------------

void printMistakes(const char* title, const std::vector<analysis::Mistake>& ms, double secs, size_t lines) {
    std::printf("[%s] %zu actions, %.2f s\n", title, lines, secs);
    for (const analysis::Mistake& m : ms)
        std::printf("   %s   [cost %.3f %s, noise %.3f]\n", analysis::describeMistake(m).c_str(), m.cost, m.unit.c_str(), m.noise);
}

// The analysis of a whole match: it replays, finds the forced mistakes among its top three, is deterministic and fast.
void checkMatch(const char* title, ui::GameKind kind, const ui::Settings& st, uint64_t seed, const SimMatch& sm,
                int minFound, double maxSeconds) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<analysis::Mistake> ms = analysis::analyzeMatch(kind, st, seed, sm.lines);
    const double secs = seconds(t0);
    printMistakes(title, ms, secs, sm.lines.size());
    std::printf("   forced:");
    for (const Key& k : sm.silly) std::printf(" (%d,%d)", k.first, k.second);
    std::printf("\n");
    CHECK(ms.size() <= 3);
    int found = 0;
    for (const analysis::Mistake& m : ms) {
        CHECK(m.notable);
        CHECK(m.cost > 0.0);
        CHECK(!m.when.empty() && !m.played.empty() && !m.better.empty());
        if (std::find(sm.silly.begin(), sm.silly.end(), Key{m.hand, m.turn}) != sm.silly.end()) ++found;
    }
    for (size_t i = 1; i < ms.size(); ++i) CHECK(ms[i - 1].cost >= ms[i].cost);
    if (std::getenv("ANALYSIS_VERBOSE")) {
        std::printf("   all notable:\n");
        for (const analysis::Mistake& m : analysis::analyzeMatch(kind, st, seed, sm.lines, analysis::defaultNames(), 50))
            std::printf("     (%d,%d) %s\n", m.hand, m.turn, analysis::describeMistake(m).c_str());
    }
    CHECK((int)sm.silly.size() >= minFound);
    CHECK(found >= std::min<int>(minFound, (int)sm.silly.size()));
    if (found < std::min<int>(minFound, (int)sm.silly.size())) std::printf("   !! only %d forced mistakes found\n", found);
    CHECK(secs < maxSeconds);
    // determinism
    const std::vector<analysis::Mistake> again = analysis::analyzeMatch(kind, st, seed, sm.lines);
    CHECK(again.size() == ms.size());
    for (size_t i = 0; i < std::min(again.size(), ms.size()); ++i) {
        CHECK(analysis::describeMistake(again[i]) == analysis::describeMistake(ms[i]));
        CHECK(again[i].cost == ms[i].cost);
    }
}

// ---------------------------------------------------------------------------------------------------------
// constructed positions

void testOkeyConstructed() {
    using namespace okey;
    Game g;
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g.startMatch(5);
    g.debugSetOkey(makeTileId(Yellow, 1, 0)); // okey = Sarı 2
    const OkeyInfo& ok = g.okey();
    // on the table: Hacı Rıza's Kırmızı 3-4-5
    Meld run;
    CHECK(makeMeld({makeTileId(Red, 3, 0), makeTileId(Red, 4, 0), makeTileId(Red, 5, 0)}, ok, run, false));
    run.owner = 1;
    g.debugTable() = {run};
    g.debugPlayer(1).opened = true;
    g.debugPlayer(0).opened = true;
    g.debugPlayer(0).openedTurn = 0;
    const int red6 = makeTileId(Red, 6, 0), black13 = makeTileId(Black, 13, 0);
    g.debugPlayer(0).hand = {red6, makeTileId(Blue, 9, 0), black13, makeTileId(Yellow, 7, 0), makeTileId(Blue, 1, 0)};
    g.debugSetTurn(0, TurnStage::Play);
    analysis::Mistake bad, good;
    CHECK(analysis::judgeOkeyDiscard(g, 0, red6, bad));
    CHECK(analysis::judgeOkeyDiscard(g, 0, black13, good));
    std::printf("[101 constructed] işlek Kırmızı 6: %s\n", analysis::describeMistake(bad).c_str());
    std::printf("[101 constructed] Siyah 13: cost %.1f\n", good.cost);
    CHECK(bad.notable);
    CHECK(bad.cost >= 80.0 && bad.cost <= 160.0);
    CHECK(bad.played == "Kırmızı 6'yı attın");
    CHECK(!good.notable);
    CHECK(good.cost < 12.0);
}

void testClassicConstructed() {
    using namespace okey;
    RulesConfig cfg;
    cfg.variant = Variant::Okey;
    Game g(cfg);
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g.startMatch(8);
    g.debugSetOkey(makeTileId(Yellow, 12, 0)); // okey = Sarı 13
    // 14 tiles in sets + a stray Siyah 9: the hand is finished by putting the Siyah 9 down
    std::vector<int> h;
    for (int n = 1; n <= 4; ++n) h.push_back(makeTileId(Red, n, 0));
    for (int n = 5; n <= 7; ++n) h.push_back(makeTileId(Blue, n, 0));
    for (int c = 0; c < 4; ++c) h.push_back(makeTileId(c, 10, 1));
    for (int n = 9; n <= 11; ++n) h.push_back(makeTileId(Yellow, n, 0));
    h.push_back(makeTileId(Black, 9, 0));
    g.debugPlayer(0).hand = h;
    g.debugSetTurn(0, TurnStage::Play);
    analysis::Mistake m;
    CHECK(analysis::judgeClassicDiscard(g, 0, makeTileId(Red, 1, 0), m));
    std::printf("[okey constructed] %s\n", analysis::describeMistake(m).c_str());
    CHECK(m.notable);
    CHECK(m.cost >= 20.0);
    // a hand far from finished: the discard Kurt prefers is not a mistake
    Game g2(cfg);
    for (int s = 0; s < 4; ++s) g2.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g2.startMatch(9);
    const int st = g2.starter();
    Bot k(BotLevel::Hard, 3);
    while (g2.current() != 0 && g2.handState() == HandState::Playing) {
        const int s = g2.current();
        applyBotAction(g2, s, Bot(BotLevel::Normal, 1).next(g2, s));
    }
    if (g2.stage() == TurnStage::NeedDraw) g2.drawFromPile(0);
    BotAction a = k.next(g2, 0);
    if (a.kind == BotAction::Kind::Discard) {
        analysis::Mistake ok;
        CHECK(analysis::judgeClassicDiscard(g2, 0, a.tile, ok));
        std::printf("[okey constructed] Kurt's own discard: cost %.1f%% (noise %.1f), starter %d\n", ok.cost, ok.noise, st);
        CHECK(!ok.notable);
    }
}

void testTavlaConstructed() {
    using namespace tavla;
    const Position start = Position::initial();
    // opening 3-1: 8/5 6/5 (the 5-point) is right; the worst play by far is not
    Position good = start;
    CHECK(applyStepTo(good, 0, Step{7, 4, 3, false}));
    CHECK(applyStepTo(good, 0, Step{5, 4, 1, false}));
    analysis::Mistake mg;
    CHECK(analysis::judgeTavlaPlay(start, 0, 3, 1, good, 1, mg));
    std::printf("[tavla constructed] 8/5 6/5: cost %.3f\n", mg.cost);
    CHECK(!mg.notable);
    CHECK(mg.cost < 0.03);
    // a blot left in reach instead of a safe play: from a contact position take the worst play
    const std::vector<Play> plays = generatePlays(start, 0, 6, 4);
    size_t worst = 0;
    double wv = 1e18;
    for (size_t i = 0; i < plays.size(); ++i) {
        const double v = botEquityAfterMove(plays[i].result, 0, 2);
        if (v < wv) {
            wv = v;
            worst = i;
        }
    }
    analysis::Mistake mb;
    CHECK(analysis::judgeTavlaPlay(start, 0, 6, 4, plays[worst].result, 1, mb));
    std::printf("[tavla constructed] %s\n", analysis::describeMistake(mb).c_str());
    CHECK(mb.notable);
    CHECK(mb.cost >= 0.06 && mb.cost <= 1.0);
    // the stake doubles the cost
    analysis::Mistake m2;
    CHECK(analysis::judgeTavlaPlay(start, 0, 6, 4, plays[worst].result, 2, m2));
    CHECK(std::abs(m2.cost - 2 * mb.cost) < 1e-9);
}

void testPistiConstructed() {
    using namespace pisti;
    Game g;
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g.startMatch(21);
    // a single face-up card on the table (the closed ones already taken by Hacı Rıza), and seat 0 holds its twin
    std::vector<int>& open = g.debugOpen();
    std::vector<int>& closed = g.debugClosed();
    for (int c : closed) {
        g.debugCaptured(1).push_back(c);
        g.debugMarkSeen(c);
    }
    closed.clear();
    while (open.size() > 1) {
        g.debugCaptured(1).push_back(open.front());
        open.erase(open.begin());
    }
    const int top = open.back();
    int twin = -1;
    // find a card of the top's rank (not a Vale) in another hand or the deck and swap it into seat 0's hand
    std::vector<int>& mine = g.debugHand(0);
    auto rankOf = [](int c) { return kart::rankOf(c); };
    for (int s = 1; s < 4 && twin < 0; ++s)
        for (int& c : g.debugHand(s))
            if (rankOf(c) == rankOf(top) && twin < 0) {
                twin = c;
                std::swap(c, mine[0]);
            }
    for (int& c : g.debugDeck())
        if (twin < 0 && rankOf(c) == rankOf(top)) {
            twin = c;
            std::swap(c, mine[0]);
        }
    CHECK(twin >= 0);
    g.debugSetTurn(0);
    int other = -1;
    for (int c : g.hand(0))
        if (rankOf(c) != rankOf(top) && rankOf(c) != kart::Vale) other = c;
    if (rankOf(top) == kart::Vale || other < 0) return; // (an unlucky deal for this construction)
    CHECK(g.wouldPisti(twin) > 0);
    analysis::Mistake bad, good;
    CHECK(analysis::judgePistiCard(g, 0, other, bad));
    CHECK(analysis::judgePistiCard(g, 0, twin, good));
    std::printf("[pişti constructed] %s\n", analysis::describeMistake(bad).c_str());
    CHECK(bad.notable);
    CHECK(bad.cost >= 4.0 && bad.cost <= 25.0);
    CHECK(!good.notable);
}

void testBatakConstructed() {
    using namespace batak;
    using kart::makeCard;
    Game g;
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g.startMatch(4);
    // two tricks left, koz Karo (broken). I lead: Maça As then Maça 2 takes both; Maça 2 first gives one away.
    std::array<std::vector<int>, 4> hands;
    hands[0] = {makeCard(kart::Maca, 14), makeCard(kart::Maca, 2)};
    hands[1] = {makeCard(kart::Maca, 13), makeCard(kart::Kupa, 3)};
    hands[2] = {makeCard(kart::Maca, 12), makeCard(kart::Kupa, 4)};
    hands[3] = {makeCard(kart::Maca, 11), makeCard(kart::Kupa, 5)};
    g.debugStartPlay(hands, 1, 5, kart::Karo, 0, true);
    analysis::Mistake bad, good;
    CHECK(analysis::judgeBatakCard(g, 0, makeCard(kart::Maca, 2), bad));
    CHECK(analysis::judgeBatakCard(g, 0, makeCard(kart::Maca, 14), good));
    std::printf("[batak constructed] %s\n", analysis::describeMistake(bad).c_str());
    CHECK(bad.notable);
    CHECK(bad.cost >= 0.8 && bad.cost <= 10.0);
    CHECK(good.cost == 0.0);
}

void testKingConstructed() {
    using namespace king;
    using kart::makeCard;
    Game g;
    for (int s = 0; s < 4; ++s) g.setPlayer(s, analysis::defaultNames()[(size_t)s], s == 0);
    g.startMatch(3);
    // Kız Almaz: Hacı Rıza leads Maça 5, Kel Mahmut Maça 7, Emekli Nuri Maça 9; I hold Maça Kız and Maça 3: the Kız
    // on this trick costs me 100, the 3 ducks.
    std::array<std::vector<int>, 4> hands;
    hands[0] = {makeCard(kart::Maca, 12), makeCard(kart::Maca, 3)};
    hands[1] = {makeCard(kart::Maca, 5)};
    hands[2] = {makeCard(kart::Maca, 7)};
    hands[3] = {makeCard(kart::Maca, 9)};
    std::vector<int> rest;
    for (int c = 0; c < 52; ++c) {
        bool used = false;
        for (const auto& h : hands) used = used || std::find(h.begin(), h.end(), c) != h.end();
        if (!used && kart::suitOf(c) != kart::Maca) rest.push_back(c);
    }
    std::vector<int> spades; // the other spades go to Rıza (he leads them later)
    for (int r = 2; r <= 14; ++r) {
        const int c = makeCard(kart::Maca, r);
        bool used = false;
        for (const auto& h : hands) used = used || std::find(h.begin(), h.end(), c) != h.end();
        if (!used) spades.push_back(c);
    }
    for (int c : spades) hands[1].push_back(c);
    size_t i = 0;
    for (int s = 0; s < 4; ++s)
        while (hands[(size_t)s].size() < 13) hands[(size_t)s].push_back(rest[i++]);
    for (auto& h : hands) std::sort(h.begin(), h.end());
    g.debugSetHands(hands);
    g.debugSetChooser(1);
    CHECK(g.chooseContract(1, Contract::KizAlmaz).ok);
    CHECK(g.playCard(1, makeCard(kart::Maca, 5)).ok);
    CHECK(g.playCard(2, makeCard(kart::Maca, 7)).ok);
    CHECK(g.playCard(3, makeCard(kart::Maca, 9)).ok);
    analysis::Mistake bad, good;
    CHECK(analysis::judgeKingCard(g, 0, makeCard(kart::Maca, 12), bad));
    CHECK(analysis::judgeKingCard(g, 0, makeCard(kart::Maca, 3), good));
    std::printf("[king constructed] %s\n", analysis::describeMistake(bad).c_str());
    CHECK(bad.notable);
    CHECK(bad.cost >= 25.0 && bad.cost <= 150.0);
    CHECK(!good.notable);
}

void testTexts() {
    analysis::Mistake m;
    m.when = "3. el, 14. tur";
    m.played = "Kırmızı 12'yi attın";
    m.why = "Hacı Rıza onu alıp açtı";
    m.better = "Kurt Sarı 3'ü atardı";
    m.cost = 38.2;
    m.unit = "puan";
    CHECK(analysis::describeMistake(m) ==
          "3. el, 14. tur: Kırmızı 12'yi attın, Hacı Rıza onu alıp açtı. Kurt Sarı 3'ü atardı (yaklaşık 38 puan kayıp)");
    m.cost = 0.35;
    CHECK(analysis::describeMistake(m).find("yaklaşık 0,35 puan kayıp") != std::string::npos);
}

} // namespace

int main() {
    testTexts();
    testOkeyConstructed();
    testClassicConstructed();
    testTavlaConstructed();
    testPistiConstructed();
    testBatakConstructed();
    testKingConstructed();

    ui::Settings st;
    st.numHands = 3;
    {
        const SimMatch sm = simOkey(ui::GameKind::Yuzbir, st, 41, {4});
        checkMatch("101", ui::GameKind::Yuzbir, st, 41, sm, 2, 8.0);
    }
    {
        ui::Settings se = st;
        se.numHands = 5;
        const SimMatch sm = simOkey(ui::GameKind::YuzbirEsli, se, 43, {2});
        checkMatch("eşli 101", ui::GameKind::YuzbirEsli, se, 43, sm, 2, 8.0);
    }
    {
        ui::Settings so = st;
        so.okeyStart = 10;
        const SimMatch sm = simOkey(ui::GameKind::Okey, so, 47, {2});
        checkMatch("okey", ui::GameKind::Okey, so, 47, sm, 2, 8.0);
    }
    {
        ui::Settings stv = st;
        stv.tavlaPoints = 3;
        const SimMatch sm = simTavla(stv, 51, {4, 9, 15});
        checkMatch("tavla", ui::GameKind::Tavla, stv, 51, sm, 2, 8.0);
    }
    {
        ui::Settings sp = st;
        sp.pistiTarget = 101;
        const SimMatch sm = simPisti(sp, 53, 3);
        checkMatch("pişti", ui::GameKind::Pisti, sp, 53, sm, 2, 8.0);
    }
    {
        ui::Settings sb = st;
        sb.batakTarget = 31;
        const SimMatch sm = simBatak(sb, 57, 3);
        checkMatch("batak", ui::GameKind::Batak, sb, 57, sm, 2, 8.0);
    }
    {
        ui::Settings sk = st;
        sk.king12 = true;
        const SimMatch sm = simKing(sk, 59, 3);
        checkMatch("king", ui::GameKind::King, sk, 59, sm, 2, 8.0);
    }
    // on a worker thread, as App runs it; a cancelled analysis stops at once
    {
        const SimMatch sm = simOkey(ui::GameKind::Yuzbir, st, 41, {4});
        std::vector<analysis::Mistake> res;
        std::atomic<bool> cancel{false};
        std::thread th([&] { res = analysis::analyzeMatch(ui::GameKind::Yuzbir, st, 41, sm.lines, analysis::defaultNames(), 3, &cancel); });
        th.join();
        CHECK(!res.empty() && analysis::describeMistake(res[0]) ==
                                  analysis::describeMistake(analysis::analyzeMatch(ui::GameKind::Yuzbir, st, 41, sm.lines)[0]));
        cancel = true;
        const auto t0 = std::chrono::steady_clock::now();
        std::thread th2([&] { res = analysis::analyzeMatch(ui::GameKind::Yuzbir, st, 41, sm.lines, analysis::defaultNames(), 3, &cancel); });
        th2.join();
        CHECK(res.empty());
        CHECK(seconds(t0) < 0.05);
        // lines that do not replay: nothing (and no crash)
        CHECK(analysis::analyzeMatch(ui::GameKind::Batak, st, 41, {"garbage", "1 2 3"}).empty());
        CHECK(analysis::analyzeMatch(ui::GameKind::Tavla, st, 41, {}).empty());
    }
    // a whole match at the default settings (the forced mistakes off): how long the analysis takes
    {
        const ui::Settings d;
        struct T {
            const char* name;
            ui::GameKind kind;
            SimMatch sm;
        };
        const T runs[] = {
            {"101 (5 el)", ui::GameKind::Yuzbir, simOkey(ui::GameKind::Yuzbir, d, 61, {})},
            {"eşli 101 (5 el)", ui::GameKind::YuzbirEsli, simOkey(ui::GameKind::YuzbirEsli, d, 62, {})},
            {"okey (20 puan)", ui::GameKind::Okey, simOkey(ui::GameKind::Okey, d, 63, {})},
            {"tavla (5 sayı)", ui::GameKind::Tavla, simTavla(d, 64, {})},
            {"pişti (101)", ui::GameKind::Pisti, simPisti(d, 65, 0)},
            {"batak (51)", ui::GameKind::Batak, simBatak(d, 66, 0)},
            {"king (20 el)", ui::GameKind::King, simKing(d, 67, 0)},
        };
        uint64_t seed = 61;
        for (const T& t : runs) {
            const auto t0 = std::chrono::steady_clock::now();
            const std::vector<analysis::Mistake> ms = analysis::analyzeMatch(t.kind, d, seed++, t.sm.lines);
            const double secs = seconds(t0);
            printMistakes(t.name, ms, secs, t.sm.lines.size());
            CHECK(secs < 10.0);
        }
    }
    std::printf("test_analysis: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
