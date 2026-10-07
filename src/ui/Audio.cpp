// Audio for SaklıBahçe (audio owner).
//
// The old radio plays real recordings from assets/music (see RecordFx and Audio::Impl::updateRadio; credits in
// assets/music/KAYNAKLAR.md); everything else is synthesised. At init() every one-shot effect is made (modal resonator banks
// excited by short contact pulses, filtered noise, Karplus-Strong strings) into a few random
// variations, each loaded as a raylib Sound with a small alias pool so that copies can overlap.
// The coffeehouse ambience and the old radio are endless generators pulled by raylib's audio thread
// through AudioStream callbacks: they cannot underrun when the game loop hitches, and they are
// sample-continuous (no buffer seams). The main thread talks to them only through atomics.
// This file: the facade (Audio::Impl), the voice bus and worker, the audio-thread callbacks; the other parts are listed
// in AudioInternal.h.
#include "ui/AudioInternal.h"

namespace ui {
namespace audio_detail {

std::atomic<VoiceBus*> gVoices{nullptr};

void voiceCallback(void* data, unsigned int frames) {
    audioThreadNoDenormals();
    float* out = static_cast<float*>(data);
    if (VoiceBus* v = gVoices.load(std::memory_order_acquire))
        v->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
}

// ------------------------------------------------------------------------------------------------
// Live state shared with raylib's audio thread
// ------------------------------------------------------------------------------------------------

std::atomic<AmbienceGen*> gAmb{nullptr};
std::atomic<RadioGen*> gRadio{nullptr};
std::atomic<const SfxBank*> gBank{nullptr};
std::atomic<int> gRate{0};
CbStats gAmbStats, gRadioStats;

uint64_t nanosSince(std::chrono::steady_clock::time_point t0) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}

void ambienceCallback(void* data, unsigned int frames) {
    audioThreadNoDenormals();
    const auto t0 = std::chrono::steady_clock::now();
    float* out = static_cast<float*>(data);
    if (AmbienceGen* g = gAmb.load(std::memory_order_acquire))
        g->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
    gAmbStats.record(frames, nanosSince(t0));
}

void radioCallback(void* data, unsigned int frames) {
    audioThreadNoDenormals();
    const auto t0 = std::chrono::steady_clock::now();
    float* out = static_cast<float*>(data);
    if (RadioGen* g = gRadio.load(std::memory_order_acquire))
        g->render(out, (int)frames);
    else
        std::memset(out, 0, sizeof(float) * 2 * frames);
    gRadioStats.record(frames, nanosSince(t0));
}

std::atomic<RecordFx*> gRecFx{nullptr};

void recordProcessor(void* data, unsigned int frames) {
    audioThreadNoDenormals();
    if (RecordFx* fx = gRecFx.load(std::memory_order_acquire)) fx->process(static_cast<float*>(data), frames);
}

// Safety net on the final device mix: transparent below -1.9 dBFS (every single sound peaks at
// -3 dBFS or lower), soft saturation above, so overlapping effects never hard-clip. Stateless.
void mixLimiter(void* data, unsigned int frames) {
    audioThreadNoDenormals();
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

} // namespace audio_detail

using namespace audio_detail;

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
    float musicFadeOut = 0.f; // seconds the record has been fading out after the radio was switched off
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
    recFx->gainTarget.store(1.f, std::memory_order_relaxed);
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
    if (!musicOn) {  // switched off: the song fades out (~150 ms in recordProcessor), then waits where it was
        if (musicPaused) return;
        recFx->gainTarget.store(0.f, std::memory_order_relaxed);
        musicFadeOut += dt;
        UpdateMusicStream(music); // keep the stream fed while it fades
        if (recFx->gainNow.load(std::memory_order_relaxed) <= 0.f || musicFadeOut > .5f ||
            !IsMusicStreamPlaying(music)) {
            PauseMusicStream(music);
            musicPaused = true;
        }
        return;
    }
    musicFadeOut = 0.f;
    recFx->gainTarget.store(1.f, std::memory_order_relaxed); // ramps back up from where the fade left it
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

namespace audio_detail {
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
} // namespace audio_detail

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

} // namespace ui
