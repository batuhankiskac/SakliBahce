// Screens (screens owner): Ayarlar — the name editing, the switches, and the pages (Oyun, 101 kuralları, Sen,
// Görünüm · Ses).
#include "ui/ScreensInternal.h"

namespace ui {

void Screens::Impl::startEditing() {
    nameEditing = true;
    nameBuf = settings.playerName;
    nameBefore = settings.playerName;
}

bool Screens::Impl::commitName() {
    nameEditing = false;
    std::string t = trimmed(nameBuf);
    if (t.empty()) t = nameBefore.empty() ? std::string("Sen") : nameBefore;
    nameBuf = t;
    if (t == settings.playerName) return false;
    settings.playerName = t;
    return true;
}

bool Screens::Impl::cancelName() {
    nameBuf = nameBefore.empty() ? settings.playerName : nameBefore;
    nameEditing = false;
    if (nameBuf == settings.playerName) return false;
    settings.playerName = nameBuf;
    return true;
}

bool Screens::Impl::applyLiveName() {
    const std::string t = trimmed(nameBuf);
    if (t.empty() || t == settings.playerName) return false;
    settings.playerName = t;
    return true;
}

int Screens::Impl::selectedHands() const {
    int best = 0;
    for (int i = 0; i < 6; ++i)
        if (std::abs(kHandChoices[i] - settings.numHands) < std::abs(kHandChoices[best] - settings.numHands)) best = i;
    return best;
}

int Screens::Impl::selectedAnim() const {
    int best = 0;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(kAnimChoices[i] - settings.animSpeed) < std::fabs(kAnimChoices[best] - settings.animSpeed))
            best = i;
    return best;
}

ScreenAction Screens::Impl::updateSettings(float dt, Vector2 m, bool clicked) {
    for (int i = 0; i < 16; ++i) toggleAnim[i] = approach(toggleAnim[i], settings.*kToggleFields[i] ? 1.f : 0.f, 16.f, dt);
    for (int i = 0; i < 2; ++i) senAnim[i] = approach(senAnim[i], settings.*kSenFields[i] ? 1.f : 0.f, 16.f, dt);
    for (int i = 0; i < 4; ++i) y101Anim[i] = approach(y101Anim[i], settings.*kY101Fields[i] ? 1.f : 0.f, 16.f, dt);

    ScreenAction act = ScreenAction::None;
    if (nameEditing) {
        // a press anywhere outside the field ends editing
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !pointInRect(m, L::SetName) && commitName())
            act = ScreenAction::SettingsChanged;
    }
    if (nameEditing) {
        bool changed = false;
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
            if (!nameCodepointOk(cp) || utf8Count(nameBuf) >= 12) continue;
            if (cp == ' ' && (nameBuf.empty() || nameBuf.back() == ' ')) continue;
            utf8Append(nameBuf, cp);
            changed = true;
        }
        if (!nameBuf.empty() && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))) {
            utf8PopBack(nameBuf);
            changed = true;
        }
        if (changed && applyLiveName()) act = ScreenAction::SettingsChanged;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            if (commitName()) act = ScreenAction::SettingsChanged;
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            if (cancelName()) act = ScreenAction::SettingsChanged;
        }
        return act;
    }
    if (!clicked && IsKeyPressed(KEY_ESCAPE)) goBack();
    return act;
}

void Screens::Impl::settingRow(float cy, const char* label, const std::string& hint) {
    if (hint.empty()) {
        drawTextShadow(FontId::UiBold, label, {L::SetLabelX, cy - 15.f}, 27.f, pal::TextLight);
    } else {
        drawTextShadow(FontId::UiBold, label, {L::SetLabelX, cy - 26.f}, 27.f, pal::TextLight);
        drawText(FontId::Ui, hint, {L::SetLabelX, cy + 5.f}, 17.f, alphaMul(pal::TextLight, 0.62f));
    }
}

void Screens::Impl::drawSelectedChip(Rectangle r, const std::string& label, float fs) {
    DrawRectangleRounded({r.x - 4.f, r.y - 4.f, r.width + 8.f, r.height + 8.f}, 0.3f, 8,
                         alphaMul(pal::Highlight, 0.16f));
    DrawRectangleRounded({r.x + 2.f, r.y + 4.f, r.width, r.height}, 0.25f, 8, rgba(0, 0, 0, 0.43f));
    DrawRectangleRounded(r, 0.25f, 8, Color{150, 108, 44, 255});
    const Rectangle inner{r.x + 2.f, r.y + 2.f, r.width - 4.f, r.height - 6.f};
    DrawRectangleRounded(inner, 0.25f, 8, pal::Brass);
    DrawLineEx({inner.x + 8.f, inner.y + 3.f}, {inner.x + inner.width - 8.f, inner.y + 3.f}, 2.f,
               rgba(255, 244, 200, 0.55f));
    DrawRectangleRoundedLinesEx(inner, 0.25f, 8, 1.5f, alphaMul(pal::Highlight, 0.9f));
    drawTextCentered(FontId::UiBold, label, {r.x + r.width * 0.5f, r.y + r.height * 0.5f - 2.f}, fs,
                     pal::WoodDark, 0.5f);
}

void Screens::Impl::chipRow(int id, const std::vector<std::string>& labels, int selected, float cy, float w, float gap,
             Vector2 m, float fs) {
    const float h = 46.f;
    for (int i = 0; i < (int)labels.size(); ++i) {
        const Rectangle r{L::SetCtrlX + (float)i * (w + gap), cy - h * 0.5f, w, h};
        if (i == selected) drawSelectedChip(r, labels[(size_t)i], fs);
        else if (drawButton(r, labels[(size_t)i], m, true, ButtonStyle::Wood, fs)) click(id, i);
    }
}

void Screens::Impl::toggle(int id, int animIdx, bool on, float cy, Vector2 m) {
    const float a = toggleAnim[animIdx];
    const Rectangle track{L::SetCtrlX, cy - 18.f, 78.f, 36.f};
    const Rectangle area{L::SetCtrlX - 6.f, cy - 24.f, 230.f, 48.f};
    const bool hover = pointInRect(m, area);
    DrawRectangleRounded({track.x, track.y + 2.f, track.width, track.height}, 1.f, 16, rgba(0, 0, 0, 0.4f));
    DrawRectangleRounded(track, 1.f, 16, lerpColor(Color{44, 24, 12, 255}, Color{54, 128, 78, 255}, a));
    DrawRectangleRoundedLinesEx(track, 1.f, 16, 1.5f, alphaMul(pal::Brass, hover ? 0.9f : 0.5f));
    const Vector2 k{track.x + 18.f + a * (track.width - 36.f), cy};
    DrawCircleV({k.x + 1.f, k.y + 3.f}, 14.f, rgba(0, 0, 0, 0.4f));
    DrawCircleV(k, 14.f, hover ? pal::Highlight : pal::Brass);
    DrawCircleV({k.x - 4.f, k.y - 4.f}, 5.f, rgba(255, 250, 230, 0.5f));
    drawText(FontId::UiBold, on ? "Açık" : "Kapalı", {track.x + track.width + 16.f, cy - 13.f}, 24.f,
             on ? pal::Highlight : alphaMul(pal::TextLight, 0.6f));
    if (hit(area, id, m)) click(id);
}

int Screens::Impl::selectedOkeyStart() const {
    int best = 0;
    for (int i = 0; i < 3; ++i)
        if (std::abs(kOkeyStartChoices[i] - settings.okeyStart) < std::abs(kOkeyStartChoices[best] - settings.okeyStart))
            best = i;
    return best;
}

void Screens::Impl::drawSettings(Vector2 m) {
    drawDim(0.55f);
    drawPanel(L::SetPanel, PanelStyle::Wood);
    drawHeader("Ayarlar", {650.f, 94.f}, 54.f, kGold); // (left of centre: the three page tabs take the right)
    brassRule(L::SetPanel.x + 50.f, L::SetPanel.x + L::SetPanel.width - 50.f, 136.f);

    // three pages: the game (player, level, the game's rules) | you (your hands at the table) | looks and sound
    {
        const char* tabs[3] = {"Oyun", "Sen", "Görünüm \xC2\xB7 Ses"};
        const int page[3] = {0, 2, 1};
        const float tw[3] = {84.f, 66.f, 160.f};
        float tx = L::SetPanel.x + L::SetPanel.width - 55.f - (tw[0] + tw[1] + tw[2] + 12.f); // top right, clear of the title
        for (int i = 0; i < 3; ++i) {
            const Rectangle r{tx, 70.f, tw[i], 44.f};
            if (page[i] == settingsPage) drawSelectedChip(r, tabs[i], 20.f);
            else if (drawButton(r, tabs[i], m, true, ButtonStyle::Wood, 20.f)) click(C_SetPage, page[i]);
            tx += tw[i] + 6.f;
        }
    }

    // a running list: section headings and rows, the game's own rules in the middle
    float y = L::SetTopY;
    auto section = [&](const std::string& s) {
        drawText(FontId::UiBold, s, {L::SetLabelX, y}, 17.f, alphaMul(pal::Brass, 0.9f), 4.f);
        const float w = measureText(FontId::UiBold, s, 17.f, 4.f).x;
        DrawLineEx({L::SetLabelX + w + 14.f, y + 10.f}, {L::SetPanel.x + L::SetPanel.width - 55.f, y + 10.f}, 1.f,
                   alphaMul(pal::Brass, 0.3f));
        y += L::SetHeadH;
    };
    auto row = [&]() {
        const float cy = y + 16.f;
        y += L::SetRowH;
        return cy;
    };
    if (settingsPage == 1) {
        drawSettingsLooks(m, section, row);
    } else if (settingsPage == 2) {
        drawSettingsSen(m, section, row);
    } else if (settingsPage == 3) {
        drawSettings101(m, section, row); // 101 kuralları
    } else {
    section("OYUN");

    // player name
    {
        const float cy = row();
        settingRow(cy, "Oyuncu adı", "En fazla 12 harf");
        const Rectangle f = L::SetName;
        const bool hover = pointInRect(m, f);
        DrawRectangleRounded({f.x, f.y + 2.f, f.width, f.height}, 0.2f, 8, rgba(0, 0, 0, 0.35f));
        DrawRectangleRounded(f, 0.2f, 8, Color{34, 20, 12, 240});
        DrawRectangleRoundedLinesEx(f, 0.2f, 8, nameEditing ? 2.5f : 1.5f,
                                    nameEditing ? pal::Highlight : alphaMul(pal::Brass, hover ? 0.95f : 0.55f));
        const std::string& shown = nameEditing ? nameBuf : settings.playerName;
        const float tw = measureText(FontId::UiBold, shown, 27.f).x;
        drawText(FontId::UiBold, shown, {f.x + 16.f, f.y + 9.f}, 27.f, pal::TextLight);
        if (nameEditing) {
            if (std::fmod(time, 1.f) < 0.55f)
                DrawRectangleRec({f.x + 18.f + tw, f.y + 10.f, 2.5f, 27.f}, pal::Highlight);
            const std::string cnt = std::to_string(utf8Count(nameBuf)) + "/12";
            drawText(FontId::Ui, cnt, {f.x + f.width - 16.f - measureText(FontId::Ui, cnt, 16.f).x, f.y + 15.f},
                     16.f, alphaMul(pal::TextLight, 0.5f));
        } else {
            drawText(FontId::Ui, "düzenle", {f.x + f.width + 14.f, f.y + 13.f}, 17.f,
                     alphaMul(pal::TextLight, hover ? 0.8f : 0.4f));
        }
        if (hit(f, C_Name, m)) click(C_Name);
    }

    const int lvl = std::clamp(settings.difficulty, 0, 2);
    float cy = row();
    settingRow(cy, "Rakip seviyesi", kLevelHints[lvl]);
    chipRow(C_Level, {kLevelNames[0], kLevelNames[1], kLevelNames[2]}, lvl, cy, 146.f, 12.f, m, 24.f);

    cy = row();
    settingRow(cy, "Animasyon hızı", "Taşların ve rakiplerin hızı");
    chipRow(C_Anim, {"Yavaş", "Normal", "Hızlı"}, selectedAnim(), cy, 146.f, 12.f, m, 24.f);

    cy = row();
    settingRow(cy, "İpuçları", "Oynanabilir hamleler ve İpucu düğmesi");
    toggle(C_Hints, 3, settings.hints, cy, m);

    cy = row();
    settingRow(cy, "Oyun rehberi", "Her oyunun ilk maçında kısa anlatım");
    toggle(C_Guide, 7, settings.guide, cy, m);

    // the selected game's own rules
    const GameKind game = (GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1);
    std::string upper = gameInfo(game).neon;
    section(upper + " KURALLARI");
    switch (game) {
    case GameKind::Yuzbir:
    case GameKind::YuzbirEsli:
        cy = row();
        settingRow(cy, "El sayısı", "Maç kaç el sürsün?");
        chipRow(C_Hands, {"1", "3", "5", "7", "9", "11"}, selectedHands(), cy, 66.f, 12.f, m, 25.f);
        cy = row();
        settingRow(cy, "Katlamalı oyun", "Her açan, öncekinden en az 1 fazlasıyla açar");
        toggle(C_Katlamali, 4, settings.katlamali, cy, m);
        cy = row();
        settingRow(cy, "Yandan açma cezası", "Atana taşın sayısı \xC3\x97" "10, çiftte \xC3\x97" "20");
        toggle(C_YandanCeza, 5, settings.yandanCeza, cy, m);
        {   // 101 kuralları: the rest on their own page
            cy = row();
            const std::string sum = summary101Settings();
            settingRow(cy, "Diğer 101 kuralları", sum.empty() ? std::string("Açma sınırı, katlar, cezalar: kahve usulü") : sum);
            const Rectangle r{L::SetPanel.x + L::SetPanel.width - 55.f - 190.f, cy - 23.f, 190.f, 46.f}; // (right: the summary may be long)
            if (drawButton(r, "Değiştir", m, true, ButtonStyle::Wood, 23.f)) click(C_Y101Page);
        }
        break;
    case GameKind::Okey:
        cy = row();
        settingRow(cy, "Başlangıç puanı", "Herkes bununla başlar, sıfıra inen oyunu bitirir");
        chipRow(C_OkeyStart, {"6", "12", "20"}, selectedOkeyStart(), cy, 66.f, 12.f, m, 25.f);
        cy = row();
        settingRow(cy, "Renkli okey", "Kırmızı, siyah göstergede puan iki kat");
        toggle(C_OkeyRenkli, 10, settings.okeyRenkli, cy, m);
        break;
    case GameKind::Tavla:
        cy = row();
        settingRow(cy, "Rakip", "İki kişilik oyunlarda karşına kim otursun?");
        chipRow(C_TavlaRakip, {"Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, std::clamp(settings.rakip, 1, 3) - 1, cy, 128.f,
                12.f, m, 21.f);
        cy = row(); // Tavla çeşidi
        settingRow(cy, "Çeşit", settings.tavlaCesit == 1   ? "Gülbahar: kırma yok, çift düşeşe kadar"
                                : settings.tavlaCesit == 2 ? "Fevga: tek köşeden, aynı yöne, kırma yok"
                                                           : "Klasik Türk tavlası: pul kırılır");
        chipRow(C_TavlaCesit, {"Klasik", "Gülbahar", "Fevga"}, std::clamp(settings.tavlaCesit, 0, 2), cy, 128.f, 12.f, m, 21.f);
        cy = row();
        settingRow(cy, "Maç", "Kaç sayıya oynansın? (mars iki sayı)");
        chipRow(C_TavlaPoints, {"3", "5", "7"}, closest(kTavlaChoices, settings.tavlaPoints), cy, 66.f, 12.f, m, 25.f);
        cy = row();
        settingRow(cy, "Katlama zarı", "Oyunun değerini ikiye katlayabilirsin");
        toggle(C_TavlaDoubling, 8, settings.tavlaDoubling, cy, m);
        cy = row();
        settingRow(cy, "Katmerli mars", "Kırık pulu ya da evinde pulu kalan marsa 3 sayı");
        toggle(C_TavlaKatmerli, 9, settings.tavlaKatmerli, cy, m);
        break;
    case GameKind::Batak:
        cy = row();
        settingRow(cy, "Eşli batak", "Karşındaki ortağın, puanlar takıma");
        toggle(C_BatakEsli, 6, settings.batakEsli, cy, m);
        cy = row();
        settingRow(cy, "Oyun sonu", "Bu puana ilk ulaşan kazanır");
        chipRow(C_BatakTarget, {"31", "51", "71"}, closest(kBatakTargets, settings.batakTarget), cy, 66.f, 12.f, m, 25.f);
        cy = row();
        settingRow(cy, "Önce koz açılmalı", "Koz çakılmadan ele kozla başlanmaz");
        toggle(C_BatakKoz, 11, settings.batakKozKirilmadan, cy, m);
        break;
    case GameKind::Pisti:
        cy = row();
        settingRow(cy, "Masa", "Dört kişi tek tek, eşli ya da iki kişilik masada");
        chipRow(C_PistiMode, {"4 kişi", "Eşli", "2 kişi"}, std::clamp(settings.pistiMode, 0, 2), cy, 108.f, 12.f, m, 22.f);
        if (settings.pistiMode == 2) { // Rakip: iki kişilik pişti at the tavla table
            cy = row();
            settingRow(cy, "Rakip", "İki kişilik oyunlarda karşına kim otursun?");
            chipRow(C_TavlaRakip, {"Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, std::clamp(settings.rakip, 1, 3) - 1, cy,
                    128.f, 12.f, m, 21.f);
        }
        cy = row();
        settingRow(cy, "Oyun sonu", "Bu puana ilk ulaşan kazanır");
        chipRow(C_PistiTarget, {"101", "151"}, closest(kPistiTargets, settings.pistiTarget), cy, 78.f, 12.f, m, 25.f);
        break;
    case GameKind::King:
        cy = row();
        settingRow(cy, "Kısa King (12 el)", settings.king12 ? "Herkes 1 koz, 2 ceza seçer"
                                                            : "Kapalıyken 20 el: herkes 2 koz, 3 ceza");
        toggle(C_King12, 12, settings.king12, cy, m);
        break;
    case GameKind::Dama: // Dama
        cy = row();
        settingRow(cy, "Rakip", "İki kişilik oyunlarda karşına kim otursun?");
        chipRow(C_DamaRakip, {"Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, std::clamp(settings.rakip, 1, 3) - 1, cy, 128.f,
                12.f, m, 21.f);
        cy = row();
        settingRow(cy, "Maç", "Kaç oyunu ilk alan kazansın?");
        chipRow(C_DamaWins, {"1", "3", "5"}, closest(kDamaWins, settings.damaWins), cy, 66.f, 12.f, m, 25.f);
        break;
    case GameKind::Altmisalti: // Altmışaltı (Rakip)
        cy = row();
        settingRow(cy, "Rakip", "İki kişilik oyunlarda karşına kim otursun?");
        chipRow(C_TavlaRakip, {"Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, std::clamp(settings.rakip, 1, 3) - 1, cy, 128.f,
                12.f, m, 21.f);
        break;
    case GameKind::Bezik: // Bezik
        cy = row(); // (Rakip)
        settingRow(cy, "Rakip", "İki kişilik oyunlarda karşına kim otursun?");
        chipRow(C_TavlaRakip, {"Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}, std::clamp(settings.rakip, 1, 3) - 1, cy, 128.f,
                12.f, m, 21.f);
        cy = row();
        settingRow(cy, "Oyun sonu", "Bu sayıya ilk ulaşan kazanır");
        chipRow(C_BezikTarget, {"500", "1000", "1500"}, closest(kBezikTargets, settings.bezikTarget), cy, 78.f, 12.f, m, 25.f);
        break;
    case GameKind::Konken: // Konken
        cy = row();
        settingRow(cy, "Açma sınırı", "İlk açışta perlerin en az bu kadar etmeli");
        chipRow(C_KonkenOpen, {"40", "51", "71"}, closest(kKonkenOpens, settings.konkenOpen), cy, 66.f, 12.f, m, 25.f);
        cy = row();
        settingRow(cy, "Yanma sınırı", "Toplamı bunu bulan yanar");
        chipRow(C_KonkenLimit, {"101", "151", "201"}, closest(kKonkenLimits, settings.konkenLimit), cy, 78.f, 12.f, m, 25.f);
        cy = row(); // Konken bitiş
        settingRow(cy, "Bitiş", settings.konkenLastStanding ? "Yanan masadan kalkar, son kalan kazanır"
                                                            : "İlk yanan çıkınca biter, en az yazan kazanır");
        chipRow(C_KonkenEnd, {"İlk yanan", "Son kalan"}, settings.konkenLastStanding ? 1 : 0, cy, 128.f, 12.f, m, 21.f);
        break;
    default: break;
    }

    }

    if (backSettings != ScreenId::Title) {
        drawTextWrapped(FontId::Ui, "Oyunun kuralları yeni maçta geçerli olur.", {640.f, 778.f, 330.f, 60.f},
                        17.f, alphaMul(pal::TextLight, 0.6f), 2.f);
    }
    if (drawButton(L::SetDefaults, "Varsayılanlar", m, true, ButtonStyle::Wood, 22.f)) click(C_Defaults);
    if (drawButton(L::SetBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_Back);
}

std::string Screens::Impl::summary101Settings() const {
    std::string out;
    auto add = [&](const std::string& t) { out += (out.empty() ? "" : " \xC2\xB7 ") + t; };
    if (settings.y101Acma != 101) add("açma " + std::to_string(settings.y101Acma));
    if (settings.y101Kat == 1) add("tek kat");
    if (settings.y101Kat == 2) add("katsız");
    if (settings.y101Acmayan != 202) add("açmayan " + std::to_string(settings.y101Acmayan));
    if (!settings.y101OkeyCeza) add("okey cezasız");
    if (!settings.y101IslekCeza) add("işlek cezasız");
    if (settings.y101GeriVer) add("geri verme cezalı");
    if (!settings.y101Bekle) add("açınca işlenir");
    if (out == summary101Key) return summary101Val; // (drawn every frame: measure and trim once per change)
    summary101Key = out;
    if (measureText(FontId::Ui, out, 17.f).x > 560.f) { // (a long list: the first ones and "…")
        while (!out.empty() && measureText(FontId::Ui, out + " \xE2\x80\xA6", 17.f).x > 560.f) {
            const size_t dot = out.rfind(" \xC2\xB7 ");
            if (dot == std::string::npos) break;
            out.erase(dot);
        }
        out += " \xE2\x80\xA6";
    }
    summary101Val = out;
    return out;
}

template <class Section, class Row> void Screens::Impl::drawSettings101(Vector2 m, Section& section, Row& row) {
    section("101 KURALLARI  (101 ve Eşli 101)");
    float cy = row();
    settingRow(cy, "Açma sınırı", "Seriyle açmak için perlerin toplamı");
    int ai = 2;
    for (int i = 0; i < 4; ++i)
        if (kY101Acma[i] == settings.y101Acma) ai = i;
    chipRow(C_Y101Acma, {"51", "81", "101", "121"}, ai, cy, 72.f, 10.f, m, 24.f);
    cy = row();
    settingRow(cy, "Katlamalı oyun", "Her açan, öncekinden en az 1 fazlasıyla açar");
    toggle(C_Katlamali, 4, settings.katlamali, cy, m);
    cy = row();
    const int kat = std::clamp(settings.y101Kat, 0, 2);
    settingRow(cy, "Bitiş katları", kat == 0   ? "Okeyle, çiftten, elden: her biri \xC3\x97" "2, katlanır (\xC3\x97" "8'e kadar)"
                                    : kat == 1 ? "Kaç kat olursa olsun en çok \xC3\x97" "2"
                                               : "Hiç kat yok, her bitiş \xE2\x80\x93" "101");
    chipRow(C_Y101Kat, {"Katlanır", "Tek kat", "Katsız"}, kat, cy, 118.f, 10.f, m, 21.f);
    cy = row();
    settingRow(cy, "Açmayan yazar", "Elini hiç açamayanın cezası (katlarla çarpılır)");
    chipRow(C_Y101Acmayan, {"202", "404"}, settings.y101Acmayan == 404 ? 1 : 0, cy, 78.f, 12.f, m, 25.f);
    cy = row();
    settingRow(cy, "Okey atma cezası", "Okeyi atana 101 (bitiren son taş hariç)");
    senToggle(C_Y101OkeyCeza, y101Anim[0], settings.y101OkeyCeza, cy, m);
    cy = row();
    settingRow(cy, "İşlek taş cezası", "Masadaki pere gidecek taşı atana 101");
    senToggle(C_Y101IslekCeza, y101Anim[1], settings.y101IslekCeza, cy, m);
    cy = row();
    settingRow(cy, "Yandan açma cezası", "Atana taşın sayısı \xC3\x97" "10, çiftte \xC3\x97" "20");
    toggle(C_YandanCeza, 5, settings.yandanCeza, cy, m);
    cy = row();
    settingRow(cy, "Geri verme cezası", "Yandan alıp kullanamadığın taşı geri verene 101");
    senToggle(C_Y101GeriVer, y101Anim[2], settings.y101GeriVer, cy, m);
    cy = row();
    settingRow(cy, "Açınca bir tur bekle", "Açtığın turda işleyemez, per ekleyemezsin");
    senToggle(C_Y101Bekle, y101Anim[3], settings.y101Bekle, cy, m);
}

void Screens::Impl::senToggle(int id, float a, bool on, float cy, Vector2 m) {
    const Rectangle track{L::SetCtrlX, cy - 18.f, 78.f, 36.f};
    const Rectangle area{L::SetCtrlX - 6.f, cy - 24.f, 230.f, 48.f};
    const bool hover = pointInRect(m, area);
    DrawRectangleRounded({track.x, track.y + 2.f, track.width, track.height}, 1.f, 16, rgba(0, 0, 0, 0.4f));
    DrawRectangleRounded(track, 1.f, 16, lerpColor(Color{44, 24, 12, 255}, Color{54, 128, 78, 255}, a));
    DrawRectangleRoundedLinesEx(track, 1.f, 16, 1.5f, alphaMul(pal::Brass, hover ? 0.9f : 0.5f));
    const Vector2 k{track.x + 18.f + a * (track.width - 36.f), cy};
    DrawCircleV({k.x + 1.f, k.y + 3.f}, 14.f, rgba(0, 0, 0, 0.4f));
    DrawCircleV(k, 14.f, hover ? pal::Highlight : pal::Brass);
    DrawCircleV({k.x - 4.f, k.y - 4.f}, 5.f, rgba(255, 250, 230, 0.5f));
    drawText(FontId::UiBold, on ? "Açık" : "Kapalı", {track.x + track.width + 16.f, cy - 13.f}, 24.f,
             on ? pal::Highlight : alphaMul(pal::TextLight, 0.6f));
    if (hit(area, id, m)) click(id);
}

void Screens::Impl::chipSwatch(int i, float cy, float w, float gap, Color c) {
    const Rectangle r{L::SetCtrlX + (float)i * (w + gap) + 12.f, cy + 12.f, w - 24.f, 5.f};
    DrawRectangleRounded(r, 1.f, 6, c);
    DrawRectangleRoundedLinesEx(r, 1.f, 6, 1.f, rgba(0, 0, 0, 0.35f));
}

template <class Section, class Row>
void Screens::Impl::drawSettingsSen(Vector2 m, Section& section, Row& row) {
    section("SEN");
    float cy = row();
    settingRow(cy, "Ellerimi göster", "Taş çekerken, kâğıt atarken, çay içerken");
    senToggle(C_SenHands, senAnim[0], settings.hands, cy, m);
    cy = row();
    settingRow(cy, "Kıyafet", "Kollarında ne var?");
    chipRow(C_SenKol, {"Ceket", "Gömlek", "Kazak"}, std::clamp(settings.kol, 0, 2), cy, 120.f, 10.f, m, 21.f);
    cy = row();
    settingRow(cy, "Renk", settings.kol == 1 ? "Gömleğin (kolları sıvalı)" : settings.kol == 2 ? "Kazağın" : "Ceketin");
    chipRow(C_SenRenk, {"Lacivert", "Kahve", "Gri", "Bordo", "Krem"}, std::clamp(settings.kolRenk, 0, 4), cy, 92.f, 8.f, m,
            18.f);
    {
        const Color sw[5] = {{38, 46, 74, 255}, {92, 62, 42, 255}, {96, 96, 100, 255}, {108, 32, 42, 255}, {214, 202, 172, 255}};
        for (int i = 0; i < 5; ++i) chipSwatch(i, cy, 92.f, 8.f, sw[i]);
    }
    cy = row();
    settingRow(cy, "Ten rengi", "Ellerin ve bileklerin");
    chipRow(C_SenTen, {"Açık", "Buğday", "Esmer", "Koyu"}, std::clamp(settings.ten, 0, 3), cy, 110.f, 10.f, m, 20.f);
    {
        const Color sw[4] = {{234, 192, 160, 255}, {206, 152, 114, 255}, {168, 112, 78, 255}, {112, 74, 52, 255}};
        for (int i = 0; i < 4; ++i) chipSwatch(i, cy, 110.f, 10.f, sw[i]);
    }
    section("MASADA");
    cy = row();
    settingRow(cy, "Takılar", "Sağ elde yüzük, sol bilekte saat");
    {
        // two switches as chips: lit = on
        const char* labels[2] = {"Yüzük", "Saat"};
        const bool ons[2] = {settings.yuzuk, settings.saat};
        const int ids[2] = {C_SenYuzuk, C_SenSaat};
        const float w = 120.f, gap = 10.f, h = 46.f;
        for (int i = 0; i < 2; ++i) {
            const Rectangle r{L::SetCtrlX + (float)i * (w + gap), cy - h * 0.5f, w, h};
            if (ons[i]) {
                drawSelectedChip(r, labels[i], 21.f);
                if (hit(r, ids[i] + 3000, m)) click(ids[i]);
            } else if (drawButton(r, labels[i], m, true, ButtonStyle::Wood, 21.f)) {
                click(ids[i]);
            }
        }
    }
    cy = row();
    settingRow(cy, "Tespih", "Sol elinde; arada bir çevirirsin");
    chipRow(C_SenTespih, {"Yok", "Kehribar", "Oltu", "Yeşil", "Mercan"}, std::clamp(settings.tespih, 0, 4), cy, 92.f, 8.f, m,
            18.f);
    {
        const Color sw[5] = {{0, 0, 0, 0}, {214, 128, 28, 255}, {34, 28, 28, 255}, {36, 104, 66, 255}, {190, 56, 46, 255}};
        for (int i = 1; i < 5; ++i) chipSwatch(i, cy, 92.f, 8.f, sw[i]);
    }
    cy = row();
    settingRow(cy, "Çay bardağın", "İnce belli ya da renkli cam");
    chipRow(C_SenBardak, {"İnce belli", "Yeşil", "Mavi", "Mor"}, std::clamp(settings.bardak, 0, 3), cy, 110.f, 10.f, m, 19.f);
    {
        const Color sw[4] = {{236, 242, 248, 255}, {120, 220, 150, 255}, {120, 170, 255, 255}, {200, 130, 235, 255}};
        for (int i = 1; i < 4; ++i) chipSwatch(i, cy, 110.f, 10.f, sw[i]);
    }
    cy = row();
    settingRow(cy, "Sigara", "Kapalıyken içmezsin; ustalar zaten içiyor");
    senToggle(C_SenSigara, senAnim[1], settings.sigara, cy, m);
}

template <class Section, class Row>
void Screens::Impl::drawSettingsLooks(Vector2 m, Section& section, Row& row) {
    section("GÖRÜNÜM");
    float cy = row();
    settingRow(cy, "Vakit", "Otomatik: bilgisayarın saatine göre");
    chipRow(C_DayTime, {"Otomatik", "Sabah", "Öğle", "Akşam", "Gece"}, std::clamp(settings.dayTime, 0, 4), cy, 96.f,
            8.f, m, 19.f);
    cy = row();
    settingRow(cy, "Mevsim", "Kışın soba yanar, yazın kapı açık");
    chipRow(C_Season, {"Otomatik", "İlkbahar", "Yaz", "Sonbahar", "Kış"}, std::clamp(settings.season, 0, 4), cy, 96.f,
            8.f, m, 19.f);
    // Mekân: the garden kahvehane (Room::setVenue)
    cy = row();
    settingRow(cy, "Mekân", "Otomatik: bahar ve yaz günlerinde bahçe");
    chipRow(C_Venue, {"İçerisi", "Bahçe", "Otomatik"}, std::clamp(settings.venue, 0, 2), cy, 120.f, 10.f, m, 21.f);
    // Özel günler (ozelgun): bayram, Ramazan akşamları, Pazar derbisi
    cy = row();
    settingRow(cy, "Özel günler", "Bayram, Ramazan akşamları, Pazar derbisi");
    chipRow(C_OzelGun, {"Açık", "Kapalı"}, settings.ozelGun ? 0 : 1, cy, 120.f, 10.f, m, 21.f);
    cy = row();
    settingRow(cy, "Renk körü modu", "Taşlarda şekil işareti, dört renkli deste");
    toggle(C_ColorBlind, 14, settings.colorBlind, cy, m);
    cy = row();
    settingRow(cy, "Büyük yazı", "Masadaki yazılar daha büyük");
    toggle(C_BigText, 15, settings.bigText, cy, m);

    section("SES");
    cy = row();
    settingRow(cy, "Sesler", "Efektler, kahvehanenin uğultusu ve radyo");
    {
        // three switches as chips: lit = on
        const char* labels[3] = {"Efekt", "Ortam", "Radyo"};
        const bool ons[3] = {settings.sfx, settings.ambient, settings.music};
        const int ids[3] = {C_Sfx, C_Ambient, C_Music};
        const float w = 108.f, gap = 12.f, h = 46.f;
        for (int i = 0; i < 3; ++i) {
            const Rectangle r{L::SetCtrlX + (float)i * (w + gap), cy - h * 0.5f, w, h};
            if (ons[i]) {
                drawSelectedChip(r, labels[i], 22.f);
                if (hit(r, ids[i] + 3000, m)) click(ids[i]);
            } else if (drawButton(r, labels[i], m, true, ButtonStyle::Wood, 22.f)) {
                click(ids[i]);
            }
        }
    }
    cy = row();
    settingRow(cy, "Konuşma sesleri", "Rakipler konuşurken mırıldanır");
    toggle(C_Voices, 13, settings.voices, cy, m);
}

} // namespace ui
