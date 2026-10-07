// App: the match flow (AppInternal.h).
#include "app/AppInternal.h"
#include "app/Rules101.h" // 101 kuralları

namespace app {
namespace detail {

// ---------------------------------------------------------------- flow
void App::setLocation(int loc, int seat) {
    if (loc != 1) seat = 0;
    if (loc == location_ && seat == locationSeat_) return;
    location_ = loc;
    locationSeat_ = seat;
    room_.setTavlaFocus(loc == 1);
    characters_.setTavlaTable(loc == 1, seat);
    if (loc == 1) pcam_.setTavlaSeat();
    else pcam_.setOkeySeat();
    pcam_.setRecentreKey(loc != 1); // (tavla: R throws the dice)
    if (!snapshot_) fade_ = 1.f;
}

void App::setTitleMode(bool on) {
    room_.setTitleMode(on);
    characters_.setTitleMode(on);
    pcam_.setTitleMode(on);
    if (!on) pcam_.reset();
}

void App::handleScreenAction(ui::ScreenAction a) {
    using A = ui::ScreenAction;
    switch (a) {
    case A::None:
    case A::Resume: break;
    case A::StartMatch:
        if (flow_ == Flow::Title) setAiMode(false); // "Oyna": you play ("Yeni Oyun" keeps the mode as it is)
        startMatch();
        break;
    case A::StartAiMatch:
        setAiMode(true);
        startMatch();
        break;
    case A::ToggleAiMode: setAiMode(!aiMode_); break;
    case A::NextHand: startNextHand(); break;
    case A::ShowMatchResult: flow_ = Flow::MatchOver; break;
    case A::ResumeSaved: resumeSaved(); break;
    case A::WatchReplay: startReplay(screens_.chosenReplay()); break;
    case A::AnalyzeReplay: analyzeReplay(screens_.chosenReplay()); break;
    case A::ToTitle: toTitle(); break;
    case A::Quit: quit_ = true; break;
    case A::SettingsChanged:
        releaseOverrides();
        applySettings();
        settingsDirty_ = true;
        break;
    }
}

// What every match start shares (startMatch, startOtherMatch): its seed (a resumed match's own, --seed's, or a fresh
// one), game and rules for the save; the analysis of the last match goes; a new match the player starts drops the
// old save (not one the Yapay Zeka was asked to play: the player's save stays for "Devam Et"); the room leaves the
// title. Returns the seed.
uint64_t App::beginMatch(ui::GameKind kind) {
    const ui::Settings& st = screens_.settings();
    const uint64_t matchSeed = forcedSeed_ ? *forcedSeed_
                               : opt_.hasSeed ? opt_.seed + (uint64_t)matchCount_
                                              : mix64(baseSeed_ + (uint64_t)matchCount_);
    matchSeed_ = matchSeed;
    stopAnalysis();  // "Hatalarım" belongs to the match that just ended
    screens_.setAnalysis(false, true, "", {});
    matchGame_ = (int)kind;
    matchSettings_ = st;
    matchAiStarted_ = aiMode_ && !resuming_;
    if (!resuming_) {
        if (!matchAiStarted_) deleteSave();
        replayMode_ = false; // "Yeni Oyun" after a replay is a real match
    }
    ++matchCount_;
    characters_.setNames(names());
    setTitleMode(false);
    return matchSeed;
}

// ... and just before the engine deals: the flow is in play, nothing of the last match is pending.
void App::enterMatch() {
    delayed_.clear();
    think_ = Think{};
    handOverT_ = screenT_ = 0.f;
    flow_ = Flow::Playing;
    matchAiTouched_ = aiMode_;
}

// ... and once it is dealt: the regulars greet it, the rehber's card, the Yapay Zeka's note.
void App::greetMatch(ui::GameKind kind) {
    if (!unattended() && !resuming_ && !replayMode_)
        characters_.banter().matchStart((int)kind, record_.games[(size_t)kind].streak);
    maybeShowGuide(kind);
    if (aiMode_ && !snapshot_) {
        const std::string note = "Yapay zeka senin yerine oynuyor  \xC2\xB7  geri almak için Y";
        if (other_ && !ui::isOkeyFamily(kind)) other_->toast(note, ui::pal::Highlight, 4.f);
        else table_.toast(note, ui::pal::Highlight, 4.f);
    }
}

void App::startMatch() {
    cancelThink();
    const ui::Settings& st = screens_.settings();
    okey::RulesConfig cfg;
    const ui::GameKind kind = (ui::GameKind)std::clamp(st.game, 0, (int)ui::GameKind::Count - 1);
    if (!ui::isOkeyFamily(kind)) {
        startOtherMatch(kind);
        return;
    }
    if (other_) {
        other_->shutdown();
        other_.reset();
        hands_.reset();  // (its cues point into the game that is gone)
    }
    setLocation(0, 0);
    table_.setFurnitureOnly(false);
    screens_.clearSheet();
    cfg.variant = kind == ui::GameKind::Okey ? okey::Variant::Okey : okey::Variant::Yuzbir;
    cfg.teams = kind == ui::GameKind::YuzbirEsli;
    cfg.okeyStartPoints = std::clamp(st.okeyStart, 1, 99);
    cfg.okeyColorDouble = st.okeyRenkli;
    cfg.numHands = std::clamp(st.numHands, 1, 11);
    apply101Rules(st, cfg); // 101 kuralları (app/Rules101.h)
    cfg.katlamali = cfg.katlamali || opt_.katlamali;
    game_.setRules(cfg);
    const std::array<std::string, 4> nm = names();
    for (int s = 0; s < 4; ++s) game_.setPlayer(s, nm[s], s == HUMAN);
    const uint64_t matchSeed = beginMatch(kind);
    for (int s = 0; s < 4; ++s) {
        // the human's seat always has one too, ready for the Yapay Zeka mode at any moment: a Kurt (--autoplay: an Usta)
        const okey::BotLevel lvl = s != HUMAN     ? (okey::BotLevel)std::clamp(st.difficulty, 0, 2)
                                   : opt_.autoplay ? okey::BotLevel::Normal
                                                   : okey::BotLevel::Hard;
        bots_[s] = std::make_unique<okey::Bot>(lvl, mix64(matchSeed * 4u + (uint64_t)s + 0xB07ull));
        // the regulars' own ways: Kel Mahmut bold, Emekli Nuri careful (the AI in the player's seat stays neutral)
        bots_[s]->setStyle(s == HUMAN ? okey::BotStyle{} : okey::BotStyle::forSeat(s));
    }
    table_.setAnimationSpeed(st.animSpeed);
    characters_.setAnimationSpeed(st.animSpeed);
    table_.setHints(st.hints);
    enterMatch();
    game_.startMatch(matchSeed);
    pumpEvents();
    greetMatch(kind);
    if (opt_.autoplay || opt_.aiChaos)
        std::printf("[%s] match %d (%s): seed %llu, %d hands (frame %ld)%s\n", opt_.autoplay ? "autoplay" : "aichaos",
                    matchCount_, ui::gameInfo(kind).name, (unsigned long long)matchSeed, cfg.numHands, frame_,
                    aiMode_ ? ", Yapay Zeka on" : "");
    if ((opt_.autoplay || opt_.aiChaos) && !game_.classic() && !summary101(cfg).empty()) // 101 kuralları
        std::printf("[%s] 101 kuralları: %s\n", opt_.autoplay ? "autoplay" : "aichaos", summary101(cfg).c_str());
}

// A match of one of the other games (tavla, the card games): the table shows only its furniture, the game brings
// its own pieces.
void App::startOtherMatch(ui::GameKind kind) {
    const ui::Settings& st = screens_.settings();
    if (!other_ || otherKind_ != kind) {
        if (other_) other_->shutdown();
        hands_.reset();  // (the player's hands drop the old table's cues)
        other_ = makeTableGame(kind);
        otherKind_ = kind;
        if (!other_) {
            std::fprintf(stderr, "Bu oyun henüz hazır değil.\n");
            flow_ = Flow::Title;
            return;
        }
        TableContext ctx;
        ctx.renderer = &renderer_;
        ctx.characters = &characters_;
        ctx.sfx = [this](ui::Sfx s) {
            if (audioOn_) audio_.play(s);
        };
        ctx.handCue = [this](const r3d::HandCue& c) { hands_.cue(c); };  // the player's own hands
        ctx.handLead = [this](r3d::HandCueKind k) { return hands_.lead(k); };
        if (!other_->init(ctx)) {
            std::fprintf(stderr, "Oyun masası kurulamadı.\n");
            other_.reset();
            flow_ = Flow::Title;
            return;
        }
    }
    for (auto& b : bots_) b.reset();
    game_ = okey::Game(game_.rules()); // the okey game rests (not started)
    table_.setFurnitureOnly(true);
    screens_.clearSheet();
    const std::array<std::string, 4> nm = names();
    const uint64_t matchSeed = beginMatch(kind);
    other_->setLevel(std::clamp(st.difficulty, 0, 2));
    other_->setAnimationSpeed(st.animSpeed);
    other_->setHints(st.hints);
    characters_.setAnimationSpeed(st.animSpeed);
    enterMatch();
    other_->startMatch(st, nm, matchSeed);
    {
        const std::vector<int> seats = other_->seats();
        setLocation(other_->location(), seats.size() > 1 ? seats[1] : 0);
    }
    greetMatch(kind);
    if (opt_.autoplay)
        std::printf("[autoplay] match %d (%s): seed %llu (frame %ld)\n", matchCount_, ui::gameInfo(kind).name,
                    (unsigned long long)matchSeed, frame_);
}

void App::startNextHand() {
    cancelThink();
    if (otherInGame()) {
        if (!other_->handOver() || other_->matchOver()) return;
        handOverT_ = 0.f;
        flow_ = Flow::Playing;
        other_->startNextHand();
        return;
    }
    if (game_.handState() != okey::HandState::HandOver) return;
    think_ = Think{};
    handOverT_ = 0.f;
    flow_ = Flow::Playing;
    game_.startNextHand();
    pumpEvents();
}

void App::toTitle() {
    saveMatch();
    if (replayMode_ && other_) other_->setReplayMode(false);
    replayMode_ = false;
    cancelThink();
    if (other_) {
        other_->shutdown();
        other_.reset();
        hands_.reset();
    }
    table_.setFurnitureOnly(false);
    screens_.clearSheet();
    think_ = Think{};
    delayed_.clear();
    const okey::RulesConfig cfg = game_.rules();
    game_ = okey::Game(cfg);  // not started: the tiles rest in a heap on the table
    for (auto& b : bots_) b.reset();
    flow_ = Flow::Title;
    setLocation(0, 0);
    setTitleMode(true);
    lookDrag_ = false;
}

// ---------------------------------------------------------------- the room around us
// The other tables cheer a big finish or a mars, groan when the player loses big; at the end of a match the winner
// buys a round of tea (the çırak brings it).
void App::crowdAtHandEnd() {
    if (replayMode_ && replaySpeed_ > 2.f) return;
    using C = r3d::Characters;
    if (otherInGame()) {
        const ui::SheetModel sh = other_->sheet(false);
        const bool humanHand = sh.starCol >= 0 && sh.starCol == sh.humanCol;
        if (sh.taglineRed) characters_.crowdReact(humanHand ? C::CrowdCheer : C::CrowdLaugh);
        if (other_->matchOver()) {
            int payer = sh.humanWon ? 0 : 1;
            const std::vector<int> seats = other_->seats();
            if (!sh.humanWon && !sh.ranking.empty() && sh.columns.size() == seats.size() &&
                sh.ranking[0] < (int)seats.size())
                payer = seats[(size_t)sh.ranking[0]];
            if (payer == 0 && !sh.humanWon) payer = 1;
            if (!sh.humanWon) characters_.crowdReact(C::CrowdGroan, Vector3{0.f, 0.8f, 0.f}, 0.5f);
            characters_.orderTeaRound(payer);
        }
        return;
    }
    const okey::HandResult& r = game_.lastHandResult();
    if (r.winner >= 0) {
        const bool ours = r.winner == HUMAN || (game_.teams() && r.winner == okey::Game::partnerOf(HUMAN));
        if (r.finishedWithJoker || r.multiplier >= 2)
            characters_.crowdReact(ours ? C::CrowdCheer : C::CrowdGroan, Vector3{0.f, 0.8f, 0.f}, ours ? 1.f : 0.6f);
    }
    if (game_.handState() == okey::HandState::MatchOver) characters_.orderTeaRound(game_.leaderSeat());
}

void App::drawCrowdShouts() {
    if (flow_ == Flow::Title || screens_.blocksGame()) return;
    for (const Shout& sh : shouts_) {
        Vector2 p;
        if (!renderer_.projectToVirtual(sh.at, p)) continue;
        const float a = std::min(1.f, sh.t / 0.2f) * std::clamp((2.4f - sh.t) / 0.5f, 0.f, 1.f);
        p.y -= 30.f + sh.t * 14.f;
        p.x = std::clamp(p.x, 90.f, 1510.f);
        p.y = std::clamp(p.y, 40.f, 700.f);
        const float fs = 19.f * ui::hudTextScale();
        const Vector2 sz = ui::measureText(ui::FontId::UiBold, sh.text, fs);
        const Rectangle box{p.x - sz.x * 0.5f - 12.f, p.y - sz.y * 0.5f - 6.f, sz.x + 24.f, sz.y + 12.f};
        DrawRectangleRounded(box, 0.5f, 8, Color{24, 14, 8, (unsigned char)(170 * a)});
        ui::drawTextCentered(ui::FontId::UiBold, sh.text, p, fs, Color{246, 232, 200, (unsigned char)(255 * a)});
    }
}

// ---------------------------------------------------------------- rehber (a game's first match)
namespace {

struct Guide {
    std::string title;
    std::vector<std::string> lines;
    std::string firstTurn; // said when the player's turn first comes
};

Guide guideFor(ui::GameKind k, const std::string& tavlaOpp = "Kel Mahmut", int tavlaCesit = 0) {
    using G = ui::GameKind;
    const std::string hint = "Takılırsan İpucu düğmesi (ya da H tuşu) Kurt'un senin yerinde ne yapacağını gösterir.";
    switch (k) {
    case G::Yuzbir:
    case G::YuzbirEsli: {
        Guide g{k == G::Yuzbir ? "101'e Hoş Geldin" : "Eşli 101'e Hoş Geldin", {}, ""};
        g.lines = {"Amaç taşlarını perlere dizip masaya açmak ve elini bitirmek. Puanı en düşük olan kazanır.",
                   "Sıran gelince ortadaki desteden ya da soldakinin attığı taştan çek, sonra bir taş at.",
                   "Açmak için serilerin toplamı en az 101 olmalı ya da 5 çiftin olmalı. Seri Diz / Çift Diz istakanı "
                   "düzenler, El Aç ile açarsın.",
                   "Açtıktan sonra masadaki perlere taş işleyebilirsin. İşlenebilecek bir taşı ya da okeyi atmak 101 "
                   "cezadır."};
        if (k == G::YuzbirEsli)
            g.lines.push_back("Karşındaki Kel Mahmut ortağın: biriniz bitirince ortağının eli yazılmaz, takımların "
                              "toplamı sayılır.");
        g.lines.push_back(hint);
        g.firstTurn = "Sıra sende: ortadaki desteye ya da soldaki taşa tıkla, sonra atacağın taşı sürükleyip bırak.";
        return g;
    }
    case G::Okey:
        return {"Okey'e Hoş Geldin",
                {"Amaç 14 taşını perlere (sıralı aynı renk ya da aynı sayı farklı renk) ya da 7 çifte dizip 15. taşı "
                 "bırakarak bitirmek.",
                 "Sırası gelen ortadan ya da soldan bir taş çeker, bir taş atar.",
                 "Elin tamamlanınca fazla taşı masanın ortasındaki Bitir yerine sürükle. Göstergenin eşi elindeyse ilk "
                 "taşını atmadan Göster'e bas.",
                 "Herkes puanla başlar; bitiren her oyuncudan 2 puan (okey atarak ya da çiftle 4) alır. Biri 0'a "
                 "düşünce oyun biter, en çok puanı kalan kazanır.",
                 hint},
                "Sıra sende: bir taş çek, sonra atacağın taşı sürükleyip bırak."};
    case G::Tavla:
        if (tavlaCesit == 1 || tavlaCesit == 2) { // Tavla çeşidi: Gülbahar / Fevga
            const bool gul = tavlaCesit == 1;
            Guide g{gul ? "Gülbahar'a Hoş Geldin" : "Fevga'ya Hoş Geldin", {}, ""};
            g.lines = {"Tavla kendi masasında oynanır: " + tavlaOpp + " karşına oturur. Bu çeşitte 15 pulun hepsi tek "
                       "köşeden (sağ üstteki hane) başlar; ikiniz de aynı yöne, saat yönünün tersine yürürsünüz.",
                       "Zar At'a bas, sonra oynatacağın pulun hanesine, ardından yeşil yanan haneye tıkla. Evin sağ "
                       "alttaki 6 hane; pullarının hepsi oraya girince toplarsın.",
                       "Kırma yok: tek pul da haneyi tutar, rakibin pulu olan haneye konamazsın."};
            if (gul)
                g.lines.push_back("Her oyuncunun ilk üç zarından sonra çift gelirse merdiven çıkar: 3-3 attıysan dört 3, "
                                  "dört 4, dört 5, dört 6 oynarsın. Tam oynayamadığın basamakta merdiven biter.");
            else
                g.lines.push_back("İlk çıkardığın pul rakibin başlangıç hanesini geçmeden ikinci pul çıkmaz. Rakibin "
                                  "bütün pullarının önünü altı hanelik kapıyla kapatamazsın.");
            g.lines.push_back("Rakip hiç pul toplamadan bitirirsen mars olur: 2 sayı.");
            g.lines.push_back(hint);
            g.firstTurn = gul ? "Sıra sende: Zar At'a bas. Pullar sağ üstten çıkar, tek pul da haneyi tutar."
                              : "Sıra sende: Zar At'a bas. Önce tek bir pulu, rakibin köşesini geçene kadar yürüt.";
            return g;
        }
        return {"Tavlaya Hoş Geldin",
                {"Tavla kendi masasında oynanır: sedirin önündeki küçük masada " + tavlaOpp +
                     " karşına oturur (rakibi Ayarlar'dan seçersin). Amaç 15 pulunu kendi evine (sağ alttaki 6 hane) "
                     "getirip hepsini toplamak.",
                 "Zar At'a bas, sonra oynatacağın pulun hanesine, ardından yeşil yanan haneye tıkla. Geri Al ile "
                 "hamleni geri alabilirsin.",
                 "Tek duran pul vurulabilir; kırılan pul önce rakibin evinden yeniden girmelidir.",
                 "Rakip hiç pul toplamadan bitirirsen mars olur: 2 sayı. Maç ayarlardaki sayıya kadar sürer.", hint},
                "Sıra sende: Zar At'a bas."};
    case G::Pisti:
        return {"Pişti'ye Hoş Geldin",
                {"Sırası gelen yere bir kâğıt atar. Yerdeki en üstteki kâğıtla aynı sayıyı atan yerdekilerin hepsini "
                 "alır; Vale her şeyi alır.",
                 "Yerde tek kâğıt varken aynısıyla alırsan pişti: 10 puan (Vale ile Vale: 20).",
                 "As, Vale 1; Sinek 2 2; Karo 10 3 puan; en çok kâğıt alan 3 puan daha alır.",
                 "Oynayacağın kâğıda tıklaman yeterli.", hint},
                "Sıra sende: atacağın kâğıda tıkla."};
    case G::Batak:
        return {"Batak'a Hoş Geldin",
                {"İhalede kaç el alacağını söyle ya da pas de. İhaleyi alan kozu seçer ve ilk kâğıdı atar.",
                 "Renge uymak ve yükseltmek zorunlu; o renk yoksa koz çakmalısın. Koz açılmadan kozla başlanmaz.",
                 "Sözünü tutarsan aldığın el kadar yazarsın, tutamazsan ihale kadar düşersin. Hiç el alamayan da batar.",
                 "Oynanabilir kâğıtların parlak durur; tıkladığın kâğıt oynanır.", hint},
                "Sıra sende: ortadaki panelden ihaleni söyle ya da pas de."};
    case G::King:
        return {"King'e Hoş Geldin",
                {"20 el oynanır. Seçme sırası gelen o elin oyununu söyler: herkes 3 kez ceza, 2 kez koz seçer.",
                 "Cezalarda amaç almamak: el, kupa, erkek, kız, rıfkı, son iki. Kozda aldığın her el +50.",
                 "Renge uymak zorunlu; bazı cezaların kendi zorunlulukları var (Kurallar sayfasında).",
                 "Seçme sırası sende olunca ortada açılan panelden oyunu seç; kartlarına tıklayarak oyna.", hint},
                "Sıra sende: oynayacağın kâğıda tıkla."};
    case G::Altmisalti: // Altmışaltı
        return {"Altmışaltı'ya Hoş Geldin",
                {tavlaOpp + " ile iki kişilik masada, 24 kâğıtla: As 11, 10 10, Papaz 4, Kız 3, Vale 2, 9 0. Altta açık "
                 "duran kâğıt kozdur.",
                 "Deste açıkken renge uymak zorunlu değil; eli alan önce desteden çeker. Aldığın kâğıtlarla 66'ya ilk "
                 "ulaşan eli kazanır.",
                 "Eşi elindeyken Kızı ya da Papazı açarsan evlilik: 20, kozda 40. Koz dokuzuyla açık kozu alabilirsin.",
                 "Kapat düğmesi desteyi kapatır: artık renge uymak ve eli yükseltmek zorunlu. Tutturamazsan rakibin 2-3 "
                 "oyun alır. 7 oyuna ilk varan kazanır.", hint},
                "Sıra sende: oynayacağın kâğıda tıkla; puanın alttaki satırda."};
    case G::Bezik: // Bezik
        return {"Bezik'e Hoş Geldin",
                {tavlaOpp + " ile iki kişilik masada, iki desteyle: 7'den As'a her kâğıttan iki tane. Sıra As, 10, Papaz, "
                 "Kız, Vale, 9, 8, 7. Destenin altında açık duran kâğıt kozdur.",
                 "Deste bitene kadar renge uymak zorunlu değil. Eli alan bir deklarasyon yapar: bezik (Maça Kız + Karo "
                 "Vale) 40, çift bezik 500, koz serisi 250, dört as 100, evlilik 20, koz evliliği 40…",
                 "Deklarasyonlar önünde açık durur, oradan da oynarsın. Koz 7'siyle açık kozu alırsan 10 sayı.",
                 "Deste bitince renge uymak ve eli geçmek zorunlu. Aldığın her As ve 10 için 10 sayı. 1000'e ilk varan "
                 "kazanır.", hint},
                "Sıra sende: bir kâğıt aç. El alınca ortada deklarasyon paneli açılır."};
    case G::Dama: // Dama (tavlaOpp: the two-player games' opponent, ui::twoPlayerOpponent)
        return {"Damaya Hoş Geldin",
                {"Dama iki kişilik masada oynanır: " + tavlaOpp + " karşına oturur (rakibi Ayarlar'dan seçersin). "
                 "Türk damasında taşlar düz gider: bir kare ileri, sağa ya da sola; geri ve çapraz gitmez.",
                 "Oynatacağın taşa tıkla, sonra yeşil yanan kareye; ya da taşı tutup oraya sürükle.",
                 "Almak zorunlu: rakibin taşının arkası boşsa üstünden atlayıp alırsın, alabildiğin kadar devam edersin. "
                 "Birkaç yol varsa en çok taş alanı seçmelisin.",
                 "Son sıraya varan taş dama olur: düz çizgide istediği kadar gider, uzaktan da alır. Rakibin taşını "
                 "bitiren ya da onu kımıldayamaz bırakan oyunu alır.", hint},
                "Sıra sende: bir taşına tıkla, sonra yeşil kareye. Beyazlar ilk hamleyi yapar."};
    case G::Konken: // Konken
        return {"Konken'e Hoş Geldin",
                {"İki deste ve dört jokerle, 14'er kâğıt. Aynı renkten sıralı üç kâğıt seri, aynı sayıdan farklı renk "
                 "üç-dört kâğıt grup olur; joker her kâğıdın yerine geçer.",
                 "Sıran gelince desteden ya da yerden çek, bir kâğıt at. Yerden aldığını o tur masada kullanmalısın; "
                 "kullanamazsan Geri Ver.",
                 "Perlerin en az 51 edince aç: kâğıtlara tıklayıp işaretle, sonra Aç. Açtıktan sonra masadaki bütün "
                 "perlere kâğıt sürükleyip işleyebilirsin.",
                 "Elini bitiren 0, açan elindekini, açmayan 100 yazar; hiç açmadan bir turda bitirmek konken: herkes "
                 "iki kat yazar. 151'i bulan yanar ve masadan kalkar; son kalan kazanır.", hint},
                "Sıra sende: desteye ya da yerdeki kâğıda tıkla (D / A). Kâğıtlarını sürükleyerek dizebilirsin."};
    default: return {};
    }
}

} // namespace

void App::maybeShowGuide(ui::GameKind kind) {
    firstTurnTip_.clear();
    ui::Settings& st = screens_.settings();
    const int bit = 1 << (int)kind;
    if (resuming_ || (!opt_.guideDemo && (unattended() || aiMode_ || !st.guide || (st.guideSeen & bit)))) return;
    const int across = ui::twoPlayerOpponent(screens_.settings(), kind); // Rakip (the two-player games)
    Guide g = guideFor(kind, names()[(size_t)std::clamp(across, 1, 3)], screens_.settings().tavlaCesit);
    if (g.title.empty()) return;
    if (kind == ui::GameKind::Yuzbir || kind == ui::GameKind::YuzbirEsli) { // 101 kuralları: this table's own rules
        const okey::RulesConfig& rc = game_.rules();
        if (rc.openThreshold != 101 && g.lines.size() > 2) {
            std::string& l = g.lines[2];
            const size_t at = l.find("en az 101");
            if (at != std::string::npos) l.replace(at, 9, "en az " + std::to_string(rc.openThreshold));
        }
        const std::string sum = summary101(rc);
        if (!sum.empty() && !g.lines.empty())
            g.lines.insert(g.lines.end() - 1, "Bu masanın kuralları: " + sum + ". Ayrıntısı Kurallar sayfasında.");
    }
    st.guideSeen |= bit;
    settingsDirty_ = true;
    screens_.setGuide(g.title, g.lines);
    screens_.show(ui::ScreenId::Guide);
    firstTurnTip_ = g.firstTurn;
}

// The first time the player's turn comes in a guided match: what to do, in one line.
void App::updateFirstTurnTip() {
    if (firstTurnTip_.empty() || aiSeat() || screens_.blocksGame()) return;
    bool mine;
    if (otherInGame()) mine = other_->activeSeat() == HUMAN && !other_->animating();
    else mine = game_.handState() == okey::HandState::Playing && game_.current() == HUMAN && !table_.isAnimating();
    if (!mine) return;
    if (otherInGame()) other_->toast(firstTurnTip_, ui::pal::Highlight, 6.f);
    else table_.toast(firstTurnTip_, ui::pal::Highlight, 6.f);
    firstTurnTip_.clear();
}

// ---------------------------------------------------------------- İpucu
// What a Kurt would do now in the player's seat: the first step of its plan, glowing on the table and said in words.
void App::showOkeyHint() {
    if (game_.handState() != okey::HandState::Playing || game_.current() != HUMAN || aiSeat()) return;
    if (!hintBot_) hintBot_ = std::make_unique<okey::Bot>(okey::BotLevel::Hard, 0x41D7ull);
    hintBot_->resetForHand();
    const okey::BotAction a = hintBot_->next(game_, HUMAN);
    const okey::OkeyInfo& ok = game_.okey();
    auto name = [&](int id) { return okey::tileNameTR(id, ok); };
    std::vector<int> tiles;
    bool pile = false, left = false;
    std::string text;
    using K = okey::BotAction::Kind;
    switch (a.kind) {
    case K::DrawPile:
        pile = true;
        text = "İpucu: ortadan çek";
        break;
    case K::TakeLeft:
        left = true;
        text = "İpucu: soldaki taşı al (" + name(game_.topDiscard(okey::Game::leftOf(HUMAN))) + ")";
        break;
    case K::ReturnLeft:
        tiles.push_back(game_.pendingLeftTile());
        text = "İpucu: soldan aldığın taşı geri ver, işine yaramıyor";
        break;
    case K::Open:
    case K::LayMelds: {
        int value = 0, n = 0;
        for (const auto& m : a.melds) {
            okey::Meld mm;
            if (okey::makeMeld(m, ok, mm, m.size() == 2)) value += mm.value();
            ++n;
            tiles.insert(tiles.end(), m.begin(), m.end());
        }
        const bool pairs = !a.melds.empty() && a.melds.front().size() == 2;
        text = a.kind == K::Open ? (pairs ? "İpucu: " + std::to_string(n) + " çiftle elini aç"
                                          : "İpucu: elini aç (" + std::to_string(value) + ")")
                                 : "İpucu: parlayan taşlarla yeni per aç";
        break;
    }
    case K::AddToMeld:
        tiles.push_back(a.tile);
        text = "İpucu: " + name(a.tile) + " taşını masadaki pere işle";
        break;
    case K::SwapJoker:
        tiles.push_back(a.tile);
        text = "İpucu: " + name(a.tile) + " ile masadaki okeyi al";
        break;
    case K::Discard:
        tiles.push_back(a.tile);
        text = "İpucu: " + name(a.tile) + " at";
        break;
    case K::Finish:
        tiles.push_back(a.tile);
        text = "İpucu: bitirebilirsin! " + name(a.tile) + " taşını bırak (Bitir)";
        break;
    case K::ShowIndicator:
        tiles.push_back(game_.indicatorTwin(HUMAN));
        text = "İpucu: göstergeyi göster (Göster)";
        break;
    }
    table_.showHint(tiles, pile, left, text);
}

void App::updateFlow(float simDt) {
    if (otherInGame()) {
        // (Bezik: a replay of one of the other games feeds its saved lines here too; they were never fed before)
        if (replayMode_ && flow_ == Flow::Playing && !other_->handOver()) updateReplay(simDt);
        if (flow_ == Flow::Playing && other_->handOver()) {
            flow_ = Flow::HandOver;
            handOverT_ = 0.f;
            ++handsPlayed_;
            recordOtherHand();
            saveMatch();
            crowdAtHandEnd();
            achievementsAtHandEnd(); // Başarımlar
            if (other_->matchOver()) {
                const ui::SheetModel sh = other_->sheet(false);
                archiveMatch(sh.humanWon ? std::string("Kazandın") : sh.resultTitle);
                analyzeFinishedMatch();
            }
            if (opt_.autoplay && !snapshot_)
                std::printf("[autoplay] hand %d: %s\n", handsPlayed_, other_->lastLogLine().c_str());
        }
        if (flow_ == Flow::HandOver) {
            handOverT_ += simDt;
            if ((handOverT_ >= SUMMARY_DELAY + achHold_ && !other_->animating()) || handOverT_ >= SUMMARY_DELAY + achHold_ + 5.f) {
                achHold_ = 0.f; // (Başarımlar: the celebration had its moment over the table)
                screens_.setSheet(other_->sheet(aiMode_));
                screens_.show(ui::ScreenId::HandSummary);
                flow_ = Flow::Summary;
                screenT_ = 0.f;
            }
        }
        return;
    }
    switch (game_.handState()) {
    case okey::HandState::Playing:
        if (replayMode_) updateReplay(simDt);
        else updateBots(simDt);
        break;
    case okey::HandState::HandOver:
    case okey::HandState::MatchOver:
        if (flow_ == Flow::HandOver) {
            // let everyone see the revealed istakas before the score sheet covers the table
            handOverT_ += simDt;
            if ((handOverT_ >= SUMMARY_DELAY + achHold_ && !table_.isAnimating()) || handOverT_ >= SUMMARY_DELAY + achHold_ + 5.f) {
                achHold_ = 0.f; // (Başarımlar: the celebration had its moment over the table)
                screens_.show(ui::ScreenId::HandSummary);
                flow_ = Flow::Summary;
                screenT_ = 0.f;
            }
        }
        break;
    case okey::HandState::NotStarted: break;
    }
}

// Autoplay and the Yapay Zeka mode press the score sheet's button by themselves (autoplay also leaves after the
// final standings); the button shows the countdown. A player can still press it (or anything else) first.
void App::updateAutoScreens(float simDt) {
    const ui::ScreenId cur = screens_.current();
    if (cur != autoScreen_) { // every screen gets its full time, however it came up
        autoScreen_ = cur;
        screenT_ = 0.f;
    }
    float wait = -1.f;
    if (cur == ui::ScreenId::HandSummary) wait = opt_.autoplay ? AUTO_SUMMARY : aiMode_ ? AI_SUMMARY : -1.f;
    else if (cur == ui::ScreenId::MatchOver) wait = opt_.autoplay ? AUTO_MATCHOVER : aiMode_ ? AI_MATCHOVER : -1.f;
    if (replayMode_ && cur == ui::ScreenId::HandSummary) wait = AI_SUMMARY; // a replay moves on by itself (not past the end)
    if (cur == ui::ScreenId::HandSummary && wait < 0.f && otherInGame() && other_->spectating()) wait = AI_SUMMARY; // (Konken son kalan)
    if (cur == ui::ScreenId::MatchOver && (opt_.autoplay || opt_.aiChaos)) matchOverReached_ = true;
    const bool picture = snapshot_ && ((snapState_ == SnapState::Summary && cur == ui::ScreenId::HandSummary) ||
                                       (snapState_ == SnapState::MatchOver && cur == ui::ScreenId::MatchOver && opt_.matches <= 1));
    if (wait < 0.f || picture) { // (a snapshot of the sheet or the final standings keeps it up: it is the picture)
        screens_.setAutoAdvance(-1.f);
        if (opt_.autoplay && cur == ui::ScreenId::Title && flow_ == Flow::Title && matchCount_ > 0) {
            screenT_ += simDt;
            if (screenT_ < AUTO_TITLE) return;
            screenT_ = 0.f;
            screens_.show(ui::ScreenId::None);
            startMatch();
        }
        return;
    }
    screenT_ += simDt;
    screens_.setAutoAdvance(std::max(0.f, wait - screenT_) / std::max(0.05f, opt_.speed));
    if (screenT_ >= wait) pressBetweenHands();
}

// The main button of the score sheet ("Sonraki El" / "Sonuçlar") or the final standings ("Yeni Oyun"), pressed for
// the player: by autoplay, the Yapay Zeka mode or the --ai-chaos stand-in. Autoplay and the test count the matches
// and leave after the last one; autoplay goes through the title screen every other time.
void App::pressBetweenHands() {
    const ui::ScreenId cur = screens_.current();
    screenT_ = 0.f;
    if (cur == ui::ScreenId::HandSummary) {
        if (otherInGame() ? other_->matchOver() : game_.handState() == okey::HandState::MatchOver) {
            screens_.show(ui::ScreenId::MatchOver);
            flow_ = Flow::MatchOver;
        } else {
            screens_.show(ui::ScreenId::None);
            startNextHand();
        }
    } else if (cur == ui::ScreenId::MatchOver) {
        if ((opt_.autoplay || opt_.aiChaos) && ++matchesDone_ >= opt_.matches) {
            quit_ = true;
        } else if (opt_.autoplay && matchesDone_ % 2 == 1) { // "Ana Menü", then "Oyna" a little later
            screens_.show(ui::ScreenId::Title);
            toTitle();
        } else if (opt_.aiChaos && matchesDone_ % 2 == 1) { // the title's "Oyna" or "Yapay Zekayı İzle" later
            screens_.show(ui::ScreenId::Title);
            toTitle();
            chaos_.titleIn = chaos_.rng.uniform(0.3f, 2.f);
        } else {                                             // "Yeni Oyun" straight from the final standings
            screens_.show(ui::ScreenId::None);
            startMatch();
        }
    }
}

void App::pumpEvents() {
    const std::vector<okey::GameEvent> events = game_.drainEvents();
    for (const okey::GameEvent& e : events) {
        using okey::EvType;
        if (aiSeat()) { // the table's messages speak about the Yapay Zeka instead of to the player
            okey::GameEvent w = e;
            w.text = watchText(e);
            table_.onEvent(w);
        } else {
            table_.onEvent(e);
        }
        characters_.onEvent(e, game_);
        routeAudio(e);
        achievementsOkeyEvent(e); // Başarımlar
        if (e.type == EvType::HandStart) {
            for (auto& b : bots_)
                if (b) b->resetForHand();
            table_.onHandStart();
        }
        for (auto& b : bots_)
            if (b) b->observe(e, game_);
        switch (e.type) {
        case EvType::TurnStart: characters_.setActiveSeat(e.player); break;
        case EvType::Open:
            // our eyes follow a big moment across the table (never while we handle our own tiles)
            if (e.player != HUMAN && e.player >= 0 && !table_.mouseBusy() && !lookDrag_) {
                const w3d::RectXZ z = w3d::MELD_ZONE[e.player];
                pcam_.glanceAt({(z.x0 + z.x1) * 0.5f, w3d::TABLE_Y, (z.z0 + z.z1) * 0.5f}, 1.1f);
            }
            // katlamalı: say where the bar is now (while the player still has to open)
            if (game_.rules().katlamali && e.player != HUMAN && !game_.player(HUMAN).opened) {
                const bool pairs = e.player >= 0 && game_.player(e.player).openedWithPairs;
                table_.toast(pairs ? "Katlamalı: çift açmak için artık en az " + std::to_string(game_.pairsOpenNeed()) + " çift"
                                   : "Katlamalı: açmak için artık en az " + std::to_string(game_.seriesOpenNeed()),
                             ui::pal::Highlight, 3.2f);
            }
            break;
        case EvType::HandEnd: {
            cancelThink();
            flow_ = Flow::HandOver;
            handOverT_ = 0.f;
            ++handsPlayed_;
            recordOkeyHand();
            saveMatch();
            crowdAtHandEnd();
            achievementsAtHandEnd(); // Başarımlar
            if (game_.handState() == okey::HandState::MatchOver) {
                const int lead = game_.leaderSeat();
                const bool won = lead == HUMAN || (game_.teams() && lead == okey::Game::partnerOf(HUMAN));
                archiveMatch(won ? std::string("Kazandın") : game_.player(lead).name + " kazandı");
                analyzeFinishedMatch();
            }
            if (opt_.autoplay && !snapshot_) {
                const okey::HandResult& r = game_.lastHandResult();
                const std::string of = game_.classic() ? std::string() : "/" + std::to_string(game_.numHands());
                std::printf("[autoplay] hand %d%s: %s | scores %d %d %d %d | totals %d %d %d %d\n", game_.handIndex() + 1,
                            of.c_str(), e.text.c_str(), r.score[0], r.score[1], r.score[2], r.score[3],
                            game_.player(0).totalScore, game_.player(1).totalScore, game_.player(2).totalScore,
                            game_.player(3).totalScore);
            }
            break;
        }
        case EvType::MatchEnd:
            if (opt_.autoplay && !snapshot_) std::printf("[autoplay] %s\n", e.text.c_str());
            break;
        default: break;
        }
    }
}

// Tile sounds that belong to a tile landing are held back so the clack comes when the tile touches the felt;
// an opponent's tiles also wait for its hand (w3d::BOT_*_LEAD).
void App::routeAudio(const okey::GameEvent& e) {
    if (!audioOn_) return;
    using okey::EvType;
    const bool bot = e.player >= 0 && e.player < 4 && e.player != HUMAN;
    switch (e.type) {
    case EvType::DrawPile:
    case EvType::TakeLeft:
        if (bot) delayed_.push_back({e, w3d::BOT_TAKE_LEAD / animSpeed()});
        else audio_.onEvent(e);
        break;
    case EvType::Discard:
    case EvType::AddToMeld:
    case EvType::SwapJoker: delayed_.push_back({e, (LANDING_LAG + (bot ? w3d::BOT_GIVE_LEAD : 0.f)) / animSpeed()}); break;
    case EvType::Open:
    case EvType::LayMelds: delayed_.push_back({e, (LANDING_LAG + (bot ? w3d::BOT_MELD_LEAD : 0.f)) / animSpeed()}); break;
    default: audio_.onEvent(e); break;
    }
}

void App::updateDelayedAudio(float simDt) {
    for (size_t i = 0; i < delayed_.size();) {
        delayed_[i].t -= simDt;
        if (delayed_[i].t <= 0.f) {
            if (audioOn_) audio_.onEvent(delayed_[i].e);
            delayed_.erase(delayed_.begin() + (std::ptrdiff_t)i);
        } else {
            ++i;
        }
    }
}

void App::updateScoreboard() {
    if (otherInGame()) {
        room_.setScoreboard(other_->scoreTitle(), other_->scoreLines());
        return;
    }
    if (flow_ == Flow::Title || game_.handState() == okey::HandState::NotStarted) {
        room_.setScoreboard("", {});
        return;
    }
    std::string title;
    if (game_.classic())
        title = game_.handState() == okey::HandState::MatchOver ? std::string("Okey bitti")
                                                                 : "Okey · " + std::to_string(game_.handIndex() + 1) + ". el";
    else
        title = game_.handState() == okey::HandState::MatchOver
                    ? std::string("Maç bitti")
                    : std::string(game_.teams() ? "Eşli · " : "") + "El " +
                          std::to_string(std::min(game_.handIndex() + 1, game_.numHands())) + " / " +
                          std::to_string(game_.numHands()) + (game_.rules().katlamali ? " · Katlamalı" : "") +
                          // 101 kuralları
                          (game_.classic() ? "" : game_.rules().finishMult == okey::FinishMult::Single ? " · Tek kat"
                                                : game_.rules().finishMult == okey::FinishMult::None   ? " · Katsız" : "");
    std::vector<std::string> lines;
    for (int s = 0; s < 4; ++s) lines.push_back(game_.player(s).name + " ....... " + std::to_string(game_.player(s).totalScore));
    if (game_.teams()) {
        lines.clear();
        for (int s = 0; s < 2; ++s)
            lines.push_back(game_.player(s).name + " + " + game_.player(s + 2).name + " ... " + std::to_string(game_.teamTotal(s)));
    }
    room_.setScoreboard(title, lines);
}

} // namespace detail
} // namespace app
