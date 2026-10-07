#pragma once
// Turkish text helpers shared by the game engines (event texts with player names): a small UTF-8 decoder, Turkish
// upper / lower case (i <-> İ, ı <-> I, ç ğ ö ş ü) and the case suffixes of a name by vowel harmony. Header-only.
#include <string>
#include <vector>

namespace trtext {

// The code points of `s` (names are short; a stray or truncated byte passes through as itself).
inline std::vector<unsigned> codePoints(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        size_t extra = 0;
        unsigned cp = c;
        if (c >= 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else if (c >= 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if (c >= 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        }
        if (i + extra >= s.size()) extra = 0;
        for (size_t k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(extra ? cp : c);
        i += 1 + extra;
    }
    return out;
}

inline void appendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

// Turkish case mapping of one code point: i <-> İ (U+0130), ı (U+0131) <-> I, ç ğ ö ş ü <-> Ç Ğ Ö Ş Ü, ASCII.
inline unsigned upperCp(unsigned c) {
    switch (c) {
    case 'i': return 0x130;
    case 0x131: return 'I';
    case 0xE7: return 0xC7;   // ç
    case 0x11F: return 0x11E; // ğ
    case 0xF6: return 0xD6;   // ö
    case 0x15F: return 0x15E; // ş
    case 0xFC: return 0xDC;   // ü
    default: return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    }
}
inline unsigned lowerCp(unsigned c) {
    switch (c) {
    case 'I': return 0x131;
    case 0x130: return 'i';
    case 0xC7: return 0xE7;
    case 0x11E: return 0x11F;
    case 0xD6: return 0xF6;
    case 0x15E: return 0x15F;
    case 0xDC: return 0xFC;
    default: return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
    }
}

inline std::string upperTR(const std::string& s) {
    std::string out;
    for (unsigned c : codePoints(s)) appendUtf8(out, upperCp(c));
    return out;
}
inline std::string lowerTR(const std::string& s) {
    std::string out;
    for (unsigned c : codePoints(s)) appendUtf8(out, lowerCp(c));
    return out;
}

// The first letter in upper case, the rest as it is ("ısmarladın" -> "Ismarladın", "iki" -> "İki").
inline std::string capitalizeFirst(const std::string& s) {
    if (s.empty()) return s;
    const unsigned char c = (unsigned char)s[0];
    size_t len = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (len > s.size()) len = 1;
    const std::vector<unsigned> first = codePoints(s.substr(0, len));
    if (first.size() != 1) return s;
    const unsigned up = upperCp(first[0]);
    if (up == first[0]) return s;
    std::string out;
    appendUtf8(out, up);
    return out + s.substr(len);
}

// Vowels: 'a' (a ı), 'o' (o u), 'e' (e i), 'u' (ö ü) by the class they give a suffix; 0 = not a vowel.
inline char vowelClass(unsigned c) {
    switch (lowerCp(c)) {
    case 'a': case 0x131: return 'a';
    case 'o': case 'u': return 'o';
    case 'e': case 'i': return 'e';
    case 0xF6: case 0xFC: return 'u';
    default: return 0;
    }
}

// The class of the last vowel of `cps` (0: none).
inline char lastVowel(const std::vector<unsigned>& cps) {
    char v = 0;
    for (unsigned c : cps)
        if (const char k = vowelClass(c)) v = k;
    return v;
}

// true: the last vowel is a back vowel (a ı o u; also when there is none) -> suffixes with "a"; false -> "e".
inline bool backHarmony(const std::vector<unsigned>& cps) {
    const char v = lastVowel(cps);
    return v == 0 || v == 'a' || v == 'o';
}

// Ends in a voiceless consonant (f s t k ç ş h p: "fıstıkçı şahap"): d -> t in the suffix.
inline bool endsHard(const std::vector<unsigned>& cps) {
    if (cps.empty()) return false;
    switch (lowerCp(cps.back())) {
    case 'f': case 's': case 't': case 'k': case 'h': case 'p': case 0xE7: case 0x15F: return true;
    default: return false;
    }
}

// "Kel Mahmut'ta", "Hacı Rıza'da", "Emekli Nuri'de"
inline std::string locative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    return name + "'" + (endsHard(cps) ? "t" : "d") + (backHarmony(cps) ? "a" : "e");
}

// "Hacı Rıza'ya", "Kel Mahmut'a", "Emekli Nuri'ye"
inline std::string dative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    const bool vowelEnd = !cps.empty() && vowelClass(cps.back()) != 0;
    return name + "'" + (vowelEnd ? "y" : "") + (backHarmony(cps) ? "a" : "e");
}

// "Kel Mahmut'un", "Hacı Rıza'nın", "Emekli Nuri'nin", "Ömür'ün"
inline std::string genitive(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    const bool vowelEnd = !cps.empty() && vowelClass(cps.back()) != 0;
    const char v = lastVowel(cps);
    const char* suf = v == 'o' ? "un" : v == 'e' ? "in" : v == 'u' ? "\xC3\xBCn" : "\xC4\xB1n";
    return name + "'" + (vowelEnd ? "n" : "") + suf;
}

} // namespace trtext
