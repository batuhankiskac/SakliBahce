// App: the player's record, save / resume and Tekrarlar (AppInternal.h).
#include "app/AppInternal.h"
#include "app/Rules101.h" // 101 kuralları

namespace app {
namespace detail {

// ---------------------------------------------------------------- the match left unfinished (kayit.txt)
// The game, the rules it was started with (the settings lines), the seed and every action since; resuming starts the
// same match again and replays them.
std::string savePath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/kayit.txt";
}

std::string replayDir() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/tekrarlar";
}

bool readSave(SavedMatch& sv, const std::string& from) {
    const std::string path = from.empty() ? savePath() : from;
    std::string text;
    return !path.empty() && ui::readFileText(path, text) && parseSave(text, sv);
}

// ---------------------------------------------------------------- the player's record
// Only matches the player played himself go into the book: no unattended runs, and not a match in which the Yapay
// Zeka mode took the seat at any moment.
bool App::recording() const {
    if (opt_.achievementTest) return !replayMode_; // (--achievement-test: this run's bot match counts, for the check only)
    return !unattended() && !matchAiTouched_ && !replayMode_;
}

void App::recordOkeyHand() {
    if (!recording()) return;
    const int kind = std::clamp(matchGame_, 0, (int)ui::GameKind::Count - 1);
    const okey::HandResult& r = game_.lastHandResult();
    const bool won = r.winner == HUMAN || (game_.teams() && r.winner == okey::Game::partnerOf(HUMAN));
    record_.hand(kind, won, !game_.classic(), r.score[HUMAN], true);
    if (r.winner == HUMAN && r.finishedWithJoker)
        for (int s = 1; s <= 3; ++s) memory_.noteMoment(ui::MomentKind::OkeyFinish, 0, s, kind);
    if (game_.handState() == okey::HandState::MatchOver) {
        const int lead = game_.leaderSeat();
        record_.match(kind, matchSettings_.difficulty, lead == HUMAN || (game_.teams() && lead == okey::Game::partnerOf(HUMAN)));
        std::array<int, 4> tot{};
        for (int s = 0; s < 4; ++s) tot[(size_t)s] = game_.player(s).totalScore;
        memory_.recordMatch(ui::MatchRecord::fromScores(kind, tot, 0xFu, !game_.classic(), game_.teams()));
        noteRankUp();
    }
    saveBooks();
}

void App::recordOtherHand() {
    if (!recording() || !other_) return;
    const ui::SheetModel sh = other_->sheet(false);
    int score = 0;
    const bool hasScore = other_->humanHandScore(score);
    if (other_->humanInHand()) // (Konken son kalan: not the hands he watched after burning)
        record_.hand((int)otherKind_, sh.starCol >= 0 && sh.starCol == sh.humanCol, hasScore, score, false);
    if (otherKind_ == ui::GameKind::Tavla && sh.taglineRed && sh.starCol >= 0) { // a mars, by or to the player
        const std::vector<int> st = other_->seats(); // (Rakip: whoever sat across)
        const int opp = st.size() > 1 ? st[1] : 2;
        memory_.noteMoment(ui::MomentKind::Mars, sh.starCol == 0 ? 0 : opp, sh.starCol == 0 ? opp : 0, (int)otherKind_);
    }
    if (other_->matchOver()) {
        record_.match((int)otherKind_, matchSettings_.difficulty, sh.humanWon);
        memory_.recordMatch(ui::MatchRecord::fromStandings((int)otherKind_, other_->seats(), (int)sh.columns.size(),
                                                           sh.ranking, sh.rank, sh.totals));
        noteRankUp();
    }
    saveBooks();
}

// The record and the regulars' memory to disk (never from a run that plays by itself: --achievement-test counts its
// match for the check, but the player's books stay as they are).
void App::saveBooks() {
    if (unattended()) return;
    const std::string sp = statsPath(), mp = memoryPath();
    if (!sp.empty() && ensureDirOf(sp)) record_.save(sp);
    if (!mp.empty() && ensureDirOf(mp)) memory_.save(mp);
}

// A regular congratulates the player when the match just finished lifted his rank.
void App::noteRankUp() {
    const ui::Rank& rk = ui::StatsBook::rankFor(record_.rankPoints());
    if (memory_.noteRank(rk.points)) characters_.banter().rankUp(rk.name);
}

// ---------------------------------------------------------------- save / resume
void App::refreshResumable() {
    SavedMatch sv;
    const bool ok = !unattended() && readSave(sv);
    screens_.setResumable(ok, ok ? sv.label : std::string());
}

void App::deleteSave() {
    const std::string path = savePath();
    if (!unattended() && !path.empty() && FileExists(path.c_str())) std::remove(path.c_str());
    refreshResumable();
}

// Writes the match in play (called when the player leaves it: to the title, closing the window, between hands).
void App::saveMatch() {
    // (a match the Yapay Zeka was asked to play, "Yapay Zekayı İzle", leaves the player's own save alone)
    if (unattended() || flow_ == Flow::Title || resuming_ || replayMode_ || matchAiStarted_) return;
    const bool over = otherInGame() ? other_->matchOver() : game_.handState() == okey::HandState::MatchOver;
    if (over || (!otherInGame() && game_.handState() == okey::HandState::NotStarted)) {
        deleteSave();
        return;
    }
    std::vector<std::string> actions;
    std::string label = ui::gameInfo((ui::GameKind)matchGame_).name;
    if (otherInGame()) {
        if (!other_->saveState(actions)) return;
        label += "  \xC2\xB7  " + other_->scoreTitle();
    } else {
        for (const okey::LoggedAction& a : game_.actionLog()) actions.push_back(a.encode());
        label += "  \xC2\xB7  " + std::to_string(game_.handIndex() + 1) + ". el";
    }
    const std::string path = savePath();
    if (path.empty() || !ensureDirOf(path)) return;
    ui::writeFileAtomic(path, "# SaklıBahçe: yarım kalan maç\n" + matchText(label, actions));
    refreshResumable();
}

// The match in play as save / replay text: game, seed, settings and the engine's action lines.
std::string App::matchText(const std::string& label, const std::vector<std::string>& actions) const {
    std::string out = "g " + std::to_string(matchGame_) + "\ns " + std::to_string(matchSeed_) + "\ny " +
                      (matchAiTouched_ ? "1" : "0") + "\nl " + label + "\n";
    std::istringstream st(settingsText(matchSettings_));
    std::string line;
    while (std::getline(st, line)) out += "k " + line + "\n";
    for (const std::string& a : actions) out += "a " + a + "\n";
    return out;
}

// ---------------------------------------------------------------- tekrarlar (finished matches, the last 20)
void App::archiveMatch(const std::string& result) {
    if (unattended() || replayMode_) return;
    std::vector<std::string> actions;
    if (otherInGame()) {
        if (!other_->saveState(actions)) return;
    } else {
        for (const okey::LoggedAction& a : game_.actionLog()) actions.push_back(a.encode());
    }
    const std::string dir = replayDir();
    if (dir.empty() || (!DirectoryExists(dir.c_str()) && (!ensureDirOf(dir) || MakeDirectory(dir.c_str()) != 0))) return;
    const std::time_t now = std::time(nullptr);
    char stamp[32], when[48];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::strftime(when, sizeof when, "%d.%m.%Y %H:%M", std::localtime(&now));
    const std::string text = "# SaklıBahçe: maç tekrarı\nd " + std::string(when) + "\nr " + result + "\n" +
                             matchText(ui::gameInfo((ui::GameKind)matchGame_).name, actions);
    ui::writeFileAtomic(dir + "/" + stamp + ".txt", text);
    std::vector<std::string> files = listReplays(); // keep the newest 20
    for (size_t i = 20; i < files.size(); ++i) std::remove(files[i].c_str());
    refreshReplays();
}

// Newest first.
std::vector<std::string> App::listReplays() const {
    std::vector<std::string> out;
    const std::string dir = replayDir();
    if (dir.empty() || !DirectoryExists(dir.c_str())) return out;
    FilePathList fl = LoadDirectoryFilesEx(dir.c_str(), ".txt", false);
    for (unsigned i = 0; i < fl.count; ++i) out.push_back(fl.paths[i]);
    UnloadDirectoryFiles(fl);
    std::sort(out.begin(), out.end(), std::greater<std::string>());
    return out;
}

void App::refreshReplays() {
    replayFiles_.clear();
    std::vector<ui::ReplayEntry> rows;
    if (!unattended()) {
        for (const std::string& f : listReplays()) {
            SavedMatch sv;
            if (!readSave(sv, f)) continue;
            replayFiles_.push_back(f);
            rows.push_back({std::string(ui::gameInfo((ui::GameKind)sv.game).name), sv.date, sv.result,
                            (int)sv.actions.size()});
        }
    }
    screens_.setReplays(rows);
}

// Starts watching a saved match: the same match with its rules and seed; its actions are played one by one at the
// table's own pace (updateReplay), nobody else moves. Pause / speed with Space and the arrows; the menu leaves.
void App::startReplay(int index) {
    if (index < 0 || index >= (int)replayFiles_.size()) return;
    SavedMatch sv;
    if (!readSave(sv, replayFiles_[(size_t)index])) return;
    ui::Settings& st = screens_.settings();
    const ui::Settings mine = st;
    const ui::Settings rules = rulesFromSave(sv, mine);
    if (aiMode_) setAiMode(false);
    st = rules;
    forcedSeed_ = sv.seed;
    resuming_ = true;
    replayMode_ = true;
    replayActions_ = sv.actions;
    replayPos_ = 0;
    replayPaused_ = false;
    replayWait_ = 1.0f;
    startMatch();
    resuming_ = false;
    forcedSeed_.reset();
    st = mine;
    if (otherInGame() && !other_->setReplayMode(true)) {
        toTitle(); // (still in replay mode: nothing of it is saved; toTitle ends the mode)
        screens_.show(ui::ScreenId::Title);
        return;
    }
    screens_.show(ui::ScreenId::None);
}

void App::updateReplay(float simDt) {
    if (IsKeyPressed(KEY_SPACE)) replayPaused_ = !replayPaused_;
    if (IsKeyPressed(KEY_RIGHT)) replaySpeed_ = std::min(8.f, replaySpeed_ * 2.f);
    if (IsKeyPressed(KEY_LEFT)) replaySpeed_ = std::max(0.5f, replaySpeed_ * 0.5f);
    if (replayPaused_ || replayPos_ >= replayActions_.size()) return;
    const bool busy = otherInGame() ? other_->animating() : table_.isAnimating();
    if (busy) return;
    replayWait_ -= simDt * replaySpeed_;
    if (replayWait_ > 0.f) return;
    const std::string& line = replayActions_[replayPos_];
    if (otherInGame()) {
        const int r = other_->replayStep(line);
        if (r == 0) return;                            // not now (the table is not ready for it yet)
        ++replayPos_;
        if (r < 0) replayPos_ = replayActions_.size(); // a line that does not apply: the replay ends here
    } else {
        okey::LoggedAction a;
        if (!okey::LoggedAction::decode(line, a)) {
            replayPos_ = replayActions_.size();
            return;
        }
        if (a.kind == okey::LogKind::NextHand) {
            if (game_.handState() == okey::HandState::HandOver) return; // the score sheet's button deals it
            ++replayPos_;                                              // (already dealt)
            return;
        }
        if (game_.handState() != okey::HandState::Playing) return;
        ++replayPos_;
        if (!game_.replay(a)) {
            replayPos_ = replayActions_.size();
            return;
        }
        pumpEvents();
    }
    replayWait_ = paceRng_.uniform(0.5f, 0.9f) / animSpeed();
}

void App::drawReplayBadge() {
    if (!replayMode_ || screens_.blocksGame()) return;
    const bool done = replayPos_ >= replayActions_.size();
    char sp[16];
    std::snprintf(sp, sizeof sp, replaySpeed_ < 1.f ? "%.1f\xC3\x97" : "%.0f\xC3\x97", (double)replaySpeed_);
    const std::string head = std::string("TEKRAR  ") + (done ? "bitti" : replayPaused_ ? "duraklatıldı" : sp);
    const std::string help = "Boşluk: durdur   \xC2\xB7   sol / sağ ok: hız   \xC2\xB7   ESC: menü";
    const Rectangle r{24.f, 22.f, 430.f, 66.f};
    DrawRectangleRounded(r, 0.25f, 8, Color{20, 12, 8, 210});
    DrawRectangleRoundedLinesEx(r, 0.25f, 8, 1.5f, ui::pal::Brass);
    DrawCircleV({r.x + 22.f, r.y + 22.f}, 6.f, done ? ui::pal::Brass : replayPaused_ ? ui::pal::Highlight : Color{220, 60, 50, 255});
    ui::drawText(ui::FontId::UiBold, head, {r.x + 38.f, r.y + 10.f}, 22.f, ui::pal::TextLight);
    ui::drawText(ui::FontId::Ui, help, {r.x + 16.f, r.y + 40.f}, 15.f, Color{ui::pal::TextLight.r, ui::pal::TextLight.g, ui::pal::TextLight.b, 180});
    const float f = replayActions_.empty() ? 1.f : (float)replayPos_ / (float)replayActions_.size();
    DrawRectangleRec({r.x + 240.f, r.y + 20.f, 170.f, 5.f}, Color{60, 40, 28, 255});
    DrawRectangleRec({r.x + 240.f, r.y + 20.f, 170.f * f, 5.f}, ui::pal::Brass);
}

// "Devam Et": the saved match is started again with its rules and seed, and its actions are replayed.
void App::resumeSaved(bool ai) {
    SavedMatch sv;
    if (!readSave(sv)) {
        refreshResumable();
        screens_.show(ui::ScreenId::Title);
        return;
    }
    setAiMode(ai); // the player's own match ("Devam Et" after "Yapay Zekayı İzle" too); --resume --ai: the AI goes on
    ui::Settings& st = screens_.settings();
    const ui::Settings mine = st;
    const ui::Settings rules = rulesFromSave(sv, mine);
    st = rules;
    forcedSeed_ = sv.seed;
    resuming_ = true;
    startMatch();
    bool ok = true;
    if (otherInGame()) {
        ok = other_->restoreState(sv.actions);
        other_->achievementEvents(); // Başarımlar: what the saved part held does not count a second time
        if (other_->handOver()) { // saved at the end of a hand: it was recorded then; the score sheet comes up
            flow_ = Flow::HandOver;
            handOverT_ = SUMMARY_DELAY;
        }
    } else {
        for (const std::string& line : sv.actions) {
            okey::LoggedAction a;
            if (!okey::LoggedAction::decode(line, a) || !game_.replay(a)) {
                ok = false;
                break;
            }
        }
        game_.drainEvents(); // (what happened is already on the table: nothing to animate)
        table_.onHandStart();
        updateScoreboard();
        if (game_.handState() == okey::HandState::HandOver || game_.handState() == okey::HandState::MatchOver) {
            flow_ = Flow::HandOver;
            handOverT_ = SUMMARY_DELAY;
        }
    }
    resuming_ = false;
    forcedSeed_.reset();
    std::printf("[resume] %s: %zu actions replayed%s\n", ui::gameInfo((ui::GameKind)sv.game).name, sv.actions.size(),
                ok ? "" : " (FAILED part way)");
    matchAiTouched_ = matchAiTouched_ || sv.aiTouched;
    st = mine;
    st.game = sv.game;
    screens_.show(ui::ScreenId::None);
    if (!ok) {
        // a save from another version: the match goes on from where the replay stopped
        if (otherInGame()) other_->toast("Kayıt tam açılamadı, maç kaldığı yerden biraz önceden sürüyor", ui::pal::Bad, 4.f);
        else table_.toast("Kayıt tam açılamadı, maç kaldığı yerden biraz önceden sürüyor", ui::pal::Bad, 4.f);
    } else {
        if (otherInGame()) other_->toast("Kaldığın yerden devam", ui::pal::Highlight, 3.f);
        else table_.toast("Kaldığın yerden devam", ui::pal::Highlight, 3.f);
    }
}

} // namespace detail
} // namespace app
