// r3d::Characters — the regulars' faces (Yüz): expressions that ease in and fade back to each man's resting face,
// blinking, the eyes, the talking mouth and the mustache. Kel Mahmut's face flares up quickly (a loud laugh, a quick
// temper), Emekli Nuri's barely moves, Hacı Rıza is in between and kind.
//
// Expressions (Mood) come from the game events (CharactersAnim.cpp react, Characters::react for the other games,
// crowdReact at a big moment), from being named in someone's line (teased) and from thinking on their turn. Each is a
// target Face (brows, lids, mouth corners, jaw), scaled by the man's own strength around his resting face; the face
// follows at his own onset / release speed. The mustache comes in two halves turned about its middle: the ends rise
// with a smile, droop with a frown and the whole thing lifts with the upper lip — the most readable part of a face
// from the player's seat.
//
// Talking: while his bubble is up the jaw follows the voice (ui::Audio::mouthOpen: the murmur's loudness, 10 ms
// frames) through the Characters::voiceMouth hook. Without a voice (voices off, no audio) the mouth follows a syllable
// schedule built from the text at the voice's own pace: every vowel opens it (a, o wide; ı, i a little; o ö u ü
// round the lips), m/b/p and word ends close it, commas and full stops pause.
#include "r3d/CharactersState.h"

#include <algorithm>
#include <cmath>

namespace r3d {
namespace chr {

namespace {

// Each man's way with his face. [3]: the çaycı (only his talking pace is used).
struct Persona {
    float gain;              // expression strength (around the resting face)
    float onset, release;    // how fast an expression comes / fades back
    float talkJaw;           // jaw drop per unit of voice
    float browTalk;          // brows flick up on loud syllables (m)
    float stache;            // mustache end angle per unit of smile (rad)
    float syl, gap, pause;   // the text schedule's pace (ui::Audio's voice profiles)
    float blinkLen, blinkMin, blinkMax;
};
constexpr Persona kPersona[4] = {
    // Hacı Rıza: kind and slow
    {0.95f, 7.f, 2.6f, 1.00f, 0.0022f, 0.26f, 0.145f, 0.075f, 1.25f, 0.17f, 2.6f, 5.8f},
    // Kel Mahmut: everything at once, and loud
    {1.35f, 15.f, 4.5f, 1.30f, 0.0036f, 0.30f, 0.088f, 0.030f, 0.80f, 0.14f, 1.8f, 4.2f},
    // Emekli Nuri: restrained; a slow, heavy blink
    {0.75f, 4.5f, 1.8f, 0.75f, 0.0012f, 0.20f, 0.128f, 0.060f, 1.15f, 0.21f, 3.0f, 6.5f},
    // the çaycı
    {1.f, 9.f, 3.f, 0.85f, 0.f, 0.f, 0.082f, 0.035f, 0.85f, 0.15f, 2.f, 5.f},
};

constexpr float kVoiceCap = 2.45f;  // the murmur never runs longer (ui::Audio's kVoiceMaxSeconds)

Face restFace(int kind) {
    Face f;
    if (kind == 0) {
        f.smile = 0.15f;
        f.lidOpen = 0.92f;
    } else if (kind == 1) {
        f.smile = 0.25f;
    } else if (kind == 2) {
        f.smile = -0.3f;
        f.browTilt[0] = f.browTilt[1] = 0.08f;
    }
    return f;
}

// The full expression (before the man's own strength). `age`: seconds since it began; `t`: the clock.
Face moodFace(const Face& rest, Mood m, int kind, float age, float t, float& purse) {
    Face f = rest;
    purse = 0.f;
    switch (m) {
    case Mood::Happy:
        f.smile = 0.9f;
        f.browRaise[0] = f.browRaise[1] = 0.003f;
        f.browTilt[0] = f.browTilt[1] = -0.07f;
        f.lidOpen = 0.8f;
        break;
    case Mood::Laugh: {
        f.smile = 1.f;
        f.browRaise[0] = f.browRaise[1] = 0.004f;
        f.browTilt[0] = f.browTilt[1] = -0.10f;
        f.lidOpen = 0.5f;
        // the laugh itself: bursts that die down (Mahmut roars, Rıza chuckles, Nuri barely)
        const float fade = 1.f - smooth01((age - 1.2f) / 1.8f);
        if (kind == 1) f.jaw = (0.42f + 0.30f * std::sin(t * 15.f)) * fade + 0.12f;
        else if (kind == 0) f.jaw = (0.22f + 0.14f * std::sin(t * 10.f)) * fade + 0.05f;
        else f.jaw = (0.10f + 0.06f * std::sin(t * 7.f)) * fade;
        if (kind == 2) f.lidOpen = 0.66f;
        break;
    }
    case Mood::Grumpy:
        f.smile = -1.f;
        f.browRaise[0] = f.browRaise[1] = -0.0035f;
        f.browTilt[0] = f.browTilt[1] = 0.38f;
        f.lidOpen = 0.76f;
        if (kind == 2) { // Nuri: one sceptical brow instead of a scowl
            f.browRaise[0] = 0.0035f;
            f.browTilt[0] = 0.05f;
            purse = 0.4f;
        }
        break;
    case Mood::Surprised: {
        const float settle = 1.f - 0.6f * smooth01((age - 0.8f) / 0.8f);
        f.smile = 0.f;
        f.browRaise[0] = f.browRaise[1] = 0.0075f;
        f.browTilt[0] = f.browTilt[1] = -0.16f;
        f.lidOpen = 1.35f;
        f.mouthWide = 0.8f * settle;
        f.jaw = 0.38f * settle;
        break;
    }
    case Mood::Sad:
        f.smile = -0.7f;
        f.browRaise[0] = f.browRaise[1] = 0.0015f;
        f.browTilt[0] = f.browTilt[1] = -0.40f;
        f.lidOpen = 0.74f;
        break;
    case Mood::Thinking:
        if (kind == 1) { // Mahmut frowns at his tiles and chews his mustache
            f.smile = -0.3f;
            f.browRaise[0] = f.browRaise[1] = -0.002f;
            f.browTilt[0] = f.browTilt[1] = 0.30f;
            f.lidOpen = 0.74f;
            f.jaw = 0.06f + 0.05f * std::sin(t * 5.f);
        } else if (kind == 2) { // Nuri: one brow up, lips pursed
            f.smile = -0.4f;
            f.browRaise[0] = 0.005f;
            f.browRaise[1] = -0.0015f;
            f.browTilt[1] = 0.25f;
            f.lidOpen = 0.8f;
            purse = 0.6f;
        } else { // Rıza weighs it calmly, now and then a quiet "hmm"
            f.smile = 0.f;
            f.browRaise[0] = f.browRaise[1] = 0.0028f;
            f.browTilt[0] = f.browTilt[1] = -0.05f;
            f.lidOpen = 0.8f;
            f.jaw = 0.07f * smooth01((std::sin(t * 1.7f) - 0.75f) * 4.f);
        }
        break;
    case Mood::Smug:
        f.smile = 0.7f;
        f.browRaise[0] = 0.0045f;
        f.browRaise[1] = 0.f;
        f.lidOpen = 0.7f;
        break;
    case Mood::Content:
        f.smile = 0.5f;
        f.lidOpen = 0.86f;
        break;
    default: break;
    }
    return f;
}

Face scaleAround(const Face& rest, const Face& f, float g) {
    Face o;
    for (int i = 0; i < 2; ++i) {
        o.browRaise[i] = rest.browRaise[i] + (f.browRaise[i] - rest.browRaise[i]) * g;
        o.browTilt[i] = rest.browTilt[i] + (f.browTilt[i] - rest.browTilt[i]) * g;
    }
    o.lidOpen = clampf(rest.lidOpen + (f.lidOpen - rest.lidOpen) * g, 0.35f, 1.45f);
    o.smile = clampf(rest.smile + (f.smile - rest.smile) * g, -1.f, 1.f);
    o.jaw = clampf(f.jaw * std::min(g, 1.15f), 0.f, 1.f);
    o.mouthWide = clampf(f.mouthWide * g, 0.f, 1.f);
    return o;
}

bool calmMood(Mood m) { return m == Mood::Neutral || m == Mood::Thinking || m == Mood::Content; }

// ---- the text schedule
char32_t nextLower(const std::string& t, size_t& i) {
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
    case U'Ö': return U'ö';
    case U'Ü': return U'ü';
    case U'Ç': return U'ç';
    case U'Ş': return U'ş';
    case U'Ğ': return U'ğ';
    case U'â': case U'Â': return U'a';
    case U'î': case U'Î': return U'i';
    case U'û': case U'Û': return U'u';
    default: break;
    }
    if (cp >= U'A' && cp <= U'Z') cp += 32;
    return cp;
}

// vowel -> how far the jaw drops (0: not a vowel), rounded lips
float vowelOpen(char32_t c, float& round) {
    round = 0.f;
    switch (c) {
    case U'a': return 0.95f;
    case U'e': return 0.72f;
    case U'ı': return 0.50f;
    case U'i': return 0.48f;
    case U'o': round = 1.f; return 0.85f;
    case U'ö': round = 1.f; return 0.70f;
    case U'u': round = 1.f; return 0.60f;
    case U'ü': round = 1.f; return 0.55f;
    default: return 0.f;
    }
}
bool isLetter(char32_t c) {
    return (c >= U'a' && c <= U'z') || c == U'ç' || c == U'ğ' || c == U'ı' || c == U'ö' || c == U'ş' || c == U'ü';
}
bool isLabial(char32_t c) { return c == U'm' || c == U'b' || c == U'p' || c == U'f' || c == U'v'; }

void buildSchedule(const std::string& text, const Persona& P, std::vector<MouthSyl>& out, char& punct) {
    out.clear();
    punct = 0;
    std::vector<char32_t> cps;
    cps.reserve(text.size());
    for (size_t i = 0; i < text.size();) cps.push_back(nextLower(text, i));
    float t = 0.f;
    size_t i = 0;
    while (i < cps.size()) {
        const char32_t c = cps[i];
        if (isLetter(c) || (c >= U'0' && c <= U'9')) {
            // one word
            size_t j = i;
            while (j < cps.size() && (isLetter(cps[j]) || (cps[j] >= U'0' && cps[j] <= U'9') || cps[j] == U'\'')) ++j;
            const size_t first = out.size();
            for (size_t k = i; k < j; ++k) {
                const char32_t w = cps[k];
                float round = 0.f;
                const float open = vowelOpen(w, round);
                if (w >= U'0' && w <= U'9') { // a digit is a short word of its own ("beş", "yedi")
                    for (int r = 0; r < 2; ++r) {
                        out.push_back({t, P.syl, 0.6f, 0.3f, 0.f});
                        t += P.syl;
                    }
                    continue;
                }
                if (open <= 0.f) continue;
                if (!out.empty() && out.size() > first && k > i && cps[k - 1] == w) { // "Gooool": a long vowel
                    out.back().dur += P.syl * 0.6f;
                    t += P.syl * 0.6f;
                    continue;
                }
                // its coda: the consonants up to the next vowel but the last (the next syllable's onset)
                size_t n = k + 1;
                int cons = 0;
                while (n < j && vowelOpen(cps[n], round) <= 0.f) ++n, ++cons;
                vowelOpen(w, round);
                const bool last = n >= j;
                const int coda = std::min(2, last ? cons : std::max(0, cons - 1));
                const float d = P.syl * (1.f + 0.18f * (float)coda);
                const float vary = 0.9f + 0.05f * (float)((k * 7u) % 3u);
                MouthSyl s;
                s.t0 = t;
                s.dur = d;
                s.open = std::min(1.f, open * vary * (last ? 1.08f : 1.f));  // Turkish stress: the word's end
                s.round = round;
                s.closeTo = last || (n < j && n > 0 && isLabial(cps[n - 1])) ? 0.05f : 0.22f;
                out.push_back(s);
                t += d;
            }
            if (out.size() == first) { // no vowel at all ("Hmm", "Şşt"): a hummed syllable
                out.push_back({t, P.syl * 0.9f, 0.14f, 0.f, 0.3f});
                t += P.syl * 0.9f;
            }
            t += P.gap;
            i = j;
            continue;
        }
        if (c == U',' || c == U';' || c == U':') t += 0.16f * P.pause;
        else if (c == U'.' || c == U'!' || c == U'?' || c == U'…') t += 0.28f * P.pause;
        if (c == U'!' || c == U'?') punct = (char)c;
        ++i;
    }
    if (out.empty()) return;
    // the voice talks a little faster to fit its cap, then stops at a word: so does the mouth
    const float total = out.back().t0 + out.back().dur;
    if (total > kVoiceCap) {
        const float k = std::max(1.f / 1.3f, kVoiceCap / total);
        for (MouthSyl& s : out) {
            s.t0 *= k;
            s.dur *= k;
        }
        while (!out.empty() && out.back().t0 + out.back().dur > kVoiceCap) out.pop_back();
        if (!out.empty()) out.back().closeTo = 0.f;
    }
}

float scheduleAt(const std::vector<MouthSyl>& syl, float t, float& round) {
    round = 0.f;
    for (size_t i = 0; i < syl.size(); ++i) {
        const MouthSyl& s = syl[i];
        if (t < s.t0) return 0.f;          // a pause before it
        if (t >= s.t0 + s.dur) continue;
        const float u = (t - s.t0) / s.dur;
        float start = 0.f;
        if (i > 0 && syl[i - 1].t0 + syl[i - 1].dur >= s.t0 - 1e-3f) start = syl[i - 1].open * syl[i - 1].closeTo;
        round = s.round;
        if (u < 0.32f) return lerpf(start, s.open, smooth01(u / 0.32f));
        if (u < 0.62f) return s.open;
        return lerpf(s.open, s.open * s.closeTo, smooth01((u - 0.62f) / 0.38f));
    }
    return -1.f;  // past the end
}

} // namespace

// ============================================================================ talking
void Cast::startTalk(TalkMouth& tm, const std::string& text, int kind) {
    buildSchedule(text, kPersona[std::clamp(kind, 0, 3)], tm.syl, tm.punct);
    tm.src = 0;
    tm.open = tm.round = 0.f;
}

float Cast::talkOpen(TalkMouth& tm, int who, float t, float dt) {
    (void)dt;
    if (tm.src != 2 && owner && owner->voiceMouth) {
        const float v = owner->voiceMouth(who);
        if (v >= 0.f) {
            tm.src = 1;
            tm.round = 0.f;
            return v;
        }
        if (tm.src == 1) return -1.f;  // the voice said it (or was cut off): the mouth rests
    }
    tm.src = 2;
    return scheduleAt(tm.syl, t, tm.round);
}

// ============================================================================ reactions
void Cast::moodNudge(Opponent& o, Mood m, float seconds) {
    if (calmMood(o.mood) || o.moodT < 0.6f) setMood(o, m, seconds);
}

void Cast::faceReact(int seat, int mood, Vector3 lookAt) {
    (void)lookAt;
    if (seat < 1 || seat > 3 || mood <= 0) return;
    Opponent& o = opp[seat];
    const bool free = o.gest == G_None;  // (only head and shoulder gestures here: the hands may hold cards)
    if (mood == 1) {
        if (o.kind == 1) {
            setMood(o, Mood::Laugh, 3.f);
            if (free) startGesture(o, G_Laugh, 1.6f);
        } else if (o.kind == 2) {
            setMood(o, Mood::Smug, 3.f);
        } else {
            setMood(o, Mood::Happy, 2.8f);
            if (free) startGesture(o, G_Nod, 0.9f);
        }
    } else if (mood == 2) {
        if (o.kind == 1) {
            setMood(o, Mood::Grumpy, 3.5f);
            if (free) startGesture(o, G_Grumble, 1.5f);
        } else if (o.kind == 2) {
            setMood(o, Mood::Grumpy, 3.5f);
            if (free) startGesture(o, G_HeadShake, 1.1f);
        } else {
            setMood(o, Mood::Sad, 3.f);
        }
    } else {
        setMood(o, Mood::Surprised, o.kind == 2 ? 1.6f : 2.2f);
    }
    // the others at the table have something on their faces too: Mahmut enjoys a neighbour's bad luck, Nuri
    // doesn't like anybody's good luck
    for (int s = 1; s <= 3; ++s) {
        if (s == seat) continue;
        Opponent& q = opp[s];
        if (mood == 2 && q.kind == 1 && rng.chance(0.5f)) {
            q.pendMood = Mood::Laugh;
            q.pendFor = 2.f;
            q.pendGest = G_Laugh;
            q.pendIn = rng.f(0.35f, 0.7f);
        } else if (mood == 1 && q.kind == 2 && rng.chance(0.4f)) {
            q.pendMood = Mood::Grumpy;
            q.pendFor = 2.2f;
            q.pendGest = 0;
            q.pendIn = rng.f(0.4f, 0.9f);
        }
    }
}

void Cast::faceCrowd(int kind) {
    for (int s = 1; s <= 3; ++s) {
        Opponent& o = opp[s];
        if (!rng.chance(o.kind == 2 ? 0.4f : 0.8f)) continue;
        Mood m = Mood::Surprised;
        if (kind == 2) m = o.kind == 1 ? Mood::Laugh : (o.kind == 0 ? Mood::Content : Mood::Smug);
        else if (kind == 1) m = o.kind == 1 ? Mood::Grumpy : Mood::Sad;
        o.pendMood = m;
        o.pendFor = m == Mood::Surprised ? 1.4f : 2.f;
        o.pendGest = 0;
        o.pendIn = rng.f(0.05f, 0.35f);
    }
}

void Cast::onLineFace(int who, const std::string& text) {
    if (who == 4) {
        startTalk(boy.tm, text, 3);
        return;
    }
    if (who < 1 || who > 3) return;
    Opponent& sp = opp[who];
    startTalk(sp.tm, text, sp.kind);
    // somebody named in the line hears it (teased, mostly): the speaker gets a look and a face
    for (int s = 1; s <= 3; ++s) {
        if (s == who) continue;
        const std::string& n = names[(size_t)s];
        const size_t sp0 = n.rfind(' ');
        const std::string key = sp0 == std::string::npos ? n : n.substr(sp0 + 1);
        if (key.size() < 3 || text.find(key) == std::string::npos) continue;
        Opponent& q = opp[s];
        if (!calmMood(q.mood) && q.moodT > 0.6f) continue;
        Mood m = Mood::Content;
        int g = 0;
        if (q.kind == 1) {
            if (rng.chance(0.55f)) {
                m = Mood::Grumpy;
                g = G_Grumble;
            } else {
                m = Mood::Laugh;
                g = G_Laugh;
            }
        } else if (q.kind == 2) {
            m = rng.chance(0.6f) ? Mood::Smug : Mood::Thinking;
        } else {
            m = rng.chance(0.7f) ? Mood::Content : Mood::Happy;
        }
        q.pendMood = m;
        q.pendFor = 2.4f;
        q.pendGest = g;
        q.pendIn = rng.f(0.45f, 0.9f);
        q.gazeGoal = headTarget(who);
        q.gazeHold = std::max(q.gazeHold, 1.4f);
    }
}

// ============================================================================ per frame
void Cast::faceBody(Opponent& o, float dt) {
    const Persona& P = kPersona[std::clamp(o.kind, 0, 2)];
    // a delayed reaction (named in a line, a neighbour's luck, the crowd)
    if (o.pendIn >= 0.f) {
        o.pendIn -= dt;
        if (o.pendIn < 0.f) {
            moodNudge(o, o.pendMood, o.pendFor);
            if (o.pendGest && o.gest == G_None && o.mood == o.pendMood) startGesture(o, o.pendGest, 1.5f);
        }
    }
    // the expression's age (and a blink as a new one sets in)
    if (o.mood != o.faceMood) {
        o.faceMood = o.mood;
        o.moodAge = 0.f;
        if (o.mood != Mood::Neutral && o.mood != Mood::Thinking && o.blinkT < 0.f && rng.chance(0.55f))
            o.blinkIn = std::min(o.blinkIn, 0.06f);
    } else {
        o.moodAge += dt;
    }
    // the head goes with the face (+ pitch = up)
    const float on = smooth01(o.moodAge / 0.3f) * std::min(P.gain, 1.2f);
    switch (o.mood) {
    case Mood::Surprised: o.nodPitch += 0.08f * on * (1.f - smooth01((o.moodAge - 1.f) / 0.8f)); break;
    case Mood::Grumpy: o.nodPitch -= 0.05f * on; break;
    case Mood::Sad: o.nodPitch -= 0.08f * on; break;
    case Mood::Happy: o.nodPitch += 0.03f * on; break;
    case Mood::Laugh:
        if (o.gest != G_Laugh && o.gest != G_Celebrate)
            o.nodPitch += (0.05f + 0.035f * std::sin(time * 15.f) * (o.kind == 1)) * on * (1.f - smooth01((o.moodAge - 1.5f) / 1.5f));
        break;
    case Mood::Smug: o.nodPitch += 0.04f * on; o.nodRoll += 0.04f * on; break;
    default: break;
    }
    // talking: the line's clock and the mouth (the head bobs with the open syllables)
    o.tm.open = 0.f;
    if (o.talkT >= 0.f) {
        o.talkT += dt;
        if (o.talkT > o.talkDur) {
            o.talkT = -1.f;
        } else {
            const float v = talkOpen(o.tm, o.seat, o.talkT, dt);
            if (v >= 0.f) {
                o.tm.open = v;
                o.nodPitch += 0.03f * (v - 0.35f) * (o.kind == 1 ? 1.4f : (o.kind == 2 ? 0.6f : 1.f));
                if (o.tm.punct == '?') o.nodRoll += 0.05f * smooth01((o.talkT / std::max(0.5f, o.talkDur) - 0.4f) * 3.f);
            }
        }
    }
}

void Cast::updateFace(Opponent& o, float dt, float jawGest) {
    const Persona& P = kPersona[std::clamp(o.kind, 0, 2)];
    const Face rest = restFace(o.kind);
    float purse = 0.f;
    const Face full = moodFace(rest, o.mood, o.kind, o.moodAge, time + (float)(o.seed % 97u), purse);
    Face goal = scaleAround(rest, full, P.gain);
    if (o.gest == G_Drink && o.gestT > 1.4f && o.gestT < 2.7f) goal.lidOpen = std::min(goal.lidOpen, 0.45f);
    if (o.drag > 0.3f) goal.lidOpen = std::min(goal.lidOpen, 0.6f);
    // talking: brows flick up on the loud syllables; an exclamation keeps them up, a question lifts them at the end
    const bool talking = o.talkT >= 0.f && o.tm.open > 0.f;
    o.emph = approachExp(o.emph, talking ? std::max(0.f, o.tm.open - 0.45f) * 1.8f : 0.f, 14.f, dt);
    float browUp = P.browTalk * o.emph;
    if (o.talkT >= 0.f && o.tm.punct == '!') browUp += P.browTalk * 0.6f;
    if (o.talkT >= 0.f && o.tm.punct == '?') browUp += P.browTalk * 0.9f * smooth01((o.talkT / std::max(0.5f, o.talkDur) - 0.4f) * 3.f);
    // the face follows at the man's own speed: quick into an expression, slower back to rest
    const float fr = o.mood == Mood::Neutral ? P.release : P.onset;
    for (int i = 0; i < 2; ++i) {
        o.face.browRaise[i] = approachExp(o.face.browRaise[i], goal.browRaise[i], fr, dt);
        o.face.browTilt[i] = approachExp(o.face.browTilt[i], goal.browTilt[i], fr, dt);
    }
    o.face.lidOpen = approachExp(o.face.lidOpen, goal.lidOpen, fr, dt);
    o.face.smile = approachExp(o.face.smile, goal.smile, fr * 0.75f, dt);
    o.face.mouthWide = approachExp(o.face.mouthWide, goal.mouthWide, fr, dt);
    o.face.jaw = goal.jaw;

    // the jaw: the expression, a gesture (cheering, laughing) or the voice, whichever opens it most
    float jawGoal = std::max(goal.jaw, jawGest);
    float round = purse;
    if (talking) {
        jawGoal = std::min(1.f, std::max(jawGoal, o.tm.open * P.talkJaw));
        round = std::max(round, o.tm.round * 0.6f * o.tm.open);
    }
    spring(o.jaw, o.jawV, jawGoal, talking ? 55.f : 28.f, dt);
    o.jaw = clampf(o.jaw, 0.f, 1.f);

    // blinking (each at his own pace; a quick turn of the head brings one on)
    o.blinkIn -= dt;
    if (std::fabs(o.hYawV) > 3.5f && o.blinkT < 0.f && rng.chance(dt * 6.f)) o.blinkIn = 0.f;
    if (o.blinkIn <= 0.f && o.blinkT < 0.f) {
        o.blinkT = 0.f;
        o.blinkIn = rng.chance(0.18f) ? 0.28f : rng.f(P.blinkMin, P.blinkMax);
    }
    if (o.blinkT >= 0.f) {
        o.blinkT += dt;
        const float bd = P.blinkLen;
        o.lidClose = std::sin(clampf(o.blinkT / bd, 0.f, 1.f) * PI_F);
        if (o.blinkT >= bd) {
            o.blinkT = -1.f;
            o.lidClose = 0.f;
        }
    }

    // eyes: aim each eye at the gaze point (head space), with micro saccades
    o.microIn -= dt;
    if (o.microIn <= 0.f) {
        o.microIn = rng.f(0.35f, 1.4f);
        o.micro = {rng.f(-0.035f, 0.035f), rng.f(-0.025f, 0.025f)};
    }
    const FaceGeo& fg = o.pm->face;
    const Matrix headInv = MatrixInvert(o.headW);
    const Vector3 tH = xfPoint(headInv, o.gaze);
    const Vector3 mid = Vector3Lerp(fg.eye[0], fg.eye[1], 0.5f);
    const Vector3 d = Vector3Subtract(tH, mid);
    const float eyGoal = clampf(std::atan2(-d.x, -d.z), -0.55f, 0.55f) + o.micro.x;
    const float epGoal = clampf(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)), -0.45f, 0.35f) + o.micro.y;
    o.eYaw = approachExp(o.eYaw, eyGoal, 30.f, dt);
    o.ePitch = approachExp(o.ePitch, epGoal, 30.f, dt);
    const float es = fg.eyeR / 0.0135f;
    const float squint = clampf(1.f - o.face.lidOpen, -0.45f, 0.65f);
    const float openEdge = -0.40f + squint * 0.55f - o.ePitch * 0.55f;
    const float edge = lerpf(openEdge, 0.60f, std::max(o.lidClose, o.gest == G_Laugh ? 0.35f : 0.f));
    for (int i = 0; i < 2; ++i) {
        // converge slightly on near targets
        const Vector3 de = Vector3Subtract(tH, fg.eye[i]);
        const float ey = clampf(std::atan2(-de.x, -de.z), -0.55f, 0.55f);
        const float yawI = lerpf(o.eYaw, ey + o.micro.x, 0.22f);  // a little convergence, never cross-eyed
        o.eyeW[i] = mul(mul(S3(es, es, es), RX(o.ePitch), RY(yawI)), T(fg.eye[i]), o.headW);
        o.lidW[i] = mul(mul(S3(es, es, es), RX(-edge), RY(yawI * 0.25f)), T(fg.eye[i]), o.headW);
        const float sgn = i == 0 ? 1.f : -1.f;
        const Vector3 bp = Vector3Add(fg.brow[i], {0, o.face.browRaise[i] + browUp, 0});
        o.browW[i] = mul(RZ(sgn * o.face.browTilt[i]), T(bp), o.headW);
    }
    // the mouth: the dark inside drops with the jaw (a little more than life: it must read across the table)
    const float jawAmt = clampf(o.jaw + o.face.mouthWide * 0.4f, 0.f, 1.f);
    const Vector3 mc = Vector3Add(fg.mouth, {0, -0.0065f * jawAmt, 0.004f - 0.004f * smooth01(jawAmt * 4.f)});
    const float sx = 1.f + 0.18f * o.face.smile - 0.25f * o.face.mouthWide - 0.28f * round + 0.12f * jawAmt;
    o.mouthW = mul(S3(sx, 0.06f + 1.15f * jawAmt, 1.f), T(mc), o.headW);
    o.lipVariant = std::clamp((int)std::lround(o.face.smile * 2.f) + 2, 0, 4);
    const float lipSx = 1.f - 0.22f * round;
    o.lipW = mul(S3(lipSx, 1.f, 1.f), T(Vector3Add(fg.mouth, {0, -0.0058f - 0.0145f * jawAmt, -0.0015f})), o.headW);

    // the mustache: its ends follow the mouth corners, the upper lip lifts it as the mouth opens
    float angGoal = clampf(P.stache * o.face.smile, -0.32f, 0.34f);
    if (o.mood == Mood::Laugh && o.kind == 1) angGoal += 0.05f * std::sin(time * 15.f) * (1.f - smooth01((o.moodAge - 1.2f) / 1.8f));
    o.stacheAng = approachExp(o.stacheAng, angGoal, 18.f, dt);
    o.stacheLift = 0.0026f * jawAmt + 0.0012f * std::max(0.f, o.face.smile);
    const Vector3 pv = o.pm->stachePivot;
    for (int w = 0; w < 2; ++w) {
        const float a = (w == 0 ? 1.f : -1.f) * o.stacheAng;
        o.stacheW[w] = mul(mul(T(Vector3Negate(pv)), RZ(a)), T(Vector3Add(pv, {0, o.stacheLift, 0})), o.headW);
    }
}

// ============================================================================ the standing men's heads (the çaycı, the ocakçı, the bystanders)
void headLookSpring(const Matrix& torsoW, const PersonLook& L, Vector3 gaze, float pitchGain, float pitchMin, float omega,
                    float dt, float& yaw, float& yawV, float& pitch, float& pitchV) {
    const Vector3 tgt = xfPoint(MatrixInvert(torsoW), gaze);
    const Vector3 eyes{0, L.spineLen + 0.10f, L.headZ - 0.07f};
    const Vector3 d = Vector3Subtract(tgt, eyes);
    const float yawN = clampf(std::atan2(-d.x, -d.z) * 0.8f, -1.1f, 1.1f);
    const float pitchN = clampf(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * pitchGain, pitchMin, 0.4f);
    spring(yaw, yawV, yawN, omega, dt);
    spring(pitch, pitchV, pitchN, omega, dt);
}

float blinkStep(Rng& rng, float& blinkIn, float& blinkT, float dt, float maxGap, float dur) {
    blinkIn -= dt;
    if (blinkIn <= 0.f && blinkT < 0.f) {
        blinkT = 0.f;
        blinkIn = rng.f(2.f, maxGap);
    }
    float lidClose = 0.f;
    if (blinkT >= 0.f) {
        blinkT += dt;
        lidClose = std::sin(clampf(blinkT / dur, 0.f, 1.f) * PI_F);
        if (blinkT > dur) blinkT = -1.f;
    }
    return lidClose;
}

void standingEyes(const FaceGeo& fg, const Matrix& headW, Vector3 gaze, float lidBase, float lidClose, float browTilt, float dt,
                  float& eYaw, float& ePitch, Matrix eyeW[2], Matrix lidW[2], Matrix browW[2]) {
    const Vector3 tH = xfPoint(MatrixInvert(headW), gaze);
    const Vector3 dd = Vector3Subtract(tH, Vector3Lerp(fg.eye[0], fg.eye[1], 0.5f));
    eYaw = approachExp(eYaw, clampf(std::atan2(-dd.x, -dd.z), -0.5f, 0.5f), 25.f, dt);
    ePitch = approachExp(ePitch, clampf(std::atan2(dd.y, std::sqrt(dd.x * dd.x + dd.z * dd.z)), -0.45f, 0.35f), 25.f, dt);
    const float edge = lerpf(lidBase - ePitch * 0.55f, 0.6f, lidClose);
    const float es = fg.eyeR / 0.0135f;
    for (int i = 0; i < 2; ++i) {
        eyeW[i] = mul(mul(S3(es, es, es), RX(ePitch), RY(eYaw)), T(fg.eye[i]), headW);
        lidW[i] = mul(mul(S3(es, es, es), RX(-edge)), T(fg.eye[i]), headW);
        browW[i] = mul(RZ((i == 0 ? 1.f : -1.f) * browTilt), T(fg.brow[i]), headW);
    }
}

void standingMouth(const FaceGeo& fg, const Matrix& headW, float jaw, Matrix& mouthW, Matrix& lipW) {
    mouthW = mul(S3(1.1f, 0.06f + 0.9f * jaw, 1.f), T(Vector3Add(fg.mouth, {0, -0.004f * jaw, 0.004f - 0.004f * smooth01(jaw * 4.f)})),
                 headW);
    lipW = mul(T(Vector3Add(fg.mouth, {0, -0.0058f - 0.009f * jaw, -0.0015f})), headW);
}

} // namespace chr
} // namespace r3d
