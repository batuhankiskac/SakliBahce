// Audio (audio owner): developer hooks for tools/audio_render.cpp and tools/voice_render.cpp — the exact sample
// buffers and generator classes the game plays, so the tools measure the real thing.
#include "ui/AudioInternal.h"

namespace ui {
using namespace audio_detail;

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
