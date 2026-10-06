#pragma once
// Okey tile rendering (table owner). Tile faces are rendered once, supersampled, into a mip-mapped atlas
// (ivory body with thickness and bevel, engraved number + dot in the tile ink, sahte okey clover, backs
// with a star emblem), so tiles stay crisp and anti-aliased at any window scale and any rotation.
// Everything here is drawn in virtual units; call init() after InitWindow().
#include "core/Tile.h"
#include <raylib.h>

namespace ui {
namespace tilegfx {

// Reference size of a rack tile; every tile keeps this aspect. The bottom FACE_H..H band is the tile's
// visible thickness (front side), the face is 0..FACE_H.
constexpr float W = 48.f;
constexpr float H = 68.f;
constexpr float FACE_H = 62.f;
inline float heightFor(float w) { return w * (H / W); }

constexpr int KEY_FAKE = 52;  // sahte okey face
constexpr int KEY_BACK = 53;  // back
// Face key of a physical tile: 0..51 = color*13 + number-1 (both copies share a face), 52 fake, 53 back.
int faceKeyOf(int tileId);
inline int faceKey(int color, int number) { return color * 13 + (number - 1); }

struct Fx {
    float alpha = 1.f;
    float flip = 1.f;          // 1 = face up, 0 = back; in between the tile is squashed (mid-flip)
    float lift = 0.f;          // 0 = resting; >0 = in the air (shadow grows, drifts and softens)
    bool shadow = true;
    float shadowAlpha = 1.f;
    bool okeyBadge = false;    // small gold star (wild okey)
    float glow = 0.f;          // soft halo behind the tile
    Color glowColor = {255, 214, 110, 255};
    float outline = 0.f;       // crisp outline around the face
    Color outlineColor = {255, 214, 110, 255};
    float dim = 0.f;           // 0..1 darken
    Color tint = {255, 255, 255, 255};
};

void init();
void shutdown();
bool ready();
// Colour-blind mode (Ayarlar "Renk körü modu"): inks told apart by lightness (deuteranopia / protanopia) and a shape per
// colour in place of the dot (sarı ●, mavi ■, siyah ▲, kırmızı ◆). Also sets ui::setColorBlind. Safe to call any
// time: the atlases are repainted in place (same textures) by the next refresh() — Table3D::update calls it — or init().
void setColorBlind(bool on);
bool colorBlind();
void refresh(); // outside BeginDrawing/BeginTextureMode: repaints the atlases if the mode changed

// Draws a tile centred at `c`, `w` wide (height = heightFor(w)), rotated `rotDeg` degrees clockwise.
// `faceKey` is shown while fx.flip >= 0.5, the back otherwise.
void drawKey(int faceKey, Vector2 c, float w, float rotDeg, const Fx& fx);
void drawTile(int tileId, const okey::OkeyInfo& ok, Vector2 c, float w, float rotDeg, Fx fx);
void drawBack(Vector2 c, float w, float rotDeg, const Fx& fx);
// Soft rounded shadow/halo of a tile-sized box (also used for glows and drop targets).
void drawSoftBox(Vector2 c, float w, float h, float rotDeg, Color col);
// The okey star badge alone (radius in virtual units).
void drawStar(Vector2 c, float radius, float rotDeg, float alpha);
// Filled rounded rectangle with a vertical colour gradient (used by racks and plates as well).
void roundedGradV(Rectangle r, float radius, Color top, Color bottom, int segPerCorner = 6);
void roundedRect(Rectangle r, float radius, Color c, int segPerCorner = 6);
void roundedLines(Rectangle r, float radius, float thick, Color c, int segPerCorner = 6);

// Debug/harness: the atlas texture (valid after init()).
Texture2D atlasTexture();

// ------------------------------------------------------------------ 3D tile faces (r3d::Table3D)
// A second, fully opaque atlas whose cells have the exact proportions of a 3D tile face (5 : 7, like
// w3d::TILE_W : TILE_H). Each cell is padded with its own border colour, so mip-mapping never bleeds
// between cells. Keys: 0..51 numbers, KEY_FAKE (clover), KEY_BACK (back emblem), KEY_BODY (plain ivory for
// the sides). Built by init(); mip-mapped, anisotropic, clamped.
constexpr int KEY_BODY = 54;
Texture2D faceAtlas3D();
// Normalised UV rectangle (x, y, width, height in 0..1, y down) of a cell's face area in faceAtlas3D().
Rectangle faceUV3D(int key);
// The okey star badge alone on a transparent background (square, mip-mapped) — for 3D decals.
Texture2D starTexture3D();

} // namespace tilegfx
} // namespace ui
