// Room module: dynamic canvases — the TV football match (simulation + broadcast-style drawing) and the chalk
// scoreboard on the back wall (room owner).
#include "r3d/RoomInternal.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r3d {
namespace rm {

namespace {
constexpr int TEAM_N = 11;
const char* kTeam[2] = {"BGZ", "HLÇ"};
const Color kShirt[2] = {{206, 36, 40, 255}, {250, 214, 40, 255}};
const Color kShirt2[2] = {{245, 245, 245, 255}, {24, 40, 110, 255}};
const Color kKeeper[2] = {{40, 170, 90, 255}, {30, 30, 34, 255}};

Vector2 add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 scl(Vector2 a, float k) { return {a.x * k, a.y * k}; }
float len(Vector2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
Vector2 nrm(Vector2 a) {
    float l = len(a);
    return l > 1e-5f ? scl(a, 1.f / l) : Vector2{0, 0};
}
}  // namespace

// ============================================================================ TV football simulation
// Pitch coordinates: x along the length (0..1), y across (0 = far touchline, 1 = near touchline).
void TvSim::reset(uint64_t seed) {
    rng.reseed(seed ^ 0x7F00Du);
    const float hx[TEAM_N] = {0.04f, 0.2f, 0.2f, 0.2f, 0.2f, 0.38f, 0.38f, 0.38f, 0.38f, 0.52f, 0.52f};
    const float hy[TEAM_N] = {0.5f, 0.14f, 0.38f, 0.62f, 0.86f, 0.14f, 0.38f, 0.62f, 0.86f, 0.4f, 0.6f};
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < TEAM_N; ++i) {
            Player& p = pl[t * TEAM_N + i];
            p.team = t;
            p.keeper = i == 0;
            p.home = {attackDir[t] > 0 ? hx[i] : 1.f - hx[i], hy[i]};
            p.pos = p.home;
            p.vel = {0, 0};
        }
    score[0] = rng.range(0, 2);
    score[1] = rng.range(0, 2);
    minute = rng.uniform(18.f, 64.f);
    kickoff(rng.range(0, 1));
    pauseT = 0.f;
}

void TvSim::kickoff(int team) {
    for (Player& p : pl) {
        Vector2 h = p.home;
        // everyone in their own half
        if (attackDir[p.team] > 0) h.x = std::min(h.x, 0.47f);
        else h.x = std::max(h.x, 0.53f);
        p.pos = h;
        p.vel = {0, 0};
    }
    ball = {0.5f, 0.5f};
    ballVel = {0, 0};
    ballZ = ballVz = 0.f;
    owner = team * TEAM_N + 9;
    pl[owner].pos = {0.5f - attackDir[team] * 0.012f, 0.5f};
    lastTouchTeam = team;
    kickT = 1.2f;
    pauseT = 1.0f;
}

void TvSim::step(float dt) {
    flicker = 0.93f + 0.07f * noise1(minute * 40.f, 3u);
    roll += dt * 0.23f;
    if (roll > 1.4f) roll = -0.3f;
    if (endT > 0.f) {
        endT -= dt;
        if (endT <= 0.f) {
            score[0] = score[1] = 0;
            minute = 0.f;
            kickoff(rng.range(0, 1));
        }
        brightness = 0.35f;
        return;
    }
    minute += dt * 0.5f;
    if (minute >= 90.f + 3.f) {
        endT = 6.f;
        return;
    }
    if (goalFlash > 0.f) {
        goalFlash -= dt;
        // scorers celebrate toward the corner flag, the others trudge back
        for (Player& p : pl) {
            Vector2 tgt = p.team == goalTeam ? Vector2{attackDir[p.team] > 0 ? 0.93f : 0.07f, 0.9f} : p.home;
            Vector2 d = sub(tgt, p.pos);
            p.pos = add(p.pos, scl(nrm(d), std::min(len(d), 0.05f * dt)));
        }
        camX += (std::clamp(ball.x, 0.27f, 0.73f) - camX) * std::min(1.f, dt * 1.2f);
        brightness = 0.55f + 0.25f * (std::fmod(goalFlash * 3.f, 1.f) > 0.4f ? 1.f : 0.f);
        if (goalFlash <= 0.f) kickoff(1 - goalTeam);
        return;
    }
    if (pauseT > 0.f) {
        pauseT -= dt;
        brightness = 0.5f;
        return;
    }
    const int ownTeam = owner >= 0 ? pl[owner].team : lastTouchTeam;
    // nearest two players of each team to the ball chase / cover
    int chase[2] = {-1, -1}, cover[2] = {-1, -1};
    for (int t = 0; t < 2; ++t) {
        float b1 = 9, b2 = 9;
        for (int i = 1; i < TEAM_N; ++i) {
            int k = t * TEAM_N + i;
            float d = len(sub(pl[k].pos, ball));
            if (d < b1) {
                b2 = b1;
                cover[t] = chase[t];
                b1 = d;
                chase[t] = k;
            } else if (d < b2) {
                b2 = d;
                cover[t] = k;
            }
        }
    }
    for (int k = 0; k < 22; ++k) {
        Player& p = pl[k];
        const int dir = attackDir[p.team];
        Vector2 tgt;
        float speed = 0.05f;
        if (p.keeper) {
            float gx = dir > 0 ? 0.025f : 0.975f;
            float out = std::clamp(0.06f - std::fabs(ball.x - gx) * 0.05f, 0.f, 0.03f);
            tgt = {gx + dir * out, 0.5f + std::clamp(ball.y - 0.5f, -0.09f, 0.09f)};
            speed = 0.06f;
        } else if (k == owner) {
            Vector2 goal{dir > 0 ? 1.f : 0.f, 0.5f};
            Vector2 d = nrm(sub(goal, p.pos));
            tgt = add(p.pos, add(scl(d, 0.1f), Vector2{0, (0.5f - p.pos.y) * 0.05f + std::sin(minute * 3.f + k) * 0.03f}));
            speed = 0.058f;
        } else if (p.team != ownTeam && (k == chase[p.team] || (owner < 0 && k == chase[p.team]))) {
            tgt = ball;
            speed = 0.075f;
        } else if (owner < 0 && k == chase[p.team]) {
            tgt = ball;
            speed = 0.07f;
        } else if (p.team != ownTeam && k == cover[p.team]) {
            Vector2 own{dir > 0 ? 0.f : 1.f, 0.5f};
            tgt = add(ball, scl(sub(own, ball), 0.3f));
            speed = 0.06f;
        } else {
            float shift = (ball.x - 0.5f) * 0.55f + (p.team == ownTeam ? dir * 0.1f : -dir * 0.03f);
            tgt = {p.home.x + shift, p.home.y + (ball.y - 0.5f) * 0.28f};
            tgt.x += std::sin(minute * 0.7f + k * 1.3f) * 0.02f;
            speed = 0.045f;
        }
        tgt.x = std::clamp(tgt.x, 0.01f, 0.99f);
        tgt.y = std::clamp(tgt.y, 0.02f, 0.98f);
        Vector2 want = scl(nrm(sub(tgt, p.pos)), std::min(speed, len(sub(tgt, p.pos)) * 3.f));
        p.vel = add(p.vel, scl(sub(want, p.vel), std::min(1.f, dt * 5.f)));
        p.pos = add(p.pos, scl(p.vel, dt));
    }
    // ball
    if (owner >= 0) {
        Player& o = pl[owner];
        const int dir = attackDir[o.team];
        ball = add(o.pos, Vector2{dir * 0.011f, 0.004f});
        ballZ = 0.f;
        kickT -= dt;
        // tackles
        for (int k = 0; k < 22; ++k)
            if (pl[k].team != o.team && !pl[k].keeper && len(sub(pl[k].pos, ball)) < 0.014f && rng.chance(dt * 1.6f)) {
                owner = k;
                lastTouchTeam = pl[k].team;
                kickT = rng.uniform(0.5f, 1.4f);
                return;
            }
        if (kickT <= 0.f) {
            float gx = dir > 0 ? 1.f : 0.f;
            float distGoal = std::fabs(gx - o.pos.x);
            if (distGoal < 0.24f && rng.chance(0.6f)) {  // shot
                Vector2 aim{gx, 0.5f + rng.uniform(-0.075f, 0.075f)};
                ballVel = scl(nrm(sub(aim, ball)), rng.uniform(0.55f, 0.75f));
                ballVz = rng.uniform(0.02f, 0.12f);
            } else {  // pass to a teammate, preferably ahead
                int best = -1;
                float bestS = -1e9f;
                for (int i = 1; i < TEAM_N; ++i) {
                    int k = o.team * TEAM_N + i;
                    if (k == owner) continue;
                    Vector2 d = sub(pl[k].pos, o.pos);
                    float l = len(d);
                    if (l < 0.06f || l > 0.42f) continue;
                    float press = 9;
                    for (int j = 0; j < TEAM_N; ++j) press = std::min(press, len(sub(pl[(1 - o.team) * TEAM_N + j].pos, pl[k].pos)));
                    float s = d.x * dir * 1.5f + press * 2.f - l * 0.4f + rng.uniform(0.f, 0.35f);
                    if (s > bestS) {
                        bestS = s;
                        best = k;
                    }
                }
                if (best < 0) best = o.team * TEAM_N + 1 + rng.range(0, TEAM_N - 2);
                Vector2 lead = add(pl[best].pos, scl(pl[best].vel, 0.4f));
                float l = len(sub(lead, ball));
                ballVel = scl(nrm(sub(lead, ball)), 0.22f + l * 1.2f);
                ballVz = l > 0.25f ? rng.uniform(0.08f, 0.16f) : 0.f;
            }
            lastTouchTeam = o.team;
            owner = -1;
            kickT = 0.25f;  // short no-pickup window for the kicker's team
        }
    } else {
        ball = add(ball, scl(ballVel, dt));
        ballVz -= 0.45f * dt;
        ballZ += ballVz * dt;
        if (ballZ < 0.f) {
            ballZ = 0.f;
            ballVz = -ballVz * 0.35f;
            if (std::fabs(ballVz) < 0.01f) ballVz = 0.f;
        }
        ballVel = scl(ballVel, 1.f - std::min(0.9f, dt * (ballZ > 0.002f ? 0.3f : 1.1f)));
        kickT -= dt;
        // goal line
        if (ball.x <= 0.f || ball.x >= 1.f) {
            int side = ball.x >= 1.f ? 1 : -1;
            int scorer = attackDir[0] == side ? 0 : 1;
            int keeper = (1 - scorer) * TEAM_N;
            bool onTarget = ball.y > 0.446f && ball.y < 0.554f && ballZ < 0.03f;
            bool saved = onTarget && len(sub(pl[keeper].pos, Vector2{ball.x, ball.y})) < 0.05f && rng.chance(0.4f);
            if (onTarget && !saved) {
                score[scorer] = std::min(9, score[scorer] + 1);
                goalTeam = scorer;
                goalFlash = 4.f;
                goalEvent = true;
                ball.x = std::clamp(ball.x, 0.f, 1.f);
                ballVel = {0, 0};
                owner = -1;
                return;
            }
            // save / goal kick: the keeper takes it
            owner = keeper;
            lastTouchTeam = pl[keeper].team;
            pl[keeper].pos = {side > 0 ? 0.97f : 0.03f, 0.5f};
            ball = pl[keeper].pos;
            ballVel = {0, 0};
            kickT = rng.uniform(1.0f, 1.8f);
            pauseT = saved ? 0.4f : 1.2f;
            return;
        }
        // throw-in
        if (ball.y < 0.f || ball.y > 1.f) {
            ball.y = std::clamp(ball.y, 0.005f, 0.995f);
            int team = 1 - lastTouchTeam;
            int best = team * TEAM_N + 1;
            float bd = 9;
            for (int i = 1; i < TEAM_N; ++i) {
                float d = len(sub(pl[team * TEAM_N + i].pos, ball));
                if (d < bd) {
                    bd = d;
                    best = team * TEAM_N + i;
                }
            }
            pl[best].pos = ball;
            owner = best;
            lastTouchTeam = team;
            ballVel = {0, 0};
            kickT = 0.4f;
            pauseT = 1.1f;
            return;
        }
        // pick-up / interception
        if (ballZ < 0.02f) {
            float bd = 0.016f;
            int best = -1;
            for (int k = 0; k < 22; ++k) {
                if (kickT > 0.f && pl[k].team == lastTouchTeam) continue;
                float d = len(sub(pl[k].pos, ball));
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
            if (best >= 0 && (len(ballVel) < 0.5f || rng.chance(0.5f))) {
                owner = best;
                lastTouchTeam = pl[best].team;
                kickT = rng.uniform(0.5f, 1.6f);
            }
        }
    }
    camX += (std::clamp(ball.x, 0.27f, 0.73f) - camX) * std::min(1.f, dt * 1.5f);
    brightness = 0.46f + 0.04f * flicker;
}

// ============================================================================ TV drawing (320 x 240)
namespace {
struct TvView {
    float camX;
    // pitch (x, y) -> screen: y=0 far touchline (top), y=1 near touchline (bottom)
    Vector2 P(float x, float y) const {
        float s = 0.62f + 0.38f * y;
        return {160.f + (x - camX) * 560.f * s, 70.f + 162.f * y};
    }
    float S(float y) const { return 0.62f + 0.38f * y; }
};
}  // namespace

void drawTvCanvas(RenderTexture2D& rt, const TvSim& tv, float time) {
    beginRoomCanvas(rt);
    rlDisableBackfaceCulling();
    const float W = 320, H = 240;
    ClearBackground(Color{10, 14, 12, 255});
    TvView v{tv.camX};
    okey::Rng rng((uint64_t)(time * 24.f) + 7);
    if (tv.endT > 0.f) {
        rectGradV({0, 0, W, H}, Color{16, 30, 70, 255}, Color{8, 12, 30, 255});
        ui::drawTextCentered(ui::FontId::Sign, "MAÇ SONU", {W / 2, 76}, 34, Color{250, 220, 80, 255}, 2.f);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s  %d - %d  %s", kTeam[0], tv.score[0], tv.score[1], kTeam[1]);
        ui::drawTextCentered(ui::FontId::UiBold, buf, {W / 2, 132}, 28, WHITE);
    } else {
        // stands: crowd speckles above the far touchline, lit by floodlights
        rectGradV({0, 0, W, 64}, Color{22, 22, 30, 255}, Color{40, 38, 44, 255});
        okey::Rng crowd(11);
        for (int i = 0; i < 700; ++i) {
            float x = crowd.uniform(0.f, W), y = crowd.uniform(4.f, 62.f);
            x = std::fmod(x - (tv.camX - 0.5f) * 180.f + W * 4.f, W);
            Color c = crowd.chance(0.5f) ? kShirt[crowd.range(0, 1)] : Color{(unsigned char)crowd.range(90, 200), (unsigned char)crowd.range(80, 180),
                                                                             (unsigned char)crowd.range(70, 160), 255};
            float bounce = tv.goalFlash > 0.f ? std::fabs(std::sin(time * 12.f + i)) * 2.f : 0.f;
            DrawRectangleV({x, y - bounce}, {2.f, 2.f}, scaleRgb(c, 0.55f + 0.2f * (y / 62.f)));
        }
        // advertising boards along the far touchline
        const char* ads[4] = {"SAKLI BAHÇE", "ÇAY 15 TL", "OKEY KULÜBÜ", "SİMİT SARAYI"};
        for (int k = -2; k < 8; ++k) {
            Vector2 a = v.P(k * 0.25f, -0.03f), b = v.P((k + 1) * 0.25f, -0.03f);
            if (b.x < 0 || a.x > W) continue;
            Color bg = (k & 1) ? Color{20, 60, 150, 255} : Color{200, 30, 36, 255};
            DrawRectangleRec({a.x, 60.f, b.x - a.x, 9.f}, bg);
            ui::drawTextCentered(ui::FontId::UiBold, ads[(k + 8) % 4], {(a.x + b.x) * 0.5f, 64.5f}, 8.f, WHITE);
        }
        // pitch with mowing stripes
        for (int s = 0; s < 18; ++s) {
            float x0 = s / 18.f, x1 = (s + 1) / 18.f;
            Color g = (s % 2) ? Color{52, 128, 56, 255} : Color{44, 114, 50, 255};
            quad2(v.P(x0, 0), v.P(x1, 0), v.P(x1, 1.08f), v.P(x0, 1.08f), g);
        }
        quad2(v.P(-0.2f, -0.02f), v.P(0.f, -0.02f), v.P(0.f, 1.08f), v.P(-0.2f, 1.08f), Color{40, 104, 46, 255});
        quad2(v.P(1.f, -0.02f), v.P(1.2f, -0.02f), v.P(1.2f, 1.08f), v.P(1.f, 1.08f), Color{40, 104, 46, 255});
        const Color line{230, 240, 230, 210};
        auto L = [&](float x0, float y0, float x1, float y1) { DrawLineEx(v.P(x0, y0), v.P(x1, y1), 1.2f, line); };
        L(0, 0, 1, 0);
        L(0, 1, 1, 1);
        L(0, 0, 0, 1);
        L(1, 0, 1, 1);
        L(0.5f, 0, 0.5f, 1);
        for (int i = 0; i < 32; ++i) {
            float a0 = i / 32.f * 2 * PI, a1 = (i + 1) / 32.f * 2 * PI;
            L(0.5f + std::cos(a0) * 0.087f, 0.5f + std::sin(a0) * 0.135f, 0.5f + std::cos(a1) * 0.087f, 0.5f + std::sin(a1) * 0.135f);
        }
        for (int side = 0; side < 2; ++side) {
            float gx = side ? 1.f : 0.f, d = side ? -1.f : 1.f;
            L(gx, 0.204f, gx + d * 0.157f, 0.204f);
            L(gx + d * 0.157f, 0.204f, gx + d * 0.157f, 0.796f);
            L(gx, 0.796f, gx + d * 0.157f, 0.796f);
            L(gx, 0.365f, gx + d * 0.052f, 0.365f);
            L(gx + d * 0.052f, 0.365f, gx + d * 0.052f, 0.635f);
            L(gx, 0.635f, gx + d * 0.052f, 0.635f);
            // goal frame
            Vector2 p0 = v.P(gx, 0.446f), p1 = v.P(gx, 0.554f);
            float h = 9.f * v.S(0.5f);
            DrawLineEx(p0, {p0.x, p0.y - h}, 1.5f, WHITE);
            DrawLineEx(p1, {p1.x, p1.y - h}, 1.5f, WHITE);
            DrawLineEx({p0.x, p0.y - h}, {p1.x, p1.y - h}, 1.5f, WHITE);
            quad2({p0.x, p0.y - h}, {p1.x, p1.y - h}, {p1.x - d * 6, p1.y - h + 3}, {p0.x - d * 6, p0.y - h + 3}, Color{220, 220, 220, 60});
        }
        // players, far to near
        int order[22];
        for (int i = 0; i < 22; ++i) order[i] = i;
        std::sort(order, order + 22, [&](int a, int b) { return tv.pl[a].pos.y < tv.pl[b].pos.y; });
        for (int oi = 0; oi < 22; ++oi) {
            const TvSim::Player& p = tv.pl[order[oi]];
            Vector2 q = v.P(p.pos.x, p.pos.y);
            if (q.x < -10 || q.x > W + 10) continue;
            float s = v.S(p.pos.y);
            float run = std::sin(time * 14.f + order[oi] * 1.7f) * std::min(1.f, len(p.vel) * 25.f);
            ellipse({q.x + 1.5f * s, q.y + 0.5f}, 3.2f * s, 1.1f * s, Color{0, 0, 0, 90}, 10);
            Color shirt = p.keeper ? kKeeper[p.team] : kShirt[p.team];
            DrawLineEx({q.x - 0.8f * s, q.y - 3.f * s}, {q.x - 0.8f * s + run * 1.2f * s, q.y}, 1.3f * s, Color{30, 30, 30, 255});
            DrawLineEx({q.x + 0.8f * s, q.y - 3.f * s}, {q.x + 0.8f * s - run * 1.2f * s, q.y}, 1.3f * s, Color{30, 30, 30, 255});
            DrawRectangleRec({q.x - 1.8f * s, q.y - 7.5f * s, 3.6f * s, 4.8f * s}, shirt);
            if (!p.keeper) DrawRectangleRec({q.x - 0.6f * s, q.y - 7.5f * s, 1.2f * s, 4.8f * s}, kShirt2[p.team]);
            ellipse({q.x, q.y - 9.f * s}, 1.4f * s, 1.5f * s, Color{200, 150, 110, 255}, 8);
        }
        // ball + shadow
        Vector2 bq = v.P(tv.ball.x, tv.ball.y);
        float bs = v.S(tv.ball.y);
        ellipse({bq.x + 1, bq.y + 0.5f}, 1.8f * bs, 0.8f * bs, Color{0, 0, 0, 100}, 8);
        ellipse({bq.x, bq.y - 1.4f * bs - tv.ballZ * 260.f}, 1.4f * bs, 1.4f * bs, WHITE, 10);
        // score bug + clock
        DrawRectangleRounded({6, 6, 128, 18}, 0.3f, 4, Color{12, 16, 40, 230});
        DrawRectangleRec({6, 6, 6, 18}, kShirt[0]);
        DrawRectangleRec({128, 6, 6, 18}, kShirt[1]);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s %d-%d %s", kTeam[0], tv.score[0], tv.score[1], kTeam[1]);
        ui::drawText(ui::FontId::UiBold, buf, {16, 7}, 15.f, WHITE);
        std::snprintf(buf, sizeof buf, "%02d:%02d", (int)tv.minute, (int)(std::fmod(tv.minute, 1.f) * 60.f));
        DrawRectangleRounded({136, 6, 44, 18}, 0.3f, 4, Color{240, 240, 240, 230});
        ui::drawText(ui::FontId::UiBold, buf, {140, 7}, 14.f, Color{20, 20, 30, 255});
        // channel logo
        ui::drawText(ui::FontId::Sign, "SPOR", {262, 6}, 17.f, Color{250, 250, 250, 200});
        DrawRectangleRounded({262, 25, 42, 11}, 0.4f, 4, Color{210, 30, 30, 230});
        ui::drawTextCentered(ui::FontId::UiBold, "CANLI", {283, 30.5f}, 9.f, WHITE);
        if (tv.derby) {  // (ozelgun) maç gecesi: the big derby
            DrawRectangleRounded({6, 26, 70, 15}, 0.35f, 4, Color{214, 160, 30, 235});
            ui::drawTextCentered(ui::FontId::UiBold, "DERBİ", {41, 33.5f}, 12.f, Color{30, 20, 10, 255});
        }
        if (tv.goalFlash > 0.f) {
            float t = tv.goalFlash;
            float a = std::fmod(t * 2.5f, 1.f) > 0.3f ? 1.f : 0.55f;
            DrawRectangleRec({0, 0, W, H}, Color{255, 240, 200, (unsigned char)(40 * a)});
            DrawRectangleRec({0, 150, W, 44}, alpha(kShirt[tv.goalTeam], 0.85f));
            ui::drawTextCentered(ui::FontId::Sign, "GOL!", {W / 2 + 3, 108 + 3}, 72, Color{0, 0, 0, (unsigned char)(160 * a)});
            ui::drawTextCentered(ui::FontId::Sign, "GOL!", {W / 2, 108}, 72, Color{255, 226, 60, (unsigned char)(255 * a)});
            char g[48];
            std::snprintf(g, sizeof g, "%s  %d - %d  %s", kTeam[0], tv.score[0], tv.score[1], kTeam[1]);
            ui::drawTextCentered(ui::FontId::UiBold, g, {W / 2, 172}, 22, kShirt2[tv.goalTeam]);
        }
    }
    // CRT: scanlines, rolling band, vignette, glass highlight, a bit of snow
    for (float y = 0; y < H; y += 2.f) DrawRectangleRec({0, y, W, 1.f}, Color{0, 0, 0, 52});
    float by = tv.roll * H;
    rectGradV({0, by, W, 26}, Color{255, 255, 255, 0}, Color{255, 255, 255, 20});
    for (int i = 0; i < 160; ++i)
        DrawRectangleV({rng.uniform(0.f, W), rng.uniform(0.f, H)}, {1.5f, 1.f}, Color{255, 255, 255, (unsigned char)rng.range(10, 40)});
    rectGradV({0, 0, W, 30}, Color{0, 0, 0, 150}, Color{0, 0, 0, 0});
    rectGradV({0, H - 30, W, 30}, Color{0, 0, 0, 0}, Color{0, 0, 0, 170});
    rectGradH({0, 0, 34, H}, Color{0, 0, 0, 150}, Color{0, 0, 0, 0});
    rectGradH({W - 34, 0, 34, H}, Color{0, 0, 0, 0}, Color{0, 0, 0, 150});
    ellipseGrad({W * 0.32f, H * 0.2f}, W * 0.3f, H * 0.12f, Color{255, 255, 255, 34}, Color{255, 255, 255, 0});
    DrawRectangleRec({0, 0, W, H}, Color{0, 0, 0, (unsigned char)((1.f - tv.flicker) * 400.f)});
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    endRoomCanvas(rt);
}

// ============================================================================ chalk scoreboard (1024 x 696)
void drawScoreboardCanvas(RenderTexture2D& rt, const std::string& title, const std::vector<std::string>& lines,
                          uint32_t seed) {
    okey::Rng rng(seed);
    const float W = (float)rt.texture.width, H = (float)rt.texture.height;
    const Color board{36, 48, 42, 255}, chalk{238, 236, 222, 255};
    beginRoomCanvas(rt);
    rlDisableBackfaceCulling();
    ClearBackground(board);
    rectGradV({0, 0, W, H}, Color{58, 70, 62, 90}, Color{14, 20, 16, 110});
    // ghosts of earlier games, rubbed out
    okey::Rng ghost(seed * 3 + 1);
    const char* old[5] = {"El 4 / 5", "Kazım ...... 212", "Topal Rıfat ...... 87", "101 !!", "Cemil 404"};
    for (int i = 0; i < 5; ++i)
        ui::drawText(ui::FontId::Chalk, old[i], {ghost.uniform(20.f, W * 0.5f), ghost.uniform(10.f, H - 90.f)}, ghost.uniform(60.f, 90.f),
                     alpha(chalk, 0.028f));
    chalkSmudges({0, 0, W, H}, chalk, rng, 12);
    const float margin = 56.f;
    // title
    std::string t = title.empty() ? std::string("HOŞ GELDİNİZ") : title;
    float ts = 104.f;
    while (ts > 40.f && ui::measureText(ui::FontId::Chalk, t, ts).x > W - 2 * margin) ts -= 4.f;
    Vector2 tm = ui::measureText(ui::FontId::Chalk, t, ts);
    chalkText(ui::FontId::Chalk, t, {W * 0.5f - tm.x * 0.5f, 22.f}, ts, chalk, rng);
    // wavy underline
    {
        float y = 22.f + tm.y + 4.f, x0 = W * 0.5f - tm.x * 0.55f, x1 = W * 0.5f + tm.x * 0.55f;
        Vector2 prev{x0, y};
        for (int i = 1; i <= 24; ++i) {
            float x = x0 + (x1 - x0) * i / 24.f;
            Vector2 p{x, y + std::sin(i * 0.9f) * 3.f};
            DrawLineEx(prev, p, 4.f, alpha(chalk, 0.6f));
            prev = p;
        }
    }
    std::vector<std::string> ls = lines;
    if (title.empty() && ls.empty()) ls = {"Okey masası hazır", "Çaylar bizden!"};
    const int n = std::min<int>((int)ls.size(), 4);
    const float top = 22.f + tm.y + 30.f, avail = H - top - 26.f;
    const float step = n > 0 ? std::min(avail / n, 132.f) : 0.f;
    float fs = std::min(step * 0.86f, 96.f);
    for (int i = 0; i < n; ++i) {
        const std::string& s = ls[i];
        // split "Name ....... 143" into name + number with our own dotted leader
        size_t dots = s.find("..");
        std::string left = s, right;
        if (dots != std::string::npos) {
            left = s.substr(0, dots);
            size_t r0 = s.find_first_not_of(". ", dots);
            right = r0 == std::string::npos ? "" : s.substr(r0);
            while (!left.empty() && left.back() == ' ') left.pop_back();
        }
        float size = fs;
        auto widthAt = [&](float sz) {
            return ui::measureText(ui::FontId::Chalk, left, sz).x + (right.empty() ? 0.f : ui::measureText(ui::FontId::Chalk, right, sz).x + 60.f);
        };
        while (size > 30.f && widthAt(size) > W - 2 * margin) size -= 3.f;
        float y = top + i * step + (step - size * 1.1f) * 0.5f;
        chalkText(ui::FontId::Chalk, left, {margin, y}, size, chalk, rng);
        if (!right.empty()) {
            Vector2 rm = ui::measureText(ui::FontId::Chalk, right, size);
            chalkText(ui::FontId::Chalk, right, {W - margin - rm.x, y}, size, chalk, rng);
            float x0 = margin + ui::measureText(ui::FontId::Chalk, left, size).x + 18.f, x1 = W - margin - rm.x - 18.f;
            for (float x = x0; x < x1; x += size * 0.24f)
                ellipse({x + rng.uniform(-1.f, 1.f), y + size * 0.78f + rng.uniform(-1.5f, 1.5f)}, size * 0.035f, size * 0.03f,
                        alpha(chalk, 0.62f), 8);
        }
    }
    chalkSpeckle({0, 0, W, H}, board, rng, 9000);
    // a little chalk dust along the bottom edge
    for (int i = 0; i < 400; ++i)
        DrawRectangleV({rng.uniform(0.f, W), H - rng.uniform(0.f, 16.f) * rng.uniform(0.f, 1.f)}, {2.f, 2.f}, alpha(chalk, rng.uniform(0.05f, 0.25f)));
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    endRoomCanvas(rt);
}

}  // namespace rm
}  // namespace r3d
