// Characters module: the people's geometry — heads with faces, torsos with clothes, seated legs, arms,
// hands in several poses and the animated face parts (eyes, lids, brows, lips). Built once at init from
// smooth-union SDF primitives (CharactersSdf.cpp) and lathes; per-frame work is transforms only.
#include "r3d/CharactersInternal.h"

#include <algorithm>

namespace r3d {
namespace chr {

namespace {

Color rgb(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}
Color scaleC(Color c, float k) {
    return Color{(unsigned char)clampf(c.r * k, 0, 255), (unsigned char)clampf(c.g * k, 0, 255),
                 (unsigned char)clampf(c.b * k, 0, 255), c.a};
}
Color mixC(Color a, Color b, float t) {
    t = clampf(t, 0.f, 1.f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Lathe with per-vertex colour: profile (radius, y) bottom -> top around +Y.
void latheColored(MeshBuilder& b, const std::vector<Vector2>& prof, int seg,
                  const std::function<Color(float u, float y)>& colFn) {
    int np = (int)prof.size();
    if (np < 2) return;
    std::vector<Vector2> nrm(np);
    for (int i = 0; i < np; ++i) {
        Vector2 a = prof[std::max(i - 1, 0)], c = prof[std::min(i + 1, np - 1)];
        Vector2 t{c.x - a.x, c.y - a.y};
        Vector2 n{t.y, -t.x};
        float l = std::sqrt(n.x * n.x + n.y * n.y);
        nrm[i] = l > 1e-6f ? Vector2{n.x / l, n.y / l} : Vector2{1, 0};
    }
    int base = b.vertexCount();
    for (int i = 0; i < np; ++i)
        for (int j = 0; j <= seg; ++j) {
            float a = 2.f * PI_F * j / seg;
            float s = std::sin(a), c = std::cos(a);
            b.vertex({prof[i].x * s, prof[i].y, prof[i].x * c}, {nrm[i].x * s, nrm[i].y, nrm[i].x * c},
                     {j / (float)seg, prof[i].y}, colFn(j / (float)seg, prof[i].y));
        }
    for (int i = 0; i + 1 < np; ++i)
        for (int j = 0; j < seg; ++j) {
            int a = base + i * (seg + 1) + j, bb = a + 1, cc = a + (seg + 1) + 1, d = a + (seg + 1);
            b.quad(a, bb, cc, d);
        }
}

// Rounded limb profile along +Y from 0 to len (radius r0 -> r1) with hemispherical ends.
std::vector<Vector2> limbProfile(float len, float r0, float r1, int capSeg = 5) {
    std::vector<Vector2> p;
    for (int i = 0; i <= capSeg; ++i) {
        float t = -PI_F / 2 + (PI_F / 2) * i / capSeg;
        p.push_back({r0 * std::cos(t), r0 * std::sin(t)});
    }
    const int mid = 6;
    for (int i = 1; i < mid; ++i) {
        float t = i / (float)mid;
        // slight muscle bulge in the upper third
        float r = lerpf(r0, r1, t) * (1.f + 0.06f * std::sin(PI_F * std::min(1.f, t * 1.6f)));
        p.push_back({r, len * t});
    }
    for (int i = 0; i <= capSeg; ++i) {
        float t = (PI_F / 2) * i / capSeg;
        p.push_back({r1 * std::cos(t), len + r1 * std::sin(t)});
    }
    return p;
}

// ---------------------------------------------------------------------------- hands
struct FingerDef {
    float curl[3];
    float spread;
};
struct HandDef {
    FingerDef f[4];
    Vector3 thumb[3];   // directions of the three thumb segments
    float palmCup = 0.f;
};

HandDef handDef(HandPose p) {
    HandDef d{};
    auto fingers = [&](float c0, float c1, float c2, float extraPinky) {
        for (int i = 0; i < 4; ++i) {
            float k = 1.f + 0.12f * i;
            d.f[i].curl[0] = c0 * k + (i == 3 ? extraPinky : 0.f);
            d.f[i].curl[1] = c1 * k + (i == 3 ? extraPinky : 0.f);
            d.f[i].curl[2] = c2 * k;
        }
    };
    const float spreads[4] = {-0.07f, -0.015f, 0.035f, 0.09f};
    for (int i = 0; i < 4; ++i) d.f[i].spread = spreads[i];
    switch (p) {
    case HandPose::Rest:
        fingers(0.24f, 0.34f, 0.22f, 0.06f);
        d.thumb[0] = {-0.44f, -0.40f, -0.80f};
        d.thumb[1] = {-0.26f, -0.38f, -0.89f};
        d.thumb[2] = {-0.12f, -0.40f, -0.91f};
        break;
    case HandPose::Grip:
        fingers(0.55f, 0.95f, 0.75f, 0.1f);
        d.thumb[0] = {-0.45f, -0.55f, -0.70f};
        d.thumb[1] = {-0.05f, -0.75f, -0.66f};
        d.thumb[2] = {0.25f, -0.75f, -0.60f};
        d.palmCup = 0.5f;
        break;
    case HandPose::Fist:
        fingers(1.35f, 1.55f, 1.05f, 0.f);
        d.thumb[0] = {-0.40f, -0.62f, -0.68f};
        d.thumb[1] = {0.20f, -0.70f, -0.68f};
        d.thumb[2] = {0.60f, -0.45f, -0.66f};
        break;
    case HandPose::Open:
        fingers(0.04f, 0.05f, 0.02f, 0.f);
        for (int i = 0; i < 4; ++i) d.f[i].spread *= 2.2f;
        d.thumb[0] = {-0.82f, -0.12f, -0.56f};
        d.thumb[1] = {-0.70f, -0.08f, -0.71f};
        d.thumb[2] = {-0.60f, -0.02f, -0.80f};
        break;
    case HandPose::Pinch:
        fingers(0.95f, 1.15f, 0.8f, 0.f);
        d.f[0].curl[0] = 0.42f;
        d.f[0].curl[1] = 0.62f;
        d.f[0].curl[2] = 0.35f;
        d.thumb[0] = {-0.50f, -0.40f, -0.77f};
        d.thumb[1] = {-0.10f, -0.42f, -0.90f};
        d.thumb[2] = {0.18f, -0.30f, -0.94f};
        break;
    case HandPose::Cig:
        fingers(1.10f, 1.30f, 0.95f, 0.f);
        d.f[0].curl[0] = 0.12f;
        d.f[0].curl[1] = 0.22f;
        d.f[0].curl[2] = 0.12f;
        d.f[1].curl[0] = 0.16f;
        d.f[1].curl[1] = 0.26f;
        d.f[1].curl[2] = 0.14f;
        d.f[0].spread = -0.12f;
        d.f[1].spread = 0.03f;
        d.thumb[0] = {-0.55f, -0.40f, -0.73f};
        d.thumb[1] = {-0.20f, -0.50f, -0.84f};
        d.thumb[2] = {0.05f, -0.48f, -0.88f};
        break;
    case HandPose::Point:
        fingers(1.35f, 1.55f, 1.05f, 0.f);
        d.f[0].curl[0] = 0.05f;
        d.f[0].curl[1] = 0.06f;
        d.f[0].curl[2] = 0.03f;
        d.thumb[0] = {-0.40f, -0.62f, -0.68f};
        d.thumb[1] = {0.15f, -0.72f, -0.68f};
        d.thumb[2] = {0.50f, -0.50f, -0.70f};
        break;
    case HandPose::Hold:
    default:
        fingers(0.62f, 0.78f, 0.55f, 0.05f);
        d.thumb[0] = {-0.50f, -0.50f, -0.71f};
        d.thumb[1] = {-0.15f, -0.62f, -0.77f};
        d.thumb[2] = {0.10f, -0.60f, -0.79f};
        d.palmCup = 0.3f;
        break;
    }
    return d;
}

// Finger chains in hand space (scale 1): finger 0..3 = index..little, 4 = thumb. Returns the three segment
// end points (q[0..2]) and radii (r[0] at the knuckle .. r[3] at the tip), from the knuckle at q0.
void fingerChain(const HandDef& d, int finger, Vector3& q0, Vector3 q[3], float r[4]) {
    if (finger < 4) {
        const float kx[4] = {-0.0262f, -0.0088f, 0.0088f, 0.0255f};
        const float kz[4] = {-0.091f, -0.095f, -0.092f, -0.085f};
        const float len[4][3] = {{0.036f, 0.023f, 0.020f}, {0.040f, 0.026f, 0.021f}, {0.037f, 0.024f, 0.020f},
                                 {0.029f, 0.019f, 0.017f}};
        const float rad[4] = {0.0083f, 0.0085f, 0.0080f, 0.0070f};
        q0 = {kx[finger], -0.0005f, kz[finger]};
        Vector3 p = q0;
        float C = 0.f;
        r[0] = rad[finger];
        for (int seg = 0; seg < 3; ++seg) {
            C += d.f[finger].curl[seg];
            const float sp = d.f[finger].spread;
            const Vector3 dir{std::sin(sp) * std::cos(C), -std::sin(C), -std::cos(sp) * std::cos(C)};
            p = Vector3Add(p, Vector3Scale(dir, len[finger][seg]));
            q[seg] = p;
            r[seg + 1] = r[seg] * (seg == 2 ? 0.86f : 0.92f);
        }
        return;
    }
    q0 = {-0.028f, -0.009f, -0.026f};
    const float tl[3] = {0.030f, 0.027f, 0.023f};
    Vector3 p = q0;
    r[0] = 0.0115f;
    for (int seg = 0; seg < 3; ++seg) {
        p = Vector3Add(p, Vector3Scale(vnorm(d.thumb[seg]), tl[seg]));
        q[seg] = p;
        r[seg + 1] = seg == 0 ? 0.0098f : (seg == 1 ? 0.0090f : 0.0078f);
    }
}

RawMesh buildHandMesh(HandPose pose) {
    const HandDef d = handDef(pose);
    Sdf s;
    const Color c = WHITE;
    // palm, back of the hand, thenar/hypothenar pads
    s.box({0.f, 0.f, -0.050f}, {0.038f, 0.0108f, 0.043f}, 0.0105f, c);
    s.ellipsoid({0.f, 0.005f, -0.052f}, {0.036f, 0.0095f, 0.041f}, c, 0.010f);
    s.ellipsoid({-0.020f, -0.0075f, -0.030f}, {0.019f, 0.0125f, 0.026f}, c, 0.012f);
    s.ellipsoid({0.023f, -0.0065f, -0.036f}, {0.014f, 0.0105f, 0.030f}, c, 0.010f);
    if (d.palmCup > 0.f) s.cutEllipsoid({0.f, -0.030f, -0.060f}, {0.030f, 0.014f, 0.030f}, c, 0.010f);
    // the wrist: only a short, slim stub behind the wrist point, so however the hand bends it stays inside the
    // sleeve's cuff (radius >= 0.035 for every sleeved person; the stub reaches at most 0.024 from the axis)
    s.cone({0.f, 0.001f, 0.006f}, {0.f, 0.f, -0.010f}, 0.0205f, 0.0228f, c, 0.012f);
    // fingers and thumb
    for (int f = 0; f < 5; ++f) {
        Vector3 q0, q[3];
        float r[4];
        fingerChain(d, f, q0, q, r);
        Vector3 p = q0;
        for (int seg = 0; seg < 3; ++seg) {
            s.cone(p, q[seg], r[seg], r[seg + 1], c, seg == 0 ? (f == 4 ? 0.012f : 0.009f) : 0.002f);
            p = q[seg];
        }
    }
    // knuckle/nail shading via vertex colour: nails a touch lighter, creases darker
    s.paint = [](Vector3 p, Vector3 n, Color) {
        float k = 1.f;
        if (p.z < -0.12f && n.y > 0.3f) k = 1.06f;           // nails / fingertips lighter
        if (p.z > -0.1f && p.z < -0.02f && n.y < -0.5f) k = 0.93f; // palm slightly darker
        k *= 0.97f + 0.06f * noise3(Vector3Scale(p, 260.f), 5u);
        return scaleC(WHITE, k);
    };
    return meshSdf(s, 0.0021f);
}

// ---------------------------------------------------------------------------- face parts
// Eyeball (pupil toward -Z), unit radius 0.0135.
Mesh buildEye(Color iris) {
    MeshBuilder b;
    const int rings = 20, slices = 28;
    const float R = 0.0135f;
    int base = b.vertexCount();
    for (int i = 0; i <= rings; ++i) {
        float th = PI_F * i / rings;  // 0 at the pupil
        for (int j = 0; j <= slices; ++j) {
            float ph = 2.f * PI_F * j / slices;
            Vector3 d{std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), -std::cos(th)};
            Color c;
            if (th < 0.24f) c = rgb(14, 11, 10);
            else if (th < 0.58f) {
                float k = (th - 0.24f) / 0.34f;
                c = scaleC(iris, 1.2f - 0.5f * k);
                if (th > 0.53f) c = scaleC(iris, 0.42f);
            } else if (th < 1.35f) c = mixC(rgb(240, 234, 224), rgb(222, 206, 196), smooth01((th - 0.9f) / 0.45f));
            else c = rgb(214, 170, 160);
            b.vertex(Vector3Scale(d, R), d, {j / (float)slices, i / (float)rings}, c);
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = base + i * (slices + 1) + j, bb = a + 1, d = a + (slices + 1), cc = d + 1;
            b.quad(a, bb, cc, d);
        }
    return b.build(true);
}

// Upper eyelid: a shell over the top of the eye (pole +Y) down to just past the equator, with a dark
// lash line on its thick edge. Radius slightly larger than the eye.
Mesh buildLid() {
    MeshBuilder b;
    const int rings = 10, slices = 28;
    const float R = 0.0149f, Rin = 0.0137f, thMax = 1.66f;
    int base = b.vertexCount();
    auto dirOf = [](float th, float ph) {
        return Vector3{std::sin(th) * std::sin(ph), std::cos(th), -std::sin(th) * std::cos(ph)};
    };
    for (int i = 0; i <= rings; ++i) {
        float th = thMax * i / rings;
        for (int j = 0; j <= slices; ++j) {
            float ph = 2.f * PI_F * j / slices;
            Vector3 d = dirOf(th, ph);
            Color c = WHITE;
            if (i == rings) c = rgb(150, 120, 110);
            b.vertex(Vector3Scale(d, R), d, {0, 0}, c);
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = base + i * (slices + 1) + j, bb = a + 1, d = a + (slices + 1), cc = d + 1;
            b.quad(a, bb, cc, d);
        }
    // thick edge: from the outer rim inward to just above the eyeball (lash line, dark)
    int rimOuter = b.vertexCount();
    for (int j = 0; j <= slices; ++j) {
        float ph = 2.f * PI_F * j / slices;
        Vector3 d = dirOf(thMax, ph);
        Vector3 down = dirOf(thMax + 0.3f, ph);
        b.vertex(Vector3Scale(d, R), down, {0, 0}, rgb(52, 36, 30));
    }
    int rimInner = b.vertexCount();
    for (int j = 0; j <= slices; ++j) {
        float ph = 2.f * PI_F * j / slices;
        Vector3 d = dirOf(thMax + 0.04f, ph);
        Vector3 down = dirOf(thMax + 0.3f, ph);
        b.vertex(Vector3Scale(d, Rin), down, {0, 0}, rgb(40, 28, 24));
    }
    for (int j = 0; j < slices; ++j) b.quad(rimOuter + j, rimInner + j, rimInner + j + 1, rimOuter + j + 1);
    return b.build(true);
}

Mesh buildMouthCavity() {
    MeshBuilder b;
    const int rings = 12, slices = 20;
    const Vector3 r{0.0165f, 0.0080f, 0.0055f};
    int base = b.vertexCount();
    for (int i = 0; i <= rings; ++i) {
        float th = PI_F * i / rings;
        for (int j = 0; j <= slices; ++j) {
            float ph = 2.f * PI_F * j / slices;
            Vector3 u{std::sin(th) * std::sin(ph), std::cos(th), std::sin(th) * std::cos(ph)};
            Vector3 p{u.x * r.x, u.y * r.y, u.z * r.z};
            Color c = rgb(52, 18, 16);
            if (u.y > 0.45f && u.z < 0.2f) c = rgb(226, 218, 196);        // upper teeth
            else if (u.y < -0.55f && u.z < 0.2f) c = rgb(120, 48, 44);   // tongue
            b.vertex(p, vnorm({u.x / r.x, u.y / r.y, u.z / r.z}), {0, 0}, c);
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = base + i * (slices + 1) + j, bb = a + 1, d = a + (slices + 1), cc = d + 1;
            b.quad(a, d, cc, bb);
        }
    return b.build(true);
}

// Lower lip: curve with the corners raised (smile > 0) or lowered.
Sdf lowerLipSdf(float smile) {
    Sdf s;
    Color lip = rgb(168, 92, 82);
    float cy = smile * 0.0036f;
    Vector3 l{-0.0155f, cy, 0.0045f}, m{0.f, -0.0012f - std::fabs(smile) * 0.0006f, -0.0005f},
        r{0.0155f, cy, 0.0045f};
    s.cone(l, Vector3Lerp(l, m, 0.5f) + Vector3{0, -0.0008f, -0.001f}, 0.0022f, 0.0036f, lip);
    s.cone(Vector3Lerp(l, m, 0.5f) + Vector3{0, -0.0008f, -0.001f}, m, 0.0036f, 0.0044f, lip, 0.002f);
    s.cone(m, Vector3Lerp(r, m, 0.5f) + Vector3{0, -0.0008f, -0.001f}, 0.0044f, 0.0036f, lip, 0.002f);
    s.cone(Vector3Lerp(r, m, 0.5f) + Vector3{0, -0.0008f, -0.001f}, r, 0.0036f, 0.0022f, lip, 0.002f);
    s.paint = [](Vector3 p, Vector3 n, Color c) { return n.y > 0.4f ? scaleC(c, 0.8f) : (p.y < -0.004f ? scaleC(c, 0.92f) : c); };
    return s;
}

// Right eyebrow in brow space (origin at the brow centre; inner end toward -X).
Sdf browSdf(Color col, float thick, float bushy, uint32_t seed) {
    Sdf s;
    Vector3 in{-0.0175f, -0.0015f, -0.0015f}, mid{-0.002f, 0.0035f, -0.0005f}, out{0.0165f, -0.0012f, 0.0055f};
    s.cone(in, mid, thick * 0.95f, thick * 1.05f, col);
    s.cone(mid, out, thick * 1.05f, thick * 0.55f, col, 0.002f);
    Rng rng(seed);
    for (int i = 0; i < (int)(bushy * 10.f); ++i) {
        float t = rng.f();
        Vector3 p = t < 0.5f ? Vector3Lerp(in, mid, t * 2.f) : Vector3Lerp(mid, out, (t - 0.5f) * 2.f);
        p = Vector3Add(p, {rng.f(-0.002f, 0.002f), rng.f(-0.0015f, 0.003f), rng.f(-0.002f, 0.001f)});
        float r = thick * rng.f(0.5f, 0.9f);
        s.ellipsoid(p, {r * 1.6f, r * 0.8f, r}, col, 0.002f, {0, 0, rng.f(-25.f, 25.f)});
    }
    s.paint = [col](Vector3 p, Vector3, Color) {
        float k = 0.85f + 0.3f * noise3(Vector3Scale(p, 900.f), 3u);
        return scaleC(col, k);
    };
    return s;
}

// ---------------------------------------------------------------------------- heads
struct HeadOut {
    Sdf s;
    FaceGeo g;
    // separate mustache mesh (opponents and the çaycı)
    bool stache = false;
    Color stacheCol{};
    float thick = 0.f, span = 0.f, droop = 0.f, y = 0.f;
    void setStache(Color c, float t, float sp, float dr, float yy) {
        stache = true;
        stacheCol = c;
        thick = t;
        span = sp;
        droop = dr;
        y = yy;
    }
};

// How much colour `c` is "like" `ref` (1 identical .. 0 clearly different) — paints fade smoothly across
// the blended colour boundaries of the SDF (no jaggies along triangle edges).
float likeC(Color c, Color ref, float range = 60.f) {
    float d = (float)(std::abs(c.r - ref.r) + std::abs(c.g - ref.g) + std::abs(c.b - ref.b));
    return clampf(1.f - d / range, 0.f, 1.f);
}

// Shared facial structure. `w` widens, `jowl` fills the lower face, `nose` scales the nose.
void baseHead(Sdf& s, FaceGeo& g, const PersonLook& L, float w, float jowl, float nose, float noseDroop,
              Color noseCol) {
    const Color skin = L.skin;
    const Color socket = scaleC(skin, 0.9f);
    s.ellipsoid({0, 0.118f, 0.008f}, {0.076f * w, 0.090f, 0.090f}, skin);                  // cranium
    s.ellipsoid({0, 0.064f, -0.027f}, {0.066f * w, 0.071f, 0.068f}, skin, 0.03f);          // face/jaw
    s.ellipsoid({0.040f * w, 0.079f, -0.060f}, {0.021f, 0.015f, 0.017f}, skin, 0.02f);     // cheekbones
    s.ellipsoid({-0.040f * w, 0.079f, -0.060f}, {0.021f, 0.015f, 0.017f}, skin, 0.02f);
    float jr = 0.023f + 0.008f * jowl;
    s.ellipsoid({0.044f * w, 0.048f - 0.004f * jowl, -0.044f}, {jr, jr * 1.15f, jr * 1.1f}, skin, 0.025f); // jowls
    s.ellipsoid({-0.044f * w, 0.048f - 0.004f * jowl, -0.044f}, {jr, jr * 1.15f, jr * 1.1f}, skin, 0.025f);
    s.ellipsoid({0, 0.016f, -0.069f}, {0.025f * w, 0.020f, 0.020f}, skin, 0.018f);         // chin
    if (jowl > 0.6f) s.ellipsoid({0, 0.006f, -0.045f}, {0.046f * w, 0.024f, 0.04f}, skin, 0.03f); // double chin
    s.cone({-0.040f * w, 0.1255f, -0.074f}, {0.040f * w, 0.1255f, -0.074f}, 0.0105f, 0.0105f, skin, 0.018f); // brow ridge
    // nose: bridge, ball and wings
    Vector3 tip{0, 0.080f - noseDroop, -0.106f - 0.010f * nose};
    s.cone({0, 0.116f, -0.085f}, Vector3Add(tip, {0, 0.006f, 0.005f}), 0.0078f, 0.0105f * nose, skin, 0.010f);
    s.ellipsoid(tip, Vector3Scale({0.0135f, 0.0120f, 0.0125f}, nose), noseCol, 0.009f);
    s.ellipsoid({0.0125f * nose, tip.y - 0.004f, tip.z + 0.011f}, {0.0088f, 0.0074f, 0.0090f}, skin, 0.006f);
    s.ellipsoid({-0.0125f * nose, tip.y - 0.004f, tip.z + 0.011f}, {0.0088f, 0.0074f, 0.0090f}, skin, 0.006f);
    s.cutEllipsoid({0.0064f, tip.y - 0.0100f, tip.z + 0.005f}, {0.0034f, 0.0026f, 0.0045f}, rgb(90, 50, 40), 0.002f);
    s.cutEllipsoid({-0.0064f, tip.y - 0.0100f, tip.z + 0.005f}, {0.0034f, 0.0026f, 0.0045f}, rgb(90, 50, 40), 0.002f);
    // ears
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float x = sgn * 0.0775f * w;
        s.ellipsoid({x, 0.097f, 0.004f}, {0.0105f, 0.029f, 0.019f}, skin, 0.006f, {0, sgn * 18.f, sgn * 6.f});
        s.cutEllipsoid({x + sgn * 0.006f, 0.098f, -0.001f}, {0.0055f, 0.018f, 0.0105f}, scaleC(skin, 0.8f), 0.004f,
                       {0, sgn * 18.f, 0});
    }
    // eye sockets (roomy, so the eyeballs and lids read from across the table)
    for (int sgn = -1; sgn <= 1; sgn += 2)
        s.cutEllipsoid({sgn * 0.0315f, 0.1030f, -0.0810f}, {0.0218f, 0.0152f, 0.0158f}, socket, 0.006f);
    // mouth recess (dark inside)
    s.cutEllipsoid({0, 0.0420f, -0.0920f}, {0.0180f, 0.0055f, 0.0095f}, rgb(92, 40, 36), 0.004f);

    g.eye[0] = {0.0315f, 0.1030f, -0.0690f};
    g.eye[1] = {-0.0315f, 0.1030f, -0.0690f};
    g.eyeR = 0.0160f;  // a touch larger than life: the eyes must read from across the table
    g.brow[0] = {0.034f * w, 0.1268f, -0.0858f};
    g.brow[1] = {-0.034f * w, 0.1268f, -0.0858f};
    g.mouth = {0, 0.0425f, -0.0905f};
    g.mouthOut = {0, 0.040f, -0.125f};
    g.chin = {0, -0.006f, -0.070f};
    g.forehead = {0, 0.150f, -0.090f};
    g.nose = tip;
    g.headTop = 0.21f;
}

// A mustache (separate mesh: hair material, not the shiny scalp).
// `side` (Yüz): 0 the whole mustache; +1 only the +x half with the middle tuft, -1 only the -x half.
Sdf mustacheSdf(Color c, float thick, float span, float droop, float y, uint32_t seed, int side = 0) {
    Sdf s;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        if (side != 0 && sgn != side) continue;
        Vector3 a{sgn * 0.0025f, y + 0.0015f, -0.1015f};
        Vector3 m{sgn * span * 0.55f, y - 0.004f, -0.0975f};
        Vector3 e{sgn * span, y - 0.010f - droop, -0.088f};
        s.cone(a, m, thick, thick * 0.92f, c, 0.004f);
        s.cone(m, e, thick * 0.92f, thick * 0.5f, c, 0.004f);
        if (droop > 0.006f) s.cone(e, {sgn * (span + 0.002f), y - 0.012f - droop * 1.8f, -0.085f}, thick * 0.5f, thick * 0.3f, c, 0.003f);
    }
    if (side >= 0) s.ellipsoid({0, y + 0.001f, -0.1025f}, {thick * 1.5f, thick * 0.9f, thick * 0.9f}, c, 0.004f);
    s.paint = [c, seed](Vector3 p, Vector3 n, Color) {
        // combed strands: stripes running down/outward + a darker underside
        float strand = 0.5f + 0.5f * std::sin(p.x * 2400.f + p.y * 900.f);
        float k = 0.82f + 0.16f * strand + 0.12f * (noise3(Vector3Scale(p, 1500.f), seed) - 0.5f);
        if (n.y < -0.3f) k *= 0.8f;
        return scaleC(c, k);
    };
    return s;
}

// Baked mustache for the low-detail patrons (same shape, part of the head mesh).
void mustache(Sdf& s, Color c, float thick, float span, float droop, float y) {
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        Vector3 a{sgn * 0.0025f, y + 0.0015f, -0.1015f};
        Vector3 m{sgn * span * 0.55f, y - 0.004f, -0.0975f};
        Vector3 e{sgn * span, y - 0.010f - droop, -0.088f};
        s.cone(a, m, thick, thick * 0.92f, c, 0.004f);
        s.cone(m, e, thick * 0.92f, thick * 0.5f, c, 0.004f);
    }
    s.ellipsoid({0, y + 0.001f, -0.1025f}, {thick * 1.5f, thick * 0.9f, thick * 0.9f}, c, 0.004f);
}

Color skinPaint(const PersonLook& L, Vector3 p, Vector3 n, Color c, float rosy) {
    float w = likeC(c, L.skin);
    if (w <= 0.f) return c;
    float k = 0.97f + 0.06f * noise3(Vector3Scale(p, 140.f), 11u);
    Color out = scaleC(c, k);
    // warm cheeks, nose and ears
    float dc = std::min(Vector3Distance(p, {0.043f, 0.072f, -0.062f}), Vector3Distance(p, {-0.043f, 0.072f, -0.062f}));
    float cheek = smooth01(1.f - dc / 0.032f) * rosy;
    out = mixC(out, rgb(212, 110, 90), cheek * 0.32f);
    float dn = Vector3Distance(p, {0, 0.085f, -0.11f});
    out = mixC(out, rgb(214, 116, 94), smooth01(1.f - dn / 0.03f) * 0.22f);
    // gentle darkening in the eye sockets and under the jaw
    float de = std::min(Vector3Distance(p, {0.031f, 0.10f, -0.078f}), Vector3Distance(p, {-0.031f, 0.10f, -0.078f}));
    out = scaleC(out, 1.f - smooth01(1.f - de / 0.026f) * 0.10f);
    out = scaleC(out, 1.f - 0.10f * smooth01((-n.y - 0.2f) / 0.6f));
    return mixC(c, out, w);
}

HeadOut buildHeadRiza(const PersonLook& L) {
    HeadOut h;
    Sdf& s = h.s;
    baseHead(s, h.g, L, 1.0f, 0.5f, 1.05f, 0.002f, rgb(200, 128, 98));
    // grey hair at the back/sides (under the cap)
    s.ellipsoid({0, 0.106f, 0.028f}, {0.0795f, 0.068f, 0.080f}, L.hair, 0.010f);
    // upper lip
    s.ellipsoid({0, 0.0485f, -0.0925f}, {0.017f, 0.0045f, 0.007f}, L.lip, 0.004f);
    h.setStache(rgb(178, 172, 164), 0.0086f, 0.031f, 0.004f, 0.0575f);
    // flat cap (kasket): its own layer so the flat underside only carves the cap
    const Color cap = rgb(98, 90, 80), capBand = rgb(82, 74, 66);
    s.newLayer();
    s.ellipsoid({0, 0.168f, -0.012f}, {0.087f, 0.050f, 0.103f}, cap, 0.f, {-9.f, 0, 0});
    s.ellipsoid({0, 0.183f, -0.030f}, {0.083f, 0.036f, 0.092f}, cap, 0.012f, {-12.f, 0, 0});
    s.cutBox({0, 0.062f, 0.0f}, {0.2f, 0.07f, 0.2f}, 0.f, cap, 0.004f, {-8.f, 0, 0});  // flat underside of the crown
    s.ellipsoid({0, 0.147f, -0.093f}, {0.074f, 0.0065f, 0.046f}, capBand, 0.004f, {14.f, 0, 0}); // brim
    s.ellipsoid({0, 0.215f, -0.045f}, {0.010f, 0.005f, 0.010f}, capBand, 0.004f);                // button
    h.g.headTop = 0.225f;
    PersonLook Lc = L;
    s.paint = [Lc, cap](Vector3 p, Vector3 n, Color c) {
        float wc = likeC(c, cap, 50.f);
        if (wc > 0.f) {  // tweed
            float t = noise3(Vector3Scale(p, 420.f), 21u) * 0.6f + noise3(Vector3Scale(p, 90.f), 22u) * 0.4f;
            float herring = 0.5f + 0.5f * std::sin((p.x + std::fabs(std::fmod(p.z * 60.f, 1.f)) * 0.004f) * 900.f);
            return mixC(c, scaleC(c, 0.84f + 0.26f * t + 0.06f * herring), wc);
        }
        float wh = likeC(c, Lc.hair, 40.f);
        if (wh > 0.f) return mixC(c, scaleC(c, 0.88f + 0.24f * noise3(Vector3Scale(p, 700.f), 9u)), wh);
        return skinPaint(Lc, p, n, c, 0.35f);
    };
    return h;
}

HeadOut buildHeadMahmut(const PersonLook& L) {
    HeadOut h;
    Sdf& s = h.s;
    baseHead(s, h.g, L, 1.08f, 0.9f, 1.15f, 0.003f, rgb(212, 124, 100));
    // bigger dome
    s.ellipsoid({0, 0.125f, 0.004f}, {0.082f, 0.094f, 0.093f}, L.skin, 0.02f);
    // horseshoe of cropped dark hair around the back and sides
    s.ellipsoid({0, 0.098f, 0.024f}, {0.0865f, 0.050f, 0.088f}, L.hair, 0.010f);
    h.setStache(rgb(30, 24, 22), 0.0120f, 0.037f, 0.010f, 0.0565f);
    h.g.headTop = 0.222f;
    PersonLook Lc = L;
    s.paint = [Lc](Vector3 p, Vector3 n, Color c) {
        float wh = likeC(c, Lc.hair, 80.f);
        Color out = skinPaint(Lc, p, n, c, 0.55f);
        if (wh > 0.f) {
            float k = 0.8f + 0.4f * noise3(Vector3Scale(p, 800.f), 4u);
            // short-cropped: the scalp shows through toward the top edge
            float edge = smooth01((0.140f - p.y) / 0.016f);
            out = mixC(out, mixC(Lc.skin, scaleC(Lc.hair, k), 0.45f + 0.5f * edge), wh);
        }
        // stubble shadow on the jaw and cheeks
        if (p.y < 0.075f && p.z < -0.02f && std::fabs(p.x) > 0.018f)
            out = mixC(out, rgb(112, 92, 86), 0.16f * smooth01((0.075f - p.y) / 0.025f) * likeC(c, Lc.skin));
        return out;
    };
    return h;
}

HeadOut buildHeadNuri(const PersonLook& L) {
    HeadOut h;
    Sdf& s = h.s;
    baseHead(s, h.g, L, 0.95f, 0.15f, 1.12f, 0.006f, rgb(214, 150, 126));
    // hollow temples, thin face
    s.cutEllipsoid({0.064f, 0.118f, -0.050f}, {0.012f, 0.02f, 0.02f}, L.skin, 0.02f);
    s.cutEllipsoid({-0.064f, 0.118f, -0.050f}, {0.012f, 0.02f, 0.02f}, L.skin, 0.02f);
    // white hair: fluffy sides and back, the crown bald
    s.ellipsoid({0, 0.100f, 0.030f}, {0.0785f, 0.064f, 0.082f}, L.hair, 0.012f);
    s.ellipsoid({0.067f, 0.124f, 0.012f}, {0.022f, 0.028f, 0.048f}, L.hair, 0.014f);
    s.ellipsoid({-0.067f, 0.124f, 0.012f}, {0.022f, 0.028f, 0.048f}, L.hair, 0.014f);
    s.ellipsoid({0, 0.0485f, -0.0925f}, {0.016f, 0.0042f, 0.007f}, L.lip, 0.004f);
    h.setStache(rgb(232, 230, 224), 0.0072f, 0.028f, 0.002f, 0.0565f);
    h.g.headTop = 0.212f;
    PersonLook Lc = L;
    s.paint = [Lc](Vector3 p, Vector3 n, Color c) {
        float wh = likeC(c, Lc.hair, 80.f);
        Color out = skinPaint(Lc, p, n, c, 0.3f);
        if (wh > 0.f) {
            float k = 0.86f + 0.2f * noise3(Vector3Scale(p, 650.f), 8u);
            float wisps = 0.5f + 0.5f * std::sin(p.y * 1400.f + p.z * 500.f);
            out = mixC(out, scaleC(Lc.hair, k * (0.94f + 0.08f * wisps)), wh);
        }
        return out;
    };
    return h;
}

HeadOut buildHeadCayci(const PersonLook& L) {
    HeadOut h;
    Sdf& s = h.s;
    baseHead(s, h.g, L, 0.95f, 0.0f, 0.92f, 0.0f, rgb(206, 142, 112));
    s.ellipsoid({0, 0.0485f, -0.0925f}, {0.016f, 0.0045f, 0.007f}, L.lip, 0.004f);
    h.setStache(L.hair, 0.0052f, 0.023f, 0.0f, 0.0575f);
    // young man's dark hair, short at the sides with a little volume on top (own layer so the hairline
    // cut spares the forehead)
    s.newLayer();
    s.ellipsoid({0, 0.120f, 0.010f}, {0.0782f, 0.0930f, 0.0925f}, L.hair, 0.f);
    s.ellipsoid({0.010f, 0.176f, -0.018f}, {0.058f, 0.030f, 0.066f}, L.hair, 0.024f, {-6.f, 0, -8.f}); // a little quiff
    s.cutBox({0, 0.10f, -0.14f}, {0.2f, 0.034f, 0.07f}, 0.f, L.hair, 0.008f, {-22.f, 0, 0});    // forehead hairline
    s.cutEllipsoid({0.074f, 0.125f, -0.072f}, {0.030f, 0.032f, 0.042f}, L.hair, 0.012f);         // temples
    s.cutEllipsoid({-0.074f, 0.125f, -0.072f}, {0.030f, 0.032f, 0.042f}, L.hair, 0.012f);
    // the hairline: low on the nape (the back of the skull is covered), rising toward the ears
    s.cutBox({0, -0.0015f, 0.f}, {0.2f, 0.07f, 0.2f}, 0.f, L.hair, 0.01f, {24.f, 0, 0});
    s.cutEllipsoid({0.084f, 0.098f, 0.004f}, {0.016f, 0.028f, 0.026f}, L.hair, 0.008f);          // around the ears
    s.cutEllipsoid({-0.084f, 0.098f, 0.004f}, {0.016f, 0.028f, 0.026f}, L.hair, 0.008f);
    h.g.headTop = 0.222f;
    PersonLook Lc = L;
    s.paint = [Lc](Vector3 p, Vector3 n, Color c) {
        float wh = likeC(c, Lc.hair, 60.f);
        if (wh > 0.f) {
            // combed strands and a little noise, no finer than the mesh (3.1 mm) can carry
            float comb = 0.5f + 0.5f * std::sin(p.x * 380.f + p.z * 140.f);
            return mixC(c, scaleC(c, 0.88f + 0.22f * noise3(Vector3Scale(p, 220.f), 2u) + 0.10f * comb), wh);
        }
        return skinPaint(Lc, p, n, c, 0.25f);
    };
    return h;
}

// Background patrons: simplified faces with baked eyes and brows (seen from 2-4 m in the haze).
HeadOut buildHeadPatron(const PersonLook& L, int v) {
    HeadOut h;
    Sdf& s = h.s;
    float w = 1.f + 0.05f * (float)((v * 7) % 3 - 1);
    baseHead(s, h.g, L, w, v % 2 ? 0.7f : 0.3f, 1.05f + 0.05f * (float)(v % 3), 0.003f, scaleC(L.skin, 1.05f));
    // baked eyes (white, dark iris, a heavy upper lid) and brows
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        s.ellipsoid({sgn * 0.031f, 0.1025f, -0.0735f}, {0.0135f, 0.0090f, 0.0100f}, rgb(214, 206, 192), 0.f);
        s.ellipsoid({sgn * 0.030f, 0.1020f, -0.0830f}, {0.0058f, 0.0062f, 0.0030f}, rgb(38, 28, 22), 0.f);
        s.ellipsoid({sgn * 0.031f, 0.1080f, -0.0745f}, {0.0150f, 0.0060f, 0.0105f}, scaleC(L.skin, 0.92f), 0.002f);
        s.cone({sgn * 0.016f, 0.1215f, -0.087f}, {sgn * 0.047f, 0.1225f, -0.079f}, 0.0042f, 0.0032f, L.browCol, 0.002f);
    }
    switch (v) {
    case 0: // flat cap, grey mustache
        s.ellipsoid({0, 0.108f, 0.018f}, {0.0795f, 0.071f, 0.087f}, L.hair, 0.010f);
        mustache(s, rgb(170, 166, 158), 0.009f, 0.031f, 0.004f, 0.0575f);
        s.newLayer();
        s.ellipsoid({0, 0.168f, -0.012f}, {0.087f * w, 0.050f, 0.103f}, rgb(58, 60, 64), 0.f, {-9.f, 0, 0});
        s.cutBox({0, 0.062f, 0.0f}, {0.2f, 0.07f, 0.2f}, 0.f, rgb(58, 60, 64), 0.004f, {-8.f, 0, 0});
        s.ellipsoid({0, 0.147f, -0.093f}, {0.074f, 0.0065f, 0.046f}, rgb(50, 52, 56), 0.004f, {14.f, 0, 0});
        break;
    case 1: // bald with a dark mustache
        s.ellipsoid({0, 0.098f, 0.022f}, {0.083f, 0.05f, 0.088f}, L.hair, 0.012f);
        mustache(s, rgb(40, 32, 28), 0.011f, 0.034f, 0.006f, 0.0565f);
        break;
    case 2: // thick black hair and mustache
        mustache(s, L.hair, 0.010f, 0.033f, 0.003f, 0.0575f);
        s.newLayer();
        s.ellipsoid({0, 0.124f, 0.012f}, {0.081f * w, 0.094f, 0.092f}, L.hair, 0.f);
        s.cutBox({0, 0.10f, -0.14f}, {0.2f, 0.035f, 0.07f}, 0.f, L.hair, 0.008f, {-24.f, 0, 0});
        s.cutBox({0, 0.02f, 0.f}, {0.2f, 0.07f, 0.2f}, 0.f, L.hair, 0.01f, {-12.f, 0, 0});
        break;
    case 3: // white hair, clean shaven
        s.ellipsoid({0, 0.112f, 0.024f}, {0.0785f, 0.078f, 0.084f}, L.hair, 0.012f);
        s.ellipsoid({0.066f, 0.128f, 0.012f}, {0.022f, 0.028f, 0.05f}, L.hair, 0.014f);
        s.ellipsoid({-0.066f, 0.128f, 0.012f}, {0.022f, 0.028f, 0.05f}, L.hair, 0.014f);
        break;
    case 4: // skull cap (takke) and a short grey beard
        s.ellipsoid({0, 0.022f, -0.058f}, {0.052f, 0.04f, 0.04f}, L.hair, 0.012f);
        mustache(s, L.hair, 0.008f, 0.03f, 0.006f, 0.0575f);
        s.newLayer();
        s.ellipsoid({0, 0.155f, 0.004f}, {0.081f * w, 0.066f, 0.093f}, rgb(222, 218, 206), 0.f);
        s.cutBox({0, 0.08f, 0.f}, {0.2f, 0.06f, 0.2f}, 0.f, rgb(222, 218, 206), 0.002f);
        break;
    default: // grey hair, bushy mustache
        mustache(s, rgb(120, 116, 110), 0.011f, 0.035f, 0.007f, 0.0565f);
        s.newLayer();
        s.ellipsoid({0, 0.121f, 0.012f}, {0.0805f * w, 0.0925f, 0.0925f}, L.hair, 0.f);
        s.cutBox({0, 0.105f, -0.14f}, {0.2f, 0.035f, 0.07f}, 0.f, L.hair, 0.008f, {-20.f, 0, 0});
        s.cutBox({0, 0.02f, 0.f}, {0.2f, 0.07f, 0.2f}, 0.f, L.hair, 0.01f, {-12.f, 0, 0});
        break;
    }
    PersonLook Lc = L;
    s.paint = [Lc](Vector3 p, Vector3 n, Color c) { return skinPaint(Lc, p, n, c, 0.35f); };
    return h;
}

// ---------------------------------------------------------------------------- torsos (torso space)
// Clothing is painted from the position, with every colour boundary ramped over ~2.5-3 mesh cells: the SDF's
// own primitive colours switch within a millimetre, which the surface-nets vertices (and the decimation) turn
// into sawtooth edges. Only small hard-unioned accessories (buttons, the watch chain, the belt) keep their
// primitive colour — their outline is a geometric crease — and the collar is real geometry (see addCollar).
struct TorsoSpec {
    float w = 1.f;       // width scale
    float belly = 0.f;   // 0..1
    float hunch = 0.f;   // neck forward
    float neckR = 0.047f;
};

// 0 -> 1 across [-width/2, width/2]
float ramp(float x, float width) { return smooth01(x / width + 0.5f); }

constexpr float kEdge = 0.013f;             // painted boundary width on the regulars (cell 0.0045)
constexpr float kEdgePatron = 0.018f;       // ... on the background patrons (cell 0.0065)
constexpr float kCellTorso = 0.0045f, kCellPatronTorso = 0.0065f;
const Color kCollarMark{255, 0, 255, 255};  // primitive colour of the collar geometry (always painted over)
constexpr float kCollarCell = 0.0024f;
constexpr float kCollarLift = 0.011f;       // collar band centre above the neckline
constexpr float kColorEdge = 0.005f;
// MeshJobs cost estimates (only the order the jobs start in): an SDF's raw triangles per (bounding-box area / cell²),
// and a torso's raw triangles times cell² (measured: ~80k at kCellTorso, ~40k at kCellPatronTorso)
constexpr float kSdfTrisPerArea = 1.f, kTorsoTrisCell2 = 1.6f, kHandTris = 25000.f;        // simplifyMesh: colour steps above this keep their vertices

struct NeckAxis {
    Vector3 a, b;  // bottom (inside the shoulders) and top (the head pivot)
    float r;
};
NeckAxis neckAxis(const PersonLook& L, const TorsoSpec& t) {
    return {{0, L.shoulderY + 0.02f, 0.014f}, {0, L.spineLen - 0.004f, L.headZ + 0.004f - t.hunch}, t.neckR * t.w};
}

// First surface crossing of `s` from `from` along the unit direction `dir` (sphere tracing, then bisection).
bool marchSdf(const Sdf& s, Vector3 from, Vector3 dir, float maxT, Vector3& hit) {
    float t = 0.f, d = s.eval(from);
    if (d <= 0.f) return false;
    for (int i = 0; i < 128 && t < maxT; ++i) {
        const float t1 = t + std::max(d * 0.8f, 0.0006f);
        const float d1 = s.eval(Vector3Add(from, Vector3Scale(dir, t1)));
        if (d1 <= 0.f) {
            float lo = t, hi = t1;
            for (int k = 0; k < 14; ++k) {
                const float m = 0.5f * (lo + hi);
                (s.eval(Vector3Add(from, Vector3Scale(dir, m))) > 0.f ? lo : hi) = m;
            }
            hit = Vector3Add(from, Vector3Scale(dir, hi));
            return true;
        }
        t = t1;
        d = d1;
    }
    return false;
}

// Where the neck leaves the shoulders and the back: for each direction around the neck axis, the height
// below which the body flares out past the neck. The collar sits on this line; skin starts above it.
struct Neckline {
    static constexpr int N = 48;
    NeckAxis ax{};
    std::array<float, N> base{};  // [0] straight back (+Z), increasing toward +X
    Vector3 axisAt(float y) const { return Vector3Lerp(ax.a, ax.b, (y - ax.a.y) / (ax.b.y - ax.a.y)); }
    static Vector3 dirOf(float th) { return {std::sin(th), 0.f, std::cos(th)}; }
    float angleOf(Vector3 p) const {
        const Vector3 a = axisAt(p.y);
        return std::atan2(p.x - a.x, p.z - a.z);
    }
    float baseAt(float th) const {
        float u = th / (2.f * PI_F) * (float)N;
        u -= std::floor(u / (float)N) * (float)N;
        const int i = std::min((int)u, N - 1);
        return lerpf(base[(size_t)i], base[(size_t)((i + 1) % N)], u - (float)i);
    }
    float baseAt(Vector3 p) const { return baseAt(angleOf(p)); }
    // the surface at angle `th`, height `y` (marching in toward the axis)
    Vector3 surface(const Sdf& s, float th, float y) const {
        const Vector3 a = axisAt(y), d = dirOf(th);
        Vector3 hit;
        if (marchSdf(s, Vector3Add(a, Vector3Scale(d, 0.30f)), Vector3Negate(d), 0.30f, hit)) return hit;
        return Vector3Add(a, Vector3Scale(d, ax.r));
    }
};

Neckline findNeckline(const Sdf& s, const NeckAxis& ax) {
    Neckline nl;
    nl.ax = ax;
    const float yTop = ax.b.y - 0.03f, yLow = ax.a.y - 0.05f;
    for (int i = 0; i < Neckline::N; ++i) {
        const float th = 2.f * PI_F * (float)i / (float)Neckline::N;
        float b = yLow;
        for (float y = yTop; y > yLow; y -= 0.002f) {
            const Vector3 a = nl.axisAt(y), p = nl.surface(s, th, y);
            if (std::hypot(p.x - a.x, p.z - a.z) > ax.r + 0.012f) {
                b = y;
                break;
            }
        }
        nl.base[(size_t)i] = b;
    }
    // a thick neck on a round back would pull the line (and the skin) far down between the shoulder blades:
    // a collar sits at most 3 cm lower at the back than at the sides
    const float side = 0.5f * (nl.base[(size_t)(Neckline::N / 4)] + nl.base[(size_t)(3 * Neckline::N / 4)]);
    for (int i = 0; i < Neckline::N; ++i) {
        const float c = std::cos(2.f * PI_F * (float)i / (float)Neckline::N);
        if (c > 0.f) nl.base[(size_t)i] = std::max(nl.base[(size_t)i], side - 0.03f * c);
    }
    for (int pass = 0; pass < 3; ++pass) {  // a collar line has no kinks
        const std::array<float, Neckline::N> t = nl.base;
        for (int i = 0; i < Neckline::N; ++i)
            nl.base[(size_t)i] = 0.25f * t[(size_t)((i + Neckline::N - 1) % Neckline::N)] + 0.5f * t[(size_t)i] +
                                 0.25f * t[(size_t)((i + 1) % Neckline::N)];
    }
    return nl;
}

// A shirt collar on the neckline: a band around the back of the neck, standing ~5 mm proud of it, and two
// leaves lying flat on the chest. `closed`: buttoned at the throat — each leaf runs from the side of the
// neck down to its point (±tipX, tipDrop lower) and back up to the top button, so the collar reads as the
// familiar W; open: the band ends at the V's corners and the points spread outward over the chest.
// Real geometry in its own SDF, meshed finer than the torso (a 1 cm tube is only two torso cells across) and
// merged into the torso mesh, so its outline is a crisp small silhouette.
struct CollarLine {
    std::vector<Vector3> a, b;  // centre-line segments
    float r = 0.f;
    float dist(Vector3 p) const {
        float best = 1e9f;
        for (size_t i = 0; i < a.size(); ++i) {
            const Vector3 ab = Vector3Subtract(b[i], a[i]);
            const float t = clampf(Vector3DotProduct(Vector3Subtract(p, a[i]), ab) / std::max(Vector3DotProduct(ab, ab), 1e-9f), 0.f, 1.f);
            best = std::min(best, Vector3Distance(p, Vector3Add(a[i], Vector3Scale(ab, t))));
        }
        return best;
    }
};

CollarLine addCollar(Sdf& collar, const Sdf& body, const Neckline& nl, bool closed, float tipX, float tipDrop,
                     float r = 0.0095f) {
    CollarLine cl;
    cl.r = r;
    auto seg = [&](Vector3 p, Vector3 q, float r0, float r1) {
        collar.cone(p, q, r0, r1, kCollarMark, 0.004f);
        cl.a.push_back(p);
        cl.b.push_back(q);
    };
    // a point on the chest at (x, y), its axis `inset` under the surface
    auto onChest = [&](float x, float y, float inset, Vector3 fallback) {
        Vector3 hit;
        if (!marchSdf(body, {x, y, -0.35f}, {0, 0, 1}, 0.35f, hit)) return fallback;
        hit.z += inset;
        return hit;
    };
    const float thEnd = closed ? PI_F - 0.75f : PI_F - 0.85f;
    const int n = 16;
    for (int sg = -1; sg <= 1; sg += 2) {
        Vector3 prev{};
        for (int i = 0; i <= n; ++i) {
            const float th = (float)sg * thEnd * (float)i / (float)n;
            Vector3 p = nl.surface(body, th, nl.baseAt(th) + kCollarLift);
            p = Vector3Subtract(p, Vector3Scale(Neckline::dirOf(th), r * 0.5f));
            if (i > 0) seg(prev, p, r, r);
            prev = p;
        }
        const Vector3 c = prev;  // the band's front end
        const float yTip = c.y - tipDrop;
        Vector3 mid = onChest(lerpf(c.x, (float)sg * tipX, 0.5f), lerpf(c.y, yTip, 0.5f), r * 0.45f, c);
        const Vector3 tip = onChest((float)sg * tipX, yTip, r * 0.35f, mid);
        seg(c, mid, r, r * 0.8f);
        seg(mid, tip, r * 0.8f, r * 0.45f);
        if (closed) {  // the leaf's inner edge, from its point back up to the top button
            const Vector3 top = onChest((float)sg * 0.006f, c.y - 0.006f, r * 0.5f, c);
            seg(tip, top, r * 0.45f, r * 0.75f);
        }
    }
    return cl;
}

// A torso: the body and its collar (meshed separately, then merged).
struct TorsoOut {
    Sdf body, collar;
};

void torsoBase(Sdf& s, const PersonLook& L, const TorsoSpec& t, Color body, Color shoulder, Color neck) {
    const float w = t.w;
    const float sh = L.shoulderW;
    const float sy = L.shoulderY;
    s.ellipsoid({0, 0.035f, 0.002f}, {0.140f * w, 0.090f, 0.098f}, body);                      // pelvis
    s.ellipsoid({0, 0.15f - 0.02f * t.belly, -0.026f - 0.036f * t.belly},
                {(0.138f + 0.034f * t.belly) * w, 0.122f + 0.034f * t.belly, 0.094f + 0.05f * t.belly}, body, 0.05f); // belly
    s.ellipsoid({0, sy - 0.085f, 0.004f}, {0.150f * w, 0.120f, 0.098f + 0.008f * t.belly}, body, 0.06f); // chest
    // sloping shoulders (trapezius) from the neck base out to the shoulder joints, and the deltoids
    for (int sg = -1; sg <= 1; sg += 2) {
        s.cone({sg * 0.035f, sy + 0.040f, 0.018f}, {sg * (sh - 0.02f), sy + 0.006f, 0.014f}, 0.046f * w, 0.046f * w, shoulder, 0.045f);
        s.ellipsoid({sg * sh, sy - 0.012f, 0.012f}, {0.050f * w, 0.058f, 0.052f * w}, shoulder, 0.03f);
    }
    const NeckAxis n = neckAxis(L, t);
    s.cone(n.a, n.b, n.r, n.r * 0.88f, neck, 0.022f);
}

Color fabric(Color c, Vector3 p, float grain, uint32_t seed) {
    // mottling no finer than ~2 cells (finer noise only aliases on the mesh vertices)
    float k = 0.955f + 0.07f * noise3(Vector3Scale(p, 45.f), seed) + grain * 0.4f * (noise3(Vector3Scale(p, 90.f), seed + 1) - 0.5f);
    return scaleC(c, k);
}

// 1 on the front opening of a garment: a V from its point at yBottom widening to halfTop at yTop (and above).
float vOpening(Vector3 p, float yBottom, float yTop, float halfTop, float edge) {
    const float vx = halfTop * std::min((p.y - yBottom) / (yTop - yBottom), 1.f);
    return ramp(vx - std::fabs(p.x), edge) * ramp(-p.z - 0.03f, 0.03f);
}
// 1 inside a waistcoat's armhole (an ellipse in x/y around the shoulder joint, front and back alike).
float armhole(Vector3 p, Vector2 c, Vector2 r, float edge) {
    const float dx = (std::fabs(p.x) - c.x) / r.x, dy = (p.y - c.y) / r.y;
    return ramp((1.f - std::sqrt(dx * dx + dy * dy)) * std::min(r.x, r.y), edge);
}
// Skin above the collar.
float neckSkin(Vector3 p, float base) { return ramp(p.y - (base + kCollarLift + 0.0105f), 0.008f); }
// 1 on a garment worn over the shirt, 0 close around the collar (the shirt shows between them). Measured from
// the collar's centre line, so the edge stays sharp on the near-flat shoulder tops too.
float belowCollar(Vector3 p, const CollarLine& cl, float edge) { return ramp(cl.dist(p) - (cl.r + 0.012f), edge); }
// A darker piping line along a mask's edge (where it crosses 0.5).
float piping(float w) { return 1.f - std::fabs(2.f * w - 1.f); }

// Kel Mahmut's shirt stripes (torso and sleeves share the profile, so they match at the shoulder): a soft,
// nearly sinusoidal ramp — a hard one only zig-zags on the mesh vertices.
float stripeMix(float band) { return smooth01((band - 0.40f) / 0.55f) * 0.85f; }
const Color kStripe{58, 76, 124, 255};

TorsoOut torsoRiza(const PersonLook& L) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color vest = rgb(84, 58, 40), shirt = L.sleeve, skin = L.skin;
    const TorsoSpec ts{1.0f, 0.35f, 0.f, 0.047f};
    torsoBase(s, L, ts, vest, shirt, L.skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    const CollarLine cl = addCollar(t.collar, s, nl, true, 0.037f, 0.034f);
    t.collar.paint = [shirt](Vector3 p, Vector3, Color) { return fabric(shirt, p, 0.06f, 33u); };
    // vest buttons
    const Color btn = rgb(40, 28, 20), gold = rgb(214, 176, 96);
    for (int i = 0; i < 4; ++i) s.ellipsoid({0.004f, 0.245f - 0.050f * i, -0.126f + 0.006f * i}, {0.0055f, 0.0055f, 0.004f}, btn, 0.f);
    // watch chain
    s.cone({0.02f, 0.14f, -0.128f}, {0.09f, 0.15f, -0.105f}, 0.0016f, 0.0016f, gold, 0.f);
    const float sy = L.shoulderY;
    s.paint = [=](Vector3 p, Vector3, Color c) {
        // the waistcoat: straps over the shoulders up to the collar, armholes, the V at the front
        float wv = belowCollar(p, cl, kEdge);
        wv *= 1.f - armhole(p, {0.195f, sy - 0.005f}, {0.064f, 0.088f}, kEdge);
        wv *= 1.f - vOpening(p, 0.235f, sy + 0.01f, 0.050f, kEdge);
        const Color sh = fabric(shirt, p, 0.06f, 33u);
        const Color v = scaleC(fabric(vest, p, 0.10f, 32u), 1.f - 0.22f * piping(wv));
        Color out = mixC(sh, v, wv);
        out = mixC(out, skin, neckSkin(p, nl.baseAt(p)));
        return mixC(out, c, std::max(likeC(c, btn, 40.f), likeC(c, gold, 60.f)));
    };
    return t;
}

Color mahmutShirt(Color shirt, Vector3 p) {
    const float ang = std::atan2(p.x, -(p.z - 0.01f));
    const float band = 0.5f + 0.5f * std::cos(ang * 26.f);
    return mixC(fabric(shirt, p, 0.05f, 42u), fabric(kStripe, p, 0.05f, 43u), stripeMix(band));
}

TorsoOut torsoMahmut(const PersonLook& L) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color shirt = L.sleeve, skin = L.skin;
    const TorsoSpec ts{1.06f, 1.0f, 0.f, 0.058f};
    torsoBase(s, L, ts, shirt, shirt, L.skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    addCollar(t.collar, s, nl, false, 0.088f, 0.050f);  // open collar, the points spread on the chest
    t.collar.paint = [shirt](Vector3 p, Vector3, Color) { return mahmutShirt(shirt, p); };
    // buttons on the placket (straining over the belly)
    const Color btn = rgb(230, 230, 236);
    for (int i = 0; i < 4; ++i) s.ellipsoid({0.f, 0.27f - 0.06f * i, -0.115f - 0.028f * (i >= 2)}, {0.005f, 0.005f, 0.004f}, btn, 0.f);
    const float sy = L.shoulderY;
    s.paint = [=](Vector3 p, Vector3, Color c) {
        Color k = mahmutShirt(shirt, p);
        const float front = ramp(-p.z - 0.05f, 0.02f);
        k = scaleC(k, 1.f - 0.05f * front * ramp(0.012f - std::fabs(p.x), 0.008f));  // placket
        // open collar: a V of skin down to the second button, a little dark chest hair in the middle
        const float inV = vOpening(p, sy - 0.035f, sy + 0.05f, 0.044f, 0.011f);
        const float hair = smooth01((noise3(Vector3Scale(p, 70.f), 41u) - 0.40f) / 0.35f) *
                           smooth01(1.f - std::fabs(p.x) / 0.035f) * 0.40f;
        k = mixC(k, mixC(scaleC(skin, 0.95f), rgb(84, 62, 52), hair), inV);
        k = mixC(k, skin, neckSkin(p, nl.baseAt(p)));
        return mixC(k, c, likeC(c, btn, 30.f));
    };
    return t;
}

TorsoOut torsoNuri(const PersonLook& L) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color card = L.sleeve, shirt = rgb(150, 162, 176), skin = L.skin;
    const TorsoSpec ts{0.92f, 0.1f, 0.022f, 0.043f};
    torsoBase(s, L, ts, card, card, L.skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    const CollarLine cl = addCollar(t.collar, s, nl, true, 0.038f, 0.034f, 0.0088f);
    t.collar.paint = [shirt](Vector3 p, Vector3, Color) { return fabric(shirt, p, 0.05f, 51u); };
    const Color btn = rgb(70, 46, 30);
    for (int i = 0; i < 4; ++i) s.ellipsoid({0.002f, 0.215f - 0.052f * i, -0.108f + 0.002f * i}, {0.006f, 0.006f, 0.0045f}, btn, 0.f);
    const float sy = L.shoulderY;
    s.paint = [=](Vector3 p, Vector3, Color c) {
        // the cardigan: a knit with a ribbed waistband and a lighter button band along the V
        const float inV = vOpening(p, 0.24f, sy + 0.02f, 0.042f, kEdge);
        const float wc = belowCollar(p, cl, kEdge) * (1.f - inV);
        const float vx = 0.042f * std::min((p.y - 0.24f) / (sy + 0.02f - 0.24f), 1.f);
        const float bandW = ramp(0.011f - std::fabs(std::fabs(p.x) - vx - 0.010f), 0.008f) * ramp(p.y - 0.235f, 0.02f) *
                            ramp(-p.z - 0.04f, 0.02f);
        Color k = fabric(card, p, 0.12f, 52u);
        k = scaleC(k, (1.f + 0.10f * bandW) * (1.f - 0.07f * ramp(0.075f - p.y, 0.01f) * ramp(p.y - 0.03f, 0.01f)));
        Color out = mixC(fabric(shirt, p, 0.05f, 51u), k, wc);
        out = mixC(out, skin, neckSkin(p, nl.baseAt(p)));
        return mixC(out, c, likeC(c, btn, 40.f));
    };
    return t;
}

TorsoOut torsoCayci(const PersonLook& L) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color shirt = L.sleeve, apron = rgb(34, 26, 22), vest = rgb(30, 30, 34), skin = L.skin;
    const TorsoSpec ts{0.90f, 0.f, 0.f, 0.052f};
    torsoBase(s, L, ts, shirt, shirt, L.skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    const CollarLine cl = addCollar(t.collar, s, nl, true, 0.038f, 0.034f, 0.0088f);
    t.collar.paint = [shirt](Vector3 p, Vector3, Color) { return fabric(shirt, p, 0.04f, 62u); };
    // belt
    s.ellipsoid({0, 0.050f, 0.002f}, {0.128f, 0.016f, 0.100f}, apron, 0.006f);
    const float sy = L.shoulderY;
    const Color trousers = L.trousers;
    s.paint = [=](Vector3 p, Vector3, Color c) {
        const Color sh = fabric(shirt, p, 0.04f, 62u);
        // a black waistcoat over the white shirt: straps up to the collar, armholes, the V at the front
        float wv = belowCollar(p, cl, kEdge) * ramp(p.y - 0.062f, 0.008f);
        wv *= 1.f - armhole(p, {0.185f, sy - 0.01f}, {0.062f, 0.092f}, kEdge);
        wv *= 1.f - vOpening(p, 0.16f, sy - 0.02f, 0.06f, kEdge);
        Color out = mixC(sh, fabric(vest, p, 0.08f, 61u), wv);
        // below the belt: the trousers (he stands, so no seated-legs mesh covers the hips)
        out = mixC(out, fabric(trousers, p, 0.06f, 64u), ramp(0.040f - p.y, 0.012f));
        out = mixC(out, skin, neckSkin(p, nl.baseAt(p)));
        return mixC(out, fabric(apron, p, 0.08f, 63u), likeC(c, apron, 40.f));
    };
    return t;
}

TorsoOut torsoPatron(const PersonLook& L, int v) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color body = L.sleeve, col = L.cuff, skin = L.skin;
    const TorsoSpec ts{0.95f + 0.04f * (float)(v % 3), 0.2f + 0.3f * (float)(v % 2), 0.01f, 0.047f};
    torsoBase(s, L, ts, body, body, L.skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    const CollarLine cl = addCollar(t.collar, s, nl, true, 0.042f, 0.036f);
    t.collar.paint = [col](Vector3 p, Vector3, Color) { return fabric(col, p, 0.05f, 71u); };
    const int var = v;
    s.paint = [=](Vector3 p, Vector3, Color) {
        Color k = fabric(body, p, 0.10f, 72u + (uint32_t)var);
        if (var == 2) {  // checked flannel (soft: the pattern is only a few cells across)
            const float a = std::sin(p.x * 220.f) * std::sin(p.y * 220.f);
            k = scaleC(k, 0.94f + 0.10f * (0.5f + 0.5f * a));
        }
        // the jacket's opening shows the shirt; the jacket ends a little below the shirt collar
        const float coat = belowCollar(p, cl, kEdgePatron) * (1.f - vOpening(p, 0.22f, 0.36f, 0.06f, kEdgePatron));
        Color out = mixC(fabric(col, p, 0.05f, 71u), k, coat);
        return mixC(out, skin, neckSkin(p, nl.baseAt(p)));
    };
    return t;
}

// Seated legs in character-local space (static).
Sdf seatedLegs(const PersonLook& L, float w) {
    Sdf s;
    const Color tr = L.trousers, sh = L.shoes;
    s.ellipsoid({0, 0.515f, 0.05f}, {0.162f * w, 0.068f, 0.13f}, tr);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float x0 = sgn * 0.088f * w, x1 = sgn * 0.105f * w;
        s.cone({x0, 0.532f, 0.03f}, {x1, 0.527f, -0.37f}, 0.077f * w, 0.057f, tr, 0.03f);
        s.ellipsoid({x1, 0.527f, -0.385f}, {0.054f, 0.054f, 0.054f}, tr, 0.02f);
        s.cone({x1, 0.50f, -0.40f}, {sgn * 0.115f * w, 0.10f, -0.43f}, 0.050f, 0.040f, tr, 0.02f);
        s.ellipsoid({sgn * 0.115f * w, 0.045f, -0.47f}, {0.046f, 0.040f, 0.108f}, sh, 0.012f);
    }
    s.paint = [tr](Vector3 p, Vector3, Color c) {
        float wt = likeC(c, tr, 50.f);
        Color shoe = scaleC(c, 0.9f + 0.2f * noise3(Vector3Scale(p, 100.f), 82u));
        // a crease line down the front of each trouser leg
        Color t = fabric(c, p, 0.08f, 81u);
        t = scaleC(t, 1.f + 0.08f * smooth01(1.f - std::fabs(std::fabs(p.x) - 0.10f) / 0.012f));
        return mixC(shoe, t, wt);
    };
    return s;
}

// Standing legs of the çaycı: thigh (hip -> knee) and shin+shoe (knee -> ankle, foot toward -Z).
Mesh buildThigh(const PersonLook& L, float len) {
    MeshBuilder b;
    latheColored(b, limbProfile(len, 0.070f, 0.054f), 20, [L](float, float) { return L.trousers; });
    return b.build(true);
}
Sdf shinSdf(const PersonLook& L, float len) {
    Sdf s;
    s.cone({0, 0, 0}, {0, len, 0}, 0.052f, 0.040f, L.trousers);
    // shoe: the shin mesh runs along +Y (knee -> ankle), mesh +Z = backwards, so the toe goes to -Z
    s.ellipsoid({0, len + 0.03f, -0.045f}, {0.047f, 0.040f, 0.112f}, L.shoes, 0.015f);
    s.box({0, len + 0.062f, -0.04f}, {0.046f, 0.008f, 0.11f}, 0.006f, scaleC(L.shoes, 0.6f), 0.004f);
    return s;
}

// Arm segments along +Y. upper: shoulder -> elbow, fore: elbow -> wrist.
Mesh buildUpperArm(const PersonLook& L, bool striped) {
    MeshBuilder b;
    const float r0 = L.armR, r1 = L.armR * 0.84f;
    Color c = L.sleeve;
    latheColored(b, limbProfile(L.upperArm, r0, r1), striped ? 60 : 22, [&](float u, float y) {
        Color k = scaleC(c, 0.95f + 0.1f * noise1(u * 40.f + y * 70.f, 3u));
        if (striped) {
            float band = 0.5f + 0.5f * std::cos(u * 10.f * 2.f * PI_F);
            k = mixC(k, kStripe, stripeMix(band));
        }
        return k;
    });
    return b.build(true);
}

Mesh buildForeArm(const PersonLook& L, bool rolled) {
    MeshBuilder b;
    const float r0 = L.armR * 0.82f, r1 = L.armR * 0.62f;
    const float len = L.foreArm;
    if (L.bareForearm) {
        // rolled-up sleeve at the elbow, bare skin to the wrist
        latheColored(b, limbProfile(len, r0 * 0.92f, r1), 20, [&](float u, float y) {
            float hair = noise1(u * 60.f + y * 300.f, 7u);
            Color k = scaleC(L.skin, 0.97f + 0.05f * noise1(u * 17.f, 2u));
            if (y > 0.04f && y < len * 0.8f && hair > 0.72f) k = scaleC(k, 0.78f);
            return k;
        });
        if (rolled) {
            std::vector<Vector2> roll;
            for (int i = 0; i <= 8; ++i) {
                float t = PI_F * i / 8.f - PI_F * 0.5f;
                roll.push_back({r0 * 1.06f + 0.012f * std::cos(t), 0.035f + 0.022f * std::sin(t)});
            }
            latheColored(b, roll, 20, [&](float, float) { return scaleC(L.sleeve, 0.95f); });
        }
    } else {
        // the sleeve ends in a slightly flared cuff with a short, flat end (not a round cap reaching past the
        // wrist): the hand's base always comes out of its opening, however the wrist bends
        std::vector<Vector2> prof;
        for (const Vector2& q : limbProfile(len, r0, r1))
            if (q.y <= len - 0.026f) prof.push_back(q);
        const float rc = r1 * 1.13f, e = 0.0045f, end = len + 0.010f;  // the cuff hides the heel of the hand
        prof.push_back({r1 * 1.02f, len - 0.024f});
        prof.push_back({rc, len - 0.018f});
        prof.push_back({rc, end - e});
        for (int i = 1; i <= 3; ++i) {
            const float t = (PI_F * 0.5f) * (float)i / 3.f;
            prof.push_back({rc - e + e * std::cos(t), end - e + e * std::sin(t)});
        }
        prof.push_back({r1 * 0.55f, end + 0.0005f});
        prof.push_back({0.f, end + 0.0005f});
        latheColored(b, prof, 20, [&](float u, float y) {
            Color k = scaleC(L.cuff, 0.95f + 0.1f * noise1(u * 40.f + y * 70.f, 5u));
            if (y > len - 0.03f) k = scaleC(L.cuff, 0.85f);  // cuff band
            if (y > end + 0.0002f) k = scaleC(L.cuff, 0.45f); // the opening (inside of the sleeve)
            return k;
        });
    }
    return b.build(true);
}

// Queues an SDF shape: meshed at `cell`, simplified to `target` triangles.
void queueSdf(MeshJobs& J, Sdf s, float cell, int target, Mesh* out, Mesh* mirror = nullptr) {
    Vector3 lo, hi;
    s.bounds(lo, hi);
    const Vector3 d = Vector3Subtract(hi, lo);
    const float area = 2.f * (d.x * d.y + d.y * d.z + d.z * d.x);
    J.add([s = std::move(s), cell]() { return meshSdf(s, cell); }, target, out, mirror);
    J.jobs.back().cost = kSdfTrisPerArea * area / (cell * cell);  // the surface's cells: what meshing and simplifying cost
}
// Queues a torso: modelled on the worker too (its neckline search marches the SDF), the body and the finer
// collar meshed and merged, then decimated only between the painted edges.
void queueTorso(MeshJobs& J, std::function<TorsoOut()> make, float cell, int target, Mesh* out) {
    J.add([make = std::move(make), cell]() {
        const TorsoOut t = make();
        RawMesh m = meshSdf(t.body, cell);
        if (t.collar.count() > 0) {
            const RawMesh c = meshSdf(t.collar, std::min(cell, kCollarCell));
            const unsigned base = (unsigned)m.p.size();
            m.p.insert(m.p.end(), c.p.begin(), c.p.end());
            m.n.insert(m.n.end(), c.n.begin(), c.n.end());
            m.c.insert(m.c.end(), c.c.begin(), c.c.end());
            for (unsigned i : c.idx) m.idx.push_back(base + i);
        }
        return m;
    }, target, out);
    J.jobs.back().colorEdge = kColorEdge;
    J.jobs.back().cost = kTorsoTrisCell2 / (cell * cell);  // (the body isn't modelled yet: a torso's typical surface)
}

} // namespace

Vector3 handTip(HandPose pose, int finger) {
    Vector3 q0, q[3];
    float r[4];
    fingerChain(handDef(pose), std::clamp(finger, 0, 4), q0, q, r);
    return Vector3Add(q[2], Vector3Scale(vnorm(Vector3Subtract(q[2], q[1])), r[3]));
}

// ============================================================================ looks
PersonLook lookFor(int kind) {
    PersonLook L;
    L.kind = kind;
    switch (kind) {
    case 0:  // Hacı Rıza
        L.skin = rgb(192, 134, 96);
        L.lip = rgb(160, 92, 80);
        L.hair = rgb(150, 146, 140);
        L.browCol = rgb(132, 126, 118);
        L.iris = rgb(92, 60, 34);
        L.sleeve = rgb(212, 202, 180);
        L.cuff = rgb(212, 202, 180);
        L.trousers = rgb(62, 56, 52);
        L.shoes = rgb(40, 30, 24);
        L.headSpec = 0.20f;
        L.shoulderW = 0.188f;
        L.armR = 0.051f;
        break;
    case 1:  // Kel Mahmut
        L.skin = rgb(210, 144, 106);
        L.lip = rgb(170, 90, 80);
        L.hair = rgb(38, 32, 30);
        L.browCol = rgb(30, 25, 23);
        L.iris = rgb(56, 38, 26);
        L.sleeve = rgb(196, 204, 216);
        L.cuff = rgb(196, 204, 216);
        L.bareForearm = true;
        L.trousers = rgb(38, 38, 46);
        L.shoes = rgb(26, 22, 20);
        L.headSpec = 0.55f;
        L.bodyScale = 1.06f;
        L.shoulderW = 0.212f;
        L.upperArm = 0.30f;
        L.foreArm = 0.28f;
        L.armR = 0.060f;
        L.handScale = 1.1f;
        L.spineLen = 0.505f;
        L.shoulderY = 0.375f;
        break;
    case 2:  // Emekli Nuri
        L.skin = rgb(222, 172, 142);
        L.lip = rgb(170, 110, 100);
        L.hair = rgb(236, 234, 228);
        L.browCol = rgb(226, 224, 218);
        L.iris = rgb(88, 106, 122);
        L.sleeve = rgb(118, 40, 38);
        L.cuff = rgb(118, 40, 38);
        L.trousers = rgb(92, 88, 84);
        L.shoes = rgb(52, 40, 32);
        L.headSpec = 0.18f;
        L.shoulderW = 0.176f;
        L.armR = 0.051f;
        L.handScale = 0.97f;
        L.spineLen = 0.49f;
        L.headZ = -0.028f;
        break;
    case 3:  // the çaycı
        L.skin = rgb(204, 148, 108);
        L.lip = rgb(170, 98, 86);
        L.hair = rgb(26, 22, 20);
        L.browCol = rgb(30, 24, 22);
        L.iris = rgb(70, 46, 28);
        L.sleeve = rgb(234, 232, 226);
        L.cuff = rgb(234, 232, 226);
        L.bareForearm = true;
        L.trousers = rgb(30, 30, 34);
        L.shoes = rgb(20, 18, 18);
        L.shoulderW = 0.18f;
        L.armR = 0.047f;
        L.spineLen = 0.50f;
        L.shoulderY = 0.385f;
        L.hipPivot = {0.f, 0.94f, 0.f};
        break;
    default: {  // patrons 10..15
        int v = (kind - 10) % Meshes::PATRON_VARIANTS;
        const Color skins[6] = {rgb(190, 136, 100), rgb(206, 146, 108), rgb(176, 122, 88), rgb(220, 170, 140),
                                rgb(196, 140, 104), rgb(200, 150, 116)};
        const Color hairs[6] = {rgb(140, 136, 130), rgb(40, 34, 30), rgb(28, 24, 22), rgb(230, 228, 222),
                                rgb(160, 156, 150), rgb(120, 116, 110)};
        const Color coats[6] = {rgb(66, 64, 62), rgb(46, 54, 78), rgb(96, 42, 38), rgb(88, 94, 80),
                                rgb(74, 58, 44), rgb(56, 74, 62)};
        const Color shirts[6] = {rgb(200, 196, 184), rgb(210, 210, 214), rgb(190, 180, 160), rgb(184, 200, 214),
                                 rgb(214, 206, 190), rgb(200, 190, 170)};
        L.skin = skins[v];
        L.hair = hairs[v];
        L.browCol = scaleC(hairs[v], 0.8f);
        L.sleeve = coats[v];
        L.cuff = shirts[v];
        L.trousers = rgb(48 + 8 * v, 46 + 5 * v, 44 + 3 * v);
        L.shoes = rgb(34, 28, 24);
        L.shoulderW = 0.182f + 0.006f * (float)(v % 3);
        L.armR = 0.048f;
        break;
    }
    }
    return L;
}

// ============================================================================ build
namespace {

void makePersonMats(PersonMeshes& pm, const PersonLook& L, Renderer& r) {
    pm.cloth = r.makeMat(WHITE, Texture2D{}, 0.05f, 8.f, 0.f, 0.32f);
    pm.skin = r.makeMat(L.skin, Texture2D{}, 0.22f, 18.f, 0.f, 0.22f);
    pm.headMat = r.makeMat(WHITE, Texture2D{}, L.headSpec, L.headSpec > 0.4f ? 42.f : 18.f, 0.f, 0.22f);
    pm.hairMat = r.makeMat(WHITE, Texture2D{}, 0.05f, 10.f, 0.f, 0.3f);
}

void buildOpponent(MeshJobs& J, PersonMeshes& pm, int kind, Renderer& r) {
    PersonLook L = lookFor(kind);
    HeadOut h;
    std::function<TorsoOut()> torso;
    float browThick = 0.0042f, bushy = 0.3f;
    switch (kind) {
    case 0:
        h = buildHeadRiza(L);
        torso = [L]() { return torsoRiza(L); };
        browThick = 0.0040f;
        bushy = 0.5f;
        break;
    case 1:
        h = buildHeadMahmut(L);
        torso = [L]() { return torsoMahmut(L); };
        browThick = 0.0058f;
        bushy = 0.6f;
        break;
    case 2:
        h = buildHeadNuri(L);
        torso = [L]() { return torsoNuri(L); };
        browThick = 0.0050f;
        bushy = 1.4f;
        break;
    default:
        h = buildHeadCayci(L);
        torso = [L]() { return torsoCayci(L); };
        browThick = 0.0038f;
        bushy = 0.2f;
        break;
    }
    pm.face = h.g;
    pm.hasFace = true;
    queueSdf(J, h.s, 0.0031f, kind == 3 ? 9000 : 14000, &pm.head);
    if (h.stache)
        queueSdf(J, mustacheSdf(h.stacheCol, h.thick, h.span, h.droop, h.y, 200u + (uint32_t)kind), 0.0011f, 1800, &pm.stache);
    if (h.stache && kind <= 2) { // Yüz: the regulars' mustache halves (CharactersFace.cpp moves them)
        for (int w = 0; w < 2; ++w)
            queueSdf(J, mustacheSdf(h.stacheCol, h.thick, h.span, h.droop, h.y, 200u + (uint32_t)kind, w == 0 ? 1 : -1),
                     0.0011f, 1100, &pm.stacheWing[w]);
        pm.stachePivot = {0.f, h.y + 0.001f, -0.1025f};
    }
    queueTorso(J, torso, kCellTorso, kind == 1 ? 30000 : 22000, &pm.torso);
    if (kind != 3) queueSdf(J, seatedLegs(L, L.bodyScale), 0.0075f, 4000, &pm.lower);
    queueSdf(J, browSdf(L.browCol, browThick, bushy, 100u + (uint32_t)kind), 0.0011f, 500, &pm.brow[0], &pm.brow[1]);
    Mesh up = buildUpperArm(L, kind == 1);
    pm.upper[0] = up;
    pm.upper[1] = mirrorMeshX(up);
    Mesh fo = buildForeArm(L, true);
    pm.fore[0] = fo;
    pm.fore[1] = mirrorMeshX(fo);
    makePersonMats(pm, L, r);
}

void buildPatron(MeshJobs& J, PersonMeshes& pm, int v, Renderer& r) {
    PersonLook L = lookFor(10 + v);
    HeadOut h = buildHeadPatron(L, v);
    pm.face = h.g;
    pm.hasFace = false;
    queueSdf(J, h.s, 0.0045f, 3500, &pm.head);
    queueTorso(J, [L, v]() { return torsoPatron(L, v); }, kCellPatronTorso, 6000, &pm.torso);
    queueSdf(J, seatedLegs(L, 1.f), 0.0095f, 2500, &pm.lower);
    Mesh up = buildUpperArm(L, false);
    pm.upper[0] = up;
    pm.upper[1] = mirrorMeshX(up);
    Mesh fo = buildForeArm(L, false);
    pm.fore[0] = fo;
    pm.fore[1] = mirrorMeshX(fo);
    makePersonMats(pm, L, r);
}

} // namespace

// (Ocakçı) the tea maker's head, torso and apron, built with the helpers above (CharactersOcakci.cpp uses them)
#include "r3d/CharactersOcakciMesh.inc"

void buildPeople(Meshes& M, Renderer& r, const std::function<void(MeshJobs&)>& more);
void buildPeople(Meshes& M, Renderer& r, const std::function<void(MeshJobs&)>& more) {
    // the SDF shapes are meshed and simplified on all cores, then uploaded here
    MeshJobs J;
    for (int p = 0; p < HAND_POSES; ++p) {
        J.add([p]() { return buildHandMesh((HandPose)p); }, 2600, &M.hand[0][p], &M.hand[1][p], 700, &M.handLo[0][p],
              &M.handLo[1][p]);
        J.jobs.back().cost = kHandTris;
    }
    for (int i = 0; i < 5; ++i) queueSdf(J, lowerLipSdf((float)(i - 2) * 0.5f), 0.0008f, 700, &M.lowerLip[i]);
    buildOpponent(J, M.person[1], 0, r);
    buildOpponent(J, M.person[2], 1, r);
    buildOpponent(J, M.person[3], 2, r);
    buildOpponent(J, M.person[0], 3, r);
    for (int v = 0; v < Meshes::PATRON_VARIANTS; ++v) buildPatron(J, M.patron[v], v, r);
    const PersonLook cay = lookFor(3);
    queueSdf(J, shinSdf(cay, 0.43f), 0.006f, 1600, &M.walkShin[0], &M.walkShin[1]);
    if (more) more(J);  // (Ocakçı) his body in the same batch
    J.run();

    M.eye[0] = buildEye(rgb(92, 60, 34));
    M.eye[1] = buildEye(rgb(56, 38, 26));
    M.eye[2] = buildEye(rgb(88, 110, 126));
    M.lidUpper = buildLid();
    M.mouthCavity = buildMouthCavity();
    M.walkThigh[0] = buildThigh(cay, 0.44f);
    M.walkThigh[1] = mirrorMeshX(M.walkThigh[0]);
    M.eyeMat = r.makeMat(WHITE, Texture2D{}, 0.85f, 110.f, 0.f, 0.05f);
    M.mouthMat = r.makeMat(WHITE, Texture2D{}, 0.15f, 16.f, 0.f, 0.f);
    M.lipMat = r.makeMat(WHITE, Texture2D{}, 0.18f, 24.f, 0.f, 0.15f);
}

void freePerson(PersonMeshes& pm, Renderer& r);
void freePerson(PersonMeshes& pm, Renderer& r) {
    auto U = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    U(pm.lower);
    U(pm.torso);
    U(pm.head);
    U(pm.stache);
    for (int i = 0; i < 2; ++i) {
        U(pm.stacheWing[i]); // (Yüz)
        U(pm.upper[i]);
        U(pm.fore[i]);
        U(pm.brow[i]);
    }
    r.unloadMat(pm.cloth);
    r.unloadMat(pm.skin);
    r.unloadMat(pm.headMat);
    r.unloadMat(pm.hairMat);
}

} // namespace chr
} // namespace r3d
