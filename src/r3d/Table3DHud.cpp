// Table3D HUD (2D, ui virtual coordinates): name plates over the opponents' heads, pile count and okey
// preview, istaka hints (işlek marks, group values), status bar with the live meld counter, buttons,
// toasts, drag hints and the confirm modal. Kept light: the 3D table is the star.
#include "r3d/Table3DInternal.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace t3d;
using ui::FontId;
namespace pal = ui::pal;

namespace {

Color rgba(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}
Color fadeC(Color c, float a) {
    c.a = (unsigned char)std::clamp(c.a * a + 0.5f, 0.f, 255.f);
    return c;
}
// Largest size <= `size` (down to `minSize`) at which `text` fits `maxW`, else shortened with "…".
std::string fitText(FontId f, std::string text, float maxW, float& size, float minSize) {
    while (size > minSize && ui::measureText(f, text, size).x > maxW) size -= 1.f;
    if (ui::measureText(f, text, size).x <= maxW) return text;
    while (!text.empty() && ui::measureText(f, text + "…", size).x > maxW) {
        size_t cut = text.size() - 1;
        while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) --cut;
        text.erase(cut);
    }
    return text + "…";
}
void pill(Rectangle r, Color fill, Color line) {
    ui::tilegfx::roundedRect({r.x + 1, r.y + 2, r.width, r.height}, r.height * 0.5f, rgba(0, 0, 0, 70));
    ui::tilegfx::roundedRect(r, r.height * 0.5f, fill);
    if (line.a) ui::tilegfx::roundedLines(r, r.height * 0.5f, 1.2f, line);
}

constexpr float BTN_W = 124.f, BTN_H = 46.f, BTN_GAP = 9.f, EDGE = 18.f;
constexpr float STATUS_Y = 877.f;
// Newest toast just above the status line, over the istaka's front board (older ones stack upward): never
// over an opponent's face or the table centre, and right where the player is already looking.
constexpr float TOAST_Y = STATUS_Y - 36.f;

// Code points of a UTF-8 name (stray bytes pass through as they are).
std::vector<unsigned> codePoints(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (i + (size_t)extra >= s.size()) extra = 0;
        unsigned cp = extra == 3 ? (c & 0x07u) : extra == 2 ? (c & 0x0Fu) : extra == 1 ? (c & 0x1Fu) : c;
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3Fu);
        out.push_back(cp);
        i += 1 + (size_t)extra;
    }
    return out;
}
// The last vowel of a name: 0 none, else one of a ı o u e i ö ü (as 'a','I','o','u','e','i','O','U').
char lastVowel(const std::vector<unsigned>& cps) {
    char v = 0;
    for (unsigned c : cps) {
        switch (c) {
        case 'a': case 'A': v = 'a'; break;
        case 0x131: case 'I': v = 'I'; break; // ı I
        case 'o': case 'O': v = 'o'; break;
        case 'u': case 'U': v = 'u'; break;
        case 'e': case 'E': v = 'e'; break;
        case 'i': case 0x130: v = 'i'; break; // i İ
        case 0xF6: case 0xD6: v = 'O'; break; // ö Ö
        case 0xFC: case 0xDC: v = 'U'; break; // ü Ü
        default: break;
        }
    }
    return v;
}
bool isVowelCp(unsigned c) {
    switch (c) {
    case 'a': case 'A': case 0x131: case 'I': case 'o': case 'O': case 'u': case 'U': case 'e': case 'E':
    case 'i': case 0x130: case 0xF6: case 0xD6: case 0xFC: case 0xDC: return true;
    default: return false;
    }
}

} // namespace

namespace t3d {
std::string nameLocative(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    const char v = lastVowel(cps);
    const bool back = v == 0 || v == 'a' || v == 'I' || v == 'o' || v == 'u';
    bool hard = false; // f s t k ç ş h p: "Mahmut'ta"
    if (!cps.empty()) {
        switch (cps.back()) {
        case 'f': case 's': case 't': case 'k': case 'h': case 'p': case 'F': case 'S': case 'T': case 'K':
        case 'H': case 'P': case 0xE7: case 0xC7: case 0x15F: case 0x15E: hard = true; break;
        default: break;
        }
    }
    return name + "'" + (hard ? "t" : "d") + (back ? "a" : "e");
}

std::string nameGenitive(const std::string& name) {
    const std::vector<unsigned> cps = codePoints(name);
    const char v = lastVowel(cps);
    const char* vowel = "ın"; // four-way harmony: a ı -> ın, e i -> in, o u -> un, ö ü -> ün
    if (v == 'e' || v == 'i') vowel = "in";
    else if (v == 'o' || v == 'u') vowel = "un";
    else if (v == 'O' || v == 'U') vowel = "ün";
    const bool endsInVowel = !cps.empty() && isVowelCp(cps.back());
    return name + "'" + (endsInVowel ? "n" : "") + vowel;
}
} // namespace t3d

std::array<Rectangle, NUM_BUTTONS> TableState::buttonRects() const {
    std::array<Rectangle, NUM_BUTTONS> r{};
    const float x = ui::VW - EDGE - BTN_W;
    const float y0 = ui::VH - EDGE - (float)NUM_BUTTONS * BTN_H - (float)(NUM_BUTTONS - 1) * BTN_GAP;
    for (int i = 0; i < NUM_BUTTONS; ++i) r[i] = {x, y0 + (float)i * (BTN_H + BTN_GAP), BTN_W, BTN_H};
    return r;
}

bool TableState::overHud(Vector2 m) const {
    if (confirm.active) return true;
    if (!started()) return false;
    for (const Rectangle& r : buttonRects())
        if (ui::pointInRect(m, {r.x - 3, r.y - 3, r.width + 6, r.height + 6})) return true;
    return false;
}

// ---------------------------------------------------------------- name plates
// A plate floats beside each opponent's head at eye level, on the side facing the middle of the screen
// (right of a centred head: the wall scoreboard hangs to its left). Speech bubbles (Characters) always sit
// above the head, so the two never collide.
void TableState::drawNameplates(const Renderer& r) {
    const Camera3D& view = r.lastCamera();
    const Vector3 fwd = Vector3Normalize(Vector3Subtract(view.target, view.position));
    const Vector3 camRight = Vector3Normalize(Vector3CrossProduct(fwd, view.up));
    for (int seat = 0; seat < 4; ++seat) {
        if (seat == human) continue;
        const Vector3 head = headsSet ? heads[seat] : w3d::seatLocal(seat, 0.f, w3d::SEAT_DIST - 0.04f, w3d::HEAD_Y + 0.02f);
        Vector2 hc, he;
        if (Vector3DotProduct(Vector3Subtract(head, view.position), fwd) < 0.05f) continue; // behind the viewer
        if (!r.projectToVirtual(head, hc) || !r.projectToVirtual(Vector3Add(head, Vector3Scale(camRight, 0.1f)), he)) continue;
        const float rad = std::clamp(std::fabs(he.x - hc.x), 16.f, 140.f);
        const okey::PlayerInfo& p = game->player(seat);
        const bool turn = playing() && game->current() == seat && !dealing();
        float ns = 17.f;
        const std::string name = fitText(FontId::UiBold, p.name, 150.f, ns, 12.f);
        // the match total, labelled so it is never read as the opening value
        const std::string totLbl = "Toplam ", tot = std::to_string(p.totalScore);
        std::vector<std::pair<std::string, Color>> badges;
        if (p.opened) {
            if (p.openedWithPairs) badges.push_back({"Açtı: " + std::to_string(p.openValue) + " çift", rgba(64, 112, 196)});
            else badges.push_back({"Açtı: " + std::to_string(p.openValue), rgba(52, 124, 62)});
        }
        if (p.handPenalty > 0) badges.push_back({"+" + std::to_string(p.handPenalty), rgba(168, 42, 34)});
        const float nameW = ui::measureText(FontId::UiBold, name, ns).x;
        const float totLblW = ui::measureText(FontId::Ui, totLbl, 12.f).x;
        const float totW = totLblW + ui::measureText(FontId::UiBold, tot, 15.f).x;
        float bw = 0.f;
        for (auto& b : badges) bw += ui::measureText(FontId::UiBold, b.first, 12.f).x + 14.f + 4.f;
        const float w = std::max({nameW + totW + 34.f, bw + 12.f, 96.f});
        const float h = badges.empty() ? 28.f : 46.f;
        const float side = hc.x > ui::VW * 0.5f + 150.f ? -1.f : 1.f; // a centred head: right (the scoreboard is left)
        Vector2 c{hc.x + side * (rad + 14.f + w * 0.5f), hc.y - rad * 0.15f};
        c.x = std::clamp(c.x, w * 0.5f + 8.f, ui::VW - w * 0.5f - 8.f);
        c.y = std::clamp(c.y, h * 0.5f + 8.f, ui::VH - 220.f);
        const Rectangle rc{std::round(c.x - w * 0.5f), std::round(c.y - h * 0.5f), std::round(w), h};
        if (turn) {
            const float a = 0.45f + 0.25f * std::sin(now * 3.2f);
            ui::tilegfx::drawSoftBox({c.x, c.y}, w + 14, h + 16, 0.f, fadeC(pal::Highlight, a));
        }
        ui::tilegfx::roundedRect({rc.x + 1, rc.y + 3, rc.width, rc.height}, 10, rgba(0, 0, 0, 90));
        ui::tilegfx::roundedGradV(rc, 10, rgba(58, 36, 20, 228), rgba(30, 18, 10, 228));
        ui::tilegfx::roundedLines(rc, 10, turn ? 2.f : 1.2f, turn ? pal::Highlight : fadeC(pal::Brass, 0.55f));
        // name left, total right
        const float ty = rc.y + 14.f;
        ui::drawText(FontId::UiBold, name, {rc.x + 10.f, ty - ns * 0.56f}, ns, turn ? pal::Highlight : pal::TextLight);
        ui::drawText(FontId::Ui, totLbl, {rc.x + rc.width - 10.f - totW, ty - 12.f * 0.56f + 1.f}, 12.f, fadeC(pal::Brass, 0.7f));
        ui::drawText(FontId::UiBold, tot, {rc.x + rc.width - 10.f - totW + totLblW, ty - 15.f * 0.56f}, 15.f, pal::Brass);
        if (!badges.empty()) {
            float x = c.x - (bw - 4.f) * 0.5f;
            for (auto& b : badges) {
                const float bwi = ui::measureText(FontId::UiBold, b.first, 12.f).x + 14.f;
                const Rectangle br{x, rc.y + 26.f, bwi, 15.f};
                ui::tilegfx::roundedRect(br, 7.5f, b.second);
                ui::drawTextCentered(FontId::UiBold, b.first, {br.x + br.width * 0.5f, br.y + 7.5f}, 12.f, rgba(250, 244, 230));
                x += bwi + 4.f;
            }
        }
        // a small pointer toward the head (always drawn on their turn, faint otherwise)
        {
            const float px = side < 0.f ? rc.x + rc.width : rc.x;
            const float py = std::clamp(hc.y, rc.y + 8.f, rc.y + rc.height - 8.f);
            const float dx = -side * 7.f;
            const Color col = turn ? fadeC(pal::Highlight, 0.95f) : rgba(30, 18, 10, 228);
            if (side < 0.f) DrawTriangle({px, py - 6}, {px, py + 6}, {px + dx, py}, col); // counter-clockwise on screen
            else DrawTriangle({px, py - 6}, {px + dx, py}, {px, py + 6}, col);
        }
    }
}

// ---------------------------------------------------------------- pile count + okey preview
void TableState::drawPileLabel(const Renderer& r) {
    if (dealing()) return;
    Vector2 c;
    if (r.projectToVirtual({w3d::PILE_POS.x, w3d::TABLE_Y, w3d::PILE_POS.z + 0.066f}, c)) {
        const int n = game->pileCount();
        const bool low = n <= 8;
        const float pulse = low ? 0.75f + 0.25f * std::sin(now * 6.f) : 1.f;
        const std::string cnt = std::to_string(n) + " taş";
        const Vector2 m = ui::measureText(FontId::UiBold, cnt, 15);
        pill({c.x - m.x * 0.5f - 9, c.y - 10, m.x + 18, 20}, rgba(10, 24, 16, 185), rgba(255, 255, 255, 30));
        ui::drawTextCentered(FontId::UiBold, cnt, {c.x, c.y}, 15, low ? fadeC(pal::Bad, pulse) : pal::TextLight);
    }
    if (r.projectToVirtual({w3d::INDICATOR_POS.x, w3d::TABLE_Y, w3d::INDICATOR_POS.z + 0.066f}, c)) {
        // "Okey" + a small drawing of this hand's wild tile
        const std::string lbl = "Okey";
        const Vector2 m = ui::measureText(FontId::UiBold, lbl, 14);
        const float tw = 17.f, th = ui::tilegfx::heightFor(tw);
        const float w = m.x + tw + 26.f;
        const Rectangle rc{c.x - w * 0.5f, c.y - 13.f, w, 26.f};
        pill(rc, rgba(10, 24, 16, 185), rgba(255, 255, 255, 30));
        ui::drawText(FontId::UiBold, lbl, {rc.x + 10.f, c.y - m.y * 0.5f}, 14, pal::Brass);
        ui::tilegfx::Fx fx;
        fx.shadow = false;
        fx.okeyBadge = true;
        ui::tilegfx::drawKey(ui::tilegfx::faceKey(ok().color, ok().number), {rc.x + rc.width - 8.f - tw * 0.5f, c.y + 0.5f}, tw,
                             0.f, fx);
        (void)th;
    }
}

// ---------------------------------------------------------------- istaka hints
void TableState::drawRackHints(const Renderer& r) {
    if (!hints || !playing() || dealing()) return;
    const int dragged = draggedTileId();
    // işlek: a small green "+" on the tile's top-left corner (it fits a table meld: discarding it costs 101)
    for (int i = 0; i < SLOTS; ++i) {
        const int id = slots[i];
        if (id < 0 || id == dragged || !islek[id] || tgt[id].cont != C_HAND + human) continue;
        Vector2 c;
        if (!r.projectToVirtual(poseToWorld(vis[id].pose, {-TW * 0.5f + 0.0058f, TH * 0.5f - 0.0058f, TT * 0.5f}), c)) continue;
        const float rr = 6.5f;
        DrawCircleV({c.x + 0.6f, c.y + 1.2f}, rr + 1.f, rgba(0, 0, 0, 80));
        DrawCircleV(c, rr, rgba(44, 150, 68));
        DrawCircleLinesV(c, rr, rgba(18, 78, 30));
        DrawRectangleRec({c.x - rr * 0.55f, c.y - 1.1f, rr * 1.1f, 2.2f}, rgba(240, 255, 240));
        DrawRectangleRec({c.x - 1.1f, c.y - rr * 0.55f, 2.2f, rr * 1.1f}, rgba(240, 255, 240));
    }
    // series values under valid groups
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groupKind[i] != 1) continue;
        const RackGroup& g = groups[i];
        bool moving = false;
        for (int id : g.ids)
            if (id == dragged || vis[id].flying || tgt[id].cont != C_HAND + human) moving = true;
        if (moving) continue;
        Vector3 c3, n, rt, up;
        rackRowFrame(human, true, g.row, c3, n, rt, up);
        const float mid = (slotRight(g.col) + slotRight(g.col + g.len - 1)) * 0.5f;
        const Vector3 p = Vector3Add(c3, Vector3Add(Vector3Scale(rt, mid), Vector3Add(Vector3Scale(up, -TH * 0.5f + 0.0012f),
                                                                                      Vector3Scale(n, TT * 0.5f + 0.0112f))));
        Vector2 c;
        if (!r.projectToVirtual(p, c)) continue;
        const std::string v = std::to_string(groupValue[i]);
        const Vector2 tm = ui::measureText(FontId::UiBold, v, 13);
        const Rectangle rc{c.x - tm.x * 0.5f - 6, c.y - 1.f, tm.x + 12, 15};
        ui::tilegfx::roundedRect(rc, 7.5f, rgba(16, 58, 22, 235));
        ui::tilegfx::roundedLines(rc, 7.5f, 1.f, rgba(120, 226, 120, 200));
        ui::drawTextCentered(FontId::UiBold, v, {c.x, rc.y + 7.5f}, 13, rgba(196, 255, 196));
    }
}

// ---------------------------------------------------------------- status bar + live counter
std::string TableState::statusText(Color& c) const {
    c = pal::TextLight;
    const okey::HandState hs = game->handState();
    if (hs == okey::HandState::HandOver) return "El bitti";
    if (hs == okey::HandState::MatchOver) return "Maç bitti";
    if (hs != okey::HandState::Playing) return "";
    if (dealing()) return "Taşlar dağıtılıyor…";
    const int cur = game->current();
    if (cur != human) {
        float sz = 17.f;
        return fitText(FontId::UiBold, game->player(cur).name, 260.f, sz, 17.f) + " düşünüyor…";
    }
    c = pal::Highlight;
    if (aiMode) return game->stage() == okey::TurnStage::NeedDraw ? "Yapay zeka düşünüyor…" : "Yapay zeka oynuyor…";
    if (game->stage() == okey::TurnStage::NeedDraw) {
        if (game->canTakeFromLeft(human)) return "Sıra sende — ortadan çek ya da soldakini al";
        return "Sıra sende — ortadan taş çek";
    }
    if (game->pendingLeftTile() >= 0) {
        c = rgba(255, 170, 90);
        return "Yandan aldığın taşı kullan ya da Geri Ver (" + std::to_string(game->rules().penalty) + " ceza)";
    }
    if (game->openedThisTurn(human) && game->rules().waitTurnAfterOpening) return "Bir taş at (açtığın turda işlenmez)";
    return "Bir taş at";
}

void TableState::drawStatus() {
    Color sc;
    const std::string st = statusText(sc);
    std::vector<std::pair<std::string, Color>> parts;
    if (hints && playing() && !dealing()) {
        const okey::PlayerInfo& me = game->player(human);
        const okey::RulesConfig& rc = game->rules();
        const Color dim = rgba(226, 216, 196);
        int seriesVal = 0;
        for (size_t i = 0; i < groups.size(); ++i)
            if (groupKind[i] == 1) seriesVal += groupValue[i];
        const int pc = (int)pairGroups.size();
        if (!me.opened) {
            parts.push_back({"Seri " + std::to_string(seriesVal) + "/" + std::to_string(rc.openThreshold),
                             seriesVal >= rc.openThreshold ? rgba(140, 240, 140) : dim});
            parts.push_back({"  ·  ", fadeC(dim, 0.45f)});
            parts.push_back({"Çift " + std::to_string(pc) + "/" + std::to_string(rc.minPairsToOpen),
                             pc >= rc.minPairsToOpen ? rgba(140, 200, 255) : dim});
        } else if (me.openedWithPairs) {
            parts.push_back({aiMode ? "Çiftten açtı" : "Çiftten açtın", rgba(140, 200, 255)});
            if (pc > 0) {
                parts.push_back({"  ·  ", fadeC(dim, 0.45f)});
                parts.push_back({"Yeni çift: " + std::to_string(pc), rgba(140, 200, 255)});
            }
        } else {
            parts.push_back({(aiMode ? "Açtı: " : "Açtın: ") + std::to_string(me.openValue), rgba(140, 240, 140)});
            if (!seriesGroups.empty()) {
                parts.push_back({"  ·  ", fadeC(dim, 0.45f)});
                parts.push_back({"Yeni per: " + std::to_string(seriesGroups.size()) + " (" + std::to_string(seriesVal) + ")",
                                 rgba(140, 240, 140)});
            }
        }
    }
    if (st.empty() && parts.empty()) return;
    const float fs = 17.f;
    const float stW = st.empty() ? 0.f : ui::measureText(FontId::UiBold, st, fs).x + (myTurn() ? 16.f : 0.f);
    float ctW = 0.f;
    for (auto& p : parts) ctW += ui::measureText(FontId::UiBold, p.first, 16.f).x;
    const float sep = (!st.empty() && !parts.empty()) ? 25.f : 0.f;
    // the Yapay Zeka mode wears a small lit tag at the front of the line
    const char* tag = "YAPAY ZEKA";
    const float tagFs = 12.f, tagSp = 1.f;
    const float tagW = aiMode ? ui::measureText(FontId::UiBold, tag, tagFs, tagSp).x + 16.f : 0.f;
    const float w = stW + ctW + sep + 30.f + (aiMode ? tagW + 8.f : 0.f);
    const Rectangle rc{std::round(ui::VW * 0.5f - w * 0.5f), STATUS_Y - 13.f, std::round(w), 26.f};
    pill(rc, rgba(14, 9, 5, 200), fadeC(sc, myTurn() ? 0.5f : 0.22f));
    float x = rc.x + 15.f;
    if (aiMode) {
        const Rectangle tr{std::round(x - 8.f), STATUS_Y - 8.f, std::round(tagW), 16.f};
        ui::tilegfx::roundedRect(tr, 8.f, rgba(38, 104, 58, 235));
        ui::tilegfx::roundedLines(tr, 8.f, 1.f, rgba(150, 236, 150, 200));
        ui::drawTextCentered(FontId::UiBold, tag, {tr.x + tr.width * 0.5f, STATUS_Y}, tagFs, rgba(214, 255, 214), tagSp);
        x = tr.x + tr.width + 10.f;
    }
    if (!st.empty()) {
        if (myTurn()) {
            DrawCircleV({x + 4.f, STATUS_Y}, 3.8f, fadeC(sc, 0.6f + 0.4f * std::sin(now * 5.f)));
            x += 16.f;
        }
        const Vector2 m = ui::measureText(FontId::UiBold, st, fs);
        ui::drawText(FontId::UiBold, st, {x, STATUS_Y - m.y * 0.5f}, fs, sc);
        x += m.x;
    }
    if (!parts.empty()) {
        if (sep > 0.f) {
            DrawRectangleRec({x + 12.f, STATUS_Y - 8.f, 1.f, 16.f}, rgba(255, 255, 255, 50));
            x += sep;
        }
        for (auto& p : parts) {
            const Vector2 m = ui::measureText(FontId::UiBold, p.first, 16.f);
            ui::drawText(FontId::UiBold, p.first, {x, STATUS_Y - m.y * 0.5f}, 16.f, p.second);
            x += m.x;
        }
    }
}

// ---------------------------------------------------------------- buttons
void TableState::drawButtons() {
    const std::array<Rectangle, NUM_BUTTONS> rc = buttonRects();
    const Vector2 m = confirm.active ? Vector2{-10000, -10000} : in.mouse;
    const okey::PlayerInfo& me = game->player(human);
    const bool canPlay = canAct() && game->stage() == okey::TurnStage::Play;
    const bool openOk = canPlay && (!me.opened || game->canWorkTable(human)) && (!seriesGroups.empty() || !pairGroups.empty());
    const bool ready = hints && openOk && (seriesCheck.valid || pairCheck.valid);
    if (ready) {
        const Rectangle r = rc[0];
        const float a = 0.55f + 0.35f * std::sin(now * 4.f);
        ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f, fadeC(pal::Good, a));
    }
    if (ui::drawButton(rc[0], me.opened ? "Per Aç" : "El Aç", m, openOk, ui::ButtonStyle::Wood, 21)) queue(Btn::Open);
    const bool giveOk = canAct() && game->pendingLeftTile() >= 0;
    if (ui::drawButton(rc[1], "Geri Ver", m, giveOk, ui::ButtonStyle::Wood, 20)) queue(Btn::GiveBack);
    const bool arrOk = rackInteractive() && !me.hand.empty();
    if (ui::drawButton(rc[2], "Seri Diz", m, arrOk, ui::ButtonStyle::Wood, 20)) queue(Btn::Series);
    if (ui::drawButton(rc[3], "Çift Diz", m, arrOk, ui::ButtonStyle::Wood, 20)) queue(Btn::Pairs);
    // "Yapay Zeka": lit (a warm halo, a gilt rim and a green lamp) while the AI plays this seat
    {
        const Rectangle r = rc[4];
        if (aiMode) {
            const float a = 0.45f + 0.25f * std::sin(now * 2.6f);
            ui::tilegfx::drawSoftBox({r.x + r.width / 2, r.y + r.height / 2}, r.width + 16, r.height + 22, 0.f,
                                     fadeC(pal::Highlight, a));
        }
        if (ui::drawButton(r, "Yapay Zeka", m, true, ui::ButtonStyle::Wood, 19)) queue(Btn::AiToggle);
        const Vector2 lamp{r.x + r.width - 10.f, r.y + 9.f};
        if (aiMode) {
            ui::tilegfx::roundedLines({r.x + 2, r.y + 2, r.width - 4, r.height - 6}, 10.f, 2.f, fadeC(pal::Highlight, 0.9f));
            DrawCircleV(lamp, 7.f, rgba(120, 255, 120, 60));
            DrawCircleV(lamp, 4.f, rgba(120, 236, 110));
            DrawCircleV({lamp.x - 1.f, lamp.y - 1.f}, 1.6f, rgba(236, 255, 230));
        } else {
            DrawCircleV(lamp, 4.f, rgba(40, 30, 22));
            DrawCircleLinesV(lamp, 4.f, fadeC(pal::Brass, 0.35f));
        }
    }
    if (ui::drawButton(rc[5], "Menü", m, true, ui::ButtonStyle::Wood, 20)) queue(Btn::Menu);
}

// ---------------------------------------------------------------- toasts
void TableState::drawToasts() {
    const float cx = ui::VW * 0.5f;
    const int n = (int)toasts.size();
    const int first = std::max(0, n - 2);
    float y = TOAST_Y - (float)(n - first - 1) * 30.f;
    for (int i = first; i < n; ++i) {
        const Toast& t = toasts[i];
        const float a = std::min(ui::clamp01(t.age / 0.18f), ui::clamp01((t.dur - t.age) / 0.45f));
        const float size = 17.f;
        float fs = size;
        const std::string text = fitText(FontId::UiBold, t.text, 620.f, fs, 14.f);
        const Vector2 m = ui::measureText(FontId::UiBold, text, fs);
        const float w = m.x + 32.f;
        const float slide = (1.f - ui::easeOutCubic(ui::clamp01(t.age / 0.25f))) * 8.f;
        const Rectangle r{std::round(cx - w / 2), std::round(y - 13 + slide), std::round(w), 26};
        if (a > 0.01f) {
            ui::tilegfx::roundedRect({r.x + 2, r.y + 3, r.width, r.height}, 13, fadeC(rgba(0, 0, 0, 100), a));
            ui::tilegfx::roundedRect(r, 13, fadeC(rgba(22, 14, 8, 232), a));
            ui::tilegfx::roundedLines(r, 13, 1.3f, fadeC(t.color, 0.55f * a));
            ui::drawTextCentered(FontId::UiBold, text, {cx, r.y + 13}, fs, fadeC(t.color, a));
        }
        y += 30.f;
    }
}

// ---------------------------------------------------------------- drag hints
void TableState::drawDragHints(const Renderer& r) {
    const bool holding = (press.dragging && press.kind == Press::RackTile) || (selected >= 0 && press.kind == Press::None);
    if (holding && canAct() && game->stage() == okey::TurnStage::Play) {
        Vector2 c;
        const Vector3 d = w3d::DISCARD_POS[human];
        if (r.projectToVirtual({d.x, w3d::TABLE_Y, d.z + 0.05f}, c)) {
            const float a = aim.overDiscard ? 1.f : 0.55f + 0.25f * std::sin(now * 4.f);
            const Vector2 m = ui::measureText(FontId::UiBold, "At", 16);
            pill({c.x - m.x * 0.5f - 10, c.y - 10, m.x + 20, 21}, fadeC(rgba(14, 9, 5, 210), a), fadeC(pal::Highlight, 0.6f * a));
            ui::drawTextCentered(FontId::UiBold, "At", c, 16, fadeC(pal::Highlight, a));
        }
    }
    if (!press.dragging) return;
    const int id = draggedTileId();
    if (press.kind == Press::RackTile && aim.meld >= 0 && id >= 0 && canAct() && aim.meld < (int)boxes.size()) {
        bool swap = false;
        const bool fits = isleFits(id, aim.meld, aim.meldJoker, &swap);
        const bool legal = fits && game->canWorkTable(human);
        // a verdict tag riding beside the held tile: a tile that fits but cannot go yet says why
        std::string s = swap ? "Okeyi al" : "İşle";
        Color col = pal::Good;
        if (!fits) {
            s = "İşlenmez";
            col = pal::Bad;
        } else if (!legal) {
            col = rgba(255, 170, 90);
            if (game->stage() == okey::TurnStage::NeedDraw) s = "Önce taş çek";
            else if (!game->player(human).opened) s = "Önce elini aç";
            else s = "Sonraki turda işlenir";
        } else if (!swap && game->table()[aim.meld].kind == okey::MeldKind::Run) {
            s = isleFront(id, aim.meld, aim.meldFront) ? "Başa işle" : "Sona işle";
        }
        const Vector2 m = ui::measureText(FontId::UiBold, s, 15);
        const float w = m.x + 20.f;
        float x = in.mouse.x + 30.f;
        if (x + w > ui::VW - 160.f) x = in.mouse.x - 30.f - w;
        const Rectangle rc{std::round(x), std::round(in.mouse.y + 6.f), std::round(w), 21.f};
        pill(rc, rgba(14, 9, 5, 225), fadeC(col, 0.75f));
        ui::drawTextCentered(FontId::UiBold, s, {rc.x + rc.width * 0.5f, rc.y + 10.5f}, 15, col);
    }
    // insertion caret between two istaka tiles
    if (aim.overRack && aim.slot >= 0 && (press.kind == Press::RackTile || press.kind == Press::Pile || press.kind == Press::Left)) {
        const int occupant = slots[aim.slot];
        if (occupant >= 0 && occupant != id) {
            const int row = aim.slot / COLS, col = aim.slot % COLS;
            const float x = slotRight(col) + (aim.after ? PITCH * 0.5f : -PITCH * 0.5f);
            Vector3 c3, n, rt, up;
            rackRowFrame(human, true, row, c3, n, rt, up);
            const Vector3 base = Vector3Add(Vector3Add(c3, Vector3Scale(rt, x)), Vector3Scale(n, TT * 0.5f + 0.001f));
            Vector2 a, b;
            if (r.projectToVirtual(Vector3Add(base, Vector3Scale(up, TH * 0.5f)), a) &&
                r.projectToVirtual(Vector3Add(base, Vector3Scale(up, -TH * 0.5f)), b)) {
                DrawLineEx(a, b, 6.f, fadeC(pal::Highlight, 0.35f));
                DrawLineEx(a, b, 3.f, pal::Highlight);
            }
        }
    }
}

// ---------------------------------------------------------------- confirm modal
void TableState::drawConfirm() {
    if (!confirm.active) return;
    const float a = ui::easeOutCubic(ui::clamp01(confirm.t / 0.2f));
    ui::drawDim(0.5f * a);
    const Rectangle r{800 - 250, 330 + (1.f - a) * 20.f, 500, 210};
    ui::drawPanel(r, ui::PanelStyle::Wood);
    ui::drawTextCentered(FontId::Sign, "Dikkat!", {r.x + r.width / 2, r.y + 42}, 32, pal::Brass);
    if (okey::isValidTile(confirm.tile)) {
        ui::tilegfx::Fx fx;
        ui::tilegfx::drawTile(confirm.tile, ok(), {r.x + 62, r.y + 108}, 44, -6.f, fx);
    }
    ui::drawTextWrapped(FontId::UiBold, confirm.text, {r.x + 104, r.y + 78, r.width - 130, 70}, 20, pal::TextLight, 2);
    const Vector2 m = in.mouse;
    if (ui::drawButton({r.x + r.width / 2 - 170, r.y + 150, 150, 42}, "Evet, at", m, humanInput, ui::ButtonStyle::Wood, 20))
        queue(Btn::ConfirmYes);
    if (ui::drawButton({r.x + r.width / 2 + 20, r.y + 150, 150, 42}, "Vazgeç", m, true, ui::ButtonStyle::Wood, 20))
        queue(Btn::ConfirmNo);
}

// ---------------------------------------------------------------- hover peek (far melds / discard piles)
void TableState::drawPeek(const Renderer& r) {
    (void)r;
    // carrying a rack tile over a meld: the meld is shown as it would be with the tile added (or swapped in)
    const int held = press.dragging && press.kind == Press::RackTile ? draggedTileId() : -1;
    if (peekKind == 0 || confirm.active || (press.kind != Press::None && !(held >= 0 && peekKind == 1))) return;
    const float dwell = held >= 0 ? 0.18f : 0.28f;
    if (peekT < dwell) return;
    std::vector<int> ids;
    std::string title;
    int highlight = -1;
    if (peekKind == 1) {
        if (peekIdx < 0 || peekIdx >= (int)game->table().size()) return;
        const okey::Meld& m = game->table()[peekIdx];
        ids = m.ids();
        const std::string owner = m.owner == human ? std::string("Senin perin") : game->player(m.owner).name;
        title = owner + (m.kind == okey::MeldKind::Pair ? " · çift" : " · " + std::to_string(m.value()));
        if (held >= 0) {
            okey::Meld out;
            bool shown = false;
            int freed = -1;
            if (aim.meldJoker >= 0 && !ok().isJoker(held)) shown = okey::trySwapJoker(m, held, ok(), out, freed);
            if (!shown) {
                const bool front = isleFront(held, peekIdx, aim.meldFront);
                shown = okey::tryAddTile(m, held, ok(), front ? okey::AddSide::Front : okey::AddSide::Back, out) ||
                        okey::tryAddTile(m, held, ok(), okey::AddSide::Auto, out);
            }
            if (shown) {
                ids = out.ids();
                if (m.kind != okey::MeldKind::Pair) title = owner + " · " + std::to_string(out.value());
                for (int i = 0; i < (int)ids.size(); ++i)
                    if (ids[i] == held) highlight = i;
            }
        }
    } else {
        if (peekIdx < 0 || peekIdx > 3) return;
        const std::vector<int>& d = game->player(peekIdx).discards;
        if (d.empty()) return;
        const int n = std::min<int>((int)d.size(), 8);
        for (int i = (int)d.size() - n; i < (int)d.size(); ++i) ids.push_back(d[i]);
        highlight = (int)ids.size() - 1;
        title = (peekIdx == human ? std::string("Senin attıkların") : nameGenitive(game->player(peekIdx).name) + " attıkları");
        if ((int)d.size() > n) title += " (son " + std::to_string(n) + ")";
        if (peekIdx == leftSeat() && game->canTakeFromLeft(human) && !aiMode) title += " · üsttekini alabilirsin";
    }
    if (ids.empty()) return;
    const float a = ui::easeOutCubic(ui::clamp01((peekT - dwell) / 0.18f));
    const float tw = 34.f, th = ui::tilegfx::heightFor(tw), gap = 3.f;
    const float rowW = (float)ids.size() * (tw + gap) - gap;
    const Vector2 tm = ui::measureText(FontId::UiBold, title, 15.f);
    const float w = std::max(rowW, tm.x) + 24.f, h = th + 40.f;
    const Vector2 m = in.mouse;
    float x = m.x + 22.f, y = m.y - h - 16.f;
    if (x + w > ui::VW - 150.f) x = m.x - w - 22.f;
    x = std::clamp(x, 8.f, ui::VW - w - 8.f);
    y = std::clamp(y, 8.f, ui::VH - h - 60.f);
    const Rectangle rc{std::round(x), std::round(y + (1.f - a) * 6.f), std::round(w), h};
    ui::tilegfx::roundedRect({rc.x + 2, rc.y + 4, rc.width, rc.height}, 10, fadeC(rgba(0, 0, 0, 110), a));
    ui::tilegfx::roundedGradV(rc, 10, fadeC(rgba(52, 34, 20, 238), a), fadeC(rgba(28, 18, 10, 238), a));
    ui::tilegfx::roundedLines(rc, 10, 1.3f, fadeC(pal::Brass, 0.6f * a));
    ui::drawText(FontId::UiBold, title, {rc.x + 12.f, rc.y + 8.f}, 15.f, fadeC(pal::TextLight, a));
    float tx = rc.x + 12.f + (w - 24.f - rowW) * 0.5f + tw * 0.5f;
    for (int i = 0; i < (int)ids.size(); ++i) {
        ui::tilegfx::Fx fx;
        fx.alpha = a;
        if (i == highlight) {
            fx.outline = 1.f;
            fx.outlineColor = pal::Highlight;
        }
        ui::tilegfx::drawTile(ids[i], ok(), {tx, rc.y + 30.f + th * 0.5f}, tw, 0.f, fx);
        tx += tw + gap;
    }
}

void TableState::drawHUD(const Renderer& r) {
    if (!game || !started() || !ui::tilegfx::ready()) return;
    if (hoverTile >= 0 || hoverPile || hoverLeft || press.dragging) ui::requestHandCursor();
    drawNameplates(r);
    drawPileLabel(r);
    drawRackHints(r);
    drawDragHints(r);
    drawStatus();
    drawButtons();
    drawToasts();
    drawPeek(r);
    drawConfirm();
}

} // namespace r3d
