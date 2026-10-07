// Bezik engine (see Bezik.h, docs/kurallar_bezik.md).
#include "core/Bezik.h"

#include <algorithm>

namespace bezik {

namespace {

// Which earlier uses block a card from a new combination of `k`: the same kind, and a "smaller" use of cards that
// already made a bigger one (a single bezik from a double's cards, a koz marriage from the sequence's).
uint16_t blockMask(MeldKind k) {
    auto b = [](MeldKind m) { return (uint16_t)(1u << (int)m); };
    switch (k) {
    case MeldKind::Bezik: return b(MeldKind::Bezik) | b(MeldKind::CiftBezik);
    case MeldKind::Evlilik: return b(MeldKind::Evlilik) | b(MeldKind::KozEvlilik);
    case MeldKind::KozEvlilik: return b(MeldKind::Evlilik) | b(MeldKind::KozEvlilik) | b(MeldKind::Seri);
    default: return b(k);
    }
}

int fourRank(MeldKind k) {
    switch (k) {
    case MeldKind::DortAs: return kart::As;
    case MeldKind::DortPapaz: return kart::Papaz;
    case MeldKind::DortKiz: return kart::Kiz;
    case MeldKind::DortVale: return kart::Vale;
    default: return -1;
    }
}

// The faces a combination needs (a multiset; four of a kind: -rank, any suit).
std::vector<int> pattern(MeldKind k, int trump, int suit) {
    using namespace kart;
    switch (k) {
    case MeldKind::Bezik: return {BEZIK_QUEEN, BEZIK_JACK};
    case MeldKind::CiftBezik: return {BEZIK_QUEEN, BEZIK_QUEEN, BEZIK_JACK, BEZIK_JACK};
    case MeldKind::Seri:
        return {makeCard(trump, As), makeCard(trump, 10), makeCard(trump, Papaz), makeCard(trump, Kiz), makeCard(trump, Vale)};
    case MeldKind::Evlilik:
    case MeldKind::KozEvlilik: return {makeCard(suit, Papaz), makeCard(suit, Kiz)};
    default: {
        const int r = fourRank(k);
        return {-r, -r, -r, -r};
    }
    }
}

bool slotFits(int slot, int card) { return slot < 0 ? rankOf(card) == -slot : faceOf(card) == slot; }

const char* const kMeldNames[NUM_MELDS] = {"Bezik", "Çift bezik", "Koz serisi", "Dört as", "Dört papaz",
                                           "Dört kız", "Dört vale", "Evlilik", "Koz evliliği"};

} // namespace

std::vector<int> fullDeck() {
    std::vector<int> d;
    for (int i = 0; i < DECK_SIZE; ++i) d.push_back(idOf(i));
    std::sort(d.begin(), d.end());
    return d;
}

std::string cardName(int id) { return kart::cardNameTR(faceOf(id)); }
std::string cardAccusative(int id) { return kart::cardAccusativeTR(faceOf(id)); }
const char* meldNameTR(MeldKind k) { return (int)k >= 0 && (int)k < NUM_MELDS ? kMeldNames[(int)k] : "?"; }

// ---------------------------------------------------------------------------------------------------------
// LoggedAction
// ---------------------------------------------------------------------------------------------------------

std::string LoggedAction::encode() const {
    std::string s = std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(value);
    for (int c : cards) s += " " + std::to_string(c);
    return s;
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    std::vector<int> v;
    if (!kart::parseIntList(line, v, 8)) return false;
    if (v.size() < 3 || v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)v[0];
    out.seat = v[1];
    out.value = v[2];
    out.cards.assign(v.begin() + 3, v.end());
    if (out.kind != LogKind::Declare && !out.cards.empty()) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

Game::Game(const Rules& r) : rules_(r), rng_(1) {}

void Game::setRules(const Rules& r) {
    if (stage_ == Stage::NotStarted || stage_ == Stage::MatchOver) rules_ = r;
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat > 1) return;
    names_[(size_t)seat] = name;
    human_[(size_t)seat] = human;
}

void Game::push(GameEvent&& e) {
    if (!silent_) kart::pushEvent(events_, std::move(e));
}

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> e;
    e.swap(events_);
    return e;
}

std::string Game::says(int s, const std::string& third, const std::string& second) const {
    if (silent_) return {};
    return human_[(size_t)s] ? second : names_[(size_t)s] + " " + third;
}

void Game::logAction(LogKind k, int seat, int value, const std::vector<int>& cards) {
    if (silent_) return; // (the bots' copies)
    LoggedAction a;
    a.kind = k;
    a.seat = seat;
    a.value = value;
    a.cards = cards;
    log_.push_back(std::move(a));
}

void Game::startMatch(uint64_t seed) {
    matchSeed_ = seed;
    rng_.reseed(seed ^ 0xBE21Cull);
    log_.clear();
    sheet_.clear();
    totals_ = {0, 0};
    handIndex_ = 0;
    firstDealer_ = (int)rng_.range(2);
    dealer_ = firstDealer_;
    events_.clear();
    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = "Bezik başlıyor: " + std::to_string(rules_.target) + " sayıya";
    push(std::move(e));
    dealHand();
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    logAction(LogKind::NextHand, -1, 0);
    ++handIndex_;
    dealer_ = 1 - dealer_;
    dealHand();
}

void Game::sortHand(int s) {
    std::vector<int>& h = hand_[(size_t)s];
    std::sort(h.begin(), h.end(), [](int a, int b) {
        if (suitOf(a) != suitOf(b)) return suitOf(a) < suitOf(b);
        if (strength(a) != strength(b)) return strength(a) > strength(b);
        return a < b;
    });
}

void Game::dealHand() {
    std::vector<int> deck = fullDeck();
    rng_.shuffle(deck);
    for (int s = 0; s < 2; ++s) {
        hand_[(size_t)s].clear();
        table_[(size_t)s].clear();
        won_[(size_t)s].clear();
        exposed_[(size_t)s].clear();
    }
    // 3-2-3, the dealer's opponent first
    const int first = 1 - dealer_;
    size_t k = 0;
    for (int part : {3, 2, 3})
        for (int j = 0; j < 2; ++j) {
            const int s = j == 0 ? first : dealer_;
            for (int i = 0; i < part; ++i) hand_[(size_t)s].push_back(deck[k++]);
        }
    turnUp_ = deck[k++];
    trump_ = suitOf(turnUp_);
    stock_.assign(deck.begin() + (std::ptrdiff_t)k, deck.end()); // drawn from the back
    for (int s = 0; s < 2; ++s) sortHand(s);
    used_.fill(0);
    sevenDone_.fill(false);
    tricks_.clear();
    declared_.clear();
    meldPts_ = {0, 0};
    brisquePts_ = {0, 0};
    lastPts_ = {0, 0};
    bestMeld_ = {0, 0};
    bestName_ = {"", ""};
    second_ = false;
    declaredNow_ = false;
    trick_ = Trick{};
    {
        GameEvent e;
        e.type = EvType::HandStart;
        e.seat = dealer_;
        e.amount = handIndex_;
        if (!silent_)
            e.text = std::to_string(handIndex_ + 1) + ". el" + (human_[(size_t)dealer_] ? std::string(": sen dağıtıyorsun")
                                                                                         : ": " + names_[(size_t)dealer_] + " dağıtıyor");
        push(std::move(e));
    }
    {
        GameEvent e;
        e.type = EvType::Deal;
        e.seat = dealer_;
        e.card = turnUp_;
        if (rankOf(turnUp_) == 7) {
            e.amount = rules_.seven;
            meldPts_[(size_t)dealer_] += rules_.seven;
            sevenDone_[(size_t)turnUp_] = true;
        }
        if (!silent_) {
            e.text = std::string("Koz ") + kart::suitNameTR(trump_) + " (" + cardName(turnUp_) + ")";
            if (e.amount) e.text += human_[(size_t)dealer_] ? " · yedili açtın: +10" : " · " + names_[(size_t)dealer_] + " yedili açtı: +10";
        }
        push(std::move(e));
    }
    stage_ = Stage::Playing;
    beginTurn(first);
}

void Game::beginTurn(int seat) {
    current_ = seat;
    trick_ = Trick{};
    trick_.index = (int)tricks_.size();
    trick_.leader = seat;
    trick_.second = second_;
    stage_ = Stage::Playing;
    declaredNow_ = false;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = seat;
    push(std::move(e));
}

bool Game::has(int s, int card) const {
    const auto& h = hand_[(size_t)s];
    return std::find(h.begin(), h.end(), card) != h.end();
}

bool Game::inTable(int s, int card) const {
    const auto& t = table_[(size_t)s];
    return std::find(t.begin(), t.end(), card) != t.end();
}

bool Game::removeCard(int s, int card, bool* fromTable) {
    auto& h = hand_[(size_t)s];
    auto it = std::find(h.begin(), h.end(), card);
    if (it != h.end()) {
        h.erase(it);
        if (fromTable) *fromTable = false;
    } else {
        auto& t = table_[(size_t)s];
        auto jt = std::find(t.begin(), t.end(), card);
        if (jt == t.end()) return false;
        t.erase(jt);
        if (fromTable) *fromTable = true;
    }
    auto& x = exposed_[(size_t)s];
    x.erase(std::remove(x.begin(), x.end(), card), x.end());
    return true;
}

std::vector<int> Game::playable(int s) const {
    std::vector<int> v = hand_[(size_t)s];
    v.insert(v.end(), table_[(size_t)s].begin(), table_[(size_t)s].end());
    return v;
}

std::vector<int> Game::legalCards(int seat) const {
    if (stage_ != Stage::Playing || seat != current_) return {};
    std::vector<int> all = playable(seat);
    if (!second_ || trick_.cards.empty()) return all;
    const int lead = trick_.cards[0].card;
    const int ls = suitOf(lead);
    std::vector<int> same, higher, trumps;
    for (int c : all) {
        if (suitOf(c) == ls) {
            same.push_back(c);
            if (strength(c) > strength(lead)) higher.push_back(c);
        } else if (suitOf(c) == trump_) {
            trumps.push_back(c);
        }
    }
    if (!same.empty()) return higher.empty() ? same : higher;
    if (!trumps.empty()) return trumps;
    return all;
}

bool Game::isLegal(int seat, int card) const {
    const std::vector<int> l = legalCards(seat);
    return std::find(l.begin(), l.end(), card) != l.end();
}

ActionResult Game::playCard(int seat, int card) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Şimdi kâğıt oynanmaz.");
    if (seat != current_) return ActionResult::fail("Sıra sende değil.");
    if (!has(seat, card) && !inTable(seat, card)) return ActionResult::fail("Bu kâğıt elinde değil.");
    if (second_ && !isLegal(seat, card)) {
        const int lead = trick_.cards[0].card;
        bool same = false;
        for (int c : hand_[(size_t)seat]) same = same || suitOf(c) == suitOf(lead);
        if (same && suitOf(card) == suitOf(lead)) return ActionResult::fail("Yerdekini geçmen gerek: daha büyük " + std::string(kart::suitNameTR(suitOf(lead))) + " at.");
        if (same) return ActionResult::fail(std::string("Renge uymalısın: ") + kart::suitNameTR(suitOf(lead)) + " at.");
        return ActionResult::fail(std::string("Rengin yoksa koz çakmalısın (") + kart::suitNameTR(trump_) + ").");
    }
    bool fromTable = false;
    removeCard(seat, card, &fromTable);
    logAction(LogKind::Play, seat, card);
    trick_.cards.push_back({seat, card});
    {
        GameEvent e;
        e.type = EvType::Play;
        e.seat = seat;
        e.card = card;
        e.fromTable = fromTable;
        if (!silent_) e.text = says(seat, cardName(card) + " attı", cardName(card) + " attın");
        push(std::move(e));
    }
    if (trick_.cards.size() < 2) {
        current_ = 1 - seat;
        GameEvent e;
        e.type = EvType::TurnStart;
        e.seat = current_;
        push(std::move(e));
        return ActionResult::success();
    }
    finishTrick();
    return ActionResult::success();
}

void Game::finishTrick() {
    const TrickCard a = trick_.cards[0], b = trick_.cards[1];
    const int w = beats(b.card, a.card, trump_) ? b.seat : a.seat;
    trick_.winner = w;
    won_[(size_t)w].push_back(a.card);
    won_[(size_t)w].push_back(b.card);
    tricks_.push_back(trick_);
    trick_.cards.clear(); // (the finished trick is tricks_.back())
    {
        GameEvent e;
        e.type = EvType::TrickWon;
        e.seat = w;
        if (!silent_) {
            e.cards = {a.card, b.card};
            e.text = says(w, "eli aldı", "eli aldın");
        }
        push(std::move(e));
    }
    if (second_) {
        if (hand_[0].empty() && hand_[1].empty()) {
            lastPts_[(size_t)w] += rules_.lastTrick;
            endHand();
            return;
        }
        beginTurn(w);
        return;
    }
    current_ = w;
    declaredNow_ = false;
    if (bestMeldPoints(w) > 0 || holdsKoz7(w)) {
        stage_ = Stage::Declare;
        GameEvent e;
        e.type = EvType::DeclareTurn;
        e.seat = w;
        push(std::move(e));
        return;
    }
    drawCards(w);
}

// ---- declarations ----

bool Game::kozSevenSwaps() const { return turnUp_ >= 0 && rankOf(turnUp_) != 7 && !stock_.empty(); }

bool Game::canKoz7(int seat) const {
    if (stage_ != Stage::Declare || seat != current_) return false;
    return holdsKoz7(seat);
}

bool Game::holdsKoz7(int seat) const {
    if (second_) return false;
    for (int c : hand_[(size_t)seat])
        if (suitOf(c) == trump_ && rankOf(c) == 7 && !sevenDone_[(size_t)c]) return true;
    return false;
}

std::vector<Meld> Game::availableMelds(int seat) const {
    std::vector<Meld> out;
    scanMelds(seat, &out);
    std::stable_sort(out.begin(), out.end(), [](const Meld& a, const Meld& b) { return a.points > b.points; });
    return out;
}

int Game::bestMeldPoints(int seat) const { return scanMelds(seat, nullptr); }

// Every combination `seat` could declare now, one canonical card set each (table cards first: they are shown already;
// one card at least from the hand). Returns the biggest one's points (0: none); `out` (optional) gets them all.
int Game::scanMelds(int seat, std::vector<Meld>* out) const {
    if (seat < 0 || seat > 1 || second_ || declaredNow_) return 0;
    if (stage_ != Stage::Declare && stage_ != Stage::Playing) return 0;
    const std::vector<int>& hand = hand_[(size_t)seat];
    const std::vector<int>& table = table_[(size_t)seat];
    int best = 0;
    auto build = [&](MeldKind k, int suit) {
        int pat[5];
        int np = 0;
        {
            using namespace kart;
            switch (k) {
            case MeldKind::Bezik: pat[0] = BEZIK_QUEEN, pat[1] = BEZIK_JACK, np = 2; break;
            case MeldKind::CiftBezik: pat[0] = pat[1] = BEZIK_QUEEN, pat[2] = pat[3] = BEZIK_JACK, np = 4; break;
            case MeldKind::Seri:
                pat[0] = makeCard(trump_, As), pat[1] = makeCard(trump_, 10), pat[2] = makeCard(trump_, Papaz);
                pat[3] = makeCard(trump_, Kiz), pat[4] = makeCard(trump_, Vale), np = 5;
                break;
            case MeldKind::Evlilik:
            case MeldKind::KozEvlilik: pat[0] = makeCard(suit, Papaz), pat[1] = makeCard(suit, Kiz), np = 2; break;
            default: pat[0] = pat[1] = pat[2] = pat[3] = -fourRank(k), np = 4; break;
            }
        }
        const uint16_t block = blockMask(k);
        int cards[5];
        bool fromHand = false;
        auto taken = [&](int c, int n) {
            for (int j = 0; j < n; ++j)
                if (cards[j] == c) return true;
            return false;
        };
        for (int i = 0; i < np; ++i) {
            int pick = -1;
            for (int pass = 0; pass < 2 && pick < 0; ++pass) {
                const std::vector<int>& pool = pass == 0 ? table : hand;
                for (int c : pool) {
                    if (!slotFits(pat[i], c) || (used_[(size_t)c] & block) || taken(c, i)) continue;
                    pick = c;
                    fromHand = fromHand || pass == 1;
                    break;
                }
            }
            if (pick < 0) return;
            cards[i] = pick;
        }
        if (!fromHand) { // one card at least must come from the hand: swap one slot for a hand card that fits
            bool done = false;
            for (int i = 0; i < np && !done; ++i)
                for (int c : hand) {
                    if (!slotFits(pat[i], c) || (used_[(size_t)c] & block) || taken(c, np)) continue;
                    cards[i] = c;
                    done = true;
                    break;
                }
            if (!done) return;
        }
        const int pts = rules_.points[(size_t)k];
        best = std::max(best, pts);
        if (out) {
            Meld m;
            m.kind = k;
            m.cards.assign(cards, cards + np);
            m.points = pts;
            out->push_back(std::move(m));
        }
    };
    build(MeldKind::CiftBezik, 0);
    build(MeldKind::Seri, 0);
    build(MeldKind::DortAs, 0);
    build(MeldKind::DortPapaz, 0);
    build(MeldKind::DortKiz, 0);
    build(MeldKind::Bezik, 0);
    build(MeldKind::DortVale, 0);
    build(MeldKind::KozEvlilik, trump_);
    for (int s = 0; s < 4; ++s)
        if (s != trump_) build(MeldKind::Evlilik, s);
    return best;
}

bool Game::meldValid(int seat, const Meld& m, std::string* why) const {
    auto fail = [&](const char* t) {
        if (why) *why = t;
        return false;
    };
    if ((int)m.kind < 0 || (int)m.kind >= NUM_MELDS) return fail("Böyle bir deklarasyon yok.");
    int suit = trump_;
    if (m.kind == MeldKind::Evlilik) {
        if (m.cards.empty() || !isCard(m.cards[0])) return fail("Bu kâğıtlar bu deklarasyonu yapmaz.");
        suit = suitOf(m.cards[0]);
        if (suit == trump_) return fail("Kozun evliliği koz evliliğidir.");
    }
    std::vector<int> pat = pattern(m.kind, trump_, suit);
    if (pat.size() != m.cards.size()) return fail("Bu kâğıtlar bu deklarasyonu yapmaz.");
    // the cards must match the pattern (a multiset)
    std::vector<bool> taken(pat.size(), false);
    for (size_t i = 0; i < m.cards.size(); ++i) {
        const int c = m.cards[i];
        if (!isCard(c)) return fail("Bu kâğıtlar bu deklarasyonu yapmaz.");
        for (size_t j = 0; j < i; ++j)
            if (m.cards[j] == c) return fail("Aynı kâğıt iki kez sayılmaz.");
        bool ok = false;
        for (size_t j = 0; j < pat.size() && !ok; ++j)
            if (!taken[j] && slotFits(pat[j], c)) {
                taken[j] = true;
                ok = true;
            }
        if (!ok) return fail("Bu kâğıtlar bu deklarasyonu yapmaz.");
    }
    const uint16_t block = blockMask(m.kind);
    bool fromHand = false;
    for (int c : m.cards) {
        const bool h = has(seat, c);
        if (!h && !inTable(seat, c)) return fail("Bu kâğıtlar sende değil.");
        fromHand = fromHand || h;
        if (used_[(size_t)c] & block) return fail("Bir kâğıt aynı deklarasyonda iki kez kullanılmaz.");
    }
    if (!fromHand) return fail("Deklarasyona elinden en az bir yeni kâğıt katmalısın.");
    return true;
}

ActionResult Game::declare(int seat, const Meld& m0) {
    if (stage_ != Stage::Declare) return ActionResult::fail("Şimdi deklarasyon yapılmaz: önce el almalısın.");
    if (seat != current_) return ActionResult::fail("Deklarasyonu eli alan yapar.");
    if (declaredNow_) return ActionResult::fail("Her el için tek deklarasyon yapılır.");
    std::string why;
    if (!meldValid(seat, m0, &why)) return ActionResult::fail(why);
    Meld m = m0;
    m.points = rules_.points[(size_t)m.kind];
    for (int c : m.cards) {
        used_[(size_t)c] |= (uint16_t)(1u << (int)m.kind);
        if (has(seat, c)) {
            auto& h = hand_[(size_t)seat];
            h.erase(std::find(h.begin(), h.end(), c));
            auto& x = exposed_[(size_t)seat];
            x.erase(std::remove(x.begin(), x.end(), c), x.end());
            table_[(size_t)seat].push_back(c);
        }
    }
    meldPts_[(size_t)seat] += m.points;
    if (m.points > bestMeld_[(size_t)seat]) {
        bestMeld_[(size_t)seat] = m.points;
        bestName_[(size_t)seat] = meldNameTR(m.kind);
    }
    declared_.push_back({seat, m});
    logAction(LogKind::Declare, seat, (int)m.kind, m.cards);
    declaredNow_ = true;
    {
        GameEvent e;
        e.type = EvType::Declared;
        e.seat = seat;
        e.meld = m.kind;
        e.amount = m.points;
        e.cards = m.cards;
        if (!silent_) {
            const std::string what = std::string(meldNameTR(m.kind)) + " +" + std::to_string(m.points);
            e.text = human_[(size_t)seat] ? what : names_[(size_t)seat] + ": " + what;
        }
        push(std::move(e));
    }
    endDeclare();
    return ActionResult::success();
}

ActionResult Game::koz7(int seat) {
    if (stage_ != Stage::Declare) return ActionResult::fail("Koz yedilisi el alınca gösterilir.");
    if (seat != current_) return ActionResult::fail("Koz yedilisini eli alan gösterir.");
    int seven = -1;
    for (int c : hand_[(size_t)seat])
        if (suitOf(c) == trump_ && rankOf(c) == 7 && !sevenDone_[(size_t)c]) {
            seven = c;
            break;
        }
    if (seven < 0) return ActionResult::fail("Elinde sayılmamış koz yedilisi yok.");
    const bool swap = kozSevenSwaps();
    sevenDone_[(size_t)seven] = true;
    meldPts_[(size_t)seat] += rules_.seven;
    logAction(LogKind::Koz7, seat, seven);
    GameEvent e;
    e.type = EvType::Koz7;
    e.seat = seat;
    e.card = seven;
    e.swapped = swap;
    e.amount = rules_.seven;
    if (swap) {
        auto& h = hand_[(size_t)seat];
        h.erase(std::find(h.begin(), h.end(), seven));
        const int old = turnUp_;
        turnUp_ = seven;
        h.push_back(old);
        sortHand(seat);
        exposed_[(size_t)seat].push_back(old);
        e.card2 = old;
        e.text = says(seat, "koz yedilisiyle " + cardAccusative(old) + " aldı: +10", "koz yedilisiyle " + cardAccusative(old) + " aldın: +10");
    } else {
        exposed_[(size_t)seat].push_back(seven);
        e.text = says(seat, "koz yedilisini gösterdi: +10", "koz yedilisini gösterdin: +10");
    }
    push(std::move(e));
    if (declaredNow_ || (bestMeldPoints(seat) == 0 && !canKoz7(seat))) endDeclare();
    return ActionResult::success();
}

ActionResult Game::pass(int seat) {
    if (stage_ != Stage::Declare) return ActionResult::fail("Şimdi geçilecek bir şey yok.");
    if (seat != current_) return ActionResult::fail("Sıra sende değil.");
    logAction(LogKind::Pass, seat, 0);
    GameEvent e;
    e.type = EvType::Passed;
    e.seat = seat;
    push(std::move(e));
    endDeclare();
    return ActionResult::success();
}

void Game::endDeclare() { drawCards(current_); }

void Game::drawCards(int winner) {
    for (int k = 0; k < 2; ++k) {
        const int s = k == 0 ? winner : 1 - winner;
        int card = -1;
        bool up = false;
        if (!stock_.empty()) {
            card = stock_.back();
            stock_.pop_back();
        } else if (turnUp_ >= 0) {
            card = turnUp_;
            turnUp_ = -1;
            up = true;
            exposed_[(size_t)s].push_back(card);
        }
        if (card < 0) continue;
        hand_[(size_t)s].push_back(card);
        sortHand(s);
        GameEvent e;
        e.type = EvType::Draw;
        e.seat = s;
        e.card = card;
        e.turnUp = up;
        push(std::move(e));
    }
    if (stock_.empty() && turnUp_ < 0) enterSecondStage();
    beginTurn(winner);
}

void Game::enterSecondStage() {
    second_ = true;
    for (int s = 0; s < 2; ++s) {
        for (int c : table_[(size_t)s]) {
            hand_[(size_t)s].push_back(c);
            exposed_[(size_t)s].push_back(c);
        }
        table_[(size_t)s].clear();
        sortHand(s);
    }
    GameEvent e;
    e.type = EvType::Stage2;
    e.text = "Deste bitti: artık renge uymak ve eli geçmek zorunlu";
    push(std::move(e));
}

void Game::endHand() {
    for (int s = 0; s < 2; ++s) {
        int b = 0;
        for (int c : won_[(size_t)s]) b += isBrisque(c) ? rules_.brisque : 0;
        brisquePts_[(size_t)s] = b;
    }
    HandRecord r;
    r.index = handIndex_;
    r.dealer = dealer_;
    r.trump = trump_;
    std::array<int, 2> pts{};
    for (int s = 0; s < 2; ++s) {
        r.melds[(size_t)s] = meldPts_[(size_t)s];
        r.brisques[(size_t)s] = brisquePts_[(size_t)s];
        r.last[(size_t)s] = lastPts_[(size_t)s];
        pts[(size_t)s] = handPoints(s);
        r.points[(size_t)s] = pts[(size_t)s];
        totals_[(size_t)s] += pts[(size_t)s];
        r.totals[(size_t)s] = totals_[(size_t)s];
        r.best[(size_t)s] = bestMeld_[(size_t)s];
        r.bestName[(size_t)s] = bestName_[(size_t)s];
    }
    sheet_.push_back(r);
    const bool over = std::max(totals_[0], totals_[1]) >= rules_.target && totals_[0] != totals_[1];
    stage_ = over ? Stage::MatchOver : Stage::HandOver;
    current_ = -1;
    {
        GameEvent e;
        e.type = EvType::HandEnd;
        e.points = pts;
        if (!silent_) {
            e.text = std::to_string(handIndex_ + 1) + ". el bitti";
            for (int s = 0; s < 2; ++s)
                e.lines.push_back(names_[(size_t)s] + ": deklarasyon " + std::to_string(meldPts_[(size_t)s]) + ", brisk " +
                                  std::to_string(brisquePts_[(size_t)s]) + (lastPts_[(size_t)s] ? ", son el 10" : "") +
                                  " = " + std::to_string(pts[(size_t)s]) + " (toplam " + std::to_string(totals_[(size_t)s]) + ")");
        }
        push(std::move(e));
    }
    if (over) {
        GameEvent e;
        e.type = EvType::MatchEnd;
        e.seat = winner();
        e.points = totals_;
        if (!silent_)
            e.text = human_[(size_t)e.seat] ? "Maçı kazandın: " + std::to_string(totals_[(size_t)e.seat])
                                             : names_[(size_t)e.seat] + " maçı kazandı: " + std::to_string(totals_[(size_t)e.seat]);
        push(std::move(e));
    }
}

int Game::winner() const { return totals_[1] > totals_[0] ? 1 : 0; }

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Play: return playCard(a.seat, a.value).ok;
    case LogKind::Declare: {
        if (a.value < 0 || a.value >= NUM_MELDS) return false;
        Meld m;
        m.kind = (MeldKind)a.value;
        m.cards = a.cards;
        return declare(a.seat, m).ok;
    }
    case LogKind::Koz7: return koz7(a.seat).ok;
    case LogKind::Pass: return pass(a.seat).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

void Game::debugSetDeal(const std::array<std::vector<int>, 2>& hands, const std::vector<int>& stock, int turnUp) {
    for (int s = 0; s < 2; ++s) {
        hand_[(size_t)s] = hands[(size_t)s];
        table_[(size_t)s].clear();
        exposed_[(size_t)s].clear();
        sortHand(s);
    }
    stock_ = stock;
    turnUp_ = turnUp;
    if (turnUp >= 0) trump_ = suitOf(turnUp);
    used_.fill(0);
    sevenDone_.fill(false);
    meldPts_ = {0, 0};
    declared_.clear();
}

} // namespace bezik
