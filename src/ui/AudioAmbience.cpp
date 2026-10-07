// Audio (audio owner): the coffeehouse ambience generator (AudioInternal.h: AmbienceGen).
#include "ui/AudioInternal.h"

namespace ui {
namespace audio_detail {

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

} // namespace audio_detail
} // namespace ui
