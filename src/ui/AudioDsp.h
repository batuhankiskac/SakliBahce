#pragma once
// Audio internals (audio owner): constants, helpers and the DSP building blocks every part of the synthesis uses
// (noise, filters, reverb, plucked strings, the modal Synth, buffer finishing). Not a public API: src/ui/Audio*.cpp only.
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

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace ui {
namespace audio_detail {

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

// Called first in every audio-thread callback: on x86 the decaying filters and reverbs would otherwise sink into
// denormal floats (tens of times slower per operation); flush-to-zero + denormals-are-zero on that thread. ARM's
// float unit has no such penalty worth the bother here: a no-op there.
inline void audioThreadNoDenormals() {
#if defined(__SSE__)
    _mm_setcsr(_mm_getcsr() | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#endif
}

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

inline void mixInto(Buf& dst, const Buf& src, int offset, float gain) {
    if ((int)dst.size() < offset + (int)src.size()) dst.resize((size_t)(offset + (int)src.size()), 0.f);
    for (size_t i = 0; i < src.size(); ++i) dst[(size_t)offset + i] += src[i] * gain;
}

inline void filterBuf(Buf& b, Biquad f) {
    for (float& x : b) x = f.process(x);
}

inline float peakOf(const Buf& b) {
    float p = 0.f;
    for (float x : b) p = std::max(p, std::fabs(x));
    return p;
}

// DC block, trim the silent tail, fade the edges, normalise the peak.
inline void finish(Buf& b, float sr, float peakDb, float trimDb = -66.f) {
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

} // namespace audio_detail
} // namespace ui
