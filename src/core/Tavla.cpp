// Tavla engine implementation. See Tavla.h for the board index convention and docs/kurallar_tavla.md for
// the rules.
#include "core/Tavla.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace tavla {

namespace {

static_assert(sizeof(Position) == 28, "Position must be 28 packed bytes (hashing)");

inline int relOf(Variant v, int p, int i) { return relPoint(v, p, i); }
inline int absOf(Variant v, int p, int r) { return absPoint(v, p, r); }
inline int ownAt(const Position& pos, int p, int i) {
    const int v = pos.pts[i];
    return p == 0 ? (v > 0 ? v : 0) : (v < 0 ? -v : 0);
}
inline int oppAt(const Position& pos, int p, int i) { return ownAt(pos, 1 - p, i); }

// Highest relative point (0 = own 1-point .. 23) holding a checker of p, or -1. Bar is not included.
inline int highestRel(const Position& pos, int p, Variant v) {
    for (int r = 23; r >= 0; --r)
        if (ownAt(pos, p, absOf(v, p, r))) return r;
    return -1;
}

// ---- Gülbahar / Fevga (sameWay): no hits, one checker holds a point ----

// Fevga: while p has 14 checkers on his start point and the 15th (the first one out) has not yet passed the
// opponent's start point (p's rel 11), only that checker may move. Its rel, or -1 when the rule is over.
inline int fevgaRunner(const Position& pos, int p, Variant v) {
    if (v != Variant::Fevga || ownAt(pos, p, absOf(v, p, 23)) != kCheckers - 1) return -1;
    for (int r = 22; r >= 0; --r)
        if (ownAt(pos, p, absOf(v, p, r))) return r >= 11 ? r : -1;
    return -1;
}

// Fevga: would p moving one checker from `from` to the empty point `to` close six points in a row (a 6-prime, seen
// along the opponent's way) with every opposing checker behind it? (Allowed once one of his is past it.)
bool fevgaBlocksAll(const Position& pos, int p, int from, int to, Variant v) {
    const int o = 1 - p;
    if (pos.off[o] > 0) return false;
    auto mine = [&](int i) { return i == to || (i != from && ownAt(pos, p, i) > 0) || (i == from && ownAt(pos, p, i) > 1); };
    const int t = relOf(v, o, to); // in the opponent's own numbering: he moves down towards 0
    int lo = t, hi = t;
    while (lo > 0 && mine(absOf(v, o, lo - 1))) --lo;
    while (hi < 23 && mine(absOf(v, o, hi + 1))) ++hi;
    if (hi - lo + 1 < 6) return false;
    for (int r = 0; r < lo; ++r)
        if (ownAt(pos, o, absOf(v, o, r))) return false; // one of his is already past it
    return true;
}

template <class F>
void forEachStepSameWay(const Position& pos, int p, int die, Variant v, F&& f) {
    const int highest = highestRel(pos, p, v);
    const bool home = highest < 6;
    const int only = fevgaRunner(pos, p, v);
    for (int r = highest; r >= 0; --r) {
        if (only >= 0 && r != only) continue;
        const int i = absOf(v, p, r);
        if (!ownAt(pos, p, i)) continue;
        const int nr = r - die;
        if (nr >= 0) {
            const int to = absOf(v, p, nr);
            if (oppAt(pos, p, to) > 0) continue;
            if (v == Variant::Fevga && ownAt(pos, p, to) == 0 && fevgaBlocksAll(pos, p, i, to, v)) continue;
            f(Step{i, to, die, false});
        } else if (home && (nr == -1 || r == highest)) {
            f(Step{i, OFF, die, false});
        }
    }
}

// Calls f(Step) for every single step of player p with `die` that is legal on its own (bar first, closed
// points, bearing off with the exact or - from the highest point - a larger die). Order: bar, then from
// p's back checkers towards his home (deterministic).
template <class F>
void forEachStep(const Position& pos, int p, int die, Variant v, F&& f) {
    if (v != Variant::Klasik) {
        forEachStepSameWay(pos, p, die, v, f);
        return;
    }
    if (pos.bar[p] > 0) {
        const int to = entryPoint(p, die);
        const int o = oppAt(pos, p, to);
        if (o < 2) f(Step{BAR, to, die, o == 1});
        return;
    }
    const int highest = highestRel(pos, p, Variant::Klasik);
    const bool home = highest < 6;
    for (int r = highest; r >= 0; --r) {
        const int i = absOf(Variant::Klasik, p, r);
        if (!ownAt(pos, p, i)) continue;
        const int nr = r - die;
        if (nr >= 0) {
            const int to = absOf(Variant::Klasik, p, nr);
            const int o = oppAt(pos, p, to);
            if (o < 2) f(Step{i, to, die, o == 1});
        } else if (home && (nr == -1 || r == highest)) {
            f(Step{i, OFF, die, false});
        }
    }
}

inline void applyRaw(Position& pos, int p, const Step& s) {
    const int sg = p == 0 ? 1 : -1;
    if (s.from == BAR) --pos.bar[p];
    else pos.pts[s.from] = (int8_t)(pos.pts[s.from] - sg);
    if (s.to == OFF) {
        ++pos.off[p];
    } else {
        if (oppAt(pos, p, s.to) == 1) {
            pos.pts[s.to] = 0;
            ++pos.bar[1 - p];
        }
        pos.pts[s.to] = (int8_t)(pos.pts[s.to] + sg);
    }
}

inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint64_t hashPos(const Position& pos) {
    uint64_t w[4] = {0, 0, 0, 0};
    std::memcpy(w, &pos, sizeof(Position));
    uint64_t h = 0x9E3779B97F4A7C15ull;
    for (uint64_t x : w) h = mix64(h ^ x) + 0x632BE59BD9B4E019ull;
    return h;
}

// Small open-addressing hash set of 64-bit keys, cleared in O(1) by a generation stamp.
class SeenSet {
public:
    void reset() {
        if (keys_.empty()) resize(1u << 12);
        if (++cur_ == 0) {
            std::fill(stamp_.begin(), stamp_.end(), 0u);
            cur_ = 1;
        }
        count_ = 0;
    }
    // true if newly inserted
    bool insert(uint64_t k) {
        if ((count_ + 1) * 2 > keys_.size()) grow();
        size_t i = (size_t)k & mask_;
        while (stamp_[i] == cur_) {
            if (keys_[i] == k) return false;
            i = (i + 1) & mask_;
        }
        stamp_[i] = cur_;
        keys_[i] = k;
        ++count_;
        return true;
    }

private:
    void resize(size_t n) {
        keys_.assign(n, 0);
        stamp_.assign(n, 0);
        mask_ = n - 1;
    }
    void grow() {
        std::vector<uint64_t> old;
        for (size_t i = 0; i < keys_.size(); ++i)
            if (stamp_[i] == cur_) old.push_back(keys_[i]);
        resize(keys_.size() * 2);
        cur_ = 1;
        count_ = 0;
        for (uint64_t k : old) insert(k);
    }
    std::vector<uint64_t> keys_;
    std::vector<uint32_t> stamp_;
    uint32_t cur_ = 0;
    size_t mask_ = 0;
    size_t count_ = 0;
};

// Max number of dice that can still be played (<= cap). `dice` sorted descending.
int maxDice(const Position& pos, int p, const int* dice, int n, int cap, Variant v) {
    if (n == 0 || cap <= 0) return 0;
    int best = 0;
    for (int k = 0; k < n; ++k) {
        if (k > 0 && dice[k] == dice[k - 1]) continue;
        int rest[4];
        int m = 0;
        for (int j = 0; j < n; ++j)
            if (j != k) rest[m++] = dice[j];
        bool stop = false;
        forEachStep(pos, p, dice[k], v, [&](const Step& s) {
            if (stop) return;
            Position q = pos;
            applyRaw(q, p, s);
            // bearing off the last checker ends the game: it counts as playing every die
            const int u = q.off[p] == kCheckers ? n : 1 + maxDice(q, p, rest, m, std::min(cap, n) - 1, v);
            if (u > best) best = u;
            if (best >= n || best >= cap) stop = true;
        });
        if (best >= n || best >= cap) break;
    }
    return best;
}

// Complete-play generator shared by generatePlays / generateResults.
struct Leaf {
    int used;       // steps played
    bool won;       // bore off the last checker (counts as using every die)
    int firstDie;
    Position pos;
    std::array<Step, 4> steps;
};

struct Generator {
    int p = 0;
    Variant v = Variant::Klasik;
    bool wantSteps = true;
    std::vector<Leaf>* leaves = nullptr;
    SeenSet* seen = nullptr;
    std::array<Step, 4> path{};

    void dfs(const Position& pos, const int* dice, int n, int depth) {
        uint64_t dk = (uint64_t)n;
        for (int j = 0; j < n; ++j) dk = dk * 8 + (uint64_t)dice[j];
        const uint64_t key = mix64(hashPos(pos) ^ (dk * 0xD6E8FEB86659FD93ull));
        if (!seen->insert(key)) return;
        bool any = false;
        for (int k = 0; k < n; ++k) {
            if (k > 0 && dice[k] == dice[k - 1]) continue;
            int rest[4];
            int m = 0;
            for (int j = 0; j < n; ++j)
                if (j != k) rest[m++] = dice[j];
            forEachStep(pos, p, dice[k], v, [&](const Step& s) {
                any = true;
                Position q = pos;
                applyRaw(q, p, s);
                path[depth] = s;
                if (q.off[p] == kCheckers) record(q, depth + 1, true); // remaining dice are irrelevant
                else dfs(q, rest, m, depth + 1);
            });
        }
        if (!any) record(pos, depth, false);
    }

    void record(const Position& pos, int used, bool won) {
        Leaf l;
        l.used = used;
        l.won = won;
        l.firstDie = used > 0 ? path[0].die : 0;
        l.pos = pos;
        if (wantSteps)
            for (int i = 0; i < used; ++i) l.steps[i] = path[i];
        leaves->push_back(l);
    }
};

thread_local SeenSet tlSeen;
thread_local SeenSet tlDedup;
thread_local std::vector<Leaf> tlLeaves;

// Runs the generator and leaves the maximal, deduplicated leaves in `out` (in discovery order).
void collect(const Position& pos, int p, int d1, int d2, bool wantSteps, std::vector<const Leaf*>& out, Variant v) {
    out.clear();
    tlLeaves.clear();
    tlSeen.reset();
    Generator g;
    g.p = p;
    g.v = v;
    g.wantSteps = wantSteps;
    g.leaves = &tlLeaves;
    g.seen = &tlSeen;
    int dice[4];
    int n;
    if (d1 == d2) {
        n = 4;
        dice[0] = dice[1] = dice[2] = dice[3] = d1;
    } else {
        n = 2;
        dice[0] = std::max(d1, d2);
        dice[1] = std::min(d1, d2);
    }
    g.dfs(pos, dice, n, 0);
    int maxUsed = 0;
    for (const Leaf& l : tlLeaves) maxUsed = std::max(maxUsed, l.won ? n : l.used);
    if (maxUsed == 0) return;
    const int larger = std::max(d1, d2);
    bool largerPossible = false;
    if (maxUsed == 1 && d1 != d2)
        for (const Leaf& l : tlLeaves)
            if (l.firstDie == larger) largerPossible = true;
    tlDedup.reset();
    for (const Leaf& l : tlLeaves) {
        if (!l.won && l.used != maxUsed) continue;
        if (largerPossible && l.firstDie != larger) continue;
        if (!tlDedup.insert(hashPos(l.pos))) continue;
        out.push_back(&l);
    }
}

// ---- Turkish text helpers ----

std::vector<unsigned> codePoints(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        unsigned cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
        else { cp = c & 0x07; len = 4; }
        for (int k = 1; k < len && i + (size_t)k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp);
        i += (size_t)len;
    }
    return out;
}

// "Kel Mahmut'ta", "Hacı Rıza'da", "Emekli Nuri'de"
std::string locative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    bool backVowel = true;
    for (unsigned c : cps) {
        switch (c) {
        case 'a': case 'A': case 'o': case 'O': case 'u': case 'U': case 'I': case 0x131:
            backVowel = true;
            break;
        case 'e': case 'E': case 'i': case 0x130: case 0xF6: case 0xD6: case 0xFC: case 0xDC:
            backVowel = false;
            break;
        default:
            break;
        }
    }
    bool hard = false;
    if (!cps.empty()) {
        switch (cps.back()) {
        case 'f': case 's': case 't': case 'k': case 'h': case 'p':
        case 'F': case 'S': case 'T': case 'K': case 'H': case 'P':
        case 0xE7: case 0xC7: case 0x15F: case 0x15E:
            hard = true;
            break;
        default:
            break;
        }
    }
    return name + "'" + (hard ? "t" : "d") + (backVowel ? "a" : "e");
}

std::string capitalizeFirst(const std::string& s) {
    if (s.empty()) return s;
    if (s[0] == 'i') return "İ" + s.substr(1);
    if (s[0] >= 'a' && s[0] <= 'z') return std::string(1, (char)(s[0] - 'a' + 'A')) + s.substr(1);
    static const char* const kPairs[][2] = {{"ı", "I"}, {"ç", "Ç"}, {"ş", "Ş"}, {"ğ", "Ğ"}, {"ö", "Ö"}, {"ü", "Ü"}};
    for (const auto& pr : kPairs) {
        const std::string lower = pr[0];
        if (s.compare(0, lower.size(), lower) == 0) return pr[1] + s.substr(lower.size());
    }
    return s;
}

// Point numbers with case suffixes, by the spoken number: 13'ten, 8'e, 6'ya, 10'a, 20'ye.
std::string ablative(int n) {
    static const char* const u[] = {"", "den", "den", "ten", "ten", "ten", "dan", "den", "den", "dan"};
    const char* s = n % 10 ? u[n % 10] : (n == 10 ? "dan" : "den");
    return std::to_string(n) + "'" + s;
}
std::string dative(int n) {
    static const char* const u[] = {"", "e", "ye", "e", "e", "e", "ya", "ye", "e", "a"};
    const char* s = n % 10 ? u[n % 10] : (n == 10 ? "a" : "ye");
    return std::to_string(n) + "'" + s;
}

std::string diceStr(int d1, int d2) {
    const int a = std::max(d1, d2), b = std::min(d1, d2);
    return std::to_string(a) + "-" + std::to_string(b);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// free functions

const char* variantName(Variant v) {
    switch (v) {
    case Variant::Gulbahar: return "Gülbahar";
    case Variant::Fevga: return "Fevga";
    default: return "Klasik";
    }
}

Position Position::initial(Variant v) {
    if (v == Variant::Klasik) return initial();
    Position pos;
    pos.pts[startPoint(v, 0)] = kCheckers;
    pos.pts[startPoint(v, 1)] = -kCheckers;
    return pos;
}

Position Position::initial() {
    Position pos;
    pos.pts[23] = 2;
    pos.pts[12] = 5;
    pos.pts[7] = 3;
    pos.pts[5] = 5;
    pos.pts[0] = -2;
    pos.pts[11] = -5;
    pos.pts[16] = -3;
    pos.pts[18] = -5;
    return pos;
}

int Position::onBoard(int p) const {
    int n = 0;
    for (int i = 0; i < kPoints; ++i) n += count(p, i);
    return n;
}

int pipCount(const Position& pos, int p) { return pipCount(pos, p, Variant::Klasik); }

int pipCount(const Position& pos, int p, Variant v) {
    int s = pos.bar[p] * 25;
    for (int i = 0; i < kPoints; ++i) s += ownAt(pos, p, i) * (relOf(v, p, i) + 1);
    return s;
}

std::vector<Play> generatePlays(const Position& pos, int p, int d1, int d2, Variant v) {
    std::vector<const Leaf*> leaves;
    collect(pos, p, d1, d2, true, leaves, v);
    std::vector<Play> out;
    out.reserve(leaves.size());
    for (const Leaf* l : leaves) {
        Play pl;
        pl.steps.assign(l->steps.begin(), l->steps.begin() + l->used);
        pl.result = l->pos;
        out.push_back(std::move(pl));
    }
    return out;
}

void generateResults(const Position& pos, int p, int d1, int d2, std::vector<Position>& out, Variant v) {
    std::vector<const Leaf*> leaves;
    collect(pos, p, d1, d2, false, leaves, v);
    out.clear();
    for (const Leaf* l : leaves) out.push_back(l->pos);
}

bool applyStepTo(Position& pos, int p, const Step& s, Variant v) {
    bool found = false;
    forEachStep(pos, p, s.die, v, [&](const Step& t) {
        if (t.from == s.from && t.to == s.to) found = true;
    });
    if (!found) return false;
    applyRaw(pos, p, s);
    return true;
}

std::string diceName(int d1, int d2) {
    const int a = std::max(d1, d2), b = std::min(d1, d2);
    if (a < 1 || b < 1 || a > 6) return "";
    // [a][b], a >= b
    static const char* const kNames[7][7] = {
        {},
        {"", "hepyek"},
        {"", "yeki dü", "dübara"},
        {"", "se yek", "sebai dü", "düse"},
        {"", "cihari yek", "cihari dü", "ciharü se", "dört cihar"},
        {"", "penci yek", "penci dü", "pencü se", "ciharü penç", "dübeş"},
        {"", "şeşi yek", "şeşi dü", "şeşü se", "şeş cihar", "şeşbeş", "düşeş"},
    };
    return kNames[a][b];
}

std::string stepNotation(int p, const Step& s) { return stepNotation(p, s, Variant::Klasik); }

std::string stepNotation(int p, const Step& s, Variant v) {
    const std::string from = s.from == BAR ? std::string("bar") : std::to_string(pointNumber(v, p, s.from));
    const std::string to = s.to == OFF ? std::string("çıktı") : std::to_string(pointNumber(v, p, s.to));
    return from + "/" + to + (s.hit ? "*" : "");
}

std::string turnNotation(const TurnRecord& t) {
    if (t.d1 == 0) return t.note;
    std::string out = diceStr(t.d1, t.d2) + " " + diceName(t.d1, t.d2) + ":";
    if (t.steps.empty()) return out + " oynayamadı";
    for (const Step& s : t.steps) out += " " + stepNotation(t.player, s, t.variant);
    return out;
}

int Dice::leftCount() const {
    int c = 0;
    for (int i = 0; i < n; ++i) c += used[i] ? 0 : 1;
    return c;
}

std::vector<int> Dice::left() const {
    std::vector<int> v;
    for (int i = 0; i < n; ++i)
        if (!used[i]) v.push_back(value[i]);
    return v;
}

// ---------------------------------------------------------------------------------------------------------
// setup

Game::Game(const Rules& r) : rules_(r) {
    players_[0].name = "Sen";
    players_[0].human = true;
    players_[1].name = "Rakip";
    pos_ = Position::initial(rules_.variant);
}

void Game::setRules(const Rules& r) { rules_ = r; }

void Game::setPlayer(int p, const std::string& name, bool human) {
    if (p < 0 || p > 1) return;
    players_[p].name = name;
    players_[p].human = human;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed);
    events_.clear();
    for (PlayerInfo& pl : players_) pl.score = 0;
    gameIndex_ = 0;
    turnNumber_ = 0;
    matchWinner_ = -1;
    lastWinner_ = -1;
    crawfordUsed_ = false;
    actions_.clear();
    lastResult_ = GameResult();
    GameEvent e;
    e.type = EvType::MatchStart;
    e.amount = rules_.matchPoints;
    e.text = std::to_string(rules_.matchPoints) + " sayılık maç başlıyor";
    push(e);
    beginGame();
}

void Game::startNextGame() {
    if (stage_ != Stage::GameOver) return;
    logged(ActionResult::success(), ActKind::NextGame, -1);
    ++gameIndex_;
    beginGame();
}

void Game::beginGame() {
    pos_ = Position::initial(rules_.variant);
    rolls_ = {0, 0};
    ladder_ = 0;
    dice_ = Dice();
    history_.clear();
    turnSteps_.clear();
    openingDice_ = {0, 0};
    turnMax_ = 0;
    forcedDie_ = 0;
    log_.clear();
    gameTurn_ = 0;
    cubeValue_ = 1;
    cubeOwner_ = -1;
    // Crawford: the first game after someone reaches matchPoints - 1 is played without the cube
    crawford_ = false;
    if (rules_.doubling && !crawfordUsed_ && gameIndex_ > 0 &&
        (players_[0].score == rules_.matchPoints - 1 || players_[1].score == rules_.matchPoints - 1)) {
        crawford_ = true;
        crawfordUsed_ = true;
    }
    GameEvent e;
    e.type = EvType::GameStart;
    e.amount = gameIndex_;
    const std::string head = std::to_string(gameIndex_ + 1) + ". oyun" + (crawford_ ? " (Crawford: katlama yok)" : "");
    if (gameIndex_ > 0 && rules_.winnerStarts && lastWinner_ >= 0) {
        const int w = lastWinner_;
        e.player = w;
        e.text = head + ": " +
                 (players_[w].human ? std::string("önceki oyunu sen aldın, sen başlıyorsun")
                                    : "önceki oyunu " + nameOf(w) + " aldı, o başlıyor");
        push(e);
        startTurn(w);
        return;
    }
    e.text = head + " başlıyor: başlangıç zarları atılsın";
    push(e);
    stage_ = Stage::OpeningRoll;
    current_ = -1;
}

// ---------------------------------------------------------------------------------------------------------
// turn flow

std::pair<int, int> Game::throwDice() {
    if (!queued_.empty()) {
        const std::pair<int, int> d = queued_.front();
        queued_.erase(queued_.begin());
        return d;
    }
    const int a = rng_.range(1, 6);
    const int b = rng_.range(1, 6);
    return {a, b};
}

void Game::startTurn(int p) {
    current_ = p;
    stage_ = Stage::NeedRoll;
    dice_ = Dice();
    history_.clear();
    turnSteps_.clear();
    turnMax_ = 0;
    forcedDie_ = 0;
    ladder_ = 0;
    ++turnNumber_;
    ++gameTurn_;
}

ActionResult Game::rollOpeningImpl() {
    if (stage_ != Stage::OpeningRoll) return ActionResult::fail("Şimdi başlangıç zarı atılmaz");
    const std::pair<int, int> d = throwDice();
    openingDice_ = {d.first, d.second};
    auto who = [&](int p) { return players_[p].human ? std::string("sen") : nameOf(p); };
    GameEvent e;
    e.type = EvType::OpeningRoll;
    e.d1 = d.first;
    e.d2 = d.second;
    const std::string head = "Başlangıç zarı: " + who(0) + " " + std::to_string(d.first) + ", " + who(1) + " " +
                             std::to_string(d.second) + ". ";
    if (d.first == d.second) {
        e.player = -1;
        e.text = head + "Eşit geldi, yeniden atılıyor";
        push(e);
        return ActionResult::success();
    }
    const int starter = d.first > d.second ? 0 : 1;
    e.player = starter;
    if (rules_.openingReroll)
        e.text = head + (players_[starter].human ? std::string("Sen başlıyorsun, zarları at!")
                                                 : nameOf(starter) + " başlıyor");
    else
        e.text = head + (players_[starter].human ? std::string("Sen başlıyorsun, bu iki zarı oynuyorsun")
                                                 : nameOf(starter) + " başlıyor, bu iki zarı oynuyor");
    push(e);
    startTurn(starter);
    if (!rules_.openingReroll) {
        const int a = starter == 0 ? d.first : d.second;
        const int b = starter == 0 ? d.second : d.first;
        GameEvent r;
        r.type = EvType::Roll;
        r.player = starter;
        r.d1 = a;
        r.d2 = b;
        r.text = says(starter, "açılış zarlarıyla oynuyor: ", "açılış zarlarıyla oynuyorsun: ") + diceStr(a, b) +
                 ", " + diceName(a, b);
        push(r);
        setDice(a, b);
    }
    return ActionResult::success();
}

ActionResult Game::rollImpl(int p) {
    if (stage_ == Stage::OpeningRoll) return ActionResult::fail("Önce başlangıç zarları atılmalı");
    if (stage_ == Stage::GameOver || stage_ == Stage::MatchOver || stage_ == Stage::NotStarted)
        return ActionResult::fail("Oyun bitti");
    if (stage_ == Stage::DoubleOffered) return ActionResult::fail("Önce katlamaya cevap verilmeli");
    if (p != current_) return ActionResult::fail("Sıra sende değil");
    if (stage_ != Stage::NeedRoll) return ActionResult::fail("Zarı zaten attın, şimdi pullarını oyna");
    const std::pair<int, int> d = throwDice();
    GameEvent e;
    e.type = EvType::Roll;
    e.player = p;
    e.d1 = d.first;
    e.d2 = d.second;
    const std::string what = diceStr(d.first, d.second) + ", " + diceName(d.first, d.second) +
                             (d.first == d.second ? "!" : "");
    e.text = says(p, "zarı attı: ", "zarı attın: ") + what;
    push(e);
    setDice(d.first, d.second);
    return ActionResult::success();
}

void Game::setDice(int d1, int d2) {
    ++rolls_[current_];
    // Gülbahar: from a player's 4th roll a double climbs the ladder (this rung, then every higher double to 6-6)
    ladder_ = rules_.variant == Variant::Gulbahar && d1 == d2 && rolls_[current_] > 3 ? d1 : 0;
    dice_ = Dice();
    dice_.d1 = d1;
    dice_.d2 = d2;
    if (d1 == d2) {
        dice_.n = 4;
        dice_.value = {d1, d1, d1, d1};
    } else {
        dice_.n = 2;
        dice_.value = {d1, d2, 0, 0};
    }
    stage_ = Stage::Moving;
    history_.clear();
    turnSteps_.clear();
    computeConstraints();
    if (turnMax_ == 0) {
        GameEvent e;
        e.type = EvType::NoMove;
        e.player = current_;
        e.d1 = d1;
        e.d2 = d2;
        e.text = players_[current_].human ? std::string("Oynayacak yerin yok, sıra geçti")
                                          : nameOf(current_) + " oynayacak yer bulamadı, sıra geçti";
        push(e);
        finishTurn();
    }
}

void Game::computeConstraints() {
    std::vector<int> left = dice_.left();
    std::sort(left.begin(), left.end(), std::greater<int>());
    turnMax_ = maxDice(pos_, current_, left.data(), (int)left.size(), (int)left.size(), rules_.variant);
    forcedDie_ = 0;
    if (!dice_.isDouble() && turnMax_ == 1) {
        const int larger = std::max(dice_.d1, dice_.d2);
        bool any = false;
        forEachStep(pos_, current_, larger, rules_.variant, [&](const Step&) { any = true; });
        if (any) forcedDie_ = larger;
    }
}

int Game::maxDiceFrom(Position& pos, std::array<int, 4>& left, int nLeft, int cap) const {
    std::sort(left.begin(), left.begin() + nLeft, std::greater<int>());
    return maxDice(pos, current_, left.data(), nLeft, cap, rules_.variant);
}

std::vector<Step> Game::rawSteps(const Position& pos, int die) const {
    std::vector<Step> v;
    forEachStep(pos, current_, die, rules_.variant, [&](const Step& s) { v.push_back(s); });
    return v;
}

std::vector<Step> Game::legalSteps() const {
    std::vector<Step> out;
    if (stage_ != Stage::Moving) return out;
    const int need = turnMax_ - (int)turnSteps_.size();
    if (need <= 0) return out;
    const std::vector<int> left = dice_.left();
    std::vector<int> tried;
    for (int v : left) {
        if (std::find(tried.begin(), tried.end(), v) != tried.end()) continue;
        tried.push_back(v);
        if (forcedDie_ && v != forcedDie_) continue;
        std::array<int, 4> rest{};
        int m = 0;
        bool skipped = false;
        for (int w : left) {
            if (w == v && !skipped) {
                skipped = true;
                continue;
            }
            rest[m++] = w;
        }
        for (const Step& s : rawSteps(pos_, v)) {
            Position q = pos_;
            applyRaw(q, current_, s);
            std::array<int, 4> r2 = rest;
            const bool won = q.off[current_] == kCheckers;
            if (won || 1 + maxDiceFrom(q, r2, m, need - 1) >= need) out.push_back(s);
        }
    }
    return out;
}

std::vector<Step> Game::legalStepsFrom(int from) const {
    std::vector<Step> out;
    for (const Step& s : legalSteps())
        if (s.from == from) out.push_back(s);
    return out;
}

std::vector<Play> Game::allTurnPlays() const {
    std::vector<Play> out;
    if (stage_ != Stage::Moving) return out;
    const std::vector<int> left = dice_.left();
    if (left.empty()) return out;
    if (turnSteps_.empty()) return generatePlays(pos_, current_, dice_.d1, dice_.d2, rules_.variant);
    // Partial turn: enumerate by legal steps (keeps the max-dice / larger-die constraints exact).
    struct Rec {
        static void go(Game& tmp, std::vector<Step>& path, std::vector<Play>& out, SeenSet& seen) {
            const std::vector<Step> steps = tmp.legalSteps();
            if (steps.empty() || tmp.stage_ != Stage::Moving) {
                if (seen.insert(hashPos(tmp.pos_))) out.push_back(Play{path, tmp.pos_});
                return;
            }
            for (const Step& s : steps) {
                Game t2 = tmp;
                t2.rules_.confirmTurn = true; // never pass the turn inside the search
                t2.events_.clear();
                t2.applyStep(t2.current_, s.from, s.to, s.die);
                path.push_back(s);
                go(t2, path, out, seen);
                path.pop_back();
            }
        }
    };
    Game tmp = *this;
    tmp.rules_.confirmTurn = true;
    tmp.events_.clear();
    tmp.actions_.clear();
    tmp.logActions_ = false;
    std::vector<Step> path;
    SeenSet seen;
    seen.reset();
    Rec::go(tmp, path, out, seen);
    return out;
}

bool Game::turnComplete() const { return stage_ == Stage::Moving && legalSteps().empty(); }

ActionResult Game::checkMoving(int p) const {
    if (stage_ == Stage::OpeningRoll) return ActionResult::fail("Önce başlangıç zarları atılmalı");
    if (stage_ == Stage::GameOver || stage_ == Stage::MatchOver || stage_ == Stage::NotStarted)
        return ActionResult::fail("Oyun bitti");
    if (stage_ == Stage::DoubleOffered) return ActionResult::fail("Önce katlamaya cevap verilmeli");
    if (p != current_) return ActionResult::fail("Sıra sende değil");
    if (stage_ == Stage::NeedRoll) return ActionResult::fail("Önce zar atmalısın");
    return ActionResult::success();
}

std::string Game::stepError(int from, int to) const {
    const int p = current_;
    const Variant var = rules_.variant;
    if (from == BAR && pos_.bar[p] == 0) return sameWay(var) ? "Bu oyunda kırık pul yok" : "Kırık pulun yok";
    if (pos_.bar[p] > 0 && from != BAR) return "Önce kırık pulunu girmelisin";
    if (from != BAR && (from < 0 || from >= kPoints)) return "Geçersiz hane";
    if (to != OFF && (to < 0 || to >= kPoints)) return "Geçersiz hane";
    if (from != BAR && ownAt(pos_, p, from) == 0) return "Orada senin pulun yok";
    if (turnMax_ - (int)turnSteps_.size() <= 0) return "Zarların hepsini oynadın";
    const std::vector<int> left = dice_.left();
    if (to == OFF) {
        if (highestRel(pos_, p, var) >= 6) return "Pul toplamak için bütün pulların evde olmalı";
        bool fits = false;
        for (int v : left)
            forEachStep(pos_, p, v, var, [&](const Step& s) { fits = fits || (s.from == from && s.to == OFF); });
        if (!fits) return "Bu zarlarla o puldan toplanmaz; önce arkadaki pulları oyna";
    } else {
        const int fromRel = from == BAR ? 24 : relOf(var, p, from);
        const int dist = fromRel - relOf(var, p, to);
        if (dist <= 0) return "Pullar geri gitmez";
        if (sameWay(var) && oppAt(pos_, p, to) >= 1) return "O hane rakibin: bu oyunda tek pul da haneyi tutar";
        if (oppAt(pos_, p, to) >= 2) return "O kapı kapalı";
        const int runner = fevgaRunner(pos_, p, var);
        if (runner >= 0 && from != absOf(var, p, runner))
            return "Önce ilk çıkan pulun rakibin başlangıç hanesini geçmeli";
        if (std::find(left.begin(), left.end(), dist) == left.end()) {
            // a drag over two or more dice ("6-4 ile 10 hane"): the UI must move one die at a time
            bool combo = false;
            if (dice_.isDouble()) combo = dist % left[0] == 0 && dist / left[0] <= (int)left.size();
            else combo = left.size() == 2 && dist == left[0] + left[1];
            if (combo) return "Her seferinde bir zar oyna: önce birini, sonra ötekini";
            return "Zarlarda " + std::to_string(dist) + " yok";
        }
        if (var == Variant::Fevga && ownAt(pos_, p, to) == 0 && fevgaBlocksAll(pos_, p, from, to, var))
            return "Altı hanelik kapı yapamazsın: rakibin bütün pulları arkasında kalır";
    }
    if (forcedDie_) return "Sadece bir zar oynanabiliyor; büyük zarı oynamak zorundasın";
    return "Bu hamleden sonra zarların hepsi oynanamıyor; oynanabilen bütün zarları oynamalısın";
}

ActionResult Game::applyStep(int p, int from, int to) { return applyStep(p, from, to, 0); }

ActionResult Game::applyStep(int p, int from, int to, int die) {
    int used = 0;
    return logged(applyStepImpl(p, from, to, die, used), ActKind::Step, p, from, to, used);
}

ActionResult Game::applyStepImpl(int p, int from, int to, int die, int& used) {
    const ActionResult chk = checkMoving(p);
    if (!chk.ok) return chk;
    const std::vector<Step> steps = legalSteps();
    const Step* pick = nullptr;
    for (const Step& s : steps) {
        if (s.from != from || s.to != to) continue;
        if (die && s.die != die) continue;
        if (!pick || s.die < pick->die) pick = &s;
    }
    if (!pick) {
        if (die) {
            bool other = false;
            for (const Step& s : steps) other = other || (s.from == from && s.to == to);
            if (other) return ActionResult::fail("Bu hamle o zarla oynanmaz");
        }
        return ActionResult::fail(stepError(from, to));
    }
    const Step s = *pick;
    used = s.die;
    history_.push_back(Snapshot{pos_, dice_});
    applyRaw(pos_, p, s);
    for (int i = 0; i < dice_.n; ++i) {
        if (!dice_.used[i] && dice_.value[i] == s.die) {
            dice_.used[i] = true;
            break;
        }
    }
    turnSteps_.push_back(s);

    GameEvent e;
    e.type = EvType::Step;
    e.player = p;
    e.from = s.from;
    e.to = s.to;
    e.die = s.die;
    e.hit = s.hit;
    const Variant var = rules_.variant;
    if (s.from == BAR) {
        e.text = says(p, "kırık pulunu " + dative(pointNumber(var, p, s.to)) + " girdi",
                      "kırık pulunu " + dative(pointNumber(var, p, s.to)) + " girdin");
    } else if (s.to == OFF) {
        e.text = says(p, ablative(pointNumber(var, p, s.from)) + " pul topladı", ablative(pointNumber(var, p, s.from)) + " pul topladın");
    } else {
        const std::string mv = ablative(pointNumber(var, p, s.from)) + " " + dative(pointNumber(var, p, s.to));
        e.text = says(p, mv + " oynadı", mv + " oynadın");
    }
    if (s.hit) e.text += players_[p].human ? ", pul kırdın!" : ", pul kırdı!";
    push(e);
    if (s.hit) {
        GameEvent h;
        h.type = EvType::Hit;
        h.player = p;
        h.to = s.to;
        h.die = s.die;
        h.hit = true;
        h.amount = 1 - p;
        if (players_[p].human) h.text = "Pul kırdın!";
        else if (players_[1 - p].human) h.text = nameOf(p) + " pulunu kırdı!";
        else h.text = nameOf(p) + " pul kırdı!";
        push(h);
    }
    if (s.from == BAR) {
        GameEvent b;
        b.type = EvType::EnterFromBar;
        b.player = p;
        b.from = BAR;
        b.to = s.to;
        b.die = s.die;
        b.text = says(p, "kırık pulunu girdi", "kırık pulunu girdin");
        push(b);
    }
    if (s.to == OFF) {
        GameEvent b;
        b.type = EvType::BearOff;
        b.player = p;
        b.from = s.from;
        b.to = OFF;
        b.die = s.die;
        b.amount = pos_.off[p];
        b.text = says(p, "pul topladı (", "pul topladın (") + std::to_string(pos_.off[p]) + "/15)";
        push(b);
    }
    if (pos_.off[p] == kCheckers) {
        recordTurn();
        endGame(p);
        return ActionResult::success();
    }
    afterStep();
    return ActionResult::success();
}

void Game::afterStep() {
    if (rules_.confirmTurn) return;
    if (legalSteps().empty()) endOfPlay();
}

void Game::endOfPlay() {
    // Gülbahar: a rung played out in full climbs to the next double; a rung that could not be played in full ends
    // the turn (the rest of the ladder is lost)
    if (ladder_ > 0 && ladder_ < 6 && dice_.isDouble() && dice_.leftCount() == 0) nextRung();
    else finishTurn();
}

void Game::nextRung() {
    recordTurn(); // (each rung is its own line of the record)
    const int v = ++ladder_;
    dice_ = Dice();
    dice_.d1 = dice_.d2 = v;
    dice_.n = 4;
    dice_.value = {v, v, v, v};
    history_.clear();
    turnSteps_.clear();
    GameEvent e;
    e.type = EvType::Roll;
    e.player = current_;
    e.d1 = e.d2 = v;
    e.amount = 1;
    e.text = "Gülbahar: sıra " + diceStr(v, v) + ", " + diceName(v, v);
    push(e);
    computeConstraints();
    if (turnMax_ == 0) {
        GameEvent n;
        n.type = EvType::NoMove;
        n.player = current_;
        n.d1 = n.d2 = v;
        n.text = players_[current_].human ? diceName(v, v) + " oynanmıyor, merdiven bitti; sıra geçti"
                                          : nameOf(current_) + " " + diceName(v, v) + " oynayamadı, sıra geçti";
        push(n);
        finishTurn();
    }
}

ActionResult Game::undoStepImpl(int p) {
    const ActionResult chk = checkMoving(p);
    if (!chk.ok) return chk;
    if (history_.empty()) return ActionResult::fail("Geri alınacak hamle yok");
    const Step s = turnSteps_.back();
    pos_ = history_.back().pos;
    dice_ = history_.back().dice;
    history_.pop_back();
    turnSteps_.pop_back();
    GameEvent e;
    e.type = EvType::Undo;
    e.player = p;
    e.from = s.from;
    e.to = s.to;
    e.die = s.die;
    e.hit = s.hit;
    e.text = says(p, "hamlesini geri aldı", "hamleni geri aldın");
    push(e);
    return ActionResult::success();
}

ActionResult Game::endTurnImpl(int p) {
    const ActionResult chk = checkMoving(p);
    if (!chk.ok) return chk;
    if (!legalSteps().empty()) return ActionResult::fail("Daha oynaman gereken zar var");
    endOfPlay();
    return ActionResult::success();
}

void Game::finishTurn() {
    const int p = current_;
    const int next = 1 - p;
    GameEvent e;
    e.type = EvType::TurnEnd;
    e.player = p;
    e.amount = next;
    e.text = players_[next].human ? std::string("Sıra sende") : "Sıra " + locative(nameOf(next));
    push(e);
    recordTurn();
    startTurn(next);
}

void Game::recordTurn() {
    TurnRecord t;
    t.player = current_;
    t.d1 = dice_.d1;
    t.d2 = dice_.d2;
    t.steps = turnSteps_;
    t.variant = rules_.variant;
    if (t.d1 > 0) log_.push_back(std::move(t));
}

bool Game::canDouble(int p) const {
    return rules_.doubling && stage_ == Stage::NeedRoll && p == current_ && (cubeOwner_ < 0 || cubeOwner_ == p) &&
           !crawford_ && gameTurn_ >= 2 && cubeValue_ < 64;
}

ActionResult Game::offerDoubleImpl(int p) {
    if (!rules_.doubling) return ActionResult::fail("Bu masada katlama yok");
    if (stage_ == Stage::GameOver || stage_ == Stage::MatchOver || stage_ == Stage::NotStarted)
        return ActionResult::fail("Oyun bitti");
    if (stage_ == Stage::OpeningRoll) return ActionResult::fail("Önce başlangıç zarları atılmalı");
    if (stage_ == Stage::DoubleOffered) return ActionResult::fail("Katlama zaten teklif edildi");
    if (p != current_) return ActionResult::fail("Sıra sende değil");
    if (stage_ != Stage::NeedRoll) return ActionResult::fail("Katlama ancak zar atmadan önce teklif edilir");
    if (crawford_) return ActionResult::fail("Crawford oyununda katlama yapılmaz");
    if (cubeOwner_ >= 0 && cubeOwner_ != p) return ActionResult::fail("Katlama zarı rakibinde; sadece o katlayabilir");
    if (gameTurn_ < 2) return ActionResult::fail("İlk hamleden önce katlanmaz");
    if (cubeValue_ >= 64) return ActionResult::fail("Katlama zarı en yüksek değerde");
    stage_ = Stage::DoubleOffered;
    GameEvent e;
    e.type = EvType::DoubleOffer;
    e.player = p;
    e.amount = cubeValue_ * 2;
    e.text = says(p, "katladı: ", "katladın: ") + std::to_string(cubeValue_ * 2);
    push(e);
    TurnRecord t;
    t.player = p;
    t.note = "katladı: " + std::to_string(cubeValue_ * 2);
    log_.push_back(t);
    return ActionResult::success();
}

ActionResult Game::acceptDoubleImpl(int p) {
    if (stage_ != Stage::DoubleOffered) return ActionResult::fail("Cevap verilecek bir katlama yok");
    if (p != responder()) return ActionResult::fail("Katlamayı sen teklif ettin; cevap rakibinden");
    cubeValue_ *= 2;
    cubeOwner_ = p;
    stage_ = Stage::NeedRoll;
    GameEvent e;
    e.type = EvType::DoubleTake;
    e.player = p;
    e.amount = cubeValue_;
    e.text = says(p, "katlamayı kabul etti", "katlamayı kabul ettin");
    push(e);
    if (!log_.empty() && log_.back().d1 == 0) log_.back().note += ", kabul";
    return ActionResult::success();
}

ActionResult Game::declineDoubleImpl(int p) {
    if (stage_ != Stage::DoubleOffered) return ActionResult::fail("Cevap verilecek bir katlama yok");
    if (p != responder()) return ActionResult::fail("Katlamayı sen teklif ettin; cevap rakibinden");
    GameEvent e;
    e.type = EvType::DoubleDrop;
    e.player = p;
    e.amount = cubeValue_;
    e.text = says(p, "pes etti", "pes ettin");
    push(e);
    if (!log_.empty() && log_.back().d1 == 0) log_.back().note += ", pes";
    endGame(current_, true);
    return ActionResult::success();
}

void Game::endGame(int winner, bool dropped) {
    const int loser = 1 - winner;
    GameResult r;
    r.winner = winner;
    r.gameIndex = gameIndex_;
    r.cube = cubeValue_;
    r.dropped = dropped;
    if (!dropped) {
        r.mars = pos_.off[loser] == 0;
        bool inWinnerHome = false;
        for (int i = homeLo(winner); i < homeLo(winner) + 6; ++i) inWinnerHome = inWinnerHome || ownAt(pos_, loser, i) > 0;
        // (Gülbahar / Fevga: no katmerli mars)
        r.katmerli = r.mars && rules_.variant == Variant::Klasik && (pos_.bar[loser] > 0 || inWinnerHome);
    }
    r.base = r.mars ? ((r.katmerli && rules_.katmerliMars) ? 3 : 2) : 1;
    r.points = r.base * r.cube;
    lastResult_ = r;
    lastWinner_ = winner;
    players_[winner].score += r.points;
    stage_ = Stage::GameOver;
    dice_ = Dice();
    ladder_ = 0;
    history_.clear();
    turnSteps_.clear();

    GameEvent e;
    e.type = EvType::GameEnd;
    e.player = winner;
    e.amount = r.points;
    e.mars = r.mars;
    e.katmerli = r.katmerli;
    std::string pts = " (+" + std::to_string(r.points) + ")";
    if (r.cube > 1)
        pts = r.base > 1 ? " (" + std::to_string(r.base) + " × " + std::to_string(r.cube) + " = +" + std::to_string(r.points) + ")"
                         : " (+" + std::to_string(r.points) + ", katlama " + std::to_string(r.cube) + ")";
    const bool sen = players_[winner].human;
    if (dropped)
        e.text = (sen ? nameOf(loser) + " pes etti, oyunu aldın" : nameOf(winner) + " oyunu aldı") + pts;
    else if (r.mars && r.katmerli)
        e.text = (sen ? std::string("Katmerli mars ettin!") : nameOf(winner) + " katmerli mars etti!") + pts;
    else if (r.mars)
        e.text = (sen ? std::string("Mars ettin!") : nameOf(winner) + " mars etti!") + pts;
    else
        e.text = (sen ? std::string("Oyunu kazandın!") : nameOf(winner) + " oyunu kazandı") + pts;
    push(e);

    if (players_[winner].score >= rules_.matchPoints) {
        stage_ = Stage::MatchOver;
        matchWinner_ = winner;
        GameEvent m;
        m.type = EvType::MatchEnd;
        m.player = winner;
        m.amount = players_[winner].score;
        const std::string sc = " (" + std::to_string(players_[winner].score) + "-" + std::to_string(players_[loser].score) + ")";
        m.text = (sen ? std::string("Maçı kazandın!") : "Maçı " + nameOf(winner) + " kazandı") + sc;
        push(m);
    }
}

// ---------------------------------------------------------------------------------------------------------
// the action log (save / resume)

ActionResult Game::rollOpening() { return logged(rollOpeningImpl(), ActKind::OpeningRoll, -1); }
ActionResult Game::roll(int p) { return logged(rollImpl(p), ActKind::Roll, p); }
ActionResult Game::undoStep(int p) { return logged(undoStepImpl(p), ActKind::Undo, p); }
ActionResult Game::endTurn(int p) { return logged(endTurnImpl(p), ActKind::EndTurn, p); }
ActionResult Game::offerDouble(int p) { return logged(offerDoubleImpl(p), ActKind::Double, p); }
ActionResult Game::acceptDouble(int p) { return logged(acceptDoubleImpl(p), ActKind::Take, p); }
ActionResult Game::declineDouble(int p) { return logged(declineDoubleImpl(p), ActKind::Drop, p); }

ActionResult Game::logged(ActionResult r, ActKind k, int p, int from, int to, int die) {
    if (r.ok && logActions_) {
        LoggedAction a;
        a.kind = k;
        a.player = p;
        a.from = from;
        a.to = to;
        a.die = die;
        actions_.push_back(a);
    }
    return r;
}

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case ActKind::OpeningRoll: return rollOpening().ok;
    case ActKind::Roll: return roll(a.player).ok;
    case ActKind::Step: return applyStep(a.player, a.from, a.to, a.die).ok;
    case ActKind::Undo: return undoStep(a.player).ok;
    case ActKind::EndTurn: return endTurn(a.player).ok;
    case ActKind::Double: return offerDouble(a.player).ok;
    case ActKind::Take: return acceptDouble(a.player).ok;
    case ActKind::Drop: return declineDouble(a.player).ok;
    case ActKind::NextGame:
        if (stage_ != Stage::GameOver) return false;
        startNextGame();
        return true;
    }
    return false;
}

std::string LoggedAction::encode() const {
    std::string s = std::to_string((int)kind) + " " + std::to_string(player);
    if (kind == ActKind::Step) s += " " + std::to_string(from) + " " + std::to_string(to) + " " + std::to_string(die);
    return s;
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    std::vector<int> v;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        size_t j = i;
        if (line[j] == '-') ++j;
        const size_t digits = j;
        while (j < line.size() && line[j] >= '0' && line[j] <= '9') ++j;
        if (j == digits || (j < line.size() && line[j] != ' ')) return false;
        v.push_back(std::atoi(line.substr(i, j - i).c_str()));
        i = j;
    }
    if (v.size() < 2 || v[0] < 0 || v[0] > (int)ActKind::NextGame) return false;
    out.kind = (ActKind)v[0];
    out.player = v[1];
    if (out.kind == ActKind::Step) {
        if (v.size() != 5) return false;
        out.from = v[2];
        out.to = v[3];
        out.die = v[4];
    } else if (v.size() != 2) {
        return false;
    }
    return out.player >= -1 && out.player <= 1;
}

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::push(GameEvent e) { events_.push_back(std::move(e)); }

std::string Game::says(int p, const std::string& third, const std::string& second) const {
    return players_[p].human ? capitalizeFirst(second) : players_[p].name + " " + third;
}

// ---------------------------------------------------------------------------------------------------------
// debug hooks

void Game::debugSetTurn(int p, Stage s) {
    current_ = p;
    stage_ = s;
    dice_ = Dice();
    history_.clear();
    turnSteps_.clear();
    turnMax_ = 0;
    forcedDie_ = 0;
    ladder_ = 0;
    ++turnNumber_;
    gameTurn_ = std::max(gameTurn_ + 1, 2); // a later turn of the game: doubling is allowed
}

void Game::debugSetDice(int d1, int d2) {
    ladder_ = 0;
    dice_ = Dice();
    dice_.d1 = d1;
    dice_.d2 = d2;
    if (d1 == d2) {
        dice_.n = 4;
        dice_.value = {d1, d1, d1, d1};
    } else {
        dice_.n = 2;
        dice_.value = {d1, d2, 0, 0};
    }
    stage_ = Stage::Moving;
    history_.clear();
    turnSteps_.clear();
    computeConstraints();
}

void Game::debugQueueDice(int d1, int d2) { queued_.push_back({d1, d2}); }

} // namespace tavla
