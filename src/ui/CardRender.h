#pragma once
// Playing card faces (iskambil) for the card games: rendered once, supersampled, into a mip-mapped atlas — cream
// card stock, corner indices in Turkish letters (V K P A), classic pip layouts, double-headed court figures and a
// bordeaux lattice back. Used by the 3D card table (UVs into atlas()) and by 2D screens (drawCard).
// Call init() after InitWindow().
#include <raylib.h>

namespace ui {
namespace cardgfx {

constexpr float W = 63.f;  // card size in millimetres (poker size); every cell keeps this aspect
constexpr float H = 88.f;
constexpr int KEY_BACK = 52;  // the back
constexpr int KEY_BODY = 53;  // plain card stock (the edges)
constexpr int NUM_KEYS = 54;  // 0..51 = kart::Cards ids (suit * 13 + rank - 2)

void init();
void shutdown();
bool ready();

Texture2D atlas();          // mip-mapped, anisotropic, clamped
Rectangle uv(int key);      // normalised UV rect (x, y, w, h; y down) of a cell's card area

// A card centred at `c`, `w` wide (height = w * H / W), rotated `rotDeg` clockwise (2D, virtual units).
void drawCard(int key, Vector2 c, float w, float rotDeg, Color tint = WHITE);

} // namespace cardgfx
} // namespace ui
