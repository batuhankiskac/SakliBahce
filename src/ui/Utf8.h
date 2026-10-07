#pragma once
// Tiny UTF-8 helpers shared by the UI and the raylib-free modules (Banter): character counts, not bytes.
#include <string>

namespace ui {

// Characters (codepoints) in a UTF-8 string: every byte that does not continue a sequence.
inline int utf8Count(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

} // namespace ui
