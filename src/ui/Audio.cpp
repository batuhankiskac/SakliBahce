// Audio for SaklıBahçe (audio owner).
//
// The old radio plays real recordings from assets/music (see RecordFx and Audio::Impl::updateRadio; credits in
// assets/music/KAYNAKLAR.md); everything else is synthesised. At init() every one-shot effect is made (modal resonator banks
// excited by short contact pulses, filtered noise, Karplus-Strong strings) into a few random
// variations, each loaded as a raylib Sound with a small alias pool so that copies can overlap.
// The coffeehouse ambience and the old radio are endless generators pulled by raylib's audio thread
// through AudioStream callbacks: they cannot underrun when the game loop hitches, and they are
// sample-continuous (no buffer seams). The main thread talks to them only through atomics.
#include "ui/Audio.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ui {

// Developer hooks used by tools/audio_render.cpp. Not part of the public API; they expose the exact
// sample buffers and generator classes the game plays, so the renderer measures the real thing.
namespace audio_dev {
int sampleRate();
const char* sfxName(Sfx s);
int variationCount(Sfx s);
bool sfxSamples(Sfx s, int variation, std::vector<float>& out);
void renderAmbience(int sampleRate, unsigned long long seed, double seconds, unsigned layerMask,
                    bool randomChunks, std::vector<float>& stereoOut);
void renderRadio(int sampleRate, unsigned long long seed, double seconds, bool randomChunks,
                 std::vector<float>& stereoOut);
void renderAmbienceGarden(int sampleRate, unsigned long long seed, double seconds, float garden, float night,
                          std::vector<float>& stereoOut);  // (Bahçe)
// out[0..7] = amb frames, amb calls, amb max ms, amb total ms, radio frames, radio calls,
//             radio max ms, radio total ms
bool streamStats(double out[8]);
// Long run of fresh ambience + radio generators without storing the output. out[0..11] = amb peak,
// amb RMS first/last minute, amb max jump first/last minute, radio peak, radio RMS first/last
// minute, radio max jump first/last minute, non-finite samples, seconds rendered.
void soak(int sampleRate, unsigned long long seed, double seconds, double out[12]);
// A recording (assets/music) through the radio's speaker chain at the level the game plays it (stereo,
// what reaches the device at full master volume); false if the file can't be decoded.
bool renderRecord(const char* path, int sampleRate, double seconds, std::vector<float>& stereoOut);
double renderVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, bool chunked,
                   std::vector<float>& stereoOut);
// The worker's cost for one line (tools/voice_render.cpp): out[0] plan ms, out[1] render ms, out[2] the plan's seconds.
bool timeVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, double out[3]);
// The planned line as text: one event per row (flags, vowel, consonants, word kind, duration, pitch, loudness).
std::string dumpVoicePlan(int voice, const std::string& text, unsigned long long seed);
} // namespace audio_dev

namespace {

constexpr double kTau = 6.283185307179586;
constexpr double kPi = 3.141592653589793;
constexpr float kTauF = 6.2831853f;
constexpr int kCtrl = 32; // control-rate block of the generators, in samples

// raylib's stereo pan law gives 0.6875 per channel at pan 0; streams are compensated so the
// generator output is exactly what reaches the device.
constexpr float kCenterPanGain = 0.6875f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
inline float centsToRatio(float c) { return std::pow(2.f, c / 1200.f); }
inline float smoothCoef(float sr, float seconds) { return 1.f - std::exp(-1.f / (seconds * sr)); }

// Runs f(0..n-1) on a few worker threads (init-time synthesis only; every task owns its output and
// seeds its own RNG, so the result doesn't depend on scheduling).
template <class F>
void parallelFor(int n, F&& f) {
    const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
    const int nt = std::max(1, std::min({n, hw, 8}));
    std::atomic<int> next{0};
    auto worker = [&]() {
        for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) f(i);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < nt; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

using Buf = std::vector<float>;

// ------------------------------------------------------------------------------------------------
// DSP building blocks
// ------------------------------------------------------------------------------------------------

// xorshift32 white noise in [-1, 1)
struct Noise {
    uint32_t s = 0x9E3779B9u;
    Noise() = default;
    explicit Noise(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(int32_t)s * (1.f / 2147483648.f);
    }
};

// RBJ biquad, transposed direct form II.
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = (float)(B0 / A0);
        b1 = (float)(B1 / A0);
        b2 = (float)(B2 / A0);
        a1 = (float)(A1 / A0);
        a2 = (float)(A2 / A0);
    }
    static double omega(float sr, float hz) { return kTau * clampf(hz, 5.f, 0.47f * sr) / sr; }
    void lowpass(float sr, float hz, float q = 0.7071f) {
        const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(float sr, float hz, float q = 0.7071f) {
        const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass(float sr, float hz, float q) { // 0 dB peak gain
        const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peaking(float sr, float hz, float q, float db) {
        const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
        const double A = std::pow(10.0, db / 40.0);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct OnePole {
    float a = 1.f, y = 0.f;
    void lp(float sr, float hz) { a = 1.f - std::exp(-kTauF * hz / sr); }
    float process(float x) {
        y += a * (x - y);
        return y;
    }
};

// Schroeder allpass (diffuser).
struct Allpass {
    std::vector<float> buf;
    uint32_t mask = 0, len = 1, w = 0;
    float g = 0.6f;
    void init(int length, float gain) {
        uint32_t n = 1;
        while (n < (uint32_t)length + 2) n <<= 1;
        buf.assign(n, 0.f);
        mask = n - 1;
        len = (uint32_t)std::max(1, length);
        g = gain;
        w = 0;
    }
    float process(float x) {
        const float d = buf[(w - len) & mask];
        const float v = x + g * d;
        buf[w & mask] = v;
        ++w;
        return d - g * v;
    }
};

// 8-line feedback delay network (Householder mixing), mono in, stereo out.
class Reverb {
public:
    void init(float sr, float rt60, float dampHz, float size) {
        static const float kMs[8] = {23.1f, 28.7f, 33.9f, 38.3f, 43.1f, 49.7f, 55.3f, 61.9f};
        for (int k = 0; k < 8; ++k) {
            const int len = std::max(8, (int)(kMs[k] * size * 0.001f * sr));
            uint32_t n = 1;
            while (n < (uint32_t)len + 2) n <<= 1;
            line_[k].assign(n, 0.f);
            mask_[k] = n - 1;
            len_[k] = (uint32_t)len;
            g_[k] = std::pow(10.f, -3.f * ((float)len / sr) / rt60);
            damp_[k].lp(sr, dampHz);
            damp_[k].y = 0.f;
        }
        ap1_.init((int)(0.0071f * sr), 0.62f);
        ap2_.init((int)(0.0023f * sr), 0.6f);
        w_ = 0;
    }
    void process(float in, float& outL, float& outR) {
        in = ap2_.process(ap1_.process(in));
        float y[8];
        float sum = 0.f;
        for (int k = 0; k < 8; ++k) {
            y[k] = damp_[k].process(line_[k][(w_ - len_[k]) & mask_[k]]) * g_[k];
            sum += y[k];
        }
        const float h = sum * 0.25f; // 2/N
        for (int k = 0; k < 8; ++k) line_[k][w_ & mask_[k]] = y[k] - h + in;
        ++w_;
        outL = (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] + y[7])) * 0.3f;
        outR = (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] + y[6])) * 0.3f;
    }

private:
    std::vector<float> line_[8];
    uint32_t mask_[8] = {}, len_[8] = {};
    float g_[8] = {};
    OnePole damp_[8];
    Allpass ap1_, ap2_;
    uint32_t w_ = 0;
};

// Karplus-Strong string with a one-zero loss filter and allpass fine tuning (accurate to a few
// cents, which matters for makam intervals). Excitation is added into the loop, so re-plucking a
// ringing string (tremolo) behaves like a real plectrum stroke.
class KsString {
public:
    static constexpr int kLen = 4096;
    static constexpr uint32_t kMask = kLen - 1;

    void init(float sr) {
        sr_ = sr;
        buf_.assign(kLen, 0.f);
        exc_.assign(kLen, 0.f);
        envDecay_ = std::exp(-1.f / (0.06f * sr));
        retune();
        updateGain();
    }
    void setLoss(float s) {
        S_ = clampf(s, 0.02f, 0.5f);
        retune();
    }
    void setFreq(float hz) {
        f_ = clampf(hz, 30.f, sr_ * 0.2f);
        retune();
        updateGain();
    }
    void setT60(float sec) {
        t60_ = std::max(sec, 0.004f);
        updateGain();
    }
    float freq() const { return f_; }
    bool active() const { return active_; }

    void pluck(float amp, float bright, float pickPos, Noise& nz) {
        const int n = std::min(N_ + 1, kLen - 1);
        const float a = 0.1f + 0.55f * clampf(bright, 0.f, 1.f);
        float lp = 0.f, lp2 = 0.f, mean = 0.f;
        for (int i = 0; i < n; ++i) {
            lp += a * (nz.next() - lp);
            lp2 += a * (lp - lp2);
            exc_[i] = lp2;
            mean += lp2;
        }
        mean /= (float)n;
        const int P = std::max(1, (int)(clampf(pickPos, 0.02f, 0.5f) * (float)n));
        float pk = 1e-9f;
        for (int i = n - 1; i >= 0; --i) {
            const float v = (exc_[i] - mean) - (i >= P ? exc_[i - P] - mean : 0.f);
            exc_[i] = v;
            pk = std::max(pk, std::fabs(v));
        }
        // soften the very first samples of the stroke (plectrum leaving the string)
        const float g = amp / pk;
        for (int i = 0; i < n; ++i) {
            const float ramp = i < 3 ? (float)(i + 1) / 4.f : 1.f;
            exc_[i] *= g * ramp;
        }
        excLen_ = n;
        excPos_ = 0;
        active_ = true;
        env_ = std::max(env_, amp);
    }

    float tick() {
        if (!active_) return 0.f;
        const float x = buf_[(w_ - (uint32_t)N_) & kMask];
        const float y = apC_ * x + apX1_ - apC_ * apY1_;
        apX1_ = x;
        apY1_ = y;
        float v = ((1.f - S_) * y + S_ * prev_) * g_;
        prev_ = y;
        if (excPos_ < excLen_) v += exc_[excPos_++];
        buf_[w_ & kMask] = v;
        ++w_;
        const float av = std::fabs(v);
        env_ = av > env_ ? av : env_ * envDecay_;
        if (env_ < 2e-6f && excPos_ >= excLen_) active_ = false;
        return v;
    }

private:
    void retune() {
        const double w = kTau * f_ / sr_;
        const double re = (1.0 - S_) + S_ * std::cos(w), im = -S_ * std::sin(w);
        const double lossDelay = -std::atan2(im, re) / w;
        const double rem = (double)sr_ / f_ - lossDelay;
        int N = (int)std::floor(rem - 0.618);
        N = std::max(2, std::min(N, kLen - 2));
        const double d = rem - N;
        apC_ = (float)((1.0 - d) / (1.0 + d));
        N_ = N;
    }
    void updateGain() { g_ = std::exp(-6.9078f / (t60_ * f_)); }

    float sr_ = 48000.f;
    std::vector<float> buf_, exc_;
    uint32_t w_ = 0;
    int N_ = 100;
    float apC_ = 0.f, apX1_ = 0.f, apY1_ = 0.f;
    float S_ = 0.3f, prev_ = 0.f, g_ = 0.99f;
    float f_ = 220.f, t60_ = 2.f;
    int excLen_ = 0, excPos_ = 0;
    float env_ = 0.f, envDecay_ = 0.999f;
    bool active_ = false;
};

inline float softClip(float x) { // Padé tanh, smooth to +-1
    x = clampf(x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

// ------------------------------------------------------------------------------------------------
// Offline synthesis toolkit (runs at init)
// ------------------------------------------------------------------------------------------------

struct Mode {
    float f, tau, a; // frequency (Hz), decay time constant (s), amplitude
};

class Synth {
public:
    Synth(float sr, uint64_t seed) : sr_(sr), rng_(seed) {}
    float sr() const { return sr_; }
    float u(float lo, float hi) { return rng_.uniform(lo, hi); }
    float bip() { return rng_.uniform() * 2.f - 1.f; }
    int ir(int lo, int hi) { return rng_.range(lo, hi); }
    bool chance(float p) { return rng_.chance(p); }
    uint32_t seed32() { return (uint32_t)(rng_.next() >> 16) | 1u; }
    int at(float t) const { return std::max(0, (int)std::lround(t * sr_)); }
    Buf buf(float seconds) const { return Buf((size_t)(seconds * sr_) + 1, 0.f); }

    void jitter(Mode* m, int n, float fj, float aj) {
        for (int k = 0; k < n; ++k) {
            m[k].f *= u(1.f - fj, 1.f + fj);
            m[k].a *= u(1.f - aj, 1.f + aj);
        }
    }

    // Filter `exc` (starting at sample `start` of `out`) through a bank of damped resonators.
    void resonate(Buf& out, int start, const std::vector<double>& exc, const Mode* m, int n, float gain,
                  float fscale = 1.f) {
        for (int k = 0; k < n; ++k) {
            const double f = (double)m[k].f * fscale;
            if (f <= 0.0 || f >= 0.45 * sr_ || m[k].a == 0.f) continue;
            const double w = kTau * f / sr_;
            const double r = std::exp(-1.0 / ((double)m[k].tau * sr_));
            const double c1 = 2.0 * r * std::cos(w), c2 = -r * r;
            const double gin = (double)m[k].a * gain * std::sin(w);
            const int L = (int)exc.size() + (int)((double)m[k].tau * sr_ * 7.0);
            if ((int)out.size() < start + L) out.resize((size_t)(start + L), 0.f);
            double y1 = 0.0, y2 = 0.0;
            const int E = (int)exc.size();
            for (int i = 0; i < L; ++i) {
                const double y = (i < E ? exc[(size_t)i] * gin : 0.0) + c1 * y1 + c2 * y2;
                y2 = y1;
                y1 = y;
                out[(size_t)(start + i)] += (float)y;
            }
        }
    }

    // One contact: a half-sine force pulse of `contactMs` (its width sets how dull the hit is).
    void impact(Buf& out, float t0, const Mode* m, int n, float contactMs, float gain,
                float roughness = 0.3f, float fscale = 1.f) {
        if (gain <= 0.f) return;
        const int W = std::max(1, (int)std::lround(contactMs * 0.001f * sr_));
        std::vector<double> exc((size_t)W);
        double area = 0.0;
        for (int i = 0; i < W; ++i) {
            exc[(size_t)i] = std::sin(kPi * (i + 0.5) / W);
            area += exc[(size_t)i];
        }
        for (int i = 0; i < W; ++i) exc[(size_t)i] = exc[(size_t)i] / area * (1.0 + roughness * bip());
        resonate(out, at(t0), exc, m, n, gain, fscale);
    }

    // Band-limited noise with a linear attack and exponential decay.
    void noiseBurst(Buf& out, float t0, float attackMs, float decayMs, float hpHz, float lpHz, float gain) {
        const int start = at(t0);
        const int A = std::max(1, (int)(attackMs * 0.001f * sr_));
        const float tau = std::max(decayMs, 0.05f) * 0.001f;
        const int L = A + (int)(tau * sr_ * 7.f);
        if ((int)out.size() < start + L) out.resize((size_t)(start + L), 0.f);
        Biquad hp, lp, lp2;
        hp.highpass(sr_, hpHz);
        lp.lowpass(sr_, lpHz);
        lp2.lowpass(sr_, lpHz);
        Noise nz(seed32());
        const float dk = std::exp(-1.f / (tau * sr_));
        float e = 0.f;
        for (int i = 0; i < L; ++i) {
            e = i < A ? (float)(i + 1) / (float)A : e * dk;
            out[(size_t)(start + i)] += lp2.process(lp.process(hp.process(nz.next()))) * e * gain;
        }
    }

private:
    float sr_;
    okey::Rng rng_;
};

void mixInto(Buf& dst, const Buf& src, int offset, float gain) {
    if ((int)dst.size() < offset + (int)src.size()) dst.resize((size_t)(offset + (int)src.size()), 0.f);
    for (size_t i = 0; i < src.size(); ++i) dst[(size_t)offset + i] += src[i] * gain;
}

void filterBuf(Buf& b, Biquad f) {
    for (float& x : b) x = f.process(x);
}

float peakOf(const Buf& b) {
    float p = 0.f;
    for (float x : b) p = std::max(p, std::fabs(x));
    return p;
}

// DC block, trim the silent tail, fade the edges, normalise the peak.
void finish(Buf& b, float sr, float peakDb, float trimDb = -66.f) {
    if (b.empty()) return;
    Biquad hp;
    hp.highpass(sr, 28.f, 0.6f);
    filterBuf(b, hp);
    const float pk = peakOf(b);
    if (pk <= 0.f) return;
    const float thr = pk * dbToGain(trimDb);
    size_t end = b.size();
    while (end > 1 && std::fabs(b[end - 1]) < thr) --end;
    end = std::min(b.size(), end + (size_t)(0.01f * sr));
    b.resize(end);
    const size_t fadeOut = std::min(b.size() / 3, (size_t)(0.012f * sr));
    for (size_t i = 0; i < fadeOut; ++i) {
        const float g = 0.5f - 0.5f * std::cos((float)kPi * (float)i / (float)fadeOut);
        b[b.size() - 1 - i] *= g;
    }
    const size_t fadeIn = std::min(b.size() / 4, (size_t)(0.0006f * sr) + 1);
    for (size_t i = 0; i < fadeIn; ++i) b[i] *= (float)i / (float)fadeIn;
    // remove the residual mean of the (finite) buffer with a smooth window so the ends stay at 0
    double mean = 0.0, wsum = 0.0;
    const size_t n = b.size();
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(kTau * (double)i / (double)(n > 1 ? n - 1 : 1));
        mean += b[i];
        wsum += w;
    }
    const double k = mean / std::max(wsum, 1e-9);
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(kTau * (double)i / (double)(n > 1 ? n - 1 : 1));
        b[i] -= (float)(k * w);
    }
    const float p2 = peakOf(b);
    if (p2 > 0.f) {
        const float g = dbToGain(peakDb) / p2;
        for (float& x : b) x *= g;
    }
}

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

// Scales in cents above the karar (tonic). 7 degrees per octave.
struct Makam {
    const char* name;
    float cents[7];
    int aStart, aPeak, aEnd; // first phrase: from, up to, half cadence on (güçlü)
    int bStart, bPeak;       // second phrase, ends on the karar through the cadence
    int cadence[4];
    int cadLen;
};

const Makam kMakams[] = {
    {"Hicaz", {0, 114, 384, 498, 702, 792, 996}, 3, 5, 3, 4, 7, {3, 2, 1, 0}, 4},
    {"Uşşak", {0, 165, 294, 498, 702, 792, 996}, 0, 4, 3, 4, 7, {3, 2, 1, 0}, 4},
    {"Hüseyni", {0, 165, 294, 498, 702, 906, 996}, 4, 6, 4, 5, 7, {3, 2, 1, 0}, 4},
    {"Rast", {0, 204, 370, 498, 702, 906, 1070}, 0, 4, 4, 4, 7, {2, 1, -1, 0}, 4},
    {"Kürdi", {0, 90, 294, 498, 702, 792, 996}, 0, 4, 3, 3, 6, {2, 1, 0, 0}, 3},
};
constexpr int kNumMakams = (int)(sizeof(kMakams) / sizeof(kMakams[0]));

float degreeCents(const Makam& m, int d) {
    const int oct = (d >= 0) ? d / 7 : -((-d + 6) / 7);
    const int idx = d - 7 * oct;
    return m.cents[idx] + 1200.f * (float)oct;
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

// ------------------------------------------------------------------------------------------------
// Sfx table
// ------------------------------------------------------------------------------------------------

struct SfxDef {
    const char* name;
    int vars;       // synthesised variations
    int voices;     // simultaneous copies per variation
    float peakDb;   // peak level of the stored wave
    float pitchJit; // +- relative pitch randomisation per play
    float volJit;   // 0..1: volume is scaled by 1 - volJit * rand
    float pan = 0.f; // where play() puts it (-1 left .. 1 right): the street is on the player's left
};

constexpr SfxDef kSfx[(int)Sfx::Count] = {
    {"tile_click", 4, 3, -10.f, .05f, .15f},   {"tile_draw", 3, 2, -11.f, .04f, .12f},
    {"tile_discard", 4, 3, -5.f, .04f, .12f},  {"tile_slam", 3, 2, -3.f, .03f, .08f},
    {"shuffle", 2, 1, -9.f, .02f, .05f},       {"tea_clink", 4, 2, -13.f, .015f, .12f},
    {"tea_sip", 2, 1, -18.f, .03f, .1f},       {"glass_set", 3, 2, -10.f, .03f, .1f},
    {"penalty", 2, 1, -6.f, .015f, .05f},      {"win", 2, 1, -5.f, 0.f, .03f},
    {"lose", 2, 1, -7.f, 0.f, .03f},           {"button", 2, 3, -15.f, .04f, .1f},
    {"error", 1, 2, -11.f, .02f, .05f},        {"dice", 4, 2, -13.f, .04f, .15f},
    {"chair", 3, 1, -15.f, .04f, .15f},        {"car_pass", 4, 1, -17.f, .05f, .2f, -.55f},
    {"meow", 3, 1, -19.f, .07f, .25f, .15f},
    {"card_place", 4, 3, -12.f, .06f, .15f},  {"card_slap", 3, 2, -6.f, .05f, .1f},
    {"card_shuffle", 2, 1, -11.f, .03f, .05f}, {"dice_throw", 4, 2, -6.f, .04f, .1f},
    {"checker", 4, 3, -8.f, .05f, .12f},
    {"card_slide", 4, 3, -17.f, .06f, .2f},    {"card_gather", 3, 2, -12.f, .04f, .12f},
    {"card_snap", 4, 2, -9.f, .05f, .12f},
    {"chime", 2, 1, -9.f, 0.f, .03f},
    {"davul", 2, 1, -12.f, .02f, .1f, -.45f},  // (ozelgun) from the street, on the left
};

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

struct SfxBank {
    float sr = 48000.f;
    std::array<std::vector<Buf>, (size_t)Sfx::Count> vars;
};

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

// ------------------------------------------------------------------------------------------------
// Ambience: crowd murmur, TV football, ceiling fan, distant table sounds, room reverb
// ------------------------------------------------------------------------------------------------

enum AmbLayer : unsigned {
    LayerMurmur = 1, LayerTv = 2, LayerFan = 4, LayerEvents = 8, LayerRoom = 16, LayerRain = 32, LayerAll = 63
};

struct Vowel {
    float f1, f2, f3;
};
// Turkish vowels (adult male): a e ı i o ö u ü
const Vowel kVowels[8] = {{700, 1220, 2500}, {500, 1750, 2500}, {390, 1450, 2350}, {300, 2150, 2850},
                          {500, 900, 2450},  {450, 1500, 2350}, {330, 820, 2300},  {310, 1650, 2250}};

class AmbienceGen {
public:
    AmbienceGen(float sr, uint64_t seed, unsigned layers = LayerAll);
    void render(float* out, int frames); // interleaved stereo, overwrites
    std::atomic<float> target{0.f};      // written by the main thread
    std::atomic<float> rainTarget{0.f};  // rain outside, 0..1 (main thread)
    void setImmediate(float g) {
        target.store(g);
        cur_ = g;
    }
    void setRainImmediate(float r) {
        rainTarget.store(r);
        rain_ = r;
    }
    // Bahçe: the garden bed (Audio::setVenue) crossfades with the room's TV, fan and close reverb; night brings crickets
    std::atomic<float> gardenTarget{0.f}, nightTarget{0.f};
    // (ozelgun) the garden's awning in the rain: drops drumming on the canvas overhead (Audio::setRainCanvas)
    std::atomic<float> canvasTarget{0.f};
    float canvas_ = 0.f;
    bool canvasInit_ = false;
    Biquad canvasBp_[2], canvasLp_;
    void setGardenImmediate(float g, float n) {
        gardenTarget.store(g);
        nightTarget.store(n);
        garden_ = g;
        night_ = n;
    }

private:
    // ---- Bahçe (garden ambience): sparrows, the odd gull, wind in the leaves, crickets at night
    struct Chirper {
        float timer = 1.f, left = 0.f, len = .05f, t = 0.f, ph = 0.f, f0 = 5000.f, f1 = 3500.f, amp = 0.f, gl = .7f, gr = .7f;
        int notes = 0;
    };
    struct GullCall {
        float timer = 20.f, left = 0.f, len = .3f, t = 0.f, ph = 0.f, base = 1100.f, amp = 0.f, gl = .7f, gr = .7f;
        int notes = 0;
    };
    struct Cricket {
        float ph = 0.f, f = 4500.f, chirpT = 0.f, period = .6f, pulse = .03f, pulses = 3.f, amp = 0.f, gl = .7f, gr = .7f;
    };
    float garden_ = 0.f, night_ = 0.f, gardenCoef_ = 0.f;
    float windGust_ = .6f, windGustT_ = .6f, windTimer_ = 3.f;
    Biquad windBp_, windHp_, windLp_, gullLp_;
    std::array<Chirper, 2> chirp_{};
    GullCall gull_{};
    std::array<Cricket, 2> crick_{};
    void gardenTick(float dt, float& l, float& r, float& send);

    struct Talker {
        float f0Base = 110.f, f0 = 110.f, f0T = 110.f, ph = 0.f, jit = 0.f;
        float breath = .12f, gain = 1.f, gl = .7f, gr = .7f, send = .6f;
        OnePole tilt, dist;
        Biquad b1, b2, b3;
        float f1 = 500.f, f2 = 1500.f, f3 = 2500.f, f1T = 500.f, f2T = 1500.f, f3T = 2500.f;
        float vA = 0.f, nA = 0.f, dvA = 0.f, dnA = 0.f, vS = 0.f, nS = 0.f;
        bool talking = false, laugh = false, commentator = false, silent = true;
        float left = 0.f, syT = 0.f, syDur = .2f, consFrac = .25f, stress = 1.f, accent = 0.f, glide = 0.f;
        int cons = 0, syLeft = 0, syIdx = 0, syN = 1;
    };
    struct Bank {
        std::vector<Buf> vars;
        float meanGap = 10.f, gLo = .5f, gHi = 1.f, timer = 5.f;
        int last = -1;
    };
    struct Voice {
        const Buf* b = nullptr;
        size_t pos = 0;
        float gl = 0.f, gr = 0.f, send = 0.f;
    };

    float u(float lo, float hi) { return rng_.uniform(lo, hi); }
    float expo(float mean) { return -std::log(1.f - rng_.uniform() * .999f) * mean; }
    void initTalker(Talker& t, bool commentator);
    void startPhrase(Talker& t);
    void nextSyllable(Talker& t);
    void talkerControl(Talker& t, float dt, float excite);
    float talkerTick(Talker& t);
    void control();
    void spawn(Bank& bk);

    float sr_;
    unsigned layers_;
    okey::Rng rng_;
    Noise nz_;
    float cur_ = 0.f, gainCoef_ = 0.f;
    float kFormant_ = 0.f, kPitch_ = 0.f, kAmp_ = 0.f; // per-control-block smoothing
    int ctrlLeft_ = 0;

    std::array<Talker, 9> crowd_;
    Talker comm_;
    Reverb rev_;

    // TV
    Biquad tvBp_, tvO1_, tvO2_, tvR_, tvHp_, tvLp_, tvLp2_, tvPk_;
    float tvBase_ = .3f, tvWalk_ = 0.f, tvBaseCur_ = .3f, dTvBase_ = 0.f;
    float swellTimer_ = 20.f, swellT_ = -1.f, swellA_ = 1.f, swellH_ = 1.f, swellR_ = 2.f, swellPk_ = 1.f;
    int swellKind_ = 0;
    float ooh_ = 0.f, roar_ = 0.f, dOoh_ = 0.f, dRoar_ = 0.f;
    float whistleTimer_ = 30.f, wLeft_ = 0.f, wGap_ = 0.f, wAmp_ = 0.f, wAmpT_ = 0.f, dWAmp_ = 0.f;
    int wBlasts_ = 0;
    float wPh_ = 0.f, wTr_ = 0.f, wF_ = 3000.f;

    // fan
    float fanPh_ = 0.f, humPh_ = 0.f;
    Biquad fanLp_;
    // room tone
    OnePole roomLp_;
    // rain outside (heard through the glass and the open street door, from the left)
    float rain_ = 0.f, rainCoef_ = 0.f, gust_ = 1.f, gustTarget_ = 1.f, gustTimer_ = 30.f, gustLeft_ = 0.f, dGust_ = 0.f;
    Biquad rainBody_, rainBody2_, rainHissHp_, rainHissLp_;
    OnePole rainGrain_;
    std::array<Biquad, 3> patter_;
    float dripTimer_ = 1.f, dripPh_ = 0.f, dripF_ = 1600.f, dripA_ = 0.f, dripDk_ = 0.f, dripGlide_ = 1.f;

    // events
    std::array<Bank, 8> banks_;
    std::array<Voice, 12> voices_;
};

AmbienceGen::AmbienceGen(float sr, uint64_t seed, unsigned layers)
    : sr_(sr), layers_(layers), rng_(seed), nz_((uint32_t)(seed * 2654435761u) | 1u) {
    gainCoef_ = smoothCoef(sr, .3f);
    const float dtc = (float)kCtrl / sr;
    kFormant_ = 1.f - std::exp(-dtc / .025f);
    kPitch_ = 1.f - std::exp(-dtc / .03f);
    kAmp_ = 1.f - std::exp(-dtc / .009f);
    rev_.init(sr, 1.0f, 3200.f, 1.1f);
    for (auto& t : crowd_) initTalker(t, false);
    initTalker(comm_, true);

    tvBp_.bandpass(sr, 650.f, .6f);
    tvO1_.bandpass(sr, 420.f, 4.f);
    tvO2_.bandpass(sr, 820.f, 4.f);
    tvR_.bandpass(sr, 1200.f, .5f);
    tvHp_.highpass(sr, 330.f, .8f);
    tvLp_.lowpass(sr, 3600.f, .8f);
    tvLp2_.lowpass(sr, 4200.f, .6f);
    tvPk_.peaking(sr, 1600.f, 1.2f, 4.f);
    swellTimer_ = u(8.f, 30.f);
    whistleTimer_ = u(15.f, 45.f);

    fanLp_.lowpass(sr, 420.f, .8f);
    roomLp_.lp(sr, 260.f);
    rainCoef_ = smoothCoef(sr, 2.f);
    rainBody_.bandpass(sr, 420.f, .45f);
    rainBody2_.lowpass(sr, 1400.f, .6f);
    rainHissHp_.highpass(sr, 1600.f, .7f);
    rainHissLp_.lowpass(sr, 5600.f, .6f);
    rainGrain_.lp(sr, 45.f);
    // Bahçe
    gardenCoef_ = smoothCoef(sr, 2.5f);
    windBp_.bandpass(sr, 800.f, .45f);
    windHp_.highpass(sr, 2800.f, .7f);
    windLp_.lowpass(sr, 7000.f, .6f);
    gullLp_.lowpass(sr, 3200.f, .7f);
    for (int k = 0; k < 2; ++k) {
        chirp_[(size_t)k].timer = u(.3f, 2.5f);
        const float pan = k == 0 ? .3f : .75f;
        chirp_[(size_t)k].gl = std::cos(pan * (float)kPi * .5f), chirp_[(size_t)k].gr = std::sin(pan * (float)kPi * .5f);
        Cricket& c = crick_[(size_t)k];
        c.f = k == 0 ? 4420.f : 4760.f;
        c.period = k == 0 ? .58f : .71f;
        c.pulses = k == 0 ? 3.f : 4.f;
        c.chirpT = u(0.f, c.period);
        c.gl = k == 0 ? .85f : .45f, c.gr = k == 0 ? .45f : .85f;
    }
    gull_.timer = u(6.f, 20.f);
    patter_[0].bandpass(sr, 2300.f, 7.f);
    patter_[1].bandpass(sr, 3500.f, 8.f);
    patter_[2].bandpass(sr, 4700.f, 8.f);
    dripDk_ = std::exp(-1.f / (.028f * sr));
    gustTimer_ = u(15.f, 45.f);

    // Distant table sounds, synthesised with the same code as the effects (in parallel: each
    // variant has its own seed).
    struct Job {
        int bank, kind;
    };
    static const int kCount[8] = {5, 3, 4, 3, 3, 2, 3, 2};
    std::vector<Job> jobs;
    for (int k = 0; k < 8; ++k) {
        banks_[(size_t)k].vars.resize((size_t)kCount[k]);
        for (int v = 0; v < kCount[k]; ++v) jobs.push_back({k, v});
    }
    parallelFor((int)jobs.size(), [&](int j) {
        const Job jb = jobs[(size_t)j];
        Synth s(sr, (seed ^ 0xA5A5F00Dull) + (uint64_t)(jb.bank * 1009 + jb.kind * 17 + 3));
        Buf out;
        switch (jb.bank) {
        case 0: { // tavla dice, sometimes followed by the checkers being moved
            Buf d = sfxDiceRaw(s);
            if (s.chance(.5f)) mixInto(d, ambCheckers(s), s.at(s.u(.7f, 1.2f)), .8f);
            out = distant(std::move(d), sr, 3000.f, -3.f);
            break;
        }
        case 1: out = distant(ambCheckers(s), sr, 3200.f, -3.f); break;
        case 2: { // tea being stirred at another table
            Buf b = s.buf(1.3f);
            glassClinks(s, b, .01f, s.ir(4, 8), s.u(1850.f, 2700.f), 1.f);
            out = distant(std::move(b), sr, 4500.f, -6.f);
            break;
        }
        case 3: out = distant(sfxGlassSet(s), sr, 3800.f, -6.f); break;
        case 4: out = distant(sfxChair(s, jb.kind % 3), sr, 2600.f, -5.f); break;
        case 5: out = distant(ambTray(s), sr, 3500.f, -6.f); break;
        case 6: out = distant(ambCough(s), sr, 2400.f, -8.f); break;
        default: { // okey tiles slapped down at other tables
            Buf b = s.buf(.5f);
            tileOnFelt(s, b, .01f, 1.f, s.u(.9f, 1.1f), .3f);
            for (int k = 0, n = s.ir(1, 3); k < n; ++k)
                tileOnFelt(s, b, s.u(.15f, .45f), s.u(.3f, .7f), s.u(.9f, 1.1f), .3f);
            out = distant(std::move(b), sr, 3000.f, -8.f);
            break;
        }
        }
        banks_[(size_t)jb.bank].vars[(size_t)jb.kind] = std::move(out);
    });
    auto add = [&](int idx, float meanGap, float gLo, float gHi) {
        banks_[(size_t)idx].meanGap = meanGap;
        banks_[(size_t)idx].gLo = gLo;
        banks_[(size_t)idx].gHi = gHi;
        banks_[(size_t)idx].timer = u(1.f, meanGap);
    };
    add(0, 9.f, .35f, 1.f);   // dice
    add(1, 12.f, .3f, .9f);   // checkers
    add(2, 11.f, .3f, .9f);   // spoon clinks
    add(3, 16.f, .3f, .9f);   // glass on saucer
    add(4, 22.f, .4f, 1.f);   // chairs
    add(5, 55.f, .5f, .9f);   // çaycı's tray
    add(6, 70.f, .4f, .8f);   // cough
    add(7, 14.f, .3f, .8f);   // okey tiles from other tables
}

void AmbienceGen::initTalker(Talker& t, bool commentator) {
    t.commentator = commentator;
    const bool young = !commentator && rng_.chance(.15f);
    t.f0Base = commentator ? u(118.f, 135.f) : (young ? u(135.f, 175.f) : u(85.f, 130.f));
    t.f0 = t.f0T = t.f0Base;
    t.breath = commentator ? .08f : u(.08f, .2f);
    t.tilt.lp(sr_, commentator ? 700.f : u(450.f, 650.f));
    const float d = commentator ? 0.f : u(0.f, 1.f); // distance 0..1
    t.dist.lp(sr_, commentator ? 5000.f : 3800.f - 2300.f * d);
    t.gain = commentator ? 1.f : .35f + .65f * (1.f - d) * u(.8f, 1.f);
    const float pan = commentator ? .25f : u(-.85f, .85f);
    t.gl = std::cos((pan + 1.f) * (float)kPi * .25f);
    t.gr = std::sin((pan + 1.f) * (float)kPi * .25f);
    t.send = .45f + .5f * d;
    t.ph = u(0.f, 1.f);
    if (!commentator && rng_.chance(.5f)) {
        startPhrase(t);
        t.syLeft = std::max(1, (int)(u(.2f, 1.f) * (float)t.syLeft));
    } else {
        t.talking = false;
        t.left = u(.1f, 2.5f);
    }
}

void AmbienceGen::startPhrase(Talker& t) {
    t.talking = true;
    t.laugh = !t.commentator && rng_.chance(.05f);
    if (t.laugh)
        t.syN = rng_.range(4, 8);
    else if (t.commentator)
        t.syN = rng_.range(8, 24);
    else
        t.syN = rng_.range(3, 15);
    t.syLeft = t.syN;
    t.syIdx = -1;
    nextSyllable(t);
}

void AmbienceGen::nextSyllable(Talker& t) {
    ++t.syIdx;
    t.syT = 0.f;
    if (t.laugh) {
        t.syDur = u(.12f, .16f);
        t.cons = 3;
        t.consFrac = .3f;
        t.stress = 1.f - .5f * (float)t.syIdx / (float)std::max(1, t.syN);
        t.f1T = 720.f;
        t.f2T = 1250.f;
        t.f3T = 2500.f;
        t.accent = 0.f;
        return;
    }
    t.syDur = t.commentator ? u(.1f, .19f) : u(.13f, .27f);
    const float r = rng_.uniform();
    t.cons = r < .4f ? 0 : (r < .62f ? 1 : 2);
    t.consFrac = t.cons == 1 ? u(.25f, .4f) : u(.15f, .3f);
    t.stress = t.syIdx == 0 ? 1.f : u(.55f, 1.f);
    t.accent = rng_.chance(.45f) ? u(-.06f, .16f) : 0.f;
    t.glide = t.commentator ? u(-.15f, .2f) : u(-.1f, .1f); // pitch movement within the syllable
    const Vowel& v = kVowels[rng_.range(8)];
    const float k = u(.94f, 1.06f);
    t.f1T = v.f1 * k;
    t.f2T = v.f2 * k;
    t.f3T = v.f3 * k;
}

void AmbienceGen::talkerControl(Talker& t, float dt, float excite) {
    float vT = 0.f, nT = 0.f;
    float f1t = t.f1T, f2t = t.f2T, f3t = t.f3T;
    if (!t.talking) {
        t.left -= dt;
        if (t.left <= 0.f) startPhrase(t);
    } else {
        t.syT += dt;
        if (t.syT >= t.syDur) {
            if (--t.syLeft <= 0) {
                t.talking = false;
                t.left = t.commentator ? u(.15f, .7f) * (1.f - .6f * excite) : (t.laugh ? u(1.5f, 4.f) : u(.4f, 3.2f));
            } else {
                nextSyllable(t);
            }
        }
    }
    if (t.talking) {
        const float p = t.syT / t.syDur;
        if (p < t.consFrac) {
            switch (t.cons) {
            case 0: // plosive: closure, then a short burst
                vT = .03f;
                nT = p > t.consFrac * .75f ? .22f : 0.f;
                break;
            case 1: // fricative
                vT = 0.f;
                nT = .28f;
                f1t = 400.f;
                f2t = 2300.f;
                f3t = 3500.f;
                break;
            case 2: // sonorant
                vT = .45f * t.stress;
                nT = .02f;
                f1t = 320.f;
                break;
            default: // "h" of a laugh
                vT = .15f;
                nT = .4f * t.stress;
                break;
            }
        } else {
            const float q = (p - t.consFrac) / (1.f - t.consFrac);
            const float e = q < .75f ? 1.f : 1.f - (q - .75f) / .25f * .6f;
            vT = t.stress * e * (1.f + .8f * excite);
            nT = t.breath * vT * (t.laugh ? 2.5f : 1.f);
        }
        const float ph = (float)t.syIdx / (float)std::max(1, t.syN - 1);
        float f = t.f0Base * (1.f + t.accent + .1f - .22f * ph + t.glide * p);
        if (t.laugh) f *= 1.45f - .3f * ph;
        if (t.commentator) f *= 1.f + .35f * excite;
        t.f0T = f;
    }
    // smoothing (per control block); amplitudes are ramped per sample
    t.f1 += (f1t - t.f1) * kFormant_;
    t.f2 += (f2t - t.f2) * kFormant_;
    t.f3 += (f3t - t.f3) * kFormant_;
    t.jit += (rng_.uniform(-.018f, .018f) - t.jit) * .3f;
    t.f0 += (t.f0T - t.f0) * kPitch_;
    const float vNext = t.vS + (vT - t.vS) * kAmp_, nNext = t.nS + (nT - t.nS) * kAmp_;
    t.dvA = (vNext - t.vA) / (float)kCtrl;
    t.dnA = (nNext - t.nA) / (float)kCtrl;
    t.vS = vNext;
    t.nS = nNext;
    t.silent = !t.talking && t.vA < 1e-4f && t.nA < 1e-4f && vNext < 1e-4f && nNext < 1e-4f;
    if (!t.silent) {
        t.b1.bandpass(sr_, t.f1, t.f1 / 90.f);
        t.b2.bandpass(sr_, t.f2, t.f2 / 120.f);
        t.b3.bandpass(sr_, t.f3, t.f3 / 180.f);
    }
}

inline float AmbienceGen::talkerTick(Talker& t) {
    if (t.silent) return 0.f;
    const float inc = t.f0 * (1.f + t.jit) / sr_;
    t.ph += inc;
    if (t.ph >= 1.f) t.ph -= 1.f;
    const float saw = 2.f * t.ph - 1.f - polyBlep(t.ph, inc);
    const float src = t.tilt.process(saw) * t.vA + nz_.next() * t.nA;
    t.vA += t.dvA;
    t.nA += t.dnA;
    const float y = t.b1.process(src) + .55f * t.b2.process(src) + .25f * t.b3.process(src);
    return t.dist.process(y) * t.gain;
}

void AmbienceGen::spawn(Bank& bk) {
    if (bk.vars.empty()) return;
    for (auto& v : voices_) {
        if (v.b) continue;
        int idx = rng_.range((int)bk.vars.size());
        if ((int)bk.vars.size() > 1 && idx == bk.last) idx = (idx + 1) % (int)bk.vars.size();
        bk.last = idx;
        const float pan = u(-.85f, .85f), g = u(bk.gLo, bk.gHi);
        v.b = &bk.vars[(size_t)idx];
        v.pos = 0;
        v.gl = std::cos((pan + 1.f) * (float)kPi * .25f) * g;
        v.gr = std::sin((pan + 1.f) * (float)kPi * .25f) * g;
        v.send = u(.6f, 1.f) * g;
        return;
    }
}

void AmbienceGen::control() {
    const float dt = (float)kCtrl / sr_;
    if (layers_ & LayerTv) {
        // crowd bed wanders slowly
        tvWalk_ = clampf(tvWalk_ + u(-1.f, 1.f) * .015f, -1.f, 1.f);
        tvBase_ += ((.3f + .1f * tvWalk_) - tvBase_) * .002f;
        dTvBase_ = (tvBase_ - tvBaseCur_) / (float)kCtrl;
        // swells: a near miss ("ooo!") or a goal roar
        float oohT = 0.f, roarT = 0.f;
        if (swellT_ < 0.f) {
            swellTimer_ -= dt;
            if (swellTimer_ <= 0.f) {
                swellT_ = 0.f;
                swellKind_ = rng_.chance(.7f) ? 0 : 1;
                if (swellKind_ == 0) {
                    swellA_ = u(.7f, 1.3f);
                    swellH_ = u(.2f, .6f);
                    swellR_ = u(1.2f, 2.f);
                    swellPk_ = u(.8f, 1.2f);
                } else {
                    swellA_ = u(.4f, .7f);
                    swellH_ = u(2.5f, 4.f);
                    swellR_ = u(2.5f, 4.f);
                    swellPk_ = u(1.f, 1.3f);
                }
            }
        } else {
            swellT_ += dt;
            float e;
            if (swellT_ < swellA_)
                e = (swellT_ / swellA_) * (swellT_ / swellA_);
            else if (swellT_ < swellA_ + swellH_)
                e = 1.f - .1f * std::sin((swellT_ - swellA_) * 7.f) * (float)swellKind_;
            else
                e = std::exp(-(swellT_ - swellA_ - swellH_) / swellR_ * 3.f);
            e *= swellPk_;
            if (swellKind_ == 0)
                oohT = e;
            else
                roarT = e;
            if (swellT_ > swellA_ + swellH_ + swellR_ * 2.5f) {
                swellT_ = -1.f;
                swellTimer_ = u(25.f, 60.f);
            }
        }
        dOoh_ = (oohT - ooh_) / (float)kCtrl;
        dRoar_ = (roarT - roar_) / (float)kCtrl;
        // referee whistle: one to three blasts
        if (wBlasts_ == 0) {
            wAmpT_ = 0.f;
            whistleTimer_ -= dt;
            if (whistleTimer_ <= 0.f) {
                wBlasts_ = rng_.range(1, 3);
                wLeft_ = u(.18f, .6f);
                wGap_ = 0.f;
                wF_ = u(2800.f, 3300.f);
                whistleTimer_ = u(35.f, 90.f);
            }
        } else if (wLeft_ > 0.f) {
            wAmpT_ = 1.f;
            wLeft_ -= dt;
            if (wLeft_ <= 0.f) wGap_ = u(.12f, .25f);
        } else {
            wAmpT_ = 0.f;
            wGap_ -= dt;
            if (wGap_ <= 0.f) {
                if (--wBlasts_ > 0) wLeft_ = u(.12f, .35f);
            }
        }
        const float wNext = wAmp_ + (wAmpT_ - wAmp_) * (1.f - std::exp(-dt / .006f));
        dWAmp_ = (wNext - wAmp_) / (float)kCtrl;
        talkerControl(comm_, dt, std::max(ooh_, roar_));
    }
    if (layers_ & LayerMurmur)
        for (auto& t : crowd_) talkerControl(t, dt, 0.f);
    if ((layers_ & LayerRain) && rain_ > 1e-4f) {
        // gusts: the rain swells against the glass for a few seconds now and then
        if (gustLeft_ > 0.f) {
            gustLeft_ -= dt;
            if (gustLeft_ <= 0.f) gustTarget_ = 1.f;
        } else {
            gustTimer_ -= dt;
            if (gustTimer_ <= 0.f) {
                gustTarget_ = u(1.25f, 1.6f);
                gustLeft_ = u(2.5f, 6.f);
                gustTimer_ = u(20.f, 60.f);
            }
        }
        const float gNext = gust_ + (gustTarget_ - gust_) * (1.f - std::exp(-dt / .9f));
        dGust_ = (gNext - gust_) / (float)kCtrl;
        // the gutter drips onto the tin sill by the door
        dripTimer_ -= dt;
        if (dripTimer_ <= 0.f) {
            dripTimer_ = u(.45f, 1.5f) / std::max(rain_, .3f);
            if (rain_ > .2f) {
                dripA_ = u(.5f, 1.f) * std::min(1.f, rain_ * 1.4f);
                dripPh_ = 0.f;  // starts at a zero crossing: the "plip" has a soft edge, not a click
                dripF_ = u(1250.f, 2100.f);
                dripGlide_ = 1.f - u(2.f, 6.f) / sr_;
            }
        }
    } else {
        dGust_ = 0.f;
    }
    if (layers_ & LayerEvents) {
        for (auto& bk : banks_) {
            bk.timer -= dt;
            if (bk.timer <= 0.f) {
                spawn(bk);
                bk.timer = std::max(1.5f, expo(bk.meanGap));
            }
        }
    }
}

void AmbienceGen::render(float* out, int frames) {
    const float tgt = target.load(std::memory_order_relaxed);
    const float rainTgt = rainTarget.load(std::memory_order_relaxed);
    if (cur_ < 1e-5f && tgt < 1e-5f) {
        cur_ = 0.f;
        std::memset(out, 0, sizeof(float) * 2 * (size_t)frames);
        return;
    }
    // calibrated layer gains (see tools/audio_render.cpp for the measured levels)
    constexpr float kMurmurDry = .28f, kMurmurSend = .41f;
    constexpr float kTvDry = .12f, kTvSend = .12f;
    constexpr float kHum = .0064f, kWhoosh = .048f, kRoom = .035f;
    constexpr float kEvDry = .27f, kEvSend = .27f;
    constexpr float kRainWash = .085f, kRainPatter = .085f, kRainDrip = .008f;
    constexpr float kWet = 1.f;
    const float humInc = 50.f / sr_, fanInc = 2.35f / sr_;
    const float tvGl = std::cos(1.25f * (float)kPi * .25f), tvGr = std::sin(1.25f * (float)kPi * .25f);
    // Bahçe: garden_ 0 inside .. 1 out in the garden (no TV, no fan, an open murmur, a dry air)
    const float gardenTgt = gardenTarget.load(std::memory_order_relaxed), nightTgt = nightTarget.load(std::memory_order_relaxed);
    const float dtS = 1.f / sr_;
    for (int i = 0; i < frames; ++i) {
        if (--ctrlLeft_ < 0) {
            control();
            ctrlLeft_ = kCtrl - 1;
        }
        cur_ += (tgt - cur_) * gainCoef_;
        garden_ += (gardenTgt - garden_) * gardenCoef_;
        night_ += (nightTgt - night_) * gardenCoef_;
        const float inK = 1.f - garden_;
        float l = 0.f, r = 0.f, send = 0.f;
        float lIn = 0.f, rIn = 0.f, sendIn = 0.f;  // (Bahçe) the TV and the fan: inside only
        if (layers_ & LayerMurmur) {
            for (auto& t : crowd_) {
                const float y = talkerTick(t);
                l += y * t.gl * kMurmurDry;
                r += y * t.gr * kMurmurDry;
                send += y * t.send * kMurmurSend;
            }
        }
        if (layers_ & LayerTv) {
            const float n1 = nz_.next(), n2 = nz_.next();
            tvBaseCur_ += dTvBase_;
            ooh_ += dOoh_;
            roar_ += dRoar_;
            wAmp_ += dWAmp_;
            float tv = tvBp_.process(n1) * tvBaseCur_ + (tvO1_.process(n2) + .7f * tvO2_.process(n2)) * ooh_ * 3.f +
                       tvR_.process(n1) * roar_ * 1.5f;
            tv += talkerTick(comm_) * .9f;
            if (wAmp_ > 1e-4f) {
                wTr_ += 33.f / sr_;
                if (wTr_ >= 1.f) wTr_ -= 1.f;
                const float tr = std::sin(kTauF * wTr_);
                wPh_ += (wF_ + 140.f * tr) / sr_;
                if (wPh_ >= 1.f) wPh_ -= 1.f;
                tv += std::sin(kTauF * wPh_) * wAmp_ * (.7f + .3f * tr) * .06f;
            }
            tv = tvPk_.process(tvLp2_.process(tvLp_.process(tvHp_.process(tv))));
            lIn += tv * tvGl * kTvDry;
            rIn += tv * tvGr * kTvDry;
            sendIn += tv * kTvSend;
        }
        if (layers_ & LayerFan) {
            humPh_ += humInc;
            if (humPh_ >= 1.f) humPh_ -= 1.f;
            fanPh_ += fanInc;
            if (fanPh_ >= 1.f) fanPh_ -= 1.f;
            const float wob = 1.f + .12f * std::sin(kTauF * fanPh_);
            const float hum = (std::sin(kTauF * 2.f * humPh_) * .6f + std::sin(kTauF * humPh_) * .25f +
                               std::sin(kTauF * 3.f * humPh_) * .12f) * wob;
            const float bp = .5f + .5f * std::cos(kTauF * 3.f * fanPh_);
            const float whoosh = fanLp_.process(nz_.next()) * (.55f + .45f * bp * bp) * wob;
            const float f = hum * kHum + whoosh * kWhoosh;
            lIn += f;
            rIn += f;
            sendIn += whoosh * kWhoosh * .5f;
        }
        l *= 1.f - .35f * garden_;  // (Bahçe) the murmur, softer and more open outside
        r *= 1.f - .35f * garden_;
        send *= 1.f - .65f * garden_;
        l += lIn * inK, r += rIn * inK, send += sendIn * inK;
        if (garden_ > 1e-4f) gardenTick(dtS, l, r, send);  // Bahçe
        if (layers_ & LayerRoom) {
            const float rt = roomLp_.process(nz_.next()) * kRoom * (1.f - .7f * garden_);
            l += rt;
            r += rt * .9f + nz_.next() * kRoom * .02f;
        }
        if (layers_ & LayerRain) {
            rain_ += (rainTgt - rain_) * rainCoef_;
            gust_ += dGust_;
            if (rain_ > 1e-4f) {
                // a soft broadband wash (pavement, roofs) plus the higher hiss of drops, grainy rather than steady
                const float n1 = nz_.next(), n2 = nz_.next();
                const float grain = 1.f + 2.2f * rainGrain_.process(nz_.next());
                const float wash = rainBody2_.process(rainBody_.process(n1)) * 1.1f + rainHissLp_.process(rainHissHp_.process(n2)) * .5f * grain;
                // drops ticking on the window glass and the sill
                float tick = 0.f;
                if (rng_.uniform() < 90.f * rain_ / sr_) {
                    const float a = u(.2f, 1.f) * (rng_.chance(.5f) ? 1.f : -1.f);
                    tick = a;
                }
                const float pat = patter_[0].process(tick) + patter_[1].process(tick * .8f) + patter_[2].process(tick * .6f);
                float drip = 0.f;
                if (dripA_ > 1e-4f) {
                    dripPh_ += dripF_ / sr_;
                    if (dripPh_ >= 1.f) dripPh_ -= 1.f;
                    drip = std::sin(kTauF * dripPh_) * dripA_;
                    dripA_ *= dripDk_;
                    dripF_ *= dripGlide_;
                }
                float x = (wash * kRainWash * gust_ + pat * kRainPatter) * rain_ + drip * kRainDrip;
                // (ozelgun) under the awning: a dense, dull drumming of drops on the stretched canvas right overhead
                canvas_ += (canvasTarget.load(std::memory_order_relaxed) - canvas_) * rainCoef_;
                if (canvas_ > 1e-3f) {
                    if (!canvasInit_) {
                        canvasBp_[0].bandpass(sr_, 210.f, 2.2f);
                        canvasBp_[1].bandpass(sr_, 620.f, 1.6f);
                        canvasLp_.lowpass(sr_, 2400.f);
                        canvasInit_ = true;
                    }
                    float hit = 0.f;
                    if (rng_.uniform() < 260.f * rain_ / sr_) hit = u(.3f, 1.f) * (rng_.chance(.5f) ? 1.f : -1.f);
                    const float drum = canvasBp_[0].process(hit) * 1.6f + canvasBp_[1].process(hit) * .9f + canvasLp_.process(hit) * .25f;
                    x = x * (1.f - .35f * canvas_) + drum * .12f * rain_ * canvas_;
                }
                l += x * .9f;
                r += x * .55f;
                send += x * .25f;
            }
        }
        if (layers_ & LayerEvents) {
            for (auto& v : voices_) {
                if (!v.b) continue;
                const float x = (*v.b)[v.pos];
                l += x * v.gl * kEvDry;
                r += x * v.gr * kEvDry;
                send += x * v.send * kEvSend;
                if (++v.pos >= v.b->size()) v.b = nullptr;
            }
        }
        float wl, wr;
        rev_.process(send, wl, wr);
        const float wet = kWet * (1.f - .5f * garden_);  // (Bahçe) open air: less of the room's reverb
        out[2 * i] = (l + wl * wet) * cur_;
        out[2 * i + 1] = (r + wr * wet) * cur_;
    }
}

// ---- Bahçe: the garden bed, one sample. Sparrows chirp in short phrases (by day), a gull calls now and then from over
// the sea, the wind rustles the leaves in gusts, and at night two crickets chirp instead of the birds. Modest levels:
// the whole bed stays well under the murmur.
void AmbienceGen::gardenTick(float dt, float& l, float& r, float& send) {
    const float g = garden_, day = 1.f - night_;
    // wind in the leaves: a soft band of noise and a higher rustle, swelling in gusts
    windTimer_ -= dt;
    if (windTimer_ <= 0.f) {
        windTimer_ = u(1.5f, 6.f);
        windGustT_ = u(.25f, 1.15f);
    }
    windGust_ += (windGustT_ - windGust_) * dt * .6f;
    {
        const float body = windBp_.process(nz_.next()) * (.35f + .65f * windGust_);
        const float rustle = windLp_.process(windHp_.process(nz_.next())) * windGust_ * windGust_ * .5f;
        const float w = (body * .028f + rustle * .02f) * g;
        l += w;
        r += w * .9f;
    }
    // sparrows
    if (day > 1e-3f) {
        for (Chirper& c : chirp_) {
            if (c.left > 0.f) {
                c.t += dt;
                const float x = clampf(c.t / c.len, 0.f, 1.f);
                const float f = c.f0 + (c.f1 - c.f0) * x;
                c.ph += f * dt;
                if (c.ph >= 1.f) c.ph -= std::floor(c.ph);
                const float env = std::sin((float)kPi * x);
                const float y = (std::sin(kTauF * c.ph) + .25f * std::sin(2.f * kTauF * c.ph)) * env * env * c.amp * g * day;
                l += y * c.gl;
                r += y * c.gr;
                send += y * .15f;
                c.left -= dt;
                if (c.left <= 0.f) c.timer = c.notes > 0 ? u(.05f, .14f) : u(.6f, 4.f);
            } else {
                c.timer -= dt;
                if (c.timer <= 0.f) {
                    if (c.notes <= 0) c.notes = 2 + (int)u(0.f, 5.f);  // a new phrase
                    --c.notes;
                    c.len = u(.035f, .085f);
                    c.left = c.len;
                    c.t = 0.f;
                    const bool down = u(0.f, 1.f) < .7f;
                    c.f0 = down ? u(4800.f, 6600.f) : u(3000.f, 3800.f);
                    c.f1 = down ? u(2900.f, 3900.f) : u(4800.f, 6000.f);
                    c.amp = u(.006f, .014f);
                }
            }
        }
        // the odd gull, far over the sea
        GullCall& gc = gull_;
        if (gc.left > 0.f) {
            gc.t += dt;
            const float x = clampf(gc.t / gc.len, 0.f, 1.f);
            const float f = gc.base * (1.f + .55f * std::sin((float)kPi * std::min(1.f, x * 1.25f))) * (1.f + .02f * std::sin(kTauF * 28.f * gc.t));
            gc.ph += f * dt;
            if (gc.ph >= 1.f) gc.ph -= std::floor(gc.ph);
            const float env = std::min(1.f, x / .12f) * (1.f - x) * (1.f - x);
            float y = std::sin(kTauF * gc.ph) + .5f * std::sin(2.f * kTauF * gc.ph) + .3f * std::sin(3.f * kTauF * gc.ph) + .15f * nz_.next();
            y = gullLp_.process(y) * env * gc.amp * g * day;
            l += y * gc.gl;
            r += y * gc.gr;
            send += y * .5f;
            gc.left -= dt;
            if (gc.left <= 0.f) gc.timer = gc.notes > 0 ? u(.1f, .22f) : u(14.f, 45.f);
        } else {
            gc.timer -= dt;
            if (gc.timer <= 0.f) {
                if (gc.notes <= 0) {
                    gc.notes = 2 + (int)u(0.f, 3.f);
                    gc.base = u(950.f, 1250.f);
                    const float pan = u(.15f, .85f);
                    gc.gl = std::cos(pan * (float)kPi * .5f), gc.gr = std::sin(pan * (float)kPi * .5f);
                }
                --gc.notes;
                gc.len = u(.22f, .38f);
                gc.left = gc.len;
                gc.t = 0.f;
                gc.amp = u(.004f, .008f);
            }
        }
    }
    // crickets (night)
    if (night_ > 1e-3f) {
        for (Cricket& c : crick_) {
            c.chirpT += dt;
            if (c.chirpT >= c.period) c.chirpT -= c.period;
            const float pos = c.chirpT / c.pulse;  // pulses of `pulse` seconds, on for the first 60 %
            float env = 0.f;
            if (pos < c.pulses) {
                const float fr = pos - std::floor(pos);
                env = fr < .6f ? std::sin((float)kPi * fr / .6f) : 0.f;
            }
            c.ph += c.f * dt;
            if (c.ph >= 1.f) c.ph -= std::floor(c.ph);
            const float y = std::sin(kTauF * c.ph) * env * .0055f * g * night_;
            l += y * c.gl;
            r += y * c.gr;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Old radio: synthesised Turkish folk in makam scales, aksak and 4/4 rhythms, lo-fi chain
// ------------------------------------------------------------------------------------------------

struct Meter {
    const char* name;
    int groups[4]; // in eighths
    int nGroups;
    float eighthLo, eighthHi;
};
const Meter kMeters[] = {
    {"9/8", {2, 2, 2, 3}, 4, .15f, .185f},
    {"7/8", {2, 2, 3, 0}, 3, .16f, .2f},
    {"4/4", {2, 2, 2, 2}, 4, .26f, .31f},
};
constexpr int kNumMeters = 3;

class RadioGen {
public:
    RadioGen(float sr, uint64_t seed);
    void render(float* out, int frames);
    std::atomic<float> target{0.f};
    // true while real recordings play (RecordFx): the generator stops composing and only gives the set's
    // faint reception hiss and crackle, under the songs and in the pauses between them
    std::atomic<bool> recorded{false};
    void setImmediate(float g) {
        target.store(g);
        cur_ = g;
    }

private:
    enum : uint8_t { EvMel, EvTrem, EvDrone, EvDum, EvTek, EvKa, EvPadOn, EvPadOff };
    struct Ev {
        int64_t t;
        uint8_t kind;
        uint8_t idx;
        float freq;
        float amp;
    };
    struct Phrase {
        int n = 0;
        uint8_t dur[40] = {}; // sixteenths
        int8_t deg[40] = {};
    };
    struct PercVoice {
        const Buf* b = nullptr;
        size_t pos = 0;
        float g = 0.f;
    };

    float u(float lo, float hi) { return rng_.uniform(lo, hi); }
    void push(int64_t t, uint8_t kind, uint8_t idx, float freq, float amp);
    void genRhythm(Phrase& p, const Meter& m, int bars);
    void genPitches(Phrase& p, const Makam& mk, int s0, int peak, int end, bool cadence);
    int64_t at16(double pos16) const { return tuneStart_ + (int64_t)std::llround(pos16 * sixteenth_ * sr_); }
    bool groupStart(int pos16) const;
    void emitPhrase(const Phrase& p, const Makam& mk, int& pos16, bool finalHold);
    void emitBarAccomp(int bar16, bool lastBar);
    void startTune();
    void fire(const Ev& e);

    float sr_;
    okey::Rng rng_;
    Noise nz_;
    float cur_ = 0.f, gainCoef_ = 0.f;
    int64_t now_ = 0, tuneEnd_ = 0, tuneStart_ = 0;
    std::vector<Ev> seq_;
    size_t seqPos_ = 0;
    int lastMakam_ = -1, tunes_ = 0;
    float karar_ = 220.f, sixteenth_ = .09f;
    const Meter* meter_ = &kMeters[0];
    int barLen16_ = 18;

    KsString mel_[4]; // two courses x two strings
    KsString drone_[2];
    int course_ = 0;
    std::vector<Buf> dum_, tek_, ka_;
    std::array<PercVoice, 6> perc_;
    // pad drone
    std::vector<float> padTab_;
    float padPh_ = 0.f, padPh2_ = 0.f, padInc_ = 0.f, padAmp_ = 0.f, padTarget_ = 0.f, padLfo_ = 0.f;
    // radio chain
    Biquad hp1_, hp2_, lp1_, lp2_, pk_, hissBp_, crackBp_, outLp_;
    std::vector<float> wow_;
    uint32_t wowW_ = 0;
    float wowPh1_ = 0.f, wowPh2_ = 0.f, driftPh_ = 0.f;
    float crackle_ = 0.f;
    std::vector<float> refl_;
    uint32_t reflW_ = 0;
};

RadioGen::RadioGen(float sr, uint64_t seed) : sr_(sr), rng_(seed), nz_((uint32_t)(seed * 40503u) | 1u) {
    gainCoef_ = smoothCoef(sr, .35f);
    seq_.reserve(4096);
    for (auto& k : mel_) {
        k.init(sr);
        k.setLoss(.22f);
    }
    for (auto& k : drone_) {
        k.init(sr);
        k.setLoss(.35f);
    }
    Synth s(sr, seed ^ 0x5EEDull);
    for (int i = 0; i < 2; ++i) {
        Buf d = percDum(s, 1.f);
        finish(d, sr, -3.f);
        dum_.push_back(std::move(d));
        Buf t = percTek(s, 1.f, .2f);
        finish(t, sr, -5.f);
        tek_.push_back(std::move(t));
        Buf k = percTek(s, 1.f, .45f);
        finish(k, sr, -11.f);
        ka_.push_back(std::move(k));
    }
    // pad: a soft reedy drone (harmonics 1..6)
    padTab_.resize(2048);
    const float hs[6] = {1.f, .55f, .35f, .22f, .12f, .07f};
    for (size_t i = 0; i < padTab_.size(); ++i) {
        const double ph = kTau * (double)i / (double)padTab_.size();
        double v = 0.0;
        for (int h = 0; h < 6; ++h) v += hs[h] * std::sin((h + 1) * ph);
        padTab_[i] = (float)(v * .45);
    }
    hp1_.highpass(sr, 300.f, .54f);
    hp2_.highpass(sr, 300.f, 1.31f);
    lp1_.lowpass(sr, 3500.f, .54f);
    lp2_.lowpass(sr, 3500.f, 1.31f);
    pk_.peaking(sr, 1150.f, 1.1f, 4.f);
    hissBp_.bandpass(sr, 2200.f, .6f);
    crackBp_.bandpass(sr, 1800.f, .9f);
    outLp_.lowpass(sr, 3900.f, .7f);
    wow_.assign(4096, 0.f);
    refl_.assign(4096, 0.f);
    tuneEnd_ = (int64_t)(.4f * sr);
}

void RadioGen::push(int64_t t, uint8_t kind, uint8_t idx, float freq, float amp) {
    if (seq_.size() >= seq_.capacity()) return; // never allocate on the audio thread
    seq_.push_back({t, kind, idx, freq, amp});
}

void RadioGen::genRhythm(Phrase& p, const Meter& m, int bars) {
    // cells in sixteenths for a group of 2 eighths (4) and of 3 eighths (6)
    static const uint8_t c4[6][4] = {{4}, {2, 2}, {2, 1, 1}, {1, 1, 2}, {3, 1}, {1, 1, 1, 1}};
    static const int c4n[6] = {1, 2, 3, 3, 2, 4};
    static const float c4w[6] = {2.5f, 5.f, 2.f, 2.f, 1.2f, 1.f};
    static const uint8_t c6[5][4] = {{6}, {4, 2}, {2, 2, 2}, {2, 4}, {2, 1, 1, 2}};
    static const int c6n[5] = {1, 2, 3, 2, 4};
    static const float c6w[5] = {1.5f, 3.f, 4.f, 2.f, 1.f};
    p.n = 0;
    for (int b = 0; b < bars; ++b) {
        for (int g = 0; g < m.nGroups; ++g) {
            const bool three = m.groups[g] == 3;
            const bool last = b == bars - 1 && g == m.nGroups - 1;
            const int nc = three ? 5 : 6;
            const float* w = three ? c6w : c4w;
            int pick = 0;
            if (last) {
                pick = 0; // phrase ends on a long note
            } else {
                float tot = 0.f;
                for (int i = 0; i < nc; ++i) tot += w[i];
                float r = rng_.uniform() * tot;
                for (pick = 0; pick < nc - 1; ++pick) {
                    r -= w[pick];
                    if (r <= 0.f) break;
                }
            }
            const int cnt = three ? c6n[pick] : c4n[pick];
            for (int i = 0; i < cnt && p.n < 40; ++i) p.dur[p.n++] = three ? c6[pick][i] : c4[pick][i];
        }
    }
}

void RadioGen::genPitches(Phrase& p, const Makam& mk, int s0, int peak, int end, bool cadence) {
    const int n = p.n;
    int d = s0;
    for (int i = 0; i < n; ++i) {
        const float tn = n > 1 ? (float)i / (float)(n - 1) : 1.f;
        const float contour = tn < .5f ? (float)s0 + (float)(peak - s0) * (tn / .5f)
                                       : (float)peak + (float)(end - peak) * ((tn - .5f) / .5f);
        if (i > 0) {
            int step = (int)std::lround(contour - (float)d + u(-1.3f, 1.3f));
            step = std::max(-2, std::min(2, step));
            if (step == 0 && rng_.chance(.4f)) step = rng_.chance(.5f) ? 1 : -1;
            d = std::max(-3, std::min(9, d + step));
        }
        p.deg[i] = (int8_t)d;
    }
    int fixedFrom = n - 1;
    if (cadence && n > mk.cadLen + 1) {
        for (int k = 0; k < mk.cadLen; ++k) p.deg[n - mk.cadLen + k] = (int8_t)mk.cadence[k];
        fixedFrom = n - mk.cadLen;
    } else if (n >= 2) {
        p.deg[n - 1] = (int8_t)end;
        if (p.deg[n - 2] == end) p.deg[n - 2] = (int8_t)(end + (rng_.chance(.5f) ? 1 : -1));
        fixedFrom = n - 2;
    }
    // walk back from the fixed ending so that no leap into it is larger than a third
    for (int i = fixedFrom - 1; i >= 1; --i) {
        const int nxt = p.deg[i + 1];
        if (p.deg[i] > nxt + 2)
            p.deg[i] = (int8_t)(nxt + 2);
        else if (p.deg[i] < nxt - 2)
            p.deg[i] = (int8_t)(nxt - 2);
        else
            break;
    }
}

bool RadioGen::groupStart(int pos16) const {
    int p = pos16 % barLen16_, acc = 0;
    for (int g = 0; g < meter_->nGroups; ++g) {
        if (p == acc) return true;
        acc += 2 * meter_->groups[g];
    }
    return false;
}

void RadioGen::emitPhrase(const Phrase& p, const Makam& mk, int& pos16, bool finalHold) {
    for (int i = 0; i < p.n; ++i) {
        const bool last = i == p.n - 1;
        float durS = (float)p.dur[i] * sixteenth_;
        if (last && finalHold) durS += (float)barLen16_ * sixteenth_;
        const float hz = karar_ * centsToRatio(degreeCents(mk, p.deg[i]));
        const int64_t tn = at16(pos16) + (int64_t)(u(-.004f, .004f) * sr_);
        const float amp = (groupStart(pos16) ? .9f : .7f) * u(.9f, 1.05f);
        if (i > 0 && !last && p.dur[i] >= 2 && rng_.chance(.16f)) { // çarpma: grace from above
            const float gh = karar_ * centsToRatio(degreeCents(mk, p.deg[i] + 1));
            push(tn - (int64_t)(.042f * sr_), EvMel, 0, gh, amp * .5f);
        }
        push(tn, EvMel, 0, hz, amp);
        const bool hold = last && finalHold;
        if (durS >= .24f && (hold || rng_.chance(.65f))) { // mızrap tremolo on long notes
            const float dt = u(.058f, .07f);
            int k = 1;
            for (float tt = dt; tt < durS - .05f; tt += dt, ++k) {
                const float fade = hold ? 1.f - .6f * tt / durS : 1.f;
                push(tn + (int64_t)(tt * sr_), EvTrem, 0, hz, amp * ((k & 1) ? .5f : .4f) * fade);
            }
        }
        pos16 += p.dur[i];
    }
}

void RadioGen::emitBarAccomp(int bar16, bool lastBar) {
    const float kararHz = karar_;
    int acc = 0;
    for (int g = 0; g < meter_->nGroups; ++g) {
        const int gs = bar16 + acc;
        const float a = (g == 0 ? .5f : .34f) * u(.9f, 1.05f);
        if (!lastBar || g == 0) {
            const int64_t t = at16(gs);
            push(t, EvDrone, 0, kararHz, a);
            push(t + (int64_t)(.009f * sr_), EvDrone, 1, kararHz * 1.5f, a * .8f);
            if (meter_->groups[g] == 3 && !lastBar) push(at16(gs + 3), EvDrone, 0, kararHz, a * .6f);
        }
        acc += 2 * meter_->groups[g];
    }
    // darbuka pattern, in eighths
    struct Hit {
        int8_t e;
        uint8_t k;
    };
    static const Hit p98[] = {{0, EvDum}, {2, EvTek}, {4, EvDum}, {6, EvTek}, {7, EvKa}, {8, EvTek}};
    static const Hit p78[] = {{0, EvDum}, {2, EvTek}, {4, EvTek}, {5, EvKa}, {6, EvTek}};
    static const Hit p44[] = {{0, EvDum}, {1, EvTek}, {3, EvTek}, {4, EvDum}, {6, EvTek}};
    const Hit* pat = p98;
    int n = 6;
    if (meter_ == &kMeters[1]) {
        pat = p78;
        n = 5;
    } else if (meter_ == &kMeters[2]) {
        pat = p44;
        n = 5;
    }
    if (lastBar) n = 1;
    bool used[16] = {};
    for (int i = 0; i < n; ++i) {
        used[pat[i].e] = true;
        const float a = (pat[i].k == EvDum ? 1.f : .8f) * u(.85f, 1.05f);
        push(at16(bar16 + 2 * pat[i].e) + (int64_t)(u(-.003f, .003f) * sr_), pat[i].k, 0, 0.f, a);
    }
    if (!lastBar) { // ghost strokes
        const int eighths = barLen16_ / 2;
        for (int e = 0; e < eighths; ++e)
            if (!used[e] && rng_.chance(.3f)) push(at16(bar16 + 2 * e), EvKa, 0, 0.f, u(.4f, .7f));
        if (rng_.chance(.2f)) push(at16(bar16 + barLen16_ - 1), EvKa, 0, 0.f, .5f);
    }
}

void RadioGen::startTune() {
    seq_.clear();
    seqPos_ = 0;
    int mi = rng_.range(kNumMakams);
    if (mi == lastMakam_) mi = (mi + 1) % kNumMakams;
    lastMakam_ = mi;
    const Makam& mk = kMakams[mi];
    meter_ = &kMeters[rng_.range(kNumMeters)];
    const float eighth = u(meter_->eighthLo, meter_->eighthHi);
    sixteenth_ = eighth * .5f;
    barLen16_ = 0;
    for (int g = 0; g < meter_->nGroups; ++g) barLen16_ += 2 * meter_->groups[g];
    static const float kKarar[] = {196.f, 207.65f, 220.f, 233.08f, 246.94f};
    karar_ = kKarar[rng_.range(5)];
    const float gap = tunes_ == 0 ? .3f : u(1.8f, 3.6f);
    ++tunes_;
    tuneStart_ = now_ + (int64_t)(gap * sr_);
    drone_[0].setFreq(karar_);
    drone_[1].setFreq(karar_ * 1.5f);
    drone_[0].setT60(1.3f);
    drone_[1].setT60(1.1f);
    padInc_ = karar_ * .5f / sr_;

    Phrase a, b;
    genRhythm(a, *meter_, 2);
    genPitches(a, mk, mk.aStart, mk.aPeak, mk.aEnd, false);
    genRhythm(b, *meter_, 2);
    genPitches(b, mk, mk.bStart, mk.bPeak, 0, true);

    push(tuneStart_, EvPadOn, 0, 0.f, 0.f);
    int pos16 = 0;
    emitBarAccomp(0, false); // one bar of saz + darbuka before the melody
    pos16 += barLen16_;
    const Phrase* order[8] = {&a, &a, &b, &b, &a, &a, &b, &b};
    for (int s = 0; s < 8; ++s) {
        const int start = pos16;
        const bool fin = s == 7;
        emitBarAccomp(start, false);
        emitBarAccomp(start + barLen16_, false);
        emitPhrase(*order[s], mk, pos16, fin);
        pos16 = start + 2 * barLen16_;
    }
    emitBarAccomp(pos16, true);
    pos16 += barLen16_;
    push(at16(pos16), EvPadOff, 0, 0.f, 0.f);
    tuneEnd_ = at16(pos16) + (int64_t)(1.2f * sr_);
    std::sort(seq_.begin(), seq_.end(), [](const Ev& x, const Ev& y) { return x.t < y.t; });
}

void RadioGen::fire(const Ev& e) {
    switch (e.kind) {
    case EvMel: {
        // the fretting finger stops the previous note; the new one sounds on the other course
        mel_[course_ * 2].setT60(.07f);
        mel_[course_ * 2 + 1].setT60(.07f);
        course_ ^= 1;
        KsString& s0 = mel_[course_ * 2];
        KsString& s1 = mel_[course_ * 2 + 1];
        s0.setFreq(e.freq * centsToRatio(-2.5f));
        s1.setFreq(e.freq * centsToRatio(2.5f));
        s0.setT60(1.8f);
        s1.setT60(1.8f);
        s0.pluck(e.amp, .85f, u(.11f, .15f), nz_);
        s1.pluck(e.amp * .8f, .75f, u(.15f, .19f), nz_);
        break;
    }
    case EvTrem:
        mel_[course_ * 2].pluck(e.amp, .65f, .13f, nz_);
        mel_[course_ * 2 + 1].pluck(e.amp * .8f, .55f, .17f, nz_);
        break;
    case EvDrone: drone_[e.idx & 1].pluck(e.amp, .5f, .2f, nz_); break;
    case EvDum:
    case EvTek:
    case EvKa: {
        const std::vector<Buf>& bank = e.kind == EvDum ? dum_ : (e.kind == EvTek ? tek_ : ka_);
        for (auto& v : perc_) {
            if (v.b) continue;
            v.b = &bank[(size_t)rng_.range((int)bank.size())];
            v.pos = 0;
            v.g = e.amp;
            break;
        }
        break;
    }
    case EvPadOn: padTarget_ = 1.f; break;
    case EvPadOff: padTarget_ = 0.f; break;
    default: break;
    }
}

void RadioGen::render(float* out, int frames) {
    const float tgt = target.load(std::memory_order_relaxed);
    if (cur_ < 1e-5f && tgt < 1e-5f) {
        cur_ = 0.f;
        std::memset(out, 0, sizeof(float) * 2 * (size_t)frames);
        return;
    }
    constexpr float kOut = .52f; // calibrated output level
    const float padCoef = smoothCoef(sr_, 1.2f);
    const uint32_t reflD = (uint32_t)(.017f * sr_);
    const bool rec = recorded.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        while (!rec && seqPos_ < seq_.size() && seq_[seqPos_].t <= now_) fire(seq_[seqPos_++]);
        if (!rec && seqPos_ >= seq_.size() && now_ >= tuneEnd_) startTune();
        cur_ += (tgt - cur_) * gainCoef_;

        float mel = 0.f;
        for (auto& k : mel_) mel += k.tick();
        const float dr = drone_[0].tick() + drone_[1].tick();
        float pc = 0.f;
        for (auto& v : perc_) {
            if (!v.b) continue;
            pc += (*v.b)[v.pos] * v.g;
            if (++v.pos >= v.b->size()) v.b = nullptr;
        }
        padAmp_ += (padTarget_ - padAmp_) * padCoef;
        float pad = 0.f;
        if (padAmp_ > 1e-4f) {
            padPh_ += padInc_;
            if (padPh_ >= 1.f) padPh_ -= 1.f;
            padPh2_ += padInc_ * 1.0035f;
            if (padPh2_ >= 1.f) padPh2_ -= 1.f;
            padLfo_ += .23f / sr_;
            if (padLfo_ >= 1.f) padLfo_ -= 1.f;
            const float n = (float)padTab_.size();
            auto tab = [&](float ph) {
                const float x = ph * n;
                const int i0 = (int)x;
                const float fr = x - (float)i0;
                const float a = padTab_[(size_t)i0 & 2047], b = padTab_[(size_t)(i0 + 1) & 2047];
                return a + (b - a) * fr;
            };
            pad = (tab(padPh_) + tab(padPh2_)) * .5f * padAmp_ * (.8f + .2f * std::sin(kTauF * padLfo_));
        }
        const float m = rec ? 0.f : mel * .5f + dr * .32f + pad * .07f + pc * .3f;

        // wow & flutter: slowly modulated delay
        wow_[wowW_ & 4095] = m;
        wowPh1_ += .45f / sr_;
        if (wowPh1_ >= 1.f) wowPh1_ -= 1.f;
        wowPh2_ += 4.7f / sr_;
        if (wowPh2_ >= 1.f) wowPh2_ -= 1.f;
        // (interpolate relative to the integer write index: no float position that loses precision)
        const float dly = (.006f + .0009f * std::sin(kTauF * wowPh1_) + .00005f * std::sin(kTauF * wowPh2_)) * sr_;
        const uint32_t di = (uint32_t)dly;
        const float fr = dly - (float)di;
        const float w0 = wow_[(wowW_ - di) & 4095], w1 = wow_[(wowW_ - di - 1) & 4095];
        const float wv = w0 + (w1 - w0) * fr;
        ++wowW_;

        // small speaker in a bakelite box: band-pass 300..3500 Hz, a resonance, soft saturation
        float x = pk_.process(lp2_.process(lp1_.process(hp2_.process(hp1_.process(wv)))));
        x = softClip(x * 2.4f) / 2.4f;
        driftPh_ += .07f / sr_;
        if (driftPh_ >= 1.f) driftPh_ -= 1.f;
        const float fade = 1.f + .08f * std::sin(kTauF * driftPh_);
        // reception noise: faint hiss and sparse crackle
        const float hiss = hissBp_.process(nz_.next()) * .006f;
        const float r = nz_.next();
        if (r > 1.f - 2.f * 5.f / sr_) crackle_ = (nz_.next() > 0.f ? 1.f : -1.f) * (.02f + .05f * std::fabs(nz_.next()));
        const float crk = crackBp_.process(crackle_);
        crackle_ *= .55f;
        const float y = outLp_.process(x * fade + hiss + crk) * kOut;

        refl_[reflW_ & 4095] = y;
        const float rf = refl_[(reflW_ - reflD) & 4095];
        ++reflW_;
        out[2 * i] = (y * .82f + rf * .16f) * cur_;
        out[2 * i + 1] = (y + rf * .1f) * cur_;
        ++now_;
    }
}

// ------------------------------------------------------------------------------------------------
// Voices: the regulars' murmur when a speech bubble appears
// ------------------------------------------------------------------------------------------------
//
// No recordings, no TTS: a little formant synthesiser "says" the bubble's Turkish text as friendly gibberish (the
// Animal Crossing idea, slower, softer and closer to real speech). Round 5 (ses) made it a small Klatt-style talker:
//  - Text -> words -> syllables. A syllable is (onset consonant) vowel (coda consonant); word gaps are short (real
//    speech runs words together), commas and full stops are pauses. Interjections are not read as words but made as
//    the sounds they stand for: laughs ("hahaha": breathy pulses that sink, then a breath in), "hah", the scoffing
//    "hıh", sighing "ah"/"of"/"oh", the resigned "eh", "tüh", a dental click "cık", a thinking "hmm", "vay",
//    "aman", "şşt", and the particles "be", "ya", "ha", "yahu", "hadi".
//  - Prosody: a declining baseline per sentence (a little lower each sentence), Turkish word stress on the last
//    syllable (with the common exceptions: şimdi, hadi, nasıl, evet ...), rising pre-nuclear words, a nuclear accent
//    on the word before the verb, then a low tail; a high peak on the syllable before the question particle "mi",
//    a rise at the end of other yes/no questions, the wh-word accented in "ne / kim / nasıl" questions, a continuation
//    rise at a comma, emphatic exclamations (Mahmut's go up), trailing "…" lines sink and slow down; phrase-final
//    lengthening and a breathy, devoiced end of the line; creak at statement ends for the old voices.
//  - Source: a Rosenberg/LF glottal flow derivative (open quotient and return phase per voice: pressed and bright
//    for Mahmut, soft and dark for Rıza), per-period jitter and shimmer, roughness (alternating periods), tremor,
//    and pulse-synchronous breath noise.
//  - Tract: a cascade of four formant resonators with a nasal pole/zero pair; the formants move from consonant
//    loci into each vowel and out again (labial, dental, post-alveolar, velar loci; k/g follow the vowel's
//    frontness); nasal murmurs for m/n with their antiresonance and nasalised vowels next to them.
//  - Consonants: closures with a voice bar for b/d/g/c, bursts shaped by place, aspiration after p/t/k, the
//    affricates ç/c (burst + "ş"), s (high, sharp) and ş (lower, broad) as shaped noise, a tapped r (devoiced at the
//    end of a word), l/y/v as approximants, h as breath through the next vowel.
// Each voice has its own pitch, range, tempo, tract length, source and habits (kVoiceProfiles).
// The whole line is rendered on a worker thread (VoiceWorker) into a preallocated slot and the audio thread only mixes
// finished slots (lock-free hand-over through an atomic state), panned by seat. tools/voice_render.cpp writes the
// lines to WAV files for listening and A/B checks.

constexpr float kVoiceMaxSeconds = 2.5f;   // a murmur never runs longer (the bubble stays longer: that's fine)
constexpr float kVoiceTail = 0.12f;        // room for the filters to ring out
constexpr int kVoiceSlots = 5;
constexpr int kVoiceMaxSyl = 96;
constexpr int kVoiceCount = 7;             // [0] unused, 1..6 (see Audio::speak)
// Level: the voiced parts sit at this RMS (mono, before panning and master); peaks stay below kVoicePeak. The full
// ambience bed runs at about -30 dBFS RMS; after the seat's pan and distance a murmur lands 1..4 dB under it
// (audio_dev::renderVoice measures it).
constexpr float kVoiceRmsDb = -28.f;
constexpr float kVoicePeak = 0.30f;

struct VoiceProfile {
    float f0;         // base pitch (Hz)
    float range;      // depth of the pitch contour (1: accents of about 3..4 semitones)
    float syl;        // base syllable length (s): the tempo
    float formant;    // vocal tract scale (formant frequencies)
    float oq;         // glottal open quotient: .42 pressed, loud .. .65 soft, breathy
    float ta;         // return phase (ms): small = bright and hard (.04), large = dark and soft (.15)
    float breath;     // aspiration noise riding on the voicing
    float nasal;      // constant nasal coupling 0..1 (a nasal voice)
    float gainDb;     // relative level
    float jitter;     // per-period pitch perturbation (fraction of the period, rms)
    float shimmer;    // per-period amplitude perturbation (fraction, rms)
    float rough;      // alternating periods (a rough, harsh voice) 0..1
    float tremor;     // slow pitch wobble (cents; an old voice), with a matching loudness wobble
    float fry;        // creak at statement ends 0..1
    float sigh;       // chance of a sigh after the line
    float exclRise;   // pitch rise at the end of an exclamation (the others fall)
    float wordGap;    // the longest pause between words (s); most are shorter
    float pauseScale; // commas / full stops
    float lp;         // final low-pass (Hz)
    float effort;     // how much stress and exclamations push the voice (louder, brighter)
    float laugh;      // the laugh's pitch over the voice's base
};

// [0] unused, 1 Hacı Rıza, 2 Kel Mahmut, 3 Emekli Nuri, 4 a patron (the crowd), 5 the çaycı (çırak), 6 the player
const VoiceProfile kVoiceProfiles[kVoiceCount] = {
    {},
    // Rıza: low and warm, unhurried, a soft dark source with a little breath, a gentle wobble, a wide easy melody
    {98.f, .9f, .132f, .94f, .62f, .1f, .10f, 0.f, -1.0f, .006f, .05f, 0.f, 9.f, .25f, .12f, .05f, .05f, 1.2f, 4600.f,
     .8f, 1.35f},
    // Mahmut: loud and rough: higher, fast, pressed and bright, harsh alternating periods; exclamations shoot up
    {132.f, 1.3f, .088f, 1.0f, .42f, .04f, .05f, 0.f, 2.0f, .011f, .09f, .38f, 0.f, .1f, 0.f, .3f, .03f, .8f, 6600.f,
     1.3f, 1.5f},
    // Nuri: older and slower: a shaky (tremor), breathy, nasal voice, narrow melody; creaks at the ends and sighs
    {114.f, .62f, .155f, .97f, .6f, .12f, .17f, .45f, 0.f, .014f, .10f, .08f, 24.f, .55f, .5f, .05f, .085f, 1.35f,
     4800.f, .7f, 1.25f},
    // a patron across the room: muffled and quiet (lineProfile gives each one his own pitch, size and tempo)
    {116.f, .9f, .1f, 1.f, .55f, .1f, .10f, 0.f, -8.f, .008f, .06f, .05f, 0.f, .1f, 0.f, .1f, .05f, 1.f, 2600.f, 1.f,
     1.4f},
    // the çaycı: a lad, quick and bright, a lively melody
    {182.f, 1.2f, .084f, 1.12f, .55f, .06f, .08f, 0.f, -2.f, .005f, .04f, 0.f, 0.f, 0.f, 0.f, .22f, .035f, .85f, 7000.f,
     1.1f, 1.6f},
    // the player: a plain young man's voice, medium pace
    {118.f, 1.f, .104f, 1.02f, .52f, .08f, .07f, 0.f, -1.5f, .006f, .05f, 0.f, 0.f, .15f, 0.f, .12f, .045f, 1.f,
     6000.f, 1.f, 1.45f},
};

enum : uint8_t { VC_None = 0, VC_Stop, VC_Fric, VC_Son, VC_H };
enum : uint8_t { VS_Statement = 0, VS_Question, VS_Exclaim, VS_Trail };
enum : uint16_t {
    VF_WordEnd = 1, VF_SentEnd = 2, VF_Hum = 4, VF_Pause = 8, VF_Sigh = 16, VF_Laugh = 32, VF_Click = 64,
    VF_Inhale = 128, VF_PhraseEnd = 256, VF_Stress = 512, VF_Hiss = 1024
};
// What a word is (interjections are made as the sounds they stand for; particles and question words shape the tune)
enum : uint8_t {
    W_Plain = 0, W_Laugh, W_Hah, W_Scoff, W_Ah, W_Of, W_Oh, W_Eh, W_Tuh, W_Click, W_Hum, W_Hiss, W_Vay, W_Aman,
    W_Hadi, W_Be, W_Ya, W_Ha, W_Mi, W_Wh
};

// One planned event: a syllable, a pause or a non-word sound (sigh, laugh pulse, click, breath in, hiss).
struct VSyl {
    uint16_t flags = 0;
    uint8_t vowel = 0;              // kVowels index
    uint8_t onset = VC_None, coda = VC_None;
    char32_t onCh = 0, codaCh = 0;
    uint8_t sent = VS_Statement;
    uint8_t longV = 0;              // repeated vowel letters ("Gooool"), ğ
    uint8_t word = 0, wk = W_Plain; // word index in the line, its kind
    uint8_t wpos = 0, wlen = 1;     // syllable index within the word, the word's syllable count
    uint8_t wstress = 0;            // the stressed syllable's index within the word
    float dur = 0.f;                // seconds
    float f0a = 1.f, f0m = 1.f, f0b = 1.f; // pitch multipliers at the start / middle / end
    float amp = 1.f;
    float breath = 0.f;             // extra aspiration (interjections, laughs)
    float nasal = 0.f;              // extra nasal coupling ("hıh", "hmm")
};

struct VoicePlan {
    std::array<VSyl, kVoiceMaxSyl> s;
    int n = 0;
    float total = 0.f;
};

// UTF-8 -> lower-case Turkish code points, one at a time.
char32_t nextCodepoint(const std::string& t, size_t& i) {
    const unsigned char c = (unsigned char)t[i];
    char32_t cp = c;
    int n = 1;
    if (c >= 0xF0 && i + 3 < t.size()) {
        cp = ((c & 7u) << 18) | (((unsigned char)t[i + 1] & 63u) << 12) | (((unsigned char)t[i + 2] & 63u) << 6) |
             ((unsigned char)t[i + 3] & 63u);
        n = 4;
    } else if (c >= 0xE0 && i + 2 < t.size()) {
        cp = ((c & 15u) << 12) | (((unsigned char)t[i + 1] & 63u) << 6) | ((unsigned char)t[i + 2] & 63u);
        n = 3;
    } else if (c >= 0xC0 && i + 1 < t.size()) {
        cp = ((c & 31u) << 6) | ((unsigned char)t[i + 1] & 63u);
        n = 2;
    }
    i += (size_t)n;
    switch (cp) {
    case U'I': return U'ı';
    case U'İ': return U'i';
    case U'Ç': return U'ç';
    case U'Ğ': return U'ğ';
    case U'Ö': return U'ö';
    case U'Ş': return U'ş';
    case U'Ü': return U'ü';
    case U'â': case U'Â': return U'a';
    case U'î': case U'Î': return U'i';
    case U'û': case U'Û': return U'u';
    default: break;
    }
    if (cp >= U'A' && cp <= U'Z') cp += 32;
    return cp;
}

int vowelIndex(char32_t c) {
    switch (c) {
    case U'a': return 0;
    case U'e': return 1;
    case U'ı': return 2;
    case U'i': return 3;
    case U'o': return 4;
    case U'ö': return 5;
    case U'u': return 6;
    case U'ü': return 7;
    default: return -1;
    }
}
inline bool frontVowel(int v) { return v == 1 || v == 3 || v == 5 || v == 7; }

uint8_t consonantClass(char32_t c) {
    switch (c) {
    case U'b': case U'c': case U'ç': case U'd': case U'g': case U'k': case U'p': case U't': case U'q': return VC_Stop;
    case U'f': case U'j': case U's': case U'ş': case U'v': case U'z': case U'x': case U'w': return VC_Fric;
    case U'h': return VC_H;
    case U'l': case U'm': case U'n': case U'r': case U'y': return VC_Son;
    default: return VC_None;
    }
}

const char* digitWord(char32_t d) {
    static const char* const k[10] = {"sıfır", "bir", "iki", "üç", "dört", "beş", "altı", "yedi", "sekiz", "dokuz"};
    return (d >= U'0' && d <= U'9') ? k[d - U'0'] : "";
}

// A word of the line (letters only, lower case), what follows it and what it is.
struct VWord {
    std::array<char32_t, 24> c{};
    int n = 0;
    uint8_t after = 0;            // 0 a space, 1 a comma (phrase break), 2 the end of a sentence
    uint8_t sent = VS_Statement;  // the type of the sentence it is in
    uint8_t kind = W_Plain;
    int8_t stress = -1;           // the stressed syllable counted from the start (-1: the last one)
    uint8_t reps = 0;             // laugh pulses, hum length
};

bool wordIs(const VWord& w, const char32_t* s) {
    int i = 0;
    for (; s[i]; ++i)
        if (i >= w.n || w.c[(size_t)i] != s[i]) return false;
    return i == w.n;
}
bool wordIsAny(const VWord& w, std::initializer_list<const char32_t*> l) {
    for (const char32_t* s : l)
        if (wordIs(w, s)) return true;
    return false;
}
bool wordStarts(const VWord& w, const char32_t* s) {
    int i = 0;
    for (; s[i]; ++i)
        if (i >= w.n || w.c[(size_t)i] != s[i]) return false;
    return true;
}

// What kind of word: an interjection, a particle, the question particle, a question word, or a plain word (with its
// stress when it is not on the last syllable). `first`: the first word of its sentence.
void classifyWord(VWord& w, bool first) {
    // squeeze repeated letters ("offf", "hmmm", "yaaa", "şşşt")
    VWord q;
    for (int i = 0; i < w.n; ++i)
        if (q.n == 0 || q.c[(size_t)(q.n - 1)] != w.c[(size_t)i]) q.c[(size_t)q.n++] = w.c[(size_t)i];
    // laughs: (h|k) + the same vowel, twice or more ("haha", "hehehe", "hahah", "kahkah")
    {
        int n = w.n;
        while (n > 0 && w.c[(size_t)(n - 1)] == U'h') --n;
        int reps = 0;
        char32_t vw = 0;
        bool ok = n >= 4;
        for (int i = 0; ok && i + 1 < n; i += 2) {
            const char32_t h = w.c[(size_t)i], v = w.c[(size_t)(i + 1)];
            if ((h != U'h' && h != U'k') || vowelIndex(v) < 0 || (vw && v != vw)) ok = false;
            vw = v;
            ++reps;
        }
        if (ok && (n & 1) == 0 && reps >= 2) {
            w.kind = W_Laugh;
            w.reps = (uint8_t)std::min(5, reps);
            return;
        }
    }
    if (wordIsAny(q, {U"hah", U"hoh"}) || (first && wordIs(q, U"ha"))) w.kind = W_Hah;
    else if (wordIsAny(q, {U"hıh", U"hı", U"ıh", U"pöh", U"püh", U"hıhı"})) w.kind = W_Scoff;
    else if (wordIsAny(q, {U"ah", U"aha"})) w.kind = W_Ah;
    else if (wordIsAny(q, {U"of", U"öf", U"uf", U"üf", U"pof"})) w.kind = W_Of;
    else if (wordIsAny(q, {U"oh", U"o"})) w.kind = W_Oh;
    else if (wordIsAny(q, {U"eh", U"e", U"ehe"})) w.kind = W_Eh;
    else if (wordIsAny(q, {U"tüh", U"tuh", U"töh", U"tü"})) w.kind = W_Tuh;
    else if (wordIsAny(q, {U"cık", U"tsk", U"ck", U"cik"})) w.kind = W_Click;
    else if (wordIsAny(q, {U"hm", U"hım", U"ım", U"m", U"hmh"})) {
        w.kind = W_Hum;
        int m = 0;
        for (int i = 0; i < w.n; ++i) m += w.c[(size_t)i] == U'm';
        w.reps = (uint8_t)std::min(3, std::max(0, m - 1));
    } else if (wordIsAny(q, {U"şt", U"ş", U"pşt", U"pışt", U"pst", U"st", U"sst", U"pss"})) w.kind = W_Hiss;
    else if (wordIsAny(q, {U"vay", U"vah", U"hay"})) w.kind = W_Vay;
    else if (wordIs(q, U"aman")) w.kind = W_Aman;
    else if (wordIsAny(q, {U"hadi", U"haydi", U"hayda", U"hayde", U"hade"})) w.kind = W_Hadi;
    else if (wordIs(q, U"be")) w.kind = W_Be;
    else if (wordIsAny(q, {U"ya", U"yahu", U"yav", U"yaw"})) w.kind = W_Ya;
    else if (wordIs(q, U"ha")) w.kind = W_Ha;
    else if (wordIsAny(w, {U"mi", U"mı", U"mu", U"mü"}) || wordStarts(w, U"misin") || wordStarts(w, U"mısın") ||
             wordStarts(w, U"musun") || wordStarts(w, U"müsün") || wordStarts(w, U"miyi") || wordStarts(w, U"mıyı") ||
             wordStarts(w, U"muyu") || wordStarts(w, U"müyü") || wordStarts(w, U"midir") || wordStarts(w, U"mıdır") ||
             wordStarts(w, U"miydi") || wordStarts(w, U"mıydı") || wordStarts(w, U"muydu") || wordStarts(w, U"müydü"))
        w.kind = W_Mi;
    else if (wordIsAny(w, {U"ne", U"neyi", U"neden", U"niye", U"niçin", U"kim", U"kimi", U"kimin", U"nasıl", U"nerede",
                           U"nereye", U"nereden", U"nerde", U"hangi", U"hangisi", U"kaç", U"kaçta", U"neresi"}))
        w.kind = W_Wh;
    // Turkish stress falls on the last syllable, except in these common words (and the place it falls on)
    if (w.kind == W_Hadi ||
        wordIsAny(w, {U"şimdi", U"nasıl", U"evet", U"hayır", U"belki", U"sonra", U"yine", U"gene", U"işte", U"ama",
                      U"ancak", U"nerede", U"nereye", U"nereden", U"niye", U"neden", U"şöyle", U"böyle", U"öyle",
                      U"yani", U"lütfen", U"yarın", U"bugün", U"abi", U"amca", U"hani", U"sanki", U"bazen",
                      U"hemen", U"çünkü", U"tabii", U"aslında", U"merhaba", U"aferin", U"hayırdır", U"hayırlı",
                      U"maşallah", U"inşallah", U"oraya", U"şuraya", U"buraya", U"burada", U"orada", U"şurada"}))
        w.stress = 0;
    else if (wordIsAny(w, {U"galiba", U"efendim", U"kardeşim"}))
        w.stress = 1;
}

// Text -> syllables and pauses, with durations, the pitch contour and loudness.
void planSpeech(const std::string& text, const VoiceProfile& vp, okey::Rng& rng, VoicePlan& P) {
    P.n = 0;
    P.total = 0.f;
    // ---- 1. text -> words, phrase breaks and sentence ends
    constexpr int kMaxWords = 48;
    VWord words[kMaxWords];
    int nw = 0;
    VWord cur;
    auto flush = [&]() {
        if (cur.n > 0 && nw < kMaxWords) words[nw++] = cur;
        cur = VWord();
    };
    auto sentEnd = [&](uint8_t t) {
        flush();
        if (nw == 0) return;
        VWord& w = words[nw - 1];
        if (w.after < 2) {
            w.after = 2;
            w.sent = t;
        } else if (w.sent == VS_Statement || (w.sent == VS_Exclaim && t == VS_Question)) {
            w.sent = t; // "?!" is a question; "!.." stays an exclamation
        }
    };
    int dots = 0;
    for (size_t i = 0; i < text.size();) {
        const char32_t c = nextCodepoint(text, i);
        if (c != U'.') {
            if (dots >= 2) sentEnd(VS_Trail);
            else if (dots == 1) sentEnd(VS_Statement);
            dots = 0;
        }
        if (c >= U'0' && c <= U'9') {
            flush(); // "1 0 1": each digit a word
            const std::string dw = digitWord(c);
            for (size_t k = 0; k < dw.size();) cur.c[(size_t)cur.n++] = nextCodepoint(dw, k);
            flush();
        } else if (vowelIndex(c) >= 0 || consonantClass(c) != VC_None || c == U'ğ') {
            if (cur.n < 23) cur.c[(size_t)cur.n++] = c;
        } else if (c == U'.') {
            ++dots;
        } else if (c == U'?') {
            sentEnd(VS_Question);
        } else if (c == U'!') {
            sentEnd(VS_Exclaim);
        } else if (c == U'…') {
            sentEnd(VS_Trail);
        } else if (c == U',' || c == U';' || c == U':' || c == U'—' || c == U'–') {
            flush();
            if (nw > 0 && words[nw - 1].after == 0) words[nw - 1].after = 1;
        } else if (c == U'\'' || c == U'’') {
            // "101'i", "Mahmut'un": the suffix stays with its word
        } else {
            flush();
        }
    }
    if (dots >= 2) sentEnd(VS_Trail);
    else if (dots == 1) sentEnd(VS_Statement);
    flush();
    if (nw == 0) return;
    if (words[nw - 1].after < 2) words[nw - 1].after = 2;
    for (int k = nw - 1, t = VS_Statement; k >= 0; --k) { // every word learns its sentence's type
        if (words[k].after == 2) t = words[k].sent;
        words[k].sent = (uint8_t)t;
    }
    for (int k = 0; k < nw; ++k) classifyWord(words[k], k == 0 || words[k - 1].after == 2);

    // ---- 2. words -> syllables (onset = the consonant just before the vowel, the rest close the previous one)
    auto push = [&](const VSyl& v) -> VSyl* {
        if (P.n >= kVoiceMaxSyl - 3) return nullptr;
        P.s[(size_t)P.n] = v;
        return &P.s[(size_t)P.n++];
    };
    auto pause = [&](float seconds) {
        if (P.n == 0 || seconds <= 0.f) return;
        VSyl& last = P.s[(size_t)(P.n - 1)];
        if (last.flags & VF_Pause) {
            last.dur = std::max(last.dur, seconds);
            return;
        }
        VSyl v;
        v.flags = VF_Pause;
        v.dur = seconds;
        v.sent = last.sent;
        push(v);
    };
    for (int wi = 0; wi < nw && P.n < kVoiceMaxSyl - 4; ++wi) {
        const VWord& w = words[wi];
        const int first = P.n;
        VSyl base;
        base.word = (uint8_t)std::min(wi, 255);
        base.wk = w.kind;
        base.sent = w.sent;
        if (w.kind == W_Laugh) {
            const int vi = vowelIndex(w.c[1]);
            for (int k = 0; k < w.reps; ++k) {
                VSyl v = base;
                v.flags = VF_Laugh;
                v.vowel = (uint8_t)std::max(0, vi);
                v.onset = VC_H;
                v.onCh = U'h';
                v.wpos = (uint8_t)k;
                v.breath = .45f;
                if (!push(v)) break;
                if (k + 1 < w.reps) pause(rng.uniform(.025f, .045f));
            }
            if (w.reps >= 3) { // a breath in after a good laugh
                pause(.04f);
                VSyl v = base;
                v.flags = VF_Inhale;
                v.vowel = 2;
                v.dur = rng.uniform(.15f, .2f);
                push(v);
            }
        } else if (w.kind == W_Click) {
            VSyl v = base;
            v.flags = VF_Click;
            v.dur = .09f;
            push(v);
        } else if (w.kind == W_Hiss) {
            VSyl v = base;
            v.flags = VF_Hiss;
            v.onCh = U'ş';
            for (int i = 0; i < w.n; ++i)
                if (w.c[(size_t)i] == U's') v.onCh = U's';
            const char32_t e = w.c[(size_t)(w.n - 1)];
            if (e == U't') {
                v.coda = VC_Stop;
                v.codaCh = U't';
            }
            push(v);
        } else if (w.kind == W_Hum) {
            VSyl v = base;
            v.flags = VF_Hum;
            v.vowel = 2;
            v.longV = w.reps;
            v.nasal = 1.f;
            push(v);
        } else if (w.kind == W_Scoff) { // "hıh": a nasal snort with a scrap of voice
            VSyl v = base;
            v.vowel = 2;
            v.onset = VC_H;
            v.onCh = U'h';
            v.coda = VC_H;
            v.codaCh = U'h';
            v.nasal = 1.f;
            v.breath = .6f;
            push(v);
        } else {
            // plain letters (the other interjections and particles too: their tune comes in step 4)
            char32_t pend[4] = {};
            int np = 0;
            for (int i = 0; i < w.n; ++i) {
                const char32_t c = w.c[(size_t)i];
                const int vi = vowelIndex(c);
                if (vi >= 0) {
                    // the same vowel again ("Gooool", "Göstergeee"): one long vowel
                    if (np == 0 && P.n > first && P.s[(size_t)(P.n - 1)].vowel == vi &&
                        P.s[(size_t)(P.n - 1)].coda == VC_None) {
                        VSyl& l = P.s[(size_t)(P.n - 1)];
                        l.longV = (uint8_t)std::min(4, l.longV + 1);
                        continue;
                    }
                    VSyl v = base;
                    v.vowel = (uint8_t)vi;
                    if (np > 0) {
                        v.onCh = pend[np - 1];
                        v.onset = consonantClass(v.onCh);
                        if (np > 1 && P.n > first && P.s[(size_t)(P.n - 1)].coda == VC_None) {
                            P.s[(size_t)(P.n - 1)].codaCh = pend[0];
                            P.s[(size_t)(P.n - 1)].coda = consonantClass(pend[0]);
                        }
                    }
                    np = 0;
                    if (!push(v)) break;
                } else if (c == U'ğ') { // yumuşak g: the vowel before it gets longer
                    if (P.n > first) P.s[(size_t)(P.n - 1)].longV = (uint8_t)std::min(4, P.s[(size_t)(P.n - 1)].longV + 1);
                } else if (consonantClass(c) != VC_None) {
                    if (np < 4) pend[np++] = c;
                    else pend[3] = c;
                }
            }
            if (P.n > first) {
                VSyl& last = P.s[(size_t)(P.n - 1)];
                if (np > 0 && last.coda == VC_None) {
                    last.coda = consonantClass(pend[0]);
                    last.codaCh = pend[np - 1] == U'h' ? U'h' : pend[0];
                    if (pend[np - 1] == U'h') last.coda = VC_H;
                }
            } else if (np > 0) { // no vowel at all ("Hmm", "Grr"): a hummed syllable
                VSyl v = base;
                v.flags = VF_Hum;
                v.vowel = 2;
                v.nasal = 1.f;
                push(v);
            }
        }
        // the word's syllables learn their place in it
        int nsyl = 0;
        for (int k = first; k < P.n; ++k)
            if (!(P.s[(size_t)k].flags & VF_Pause)) ++nsyl;
        const int st = w.stress < 0 ? nsyl - 1 : std::min((int)w.stress, nsyl - 1);
        for (int k = first, j = 0; k < P.n; ++k) {
            VSyl& v = P.s[(size_t)k];
            if (v.flags & VF_Pause) continue;
            v.wpos = (uint8_t)std::min(j, 255);
            v.wlen = (uint8_t)std::min(nsyl, 255);
            v.wstress = (uint8_t)std::max(0, st);
            if (j == st && w.kind != W_Mi && w.kind != W_Be && w.kind != W_Ya && w.kind != W_Ha) v.flags |= VF_Stress;
            ++j;
        }
        if (P.n > first) {
            int k = P.n - 1;
            while (k > first && (P.s[(size_t)k].flags & VF_Pause)) --k;
            P.s[(size_t)k].flags |= VF_WordEnd;
            if (w.after == 1) P.s[(size_t)k].flags |= VF_PhraseEnd;
            if (w.after == 2) P.s[(size_t)k].flags |= VF_SentEnd | VF_PhraseEnd;
        }
        // the gap after it: words run together inside a phrase (now and then a short break), commas and full stops pause
        if (w.after == 2) pause((w.sent == VS_Trail ? .36f : .24f) * vp.pauseScale * rng.uniform(.85f, 1.15f));
        else if (w.after == 1) pause(.15f * vp.pauseScale * rng.uniform(.85f, 1.2f));
        else if (rng.chance(.45f)) pause(vp.wordGap * rng.uniform(.2f, 1.f));
    }
    while (P.n > 0 && (P.s[(size_t)(P.n - 1)].flags & VF_Pause)) --P.n; // no trailing silence
    if (P.n == 0) return;
    P.s[(size_t)(P.n - 1)].flags |= VF_SentEnd | VF_PhraseEnd;

    // ---- 3. durations
    static const float kVowelLen[8] = {1.08f, 1.04f, .88f, .9f, 1.05f, 1.f, .92f, .92f}; // a e ı i o ö u ü
    float total = 0.f;
    for (int k = 0; k < P.n; ++k) {
        VSyl& v = P.s[(size_t)k];
        if (v.flags & VF_Pause) {
            total += v.dur;
            continue;
        }
        if (v.flags & (VF_Click | VF_Inhale)) {
            total += v.dur;
            continue;
        }
        float d = vp.syl * rng.uniform(.9f, 1.1f) * kVowelLen[v.vowel];
        if (v.onset != VC_None) d *= 1.06f;
        if (v.coda != VC_None) d *= 1.18f;
        if (v.flags & VF_Stress) d *= 1.1f;
        if (v.flags & VF_SentEnd) d *= v.sent == VS_Trail ? 1.7f : 1.42f; // phrase-final lengthening
        else if (v.flags & VF_PhraseEnd) d *= 1.3f;
        if (v.sent == VS_Exclaim && !(v.flags & VF_SentEnd)) d *= .92f;
        if (v.sent == VS_Trail) d *= 1.08f;
        switch (v.wk) {
        case W_Laugh: d = vp.syl * rng.uniform(.82f, .98f); break;
        case W_Hah: d *= 1.15f; break;
        case W_Scoff: d = .2f + .2f * vp.syl; break;
        case W_Ah: case W_Of: case W_Oh: d *= 1.75f; break;
        case W_Eh: d *= 1.15f; break;
        case W_Tuh: d *= 1.5f; break;
        case W_Hiss: d = .28f * rng.uniform(.85f, 1.15f); break;
        case W_Hum: d = .32f + .1f * (float)v.longV; break;
        case W_Vay: d *= 1.55f; break;
        case W_Aman: if (v.wpos == 0) d *= 1.6f; break;
        case W_Hadi: if (v.wpos == 0) d *= 1.1f; break;
        case W_Be: d *= .8f; break;
        case W_Ya: if (v.flags & VF_SentEnd) d *= 1.3f; break;
        default: break;
        }
        if (v.flags & VF_Hum && v.wk != W_Hum) d *= 1.3f;
        if (!(v.flags & VF_Hum)) d *= 1.f + .55f * (float)v.longV;
        v.dur = d;
        total += d;
    }
    // too long: talk a little faster, then stop at a word boundary and let the last syllable fall
    const float cap = kVoiceMaxSeconds - .05f;
    if (total > cap) {
        const float k = std::max(.8f, cap / total);
        total = 0.f;
        for (int i = 0; i < P.n; ++i) {
            P.s[(size_t)i].dur *= k;
            total += P.s[(size_t)i].dur;
        }
    }
    if (total > cap) {
        float t = 0.f;
        int keep = 0, lastWordEnd = -1;
        for (int i = 0; i < P.n; ++i) {
            if (t + P.s[(size_t)i].dur > cap) break;
            t += P.s[(size_t)i].dur;
            keep = i + 1;
            if (!(P.s[(size_t)i].flags & VF_Pause) && (P.s[(size_t)i].flags & VF_WordEnd)) lastWordEnd = i;
        }
        P.n = lastWordEnd >= 0 ? lastWordEnd + 1 : std::max(1, keep);
        VSyl& l = P.s[(size_t)(P.n - 1)];
        l.flags |= VF_SentEnd | VF_PhraseEnd;
        const uint8_t cutSent = l.sent;
        if (cutSent == VS_Question) // the question mark is cut off: no rise
            for (int i = P.n - 1; i >= 0 && P.s[(size_t)i].sent == VS_Question; --i) {
                if (i < P.n - 1 && (P.s[(size_t)i].flags & VF_SentEnd)) break;
                P.s[(size_t)i].sent = VS_Statement;
            }
        total = 0.f;
        for (int i = 0; i < P.n; ++i) total += P.s[(size_t)i].dur;
    }
    // a sigh after the line (Nuri, sometimes Rıza), if there is room
    if (vp.sigh > 0.f && P.n > 0 && P.n < kVoiceMaxSyl - 2 && rng.chance(vp.sigh) && total + .55f < kVoiceMaxSeconds) {
        VSyl g;
        g.flags = VF_Pause;
        g.dur = .12f;
        P.s[(size_t)P.n++] = g;
        VSyl s;
        s.flags = VF_Sigh | VF_SentEnd;
        s.vowel = 0;
        s.dur = rng.uniform(.34f, .42f);
        s.amp = .55f;
        P.s[(size_t)P.n++] = s;
        total += g.dur + s.dur;
    }
    P.total = total;

    // ---- 4. the tune (semitones over the voice's base) and loudness, sentence by sentence
    float tAt[kVoiceMaxSyl];
    {
        float t = 0.f;
        for (int k = 0; k < P.n; ++k) {
            tAt[k] = t + .5f * P.s[(size_t)k].dur;
            t += P.s[(size_t)k].dur;
        }
    }
    auto isSyl = [&](int k) { return !(P.s[(size_t)k].flags & (VF_Pause | VF_Sigh | VF_Click | VF_Inhale)); };
    auto content = [](uint8_t wk) { return wk == W_Plain || wk == W_Wh || wk == W_Hadi || wk == W_Aman; };
    float A[kVoiceMaxSyl], M[kVoiceMaxSyl], B[kVoiceMaxSyl];
    int sentIdx = 0;
    for (int a = 0; a < P.n;) {
        int b = a;
        while (b < P.n - 1 && !(P.s[(size_t)b].flags & VF_SentEnd)) ++b;
        // [a, b]: one sentence (the sigh after the line is a sentence of its own)
        int sa = -1, sb = -1;
        for (int k = a; k <= b; ++k)
            if (isSyl(k)) {
                if (sa < 0) sa = k;
                sb = k;
            }
        if (sa < 0) {
            a = b + 1;
            continue;
        }
        const uint8_t T = P.s[(size_t)sb].sent;
        const float R = 1.3f * vp.range * (T == VS_Exclaim ? 1.35f : T == VS_Trail ? .8f : 1.f); // ~semitones
        const float t0 = tAt[sa], t1 = std::max(tAt[sb], t0 + 1e-3f);
        const float down = -std::min(2.f, .9f * (float)sentIdx) * R; // each sentence starts a little lower
        // the phrases of the sentence (split at commas)
        for (int pa = sa; pa <= sb;) {
            int pb = pa;
            while (pb < sb && !(P.s[(size_t)pb].flags & VF_PhraseEnd)) ++pb;
            while (pb > pa && !isSyl(pb)) --pb;
            const bool lastPhrase = pb >= sb;
            // the words in the phrase; the nucleus (the accented word)
            int firstW = P.s[(size_t)pa].word, lastW = P.s[(size_t)pb].word;
            int miW = -1, whW = -1, nContent = 0, lastContent = -1, prevContent = -1, firstContent = -1;
            for (int k = pa; k <= pb; ++k) {
                if (!isSyl(k)) continue;
                const VSyl& v = P.s[(size_t)k];
                if (k > pa && v.word == P.s[(size_t)(k - 1)].word) continue;
                if (v.wk == W_Mi && miW < 0) miW = v.word;
                if (v.wk == W_Wh && whW < 0) whW = v.word;
                if (content(v.wk)) {
                    ++nContent;
                    if (firstContent < 0) firstContent = v.word;
                    prevContent = lastContent;
                    lastContent = v.word;
                }
            }
            int nucleus = lastContent;
            if (nContent >= 3 && prevContent >= 0) nucleus = prevContent; // SOV: the word before the verb
            bool qMi = false, qWh = false, qRise = false;
            if (lastPhrase && T == VS_Question) {
                if (miW >= 0) {
                    qMi = true;
                    nucleus = -1;
                    for (int k = pa; k <= pb; ++k)
                        if (isSyl(k) && P.s[(size_t)k].word < miW && content(P.s[(size_t)k].wk)) nucleus = P.s[(size_t)k].word;
                } else if (whW >= 0) {
                    qWh = true;
                    nucleus = whW;
                } else {
                    qRise = true;
                }
            }
            // emphatic: the first word carries it; a trailing line ("…") gives up early and sinks
            if ((T == VS_Exclaim || T == VS_Trail) && firstContent >= 0) nucleus = firstContent;
            (void)firstW;
            (void)lastW;
            for (int k = pa; k <= pb; ++k) {
                if (!isSyl(k)) continue;
                VSyl& v = P.s[(size_t)k];
                const float pos = clampf((tAt[k] - t0) / (t1 - t0), 0.f, 1.f);
                // declination: statements fall most, questions least
                const float base = (T == VS_Question ? R * (.6f - 1.f * pos)
                                    : T == VS_Exclaim ? R * (1.2f - 2.2f * pos) : R * (.8f - 1.8f * pos)) +
                                   down + rng.uniform(-.25f, .25f) * R;
                float a0 = base, m0, b0 = base - .3f * R, amp = rng.uniform(.9f, 1.f) * (1.f - .15f * pos);
                const bool stressed = (v.flags & VF_Stress) != 0;
                const bool lastSyl = k == pb;
                if (nucleus >= 0 && v.word < nucleus) { // pre-nuclear: low, rising to the stressed syllable
                    if (stressed) {
                        a0 = base + 1.2f * R;
                        b0 = base + 2.6f * R;
                        amp *= 1.f + .1f * vp.effort;
                    } else if (v.wpos < v.wstress) {
                        a0 = base - .3f * R;
                        b0 = base + .2f * R;
                    } else {
                        a0 = base + .6f * R;
                        b0 = base - .2f * R;
                    }
                } else if (v.word == nucleus) { // the nuclear accent: high on the stress, then down
                    if (stressed) {
                        a0 = base + 2.8f * R;
                        b0 = base + (qMi ? 4.6f : qWh ? 2.4f : 1.6f) * R;
                        amp *= 1.f + .2f * vp.effort;
                    } else if (v.wpos < v.wstress) {
                        a0 = base;
                        b0 = base + .8f * R;
                    } else {
                        a0 = base - .4f * R;
                        b0 = base - 1.f * R;
                    }
                } else { // after the nucleus (or no nucleus): low and flat, sinking
                    a0 = base - .8f * R;
                    b0 = base - 1.2f * R;
                    amp *= nucleus >= 0 ? .88f : 1.f;
                    if (nucleus < 0 && stressed) {
                        a0 = base + .4f * R;
                        b0 = base;
                    }
                }
                if (qMi && v.wk == W_Mi) { // "... mi?": a step down from the peak, a little lift at the very end
                    a0 = base + 2.6f * R;
                    b0 = base + 2.f * R;
                    amp *= .9f;
                }
                // the end of the phrase
                if (lastSyl) {
                    if (!lastPhrase) { // continuation: up at the comma
                        b0 = a0 + 1.8f * R;
                    } else if (qRise) {
                        a0 = base + .3f * R;
                        b0 = base + 6.f * R;
                    } else if (qMi) {
                        b0 = std::max(b0, base + 4.2f * R);
                    } else if (T == VS_Exclaim) {
                        if (vp.exclRise > .15f) b0 = a0 + 12.f * std::log2(1.f + vp.exclRise);
                        else b0 = a0 - 3.f * R;
                        amp *= 1.1f;
                    } else if (T == VS_Trail) {
                        b0 = a0 - 2.4f * R;
                        amp *= .72f;
                    } else {
                        b0 = std::min(b0, a0) - 2.2f * R; // L%: a statement ends low
                    }
                } else if (qRise && k == pb - 1) {
                    b0 = a0 - .4f * R; // a little dip before the rise
                }
                if (T == VS_Exclaim) amp *= 1.15f;
                m0 = .5f * (a0 + b0);
                // the interjections and particles have tunes of their own (over the voice's base, not the phrase)
                const float lift = down * .5f;
                switch (v.wk) {
                case W_Laugh: {
                    const float L = 12.f * std::log2(vp.laugh) + lift;
                    a0 = L - 1.1f * (float)v.wpos;
                    m0 = a0 + .4f;
                    b0 = a0 - 1.2f;
                    amp = std::pow(.86f, (float)v.wpos) * 1.05f;
                } break;
                case W_Hah: a0 = 3.5f * R + lift; m0 = a0 + .8f * R; b0 = lift; amp = 1.2f; v.breath = .3f; break;
                case W_Scoff: a0 = -.5f * R + lift; m0 = a0 - .5f * R; b0 = a0 - 3.f * R; amp = .8f; break;
                case W_Ah: a0 = 3.f * R + lift; m0 = 2.f * R + lift; b0 = -3.f * R + lift; v.breath = .35f; break;
                case W_Of: a0 = 2.f * R + lift; m0 = 1.f * R + lift; b0 = -2.5f * R + lift; v.breath = .3f; break;
                case W_Oh: a0 = 2.f * R + lift; m0 = 3.f * R + lift; b0 = -1.5f * R + lift; v.breath = .25f; break;
                case W_Eh: a0 = lift; m0 = -.6f * R + lift; b0 = -2.f * R + lift; v.breath = .28f; amp *= .85f; break;
                case W_Tuh: a0 = 2.5f * R + lift; m0 = 1.5f * R + lift; b0 = -2.f * R + lift; v.breath = .3f; break;
                case W_Hum:
                    a0 = lift;
                    m0 = 1.2f * R + lift;
                    b0 = (T == VS_Question ? 2.5f : -1.f) * R + lift;
                    amp = .85f;
                    break;
                case W_Vay: a0 = 1.f * R + lift; m0 = 5.f * R + lift; b0 = 1.f * R + lift; amp = 1.2f; break;
                case W_Aman:
                    if (v.wpos == 0) a0 = 3.f * R + lift, m0 = 3.6f * R + lift, b0 = 3.f * R + lift, amp = 1.15f;
                    else a0 = 2.f * R + lift, m0 = 1.f * R + lift, b0 = -1.f * R + lift;
                    break;
                case W_Hadi:
                    if (v.wpos == 0) a0 = 2.8f * R + lift, m0 = 3.5f * R + lift, b0 = 3.f * R + lift, amp = 1.18f;
                    else a0 = 2.f * R + lift, m0 = 1.2f * R + lift, b0 = .4f * R + lift;
                    break;
                case W_Be: // "hadi be!": short, emphatic, falling
                    a0 = 1.6f * R + lift;
                    m0 = a0;
                    b0 = -1.6f * R + lift;
                    amp = 1.15f;
                    break;
                case W_Ya:
                    if (v.flags & VF_SentEnd) a0 = 1.f * R + lift, m0 = 3.f * R + lift, b0 = -.8f * R + lift;
                    break;
                case W_Ha: a0 = 2.f * R + lift; m0 = 3.f * R + lift; b0 = .5f * R + lift; break;
                default: break;
                }
                A[k] = a0;
                M[k] = m0;
                B[k] = b0;
                v.amp = std::min(amp, 1.45f);
            }
            pa = pb + 1;
            while (pa <= sb && !isSyl(pa)) ++pa;
        }
        ++sentIdx;
        a = b + 1;
    }
    for (int k = 0; k < P.n; ++k) {
        VSyl& v = P.s[(size_t)k];
        if (!isSyl(k)) continue;
        v.f0a = std::exp2(A[k] / 12.f);
        v.f0m = std::exp2(M[k] / 12.f);
        v.f0b = std::exp2(B[k] / 12.f);
    }
}

// ---- consonants: where the formants point (loci), the sonorants' own formants, the noise spectra
// The formant edge at a consonant next to vowel `vi`: the locus pulled part of the way to the vowel (Lindblom).
void consonantEdge(char32_t c, int vi, float& f1, float& f2, float& f3) {
    const Vowel& V = kVowels[vi];
    float l2 = V.f2, l3 = V.f3, e1 = 300.f;
    switch (c) {
    case U'b': case U'p': case U'm': case U'f': case U'v': l2 = 900.f; l3 = 2200.f; e1 = 260.f; break;
    case U't': case U'd': case U'n': case U's': case U'z': case U'l': case U'r': l2 = 1750.f; l3 = 2650.f; e1 = 280.f;
        break;
    case U'ş': case U'ç': case U'c': case U'j': l2 = 2000.f; l3 = 2700.f; e1 = 280.f; break;
    case U'y': l2 = 2200.f; l3 = 3000.f; e1 = 270.f; break;
    case U'k': case U'g': case U'q':
        if (frontVowel(vi)) l2 = 2300.f, l3 = 2950.f;
        else l2 = 1250.f, l3 = 2100.f;
        e1 = 260.f;
        break;
    default: f1 = V.f1; f2 = V.f2; f3 = V.f3; return; // h: the vowel itself
    }
    if (c == U'l' || c == U'r') e1 = 380.f;
    f1 = e1;
    f2 = l2 + .45f * (V.f2 - l2);
    f3 = l3 + .45f * (V.f3 - l3);
}

// A sonorant's own formants while it sounds (m n l r y), plus how nasal it is and where its antiresonance sits.
void sonorantFormants(char32_t c, int vi, float& f1, float& f2, float& f3, float& nas, float& zHz) {
    nas = 0.f;
    zHz = 1000.f;
    switch (c) {
    case U'm': f1 = 260.f; f2 = 1000.f; f3 = 2250.f; nas = 1.f; zHz = 900.f; break;
    case U'n': f1 = 260.f; f2 = 1500.f; f3 = 2500.f; nas = 1.f; zHz = 1650.f; break;
    case U'l': f1 = 360.f; f2 = frontVowel(vi) ? 1750.f : 1050.f; f3 = 2700.f; break;
    case U'r': f1 = 420.f; f2 = 1500.f; f3 = 2300.f; break;
    case U'y': f1 = 270.f; f2 = 2200.f; f3 = 3000.f; break;
    default: { const Vowel& V = kVowels[vi]; f1 = V.f1; f2 = V.f2; f3 = V.f3; } break;
    }
}

// Shaped noise for a fricative (or the burst of a stop): band centre, Q, high-pass, level.
void fricSpec(char32_t c, int vi, bool burst, float& hz, float& q, float& hpHz, float& lvl) {
    if (burst) {
        switch (c) {
        case U'p': case U'b': hz = 1300.f; q = .6f; hpHz = 300.f; lvl = c == U'p' ? .55f : .4f; return;
        case U't': case U'd': hz = 4300.f; q = 1.f; hpHz = 2200.f; lvl = c == U't' ? .7f : .5f; return;
        case U'ç': case U'c': hz = 3500.f; q = 1.2f; hpHz = 2000.f; lvl = .6f; return;
        default: // k g q
            hz = frontVowel(vi) ? 3000.f : 1700.f;
            q = 1.6f;
            hpHz = 800.f;
            lvl = c == U'g' ? .5f : .7f;
            return;
        }
    }
    switch (c) {
    case U's': case U'z': hz = 6200.f; q = 1.4f; hpHz = 3600.f; lvl = c == U's' ? .55f : .35f; return;
    case U'ş': case U'j': case U'ç': case U'c': hz = 3100.f; q = 1.f; hpHz = 1700.f; lvl = c == U'j' || c == U'c' ? .35f : .6f;
        return;
    case U'f': case U'v': hz = 5000.f; q = .5f; hpHz = 1300.f; lvl = c == U'f' ? .14f : .05f; return;
    case U'r': hz = 3200.f; q = .8f; hpHz = 1500.f; lvl = .12f; return;
    default: hz = 2500.f; q = 1.f; hpHz = 1000.f; lvl = .12f; return;
    }
}

bool voicedConsonant(char32_t c) {
    return c == U'b' || c == U'c' || c == U'd' || c == U'g' || c == U'v' || c == U'z' || c == U'j';
}

// Klatt's two-pole resonator (unity gain at DC) and its inverse (a zero pair), for the cascade tract.
struct Reso {
    float a = 1.f, b = 0.f, c = 0.f, y1 = 0.f, y2 = 0.f;
    void set(float sr, float f, float bw) {
        const float r = std::exp(-(float)kPi * bw / sr);
        c = -r * r;
        b = 2.f * r * std::cos(kTauF * f / sr);
        a = 1.f - b - c;
    }
    float process(float x) {
        const float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};
struct AntiReso {
    float a = 1.f, b = 0.f, c = 0.f, x1 = 0.f, x2 = 0.f;
    void set(float sr, float f, float bw) {
        const float r = std::exp(-(float)kPi * bw / sr);
        const float C = -r * r, B = 2.f * r * std::cos(kTauF * f / sr), Aa = 1.f - B - C;
        a = 1.f / Aa;
        b = -B / Aa;
        c = -C / Aa;
    }
    float process(float x) {
        const float y = a * x + b * x1 + c * x2;
        x2 = x1;
        x1 = x;
        return y;
    }
};

// sin(pi * x) for x in [0, 1] (a parabola with one correction step; error ~0.1 %)
inline float sinPi01(float x) {
    float y = 4.f * x * (1.f - x);
    return .225f * (y * y - y) + y;
}

// Renders a planned line (mono) into `out` (capacity `cap` frames); returns the frames written.
int renderSpeech(const VoicePlan& P, const VoiceProfile& vp, float sr, uint64_t seed, float* out, int cap) {
    okey::Rng rng(seed);
    Noise nz((uint32_t)(seed * 2654435761u) | 1u);
    const int total = std::min(cap, (int)((P.total + kVoiceTail) * sr));
    if (total <= 0 || P.n <= 0) return 0;
    const float fs = vp.formant;
    Reso r1, r2, r3, r4, r5, rNp;
    AntiReso rNz;
    Biquad fb, fh, lp1, lp2, hp;
    lp1.lowpass(sr, vp.lp, .6f);
    lp2.lowpass(sr, vp.lp * 1.2f, .7f);
    hp.highpass(sr, 70.f, .7f);
    rNp.set(sr, 270.f, 100.f);
    r4.set(sr, 3350.f * fs, 250.f);
    r5.set(sr, 4300.f * fs, 320.f);
    int firstSyl = 0;
    while (firstSyl < P.n - 1 && (P.s[(size_t)firstSyl].flags & (VF_Pause | VF_Click))) ++firstSyl;
    const Vowel& v0 = kVowels[P.s[(size_t)firstSyl].vowel];
    float F1 = v0.f1 * fs, F2 = v0.f2 * fs, F3 = v0.f3 * fs, B1 = 80.f, B2 = 100.f, B3 = 160.f;
    float nzF = 270.f, nzB = 100.f, nC = 0.f;
    float f0 = vp.f0 * P.s[(size_t)firstSyl].f0a;
    float vA = 0.f, aA = 0.f, fA = 0.f, brA = 0.f, dvA = 0.f, daA = 0.f, dfA = 0.f, dbA = 0.f;
    float fryAmt = 0.f, fMod = 0.f;
    // glottal state
    float ph = 0.f, perMul = 1.f, pAmp = 1.f, ret = 0.f, alt = 1.f;
    bool inRet = false;
    float trPh = rng.uniform(0.f, 1.f);
    const float trRate = 5.2f + rng.uniform(-.4f, .4f);
    const float kForm = 1.f - std::exp(-(float)kCtrl / (.006f * sr));
    const float kF0 = 1.f - std::exp(-(float)kCtrl / (.022f * sr));
    const float kFry = 1.f - std::exp(-(float)kCtrl / (.03f * sr));
    const float kFricGain = 2.5f; // the noise branch against the voiced cascade's gain
    auto gauss = [&]() { return (rng.uniform() + rng.uniform() + rng.uniform() - 1.5f) * 2.f; };
    float lastFz = -1.f, lastFq = -1.f, lastHp = -1.f;
    int seg = 0;
    float segT0 = 0.f;
    for (int i0 = 0; i0 < total; i0 += kCtrl) {
        const float t = (float)i0 / sr;
        while (seg < P.n && t >= segT0 + P.s[(size_t)seg].dur) {
            segT0 += P.s[(size_t)seg].dur;
            ++seg;
        }
        // ---- targets for this control block
        float vT = 0.f, aT = 0.f, fT = 0.f, brT = vp.breath, nT = vp.nasal * .5f, zT = 900.f, fModT = 0.f, fryT = 0.f;
        float f1t = F1, f2t = F2, f3t = F3, b1t = 80.f, b2t = 100.f, b3t = 160.f, f0t = f0, oqAdd = 0.f;
        float fHz = 2500.f, fQ = 1.f, fHp = 1000.f;
        if (seg < P.n) {
            const VSyl& v = P.s[(size_t)seg];
            const float tt = t - segT0;
            const float u = tt / std::max(v.dur, 1e-3f);
            const int vi = v.vowel;
            const Vowel& vw = kVowels[vi];
            const float Vf1 = vw.f1 * fs, Vf2 = vw.f2 * fs, Vf3 = vw.f3 * fs;
            // the pitch: start -> middle -> end
            const float pr = u < .5f ? v.f0a + (v.f0m - v.f0a) * (u * 2.f) : v.f0m + (v.f0b - v.f0m) * (u * 2.f - 1.f);
            if (v.flags & VF_Pause) {
                // silence: formants and pitch hold (the next syllable's start pulls the pitch over)
                if (seg + 1 < P.n) f0t = vp.f0 * P.s[(size_t)(seg + 1)].f0a;
            } else if (v.flags & VF_Sigh) {
                // "hhhaah": breath through an open vowel, a whisper of voice that sinks
                const float e = u < .18f ? u / .18f : std::pow(1.f - (u - .18f) / .82f, 1.6f);
                aT = .55f * e * v.amp;
                vT = .12f * e * v.amp;
                f1t = Vf1 * .95f;
                f2t = Vf2;
                f3t = Vf3;
                f0t = vp.f0 * (.8f - .2f * u);
                brT = .6f;
            } else if (v.flags & VF_Inhale) { // a breath in through the mouth after a laugh
                const float e = std::sin((float)kPi * clampf(u, 0.f, 1.f));
                aT = .12f * e;
                f1t = 420.f * fs;
                f2t = 1500.f * fs;
                f3t = 2500.f * fs;
                b1t = 200.f;
                b2t = 250.f;
                fricSpec(U'h', vi, false, fHz, fQ, fHp, fT);
                fT = .015f * e;
            } else if (v.flags & VF_Click) { // "cık": the tongue tip pulled off the teeth
                fHz = 3000.f;
                fQ = 3.f;
                fHp = 1500.f;
                fT = tt < .004f ? 1.4f : 0.f;
            } else if (v.flags & VF_Hiss) { // "şşt"
                fricSpec(v.onCh, vi, false, fHz, fQ, fHp, fT);
                const float body = v.coda == VC_Stop ? .82f : 1.f;
                const float e = u < .12f ? u / .12f : u < body ? 1.f - .3f * (u - .12f) / (body - .12f) : 0.f;
                fT *= e * 1.1f;
                if (v.coda == VC_Stop && u > body + .1f && u < body + .16f) {
                    fricSpec(v.codaCh, vi, true, fHz, fQ, fHp, fT);
                }
            } else {
                // phases: onset consonant | vowel | coda consonant (in seconds)
                float on = 0.f, close = 0.f;
                switch (v.onset) {
                case VC_Stop:
                    close = voicedConsonant(v.onCh) ? .035f : .045f;
                    on = close + (v.onCh == U'ç' ? .05f : v.onCh == U'c' ? .03f : voicedConsonant(v.onCh) ? .006f : .024f);
                    break;
                case VC_Fric:
                    on = (v.onCh == U's' || v.onCh == U'ş') ? .075f : (v.onCh == U'z' || v.onCh == U'j') ? .06f
                         : v.onCh == U'v' ? .04f : .055f;
                    break;
                case VC_Son:
                    on = (v.onCh == U'm' || v.onCh == U'n') ? .05f : v.onCh == U'r' ? .024f : v.onCh == U'y' ? .045f : .04f;
                    break;
                case VC_H: on = (v.flags & VF_Laugh) ? .045f : .04f; break;
                default: break;
                }
                float cd = 0.f;
                switch (v.coda) {
                case VC_Stop: cd = .045f; break;
                case VC_Fric: cd = (v.codaCh == U's' || v.codaCh == U'ş') ? .075f : .055f; break;
                case VC_Son: cd = (v.codaCh == U'm' || v.codaCh == U'n') ? .055f : .045f; break;
                case VC_H: cd = (v.wk == W_Scoff || v.wk == W_Tuh || v.wk == W_Hah) ? .09f : .05f; break;
                default: break;
                }
                const float sc = std::min(1.f, v.dur * .55f / std::max(on + cd, 1e-3f));
                const float tOn = on * sc, tClose = close * sc, tCd = cd * sc;
                const float vowelLen = std::max(.02f, v.dur - tOn - tCd);
                f0t = vp.f0 * pr;
                // the formant edges at the onset and the coda
                float e1 = Vf1, e2 = Vf2, e3 = Vf3, c1 = Vf1, c2 = Vf2, c3 = Vf3;
                if (v.onset != VC_None) {
                    consonantEdge(v.onCh, vi, e1, e2, e3);
                    e1 *= fs, e2 *= fs, e3 *= fs;
                }
                if (v.coda != VC_None) {
                    consonantEdge(v.codaCh, vi, c1, c2, c3);
                    c1 *= fs, c2 *= fs, c3 *= fs;
                }
                const float trIn = v.onset == VC_Stop ? .045f : v.onset == VC_Son ? (v.onCh == U'y' ? .06f : .04f)
                                   : v.onset == VC_Fric ? .035f : .001f;
                const float trOut = v.coda != VC_None && v.coda != VC_H ? .04f : .001f;
                const bool lineEnd = (v.flags & VF_SentEnd) && seg >= P.n - 1 - (P.n >= 2 && (P.s[(size_t)(P.n - 1)].flags & VF_Sigh) ? 2 : 0);
                float vowelNas = v.nasal;
                if (tt < tOn) { // ---- onset
                    const float q = tt / std::max(tOn, 1e-4f);
                    f1t = e1;
                    f2t = e2;
                    f3t = e3;
                    switch (v.onset) {
                    case VC_Stop: {
                        const bool vd = voicedConsonant(v.onCh);
                        if (tt < tClose) { // closure: silence (a voice bar for b d g c)
                            vT = vd ? .1f * v.amp : 0.f;
                            f1t = 200.f * fs;
                            b2t = 200.f;
                            b3t = 300.f;
                        } else { // the burst, then aspiration (p t k) or frication (ç c) into the vowel
                            const float tb = tt - tClose;
                            fricSpec(v.onCh, vi, true, fHz, fQ, fHp, fT);
                            fT *= v.amp * std::exp(-tb / .0035f);
                            if (v.onCh == U'ç' || v.onCh == U'c') {
                                if (tb > .006f) {
                                    float lvl;
                                    fricSpec(v.onCh, vi, false, fHz, fQ, fHp, lvl);
                                    fT = lvl * v.amp * (1.f - .6f * (tb / std::max(tOn - tClose, 1e-3f)));
                                }
                                if (vd) {
                                    vT = .4f * v.amp;
                                    fModT = 1.f;
                                }
                            } else if (vd) {
                                vT = .6f * v.amp;
                            } else {
                                aT = .42f * v.amp * (1.f - .5f * clampf(tb / std::max(tOn - tClose, 1e-3f), 0.f, 1.f));
                                f1t = Vf1 * .8f;
                                b1t = 200.f;
                            }
                        }
                    } break;
                    case VC_Fric: {
                        fricSpec(v.onCh, vi, false, fHz, fQ, fHp, fT);
                        const float env = q < .2f ? q / .2f : q > .85f ? 1.f - (q - .85f) / .15f * .5f : 1.f;
                        fT *= v.amp * env;
                        if (voicedConsonant(v.onCh)) {
                            vT = (v.onCh == U'v' ? .5f : .35f) * v.amp;
                            fModT = 1.f;
                        }
                    } break;
                    case VC_H:
                        aT = ((v.flags & VF_Laugh) ? .7f : .5f) * v.amp * (q < .3f ? q / .3f : 1.f);
                        f1t = Vf1;
                        f2t = Vf2;
                        f3t = Vf3;
                        if (v.flags & VF_Laugh) vT = .12f * v.amp * q;
                        break;
                    default: { // sonorant: its own formants (nasal murmur for m n, a tap for r)
                        float s1, s2, s3, nas, zh;
                        sonorantFormants(v.onCh, vi, s1, s2, s3, nas, zh);
                        f1t = s1 * fs;
                        f2t = s2 * fs;
                        f3t = s3 * fs;
                        nT = std::max(nT, nas);
                        zT = zh * fs;
                        vT = (nas > 0.f ? .55f : v.onCh == U'y' ? .75f : .68f) * v.amp;
                        if (v.onCh == U'r') vT *= (q > .3f && q < .75f) ? .35f : .8f;
                        if (nas > 0.f) {
                            b1t = 100.f;
                            b2t = 260.f;
                            b3t = 320.f;
                        }
                    } break;
                    }
                } else if (tt < tOn + vowelLen) { // ---- the vowel
                    const float tv = tt - tOn, q = tv / vowelLen;
                    float w = clampf(tv / trIn, 0.f, 1.f);
                    w = w * w * (3.f - 2.f * w);
                    f1t = e1 + (Vf1 - e1) * w;
                    f2t = e2 + (Vf2 - e2) * w;
                    f3t = e3 + (Vf3 - e3) * w;
                    const float tl = vowelLen - tv;
                    if (tl < trOut) { // heading for the coda's place
                        float z = 1.f - tl / trOut;
                        z = z * z * (3.f - 2.f * z);
                        f1t += (c1 - f1t) * z * .8f;
                        f2t += (c2 - f2t) * z;
                        f3t += (c3 - f3t) * z;
                    }
                    const bool afterObstr = v.onset == VC_Stop || v.onset == VC_Fric;
                    const float atkT = afterObstr ? .01f : v.onset == VC_None ? (v.sent == VS_Exclaim ? .012f : .028f) : .018f;
                    float atk = v.onset == VC_Son ? 1.f : clampf(tv / atkT, 0.f, 1.f);
                    atk = .5f - .5f * std::cos((float)kPi * atk);
                    const bool open = v.coda == VC_None && (v.flags & (VF_WordEnd | VF_SentEnd)) != 0;
                    const float rel = !open ? 1.f : q < .6f ? 1.f : 1.f - (q - .6f) / .4f * .85f;
                    vT = v.amp * atk * rel;
                    // microprosody: higher after voiceless consonants, a dip after voiced ones
                    if (afterObstr) f0t *= tv < .03f ? (voicedConsonant(v.onCh) ? .97f : 1.035f) : 1.f;
                    // nasalised next to a nasal consonant
                    if (v.onset == VC_Son && (v.onCh == U'm' || v.onCh == U'n')) vowelNas = std::max(vowelNas, .5f * (1.f - clampf(tv / .05f, 0.f, 1.f)));
                    if (v.coda == VC_Son && (v.codaCh == U'm' || v.codaCh == U'n') && tl < .06f) vowelNas = std::max(vowelNas, .6f * (1.f - tl / .06f));
                    nT = std::max(nT, vowelNas);
                    zT = (v.nasal > .5f ? 1000.f : 700.f) * fs;
                    if (v.nasal > .5f) b1t = 140.f;
                    brT = vp.breath + v.breath;
                    if (v.flags & VF_Laugh) {
                        oqAdd = .12f;
                        aT = .15f * vT;
                    }
                    if (v.flags & VF_Hum) { // closed mouth: "mmm"
                        f1t = 260.f * fs;
                        f2t = 1000.f * fs;
                        f3t = 2250.f * fs;
                        b1t = 110.f;
                        b2t = 300.f;
                        b3t = 400.f;
                        nT = 1.f;
                        zT = 900.f * fs;
                        const float e = q < .15f ? q / .15f : q > .8f ? 1.f - (q - .8f) / .2f : 1.f;
                        vT = v.amp * .8f * e;
                    }
                    // the end of the line: breathier, partly devoiced, creaky for the old voices
                    if (lineEnd && q > .55f) {
                        const float z = (q - .55f) / .45f;
                        aT += .22f * vT * z;
                        vT *= 1.f - .35f * z;
                        if (v.sent == VS_Statement || v.sent == VS_Trail) fryT = vp.fry;
                    } else if ((v.flags & VF_SentEnd) && q > .5f && (v.sent == VS_Statement || v.sent == VS_Trail)) {
                        fryT = vp.fry * .6f;
                    }
                    // stressed and loud syllables push the source: brighter, pressed
                    oqAdd -= .06f * (v.amp - 1.f) * vp.effort;
                } else { // ---- coda
                    const float q = (tt - tOn - vowelLen) / std::max(tCd, 1e-4f);
                    f1t = c1;
                    f2t = c2;
                    f3t = c3;
                    switch (v.coda) {
                    case VC_Stop: {
                        const bool vd = voicedConsonant(v.codaCh);
                        vT = vd && q < .4f ? .12f * v.amp : 0.f;
                        f1t = 200.f * fs;
                        if (q > .72f) { // the release: weak, a breath after it
                            fricSpec(v.codaCh, vi, true, fHz, fQ, fHp, fT);
                            fT *= .45f * v.amp * std::exp(-(q - .72f) * tCd / .004f);
                            aT = vd ? 0.f : .12f * v.amp;
                        }
                    } break;
                    case VC_Fric: {
                        fricSpec(v.codaCh, vi, false, fHz, fQ, fHp, fT);
                        const float env = q < .25f ? q / .25f : 1.f - (q - .25f) / .75f * .8f;
                        fT *= .85f * v.amp * env;
                        if (voicedConsonant(v.codaCh)) {
                            vT = .3f * v.amp * (1.f - q);
                            fModT = 1.f;
                        }
                    } break;
                    case VC_H:
                        aT = .42f * v.amp * (1.f - q) * (1.f + v.breath);
                        f1t = Vf1 * .9f;
                        f2t = Vf2;
                        f3t = Vf3;
                        if (v.nasal > .5f) { // "hıh": out through the nose
                            nT = 1.f;
                            zT = 1000.f * fs;
                            f1t = 280.f * fs;
                        }
                        break;
                    default: {
                        float s1, s2, s3, nas, zh;
                        sonorantFormants(v.codaCh, vi, s1, s2, s3, nas, zh);
                        f1t = s1 * fs;
                        f2t = s2 * fs;
                        f3t = s3 * fs;
                        nT = std::max(nT, nas);
                        zT = zh * fs;
                        vT = (nas > 0.f ? .5f : .6f) * v.amp * (1.f - .6f * q);
                        if (nas > 0.f) {
                            b1t = 100.f;
                            b2t = 260.f;
                            b3t = 320.f;
                        }
                        if (v.codaCh == U'r' && (v.flags & VF_WordEnd)) { // a word-final r hisses
                            vT *= 1.f - .7f * q;
                            fricSpec(U'r', vi, false, fHz, fQ, fHp, fT);
                            fT *= v.amp * q;
                        } else if (v.codaCh == U'r') {
                            vT *= (q > .3f && q < .7f) ? .4f : 1.f;
                        }
                    } break;
                    }
                }
            }
        }
        // ---- smoothing and filter updates (control rate)
        F1 += (f1t - F1) * kForm;
        F2 += (f2t - F2) * kForm;
        F3 += (f3t - F3) * kForm;
        B1 += (b1t - B1) * kForm;
        B2 += (b2t - B2) * kForm;
        B3 += (b3t - B3) * kForm;
        nC += (clampf(nT, 0.f, 1.f) - nC) * kForm;
        nzF += (270.f + nC * (zT - 270.f) - nzF) * kForm;
        nzB += (100.f + nC * 80.f - nzB) * kForm;
        f0 += (f0t - f0) * kF0;
        fryAmt += (fryT - fryAmt) * kFry;
        fMod = fModT;
        const float bw1 = B1 * (1.f + .6f * brT) + 40.f * nC; // breathy and nasal voices damp F1
        r1.set(sr, std::max(150.f, F1), bw1);
        r2.set(sr, std::max(500.f, F2), B2);
        r3.set(sr, std::max(1200.f, F3), B3);
        rNz.set(sr, nzF, nzB);
        if (fT > 1e-4f || fA > 1e-4f) {
            if (fHz != lastFz || fQ != lastFq) {
                fb.bandpass(sr, fHz, fQ);
                lastFz = fHz;
                lastFq = fQ;
            }
            if (fHp != lastHp) {
                fh.highpass(sr, fHp, .7f);
                lastHp = fHp;
            }
        }
        // the voice's tremor (pitch and loudness), and the source's shape for this block
        trPh += trRate * (float)kCtrl / sr;
        if (trPh >= 1.f) trPh -= 1.f;
        const float trS = std::sin(kTauF * trPh);
        const float trem = vp.tremor > 0.f ? centsToRatio(vp.tremor * trS) : 1.f;
        const float tremA = 1.f + vp.tremor * .004f * trS;
        const float fryF = 1.f - .3f * fryAmt; // creak: lower, slower pulses
        const float inc = f0 * trem * fryF / sr;
        const float te = clampf(vp.oq + oqAdd + .1f * brT, .32f, .85f), tp = te * .62f;
        const float invTp = 1.f / tp, invTc = 1.f / (te - tp), cNorm = (te - tp) * invTp;
        const float taSec = vp.ta * .001f * (1.f + .5f * brT);
        const float retK = std::exp(-1.f / (taSec * sr));
        const float jit = vp.jitter * (1.f + 2.f * fryAmt), shm = vp.shimmer * (1.f + fryAmt);
        const float rough = std::min(1.f, vp.rough + .6f * fryAmt);
        dvA = (vT * tremA - vA) / (float)kCtrl;
        daA = (aT - aA) / (float)kCtrl;
        dfA = (fT - fA) / (float)kCtrl;
        dbA = (brT - brA) / (float)kCtrl;
        const int end = std::min(total, i0 + kCtrl);
        for (int i = i0; i < end; ++i) {
            ph += inc * perMul;
            if (ph >= 1.f) { // a new glottal period: jitter, shimmer, roughness
                ph -= 1.f;
                inRet = false;
                alt = -alt;
                perMul = 1.f / std::max(.7f, 1.f + jit * gauss() + alt * rough * .035f);
                pAmp = std::max(.2f, 1.f + shm * gauss()) * (alt > 0.f ? 1.f - .3f * rough : 1.f);
            }
            float dg, flow;
            if (ph < tp) { // opening
                const float x = ph * invTp;
                dg = sinPi01(x) * cNorm;
                const float s = sinPi01(.5f * x);
                flow = s * s;
            } else if (ph < te) { // closing: the flow falls faster and faster, to the sharp negative peak (the
                // corner there, into the return phase, is what excites the tract: the voice's brightness)
                const float x = (ph - tp) * invTc;
                dg = -x * (.35f + .65f * x);
                flow = 1.f - x * x;
            } else { // return phase
                if (!inRet) {
                    inRet = true;
                    ret = 1.f;
                }
                dg = -ret;
                ret *= retK;
                flow = 0.f;
            }
            const float n = nz.next();
            float x = dg * vA * pAmp + n * (vA * brA * .35f * (.25f + .75f * flow) + aA * .5f);
            // the nasal pole/zero pair (they cancel when the velum is closed), then the formants
            x = rNz.process(rNp.process(x));
            x = r1.process(x);
            x = r2.process(x);
            x = r3.process(x);
            x = r5.process(r4.process(x));
            if (fA > 1e-5f || dfA > 0.f) {
                const float fn = fh.process(fb.process(n)) * fA * kFricGain;
                x += fMod > 0.f ? fn * (.35f + .65f * flow) : fn;
            }
            out[i] = hp.process(lp2.process(lp1.process(x)));
            vA += dvA;
            aA += daA;
            fA += dfA;
            brA += dbA;
        }
    }
    // ---- level: the voiced parts at the target RMS, peaks under the ceiling, faded edges
    const int win = std::max(1, (int)(.02f * sr));
    float wins[160];
    int nw = 0;
    for (int i = 0; i + win <= total && nw < 160; i += win) {
        double s = 0.0;
        for (int k = 0; k < win; ++k) s += (double)out[i + k] * out[i + k];
        wins[nw++] = (float)std::sqrt(s / win);
    }
    std::sort(wins, wins + nw);
    const float active = nw > 0 ? wins[(int)(.8f * (float)(nw - 1))] : 0.f;
    float peak = 0.f;
    for (int i = 0; i < total; ++i) peak = std::max(peak, std::fabs(out[i]));
    int syl = 0, excl = 0;
    for (int k = 0; k < P.n; ++k)
        if (!(P.s[(size_t)k].flags & (VF_Pause | VF_Sigh))) {
            ++syl;
            excl += P.s[(size_t)k].sent == VS_Exclaim ? 1 : 0;
        }
    const float punch = syl > 0 ? 2.f * (float)excl / (float)syl : 0.f; // exclamations: up to +2 dB
    if (active > 1e-6f && peak > 0.f) {
        float g = dbToGain(kVoiceRmsDb + vp.gainDb + punch) / active;
        g = std::min(g, kVoicePeak / peak);
        for (int i = 0; i < total; ++i) out[i] *= g;
    }
    const int fi = std::min(total / 4, (int)(.004f * sr) + 1), fo = std::min(total / 3, (int)(.03f * sr) + 1);
    for (int i = 0; i < fi; ++i) out[i] *= (float)i / (float)fi;
    for (int i = 0; i < fo; ++i) out[total - 1 - i] *= (float)i / (float)fo;
    return total;
}

// One line in flight, handed from the main thread to the audio thread.
struct VoiceSlot {
    std::vector<float> buf;           // preallocated at init
    std::atomic<int> state{0};        // 0 free (main thread may write), 1 playing (audio thread owns),
                                      // 2 queued / rendering (VoiceWorker owns)
    std::atomic<bool> stop{false};    // main -> audio: fade out now
    int len = 0, voice = 0;
    float gl = 0.f, gr = 0.f;
    uint64_t serial = 0;              // main thread: start order
    // audio thread only
    int pos = 0;
    float fade = 1.f;
    // ---- Yüz (lip sync) hook: the line's loudness every 10 ms (0..255, written by the VoiceWorker before it hands
    // the slot to the audio thread) and how far the audio thread has played it (Audio::mouthOpen reads both).
    std::array<uint8_t, 288> env{};
    int envN = 0;
    std::atomic<int> played{0};
    // ---- end yüz hook
};

struct VoiceBus {
    std::array<VoiceSlot, kVoiceSlots> slots;
    std::atomic<float> target{0.f};   // master * voices on/off (main thread)
    float cur = 0.f, coef = 0.f, fadeStep = 0.f;
    void init(float sr) {
        for (VoiceSlot& s : slots) s.buf.assign((size_t)((kVoiceMaxSeconds + kVoiceTail + .05f) * sr), 0.f);
        coef = smoothCoef(sr, .05f);
        fadeStep = 1.f / (.02f * sr);
    }
    // audio thread: mixes the playing slots (overwrites `out`, interleaved stereo)
    void render(float* out, int frames) {
        std::memset(out, 0, sizeof(float) * 2 * (size_t)frames);
        const float tgt = target.load(std::memory_order_relaxed);
        float g = cur;
        for (VoiceSlot& s : slots) {
            if (s.state.load(std::memory_order_acquire) != 1) continue;
            const bool stopping = s.stop.load(std::memory_order_relaxed);
            g = cur;
            int i = 0;
            for (; i < frames && s.pos < s.len; ++i, ++s.pos) {
                g += (tgt - g) * coef;
                if (stopping) {
                    s.fade -= fadeStep;
                    if (s.fade <= 0.f) break;
                }
                const float x = s.buf[(size_t)s.pos] * g * s.fade;
                out[2 * i] += x * s.gl;
                out[2 * i + 1] += x * s.gr;
            }
            s.played.store(s.pos, std::memory_order_relaxed); // (yüz hook: lip sync)
            if (s.pos >= s.len || (stopping && s.fade <= 0.f)) s.state.store(0, std::memory_order_release);
        }
        // the shared gain moves the same way whether or not anything played
        for (int i = 0; i < frames; ++i) cur += (tgt - cur) * coef;
    }
};

std::atomic<VoiceBus*> gVoices{nullptr};

// ---- Voices off the main thread (2026-10) ----
// Audio::speak used to plan and render the line itself (0.3..4 ms on the main thread, per line). Now it only picks a
// free slot, marks it queued (state 2: this worker owns it) and queues the text; the worker plans and renders the line
// into the slot's preallocated buffer with the very same functions and seed (the same samples as before, bit for
// bit), then hands it to the audio thread (state 1). The audio callback is unchanged: no lock, no allocation there
// (the mutex is only between the main thread and this worker). A line queued for a voice that is told to stop (the
// same man starts another line, voices switched off) is dropped instead of played.
struct VoiceJob {
    int slot = -1, voice = 0;
    uint64_t seed = 0;
    std::string text;
};

class VoiceWorker {
public:
    ~VoiceWorker() { stop(); }
    void start(VoiceBus* bus, float sr) {
        stop();
        bus_ = bus;
        sr_ = sr;
        quit_ = false;
        count_ = 0;
        plan_ = std::make_unique<VoicePlan>();
        th_ = std::thread([this] { run(); });
    }
    // Joins the worker; slots still queued go back to free (nothing is half-written into a playing slot).
    void stop() {
        if (!th_.joinable()) return;
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        th_.join();
        for (int k = 0; k < count_; ++k) bus_->slots[(size_t)jobs_[(size_t)((head_ + k) % kVoiceSlots)].slot].state.store(0);
        count_ = 0;
    }
    bool running() const { return th_.joinable(); }
    // Main thread: the slot is already marked queued (state 2). At most kVoiceSlots jobs can wait (one per slot).
    void push(int slot, int voice, uint64_t seed, const std::string& text) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            VoiceJob& j = jobs_[(size_t)((head_ + count_) % kVoiceSlots)];
            j.slot = slot;
            j.voice = voice;
            j.seed = seed;
            j.text = text;
            ++count_;
        }
        cv_.notify_one();
    }

private:
    void run(); // (below, after the voices' profiles and placement)
    VoiceBus* bus_ = nullptr;
    float sr_ = 48000.f;
    std::thread th_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::array<VoiceJob, kVoiceSlots> jobs_;
    int head_ = 0, count_ = 0;
    bool quit_ = false;
    std::unique_ptr<VoicePlan> plan_;  // worker thread only
};

void voiceCallback(void* data, unsigned int frames) {
    float* out = static_cast<float*>(data);
    if (VoiceBus* v = gVoices.load(std::memory_order_acquire))
        v->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
}

// ------------------------------------------------------------------------------------------------
// Live state shared with raylib's audio thread
// ------------------------------------------------------------------------------------------------

struct CbStats {
    std::atomic<uint64_t> frames{0}, calls{0}, nsTotal{0}, nsMax{0};
    void reset() {
        frames = 0;
        calls = 0;
        nsTotal = 0;
        nsMax = 0;
    }
    void record(unsigned n, uint64_t ns) { // single writer: the audio thread
        frames.fetch_add(n, std::memory_order_relaxed);
        calls.fetch_add(1, std::memory_order_relaxed);
        nsTotal.fetch_add(ns, std::memory_order_relaxed);
        if (ns > nsMax.load(std::memory_order_relaxed)) nsMax.store(ns, std::memory_order_relaxed);
    }
};

std::atomic<AmbienceGen*> gAmb{nullptr};
std::atomic<RadioGen*> gRadio{nullptr};
std::atomic<const SfxBank*> gBank{nullptr};
std::atomic<int> gRate{0};
CbStats gAmbStats, gRadioStats;

uint64_t nanosSince(std::chrono::steady_clock::time_point t0) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}

void ambienceCallback(void* data, unsigned int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    float* out = static_cast<float*>(data);
    if (AmbienceGen* g = gAmb.load(std::memory_order_acquire))
        g->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
    gAmbStats.record(frames, nanosSince(t0));
}

void radioCallback(void* data, unsigned int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    float* out = static_cast<float*>(data);
    if (RadioGen* g = gRadio.load(std::memory_order_acquire))
        g->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
    gRadioStats.record(frames, nanosSince(t0));
}

// Real recordings on the radio (raylib Music streams from assets/music): this processor puts them through the
// set's small speaker — a wide band-pass (warmer and clearer than the synthesised tunes' 300..3500 Hz, the
// songs must stay pleasant), a boxy resonance, gentle saturation and the wall reflection. It runs on raylib's
// audio thread on the stream converted to the device format (float, stereo, device rate).
struct RecordFx {
    Biquad hp1, hp2, lp1, lp2, pk, lowShelf;
    std::vector<float> refl;
    uint32_t reflW = 0, reflD = 1;
    std::atomic<bool> reset{true};
    void init(float sr) {
        hp1.highpass(sr, 95.f, .54f);
        hp2.highpass(sr, 95.f, 1.31f);
        lp1.lowpass(sr, 6800.f, .54f);
        lp2.lowpass(sr, 6800.f, 1.31f);
        pk.peaking(sr, 1500.f, .9f, 2.5f);
        lowShelf.peaking(sr, 220.f, .8f, 1.5f);
        refl.assign(4096, 0.f);
        reflD = (uint32_t)(.017f * sr);
    }
    void process(float* x, unsigned frames) {
        if (reset.exchange(false, std::memory_order_acq_rel)) {
            for (Biquad* b : {&hp1, &hp2, &lp1, &lp2, &pk, &lowShelf}) b->z1 = b->z2 = 0.f;
            std::fill(refl.begin(), refl.end(), 0.f);
        }
        for (unsigned i = 0; i < frames; ++i) {
            const float in = .5f * (x[2 * i] + x[2 * i + 1]);
            float y = lowShelf.process(pk.process(lp2.process(lp1.process(hp2.process(hp1.process(in))))));
            y = softClip(y * 1.35f) / 1.35f;
            refl[reflW & 4095] = y;
            const float rf = refl[(reflW - reflD) & 4095];
            ++reflW;
            x[2 * i] = y * .86f + rf * .14f;
            x[2 * i + 1] = y + rf * .09f;
        }
    }
};
std::atomic<RecordFx*> gRecFx{nullptr};
// The records sit a little under the synthesised radio's level (-18 LUFS masters): a song in the room, not
// over the table (SetMusicVolume at full master volume).
constexpr float kRecordGain = .5f;

void recordProcessor(void* data, unsigned int frames) {
    if (RecordFx* fx = gRecFx.load(std::memory_order_acquire)) fx->process(static_cast<float*>(data), frames);
}

// Safety net on the final device mix: transparent below -1.9 dBFS (every single sound peaks at
// -3 dBFS or lower), soft saturation above, so overlapping effects never hard-clip. Stateless.
void mixLimiter(void* data, unsigned int frames) {
    float* x = static_cast<float*>(data);
    for (unsigned int i = 0; i < frames * 2; ++i) {
        const float v = x[i], a = std::fabs(v);
        if (a > .8f) x[i] = std::copysign(.8f + .2f * std::tanh((a - .8f) * 5.f), v);
    }
}

// raylib converts every Sound to the device rate; synthesising at that rate avoids resampling.
float detectDeviceRate() {
    float probe[64] = {};
    Wave w{64, 48000, 32, 1, probe};
    Sound s = LoadSoundFromWave(w);
    const unsigned rate = s.stream.sampleRate;
    if (IsSoundValid(s)) UnloadSound(s);
    return (rate >= 8000 && rate <= 192000) ? (float)rate : 48000.f;
}

void seatSpatial(int seat, float& pan, float& vol) {
    switch (seat) {
    case 1: pan = .5f; vol = .85f; break;   // right
    case 2: pan = 0.f; vol = .72f; break;   // across the table
    case 3: pan = -.5f; vol = .85f; break;  // left
    default: pan = 0.f; vol = 1.f; break;   // the player
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Audio
// ------------------------------------------------------------------------------------------------

struct Audio::Impl {
    struct Var {
        std::vector<Sound> voices; // [0] owns the samples, the rest are aliases
        std::vector<uint64_t> stamp;
    };
    struct Pending {
        Sfx s;
        float vol, pitch, pan;
        double due;
        bool handEnd;
    };

    bool ready = false, sfxOn = true, ambOn = true, musicOn = true;
    float master = 1.f, rain = 0.f;
    float sr = 48000.f;
    okey::Rng rng;
    std::unique_ptr<SfxBank> bank;
    std::array<std::vector<Var>, (size_t)Sfx::Count> sounds;
    std::array<int, (size_t)Sfx::Count> lastVar{};
    uint64_t stampCounter = 0;
    std::unique_ptr<AmbienceGen> amb;
    std::unique_ptr<RadioGen> radio;
    AudioStream ambStream{}, radioStream{};
    bool ambStreamOk = false, radioStreamOk = false;
    std::vector<Pending> pending;
    double now = 0.0, lastDue = -10.0, lastHandEndAt = -10.0;
    // the radio's records (assets/music): shuffled, a few seconds of hiss between songs
    struct Track {
        std::string path, title, artist;
    };
    std::vector<Track> tracks;
    std::vector<int> order;
    size_t orderPos = 0;
    int lastTrack = -1;
    Music music{};
    bool musicLoaded = false, musicPaused = false;
    float songGap = 2.5f;  // seconds of hiss before the next song
    std::unique_ptr<RecordFx> recFx;
    std::string nowPlaying;
    bool nowPlayingNew = false;
    // the regulars' voices
    std::unique_ptr<VoiceBus> voices;
    AudioStream voiceStream{};
    bool voiceStreamOk = false, voicesOn = true;
    VoiceWorker voiceWorker;           // renders the lines (declared after `voices`: it is stopped before they go)
    uint64_t voiceSerial = 0, voiceSeed = 0;

    void loadPlaylist();
    void startSong();
    void stopSong();
    void updateRadio(float dt);

    float masterGain() const { return master * master; } // perceptual taper
    void pushTargets() {
        if (amb) amb->target.store(ambOn ? masterGain() : 0.f, std::memory_order_relaxed);
        if (radio) radio->target.store(musicOn ? masterGain() : 0.f, std::memory_order_relaxed);
        if (voices) voices->target.store(voicesOn ? masterGain() : 0.f, std::memory_order_relaxed);
    }
    void playNow(Sfx s, float vol, float pitch, float pan);
    void enqueue(Sfx s, float vol, float pan, float gap, bool handEnd = false, float pitch = 1.f);
};

// assets/music next to the executable (or in the working directory): liste.tsv names the songs
// (file <TAB> title <TAB> artist ...); without it every .mp3/.ogg in the folder plays under its file name.
void Audio::Impl::loadPlaylist() {
    tracks.clear();
    std::string dir;
    for (const std::string& d : {std::string(GetApplicationDirectory()) + "assets/music", std::string("assets/music"),
                                 std::string(GetApplicationDirectory()) + "../../assets/music"})
        if (DirectoryExists(d.c_str())) {
            dir = d;
            break;
        }
    if (dir.empty()) return;
    std::ifstream list(dir + "/liste.tsv");
    std::string line;
    while (std::getline(list, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t a = 0;
        for (size_t b; (b = line.find('\t', a)) != std::string::npos; a = b + 1) f.push_back(line.substr(a, b - a));
        f.push_back(line.substr(a));
        const std::string path = dir + "/" + f[0];
        if (!FileExists(path.c_str())) continue;
        tracks.push_back({path, f.size() > 1 ? f[1] : f[0], f.size() > 2 ? f[2] : std::string()});
    }
    if (tracks.empty()) {
        FilePathList files = LoadDirectoryFiles(dir.c_str());
        for (unsigned i = 0; i < files.count; ++i)
            if (IsFileExtension(files.paths[i], ".mp3;.ogg")) tracks.push_back({files.paths[i], GetFileNameWithoutExt(files.paths[i]), {}});
        UnloadDirectoryFiles(files);
    }
    songGap = rng.uniform(1.5f, 3.5f);
}

void Audio::Impl::startSong() {
    if (tracks.empty()) return;
    if (orderPos >= order.size()) {  // a fresh shuffle, never starting with the song just heard
        order.resize(tracks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[(size_t)rng.range((int)i)]);
        if (order.size() > 1 && order[0] == lastTrack) std::swap(order[0], order[1]);
        orderPos = 0;
    }
    const int t = order[orderPos++];
    // longer buffers than raylib's default: a slow frame must not starve the song
    SetAudioStreamBufferSizeDefault(8192);
    music = LoadMusicStream(tracks[(size_t)t].path.c_str());
    SetAudioStreamBufferSizeDefault(0);
    if (!IsMusicValid(music)) {
        songGap = 1.f;
        return;
    }
    music.looping = false;
    recFx->reset.store(true, std::memory_order_release);
    AttachAudioStreamProcessor(music.stream, recordProcessor);
    SetMusicPan(music, 0.15f);  // the set hangs a little to the right of the room's middle
    SetMusicVolume(music, 0.f);
    PlayMusicStream(music);
    musicLoaded = true;
    musicPaused = false;
    lastTrack = t;
    const Track& tr = tracks[(size_t)t];
    nowPlaying = tr.artist.empty() ? tr.title : tr.title + " — " + tr.artist;
    nowPlayingNew = true;
}

void Audio::Impl::stopSong() {
    if (!musicLoaded) return;
    StopMusicStream(music);
    DetachAudioStreamProcessor(music.stream, recordProcessor);
    UnloadMusicStream(music);
    music = Music{};
    musicLoaded = false;
}

void Audio::Impl::updateRadio(float dt) {
    if (tracks.empty()) return;
    const float vol = musicOn ? masterGain() * kRecordGain : 0.f;
    if (!musicLoaded) {
        if (!musicOn) return;
        songGap -= dt;
        if (songGap <= 0.f) startSong();
        return;
    }
    if (!musicOn) {  // switched off: the song waits where it was
        if (!musicPaused) PauseMusicStream(music);
        musicPaused = true;
        return;
    }
    if (musicPaused) {
        ResumeMusicStream(music);
        musicPaused = false;
    }
    UpdateMusicStream(music);
    const float played = GetMusicTimePlayed(music), len = GetMusicTimeLength(music);
    // a short fade in, and out over the last second
    const float fade = std::min(clampf(played / 0.8f, 0.f, 1.f), clampf((len - played) / 1.0f, 0.f, 1.f));
    SetMusicVolume(music, vol * fade);
    if (!IsMusicStreamPlaying(music) || played >= len - 0.05f) {
        stopSong();
        songGap = rng.uniform(2.5f, 6.f);
    }
}

void Audio::Impl::playNow(Sfx s, float vol, float pitch, float pan) {
    if (!ready || !sfxOn) return;
    const int id = (int)s;
    if (id < 0 || id >= (int)Sfx::Count) return;
    std::vector<Var>& vars = sounds[(size_t)id];
    if (vars.empty()) return;
    const int nv = (int)vars.size();
    int v = 0;
    if (s == Sfx::CarPass && nv >= 4) { // wet tyres on a rainy night, the other two on a dry one
        const int base = rain > .05f ? 2 : 0;
        v = base + (lastVar[(size_t)id] == base ? 1 : (lastVar[(size_t)id] == base + 1 ? 0 : rng.range(2)));
    } else if (nv > 1) { // any variation but the one heard last
        if (lastVar[(size_t)id] < 0) {
            v = rng.range(nv);
        } else {
            v = rng.range(nv - 1);
            if (v >= lastVar[(size_t)id]) ++v;
        }
    }
    lastVar[(size_t)id] = v;
    Var& var = vars[(size_t)v];
    size_t pick = 0;
    bool freeVoice = false;
    for (size_t k = 0; k < var.voices.size(); ++k) {
        if (!IsSoundPlaying(var.voices[k])) {
            pick = k;
            freeVoice = true;
            break;
        }
    }
    if (!freeVoice) { // steal the oldest copy
        for (size_t k = 1; k < var.voices.size(); ++k)
            if (var.stamp[k] < var.stamp[pick]) pick = k;
    }
    var.stamp[pick] = ++stampCounter;
    Sound& snd = var.voices[pick];
    const SfxDef& d = kSfx[id];
    const float vj = 1.f - d.volJit * rng.uniform();
    const float pj = 1.f + d.pitchJit * (rng.uniform() * 2.f - 1.f);
    pan = clampf(pan, -1.f, 1.f);
    // raylib's pan law: keep the louder channel at the wave's own level
    const float right = (pan + 1.f) * .5f, left = 1.f - right;
    const float lv = .5f * left * (3.f - left * left), rv = .5f * right * (3.f - right * right);
    const float comp = 1.f / std::max(std::max(lv, rv), .5f);
    const float g = clampf(vol, 0.f, 1.f) * vj * masterGain() * comp;
    if (!freeVoice) StopSound(snd);
    SetSoundVolume(snd, g);
    SetSoundPitch(snd, clampf(pitch * pj, .25f, 4.f));
    SetSoundPan(snd, pan);
    PlaySound(snd);
}

void Audio::Impl::enqueue(Sfx s, float vol, float pan, float gap, bool handEnd, float pitch) {
    if (!ready || !sfxOn) return;
    const double due = std::max(now, lastDue + gap);
    if (due - now > 1.5) return; // a burst of events (fast bots): don't build a backlog
    lastDue = due;
    if (handEnd) lastHandEndAt = due;
    if (due <= now)
        playNow(s, vol, pitch, pan);
    else
        pending.push_back({s, vol, pitch, pan, due, handEnd});
}

Audio::Audio() : impl_(new Impl) {}

Audio::~Audio() {
    shutdown();
    delete impl_;
}

bool Audio::init() {
    Impl& m = *impl_;
    if (m.ready) return true;
    if (!IsAudioDeviceReady()) return false;
    if (gAmb.load() || gRadio.load()) return false; // another Audio instance owns the streams

    const uint64_t seed = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m.rng.reseed(seed ^ 0xC0FFEEull);
    m.sr = detectDeviceRate();
    m.bank = buildSfxBank(m.sr, seed);
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        for (Buf& b : m.bank->vars[(size_t)i]) {
            Wave w{(unsigned)b.size(), (unsigned)m.sr, 32, 1, b.data()};
            Sound base = LoadSoundFromWave(w);
            if (!IsSoundValid(base)) continue;
            Impl::Var var;
            var.voices.push_back(base);
            for (int k = 1; k < kSfx[i].voices; ++k) {
                Sound a = LoadSoundAlias(base);
                if (IsSoundValid(a)) var.voices.push_back(a);
            }
            var.stamp.assign(var.voices.size(), 0);
            m.sounds[(size_t)i].push_back(std::move(var));
        }
    }
    m.lastVar.fill(-1);

    m.amb = std::make_unique<AmbienceGen>(m.sr, seed ^ 0x1111AAAAull);
    m.radio = std::make_unique<RadioGen>(m.sr, seed ^ 0x2222BBBBull);
    m.pushTargets();
    gRate.store((int)m.sr);
    gAmbStats.reset();
    gRadioStats.reset();
    gBank.store(m.bank.get(), std::memory_order_release);
    gAmb.store(m.amb.get(), std::memory_order_release);
    gRadio.store(m.radio.get(), std::memory_order_release);

    m.ambStream = LoadAudioStream((unsigned)m.sr, 32, 2);
    m.ambStreamOk = IsAudioStreamValid(m.ambStream);
    if (m.ambStreamOk) {
        SetAudioStreamCallback(m.ambStream, ambienceCallback);
        SetAudioStreamVolume(m.ambStream, 1.f / kCenterPanGain);
        PlayAudioStream(m.ambStream);
    }
    m.radioStream = LoadAudioStream((unsigned)m.sr, 32, 2);
    m.radioStreamOk = IsAudioStreamValid(m.radioStream);
    if (m.radioStreamOk) {
        SetAudioStreamCallback(m.radioStream, radioCallback);
        SetAudioStreamVolume(m.radioStream, 1.f / kCenterPanGain);
        PlayAudioStream(m.radioStream);
    }
    m.voices = std::make_unique<VoiceBus>();
    m.voices->init(m.sr);
    m.voiceWorker.start(m.voices.get(), m.sr);
    m.voiceSeed = seed ^ 0x5EECull;
    m.pushTargets();
    gVoices.store(m.voices.get(), std::memory_order_release);
    m.voiceStream = LoadAudioStream((unsigned)m.sr, 32, 2);
    m.voiceStreamOk = IsAudioStreamValid(m.voiceStream);
    if (m.voiceStreamOk) {
        SetAudioStreamCallback(m.voiceStream, voiceCallback);
        SetAudioStreamVolume(m.voiceStream, 1.f / kCenterPanGain);
        PlayAudioStream(m.voiceStream);
    }
    AttachAudioMixedProcessor(mixLimiter);
    m.loadPlaylist();
    if (!m.tracks.empty()) {
        m.recFx = std::make_unique<RecordFx>();
        m.recFx->init(m.sr);
        gRecFx.store(m.recFx.get(), std::memory_order_release);
        m.radio->recorded.store(true, std::memory_order_relaxed);
        TraceLog(LOG_INFO, "AUDIO: radio: %d recordings", (int)m.tracks.size());
    }
    m.now = 0.0;
    m.lastDue = m.lastHandEndAt = -10.0;
    m.ready = true;
    return true;
}

void Audio::shutdown() {
    Impl& m = *impl_;
    if (!m.ready) return;
    // With the device still open, unloading takes raylib's mixer lock, so no callback can be running
    // afterwards. If the device was already closed, the audio thread is gone and nothing can run.
    if (IsAudioDeviceReady()) {
        m.stopSong();
        DetachAudioMixedProcessor(mixLimiter);
        if (m.ambStreamOk) {
            StopAudioStream(m.ambStream);
            UnloadAudioStream(m.ambStream);
        }
        if (m.radioStreamOk) {
            StopAudioStream(m.radioStream);
            UnloadAudioStream(m.radioStream);
        }
        if (m.voiceStreamOk) {
            StopAudioStream(m.voiceStream);
            UnloadAudioStream(m.voiceStream);
        }
        for (auto& vars : m.sounds) {
            for (auto& var : vars) {
                for (size_t k = 1; k < var.voices.size(); ++k) UnloadSoundAlias(var.voices[k]);
                if (!var.voices.empty()) UnloadSound(var.voices[0]);
            }
        }
    }
    gAmb.store(nullptr);
    gRadio.store(nullptr);
    gRecFx.store(nullptr);
    m.voiceWorker.stop(); // (before the slots it writes go)
    gVoices.store(nullptr);
    gBank.store(nullptr);
    gRate.store(0);
    for (auto& vars : m.sounds) vars.clear();
    m.amb.reset();
    m.radio.reset();
    m.recFx.reset();
    m.voices.reset();
    m.voiceStreamOk = false;
    m.tracks.clear();
    m.order.clear();
    m.orderPos = 0;
    m.musicLoaded = false;
    m.bank.reset();
    m.pending.clear();
    m.ambStreamOk = m.radioStreamOk = false;
    m.ready = false;
}

void Audio::update(float dt) {
    Impl& m = *impl_;
    if (!m.ready) return;
    m.now += (double)clampf(dt, 0.f, .25f);
    for (size_t i = 0; i < m.pending.size();) {
        if (m.pending[i].due <= m.now) {
            const Impl::Pending p = m.pending[i];
            m.pending.erase(m.pending.begin() + (std::ptrdiff_t)i);
            m.playNow(p.s, p.vol, p.pitch, p.pan);
        } else {
            ++i;
        }
    }
    // a device hiccup can stop a stream; keep the beds running
    if (m.ambStreamOk && !IsAudioStreamPlaying(m.ambStream)) PlayAudioStream(m.ambStream);
    if (m.radioStreamOk && !IsAudioStreamPlaying(m.radioStream)) PlayAudioStream(m.radioStream);
    if (m.voiceStreamOk && !IsAudioStreamPlaying(m.voiceStream)) PlayAudioStream(m.voiceStream);
    m.updateRadio(clampf(dt, 0.f, .25f));
}

bool Audio::consumeNowPlaying(std::string& text) {
    Impl& m = *impl_;
    if (!m.nowPlayingNew) return false;
    m.nowPlayingNew = false;
    text = m.nowPlaying;
    return true;
}

void Audio::play(Sfx s, float volume, float pitch) {
    const int id = (int)s;
    impl_->playNow(s, volume, pitch, (id >= 0 && id < (int)Sfx::Count) ? kSfx[id].pan : 0.f);
}

void Audio::onEvent(const okey::GameEvent& e) {
    Impl& m = *impl_;
    if (!m.ready || !m.sfxOn) return;
    float pan = 0.f, vol = 1.f;
    seatSpatial(e.player, pan, vol);
    using okey::EvType;
    switch (e.type) {
    case EvType::DrawPile:
    case EvType::TakeLeft: m.enqueue(Sfx::TileDraw, vol, pan, .12f); break;
    case EvType::ReturnLeft: m.enqueue(Sfx::TileDraw, vol * .8f, pan, .12f, false, .94f); break;
    case EvType::Discard: m.enqueue(Sfx::TileDiscard, vol, pan, .12f); break;
    case EvType::Open: m.enqueue(Sfx::TileSlam, vol, pan, .15f); break;
    case EvType::LayMelds: m.enqueue(Sfx::TileSlam, vol * (e.count <= 1 ? .7f : .88f), pan, .15f); break;
    case EvType::AddToMeld:
    case EvType::SwapJoker: m.enqueue(Sfx::TileDiscard, vol * .6f, pan, .12f, false, 1.06f); break;
    case EvType::Penalty: m.enqueue(Sfx::Penalty, 1.f, 0.f, .22f); break;
    case EvType::MatchStart: m.enqueue(Sfx::Chair, .55f, 0.f, .1f); break; // everyone pulls up a chair
    case EvType::HandStart: m.enqueue(Sfx::Shuffle, 1.f, 0.f, .25f); break;
    case EvType::HandEnd:
        if (e.player == 0)
            m.enqueue(Sfx::Win, 1.f, 0.f, .35f, true);
        else
            m.enqueue(Sfx::Lose, e.player < 0 ? .8f : 1.f, 0.f, .35f, true);
        break;
    case EvType::MatchEnd: {
        const Sfx s = e.player == 0 ? Sfx::Win : Sfx::Lose;
        bool replaced = false;
        for (auto& p : m.pending) { // the match result replaces a hand flourish that hasn't played yet
            if (p.handEnd) {
                p.s = s;
                p.vol = 1.f;
                replaced = true;
            }
        }
        if (replaced) break;
        if (m.now - m.lastHandEndAt < 1.45) { // otherwise let the hand flourish finish first
            const double due = m.lastHandEndAt + 1.45;
            m.pending.push_back({s, 1.f, 1.f, 0.f, due, false});
            m.lastDue = std::max(m.lastDue, due);
        } else {
            m.enqueue(s, 1.f, 0.f, .35f);
        }
        break;
    }
    default: break;
    }
}

void Audio::setSfxEnabled(bool on) {
    impl_->sfxOn = on;
    if (!on) impl_->pending.clear();
}

void Audio::setAmbientEnabled(bool on) {
    impl_->ambOn = on;
    impl_->pushTargets();
}

void Audio::setMusicEnabled(bool on) {
    impl_->musicOn = on;
    impl_->pushTargets();
}

void Audio::setMasterVolume(float v01) {
    impl_->master = clampf(v01, 0.f, 1.f);
    impl_->pushTargets();
}

void Audio::setRain(float amount01) {
    Impl& m = *impl_;
    m.rain = clampf(amount01, 0.f, 1.f);
    if (m.amb) m.amb->rainTarget.store(m.rain, std::memory_order_relaxed);
}

// (ozelgun) rain on the awning
void Audio::setRainCanvas(float k01) {
    Impl& m = *impl_;
    if (m.amb) m.amb->canvasTarget.store(clampf(k01, 0.f, 1.f), std::memory_order_relaxed);
}

// Bahçe: the garden bed crossfades with the room's (a few seconds, on the audio thread)
void Audio::setVenue(int venue, bool night) {
    Impl& m = *impl_;
    if (m.amb) {
        m.amb->gardenTarget.store(venue == 1 ? 1.f : 0.f, std::memory_order_relaxed);
        m.amb->nightTarget.store(night ? 1.f : 0.f, std::memory_order_relaxed);
    }
}

namespace {
uint64_t hashText(const std::string& t) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : t) h = (h ^ c) * 1099511628211ull;
    return h;
}

// Where a voice sits: the seats like their other sounds, a patron somewhere in the room, the çaycı nearby, the
// player in the middle (our own voice).
void voiceSpatial(int voice, okey::Rng& r, float& gl, float& gr) {
    float pan = 0.f, vol = 1.f;
    if (voice >= 1 && voice <= 3) seatSpatial(voice, pan, vol);
    else if (voice == 4) pan = r.uniform(-.85f, .85f);
    else if (voice == 6) pan = 0.f, vol = .85f;
    else pan = r.uniform(-.35f, .35f), vol = .9f;
    gl = std::cos((pan + 1.f) * (float)kPi * .25f) * vol;
    gr = std::sin((pan + 1.f) * (float)kPi * .25f) * vol;
}

// The voice's profile for one line (a patron gets a voice of his own each time).
VoiceProfile lineProfile(int voice, okey::Rng& r) {
    VoiceProfile p = kVoiceProfiles[voice];
    if (voice == 4) {
        p.f0 *= r.uniform(.82f, 1.22f);
        p.formant *= r.uniform(.95f, 1.06f);
        p.syl *= r.uniform(.9f, 1.15f);
    }
    return p;
}

// The worker (VoiceWorker above): one queued line at a time, rendered exactly as speak() used to do it.
// ---- Yüz (lip sync) hook: the rendered line's loudness in 10 ms frames, normalised to its loudest frame and
// lifted a little (quiet consonants still part the lips), for the regulars' mouths (Audio::mouthOpen).
void voiceEnvelope(VoiceSlot& s, float sr) {
    const int hop = std::max(1, (int)(sr * 0.01f));
    const int n = std::min((int)s.env.size(), (s.len + hop - 1) / hop);
    float rms[288];
    float peak = 1e-6f;
    for (int k = 0; k < n; ++k) {
        const int a = k * hop, b = std::min(s.len, a + hop);
        double acc = 0.0;
        for (int i = a; i < b; ++i) acc += (double)s.buf[(size_t)i] * s.buf[(size_t)i];
        rms[k] = (float)std::sqrt(acc / std::max(1, b - a));
        peak = std::max(peak, rms[k]);
    }
    for (int k = 0; k < n; ++k) {
        const float v = std::sqrt(std::min(1.f, rms[k] / peak)); // (a gentle curve: soft syllables still show)
        s.env[(size_t)k] = (uint8_t)std::lround((v < 0.12f ? 0.f : v) * 255.f);
    }
    s.envN = n;
    s.played.store(0, std::memory_order_relaxed);
}
// ---- end yüz hook

void VoiceWorker::run() {
    for (;;) {
        VoiceJob j;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return quit_ || count_ > 0; });
            if (quit_) return;
            std::swap(j, jobs_[(size_t)head_]);
            head_ = (head_ + 1) % kVoiceSlots;
            --count_;
        }
        VoiceSlot& s = bus_->slots[(size_t)j.slot];
        okey::Rng r(j.seed);
        const VoiceProfile p = lineProfile(j.voice, r);
        planSpeech(j.text, p, r, *plan_);
        s.len = s.stop.load(std::memory_order_relaxed) ? 0 : renderSpeech(*plan_, p, sr_, j.seed, s.buf.data(), (int)s.buf.size());
        if (s.len <= 0 || s.stop.load(std::memory_order_relaxed)) { // (told to stop meanwhile: never heard)
            s.state.store(0, std::memory_order_release);
            continue;
        }
        voiceSpatial(j.voice, r, s.gl, s.gr);
        voiceEnvelope(s, sr_); // (yüz hook: lip sync)
        s.pos = 0;
        s.fade = 1.f;
        s.state.store(1, std::memory_order_release);
    }
}
} // namespace

void Audio::speak(int voice, const std::string& text) {
    Impl& m = *impl_;
    if (!m.ready || !m.voicesOn || !m.voices || text.empty() || voice < 1 || voice >= kVoiceCount) return;
    VoiceBus& bus = *m.voices;
    // the same man starts a new line: the old one fades out (or, still queued, is dropped)
    for (VoiceSlot& s : bus.slots)
        if (s.voice == voice && s.state.load(std::memory_order_acquire) != 0) s.stop.store(true, std::memory_order_relaxed);
    VoiceSlot* slot = nullptr;
    for (VoiceSlot& s : bus.slots)
        if (s.state.load(std::memory_order_acquire) == 0 && (!slot || s.serial < slot->serial)) slot = &s;
    if (!slot) return; // five lines at once already: this one stays silent
    const uint64_t seed = hashText(text) ^ (m.voiceSeed + 0x9E3779B97F4A7C15ull * ++m.voiceSerial);
    slot->voice = voice;
    slot->serial = m.voiceSerial;
    slot->stop.store(false, std::memory_order_relaxed);
    slot->state.store(2, std::memory_order_relaxed); // queued: the worker owns it (the push below publishes it)
    m.voiceWorker.push((int)(slot - bus.slots.data()), voice, seed, text);
}

// ---- Yüz (lip sync) hook (r3d::Characters' mouths follow the murmur). Main thread.
float Audio::mouthOpen(int voice) const {
    const Impl& m = *impl_;
    if (!m.ready || !m.voicesOn || !m.voiceStreamOk || !m.voices || voice < 1 || voice > 5) return -1.f;
    const VoiceSlot* best = nullptr;
    int bestState = 0;
    for (const VoiceSlot& s : m.voices->slots) {
        const int st = s.state.load(std::memory_order_acquire);
        if (st == 0 || s.voice != voice || s.stop.load(std::memory_order_relaxed)) continue;
        if (!best || s.serial > best->serial) {
            best = &s;
            bestState = st;
        }
    }
    if (!best) return -1.f;
    if (bestState != 1) return 0.f; // queued: the worker renders it (a millisecond or two)
    const int sr = std::max(1, gRate.load());
    // a few frames ahead: the lips lead the sound a little (and the device buffer delays what we hear)
    const int k = best->played.load(std::memory_order_relaxed) / std::max(1, sr / 100) + 3;
    if (k >= best->envN) return 0.f;
    return (float)best->env[(size_t)k] / 255.f;
}
// ---- end yüz hook

void Audio::setVoicesEnabled(bool on) {
    Impl& m = *impl_;
    m.voicesOn = on;
    m.pushTargets();
    if (!on && m.voices)
        for (VoiceSlot& s : m.voices->slots) s.stop.store(true, std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------------------------
// Developer hooks (tools/audio_render.cpp)
// ------------------------------------------------------------------------------------------------

namespace audio_dev {

int sampleRate() { return gRate.load(); }

const char* sfxName(Sfx s) {
    const int i = (int)s;
    return (i >= 0 && i < (int)Sfx::Count) ? kSfx[i].name : "?";
}

int variationCount(Sfx s) {
    const SfxBank* b = gBank.load(std::memory_order_acquire);
    const int i = (int)s;
    if (!b || i < 0 || i >= (int)Sfx::Count) return 0;
    return (int)b->vars[(size_t)i].size();
}

bool sfxSamples(Sfx s, int variation, std::vector<float>& out) {
    const SfxBank* b = gBank.load(std::memory_order_acquire);
    const int i = (int)s;
    if (!b || i < 0 || i >= (int)Sfx::Count) return false;
    if (variation < 0 || variation >= (int)b->vars[(size_t)i].size()) return false;
    out = b->vars[(size_t)i][(size_t)variation];
    return true;
}

namespace {
template <class Gen>
void renderGen(Gen& g, double seconds, float sr, bool randomChunks, uint64_t seed, std::vector<float>& out) {
    const size_t frames = (size_t)(seconds * sr);
    out.assign(frames * 2, 0.f);
    okey::Rng r(seed ^ 0xC4u);
    size_t done = 0;
    while (done < frames) {
        int n = randomChunks ? r.range(1, 1024) : 512;
        n = (int)std::min<size_t>((size_t)n, frames - done);
        g.render(out.data() + done * 2, n);
        done += (size_t)n;
    }
}
} // namespace

void renderAmbience(int sampleRate, unsigned long long seed, double seconds, unsigned layerMask, bool randomChunks,
                    std::vector<float>& stereoOut) {
    const float sr = (float)sampleRate;
    AmbienceGen g(sr, seed, layerMask);
    g.setImmediate(1.f);
    if (layerMask & LayerRain) g.setRainImmediate(1.f);
    std::vector<float> warm;
    renderGen(g, 3.0, sr, false, seed, warm); // settle into the steady state (reverb, talkers)
    renderGen(g, seconds, sr, randomChunks, seed, stereoOut);
}

// Bahçe: the ambience with the garden bed (garden 0..1, night 0..1) for measuring levels
void renderAmbienceGarden(int sampleRate, unsigned long long seed, double seconds, float garden, float night,
                          std::vector<float>& stereoOut) {
    const float sr = (float)sampleRate;
    AmbienceGen g(sr, seed, LayerAll & ~(unsigned)LayerRain);
    g.setImmediate(1.f);
    g.setGardenImmediate(garden, night);
    std::vector<float> warm;
    renderGen(g, 3.0, sr, false, seed, warm);
    renderGen(g, seconds, sr, true, seed, stereoOut);
}

void renderRadio(int sampleRate, unsigned long long seed, double seconds, bool randomChunks,
                 std::vector<float>& stereoOut) {
    const float sr = (float)sampleRate;
    RadioGen g(sr, seed);
    g.setImmediate(1.f);
    renderGen(g, seconds, sr, randomChunks, seed, stereoOut);
}

bool renderRecord(const char* path, int sampleRate, double seconds, std::vector<float>& stereoOut) {
    Wave w = LoadWave(path);
    if (!IsWaveValid(w)) return false;
    WaveFormat(&w, sampleRate, 32, 2);
    const size_t n = std::min((size_t)w.frameCount, (size_t)(seconds * sampleRate));
    stereoOut.assign((const float*)w.data, (const float*)w.data + 2 * n);
    UnloadWave(w);
    RecordFx fx;
    fx.init((float)sampleRate);
    fx.process(stereoOut.data(), (unsigned)n);
    // SetMusicVolume(kRecordGain) and raylib's pan law near the middle
    for (float& v : stereoOut) v *= kRecordGain * kCenterPanGain;
    return true;
}

void soak(int sampleRate, unsigned long long seed, double seconds, double out[12]) {
    const float sr = (float)sampleRate;
    AmbienceGen amb(sr, seed);
    RadioGen radio(sr, seed + 1);
    amb.setImmediate(1.f);
    radio.setImmediate(1.f);
    struct Acc {
        double peak = 0, sqFirst = 0, sqLast = 0, jFirst = 0, jLast = 0;
        size_t nFirst = 0, nLast = 0;
        float prevL = 0.f, prevR = 0.f;
    } acc[2];
    const size_t total = (size_t)(seconds * sr), minute = (size_t)(60.0 * sr);
    size_t done = 0, nonFinite = 0;
    std::vector<float> buf(2 * 480);
    while (done < total) {
        const int n = (int)std::min<size_t>(480, total - done);
        for (int g = 0; g < 2; ++g) {
            if (g == 0)
                amb.render(buf.data(), n);
            else
                radio.render(buf.data(), n);
            Acc& a = acc[g];
            for (int i = 0; i < n; ++i) {
                const float l = buf[(size_t)(2 * i)], r = buf[(size_t)(2 * i + 1)];
                if (!std::isfinite(l) || !std::isfinite(r)) ++nonFinite;
                const size_t k = done + (size_t)i;
                const double pk = std::max(std::fabs(l), std::fabs(r));
                const double j = std::max(std::fabs(l - a.prevL), std::fabs(r - a.prevR));
                a.peak = std::max(a.peak, pk);
                if (k < minute) {
                    a.sqFirst += 0.5 * ((double)l * l + (double)r * r);
                    ++a.nFirst;
                    if (k > 0) a.jFirst = std::max(a.jFirst, j);
                }
                if (k + minute >= total) {
                    a.sqLast += 0.5 * ((double)l * l + (double)r * r);
                    ++a.nLast;
                    a.jLast = std::max(a.jLast, j);
                }
                a.prevL = l;
                a.prevR = r;
            }
        }
        done += (size_t)n;
    }
    for (int g = 0; g < 2; ++g) {
        const Acc& a = acc[g];
        out[g * 5 + 0] = a.peak;
        out[g * 5 + 1] = std::sqrt(a.sqFirst / (double)std::max<size_t>(1, a.nFirst));
        out[g * 5 + 2] = std::sqrt(a.sqLast / (double)std::max<size_t>(1, a.nLast));
        out[g * 5 + 3] = a.jFirst;
        out[g * 5 + 4] = a.jLast;
    }
    out[10] = (double)nonFinite;
    out[11] = (double)done / sr;
}

// One line of a voice as the game would play it (stereo, panned, at full master volume), plus its plan's length.
// `chunked`: through the voice bus with random callback-sized chunks (the audio thread's path).
double renderVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, bool chunked,
                   std::vector<float>& stereoOut) {
    stereoOut.clear();
    if (voice < 1 || voice >= kVoiceCount) return 0.0;
    const float sr = (float)sampleRate;
    auto bus = std::make_unique<VoiceBus>();
    bus->init(sr);
    bus->target.store(1.f);
    bus->cur = 1.f;
    auto plan = std::make_unique<VoicePlan>();
    okey::Rng r(seed);
    const VoiceProfile p = lineProfile(voice, r);
    planSpeech(text, p, r, *plan);
    VoiceSlot& s = bus->slots[0];
    s.len = renderSpeech(*plan, p, sr, seed, s.buf.data(), (int)s.buf.size());
    voiceSpatial(voice, r, s.gl, s.gr);
    s.state.store(1);
    const int frames = s.len;
    stereoOut.assign((size_t)frames * 2, 0.f);
    okey::Rng cr(seed ^ 0xC4u);
    int done = 0;
    while (done < frames) {
        const int n = std::min(frames - done, chunked ? cr.range(1, 1024) : 512);
        bus->render(stereoOut.data() + 2 * done, n);
        done += n;
    }
    return (double)plan->total;
}

bool timeVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, double out[3]) {
    if (voice < 1 || voice >= kVoiceCount) return false;
    const float sr = (float)sampleRate;
    std::vector<float> buf((size_t)((kVoiceMaxSeconds + kVoiceTail + .05f) * sr), 0.f);
    auto plan = std::make_unique<VoicePlan>();
    const auto t0 = std::chrono::steady_clock::now();
    okey::Rng r(seed);
    const VoiceProfile p = lineProfile(voice, r);
    planSpeech(text, p, r, *plan);
    const auto t1 = std::chrono::steady_clock::now();
    renderSpeech(*plan, p, sr, seed, buf.data(), (int)buf.size());
    const auto t2 = std::chrono::steady_clock::now();
    out[0] = std::chrono::duration<double, std::milli>(t1 - t0).count();
    out[1] = std::chrono::duration<double, std::milli>(t2 - t1).count();
    out[2] = (double)plan->total;
    return true;
}

std::string dumpVoicePlan(int voice, const std::string& text, unsigned long long seed) {
    if (voice < 1 || voice >= kVoiceCount) return {};
    auto plan = std::make_unique<VoicePlan>();
    okey::Rng r(seed);
    const VoiceProfile p = lineProfile(voice, r);
    planSpeech(text, p, r, *plan);
    std::string s;
    char row[160];
    for (int k = 0; k < plan->n; ++k) {
        const VSyl& v = plan->s[(size_t)k];
        std::snprintf(row, sizeof row, "%2d fl%04x v%d on%d/%u cd%d/%u w%d wk%d %d/%d st%d dur%.3f f0 %.2f %.2f %.2f amp%.2f\n",
                      k, v.flags, v.vowel, v.onset, (unsigned)v.onCh, v.coda, (unsigned)v.codaCh, v.word, v.wk, v.wpos,
                      v.wlen, v.wstress, v.dur, v.f0a, v.f0m, v.f0b, v.amp);
        s += row;
    }
    return s;
}

bool streamStats(double out[8]) {
    out[0] = (double)gAmbStats.frames.load();
    out[1] = (double)gAmbStats.calls.load();
    out[2] = (double)gAmbStats.nsMax.load() * 1e-6;
    out[3] = (double)gAmbStats.nsTotal.load() * 1e-6;
    out[4] = (double)gRadioStats.frames.load();
    out[5] = (double)gRadioStats.calls.load();
    out[6] = (double)gRadioStats.nsMax.load() * 1e-6;
    out[7] = (double)gRadioStats.nsTotal.load() * 1e-6;
    return gRate.load() > 0;
}

} // namespace audio_dev

} // namespace ui
