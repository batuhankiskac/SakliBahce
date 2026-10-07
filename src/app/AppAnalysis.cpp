// App: "Hatalarım" (AppInternal.h).
#include "app/AppInternal.h"

namespace app {
namespace detail {

// ---------------------------------------------------------------- hatalarım
namespace {
std::string costText(const analysis::Mistake& m) {
    char buf[32];
    if (m.cost >= 9.95) std::snprintf(buf, sizeof buf, "%.0f", m.cost);
    else std::snprintf(buf, sizeof buf, "%.1f", m.cost);
    std::string n = buf;
    for (char& c : n)
        if (c == '.') c = ',';
    return "-" + n + (m.unit == "%" ? std::string(" puan şans") : " " + m.unit);
}
} // namespace

void App::stopAnalysis() {
    if (anaThread_.joinable()) {
        anaCancel_ = true;
        anaThread_.join();
    }
    anaCancel_ = false;
    anaDone_ = false;
}

void App::startAnalysis(int game, const ui::Settings& rules, uint64_t seed, std::vector<std::string> actions,
                        const std::string& title) {
    stopAnalysis();
    anaResult_.clear();
    anaTitle_ = title;
    screens_.setAnalysis(true, false, title, {});
    const std::array<std::string, 4> nm = names();
    anaThread_ = std::thread([this, game, rules, seed, actions = std::move(actions), nm] {
        std::vector<analysis::Mistake> out =
            analysis::analyzeMatch((ui::GameKind)game, rules, seed, actions, nm, 3, &anaCancel_);
        if (!anaCancel_) anaResult_ = std::move(out);
        anaDone_ = true;
    });
}

void App::pollAnalysis() {
    if (!anaDone_ || !anaThread_.joinable()) return;
    anaThread_.join();
    anaDone_ = false;
    std::vector<ui::MistakeView> rows;
    for (const analysis::Mistake& m : anaResult_) rows.push_back({m.when, m.played, m.better, m.why, costText(m)});
    screens_.setAnalysis(true, true, anaTitle_, rows);
    if (anaCounts_) achievementsAnalysis(anaGame_, (int)anaResult_.size()); // Başarımlar: "Kusursuz"
    anaCounts_ = false;
}

// At the end of a match the player played himself (not the Yapay Zeka, not a replay).
void App::analyzeFinishedMatch() {
    if (replayMode_ || matchAiTouched_) return;
    std::vector<std::string> actions;
    if (otherInGame()) {
        if (!other_->saveState(actions)) return;
    } else {
        for (const okey::LoggedAction& a : game_.actionLog()) actions.push_back(a.encode());
    }
    startAnalysis(matchGame_, matchSettings_, matchSeed_, std::move(actions),
                  std::string(ui::gameInfo((ui::GameKind)matchGame_).name) + " \xC2\xB7 bu maç");
    anaCounts_ = recording(); // Başarımlar: the player's own match
    anaGame_ = matchGame_;
}

void App::analyzeReplay(int index) {
    SavedMatch sv;
    if (index < 0 || index >= (int)replayFiles_.size() || !readSave(sv, replayFiles_[(size_t)index])) {
        screens_.setAnalysis(false, true, "", {});
        return;
    }
    ui::Settings rules = screens_.settings();
    rules.tavlaCesit = 0; // Tavla çeşidi: a replay from before the çeşitler has no line for it, it was klasik
    rules.konkenLastStanding = false; // Konken bitiş: a save from before it has no line for it, it was "ilk yanan"
    rules.rakip = rules.tavlaRakip = rules.damaRakip = 0; // Rakip: only what the replay says (ui::twoPlayerOpponent)
    for (const std::string& kv : sv.settings) {
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) applySettingLine(rules, trim(kv.substr(0, eq)), trim(kv.substr(eq + 1)));
    }
    rules.game = sv.game;
    anaCounts_ = false; // (a replay's analysis opens no badge)
    startAnalysis(sv.game, rules, sv.seed, sv.actions,
                  std::string(ui::gameInfo((ui::GameKind)sv.game).name) + " \xC2\xB7 " + sv.date);
}

} // namespace detail
} // namespace app
