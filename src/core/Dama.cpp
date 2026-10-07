// Dama (Türk daması) engine: see Dama.h and docs/kurallar_dama.md.
#include "core/Dama.h"

#include <algorithm>
#include <sstream>

namespace dama {

namespace {

// Directions: 0 up (+1 row, toward player 1), 1 right (+1 col), 2 down, 3 left. Opposite = (d + 2) % 4.
constexpr int DR[4] = {+1, 0, -1, 0};
constexpr int DC[4] = {0, +1, 0, -1};
inline int backDir(int p) { return p == 0 ? 2 : 0; } // a man's backward direction (never allowed)

struct Zobrist {
    uint64_t piece[kSquares][4];
    uint64_t side;
    Zobrist() {
        uint64_t s = 0xDA3A5EEDull;
        auto next = [&]() {
            uint64_t z = (s += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        };
        for (auto& sqr : piece)
            for (uint64_t& v : sqr) v = next();
        side = next();
    }
};
const Zobrist& zobrist() {
    static const Zobrist z;
    return z;
}
inline int zIndex(int8_t v) { return v == 1 ? 0 : v == 2 ? 1 : v == -1 ? 2 : 3; }

// The capture search: walks every jump sequence on the board in place (pieces removed one by one as they are jumped,
// no 180° turn between two jumps) and keeps the longest ones.
struct CaptureGen {
    Board& b;
    int p;
    std::vector<CMove>& out;
    int best = 0;
    CMove cur;

    void dfs(int s, bool king, int lastDir) {
        bool any = false;
        const int r0 = rowOf(s), c0 = colOf(s);
        for (int d = 0; d < 4; ++d) {
            if (lastDir >= 0 && d == (lastDir + 2) % 4) continue; // the 180° rule
            if (!king && d == backDir(p)) continue;               // a man never goes back
            int r = r0 + DR[d], c = c0 + DC[d];
            if (!king) {
                if (!onBoard(r, c)) continue;
                const int e = sq(r, c);
                if (b.owner(e) != 1 - p) continue;
                const int r2 = r + DR[d], c2 = c + DC[d];
                if (!onBoard(r2, c2) || !b.empty(sq(r2, c2))) continue;
                jump(s, e, sq(r2, c2), king, d);
                any = true;
            } else {
                while (onBoard(r, c) && b.empty(sq(r, c))) {
                    r += DR[d];
                    c += DC[d];
                }
                if (!onBoard(r, c) || b.owner(sq(r, c)) != 1 - p) continue;
                const int e = sq(r, c);
                r += DR[d];
                c += DC[d];
                while (onBoard(r, c) && b.empty(sq(r, c))) {
                    jump(s, e, sq(r, c), king, d);
                    any = true;
                    r += DR[d];
                    c += DC[d];
                }
            }
        }
        if (!any && cur.ncap > 0) record(s, king);
    }
    void jump(int s, int e, int l, bool king, int d) {
        const int8_t piece = b.c[(size_t)s], victim = b.c[(size_t)e];
        b.c[(size_t)s] = 0;
        b.c[(size_t)e] = 0;
        b.c[(size_t)l] = piece;
        cur.path[(size_t)cur.n++] = (int8_t)l;
        cur.cap[(size_t)cur.ncap++] = (int8_t)e;
        dfs(l, king, d);
        --cur.n;
        --cur.ncap;
        b.c[(size_t)l] = 0;
        b.c[(size_t)e] = victim;
        b.c[(size_t)s] = piece;
    }
    void record(int last, bool king) {
        if (cur.ncap < best) return;
        if (cur.ncap > best) {
            out.clear();
            best = cur.ncap;
        }
        CMove m = cur;
        m.promotes = !king && rowOf(last) == promotionRow(p); // a man is crowned only where its capture ends
        out.push_back(m);
    }
};

} // namespace

// "Kel Mahmut'un", "Hacı Rıza'nın", "Emekli Nuri'nin": the genitive by the last vowel (vowel harmony).
std::string genitive(const std::string& n) {
    char last = 0;     // 'a' (a, ı), 'u' (o, u), 'e' (e, i), 'o' (ö, ü)
    bool endsVowel = false;
    for (size_t i = 0; i < n.size(); ++i) {
        const unsigned char ch = (unsigned char)n[i];
        char v = 0;
        if (ch == 'a' || ch == 'A') v = 'a';
        else if (ch == 'o' || ch == 'O' || ch == 'u' || ch == 'U') v = 'u';
        else if (ch == 'e' || ch == 'E' || ch == 'i') v = 'e';
        else if (ch == 0xC4 && i + 1 < n.size() && ((unsigned char)n[i + 1] == 0xB1)) v = 'a';            // ı
        else if (ch == 0xC4 && i + 1 < n.size() && ((unsigned char)n[i + 1] == 0xB0)) v = 'e';            // İ
        else if (ch == 0xC3 && i + 1 < n.size() && ((unsigned char)n[i + 1] == 0xB6 || (unsigned char)n[i + 1] == 0x96 ||
                                                     (unsigned char)n[i + 1] == 0xBC || (unsigned char)n[i + 1] == 0x9C))
            v = 'o';                                                                                       // ö ü
        if (v) last = v;
        if ((ch & 0xC0) != 0x80) endsVowel = v != 0;
    }
    const char* suf = last == 'u' ? "un" : last == 'e' ? "in" : last == 'o' ? "\xC3\xBCn" : "\xC4\xB1n";
    return n + "'" + (endsVowel ? "n" : "") + suf;
}

// ---------------------------------------------------------------- board

Board Board::initial() {
    Board b;
    for (int c = 0; c < kSize; ++c) {
        b.c[(size_t)sq(1, c)] = b.c[(size_t)sq(2, c)] = 1;
        b.c[(size_t)sq(5, c)] = b.c[(size_t)sq(6, c)] = -1;
    }
    return b;
}

int Board::pieces(int p) const {
    int n = 0;
    for (int8_t v : c) n += (p == 0 ? v > 0 : v < 0) ? 1 : 0;
    return n;
}

int Board::kings(int p) const {
    int n = 0;
    for (int8_t v : c) n += v == (p == 0 ? 2 : -2) ? 1 : 0;
    return n;
}

uint64_t Board::hash(int toMove) const {
    const Zobrist& z = zobrist();
    uint64_t h = toMove == 1 ? z.side : 0;
    for (int s = 0; s < kSquares; ++s)
        if (c[(size_t)s]) h ^= z.piece[s][zIndex(c[(size_t)s])];
    return h;
}

// ---------------------------------------------------------------- moves

void generateCompact(const Board& b0, int p, std::vector<CMove>& out) {
    out.clear();
    Board b = b0;
    CaptureGen g{b, p, out, 0, CMove()};
    for (int s = 0; s < kSquares; ++s) {
        if (b.owner(s) != p) continue;
        g.cur = CMove();
        g.cur.from = (int8_t)s;
        g.dfs(s, b.king(s), -1);
    }
    if (g.best > 0) return;
    for (int s = 0; s < kSquares; ++s) {
        if (b.owner(s) != p) continue;
        const bool king = b.king(s);
        const int r0 = rowOf(s), c0 = colOf(s);
        for (int d = 0; d < 4; ++d) {
            if (!king && d == backDir(p)) continue;
            int r = r0 + DR[d], c = c0 + DC[d];
            while (onBoard(r, c) && b.empty(sq(r, c))) {
                CMove m;
                m.from = (int8_t)s;
                m.n = 1;
                m.path[0] = (int8_t)sq(r, c);
                m.promotes = !king && r == promotionRow(p);
                out.push_back(m);
                if (!king) break;
                r += DR[d];
                c += DC[d];
            }
        }
    }
}

bool hasCapture(const Board& b, int p) {
    for (int s = 0; s < kSquares; ++s) {
        if (b.owner(s) != p) continue;
        const bool king = b.king(s);
        const int r0 = rowOf(s), c0 = colOf(s);
        for (int d = 0; d < 4; ++d) {
            if (!king && d == backDir(p)) continue;
            int r = r0 + DR[d], c = c0 + DC[d];
            if (king)
                while (onBoard(r, c) && b.empty(sq(r, c))) {
                    r += DR[d];
                    c += DC[d];
                }
            if (!onBoard(r, c) || b.owner(sq(r, c)) != 1 - p) continue;
            const int r2 = r + DR[d], c2 = c + DC[d];
            if (onBoard(r2, c2) && b.empty(sq(r2, c2))) return true;
        }
    }
    return false;
}

Move toMove(const CMove& m) {
    Move out;
    out.from = m.from;
    out.promotes = m.promotes;
    for (int i = 0; i < m.n; ++i) out.path.push_back(m.path[(size_t)i]);
    for (int i = 0; i < m.ncap; ++i) out.captured.push_back(m.cap[(size_t)i]);
    return out;
}

std::vector<Move> generateMoves(const Board& b, int p) {
    std::vector<CMove> cm;
    generateCompact(b, p, cm);
    std::vector<Move> out;
    out.reserve(cm.size());
    for (const CMove& m : cm) out.push_back(toMove(m));
    return out;
}

void applyCompact(Board& b, int p, const CMove& m) {
    int8_t piece = b.c[(size_t)m.from];
    b.c[(size_t)m.from] = 0;
    for (int i = 0; i < m.ncap; ++i) b.c[(size_t)m.cap[(size_t)i]] = 0;
    if (m.promotes) piece = p == 0 ? 2 : -2;
    b.c[(size_t)m.to()] = piece;
}

void applyMoveTo(Board& b, int p, const Move& m) {
    int8_t piece = b.c[(size_t)m.from];
    b.c[(size_t)m.from] = 0;
    for (int s : m.captured) b.c[(size_t)s] = 0;
    if (m.promotes) piece = p == 0 ? 2 : -2;
    b.c[(size_t)m.to()] = piece;
}

std::string squareName(int s) {
    if (s < 0 || s >= kSquares) return "?";
    std::string n(1, (char)('a' + colOf(s)));
    n += (char)('1' + rowOf(s));
    return n;
}

std::string moveNotation(const Move& m) {
    std::string s = squareName(m.from);
    for (int l : m.path) s += (m.isCapture() ? "x" : "-") + squareName(l);
    if (m.promotes) s += "=D";
    return s;
}

const char* endReasonText(EndReason r) {
    switch (r) {
    case EndReason::NoPieces: return "taşı kalmadı";
    case EndReason::Blocked: return "oynayacak hamlesi kalmadı";
    case EndReason::LoneMan: return "tek damaya karşı tek taş kaldı";
    case EndReason::OneEach: return "ikisinde de birer taş kaldı";
    case EndReason::Repetition: return "aynı konum üç kez geldi";
    case EndReason::NoProgress: return "25 hamledir taş alınmadı, taş ilerlemedi";
    default: return "";
    }
}

// ---------------------------------------------------------------- the action log

std::string LoggedAction::encode() const {
    if (kind == ActKind::NextGame) return "n";
    std::string s = "m " + std::to_string(player) + " " + std::to_string(from);
    for (int l : path) s += " " + std::to_string(l);
    return s;
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    std::istringstream in(line);
    std::string k;
    if (!(in >> k)) return false;
    out = LoggedAction();
    if (k == "n") {
        out.kind = ActKind::NextGame;
        return true;
    }
    if (k != "m") return false;
    out.kind = ActKind::Move;
    if (!(in >> out.player >> out.from)) return false;
    int v;
    while (in >> v) {
        if (v < 0 || v >= kSquares) return false;
        out.path.push_back(v);
    }
    return (out.player == 0 || out.player == 1) && out.from >= 0 && out.from < kSquares && !out.path.empty() &&
           out.path.size() <= 16;
}

// ---------------------------------------------------------------- the game

Game::Game(const Rules& r) : rules_(r) {
    players_[0].name = "Sen";
    players_[0].human = true;
    players_[1].name = "Kel Mahmut";
}

void Game::setRules(const Rules& r) { rules_ = r; }

void Game::setPlayer(int p, const std::string& name, bool human) {
    if (p < 0 || p > 1) return;
    players_[(size_t)p].name = name;
    players_[(size_t)p].human = human;
}

std::string Game::says(int p, const std::string& third, const std::string& second) const {
    if (p >= 0 && p <= 1 && players_[(size_t)p].human) return second;
    return (p >= 0 && p <= 1 ? nameOf(p) + " " : std::string()) + third;
}

void Game::push(GameEvent e) { events_.push_back(std::move(e)); }

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed ^ 0xDA4Aull);
    for (PlayerInfo& pi : players_) {
        pi.score = 0;
        pi.taken = 0;
    }
    firstStarter_ = rng_.range(2);
    gameIndex_ = 0;
    turnNumber_ = 0;
    matchWinner_ = -1;
    actions_.clear();
    events_.clear();
    lastResult_ = GameResult();
    GameEvent e;
    e.type = EvType::MatchStart;
    e.amount = rules_.winsNeeded;
    e.text = "Dama maçı: " + std::to_string(rules_.winsNeeded) + " oyun alan kazanır";
    push(e);
    beginGame();
}

void Game::beginGame() {
    board_ = Board::initial();
    starter_ = (firstStarter_ + gameIndex_) % 2;
    current_ = starter_;
    plies_ = quiet_ = 0;
    log_.clear();
    hist_.assign(1, board_.hash(current_));
    legal_ = generateMoves(board_, current_);
    stage_ = Stage::Playing;
    GameEvent e;
    e.type = EvType::GameStart;
    e.player = starter_;
    e.amount = gameIndex_;
    e.text = std::to_string(gameIndex_ + 1) + ". oyun: " +
             (players_[(size_t)starter_].human ? std::string("beyazlarla sen başlıyorsun")
                                               : "beyazlarla " + nameOf(starter_) + " başlıyor");
    push(e);
}

void Game::startNextGame() {
    if (stage_ != Stage::GameOver) return;
    ++gameIndex_;
    LoggedAction a;
    a.kind = ActKind::NextGame;
    actions_.push_back(a);
    beginGame();
}

void Game::debugSetBoard(const Board& b, int toMove) {
    board_ = b;
    current_ = toMove & 1;
    stage_ = Stage::Playing;
    quiet_ = 0;
    hist_.assign(1, board_.hash(current_));
    legal_ = generateMoves(board_, current_);
}

std::string Game::moveError(int p, const Move& m) const {
    if (m.from < 0 || m.from >= kSquares || board_.owner(m.from) != p) return "Orada senin taşın yok";
    if (m.path.empty()) return "Nereye?";
    const bool mustTake = !legal_.empty() && legal_[0].isCapture();
    // the piece's own moves
    bool pieceCan = false;
    for (const Move& l : legal_) pieceCan = pieceCan || l.from == m.from;
    if (mustTake) {
        const int most = (int)legal_[0].captured.size();
        const int to = m.path.back();
        const bool step = m.path.size() == 1 && std::abs(rowOf(to) - rowOf(m.from)) + std::abs(colOf(to) - colOf(m.from)) == 1;
        if (step || (!pieceCan && board_.king(m.from) && m.path.size() == 1))
            return pieceCan ? std::string("Taş almak zorunlu") : std::string("Taş almak zorunlu: alabileceğin bir taş var");
        return most > 1 ? "En çok taşı alan yolu seçmelisin (" + std::to_string(most) + " taş)" : std::string("Bu yoldan alınmaz");
    }
    if (!board_.king(m.from)) {
        const int to = m.path.back();
        const int dr = rowOf(to) - rowOf(m.from), dc = colOf(to) - colOf(m.from);
        if (dr == -forwardOf(p) && dc == 0) return "Taş geri gidemez; yalnızca dama geri gider";
        if (dr != 0 && dc != 0) return "Çapraz gidilmez: ileri ya da yana";
        if (std::abs(dr) + std::abs(dc) > 1) return "Taş bir kare gider; uzağa yalnızca dama gider";
    } else {
        const int to = m.path.back();
        if (rowOf(to) != rowOf(m.from) && colOf(to) != colOf(m.from)) return "Dama düz gider: ileri, geri ya da yana";
    }
    return "Bu hamle olmaz";
}

ActionResult Game::applyMove(int p, int from, const std::vector<int>& path) {
    Move m;
    m.from = from;
    m.path = path;
    return applyMove(p, m);
}

ActionResult Game::applyMove(int p, const Move& m) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Oyun şu an oynanmıyor");
    if (p != current_) return ActionResult::fail("Sıra sende değil");
    const Move* found = nullptr;
    for (const Move& l : legal_)
        if (l.sameAs(m)) found = &l;
    if (!found) return ActionResult::fail(moveError(p, m));
    const Move mv = *found;
    LoggedAction a;
    a.kind = ActKind::Move;
    a.player = p;
    a.from = mv.from;
    a.path = mv.path;
    actions_.push_back(a);
    afterMove(p, mv);
    return ActionResult::success();
}

void Game::afterMove(int p, const Move& m) {
    // irreversible: a capture or a man's step forward (a man's sideways step can be undone by the next one)
    const bool forward = !board_.king(m.from) && rowOf(m.to()) != rowOf(m.from);
    applyMoveTo(board_, p, m);
    ++plies_;
    ++turnNumber_;
    players_[(size_t)p].taken += (int)m.captured.size();
    const bool irreversible = m.isCapture() || forward;
    quiet_ = irreversible ? 0 : quiet_ + 1;
    log_.push_back({p, m});
    {
        GameEvent e;
        e.type = EvType::Move;
        e.player = p;
        e.from = m.from;
        e.to = m.to();
        e.path = m.path;
        e.captured = m.captured;
        e.promotes = m.promotes;
        e.amount = (int)m.captured.size();
        const std::string n = std::to_string(m.captured.size());
        e.text = m.isCapture() ? says(p, n + " taş aldı", n + " taş aldın")
                               : says(p, moveNotation(m) + " oynadı", moveNotation(m) + " oynadın");
        push(e);
    }
    if (m.promotes) {
        GameEvent e;
        e.type = EvType::Promote;
        e.player = p;
        e.from = m.from;
        e.to = m.to();
        e.text = says(p, "dama çıktı!", "Dama çıktın!");
        push(e);
    }
    current_ = 1 - p;
    if (irreversible) hist_.clear();
    const uint64_t key = board_.hash(current_);
    hist_.push_back(key);
    legal_ = generateMoves(board_, current_);

    const int q = current_;
    if (board_.pieces(q) == 0) return endGame(p, EndReason::NoPieces);
    if (legal_.empty()) return endGame(p, EndReason::Blocked);
    const bool canTake = legal_[0].isCapture();
    if (!canTake && board_.pieces(0) == 1 && board_.pieces(1) == 1) {
        const int k0 = board_.kings(0), k1 = board_.kings(1);
        if (rules_.loneKingWins && k0 + k1 == 1) return endGame(k0 == 1 ? 0 : 1, EndReason::LoneMan);
        return endGame(-1, EndReason::OneEach);
    }
    if (rules_.repetitions > 0 && (int)std::count(hist_.begin(), hist_.end(), key) >= rules_.repetitions)
        return endGame(-1, EndReason::Repetition);
    if (rules_.noProgressPlies > 0 && quiet_ >= rules_.noProgressPlies) return endGame(-1, EndReason::NoProgress);
}

void Game::endGame(int winner, EndReason why) {
    lastResult_ = GameResult();
    lastResult_.winner = winner;
    lastResult_.reason = why;
    for (int p = 0; p < 2; ++p) {
        lastResult_.left[(size_t)p] = board_.pieces(p);
        lastResult_.kingsLeft[(size_t)p] = board_.kings(p);
    }
    lastResult_.plies = plies_;
    lastResult_.gameIndex = gameIndex_;
    legal_.clear();
    if (winner >= 0) ++players_[(size_t)winner].score;
    {
        GameEvent e;
        e.type = EvType::GameEnd;
        e.player = winner;
        e.amount = (int)why;
        if (winner < 0) {
            e.text = std::string("Berabere: ") + endReasonText(why);
        } else {
            const int l = 1 - winner;
            const std::string loser = players_[(size_t)l].human ? std::string("senin ") : genitive(nameOf(l)) + " ";
            std::string why2;
            switch (why) {
            case EndReason::NoPieces: why2 = loser + "taşı kalmadı"; break;
            case EndReason::Blocked: why2 = loser + "hamlesi kalmadı"; break;
            default: why2 = endReasonText(why); break;
            }
            e.text = says(winner, "kazandı: " + why2, "Kazandın: " + why2);
        }
        push(e);
    }
    const int w0 = players_[0].score, w1 = players_[1].score;
    const bool won = w0 >= rules_.winsNeeded || w1 >= rules_.winsNeeded;
    const bool cap = gameIndex_ + 1 >= rules_.maxGames();
    if (won || cap) {
        stage_ = Stage::MatchOver;
        if (w0 != w1) matchWinner_ = w0 > w1 ? 0 : 1;
        else if (players_[0].taken != players_[1].taken) matchWinner_ = players_[0].taken > players_[1].taken ? 0 : 1;
        else matchWinner_ = -1;
        GameEvent e;
        e.type = EvType::MatchEnd;
        e.player = matchWinner_;
        e.amount = 0;
        e.text = matchWinner_ < 0 ? std::string("Maç berabere bitti")
                                  : says(matchWinner_, "maçı aldı (" + std::to_string(std::max(w0, w1)) + "-" +
                                                           std::to_string(std::min(w0, w1)) + ")",
                                         "Maçı aldın! (" + std::to_string(std::max(w0, w1)) + "-" +
                                             std::to_string(std::min(w0, w1)) + ")");
        push(e);
    } else {
        stage_ = Stage::GameOver;
    }
}

bool Game::replay(const LoggedAction& a) {
    if (a.kind == ActKind::NextGame) {
        if (stage_ != Stage::GameOver) return false;
        startNextGame();
        return true;
    }
    return applyMove(a.player, a.from, a.path).ok;
}

} // namespace dama
