// Tavla at its own table (w3d::tavlaFrame): you (seat 0, the bottom of the board) against the regular chosen in Ayarlar
// (Kel Mahmut by default; Hacı Rıza or Emekli Nuri), across; the other two stay at the okey table, watch and talk. Engine tavla::Game (player 0 = you, player 1 = Mahmut), bots tavla::Bot, Tavla3D.
#include "app/TableGame.h"
#include "core/Rng.h"
#include "core/Tavla.h"
#include "core/TavlaBot.h"
#include "r3d/GameHud.h"
#include "r3d/Tavla3D.h"
#include "r3d/World.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

// What the opponent says, by who sits across (seat 1 Hacı Rıza: calm, proverbs; 2 Kel Mahmut: loud, football;
// 3 Emekli Nuri: careful, "bizim zamanımızda").
const char* const kOppHit[4][3] = {
    {},
    {"Kırdım evlat, hayırlısı.", "Sabreden derviş muradına erer: kırdım.", "Açık pul, açık kapı… kırıldı."},
    {"Kırdım abi! Gooool!", "Pulun gitti, kapıdan gir bakalım!", "Vurdum, tribünler ayağa!"},
    {"Kırdım. Bizim zamanımızda pul açık bırakılmazdı.", "Açık pulu gördün mü vuracaksın delikanlı.", "Kırdım. Tecrübe."},
};
const char* const kOppHitBy[4][3] = {
    {},
    {"Vurdun ha, Allah'ın takdiri.", "Kırıldık, olsun; zar döner.", "Her inişin bir çıkışı var evlat."},
    {"Hakem! Bu nasıl zar!", "Kırdın mı? Rövanş var!", "Ofsayttı o, ofsayt!"},
    {"Vay, kırdı. Gözlüğüm buğulandı herhalde.", "Bu zarlar eskisi gibi değil.", "Kırdın ha? Şansa bak."},
};
const char* const kOppDouble[4][3] = {
    {},
    {"Maşallah, çift geldi.", "Çift zar, bereket.", "Hayırlı zar, elhamdülillah."},
    {"Çift geldi abi, kaçın!", "Gördün mü zarı? Şampiyon zarı!", "Dubeş gibisi yok!"},
    {"Çift geldi. Hıh.", "Bak sen, çift zar.", "Eski zar ama sağlam."},
};
const char* const kOppOffer[4][3] = {
    {},
    {"Katlıyorum evlat, hayırlısı.", "Bir kat daha, ne dersin?", "Katladım. Acele işe şeytan karışır, düşün."},
    {"Katlıyorum! Yürek var mı?", "Hadi bakalım, iki katı!", "Katladım, korkan kaçsın!"},
    {"Katlıyorum. Düşün taşın, delikanlı.", "Katladım. Temkinli ol.", "Bir kat. Bu yaşta riski hesaplarım."},
};
const char* const kOppTake[4][3] = {
    {},
    {"Alırım. Tevekkül.", "Kabul. Oyun bitmeden bitmez.", "Alayım bakalım, Allah kerim."},
    {"Alırım! Mahmut kaçmaz!", "Kabul, oyun daha bitmedi!", "Korkmam, at zarını!"},
    {"Alırım. Hesabını yaptım.", "Kabul, ama temkinli.", "Alıyorum. Gözlüğümü takayım."},
};
const char* const kOppDrop[4][3] = {
    {},
    {"Bu el senin olsun evlat.", "Pes ettim; inat iyi değil.", "Vermesini de bilmek lazım."},
    {"Tamam tamam, bu el senin.", "Pes, ama rövanş var!", "Bu oyunu verdim, maçı vermem."},
    {"Pes. Hesap ortada.", "Bu eli verdim, maçı vermem.", "Temkin, delikanlı, temkin."},
};
const char* const kOppSeesDrop[4][2] = {
    {}, {"Akıllıca, evlat.", "Pes ettin ha? Hayırlısı."}, {"Kaçtın ha! Bilirdim.", "Pes etti! Tribünler ayakta!"},
    {"Hıh, kaçtın.", "Pes etti. Bizim zamanımızda pes edilmezdi."},
};
const char* const kOppSeesTake[4][2] = {
    {}, {"Aldın ha? Cesaret güzel.", "Aldın. Hayırlısı olsun."}, {"Aldın ha? Pişman olacaksın!", "Cesaretine hayran kaldım abi."},
    {"Aldın ha? Gençlik.", "Cesur ama akıllı mı, göreceğiz."},
};
const char* const kOppMarsLost[4][3] = {
    {},
    {"Mars olduk, nasip.", "Bu da geçer evlat.", "Sabır; bir daha oynarız."},
    {"Mars mı? Bu zarlarla kim olsa olurdu!", "Hakem, maç satılmış!", "Bir daha, bir daha!"},
    {"Mars mı? Bu zarlarla kim olsa olurdu.", "Gözlüğüm buğulandı…", "Bizim zamanımızda böyle zar gelmezdi."},
};
const char* const kOppMarsThreat[4] = {"", "Bir pul toplayayım da mars olmayalım inşallah.",
                                       "Dur dur, bir pul toplayayım da mars olmayayım!",
                                       "Dur hele, bir pul toplayayım, mars olmayayım."};
// The two at the okey table, watching (by seat).
const char* const kWatchHit[4][2] = {
    {},
    {"Kırdı bak, hayırlısı.", "Kırık pul, kırık kalp…"},
    {"Kırdı! Gol gibi kırık!", "Vay be, tribünler ayağa!"},
    {"Bizim zamanımızda böyle açık bırakılmazdı.", "Hıh, açık pul gördü mü vurur."},
};
const char* const kMarsLines[] = {"Mars! Çift yazın!", "Bu mars tarihe geçer!", "Mars oldun, çaylar senden!"};
const char* const kWatchMars[] = {"Vay vay vay, mars! Ellerine sağlık.", "Mars yediyse çayları o ısmarlar, adet böyle."};

// The buttons of the column, whatever order they are shown in this frame.
enum Btn { B_DOUBLE, B_ROLL, B_UNDO, B_HINT, B_HISTORY };

class TavlaTable final : public TableGame {
public:
    bool init(const TableContext& ctx) override {
        ctx_ = ctx;
        built_ = ctx_.renderer && board_.init(*ctx_.renderer);
        board_.setFrame(w3d::tavlaFrame()); // tavla has its own table
        return built_;
    }
    void shutdown() override {
        if (built_ && ctx_.renderer) board_.shutdown(*ctx_.renderer);
        built_ = false;
    }
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        tavla::Rules r;
        r.matchPoints = std::clamp(st.tavlaPoints, 1, 15);
        r.doubling = st.tavlaDoubling;
        r.katmerliMars = st.tavlaKatmerli;
        r.variant = (tavla::Variant)std::clamp(st.tavlaCesit, 0, tavla::kVariants - 1);
        g_ = tavla::Game(r);
        names_ = names;
        opp_ = ui::twoPlayerOpponent(st, ui::GameKind::Tavla); // Rakip
        g_.setPlayer(0, names[0], true);
        g_.setPlayer(1, names[opp_], false);
        bots_[0] = std::make_unique<tavla::Bot>(tavla::BotLevel::Hard, seed * 2 + 0x7A1ull);
        bots_[1] = std::make_unique<tavla::Bot>((tavla::BotLevel)std::clamp(level_, 0, 2), seed * 2 + 0x7A2ull);
        rng_.reseed(seed ^ 0x7A7Aull);
        replay_ = false;
        hud_.clearToasts();
        hud_.clearBanner();
        results_.clear();
        selected_ = -1;
        marsWarned_ = false;
        hint_ = Hint();
        board_.hideDice();
        board_.setCube(false, 1, -1, 0);
        g_.startMatch(seed);
        syncBoard(true);
        pump();
        if (g_.variant() != tavla::Variant::Klasik) hud_.toast(variantNote(), ui::pal::Highlight, 6.f);
    }
    void startNextHand() override {
        if (g_.stage() != tavla::Stage::GameOver) return;
        selected_ = -1;
        board_.hideDice();
        g_.startNextGame();
        syncBoard(false);
        pump();
    }

    void update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput, bool aiSeat) override {
        (void)cam;
        if (replay_) { // maç tekrarı: our seat plays its saved moves like a bot's, nobody takes input
            humanInput = false;
            aiSeat = true;
        }
        aiSeat_ = aiSeat;
        now_ += dt;
        hud_.update(dt);
        board_.setSpeed(speed_);
        board_.update(dt);
        for (size_t i = 0; i < sfxAt_.size();) {
            if (sfxAt_[i].first <= now_) {
                sound(sfxAt_[i].second);
                sfxAt_.erase(sfxAt_.begin() + (std::ptrdiff_t)i);
            } else {
                ++i;
            }
        }
        ui::updateInputMode(mouse);
        const bool hudKeys = hud_.keyboard(humanInput);
        for (const r3d::GameHud::Click& c : hud_.takeClicks()) {
            if (c.id == r3d::GameHud::AI_ID) aiReq_ = aiReq_ || !replay_;
            else if (c.id == r3d::GameHud::MENU_ID) menuReq_ = true;
            else if (c.id >= 0 && c.id < (int)btnIds_.size() && btnIds_[(size_t)c.id] == B_HISTORY) histOpen_ = !histOpen_;
            else if (c.id == r3d::GameHud::PANEL_ID && humanInput && !aiSeat) answerDouble(true);
            else if (c.id == r3d::GameHud::PANEL_ID + 1 && humanInput && !aiSeat) answerDouble(false);
            else if (humanInput && !aiSeat && c.id >= 0 && c.id < (int)btnIds_.size()) onButton(btnIds_[(size_t)c.id]);
        }
        const bool myMove = humanInput && !aiSeat && humanToAct();
        const bool enter = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        if (myMove && !hudKeys && (enter || IsKeyPressed(KEY_R)) && canRoll()) {
            ui::noteKeyboardNav();
            roll();
        } else if (myMove && !hudKeys) {
            keyboardMove(enter);
        }
        if (myMove && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_U)) && g_.canUndo() && g_.current() == 0) {
            ui::noteKeyboardNav();
            undo();
        }
        if (myMove && IsKeyPressed(KEY_K) && canOfferDouble()) {
            ui::noteKeyboardNav();
            onButton(B_DOUBLE);
        }
        if (myMove && IsKeyPressed(KEY_H) && canHint()) showHint();
        // pointing at the board
        hover_ = -1;
        if (humanInput && !aiSeat && ctx_.renderer && !hud_.mouseOverHud()) {
            hover_ = board_.pick(ctx_.renderer->rayFromVirtual(mouse));
            if (myMove && g_.stage() == tavla::Stage::Moving && g_.current() == 0 && !board_.animating()) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover_ >= 0) clickPoint(hover_);
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) selected_ = -1;
            }
        }
        // bots (Mahmut, and our seat in the Yapay Zeka mode)
        if (!replay_ && !board_.animating() && !over()) {
            const int p = actorPlayer();
            if (p >= 0 && (p == 1 || aiSeat)) {
                if (botPlayer_ != p || botTurn_ != g_.turnNumber() || botStage_ != (int)g_.stage()) {
                    botPlayer_ = p;
                    botTurn_ = g_.turnNumber();
                    botStage_ = (int)g_.stage();
                    botWait_ = (g_.stage() == tavla::Stage::Moving ? (g_.turnSteps().empty() ? 0.7f : 0.45f)
                                : g_.stage() == tavla::Stage::DoubleOffered ? 1.4f // thinking it over
                                                                            : 0.8f) / speed_;
                }
                botWait_ -= dt;
                if (botWait_ <= 0.f) {
                    tavla::Bot& b = *bots_[(size_t)p];
                    tavla::BotAction a = b.next(g_, p);
                    if (a.kind == tavla::BotAction::Kind::None) a = tavla::fallbackAction(g_, p);
                    if (!tavla::applyBotAction(g_, p, a).ok) tavla::applyBotAction(g_, p, tavla::fallbackAction(g_, p));
                    botStage_ = -1; // the next decision gets its own pause
                    pump();
                }
            }
        }
        // the player's highlights
        std::vector<int> targets, sources;
        if (humanToAct() && !aiSeat && g_.stage() == tavla::Stage::Moving && g_.current() == 0) {
            const std::vector<tavla::Step> steps = g_.legalSteps();
            for (const tavla::Step& s : steps)
                if (std::find(sources.begin(), sources.end(), s.from) == sources.end()) sources.push_back(s.from);
            if (selected_ >= 0 && std::find(sources.begin(), sources.end(), selected_) == sources.end()) selected_ = -1;
            if (selected_ < 0 && sources.size() == 1) selected_ = sources[0]; // (the bar, or the only playable point)
            if (selected_ >= 0)
                for (const tavla::Step& s : steps)
                    if (s.from == selected_ && std::find(targets.begin(), targets.end(), s.to) == targets.end()) targets.push_back(s.to);
            if (!hints_ && selected_ < 0) sources.clear();
        } else {
            selected_ = -1;
        }
        // an İpucu: the first step of Kurt's play lit while nothing else is chosen (until the board changes)
        if (hint_.from != -2 && (hint_.turn != g_.turnNumber() || hint_.steps != (int)g_.turnSteps().size() ||
                                 g_.stage() != tavla::Stage::Moving))
            hint_ = Hint();
        if (hint_.from != -2 && selected_ < 0 && humanToAct() && !aiSeat) {
            board_.setHighlights({hint_.to}, {hint_.from}, hint_.from);
        } else {
            board_.setHighlights(targets, sources, selected_);
        }
        if (std::find(targets.begin(), targets.end(), kbTarget_) == targets.end()) kbTarget_ = targets.empty() ? -1 : targets[0];
        board_.setKeyFocus(ui::keyboardNav() && selected_ >= 0 ? kbTarget_ : -1);
        const tavla::Dice& d = g_.dice();
        if (d.n > 0) board_.setDiceUsed(d.used, d.n, d.isDouble());
        syncCube();
        // the mars threat, said once per game when it begins
        const int mt = marsThreat();
        if (mt >= 0 && !marsWarned_) {
            marsWarned_ = true;
            hud_.toast(mt == 0 ? "Mars tehlikesi! Bir pul toplamadan bitirirse mars olursun" : names_[opp_] + " mars tehlikesinde!",
                       mt == 0 ? ui::pal::Bad : ui::pal::Highlight, 3.f);
            if (mt == 1) say(opp_, kOppMarsThreat[opp_]);
            else if (rng_.chance(0.5f)) say(watcher(), "Aman ha, mars kapıda!");
        }
    }

    void submit(r3d::Renderer& r) override { board_.submit(r); }


    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        const bool aiTag = aiSeat && !replay_; // (the "Yapay Zeka" look; in a replay our seat is just its player)
        const bool sen = replay_ && names_[0] == "Sen"; // (the default name: "Sıra sende", not "Sen oynuyor")
        const std::string me = replay_ ? names_[0] : std::string("Yapay zeka");
        aiSeat = aiSeat || replay_;            // no decisions of the player's here
        const bool cube = g_.rules().doubling;
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        for (int s = 1; s < 4; ++s) heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
        r3d::GameHud::Plate& p = plates[opp_];
        p.show = true;
        p.name = names_[opp_];
        p.label = "Sayı";
        p.value = std::to_string(g_.score(1));
        p.turn = g_.current() == 1 && !over();
        if (hints_ && g_.stage() != tavla::Stage::NotStarted) p.badges.push_back({"Pip " + std::to_string(g_.pipCount(1)), Color{70, 60, 50, 255}});
        if (cube && g_.cubeOwner() == 1) p.badges.push_back({"Katlama " + std::to_string(g_.cubeValue()), Color{96, 52, 120, 255}});
        if (g_.barCount(1) > 0) p.badges.push_back({"Kırık " + std::to_string(g_.barCount(1)), Color{168, 42, 34, 255}});
        if (g_.offCount(1) > 0) p.badges.push_back({"Toplanan " + std::to_string(g_.offCount(1)), Color{52, 110, 64, 255}});
        hud_.plates(r, heads, plates);

        std::string st;
        Color sc = ui::pal::TextLight;
        const tavla::Dice& d = g_.dice();
        if (g_.stage() == tavla::Stage::GameOver) st = "Oyun bitti";
        else if (g_.stage() == tavla::Stage::MatchOver) st = "Maç bitti";
        else if (g_.stage() == tavla::Stage::OpeningRoll) {
            st = aiSeat ? "Başlangıç zarı atılıyor…" : "Kim başlayacak? Zarı at";
            sc = ui::pal::Highlight;
        } else if (g_.stage() == tavla::Stage::DoubleOffered) {
            if (g_.responder() == 1) st = names_[opp_] + " düşünüyor…";
            else if (aiSeat) st = sen ? std::string("Katlama sende") : me + " katlamayı düşünüyor…";
            else {
                st = "Katlamayı kabul ediyor musun?";
                sc = ui::pal::Highlight;
            }
        } else if (g_.current() == 0 && aiSeat) st = sen ? std::string("Sıra sende") : me + (g_.stage() == tavla::Stage::NeedRoll ? " zarını atıyor…" : " oynuyor…");
        else if (g_.current() == 0) {
            sc = ui::pal::Highlight;
            if (g_.stage() == tavla::Stage::NeedRoll) st = g_.canDouble(0) ? "Sıra sende: zarı at ya da katla" : "Sıra sende: zarı at";
            else {
                std::string left;
                for (int v : d.left()) left += (left.empty() ? "" : "-") + std::to_string(v);
                st = selected_ < 0 ? "Bir pul seç (" + left + ")" : "Nereye? (" + left + ")";
                if (g_.barCount(0) > 0) st = "Kırık pulunu gir (" + left + ")";
            }
        } else {
            st = names_[opp_] + (g_.stage() == tavla::Stage::NeedRoll ? " zarını atıyor…" : " oynuyor…");
        }
        std::vector<std::pair<std::string, Color>> parts;
        const Color sep{226, 216, 196, 120};
        const int mt = marsThreat();
        if (mt >= 0) {
            parts.push_back({"Mars tehlikesi!", mt == 0 ? Color{240, 96, 80, 255} : Color{246, 200, 90, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
        }
        if (g_.variant() != tavla::Variant::Klasik) { // Tavla çeşidi, and a Gülbahar ladder being climbed
            std::string v = tavla::variantName(g_.variant());
            if (g_.ladder() > 0) v += ": merdiven " + std::to_string(g_.ladder()) + "-" + std::to_string(g_.ladder()) + (g_.ladder() < 6 ? " \xC2\xBB 6-6" : "");
            parts.push_back({v, Color{246, 200, 90, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
        }
        if (hints_ && g_.stage() != tavla::Stage::NotStarted && g_.stage() != tavla::Stage::OpeningRoll) {
            parts.push_back({"Pip " + std::to_string(g_.pipCount(0)) + " / " + std::to_string(g_.pipCount(1)), Color{226, 216, 196, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
        }
        if (cube && g_.stage() != tavla::Stage::NotStarted) {
            std::string c = "Katlama " + std::to_string(g_.cubeValue());
            if (g_.crawfordGame()) c = "Crawford: katlama yok";
            else if (g_.cubeOwner() == 0) c += " (sende)";
            else if (g_.cubeOwner() == 1) c += " (" + names_[opp_] + ")";
            else c += " (ortada)";
            parts.push_back({c, Color{206, 176, 232, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
        }
        parts.push_back({"Sayı " + std::to_string(g_.score(0)) + " - " + std::to_string(g_.score(1)) + " (" +
                             std::to_string(g_.matchPoints()) + "'e)",
                         Color{238, 198, 112, 255}});
        hud_.status(st, sc, humanToAct() && !aiSeat, parts, aiTag);

        // the button column: Katla (only while the player may double), Zar At, Geri Al, Hamleler
        const bool canR = !aiSeat && canRoll();
        const bool canU = !aiSeat && g_.canUndo() && g_.current() == 0;
        const bool canD = !aiSeat && canOfferDouble();
        std::vector<std::string> labels;
        std::vector<bool> en, glow;
        btnIds_.clear();
        auto add = [&](int id, const char* label, bool e, bool gl) {
            btnIds_.push_back(id);
            labels.push_back(label);
            en.push_back(e);
            glow.push_back(gl);
        };
        if (canD) add(B_DOUBLE, "Katla", true, false);
        add(B_ROLL, "Zar At", canR, canR);
        add(B_UNDO, "Geri Al", canU, false);
        add(B_HINT, "\xC4\xB0pucu", !aiSeat && canHint(), false);
        add(B_HISTORY, "Hamleler", true, false);
        hud_.buttons(mouse, labels, en, glow, aiTag);

        // Mahmut's offer waits for the player's answer
        if (g_.stage() == tavla::Stage::DoubleOffered && g_.responder() == 0 && !aiSeat && !board_.animating()) {
            const int v = g_.cubeValue();
            std::vector<r3d::GameHud::PanelButton> b(2);
            b[0].label = "Kabul Et";
            b[0].hint = "oyun " + std::to_string(2 * v) + " katına";
            b[1].label = "Pes Et";
            b[1].hint = names_[opp_] + "'a " + std::to_string(v) + " sayı";
            hud_.panel(mouse, names_[opp_] + " katlamak istiyor (" + std::to_string(v) + " \xC2\xBB " + std::to_string(2 * v) + ")",
                       "Kabul edersen katlama zarı senin olur, pes edersen oyun biter", b, 2, 190.f, 150.f);
        }
        if (histOpen_) hud_.listPanel(mouse, "Hamleler \xC2\xB7 " + std::to_string(g_.gameIndex() + 1) + ". oyun", historyLines(aiTag));
        {
            std::string help = "Tab düğmeler  ·  Y yapay zeka  ·  Esc menü";
            if (replay_) help = "Maç tekrarı  ·  Esc menü";
            else if (hud_.buttonFocused()) help = "Tab / Shift+Tab düğme seç  ·  Enter / Boşluk bas  ·  Oklar tahtaya dön  ·  Esc menü";
            else if (hud_.panelShown()) help = "Sol / Sağ seç  ·  Enter / Boşluk onayla  ·  H ipucu  ·  Esc menü";
            else if (!aiSeat && humanToAct() && canRoll())
                help = std::string("R / Enter zar at") + (canD ? "  ·  K katla" : "") + "  ·  H ipucu  ·  1-" +
                       std::to_string(labels.size()) + " düğmeler  ·  Esc menü";
            else if (!aiSeat && humanToAct() && g_.stage() == tavla::Stage::Moving)
                help = "Sol / Sağ pul seç  ·  Yukarı / Aşağı nereye  ·  Enter / Boşluk oyna  ·  U / Geri tuşu geri al  ·  H ipucu  ·  Esc menü";
            hud_.keyHelp(help);
        }
        hud_.toasts();
        // (tools/tables_check takes a picture of each of these once)
        phase_.clear();
        if (!board_.animating() && ui::keyboardNav() && !aiSeat && humanToAct() && g_.stage() == tavla::Stage::Moving &&
            selected_ >= 0)
            phase_ = "klavye_hamle";
        else if (!board_.animating()) {
            if (g_.stage() == tavla::Stage::DoubleOffered && g_.responder() == 0) phase_ = "teklif";
            else if (g_.stage() == tavla::Stage::DoubleOffered) phase_ = "katladin";
            else if (g_.lastResult().mars && over()) phase_ = "mars";
            else if (marsThreat() >= 0) phase_ = "mars_tehlikesi";
            else if (canD) phase_ = "katla";
            else if (cube && g_.cubeOwner() >= 0 && g_.cubeValue() >= 4) phase_ = "kup4";
            else if (cube && g_.cubeOwner() >= 0) phase_ = g_.cubeOwner() == 0 ? "kup_sende" : "kup_mahmut";
            else if (histOpen_ && g_.gameLog().size() >= 12) phase_ = "hamleler";
        }
    }

    bool handOver() const override { return over(); }
    bool matchOver() const override { return g_.stage() == tavla::Stage::MatchOver; }
    bool animating() const override { return board_.animating(); }
    int activeSeat() const override {
        if (over() || g_.stage() == tavla::Stage::OpeningRoll) return -1;
        return g_.current() == 0 ? 0 : opp_;
    }
    bool mouseBusy() const override { return hover_ >= 0 || hud_.mouseOverHud(); }
    std::vector<int> seats() const override { return {0, opp_}; }
    int location() const override { return 1; }
    void setLevel(int level) override {
        level_ = std::clamp(level, 0, 2);
        if (bots_[1]) bots_[1]->setLevel((tavla::BotLevel)level_);
    }
    void setAnimationSpeed(float s) override { speed_ = std::clamp(s, 0.25f, 4.f); }
    void setHints(bool on) override { hints_ = on; }
    bool consumeMenuRequest() override {
        const bool r = menuReq_;
        menuReq_ = false;
        return r;
    }
    bool consumeAiToggleRequest() override {
        const bool r = aiReq_;
        aiReq_ = false;
        return r;
    }
    void toast(const std::string& text, Color c, float seconds) override { hud_.toast(text, c, seconds); }
    std::string lastLogLine() const override { return log_; }

    // ---- save / resume: the engine's action log, one line each ----
    bool saveState(std::vector<std::string>& lines) const override {
        if (g_.stage() == tavla::Stage::NotStarted) return false;
        lines.clear();
        for (const tavla::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    // Right after startMatch with the saved seed: every action again, nothing animated, the board snapped there.
    bool restoreState(const std::vector<std::string>& lines) override {
        for (const std::string& line : lines) {
            tavla::LoggedAction a;
            if (!tavla::LoggedAction::decode(line, a) || !g_.replay(a)) return false;
            for (const tavla::GameEvent& e : g_.drainEvents()) {
                if (e.type == tavla::EvType::GameEnd) {
                    results_.push_back(g_.lastResult());
                    log_ = e.text + " | " + std::to_string(g_.score(0)) + "-" + std::to_string(g_.score(1));
                }
            }
        }
        for (auto& b : bots_)
            if (b) b->resetForGame();
        hud_.clearToasts();
        hud_.clearBanner();
        sfxAt_.clear();
        selected_ = -1;
        hint_ = Hint();
        marsWarned_ = marsThreat() >= 0;
        botStage_ = -1;
        syncBoard(true);
        const tavla::Dice& d = g_.dice();
        if (d.n > 0) {
            board_.placeDice(g_.current(), d.d1, d.d2);
            board_.setDiceUsed(d.used, d.n, d.isDouble());
        } else {
            board_.hideDice();
        }
        board_.setCube(false, 1, -1, 0);
        syncCube(); // (appears in place)
        hud_.toast(std::to_string(g_.gameIndex() + 1) + ". oyundan devam: sayı " + std::to_string(g_.score(0)) + " - " +
                       std::to_string(g_.score(1)),
                   ui::pal::Highlight, 3.f);
        return true;
    }
    // ---- maç tekrarı: the saved lines one by one, each with its usual animation (dice show the logged roll) ----
    bool setReplayMode(bool on) override {
        replay_ = on;
        if (on) aiSeat_ = true;
        selected_ = hover_ = kbTarget_ = -1;
        hint_ = Hint();
        botStage_ = -1;
        return true;
    }
    int replayStep(const std::string& line) override {
        if (!replay_) return -1;
        tavla::LoggedAction a;
        if (!tavla::LoggedAction::decode(line, a)) return -1;
        if (a.kind == tavla::ActKind::NextGame) {
            if (g_.stage() == tavla::Stage::GameOver) return 0;     // the sheet is up: App starts the next game
            return g_.stage() == tavla::Stage::MatchOver ? -1 : 1; // (already started)
        }
        if (board_.animating()) return 0;
        if (!g_.replay(a)) return -1;
        pump();
        return 1;
    }
    bool humanHandScore(int& score) const override {
        if (!over() || g_.lastResult().winner != 0) return false;
        score = g_.lastResult().points;
        return true;
    }
    bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const override {
        if (board_.animating() || !humanToAct()) return false;
        auto button = [&](int id) {
            const std::vector<Rectangle>& b = hud_.buttonRects();
            for (size_t i = 0; i < btnIds_.size() && i < b.size(); ++i)
                if (btnIds_[i] == id) {
                    out = {b[i].x + b[i].width * 0.5f, b[i].y + b[i].height * 0.5f};
                    return true;
                }
            return false;
        };
        // Mahmut's offer: take it unless far behind in the race
        if (g_.stage() == tavla::Stage::DoubleOffered) {
            const std::vector<Rectangle>& pr = hud_.panelRects();
            if (pr.size() < 2) return false;
            const Rectangle& b = pr[g_.pipCount(0) - g_.pipCount(1) > 40 ? 1 : 0];
            out = {b.x + b.width * 0.5f, b.y + b.height * 0.5f};
            return true;
        }
        if (canRoll()) {
            // once in a while look at the moves (the list stays open), double when well ahead, else roll
            if (!histOpen_ && g_.turnNumber() >= 8 && g_.stage() == tavla::Stage::NeedRoll && button(B_HISTORY)) return true;
            if (canOfferDouble() && g_.pipCount(1) - g_.pipCount(0) >= 12 && button(B_DOUBLE)) return true;
            return button(B_ROLL);
        }
        if (g_.stage() != tavla::Stage::Moving || g_.current() != 0) return false;
        const std::vector<tavla::Step> steps = g_.legalSteps();
        if (steps.empty()) return false;
        // first the checker, then where it goes (the same step every time: the first legal one)
        int target = steps[0].from;
        if (selected_ >= 0) {
            target = -1;
            for (const tavla::Step& s : steps)
                if (s.from == selected_) {
                    target = s.to;
                    break;
                }
            if (target < 0) target = steps[0].from;
        }
        Vector3 w;
        if (target == r3d::Tavla3D::OFF) w = {0.38f, w3d::TABLE_Y + 0.0105f, 0.20f};
        else if (target == r3d::Tavla3D::BAR) w = {0.f, w3d::TABLE_Y + 0.0105f, 0.06f};
        else w = board_.pointLocal(target, 0);
        w.y = w3d::TABLE_Y + 0.0105f;
        return r.projectToVirtual(board_.toWorld(w), out);
    }

    std::string debugPhase() const override { return phase_; }

    std::string scoreTitle() const override {
        if (g_.stage() == tavla::Stage::NotStarted) return "";
        const std::string name = g_.variant() == tavla::Variant::Klasik ? std::string("Tavla") : tavla::variantName(g_.variant());
        return name + " \xC2\xB7 " + std::to_string(g_.matchPoints()) + " sayı";
    }
    std::vector<std::string> scoreLines() const override {
        return {names_[0] + " ....... " + std::to_string(g_.score(0)), names_[opp_] + " ....... " + std::to_string(g_.score(1))};
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        m.columns = {aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[0], names_[opp_]};
        m.humanCol = 0;
        const int games = (int)results_.size();
        m.title = std::to_string(games) + ". Oyun";
        m.corner = (g_.variant() == tavla::Variant::Klasik ? std::string("tavla") : std::string(tavla::variantName(g_.variant()))) + ": " +
                   std::to_string(g_.matchPoints()) + " sayı";
        if (!results_.empty()) {
            const tavla::GameResult& r = results_.back();
            const std::string w = r.winner == 0 ? (aiMode ? std::string("Yapay zeka") : std::string("Sen")) : names_[opp_];
            const bool sen = r.winner == 0 && !aiMode;
            if (r.dropped) m.headline = r.winner == 0 ? names_[opp_] + " pes etti, oyun " + (aiMode ? std::string("yapay zekanın!") : std::string("senin!"))
                                                      : (aiMode ? std::string("Yapay zeka pes etti.") : std::string("Pes ettin, oyun Mahmut'un."));
            else if (r.mars) m.headline = sen ? std::string(r.katmerli ? "Katmerli mars yaptın!" : "Mars yaptın!")
                                              : w + (r.katmerli ? " katmerli mars yaptı!" : " mars yaptı!");
            else m.headline = sen ? std::string("Oyunu aldın!") : w + " oyunu aldı.";
            m.tagline = resultText(r);
            m.taglineRed = r.mars;
            m.starCol = r.winner;
            m.rowLabels = {"Sonuç", "Bu oyun"};
            const std::string won = r.mars ? (r.katmerli ? "Katmerli mars!" : "Mars!") : "Kazandı";
            const std::string lost = r.dropped ? "Pes etti" : "";
            m.cells = {{r.winner == 0 ? won : lost, r.winner == 1 ? won : lost},
                       {r.winner == 0 ? "+" + std::to_string(r.points) : "", r.winner == 1 ? "+" + std::to_string(r.points) : ""}};
            m.red = {{r.mars && r.winner == 0, r.mars && r.winner == 1}, {r.mars && r.winner == 0, r.mars && r.winner == 1}};
        }
        for (size_t i = 0; i < results_.size(); ++i) {
            const tavla::GameResult& r = results_[i];
            m.historyLabels.push_back(std::to_string(i + 1) + ". oyun" + (r.mars || r.dropped || r.cube > 1 ? ": " + resultText(r) : ""));
            m.history.push_back({r.winner == 0 ? "+" + std::to_string(r.points) : "", r.winner == 1 ? "+" + std::to_string(r.points) : ""});
        }
        m.totalLabel = "Sayı";
        m.totals = {std::to_string(g_.score(0)), std::to_string(g_.score(1))};
        if (g_.score(0) >= g_.score(1)) m.leaders.push_back(0);
        if (g_.score(1) >= g_.score(0)) m.leaders.push_back(1);
        m.last = g_.stage() == tavla::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.matchPoints()) + " sayıyı ilk yapan maçı alır";
        m.matchHeader = "Maç Bitti";
        m.playedLine = std::to_string(games) + " oyun oynandı";
        const int w = g_.matchWinner() >= 0 ? g_.matchWinner() : (g_.score(0) >= g_.score(1) ? 0 : 1);
        m.ranking = {w, 1 - w};
        m.rank = {1, 2};
        auto sub = [&](int p) {
            int g = 0, mars = 0;
            for (const tavla::GameResult& r : results_)
                if (r.winner == p) {
                    ++g;
                    mars += r.mars ? 1 : 0;
                }
            return std::to_string(g) + " oyun" + (mars ? ", " + std::to_string(mars) + " mars" : "");
        };
        m.rankSub = {sub(0), sub(1)};
        m.humanWon = w == 0;
        m.resultTitle = w == 0 ? (aiMode ? "Yapay zeka kazandı! Çaylar Mahmut'tan!" : "Kazandın! Çaylar Mahmut'tan!")
                               : "Mahmut aldı, bir maç daha?";
        m.resultSub = std::to_string(g_.score(0)) + " - " + std::to_string(g_.score(1));
        return m;
    }

private:
    // The çeşit in a line, at the start of a Gülbahar / Fevga match.
    std::string variantNote() const {
        if (g_.variant() == tavla::Variant::Gulbahar)
            return "Gülbahar: aynı yöne yürünür, tek pul haneyi tutar; 4. zardan sonra çift düşeşe dek çıkar.";
        return "Fevga: aynı yöne yürünür, tek pul haneyi tutar; ilk pul rakibin köşesini geçmeli.";
    }
    // "Mars (2 sayı)", "Mars ×4 = 8 sayı", "Katlama ×2 = 2 sayı", "Pes etti, 4 sayı", "1 sayı"
    static std::string resultText(const tavla::GameResult& r) {
        const std::string pts = std::to_string(r.points) + " sayı";
        const std::string x = " \xC3\x97" + std::to_string(r.cube) + " = ";
        if (r.dropped) return "Pes etti, " + pts;
        if (r.mars) {
            const std::string what = r.katmerli ? "Katmerli mars" : "Mars";
            return r.cube > 1 ? what + x + pts : what + " (" + pts + ")";
        }
        return r.cube > 1 ? "Katlama" + x + pts : pts;
    }
    // The player threatened with a mars (nothing borne off while the other is bearing off), -1 none.
    int marsThreat() const {
        if (over() || g_.stage() == tavla::Stage::NotStarted || g_.stage() == tavla::Stage::OpeningRoll) return -1;
        for (int l = 0; l < 2; ++l)
            if (g_.offCount(l) == 0 && g_.offCount(1 - l) > 0) return l;
        return -1;
    }
    bool canOfferDouble() const { return !board_.animating() && g_.canDouble(0); }

    // ---- İpucu: what Kurt would do in the player's place ----
    struct Hint {
        int from = -2, to = -2; // the first step of the suggested play (-2: none shown)
        int turn = -1, steps = -1;
    };
    bool canHint() const {
        if (board_.animating() || !humanToAct()) return false;
        switch (g_.stage()) {
        case tavla::Stage::NeedRoll: return g_.current() == 0;
        case tavla::Stage::Moving: return g_.current() == 0 && !g_.legalSteps().empty();
        case tavla::Stage::DoubleOffered: return g_.responder() == 0;
        default: return false;
        }
    }
    void showHint() {
        tavla::Bot kurt(tavla::BotLevel::Hard, 0x1B0CEull);
        std::string text;
        if (g_.stage() == tavla::Stage::DoubleOffered) {
            text = kurt.next(g_, 0).kind == tavla::BotAction::Kind::Take ? "kabul et" : "pes et";
        } else if (g_.stage() == tavla::Stage::NeedRoll) {
            text = kurt.next(g_, 0).kind == tavla::BotAction::Kind::Double ? "katla" : "zarı at";
        } else {
            // the whole play for these dice, played out on a copy
            tavla::Game t = g_;
            std::vector<tavla::Step> play;
            const int rung = t.dice().d1; // (Gülbahar: only this rung of a ladder; the next one is its own decision)
            for (int guard = 0; guard < 8 && t.stage() == tavla::Stage::Moving && t.current() == 0 && t.dice().d1 == rung; ++guard) {
                const tavla::BotAction a = kurt.next(t, 0);
                if (a.kind != tavla::BotAction::Kind::Step) break;
                const std::vector<tavla::Step> legal = t.legalSteps();
                tavla::Step st{a.from, a.to, a.die, false};
                for (const tavla::Step& l : legal)
                    if (l.from == a.from && l.to == a.to && l.die == a.die) st = l;
                if (!t.applyStep(0, a.from, a.to, a.die).ok) break;
                play.push_back(st);
            }
            for (const tavla::Step& st : play) text += (text.empty() ? "" : " ") + tavla::stepNotation(0, st);
            if (!play.empty()) {
                hint_.from = play[0].from;
                hint_.to = play[0].to;
                hint_.turn = g_.turnNumber();
                hint_.steps = (int)g_.turnSteps().size();
                selected_ = -1;
            }
        }
        if (text.empty()) return;
        hud_.toast("\xC4\xB0pucu: " + text, Color{150, 220, 160, 255}, 4.f);
        sound(ui::Sfx::Button);
    }
    void syncCube() {
        board_.setCube(g_.rules().doubling, g_.cubeValue(), g_.cubeOwner(),
                       g_.stage() == tavla::Stage::DoubleOffered ? 2 * g_.cubeValue() : 0);
    }
    void answerDouble(bool take) {
        if (g_.responder() != 0 || board_.animating()) return;
        const tavla::ActionResult r = take ? g_.acceptDouble(0) : g_.declineDouble(0);
        if (!r.ok) hud_.toast(r.error, ui::pal::Bad, 2.4f);
        pump();
    }
    // The move list: numbered lines, "Sen 6-5 şeşbeş: 24/18 18/13*", the turn in progress last.
    std::vector<std::pair<std::string, Color>> historyLines(bool aiSeat) const {
        std::vector<std::pair<std::string, Color>> out;
        const Color mine{255, 204, 92, 255}, his{206, 198, 182, 225}; // the player's lines tinted gold
        auto who = [&](int p) { return p == 0 ? (aiSeat ? std::string("Yapay zeka") : names_[0]) : names_[opp_]; };
        int n = 0;
        auto line = [&](const tavla::TurnRecord& t) {
            out.push_back({std::to_string(++n) + ". " + who(t.player) + "  " + tavla::turnNotation(t), t.player == 0 ? mine : his});
        };
        for (const tavla::TurnRecord& t : g_.gameLog()) line(t);
        if (g_.stage() == tavla::Stage::Moving && g_.dice().n > 0) {
            tavla::TurnRecord t;
            t.player = g_.current();
            t.variant = g_.variant();
            t.d1 = g_.dice().d1;
            t.d2 = g_.dice().d2;
            t.steps = g_.turnSteps();
            line(t);
            if (t.steps.empty()) out.back().first = std::to_string(n) + ". " + who(t.player) + "  " +
                                                    std::to_string(std::max(t.d1, t.d2)) + "-" + std::to_string(std::min(t.d1, t.d2)) +
                                                    " " + tavla::diceName(t.d1, t.d2) + ": …";
        }
        return out;
    }
    bool over() const { return g_.stage() == tavla::Stage::GameOver || g_.stage() == tavla::Stage::MatchOver; }
    int actorPlayer() const {
        switch (g_.stage()) {
        case tavla::Stage::OpeningRoll: return 0; // the player throws the opening dice (the AI in the Yapay Zeka mode)
        case tavla::Stage::DoubleOffered: return g_.responder();
        case tavla::Stage::NeedRoll:
        case tavla::Stage::Moving: return g_.current();
        default: return -1;
        }
    }
    bool humanToAct() const { return actorPlayer() == 0 && !over(); }
    // ---- the player's own hand (r3d::PlayerHands via ctx_.handCue / handLead): the dice and the checkers
    float handLead(r3d::HandCueKind k) const { return ctx_.handLead ? ctx_.handLead(k) : 0.f; }
    void handCueDice(float lead) {
        r3d::HandCue c;
        if (!ctx_.handCue || !board_.diceThrowPoint(c.from, 0)) return;
        c.kind = r3d::HandCueKind::Dice;
        c.lead = lead;
        ctx_.handCue(c);
    }
    void handCueChecker(int checker, float lead) {
        r3d::HandCue c;
        if (!ctx_.handCue || !board_.checkerMoving(checker, c.from)) return;
        c.kind = r3d::HandCueKind::Give;
        c.style = 2;
        c.lead = lead;
        c.where = [this, checker](Vector3& out) { return board_.checkerMoving(checker, out); };
        ctx_.handCue(c);
    }
    bool canRoll() const {
        return !board_.animating() &&
               (g_.stage() == tavla::Stage::OpeningRoll || (g_.stage() == tavla::Stage::NeedRoll && g_.current() == 0));
    }
    void roll() {
        tavla::ActionResult r = g_.stage() == tavla::Stage::OpeningRoll ? g_.rollOpening() : g_.roll(0);
        if (!r.ok) hud_.toast(r.error, ui::pal::Bad, 2.4f);
        pump();
    }
    void undo() {
        tavla::ActionResult r = g_.undoStep(0);
        if (!r.ok) hud_.toast(r.error, ui::pal::Bad, 2.4f);
        selected_ = -1;
        pump();
    }
    void onButton(int id) {
        if (id == B_ROLL && canRoll()) roll();
        else if (id == B_UNDO && g_.canUndo() && g_.current() == 0) undo();
        else if (id == B_HINT && canHint()) showHint();
        else if (id == B_DOUBLE && canOfferDouble()) {
            const tavla::ActionResult r = g_.offerDouble(0);
            if (!r.ok) hud_.toast(r.error, ui::pal::Bad, 2.4f);
            pump();
        }
    }
    // The keyboard on the board: ←/→ choose among the points with a playable checker (left to right as the player sees
    // the board), ↑/↓ among the places the chosen one can go, Enter / Space plays it. The first key only shows the cursor.
    void keyboardMove(bool enter) {
        if (g_.stage() != tavla::Stage::Moving || g_.current() != 0 || board_.animating()) return;
        const std::vector<tavla::Step> steps = g_.legalSteps();
        if (steps.empty()) return;
        auto screenX = [&](int p) {
            if (p == r3d::Tavla3D::BAR) return 0.f;
            if (p == r3d::Tavla3D::OFF) return 10.f;
            return board_.pointLocal(p, 0).x;
        };
        std::vector<int> sources;
        for (const tavla::Step& st : steps)
            if (std::find(sources.begin(), sources.end(), st.from) == sources.end()) sources.push_back(st.from);
        std::sort(sources.begin(), sources.end(), [&](int a, int b) { return screenX(a) < screenX(b); });
        auto targetsOf = [&](int from) {
            std::vector<int> t;
            for (const tavla::Step& st : steps)
                if (st.from == from && std::find(t.begin(), t.end(), st.to) == t.end()) t.push_back(st.to);
            std::sort(t.begin(), t.end(), [&](int a, int b) { return screenX(a) < screenX(b); });
            return t;
        };
        const bool wasNav = ui::keyboardNav();
        const bool lr = ui::keyPressedRepeat(KEY_LEFT) || ui::keyPressedRepeat(KEY_RIGHT);
        const bool ud = ui::keyPressedRepeat(KEY_UP) || ui::keyPressedRepeat(KEY_DOWN);
        if (!lr && !ud && !enter) return;
        ui::noteKeyboardNav();
        auto it = std::find(sources.begin(), sources.end(), selected_);
        if (it == sources.end()) { // nothing chosen yet: the İpucu's checker, or the rightmost one
            int pick = sources.back();
            if (hint_.from != -2 && std::find(sources.begin(), sources.end(), hint_.from) != sources.end()) pick = hint_.from;
            selected_ = pick;
            const std::vector<int> t = targetsOf(selected_);
            kbTarget_ = hint_.from == selected_ && std::find(t.begin(), t.end(), hint_.to) != t.end() ? hint_.to : t.front();
            sound(ui::Sfx::TileClick);
            return;
        }
        if (lr && wasNav) {
            const int n = (int)sources.size();
            int i = (int)(it - sources.begin());
            i = (i + (ui::keyPressedRepeat(KEY_RIGHT) ? 1 : -1) + n) % n;
            selected_ = sources[(size_t)i];
            kbTarget_ = targetsOf(selected_).front();
            sound(ui::Sfx::TileClick);
        }
        std::vector<int> t = targetsOf(selected_);
        auto ti = std::find(t.begin(), t.end(), kbTarget_);
        if (ti == t.end()) {
            kbTarget_ = t.front();
            ti = t.begin();
        }
        if (ud && wasNav) {
            const int n = (int)t.size();
            int i = (int)(ti - t.begin());
            i = (i + (ui::keyPressedRepeat(KEY_UP) ? 1 : -1) + n) % n;
            kbTarget_ = t[(size_t)i];
            sound(ui::Sfx::TileClick);
        }
        if (enter && wasNav) {
            const int to = kbTarget_;
            kbTarget_ = -1;
            clickPoint(to);
        }
    }
    void clickPoint(int idx) {
        const std::vector<tavla::Step> steps = g_.legalSteps();
        auto from = [&](int f) {
            for (const tavla::Step& s : steps)
                if (s.from == f) return true;
            return false;
        };
        if (selected_ >= 0) {
            for (const tavla::Step& s : steps) {
                if (s.from != selected_ || s.to != idx) continue;
                const tavla::ActionResult r = g_.applyStep(0, s.from, s.to);
                if (!r.ok) {
                    hud_.toast(r.error, ui::pal::Bad, 2.4f);
                    sound(ui::Sfx::Error);
                }
                selected_ = -1;
                pump();
                return;
            }
        }
        if (from(idx)) {
            selected_ = idx == selected_ ? -1 : idx;
            sound(ui::Sfx::TileClick);
            return;
        }
        if (selected_ >= 0) {
            // a point the selected checker can't reach: say why
            const tavla::ActionResult r = g_.applyStep(0, selected_, idx);
            if (!r.ok) {
                hud_.toast(r.error, ui::pal::Bad, 2.4f);
                sound(ui::Sfx::Error);
            } else {
                selected_ = -1;
                pump();
            }
        }
    }
    void syncBoard(bool snap) {
        const tavla::Position& pos = g_.position();
        board_.setPosition(pos.pts, pos.bar, pos.off, snap);
    }
    void sound(ui::Sfx s) {
        if (ctx_.sfx) ctx_.sfx(s);
    }
    void say(int seat, const std::string& line, bool important = false) {
        if (ctx_.characters) ctx_.characters->chat(seat, line, important);
    }
    void pump() {
        for (const tavla::GameEvent& e : g_.drainEvents()) {
            using E = tavla::EvType;
            const bool bot = e.player == 1 || (e.player == 0 && aiSeat_);
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::GameStart:
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                marsWarned_ = false;
                break;
            case E::DoubleOffer:
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : Color{206, 176, 232, 255}, 2.6f);
                sound(ui::Sfx::TileClick);
                if (e.player == 1) {
                    say(opp_, kOppOffer[opp_][rng_.range(3)], true);
                    if (ctx_.characters) ctx_.characters->reach(opp_, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}), 0);
                } else if (rng_.chance(0.5f)) {
                    say(watcher(), "Oo, katladı! Masa ısınıyor.");
                }
                break;
            case E::DoubleTake:
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::TextLight, 2.4f);
                sfxAt_.push_back({now_ + 0.6f / speed_, ui::Sfx::Checker});
                if (e.player == 1) say(opp_, kOppTake[opp_][rng_.range(3)], true);
                else if (rng_.chance(0.6f)) say(opp_, kOppSeesTake[opp_][rng_.range(2)]);
                break;
            case E::DoubleDrop:
                hud_.toast(e.text, e.player == 0 ? ui::pal::Bad : ui::pal::Highlight, 3.f);
                if (e.player == 1) say(opp_, kOppDrop[opp_][rng_.range(3)], true);
                else say(opp_, kOppSeesDrop[opp_][rng_.range(2)], true);
                break;
            case E::OpeningRoll: {
                const float hl = handLead(r3d::HandCueKind::Dice); // (the player's own hand throws: PlayerHands)
                board_.throwDice(0, e.d1, e.d2, true, hl);
                if (hl > 0.f) sfxAt_.push_back({now_ + hl / speed_, ui::Sfx::DiceThrow});
                else sound(ui::Sfx::DiceThrow);
                handCueDice(hl);
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                break;
            }
            case E::Roll: {
                if (e.amount == 1) { // Gülbahar: the next rung of the ladder; the dice are turned, not thrown
                    board_.placeDice(e.player, e.d1, e.d2);
                    sound(ui::Sfx::TileClick);
                    hud_.toast(e.text, Color{246, 200, 90, 255}, 2.0f);
                    if (e.player == 1 && e.d1 == 6 && rng_.chance(0.5f)) say(opp_, kOppDouble[opp_][rng_.range(3)]);
                    break;
                }
                const float delay = e.player == 1 ? w3d::BOT_GIVE_LEAD : handLead(r3d::HandCueKind::Dice);
                board_.throwDice(e.player, e.d1, e.d2, false, delay);
                if (e.player == 1 && ctx_.characters) ctx_.characters->reach(opp_, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}), 1);
                if (e.player == 0 && delay > 0.f) sfxAt_.push_back({now_ + delay / speed_, ui::Sfx::DiceThrow});
                else sound(ui::Sfx::DiceThrow);
                if (e.player == 0) handCueDice(delay);
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::TextLight, 2.0f);
                if (e.player == 1 && e.d1 == e.d2 && e.d1 >= 4 && rng_.chance(0.6f))
                    say(opp_, kOppDouble[opp_][rng_.range(3)]);
                if (e.player == 0 && e.d1 == 6 && e.d2 == 6 && e.amount != 1) noteAchievement("duses"); // Başarımlar (a real throw)
                break;
            }
            case E::Step: {
                const float delay = bot && e.player == 1 ? w3d::BOT_TAKE_LEAD
                                  : e.player == 0 ? handLead(r3d::HandCueKind::Give) : 0.f;
                const int moved = board_.moveChecker(e.player, e.from, e.to, delay);
                if (e.player == 1 && ctx_.characters) ctx_.characters->reach(opp_, board_.pointWorld(e.from, 1), 0);
                if (e.player == 0) handCueChecker(moved, delay);
                sfxAt_.push_back({now_ + (delay + 0.45f) / speed_, ui::Sfx::Checker});
                break;
            }
            case E::Hit:
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::Bad, 2.4f);
                if (e.player == 1) say(opp_, kOppHit[opp_][rng_.range(3)], true);
                else if (rng_.chance(0.7f)) say(opp_, kOppHitBy[opp_][rng_.range(3)], true);
                if (rng_.chance(0.35f)) {
                    const int w = watcher();
                    say(w, kWatchHit[w][rng_.range(2)]);
                }
                if (ctx_.characters) ctx_.characters->react(opp_, e.player == 1 ? 1 : 2, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}));
                break;
            case E::BearOff: sound(ui::Sfx::Checker); break;
            case E::NoMove: hud_.toast(e.text, ui::pal::TextLight, 2.6f); break;
            case E::GameEnd: {
                hud_.toast(e.text, ui::pal::Highlight, 4.f);
                const tavla::GameResult& res = g_.lastResult();
                results_.push_back(res);
                log_ = e.text + " | " + std::to_string(g_.score(0)) + "-" + std::to_string(g_.score(1));
                if (res.mars && res.winner == 0) { // Başarımlar
                    noteAchievement("mars");
                    if (res.katmerli) noteAchievement("katmerli");
                }
                if (res.mars) {
                    // a mars is an event in the kahvehane: the big word, the line, everyone has something to say
                    hud_.banner(res.katmerli ? "KATMERLİ MARS!" : "MARS!", resultText(res), Color{255, 206, 84, 255}, 3.2f);
                    say(e.player == 1 ? opp_ : watcher(), kMarsLines[rng_.range(3)], true);
                    if (e.player == 0) say(opp_, kOppMarsLost[opp_][rng_.range(3)]);
                    else say(watcher(), kWatchMars[rng_.range(2)]);
                    sound(ui::Sfx::TileSlam);
                }
                if (ctx_.characters) ctx_.characters->react(opp_, e.player == 1 ? 1 : 2, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}));
                sound(e.player == 0 ? ui::Sfx::Win : ui::Sfx::Lose);
                break;
            }
            case E::MatchEnd:
                hud_.toast(e.text, ui::pal::Highlight, 5.f);
                if (e.player == 0 && g_.rules().variant == tavla::Variant::Gulbahar) noteAchievement("gulbahar"); // Başarımlar
                if (e.player == 0 && g_.rules().variant == tavla::Variant::Fevga) noteAchievement("fevga");
                break;
            default: break;
            }
            if (e.type == E::Step || e.type == E::Undo || e.type == E::GameStart || e.type == E::Hit) syncBoard(false);
        }
        syncBoard(false);
    }

    // One of the two regulars still at the okey table (they watch and talk).
    int watcher() {
        int w[2], n = 0;
        for (int s = 1; s <= 3; ++s)
            if (s != opp_) w[n++] = s;
        return w[rng_.range(2)];
    }

    int opp_ = 2;  // who plays player 1 at the tavla table (Settings::rakip, ui::twoPlayerOpponent)
    TableContext ctx_;
    tavla::Game g_;
    std::array<std::unique_ptr<tavla::Bot>, 2> bots_;
    r3d::Tavla3D board_;
    r3d::GameHud hud_;
    std::array<std::string, 4> names_{};
    std::vector<tavla::GameResult> results_;
    std::vector<std::pair<float, ui::Sfx>> sfxAt_;
    okey::Rng rng_{3};
    std::string log_;
    int level_ = 1;
    float speed_ = 1.f, now_ = 0.f, botWait_ = 0.f;
    int botPlayer_ = -1, botTurn_ = -1, botStage_ = -1;
    int selected_ = -1, hover_ = -1;
    int kbTarget_ = -1; // the keyboard's chosen place for the selected checker
    std::vector<int> btnIds_; // what each button of this frame's column does (Btn)
    bool hints_ = true, aiSeat_ = false, built_ = false, menuReq_ = false, aiReq_ = false;
    bool replay_ = false; // maç tekrarı (setReplayMode)
    bool histOpen_ = false, marsWarned_ = false;
    Hint hint_;
    std::string phase_;
};

} // namespace

std::unique_ptr<TableGame> makeTavlaTable() { return std::make_unique<TavlaTable>(); }

} // namespace app
