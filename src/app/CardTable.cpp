// Shared card-table machinery of the card games (see CardTable.h).
#include "app/CardTable.h"

#include "core/Cards.h"
#include "r3d/World.h"

#include <algorithm>
#include <cmath>

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
    hover_ = -1;
    botSeat_ = -1;
    botWait_ = 0.f;
    hud_.clearToasts();
}

void CardTableBase::dealFrom(int dealer) {
    shown_.clear();
    sweepAt_ = -1.f;
    for (auto& w : won_) w.clear();
    cards_.hideAll();
    // every card in a hand starts on the deck and is dealt round the table, one at a time
    std::array<std::vector<int>, 4> hands;
    size_t most = 0;
    for (int s = 0; s < 4; ++s) {
        hands[(size_t)s] = handOf(s);
        most = std::max(most, hands[(size_t)s].size());
    }
    int k = 0, total = 0;
    for (auto& h : hands) total += (int)h.size();
    for (size_t i = 0; i < most; ++i) {
        for (int j = 1; j <= 4; ++j) {
            const int s = (dealer + j) % 4;
            if (i >= hands[(size_t)s].size()) continue;
            const int card = hands[(size_t)s][i];
            cards_.place(card, r3d::cardlayout::deck(dealer, total - k), false);
            dealDelay_[(size_t)card] = 0.05f + 0.035f * (float)k;
            ++k;
        }
    }
    dealUntil_ = now_ + (0.05f + 0.035f * (float)k + 0.6f) / speed_;
    sound(ui::Sfx::CardShuffle);
}

void CardTableBase::onPlayed(int seat, int card, bool bot) {
    const r3d::CardPose p = r3d::cardlayout::trick(seat, (int)shown_.size() + (int)(rng_.next() % 7));
    shown_.push_back({seat, card});
    const float delay = bot ? w3d::BOT_GIVE_LEAD : 0.f;
    cards_.place(card, p, true, delay, 0.06f);
    if (bot && ctx_.characters) ctx_.characters->reach(seat, p.pos, 1);
    soundAt(ui::Sfx::CardPlace, (delay + 0.42f) / speed_);
}

void CardTableBase::onTrickWon(int winner, const std::vector<int>& cards) {
    (void)cards;
    sweepTo_ = winner;
    sweepAt_ = now_ + 1.25f / speed_;
}

void CardTableBase::sweepNow() {
    if (sweepTo_ < 0 || shown_.empty()) return;
    std::vector<int> c;
    for (const ShownCard& s : shown_) c.push_back(s.card);
    collectTo(sweepTo_, c, 0.f);
    shown_.clear();
    sweepAt_ = -1.f;
}

void CardTableBase::collectTo(int seat, const std::vector<int>& cards, float delay) {
    if (seat < 0 || seat > 3) return;
    for (size_t i = 0; i < cards.size(); ++i) {
        won_[(size_t)seat].push_back(cards[i]);
        const int h = (int)won_[(size_t)seat].size() - 1;
        cards_.place(cards[i], r3d::cardlayout::wonPile(seat, h), true, delay + 0.05f * (float)i, 0.05f);
    }
    if (seat != 0 && ctx_.characters) ctx_.characters->reach(seat, {0.f, w3d::TABLE_Y, 0.f}, 0);
    sound(ui::Sfx::CardPlace);
}

bool CardTableBase::tableBusy() const {
    return now_ < dealUntil_ || sweepAt_ >= 0.f || cards_.animating() || extraBusy();
}

void CardTableBase::dealNew(const r3d::CardPose& from) {
    int k = 0;
    for (int s = 0; s < 4; ++s) {
        for (int c : handOf(s)) {
            if (c < 0 || c >= r3d::CARD_COUNT || cards_.visible(c)) continue;
            r3d::CardPose p = from;
            p.pos.y += r3d::CARD_T * (float)k;
            cards_.place(c, p, false);
            dealDelay_[(size_t)c] = 0.05f + 0.06f * (float)k;
            ++k;
        }
    }
    if (k > 0) {
        dealUntil_ = std::max(dealUntil_, now_ + (0.05f + 0.06f * (float)k + 0.6f) / speed_);
        sound(ui::Sfx::CardShuffle);
    }
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
    cam_ = cam;
    aiSeat_ = aiSeat;
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

    for (const r3d::GameHud::Click& c : hud_.takeClicks()) {
        if (c.id == r3d::GameHud::AI_ID) aiReq_ = true;
        else if (c.id == r3d::GameHud::MENU_ID) menuReq_ = true;
        else if (humanInput && !aiSeat) onHudClick(c.id);
    }
    pumpEngine();

    if (sweepAt_ >= 0.f && now_ >= sweepAt_) sweepNow();

    // the player's cards: hover, click
    hover_ = -1;
    const bool canClick = humanInput && !aiSeat && humanTurn() && !tableBusy();
    if (humanInput && !aiSeat && ctx_.renderer && !hud_.mouseOverHud()) {
        const std::vector<int> mine = clickable();
        const Ray ray = ctx_.renderer->rayFromVirtual(mouse);
        hover_ = cards_.pick(ray, &mine);
        if (hover_ >= 0 && canClick && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            playHumanCard(hover_);
            pumpEngine();
        }
    }

    // bots (and the player's seat in the Yapay Zeka mode), paced
    if (!tableBusy()) {
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
}

void CardTableBase::layoutAll() {
    std::array<bool, r3d::CARD_COUNT> placed{};
    const bool playing = humanTurn() && !aiSeat_;
    const std::vector<int> legal = playing ? playableCards() : std::vector<int>{};
    for (int s = 0; s < 4; ++s) {
        std::vector<int> h = handOf(s);
        const bool open = faceUpHand(s) && s != 0;
        if (s == 0 || open) std::sort(h.begin(), h.end(), [](int a, int b) { return displayKey(a) < displayKey(b); });
        const int n = (int)h.size();
        for (int i = 0; i < n; ++i) {
            const int c = h[(size_t)i];
            if (c < 0 || c >= r3d::CARD_COUNT || placed[(size_t)c]) continue;
            placed[(size_t)c] = true;
            r3d::CardPose p = open ? dummyPose(s, c, h) : r3d::cardlayout::hand(s, i, n);
            float delay = dealDelay_[(size_t)c];
            dealDelay_[(size_t)c] = 0.f;
            if (!cards_.visible(c)) cards_.place(c, p, false);
            else cards_.place(c, p, true, delay, 0.04f);
            // the player's own cards: legal ones bright (lifted when hovered), the rest dimmed while it's their turn
            if (s == 0) {
                const bool isLegal = std::find(legal.begin(), legal.end(), c) != legal.end();
                const bool dim = playing && hints_ && !legal.empty() && !isLegal;
                cards_.setTint(c, dim ? Color{150, 146, 140, 255} : WHITE);
                cards_.setLift(c, (c == hover_ && (isLegal || !hints_)) ? 0.016f : (playing && isLegal && hints_ ? 0.004f : 0.f));
                cards_.setGlow(c, (c == hover_ && isLegal) ? 0.8f : 0.f);
            } else {
                cards_.setTint(c, WHITE);
                cards_.setLift(c, 0.f);
                cards_.setGlow(c, 0.f);
            }
        }
    }
    for (const ShownCard& sc : shown_) {
        if (sc.card >= 0 && sc.card < r3d::CARD_COUNT) {
            placed[(size_t)sc.card] = true;
            cards_.setTint(sc.card, WHITE);
            cards_.setLift(sc.card, 0.f);
            cards_.setGlow(sc.card, sweepTo_ >= 0 && sweepAt_ >= 0.f && sc.seat == sweepTo_ ? 0.6f : 0.f);
        }
    }
    for (auto& w : won_)
        for (int c : w)
            if (c >= 0 && c < r3d::CARD_COUNT) {
                placed[(size_t)c] = true;
                cards_.setGlow(c, 0.f);
                cards_.setTint(c, WHITE);
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
    if (!cards_.visible(card)) cards_.place(card, p, false);
    else cards_.place(card, p, animate, delay, 0.05f);
}

void CardTableBase::submit(r3d::Renderer& r) { cards_.submit(r); }

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
    // the legal card whose centre is most in view (fanned cards overlap: aim at the visible strip, left of centre)
    const int c = legal[legal.size() / 2];
    const r3d::CardPose p = cards_.pose(c);
    const Vector3 off = Vector3RotateByQuaternion({-r3d::CARD_W * 0.3f, 0.f, -r3d::CARD_H * 0.25f}, p.rot);
    return r.projectToVirtual(Vector3Add(p.pos, off), out);
}

} // namespace app
