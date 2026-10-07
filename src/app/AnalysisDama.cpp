// Hata analizi for Dama (see Analysis.h): the match is replayed move by move; at each of the player's moves that had a
// choice a Kurt-like search (dama::botScoreMoves, a fixed depth) scores every legal move, and the move made is judged
// against the best one in pieces ("taş": a man = 1, a dama about 3). The opponent's next move tells what it cost ("Kel
// Mahmut ardından 2 taşını aldı").
#include "app/Analysis.h"

#include "core/Dama.h"
#include "core/DamaBot.h"
#include "ui/Screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace analysis {

namespace {

constexpr double DAMA_MIN = 0.8;   // pieces: what is worth telling
constexpr int DAMA_DEPTH = 4;      // the search per candidate move (plies after it, captures searched on)
constexpr long DAMA_NODES = 25000; // ... and its node budget

std::string trimLine(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string pieces(double v) {
    char buf[32];
    if (v >= 9.95) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.1f", v);
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

// "c3-c4 oynadın", "d4xd6xf6 ile 2 taş aldın"
std::string playedText(const dama::Move& m) {
    if (m.isCapture()) return dama::moveNotation(m) + " ile " + std::to_string(m.captured.size()) + " taş aldın";
    return dama::moveNotation(m) + (m.promotes ? " ile dama çıktın" : " oynadın");
}
std::string betterText(const dama::Move& m) {
    if (m.isCapture()) return "Kurt " + dama::moveNotation(m) + " ile " + std::to_string(m.captured.size()) + " taş alırdı";
    return "Kurt " + dama::moveNotation(m) + (m.promotes ? " ile dama çıkardı" : " oynardı");
}

// A score from the search to pieces, forced results (a won or lost game) counted as 6 pieces.
double toPieces(int score) {
    if (score > dama::WIN_SCORE / 2) return 6.0;
    if (score < -dama::WIN_SCORE / 2) return -6.0;
    return score / 100.0;
}

} // namespace

bool judgeDamaMove(const dama::Board& before, int player, const dama::Move& played, Mistake& out) {
    const std::vector<dama::Move> legal = dama::generateMoves(before, player);
    if (legal.size() < 2) return false; // forced
    int playedIdx = -1;
    for (size_t i = 0; i < legal.size(); ++i)
        if (legal[i].sameAs(played)) playedIdx = (int)i;
    if (playedIdx < 0) return false;
    const std::vector<int> sc = dama::botScoreMoves(before, player, legal, DAMA_DEPTH, DAMA_NODES);
    int best = 0;
    for (size_t i = 1; i < sc.size(); ++i)
        if (sc[i] > sc[(size_t)best]) best = (int)i;
    out = Mistake();
    out.topic = "hamle";
    out.unit = "taş";
    out.played = playedText(legal[(size_t)playedIdx]);
    out.better = betterText(legal[(size_t)best]);
    out.cost = std::max(0.0, toPieces(sc[(size_t)best]) - toPieces(sc[(size_t)playedIdx]));
    out.noise = 0.0; // a deterministic search
    out.notable = best != playedIdx && out.cost >= DAMA_MIN;
    if (sc[(size_t)best] > dama::WIN_SCORE / 2 && sc[(size_t)playedIdx] <= dama::WIN_SCORE / 2) out.why = "kazanan yolu kaçırdın";
    else if (sc[(size_t)playedIdx] < -dama::WIN_SCORE / 2 && sc[(size_t)best] >= -dama::WIN_SCORE / 2) out.why = "bu hamle oyunu kaybettirdi";
    else if (out.cost >= DAMA_MIN) out.why = "yaklaşık " + pieces(out.cost) + " taş verdin";
    return true;
}

std::vector<Mistake> analyzeDama(const ui::Settings& st, uint64_t seed, const std::vector<std::string>& lines,
                                 const std::array<std::string, 4>& names, const std::atomic<bool>* cancel) {
    std::vector<Mistake> all;
    dama::Rules r;
    r.winsNeeded = std::clamp(st.damaWins, 1, 9); // (as DamaTable::startMatch)
    dama::Game g(r);
    const int oppSeat = ui::twoPlayerOpponent(st, ui::GameKind::Dama); // Rakip
    const std::string opp = names[(size_t)oppSeat];
    g.setPlayer(0, names[0], true);
    g.setPlayer(1, opp, false);
    g.startMatch(seed);
    int myMoves = 0, lastGame = 0;
    size_t tracking = (size_t)-1; // the mistake whose outcome the opponent's next move tells
    for (const std::string& raw : lines) {
        if (cancel && cancel->load()) break;
        dama::LoggedAction a;
        if (!dama::LoggedAction::decode(trimLine(raw), a)) break;
        if (g.gameIndex() != lastGame) {
            lastGame = g.gameIndex();
            myMoves = 0;
        }
        if (a.kind == dama::ActKind::Move && a.player == 0 && g.stage() == dama::Stage::Playing && g.current() == 0) {
            ++myMoves;
            dama::Move played;
            played.from = a.from;
            played.path = a.path;
            Mistake m;
            if (judgeDamaMove(g.board(), 0, played, m) && m.notable) {
                m.hand = g.gameIndex() + 1;
                m.turn = myMoves;
                m.when = std::to_string(m.hand) + ". oyun, " + std::to_string(myMoves) + ". hamle";
                all.push_back(m);
                tracking = all.size() - 1;
            }
        }
        const int mover = a.player;
        if (!g.replay(a)) break;
        for (const dama::GameEvent& e : g.drainEvents()) {
            if (tracking == (size_t)-1) continue;
            if (e.type == dama::EvType::Move && e.player == 1 && mover == 1) {
                if (!e.captured.empty())
                    all[tracking].why = opp + " ardından " + std::to_string(e.captured.size()) + " taşını aldı";
                else if (e.promotes)
                    all[tracking].why = opp + " ardından dama çıktı";
                tracking = (size_t)-1;
            } else if (e.type == dama::EvType::Promote && e.player == 1) {
                all[tracking].why = opp + " ardından dama çıktı";
                tracking = (size_t)-1;
            } else if (e.type == dama::EvType::GameEnd) {
                tracking = (size_t)-1;
            }
        }
    }
    return all;
}

} // namespace analysis
