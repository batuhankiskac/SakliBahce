// King rules engine. Rules: docs/kurallar_king.md, API: King.h.
#include "core/King.h"
#include "core/TurkishText.h"

#include <algorithm>

namespace king {

using namespace kart;

namespace {

constexpr int HAND_SIZE = 13;
constexpr int NUM_TRICKS = 13;

const char* const kContractNames[NUM_CONTRACTS] = {"El Almaz", "Kupa Almaz", "Erkek Almaz", "Kız Almaz",
                                                   "Rıfkı",    "Son İki",    "Koz"};
// Lower-case forms for the middle of a sentence ("Emekli Nuri kız almaz seçti").
const char* const kContractLower[NUM_CONTRACTS] = {"el almaz", "kupa almaz", "erkek almaz", "kız almaz",
                                                   "rıfkı",    "son iki",    "koz"};

using trtext::capitalizeFirst;
using trtext::locative;

std::string signedPoints(int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); }

int highestOfSuit(const TrickContext& t, int suit) {
    int best = -1;
    for (int i = 0; i < t.n; ++i)
        if (suitOf(t.cards[i]) == suit && t.cards[i] > best) best = t.cards[i];
    return best;
}

// Cards of `suit` strictly above `card` (same suit) / below.
CardMask above(int card) { return suitMask(suitOf(card)) & ~((cardBit(card) << 1) - 1); }
CardMask below(int card) { return suitMask(suitOf(card)) & (cardBit(card) - 1); }

} // namespace

const char* contractNameTR(Contract c) {
    const int i = (int)c;
    return i >= 0 && i < NUM_CONTRACTS ? kContractNames[i] : "?";
}

std::string contractLabelTR(Contract c, int trump) {
    if (c == Contract::Koz && trump >= 0 && trump < NUM_SUITS) return std::string("Koz (") + suitNameTR(trump) + ")";
    return contractNameTR(c);
}

CardMask penaltyCards(Contract c) {
    CardMask m = 0;
    switch (c) {
    case Contract::KupaAlmaz: return suitMask(Kupa);
    case Contract::ErkekAlmaz:
        for (int s = 0; s < NUM_SUITS; ++s) m |= cardBit(makeCard(s, Papaz)) | cardBit(makeCard(s, Vale));
        return m;
    case Contract::KizAlmaz:
        for (int s = 0; s < NUM_SUITS; ++s) m |= cardBit(makeCard(s, Kiz));
        return m;
    case Contract::Rifki: return cardBit(RIFKI);
    default: return 0;
    }
}

int cardPoints(const Rules& r, Contract c, int card) {
    if (!isCardCeza(c) || !isValidCard(card)) return 0;
    return (penaltyCards(c) & cardBit(card)) ? r.points(c) : 0;
}

int trickPoints(const Rules& r, Contract c, CardMask cards, int trickIndex) {
    switch (c) {
    case Contract::Koz:
    case Contract::ElAlmaz: return r.points(c);
    case Contract::SonIki: return trickIndex >= NUM_TRICKS - 2 ? r.points(c) : 0;
    default: return popcount(cards & penaltyCards(c)) * r.points(c);
    }
}

int trickWinnerIndex(const int* cards, int n, Contract c, int trump) {
    if (n <= 0) return -1;
    int best = 0;
    for (int i = 1; i < n; ++i) {
        const int sb = suitOf(cards[best]), si = suitOf(cards[i]);
        if (si == sb) {
            if (cards[i] > cards[best]) best = i;
        } else if (c == Contract::Koz && si == trump) {
            best = i; // first trump over a plain card
        }
    }
    return best;
}

int playFilters(const Rules& r, const TrickContext& t, PlayFilter out[MAX_FILTERS]) {
    int k = 0;
    auto add = [&](CardMask m, PlayRule rule) {
        if (k < MAX_FILTERS) out[k++] = PlayFilter{m, rule};
    };
    const Contract c = t.contract;
    const bool koz = c == Contract::Koz && t.trump >= 0 && t.trump < NUM_SUITS;
    if (t.n <= 0) { // leading
        if (koz && r.trumpLeadNeedsBreak && !t.trumpBroken) add(ALL_CARDS & ~suitMask(t.trump), PlayRule::LeadNoTrump);
        if ((c == Contract::KupaAlmaz || c == Contract::Rifki) && r.heartLeadNeedsBreak && !t.heartsBroken)
            add(ALL_CARDS & ~suitMask(Kupa), PlayRule::LeadNoHeart);
        return k;
    }
    const int led = suitOf(t.cards[0]);
    if (koz) {
        const int hiTrump = highestOfSuit(t, t.trump);
        if (led == t.trump) {
            if (r.mustRaiseOnTrumpLead && hiTrump >= 0) add(above(hiTrump), PlayRule::RaiseTrump);
            add(suitMask(led), PlayRule::FollowSuit);
        } else {
            if (r.mustBeatLedSuit && hiTrump < 0) add(above(highestOfSuit(t, led)), PlayRule::BeatLed);
            add(suitMask(led), PlayRule::FollowSuit);
            if (r.mustTrumpWhenVoid) {
                if (r.mustOvertrumpWhenRuffing && hiTrump >= 0) add(above(hiTrump), PlayRule::Overtrump);
                add(suitMask(t.trump), PlayRule::MustTrump);
            }
        }
        return k;
    }
    const int hiLed = highestOfSuit(t, led);
    if (r.forceDropUnderHigher &&
        (c == Contract::KizAlmaz || c == Contract::ErkekAlmaz || c == Contract::Rifki)) {
        const CardMask drop = penaltyCards(c) & below(hiLed);
        if (drop) add(drop, PlayRule::DropUnder);
    }
    if (r.mustBeatLedSuit) add(above(hiLed), PlayRule::BeatLed);
    add(suitMask(led), PlayRule::FollowSuit);
    if (r.voidMustDiscardPenalty) {
        if (c == Contract::KizAlmaz) add(penaltyCards(c), PlayRule::VoidQueen);
        if (c == Contract::ErkekAlmaz) add(penaltyCards(c), PlayRule::VoidErkek);
        if (c == Contract::Rifki) {
            add(cardBit(RIFKI), PlayRule::VoidRifki);
            add(suitMask(Kupa), PlayRule::VoidHeart);
        }
    }
    if (c == Contract::KupaAlmaz && r.kupaAlmazVoidMustHeart) add(suitMask(Kupa), PlayRule::VoidHeart);
    return k;
}

CardMask legalMask(const Rules& r, const TrickContext& t, CardMask hand) {
    PlayFilter f[MAX_FILTERS];
    const int k = playFilters(r, t, f);
    for (int i = 0; i < k; ++i)
        if (hand & f[i].mask) return hand & f[i].mask;
    return hand;
}

PlayRule whyIllegal(const Rules& r, const TrickContext& t, CardMask hand, int card) {
    PlayFilter f[MAX_FILTERS];
    const int k = playFilters(r, t, f);
    for (int i = 0; i < k; ++i)
        if (hand & f[i].mask) return (f[i].mask & cardBit(card)) ? PlayRule::None : f[i].rule;
    return PlayRule::None;
}

// ---------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------

Game::Game(const Rules& r) : rules_(r) {
    static const char* const defaults[4] = {"Sen", "Sağdaki", "Karşıdaki", "Soldaki"};
    for (int s = 0; s < 4; ++s) {
        seats_[s].name = defaults[s];
        seats_[s].human = s == 0;
    }
}

void Game::setRules(const Rules& r) {
    if (stage_ == Stage::NotStarted || stage_ == Stage::MatchOver) rules_ = r;
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat >= 4) return;
    seats_[seat].name = name;
    seats_[seat].human = human;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed);
    matchSeed_ = seed;
    log_.clear();
    handIndex_ = 0;
    sheet_.clear();
    timesChosen_.fill(0);
    for (SeatInfo& s : seats_) {
        s.total = 0;
        s.kozUsed = s.cezaUsed = 0;
    }
    firstChooser_ = rules_.firstChooser >= 0 && rules_.firstChooser < 4 ? rules_.firstChooser : -1;
    GameEvent e;
    e.type = EvType::MatchStart;
    e.text = "Yeni parti başladı (" + std::to_string(numHands()) + " el)";
    push(std::move(e));
    dealHand();
}

void Game::startNextHand() {
    if (stage_ != Stage::HandOver) return;
    log_.push_back({LogKind::NextHand, -1, -1, -1});
    ++handIndex_;
    dealHand();
}

void Game::dealHand() {
    std::vector<int> deck = shuffledDeck(rng_);
    for (int s = 0; s < 4; ++s) {
        SeatInfo& p = seats_[s];
        p.hand.assign(deck.begin() + s * HAND_SIZE, deck.begin() + (s + 1) * HAND_SIZE);
        std::sort(p.hand.begin(), p.hand.end());
        p.tricks = 0;
        p.taken.clear();
        p.handPoints = 0;
    }
    if (firstChooser_ < 0) { // the Karo 2 holder of the first deal chooses first
        const int k2 = makeCard(Karo, 2);
        for (int s = 0; s < 4; ++s)
            if (std::find(seats_[s].hand.begin(), seats_[s].hand.end(), k2) != seats_[s].hand.end()) firstChooser_ = s;
    }
    chooser_ = current_ = (firstChooser_ + handIndex_) % 4;
    contract_ = Contract::ElAlmaz;
    trump_ = -1;
    trick_ = Trick{};
    lastTrick_ = Trick{};
    tricks_.clear();
    plays_.clear();
    heartsBroken_ = trumpBroken_ = false;
    played_ = 0;
    voids_.fill(0);
    stage_ = Stage::Choosing;

    GameEvent h;
    h.type = EvType::HandStart;
    h.seat = chooser_;
    h.amount = handIndex_;
    h.text = std::to_string(handIndex_ + 1) + ". el — seçim sırası " +
             (isSen(chooser_) ? std::string("sende") : locative(seats_[chooser_].name));
    push(std::move(h));
    GameEvent d;
    d.type = EvType::Deal;
    d.amount = HAND_SIZE;
    d.text = "Kartlar dağıtıldı";
    push(std::move(d));
}

int Game::cezaRemaining(Contract c) const {
    if (!isCeza(c)) return 0;
    return std::max(0, rules_.maxPerCeza - timesChosen_[(int)c]);
}

bool Game::canChoose(int seat, Contract c, std::string* why) const {
    auto no = [&](const std::string& e) {
        if (why) *why = e;
        return false;
    };
    if (stage_ != Stage::Choosing) return no("Şu an oyun seçme zamanı değil");
    if (seat != chooser_) return no("Seçim sırası sende değil");
    if ((int)c < 0 || (int)c >= NUM_CONTRACTS) return no("Geçersiz oyun");
    if (c == Contract::Koz) {
        if (kozLeft(seat) <= 0) return no("Koz hakkın bitti, ceza seçmelisin");
        if (rules_.firstRoundCezaOnly && handIndex_ < 4) return no("İlk turda koz seçilemez, ceza seçmelisin");
        return true;
    }
    if (cezaLeft(seat) <= 0) return no("Ceza hakkın bitti, koz seçmelisin");
    if (cezaRemaining(c) <= 0)
        return no(std::string(contractNameTR(c)) + " " + std::to_string(rules_.maxPerCeza) +
                  " kez oynandı, artık seçilemez");
    return true;
}

std::vector<ContractOption> Game::options(int seat) const {
    std::vector<ContractOption> v;
    for (int i = 0; i < NUM_CONTRACTS; ++i) {
        ContractOption o;
        o.contract = (Contract)i;
        o.remaining = isCeza(o.contract) ? cezaRemaining(o.contract) : (seat >= 0 && seat < 4 ? kozLeft(seat) : 0);
        o.allowed = canChoose(seat, o.contract, &o.reason);
        if (o.allowed) o.reason.clear();
        v.push_back(std::move(o));
    }
    return v;
}

ActionResult Game::chooseContractImpl(int seat, Contract c, int trump) {
    std::string why;
    if (!canChoose(seat, c, &why)) return ActionResult::fail(why);
    if (c == Contract::Koz && (trump < 0 || trump >= NUM_SUITS)) return ActionResult::fail("Koz rengini seçmelisin");
    contract_ = c;
    trump_ = c == Contract::Koz ? trump : -1;
    if (c == Contract::Koz)
        ++seats_[seat].kozUsed;
    else
        ++seats_[seat].cezaUsed;
    ++timesChosen_[(int)c];
    stage_ = Stage::Playing;
    trick_ = Trick{};
    trick_.index = 0;
    trick_.leader = chooser_;

    GameEvent e;
    e.type = EvType::ContractChosen;
    e.seat = seat;
    e.contract = c;
    e.trump = trump_;
    if (c == Contract::Koz)
        e.text = says(seat, std::string("koz seçti: ") + suitNameTR(trump), std::string("koz seçtin: ") + suitNameTR(trump));
    else
        e.text = says(seat, std::string(kContractLower[(int)c]) + " seçti", std::string(kContractLower[(int)c]) + " seçtin");
    push(std::move(e));
    beginTurn(chooser_);
    return ActionResult::success();
}

void Game::beginTurn(int seat) {
    current_ = seat;
    GameEvent e;
    e.type = EvType::TurnStart;
    e.seat = seat;
    e.text = isSen(seat) ? std::string("Sıra sende") : "Sıra " + locative(seats_[seat].name);
    push(std::move(e));
}

TrickContext Game::trickContext() const {
    TrickContext t;
    t.contract = contract_;
    t.trump = trump_;
    t.n = (int)trick_.cards.size();
    for (int i = 0; i < t.n && i < 4; ++i) t.cards[i] = trick_.cards[i].card;
    t.heartsBroken = heartsBroken_;
    t.trumpBroken = trumpBroken_;
    return t;
}

int Game::ledSuit() const { return trick_.cards.empty() ? -1 : suitOf(trick_.cards[0].card); }

int Game::trickWinnerSoFar() const {
    if (trick_.cards.empty()) return -1;
    const TrickContext t = trickContext();
    return trick_.cards[trickWinnerIndex(t.cards, t.n, contract_, trump_)].seat;
}

std::vector<int> Game::legalCards(int seat) const {
    if (stage_ != Stage::Playing || seat != current_) return {};
    return cardsOf(legalMask(rules_, trickContext(), maskOf(seats_[seat].hand)));
}

bool Game::isLegal(int seat, int card) const {
    if (stage_ != Stage::Playing || seat != current_ || !isValidCard(card)) return false;
    return (legalMask(rules_, trickContext(), maskOf(seats_[seat].hand)) & cardBit(card)) != 0;
}

ActionResult Game::playCardImpl(int seat, int card) {
    if (stage_ != Stage::Playing) return ActionResult::fail("Şu an kart atma zamanı değil");
    if (seat < 0 || seat >= 4 || seat != current_) return ActionResult::fail("Sıra sende değil");
    std::vector<int>& hand = seats_[seat].hand;
    auto it = std::find(hand.begin(), hand.end(), card);
    if (!isValidCard(card) || it == hand.end()) return ActionResult::fail("Bu kart elinde yok");
    const TrickContext t = trickContext();
    const CardMask hm = maskOf(hand);
    const PlayRule why = whyIllegal(rules_, t, hm, card);
    if (why != PlayRule::None) {
        const int led = t.n > 0 ? suitOf(t.cards[0]) : -1;
        const std::string ledName = led >= 0 ? suitLowerTR(led) : "";
        switch (why) {
        case PlayRule::FollowSuit: return ActionResult::fail("Elinde " + ledName + " varken başka renk atamazsın");
        case PlayRule::BeatLed: return ActionResult::fail("Yerdekinden büyük " + ledName + " atmak zorundasın");
        case PlayRule::RaiseTrump: return ActionResult::fail("Kozu yükseltmek zorundasın: elinde daha büyük koz var");
        case PlayRule::MustTrump:
            return ActionResult::fail("Elinde " + ledName + " yok, koz atmak zorundasın");
        case PlayRule::Overtrump: return ActionResult::fail("Yerdeki kozdan büyük koz atmak zorundasın");
        case PlayRule::LeadNoTrump: return ActionResult::fail("Koz henüz açılmadı, kozla başlayamazsın");
        case PlayRule::LeadNoHeart: return ActionResult::fail("Kupa henüz açılmadı, kupayla başlayamazsın");
        case PlayRule::DropUnder: {
            const CardMask drop = hm & penaltyCards(contract_) & suitMask(led);
            if (popcount(drop) == 1)
                return ActionResult::fail("Yerde büyüğü varken " + cardAccusativeTR(lowestCard(drop)) + " atmak zorundasın");
            return ActionResult::fail("Yerde büyüğü varken erkeğini atmak zorundasın");
        }
        case PlayRule::VoidQueen: return ActionResult::fail("Elinde " + ledName + " yok, kız atmak zorundasın");
        case PlayRule::VoidErkek:
            return ActionResult::fail("Elinde " + ledName + " yok, erkek (papaz ya da vale) atmak zorundasın");
        case PlayRule::VoidRifki: return ActionResult::fail("Elinde " + ledName + " yok, rıfkıyı atmak zorundasın");
        case PlayRule::VoidHeart: return ActionResult::fail("Elinde " + ledName + " yok, kupa atmak zorundasın");
        case PlayRule::None: break;
        }
    }

    hand.erase(it);
    if (t.n > 0 && suitOf(card) != suitOf(t.cards[0])) voids_[seat] |= 1 << suitOf(t.cards[0]);
    trick_.cards.push_back(TrickCard{seat, card});
    plays_.push_back(PlayRecord{seat, card, trick_.index});
    played_ |= cardBit(card);
    if (suitOf(card) == Kupa) heartsBroken_ = true;
    if (contract_ == Contract::Koz && suitOf(card) == trump_) trumpBroken_ = true;

    GameEvent e;
    e.type = EvType::Play;
    e.seat = seat;
    e.card = card;
    e.text = says(seat, cardNameTR(card) + " attı", cardNameTR(card) + " attın");
    push(std::move(e));

    if (trick_.cards.size() == 4)
        finishTrick();
    else
        beginTurn(nextSeat(seat));
    return ActionResult::success();
}

void Game::finishTrick() {
    int cards[4];
    CardMask m = 0;
    for (int i = 0; i < 4; ++i) {
        cards[i] = trick_.cards[i].card;
        m |= cardBit(cards[i]);
    }
    const int w = trick_.cards[trickWinnerIndex(cards, 4, contract_, trump_)].seat;
    const int pts = trickPoints(rules_, contract_, m, trick_.index);
    trick_.winner = w;
    trick_.points = pts;
    SeatInfo& p = seats_[w];
    ++p.tricks;
    p.handPoints += pts;

    GameEvent e;
    e.type = EvType::TrickWon;
    e.seat = w;
    e.amount = pts;
    for (int i = 0; i < 4; ++i) e.cards.push_back(cards[i]);
    {
        std::string what = isSen(w) ? std::string("Eli sen aldın") : seats_[w].name + " eli aldı";
        if (contract_ == Contract::SonIki && pts != 0)
            what = (trick_.index == NUM_TRICKS - 1 ? "Son eli " : "Sondan ikinci eli ") +
                   (isSen(w) ? std::string("sen aldın!") : seats_[w].name + " aldı!");
        if (pts != 0 && !isCardCeza(contract_)) what += " " + signedPoints(pts);
        e.text = what;
    }
    push(std::move(e));

    if (isCardCeza(contract_)) {
        for (int i = 0; i < 4; ++i) {
            const int v = cardPoints(rules_, contract_, cards[i]);
            if (v == 0) continue;
            p.taken.push_back(cards[i]);
            GameEvent pc;
            pc.type = EvType::PenaltyCard;
            pc.seat = w;
            pc.card = cards[i];
            pc.amount = v;
            if (contract_ == Contract::Rifki)
                pc.text = isSen(w) ? "Rıfkıyı sen aldın! " + signedPoints(v)
                                   : seats_[w].name + " rıfkıyı aldı! " + signedPoints(v);
            else
                pc.text = isSen(w) ? cardAccusativeTR(cards[i]) + " sen aldın " + signedPoints(v)
                                   : seats_[w].name + " " + cardAccusativeTR(cards[i]) + " aldı " + signedPoints(v);
            push(std::move(pc));
        }
    }

    lastTrick_ = trick_;
    tricks_.push_back(trick_);
    const int nextIndex = trick_.index + 1;
    trick_ = Trick{};
    trick_.index = nextIndex;
    trick_.leader = w;

    const bool allOut = isCardCeza(contract_) && (penaltyCards(contract_) & ~played_) == 0;
    if (nextIndex >= NUM_TRICKS || (rules_.endEarly && allOut)) {
        endHand(nextIndex < NUM_TRICKS);
        return;
    }
    beginTurn(w);
}

std::string Game::earlyEndText() const {
    switch (contract_) {
    case Contract::KupaAlmaz: return "Bütün kupalar çıktı, el bitti";
    case Contract::ErkekAlmaz: return "Bütün erkekler çıktı, el bitti";
    case Contract::KizAlmaz: return "Bütün kızlar çıktı, el bitti";
    case Contract::Rifki: return "Rıfkı düştü, el bitti";
    default: return "El bitti";
    }
}

void Game::endHand(bool early) {
    HandRecord rec;
    rec.index = handIndex_;
    rec.chooser = chooser_;
    rec.contract = contract_;
    rec.trump = trump_;
    rec.label = contractLabelTR(contract_, trump_);
    rec.tricksPlayed = (int)tricks_.size();
    rec.endedEarly = early;
    for (int s = 0; s < 4; ++s) {
        SeatInfo& p = seats_[s];
        p.total += p.handPoints;
        rec.points[s] = p.handPoints;
        rec.totals[s] = p.total;
        rec.tricks[s] = p.tricks;
        switch (contract_) {
        case Contract::ElAlmaz:
        case Contract::Koz: rec.units[s] = p.tricks; break;
        case Contract::SonIki: rec.units[s] = rules_.unit[(int)contract_] ? -p.handPoints / rules_.unit[(int)contract_] : 0; break;
        default: rec.units[s] = (int)p.taken.size(); break;
        }
    }
    sheet_.push_back(rec);

    if (early) {
        GameEvent e;
        e.type = EvType::HandEndEarly;
        e.contract = contract_;
        e.amount = (int)tricks_.size();
        e.text = earlyEndText();
        push(std::move(e));
    }

    GameEvent e;
    e.type = EvType::HandEnd;
    e.contract = contract_;
    e.trump = trump_;
    e.amount = handIndex_;
    e.points = rec.points;
    e.text = std::to_string(handIndex_ + 1) + ". el bitti: " + rec.label;
    static const char* const unitWord[NUM_CONTRACTS] = {"el", "kupa", "erkek", "kız", "rıfkı", "el", "el"};
    for (int s = 0; s < 4; ++s) {
        std::string who = isSen(s) ? std::string("Sen") : seats_[s].name;
        std::string line = who + ": ";
        if (rec.units[s] == 0)
            line += contract_ == Contract::Koz ? "el yok, 0" : "temiz, 0";
        else
            line += std::to_string(rec.units[s]) + " " + unitWord[(int)contract_] + ", " + signedPoints(rec.points[s]);
        line += " (toplam " + signedPoints(rec.totals[s]) + ")";
        e.lines.push_back(line);
    }
    push(std::move(e));

    stage_ = Stage::HandOver;
    if (handIndex_ + 1 >= numHands()) {
        stage_ = Stage::MatchOver;
        GameEvent m;
        m.type = EvType::MatchEnd;
        const std::array<int, 4> rank = ranking();
        m.seat = rank[0];
        for (int s = 0; s < 4; ++s) m.points[s] = seats_[s].total;
        const bool tie = seats_[rank[1]].total == seats_[rank[0]].total;
        if (tie)
            m.text = "Parti bitti! Berabere (" + signedPoints(seats_[rank[0]].total) + ")";
        else if (isSen(rank[0]))
            m.text = "Parti bitti! Sen kazandın! (" + signedPoints(seats_[rank[0]].total) + ")";
        else
            m.text = "Parti bitti! Kazanan " + seats_[rank[0]].name + " (" + signedPoints(seats_[rank[0]].total) + ")";
        for (int i = 0; i < 4; ++i) {
            const int s = rank[i];
            m.lines.push_back(std::to_string(i + 1) + ". " + (isSen(s) ? std::string("Sen") : seats_[s].name) + ": " +
                              signedPoints(seats_[s].total));
        }
        push(std::move(m));
    }
}

std::array<int, 4> Game::ranking() const {
    std::array<int, 4> r{{0, 1, 2, 3}};
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) { return seats_[a].total > seats_[b].total; });
    return r;
}

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::push(GameEvent e) { kart::pushEvent(events_, std::move(e)); }

std::string Game::says(int s, const std::string& third, const std::string& second) const {
    return isSen(s) ? capitalizeFirst(second) : seats_[s].name + " " + third;
}

void Game::debugSetHands(const std::array<std::vector<int>, 4>& hands) {
    for (int s = 0; s < 4; ++s) {
        seats_[s].hand = hands[s];
        std::sort(seats_[s].hand.begin(), seats_[s].hand.end());
    }
}

// ---------------------------------------------------------------------------------------------------------
// the match's action log (save / resume)

ActionResult Game::chooseContract(int seat, Contract c, int trump) {
    ActionResult r = chooseContractImpl(seat, c, trump);
    if (r.ok) log_.push_back({LogKind::Choose, seat, (int)c, trump_});
    return r;
}

ActionResult Game::playCard(int seat, int card) {
    ActionResult r = playCardImpl(seat, card);
    if (r.ok) log_.push_back({LogKind::Play, seat, card, -1});
    return r;
}

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Choose:
        if (a.value < 0 || a.value >= NUM_CONTRACTS) return false;
        return chooseContract(a.seat, (Contract)a.value, a.trump).ok;
    case LogKind::Play: return playCard(a.seat, a.value).ok;
    case LogKind::NextHand:
        if (stage_ != Stage::HandOver) return false;
        startNextHand();
        return true;
    }
    return false;
}

std::string LoggedAction::encode() const {
    return std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(value) + " " + std::to_string(trump);
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    int v[4] = {0, 0, 0, 0};
    if (!parseInts(line, v, 4)) return false;
    if (v[0] < 0 || v[0] > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)v[0];
    out.seat = v[1];
    out.value = v[2];
    out.trump = v[3];
    return true;
}

} // namespace king
