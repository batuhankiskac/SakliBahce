// King (20 hands, or the short 12-hand game: penalties and trump games) at our table: engine king::Game, bots king::Bot, the shared card table.
#include "app/CardTable.h"
#include "core/King.h"
#include "core/KingBot.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

std::string signedText(int v) { return v > 0 ? "+" + std::to_string(v) : v < 0 ? "-" + std::to_string(-v) : "0"; }

// 1 Rıza: calm, 2 Mahmut: loud, 3 Nuri: grumpy
const char* const kTookRifki[4][2] = {
    {},
    {"Rıfkı bana geldi, kısmet.", "Eyvah, Rıfkı… hayırlısı."},
    {"Rıfkı mı?! Kendi kaleme gol attım abi!", "Olamaz, Rıfkı bende!"},
    {"Hıh, Rıfkı. Bizim zamanımızda da hep bana gelirdi.", "Rıfkıyı yine bana yıktınız."},
};
const char* const kHumanRifki[4][2] = {
    {},
    {"Rıfkı sana gitti {h}, geçmiş olsun.", "Eh {h}, Rıfkı da oyunun parçası."},
    {"Rıfkı sende {h}! Ha ha!", "Yaz yaz, Rıfkıyı {h} aldı!"},
    {"Rıfkıyı aldın {h}. Ders olsun.", "Hıh, Rıfkı. Acemi işi {h}."},
};
const char* const kChose[4][2] = {
    {},
    {"Bismillah, {v} oynayalım.", "Hayırlısı, {v}."},
    {"{v}! Hadi bakalım!", "{v} diyorum, kimse kaçamaz!"},
    {"{v}. Gençler öğrensin.", "Hıh, {v} olsun."},
};

std::string fillLine(std::string t, const std::string& v, const std::string& h) {
    size_t p;
    while ((p = t.find("{v}")) != std::string::npos) t.replace(p, 3, v);
    while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, h);
    return t;
}

class KingTable final : public CardTableBase {
public:
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        king::Rules rules;
        if (st.king12) { // Kısa King: each player chooses 1 koz and 2 cezas, 12 hands
            rules.kozPerPlayer = 1;
            rules.cezaPerPlayer = 2;
        }
        g_ = king::Game(rules);
        pickingTrump_ = false;
        names_ = names;
        for (int s = 0; s < 4; ++s) {
            g_.setPlayer(s, names[(size_t)s], s == 0);
            const king::BotLevel lv = s == 0 ? king::BotLevel::Kurt : (king::BotLevel)std::clamp(level_, 0, 2);
            bots_[(size_t)s] = std::make_unique<king::Bot>(lv, seed * 4 + (uint64_t)s + 0x4196ull);
            // personalities: Kel Mahmut (2) bold, Emekli Nuri (3) cautious; Hacı Rıza and the AI in my seat neutral
            bots_[(size_t)s]->setStyle(king::BotStyle::forSeat(s));
        }
        rng_.reseed(seed ^ 0x4196Bull);
        replay_ = false;
        resetTable();
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != king::Stage::HandOver) return;
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        for (int s = 1; s < 4; ++s)
            if (bots_[(size_t)s]) bots_[(size_t)s]->setLevel((king::BotLevel)level_);
    }
    bool handOver() const override { return g_.stage() == king::Stage::HandOver || g_.stage() == king::Stage::MatchOver; }
    bool matchOver() const override { return g_.stage() == king::Stage::MatchOver; }

    std::string scoreTitle() const override {
        if (g_.stage() == king::Stage::NotStarted) return "";
        return "King \xC2\xB7 " + std::to_string(std::min(g_.handIndex() + 1, g_.numHands())) + " / " + std::to_string(g_.numHands());
    }
    std::vector<std::string> scoreLines() const override {
        std::vector<std::string> v;
        for (int s = 0; s < 4; ++s) v.push_back(names_[(size_t)s] + " ....... " + std::to_string(g_.total(s)));
        return v;
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        const bool playing = g_.stage() == king::Stage::Playing || g_.stage() == king::Stage::HandOver;
        for (int s = 1; s < 4; ++s) {
            heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
            r3d::GameHud::Plate& p = plates[(size_t)s];
            p.show = true;
            p.name = names_[(size_t)s];
            p.label = "Toplam";
            p.value = std::to_string(g_.total(s));
            p.turn = g_.current() == s && !tableBusy() && g_.stage() != king::Stage::HandOver;
            if (g_.chooser() == s) p.badges.push_back({"Seçen", Color{168, 120, 30, 255}});
            if (playing && g_.handPoints(s) != 0)
                p.badges.push_back({signedText(g_.handPoints(s)), g_.handPoints(s) < 0 ? Color{168, 42, 34, 255} : Color{52, 110, 64, 255}});
            if (playing) p.badges.push_back({"El " + std::to_string(g_.seat(s).tricks), Color{70, 60, 50, 255}});
        }
        hud_.plates(r, heads, plates);
        if (g_.stage() == king::Stage::Playing || g_.stage() == king::Stage::HandOver)
            hud_.label3D(r, {0.f, w3d::TABLE_Y + 0.01f, -0.05f}, g_.contractLabel() + " \xC2\xB7 " + names_[(size_t)g_.chooser()],
                         ui::pal::Highlight, 16.f);

        const bool mine = !aiSeat && !replay_ && g_.stage() == king::Stage::Choosing && g_.current() == 0 && !tableBusy();
        if (mine && !pickingTrump_) {
            panelKind_ = 1;
            std::vector<r3d::GameHud::PanelButton> b;
            const Hint* tip = activeHint();
            for (const king::ContractOption& o : g_.options(0)) {
                std::string hint;
                if (!o.allowed) hint = o.reason;
                else if (o.contract == king::Contract::Koz) hint = "kalan " + std::to_string(g_.kozLeft(0));
                else hint = "kalan " + std::to_string(o.remaining);
                b.push_back({king::contractNameTR(o.contract), o.allowed, tip && tip->choice == (int)o.contract, hint});
            }
            hud_.panel(mouse, "Ne oynayalım?",
                       "Senin seçimin  \xC2\xB7  koz hakkın " + std::to_string(g_.kozLeft(0)) + ", ceza hakkın " +
                           std::to_string(g_.cezaLeft(0)),
                       b, 4, 220.f, 150.f);
        } else if (mine && pickingTrump_) {
            panelKind_ = 2;
            std::vector<r3d::GameHud::PanelButton> b;
            const Hint* tip = activeHint();
            for (int s = 0; s < 4; ++s) {
                int n = 0;
                for (int c : g_.hand(0)) n += kart::suitOf(c) == s ? 1 : 0;
                b.push_back({kart::suitNameTR(s), true, tip && tip->choice == (int)king::Contract::Koz && tip->suit == s,
                             "elinde " + std::to_string(n) + " kart"});
            }
            b.push_back({"Geri", true, false, ""});
            hud_.panel(mouse, "Hangi renk koz?", "", b, 5, 240.f, 116.f);
        } else {
            panelKind_ = 0;
            if (g_.stage() != king::Stage::Choosing) pickingTrump_ = false;
        }

        Color sc = ui::pal::TextLight;
        std::string st;
        const int a = actor();
        if (g_.stage() == king::Stage::HandOver) st = "El bitti";
        else if (g_.stage() == king::Stage::MatchOver) st = "Parti bitti";
        else if (now_ < dealUntil_) st = "Kağıtlar dağıtılıyor…";
        else if (a == 0 && replay_) st = replaySelfStatus();
        else if (a == 0 && aiSeat) st = "Yapay zeka düşünüyor…";
        else if (a == 0) {
            sc = ui::pal::Highlight;
            st = g_.stage() == king::Stage::Choosing ? "Bu eli sen seçiyorsun" : "Bir kağıt at";
        } else if (a > 0) {
            st = names_[(size_t)a] + (g_.stage() == king::Stage::Choosing ? " oyunu seçiyor…" : " düşünüyor…");
        }
        std::vector<std::pair<std::string, Color>> parts;
        if (playing) {
            parts.push_back({g_.contractLabel(), Color{238, 198, 112, 255}});
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
            parts.push_back({"Bu el: " + signedText(g_.handPoints(0)), g_.handPoints(0) < 0 ? Color{255, 150, 130, 255} : Color{160, 236, 160, 255}});
        }
        hud_.status(st, sc, a == 0 && !aiSeat && !replay_, parts, aiSeat && !replay_);
        drawButtons(mouse, aiSeat);
        hud_.toasts();
    }

    bool humanHandScore(int& score) const override {
        if (g_.sheet().empty()) return false;
        score = g_.sheet().back().points[0];
        return true;
    }

    bool saveState(std::vector<std::string>& lines) const override {
        lines.clear();
        for (const king::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        bool ok = true;
        for (const std::string& l : lines) {
            king::LoggedAction a;
            if (!king::LoggedAction::decode(l, a) || !g_.replay(a)) {
                ok = false;
                break;
            }
        }
        g_.drainEvents(); // (what happened is already on the table: nothing to animate)
        pickingTrump_ = false;
        panelKind_ = 0;
        log_.clear();
        restoreView();
        return ok;
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const auto& rows = g_.sheet();
        for (int s = 0; s < 4; ++s)
            m.columns.push_back(s == 0 && aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[(size_t)s]);
        m.humanCol = 0;
        m.title = std::to_string((int)rows.size()) + ". El Sonucu";
        m.corner = "king: " + std::to_string(g_.numHands()) + " el";
        if (!rows.empty()) {
            const king::HandRecord& r = rows.back();
            m.headline = r.label + "  \xC2\xB7  " + (r.chooser == 0 ? std::string(aiMode ? "yapay zeka seçti" : "sen seçtin")
                                                                    : names_[(size_t)r.chooser] + " seçti");
            m.tagline = r.endedEarly ? "cezalar bitti, el erken kapandı" : std::to_string(r.tricksPlayed) + " el oynandı";
            m.rowLabels = {king::isCeza(r.contract) ? "Aldığı" : "El", "Bu el"};
            m.cells.assign(2, std::vector<std::string>(4));
            m.red.assign(2, std::vector<bool>(4, false));
            int best = -100000;
            for (int s = 0; s < 4; ++s) best = std::max(best, r.points[(size_t)s]);
            for (int s = 0; s < 4; ++s) {
                m.cells[0][(size_t)s] = std::to_string(r.units[(size_t)s]);
                m.cells[1][(size_t)s] = signedText(r.points[(size_t)s]);
                m.red[1][(size_t)s] = r.points[(size_t)s] < 0;
                if (r.points[(size_t)s] == best && best > 0) m.starCol = s;
            }
        }
        for (const king::HandRecord& r : rows) {
            m.historyLabels.push_back(std::to_string(r.index + 1) + ". " + r.label);
            std::vector<std::string> row;
            for (int s = 0; s < 4; ++s) row.push_back(r.points[(size_t)s] == 0 ? std::string("") : signedText(r.points[(size_t)s]));
            m.history.push_back(row);
        }
        int best = -1000000;
        for (int s = 0; s < 4; ++s) best = std::max(best, g_.total(s));
        for (int s = 0; s < 4; ++s) {
            m.totals.push_back(signedText(g_.total(s)));
            if (g_.total(s) == best) m.leaders.push_back(s);
        }
        m.last = g_.stage() == king::Stage::MatchOver;
        m.note = m.last ? "" : "Yüksek puan iyidir  \xC2\xB7  " + std::to_string(g_.numHands() - (int)rows.size()) + " el kaldı";
        m.matchHeader = "Parti Bitti";
        m.playedLine = std::to_string(rows.size()) + " el oynandı";
        const std::array<int, 4> rk = g_.ranking();
        for (int i = 0; i < 4; ++i) {
            m.ranking.push_back(rk[(size_t)i]);
            const int r = (i > 0 && g_.total(rk[(size_t)i]) == g_.total(rk[(size_t)i - 1])) ? m.rank.back() : i + 1;
            m.rank.push_back(r);
        }
        for (int s = 0; s < 4; ++s) {
            int koz = 0, ceza = 0;
            for (const king::HandRecord& r : rows) {
                if (r.contract == king::Contract::Koz) koz += r.points[(size_t)s];
                else ceza += r.points[(size_t)s];
            }
            m.rankSub.push_back("Kozdan " + signedText(koz) + "  \xC2\xB7  cezadan " + signedText(ceza));
        }
        const int lead = rk[0];
        m.humanWon = lead == 0 && g_.total(rk[1]) != g_.total(0);
        if (m.humanWon) {
            m.resultTitle = aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!";
            m.resultSub = "Toplam " + signedText(g_.total(0)) + " ile partinin kralı.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir parti daha?";
            m.resultSub = "Kazanan: " + names_[(size_t)lead] + " (" + signedText(g_.total(lead)) + ")";
        }
        return m;
    }

protected:
    int replayLine(const std::string& line) override {
        king::LoggedAction a;
        if (!king::LoggedAction::decode(line, a)) return -1;
        if (a.kind == king::LogKind::NextHand) {
            if (g_.stage() == king::Stage::HandOver) return 0;     // the sheet is up: App deals the next hand
            return g_.stage() == king::Stage::MatchOver ? -1 : 1; // (already dealt)
        }
        if (!g_.replay(a)) return -1;
        pickingTrump_ = false;
        pumpEngine();
        return 1;
    }
    void pumpEngine() override {
        for (const king::GameEvent& e : g_.drainEvents()) {
            using E = king::EvType;
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::HandStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::Deal: dealFrom((g_.chooser() + 3) % 4); break;
            case E::ContractChosen:
                hud_.toast(e.text, ui::pal::Highlight, 3.2f);
                if (e.seat >= 1 && rng_.chance(0.5f))
                    say(e.seat, fillLine(kChose[e.seat][rng_.range(2)], king::contractLabelTR(e.contract, e.trump), names_[0]));
                break;
            case E::Play: onPlayed(e.seat, e.card, e.seat != 0 || aiSeat_); break;
            case E::TrickWon:
                onTrickWon(e.seat, e.cards);
                if (e.amount != 0 && (e.seat == 0 || e.amount <= -100)) hud_.toast(e.text, e.amount < 0 ? ui::pal::Bad : ui::pal::TextLight, 2.0f);
                break;
            case E::PenaltyCard:
                hud_.toast(e.text, ui::pal::Bad, e.card == king::RIFKI ? 3.6f : 2.4f);
                if (e.card == king::RIFKI) {
                    sound(ui::Sfx::Penalty);
                    if (e.seat >= 1) {
                        say(e.seat, kTookRifki[e.seat][rng_.range(2)], true);
                        if (ctx_.characters) ctx_.characters->react(e.seat, 2, {0, w3d::TABLE_Y, 0});
                    } else {
                        const int s = 1 + rng_.range(3);
                        say(s, fillLine(kHumanRifki[s][rng_.range(2)], "", names_[0]), true);
                    }
                }
                break;
            case E::HandEndEarly: hud_.toast(e.text, ui::pal::Highlight, 3.0f); break;
            case E::HandEnd: {
                log_ = e.text;
                for (const std::string& l : e.lines) log_ += " | " + l;
                sound(ui::Sfx::Win);
                // Başarımlar: a ceza deal without a single penalty for the player
                if (!g_.sheet().empty() && king::isCeza(g_.sheet().back().contract) && g_.sheet().back().units[0] == 0)
                    noteAchievement("king_cezasiz");
                break;
            }
            case E::MatchEnd: {
                hud_.toast(e.text, ui::pal::Highlight, 5.f);
                // Başarımlar: a whole match (with at least one Rıfkı deal) without taking the Rıfkı
                int rifki = 0;
                bool took = false;
                for (const king::HandRecord& h : g_.sheet())
                    if (h.contract == king::Contract::Rifki) {
                        ++rifki;
                        took = took || h.units[0] > 0;
                    }
                if (rifki > 0 && !took) noteAchievement("rifki_yok");
                break;
            }
            default: break;
            }
        }
    }
    std::vector<int> handOf(int seat) const override { return g_.hand(seat); }
    int actor() const override {
        if (g_.stage() == king::Stage::Choosing || g_.stage() == king::Stage::Playing) return g_.current();
        return -1;
    }
    bool humanTurn() const override { return g_.stage() == king::Stage::Playing && g_.current() == 0; }
    std::vector<int> playableCards() const override { return humanTurn() ? g_.legalCards(0) : std::vector<int>{}; }
    void playHumanCard(int card) override {
        const king::ActionResult r = g_.playCard(0, card);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.6f);
            sound(ui::Sfx::Error);
        }
    }
    float botDelay(int seat) const override {
        (void)seat;
        if (g_.stage() == king::Stage::Choosing) return 1.4f;
        return shown_.empty() ? 0.9f : 0.65f;
    }
    void botStep(int seat) override {
        king::BotAction a = bots_[(size_t)seat]->next(g_, seat);
        if (!king::applyBotAction(g_, seat, a).ok) king::applyBotAction(g_, seat, king::fallbackAction(g_, seat));
    }
    size_t decisionStamp() const override { return g_.actionLog().size() + 100000u * (size_t)g_.handIndex(); }
    bool computeHint(Hint& h) override {
        if (actor() != 0 || !bots_[0]) return false;
        const king::BotAction a = bots_[0]->next(g_, 0);
        if (a.kind == king::BotAction::Kind::Choose) {
            h.choice = (int)a.contract;
            h.suit = a.trump;
            if (a.contract == king::Contract::Koz) h.text = std::string("İpucu: koz ") + kart::suitNameTR(a.trump) + " seç";
            else h.text = std::string("İpucu: ") + king::contractNameTR(a.contract) + " seç";
        } else {
            h.card = a.card;
            h.text = "İpucu: " + kart::cardAccusativeTR(a.card) + " oyna";
        }
        return true;
    }
    std::vector<std::pair<int, int>> trickOnTable() const override {
        std::vector<std::pair<int, int>> v;
        for (const king::TrickCard& tc : g_.currentTrick().cards) v.push_back({tc.seat, tc.card});
        return v;
    }
    std::array<std::vector<int>, 4> wonPiles() const override {
        std::array<std::vector<int>, 4> w;
        for (const king::Trick& t : g_.tricks())
            if (t.winner >= 0 && t.winner < 4)
                for (const king::TrickCard& tc : t.cards) w[(size_t)t.winner].push_back(tc.card);
        return w;
    }
    void onHudClick(int id) override {
        const int i = id - r3d::GameHud::PANEL_ID;
        if (i < 0) return;
        sound(ui::Sfx::Button);
        if (panelKind_ == 1) {
            const std::vector<king::ContractOption> opts = g_.options(0);
            if (i >= (int)opts.size() || !opts[(size_t)i].allowed) return;
            if (opts[(size_t)i].contract == king::Contract::Koz) {
                pickingTrump_ = true;
                return;
            }
            report(g_.chooseContract(0, opts[(size_t)i].contract));
        } else if (panelKind_ == 2) {
            if (i == 4) {
                pickingTrump_ = false;
                return;
            }
            pickingTrump_ = false;
            report(g_.chooseContract(0, king::Contract::Koz, i));
        }
        pumpEngine();
    }

private:
    void report(const king::ActionResult& r) {
        if (r.ok) return;
        hud_.toast(r.error, ui::pal::Bad, 2.6f);
        sound(ui::Sfx::Error);
    }
    king::Game g_;
    std::array<std::unique_ptr<king::Bot>, 4> bots_;
    int panelKind_ = 0;
    bool pickingTrump_ = false;
};

} // namespace

std::unique_ptr<TableGame> makeKingTable() { return std::make_unique<KingTable>(); }

} // namespace app
