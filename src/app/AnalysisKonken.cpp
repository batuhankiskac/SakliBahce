// Hata analizi for Konken (see Analysis.h): the match is replayed action by action; at the player's decisions a Kurt
// judges what was done, in points of the hand (lower is better):
//   atış   every discard with a choice: Kurt's rollouts give the expected points of the hand for each card that could
//          go (konken::Bot::evaluateDiscards); the cost is the card thrown against Kurt's best. What came next is told
//          ("Hacı Rıza onu yerden aldı").
//   açış   a turn ended with a discard although an opening was there (Kurt would have opened): an unopened player
//          writes 100, so the cost is about what opening would have saved (told once a hand, at its first turn).
//   çekiş  the stock drawn while the discard on top would have opened the hand at once.
#include "app/Analysis.h"

#include "core/Konken.h"
#include "core/KonkenBot.h"
#include "ui/Screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace analysis {

namespace {

constexpr double KONKEN_MIN = 4.0;       // points: a discard worth telling
constexpr double KONKEN_OPEN_MIN = 8.0;  // ... a missed opening / draw
constexpr double NOISE_Z_K = 2.0;        // a cost must stand this many standard errors clear of the noise
constexpr double OPEN_SHARE = 0.5;       // how much of an opening's saving is counted (the hand may end anyway later)

std::string trimLine(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

uint64_t mixSeed(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

konken::Rules rulesOf(const ui::Settings& st) {
    konken::Rules r;
    r.openMin = std::clamp(st.konkenOpen, 30, 120);
    r.limit = std::clamp(st.konkenLimit, 51, 501);
    r.lastStanding = st.konkenLastStanding; // Konken bitiş: the burned leave (the table shrinks)
    return r;
}

// The points the player writes if the hand ended now with an opening laid (what is left in hand).
int restAfterOpening(const konken::Game& g, const std::vector<std::vector<int>>& melds) {
    std::vector<int> h = g.hand(0);
    for (const auto& m : melds)
        for (int c : m) h.erase(std::remove(h.begin(), h.end(), c), h.end());
    int p = 0;
    for (int c : h) p += konken::handPoints(c, g.rules().jokerPoints);
    return p;
}

} // namespace

bool judgeKonkenDiscard(const konken::Game& g, int seat, int card, Mistake& out) {
    if (g.stage() != konken::Stage::Play || g.current() != seat || g.takenCard() >= 0) return false;
    konken::Bot eval(konken::BotLevel::Kurt, mixSeed(g.matchSeed(), 0x6B6E));
    const std::vector<konken::DiscardValue> vals = eval.evaluateDiscards(g, seat);
    if (vals.size() < 2) return false;
    const konken::DiscardValue* act = nullptr;
    const konken::DiscardValue* best = nullptr;
    for (const konken::DiscardValue& v : vals) {
        const bool same = v.card == card || (!konken::isJoker(card) && !konken::isJoker(v.card) && konken::faceOf(v.card) == konken::faceOf(card));
        if (same) act = &v;
        if (!best || v.value < best->value - 1e-12) best = &v;
    }
    if (!act || !best) return false;
    out = Mistake();
    out.unit = "puan";
    out.topic = "atış";
    out.cost = std::max(0.0, act->value - best->value);
    out.noise = act->se;
    out.played = konken::cardAccusativeTR(card) + " attın";
    out.better = "Kurt " + konken::cardAccusativeTR(best->card) + " atardı";
    out.why = "Kurt'un hesabıyla bu elden beklenen cezan " + num(out.cost) + " arttı";
    out.notable = out.cost >= KONKEN_MIN && out.cost > NOISE_Z_K * out.noise;
    return true;
}

std::vector<Mistake> analyzeKonken(const ui::Settings& rules, uint64_t seed, const std::vector<std::string>& lines,
                                   const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    konken::Game g(rulesOf(rules));
    for (int s = 0; s < 4; ++s) g.setPlayer(s, names[(size_t)s], s == 0);
    konken::Bot eval(konken::BotLevel::Kurt, mixSeed(seed, 0x6B6E));
    g.startMatch(seed);
    g.drainEvents();
    std::vector<Mistake> all;
    std::vector<konken::LoggedAction> acts;
    for (const std::string& line : lines) {
        konken::LoggedAction a;
        if (!konken::LoggedAction::decode(trimLine(line), a)) break;
        acts.push_back(a);
    }
    int openToldHand = -1;
    for (size_t i = 0; i < acts.size(); ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        const konken::LoggedAction& a = acts[i];
        const int handNo = g.handIndex() + 1, tur = g.turn() / std::max(1, g.activeCount()) + 1; // (a shrinking table)
        const std::string when = std::to_string(handNo) + ". el, " + std::to_string(tur) + ". tur";
        Mistake m;
        bool judged = false;
        if (a.seat == 0 && g.current() == 0 && g.active(0)) { // (burned: nothing more of the player's to judge)
            // a draw from the stock while the discard on top would have opened at once
            if (a.kind == konken::LogKind::Draw && g.stage() == konken::Stage::Draw && !g.opened(0) && !g.mustDrawStock() &&
                g.discardTop() >= 0) {
                std::vector<int> h = g.hand(0);
                h.push_back(g.discardTop());
                const konken::Partition P = konken::bestPartition(h, konken::Goal::Value, g.discardTop());
                if (!P.melds.empty() && P.value >= g.rules().openMin &&
                    konken::bestPartition(g.hand(0), konken::Goal::Value).value < g.rules().openMin) {
                    m.unit = "puan";
                    m.topic = "çekiş";
                    m.cost = OPEN_SHARE * std::max(0, g.rules().unopenedPoints - P.restPoints);
                    m.played = "Desteden çektin";
                    m.better = "Kurt yerdeki " + konken::cardAccusativeTR(g.discardTop()) + " alıp açardı";
                    m.why = "o kâğıtla hemen " + std::to_string(P.value) + " açılıyordu";
                    m.notable = m.cost >= KONKEN_OPEN_MIN;
                    judged = true;
                }
            }
            if (a.kind == konken::LogKind::Discard && g.stage() == konken::Stage::Play && g.takenCard() < 0) {
                std::vector<std::vector<int>> melds;
                if (!g.opened(0) && openToldHand != handNo && eval.openingNow(g, 0, melds)) {
                    // the turn ends without the opening Kurt would have laid
                    const int rest = restAfterOpening(g, melds);
                    m.unit = "puan";
                    m.topic = "açış";
                    m.cost = OPEN_SHARE * std::max(0, g.rules().unopenedPoints - rest);
                    int v = 0;
                    for (const auto& mv : melds) {
                        konken::Meld md;
                        if (konken::makeMeld(mv, md)) v += md.value();
                    }
                    m.played = "Açabilecekken açmadın";
                    m.better = "Kurt " + std::to_string(v) + " ile açardı";
                    m.why = "açmayan " + std::to_string(g.rules().unopenedPoints) + " yazar";
                    m.notable = m.cost >= KONKEN_OPEN_MIN;
                    judged = true;
                    openToldHand = handNo;
                } else if (judgeKonkenDiscard(g, 0, a.card, m) && m.cost > 0.0) {
                    judged = true;
                    // what happened next: the next player took it up
                    const int nx = g.nextActive(0); // (the next one still at the table)
                    if (i + 1 < acts.size() && acts[i + 1].kind == konken::LogKind::Take && acts[i + 1].seat == nx)
                        m.why = names[(size_t)nx] + " onu yerden aldı";
                }
            }
        }
        if (judged) {
            m.hand = handNo;
            m.turn = tur;
            m.when = when;
            all.push_back(m);
        }
        if (!g.replay(a)) break;
        g.drainEvents();
    }
    return all;
}

} // namespace analysis
