// Room module: özel günler and the rain in the garden (ozelgun agent; RoomInternal.h Room::Impl "Özel günler").
//   * Bayram (w3d::GunRamazanBayrami / GunKurbanBayrami): strings of red and white bunting under the ceiling (in the
//     garden under the trellis), the flag on the back wall over Kel Mahmut's head (in the garden from the trellis' back
//     beam over the parapet), plates of lokum and akide şekeri on the background tables, a bowl of bayram candy on the counter, and in the
//     garden a "Bayramınız mübarek olsun" banner under the ocak's roof.
//   * Ramazan evenings (after iftar, akşam / gece): güllaç and baklava on the tables, a tray of güllaç on the counter, a
//     "Hoş geldin ya Şehr-i Ramazan" sign with a painted mahya.
//   * Maç gecesi (Pazar akşamı): a "Bu akşam derbi" poster on the back wall, a red and white scarf over the TV and the two
//     teams' pennants beside it (the TV's own fictional teams, BGZ and HLÇ), "DERBİ" on the TV. In the garden the ocakçı
//     has carried the old portable TV out onto a stand by the ocak (it shows the same match, goals count), and a banner.
//     (With Mekân = otomatik a derby night keeps us inside: Room::Impl::resolveGarden.)
//   * A passing shower on a fair spring / summer day (seed; developer override SAKLI_YAGMUR=<seconds>): rainTarget rises
//     over the phase's own value for some minutes. With Mekân = otomatik the room wants to go inside, but while App holds
//     the venue (a hand in play) it only notes it (venuePend); App fades out between hands and calls Room::applyVenue.
// Everything is built once at init (a few small meshes, one canvas atlas); per frame only the day's few submissions.
#include "r3d/Daytime.h"
#include "r3d/RoomGardenInternal.h"
#include "r3d/RoomInternal.h"
#include "r3d/SpecialDay.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace r3d {
namespace rm {

namespace {

constexpr int SP_W = 1024, SP_H = 1536;
namespace spa {
constexpr Rectangle FLAG{0, 0, 600, 400};
constexpr Rectangle DERBY{616, 0, 400, 560};
constexpr Rectangle RAMAZAN{0, 416, 600, 300};
constexpr Rectangle PEN_A{0, 732, 300, 150};
constexpr Rectangle PEN_B{316, 732, 300, 150};
constexpr Rectangle BAN_BAYRAM{0, 1024, 1000, 125};
constexpr Rectangle BAN_RAMAZAN{0, 1160, 1000, 125};
constexpr Rectangle BAN_DERBY{0, 1296, 1000, 125};
}  // namespace spa

// The two teams of the TV's match (RoomCanvas.cpp kShirt / kShirt2): fictional, generic colours.
const Color kTeamA[2] = {{206, 36, 40, 255}, {245, 245, 245, 255}};  // BGZ: red, white
const Color kTeamB[2] = {{250, 214, 40, 255}, {24, 40, 110, 255}};   // HLÇ: yellow, navy
const Color kRed{214, 22, 34, 255}, kWhite{244, 240, 230, 255};

// The back wall over Kel Mahmut's head, between the scoreboard and the price board: the day's flag / sign / poster.
constexpr Vector3 kSlot{0.03f, 1.60f, w3d::ROOM_Z0};

Vector3 sag(Vector3 a, Vector3 b, float t, float s) {
    Vector3 p = Vector3Lerp(a, b, t);
    p.y -= s * 4.f * t * (1.f - t);
    return p;
}

void star(Vector2 c, float r, float rot, Color col) {
    Vector2 p[10];
    for (int i = 0; i < 10; ++i) {
        const float a = rot + (float)i * PI / 5.f, rr = (i % 2 == 0) ? r : r * 0.382f;
        p[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) tri(c, p[i], p[(i + 1) % 10], col);
}

// The Turkish flag at the official proportions (G = the height): the crescent's outer circle at G/2 from the hoist,
// diameter G/2; the inner circle 0.0625 G further, diameter 0.4 G; the star's circle G/3 beyond it, diameter G/4, one
// point toward the hoist.
void drawFlag(Rectangle r) {
    DrawRectangleRec(r, Color{227, 10, 23, 255});
    const float G = r.height, cy = r.y + G * 0.5f;
    const float x0 = r.x + G * 0.5f;
    DrawCircleV({x0, cy}, G * 0.25f, WHITE);
    DrawCircleV({x0 + G * 0.0625f, cy}, G * 0.2f, Color{227, 10, 23, 255});
    const float sx = x0 + G * 0.0625f + G * 0.2f + G / 3.f + G * 0.125f;
    star({sx, cy}, G * 0.125f, PI, WHITE);
    // a little cloth shading: folds as soft vertical bands
    for (int i = 0; i < 6; ++i) {
        const float x = r.x + r.width * (0.1f + 0.16f * (float)i);
        DrawRectangleGradientH((int)x, (int)r.y, (int)(r.width * 0.08f), (int)r.height, Color{0, 0, 0, 0}, Color{0, 0, 0, 26});
    }
}

void drawDerbyPoster(Rectangle r, okey::Rng& rng) {
    // two halves of the teams' colours, split on a diagonal
    DrawRectangleRec(r, kTeamB[1]);
    tri({r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height * 0.75f}, kTeamA[0]);
    tri({r.x + r.width, r.y}, {r.x + r.width, r.y + r.height * 0.25f}, {r.x, r.y + r.height * 0.75f}, kTeamA[0]);
    for (int i = 0; i < 9; ++i) {  // stripes of the scarves
        const float y = r.y + r.height * (0.79f + 0.022f * (float)i);
        DrawRectangleRec({r.x, y, r.width, r.height * 0.011f}, i % 2 ? kTeamB[0] : kTeamA[1]);
    }
    const float cx = r.x + r.width * 0.5f;
    ui::drawTextCentered(ui::FontId::Sign, "BU AKŞAM", {cx, r.y + 70.f}, 46.f, Color{255, 236, 170, 255});
    ui::drawTextCentered(ui::FontId::Sign, "DERBİ", {cx + 3.f, r.y + 158.f}, 104.f, Color{0, 0, 0, 120});
    ui::drawTextCentered(ui::FontId::Sign, "DERBİ", {cx, r.y + 154.f}, 104.f, Color{255, 246, 220, 255});
    // the two crests: plain shields in the teams' colours
    for (int k = 0; k < 2; ++k) {
        const float x = cx + (k ? 90.f : -90.f), y = r.y + 290.f;
        const Color* c = k ? kTeamB : kTeamA;
        DrawRectangleRec({x - 42.f, y - 50.f, 84.f, 60.f}, c[1]);
        tri({x - 42.f, y + 10.f}, {x + 42.f, y + 10.f}, {x, y + 62.f}, c[1]);
        DrawRectangleRec({x - 34.f, y - 42.f, 68.f, 50.f}, c[0]);
        tri({x - 34.f, y + 8.f}, {x + 34.f, y + 8.f}, {x, y + 50.f}, c[0]);
        ui::drawTextCentered(ui::FontId::UiBold, k ? "HLÇ" : "BGZ", {x, y - 12.f}, 30.f, c[1]);
    }
    ui::drawTextCentered(ui::FontId::Sign, "–", {cx, r.y + 290.f}, 50.f, WHITE);
    ui::drawTextCentered(ui::FontId::UiBold, "Saat 21.00 · Dev ekranda", {cx, r.y + 398.f}, 30.f, WHITE);
    ui::drawTextCentered(ui::FontId::Hand, "Çay ocağı maç boyu açık", {cx, r.y + 444.f}, 28.f, Color{255, 230, 150, 255});
    // tape at the corners, a little wear
    for (int k = 0; k < 2; ++k)
        DrawRectanglePro({r.x + (k ? r.width - 24.f : 24.f), r.y + 10.f, 60.f, 20.f}, {30.f, 10.f}, k ? 35.f : -35.f,
                         Color{236, 226, 190, 200});
    for (int i = 0; i < 60; ++i)
        DrawCircleV({rng.uniform(r.x, r.x + r.width), rng.uniform(r.y, r.y + r.height)}, rng.uniform(0.6f, 2.f),
                    Color{255, 255, 255, 30});
}

// "Hoş geldin ya Şehr-i Ramazan": a paper sign with a painted mahya of little lights between two minarets.
void drawRamazanSign(Rectangle r, okey::Rng& rng) {
    rectGradV(r, Color{30, 34, 70, 255}, Color{16, 18, 44, 255});
    frameRect({r.x + 8, r.y + 8, r.width - 16, r.height - 16}, 4.f, Color{214, 176, 90, 255});
    const float cx = r.x + r.width * 0.5f;
    // two minarets and the mahya's string of lights between them
    for (int k = 0; k < 2; ++k) {
        const float x = k ? r.x + r.width - 70.f : r.x + 70.f;
        DrawRectangleRec({x - 9.f, r.y + 70.f, 18.f, r.height - 90.f}, Color{200, 196, 186, 255});
        tri({x - 11.f, r.y + 70.f}, {x + 11.f, r.y + 70.f}, {x, r.y + 30.f}, Color{170, 160, 150, 255});
        DrawRectangleRec({x - 13.f, r.y + 120.f, 26.f, 6.f}, Color{170, 160, 150, 255});
    }
    for (int i = 0; i <= 26; ++i) {
        const float t = (float)i / 26.f;
        const float x = r.x + 70.f + t * (r.width - 140.f), y = r.y + 64.f + 26.f * 4.f * t * (1.f - t);
        DrawCircleV({x, y}, 5.f, Color{255, 220, 120, 255});
        DrawCircleV({x, y}, 10.f, Color{255, 200, 90, 60});
    }
    ui::drawTextCentered(ui::FontId::Hand, "Hoş geldin ya", {cx, r.y + 130.f}, 40.f, Color{255, 232, 170, 255});
    ui::drawTextCentered(ui::FontId::Sign, "ŞEHR-İ RAMAZAN", {cx, r.y + 180.f}, 50.f, Color{255, 244, 214, 255});
    ui::drawTextCentered(ui::FontId::Hand, "İftardan sahura kadar açığız · Güllaç var", {cx, r.y + 240.f}, 26.f,
                         Color{220, 210, 180, 255});
    for (int i = 0; i < 40; ++i)
        DrawCircleV({rng.uniform(r.x + 20.f, r.x + r.width - 20.f), rng.uniform(r.y + 14.f, r.y + 50.f)}, rng.uniform(0.6f, 1.6f),
                    Color{255, 255, 230, 160});
}

// A felt pennant (flama): the team's colour with a stripe and its letters, the hoist edge in the other colour.
void drawPennant(Rectangle r, const Color c[2], const char* name) {
    DrawRectangleRec(r, c[0]);
    DrawRectangleRec({r.x, r.y, r.width * 0.12f, r.height}, c[1]);
    DrawRectangleRec({r.x, r.y + r.height * 0.42f, r.width, r.height * 0.16f}, c[1]);
    ui::drawTextCentered(ui::FontId::Sign, name, {r.x + r.width * 0.36f, r.y + r.height * 0.5f}, 40.f, (c[0].r > 200 && c[0].g > 180) ? c[1] : WHITE);
}

void drawBanner(Rectangle r, Color bg, Color fg, const char* text, Color edge) {
    DrawRectangleRec(r, bg);
    DrawRectangleRec({r.x, r.y, r.width, 10.f}, edge);
    DrawRectangleRec({r.x, r.y + r.height - 10.f, r.width, 10.f}, edge);
    const float w = ui::measureText(ui::FontId::Sign, text, 62.f).x;
    const float size = w > r.width - 70.f ? 62.f * (r.width - 70.f) / w : 62.f;  // (fits the banner)
    ui::drawTextCentered(ui::FontId::Sign, text, {r.x + r.width * 0.5f + 2.f, r.y + r.height * 0.5f + 3.f}, size, Color{0, 0, 0, 90});
    ui::drawTextCentered(ui::FontId::Sign, text, {r.x + r.width * 0.5f, r.y + r.height * 0.5f}, size, fg);
}

void drawSpecialCanvas(RenderTexture2D& cv, uint32_t seed) {
    okey::Rng rng(seed * 13u + 5u);
    beginRoomCanvas(cv);
    drawFlag(spa::FLAG);
    drawDerbyPoster(spa::DERBY, rng);
    drawRamazanSign(spa::RAMAZAN, rng);
    drawPennant(spa::PEN_A, kTeamA, "BGZ");
    drawPennant(spa::PEN_B, kTeamB, "HLÇ");
    drawBanner(spa::BAN_BAYRAM, Color{200, 20, 30, 255}, WHITE, "BAYRAMINIZ MÜBAREK OLSUN", WHITE);
    drawBanner(spa::BAN_RAMAZAN, Color{24, 30, 70, 255}, Color{255, 230, 160, 255}, "HOŞ GELDİN YA ŞEHR-İ RAMAZAN",
               Color{214, 176, 90, 255});
    drawBanner(spa::BAN_DERBY, kTeamA[0], WHITE, "BU AKŞAM DERBİ · BGZ – HLÇ", kTeamB[0]);
    endRoomCanvas(cv);
}

// A piece of cloth hanging on a wall (or from a beam): a grid with gentle vertical folds, canvas UVs.
// c = centre, n = the side it shows (facing the viewer), w x h, src = canvas pixels, fold = fold depth (m).
void cloth(MeshBuilder& b, Vector3 c, Vector3 n, float w, float h, Rectangle src, float fold, float phase) {
    const Vector3 r = wallRight(n), up{0, 1, 0};
    const int nx = 14, ny = 4;
    const float u0 = src.x / SP_W, u1 = (src.x + src.width) / SP_W;
    const float vb = 1.f - (src.y + src.height) / SP_H, vt = 1.f - src.y / SP_H;
    const int base = b.vertexCount();
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            const float s = (float)i / nx, t = (float)j / ny;
            const float d = fold * (0.5f + 0.5f * std::sin(s * 2.f * PI * 2.5f + phase)) * (0.4f + 0.6f * (1.f - t));
            const Vector3 p = Vector3Add(Vector3Add(c, Vector3Scale(r, (s - 0.5f) * w)),
                                         Vector3Add(Vector3Scale(up, (t - 0.5f) * h), Vector3Scale(n, d)));
            const float dn = std::cos(s * 2.f * PI * 2.5f + phase) * 0.35f;
            b.vertex(p, Vector3Normalize(Vector3Add(n, Vector3Scale(r, -dn))), {u0 + (u1 - u0) * s, vb + (vt - vb) * t}, WHITE);
        }
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const int a = base + j * (nx + 1) + i;
            b.quad(a, a + 1, a + nx + 2, a + nx + 1);
        }
}

// A felt pennant on the wall: hoist edge (h tall) at `hoist`, the point `len` along the wall's right.
void pennant(MeshBuilder& b, Vector3 hoist, Vector3 n, float len, float h, Rectangle src, float droop) {
    const Vector3 r = wallRight(n);
    const float u0 = src.x / SP_W, u1 = (src.x + src.width) / SP_W;
    const float vb = 1.f - (src.y + src.height) / SP_H, vt = 1.f - src.y / SP_H;
    const Vector3 off = Vector3Scale(n, 0.006f);
    const Vector3 top = Vector3Add(Vector3Add(hoist, {0, h * 0.5f, 0}), off), bot = Vector3Add(Vector3Add(hoist, {0, -h * 0.5f, 0}), off);
    const Vector3 tip = Vector3Add(Vector3Add(Vector3Add(hoist, Vector3Scale(r, len)), {0, -droop, 0}), Vector3Scale(n, 0.02f));
    const int a = b.vertex(top, n, {u0, vt}), c = b.vertex(bot, n, {u0, vb}), d = b.vertex(tip, n, {u1, (vt + vb) * 0.5f});
    b.triangle(c, d, a);
}

// Bunting: a cord from a to b sagging by s, small red and white pennants every ~0.24 m.
void bunting(MeshBuilder& b, Vector3 a, Vector3 c, float s, okey::Rng& rng) {
    std::vector<Vector3> cord;
    for (int i = 0; i <= 24; ++i) cord.push_back(sag(a, c, (float)i / 24.f, s));
    b.tube(cord, 0.0035f, 4, Color{230, 226, 214, 255});
    const float len = Vector3Distance(a, c);
    const int n = std::max(2, (int)(len / 0.24f));
    for (int i = 1; i < n; ++i) {
        const float t0 = ((float)i - 0.36f) / (float)n, t1 = ((float)i + 0.36f) / (float)n;
        const Vector3 p0 = sag(a, c, t0, s), p1 = sag(a, c, t1, s);
        const Vector3 mid = Vector3Lerp(p0, p1, 0.5f);
        const Vector3 along = Vector3Normalize(Vector3Subtract(p1, p0));
        Vector3 side = Vector3Normalize(Vector3CrossProduct(along, {0, 1, 0}));
        const float twist = rng.uniform(-0.25f, 0.25f);
        const Vector3 tip = Vector3Add(Vector3Add(mid, {0, -0.19f, 0}), Vector3Scale(side, twist * 0.12f));
        const Color col = i % 2 ? kRed : kWhite;
        const Vector3 nn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(tip, p0)));
        const int ia = b.vertex(p0, nn, {0, 0}, col), ib = b.vertex(p1, nn, {1, 0}, col), ic = b.vertex(tip, nn, {0.5f, 1}, scaleRgb(col, 0.92f));
        b.triangle(ia, ib, ic);
    }
}

// A plate (tabak) on a table at p: lokum and akide şekeri (bayram) or güllaç / baklava (Ramazan).
void plate(MeshBuilder& b, Vector3 p, float r) {
    std::vector<Vector2> prof{{0.f, 0.f}, {r * 0.55f, 0.f}, {r * 0.6f, 0.004f}, {r * 0.92f, 0.008f}, {r, 0.014f},
                              {r * 0.96f, 0.015f}, {r * 0.86f, 0.01f}, {0.f, 0.007f}};
    latheAt(b, p, prof, 24, false, false, Color{246, 244, 236, 255});
    ringTube(b, {p.x, p.y + 0.0135f, p.z}, r * 0.93f, 0.0018f, 24, Color{70, 110, 170, 255});  // a blue rim line
}

void lokumPlate(MeshBuilder& b, Vector3 p, okey::Rng& rng) {
    plate(b, p, 0.075f);
    const Color pink{236, 160, 172, 255}, white{246, 240, 226, 255}, powder{250, 248, 244, 255};
    for (int i = 0; i < 7; ++i) {
        const float a = (float)i * 2.f * PI / 7.f + rng.uniform(-0.2f, 0.2f), rr = i == 6 ? 0.f : rng.uniform(0.022f, 0.03f);
        const Vector3 c{p.x + std::cos(a) * rr, p.y + 0.019f + (i == 6 ? 0.02f : 0.f), p.z + std::sin(a) * rr};
        b.setTransform(MatrixMultiply(MatrixRotateY(rng.uniform(0.f, PI)), MatrixTranslate(c.x, c.y, c.z)));
        b.roundedBox({0, 0, 0}, {0.022f, 0.02f, 0.022f}, 0.005f, 1, i % 3 == 0 ? white : pink);
        b.roundedBox({0, 0.0101f, 0}, {0.018f, 0.001f, 0.018f}, 0.0005f, 1, powder);  // the dusting of sugar
        b.resetTransform();
    }
    // a few wrapped akide candies beside the plate
    const Color wraps[4] = {{220, 60, 50, 255}, {60, 140, 90, 255}, {240, 190, 50, 255}, {120, 80, 170, 255}};
    for (int i = 0; i < 3; ++i) {
        const float a = rng.uniform(0.f, 2.f * PI);
        const Vector3 c{p.x + std::cos(a) * 0.105f, p.y + 0.008f, p.z + std::sin(a) * 0.105f};
        const Color w = wraps[rng.range(0, 3)];
        b.setTransform(MatrixMultiply(MatrixRotateY(rng.uniform(0.f, PI)), MatrixTranslate(c.x, c.y, c.z)));
        b.ellipsoid({0, 0, 0}, {0.014f, 0.008f, 0.009f}, 4, 8, w);
        b.ellipsoid({0.019f, 0, 0}, {0.007f, 0.005f, 0.008f}, 3, 6, scaleRgb(w, 1.15f));
        b.ellipsoid({-0.019f, 0, 0}, {0.007f, 0.005f, 0.008f}, 3, 6, scaleRgb(w, 1.15f));
        b.resetTransform();
    }
}

void gullacPlate(MeshBuilder& b, Vector3 p, bool baklava, okey::Rng& rng) {
    plate(b, p, 0.07f);
    if (!baklava) {  // two squares of güllaç: thin cream layers, pomegranate seeds and crushed walnut on top
        for (int k = 0; k < 2; ++k) {
            const Vector3 c{p.x + (k ? 0.022f : -0.022f), p.y + 0.021f, p.z + (k ? -0.01f : 0.012f)};
            b.setTransform(MatrixMultiply(MatrixRotateY(rng.uniform(-0.3f, 0.3f)), MatrixTranslate(c.x, c.y, c.z)));
            b.roundedBox({0, 0, 0}, {0.042f, 0.022f, 0.04f}, 0.003f, 1, Color{246, 238, 214, 255});
            for (int l = 0; l < 3; ++l) b.box({0, -0.006f + 0.0055f * l, 0.0202f}, {0.04f, 0.0012f, 0.0006f}, Color{226, 214, 186, 255});
            for (int s = 0; s < 6; ++s)
                b.sphere({rng.uniform(-0.016f, 0.016f), 0.0115f, rng.uniform(-0.015f, 0.015f)}, 0.0028f, 3, 5, Color{196, 20, 40, 255});
            for (int s = 0; s < 4; ++s)
                b.sphere({rng.uniform(-0.015f, 0.015f), 0.0112f, rng.uniform(-0.014f, 0.014f)}, 0.0024f, 2, 4, Color{150, 110, 70, 255});
            b.resetTransform();
        }
    } else {  // three diamonds of baklava, golden with green pistachio on top
        for (int k = 0; k < 3; ++k) {
            const float a = (float)k * 2.f * PI / 3.f;
            const Vector3 c{p.x + std::cos(a) * 0.022f, p.y + 0.017f, p.z + std::sin(a) * 0.022f};
            b.setTransform(MatrixMultiply(MatrixMultiply(MatrixScale(1.f, 1.f, 0.6f), MatrixRotateY(PI * 0.25f + a)), MatrixTranslate(c.x, c.y, c.z)));
            b.roundedBox({0, 0, 0}, {0.03f, 0.016f, 0.03f}, 0.002f, 1, Color{206, 146, 60, 255});
            b.roundedBox({0, 0.0085f, 0}, {0.02f, 0.002f, 0.02f}, 0.001f, 1, Color{110, 150, 60, 255});
            b.resetTransform();
        }
    }
}

// The bg tables' plates (their free corner; the okey table is too busy), as offsets from the table centres.
struct PlateAt {
    int table;
    float dx, dz;
};
const PlateAt kPlates[3] = {{0, -0.32f, 0.31f}, {2, -0.30f, 0.30f}, {3, 0.28f, 0.28f}};
float tableTop(int t) { return w3d::BG_TABLE_Y + ((w3d::BG_TABLES[t].kind == 1 || w3d::BG_TABLES[t].kind == 2) ? 0.003f : 0.f); }

}  // namespace

struct Special {
    RenderTexture2D cv{};
    Mat mDecor, mCanvas, mTvCase, mLit, mScarf;  // mScarf: a touch of emissive (it hangs in the TV's shadow)  // mLit: the Ramazan sign's painted mahya glows a little
    Mesh buntIn{}, buntOut{};              // bayram: bunting inside / in the garden
    Mesh flagIn{}, ramazanIn{}, derbyIn{};  // the slot's three faces (canvas)
    Mesh flagOut{}, ramazanOut{}, derbyOut{};
    Mesh rod{};                            // the brass rod the slot hangs from (vertex colours)
    Mesh sweets{}, gullac{};               // plates on the tables and the counter
    Mesh fansIn{};                         // the derby: the scarf over the TV (vertex colours)
    Mesh pennants{};                       // the derby: pennants beside the TV (canvas)
    Mesh tvSet{}, tvScreen{};              // the derby in the garden: the portable TV and its stand
    // the passing shower
    bool planned = false;
    float t = 0.f, at = -1.f, dur = 0.f, strength = 0.f;
};

}  // namespace rm

using namespace rm;

// ============================================================================ init / free
void Room::Impl::initSpecial() {
    if (sp) return;
    sp = new Special;
    Special& S = *sp;
    Renderer& r = *R;
    okey::Rng rng(seed * 2654435761u + 0x0de1u);
    S.cv = makeCanvas(SP_W, SP_H);
    drawSpecialCanvas(S.cv, seed);
    S.mDecor = r.makeMat(WHITE, Texture2D{}, 0.12f, 16.f, 0.f, 0.2f);
    S.mCanvas = r.makeMat(WHITE, S.cv.texture, 0.05f, 8.f, 0.f, 0.2f);
    S.mTvCase = r.makeMat(WHITE, Texture2D{}, 0.3f, 30.f, 0.f, 0.1f);
    S.mLit = r.makeMat(WHITE, S.cv.texture, 0.05f, 8.f, 0.45f, 0.2f);
    S.mScarf = r.makeMat(WHITE, Texture2D{}, 0.04f, 8.f, 0.3f, 0.3f);
    rainWas = rainTarget > 0.f;  // (a rainy night from the start is not "the rain began")
    const Vector3 back{0, 0, 1};

    // ---- bayram: bunting under the ceiling; in the garden under the trellis
    {
        MeshBuilder b;
        bunting(b, {w3d::ROOM_X0 + 0.05f, 2.88f, w3d::ROOM_Z0 + 0.05f}, {w3d::ROOM_X1 - 0.05f, 2.88f, w3d::ROOM_Z1 - 0.05f}, 0.3f, rng);
        bunting(b, {w3d::ROOM_X0 + 0.05f, 2.88f, w3d::ROOM_Z1 - 0.05f}, {w3d::ROOM_X1 - 0.05f, 2.88f, w3d::ROOM_Z0 + 0.05f}, 0.3f, rng);
        bunting(b, {w3d::ROOM_X0 + 0.02f, 2.95f, -3.15f}, {w3d::ROOM_X1 - 0.02f, 2.95f, -3.15f}, 0.12f, rng);  // along the back wall
        bunting(b, {w3d::ROOM_X0 + 0.02f, 2.92f, 0.9f}, {w3d::ROOM_X1 - 0.02f, 2.92f, 0.9f}, 0.22f, rng);
        S.buntIn = b.build(true);
        MeshBuilder g;
        bunting(g, {w3d::ROOM_X0, 2.78f, -0.4f}, {w3d::ROOM_X1 - 0.1f, 2.78f, -0.6f}, 0.2f, rng);
        bunting(g, {w3d::ROOM_X0, 2.78f, 2.175f}, {w3d::ROOM_X1 - 0.1f, 2.78f, 1.6f}, 0.2f, rng);
        bunting(g, {0.45f, 2.78f, -3.3f}, {1.05f, 2.78f, w3d::ROOM_Z1 - 0.1f}, 0.25f, rng);
        bunting(g, {w3d::ROOM_X0, 2.78f, -3.3f}, {0.45f, 2.78f, -3.3f}, 0.15f, rng);
        S.buntOut = g.build(true);
    }
    // ---- the back-wall slot inside (over Kel Mahmut's head): the flag, the Ramazan sign, the derby poster; a brass rod
    {
        const Vector3 c{kSlot.x, kSlot.y, kSlot.z + 0.03f};
        MeshBuilder f, ra, d, rod;
        cloth(f, c, back, 0.66f, 0.44f, spa::FLAG, 0.022f, 0.3f);
        cloth(ra, {c.x, c.y - 0.02f, c.z - 0.015f}, back, 0.66f, 0.33f, spa::RAMAZAN, 0.004f, 0.f);
        cloth(d, {c.x + 0.27f, c.y - 0.06f, c.z - 0.015f}, back, 0.4f, 0.56f, spa::DERBY, 0.004f, 1.1f);  // (right of his head)
        S.flagIn = f.build();
        S.ramazanIn = ra.build();
        S.derbyIn = d.build();
        rod.capsule({c.x - 0.36f, c.y + 0.235f, c.z + 0.01f}, {c.x + 0.36f, c.y + 0.235f, c.z + 0.01f}, 0.006f, 8, Color{200, 160, 84, 255});
        for (int k = 0; k < 2; ++k)
            rod.sphere({c.x + (k ? 0.37f : -0.37f), c.y + 0.235f, c.z + 0.01f}, 0.011f, 5, 8, Color{210, 170, 90, 255});
        S.rod = rod.build(true);
    }
    // ---- the garden: a banner under the ocak's lean-to roof and the flag on the kahvehane's front
    {
        const Vector3 bc{2.25f, 2.27f, -2.38f};  // under the roof's front beam, facing the garden
        MeshBuilder f, ra, d;
        cloth(f, bc, back, 2.2f, 0.275f, spa::BAN_BAYRAM, 0.012f, 0.f);
        cloth(f, {-2.9f, 2.5f, -3.27f}, back, 0.9f, 0.6f, spa::FLAG, 0.03f, 0.7f);  // from the trellis' back beam, over the parapet
        cloth(ra, bc, back, 2.2f, 0.275f, spa::BAN_RAMAZAN, 0.012f, 0.f);
        cloth(d, bc, back, 2.2f, 0.275f, spa::BAN_DERBY, 0.012f, 0.f);
        S.flagOut = f.build();
        S.ramazanOut = ra.build();
        S.derbyOut = d.build();
    }
    // ---- plates: lokum / güllaç on the bg tables, a bowl of candy / a tray of güllaç on the counter
    {
        MeshBuilder sw, gu;
        for (const PlateAt& pa : kPlates) {
            const w3d::BgTable& t = w3d::BG_TABLES[pa.table];
            const Vector3 p{t.x + pa.dx, tableTop(pa.table), t.z + pa.dz};
            lokumPlate(sw, p, rng);
            gullacPlate(gu, p, pa.table == 3, rng);
        }
        // the counter's left end, by the gas rings: a brass bowl of bayram candy / a round tray of güllaç
        const Vector3 cp{2.16f, 1.04f, -3.17f};
        latheAt(sw, cp, {{0.f, 0.f}, {0.03f, 0.f}, {0.03f, 0.03f}, {0.05f, 0.04f}, {0.085f, 0.075f}, {0.09f, 0.085f}, {0.f, 0.03f}}, 24,
                false, false, Color{214, 168, 84, 255});
        const Color wraps[5] = {{220, 60, 50, 255}, {60, 140, 90, 255}, {240, 190, 50, 255}, {120, 80, 170, 255}, {240, 240, 236, 255}};
        for (int i = 0; i < 16; ++i) {
            const float a = rng.uniform(0.f, 2.f * PI), rr = rng.uniform(0.f, 0.06f);
            const Vector3 c{cp.x + std::cos(a) * rr, cp.y + 0.07f + (0.06f - rr) * 0.35f + rng.uniform(0.f, 0.01f), cp.z + std::sin(a) * rr};
            const Color w = wraps[rng.range(0, 4)];
            sw.setTransform(MatrixMultiply(MatrixMultiply(MatrixRotateX(rng.uniform(-0.4f, 0.4f)), MatrixRotateY(rng.uniform(0.f, PI))),
                                           MatrixTranslate(c.x, c.y, c.z)));
            sw.ellipsoid({0, 0, 0}, {0.014f, 0.008f, 0.009f}, 4, 8, w);
            sw.ellipsoid({0.019f, 0, 0}, {0.007f, 0.005f, 0.008f}, 3, 6, scaleRgb(w, 1.15f));
            sw.ellipsoid({-0.019f, 0, 0}, {0.007f, 0.005f, 0.008f}, 3, 6, scaleRgb(w, 1.15f));
            sw.resetTransform();
        }
        // güllaç tray (tepsi) with a grid of squares
        gu.cylinder(cp, 0.13f, 0.006f, 28, Color{196, 196, 192, 255});
        ringTube(gu, {cp.x, cp.y + 0.012f, cp.z}, 0.13f, 0.006f, 28, Color{180, 180, 176, 255});
        for (int i = -2; i <= 1; ++i)
            for (int j = -2; j <= 1; ++j) {
                const Vector3 c{cp.x + (i + 0.5f) * 0.05f, cp.y + 0.018f, cp.z + (j + 0.5f) * 0.05f};
                if (Vector2Length({c.x - cp.x, c.z - cp.z}) > 0.1f) continue;
                gu.roundedBox(c, {0.046f, 0.022f, 0.046f}, 0.002f, 1, Color{246, 238, 214, 255});
                for (int s = 0; s < 3; ++s)
                    gu.sphere({c.x + rng.uniform(-0.016f, 0.016f), c.y + 0.0115f, c.z + rng.uniform(-0.016f, 0.016f)}, 0.0028f, 3, 5,
                              Color{196, 20, 40, 255});
            }
        S.sweets = sw.build(true);
        S.gullac = gu.build(true);
    }
    // ---- the derby inside: a red and white scarf tied to the TV's lower corners, sagging under the set with its ends
    //      hanging down (facing the room), the teams' pennants on the walls beside it
    {
        MeshBuilder b;
        b.setTransform(tvXf);
        // TV-local: the case is a 0.6 x 0.46 x 0.36 rounded box, its front at z = +0.18; the scarf hangs just in front
        const float z = 0.2f;
        std::vector<Vector3> path;
        path.push_back({-0.32f, -0.52f, z});  // the left end, hanging
        path.push_back({-0.315f, -0.36f, z});
        path.push_back({-0.3f, -0.21f, z});   // tied at the corner
        for (int k = 1; k < 8; ++k) {         // the sag under the set
            const float t = (float)k / 8.f;
            path.push_back({-0.3f + 0.6f * t, -0.21f - 0.13f * 4.f * t * (1.f - t), z + 0.01f});
        }
        path.push_back({0.3f, -0.21f, z});
        path.push_back({0.315f, -0.36f, z});
        path.push_back({0.32f, -0.52f, z});   // the right end
        float along = 0.f;
        int prevA = -1, prevB = -1;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) along += Vector3Distance(path[i], path[i - 1]);
            const Vector3 d = Vector3Normalize(Vector3Subtract(path[std::min(i + 1, path.size() - 1)], path[i > 0 ? i - 1 : 0]));
            const Vector3 w = Vector3Scale(Vector3Normalize(Vector3CrossProduct(d, {0, 0, 1})), 0.045f);  // across, in the front plane
            const Color c = ((int)(along / 0.08f)) % 2 ? kTeamA[1] : kTeamA[0];
            const int a = b.vertex(Vector3Subtract(path[i], w), {0, 0, 1}, {0, 0}, c), bb = b.vertex(Vector3Add(path[i], w), {0, 0, 1}, {1, 0}, c);
            if (prevA >= 0) b.quad(prevA, a, bb, prevB);
            prevA = a, prevB = bb;
        }
        for (int e = 0; e < 2; ++e)  // fringes
            for (int k = 0; k < 6; ++k) {
                const Vector3 end = e ? path.back() : path.front();
                const Vector3 p0{end.x - 0.04f + k * 0.016f, end.y, end.z};
                b.capsule(p0, Vector3Add(p0, {0, -0.035f, 0}), 0.0025f, 4, kTeamA[1]);
            }
        b.resetTransform();
        S.fansIn = b.build(true);
        MeshBuilder pn;
        pennant(pn, wallPoint(2, w3d::ROOM_Z1 - (-1.95f), 2.42f), {1, 0, 0}, 0.46f, 0.26f, spa::PEN_A, 0.03f);
        pennant(pn, wallPoint(0, -3.05f - w3d::ROOM_X0, 2.6f), {0, 0, 1}, 0.46f, 0.26f, spa::PEN_B, 0.03f);
        S.pennants = pn.build();
    }
    // ---- the derby in the garden: the old portable TV on a little stand beside the ocak, turned toward the tables
    {
        const Vector3 base{1.42f, 0.f, -2.95f};
        const float yaw = std::atan2(0.2f - base.x, 0.3f - base.z) * RAD2DEG;
        const Matrix M = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(base.x, base.y, base.z));
        MeshBuilder b;
        b.setTransform(M);
        const Color wood{118, 80, 50, 255}, caseC{70, 62, 58, 255};
        b.roundedBox({0, 0.86f, 0}, {0.56f, 0.03f, 0.44f}, 0.01f, 2, wood);  // the stand's top
        for (int k = 0; k < 4; ++k) b.roundedBox({(k % 2 ? 0.23f : -0.23f), 0.43f, (k < 2 ? 0.17f : -0.17f)}, {0.035f, 0.86f, 0.035f}, 0.006f, 1, wood);
        b.roundedBox({0, 0.22f, 0}, {0.5f, 0.02f, 0.38f}, 0.006f, 1, scaleRgb(wood, 0.9f));
        b.roundedBox({0, 1.085f, -0.02f}, {0.48f, 0.42f, 0.36f}, 0.035f, 3, caseC);           // the set
        b.roundedBox({0, 1.085f, -0.24f}, {0.32f, 0.28f, 0.14f}, 0.05f, 2, scaleRgb(caseC, 0.9f));
        b.roundedBox({-0.05f, 1.085f, 0.16f}, {0.36f, 0.3f, 0.02f}, 0.02f, 2, Color{26, 24, 24, 255});
        b.capsule({0.1f, 1.29f, -0.05f}, {0.26f, 1.62f, -0.1f}, 0.004f, 4, Color{180, 180, 180, 255});  // the rabbit ears
        b.capsule({-0.06f, 1.29f, -0.05f}, {-0.2f, 1.6f, -0.12f}, 0.004f, 4, Color{180, 180, 180, 255});
        b.sphere({0.02f, 1.3f, -0.05f}, 0.03f, 5, 8, Color{40, 40, 40, 255});
        b.capsule({0.15f, 1.0f, 0.165f}, {0.15f, 1.0f, 0.18f}, 0.012f, 6, Color{180, 180, 176, 255});  // knobs
        b.capsule({0.15f, 1.07f, 0.165f}, {0.15f, 1.07f, 0.18f}, 0.012f, 6, Color{180, 180, 176, 255});
        // the cable down to the ground and off toward the kahvehane's door
        b.resetTransform();
        b.tube({Vector3Transform({0.f, 0.95f, -0.2f}, M), Vector3Transform({0.05f, 0.5f, -0.25f}, M), Vector3Transform({0.1f, 0.01f, -0.3f}, M),
                {2.0f, 0.01f, -2.5f}, {3.5f, 0.01f, -2.3f}, {4.15f, 0.2f, -2.2f}},
               0.006f, 4, Color{30, 30, 30, 255});
        S.tvSet = b.build(true);
        MeshBuilder s;
        s.setTransform(M);
        // the screen: the room's TV canvas (Room::Impl::cvTv, mTv), a slightly curved quad
        const int nx = 6, ny = 5;
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const float u = (float)i / nx, v = (float)j / ny;
                const float x = -0.05f + (u - 0.5f) * 0.3f, y = 1.085f + (v - 0.5f) * 0.235f;
                const float bulge = 0.012f * (1.f - std::pow(2.f * u - 1.f, 2.f)) * (1.f - std::pow(2.f * v - 1.f, 2.f));
                s.vertex({x, y, 0.172f + bulge}, {0, 0, 1}, {u, v}, WHITE);
            }
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const int a = j * (nx + 1) + i;
                s.quad(a, a + 1, a + nx + 2, a + nx + 1);
            }
        S.tvScreen = s.build();
    }
}

void Room::Impl::freeSpecial(Renderer& r) {
    if (!sp) return;
    Special& S = *sp;
    for (Mesh* m : {&S.buntIn, &S.buntOut, &S.flagIn, &S.ramazanIn, &S.derbyIn, &S.flagOut, &S.ramazanOut,
                    &S.derbyOut, &S.rod, &S.sweets, &S.gullac, &S.fansIn, &S.pennants, &S.tvSet, &S.tvScreen})
        if (m->vertexCount > 0) {
            UnloadMesh(*m);
            *m = Mesh{};
        }
    for (Mat* m : {&S.mDecor, &S.mCanvas, &S.mTvCase, &S.mLit, &S.mScarf}) r.unloadMat(*m);
    if (S.cv.id) unloadCanvas(S.cv);
    delete sp;
    sp = nullptr;
}

// ============================================================================ per frame
bool Room::Impl::specialTvOut() const { return garden && specialDay == w3d::GunMac; }

// A passing shower on a fair spring / summer day (the garden's season): planned once from the seed. Developer override
// SAKLI_YAGMUR=<seconds>: it starts that many seconds after the room came up and does not stop.
void Room::Impl::updateShower(float dt) {
    if (!sp) return;
    Special& S = *sp;
    if (!S.planned) {
        S.planned = true;
        okey::Rng r(seed * 7477u + 0x5a6u);
        if (const char* e = std::getenv("SAKLI_YAGMUR")) {
            S.at = (float)std::max(0, std::atoi(e));
            S.dur = 1e9f;
            S.strength = 0.75f;
        } else if ((seasonNow == w3d::Ilkbahar || seasonNow == w3d::Yaz) && r.chance(seasonNow == w3d::Ilkbahar ? 0.35f : 0.2f)) {
            S.at = r.uniform(360.f, 1500.f);  // some minutes into the session
            S.dur = r.uniform(300.f, 720.f);
            S.strength = r.uniform(0.45f, 0.85f);
        }
    }
    S.t += dt;
    const bool on = S.at >= 0.f && S.t >= S.at && S.t < S.at + S.dur && seasonNow != w3d::Kis;
    rainTarget = std::max(phaseRain, on ? S.strength : 0.f);
    const bool wet = rainTarget > 0.f;
    if (wet && !rainWas) rainStarted = true;
    rainWas = wet;
}

void Room::Impl::updateSpecial(float dt) {
    (void)dt;
    tv.derby = specialDay == w3d::GunMac;
}

void Room::Impl::submitSpecial(Renderer& r) {
    if (!sp) return;
    Special& S = *sp;
    const int d = specialDay;
    if (d == w3d::GunNormal) return;
    const bool bayram = w3d::isBayram(d), ramazan = w3d::ramazanEvening(d, phase), mac = d == w3d::GunMac;
    if (!bayram && !ramazan && !mac) return;  // (Ramazan by day: nothing to see yet)
    if (!garden) {
        if (bayram) {
            r.submit(&S.buntIn, &S.mDecor, MatrixIdentity(), DoubleSided);
            r.submit(&S.flagIn, &S.mCanvas, MatrixIdentity(), DoubleSided);
        } else if (ramazan) {
            r.submit(&S.ramazanIn, &S.mLit, MatrixIdentity(), 0);
        } else {
            r.submit(&S.derbyIn, &S.mCanvas, MatrixIdentity(), 0);
            r.submit(&S.fansIn, &S.mScarf, MatrixIdentity(), DoubleSided);
            r.submit(&S.pennants, &S.mCanvas, MatrixIdentity(), DoubleSided);
        }
        if (bayram || ramazan) r.submit(&S.rod, &S.mDecor, MatrixIdentity(), 0);
    } else {
        if (bayram) {
            r.submit(&S.buntOut, &S.mDecor, MatrixIdentity(), DoubleSided);
            r.submit(&S.flagOut, &S.mCanvas, MatrixIdentity(), DoubleSided);
        } else if (ramazan) {
            r.submit(&S.ramazanOut, &S.mLit, MatrixIdentity(), DoubleSided);
        } else {
            r.submit(&S.derbyOut, &S.mCanvas, MatrixIdentity(), DoubleSided);
            r.submit(&S.tvSet, &S.mTvCase, MatrixIdentity(), CastShadow);
            r.submit(&S.tvScreen, &mTv, MatrixIdentity(), 0);
            r.submitGlow(Vector3{1.42f, 1.09f, -2.75f}, 0.35f, Color{150, 190, 255, 255}, 0.18f * (0.6f + 0.4f * tvLight));
        }
    }
    if (bayram) r.submit(&S.sweets, &S.mDecor, MatrixIdentity(), CastShadow);
    else if (ramazan) r.submit(&S.gullac, &S.mDecor, MatrixIdentity(), CastShadow);
}

// ============================================================================ public API
void Room::setSpecialDay(int day) { impl_->specialDay = std::clamp(day, 0, 4); }
void Room::setVenueHold(bool hold) { impl_->venueHold = hold; }
int Room::venuePending() const { return impl_->ready ? impl_->venuePend : 0; }
void Room::applyVenue() {
    Impl& I = *impl_;
    if (I.ready) I.evalVenue(true);
}
float Room::awningAmount() const {
    const Impl& I = *impl_;
    return I.ready && I.garden && I.gd ? I.gd->awningK : 0.f;
}
bool Room::consumeRainStart() {
    Impl& I = *impl_;
    const bool s = I.rainStarted;
    I.rainStarted = false;
    return s;
}

}  // namespace r3d
