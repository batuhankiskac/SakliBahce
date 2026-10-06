// r3d::Characters — public API, banter → speech bubbles, per-frame orchestration and draw submission.
// Geometry: CharactersMesh/Props.cpp (people and props), CharactersSdf.cpp (SDF modelling, surface nets,
// init-time job batch), CharactersSimplify.cpp (mesh decimation). Opponent animation: CharactersAnim.cpp.
// Patrons + çaycı: CharactersCrowd.cpp. Internal types: CharactersInternal.h, CharactersState.h.
#include "r3d/Characters.h"
#include "r3d/CharactersState.h"
#include "ui/Common.h"

#include <algorithm>

namespace r3d {

namespace {
chr::Cast* g_lastCast = nullptr;

constexpr float kBubbleFont = 22.f;
constexpr float kNameFont = 15.f;
constexpr float kFadeIn = 0.22f, kFadeOut = 0.35f;
constexpr int kMaxVisible = 2;

Color nameColor(int who) {
    switch (who) {
    case 1: return Color{128, 84, 40, 255};
    case 2: return Color{40, 72, 140, 255};
    case 3: return Color{128, 34, 34, 255};
    default: return Color{40, 104, 64, 255};
    }
}
} // namespace

namespace chr {
Cast* debugLastCast() { return g_lastCast; }
} // namespace chr

using namespace chr;

// ============================================================================ lifecycle
Characters::Characters() : impl_(new Impl) { impl_->owner = this; }

Characters::~Characters() {
    if (impl_->ready && impl_->renderer) shutdown(*impl_->renderer);
    if (g_lastCast == impl_) g_lastCast = nullptr;
    delete impl_;
}

bool Characters::init(Renderer& r, uint64_t seed) {
    Impl& m = *impl_;
    if (m.ready) return true;
    m.renderer = &r;
    m.seed = seed ? seed : 1;
    m.rng = Rng(m.seed * 7919u + 17u);
    buildAll(m.M, r, m.seed);
    m.setupOpponents();
    m.setupCrowd();
    m.initLife(r);
    m.banter.reset(m.seed * 31u + 7u);
    m.banter.setNames(m.names);
    m.banter.setEnabled(!m.titleMode);
    m.ready = true;
    g_lastCast = &m;
    // settle every pose (silently) so the first frame doesn't start from zero
    std::function<void(ui::Sfx)> keep = playSfx;
    playSfx = nullptr;
    Camera3D cam{};
    cam.position = w3d::EYE;
    cam.target = {0, w3d::EYE.y + std::tan(w3d::EYE_PITCH_DEG * DEG2RAD), w3d::EYE.z - 1.f};
    cam.up = {0, 1, 0};
    cam.fovy = w3d::FOVY_DEG;
    for (int i = 0; i < 40; ++i) update(1.f / 40.f, cam);
    playSfx = keep;
    return true;
}

void Characters::shutdown(Renderer& r) {
    Impl& m = *impl_;
    if (!m.ready) return;
    m.freeLife(r);
    freeAll(m.M, r);
    m.ready = false;
}

void Characters::setNames(const std::array<std::string, 4>& names) {
    impl_->names = names;
    impl_->banter.setNames(names);
}

void Characters::setActiveSeat(int seat) {
    Impl& m = *impl_;
    if (seat != m.activeSeat) {
        m.activeSeat = seat;
        // -1 also means "paused / behind a menu": coming back to the same turn keeps its clocks running on
        if (seat >= 0 && seat != m.lastTurnSeat) {
            m.humanWait = 0.f;
            if (seat >= 1 && seat <= 3) m.opp[seat].turnT = 0.f;
        }
        if (seat >= 0) m.lastTurnSeat = seat;
    }
}

void Characters::onEvent(const okey::GameEvent& e, const okey::Game& g) {
    Impl& m = *impl_;
    if (!m.ready) return;
    m.react(e, g);
    if (!m.titleMode) m.banter.onEvent(e, g);
    if (e.player == 0) m.lastHumanEventT = m.time;
}

void Characters::onTvGoal() {
    Impl& m = *impl_;
    if (!m.ready) return;
    if (!m.titleMode) m.banter.tvGoal();
    // the TV hangs high in the back-left corner; Mahmut cheers, the others glance over
    const Vector3 tv = chr::kTvPos;
    for (int s = 1; s <= 3; ++s) {
        Opponent& o = m.opp[s];
        if (s == 2) {
            o.gazeGoal = tv;
            o.gazeHold = 3.f;
            if (!o.arm[0].track.on && !o.arm[1].track.on) m.startGesture(o, G_TvCheer, 1.8f);
            m.setMood(o, Mood::Surprised, 1.2f);
        } else if (m.rng.chance(0.6f)) {
            o.gazeGoal = tv;
            o.gazeHold = m.rng.f(1.f, 2.2f);
        }
    }
}

void Characters::say(int seat, const std::string& text, float seconds) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    m.pushLine(seat, text, seconds, 4.5f);
}

void Characters::reach(int seat, Vector3 target, int mode) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    chr::Opponent& o = m.opp[seat];
    const Vector3 l = xfPoint(o.rootInv, target);
    int pref = l.x >= 0.f ? 0 : 1;
    if (o.kind == 0 && pref == 1 && l.x > -0.3f) pref = 0; // Rıza's tespih hand, Mahmut's cigarette hand
    if (o.kind == 1 && pref == 0 && l.x < 0.3f) pref = 1;
    if (o.arm[pref].track.on && o.arm[pref].track.kind == chr::TK_Sip) pref = 1 - pref;
    m.reachTo(o, pref, target, mode == 1 ? 1 : 0);
    o.chinRest = false;
}

void Characters::react(int seat, int mood, Vector3 lookAt) {
    Impl& m = *impl_;
    if (!m.ready) return;
    for (int s = 1; s <= 3; ++s) {
        chr::Opponent& o = m.opp[s];
        if (s != seat && m.rng.chance(0.7f)) {
            o.gazeGoal = lookAt;
            o.gazeHold = m.rng.f(0.8f, 1.6f);
        }
    }
    if (seat < 1 || seat > 3 || mood <= 0) return;
    const chr::Mood md = mood == 1 ? chr::Mood::Happy : mood == 2 ? chr::Mood::Grumpy : chr::Mood::Surprised;
    m.setMood(m.opp[seat], md, 2.2f);
}

bool Characters::chat(int seat, const std::string& text, bool important) {
    Impl& m = *impl_;
    if (!m.ready || m.titleMode || seat < 1 || seat > 4 || text.empty()) return false;
    return m.banter.external(seat, text, important);
}

void Characters::onCatMeow(Vector3 where) {
    Impl& m = *impl_;
    // the regulars glance over unless they are busy with their own turn; patrons near it look too
    for (int s = 1; s <= 3; ++s) {
        chr::Opponent& o = m.opp[s];
        if (s == m.activeSeat || !m.rng.chance(0.75f)) continue;
        o.gazeGoal = where;
        o.gazeHold = m.rng.f(1.2f, 2.4f);
    }
    for (chr::Patron& p : m.patrons) {
        if (p.reading || Vector3Distance(p.pos, where) > 2.6f || !m.rng.chance(0.6f)) continue;
        p.gazeGoal = where;
        p.gazeHold = m.rng.f(1.f, 2.2f);
    }
    if (!m.titleMode) m.banter.catMeow();
}

ui::Banter& Characters::banter() { return impl_->banter; }

void Characters::setAnimationSpeed(float speed) { impl_->animSpeed = std::clamp(speed, 0.25f, 4.f); }

void Characters::setTitleMode(bool on) {
    Impl& m = *impl_;
    m.titleMode = on;
    m.banter.setEnabled(!on);
    if (on) {
        for (auto& q : m.bubbles.pending) q.clear();
        m.bubbles.on.fill(false);
        m.activeSeat = -1;
    }
}

Vector3 Characters::headPosition(int seat) const {
    const Impl& m = *impl_;
    if (seat <= 0 || seat > 3) return m.viewerSet ? m.viewer.position : w3d::EYE;
    if (!m.ready) return w3d::seatLocal(seat, 0.f, w3d::SEAT_DIST - 0.12f, w3d::HEAD_Y);
    return xfPoint(m.opp[seat].headW, {0, 0.11f, -0.01f});
}

// ============================================================================ update
void Cast::sfx(ui::Sfx s) {
    if (owner && owner->playSfx) owner->playSfx(s);
}

void Characters::update(float dt, const Camera3D& viewer) {
    Impl& m = *impl_;
    if (!m.ready) return;
    dt = std::clamp(dt, 0.f, 0.1f);
    m.time += dt;
    m.viewer = viewer;
    m.viewerSet = true;
    if (m.activeSeat == 0) m.humanWait += dt;
    else if (m.activeSeat > 0) m.humanWait = 0.f;
    for (int s = 1; s <= 3; ++s) m.updateOpponent(m.opp[s], dt);
    m.updateLife(dt);
    m.updateCrowd(dt);
    m.updateGlasses(dt);
    m.emitSteam(dt);

    // banter → bubbles
    float lvl = 0.f;
    for (int s = 1; s <= 3; ++s) lvl += m.glass[s].level;
    bool teaLow = lvl / 3.f < 0.3f;
    m.banter.setGameRunning(m.activeSeat >= 0);  // App sets -1 while the game is paused or behind a menu
    m.banter.update(dt, teaLow, m.boy.state != 0 || m.boy.called);
    ui::BanterLine line;
    while (m.banter.pop(line)) {
        if (line.cue == ui::BanterCue::TeaOrder) m.boy.called = true;
        if (!m.titleMode) m.pushLine(line.seat, line.text, line.seconds, line.maxWait, line.cue == ui::BanterCue::TeaOrder);
    }
    m.updateBubbles(dt);
}

// ============================================================================ bubbles
void Cast::pushLine(int who, const std::string& text, float seconds, float maxWait, bool teaOrder) {
    if (titleMode || who < 1 || who > 4 || text.empty()) return;
    Bubble b;
    b.who = who;
    b.teaOrder = teaOrder;
    b.text = text;
    b.dur = std::max(seconds, 1.2f);
    b.maxWait = maxWait;
    // word wrap
    const float maxW = 330.f, padX = 16.f;
    std::string cur, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && ui::measureText(ui::FontId::Ui, cand, kBubbleFont).x > maxW - 2 * padX) {
            b.lines.push_back(cur);
            cur = word;
        } else {
            cur = cand;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!cur.empty()) b.lines.push_back(cur);
    float w = 0.f;
    for (const std::string& l : b.lines) w = std::max(w, ui::measureText(ui::FontId::Ui, l, kBubbleFont).x);
    w = std::max(w, ui::measureText(ui::FontId::UiBold, who <= 3 ? names[who] : std::string("Çaycı"), kNameFont).x);
    b.w = std::max(90.f, w + 2 * padX);
    b.h = (float)b.lines.size() * (kBubbleFont * 1.12f + 2.f) - 2.f + 22.f + kNameFont + 2.f;
    auto& q = bubbles.pending[who];
    if (bubbles.on[who] && bubbles.cur[who].text == text) return;
    for (const Bubble& o : q)
        if (o.text == text) return;
    if (q.size() >= 3) q.pop_front();
    q.push_back(std::move(b));
}

void Cast::updateBubbles(float dt) {
    BubbleState& B = bubbles;
    int visible = 0;
    for (int w = 1; w <= 4; ++w) {
        if (!B.on[w]) continue;
        Bubble& b = B.cur[w];
        b.t += dt;
        if (b.t >= b.dur + kFadeOut) B.on[w] = false;
        else ++visible;
    }
    for (int w = 1; w <= 4; ++w) {
        auto& q = B.pending[w];
        for (Bubble& b : q) b.queued += dt;
        while (!q.empty() && q.front().queued > q.front().maxWait) q.pop_front();
    }
    while (visible < kMaxVisible && time - B.lastStart > 0.45f) {
        int best = -1;
        float bw = -1.f;
        for (int w = 1; w <= 4; ++w) {
            if (B.on[w] || B.pending[w].empty()) continue;
            if (B.pending[w].front().queued > bw) {
                bw = B.pending[w].front().queued;
                best = w;
            }
        }
        if (best < 0) break;
        B.cur[best] = B.pending[best].front();
        B.pending[best].pop_front();
        B.cur[best].t = 0.f;
        B.cur[best].placed = false;
        B.on[best] = true;
        B.lastStart = time;
        ++visible;
        banter.spoke(best);  // (the çaycı too: Banter spaces his lines as well)
        if (owner && owner->speak) owner->speak(best, B.cur[best].text);
        if (best <= 3) {
            Opponent& o = opp[best];
            o.talk = B.cur[best].text;
            o.jawKeys.clear();
            o.talkT = 0.f;
            o.talkDur = B.cur[best].dur * 0.85f;
            // who is he talking to?
            bool toHuman = activeSeat == 0 || (time - lastHumanEventT < 3.5f) || rng.chance(0.25f);
            o.talkToHuman = toHuman;
            if (toHuman) {
                o.gazeGoal = viewerPos();
                o.gazeHold = std::min(2.2f, o.talkDur);
            } else {
                int other = 1 + rng.i(3);
                if (other == best) other = other % 3 + 1;
                o.gazeGoal = headTarget(other);
                o.gazeHold = std::min(2.f, o.talkDur);
            }
            if (B.cur[best].teaOrder) {
                // "Çaycı! İki çay!" — a raised hand toward the counter; the boy answers from there
                o.gazeGoal = Vector3Add(w3d::COUNTER_POS, {0, 1.3f, 0});
                o.gazeHold = 1.6f;
                startGesture(o, G_CallTea, 1.9f);
                if (boy.state == 0) boy.replyIn = rng.f(0.9f, 1.4f);
            } else if (o.kind == 1 && !o.arm[1].track.on && rng.chance(0.6f)) {
                // Mahmut talks with his (free) left hand
                const float hs = o.L.handScale;
                Vector3 f{0.3f, 0.35f, -0.88f}, p{0, 1, 0.2f};
                f = vnorm(f);
                p = vnorm(Vector3Subtract(p, Vector3Scale(f, Vector3DotProduct(p, f))));
                std::vector<Key> k{Key{}};
                auto W = [&](Vector3 pc) {
                    Vector3 X, Y, Z;
                    Z = vnorm(Vector3Negate(f));
                    Y = vnorm(Vector3Negate(p));
                    X = Vector3CrossProduct(Y, Z);
                    Vector3 off{-PALM_CENTER.x, PALM_CENTER.y, PALM_CENTER.z};
                    return Vector3Subtract(pc, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(X, off.x), Vector3Scale(Y, off.y)), Vector3Scale(Z, off.z)), hs));
                };
                Key a;
                a.t = 0.45f;
                a.pos = W({-0.20f, 0.93f, -0.30f});
                a.fingers = f;
                a.palm = p;
                a.pose = HandPose::Open;
                k.push_back(a);
                float t = 0.45f;
                for (int i = 0; i < 3; ++i) {
                    Key b2 = a;
                    t += rng.f(0.3f, 0.45f);
                    b2.t = t;
                    b2.pos = Vector3Add(a.pos, {rng.f(-0.03f, 0.03f), rng.f(-0.02f, 0.05f), rng.f(-0.03f, 0.02f)});
                    b2.pose = i % 2 ? HandPose::Open : HandPose::Point;
                    k.push_back(b2);
                }
                Key rest;
                restPose(o, 1, 0, rest);
                rest.t = t + 0.6f;
                k.push_back(rest);
                startTrack(o, 1, TK_Gesture, k);
            }
            for (int s = 1; s <= 3; ++s) {
                if (s == best || !rng.chance(0.7f)) continue;
                opp[s].gazeGoal = headTarget(best);
                opp[s].gazeHold = rng.f(1.f, 2.2f);
            }
        } else {
            boy.talk = B.cur[best].text;
            boy.talkT = 0.f;
            boy.talkDur = B.cur[best].dur * 0.8f;
            for (int s = 1; s <= 3; ++s)
                if (rng.chance(0.5f)) {
                    opp[s].gazeGoal = xfPoint(boy.headW, {0, 0.1f, -0.05f});
                    opp[s].gazeHold = 1.2f;
                }
        }
    }
}

void Characters::drawOverlay(const Renderer& r) {
    Impl& m = *impl_;
    if (!m.ready || m.titleMode) return;
    BubbleState& B = m.bubbles;
    const Camera3D& cam = r.lastCamera();
    Vector3 camFwd = vnorm(Vector3Subtract(cam.target, cam.position));
    Vector3 camRight = vnorm(Vector3CrossProduct(camFwd, cam.up));
    struct Placed {
        int who;
        Rectangle box;
        Vector2 tail;     // where the tail points (mouth, or off-screen direction)
        bool offscreen;
        float alpha, scale;
    };
    // Table3D floats a name plate beside each opponent's head at eye level; estimate those rectangles so the
    // bubbles (always above the head) never cover them.
    std::vector<Rectangle> plates;
    std::array<Rectangle, 5> faces{};  // projected face rectangles (bubbles keep off the faces too)
    std::array<float, 5> plateSide{};
    for (int s = 1; s <= 4; ++s) {
        Vector3 head = s <= 3 ? headPosition(s) : xfPoint(m.boy.headW, {0, 0.11f, -0.01f});
        Vector2 hc{}, he{};
        if (Vector3DotProduct(Vector3Subtract(head, cam.position), camFwd) < 0.05f) continue;
        if (!r.projectToVirtual(head, hc) || !r.projectToVirtual(Vector3Add(head, Vector3Scale(camRight, 0.1f)), he)) continue;
        const float rad = clampf(std::fabs(he.x - hc.x), 16.f, 140.f);
        faces[(size_t)s] = {hc.x - rad * 0.95f, hc.y - rad * 1.25f, rad * 1.9f, rad * 2.2f};
        plateSide[(size_t)s] = hc.x > ui::VW * 0.5f + 150.f ? -1.f : 1.f;
        if (s == 4) continue;
        const float w = std::max(ui::measureText(ui::FontId::UiBold, m.names[s], 17.f).x + 70.f, 100.f), h = 46.f;
        const float side = hc.x > ui::VW * 0.5f + 150.f ? -1.f : 1.f;
        Vector2 c{hc.x + side * (rad + 14.f + w * 0.5f), hc.y - rad * 0.15f};
        c.x = clampf(c.x, w * 0.5f + 8.f, ui::VW - w * 0.5f - 8.f);
        c.y = clampf(c.y, h * 0.5f + 8.f, ui::VH - 220.f);
        plates.push_back({c.x - w * 0.5f - 8.f, c.y - h * 0.5f - 8.f, w + 16.f, h + 16.f});
    }
    // The chalk scoreboard on the back wall (integration): bubbles prefer not to hide the scores either.
    Rectangle board{};
    {
        const Vector3 c = w3d::SCOREBOARD_POS;
        const float hw = w3d::SCOREBOARD_W * 0.5f, hh = w3d::SCOREBOARD_H * 0.5f;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            const Vector3 p{c.x + (k & 1 ? hw : -hw), c.y + (k & 2 ? hh : -hh), c.z};
            Vector2 v{};
            if (Vector3DotProduct(Vector3Subtract(p, cam.position), camFwd) < 0.05f || !r.projectToVirtual(p, v)) {
                ok = false;
                break;
            }
            x0 = std::min(x0, v.x);
            y0 = std::min(y0, v.y);
            x1 = std::max(x1, v.x);
            y1 = std::max(y1, v.y);
        }
        if (ok) board = {x0, y0, x1 - x0, y1 - y0};
    }
    // Place the bubbles oldest first: each picks, among a few candidate spots around its speaker's head, the
    // one that covers no face, no name plate and no earlier bubble, stays close to the mouth and close to
    // where it was last frame (no jumping around).
    auto overlap = [](const Rectangle& a, const Rectangle& b) {
        float w = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
        float h = std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
        return (w > 0.f && h > 0.f) ? w * h : 0.f;
    };
    int order[4] = {1, 2, 3, 4};
    std::sort(order, order + 4, [&](int a, int b) { return B.cur[(size_t)a].t > B.cur[(size_t)b].t; });
    const float margin = 14.f;
    std::vector<Rectangle> taken;
    std::vector<Placed> placed;
    for (int w : order) {
        if (!B.on[(size_t)w]) continue;
        Bubble& b = B.cur[(size_t)w];
        Vector3 headTop, mouth;
        if (w <= 3) {
            const Opponent& o = m.opp[w];
            headTop = xfPoint(o.headW, {0, o.pm->face.headTop + 0.03f, -0.02f});
            mouth = xfPoint(o.headW, Vector3Add(o.pm->face.mouth, {0, -0.004f, -0.03f}));
        } else {
            headTop = xfPoint(m.boy.headW, {0, m.boy.pm->face.headTop + 0.03f, -0.02f});
            mouth = xfPoint(m.boy.headW, Vector3Add(m.boy.pm->face.mouth, {0, -0.004f, -0.03f}));
        }
        Vector2 top{}, mo{};
        const bool inFront = Vector3DotProduct(Vector3Subtract(mouth, cam.position), camFwd) > 0.05f;
        bool okTop = inFront && r.projectToVirtual(headTop, top);
        bool okMouth = inFront && r.projectToVirtual(mouth, mo);
        bool offscreen = !okTop || !okMouth || mo.x < 0 || mo.x > ui::VW || mo.y < 0 || mo.y > ui::VH;
        const float bw = b.w, bh = b.h;
        Vector2 tail = mo;
        std::vector<Vector2> cand;
        if (!offscreen) {
            const Rectangle& face = faces[(size_t)w];
            const float bias = clampf((ui::VW * 0.5f - top.x) / (ui::VW * 0.5f), -1.f, 1.f) * bw * 0.3f;
            cand.push_back({top.x - bw * 0.5f + bias, top.y - bh - 20.f});       // above the head
            for (int k = -3; k <= 3; ++k)                                        // above, slid sideways
                for (int j = 0; j < 2; ++j)
                    if (k != 0) cand.push_back({top.x - bw * 0.5f + (float)k * bw * 0.4f, top.y - bh - 12.f + (float)j * bh * 0.6f});
            if (face.width > 0.f) {
                const float away = -plateSide[(size_t)w];
                for (int k = 0; k < 2; ++k) {
                    float sd = k == 0 ? away : -away;
                    float x = sd < 0.f ? face.x - 12.f - bw : face.x + face.width + 12.f;
                    cand.push_back({x, face.y});                                 // beside the face
                    cand.push_back({x, face.y + face.height * 0.45f});
                    cand.push_back({x, face.y - bh * 0.6f});
                }
            }
        } else {
            // the speaker is out of view: pin the bubble to the nearest screen edge, tail pointing out
            Vector3 d = Vector3Subtract(mouth, cam.position);
            bool right = Vector3DotProduct(d, camRight) > 0.f;
            float upv = Vector3DotProduct(d, cam.up);
            float x = right ? ui::VW - bw - margin - 24.f : margin + 24.f;
            float y0 = clampf(ui::VH * 0.30f - upv * 200.f, margin, ui::VH * 0.55f);
            for (int k = 0; k < 6; ++k) cand.push_back({x, y0 + (k % 2 ? 1.f : -1.f) * (float)((k + 1) / 2) * (bh + 12.f)});
            tail = {right ? ui::VW - 4.f : 4.f, 0.f};
        }
        Rectangle best{};
        float bestCost = 1e30f;
        for (Vector2 c : cand) {
            Rectangle box{clampf(c.x, margin, ui::VW - margin - bw), clampf(c.y, margin, ui::VH - margin - bh), bw, bh};
            const float area = bw * bh;
            float cost = 0.f;
            for (int f = 1; f <= 4; ++f)
                if (faces[(size_t)f].width > 0.f) cost += 900.f * overlap(box, faces[(size_t)f]) / area;
            for (const Rectangle& pr : plates) cost += 4000.f * overlap(box, pr) / area;
            if (board.width > 0.f) cost += 600.f * overlap(box, board) / area;
            for (const Rectangle& tb : taken)
                cost += 3000.f * overlap(Rectangle{box.x - 8.f, box.y - 8.f, bw + 16.f, bh + 16.f}, tb) / area;
            if (!offscreen) {
                Vector2 anchor{box.x + bw * 0.5f, box.y + bh};
                cost += 0.35f * std::sqrt((anchor.x - mo.x) * (anchor.x - mo.x) + (anchor.y - mo.y) * (anchor.y - mo.y));
            }
            if (b.placed) cost += 0.25f * (std::fabs(box.x - b.goal.x) + std::fabs(box.y - b.goal.y));
            if (cost < bestCost) {
                bestCost = cost;
                best = box;
            }
        }
        taken.push_back(best);
        // smooth motion (camera sway, head bobs)
        const float ft = GetFrameTime();
        b.goal = {best.x, best.y};
        if (!b.placed || ft <= 0.f) {
            b.pos = b.goal;
            b.placed = true;
        } else {
            float k = 1.f - std::exp(-12.f * ft);
            b.pos.x += (best.x - b.pos.x) * k;
            b.pos.y += (best.y - b.pos.y) * k;
        }
        Rectangle box{b.pos.x, b.pos.y, bw, bh};
        if (offscreen) tail.y = box.y + bh * 0.5f;
        float aIn = ui::clamp01(b.t / kFadeIn);
        float aOut = ui::clamp01((b.dur + kFadeOut - b.t) / kFadeOut);
        placed.push_back({w, box, tail, offscreen, std::min(aIn, aOut), 0.86f + 0.14f * ui::easeOutBack(aIn)});
    }
    for (const Placed& P : placed) {
        const Bubble& b = B.cur[P.who];
        float alpha = P.alpha;
        if (alpha <= 0.01f) continue;
        Rectangle box = P.box;
        // tail anchor on the box edge nearest the target
        Vector2 c{box.x + box.width * 0.5f, box.y + box.height * 0.5f};
        Vector2 d{P.tail.x - c.x, P.tail.y - c.y};
        float dl = std::sqrt(d.x * d.x + d.y * d.y);
        Vector2 dir = dl > 1e-3f ? Vector2{d.x / dl, d.y / dl} : Vector2{0, 1};
        Vector2 base;
        Vector2 side;
        if (std::fabs(dir.y) * box.width > std::fabs(dir.x) * box.height) {
            base = {clampf(c.x + dir.x * box.height * 0.5f / std::max(std::fabs(dir.y), 1e-3f), box.x + 22.f, box.x + box.width - 22.f),
                    dir.y > 0 ? box.y + box.height - 2.f : box.y + 2.f};
            side = {1, 0};
        } else {
            base = {dir.x > 0 ? box.x + box.width - 2.f : box.x + 2.f,
                    clampf(c.y + dir.y * box.width * 0.5f / std::max(std::fabs(dir.x), 1e-3f), box.y + 16.f, box.y + box.height - 16.f)};
            side = {0, 1};
        }
        Vector2 td{P.tail.x - base.x, P.tail.y - base.y};
        float tl = std::sqrt(td.x * td.x + td.y * td.y);
        float len = std::min(P.offscreen ? 20.f : 58.f, tl * 0.72f);
        Vector2 tdir = tl > 1e-3f ? Vector2{td.x / tl, td.y / tl} : Vector2{0, 1};
        if (!P.offscreen) {
            // the tail points at the speaker but stops short of his face and of any name plate (it must not
            // stab across the eyes or end on somebody's name)
            auto clip = [&](const Rectangle& rc) {
                if (rc.width <= 0.f) return;
                const Rectangle e{rc.x - 4.f, rc.y - 4.f, rc.width + 8.f, rc.height + 8.f};
                if (base.x > e.x && base.x < e.x + e.width && base.y > e.y && base.y < e.y + e.height) return;
                float t0 = 0.f, t1 = len;  // Liang–Barsky: where the tail enters the rectangle
                const float pq[4][2] = {{-tdir.x, base.x - e.x}, {tdir.x, e.x + e.width - base.x},
                                        {-tdir.y, base.y - e.y}, {tdir.y, e.y + e.height - base.y}};
                for (const auto& v : pq) {
                    if (std::fabs(v[0]) < 1e-6f) {
                        if (v[1] < 0.f) return;
                        continue;
                    }
                    const float t = v[1] / v[0];
                    if (v[0] < 0.f) t0 = std::max(t0, t);
                    else t1 = std::min(t1, t);
                }
                if (t0 <= t1) len = std::min(len, t0);
            };
            clip(faces[(size_t)P.who]);
            for (const Rectangle& pr : plates) clip({pr.x + 8.f, pr.y + 8.f, pr.width - 16.f, pr.height - 16.f});
        }
        Vector2 tip{base.x + tdir.x * std::max(len, 12.f), base.y + tdir.y * std::max(len, 12.f)};
        // scale around the tail tip (the bubble pops out of the mouth)
        const float sc = P.scale;
        auto S = [&](Vector2 p) { return Vector2{tip.x + (p.x - tip.x) * sc, tip.y + (p.y - tip.y) * sc}; };
        Vector2 o = S({box.x, box.y});
        Rectangle rr{o.x, o.y, box.width * sc, box.height * sc};
        Vector2 bc = S(base);
        float bw = 10.f * sc;
        Vector2 b1{bc.x - side.x * bw, bc.y - side.y * bw}, b2{bc.x + side.x * bw, bc.y + side.y * bw};
        Color paper = ui::withAlpha(Color{252, 246, 230, 255}, alpha);
        Color edge = ui::withAlpha(Color{96, 64, 38, 255}, alpha * 0.9f);
        Color shadow = ui::withAlpha(Color{0, 0, 0, 255}, alpha * 0.30f);
        auto tri = [](Vector2 a, Vector2 b, Vector2 c2, Color col) {
            // raylib wants counter-clockwise order
            float cr = (b.x - a.x) * (c2.y - a.y) - (b.y - a.y) * (c2.x - a.x);
            if (cr < 0) DrawTriangle(a, b, c2, col);
            else DrawTriangle(a, c2, b, col);
        };
        DrawRectangleRounded({rr.x + 3, rr.y + 5, rr.width, rr.height}, 0.35f, 10, shadow);
        tri({b1.x + 3, b1.y + 5}, {b2.x + 3, b2.y + 5}, {tip.x + 3, tip.y + 5}, shadow);
        DrawRectangleRounded({rr.x - 1.8f, rr.y - 1.8f, rr.width + 3.6f, rr.height + 3.6f}, 0.35f, 10, edge);
        Vector2 ob1{b1.x - side.x * 2.f, b1.y - side.y * 2.f}, ob2{b2.x + side.x * 2.f, b2.y + side.y * 2.f};
        tri(ob1, ob2, {tip.x + tdir.x * 2.2f, tip.y + tdir.y * 2.2f}, edge);
        DrawRectangleRounded(rr, 0.35f, 10, paper);
        tri(b1, b2, tip, paper);
        DrawRectangleRounded({rr.x + 3, rr.y + rr.height * 0.58f, rr.width - 6, rr.height * 0.38f}, 0.5f, 8,
                             ui::withAlpha(Color{222, 204, 172, 255}, alpha * 0.18f));
        // speaker name, then the line
        float fsN = kNameFont * sc;
        std::string nm = P.who <= 3 ? m.names[P.who] : std::string("Çaycı");
        ui::drawText(ui::FontId::UiBold, nm, {std::round(rr.x + 14.f * sc), std::round(rr.y + 7.f * sc)}, fsN,
                     ui::withAlpha(nameColor(P.who), alpha * 0.9f));
        float fs = kBubbleFont * sc;
        float lh = (kBubbleFont * 1.12f + 2.f) * sc;
        float y = rr.y + (9.f + kNameFont + 2.f) * sc;
        for (const std::string& l : b.lines) {
            Vector2 sz = ui::measureText(ui::FontId::Ui, l, fs);
            ui::drawText(ui::FontId::Ui, l, {std::round(rr.x + (rr.width - sz.x) * 0.5f), std::round(y)}, fs,
                         ui::withAlpha(ui::pal::TextDark, alpha));
            y += lh;
        }
    }
}

// ============================================================================ submission
void Cast::submitSeatedBody(Renderer& r, const Seated& s, bool detailed) {
    const PersonMeshes& pm = *s.pm;
    // the regulars cast everything; background patrons only their bodies (cheap and enough to ground them)
    const uint32_t body = CastShadow, limbs = detailed ? CastShadow : 0u;
    r.submit(&pm.lower, &pm.cloth, s.root, body);
    r.submit(&pm.torso, &pm.cloth, s.torsoW, body);
    // heads don't cast into the shadow map: the lamp grazes the faces, and self-shadowing there only
    // produces shadow-map stair steps on the cheeks
    r.submit(&pm.head, &pm.headMat, s.headW, 0);
    for (int a = 0; a < 2; ++a) {
        const Arm& A = s.arm[a];
        r.submit(&pm.upper[a], &pm.cloth, A.upper, limbs);
        r.submit(&pm.fore[a], &pm.cloth, A.fore, limbs);
        r.submit(detailed ? &M.hand[a][(int)A.pose] : &M.handLo[a][(int)A.pose], &pm.skin, A.hand, limbs);
    }
    submitCount += 9;
}

void Cast::submitOpponent(Renderer& r, const Opponent& o) {
    submitSeatedBody(r, o, true);
    const PersonMeshes& pm = *o.pm;
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.eye[o.irisMesh], &M.eyeMat, o.eyeW[i], 0);
        r.submit(&M.lidUpper, &pm.skin, o.lidW[i], 0);
        r.submit(&pm.brow[i], &pm.hairMat, o.browW[i], 0);
    }
    // the open mouth (teeth, tongue) only when the jaw actually drops; closed lips show the dark seam
    if (o.jaw + o.face.mouthWide * 0.4f > 0.03f) {
        r.submit(&M.mouthCavity, &M.mouthMat, o.mouthW, 0);
        ++submitCount;
    }
    r.submit(&M.lowerLip[o.lipVariant], &M.lipMat, o.lipW, 0);
    r.submit(&pm.stache, &pm.hairMat, o.headW, 0);
    submitCount += 8;
    if (o.kind == 2) {
        r.submit(&M.spectacles, &M.frameMat, o.headW, 0);
        r.submit(&M.lenses, &M.lensMat, o.headW, Transparent | DoubleSided);
        submitCount += 2;
    }
    if (o.kind == 1) {
        r.submit(&M.cigarette, &M.cigMat, o.cigW, 0);
        r.submit(&M.ember, &M.emberMat, o.cigW, 0);
        submitCount += 2;
        Vector3 tip = xfPoint(o.cigW, {0, 0.0748f, 0});
        float flick = 0.75f + 0.25f * std::sin(time * 23.f) * std::sin(time * 7.3f);
        float glow = 0.35f + 0.65f * o.drag;
        r.submitGlow(tip, 0.012f + 0.02f * o.drag, Color{255, 120, 40, 255}, glow * flick);
        if (o.drag > 0.05f) {
            PointLight pl;
            pl.position = Vector3Add(tip, {0, 0.01f, 0});
            pl.color = Color{255, 110, 40, 255};
            pl.intensity = 0.55f * o.drag;
            pl.range = 0.45f;
            r.addPointLight(pl);
        }
    }
    if (o.kind == 0 && o.tespih.init) {
        const Chain& c = o.tespih;
        Vector3 hint = xfDir(o.root, {0, 0, 1});
        for (size_t i = 0; i + 1 < c.p.size(); ++i) {
            float d = Vector3Distance(c.p[i], c.p[i + 1]);
            Matrix bm = mul(S3(1.f, d / TESPIH_SEG, 1.f), boneMatrix(c.p[i], c.p[i + 1], hint));
            r.submit(&M.bead4, &M.amber, bm, 0);
        }
        float d0 = Vector3Distance(c.tail[0], c.tail[1]), d1 = Vector3Distance(c.tail[1], c.tail[2]);
        r.submit(&M.imame, &M.amber, mul(S3(1.f, d0 / 0.0445f, 1.f), boneMatrix(c.tail[0], c.tail[1], hint)), 0);
        r.submit(&M.tassel, &M.tassleMat, mul(S3(1.f, d1 / 0.0425f, 1.f), boneMatrix(c.tail[1], c.tail[2], hint)), 0);
        submitCount += (int)c.p.size() + 1;
    }
}

void Cast::submitCrowd(Renderer& r) {
    // patrons
    for (const Patron& p : patrons) {
        if (!p.present) continue;
        submitSeatedBody(r, p, false);
        if (p.prop == 1) {  // card fan in the left hand, faces toward the holder
            Vector3 hp = xfPoint(p.arm[1].hand, {-0.004f, -0.02f, -0.07f});
            Vector3 toHead = vnorm(Vector3Subtract(xfPoint(p.headW, {0, 0.1f, -0.05f}), hp));
            Vector3 up = vnorm(Vector3Subtract({0, 1, 0}, Vector3Scale(toHead, Vector3DotProduct({0, 1, 0}, toHead))));
            Matrix fan = basisMatrix(Vector3CrossProduct(up, toHead), up, toHead, hp);
            fan = mul(RX(-0.35f), fan);
            r.submit(&M.cardFan, &M.cardMat, fan, 0);
            ++submitCount;
        } else if (p.prop == 2) {  // tea glass in the right hand
            Matrix g = mul(basisMatrix({0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, Vector3Subtract(GRIP_CENTER, Vector3{-0.055f, 0, 0})),
                           p.arm[0].hand);
            r.submit(&M.tea[12], &M.teaMat, g, 0);
            r.submit(&M.glass, &M.glassMat, g, Transparent);
            submitCount += 2;
        }
        if (p.reading) {
            Vector3 a = mPos(p.arm[0].hand), b = mPos(p.arm[1].hand);
            Vector3 mid = Vector3Add(Vector3Lerp(a, b, 0.5f), {0, 0.05f, 0});
            Vector3 toHead = vnorm(Vector3Subtract(xfPoint(p.headW, {0, 0.1f, -0.05f}), mid));
            Vector3 xr = vnorm(Vector3Subtract(a, b));
            Vector3 up = vnorm(Vector3CrossProduct(toHead, xr));
            if (up.y < 0) up = Vector3Negate(up);
            Vector3 xx = Vector3CrossProduct(up, Vector3Negate(toHead));
            Matrix np = basisMatrix(xx, up, Vector3Negate(toHead), Vector3Add(mid, Vector3Scale(toHead, -0.02f)));
            r.submit(&M.newspaper, &M.paperMat, np, DoubleSided | CastShadow);
            ++submitCount;
        }
    }
    for (const BgTable& T : bgTables) {
        if (T.kind == 0 && T.diceVisible) {
            for (int i = 0; i < 2; ++i) {
                float u = clampf(T.diceT / 0.55f, 0.f, 1.f);
                Vector3 p = Vector3Lerp(T.diceFrom[i], T.dicePos[i], ui::easeOutCubic(u));
                p.y += std::sin(u * PI_F) * 0.06f * (1.f - u);
                if (T.diceT < 0.f) continue;
                Matrix m = mul(trs({0, 0, 0}, T.diceRot[i], {1, 1, 1}), chr::T(p));
                r.submit(&M.dice, &M.diceMat, m, 0);
                ++submitCount;
            }
        }
        if (T.kind == 2 && !T.patrons.empty() && patrons[T.patrons[0]].present)
            for (const Matrix& c : T.pileCards) {
                r.submit(&M.card, &M.cardMat, c, 0);
                ++submitCount;
            }
    }
    // the çaycı
    const Cayci& b = boy;
    const PersonMeshes& pm = *b.pm;
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.walkThigh[i], &pm.cloth, b.thighW[i], CastShadow);
        r.submit(&M.walkShin[i], &pm.cloth, b.shinW[i], CastShadow);
    }
    r.submit(&pm.torso, &pm.cloth, b.torsoW, CastShadow);
    r.submit(&pm.head, &pm.headMat, b.headW, 0);
    for (int a = 0; a < 2; ++a) {
        const Arm& A = b.arm[a];
        r.submit(&pm.upper[a], &pm.cloth, A.upper, CastShadow);
        r.submit(&pm.fore[a], &pm.cloth, A.fore, CastShadow);
        r.submit(&M.hand[a][(int)A.pose], &pm.skin, A.hand, CastShadow);
    }
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.eye[0], &M.eyeMat, b.eyeW[i], 0);
        r.submit(&M.lidUpper, &pm.skin, b.lidW[i], 0);
        r.submit(&pm.brow[i], &pm.hairMat, b.browW[i], 0);
    }
    if (b.jaw > 0.03f) {
        r.submit(&M.mouthCavity, &M.mouthMat, b.mouthW, 0);
        ++submitCount;
    }
    r.submit(&M.lowerLip[3], &M.lipMat, b.lipW, 0);
    r.submit(&pm.stache, &pm.hairMat, b.headW, 0);
    r.submit(&M.trayHanger, &M.trayMat, b.trayW, CastShadow);
    r.submit(&M.trayTea, &M.porcelain, b.trayW, 0);
    r.submit(&M.trayGlasses, &M.glassMat, b.trayW, Transparent);
    submitCount += 25;
}

void Characters::submit(Renderer& r) {
    Impl& m = *impl_;
    if (!m.ready) return;
    m.submitCount = 0;
    // our table's chairs (the human's too, behind the camera)
    for (int s = 0; s < 4; ++s) {
        Matrix cm = trsYaw(w3d::seatPos(s), w3d::seatYawDeg(s), 1.f);
        r.submit(&m.M.chair, &m.M.wood, cm, CastShadow);
        r.submit(&m.M.chairSeat, &m.M.straw, cm, CastShadow);
        m.submitCount += 2;
    }
    // tea glasses, saucers, spoons
    for (int s = 0; s < 4; ++s) {
        const TeaGlass& g = m.glass[s];
        r.submit(&m.M.saucer, &m.M.porcelain, mul(RY(g.spoonYaw), T(g.saucer)), CastShadow);
        if (g.level > 0.02f) {
            int li = std::clamp((int)std::lround(g.level * Meshes::TEA_LEVELS) - 1, 0, Meshes::TEA_LEVELS - 1);
            r.submit(&m.M.tea[li], g.oralet ? &m.M.oraletMat : &m.M.teaMat, g.world, 0);
            ++m.submitCount;
        }
        r.submit(&m.M.glass, &m.M.glassMat, g.world, Transparent);
        m.submitCount += 2;
    }
    for (int s = 1; s <= 3; ++s) m.submitOpponent(r, m.opp[s]);
    m.submitCrowd(r);
    m.submitLife(r);
}

} // namespace r3d
