// The kıraathane interior (room owner): public API, per-frame animation (lamps, TV, fan, clock, dice, chairs,
// passing cars, bead curtain), lighting & atmosphere (key light, point lights, fog/haze, smoke) and submission.
#include "r3d/RoomInternal.h"
#include "r3d/Daytime.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace r3d {

using namespace rm;

namespace {
constexpr float CY = w3d::CEILING_Y;

Matrix translate(Vector3 p) { return MatrixTranslate(p.x, p.y, p.z); }
float ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
// Orientation that puts die face `f` (1..6) on top.
Matrix faceUp(int f) {
    switch (f) {
    case 6: return MatrixRotateX(PI);
    case 3: return MatrixRotateZ(PI * 0.5f);
    case 4: return MatrixRotateZ(-PI * 0.5f);
    case 2: return MatrixRotateX(-PI * 0.5f);
    case 5: return MatrixRotateX(PI * 0.5f);
    default: return MatrixIdentity();
    }
}
}  // namespace

Room::Room() : impl_(new Impl) {}
Room::~Room() { delete impl_; }

// ============================================================================ init / shutdown
bool Room::init(Renderer& r, uint64_t seed) {
    Impl& I = *impl_;
    if (I.ready) return true;
    I.R = &r;
    I.seed = (uint32_t)(seed ^ (seed >> 32)) | 1u;
    I.rng.reseed(seed * 2654435761ull + 17);

    I.planWalls();
    I.texFloor = genFloorTexture(I.seed);
    I.texWall = genWallAtlas(I.seed, I.openings, I.marks);
    I.texCeil = genCeilingTexture(I.seed + 9);
    I.texWood = genBoardTexture(512, Color{214, 176, 134, 255}, Color{150, 108, 72, 255}, I.seed + 1);
    I.texWoodDark = genBoardTexture(256, Color{176, 138, 100, 255}, Color{110, 80, 56, 255}, I.seed + 2);
    I.texFelt = genFeltTexture(256, Color{40, 100, 64, 255}, I.seed + 3);
    I.texCondense = genCondensationTexture(I.seed + 4);
    I.texLace = genLaceTexture(I.seed + 8);
    I.texDust = genDustTexture(I.seed + 5);
    I.texVarnish = genVarnishTexture(I.seed + 6);
    I.texMarble = genNoiseTexture(256, Color{236, 234, 228, 255}, Color{176, 174, 170, 255}, 5.f, I.seed + 7);

    I.cvArt = makeCanvas(ART_W, ART_H);
    drawArtAtlas(I.cvArt, I.seed);
    I.cvStreet = makeCanvas(STREET_W, STREET_H);
    I.phase = w3d::resolveDayPhase(I.dayMode);
    I.seasonNow = w3d::resolveSeason(I.seasonMode);
    drawStreetCanvas(I.cvStreet, I.seed, I.phase, I.seasonNow);
    I.streetPhase = I.phase;
    I.streetSeason = I.seasonNow;
    I.cvScore = makeCanvas(1024, 696);
    I.cvTv = makeCanvas(320, 240);
    I.cvLetter = makeCanvas(1024, 256);
    drawLetteringCanvas(I.cvLetter);

    I.mFloor = r.makeMat(WHITE, I.texFloor, 0.22f, 34.f);
    I.mWall = r.makeMat(WHITE, I.texWall, 0.04f, 8.f);
    I.mCeil = r.makeMat(WHITE, I.texCeil, 0.02f, 6.f);
    I.mWood = r.makeMat(WHITE, I.texWood, 0.3f, 36.f);
    I.mWoodDark = r.makeMat(WHITE, I.texWoodDark, 0.12f, 16.f);
    I.mVarnish = r.makeMat(WHITE, I.texVarnish, 0.42f, 44.f);
    I.mPaint = r.makeMat(WHITE, Texture2D{}, 0.25f, 28.f);
    I.mCloth = r.makeMat(WHITE, Texture2D{}, 0.03f, 6.f, 0.f, 0.3f);
    I.mMetal = r.makeMat(WHITE, Texture2D{}, 0.85f, 90.f, 0.f, 0.15f);
    I.mBrass = r.makeMat(WHITE, Texture2D{}, 0.8f, 60.f, 0.f, 0.1f);
    I.mCeramic = r.makeMat(WHITE, Texture2D{}, 0.55f, 80.f);
    I.mFelt = r.makeMat(WHITE, I.texFelt, 0.03f, 6.f, 0.f, 0.2f);
    I.mMarble = r.makeMat(WHITE, I.texMarble, 0.6f, 70.f);
    I.mShadeIn = r.makeMat(Color{255, 244, 222, 255}, Texture2D{}, 0.2f, 10.f, 0.6f);
    I.mBulb = r.makeMat(Color{255, 236, 196, 255}, Texture2D{}, 0.f, 8.f, 1.f);
    I.mGlass = r.makeMat(Color{226, 236, 240, 64}, Texture2D{}, 0.9f, 140.f, 0.f, 0.6f);
    I.mBottle = r.makeMat(WHITE, Texture2D{}, 0.8f, 100.f, 0.f, 0.4f);
    I.mTea = r.makeMat(Color{150, 50, 16, 255}, Texture2D{}, 0.7f, 90.f, 0.12f);
    I.mArt = r.makeMat(WHITE, I.cvArt.texture, 0.12f, 20.f);
    I.mArtGloss = r.makeMat(WHITE, I.cvArt.texture, 0.7f, 90.f);
    // the price board hangs outside the picture light: its chalk glows a little so the prices read from our seat
    I.mArtChalk = r.makeMat(WHITE, I.cvArt.texture, 0.04f, 8.f, 0.2f);
    I.mLeaf = r.makeMat(WHITE, Texture2D{}, 0.45f, 38.f, 0.f, 0.25f);  // waxy rubber-plant leaves (vertex colours)
    I.mScore = r.makeMat(WHITE, I.cvScore.texture, 0.04f, 8.f, 0.1f);  // chalk catches every bit of light
    I.mTv = r.makeMat(WHITE, I.cvTv.texture, 0.3f, 60.f, 1.f);
    I.mStreet = r.makeMat(WHITE, I.cvStreet.texture, 0.f, 1.f, 1.f);
    I.mCondense = r.makeMat(WHITE, I.texCondense, 0.9f, 120.f, 0.f, 0.3f);
    I.mLace = r.makeMat(Color{222, 226, 234, 255}, I.texLace, 0.04f, 6.f, 0.08f, 0.35f);  // cool: the street light behind it
    I.mLetter = r.makeMat(WHITE, I.cvLetter.texture, 0.6f, 60.f, 0.15f);
    I.mSweep = r.makeMat(Color{200, 214, 255, 50}, r.glowTexture(), 0.f, 1.f, 1.f);
    I.mEmber = r.makeMat(Color{255, 120, 40, 255}, Texture2D{}, 0.f, 1.f, 1.f);

    I.buildAll();
    I.initWeather();
    I.initCat();
    I.initDaylight();
    I.initSpecial();  // (ozelgun) özel günler, the passing shower
    I.garden = I.resolveGarden();
    if (I.garden) I.initGarden();
    for (Impl::Lamp& L : I.lamps) L.dust = r.makeMat(Color{255, 212, 158, 26}, I.texDust, 0.f, 1.f, 1.f);
    {
        MeshBuilder q;
        int a = q.vertex({-0.5f, 0, -0.5f}, {0, -1, 0}, {0, 0}), b = q.vertex({0.5f, 0, -0.5f}, {0, -1, 0}, {1, 0});
        int c = q.vertex({0.5f, 0, 0.5f}, {0, -1, 0}, {1, 1}), d = q.vertex({-0.5f, 0, 0.5f}, {0, -1, 0}, {0, 1});
        q.quad(a, b, c, d);
        I.sweepQuad = q.build();
    }
    // dice resting on the tavla board
    for (int k = 0; k < 2; ++k) {
        Impl::Die& d = I.dice[k];
        d.to = {I.tavlaCenter.x + 0.08f + k * 0.045f, I.tavlaCenter.y + 0.0085f + 0.0075f, I.tavlaCenter.z + 0.04f - k * 0.05f};
        d.face = 1 + I.rng.range(0, 5);
        d.yaw = I.rng.uniform(0.f, 90.f);
        d.from = d.to;
    }
    I.diceAnim = 1.f;
    I.diceT = I.rng.uniform(4.f, 9.f);
    I.chairT = I.rng.uniform(25.f, 50.f);
    I.glassT = I.rng.uniform(40.f, 80.f);
    I.carT = I.rng.uniform(6.f, 14.f);

    I.tv.reset(seed);
    drawTvCanvas(I.cvTv, I.tv, 0.f);
    drawScoreboardCanvas(I.cvScore, I.scoreTitle, I.scoreLines, I.seed);
    I.scoreDirty = false;
    {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_r(&now, &lt);
        I.lastDay = lt.tm_yday;
    }
    if (!I.garden) I.warmUpSmoke();
    I.ready = true;
    return true;
}

void Room::Impl::freeAll(Renderer& r) {
    freeSpecial(r);  // (ozelgun)
    freeGarden(r);
    freeDaylight(r);
    freeWeather(r);
    freeCat(r);
    auto um = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    for (Static& s : statics) um(s.mesh);
    statics.clear();
    for (Mesh* m : {&shadeOut, &shadeIn, &bulbMesh, &cordMesh, &chairMesh, &fanBlades, &handHour, &handMin, &handSec,
                    &dieMesh, &sweepQuad, &ashGlass, &ashInside, &ashCig, &ashEmber, &curtainMesh[0], &curtainMesh[1], &curtainMesh[2]})
        um(*m);
    for (Mesh& m : dustMeshes) um(m);
    dustMeshes.clear();
    for (GlassSet& g : glassSets) um(g.glass);
    glassSets.clear();
    for (Pane& p : panes) um(p.mesh);
    panes.clear();
    for (Lamp& L : lamps) r.unloadMat(L.dust);
    lamps.clear();
    for (Mat* m : {&mFloor, &mWall, &mCeil, &mWood, &mWoodDark, &mVarnish, &mPaint, &mCloth, &mMetal, &mBrass, &mCeramic, &mFelt, &mMarble,
                   &mShadeIn, &mBulb, &mGlass, &mBottle, &mTea, &mArt, &mArtGloss, &mArtChalk, &mLeaf, &mScore, &mTv, &mStreet, &mCondense, &mLace, &mLetter,
                   &mSweep, &mEmber})
        r.unloadMat(*m);
    for (Texture2D* t : {&texFloor, &texWall, &texCeil, &texWood, &texWoodDark, &texFelt, &texCondense, &texLace, &texDust, &texVarnish, &texMarble})
        if (t->id) {
            unloadTexture(*t);
            *t = Texture2D{};
        }
    for (RenderTexture2D* c : {&cvArt, &cvStreet, &cvScore, &cvTv, &cvLetter})
        if (c->id) {
            unloadCanvas(*c);
            *c = RenderTexture2D{};
        }
    chairs.clear();
    motes.clear();
    emitters.clear();
    pending.clear();
}

void Room::shutdown(Renderer& r) {
    Impl& I = *impl_;
    if (!I.ready) return;
    I.freeAll(r);
    I.ready = false;
}

// ============================================================================ public API
void Room::setScoreboard(const std::string& title, const std::vector<std::string>& lines) {
    Impl& I = *impl_;
    if (title == I.scoreTitle && lines == I.scoreLines) return;
    I.scoreTitle = title;
    I.scoreLines = lines;
    I.scoreDirty = true;
}
void Room::setTitleMode(bool on) { impl_->title = on; }
void Room::setTavlaFocus(bool on) { impl_->tavlaFocus = on; }
void Room::setFloorPeople(const std::vector<Vector3>& people) { impl_->floorPeople = people; } // (duzelt: the cat)
bool Room::consumeCatMeow(Vector3& where) {
    if (!impl_->catMeowed) return false;
    impl_->catMeowed = false;
    where = impl_->catMeowAt;
    return true;
}

bool Room::consumeTvGoal() {
    bool g = impl_->tvGoal;
    impl_->tvGoal = false;
    return g;
}

void Room::update(float dt) {
    Impl& I = *impl_;
    if (!I.ready) return;
    dt = std::clamp(dt, 0.f, 0.1f);
    I.time += dt;
    I.updateDaylight(dt);
    I.updateLamps(dt);
    I.updateProps(dt);
    I.updateSmoke(dt);
    I.updateWeather(dt);
    I.updateCat(dt);
    I.updateSpecial(dt);  // (ozelgun)
    I.venueCheckT -= dt;
    if (I.venueCheckT <= 0.f) {  // otomatik: the clock, the calendar and the weather move on
        I.venueCheckT = 5.f;
        I.evalVenue();
    }
    if (I.garden) I.updateGarden(dt);
    if (I.scoreDirty) {
        drawScoreboardCanvas(I.cvScore, I.scoreTitle, I.scoreLines, I.seed + (uint32_t)I.scoreLines.size() * 31u);
        I.scoreDirty = false;
    }
    for (ui::Sfx s : I.sfxQueue)
        if (playSfx) playSfx(s);
    I.sfxQueue.clear();
}

void Room::submit(Renderer& r) {
    Impl& I = *impl_;
    if (!I.ready) return;
    for (const Impl::Puff& p : I.pending) r.emitSmoke(p.pos, p.p);
    I.pending.clear();
    I.submitSpecial(r);  // (ozelgun) the day's decorations, inside or in the garden
    if (I.garden) {
        I.submitGarden(r);
        return;
    }
    I.submitLights(r);
    for (const Impl::Static& s : I.statics)
        if (s.venues & 1u) r.submit(&s.mesh, s.mat, s.xf, s.flags);
    I.submitLamps(r);
    I.submitProps(r);
    I.submitWeather(r);
    I.submitDaylight(r);
    I.submitCat(r);
}

// ============================================================================ animation
void Room::Impl::sfx(ui::Sfx s) { sfxQueue.push_back(s); }

void Room::Impl::updateLamps(float dt) {
    for (size_t i = 0; i < lamps.size(); ++i) {
        Lamp& L = lamps[i];
        float n = noise1(time * 5.3f + L.phase, 17u + (uint32_t)i);
        float target = (L.key ? 0.99f + 0.01f * n : 0.92f + 0.08f * n) * L.power;
        if (L.dip > 0.f) {
            L.dip -= dt;
            if (L.faulty > 0.f) target *= noise1(time * 55.f, 5u) > 0.45f ? 0.3f : 0.95f;
            else target *= L.key ? 0.93f : 0.74f + 0.14f * noise1(time * 38.f, 9u);
        } else if (rng.chance(dt * (L.key ? 0.006f : (L.faulty > 0.f ? 0.06f : 0.025f)))) {
            L.dip = rng.uniform(0.06f, L.faulty > 0.f ? 0.8f : 0.3f);
        }
        L.level += (target - L.level) * std::min(1.f, dt * 28.f);
        const float amp = L.swingAmp * (1.f + 0.6f * sweepLevel);
        float sx = std::sin(time * 0.83f + L.phase) * amp + std::sin(time * 2.1f + L.phase * 1.3f) * amp * 0.25f;
        float sz = std::cos(time * 0.71f + L.phase * 0.7f) * amp * 0.8f;
        Matrix R = MatrixMultiply(MatrixRotateX(sz * DEG2RAD), MatrixRotateZ(sx * DEG2RAD));
        L.xf = MatrixMultiply(MatrixMultiply(MatrixTranslate(-L.anchor.x, -L.anchor.y, -L.anchor.z), R), translate(L.anchor));
        L.cur = Vector3Transform(L.bulb, L.xf);
    }
    // motes: slow brownian drift, kept inside their lamp's cone
    for (Mote& m : motes) {
        const Lamp& L = lamps[m.lamp];
        m.v.x += rng.uniform(-1.f, 1.f) * 0.02f * dt;
        m.v.y += rng.uniform(-1.f, 1.f) * 0.015f * dt;
        m.v.z += rng.uniform(-1.f, 1.f) * 0.02f * dt;
        m.v = Vector3Scale(m.v, 1.f - std::min(0.5f, dt * 0.4f));
        m.p = Vector3Add(m.p, Vector3Scale(m.v, dt));
        float s = L.small ? 0.7f : 1.f;
        float yTop = L.cur.y - 0.08f * s, yBot = L.key ? 1.5f : L.cur.y - 0.8f;
        float depth = yTop - m.p.y;
        float rad = 0.18f * s + std::max(depth, 0.f) * 1.1f;
        float dx = m.p.x - L.cur.x, dz = m.p.z - L.cur.z;
        if (m.p.y > yTop || m.p.y < yBot || dx * dx + dz * dz > rad * rad) {
            m.p = {L.cur.x + rng.uniform(-0.2f, 0.2f), rng.uniform(yBot + 0.05f, yTop - 0.05f), L.cur.z + rng.uniform(-0.2f, 0.2f)};
            m.v = {rng.uniform(-0.01f, 0.01f), rng.uniform(-0.004f, 0.006f), rng.uniform(-0.01f, 0.01f)};
        }
        m.tw += dt;
    }
}

void Room::Impl::updateProps(float dt) {
    const float sfxRate = title ? 0.5f : 1.f;
    fanAngle = std::fmod(fanAngle + dt * fanSpeed, 2.f * PI);
    // --- TV match
    tv.step(dt);
    if (tv.goalEvent) {
        tv.goalEvent = false;
        tvGoal = !garden || specialTvOut();  // (the TV is inside; ozelgun: on a derby night one is out in the garden)
    }
    tvRedraw -= dt;
    // (only where a TV shows it: inside, or the garden's portable one on a derby night; the canvas is pure drawing)
    if (tvRedraw <= 0.f && (!garden || specialTvOut())) {
        drawTvCanvas(cvTv, tv, time);
        tvRedraw = 1.f / 24.f;
    }
    tvLight += (tv.brightness * tv.flicker - tvLight) * std::min(1.f, dt * 20.f);
    // --- stove fire flicker, window light
    stoveLevel = (0.8f + 0.2f * noise1(time * 3.1f, 41u) + 0.08f * noise1(time * 11.f, 43u)) * stoveLit;
    windowLevel = 0.95f + 0.05f * noise1(time * 0.3f, 47u);
    emberHeat = 0.75f + 0.25f * noise1(time * 1.7f, 51u) + 0.1f * noise1(time * 9.f, 53u);
    // --- tavla dice: every now and then somebody throws
    diceT -= dt * sfxRate;
    if (diceT <= 0.f && diceAnim >= 1.f) {
        diceSide = 1 - diceSide;  // players at the +Z / -Z sides take turns
        const float sgn = diceSide == 0 ? 1.f : -1.f;
        for (int k = 0; k < 2; ++k) {
            Die& d = dice[k];
            d.from = {tavlaCenter.x + sgn * rng.uniform(0.02f, 0.08f), tavlaCenter.y + 0.09f, tavlaCenter.z + sgn * 0.2f};
            d.to = {tavlaCenter.x + sgn * rng.uniform(0.07f, 0.2f), tavlaCenter.y + 0.0085f + 0.0075f,
                    tavlaCenter.z + sgn * rng.uniform(-0.12f, 0.1f) + (k ? 0.045f : 0.f)};
            d.axis = Vector3Normalize({rng.uniform(-1.f, 1.f), rng.uniform(-0.3f, 0.3f), rng.uniform(-1.f, 1.f)});
            d.spin = rng.uniform(9.f, 16.f);
            d.face = 1 + rng.range(0, 5);
            d.yaw = rng.uniform(0.f, 90.f);
        }
        diceAnim = 0.f;
        diceT = rng.uniform(7.f, 16.f);
        sfx(ui::Sfx::Dice);
    }
    if (diceAnim < 1.f) diceAnim = std::min(1.f, diceAnim + dt / 0.9f);
    // --- an empty chair gets pulled / pushed with a scrape
    chairT -= dt * sfxRate;
    if (chairT <= 0.f) {
        std::vector<int> empty;
        for (int i = 0; i < (int)chairs.size(); ++i)
            if (!chairs[i].occupied && !catNearChair(i)) empty.push_back(i);  // not from under (or into) the cat
        if (!empty.empty()) {
            Chair& c = chairs[empty[rng.range(0, (int)empty.size() - 1)]];
            c.offFrom = c.off;
            c.yawFrom = c.yawOff;
            Vector3 dir = Vector3Normalize(Vector3Subtract(c.pos, Vector3{0, 0, 0}));
            float push = rng.uniform(-0.07f, 0.07f);
            c.offTo = {std::clamp(dir.x * push + rng.uniform(-0.03f, 0.03f), -0.08f, 0.08f), 0.f,
                       std::clamp(dir.z * push + rng.uniform(-0.03f, 0.03f), -0.08f, 0.08f)};
            c.yawTo = rng.uniform(-8.f, 8.f);
            c.t = 0.f;
            sfx(ui::Sfx::Chair);
        }
        chairT = rng.uniform(30.f, 75.f);
    }
    for (Chair& c : chairs)
        if (c.t < 1.f) {
            c.t = std::min(1.f, c.t + dt / 0.55f);
            float e = ease(c.t);
            c.off = Vector3Lerp(c.offFrom, c.offTo, e);
            c.yawOff = c.yawFrom + (c.yawTo - c.yawFrom) * e;
        }
    // --- the çaycı sets a fresh glass at the counter: steam burst + clink
    glassT -= dt * sfxRate;
    if (glassT <= 0.f) {
        steamBurst = 1.2f;
        sfx(ui::Sfx::GlassSet);
        glassT = rng.uniform(55.f, 110.f);
    }
    steamBurst = std::max(0.f, steamBurst - dt);
    // --- passing car on the street: headlights through the windows, a sweep across the ceiling
    if (carActive <= 0.f) {
        carT -= dt;
        if (carT <= 0.f) {
            carActive = 1.f;
            carDir = rng.chance(0.5f) ? 1.f : -1.f;
            carZ = carDir > 0 ? -12.f : 10.f;
            carT = rng.uniform(16.f, 40.f);
            carSoundDone = false;
        }
        sweepLevel = 0.f;
    } else {
        carZ += carDir * 7.5f * dt;
        if (carZ > 10.5f || carZ < -12.5f) carActive = 0.f;
        // the tyres' hiss peaks ~1.3 s into the sound: start it so that it peaks as the car passes our seat
        if (!carSoundDone && std::fabs(carZ - 0.8f) < 7.5f * 1.3f) {
            carSoundDone = true;
            sfx(ui::Sfx::CarPass);
        }
        float s = 0.f;
        for (float zw : {-1.7f, 0.8f, 2.8f}) s = std::max(s, std::exp(-(carZ - zw) * (carZ - zw) / 2.2f));
        sweepLevel = s;
        sweepLightPos = {-3.95f, 1.5f, std::clamp(carZ, -2.6f, 3.2f)};
    }
    // --- clock second hand ticks
    {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_r(&now, &lt);
        if (lt.tm_sec != lastSec) {
            lastSec = lt.tm_sec;
            secTick = 0.f;
        }
        secTick = std::min(1.f, secTick + dt / 0.12f);
        if (lt.tm_yday != lastDay) {  // the calendar turns over at midnight
            lastDay = lt.tm_yday;
            drawArtAtlas(cvArt, seed);
        }
    }
}

// Ambient smoke: puffs hang under the ceiling, gather in the lamp cones and rise from the smokers' tables.
static SmokeParams ambientPuff(okey::Rng& rng, int kind, float time) {
    SmokeParams p;
    Vector3 draught{0.025f * std::sin(time * 0.05f), 0.f, 0.018f * std::cos(time * 0.037f)};
    p.color = Color{200, 194, 184, 255};
    if (kind == 0) {
        p.velocity = {draught.x + rng.uniform(-0.015f, 0.015f), rng.uniform(-0.004f, 0.006f), draught.z + rng.uniform(-0.015f, 0.015f)};
        p.size0 = rng.uniform(0.5f, 0.9f);
        p.size1 = rng.uniform(1.3f, 2.1f);
        p.life = rng.uniform(16.f, 26.f);
        p.alpha = rng.uniform(0.09f, 0.15f);
        p.turbulence = 0.5f;
        p.buoyancy = 0.f;
    } else if (kind == 1) {
        p.velocity = {rng.uniform(-0.02f, 0.02f), rng.uniform(0.0f, 0.02f), rng.uniform(-0.02f, 0.02f)};
        p.size0 = rng.uniform(0.25f, 0.4f);
        p.size1 = rng.uniform(0.8f, 1.2f);
        p.life = rng.uniform(10.f, 15.f);
        p.alpha = rng.uniform(0.1f, 0.17f);
        p.turbulence = 0.7f;
        p.buoyancy = 0.004f;
    } else {
        p.velocity = {rng.uniform(-0.02f, 0.02f), rng.uniform(0.02f, 0.05f), rng.uniform(-0.02f, 0.02f)};
        p.size0 = rng.uniform(0.15f, 0.25f);
        p.size1 = rng.uniform(0.6f, 0.9f);
        p.life = rng.uniform(8.f, 12.f);
        p.alpha = rng.uniform(0.08f, 0.13f);
        p.turbulence = 0.9f;
        p.buoyancy = 0.01f;
    }
    return p;
}

static Vector3 ambientPos(okey::Rng& rng, int kind, const std::vector<Vector3>& lampPos);

void Room::Impl::updateSmoke(float dt) {
    for (Emitter& e : emitters) {
        float rate = e.rate * (e.kind == 0 ? 1.f + 4.f * steamBurst : 1.f);
        e.acc += rate * dt;
        while (e.acc >= 1.f) {
            e.acc -= 1.f;
            SmokeParams p;
            if (e.kind == 0) {  // steam from the çaydanlık
                p.velocity = {rng.uniform(-0.012f, 0.012f), rng.uniform(0.1f, 0.16f), rng.uniform(-0.012f, 0.012f)};
                p.size0 = 0.025f;
                p.size1 = rng.uniform(0.14f, 0.22f);
                p.life = rng.uniform(1.6f, 2.6f);
                p.alpha = 0.26f;
                p.color = Color{236, 234, 230, 255};
                p.turbulence = 0.7f;
                p.buoyancy = 0.04f;
            } else {  // a smouldering cigarette: thin bluish thread that curls as it rises
                p.velocity = {rng.uniform(-0.004f, 0.004f), rng.uniform(0.06f, 0.085f), rng.uniform(-0.004f, 0.004f)};
                p.size0 = 0.006f;
                p.size1 = rng.uniform(0.06f, 0.1f);
                p.life = rng.uniform(3.2f, 4.4f);
                p.alpha = 0.3f;
                p.color = Color{196, 202, 214, 255};
                p.turbulence = 0.45f;
                p.buoyancy = 0.03f;
            }
            emit(Vector3Add(e.pos, Vector3{rng.uniform(-0.003f, 0.003f), 0.f, rng.uniform(-0.003f, 0.003f)}), p);
        }
    }
    ambientAcc += dt * (garden ? 0.8f : (title ? 7.f : 5.5f));
    while (ambientAcc >= 1.f) {
        ambientAcc -= 1.f;
        float r = rng.uniform();
        int kind = garden ? 2 : (r < 0.55f ? 0 : (r < 0.82f ? 1 : 2));  // out in the garden the air carries it away
        Vector3 p = ambientPos(rng, kind, bgLampPos());
        SmokeParams sp = ambientPuff(rng, kind, time);
        // the fan stirs the smoke under the ceiling
        Vector3 d = Vector3Subtract(p, fanPos);
        float fd = std::sqrt(d.x * d.x + d.z * d.z);
        if (kind == 0 && fd < 1.8f && fd > 0.05f) {
            float k = 0.04f * (1.f - fd / 1.8f);
            sp.velocity.x += -d.z / fd * k;
            sp.velocity.z += d.x / fd * k;
        }
        emit(p, sp);
    }
}

static Vector3 ambientPos(okey::Rng& rng, int kind, const std::vector<Vector3>& lampPos) {
    for (int tries = 0; tries < 8; ++tries) {
        Vector3 p;
        if (kind == 0) {
            p = {rng.uniform(w3d::ROOM_X0 + 0.3f, w3d::ROOM_X1 - 0.3f), rng.uniform(2.05f, 2.9f), rng.uniform(w3d::ROOM_Z0 + 0.3f, w3d::ROOM_Z1 - 0.3f)};
        } else if (kind == 1) {
            const Vector3& L = lampPos[rng.range(0, (int)lampPos.size() - 1)];
            p = {L.x + rng.uniform(-0.4f, 0.4f), L.y - rng.uniform(0.05f, 0.55f), L.z + rng.uniform(-0.4f, 0.4f)};
        } else {
            const w3d::BgTable& t = w3d::BG_TABLES[rng.range(0, 3)];
            p = {t.x + rng.uniform(-0.5f, 0.5f), rng.uniform(1.25f, 1.65f), t.z + rng.uniform(-0.5f, 0.5f)};
        }
        // keep the air right over our table clear
        if (std::fabs(p.x) < 1.0f && std::fabs(p.z) < 1.0f && p.y < 1.75f) continue;
        return p;
    }
    return {2.f, 2.6f, 2.f};
}

void Room::Impl::warmUpSmoke() {
    // start with the room already smoky: full-size puffs that fade in over a second or two
    for (int i = 0; i < 90; ++i) {
        float r = rng.uniform();
        int kind = r < 0.6f ? 0 : (r < 0.85f ? 1 : 2);
        SmokeParams sp = ambientPuff(rng, kind, 0.f);
        float s = sp.size0 + (sp.size1 - sp.size0) * rng.uniform(0.3f, 0.9f);
        sp.size0 = s;
        sp.size1 = s * 1.15f;
        sp.life *= rng.uniform(0.35f, 1.f);
        emit(ambientPos(rng, kind, bgLampPos()), sp);
    }
}

// ============================================================================ submission
void Room::Impl::submitLights(Renderer& r) {
    // Night: very little bounce light, so the lamps make pools and the corners fall into brown shadow. By day the
    // windows fill the room with a soft, cooler light (golden in the evening) and the smoke glows with it.
    const float D = dayK;
    Ambient a;
    a.sky = mix(Color{92, 74, 58, 255}, mix(Color{176, 170, 158, 255}, Color{200, 150, 110, 255}, duskK), D);
    a.ground = mix(Color{40, 30, 24, 255}, Color{92, 78, 62, 255}, D);
    a.intensity = (title ? 0.34f : 0.2f) + 0.3f * D;
    r.setAmbient(a);
    // Dumanaltı: thin smoky air everywhere, a thick layer under the ceiling that starts above head height.
    // The colour is the radiance of *unlit* smoke (the renderer scales it by its FOG_AMBIENT), so it is kept
    // dark: the haze glows where the lamps light it (in-scatter) and the corners fall off to near-black.
    Fog f;
    f.color = mix(Color{50, 44, 38, 255}, mix(Color{118, 116, 110, 255}, Color{130, 104, 84, 255}, duskK), D);
    f.density = 0.05f;
    f.hazeStart = 1.95f;
    f.hazeTop = CY;
    f.hazeDensity = 0.5f - 0.12f * D;
    r.setFog(f);

    const int keyIdx = keyLamp();
    const Lamp& K = lamps[(size_t)keyIdx];
    KeyLight k;
    k.position = K.cur;
    k.target = {K.cur.x, w3d::TABLE_Y, K.cur.z};
    k.color = Color{255, 204, 148, 255};
    k.intensity = 3.3f * K.level;
    k.innerDeg = 34.f;
    k.outerDeg = 64.f;
    k.range = 6.f;
    k.shadows = true;
    r.setKeyLight(k);

    // the room's budget is 9 point lights (10 while tavla is played: our table has no istaka fill then); a lamp
    // switched off (an empty table by day) or the cold stove leave their slot to the daylight from the other windows
    int n = 0;
    const int budget = tavlaFocus ? 10 : 9;
    auto add = [&](const PointLight& p) {
        if (n >= budget || p.intensity <= 0.01f) return;
        r.addPointLight(p);
        ++n;
    };
    // Pendants: the light source sits a little below the shade mouth, so the enamel shade "blocks" most of
    // the light that would otherwise wash the ceiling.
    for (size_t i = 0; i < lamps.size(); ++i) {
        if ((int)i == keyIdx) continue;
        const Lamp& L = lamps[i];
        float s = L.small ? 0.7f : 1.f;
        // (our okey table's lamp, not the key light now, still lights the regulars sitting there)
        add({Vector3Add(L.cur, Vector3{0, -0.26f * s, 0}), L.color, L.intensity * L.level * (i == 0 ? 1.6f : 1.f), L.range});
    }
    add({tvFront, Color{150, 176, 255, 255}, (0.2f + 0.8f * tvLight) * (1.f - 0.5f * D), 3.2f});
    // cool street light through the left windows (daylight by day); a passing car's headlights drag it along the wall
    // (by day the light sits up and into the room: the haze glows with it instead of a blob on the glass)
    Vector3 wp = Vector3Lerp(Vector3{-3.8f, 1.85f, -0.45f}, Vector3{-2.7f, 2.45f, -0.6f}, std::min(1.f, D * 2.f));
    const float sweep = sweepLevel * (1.f - D);
    if (sweep > 0.01f) wp = Vector3Lerp(wp, sweepLightPos, std::min(1.f, sweep * 1.5f));
    const Color winCol = mix(Color{116, 146, 210, 255}, sunColor, std::min(1.f, D * 1.6f));
    add({wp, winCol, 1.0f * windowLevel + 1.1f * sweep + 1.5f * D, 3.6f + 2.4f * D});
    if (stoveLit > 0.02f) add({stoveGlowPos, Color{255, 128, 56, 255}, 0.75f * stoveLevel * (1.f + 0.35f * (1.f - D)), 2.3f + 0.6f * stoveLit});
    // the picture light keeps the chalked scores readable from our table
    add({boardLightPos, Color{255, 200, 140, 255}, 0.42f * lamps[1].level, 1.7f});
    if (D > 0.02f) {
        add({{-2.7f, 2.45f, 0.9f}, winCol, 1.4f * D, 5.5f});     // the second street window
        add({{-3.2f, 2.4f, -2.4f}, winCol, 1.1f * D, 4.5f});     // the back window
        add({{-3.0f, 2.3f, 2.75f}, winCol, 0.8f * D, 3.8f});     // the open street door
    }
}

void Room::Impl::submitLamps(Renderer& r) {
    const Vector3 cam = r.lastCamera().position;
    for (size_t i = 0; i < lamps.size(); ++i) {
        Lamp& L = lamps[i];
        const float s = L.small ? 0.7f : 1.f;
        Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), translate(L.bulb)), L.xf);
        r.submit(&shadeOut, &mCeramic, M, 0);  // never in the shadow map: the key light sits inside its shade
        const bool lit = L.power > 0.3f;       // switched off by day over an empty table
        r.submit(&shadeIn, lit ? &mShadeIn : &mCeramic, M, 0);
        if (lit) r.submit(&bulbMesh, &mBulb, M, 0);
        float top = L.bulb.y + 0.165f * s;
        // (in the garden the counter's lamp hangs from the ocak's little roof, the others from the vine trellis)
        const float cordTop = garden && L.small ? 2.42f : CY;
        Matrix C = MatrixMultiply(MatrixMultiply(MatrixScale(1.f, cordTop - top, 1.f), MatrixTranslate(L.bulb.x, top, L.bulb.z)), L.xf);
        r.submit(&cordMesh, &mPaint, C, 0);
        if (L.level < 0.02f) continue;
        const bool key = (int)i == keyLamp();
        r.submitGlow(Vector3Add(L.cur, Vector3{0, -0.03f * s, 0}), (key ? 0.2f : 0.17f) * s, Color{255, 214, 150, 255}, 0.6f * L.level);
        r.submitGlow(Vector3Add(L.cur, Vector3{0, -0.09f * s, 0}), (key ? 0.55f : 0.45f) * s, Color{255, 190, 120, 255}, 0.12f * L.level);
        // light shaft facing the camera about the vertical axis
        Vector3 rim = Vector3Transform(Vector3Add(L.bulb, Vector3{0, -0.075f * s, 0}), L.xf);
        float yaw = std::atan2(cam.x - rim.x, cam.z - rim.z);
        // (out in the garden the air is clear: only a faint cone at night)
        const float airK = garden ? 0.4f * (1.f - dayK) : 1.f;
        L.dust.material.maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)std::clamp(26.f * L.level * (key ? 0.8f : 1.f) * airK, 0.f, 255.f);
        if (airK > 0.02f)
            r.submit(&dustMeshes[i], &L.dust, MatrixMultiply(MatrixRotateY(yaw), translate(rim)), Transparent | Additive | DoubleSided | NoFog);
    }
    for (const Mote& m : motes) {
        if (garden) break;  // (no dust motes in the open air)
        const Lamp& L = lamps[m.lamp];
        float tw = 0.5f + 0.5f * std::sin(m.tw * 2.3f + m.p.x * 40.f);
        r.submitGlow(m.p, 0.0045f, Color{255, 226, 180, 255}, (0.25f + 0.45f * tw) * L.level);
    }
}

void Room::Impl::submitProps(Renderer& r) {
    const bool in = !garden;  // the room's own things; the garden has its own chairs (RoomGarden.cpp)
    // chairs
    if (in)
        for (const Chair& c : chairs)
            r.submit(&chairMesh, &mVarnish, MatrixMultiply(MatrixRotateY((c.yaw + c.yawOff) * DEG2RAD), translate(Vector3Add(c.pos, c.off))), CastShadow);
    // ceiling fan
    if (in) r.submit(&fanBlades, &mVarnish, MatrixMultiply(MatrixRotateY(fanAngle), translate(fanPos)), 0);
    // clock hands (real local time)
    if (in) {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_r(&now, &lt);
        float sec = (float)lt.tm_sec - 1.f + ease(secTick);
        float mins = lt.tm_min + std::max(sec, 0.f) / 60.f;
        float hrs = (lt.tm_hour % 12) + mins / 60.f;
        auto hand = [&](const Mesh& m, float frac, float dz) {
            Matrix M = MatrixMultiply(MatrixRotateZ(-frac * 2.f * PI), translate(Vector3Add(clockPos, Vector3{0, 0, dz})));
            r.submit(&m, &mPaint, M, 0);
        };
        hand(handHour, hrs / 12.f, 0.f);
        hand(handMin, mins / 60.f, 0.004f);
        hand(handSec, sec / 60.f, 0.008f);
    }
    // tavla dice
    for (int k = 0; k < 2; ++k) {
        const Die& d = dice[k];
        float t = diceAnim;
        Vector3 p = Vector3Lerp(d.from, d.to, ease(std::min(1.f, t * 1.15f)));
        p.y = d.to.y + (d.from.y - d.to.y) * (1.f - t) * std::fabs(std::cos(t * PI * 2.6f)) * (1.f - t);
        Matrix rest = MatrixMultiply(faceUp(d.face), MatrixRotateY(d.yaw * DEG2RAD));
        float spinAng = d.spin * (1.f - t) * (1.f - t);
        Matrix M = MatrixMultiply(MatrixMultiply(rest, MatrixRotate(d.axis, spinAng)), translate(p));
        r.submit(&dieMesh, &mArtGloss, M, CastShadow);
    }
    // our ashtray: contents, cigarette, ember glow, glass body last (transparent)
    {
        Matrix A = translate(w3d::ASHTRAY_POS);
        r.submit(&ashInside, &mPaint, A, CastShadow);
        r.submit(&ashCig, &mPaint, A, CastShadow);
        mEmber.emissive = 1.f;
        mEmber.material.maps[MATERIAL_MAP_ALBEDO].color =
            Color{255, (unsigned char)std::clamp(90.f + 60.f * emberHeat, 0.f, 255.f), (unsigned char)std::clamp(20.f + 30.f * emberHeat, 0.f, 255.f), 255};
        r.submit(&ashEmber, &mEmber, A, 0);
        r.submit(&ashGlass, &mGlass, A, Transparent | DoubleSided);
        // the tavla table's ashtray: a few butts, nothing burning
        const Matrix TA = translate(w3d::tavlaToWorld(w3d::TAVLA_ASHTRAY_LOCAL));
        r.submit(&ashInside, &mPaint, TA, CastShadow);
        r.submit(&ashGlass, &mGlass, TA, Transparent | DoubleSided);
        r.submitGlow(emberPos, 0.011f, Color{255, 130, 50, 255}, 0.9f * emberHeat);
        r.submitGlow(emberPos, 0.035f, Color{255, 110, 40, 255}, 0.22f * emberHeat);
    }
    // tea glasses on the background tables and the counter
    for (const GlassSet& g : glassSets) r.submit(&g.glass, &mGlass, translate(g.at), Transparent | DoubleSided);
    // window panes (condensation) and the painted lettering
    if (in)
        for (const Pane& p : panes) r.submit(&p.mesh, p.mat, translate(p.at), Transparent | DoubleSided);
    // bead curtain swaying in the draught
    for (int g = 0; g < (in ? 3 : 0); ++g) {
        float a = std::sin(time * (0.8f + 0.13f * g) + g * 1.7f) * 1.1f + std::sin(time * 2.3f + g) * 0.3f + sweepLevel * 2.f * std::sin(time * 3.f + g);
        float b = std::sin(time * 0.6f + g * 2.1f) * 0.8f;
        Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateZ(a * DEG2RAD), MatrixRotateX(b * DEG2RAD)), translate(curtainPivot));
        r.submit(&curtainMesh[g], &mVarnish, M, 0);
    }
    // headlight sweep across the ceiling
    if (in && sweepLevel * (1.f - dayK) > 0.01f) {
        float zc = 0.f, best = -1.f;
        for (float zw : {-1.7f, 0.8f, 2.8f}) {
            float s = std::exp(-(carZ - zw) * (carZ - zw) / 2.2f);
            if (s > best) {
                best = s;
                zc = zw - (carZ - zw) * 1.3f;
            }
        }
        mSweep.material.maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)std::clamp(70.f * sweepLevel * (1.f - dayK), 0.f, 255.f);
        Matrix M = MatrixMultiply(MatrixScale(2.2f, 1.f, 0.9f), MatrixTranslate(-2.3f, CY - 0.012f, std::clamp(zc, -3.f, 3.2f)));
        r.submit(&sweepQuad, &mSweep, M, Transparent | Additive | DoubleSided | NoFog);
    }

    // --- glows: TV, stove, gas flame, radio dial, street lamps, neighbours' windows, car lights
    {
        Vector3 sc = Vector3Subtract(tvFront, Vector3Scale(tvNormal, 0.25f));
        if (in) r.submitGlow(sc, 0.42f, Color{140, 170, 255, 255}, 0.16f * tvLight);
        if (in && stoveLit > 0.02f) {
            r.submitGlow(stoveGlowPos, 0.12f + 0.05f * stoveLit, Color{255, 120, 40, 255}, 0.55f * stoveLevel);
            r.submitGlow(Vector3Add(stoveGlowPos, Vector3{0, -0.2f, 0}), 0.35f + 0.15f * stoveLit, Color{255, 110, 40, 255}, 0.14f * stoveLevel);
        }
        float fl = 0.8f + 0.2f * noise1(time * 13.f, 61u);
        r.submitGlow(flamePos, 0.07f, Color{90, 150, 255, 255}, 0.7f * fl);
        r.submitGlow(Vector3Add(flamePos, Vector3{0.3f, 0, 0}), 0.05f, Color{90, 150, 255, 255}, 0.35f * fl);
        r.submitGlow(radioDialPos, 0.05f, Color{255, 190, 110, 255}, 0.35f);
        r.submitGlow(boardGlowPos, 0.09f, Color{255, 214, 160, 255}, 0.28f * lamps[1].level);
        const float night = 1.f - std::min(1.f, dayK * 1.4f);  // street lamps and lit windows: evening and night
        if (night > 0.01f) {
        r.submitGlow(streetLampGlow, 0.9f, Color{255, 170, 90, 255}, 0.55f * night);
        r.submitGlow(backLampGlow, 0.7f, Color{255, 176, 96, 255}, 0.5f * night);
        for (size_t i = 0; i < neighbourWindows.size(); ++i) {
            float f = i == 0 ? 0.5f + 0.5f * noise1(time * 6.f, 71u) : 0.8f;
            Color c = i == 0 ? Color{120, 160, 255, 255} : Color{255, 190, 110, 255};
            r.submitGlow(neighbourWindows[i], 0.5f, c, 0.18f * f * night);
        }
        }
        if (in && carActive > 0.f && dayK < 0.6f) {  // (behind the garden's wall the lane's cars stay hidden)
            for (int s = -1; s <= 1; s += 2) {
                r.submitGlow({-6.2f + s * 0.65f, 0.62f, carZ}, 0.28f, Color{255, 246, 220, 255}, 0.9f);
                r.submitGlow({-6.2f + s * 0.65f, 0.62f, carZ + carDir * 0.2f}, 1.1f, Color{255, 240, 210, 255}, 0.18f);
                r.submitGlow({-6.2f + s * 0.6f, 0.7f, carZ - carDir * 4.1f}, 0.12f, Color{255, 40, 30, 255}, 0.8f);
            }
        }
    }
}

}  // namespace r3d
