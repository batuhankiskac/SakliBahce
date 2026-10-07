// İhaleli / Eşli Batak engine. See Batak.h for the flow and docs/kurallar_batak.md for the rules.
#include "core/Batak.h"

#include <algorithm>

namespace batak {

using kart::nextSeat;
using kart::partnerOf;
using kart::suitOf;

// ---------------------------------------------------------------------------------------------------------
// card-set helpers and the play rules

TrickView viewTrick(const std::vector<PlayedCard>& trick, int trump) {
    TrickView v;
    if (trick.empty()) return v;
    v.ledSuit = suitOf(trick[0].card);
    v.winner = trick[0].seat;
    v.winningCard = trick[0].card;
    v.bestLed = trick[0].card;
    for (size_t i = 1; i < trick.size(); ++i) {
        const int c = trick[i].card;
        if (beatsTrick(c, v, trump)) {
            v.winner = trick[i].seat;
            v.winningCard = c;
        }
        if (suitOf(c) == v.ledSuit) {
            if (c > v.bestLed) v.bestLed = c;
        } else if (suitOf(c) == trump) {
            if (c > v.bestTrump) v.bestTrump = c;
        }
    }
    return v;
}

bool beatsTrick(int card, const TrickView& v, int trump) {
    if (v.winningCard < 0) return true;
    const int s = suitOf(card), ws = suitOf(v.winningCard);
    if (s == ws) return card > v.winningCard;
    return s == trump; // a koz over a non-koz; any other suit never wins
}

uint64_t legalMask(uint64_t hand, const TrickView& v, int trump, bool trumpBroken, const Rules& r) {
    if (!hand) return 0;
    if (v.ledSuit < 0) { // leading
        if (trump >= 0 && r.trumpMustBeBroken && !trumpBroken) {
            const uint64_t nonTrump = hand & ~suitMask(trump);
            return nonTrump ? nonTrump : hand; // only koz left: may lead koz
        }
        return hand;
    }
    const uint64_t follow = hand & suitMask(v.ledSuit);
    if (follow) {
        if (r.mustBeat && (v.bestTrump < 0 || r.mustBeatEvenIfTrumped)) {
            const uint64_t higher = follow & aboveMask(v.bestLed);
            if (higher) return higher;
        }
        return follow;
    }
    if (trump >= 0 && r.mustTrump) {
        const uint64_t tr = hand & suitMask(trump);
        if (tr) {
            if (r.mustOvertrump && v.bestTrump >= 0) {
                const uint64_t over = tr & aboveMask(v.bestTrump);
                if (over) return over;
            }
            return tr;
        }
    }
    return hand;
}

int sidePoints(const Rules& r, bool declaring, int tricks, int contract) {
    const bool made = declaring ? tricks >= contract : tricks >= r.defenderMinTricks;
    return (made ? tricks : -contract) * r.multiplier;
}

// ---------------------------------------------------------------------------------------------------------
// text helpers

namespace {

std::string signed_(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }

} // namespace

// ---------------------------------------------------------------------------------------------------------
// setup

Game::Game(const Rules& r) { setRules(r); }

void Game::setRules(const Rules& r) {
    if (stage_ != Stage::NotStarted && stage_ != Stage::MatchOver) return; // (only between matches)
    rules_ = r;
    rules_.minBid = std::max(1, std::min(13, rules_.minBid));
    rules_.allPassBid = std::max(1, std::min(13, rules_.allPassBid));
    rules_.multiplier = std::max(1, rules_.multiplier);
    if (rules_.targetScore <= 0 && rules_.numHands <= 0) rules_.targetScore = 51;
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat > 3) return;
    names_[seat] = name;
    human_[seat] = human;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed);
    matchSeed_ = seed;
    log_.clear();
    totals_ = {};
    results_.clear();
    lastResult_ = HandResult();
    winnerSide_ = -1;
    handIndex_ = 0;
    events_.clear();
    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = rules_.esli ? "Eşli ihaleli batak başlıyor" : "İhaleli batak başlıyor";
    push(std::move(e));
    const int d = rules_.firstDealer >= 0 ? rules_.firstDealer % 4 : rng_.range(4);
    dealHand(d, nullptr);
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    log_.push_back({LogKind::NextHand, -1, -1});
    ++handIndex_;
    dealHand(nextSeat(dealer_), nullptr);
}

void Game::resetHandState() {
    for (auto& h : hands_) h.clear();
    bids_.clear();
    passed_ = {};
    highBid_ = 0;
    highBidder_ = -1;
    declarer_ = -1;
    contract_ = 0;
    forced_ = false;
    trump_ = -1;
    trumpBroken_ = false;
    dummy_ = -1;
    trick_.clear();
    leader_ = -1;
    tricks_.clear();
    tricksWon_ = {};
    played_ = 0;
    current_ = -1;
}

void Game::dealHand(int dealer, const std::array<std::vector<int>, 4>* fixed) {
    resetHandState();
    dealer_ = dealer;
    if (fixed) {
        hands_ = *fixed;
    } else {
        const std::vector<int> deck = kart::shuffledDeck(rng_);
        // one card at a time, starting with the dealer's right
        for (int i = 0; i < kart::NUM_CARDS; ++i) hands_[(dealer + 1 + i) % 4].push_back(deck[i]);
    }
    for (auto& h : hands_) std::sort(h.begin(), h.end());

    GameEvent e;
    e.type = EvType::HandStart;
    e.seat = dealer;
    e.value = handIndex_;
    e.text = "Yeni el (" + std::to_string(handIndex_ + 1) + ".): " +
             (human_[dealer] ? std::string("kağıtları sen dağıtıyorsun")
                             : "kağıtları " + names_[dealer] + " dağıtıyor");
    push(std::move(e));
    GameEvent d;
    d.type = EvType::Deal;
    d.seat = dealer;
    d.value = (int)hands_[0].size();
    d.text = "Kağıtlar dağıtıldı";
    push(std::move(d));

    stage_ = Stage::Bidding;
    beginTurn(firstBidder());
}

std::vector<int> Game::sideSeats(int side) const {
    if (rules_.esli) return {side, side + 2};
    return {side};
}

int Game::leaderSide() const {
    int best = 0;
    for (int s = 1; s < numSides(); ++s)
        if (totals_[s] > totals_[best]) best = s;
    return best;
}

std::string Game::sideName(int side) const {
    if (!rules_.esli) return names_[side];
    return names_[side] + " ile " + names_[side + 2];
}

bool Game::sideHasHuman(int side) const {
    for (int s : sideSeats(side))
        if (human_[s]) return true;
    return false;
}

int Game::controllerOf(int seat) const {
    if (seat >= 0 && seat == dummy_) return declarer_;
    return seat;
}

int Game::lastBidOf(int seat) const {
    for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
        if (it->seat == seat) return it->value;
    return 0;
}

int Game::sideTricks(int side) const {
    int t = 0;
    for (int s : sideSeats(side)) t += tricksWon_[s];
    return t;
}

int Game::trickWinnerSoFar() const { return viewTrick(trick_, trump_).winner; }

// ---------------------------------------------------------------------------------------------------------
// turn flow

std::string Game::says(int seat, const std::string& third, const std::string& second) const {
    return human_[seat] ? second : names_[seat] + " " + third;
}

std::string Game::sideSays(int side, const std::string& third, const std::string& second) const {
    return sideHasHuman(side) ? second : sideName(side) + " " + third;
}

void Game::push(GameEvent e) { kart::pushEvent(events_, std::move(e)); }

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::beginTurn(int seat) {
    current_ = seat;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = seat;
    e.stage = stage_;
    const int ctl = controllerOf(seat);
    switch (stage_) {
    case Stage::Bidding: e.text = human_[seat] ? "İhale sırası sende" : "İhale sırası: " + names_[seat]; break;
    case Stage::ChoosingTrump: e.text = says(seat, "kozu seçiyor", "Kozu seç"); break;
    default:
        if (ctl != seat) e.text = human_[ctl] ? "Sıra sende (açık el)" : "Sıra: " + names_[ctl] + " (açık el)";
        else e.text = human_[seat] ? "Sıra sende" : "Sıra: " + names_[seat];
        break;
    }
    push(std::move(e));
}

ActionResult Game::checkTurn(int seat, Stage need) const {
    if (seat < 0 || seat > 3) return ActionResult::fail("Geçersiz oyuncu");
    if (stage_ != need) {
        switch (need) {
        case Stage::Bidding: return ActionResult::fail("İhale bitti");
        case Stage::ChoosingTrump: return ActionResult::fail("Şimdi koz seçilmez");
        default: return ActionResult::fail("Şimdi kart oynanmaz");
        }
    }
    if (seat != current_) return ActionResult::fail("Sıra sende değil");
    return ActionResult::success();
}

// ---------------------------------------------------------------------------------------------------------
// bidding

std::vector<int> Game::legalBids(int seat) const {
    std::vector<int> v;
    if (stage_ != Stage::Bidding || seat != current_) return v;
    for (int b = std::max(rules_.minBid, highBid_ + 1); b <= 13; ++b) v.push_back(b);
    return v;
}

bool Game::canPass(int seat) const { return stage_ == Stage::Bidding && seat == current_; }

ActionResult Game::bidImpl(int seat, int value) {
    ActionResult t = checkTurn(seat, Stage::Bidding);
    if (!t.ok) return t;
    if (value > 13) return ActionResult::fail("En fazla 13 denebilir");
    if (value < rules_.minBid) return ActionResult::fail("İhale en az " + std::to_string(rules_.minBid) + " ile açılır");
    if (value <= highBid_) return ActionResult::fail("İhaleyi geçmek için en az " + std::to_string(highBid_ + 1) + " demelisin");

    bids_.push_back({seat, value});
    highBid_ = value;
    highBidder_ = seat;
    GameEvent e;
    e.type = EvType::Bid;
    e.seat = seat;
    e.value = value;
    e.text = says(seat, std::to_string(value) + " dedi", std::to_string(value) + " dedin");
    push(std::move(e));

    if (value == 13) {
        finishBidding(seat, 13, false);
        return ActionResult::success();
    }
    int others = 0;
    for (int s = 0; s < 4; ++s)
        if (s != seat && !passed_[s]) ++others;
    if (others == 0) {
        finishBidding(seat, value, false);
        return ActionResult::success();
    }
    int nx = nextSeat(seat);
    while (passed_[nx]) nx = nextSeat(nx);
    beginTurn(nx);
    return ActionResult::success();
}

ActionResult Game::passImpl(int seat) {
    ActionResult t = checkTurn(seat, Stage::Bidding);
    if (!t.ok) return t;
    bids_.push_back({seat, 0});
    passed_[seat] = true;
    GameEvent e;
    e.type = EvType::Bid;
    e.seat = seat;
    e.value = 0;
    e.text = says(seat, "pas dedi", "Pas dedin");
    push(std::move(e));

    int active = 0, last = -1;
    for (int s = 0; s < 4; ++s)
        if (!passed_[s]) {
            ++active;
            last = s;
        }
    if (highBidder_ >= 0 && active == 1 && last == highBidder_) {
        finishBidding(highBidder_, highBid_, false);
        return ActionResult::success();
    }
    if (active == 0) { // everybody passed
        switch (rules_.allPass) {
        case AllPass::FirstBidder: finishBidding(firstBidder(), rules_.allPassBid, true); break;
        case AllPass::Dealer: finishBidding(dealer_, rules_.allPassBid, true); break;
        case AllPass::Redeal: {
            GameEvent r;
            r.type = EvType::Redeal;
            r.seat = dealer_;
            r.text = "Herkes pas dedi, kağıtlar yeniden dağıtılıyor";
            push(std::move(r));
            dealHand(nextSeat(dealer_), nullptr);
            break;
        }
        }
        return ActionResult::success();
    }
    int nx = nextSeat(seat);
    while (passed_[nx]) nx = nextSeat(nx);
    beginTurn(nx);
    return ActionResult::success();
}

void Game::finishBidding(int seat, int value, bool forced) {
    declarer_ = seat;
    contract_ = value;
    forced_ = forced;
    highBid_ = std::max(highBid_, value);
    highBidder_ = seat;
    const std::string v = std::to_string(value);
    GameEvent e;
    e.type = EvType::BiddingWon;
    e.seat = seat;
    e.value = value;
    e.forced = forced;
    if (forced) e.text = "Herkes pas dedi: " + says(seat, "ihaleyi " + v + " ile almak zorunda", "ihaleyi " + v + " ile almak zorundasın");
    else e.text = says(seat, "ihaleyi " + v + " ile aldı", "İhaleyi " + v + " ile aldın");
    push(std::move(e));
    stage_ = Stage::ChoosingTrump;
    beginTurn(seat);
}

// ---------------------------------------------------------------------------------------------------------
// trump and play

ActionResult Game::chooseTrumpImpl(int seat, int suit) {
    ActionResult t = checkTurn(seat, Stage::ChoosingTrump);
    if (!t.ok) return t;
    if (suit < 0 || suit >= kart::NUM_SUITS) return ActionResult::fail("Geçersiz koz");
    trump_ = suit;
    GameEvent e;
    e.type = EvType::TrumpChosen;
    e.seat = seat;
    e.suit = suit;
    e.text = says(seat, "koz " + std::string(kart::suitLowerTR(suit)) + " dedi", "Koz " + std::string(kart::suitLowerTR(suit)) + " dedin");
    push(std::move(e));

    if (rules_.esli && rules_.openDummy) {
        dummy_ = partnerOf(seat);
        GameEvent d;
        d.type = EvType::DummyOpen;
        d.seat = dummy_;
        for (int c : hands_[dummy_]) d.cards.push_back({dummy_, c});
        if (human_[dummy_]) d.text = "Kağıtlarını açtın; " + names_[seat] + " senin yerine oynayacak";
        else if (human_[seat]) d.text = names_[dummy_] + " kağıtlarını açtı; onun kağıtlarını da sen oynayacaksın";
        else d.text = names_[dummy_] + " kağıtlarını açtı; " + names_[seat] + " onun yerine oynayacak";
        push(std::move(d));
    }
    stage_ = Stage::Playing;
    leader_ = seat;
    beginTurn(seat);
    return ActionResult::success();
}

std::vector<int> Game::legalCards(int seat) const {
    if (stage_ != Stage::Playing || seat != current_) return {};
    return cardsOf(legalMask(maskOf(hands_[seat]), viewTrick(trick_, trump_), trump_, trumpBroken_, rules_));
}

ActionResult Game::checkPlay(int seat, int card) const {
    ActionResult t = checkTurn(seat, Stage::Playing);
    if (!t.ok) return t;
    const uint64_t hand = maskOf(hands_[seat]);
    if (!kart::isValidCard(card) || !(hand & bit(card))) return ActionResult::fail("Bu kart elinde yok");
    const TrickView v = viewTrick(trick_, trump_);
    if (legalMask(hand, v, trump_, trumpBroken_, rules_) & bit(card)) return ActionResult::success();
    if (v.ledSuit < 0) return ActionResult::fail("Koz henüz açılmadı");
    if (hand & suitMask(v.ledSuit)) {
        if (suitOf(card) != v.ledSuit)
            return ActionResult::fail("Elinde " + std::string(kart::suitLowerTR(v.ledSuit)) + " varken başka renk atamazsın");
        return ActionResult::fail("Yükseltmen gerek");
    }
    if (suitOf(card) != trump_) return ActionResult::fail("Koz çakmalısın");
    return ActionResult::fail("Daha büyük koz atmalısın");
}

ActionResult Game::playCardImpl(int seat, int card) {
    ActionResult t = checkPlay(seat, card);
    if (!t.ok) return t;
    auto& h = hands_[seat];
    h.erase(std::find(h.begin(), h.end(), card));
    played_ |= bit(card);
    const bool leading = trick_.empty();
    if (leading) leader_ = seat;
    trick_.push_back({seat, card});

    const std::string cn = kart::cardNameTR(card);
    GameEvent e;
    e.type = EvType::Play;
    e.seat = seat;
    e.card = card;
    if (seat == dummy_) {
        const int ctl = controllerOf(seat);
        e.text = says(ctl, "açık elden " + cn + " attı", "Açık elden " + cn + " attın");
    } else {
        e.text = says(seat, cn + " attı", cn + " attın");
    }
    push(std::move(e));

    if (suitOf(card) == trump_ && !trumpBroken_) {
        trumpBroken_ = true;
        GameEvent b;
        b.type = EvType::TrumpBroken;
        b.seat = seat;
        b.card = card;
        b.text = leading ? "Koz açıldı!" : "Koz çakıldı, koz açıldı!";
        push(std::move(b));
    }

    if (trick_.size() == 4) completeTrick();
    else beginTurn(nextSeat(seat));
    return ActionResult::success();
}

void Game::completeTrick() {
    const TrickView v = viewTrick(trick_, trump_);
    Trick tr;
    tr.leader = trick_[0].seat;
    tr.winner = v.winner;
    tr.cards = trick_;
    tricks_.push_back(tr);
    ++tricksWon_[v.winner];
    trick_.clear();

    GameEvent e;
    e.type = EvType::TrickWon;
    e.seat = v.winner;
    e.value = (int)tricks_.size();
    e.cards = tr.cards;
    e.text = says(v.winner, "eli aldı", "Eli aldın");
    push(std::move(e));

    if (hands_[0].empty() && hands_[1].empty() && hands_[2].empty() && hands_[3].empty()) {
        endHand();
        return;
    }
    leader_ = v.winner;
    beginTurn(v.winner);
}

void Game::endHand() {
    current_ = -1;
    HandResult r;
    r.handIndex = handIndex_;
    r.dealer = dealer_;
    r.declarer = declarer_;
    r.contract = contract_;
    r.trump = trump_;
    r.forced = forced_;
    r.tricks = tricksWon_;
    const int declSide = sideOf(declarer_);
    const bool plural = rules_.esli;
    for (int side = 0; side < numSides(); ++side) {
        SideResult sr;
        sr.side = side;
        sr.seats = sideSeats(side);
        sr.declarer = side == declSide;
        sr.bid = sr.declarer ? contract_ : 0;
        sr.tricks = sideTricks(side);
        sr.made = sr.declarer ? sr.tricks >= contract_ : sr.tricks >= rules_.defenderMinTricks;
        sr.points = sidePoints(rules_, sr.declarer, sr.tricks, contract_);
        const std::string n = std::to_string(sr.tricks), p = signed_(sr.points);
        if (sr.declarer) {
            if (sr.made)
                sr.text = sideSays(side, "ihaleyi yaptı: " + n + " el, " + p,
                                   std::string(plural ? "İhaleyi yaptınız: " : "İhaleyi yaptın: ") + n + " el, " + p);
            else
                sr.text = sideSays(side, "battı! " + n + " el, " + p,
                                   std::string(plural ? "Battınız! " : "Battın! ") + n + " el, " + p);
        } else if (sr.made) {
            sr.text = sideSays(side, n + " el aldı: " + p, n + (plural ? " el aldınız: " : " el aldın: ") + p);
        } else if (sr.tricks == 0) {
            sr.text = sideSays(side, "hiç el alamadı, battı: " + p,
                               std::string(plural ? "Hiç el alamadınız, battınız: " : "Hiç el alamadın, battın: ") + p);
        } else {
            sr.text = sideSays(side, n + " el yetmedi, battı: " + p,
                               n + (plural ? " el yetmedi, battınız: " : " el yetmedi, battın: ") + p);
        }
        for (int s : sr.seats) r.points[s] = sr.points;
        totals_[side] += sr.points;
        r.sides.push_back(std::move(sr));
    }
    r.king = contract_ == 13 && sideTricks(declSide) == 13;
    lastResult_ = r;
    results_.push_back(r);

    GameEvent e;
    e.type = EvType::HandEnd;
    e.seat = declarer_;
    e.value = handIndex_;
    for (const SideResult& sr : r.sides) e.lines.push_back(sr.text);
    e.text = "El bitti";
    push(std::move(e));

    if (r.king && rules_.kingEndsMatch) {
        endMatch(declSide);
        return;
    }
    bool target = false;
    if (rules_.targetScore > 0)
        for (int s = 0; s < numSides(); ++s)
            if (totals_[s] >= rules_.targetScore) target = true;
    const bool limit = rules_.numHands > 0 && handIndex_ + 1 >= rules_.numHands;
    if (target || limit) {
        const int best = leaderSide();
        int ties = 0;
        for (int s = 0; s < numSides(); ++s)
            if (totals_[s] == totals_[best]) ++ties;
        if (ties == 1) {
            endMatch(best);
            return;
        }
        if (limit) {
            endMatch(-1);
            return;
        }
        // tied at the top past the target: play on until somebody leads alone
    }
    stage_ = Stage::HandOver;
}

void Game::endMatch(int winner) {
    stage_ = Stage::MatchOver;
    winnerSide_ = winner;
    GameEvent e;
    e.type = EvType::MatchEnd;
    e.value = winner;
    if (winner < 0) {
        e.text = "Oyun berabere bitti";
    } else {
        e.seat = sideSeats(winner)[0];
        const std::string pts = " (" + std::to_string(totals_[winner]) + ")";
        const std::string king = lastResult_.king && rules_.kingEndsMatch ? "King! " : "";
        if (sideHasHuman(winner)) e.text = king + (rules_.esli ? "Oyunu kazandınız!" : "Oyunu kazandın!") + pts;
        else e.text = king + "Oyunu " + sideName(winner) + " kazandı" + pts;
    }
    push(std::move(e));
}

// ---------------------------------------------------------------------------------------------------------
// testing hooks

void Game::debugRedeal(int dealer, const std::array<std::vector<int>, 4>& hands) {
    if (stage_ == Stage::NotStarted || stage_ == Stage::MatchOver) stage_ = Stage::HandOver;
    dealHand(((dealer % 4) + 4) % 4, &hands);
}

void Game::debugStartPlay(const std::array<std::vector<int>, 4>& hands, int declarer, int contract, int trump,
                          int leader, bool trumpBroken) {
    resetHandState();
    hands_ = hands;
    for (auto& h : hands_) std::sort(h.begin(), h.end());
    uint64_t all = 0;
    for (auto& h : hands_) all |= maskOf(h);
    played_ = ~all & ((1ull << kart::NUM_CARDS) - 1);
    declarer_ = declarer;
    contract_ = contract;
    highBid_ = contract;
    highBidder_ = declarer;
    trump_ = trump;
    trumpBroken_ = trumpBroken;
    if (rules_.esli && rules_.openDummy) dummy_ = partnerOf(declarer);
    stage_ = Stage::Playing;
    const int l = leader >= 0 ? leader : declarer;
    leader_ = l;
    beginTurn(l);
}

// ---------------------------------------------------------------------------------------------------------
// the match's action log (save / resume)

ActionResult Game::logged(ActionResult r, LoggedAction a) {
    if (r.ok) log_.push_back(a);
    return r;
}

ActionResult Game::bid(int seat, int value) { return logged(bidImpl(seat, value), {LogKind::Bid, seat, value}); }
ActionResult Game::pass(int seat) { return logged(passImpl(seat), {LogKind::Pass, seat, -1}); }
ActionResult Game::chooseTrump(int seat, int suit) { return logged(chooseTrumpImpl(seat, suit), {LogKind::Trump, seat, suit}); }
ActionResult Game::playCard(int seat, int card) { return logged(playCardImpl(seat, card), {LogKind::Play, seat, card}); }

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Bid: return bid(a.seat, a.value).ok;
    case LogKind::Pass: return pass(a.seat).ok;
    case LogKind::Trump: return chooseTrump(a.seat, a.value).ok;
    case LogKind::Play: return playCard(a.seat, a.value).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

std::string LoggedAction::encode() const {
    return std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(value);
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    int v[3] = {0, 0, 0};
    if (!kart::parseInts(line, v, 3)) return false;
    if (v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)v[0];
    out.seat = v[1];
    out.value = v[2];
    return true;
}

} // namespace batak
