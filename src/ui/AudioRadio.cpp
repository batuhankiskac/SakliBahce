// Audio (audio owner): the old radio's synthesised Turkish folk (AudioInternal.h: RadioGen).
#include "ui/AudioInternal.h"

namespace ui {
namespace audio_detail {

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

} // namespace audio_detail
} // namespace ui
