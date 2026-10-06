// Room module: time of day and season (room owner).
//   * Morning / noon / evening / night (Room::setTimeOfDay): daylight through the windows (point lights, a brighter
//     smoky air, the café curtains glowing), sun shafts and sunlit patches on the floor (morning: through the street
//     windows and the door; noon: short, steep; evening: low and golden through the back window), the street
//     outside by day or by night (RoomTex.cpp), the lamps over the empty background tables switched off by day.
//   * Seasons (Room::setSeason): the soba burns in winter (and on cold autumn / spring nights) with steam from the
//     kettle on top, the fan turns faster in summer and stands still in winter, more mist on the glass in winter,
//     snow falling outside in winter (rain stays a mostly autumn / winter thing, RoomWeather.cpp), leaves drifting
//     past in autumn.
#include "r3d/Daytime.h"
#include "r3d/RoomInternal.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace rm;

namespace {

// Soft snowflakes scattered on a transparent square (several flakes per billboard).
Texture2D genFlakeTexture(uint32_t seed) {
    const int N = 128;
    Image img = GenImageColor(N, N, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    okey::Rng rng(seed);
    for (int k = 0; k < 16; ++k) {
        const float cx = rng.uniform(6.f, N - 6.f), cy = rng.uniform(6.f, N - 6.f), r = rng.uniform(1.8f, 3.4f);
        const float a = rng.uniform(0.55f, 1.f);
        for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); ++y)
            for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); ++x) {
                if (x < 0 || y < 0 || x >= N || y >= N) continue;
                float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                float w = std::clamp(1.f - (d - r * 0.4f) / (r * 0.8f), 0.f, 1.f);
                Color& p = px[y * N + x];
                p.a = (unsigned char)std::clamp(p.a + w * w * a * 255.f, 0.f, 255.f);
            }
    }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// One leaf (white, tinted per billboard): a pointed oval with a darker midrib.
Texture2D genLeafTexture() {
    const int N = 64;
    Image img = GenImageColor(N, N, Color{255, 255, 255, 0});
    Color* px = (Color*)img.data;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N * 2.f - 1.f, v = (y + 0.5f) / N * 2.f - 1.f;
            // rotated 35 degrees
            float ru = u * 0.82f - v * 0.57f, rv = u * 0.57f + v * 0.82f;
            float w = 0.42f * std::pow(std::max(0.f, 1.f - rv * rv), 0.8f);
            float d = std::fabs(ru) - w;
            if (d > 0.03f || std::fabs(rv) > 0.98f) continue;
            float cover = std::clamp(-d / 0.03f, 0.f, 1.f);
            float rib = std::fabs(ru) < 0.035f ? 0.7f : 1.f;
            unsigned char c = (unsigned char)(255.f * rib * (0.85f + 0.15f * rv));
            px[y * N + x] = Color{c, c, c, (unsigned char)(255.f * cover)};
        }
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return t;
}

constexpr float kTargetDay[4] = {0.78f, 1.f, 0.34f, 0.f};   // sabah, öğle, akşam, gece
constexpr float kTargetSun[4] = {1.f, 0.55f, 0.85f, 0.f};
const Color kSunCol[4] = {{255, 226, 182, 255}, {255, 246, 228, 255}, {255, 168, 96, 255}, {150, 170, 220, 255}};

}  // namespace

// ============================================================================ public API
void Room::setTimeOfDay(int mode) {
    Impl& I = *impl_;
    if (mode == I.dayMode) return;
    I.dayMode = mode;
    if (I.ready) I.evalLook(false);
}
void Room::setSeason(int mode) {
    Impl& I = *impl_;
    if (mode == I.seasonMode) return;
    I.seasonMode = mode;
    if (I.ready) I.evalLook(false);
}
int Room::dayPhase() const { return impl_->phase; }
int Room::season() const { return impl_->seasonNow; }
float Room::snowAmount() const { return impl_->ready ? impl_->snow : 0.f; }

// ============================================================================ init / free
void Room::Impl::initDaylight() {
    mSunShaft = R->makeMat(Color{255, 230, 190, 20}, texDust, 0.f, 1.f, 1.f);
    mSunPatch = R->makeMat(Color{255, 230, 190, 60}, Texture2D{}, 0.f, 1.f, 1.f);
    mStoveSlot = R->makeMat(Color{255, 120, 40, 255}, Texture2D{}, 0.f, 1.f, 1.f);
    texFlake = genFlakeTexture(seed + 77);
    texLeaf = genLeafTexture();
    okey::Rng fr(seed * 31u + 5u);
    flakes.resize(300);
    for (Flake& f : flakes) placeFlake(f, fr, true, false);
    leaves.resize(18);
    for (Flake& f : leaves) placeFlake(f, fr, true, true);
    evalLook(true);
    // start in the right light (no fade from night on the first frame)
    updateDaylight(0.f);
}

void Room::Impl::freeDaylight(Renderer& r) {
    for (Mesh* m : {&sunShaft, &sunPatch, &stoveSlot})
        if (m->vertexCount > 0) {
            UnloadMesh(*m);
            *m = Mesh{};
        }
    for (Mat* m : {&mSunShaft, &mSunPatch, &mStoveSlot}) r.unloadMat(*m);
    for (Texture2D* t : {&texFlake, &texLeaf})
        if (t->id) {
            UnloadTexture(*t);
            *t = Texture2D{};
        }
    flakes.clear();
    leaves.clear();
    lookInit = false;
}

// A snowflake board or a leaf somewhere outside: along the left street (seen through both windows and the door) or
// the back street (through the back window).
void Room::Impl::placeFlake(Flake& f, okey::Rng& rng, bool anywhereY, bool leaf) {
    const bool back = rng.chance(0.24f);
    if (back) f.p = {rng.uniform(-5.2f, -1.6f), 0.f, rng.uniform(-6.2f, -3.75f)};
    else f.p = {rng.uniform(-6.4f, -4.45f), 0.f, rng.uniform(-4.6f, 4.4f)};
    f.p.y = anywhereY ? rng.uniform(-0.3f, 3.9f) : rng.uniform(3.6f, 4.2f);
    f.speed = leaf ? rng.uniform(0.45f, 0.85f) : rng.uniform(0.55f, 1.05f);
    f.sway = leaf ? rng.uniform(0.35f, 0.7f) : rng.uniform(0.1f, 0.3f);
    f.phase = rng.uniform(0.f, 6.28f);
    f.size = leaf ? rng.uniform(0.07f, 0.11f) : rng.uniform(0.6f, 0.85f);
    f.kind = rng.range(0, 3);
}

// ============================================================================ the look (phase / season)
void Room::Impl::evalLook(bool force) {
    const int ph = w3d::resolveDayPhase(dayMode), se = w3d::resolveSeason(seasonMode);
    if (!force && ph == phase && se == seasonNow) return;
    phase = ph;
    seasonNow = se;
    // a wet spell: likelier in autumn and winter, and at night (the seed says how wet a day this is)
    static const float pSeason[4] = {0.35f, 0.08f, 0.65f, 0.6f};
    static const float pHour[4] = {0.55f, 0.4f, 0.85f, 1.f};
    const bool wet = rainSeedU < pSeason[se] * pHour[ph];
    rainTarget = (wet && se != w3d::Kis) ? rainSeedI : 0.f;
    // winter: a few flakes always drift past, a real snowfall on a wet day
    snowTarget = se == w3d::Kis ? (wet ? rainSeedI : 0.25f) : 0.f;
    leafK = se == w3d::Sonbahar ? 1.f : 0.f;
}

// Sun shafts and the sunlit patches they leave on the floor, for the current phase.
void Room::Impl::buildSun() {
    for (Mesh* m : {&sunShaft, &sunPatch})
        if (m->vertexCount > 0) {
            UnloadMesh(*m);
            *m = Mesh{};
        }
    if (phase == w3d::Gece) return;
    // direction the light travels (into the room, downward)
    Vector3 s;
    std::vector<int> through;
    if (phase == w3d::Sabah) {
        s = {0.80f, -0.62f, 0.22f};   // low morning sun through the street windows and the open door
        through = {1, 2, 3};
    } else if (phase == w3d::Ogle) {
        s = {0.45f, -0.95f, -0.12f};  // high: short patches under the windows
        through = {1, 2, 3};
    } else {
        s = {0.30f, -0.52f, 0.86f};   // evening: low and golden through the back window
        through = {0};
    }
    s = Vector3Normalize(s);
    MeshBuilder shaft, patch;
    auto toFloor = [&](Vector3 p) {
        float t = (p.y - 0.004f) / -s.y;
        return Vector3Add(p, Vector3Scale(s, t));
    };
    for (int oi : through) {
        const Opening& o = openings[(size_t)oi];
        const bool door = o.kind == 1;
        const float W = o.a1 - o.a0, H = o.y1 - o.y0;
        // panes (fractions of the opening): the lower ones behind the lace café curtain let less through
        struct PaneR {
            float u0, u1, v0, v1, k;
        };
        std::vector<PaneR> panes;
        if (door) {
            panes.push_back({0.04f, 0.96f, 0.05f, 0.92f, 0.5f});  // the bead curtain
        } else {
            const float bw = 0.055f / W, bh = 0.055f / H, tr = 0.72f;
            panes.push_back({bw, 0.5f - bw * 0.35f, bh, 0.5f, 0.42f});
            panes.push_back({0.5f + bw * 0.35f, 1.f - bw, bh, 0.5f, 0.42f});
            panes.push_back({bw, 0.5f - bw * 0.35f, 0.5f, tr - bh * 0.4f, 1.f});
            panes.push_back({0.5f + bw * 0.35f, 1.f - bw, 0.5f, tr - bh * 0.4f, 1.f});
            panes.push_back({bw, 1.f - bw, tr + bh * 0.4f, 1.f - bh, 1.f});
        }
        for (const PaneR& pr : panes) {
            Vector3 w[4] = {wallPoint(o.wall, o.a0 + W * pr.u0, o.y0 + H * pr.v0, -0.12f),
                            wallPoint(o.wall, o.a0 + W * pr.u1, o.y0 + H * pr.v0, -0.12f),
                            wallPoint(o.wall, o.a0 + W * pr.u1, o.y0 + H * pr.v1, -0.12f),
                            wallPoint(o.wall, o.a0 + W * pr.u0, o.y0 + H * pr.v1, -0.12f)};
            Vector3 f[4];
            for (int k = 0; k < 4; ++k) f[k] = toFloor(w[k]);
            const unsigned char aw = (unsigned char)(255.f * pr.k), af = (unsigned char)(70.f * pr.k);
            const Color cw{255, 255, 255, aw}, cf{255, 255, 255, af};
            // the sides of the beam, plus its two diagonals; the dust texture fades them out across (u) and toward the
            // floor (v), so the beam reads as a soft volume instead of four lit edges
            const int sides[6][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}, {1, 3}};
            for (const auto& sd : sides) {
                const int k = sd[0], k2 = sd[1];
                int a = shaft.vertex(w[k], {0, 1, 0}, {0, 0.04f}, cw), b = shaft.vertex(w[k2], {0, 1, 0}, {1, 0.04f}, cw);
                int c = shaft.vertex(f[k2], {0, 1, 0}, {1, 0.75f}, cf), d = shaft.vertex(f[k], {0, 1, 0}, {0, 0.75f}, cf);
                shaft.quad(a, b, c, d);
            }
            const Color cp{255, 255, 255, (unsigned char)(255.f * pr.k)};
            int a = patch.vertex(f[0], {0, 1, 0}, {0, 0}, cp), b = patch.vertex(f[1], {0, 1, 0}, {1, 0}, cp);
            int c = patch.vertex(f[2], {0, 1, 0}, {1, 1}, cp), d = patch.vertex(f[3], {0, 1, 0}, {0, 1}, cp);
            patch.quad(a, b, c, d);
        }
    }
    if (shaft.vertexCount() > 0) sunShaft = shaft.build(true);
    if (patch.vertexCount() > 0) sunPatch = patch.build(true);
}

// ============================================================================ per frame
void Room::Impl::updateDaylight(float dt) {
    lookCheckT -= dt;
    if (lookCheckT <= 0.f) {  // the clock / calendar (otomatik) moves on slowly
        lookCheckT = 20.f;
        evalLook(false);
    }
    if (streetPhase != phase || streetSeason != seasonNow) {
        drawStreetCanvas(cvStreet, seed, phase, seasonNow);
        streetPhase = phase;
        streetSeason = seasonNow;
        buildSun();
        refreshWalkers();
    } else if (!lookInit) {
        buildSun();
        refreshWalkers();
    }
    const int se = seasonNow;
    float tDay = kTargetDay[phase], tSun = kTargetSun[phase], tDusk = phase == w3d::Aksam ? 1.f : 0.f;
    if (se == w3d::Kis) tDay *= 0.88f, tSun *= 0.55f;
    if (se == w3d::Yaz) tSun = std::min(1.f, tSun * 1.15f);
    if (rainTarget > 0.f || snowTarget > 0.5f) tSun *= 0.15f, tDay *= 0.82f;  // overcast
    float tStove = 0.f;
    if (se == w3d::Kis) tStove = 1.f;
    else if (se == w3d::Sonbahar) tStove = phase == w3d::Ogle ? 0.f : 0.75f;
    else if (se == w3d::Ilkbahar) tStove = phase == w3d::Gece ? 0.5f : 0.f;
    const float tFan = se == w3d::Yaz ? 3.6f : se == w3d::Kis ? 0.f : se == w3d::Sonbahar ? 1.4f : 2.3f;
    const float tMist = (se == w3d::Kis ? 1.25f : se == w3d::Sonbahar ? 1.f : se == w3d::Ilkbahar ? 0.7f : 0.35f) * (1.f - 0.6f * tDay);
    // first frame: no fade in; afterwards a few seconds (the clock moving on, a change in Ayarlar)
    const float k = lookInit ? 1.f - std::exp(-dt * 0.9f) : 1.f;
    dayK += (tDay - dayK) * k;
    sunK += (tSun - sunK) * k;
    duskK += (tDusk - duskK) * k;
    stoveLit += (tStove - stoveLit) * (lookInit ? 1.f - std::exp(-dt * 0.3f) : 1.f);
    fanSpeed += (tFan - fanSpeed) * (lookInit ? 1.f - std::exp(-dt * 0.4f) : 1.f);
    condenseK += (tMist - condenseK) * k;
    sunColor = mix(sunColor, kSunCol[phase], lookInit ? k : 1.f);
    snow += (snowTarget - snow) * (lookInit ? 1.f - std::exp(-dt * 0.35f) : 1.f);
    if (snow < 0.004f && snowTarget <= 0.f) snow = 0.f;
    // the lamps over tables nobody sits at are off by day
    for (int t = 0; t < 4 && t + 1 < (int)lamps.size(); ++t) {
        const float want = (phase >= w3d::Aksam || w3d::bgTableBusy(t, phase)) ? 1.f : 0.f;
        Lamp& L = lamps[(size_t)t + 1];
        L.power += (want - L.power) * (lookInit ? std::min(1.f, dt * 3.f) : 1.f);
    }
    lookInit = true;

    mCondense.alpha = std::clamp(condenseK, 0.2f, 1.3f);
    // the street outside is self-lit: by day a little held back, or the windows burn out to white
    {
        const unsigned char v = (unsigned char)(255.f * (1.f - 0.3f * dayK));
        mStreet.material.maps[MATERIAL_MAP_ALBEDO].color = Color{v, v, v, 255};
    }
    mLace.emissive = 0.08f + 0.14f * dayK;
    // the stove: steam from the kettle on top, now and then a breath of warm air shimmering over it
    if (stoveLit > 0.5f && dt > 0.f) {
        kettleAcc += dt * 2.2f * stoveLit;
        while (kettleAcc >= 1.f) {
            kettleAcc -= 1.f;
            SmokeParams p;
            p.velocity = {rng.uniform(-0.01f, 0.01f), rng.uniform(0.08f, 0.13f), rng.uniform(-0.01f, 0.01f)};
            p.size0 = 0.02f;
            p.size1 = rng.uniform(0.12f, 0.18f);
            p.life = rng.uniform(1.4f, 2.2f);
            p.alpha = 0.22f;
            p.color = Color{236, 234, 230, 255};
            p.turbulence = 0.7f;
            p.buoyancy = 0.04f;
            emit(Vector3Add(kettleSpout, {rng.uniform(-0.004f, 0.004f), 0.f, rng.uniform(-0.004f, 0.004f)}), p);
        }
        stoveSmokeAcc += dt * 0.7f * stoveLit;
        while (stoveSmokeAcc >= 1.f) {
            stoveSmokeAcc -= 1.f;
            SmokeParams p;
            p.velocity = {rng.uniform(-0.02f, 0.02f), rng.uniform(0.18f, 0.26f), rng.uniform(-0.02f, 0.02f)};
            p.size0 = 0.12f;
            p.size1 = rng.uniform(0.4f, 0.6f);
            p.life = rng.uniform(2.5f, 3.5f);
            p.alpha = 0.035f;
            p.color = Color{255, 214, 170, 255};
            p.turbulence = 1.2f;
            p.buoyancy = 0.05f;
            emit(Vector3Add(stoveTop, {rng.uniform(-0.08f, 0.08f), 0.03f, rng.uniform(-0.08f, 0.08f)}), p);
        }
    }
    // snow and leaves outside
    if (snow > 0.01f)
        for (Flake& f : flakes) {
            f.p.y -= f.speed * dt;
            f.p.x += std::sin(time * 0.8f + f.phase) * f.sway * dt;
            f.p.z += std::cos(time * 0.6f + f.phase * 1.3f) * f.sway * 0.6f * dt;
            if (f.p.y < -0.4f) placeFlake(f, rng, false, false);
        }
    if (leafK > 0.01f)
        for (Flake& f : leaves) {
            f.p.y -= f.speed * dt * (0.7f + 0.3f * std::sin(time * 2.1f + f.phase));
            f.p.x += std::sin(time * 1.3f + f.phase) * f.sway * dt;
            f.p.z += (0.25f + std::cos(time * 0.9f + f.phase) * f.sway) * dt;
            f.phase += dt * 3.f;
            if (f.p.y < -0.2f) placeFlake(f, rng, false, true);
        }
}

void Room::Impl::submitDaylight(Renderer& r) {
    // the soba's door slot: glowing embers while it burns, cold iron otherwise
    {
        const float g = std::clamp(stoveLevel, 0.f, 1.2f);
        mStoveSlot.emissive = std::clamp(stoveLit * 1.2f, 0.f, 1.f);
        mStoveSlot.material.maps[MATERIAL_MAP_ALBEDO].color =
            mix(Color{34, 30, 28, 255}, Color{255, (unsigned char)std::clamp(100.f + 60.f * g, 0.f, 255.f), 40, 255}, std::min(1.f, stoveLit * 1.5f));
        r.submit(&stoveSlot, &mStoveSlot, MatrixIdentity(), 0);
    }
    // sun shafts and patches
    if (sunK * dayK > 0.01f && sunShaft.vertexCount > 0) {
        const float s = sunK * std::min(1.f, dayK * 1.3f);
        Color c = sunColor;
        c.a = (unsigned char)std::clamp(24.f * s, 0.f, 255.f);
        mSunShaft.material.maps[MATERIAL_MAP_ALBEDO].color = c;
        r.submit(&sunShaft, &mSunShaft, MatrixIdentity(), Transparent | Additive | DoubleSided | NoFog);
        c.a = (unsigned char)std::clamp(95.f * s, 0.f, 255.f);
        mSunPatch.material.maps[MATERIAL_MAP_ALBEDO].color = c;
        r.submit(&sunPatch, &mSunPatch, MatrixIdentity(), Transparent | Additive | NoFog);
    }
    // snowfall outside: soft flakes, brighter where the street lamps catch them at night
    if (snow > 0.01f) {
        const int n = std::min((int)flakes.size(), (int)(flakes.size() * std::min(1.f, 0.3f + snow)));
        const unsigned char a = (unsigned char)std::clamp(255.f * std::min(1.f, 0.5f + snow), 0.f, 255.f);
        // at night the flakes only show where light catches them (additive), by day white against the street
        const bool add = dayK < 0.45f;
        const Color tint = add ? Color{150, 162, 196, a} : Color{250, 252, 255, a};
        const float half = (float)texFlake.width * 0.5f;
        for (int i = 0; i < n; ++i) {
            const Flake& f = flakes[(size_t)i];
            Rectangle src{(f.kind & 1) ? half : 0.f, (f.kind & 2) ? half : 0.f, half, half};
            Color c = tint;
            if (add) {  // brighter near the street lamps
                const float dx = f.p.x - streetLampGlow.x, dz = f.p.z - streetLampGlow.z;
                const float k = 0.6f + 1.2f * std::exp(-(dx * dx + dz * dz) / 6.f);
                c = Color{(unsigned char)std::min(255.f, tint.r * k), (unsigned char)std::min(255.f, tint.g * k),
                          (unsigned char)std::min(255.f, tint.b * k), a};
            }
            r.submitBillboard(texFlake, src, f.p, {f.size, f.size}, c, add);
        }
    }
    if (leafK > 0.01f) {
        static const Color kLeaf[4] = {{196, 110, 40, 255}, {170, 70, 34, 255}, {206, 156, 60, 255}, {130, 88, 44, 255}};
        const Rectangle src{0.f, 0.f, (float)texLeaf.width, (float)texLeaf.height};
        for (const Flake& f : leaves) {
            Color c = scaleRgb(kLeaf[f.kind & 3], 0.45f + 0.55f * dayK);
            float tumble = 0.25f + 0.75f * std::fabs(std::cos(f.phase));
            r.submitBillboard(texLeaf, src, f.p, {f.size * tumble, f.size}, c, false);
        }
    }
}

}  // namespace r3d
