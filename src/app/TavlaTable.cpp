// Tavla at our table: you (seat 0, the bottom of the board) against Kel Mahmut (seat 2, across); Hacı Rıza and
// Emekli Nuri watch and talk. Engine tavla::Game (player 0 = you, player 1 = Mahmut), bots tavla::Bot, Tavla3D.
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

constexpr int OPP_SEAT = 2; // Kel Mahmut plays player 1

const char* const kMahmutHit[] = {"Kırdım abi! Gooool!", "Pulun gitti, kapıdan gir bakalım!", "Vurdum, tribünler ayağa!"};
const char* const kMahmutHitBy[] = {"Hakem! Bu nasıl zar!", "Kırdın mı? Rövanş var!", "Ofsayttı o, ofsayt!"};
const char* const kMahmutDouble[] = {"Çift geldi abi, kaçın!", "Gördün mü zarı? Şampiyon zarı!", "Dubeş gibisi yok!"};
const char* const kWatchHit[3][2] = {
    {"Kırdı bak, hayırlısı.", "Kırık pul, kırık kalp…"},
    {},
    {"Bizim zamanımızda böyle açık bırakılmazdı.", "Hıh, açık pul gördü mü vurur."},
};
const char* const kMarsLines[] = {"Mars! Çift yazın!", "Bu mars tarihe geçer!", "Mars oldun, çaylar senden!"};

class TavlaTable final : public TableGame {
public:
    bool init(const TableContext& ctx) override {
        ctx_ = ctx;
        built_ = ctx_.renderer && board_.init(*ctx_.renderer);
        return built_;
    }
    void shutdown() override {
        if (built_ && ctx_.renderer) board_.shutdown(*ctx_.renderer);
        built_ = false;
    }
    void startMatch(const ui::Settings& st, const std::array<std::string, 4>& names, uint64_t seed) override {
        tavla::Rules r;
        r.matchPoints = std::clamp(st.tavlaPoints, 1, 15);
        g_ = tavla::Game(r);
        names_ = names;
        g_.setPlayer(0, names[0], true);
        g_.setPlayer(1, names[OPP_SEAT], false);
        bots_[0] = std::make_unique<tavla::Bot>(tavla::BotLevel::Hard, seed * 2 + 0x7A1ull);
        bots_[1] = std::make_unique<tavla::Bot>((tavla::BotLevel)std::clamp(level_, 0, 2), seed * 2 + 0x7A2ull);
        rng_.reseed(seed ^ 0x7A7Aull);
        hud_.clearToasts();
        results_.clear();
        selected_ = -1;
        board_.hideDice();
        g_.startMatch(seed);
        syncBoard(true);
        pump();
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
        for (const r3d::GameHud::Click& c : hud_.takeClicks()) {
            if (c.id == r3d::GameHud::AI_ID) aiReq_ = true;
            else if (c.id == r3d::GameHud::MENU_ID) menuReq_ = true;
            else if (humanInput && !aiSeat) onButton(c.id);
        }
        const bool myMove = humanInput && !aiSeat && humanToAct();
        if (myMove && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) && canRoll()) roll();
        if (myMove && IsKeyPressed(KEY_BACKSPACE) && g_.canUndo() && g_.current() == 0) undo();
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
        if (!board_.animating() && !over()) {
            const int p = actorPlayer();
            if (p >= 0 && (p == 1 || aiSeat)) {
                if (botPlayer_ != p || botTurn_ != g_.turnNumber() || botStage_ != (int)g_.stage()) {
                    botPlayer_ = p;
                    botTurn_ = g_.turnNumber();
                    botStage_ = (int)g_.stage();
                    botWait_ = (g_.stage() == tavla::Stage::Moving ? (g_.turnSteps().empty() ? 0.7f : 0.45f) : 0.8f) / speed_;
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
        board_.setHighlights(targets, sources, selected_);
        const tavla::Dice& d = g_.dice();
        if (d.n > 0) board_.setDiceUsed(d.used, d.n, d.isDouble());
    }

    void submit(r3d::Renderer& r) override { board_.submit(r); }

    void drawHUD(const r3d::Renderer& r, Vector2 mouse, bool aiSeat) override {
        hud_.beginFrame();
        std::array<Vector3, 4> heads{};
        std::array<r3d::GameHud::Plate, 4> plates{};
        for (int s = 1; s < 4; ++s) heads[(size_t)s] = ctx_.characters ? ctx_.characters->headPosition(s) : Vector3{};
        r3d::GameHud::Plate& p = plates[OPP_SEAT];
        p.show = true;
        p.name = names_[OPP_SEAT];
        p.label = "Sayı";
        p.value = std::to_string(g_.score(1));
        p.turn = g_.current() == 1 && !over();
        if (hints_ && g_.stage() != tavla::Stage::NotStarted) p.badges.push_back({"Pip " + std::to_string(g_.pipCount(1)), Color{70, 60, 50, 255}});
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
        } else if (g_.current() == 0 && aiSeat) st = "Yapay zeka oynuyor…";
        else if (g_.current() == 0) {
            sc = ui::pal::Highlight;
            if (g_.stage() == tavla::Stage::NeedRoll) st = "Sıra sende: zarı at";
            else {
                std::string left;
                for (int v : d.left()) left += (left.empty() ? "" : "-") + std::to_string(v);
                st = selected_ < 0 ? "Bir pul seç (" + left + ")" : "Nereye? (" + left + ")";
                if (g_.barCount(0) > 0) st = "Kırık pulunu gir (" + left + ")";
            }
        } else {
            st = names_[OPP_SEAT] + (g_.stage() == tavla::Stage::NeedRoll ? " zarını atıyor…" : " oynuyor…");
        }
        std::vector<std::pair<std::string, Color>> parts;
        if (hints_ && g_.stage() != tavla::Stage::NotStarted && g_.stage() != tavla::Stage::OpeningRoll) {
            parts.push_back({"Pip " + std::to_string(g_.pipCount(0)) + " / " + std::to_string(g_.pipCount(1)), Color{226, 216, 196, 255}});
            parts.push_back({"  \xC2\xB7  ", Color{226, 216, 196, 120}});
        }
        parts.push_back({"Sayı " + std::to_string(g_.score(0)) + " - " + std::to_string(g_.score(1)) + " (" +
                             std::to_string(g_.matchPoints()) + "'e)",
                         Color{238, 198, 112, 255}});
        hud_.status(st, sc, humanToAct() && !aiSeat, parts, aiSeat);
        const bool canR = !aiSeat && canRoll();
        const bool canU = !aiSeat && g_.canUndo() && g_.current() == 0;
        hud_.buttons(mouse, {"Zar At", "Geri Al"}, {canR, canU}, {canR, false}, aiSeat);
        hud_.toasts();
    }

    bool handOver() const override { return over(); }
    bool matchOver() const override { return g_.stage() == tavla::Stage::MatchOver; }
    bool animating() const override { return board_.animating(); }
    int activeSeat() const override {
        if (over() || g_.stage() == tavla::Stage::OpeningRoll) return -1;
        return g_.current() == 0 ? 0 : OPP_SEAT;
    }
    bool mouseBusy() const override { return hover_ >= 0 || hud_.mouseOverHud(); }
    std::vector<int> seats() const override { return {0, OPP_SEAT}; }
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
    bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const override {
        if (board_.animating() || !humanToAct()) return false;
        if (canRoll()) {
            const std::vector<Rectangle>& b = hud_.buttonRects();
            if (b.empty()) return false;
            out = {b[0].x + b[0].width * 0.5f, b[0].y + b[0].height * 0.5f};
            return true;
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
        else w = board_.pointWorld(target, 0);
        w.y = w3d::TABLE_Y + 0.0105f;
        return r.projectToVirtual(w, out);
    }

    std::string scoreTitle() const override {
        if (g_.stage() == tavla::Stage::NotStarted) return "";
        return "Tavla \xC2\xB7 " + std::to_string(g_.matchPoints()) + " sayı";
    }
    std::vector<std::string> scoreLines() const override {
        return {names_[0] + " ....... " + std::to_string(g_.score(0)), names_[OPP_SEAT] + " ....... " + std::to_string(g_.score(1))};
    }

    ui::SheetModel sheet(bool aiMode) const override {
        ui::SheetModel m;
        m.columns = {aiMode ? std::string("Yapay Zeka (") + names_[0] + ")" : names_[0], names_[OPP_SEAT]};
        m.humanCol = 0;
        const int games = (int)results_.size();
        m.title = std::to_string(games) + ". Oyun";
        m.corner = "tavla: " + std::to_string(g_.matchPoints()) + " sayı";
        if (!results_.empty()) {
            const tavla::GameResult& r = results_.back();
            const std::string w = r.winner == 0 ? (aiMode ? std::string("Yapay zeka") : std::string("Sen")) : names_[OPP_SEAT];
            m.headline = r.winner == 0 && !aiMode ? std::string(r.mars ? "Mars yaptın!" : "Oyunu aldın!")
                                                  : w + (r.mars ? " mars yaptı!" : " oyunu aldı.");
            m.tagline = r.mars ? "mars: " + std::to_string(r.points) + " sayı" : "1 sayı";
            m.taglineRed = r.mars;
            m.starCol = r.winner;
            m.rowLabels = {"Sonuç", "Bu oyun"};
            m.cells = {{r.winner == 0 ? (r.mars ? "Mars!" : "Kazandı") : "", r.winner == 1 ? (r.mars ? "Mars!" : "Kazandı") : ""},
                       {r.winner == 0 ? "+" + std::to_string(r.points) : "", r.winner == 1 ? "+" + std::to_string(r.points) : ""}};
            m.red = {{r.mars && r.winner == 0, r.mars && r.winner == 1}, {false, false}};
        }
        for (size_t i = 0; i < results_.size(); ++i) {
            const tavla::GameResult& r = results_[i];
            m.historyLabels.push_back(std::to_string(i + 1) + ". oyun" + (r.mars ? " (mars)" : ""));
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
    bool over() const { return g_.stage() == tavla::Stage::GameOver || g_.stage() == tavla::Stage::MatchOver; }
    int actorPlayer() const {
        switch (g_.stage()) {
        case tavla::Stage::OpeningRoll: return 0; // the player throws the opening dice (the AI in the Yapay Zeka mode)
        case tavla::Stage::NeedRoll:
        case tavla::Stage::Moving: return g_.current();
        default: return -1;
        }
    }
    bool humanToAct() const { return actorPlayer() == 0 && !over(); }
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
        if (id == 0 && canRoll()) roll();
        else if (id == 1 && g_.canUndo() && g_.current() == 0) undo();
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
            case E::MatchStart:
            case E::GameStart: hud_.toast(e.text, ui::pal::Highlight, 2.6f); break;
            case E::OpeningRoll:
                board_.throwDice(0, e.d1, e.d2, true, 0.f);
                sound(ui::Sfx::DiceThrow);
                hud_.toast(e.text, ui::pal::Highlight, 2.6f);
                break;
            case E::Roll: {
                const float delay = e.player == 1 ? w3d::BOT_GIVE_LEAD : 0.f;
                board_.throwDice(e.player, e.d1, e.d2, false, delay);
                if (e.player == 1 && ctx_.characters) ctx_.characters->reach(OPP_SEAT, {0.f, w3d::TABLE_Y, 0.f}, 1);
                sound(ui::Sfx::DiceThrow);
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::TextLight, 2.0f);
                if (e.player == 1 && e.d1 == e.d2 && e.d1 >= 4 && rng_.chance(0.6f))
                    say(OPP_SEAT, kMahmutDouble[rng_.range(3)]);
                break;
            }
            case E::Step: {
                const float delay = bot && e.player == 1 ? w3d::BOT_TAKE_LEAD : 0.f;
                board_.moveChecker(e.player, e.from, e.to, delay);
                if (e.player == 1 && ctx_.characters) ctx_.characters->reach(OPP_SEAT, board_.pointWorld(e.from, 1), 0);
                sfxAt_.push_back({now_ + (delay + 0.45f) / speed_, ui::Sfx::Checker});
                break;
            }
            case E::Hit:
                hud_.toast(e.text, e.player == 0 ? ui::pal::Highlight : ui::pal::Bad, 2.4f);
                if (e.player == 1) say(OPP_SEAT, kMahmutHit[rng_.range(3)], true);
                else if (rng_.chance(0.7f)) say(OPP_SEAT, kMahmutHitBy[rng_.range(3)], true);
                if (rng_.chance(0.35f)) {
                    const int w = rng_.chance(0.5f) ? 1 : 3;
                    say(w, kWatchHit[w == 1 ? 0 : 2][rng_.range(2)]);
                }
                if (ctx_.characters) ctx_.characters->react(OPP_SEAT, e.player == 1 ? 1 : 2, {0.f, w3d::TABLE_Y, 0.f});
                break;
            case E::BearOff: sound(ui::Sfx::Checker); break;
            case E::NoMove: hud_.toast(e.text, ui::pal::TextLight, 2.6f); break;
            case E::GameEnd:
                hud_.toast(e.text, ui::pal::Highlight, 4.f);
                results_.push_back(g_.lastResult());
                log_ = e.text + " | " + std::to_string(g_.score(0)) + "-" + std::to_string(g_.score(1));
                if (g_.lastResult().mars) say(e.player == 1 ? OPP_SEAT : (rng_.chance(0.5f) ? 1 : 3), kMarsLines[rng_.range(3)], true);
                if (ctx_.characters) ctx_.characters->react(OPP_SEAT, e.player == 1 ? 1 : 2, {0.f, w3d::TABLE_Y, 0.f});
                sound(e.player == 0 ? ui::Sfx::Win : ui::Sfx::Lose);
                break;
            case E::MatchEnd: hud_.toast(e.text, ui::pal::Highlight, 5.f); break;
            default: break;
            }
            if (e.type == E::Step || e.type == E::Undo || e.type == E::GameStart || e.type == E::Hit) syncBoard(false);
        }
        syncBoard(false);
    }

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
    bool hints_ = true, aiSeat_ = false, built_ = false, menuReq_ = false, aiReq_ = false;
};

} // namespace

std::unique_ptr<TableGame> makeTavlaTable() { return std::make_unique<TavlaTable>(); }

} // namespace app
