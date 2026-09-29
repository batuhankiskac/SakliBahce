// Characters module: init-time mesh simplification. The surface-nets meshes are dense and uniform; a
// quadric-error (Garland–Heckbert) decimator with half-edge collapses reduces them to a triangle budget
// while keeping silhouettes, small features and colour boundaries (mustache edges, stripes, lips).
// Collapses only move a vertex onto an existing surface vertex, so positions stay on the SDF surface and
// the kept vertices keep their exact gradient normals and painted colours.
#include "r3d/CharactersInternal.h"

#include <algorithm>
#include <queue>

namespace r3d {
namespace chr {

namespace {

struct Quadric {
    double a[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // xx xy xz xw yy yz yw zz zw ww
    void addPlane(double nx, double ny, double nz, double d, double w) {
        a[0] += w * nx * nx;
        a[1] += w * nx * ny;
        a[2] += w * nx * nz;
        a[3] += w * nx * d;
        a[4] += w * ny * ny;
        a[5] += w * ny * nz;
        a[6] += w * ny * d;
        a[7] += w * nz * nz;
        a[8] += w * nz * d;
        a[9] += w * d * d;
    }
    void add(const Quadric& q) {
        for (int i = 0; i < 10; ++i) a[i] += q.a[i];
    }
    double eval(const Vector3& p) const {
        const double x = p.x, y = p.y, z = p.z;
        return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z +
               2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
    }
};

struct Candidate {
    float cost;
    int from, to;          // collapse `from` into `to`
    unsigned verFrom, verTo;
    bool operator>(const Candidate& o) const { return cost > o.cost; }
};

float colorDist2(Color a, Color b) {
    const float dr = (a.r - b.r) / 255.f, dg = (a.g - b.g) / 255.f, db = (a.b - b.b) / 255.f;
    return dr * dr + dg * dg + db * db;
}

} // namespace

void simplifyMesh(RawMesh& m, int targetTris, float colorWeight, float colorEdge) {
    const int nv = (int)m.p.size();
    int nf = (int)m.idx.size() / 3;
    if (targetTris <= 0 || nf <= targetTris || nv < 4) return;

    std::vector<int> tri(m.idx.begin(), m.idx.end());
    std::vector<char> faceAlive((size_t)nf, 1), vertAlive((size_t)nv, 1);
    std::vector<unsigned> version((size_t)nv, 0);
    std::vector<std::vector<int>> vf((size_t)nv);  // vertex -> faces
    for (int f = 0; f < nf; ++f)
        for (int k = 0; k < 3; ++k) vf[(size_t)tri[(size_t)f * 3 + k]].push_back(f);

    auto faceNormal = [&](int f, Vector3& n, float& area) {
        const Vector3& a = m.p[(size_t)tri[(size_t)f * 3]];
        const Vector3& b = m.p[(size_t)tri[(size_t)f * 3 + 1]];
        const Vector3& c = m.p[(size_t)tri[(size_t)f * 3 + 2]];
        Vector3 cr = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        float l = Vector3Length(cr);
        area = 0.5f * l;
        n = l > 1e-20f ? Vector3Scale(cr, 1.f / l) : Vector3{0, 0, 0};
    };

    // vertex quadrics (planes of the adjacent faces, weighted by area relative to the mean)
    std::vector<Quadric> Q((size_t)nv);
    {
        double meanArea = 0.0;
        for (int f = 0; f < nf; ++f) {
            Vector3 n;
            float ar;
            faceNormal(f, n, ar);
            meanArea += ar;
        }
        meanArea = std::max(meanArea / std::max(nf, 1), 1e-12);
        for (int f = 0; f < nf; ++f) {
            Vector3 n;
            float ar;
            faceNormal(f, n, ar);
            if (ar <= 0.f) continue;
            const Vector3& a = m.p[(size_t)tri[(size_t)f * 3]];
            const double d = -(n.x * a.x + n.y * a.y + n.z * a.z);
            const double w = ar / meanArea;
            for (int k = 0; k < 3; ++k) Q[(size_t)tri[(size_t)f * 3 + k]].addPlane(n.x, n.y, n.z, d, w);
        }
    }
    // boundary vertices (open edges) never move
    std::vector<char> boundary((size_t)nv, 0);
    {
        std::vector<std::pair<uint64_t, int>> edges;
        edges.reserve((size_t)nf * 3);
        for (int f = 0; f < nf; ++f)
            for (int k = 0; k < 3; ++k) {
                uint32_t a = (uint32_t)tri[(size_t)f * 3 + k], b = (uint32_t)tri[(size_t)f * 3 + (k + 1) % 3];
                if (a > b) std::swap(a, b);
                edges.push_back({((uint64_t)a << 32) | b, 1});
            }
        std::sort(edges.begin(), edges.end());
        for (size_t i = 0; i < edges.size();) {
            size_t j = i;
            while (j < edges.size() && edges[j].first == edges[i].first) ++j;
            if (j - i == 1) {
                boundary[(size_t)(edges[i].first >> 32)] = 1;
                boundary[(size_t)(edges[i].first & 0xffffffffu)] = 1;
            }
            i = j;
        }
    }

    // colour features: a vertex with a clearly different colour in its 1-ring (a painted edge, a stripe, a
    // neckline) never moves, so the boundaries keep the resolution they were meshed at; only the flat-coloured
    // areas between them are coarsened
    std::vector<char> feature((size_t)nv, 0);
    if (colorEdge > 0.f) {
        for (int f = 0; f < nf; ++f)
            for (int k = 0; k < 3; ++k) {
                const int a = tri[(size_t)f * 3 + k], b = tri[(size_t)f * 3 + (k + 1) % 3];
                if (colorDist2(m.c[(size_t)a], m.c[(size_t)b]) > colorEdge) feature[(size_t)a] = feature[(size_t)b] = 1;
            }
    }

    auto neighbors = [&](int v, std::vector<int>& out) {
        out.clear();
        for (int f : vf[(size_t)v]) {
            if (!faceAlive[(size_t)f]) continue;
            for (int k = 0; k < 3; ++k) {
                int u = tri[(size_t)f * 3 + k];
                if (u != v) out.push_back(u);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };

    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
    auto pushEdge = [&](int a, int b) {
        // try both directions, keep the cheaper
        for (int dir = 0; dir < 2; ++dir) {
            int from = dir == 0 ? a : b, to = dir == 0 ? b : a;
            if (boundary[(size_t)from] || feature[(size_t)from]) continue;
            Quadric q = Q[(size_t)from];
            q.add(Q[(size_t)to]);
            float geo = (float)std::max(0.0, q.eval(m.p[(size_t)to]));
            float l2 = Vector3DistanceSqr(m.p[(size_t)from], m.p[(size_t)to]);
            float col = colorWeight * colorDist2(m.c[(size_t)from], m.c[(size_t)to]) * l2 * 1e4f;
            heap.push({geo + col, from, to, version[(size_t)from], version[(size_t)to]});
        }
    };
    auto fillHeap = [&]() {
        std::vector<int> nb;
        for (int v = 0; v < nv; ++v) {
            if (!vertAlive[(size_t)v]) continue;
            neighbors(v, nb);
            for (int u : nb)
                if (u > v) pushEdge(v, u);
        }
    };
    fillHeap();

    std::vector<int> nbFrom, nbTo;
    int alive = nf;
    // edges rejected earlier (link condition, flips) may become valid later: refill a couple of times
    for (int pass = 0; pass < 3 && alive > targetTris; ++pass) {
    if (pass > 0) fillHeap();
    while (alive > targetTris && !heap.empty()) {
        Candidate c = heap.top();
        heap.pop();
        const int from = c.from, to = c.to;
        if (!vertAlive[(size_t)from] || !vertAlive[(size_t)to]) continue;
        if (version[(size_t)from] != c.verFrom || version[(size_t)to] != c.verTo) continue;
        // link condition: exactly two shared neighbours (keeps the mesh manifold)
        neighbors(from, nbFrom);
        neighbors(to, nbTo);
        if (!std::binary_search(nbFrom.begin(), nbFrom.end(), to)) continue;
        int shared = 0;
        for (int u : nbFrom)
            if (std::binary_search(nbTo.begin(), nbTo.end(), u)) ++shared;
        if (shared != 2) continue;
        // no flipped or needle faces around `from` after it moves onto `to`
        bool ok = true;
        const Vector3 target = m.p[(size_t)to];
        for (int f : vf[(size_t)from]) {
            if (!faceAlive[(size_t)f]) continue;
            int i0 = tri[(size_t)f * 3], i1 = tri[(size_t)f * 3 + 1], i2 = tri[(size_t)f * 3 + 2];
            if (i0 == to || i1 == to || i2 == to) continue;  // collapses away
            Vector3 nOld;
            float aOld;
            faceNormal(f, nOld, aOld);
            Vector3 P[3] = {m.p[(size_t)i0], m.p[(size_t)i1], m.p[(size_t)i2]};
            for (int k = 0; k < 3; ++k)
                if (tri[(size_t)f * 3 + k] == from) P[k] = target;
            Vector3 cr = Vector3CrossProduct(Vector3Subtract(P[1], P[0]), Vector3Subtract(P[2], P[0]));
            float l = Vector3Length(cr);
            if (l < 1e-14f || Vector3DotProduct(Vector3Scale(cr, 1.f / l), nOld) < 0.35f) {
                ok = false;
                break;
            }
            // triangle quality: reject slivers (area vs longest edge squared)
            float e0 = Vector3DistanceSqr(P[0], P[1]), e1 = Vector3DistanceSqr(P[1], P[2]), e2 = Vector3DistanceSqr(P[2], P[0]);
            if (0.5f * l < 0.02f * std::max(e0, std::max(e1, e2))) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        // collapse
        for (int f : vf[(size_t)from]) {
            if (!faceAlive[(size_t)f]) continue;
            int* t = &tri[(size_t)f * 3];
            if (t[0] == to || t[1] == to || t[2] == to) {
                faceAlive[(size_t)f] = 0;
                --alive;
                continue;
            }
            for (int k = 0; k < 3; ++k)
                if (t[k] == from) t[k] = to;
            vf[(size_t)to].push_back(f);
        }
        vertAlive[(size_t)from] = 0;
        vf[(size_t)from].clear();
        Q[(size_t)to].add(Q[(size_t)from]);
        ++version[(size_t)to];
        // compact the face list of `to` and requeue its edges
        std::vector<int>& lst = vf[(size_t)to];
        lst.erase(std::remove_if(lst.begin(), lst.end(), [&](int f) { return !faceAlive[(size_t)f]; }), lst.end());
        std::sort(lst.begin(), lst.end());
        lst.erase(std::unique(lst.begin(), lst.end()), lst.end());
        // only the edges touching `to` changed cost (its quadric grew); older entries are now stale
        neighbors(to, nbTo);
        for (int u : nbTo) pushEdge(to, u);
    }
    }

    // compact
    std::vector<int> remap((size_t)nv, -1);
    RawMesh out;
    for (int v = 0; v < nv; ++v) {
        if (!vertAlive[(size_t)v]) continue;
        bool used = false;
        for (int f : vf[(size_t)v])
            if (faceAlive[(size_t)f]) {
                used = true;
                break;
            }
        if (!used) continue;
        remap[(size_t)v] = (int)out.p.size();
        out.p.push_back(m.p[(size_t)v]);
        out.n.push_back(m.n[(size_t)v]);
        out.c.push_back(m.c[(size_t)v]);
    }
    for (int f = 0; f < nf; ++f) {
        if (!faceAlive[(size_t)f]) continue;
        int a = remap[(size_t)tri[(size_t)f * 3]], b = remap[(size_t)tri[(size_t)f * 3 + 1]], cc = remap[(size_t)tri[(size_t)f * 3 + 2]];
        if (a < 0 || b < 0 || cc < 0 || a == b || b == cc || a == cc) continue;
        out.idx.push_back((unsigned)a);
        out.idx.push_back((unsigned)b);
        out.idx.push_back((unsigned)cc);
    }
    m = std::move(out);
}

Mesh uploadRaw(const RawMesh& m) {
    MeshBuilder mb;
    for (size_t i = 0; i < m.p.size(); ++i) mb.vertex(m.p[i], m.n[i], {m.p[i].x * 8.f, m.p[i].y * 8.f}, m.c[i]);
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) mb.triangle((int)m.idx[i], (int)m.idx[i + 1], (int)m.idx[i + 2]);
    return mb.build(true);
}

} // namespace chr
} // namespace r3d
