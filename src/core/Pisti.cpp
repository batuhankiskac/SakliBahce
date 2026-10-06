// Pişti rules engine. Rules: docs/kurallar_pisti.md, semantics: Pisti.h.
#include "core/Pisti.h"

#include <algorithm>

namespace pisti {

using namespace kart;

namespace {

constexpr size_t MAX_QUEUED_EVENTS = 20000; // safety valve for headless loops that never drain
constexpr int CARDS_PER_DEAL = 4;

// Minimal UTF-8 decoder for the vowel harmony of player names.
std::vector<unsigned> codePoints(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int extra = 0;
        unsigned cp = c;
        if (c >= 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else if (c >= 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if (c >= 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        }
        if (i + extra >= s.size()) extra = 0;
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(extra ? cp : c);
        i += 1 + extra;
    }
    return out;
}

bool isVowel(unsigned c) {
    switch (c) {
    case 'a': case 'A': case 'o': case 'O': case 'u': case 'U': case 'I': case 0x131:
    case 'e': case 'E': case 'i': case 0x130: case 0xF6: case 0xD6: case 0xFC: case 0xDC:
        return true;
    default:
        return false;
    }
}

// true: the last vowel is a back vowel (a ı o u) -> suffixes with "a"; false -> "e".
bool backHarmony(const std::vector<unsigned>& cps) {
    bool back = true;
    for (unsigned c : cps) {
        switch (c) {
        case 'a': case 'A': case 'o': case 'O': case 'u': case 'U': case 'I': case 0x131:
            back = true;
            break;
        case 'e': case 'E': case 'i': case 0x130: case 0xF6: case 0xD6: case 0xFC: case 0xDC:
            back = false;
            break;
        default:
            break;
        }
    }
    return back;
}

// "Hacı Rıza'da", "Kel Mahmut'ta", "Emekli Nuri'de".
std::string locative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
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
    return name + "'" + (hard ? "t" : "d") + (backHarmony(cps) ? "a" : "e");
}

// "Hacı Rıza'ya", "Kel Mahmut'a", "Emekli Nuri'ye".
std::string dative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    const bool vowelEnd = !cps.empty() && isVowel(cps.back());
    return name + "'" + (vowelEnd ? "y" : "") + (backHarmony(cps) ? "a" : "e");
}

std::string capitalizeFirst(const std::string& s) {
    if (s.empty()) return s;
    if (s[0] == 'i') return "İ" + s.substr(1);
    if (s[0] >= 'a' && s[0] <= 'z') return std::string(1, (char)(s[0] - 'a' + 'A')) + s.substr(1);
    static const char* const kPairs[][2] = {{"ı", "I"}, {"ç", "Ç"}, {"ş", "Ş"}, {"ğ", "Ğ"}, {"ö", "Ö"}, {"ü", "Ü"}};
    for (const auto& p : kPairs) {
        const std::string lower = p[0];
        if (s.compare(0, lower.size(), lower) == 0) return p[1] + s.substr(lower.size());
    }
    return s;
}

std::string cardList(const std::vector<int>& cards) {
    std::string s;
    for (size_t i = 0; i < cards.size(); ++i) s += (i ? ", " : "") + cardNameTR(cards[i]);
    return s;
}

uint64_t handSeed(uint64_t seed, int hand) {
    uint64_t z = seed + 0xD1B54A32D192ED03ull * (uint64_t)(hand + 1);
    z = (z ^ (z >> 31)) * 0x9E3779B97F4A7C15ull;
    return z ^ (z >> 29);
}

} // namespace

int cardPoints(int card) {
    if (!isValidCard(card)) return 0;
    const int r = rankOf(card);
    if (r == As || r == Vale) return 1;
    if (card == makeCard(Sinek, 2)) return 2;
    if (card == makeCard(Karo, 10)) return 3;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
// setup

Game::Game(const Rules& rules) {
    static const char* const kNames[4] = {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"};
    for (int s = 0; s < 4; ++s) {
        names_[s] = kNames[s];
        human_[s] = (s == 0);
    }
    setRules(rules);
}

void Game::setRules(const Rules& r) {
    rules_ = r;
    if (rules_.targetScore < 1) rules_.targetScore = 1;
    setupSeats();
}

void Game::setupSeats() {
    active_.clear();
    side_.fill(-1);
    switch (rules_.mode) {
    case Mode::Bireysel:
        active_ = {0, 1, 2, 3};
        for (int s = 0; s < 4; ++s) side_[s] = s;
        numSides_ = 4;
        break;
    case Mode::Esli:
        active_ = {0, 1, 2, 3};
        for (int s = 0; s < 4; ++s) side_[s] = s % 2;
        numSides_ = 2;
        break;
    case Mode::Ikili:
        active_ = {0, 2};
        side_[0] = 0;
        side_[2] = 1;
        numSides_ = 2;
        break;
    }
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat >= 4) return;
    names_[seat] = name;
    human_[seat] = human;
}

void Game::startMatch(uint64_t seed) {
    seed_ = seed;
    log_.clear();
    totals_.fill(0);
    winnerSide_ = -1;
    handIndex_ = 0;
    turnNumber_ = 0;
    lastResult_ = HandResult();
    Rng r(seed);
    firstDealer_ = active_[r.range((int)active_.size())];
    dealer_ = firstDealer_;

    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = "Pişti başlıyor · " + std::to_string(rules_.targetScore) + " puana kadar";
    if (rules_.mode == Mode::Esli) e.text += " · eşli";
    else if (rules_.mode == Mode::Ikili) e.text += " · iki kişilik";
    push(std::move(e));
    beginHand();
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    log_.push_back({LogKind::NextHand, -1, -1});
    ++handIndex_;
    dealer_ = nextActive(dealer_);
    beginHand();
}

void Game::beginHand() {
    for (int s = 0; s < 4; ++s) {
        hands_[s].clear();
        captured_[s].clear();
    }
    pistis_.fill(0);
    jackPistis_.fill(0);
    pistiPts_.fill(0);
    open_.clear();
    closed_.clear();
    buried_.clear();
    seen_.fill(false);
    lastCapturer_ = -1;
    dealRound_ = -1;
    stage_ = Stage::Playing;
    current_ = -1;

    if (!nextDeck_.empty()) {
        deck_ = nextDeck_;
        nextDeck_.clear();
    } else {
        Rng r(handSeed(seed_, handIndex_));
        deck_ = shuffledDeck(r);
    }

    GameEvent hs;
    hs.type = EvType::HandStart;
    hs.seat = dealer_;
    hs.count = handIndex_;
    hs.text = std::to_string(handIndex_ + 1) + ". el · " +
              (human_[dealer_] ? std::string("Dağıtan sensin") : "Dağıtan " + names_[dealer_]);
    push(std::move(hs));

    // Four cards to the table: three closed, the fourth turned face up.
    for (int i = 0; i < 3 && !deck_.empty(); ++i) {
        closed_.push_back(deck_.back());
        deck_.pop_back();
    }
    int up = -1;
    if (!deck_.empty()) {
        up = deck_.back();
        deck_.pop_back();
    }
    GameEvent d;
    d.type = EvType::Deal;
    d.seat = -1;
    d.count = (int)closed_.size() + (up >= 0 ? 1 : 0);
    d.deckLeft = (int)deck_.size();
    d.text = "Yere 4 kart: 3 kapalı, 1 açık";
    push(std::move(d));

    while (up >= 0) {
        GameEvent t;
        t.type = EvType::TableTurnUp;
        t.card = up;
        const bool bury = rules_.reTurnOpeningJack && rankOf(up) == Vale && !deck_.empty();
        if (bury) {
            t.jack = true;
            t.text = "Açılan kart " + cardNameTR(up) + ": destenin altına kondu, yeni kart açılıyor";
            push(std::move(t));
            buried_.push_back(up);
            deck_.insert(deck_.begin(), up);
            up = deck_.back();
            deck_.pop_back();
        } else {
            t.text = "Yerde açılan kart: " + cardNameTR(up);
            push(std::move(t));
            open_.push_back(up);
            seen_[up] = true;
            up = -1;
        }
    }

    dealCards();
    beginTurn(nextActive(dealer_));
}

void Game::dealCards() {
    ++dealRound_;
    int s = nextActive(dealer_);
    for (size_t k = 0; k < active_.size(); ++k, s = nextActive(s)) {
        GameEvent d;
        d.type = EvType::Deal;
        d.seat = s;
        for (int i = 0; i < CARDS_PER_DEAL && !deck_.empty(); ++i) {
            hands_[s].push_back(deck_.back());
            if (human_[s]) d.cards.push_back(deck_.back());
            deck_.pop_back();
        }
        d.count = CARDS_PER_DEAL;
        d.deckLeft = (int)deck_.size();
        d.text = human_[s] ? std::string("Sana 4 kart geldi") : dative(names_[s]) + " 4 kart";
        push(std::move(d));
    }
}

void Game::beginTurn(int seat) {
    current_ = seat;
    ++turnNumber_;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = seat;
    e.side = sideOf(seat);
    e.text = human_[seat] ? std::string("Sıra sende") : "Sıra " + locative(names_[seat]);
    push(std::move(e));
}

// ---------------------------------------------------------------------------------------------------------
// queries

bool Game::isActive(int seat) const { return seat >= 0 && seat < 4 && side_[seat] >= 0; }

int Game::nextActive(int seat) const {
    for (int k = 1; k <= 4; ++k) {
        const int s = (seat + k) % 4;
        if (isActive(s)) return s;
    }
    return seat;
}

int Game::sideOf(int seat) const { return seat >= 0 && seat < 4 ? side_[seat] : -1; }

std::vector<int> Game::seatsOfSide(int side) const {
    std::vector<int> v;
    for (int s : active_)
        if (side_[s] == side) v.push_back(s);
    return v;
}

bool Game::humanOnSide(int side) const {
    for (int s : seatsOfSide(side))
        if (human_[s]) return true;
    return false;
}

std::string Game::sideName(int side) const {
    const std::vector<int> seats = seatsOfSide(side);
    if (seats.empty()) return "?";
    if (seats.size() == 1) return names_[seats[0]];
    // Put the human first: "Sen ve Kel Mahmut".
    int a = seats[0], b = seats[1];
    if (human_[b] && !human_[a]) std::swap(a, b);
    return names_[a] + " ve " + names_[b];
}

int Game::sideCapturedCount(int side) const {
    int n = 0;
    for (int s : seatsOfSide(side)) n += (int)captured_[s].size();
    return n;
}

bool Game::isLastCardOfHand() const {
    if (stage_ != Stage::Playing || !deck_.empty()) return false;
    int n = 0;
    for (int s : active_) n += (int)hands_[s].size();
    return n == 1;
}

bool Game::wouldCapture(int card) const {
    if (!isValidCard(card) || tableCount() == 0) return false;
    if (rankOf(card) == Vale) return true;
    return !open_.empty() && rankOf(open_.back()) == rankOf(card);
}

int Game::wouldPisti(int card) const {
    if (!isValidCard(card) || open_.size() != 1 || !closed_.empty()) return 0;
    if (isLastCardOfHand() && !rules_.lastCardPisti) return 0;
    const int top = open_.back();
    if (rankOf(card) == rankOf(top)) return rankOf(card) == Vale ? rules_.jackPistiPoints : rules_.pistiPoints;
    if (rankOf(card) == Vale && rules_.jackPistiOnAny) return rules_.pistiPoints;
    return 0;
}

int Game::leaderSide() const {
    int best = 0;
    for (int i = 1; i < numSides_; ++i)
        if (totals_[i] > totals_[best]) best = i;
    return best;
}

// ---------------------------------------------------------------------------------------------------------
// action

ActionResult Game::playCardImpl(int seat, int card) {
    if (seat < 0 || seat >= 4 || !isActive(seat)) return ActionResult::fail("Bu oyuncu bu oyunda yok");
    if (stage_ != Stage::Playing) return ActionResult::fail("Şu an kart oynanmıyor");
    if (seat != current_)
        return ActionResult::fail(human_[current_] ? std::string("Sıra sende") : "Sıra " + locative(names_[current_]));
    std::vector<int>& h = hands_[seat];
    auto it = std::find(h.begin(), h.end(), card);
    if (!isValidCard(card) || it == h.end())
        return ActionResult::fail(human_[seat] ? std::string("Bu kart elinde yok") : names_[seat] + " bu kartı tutmuyor");

    const bool capture = wouldCapture(card);
    const int pistiPts = wouldPisti(card);
    const bool rankMatch = !open_.empty() && rankOf(open_.back()) == rankOf(card);
    h.erase(it);
    seen_[card] = true;

    GameEvent p;
    p.type = EvType::Play;
    p.seat = seat;
    p.side = sideOf(seat);
    p.card = card;
    p.text = says(seat, cardNameTR(card) + " attı", cardNameTR(card) + " attın");
    push(std::move(p));

    if (capture) {
        const int tableBefore = tableCount();
        const int closedTaken = (int)closed_.size();
        std::vector<int> taken = closed_;
        for (int c : closed_) seen_[c] = true;
        taken.insert(taken.end(), open_.begin(), open_.end());
        taken.push_back(card);
        closed_.clear();
        open_.clear();
        captured_[seat].insert(captured_[seat].end(), taken.begin(), taken.end());
        lastCapturer_ = seat;

        const bool byJack = rankOf(card) == Vale;
        GameEvent c;
        c.type = EvType::Capture;
        c.seat = seat;
        c.side = sideOf(seat);
        c.card = card;
        c.count = (int)taken.size();
        c.closed = closedTaken;
        c.pisti = pistiPts > 0;
        c.jack = byJack;
        c.cards = taken;
        const std::string n = std::to_string(tableBefore);
        if (byJack && !rankMatch)
            c.text = says(seat, "Vale ile yerdeki " + n + " kartı aldı", "Vale ile yerdeki " + n + " kartı aldın");
        else
            c.text = says(seat, "yerdeki " + n + " kartı aldı", "yerdeki " + n + " kartı aldın");
        if (closedTaken > 0)
            c.text += " · kapalılar: " + cardList(std::vector<int>(taken.begin(), taken.begin() + closedTaken));
        push(std::move(c));

        if (pistiPts > 0) {
            const bool jackPisti = byJack && rankMatch;
            ++pistis_[seat];
            if (jackPisti) ++jackPistis_[seat];
            pistiPts_[seat] += pistiPts;
            GameEvent pe;
            pe.type = EvType::Pisti;
            pe.seat = seat;
            pe.side = sideOf(seat);
            pe.card = card;
            pe.points = pistiPts;
            pe.jack = jackPisti;
            const std::string pts = " +" + std::to_string(pistiPts);
            if (jackPisti) pe.text = says(seat, "valeyle pişti yaptı!" + pts, "valeyle pişti yaptın!" + pts);
            else pe.text = says(seat, "pişti yaptı!" + pts, "pişti yaptın!" + pts);
            push(std::move(pe));
        }
    } else {
        open_.push_back(card);
    }

    // Next: same deal continues, or a new deal, or the hand is over.
    bool handsEmpty = true;
    for (int s : active_)
        if (!hands_[s].empty()) handsEmpty = false;
    if (handsEmpty && deck_.empty()) {
        endHand();
        return ActionResult::success();
    }
    if (handsEmpty) dealCards();
    beginTurn(nextActive(seat));
    return ActionResult::success();
}

void Game::endHand() {
    current_ = -1;
    HandResult r;
    r.handIndex = handIndex_;
    r.dealer = dealer_;
    r.numSides = numSides_;

    // The cards left on the table go to the last player who captured (nobody captured: the dealer).
    if (tableCount() > 0) {
        const int to = lastCapturer_ >= 0 ? lastCapturer_ : dealer_;
        std::vector<int> rest = closed_;
        rest.insert(rest.end(), open_.begin(), open_.end());
        for (int c : rest) seen_[c] = true;
        closed_.clear();
        open_.clear();
        captured_[to].insert(captured_[to].end(), rest.begin(), rest.end());
        r.lastCapturer = to;
        r.leftoverCards = (int)rest.size();
        GameEvent e;
        e.type = EvType::LastCapture;
        e.seat = to;
        e.side = sideOf(to);
        e.count = (int)rest.size();
        e.cards = rest;
        const std::string n = std::to_string(rest.size());
        if (human_[to]) e.text = "Yerde kalan " + n + " kart sana kaldı (son alan sendin)";
        else e.text = "Yerde kalan " + n + " kart son alan " + dative(names_[to]) + " gitti";
        push(std::move(e));
    }

    for (int s : active_) {
        ScoreLine& L = r.seat[s];
        for (int c : captured_[s]) {
            ++L.cards;
            if (rankOf(c) == As) ++L.aces;
            if (rankOf(c) == Vale) ++L.jacks;
            if (c == makeCard(Sinek, 2)) L.sinek2 = 2;
            if (c == makeCard(Karo, 10)) L.karo10 = 3;
        }
        L.pistis = pistis_[s] - jackPistis_[s];
        L.jackPistis = jackPistis_[s];
        L.pistiPoints = pistiPts_[s];
        ScoreLine& S = r.side[side_[s]];
        S.cards += L.cards;
        S.aces += L.aces;
        S.jacks += L.jacks;
        S.sinek2 += L.sinek2;
        S.karo10 += L.karo10;
        S.pistis += L.pistis;
        S.jackPistis += L.jackPistis;
        S.pistiPoints += L.pistiPoints;
    }
    // Majority: strictly the most cards; a tie for the most gives nobody the points.
    int best = -1, bestCards = -1;
    bool tie = false;
    for (int i = 0; i < numSides_; ++i) {
        if (r.side[i].cards > bestCards) {
            best = i;
            bestCards = r.side[i].cards;
            tie = false;
        } else if (r.side[i].cards == bestCards) {
            tie = true;
        }
    }
    if (!tie && best >= 0) {
        r.majoritySide = best;
        r.side[best].majority = rules_.majorityPoints;
        if (rules_.mode != Mode::Esli)
            for (int s : seatsOfSide(best)) r.seat[s].majority = rules_.majorityPoints;
    }
    auto sumUp = [](ScoreLine& L) { L.total = L.aces + L.jacks + L.sinek2 + L.karo10 + L.majority + L.pistiPoints; };
    for (auto& L : r.seat) sumUp(L);
    for (auto& L : r.side) sumUp(L);
    for (int i = 0; i < numSides_; ++i) totals_[i] += r.side[i].total;
    r.totalsAfter = totals_;

    // Match over: some side reached the target and is strictly ahead (a tie on top plays another hand).
    int lead = leaderSide();
    bool leadTie = false;
    for (int i = 0; i < numSides_; ++i)
        if (i != lead && totals_[i] == totals_[lead]) leadTie = true;
    if (totals_[lead] >= rules_.targetScore && !leadTie) {
        r.matchOver = true;
        r.winnerSide = lead;
        winnerSide_ = lead;
    }
    lastResult_ = r;
    stage_ = r.matchOver ? Stage::MatchOver : Stage::HandOver;

    GameEvent he;
    he.type = EvType::HandEnd;
    he.count = handIndex_;
    he.side = r.majoritySide;
    he.text = std::to_string(handIndex_ + 1) + ". el bitti ·";
    for (int i = 0; i < numSides_; ++i)
        he.text += std::string(i ? "," : "") + " " + sideName(i) + " " + std::to_string(r.side[i].total);
    push(std::move(he));

    if (r.matchOver) {
        GameEvent m;
        m.type = EvType::MatchEnd;
        m.side = lead;
        m.seat = seatsOfSide(lead).front();
        m.points = totals_[lead];
        const std::string pts = " (" + std::to_string(totals_[lead]) + ")";
        if (humanOnSide(lead))
            m.text = seatsOfSide(lead).size() > 1 ? "Maç bitti! Kazandınız!" + pts : "Maç bitti! Kazandın!" + pts;
        else
            m.text = "Maç bitti! " + sideName(lead) + " kazandı" + pts;
        push(std::move(m));
    }
}

// ---------------------------------------------------------------------------------------------------------
// events / text

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::push(GameEvent e) {
    if (events_.size() >= MAX_QUEUED_EVENTS) events_.erase(events_.begin(), events_.begin() + MAX_QUEUED_EVENTS / 2);
    events_.push_back(std::move(e));
}

// Turkish drops the pronoun: the human reads "Pişti yaptın! +10", the others "Hacı Rıza pişti yaptı! +10".
std::string Game::says(int seat, const std::string& third, const std::string& second) const {
    return human_[seat] ? capitalizeFirst(second) : names_[seat] + " " + third;
}

// ---------------------------------------------------------------------------------------------------------
// the match's action log (save / resume)

ActionResult Game::playCard(int seat, int card) {
    ActionResult r = playCardImpl(seat, card);
    if (r.ok) log_.push_back({LogKind::Play, seat, card});
    return r;
}

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Play: return playCard(a.seat, a.card).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

std::string LoggedAction::encode() const {
    return std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(card);
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    int v[3] = {0, 0, 0};
    if (!parseInts(line, v, 3)) return false;
    if (v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)v[0];
    out.seat = v[1];
    out.card = v[2];
    return true;
}

} // namespace pisti
