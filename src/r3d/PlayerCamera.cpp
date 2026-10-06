// The human's seated first-person camera: right-drag look-around, wheel zoom, R / double right-click
// recentre, a slow breathing sway, involuntary glances, and a cinematic drift through the room in title mode.
#include "r3d/Table3D.h"
#include "r3d/World.h"
#include "ui/Common.h"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace r3d {

namespace {

// Default seated view, tuned from World.h's EYE (0, 1.20, 0.80) / -27 deg / 66 deg: sitting up a little
// straighter (eye 5 cm further back, 1 cm higher) and looking 2.5 deg higher keeps the whole istaka large at
// the bottom, the table centre with every discard pile in the middle, and Kel Mahmut's face fully (Hacı
// Rıza and Emekli Nuri at the frame edges) in one frame.
constexpr Vector3 DEF_EYE{0.f, 1.21f, 0.85f};
constexpr float DEF_PITCH = -24.5f;
constexpr float DEF_FOV = w3d::FOVY_DEG;
constexpr float YAW_LIMIT = 110.f;
constexpr float PITCH_MIN = -75.f, PITCH_MAX = 35.f;
constexpr float FOV_MIN = 35.f, FOV_MAX = 70.f;
// The head turns about the neck: the eyes sit in front of and above the pivot, so looking around moves them
// a little (natural parallax). At the default view the eye is exactly at DEF_EYE.
constexpr Vector3 EYE_FROM_NECK{0.f, 0.075f, -0.085f};

Vector3 lookDir(float yawDeg, float pitchDeg) {
    const float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

Vector3 rotateHead(Vector3 v, float yawDeg, float pitchDeg) {
    // pitch about the head's X axis, then yaw about Y (positive yaw turns right, toward +X)
    const Matrix m = MatrixMultiply(MatrixRotateX(pitchDeg * DEG2RAD), MatrixRotateY(-yawDeg * DEG2RAD));
    return Vector3Transform(v, m);
}

// The eye for a head turned by (yaw, pitch) whose resting eye is `eye` at (baseYaw, defPitch).
Vector3 eyeFor(Vector3 eye, float baseYaw, float defPitch, float yawDeg, float pitchDeg) {
    const Vector3 neck = Vector3Subtract(eye, rotateHead(EYE_FROM_NECK, baseYaw, defPitch));
    return Vector3Add(neck, rotateHead(EYE_FROM_NECK, yawDeg, pitchDeg));
}

// ---- title mode: a slow loop through the room (Catmull-Rom through positions, look targets and framings).
// The path stays above the seated heads (>= 0.9 m from every head, clear of the lamps) and keeps our table,
// lit under its pendant, in most of the frames. Each subject is framed on a third (`frame`: where it sits on
// screen, -1..1 with y up), clear of the title screen's sign (top centre) and menu column (centre), and
// inside the side vignettes.
struct Waypoint {
    Vector3 pos, look;
    Vector2 frame;
};
constexpr float kThird = 0.34f; // x = 800 +- 272 of 1600: between the menu column and the edge vignette
constexpr Waypoint kPath[] = {
    {{1.70f, 1.86f, 1.55f}, {0.00f, 0.86f, 0.00f}, {-kThird, -0.12f}},    // behind our chair, right: the table under the lamp
    {{2.05f, 2.02f, 0.25f}, {3.00f, 1.15f, -2.85f}, {-kThird, -0.06f}},   // along the right side: the tea counter, steam
    {{0.90f, 2.18f, -1.62f}, {-2.50f, 0.95f, -1.80f}, {-kThird, -0.14f}}, // across the back: tavla left, scoreboard right
    {{-1.78f, 2.24f, -1.58f}, {0.10f, 0.82f, 0.25f}, {kThird, -0.12f}},   // behind Kel Mahmut: our table from across
    {{-2.02f, 2.12f, 0.32f}, {-2.60f, 0.88f, 1.70f}, {-kThird, -0.28f}},  // over the card players by the left wall
    {{-1.30f, 1.90f, 1.50f}, {0.20f, 0.88f, -0.30f}, {-kThird, -0.12f}},  // round behind our seat again (between our
                                                                          // chair and the tavla table: its lamp behind)
};
constexpr int kPathN = (int)(sizeof(kPath) / sizeof(kPath[0]));
constexpr float kSegSeconds = 13.f;
constexpr float kTitleFov = 52.f;

Vector3 catmull(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float a, float b, float c, float d) {
        return 0.5f * ((2.f * b) + (-a + c) * t + (2.f * a - 5.f * b + 4.f * c - d) * t2 + (-a + 3.f * b - 3.f * c + d) * t3);
    };
    return {f(p0.x, p1.x, p2.x, p3.x), f(p0.y, p1.y, p2.y, p3.y), f(p0.z, p1.z, p2.z, p3.z)};
}

// The forward direction (no roll) that puts `toSubject` at screen point `frame` (-1..1, x right, y up) of a
// 16:9 camera with vertical field of view `fovyDeg`.
Vector3 framedLook(Vector3 toSubject, Vector2 frame, float fovyDeg) {
    const Vector3 d = Vector3Normalize(toSubject);
    const float tv = std::tan(fovyDeg * 0.5f * DEG2RAD), th = tv * (16.f / 9.f);
    // the subject's direction in camera space: a right, b up, c forward
    const float a0 = frame.x * th, b0 = frame.y * tv, inv = 1.f / std::sqrt(a0 * a0 + b0 * b0 + 1.f);
    const float a = a0 * inv, b = b0 * inv, c = inv;
    // pitch p: the subject's height  b cos p + c sin p = d.y
    const float rho = std::sqrt(b * b + c * c);
    const float pitch = std::atan2(c, b) - std::acos(std::clamp(d.y / rho, -1.f, 1.f));
    // yaw: the subject's heading minus its heading inside the pitched camera
    const float x1 = a, z1 = b * std::sin(pitch) - c * std::cos(pitch);
    const float yaw = std::atan2(d.x, -d.z) - std::atan2(x1, -z1);
    return lookDir(yaw * RAD2DEG, pitch * RAD2DEG);
}

} // namespace

void PlayerCamera::setSeat(Vector3 eye, float baseYawDeg, float pitchDeg) {
    eye_ = eye;
    baseYaw_ = baseYawDeg;
    defPitch_ = pitchDeg;
    reset();
}

void PlayerCamera::setOkeySeat() { setSeat(DEF_EYE, 0.f, DEF_PITCH); }

// The tavla table: the same seated eye as at our table (above the chair, a little in front), looking down at the board
// closer in front, so the resting pitch is steeper.
void PlayerCamera::setTavlaSeat() {
    const Vector3 eye = w3d::tavlaToWorld({0.f, DEF_EYE.y, w3d::TAVLA_SEAT_DIST - (w3d::SEAT_DIST - DEF_EYE.z)});
    setSeat(eye, -w3d::TAVLA_YAW_DEG, -36.f);
}

void PlayerCamera::reset() {
    yaw_ = targetYaw_ = 0.f;
    pitch_ = targetPitch_ = defPitch_;
    fov_ = targetFov_ = DEF_FOV;
    glanceT_ = 0.f;
    lastRightClick_ = -10.f;
    lastMouse_ = GetMousePosition();
}

void PlayerCamera::setTitleMode(bool on) {
    if (on == title_) return;
    title_ = on;
    if (!on) reset();
}

void PlayerCamera::glanceAt(Vector3 target, float seconds) {
    glance_ = target;
    glanceT_ = std::max(0.f, seconds);
}

void PlayerCamera::update(float dt, bool allowLook) {
    if (title_) {
        titleT_ += dt;
        time_ += dt;
        return;
    }
    // the breathing clock stops while the left button is held, so a tile being aimed never drifts
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) time_ += dt;

    const Vector2 mp = GetMousePosition();
    const bool rmbDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (allowLook && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (time_ - lastRightClick_ < 0.35f) {
            targetYaw_ = 0.f;
            targetPitch_ = defPitch_;
            targetFov_ = DEF_FOV;
            lastRightClick_ = -10.f;
        } else {
            lastRightClick_ = time_;
        }
        lastMouse_ = mp;
    }
    if (allowLook && rmbDown) {
        const float k = 0.12f * (fov_ / DEF_FOV);
        targetYaw_ = std::clamp(targetYaw_ + (mp.x - lastMouse_.x) * k, -YAW_LIMIT, YAW_LIMIT);
        targetPitch_ = std::clamp(targetPitch_ - (mp.y - lastMouse_.y) * k, PITCH_MIN, PITCH_MAX);
    }
    lastMouse_ = mp;
    if (allowLook) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.f) targetFov_ = std::clamp(targetFov_ - wheel * 3.5f, FOV_MIN, FOV_MAX);
    }
    if (recentreKey_ && IsKeyPressed(KEY_R)) {
        targetYaw_ = 0.f;
        targetPitch_ = defPitch_;
        targetFov_ = DEF_FOV;
    }

    // involuntary glance: pull the view part of the way toward the target, then let it return
    float ty = targetYaw_, tp = targetPitch_;
    if (glanceT_ > 0.f) {
        glanceT_ = std::max(0.f, glanceT_ - dt);
        if (!rmbDown) {
            const Vector3 d = Vector3Subtract(glance_, eye_);
            float gy = std::atan2(d.x, -d.z) * RAD2DEG - baseYaw_;
            while (gy > 180.f) gy -= 360.f;
            while (gy < -180.f) gy += 360.f;
            const float gp = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * RAD2DEG;
            const float w = 0.38f * std::min(1.f, glanceT_ / 0.5f);
            ty += (std::clamp(gy, -YAW_LIMIT, YAW_LIMIT) - ty) * w;
            tp += (std::clamp(gp, PITCH_MIN, PITCH_MAX) - tp) * w;
        }
    }
    const float rate = rmbDown ? 20.f : (glanceT_ > 0.f ? 3.5f : 9.f);
    yaw_ = ui::approach(yaw_, ty, rate, dt);
    pitch_ = ui::approach(pitch_, tp, rate, dt);
    fov_ = ui::approach(fov_, targetFov_, 10.f, dt);
}

Camera3D PlayerCamera::camera() const {
    Camera3D c{};
    c.up = {0, 1, 0};
    c.projection = CAMERA_PERSPECTIVE;
    if (title_) {
        const float total = kSegSeconds * (float)kPathN;
        float t = std::fmod(titleT_, total) / kSegSeconds;
        const int i = (int)t;
        float u = t - (float)i;
        u = u * u * (3.f - 2.f * u) * 0.35f + u * 0.65f; // linger a touch at each view
        auto P = [&](int k) { return kPath[((k % kPathN) + kPathN) % kPathN]; };
        auto F = [&](int k) { return Vector3{P(k).frame.x, P(k).frame.y, 0.f}; };
        c.position = catmull(P(i - 1).pos, P(i).pos, P(i + 1).pos, P(i + 2).pos, u);
        const Vector3 look = catmull(P(i - 1).look, P(i).look, P(i + 1).look, P(i + 2).look, u);
        const Vector3 fr = catmull(F(i - 1), F(i), F(i + 1), F(i + 2), u);
        // a slow handheld float
        c.position.y += 0.012f * std::sin(titleT_ * 0.41f);
        c.position.x += 0.008f * std::sin(titleT_ * 0.27f + 1.3f);
        c.fovy = kTitleFov;
        c.target = Vector3Add(c.position, framedLook(Vector3Subtract(look, c.position), {fr.x, fr.y}, kTitleFov));
        return c;
    }
    // breathing: one slow breath every ~4.5 s, a few millimetres and a fraction of a degree
    const float b = std::sin(time_ * 2.f * PI / 4.5f);
    const float b2 = std::sin(time_ * 2.f * PI / 7.3f + 0.8f);
    const float yaw = baseYaw_ + yaw_ + 0.05f * b2;
    const float pitch = pitch_ + 0.08f * b;
    c.position = eyeFor(eye_, baseYaw_, defPitch_, yaw, pitch);
    c.position.y += 0.0018f * b;
    c.position.z += 0.0010f * b;
    c.target = Vector3Add(c.position, lookDir(yaw, pitch));
    c.fovy = fov_;
    return c;
}

float PlayerCamera::yawDeg() const { return baseYaw_ + yaw_; }
float PlayerCamera::pitchDeg() const { return pitch_; }

} // namespace r3d
