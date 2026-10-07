// Characters module: props owned by the characters — our table's four chairs, the ince belli tea glasses
// (with liquid at several fill levels), saucers, spoons, the oralet, Mahmut's cigarette, Rıza's tespih,
// Nuri's spectacles, the çaycı's hanging tray, and the background patrons' dice, cards and newspaper.
#include "r3d/CharactersInternal.h"
#include "ui/Common.h"

#include <algorithm>

namespace r3d {
namespace chr {

void buildPeople(Meshes& M, Renderer& r, const std::function<void(MeshJobs&)>& more);
void freePerson(PersonMeshes& pm, Renderer& r);

namespace {

Color rgb(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}

constexpr float GS = 1.12f;  // tea glass scale (the table's tiles are ~1.15x real size too)

// ince belli glass: outer wall bottom -> rim, then the inner wall back down (unscaled, metres)
const Vector2 kOuter[] = {{0.0f, 0.0f},        {0.0205f, 0.0f},     {0.0214f, 0.0025f}, {0.0206f, 0.0060f},
                          {0.0200f, 0.0120f},  {0.0207f, 0.0200f},  {0.0202f, 0.0300f}, {0.0181f, 0.0420f},
                          {0.0168f, 0.0500f},  {0.0174f, 0.0580f},  {0.0194f, 0.0690f}, {0.0219f, 0.0800f},
                          {0.0236f, 0.0890f},  {0.0241f, 0.0925f}};
const Vector2 kInner[] = {{0.0229f, 0.0925f}, {0.0225f, 0.0890f}, {0.0207f, 0.0800f}, {0.0182f, 0.0690f},
                          {0.0162f, 0.0580f}, {0.0155f, 0.0500f}, {0.0168f, 0.0420f}, {0.0189f, 0.0300f},
                          {0.0194f, 0.0200f}, {0.0186f, 0.0120f}, {0.0170f, 0.0095f}, {0.0f, 0.0090f}};

float innerR(float yUnscaled) {
    // inner profile runs top -> bottom
    const int n = (int)(sizeof(kInner) / sizeof(kInner[0])) - 1;  // skip the axis point
    if (yUnscaled >= kInner[0].y) return kInner[0].x;
    for (int i = 0; i + 1 < n; ++i) {
        const Vector2 a = kInner[i], b = kInner[i + 1];
        if (yUnscaled <= a.y && yUnscaled >= b.y) {
            float t = (a.y - yUnscaled) / std::max(a.y - b.y, 1e-6f);
            return lerpf(a.x, b.x, t);
        }
    }
    return kInner[n - 1].x;
}

void appendGlass(MeshBuilder& b, Color glassCol, Color rim) {
    std::vector<Vector2> prof;
    for (const Vector2& p : kOuter) prof.push_back({p.x * GS, p.y * GS});
    for (const Vector2& p : kInner) prof.push_back({p.x * GS, p.y * GS});
    b.lathe(prof, 32, false, false, glassCol);
    // thin gold rim, just proud of the glass
    std::vector<Vector2> ring;
    const float rr = 0.0235f * GS, ry = 0.0915f * GS;
    for (int i = 0; i <= 8; ++i) {
        float t = -PI_F * 0.5f + PI_F * i / 8.f;
        ring.push_back({rr + 0.0011f * std::cos(t), ry + 0.0016f * std::sin(t)});
    }
    b.lathe(ring, 32, false, false, rim);
}

void appendLiquid(MeshBuilder& b, float level01, Color c) {
    const float y0 = 0.0098f, y1 = 0.0800f;
    const float h = lerpf(y0 + 0.002f, y1, level01);
    std::vector<Vector2> prof;
    prof.push_back({(innerR(y0) - 0.0006f) * GS, y0 * GS});
    for (int i = (int)(sizeof(kInner) / sizeof(kInner[0])) - 2; i >= 0; --i) {
        float y = kInner[i].y;
        if (y <= y0 || y >= h) continue;
        prof.push_back({(kInner[i].x - 0.0005f) * GS, y * GS});
    }
    prof.push_back({(innerR(h) - 0.0005f) * GS, h * GS});
    b.lathe(prof, 28, true, true, c);
}

void appendSaucer(MeshBuilder& b, Color porcelain, Color line) {
    const Vector2 prof[] = {{0.0f, 0.0f},      {0.030f, 0.0f},     {0.034f, 0.0010f},  {0.050f, 0.0040f},
                            {0.0570f, 0.0080f}, {0.0595f, 0.0105f}, {0.0588f, 0.0116f}, {0.0555f, 0.0098f},
                            {0.0480f, 0.0062f}, {0.0265f, 0.0046f}, {0.0247f, 0.0064f}, {0.0228f, 0.0063f},
                            {0.0f, 0.0055f}};
    std::vector<Vector2> p;
    for (const Vector2& v : prof) p.push_back({v.x * GS, v.y * GS});
    b.lathe(p, 36, false, false, porcelain);
    std::vector<Vector2> ring;
    for (int i = 0; i <= 6; ++i) {
        float t = -PI_F * 0.5f + PI_F * i / 6.f;
        ring.push_back({(0.0590f + 0.0008f * std::cos(t)) * GS, (0.0110f + 0.0009f * std::sin(t)) * GS});
    }
    b.lathe(ring, 36, false, false, line);
}

Image strawImage(int S) {
    Image img = GenImageColor(S, S, rgb(150, 116, 60));
    Color* px = (Color*)img.data;
    const int cells = 16;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (x + 0.5f) / S * cells, v = (y + 0.5f) / S * cells;
            int cu = (int)std::floor(u), cv = (int)std::floor(v);
            float fu = u - cu, fv = v - cv;
            bool horiz = ((cu + cv) & 1) == 0;
            float across = horiz ? fv : fu;  // position across the strand
            float along = horiz ? fu : fv;
            float strand = std::sin(across * PI_F);
            float k = 0.55f + 0.45f * strand * (0.85f + 0.15f * std::sin(along * PI_F));
            float grain = hashf((uint32_t)(x * 7 + y * 131)) * 0.12f;
            Color c{(unsigned char)clampf((206.f + 20.f * grain) * k, 0, 255),
                    (unsigned char)clampf((172.f + 10.f * grain) * k, 0, 255),
                    (unsigned char)clampf((104.f) * k, 0, 255), 255};
            px[y * S + x] = c;
        }
    return img;
}

Mesh buildChair() {
    MeshBuilder b;
    const Color c = WHITE;
    // turned legs
    std::vector<Vector2> leg = {{0.0f, 0.0f},      {0.0150f, 0.0f},   {0.0160f, 0.012f}, {0.0135f, 0.03f},
                                {0.0150f, 0.13f},  {0.0185f, 0.15f},  {0.0150f, 0.17f},  {0.0170f, 0.33f},
                                {0.0205f, 0.36f},  {0.0180f, 0.38f},  {0.0190f, 0.44f},  {0.0f, 0.44f}};
    const float fx = 0.185f, fz = -0.170f, bz = 0.175f;
    for (int i = 0; i < 2; ++i) {
        float x = i == 0 ? -fx : fx;
        b.setTransform(MatrixTranslate(x, 0, fz));
        b.lathe(leg, 12, false, false, c);
        b.setTransform(MatrixTranslate(x, 0, bz));
        b.lathe(leg, 12, false, false, c);
    }
    b.resetTransform();
    // seat frame rails
    b.roundedBox({0, 0.440f, fz - 0.005f}, {0.41f, 0.036f, 0.030f}, 0.008f, 2, c);
    b.roundedBox({0, 0.440f, bz + 0.005f}, {0.41f, 0.036f, 0.030f}, 0.008f, 2, c);
    b.roundedBox({-fx, 0.440f, 0.0f}, {0.030f, 0.036f, 0.38f}, 0.008f, 2, c);
    b.roundedBox({fx, 0.440f, 0.0f}, {0.030f, 0.036f, 0.38f}, 0.008f, 2, c);
    // rear posts continue up, leaning back
    for (int i = 0; i < 2; ++i) {
        float x = i == 0 ? -fx : fx;
        b.capsule({x, 0.44f, bz}, {x * 1.02f, 0.935f, bz + 0.055f}, 0.0165f, 10, c);
        b.sphere({x * 1.02f, 0.955f, bz + 0.057f}, 0.020f, 8, 10, c);
    }
    // back slats (top rail and a middle slat)
    b.setTransform(MatrixMultiply(MatrixRotateX(-8.f * DEG2RAD), MatrixTranslate(0, 0.865f, bz + 0.048f)));
    b.roundedBox({0, 0, 0}, {0.39f, 0.080f, 0.022f}, 0.009f, 2, c);
    b.setTransform(MatrixMultiply(MatrixRotateX(-6.f * DEG2RAD), MatrixTranslate(0, 0.70f, bz + 0.030f)));
    b.roundedBox({0, 0, 0}, {0.38f, 0.042f, 0.018f}, 0.007f, 2, c);
    b.resetTransform();
    // stretchers
    b.capsule({-fx, 0.16f, fz}, {-fx, 0.16f, bz}, 0.0085f, 8, c);
    b.capsule({fx, 0.16f, fz}, {fx, 0.16f, bz}, 0.0085f, 8, c);
    b.capsule({-fx, 0.12f, fz}, {fx, 0.12f, fz}, 0.0085f, 8, c);
    b.capsule({-fx, 0.20f, bz}, {fx, 0.20f, bz}, 0.0085f, 8, c);
    return b.build();
}

Mesh buildChairSeat() {
    MeshBuilder b;
    b.roundedBox({0, 0.451f, 0.0f}, {0.345f, 0.018f, 0.325f}, 0.009f, 2, WHITE);
    return b.build();
}

void appendSpoon(MeshBuilder& b, Color c) {
    b.ellipsoid({0, 0, 0}, {0.0075f, 0.0022f, 0.0105f}, 10, 12, c);
    b.capsule({0, 0.0015f, 0.010f}, {0, 0.004f, 0.088f}, 0.0017f, 6, c);
    b.ellipsoid({0, 0.0045f, 0.089f}, {0.0032f, 0.0015f, 0.005f}, 6, 8, c);
}

Mesh buildCigarette() {
    MeshBuilder b;
    const float R = 0.0042f;
    b.lathe({{0, 0}, {R, 0}, {R, 0.022f}}, 12, false, false, rgb(206, 142, 72));
    b.lathe({{R, 0.022f}, {R, 0.0715f}}, 12, false, false, rgb(242, 238, 230));
    b.lathe({{R, 0.0715f}, {R * 0.97f, 0.0738f}, {R * 0.7f, 0.0752f}, {0, 0.0756f}}, 12, false, false,
            rgb(112, 108, 104));
    return b.build(true);
}

// Four amber beads on their string, along +Y over one chain segment (TESPIH_SEG).
Mesh buildBead4() {
    MeshBuilder b;
    const float step = TESPIH_SEG / 4.f;
    for (int i = 0; i < 4; ++i)
        b.ellipsoid({0, step * (0.5f + i), 0}, {0.0064f, 0.0056f, 0.0064f}, 8, 10,
                    i % 2 ? rgb(236, 226, 214) : WHITE);
    b.capsule({0, 0, 0}, {0, TESPIH_SEG, 0}, 0.0009f, 4, rgb(60, 40, 30));
    return b.build(true);
}

Mesh buildImame() {
    MeshBuilder b;
    b.lathe({{0.0f, 0.0f}, {0.0040f, 0.0015f}, {0.0058f, 0.0075f}, {0.0048f, 0.0160f}, {0.0037f, 0.0290f},
             {0.0026f, 0.0390f}, {0.0033f, 0.0420f}, {0.0f, 0.0445f}},
            14, false, false, WHITE);
    return b.build(true);
}

Mesh buildTassel() {
    MeshBuilder b;
    b.lathe({{0.0f, 0.0f}, {0.0032f, 0.0010f}, {0.0036f, 0.0050f}, {0.0050f, 0.0150f}, {0.0080f, 0.0380f},
             {0.0060f, 0.0420f}, {0.0f, 0.0425f}},
            12, false, false, WHITE);
    return b.build(true);
}

// Nuri's spectacles in head space: thin metal frames and two lenses.
void buildSpectacles(Mesh& frame, Mesh& lenses) {
    MeshBuilder f, l;
    const float cx = 0.0315f, cy = 0.1005f, cz = -0.0955f, hw = 0.0175f, hh = 0.0130f;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        std::vector<Vector3> loop;
        const int N = 28;
        for (int i = 0; i <= N; ++i) {
            float a = 2.f * PI_F * i / N;
            float ca = std::cos(a), sa = std::sin(a);
            // rounded rectangle via superellipse
            float ex = std::copysign(std::pow(std::fabs(ca), 0.6f), ca) * hw;
            float ey = std::copysign(std::pow(std::fabs(sa), 0.6f), sa) * hh;
            float z = cz + 0.0025f * (ex * ex + ey * ey) / (hw * hw);
            loop.push_back({sgn * cx + ex, cy + ey, z + (sgn * ex > 0 ? 0.003f * (sgn * ex / hw) : 0.f)});
        }
        f.tube(loop, 0.0011f, 6, WHITE);
        // lens: fan from the centre
        int c0 = l.vertex({sgn * cx, cy, cz - 0.0012f}, {0, 0, -1}, {0.5f, 0.5f}, WHITE);
        int first = l.vertexCount();
        for (int i = 0; i < N; ++i) l.vertex(loop[i], vnorm({(loop[i].x - sgn * cx) * 3.f, (loop[i].y - cy) * 3.f, -1.f}), {0, 0}, WHITE);
        for (int i = 0; i < N; ++i) {
            int a = first + i, b2 = first + (i + 1) % N;
            if (sgn > 0) l.triangle(c0, b2, a);
            else l.triangle(c0, b2, a);
        }
        // temple arm back to the ear, hooking down behind it
        std::vector<Vector3> arm = {{sgn * (cx + hw), cy + 0.002f, cz + 0.004f},
                                    {sgn * (cx + hw + 0.012f), cy + 0.004f, cz + 0.016f},
                                    {sgn * 0.0735f, cy + 0.0045f, -0.040f},
                                    {sgn * 0.0765f, cy + 0.003f, -0.004f},
                                    {sgn * 0.0770f, cy - 0.012f, 0.012f}};
        f.tube(arm, 0.0010f, 5, WHITE);
    }
    std::vector<Vector3> bridge = {{-(cx - hw), cy + 0.003f, cz},
                                   {-0.006f, cy + 0.0075f, cz - 0.0015f},
                                   {0.006f, cy + 0.0075f, cz - 0.0015f},
                                   {cx - hw, cy + 0.003f, cz}};
    f.tube(bridge, 0.0011f, 6, WHITE);
    frame = f.build(true);
    lenses = l.build(true);
}

// Askılı tepsi: tray-local origin at the handle ring the hand holds; the tray hangs 0.30 m below.
// The askılı tepsi's hanger and tray (its glasses are the ocakçı's to fill: Cast::submitTray).
void buildTray(Mesh& hanger) {
    MeshBuilder h;
    const float trayY = -0.300f, R = 0.140f;
    // handle: a vertical loop and a hub
    {
        std::vector<Vector3> loop;
        for (int i = 0; i <= 20; ++i) {
            float a = 2.f * PI_F * i / 20.f;
            loop.push_back({0.026f * std::sin(a), 0.004f + 0.026f * std::cos(a), 0.f});
        }
        h.tube(loop, 0.0035f, 8, WHITE);
        h.sphere({0, -0.030f, 0}, 0.009f, 8, 10, WHITE);
        h.capsule({0, -0.022f, 0}, {0, -0.036f, 0}, 0.005f, 8, WHITE);
    }
    // three arms curving down to the rim
    for (int k = 0; k < 3; ++k) {
        float a = PI_F * 0.5f + 2.f * PI_F * k / 3.f;
        Vector3 dir{std::cos(a), 0, std::sin(a)};
        std::vector<Vector3> arm;
        for (int i = 0; i <= 12; ++i) {
            float s = i / 12.f;
            // quadratic bezier: hub -> outward bulge -> rim
            Vector3 p0{0, -0.034f, 0}, p1 = Vector3Add(Vector3Scale(dir, 0.15f), {0, -0.08f, 0}),
                    p2 = Vector3Add(Vector3Scale(dir, R - 0.004f), {0, trayY + 0.012f, 0});
            Vector3 q = Vector3Add(Vector3Add(Vector3Scale(p0, (1 - s) * (1 - s)), Vector3Scale(p1, 2 * s * (1 - s))),
                                   Vector3Scale(p2, s * s));
            arm.push_back(q);
        }
        h.tube(arm, 0.0028f, 6, WHITE);
    }
    // the tray
    h.setTransform(MatrixTranslate(0, trayY, 0));
    h.lathe({{0.0f, 0.0f}, {R - 0.01f, 0.0f}, {R, 0.004f}, {R + 0.004f, 0.014f}, {R + 0.001f, 0.016f},
             {R - 0.004f, 0.006f}, {R - 0.012f, 0.0025f}, {0.0f, 0.0025f}},
            40, false, false, WHITE);
    h.resetTransform();
    hanger = h.build(true);
}

Mesh buildDice() {
    Sdf s;
    const float hs = 0.0078f;
    s.box({0, 0, 0}, {hs, hs, hs}, 0.0022f, rgb(238, 232, 218));
    const Color pip = rgb(30, 24, 22);
    auto pipAt = [&](Vector3 c) { s.cutEllipsoid(c, {0.0016f, 0.0016f, 0.0016f}, pip, 0.0004f); };
    const float o = 0.0042f;
    pipAt({0, hs, 0});                                                     // 1 top
    pipAt({-o, -o, hs}); pipAt({o, o, hs});                               // 2 front
    pipAt({hs, -o, -o}); pipAt({hs, 0, 0}); pipAt({hs, o, o});             // 3 right
    for (int i = 0; i < 4; ++i) pipAt({-hs, (i & 1) ? o : -o, (i & 2) ? o : -o}); // 4 left
    for (int i = 0; i < 4; ++i) pipAt({(i & 1) ? o : -o, (i & 2) ? o : -o, -hs}); // 5 back
    pipAt({0, 0, -hs});
    for (int i = 0; i < 6; ++i) pipAt({(i & 1) ? o : -o, -hs, -o + o * (float)(i / 2)}); // 6 bottom
    return buildSdf(s, 0.00055f, 500);
}

void appendCard(MeshBuilder& b, const Matrix& m, bool back) {
    b.setTransform(m);
    b.box({0, 0.043f, 0}, {0.056f, 0.086f, 0.0007f}, rgb(244, 240, 230));
    if (back) b.box({0, 0.043f, 0.0005f}, {0.050f, 0.080f, 0.0004f}, rgb(160, 30, 34));
    b.resetTransform();
}

Mesh buildCardFan() {
    MeshBuilder b;
    for (int i = 0; i < 6; ++i) {
        float a = (-25.f + 10.f * i) * DEG2RAD;
        Matrix m = MatrixMultiply(MatrixRotateZ(-a), MatrixTranslate(0.004f * i - 0.01f, 0.f, -0.0012f * i));
        appendCard(b, m, true);
    }
    return b.build(true);
}

Mesh buildCard() {
    MeshBuilder b;
    appendCard(b, MatrixRotateX(-90.f * DEG2RAD), true);
    return b.build(true);
}

// Newspaper held open: two panels in a shallow V (x across, y up, facing -Z toward the reader).
Mesh buildNewspaper() {
    MeshBuilder b;
    const float w = 0.29f, h = 0.40f, fold = 0.07f;
    Vector3 n0 = vnorm({0.24f, 0, -1}), n1 = vnorm({-0.24f, 0, -1});
    int a = b.vertex({-w, -h / 2, fold}, n0, {0, 0});
    int c = b.vertex({0, -h / 2, 0}, n0, {0.5f, 0});
    int d = b.vertex({0, h / 2, 0}, n0, {0.5f, 1});
    int e = b.vertex({-w, h / 2, fold}, n0, {0, 1});
    b.quad(a, c, d, e);
    int f = b.vertex({0, -h / 2, 0}, n1, {0.5f, 0});
    int g = b.vertex({w, -h / 2, fold}, n1, {1, 0});
    int hh = b.vertex({w, h / 2, fold}, n1, {1, 1});
    int k = b.vertex({0, h / 2, 0}, n1, {0.5f, 1});
    b.quad(f, g, hh, k);
    return b.build();
}

void drawNewspaperCanvas(RenderTexture2D& rt) {
    beginCanvas(rt);
    const int W = rt.texture.width, H = rt.texture.height;
    DrawRectangle(0, 0, W, H, rgb(226, 220, 204));
    for (int half = 0; half < 2; ++half) {
        int x0 = half * W / 2 + 14, x1 = (half + 1) * W / 2 - 14;
        if (half == 0) {
            ui::drawText(ui::FontId::Sign, "GÜNDEM", {(float)x0, 12.f}, 44.f, rgb(30, 28, 26));
            DrawRectangle(x0, 62, x1 - x0, 3, rgb(40, 36, 34));
            DrawRectangle(x0, 74, (x1 - x0) * 55 / 100, 110, rgb(120, 116, 110));
        } else {
            ui::drawText(ui::FontId::Sign, "SPOR", {(float)x0, 12.f}, 44.f, rgb(150, 30, 30));
            DrawRectangle(x0, 62, x1 - x0, 3, rgb(40, 36, 34));
            DrawRectangle(x0 + (x1 - x0) * 45 / 100, 74, (x1 - x0) * 55 / 100, 90, rgb(96, 110, 90));
        }
        int cols = 3;
        int cw = (x1 - x0 - (cols - 1) * 10) / cols;
        for (int c = 0; c < cols; ++c)
            for (int y = 196; y < H - 12; y += 9) {
                int len = cw - (int)(hashf((uint32_t)(c * 977 + y * 31 + half * 7)) * 18.f);
                DrawRectangle(x0 + c * (cw + 10), y, len, 4, rgb(110, 106, 100));
            }
        for (int y = 82; y < 190; y += 9)
            DrawRectangle(half == 0 ? x0 + (x1 - x0) * 58 / 100 : x0, y, (x1 - x0) * 40 / 100, 4, rgb(110, 106, 100));
    }
    endCanvas(rt);
}

} // namespace

float glassScale() { return GS; }

void buildAll(Meshes& M, Renderer& r, uint64_t seed, const std::function<void(MeshJobs&)>& morePeople) {
    buildPeople(M, r, morePeople);

    M.woodTex = genWoodTexture(256, rgb(150, 98, 58), rgb(80, 48, 28), (uint32_t)(seed * 7u + 3u), 9.f);
    {
        Image img = strawImage(256);
        M.strawTex = textureFromImage(img);
        UnloadImage(img);
    }
    M.chair = buildChair();
    M.chairSeat = buildChairSeat();

    {
        MeshBuilder b;
        appendGlass(b, rgb(236, 242, 248, 46), rgb(214, 170, 80, 255));
        M.glass = b.build(true);
    }
    {
        // the saucer with its teaspoon lying on it (one draw)
        MeshBuilder b;
        appendSaucer(b, rgb(242, 238, 230), rgb(200, 160, 70));
        b.setTransform(MatrixMultiply(MatrixRotateX(-5.f * DEG2RAD), MatrixTranslate(0.f, 0.0068f, 0.032f)));
        appendSpoon(b, rgb(196, 194, 190));
        b.resetTransform();
        M.saucer = b.build(true);
    }
    for (int i = 0; i < Meshes::TEA_LEVELS; ++i) {
        MeshBuilder b;
        appendLiquid(b, (float)(i + 1) / (float)Meshes::TEA_LEVELS, WHITE);
        M.tea[i] = b.build();
    }
    M.tulipH = 0.0925f * GS;
    M.cigarette = buildCigarette();
    {
        MeshBuilder b;
        b.sphere({0, 0.0745f, 0}, 0.0036f, 6, 8, WHITE);
        M.ember = b.build();
    }
    M.bead4 = buildBead4();
    M.imame = buildImame();
    M.tassel = buildTassel();
    buildSpectacles(M.spectacles, M.lenses);
    buildTray(M.trayHanger);
    M.dice = buildDice();
    M.cardFan = buildCardFan();
    M.card = buildCard();
    M.newspaper = buildNewspaper();
    M.paperCanvas = makeCanvas(1024, 720);
    M.paperCanvasOk = M.paperCanvas.id != 0;
    if (M.paperCanvasOk) drawNewspaperCanvas(M.paperCanvas);

    M.wood = r.makeMat(WHITE, M.woodTex, 0.28f, 30.f, 0.f, 0.08f);
    M.straw = r.makeMat(WHITE, M.strawTex, 0.08f, 10.f, 0.f, 0.1f);
    M.glassMat = r.makeMat(WHITE, Texture2D{}, 0.95f, 150.f, 0.f, 0.75f);
    M.teaMat = r.makeMat(rgb(138, 40, 13), Texture2D{}, 0.8f, 90.f, 0.10f, 0.1f);
    M.oraletMat = r.makeMat(rgb(236, 112, 22), Texture2D{}, 0.7f, 80.f, 0.16f, 0.1f);
    M.porcelain = r.makeMat(WHITE, Texture2D{}, 0.55f, 70.f, 0.f, 0.1f);
    M.cigMat = r.makeMat(WHITE, Texture2D{}, 0.1f, 10.f, 0.f, 0.1f);
    M.emberMat = r.makeMat(rgb(255, 120, 40), Texture2D{}, 0.f, 8.f, 1.f, 0.f);
    M.amber = r.makeMat(rgb(200, 108, 28), Texture2D{}, 0.75f, 80.f, 0.06f, 0.35f);
    M.tassleMat = r.makeMat(rgb(120, 22, 26), Texture2D{}, 0.1f, 8.f, 0.f, 0.3f);
    M.lensMat = r.makeMat(rgb(220, 236, 246, 34), Texture2D{}, 0.95f, 160.f, 0.f, 0.6f);
    M.frameMat = r.makeMat(rgb(168, 136, 80), Texture2D{}, 0.8f, 70.f, 0.f, 0.1f);
    M.trayMat = r.makeMat(rgb(206, 204, 198), Texture2D{}, 0.85f, 80.f, 0.f, 0.2f);
    M.diceMat = r.makeMat(WHITE, Texture2D{}, 0.5f, 60.f, 0.f, 0.05f);
    M.cardMat = r.makeMat(WHITE, Texture2D{}, 0.2f, 20.f, 0.f, 0.05f);
    M.paperMat = r.makeMat(WHITE, M.paperCanvasOk ? M.paperCanvas.texture : Texture2D{}, 0.05f, 8.f, 0.f, 0.1f);
}

void freeAll(Meshes& M, Renderer& r) {
    auto U = [](Mesh& m) {
        if (m.vertexCount > 0) UnloadMesh(m);
        m = Mesh{};
    };
    for (auto& side : M.hand)
        for (Mesh& m : side) U(m);
    for (auto& side : M.handLo)
        for (Mesh& m : side) U(m);
    for (Mesh& m : M.eye) U(m);
    U(M.lidUpper);
    U(M.mouthCavity);
    for (Mesh& m : M.lowerLip) U(m);
    U(M.chair);
    U(M.chairSeat);
    U(M.glass);
    U(M.saucer);
    for (Mesh& m : M.tea) U(m);
    U(M.cigarette);
    U(M.ember);
    U(M.bead4);
    U(M.imame);
    U(M.tassel);
    U(M.spectacles);
    U(M.lenses);
    U(M.trayHanger);
    U(M.dice);
    U(M.cardFan);
    U(M.card);
    U(M.newspaper);
    for (int i = 0; i < 2; ++i) {
        U(M.walkThigh[i]);
        U(M.walkShin[i]);
    }
    for (PersonMeshes& p : M.person) freePerson(p, r);
    for (PersonMeshes& p : M.patron) freePerson(p, r);
    Mat* mats[] = {&M.eyeMat,   &M.mouthMat,  &M.wood,     &M.straw,    &M.glassMat, &M.teaMat,   &M.oraletMat,
                   &M.porcelain, &M.cigMat,   &M.emberMat, &M.amber,    &M.tassleMat, &M.lensMat,
                   &M.frameMat, &M.trayMat,   &M.diceMat,  &M.cardMat,  &M.paperMat, &M.lipMat};
    for (Mat* m : mats) r.unloadMat(*m);
    unloadTexture(M.woodTex); // (r3d's: it also forgets the opaque note on the id)
    unloadTexture(M.strawTex);
    if (M.paperCanvasOk) unloadCanvas(M.paperCanvas);
    M.paperCanvasOk = false;
}

} // namespace chr
} // namespace r3d
