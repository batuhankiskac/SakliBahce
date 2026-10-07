// Altmışaltı (66) engine (see Altmisalti.h, docs/kurallar_altmisalti.md).
#include "core/Altmisalti.h"

#include <algorithm>

namespace altmisalti {

using kart::rankOf;
using kart::suitOf;

namespace {

std::string capitalizeFirst(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 'a' + 'A');
    return s;
}

int nineOf(int suit) { return kart::makeCard(suit, 9); }
int partnerOf(int card) { return kart::makeCard(suitOf(card), rankOf(card) == kart::Kiz ? kart::Papaz : kart::Kiz); }

} // namespace

CardMask maskOf(const std::vector<int>& cards) {
    CardMask m = 0;
    for (int c : cards)
        if (kart::isValidCard(c)) m |= cardBit(c);
    return m;
}

std::vector<int> cardsOf(CardMask m) {
    std::vector<int> v;
    while (m) {
        const int c = lowestCard(m);
        v.push_back(c);
        m &= m - 1;
    }
    return v;
}

int cardPoints(int card) {
    switch (rankOf(card)) {
    case kart::As: return 11;
    case 10: return 10;
    case kart::Papaz: return 4;
    case kart::Kiz: return 3;
    case kart::Vale: return 2;
    default: return 0;
    }
}

int cardOrder(int card) {
    switch (rankOf(card)) {
    case kart::As: return 5;
    case 10: return 4;
    case kart::Papaz: return 3;
    case kart::Kiz: return 2;
    case kart::Vale: return 1;
    default: return 0;
    }
}

bool beats(int first, int second, int trumpSuit) {
    if (suitOf(second) == suitOf(first)) return cardOrder(second) > cardOrder(first);
    return suitOf(second) == trumpSuit;
}

// ---------------------------------------------------------------------------------------------------------
// Deal
// ---------------------------------------------------------------------------------------------------------

CardMask Deal::legal(int p) const {
    const CardMask h = hand[(size_t)p];
    if (lead < 0 || !strict()) return h;
    const CardMask same = h & suitMask(suitOf(lead));
    if (same) {
        CardMask higher = 0;
        for (CardMask m = same; m; m &= m - 1) {
            const int c = lowestCard(m);
            if (cardOrder(c) > cardOrder(lead)) higher |= cardBit(c);
        }
        return higher ? higher : same;
    }
    const CardMask tr = h & suitMask(trumpSuit);
    return tr ? tr : h;
}

bool Deal::canExchange(int p) const {
    return !over && lead < 0 && leader == p && stockOpen() && stockN > 2 && tricks[(size_t)p] > 0 &&
           (hand[(size_t)p] & cardBit(nineOf(trumpSuit)));
}

bool Deal::canClose(int p) const { return !over && lead < 0 && leader == p && stockOpen(); }

int Deal::marriageValue(int p, int card) const {
    if (over || lead >= 0 || leader != p || !kart::isValidCard(card)) return 0;
    const int r = rankOf(card);
    if (r != kart::Kiz && r != kart::Papaz) return 0;
    const CardMask h = hand[(size_t)p];
    if (!(h & cardBit(card)) || !(h & cardBit(partnerOf(card)))) return 0;
    return suitOf(card) == trumpSuit ? 40 : 20;
}

void Deal::exchange(int p) {
    const int nine = nineOf(trumpSuit);
    hand[(size_t)p] = (hand[(size_t)p] & ~cardBit(nine)) | cardBit(stock[0]);
    stock[0] = nine;
}

void Deal::close(int p) {
    closed = true;
    closer = p;
    closeOppTricks = tricks[(size_t)(1 - p)];
}

void Deal::finish(int w, EndReason why) {
    over = true;
    winner = w;
    reason = why;
    const int l = 1 - w;
    int gp = tricks[(size_t)l] == 0 ? 3 : points[(size_t)l] < SCHNEIDER ? 2 : 1;
    if (why == EndReason::LastTrick) gp = 1;
    if (closed && closer == l) { // the closer lost: at least 2, 3 if the other one had no trick when it was closed
        gp = std::max(gp, closeOppTricks == 0 ? 3 : 2);
        reason = EndReason::CloserFailed;
    }
    gamePoints = gp;
}

void Deal::play(int p, int card) {
    drew[0] = drew[1] = -1;
    trickCards[0] = trickCards[1] = -1;
    if (lead < 0) {
        const int m = marriageValue(p, card);
        hand[(size_t)p] &= ~cardBit(card);
        played |= cardBit(card);
        lead = card;
        if (m > 0) {
            marriages[(size_t)p] = (uint8_t)(marriages[(size_t)p] | (1u << suitOf(card)));
            if (tricks[(size_t)p] > 0) {
                points[(size_t)p] += m;
                if (points[(size_t)p] >= GOAL) finish(p, EndReason::Marriage);
            } else {
                pending[(size_t)p] += m;
            }
        }
        return;
    }
    hand[(size_t)p] &= ~cardBit(card);
    played |= cardBit(card);
    const int first = lead;
    const int w = beats(first, card, trumpSuit) ? p : leader;
    trickCards[0] = first;
    trickCards[1] = card;
    points[(size_t)w] += cardPoints(first) + cardPoints(card);
    ++tricks[(size_t)w];
    points[(size_t)w] += pending[(size_t)w];
    pending[(size_t)w] = 0;
    lead = -1;
    leader = w;
    lastWinner = w;
    if (points[(size_t)w] >= GOAL) {
        finish(w, EndReason::Reached);
        return;
    }
    if (stockOpen()) { // the winner draws the top card, the other the next (the last one is the koz card)
        drew[w] = stock[(size_t)stockN - 1];
        drew[1 - w] = stock[(size_t)stockN - 2];
        hand[(size_t)w] |= cardBit(drew[w]);
        hand[(size_t)(1 - w)] |= cardBit(drew[1 - w]);
        stockN -= 2;
    }
    if (hand[0] == 0 && hand[1] == 0) {
        if (closed) {
            finish(1 - closer, EndReason::CloserFailed);
            return;
        }
        if (lastBonus) points[(size_t)w] += 10;
        finish(w, points[(size_t)w] >= GOAL ? EndReason::Reached : EndReason::LastTrick);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

Game::Game(const Rules& r) : rules_(r) {}

void Game::setPlayer(int p, const std::string& name, bool human) {
    if (p < 0 || p > 1) return;
    names_[(size_t)p] = name;
    human_[(size_t)p] = human;
}

std::string Game::says(int p, const std::string& third, const std::string& second) const {
    return human_[(size_t)p] ? capitalizeFirst(second) : names_[(size_t)p] + " " + third;
}

void Game::push(GameEvent e) { events_.push_back(std::move(e)); }

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> v;
    v.swap(events_);
    return v;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed ^ 0x66A1717Bull);
    matchSeed_ = seed;
    totals_ = {0, 0};
    sheet_.clear();
    log_.clear();
    events_.clear();
    handIndex_ = 0;
    dealer_ = rng_.range(2);
    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = "Altmışaltı başlıyor: " + std::to_string(rules_.target) + " oyuna";
    push(std::move(e));
    dealHand();
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    ++handIndex_;
    dealer_ = 1 - dealer_;
    log_.push_back({LogKind::NextHand, -1, -1});
    dealHand();
}

void Game::dealHand() {
    std::vector<int> deck;
    for (int c = 0; c < kart::NUM_CARDS; ++c)
        if (inDeck(c)) deck.push_back(c);
    rng_.shuffle(deck);
    deal_ = Deal{};
    deal_.lastBonus = rules_.lastTrickBonus;
    const int first = 1 - dealer_;
    size_t k = 0;
    for (int round = 0; round < 2; ++round)
        for (int j = 0; j < 2; ++j) {
            const int p = j == 0 ? first : dealer_;
            for (int i = 0; i < 3; ++i) deal_.hand[(size_t)p] |= cardBit(deck[k++]);
        }
    // the 13th card is turned up under the stock: the koz; the rest lie on it face down (the top = the last)
    deal_.stock[0] = deck[k++];
    deal_.stockN = 1;
    while (k < deck.size()) deal_.stock[(size_t)deal_.stockN++] = deck[k++];
    deal_.trumpSuit = suitOf(deal_.stock[0]);
    deal_.leader = first;
    for (auto& w : won_) w.clear();
    shown_ = {0, 0};
    cannot_ = {0, 0};
    plays_.clear();
    stage_ = Stage::Playing;
    GameEvent hs;
    hs.type = EvType::HandStart;
    hs.seat = dealer_;
    hs.amount = handIndex_;
    hs.text = std::to_string(handIndex_ + 1) + ". el  ·  " + (human_[(size_t)dealer_] ? std::string("sen dağıtıyorsun")
                                                                                      : names_[(size_t)dealer_] + " dağıtıyor");
    push(std::move(hs));
    GameEvent d;
    d.type = EvType::Deal;
    d.card = deal_.stock[0];
    d.suit = deal_.trumpSuit;
    d.amount = HAND_SIZE;
    d.text = std::string("Koz ") + kart::suitNameTR(deal_.trumpSuit) + " (" + cardName(deal_.stock[0]) + ")";
    push(std::move(d));
    afterAction();
}

void Game::afterAction() {
    if (stage_ != Stage::Playing) return;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = deal_.turn();
    e.text = human_[(size_t)e.seat] ? std::string("Sıra sende") : "Sıra: " + names_[(size_t)e.seat];
    push(std::move(e));
}

std::vector<int> Game::stockCards() const {
    std::vector<int> v;
    for (int i = 0; i < deal_.stockN; ++i) v.push_back(deal_.stock[(size_t)i]);
    return v;
}

std::vector<int> Game::legalCards(int p) const {
    if (stage_ != Stage::Playing || p != deal_.turn()) return {};
    return cardsOf(deal_.legal(p));
}

Deal Game::viewOf(int viewer) const {
    Deal d = deal_;
    const int o = 1 - viewer;
    d.hand[(size_t)o] = shownCards(o);
    for (int i = 1; i < d.stockN; ++i) d.stock[(size_t)i] = -1;
    return d;
}

int Game::matchWinner() const {
    for (int p = 0; p < 2; ++p)
        if (totals_[(size_t)p] >= rules_.target) return p;
    return -1;
}

ActionResult Game::playCard(int p, int card) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Şu an kâğıt oynanmıyor");
    if (p != deal_.turn()) return ActionResult::fail(human_[(size_t)p] ? "Sıra sende değil" : "Sıra onda değil");
    if (!kart::isValidCard(card) || !(deal_.hand[(size_t)p] & cardBit(card))) return ActionResult::fail("Bu kâğıt elinde değil");
    if (!(deal_.legal(p) & cardBit(card))) {
        const int led = suitOf(deal_.lead);
        if (deal_.hand[(size_t)p] & suitMask(led)) {
            if (suitOf(card) == led) return ActionResult::fail("Kapalı oyunda eli yükseltmelisin: daha büyük bir " + std::string(kart::suitNameTR(led)) + " at");
            return ActionResult::fail("Kapalı oyunda renge uymalısın: " + std::string(kart::suitNameTR(led)) + " at");
        }
        return ActionResult::fail("Elinde " + std::string(kart::suitNameTR(led)) + " yok: koz çakmalısın");
    }
    const bool leading = deal_.lead < 0;
    PlayRecord rec;
    rec.player = p;
    rec.card = card;
    rec.lead = leading;
    rec.strict = deal_.strict();
    rec.marriage = deal_.marriageValue(p, card);
    // what the others learn about p's hand
    if (rec.marriage > 0) shown_[(size_t)p] |= cardBit(partnerOf(card));
    if (!leading && rec.strict) {
        const int led = suitOf(deal_.lead);
        if (suitOf(card) != led) {
            cannot_[(size_t)p] |= suitMask(led);
            if (suitOf(card) != deal_.trumpSuit) cannot_[(size_t)p] |= suitMask(deal_.trumpSuit);
        } else if (cardOrder(card) < cardOrder(deal_.lead)) {
            for (CardMask m = suitMask(led); m; m &= m - 1)
                if (cardOrder(lowestCard(m)) > cardOrder(deal_.lead)) cannot_[(size_t)p] |= cardBit(lowestCard(m));
        }
    }
    plays_.push_back(rec);
    log_.push_back({LogKind::Play, p, card});
    const int stockBefore = deal_.stockN;
    const int trumpBefore = deal_.stockN > 0 ? deal_.stock[0] : -1;
    const int leaderBefore = deal_.leader;
    deal_.play(p, card);

    if (rec.marriage > 0) {
        GameEvent m;
        m.type = EvType::Marriage;
        m.seat = p;
        m.card = card;
        m.suit = suitOf(card);
        m.amount = rec.marriage;
        const std::string what = std::string(kart::suitNameTR(m.suit)) + " evliliği (" + std::to_string(rec.marriage) + ")";
        m.text = says(p, what + " söyledi", what + " söyledin");
        if (deal_.tricks[(size_t)p] == 0) m.text += "  ·  ilk elini alınca yazılır";
        push(std::move(m));
    }
    GameEvent e;
    e.type = EvType::Play;
    e.seat = p;
    e.card = card;
    e.text = says(p, cardName(card) + " attı", cardName(card) + " attın");
    push(std::move(e));

    if (deal_.trickCards[0] >= 0) {
        const int w = deal_.lastWinner;
        GameEvent t;
        t.type = EvType::TrickWon;
        t.seat = w;
        t.cards = {deal_.trickCards[0], deal_.trickCards[1]};
        t.amount = cardPoints(deal_.trickCards[0]) + cardPoints(deal_.trickCards[1]);
        t.text = says(w, "eli aldı (" + std::to_string(t.amount) + ")", "eli aldın (" + std::to_string(t.amount) + ")");
        won_[(size_t)w].push_back(deal_.trickCards[0]);
        won_[(size_t)w].push_back(deal_.trickCards[1]);
        push(std::move(t));
        (void)leaderBefore;
        if (deal_.drew[0] >= 0) {
            for (int k = 0; k < 2; ++k) {
                const int q = k == 0 ? w : 1 - w;
                GameEvent d;
                d.type = EvType::Draw;
                d.seat = q;
                const bool koz = deal_.drew[q] == trumpBefore && stockBefore == 2;
                d.card = koz ? deal_.drew[q] : -1;
                if (koz) shown_[(size_t)q] |= cardBit(deal_.drew[q]);
                d.text = koz ? says(q, "açık kozu aldı", "açık kozu aldın") : says(q, "desteden çekti", "desteden çektin");
                push(std::move(d));
            }
            if (deal_.stockN == 0) {
                GameEvent so;
                so.type = EvType::StockOut;
                so.text = "Deste bitti: artık renge uymak ve eli yükseltmek zorunlu";
                push(std::move(so));
            }
        }
    }
    if (deal_.over) endHand();
    else afterAction();
    return ActionResult::success();
}

ActionResult Game::exchangeNine(int p) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Şu an oynanmıyor");
    if (p != deal_.turn()) return ActionResult::fail("Sıra sende değil");
    if (!deal_.canExchange(p)) {
        const int nine = nineOf(deal_.trumpSuit);
        if (deal_.lead >= 0 || deal_.leader != p) return ActionResult::fail("Kozu yalnızca eli açarken değiştirebilirsin");
        if (!(deal_.hand[(size_t)p] & cardBit(nine))) return ActionResult::fail("Elinde koz dokuzu yok");
        if (!deal_.stockOpen() || deal_.stockN <= 2) return ActionResult::fail("Deste kapandı ya da son iki kâğıda indi: koz değişmez");
        return ActionResult::fail("Kozu değiştirmek için önce bir el almalısın");
    }
    const int took = deal_.stock[0];
    const int nine = nineOf(deal_.trumpSuit);
    deal_.exchange(p);
    shown_[(size_t)p] |= cardBit(took);
    log_.push_back({LogKind::Exchange, p, nine});
    GameEvent e;
    e.type = EvType::Exchange;
    e.seat = p;
    e.card = nine;
    e.amount = took;
    e.text = says(p, "koz dokuzuyla " + kart::cardAccusativeTR(took) + " aldı", "koz dokuzuyla " + kart::cardAccusativeTR(took) + " aldın");
    push(std::move(e));
    afterAction();
    return ActionResult::success();
}

ActionResult Game::closeStock(int p) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Şu an oynanmıyor");
    if (p != deal_.turn()) return ActionResult::fail("Sıra sende değil");
    if (!deal_.canClose(p)) {
        if (deal_.lead >= 0 || deal_.leader != p) return ActionResult::fail("Desteyi yalnızca eli açarken kapatabilirsin");
        return ActionResult::fail("Deste zaten kapalı ya da bitti");
    }
    deal_.close(p);
    log_.push_back({LogKind::Close, p, -1});
    GameEvent e;
    e.type = EvType::Close;
    e.seat = p;
    e.text = says(p, "desteyi kapattı", "desteyi kapattın");
    push(std::move(e));
    afterAction();
    return ActionResult::success();
}

void Game::endHand() {
    const Deal& d = deal_;
    HandRecord r;
    r.index = handIndex_;
    r.dealer = dealer_;
    r.winner = d.winner;
    r.gamePoints = d.gamePoints;
    r.reason = d.reason;
    r.closer = d.closer;
    r.points = d.points;
    r.tricks = d.tricks;
    for (const PlayRecord& pr : plays_)
        if (pr.marriage > 0) r.marriages[(size_t)pr.player] += pr.marriage;
    totals_[(size_t)d.winner] += d.gamePoints;
    r.totals = totals_;
    sheet_.push_back(r);
    const int w = d.winner, l = 1 - w;
    const std::string gp = std::to_string(d.gamePoints) + " oyun";
    GameEvent e;
    e.type = EvType::HandEnd;
    e.seat = w;
    e.amount = d.gamePoints;
    switch (d.reason) {
    case EndReason::Reached:
    case EndReason::Marriage:
        e.text = says(w, "66'yı buldu (" + std::to_string(d.points[(size_t)w]) + "): " + gp,
                      "66'yı buldun (" + std::to_string(d.points[(size_t)w]) + "): " + gp);
        if (d.tricks[(size_t)l] == 0) e.text += "  ·  hiç el alamadı";
        else if (d.points[(size_t)l] < SCHNEIDER) e.text += "  ·  33'ü bulamadı";
        break;
    case EndReason::CloserFailed:
        e.text = says(l, "kapattı ama tutturamadı", "kapattın ama tutturamadın") + ": " +
                 (human_[(size_t)w] ? std::string("sana ") : names_[(size_t)w] + "'a ") + gp;
        break;
    case EndReason::LastTrick:
        e.text = says(w, "son eli aldı, kimse 66'yı bulamadı: " + gp, "son eli aldın, kimse 66'yı bulamadı: " + gp);
        break;
    default: break;
    }
    push(e);
    if (totals_[(size_t)w] >= rules_.target) {
        stage_ = Stage::MatchOver;
        GameEvent m;
        m.type = EvType::MatchEnd;
        m.seat = w;
        m.text = says(w, "maçı kazandı", "maçı kazandın") + " (" + std::to_string(totals_[(size_t)w]) + " - " +
                 std::to_string(totals_[(size_t)l]) + ")";
        push(std::move(m));
    } else {
        stage_ = Stage::HandOver;
    }
}

void Game::debugSetDeal(const std::vector<int>& h0, const std::vector<int>& h1, const std::vector<int>& stock, int leader) {
    const bool bonus = deal_.lastBonus;
    deal_ = Deal{};
    deal_.lastBonus = bonus;
    deal_.hand[0] = maskOf(h0);
    deal_.hand[1] = maskOf(h1);
    deal_.stockN = 0;
    for (int c : stock) deal_.stock[(size_t)deal_.stockN++] = c;
    deal_.trumpSuit = stock.empty() ? kart::Maca : suitOf(stock[0]);
    deal_.leader = leader;
    deal_.played = DECK_MASK & ~(deal_.hand[0] | deal_.hand[1] | maskOf(stock));
    for (auto& w : won_) w.clear();
    shown_ = {0, 0};
    cannot_ = {0, 0};
    plays_.clear();
    stage_ = Stage::Playing;
    events_.clear();
}

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Play: return playCard(a.player, a.card).ok;
    case LogKind::Exchange: return exchangeNine(a.player).ok;
    case LogKind::Close: return closeStock(a.player).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

std::string LoggedAction::encode() const {
    return std::to_string((int)kind) + " " + std::to_string(player) + " " + std::to_string(card);
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    int v[3] = {0, 0, 0};
    if (!kart::parseInts(line, v, 3)) return false;
    if (v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    if (v[0] != (int)LogKind::NextHand && (v[1] < 0 || v[1] > 1)) return false;
    out.kind = (LogKind)v[0];
    out.player = v[1];
    out.card = v[2];
    return true;
}

} // namespace altmisalti
