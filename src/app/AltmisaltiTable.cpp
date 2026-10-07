// Altmışaltı (66) at the two-seat tavla table: engine altmisalti::Game, bots altmisalti::Bot, the shared card table.
// The player (engine player 0) sits at seat 0, the opponent (engine player 1; Rakip, Kel Mahmut by default) across as seat 2 (CardTableFrame maps the
// card layout onto the tavla table). Our own pieces: the stock with the koz card turned up crosswise under it (closed:
// the koz turned face down on top), the cards drawn after a trick (they stay on the stock until the trick is swept),
// and a declared marriage's other half shown for a moment beside the led card.
#include "app/CardTable.h"
#include "core/Altmisalti.h"
#include "core/AltmisaltiBot.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

constexpr int OPP_SEAT = 2; // the opponent, across (layout seat; who sits there: CardTableBase::oppSeat_)
int seatOf(int player) { return player == 0 ? 0 : OPP_SEAT; }
int playerOf(int seat) { return seat == 0 ? 0 : seat == OPP_SEAT ? 1 : -1; }

// What the opponent says, by who sits across (Rakip: seat 1 Hacı Rıza calm, proverbs; 2 Kel Mahmut loud, a football
// fan; 3 Emekli Nuri careful, "bizim zamanımızda").
const char* const kOppKirk[4][3] = {
    {},
    {"Kırk, koz evliliği. Hayırlısı.", "Kozda kırk var evlat, yazıver.", "Sabredene kırk gelir: koz evliliği."},
    {"Kırk! Koz evliliği, yaz bakalım!", "Kırk var abi, kırk!", "Kozda kırk, gol gibi!"},
    {"Kırk. Koz evliliği, hesapta vardı.", "Kozda kırk. Tecrübe, delikanlı.", "Kırk yazın. Bizim zamanımızda böyle saklanırdı."},
};
const char* const kOppYirmi[4][3] = {
    {},
    {"Yirmi de bizden, bereketli olsun.", "Bir evlilik, yirmi. Allah mesut etsin.", "Yirmi, yaz evlat."},
    {"Yirmi de benden!", "Yirmi, yaz oraya!", "Bir evlilik daha, yirmi!"},
    {"Yirmi. Ufak ama sayılır.", "Evlilik, yirmi. Yazın.", "Yirmi; damla damla göl olur."},
};
const char* const kOppClose[4][3] = {
    {},
    {"Kapattım, tevekkül.", "Desteyi kapattım evlat; bakalım nasip.", "Kapattım. Acele işe şeytan karışır ama hesabım tamam."},
    {"Kapattım! Say bakalım!", "Deste kapalı, şimdi görürsün!", "Kapattım, kaçış yok!"},
    {"Kapattım. Hesabını yaptım.", "Deste kapalı. Renge uy bakalım delikanlı.", "Kapattım; yaşlı kurt boşuna kapatmaz."},
};
const char* const kOppSwap[4][3] = {
    {},
    {"Dokuzla kozu alayım, izninle.", "Koz dokuzu bende, açık kozu alıyorum.", "Dokuz gitti, koz geldi; hayırlısı."},
    {"Dokuzla kozu aldım, sağ ol!", "Şu kozu bir alayım…", "Dokuz gitti, koz geldi!"},
    {"Dokuzla kozu alırım. Kural kural.", "Koz dokuzu… Bizim zamanımızda bunu unutan çok olurdu.", "Kozu aldım. Usul böyle."},
};
const char* const kOppWon[4][3] = {
    {},
    {"Altmış altı, elhamdülillah.", "Saydık, yetti evlat.", "Bu el bizden, hayırlısı."},
    {"Altmış altı! Bu el benim abi!", "Saydım, yetti! Al sana!", "Bu da bizden, yaz!"},
    {"Altmış altı. Saymasını bilen kazanır.", "Yetti. Hesap ortada.", "Bu el benim delikanlı. Tecrübe."},
};
const char* const kOppLost[4][3] = {
    {},
    {"Nasip değilmiş, olsun.", "Bu el de senin olsun evlat.", "Kaybetmesini de bileceksin; bir daha."},
    {"Hay aksi… Kâğıt gelmedi.", "Hakem bu eli de yedi!", "Olsun, rövanşı alırız."},
    {"Hıh. Kâğıt gelmedi.", "Bu kâğıtlarla kim olsa kaybederdi.", "Gözlüğüm buğulandı herhalde."},
};
const char* const kOppYouClose[4][2] = {
    {}, {"Kapattın ha? Allah kolaylık versin.", "Cesaret güzel evlat; hesabı da tuttur."},
    {"Kapattın mı? Cesaretine bak!", "Hadi bakalım, tuttur da görelim!"},
    {"Kapattın ha? Gençlik…", "Erken kapatan çok gördüm delikanlı."},
};
const char* const kOppYouFail[4][2] = {
    {}, {"Kapattın ama olmadı {h}; acele işe şeytan karışır.", "Erken kapattın {h}, olsun, öğrenirsin."},
    {"Kapattın ama olmadı {h}!", "Erken kapattın {h}, ha ha!"},
    {"Dedim sana {h}, erken kapattın.", "Hesapsız kapatılmaz {h}. Ders olsun."},
};
const char* const kOppYouMarriage[4][2] = {
    {}, {"Maşallah {h}, evlilik; Allah mesut etsin.", "Evlilik ha {h}? Hayırlı olsun."},
    {"Evlilik mi? Maşallah {h}.", "Vay, evlilik! Şanslısın {h}."},
    {"Evlilik. Fena değil {h}.", "Hıh, evlilik. Acemi şansı {h}."},
};
// "Çaylar Mahmut'tan!": the opponent's name with its ablative suffix.
const char* const kOppTea[4] = {"", "Çaylar Rıza'dan!", "Çaylar Mahmut'tan!", "Çaylar Nuri'den!"};

template <size_t N>
std::string pick(const char* const (&lines)[N], okey::Rng& rng, const std::string& h = {}) {
    std::string t = lines[(size_t)rng.range((int)N)];
    size_t p;
    while ((p = t.find("{h}")) != std::string::npos) t.replace(p, 3, h);
    return t;
}

// The stock (layout space): on the player's left of the middle; the koz card across under it, sticking out.
constexpr float STOCK_X = -0.30f;
r3d::CardPose stockSlot(int i) {
    r3d::CardPose p;
    p.pos = {STOCK_X, w3d::TABLE_Y + r3d::CARD_T * (1.5f + (float)i), 0.f};
    p.rot = r3d::cardFlat(4.f + (float)((i * 37) % 5 - 2) * 0.6f, false);
    return p;
}
r3d::CardPose kozPose(bool closed, int onTop) {
    r3d::CardPose p;
    if (closed) { // turned face down on top of the stock
        p.pos = {STOCK_X, w3d::TABLE_Y + r3d::CARD_T * (2.0f + (float)onTop), 0.f};
        p.rot = r3d::cardFlat(90.f, false);
    } else {
        p.pos = {STOCK_X + 0.034f, w3d::TABLE_Y + r3d::CARD_T * 0.5f, 0.f};
        p.rot = r3d::cardFlat(90.f, true);
    }
    return p;
}
// A declared marriage's other half, shown face up beside the led card for a moment.
r3d::CardPose shownPartnerPose(int seat) {
    const r3d::CardPose t = r3d::cardlayout::trick(seat, 0);
    r3d::CardPose p;
    p.pos = Vector3Add(t.pos, w3d::seatLocal(seat, 0.075f, -0.01f, 0.f));
    p.pos.y = w3d::TABLE_Y + 0.0012f;
    p.rot = r3d::cardFlat(8.f, true); // readable from the player
    return p;
}

class AltmisaltiTable final : public CardTableBase {
public:
    std::vector<int> seats() const override { return {0, oppSeat_}; }
    int location() const override { return 1; }

    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        altmisalti::Rules rules;
        g_ = altmisalti::Game(rules);
        names_ = names;
        seatOpponent(ui::twoPlayerOpponent(st, ui::GameKind::Altmisalti)); // Rakip
        g_.setPlayer(0, names_[0], true);
        g_.setPlayer(1, names_[OPP_SEAT], false);
        for (int p = 0; p < 2; ++p) {
            const altmisalti::BotLevel lv = p == 0 ? altmisalti::BotLevel::Kurt : (altmisalti::BotLevel)std::clamp(level_, 0, 2);
            bots_[(size_t)p] = std::make_unique<altmisalti::Bot>(lv, seed * 2 + (uint64_t)p + 0x66ull);
            bots_[(size_t)p]->setStyle(altmisalti::BotStyle::forSeat(chr(seatOf(p)))); // the opponent's own style, the AI neutral
        }
        rng_.reseed(seed ^ 0x6666ull);
        replay_ = false;
        lastEnd_.clear();
        resetTable();
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != altmisalti::Stage::HandOver) return;
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        if (bots_[1]) bots_[1]->setLevel((altmisalti::BotLevel)level_);
    }
    bool handOver() const override {
        return g_.stage() == altmisalti::Stage::HandOver || g_.stage() == altmisalti::Stage::MatchOver;
    }
    bool matchOver() const override { return g_.stage() == altmisalti::Stage::MatchOver; }

    std::string scoreTitle() const override {
        if (g_.stage() == altmisalti::Stage::NotStarted) return "";
        return "Altmışaltı \xC2\xB7 " + std::to_string(g_.rules().target) + " oyuna";
    }
    std::vector<std::string> scoreLines() const override {
        return {names_[0] + " ....... " + std::to_string(g_.total(0)), names_[OPP_SEAT] + " ....... " + std::to_string(g_.total(1))};
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        const bool playing = g_.stage() == altmisalti::Stage::Playing;
        const bool started = g_.stage() != altmisalti::Stage::NotStarted;
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        {
            heads[OPP_SEAT] = ctx_.characters ? ctx_.characters->headPosition(chr(OPP_SEAT)) : Vector3{};
            r3d::GameHud::Plate& p = plates[OPP_SEAT];
            p.show = started;
            p.name = names_[OPP_SEAT];
            p.label = "Oyun";
            p.value = std::to_string(g_.total(1));
            p.turn = playing && g_.current() == 1 && !tableBusy();
            p.badges.push_back({"Puan " + std::to_string(g_.points(1)), Color{52, 110, 64, 255}});
            if (g_.pendingMarriage(1) > 0)
                p.badges.push_back({"Bekleyen " + std::to_string(g_.pendingMarriage(1)), Color{168, 120, 30, 255}});
            p.badges.push_back({"El " + std::to_string(g_.tricks(1)), Color{70, 60, 50, 255}});
            if (g_.closed() && g_.closer() == 1) p.badges.push_back({"Kapattı", Color{168, 42, 34, 255}});
            if (g_.dealer() == 1) p.badges.push_back({"Dağıtan", Color{70, 60, 50, 255}});
        }
        hud_.plates(r, heads, plates);
        if (started && !dealing()) {
            const std::string koz = std::string("Koz: ") + kart::suitNameTR(g_.trumpSuit());
            std::string tail;
            if (g_.closed()) tail = "  \xC2\xB7  kapalı";
            else if (g_.stockCount() == 0) tail = "  \xC2\xB7  deste bitti";
            else tail = "  \xC2\xB7  destede " + std::to_string(g_.stockCount() - 1);
            hud_.label3D(r, layPoint({STOCK_X + 0.01f, w3d::TABLE_Y + 0.01f, 0.085f}), koz + tail, Color{238, 198, 112, 255}, 14.f);
        }
        Color sc = ui::pal::TextLight;
        std::string st;
        const int a = actor();
        if (g_.stage() == altmisalti::Stage::HandOver) st = "El bitti";
        else if (g_.stage() == altmisalti::Stage::MatchOver) st = "Maç bitti";
        else if (now_ < dealUntil_) st = "Kâğıtlar dağıtılıyor…";
        else if (a == 0 && replay_) st = replaySelfStatus();
        else if (a == 0 && aiSeat) st = "Yapay zeka düşünüyor…";
        else if (a == 0) {
            sc = ui::pal::Highlight;
            if (g_.ledCard() >= 0) st = g_.strict() ? "Üstüne at: renge uy, eli yükselt" : "Üstüne bir kâğıt at";
            else st = "Bir kâğıt aç";
            const int look = hover_ >= 0 ? hover_ : (ui::keyboardNav() ? kbCard_ : -1);
            if (hints_ && look >= 0 && g_.ledCard() < 0) {
                const int m = g_.marriageValue(0, look);
                if (m > 0) st = "Evlilik: " + std::to_string(m) + (g_.tricks(0) == 0 ? " (ilk elini alınca yazılır)" : "");
                else if (look == g_.faceUpTrump() && g_.canExchange(0)) st = "Koz dokuzuyla bu kozu al";
            }
        } else if (a > 0) {
            st = names_[(size_t)a] + " düşünüyor…";
        }
        std::vector<std::pair<std::string, Color>> parts;
        if (started) {
            parts.push_back({"Puanın: " + std::to_string(g_.points(0)), Color{160, 236, 160, 255}});
            if (g_.pendingMarriage(0) > 0)
                parts.push_back({" (+" + std::to_string(g_.pendingMarriage(0)) + " bekliyor)", Color{238, 198, 112, 255}});
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
            parts.push_back({"Oyun " + std::to_string(g_.total(0)) + " - " + std::to_string(g_.total(1)), Color{238, 198, 112, 255}});
        }
        hud_.status(st, sc, a == 0 && !aiSeat && !replay_, parts, aiSeat && !replay_);
        drawButtons(mouse, aiSeat);
        hud_.toasts();
    }

    bool humanHandScore(int& score) const override {
        if (g_.sheet().empty()) return false;
        score = g_.sheet().back().points[0]; // the card points of the hand
        return true;
    }

    bool saveState(std::vector<std::string>& lines) const override {
        lines.clear();
        for (const altmisalti::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        bool ok = true;
        for (const std::string& l : lines) {
            altmisalti::LoggedAction a;
            if (!altmisalti::LoggedAction::decode(l, a) || !g_.replay(a)) {
                ok = false;
                break;
            }
        }
        g_.drainEvents();
        shownStock_ = g_.stockCards();
        partner_ = -1;
        drawAt_ = -1.f;
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
        m.corner = "altmışaltı: " + std::to_string(g_.rules().target) + " oyuna";
        if (!rows.empty()) {
            const altmisalti::HandRecord& h = rows.back();
            m.headline = lastEnd_;
            std::string tag = std::string("koz ") + kart::suitNameTR(lastTrump_);
            if (h.closer >= 0) tag += "  \xC2\xB7  " + (h.closer == 0 ? std::string(aiMode ? "yapay zeka kapattı" : "sen kapattın")
                                                                      : names_[OPP_SEAT] + " kapattı");
            if (h.marriages[0] + h.marriages[1] > 0)
                tag += "  \xC2\xB7  evlilik " + std::to_string(h.marriages[0]) + " / " + std::to_string(h.marriages[1]);
            m.tagline = tag;
            m.taglineRed = h.reason == altmisalti::EndReason::CloserFailed;
            m.starCol = h.winner;
            m.rowLabels = {"Puan", "El", "Oyun"};
            m.cells.assign(3, std::vector<std::string>(2));
            m.red.assign(3, std::vector<bool>(2, false));
            for (int p = 0; p < 2; ++p) {
                m.cells[0][(size_t)p] = std::to_string(h.points[(size_t)p]);
                m.cells[1][(size_t)p] = std::to_string(h.tricks[(size_t)p]);
                m.cells[2][(size_t)p] = h.winner == p ? "+" + std::to_string(h.gamePoints) : "";
                m.red[0][(size_t)p] = h.winner != p && h.points[(size_t)p] < altmisalti::SCHNEIDER;
            }
        }
        for (const altmisalti::HandRecord& h : rows) {
            m.historyLabels.push_back(std::to_string(h.index + 1) + ". el");
            std::vector<std::string> row(2);
            row[(size_t)h.winner] = "+" + std::to_string(h.gamePoints);
            m.history.push_back(row);
        }
        for (int p = 0; p < 2; ++p) m.totals.push_back(std::to_string(g_.total(p)));
        m.totalLabel = "Oyun";
        const int best = std::max(g_.total(0), g_.total(1));
        for (int p = 0; p < 2; ++p)
            if (g_.total(p) == best && best > 0) m.leaders.push_back(p);
        m.last = g_.stage() == altmisalti::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.rules().target) + " oyuna ilk varan kazanır";
        m.matchHeader = "Maç Bitti";
        m.playedLine = std::to_string(rows.size()) + " el oynandı";
        const int w = g_.total(0) >= g_.total(1) ? 0 : 1;
        m.ranking = {w, 1 - w};
        m.rank = {1, g_.total(0) == g_.total(1) ? 1 : 2};
        for (int p = 0; p < 2; ++p) {
            int hands = 0, closes = 0;
            for (const altmisalti::HandRecord& h : rows) {
                hands += h.winner == p ? 1 : 0;
                closes += h.closer == p && h.winner == p ? 1 : 0;
            }
            m.rankSub.push_back(std::to_string(hands) + " el kazandı" + (closes ? "  \xC2\xB7  " + std::to_string(closes) + " kapatıp tutturdu" : ""));
        }
        m.humanWon = g_.matchWinner() == 0;
        if (m.humanWon) {
            m.resultTitle = std::string(aiMode ? "Yapay zeka kazandı! " : "Kazandın! ") + kOppTea[oppSeat_];
            m.resultSub = std::to_string(g_.total(0)) + " - " + std::to_string(g_.total(1)) + " ile altmışaltının ustası.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir el daha?";
            m.resultSub = "Kazanan: " + names_[OPP_SEAT] + " (" + std::to_string(g_.total(1)) + " - " + std::to_string(g_.total(0)) + ")";
        }
        return m;
    }

protected:
    int replayLine(const std::string& line) override {
        altmisalti::LoggedAction a;
        if (!altmisalti::LoggedAction::decode(line, a)) return -1;
        if (a.kind == altmisalti::LogKind::NextHand) {
            if (g_.stage() == altmisalti::Stage::HandOver) return 0;
            return g_.stage() == altmisalti::Stage::MatchOver ? -1 : 1;
        }
        if (!g_.replay(a)) return -1;
        pumpEngine();
        return 1;
    }

    void pumpEngine() override {
        for (const altmisalti::GameEvent& e : g_.drainEvents()) {
            using E = altmisalti::EvType;
            const int seat = e.seat >= 0 ? seatOf(e.seat) : -1;
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::HandStart:
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                shownStock_ = g_.stockCards();
                partner_ = -1;
                drawAt_ = -1.f;
                break;
            case E::Deal:
                shownStock_ = g_.stockCards();
                lastTrump_ = g_.trumpSuit();
                dealFrom(seatOf(g_.dealer()));
                hud_.toast(e.text, Color{238, 198, 112, 255}, 3.0f);
                break;
            case E::Exchange: {
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                const int took = e.amount;
                // the 9 goes under the stock, the koz card into the hand
                for (int& c : shownStock_)
                    if (c == took) c = e.card;
                const r3d::CardPose kp = layPose(kozPose(false, 0));
                if (seat != 0) {
                    playFromHand(seat, e.card, kp, false);
                    if (rng_.chance(0.6f)) say(seat, pick(kOppSwap[oppSeat_], rng_));
                } else {
                    sound(ui::Sfx::CardPlace);
                }
                break;
            }
            case E::Close:
                hud_.toast(e.text, ui::pal::Highlight, 3.0f);
                sound(ui::Sfx::CardSnap);
                if (seat != 0) {
                    say(seat, pick(kOppClose[oppSeat_], rng_), true);
                    // his hand turns the koz card over onto the stock (the card waits for the fingers: kozLead_)
                    if (ctx_.characters) {
                        r3d::PieceCarry c;
                        c.card = true;
                        c.lead = w3d::BOT_TAKE_LEAD;
                        c.seg = 0.42f;
                        c.hop = 0.05f;
                        c.path = {layPoint(kozPose(false, 0).pos), layPoint(kozPose(true, (int)shownStock_.size() - 1).pos)};
                        ctx_.characters->carry(chr(seat), c);
                        kozLead_ = true;
                    }
                } else if (rng_.chance(0.7f)) {
                    say(OPP_SEAT, pick(kOppYouClose[oppSeat_], rng_));
                }
                break;
            case E::Marriage: {
                hud_.toast(e.text, Color{238, 198, 112, 255}, 3.2f);
                partner_ = kart::makeCard(e.suit, kart::rankOf(e.card) == kart::Kiz ? kart::Papaz : kart::Kiz);
                partnerSeat_ = seat;
                partnerUntil_ = now_ + 1.9f / speed_;
                sound(ui::Sfx::Button);
                if (seat != 0) say(seat, e.amount >= 40 ? pick(kOppKirk[oppSeat_], rng_) : pick(kOppYirmi[oppSeat_], rng_), true);
                else if (rng_.chance(0.5f)) say(OPP_SEAT, pick(kOppYouMarriage[oppSeat_], rng_, names_[0]));
                if (seat == 0 && e.amount >= 40) noteAchievement("66_kirk"); // Başarımlar: the koz marriage
                break;
            }
            case E::Play: onPlayed(seat, e.card, seat != 0 || aiSeat_); break;
            case E::TrickWon:
                onTrickWon(seat, e.cards);
                drawSeat_ = -1;
                if (e.amount >= 20) hud_.toast(e.text, ui::pal::TextLight, 1.8f);
                break;
            case E::Draw:
                // the drawn cards stay on the stock until the trick has gone to its pile
                drawAt_ = now_ + 1.75f / speed_;
                if (seat != 0) drawSeat_ = seat;
                break;
            case E::StockOut: hud_.toast(e.text, ui::pal::Highlight, 3.0f); break;
            case E::HandEnd: {
                log_ = e.text;
                lastEnd_ = e.text;
                hud_.toast(e.text, e.seat == 0 ? Color{160, 236, 160, 255} : ui::pal::Bad, 4.0f);
                sound(ui::Sfx::Win);
                const altmisalti::HandRecord& h = g_.sheet().back();
                if (h.closer == 0 && h.winner == 0) noteAchievement("66_kapat"); // Başarımlar: closed and made it
                if (e.seat == 1) {
                    say(OPP_SEAT, pick(kOppWon[oppSeat_], rng_), true);
                    if (ctx_.characters) ctx_.characters->react(chr(OPP_SEAT), 1, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                } else if (h.closer == 1) {
                    say(OPP_SEAT, pick(kOppLost[oppSeat_], rng_), true);
                    if (ctx_.characters) ctx_.characters->react(chr(OPP_SEAT), 2, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                } else if (rng_.chance(0.6f)) {
                    say(OPP_SEAT, pick(kOppLost[oppSeat_], rng_));
                    if (ctx_.characters) ctx_.characters->react(chr(OPP_SEAT), 2, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                }
                if (h.closer == 0 && h.winner == 1 && rng_.chance(0.7f)) say(OPP_SEAT, pick(kOppYouFail[oppSeat_], rng_, names_[0]));
                // the drawn / shown cards are where they belong now
                drawAt_ = -1.f;
                partner_ = -1;
                shownStock_ = g_.stockCards();
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            default: break;
            }
        }
    }

    // The hand as the table shows it: the cards still lying on the stock (drawn, not yet taken) and a marriage's other
    // half on show are not in it yet.
    std::vector<int> handOf(int seat) const override {
        const int p = playerOf(seat);
        if (p < 0 || g_.stage() == altmisalti::Stage::NotStarted) return {};
        std::vector<int> h;
        for (int c : g_.hand(p)) {
            if (std::find(shownStock_.begin(), shownStock_.end(), c) != shownStock_.end()) continue;
            if (c == partner_ && partnerSeat_ == seat) continue;
            h.push_back(c);
        }
        return h;
    }
    int actor() const override {
        if (g_.stage() != altmisalti::Stage::Playing) return -1;
        return seatOf(g_.current());
    }
    bool humanTurn() const override { return g_.stage() == altmisalti::Stage::Playing && g_.current() == 0; }
    std::vector<int> playableCards() const override {
        if (!humanTurn()) return {};
        std::vector<int> v = g_.legalCards(0);
        if (g_.canExchange(0)) v.push_back(g_.faceUpTrump()); // (a click on the koz card exchanges the 9)
        return v;
    }
    std::vector<int> clickable() const override {
        std::vector<int> v = handOf(0);
        if (humanTurn() && g_.canExchange(0)) v.push_back(g_.faceUpTrump());
        return v;
    }
    void playHumanCard(int card) override {
        if (card == g_.faceUpTrump() && g_.canExchange(0)) {
            report(g_.exchangeNine(0));
            return;
        }
        report(g_.playCard(0, card));
    }
    float botDelay(int seat) const override {
        (void)seat;
        return g_.ledCard() >= 0 ? 0.7f : 0.95f;
    }
    void botStep(int seat) override {
        const int p = playerOf(seat);
        if (p < 0 || !bots_[(size_t)p]) return;
        const altmisalti::BotAction a = bots_[(size_t)p]->next(g_, p);
        if (!altmisalti::applyBotAction(g_, p, a).ok) altmisalti::applyBotAction(g_, p, altmisalti::fallbackAction(g_, p));
    }
    bool extraBusy() const override { return drawAt_ >= 0.f || (partner_ >= 0 && now_ < partnerUntil_); }
    std::vector<std::pair<int, r3d::CardPose>> dealtExtras() const override {
        if (shownStock_.empty()) return {};
        return {{shownStock_[0], layPose(kozPose(false, 0))}}; // the 13th card, turned up
    }
    int deckRemaining() const override { return std::max(0, (int)shownStock_.size() - 1); }
    void layoutExtra() override {
        if (drawAt_ >= 0.f && now_ >= drawAt_) { // the drawn cards go to the hands now
            drawAt_ = -1.f;
            const std::vector<int> before = shownStock_;
            shownStock_ = g_.stockCards();
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
        if (partner_ >= 0 && now_ >= partnerUntil_) partner_ = -1;
        if (partner_ >= 0) placeExtra(partner_, layPose(shownPartnerPose(partnerSeat_)));
        if (shownStock_.empty()) return;
        const bool closed = g_.closed() && drawAt_ < 0.f;
        const int n = (int)shownStock_.size();
        // the face-down stock: while dealing it is the dealer's deck (under the cards being dealt)
        for (int i = 1; i < n; ++i) {
            const int c = shownStock_[(size_t)i];
            if (dealing()) placeExtra(c, deckSlot(seatOf(g_.dealer()), i - 1), true);
            else placeExtra(c, layPose(stockSlot(i - 1)));
        }
        const int koz = shownStock_[0];
        placeExtra(koz, layPose(kozPose(closed, n - 1)), true, kozLead_ && closed ? w3d::BOT_TAKE_LEAD : 0.f);
        if (closed) kozLead_ = false;
        const bool canSwap = humanTurn() && g_.canExchange(0) && koz == g_.faceUpTrump() && !aiSeat_ && !replay_;
        cards_.setGlow(koz, canSwap ? (hover_ == koz ? 1.f : 0.45f) : 0.f);
        if (canSwap && hover_ == koz) cards_.setLift(koz, 0.008f);
    }
    std::vector<GameButton> gameButtons() const override {
        const bool can = humanTurn() && !tableBusy();
        const Hint* h = activeHint();
        return {{"Kapat", can && g_.canClose(0), h && h->choice == 1}, {"Kozu Al", can && g_.canExchange(0), h && h->choice == 2}};
    }
    void onHudClick(int id) override {
        if (!humanTurn() || tableBusy()) return;
        sound(ui::Sfx::Button);
        if (id == 1) report(g_.closeStock(0));
        else if (id == 2) report(g_.exchangeNine(0));
        pumpEngine();
    }
    size_t decisionStamp() const override { return g_.actionLog().size() + 100000u * (size_t)g_.handIndex(); }
    bool computeHint(Hint& h) override {
        if (actor() != 0 || !bots_[0]) return false;
        const altmisalti::BotAction a = bots_[0]->next(g_, 0);
        switch (a.kind) {
        case altmisalti::BotAction::Kind::Exchange:
            h.choice = 2;
            h.card = g_.faceUpTrump();
            h.text = "İpucu: koz dokuzuyla " + kart::cardAccusativeTR(g_.faceUpTrump()) + " al";
            break;
        case altmisalti::BotAction::Kind::Close:
            h.choice = 1;
            h.text = "İpucu: desteyi kapat";
            break;
        case altmisalti::BotAction::Kind::Play:
            h.card = a.card;
            h.text = "İpucu: " + kart::cardAccusativeTR(a.card) + (g_.ledCard() >= 0 ? " at" : " aç");
            if (g_.marriageValue(0, a.card) > 0) h.text += " (evlilik " + std::to_string(g_.marriageValue(0, a.card)) + ")";
            break;
        }
        return true;
    }
    std::vector<std::pair<int, int>> trickOnTable() const override {
        if (g_.stage() != altmisalti::Stage::Playing || g_.ledCard() < 0) return {};
        return {{seatOf(g_.leader()), g_.ledCard()}};
    }
    std::array<std::vector<int>, 4> wonPiles() const override {
        std::array<std::vector<int>, 4> w;
        for (int p = 0; p < 2; ++p) w[(size_t)seatOf(p)] = g_.wonCards(p);
        return w;
    }

private:
    void report(const altmisalti::ActionResult& r) {
        if (r.ok) return;
        hud_.toast(r.error, ui::pal::Bad, 2.6f);
        sound(ui::Sfx::Error);
    }
    altmisalti::Game g_;
    std::array<std::unique_ptr<altmisalti::Bot>, 2> bots_;
    std::vector<int> shownStock_;   // the stock as it lies on the table (bottom = the koz card)
    float drawAt_ = -1.f;           // the cards drawn after the last trick leave the stock then
    bool kozLead_ = false;          // the opponent closes: the koz card waits for his fingers
    int drawSeat_ = -1;
    int partner_ = -1, partnerSeat_ = -1; // a declared marriage's other half, on show until partnerUntil_
    float partnerUntil_ = 0.f;
    int lastTrump_ = 0;
    std::string lastEnd_;
};

} // namespace

std::unique_ptr<TableGame> makeAltmisaltiTable() { return std::make_unique<AltmisaltiTable>(); }

} // namespace app
