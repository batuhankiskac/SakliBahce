// İhaleli Batak (tekli / eşli) at our table: engine batak::Game, bots batak::Bot, the shared card table.
#include "app/CardTable.h"
#include "core/Batak.h"
#include "core/BatakBot.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

const char* suitName(int s) { return kart::suitNameTR(s); }

// A few lines per regular (1 Rıza: calm, 2 Mahmut: loud, 3 Nuri: grumpy); {v} = a number.
const char* const kBidLines[4][3] = {
    {},
    {"Bismillah, {v} diyelim.", "Sabırla {v}.", "{v}, hayırlısı."},
    {"{v}! Bu el benim abi!", "{v} dedim, geri adım yok!", "Hadi {v}, tribünler ayağa!"},
    {"{v}. Bizim zamanımızda daha yüksek denirdi.", "Hıh, {v}.", "{v} diyorum, gerisi sizde."},
};
const char* const kMadeLines[4][2] = {
    {},
    {"Elhamdülillah, sözümüzü tuttuk.", "Sabır acıdır, meyvesi tatlı."},
    {"Yaptım abi, gol!", "Söz verdik, yaptık!"},
    {"Gördünüz mü? Tecrübe.", "Hıh, yaptım işte."},
};
const char* const kFailLines[4][2] = {
    {},
    {"Battık, hayırlısı.", "Kısmet değilmiş."},
    {"Hakem yüzünden battım abi!", "Olmadı, rövanş!"},
    {"Bizim zamanımızda bu kağıtla batılmazdı.", "Hıh, battım. Gözlüğümü sileyim."},
};
const char* const kHumanFailLines[4][2] = {
    {},
    {"Eh {h}, olur böyle şeyler.", "Batmak da oyunun parçası {h}."},
    {"Battın {h}! Ha ha!", "{h} battı beyler, yazın!"},
    {"Fazla yüksek dedin {h}.", "Hıh, batak. Ders olsun {h}."},
};

std::string fillLine(std::string t, int v, const std::string& h) {
    size_t p;
    while ((p = t.find("{v}")) != std::string::npos) t.replace(p, 3, std::to_string(v));
    while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, h);
    return t;
}

std::string signedText(int v) { return v > 0 ? "+" + std::to_string(v) : v < 0 ? "-" + std::to_string(-v) : "0"; }

class BatakTable final : public CardTableBase {
public:
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        batak::Rules r = st.batakEsli ? batak::Rules::esliBatak() : batak::Rules::tekli();
        r.targetScore = std::clamp(st.batakTarget, 11, 151);
        g_ = batak::Game(r);
        names_ = names;
        for (int s = 0; s < 4; ++s) {
            g_.setPlayer(s, names[(size_t)s], s == 0);
            const batak::Level lv = s == 0 ? batak::Level::Kurt : (batak::Level)std::clamp(level_, 0, 2);
            bots_[(size_t)s] = std::make_unique<batak::Bot>(lv, seed * 4 + (uint64_t)s + 0xBA7Aull);
        }
        rng_.reseed(seed ^ 0xB47A4ull);
        resetTable();
        bidText_.fill("");
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != batak::Stage::HandOver) return;
        bidText_.fill("");
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        for (int s = 1; s < 4; ++s)
            if (bots_[(size_t)s]) bots_[(size_t)s]->setLevel((batak::Level)level_);
    }

    bool handOver() const override { return g_.stage() == batak::Stage::HandOver || g_.stage() == batak::Stage::MatchOver; }
    bool matchOver() const override { return g_.stage() == batak::Stage::MatchOver; }

    std::string scoreTitle() const override {
        if (g_.stage() == batak::Stage::NotStarted) return "";
        return std::string(g_.rules().esli ? "Eşli Batak" : "Batak") + " \xC2\xB7 " + std::to_string(g_.rules().targetScore) + "'e";
    }
    std::vector<std::string> scoreLines() const override {
        std::vector<std::string> v;
        for (int side = 0; side < g_.numSides(); ++side)
            v.push_back(g_.sideName(side) + " ....... " + std::to_string(g_.sideTotal(side)));
        return v;
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        for (int s = 1; s < 4; ++s) {
            heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
            r3d::GameHud::Plate& p = plates[(size_t)s];
            p.show = true;
            p.name = names_[(size_t)s];
            p.label = "Puan";
            p.value = std::to_string(g_.total(s));
            p.turn = g_.current() == s && !tableBusy();
            if (!bidText_[(size_t)s].empty()) p.badges.push_back({bidText_[(size_t)s], Color{96, 74, 40, 255}});
            if (g_.declarer() == s) p.badges.push_back({"İhale " + std::to_string(g_.contract()), Color{168, 120, 30, 255}});
            if (g_.stage() == batak::Stage::Playing || g_.stage() == batak::Stage::HandOver)
                p.badges.push_back({"Aldı " + std::to_string(g_.tricksWon(s)), Color{52, 110, 64, 255}});
        }
        hud_.plates(r, heads, plates);
        if (g_.trump() >= 0) {
            hud_.label3D(r, {0.f, w3d::TABLE_Y + 0.01f, -0.05f}, std::string("Koz: ") + suitName(g_.trump()), ui::pal::Highlight, 16.f);
        }
        // the player's decisions
        const bool mine = !aiSeat && actor() == 0 && !tableBusy();
        if (mine && g_.stage() == batak::Stage::Bidding) {
            panel_.clear();
            panelKind_ = 1;
            panelValues_.clear();
            std::vector<r3d::GameHud::PanelButton> b;
            b.push_back({"Pas", g_.canPass(0), false, ""});
            panelValues_.push_back(0);
            for (int v : g_.legalBids(0)) {
                b.push_back({std::to_string(v), true, false, ""});
                panelValues_.push_back(v);
            }
            const std::string sub = g_.highBid() > 0 ? "Şu an: " + std::to_string(g_.highBid()) + " (" + names_[(size_t)g_.highBidder()] + ")"
                                                     : "Elinde kaç el alırsın?";
            hud_.panel(mouse, "İhale", sub, b, 5, 250.f, 92.f);
        } else if (mine && g_.stage() == batak::Stage::ChoosingTrump) {
            panelKind_ = 2;
            std::vector<r3d::GameHud::PanelButton> b;
            const std::vector<int>& h = g_.hand(0);
            for (int s = 0; s < 4; ++s) {
                int n = 0;
                for (int c : h) n += kart::suitOf(c) == s ? 1 : 0;
                b.push_back({suitName(s), true, false, "elinde " + std::to_string(n) + " kart"});
            }
            hud_.panel(mouse, "Kozu seç", "İhaleyi " + std::to_string(g_.contract()) + " ile aldın", b, 4, 250.f, 130.f);
        } else {
            panelKind_ = 0;
        }
        // status
        Color sc = ui::pal::TextLight;
        std::string st;
        const int a = actor();
        if (g_.stage() == batak::Stage::HandOver) st = "El bitti";
        else if (g_.stage() == batak::Stage::MatchOver) st = "Oyun bitti";
        else if (now_ < dealUntil_) st = "Kağıtlar dağıtılıyor…";
        else if (a == 0 && aiSeat) st = "Yapay zeka düşünüyor…";
        else if (a == 0) {
            sc = ui::pal::Highlight;
            if (g_.stage() == batak::Stage::Bidding) st = "İhale sırası sende";
            else if (g_.stage() == batak::Stage::ChoosingTrump) st = "Kozu seç";
            else st = g_.current() != 0 ? "Açık elden bir kağıt at" : "Bir kağıt at";
        } else if (a > 0) {
            st = names_[(size_t)a] + " düşünüyor…";
        }
        std::vector<std::pair<std::string, Color>> parts;
        if (g_.stage() == batak::Stage::Playing || g_.stage() == batak::Stage::HandOver) {
            const int side = g_.sideOf(0);
            parts.push_back({"Aldığın: " + std::to_string(g_.sideTricks(side)), Color{226, 216, 196, 255}});
            if (g_.declarer() >= 0) {
                parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
                parts.push_back({"İhale: " + std::to_string(g_.contract()) + " (" + names_[(size_t)g_.declarer()] + ")",
                                 Color{238, 198, 112, 255}});
            }
        }
        hud_.status(st, sc, a == 0 && !aiSeat, parts, aiSeat);
        hud_.buttons(mouse, {}, {}, {}, aiSeat);
        hud_.toasts();
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const batak::HandResult& r = g_.lastHandResult();
        const int sides = g_.numSides();
        auto colName = [&](int side) {
            if (!g_.rules().esli) return side == 0 && aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[(size_t)side];
            return names_[(size_t)side] + " + " + names_[(size_t)side + 2];
        };
        for (int s = 0; s < sides; ++s) m.columns.push_back(colName(s));
        m.humanCol = 0;
        m.title = "El " + std::to_string((int)g_.handResults().size()) + " Sonucu";
        m.corner = std::string(g_.rules().esli ? "eşli batak" : "batak") + ": " + std::to_string(g_.rules().targetScore) + "'e";
        const int decl = r.declarer;
        if (decl >= 0) {
            bool made = true;
            for (const batak::SideResult& sr : r.sides)
                if (sr.declarer) made = sr.made;
            const std::string who = decl == 0 ? std::string(aiMode ? "Yapay zeka" : "Sen") : names_[(size_t)decl];
            m.headline = decl == 0 && !aiMode ? (std::string("İhaleyi ") + std::to_string(r.contract) + " ile aldın, " + (made ? "yaptın!" : "battın!"))
                                              : who + " ihaleyi " + std::to_string(r.contract) + " ile aldı, " + (made ? "yaptı." : "battı!");
            if (r.king) m.headline = who + " king yaptı! 13 el!";
            m.tagline = std::string("koz: ") + (r.trump >= 0 ? suitName(r.trump) : "-") + (r.forced ? "  \xC2\xB7  herkes pas dedi, zorunlu ihale" : "");
            m.starCol = made ? g_.sideOf(decl) : -1;
        }
        m.rowLabels = {"İhale", "Aldığı", "Bu el"};
        m.cells.assign(3, std::vector<std::string>((size_t)sides));
        m.red.assign(3, std::vector<bool>((size_t)sides, false));
        for (const batak::SideResult& sr : r.sides) {
            const size_t c = (size_t)sr.side;
            if (c >= (size_t)sides) continue;
            m.cells[0][c] = sr.declarer ? std::to_string(sr.bid) : "";
            m.cells[1][c] = std::to_string(sr.tricks);
            m.cells[2][c] = signedText(sr.points);
            m.red[2][c] = sr.points < 0;
            if (sr.declarer && !sr.made) m.red[1][c] = true;
        }
        const auto& all = g_.handResults();
        for (size_t h = 0; h < all.size(); ++h) {
            std::string lbl = std::to_string(h + 1) + ". el";
            if (all[h].declarer >= 0) lbl += " (" + std::to_string(all[h].contract) + ")";
            m.historyLabels.push_back(lbl);
            std::vector<std::string> row((size_t)sides);
            for (const batak::SideResult& sr : all[h].sides)
                if (sr.side >= 0 && sr.side < sides) row[(size_t)sr.side] = signedText(sr.points);
            m.history.push_back(row);
        }
        int best = -100000;
        for (int s = 0; s < sides; ++s) best = std::max(best, g_.sideTotal(s));
        for (int s = 0; s < sides; ++s) {
            m.totals.push_back(std::to_string(g_.sideTotal(s)));
            if (g_.sideTotal(s) == best) m.leaders.push_back(s);
        }
        m.last = g_.stage() == batak::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.rules().targetScore) + "'e ilk ulaşan kazanır";
        // final standings
        m.matchHeader = "Oyun Bitti";
        m.playedLine = std::to_string(all.size()) + " el oynandı";
        std::vector<int> order;
        for (int s = 0; s < sides; ++s) order.push_back(s);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g_.sideTotal(a) > g_.sideTotal(b); });
        m.ranking = order;
        int rank = 1;
        for (size_t i = 0; i < order.size(); ++i) {
            if (i > 0 && g_.sideTotal(order[i]) != g_.sideTotal(order[i - 1])) rank = (int)i + 1;
            m.rank.push_back(rank);
        }
        for (int s = 0; s < sides; ++s) {
            std::string hs;
            for (size_t h = 0; h < all.size(); ++h)
                for (const batak::SideResult& sr : all[h].sides)
                    if (sr.side == s) hs += (hs.empty() ? "" : ", ") + signedText(sr.points);
            m.rankSub.push_back("El puanları: " + hs);
        }
        const int winner = g_.winnerSide();
        m.humanWon = winner == 0;
        if (winner < 0) {
            m.resultTitle = "Berabere!";
            m.resultSub = "Puanlar eşit bitti.";
        } else if (winner == 0) {
            m.resultTitle = aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : (g_.rules().esli ? "Kazandınız! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!");
            m.resultSub = "Toplam " + std::to_string(g_.sideTotal(0)) + " puanla masanın kurdu.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir oyun daha?";
            m.resultSub = "Kazanan: " + g_.sideName(winner) + " (" + std::to_string(g_.sideTotal(winner)) + ")";
        }
        return m;
    }

protected:
    void pumpEngine() override {
        for (const batak::GameEvent& e : g_.drainEvents()) {
            using E = batak::EvType;
            const bool human = e.seat == 0;
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::HandStart:
                hud_.toast(e.text, ui::pal::Highlight, 3.0f);
                bidText_.fill("");
                break;
            case E::Deal: dealFrom(e.seat); break;
            case E::Redeal: hud_.toast(e.text, ui::pal::TextLight, 3.0f); break;
            case E::Bid:
                bidText_[(size_t)e.seat] = e.value > 0 ? std::to_string(e.value) : std::string("Pas");
                hud_.toast(e.text, ui::pal::TextLight, 2.0f);
                if (e.seat >= 1 && e.value > 0 && rng_.chance(0.45f))
                    say(e.seat, fillLine(kBidLines[e.seat][rng_.range(3)], e.value, names_[0]));
                break;
            case E::BiddingWon:
                hud_.toast(e.text, ui::pal::Highlight, 3.0f);
                break;
            case E::TrumpChosen:
                hud_.toast(e.text, ui::pal::Highlight, 3.0f);
                sound(ui::Sfx::CardSlap);
                break;
            case E::DummyOpen: hud_.toast(e.text.empty() ? "Açık el masada" : e.text, ui::pal::TextLight, 2.6f); break;
            case E::Play: {
                const int ctl = g_.controllerOf(e.seat);
                onPlayed(e.seat, e.card, ctl != 0 || aiSeat_);
                break;
            }
            case E::TrumpBroken: hud_.toast(e.text.empty() ? "Koz açıldı!" : e.text, ui::pal::Highlight, 2.2f); break;
            case E::TrickWon: {
                std::vector<int> c;
                for (const batak::PlayedCard& pc : e.cards) c.push_back(pc.card);
                onTrickWon(e.seat, c);
                if (human || (g_.rules().esli && e.seat == 2)) hud_.toast(e.text, ui::pal::TextLight, 1.6f);
                break;
            }
            case E::HandEnd: {
                for (const std::string& l : e.lines) hud_.toast(l, ui::pal::Highlight, 4.0f);
                const batak::HandResult& r = g_.lastHandResult();
                for (const batak::SideResult& sr : r.sides) {
                    if (!sr.declarer) continue;
                    const int d = r.declarer;
                    if (d >= 1) say(d, sr.made ? kMadeLines[d][rng_.range(2)] : kFailLines[d][rng_.range(2)], true);
                    else if (!sr.made) {
                        const int s = 1 + rng_.range(3);
                        say(s, fillLine(kHumanFailLines[s][rng_.range(2)], 0, names_[0]), true);
                    }
                    if (ctx_.characters && d >= 1) ctx_.characters->react(d, sr.made ? 1 : 2, {0, w3d::TABLE_Y, 0});
                }
                log_.clear();
                for (const std::string& l : e.lines) log_ += (log_.empty() ? "" : " | ") + l;
                sound(ui::Sfx::Win);
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            default: break;
            }
        }
    }
    std::vector<int> handOf(int seat) const override { return g_.hand(seat); }
    int actor() const override {
        switch (g_.stage()) {
        case batak::Stage::Bidding:
        case batak::Stage::ChoosingTrump: return g_.current();
        case batak::Stage::Playing: return g_.current() >= 0 ? g_.controllerOf(g_.current()) : -1;
        default: return -1;
        }
    }
    bool humanTurn() const override { return actor() == 0 && g_.stage() == batak::Stage::Playing; }
    std::vector<int> playableCards() const override {
        if (!humanTurn()) return {};
        return g_.legalCards(g_.current());
    }
    std::vector<int> clickable() const override {
        if (humanTurn() && g_.current() != 0) return g_.hand(g_.current()); // the open dummy, played by the player
        return g_.hand(0);
    }
    bool faceUpHand(int seat) const override { return seat == 0 || seat == g_.exposedSeat(); }
    void playHumanCard(int card) override {
        const batak::ActionResult r = g_.playCard(g_.current(), card);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.6f);
            sound(ui::Sfx::Error);
        }
    }
    float botDelay(int seat) const override {
        (void)seat;
        switch (g_.stage()) {
        case batak::Stage::Bidding: return 0.85f;
        case batak::Stage::ChoosingTrump: return 1.3f;
        default: return shown_.empty() ? 0.9f : 0.65f;
        }
    }
    void botStep(int seat) override {
        const int cur = g_.current();
        if (cur < 0) return;
        batak::Bot& b = *bots_[(size_t)seat];
        batak::Action a = b.next(g_, cur);
        batak::ActionResult r = batak::applyAction(g_, cur, a);
        if (!r.ok) batak::applyAction(g_, cur, batak::fallbackAction(g_, cur));
    }
    void onHudClick(int id) override {
        const int i = id - r3d::GameHud::PANEL_ID;
        if (i < 0) return;
        batak::ActionResult r = batak::ActionResult::success();
        if (panelKind_ == 1 && i < (int)panelValues_.size()) {
            r = panelValues_[(size_t)i] == 0 ? g_.pass(0) : g_.bid(0, panelValues_[(size_t)i]);
        } else if (panelKind_ == 2 && i < 4) {
            r = g_.chooseTrump(0, i);
        } else {
            return;
        }
        sound(ui::Sfx::Button);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.6f);
            sound(ui::Sfx::Error);
        }
        pumpEngine();
    }

private:
    batak::Game g_;
    std::array<std::unique_ptr<batak::Bot>, 4> bots_;
    std::array<std::string, 4> bidText_{};
    int panelKind_ = 0;                // 1 ihale, 2 koz
    std::vector<int> panelValues_;
    std::vector<r3d::GameHud::PanelButton> panel_;
};

} // namespace

std::unique_ptr<TableGame> makeBatakTable() { return std::make_unique<BatakTable>(); }

} // namespace app
