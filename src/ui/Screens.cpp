// Screens: title menu, settings, rules ("Nasıl Oynanır?"), pause menu, the end-of-hand score sheet
// ("hesap kağıdı") and the match-over celebration.
//
// Immediate mode: draw() lays out and draws the widgets and records clicks; the next update() turns the
// recorded clicks (plus keyboard / wheel input) into ScreenActions. Screen changes cross-fade: both
// screens are drawn through a tiny alpha shader, so the shared Common.h widgets fade like everything else.
#include "ui/ScreensInternal.h"

namespace ui {

// ============================================================================ Screens::Impl

float Screens::Impl::rnd() {
    rngState = hashU(rngState + 0x9E3779B9u);
    return (float)(rngState & 0xFFFFFFu) / 16777216.f;
}

void Screens::Impl::sfx(Sfx s) {
    if (owner && owner->playSfx) owner->playSfx(s);
}

bool Screens::Impl::hit(Rectangle r, int id, Vector2 m) {
    const bool hover = pointInRect(m, r);
    if (hover) requestHandCursor();
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pressedId = id;
    bool clicked = false;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && pressedId == id) {
        clicked = hover;
        pressedId = -1;
    }
    return clicked;
}

void Screens::Impl::show(ScreenId id) {
    if (id == cur) return;
    if (cur == ScreenId::Settings && nameEditing && commitName()) settingsDirty = true;
    if (id == ScreenId::Settings || id == ScreenId::Rules) {
        ScreenId back = cur;
        if (cur == ScreenId::Settings) back = backSettings;
        else if (cur == ScreenId::Rules) back = backRules;
        (id == ScreenId::Settings ? backSettings : backRules) = back;
    }
    // keep a running fade continuous: the screen that was fading in starts fading out from where it was
    fade = (fade < 1.f) ? 1.f - fade : 0.f;
    if (freezePending) frozen.reset(); // left before its copy was taken: it fades out from the live game
    freezePending = drawsGame(id);     // copied by the next update()/draw() that has the game
    prev = cur;
    cur = id;
    shownAt[(int)id] = time;
    autoLeft = -1.f;
    clicks.clear();
    pressedId = -1;
    switch (id) {
    case ScreenId::Settings:
        nameEditing = false;
        for (int i = 0; i < 16; ++i) toggleAnim[i] = settings.*kToggleFields[i] ? 1.f : 0.f;
        for (int i = 0; i < 2; ++i) senAnim[i] = settings.*kSenFields[i] ? 1.f : 0.f;
        for (int i = 0; i < 4; ++i) y101Anim[i] = settings.*kY101Fields[i] ? 1.f : 0.f;
        break;
    case ScreenId::Rules:
        scroll = scrollTarget = 0.f;
        draggingThumb = false;
        break;
    case ScreenId::Paused:
        confirmQuit = false;
        break;
    case ScreenId::MatchOver:
        spawnConfetti();
        break;
    default:
        break;
    }
}

void Screens::Impl::goBack() {
    if (cur == ScreenId::Settings) show(backSettings);
    else if (cur == ScreenId::Rules) show(backRules);
}

bool Screens::Impl::drawsGame(ScreenId id) {
    return id == ScreenId::Paused || id == ScreenId::HandSummary || id == ScreenId::MatchOver;
}

void Screens::Impl::freeze(const okey::Game* g) {
    if (!freezePending || !g || !drawsGame(cur)) return;
    frozen = *g;
    freezePending = false;
}

const okey::Game* Screens::Impl::gameFor(ScreenId id, const okey::Game* g) const {
    return (id == prev && id != cur && drawsGame(id) && frozen) ? &*frozen : g;
}

int Screens::Impl::standing(const okey::Game& g, int s) {
    if (g.classic()) return -g.player(s).totalScore;
    if (g.teams()) return g.teamTotal(s);
    return g.player(s).totalScore;
}

std::vector<int> Screens::Impl::coLeaders(const okey::Game& g) {
    int best = standing(g, 0);
    for (int s = 1; s < okey::NUM_PLAYERS; ++s) best = std::min(best, standing(g, s));
    std::vector<int> v;
    for (int s = 0; s < okey::NUM_PLAYERS; ++s)
        if (standing(g, s) == best) v.push_back(s);
    return v;
}

std::string Screens::Impl::teamName(const okey::Game& g, int s) const {
    const int a = std::min(s, okey::Game::partnerOf(s)), b = std::max(s, okey::Game::partnerOf(s));
    return seatName(g, a) + " ve " + seatName(g, b);
}

std::string Screens::Impl::seatName(const okey::Game& g, int s) const {
    const okey::PlayerInfo& p = g.player(s);
    return (aiMode && p.human) ? "Yapay Zeka (" + p.name + ")" : p.name;
}

std::string Screens::Impl::joinNames(const okey::Game& g, const std::vector<int>& seats, bool skipHuman) const {
    std::vector<std::string> names;
    for (int s : seats)
        if (!(skipHuman && g.player(s).human)) names.push_back(seatName(g, s));
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " ve " : ", ") + names[i];
    return out;
}

bool Screens::Impl::humanAmong(const okey::Game& g, const std::vector<int>& seats) {
    for (int s : seats)
        if (g.player(s).human) return true;
    return false;
}

std::string Screens::Impl::autoLabel(const std::string& base, FontId f, float maxW, float& size) const {
    const std::string label = autoLeft >= 0.f ? base + " (" + std::to_string((int)std::ceil(autoLeft)) + ")" : base;
    while (size > 18.f && measureText(f, label, size, 0.5f).x > maxW) size -= 1.f;
    return label;
}

bool Screens::Impl::isLastHand(const okey::Game* g) {
    if (!g) return false;
    if (g->classic()) return g->handState() == okey::HandState::MatchOver;
    return g->handState() == okey::HandState::MatchOver ||
           (int)g->player(0).handScores.size() >= g->numHands();
}

ScreenAction Screens::Impl::handleClick(const Click& c, const okey::Game* g) {
    sfx(Sfx::Button);
    switch (c.id) {
    case C_Play:
        show(ScreenId::GameSelect);
        return ScreenAction::None;
    case C_GameCard:
        if (c.value < 0 || c.value >= (int)GameKind::Count || !gameAvailable((GameKind)c.value)) return ScreenAction::None;
        if (settings.game != c.value) {
            settings.game = c.value;
            rulesLaidOut = false; // the rules page follows the game
            settingsDirty = true; // remembered (reported as SettingsChanged by the next update)
        }
        show(ScreenId::None);
        return ScreenAction::StartMatch;
    case C_SelBack:
        show(ScreenId::Title);
        return ScreenAction::None;
    case C_OkeyStart:
        settings.okeyStart = kOkeyStartChoices[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_TavlaPoints:
        settings.tavlaPoints = kTavlaChoices[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_TavlaRakip: // Rakip: one opponent for every two-player game (Settings::rakip)
        settings.rakip = std::clamp(c.value, 0, 2) + 1;
        return ScreenAction::SettingsChanged;
    case C_TavlaCesit: // Tavla çeşidi
        settings.tavlaCesit = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_DamaRakip: // Dama (Rakip: the shared Settings::rakip)
        settings.rakip = std::clamp(c.value, 0, 2) + 1;
        return ScreenAction::SettingsChanged;
    case C_DamaWins: // Dama
        settings.damaWins = kDamaWins[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_BezikTarget: // Bezik
        settings.bezikTarget = kBezikTargets[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_KonkenOpen: // Konken
        settings.konkenOpen = kKonkenOpens[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_KonkenLimit: // Konken
        settings.konkenLimit = kKonkenLimits[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_KonkenEnd: // Konken bitiş
        settings.konkenLastStanding = c.value == 1;
        return ScreenAction::SettingsChanged;
    case C_BatakEsli:
        settings.batakEsli = !settings.batakEsli;
        return ScreenAction::SettingsChanged;
    case C_BatakTarget:
        settings.batakTarget = kBatakTargets[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_PistiTarget:
        settings.pistiTarget = kPistiTargets[std::clamp(c.value, 0, 1)];
        return ScreenAction::SettingsChanged;
    case C_PistiMode:
        settings.pistiMode = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_Guide:
        settings.guide = !settings.guide;
        if (settings.guide) settings.guideSeen = 0; // switched on again: every game's guide once more
        return ScreenAction::SettingsChanged;
    case C_TavlaDoubling:
        settings.tavlaDoubling = !settings.tavlaDoubling;
        return ScreenAction::SettingsChanged;
    case C_TavlaKatmerli:
        settings.tavlaKatmerli = !settings.tavlaKatmerli;
        return ScreenAction::SettingsChanged;
    case C_OkeyRenkli:
        settings.okeyRenkli = !settings.okeyRenkli;
        return ScreenAction::SettingsChanged;
    case C_BatakKoz:
        settings.batakKozKirilmadan = !settings.batakKozKirilmadan;
        return ScreenAction::SettingsChanged;
    case C_King12:
        settings.king12 = !settings.king12;
        return ScreenAction::SettingsChanged;
    case C_SetPage:
        if (nameEditing) commitName();
        settingsPage = std::clamp(c.value, 0, 3); // (3: 101 kuralları)
        return ScreenAction::None;
    // ---- 101 kuralları
    case C_Y101Page:
        if (nameEditing) commitName();
        settingsPage = 3;
        return ScreenAction::None;
    case C_Y101Acma:
        settings.y101Acma = kY101Acma[std::clamp(c.value, 0, 3)];
        return ScreenAction::SettingsChanged;
    case C_Y101Kat:
        settings.y101Kat = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_Y101Acmayan:
        settings.y101Acmayan = c.value == 1 ? 404 : 202;
        return ScreenAction::SettingsChanged;
    case C_Y101OkeyCeza:
        settings.y101OkeyCeza = !settings.y101OkeyCeza;
        return ScreenAction::SettingsChanged;
    case C_Y101IslekCeza:
        settings.y101IslekCeza = !settings.y101IslekCeza;
        return ScreenAction::SettingsChanged;
    case C_Y101GeriVer:
        settings.y101GeriVer = !settings.y101GeriVer;
        return ScreenAction::SettingsChanged;
    case C_Y101Bekle:
        settings.y101Bekle = !settings.y101Bekle;
        return ScreenAction::SettingsChanged;
    // ---- Sen: the player's own hands
    case C_SenHands:
        settings.hands = !settings.hands;
        return ScreenAction::SettingsChanged;
    case C_SenKol:
        settings.kol = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_SenRenk:
        settings.kolRenk = std::clamp(c.value, 0, 4);
        return ScreenAction::SettingsChanged;
    case C_SenTen:
        settings.ten = std::clamp(c.value, 0, 3);
        return ScreenAction::SettingsChanged;
    case C_SenYuzuk:
        settings.yuzuk = !settings.yuzuk;
        return ScreenAction::SettingsChanged;
    case C_SenSaat:
        settings.saat = !settings.saat;
        return ScreenAction::SettingsChanged;
    case C_SenTespih:
        settings.tespih = std::clamp(c.value, 0, 4);
        return ScreenAction::SettingsChanged;
    case C_SenBardak:
        settings.bardak = std::clamp(c.value, 0, 3);
        return ScreenAction::SettingsChanged;
    case C_SenSigara:
        settings.sigara = !settings.sigara;
        return ScreenAction::SettingsChanged;
    case C_DayTime:
        settings.dayTime = std::clamp(c.value, 0, 4);
        return ScreenAction::SettingsChanged;
    case C_Season:
        settings.season = std::clamp(c.value, 0, 4);
        return ScreenAction::SettingsChanged;
    case C_Venue:  // Mekân
        settings.venue = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_OzelGun:  // Özel günler (ozelgun)
        settings.ozelGun = c.value == 0;
        return ScreenAction::SettingsChanged;
    case C_Voices:
        settings.voices = !settings.voices;
        return ScreenAction::SettingsChanged;
    case C_ColorBlind:
        settings.colorBlind = !settings.colorBlind;
        return ScreenAction::SettingsChanged;
    case C_BigText:
        settings.bigText = !settings.bigText;
        return ScreenAction::SettingsChanged;
    case C_WatchAi:
        show(ScreenId::None);
        return ScreenAction::StartAiMatch;
    case C_PauseAi:
        show(ScreenId::None);
        return ScreenAction::ToggleAiMode;
    case C_TitleStats:
        show(ScreenId::Stats);
        return ScreenAction::None;
    case C_StatsBack:
        show(ScreenId::Title);
        return ScreenAction::None;
    case C_StatsReplays:
        show(ScreenId::Replays);
        return ScreenAction::None;
    case C_StatsAchievements: // Başarımlar
        show(ScreenId::Achievements);
        return ScreenAction::None;
    case C_AchievementsBack:
        show(ScreenId::Stats);
        return ScreenAction::None;
    case C_AchievementsTab:
        achTab = std::clamp(c.value, 0, 2);
        return ScreenAction::None;
    case C_ReplaysBack:
        show(ScreenId::Stats);
        return ScreenAction::None;
    case C_ReplayAnalyze:
        chosen = c.value;
        analysisBack = ScreenId::Replays;
        show(ScreenId::Analysis);
        return ScreenAction::AnalyzeReplay;
    case C_ShowAnalysis:
        analysisBack = ScreenId::MatchOver;
        show(ScreenId::Analysis);
        return ScreenAction::None;
    case C_AnalysisBack:
        show(analysisBack);
        return ScreenAction::None;
    case C_ReplayWatch:
        chosen = c.value;
        show(ScreenId::None);
        return ScreenAction::WatchReplay;
    case C_GuideOk:
        show(ScreenId::None);
        return ScreenAction::None;
    case C_Continue:
        show(ScreenId::None);
        return ScreenAction::ResumeSaved;
    case C_TitleRules:
    case C_PauseRules:
        show(ScreenId::Rules);
        return ScreenAction::None;
    case C_TitleSettings:
    case C_PauseSettings:
        show(ScreenId::Settings);
        return ScreenAction::None;
    case C_Quit:
        return ScreenAction::Quit;
    case C_Back: {
        const bool changed = (cur == ScreenId::Settings && nameEditing) ? commitName() : false;
        goBack();
        return changed ? ScreenAction::SettingsChanged : ScreenAction::None;
    }
    case C_Defaults: {
        Settings d;
        d.game = settings.game; // "Varsayılanlar" resets the options, not the game being played
        settings = d;
        nameEditing = false;
        nameBuf = settings.playerName;
        return ScreenAction::SettingsChanged;
    }
    case C_Hands:
        settings.numHands = kHandChoices[std::clamp(c.value, 0, 5)];
        return ScreenAction::SettingsChanged;
    case C_Level:
        settings.difficulty = std::clamp(c.value, 0, 2);
        return ScreenAction::SettingsChanged;
    case C_Anim:
        settings.animSpeed = kAnimChoices[std::clamp(c.value, 0, 2)];
        return ScreenAction::SettingsChanged;
    case C_Sfx:
        settings.sfx = !settings.sfx;
        return ScreenAction::SettingsChanged;
    case C_Ambient:
        settings.ambient = !settings.ambient;
        return ScreenAction::SettingsChanged;
    case C_Music:
        settings.music = !settings.music;
        return ScreenAction::SettingsChanged;
    case C_Hints:
        settings.hints = !settings.hints;
        return ScreenAction::SettingsChanged;
    case C_Katlamali:
        settings.katlamali = !settings.katlamali;
        return ScreenAction::SettingsChanged;
    case C_YandanCeza:
        settings.yandanCeza = !settings.yandanCeza;
        return ScreenAction::SettingsChanged;
    case C_Name:
        if (!nameEditing) startEditing();
        return ScreenAction::None;
    case C_Resume:
        show(ScreenId::None);
        return ScreenAction::Resume;
    case C_PauseMenu:
        confirmQuit = true;
        confirmAt = time;
        return ScreenAction::None;
    case C_ConfirmYes:
        confirmQuit = false;
        show(ScreenId::Title);
        return ScreenAction::ToTitle;
    case C_ConfirmNo:
        confirmQuit = false;
        return ScreenAction::None;
    case C_Next:
        if (sheet ? sheet->last : isLastHand(g)) {
            show(ScreenId::MatchOver);
            return ScreenAction::ShowMatchResult;
        }
        show(ScreenId::None);
        return ScreenAction::NextHand;
    case C_NewGame:
        show(ScreenId::None);
        return ScreenAction::StartMatch;
    case C_MatchMenu:
        show(ScreenId::Title);
        return ScreenAction::ToTitle;
    case C_RulesTab:
        if (rulesLaidOut && c.value >= 0 && c.value < (int)tabY.size())
            scrollTarget = std::clamp(tabY[c.value] - 6.f, 0.f, maxScroll());
        return ScreenAction::None;
    default:
        return ScreenAction::None;
    }
}

ScreenAction Screens::Impl::update(float dt, Vector2 m, const okey::Game* g) {
    dt = std::clamp(dt, 0.f, 0.1f);
    time += dt;
    if (fade < 1.f) fade = std::min(1.f, fade + dt / kFadeTime);
    mouse = m;
    freeze(g);
    if (!fadeShader.ok) fadeShader.load();
    advanceAchievementBanner();

    ScreenAction act = ScreenAction::None;
    auto merge = [&act](ScreenAction a) {
        if (a == ScreenAction::None) return;
        if (act == ScreenAction::None || act == ScreenAction::SettingsChanged) act = a;
    };
    if (settingsDirty) {
        settingsDirty = false;
        act = ScreenAction::SettingsChanged;
    }

    // 1. clicks recorded by the previous draw()
    std::vector<Click> pending;
    pending.swap(clicks);
    bool clicked = false;
    for (const Click& c : pending) {
        if (c.screen != cur) continue; // screen changed since the click was recorded
        merge(handleClick(c, g));
        clicked = true;
    }

    // 2. keyboard / wheel / text input of the current screen
    const bool settled = fade >= 1.f;
    const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    switch (cur) {
    case ScreenId::Title:
        if (!clicked && settled && enter) merge(handleClick({cur, C_Play, 0}, g));
        break;
    case ScreenId::Settings:
        merge(updateSettings(dt, m, clicked));
        break;
    case ScreenId::Rules:
        updateRules(dt, m);
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) goBack();
        break;
    case ScreenId::None:
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Paused);
        break;
    case ScreenId::Paused:
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) {
            if (confirmQuit) confirmQuit = false;
            else merge(handleClick({cur, C_Resume, 0}, g));
        }
        break;
    case ScreenId::HandSummary:
        if (!clicked && settled && (enter || IsKeyPressed(KEY_SPACE))) merge(handleClick({cur, C_Next, 0}, g));
        break;
    case ScreenId::MatchOver:
        if (!clicked && settled && enter) merge(handleClick({cur, C_NewGame, 0}, g));
        break;
    case ScreenId::GameSelect:
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Title);
        else if (!clicked && settled && enter) merge(handleClick({cur, C_GameCard, settings.game}, g));
        break;
    case ScreenId::Stats:
        if (!clicked && (IsKeyPressed(KEY_ESCAPE) || (settled && enter))) show(ScreenId::Title);
        break;
    case ScreenId::Replays:
        if (!clicked && IsKeyPressed(KEY_ESCAPE)) show(ScreenId::Stats);
        break;
    case ScreenId::Analysis:
        if (!clicked && (IsKeyPressed(KEY_ESCAPE) || (settled && enter))) show(analysisBack);
        break;
    case ScreenId::Achievements: // Başarımlar: the arrows / Tab turn the pages
        if (!clicked && (IsKeyPressed(KEY_ESCAPE) || (settled && enter))) show(ScreenId::Stats);
        else if (!clicked && (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_TAB))) achTab = (achTab + 1) % 3;
        else if (!clicked && IsKeyPressed(KEY_LEFT)) achTab = (achTab + 2) % 3;
        break;
    case ScreenId::Guide:
        if (!clicked && settled && (IsKeyPressed(KEY_ESCAPE) || enter || IsKeyPressed(KEY_SPACE))) show(ScreenId::None);
        break;
    }

    // 3. animations
    if (cur == ScreenId::MatchOver || (prev == ScreenId::MatchOver && fade < 1.f)) updateConfetti(dt);
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pressedId = -1;
    return act;
}

void Screens::Impl::spawnConfetti() {
    confetti.clear();
    static const Color cols[] = {pal::Paper, pal::TileFace, Color{255, 255, 250, 255}, pal::Paper,
                                 pal::InkRed, pal::InkBlue, pal::InkYellow, pal::Brass, pal::Oralet};
    for (int i = 0; i < 110; ++i) {
        Confetti c;
        c.p = {rnd(0.f, VW), rnd(-700.f, -10.f)};
        c.v = {rnd(-18.f, 18.f), rnd(55.f, 115.f)};
        c.rot = rnd(0.f, 360.f);
        c.vrot = rnd(-200.f, 200.f);
        c.flip = rnd(0.f, 6.f);
        c.vflip = rnd(3.f, 8.f);
        c.size = rnd(6.f, 11.f);
        c.phase = rnd(0.f, 6.28f);
        c.color = cols[(int)rnd(0.f, 8.999f)];
        confetti.push_back(c);
    }
}

void Screens::Impl::updateConfetti(float dt) {
    const float age = ageOf(ScreenId::MatchOver);
    for (Confetti& c : confetti) {
        if (!c.alive) continue;
        c.p.x += (c.v.x + 26.f * std::sin(time * 1.6f + c.phase)) * dt;
        c.p.y += c.v.y * dt;
        c.rot += c.vrot * dt;
        c.flip += c.vflip * dt;
        if (c.p.y > VH + 20.f) {
            if (age < 7.f) {
                c.p = {rnd(0.f, VW), rnd(-60.f, -10.f)};
            } else {
                c.alive = false;
            }
        }
    }
}

void Screens::Impl::drawConfetti() const {
    for (const Confetti& c : confetti) {
        if (!c.alive) continue;
        const float cf = std::cos(c.flip);
        const float w = c.size * std::fabs(cf) + 1.2f, h = c.size * 0.62f;
        const Color col = cf < 0 ? lerpColor(c.color, Color{60, 40, 30, 255}, 0.28f) : c.color;
        DrawRectanglePro({c.p.x, c.p.y, w, h}, {w * 0.5f, h * 0.5f}, c.rot, col);
    }
}

void Screens::Impl::draw(const okey::Game* g) {
    if (!fadeShader.ok) fadeShader.load();
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pressedId = -1;
    freeze(g);
    const bool fading = fade < 1.f && prev != cur;
    const float e = easeInOutCubic(fade);
    if (fading) drawLayer(prev, 1.f - e, kNoMouse, gameFor(prev, g));
    // the incoming screen takes input once it is mostly visible (no click-through on double clicks)
    const Vector2 m = (fading && fade < 0.6f) ? kNoMouse : mouse;
    drawLayer(cur, fading ? e : 1.f, m, g);
    drawAchievementBanner(); // Başarımlar: over everything
}

void Screens::Impl::drawLayer(ScreenId id, float alpha, Vector2 m, const okey::Game* g) {
    if (id == ScreenId::None || alpha <= 0.004f) return;
    if (alpha >= 0.996f) {
        drawScreen(id, m, g);
        return;
    }
    if (!fadeShader.ok) { // no shader: hard cut half way
        if (alpha >= 0.5f) drawScreen(id, m, g);
        return;
    }
    fadeShader.begin(alpha);
    drawScreen(id, m, g);
    fadeShader.end();
}

void Screens::Impl::drawScreen(ScreenId id, Vector2 m, const okey::Game* g) {
    switch (id) {
    case ScreenId::Title: drawTitle(m); break;
    case ScreenId::Settings: drawSettings(m); break;
    case ScreenId::Rules: drawRules(m); break;
    case ScreenId::Paused: drawPaused(m, g); break;
    case ScreenId::HandSummary: drawHandSummary(m, g); break;
    case ScreenId::MatchOver: drawMatchOver(m, g); break;
    case ScreenId::GameSelect: drawGameSelect(m); break;
    case ScreenId::Stats: drawStats(m); break;
    case ScreenId::Guide: drawGuide(m); break;
    case ScreenId::Replays: drawReplays(m); break;
    case ScreenId::Analysis: drawAnalysis(m); break;
    case ScreenId::Achievements: drawAchievements(m); break;
    case ScreenId::None: break;
    }
}

void Screens::Impl::drawHeader(const std::string& s, Vector2 c, float size, Color col) {
    drawTextCentered(FontId::Sign, s, {c.x + 3.f, c.y + 4.f}, size, rgba(0, 0, 0, 0.55f), 1.f);
    drawTextCentered(FontId::Sign, s, c, size, col, 1.f);
}

void Screens::Impl::brassRule(float x0, float x1, float y) {
    DrawLineEx({x0, y}, {x1, y}, 2.f, alphaMul(pal::Brass, 0.55f));
    const float cx = (x0 + x1) * 0.5f;
    tri({cx, y - 6.f}, {cx - 7.f, y}, {cx, y + 6.f}, pal::Brass);
    tri({cx, y - 6.f}, {cx, y + 6.f}, {cx + 7.f, y}, pal::Brass);
}

const std::vector<Screens::Impl::TitleButton>& Screens::Impl::titleButtons() const {
    const int key = canResume ? 1 : 0;
    if (titleButtonsKey == key) return titleButtonsCache;
    struct Row {
        int n;
        const char* label[2];
        int id[2];
    };
    const Row resumeRows[] = {{1, {"Devam Et", nullptr}, {C_Continue, 0}},
                              {1, {"Yapay Zekayı İzle", nullptr}, {C_WatchAi, 0}},
                              {2, {"Kurallar", "İstatistik"}, {C_TitleRules, C_TitleStats}},
                              {2, {"Ayarlar", "Çıkış"}, {C_TitleSettings, C_Quit}}};
    const Row plainRows[] = {{1, {"Yapay Zekayı İzle", nullptr}, {C_WatchAi, 0}},
                             {2, {"Kurallar", "İstatistik"}, {C_TitleRules, C_TitleStats}},
                             {1, {"Ayarlar", nullptr}, {C_TitleSettings, 0}},
                             {1, {"Çıkış", nullptr}, {C_Quit, 0}}};
    const Row* rows = canResume ? resumeRows : plainRows;
    std::vector<TitleButton>& out = titleButtonsCache;
    out.clear();
    for (size_t i = 0; i < 4; ++i) {
        const float y = L::TitleRowY + (float)i * L::TitleRowStep;
        const float gap = 8.f, w = rows[i].n == 1 ? L::TitleRowW : (L::TitleRowW - gap) * 0.5f;
        for (int j = 0; j < rows[i].n; ++j) {
            const char* label = rows[i].label[j];
            const float fs = rows[i].n > 1 ? 23.f : std::strlen(label) > 12 ? 25.f : 27.f;
            out.push_back({{L::TitleRowX + (float)j * (w + gap), y, w, L::TitleRowH}, label, rows[i].id[j], fs});
        }
    }
    titleButtonsKey = key;
    return out;
}

void Screens::Impl::drawTitle(Vector2 m) {
    const float age = ageOf(ScreenId::Title);
    // let the room show through; darken the edges so the sign and the menu read
    DrawRectangleGradientV(0, 0, (int)VW, 470, rgba(10, 6, 3, 0.62f), rgba(10, 6, 3, 0.f));
    DrawRectangleGradientV(0, 660, (int)VW, 240, rgba(10, 6, 3, 0.f), rgba(10, 6, 3, 0.7f));
    DrawRectangleGradientH(0, 0, 300, (int)VH, rgba(10, 6, 3, 0.55f), rgba(10, 6, 3, 0.f));
    DrawRectangleGradientH((int)VW - 300, 0, 300, (int)VH, rgba(10, 6, 3, 0.f), rgba(10, 6, 3, 0.55f));
    rlPushMatrix();
    rlTranslatef(800.f, 612.f, 0.f);
    rlScalef(1.55f, 1.f, 1.f);
    DrawCircleGradient({0, 0}, 250.f, rgba(10, 6, 3, 0.62f), rgba(10, 6, 3, 0.f));
    rlPopMatrix();

    drawSign(age);

    { // the primary button breathes a little warm light
        const float gl = 0.5f + 0.5f * std::sin(time * 2.1f);
        const Rectangle r = L::TitlePlay;
        DrawRectangleRounded({r.x - 10.f, r.y - 9.f, r.width + 20.f, r.height + 20.f}, 0.4f, 12,
                             alphaMul(pal::Highlight, 0.07f + 0.07f * gl));
        DrawRectangleRounded({r.x - 5.f, r.y - 4.f, r.width + 10.f, r.height + 10.f}, 0.35f, 12,
                             alphaMul(pal::Highlight, 0.10f + 0.08f * gl));
    }
    if (drawButton(L::TitlePlay, "Oyna", m, true, ButtonStyle::Wood, 34.f)) click(C_Play);
    for (const TitleButton& b : titleButtons())
        if (drawButton(b.r, b.label, m, true, ButtonStyle::Wood, b.fs)) click(b.id);
    if (canResume && !resumeLabel.empty()) {
        const Rectangle r = titleButtons().front().r;
        drawTextCentered(FontId::Ui, resumeLabel, {r.x + r.width * 0.5f, r.y + r.height + 9.f}, 15.f,
                         alphaMul(pal::TextLight, 0.6f));
    }

    drawTextCentered(FontId::Ui, "SaklıBahçe  \xC2\xB7  sürüm 1.1  \xC2\xB7  radyoda Turku (CC BY 4.0) ve 1920'lerin plakları", {800.f, 872.f}, 17.f,
                     alphaMul(pal::TextLight, 0.55f));
}

void Screens::Impl::drawSign(float age) {
    const float settle = std::exp(-age * 1.5f);
    const float ang = -3.2f * settle * std::cos(age * 3.3f) + 0.3f * std::sin(time * 0.7f);
    const Vector2 pivot{800.f, 30.f};
    const float W = 880.f, H = 258.f;
    auto xf = [&](Vector2 p) { return vadd(rotateDeg(p, ang), pivot); };

    drawChain({490.f, -8.f}, xf({-310.f, 10.f}));
    drawChain({1110.f, -8.f}, xf({310.f, 10.f}));

    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, 0.f);
    rlRotatef(ang, 0.f, 0.f, 1.f);

    // --- main board: lacquered walnut, gilt frame
    const Rectangle board{-W * 0.5f, 0.f, W, H};
    DrawRectangleRounded({board.x + 10.f, board.y + 16.f, W, H}, 0.07f, 10, rgba(0, 0, 0, 0.45f));
    DrawRectangleRounded(board, 0.07f, 10, Color{40, 20, 10, 255});
    const Rectangle in{board.x + 12.f, board.y + 12.f, W - 24.f, H - 24.f};
    DrawRectangleRounded(in, 0.06f, 10, Color{84, 44, 22, 255});
    for (int i = 0; i < 22; ++i) {
        const float y = in.y + 6.f + (float)i * (in.height - 12.f) / 21.f;
        const float wob = std::sin((float)i * 1.7f) * 4.f;
        DrawLineEx({in.x + 14.f, y + wob}, {in.x + in.width - 14.f, y - wob}, 1.2f,
                   (i % 3 == 0) ? rgba(120, 70, 38, 0.35f) : rgba(40, 18, 8, 0.22f));
    }
    DrawRectangleGradientV((int)in.x + 6, (int)in.y + 4, (int)in.width - 12, (int)(in.height * 0.45f),
                           rgba(255, 196, 120, 0.13f), rgba(255, 196, 120, 0.f));
    const Rectangle gilt{in.x + 10.f, in.y + 10.f, in.width - 20.f, in.height - 20.f};
    DrawRectangleRoundedLinesEx(gilt, 0.05f, 10, 2.5f, alphaMul(kGold, 0.85f));
    DrawRectangleRoundedLinesEx({gilt.x + 6.f, gilt.y + 6.f, gilt.width - 12.f, gilt.height - 12.f}, 0.05f, 10,
                                1.f, alphaMul(kGold, 0.4f));
    drawRivet({in.x + 4.f, in.y + 4.f}, 6.f);
    drawRivet({in.x + in.width - 4.f, in.y + 4.f}, 6.f);
    drawRivet({in.x + 4.f, in.y + in.height - 4.f}, 6.f);
    drawRivet({in.x + in.width - 4.f, in.y + in.height - 4.f}, 6.f);
    for (float ex : {-310.f, 310.f}) {
        DrawRing({ex, 10.f}, 5.f, 8.5f, 0, 360, 20, Color{70, 56, 34, 255});
        DrawRing({ex, 10.f}, 6.f, 7.5f, 200, 320, 8, pal::Brass);
    }

    // --- "SAKLI BAHÇE", gilt letters
    {
        const std::string word = "SAKLI BAHÇE";
        const float sp = 7.f;
        float size = 108.f;
        const float fitW = measureText(FontId::Sign, word, size, sp).x;
        if (fitW > 720.f) size *= 720.f / fitW;
        const Vector2 ms = measureText(FontId::Sign, word, size, sp);
        const Vector2 pos{-ms.x * 0.5f, 34.f};
        drawText(FontId::Sign, word, {pos.x + 4.f, pos.y + 6.f}, size, rgba(0, 0, 0, 0.55f), sp);
        drawText(FontId::Sign, word, {pos.x, pos.y + 2.5f}, size, Color{112, 70, 22, 255}, sp);
        drawText(FontId::Sign, word, pos, size, kGold, sp);
        // warm top-light on the gilding
        beginClipLocal(pos, ms, ang, pivot);
        drawText(FontId::Sign, word, pos, size, Color{255, 236, 180, 255}, sp);
        EndScissorMode();
    }
    // ornament: — ◆ —
    DrawLineEx({-360.f, 150.f}, {-120.f, 150.f}, 2.f, alphaMul(kGold, 0.7f));
    DrawLineEx({120.f, 150.f}, {360.f, 150.f}, 2.f, alphaMul(kGold, 0.7f));
    // --- "101" neon
    {
        const std::string neon = gameInfo((GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1)).neon;
        float ns = 100.f;
        const float nw = measureText(FontId::Sign, neon, ns, 12.f).x;
        if (nw > 640.f) ns *= 640.f / nw;
        drawNeonText(neon, {0.f, 198.f}, ns, 12.f, neonFlicker(time));
    }

    // --- enamel plaque "Kıraathane" hanging below
    const float py = H + 26.f;
    for (float ex : {-150.f, 150.f}) {
        drawChain({ex, H - 4.f}, {ex, py + 6.f});
    }
    const Rectangle plaque{-215.f, py, 430.f, 86.f};
    DrawRectangleRounded({plaque.x + 6.f, plaque.y + 9.f, plaque.width, plaque.height}, 0.3f, 10,
                         rgba(0, 0, 0, 0.4f));
    DrawRectangleRounded(plaque, 0.3f, 10, Color{232, 222, 196, 255});
    DrawRectangleRoundedLinesEx({plaque.x + 5.f, plaque.y + 5.f, plaque.width - 10.f, plaque.height - 10.f},
                                0.3f, 10, 2.f, kSignRed);
    // enamel chips at the corners
    DrawCircleV({plaque.x + 14.f, plaque.y + plaque.height - 12.f}, 4.f, rgba(60, 40, 30, 0.55f));
    DrawCircleV({plaque.x + plaque.width - 20.f, plaque.y + 12.f}, 3.f, rgba(60, 40, 30, 0.45f));
    drawTextCentered(FontId::Sign, "Kıraathane", {0.f, py + 32.f}, 36.f, kSignRed, 1.f);
    drawTextCentered(FontId::UiBold, "KURULUŞ 1974  \xC2\xB7  TAVLA  \xC2\xB7  OKEY  \xC2\xB7  ÇAY",
                     {0.f, py + 64.f}, 15.f, Color{80, 60, 48, 255}, 2.f);
    rlPopMatrix();
}

void Screens::Impl::beginClipLocal(Vector2 pos, Vector2 ms, float angDeg, Vector2 pivot) {
    const Vector2 c = vadd(rotateDeg({pos.x + ms.x * 0.5f, pos.y + ms.y * 0.36f}, angDeg), pivot);
    beginClip({c.x - ms.x * 0.5f - 20.f, c.y - ms.y * 0.36f - 10.f, ms.x + 40.f, ms.y * 0.36f + 10.f});
}

Rectangle Screens::Impl::gameCardRect(int k) const {
    constexpr int first = ((int)GameKind::Count + 1) / 2; // two rows, the longer one on top
    const int row = k < first ? 0 : 1;
    const int n = row == 0 ? first : (int)GameKind::Count - first;
    const int i = row == 0 ? k : k - first;
    const float total = (float)n * L::SelCardW + (float)(n - 1) * L::SelGap;
    return {800.f - total * 0.5f + (float)i * (L::SelCardW + L::SelGap), L::SelRowY[row], L::SelCardW, L::SelCardH};
}

void Screens::Impl::drawGameEmblem(GameKind k, Vector2 c) {
    enum { Y = 0, M = 1, K = 2, R = 3 };
    auto tiles = [&](std::vector<MiniTile> ts, float y) {
        const float w = kMiniW * 1.05f, h = kMiniH * 1.05f;
        const float tw = (float)ts.size() * w + (float)(ts.size() - 1) * kMiniGap;
        float x = c.x - tw * 0.5f;
        for (const MiniTile& t : ts) {
            drawMiniTile({x, y - h * 0.5f, w, h}, t);
            x += w + kMiniGap;
        }
    };
    switch (k) {
    case GameKind::Yuzbir: tiles({T(R, 10), T(R, 11), T(R, 12), T(R, 13)}, c.y); break;
    case GameKind::YuzbirEsli: tiles({T(M, 8), T(Y, 8), T(K, 8), T(R, 8)}, c.y); break;
    case GameKind::Okey: tiles({T(K, 5), TOkey(), T(K, 7), TFake()}, c.y); break;
    case GameKind::Tavla:
        DrawCircleV({c.x - 70.f, c.y + 14.f}, 22.f, Color{232, 222, 198, 255});
        DrawRing({c.x - 70.f, c.y + 14.f}, 14.f, 16.f, 0, 360, 24, Color{180, 160, 130, 255});
        DrawCircleV({c.x + 70.f, c.y + 14.f}, 22.f, Color{70, 28, 22, 255});
        DrawRing({c.x + 70.f, c.y + 14.f}, 14.f, 16.f, 0, 360, 24, Color{120, 60, 44, 255});
        drawMiniDie({c.x - 24.f, c.y - 4.f}, 46.f, 6, -12.f);
        drawMiniDie({c.x + 28.f, c.y + 2.f}, 46.f, 5, 9.f);
        break;
    case GameKind::Pisti:
        drawMiniCard({c.x - 34.f, c.y + 4.f}, 52.f, -14.f, "V", 3);
        drawMiniCard({c.x + 34.f, c.y + 4.f}, 52.f, 12.f, "V", 2);
        break;
    case GameKind::Batak:
        drawMiniCard({c.x - 46.f, c.y + 8.f}, 50.f, -16.f, "A", 0);
        drawMiniCard({c.x, c.y}, 50.f, 0.f, "P", 0);
        drawMiniCard({c.x + 46.f, c.y + 8.f}, 50.f, 16.f, "K", 0);
        break;
    case GameKind::King:
        drawMiniCard({c.x - 30.f, c.y + 6.f}, 52.f, -10.f, "K", 1);
        drawMiniCard({c.x + 30.f, c.y + 6.f}, 52.f, 10.f, "P", 1);
        break;
    case GameKind::Altmisalti: // Altmışaltı: the koz turned up crosswise, a Karo marriage on it
        drawMiniCard({c.x - 46.f, c.y + 12.f}, 44.f, 90.f, "10", 3);
        drawMiniCard({c.x + 4.f, c.y + 2.f}, 48.f, -9.f, "K", 2);
        drawMiniCard({c.x + 46.f, c.y + 6.f}, 48.f, 11.f, "P", 2);
        break;
    case GameKind::Bezik: // Bezik: the Maça Kız and the Karo Vale (a bezik), the second copy peeking behind
        drawMiniCard({c.x - 40.f, c.y + 2.f}, 46.f, -12.f, "K", 0);
        drawMiniCard({c.x - 26.f, c.y + 6.f}, 46.f, -6.f, "K", 0);
        drawMiniCard({c.x + 26.f, c.y + 6.f}, 46.f, 6.f, "V", 2);
        drawMiniCard({c.x + 40.f, c.y + 2.f}, 46.f, 12.f, "V", 2);
        break;
    case GameKind::Dama: { // Dama: a corner of the board, a dark disc, a light one and a light dama (two stacked)
        const float sq = 21.f, x0 = c.x - sq * 3.f, y0 = c.y - sq * 1.5f + 6.f;
        DrawRectangleRounded({x0 - 5.f, y0 - 5.f, sq * 6.f + 10.f, sq * 3.f + 10.f}, 0.12f, 6, Color{92, 52, 26, 255});
        for (int r = 0; r < 3; ++r)
            for (int q = 0; q < 6; ++q)
                DrawRectangleRec({x0 + q * sq, y0 + r * sq, sq, sq},
                                 (r + q) % 2 ? Color{226, 196, 148, 255} : Color{120, 74, 40, 255});
        auto disc = [&](Vector2 p, bool light) {
            DrawCircleV({p.x, p.y + 2.f}, 9.5f, Color{0, 0, 0, 90});
            DrawCircleV(p, 9.f, light ? Color{240, 226, 194, 255} : Color{58, 30, 18, 255});
            DrawRing(p, 5.f, 6.2f, 0, 360, 24, light ? Color{190, 162, 120, 255} : Color{104, 62, 40, 255});
        };
        disc({x0 + sq * 1.5f, y0 + sq * 0.5f}, false);
        disc({x0 + sq * 2.5f, y0 + sq * 0.5f}, false);
        disc({x0 + sq * 2.5f, y0 + sq * 1.5f}, true);
        disc({x0 + sq * 4.5f, y0 + sq * 2.5f}, true);
        disc({x0 + sq * 4.5f, y0 + sq * 2.5f - 5.f}, true); // the dama: a second disc on top
        break;
    }
    case GameKind::Konken: { // Konken: a Kupa run 7-8 laid down, the joker standing in for the 9
        drawMiniCard({c.x - 50.f, c.y + 8.f}, 46.f, -4.f, "7", 1);
        drawMiniCard({c.x - 14.f, c.y + 6.f}, 46.f, 0.f, "8", 1);
        const float w = 46.f, h = w * 1.42f;
        rlPushMatrix();
        rlTranslatef(c.x + 24.f, c.y + 2.f, 0.f);
        rlRotatef(6.f, 0.f, 0.f, 1.f);
        DrawRectangleRounded({-w * 0.5f + 2.f, -h * 0.5f + 4.f, w, h}, 0.14f, 6, rgba(0, 0, 0, 0.35f));
        DrawRectangleRounded({-w * 0.5f, -h * 0.5f, w, h}, 0.14f, 6, Color{246, 240, 226, 255});
        DrawRectangleRoundedLinesEx({-w * 0.5f, -h * 0.5f, w, h}, 0.14f, 6, 1.f, Color{150, 132, 110, 255});
        const Color capC[3] = {Color{186, 32, 36, 255}, Color{30, 28, 34, 255}, Color{186, 32, 36, 255}};
        for (int i = 0; i < 3; ++i) { // the jester's cap and its bells
            const float bx = -9.f + 9.f * (float)i;
            const Vector2 tip{bx * 1.6f, i == 1 ? -24.f : -16.f};
            DrawTriangle({bx + 5.f, 0.f}, tip, {bx - 5.f, 0.f}, capC[i]);
            DrawCircleV(tip, 2.6f, Color{214, 164, 60, 255});
        }
        DrawCircleV({0.f, 9.f}, 9.f, Color{240, 210, 176, 255});
        DrawCircleV({-3.f, 7.f}, 1.3f, Color{30, 28, 34, 255});
        DrawCircleV({3.f, 7.f}, 1.3f, Color{30, 28, 34, 255});
        drawText(FontId::UiBold, "J", {-w * 0.5f + 5.f, -h * 0.5f + 3.f}, w * 0.3f, Color{186, 32, 36, 255});
        rlPopMatrix();
        break;
    }
    default: break;
    }
}

void Screens::Impl::drawGameSelect(Vector2 m) {
    const float age = ageOf(ScreenId::GameSelect);
    drawDim(0.6f);
    drawHeader("Ne oynayalım?", {800.f, 84.f}, 58.f, kGold);
    drawTextCentered(FontId::Ui, "Masaya otur, oyunu seç. Kurallar her oyunun kendi sayfasında.", {800.f, 134.f}, 20.f,
                     alphaMul(pal::TextLight, 0.7f));
    const int sel = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
    for (int k = 0; k < (int)GameKind::Count; ++k) {
        const GameKind gk = (GameKind)k;
        const GameInfo& info = gameInfo(gk);
        const bool avail = gameAvailable(gk);
        const float a = clamp01((age - 0.05f * (float)k) / 0.3f);
        Rectangle r = gameCardRect(k);
        r.y += (1.f - easeOutCubic(a)) * 30.f;
        const Rectangle hitR = r; // hover and click on the resting card (the lift is drawing only)
        const bool hover = avail && pointInRect(m, hitR) && a > 0.9f;
        if (hover) {
            r.y -= 4.f;
            requestHandCursor();
        }
        // the board: dark walnut with a brass edge; the last game played glows
        if (k == sel && avail)
            DrawRectangleRounded({r.x - 7.f, r.y - 7.f, r.width + 14.f, r.height + 14.f}, 0.1f, 10,
                                 alphaMul(pal::Highlight, (0.16f + 0.06f * std::sin(time * 2.2f)) * a));
        DrawRectangleRounded({r.x + 5.f, r.y + 9.f, r.width, r.height}, 0.08f, 10, rgba(0, 0, 0, 0.45f * a));
        DrawRectangleRounded(r, 0.08f, 10, alphaMul(Color{58, 32, 18, 255}, a));
        const Rectangle in{r.x + 8.f, r.y + 8.f, r.width - 16.f, r.height - 16.f};
        DrawRectangleRounded(in, 0.07f, 10, alphaMul(hover ? Color{104, 58, 30, 255} : Color{86, 46, 24, 255}, a));
        DrawRectangleRoundedLinesEx(in, 0.07f, 10, hover ? 2.5f : 1.5f, alphaMul(kGold, (hover ? 0.95f : 0.55f) * a));
        drawGameEmblem(gk, {r.x + r.width * 0.5f, r.y + 66.f});
        drawTextCentered(FontId::Sign, info.name, {r.x + r.width * 0.5f + 2.f, r.y + 140.f}, 40.f, rgba(0, 0, 0, 0.5f * a));
        drawTextCentered(FontId::Sign, info.name, {r.x + r.width * 0.5f, r.y + 137.f}, 40.f, alphaMul(kGold, a));
        drawTextCentered(FontId::UiBold, info.players, {r.x + r.width * 0.5f, r.y + 172.f}, 17.f,
                         alphaMul(pal::Brass, 0.9f * a), 2.f);
        drawTextWrapped(FontId::Ui, info.blurb, {r.x + 18.f, r.y + 190.f, r.width - 36.f, 70.f}, 15.f,
                        alphaMul(pal::TextLight, 0.78f * a), 2.f);
        if (!avail) {
            DrawRectangleRounded(in, 0.07f, 10, rgba(20, 10, 6, 0.55f * a));
            const Rectangle stamp{r.x + r.width * 0.5f - 74.f, r.y + 46.f, 148.f, 40.f};
            rlPushMatrix();
            rlTranslatef(stamp.x + stamp.width * 0.5f, stamp.y + stamp.height * 0.5f, 0.f);
            rlRotatef(-8.f, 0.f, 0.f, 1.f);
            DrawRectangleRoundedLinesEx({-stamp.width * 0.5f, -stamp.height * 0.5f, stamp.width, stamp.height}, 0.3f, 8,
                                        2.5f, alphaMul(kMarginRed, 0.9f * a));
            drawTextCentered(FontId::UiBold, "YAKINDA", {0.f, -2.f}, 24.f, alphaMul(kMarginRed, 0.95f * a), 3.f);
            rlPopMatrix();
        }
        if (avail && hit(hitR, C_GameCard + 1000 + k, m)) click(C_GameCard, k);
    }
    if (drawButton(L::SelBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_SelBack);
    drawText(FontId::Ui, "Kurallar için ana menüdeki Kurallar düğmesi", {L::SelBack.x + L::SelBack.width + 40.f, 806.f},
             17.f, alphaMul(pal::TextLight, 0.5f));
}

void Screens::Impl::drawGuide(Vector2 m) {
    const float a = clamp01(ageOf(ScreenId::Guide) / 0.3f);
    drawDim(0.45f * a);
    const float W = 860.f;
    float h = 150.f;
    std::vector<float>& lineH = guideLineH; // (a member: no allocation per frame)
    lineH.resize(guideLines.size());
    const size_t nLines = guideLines.size();
    for (size_t i = 0; i < nLines; ++i) {
        // (the same wrap as below; measured from y -4000 like the off-screen pass it replaced, so the card's
        // layout stays bit-identical)
        lineH[i] = measureWrapped(FontId::Ui, guideLines[i], W - 150.f, 22.f, 6.f, -4000.f) + 12.f;
        h += lineH[i];
    }
    h += 90.f;
    const Rectangle P{800.f - W * 0.5f, 450.f - h * 0.5f + (1.f - easeOutCubic(a)) * 24.f, W, h};
    drawPanel(P, PanelStyle::Wood);
    drawHeader(guideTitle, {800.f, P.y + 62.f}, 46.f, alphaMul(kGold, a));
    brassRule(P.x + 60.f, P.x + P.width - 60.f, P.y + 104.f);
    float y = P.y + 130.f;
    for (size_t i = 0; i < nLines; ++i) {
        const float la = clamp01((ageOf(ScreenId::Guide) - 0.12f - 0.08f * (float)i) / 0.3f);
        DrawCircleV({P.x + 70.f, y + 15.f}, 5.f, alphaMul(pal::Brass, la));
        drawTextWrapped(FontId::Ui, guideLines[i], {P.x + 92.f, y, W - 150.f, lineH[i]}, 22.f,
                        alphaMul(pal::TextLight, 0.92f * la), 6.f);
        y += lineH[i];
    }
    const Rectangle ok{800.f - 120.f, P.y + P.height - 82.f, 240.f, 58.f};
    if (drawButton(ok, "Anladım", m, true, ButtonStyle::Wood, 28.f)) click(C_GuideOk);
    drawText(FontId::Ui, "Kurallar: Menü > Kurallar", {P.x + 40.f, P.y + P.height - 46.f}, 15.f,
             alphaMul(pal::TextLight, 0.45f * a));
}

void Screens::Impl::drawStats(Vector2 m) {
    const float age = ageOf(ScreenId::Stats);
    static const StatsBook kEmpty;
    const StatsBook& book = stats ? *stats : kEmpty;
    drawDim(0.6f);
    const Rectangle P = L::StatsPanel;
    drawPanel(P, PanelStyle::Wood);
    drawHeader("Kahvehane Defteri", {800.f, 94.f}, 54.f, kGold);
    brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);

    // --- rank: the title the regulars give you, and how far the next one is
    const int pts = book.rankPoints();
    const Rank& rank = StatsBook::rankFor(pts);
    const Rank* next = StatsBook::nextRank(pts);
    const float a = clamp01(age / 0.35f);
    drawText(FontId::UiBold, "RÜTBEN", {P.x + 70.f, 162.f}, 17.f, alphaMul(pal::Brass, 0.9f * a), 4.f);
    drawTextShadow(FontId::Sign, rank.name, {P.x + 70.f, 184.f}, 46.f, alphaMul(kGold, a));
    drawText(FontId::Ui, rank.line, {P.x + 72.f, 238.f}, 19.f, alphaMul(pal::TextLight, 0.72f * a));
    {
        const float bx = P.x + 640.f, by = 196.f, bw = 380.f, bh = 16.f;
        const int lo = rank.points, hi = next ? next->points : rank.points;
        const float f = next ? clamp01((float)(pts - lo) / (float)std::max(1, hi - lo)) : 1.f;
        DrawRectangleRounded({bx, by, bw, bh}, 1.f, 8, rgba(20, 10, 6, 0.8f));
        if (f > 0.f) DrawRectangleRounded({bx, by, std::max(bh, bw * f * easeOutCubic(a)), bh}, 1.f, 8, pal::Brass);
        DrawRectangleRoundedLinesEx({bx, by, bw, bh}, 1.f, 8, 1.2f, alphaMul(kGold, 0.6f));
        const std::string ptsLine = std::to_string(pts) + " puan";
        drawText(FontId::UiBold, ptsLine, {bx, by - 30.f}, 20.f, alphaMul(pal::TextLight, a));
        const std::string nextLine = next ? std::string(next->name) + " için " + std::to_string(next->points - pts) +
                                                " puan daha"
                                          : std::string("En yüksek rütbe!");
        drawText(FontId::Ui, nextLine, {bx + bw - measureText(FontId::Ui, nextLine, 17.f).x, by - 27.f}, 17.f,
                 alphaMul(pal::TextLight, 0.7f * a));
        drawText(FontId::Ui, "Maç galibiyeti: Acemi'ye karşı 1, Usta'ya 2, Kurt'a 3 puan", {bx, by + 26.f}, 15.f,
                 alphaMul(pal::TextLight, 0.5f * a));
    }

    // --- the table: one line per game, the total last
    std::vector<int> shown;
    for (int k = 0; k < STATS_GAMES; ++k)
        if (gameAvailable((GameKind)k)) shown.push_back(k);
    const float x0 = P.x + 60.f, top = 296.f, rowH = shown.size() <= 7 ? 46.f : 32.f;
    struct Col {
        const char* title;
        float x;    // right edge of the numbers (the game name: left edge)
    };
    const Col cols[] = {{"Oyun", x0 + 10.f},        {"Maç", x0 + 300.f},        {"Galibiyet", x0 + 420.f},
                        {"Oran", x0 + 520.f},       {"El / Oyun", x0 + 660.f},  {"En uzun seri", x0 + 800.f},
                        {"Rekor", x0 + 970.f}};
    for (size_t c = 0; c < sizeof cols / sizeof cols[0]; ++c) {
        const float w = measureText(FontId::UiBold, cols[c].title, 16.f, 2.f).x;
        drawText(FontId::UiBold, cols[c].title, {c == 0 ? cols[c].x : cols[c].x - w, top}, 16.f,
                 alphaMul(pal::Brass, 0.9f), 2.f);
    }
    DrawLineEx({x0, top + 28.f}, {x0 + 980.f, top + 28.f}, 1.2f, alphaMul(pal::Brass, 0.45f));
    auto num = [&](float right, float y, const std::string& t, Color c, FontId f = FontId::Ui) {
        drawText(f, t, {right - measureText(f, t, 21.f).x, y}, 21.f, c);
    };
    auto pct = [](int a, int b) { return b > 0 ? std::to_string((a * 100 + b / 2) / b) + "%" : std::string("-"); };
    const int sel = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
    for (int row = 0; row <= (int)shown.size(); ++row) {
        const bool totalRow = row == (int)shown.size();
        const int k = totalRow ? STATS_GAMES : shown[(size_t)row];
        const GameRecord r = totalRow ? book.total() : book.games[(size_t)k];
        const float ra = clamp01((age - 0.04f * (float)row) / 0.3f);
        const float y = top + 40.f + (float)row * rowH + (totalRow ? 10.f : 0.f) + (1.f - easeOutCubic(ra)) * 14.f;
        if (totalRow) DrawLineEx({x0, y - 8.f}, {x0 + 980.f, y - 8.f}, 1.2f, alphaMul(pal::Brass, 0.45f * ra));
        else if (row % 2 == 0)
            DrawRectangleRounded({x0 - 10.f, y - 8.f, 1000.f, rowH - 4.f}, 0.3f, 6, rgba(255, 220, 160, 0.04f * ra));
        const bool none = r.matches == 0 && r.hands == 0;
        const Color tc = alphaMul(none ? alphaMul(pal::TextLight, 0.45f) : pal::TextLight, ra);
        const std::string name = totalRow ? "Toplam" : gameInfo((GameKind)k).name;
        drawText(FontId::UiBold, name, {cols[0].x, y}, 22.f, alphaMul(totalRow || k == sel ? kGold : pal::TextLight, ra));
        if (none && !totalRow) {
            drawText(FontId::Ui, "henüz oynanmadı", {cols[1].x - 60.f, y + 2.f}, 18.f, alphaMul(pal::TextLight, 0.4f * ra));
            continue;
        }
        num(cols[1].x, y, std::to_string(r.matches), tc);
        num(cols[2].x, y, std::to_string(r.wins), tc, FontId::UiBold);
        num(cols[3].x, y, pct(r.wins, r.matches), tc);
        num(cols[4].x, y, std::to_string(r.handWins) + " / " + std::to_string(r.hands), tc);
        num(cols[5].x, y, std::to_string(r.bestStreak) + (r.streak > 1 ? "  (şimdi " + std::to_string(r.streak) + ")" : ""), tc);
        if (!totalRow && r.hasBest && StatsBook::bestLabel(k) && rowH >= 40.f) {
            const std::string b = std::to_string(r.best);
            num(cols[6].x, y, b, alphaMul(kGold, ra), FontId::UiBold);
            const char* bl = StatsBook::bestLabel(k);
            drawText(FontId::Ui, bl, {cols[6].x - measureText(FontId::Ui, bl, 13.f).x, y + 24.f}, 13.f,
                     alphaMul(pal::TextLight, 0.45f * ra));
        } else if (!totalRow && r.hasBest && StatsBook::bestLabel(k)) {
            num(cols[6].x, y, std::to_string(r.best), alphaMul(kGold, ra), FontId::UiBold);
        } else if (!totalRow) {
            num(cols[6].x, y, "-", alphaMul(pal::TextLight, 0.4f * ra));
        } else {
            const std::string lv = "Acemi " + std::to_string(r.winsAt[0]) + "/" + std::to_string(r.playedAt[0]) +
                                   "   Usta " + std::to_string(r.winsAt[1]) + "/" + std::to_string(r.playedAt[1]) +
                                   "   Kurt " + std::to_string(r.winsAt[2]) + "/" + std::to_string(r.playedAt[2]);
            drawText(FontId::Ui, lv, {x0 + 10.f, y + 34.f}, 17.f, alphaMul(pal::TextLight, 0.65f * ra));
        }
    }
    drawTextCentered(FontId::Ui, "Yalnızca kendin oynadığın, sonuna kadar biten maçlar deftere yazılır.",
                     {800.f, 752.f}, 16.f, alphaMul(pal::TextLight, 0.5f));
    if (drawButton({L::StatsBack.x - 260.f, L::StatsBack.y, L::StatsBack.width, L::StatsBack.height}, "Başarımlar", m,
                   true, ButtonStyle::Wood, 26.f))
        click(C_StatsAchievements);
    if (drawButton(L::StatsBack, "Tekrarlar", m, true, ButtonStyle::Wood, 26.f)) click(C_StatsReplays);
    if (drawButton({L::StatsBack.x + 260.f, L::StatsBack.y, L::StatsBack.width, L::StatsBack.height}, "Geri", m, true,
                   ButtonStyle::Wood, 28.f))
        click(C_StatsBack);
}

void Screens::Impl::drawAnalysis(Vector2 m) {
    const float age = ageOf(ScreenId::Analysis);
    drawDim(0.62f);
    const Rectangle P = L::StatsPanel;
    drawPanel(P, PanelStyle::Wood);
    drawHeader("Hatalarım", {800.f, 94.f}, 54.f, kGold);
    brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);
    drawTextCentered(FontId::Ui, analysisTitle, {800.f, 160.f}, 18.f, alphaMul(pal::TextLight, 0.6f));
    if (!analysisReady) {
        const std::string dots(1 + (int)(time * 2.f) % 3, '.');
        drawTextCentered(FontId::UiBold, "Kurt maçı inceliyor" + dots, {800.f, 420.f}, 28.f, pal::TextLight);
        drawTextCentered(FontId::Ui, "Her hamlende onun yerinde ne yapacağına bakıyor.", {800.f, 462.f}, 18.f,
                         alphaMul(pal::TextLight, 0.6f));
    } else if (analysis.empty()) {
        drawTextCentered(FontId::UiBold, "Kayda değer bir hata yok!", {800.f, 400.f}, 30.f, kGold);
        drawTextCentered(FontId::Ui, "Kurt da bu maçı senin gibi oynardı. Helal olsun.", {800.f, 446.f}, 19.f,
                         alphaMul(pal::TextLight, 0.7f));
    } else {
        for (size_t i = 0; i < analysis.size() && i < 3; ++i) {
            const MistakeView& e = analysis[i];
            const float a = clamp01((age - 0.1f * (float)i) / 0.35f);
            const Rectangle c{P.x + 70.f, 196.f + (float)i * 182.f + (1.f - easeOutCubic(a)) * 16.f, P.width - 140.f, 166.f};
            DrawRectangleRounded(c, 0.08f, 8, rgba(20, 10, 6, 0.45f * a));
            DrawRectangleRoundedLinesEx(c, 0.08f, 8, 1.2f, alphaMul(pal::Brass, 0.5f * a));
            drawText(FontId::Sign, std::to_string(i + 1) + ".", {c.x + 22.f, c.y + 16.f}, 44.f, alphaMul(kGold, a));
            drawText(FontId::UiBold, e.when, {c.x + 80.f, c.y + 16.f}, 18.f, alphaMul(pal::Brass, a), 2.f);
            drawText(FontId::UiBold, e.played, {c.x + 80.f, c.y + 46.f}, 24.f, alphaMul(pal::TextLight, a));
            drawText(FontId::Ui, e.better, {c.x + 80.f, c.y + 82.f}, 21.f, alphaMul(pal::Good, a));
            if (!e.why.empty())
                drawTextWrapped(FontId::Ui, e.why, {c.x + 80.f, c.y + 114.f, c.width - 320.f, 48.f}, 17.f,
                                alphaMul(pal::TextLight, 0.65f * a), 2.f);
            if (!e.cost.empty()) {
                const float w = measureText(FontId::UiBold, e.cost, 22.f).x + 30.f;
                const Rectangle b{c.x + c.width - w - 24.f, c.y + 20.f, w, 40.f};
                DrawRectangleRounded(b, 0.5f, 8, alphaMul(kMarginRed, 0.85f * a));
                drawTextCentered(FontId::UiBold, e.cost, {b.x + b.width * 0.5f, b.y + b.height * 0.5f - 1.f}, 22.f,
                                 alphaMul(Color{255, 246, 230, 255}, a));
            }
        }
    }
    if (drawButton(L::StatsBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_AnalysisBack);
}

void Screens::Impl::drawReplays(Vector2 m) {
    const float age = ageOf(ScreenId::Replays);
    drawDim(0.6f);
    const Rectangle P = L::StatsPanel;
    drawPanel(P, PanelStyle::Wood);
    drawHeader("Maç Tekrarları", {800.f, 94.f}, 54.f, kGold);
    brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);
    if (replays.empty()) {
        drawTextCentered(FontId::Ui, "Henüz biten bir maç yok. Bir maçı sonuna kadar oyna, burada izleyebilirsin.",
                         {800.f, 420.f}, 21.f, alphaMul(pal::TextLight, 0.7f));
    }
    const float x0 = P.x + 70.f, top = 168.f, rowH = 58.f;
    const int shown = std::min<int>((int)replays.size(), 10);
    for (int i = 0; i < shown; ++i) {
        const ReplayEntry& e = replays[(size_t)i];
        const float a = clamp01((age - 0.03f * (float)i) / 0.3f);
        const float y = top + (float)i * rowH + (1.f - easeOutCubic(a)) * 12.f;
        if (i % 2 == 0) DrawRectangleRounded({x0 - 14.f, y - 6.f, P.width - 112.f, rowH - 6.f}, 0.3f, 6, rgba(255, 220, 160, 0.04f * a));
        drawText(FontId::UiBold, e.game, {x0, y + 6.f}, 24.f, alphaMul(kGold, a));
        drawText(FontId::Ui, e.date, {x0 + 200.f, y + 9.f}, 19.f, alphaMul(pal::TextLight, 0.7f * a));
        const bool won = e.result == "Kazandın";
        drawText(FontId::UiBold, e.result, {x0 + 420.f, y + 8.f}, 21.f, alphaMul(won ? pal::Good : pal::TextLight, a));
        drawText(FontId::Ui, std::to_string(e.actions) + " hamle", {x0 + 690.f, y + 10.f}, 17.f, alphaMul(pal::TextLight, 0.5f * a));
        const Rectangle b{P.x + P.width - 210.f, y, 130.f, 42.f};
        if (drawButton(b, "İzle", m, true, ButtonStyle::Wood, 22.f)) click(C_ReplayWatch, i);
        const Rectangle an{b.x - 140.f, y, 130.f, 42.f};
        if (drawButton(an, "Analiz", m, true, ButtonStyle::Wood, 22.f)) click(C_ReplayAnalyze, i);
    }
    drawTextCentered(FontId::Ui, "İzlerken: Boşluk durdurur, ok tuşları hızı değiştirir, ESC menüyü açar.", {800.f, 752.f},
                     16.f, alphaMul(pal::TextLight, 0.5f));
    if (drawButton(L::StatsBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_ReplaysBack);
}

void Screens::Impl::drawBadgeIcon(BadgeIcon icon, Vector2 c, float s, float a) const {
    const Color gold = alphaMul(kGold, a), red = alphaMul(Color{196, 44, 40, 255}, a);
    const Color cream = alphaMul(Color{240, 230, 206, 255}, a), dark = alphaMul(Color{40, 26, 18, 255}, a);
    auto tile = [&](Vector2 at, float h, int color, int number, int kind) {
        drawMiniTile({at.x - h * 0.37f, at.y - h * 0.5f, h * 0.74f, h}, MiniTile{color, number, kind, false});
    };
    auto checker = [&](Vector2 at, float r, Color body) {
        DrawCircleV({at.x + 1.f, at.y + 2.f}, r, alphaMul(Color{0, 0, 0, 255}, 0.35f * a));
        DrawCircleV(at, r, body);
        DrawRing(at, r * 0.55f, r * 0.68f, 0.f, 360.f, 24, alphaMul(Color{0, 0, 0, 255}, 0.25f * a));
        DrawRing(at, r * 0.9f, r, 0.f, 360.f, 24, alphaMul(Color{255, 255, 255, 255}, 0.18f * a));
    };
    switch (icon) {
    case BadgeIcon::Okey: tile(c, s * 0.82f, 3, 9, 1); break;
    case BadgeIcon::Indicator:
        drawMiniTile({c.x - s * 0.27f, c.y - s * 0.37f, s * 0.54f, s * 0.74f}, MiniTile{1, 4, 0, true});
        break;
    case BadgeIcon::CheckerStar:
        checker(c, s * 0.36f, cream);
        drawStar(c, s * 0.16f, gold);
        break;
    case BadgeIcon::Rose:
        for (int k = 0; k < 5; ++k) {
            const float an = (float)k * 72.f * DEG2RAD - PI * 0.5f;
            DrawCircleV({c.x + std::cos(an) * s * 0.15f, c.y - s * 0.04f + std::sin(an) * s * 0.15f}, s * 0.13f,
                        alphaMul(Color{196, 52, 70, 255}, a));
        }
        DrawCircleV({c.x, c.y - s * 0.04f}, s * 0.09f, alphaMul(Color{150, 26, 46, 255}, a));
        DrawLineEx({c.x, c.y + s * 0.12f}, {c.x + s * 0.03f, c.y + s * 0.36f}, 2.5f, alphaMul(Color{96, 150, 70, 255}, a));
        break;
    case BadgeIcon::CardEight: drawMiniCard(c, s * 0.5f, -6.f, "8", 0); break;
    case BadgeIcon::Bezik:
        drawMiniCard({c.x - s * 0.13f, c.y + s * 0.02f}, s * 0.46f, -14.f, "K", 0);
        drawMiniCard({c.x + s * 0.13f, c.y - s * 0.02f}, s * 0.46f, 12.f, "V", 2);
        break;
    case BadgeIcon::Deck:
        for (int k = 3; k >= 0; --k) {
            const Rectangle rr{c.x - s * 0.21f + (float)k * 1.5f, c.y - s * 0.3f + (float)k * 1.5f, s * 0.42f, s * 0.6f};
            DrawRectangleRounded(rr, 0.15f, 6, cream);
            DrawRectangleRounded({rr.x + 3.f, rr.y + 3.f, rr.width - 6.f, rr.height - 6.f}, 0.12f, 6,
                                 alphaMul(Color{150, 34, 36, 255}, a));
        }
        break;
    case BadgeIcon::Fan:
        drawMiniCard({c.x - s * 0.2f, c.y + s * 0.05f}, s * 0.4f, -22.f, "3", 3);
        drawMiniCard({c.x, c.y - s * 0.02f}, s * 0.4f, 0.f, "4", 3);
        drawMiniCard({c.x + s * 0.2f, c.y + s * 0.05f}, s * 0.4f, 22.f, "5", 3);
        break;
    case BadgeIcon::Medal:
        tri({c.x - s * 0.2f, c.y - s * 0.36f}, {c.x - s * 0.04f, c.y - s * 0.36f}, {c.x + s * 0.04f, c.y}, red);
        tri({c.x + s * 0.04f, c.y - s * 0.36f}, {c.x + s * 0.2f, c.y - s * 0.36f}, {c.x - s * 0.04f, c.y}, red);
        DrawCircleV({c.x, c.y + s * 0.1f}, s * 0.2f, gold);
        DrawRing({c.x, c.y + s * 0.1f}, s * 0.14f, s * 0.16f, 0.f, 360.f, 24, alphaMul(Color{150, 100, 40, 255}, a));
        drawStar({c.x, c.y + s * 0.1f}, s * 0.08f, alphaMul(Color{150, 100, 40, 255}, a));
        break;
    case BadgeIcon::Sunrise:
        for (int k = 0; k < 5; ++k) {
            const float an = (180.f + 22.5f + 33.75f * (float)k) * DEG2RAD;
            DrawLineEx({c.x + std::cos(an) * s * 0.2f, c.y + s * 0.08f + std::sin(an) * s * 0.2f},
                       {c.x + std::cos(an) * s * 0.32f, c.y + s * 0.08f + std::sin(an) * s * 0.32f}, 2.f, gold);
        }
        DrawCircleSector({c.x, c.y + s * 0.08f}, s * 0.16f, 180.f, 360.f, 16, gold);
        DrawLineEx({c.x - s * 0.34f, c.y + s * 0.09f}, {c.x + s * 0.34f, c.y + s * 0.09f}, 2.f, cream);
        drawStar({c.x - s * 0.22f, c.y - s * 0.26f}, s * 0.04f, cream);
        break;
    case BadgeIcon::Pairs:
        tile({c.x - s * 0.17f, c.y}, s * 0.7f, 0, 7, 0);
        tile({c.x + s * 0.17f, c.y}, s * 0.7f, 0, 7, 0);
        break;
    case BadgeIcon::Tile: tile(c, s * 0.82f, 2, 13, 0); break;
    case BadgeIcon::Number:
        DrawRectangleRounded({c.x - s * 0.42f, c.y - s * 0.3f, s * 0.84f, s * 0.6f}, 0.25f, 6, cream);
        drawTextCentered(FontId::Tile, "101", {c.x, c.y - 1.f}, s * 0.42f, red);
        break;
    case BadgeIcon::Checker: checker(c, s * 0.36f, cream); break;
    case BadgeIcon::Checkers:
        checker({c.x - s * 0.12f, c.y + s * 0.1f}, s * 0.3f, alphaMul(Color{60, 34, 24, 255}, a));
        checker({c.x + s * 0.12f, c.y - s * 0.08f}, s * 0.3f, cream);
        break;
    case BadgeIcon::Dice:
        drawMiniDie({c.x - s * 0.16f, c.y + s * 0.06f}, s * 0.42f, 6, -12.f);
        drawMiniDie({c.x + s * 0.18f, c.y - s * 0.08f}, s * 0.42f, 6, 14.f);
        break;
    case BadgeIcon::Card: drawMiniCard(c, s * 0.5f, -6.f, "A", 0); break;
    case BadgeIcon::Cards:
        drawMiniCard({c.x - s * 0.13f, c.y + s * 0.02f}, s * 0.46f, -14.f, "7", 2);
        drawMiniCard({c.x + s * 0.13f, c.y - s * 0.02f}, s * 0.46f, 12.f, "7", 1);
        break;
    case BadgeIcon::Jack: drawMiniCard(c, s * 0.5f, 6.f, "V", 1); break;
    case BadgeIcon::Spade: drawSuit({c.x, c.y - s * 0.02f}, s * 0.72f, 0, cream); break;
    case BadgeIcon::Heart: drawSuit({c.x, c.y + s * 0.04f}, s * 0.62f, 1, red); break;
    case BadgeIcon::Crown: {
        const float w = s * 0.62f, h = s * 0.44f, b = c.y + h * 0.5f;
        DrawRectangleRec({c.x - w * 0.5f, b - h * 0.28f, w, h * 0.28f}, gold);
        tri({c.x - w * 0.5f, b - h * 0.28f}, {c.x - w * 0.18f, b - h * 0.28f}, {c.x - w * 0.5f, b - h}, gold);
        tri({c.x - w * 0.3f, b - h * 0.28f}, {c.x + w * 0.3f, b - h * 0.28f}, {c.x, b - h * 1.1f}, gold);
        tri({c.x + w * 0.18f, b - h * 0.28f}, {c.x + w * 0.5f, b - h * 0.28f}, {c.x + w * 0.5f, b - h}, gold);
        for (float dx : {-0.5f, 0.f, 0.5f}) DrawCircleV({c.x + w * dx, b - h * (dx == 0.f ? 1.1f : 1.f)}, s * 0.04f, gold);
        DrawCircleV({c.x, b - h * 0.14f}, s * 0.035f, red);
        break;
    }
    case BadgeIcon::Disc:
        checker(c, s * 0.36f, alphaMul(Color{150, 30, 28, 255}, a));
        drawStar(c, s * 0.15f, gold);
        break;
    case BadgeIcon::Trophy: {
        const float w = s * 0.5f;
        DrawCircleSector({c.x, c.y - s * 0.14f}, w * 0.5f, 0.f, 180.f, 16, gold);
        DrawRectangleRec({c.x - w * 0.5f, c.y - s * 0.3f, w, s * 0.16f}, gold);
        DrawRing({c.x - w * 0.5f, c.y - s * 0.12f}, s * 0.08f, s * 0.12f, 90.f, 270.f, 10, gold);
        DrawRing({c.x + w * 0.5f, c.y - s * 0.12f}, s * 0.08f, s * 0.12f, -90.f, 90.f, 10, gold);
        DrawRectangleRec({c.x - s * 0.04f, c.y + s * 0.08f, s * 0.08f, s * 0.14f}, gold);
        DrawRectangleRounded({c.x - s * 0.18f, c.y + s * 0.2f, s * 0.36f, s * 0.09f}, 0.4f, 4, gold);
        break;
    }
    case BadgeIcon::Star: drawStar(c, s * 0.36f, gold); break;
    case BadgeIcon::Flame:
        DrawCircleV({c.x, c.y + s * 0.1f}, s * 0.2f, alphaMul(Color{230, 96, 30, 255}, a));
        tri({c.x - s * 0.2f, c.y + s * 0.08f}, {c.x + s * 0.2f, c.y + s * 0.08f}, {c.x + s * 0.04f, c.y - s * 0.34f},
            alphaMul(Color{230, 96, 30, 255}, a));
        DrawCircleV({c.x, c.y + s * 0.15f}, s * 0.11f, alphaMul(Color{252, 206, 80, 255}, a));
        tri({c.x - s * 0.11f, c.y + s * 0.13f}, {c.x + s * 0.11f, c.y + s * 0.13f}, {c.x - s * 0.02f, c.y - s * 0.1f},
            alphaMul(Color{252, 206, 80, 255}, a));
        break;
    case BadgeIcon::People:
        for (int i = -1; i <= 1; ++i) {
            const Vector2 h{c.x + (float)i * s * 0.22f, c.y - s * (i == 0 ? 0.14f : 0.08f)};
            DrawRectangleRounded({h.x - s * 0.1f, h.y + s * 0.1f, s * 0.2f, s * 0.24f}, 0.5f, 6,
                                 alphaMul(i == 0 ? kGold : pal::Brass, a));
            DrawCircleV(h, s * 0.085f, cream);
        }
        break;
    case BadgeIcon::Tea: drawTeaGlass({c.x, c.y + s * 0.3f}, s * 0.0068f, time, 18.f); break;
    case BadgeIcon::Calendar:
        DrawRectangleRounded({c.x - s * 0.3f, c.y - s * 0.28f, s * 0.6f, s * 0.58f}, 0.15f, 6, cream);
        DrawRectangleRounded({c.x - s * 0.3f, c.y - s * 0.28f, s * 0.6f, s * 0.16f}, 0.3f, 6, red);
        drawTextCentered(FontId::UiBold, "7", {c.x, c.y + s * 0.08f}, s * 0.32f, dark);
        break;
    case BadgeIcon::Moon:
        DrawCircleV(c, s * 0.3f, gold);
        DrawCircleV({c.x + s * 0.13f, c.y - s * 0.08f}, s * 0.25f, alphaMul(Color{86, 38, 22, 255}, a));
        drawStar({c.x + s * 0.2f, c.y + s * 0.18f}, s * 0.06f, cream);
        break;
    case BadgeIcon::Leaf: {
        const Color sc[4] = {Color{120, 188, 92, 255}, Color{246, 196, 64, 255}, Color{214, 110, 44, 255},
                             Color{226, 232, 240, 255}};
        for (int q = 0; q < 4; ++q)
            DrawCircleSector(c, s * 0.32f, -90.f + 90.f * (float)q, 90.f * (float)q, 10, alphaMul(sc[q], a));
        DrawRing(c, s * 0.32f, s * 0.35f, 0.f, 360.f, 24, gold);
        break;
    }
    case BadgeIcon::Pencil:
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0.f);
        rlRotatef(-38.f, 0.f, 0.f, 1.f);
        rlScalef(s * 0.0034f, s * 0.011f, 1.f);
        drawPencil({0.f, 0.f}, 0.f);
        rlPopMatrix();
        break;
    case BadgeIcon::Eye:
        DrawEllipse((int)c.x, (int)c.y, s * 0.36f, s * 0.2f, cream);
        DrawCircleV(c, s * 0.15f, alphaMul(Color{70, 120, 150, 255}, a));
        DrawCircleV(c, s * 0.07f, dark);
        DrawCircleV({c.x - s * 0.04f, c.y - s * 0.05f}, s * 0.025f, cream);
        break;
    case BadgeIcon::Hourglass:
        DrawRectangleRec({c.x - s * 0.26f, c.y - s * 0.34f, s * 0.52f, s * 0.06f}, gold);
        DrawRectangleRec({c.x - s * 0.26f, c.y + s * 0.28f, s * 0.52f, s * 0.06f}, gold);
        tri({c.x - s * 0.2f, c.y - s * 0.28f}, {c.x, c.y}, {c.x + s * 0.2f, c.y - s * 0.28f}, cream);
        tri({c.x, c.y}, {c.x - s * 0.2f, c.y + s * 0.28f}, {c.x + s * 0.2f, c.y + s * 0.28f}, cream);
        tri({c.x - s * 0.12f, c.y + s * 0.28f}, {c.x + s * 0.12f, c.y + s * 0.28f}, {c.x, c.y + s * 0.12f}, gold);
        break;
    case BadgeIcon::Cloud:
        for (int i = 0; i < 3; ++i)
            DrawLineEx({c.x - s * 0.14f + (float)i * s * 0.14f, c.y + s * 0.12f},
                       {c.x - s * 0.18f + (float)i * s * 0.14f, c.y + s * 0.3f}, 2.f,
                       alphaMul(Color{120, 160, 210, 255}, a));
        DrawCircleV({c.x - s * 0.15f, c.y}, s * 0.15f, alphaMul(Color{170, 170, 176, 255}, a));
        DrawCircleV({c.x + s * 0.02f, c.y - s * 0.08f}, s * 0.19f, alphaMul(Color{186, 186, 192, 255}, a));
        DrawCircleV({c.x + s * 0.18f, c.y + s * 0.02f}, s * 0.14f, alphaMul(Color{170, 170, 176, 255}, a));
        DrawRectangleRec({c.x - s * 0.15f, c.y, s * 0.33f, s * 0.15f}, alphaMul(Color{170, 170, 176, 255}, a));
        break;
    }
}

void Screens::Impl::drawBadge(int i, Vector2 c, float r, bool open, float a) const {
    DrawCircleV({c.x + 1.5f, c.y + 3.f}, r + 2.f, rgba(0, 0, 0, 0.4f * a));
    if (!open) {
        DrawCircleV(c, r, rgba(32, 22, 16, 0.95f * a));
        DrawRing(c, r - 3.f, r, 0.f, 360.f, 36, alphaMul(pal::Brass, 0.35f * a));
        drawTextCentered(FontId::Sign, "?", {c.x, c.y + 1.f}, r * 1.15f, alphaMul(pal::TextLight, 0.35f * a));
        return;
    }
    DrawCircleV(c, r, alphaMul(pal::Brass, a));
    DrawCircleV(c, r * 0.84f, alphaMul(Color{86, 38, 22, 255}, a));
    DrawRing(c, r * 0.84f, r * 0.88f, 0.f, 360.f, 36, alphaMul(kGold, 0.8f * a));
    DrawRing(c, r - 1.5f, r, 0.f, 360.f, 36, alphaMul(kGold, a));
    drawBadgeIcon(Achievements::def(i).icon, c, r * 1.6f, a);
}

void Screens::Impl::drawAchievements(Vector2 m) {
    const float age = ageOf(ScreenId::Achievements);
    static const Achievements kNone;
    const Achievements& book = achievements ? *achievements : kNone;
    drawDim(0.6f);
    const Rectangle P = L::StatsPanel;
    drawPanel(P, PanelStyle::Wood);
    drawHeader("Başarımlar", {800.f, 94.f}, 54.f, kGold);
    brassRule(P.x + 50.f, P.x + P.width - 50.f, 136.f);
    const int n = Achievements::count(), done = book.unlockedCount();
    {
        const std::string line = std::to_string(done) + " / " + std::to_string(n) + " başarım açıldı";
        const float bw = 260.f, bx = 800.f - bw * 0.5f, by = 168.f;
        drawTextCentered(FontId::UiBold, line, {800.f, 154.f}, 19.f, pal::TextLight);
        DrawRectangleRounded({bx, by, bw, 6.f}, 1.f, 6, rgba(20, 10, 6, 0.8f));
        if (done > 0) DrawRectangleRounded({bx, by, std::max(6.f, bw * (float)done / (float)n), 6.f}, 1.f, 6, pal::Brass);
    }
    // the pages
    static const char* const kTabs[3] = {"Taş ve Tahta", "Kâğıt Oyunları", "Kahvehane"};
    int inTab[3] = {}, openTab[3] = {};
    for (int i = 0; i < n; ++i) {
        const int g = std::clamp(Achievements::def(i).group, 0, 2);
        ++inTab[g];
        openTab[g] += book.unlocked(i) ? 1 : 0;
    }
    {
        const float fs = 19.f, h = 34.f, gap = 10.f, y = 204.f;
        std::string label[3];
        float w[3], total = 0.f;
        for (int t = 0; t < 3; ++t) {
            label[t] = std::string(kTabs[t]) + "  " + std::to_string(openTab[t]) + "/" + std::to_string(inTab[t]);
            w[t] = measureText(FontId::Chalk, label[t], fs).x + 34.f;
            total += w[t] + (t ? gap : 0.f);
        }
        float x = 800.f - total * 0.5f;
        for (int t = 0; t < 3; ++t) {
            const Rectangle r{x, y - h * 0.5f, w[t], h};
            if (t == achTab)
                DrawRectangleRounded({r.x - 2.f, r.y - 2.f, r.width + 4.f, r.height + 4.f}, 0.35f, 8,
                                     alphaMul(pal::Highlight, 0.85f));
            if (drawButton(r, label[t], m, true, ButtonStyle::Chalk, fs)) click(C_AchievementsTab, t);
            x += w[t] + gap;
        }
    }
    // the badges of the page: two columns
    std::vector<int> shown;
    for (int i = 0; i < n; ++i)
        if (Achievements::def(i).group == achTab) shown.push_back(i);
    const float colW = 480.f, colGap = 20.f, x0 = P.x + 60.f, top = 236.f, rowH = 74.f, cellH = 68.f;
    const int rows = ((int)shown.size() + 1) / 2;
    for (int k = 0; k < (int)shown.size(); ++k) {
        const int i = shown[(size_t)k];
        const AchievementDef& d = Achievements::def(i);
        const bool open = book.unlocked(i), hidden = d.secret && !open;
        const int col = k / rows, row = k % rows;
        const float a = clamp01((age - 0.025f * (float)k) / 0.3f);
        const Rectangle c{x0 + (float)col * (colW + colGap), top + (float)row * rowH + (1.f - easeOutCubic(a)) * 10.f,
                          colW, cellH};
        DrawRectangleRounded(c, 0.18f, 8, open ? rgba(255, 214, 140, 0.09f * a) : rgba(20, 10, 6, 0.32f * a));
        DrawRectangleRoundedLinesEx(c, 0.18f, 8, 1.2f, alphaMul(pal::Brass, (open ? 0.6f : 0.22f) * a));
        drawBadge(i, {c.x + 38.f, c.y + c.height * 0.5f}, 26.f, open, a);
        const std::string name = hidden ? std::string("Gizli Başarım") : std::string(d.name);
        const std::string desc = hidden ? std::string("Bunu kendin keşfetmelisin.") : std::string(d.desc);
        drawText(FontId::UiBold, name, {c.x + 76.f, c.y + 7.f}, 20.f,
                 alphaMul(open ? kGold : alphaMul(pal::TextLight, 0.62f), a));
        drawTextWrapped(FontId::Ui, desc, {c.x + 76.f, c.y + 32.f, c.width - 88.f, 36.f}, 15.f,
                        alphaMul(pal::TextLight, (open ? 0.8f : 0.5f) * a));
        // the right of the name line: the date it opened, or how far it has come
        const float right = c.x + c.width - 14.f;
        if (open) {
            const std::string date = book.unlockedDate(i);
            drawText(FontId::Ui, date, {right - measureText(FontId::Ui, date, 14.f).x, c.y + 10.f}, 14.f,
                     alphaMul(pal::Brass, a));
        } else if (!hidden && book.goal(i) > 1) {
            const int p = book.progress(i), g = book.goal(i);
            const std::string t = std::to_string(p) + "/" + std::to_string(g);
            const float bw = 70.f, bx = right - bw, by = c.y + 15.f;
            DrawRectangleRounded({bx, by, bw, 7.f}, 1.f, 6, rgba(20, 10, 6, 0.85f * a));
            if (p > 0)
                DrawRectangleRounded({bx, by, std::max(7.f, bw * (float)p / (float)g), 7.f}, 1.f, 6,
                                     alphaMul(pal::Brass, a));
            drawText(FontId::UiBold, t, {bx - 8.f - measureText(FontId::UiBold, t, 14.f).x, c.y + 9.f}, 14.f,
                     alphaMul(pal::TextLight, 0.7f * a));
        }
    }
    drawTextCentered(FontId::Ui, "Yalnızca kendin oynadığın maçlar sayılır  \xC2\xB7  sol / sağ ok: sayfalar",
                     {800.f, 760.f}, 15.f, alphaMul(pal::TextLight, 0.45f));
    if (drawButton({L::StatsBack.x, L::StatsBack.y + 10.f, L::StatsBack.width, L::StatsBack.height - 6.f}, "Geri", m,
                   true, ButtonStyle::Wood, 28.f))
        click(C_AchievementsBack);
}

void Screens::Impl::advanceAchievementBanner() {
    if (bannerIdx >= 0 && time - bannerAt > kBannerShow) bannerIdx = -1;
    if (bannerIdx < 0 && !bannerQueue.empty()) {
        bannerIdx = bannerQueue.front();
        bannerQueue.erase(bannerQueue.begin());
        bannerAt = time;
        sfx(Sfx::Chime);
    }
}

void Screens::Impl::drawAchievementBanner() {
    constexpr float kShow = kBannerShow;
    if (bannerIdx < 0) return;
    const float t = time - bannerAt;
    const float in = easeOutCubic(clamp01(t / 0.45f)), out = clamp01((kShow - t) / 0.5f);
    const float a = std::min(in, out);
    const AchievementDef& d = Achievements::def(bannerIdx);
    std::string name = d.name;
    if (!name.empty() && name.back() != '!') name += "!";
    const float nameW = measureText(FontId::UiBold, name, 30.f).x;
    const float w = std::max(460.f, nameW + 150.f), h = 88.f;
    const Rectangle r{800.f - w * 0.5f, 14.f - (1.f - in) * 120.f, w, h};
    DrawRectangleRounded({r.x + 3.f, r.y + 5.f, r.width, r.height}, 0.3f, 10, rgba(0, 0, 0, 0.4f * a));
    DrawRectangleRounded(r, 0.3f, 10, rgba(34, 20, 12, 0.94f * a));
    DrawRectangleRoundedLinesEx(r, 0.3f, 10, 2.f, alphaMul(kGold, 0.85f * a));
    // a glint passing over it once
    const float gx = r.x + (t - 0.3f) / 0.9f * (r.width + 80.f) - 40.f;
    if (t > 0.3f && t < 1.2f) {
        beginClip({r.x + 8.f, r.y + 3.f, r.width - 16.f, r.height - 6.f});
        DrawRectangleGradientH((int)(gx - 30.f), (int)r.y + 3, 30, (int)r.height - 6, rgba(255, 230, 170, 0.f),
                               rgba(255, 230, 170, 0.14f * a));
        DrawRectangleGradientH((int)gx, (int)r.y + 3, 30, (int)r.height - 6, rgba(255, 230, 170, 0.14f * a),
                               rgba(255, 230, 170, 0.f));
        EndScissorMode();
    }
    drawBadge(bannerIdx, {r.x + 52.f, r.y + h * 0.5f}, 31.f, true, a);
    drawText(FontId::UiBold, "BAŞARIM AÇILDI", {r.x + 100.f, r.y + 14.f}, 15.f, alphaMul(pal::Brass, a), 3.f);
    drawText(FontId::UiBold, name, {r.x + 100.f, r.y + 36.f}, 30.f, alphaMul(kGold, a));
}

void Screens::Impl::drawPaused(Vector2 m, const okey::Game* g) {
    drawDim(0.5f);
    const Rectangle P = L::PausePanel;
    drawPanel(P, PanelStyle::Wood);
    drawTeaGlass({800.f, P.y + 104.f}, 0.62f, time, 70.f);
    drawHeader("Çay Molası", {800.f, P.y + 148.f}, 46.f, kGold);
    if (g && g->handState() != okey::HandState::NotStarted) {
        const std::string s = g->classic() ? "Okey \xC2\xB7 " + std::to_string(g->handIndex() + 1) + ". el"
                                           : "El " + std::to_string(std::min(g->handIndex() + 1, g->numHands())) +
                                                 " / " + std::to_string(g->numHands());
        drawTextCentered(FontId::Ui, s, {800.f, P.y + 191.f}, 20.f, alphaMul(pal::TextLight, 0.7f));
    }
    const Vector2 bm = confirmQuit ? kNoMouse : m;
    const char* labels[5] = {"Devam", aiMode ? "Kontrolü Geri Al" : "Yapay Zeka Oynasın", "Kurallar", "Ayarlar",
                             "Ana Menü"};
    const int ids[5] = {C_Resume, C_PauseAi, C_PauseRules, C_PauseSettings, C_PauseMenu};
    for (int i = 0; i < 5; ++i) {
        if (drawButton(L::PauseBtn[i], labels[i], bm, true, ButtonStyle::Wood, i == 0 ? 30.f : 26.f)) click(ids[i]);
    }
    drawTextCentered(FontId::Ui, "ESC ile oyuna dön  \xC2\xB7  Y ile yapay zeka aç / kapa",
                     {800.f, P.y + P.height - 40.f}, 17.f, alphaMul(pal::TextLight, 0.5f));

    if (confirmQuit) {
        const float ca = easeOutBack(clamp01((time - confirmAt) / 0.25f));
        drawDim(0.62f * clamp01((time - confirmAt) / 0.15f));
        Rectangle C = L::ConfirmPanel;
        const float sc = 0.92f + 0.08f * ca;
        C = {800.f - C.width * sc * 0.5f, C.y + C.height * 0.5f - C.height * sc * 0.5f, C.width * sc, C.height * sc};
        drawPanel(C, PanelStyle::Paper);
        DrawRectangleRec({C.x, C.y, C.width, 8.f}, alphaMul(kRedPencil, 0.8f));
        handTextCentered("Maç bitecek, emin misin?", {800.f, C.y + 66.f}, 46.f, kInk);
        drawTextCentered(FontId::Ui, "Bu maçın hesabı silinir, ana menüye dönülür.", {800.f, C.y + 116.f}, 19.f,
                         kGraphite);
        if (ca > 0.9f) {
            if (drawButton(L::ConfirmYes, "Evet, bitir", m, true, ButtonStyle::Paper, 32.f)) click(C_ConfirmYes);
            if (drawButton(L::ConfirmNo, "Vazgeç", m, true, ButtonStyle::Paper, 32.f)) click(C_ConfirmNo);
        }
    }
}

void Screens::Impl::drawMatchOver(Vector2 m, const okey::Game* g) {
    if (sheet) {
        drawSheetMatchOver(m, *sheet);
        return;
    }
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
    // slow light rays
    for (int i = 0; i < 12; ++i) {
        const float a = (float)i * kPi / 6.f + time * 0.05f;
        const Vector2 c{800.f, 230.f};
        const Vector2 p1{c.x + std::cos(a - 0.07f) * 420.f, c.y + std::sin(a - 0.07f) * 420.f};
        const Vector2 p2{c.x + std::cos(a + 0.07f) * 420.f, c.y + std::sin(a + 0.07f) * 420.f};
        tri(c, p1, p2, rgba(255, 200, 120, 0.045f * pulse));
    }

    drawHeader(g && g->classic() ? "Oyun Bitti" : "Maç Bitti", {800.f, 56.f}, 60.f, kGold);
    if (!g || g->player(0).handScores.empty()) {
        drawTextCentered(FontId::Ui, "Sonuç yok.", {800.f, 400.f}, 24.f, pal::TextLight);
    } else {
        const int hands = (int)g->player(0).handScores.size();
        drawTextCentered(FontId::Ui, std::to_string(hands) + " el oynandı", {800.f, 98.f}, 20.f,
                         alphaMul(pal::TextLight, 0.7f));
        const float pop = easeOutBack(clamp01((age - 0.15f) / 0.5f));
        rlPushMatrix();
        rlTranslatef(800.f, 270.f, 0.f);
        rlScalef(pop, pop, 1.f);
        drawTeaGlass({0.f, 0.f}, 1.25f, time, 42.f);
        rlPopMatrix();

        const std::vector<int> leaders = coLeaders(*g);
        const int leader = leaders[0];
        const bool teams = g->teams();
        const std::string best = scoreText(teams ? g->teamTotal(leader) : g->player(leader).totalScore);
        const bool humanFirst = humanAmong(*g, leaders);
        const float a1 = clamp01((age - 0.35f) / 0.4f);
        if (teams && humanFirst && leaders.size() == 2) {
            drawHeader(aiMode ? "Yapay zekanın takımı kazandı!" : "Kazandınız! Çaylar onlardan!", {800.f, 324.f}, 50.f,
                       alphaMul(pal::Highlight, a1));
            drawTextCentered(FontId::Ui, teamName(*g, leader) + ", toplam " + best + " puanla masanın kurtları.",
                             {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
        } else if (teams) {
            drawHeader(leaders.size() == 4 ? "Berabere!" : "Bu sefer olmadı, bir maç daha?", {800.f, 324.f}, 46.f,
                       alphaMul(pal::TextLight, a1));
            drawTextCentered(FontId::Ui,
                             leaders.size() == 4 ? "İki takım da " + best + " puanda kaldı."
                                                 : "Kazananlar: " + teamName(*g, leader) + " (" + best + ")",
                             {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
        } else if (humanFirst && leaders.size() == 1) {
            drawHeader(aiMode ? "Yapay zeka kazandı! Çaylar onlardan!" : "Kazandın! Çaylar onlardan!", {800.f, 324.f},
                       50.f, alphaMul(pal::Highlight, a1));
            drawTextCentered(FontId::Ui, "Toplam " + best + (aiMode ? " puanla masanın kurdu oldu." : " puanla masanın kurdu oldun."),
                             {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
        } else if (humanFirst) {
            drawHeader("Berabere!", {800.f, 324.f}, 50.f, alphaMul(pal::Highlight, a1));
            drawTextCentered(FontId::Ui,
                             (aiMode ? "Yapay zeka toplam " : "Toplam ") + best + " puanla birinciliği " +
                                 joinNames(*g, leaders, true) + (aiMode ? " ile paylaştı." : " ile paylaştın."),
                             {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
        } else {
            drawHeader("Bu sefer olmadı, bir maç daha?", {800.f, 324.f}, 46.f, alphaMul(pal::TextLight, a1));
            static const char* const quotes[4] = {"", "Sabreden derviş muradına ermiş.",
                                                  "Bu maç benim demiştim abi!",
                                                  "Bizim zamanımızda da hep ben kazanırdım."};
            std::string sub;
            if (leaders.size() > 1) {
                sub = "Birinciliği " + joinNames(*g, leaders, false) + " paylaştı (" + best + ")";
            } else {
                sub = "Kazanan: " + seatName(*g, leader) + " (" + best + ")";
                if (leader >= 1 && leader <= 3)
                    sub += "  \xE2\x80\x94  \xE2\x80\x9C" + std::string(quotes[leader]) + "\xE2\x80\x9D";
            }
            drawTextCentered(FontId::Ui, sub, {800.f, 368.f}, 22.f, alphaMul(pal::TextLight, 0.85f * a1));
        }

        // ranking card
        drawPanel(L::MatchCard, PanelStyle::Dark);
        std::vector<int> order = {0, 1, 2, 3};
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (standing(*g, a) != standing(*g, b)) return standing(*g, a) < standing(*g, b);
            if (teams && std::min(a, okey::Game::partnerOf(a)) != std::min(b, okey::Game::partnerOf(b)))
                return std::min(a, okey::Game::partnerOf(a)) < std::min(b, okey::Game::partnerOf(b));
            return a < b;
        });
        static const Color medal[4] = {Color{232, 184, 64, 255}, Color{196, 198, 206, 255},
                                       Color{190, 118, 64, 255}, Color{118, 88, 62, 255}};
        int rank = 1;
        for (int i = 0; i < 4; ++i) {
            const int s = order[(size_t)i];
            const okey::PlayerInfo& p = g->player(s);
            if (i > 0 && standing(*g, s) != standing(*g, order[(size_t)i - 1])) rank = teams ? 2 : i + 1;
            const float ra = clamp01((age - 0.55f - 0.12f * (float)i) / 0.3f);
            const float slideX = (1.f - easeOutCubic(ra)) * 40.f;
            const Rectangle row{L::MatchCard.x + 18.f + slideX, L::MatchCard.y + 16.f + 68.f * (float)i,
                                L::MatchCard.width - 36.f, 62.f};
            if (rank == 1) { // every co-leader's row is lit
                DrawRectangleRounded(row, 0.3f, 8, rgba(255, 200, 110, 0.16f * ra));
                DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, alphaMul(pal::Highlight, 0.7f * ra));
            } else {
                DrawRectangleRounded(row, 0.3f, 8, rgba(255, 255, 255, 0.04f * ra));
            }
            const Vector2 mc{row.x + 36.f, row.y + row.height * 0.5f};
            DrawCircleV({mc.x + 1.f, mc.y + 2.f}, 22.f, rgba(0, 0, 0, 0.4f * ra));
            DrawCircleV(mc, 22.f, alphaMul(medal[std::min(rank, 4) - 1], ra));
            DrawRing(mc, 17.f, 19.f, 200.f, 290.f, 10, rgba(255, 255, 255, 0.35f * ra));
            const std::string digit = std::to_string(rank);
            const float dw = measureText(FontId::UiBold, digit, 25.f).x;
            drawTextCentered(FontId::UiBold, digit, {mc.x, mc.y - 1.f}, 25.f, alphaMul(pal::TextDark, ra));
            drawText(FontId::UiBold, ".", {mc.x + dw * 0.5f, mc.y - 15.f}, 25.f, alphaMul(pal::TextDark, ra));
            const Color nameCol = p.human ? pal::Highlight : pal::TextLight;
            drawText(FontId::UiBold, seatName(*g, s), {row.x + 76.f, row.y + 7.f}, 28.f, alphaMul(nameCol, ra));
            std::string hs;
            for (size_t h = 0; h < p.handScores.size(); ++h) hs += (h ? ", " : "") + scoreText(p.handScores[h]);
            const std::string sub = "El puanları: " + hs;
            float fs = 15.f;
            const float maxW = row.width - 76.f - 150.f;
            const float w = measureText(FontId::Ui, sub, fs).x;
            if (w > maxW) fs *= maxW / w;
            drawText(FontId::Ui, sub, {row.x + 77.f, row.y + 40.f}, fs, alphaMul(pal::TextLight, 0.55f * ra));
            std::string tot = scoreText(p.totalScore);
            if (teams && (i % 2 == 1)) tot = "= " + scoreText(g->teamTotal(s)); // the team total on its second row
            const float tw = measureText(FontId::UiBold, tot, 34.f).x;
            drawText(FontId::UiBold, tot, {row.x + row.width - 22.f - tw, row.y + 13.f}, 34.f,
                     alphaMul(rank == 1 ? pal::Highlight : pal::TextLight, ra));
        }
    }

    float nfs = 30.f;
    const std::string nlabel = autoLabel("Yeni Oyun", FontId::UiBold, L::MatchNew.width - 24.f, nfs);
    if (drawButton(L::MatchNew, nlabel, m, true, ButtonStyle::Wood, nfs)) click(C_NewGame);
    if (drawButton(L::MatchMenu, "Ana Menü", m, true, ButtonStyle::Wood, 30.f)) click(C_MatchMenu);
    if (analysisAvailable && drawButton(L::MatchAnalysis, "Hatalarım", m, true, ButtonStyle::Wood, 22.f))
        click(C_ShowAnalysis);
    drawConfetti();
}

// ============================================================================ Screens (public API)

const GameInfo& gameInfo(GameKind k) { return kGames[std::clamp((int)k, 0, (int)GameKind::Count - 1)]; }

bool gameAvailable(GameKind k) {
    switch (k) {
    case GameKind::Dama: return true;        // Dama (each new game turns itself on here when its table is ready)
    case GameKind::Altmisalti: return true; // Altmışaltı (AltmisaltiTable)
    case GameKind::Bezik: return true; // Bezik
    case GameKind::Konken: return true; // Konken
    default: return k >= GameKind::Yuzbir && k < GameKind::Count;
    }
}

Screens::Screens() : impl_(new Impl) { impl_->owner = this; }

Screens::~Screens() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void Screens::init() {
    Impl& I = *impl_;
    I.owner = this;
    I.fadeShader.load();
    I.rulesLaidOut = false;
    I.nameBuf = I.settings.playerName;
    I.cur = I.prev = ScreenId::Title;
    I.fade = 1.f;
    I.frozen.reset();
    I.freezePending = false;
    for (float& t : I.shownAt) t = I.time;
}

void Screens::shutdown() {
    if (!impl_) return;
    impl_->fadeShader.unload();
    impl_->frozen.reset();
}

void Screens::show(ScreenId id) { impl_->show(id); }

ScreenId Screens::current() const { return impl_->cur; }

bool Screens::blocksGame() const { return impl_->cur != ScreenId::None; }

ScreenAction Screens::update(float dt, Vector2 mouse, const okey::Game* game) { return impl_->update(dt, mouse, game); }

void Screens::draw(const okey::Game* game) { impl_->draw(game); }

Settings& Screens::settings() { return impl_->settings; }

void Screens::setAiMode(bool on) { impl_->aiMode = on; }

void Screens::setAutoAdvance(float secondsLeft) { impl_->autoLeft = secondsLeft; }

void Screens::setSheet(const SheetModel& m) { impl_->sheet = m; }

void Screens::clearSheet() { impl_->sheet.reset(); }

void Screens::setStats(const StatsBook* stats) { impl_->stats = stats; }

void Screens::setReplays(const std::vector<ReplayEntry>& rows) { impl_->replays = rows; }
int Screens::chosenReplay() const { return impl_->chosen; }

void Screens::setAnalysis(bool available, bool ready, const std::string& title, const std::vector<MistakeView>& rows) {
    impl_->analysisAvailable = available;
    impl_->analysisReady = ready;
    impl_->analysisTitle = title;
    impl_->analysis = rows;
}

// Başarımlar
void Screens::setAchievements(const Achievements* a) { impl_->achievements = a; }
void Screens::showAchievementBanner(int index) {
    if (index >= 0 && index < Achievements::count()) impl_->bannerQueue.push_back(index);
}
bool Screens::achievementBannerUp() const { return impl_->bannerIdx >= 0 || !impl_->bannerQueue.empty(); }

void Screens::setGuide(const std::string& title, const std::vector<std::string>& lines) {
    impl_->guideTitle = title;
    impl_->guideLines = lines;
}

void Screens::setResumable(bool on, const std::string& label) {
    impl_->canResume = on;
    impl_->resumeLabel = on ? label : std::string();
}

} // namespace ui