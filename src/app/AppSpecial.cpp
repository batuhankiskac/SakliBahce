// App: özel günler and the rain in the garden (ozelgun agent; AppInternal.h "Özel günler").
//   * The day (w3d::resolveSpecialDay: the date, the hour, the setting "Özel günler", SAKLI_GUN) goes to the room
//     (decorations) and the people (clothes, scarves, more men standing), every few seconds.
//   * The regulars greet the day once a match (bayram greetings, "hayırlı iftarlar", "bu akşam derbi var") and remark on
//     it now and then; on Ramazan nights the sahur davulcu passes down the street; a goal in the derby brings the room
//     to its feet.
//   * With Mekân = otomatik the room never changes places by itself while a hand is played: it notes its wish (rain began
//     in the garden, the derby, the evening) and here, between hands (the hand's end has settled, the score sheet, the
//     final standings, the title), the picture fades out, the room changes places and fades back in, and a regular says
//     why ("Yağmur başladı, içeri geçelim!"). The game, the table and the save are not touched: only the scenery moves.
//   * With Mekân = Bahçe the awning comes down in the rain and Audio hears the drops on its canvas.
#include "app/AppInternal.h"

namespace app {
namespace detail {

namespace {
constexpr Vector3 GARDEN_TV{1.42f, 1.15f, -2.95f};  // the portable TV by the ocak (RoomSpecial.cpp)
constexpr float MOVE_FADE = 0.6f;                   // seconds to black before the room changes places
}  // namespace

void App::updateSpecialDay(float dt) {
    const ui::Settings& st = screens_.settings();
    const bool title = flow_ == Flow::Title;
    // ---- the day
    specialCheckT_ -= dt;
    if (specialCheckT_ <= 0.f || st.ozelGun != specialOn_) {
        specialCheckT_ = 10.f;
        specialOn_ = st.ozelGun;
        const int d = w3d::resolveSpecialDay(st.ozelGun, room_.dayPhase());
        if (d != specialDay_) {
            specialDay_ = d;
            specialGreetSeed_ = 0;  // a new day (or the evening came): greet it
            if (snapshot_ || opt_.autoplay) std::printf("[ozelgun] day %d\n", d);
        }
    }
    room_.setSpecialDay(specialDay_);
    characters_.setSpecialDay(specialDay_, room_.venue() == 1);
    const bool evening = room_.dayPhase() >= 2;
    const bool festive = w3d::isBayram(specialDay_) || specialDay_ == w3d::GunMac || (specialDay_ == w3d::GunRamazan && evening);

    // ---- the place: with Mekân = otomatik the room only notes its wish; we move it between hands
    room_.setVenueHold(true);
    const bool safe = title || flow_ == Flow::Summary || flow_ == Flow::MatchOver || (flow_ == Flow::HandOver && handOverT_ > 1.0f);
    if (venueMoveT_ < 0.f && safe && room_.venuePending() != 0) {
        venueMoveWhy_ = room_.venuePending();
        const bool toGarden = room_.venue() == 0;
        if (snapshot_ || opt_.autoplay)
            std::printf("[ozelgun] between hands (flow %d, hand %d): moving %s (reason %d)\n", (int)flow_, handsPlayed_,
                        toGarden ? "out to the garden" : "inside", venueMoveWhy_);
        if (snapshot_) {
            room_.applyVenue();
        } else {
            venueMoveT_ = 0.f;
        }
        if (!title) {
            int seat = 2;
            std::string text;
            const ui::SpecialSit sit = toGarden ? ui::SpecialSit::MoveOut
                                       : venueMoveWhy_ == 1 ? ui::SpecialSit::MoveInRain
                                       : venueMoveWhy_ == 2 ? ui::SpecialSit::MoveInDerby
                                                            : ui::SpecialSit::MoveOther;
            if (ui::specialLine(0, sit, (uint32_t)specialRng_.next(), seat, text)) characters_.chat(seat, text, true);
        }
    }
    if (venueMoveT_ >= 0.f) {
        venueMoveT_ += dt;
        fade_ = std::max(fade_, std::min(1.f, venueMoveT_ / MOVE_FADE));
        if (venueMoveT_ >= MOVE_FADE) {
            room_.applyVenue();
            characters_.setSpecialDay(specialDay_, room_.venue() == 1);
            fade_ = 1.f;  // (tick fades it back in)
            venueMoveT_ = -1.f;
        }
    }
    // the rain begins while we sit in the garden: a remark (otomatik: we go in after the hand; Bahçe: the awning)
    if (room_.consumeRainStart() && room_.venue() == 1 && !title) {
        int seat = 1;
        std::string text;
        const ui::SpecialSit sit = st.venue == 2 ? ui::SpecialSit::RainBegins : ui::SpecialSit::UnderAwning;
        if (ui::specialLine(0, sit, (uint32_t)specialRng_.next(), seat, text)) characters_.chat(seat, text, true);
        if (snapshot_ || opt_.autoplay) std::printf("[ozelgun] the rain begins in the garden (mekan %d)\n", st.venue);
    }
    if (audioOn_) audio_.setRainCanvas(room_.awningAmount() * (room_.rainAmount() > 0.f ? 1.f : 0.f));

    // ---- the regulars and the day
    const bool playing = flow_ == Flow::Playing && !screens_.blocksGame();
    if (festive && playing) {
        if (specialGreetSeed_ != matchSeed_ + 1) {  // once a match (and again when the day changes)
            specialGreetT_ += dt;
            if (specialGreetT_ > 6.f) {
                int seat = 1;
                std::string text;
                if (ui::specialLine(specialDay_, ui::SpecialSit::Greeting, (uint32_t)specialRng_.next(), seat, text)) {
                    characters_.chat(seat, text, true);
                    if (opt_.autoplay) std::printf("[ozelgun] greeting (seat %d): %s\n", seat, text.c_str());
                }
                specialGreetSeed_ = matchSeed_ + 1;
                specialGreetT_ = 0.f;
                specialChatT_ = specialRng_.uniform(60.f, 120.f);
            }
        } else {
            specialChatT_ -= dt;
            if (specialChatT_ <= 0.f) {
                specialChatT_ = specialRng_.uniform(70.f, 150.f);
                int seat = 1;
                std::string text;
                if (ui::specialLine(specialDay_, ui::SpecialSit::Chatter, (uint32_t)specialRng_.next(), seat, text))
                    characters_.chat(seat, text, false);
            }
        }
    }
    // the sahur davulcu down the street on a Ramazan night (quiet, now and then)
    if (specialDay_ == w3d::GunRamazan && room_.dayPhase() == 3) {
        davulT_ -= dt;
        if (davulT_ <= 0.f) {
            davulT_ = specialRng_.uniform(60.f, 130.f);
            if (audioOn_) audio_.play(ui::Sfx::Davul, room_.venue() == 1 ? 0.6f : 0.45f);
            int seat = 3;
            std::string text;
            if (playing && specialRng_.chance(0.45f) && ui::specialLine(specialDay_, ui::SpecialSit::Davul, (uint32_t)specialRng_.next(), seat, text))
                characters_.chat(seat, text, false);
        }
    }
}

// A goal on the TV on a derby night: the whole room is up (the patrons, the men at the TV), a shout or two, Kel Mahmut.
void App::derbyGoal() {
    if (specialDay_ != w3d::GunMac) return;
    const Vector3 tv = room_.venue() == 1 ? GARDEN_TV : TV_POS;
    if (snapshot_ || opt_.autoplay) std::printf("[ozelgun] a goal in the derby (%s)\n", room_.venue() == 1 ? "the garden's TV" : "inside");
    characters_.crowdReact(r3d::Characters::CrowdCheer, tv, 1.f);
    static const char* const kShouts[] = {"GOOOL!", "Gol gol gol!", "Vur vur inlesin!", "Oley be!", "Ofsayt be!", "Bu ne gol!"};
    for (int k = 0; k < 2; ++k) {
        const char* s = kShouts[specialRng_.range(0, 5)];
        const Vector3 at{tv.x + specialRng_.uniform(0.4f, 1.4f), 1.75f, tv.z + specialRng_.uniform(0.6f, 1.6f)};
        if (shouts_.size() >= 4) shouts_.erase(shouts_.begin());
        shouts_.push_back({s, at, 0.f});
        if (audioOn_) audio_.speak(4, s);
    }
    if (flow_ != Flow::Title) {
        int seat = 2;
        std::string text;
        if (ui::specialLine(specialDay_, ui::SpecialSit::Goal, (uint32_t)specialRng_.next(), seat, text)) characters_.chat(seat, text, true);
    }
}

}  // namespace detail
}  // namespace app
