// Shared card-table machinery of the card games (see CardTable.h).
#include "app/CardTable.h"

#include "core/Cards.h"
#include "r3d/World.h"

#include <algorithm>
#include <cmath>
#include <cstdio> // DBGREPLAY

namespace app {

namespace {

// Display order of a hand: black and red suits alternate (Maça, Kupa, Sinek, Karo), low to high.
int displayKey(int card) {
    static const int suitOrder[4] = {0, 1, 3, 2}; // kart::Maca, Kupa, Karo, Sinek -> position
    return suitOrder[kart::suitOf(card)] * 16 + kart::rankOf(card);
}

// The open dummy (eşli batak): face up on the felt in front of its owner, one short column per suit.
r3d::CardPose dummyPose(int seat, int card, const std::vector<int>& hand) {
    int col = 0, row = 0;
    static const int suitOrder[4] = {0, 1, 3, 2};
    const int sc = suitOrder[kart::suitOf(card)];
    for (int c : hand) {
        if (c == card) break;
        if (suitOrder[kart::suitOf(c)] == sc) ++row;
    }
    col = sc;
    r3d::CardPose p;
    const float right = (1.5f - (float)col) * 0.07f;
    const float out = 0.36f - (float)row * 0.022f;
    p.pos = w3d::seatLocal(seat, right, out, w3d::TABLE_Y + r3d::CARD_T * (0.5f + (float)row));
    p.rot = r3d::cardFlat(0.f, true); // readable from the player
    return p;
}

} // namespace

bool CardTableBase::init(const TableContext& ctx) {
    ctx_ = ctx;
    if (!ctx_.renderer) return false;
    built_ = cards_.init(*ctx_.renderer);
    return built_;
}

void CardTableBase::shutdown() {
    for (int s = 1; s < 4; ++s)
        if (holding_[(size_t)s] && ctx_.characters) ctx_.characters->holdCards(s, false);
    holding_.fill(false);
    if (built_ && ctx_.renderer) cards_.shutdown(*ctx_.renderer);
    built_ = false;
}

void CardTableBase::setLevel(int level) { level_ = std::clamp(level, 0, 2); }

void CardTableBase::setAnimationSpeed(float s) {
    speed_ = std::clamp(s, 0.25f, 4.f);
    cards_.setSpeed(speed_);
}

bool CardTableBase::consumeMenuRequest() {
    const bool r = menuReq_;
    menuReq_ = false;
    return r;
}

bool CardTableBase::consumeAiToggleRequest() {
    const bool r = aiReq_;
    aiReq_ = false;
    return r;
}

void CardTableBase::resetTable() {
    cards_.hideAll();
    shown_.clear();
    sweepTo_ = -1;
    sweepAt_ = -1.f;
    for (auto& w : won_) w.clear();
    dealUntil_ = 0.f;
    deal_.on = false;
    collectAt_ = -1.f;
    hover_ = -1;
    press_ = -1;
    dragging_ = false;
    botSeat_ = -1;
    botWait_ = 0.f;
    hud_.clearToasts();
}

void CardTableBase::dealFrom(int dealer) {
    shown_.clear();
    sweepAt_ = -1.f;
    collectAt_ = -1.f;
    for (auto& w : won_) w.clear();
    cards_.hideAll();
    startDeal(dealer, true);
}

void CardTableBase::dealNew(int dealer) { startDeal(dealer, false); }

void CardTableBase::startDeal(int dealer, bool shuffle) {
    DealAnim& d = deal_;
    const bool more = d.on; // (a re-deal while the last one still runs: it starts after it)
    d = DealAnim{};
    d.leave.fill(-1.f);
    d.seat.fill(-1);
    d.dealer = dealer;
    d.shuffle = shuffle;
    // who gets what, in the order it comes off the deck: one card at a time round the table from the dealer's right
    // (play order), then the extras
    std::array<std::vector<int>, 4> hands;
    size_t most = 0;
    for (int s = 0; s < 4; ++s) {
        for (int c : handOf(s))
            if (c >= 0 && c < r3d::CARD_COUNT && (shuffle || !cards_.visible(c))) hands[(size_t)s].push_back(c);
        most = std::max(most, hands[(size_t)s].size());
    }
    std::vector<int> order;
    std::array<int, 4> count{};
    for (size_t i = 0; i < most; ++i)
        for (int j = 1; j <= 4; ++j) {
            const int s = (dealer + j) % 4;
            if (i >= hands[(size_t)s].size()) continue;
            const int c = hands[(size_t)s][i];
            d.seat[(size_t)c] = s;
            d.pile[(size_t)c] = count[(size_t)s]++;
            order.push_back(c);
        }
    for (const auto& e : dealtExtras()) {
        if (e.first < 0 || e.first >= r3d::CARD_COUNT || (!shuffle && cards_.visible(e.first))) continue;
        d.extra[(size_t)e.first] = e.second;
        order.push_back(e.first);
    }
    d.total = (int)order.size();
    if (d.total == 0) return;
    d.on = true;
    d.base = deckRemaining();
    // a long deal goes a little quicker per card
    d.interval = (d.total > 24 ? 0.045f : 0.065f) / speed_;
    d.start = now_ + (more ? 0.3f / speed_ : 0.f);
    d.shuffleEnd = d.start + (shuffle ? 1.45f / speed_ : 0.f);
    d.dealStart = d.shuffleEnd + (shuffle ? 0.15f / speed_ : 0.05f / speed_);
    for (int k = 0; k < d.total; ++k) {
        const int c = order[(size_t)k];
        d.order[(size_t)c] = k;
        d.leave[(size_t)c] = d.dealStart + d.interval * (float)k;
        // every card starts in the deck (slots above the cards that stay there; the first dealt on top)
        if (!cards_.visible(c) || shuffle) cards_.place(c, deckSlot(dealer, d.base + d.total - 1 - k), false);
        dealDelay_[(size_t)c] = 0.f;
    }
    const float dealEnd = d.dealStart + d.interval * (float)d.total + 0.5f / speed_;
    float last = dealEnd;
    for (int j = 1; j <= 4; ++j) {
        const int s = (dealer + j) % 4;
        d.pickAt[(size_t)s] = dealEnd + 0.12f / speed_ * (float)(j - 1);
        // the regulars' fingers reach their pile ~0.34 s into the pick-up (Characters::holdCards)
        d.grabAt[(size_t)s] = d.pickAt[(size_t)s] + (s == 0 || !ctx_.characters ? 0.f : 0.36f / speed_);
        d.picked[(size_t)s] = count[(size_t)s] == 0;
        last = std::max(last, d.grabAt[(size_t)s]);
    }
    if (shuffle && showCutCard()) {
        d.cut = cutCard();
        d.cutFrom = d.shuffleEnd - 0.35f / speed_;
        d.cutTo = d.cutFrom + 1.35f / speed_;
        last = std::max(last, d.cutTo);
    }
    d.end = last + 0.55f / speed_;
    dealUntil_ = std::max(dealUntil_, d.end);
    if (!shuffle) sound(ui::Sfx::CardShuffle);
}

// The riffle: the deck splits into two halves beside each other, the halves fall back into one deck card by card
// (alternating), and the top half is cut off and set back.
r3d::CardPose CardTableBase::deckSlot(int dealer, int i) const {
    r3d::CardPose p = r3d::cardlayout::deck(dealer, i);
    const DealAnim& d = deal_;
    if (!d.on || !d.shuffle || now_ >= d.shuffleEnd || now_ < d.start) return p;
    const float u = (now_ - d.start) / std::max(1e-4f, d.shuffleEnd - d.start);
    const int n = std::max(1, d.base + d.total);
    const float sd = (i % 2) ? 1.f : -1.f;
    const Vector3 right = w3d::SEAT_RIGHT[dealer];
    auto half = [&]() {
        r3d::CardPose h = r3d::cardlayout::deck(dealer, i / 2);
        h.pos = Vector3Add(h.pos, Vector3Scale(right, sd * 0.045f));
        h.pos.y += 0.004f; // held up a little
        h.rot = QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, sd * 12.f * DEG2RAD), h.rot);
        return h;
    };
    if (u < 0.18f) return half();
    if (u < 0.68f) { // card i falls back at its turn (bottom first)
        const float fall = 0.20f + 0.44f * (float)i / (float)n;
        return u < fall ? half() : p;
    }
    if (u > 0.74f && u < 0.90f && i >= n / 2) { // the cut: the top half lifted off to the side, then back on
        p.pos = Vector3Add(p.pos, Vector3Scale(right, 0.085f));
        p.pos.y -= r3d::CARD_T * (float)(n / 2);
    }
    return p;
}

bool CardTableBase::dealPose(int c, r3d::CardPose& p) const {
    const DealAnim& d = deal_;
    if (!d.on || c < 0 || c >= r3d::CARD_COUNT || d.leave[(size_t)c] < 0.f) return false;
    const int s = d.seat[(size_t)c];
    if (s >= 0 && now_ >= d.grabAt[(size_t)s] && d.picked[(size_t)s]) return false; // in the hand now
    if (now_ < d.leave[(size_t)c]) {
        p = deckSlot(d.dealer, d.base + d.total - 1 - d.order[(size_t)c]);
        return true;
    }
    p = s >= 0 ? r3d::cardlayout::dealtPile(s, d.pile[(size_t)c]) : d.extra[(size_t)c];
    return true;
}

void CardTableBase::updateDeal() {
    DealAnim& d = deal_;
    if (!d.on) return;
    if (!d.shuffled && d.shuffle && now_ >= d.start) {
        d.shuffled = true;
        sound(ui::Sfx::CardShuffle);
        if (d.dealer != 0 && ctx_.characters)
            ctx_.characters->shuffleDeck(d.dealer, r3d::cardlayout::deck(d.dealer, 20).pos, 1.45f);
    }
    if (!d.handsStarted && now_ >= d.dealStart - 0.25f / speed_) {
        d.handsStarted = true;
        if (d.dealer != 0 && ctx_.characters) {
            std::vector<Vector3> to((size_t)d.total);
            for (int c = 0; c < r3d::CARD_COUNT; ++c)
                if (d.leave[(size_t)c] >= 0.f) {
                    const int s = d.seat[(size_t)c];
                    to[(size_t)d.order[(size_t)c]] = s >= 0 ? r3d::cardlayout::dealtPile(s, 0).pos : d.extra[(size_t)c].pos;
                }
            // (Characters runs at the animation speed: its interval is the one at speed 1)
            ctx_.characters->dealCards(d.dealer, r3d::cardlayout::deck(d.dealer, d.base + d.total).pos, to, d.interval * speed_);
        }
    }
    // the cards leaving the deck: a soft slide for every other one
    const int left = now_ < d.dealStart ? 0 : std::min(d.total, 1 + (int)((now_ - d.dealStart) / d.interval));
    for (; d.slid < left; ++d.slid)
        if (d.slid % 2 == 0) sound(ui::Sfx::CardSlide);
    for (int s = 0; s < 4; ++s)
        if (!d.picked[(size_t)s] && now_ >= d.pickAt[(size_t)s]) {
            d.picked[(size_t)s] = true;
            if (s == 0) soundAt(ui::Sfx::CardGather, 0.05f / speed_);
        }
    if (now_ >= d.end) d.on = false;
}

// The regulars hold their hand fanned in the left hand (picked up off the felt after a deal) and put it down when it
// is empty or laid open (eşli batak's dummy).
void CardTableBase::updateHolding() {
    if (!ctx_.characters) return;
    for (int s = 1; s < 4; ++s) {
        const bool picked = !deal_.on || deal_.picked[(size_t)s];
        const bool want = !handOf(s).empty() && !faceUpHand(s) && picked;
        if (want == holding_[(size_t)s]) continue;
        holding_[(size_t)s] = want;
        if (want && deal_.on && deal_.leave[(size_t)handOf(s).front()] >= 0.f) {
            const Vector3 at = r3d::cardlayout::dealtPile(s, 0).pos;
            ctx_.characters->holdCards(s, true, &at);
        } else {
            ctx_.characters->holdCards(s, want);
        }
    }
}

void CardTableBase::playFromHand(int seat, int card, const r3d::CardPose& p, bool toss) {
    const bool hand = seat != 0 && ctx_.characters;
    const float delay = hand ? w3d::BOT_GIVE_LEAD : 0.f;
    cards_.place(card, p, true, delay, toss ? 0.09f : 0.06f);
    if (hand) {
        if (holding_[(size_t)seat]) ctx_.characters->playCard(seat, p.pos, toss);
        else ctx_.characters->reach(seat, p.pos, 1);
    }
    soundAt(toss ? ui::Sfx::CardSnap : ui::Sfx::CardPlace, (delay + 0.42f) / speed_);
}

void CardTableBase::onPlayed(int seat, int card, bool bot) {
    const r3d::CardPose p = r3d::cardlayout::trick(seat, (int)shown_.size() + (int)(rng_.next() % 7));
    shown_.push_back({seat, card});
    if (bot && seat != 0) {
        playFromHand(seat, card, p, rng_.chance(0.25f));
    } else {
        cards_.place(card, p, true, bot ? w3d::BOT_GIVE_LEAD : 0.f, 0.06f);
        soundAt(ui::Sfx::CardPlace, ((bot ? w3d::BOT_GIVE_LEAD : 0.f) + 0.42f) / speed_);
    }
}

void CardTableBase::onTrickWon(int winner, const std::vector<int>& cards) {
    (void)cards;
    sweepTo_ = winner;
    sweepAt_ = now_ + 1.25f / speed_;
}

// The trick is pushed together (by the taker's hand) into one bundle, then goes on to the taker's pile.
void CardTableBase::sweepNow() {
    if (sweepTo_ < 0 || shown_.empty()) return;
    const bool hand = sweepTo_ != 0 && ctx_.characters;
    const float lead = hand ? 0.30f : 0.05f;
    for (size_t i = 0; i < shown_.size(); ++i)
        cards_.place(shown_[i].card, r3d::cardlayout::gathered(sweepTo_, (int)i), true, lead + 0.03f * (float)i, 0.004f);
    if (hand) {
        const int h = (int)won_[(size_t)sweepTo_].size();
        ctx_.characters->gatherCards(sweepTo_, {0.f, w3d::TABLE_Y, 0.f}, r3d::cardlayout::wonPile(sweepTo_, h).pos);
    }
    soundAt(ui::Sfx::CardGather, (lead + 0.2f) / speed_);
    sweepAt_ = -1.f;
    collectAt_ = now_ + (hand ? 0.52f : 0.42f) / speed_;
}

void CardTableBase::sweepTo(int seat, const std::vector<int>& cards, Vector3 from) {
    const bool hand = seat != 0 && ctx_.characters;
    if (hand) {
        const int h = (int)won_[(size_t)std::clamp(seat, 0, 3)].size();
        ctx_.characters->gatherCards(seat, from, r3d::cardlayout::wonPile(seat, h).pos);
    }
    soundAt(ui::Sfx::CardGather, (hand ? 0.45f : 0.05f) / speed_);
    collectTo(seat, cards, hand ? 0.45f : 0.f);
}

void CardTableBase::collectTo(int seat, const std::vector<int>& cards, float delay) {
    if (seat < 0 || seat > 3) return;
    for (size_t i = 0; i < cards.size(); ++i) {
        won_[(size_t)seat].push_back(cards[i]);
        const int h = (int)won_[(size_t)seat].size() - 1;
        cards_.place(cards[i], r3d::cardlayout::wonPile(seat, h), true, delay + 0.012f * (float)i, 0.012f);
    }
    soundAt(ui::Sfx::CardPlace, (delay + 0.35f) / speed_);
}

bool CardTableBase::tableBusy() const {
    return now_ < dealUntil_ || deal_.on || sweepAt_ >= 0.f || collectAt_ >= 0.f || cards_.animating() || extraBusy();
}

bool CardTableBase::animating() const { return tableBusy(); }

int CardTableBase::activeSeat() const { return actor(); }

void CardTableBase::say(int seat, const std::string& line, bool important) {
    if (ctx_.characters) ctx_.characters->chat(seat, line, important);
}

void CardTableBase::sound(ui::Sfx s) {
    if (ctx_.sfx) ctx_.sfx(s);
}

void CardTableBase::soundAt(ui::Sfx s, float delaySeconds) { delayedSfx_.push_back({now_ + delaySeconds, s}); }

void CardTableBase::update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput, bool aiSeat) {
    if (replay_) { // maç tekrarı: the player's seat plays its saved moves like a bot's, nobody takes input
        humanInput = false;
        aiSeat = true;
    }
    cam_ = cam;
    aiSeat_ = aiSeat;
    humanInput_ = humanInput;
    now_ += dt;
    hud_.update(dt);
    cards_.setSpeed(speed_);
    cards_.update(dt);
    for (size_t i = 0; i < delayedSfx_.size();) {
        if (delayedSfx_[i].first <= now_) {
            sound(delayedSfx_[i].second);
            delayedSfx_.erase(delayedSfx_.begin() + (std::ptrdiff_t)i);
        } else {
            ++i;
        }
    }

    ui::updateInputMode(mouse);
    const bool hudKeys = hud_.keyboard(humanInput);
    for (const r3d::GameHud::Click& c : hud_.takeClicks()) {
        if (c.id == r3d::GameHud::AI_ID) aiReq_ = aiReq_ || !replay_;
        else if (c.id == r3d::GameHud::MENU_ID) menuReq_ = true;
        else if (c.id == HINT_BUTTON) {
            if (humanInput && !aiSeat) requestHint();
        } else if (humanInput && !aiSeat) onHudClick(c.id);
    }
    if (humanInput && !aiSeat && IsKeyPressed(KEY_H)) requestHint();
    pumpEngine();

    updateDeal();
    if (sweepAt_ >= 0.f && now_ >= sweepAt_) sweepNow();
    if (collectAt_ >= 0.f && now_ >= collectAt_ && sweepTo_ >= 0) {
        std::vector<int> c;
        for (const ShownCard& sc : shown_) c.push_back(sc.card);
        collectTo(sweepTo_, c, 0.f);
        shown_.clear();
        collectAt_ = -1.f;
    }
    updateHolding();

    // the player's cards: hover, click or drag
    hover_ = -1;
    const bool canClick = humanInput && !aiSeat && humanTurn() && !tableBusy();
    if (humanInput && !aiSeat && ctx_.renderer && !hud_.mouseOverHud()) {
        mouseCards(mouse, canClick);
    } else {
        press_ = -1;
        dragging_ = false;
    }

    keyboardCards(hudKeys || !(humanInput && !aiSeat));

    // bots (and the player's seat in the Yapay Zeka mode), paced; in a replay App feeds the saved lines instead
    if (replay_) {
        botSeat_ = -1;
    } else if (!tableBusy()) {
        const int a = actor();
        if (a >= 0 && (a != 0 || aiSeat)) {
            if (botSeat_ != a) {
                botSeat_ = a;
                botWait_ = botDelay(a) / speed_;
            }
            botWait_ -= dt;
            if (botWait_ <= 0.f) {
                botSeat_ = -1;
                botStep(a);
                pumpEngine();
            }
        } else {
            botSeat_ = -1;
        }
    }
    layoutAll();
    { static bool dbgDone = false; // DBGREPLAY
      if (matchOver() && !dbgDone) { dbgDone = true; std::string t; for (auto& l : scoreLines()) t += l + " | "; std::fprintf(stderr, "[dbg] final%s: %s\n", replay_ ? " (replay)" : "", t.c_str()); } }
}

std::vector<int> CardTableBase::playableInOrder() const {
    std::vector<int> legal = playableCards();
    std::sort(legal.begin(), legal.end(), [](int a, int b) { return displayKey(a) < displayKey(b); });
    return legal;
}

// ←/→ walk over the playable cards (the others are skipped), Enter / Space plays the chosen one. The first key only
// shows the cursor (nothing is played by a stray Enter).
void CardTableBase::keyboardCards(bool off) {
    const bool can = !off && humanTurn() && !tableBusy();
    if (!can) {
        if (!humanTurn()) kbCard_ = -1;
        return;
    }
    const std::vector<int> legal = playableInOrder();
    if (legal.empty()) return;
    auto at = std::find(legal.begin(), legal.end(), kbCard_);
    int i = at == legal.end() ? -1 : (int)(at - legal.begin());
    if (i < 0) { // (a new turn: start in the middle of the playable ones, or on the İpucu card)
        const Hint* h = activeHint();
        const auto hc = h ? std::find(legal.begin(), legal.end(), h->card) : legal.end();
        i = hc != legal.end() ? (int)(hc - legal.begin()) : (int)(legal.size() - 1) / 2;
        kbCard_ = legal[(size_t)i];
    }
    const bool wasNav = ui::keyboardNav();
    const int n = (int)legal.size();
    if (ui::keyPressedRepeat(KEY_LEFT) || ui::keyPressedRepeat(KEY_RIGHT)) {
        if (wasNav) i = (i + (ui::keyPressedRepeat(KEY_RIGHT) ? 1 : -1) + n) % n;
        kbCard_ = legal[(size_t)i];
        ui::noteKeyboardNav();
        sound(ui::Sfx::TileClick);
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
        ui::noteKeyboardNav();
        if (wasNav) {
            const int c = kbCard_;
            kbCard_ = -1;
            playHumanCard(c);
            pumpEngine();
        }
    }
}

// Hover lifts a playable card out of the fan; a click plays it, and so does dragging it up onto the felt (it follows
// the mouse over the table; let go near the fan and it goes back).
void CardTableBase::mouseCards(Vector2 mouse, bool canClick) {
    const std::vector<int> mine = clickable();
    const Ray ray = ctx_.renderer->rayFromVirtual(mouse);
    hover_ = cards_.pick(ray, &mine);
    if (press_ >= 0 && !canClick) {
        press_ = -1;
        dragging_ = false;
    }
    if (canClick && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover_ >= 0) {
        press_ = hover_;
        pressAt_ = mouse;
        dragging_ = false;
    }
    if (press_ < 0) return;
    hover_ = press_;
    if (!dragging_ && Vector2Distance(mouse, pressAt_) > 14.f) dragging_ = true;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
    // released: a click, or a drag let go over the felt (above the fan)
    const int c = press_;
    const bool drop = !dragging_ || pressAt_.y - mouse.y > 90.f;
    press_ = -1;
    dragging_ = false;
    if (!drop) return;
    playHumanCard(c);
    pumpEngine();
}

void CardTableBase::layoutAll() {
    std::array<bool, r3d::CARD_COUNT> placed{};
    const bool playing = humanTurn() && !aiSeat_;
    const std::vector<int> legal = playing ? playableCards() : std::vector<int>{};
    const Hint* hint = activeHint();
    const int hintCard = hint ? hint->card : -1;
    const int kb = playing && ui::keyboardNav() ? kbCard_ : -1;
    for (int c = 0; c < r3d::CARD_COUNT; ++c) cards_.setHintGlow(c, c == hintCard);
    for (int s = 0; s < 4; ++s) {
        std::vector<int> h = handOf(s);
        const bool open = faceUpHand(s) && s != 0;
        std::sort(h.begin(), h.end(), [](int a, int b) { return displayKey(a) < displayKey(b); });
        Matrix fan;
        const bool held = s != 0 && !open && holding_[(size_t)s] && ctx_.characters && ctx_.characters->cardFan(s, fan);
        // (the cards still on the felt from the deal are not in the fan yet)
        std::vector<int> inHand;
        for (int c : h)
            if (c >= 0 && c < r3d::CARD_COUNT) {
                r3d::CardPose dp;
                if (!dealPose(c, dp)) inHand.push_back(c);
            }
        const int n = (int)inHand.size();
        for (int c : h) {
            if (c < 0 || c >= r3d::CARD_COUNT || placed[(size_t)c]) continue;
            placed[(size_t)c] = true;
            cards_.setRaise(c, 0.f);
            cards_.setTilt(c, 0.f);
            r3d::CardPose dp;
            if (dealPose(c, dp)) { // on the deck or slid into the seat's little pile
                cards_.place(c, dp, true, 0.f, 0.010f);
                cards_.setTint(c, WHITE);
                cards_.setLift(c, 0.f);
                cards_.setGlow(c, 0.f);
                continue;
            }
            const int i = (int)(std::find(inHand.begin(), inHand.end(), c) - inHand.begin());
            float delay = dealDelay_[(size_t)c];
            dealDelay_[(size_t)c] = 0.f;
            if (held) {
                cards_.follow(c, r3d::cardlayout::fanCard(fan, i, n));
            } else {
                const r3d::CardPose p = open ? dummyPose(s, c, h) : r3d::cardlayout::hand(s, i, n);
                if (!cards_.visible(c)) cards_.place(c, p, false);
                else if (s == 0 && press_ == c && dragging_ && ctx_.renderer) {
                    // dragged: the card follows the mouse a hand's breadth over the felt
                    const Ray ray = ctx_.renderer->rayFromVirtual(ui::virtualMouse());
                    const float y = w3d::TABLE_Y + 0.07f;
                    if (ray.direction.y < -1e-3f) {
                        const float t = (y - ray.position.y) / ray.direction.y;
                        r3d::CardPose dpose;
                        dpose.pos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
                        dpose.pos.z = std::min(dpose.pos.z, 0.52f);
                        dpose.rot = r3d::cardStanding(0.f, 40.f, true);
                        cards_.follow(c, dpose);
                    }
                } else {
                    cards_.place(c, p, true, delay, 0.04f);
                }
            }
            // the player's own cards: legal ones bright (raised out of the fan when hovered), the rest dimmed while
            // it's their turn
            if (s == 0) {
                const bool isLegal = std::find(legal.begin(), legal.end(), c) != legal.end();
                const bool dim = playing && hints_ && !legal.empty() && !isLegal;
                const bool hov = c == hover_ && (isLegal || !hints_);
                cards_.setTint(c, dim ? Color{150, 146, 140, 255} : WHITE);
                cards_.setLift(c, 0.f);
                cards_.setRaise(c, hov ? 0.022f : (playing && isLegal && hints_ ? 0.006f : 0.f));
                cards_.setTilt(c, hov ? 9.f : 0.f);
                cards_.setGlow(c, (c == hover_ && isLegal) ? 0.8f : 0.f);
            } else {
                cards_.setTint(c, WHITE);
                cards_.setLift(c, open && c == hover_ ? 0.016f : 0.f);
                cards_.setGlow(c, 0.f);
            }
            if (c == hintCard) {
                cards_.setTint(c, WHITE);
                if (s == 0) cards_.setRaise(c, std::max(0.014f, c == hover_ ? 0.022f : 0.f));
                else cards_.setLift(c, std::max(0.012f, c == hover_ ? 0.016f : 0.f));
            }
            if (c == kb) { // the keyboard's card: raised and lit like a hovered one (also on eşli batak's open hand)
                cards_.setTint(c, WHITE);
                if (s == 0) {
                    cards_.setRaise(c, 0.024f);
                    cards_.setTilt(c, 9.f);
                } else {
                    cards_.setLift(c, 0.018f);
                }
                cards_.setGlow(c, 1.f);
            }
        }
    }
    for (const ShownCard& sc : shown_) {
        if (sc.card >= 0 && sc.card < r3d::CARD_COUNT) {
            placed[(size_t)sc.card] = true;
            cards_.setTint(sc.card, WHITE);
            cards_.setLift(sc.card, 0.f);
            cards_.setRaise(sc.card, 0.f);
            cards_.setTilt(sc.card, 0.f);
            cards_.setGlow(sc.card, sweepTo_ >= 0 && sweepAt_ >= 0.f && sc.seat == sweepTo_ ? 0.6f : 0.f);
        }
    }
    for (auto& w : won_)
        for (int c : w)
            if (c >= 0 && c < r3d::CARD_COUNT) {
                placed[(size_t)c] = true;
                cards_.setGlow(c, 0.f);
                cards_.setTint(c, WHITE);
                cards_.setRaise(c, 0.f);
                cards_.setTilt(c, 0.f);
            }
    layoutExtra();
    for (int c = 0; c < r3d::CARD_COUNT; ++c)
        if (!placed[(size_t)c] && !extraPlaced_[(size_t)c]) cards_.hide(c);
    extraPlaced_.fill(false);
}

void CardTableBase::placeExtra(int card, const r3d::CardPose& p, bool animate, float delay) {
    if (card < 0 || card >= r3d::CARD_COUNT) return;
    extraPlaced_[(size_t)card] = true;
    cards_.setTint(card, WHITE);
    cards_.setLift(card, 0.f);
    cards_.setRaise(card, 0.f);
    cards_.setTilt(card, 0.f);
    r3d::CardPose dp;
    if (dealPose(card, dp)) { // still on the deck, or sliding to its place
        cards_.place(card, dp, true, 0.f, 0.010f);
        return;
    }
    if (deal_.on && card == deal_.cut && now_ >= deal_.cutFrom && now_ < deal_.cutTo) {
        // the cut: the deck's bottom card is turned up beside the deck for everyone to see, then slid back under
        r3d::CardPose c = r3d::cardlayout::deck(deal_.dealer, 0);
        c.pos = Vector3Add(c.pos, Vector3Add(Vector3Scale(w3d::SEAT_RIGHT[deal_.dealer], 0.075f),
                                             Vector3Scale(w3d::SEAT_DIR[deal_.dealer], -0.07f)));
        c.pos.y += 0.002f;
        c.rot = r3d::cardFlat(0.f, true);
        cards_.place(card, c, true, 0.f, 0.03f);
        return;
    }
    if (!cards_.visible(card)) cards_.place(card, p, false);
    else cards_.place(card, p, animate, delay, 0.05f);
}

void CardTableBase::submit(r3d::Renderer& r) { cards_.submit(r); }

void CardTableBase::restoreView() {
    cards_.hideAll();
    shown_.clear();
    sweepTo_ = -1;
    sweepAt_ = -1.f;
    dealUntil_ = 0.f;
    deal_.on = false;
    collectAt_ = -1.f;
    press_ = -1;
    dragging_ = false;
    dealDelay_.fill(0.f);
    hover_ = -1;
    botSeat_ = -1;
    botWait_ = 0.f;
    delayedSfx_.clear();
    hintOn_ = false;
    hud_.clearToasts();
    hud_.clearBanner();
    won_ = wonPiles();
    for (int s = 0; s < 4; ++s)
        for (size_t i = 0; i < won_[(size_t)s].size(); ++i)
            cards_.place(won_[(size_t)s][i], r3d::cardlayout::wonPile(s, (int)i), false);
    for (const auto& sc : trickOnTable()) {
        cards_.place(sc.second, r3d::cardlayout::trick(sc.first, (int)shown_.size() + (int)(rng_.next() % 7)), false);
        shown_.push_back({sc.first, sc.second});
    }
    layoutAll(); // the hands (and layoutExtra's cards) appear in place: nothing was visible
}

void CardTableBase::drawButtons(Vector2 mouse, bool aiSeat) {
    hud_.buttons(mouse, {"İpucu"}, {!aiSeat && !replay_ && hintAvailable()}, {false}, aiSeat && !replay_);
    std::string help;
    if (replay_) help = "Maç tekrarı  ·  Esc menü";
    else if (hud_.buttonFocused()) help = "Tab / Shift+Tab düğme seç  ·  Enter / Boşluk bas  ·  Oklar kartlara dön  ·  Esc menü";
    else if (hud_.panelShown()) help = "Oklar seç  ·  Enter / Boşluk onayla  ·  H ipucu  ·  Tab düğmeler  ·  Esc menü";
    else if (!aiSeat && humanTurn()) help = "Sol / Sağ kart seç  ·  Enter / Boşluk oyna  ·  H ipucu  ·  Tab düğmeler  ·  Y yapay zeka  ·  Esc menü";
    else help = "Tab düğmeler  ·  Y yapay zeka  ·  Esc menü";
    hud_.keyHelp(help);
    // the keyboard's card: a blue chevron over its top edge (the fan overlaps, a lift alone is easy to miss)
    if (ui::keyboardNav() && !aiSeat && !replay_ && humanTurn() && kbCard_ >= 0 && ctx_.renderer && !tableBusy() && !hud_.buttonFocused()) {
        const r3d::CardPose p = cards_.pose(kbCard_);
        const Vector3 top = Vector3Add(p.pos, Vector3RotateByQuaternion({0.f, 0.f, -r3d::CARD_H * 0.5f}, p.rot));
        Vector2 c;
        if (ctx_.renderer->projectToVirtual(top, c)) {
            const float s = ui::hudTextScale(), bob = 3.f * std::sin(now_ * 6.f);
            const float y = c.y - 12.f * s + bob, w = 11.f * s, h = 12.f * s;
            DrawTriangle({c.x - w - 2.f, y - h - 2.f}, {c.x, y + 2.f}, {c.x + w + 2.f, y - h - 2.f}, Color{8, 20, 34, 220});
            DrawTriangle({c.x - w, y - h}, {c.x, y}, {c.x + w, y - h}, Color{120, 210, 255, 255});
        }
    }
    // (tools/tables_check: one picture of each)
    phase_.clear();
    if (ui::keyboardNav() && !tableBusy()) {
        if (hud_.buttonFocused()) phase_ = "klavye_dugme";
        else if (hud_.panelShown()) phase_ = "klavye_panel";
        else if (!aiSeat && humanTurn() && kbCard_ >= 0) phase_ = "klavye_kart";
    }
}

bool CardTableBase::hintAvailable() const { return !aiSeat_ && !replay_ && actor() == 0 && !tableBusy(); }

bool CardTableBase::setReplayMode(bool on) {
    replay_ = on;
    if (on) aiSeat_ = true; // (the next update says it again; App's own flag comes back when the replay ends)
    botSeat_ = -1;
    botWait_ = 0.f;
    hover_ = -1;
    kbCard_ = -1;
    hintOn_ = false;
    return true;
}

int CardTableBase::replayStep(const std::string& line) {
    if (!replay_) return -1;
    if (tableBusy()) return 0; // (App waits for animating() too: a trick still on the felt, a deal)
    return replayLine(line);
}

void CardTableBase::requestHint() {
    if (!hintAvailable()) return;
    Hint h;
    if (!computeHint(h)) return;
    hint_ = h;
    hintStamp_ = decisionStamp();
    hintOn_ = true;
    sound(ui::Sfx::Button);
    hud_.toast(h.text, Color{140, 236, 150, 255}, 3.2f);
}

const CardTableBase::Hint* CardTableBase::activeHint() const {
    if (!hintOn_ || aiSeat_ || decisionStamp() != hintStamp_ || actor() != 0) return nullptr;
    return &hint_;
}

bool CardTableBase::debugHumanClick(const r3d::Renderer& r, Vector2& out) const {
    if (tableBusy()) return false;
    const std::vector<Rectangle>& pr = hud_.panelRects();
    for (size_t i = 0; i < pr.size(); ++i) {
        if (!hud_.panelEnabled()[i]) continue;
        out = {pr[i].x + pr[i].width * 0.5f, pr[i].y + pr[i].height * 0.5f};
        return true;
    }
    if (!humanTurn()) return false;
    const std::vector<int> legal = playableCards();
    if (legal.empty()) return false;
    // a legal card (the middle one first) at a point of it the mouse really hits: fanned cards overlap, so try a
    // grid over the card and keep the first point whose ray picks this card
    const std::vector<int> mine = clickable();
    for (size_t k = 0; k < legal.size(); ++k) {
        const int c = legal[(legal.size() / 2 + k) % legal.size()];
        const r3d::CardPose p = cards_.pose(c);
        for (int gy = 0; gy < 9; ++gy)
            for (int gx = 0; gx < 9; ++gx) {
                const Vector3 off = Vector3RotateByQuaternion(
                    {r3d::CARD_W * (-0.46f + 0.115f * (float)gx), 0.f, r3d::CARD_H * (-0.46f + 0.115f * (float)gy)}, p.rot);
                Vector2 v;
                if (!r.projectToVirtual(Vector3Add(p.pos, off), v)) continue;
                if (cards_.pick(r.rayFromVirtual(v), &mine) == c) {
                    out = v;
                    return true;
                }
            }
    }
    return false;
}

} // namespace app
