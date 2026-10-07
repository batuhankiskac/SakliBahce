// App: Başarımlar, the badges (AppInternal.h; the list and the counting in ui/Achievements.h).
// What feeds them: the okey engine's events and hand results (here), the other tables' events
// (TableGame::achievementEvents, every frame), the end of every hand and match (AppFlow.cpp, after the record), the
// analysis of a finished match (AppAnalysis.cpp). Counted only by the record's rule, App::recording(); watching the
// Yapay Zeka play a whole match only needs an interactive session. When one opens: the banner with its chime
// (Screens), a regular's congratulation (Banter::achievement) and the room's applause (Characters::crowdReact).
#include "app/AppInternal.h"
#include "r3d/Daytime.h"

namespace app {
namespace detail {

std::string achievementsPath() {
    const std::string dir = supportDir();
    return dir.empty() ? dir : dir + "SakliBahce/basarimlar.txt";
}

namespace {

// The games "Hatalarım" judges (analysis::analyzeMatch): only for these does an empty analysis mean "no mistakes".
bool analysisJudges(int game) {
    using G = ui::GameKind;
    switch ((G)game) {
    case G::Yuzbir:
    case G::YuzbirEsli:
    case G::Okey:
    case G::Tavla:
    case G::Pisti:
    case G::Batak:
    case G::King:
    case G::Bezik: // Bezik
    case G::Dama:  // Dama
    case G::Konken: // Konken
    case G::Altmisalti: return true; // Altmışaltı
    default: return false;
    }
}

void append(std::vector<int>& to, const std::vector<int>& more) { to.insert(to.end(), more.begin(), more.end()); }

} // namespace

void App::initAchievements() {
    if (!unattended() || opt_.achievementTest) achievements_.load(achievementsPath());
    unsigned mask = 0;
    for (int k = 0; k < (int)ui::GameKind::Count; ++k)
        if (ui::gameAvailable((ui::GameKind)k)) mask |= 1u << k;
    achievements_.setGames(mask);
    screens_.setAchievements(&achievements_);
    if (snapshot_ && snapState_ == SnapState::Achievements) {
        // a made-up book, to see the page filled in (never saved: a snapshot is unattended)
        achievements_.setLive(true, true);
        const int day = ui::Achievements::today();
        for (const char* ev : {"okey_bitis", "cift_bitis", "mars", "katmerli", "duses", "pisti", "pisti_ustune", "batak13"})
            achievements_.event(ev, 1, day - 3);
        for (int i = 0; i < 6; ++i) achievements_.event("king_cezasiz", 1, day);
        for (int d = 0; d < 4; ++d) achievements_.handPlayed(day - 3 + d);
        ui::MatchEndInfo m;
        m.won = true;
        m.beaten = 1u << 2;
        m.phase = 3;
        m.season = 2;
        for (int g : {0, 3, 4}) {
            m.game = g;
            achievements_.matchEnd(m, day - 1);
        }
        achievements_.analysis(0, day);
        achievements_.setLive(false, false);
    }
}

void App::saveAchievements() {
    if (unattended() && !opt_.achievementTest) return;
    const std::string path = achievementsPath();
    if (!path.empty() && ensureDirOf(path)) achievements_.save(path);
}

// The room celebrates the badges that just opened: the banner (one after the other, each with its chime), a regular's
// congratulation for the first, the crowd's applause.
void App::celebrate(const std::vector<int>& opened) {
    if (opened.empty()) return;
    for (int i : opened) {
        screens_.showAchievementBanner(i);
        std::printf("[basarim] %s\n", ui::Achievements::def(i).name);
    }
    characters_.banter().achievement(ui::Achievements::def(opened.front()).name);
    characters_.crowdReact(r3d::Characters::CrowdCheer, characters_.headPosition(HUMAN), 0.7f);
    // (duzelt) at the end of a hand the score sheet waits a little: the banner, the congratulation and the applause
    // happen over the table, not behind the sheet
    if (flow_ == Flow::HandOver) achHold_ = 3.5f;
    if (opt_.achievementTest) {
        for (int i : opened) achTest_.names.push_back(ui::Achievements::def(i).name);
        achTest_.celebrated = true;
    }
}

// ---- --achievement-test: a real match through the real path (events, the record's rule, the book, celebrate) and
// what the player would see: the banner up, a regular saying the badge's name, the room's shout.
void App::achievementTestSpeech(int who, const std::string& text) {
    for (const std::string& n : achTest_.names)
        if (!achTest_.congratulated && text.find("\"" + n) != std::string::npos && !screens_.blocksGame()) { // (seen: no sheet over it)
            achTest_.congratulated = true;
            std::printf("[basarim-test] %s: \"%s\"\n", who == 4 ? "çaycı" : BOT_NAMES[std::clamp(who, 1, 3)], text.c_str());
        }
}

void App::achievementTestFrame() {
    if (!achTest_.celebrated) return;
    if (screens_.achievementBannerUp()) ++achTest_.bannerFrames;
    if (achTest_.doneAt < 0 && achTest_.bannerFrames > 30 && achTest_.congratulated && achTest_.applause) {
        achTest_.doneAt = frame_;
        std::printf("[basarim-test] banner, congratulation and applause seen (frame %ld)\n", frame_);
    }
    if (achTest_.doneAt >= 0 && frame_ - achTest_.doneAt > 90) { // (a moment more: the picture shows it)
        if (snapshot_ && !exportSnapshot()) exitCode_ = 1;
        quit_ = true;
    }
}

void App::achievementTestReport() {
    // the book on disk has them too
    ui::Achievements book;
    book.load(achievementsPath());
    int saved = 0;
    for (const std::string& n : achTest_.names)
        for (int i = 0; i < ui::Achievements::count(); ++i)
            if (n == ui::Achievements::def(i).name && book.unlocked(i)) ++saved;
    std::string names;
    for (const std::string& n : achTest_.names) names += (names.empty() ? "" : ", ") + n;
    const bool ok = !achTest_.names.empty() && achTest_.bannerFrames > 30 && achTest_.congratulated && achTest_.applause &&
                    saved == (int)achTest_.names.size();
    std::printf("[basarim-test] %s: opened %zu (%s) | banner %ld frames | congratulation %s | applause %s | in the book %d\n",
                ok ? "ok" : "FAILED", achTest_.names.size(), names.c_str(), achTest_.bannerFrames,
                achTest_.congratulated ? "yes" : "no", achTest_.applause ? "yes" : "no", saved);
    if (!ok && exitCode_ == 0) exitCode_ = 4;
}

// One of the okey engine's events (pumpEvents): opening with exactly 101.
void App::achievementsOkeyEvent(const okey::GameEvent& e) {
    if (e.type != okey::EvType::Open || e.player != HUMAN || game_.classic() || !recording()) return;
    if (game_.player(HUMAN).openedWithPairs || e.amount != 101) return;
    achievements_.setLive(true, true);
    const std::vector<int> v = achievements_.event("tam101");
    if (v.empty()) return;
    saveAchievements();
    celebrate(v);
}

// Every frame: the other table's events, the Yapay Zeka watched from the first move, --achievement-demo.
void App::updateAchievements() {
    if (matchCount_ != achMatch_) { // a new match: did the Yapay Zeka take the seat from its first move?
        achMatch_ = matchCount_;
        aiWholeMatch_ = aiMode_ && !resuming_;
    }
    if (!aiMode_) aiWholeMatch_ = false;
    if (opt_.achievementDemo && frame_ == 120) celebrate({ui::Achievements::find("pisti_ustune")});
    if (opt_.achievementTest) achievementTestFrame();
    if (!other_) return;
    const std::vector<std::string> evs = other_->achievementEvents(); // (taken every frame: nothing waits for later)
    if (evs.empty() || !otherInGame() || !recording()) return;
    achievements_.setLive(true, true);
    std::vector<int> opened;
    for (const std::string& ev : evs) append(opened, achievements_.event(ev, std::clamp(matchSettings_.difficulty, 0, 2)));
    if (opened.empty()) return;
    saveAchievements();
    celebrate(opened);
}

// After the record at the end of every hand (AppFlow.cpp): the okey finishes, the day of play, and at the end of the
// match the match itself (or a match the Yapay Zeka played while the player watched).
void App::achievementsAtHandEnd() {
    const bool over = otherInGame() ? other_->matchOver() : game_.handState() == okey::HandState::MatchOver;
    achievements_.setLive(recording(), !unattended() || opt_.achievementTest);
    std::vector<int> opened;
    if (recording()) {
        bool won = false;
        unsigned beaten = 0;
        if (!otherInGame()) {
            const okey::HandResult& r = game_.lastHandResult();
            if (r.winner == HUMAN) {
                if (r.finishedWithJoker) append(opened, achievements_.event("okey_bitis"));
                if (r.finishedWithPairs) append(opened, achievements_.event("cift_bitis"));
                if (r.finishedInOneGo && !game_.classic()) append(opened, achievements_.event("elden_bitis"));
            }
            if (game_.classic() && r.indicatorShownBy == HUMAN) append(opened, achievements_.event("gosterge"));
            const int lead = game_.leaderSeat();
            won = lead == HUMAN || (game_.teams() && lead == okey::Game::partnerOf(HUMAN));
            for (int s = 1; s <= 3; ++s)
                if (!game_.teams() || s != okey::Game::partnerOf(HUMAN)) beaten |= 1u << s;
        } else {
            const ui::SheetModel sh = other_->sheet(false);
            won = sh.humanWon;
            const std::vector<int> seats = other_->seats();
            const bool teams = !sh.columns.empty() && sh.columns.size() < seats.size(); // (partner across: seat 2)
            for (int s : seats)
                if (s >= 1 && s <= 3 && !(teams && s == 2)) beaten |= 1u << s;
        }
        append(opened, achievements_.handPlayed());
        if (over) {
            ui::MatchEndInfo m;
            m.game = matchGame_;
            m.level = std::clamp(matchSettings_.difficulty, 0, 2);
            m.won = won;
            m.beaten = won ? beaten : 0u;
            m.phase = w3d::resolveDayPhase(0);
            m.season = w3d::resolveSeason(0);
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
            localtime_r(&now, &lt);
            m.hour = lt.tm_hour;
            append(opened, achievements_.matchEnd(m));
        }
        saveAchievements();
    } else if (over && aiWholeMatch_ && !replayMode_ && !unattended()) {
        opened = achievements_.watchedAi();
        if (!opened.empty()) saveAchievements();
    }
    celebrate(opened);
}

// "Hatalarım" of a match the player played himself is ready (pollAnalysis): no mistake worth telling.
void App::achievementsAnalysis(int game, int mistakes) {
    if (!analysisJudges(game) || unattended()) return;
    achievements_.setLive(true, true);
    const std::vector<int> v = achievements_.analysis(mistakes);
    if (v.empty()) return;
    saveAchievements();
    celebrate(v);
}

} // namespace detail
} // namespace app
