// Audio verification harness (audio owner).
//
// Opens the audio device, initialises ui::Audio exactly as the game does, then
//   1. exports every Sfx variation (the very buffers loaded into raylib Sounds) to build/audio/*.wav,
//   2. renders 30 s of ambience and 30 s of radio through the same generator classes the audio
//      thread runs (random callback-sized chunks) and exports them,
//   3. prints duration / peak / RMS / DC / max sample-to-sample jump for everything, plus octave-band
//      spectra, chunking invariance and per-layer ambience levels,
//   4. runs a real-time loop (default 20 s) calling update(), firing sfx and game events, toggling the
//      beds and injecting 150 ms frame hitches, while capturing the device mix through a raylib mixed
//      processor to detect dropouts.
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra -Isrc -I/opt/homebrew/include src/ui/Audio.cpp \
//       tools/audio_render.cpp /opt/homebrew/lib/libraylib.a -framework Cocoa -framework IOKit \
//       -framework OpenGL -framework CoreVideo -framework CoreAudio -framework AudioToolbox \
//       -framework CoreFoundation -o build/audio/audio_render
// Run:  build/audio/audio_render [--audible] [--seconds N] [--soak MINUTES] [--spectro] [--out DIR]
// (The live loop is muted with SetMasterVolume(0) unless --audible; the capture is pre-master.)
#include "ui/Audio.h"

#include <raylib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace ui {
namespace audio_dev { // defined in src/ui/Audio.cpp
int sampleRate();
const char* sfxName(Sfx s);
int variationCount(Sfx s);
bool sfxSamples(Sfx s, int variation, std::vector<float>& out);
void renderAmbience(int sampleRate, unsigned long long seed, double seconds, unsigned layerMask,
                    bool randomChunks, std::vector<float>& stereoOut);
void renderRadio(int sampleRate, unsigned long long seed, double seconds, bool randomChunks,
                 std::vector<float>& stereoOut);
bool streamStats(double out[8]);
void soak(int sampleRate, unsigned long long seed, double seconds, double out[12]);
} // namespace audio_dev
} // namespace ui

namespace {

// ---------------------------------------------------------------- WAV (16-bit PCM, TPDF dither)
bool writeWav(const std::string& path, const std::vector<float>& x, int channels, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataBytes = (uint32_t)(x.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16((uint16_t)channels);
    u32((uint32_t)rate);
    u32((uint32_t)(rate * channels * 2));
    u16((uint16_t)(channels * 2));
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    uint32_t s = 12345;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / 16777216.f;
    };
    std::vector<int16_t> pcm(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        const float d = (rnd() - rnd()) / 32768.f;
        const float v = std::max(-1.f, std::min(1.f, x[i] + d));
        pcm[i] = (int16_t)std::lround(v * 32767.f);
    }
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return true;
}

// A continuous stream cut to a file: fade 20 ms at both ends so the file itself doesn't click
// (statistics are always measured on the raw, unfaded generator output).
bool writeBed(const std::string& path, std::vector<float> x, int rate) {
    const size_t n = std::min(x.size() / 4, (size_t)(0.02 * rate)) ;
    for (size_t i = 0; i < n; ++i) {
        const float g = (float)i / (float)n;
        for (size_t c = 0; c < 2; ++c) {
            x[2 * i + c] *= g;
            x[x.size() - 2 * (i + 1) + c] *= g;
        }
    }
    return writeWav(path, x, 2, rate);
}

// ---------------------------------------------------------------- measurements
double toDb(double v) { return v > 1e-12 ? 20.0 * std::log10(v) : -240.0; }

struct Stats {
    double seconds = 0, peakDb = -240, rmsDb = -240, activeRmsDb = -240, dc = 0, maxJump = 0;
    double edge = 0; // max |sample| at the first/last frame
    bool finite = true;
};

Stats analyze(const std::vector<float>& x, int ch, int rate) {
    Stats st;
    const size_t frames = x.size() / (size_t)ch;
    st.seconds = (double)frames / rate;
    double sum = 0, sq = 0, pk = 0, jump = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double v = x[i];
        if (!std::isfinite(v)) st.finite = false;
        sum += v;
        sq += v * v;
        pk = std::max(pk, std::fabs(v));
        if (i >= (size_t)ch) jump = std::max(jump, std::fabs(v - x[i - (size_t)ch]));
    }
    st.peakDb = toDb(pk);
    st.rmsDb = toDb(std::sqrt(sq / std::max<size_t>(1, x.size())));
    st.dc = sum / std::max<size_t>(1, x.size());
    st.maxJump = jump;
    // "active" RMS: 10 ms blocks within 40 dB of the loudest block
    const size_t blk = (size_t)(rate / 100) * (size_t)ch;
    std::vector<double> e;
    double emax = 0;
    for (size_t i = 0; i + blk <= x.size(); i += blk) {
        double s2 = 0;
        for (size_t k = 0; k < blk; ++k) s2 += (double)x[i + k] * x[i + k];
        e.push_back(s2 / blk);
        emax = std::max(emax, s2 / blk);
    }
    double as = 0;
    size_t an = 0;
    for (double v : e)
        if (v > emax * 1e-4) {
            as += v;
            ++an;
        }
    st.activeRmsDb = an ? toDb(std::sqrt(as / an)) : st.rmsDb;
    for (int c = 0; c < ch && x.size() >= (size_t)ch; ++c)
        st.edge = std::max({st.edge, (double)std::fabs(x[(size_t)c]), (double)std::fabs(x[x.size() - (size_t)ch + (size_t)c])});
    return st;
}

void printHeader() {
    std::printf("%-26s %7s %9s %9s %9s %10s %9s %8s\n", "file", "dur s", "peak dBFS", "RMS dBFS", "act.RMS",
                "DC", "max jump", "edge");
}

void printRow(const std::string& name, const Stats& s) {
    std::printf("%-26s %7.3f %9.2f %9.2f %9.2f %10.2e %9.4f %8.1e%s\n", name.c_str(), s.seconds, s.peakDb, s.rmsDb,
                s.activeRmsDb, s.dc, s.maxJump, s.edge, s.finite ? "" : "  NON-FINITE!");
}

// simple RBJ band-pass for octave-band analysis
struct Bp {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    Bp(double fs, double f, double q) {
        const double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = al / a0;
        b1 = 0;
        b2 = -al / a0;
        a1 = -2 * c / a0;
        a2 = (1 - al) / a0;
    }
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

void octaveBands(const char* label, const std::vector<float>& stereo, int rate) {
    static const double fc[] = {63, 125, 250, 500, 1000, 2000, 4000, 8000};
    std::printf("  %-18s", label);
    for (double f : fc) {
        Bp bp(rate, f, 1.41);
        double sq = 0;
        size_t n = 0;
        for (size_t i = 0; i < stereo.size(); i += 2) {
            const double y = bp.run(0.5 * (stereo[i] + stereo[i + 1]));
            sq += y * y;
            ++n;
        }
        std::printf(" %6.1f", toDb(std::sqrt(sq / std::max<size_t>(1, n))));
    }
    std::printf("\n");
}

// Envelope modulation: coefficient of variation of the 25 ms-RMS envelope and its dominant rate.
void modulation(const char* label, const std::vector<float>& stereo, int rate) {
    const size_t blk = (size_t)(rate / 40); // 25 ms
    std::vector<double> env;
    for (size_t i = 0; i + 2 * blk <= stereo.size(); i += 2 * blk) {
        double s = 0;
        for (size_t k = 0; k < 2 * blk; ++k) s += (double)stereo[i + k] * stereo[i + k];
        env.push_back(std::sqrt(s / (2 * blk)));
    }
    double m = 0;
    for (double v : env) m += v;
    m /= std::max<size_t>(1, env.size());
    double var = 0;
    for (double v : env) var += (v - m) * (v - m);
    const double cv = std::sqrt(var / std::max<size_t>(1, env.size())) / std::max(m, 1e-12);
    // DFT of the envelope between 0.5 and 12 Hz (40 Hz envelope rate)
    double bestF = 0, bestP = 0;
    const double N = (double)env.size();
    for (double f = 0.5; f <= 12.0; f += 0.25) {
        double re = 0, im = 0;
        for (size_t i = 0; i < env.size(); ++i) {
            const double ph = 2 * M_PI * f * (double)i / 40.0;
            re += (env[i] - m) * std::cos(ph);
            im += (env[i] - m) * std::sin(ph);
        }
        const double p = (re * re + im * im) / (N * N);
        if (p > bestP) {
            bestP = p;
            bestF = f;
        }
    }
    std::printf("  %-18s envelope CV %.3f, strongest modulation %.2f Hz\n", label, cv, bestF);
}

// ---------------------------------------------------------------- spectrogram PNGs (--spectro)
void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Log-frequency (40 Hz .. maxHz) spectrogram, dB colour scale relative to the loudest bin.
void spectrogram(const std::string& path, const std::vector<float>& x, int ch, int rate, int fftN, int width,
                 int height, float maxHz, double rangeDb) {
    std::vector<float> mono(x.size() / (size_t)ch);
    for (size_t i = 0; i < mono.size(); ++i) {
        double s = 0;
        for (int c = 0; c < ch; ++c) s += x[i * (size_t)ch + (size_t)c];
        mono[i] = (float)(s / ch);
    }
    if (mono.size() < (size_t)fftN) mono.resize((size_t)fftN, 0.f);
    const double hop = (double)(mono.size() - (size_t)fftN) / std::max(1, width - 1);
    std::vector<std::vector<double>> col((size_t)width, std::vector<double>((size_t)fftN / 2));
    double mx = 1e-20;
    std::vector<std::complex<double>> buf((size_t)fftN);
    for (int c = 0; c < width; ++c) {
        const size_t st = (size_t)(c * hop);
        for (int i = 0; i < fftN; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (fftN - 1));
            buf[(size_t)i] = mono[st + (size_t)i] * w;
        }
        fft(buf);
        for (int k = 0; k < fftN / 2; ++k) {
            col[(size_t)c][(size_t)k] = std::norm(buf[(size_t)k]);
            mx = std::max(mx, col[(size_t)c][(size_t)k]);
        }
    }
    Image img = GenImageColor(width, height, BLACK);
    const double lo = std::log(40.0), hi = std::log((double)maxHz);
    for (int y = 0; y < height; ++y) {
        const double f = std::exp(hi - (hi - lo) * y / (height - 1));
        const double kf = f * fftN / rate;
        const size_t k0 = std::min((size_t)kf, (size_t)fftN / 2 - 2);
        const double fr = kf - (double)k0;
        for (int c = 0; c < width; ++c) {
            const double p = col[(size_t)c][k0] * (1 - fr) + col[(size_t)c][k0 + 1] * fr;
            double t = (10 * std::log10(p / mx + 1e-30) + rangeDb) / rangeDb;
            t = std::max(0.0, std::min(1.0, t));
            const unsigned char r = (unsigned char)(255 * std::min(1.0, t * 1.7));
            const unsigned char g = (unsigned char)(255 * std::max(0.0, std::min(1.0, (t - 0.35) * 1.6)));
            const unsigned char b = (unsigned char)(255 * std::max(0.0, std::min(1.0, t < 0.35 ? t * 1.4 : (t - 0.75) * 4)));
            ImageDrawPixel(&img, c, y, Color{r, g, b, 255});
        }
    }
    // octave guide lines (125 Hz .. 8 kHz)
    for (double f = 125; f < maxHz; f *= 2) {
        const int y = (int)((hi - std::log(f)) / (hi - lo) * (height - 1));
        for (int c = 0; c < width; c += 6) ImageDrawPixel(&img, c, y, Color{90, 90, 90, 255});
    }
    ExportImage(img, path.c_str());
    UnloadImage(img);
}

// ---------------------------------------------------------------- live capture (audio thread)
std::vector<float> gCap;
std::atomic<size_t> gCapPos{0};
std::atomic<uint64_t> gCapCalls{0};

void captureProcessor(void* data, unsigned int frames) {
    const float* in = static_cast<const float*>(data);
    const size_t pos = gCapPos.load(std::memory_order_relaxed);
    const size_t n = std::min((size_t)frames * 2, gCap.size() - pos);
    std::memcpy(gCap.data() + pos, in, n * sizeof(float));
    gCapPos.store(pos + n, std::memory_order_release);
    gCapCalls.fetch_add(1, std::memory_order_relaxed);
}

okey::GameEvent ev(okey::EvType t, int player, int count = 0) {
    okey::GameEvent e;
    e.type = t;
    e.player = player;
    e.count = count;
    return e;
}

} // namespace

int main(int argc, char** argv) {
    using namespace ui;
    std::string outDir = "build/audio";
    double liveSeconds = 20.0;
    bool audible = false, spectro = false;
    double soakMinutes = 30.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--audible")) audible = true;
        else if (!std::strcmp(argv[i], "--spectro")) spectro = true;
        else if (!std::strcmp(argv[i], "--soak") && i + 1 < argc) soakMinutes = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) liveSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
    }
    SetTraceLogLevel(LOG_WARNING);

    // 0. without a device, init() must fail gracefully and everything must be a no-op
    {
        Audio dead;
        const bool ok = dead.init();
        dead.update(0.016f);
        dead.play(Sfx::TileClick);
        dead.onEvent(ev(okey::EvType::Discard, 1));
        dead.setAmbientEnabled(false);
        dead.setMasterVolume(0.5f);
        dead.shutdown();
        std::printf("[no device] init() returned %s, calls were no-ops\n", ok ? "TRUE (BAD)" : "false (ok)");
    }

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        std::printf("audio device not available\n");
        return 1;
    }
    if (!audible) SetMasterVolume(0.f);

    Audio audio;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = audio.init();
    const double initMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok) {
        std::printf("Audio::init() failed\n");
        CloseAudioDevice();
        return 1;
    }
    const int rate = audio_dev::sampleRate();
    std::printf("Audio::init() ok in %.1f ms, device rate %d Hz\n", initMs, rate);
    {
        Audio second;
        std::printf("second instance init(): %s\n", second.init() ? "TRUE (BAD)" : "false (ok, streams owned)");
    }

    // 1. sound effects
    std::printf("\n== Sound effects (exact buffers loaded into raylib) ==\n");
    printHeader();
    int problems = 0;
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        const Sfx s = (Sfx)i;
        for (int v = 0; v < audio_dev::variationCount(s); ++v) {
            std::vector<float> x;
            audio_dev::sfxSamples(s, v, x);
            const std::string name = std::string(audio_dev::sfxName(s)) + "_" + std::to_string(v);
            writeWav(outDir + "/sfx_" + name + ".wav", x, 1, rate);
            if (spectro && v == 0)
                spectrogram(outDir + "/spec_" + name + ".png", x, 1, rate, 512, 400, 200, 20000.f, 90.0);
            const Stats st = analyze(x, 1, rate);
            printRow(name, st);
            if (st.peakDb > -2.9 || std::fabs(st.dc) > 1e-3 || st.edge > 1e-3 || !st.finite) ++problems;
        }
    }

    // 2. beds
    std::printf("\n== Ambience & radio (same generator classes as the audio thread) ==\n");
    printHeader();
    std::vector<float> amb, rad, radLong, a2, a3;
    audio_dev::renderAmbience(rate, 101, 30.0, 31u, true, amb);
    writeBed(outDir + "/ambience_30s.wav", amb, rate);
    const Stats sa = analyze(amb, 2, rate);
    printRow("ambience_30s", sa);
    audio_dev::renderRadio(rate, 101, 30.0, true, rad);
    writeBed(outDir + "/radio_30s.wav", rad, rate);
    const Stats sr = analyze(rad, 2, rate);
    printRow("radio_30s", sr);
    audio_dev::renderRadio(rate, 7, 240.0, true, radLong);
    printRow("radio_240s (not saved)", analyze(radLong, 2, rate));
    std::vector<float> ambLong;
    audio_dev::renderAmbience(rate, 7, 180.0, 31u, true, ambLong);
    printRow("ambience_180s (not saved)", analyze(ambLong, 2, rate));
    if (spectro) {
        std::vector<float> head(amb.begin(), amb.begin() + (long)rate * 2 * 15);
        spectrogram(outDir + "/spec_ambience_15s.png", head, 2, rate, 2048, 1200, 260, 12000.f, 70.0);
        std::vector<float> rhead(rad.begin(), rad.begin() + (long)rate * 2 * 15);
        spectrogram(outDir + "/spec_radio_15s.png", rhead, 2, rate, 4096, 1200, 300, 8000.f, 70.0);
    }

    // level stability: RMS of 1 s windows
    auto windows = [&](const char* label, const std::vector<float>& x) {
        const size_t w = (size_t)rate * 2;
        double lo = 1e9, hi = -1e9;
        for (size_t i = 0; i + w <= x.size(); i += w) {
            double s = 0;
            for (size_t k = 0; k < w; ++k) s += (double)x[i + k] * x[i + k];
            const double db = toDb(std::sqrt(s / w));
            lo = std::min(lo, db);
            hi = std::max(hi, db);
        }
        std::printf("  %-18s 1 s RMS range %.1f .. %.1f dBFS\n", label, lo, hi);
    };
    windows("ambience 180 s", ambLong);
    windows("radio 240 s", radLong);

    // chunking invariance: the stream must not depend on callback sizes (no seams at boundaries)
    audio_dev::renderAmbience(rate, 55, 6.0, 31u, false, a2);
    audio_dev::renderAmbience(rate, 55, 6.0, 31u, true, a3);
    double diffA = 0;
    for (size_t i = 0; i < a2.size(); ++i) diffA = std::max(diffA, (double)std::fabs(a2[i] - a3[i]));
    audio_dev::renderRadio(rate, 55, 6.0, false, a2);
    audio_dev::renderRadio(rate, 55, 6.0, true, a3);
    double diffR = 0;
    for (size_t i = 0; i < a2.size(); ++i) diffR = std::max(diffR, (double)std::fabs(a2[i] - a3[i]));
    std::printf("  chunk-size invariance: max |fixed-512 - random 1..1024| ambience %.2e, radio %.2e\n", diffA, diffR);

    if (soakMinutes > 0) {
        double so[12];
        const auto k0 = std::chrono::steady_clock::now();
        audio_dev::soak(rate, 2024, soakMinutes * 60.0, so);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - k0).count();
        std::printf("  soak %.0f min (rendered in %.1f s = %.0fx real time), non-finite samples: %.0f\n", so[11] / 60.0,
                    sec, so[11] / sec, so[10]);
        std::printf("    ambience: peak %.2f dBFS, RMS first/last minute %.2f / %.2f dBFS, max jump %.4f / %.4f\n",
                    toDb(so[0]), toDb(so[1]), toDb(so[2]), so[3], so[4]);
        std::printf("    radio:    peak %.2f dBFS, RMS first/last minute %.2f / %.2f dBFS, max jump %.4f / %.4f\n",
                    toDb(so[5]), toDb(so[6]), toDb(so[7]), so[8], so[9]);
    }

    std::printf("\n== Ambience layers (120 s each, RMS / peak dBFS) ==\n");
    const char* layerNames[5] = {"murmur", "tv", "fan", "events", "room"};
    std::vector<float> layerMurmur;
    for (int L = 0; L < 5; ++L) {
        std::vector<float> x;
        audio_dev::renderAmbience(rate, 101, 120.0, 1u << L, false, x);
        const Stats st = analyze(x, 2, rate);
        std::printf("  %-8s RMS %7.2f  peak %7.2f\n", layerNames[L], st.rmsDb, st.peakDb);
        if (L == 0) layerMurmur = x;
    }

    std::printf("\n== Octave bands (dBFS RMS)   63    125    250    500     1k     2k     4k     8k ==\n");
    octaveBands("ambience", amb, rate);
    octaveBands("  murmur only", layerMurmur, rate);
    octaveBands("radio", rad, rate);
    {
        std::vector<float> white(amb.size());
        uint32_t s = 1;
        for (float& v : white) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = (float)(int32_t)s / 2147483648.f * 0.0316f * 1.732f; // white noise at the ambience RMS
        }
        octaveBands("(white ref.)", white, rate);
    }
    modulation("murmur only", layerMurmur, rate);
    modulation("radio", rad, rate);

    // 3. real-time loop
    std::printf("\n== Real-time loop: %.0f s, update() every ~16 ms, 150 ms hitches, capture %s ==\n", liveSeconds,
                audible ? "(audible)" : "(device muted, capture is pre-master)");
    gCap.assign((size_t)((liveSeconds + 14.0) * rate * 2), 0.f);
    gCapPos = 0;
    double st0[8], st1[8];
    audio_dev::streamStats(st0);
    AttachAudioMixedProcessor(captureProcessor);
    const auto live0 = std::chrono::steady_clock::now();
    auto last = live0;
    double nextSfx = 0.5, nextEvent = 1.0, nextHitch = 3.0;
    int hitches = 0, frames = 0, sfxCount = 0, events = 0;
    bool ambOff = false, musicOff = false;
    uint32_t r = 99;
    auto rnd = [&]() {
        r = r * 1664525u + 1013904223u;
        return (double)(r >> 8) / 16777216.0;
    };
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        const double t = std::chrono::duration<double>(now - live0).count();
        if (t >= liveSeconds) break;
        const float dt = (float)std::chrono::duration<double>(now - last).count();
        last = now;
        audio.update(dt);
        ++frames;
        if (t >= nextSfx) {
            audio.play((Sfx)(int)(rnd() * (double)Sfx::Count), 0.6f + 0.4f * (float)rnd(), 1.f);
            ++sfxCount;
            nextSfx = t + 0.35 + rnd() * 0.9;
        }
        if (t >= nextEvent) { // a burst like one bot turn: draw + discard in the same frame
            const int seat = 1 + (int)(rnd() * 3);
            audio.onEvent(ev(okey::EvType::DrawPile, seat));
            audio.onEvent(ev(okey::EvType::Discard, seat));
            if (rnd() < 0.2) audio.onEvent(ev(okey::EvType::Penalty, seat));
            events += 2;
            nextEvent = t + 0.8 + rnd() * 1.2;
        }
        if (t > 8.0 && t < 8.1 && !ambOff) {
            audio.setAmbientEnabled(false);
            ambOff = true;
        }
        if (t > 10.0 && ambOff) {
            audio.setAmbientEnabled(true);
            ambOff = false;
        }
        if (t > 12.0 && t < 12.1 && !musicOff) {
            audio.setMusicEnabled(false);
            musicOff = true;
        }
        if (t > 14.0 && musicOff) {
            audio.setMusicEnabled(true);
            musicOff = false;
        }
        if (t > 15.0 && t < 15.1) audio.setMasterVolume(0.5f);
        if (t > 16.0 && t < 16.1) audio.setMasterVolume(1.f);
        if (t > 17.0 && t < 17.1) { // end of a match: finishing discard, hand end, match end
            audio.onEvent(ev(okey::EvType::Discard, 0));
            audio.onEvent(ev(okey::EvType::HandEnd, 0));
            audio.onEvent(ev(okey::EvType::MatchEnd, 0));
            audio.onEvent(ev(okey::EvType::HandStart, -1));
        }
        if (t >= nextHitch) { // simulate a frame hitch (e.g. a slow frame or window drag)
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            ++hitches;
            nextHitch = t + 2.5;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - live0).count();
    audio_dev::streamStats(st1);
    const size_t capN = gCapPos.load();

    // Level check through the real device mix: fade both beds out, then play a centred sound and a
    // seat-panned game event and compare the captured peaks with the stored waves.
    auto pump = [&](double seconds) {
        const auto a = std::chrono::steady_clock::now();
        auto prev = a;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count() < seconds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            const auto n = std::chrono::steady_clock::now();
            audio.update((float)std::chrono::duration<double>(n - prev).count());
            prev = n;
        }
    };
    audio.setAmbientEnabled(false);
    audio.setMusicEnabled(false);
    pump(5.0);
    const size_t q0 = gCapPos.load();
    pump(0.3);
    const size_t q1 = gCapPos.load();
    audio.play(Sfx::Win, 1.f, 1.f);
    pump(1.6);
    const size_t q2 = gCapPos.load();
    okey::GameEvent de = ev(okey::EvType::Discard, 1);
    audio.onEvent(de);
    pump(0.6);
    const size_t q3 = gCapPos.load();
    // worst case pile-up: several loud effects at once must be caught by the soft limiter
    for (int k = 0; k < 3; ++k) audio.play(Sfx::TileSlam, 1.f, 1.f);
    audio.play(Sfx::TileDiscard, 1.f, 1.f);
    audio.play(Sfx::TileDiscard, 1.f, 1.f);
    audio.play(Sfx::Penalty, 1.f, 1.f);
    pump(1.0);
    const size_t q4 = gCapPos.load();
    DetachAudioMixedProcessor(captureProcessor);
    auto peakLR = [&](size_t a, size_t b, double& l, double& r) {
        l = r = 0;
        for (size_t i = a; i + 1 < b; i += 2) {
            l = std::max(l, (double)std::fabs(gCap[i]));
            r = std::max(r, (double)std::fabs(gCap[i + 1]));
        }
    };
    double sl, sr2, wl, wr, dl, dr;
    peakLR(q0, q1, sl, sr2);
    peakLR(q1, q2, wl, wr);
    peakLR(q2, q3, dl, dr);
    std::vector<float> winWave, discWave;
    double winPk = 0, discPk = 0;
    for (int v = 0; v < audio_dev::variationCount(Sfx::Win); ++v) {
        audio_dev::sfxSamples(Sfx::Win, v, winWave);
        for (float x : winWave) winPk = std::max(winPk, (double)std::fabs(x));
    }
    for (int v = 0; v < audio_dev::variationCount(Sfx::TileDiscard); ++v) {
        audio_dev::sfxSamples(Sfx::TileDiscard, v, discWave);
        for (float x : discWave) discPk = std::max(discPk, (double)std::fabs(x));
    }
    std::printf("  level check (beds faded out): silence before = %s (peak %.1e)\n",
                sl == 0 && sr2 == 0 ? "exact zero" : "NOT ZERO", std::max(sl, sr2));
    std::printf("    play(Win, 1.0), centred: L %.2f / R %.2f dBFS, stored wave %.2f dBFS (vol jitter <= 0.26 dB)\n",
                toDb(wl), toDb(wr), toDb(winPk));
    std::printf("    onEvent(Discard, seat 1 = right, x0.85): L %.2f / R %.2f dBFS, expected R %.2f .. %.2f dBFS\n",
                toDb(dl), toDb(dr), toDb(discPk * 0.85 * 0.88), toDb(discPk * 0.85));
    double ol, orr;
    peakLR(q3, q4, ol, orr);
    std::printf("    pile-up (3x TileSlam + 2x TileDiscard + Penalty at once): peak %.2f dBFS (soft limiter, never > 0)\n",
                toDb(std::max(ol, orr)));
    gCap.resize(capN);

    const double ambFrames = st1[0] - st0[0], radFrames = st1[4] - st0[4];
    std::printf("  loop: %.2f s, %d updates, %d hitches of 150 ms, %d sfx, %d game events\n", elapsed, frames, hitches,
                sfxCount, events);
    std::printf("  ambience stream: %.0f frames (%.3f x real time), %.0f callbacks, max %.3f ms, avg %.4f ms\n",
                ambFrames, ambFrames / (elapsed * rate), st1[1] - st0[1], st1[2],
                (st1[3] - st0[3]) / std::max(1.0, st1[1] - st0[1]));
    std::printf("  radio stream:    %.0f frames (%.3f x real time), %.0f callbacks, max %.3f ms, avg %.4f ms\n",
                radFrames, radFrames / (elapsed * rate), st1[5] - st0[5], st1[6],
                (st1[7] - st0[7]) / std::max(1.0, st1[5] - st0[5]));
    const double cpu = ((st1[3] - st0[3]) + (st1[7] - st0[7])) / (elapsed * 1000.0) * 100.0;
    std::printf("  generator CPU on the audio thread: %.2f%% of one core\n", cpu);
    // dropouts: runs of exact digital silence in the device mix while the beds are running
    size_t run = 0, maxRun = 0, dropouts = 0;
    for (size_t i = 0; i + 1 < capN; i += 2) {
        if (gCap[i] == 0.f && gCap[i + 1] == 0.f) {
            if (++run == 64) ++dropouts;
        } else {
            maxRun = std::max(maxRun, run);
            run = 0;
        }
    }
    maxRun = std::max(maxRun, run);
    const Stats sc = analyze(gCap, 2, rate);
    std::printf("  device mix captured: %.2f s (%.3f x elapsed), dropouts (>=64 zero frames) %zu, longest zero run %zu\n",
                sc.seconds, sc.seconds / elapsed, dropouts, maxRun);
    printHeader();
    printRow("live_capture", sc);
    writeBed(outDir + "/live_capture.wav", gCap, rate);

    audio.shutdown();
    CloseAudioDevice();
    std::printf("\nsfx files with problems (peak > -2.9 dBFS, |DC| > 1e-3, edge > 1e-3, non-finite): %d\n", problems);
    std::printf("wrote WAVs to %s/\n", outDir.c_str());
    return 0;
}
