// Characters module: init-time geometry helpers — SDF modelling with smooth unions, a surface-nets mesher
// with gradient normals, mesh mirroring, and small math/noise utilities shared by the characters files.
#include "r3d/CharactersInternal.h"

#include <rlgl.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <thread>

namespace r3d {
namespace chr {

// ============================================================================ frames
Matrix boneMatrix(Vector3 a, Vector3 b, Vector3 hint) {
    Vector3 Y = Vector3Subtract(b, a);
    float len = Vector3Length(Y);
    Y = len > 1e-6f ? Vector3Scale(Y, 1.f / len) : Vector3{0, 1, 0};
    Vector3 Z = Vector3Subtract(hint, Vector3Scale(Y, Vector3DotProduct(hint, Y)));
    if (Vector3Length(Z) < 1e-4f) {
        Vector3 alt = std::fabs(Y.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Z = Vector3Subtract(alt, Vector3Scale(Y, Vector3DotProduct(alt, Y)));
    }
    Z = Vector3Normalize(Z);
    Vector3 X = Vector3CrossProduct(Y, Z);
    return basisMatrix(X, Y, Z, a);
}

void handBasis(Vector3 fingers, Vector3 palm, Vector3& X, Vector3& Y, Vector3& Z) {
    Z = vnorm(Vector3Negate(fingers));
    Y = Vector3Negate(palm);
    Y = Vector3Subtract(Y, Vector3Scale(Z, Vector3DotProduct(Y, Z)));
    if (Vector3Length(Y) < 1e-4f) {
        const Vector3 alt = std::fabs(Z.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Y = Vector3Subtract(alt, Vector3Scale(Z, Vector3DotProduct(alt, Z)));
    }
    Y = vnorm(Y);
    X = Vector3CrossProduct(Y, Z);
}

Matrix handMatrix(Vector3 wrist, Vector3 fingers, Vector3 palm, float scale) {
    Vector3 X, Y, Z;
    handBasis(fingers, palm, X, Y, Z);
    return basisMatrix(Vector3Scale(X, scale), Vector3Scale(Y, scale), Vector3Scale(Z, scale), wrist);
}

// ============================================================================ noise
float hashf(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0xffffff) / 16777215.f;
}

float noise1(float x, uint32_t seed) {
    float fl = std::floor(x);
    int i = (int)fl;
    float f = x - fl;
    float a = hashf((uint32_t)i * 0x9E3779B1u + seed * 0x85EBCA77u);
    float b = hashf((uint32_t)(i + 1) * 0x9E3779B1u + seed * 0x85EBCA77u);
    float u = f * f * (3.f - 2.f * f);
    return a + (b - a) * u;
}

float noise3(Vector3 p, uint32_t seed) {
    float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
    auto h = [&](int x, int y, int z) {
        return hashf((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ seed * 2654435761u);
    };
    float ux = tx * tx * (3 - 2 * tx), uy = ty * ty * (3 - 2 * ty), uz = tz * tz * (3 - 2 * tz);
    float c000 = h(ix, iy, iz), c100 = h(ix + 1, iy, iz), c010 = h(ix, iy + 1, iz), c110 = h(ix + 1, iy + 1, iz);
    float c001 = h(ix, iy, iz + 1), c101 = h(ix + 1, iy, iz + 1), c011 = h(ix, iy + 1, iz + 1),
          c111 = h(ix + 1, iy + 1, iz + 1);
    float x00 = lerpf(c000, c100, ux), x10 = lerpf(c010, c110, ux), x01 = lerpf(c001, c101, ux),
          x11 = lerpf(c011, c111, ux);
    return lerpf(lerpf(x00, x10, uy), lerpf(x01, x11, uy), uz);
}

// ============================================================================ SDF primitives
namespace {

inline float sgn(float v) { return (float)((v > 0.f) - (v < 0.f)); }

float sdRoundCone(Vector3 p, Vector3 a, Vector3 b, float r1, float r2) {
    Vector3 ba = Vector3Subtract(b, a);
    float l2 = Vector3DotProduct(ba, ba);
    if (l2 < 1e-12f) return Vector3Distance(p, a) - std::max(r1, r2);
    float rr = r1 - r2;
    float a2 = l2 - rr * rr;
    float il2 = 1.f / l2;
    Vector3 pa = Vector3Subtract(p, a);
    float y = Vector3DotProduct(pa, ba);
    float z = y - l2;
    Vector3 xv = Vector3Subtract(Vector3Scale(pa, l2), Vector3Scale(ba, y));
    float x2 = Vector3DotProduct(xv, xv);
    float y2 = y * y * l2;
    float z2 = z * z * l2;
    float k = sgn(rr) * rr * rr * x2;
    if (sgn(z) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if (sgn(y) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(std::max(x2 * a2 * il2, 0.f)) + y * rr) * il2 - r1;
}

float sdEllipsoid(Vector3 p, Vector3 r) {
    Vector3 q{p.x / r.x, p.y / r.y, p.z / r.z};
    float k0 = Vector3Length(q);
    Vector3 q2{p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)};
    float k1 = Vector3Length(q2);
    if (k1 < 1e-9f) return -std::min(r.x, std::min(r.y, r.z));
    return k0 * (k0 - 1.f) / k1;
}

float sdRoundBox(Vector3 p, Vector3 half, float round) {
    Vector3 q{std::fabs(p.x) - half.x + round, std::fabs(p.y) - half.y + round, std::fabs(p.z) - half.z + round};
    Vector3 mq{std::max(q.x, 0.f), std::max(q.y, 0.f), std::max(q.z, 0.f)};
    return Vector3Length(mq) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.f) - round;
}

float sdTorus(Vector3 p, float R, float r) {
    float qx = std::sqrt(p.x * p.x + p.z * p.z) - R;
    return std::sqrt(qx * qx + p.y * p.y) - r;
}

inline float smin(float a, float b, float k) {
    if (k <= 0.f) return std::min(a, b);
    float h = std::max(k - std::fabs(a - b), 0.f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}
inline float smax(float a, float b, float k) { return -smin(-a, -b, k); }

Matrix rotMatrix(Vector3 rotDeg) {
    Matrix m = MatrixRotateX(rotDeg.x * DEG2RAD);
    m = MatrixMultiply(m, MatrixRotateY(rotDeg.y * DEG2RAD));
    return MatrixMultiply(m, MatrixRotateZ(rotDeg.z * DEG2RAD));
}

} // namespace

void Sdf::add(Prim p) {
    p.layer = layer_;
    prims_.push_back(p);
}

void Sdf::ellipsoid(Vector3 c, Vector3 r, Color col, float k, Vector3 rotDeg) {
    Prim p;
    p.type = 0;
    p.c = c;
    p.r = r;
    p.k = k;
    p.col = col;
    p.rotated = rotDeg.x != 0 || rotDeg.y != 0 || rotDeg.z != 0;
    if (p.rotated) p.inv = MatrixTranspose(rotMatrix(rotDeg));
    p.bc = c;
    p.br = std::max(r.x, std::max(r.y, r.z));
    add(p);
}
void Sdf::cone(Vector3 a, Vector3 b, float ra, float rb, Color col, float k) {
    Prim p;
    p.type = 1;
    p.a = a;
    p.b = b;
    p.ra = ra;
    p.rb = rb;
    p.k = k;
    p.col = col;
    p.bc = Vector3Scale(Vector3Add(a, b), 0.5f);
    p.br = Vector3Distance(a, b) * 0.5f + std::max(ra, rb);
    add(p);
}
void Sdf::box(Vector3 c, Vector3 half, float round, Color col, float k, Vector3 rotDeg) {
    Prim p;
    p.type = 2;
    p.c = c;
    p.r = half;
    p.ra = round;
    p.k = k;
    p.col = col;
    p.rotated = rotDeg.x != 0 || rotDeg.y != 0 || rotDeg.z != 0;
    if (p.rotated) p.inv = MatrixTranspose(rotMatrix(rotDeg));
    p.bc = c;
    p.br = Vector3Length(half);
    add(p);
}
void Sdf::torus(Vector3 c, float R, float r, Color col, float k, Vector3 rotDeg) {
    Prim p;
    p.type = 3;
    p.c = c;
    p.ra = R;
    p.rb = r;
    p.k = k;
    p.col = col;
    p.rotated = rotDeg.x != 0 || rotDeg.y != 0 || rotDeg.z != 0;
    if (p.rotated) p.inv = MatrixTranspose(rotMatrix(rotDeg));
    p.bc = c;
    p.br = R + r;
    add(p);
}
void Sdf::cutEllipsoid(Vector3 c, Vector3 r, Color col, float k, Vector3 rotDeg) {
    ellipsoid(c, r, col, k, rotDeg);
    prims_.back().sub = true;
}
void Sdf::cutBox(Vector3 c, Vector3 half, float round, Color col, float k, Vector3 rotDeg) {
    box(c, half, round, col, k, rotDeg);
    prims_.back().sub = true;
}

float Sdf::primDist(const Prim& p, Vector3 x) const {
    switch (p.type) {
    case 0: {
        Vector3 q = Vector3Subtract(x, p.c);
        if (p.rotated) q = xfDir(p.inv, q);
        return sdEllipsoid(q, p.r);
    }
    case 1: return sdRoundCone(x, p.a, p.b, p.ra, p.rb);
    case 2: {
        Vector3 q = Vector3Subtract(x, p.c);
        if (p.rotated) q = xfDir(p.inv, q);
        return sdRoundBox(q, p.r, p.ra);
    }
    default: {
        Vector3 q = Vector3Subtract(x, p.c);
        if (p.rotated) q = xfDir(p.inv, q);
        return sdTorus(q, p.ra, p.rb);
    }
    }
}

float Sdf::evalLayer(Vector3 x, int layer) const {
    float d = 1e9f;
    for (const Prim& p : prims_) {
        if (p.layer != layer) continue;
        float lb = Vector3Distance(x, p.bc) - p.br;
        if (!p.sub) {
            if (lb >= d + p.k) continue;
            d = smin(d, primDist(p, x), p.k);
        } else {
            if (lb >= p.k - d) continue;
            d = smax(d, -primDist(p, x), p.k);
        }
    }
    return d;
}

float Sdf::eval(Vector3 x) const {
    float d = 1e9f;
    for (int l = 0; l <= layer_; ++l) d = std::min(d, evalLayer(x, l));
    return d;
}

Color Sdf::color(Vector3 x) const {
    const float sigma = 0.0014f;   // blend between primitives of one layer
    const float sigmaL = 0.0016f;  // blend between layers (softens hairlines / cap edges)
    thread_local std::vector<float> ds, lw;
    ds.resize(prims_.size());
    lw.assign((size_t)layer_ + 1, 1.f);
    // each layer's weight: how close its surface is compared with the nearest layer's
    if (layer_ > 0) {
        float best = 1e9f;
        for (int l = 0; l <= layer_; ++l) {
            lw[(size_t)l] = std::fabs(evalLayer(x, l));
            best = std::min(best, lw[(size_t)l]);
        }
        for (int l = 0; l <= layer_; ++l) lw[(size_t)l] = std::exp(-(lw[(size_t)l] - best) / sigmaL);
    }
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f, wsum = 0.f;
    for (int l = 0; l <= layer_; ++l) {
        if (lw[(size_t)l] < 1e-3f) continue;
        float dmin = 1e9f;
        for (size_t i = 0; i < prims_.size(); ++i) {
            if (prims_[i].layer != l) continue;
            ds[i] = std::fabs(primDist(prims_[i], x));
            // later primitives win ties (a mustache on top of the face)
            ds[i] -= (float)i * 1e-6f;
            dmin = std::min(dmin, ds[i]);
        }
        float lr = 0.f, lg = 0.f, lb = 0.f, la = 0.f, lsum = 0.f;
        for (size_t i = 0; i < prims_.size(); ++i) {
            if (prims_[i].layer != l) continue;
            float w = std::exp(-(ds[i] - dmin) / sigma);
            if (w < 1e-3f) continue;
            const Color& c = prims_[i].col;
            lr += w * c.r;
            lg += w * c.g;
            lb += w * c.b;
            la += w * c.a;
            lsum += w;
        }
        if (lsum <= 0.f) continue;
        const float k = lw[(size_t)l] / lsum;
        r += lr * k;
        g += lg * k;
        b += lb * k;
        a += la * k;
        wsum += lw[(size_t)l];
    }
    if (wsum <= 0.f) return WHITE;
    return Color{(unsigned char)clampf(r / wsum, 0, 255), (unsigned char)clampf(g / wsum, 0, 255),
                 (unsigned char)clampf(b / wsum, 0, 255), (unsigned char)clampf(a / wsum, 0, 255)};
}

void Sdf::bounds(Vector3& lo, Vector3& hi) const {
    lo = {1e9f, 1e9f, 1e9f};
    hi = {-1e9f, -1e9f, -1e9f};
    for (const Prim& p : prims_) {
        if (p.sub) continue;
        lo = Vector3Min(lo, Vector3SubtractValue(p.bc, p.br));
        hi = Vector3Max(hi, Vector3AddValue(p.bc, p.br));
    }
}

// ============================================================================ surface nets
namespace {
thread_local bool t_inWorker = false;  // nested parallelFor calls run serially inside a worker
} // namespace

void parallelFor(int n, const std::function<void(int)>& fn) {
    const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
    const int nt = t_inWorker ? 1 : std::min(n, std::min(hw, 12));
    if (nt <= 1) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve((size_t)nt);
    for (int t = 0; t < nt; ++t)
        pool.emplace_back([&]() {
            t_inWorker = true;
            for (int i = next++; i < n; i = next++) fn(i);
        });
    for (std::thread& th : pool) th.join();
}

void MeshJobs::add(std::function<RawMesh()> make, int target, Mesh* out, Mesh* mirror, int targetLo, Mesh* outLo,
                   Mesh* mirrorLo) {
    Job j;
    j.make = std::move(make);
    j.target = target;
    j.out = out;
    j.mirror = mirror;
    j.targetLo = targetLo;
    j.outLo = outLo;
    j.mirrorLo = mirrorLo;
    jobs.push_back(std::move(j));
}

void MeshJobs::run() {
    // biggest first keeps the cores busy until the end
    std::vector<int> order((size_t)jobs.size());
    for (int i = 0; i < (int)jobs.size(); ++i) order[(size_t)i] = i;
    auto cost = [&](int i) {
        const Job& j = jobs[(size_t)i];
        return j.cost > 0.f ? j.cost : (float)j.target;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cost(a) > cost(b); });
    parallelFor((int)jobs.size(), [&](int oi) {
        const int i = order[(size_t)oi];
        Job& j = jobs[(size_t)i];
        j.raw = j.make();
        if (j.target > 0) simplifyMesh(j.raw, j.target, 10.f, j.colorEdge);
        if (j.outLo) {
            j.rawLo = j.raw;
            simplifyMesh(j.rawLo, j.targetLo);
        }
    });
    // GPU uploads on the calling (GL) thread
    for (Job& j : jobs) {
        *j.out = uploadRaw(j.raw);
        if (j.mirror) *j.mirror = mirrorMeshX(*j.out);
        if (j.outLo) {
            *j.outLo = uploadRaw(j.rawLo);
            if (j.mirrorLo) *j.mirrorLo = mirrorMeshX(*j.outLo);
        }
    }
    jobs.clear();
}

Mesh buildSdf(const Sdf& s, float cell, int targetTris) {
    RawMesh m = meshSdf(s, cell);
    if (targetTris > 0) simplifyMesh(m, targetTris);
    return uploadRaw(m);
}

RawMesh meshSdf(const Sdf& s, float h) {
    Vector3 lo, hi;
    s.bounds(lo, hi);
    lo = Vector3SubtractValue(lo, 2.f * h);
    hi = Vector3AddValue(hi, 2.f * h);
    const int B = 4;
    int bx = std::max(1, (int)std::ceil((hi.x - lo.x) / (h * B)));
    int by = std::max(1, (int)std::ceil((hi.y - lo.y) / (h * B)));
    int bz = std::max(1, (int)std::ceil((hi.z - lo.z) / (h * B)));
    const int nx = bx * B + 1, ny = by * B + 1, nz = bz * B + 1;
    auto P = [&](int i, int j, int k) { return Vector3{lo.x + i * h, lo.y + j * h, lo.z + k * h}; };
    auto I = [&](int i, int j, int k) { return (size_t)i + (size_t)nx * ((size_t)j + (size_t)ny * (size_t)k); };

    // coarse pass: find blocks that may contain the surface
    const int cx = bx + 1, cy = by + 1, cz = bz + 1;
    std::vector<float> cval((size_t)cx * cy * cz);
    parallelFor(cz, [&](int k) {
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i) cval[(size_t)i + (size_t)cx * (j + (size_t)cy * k)] = s.eval(P(i * B, j * B, k * B));
    });
    auto C = [&](int i, int j, int k) { return cval[(size_t)i + (size_t)cx * (j + (size_t)cy * k)]; };

    const float nanv = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> val((size_t)nx * ny * nz, nanv);
    const float thr = (float)B * h * 1.7320508f * 1.5f;
    // fine pass over the interesting blocks; even and odd block slabs separately (they share faces)
    for (int parity = 0; parity < 2; ++parity)
        parallelFor((bz + 1 - parity) / 2, [&](int idx) {
            const int bk = idx * 2 + parity;
            for (int bj = 0; bj < by; ++bj)
                for (int bi = 0; bi < bx; ++bi) {
                    float mn = 1e9f;
                    bool pos = false, neg = false;
                    for (int c = 0; c < 8; ++c) {
                        float v = C(bi + (c & 1), bj + ((c >> 1) & 1), bk + ((c >> 2) & 1));
                        mn = std::min(mn, std::fabs(v));
                        (v < 0 ? neg : pos) = true;
                    }
                    if (mn > thr && !(pos && neg)) continue;
                    for (int k = bk * B; k <= bk * B + B; ++k)
                        for (int j = bj * B; j <= bj * B + B; ++j)
                            for (int i = bi * B; i <= bi * B + B; ++i) {
                                float& v = val[I(i, j, k)];
                                if (std::isnan(v)) v = s.eval(P(i, j, k));
                            }
                }
        });
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                float& v = val[I(i, j, k)];
                if (std::isnan(v)) {
                    float c = C(std::min(i / B, cx - 1), std::min(j / B, cy - 1), std::min(k / B, cz - 1));
                    v = c < 0 ? -std::max(-c, h) : std::max(c, h);
                }
            }

    auto grad = [&](Vector3 p, float e) {
        return Vector3{s.eval({p.x + e, p.y, p.z}) - s.eval({p.x - e, p.y, p.z}),
                       s.eval({p.x, p.y + e, p.z}) - s.eval({p.x, p.y - e, p.z}),
                       s.eval({p.x, p.y, p.z + e}) - s.eval({p.x, p.y, p.z - e})};
    };

    const int mx = nx - 1, my = ny - 1, mz = nz - 1;
    auto CI = [&](int i, int j, int k) { return (size_t)i + (size_t)mx * ((size_t)j + (size_t)my * (size_t)k); };
    // one vertex per surface cell, computed in parallel (position projected onto the surface, gradient
    // normal, colour), then numbered in order
    struct CellVert {
        Vector3 p{}, n{};
        Color c{};
        bool on = false;
    };
    std::vector<CellVert> cells((size_t)mx * my * mz);
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    parallelFor(mz, [&](int k) {
        for (int j = 0; j < my; ++j)
            for (int i = 0; i < mx; ++i) {
                float v[8];
                int mask = 0;
                for (int c = 0; c < 8; ++c) {
                    v[c] = val[I(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
                    if (v[c] < 0) mask |= 1 << c;
                }
                if (mask == 0 || mask == 255) continue;
                Vector3 sum{0, 0, 0};
                int cnt = 0;
                for (const auto& e : edges) {
                    float a = v[e[0]], b = v[e[1]];
                    if ((a < 0) == (b < 0)) continue;
                    float t = a / (a - b);
                    Vector3 pa = P(i + (e[0] & 1), j + ((e[0] >> 1) & 1), k + ((e[0] >> 2) & 1));
                    Vector3 pb = P(i + (e[1] & 1), j + ((e[1] >> 1) & 1), k + ((e[1] >> 2) & 1));
                    sum = Vector3Add(sum, Vector3Lerp(pa, pb, t));
                    ++cnt;
                }
                Vector3 p = Vector3Scale(sum, 1.f / (float)cnt);
                Vector3 p0 = p;
                for (int it = 0; it < 2; ++it) { // project onto the surface
                    float f = s.eval(p);
                    Vector3 g = Vector3Scale(grad(p, h * 0.25f), 1.f / (h * 0.5f));
                    float g2 = Vector3DotProduct(g, g);
                    if (g2 < 1e-8f) break;
                    Vector3 step = Vector3Scale(g, f / g2);
                    float sl = Vector3Length(step);
                    if (sl > h * 0.5f) step = Vector3Scale(step, h * 0.5f / sl);
                    p = Vector3Subtract(p, step);
                }
                if (Vector3Distance(p, p0) > h) p = p0;
                CellVert& cv = cells[CI(i, j, k)];
                cv.p = p;
                cv.n = vnorm(grad(p, h * 0.5f));
                cv.c = s.color(p);
                if (s.paint) cv.c = s.paint(p, cv.n, cv.c);
                cv.on = true;
            }
    });
    std::vector<int> cellVert((size_t)mx * my * mz, -1);
    RawMesh out;
    for (size_t ci = 0; ci < cells.size(); ++ci) {
        const CellVert& cv = cells[ci];
        if (!cv.on) continue;
        cellVert[ci] = (int)out.p.size();
        out.p.push_back(cv.p);
        out.n.push_back(cv.n);
        out.c.push_back(cv.c);
    }
    auto quad = [&](int a, int b, int c, int d) {
        // split along the shorter diagonal
        if (Vector3DistanceSqr(out.p[(size_t)a], out.p[(size_t)c]) <= Vector3DistanceSqr(out.p[(size_t)b], out.p[(size_t)d])) {
            out.idx.insert(out.idx.end(), {(unsigned)a, (unsigned)b, (unsigned)c, (unsigned)a, (unsigned)c, (unsigned)d});
        } else {
            out.idx.insert(out.idx.end(), {(unsigned)a, (unsigned)b, (unsigned)d, (unsigned)b, (unsigned)c, (unsigned)d});
        }
    };
    auto cv = [&](int i, int j, int k) { return cellVert[CI(i, j, k)]; };
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                float v0 = val[I(i, j, k)];
                bool in0 = v0 < 0;
                // x edge
                if (i + 1 < nx && j >= 1 && k >= 1 && j < my && k < mz && (val[I(i + 1, j, k)] < 0) != in0) {
                    int A = cv(i, j - 1, k - 1), Bq = cv(i, j, k - 1), Cq = cv(i, j, k), D = cv(i, j - 1, k);
                    if (A >= 0 && Bq >= 0 && Cq >= 0 && D >= 0) {
                        if (in0) quad(A, Bq, Cq, D);
                        else quad(A, D, Cq, Bq);
                    }
                }
                // y edge
                if (j + 1 < ny && i >= 1 && k >= 1 && i < mx && k < mz && (val[I(i, j + 1, k)] < 0) != in0) {
                    int A = cv(i - 1, j, k - 1), Bq = cv(i - 1, j, k), Cq = cv(i, j, k), D = cv(i, j, k - 1);
                    if (A >= 0 && Bq >= 0 && Cq >= 0 && D >= 0) {
                        if (in0) quad(A, Bq, Cq, D);
                        else quad(A, D, Cq, Bq);
                    }
                }
                // z edge
                if (k + 1 < nz && i >= 1 && j >= 1 && i < mx && j < my && (val[I(i, j, k + 1)] < 0) != in0) {
                    int A = cv(i - 1, j - 1, k), Bq = cv(i, j - 1, k), Cq = cv(i, j, k), D = cv(i - 1, j, k);
                    if (A >= 0 && Bq >= 0 && Cq >= 0 && D >= 0) {
                        if (in0) quad(A, Bq, Cq, D);
                        else quad(A, D, Cq, Bq);
                    }
                }
            }
    return out;
}

// ============================================================================ mirroring
Mesh mirrorMeshX(const Mesh& src) {
    Mesh m{};
    m.vertexCount = src.vertexCount;
    m.triangleCount = src.triangleCount;
    const int vc = src.vertexCount;
    m.vertices = (float*)RL_MALLOC(sizeof(float) * 3 * vc);
    std::memcpy(m.vertices, src.vertices, sizeof(float) * 3 * vc);
    if (src.normals) {
        m.normals = (float*)RL_MALLOC(sizeof(float) * 3 * vc);
        std::memcpy(m.normals, src.normals, sizeof(float) * 3 * vc);
    }
    if (src.texcoords) {
        m.texcoords = (float*)RL_MALLOC(sizeof(float) * 2 * vc);
        std::memcpy(m.texcoords, src.texcoords, sizeof(float) * 2 * vc);
    }
    if (src.colors) {
        m.colors = (unsigned char*)RL_MALLOC(4 * vc);
        std::memcpy(m.colors, src.colors, 4 * vc);
    }
    for (int i = 0; i < vc; ++i) {
        m.vertices[i * 3] = -m.vertices[i * 3];
        if (m.normals) m.normals[i * 3] = -m.normals[i * 3];
    }
    if (src.indices) {
        m.indices = (unsigned short*)RL_MALLOC(sizeof(unsigned short) * 3 * src.triangleCount);
        for (int t = 0; t < src.triangleCount; ++t) {
            m.indices[t * 3] = src.indices[t * 3];
            m.indices[t * 3 + 1] = src.indices[t * 3 + 2];
            m.indices[t * 3 + 2] = src.indices[t * 3 + 1];
        }
    } else {
        auto swapV = [&](int a, int b) {
            for (int c = 0; c < 3; ++c) std::swap(m.vertices[a * 3 + c], m.vertices[b * 3 + c]);
            if (m.normals)
                for (int c = 0; c < 3; ++c) std::swap(m.normals[a * 3 + c], m.normals[b * 3 + c]);
            if (m.texcoords)
                for (int c = 0; c < 2; ++c) std::swap(m.texcoords[a * 2 + c], m.texcoords[b * 2 + c]);
            if (m.colors)
                for (int c = 0; c < 4; ++c) std::swap(m.colors[a * 4 + c], m.colors[b * 4 + c]);
        };
        for (int t = 0; t < vc / 3; ++t) swapV(t * 3 + 1, t * 3 + 2);
    }
    UploadMesh(&m, false);
    return m;
}

// ============================================================================ animation helpers
Vector3 solveTwoBone(Vector3 s, Vector3& w, float l1, float l2, Vector3 pole) {
    Vector3 d = Vector3Subtract(w, s);
    float dist = Vector3Length(d);
    float maxR = (l1 + l2) * 0.998f, minR = std::fabs(l1 - l2) + 0.02f;
    Vector3 dir = dist > 1e-5f ? Vector3Scale(d, 1.f / dist) : Vector3{0, -1, 0};
    if (dist > maxR) {
        dist = maxR;
        w = Vector3Add(s, Vector3Scale(dir, dist));
    } else if (dist < minR) {
        dist = minR;
        w = Vector3Add(s, Vector3Scale(dir, dist));
    }
    float a = (l1 * l1 - l2 * l2 + dist * dist) / (2.f * dist);
    float hh = std::sqrt(std::max(0.f, l1 * l1 - a * a));
    Vector3 pp = Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir)));
    if (Vector3Length(pp) < 1e-5f) {
        Vector3 alt = std::fabs(dir.y) < 0.9f ? Vector3{0, -1, 0} : Vector3{1, 0, 0};
        pp = Vector3Subtract(alt, Vector3Scale(dir, Vector3DotProduct(alt, dir)));
    }
    pp = Vector3Normalize(pp);
    return Vector3Add(Vector3Add(s, Vector3Scale(dir, a)), Vector3Scale(pp, hh));
}

namespace {
// Velocity of one channel (`get`: position, fingers or palm) at key i, arriving (in) or leaving (out):
// the time-weighted mean of the neighbouring slopes, slowed on turns and capped (no overshoot); zero at
// turning points, at the last key and at keys that ease out (1); into a slam key (2) at speed, out of it
// from rest. The first key leaves with the motion the hand had when the track began.
template <class Get>
Vector3 keyVelocity(const Track& tr, size_t i, bool in, Get get, Vector3 first) {
    const std::vector<Key>& k = tr.keys;
    const Vector3 zero{0, 0, 0};
    if (i + 1 >= k.size()) {
        if (!in || i == 0 || k[i].ease != 2) return zero;
    }
    if (i == 0) return first;
    const Key& b = k[i];
    const Vector3 sIn = Vector3Scale(Vector3Subtract(get(b), get(k[i - 1])), 1.f / std::max(b.t - k[i - 1].t, 1e-3f));
    if (b.ease == 2) return in ? Vector3Scale(sIn, 1.8f) : zero;
    if (b.ease == 1 || i + 1 >= k.size()) return zero;
    const Key& c = k[i + 1];
    const float dIn = std::max(b.t - k[i - 1].t, 1e-3f), dOut = std::max(c.t - b.t, 1e-3f);
    const Vector3 sOut = Vector3Scale(Vector3Subtract(get(c), get(b)), 1.f / dOut);
    const float lIn = Vector3Length(sIn), lOut = Vector3Length(sOut);
    if (lIn < 1e-5f || lOut < 1e-5f) return zero;
    const float cosT = Vector3DotProduct(sIn, sOut) / (lIn * lOut);
    if (cosT <= 0.f) return zero;
    Vector3 v = Vector3Scale(Vector3Add(Vector3Scale(sIn, dOut), Vector3Scale(sOut, dIn)), 1.f / (dIn + dOut));
    if (b.ease != 3) v = Vector3Scale(v, 0.5f + 0.5f * cosT);
    const float cap = 1.5f * std::min(lIn, lOut), lv = Vector3Length(v);
    return lv > cap ? Vector3Scale(v, cap / lv) : v;
}

Vector3 hermite(Vector3 p0, Vector3 m0, Vector3 p1, Vector3 m1, float span, float u) {
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = u3 - 2.f * u2 + u, h01 = -2.f * u3 + 3.f * u2, h11 = u3 - u2;
    return Vector3Add(Vector3Add(Vector3Scale(p0, h00), Vector3Scale(m0, h10 * span)),
                      Vector3Add(Vector3Scale(p1, h01), Vector3Scale(m1, h11 * span)));
}
} // namespace

void evalTrack(const Track& tr, float t, Key& out) {
    const std::vector<Key>& k = tr.keys;
    if (k.empty()) return;
    if (t <= k.front().t || k.size() == 1) {
        out = k.front();
        return;
    }
    if (t >= k.back().t) {
        out = k.back();
        return;
    }
    size_t i = 1;
    while (i < k.size() && k[i].t < t) ++i;
    const Key& a = k[i - 1];
    const Key& b = k[i];
    const float span = std::max(b.t - a.t, 1e-4f);
    const float u = clampf((t - a.t) / span, 0.f, 1.f);
    auto pos = [](const Key& q) { return q.pos; };
    auto fin = [](const Key& q) { return q.fingers; };
    auto pal = [](const Key& q) { return q.palm; };
    out = b;
    out.pos = hermite(a.pos, keyVelocity(tr, i - 1, false, pos, tr.v0), b.pos, keyVelocity(tr, i, true, pos, tr.v0), span, u);
    // the arc over obstacles: a bump with flat ends, so the path stays smooth through the keys
    if (b.lift != 0.f) out.pos.y += b.lift * 16.f * u * u * (1.f - u) * (1.f - u);
    out.fingers = vnorm(hermite(a.fingers, keyVelocity(tr, i - 1, false, fin, tr.f0), b.fingers,
                                keyVelocity(tr, i, true, fin, tr.f0), span, u));
    out.palm = vnorm(hermite(a.palm, keyVelocity(tr, i - 1, false, pal, tr.p0), b.palm,
                             keyVelocity(tr, i, true, pal, tr.p0), span, u));
    const float e = smoother01(u);
    out.pose = u < 0.55f ? a.pose : b.pose;
    out.elbowOut = lerpf(a.elbowOut, b.elbowOut, e);
    out.relax = lerpf(a.relax, b.relax, e);
    out.pole = Vector3Lerp(a.pole, b.pole, e);
    out.headRel = b.headRel;  // callers keep a track in one space
}

} // namespace chr
} // namespace r3d
