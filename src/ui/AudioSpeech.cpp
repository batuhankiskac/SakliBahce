// Audio (audio owner): the regulars' murmur, planned and rendered from the bubble's text (AudioInternal.h).
#include "ui/AudioInternal.h"

namespace ui {
namespace audio_detail {

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

} // namespace audio_detail
} // namespace ui
