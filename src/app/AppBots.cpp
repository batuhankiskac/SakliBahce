// App: the okey bots' pacing and the Yapay Zeka mode (AppInternal.h).
#include "app/AppInternal.h"

namespace app {
namespace detail {

// ---------------------------------------------------------------- the Yapay Zeka mode's table messages
// The engine speaks to the human seat in the second person ("Yandan Kırmızı 5 aldın", "Okey attın: 101 ceza").
// While the Yapay Zeka plays that seat the table talks about it instead ("Yapay zeka yandan Kırmızı 5 aldı").
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string lowerFirstTR(const std::string& w) {
    static const char* const kPairs[][2] = {{"İ", "i"}, {"I", "ı"}, {"Ç", "ç"}, {"Ş", "ş"},
                                            {"Ğ", "ğ"}, {"Ö", "ö"}, {"Ü", "ü"}};
    for (const auto& p : kPairs) {
        const std::string up = p[0];
        if (w.compare(0, up.size(), up) == 0) return p[1] + w.substr(up.size());
    }
    if (!w.empty() && w[0] >= 'A' && w[0] <= 'Z') return std::string(1, (char)(w[0] - 'A' + 'a')) + w.substr(1);
    return w;
}

// Second person singular past -> third person: "aldın" -> "aldı", "verdin" -> "verdi", "aldığın" -> "aldığı".
std::string thirdPerson(const std::string& word) {
    size_t core = word.size();
    while (core > 0 && std::strchr("!?.,:;)", word[core - 1])) --core;
    std::string w = word.substr(0, core);
    static const char* const kSecond[] = {"dın", "din", "dun", "dün", "tın", "tin", "tun", "tün", "ğın", "ğin", "ğun", "ğün"};
    for (const char* suffix : kSecond)
        if (endsWith(w, suffix)) {
            w.pop_back(); // the final "n"
            break;
        }
    return w + word.substr(core);
}

std::string watchText(const okey::GameEvent& e) {
    using okey::EvType;
    if (e.type == EvType::MatchEnd) {
        if (e.text == "Maç bitti! Sen kazandın!") return "Maç bitti! Yapay zeka kazandı!";
        const std::string head = "Birinciliği ", tail = " ile paylaştın";
        const size_t at = e.text.find(head);
        if (endsWith(e.text, tail) && at != std::string::npos)
            return e.text.substr(0, at) + "Yapay zeka birinciliği " +
                   e.text.substr(at + head.size(), e.text.size() - tail.size() - at - head.size()) + " ile paylaştı";
        return e.text;
    }
    if (e.player != HUMAN || e.text.empty()) return e.text;
    switch (e.type) {
    case EvType::MatchStart:
    case EvType::HandStart: return e.text; // (player = the starter: nothing said to anyone)
    case EvType::TurnStart: return "Sıra yapay zekada";
    default: break;
    }
    std::istringstream in(e.text);
    std::string word, out;
    bool first = true;
    while (in >> word) {
        if (first) { // a sentence-initial verb goes lower case; a tile's name keeps its capital ("Kırmızı 5'i işledin")
            static const char* const kNames[] = {"Sarı", "Mavi", "Siyah", "Kırmızı", "Sahte"};
            if (std::none_of(std::begin(kNames), std::end(kNames), [&](const char* n) { return word.rfind(n, 0) == 0; }))
                word = lowerFirstTR(word);
        }
        out += (first ? "" : " ") + thirdPerson(word);
        first = false;
    }
    return "Yapay zeka " + out;
}

// ---------------------------------------------------------------- the Yapay Zeka mode
// Switching it on hands the seat to its Kurt (paced like the others by updateBots); switching it off first takes
// back a decision still being made on the worker thread, then the player simply goes on from the stage the turn is
// in (draw / play / discard). Nothing else changes hands: the game, the scores and the seat stay the player's.
void App::setAiMode(bool on) {
    if (on == aiMode_) return;
    if (opt_.autoplay) { // --autoplay keeps its bot on the seat for the whole run
        if (flow_ != Flow::Title) table_.toast("--autoplay: bu koltukta hep yapay zeka oynar", ui::pal::TextLight, 2.4f);
        return;
    }
    if (!on && think_.seat == HUMAN) {
        cancelThink(); // joins the worker: its decision is dropped, nothing of it reaches the game
        think_ = Think{};
    }
    aiMode_ = on;
    if (on) matchAiTouched_ = true;
    screenT_ = 0.f; // a between-hands screen that is up now starts its countdown afresh (or stops it)
    table_.setAiMode(aiSeat());
    screens_.setAiMode(aiSeat());
    ++aiSwitches_;
    if (flow_ == Flow::Title || snapshot_) return;
    const bool myTurn = game_.handState() == okey::HandState::Playing && game_.current() == HUMAN;
    if (on) table_.toast("Yapay zeka senin yerine oynuyor  \xC2\xB7  geri almak için Y", ui::pal::Highlight, 3.2f);
    else table_.toast(myTurn ? "Kontrol sende  \xC2\xB7  sıra sende" : "Kontrol sende", ui::pal::TextLight, 2.6f);
    // the regulars notice (now and then, and only while a hand is being played)
    if (game_.handState() == okey::HandState::Playing && simClock_ - aiBanterAt_ >= AI_BANTER_GAP && (on || aiSwitches_ > 1)) {
        const ui::BanterLine l = ui::Banter::aiModeLine(on, (uint32_t)mix64(baseSeed_ + (uint64_t)aiSwitches_));
        characters_.say(l.seat, l.text, l.seconds);
        aiBanterAt_ = simClock_;
    }
}

// ---------------------------------------------------------------- bots
void App::updateBots(float simDt) {
    const int s = game_.current();
    if (s < 0 || s > 3 || !isBotSeat(s) || !bots_[s]) return;
    if (s != think_.seat || game_.turnNumber() != think_.turn) { // a new bot turn: think a little first
        cancelThink();
        think_.seat = s;
        think_.turn = game_.turnNumber();
        think_.actions = 0;
        // (the reach itself now takes its time — the hand goes to the tile before it moves — so a little less
        // idle thinking keeps the table's rhythm)
        think_.wait = paceRng_.uniform(0.45f, 1.0f) / animSpeed();
    }
    if (!think_.pending) launchThink(s);
    if (table_.isAnimating()) return;  // tiles still in the air: nobody moves
    think_.wait -= simDt;
    if (think_.wait > 0.f || !thinkReady()) return;
    const okey::BotAction a = takeThink();
    if (++think_.actions > MAX_BOT_ACTIONS) {
        TraceLog(LOG_WARNING, "Bot %d: %d hamlede tur bitmedi, zorla bitiriliyor", s, think_.actions);
        forceLegalMove(s);
    } else {
        applyBot(s, a);
    }
    think_.wait = paceRng_.uniform(0.25f, 0.5f) / animSpeed();
}

void App::launchThink(int seat) {
    okey::Bot* bot = bots_[seat].get();
    think_.pending = true;
    if (snapshot_) { // frame-exact determinism: decide right here
        think_.ready = bot->next(game_, seat);
        think_.haveReady = true;
        return;
    }
    // A bot decision can take ~10 ms (Kurt's rollouts): think on a worker thread while the pacing delay runs.
    // Nothing mutates the game until the result is applied on this thread; cancelThink() joins first.
    const okey::Game* g = &game_;
    think_.fut = std::async(std::launch::async, [bot, g, seat] { return bot->next(*g, seat); });
}

bool App::thinkReady() {
    if (think_.haveReady) return true;
    return think_.fut.valid() && think_.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

okey::BotAction App::takeThink() {
    think_.pending = false;
    if (think_.haveReady) {
        think_.haveReady = false;
        return think_.ready;
    }
    return think_.fut.get();
}

void App::cancelThink() {
    if (think_.fut.valid()) think_.fut.wait();
    think_.fut = std::future<okey::BotAction>();
    think_.pending = false;
    think_.haveReady = false;
}

void App::applyBot(int seat, const okey::BotAction& a) {
    okey::ActionResult r = okey::applyBotAction(game_, seat, a);
    if (!r.ok) {
        ++rejected_;
        TraceLog(LOG_WARNING, "Bot %d hamlesi reddedildi (%s): %s", seat, actionName(a.kind), r.error.c_str());
        const okey::BotAction f = okey::fallbackAction(game_, seat);
        r = okey::applyBotAction(game_, seat, f);
        if (!r.ok) {
            ++fallbackRejected_;
            TraceLog(LOG_WARNING, "Bot %d yedek hamlesi de reddedildi (%s): %s", seat, actionName(f.kind), r.error.c_str());
            forceLegalMove(seat);
        }
    }
    pumpEvents();
}

// Last resort: any legal step that moves the turn forward, so a bot can never stall the game.
void App::forceLegalMove(int seat) {
    ++forced_;
    if (game_.handState() != okey::HandState::Playing || game_.current() != seat) return;
    if (game_.stage() == okey::TurnStage::NeedDraw) {
        if (game_.pileCount() > 0 && game_.drawFromPile(seat).ok) return pumpEvents();
        if (game_.takeFromLeft(seat).ok) return pumpEvents();
        return;
    }
    if (game_.pendingLeftTile() >= 0 && game_.returnLeftTile(seat).ok) {
        game_.drawFromPile(seat);
        return pumpEvents();
    }
    const std::vector<int> hand = game_.player(seat).hand;
    for (int t : hand)
        if (game_.discard(seat, t).ok) break;
    pumpEvents();
}

} // namespace detail
} // namespace app
