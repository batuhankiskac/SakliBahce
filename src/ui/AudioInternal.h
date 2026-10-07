#pragma once
// Audio internals (audio owner): what the parts of src/ui/Audio*.cpp share — the sound tables, the generators' and
// the voices' types, the state shared with raylib's audio thread and the functions one part calls in another.
// Not a public API. The parts: AudioSfx.cpp (one-shot effects), AudioAmbience.cpp (the room), AudioRadio.cpp (the
// synthesised radio), AudioSpeech.cpp (the regulars' murmur), Audio.cpp (the facade, the voice bus and worker, the
// audio-thread callbacks), AudioDev.cpp (the developer hooks for tools/audio_render.cpp and tools/voice_render.cpp).
#include "ui/AudioDsp.h"

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

namespace audio_detail {

// ---- AudioSfx.cpp

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

inline float degreeCents(const Makam& m, int d) {
    const int oct = (d >= 0) ? d / 7 : -((-d + 6) / 7);
    const int idx = d - 7 * oct;
    return m.cents[idx] + 1200.f * (float)oct;
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

struct SfxBank {
    float sr = 48000.f;
    std::array<std::vector<Buf>, (size_t)Sfx::Count> vars;
};

std::unique_ptr<SfxBank> buildSfxBank(float sr, uint64_t seed);
// (the ambience's far-off table sounds and the radio's percussion are made by the effects' own designs)
void tileOnFelt(Synth& s, Buf& b, float t0, float gain, float fs, float contact);
void glassClinks(Synth& s, Buf& b, float t, int n, float f0, float gain);
Buf sfxGlassSet(Synth& s);
Buf percDum(Synth& s, float gain);
Buf percTek(Synth& s, float gain, float contact);
Buf sfxDiceRaw(Synth& s);
Buf ambCheckers(Synth& s);
Buf sfxChair(Synth& s, int kind);
Buf ambTray(Synth& s);
Buf ambCough(Synth& s);
Buf distant(Buf b, float sr, float lpHz, float peakDb);

// ---- AudioAmbience.cpp

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

// ---- AudioRadio.cpp

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

// ---- AudioSpeech.cpp

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

void planSpeech(const std::string& text, const VoiceProfile& vp, okey::Rng& rng, VoicePlan& P);
int renderSpeech(const VoicePlan& P, const VoiceProfile& vp, float sr, uint64_t seed, float* out, int cap);

// ---- Audio.cpp: the voice bus and its worker

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

extern std::atomic<VoiceBus*> gVoices;

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

// (Audio.cpp) the voices' placement and per-line profile
void voiceSpatial(int voice, okey::Rng& r, float& gl, float& gr);
VoiceProfile lineProfile(int voice, okey::Rng& r);

// ---- Audio.cpp: live state shared with raylib's audio thread

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

extern std::atomic<AmbienceGen*> gAmb;
extern std::atomic<RadioGen*> gRadio;
extern std::atomic<const SfxBank*> gBank;
extern std::atomic<int> gRate;
extern CbStats gAmbStats, gRadioStats;

// Real recordings on the radio (raylib Music streams from assets/music): this processor puts them through the
// set's small speaker — a wide band-pass (warmer and clearer than the synthesised tunes' 300..3500 Hz, the
// songs must stay pleasant), a boxy resonance, gentle saturation and the wall reflection. It runs on raylib's
// audio thread on the stream converted to the device format (float, stereo, device rate).
struct RecordFx {
    Biquad hp1, hp2, lp1, lp2, pk, lowShelf;
    std::vector<float> refl;
    uint32_t reflW = 0, reflD = 1;
    std::atomic<bool> reset{true};
    // The radio's on/off switch ramps the record over ~150 ms here, sample by sample, instead of a bare
    // Pause/ResumeMusicStream (a click): the main thread sets the target and pauses once gainNow reaches 0.
    std::atomic<float> gainTarget{1.f}, gainNow{1.f};
    float gain = 1.f, gainStep = 1.f / 7200.f;
    void init(float sr) {
        gainStep = 1.f / (.15f * sr);
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
            gain = gainTarget.load(std::memory_order_relaxed);
        }
        const float target = gainTarget.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < frames; ++i) {
            const float in = .5f * (x[2 * i] + x[2 * i + 1]);
            float y = lowShelf.process(pk.process(lp2.process(lp1.process(hp2.process(hp1.process(in))))));
            y = softClip(y * 1.35f) / 1.35f;
            refl[reflW & 4095] = y;
            const float rf = refl[(reflW - reflD) & 4095];
            ++reflW;
            x[2 * i] = y * .86f + rf * .14f;
            x[2 * i + 1] = y + rf * .09f;
            if (gain != target) { // (at rest the gain is exactly 1: the samples are untouched)
                gain = gain < target ? std::min(target, gain + gainStep) : std::max(target, gain - gainStep);
            }
            if (gain != 1.f) {
                x[2 * i] *= gain;
                x[2 * i + 1] *= gain;
            }
        }
        gainNow.store(gain, std::memory_order_relaxed);
    }
};
extern std::atomic<RecordFx*> gRecFx;
// The records sit a little under the synthesised radio's level (-18 LUFS masters): a song in the room, not
// over the table (SetMusicVolume at full master volume).
constexpr float kRecordGain = .5f;

} // namespace audio_detail
} // namespace ui
