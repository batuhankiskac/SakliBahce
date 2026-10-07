#pragma once
// The procedural textures' shared hash and value noise (Gfx, Table3D, Room). Header-only, no raylib. Each texture
// still picks its own octave seeds / lacunarity (fbm); only the copies that gave identical numbers were merged here.
#include <cmath>
#include <cstdint>

namespace r3d {
namespace noise {

// A 32-bit integer mix (Chris Wellons' lowbias32).
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
// 0..1 (24 bits) from a hash input.
inline float hash01(uint32_t x) { return (float)(hash32(x) & 0xffffff) / 16777215.f; }
// 0..1 at an integer lattice point.
inline float lattice01(int x, int y, uint32_t seed) {
    return hash01((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ seed * 83492791u);
}
inline float smooth(float t) { return t * t * (3.f - 2.f * t); }
inline int wrap(int i, int n) { return ((i % n) + n) % n; }

// Smooth value noise on the integer lattice: the four corners xa/xb, ya/yb (already wrapped by the caller if tiling).
inline float valueAt(int xa, int xb, int ya, int yb, float fx, float fy, uint32_t seed) {
    const float a = lattice01(xa, ya, seed), b = lattice01(xb, ya, seed), c = lattice01(xa, yb, seed), d = lattice01(xb, yb, seed);
    const float u = smooth(fx), v = smooth(fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
// Value noise, 0..1.
inline float value(float x, float y, uint32_t seed) {
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    return valueAt(x0, x0 + 1, y0, y0 + 1, x - (float)x0, y - (float)y0, seed);
}
// Value noise tiling with periods px, py (lattice units), 0..1.
inline float valueTiled(float x, float y, int px, int py, uint32_t seed) {
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    return valueAt(wrap(x0, px), wrap(x0 + 1, px), wrap(y0, py), wrap(y0 + 1, py), x - (float)x0, y - (float)y0, seed);
}

}  // namespace noise
}  // namespace r3d
