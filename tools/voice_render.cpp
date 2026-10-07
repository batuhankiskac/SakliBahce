// The regulars' voices, rendered offline for listening and A/B checks (audio owner).
//
// Writes one WAV per (voice, line) with exactly the samples the game plays (audio_dev::renderVoice: the worker's plan
// and render, the voice bus' pan and gain), plus a CSV of per-line numbers: plan length, plan and render time on this
// machine, peak, RMS, and a pitch track summary. With --speak it also opens the audio device, initialises ui::Audio as
// the game does and times Audio::speak() itself (the main thread's cost per line).
//
// Build: make tools (-> $(BUILD)/voice_render), or standalone from the repo root:
//   clang++ -std=c++17 -O2 -Isrc -isystem /opt/homebrew/include src/ui/Audio*.cpp tools/voice_render.cpp \
//       /opt/homebrew/lib/libraylib.a -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo \
//       -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -o build/voice_render
// Run:   build/voice_render OUT_DIR [--speak] [--plan] [--voices 1,2,3,4,5,6] [--line "Hah, işte şimdi oldu!"]
// (--plan prints each line's planned syllables: flags, vowel, consonants, word kind, duration, pitch, loudness.)
#include "ui/Audio.h"

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace ui {
namespace audio_dev { // defined in src/ui/AudioDev.cpp
double renderVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, bool chunked,
                   std::vector<float>& stereoOut);
bool timeVoice(int sampleRate, int voice, const std::string& text, unsigned long long seed, double out[3]);
std::string dumpVoicePlan(int voice, const std::string& text, unsigned long long seed);
} // namespace audio_dev
} // namespace ui

namespace {

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
    std::vector<int16_t> pcm(x.size());
    for (size_t i = 0; i < x.size(); ++i) pcm[i] = (int16_t)std::lround(std::max(-1.f, std::min(1.f, x[i])) * 32767.f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return true;
}

// The lines: the kinds of things the regulars say (statements, both kinds of question, exclamations, trailing off,
// interjections, laughs, numbers).
const char* const kLines[] = {
    "Sıra sende, hayırlısı.",
    "Sıra bende mi?",
    "Hayırdır, ne attın öyle?",
    "Vay be, açtı bak!",
    "Hah, işte şimdi oldu!",
    "Hıh, yine boş taş.",
    "Ah şu dizlerim…",
    "Off, yine mi bu?",
    "Tüh, yanlış aldım.",
    "Hadi be abi, uyuma!",
    "Hahaha, helal olsun sana!",
    "Hmm, bir düşüneyim.",
    "Taşımı kaptı ya!",
    "Gooool! Gol oldu!",
    "Çay söyleyeyim mi, şekerli mi olsun?",
    "Eh, 101'i zor geçti.",
    "Şimdi şu taşı şuraya koyuyorum, sonra bakarız.",
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s OUT_DIR [--speak] [--voices 1,2,3] [--line TEXT]\n", argv[0]);
        return 2;
    }
    const std::string out = argv[1];
    bool speak = false, plan = false;
    std::vector<int> voices = {1, 2, 3, 4, 5, 6};
    std::vector<std::string> lines(std::begin(kLines), std::end(kLines));
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--speak")) speak = true;
        else if (!std::strcmp(argv[i], "--plan")) plan = true;
        else if (!std::strcmp(argv[i], "--voices") && i + 1 < argc) {
            voices.clear();
            for (const char* p = argv[++i]; *p; ++p)
                if (*p >= '1' && *p <= '9') voices.push_back(*p - '0');
        } else if (!std::strcmp(argv[i], "--line") && i + 1 < argc) {
            lines.assign(1, argv[++i]);
        }
    }
    const int sr = 48000;
    FILE* csv = std::fopen((out + "/lines.csv").c_str(), "w");
    if (csv) std::fprintf(csv, "voice,line,plan_s,file_s,plan_ms,render_ms,peak,rms_db\n");
    double worst = 0.0, sum = 0.0;
    int count = 0;
    for (int v : voices) {
        for (size_t li = 0; li < lines.size(); ++li) {
            std::vector<float> st;
            const unsigned long long seed = 0x5EEDull * (li + 1) + (unsigned long long)v;
            const double planS = ui::audio_dev::renderVoice(sr, v, lines[li], seed, true, st);
            if (st.empty()) continue;
            if (plan) std::printf("v%d \"%s\"\n%s", v, lines[li].c_str(), ui::audio_dev::dumpVoicePlan(v, lines[li], seed).c_str());
            double t[3] = {0, 0, 0}, best[3] = {1e9, 1e9, 0};
            for (int rep = 0; rep < 5; ++rep) // best of 5 (the worker is a background thread on a busy machine)
                if (ui::audio_dev::timeVoice(sr, v, lines[li], seed, t)) {
                    best[0] = std::min(best[0], t[0]);
                    best[1] = std::min(best[1], t[1]);
                }
            std::vector<float> mono(st.size() / 2);
            double ss = 0.0;
            float peak = 0.f;
            for (size_t i = 0; i < mono.size(); ++i) {
                mono[i] = st[2 * i] + st[2 * i + 1];
                peak = std::max(peak, std::fabs(mono[i]));
                ss += (double)mono[i] * mono[i];
            }
            const double rmsDb = 20.0 * std::log10(std::sqrt(ss / std::max<size_t>(1, mono.size())) + 1e-12);
            char name[256];
            std::snprintf(name, sizeof name, "%s/v%d_l%02zu.wav", out.c_str(), v, li);
            writeWav(name, mono, 1, sr);
            if (csv)
                std::fprintf(csv, "%d,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n", v, li, planS, mono.size() / (double)sr, best[0],
                             best[1], peak, rmsDb);
            worst = std::max(worst, best[0] + best[1]);
            sum += best[0] + best[1];
            ++count;
        }
    }
    if (csv) std::fclose(csv);
    std::printf("lines %d, plan+render per line: mean %.3f ms, worst %.3f ms\n", count, count ? sum / count : 0.0, worst);

    if (speak) { // the main thread's cost: Audio::speak() as the game calls it
        SetTraceLogLevel(LOG_WARNING);
        InitAudioDevice();
        if (!IsAudioDeviceReady()) {
            std::fprintf(stderr, "no audio device\n");
            return 1;
        }
        SetMasterVolume(0.f);
        {
            ui::Audio a;
            if (a.init()) {
                double worstUs = 0.0, sumUs = 0.0;
                int n = 0;
                for (int rep = 0; rep < 40; ++rep) {
                    const std::string& l = lines[(size_t)rep % lines.size()];
                    const auto t0 = std::chrono::steady_clock::now();
                    a.speak(1 + rep % 5, l);
                    const double us =
                        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                    worstUs = std::max(worstUs, us);
                    sumUs += us;
                    ++n;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                std::printf("Audio::speak on the main thread: mean %.1f us, worst %.1f us (%d calls)\n", sumUs / n,
                            worstUs, n);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            a.shutdown();
        }
        CloseAudioDevice();
    }
    return 0;
}
