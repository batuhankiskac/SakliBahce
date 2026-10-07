// Bezik at the two-seat tavla table: engine bezik::Game, bots bezik::Bot, the shared card table (two decks: the second
// copy of a card is r3d card id + 52). The player (engine player 0) sits at seat 0, the opponent (engine player
// 1; Rakip, Kel Mahmut by default) across as seat 2 (CardTableFrame maps the layout onto the tavla table). Our own pieces: the stock with the koz card
// turned up crosswise under it, the cards drawn after a trick (they stay on the stock until the trick is swept), and
// every player's declared combinations lying face up in a row in front of them until they are played.
#include "app/CardTable.h"
#include "core/Bezik.h"
#include "core/BezikBot.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

constexpr int OPP_SEAT = 2; // the opponent, across (layout seat; who sits there: CardTableBase::oppSeat_)
int seatOf(int player) { return player == 0 ? 0 : OPP_SEAT; }
int playerOf(int seat) { return seat == 0 ? 0 : seat == OPP_SEAT ? 1 : -1; }

// What the opponent says, by who sits across (Rakip: seat 1 Hacı Rıza calm, proverbs; 2 Kel Mahmut loud, a football
// fan; 3 Emekli Nuri careful, "bizim zamanımızda").
const char* const kOppBezik[4][3] = {
    {},
    {"Bezik, kırk. Hayırlısı.", "Maça Kız ile Karo Vale; bezik evlat.", "Bezik geldi, elhamdülillah."},
    {"Bezik! Kırk yaz bakalım!", "Kız ile vale, bezik abi!", "Bezik geldi, kırk!"},
    {"Bezik. Kırk yazın.", "Maça Kız, Karo Vale. Bezik, delikanlı.", "Bezik. Bizim zamanımızda bunu herkes bilirdi."},
};
const char* const kOppCift[4][3] = {
    {},
    {"Çift bezik… Beş yüz. Allah'ın lütfu.", "Ömrümde birkaç kere gördüm: çift bezik!", "Beş yüz evlat, beş yüz. Şükür."},
    {"ÇİFT BEZİK! Beş yüz! Gol gol gol!", "Beş yüz abi, beş yüz! Yaz yaz!", "Çift bezik! Bu maç bitti!"},
    {"Çift bezik! Kırk yıldır bekliyordum!", "Beş yüz. Yazın, yazın; elim titriyor.", "Hıh! Çift bezik. Tecrübe, delikanlı."},
};
const char* const kOppSeri[4][2] = {
    {}, {"Koz serisi, iki yüz elli. Bereket.", "Seri bende evlat, iki elli."},
    {"Koz serisi! İki yüz elli!", "Seri bende abi, iki elli yaz!"},
    {"Koz serisi. İki yüz elli, hesaplıydı.", "Seri tamam. İki elli yazın."},
};
const char* const kOppDort[4][3] = {
    {},
    {"Dörtlü, hayırlısı.", "Dördü de bizde, sayıver.", "Dört tane; bereketli el."},
    {"Dörtlü! Say bakalım!", "Dört tane, hepsi bende!", "Dörtlüyü yazın!"},
    {"Dörtlü. Saklamasını bilene.", "Dördü bende. Yazın.", "Dörtlü; sabırla topladım."},
};
const char* const kOppEvli[4][3] = {
    {},
    {"Evlilik, yirmi. Allah mesut etsin.", "Papazla kız, yirmi.", "Bir düğün daha, hayırlısı."},
    {"Evlilik, yirmi!", "Bir düğün daha, yirmi!", "Papazla kız evlendi!"},
    {"Evlilik. Yirmi.", "Papaz ile kız; yirmi yazın.", "Ufak ama sayılır: yirmi."},
};
const char* const kOppKozEvli[4][2] = {
    {}, {"Koz evliliği, kırk. Hayırlı olsun.", "Kozda düğün var evlat, kırk."},
    {"Koz evliliği! Kırk!", "Kozda düğün var, kırk!"},
    {"Koz evliliği. Kırk.", "Kozda kırk. Bizim zamanımızda böyle oynanırdı."},
};
const char* const kOppYedi[4][2] = {
    {}, {"Koz yedilisi, on. Bereket.", "Yediyle kozu alayım, izninle."},
    {"Koz yedilisi, on yaz!", "Yediyle kozu aldım, sağ ol!"},
    {"Koz yedisi. On yazın.", "Yediyle kozu alırım. Usul böyle."},
};
const char* const kOppWon[4][3] = {
    {},
    {"Bu el bizden, elhamdülillah.", "Saydık, yetti evlat.", "Hayırlısı; bu el bizim."},
    {"Bu el benim abi!", "Sayın sayın, yetti!", "Bu da bizden, yaz!"},
    {"Bu el benim. Saymasını bilen kazanır.", "Hesap ortada, delikanlı.", "Tecrübe. Bu el bizden."},
};
const char* const kOppLost[4][3] = {
    {},
    {"Nasip değilmiş, olsun.", "Bu el senin olsun evlat.", "Kaybetmesini de bileceksin."},
    {"Hay aksi… Kâğıt gelmedi.", "Hakem bu eli de yedi!", "Olsun, rövanşı alırız."},
    {"Hıh. Kâğıt gelmedi.", "Bu kâğıtlarla kim olsa kaybederdi.", "Gözlüğüm buğulandı herhalde."},
};
const char* const kOppYouBig[4][3] = {
    {},
    {"Maşallah {h}, hayırlı olsun.", "Bereketli el {h}, aferin.", "Sabreden derviş, {h}…"},
    {"Vay {h}, maşallah!", "Bu ne şans {h}!", "Eyvah, {h} açıldı…"},
    {"Hıh. Fena değil {h}.", "Acemi şansı {h}.", "Bak sen {h}, el açılmış."},
};
const char* const kOppYouCift[4][2] = {
    {}, {"Çift bezik ha {h}? Allah'ın lütfu, hayırlı olsun.", "Beş yüz… Maşallah {h}."},
    {"Çift bezik mi?! {h}, sen bu işi biliyorsun!", "Beş yüz… Vay be {h}."},
    {"Çift bezik! Kırk yıldır bekliyorum, {h} buldu.", "Beş yüz ha {h}? Bizim zamanımızda böyle şans yoktu."},
};
const char* const kOppTea[4] = {"", "Çaylar Rıza'dan!", "Çaylar Mahmut'tan!", "Çaylar Nuri'den!"};

template <size_t N>
std::string pick(const char* const (&lines)[N], okey::Rng& rng, const std::string& h = {}) {
    std::string t = lines[(size_t)rng.range((int)N)];
    size_t p;
    while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, h);
    return t;
}

// The stock (layout space): on the player's left of the middle; the koz card across under it, sticking out.
constexpr float STOCK_X = -0.36f;
r3d::CardPose stockSlot(int i) {
    r3d::CardPose p;
    p.pos = {STOCK_X, w3d::TABLE_Y + r3d::CARD_T * (1.5f + (float)i), 0.f};
    p.rot = r3d::cardFlat(4.f + (float)((i * 37) % 5 - 2) * 0.6f, false);
    return p;
}
r3d::CardPose kozPose() {
    r3d::CardPose p;
    p.pos = {STOCK_X + 0.034f, w3d::TABLE_Y + r3d::CARD_T * 0.5f, 0.f};
    p.rot = r3d::cardFlat(90.f, true);
    return p;
}
// The declared cards in front of a seat: one overlapping row, readable from the player.
r3d::CardPose meldPose(int seat, int i, int n) {
    const float step = std::min(0.030f, 0.30f / (float)std::max(1, n));
    const float x0 = -0.03f - step * (float)(n - 1) * 0.5f;
    r3d::CardPose p;
    const float z = seat == 0 ? 0.30f : -0.30f;
    p.pos = {x0 + step * (float)i, w3d::TABLE_Y + r3d::CARD_T * (0.5f + (float)i), z};
    p.rot = r3d::cardFlat(0.f, true);
    return p;
}

// Order of the declared row: by suit, then bezik's rank order (A 10 P K V 9 8 7).
bool rowLess(int a, int b) {
    if (bezik::suitOf(a) != bezik::suitOf(b)) return bezik::suitOf(a) < bezik::suitOf(b);
    if (bezik::strength(a) != bezik::strength(b)) return bezik::strength(a) > bezik::strength(b);
    return a < b;
}

class BezikTable final : public CardTableBase {
public:
    std::vector<int> seats() const override { return {0, oppSeat_}; }
    int location() const override { return 1; }

    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        bezik::Rules rules;
        rules.target = st.bezikTarget == 500 || st.bezikTarget == 1500 ? st.bezikTarget : 1000;
        g_ = bezik::Game(rules);
        names_ = names;
        seatOpponent(ui::twoPlayerOpponent(st, ui::GameKind::Bezik)); // Rakip
        g_.setPlayer(0, names_[0], true);
        g_.setPlayer(1, names_[OPP_SEAT], false);
        for (int p = 0; p < 2; ++p) {
            const bezik::BotLevel lv = p == 0 ? bezik::BotLevel::Kurt : (bezik::BotLevel)std::clamp(level_, 0, 2);
            bots_[(size_t)p] = std::make_unique<bezik::Bot>(lv, seed * 2 + (uint64_t)p + 0xBE2ull);
            bots_[(size_t)p]->setStyle(bezik::BotStyle::forSeat(chr(seatOf(p)))); // the opponent's own style, the AI neutral
        }
        rng_.reseed(seed ^ 0xBE21Bull);
        replay_ = false;
        lastEnd_.clear();
        panelKind_ = 0;
        resetTable();
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != bezik::Stage::HandOver) return;
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        if (bots_[1]) bots_[1]->setLevel((bezik::BotLevel)level_);
    }
    bool handOver() const override { return g_.stage() == bezik::Stage::HandOver || g_.stage() == bezik::Stage::MatchOver; }
    bool matchOver() const override { return g_.stage() == bezik::Stage::MatchOver; }

    std::string scoreTitle() const override {
        if (g_.stage() == bezik::Stage::NotStarted) return "";
        return "Bezik \xC2\xB7 " + std::to_string(g_.rules().target) + "'e";
    }
    std::vector<std::string> scoreLines() const override {
        return {names_[0] + " ....... " + std::to_string(g_.total(0)), names_[OPP_SEAT] + " ....... " + std::to_string(g_.total(1))};
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        const bool started = g_.stage() != bezik::Stage::NotStarted;
        const bool inPlay = g_.stage() == bezik::Stage::Playing || g_.stage() == bezik::Stage::Declare;
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        {
            heads[OPP_SEAT] = ctx_.characters ? ctx_.characters->headPosition(chr(OPP_SEAT)) : Vector3{};
            r3d::GameHud::Plate& p = plates[OPP_SEAT];
            p.show = started;
            p.name = names_[OPP_SEAT];
            p.label = "Toplam";
            p.value = std::to_string(g_.total(1));
            p.turn = inPlay && g_.current() == 1 && !tableBusy();
            p.badges.push_back({"Bu el " + std::to_string(g_.handPoints(1)), Color{52, 110, 64, 255}});
            if (g_.dealer() == 1) p.badges.push_back({"Dağıtan", Color{70, 60, 50, 255}});
        }
        hud_.plates(r, heads, plates);
        if (started && !dealing()) {
            const std::string koz = std::string("Koz: ") + kart::suitNameTR(g_.trump());
            const std::string tail = g_.secondStage() ? "  \xC2\xB7  deste bitti: renge uy, eli geç"
                                                       : "  \xC2\xB7  destede " + std::to_string(g_.stockSize() + (g_.turnUp() >= 0 ? 1 : 0));
            hud_.label3D(r, layPoint({STOCK_X + 0.02f, w3d::TABLE_Y + 0.01f, 0.09f}), koz + tail, Color{238, 198, 112, 255}, 14.f);
        }

        // the trick's winner declares: the choices
        const bool declaring = !aiSeat && !replay_ && g_.stage() == bezik::Stage::Declare && g_.current() == 0 && !tableBusy();
        panelKind_ = 0;
        if (declaring) {
            panelKind_ = 1;
            panelChoices_.clear();
            std::vector<r3d::GameHud::PanelButton> b;
            const Hint* tip = activeHint();
            auto add = [&](const std::string& label, const std::string& hint, int code) {
                b.push_back({label, true, tip && tip->choice == code, hint});
                panelChoices_.push_back(code);
            };
            if (g_.canKoz7(0))
                add("Koz 7'si  +10", g_.kozSevenSwaps() ? "açık kozla değiştir" : "göster", KOZ7_CHOICE);
            const std::vector<bezik::Meld> ms = g_.availableMelds(0);
            for (size_t i = 0; i < ms.size(); ++i) {
                std::string cards;
                for (int c : ms[i].cards) cards += (cards.empty() ? "" : " ") + kart::rankIndexTR(bezik::rankOf(c));
                add(std::string(bezik::meldNameTR(ms[i].kind)) + "  +" + std::to_string(ms[i].points), cards, (int)ms[i].kind);
            }
            add("Geç", "deklarasyon yapma", PASS_CHOICE);
            hud_.panel(mouse, "Eli aldın: deklarasyon", "Her el için bir deklarasyon (koz 7'si ayrıca sayılır)", b,
                       std::min(4, (int)b.size()), 300.f, 170.f);
        }

        Color sc = ui::pal::TextLight;
        const int a = actor();
        const char* ended = g_.stage() == bezik::Stage::HandOver ? "El bitti" : g_.stage() == bezik::Stage::MatchOver ? "Maç bitti" : nullptr;
        const std::string st = statusLine(ended, now_ < dealUntil_, "Kâğıtlar dağıtılıyor…", a, aiSeat, sc, [&] {
            if (g_.stage() == bezik::Stage::Declare) return "Deklarasyonunu seç";
            if (!g_.currentTrick().cards.empty()) return g_.secondStage() ? "Renge uy, geçebiliyorsan geç" : "Üstüne bir kâğıt at";
            return "Bir kâğıt aç";
        }, g_.stage() == bezik::Stage::Declare ? " deklarasyon yapıyor…" : " düşünüyor…");
        std::vector<std::pair<std::string, Color>> parts;
        if (started) {
            parts.push_back({"Bu el: " + std::to_string(g_.handPoints(0)), Color{160, 236, 160, 255}});
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
            parts.push_back({"Toplam " + std::to_string(g_.total(0)) + " - " + std::to_string(g_.total(1)), Color{238, 198, 112, 255}});
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
        for (const bezik::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        bool ok = true;
        for (const std::string& l : lines) {
            bezik::LoggedAction a;
            if (!bezik::LoggedAction::decode(l, a) || !g_.replay(a)) {
                ok = false;
                break;
            }
        }
        g_.drainEvents();
        syncStock();
        drawAt_ = -1.f;
        panelKind_ = 0;
        log_.clear();
        restoreView();
        return ok;
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const auto& rows = g_.sheet();
        m.columns = {aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[0], names_[OPP_SEAT]};
        m.humanCol = 0;
        m.title = std::to_string((int)rows.size()) + ". El Sonucu";
        m.corner = "bezik: " + std::to_string(g_.rules().target) + "'e";
        if (!rows.empty()) {
            const bezik::HandRecord& h = rows.back();
            m.headline = lastEnd_;
            std::string tag = std::string("koz ") + kart::suitNameTR(h.trump);
            for (int p = 0; p < 2; ++p)
                if (h.best[(size_t)p] >= 250)
                    tag += "  \xC2\xB7  " + (p == 0 ? std::string(aiMode ? "yapay zeka" : "sen") : names_[OPP_SEAT]) + ": " +
                           h.bestName[(size_t)p] + " " + std::to_string(h.best[(size_t)p]);
            m.tagline = tag;
            m.taglineRed = h.best[0] >= 500 || h.best[1] >= 500;
            m.starCol = h.points[0] == h.points[1] ? -1 : h.points[0] > h.points[1] ? 0 : 1;
            m.rowLabels = {"Deklarasyon", "Brisk", "Son el", "Bu el"};
            m.cells.assign(4, std::vector<std::string>(2));
            m.red.assign(4, std::vector<bool>(2, false));
            for (int p = 0; p < 2; ++p) {
                m.cells[0][(size_t)p] = std::to_string(h.melds[(size_t)p]);
                m.cells[1][(size_t)p] = std::to_string(h.brisques[(size_t)p]);
                m.cells[2][(size_t)p] = h.last[(size_t)p] ? std::to_string(h.last[(size_t)p]) : "";
                m.cells[3][(size_t)p] = std::to_string(h.points[(size_t)p]);
            }
        }
        for (const bezik::HandRecord& h : rows) {
            m.historyLabels.push_back(std::to_string(h.index + 1) + ". el");
            m.history.push_back({std::to_string(h.points[0]), std::to_string(h.points[1])});
        }
        for (int p = 0; p < 2; ++p) m.totals.push_back(std::to_string(g_.total(p)));
        const int best = std::max(g_.total(0), g_.total(1));
        for (int p = 0; p < 2; ++p)
            if (g_.total(p) == best && best > 0) m.leaders.push_back(p);
        m.last = g_.stage() == bezik::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.rules().target) + "'e ilk varan kazanır";
        m.matchHeader = "Maç Bitti";
        m.playedLine = std::to_string(rows.size()) + " el oynandı";
        const int w = g_.winner();
        m.ranking = {w, 1 - w};
        m.rank = {1, g_.total(0) == g_.total(1) ? 1 : 2};
        for (int p = 0; p < 2; ++p) {
            int melds = 0, best1 = 0;
            std::string bestName;
            for (const bezik::HandRecord& h : rows) {
                melds += h.melds[(size_t)p];
                if (h.best[(size_t)p] > best1) best1 = h.best[(size_t)p], bestName = h.bestName[(size_t)p];
            }
            m.rankSub.push_back("deklarasyonlar " + std::to_string(melds) + (best1 ? "  \xC2\xB7  en büyüğü " + bestName : ""));
        }
        m.humanWon = g_.stage() == bezik::Stage::MatchOver && g_.winner() == 0;
        if (m.humanWon) {
            m.resultTitle = std::string(aiMode ? "Yapay zeka kazandı! " : "Kazandın! ") + kOppTea[oppSeat_];
            m.resultSub = std::to_string(g_.total(0)) + " - " + std::to_string(g_.total(1)) + " ile beziğin ustası.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir maç daha?";
            m.resultSub = "Kazanan: " + names_[OPP_SEAT] + " (" + std::to_string(g_.total(1)) + " - " + std::to_string(g_.total(0)) + ")";
        }
        return m;
    }

protected:
    int replayLine(const std::string& line) override {
        bezik::LoggedAction a;
        if (!bezik::LoggedAction::decode(line, a)) return -1;
        if (a.kind == bezik::LogKind::NextHand) {
            if (g_.stage() == bezik::Stage::HandOver) return 0;
            return g_.stage() == bezik::Stage::MatchOver ? -1 : 1;
        }
        if (!g_.replay(a)) return -1;
        pumpEngine();
        return 1;
    }

    void pumpEngine() override {
        for (const bezik::GameEvent& e : g_.drainEvents()) {
            using E = bezik::EvType;
            const int seat = e.seat >= 0 ? seatOf(e.seat) : -1;
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::HandStart:
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                drawAt_ = -1.f;
                break;
            case E::Deal:
                syncStock();
                dealFrom(seatOf(g_.dealer()));
                hud_.toast(e.text, Color{238, 198, 112, 255}, 3.0f);
                break;
            case E::Play:
                if (e.fromTable && seat != 0) playFromTable(seat, e.card);
                else onPlayed(seat, e.card, seat != 0 || aiSeat_);
                break;
            case E::TrickWon: onTrickWon(seat, e.cards); break;
            case E::Declared: {
                const bool big = e.amount >= 100;
                hud_.toast(e.text, Color{238, 198, 112, 255}, big ? 3.6f : 2.8f);
                sound(e.amount >= 250 ? ui::Sfx::Win : ui::Sfx::CardSnap);
                if (e.amount >= 500 && ctx_.characters)
                    ctx_.characters->crowdReact(r3d::Characters::CrowdCheer, layPoint({0.f, 0.9f, 0.f}), 0.8f);
                if (seat != 0) {
                    // his right hand pulls the cards out of the fan and lays them face up in front of him (the cards
                    // wait for it: layoutExtra, meldLead_)
                    const int n = std::max(1, (int)g_.tableCards(1).size());
                    if (ctx_.characters) ctx_.characters->playCard(chr(seat), layPoint(meldPose(seat, n / 2, n).pos), false);
                    meldLead_ = true;
                    say(seat, meldLine(e.meld), big);
                } else if (e.amount >= 500) {
                    say(OPP_SEAT, pick(kOppYouCift[oppSeat_], rng_, names_[0]), true);
                } else if (e.amount >= 100 && rng_.chance(0.6f)) {
                    say(OPP_SEAT, pick(kOppYouBig[oppSeat_], rng_, names_[0]));
                }
                if (e.amount >= 100 && rng_.chance(e.amount >= 500 ? 0.9f : 0.4f)) watcherSays(e.amount >= 500 ? 2 : seat == 0 ? 0 : 1);
                if (seat == 0 && e.meld == bezik::MeldKind::Bezik) noteAchievement("bezik");
                if (seat == 0 && e.meld == bezik::MeldKind::CiftBezik) noteAchievement("cift_bezik");
                break;
            }
            case E::Koz7:
                hud_.toast(e.text, Color{238, 198, 112, 255}, 2.6f);
                if (e.swapped) {
                    // the 7 goes under the stock, the koz card into the hand
                    if (!shownStock_.empty() && shownStock_[0] == e.card2) shownStock_[0] = e.card;
                    if (seat != 0) {
                        playFromHand(seat, e.card, layPose(kozPose()), false);
                        if (rng_.chance(0.6f)) say(seat, pick(kOppYedi[oppSeat_], rng_));
                    } else {
                        sound(ui::Sfx::CardPlace);
                    }
                } else {
                    sound(ui::Sfx::CardSnap);
                    if (seat != 0 && rng_.chance(0.5f)) say(seat, pick(kOppYedi[oppSeat_], rng_));
                }
                break;
            case E::Draw:
                // the drawn cards stay on the stock until the trick has gone to its pile
                drawAt_ = now_ + 1.75f / speed_;
                if (seat != 0) drawSeat_ = seat;
                break;
            case E::Stage2:
                hud_.toast(e.text, ui::pal::Highlight, 3.4f);
                if (rng_.chance(0.35f)) watcherSays(3);
                break;
            case E::HandEnd: {
                log_ = e.text;
                for (const std::string& l : e.lines) log_ += " | " + l;
                lastEnd_ = e.text + ":  " + std::to_string(e.points[0]) + " - " + std::to_string(e.points[1]);
                const bool mine = e.points[0] > e.points[1];
                hud_.toast(lastEnd_, mine ? Color{160, 236, 160, 255} : ui::pal::Bad, 4.0f);
                sound(ui::Sfx::Win);
                if (!mine) {
                    say(OPP_SEAT, pick(kOppWon[oppSeat_], rng_), true);
                    if (ctx_.characters) ctx_.characters->react(chr(OPP_SEAT), 1, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                } else if (rng_.chance(0.7f)) {
                    say(OPP_SEAT, pick(kOppLost[oppSeat_], rng_));
                    if (ctx_.characters) ctx_.characters->react(chr(OPP_SEAT), 2, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                }
                drawAt_ = -1.f;
                syncStock();
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            default: break;
            }
        }
    }

    // The concealed hand as the table shows it: the cards still lying on the stock (drawn, not yet taken) are not in it.
    std::vector<int> handOf(int seat) const override {
        const int p = playerOf(seat);
        if (p < 0 || g_.stage() == bezik::Stage::NotStarted) return {};
        std::vector<int> h;
        for (int c : g_.hand(p))
            if (std::find(shownStock_.begin(), shownStock_.end(), c) == shownStock_.end()) h.push_back(c);
        return h;
    }
    int actor() const override {
        if (g_.stage() != bezik::Stage::Playing && g_.stage() != bezik::Stage::Declare) return -1;
        return seatOf(g_.current());
    }
    bool humanTurn() const override { return g_.stage() == bezik::Stage::Playing && g_.current() == 0; }
    std::vector<int> playableCards() const override { return humanTurn() ? g_.legalCards(0) : std::vector<int>{}; }
    std::vector<int> clickable() const override {
        std::vector<int> v = handOf(0);
        if (g_.stage() != bezik::Stage::NotStarted)
            v.insert(v.end(), g_.tableCards(0).begin(), g_.tableCards(0).end());
        return v;
    }
    void playHumanCard(int card) override { report(g_.playCard(0, card)); }
    float botDelay(int seat) const override {
        (void)seat;
        if (g_.stage() == bezik::Stage::Declare) return 0.85f;
        return g_.currentTrick().cards.empty() ? 0.95f : 0.7f;
    }
    void botStep(int seat) override {
        const int p = playerOf(seat);
        if (p < 0 || !bots_[(size_t)p]) return;
        const bezik::BotAction a = bots_[(size_t)p]->next(g_, p);
        if (!bezik::applyBotAction(g_, p, a).ok) bezik::applyBotAction(g_, p, bezik::fallbackAction(g_, p));
    }
    bool extraBusy() const override { return drawAt_ >= 0.f; }
    std::vector<std::pair<int, r3d::CardPose>> dealtExtras() const override {
        if (shownStock_.empty()) return {};
        return {{shownStock_[0], layPose(kozPose())}}; // the 17th card, turned up
    }
    int deckRemaining() const override { return std::max(0, (int)shownStock_.size() - 1); }
    void layoutExtra() override {
        if (drawAt_ >= 0.f && now_ >= drawAt_) { // the drawn cards go to the hands now
            drawAt_ = -1.f;
            drawSeat_ = -1;
            const std::vector<int> before = shownStock_;
            syncStock();
            // each flies to its new owner's hand; the opponent's own hand takes his off the stock into the fan
            for (int c : before) {
                if (std::find(shownStock_.begin(), shownStock_.end(), c) != shownStock_.end()) continue;
                for (int p = 0; p < 2; ++p) {
                    const std::vector<int>& h = g_.hand(p);
                    if (std::find(h.begin(), h.end(), c) != h.end()) drawFromStock(seatOf(p), c);
                }
            }
            sound(ui::Sfx::CardSlide);
        }
        // the declared cards, face up in front of their owner
        for (int p = 0; p < 2; ++p) {
            if (g_.stage() == bezik::Stage::NotStarted) break;
            std::vector<int> row = g_.tableCards(p);
            std::sort(row.begin(), row.end(), rowLess);
            const int s = seatOf(p);
            const std::vector<int> legal = p == 0 ? playableCards() : std::vector<int>{};
            int fresh = 0; // (the opponent's newly declared cards leave his fan with his hand: w3d::BOT_GIVE_LEAD)
            for (size_t i = 0; i < row.size(); ++i) {
                const int c = row[i];
                if (std::find(shownStock_.begin(), shownStock_.end(), c) != shownStock_.end()) continue;
                const bool isNew = std::find(rowSeen_[(size_t)p].begin(), rowSeen_[(size_t)p].end(), c) == rowSeen_[(size_t)p].end();
                const float delay = isNew && p == 1 && meldLead_ ? w3d::BOT_GIVE_LEAD + 0.05f * (float)fresh++ : 0.f;
                placeExtra(c, layPose(meldPose(s, (int)i, (int)row.size())), true, delay);
                if (p == 0 && humanTurn() && !aiSeat_ && !replay_) {
                    const bool isLegal = std::find(legal.begin(), legal.end(), c) != legal.end();
                    const bool hov = c == hover_ || (ui::keyboardNav() && c == kbCard_);
                    cards_.setLift(c, hov && isLegal ? 0.010f : 0.f);
                    cards_.setGlow(c, hov && isLegal ? 0.8f : 0.f);
                    if (hints_ && !isLegal) cards_.setTint(c, Color{150, 146, 140, 255});
                }
                const Hint* h = activeHint();
                if (h && h->card == c) cards_.setLift(c, 0.012f);
            }
            rowSeen_[(size_t)p] = row;
        }
        meldLead_ = false;
        if (shownStock_.empty()) return;
        const int n = (int)shownStock_.size();
        // the face-down stock: while dealing it is the dealer's deck (under the cards being dealt)
        for (int i = 1; i < n; ++i) {
            const int c = shownStock_[(size_t)i];
            if (dealing()) placeExtra(c, deckSlot(seatOf(g_.dealer()), i - 1), true);
            else placeExtra(c, layPose(stockSlot(i - 1)));
        }
        placeExtra(shownStock_[0], layPose(kozPose()));
    }
    size_t decisionStamp() const override { return g_.actionLog().size() + 100000u * (size_t)g_.handIndex(); }
    bool computeHint(Hint& h) override {
        if (actor() != 0 || !bots_[0]) return false;
        const bezik::BotAction a = bots_[0]->next(g_, 0);
        switch (a.kind) {
        case bezik::BotAction::Kind::Koz7:
            h.choice = KOZ7_CHOICE;
            h.text = g_.kozSevenSwaps() ? "İpucu: koz 7'sini açık kozla değiştir (+10)" : "İpucu: koz 7'sini göster (+10)";
            break;
        case bezik::BotAction::Kind::Declare:
            h.choice = (int)a.meld.kind;
            h.text = std::string("İpucu: ") + bezik::meldNameTR(a.meld.kind) + " söyle (+" + std::to_string(a.meld.points) + ")";
            break;
        case bezik::BotAction::Kind::Pass:
            h.choice = PASS_CHOICE;
            h.text = "İpucu: geç";
            break;
        case bezik::BotAction::Kind::Play:
            h.card = a.card;
            h.text = "İpucu: " + bezik::cardAccusative(a.card) + (g_.currentTrick().cards.empty() ? " aç" : " at");
            break;
        }
        return true;
    }
    std::vector<std::pair<int, int>> trickOnTable() const override {
        std::vector<std::pair<int, int>> v;
        if (g_.stage() != bezik::Stage::Playing) return v;
        for (const bezik::TrickCard& tc : g_.currentTrick().cards) v.push_back({seatOf(tc.seat), tc.card});
        return v;
    }
    std::array<std::vector<int>, 4> wonPiles() const override {
        std::array<std::vector<int>, 4> w;
        for (int p = 0; p < 2; ++p) w[(size_t)seatOf(p)] = g_.won(p);
        return w;
    }
    void onHudClick(int id) override {
        const int i = id - r3d::GameHud::PANEL_ID;
        if (i < 0 || panelKind_ != 1 || i >= (int)panelChoices_.size()) return;
        if (g_.stage() != bezik::Stage::Declare || g_.current() != 0) return;
        sound(ui::Sfx::Button);
        const int code = panelChoices_[(size_t)i];
        if (code == KOZ7_CHOICE) {
            report(g_.koz7(0));
        } else if (code == PASS_CHOICE) {
            report(g_.pass(0));
        } else {
            for (const bezik::Meld& m : g_.availableMelds(0))
                if ((int)m.kind == code) {
                    report(g_.declare(0, m));
                    break;
                }
        }
        pumpEngine();
    }

private:
    static constexpr int KOZ7_CHOICE = 100;
    static constexpr int PASS_CHOICE = 101;

    void report(const bezik::ActionResult& r) {
        if (r.ok) return;
        hud_.toast(r.error, ui::pal::Bad, 2.6f);
        sound(ui::Sfx::Error);
    }
    // The stock as it lies on the table: [0] the turned-up koz card (if any), then the face-down cards bottom first.
    void syncStock() {
        shownStock_.clear();
        if (g_.stage() == bezik::Stage::NotStarted) return;
        if (g_.turnUp() >= 0) shownStock_.push_back(g_.turnUp());
        for (int c : g_.stockCards()) shownStock_.push_back(c);
    }
    // A bot plays one of its declared cards: the hand takes it from the row in front of it and lays it on the trick
    // (Characters::carry, on the card's own flight).
    void playFromTable(int seat, int card) {
        const r3d::CardPose p = layPose(r3d::cardlayout::trick(seat, (int)shown_.size() + (int)(rng_.next() % 7)));
        shown_.push_back({seat, card});
        const Vector3 from = cards_.pose(card).pos;
        cards_.place(card, p, true, w3d::BOT_TAKE_LEAD, 0.06f);
        const float fly = std::clamp(0.22f + Vector3Distance(from, p.pos) * 0.55f, 0.25f, 0.75f); // (Cards3D::place)
        if (ctx_.characters) {
            r3d::PieceCarry c;
            c.card = true;
            c.lead = w3d::BOT_TAKE_LEAD;
            c.seg = fly;
            c.hop = 0.06f;
            c.path = {from, p.pos};
            ctx_.characters->carry(chr(seat), c);
        }
        soundAt(ui::Sfx::CardPlace, (w3d::BOT_TAKE_LEAD + fly) / speed_);
    }
    // One of the two who are not playing calls over from the okey table (ui::bezikRemark; {o}: the opponent).
    void watcherSays(int situation) {
        const int who = watcherSeat(rng_.chance(0.5f) ? 0 : 1);
        std::string t = ui::bezikRemark(who, situation, rng_.range(3), oppSeat_);
        size_t p;
        while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, names_[0]);
        if (!t.empty()) say(who, t, situation == 2);
    }
    std::string meldLine(bezik::MeldKind k) {
        using K = bezik::MeldKind;
        switch (k) {
        case K::Bezik: return pick(kOppBezik[oppSeat_], rng_);
        case K::CiftBezik: return pick(kOppCift[oppSeat_], rng_);
        case K::Seri: return pick(kOppSeri[oppSeat_], rng_);
        case K::Evlilik: return pick(kOppEvli[oppSeat_], rng_);
        case K::KozEvlilik: return pick(kOppKozEvli[oppSeat_], rng_);
        default: return pick(kOppDort[oppSeat_], rng_);
        }
    }

    bezik::Game g_;
    std::array<std::unique_ptr<bezik::Bot>, 2> bots_;
    std::vector<int> shownStock_;
    float drawAt_ = -1.f;
    int drawSeat_ = -1;
    std::array<std::vector<int>, 2> rowSeen_{}; // the declared rows as last laid out (new cards: the hand lays them)
    bool meldLead_ = false;                     // the opponent just declared: his new row cards wait for his hand
    int panelKind_ = 0;
    std::vector<int> panelChoices_;
    std::string lastEnd_;
};

} // namespace

std::unique_ptr<TableGame> makeBezikTable() { return std::make_unique<BezikTable>(); }

} // namespace app
