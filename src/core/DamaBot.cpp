// Dama bots: alpha-beta over a material + position evaluation (see DamaBot.h).
#include "core/DamaBot.h"

#include "core/Rng.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dama {

namespace {

struct Weights {
    int man = 100, king = 300;
    std::array<int, 8> adv{{0, 0, 3, 7, 12, 19, 30, 0}}; // a man's advance bonus by rows come (1 = start row)
    int centre = 3;          // a man on the files c..f
    int guarded = 3;         // a man with a friend right behind it or beside it
    int homeRow = 5;         // a man still on its first row (the guard against crowning) while the other side has men
    int kingLine = 2;        // per empty square a dama sees along its four lines
    int tradeWhenAhead = 30; // x material lead / total material: trades are welcome when ahead
};

Weights styled(const okey::BotStyle& st) {
    Weights w;
    const float b = st.boldness;
    if (b == 0.f) return w;
    for (int& a : w.adv) a = (int)std::lround(a * (1.0 + 0.2 * b));
    w.homeRow = (int)std::lround(w.homeRow * (1.0 - 0.6 * b));
    w.guarded = (int)std::lround(w.guarded * (1.0 - 0.3 * b));
    w.tradeWhenAhead = (int)std::lround(w.tradeWhenAhead * (1.0 + 0.5 * b));
    return w;
}

constexpr int DR[4] = {+1, 0, -1, 0};
constexpr int DC[4] = {0, +1, 0, -1};

int evaluate(const Board& b, int p, const Weights& w) {
    int score[2] = {0, 0}, mat[2] = {0, 0}, men[2] = {0, 0};
    for (int s = 0; s < kSquares; ++s) {
        const int8_t v = b.c[(size_t)s];
        if (!v) continue;
        const int q = v > 0 ? 0 : 1;
        const int r = rowOf(s), c = colOf(s);
        if (v == 1 || v == -1) {
            ++men[q];
            mat[q] += w.man;
            int sc = w.man + w.adv[(size_t)advance(q, r)];
            if (c >= 2 && c <= 5) sc += w.centre;
            const int back = r - forwardOf(q);
            if ((back >= 0 && back < kSize && b.owner(sq(back, c)) == q) || (c > 0 && b.owner(sq(r, c - 1)) == q) ||
                (c < kSize - 1 && b.owner(sq(r, c + 1)) == q))
                sc += w.guarded;
            if (advance(q, r) == 1) sc += w.homeRow;
            score[q] += sc;
        } else {
            mat[q] += w.king;
            int sc = w.king;
            for (int d = 0; d < 4; ++d) {
                int rr = r + DR[d], cc = c + DC[d];
                while (onBoard(rr, cc) && b.empty(sq(rr, cc))) {
                    sc += w.kingLine;
                    rr += DR[d];
                    cc += DC[d];
                }
            }
            score[q] += sc;
        }
    }
    // the home row only matters while the other side still has men to crown
    for (int q = 0; q < 2; ++q)
        if (men[1 - q] == 0) {
            for (int c = 0; c < kSize; ++c) {
                const int s = sq(q == 0 ? 1 : 6, c);
                if (b.owner(s) == q && !b.king(s)) score[q] -= w.homeRow;
            }
        }
    int e = score[p] - score[1 - p];
    const int total = mat[0] + mat[1];
    if (total > 0) e += w.tradeWhenAhead * (mat[p] - mat[1 - p]) * 4 / total * (total < 1600 ? 2 : 1);
    return e;
}

struct TTEntry {
    uint64_t key = 0;
    int score = 0;
    int8_t depth = -1;
    int8_t flag = 0; // 0 exact, 1 lower bound, 2 upper bound
    int8_t best = -1;
};

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

class Searcher {
public:
    static constexpr int kMaxPly = 64; // (search() stops at ply 60; the buffers never move under a reference)
    explicit Searcher(int ttBits = 18) : tt_((size_t)1 << ttBits), mask_(((uint64_t)1 << ttBits) - 1), bufs_(kMaxPly + 2) {}
    Weights w;
    long nodes = 0, nodeCap = 1000000;
    double deadline = 0.0; // ms (0: none)
    bool aborted = false;
    bool lmr = true;             // late move reductions
    bool loneKingWins = false;   // dama::Rules::loneKingWins
    std::vector<uint64_t> stack; // the game's positions since the last irreversible move, then the search path

    void newSearch() {
        nodes = 0;
        aborted = false;
        for (auto& h : hist_)
            for (int& v : h) v /= 4;
    }
    void clearTT() { std::fill(tt_.begin(), tt_.end(), TTEntry()); }

    int search(Board& b, int p, int depth, int alpha, int beta, int ply) {
        if (aborted) return 0;
        if (++nodes >= nodeCap || ((nodes & 1023) == 0 && deadline > 0.0 && nowMs() > deadline)) {
            aborted = true;
            return 0;
        }
        const uint64_t key = b.hash(p);
        // a repetition on the path (or of the game's positions): a draw
        if (ply > 0) {
            for (int i = (int)stack.size() - 2; i >= 0 && i >= (int)stack.size() - 40; i -= 2)
                if (stack[(size_t)i] == key) return 0;
        }
        std::vector<CMove>& moves = buf(ply);
        generateCompact(b, p, moves);
        if (moves.empty()) return -WIN_SCORE + ply;
        const bool captures = moves[0].ncap > 0;
        if (!captures && b.pieces(0) == 1 && b.pieces(1) == 1) { // one piece each: a draw (or the lone dama wins)
            if (loneKingWins && b.kings(0) + b.kings(1) == 1) return (b.kings(p) == 1 ? 1 : -1) * (WIN_SCORE - ply - 1);
            return 0;
        }
        if (depth <= 0 && (!captures || ply >= 48)) return evaluate(b, p, w);
        if (ply >= 60) return evaluate(b, p, w);
        // the transposition table
        TTEntry& te = tt_[(size_t)(key & mask_)];
        int ttBest = -1;
        if (te.key == key) {
            ttBest = te.best;
            if (te.depth >= depth) {
                const int s = fromTT(te.score, ply);
                if (te.flag == 0) return s;
                if (te.flag == 1 && s >= beta) return s;
                if (te.flag == 2 && s <= alpha) return s;
            }
        }
        // order: the table's move, then the most captures, crowning, the history heuristic
        const int n = (int)moves.size();
        int order[128];
        int keyv[128];
        const int cnt = std::min(n, 128);
        for (int i = 0; i < cnt; ++i) {
            const CMove& m = moves[(size_t)i];
            order[i] = i;
            const int mk = m.from * 64 + m.to();
            const int killer = ply <= kMaxPly && (killers_[(size_t)ply][0] == mk || killers_[(size_t)ply][1] == mk) ? 400000 : 0;
            keyv[i] = (i == ttBest ? 1 << 28 : 0) + m.ncap * 1000000 + (m.promotes ? 500000 : 0) + killer +
                      std::min(hist_[(size_t)m.from][(size_t)m.to()], 300000);
        }
        std::sort(order, order + cnt, [&](int a, int c) { return keyv[a] > keyv[c] || (keyv[a] == keyv[c] && a < c); });
        // a forced move (one reply) does not cost a ply near the root; captures past the horizon stay at depth 0
        const int childDepth = depth <= 0 ? 0 : (n == 1 && ply < 30 ? depth : depth - 1);
        const int a0 = alpha;
        int best = -WIN_SCORE * 2, bestIdx = order[0];
        for (int k = 0; k < cnt; ++k) {
            const int i = order[k];
            const CMove m = moves[(size_t)i]; // (the buffer is reused deeper)
            Board nb = b;
            applyCompact(nb, p, m);
            stack.push_back(key);
            int s;
            if (k == 0) {
                s = -search(nb, 1 - p, childDepth, -beta, -alpha, ply + 1);
            } else { // principal variation search: a null window first (late quiet moves one ply shallower)
                const bool reduce = lmr && k >= 3 && depth >= 3 && m.ncap == 0 && !m.promotes;
                s = -search(nb, 1 - p, childDepth - (reduce ? 1 : 0), -alpha - 1, -alpha, ply + 1);
                if (reduce && !aborted && s > alpha) s = -search(nb, 1 - p, childDepth, -alpha - 1, -alpha, ply + 1);
                if (!aborted && s > alpha && s < beta) s = -search(nb, 1 - p, childDepth, -beta, -alpha, ply + 1);
            }
            stack.pop_back();
            if (aborted) return 0;
            if (s > best) {
                best = s;
                bestIdx = i;
            }
            if (s > alpha) alpha = s;
            if (alpha >= beta) {
                if (m.ncap == 0) {
                    hist_[(size_t)m.from][(size_t)m.to()] += depth * depth + 1;
                    const int mk = m.from * 64 + m.to();
                    auto& kl = killers_[(size_t)std::min(ply, kMaxPly)];
                    if (kl[0] != mk) {
                        kl[1] = kl[0];
                        kl[0] = mk;
                    }
                }
                break;
            }
        }
        te.key = key;
        te.score = toTT(best, ply);
        te.depth = (int8_t)std::clamp(depth, -1, 120);
        te.flag = best <= a0 ? 2 : best >= beta ? 1 : 0;
        te.best = (int8_t)bestIdx;
        return best;
    }

private:
    std::vector<TTEntry> tt_;
    uint64_t mask_;
    std::vector<std::vector<CMove>> bufs_;
    int hist_[kSquares][kSquares] = {};
    std::array<std::array<int, 2>, kMaxPly + 1> killers_{};
    std::vector<CMove>& buf(int ply) { return bufs_[(size_t)std::min(ply, kMaxPly)]; }
    static int toTT(int s, int ply) { return s > WIN_SCORE / 2 ? s + ply : s < -WIN_SCORE / 2 ? s - ply : s; }
    static int fromTT(int s, int ply) { return s > WIN_SCORE / 2 ? s - ply : s < -WIN_SCORE / 2 ? s + ply : s; }
};

struct Params {
    int maxDepth;
    long nodeCap;
    double timeMs;
    int noise;        // +- per root move (Acemi)
    float looseness;  // chance to pick one of the 3 best instead of the best (Acemi)
};

Params paramsFor(BotLevel l) {
    switch (l) {
    case BotLevel::Easy: return {2, 20000, 0.0, 45, 0.2f};
    case BotLevel::Normal: return {6, 40000, 0.0, 0, 0.f};
    default: return {24, 400000, 0.0, 0, 0.f};                // (a node budget, no clock: the same game on any machine)
    }
}

} // namespace

int botEvaluate(const Board& b, int p) { return evaluate(b, p, Weights()); }

std::vector<int> botScoreMoves(const Board& b, int p, const std::vector<Move>& moves, int depth, long nodeCap) {
    Searcher s(17);
    std::vector<int> out;
    out.reserve(moves.size());
    std::vector<CMove> legal;
    generateCompact(b, p, legal);
    for (const Move& mv : moves) {
        const CMove* cm = nullptr;
        for (const CMove& l : legal)
            if (l.from == mv.from && l.n == (int)mv.path.size() && std::equal(mv.path.begin(), mv.path.end(), l.path.begin()))
                cm = &l;
        if (!cm) {
            out.push_back(-WIN_SCORE);
            continue;
        }
        s.newSearch();
        s.nodeCap = nodeCap;
        Board nb = b;
        applyCompact(nb, p, *cm);
        s.stack.assign(1, b.hash(p));
        int sc = 0;
        // deepen while the budget lasts: the deepest finished score counts
        for (int d = 1; d <= depth; ++d) {
            s.aborted = false;
            s.nodes = 0;
            const int v = -s.search(nb, 1 - p, d - 1, -WIN_SCORE * 2, WIN_SCORE * 2, 1);
            if (s.aborted) break;
            sc = v;
        }
        out.push_back(sc);
    }
    return out;
}

struct Bot::Impl {
    BotLevel level = BotLevel::Normal;
    okey::BotStyle style;
    okey::Rng rng;
    Searcher searcher{18};
    SearchInfo info;
};

Bot::Bot(BotLevel level, uint64_t seed) : impl_(std::make_unique<Impl>()) {
    impl_->level = level;
    impl_->rng.reseed(seed ^ 0xD4A3B07ull);
}
Bot::~Bot() = default;
Bot::Bot(Bot&&) noexcept = default;
Bot& Bot::operator=(Bot&&) noexcept = default;

void Bot::setLevel(BotLevel level) { impl_->level = level; }
BotLevel Bot::level() const { return impl_->level; }
void Bot::setStyle(const okey::BotStyle& s) { impl_->style = s; }
const SearchInfo& Bot::lastSearch() const { return impl_->info; }

Move Bot::choose(const Game& g, int p) {
    Impl& I = *impl_;
    const std::vector<Move>& legal = g.legalMoves();
    I.info = SearchInfo();
    if (legal.empty()) return Move();
    if (legal.size() == 1) return legal[0];
    const Params pr = paramsFor(I.level);
    Searcher& S = I.searcher;
    S.w = styled(I.style);
    S.loneKingWins = g.rules().loneKingWins;
    S.newSearch();
    S.nodeCap = pr.nodeCap;
    const double t0 = nowMs();
    S.deadline = pr.timeMs > 0.0 ? t0 + pr.timeMs : 0.0;
    const Board root = g.board();
    std::vector<CMove> moves;
    generateCompact(root, p, moves);
    const int n = (int)moves.size();
    std::vector<int> scores((size_t)n, 0), order((size_t)n);
    for (int i = 0; i < n; ++i) order[(size_t)i] = i;
    int bestIdx = 0, bestScore = 0;
    std::vector<int> done = scores;
    for (int depth = 1; depth <= pr.maxDepth; ++depth) {
        std::vector<int> cur((size_t)n, -WIN_SCORE * 2);
        int alpha = -WIN_SCORE * 2;
        bool aborted = false;
        for (int k = 0; k < n; ++k) {
            const int i = order[(size_t)k];
            Board nb = root;
            applyCompact(nb, p, moves[(size_t)i]);
            S.stack = g.history();
            const bool irreversible = moves[(size_t)i].ncap > 0 || !root.king(moves[(size_t)i].from);
            if (irreversible) S.stack.clear();
            // every root move gets an exact score for the noisy levels; the others search the rest with a null window
            int s;
            if (pr.noise > 0 || k == 0) {
                s = -S.search(nb, 1 - p, depth - 1, -WIN_SCORE * 2, WIN_SCORE * 2, 1);
            } else {
                s = -S.search(nb, 1 - p, depth - 1, -alpha - 1, -alpha, 1);
                if (!S.aborted && s > alpha) s = -S.search(nb, 1 - p, depth - 1, -WIN_SCORE * 2, -alpha, 1);
            }
            if (S.aborted) {
                aborted = true;
                break;
            }
            cur[(size_t)i] = s;
            if (s > alpha) alpha = s;
        }
        if (aborted) break;
        done = cur;
        I.info.depth = depth;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cur[(size_t)a] > cur[(size_t)b]; });
        bestIdx = order[0];
        bestScore = cur[(size_t)bestIdx];
        if (bestScore > WIN_SCORE / 2 || bestScore < -WIN_SCORE / 2) break; // a forced result found
    }
    if (I.info.depth == 0) { // (not even one ply finished: the first legal move)
        bestIdx = 0;
    } else if (pr.noise > 0) {
        std::vector<int> noisy = done;
        for (int& v : noisy) v += I.rng.range(-pr.noise, pr.noise);
        std::vector<int> idx((size_t)n);
        for (int i = 0; i < n; ++i) idx[(size_t)i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return noisy[(size_t)a] > noisy[(size_t)b]; });
        bestIdx = idx[0];
        if (I.rng.chance(pr.looseness)) bestIdx = idx[(size_t)I.rng.range(std::min(n, 3))];
        bestScore = done[(size_t)bestIdx];
    }
    I.info.nodes = S.nodes;
    I.info.ms = nowMs() - t0;
    I.info.score = bestScore;
    const Move chosen = toMove(moves[(size_t)bestIdx]);
    for (const Move& m : legal)
        if (m.sameAs(chosen)) return m;
    return legal[0];
}

} // namespace dama
