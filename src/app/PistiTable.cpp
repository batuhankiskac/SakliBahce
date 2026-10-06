// Pişti at our table (four players, eşli, or you and Kel Mahmut): engine pisti::Game, bots pisti::Bot.
#include "app/CardTable.h"
#include "core/Pisti.h"
#include "core/PistiBot.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

const char* const kPistiSelf[4][3] = {
    {},
    {"Pişti, elhamdülillah.", "Sabırla bekledik, pişti geldi.", "Pişti! Hayırlısı."},
    {"Piştiiii! Gol!", "Pişti abi, ağlarda!", "İşte pişti, tribünler ayağa!"},
    {"Pişti. Tecrübe, delikanlı.", "Hıh, pişti. Gözlük işe yaradı.", "Pişti. Bizim zamanımızda böyle oynanırdı."},
};
const char* const kPistiHuman[4][2] = {
    {},
    {"Maşallah {h}, güzel pişti.", "Pişti ha {h}? Helal olsun."},
    {"Vay {h}, pişti! Rövanş!", "Pişti mi yedik? Hakem!"},
    {"Acemi şansı {h}.", "Hıh, pişti. Fena değil {h}."},
};
const char* const kValeLines[4][2] = {
    {},
    {"Valeyle topladık, hayırlısı.", "Vale her derde deva."},
    {"Vale geldi, hepsi bizim!", "Valeyi bastım, süpürdüm!"},
    {"Hıh, vale. Saklamasını bilene.", "Valeyi zamanında atacaksın."},
};

std::string fillName(std::string t, const std::string& h) {
    size_t p;
    while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, h);
    return t;
}

class PistiTable final : public CardTableBase {
public:
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        pisti::Rules r;
        r.mode = (pisti::Mode)std::clamp(st.pistiMode, 0, 2);
        r.targetScore = std::clamp(st.pistiTarget, 51, 301);
        g_ = pisti::Game(r);
        names_ = names;
        for (int s = 0; s < 4; ++s) {
            g_.setPlayer(s, names[(size_t)s], s == 0);
            const pisti::BotLevel lv = s == 0 ? pisti::BotLevel::Kurt : (pisti::BotLevel)std::clamp(level_, 0, 2);
            bots_[(size_t)s] = std::make_unique<pisti::Bot>(lv, seed * 4 + (uint64_t)s + 0x9157ull);
        }
        rng_.reseed(seed ^ 0x915711ull);
        replay_ = false;
        resetTable();
        pending_.clear();
        captureAt_ = -1.f;
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != pisti::Stage::HandOver) return;
        resetTable();
        pending_.clear();
        captureAt_ = -1.f;
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        for (int s = 1; s < 4; ++s)
            if (bots_[(size_t)s]) bots_[(size_t)s]->setLevel((pisti::BotLevel)level_);
    }
    std::vector<int> seats() const override { return g_.activeSeats(); }
    bool handOver() const override { return g_.stage() == pisti::Stage::HandOver || g_.stage() == pisti::Stage::MatchOver; }
    bool matchOver() const override { return g_.stage() == pisti::Stage::MatchOver; }

    std::string scoreTitle() const override {
        if (g_.stage() == pisti::Stage::NotStarted) return "";
        return "Pişti \xC2\xB7 " + std::to_string(g_.rules().targetScore) + "'e";
    }
    std::vector<std::string> scoreLines() const override {
        std::vector<std::string> v;
        for (int side = 0; side < g_.numSides(); ++side) v.push_back(g_.sideName(side) + " ....... " + std::to_string(g_.total(side)));
        return v;
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        for (int s = 1; s < 4; ++s) {
            heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
            if (!g_.isActive(s)) continue;
            r3d::GameHud::Plate& p = plates[(size_t)s];
            p.show = true;
            p.name = names_[(size_t)s];
            p.label = "Puan";
            p.value = std::to_string(g_.total(g_.sideOf(s)));
            p.turn = g_.current() == s && !tableBusy();
            p.badges.push_back({"Kâğıt " + std::to_string(g_.capturedCount(s)), Color{70, 60, 50, 255}});
            if (g_.pistiCount(s) > 0) p.badges.push_back({"Pişti " + std::to_string(g_.pistiCount(s)), Color{168, 120, 30, 255}});
        }
        hud_.plates(r, heads, plates);
        if (g_.stage() == pisti::Stage::Playing)
            hud_.label3D(r, {0.f, w3d::TABLE_Y + 0.01f, 0.12f},
                         "Yerde " + std::to_string(g_.tableCount()) + "  \xC2\xB7  destede " + std::to_string(g_.deckCount()),
                         Color{226, 216, 196, 255}, 14.f);
        Color sc = ui::pal::TextLight;
        std::string st;
        const int a = actor();
        if (g_.stage() == pisti::Stage::HandOver) st = "El bitti";
        else if (g_.stage() == pisti::Stage::MatchOver) st = "Oyun bitti";
        else if (now_ < dealUntil_) st = "Kâğıtlar dağıtılıyor…";
        else if (a == 0 && replay_) st = replaySelfStatus();
        else if (a == 0 && aiSeat) st = "Yapay zeka düşünüyor…";
        else if (a == 0) {
            sc = ui::pal::Highlight;
            st = "Bir kâğıt at";
            const int look = hover_ >= 0 ? hover_ : (ui::keyboardNav() ? kbCard_ : -1); // (the mouse's or the keyboard's)
            if (hints_ && look >= 0) {
                const int pp = g_.wouldPisti(look);
                if (pp > 0) st = "Pişti! +" + std::to_string(pp);
                else if (g_.wouldCapture(look)) st = "Bununla yerdekileri alırsın";
            }
        } else if (a > 0) {
            st = names_[(size_t)a] + " düşünüyor…";
        }
        std::vector<std::pair<std::string, Color>> parts;
        if (g_.stage() != pisti::Stage::NotStarted) {
            const int side = g_.sideOf(0);
            parts.push_back({"Kâğıdın: " + std::to_string(g_.sideCapturedCount(side)), Color{226, 216, 196, 255}});
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
            parts.push_back({"Puan: " + std::to_string(g_.total(side)), Color{238, 198, 112, 255}});
        }
        hud_.status(st, sc, a == 0 && !aiSeat && !replay_, parts, aiSeat && !replay_);
        drawButtons(mouse, aiSeat);
        hud_.toasts();
    }

    bool humanHandScore(int& score) const override {
        const pisti::HandResult& r = g_.lastHandResult();
        const int side = g_.sideOf(0);
        if (r.handIndex < 0 || side < 0) return false;
        score = r.side[(size_t)side].total;
        return true;
    }

    bool saveState(std::vector<std::string>& lines) const override {
        lines.clear();
        for (const pisti::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        bool ok = true;
        for (const std::string& l : lines) {
            pisti::LoggedAction a;
            if (!pisti::LoggedAction::decode(l, a) || !g_.replay(a)) {
                ok = false;
                break;
            }
            // nothing is animated, but the bots' memory, the sheet's history and the pişti counts follow the events
            for (const pisti::GameEvent& e : g_.drainEvents()) {
                for (auto& b : bots_)
                    if (b) b->observe(e, g_);
                if (e.type == pisti::EvType::HandStart) {
                    for (auto& b : bots_)
                        if (b) b->resetForHand();
                } else if (e.type == pisti::EvType::Pisti && e.seat >= 0 && e.seat < 4) {
                    const int sd = g_.sideOf(e.seat);
                    if (sd >= 0) ++pistis_[(size_t)sd];
                } else if (e.type == pisti::EvType::HandEnd) {
                    const pisti::HandResult& r = g_.lastHandResult();
                    std::array<int, 4> row{};
                    for (int s = 0; s < g_.numSides(); ++s) row[(size_t)s] = r.side[(size_t)s].total;
                    history_.push_back(row);
                }
            }
        }
        g_.drainEvents();
        pending_.clear();
        captureCards_.clear();
        captureSeat_ = -1;
        captureAt_ = -1.f;
        dealer_ = g_.dealer();
        log_.clear();
        restoreView();
        return ok;
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const pisti::HandResult& r = g_.lastHandResult();
        const int sides = g_.numSides();
        for (int s = 0; s < sides; ++s) {
            std::string n = g_.sideName(s);
            if (aiMode && s == g_.sideOf(0)) n = sides == 2 && g_.rules().mode == pisti::Mode::Esli ? "Yapay Zeka ve " + names_[2] : "Yapay Zeka (" + names_[0] + ")";
            m.columns.push_back(n);
        }
        m.humanCol = g_.sideOf(0);
        m.title = std::to_string(r.handIndex + 1) + ". El Sonucu";
        m.corner = "pişti: " + std::to_string(g_.rules().targetScore) + "'e";
        int best = -1, bestSide = -1;
        for (int s = 0; s < sides; ++s)
            if (r.side[(size_t)s].total > best) {
                best = r.side[(size_t)s].total;
                bestSide = s;
            }
        m.starCol = bestSide;
        m.headline = r.lastCapturer >= 0 && r.leftoverCards > 0
                         ? "Yerde kalan " + std::to_string(r.leftoverCards) + " kâğıt " + g_.sideName(g_.sideOf(r.lastCapturer)) + "'e"
                         : "El bitti";
        if (r.lastCapturer == 0 && r.leftoverCards > 0) m.headline = "Yerde kalan " + std::to_string(r.leftoverCards) + " kâğıt senin";
        m.tagline = r.majoritySide >= 0 ? "kâğıt çoğunluğu: " + g_.sideName(r.majoritySide) + " (+3)" : "kâğıtlar eşit: çoğunluk puanı yok";
        m.rowLabels = {"Kâğıt", "As / Vale", "Sinek 2 · Karo 10", "Piştiler", "Bu el"};
        m.cells.assign(5, std::vector<std::string>((size_t)sides));
        m.red.assign(5, std::vector<bool>((size_t)sides, false));
        for (int s = 0; s < sides; ++s) {
            const pisti::ScoreLine& l = r.side[(size_t)s];
            const size_t c = (size_t)s;
            m.cells[0][c] = std::to_string(l.cards) + (l.majority ? " (+3)" : "");
            m.cells[1][c] = (l.aces + l.jacks) ? std::to_string(l.aces) + " / " + std::to_string(l.jacks) : "";
            std::string sk;
            if (l.sinek2) sk += "+2";
            if (l.karo10) sk += std::string(sk.empty() ? "" : " ") + "+3";
            m.cells[2][c] = sk;
            m.cells[3][c] = l.pistiPoints ? "+" + std::to_string(l.pistiPoints) : "";
            m.red[3][c] = l.pistiPoints > 0;
            m.cells[4][c] = std::to_string(l.total);
        }
        for (size_t h = 0; h < history_.size(); ++h) {
            m.historyLabels.push_back(std::to_string(h + 1) + ". el");
            std::vector<std::string> row;
            for (int s = 0; s < sides; ++s) row.push_back("+" + std::to_string(history_[h][(size_t)s]));
            m.history.push_back(row);
        }
        int top = -1;
        for (int s = 0; s < sides; ++s) top = std::max(top, g_.total(s));
        for (int s = 0; s < sides; ++s) {
            m.totals.push_back(std::to_string(g_.total(s)));
            if (g_.total(s) == top) m.leaders.push_back(s);
        }
        m.last = g_.stage() == pisti::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.rules().targetScore) + "'e ilk ulaşan kazanır";
        m.matchHeader = "Oyun Bitti";
        m.playedLine = std::to_string(history_.size()) + " el oynandı";
        std::vector<int> order;
        for (int s = 0; s < sides; ++s) order.push_back(s);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g_.total(a) > g_.total(b); });
        m.ranking = order;
        int rank = 1;
        for (size_t i = 0; i < order.size(); ++i) {
            if (i > 0 && g_.total(order[i]) != g_.total(order[i - 1])) rank = (int)i + 1;
            m.rank.push_back(rank);
        }
        for (int s = 0; s < sides; ++s) m.rankSub.push_back(std::to_string(pistis_[(size_t)s]) + " pişti");
        const int w = g_.winnerSide();
        m.humanWon = w >= 0 && w == g_.sideOf(0);
        if (m.humanWon) {
            m.resultTitle = aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!";
            m.resultSub = std::to_string(g_.total(w)) + " puanla masanın en iyisi.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir oyun daha?";
            m.resultSub = w >= 0 ? "Kazanan: " + g_.sideName(w) + " (" + std::to_string(g_.total(w)) + ")" : "";
        }
        return m;
    }

protected:
    int replayLine(const std::string& line) override {
        pisti::LoggedAction a;
        if (!pisti::LoggedAction::decode(line, a)) return -1;
        if (a.kind == pisti::LogKind::NextHand) {
            if (g_.stage() == pisti::Stage::HandOver) return 0;     // the sheet is up: App deals the next hand
            return g_.stage() == pisti::Stage::MatchOver ? -1 : 1; // (already dealt)
        }
        if (!g_.replay(a)) return -1;
        pumpEngine();
        return 1;
    }
    void pumpEngine() override {
        bool dealt = false;
        for (const pisti::GameEvent& e : g_.drainEvents()) {
            for (auto& b : bots_)
                if (b) b->observe(e, g_);
            using E = pisti::EvType;
            switch (e.type) {
            case E::MatchStart:
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                history_.clear();
                pistis_.fill(0);
                break;
            case E::HandStart:
                for (auto& b : bots_)
                    if (b) b->resetForHand();
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                dealer_ = e.seat;
                break;
            case E::Deal: dealt = true; break;
            case E::TableTurnUp:
                if (e.jack) hud_.toast(e.text, ui::pal::TextLight, 2.4f);
                break;
            case E::Play:
                pending_.push_back(e.card);
                placeMiddle(e.seat, e.card, e.seat != 0 || aiSeat_);
                break;
            case E::Capture:
                captureSeat_ = e.seat;
                captureCards_ = e.cards;
                captureAt_ = now_ + 0.9f / speed_;
                if (kart::rankOf(e.card) == kart::Vale && e.seat >= 1 && rng_.chance(0.3f))
                    say(e.seat, kValeLines[e.seat][rng_.range(2)]);
                break;
            case E::Pisti:
                hud_.toast(e.text, ui::pal::Highlight, 3.2f);
                soundAt(ui::Sfx::CardSlap, 0.5f / speed_);
                if (e.seat >= 0 && e.seat < 4) {
                    const int sd = g_.sideOf(e.seat);
                    if (sd >= 0) ++pistis_[(size_t)sd];
                }
                if (e.seat >= 1) {
                    say(e.seat, kPistiSelf[e.seat][rng_.range(3)], true);
                    if (ctx_.characters) ctx_.characters->react(e.seat, 1, {0.f, w3d::TABLE_Y, 0.f});
                } else {
                    const int s = g_.rules().mode == pisti::Mode::Ikili ? 2 : 1 + rng_.range(3);
                    say(s, fillName(kPistiHuman[s][rng_.range(2)], names_[0]), true);
                }
                break;
            case E::LastCapture:
                captureSeat_ = e.seat;
                captureCards_ = e.cards;
                captureAt_ = now_ + 0.9f / speed_;
                hud_.toast(e.text, ui::pal::TextLight, 2.6f);
                break;
            case E::HandEnd: {
                const pisti::HandResult& r = g_.lastHandResult();
                std::array<int, 4> row{};
                for (int s = 0; s < g_.numSides(); ++s) row[(size_t)s] = r.side[(size_t)s].total;
                history_.push_back(row);
                log_ = e.text;
                hud_.toast(e.text, ui::pal::Highlight, 4.f);
                sound(ui::Sfx::Win);
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            default: break;
            }
        }
        if (dealt) {
            if (g_.dealRound() == 0) {
                // a new hand: the deck sits by the dealer, the table cards and the hands come out of it
                dealFrom(dealer_);
            } else {
                dealNew(r3d::cardlayout::deck(dealer_, 0));
            }
        }
    }
    std::vector<int> handOf(int seat) const override { return g_.hand(seat); }
    int actor() const override { return g_.stage() == pisti::Stage::Playing ? g_.current() : -1; }
    bool humanTurn() const override { return actor() == 0; }
    std::vector<int> playableCards() const override { return humanTurn() ? g_.hand(0) : std::vector<int>{}; }
    void playHumanCard(int card) override {
        const pisti::ActionResult r = g_.playCard(0, card);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.4f);
            sound(ui::Sfx::Error);
        }
    }
    float botDelay(int seat) const override {
        (void)seat;
        return 0.85f;
    }
    void botStep(int seat) override {
        int c = bots_[(size_t)seat]->next(g_, seat);
        if (!g_.playCard(seat, c).ok) g_.playCard(seat, pisti::fallbackCard(g_, seat));
    }
    size_t decisionStamp() const override { return g_.actionLog().size(); }
    bool computeHint(Hint& h) override {
        if (actor() != 0 || !bots_[0]) return false;
        const int c = bots_[0]->next(g_, 0);
        if (c < 0) return false;
        h.card = c;
        h.text = "İpucu: " + kart::cardAccusativeTR(c) + " oyna";
        const int pp = g_.wouldPisti(c);
        if (pp > 0) h.text += " (pişti! +" + std::to_string(pp) + ")";
        else if (g_.wouldCapture(c)) h.text += " (yerdekileri alır)";
        return true;
    }
    std::array<std::vector<int>, 4> wonPiles() const override {
        std::array<std::vector<int>, 4> w;
        for (int s = 0; s < 4; ++s) w[(size_t)s] = g_.capturedCards(s);
        return w;
    }
    bool extraBusy() const override { return captureAt_ >= 0.f; }
    void layoutExtra() override {
        // a capture waits a moment on the felt, then the cards go to the taker's pile
        if (captureAt_ >= 0.f && now_ >= captureAt_) {
            collectTo(captureSeat_, captureCards_, 0.f);
            for (int c : captureCards_) pending_.erase(std::remove(pending_.begin(), pending_.end(), c), pending_.end());
            captureAt_ = -1.f;
            captureCards_.clear();
        }
        // the middle: the closed cards (face down) under the open pile; the deck face down by the dealer
        int level = 0;
        for (int c : g_.closedCardsForDisplay()) placeExtra(c, r3d::cardlayout::middle(level++, false));
        for (int c : g_.tableCards()) {
            r3d::CardPose p = r3d::cardlayout::middle(level++, true);
            placeExtra(c, p);
        }
        // cards just captured stay in the middle until they are swept
        for (int c : captureCards_) placeExtra(c, cards_.target(c));
        const std::vector<int>& deck = g_.deckCardsForDisplay();
        for (size_t i = 0; i < deck.size(); ++i) placeExtra(deck[i], r3d::cardlayout::deck(dealer_, (int)i), false);
    }

private:
    void placeMiddle(int seat, int card, bool bot) {
        const std::vector<int>& open = g_.tableCards();
        const auto it = std::find(open.begin(), open.end(), card);
        const int level = (int)g_.closedCardsForDisplay().size() + (int)(it != open.end() ? it - open.begin() : (long)open.size());
        r3d::CardPose p = r3d::cardlayout::middle(std::max(0, level), true);
        const float delay = bot ? w3d::BOT_GIVE_LEAD : 0.f;
        cards_.place(card, p, true, delay, 0.07f);
        if (bot && seat != 0 && ctx_.characters) ctx_.characters->reach(seat, p.pos, 1);
        soundAt(ui::Sfx::CardPlace, (delay + 0.42f) / speed_);
    }

    pisti::Game g_;
    std::array<std::unique_ptr<pisti::Bot>, 4> bots_;
    std::vector<int> pending_;
    std::vector<int> captureCards_;
    int captureSeat_ = -1;
    float captureAt_ = -1.f;
    int dealer_ = 0;
    std::vector<std::array<int, 4>> history_;
    std::array<int, 4> pistis_{};
};

} // namespace

std::unique_ptr<TableGame> makePistiTable() { return std::make_unique<PistiTable>(); }

} // namespace app
