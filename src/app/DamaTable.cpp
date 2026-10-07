// Dama (Türk daması) at the two-seat table (w3d::tavlaFrame, TableGame::location() 1): you (player 0, the bottom of the
// board) against the regular chosen in Ayarlar (Settings::rakip, ui::twoPlayerOpponent; Kel Mahmut by default), across; the other two stay
// at the okey table, watch and talk. Engine dama::Game, bots dama::Bot (searching on a worker thread), board Dama3D.
#include "app/TableGame.h"
#include "core/BotStyle.h"
#include "core/Dama.h"
#include "core/DamaBot.h"
#include "core/Rng.h"
#include "r3d/Dama3D.h"
#include "r3d/GameHud.h"
#include "r3d/World.h"

#include <raymath.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>

namespace app {

namespace {

// What the opponent says, by who sits across (1 Hacı Rıza: calm, proverbs; 2 Kel Mahmut: loud, football; 3 Emekli
// Nuri: careful, "bizim zamanımızda").
const char* const kOppTakes[4][3] = {
    {},
    {"Aldım evlat, hayırlısı.", "Sabreden derviş muradına erer.", "Açıkta taş bırakma, demiştim."},
    {"Aldım abi! Gooool!", "Bu taş benim, tribünler ayakta!", "Hop, aldım! Defans nerede?"},
    {"Aldım. Bizim zamanımızda taş açıkta bırakılmazdı.", "Tecrübe, delikanlı, tecrübe.", "Hıh, aldım."},
};
const char* const kOppTakesMany[4][2] = {
    {}, {"Bereketli bir hamle oldu, maşallah.", "Bir taşla birkaç kuş, derler ya."},
    {"Hepsi gitti abi! Farklı skor!", "Hat-trick! Hepsini aldım!"}, {"Topladım hepsini. Ders olsun.", "Üç taş birden. Hıh."},
};
const char* const kOppLoses[4][3] = {
    {},
    {"Aldın ha, Allah'ın takdiri.", "Olsun, oyun daha bitmedi.", "Her inişin bir çıkışı var."},
    {"Hakem! Bu nasıl hamle!", "Ofsayttı o, ofsayt!", "Tamam tamam, rövanşı var."},
    {"Vay, aldı. Gözlüğüm buğulandı herhalde.", "Dikkatsizlik ettim, yaş.", "Aldın ha? Şansa bak."},
};
const char* const kOppKing[4][2] = {
    {}, {"Dama çıktım, elhamdülillah.", "Sabır acıdır, meyvesi dama."},
    {"Damaaa! Şampiyonluk geliyor!", "Dama oldum abi, kaçın!"}, {"Dama. Bizim zamanımızda böyle oynanırdı.", "Dama çıktım. Hıh."},
};
const char* const kOppSeesKing[4][2] = {
    {}, {"Dama çıktın ha, hayırlısı.", "Maşallah, dama oldun."},
    {"Dama mı çıktın? Hakem bakmıyor muydu?", "Ooo, dama! Maç ısındı."}, {"Dama çıktın. Gençlik.", "Hıh, dama. Görürüz."},
};
const char* const kOppWins[4][2] = {
    {}, {"Bu oyun bizim oldu, evlat.", "Nasip, bu sefer benden yana."},
    {"Kazandım abi! Çaylar senden!", "Şampiyon Mahmut!"}, {"Kazandım. Tecrübe kazandı.", "Bizim zamanımızda böyle bitirirdik."},
};
const char* const kOppLosesGame[4][2] = {
    {}, {"Tebrik ederim evlat, güzel oynadın.", "Kaybetmesini de bilmeli insan."},
    {"Rövanş! Hemen rövanş!", "Bu sayılmaz abi, ısınmadım daha."}, {"Kazandın. Gözlüğümü değiştireyim.", "Hıh. Şans."},
};
// The two at the okey table, watching (by seat).
const char* const kWatchTake[4][2] = {
    {}, {"Aldı bak, hayırlısı.", "Dama oyunu sabır oyunu."},
    {"Vay be, hepsini süpürdü!", "Gol gibi hamle!"}, {"Bizim zamanımızda böyle açık verilmezdi.", "Hıh, gördü mü alır."},
};

enum Btn { B_HINT, B_HISTORY };

using Clock = std::chrono::steady_clock;

class DamaTable final : public TableGame {
public:
    ~DamaTable() override { joinThink(); }

    bool init(const TableContext& ctx) override {
        ctx_ = ctx;
        built_ = ctx_.renderer && board_.init(*ctx_.renderer);
        board_.setFrame(w3d::tavlaFrame()); // the two-seat table
        return built_;
    }
    void shutdown() override {
        joinThink();
        if (built_ && ctx_.renderer) board_.shutdown(*ctx_.renderer);
        built_ = false;
    }
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        joinThink();
        dama::Rules r;
        r.winsNeeded = std::clamp(st.damaWins, 1, 9);
        g_ = dama::Game(r);
        names_ = names;
        opp_ = ui::twoPlayerOpponent(st, ui::GameKind::Dama); // Rakip
        g_.setPlayer(0, names[0], true);
        g_.setPlayer(1, names[opp_], false);
        bots_[0] = std::make_unique<dama::Bot>(dama::BotLevel::Hard, seed * 2 + 0xDA1ull);
        bots_[1] = std::make_unique<dama::Bot>((dama::BotLevel)std::clamp(level_, 0, 2), seed * 2 + 0xDA2ull);
        bots_[1]->setStyle(okey::BotStyle::forSeat(opp_)); // Kel Mahmut bold, Emekli Nuri careful
        rng_.reseed(seed ^ 0xDA7Aull);
        replay_ = false;
        hud_.clearToasts();
        hud_.clearBanner();
        results_.clear();
        clearSelection();
        hint_ = Hint();
        sfxAt_.clear();
        g_.startMatch(seed);
        syncBoard(true);
        pump();
    }
    void startNextHand() override {
        if (g_.stage() != dama::Stage::GameOver) return;
        joinThink();
        clearSelection();
        g_.startNextGame();
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
            else if (humanInput && !aiSeat && c.id >= 0 && c.id < (int)btnIds_.size() && btnIds_[(size_t)c.id] == B_HINT && canHint())
                showHint();
        }
        if (g_.turnNumber() != selTurn_) { // a new position: what was chosen belongs to the last one
            clearSelection();
            selTurn_ = g_.turnNumber();
        }
        const bool myMove = humanInput && !aiSeat && humanToAct() && !board_.animating();
        const bool enter = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        if (myMove && !hudKeys) keyboardMove(enter);
        if (myMove && IsKeyPressed(KEY_H) && canHint()) showHint();
        // the mouse on the board: click a disc, then a lit square; or drag the disc there
        hover_ = -1;
        Vector3 local{};
        const bool onBoard = humanInput && !aiSeat && ctx_.renderer && !hud_.mouseOverHud() &&
                             board_.pickLocal(ctx_.renderer->rayFromVirtual(mouse), local);
        if (humanInput && !aiSeat && ctx_.renderer && !hud_.mouseOverHud()) hover_ = board_.pick(ctx_.renderer->rayFromVirtual(mouse));
        if (myMove) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover_ >= 0) {
                pressAt_ = mouse;
                if (isSource(hover_) && hover_ != selected_) {
                    select(hover_);
                    dragFrom_ = hover_;
                } else if (hover_ == selected_) {
                    dragFrom_ = hover_;
                } else if (selected_ >= 0) {
                    clickTarget(hover_);
                }
            }
            if (dragFrom_ >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && onBoard &&
                (dragging_ || Vector2Distance(mouse, pressAt_) > 7.f)) {
                dragging_ = true;
                board_.setDrag(dragFrom_, local);
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (dragging_ && hover_ >= 0 && hover_ != dragFrom_) clickTarget(hover_);
                else if (dragging_ && hover_ == dragFrom_) { /* put back: stays chosen */ }
                dragging_ = false;
                dragFrom_ = -1;
                board_.setDrag(-1, {});
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) clearSelection();
        } else if (dragging_ || dragFrom_ >= 0) {
            dragging_ = false;
            dragFrom_ = -1;
            board_.setDrag(-1, {});
        }
        updateBots(dt, aiSeat);
        // the player's highlights
        std::vector<int> movable, targets, doomed;
        int focus = -1;
        if (humanToAct() && !aiSeat && !board_.animating()) {
            for (const dama::Move& m : g_.legalMoves())
                if (std::find(movable.begin(), movable.end(), m.from) == movable.end()) movable.push_back(m.from);
            if (selected_ < 0 && movable.size() == 1 && g_.legalMoves().front().isCapture()) select(movable[0]); // forced
            if (selected_ >= 0) targets = targetSquares();
            // the move the keyboard points at, or the one under the mouse
            const dama::Move* shown = nullptr;
            const std::vector<const dama::Move*> mine = movesOfSelected();
            if (ui::keyboardNav() && kbMove_ >= 0 && kbMove_ < (int)mine.size()) shown = mine[(size_t)kbMove_];
            else if (hover_ >= 0) shown = moveForTarget(hover_);
            if (!shown && hint_.turn == g_.turnNumber() && selected_ == hint_.move.from) shown = findLegal(hint_.move);
            if (shown) {
                doomed = shown->captured;
                focus = prefix_.size() >= shown->path.size() || finalUnique(*shown) ? shown->to() : shown->path[prefix_.size()];
            }
            if (!hints_ && selected_ < 0) movable.clear();
        }
        board_.setHighlights(movable, selected_, targets);
        board_.setKeyFocus(focus);
        board_.setDoomed(doomed);
    }

    void submit(r3d::Renderer& r) override { board_.submit(r); }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        const bool aiTag = aiSeat && !replay_;
        const bool sen = replay_ && names_[0] == "Sen";
        const std::string me = replay_ ? names_[0] : std::string("Yapay zeka");
        aiSeat = aiSeat || replay_;
        const dama::Board& b = g_.board();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        for (int s = 1; s < 4; ++s) heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
        r3d::GameHud::Plate& p = plates[(size_t)opp_];
        p.show = true;
        p.name = names_[(size_t)opp_];
        p.label = "Oyun";
        p.value = std::to_string(g_.score(1));
        p.turn = g_.current() == 1 && playing();
        if (g_.stage() != dama::Stage::NotStarted) {
            p.badges.push_back({g_.starter() == 1 ? "Beyaz" : "Siyah", g_.starter() == 1 ? Color{150, 132, 100, 255} : Color{60, 40, 30, 255}});
            p.badges.push_back({"Taş " + std::to_string(b.pieces(1)), Color{70, 60, 50, 255}});
            if (b.kings(1) > 0) p.badges.push_back({"Dama " + std::to_string(b.kings(1)), Color{150, 104, 30, 255}});
        }
        hud_.plates(r, heads, plates);

        std::string st;
        Color sc = ui::pal::TextLight;
        const std::vector<dama::Move>& legal = g_.legalMoves();
        const bool mustTake = !legal.empty() && legal[0].isCapture();
        const int most = mustTake ? (int)legal[0].captured.size() : 0;
        if (g_.stage() == dama::Stage::GameOver) st = "Oyun bitti";
        else if (g_.stage() == dama::Stage::MatchOver) st = "Maç bitti";
        else if (g_.current() == 0 && aiSeat) st = sen ? std::string("Sıra sende") : me + " düşünüyor…";
        else if (g_.current() == 0) {
            sc = ui::pal::Highlight;
            if (mustTake) st = most > 1 ? "Almak zorunlu: " + std::to_string(most) + " taş alan yolu seç" : std::string("Almak zorunlu: taşı al");
            else st = selected_ < 0 ? std::string("Sıra sende: bir taş seç") : std::string(ui::colorBlind() ? "Nereye? Çizgili mavi kareye götür" : "Nereye? Yeşil kareye götür");
            if (selected_ >= 0 && !prefix_.empty()) st = "Devam et: bir sonraki durak";
        } else {
            st = names_[(size_t)opp_] + " düşünüyor…";
        }
        std::vector<std::pair<std::string, Color>> parts;
        const Color sep{226, 216, 196, 120};
        if (g_.stage() != dama::Stage::NotStarted) {
            parts.push_back({std::string(g_.starter() == 0 ? "Beyazlar" : "Siyahlar") + " sende", Color{226, 216, 196, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
            parts.push_back({"Taş " + std::to_string(b.pieces(0)) + " - " + std::to_string(b.pieces(1)), Color{226, 216, 196, 255}});
            parts.push_back({"  \xC2\xB7  ", sep});
            if (g_.quietPlies() >= 30 && playing()) {
                parts.push_back({"Beraberliğe " + std::to_string((g_.rules().noProgressPlies - g_.quietPlies() + 1) / 2) + " hamle",
                                 Color{246, 200, 90, 255}});
                parts.push_back({"  \xC2\xB7  ", sep});
            }
        }
        parts.push_back({"Oyun " + std::to_string(g_.score(0)) + " - " + std::to_string(g_.score(1)) + " (" +
                             std::to_string(g_.winsNeeded()) + "'e)", // (1'e, 3'e, 5'e)
                         Color{238, 198, 112, 255}});
        hud_.status(st, sc, humanToAct() && !aiSeat, parts, aiTag);

        std::vector<std::string> labels;
        std::vector<bool> en, glow;
        btnIds_.clear();
        auto add = [&](int id, const char* label, bool e) {
            btnIds_.push_back(id);
            labels.push_back(label);
            en.push_back(e);
            glow.push_back(false);
        };
        add(B_HINT, "\xC4\xB0pucu", !aiSeat && canHint());
        add(B_HISTORY, "Hamleler", true);
        hud_.buttons(mouse, labels, en, glow, aiTag);
        if (histOpen_) hud_.listPanel(mouse, "Hamleler \xC2\xB7 " + std::to_string(g_.gameIndex() + 1) + ". oyun", historyLines(aiTag));
        {
            std::string help = "Tab düğmeler  ·  Y yapay zeka  ·  Esc menü";
            if (replay_) help = "Maç tekrarı  ·  Esc menü";
            else if (hud_.buttonFocused()) help = "Tab / Shift+Tab düğme seç  ·  Enter / Boşluk bas  ·  Oklar tahtaya dön  ·  Esc menü";
            else if (!aiSeat && humanToAct())
                help = "Sol / Sağ taş seç  ·  Yukarı / Aşağı nereye  ·  Enter / Boşluk oyna  ·  H ipucu  ·  Esc menü";
            hud_.keyHelp(help);
        }
        hud_.toasts();
        phase_.clear();
        if (!board_.animating() && ui::keyboardNav() && !aiSeat && humanToAct() && selected_ >= 0) phase_ = "klavye_hamle";
        else if (!board_.animating() && humanToAct() && !aiSeat) {
            if (mustTake && most >= 2) phase_ = "coklu_alma";
            else if (mustTake) phase_ = "alma_zorunlu";
            else if (b.kings(0) + b.kings(1) > 0) phase_ = "dama";
            else if (histOpen_ && g_.gameLog().size() >= 12) phase_ = "hamleler";
        }
    }

    bool handOver() const override { return over(); }
    bool matchOver() const override { return g_.stage() == dama::Stage::MatchOver; }
    bool animating() const override { return board_.animating(); }
    int activeSeat() const override {
        if (!playing()) return -1;
        return g_.current() == 0 ? 0 : opp_;
    }
    bool mouseBusy() const override { return hover_ >= 0 || dragging_ || hud_.mouseOverHud(); }
    std::vector<int> seats() const override { return {0, opp_}; }
    int location() const override { return 1; }
    void setLevel(int level) override {
        level_ = std::clamp(level, 0, 2);
        if (bots_[1] && !thinking_) bots_[1]->setLevel((dama::BotLevel)level_); // (else when it next starts thinking)
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
        if (g_.stage() == dama::Stage::NotStarted) return false;
        lines.clear();
        for (const dama::LoggedAction& a : g_.actionLog()) lines.push_back(a.encode());
        return true;
    }
    bool restoreState(const std::vector<std::string>& lines) override {
        joinThink();
        for (const std::string& line : lines) {
            dama::LoggedAction a;
            if (!dama::LoggedAction::decode(line, a) || !g_.replay(a)) return false;
            for (const dama::GameEvent& e : g_.drainEvents()) {
                if (e.type == dama::EvType::GameEnd) {
                    results_.push_back(g_.lastResult());
                    log_ = e.text + " | " + std::to_string(g_.score(0)) + "-" + std::to_string(g_.score(1));
                }
            }
        }
        hud_.clearToasts();
        hud_.clearBanner();
        sfxAt_.clear();
        clearSelection();
        hint_ = Hint();
        botStage_ = -1;
        syncBoard(true);
        hud_.toast(std::to_string(g_.gameIndex() + 1) + ". oyundan devam: " + std::to_string(g_.score(0)) + " - " +
                       std::to_string(g_.score(1)),
                   ui::pal::Highlight, 3.f);
        return true;
    }
    bool setReplayMode(bool on) override {
        joinThink();
        replay_ = on;
        if (on) aiSeat_ = true;
        clearSelection();
        hover_ = -1;
        hint_ = Hint();
        botStage_ = -1;
        return true;
    }
    int replayStep(const std::string& line) override {
        if (!replay_) return -1;
        dama::LoggedAction a;
        if (!dama::LoggedAction::decode(line, a)) return -1;
        if (a.kind == dama::ActKind::NextGame) {
            if (g_.stage() == dama::Stage::GameOver) return 0;
            return g_.stage() == dama::Stage::MatchOver ? -1 : 1;
        }
        if (board_.animating()) return 0;
        if (!g_.replay(a)) return -1;
        pump();
        return 1;
    }
    bool humanHandScore(int& score) const override {
        if (!over() || g_.lastResult().winner != 0) return false;
        score = g_.lastResult().left[0]; // a won game: the pieces still standing
        return true;
    }
    bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const override {
        if (board_.animating() || !humanToAct() || !playing()) return false;
        const std::vector<dama::Move>& legal = g_.legalMoves();
        if (legal.empty()) return false;
        int target = -1;
        if (selected_ < 0) {
            target = legal[0].from;
        } else {
            const std::vector<int> t = targetSquares();
            if (t.empty()) target = legal[0].from;
            else target = t.front();
        }
        return r.projectToVirtual(board_.squareWorld(target), out);
    }
    std::string debugPhase() const override { return phase_; }

    std::string scoreTitle() const override {
        if (g_.stage() == dama::Stage::NotStarted) return "";
        return "Dama \xC2\xB7 " + std::to_string(g_.winsNeeded()) + " oyun";
    }
    std::vector<std::string> scoreLines() const override {
        return {names_[0] + " ....... " + std::to_string(g_.score(0)), names_[(size_t)opp_] + " ....... " + std::to_string(g_.score(1))};
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        const std::string oppName = names_[(size_t)opp_];
        m.columns = {aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[0], oppName};
        m.humanCol = 0;
        const int games = (int)results_.size();
        m.title = std::to_string(games) + ". Oyun";
        m.corner = "dama: " + std::to_string(g_.winsNeeded()) + " oyuna";
        if (!results_.empty()) {
            const dama::GameResult& res = results_.back();
            const bool won = res.winner == 0;
            if (res.winner < 0) m.headline = "Berabere!";
            else if (won) m.headline = aiMode ? std::string("Yapay zeka oyunu aldı!") : std::string("Oyunu aldın!");
            else m.headline = oppName + " oyunu aldı.";
            m.tagline = reasonLine(res, aiMode);
            m.taglineRed = res.winner >= 0 && res.left[(size_t)res.winner] >= 8; // a rout: the room notices
            m.starCol = res.winner;
            m.rowLabels = {"Sonuç", "Kalan taş", "Dama"};
            const std::string w = "Kazandı", d = "Berabere";
            m.cells = {{res.winner == 0 ? w : res.winner < 0 ? d : "", res.winner == 1 ? w : res.winner < 0 ? d : ""},
                       {std::to_string(res.left[0]), std::to_string(res.left[1])},
                       {std::to_string(res.kingsLeft[0]), std::to_string(res.kingsLeft[1])}};
        }
        for (size_t i = 0; i < results_.size(); ++i) {
            const dama::GameResult& r = results_[i];
            m.historyLabels.push_back(std::to_string(i + 1) + ". oyun" + (r.winner < 0 ? " (berabere)" : ""));
            m.history.push_back({r.winner == 0 ? "+1" : r.winner < 0 ? "=" : "", r.winner == 1 ? "+1" : r.winner < 0 ? "=" : ""});
        }
        m.totalLabel = "Oyun";
        m.totals = {std::to_string(g_.score(0)), std::to_string(g_.score(1))};
        if (g_.score(0) >= g_.score(1)) m.leaders.push_back(0);
        if (g_.score(1) >= g_.score(0)) m.leaders.push_back(1);
        m.last = g_.stage() == dama::Stage::MatchOver;
        m.note = m.last ? "" : std::to_string(g_.winsNeeded()) + " oyunu ilk alan maçı kazanır";
        m.matchHeader = "Maç Bitti";
        m.playedLine = std::to_string(games) + " oyun oynandı";
        const int w = g_.matchWinner();
        if (w < 0 && m.last) {
            m.ranking = {0, 1};
            m.rank = {1, 1};
        } else {
            const int ww = w >= 0 ? w : (g_.score(0) >= g_.score(1) ? 0 : 1);
            m.ranking = {ww, 1 - ww};
            m.rank = {1, 2};
        }
        auto sub = [&](int p) {
            return std::to_string(g_.score(p)) + " oyun, " + std::to_string(g_.player(p).taken) + " taş aldı";
        };
        m.rankSub = {sub(0), sub(1)};
        m.humanWon = w == 0;
        // (the ablative of the three names: Rıza'dan, Mahmut'tan, Nuri'den)
        const char* abl = opp_ == 1 ? "'dan" : opp_ == 2 ? "'tan" : "'den";
        if (w == 0) m.resultTitle = std::string(aiMode ? "Yapay zeka kazandı! Çaylar " : "Kazandın! Çaylar ") + oppName + abl + "!";
        else if (w < 0) m.resultTitle = "Berabere! Bir maç daha?";
        else m.resultTitle = oppName + " aldı, bir maç daha?";
        m.resultSub = std::to_string(g_.score(0)) + " - " + std::to_string(g_.score(1));
        return m;
    }

private:
    // ---- the player's choice on the board ----
    // selected_: the chosen disc; prefix_: intermediate stops clicked along a capture that is not yet decided (two
    // capture routes ending on the same square); kbMove_: the keyboard's move among the chosen disc's moves.
    void clearSelection() {
        selected_ = -1;
        prefix_.clear();
        kbMove_ = -1;
        dragging_ = false;
        dragFrom_ = -1;
        board_.setDrag(-1, {});
    }
    void select(int s) {
        selected_ = s;
        prefix_.clear();
        kbMove_ = 0;
        // the İpucu's move first for the keyboard
        const std::vector<const dama::Move*> mine = movesOfSelected();
        if (hint_.turn == g_.turnNumber())
            for (size_t i = 0; i < mine.size(); ++i)
                if (mine[i]->sameAs(hint_.move)) kbMove_ = (int)i;
        sound(ui::Sfx::TileClick);
    }
    bool isSource(int s) const {
        for (const dama::Move& m : g_.legalMoves())
            if (m.from == s) return true;
        return false;
    }
    // The chosen disc's moves that agree with the stops clicked so far, in the screen's order of their end squares.
    std::vector<const dama::Move*> movesOfSelected() const {
        std::vector<const dama::Move*> out;
        if (selected_ < 0) return out;
        for (const dama::Move& m : g_.legalMoves()) {
            if (m.from != selected_ || m.path.size() < prefix_.size()) continue;
            if (!std::equal(prefix_.begin(), prefix_.end(), m.path.begin())) continue;
            out.push_back(&m);
        }
        std::stable_sort(out.begin(), out.end(), [](const dama::Move* a, const dama::Move* b) {
            const int ka = dama::colOf(a->to()) * 8 + dama::rowOf(a->to()), kb = dama::colOf(b->to()) * 8 + dama::rowOf(b->to());
            return ka < kb;
        });
        return out;
    }
    // A candidate's end square is enough to tell it from the others.
    bool finalUnique(const dama::Move& m) const {
        int n = 0;
        for (const dama::Move* o : movesOfSelected()) n += o->to() == m.to() ? 1 : 0;
        return n == 1;
    }
    // Where the chosen disc may be taken now: the end squares that decide a move, else the next stop of the routes
    // that share an end square.
    std::vector<int> targetSquares() const {
        std::vector<int> t;
        for (const dama::Move* m : movesOfSelected()) {
            const int s = finalUnique(*m) || m->path.size() <= prefix_.size() + 1 ? m->to() : m->path[prefix_.size()];
            if (std::find(t.begin(), t.end(), s) == t.end()) t.push_back(s);
        }
        return t;
    }
    const dama::Move* moveForTarget(int s) const {
        const dama::Move* found = nullptr;
        int n = 0;
        for (const dama::Move* m : movesOfSelected())
            if (m->to() == s) {
                found = m;
                ++n;
            }
        return n == 1 ? found : nullptr;
    }
    const dama::Move* findLegal(const dama::Move& mv) const {
        for (const dama::Move& m : g_.legalMoves())
            if (m.sameAs(mv)) return &m;
        return nullptr;
    }
    void clickTarget(int s) {
        if (selected_ < 0) return;
        if (const dama::Move* m = moveForTarget(s)) {
            play(*m);
            return;
        }
        // a stop along routes that end on the same square
        bool step = false;
        for (const dama::Move* m : movesOfSelected())
            if (m->path.size() > prefix_.size() && m->path[prefix_.size()] == s) step = true;
        if (step) {
            prefix_.push_back(s);
            kbMove_ = 0;
            sound(ui::Sfx::TileClick);
            const std::vector<const dama::Move*> left = movesOfSelected();
            for (const dama::Move* m : left)
                if (m->path.size() == prefix_.size() && left.size() == 1) {
                    play(*m);
                    return;
                }
            return;
        }
        if (isSource(s)) { // another disc of ours
            select(s);
            return;
        }
        // not a legal target: say why
        dama::Move bad;
        bad.from = selected_;
        bad.path = prefix_;
        bad.path.push_back(s);
        dama::Game probe = g_;
        const dama::ActionResult r = probe.applyMove(0, bad);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.4f);
            sound(ui::Sfx::Error);
        }
    }
    void play(const dama::Move& m) {
        const dama::Move mv = m; // (the reference lives in the legal list the move replaces)
        const dama::ActionResult r = g_.applyMove(0, mv);
        if (!r.ok) {
            hud_.toast(r.error, ui::pal::Bad, 2.4f);
            sound(ui::Sfx::Error);
        }
        clearSelection();
        pump();
    }
    // The keyboard: ←/→ choose among the discs that can move (left to right as the player sees the board), ↑/↓ among
    // the chosen disc's moves, Enter / Space plays it. The first key only shows the cursor.
    void keyboardMove(bool enter) {
        const bool lr = ui::keyPressedRepeat(KEY_LEFT) || ui::keyPressedRepeat(KEY_RIGHT);
        const bool ud = ui::keyPressedRepeat(KEY_UP) || ui::keyPressedRepeat(KEY_DOWN);
        if (!lr && !ud && !enter) return;
        std::vector<int> sources;
        for (const dama::Move& m : g_.legalMoves())
            if (std::find(sources.begin(), sources.end(), m.from) == sources.end()) sources.push_back(m.from);
        if (sources.empty()) return;
        std::sort(sources.begin(), sources.end(), [](int a, int b) {
            return dama::colOf(a) != dama::colOf(b) ? dama::colOf(a) < dama::colOf(b) : dama::rowOf(a) < dama::rowOf(b);
        });
        const bool wasNav = ui::keyboardNav();
        ui::noteKeyboardNav();
        auto it = std::find(sources.begin(), sources.end(), selected_);
        if (it == sources.end()) { // nothing chosen yet: the İpucu's disc, or the first one
            int pick = sources.front();
            if (hint_.turn == g_.turnNumber() && std::find(sources.begin(), sources.end(), hint_.move.from) != sources.end())
                pick = hint_.move.from;
            select(pick);
            return;
        }
        prefix_.clear();
        if (lr && wasNav) {
            const int n = (int)sources.size();
            int i = (int)(it - sources.begin());
            i = (i + (ui::keyPressedRepeat(KEY_RIGHT) ? 1 : -1) + n) % n;
            select(sources[(size_t)i]);
            return;
        }
        const std::vector<const dama::Move*> mine = movesOfSelected();
        if (mine.empty()) return;
        if (kbMove_ < 0 || kbMove_ >= (int)mine.size()) kbMove_ = 0;
        if (ud && wasNav) {
            const int n = (int)mine.size();
            kbMove_ = (kbMove_ + (ui::keyPressedRepeat(KEY_UP) ? 1 : -1) + n) % n;
            sound(ui::Sfx::TileClick);
        }
        if (enter && wasNav) play(*mine[(size_t)kbMove_]);
    }

    // ---- İpucu: what Kurt would play in the player's place ----
    struct Hint {
        dama::Move move;
        int turn = -1;
    };
    bool canHint() const { return humanToAct() && playing() && !board_.animating() && !thinking_; }
    void showHint() {
        dama::Bot kurt(dama::BotLevel::Hard, 0x41D7ull);
        const dama::Move m = kurt.choose(g_, 0);
        if (m.path.empty()) return;
        hint_.move = m;
        hint_.turn = g_.turnNumber();
        select(m.from);
        std::string text = dama::moveNotation(m);
        if (m.isCapture()) text += " (" + std::to_string(m.captured.size()) + " taş alır)";
        else if (m.promotes) text += " (dama çıkar)";
        hud_.toast("\xC4\xB0pucu: " + text, Color{150, 220, 160, 255}, 4.f);
        sound(ui::Sfx::Button);
    }

    // ---- the bots: Mahmut, and our seat in the Yapay Zeka mode; Kurt thinks on a worker thread ----
    void updateBots(float dt, bool aiSeat) {
        if (thinking_) {
            if (think_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            const dama::Move m = think_.get();
            thinking_ = false;
            // still the same position (nothing else moved meanwhile): play it after the pause is over
            if (thinkTurn_ == g_.turnNumber() && g_.current() == thinkPlayer_ && playing()) {
                pendingMove_ = m;
                hasPending_ = true;
            }
        }
        if (replay_ || board_.animating() || !playing()) return;
        const int p = g_.current();
        if (!(p == 1 || aiSeat)) {
            hasPending_ = false;
            return;
        }
        if (botPlayer_ != p || botTurn_ != g_.turnNumber() || botStage_ != 1) {
            botPlayer_ = p;
            botTurn_ = g_.turnNumber();
            botStage_ = 1;
            hasPending_ = false;
            botWait_ = (g_.plies() == 0 ? 1.0f : 0.55f) / speed_;
            // start thinking at once (the pause hides the search)
            if (g_.legalMoves().size() > 1) {
                thinkPlayer_ = p;
                thinkTurn_ = g_.turnNumber();
                thinking_ = true;
                bots_[1]->setLevel((dama::BotLevel)level_);
                dama::Bot* bot = bots_[(size_t)p].get();
                const dama::Game copy = g_;
                think_ = std::async(std::launch::async, [bot, copy, p]() { return bot->choose(copy, p); });
                return;
            }
            pendingMove_ = g_.legalMoves().front();
            hasPending_ = true;
        }
        botWait_ -= dt;
        if (botWait_ > 0.f || !hasPending_) return;
        hasPending_ = false;
        dama::Move m = pendingMove_;
        if (!g_.applyMove(p, m).ok) g_.applyMove(p, g_.legalMoves().front());
        botStage_ = -1;
        pump();
    }
    void joinThink() {
        if (thinking_) {
            think_.wait();
            (void)think_.get();
            thinking_ = false;
        }
        hasPending_ = false;
        botStage_ = -1;
    }

    // ---- what the board shows ----
    void syncBoard(bool snap) { board_.setBoard(g_.board().c, g_.starter(), snap); }
    void sound(ui::Sfx s) {
        if (ctx_.sfx) ctx_.sfx(s);
    }
    void say(int seat, const std::string& line, bool important = false) {
        if (ctx_.characters) ctx_.characters->chat(seat, line, important);
    }
    int watcher() {
        int w[2], n = 0;
        for (int s = 1; s <= 3; ++s)
            if (s != opp_) w[n++] = s;
        return w[rng_.range(2)];
    }
    std::string reasonLine(const dama::GameResult& r, bool aiMode) const {
        const std::string you = aiMode ? std::string("yapay zekanın") : std::string("senin");
        const std::string theirs = dama::genitive(names_[(size_t)opp_]);
        switch (r.reason) {
        case dama::EndReason::NoPieces: return std::string(r.winner == 0 ? theirs : you) + " taşı kalmadı";
        case dama::EndReason::Blocked: return std::string(r.winner == 0 ? theirs : you) + " oynayacak hamlesi kalmadı";
        default: return dama::endReasonText(r.reason);
        }
    }
    std::vector<std::pair<std::string, Color>> historyLines(bool aiSeat) const {
        std::vector<std::pair<std::string, Color>> out;
        const Color mine{255, 204, 92, 255}, his{206, 198, 182, 225};
        int n = 0;
        for (const dama::MoveRecord& r : g_.gameLog()) {
            const std::string who = r.player == 0 ? (aiSeat ? std::string("Yapay zeka") : names_[0]) : names_[(size_t)opp_];
            std::string t = std::to_string(++n) + ". " + who + "  " + dama::moveNotation(r.move);
            if (r.move.captured.size() > 1) t += "  (" + std::to_string(r.move.captured.size()) + " taş)";
            out.push_back({t, r.player == 0 ? mine : his});
        }
        return out;
    }
    bool playing() const { return g_.stage() == dama::Stage::Playing; }
    bool over() const { return g_.stage() == dama::Stage::GameOver || g_.stage() == dama::Stage::MatchOver; }
    bool humanToAct() const { return playing() && g_.current() == 0; }

    void pump() {
        for (const dama::GameEvent& e : g_.drainEvents()) {
            using E = dama::EvType;
            const bool bot = e.player == 1 || (e.player == 0 && aiSeat_);
            switch (e.type) {
            case E::MatchStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::GameStart:
                hud_.toast(e.text, ui::pal::Highlight, 3.f);
                syncBoard(false); // the discs go back to their rows (the colours change sides every game)
                sfxAt_.push_back({now_ + 0.5f / speed_, ui::Sfx::Checker});
                break;
            case E::Move: {
                const float delay = bot && e.player == 1 ? w3d::BOT_TAKE_LEAD : 0.f;
                // the opponent's own hand (Characters::carry): takes the disc up, carries it hopping over the ones it
                // takes, puts it down, then lifts the taken ones off the board one by one
                const bool byHand = e.player == 1 && ctx_.characters;
                r3d::PieceCarry hand;
                board_.playMove(e.player, e.from, e.path, e.captured, e.promotes, delay, byHand ? &hand : nullptr);
                if (byHand) ctx_.characters->carry(opp_, hand);
                // a "tok" at every landing; the taken discs click as they are lifted off (by hand: as they are dropped)
                const float seg = byHand ? hand.seg : e.captured.empty() ? 0.45f : 0.34f;
                for (size_t k = 0; k < e.path.size(); ++k)
                    sfxAt_.push_back({now_ + (delay + seg * (float)(k + 1)) / speed_, ui::Sfx::Checker});
                for (size_t k = 0; k < e.captured.size(); ++k) {
                    const float at = byHand && k < hand.takes.size() ? hand.takes[k].at + hand.takes[k].dur
                                                                     : delay + seg * (float)(k + 1) + 0.3f;
                    sfxAt_.push_back({now_ + at / speed_, ui::Sfx::TileClick});
                }
                if (e.player == 0 && !replay_ && e.captured.size() >= 3) noteAchievement("dama_uclu"); // Başarımlar
                if (!e.captured.empty()) {
                    const int n = (int)e.captured.size();
                    hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::Bad, 2.2f);
                    if (e.player == 1) say(opp_, n >= 3 ? kOppTakesMany[opp_][rng_.range(2)] : kOppTakes[opp_][rng_.range(3)], n >= 2);
                    else if (rng_.chance(0.6f)) say(opp_, kOppLoses[opp_][rng_.range(3)], n >= 2);
                    if (n >= 3 || rng_.chance(0.15f)) {
                        const int w = watcher();
                        say(w, kWatchTake[w][rng_.range(2)]);
                    }
                    if (ctx_.characters && n >= 2)
                        ctx_.characters->react(opp_, e.player == 1 ? 1 : 2, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}));
                    if (n >= 4 && ctx_.characters)
                        ctx_.characters->crowdReact(e.player == 0 ? r3d::Characters::CrowdCheer : r3d::Characters::CrowdLaugh,
                                                    board_.toWorld({0.f, w3d::TABLE_Y + 0.2f, 0.f}), 0.6f);
                }
                break;
            }
            case E::Promote:
                if (e.player == 0 && !replay_) noteAchievement("dama"); // Başarımlar
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : Color{246, 200, 90, 255}, 2.6f);
                if (e.player == 1) say(opp_, kOppKing[opp_][rng_.range(2)], true);
                else if (rng_.chance(0.7f)) say(opp_, kOppSeesKing[opp_][rng_.range(2)]);
                break;
            case E::GameEnd: {
                const dama::GameResult& res = g_.lastResult();
                results_.push_back(res);
                log_ = e.text + " | " + std::to_string(g_.score(0)) + "-" + std::to_string(g_.score(1));
                hud_.toast(e.text, ui::pal::Highlight, 4.f);
                if (res.winner >= 0 && res.left[(size_t)res.winner] >= 8)
                    hud_.banner("TEMİZ OYUN!", std::to_string(res.left[(size_t)res.winner]) + " taşla kazandı", Color{255, 206, 84, 255}, 3.f);
                if (res.winner == 1) say(opp_, kOppWins[opp_][rng_.range(2)], true);
                else if (res.winner == 0) say(opp_, kOppLosesGame[opp_][rng_.range(2)], true);
                else say(opp_, "Berabere. Olsun, bir daha oynarız.", true);
                if (ctx_.characters && res.winner >= 0)
                    ctx_.characters->react(opp_, res.winner == 1 ? 1 : 2, board_.toWorld({0.f, w3d::TABLE_Y, 0.f}));
                sound(res.winner == 0 ? ui::Sfx::Win : res.winner == 1 ? ui::Sfx::Lose : ui::Sfx::Button);
                break;
            }
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            }
        }
        syncBoard(false);
    }

    int opp_ = 2;
    TableContext ctx_;
    dama::Game g_;
    std::array<std::unique_ptr<dama::Bot>, 2> bots_;
    r3d::Dama3D board_;
    r3d::GameHud hud_;
    std::array<std::string, 4> names_{};
    std::vector<dama::GameResult> results_;
    std::vector<std::pair<float, ui::Sfx>> sfxAt_;
    okey::Rng rng_{5};
    std::string log_;
    int level_ = 1;
    float speed_ = 1.f, now_ = 0.f, botWait_ = 0.f;
    int botPlayer_ = -1, botTurn_ = -1, botStage_ = -1;
    // the worker thread's search
    std::future<dama::Move> think_;
    bool thinking_ = false, hasPending_ = false;
    int thinkPlayer_ = -1, thinkTurn_ = -1;
    dama::Move pendingMove_;
    // the player's choice
    int selected_ = -1, hover_ = -1, kbMove_ = -1, selTurn_ = -1;
    std::vector<int> prefix_;
    bool dragging_ = false;
    int dragFrom_ = -1;
    Vector2 pressAt_{};
    std::vector<int> btnIds_;
    bool hints_ = true, aiSeat_ = false, built_ = false, menuReq_ = false, aiReq_ = false;
    bool replay_ = false, histOpen_ = false;
    Hint hint_;
    std::string phase_;
};

} // namespace

std::unique_ptr<TableGame> makeDamaTable() { return std::make_unique<DamaTable>(); }

} // namespace app
