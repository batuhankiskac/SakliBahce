// App: snapshots and their extra views, frame statistics, --ai-chaos (AppInternal.h).
#include "app/AppInternal.h"

namespace app {
namespace detail {

namespace {

Vector3 lookDir(float yawDeg, float pitchDeg) {
    const float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

} // namespace

// Extra snapshot cameras for judging the space (the seat view is the PlayerCamera itself).
Camera3D viewCamera(const std::string& view, const Camera3D& seat, bool tavla) {
    Camera3D c = seat;
    c.up = {0, 1, 0};
    c.projection = CAMERA_PERSPECTIVE;
    // "straight ahead" of the seat (the tavla table faces another way than ours)
    const float yaw0 = std::atan2(seat.target.x - seat.position.x, -(seat.target.z - seat.position.z)) * RAD2DEG;
    if (tavla && view == "back") { // behind the opponent, looking back at our seat at the tavla table
        c.position = w3d::tavlaToWorld({0.30f, 1.72f, -1.55f});
        c.target = w3d::tavlaToWorld({-0.04f, 0.90f, 0.35f});
        c.fovy = 56.f;
    } else if (view == "left" || view == "right") {
        const float yaw = yaw0 + (view == "left" ? -76.f : 76.f);
        const Vector3 d = lookDir(yaw, -13.f);
        c.target = {c.position.x + d.x, c.position.y + d.y, c.position.z + d.z};
        c.fovy = w3d::FOVY_DEG;
    } else if (view == "back") { // behind Kel Mahmut, looking back at our seat and the wall behind it
        c.position = {0.42f, 1.78f, -2.55f};
        c.target = {-0.05f, 0.92f, 0.65f};
        c.fovy = 58.f;
    } else if (view == "corner") { // high in the front-left corner, over the card players
        c.position = {-3.80f, 2.50f, 3.25f};
        c.target = {0.35f, 0.82f, -0.70f};
        c.fovy = 60.f;
    } else if (view == "up") { // Bahçe: from our seat up into the çınar and the vines
        c.target = {c.position.x - 0.6f, c.position.y + 3.f, c.position.z - 2.4f};
        c.fovy = 70.f;
    } else if (view == "tv") { // (ozelgun) from beside our table toward the TV corner (the derby's scarf, pennants, crowd)
        c.position = {0.2f, 1.85f, 1.3f};
        c.target = {-3.1f, 1.55f, -2.2f};
        c.fovy = 62.f;
    } else if (view == "ocak") { // (ozelgun) toward the back wall and the counter (the flag, the plates, the garden's TV)
        c.position = {-0.6f, 1.7f, 1.6f};
        c.target = {1.3f, 1.25f, -3.2f};
        c.fovy = 62.f;
    } else if (view == "ocakci") { // (Ocakçı) close on the counter's left end: the ocakçı, his pots, the tray's handover
        c.position = {3.05f, 1.72f, -1.85f};
        c.target = {2.05f, 1.15f, -3.05f};
        c.fovy = 52.f;
    } else if (view == "wide") { // Bahçe: from the front-right corner across the garden to the tree and the sea
        c.position = {3.30f, 2.35f, 2.75f};
        c.target = {-1.60f, 2.70f, -3.80f};
        c.fovy = 72.f;
    }
    return c;
}

// ---------------------------------------------------------------- snapshots
// --render-last N: a slow (software) GL only draws the frames that lead up to the picture.
bool App::snapshotRenderNow() const {
    if (opt_.renderLast < 0) return true;
    switch (snapState_) {
    case SnapState::Summary:
    case SnapState::MatchOver: return snapReachedAt_ >= 0 && frame_ - snapReachedAt_ + opt_.renderLast >= opt_.frames;
    default: return frame_ + opt_.renderLast >= opt_.frames;
    }
}

bool App::snapshotDone() {
    switch (snapState_) {
    case SnapState::Summary:
    case SnapState::MatchOver: {
        const ui::ScreenId want = snapState_ == SnapState::Summary ? ui::ScreenId::HandSummary : ui::ScreenId::MatchOver;
        if (snapReachedAt_ < 0 && screens_.current() == want) snapReachedAt_ = frame_;
        return snapReachedAt_ >= 0 && frame_ - snapReachedAt_ >= opt_.frames;
    }
    default: return frame_ >= opt_.frames;
    }
}

bool App::exportSnapshot() {
    Image img = LoadImageFromTexture(rt_.texture);
    ImageFlipVertical(&img);
    // translucent 2D overlays blended into the render target leave alpha < 1 behind: make it opaque
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const bool ok = ExportImage(img, opt_.snapshot.c_str());
    UnloadImage(img);
    std::printf("[snapshot] %s %s (state %s, view %s, frame %ld, particles %d)\n", ok ? "wrote" : "FAILED to write",
                opt_.snapshot.c_str(), opt_.state.empty() ? "game" : opt_.state.c_str(), opt_.view.c_str(), frame_, renderer_.particleCount());
    return ok;
}

// The real window's back buffer (MSAA resolved, HiDPI size), read before the swap.
void App::saveWindowShot() {
    Image img = LoadImageFromScreen();
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const bool ok = ExportImage(img, opt_.screenshot.c_str());
    std::printf("[screenshot] %s %s (%dx%d pixels, window %dx%d points)\n", ok ? "wrote" : "FAILED to write",
                opt_.screenshot.c_str(), img.width, img.height, GetScreenWidth(), GetScreenHeight());
    UnloadImage(img);
}

void App::recordStats(float cpuMs) {
    const float frameMs = GetFrameTime() * 1000.f;
    stats_.add(frameMs, cpuMs, renderer_.particleCount());
}

// --ai-chaos. beforeTable: flips of the mode (directly, like the HUD button or Y, or through the pause menu), the
// pause menu itself and the between-hands buttons. Otherwise (where the player's clicks act, after the table's
// update): the stand-in's moves while the mode is off. A watchdog reports anything that stops moving.
void App::updateChaos(float simDt, bool beforeTable) {
    Chaos& c = chaos_;
    okey::Rng& rng = c.rng;
    const ui::ScreenId cur = screens_.current();
    if (!beforeTable) {
        if (aiSeat() || flow_ != Flow::Playing || game_.handState() != okey::HandState::Playing || game_.current() != HUMAN)
            return;
        if (table_.isAnimating()) return; // (the table takes no clicks while it deals; flights are waited out too)
        c.actIn -= simDt;
        if (c.actIn > 0.f) return;
        c.actIn = rng.uniform(0.05f, 1.2f);
        if (!c.standIn) c.standIn = std::make_unique<okey::Bot>(okey::BotLevel::Normal, rng.next());
        const okey::BotAction a = c.standIn->next(game_, HUMAN);
        okey::ActionResult r = okey::applyBotAction(game_, HUMAN, a);
        ++c.moves;
        if (!r.ok) {
            ++c.movesRejected;
            std::printf("[aichaos] stand-in move rejected (%s): %s\n", actionName(a.kind), r.error.c_str());
            r = okey::applyBotAction(game_, HUMAN, okey::fallbackAction(game_, HUMAN));
            if (!r.ok) forceLegalMove(HUMAN);
        }
        return;
    }

    // watchdog: the turn, the stage, the hand, the screen or the flow must change every now and then
    {
        const okey::PlayerInfo& p = game_.player(std::clamp(game_.current(), 0, 3));
        const uint64_t sig = mix64(((uint64_t)game_.turnNumber() << 20) ^ ((uint64_t)game_.handIndex() << 12) ^
                                   ((uint64_t)game_.stage() << 8) ^ ((uint64_t)cur << 4) ^ (uint64_t)flow_ ^
                                   ((uint64_t)p.hand.size() << 40) ^ ((uint64_t)matchCount_ << 48) ^
                                   ((uint64_t)game_.table().size() << 32));
        if (sig != c.sig || c.pauseLeft > 0.f) {
            c.sig = sig;
            c.sigAt = simClock_;
        }
        const double stall = simClock_ - c.sigAt;
        c.worstStall = std::max(c.worstStall, stall);
        if (stall > 90.0) {
            std::printf("[aichaos] STUCK for %.0f s: flow %d, screen %d, hand %d, turn %d, seat %d, stage %d, mode %s, "
                        "animating %d, think seat %d pending %d\n",
                        stall, (int)flow_, (int)cur, game_.handIndex(), game_.turnNumber(), game_.current(),
                        (int)game_.stage(), aiMode_ ? "on" : "off", table_.isAnimating() ? 1 : 0, think_.seat,
                        think_.pending ? 1 : 0);
            exitCode_ = 2;
            quit_ = true;
            return;
        }
    }

    // the title screen (every other match): "Oyna" or "Yapay Zekayı İzle"
    if (flow_ == Flow::Title) {
        if (c.titleIn < 0.f) return;
        c.titleIn -= simDt;
        if (c.titleIn > 0.f) return;
        c.titleIn = -1.f;
        ++c.titleStarts;
        screens_.show(ui::ScreenId::None);
        handleScreenAction(rng.chance(0.5f) ? ui::ScreenAction::StartAiMatch : ui::ScreenAction::StartMatch);
        return;
    }
    // the pause menu: up for a moment, left through "Devam" or through the mode's entry
    if (c.pauseLeft > 0.f) {
        c.pauseLeft -= simDt;
        if (c.pauseLeft > 0.f) return;
        if (cur == ui::ScreenId::Paused) {
            screens_.show(ui::ScreenId::None);
            if (c.pauseFlips) {
                ++c.flips;
                ++c.pauseFlipCount;
                handleScreenAction(ui::ScreenAction::ToggleAiMode);
            }
        }
        return;
    }
    c.nextPause -= simDt;
    if (c.nextPause <= 0.f && cur == ui::ScreenId::None && flow_ != Flow::Title) {
        c.nextPause = rng.uniform(4.f, 40.f);
        c.pauseLeft = rng.uniform(0.1f, 2.5f);
        c.pauseFlips = rng.chance(0.5f);
        ++c.pauses;
        screens_.show(ui::ScreenId::Paused);
        return;
    }
    // flips at random moments: mid-think, mid-flight, mid-deal, between hands (the HUD button / Y)
    c.nextFlip -= simDt;
    if (c.nextFlip <= 0.f) {
        c.nextFlip = rng.chance(0.2f) ? rng.uniform(0.f, 0.3f) : rng.uniform(0.3f, 9.f);
        ++c.flips;
        setAiMode(!aiMode_);
    }
    // the between-hands buttons when nobody else presses them (the mode is off)
    if (!aiSeat() && (cur == ui::ScreenId::HandSummary || cur == ui::ScreenId::MatchOver)) {
        if (c.pressIn < 0.f) c.pressIn = rng.uniform(0.2f, 4.f);
        c.pressIn -= simDt;
        if (c.pressIn <= 0.f) {
            c.pressIn = -1.f;
            ++c.presses;
            pressBetweenHands();
        }
    } else {
        c.pressIn = -1.f;
    }
}

} // namespace detail
} // namespace app
