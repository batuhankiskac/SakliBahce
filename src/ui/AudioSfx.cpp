// Audio (audio owner): the one-shot effects, synthesised at init into a few variations each (AudioInternal.h).
#include "ui/AudioInternal.h"

namespace ui {
namespace audio_detail {

// ------------------------------------------------------------------------------------------------
// Sound designs
// ------------------------------------------------------------------------------------------------

// A thick melamine okey tile, and the wooden istaka / felt-covered table it lands on.
const Mode kTileModes[5] = {{1650, .010f, .85f}, {2450, .0075f, .65f}, {3300, .006f, .45f},
                            {4400, .0045f, .3f}, {5600, .0035f, .2f}};
const Mode kTableModes[4] = {{150, .026f, .9f}, {235, .021f, .75f}, {380, .016f, .6f}, {540, .012f, .45f}};

Buf sfxTileClick(Synth& s) {
    Buf b = s.buf(0.1f);
    Mode tile[4] = {{3050, .011f, 1.f}, {4580, .0075f, .7f}, {6300, .0055f, .45f}, {8150, .004f, .3f}};
    Mode rack[4] = {{540, .016f, .5f}, {910, .012f, .45f}, {1450, .009f, .32f}, {2200, .006f, .2f}};
    s.jitter(tile, 4, .05f, .3f);
    s.jitter(rack, 4, .08f, .3f);
    const float fs = s.u(.9f, 1.12f), c = s.u(.12f, .2f), t0 = .001f;
    s.impact(b, t0, tile, 4, c, 1.f, .3f, fs);
    s.impact(b, t0, rack, 4, c * 1.6f, 1.f);
    s.noiseBurst(b, t0, .1f, 1.2f, 2000.f, 9000.f, .1f);
    if (s.chance(.55f)) { // tiny bounce
        const float t1 = t0 + s.u(.006f, .014f), g = s.u(.18f, .38f);
        s.impact(b, t1, tile, 4, c, g, .3f, fs * s.u(.98f, 1.02f));
        s.impact(b, t1, rack, 4, c * 1.6f, g * .8f);
    }
    return b;
}

Buf sfxTileDraw(Synth& s) {
    const float slide = s.u(.12f, .19f);
    Buf b = s.buf(slide + .12f);
    Mode tile[4] = {{3050, .011f, 1.f}, {4580, .0075f, .7f}, {6300, .0055f, .45f}, {8150, .004f, .3f}};
    Mode rack[4] = {{540, .016f, .5f}, {910, .012f, .45f}, {1450, .009f, .32f}, {2200, .006f, .2f}};
    s.jitter(tile, 4, .05f, .3f);
    s.jitter(rack, 4, .08f, .3f);
    const float fs = s.u(.92f, 1.1f);
    s.impact(b, .001f, tile, 4, .15f, .22f, .3f, fs * 1.05f); // lifted off the pile
    // slide: friction noise, band-passed, with stick-slip grain
    Noise nz(s.seed32());
    Biquad bp, bp2;
    OnePole rough;
    rough.lp(s.sr(), 70.f);
    const float fc0 = s.u(1500.f, 2100.f), fc1 = fc0 * s.u(1.2f, 1.5f);
    const int st = s.at(.012f), n = s.at(slide);
    bp2.highpass(s.sr(), 500.f);
    for (int i = 0; i < n; ++i) {
        const float p = (float)i / (float)n;
        if ((i & 31) == 0) bp.bandpass(s.sr(), fc0 + (fc1 - fc0) * p, 0.9f);
        float env = std::min(p / .25f, 1.f) * std::min((1.f - p) / .3f, 1.f);
        env = env * env * (3.f - 2.f * env);
        const float grain = .5f + .5f * std::min(1.f, std::fabs(rough.process(nz.next() * 3.f)) * 2.f);
        b[(size_t)(st + i)] += bp.process(bp2.process(nz.next())) * env * grain * .3f;
    }
    const float tEnd = .012f + slide - .008f;
    const float c = s.u(.14f, .2f);
    s.impact(b, tEnd, tile, 4, c, 1.f, .3f, fs);
    s.impact(b, tEnd, rack, 4, c * 1.6f, .9f);
    s.noiseBurst(b, tEnd, .1f, 1.f, 2000.f, 9000.f, .08f);
    return b;
}

void tileOnFelt(Synth& s, Buf& b, float t0, float gain, float fs, float contact) {
    Mode tile[5], body[4];
    std::copy(kTileModes, kTileModes + 5, tile);
    std::copy(kTableModes, kTableModes + 4, body);
    s.jitter(tile, 5, .04f, .3f);
    s.jitter(body, 4, .06f, .3f);
    s.impact(b, t0, tile, 5, contact, gain * 1.2f, .3f, fs);
    s.impact(b, t0, body, 4, contact * 3.f, gain * .5f);
    s.noiseBurst(b, t0, .15f, 1.5f, 700.f, 5000.f, .14f * gain);
    // a flat tile lands on one edge, then the other
    const float t1 = t0 + s.u(.0015f, .0045f), g = gain * s.u(.35f, .7f);
    s.impact(b, t1, tile, 5, contact, g * 1.2f, .3f, fs * s.u(.99f, 1.01f));
    s.impact(b, t1, body, 4, contact * 3.f, g * .3f);
}

Buf sfxTileDiscard(Synth& s) {
    Buf b = s.buf(.2f);
    tileOnFelt(s, b, .001f, 1.f, s.u(.92f, 1.1f), s.u(.22f, .34f));
    if (s.chance(.3f)) {
        Mode tile[5];
        std::copy(kTileModes, kTileModes + 5, tile);
        s.impact(b, s.u(.016f, .03f), tile, 5, .2f, .08f, .3f, s.u(1.f, 1.2f));
    }
    return b;
}

Buf sfxTileSlam(Synth& s) {
    Buf b = s.buf(.8f);
    const int n = s.ir(3, 5);
    float t = .002f;
    for (int i = 0; i < n; ++i) {
        tileOnFelt(s, b, t, s.u(.4f, .7f), s.u(.9f, 1.12f), s.u(.2f, .3f));
        t += s.u(.045f, .085f) * (1.f - .08f * (float)i);
    }
    // the palm comes down on the row: "şak!"
    t += s.u(.01f, .03f);
    Mode palm[4] = {{110, .05f, .8f}, {170, .04f, .8f}, {260, .03f, .65f}, {410, .022f, .45f}};
    s.jitter(palm, 4, .06f, .2f);
    s.impact(b, t, palm, 4, s.u(3.f, 4.5f), 1.1f, .2f);
    s.noiseBurst(b, t, .5f, 14.f, 600.f, 3500.f, .7f);
    tileOnFelt(s, b, t + .001f, .75f, s.u(.9f, 1.1f), .3f);
    Mode jostle[3] = {{3400, .004f, 1.f}, {5100, .003f, .6f}, {7200, .002f, .4f}};
    const int m = s.ir(3, 6);
    for (int i = 0; i < m; ++i)
        s.impact(b, t + s.u(.008f, .09f), jostle, 3, .08f, s.u(.05f, .16f), .3f, s.u(.85f, 1.2f));
    return b;
}

Buf sfxShuffle(Synth& s) {
    const float D = s.u(1.15f, 1.3f);
    Buf b = s.buf(D + .15f);
    Mode tt[3] = {{3300, .005f, 1.f}, {4900, .0035f, .6f}, {6900, .0025f, .4f}}; // tile against tile
    Mode tf[3] = {{1700, .006f, .8f}, {2600, .005f, .5f}, {380, .012f, .5f}};    // tile on felt
    const float swirlHz = s.u(1.8f, 2.6f), ph = s.u(0.f, kTauF);
    auto dens = [&](float t) {
        float e = std::min(t / .12f, 1.f) * std::min((D - t) / .25f, 1.f);
        e = std::max(e, 0.f);
        return e * (.55f + .45f * std::sin(kTauF * swirlHz * t + ph));
    };
    float t = 0.f;
    const float maxRate = 115.f;
    for (;;) {
        t += -std::log(1.f - s.u(0.f, .999f)) / maxRate;
        if (t >= D) break;
        if (s.u(0.f, 1.f) > dens(t)) continue;
        const float g = std::pow(s.u(.1f, 1.f), 1.6f) * .6f;
        if (s.chance(.7f))
            s.impact(b, t, tt, 3, s.u(.06f, .14f), g, .4f, s.u(.8f, 1.25f));
        else
            s.impact(b, t, tf, 3, s.u(.15f, .3f), g * .8f, .3f, s.u(.85f, 1.15f));
    }
    Biquad hp, lp;
    hp.highpass(s.sr(), 300.f);
    lp.lowpass(s.sr(), 2200.f);
    Noise nz(s.seed32());
    const int n = s.at(D);
    for (int i = 0; i < n; ++i)
        b[(size_t)i] += lp.process(hp.process(nz.next())) * dens((float)i / s.sr()) * .09f;
    return b;
}

// Metal teaspoon against a thin tulip-shaped tea glass (ince belli), with tea in it.
void glassClinks(Synth& s, Buf& b, float t, int n, float f0, float gain) {
    static const float kRatio[4] = {1.f, 2.63f, 4.84f, 7.4f};
    static const float kDecay[4] = {.16f, .08f, .05f, .03f};
    static const float kAmp[4] = {1.f, .7f, .5f, .35f};
    Mode spoon[3] = {{s.u(4800, 5600), .02f, .3f}, {s.u(7800, 8800), .012f, .2f}, {s.u(10500, 12000), .008f, .12f}};
    for (int i = 0; i < n; ++i) {
        Mode gl[8];
        for (int k = 0; k < 4; ++k) {
            const float a = kAmp[k] * s.u(.6f, 1.4f);
            gl[2 * k] = {f0 * kRatio[k], kDecay[k], a * .6f};
            gl[2 * k + 1] = {f0 * kRatio[k] * s.u(1.003f, 1.008f), kDecay[k] * .9f, a * .5f};
        }
        const float g = gain * (i == 0 ? 1.f : s.u(.45f, .95f));
        s.impact(b, t, gl, 8, s.u(.03f, .06f), g, .2f, s.u(.997f, 1.003f));
        s.impact(b, t, spoon, 3, .04f, g * .7f);
        t += s.u(.075f, .13f);
    }
}

Buf sfxTeaClink(Synth& s) {
    const int n = s.ir(3, 5);
    Buf b = s.buf(.13f * (float)n + .6f);
    glassClinks(s, b, .002f, n, s.u(1850.f, 2700.f), 1.f);
    // the tea swirling
    Noise nz(s.seed32());
    Biquad lp;
    lp.lowpass(s.sr(), 650.f);
    const int L = s.at(.12f * (float)n + .2f);
    for (int i = 0; i < L; ++i) {
        const float p = (float)i / (float)L;
        b[(size_t)i] += lp.process(nz.next()) * std::sin((float)kPi * p) * .02f;
    }
    return b;
}

// A sip of hot tea: air drawn over the surface plus a spray of tiny bubbles (damped sines that
// chirp upwards, as bubbles near a surface do), then a soft exhale.
Buf sfxTeaSip(Synth& s) {
    const float D = s.u(.3f, .45f);
    Buf b = s.buf(D + .45f);
    Noise nz(s.seed32());
    auto env = [&](float t) {
        const float p = t / D;
        if (p <= 0.f || p >= 1.f) return 0.f;
        const float e = std::min(p / .15f, 1.f) * std::min((1.f - p) / .3f, 1.f);
        return e * e * (3.f - 2.f * e) * (1.f - .25f * p);
    };
    Biquad bp, lp;
    lp.lowpass(s.sr(), 3000.f, .7f);
    const float fa = s.u(1200.f, 1500.f), fb = fa * s.u(1.3f, 1.6f);
    const int n = s.at(D);
    for (int i = 0; i < n; ++i) {
        const float p = (float)i / (float)n;
        if ((i & 31) == 0) bp.bandpass(s.sr(), fa + (fb - fa) * p, 1.2f);
        b[(size_t)i] += lp.process(bp.process(nz.next())) * env((float)i / s.sr()) * .12f;
    }
    const int bubbles = s.ir(30, 48);
    for (int k = 0; k < bubbles; ++k) {
        const float t = s.u(.02f, D - .02f);
        const float a = std::pow(s.u(.2f, 1.f), 2.f) * env(t);
        const float f0 = s.u(650.f, 1900.f), tau = s.u(.004f, .012f), rise = s.u(.1f, .45f);
        const int st = s.at(t), L = s.at(tau * 6.f);
        double ph = 0.0;
        for (int i = 0; i < L && st + i < (int)b.size(); ++i) {
            const float tt = (float)i / s.sr();
            ph += kTau * f0 * (1.f + rise * tt / tau) / s.sr();
            const float e = std::min(tt / .0008f, 1.f) * std::exp(-tt / tau);
            b[(size_t)(st + i)] += (float)std::sin(ph) * e * a * .5f;
        }
    }
    // soft exhale
    Biquad lp2;
    lp2.lowpass(s.sr(), 1300.f);
    const int st = n + s.at(.05f), L = s.at(.25f);
    for (int i = 0; i < L && st + i < (int)b.size(); ++i) {
        const float p = (float)i / (float)L;
        b[(size_t)(st + i)] += lp2.process(nz.next()) * std::sin((float)kPi * p) * .035f;
    }
    Biquad out;
    out.lowpass(s.sr(), 5500.f, .6f);
    filterBuf(b, out);
    return b;
}

Buf sfxGlassSet(Synth& s) {
    Buf b = s.buf(.45f);
    Mode saucer[3] = {{1350, .06f, .8f}, {3150, .035f, .6f}, {5200, .02f, .35f}};
    Mode glass[2] = {{2300, .03f, .5f}, {5100, .015f, .3f}};
    Mode table[2] = {{180, .03f, .6f}, {320, .02f, .4f}};
    s.jitter(saucer, 3, .08f, .3f);
    s.jitter(glass, 2, .1f, .3f);
    const float t0 = .001f;
    s.impact(b, t0, saucer, 3, .1f, 1.f);
    s.impact(b, t0, glass, 2, .1f, .8f);
    s.impact(b, t0, table, 2, 2.f, 1.2f);
    float t = t0;
    const int k = s.ir(1, 3);
    for (int i = 0; i < k; ++i) { // the glass settles on the saucer
        t += s.u(.02f, .05f);
        const float g = s.u(.12f, .3f);
        s.impact(b, t, saucer, 3, .1f, g);
        s.impact(b, t, glass, 2, .1f, g * .8f);
    }
    if (s.chance(.5f)) {
        Mode spoon[2] = {{s.u(4600, 5000), .015f, 1.f}, {s.u(7300, 7900), .01f, .6f}};
        s.impact(b, t0 + .004f, spoon, 2, .05f, .15f);
    }
    return b;
}

// Tongue click ("cık") of disapproval.
void tongueClick(Synth& s, Buf& b, float t, float gain) {
    Mode m[4] = {{1900, .005f, 1.f}, {3000, .004f, .6f}, {4300, .003f, .35f}, {900, .006f, .3f}};
    s.jitter(m, 4, .05f, .2f);
    s.impact(b, t, m, 4, .05f, gain);
    s.noiseBurst(b, t, .1f, 3.f, 2000.f, 5000.f, .35f * gain);
}

Buf sfxPenalty(Synth& s) {
    Buf b = s.buf(.9f);
    tongueClick(s, b, .01f, .8f);
    tongueClick(s, b, .01f + s.u(.15f, .18f), .7f);
    // knuckles on the table, and a low comic "bwomp"
    const float t = .37f;
    Mode knock[4] = {{160, .06f, 1.f}, {270, .045f, .7f}, {430, .03f, .5f}, {720, .02f, .3f}};
    s.impact(b, t, knock, 4, 1.5f, 2.2f);
    const int L = s.at(.32f), st = s.at(t);
    if ((int)b.size() < st + L) b.resize((size_t)(st + L), 0.f);
    double ph = 0.0;
    for (int i = 0; i < L; ++i) {
        const float tt = (float)i / s.sr();
        const float f = 95.f + 60.f * std::exp(-tt / .09f);
        ph += kTau * f / s.sr();
        const float env = std::min(tt / .008f, 1.f) * std::exp(-tt / .1f);
        b[(size_t)(st + i)] += softClip(2.2f * (float)std::sin(ph)) * env * .35f;
    }
    return b;
}

// Plucked bağlama-like course (two detuned strings) rendered offline.
struct PluckSpec {
    float t;         // start time (s)
    float freq;      // Hz
    float hold;      // seconds before the note is damped
    float amp;       // stroke strength
    int tremolo;     // extra strokes
    float tremDt;    // spacing of the extra strokes
    float bendCents; // pitch bend over the note (negative = down)
};

void renderPluck(Synth& s, Buf& out, const PluckSpec& p, float t60 = 1.6f) {
    KsString a, c;
    a.init(s.sr());
    c.init(s.sr());
    a.setLoss(.24f);
    c.setLoss(.28f);
    a.setT60(t60);
    c.setT60(t60);
    a.setFreq(p.freq * centsToRatio(-2.5f));
    c.setFreq(p.freq * centsToRatio(2.5f));
    Noise nz(s.seed32());
    const int start = s.at(p.t), hold = s.at(p.hold), tail = s.at(.45f);
    const int total = hold + tail;
    if ((int)out.size() < start + total) out.resize((size_t)(start + total), 0.f);
    int stroke = 0;
    const int strokes = 1 + p.tremolo;
    for (int i = 0; i < total; ++i) {
        const float tt = (float)i / s.sr();
        while (stroke < strokes && tt >= (float)stroke * p.tremDt) {
            const float amp = stroke == 0 ? p.amp : p.amp * ((stroke & 1) ? .55f : .45f);
            a.pluck(amp, stroke == 0 ? .8f : .6f, .13f, nz);
            c.pluck(amp * .8f, stroke == 0 ? .7f : .5f, .17f, nz);
            ++stroke;
        }
        if (p.bendCents != 0.f && (i & 15) == 0 && i < hold) {
            const float q = (float)i / (float)std::max(1, hold);
            const float bend = p.bendCents * q * q;
            a.setFreq(p.freq * centsToRatio(-2.5f + bend));
            c.setFreq(p.freq * centsToRatio(2.5f + bend));
        }
        if (i == hold) {
            a.setT60(.09f);
            c.setT60(.09f);
        }
        out[(size_t)(start + i)] += (a.tick() + c.tick()) * .5f;
    }
}

void bodyColour(Synth& s, Buf& b) {
    Biquad hp, p1, p2, p3, lp;
    hp.highpass(s.sr(), 90.f);
    p1.peaking(s.sr(), 230.f, 1.2f, 3.f);
    p2.peaking(s.sr(), 1050.f, 1.4f, 3.5f);
    p3.peaking(s.sr(), 2900.f, 1.5f, 2.f);
    lp.lowpass(s.sr(), 7000.f, .6f);
    for (float& x : b) x = lp.process(p3.process(p2.process(p1.process(hp.process(x)))));
}

// Frame drum / darbuka strokes (used by Win and pre-rendered for the radio).
Buf percDum(Synth& s, float gain) {
    Buf b = s.buf(.35f);
    double ph = 0.0;
    const float f0 = s.u(140.f, 160.f), f1 = s.u(78.f, 90.f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = (float)i / s.sr();
        const float f = f1 + (f0 - f1) * std::exp(-t / .03f);
        ph += kTau * f / s.sr();
        const float env = std::min(t / .002f, 1.f) * std::exp(-t / .11f);
        b[i] += (float)(std::sin(ph) + .25 * std::sin(1.59 * ph) * std::exp(-t / .03f)) * env * gain;
    }
    s.noiseBurst(b, 0.f, .3f, 12.f, 60.f, 500.f, .35f * gain);
    return b;
}

Buf percTek(Synth& s, float gain, float contact) {
    Buf b = s.buf(.15f);
    Mode m[4] = {{620, .02f, .6f}, {1250, .012f, .5f}, {2300, .008f, .4f}, {3700, .005f, .3f}};
    s.jitter(m, 4, .05f, .2f);
    s.impact(b, 0.f, m, 4, contact, gain);
    s.noiseBurst(b, 0.f, .1f, 5.f, 2000.f, 8000.f, .5f * gain);
    return b;
}

Buf sfxWin(Synth& s, int variant) {
    // Rast/Çargah-flavoured bright flourish on D4
    static const float kRast[7] = {0, 204, 386, 498, 702, 906, 1088};
    const float base = 293.66f;
    auto hz = [&](int d) {
        const int oct = d >= 0 ? d / 7 : -((-d + 6) / 7);
        return base * centsToRatio(kRast[d - 7 * oct] + 1200.f * (float)oct);
    };
    Buf b = s.buf(1.6f);
    if (variant == 0) {
        const int run[4] = {0, 2, 4, 5};
        for (int i = 0; i < 4; ++i) renderPluck(s, b, {.01f + .068f * (float)i, hz(run[i]), .2f, .5f, 0, 0.f, 0.f});
        renderPluck(s, b, {.29f, hz(7), .95f, .7f, 7, .062f, 0.f});
        renderPluck(s, b, {.29f, hz(4), .95f, .4f, 0, 0.f, 0.f});
        renderPluck(s, b, {.30f, hz(-7), 1.f, .45f, 0, 0.f, 0.f});
    } else {
        const int mel[6] = {4, 5, 4, 2, 4, 6};
        for (int i = 0; i < 6; ++i) renderPluck(s, b, {.01f + .07f * (float)i, hz(mel[i]), .18f, .5f, 0, 0.f, 0.f});
        renderPluck(s, b, {.44f, hz(7), .9f, .7f, 6, .064f, 0.f});
        renderPluck(s, b, {.45f, hz(2), .9f, .35f, 0, 0.f, 0.f});
        renderPluck(s, b, {.46f, hz(-7), .95f, .45f, 0, 0.f, 0.f});
    }
    bodyColour(s, b);
    const Buf d = percDum(s, .35f);
    mixInto(b, d, s.at(variant == 0 ? .29f : .44f), 1.f);
    return b;
}

Buf sfxLose(Synth& s, int variant) {
    const Makam& m = kMakams[variant == 0 ? 0 : 4]; // Hicaz or Kürdi
    const float base = 293.66f;
    auto hz = [&](int d) { return base * centsToRatio(degreeCents(m, d)); };
    Buf b = s.buf(1.9f);
    const int n = variant == 0 ? 5 : 4;
    const int seq0[5] = {4, 3, 2, 1, 0};
    const int seq1[4] = {3, 2, 1, 0};
    const float step = variant == 0 ? .15f : .19f;
    for (int i = 0; i < n; ++i) {
        const int d = variant == 0 ? seq0[i] : seq1[i];
        const bool last = i == n - 1;
        renderPluck(s, b, {.01f + step * (float)i, hz(d), last ? .75f : step + .05f, last ? .55f : .45f,
                           0, 0.f, last ? -45.f : 0.f});
    }
    renderPluck(s, b, {.01f + step * (float)(n - 1) + .02f, hz(-7), .8f, .4f, 0, 0.f, -30.f});
    bodyColour(s, b);
    return b;
}

Buf sfxButton(Synth& s) {
    Buf b = s.buf(.06f);
    Mode m[3] = {{1900, .006f, 1.f}, {3400, .004f, .6f}, {5200, .003f, .35f}};
    Mode w[2] = {{600, .008f, .3f}, {1100, .007f, .3f}};
    s.jitter(m, 3, .06f, .2f);
    s.impact(b, .001f, m, 3, .1f, 1.f);
    s.impact(b, .001f, w, 2, .3f, 1.f);
    return b;
}

Buf sfxError(Synth& s) {
    Buf b = s.buf(.4f);
    Mode k[3] = {{420, .03f, 1.f}, {980, .018f, .6f}, {1700, .01f, .35f}};
    s.jitter(k, 3, .03f, .1f);
    s.impact(b, .002f, k, 3, .8f, 1.f);
    s.impact(b, .115f, k, 3, .8f, .85f, .3f, .84f);
    return b;
}

// Two tavla dice thrown onto the wooden board (hollow box).
Buf sfxDiceRaw(Synth& s) {
    Buf b = s.buf(1.1f);
    Mode board[5] = {{245, .05f, .6f}, {410, .035f, .8f}, {690, .025f, .6f}, {1060, .018f, .45f}, {1620, .012f, .3f}};
    Mode die[3] = {{5300, .003f, .55f}, {7600, .0022f, .4f}, {9900, .0016f, .25f}};
    Mode wall[3] = {{520, .03f, .7f}, {880, .02f, .6f}, {1500, .012f, .4f}};
    s.jitter(board, 5, .08f, .25f);
    for (int d = 0; d < 2; ++d) {
        float t = .003f + (d ? s.u(.004f, .035f) : 0.f);
        float g = s.u(.8f, 1.f), gap = s.u(.06f, .11f);
        const int bounces = s.ir(4, 7);
        for (int k = 0; k < bounces; ++k) {
            const float c = s.u(.07f, .14f);
            s.impact(b, t, board, 5, c, g, .3f, s.u(.97f, 1.03f));
            s.impact(b, t, die, 3, c, g, .3f, s.u(.9f, 1.1f));
            if (s.chance(.15f)) s.impact(b, t + .001f, wall, 3, .2f, g * .6f);
            t += gap;
            gap *= s.u(.6f, .82f);
            g *= s.u(.55f, .78f);
            if (gap < .012f) break;
        }
        const int rolls = s.ir(3, 8);
        for (int k = 0; k < rolls; ++k) { // tumbling over the edges
            t += s.u(.012f, .03f);
            g *= .82f;
            s.impact(b, t, board, 5, .1f, g * .5f, .3f, s.u(.97f, 1.03f));
            s.impact(b, t, die, 3, .1f, g * .6f, .3f, s.u(.9f, 1.1f));
        }
    }
    if (s.chance(.5f)) s.impact(b, s.u(.03f, .12f), die, 3, .05f, .5f, .3f, 1.3f); // dice knock together
    return b;
}

// Checkers ("pul") slapped onto the tavla board.
Buf ambCheckers(Synth& s) {
    Buf b = s.buf(2.2f);
    Mode board[5] = {{245, .05f, .6f}, {410, .035f, .8f}, {690, .025f, .6f}, {1060, .018f, .45f}, {1620, .012f, .3f}};
    Mode pul[3] = {{1150, .012f, 1.f}, {2300, .008f, .6f}, {3700, .005f, .35f}};
    s.jitter(board, 5, .08f, .25f);
    s.jitter(pul, 3, .08f, .2f);
    float t = .01f;
    const int n = s.ir(2, 4);
    for (int i = 0; i < n; ++i) {
        const float g = s.u(.6f, 1.f);
        s.impact(b, t, board, 5, .35f, g);
        s.impact(b, t, pul, 3, .35f, g * .8f);
        if (s.chance(.4f)) { // a slide into place before the slap
            s.impact(b, t + s.u(.05f, .09f), pul, 3, .3f, g * .25f);
        }
        t += s.u(.25f, .6f);
    }
    return b;
}

Buf sfxChair(Synth& s, int kind) {
    Buf b = s.buf(1.2f);
    std::vector<double> exc((size_t)s.at(1.f), 0.0);
    if (kind == 0 || kind == 2) { // creak: stick-slip in a joint
        const float D = kind == 0 ? s.u(.35f, .7f) : s.u(.25f, .4f);
        Mode wood[5] = {{310, .02f, .8f}, {590, .016f, .7f}, {870, .012f, .6f}, {1330, .01f, .4f}, {2100, .008f, .25f}};
        s.jitter(wood, 5, .1f, .3f);
        const float p0 = s.u(.007f, .011f), p1 = s.u(.004f, .007f);
        float t = 0.f;
        while (t < D) {
            const float q = t / D;
            const float period = p0 + (p1 - p0) * (0.5f - 0.5f * std::cos(kTauF * q));
            const float env = std::pow(std::sin((float)kPi * q), .7f) * s.u(.6f, 1.f);
            const int i = s.at(t);
            if (i < (int)exc.size()) exc[(size_t)i] += env * s.u(.7f, 1.f);
            t += period * s.u(.85f, 1.15f);
        }
        s.resonate(b, 0, exc, wood, 5, .5f);
    }
    if (kind == 1) { // scrape: chair legs dragged on the floor
        const float D = s.u(.25f, .45f);
        Mode floor[4] = {{180, .02f, .6f}, {420, .015f, .7f}, {900, .01f, .5f}, {1600, .007f, .35f}};
        s.jitter(floor, 4, .1f, .3f);
        float t = 0.f;
        const float per = s.u(.0022f, .004f);
        while (t < D) {
            const float q = t / D;
            const float env = std::min(q / .15f, 1.f) * std::min((1.f - q) / .2f, 1.f) * s.u(.5f, 1.f);
            const int i = s.at(t);
            if (i < (int)exc.size()) exc[(size_t)i] += env;
            t += per * s.u(.7f, 1.3f);
        }
        s.resonate(b, 0, exc, floor, 4, .35f);
        Noise nz(s.seed32());
        Biquad bp;
        bp.bandpass(s.sr(), 1200.f, .8f);
        const int n = s.at(D);
        for (int i = 0; i < n; ++i) {
            const float q = (float)i / (float)n;
            b[(size_t)i] += bp.process(nz.next()) * std::sin((float)kPi * q) * .05f;
        }
    }
    if (kind >= 1) { // the leg set down
        Mode thud[3] = {{120, .04f, 1.f}, {260, .03f, .6f}, {520, .02f, .3f}};
        s.impact(b, kind == 1 ? s.u(.45f, .55f) : s.u(.42f, .5f), thud, 3, 3.f, 1.4f);
    }
    return b;
}

// Çaycı's round metal tray with glasses rattling on it.
Buf ambTray(Synth& s) {
    Buf b = s.buf(2.6f);
    Mode tray[5] = {{380, .3f, .5f}, {1020, .2f, .5f}, {1850, .12f, .4f}, {2900, .08f, .3f}, {4100, .05f, .2f}};
    s.jitter(tray, 5, .05f, .2f);
    float t = .01f;
    const int n = s.ir(4, 8);
    for (int i = 0; i < n; ++i) {
        const float g = s.u(.3f, 1.f);
        glassClinks(s, b, t, 1, s.u(1900.f, 2600.f), g * .6f);
        s.impact(b, t + .002f, tray, 5, .15f, g * .25f);
        t += s.u(.12f, .35f);
    }
    return b;
}

// Distant cough: a noise burst through a vocal tract, with a little voicing.
Buf ambCough(Synth& s) {
    Buf b = s.buf(1.4f);
    const int n = s.ir(1, 3);
    float t = .01f, g = 1.f;
    Noise nz(s.seed32());
    const float F1 = s.u(450.f, 650.f), F2 = s.u(1100.f, 1500.f), f0 = s.u(95.f, 130.f);
    for (int c = 0; c < n; ++c) {
        Biquad b1, b2, b3;
        b1.bandpass(s.sr(), F1, 3.f);
        b2.bandpass(s.sr(), F2, 4.f);
        b3.bandpass(s.sr(), 2500.f, 5.f);
        const float dec = s.u(.09f, .14f);
        const int L = s.at(dec * 6.f), st = s.at(t);
        float ph = 0.f;
        for (int i = 0; i < L && st + i < (int)b.size(); ++i) {
            const float tt = (float)i / s.sr();
            const float env = std::min(tt / .008f, 1.f) * std::exp(-tt / dec);
            ph += f0 * (1.f - .2f * tt) / s.sr();
            if (ph >= 1.f) ph -= 1.f;
            const float src = nz.next() + .6f * (2.f * ph - 1.f) * std::exp(-tt / .06f);
            b[(size_t)(st + i)] += (b1.process(src) + .7f * b2.process(src) + .3f * b3.process(src)) * env * g;
        }
        t += s.u(.22f, .4f);
        g *= s.u(.6f, .85f);
    }
    return b;
}

// Makes a one-shot sound come from further away (air absorption + level); the room reverb is
// added live by the ambience bus.
Buf distant(Buf b, float sr, float lpHz, float peakDb) {
    Biquad lp1, lp2;
    lp1.lowpass(sr, lpHz, 0.6f);
    lp2.lowpass(sr, lpHz * 1.3f, 0.7f);
    for (float& x : b) x = lp2.process(lp1.process(x));
    finish(b, sr, peakDb, -60.f);
    return b;
}

// Offline room: adds a short reverb tail so a sound is heard "from another table".
void roomize(Buf& b, float sr, float wet, float rt60) {
    Reverb r;
    r.init(sr, rt60, 3500.f, 0.7f);
    b.resize(b.size() + (size_t)(rt60 * sr * 0.8f), 0.f);
    for (float& x : b) {
        float l, rr;
        r.process(x, l, rr);
        x = x * (1.f - wet * .5f) + (l + rr) * wet;
    }
}

// A car passing on the street outside, heard through the window glass: tyre roar on a dry night, or
// the hiss and spray of wet tyres (plus a puddle splash) on a rainy one, over a low engine hum with a
// slight Doppler drop. Variations 0-1 dry, 2-3 wet. The pass (closest approach) is ~1.3 s in.
Buf sfxCarPass(Synth& s, int v) {
    const float sr = s.sr();
    const bool wet = v >= 2;
    const float dur = 3.8f, tp = 1.3f;
    const float vel = s.u(7.f, 10.f), d0 = s.u(5.5f, 7.f);  // speed (m/s), closest distance (m)
    const float f0 = s.u(30.f, 42.f);                       // engine firing rate at idle-ish revs
    Buf b = s.buf(dur);
    Noise nz(s.seed32()), nz2(s.seed32());
    Biquad tyre, tyreLp, hissHp, hissLp, engLp, glass, hp;
    tyreLp.lowpass(sr, 2200.f, .7f);
    hissHp.highpass(sr, 1700.f, .7f);
    hissLp.lowpass(sr, 5200.f, .6f);
    engLp.lowpass(sr, 230.f, .8f);
    glass.lowpass(sr, wet ? 4200.f : 3200.f, .6f);
    hp.highpass(sr, 38.f, .7f);
    OnePole grain;
    grain.lp(sr, 35.f);
    double ph = 0.0;
    float amp = 0.f, dop = 1.f;
    for (int i = 0; i < (int)b.size(); ++i) {
        const float t = (float)i / sr;
        if (i % kCtrl == 0) {
            const float x = (t - tp) * vel, dist = std::sqrt(x * x + d0 * d0);
            amp = std::pow(d0 / dist, 2.2f) * std::min(1.f, t / .35f);  // fade in from the far end of the street
            dop = 1.f - .9f * (vel * x / dist) / 343.f * 3.f;  // exaggerated a little so it reads through the wall
            tyre.bandpass(sr, (520.f + 900.f * amp) * dop, .7f);
        }
        const float n = nz.next();
        float y = tyreLp.process(tyre.process(n)) * (wet ? .55f : 1.f);
        if (wet) {
            const float g = 1.f + 1.4f * grain.process(nz2.next());
            y += hissLp.process(hissHp.process(nz2.next())) * 1.3f * std::pow(amp, .3f) * g;
        }
        ph += (double)(f0 * dop) / sr;
        if (ph >= 1.0) ph -= 1.0;
        float e = 0.f;
        for (int k = 1; k <= 6; ++k) e += std::sin(kTauF * (float)(ph * k)) / (float)k;
        y = y * amp + engLp.process(e) * .55f * std::pow(amp, 1.2f);
        b[(size_t)i] = hp.process(glass.process(y));
    }
    if (wet) s.noiseBurst(b, tp + s.u(.02f, .1f), 18.f, 220.f, 700.f, 3200.f, .22f);  // through a puddle
    return b;
}

// The cat across the room: a voiced "mi-a-uw" (a glottal pulse train through moving formants), a
// soft onset through the closed mouth ("m"), a little breath. v 0 = a short questioning "mrrp" with a
// rolled r, 1 = "miyav", 2 = a long plaintive one.
Buf sfxMeow(Synth& s, int v) {
    const float sr = s.sr();
    const float dur = v == 0 ? s.u(.26f, .34f) : (v == 1 ? s.u(.55f, .7f) : s.u(.85f, 1.05f));
    const float fStart = s.u(430.f, 520.f), fPeak = s.u(680.f, 820.f) * (v == 2 ? .92f : 1.f);
    const float fEnd = v == 0 ? s.u(640.f, 760.f) : s.u(360.f, 450.f);
    const float peakAt = v == 0 ? .85f : s.u(.28f, .4f);
    Buf b = s.buf(dur + .05f);
    Biquad F1, F2, F3, br;
    br.bandpass(sr, 3200.f, 1.2f);
    Noise nz(s.seed32());
    OnePole nasal;
    nasal.lp(sr, 700.f);
    double ph = 0.0;
    auto ease = [](float x) { x = clampf(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); };
    const int N = s.at(dur);
    for (int i = 0; i < N; ++i) {
        const float t = (float)i / sr, u = t / dur;
        float f0 = u < peakAt ? fStart + (fPeak - fStart) * ease(u / peakAt) : fPeak + (fEnd - fPeak) * ease((u - peakAt) / (1.f - peakAt));
        f0 *= 1.f + .012f * std::sin(kTauF * 6.f * t);
        if (i % kCtrl == 0) {
            // m/i -> a -> u
            float f1, f2;
            if (u < .22f) f1 = 380.f + 520.f * ease(u / .22f), f2 = 2100.f - 450.f * ease(u / .22f);
            else if (u < .62f) f1 = 900.f, f2 = 1650.f;
            else f1 = 900.f - 430.f * ease((u - .62f) / .38f), f2 = 1650.f - 780.f * ease((u - .62f) / .38f);
            if (v == 0) f1 = 420.f + 380.f * ease(u), f2 = 1700.f;
            F1.bandpass(sr, f1, 4.f);
            F2.bandpass(sr, f2, 6.f);
            F3.bandpass(sr, 3100.f, 7.f);
        }
        ph += (double)f0 / sr;
        if (ph >= 1.0) ph -= 1.0;
        float src = 0.f;
        for (int k = 1; k <= 14 && k * f0 < 7000.f; ++k) src += std::sin(kTauF * (float)(ph * k)) / std::pow((float)k, 1.1f);
        float y = F1.process(src) * 1.4f + F2.process(src) * .9f + F3.process(src) * .35f;
        const float m = clampf(t / .06f, 0.f, 1.f);  // the mouth opens: nasal hum first
        y = nasal.process(src) * (1.f - m) * .5f + y * m;
        y += br.process(nz.next()) * .06f;
        float env = std::min(t / .025f, 1.f) * std::min((dur - t) / .09f, 1.f) * (.75f + .25f * std::sin((float)kPi * u));
        if (v == 0) env *= u < .5f ? .65f + .35f * std::sin(kTauF * 26.f * t) : 1.f;  // the rolled "rr"
        b[(size_t)i] = y * env;
    }
    Biquad lp;
    lp.lowpass(sr, 5200.f, .7f);
    filterBuf(b, lp);
    roomize(b, sr, .38f, .7f);
    return b;
}

// Başarımlar: a small brass bell (zil) struck three times on a rising major arpeggio, the last one left to ring.
// Bell partials (minor-third bell: 0.5 hum, 1, 1.2, 1.5, 2, 2.6, 3) with slightly detuned pairs for the shimmer.
Buf sfxChime(Synth& s, int variant) {
    Buf b = s.buf(2.2f);
    static const float kRatio[7] = {.5f, 1.f, 1.19f, 1.5f, 2.f, 2.61f, 3.01f};
    static const float kDecay[7] = {1.1f, .9f, .55f, .45f, .35f, .22f, .16f};
    static const float kAmp[7] = {.25f, 1.f, .45f, .35f, .4f, .22f, .15f};
    const float root = variant == 0 ? 880.f : 784.f; // A5 / G5
    const float steps[3] = {1.f, 1.2599f, 1.4983f}; // root, major third, fifth
    for (int k = 0; k < 3; ++k) {
        Mode m[14];
        const float f0 = root * steps[k];
        const float last = k == 2 ? 1.6f : 1.f;
        for (int i = 0; i < 7; ++i) {
            m[2 * i] = {f0 * kRatio[i], kDecay[i] * last, kAmp[i] * .6f};
            m[2 * i + 1] = {f0 * kRatio[i] * s.u(1.002f, 1.005f), kDecay[i] * .85f * last, kAmp[i] * .4f};
        }
        s.impact(b, .01f + .12f * (float)k, m, 14, .25f, k == 2 ? 1.f : .7f, .15f);
    }
    return b;
}

// ---- the other games: cards, our own dice, tavla checkers
Buf sfxCardPlace(Synth& s) {
    Buf b = s.buf(.18f);
    const float t0 = .002f;
    s.noiseBurst(b, 0.f, 3.f, 22.f, 1800.f, 7000.f, .35f);            // the card slides through the air / felt
    s.noiseBurst(b, t0 + s.u(.018f, .03f), .4f, 6.f, 900.f, 5000.f, .9f); // ... and lands flat
    Mode felt[3] = {{180, .02f, .6f}, {320, .014f, .4f}, {640, .008f, .2f}};
    s.jitter(felt, 3, .08f, .2f);
    s.impact(b, t0 + .025f, felt, 3, s.u(.8f, 1.4f), .5f, .2f);
    return b;
}

Buf sfxCardSlap(Synth& s) {
    Buf b = s.buf(.35f);
    s.noiseBurst(b, 0.f, 2.f, 14.f, 1500.f, 6000.f, .3f);
    const float t = s.u(.02f, .035f);
    Mode palm[4] = {{120, .045f, .8f}, {190, .035f, .8f}, {300, .025f, .6f}, {470, .018f, .4f}};
    s.jitter(palm, 4, .06f, .2f);
    s.impact(b, t, palm, 4, s.u(2.5f, 3.5f), 1.f, .2f);
    s.noiseBurst(b, t, .3f, 9.f, 700.f, 4500.f, .9f);
    return b;
}

Buf sfxCardShuffle(Synth& s) {
    const float D = s.u(.95f, 1.15f);
    Buf b = s.buf(D + .25f);
    // riffle: two halves interleave, the clicks speed up and die away
    float t = .02f;
    const float riffle = D * .62f;
    while (t < riffle) {
        const float u = t / riffle;
        s.noiseBurst(b, t, .15f, s.u(1.2f, 2.4f), 2200.f, 9000.f, s.u(.25f, .55f) * (1.f - .4f * u));
        t += s.u(.006f, .014f) * (1.2f - .6f * u);
    }
    // the bridge: a soft whirr as the cards fall back into one deck, and a tap to square it
    s.noiseBurst(b, riffle + .02f, 25.f, 90.f, 800.f, 4000.f, .35f);
    Mode deck[3] = {{220, .03f, .7f}, {390, .02f, .5f}, {760, .012f, .3f}};
    s.impact(b, D, deck, 3, 1.4f, .6f, .2f);
    s.impact(b, D + s.u(.07f, .11f), deck, 3, 1.2f, .45f, .2f);
    return b;
}

// A dealt card skimming the felt: a short band of noise that rises and settles, the cloth's soft hiss.
Buf sfxCardSlide(Synth& s) {
    Buf b = s.buf(.26f);
    s.noiseBurst(b, 0.f, s.u(35.f, 55.f), s.u(110.f, 150.f), 1400.f, 5200.f, .45f);
    s.noiseBurst(b, s.u(.12f, .16f), .6f, 7.f, 900.f, 4200.f, .35f); // it stops against the pile
    return b;
}

// Cards pushed together: several papery brushes in a quick run, then the bundle squared with a tap.
Buf sfxCardGather(Synth& s) {
    Buf b = s.buf(.42f);
    float t = .0f;
    const int n = 3 + (int)s.u(0.f, 2.99f);
    for (int i = 0; i < n; ++i) {
        s.noiseBurst(b, t, 6.f, s.u(30.f, 50.f), 1500.f, 6500.f, s.u(.35f, .55f));
        t += s.u(.035f, .06f);
    }
    Mode deck[3] = {{230, .025f, .7f}, {410, .018f, .5f}, {790, .010f, .3f}};
    s.jitter(deck, 3, .08f, .2f);
    s.impact(b, t + .05f, deck, 3, 1.1f, .55f, .2f);
    return b;
}

// A tossed card: the air, then a crisp flat snap on the felt (brighter and shorter than a laid card).
Buf sfxCardSnap(Synth& s) {
    Buf b = s.buf(.2f);
    s.noiseBurst(b, 0.f, 2.f, 16.f, 2200.f, 8000.f, .25f);
    const float t = s.u(.016f, .026f);
    s.noiseBurst(b, t, .2f, 4.f, 1600.f, 7500.f, 1.f);
    Mode felt[3] = {{210, .016f, .55f}, {380, .011f, .4f}, {760, .006f, .25f}};
    s.jitter(felt, 3, .08f, .2f);
    s.impact(b, t, felt, 3, s.u(1.f, 1.6f), .55f, .2f);
    return b;
}

Buf sfxChecker(Synth& s) {
    Buf b = s.buf(.22f);
    Mode wood[4] = {{820, .022f, 1.f}, {1350, .016f, .7f}, {2150, .010f, .45f}, {3300, .006f, .25f}};
    Mode board[3] = {{210, .04f, .6f}, {360, .03f, .5f}, {620, .02f, .35f}};
    s.jitter(wood, 4, .07f, .25f);
    s.jitter(board, 3, .07f, .25f);
    const float fs = s.u(.9f, 1.12f);
    s.impact(b, .001f, wood, 4, s.u(.2f, .32f), 1.f, .3f, fs);
    s.impact(b, .001f, board, 3, .4f, .7f, .2f);
    if (s.chance(.5f)) s.impact(b, .001f + s.u(.012f, .022f), wood, 4, .2f, s.u(.2f, .35f), .3f, fs);
    return b;
}

// (ozelgun) Ramazan nights: the sahur davulcu down the street. The big davul's deep "güm" (the tokmak: a membrane
// dropping from ~100 to ~60 Hz with a long boom and a thud of noise) and the thin stick's dry "tak" on the other head,
// in the old sahur beat GÜM . tak tak GÜM . tak . (two or three bars, ~4 s), heard through the walls: low-passed, with a
// street echo. Played quietly (App, every minute or two at night).
Buf sfxDavul(Synth& s, int v) {
    const float sr = s.sr();
    Buf b = s.buf(5.5f);
    auto gum = [&](float t, float g) {
        const int st = s.at(t);
        double ph = 0.0;
        const float f0 = s.u(96.f, 106.f), f1 = s.u(58.f, 64.f);
        for (int i = 0; i < (int)(0.9f * sr) && st + i < (int)b.size(); ++i) {
            const float tt = (float)i / sr;
            const float f = f1 + (f0 - f1) * std::exp(-tt / .05f);
            ph += kTau * f / sr;
            const float env = std::min(tt / .004f, 1.f) * std::exp(-tt / .3f);
            b[(size_t)(st + i)] += (float)(std::sin(ph) + .3 * std::sin(2.3 * ph) * std::exp(-tt / .06f)) * env * g;
        }
        s.noiseBurst(b, t, .5f, 25.f, 50.f, 420.f, .5f * g);
    };
    auto tak = [&](float t, float g) {
        Mode m[3] = {{430, .03f, .6f}, {780, .02f, .5f}, {1350, .012f, .3f}};
        s.jitter(m, 3, .05f, .2f);
        s.impact(b, t, m, 3, .6f, g);
        s.noiseBurst(b, t, .1f, 8.f, 1200.f, 5000.f, .3f * g);
    };
    const float beat = s.u(.27f, .31f);  // an eighth
    const int bars = v == 0 ? 2 : 3;
    for (int bar = 0; bar < bars; ++bar) {
        const float t0 = .05f + (float)bar * 8.f * beat;
        gum(t0, s.u(.9f, 1.f));
        tak(t0 + 2.f * beat, s.u(.45f, .55f));
        tak(t0 + 3.f * beat, s.u(.4f, .5f));
        gum(t0 + 4.f * beat, s.u(.8f, .95f));
        tak(t0 + 6.f * beat, s.u(.45f, .55f));
    }
    Buf d = distant(b, sr, 900.f, -6.f);
    roomize(d, sr, .45f, 1.3f);
    return d;
}

Buf synthSfx(Sfx id, int v, Synth& s) {
    switch (id) {
    case Sfx::TileClick: return sfxTileClick(s);
    case Sfx::TileDraw: return sfxTileDraw(s);
    case Sfx::TileDiscard: return sfxTileDiscard(s);
    case Sfx::TileSlam: return sfxTileSlam(s);
    case Sfx::Shuffle: return sfxShuffle(s);
    case Sfx::TeaClink: return sfxTeaClink(s);
    case Sfx::TeaSip: return sfxTeaSip(s);
    case Sfx::GlassSet: return sfxGlassSet(s);
    case Sfx::Penalty: return sfxPenalty(s);
    case Sfx::Win: return sfxWin(s, v);
    case Sfx::Lose: return sfxLose(s, v);
    case Sfx::Button: return sfxButton(s);
    case Sfx::Error: return sfxError(s);
    case Sfx::Dice: {
        Buf b = sfxDiceRaw(s);
        Biquad lp;
        lp.lowpass(s.sr(), 7500.f);
        filterBuf(b, lp);
        roomize(b, s.sr(), .3f, .6f);
        return b;
    }
    case Sfx::Chair: return sfxChair(s, v % 3);
    case Sfx::CarPass: return sfxCarPass(s, v);
    case Sfx::Meow: return sfxMeow(s, v % 3);
    case Sfx::CardPlace: return sfxCardPlace(s);
    case Sfx::CardSlap: return sfxCardSlap(s);
    case Sfx::CardShuffle: return sfxCardShuffle(s);
    case Sfx::DiceThrow: {
        Buf b = sfxDiceRaw(s);
        roomize(b, s.sr(), .1f, .35f);
        return b;
    }
    case Sfx::Checker: return sfxChecker(s);
    case Sfx::CardSlide: return sfxCardSlide(s);
    case Sfx::CardGather: return sfxCardGather(s);
    case Sfx::CardSnap: return sfxCardSnap(s);
    case Sfx::Chime: return sfxChime(s, v % 2);
    case Sfx::Davul: return sfxDavul(s, v % 2);  // (ozelgun)
    default: return Buf(64, 0.f);
    }
}

std::unique_ptr<SfxBank> buildSfxBank(float sr, uint64_t seed) {
    auto bank = std::make_unique<SfxBank>();
    bank->sr = sr;
    std::vector<std::pair<int, int>> jobs;
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        bank->vars[(size_t)i].resize((size_t)kSfx[i].vars);
        for (int v = 0; v < kSfx[i].vars; ++v) jobs.emplace_back(i, v);
    }
    parallelFor((int)jobs.size(), [&](int j) {
        const int i = jobs[(size_t)j].first, v = jobs[(size_t)j].second;
        Synth s(sr, seed * 0x9E3779B97F4A7C15ull + (uint64_t)(i * 131 + v * 7919 + 1));
        Buf b = synthSfx((Sfx)i, v, s);
        finish(b, sr, kSfx[i].peakDb);
        bank->vars[(size_t)i][(size_t)v] = std::move(b);
    });
    return bank;
}

} // namespace audio_detail
} // namespace ui
