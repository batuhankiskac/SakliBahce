// Screens (screens owner): the score sheets — the okey hand summary ("hesap kağıdı"), the other games' sheet and
// their match over.
#include "ui/ScreensInternal.h"

namespace ui {

void Screens::Impl::drawGridPaper(Rectangle S) {
    drawPanel(S, PanelStyle::Paper);
    const float step = 30.f;
    for (float x = S.x + step; x < S.x + S.width - 4.f; x += step)
        DrawLineEx({x, S.y + 3.f}, {x, S.y + S.height - 3.f}, 1.f, alphaMul(kGridBlue, 0.13f));
    for (float y = S.y + step; y < S.y + S.height - 4.f; y += step)
        DrawLineEx({S.x + 3.f, y}, {S.x + S.width - 3.f, y}, 1.f, alphaMul(kGridBlue, 0.13f));
    DrawLineEx({S.x + 86.f, S.y + 3.f}, {S.x + 86.f, S.y + S.height - 3.f}, 1.5f, alphaMul(kMarginRed, 0.5f));
    // a tea glass left a ring on the sheet
    const Vector2 st{S.x + S.width - 92.f, S.y + 78.f};
    DrawCircleV(st, 47.f, rgba(160, 100, 40, 0.05f));
    DrawRing(st, 43.f, 48.f, 20.f, 330.f, 40, rgba(150, 90, 36, 0.16f));
    DrawRing(st, 45.f, 47.f, 90.f, 250.f, 30, rgba(130, 70, 24, 0.14f));
}

void Screens::Impl::drawHandSummary(Vector2 m, const okey::Game* g) {
    if (sheet) {
        drawSheetSummary(m, *sheet);
        return;
    }
    const float age = ageOf(ScreenId::HandSummary);
    drawDim(0.5f);
    const float slide = (1.f - easeOutCubic(age / 0.45f)) * 46.f;
    Rectangle S = L::Sheet;
    S.y += slide;
    drawGridPaper(S);
    drawPencil({S.x + 700.f, S.y + S.height - 40.f}, -4.f);

    const int handsDone = g ? (int)g->player(0).handScores.size() : 0;
    const bool last = isLastHand(g);
    const Rectangle btn{S.x + S.width - 252.f, S.y + S.height - 68.f, 216.f, 54.f};
    if (age < 0.46f) m = kNoMouse;
    if (!g || handsDone == 0) {
        handTextCentered("Henüz yazılacak bir el yok.", {800.f, S.y + 300.f}, 44.f, kGraphite);
        if (drawButton(btn, "Devam", m, true, ButtonStyle::Paper, 34.f)) click(C_Next);
        return;
    }
    auto ink = [&](float t0) { return clamp01((age - t0) / 0.28f); };
    const okey::HandResult& r = g->lastHandResult();
    int human = 0;
    for (int s = 0; s < okey::NUM_PLAYERS; ++s)
        if (g->player(s).human) human = s;
    const float labelX = S.x + 108.f;

    // --- title
    {
        const std::string title = "El " + std::to_string(handsDone) + " Sonucu";
        const float a = ink(0.12f);
        handTextCentered(title, {800.f, S.y + 52.f}, 62.f, alphaMul(kInk, a));
        const float w = handMeasure(title, 62.f).x;
        pencilLine({800.f - w * 0.5f - 18.f, S.y + 86.f}, {800.f - w * 0.5f - 18.f + (w + 36.f) * a, S.y + 84.f},
                   2.2f, alphaMul(kInk, 0.8f * a), 3u);
        const std::string sub = g->classic() ? "okey: " + std::to_string(g->rules().okeyStartPoints) + "'den geriye"
                                : std::string(g->teams() ? "eşli maç: " : "maç: ") + std::to_string(handsDone) + " / " +
                                      std::to_string(g->numHands()) + " el" +
                                      // 101 kuralları: the variant in the corner too
                                      (g->rules().katlamali ? " \xC2\xB7 katlamalı" : "") +
                                      (g->rules().finishMult == okey::FinishMult::Single ? " \xC2\xB7 tek kat"
                                       : g->rules().finishMult == okey::FinishMult::None ? " \xC2\xB7 katsız" : "");
        handText(sub, {labelX, S.y + 34.f}, 30.f, alphaMul(kRedPencil, 0.85f * a));
    }
    // --- who finished + multipliers
    {
        const float a = ink(0.3f);
        std::string line;
        if (r.reason == okey::HandEndReason::PlayerFinished && r.winner >= 0) {
            line = !g->player(r.winner).human ? g->player(r.winner).name + " eli bitirdi!"
                   : aiMode                    ? std::string("Eli yapay zeka bitirdi!")
                                               : std::string("Eli sen bitirdin!");
        } else {
            line = g->classic() ? "Taşlar bitti, el berabere." : "Taşlar bitti, eli bitiren olmadı.";
        }
        handTextCentered(line, {800.f, S.y + 118.f}, 42.f, alphaMul(kInk, a));
        // 101 kuralları: katsız oyunda katlar yazılmaz, tek katta birden çok kat yine ×2
        const okey::FinishMult fm = g->classic() ? okey::FinishMult::Stack : g->rules().finishMult;
        const std::string x2 = fm == okey::FinishMult::None ? std::string() : std::string(" \xC3\x97" "2");
        std::vector<std::string> tags;
        if (r.finishedWithJoker) tags.push_back((g->classic() ? "okey atarak" : "okeyle bitiş") + x2);
        if (r.finishedWithPairs) tags.push_back("çiftten bitiş" + x2);
        if (r.finishedInOneGo) tags.push_back("elden bitiş" + x2);
        std::string tagLine;
        Color tagCol = kRedPencil;
        if (tags.empty()) {
            tagLine = "kat yok"; // every column's "Hesap" cell shows its own formula
            tagCol = kGraphite;
        } else if (fm == okey::FinishMult::None) {
            for (size_t i = 0; i < tags.size(); ++i) tagLine += (i ? "  \xC2\xB7  " : "") + tags[i];
            tagLine += "   (katsız oyun: kat yok)";
            tagCol = kGraphite;
        } else {
            for (size_t i = 0; i < tags.size(); ++i) tagLine += (i ? "  \xC2\xB7  " : "") + tags[i];
            if (tags.size() > 1 && fm == okey::FinishMult::Single) tagLine += "  =  \xC3\x97" "2 (tek kat)";
            else if (tags.size() > 1) tagLine += "  =  \xC3\x97" + std::to_string(r.multiplier) + " katlandı";
            tagLine += g->classic() ? "   (herkesten " + std::to_string(2 * r.multiplier) + " düşülür)"
                                    : std::string("   (cezalar hariç tüm puanlar)");
        }
        handTextCentered(tagLine, {800.f, S.y + 158.f}, 32.f, alphaMul(tagCol, ink(0.42f)));
    }

    // --- grid: one column per player
    const float colX0 = S.x + 300.f, colW = 222.f;
    const float gridR = colX0 + colW * 4.f;
    auto colC = [&](int s) { return colX0 + colW * ((float)s + 0.5f); };
    auto fitSize = [&](const std::string& t, float size) {
        const float maxW = colW - 18.f;
        const float w = handMeasure(t, size).x;
        return w > maxW ? size * maxW / w : size;
    };
    auto cellText = [&](const std::string& t, int s, float cy, float size, Color c) {
        handTextCentered(t, {colC(s), cy}, fitSize(t, size), c);
    };
    const float headY = S.y + 210.f;
    const float rowY0 = headY + 48.f, rowH = 35.f;
    const bool classic = g->classic();
    const int nRows = classic ? 3 : 4;
    {
        const float a = ink(0.5f);
        for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
            const std::string name = seatName(*g, s);
            cellText(name, s, headY, 36.f, alphaMul(s == human ? kBluePen : kInk, a));
            if (s == r.winner) {
                const float w = handMeasure(name, fitSize(name, 36.f)).x;
                drawStar({colC(s) - w * 0.5f - 16.f, headY - 1.f}, 10.f, alphaMul(kRedPencil, a));
            }
        }
        pencilLine({labelX - 8.f, headY + 25.f}, {gridR, headY + 25.f}, 2.f, alphaMul(kGraphite, 0.8f * a), 11u);
        for (int s = 0; s <= okey::NUM_PLAYERS; ++s) {
            const float x = colX0 + colW * (float)s;
            pencilLine({x, headY - 26.f}, {x, rowY0 + rowH * (float)(nRows - 1) + 18.f}, 1.2f,
                       alphaMul(kGraphite, 0.35f * a), 20u + (uint32_t)s, 0.8f);
        }
    }
    // this hand, per player. "Hesap" spells out the score before penalties, so Hesap + Ceza = Bu el can be
    // checked by eye: bitiren -101, açmayan 202, seriyle açan elde kalan, çiftle açan elde kalan x 2, and the
    // hand's multiplier (okeyle / çiftten / elden bitiş) on all of them.
    const std::string times = " \xC3\x97 ";
    const std::string multTail = r.multiplier > 1 ? times + std::to_string(r.multiplier) : std::string();
    const okey::RulesConfig& rc = g->rules();
    const char* rowLabels[4] = {"Durum", "Hesap", "Ceza", "Bu el"};
    if (classic) {
        rowLabels[1] = "Gösterge";
        rowLabels[2] = "Bu el";
    }
    const int partnerOfWinner = (g->teams() && r.winner >= 0) ? okey::Game::partnerOf(r.winner) : -1;
    for (int row = 0; row < nRows; ++row) {
        const float cy = rowY0 + rowH * (float)row;
        const float a = ink(0.62f + 0.1f * (float)row);
        handText(rowLabels[row], {labelX, cy - 17.f}, 30.f, alphaMul(kGraphite, a));
        for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
            const okey::PlayerInfo& p = g->player(s);
            const size_t si = (size_t)s;
            Cell c;
            const bool won = s == r.winner;
            if (classic) {
                if (row == 0) c = won ? Cell{"Bitirdi", kRedPencil, 31.f} : Cell{kDash, alphaMul(kGraphite, 0.6f), 30.f};
                else if (row == 1)
                    c = r.indicatorShownBy == s ? Cell{"Gösterdi", kGraphite, 30.f}
                        : r.indicatorShownBy >= 0 ? Cell{scoreText(-r.penalties[si]), kRedPencil, 31.f}
                                                  : Cell{kDash, alphaMul(kGraphite, 0.6f), 30.f};
                else c = {r.score[si] == 0 ? std::string("0") : scoreText(r.score[si]), s == human ? kBluePen : kInk, 36.f};
                cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
                continue;
            }
            if (s == partnerOfWinner && row < 2) { // eşli: the finisher's partner's hand is wiped
                c = row == 0 ? Cell{"Ortağı bitirdi", kRedPencil, 28.f} : Cell{"silindi", kGraphite, 30.f};
                cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
                continue;
            }
            switch (row) {
            case 0:
                if (won) c = {"Bitirdi", kRedPencil, 31.f};
                else if (!p.opened) c = {"Açmadı", kGraphite, 30.f};
                else if (p.openedWithPairs) c = {"Çiftle açtı (" + std::to_string(p.openValue) + " çift)", kGraphite, 28.f};
                else c = {"Seriyle açtı (" + std::to_string(p.openValue) + ")", kGraphite, 28.f};
                break;
            case 1:
                if (won) c = {scoreText(rc.winnerScore) + multTail, kGraphite, 31.f};
                else if (!p.opened) c = {std::to_string(rc.unopenedScore) + multTail, kGraphite, 31.f};
                else if (p.openedWithPairs)
                    c = {std::to_string(r.remaining[si]) + times + "2" + multTail, kGraphite, 31.f};
                else c = {std::to_string(r.remaining[si]) + multTail, kGraphite, 31.f};
                break;
            case 2:
                if (r.penalties[si] > 0) c = {"+" + std::to_string(r.penalties[si]), kRedPencil, 31.f};
                else c = {kDash, alphaMul(kGraphite, 0.6f), 30.f};
                break;
            default:
                c = {scoreText(r.score[si]), s == human ? kBluePen : kInk, 36.f};
                break;
            }
            cellText(c.text, s, cy, c.size, alphaMul(c.color, a));
        }
    }
    const float sepY = rowY0 + rowH * (float)(nRows - 1) + 24.f;
    {
        const float a = ink(1.0f);
        pencilLine({labelX - 8.f, sepY}, {gridR, sepY}, 1.8f, alphaMul(kGraphite, 0.8f * a), 31u);
        pencilLine({labelX - 8.f, sepY + 5.f}, {gridR, sepY + 5.f}, 1.8f, alphaMul(kGraphite, 0.8f * a), 37u);
    }

    // --- running sheet: one row per hand, totals underlined, the leader circled in red pencil
    const int n = handsDone;
    const float histTop = sepY + 12.f;
    const float totYMax = btn.y - 40.f; // the leader's circle stays clear of the button
    const float hRow = std::min(36.f, (totYMax - 32.f - histTop) / (float)n);
    const float hFont = std::min(32.f, hRow * 1.02f + 2.f);
    const float t0 = 1.05f;
    for (int h = 0; h < n; ++h) {
        const float cy = histTop + hRow * ((float)h + 0.5f);
        const float a = ink(t0 + 0.05f * (float)h);
        if (h == n - 1) { // highlighter over this hand's row
            DrawRectangleRounded({labelX - 12.f, cy - hRow * 0.42f, (gridR - labelX + 20.f) * a, hRow * 0.84f}, 0.4f,
                                 6, rgba(255, 226, 70, 0.33f));
        }
        handText(std::to_string(h + 1) + ". el", {labelX, cy - hFont * 0.55f}, hFont, alphaMul(kGraphite, a));
        {   // 101 kuralları: the hand's kat in red pencil by its label ("×4")
            const std::vector<int>& km = g->handMultipliers();
            if (h < (int)km.size() && km[(size_t)h] > 1) {
                const std::string kt = "\xC3\x97" + std::to_string(km[(size_t)h]);
                const float kx = labelX + handMeasure(std::to_string(h + 1) + ". el", hFont).x + 12.f;
                handText(kt, {kx, cy - hFont * 0.5f}, hFont * 0.85f, alphaMul(kRedPencil, a));
            }
        }
        for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
            const std::vector<int>& hs = g->player(s).handScores;
            if (h >= (int)hs.size()) continue;
            cellText(scoreText(hs[(size_t)h]), s, cy, hFont, alphaMul(s == human ? kBluePen : kGraphite, a));
        }
    }
    const float sumY = histTop + hRow * (float)n + 4.f;
    const float totY = sumY + 28.f;
    for (int s = 0; s <= okey::NUM_PLAYERS; ++s) {
        const float x = colX0 + colW * (float)s;
        pencilLine({x, histTop}, {x, totY + 26.f}, 1.2f, alphaMul(kGraphite, 0.35f * ink(t0)), 50u + (uint32_t)s, 0.8f);
    }
    const float tTot = t0 + 0.05f * (float)n + 0.12f;
    pencilLine({colX0 + 10.f, sumY}, {gridR - 10.f, sumY}, 2.f, alphaMul(kInk, 0.85f * ink(tTot)), 61u);
    handText("Toplam", {labelX, totY - 20.f}, 36.f, alphaMul(kInk, ink(tTot)));
    const std::vector<int> leaders = coLeaders(*g);
    for (int s = 0; s < okey::NUM_PLAYERS; ++s) {
        const std::string t = scoreText(g->player(s).totalScore);
        const float a = ink(tTot + 0.06f * (float)s);
        cellText(t, s, totY, 40.f, alphaMul(s == human ? kBluePen : kInk, a));
        const float w = handMeasure(t, 40.f).x;
        pencilLine({colC(s) - w * 0.5f - 6.f, totY + 20.f}, {colC(s) + w * 0.5f + 6.f, totY + 20.f}, 1.8f,
                   alphaMul(kInk, 0.8f * a), 70u + (uint32_t)s);
        pencilLine({colC(s) - w * 0.5f - 6.f, totY + 25.f}, {colC(s) + w * 0.5f + 6.f, totY + 25.f}, 1.8f,
                   alphaMul(kInk, 0.8f * a), 80u + (uint32_t)s);
        if (std::find(leaders.begin(), leaders.end(), s) != leaders.end()) { // every co-leader is circled
            const float cp = clamp01((age - tTot - 0.35f) / 0.55f);
            pencilEllipse({colC(s), totY + 2.f}, w * 0.5f + 30.f, 28.f, cp, 2.6f, alphaMul(kRedPencil, 0.9f),
                          (uint32_t)(handsDone * 7 + s));
        }
    }
    if (g->teams()) { // the two teams' totals, under the sheet
        const float a = ink(tTot + 0.4f);
        const std::string t = teamName(*g, 0) + ": " + scoreText(g->teamTotal(0)) + "      " + teamName(*g, 1) + ": " +
                              scoreText(g->teamTotal(1));
        handTextCentered(t, {800.f, totY + 58.f}, 30.f, alphaMul(kInk, a));
    }
    const float aNote = ink(tTot + 0.6f);
    if (aNote > 0.f && g->teams()) {
        const bool you = humanAmong(*g, leaders);
        std::string note;
        if (leaders.size() == 4) note = last ? "Maç berabere bitti" : "Takımlar başa baş";
        else if (you) note = last ? "Maçı takımınız kazandı!" : "Takımınız önde!";
        else note = last ? "Maçı " + teamName(*g, leaders[0]) + " kazandı" : teamName(*g, leaders[0]) + " önde";
        if (!last) note += "  (düşük puan iyidir)";
        handText(note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
    } else if (aNote > 0.f) {
        const bool you = humanAmong(*g, leaders);
        std::string note;
        if (leaders.size() == 1) {
            const std::string who = seatName(*g, leaders[0]);
            if (you && aiMode) note = last ? "Maçı yapay zeka kazandı!" : "Yapay zeka önde!";
            else note = last ? (you ? std::string("Maçı sen kazandın!") : "Maçı " + who + " kazandı")
                             : (you ? std::string("Önde sensin!") : who + " önde");
        } else if (leaders.size() == (size_t)okey::NUM_PLAYERS) {
            note = last ? "Maç berabere bitti, birincilik herkesin" : "Herkes başa baş";
        } else if (you) {
            const std::string others = joinNames(*g, leaders, true);
            if (aiMode)
                note = last ? "Berabere! Yapay zeka birinciliği " + others + " ile paylaştı"
                            : "Yapay zeka " + others + " ile başa baş önde";
            else
                note = last ? "Berabere! Birinciliği " + others + " ile paylaştın" : others + " ile başa baş öndesin";
        } else {
            const std::string all = joinNames(*g, leaders, false);
            note = last ? "Birinciliği " + all + " paylaştı" : all + " başa baş önde";
        }
        if (!last) note += classic ? "  (yüksek puan iyidir)" : "  (düşük puan iyidir)";
        handText(note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
    }

    float bfs = 36.f;
    const std::string blabel = autoLabel(last ? "Sonuçlar" : "Sonraki El", FontId::Hand, btn.width - 22.f, bfs);
    if (drawButton(btn, blabel, m, true, ButtonStyle::Paper, bfs)) click(C_Next);
}

void Screens::Impl::drawSheetSummary(Vector2 m, const SheetModel& sm) {
    const float age = ageOf(ScreenId::HandSummary);
    drawDim(0.5f);
    const float slide = (1.f - easeOutCubic(age / 0.45f)) * 46.f;
    Rectangle S = L::Sheet;
    S.y += slide;
    drawGridPaper(S);
    drawPencil({S.x + 700.f, S.y + S.height - 40.f}, -4.f);
    const Rectangle btn{S.x + S.width - 252.f, S.y + S.height - 68.f, 216.f, 54.f};
    if (age < 0.46f) m = kNoMouse;
    auto ink = [&](float t0) { return clamp01((age - t0) / 0.28f); };
    const float labelX = S.x + 108.f;
    const int nc = std::max(1, (int)sm.columns.size());

    {
        const float a = ink(0.12f);
        handTextCentered(sm.title, {800.f, S.y + 52.f}, 62.f, alphaMul(kInk, a));
        const float w = handMeasure(sm.title, 62.f).x;
        pencilLine({800.f - w * 0.5f - 18.f, S.y + 86.f}, {800.f - w * 0.5f - 18.f + (w + 36.f) * a, S.y + 84.f}, 2.2f,
                   alphaMul(kInk, 0.8f * a), 3u);
        if (!sm.corner.empty()) handText(sm.corner, {labelX, S.y + 34.f}, 30.f, alphaMul(kRedPencil, 0.85f * a));
    }
    {
        float hs = 42.f;
        while (hs > 26.f && handMeasure(sm.headline, hs).x > S.width - 160.f) hs -= 1.f;
        handTextCentered(sm.headline, {800.f, S.y + 118.f}, hs, alphaMul(kInk, ink(0.3f)));
        if (!sm.tagline.empty())
            handTextCentered(sm.tagline, {800.f, S.y + 158.f}, 32.f,
                             alphaMul(sm.taglineRed ? kRedPencil : kGraphite, ink(0.42f)));
    }
    // columns (2..4) over the same 888-unit grid as the okey sheet
    const float gridL = S.x + 300.f, gridW = 222.f * 4.f;
    const float colW = gridW / (float)nc;
    const float colX0 = gridL;
    const float gridR = colX0 + colW * (float)nc;
    auto colC = [&](int c) { return colX0 + colW * ((float)c + 0.5f); };
    auto fitSize = [&](const std::string& t, float size) {
        const float maxW = colW - 18.f;
        const float w = handMeasure(t, size).x;
        return w > maxW ? size * maxW / w : size;
    };
    auto cellText = [&](const std::string& t, int c, float cy, float size, Color col) {
        handTextCentered(t, {colC(c), cy}, fitSize(t, size), col);
    };
    const float headY = S.y + 210.f;
    const float rowY0 = headY + 48.f, rowH = 35.f;
    const int nRows = (int)sm.rowLabels.size();
    {
        const float a = ink(0.5f);
        for (int c = 0; c < nc; ++c) {
            const std::string& name = sm.columns[(size_t)c];
            cellText(name, c, headY, 36.f, alphaMul(c == sm.humanCol ? kBluePen : kInk, a));
            if (c == sm.starCol) {
                const float w = handMeasure(name, fitSize(name, 36.f)).x;
                drawStar({colC(c) - w * 0.5f - 16.f, headY - 1.f}, 10.f, alphaMul(kRedPencil, a));
            }
        }
        pencilLine({labelX - 8.f, headY + 25.f}, {gridR, headY + 25.f}, 2.f, alphaMul(kGraphite, 0.8f * a), 11u);
        for (int c = 0; c <= nc; ++c) {
            const float x = colX0 + colW * (float)c;
            pencilLine({x, headY - 26.f}, {x, rowY0 + rowH * (float)std::max(0, nRows - 1) + 18.f}, 1.2f,
                       alphaMul(kGraphite, 0.35f * a), 20u + (uint32_t)c, 0.8f);
        }
    }
    for (int row = 0; row < nRows; ++row) {
        const float cy = rowY0 + rowH * (float)row;
        const float a = ink(0.62f + 0.1f * (float)row);
        const bool lastRow = row == nRows - 1;
        handText(sm.rowLabels[(size_t)row], {labelX, cy - 17.f}, 30.f, alphaMul(kGraphite, a));
        for (int c = 0; c < nc; ++c) {
            if (row >= (int)sm.cells.size() || c >= (int)sm.cells[(size_t)row].size()) continue;
            const std::string& t = sm.cells[(size_t)row][(size_t)c];
            const bool red = row < (int)sm.red.size() && c < (int)sm.red[(size_t)row].size() && sm.red[(size_t)row][(size_t)c];
            const Color col = red ? kRedPencil : lastRow ? (c == sm.humanCol ? kBluePen : kInk) : kGraphite;
            cellText(t.empty() ? std::string(kDash) : t, c, cy, lastRow ? 36.f : 30.f,
                     alphaMul(t.empty() ? alphaMul(kGraphite, 0.6f) : col, a));
        }
    }
    const float sepY = rowY0 + rowH * (float)std::max(0, nRows - 1) + 24.f;
    {
        const float a = ink(1.0f);
        pencilLine({labelX - 8.f, sepY}, {gridR, sepY}, 1.8f, alphaMul(kGraphite, 0.8f * a), 31u);
        pencilLine({labelX - 8.f, sepY + 5.f}, {gridR, sepY + 5.f}, 1.8f, alphaMul(kGraphite, 0.8f * a), 37u);
    }
    const int n = std::max(1, (int)sm.history.size());
    const float histTop = sepY + 12.f;
    const float totYMax = btn.y - 40.f;
    const float hRow = std::min(36.f, (totYMax - 32.f - histTop) / (float)n);
    const float hFont = std::min(32.f, hRow * 1.02f + 2.f);
    const float t0 = 1.05f;
    for (int h = 0; h < (int)sm.history.size(); ++h) {
        const float cy = histTop + hRow * ((float)h + 0.5f);
        const float a = ink(t0 + 0.05f * (float)std::min(h, 12));
        if (h == (int)sm.history.size() - 1)
            DrawRectangleRounded({labelX - 12.f, cy - hRow * 0.42f, (gridR - labelX + 20.f) * a, hRow * 0.84f}, 0.4f, 6,
                                 rgba(255, 226, 70, 0.33f));
        const std::string lbl = h < (int)sm.historyLabels.size() ? sm.historyLabels[(size_t)h] : std::to_string(h + 1) + ". el";
        float ls = hFont;
        while (ls > 12.f && handMeasure(lbl, ls).x > colX0 - labelX - 10.f) ls -= 1.f;
        handText(lbl, {labelX, cy - ls * 0.55f}, ls, alphaMul(kGraphite, a));
        for (int c = 0; c < nc && c < (int)sm.history[(size_t)h].size(); ++c)
            cellText(sm.history[(size_t)h][(size_t)c], c, cy, hFont, alphaMul(c == sm.humanCol ? kBluePen : kGraphite, a));
    }
    const float sumY = histTop + hRow * (float)n + 4.f;
    const float totY = sumY + 28.f;
    for (int c = 0; c <= nc; ++c) {
        const float x = colX0 + colW * (float)c;
        pencilLine({x, histTop}, {x, totY + 26.f}, 1.2f, alphaMul(kGraphite, 0.35f * ink(t0)), 50u + (uint32_t)c, 0.8f);
    }
    const float tTot = t0 + 0.05f * (float)std::min(n, 12) + 0.12f;
    pencilLine({colX0 + 10.f, sumY}, {gridR - 10.f, sumY}, 2.f, alphaMul(kInk, 0.85f * ink(tTot)), 61u);
    handText(sm.totalLabel, {labelX, totY - 20.f}, 36.f, alphaMul(kInk, ink(tTot)));
    for (int c = 0; c < nc && c < (int)sm.totals.size(); ++c) {
        const std::string& t = sm.totals[(size_t)c];
        const float a = ink(tTot + 0.06f * (float)c);
        cellText(t, c, totY, 40.f, alphaMul(c == sm.humanCol ? kBluePen : kInk, a));
        const float w = handMeasure(t, fitSize(t, 40.f)).x;
        pencilLine({colC(c) - w * 0.5f - 6.f, totY + 20.f}, {colC(c) + w * 0.5f + 6.f, totY + 20.f}, 1.8f,
                   alphaMul(kInk, 0.8f * a), 70u + (uint32_t)c);
        pencilLine({colC(c) - w * 0.5f - 6.f, totY + 25.f}, {colC(c) + w * 0.5f + 6.f, totY + 25.f}, 1.8f,
                   alphaMul(kInk, 0.8f * a), 80u + (uint32_t)c);
        if (std::find(sm.leaders.begin(), sm.leaders.end(), c) != sm.leaders.end()) {
            const float cp = clamp01((age - tTot - 0.35f) / 0.55f);
            pencilEllipse({colC(c), totY + 2.f}, w * 0.5f + 30.f, 28.f, cp, 2.6f, alphaMul(kRedPencil, 0.9f),
                          (uint32_t)(n * 7 + c));
        }
    }
    const float aNote = ink(tTot + 0.6f);
    if (aNote > 0.f && !sm.note.empty()) handText(sm.note, {labelX, btn.y + 10.f}, 30.f, alphaMul(kRedPencil, 0.9f * aNote));
    float bfs = 36.f;
    const std::string blabel = autoLabel(sm.last ? "Sonuçlar" : "Sonraki El", FontId::Hand, btn.width - 22.f, bfs);
    if (drawButton(btn, blabel, m, true, ButtonStyle::Paper, bfs)) click(C_Next);
}

void Screens::Impl::drawSheetMatchOver(Vector2 m, const SheetModel& sm) {
    const float age = ageOf(ScreenId::MatchOver);
    drawDim(0.62f);
    rlPushMatrix();
    rlTranslatef(800.f, 200.f, 0.f);
    rlScalef(1.6f, 1.f, 1.f);
    DrawCircleGradient({0, 0}, 260.f, rgba(8, 5, 3, 0.7f), rgba(8, 5, 3, 0.f));
    rlPopMatrix();
    const float pulse = 0.85f + 0.15f * std::sin(time * 1.4f);
    DrawCircleGradient({800.f, 250.f}, 520.f, rgba(255, 170, 80, 0.26f * pulse), rgba(255, 170, 80, 0.f));
    DrawCircleGradient({800.f, 250.f}, 230.f, rgba(255, 210, 140, 0.22f * pulse), rgba(255, 210, 140, 0.f));
    for (int i = 0; i < 12; ++i) {
        const float a = (float)i * kPi / 6.f + time * 0.05f;
        const Vector2 c{800.f, 230.f};
        tri(c, {c.x + std::cos(a - 0.07f) * 420.f, c.y + std::sin(a - 0.07f) * 420.f},
            {c.x + std::cos(a + 0.07f) * 420.f, c.y + std::sin(a + 0.07f) * 420.f}, rgba(255, 200, 120, 0.045f * pulse));
    }
    drawHeader(sm.matchHeader, {800.f, 56.f}, 60.f, kGold);
    if (!sm.playedLine.empty())
        drawTextCentered(FontId::Ui, sm.playedLine, {800.f, 98.f}, 20.f, alphaMul(pal::TextLight, 0.7f));
    const float pop = easeOutBack(clamp01((age - 0.15f) / 0.5f));
    rlPushMatrix();
    rlTranslatef(800.f, 270.f, 0.f);
    rlScalef(pop, pop, 1.f);
    drawTeaGlass({0.f, 0.f}, 1.25f, time, 42.f);
    rlPopMatrix();
    const float a1 = clamp01((age - 0.35f) / 0.4f);
    drawHeader(sm.resultTitle, {800.f, 324.f}, sm.humanWon ? 50.f : 46.f,
               alphaMul(sm.humanWon ? pal::Highlight : pal::TextLight, a1));
    drawTextCentered(FontId::Ui, sm.resultSub, {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));

    const int rows = (int)sm.ranking.size();
    Rectangle card = L::MatchCard;
    card.height = 32.f + 68.f * (float)rows - 6.f;
    drawPanel(card, PanelStyle::Dark);
    static const Color medal[4] = {Color{232, 184, 64, 255}, Color{196, 198, 206, 255}, Color{190, 118, 64, 255},
                                   Color{118, 88, 62, 255}};
    for (int i = 0; i < rows; ++i) {
        const int c = sm.ranking[(size_t)i];
        const int rank = i < (int)sm.rank.size() ? sm.rank[(size_t)i] : i + 1;
        const float ra = clamp01((age - 0.55f - 0.12f * (float)i) / 0.3f);
        const float slideX = (1.f - easeOutCubic(ra)) * 40.f;
        const Rectangle row{card.x + 18.f + slideX, card.y + 16.f + 68.f * (float)i, card.width - 36.f, 62.f};
        if (rank == 1) {
            DrawRectangleRounded(row, 0.3f, 8, rgba(255, 200, 110, 0.16f * ra));
            DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, alphaMul(pal::Highlight, 0.7f * ra));
        } else {
            DrawRectangleRounded(row, 0.3f, 8, rgba(255, 255, 255, 0.04f * ra));
        }
        const Vector2 mc{row.x + 36.f, row.y + row.height * 0.5f};
        DrawCircleV({mc.x + 1.f, mc.y + 2.f}, 22.f, rgba(0, 0, 0, 0.4f * ra));
        DrawCircleV(mc, 22.f, alphaMul(medal[std::clamp(rank, 1, 4) - 1], ra));
        const std::string digit = std::to_string(rank);
        const float dw = measureText(FontId::UiBold, digit, 25.f).x;
        drawTextCentered(FontId::UiBold, digit, {mc.x, mc.y - 1.f}, 25.f, alphaMul(pal::TextDark, ra));
        drawText(FontId::UiBold, ".", {mc.x + dw * 0.5f, mc.y - 15.f}, 25.f, alphaMul(pal::TextDark, ra));
        const Color nameCol = c == sm.humanCol ? pal::Highlight : pal::TextLight;
        const std::string name = c < (int)sm.columns.size() ? sm.columns[(size_t)c] : std::string();
        drawText(FontId::UiBold, name, {row.x + 76.f, row.y + 7.f}, 28.f, alphaMul(nameCol, ra));
        if (c < (int)sm.rankSub.size()) {
            float fs = 15.f;
            const float maxW = row.width - 76.f - 150.f;
            const float w = measureText(FontId::Ui, sm.rankSub[(size_t)c], fs).x;
            if (w > maxW) fs *= maxW / w;
            drawText(FontId::Ui, sm.rankSub[(size_t)c], {row.x + 77.f, row.y + 40.f}, fs, alphaMul(pal::TextLight, 0.55f * ra));
        }
        const std::string tot = c < (int)sm.totals.size() ? sm.totals[(size_t)c] : std::string();
        const float tw = measureText(FontId::UiBold, tot, 34.f).x;
        drawText(FontId::UiBold, tot, {row.x + row.width - 22.f - tw, row.y + 13.f}, 34.f,
                 alphaMul(rank == 1 ? pal::Highlight : pal::TextLight, ra));
    }
    float nfs = 30.f;
    const std::string nlabel = autoLabel("Yeni Oyun", FontId::UiBold, L::MatchNew.width - 24.f, nfs);
    if (drawButton(L::MatchNew, nlabel, m, true, ButtonStyle::Wood, nfs)) click(C_NewGame);
    if (drawButton(L::MatchMenu, "Ana Menü", m, true, ButtonStyle::Wood, 30.f)) click(C_MatchMenu);
    if (analysisAvailable && drawButton(L::MatchAnalysis, "Hatalarım", m, true, ButtonStyle::Wood, 22.f))
        click(C_ShowAnalysis);
    drawConfetti();
}

} // namespace ui
