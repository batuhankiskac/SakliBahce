// Konken (two decks and four jokers; per, seri, işlemek, the discard that must be used) at our table: engine
// konken::Game, bots konken::Bot, the shared card table. The player's fan is laid out here (gameLaysOwnHand): the
// player's own order (drag a card along the fan), selection (click / Space) for "Aç", dragging a card onto a meld on the
// table (işle; onto a joker: take it) or onto the discard pile (at). The melds lie face up in rows in front of their
// owner, readable from our seat; the stock and the discard pile are in the middle.
#include "app/CardTable.h"
#include "core/Konken.h"
#include "core/KonkenBot.h"
#include "ui/Common.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace app {

namespace {

namespace kk = konken;

std::string fill(std::string t, const std::string& v) {
    size_t p;
    while ((p = t.find("{v}")) != std::string::npos) t.replace(p, 3, v);
    return t;
}

// 1 Rıza: calm, 2 Mahmut: loud, 3 Nuri: grumpy
const char* const kOpened[4][2] = {
    {},
    {"Bismillah, açtım.", "Sabreden derviş, açtım işte."},
    {"Açtım abi, {v}! Hadi bakalım!", "{v} ile açtım, kaçın!"},
    {"Açtım. {v}. Bizim zamanımızda daha erken açılırdı.", "Hıh, {v}. Açtık."},
};
const char* const kFinished[4][2] = {
    {},
    {"Elhamdülillah, bitti.", "Hayırlısı, el bitti."},
    {"Bitti abi! Yazın yazın!", "Ben demiştim, bu el benim!"},
    {"Bitti. Gençler ders alsın.", "Hıh, işte böyle bitirilir."},
};
const char* const kKonken[4] = {"", "Konken! Allah'ın izniyle elden.", "KONKEN! Elden abi, elden! Çift yazın!",
                                "Konken. Bizim zamanımızda buna usta işi derlerdi."};
// Konken son kalan: the one who burned, and the others about him (1 Rıza, 2 Mahmut, 3 Nuri)
const char* const kBurned[4] = {"", "Kısmet değilmiş. Ben kenardan seyrederim.", "Yandım abi! Yandım! Bir çay söyleyin bari.",
                                "Hıh. Yandık. Bari seyredip akıl vereyim."};
const char* const kOtherBurned[4] = {"", "Geçmiş olsun, {v}.", "{v} yandı! Kalk bakalım, sıra bizde!", "{v} de yandı. Kalanlar sağ olsun."};
const char* const kYouBurned[4] = {"", "Geçmiş olsun evladım, bir dahakine.", "Yandın abi! Seyret de öğren!",
                                   "Yandın. Bizim zamanımızda bu kadar yazılmazdı."};

const char* const kTookMine[4][2] = {
    {},
    {"Attığın {v} işime yaradı, sağ ol.", "Bu {v} tam bana göreydi."},
    {"Sağ ol, {v} bana lazımdı!", "Ooo {v}! Kısmetmiş!"},
    {"{v}'ı aldım. Dikkat et biraz.", "Hıh, {v}. Teşekkürler."},
};

// ---- the layout (layout space: our okey table, seat 0 at +Z)
constexpr float MELD_STEP = 0.020f;   // a card of a meld over the previous one (the corner index stays visible)
constexpr float MELD_GAP = 0.014f;    // between two melds of a row
constexpr float ROW_STEP = 0.058f;    // rows overlap: the next row covers the bottom of the one before
constexpr Vector3 STOCK_POS{-0.085f, w3d::TABLE_Y, 0.0f};
constexpr Vector3 DISCARD_POS{0.075f, w3d::TABLE_Y, 0.0f};

// Each seat's meld area: the left edge of the first row's cards (x), the first row's centre (z), the width and the row
// direction (rows always come toward the player, so a later row lies over the bottom of the one before it).
struct MeldZone {
    float x0, z0, width;
    int maxRows;
};
constexpr MeldZone kZones[4] = {
    {-0.27f, 0.170f, 0.54f, 3},  // the player: in front of the fan
    {0.165f, -0.27f, 0.26f, 10}, // Hacı Rıza (right): a column of short rows
    {-0.27f, -0.33f, 0.54f, 3},  // Kel Mahmut (across)
    {-0.425f, -0.27f, 0.26f, 10} // Emekli Nuri (left)
};

struct MeldRect {
    int meld = -1;
    float x0 = 0.f, x1 = 0.f, z0 = 0.f, z1 = 0.f; // layout space
};

// "Karo 7-8-9", "Kupa 10-V-K-P-A", "7'li grup (3)"; a joker shows as "J".
std::string meldText(const kk::Meld& m) {
    if (m.kind == kk::MeldKind::Set) return kart::rankNameTR(m.rank) + " grubu (" + std::to_string(m.size()) + ")";
    std::string t = std::string(kart::suitNameTR(m.suit)) + " ";
    for (int i = 0; i < m.size(); ++i) {
        const int r = m.rankAt(i);
        t += (i ? "-" : "") + (kk::isJoker(m.cards[(size_t)i]) ? std::string("J") : kart::rankIndexTR(r == 1 ? 14 : r));
    }
    return t;
}

int sortKey(int c) {
    if (kk::isJoker(c)) return 1000 + c;
    static const int suitOrder[4] = {0, 1, 3, 2}; // maça, kupa, sinek, karo
    return (suitOrder[kk::suitOf(c)] * 16 + kk::rankOf(c)) * 2 + c / kk::NUM_FACES;
}

class KonkenTable final : public CardTableBase {
public:
    // (son kalan) A match left part way: the burned regulars come back to the table and the room leaves the fast
    // watching speed, before the base puts the cards away.
    void shutdown() override {
        for (int s = 1; s < 4; ++s)
            if (ctx_.characters) ctx_.characters->setSeatOut(s, false, true);
        if (fast_) setFast(false);
        CardTableBase::shutdown();
    }
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        kk::Rules rules;
        rules.openMin = std::clamp(st.konkenOpen, 30, 120);
        rules.limit = std::clamp(st.konkenLimit, 51, 501);
        rules.lastStanding = st.konkenLastStanding; // Konken bitiş: the burned leave, the last one left wins
        g_ = kk::Game(rules);
        // (son kalan) everyone back at the table, at the normal speed
        for (int s = 1; s < 4; ++s)
            if (ctx_.characters) ctx_.characters->setSeatOut(s, false);
        outChoice_ = 0;
        setFast(false);
        names_ = names;
        for (int s = 0; s < 4; ++s) {
            g_.setPlayer(s, names[(size_t)s], s == 0);
            const kk::BotLevel lv = s == 0 ? kk::BotLevel::Kurt : (kk::BotLevel)std::clamp(level_, 0, 2);
            bots_[(size_t)s] = std::make_unique<kk::Bot>(lv, seed * 4 + (uint64_t)s + 0x6B6Eull);
            bots_[(size_t)s]->setStyle(kk::BotStyle::forSeat(s));
        }
        rng_.reseed(seed ^ 0x6B6F6EULL);
        replay_ = false;
        order_.clear();
        selected_.clear();
        resetTable();
        g_.startMatch(seed);
        pumpEngine();
    }
    void startNextHand() override {
        if (g_.stage() != kk::Stage::HandOver) return;
        g_.startNextHand();
        pumpEngine();
    }
    void setLevel(int level) override {
        CardTableBase::setLevel(level);
        for (int s = 1; s < 4; ++s)
            if (bots_[(size_t)s]) bots_[(size_t)s]->setLevel((kk::BotLevel)level_);
    }
    bool handOver() const override { return g_.stage() == kk::Stage::HandOver || g_.stage() == kk::Stage::MatchOver; }
    // (son kalan) the player burned and watches the rest at speed: App moves the sheets on by itself
    bool humanInHand() const override { return g_.sheet().empty() || g_.sheet().back().playing[0]; }
    bool spectating() const override { return g_.isOut(0) && outChoice_ == 1 && g_.stage() != kk::Stage::MatchOver; }
    void setAnimationSpeed(float s) override {
        baseSpeed_ = s;
        CardTableBase::setAnimationSpeed(fast_ ? s * FAST : s);
    }
    bool matchOver() const override { return g_.stage() == kk::Stage::MatchOver; }
    bool mouseBusy() const override { return myHover_ >= 0 || held_ >= 0 || hud_.mouseOverHud() || pileHover_ != 0; }

    std::string scoreTitle() const override {
        if (g_.stage() == kk::Stage::NotStarted) return "";
        return "Konken \xC2\xB7 " + std::to_string(g_.handIndex() + 1) + ". el";
    }
    std::vector<std::string> scoreLines() const override {
        std::vector<std::string> v;
        for (int s = 0; s < 4; ++s)
            v.push_back(names_[(size_t)s] + " ....... " + std::to_string(g_.total(s)) + (g_.isOut(s) ? " (yandı)" : ""));
        return v;
    }

    void update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput, bool aiSeat) override {
        syncOrder();
        if (!replay_ && humanInput && !aiSeat && ctx_.renderer) myMouse(mouse);
        else {
            held_ = -1;
            heldDrag_ = false;
            myHover_ = -1;
            pileHover_ = 0;
        }
        if (!replay_ && humanInput && !aiSeat) myKeys();
        CardTableBase::update(dt, cam, mouse, humanInput, aiSeat);
    }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        const bool inHand = g_.stage() == kk::Stage::Draw || g_.stage() == kk::Stage::Play;
        for (int s = 1; s < 4; ++s) {
            heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
            r3d::GameHud::Plate& p = plates[(size_t)s];
            p.show = true;
            p.name = names_[(size_t)s];
            p.label = "Toplam";
            p.value = std::to_string(g_.total(s));
            p.turn = inHand && g_.current() == s && !tableBusy();
            if (g_.isOut(s)) { // (son kalan) burned: he watches from behind his chair
                p.badges.push_back({"Yandı", Color{168, 42, 34, 255}});
                continue;
            }
            if (inHand) {
                p.badges.push_back({std::to_string(g_.handSize(s)) + " kâğıt", Color{70, 60, 50, 255}});
                if (g_.opened(s)) p.badges.push_back({"Açtı", Color{52, 110, 64, 255}});
            }
            if (g_.total(s) >= g_.rules().limit * 3 / 4) p.badges.push_back({"Yanıyor", Color{168, 42, 34, 255}});
        }
        hud_.plates(r, heads, plates);
        // the stock's count and, while choosing, what the selection is worth
        if (inHand && !dealing()) {
            hud_.label3D(r, layPoint({STOCK_POS.x, w3d::TABLE_Y + 0.02f, STOCK_POS.z + 0.075f}),
                         "Deste " + std::to_string(g_.stockSize()), ui::pal::TextLight, 14.f);
            if (g_.takenCard() >= 0 && g_.current() == 0 && !aiSeat)
                hud_.label3D(r, layPoint({DISCARD_POS.x, w3d::TABLE_Y + 0.02f, DISCARD_POS.z + 0.075f}),
                             kk::cardNameTR(g_.takenCard()) + " kullanılmalı", ui::pal::Highlight, 14.f);
        }

        // the meld under the mouse, by name (and, while a card is dragged over it, whether it goes there)
        if (inHand && !aiSeat && !replay_) {
            Vector3 lp{};
            kk::Side side = kk::Side::Auto;
            const int m = feltPoint(mouse, lp) ? meldAt(lp, &side) : -1;
            if (m >= 0) {
                const kk::Meld& md = g_.table()[(size_t)m];
                std::string t = meldText(md) + "  \xC2\xB7  " + names_[(size_t)std::max(0, md.owner)];
                Color c = ui::pal::TextLight;
                if (held_ >= 0 && heldDrag_) {
                    const bool sw = kk::swapIndex(md, held_) >= 0, add = kk::canAdd(md, held_);
                    t = sw ? "Jokeri al: " + kk::cardNameTR(held_) : add ? "İşle: " + kk::cardNameTR(held_) : kk::cardNameTR(held_) + " buraya gitmez";
                    c = sw || add ? Color{160, 236, 160, 255} : ui::pal::Bad;
                }
                MeldLayout L;
                computeMelds(L);
                for (const MeldRect& rc : L.rects)
                    if (rc.meld == m)
                        hud_.label3D(r, layPoint({(rc.x0 + rc.x1) * 0.5f, w3d::TABLE_Y + 0.02f, rc.z0 - 0.012f}), t, c, 15.f);
            }
        }

        Color sc = ui::pal::TextLight;
        const int a = actor();
        const char* ended = g_.stage() == kk::Stage::HandOver ? "El bitti" : g_.stage() == kk::Stage::MatchOver ? "Parti bitti" : nullptr;
        std::string st;
        if (!ended && !dealing() && g_.isOut(0)) // (son kalan) burned: he watches the rest
            st = "Yandın: kalanları izliyorsun" + std::string(fast_ ? " (hızlı)" : "") +
                 (a > 0 ? "  \xC2\xB7  " + names_[(size_t)a] + " düşünüyor…" : std::string());
        else
            st = statusLine(ended, dealing(), "Kâğıtlar dağıtılıyor…", a, aiSeat, sc, [&]() -> std::string {
                if (g_.stage() == kk::Stage::Draw) return g_.mustDrawStock() ? "Desteden çek" : "Desteden ya da yerden bir kâğıt çek";
                if (g_.takenCard() >= 0) return "Yerden aldığın " + kk::cardAccusativeTR(g_.takenCard()) + " masada kullan ya da geri ver";
                return g_.opened(0) ? "Per indir, işle ya da bir kâğıt at" : "Aç ya da bir kâğıt at";
            });
        std::vector<std::pair<std::string, Color>> parts;
        if (inHand && !g_.isOut(0)) {
            if (!g_.opened(0)) {
                const int v = selectionValue();
                parts.push_back({"Açma " + std::to_string(v) + " / " + std::to_string(g_.openNeed()),
                                 v >= g_.openNeed() ? Color{160, 236, 160, 255} : Color{238, 198, 112, 255}});
            } else {
                parts.push_back({"Açtın", Color{160, 236, 160, 255}});
            }
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
            parts.push_back({"Elde " + std::to_string(g_.pointsInHand(0)), Color{226, 216, 196, 255}});
        }
        hud_.status(st, sc, a == 0 && !aiSeat && !replay_, parts, aiSeat && !replay_);
        drawMyButtons(mouse, aiSeat);
        if (outPanel()) // (son kalan) the player burned: watch the rest at speed, or straight to the result
            hud_.panel(mouse, "Yandın, masadan kalktın (" + std::to_string(g_.place(0)) + ". oldun)",
                       "Kalan elleri " + std::to_string(g_.activeCount()) + " kişi oynuyor: hızlı izleyebilir ya da sonuca geçebilirsin",
                       {{"Hızlı izle", true, false, "masayı seyret"}, {"Sonuca geç", true, false, "kalanları hemen oyna"}}, 2, 300.f, 170.f);
        hud_.toasts();
    }

    bool humanHandScore(int& score) const override {
        // "En büyük bitiş": what the others wrote in a hand the player finished
        if (g_.sheet().empty() || g_.sheet().back().finisher != 0) return false;
        score = 0;
        for (int s = 1; s < 4; ++s) score += g_.sheet().back().points[(size_t)s];
        return true;
    }

    bool saveState(std::vector<std::string>& lines) const override {
        lines.clear();
        for (const kk::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        bool ok = true;
        for (const std::string& l : lines) {
            kk::LoggedAction a;
            if (!kk::LoggedAction::decode(l, a) || !g_.replay(a)) {
                ok = false;
                break;
            }
        }
        g_.drainEvents();
        order_.clear();
        selected_.clear();
        syncOrder();
        log_.clear();
        restoreView();
        // (son kalan) the ones who burned already stand behind their chairs; a burned player chooses again
        for (int s = 1; s < 4; ++s)
            if (ctx_.characters) ctx_.characters->setSeatOut(s, g_.isOut(s) && g_.stage() != kk::Stage::MatchOver, true);
        outChoice_ = 0;
        return ok;
    }

    // ---- sheet()'s parts: the hand that just ended (headline, who burned, the two rows) ...
    void sheetLastHand(ui::SheetModel& m, const kk::HandRecord& r, bool aiMode, bool elim) const {
        const std::string who = r.finisher == 0 ? std::string(aiMode ? "Yapay zeka" : "Sen")
                                                : r.finisher > 0 ? names_[(size_t)r.finisher] : std::string();
        if (r.finisher < 0) m.headline = "Deste bitti, kimse bitiremedi";
        else if (r.konken) m.headline = "Konken! " + who + (r.finisher == 0 && !aiMode ? " elden bitirdin" : " elden bitirdi");
        else m.headline = who + (r.finisher == 0 && !aiMode ? " eli bitirdin" : " eli bitirdi");
        m.tagline = r.konken ? "elden bitiş: herkesin yazdığı iki kat" : "açmayan " + std::to_string(g_.rules().unopenedPoints) + " yazar";
        m.taglineRed = r.konken;
        { // (son kalan) who burned with this hand
            std::string burnt;
            for (int s = 0; s < 4; ++s)
                if (r.burned[(size_t)s]) burnt += (burnt.empty() ? "" : ", ") + (s == 0 && !aiMode ? std::string("sen") : names_[(size_t)s]);
            if (!burnt.empty()) {
                m.tagline = burnt + " yandı" + (elim && g_.stage() != kk::Stage::MatchOver ? ", masadan kalkıyor" : "");
                if (r.burned[0] && elim && !aiMode && g_.stage() != kk::Stage::MatchOver) m.tagline = "Yandın! Kalanları izleyebilirsin";
                m.taglineRed = true;
            }
        }
        m.starCol = r.finisher;
        m.rowLabels = {"Elde", "Bu el"};
        m.cells.assign(2, std::vector<std::string>(4));
        m.red.assign(2, std::vector<bool>(4, false));
        for (int s = 0; s < 4; ++s) {
            m.cells[0][(size_t)s] = s == r.finisher ? std::string("bitti")
                                    : !r.opened[(size_t)s] ? std::string("açmadı")
                                                           : std::to_string(r.cardsLeft[(size_t)s]) + " kâğıt";
            m.cells[1][(size_t)s] = std::to_string(r.points[(size_t)s]);
            m.red[0][(size_t)s] = !r.opened[(size_t)s];
            m.red[1][(size_t)s] = r.points[(size_t)s] >= 50;
            if (!r.playing[(size_t)s]) { // (son kalan) burned earlier: not at the table
                m.cells[0][(size_t)s] = "masada yok";
                m.cells[1][(size_t)s] = "—";
                m.red[0][(size_t)s] = m.red[1][(size_t)s] = false;
            } else if (r.burned[(size_t)s]) {
                m.cells[0][(size_t)s] = "yandı";
                m.red[0][(size_t)s] = true;
            }
        }
    }
    // ... and the final standings (places, ties, each one's line, the result)
    void sheetStandings(ui::SheetModel& m, bool aiMode, bool elim) const {
        const auto& rows = g_.sheet();
        m.matchHeader = "Parti Bitti";
        m.playedLine = std::to_string(rows.size()) + " el oynandı";
        const std::array<int, 4> rk = g_.ranking();
        for (int i = 0; i < 4; ++i) {
            m.ranking.push_back(rk[(size_t)i]);
            // ties share a place; son kalan: only two who burned in the same hand with the same total
            const int a = rk[(size_t)i], b = i > 0 ? rk[(size_t)i - 1] : -1;
            const bool tie = b >= 0 && g_.total(a) == g_.total(b) && (!elim || (g_.isOut(a) && g_.isOut(b) && burnHand(a) == burnHand(b)));
            m.rank.push_back(tie ? m.rank.back() : i + 1);
        }
        for (int s = 0; s < 4; ++s) {
            int fin = 0, kon = 0;
            for (const kk::HandRecord& r : rows) {
                fin += r.finisher == s ? 1 : 0;
                kon += r.finisher == s && r.konken ? 1 : 0;
            }
            const int bh = burnHand(s);
            m.rankSub.push_back(std::to_string(fin) + " el bitirdi" + (kon ? "  \xC2\xB7  " + std::to_string(kon) + " konken" : std::string()) +
                                (bh >= 0 ? "  \xC2\xB7  " + std::to_string(bh + 1) + ". elde yandı"
                                         : g_.total(s) >= g_.rules().limit ? "  \xC2\xB7  yandı" : ""));
        }
        const int lead = rk[0];
        m.humanWon = lead == 0 && m.rank.size() > 1 && m.rank[1] != 1;
        if (m.humanWon) {
            m.resultTitle = aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!";
            m.resultSub = elim ? "Masada son kalan sensin (" + std::to_string(g_.total(0)) + ")."
                               : "Toplam " + std::to_string(g_.total(0)) + " ile en az yazan sensin.";
        } else {
            m.resultTitle = "Bu sefer olmadı, bir parti daha?";
            m.resultSub = (elim ? "Son kalan: " : "Kazanan: ") + names_[(size_t)lead] + " (" + std::to_string(g_.total(lead)) + ")";
        }
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const auto& rows = g_.sheet();
        for (int s = 0; s < 4; ++s)
            m.columns.push_back(s == 0 && aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[(size_t)s]);
        m.humanCol = 0;
        m.title = std::to_string((int)rows.size()) + ". El Sonucu";
        const bool elim = g_.rules().lastStanding;
        m.corner = "konken: " + std::to_string(g_.rules().limit) + " olan yanar" + (elim ? ", son kalan kazanır" : "");
        if (!rows.empty()) sheetLastHand(m, rows.back(), aiMode, elim);
        for (const kk::HandRecord& r : rows) {
            m.historyLabels.push_back(std::to_string(r.index + 1) + ". el" + (r.konken ? " (konken)" : ""));
            std::vector<std::string> row;
            for (int s = 0; s < 4; ++s)
                row.push_back(!r.playing[(size_t)s] ? std::string("") : r.finisher == s ? std::string("—") : std::to_string(r.points[(size_t)s]));
            m.history.push_back(row);
        }
        int best = 1 << 30;
        for (int s = 0; s < 4; ++s)
            if (g_.active(s)) best = std::min(best, g_.total(s)); // (son kalan: among the ones still playing)
        for (int s = 0; s < 4; ++s) {
            m.totals.push_back(std::to_string(g_.total(s)));
            if (g_.active(s) && g_.total(s) == best) m.leaders.push_back(s);
        }
        m.last = g_.stage() == kk::Stage::MatchOver;
        int top = 0;
        for (int s = 0; s < 4; ++s) top = std::max(top, g_.total(s));
        m.note = m.last ? "" : "Düşük puan iyidir  \xC2\xB7  " + std::to_string(g_.rules().limit) + " olan yanar (en yüksek " +
                                   std::to_string(top) + ")";
        if (!m.last && elim && g_.activeCount() < 4) { // (son kalan)
            int topIn = 0;
            for (int s = 0; s < 4; ++s)
                if (g_.active(s)) topIn = std::max(topIn, g_.total(s));
            m.note = "Masada " + std::to_string(g_.activeCount()) + " kişi kaldı  \xC2\xB7  " + std::to_string(g_.rules().limit) +
                     " olan yanar (kalanlarda en yüksek " + std::to_string(topIn) + ")";
        }
        sheetStandings(m, aiMode, elim);
        return m;
    }

    bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const override {
        if (outPanel()) { // (son kalan) tools/tables_check takes "Sonuca geç" (or "Hızlı izle" when told so)
            const std::vector<Rectangle>& pr = hud_.panelRects();
            const size_t k = debugWatch_ ? 0 : 1;
            if (k >= pr.size()) return false;
            out = {pr[k].x + pr[k].width * 0.5f, pr[k].y + pr[k].height * 0.5f};
            return true;
        }
        if (tableBusy() || actor() != 0 || aiSeat_ || replay_) return false;
        // the decision Kurt would make, done by the mouse: the stock / the discard pile, a button, or a card
        if (!plan()) return false;
        const kk::BotAction& a = *debugPlan_;
        auto proj = [&](Vector3 p, Vector2& o) { return r.projectToVirtual(p, o); };
        switch (a.kind) {
        case kk::BotAction::Kind::DrawStock:
            return proj(cards_.pose(g_.stockSize() > 0 ? stockTop() : 0).pos, out);
        case kk::BotAction::Kind::TakeDiscard:
            return proj(cards_.pose(g_.discardTop()).pos, out);
        case kk::BotAction::Kind::ReturnDiscard:
        case kk::BotAction::Kind::Lay: {
            // select the cards first (a click each), then the button
            if (a.kind == kk::BotAction::Kind::Lay) {
                for (const auto& mv : a.melds)
                    for (int c : mv)
                        if (!selected_.count(c)) return cardPoint(r, c, out);
                for (int c : selected_) {
                    bool in = false;
                    for (const auto& mv : a.melds) in = in || std::find(mv.begin(), mv.end(), c) != mv.end();
                    if (!in) return cardPoint(r, c, out); // (unselect the others)
                }
            }
            const std::vector<Rectangle>& br = hud_.buttonRects();
            const size_t idx = a.kind == kk::BotAction::Kind::Lay ? 1 : 2;
            if (idx >= br.size()) return false;
            out = {br[idx].x + br[idx].width * 0.5f, br[idx].y + br[idx].height * 0.5f};
            return true;
        }
        case kk::BotAction::Kind::Add:
        case kk::BotAction::Kind::Swap:
        case kk::BotAction::Kind::Discard:
            // a drag: tables_check presses on the card, moves and lets go (debugDragTarget gives the drop point)
            return cardPoint(r, a.card, out);
        }
        return false;
    }

protected:
    bool gameLaysOwnHand() const override { return true; }
    // (son kalan) the bots wait while the burned player chooses (the panel)
    bool extraBusy() const override { return outPanel(); }
    std::vector<int> clickable() const override { return {}; } // (the base's hover / click: the fan is ours)

    int replayLine(const std::string& line) override {
        kk::LoggedAction a;
        if (!kk::LoggedAction::decode(line, a)) return -1;
        if (a.kind == kk::LogKind::NextHand) {
            if (g_.stage() == kk::Stage::HandOver) return 0;
            return g_.stage() == kk::Stage::MatchOver ? -1 : 1;
        }
        if (!g_.replay(a)) return -1;
        pumpEngine();
        return 1;
    }

    void pumpEngine() override {
        for (const kk::GameEvent& e : g_.drainEvents()) {
            using E = kk::EvType;
            const bool bot = e.seat != 0 || aiSeat_ || replay_;
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::HandStart:
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                order_.clear();
                selected_.clear();
                // (son kalan) the ones who burned get up from the table now (the sheet is over); the table plays on
                for (int s = 1; s < 4; ++s)
                    if (ctx_.characters && g_.isOut(s)) ctx_.characters->setSeatOut(s, true);
                break;
            case E::Deal: dealFrom(g_.dealer()); break;
            case E::TurnStart:
                if (e.seat == 0) kbIndex_ = std::clamp(kbIndex_, 0, std::max(0, (int)order_.size() - 1));
                break;
            case E::DrawStock:
            case E::TakeDiscard: {
                const bool stock = e.type == E::DrawStock;
                const Vector3 from = layPoint(stock ? STOCK_POS : DISCARD_POS);
                if (e.seat != 0) {
                    if (ctx_.characters) ctx_.characters->reach(e.seat, from, 0);
                    r3d::CardPose p = cards_.pose(e.card);
                    p.pos.y += 0.12f; // (on its way up to the fan: Characters' fan pose takes over in flight)
                    p.pos = Vector3Add(p.pos, Vector3Scale(w3d::SEAT_DIR[e.seat], 0.25f));
                    cards_.place(e.card, p, true, w3d::BOT_TAKE_LEAD, 0.05f);
                    soundAt(ui::Sfx::CardSlide, (w3d::BOT_TAKE_LEAD + 0.1f) / speed_);
                } else {
                    soundAt(ui::Sfx::CardSlide, 0.05f / speed_);
                    if (!stock) handCueCard(e.card, 0.f);
                }
                if (!stock && e.seat != 0 && !g_.discards().empty()) {
                    // was it the player's card?
                    for (auto it = g_.discards().rbegin(); it != g_.discards().rend(); ++it)
                        if (it->card == e.card) {
                            if (it->seat == 0 && rng_.chance(0.6f))
                                say(e.seat, fill(kTookMine[e.seat][rng_.range(2)], kk::cardNameTR(e.card)));
                            break;
                        }
                }
                if (e.seat == 0 && !stock) hud_.toast(e.text + ": bu tur masada kullanmalısın", ui::pal::Highlight, 2.6f);
                break;
            }
            case E::ReturnDiscard:
                hud_.toast(e.text, ui::pal::TextLight, 2.0f);
                if (e.seat != 0 && ctx_.characters) ctx_.characters->reach(e.seat, layPoint(DISCARD_POS), 1);
                soundAt(ui::Sfx::CardPlace, 0.4f / speed_);
                break;
            case E::Open:
            case E::LayMelds: {
                hud_.toast(e.text, e.type == E::Open ? ui::pal::Highlight : ui::pal::TextLight, 2.4f);
                std::vector<int> cs;
                for (int mi : e.melds)
                    if (mi >= 0 && mi < (int)g_.table().size())
                        for (int c : g_.table()[(size_t)mi].cards) cs.push_back(c);
                if (bot && e.seat != 0 && !cs.empty() && ctx_.characters)
                    ctx_.characters->playCard(e.seat, layPoint(meldPose(cs.front()).pos), false);
                const float lead = e.seat != 0 && ctx_.characters ? w3d::BOT_GIVE_LEAD : 0.f;
                for (size_t k = 0; k < cs.size(); ++k) cards_.place(cs[k], meldPose(cs[k]), true, lead + 0.035f * (float)k, 0.05f);
                soundAt(ui::Sfx::CardSlap, (lead + 0.35f) / speed_);
                if (e.type == E::Open && e.seat > 0 && rng_.chance(0.55f))
                    say(e.seat, fill(kOpened[e.seat][rng_.range(2)], std::to_string(e.amount)));
                if (e.seat == 0) selected_.clear();
                break;
            }
            case E::AddToMeld:
            case E::SwapJoker: {
                hud_.toast(e.text, ui::pal::TextLight, 2.0f);
                const float lead = e.seat != 0 && ctx_.characters ? w3d::BOT_GIVE_LEAD : 0.f;
                if (e.seat != 0 && ctx_.characters) ctx_.characters->playCard(e.seat, layPoint(meldPose(e.card).pos), false);
                cards_.place(e.card, meldPose(e.card), true, lead, 0.05f);
                if (e.type == E::SwapJoker && e.seat != 0) {
                    r3d::CardPose p = cards_.pose(e.card2);
                    p.pos.y += 0.10f;
                    cards_.place(e.card2, p, true, w3d::BOT_SWAPBACK_LEAD, 0.05f);
                }
                soundAt(ui::Sfx::CardPlace, (lead + 0.4f) / speed_);
                selected_.erase(e.card);
                break;
            }
            case E::Discard: {
                const r3d::CardPose p = discardPose(std::min((int)g_.discardPile().size(), SHOWN_PILE) - 1, e.card);
                if (e.seat != 0) {
                    playFromHand(e.seat, e.card, p, rng_.chance(0.3f));
                } else {
                    cards_.place(e.card, p, true, 0.f, 0.06f);
                    soundAt(ui::Sfx::CardPlace, 0.42f / speed_);
                    if (!aiSeat_ && !replay_) handCueCard(e.card, 0.f);
                }
                selected_.erase(e.card);
                break;
            }
            case E::HandEnd: {
                log_ = e.text;
                for (const std::string& l : e.lines) log_ += " | " + l;
                hud_.toast(e.text, ui::pal::Highlight, 3.6f);
                if (e.amount == 1) { // konken
                    hud_.banner("KONKEN!", e.seat == 0 ? std::string("Elden bitirdin, herkes iki kat yazar")
                                                       : names_[(size_t)std::max(0, e.seat)] + " elden bitirdi",
                                ui::pal::Highlight, 3.2f);
                    if (ctx_.characters) ctx_.characters->crowdReact(e.seat == 0 ? r3d::Characters::CrowdCheer : r3d::Characters::CrowdLaugh,
                                                                     layPoint({0.f, 0.8f, 0.f}), 0.8f);
                    if (e.seat > 0) say(e.seat, kKonken[e.seat], true);
                    if (e.seat == 0) noteAchievement("konken_elden");
                } else if (e.seat > 0 && rng_.chance(0.6f)) {
                    say(e.seat, kFinished[e.seat][rng_.range(2)]);
                }
                if (e.seat > 0 && ctx_.characters) ctx_.characters->react(e.seat, 1, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                sound(e.seat == 0 ? ui::Sfx::Win : e.seat < 0 ? ui::Sfx::Lose : ui::Sfx::CardGather);
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            case E::Burn: { // (son kalan) a player burned: he leaves the table at the next deal
                hud_.toast(e.text, ui::pal::Bad, 3.6f);
                const bool last = g_.stage() == kk::Stage::MatchOver;
                if (e.seat == 0) {
                    if (!replay_ && !aiSeat_ && last) hud_.banner("YANDIN!", "Parti bitti", ui::pal::Bad, 3.2f);
                    // (otherwise the panel at the next deal tells it: "Yandın, masadan kalktın")
                    const int t = g_.nextActive(0);
                    if (t > 0 && rng_.chance(0.8f)) say(t, kYouBurned[t], true);
                } else {
                    say(e.seat, kBurned[e.seat], true);
                    if (ctx_.characters) ctx_.characters->react(e.seat, 2, layPoint({0.f, w3d::TABLE_Y, 0.f}));
                    const int o = g_.nextActive(e.seat);
                    if (o > 0 && o != e.seat && rng_.chance(0.5f)) say(o, fill(kOtherBurned[o], names_[(size_t)e.seat]));
                }
                if (ctx_.characters && !last) ctx_.characters->crowdReact(r3d::Characters::CrowdLaugh, layPoint({0.f, 0.8f, 0.f}), 0.5f);
                break;
            }
            default: break;
            }
        }
        syncOrder();
    }

    std::vector<int> handOf(int seat) const override { return g_.hand(seat); }
    int actor() const override {
        if (g_.stage() == kk::Stage::Draw || g_.stage() == kk::Stage::Play) return g_.current();
        return -1;
    }
    bool humanTurn() const override {
        return (g_.stage() == kk::Stage::Draw || g_.stage() == kk::Stage::Play) && g_.current() == 0;
    }
    std::vector<int> playableCards() const override { return {}; }
    void playHumanCard(int card) override { (void)card; }
    float botDelay(int seat) const override {
        (void)seat;
        if (g_.stage() == kk::Stage::Draw) return 0.85f;
        return 0.6f;
    }
    void botStep(int seat) override {
        const kk::BotAction a = bots_[(size_t)seat]->next(g_, seat);
        if (!kk::applyBotAction(g_, seat, a).ok) kk::applyBotAction(g_, seat, kk::fallbackAction(g_, seat));
    }
    size_t decisionStamp() const override { return g_.actionLog().size() + 100000u * (size_t)g_.handIndex(); }
    bool computeHint(Hint& h) override {
        if (actor() != 0 || !bots_[0]) return false;
        const kk::BotAction a = bots_[0]->next(g_, 0);
        switch (a.kind) {
        case kk::BotAction::Kind::DrawStock: h.text = "İpucu: desteden çek"; break;
        case kk::BotAction::Kind::TakeDiscard: h.text = "İpucu: yerden " + kk::cardAccusativeTR(g_.discardTop()) + " al"; break;
        case kk::BotAction::Kind::ReturnDiscard: h.text = "İpucu: yerden aldığını geri ver"; break;
        case kk::BotAction::Kind::Lay: {
            selected_.clear();
            int v = 0;
            for (const auto& mv : a.melds) {
                kk::Meld m;
                if (kk::makeMeld(mv, m)) v += m.value();
                for (int c : mv) selected_.insert(c);
            }
            h.text = g_.opened(0) ? "İpucu: seçili perleri indir" : "İpucu: seçili kâğıtlarla aç (" + std::to_string(v) + ")";
            break;
        }
        case kk::BotAction::Kind::Add:
            h.card = a.card;
            h.text = "İpucu: " + kk::cardAccusativeTR(a.card) + " masadaki pere işle";
            break;
        case kk::BotAction::Kind::Swap:
            h.card = a.card;
            h.text = "İpucu: " + kk::cardNameTR(a.card) + " ile masadaki jokeri al";
            break;
        case kk::BotAction::Kind::Discard:
            h.card = a.card;
            h.text = "İpucu: " + kk::cardAccusativeTR(a.card) + " at";
            break;
        }
        return true;
    }
    void onHudClick(int id) override {
        sound(ui::Sfx::Button);
        if (outPanel() && (id == r3d::GameHud::PANEL_ID || id == r3d::GameHud::PANEL_ID + 1)) { // (son kalan)
            if (id == r3d::GameHud::PANEL_ID) {
                outChoice_ = 1;
                setFast(true);
                hud_.toast("Kalanları hızlı izliyorsun", ui::pal::TextLight, 2.2f);
            } else {
                outChoice_ = 2;
                skipToResult();
            }
            return;
        }
        if (g_.isOut(0)) { // (son kalan) the column: "Hızlı İzle" / "Normal Hız", "Sonuca Geç"
            if (id == BTN_OPEN) setFast(!fast_);
            else if (id == BTN_RETURN) skipToResult();
            return;
        }
        if (id == BTN_OPEN) layOpen();
        else if (id == BTN_RETURN) report(g_.returnDiscard(0));
        else if (id == BTN_SORT) sortHand();
        pumpEngine();
    }

    // ---- dealing: the first discard comes off the deck after the hands; the stock lies under them
    std::vector<std::pair<int, r3d::CardPose>> dealtExtras() const override {
        if (g_.discardTop() < 0) return {};
        return {{g_.discardTop(), discardPose(0, g_.discardTop())}};
    }
    int deckRemaining() const override { return g_.stockSize(); }

    void layoutExtra() override {
        const bool deal = dealing();
        // the stock: by the dealer while dealing, then in the middle
        // (once dealt, only the top of the stock and of the discard pile is drawn: a hundred stacked cards with their
        // shadows cost a lot of frame time and nobody sees the ones underneath; the stack keeps its height)
        const std::vector<int> stock = stockCards();
        const int ns = (int)stock.size(), shownStock = deal ? ns : std::min(ns, SHOWN_STOCK);
        for (int j = 0; j < shownStock; ++j) {
            const int i = ns - shownStock + j;
            if (deal) {
                placeExtra(stock[(size_t)i], deckSlot(g_.dealer(), i), true);
            } else {
                r3d::CardPose p = stockPose(j);
                p.pos.y = w3d::TABLE_Y + r3d::CARD_T * 0.5f + (float)(j + 1) / (float)shownStock * r3d::CARD_T * (float)(ns - 1);
                placeExtra(stock[(size_t)i], layPose(p), true, 0.004f * (float)(j % 13));
            }
        }
        // the discard pile
        const std::vector<int>& pile = g_.discardPile();
        const int np = (int)pile.size(), first = std::max(0, np - SHOWN_PILE);
        for (int i = first; i < np; ++i) placeExtra(pile[(size_t)i], discardPose(i - first, pile[(size_t)i]));
        // the melds
        MeldLayout L;
        computeMelds(L);
        for (const kk::Meld& m : g_.table())
            for (int c : m.cards) placeExtra(c, L.has[(size_t)c] ? L.pose[(size_t)c] : cards_.target(c));
        // the player's fan (once the deal brought it to the hand)
        layoutMine();
    }

private:
    static constexpr int BTN_OPEN = 1, BTN_RETURN = 2, BTN_SORT = 3;
    static constexpr int SHOWN_STOCK = 10, SHOWN_PILE = 8;

    // ---- poses (world)
    r3d::CardPose stockPose(int i) const {
        r3d::CardPose p;
        p.pos = {STOCK_POS.x, w3d::TABLE_Y + r3d::CARD_T * (0.5f + (float)i), STOCK_POS.z};
        p.pos.x += 0.0006f * (float)((i * 7) % 5 - 2);
        p.rot = r3d::cardFlat(6.f + (float)((i * 13) % 5 - 2) * 0.5f, false);
        return p;
    }
    r3d::CardPose discardPose(int i, int card) const {
        uint32_t h = (uint32_t)(card * 2654435761u) ^ 40503u; // (the scatter follows the card: it stays put as the pile grows)
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        const float r0 = (float)(h & 1023u) / 1023.f - 0.5f, r1 = (float)((h >> 10) & 1023u) / 1023.f - 0.5f;
        const float r2 = (float)((h >> 20) & 1023u) / 1023.f - 0.5f;
        r3d::CardPose p;
        p.pos = {DISCARD_POS.x + r0 * 0.018f, w3d::TABLE_Y + r3d::CARD_T * (0.5f + (float)i), DISCARD_POS.z + r1 * 0.014f};
        p.rot = r3d::cardFlat(r2 * 26.f, true);
        return layPose(p);
    }
    std::vector<int> stockCards() const {
        // the stock's cards, bottom first: everything not in a hand, the pile or a meld (the engine keeps its order
        // private; the display only needs a stable one)
        std::array<bool, kk::NUM_CARDS> used{};
        for (int s = 0; s < 4; ++s)
            for (int c : g_.hand(s)) used[(size_t)c] = true;
        for (int c : g_.discardPile()) used[(size_t)c] = true;
        for (const kk::Meld& m : g_.table())
            for (int c : m.cards) used[(size_t)c] = true;
        std::vector<int> v;
        for (int c = 0; c < kk::NUM_CARDS; ++c)
            if (!used[(size_t)c]) v.push_back(c);
        // a fixed shuffle so the visible top is not always the same id
        std::sort(v.begin(), v.end(), [](int a, int b) { return (a * 37) % 109 < (b * 37) % 109; });
        if ((int)v.size() > g_.stockSize()) v.resize((size_t)g_.stockSize());
        return v;
    }
    int stockTop() const {
        const std::vector<int> v = stockCards();
        return v.empty() ? -1 : v.back();
    }

    // Every meld's place: rows in its owner's zone (layout space), each card overlapping the previous by MELD_STEP.
    struct MeldLayout {
        std::vector<MeldRect> rects;
        std::array<r3d::CardPose, kk::NUM_CARDS> pose{};
        std::array<bool, kk::NUM_CARDS> has{};
    };
    void computeMelds(MeldLayout& L) const {
        const std::vector<kk::Meld>& table = g_.table();
        for (int s = 0; s < 4; ++s) {
            const MeldZone& z = kZones[s];
            std::vector<int> mine;
            for (size_t i = 0; i < table.size(); ++i)
                if (table[i].owner == s) mine.push_back((int)i);
            if (mine.empty()) continue;
            float step = MELD_STEP;
            // squeeze when the rows run out
            for (int tries = 0; tries < 4; ++tries) {
                int rows = 1;
                float x = 0.f;
                for (int mi : mine) {
                    const float w = r3d::CARD_W + step * (float)(table[(size_t)mi].size() - 1);
                    if (x > 0.f && x + w > z.width) {
                        ++rows;
                        x = 0.f;
                    }
                    x += w + MELD_GAP;
                }
                if (rows <= z.maxRows) break;
                step *= 0.8f;
            }
            int row = 0;
            float x = 0.f;
            for (int mi : mine) {
                const kk::Meld& m = table[(size_t)mi];
                const float w = r3d::CARD_W + step * (float)(m.size() - 1);
                if (x > 0.f && x + w > z.width) {
                    ++row;
                    x = 0.f;
                }
                const float zc = z.z0 + ROW_STEP * (float)row;
                MeldRect rc;
                rc.meld = mi;
                rc.x0 = z.x0 + x;
                rc.x1 = z.x0 + x + w;
                rc.z0 = zc - r3d::CARD_H * 0.5f;
                rc.z1 = zc + r3d::CARD_H * 0.5f;
                L.rects.push_back(rc);
                for (int k = 0; k < m.size(); ++k) {
                    r3d::CardPose p;
                    p.pos = {z.x0 + x + r3d::CARD_W * 0.5f + step * (float)k,
                             w3d::TABLE_Y + r3d::CARD_T * (0.5f + (float)k) + 0.0011f * (float)row, zc};
                    p.rot = r3d::cardFlat(0.f, true);
                    const int c = m.cards[(size_t)k];
                    L.pose[(size_t)c] = layPose(p);
                    L.has[(size_t)c] = true;
                }
                x += w + MELD_GAP;
            }
        }
    }
    r3d::CardPose meldPose(int card) const {
        MeldLayout L;
        computeMelds(L);
        return L.has[(size_t)card] ? L.pose[(size_t)card] : cards_.pose(card);
    }

    // ---- the player's fan
    void syncOrder() {
        const std::vector<int>& h = g_.hand(0);
        order_.erase(std::remove_if(order_.begin(), order_.end(),
                                    [&](int c) { return std::find(h.begin(), h.end(), c) == h.end(); }),
                     order_.end());
        if (order_.empty() && !h.empty()) { // a new hand: sorted by suit and rank
            order_ = h;
            std::sort(order_.begin(), order_.end(), [](int a, int b) { return sortKey(a) < sortKey(b); });
        } else {
            for (int c : h)
                if (std::find(order_.begin(), order_.end(), c) == order_.end()) order_.push_back(c);
        }
        for (auto it = selected_.begin(); it != selected_.end();)
            it = std::find(order_.begin(), order_.end(), *it) == order_.end() ? selected_.erase(it) : std::next(it);
        kbIndex_ = std::clamp(kbIndex_, 0, std::max(0, (int)order_.size() - 1));
    }
    r3d::CardPose fanPose(int i, int n) const { return tableFrame().eyePose(r3d::cardlayout::hand(0, i, n)); }

    // (while dealing, placeExtra keeps a card that has not reached the hand on its deal pose)
    void layoutMine() {
        const int n = (int)order_.size();
        const bool myTurn = humanTurn() && !aiSeat_ && !replay_;
        const Hint* hint = activeHint();
        const int hintCard = hint ? hint->card : -1;
        const int kb = myTurn && ui::keyboardNav() && kbIndex_ < n ? order_[(size_t)kbIndex_] : -1;
        for (int i = 0; i < n; ++i) {
            const int c = order_[(size_t)i];
            if (c == held_ && heldDrag_ && ctx_.renderer) {
                // dragged: the card follows the mouse a hand's breadth over the felt
                const Ray ray = ctx_.renderer->rayFromVirtual(ui::virtualMouse());
                const float y = w3d::TABLE_Y + 0.06f;
                if (ray.direction.y < -1e-3f) {
                    const float t = (y - ray.position.y) / ray.direction.y;
                    r3d::CardPose dp;
                    dp.pos = tableFrame().toLayout(Vector3Add(ray.position, Vector3Scale(ray.direction, t)));
                    dp.pos.z = std::min(dp.pos.z, 0.52f);
                    dp.rot = r3d::cardStanding(0.f, 35.f, true);
                    extraFollow(c, layPose(dp));
                }
            } else {
                placeExtra(c, fanPose(i, n), true, 0.f);
            }
            const bool sel = selected_.count(c) > 0;
            const bool hov = c == myHover_;
            float raise = sel ? 0.020f : 0.f;
            if (hov) raise = std::max(raise, 0.012f);
            if (c == kb) raise = std::max(raise, 0.016f);
            if (c == hintCard) raise = std::max(raise, 0.014f);
            cards_.setRaise(c, raise);
            cards_.setTilt(c, hov || c == kb ? 8.f : 0.f);
            cards_.setGlow(c, sel ? 0.75f : (hov || c == kb) ? 0.45f : 0.f);
            const bool pending = c == g_.takenCard() && g_.current() == 0;
            cards_.setTint(c, pending ? Color{255, 236, 190, 255} : WHITE);
        }
        // the melds a dragged card would go to glow softly
        if (held_ >= 0 && heldDrag_ && g_.opened(0)) {
            for (const kk::Meld& m : g_.table())
                if (kk::canAdd(m, held_) || kk::swapIndex(m, held_) >= 0)
                    for (int c : m.cards) cards_.setGlow(c, 0.35f);
        }
    }
    // A pose that changes every frame (the dragged card): marked placed, then followed.
    void extraFollow(int c, const r3d::CardPose& p) {
        placeExtra(c, cards_.target(c), false); // (keeps it visible this frame; same target: nothing restarts)
        cards_.follow(c, p);
    }

    // ---- input
    // The point of the felt under the mouse (layout space); false when the ray misses the table plane.
    bool feltPoint(Vector2 mouse, Vector3& out) const {
        if (!ctx_.renderer) return false;
        const Ray ray = ctx_.renderer->rayFromVirtual(mouse);
        if (ray.direction.y > -1e-4f) return false;
        const float t = (w3d::TABLE_Y - ray.position.y) / ray.direction.y;
        out = tableFrame().toLayout(Vector3Add(ray.position, Vector3Scale(ray.direction, t)));
        return true;
    }
    // The meld under a layout-space point (-1 none) and which half (front / back).
    int meldAt(Vector3 lp, kk::Side* side) const {
        MeldLayout L;
        computeMelds(L);
        for (auto it = L.rects.rbegin(); it != L.rects.rend(); ++it) {
            if (lp.x < it->x0 - 0.01f || lp.x > it->x1 + 0.01f || lp.z < it->z0 || lp.z > it->z1) continue;
            if (side) *side = lp.x < (it->x0 + it->x1) * 0.5f ? kk::Side::Front : kk::Side::Back;
            return it->meld;
        }
        return -1;
    }
    // 1: the stock, 2: the discard pile, 0: neither.
    int pileAt(Vector3 lp) const {
        auto near = [&](Vector3 c) { return std::fabs(lp.x - c.x) < r3d::CARD_W * 0.62f && std::fabs(lp.z - c.z) < r3d::CARD_H * 0.6f; };
        if (near(STOCK_POS)) return 1;
        if (near(DISCARD_POS)) return 2;
        return 0;
    }

    void myMouse(Vector2 mouse) {
        const bool canAct = humanTurn() && !tableBusy();
        const Ray ray = ctx_.renderer->rayFromVirtual(mouse);
        myHover_ = hud_.mouseOverHud() ? -1 : cards_.pick(ray, &order_);
        Vector3 lp{};
        const bool onFelt = feltPoint(mouse, lp);
        pileHover_ = 0;
        if (onFelt && myHover_ < 0 && !hud_.mouseOverHud() && canAct && g_.stage() == kk::Stage::Draw) {
            pileHover_ = pileAt(lp);
            if (pileHover_ == 2 && !g_.canTakeDiscard(0)) pileHover_ = 0;
            if (pileHover_) ui::requestHandCursor();
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hud_.mouseOverHud()) {
            if (myHover_ >= 0) {
                held_ = myHover_;
                heldAt_ = mouse;
                heldDrag_ = false;
            } else if (pileHover_ == 1) {
                report(g_.drawStock(0));
                pumpEngine();
            } else if (pileHover_ == 2) {
                report(g_.takeDiscard(0));
                pumpEngine();
            }
        }
        if (held_ < 0) return;
        if (!heldDrag_ && Vector2Distance(mouse, heldAt_) > 14.f) {
            heldDrag_ = true;
            handCueCard(held_, -1.f);
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
        // released
        const int c = held_;
        const bool dragged = heldDrag_;
        held_ = -1;
        heldDrag_ = false;
        if (!dragged) {
            // a click: select / unselect; a double click throws it
            const bool dbl = c == lastClick_ && now_ - lastClickAt_ < 0.35f;
            lastClick_ = c;
            lastClickAt_ = now_;
            if (dbl && canAct && g_.stage() == kk::Stage::Play) {
                selected_.erase(c);
                report(g_.discard(0, c));
                pumpEngine();
                lastClick_ = -1;
                return;
            }
            if (selected_.count(c)) selected_.erase(c);
            else selected_.insert(c);
            sound(ui::Sfx::TileClick);
            return;
        }
        // a drag let go: on a meld (işle / take its joker), on the discard pile (at), or back in the fan (reorder)
        kk::Side side = kk::Side::Auto;
        const int m = onFelt ? meldAt(lp, &side) : -1;
        if (m >= 0) {
            if (!canAct) return;
            dropOnMeld(c, m, side, ray);
            return;
        }
        if (onFelt && pileAt(lp) == 2) {
            if (!canAct) return;
            selected_.erase(c);
            report(g_.discard(0, c));
            pumpEngine();
            return;
        }
        reorder(c, mouse);
    }

    void dropOnMeld(int c, int m, kk::Side side, const Ray& ray) {
        const kk::Meld& meld = g_.table()[(size_t)m];
        // dropped right on a joker it stands for: take the joker
        const int under = cards_.pick(ray, &meld.cards);
        const int si = kk::swapIndex(meld, c);
        if (si >= 0 && (under == meld.cards[(size_t)si] || !kk::canAdd(meld, c))) {
            report(g_.swapJoker(0, c, m));
        } else if (kk::canAdd(meld, c, side)) {
            report(g_.addToMeld(0, c, m, side));
        } else {
            report(g_.addToMeld(0, c, m, kk::Side::Auto));
        }
        pumpEngine();
    }

    void reorder(int c, Vector2 mouse) {
        if (!ctx_.renderer) return;
        std::vector<int> rest;
        for (int x : order_)
            if (x != c) rest.push_back(x);
        int at = 0;
        for (size_t i = 0; i < rest.size(); ++i) {
            Vector2 v;
            if (ctx_.renderer->projectToVirtual(cards_.pose(rest[i]).pos, v) && v.x < mouse.x) at = (int)i + 1;
        }
        rest.insert(rest.begin() + at, c);
        if (rest != order_) sound(ui::Sfx::TileClick);
        order_ = rest;
        kbIndex_ = at;
    }

    void myKeys() {
        if (hud_.buttonFocused() || order_.empty()) return;
        const bool canAct = humanTurn() && !tableBusy();
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool wasNav = ui::keyboardNav();
        const int n = (int)order_.size();
        if (ui::keyPressedRepeat(KEY_LEFT) || ui::keyPressedRepeat(KEY_RIGHT)) {
            const int d = ui::keyPressedRepeat(KEY_RIGHT) ? 1 : -1;
            if (wasNav) {
                if (shift) { // carry the card along the fan
                    const int j = std::clamp(kbIndex_ + d, 0, n - 1);
                    std::swap(order_[(size_t)kbIndex_], order_[(size_t)j]);
                    kbIndex_ = j;
                } else {
                    kbIndex_ = (kbIndex_ + d + n) % n;
                }
            }
            ui::noteKeyboardNav();
            sound(ui::Sfx::TileClick);
        }
        const int cur = order_[(size_t)std::clamp(kbIndex_, 0, n - 1)];
        if (IsKeyPressed(KEY_SPACE)) {
            ui::noteKeyboardNav();
            if (wasNav) {
                if (selected_.count(cur)) selected_.erase(cur);
                else selected_.insert(cur);
                sound(ui::Sfx::TileClick);
            }
        }
        if (!canAct) return;
        if (IsKeyPressed(KEY_D)) {
            ui::noteKeyboardNav();
            report(g_.drawStock(0));
            pumpEngine();
        } else if (IsKeyPressed(KEY_A)) {
            ui::noteKeyboardNav();
            report(g_.takeDiscard(0));
            pumpEngine();
        } else if (IsKeyPressed(KEY_O)) {
            ui::noteKeyboardNav();
            layOpen();
            pumpEngine();
        } else if (IsKeyPressed(KEY_G)) {
            ui::noteKeyboardNav();
            report(g_.returnDiscard(0));
            pumpEngine();
        } else if (IsKeyPressed(KEY_I)) {
            ui::noteKeyboardNav();
            addAnywhere(cur);
            pumpEngine();
        } else if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && wasNav) {
            ui::noteKeyboardNav();
            selected_.erase(cur);
            report(g_.discard(0, cur));
            pumpEngine();
        } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            ui::noteKeyboardNav(); // (the first key only shows the cursor)
        }
    }

    // "I": the card goes to the first meld it fits (a joker it stands for first).
    void addAnywhere(int c) {
        const std::vector<kk::Meld>& t = g_.table();
        for (size_t i = 0; i < t.size(); ++i)
            if (kk::swapIndex(t[i], c) >= 0) {
                report(g_.swapJoker(0, c, (int)i));
                return;
            }
        for (size_t i = 0; i < t.size(); ++i)
            if (kk::canAdd(t[i], c)) {
                report(g_.addToMeld(0, c, (int)i));
                return;
            }
        report(kk::ActionResult::fail(kk::cardNameTR(c) + " masadaki hiçbir pere gitmiyor"));
    }

    // The selection as melds: one meld in the fan's order when it is one, else the best partition (all of it).
    bool selectionMelds(std::vector<std::vector<int>>& melds, std::string* why) const {
        std::vector<int> sel;
        for (int c : order_)
            if (selected_.count(c)) sel.push_back(c);
        melds.clear();
        if (sel.empty()) {
            if (why) *why = "Önce perlerini seç (kâğıda tıkla ya da Boşluk)";
            return false;
        }
        kk::Meld one;
        if (kk::makeMeld(sel, one)) {
            melds.push_back(sel);
            return true;
        }
        const kk::Partition P = kk::bestPartition(sel, g_.opened(0) ? kk::Goal::Shed : kk::Goal::Value, -1, true);
        if (P.melds.empty() || !P.rest.empty()) {
            if (why) {
                std::string s;
                for (size_t i = 0; i < P.rest.size() && i < 3; ++i) s += (i ? ", " : "") + kk::cardNameTR(P.rest[i]);
                *why = P.melds.empty() ? "Seçtiğin kâğıtlardan per çıkmıyor" : "Bu kâğıtlar hiçbir pere girmiyor: " + s;
            }
            return false;
        }
        melds = P.melds;
        return true;
    }
    int selectionValue() const {
        std::vector<std::vector<int>> melds;
        if (!selectionMelds(melds, nullptr)) return 0;
        int v = 0;
        for (const auto& mv : melds) {
            kk::Meld m;
            if (kk::makeMeld(mv, m)) v += m.value();
        }
        return v;
    }
    bool canLaySelection() const {
        if (!humanTurn() || g_.stage() != kk::Stage::Play) return false;
        std::vector<std::vector<int>> melds;
        return selectionMelds(melds, nullptr) && g_.checkLay(0, melds);
    }
    void layOpen() {
        std::vector<std::vector<int>> melds;
        std::string why;
        if (!selectionMelds(melds, &why)) {
            report(kk::ActionResult::fail(why));
            return;
        }
        report(g_.layMelds(0, melds));
    }
    // "Diz": the fan sorted — first by melds (what the best partition lays, then the rest by suit), pressed again by suit
    // and rank.
    void sortHand() {
        sortByMelds_ = !sortByMelds_;
        std::vector<int> h = order_;
        std::sort(h.begin(), h.end(), [](int a, int b) { return sortKey(a) < sortKey(b); });
        if (sortByMelds_) {
            const kk::Partition P = kk::bestPartition(h, g_.opened(0) ? kk::Goal::Shed : kk::Goal::Value, -1, false);
            std::vector<int> o;
            for (const auto& mv : P.melds) o.insert(o.end(), mv.begin(), mv.end());
            std::vector<int> rest = P.rest;
            std::sort(rest.begin(), rest.end(), [](int a, int b) { return sortKey(a) < sortKey(b); });
            o.insert(o.end(), rest.begin(), rest.end());
            h = o;
            hud_.toast("Perler başa dizildi", ui::pal::TextLight, 1.6f);
        } else {
            hud_.toast("Renge ve sıraya göre dizildi", ui::pal::TextLight, 1.6f);
        }
        order_ = h;
    }

    void drawMyButtons(Vector2 mouse, bool aiSeat) {
        if (g_.isOut(0)) { // (son kalan) the player burned: the speed and the result
            const bool can = !aiSeat && !replay_ && g_.stage() != kk::Stage::MatchOver && outChoice_ != 0;
            hud_.buttons(mouse, {"İpucu", fast_ ? "Normal Hız" : "Hızlı İzle", "Sonuca Geç"}, {false, can, can}, {false, false, false},
                         aiSeat && !replay_);
            hud_.keyHelp(replay_ ? "Maç tekrarı  ·  Esc menü" : "Yandın: kalanları izliyorsun  ·  Tab düğmeler  ·  Esc menü");
            phase_ = outPanel() ? "yandin" : "";
            return;
        }
        const bool mine = !aiSeat && !replay_ && humanTurn() && !tableBusy();
        const bool canLay = mine && canLaySelection();
        const bool canReturn = mine && g_.stage() == kk::Stage::Play && g_.takenCard() >= 0;
        hud_.buttons(mouse, {"İpucu", g_.opened(0) ? "Per İndir" : "Aç", "Geri Ver", "Diz"},
                     {!aiSeat && !replay_ && hintAvailable(), canLay, canReturn, !aiSeat && !replay_ && !order_.empty()},
                     {false, canLay, canReturn, false}, aiSeat && !replay_);
        std::string help;
        if (replay_) help = "Maç tekrarı  ·  Esc menü";
        else if (hud_.buttonFocused()) help = "Tab / Shift+Tab düğme seç  ·  Enter / Boşluk bas  ·  Oklar kâğıtlara dön  ·  Esc menü";
        else if (!aiSeat && humanTurn() && g_.stage() == kk::Stage::Draw)
            help = "D desteden çek  ·  A yerden al  ·  Oklar kâğıt seç  ·  Shift+Ok kâğıdı taşı  ·  Boşluk işaretle  ·  H ipucu";
        else if (!aiSeat && humanTurn())
            help = "Boşluk işaretle  ·  O aç  ·  I işle  ·  Enter at  ·  G geri ver  ·  Shift+Ok taşı  ·  H ipucu  ·  Tab düğmeler";
        else help = "Oklar / Shift+Ok kâğıtları diz  ·  Tab düğmeler  ·  Y yapay zeka  ·  Esc menü";
        hud_.keyHelp(help);
        // the keyboard's card: a blue chevron over its top edge
        const int n = (int)order_.size();
        if (ui::keyboardNav() && !aiSeat && !replay_ && n > 0 && ctx_.renderer && !dealing() && !hud_.buttonFocused()) {
            const int c = order_[(size_t)std::clamp(kbIndex_, 0, n - 1)];
            const r3d::CardPose p = cards_.pose(c);
            const Vector3 top = Vector3Add(p.pos, Vector3RotateByQuaternion({0.f, 0.f, -r3d::CARD_H * 0.5f}, p.rot));
            Vector2 v;
            if (ctx_.renderer->projectToVirtual(top, v)) {
                const float s = ui::hudTextScale(), bob = 3.f * std::sin(now_ * 6.f);
                const float y = v.y - 12.f * s + bob, w = 11.f * s, h = 12.f * s;
                DrawTriangle({v.x - w - 2.f, y - h - 2.f}, {v.x, y + 2.f}, {v.x + w + 2.f, y - h - 2.f}, Color{8, 20, 34, 220});
                DrawTriangle({v.x - w, y - h}, {v.x, y}, {v.x + w, y - h}, Color{120, 210, 255, 255});
            }
        }
        phase_.clear();
        if (ui::keyboardNav() && !tableBusy()) {
            if (hud_.buttonFocused()) phase_ = "klavye_dugme";
            else if (!aiSeat && humanTurn()) phase_ = "klavye_kart";
        } else if (!aiSeat && humanTurn() && !tableBusy()) {
            phase_ = g_.stage() == kk::Stage::Draw ? "cek" : selected_.empty() ? "oyna" : "secili";
        }
    }

    bool cardPoint(const r3d::Renderer& r, int c, Vector2& out) const {
        const r3d::CardPose p = cards_.pose(c);
        for (int gy = 0; gy < 19; ++gy)
            for (int gx = 0; gx < 19; ++gx) {
                const Vector3 off = Vector3RotateByQuaternion(
                    {r3d::CARD_W * (-0.48f + 0.0533f * (float)gx), 0.f, r3d::CARD_H * (-0.48f + 0.0533f * (float)gy)}, p.rot);
                Vector2 v;
                if (!r.projectToVirtual(Vector3Add(p.pos, off), v)) continue;
                if (cards_.pick(r.rayFromVirtual(v), &order_) == c) {
                    out = v;
                    return true;
                }
            }
        return false;
    }

public:
    bool debugNeedsDraw() const { return humanTurn() && g_.stage() == kk::Stage::Draw && !tableBusy(); }
    // tools/tables_check: where the card being dragged should be let go (a meld / the discard pile), for the plan's
    // Add / Swap / Discard. False: not a drag.
    bool debugDragTarget(const r3d::Renderer& r, Vector2& out) const {
        if (!plan()) return false;
        const kk::BotAction& a = *debugPlan_;
        if (a.kind == kk::BotAction::Kind::Discard) return r.projectToVirtual(layPoint(DISCARD_POS), out);
        if (a.kind != kk::BotAction::Kind::Add && a.kind != kk::BotAction::Kind::Swap) return false;
        if (a.meld < 0 || a.meld >= (int)g_.table().size()) return false;
        const kk::Meld& m = g_.table()[(size_t)a.meld];
        if (a.kind == kk::BotAction::Kind::Swap) {
            const int si = kk::swapIndex(m, a.card);
            if (si < 0) return false;
            const r3d::CardPose p = cards_.pose(m.cards[(size_t)si]);
            // the joker's visible strip (the next card covers its right part)
            return r.projectToVirtual(Vector3Add(p.pos, Vector3RotateByQuaternion({-r3d::CARD_W * 0.38f, 0.f, 0.f}, p.rot)), out);
        }
        kk::Side used = kk::Side::Back;
        kk::canAdd(m, a.card, a.side, &used);
        const int end = used == kk::Side::Front ? m.cards.front() : m.cards.back();
        const r3d::CardPose p = cards_.pose(end);
        const float dx = used == kk::Side::Front ? -r3d::CARD_W * 0.38f : r3d::CARD_W * 0.3f;
        return r.projectToVirtual(Vector3Add(p.pos, Vector3RotateByQuaternion({dx, 0.f, 0.f}, p.rot)), out);
    }

private:
    void report(const kk::ActionResult& r) {
        if (r.ok) return;
        hud_.toast(r.error, ui::pal::Bad, 2.6f);
        sound(ui::Sfx::Error);
    }

    // ---- (son kalan) the player burned
    static constexpr float FAST = 3.f;  // "Hızlı izle": the table at three times the speed
    int outChoice_ = 0;                 // 0 not chosen yet (the panel), 1 watching, 2 skipped to the result
    bool fast_ = false;
    float baseSpeed_ = 1.f;
    bool outPanel() const {
        return g_.isOut(0) && outChoice_ == 0 && !aiSeat_ && !replay_ && (g_.stage() == kk::Stage::Draw || g_.stage() == kk::Stage::Play) &&
               !dealing();
    }
    int burnHand(int s) const {
        for (const kk::HandRecord& r : g_.sheet())
            if (r.burned[(size_t)s]) return r.index;
        return -1;
    }
    void setFast(bool on) {
        fast_ = on;
        CardTableBase::setAnimationSpeed(on ? baseSpeed_ * FAST : baseSpeed_);
        if (ctx_.characters) ctx_.characters->setAnimationSpeed(on ? baseSpeed_ * FAST : baseSpeed_);
    }
    // "Sonuca geç": the bots play the rest of the match at once (every action goes to the log as usual: the save, the
    // replay and the analysis see the same match); the table then shows where it ended.
    void skipToResult() {
        int guard = 0;
        while (g_.stage() != kk::Stage::MatchOver && ++guard < 200000) {
            if (g_.stage() == kk::Stage::HandOver) {
                g_.startNextHand();
                continue;
            }
            const int s = g_.current();
            const kk::BotAction a = bots_[(size_t)s]->next(g_, s);
            if (!kk::applyBotAction(g_, s, a).ok) kk::applyBotAction(g_, s, kk::fallbackAction(g_, s));
        }
        g_.drainEvents();
        order_.clear();
        selected_.clear();
        syncOrder();
        restoreView();
        for (int s = 1; s < 4; ++s)
            if (ctx_.characters) ctx_.characters->setSeatOut(s, false, true);
        setFast(false);
    }

public:
    bool debugWatch_ = false; // tools/tables_check: the burned player's panel takes "Hızlı izle" instead of "Sonuca geç"
    void debugBurn(int seat) { g_.debugSetTotal(seat, g_.rules().limit); }
    int debugPlace(int seat) const { return g_.place(seat); }

private:
    kk::Game g_;
    std::array<std::unique_ptr<kk::Bot>, 4> bots_;
    std::vector<int> order_;          // the player's fan, left to right
    mutable std::set<int> selected_;  // marked for "Aç" (computeHint marks the hint's melds)
    int kbIndex_ = 0;
    int myHover_ = -1;
    int pileHover_ = 0;
    int held_ = -1;
    Vector2 heldAt_{};
    bool heldDrag_ = false;
    int lastClick_ = -1;
    float lastClickAt_ = -10.f;
    bool sortByMelds_ = false;
    mutable std::unique_ptr<kk::BotAction> debugPlan_;
    mutable size_t debugStamp_ = 0;
    // tools/tables_check: Kurt's plan for the player's decision now (made once per decision)
    bool plan() const {
        if (!humanTurn() || !bots_[0]) return false;
        if (!debugPlan_ || debugStamp_ != decisionStamp() * 1000003u + order_.size() * 1009u + selected_.size()) {
            debugPlan_ = std::make_unique<kk::BotAction>(bots_[0]->next(g_, 0));
            debugStamp_ = decisionStamp() * 1000003u + order_.size() * 1009u + selected_.size();
        }
        return true;
    }
};

} // namespace

std::unique_ptr<TableGame> makeKonkenTable() { return std::make_unique<KonkenTable>(); }

// tools/tables_check: the drop point of the drag the player's next move needs (Konken only).
bool konkenDebugDragTarget(const TableGame* t, const r3d::Renderer& r, Vector2& out) {
    const KonkenTable* k = dynamic_cast<const KonkenTable*>(t);
    return k && k->debugDragTarget(r, out);
}
// tools/tables_check (son kalan): `seat`'s total is set to the limit, so he burns when this hand ends; and the place he
// finished in (0: still playing).
void konkenDebugBurn(TableGame* t, int seat, bool watch) {
    if (KonkenTable* k = dynamic_cast<KonkenTable*>(t)) {
        k->debugBurn(seat);
        k->debugWatch_ = watch;
    }
}
int konkenDebugPlace(const TableGame* t, int seat) {
    const KonkenTable* k = dynamic_cast<const KonkenTable*>(t);
    return k ? k->debugPlace(seat) : -1;
}
// tools/tables_check: the player has to draw now (the keyboard presses D / A then).
bool konkenDebugNeedsDraw(const TableGame* t) {
    const KonkenTable* k = dynamic_cast<const KonkenTable*>(t);
    return k && k->debugNeedsDraw();
}

} // namespace app
