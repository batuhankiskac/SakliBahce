#pragma once
// 3D WORLD CONTRACT (architect-owned, header-only, FROZEN). Every 3D module places things with these.
//
// Units: metres. Y up. Floor at y = 0. The okey table's centre is at the origin (x = z = 0).
// raylib is right-handed: the human (seat 0) sits at +Z and looks toward -Z.
// Seats: 0 human (+Z, "south"), 1 right (+X), 2 across (-Z), 3 left (-X). Play order 0 -> 1 -> 2 -> 3.
#include <raylib.h>

namespace w3d {

// ---------------------------------------------------------------- the okey table
constexpr float TABLE_Y = 0.76f;    // top of the green felt (çuha)
constexpr float FELT_HALF = 0.55f;  // felt covers x, z in [-0.55, 0.55]
constexpr float RIM_W = 0.07f;      // wooden rim around the felt (table top outer half-size = 0.62)
constexpr float RIM_H = 0.018f;     // rim top height above the felt

// Tile size (slightly larger than a real okey tile for readability). Width x height x thickness.
constexpr float TILE_W = 0.030f;
constexpr float TILE_H = 0.042f;
constexpr float TILE_T = 0.011f;

// Seat frames. SEAT_DIR = from the table centre toward the seat. SEAT_RIGHT = that player's right hand.
// A seated player faces -SEAT_DIR.
constexpr Vector3 SEAT_DIR[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
constexpr Vector3 SEAT_RIGHT[4] = {{1, 0, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 0, 1}};
constexpr float SEAT_DIST = 0.88f;  // chair centre distance from the table centre
constexpr float HEAD_Y = 1.20f;     // seated eye height
constexpr float CHAIR_SEAT_Y = 0.46f;
inline Vector3 seatPos(int s) { return {SEAT_DIR[s].x * SEAT_DIST, 0.f, SEAT_DIR[s].z * SEAT_DIST}; }
// Yaw (degrees about +Y, raylib MatrixRotateY convention) that turns the local forward axis (0,0,-1) into
// the seat's facing (-SEAT_DIR): 0 -> 0, 1 -> 90 (faces -X), 2 -> 180 (faces +Z), 3 -> 270 (faces +X).
inline float seatYawDeg(int s) { return (float)s * 90.f; }
// Point in a seat's local frame: `right` along SEAT_RIGHT, `out` along SEAT_DIR (toward that player), y absolute.
inline Vector3 seatLocal(int s, float right, float out, float y) {
    return {SEAT_RIGHT[s].x * right + SEAT_DIR[s].x * out, y, SEAT_RIGHT[s].z * right + SEAT_DIR[s].z * out};
}

// Racks (istaka): centred RACK_DIST from the table centre along SEAT_DIR, long axis along SEAT_RIGHT.
constexpr float RACK_DIST = 0.48f;
constexpr float RACK_LEN = 0.56f;
constexpr float RACK_DEPTH = 0.085f;
constexpr int RACK_SLOTS = 16;      // per row, 2 rows (back row higher)

// Centre of the table
constexpr Vector3 PILE_POS{-0.055f, TABLE_Y, 0.0f};      // face-down draw pile
constexpr Vector3 INDICATOR_POS{0.060f, TABLE_Y, 0.0f};  // gösterge, face up
// Discard piles at the table corners: seat s discards at its right-front corner. The human takes from
// DISCARD_POS[3] (front-left) and discards to DISCARD_POS[0] (front-right).
constexpr Vector3 DISCARD_POS[4] = {
    {0.36f, TABLE_Y, 0.36f}, {0.36f, TABLE_Y, -0.36f}, {-0.36f, TABLE_Y, -0.36f}, {-0.36f, TABLE_Y, 0.36f}};
// Opened melds lie flat in the owner's zone (world XZ rectangle: x0, z0, x1, z1). Tiles in melds and
// discard piles are laid readable from the human seat (numbers upright toward +Z).
struct RectXZ { float x0, z0, x1, z1; };
constexpr RectXZ MELD_ZONE[4] = {
    {-0.26f, 0.14f, 0.26f, 0.40f}, {0.14f, -0.26f, 0.40f, 0.26f},
    {-0.26f, -0.40f, 0.26f, -0.14f}, {-0.40f, -0.26f, -0.14f, 0.26f}};

// Personal props on the table (owned by Characters; the human's glass too) and the shared ashtray (Room).
// Keep-out: nothing else may be placed within the radius.
constexpr Vector3 GLASS_POS[4] = {           // 0 human tea, 1 Rıza tea, 2 Mahmut tea, 3 Nuri ORALET
    {-0.42f, TABLE_Y, 0.48f}, {0.48f, TABLE_Y, 0.42f}, {0.42f, TABLE_Y, -0.48f}, {-0.48f, TABLE_Y, -0.42f}};
constexpr float GLASS_KEEPOUT = 0.05f;
constexpr Vector3 ASHTRAY_POS{-0.40f, TABLE_Y, -0.49f};
constexpr float ASHTRAY_KEEPOUT = 0.055f;

// ---------------------------------------------------------------- the human's camera
constexpr Vector3 EYE{0.f, 1.20f, 0.80f};
constexpr float EYE_PITCH_DEG = -27.f;      // default look: down toward the table
constexpr float FOVY_DEG = 66.f;            // default vertical field of view

// ---------------------------------------------------------------- the room (kıraathane)
constexpr float ROOM_X0 = -4.2f, ROOM_X1 = 4.2f;   // side walls
constexpr float ROOM_Z0 = -3.4f, ROOM_Z1 = 3.6f;   // back wall (behind seat 2) / wall behind the human
constexpr float CEILING_Y = 3.1f;
constexpr Vector3 TABLE_LAMP{0.f, 1.86f, 0.f};     // pendant lamp over our table (key light, casts shadows)
// Scores are chalked on this wall board (Room renders it from setScoreboard): centre and size, on the back wall.
constexpr Vector3 SCOREBOARD_POS{-1.25f, 1.32f, ROOM_Z0 + 0.02f};
constexpr float SCOREBOARD_W = 1.0f, SCOREBOARD_H = 0.68f;

// Background tables (0.9 m square, top at 0.75 m) and which sides have a patron (bit s = side SEAT_DIR[s]
// relative to that table; chairs at 0.75 m from its centre). Room draws the tables and all 4 chairs;
// Characters seats the patrons. `kind`: 0 tavla, 1 okey, 2 cards (iskambil), 3 just tea & chat.
struct BgTable { float x, z; unsigned sides; int kind; };
constexpr BgTable BG_TABLES[4] = {
    {-2.5f, -1.8f, 0b0101u, 0},  // tavla, two players
    {2.4f, -1.9f, 0b1111u, 1},   // okey, four players
    {-2.6f, 1.7f, 0b0111u, 2},   // cards, three players
    {2.5f, 1.8f, 0b1010u, 3},    // two old men drinking tea
};
constexpr float BG_TABLE_Y = 0.75f;
constexpr float BG_CHAIR_DIST = 0.75f;
// Tea counter / stove (ocak) where the tea boy (çaycı) works; he walks from here to the tables.
constexpr Vector3 COUNTER_POS{3.0f, 0.f, -2.95f};  // front-centre of the counter, on the floor

} // namespace w3d
