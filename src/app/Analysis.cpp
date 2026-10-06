// Hata analizi: see Analysis.h. The match is replayed action by action; at each of the player's (seat 0) decisions a
// Kurt values the move made against its best alternative (the bots' evaluate* APIs), and small "trackers" watch the
// following events to say what happened next (the tile was taken, the trick was lost, the checker was hit).
#include "app/Analysis.h"

#include "core/BatakBot.h"
#include "core/Bot.h"
#include "core/Cards.h"
#include "core/KingBot.h"
#include "core/PistiBot.h"
#include "core/TavlaBot.h"
#include "ui/Screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

namespace analysis {

namespace {

// ---------------------------------------------------------------------------------------------------------
// Thresholds: what is worth telling (in the game's unit), and how clear of the sampling noise it must be.
constexpr double OKEY_MIN = 12.0;      // 101: points of expected hand score
constexpr double CLASSIC_MIN = 8.0;    // klasik okey: percentage points of the finishing chance
constexpr double CLASSIC_SCREEN = 0.25; // ... valued only when Kurt's quick hand value finds the discard this much worse
constexpr int CLASSIC_MAX_EVALS = 20;   // ... and at most this many per match (the most suspicious)
constexpr double TAVLA_MIN = 0.06;     // tavla: equity (x the cube)
constexpr double PISTI_MIN = 1.0;      // pişti: points
constexpr double BATAK_MIN = 0.8;      // batak card play: points of score difference
constexpr double BATAK_BID_MIN = 1.5;  // batak ihale / koz
constexpr double KING_MIN = 25.0;      // king card play: points
constexpr double KING_CHOICE_MIN = 30.0;
constexpr double NOISE_Z = 2.0;        // cost must exceed this many standard errors

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 31);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string ordinal(int n) { return std::to_string(n) + "."; }

// "12'yi", "3'ü", "6'yı" (belirtme hali of a number written in digits)
std::string numberAcc(int n) {
    static const char* const unit[] = {"'ı", "'i", "'yi", "'ü", "'ü", "'i", "'yı", "'yi", "'i", "'u"};
    if (n % 10 == 0) {
        switch (n % 100 == 0 ? 100 : n % 100) {
        case 10: return std::to_string(n) + "'u";
        case 20: return std::to_string(n) + "'yi";
        case 30: return std::to_string(n) + "'u";
        default: return std::to_string(n) + "'ı";
        }
    }
    return std::to_string(n) + unit[n % 10];
}

// "Kırmızı 12'yi", "Okeyi", "Sahte Okeyi"
std::string tileAcc(int id, const okey::OkeyInfo& ok) {
    if (!okey::isValidTile(id)) return "?";
    if (ok.isJoker(id)) return "Okeyi";
    if (okey::isFakeJoker(id)) return "Sahte Okeyi";
    return std::string(okey::colorNameTR(okey::printedColor(id))) + " " + numberAcc(okey::printedNumber(id));
}

std::string suitAcc(int suit) {
    switch (suit) {
    case kart::Maca: return "Maça'yı";
    case kart::Kupa: return "Kupa'yı";
    case kart::Karo: return "Karo'yu";
    case kart::Sinek: return "Sinek'i";
    default: return "?";
    }
}

std::string pct(double p) { return "%" + std::to_string((int)std::lround(100.0 * std::clamp(p, 0.0, 1.0))); }

std::string number(double v) {
    char buf[32];
    const double a = std::fabs(v);
    if (a >= 9.95) std::snprintf(buf, sizeof buf, "%.0f", v);
    else if (a >= 0.995) std::snprintf(buf, sizeof buf, "%.1f", v);
    else std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

bool notableCost(double cost, double noise, double minCost) { return cost >= minCost && cost > NOISE_Z * noise; }

// Settings -> the engines' rules: kept in sync with App::startMatch and the tables' startMatch.
okey::RulesConfig okeyRules(ui::GameKind kind, const ui::Settings& st) {
    okey::RulesConfig cfg;
    cfg.variant = kind == ui::GameKind::Okey ? okey::Variant::Okey : okey::Variant::Yuzbir;
    cfg.teams = kind == ui::GameKind::YuzbirEsli;
    cfg.okeyStartPoints = std::clamp(st.okeyStart, 1, 99);
    cfg.okeyColorDouble = st.okeyRenkli;
    cfg.numHands = std::clamp(st.numHands, 1, 11);
    cfg.katlamali = st.katlamali;
    cfg.leftOpenPenalty = st.yandanCeza;
    return cfg;
}
tavla::Rules tavlaRules(const ui::Settings& st) {
    tavla::Rules r;
    r.matchPoints = std::clamp(st.tavlaPoints, 1, 15);
    r.doubling = st.tavlaDoubling;
    r.katmerliMars = st.tavlaKatmerli;
    return r;
}
pisti::Rules pistiRules(const ui::Settings& st) {
    pisti::Rules r;
    r.mode = (pisti::Mode)std::clamp(st.pistiMode, 0, 2);
    r.targetScore = std::clamp(st.pistiTarget, 51, 301);
    return r;
}
batak::Rules batakRules(const ui::Settings& st) {
    batak::Rules r = st.batakEsli ? batak::Rules::esliBatak() : batak::Rules::tekli();
    r.targetScore = std::clamp(st.batakTarget, 11, 151);
    r.trumpMustBeBroken = st.batakKozKirilmadan;
    return r;
}
king::Rules kingRules(const ui::Settings& st) {
    king::Rules rules;
    if (st.king12) {
        rules.kozPerPlayer = 1;
        rules.cezaPerPlayer = 2;
    }
    return rules;
}

// The notable mistakes of a match, most expensive first, at most one per (hand, turn).
std::vector<Mistake> pick(std::vector<Mistake> all, int maxResults) {
    std::vector<Mistake> found;
    for (Mistake& m : all)
        if (m.notable) found.push_back(std::move(m));
    std::stable_sort(found.begin(), found.end(), [](const Mistake& a, const Mistake& b) { return a.cost > b.cost; });
    std::vector<Mistake> out;
    for (Mistake& m : found) {
        if ((int)out.size() >= maxResults) break;
        bool dup = false;
        for (const Mistake& o : out)
            dup = dup || (o.hand == m.hand && o.turn == m.turn) ||
                  (o.hand == m.hand && o.topic == m.topic && (m.topic == "açış" || m.topic == "bitiş"));
        if (!dup) out.push_back(std::move(m));
    }
    return out;
}

bool cancelled(const std::atomic<bool>* c) { return c && c->load(std::memory_order_relaxed); }

// ---------------------------------------------------------------------------------------------------------
// 101 / klasik okey

struct OkeyJudge {
    okey::Bot eval;  // the valuations
    okey::Bot kurt;  // "what would Kurt do here" (its own plans)
    explicit OkeyJudge(uint64_t seed)
        : eval(okey::BotLevel::Hard, mix(seed, 0xA11A)), kurt(okey::BotLevel::Hard, mix(seed, 0xA11B)) {}

    // The discard of `tile` (101). `turnStart`: the state when this turn's Play stage began (null: unknown).
    bool discard(const okey::Game& g, int seat, int tile, const okey::Game* turnStart,
                 const std::array<std::string, 4>& names, Mistake& out) {
        (void)names;
        const okey::PlayerInfo& p = g.player(seat);
        if (p.hand.size() <= 1 || g.pendingLeftTile() >= 0) return false;
        const okey::OkeyInfo& ok = g.okey();
        const std::vector<okey::Bot::TileValue> vals = eval.evaluateDiscards(g, seat);
        if (vals.empty()) return false;
        double actual = 0.0, best = 1e18;
        int bestTile = -1;
        bool found = false;
        for (const auto& v : vals) {
            if (v.tile == tile) {
                actual = v.value;
                found = true;
            }
            if (v.value < best - 1e-9) {
                best = v.value;
                bestTile = v.tile;
            }
        }
        if (!found) return false;
        std::string better = "Kurt " + tileAcc(bestTile, ok) + " atardı";
        std::string topic = "atış";
        // opening now instead of waiting
        std::vector<std::vector<int>> melds;
        if (!p.opened && eval.openingNow(g, seat, melds)) {
            okey::Game g2 = g;
            if (g2.openHand(seat, melds).ok) {
                double ev = 1e18;
                int t2 = -1;
                if (g2.player(seat).hand.size() == 1) {
                    okey::Game g3 = g2;
                    if (g3.discard(seat, g2.player(seat).hand[0]).ok) ev = g3.lastHandResult().score[seat];
                } else {
                    for (const auto& v : eval.evaluateDiscards(g2, seat))
                        if (v.value < ev) {
                            ev = v.value;
                            t2 = v.tile;
                        }
                }
                if (ev < best - 1e-9) {
                    best = ev;
                    better = t2 >= 0 ? "Kurt elini açıp " + tileAcc(t2, ok) + " atardı" : "Kurt elini açıp bitirirdi";
                    topic = "açış";
                }
            }
        }
        // finishing this turn (Kurt's whole turn from the start of the Play stage)
        if (turnStart && turnStart->turnNumber() == g.turnNumber() && turnStart->current() == seat) {
            okey::Game g4 = *turnStart;
            for (int i = 0; i < 60 && g4.handState() == okey::HandState::Playing && g4.current() == seat &&
                            g4.turnNumber() == g.turnNumber();
                 ++i) {
                const okey::BotAction a = kurt.next(g4, seat);
                if (!okey::applyBotAction(g4, seat, a).ok) break;
            }
            if (g4.handState() != okey::HandState::Playing && g4.lastHandResult().winner == seat) {
                const double fin = g4.lastHandResult().score[seat];
                if (fin < best - 1e-9) {
                    best = fin;
                    better = "Kurt bu el bitirirdi";
                    topic = "bitiş";
                }
            }
        }
        out = Mistake();
        out.unit = "puan";
        out.cost = std::max(0.0, actual - best);
        out.topic = topic;
        out.played = (topic == "açış" ? "elini açmadın, " : topic == "bitiş" ? "bitirmedin, " : "") + tileAcc(tile, ok) + " attın";
        out.better = better;
        const okey::RulesConfig& rc = g.rules();
        if (ok.isJoker(tile) && rc.penaltyJokerDiscard)
            out.why = "okey atmak " + std::to_string(rc.penalty) + " ceza";
        else if (!ok.isJoker(tile) && rc.penaltyPlayableDiscard && g.isPlayableOnTable(tile))
            out.why = "işlek taştı, " + std::to_string(rc.penalty) + " ceza";
        else
            out.why = "Kurt'un hesabıyla elin beklenen puanı " + number(best) + " yerine " + number(actual);
        out.notable = notableCost(out.cost, 0.0, OKEY_MIN);
        return true;
    }

    // NeedDraw (101, not opened, a left tile): judged only when Kurt would draw the other way.
    bool draw(const okey::Game& g, int seat, bool tookLeft, Mistake& out) {
        if (g.player(seat).opened || g.stage() != okey::TurnStage::NeedDraw) return false;
        const int L = g.topDiscard(okey::Game::leftOf(seat));
        if (L < 0 || !g.canTakeFromLeft(seat) || g.pileCount() == 0) return false;
        const okey::BotAction k = kurt.next(g, seat);
        const bool kurtTakes = k.kind == okey::BotAction::Kind::TakeLeft;
        out = Mistake();
        out.unit = "puan";
        out.topic = "çekiş";
        const okey::OkeyInfo& ok = g.okey();
        out.played = tookLeft ? "soldan " + tileAcc(L, ok) + " aldın" : "soldaki " + okey::tileNameTR(L, ok) + " yerine desteden çektin";
        if (!kurtTakes || tookLeft) return true; // the same choice, or the direction not told (see below): cost 0
        double take = 0, pile = 0;
        if (!eval.evaluateDraw(g, seat, take, pile)) return false;
        const double actual = tookLeft ? take : pile, best = std::min(take, pile);
        out.cost = std::max(0.0, actual - best);
        out.better = kurtTakes ? "Kurt " + tileAcc(L, ok) + " alıp açardı" : "Kurt desteden çekerdi";
        out.why = "Kurt'un hesabıyla elin beklenen puanı " + number(best) + " yerine " + number(actual);
        // Only "Kurt would have taken it and opened" is told: the take line is valued as an opened hand and the pile
        // line as the better of opening and waiting, and the rollouts of a hand still waiting to open are the more
        // hopeful of the two, so this direction is the safe one.
        out.notable = kurtTakes && !tookLeft && notableCost(out.cost, 0.0, OKEY_MIN);
        return true;
    }

    // Klasik okey, the cheap screen of a discard: how much worse Kurt's quick hand value finds it than Kurt's own choice
    // (-1: it is Kurt's choice; a large value when the hand could have been finished instead).
    double classicScreen(const okey::Game& g, int seat, int tile) {
        for (int t : g.player(seat).hand)
            if (g.finishKind(seat, t) > 0) return 1e9;
        if (g.player(seat).hand.size() <= 1) return -1.0;
        const okey::BotAction k = kurt.next(g, seat);
        if (k.kind != okey::BotAction::Kind::Discard) return -1.0;
        const okey::OkeyInfo& ok = g.okey();
        if (k.tile == tile || (!ok.isJoker(tile) && !ok.isJoker(k.tile) && ok.faceColor(k.tile) == ok.faceColor(tile) &&
                               ok.faceNumber(k.tile) == ok.faceNumber(tile)))
            return -1.0;
        return eval.classicKeepValue(g, seat, k.tile) - eval.classicKeepValue(g, seat, tile);
    }

    // Klasik okey: the discard of `tile` (or not finishing when it could), valued by its finishing chances.
    bool classic(const okey::Game& g, int seat, int tile, Mistake& out) {
        const okey::OkeyInfo& ok = g.okey();
        auto sameFace = [&](int a, int b) {
            if (ok.isJoker(a) || ok.isJoker(b)) return ok.isJoker(a) && ok.isJoker(b);
            return ok.faceColor(a) == ok.faceColor(b) && ok.faceNumber(a) == ok.faceNumber(b);
        };
        int finishTile = -1;
        for (int t : g.player(seat).hand)
            if (g.finishKind(seat, t) > 0) {
                finishTile = t;
                break;
            }
        out = Mistake();
        out.unit = "%";
        out.topic = "atış";
        out.played = tileAcc(tile, ok) + " attın";
        if (finishTile < 0 && g.player(seat).hand.size() <= 1) return false;
        const std::vector<okey::Bot::TileValue> vals = eval.evaluateClassicDiscards(g, seat, tile);
        if (vals.empty()) return false;
        double actual = -1, best = -1, seA = 0, seB = 0;
        int bestTile = -1;
        for (const auto& v : vals) {
            if (sameFace(v.tile, tile)) {
                actual = v.value;
                seA = v.se;
            }
            if (v.value > best + 1e-9) {
                best = v.value;
                bestTile = v.tile;
                seB = v.se;
            }
        }
        if (actual < 0) actual = 0.0; // (an okey thrown away: not among the candidates)
        if (finishTile >= 0) {
            out.topic = "bitiş";
            out.played = "bitirmedin, " + out.played;
            out.cost = 100.0 * (1.0 - actual);
            out.better = "Kurt " + tileAcc(finishTile, ok) + " atıp bitirirdi";
            out.why = "elin bitmişti";
            out.notable = true;
            return true;
        }
        if (bestTile < 0) return false;
        out.cost = std::max(0.0, 100.0 * (best - actual));
        // (the candidates share their sampled futures: the difference is less noisy than the two values)
        out.noise = 100.0 * 0.75 * std::sqrt(seA * seA + seB * seB);
        out.better = "Kurt " + tileAcc(bestTile, ok) + " atardı";
        out.why = "bitme şansın " + pct(best) + " yerine " + pct(actual);
        out.notable = notableCost(out.cost, out.noise, CLASSIC_MIN);
        return true;
    }
};

std::vector<Mistake> analyzeOkey(ui::GameKind kind, const ui::Settings& st, uint64_t seed,
                                 const std::vector<std::string>& lines, const std::array<std::string, 4>& names,
                                 const std::atomic<bool>* cancel) {
    okey::Game g(okeyRules(kind, st));
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[(size_t)s], s == 0);
    g.startMatch(seed);
    g.drainEvents();
    OkeyJudge judge(seed);
    std::vector<Mistake> all;
    int lastHand = -1, handBase = 0;
    std::unique_ptr<okey::Game> turnStart;
    // klasik okey: the discards that pass the screen, valued after the replay (the most suspicious first, capped)
    struct ClassicCand {
        double screen;
        std::unique_ptr<okey::Game> g;
        int tile, hand, turn;
    };
    std::vector<ClassicCand> classicCands;
    // follow-up of a 101 discard: did the right neighbour take it and open?
    struct Track {
        size_t idx;
        int tile;
        bool took = false, opened = false;
        int penalty = 0;
    };
    std::vector<Track> tracks;
    for (const std::string& line : lines) {
        if (cancelled(cancel)) break;
        okey::LoggedAction a;
        if (!okey::LoggedAction::decode(trim(line), a)) break;
        if (g.handIndex() != lastHand) {
            lastHand = g.handIndex();
            handBase = g.turnNumber();
            turnStart.reset();
        }
        const bool mine = a.seat == 0 && g.current() == 0 && g.handState() == okey::HandState::Playing;
        const int round = (g.turnNumber() - handBase) / 4 + 1;
        Mistake m;
        bool judged = false;
        if (mine && !g.classic() && (a.kind == okey::LogKind::Draw || a.kind == okey::LogKind::TakeLeft))
            judged = judge.draw(g, 0, a.kind == okey::LogKind::TakeLeft, m);
        else if (mine && a.kind == okey::LogKind::Discard && g.classic()) {
            const double sc = judge.classicScreen(g, 0, a.tile);
            if (sc >= CLASSIC_SCREEN)
                classicCands.push_back({sc, std::make_unique<okey::Game>(g), a.tile, g.handIndex() + 1, round});
        }
        else if (mine && a.kind == okey::LogKind::Discard)
            judged = judge.discard(g, 0, a.tile, turnStart.get(), names, m);
        if (judged && m.notable) {
            m.hand = g.handIndex() + 1;
            m.turn = round;
            m.when = ordinal(m.hand) + " el, " + ordinal(m.turn) + " tur";
            all.push_back(m);
            if (a.kind == okey::LogKind::Discard && !g.classic()) tracks.push_back({all.size() - 1, a.tile});
        }
        if (!g.replay(a)) break;
        for (const okey::GameEvent& e : g.drainEvents()) {
            for (Track& t : tracks) {
                if (t.idx == (size_t)-1) continue;
                if (e.type == okey::EvType::TakeLeft && e.player == 1 && e.tile == t.tile) t.took = true;
                else if (e.type == okey::EvType::Open && e.player == 1 && t.took) t.opened = true;
                else if (e.type == okey::EvType::Penalty && e.player == 0 && t.opened) t.penalty += e.amount;
                else if ((e.type == okey::EvType::TurnStart && e.player == 2) || e.type == okey::EvType::HandEnd) {
                    if (t.took) {
                        Mistake& mm = all[t.idx];
                        std::string w = names[1] + (t.opened ? " onu alıp açtı" : " onu aldı");
                        if (t.penalty > 0) w += ", sana " + std::to_string(t.penalty) + " ceza yazıldı";
                        mm.why = w;
                    }
                    t.idx = (size_t)-1;
                }
            }
        }
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.idx == (size_t)-1; }),
                     tracks.end());
        // the Play stage of my turn begins: remember it (Kurt's whole turn is tried from here)
        if (mine && (a.kind == okey::LogKind::Draw || a.kind == okey::LogKind::TakeLeft) &&
            g.stage() == okey::TurnStage::Play && g.current() == 0)
            turnStart = std::make_unique<okey::Game>(g);
    }
    std::stable_sort(classicCands.begin(), classicCands.end(),
                     [](const ClassicCand& x, const ClassicCand& y) { return x.screen > y.screen; });
    for (size_t i = 0; i < classicCands.size() && i < (size_t)CLASSIC_MAX_EVALS; ++i) {
        if (cancelled(cancel)) break;
        const ClassicCand& c = classicCands[i];
        Mistake m;
        if (judge.classic(*c.g, 0, c.tile, m) && m.notable) {
            m.hand = c.hand;
            m.turn = c.turn;
            m.when = ordinal(m.hand) + " el, " + ordinal(m.turn) + " tur";
            all.push_back(m);
        }
    }
    return all;
}

// ---------------------------------------------------------------------------------------------------------
// tavla

// The steps of a play with their hit flags (a lone opposing checker on the landing point).
std::vector<tavla::Step> withHits(const tavla::Position& start, int p, std::vector<tavla::Step> steps) {
    tavla::Position pos = start;
    for (tavla::Step& s : steps) {
        s.hit = s.to >= 0 && s.to < tavla::kPoints && pos.owner(s.to) == 1 - p && pos.count(s.to) == 1;
        tavla::applyStepTo(pos, p, s);
    }
    return steps;
}

std::string stepsText(int p, const std::vector<tavla::Step>& steps) {
    std::string s;
    for (const tavla::Step& st : steps) s += (s.empty() ? "" : " ") + tavla::stepNotation(p, st);
    return s.empty() ? std::string("hiçbir şey") : s;
}

bool tavlaPlay(const tavla::Position& before, int p, int d1, int d2, const tavla::Position& after, int cube,
               Mistake& out) {
    const std::vector<tavla::Play> plays = tavla::generatePlays(before, p, d1, d2);
    if (plays.size() <= 1) return false;
    int actual = -1;
    for (size_t i = 0; i < plays.size(); ++i)
        if (plays[i].result == after) actual = (int)i;
    if (actual < 0) return false;
    std::vector<std::pair<double, int>> one;
    for (size_t i = 0; i < plays.size(); ++i) one.push_back({-tavla::botEquityAfterMove(plays[i].result, p, 1), (int)i});
    std::stable_sort(one.begin(), one.end());
    std::vector<int> cands;
    for (size_t i = 0; i < one.size() && cands.size() < 6; ++i) cands.push_back(one[i].second);
    if (std::find(cands.begin(), cands.end(), actual) == cands.end()) cands.push_back(actual);
    double bestEq = -1e18, actualEq = 0, bestWin = 0, actualWin = 0;
    int best = actual;
    for (int i : cands) {
        double w = 0;
        const double e = tavla::botEquityAfterMove(plays[(size_t)i].result, p, 2, &w);
        if (i == actual) {
            actualEq = e;
            actualWin = w;
        }
        if (e > bestEq + 1e-12) {
            bestEq = e;
            best = i;
            bestWin = w;
        }
    }
    out = Mistake();
    out.unit = "puan";
    out.topic = "hamle";
    out.cost = std::max(0.0, (bestEq - actualEq) * cube);
    tavla::TurnRecord tr;
    tr.player = p;
    tr.d1 = std::max(d1, d2);
    tr.d2 = std::min(d1, d2);
    out.played = std::to_string(tr.d1) + "-" + std::to_string(tr.d2) + " (" + tavla::diceName(d1, d2) + ") ile " +
                 stepsText(p, withHits(before, p, plays[(size_t)actual].steps)) + " oynadın";
    out.better = "Kurt " + stepsText(p, withHits(before, p, plays[(size_t)best].steps)) + " oynardı";
    if (std::lround(100 * bestWin) != std::lround(100 * actualWin))
        out.why = "kazanma şansın " + pct(bestWin) + " yerine " + pct(actualWin);
    else // the same chance to win: the difference is the mars
        out.why = actualWin < 0.5 ? "mars olma tehlikesi arttı" : "mars yapma şansın azaldı";
    out.notable = notableCost(out.cost, 0.0, TAVLA_MIN * cube);
    return true;
}

std::vector<Mistake> analyzeTavla(const ui::Settings& st, uint64_t seed, const std::vector<std::string>& lines,
                                  const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    tavla::Game g(tavlaRules(st));
    g.setPlayer(0, names[0], true);
    g.setPlayer(1, names[2], false);
    g.startMatch(seed);
    g.drainEvents();
    std::vector<Mistake> all;
    bool capturing = false;
    tavla::Position pos0;
    int d1 = 0, d2 = 0, cube = 1, capTurn = -1, capGame = 0, moveNo = 0, lastGame = -1;
    struct Track {
        size_t idx;
        bool oppTurn = false;
    };
    std::vector<Track> tracks;
    auto capture = [&]() {
        if (g.gameIndex() != lastGame) {
            lastGame = g.gameIndex();
            moveNo = 0;
        }
        if (!capturing && g.stage() == tavla::Stage::Moving && g.current() == 0 && g.turnSteps().empty()) {
            capturing = true;
            pos0 = g.position();
            d1 = g.dice().d1;
            d2 = g.dice().d2;
            cube = g.cubeValue();
            capTurn = g.turnNumber();
            capGame = g.gameIndex();
            ++moveNo;
        }
    };
    capture();
    for (const std::string& line : lines) {
        if (cancelled(cancel)) break;
        tavla::LoggedAction a;
        if (!tavla::LoggedAction::decode(trim(line), a)) break;
        // take / drop of a double offered to me
        if (a.player == 0 && g.responder() == 0 && (a.kind == tavla::ActKind::Take || a.kind == tavla::ActKind::Drop)) {
            const int c = g.cubeValue(), mp = g.matchPoints();
            if (g.score(0) + 2 * c < mp && g.score(1) + 2 * c < mp) {
                double win = 0;
                const double eq = -tavla::botEquityToRoll(g.position(), 1, &win); // the opponent is to roll
                const double eTake = 2.0 * c * eq, eDrop = -1.0 * c;
                const bool took = a.kind == tavla::ActKind::Take;
                Mistake m;
                m.unit = "puan";
                m.topic = "katlama";
                m.cost = std::max(0.0, took ? eDrop - eTake : eTake - eDrop);
                m.played = took ? "katlamayı kabul ettin" : "katlamayı reddettin";
                m.better = took ? "Kurt pes ederdi" : "Kurt kabul ederdi";
                m.why = "kazanma şansın " + pct(1.0 - win);
                m.notable = notableCost(m.cost, 0.0, (took ? 0.35 : 0.25) * c);
                m.hand = g.gameIndex() + 1;
                m.turn = moveNo + 1;
                m.when = ordinal(m.hand) + " oyun, katlama";
                if (m.notable) all.push_back(m);
            }
        }
        if (!g.replay(a)) break;
        for (const tavla::GameEvent& e : g.drainEvents()) {
            for (Track& t : tracks) {
                if (t.idx == (size_t)-1) continue;
                if (e.type == tavla::EvType::Roll && e.player == 1) t.oppTurn = true;
                else if (e.type == tavla::EvType::Hit && e.player == 1 && e.amount == 0 && t.oppTurn) {
                    Mistake& mm = all[t.idx];
                    mm.why = names[2] + " sıradaki zarda pulunu kırdı; " + mm.why;
                    t.idx = (size_t)-1;
                } else if ((e.type == tavla::EvType::TurnEnd && e.player == 1) || e.type == tavla::EvType::GameEnd) {
                    t.idx = (size_t)-1;
                }
            }
        }
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.idx == (size_t)-1; }),
                     tracks.end());
        if (capturing && (g.stage() != tavla::Stage::Moving || g.current() != 0 || g.turnNumber() != capTurn ||
                          g.gameIndex() != capGame)) {
            capturing = false;
            const tavla::Position after = g.gameIndex() != capGame ? pos0 : g.position();
            Mistake m;
            if (g.gameIndex() == capGame && tavlaPlay(pos0, 0, d1, d2, after, cube, m) && m.notable) {
                m.hand = capGame + 1;
                m.turn = moveNo;
                m.when = ordinal(m.hand) + " oyun, " + ordinal(m.turn) + " hamle";
                all.push_back(m);
                tracks.push_back({all.size() - 1});
            }
        }
        capture();
    }
    return all;
}

// ---------------------------------------------------------------------------------------------------------
// the card games

template <class CV>
bool cardCost(const std::vector<CV>& vals, int card, double& cost, double& noise, int& best) {
    const CV* act = nullptr;
    const CV* b = nullptr;
    for (const CV& v : vals) {
        if (v.card == card) act = &v;
        if (!b || v.value > b->value + 1e-12) b = &v;
    }
    if (!act || !b) return false;
    cost = std::max(0.0, b->value - act->value);
    noise = act->se;
    best = b->card;
    return true;
}

// pişti
bool pistiCard(pisti::Bot& bot, const pisti::Game& g, int seat, int card, Mistake& out) {
    if (g.hand(seat).size() <= 1) return false;
    const std::vector<pisti::CardValue> vals = bot.evaluate(g, seat);
    double cost = 0, noise = 0;
    int best = -1;
    if (!cardCost(vals, card, cost, noise, best)) return false;
    out = Mistake();
    out.unit = "puan";
    out.topic = "kart";
    out.cost = cost;
    out.noise = noise;
    out.played = kart::cardAccusativeTR(card) + " attın";
    const int pp = g.wouldPisti(best);
    if (pp > 0 && g.wouldPisti(card) == 0)
        out.better = "Kurt " + kart::cardAccusativeTR(best) + " atıp pişti yapardı (+" + std::to_string(pp) + ")";
    else if (g.wouldCapture(best) && !g.wouldCapture(card))
        out.better = "Kurt " + kart::cardAccusativeTR(best) + " atıp masayı alırdı";
    else
        out.better = "Kurt " + kart::cardAccusativeTR(best) + " atardı";
    out.why = "Kurt'un hesabıyla bu elden beklenen puan farkın " + number(cost) + " düştü";
    out.notable = notableCost(cost, noise, PISTI_MIN);
    return true;
}

std::vector<Mistake> analyzePisti(const ui::Settings& st, uint64_t seed, const std::vector<std::string>& lines,
                                  const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    pisti::Game g(pistiRules(st));
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[(size_t)s], s == 0);
    pisti::Bot bot(pisti::BotLevel::Kurt, mix(seed, 0x9157));
    g.startMatch(seed);
    for (const pisti::GameEvent& e : g.drainEvents()) bot.observe(e, g);
    std::vector<Mistake> all;
    int lastHand = -1, cardNo = 0;
    struct Track {
        size_t idx;
    };
    std::vector<Track> tracks;
    for (const std::string& line : lines) {
        if (cancelled(cancel)) break;
        pisti::LoggedAction a;
        if (!pisti::LoggedAction::decode(trim(line), a)) break;
        if (g.handIndex() != lastHand) {
            lastHand = g.handIndex();
            cardNo = 0;
        }
        if (a.kind == pisti::LogKind::Play && a.seat == 0 && g.current() == 0 && g.stage() == pisti::Stage::Playing) {
            ++cardNo;
            Mistake m;
            if (pistiCard(bot, g, 0, a.card, m) && m.notable) {
                m.hand = g.handIndex() + 1;
                m.turn = cardNo;
                m.when = ordinal(m.hand) + " el, " + ordinal(m.turn) + " kart";
                all.push_back(m);
                tracks.push_back({all.size() - 1});
            }
        }
        if (!g.replay(a)) break;
        for (const pisti::GameEvent& e : g.drainEvents()) {
            bot.observe(e, g);
            for (Track& t : tracks) {
                if (t.idx == (size_t)-1) continue;
                const bool opp = e.seat >= 0 && e.seat != 0 && !g.sameSide(e.seat, 0);
                if (e.type == pisti::EvType::Pisti && opp) {
                    all[t.idx].why = names[(size_t)e.seat] + " pişti yaptı (+" + std::to_string(e.points) + ")";
                    t.idx = (size_t)-1;
                } else if (e.type == pisti::EvType::Capture && opp && !e.pisti) {
                    all[t.idx].why = names[(size_t)e.seat] + " masayı aldı (" + std::to_string(e.count) + " kart)";
                    t.idx = (size_t)-1;
                } else if ((e.type == pisti::EvType::TurnStart && e.seat == 0) || e.type == pisti::EvType::HandEnd) {
                    t.idx = (size_t)-1;
                }
            }
        }
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.idx == (size_t)-1; }),
                     tracks.end());
    }
    return all;
}

// batak
bool batakCard(batak::Bot& bot, const batak::Game& g, int seat, int card, Mistake& out) {
    if (g.legalCards(seat).size() <= 1) return false;
    const std::vector<batak::CardValue> vals = bot.evaluateCards(g, seat);
    double cost = 0, noise = 0;
    int best = -1;
    if (!cardCost(vals, card, cost, noise, best)) return false;
    out = Mistake();
    out.unit = "puan";
    out.topic = "kart";
    out.cost = cost;
    out.noise = noise;
    const std::string from = seat != 0 && g.controllerOf(seat) == 0 ? "ortağının elinden " : "";
    out.played = from + kart::cardAccusativeTR(card) + " attın";
    out.better = "Kurt " + kart::cardAccusativeTR(best) + " atardı";
    out.why = "Kurt'un hesabıyla bu elden beklenen puan farkın " + number(cost) + " düştü";
    out.notable = notableCost(cost, noise, BATAK_MIN);
    return true;
}

std::vector<Mistake> analyzeBatak(const ui::Settings& st, uint64_t seed, const std::vector<std::string>& lines,
                                  const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    batak::Game g(batakRules(st));
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[(size_t)s], s == 0);
    batak::Bot eval(batak::Level::Kurt, mix(seed, 0xBA7A));
    batak::Bot kurt(batak::Level::Kurt, mix(seed, 0xBA7B));
    g.startMatch(seed);
    g.drainEvents();
    std::vector<Mistake> all;
    struct Track {
        size_t idx;
        bool bid; // a bid: watch the hand's result; else the trick
    };
    std::vector<Track> tracks;
    for (const std::string& line : lines) {
        if (cancelled(cancel)) break;
        batak::LoggedAction a;
        if (!batak::LoggedAction::decode(trim(line), a)) break;
        const int cur = g.current();
        const bool mine = cur >= 0 && a.seat == cur && g.controllerOf(cur) == 0;
        Mistake m;
        bool judged = false;
        const int handNo = g.handIndex() + 1;
        if (mine && g.stage() == batak::Stage::Bidding && (a.kind == batak::LogKind::Bid || a.kind == batak::LogKind::Pass)) {
            const batak::Action k = kurt.next(g, cur);
            const bool iPass = a.kind == batak::LogKind::Pass, kPass = k.kind == batak::Action::Kind::Pass;
            if (iPass != kPass) {
                double decl = 0, pass = 0;
                const int bidV = iPass ? k.value : a.value;
                if (eval.evaluateBid(g, cur, bidV, decl, pass)) {
                    m.unit = "puan";
                    m.topic = "ihale";
                    const double actual = iPass ? pass : decl;
                    m.cost = std::max(0.0, std::max(decl, pass) - actual);
                    m.played = iPass ? "ihalede pas dedin" : "ihalede " + std::to_string(a.value) + " dedin";
                    m.better = kPass ? "Kurt pas derdi" : "Kurt " + std::to_string(k.value) + " derdi";
                    m.why = "Kurt'un hesabıyla bu elden beklenen puan farkın " + number(m.cost) + " düştü";
                    m.notable = notableCost(m.cost, 0.0, BATAK_BID_MIN) && (iPass ? decl > pass : pass > decl);
                    m.when = ordinal(handNo) + " el, ihale";
                    m.turn = -1;
                    judged = true;
                }
            }
        } else if (mine && g.stage() == batak::Stage::ChoosingTrump && a.kind == batak::LogKind::Trump) {
            double v[4];
            if (eval.evaluateTrumps(g, cur, v) && a.value >= 0 && a.value < 4) {
                const int best = (int)(std::max_element(v, v + 4) - v);
                m.unit = "puan";
                m.topic = "koz";
                m.cost = std::max(0.0, v[best] - v[a.value]);
                m.played = "koz olarak " + suitAcc(a.value) + " seçtin";
                m.better = "Kurt " + suitAcc(best) + " seçerdi";
                m.why = "Kurt'un hesabıyla bu elden beklenen puan farkın " + number(m.cost) + " düştü";
                m.notable = notableCost(m.cost, 0.0, BATAK_BID_MIN);
                m.when = ordinal(handNo) + " el, koz seçimi";
                m.turn = 0;
                judged = true;
            }
        } else if (mine && g.stage() == batak::Stage::Playing && a.kind == batak::LogKind::Play) {
            const int trickNo = g.trickNumber();
            if (batakCard(eval, g, cur, a.value, m)) {
                m.turn = trickNo;
                m.when = ordinal(handNo) + " el, " + ordinal(trickNo) + " tur";
                judged = true;
            }
        }
        if (judged && m.notable) {
            m.hand = handNo;
            all.push_back(m);
            if (a.kind == batak::LogKind::Bid || a.kind == batak::LogKind::Play)
                tracks.push_back({all.size() - 1, a.kind == batak::LogKind::Bid});
        }
        if (!g.replay(a)) break;
        for (const batak::GameEvent& e : g.drainEvents()) {
            for (Track& t : tracks) {
                if (t.idx == (size_t)-1) continue;
                if (!t.bid && e.type == batak::EvType::TrickWon) {
                    if (g.sideOf(e.seat) != g.sideOf(0)) all[t.idx].why = "eli " + names[(size_t)e.seat] + " aldı";
                    t.idx = (size_t)-1;
                } else if (t.bid && e.type == batak::EvType::HandEnd) {
                    const batak::HandResult& r = g.lastHandResult();
                    if (r.declarer >= 0 && g.sideOf(r.declarer) == g.sideOf(0))
                        for (const batak::SideResult& sr : r.sides)
                            if (sr.declarer && !sr.made)
                                all[t.idx].why = "ihaleyi aldın ve battın (" + std::to_string(sr.points) + ")";
                    t.idx = (size_t)-1;
                } else if (t.bid && e.type == batak::EvType::Redeal) {
                    t.idx = (size_t)-1;
                }
            }
        }
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.idx == (size_t)-1; }),
                     tracks.end());
    }
    return all;
}

// king
bool kingCard(king::Bot& bot, const king::Game& g, int seat, int card, Mistake& out) {
    if (g.legalCards(seat).size() <= 1) return false;
    const std::vector<king::CardValue> vals = bot.evaluateCards(g, seat);
    double cost = 0, noise = 0;
    int best = -1;
    if (!cardCost(vals, card, cost, noise, best)) return false;
    out = Mistake();
    out.unit = "puan";
    out.topic = "kart";
    out.cost = cost;
    out.noise = noise;
    out.played = kart::cardAccusativeTR(card) + " attın";
    out.better = "Kurt " + kart::cardAccusativeTR(best) + " atardı";
    out.why = "Kurt'un hesabıyla bu elden beklenen puanın " + number(cost) + " düştü";
    out.notable = notableCost(cost, noise, KING_MIN);
    return true;
}

std::vector<Mistake> analyzeKing(const ui::Settings& st, uint64_t seed, const std::vector<std::string>& lines,
                                 const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    king::Game g(kingRules(st));
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[(size_t)s], s == 0);
    king::Bot eval(king::BotLevel::Kurt, mix(seed, 0x4196));
    g.startMatch(seed);
    g.drainEvents();
    std::vector<Mistake> all;
    struct Track {
        size_t idx;
        int pts = 0;
    };
    std::vector<Track> tracks;
    for (const std::string& line : lines) {
        if (cancelled(cancel)) break;
        king::LoggedAction a;
        if (!king::LoggedAction::decode(trim(line), a)) break;
        Mistake m;
        bool judged = false;
        const int handNo = g.handIndex() + 1;
        if (a.seat == 0 && a.kind == king::LogKind::Choose && g.stage() == king::Stage::Choosing && g.chooser() == 0) {
            const std::vector<king::ContractValue> vals = eval.evaluateContracts(g, 0);
            const king::ContractValue* act = nullptr;
            const king::ContractValue* best = nullptr;
            for (const auto& v : vals) {
                if ((int)v.contract == a.value && (v.contract != king::Contract::Koz || v.trump == a.trump)) act = &v;
                if (!best || v.value > best->value + 1e-12) best = &v;
            }
            if (act && best && vals.size() > 1) {
                m.unit = "puan";
                m.topic = "oyun";
                m.cost = std::max(0.0, best->value - act->value);
                m.noise = act->se;
                m.played = king::contractLabelTR(act->contract, act->trump) + " seçtin";
                m.better = "Kurt " + king::contractLabelTR(best->contract, best->trump) + " seçerdi";
                m.why = "Kurt'un hesabıyla bu elden beklenen puanın " + number(m.cost) + " düştü";
                m.notable = notableCost(m.cost, m.noise, KING_CHOICE_MIN);
                m.turn = 0;
                m.when = ordinal(handNo) + " el, oyun seçimi";
                judged = true;
            }
        } else if (a.seat == 0 && a.kind == king::LogKind::Play && g.stage() == king::Stage::Playing && g.current() == 0) {
            const int trickNo = g.trickNumber() + 1;
            if (kingCard(eval, g, 0, a.value, m)) {
                m.turn = trickNo;
                m.when = ordinal(handNo) + " el, " + ordinal(trickNo) + " tur";
                judged = true;
            }
        }
        if (judged && m.notable) {
            m.hand = handNo;
            all.push_back(m);
            if (a.kind == king::LogKind::Play) tracks.push_back({all.size() - 1});
        }
        if (!g.replay(a)) break;
        for (const king::GameEvent& e : g.drainEvents()) {
            for (Track& t : tracks) {
                if (t.idx == (size_t)-1) continue;
                if (e.type == king::EvType::TrickWon) {
                    if (e.seat == 0 && e.amount < 0) all[t.idx].why = "eli sen aldın (" + std::to_string(e.amount) + ")";
                    else if (e.seat != 0 && e.amount > 0) all[t.idx].why = "eli " + names[(size_t)e.seat] + " aldı";
                    t.idx = (size_t)-1;
                } else if (e.type == king::EvType::HandEnd) {
                    t.idx = (size_t)-1;
                }
            }
        }
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.idx == (size_t)-1; }),
                     tracks.end());
    }
    return all;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------

const std::array<std::string, 4>& defaultNames() {
    static const std::array<std::string, 4> n{{"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}};
    return n;
}

std::vector<Mistake> analyzeMatch(ui::GameKind kind, const ui::Settings& rules, uint64_t seed,
                                  const std::vector<std::string>& actionLines, const std::array<std::string, 4>& names,
                                  int maxResults, const std::atomic<bool>* cancel) {
    std::vector<Mistake> all;
    switch (kind) {
    case ui::GameKind::Yuzbir:
    case ui::GameKind::YuzbirEsli:
    case ui::GameKind::Okey: all = analyzeOkey(kind, rules, seed, actionLines, names, cancel); break;
    case ui::GameKind::Tavla: all = analyzeTavla(rules, seed, actionLines, names, cancel); break;
    case ui::GameKind::Pisti: all = analyzePisti(rules, seed, actionLines, names, cancel); break;
    case ui::GameKind::Batak: all = analyzeBatak(rules, seed, actionLines, names, cancel); break;
    case ui::GameKind::King: all = analyzeKing(rules, seed, actionLines, names, cancel); break;
    default: break;
    }
    return pick(std::move(all), maxResults);
}

std::string describeMistake(const Mistake& m) {
    std::string s = m.when.empty() ? std::string() : m.when + ": ";
    s += m.played;
    if (!m.why.empty()) s += ", " + m.why;
    s += ". " + m.better;
    if (m.unit == "%") s += " (bitme şansın yaklaşık " + number(m.cost) + " puan düştü)";
    else s += " (yaklaşık " + number(m.cost) + " " + m.unit + " kayıp)";
    return s;
}

bool judgeOkeyDiscard(const okey::Game& g, int seat, int tile, Mistake& out, const std::array<std::string, 4>& names) {
    if (g.classic() || g.current() != seat || g.stage() != okey::TurnStage::Play) return false;
    OkeyJudge j(g.matchSeed() ^ 0x5EEDull);
    return j.discard(g, seat, tile, nullptr, names, out);
}

bool judgeClassicDiscard(const okey::Game& g, int seat, int tile, Mistake& out) {
    if (!g.classic() || g.current() != seat || g.stage() != okey::TurnStage::Play) return false;
    OkeyJudge j(g.matchSeed() ^ 0x5EEDull);
    return j.classic(g, seat, tile, out);
}

bool judgeTavlaPlay(const tavla::Position& before, int player, int d1, int d2, const tavla::Position& after, int cube,
                    Mistake& out) {
    return tavlaPlay(before, player, d1, d2, after, cube, out);
}

bool judgePistiCard(const pisti::Game& g, int seat, int card, Mistake& out) {
    if (g.stage() != pisti::Stage::Playing || g.current() != seat) return false;
    pisti::Bot bot(pisti::BotLevel::Kurt, 1);
    return pistiCard(bot, g, seat, card, out);
}

bool judgeBatakCard(const batak::Game& g, int seat, int card, Mistake& out) {
    if (g.stage() != batak::Stage::Playing || g.current() != seat) return false;
    batak::Bot bot(batak::Level::Kurt, 1);
    return batakCard(bot, g, seat, card, out);
}

bool judgeKingCard(const king::Game& g, int seat, int card, Mistake& out) {
    if (g.stage() != king::Stage::Playing || g.current() != seat) return false;
    king::Bot bot(king::BotLevel::Kurt, 1);
    return kingCard(bot, g, seat, card, out);
}

} // namespace analysis
