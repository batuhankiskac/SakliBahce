// 101 Okey rules engine. Rules: DESIGN.md §2, semantics: DESIGN.md §3 and Game.h.
#include "core/Game.h"
#include "core/OkeyHand.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace okey {

namespace {

// A log entry (every field named: no brace-init with fields left out, -Wmissing-field-initializers).
LoggedAction act(LogKind kind, int seat, int tile = -1, int meld = -1, int side = 0,
                 std::vector<std::vector<int>> melds = {}) {
    LoggedAction a;
    a.kind = kind;
    a.seat = seat;
    a.tile = tile;
    a.meld = meld;
    a.side = side;
    a.melds = std::move(melds);
    return a;
}

// Safety valve for headless loops that never drain the queue (the UI drains every frame).
constexpr size_t MAX_QUEUED_EVENTS = 20000;
constexpr int INITIAL_HAND = 21;         // 101: everyone gets 21, the starter one more
constexpr int INITIAL_HAND_CLASSIC = 14; // klasik okey: 14, the starter 15

// Minimal UTF-8 decoder (player names are short; stray bytes are passed through as-is).
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
        if (i + extra >= s.size()) extra = 0; // truncated sequence: keep the raw byte
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(extra ? cp : c);
        i += 1 + extra;
    }
    return out;
}

// Turkish locative for a proper name with vowel harmony and consonant assimilation:
// "Hacı Rıza'da", "Kel Mahmut'ta", "Emekli Nuri'de".
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
        case 0xE7: case 0xC7: case 0x15F: case 0x15E: // ç Ç ş Ş
            hard = true;
            break;
        default:
            break;
        }
    }
    return name + "'" + (hard ? "t" : "d") + (backVowel ? "a" : "e");
}

// Tile name for event texts; the wild tile is simply "okey".
std::string tileText(int id, const OkeyInfo& ok) { return ok.isJoker(id) ? std::string("okey") : tileNameTR(id, ok); }

// The same name as a definite object (belirtme hali): "Kırmızı 10'u", "Mavi 2'yi", "okeyi",
// "Sahte Okeyi (Sarı 7)". The suffix follows the spoken number: bir'i, iki'yi, üç'ü, ..., on'u.
std::string tileAccusative(int id, const OkeyInfo& ok) {
    if (ok.isJoker(id)) return "okeyi";
    const std::string name = tileNameTR(id, ok);
    if (!isValidTile(id)) return name;
    if (isFakeJoker(id)) {
        const size_t paren = name.find(" (");
        return paren == std::string::npos ? name + "i" : name.substr(0, paren) + "i" + name.substr(paren);
    }
    // by last digit: on, bir, iki, üç, dört, beş, altı, yedi, sekiz, dokuz (11-13 end in bir/iki/üç)
    static const char* const kSuffix[10] = {"'u", "'i", "'yi", "'ü", "'ü", "'i", "'yı", "'yi", "'i", "'u"};
    return name + kSuffix[printedNumber(id) % 10];
}

// Upper-cases the first letter the Turkish way ("işlek" -> "İşlek", "ılık" -> "Ilık", "çiftten" -> "Çiftten").
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

} // namespace

// ---------------------------------------------------------------------------------------------------------
// setup

Game::Game(const RulesConfig& cfg) {
    setRules(cfg);
    static const char* const kNames[NUM_PLAYERS] = {"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"};
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        players_[s].name = kNames[s];
        players_[s].human = (s == 0);
    }
}

void Game::setRules(const RulesConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.numHands < 1) cfg_.numHands = 1;
    if (cfg_.minPairsToOpen < 1) cfg_.minPairsToOpen = 1;
    if (cfg_.openThreshold < 1) cfg_.openThreshold = 1;
}

void Game::setPlayer(int seat, const std::string& name, bool human) {
    if (seat < 0 || seat >= NUM_PLAYERS) return;
    players_[seat].name = name;
    players_[seat].human = human;
}

void Game::startMatch(uint64_t seed) {
    rng_.reseed(seed);
    matchSeed_ = seed;
    log_.clear();
    events_.clear();
    handMults_.clear();
    for (PlayerInfo& p : players_) {
        p.totalScore = classic() ? cfg_.okeyStartPoints : 0;
        p.handScores.clear();
    }
    lastResult_ = HandResult();
    handIndex_ = 0;
    turnNumber_ = 0;
    firstStarter_ = rng_.range(NUM_PLAYERS);
    starter_ = firstStarter_;

    GameEvent e;
    e.type = EvType::MatchStart;
    e.amount = cfg_.numHands;
    if (classic()) e.text = "Yeni okey oyunu başladı (herkes " + std::to_string(cfg_.okeyStartPoints) + " puanla)";
    else e.text = std::string(teams() ? "Yeni eşli maç başladı (" : "Yeni maç başladı (") + std::to_string(cfg_.numHands) + " el)";
    push(std::move(e));
    dealHand();
}

void Game::startNextHand() {
    if (handState_ != HandState::HandOver) return;
    log_.push_back(act(LogKind::NextHand, -1));
    ++handIndex_;
    starter_ = rightOf(starter_);
    dealHand();
}

void Game::dealHand() {
    for (PlayerInfo& p : players_) {
        p.hand.clear();
        p.discards.clear();
        p.opened = false;
        p.openedWithPairs = false;
        p.openedTurn = -1;
        p.openValue = 0;
        p.handPenalty = 0;
    }
    table_.clear();
    discardHistory_.clear();
    pendingLeftTile_ = -1;
    tookLeftThisTurn_ = false;
    returnedLeftThisTurn_ = false;
    indicatorShownBy_ = -1;
    lastResult_ = HandResult();

    std::vector<int> deck(NUM_TILES);
    std::iota(deck.begin(), deck.end(), 0);
    rng_.shuffle(deck);

    // Gösterge: the first numbered tile from the top. Skipped sahte okeys are shuffled back into the deck.
    std::vector<int> skipped;
    while (isFakeJoker(deck.back())) {
        skipped.push_back(deck.back());
        deck.pop_back();
    }
    const int indicator = deck.back();
    deck.pop_back();
    for (int f : skipped) deck.insert(deck.begin() + rng_.range((int)deck.size() + 1), f);
    okey_ = OkeyInfo::fromIndicator(indicator);

    const int dealt = classic() ? INITIAL_HAND_CLASSIC : INITIAL_HAND;
    for (int k = 0; k < dealt; ++k) {
        for (int i = 0; i < NUM_PLAYERS; ++i) {
            players_[(starter_ + i) % NUM_PLAYERS].hand.push_back(deck.back());
            deck.pop_back();
        }
    }
    players_[starter_].hand.push_back(deck.back());
    deck.pop_back();
    pile_ = std::move(deck);

    handState_ = HandState::Playing;
    GameEvent e;
    e.type = EvType::HandStart;
    e.player = starter_;
    e.amount = handIndex_;
    e.tile = indicator;
    e.text = std::to_string(handIndex_ + 1) + ". el başladı · Gösterge: " + tileNameTR(indicator, okey_);
    push(std::move(e));

    beginTurn(starter_);
    stage_ = TurnStage::Play; // the starter already holds one tile more (22 / 15): no draw
}

void Game::beginTurn(int seat) {
    current_ = seat;
    stage_ = TurnStage::NeedDraw;
    ++turnNumber_;
    pendingLeftTile_ = -1;
    tookLeftThisTurn_ = false;
    returnedLeftThisTurn_ = false;

    GameEvent e;
    e.type = EvType::TurnStart;
    e.player = seat;
    e.text = isSen(seat) ? std::string("Sıra sende") : "Sıra " + locative(players_[seat].name);
    push(std::move(e));
}

// ---------------------------------------------------------------------------------------------------------
// queries

int Game::topDiscard(int seat) const {
    if (seat < 0 || seat >= NUM_PLAYERS || players_[seat].discards.empty()) return -1;
    return players_[seat].discards.back();
}

bool Game::openedThisTurn(int seat) const {
    if (seat < 0 || seat >= NUM_PLAYERS) return false;
    return players_[seat].opened && players_[seat].openedTurn == turnNumber_;
}

bool Game::canTakeFromLeft(int seat) const {
    return handState_ == HandState::Playing && seat == current_ && stage_ == TurnStage::NeedDraw &&
           !returnedLeftThisTurn_ && topDiscard(leftOf(seat)) >= 0;
}

bool Game::canWorkTable(int seat) const {
    if (handState_ != HandState::Playing || seat != current_ || stage_ != TurnStage::Play) return false;
    const PlayerInfo& p = players_[seat];
    return p.opened && (!cfg_.waitTurnAfterOpening || p.openedTurn != turnNumber_);
}

bool Game::isPlayableOnTable(int tile) const { return fitsAnyMeld(table_, tile, okey_); }

int Game::handPoints(int seat) const {
    if (seat < 0 || seat >= NUM_PLAYERS) return 0;
    int sum = 0;
    for (int id : players_[seat].hand)
        if (!okey_.isJoker(id)) sum += okey_.faceNumber(id);  // an okey left in hand is a penalty instead
    return sum;
}

int Game::jokersInHand(int seat) const {
    if (seat < 0 || seat >= NUM_PLAYERS) return 0;
    int n = 0;
    for (int id : players_[seat].hand) n += okey_.isJoker(id) ? 1 : 0;
    return n;
}

int Game::seriesOpenNeed() const {
    int need = cfg_.openThreshold;
    if (cfg_.katlamali)
        for (const PlayerInfo& p : players_)
            if (p.opened && !p.openedWithPairs) need = std::max(need, p.openValue + 1);
    return need;
}

int Game::pairsOpenNeed() const {
    int need = cfg_.minPairsToOpen;
    if (cfg_.katlamali)
        for (const PlayerInfo& p : players_)
            if (p.opened && p.openedWithPairs) need = std::max(need, p.openValue + 1);
    return need;
}

bool Game::pairsOpenedByOther(int seat) const {
    for (int s = 0; s < NUM_PLAYERS; ++s)
        if (s != seat && players_[s].opened && players_[s].openedWithPairs) return true;
    return false;
}

OpenCheck Game::checkOpen(int seat, const std::vector<std::vector<int>>& groups) const {
    return evaluate(seat, groups, false, nullptr);
}

OpenCheck Game::checkLay(int seat, const std::vector<std::vector<int>>& groups) const {
    return evaluate(seat, groups, true, nullptr);
}

int Game::leaderSeat() const {
    int best = 0;
    for (int s = 1; s < NUM_PLAYERS; ++s) {
        if (classic()) {
            if (players_[s].totalScore > players_[best].totalScore) best = s;
        } else if (teams()) {
            if (teamTotal(s) < teamTotal(best)) best = s;
        } else if (players_[s].totalScore < players_[best].totalScore) {
            best = s;
        }
    }
    return best;
}

int Game::indicatorTwin(int seat) const {
    if (!classic() || handState_ != HandState::Playing || seat != current_ || indicatorShownBy_ >= 0) return -1;
    for (const DiscardRecord& d : discardHistory_)
        if (d.player == seat) return -1; // only before your first discard of the hand
    const int ind = okey_.indicatorId;
    for (int id : players_[seat].hand)
        if (id != ind && id < FAKE_JOKER_A && printedColor(id) == printedColor(ind) && printedNumber(id) == printedNumber(ind))
            return id;
    return -1;
}

int Game::finishKind(int seat, int tile) const {
    if (!classic() || handState_ != HandState::Playing || seat != current_ || stage_ != TurnStage::Play) return 0;
    const std::vector<int>& h = players_[seat].hand;
    if (h.size() != (size_t)INITIAL_HAND_CLASSIC + 1) return 0;
    return classicFinishKind(h, tile, okey_);
}

// ---------------------------------------------------------------------------------------------------------
// actions

ActionResult Game::drawFromPileImpl(int seat) {
    ActionResult r = checkTurn(seat, TurnStage::NeedDraw);
    if (!r.ok) return r;
    if (pile_.empty()) return ActionResult::fail("Ortada çekilecek taş kalmadı");

    const int tile = pile_.back();
    pile_.pop_back();
    players_[seat].hand.push_back(tile);
    stage_ = TurnStage::Play;

    GameEvent e;
    e.type = EvType::DrawPile;
    e.player = seat;
    e.tile = tile;
    if (players_[seat].human) {
        const std::string t = tileText(tile, okey_);
        e.text = says(seat, "ortadan " + t + " çekti", "ortadan " + t + " çektin");
    } else {
        e.text = says(seat, "ortadan taş çekti", "ortadan taş çektin");
    }
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::takeFromLeftImpl(int seat) {
    ActionResult r = checkTurn(seat, TurnStage::NeedDraw);
    if (!r.ok) return r;
    if (returnedLeftThisTurn_) return ActionResult::fail("Geri verdiğin taşı bu turda tekrar alamazsın");
    PlayerInfo& left = players_[leftOf(seat)];
    if (left.discards.empty()) return ActionResult::fail("Yandan alınacak taş yok");

    const int tile = left.discards.back();
    left.discards.pop_back();
    players_[seat].hand.push_back(tile);
    pendingLeftTile_ = classic() ? -1 : tile; // klasik okey: the tile is simply yours
    tookLeftThisTurn_ = true;
    stage_ = TurnStage::Play;

    GameEvent e;
    e.type = EvType::TakeLeft;
    e.player = seat;
    e.tile = tile;
    const std::string t = tileText(tile, okey_);
    e.text = says(seat, "yandan " + t + " aldı", "yandan " + t + " aldın");
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::returnLeftTileImpl(int seat) {
    ActionResult r = checkTurn(seat, stage_); // hand/seat checks only
    if (!r.ok) return r;
    if (pendingLeftTile_ < 0 || stage_ != TurnStage::Play) return ActionResult::fail("Geri verilecek taş yok");

    const int tile = pendingLeftTile_;
    removeFromHand(seat, tile);
    players_[leftOf(seat)].discards.push_back(tile);
    pendingLeftTile_ = -1;
    returnedLeftThisTurn_ = true;
    stage_ = TurnStage::NeedDraw;

    GameEvent e;
    e.type = EvType::ReturnLeft;
    e.player = seat;
    e.tile = tile;
    const std::string t = tileAccusative(tile, okey_);
    const std::string what = says(seat, "yandan aldığı " + t + " geri verdi", "yandan aldığın " + t + " geri verdin");
    e.text = what;
    push(std::move(e));

    // The Penalty repeats the whole story ("... geri verdi: 101 ceza"), so a table that shows only the
    // Penalty still tells what happened.
    if (cfg_.penaltyReturnLeft) addPenalty(seat, what + ": " + std::to_string(cfg_.penalty) + " ceza");
    return ActionResult::success();
}

ActionResult Game::openHandImpl(int seat, const std::vector<std::vector<int>>& groups) {
    std::vector<Meld> melds;
    const OpenCheck c = evaluate(seat, groups, false, &melds);
    if (!c.valid) return ActionResult::fail(c.error);

    PlayerInfo& p = players_[seat];
    const int first = (int)table_.size();
    int leftTile = -1, leftNumber = 0; // the tile taken from the left that this opening uses
    for (Meld& m : melds) {
        for (const PlacedTile& t : m.tiles) {
            removeFromHand(seat, t.id);
            if (t.id == pendingLeftTile_) {
                pendingLeftTile_ = -1;
                leftTile = t.id;
                leftNumber = t.number;
            }
        }
        table_.push_back(std::move(m));
    }
    p.opened = true;
    p.openedWithPairs = c.pairs;
    p.openedTurn = turnNumber_;
    p.openValue = c.pairs ? c.pairCount : c.value;

    GameEvent e;
    e.type = EvType::Open;
    e.player = seat;
    e.meld = first;
    e.count = (int)table_.size() - first;
    e.amount = p.openValue;
    if (c.pairs) {
        const std::string n = " (" + std::to_string(c.pairCount) + " çift)";
        e.text = says(seat, "çiftten açtı" + n, "çiftten açtın" + n);
    } else {
        const std::string v = " (" + std::to_string(c.value) + ")";
        e.text = says(seat, "eli açtı" + v, "eli açtın" + v);
    }
    push(std::move(e));

    // Yandan alıp açma: the player who discarded that tile pays its number x10 (series) or x20 (pairs).
    if (leftTile >= 0 && cfg_.leftOpenPenalty) {
        const int giver = leftOf(seat);
        const int amount = leftNumber * (c.pairs ? 20 : 10);
        const std::string t = tileText(leftTile, okey_);
        addPenalty(giver,
                   says(giver, t + " attı, onunla açıldı", t + " attın, onunla açıldı") + ": " +
                       std::to_string(amount) + " ceza",
                   amount);
    }
    return ActionResult::success();
}

ActionResult Game::layMeldsImpl(int seat, const std::vector<std::vector<int>>& groups) {
    std::vector<Meld> melds;
    const OpenCheck c = evaluate(seat, groups, true, &melds);
    if (!c.valid) return ActionResult::fail(c.error);

    const int first = (int)table_.size();
    for (Meld& m : melds) {
        for (const PlacedTile& t : m.tiles) {
            removeFromHand(seat, t.id);
            if (t.id == pendingLeftTile_) pendingLeftTile_ = -1;
        }
        table_.push_back(std::move(m));
    }

    GameEvent e;
    e.type = EvType::LayMelds;
    e.player = seat;
    e.meld = first;
    e.count = (int)table_.size() - first;
    e.amount = c.pairs ? c.pairCount : c.value;
    const std::string what = std::string(e.count > 1 ? std::to_string(e.count) + " " : "") +
                             (c.pairs ? "yeni çift açt" : "yeni per açt");
    e.text = says(seat, what + "ı", what + "ın");
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::addToMeldImpl(int seat, int tile, int meldIndex, AddSide side) {
    ActionResult r = checkTurn(seat, TurnStage::Play);
    if (!r.ok) return r;
    if (classic()) return yuzbirOnly();
    const PlayerInfo& p = players_[seat];
    if (!p.opened) return ActionResult::fail("Önce elini açmalısın");
    if (cfg_.waitTurnAfterOpening && p.openedTurn == turnNumber_)
        return ActionResult::fail("Açtığın turda işleyemezsin, sonraki turunu bekle");
    if (meldIndex < 0 || meldIndex >= (int)table_.size()) return ActionResult::fail("Böyle bir per yok");
    if (!handHas(seat, tile)) return ActionResult::fail("Bu taş sende yok");
    if (table_[meldIndex].kind == MeldKind::Pair) return ActionResult::fail("Çiftlere taş işlenemez");
    Meld out;
    if (!tryAddTile(table_[meldIndex], tile, okey_, side, out)) return ActionResult::fail("Bu taş o pere işlenemez");
    if (p.hand.size() <= 1) return ActionResult::fail("Elde en az bir taş kalmalı");
    // The tile kept for the discard can't be the pending left tile (it can't be discarded).
    if (pendingLeftTile_ >= 0 && tile != pendingLeftTile_ && p.hand.size() <= 2)
        return ActionResult::fail("Yandan aldığın taş dışında elde bir taş kalmalı");

    table_[meldIndex] = std::move(out);
    removeFromHand(seat, tile);
    if (tile == pendingLeftTile_) pendingLeftTile_ = -1;

    GameEvent e;
    e.type = EvType::AddToMeld;
    e.player = seat;
    e.tile = tile;
    e.meld = meldIndex;
    const std::string t = tileAccusative(tile, okey_);
    e.text = says(seat, t + " işledi", t + " işledin");
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::swapJokerImpl(int seat, int tile, int meldIndex) {
    ActionResult r = checkTurn(seat, TurnStage::Play);
    if (!r.ok) return r;
    if (classic()) return yuzbirOnly();
    const PlayerInfo& p = players_[seat];
    if (!p.opened) return ActionResult::fail("Önce elini açmalısın");
    if (cfg_.waitTurnAfterOpening && p.openedTurn == turnNumber_)
        return ActionResult::fail("Açtığın turda okey alamazsın, sonraki turunu bekle");
    if (meldIndex < 0 || meldIndex >= (int)table_.size()) return ActionResult::fail("Böyle bir per yok");
    if (!handHas(seat, tile)) return ActionResult::fail("Bu taş sende yok");
    const Meld& m = table_[meldIndex];
    if (m.kind == MeldKind::Pair) return ActionResult::fail("Çiftteki okey alınamaz");
    if (!m.hasJoker()) return ActionResult::fail("Bu perde okey yok");
    if (okey_.isJoker(tile)) return ActionResult::fail("Okeyin yerine okey konmaz");
    Meld out;
    int freed = -1;
    if (!trySwapJoker(m, tile, okey_, out, freed)) return ActionResult::fail("Bu taş okeyin yerine konamaz");

    table_[meldIndex] = std::move(out);
    removeFromHand(seat, tile);
    players_[seat].hand.push_back(freed);
    if (tile == pendingLeftTile_) pendingLeftTile_ = -1;

    GameEvent e;
    e.type = EvType::SwapJoker;
    e.player = seat;
    e.tile = tile;
    e.meld = meldIndex;
    e.amount = freed;
    e.text = says(seat, "okeyi aldı", "okeyi aldın");
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::discardImpl(int seat, int tile) {
    ActionResult r = checkTurn(seat, TurnStage::Play);
    if (!r.ok) return r;
    if (pendingLeftTile_ >= 0)
        return ActionResult::fail(cfg_.penaltyReturnLeft ? "Yandan aldığın taşı önce masada kullan ya da Geri Ver (" +
                                                               std::to_string(cfg_.penalty) + " ceza)"
                                                         : std::string("Yandan aldığın taşı önce masada kullan ya da Geri Ver"));
    if (!handHas(seat, tile)) return ActionResult::fail("Bu taş sende yok");

    const bool joker = okey_.isJoker(tile);
    const bool finishing = !classic() && players_[seat].hand.size() == 1;
    const bool islek = !joker && !finishing && !classic() && cfg_.penaltyPlayableDiscard && isPlayableOnTable(tile);

    removeFromHand(seat, tile);
    players_[seat].discards.push_back(tile);
    discardHistory_.push_back({seat, tile});

    GameEvent e;
    e.type = EvType::Discard;
    e.player = seat;
    e.tile = tile;
    const std::string t = tileText(tile, okey_);
    e.text = says(seat, t + " attı", t + " attın");
    push(std::move(e));

    if (!finishing && !classic()) {
        const std::string pen = ": " + std::to_string(cfg_.penalty) + " ceza";
        if (joker && cfg_.penaltyJokerDiscard)
            addPenalty(seat, says(seat, "okey attı", "okey attın") + pen);
        else if (islek)
            addPenalty(seat, says(seat, "işlek taş attı", "işlek taş attın") + pen);
    }

    if (finishing) endHand(HandEndReason::PlayerFinished, seat, joker);
    else if (pile_.empty() && classic()) endClassicHand(HandEndReason::PileExhausted, -1, false, false);
    else if (pile_.empty()) endHand(HandEndReason::PileExhausted, -1, false);
    else beginTurn(rightOf(seat));
    return ActionResult::success();
}

ActionResult Game::finishHandImpl(int seat, int tile) {
    if (!classic()) return ActionResult::fail("101'de el, son taşı atınca biter");
    ActionResult r = checkTurn(seat, TurnStage::Play);
    if (!r.ok) return r;
    if (!handHas(seat, tile)) return ActionResult::fail("Bu taş sende yok");
    const int kind = finishKind(seat, tile);
    if (kind == 0) return ActionResult::fail("Kalan 14 taş per ya da yedi çift olmuyor, bitemezsin");

    const bool joker = okey_.isJoker(tile);
    removeFromHand(seat, tile);
    players_[seat].discards.push_back(tile);
    discardHistory_.push_back({seat, tile});

    GameEvent e;
    e.type = EvType::Discard;
    e.player = seat;
    e.tile = tile;
    e.count = 1; // the finishing tile
    const std::string t = tileText(tile, okey_);
    e.text = joker ? says(seat, "okeyi atarak bitti", "okeyi atarak bittin") : says(seat, t + " atıp bitti", t + " atıp bittin");
    push(std::move(e));
    endClassicHand(HandEndReason::PlayerFinished, seat, joker, kind == 2);
    return ActionResult::success();
}

ActionResult Game::showIndicatorImpl(int seat) {
    if (!classic()) return ActionResult::fail("Gösterge yalnızca okeyde gösterilir");
    if (handState_ != HandState::Playing) return ActionResult::fail("Şu an oynanan bir el yok");
    if (seat != current_) return ActionResult::fail("Sıra sende değil");
    const int twin = indicatorTwin(seat);
    if (twin < 0) {
        if (indicatorShownBy_ >= 0) return ActionResult::fail("Gösterge bu el zaten gösterildi");
        for (const DiscardRecord& d : discardHistory_)
            if (d.player == seat) return ActionResult::fail("Göstergeyi ilk taşını atmadan göstermeliydin");
        return ActionResult::fail("Elinde göstergenin eşi yok");
    }
    indicatorShownBy_ = seat;
    for (int s = 0; s < NUM_PLAYERS; ++s)
        if (s != seat) players_[s].handPenalty += cfg_.okeyIndicatorPoints * colorMultiplier();
    GameEvent e;
    e.type = EvType::ShowIndicator;
    e.player = seat;
    e.tile = twin;
    e.amount = cfg_.okeyIndicatorPoints * colorMultiplier();
    const std::string pts = ": herkesten " + std::to_string(e.amount) + " puan düştü";
    e.text = says(seat, "göstergeyi gösterdi", "göstergeyi gösterdin") + pts;
    push(std::move(e));
    return ActionResult::success();
}

ActionResult Game::yuzbirOnly() const {
    return ActionResult::fail("Okeyde masaya per açılmaz; elin bitince 14 taşını birden gösterirsin");
}

std::vector<GameEvent> Game::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// internals

void Game::endHand(HandEndReason reason, int winner, bool finishedWithJoker) {
    HandResult r;
    r.reason = reason;
    r.winner = winner;
    int mult = 1;
    if (reason == HandEndReason::PlayerFinished && winner >= 0) {
        const PlayerInfo& w = players_[winner];
        r.finishedWithJoker = finishedWithJoker;
        r.finishedWithPairs = w.opened && w.openedWithPairs;
        // elden bitiş: nobody had opened, and the winner laid the whole hand at once and finished
        bool othersOpened = false;
        for (int s = 0; s < NUM_PLAYERS; ++s) othersOpened = othersOpened || (s != winner && players_[s].opened);
        r.finishedInOneGo = w.opened && w.openedTurn == turnNumber_ && !othersOpened;
        mult = finishMultiplier(r.finishedWithJoker, r.finishedWithPairs, r.finishedInOneGo);
    }
    r.multiplier = mult;
    handMults_.push_back(mult);

    for (int s = 0; s < NUM_PLAYERS; ++s) {
        PlayerInfo& p = players_[s];
        // an okey left in an opened player's hand: +101 penalty (not multiplied)
        if (s != winner && p.opened) p.handPenalty += cfg_.penalty * jokersInHand(s);
        const int rem = handPoints(s);
        int score;
        if (s == winner) score = cfg_.winnerScore * mult;
        else if (teams() && winner >= 0 && s == partnerOf(winner)) score = 0; // eşli: the partner's hand is wiped
        else if (!p.opened) score = cfg_.unopenedScore * mult;
        else if (p.openedWithPairs) score = rem * 2 * mult;
        else score = rem * mult;
        score += p.handPenalty;
        r.remaining[s] = rem;
        r.penalties[s] = p.handPenalty;
        r.score[s] = score;
        p.handScores.push_back(score);
        p.totalScore += score;
    }
    lastResult_ = r;
    pendingLeftTile_ = -1;

    const bool matchOver = handIndex_ + 1 >= cfg_.numHands;
    handState_ = matchOver ? HandState::MatchOver : HandState::HandOver;

    GameEvent e;
    e.type = EvType::HandEnd;
    e.player = winner;
    e.amount = mult;
    if (winner >= 0) {
        e.text = says(winner, "eli bitirdi!", "eli bitirdin!");
        if (mult > 1) {
            // same order and sign as the score sheet: okeyle, çiftten, elden ... ×N
            std::string how;
            if (r.finishedWithJoker) how += "okeyle";
            if (r.finishedWithPairs) how += std::string(how.empty() ? "" : ", ") + "çiftten";
            if (r.finishedInOneGo) how += std::string(how.empty() ? "" : ", ") + "elden";
            e.text += " (" + how + " bitiş, ×" + std::to_string(mult) + ")";
        }
        // 101 kuralları: katsız oyunda kat yazılmaz; tek katta birden çok kat yine ×2
        else if (cfg_.finishMult == FinishMult::None && (r.finishedWithJoker || r.finishedWithPairs || r.finishedInOneGo)) {
            e.text += " (katsız oyun: kat yok)";
        }
    } else {
        e.text = "Ortada taş kalmadı, el bitti";
    }
    push(std::move(e));

    if (matchOver) pushMatchEnd();
}

int Game::finishMultiplier(bool withJoker, bool withPairs, bool inOneGo) const {
    const int n = (withJoker ? 1 : 0) + (withPairs ? 1 : 0) + (inOneGo ? 1 : 0);
    switch (cfg_.finishMult) {
    case FinishMult::None: return 1;
    case FinishMult::Single: return n > 0 ? 2 : 1;
    case FinishMult::Stack: break;
    }
    return 1 << n;
}

int Game::colorMultiplier() const {
    if (!classic() || !cfg_.okeyColorDouble || !isValidTile(okey_.indicatorId)) return 1;
    const int c = okey_.faceColor(okey_.indicatorId);
    return c == Red || c == Black ? 2 : 1;
}

void Game::endClassicHand(HandEndReason reason, int winner, bool finishedWithJoker, bool pairs) {
    HandResult r;
    r.reason = reason;
    r.winner = winner;
    r.indicatorShownBy = indicatorShownBy_;
    int mult = 1;
    if (reason == HandEndReason::PlayerFinished && winner >= 0) {
        r.finishedWithJoker = finishedWithJoker;
        r.finishedWithPairs = pairs;
        if (finishedWithJoker) mult *= 2;
        if (pairs) mult *= 2;
        mult *= colorMultiplier();
    }
    r.multiplier = mult;
    handMults_.push_back(mult);
    bool someoneOut = false;
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        PlayerInfo& p = players_[s];
        int lost = p.handPenalty; // the gösterge
        if (winner >= 0 && s != winner) lost += cfg_.okeyFinishPoints * mult;
        r.remaining[s] = (int)p.hand.size();
        r.penalties[s] = p.handPenalty;
        r.score[s] = -lost;
        p.handScores.push_back(-lost);
        p.totalScore -= lost;
        someoneOut = someoneOut || p.totalScore <= 0;
    }
    lastResult_ = r;
    pendingLeftTile_ = -1;
    handState_ = someoneOut ? HandState::MatchOver : HandState::HandOver;

    GameEvent e;
    e.type = EvType::HandEnd;
    e.player = winner;
    e.amount = mult;
    if (winner >= 0) {
        e.text = says(winner, "eli bitirdi!", "eli bitirdin!");
        if (mult > 1) {
            std::string how;
            if (r.finishedWithJoker) how += "okey atarak";
            if (r.finishedWithPairs) how += std::string(how.empty() ? "" : ", ") + "çiftten";
            if (colorMultiplier() > 1) how += std::string(how.empty() ? "" : ", ") + "renkli gösterge";
            e.text += " (" + how + ", ×" + std::to_string(mult) + ")";
        }
    } else {
        e.text = "Ortada taş kalmadı, el berabere bitti";
    }
    push(std::move(e));
    if (someoneOut) pushMatchEnd();
}

void Game::pushMatchEnd() {
    const int lead = leaderSeat();
    GameEvent m;
    m.type = EvType::MatchEnd;
    m.player = lead;
    m.amount = players_[lead].totalScore;
    if (teams()) {
        const int a = std::min(lead, partnerOf(lead)), b = std::max(lead, partnerOf(lead));
        m.amount = teamTotal(lead);
        if (teamTotal(0) == teamTotal(1)) m.text = "Maç berabere bitti! İki takım da " + std::to_string(m.amount);
        else if (isSen(a) || isSen(b))
            m.text = "Maç bitti! Sen ve " + players_[isSen(a) ? b : a].name + " kazandınız!";
        else
            m.text = "Maç bitti! Kazananlar: " + players_[a].name + " ve " + players_[b].name;
        push(std::move(m));
        return;
    }
    // Several seats on the best total share first place (the score sheet and match-over screen say so too);
    // `player` stays leaderSeat() so listeners keep a single seat to look at.
    std::vector<int> co;
    bool humanCo = false;
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        if (players_[s].totalScore != m.amount) continue;
        if (isSen(s)) humanCo = true;
        else co.push_back(s);
    }
    auto join = [&](const std::vector<int>& seats) {
        std::string out;
        for (size_t i = 0; i < seats.size(); ++i)
            out += (i == 0 ? "" : i + 1 == seats.size() ? " ve " : ", ") + players_[seats[i]].name;
        return out;
    };
    const char* over = classic() ? "Oyun bitti! " : "Maç bitti! ";
    if (co.size() + (humanCo ? 1 : 0) <= 1)
        m.text = isSen(lead) ? std::string(over) + "Sen kazandın!" : std::string(over) + "Kazanan: " + players_[lead].name;
    else if (co.size() + (humanCo ? 1 : 0) == (size_t)NUM_PLAYERS)
        m.text = std::string(classic() ? "Oyun" : "Maç") + " berabere bitti! Birincilik herkesin";
    else if (humanCo)
        m.text = std::string(classic() ? "Oyun" : "Maç") + " berabere bitti! Birinciliği " + join(co) + " ile paylaştın";
    else
        m.text = std::string(classic() ? "Oyun" : "Maç") + " berabere bitti! Birinciliği " + join(co) + " paylaştı";
    push(std::move(m));
}

void Game::push(GameEvent e) {
    if (events_.size() >= MAX_QUEUED_EVENTS) events_.erase(events_.begin(), events_.begin() + MAX_QUEUED_EVENTS / 2);
    events_.push_back(std::move(e));
}

ActionResult Game::checkTurn(int seat, TurnStage need) const {
    if (handState_ != HandState::Playing) return ActionResult::fail("Şu an oynanan bir el yok");
    if (seat < 0 || seat >= NUM_PLAYERS) return ActionResult::fail("Geçersiz oyuncu");
    if (seat != current_) return ActionResult::fail("Sıra sende değil");
    if (stage_ != need) {
        if (need == TurnStage::Play) return ActionResult::fail("Önce taş çekmelisin");
        if (seat == starter_ && discardHistory_.empty())
            return ActionResult::fail("Başlayan oyuncu ilk turda taş çekmez, bir taş at");
        return ActionResult::fail("Zaten taş çektin");
    }
    return ActionResult::success();
}

bool Game::removeFromHand(int seat, int tile) {
    std::vector<int>& h = players_[seat].hand;
    auto it = std::find(h.begin(), h.end(), tile);
    if (it == h.end()) return false;
    h.erase(it);
    return true;
}

bool Game::handHas(int seat, int tile) const {
    const std::vector<int>& h = players_[seat].hand;
    return std::find(h.begin(), h.end(), tile) != h.end();
}

bool Game::buildGroups(int seat, const std::vector<std::vector<int>>& groups, bool pairMode,
                       std::vector<Meld>& out, std::string& err) const {
    std::vector<int> seen;
    for (const std::vector<int>& g : groups) {
        for (int id : g) {
            if (!handHas(seat, id)) {
                err = "Bu taş sende yok";
                return false;
            }
            if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
                err = "Aynı taş iki kez kullanılamaz";
                return false;
            }
            seen.push_back(id);
        }
    }
    out.clear();
    for (const std::vector<int>& g : groups) {
        Meld m;
        std::string why;
        if (!makeMeld(g, okey_, m, pairMode, &why)) {
            err = why;
            return false;
        }
        m.owner = seat;
        out.push_back(std::move(m));
    }
    return true;
}

OpenCheck Game::evaluate(int seat, const std::vector<std::vector<int>>& groups, bool laying,
                         std::vector<Meld>* melds) const {
    OpenCheck c;
    bool anyPair = false, anySeries = false;
    for (const std::vector<int>& g : groups) {
        if (g.size() == 2) anyPair = true;
        else if (g.size() >= 3) anySeries = true;
    }
    const bool pairMode = anyPair && !anySeries;
    c.pairs = pairMode;

    // Live counters, filled even when the attempt is not (yet) valid.
    for (const std::vector<int>& g : groups) {
        Meld m;
        if (g.size() == 2) {
            if (makePair(g, okey_, m)) ++c.pairCount;
        } else if (g.size() >= 3) {
            if (makeMeld(g, okey_, m, false)) c.value += m.value();
        }
    }

    auto failWith = [&c](std::string e) {
        c.valid = false;
        c.error = std::move(e);
        return c;
    };

    ActionResult t = checkTurn(seat, TurnStage::Play);
    if (!t.ok) return failWith(t.error);
    if (classic()) return failWith(yuzbirOnly().error);
    const PlayerInfo& p = players_[seat];
    if (!laying) {
        if (p.opened) return failWith("Elini zaten açtın");
    } else {
        if (!p.opened) return failWith("Önce elini açmalısın");
        if (cfg_.waitTurnAfterOpening && p.openedTurn == turnNumber_)
            return failWith("Açtığın turda yeni per açamazsın, sonraki turunu bekle");
    }
    if (groups.empty()) return failWith("Per seçmedin");
    if (anyPair && anySeries) return failWith("Seri ve çift karıştırılamaz");
    if (laying) {
        if (p.openedWithPairs && !pairMode) return failWith("Çiftle açan yeni seri açamaz, sadece çift açabilir");
        if (!p.openedWithPairs && pairMode && !pairsOpenedByOther(seat))
            return failWith("Seriyle açan, masada çift açan biri yokken çift açamaz");
    }

    std::vector<Meld> built;
    std::string err;
    if (!buildGroups(seat, groups, pairMode, built, err)) return failWith(err);

    if (!laying) {
        // (katlamalı: the bar is one above the last opening of the same kind)
        const int needPairs = pairsOpenNeed(), needSeries = seriesOpenNeed();
        if (pairMode) {
            if (c.pairCount < needPairs)
                return failWith(std::string(cfg_.katlamali ? "Katlamalı: çift" : "Çift") + " açmak için en az " +
                                std::to_string(needPairs) + " çift gerekli (şu an " + std::to_string(c.pairCount) +
                                ")");
        } else if (c.value < needSeries) {
            return failWith(std::string(cfg_.katlamali ? "Katlamalı: açmak" : "Açmak") + " için en az " +
                            std::to_string(needSeries) + " gerekli (şu an " + std::to_string(c.value) + ")");
        }
    }

    size_t used = 0;
    bool usesPending = false;
    for (const Meld& m : built) {
        used += m.tiles.size();
        for (const PlacedTile& pt : m.tiles)
            if (pt.id == pendingLeftTile_) usesPending = true;
    }
    if (!laying && pendingLeftTile_ >= 0 && !usesPending) return failWith("Yandan aldığın taş açılışta yer almalı");
    if (used >= p.hand.size()) return failWith("Elde en az bir taş kalmalı");
    if (pendingLeftTile_ >= 0 && !usesPending && used + 2 > p.hand.size())
        return failWith("Yandan aldığın taş dışında elde bir taş kalmalı");

    c.valid = true;
    c.error.clear();
    if (melds) *melds = std::move(built);
    return c;
}

// "Sen" = the human at this table, whatever name they chose in Ayarlar.
bool Game::isSen(int seat) const { return players_[seat].human; }

// Turkish drops the pronoun: the human reads "Eli açtın (101)", the others "Hacı Rıza eli açtı (101)".
std::string Game::says(int seat, const std::string& third, const std::string& second) const {
    return isSen(seat) ? capitalizeFirst(second) : players_[seat].name + " " + third;
}

void Game::addPenalty(int seat, const std::string& text) { addPenalty(seat, text, cfg_.penalty); }

void Game::addPenalty(int seat, const std::string& text, int amount) {
    players_[seat].handPenalty += amount;
    GameEvent e;
    e.type = EvType::Penalty;
    e.player = seat;
    e.amount = amount;
    e.text = text;
    push(std::move(e));
}

} // namespace okey

// ---------------------------------------------------------------- the match's action log (save / resume)
namespace okey {

ActionResult Game::logged(ActionResult r, LoggedAction a) {
    if (r.ok) log_.push_back(std::move(a));
    return r;
}

ActionResult Game::drawFromPile(int seat) { return logged(drawFromPileImpl(seat), act(LogKind::Draw, seat)); }
ActionResult Game::takeFromLeft(int seat) { return logged(takeFromLeftImpl(seat), act(LogKind::TakeLeft, seat)); }
ActionResult Game::returnLeftTile(int seat) { return logged(returnLeftTileImpl(seat), act(LogKind::ReturnLeft, seat)); }
ActionResult Game::openHand(int seat, const std::vector<std::vector<int>>& groups) {
    return logged(openHandImpl(seat, groups), act(LogKind::Open, seat, -1, -1, 0, groups));
}
ActionResult Game::layMelds(int seat, const std::vector<std::vector<int>>& groups) {
    return logged(layMeldsImpl(seat, groups), act(LogKind::Lay, seat, -1, -1, 0, groups));
}
ActionResult Game::addToMeld(int seat, int tile, int meldIndex, AddSide side) {
    return logged(addToMeldImpl(seat, tile, meldIndex, side), act(LogKind::Add, seat, tile, meldIndex, (int)side));
}
ActionResult Game::swapJoker(int seat, int tile, int meldIndex) {
    return logged(swapJokerImpl(seat, tile, meldIndex), act(LogKind::Swap, seat, tile, meldIndex));
}
ActionResult Game::discard(int seat, int tile) { return logged(discardImpl(seat, tile), act(LogKind::Discard, seat, tile)); }
ActionResult Game::finishHand(int seat, int tile) {
    return logged(finishHandImpl(seat, tile), act(LogKind::Finish, seat, tile));
}
ActionResult Game::showIndicator(int seat) { return logged(showIndicatorImpl(seat), act(LogKind::ShowIndicator, seat)); }

bool Game::replay(const LoggedAction& a) {
    switch (a.kind) {
    case LogKind::Draw: return drawFromPile(a.seat).ok;
    case LogKind::TakeLeft: return takeFromLeft(a.seat).ok;
    case LogKind::ReturnLeft: return returnLeftTile(a.seat).ok;
    case LogKind::Open: return openHand(a.seat, a.melds).ok;
    case LogKind::Lay: return layMelds(a.seat, a.melds).ok;
    case LogKind::Add: return addToMeld(a.seat, a.tile, a.meld, (AddSide)a.side).ok;
    case LogKind::Swap: return swapJoker(a.seat, a.tile, a.meld).ok;
    case LogKind::Discard: return discard(a.seat, a.tile).ok;
    case LogKind::Finish: return finishHand(a.seat, a.tile).ok;
    case LogKind::ShowIndicator: return showIndicator(a.seat).ok;
    case LogKind::NextHand: {
        if (handState_ != HandState::HandOver) return false;
        startNextHand();
        return true;
    }
    }
    return false;
}

// "kind seat tile meld side | a b c | d e f" (melds after the bars)
std::string LoggedAction::encode() const {
    std::string s = std::to_string((int)kind) + " " + std::to_string(seat) + " " + std::to_string(tile) + " " +
                    std::to_string(meld) + " " + std::to_string(side);
    for (const auto& m : melds) {
        s += " |";
        for (int id : m) s += " " + std::to_string(id);
    }
    return s;
}

bool LoggedAction::decode(const std::string& line, LoggedAction& out) {
    out = LoggedAction();
    std::vector<std::string> tok;
    std::string cur;
    for (char ch : line) {
        if (ch == ' ') {
            if (!cur.empty()) tok.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() < 5) return false;
    auto num = [](const std::string& t, int& v) {
        char* end = nullptr;
        const long x = std::strtol(t.c_str(), &end, 10);
        if (!end || *end) return false;
        v = (int)x;
        return true;
    };
    int k = 0;
    if (!num(tok[0], k) || k < 0 || k > (int)LogKind::NextHand) return false;
    out.kind = (LogKind)k;
    if (!num(tok[1], out.seat) || !num(tok[2], out.tile) || !num(tok[3], out.meld) || !num(tok[4], out.side)) return false;
    for (size_t i = 5; i < tok.size(); ++i) {
        if (tok[i] == "|") {
            out.melds.emplace_back();
            continue;
        }
        int v = 0;
        if (out.melds.empty() || !num(tok[i], v)) return false;
        out.melds.back().push_back(v);
    }
    return true;
}

} // namespace okey
