// Banter lines and the logic deciding who says what, when. Turkish, friendly, no profanity.
#include "ui/Banter.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// Situations. Each has three line tables: [0] Hacı Rıza (seat 1), [1] Kel Mahmut (seat 2),
// [2] Emekli Nuri (seat 3). Templates: {v} value, {p} actor, {g} giver, {h} human address.
enum Sit {
    S_HandStart,
    S_TurnSelf,
    S_WaitHuman,
    S_DrawSelf,
    S_TakeLeftSelf,
    S_TakeLeftVictim,
    S_HumanTookMine,
    S_OpenSelf,
    S_OpenSelfPairs,
    S_OpenOther,
    S_OpenOtherBig,
    S_OpenOtherPairs,
    S_OpenHuman,
    S_AddSelf,
    S_AddOther,
    S_SwapSelf,
    S_SwapOther,
    S_PenaltySelf,
    S_PenaltyOther,
    S_PenaltyHuman,
    S_LowPile,
    S_WinSelf,
    S_WinOkey,
    S_LoseGrumble,
    S_HumanWon,
    S_PileOut,
    S_MatchWinSelf,
    S_MatchLose,
    S_MatchHumanWon,
    S_Idle,
    S_TeaOrder,
    S_TeaThanks,
    S_Goal,
    S_Count
};

struct Tbl {
    const char* const* lines;
    int n;
};

#define LINES(name, ...) static const char* const name[] = {__VA_ARGS__};
#define T(name) Tbl{name, (int)(sizeof(name) / sizeof(name[0]))}

// ---------------------------------------------------------------- hand start
LINES(kHandStartR, "Hayırlısı olsun beyler.", "Bismillah, dağıtın bakalım.", "Bu el kısmet kimeyse.",
      "Sabır ve dikkat, gerisi gelir.", "Allah bereket versin, başlayalım.")
LINES(kHandStartM, "Bu el benim abi!", "Hadi hadi, dağıt şu taşları!", "Bu sefer kimse beni tutamaz!",
      "Isındım beyler, dikkat edin!", "Taşlar bu sefer bana güzel gelecek, hissediyorum!")
LINES(kHandStartN, "Yeni el, eski dert.", "Bakalım bu sefer ne felaket gelecek.",
      "İyi karıştırdınız mı? Bizim zamanımızda iyice karıştırılırdı.",
      "Hadi başlayalım da akşam ezanına yetişeyim.", "Gözlüğümü taktım, artık hiçbir şey kaçmaz.")
// ---------------------------------------------------------------- own turn
LINES(kTurnR, "Bismillah…", "Bakalım kısmette ne var.", "Acele işe şeytan karışır…", "Hmm, bir düşüneyim.",
      "Yavaş yavaş…")
LINES(kTurnM, "Gel gel gel…", "Hadi güzel bir taş!", "Bakın şimdi ne yapacağım!", "Sıra bende, izleyin!",
      "Hah, işte şimdi oldu!")
LINES(kTurnN, "Hıh, yine ne gelecek kim bilir.", "Gözlüğüm nerede… ha, burada.", "Ah şu dizlerim…",
      "Sıra bende mi? Tamam tamam.", "Acele ettirmeyin beni.")
// ---------------------------------------------------------------- waiting for the human
LINES(kWaitR, "Acele etme {h}, ama sabahı da etme.", "Düşün taşın, sonra at taşı {h}.", "{h}, çayın soğuyor.",
      "Sabreden derviş muradına ermiş… ama bu kadar da değil.", "Hayırdır {h}, taşlarla sohbete mi daldın?")
LINES(kWaitM, "Hadi be {h}, uyuma!", "{h}, taş atacaksın, roket fırlatmıyorsun!", "Maç başlayacak, hadi {h}!",
      "Uyudun mu {h}? Hadi!", "Hadi ama, sabah oldu!")
LINES(kWaitN, "Bizim zamanımızda bu kadar düşünen olmazdı…", "Ben bu sırada iki oralet içtim {h}.",
      "Hadi {h}, torunumu okuldan alacağım.", "Gençler hep böyle, taşa telefon gibi bakıyor.",
      "{h}, emekli maaşım yatacak, hadi!")
// ---------------------------------------------------------------- drawing from the pile (rare)
LINES(kDrawR, "Hayırlısı…", "Bu da bir kısmet.")
LINES(kDrawM, "Yine işe yaramaz taş!", "Hah, güzel geldi!", "Ne bu be!")
LINES(kDrawN, "Hıh, yine boş taş.", "Bu taşı kim çekti ya… ha, ben.")
// ---------------------------------------------------------------- taking from the left
LINES(kTakeSelfR, "Sağ ol komşu!", "Allah razı olsun, tam lazımdı.", "Bu taş bana nasipmiş.",
      "Eyvallah, işime yarar.")
LINES(kTakeSelfM, "Sağ ol komşu!", "Oh be, tam aradığım taş!", "Bunu bana mı attın? Eyvallah!",
      "Ellerine sağlık {g}!")
LINES(kTakeSelfN, "Hah, bu taş benim işime yarar.", "Sağ ol {g}, eline sağlık.",
      "Bizim zamanımızda böyle cömert taş atılmazdı!")
LINES(kTakeVictimR, "Hayırlı olsun, benden sana.", "Eh, kısmetin varmış {p}.")
LINES(kTakeVictimM, "Taşımı kaptı ya!", "Keşke atmasaydım o taşı…", "Dur be {p}, o taş bana lazımdı!")
LINES(kTakeVictimN, "Aman al, senin olsun…", "Gözün taşımda mıydı {p}?", "Hıh, benim taşımla el açacak şimdi.")
LINES(kHumanTookR, "Hayırlı olsun {h}, işine yarasın.")
LINES(kHumanTookM, "Vay, yandan aldın ha {h}! Dikkat beyler!")
LINES(kHumanTookN, "Al o da senin olsun {h}…", "Gözün taşımdaymış demek {h}.", "Bak sen, taşımı kaptı!")
// ---------------------------------------------------------------- opening
LINES(kOpenSelfR, "Bismillah, {v} ile açtım.", "Sabreden derviş muradına ermiş: {v}!", "Eh, {v}… hayırlısı.",
      "Yavaş yavaş, {v} ile açıldık.")
LINES(kOpenSelfM, "Al sana {v}!", "{v}! Bu el benim abi!", "İşte böyle açılır el! {v}!",
      "Açtım gitti! {v}, hadi bakalım!", "Görün beyler: {v}!")
LINES(kOpenSelfN, "{v} ile açtım. Bizim zamanımızda 101'i ilk turda geçerdik.", "Açtım, rahat bırakın beni artık.",
      "Hıh, {v} ile açtım. Fena değil.")
LINES(kOpenPairsR, "Çiftten gidiyorum, hayırlısı.", "{v} çift, kısmet böyleymiş.")
LINES(kOpenPairsM, "Çiftten gidiyorum beyler!", "{v} çift! Korkun benden!")
LINES(kOpenPairsN, "Çift açtım, gençler öğrensin.", "{v} çift. Eskiden de çiftten giderdim.")
LINES(kOpenOtherR, "Hayırlı olsun {p}.", "Maşallah, açtı bile.")
LINES(kOpenOtherM, "Vay be, açtı bak!", "Dikkat beyler, {p} açtı!")
LINES(kOpenOtherN, "Açtı mı? Eh, 101'i zor geçti.", "Hıh, şanslı adam.")
LINES(kOpenBigR, "{v} mü? Maşallah, nazar değmesin!")
LINES(kOpenBigM, "{v} mü? Hile var bunda!", "Oha, {v}! Bu ne be!")
LINES(kOpenBigN, "{v}! Bizim zamanımızda bu kadar taş bir arada gelmezdi.")
LINES(kOpenOtherPairsR, "Çiftten mi? Cesaret ister.")
LINES(kOpenOtherPairsM, "Çift açtı, eyvah! Dikkat!")
LINES(kOpenOtherPairsN, "Çiftten gidiyor, cesur adam.")
LINES(kOpenHumanR, "Maşallah {h}, güzel açtın.")
LINES(kOpenHumanM, "Vay, açtın ha {h}! Bak sen!", "{v} mü? Fena değilsin {h}!")
LINES(kOpenHumanN, "Hıh, {h} açtı. Acemi şansı.")
// ---------------------------------------------------------------- işleme / joker
LINES(kAddSelfR, "Bir taş da buraya.", "Damlaya damlaya göl olur.")
LINES(kAddSelfM, "İşledim gitti!", "Bir tane daha, al buraya!")
LINES(kAddSelfN, "Şunu da buraya koyalım.", "Hıh, bu da buraya.")
LINES(kAddOtherR, "Güzel işledin {p}.")
LINES(kAddOtherM, "Yine mi işledin {p}!")
LINES(kAddOtherN, "Ne kadar da işliyor, maşallah.")
LINES(kSwapSelfR, "Okey yerine geldi, elhamdülillah.", "Emanet okeyi alıyorum.")
LINES(kSwapSelfM, "Okeyi kaptım!", "Okey benim abi!", "Gel bakalım okey, gel!")
LINES(kSwapSelfN, "Okeyi aldım, kimse görmedi sandınız.", "Hah, okey bende.")
LINES(kSwapOtherR, "Okeyi aldı, dikkat edin.")
LINES(kSwapOtherM, "Eyvah, okey gitti!", "Nasıl kaptı o okeyi!")
LINES(kSwapOtherN, "Okeyi kaptırdık, bravo bize.")
// ---------------------------------------------------------------- penalties
LINES(kPenSelfR, "Hay Allah, dikkat etmedim.", "Olur böyle şeyler, sabır.")
LINES(kPenSelfM, "Of be, görmedim!", "Tüh! Olsun, bir dahakine.")
LINES(kPenSelfN, "Gözlüğüm buğulandı, ondan!", "Göremedim valla, ışık az burada.")
LINES(kPenOtherR, "Dikkat {p}, hesaba yazıldı.", "Acele işe şeytan karışır {p}.")
LINES(kPenOtherM, "Ceza yazın ceza!", "Hesaba bir 101 daha!", "Ha ha! {p}, gözünü seveyim!")
LINES(kPenOtherN, "Ceza! Kalemi verin, ben yazarım.", "Gözlüğü tak da öyle at {p}!")
LINES(kPenHumanR, "Olur öyle {h}, dikkat et.")
LINES(kPenHumanM, "Ceza yazın ceza! 101'i yedin {h}!", "Ha ha, hoş geldin 101!")
LINES(kPenHumanN, "Bizim zamanımızda böyle hatayı çıraklar yapardı.")
// ---------------------------------------------------------------- low pile
LINES(kLowR, "Taşlar azaldı, dikkatli olun.", "Son taşlar, hayırlısı.", "Bitmeye yakın, sabır.",
      "Deste inceldi, akıllı oynayın.", "Kısmet son taşlardaymış demek.")
LINES(kLowM, "Taşlar bitiyor beyler…", "Hadi bitirin artık, taş bitiyor!", "Ortada birkaç taş kaldı abi!",
      "Az kaldı, son düzlük!", "Son taşlar beyler, dikkat!")
LINES(kLowN, "Taşlar bitiyor, kimse bitiremeyecek bu gidişle.", "Son taşlar… bizim zamanımızda çoktan biterdi.",
      "Eh, ortada birkaç taş kaldı.", "Hıh, ortada üç beş taş kaldı.", "Bu el de böyle biter, görürsünüz.")
// ---------------------------------------------------------------- end of hand
LINES(kWinR, "Elhamdülillah, bitti.", "Sabreden derviş muradına ermiş!", "Hayırlı olsun, bu el bizden.")
LINES(kWinM, "Bitti! Eller havaya!", "İşte bu! Bu el benim abi!", "Gooool! Ay pardon, bitti!", "Kim tutar beni!")
LINES(kWinN, "Bitirdim. Bizim zamanımızda da böyle bitirirdik.", "Hıh, gördünüz mü? Yaşlı kurt!",
      "Bitti. Şimdi çaylar sizden.")
LINES(kWinOkeyR, "Okeyle bitti, hayırlısı.")
LINES(kWinOkeyM, "Okeyle bitirdim abi, çift yazın!")
LINES(kWinOkeyN, "Okeyle bitirdim, ders olsun.")
LINES(kLoseR, "Hayırlısı, bir sonraki ele.", "Kısmet değilmiş.")
LINES(kLoseM, "Bu elde şans yoktu abi.", "Taşlar hep ona geliyor!", "Bir dahaki el benim, yazın bir kenara!")
LINES(kLoseN, "Bu taşlarla kim bitirir ki?", "Bizim zamanımızda böyle şans olmazdı.", "Hıh, yine kaybettik.")
LINES(kHumanWonR, "Maşallah {h}, helal olsun!", "Tebrikler {h}, güzel bitirdin.")
LINES(kHumanWonM, "Vay be {h}, helal olsun!", "Şansına şaşayım {h}!")
LINES(kHumanWonN, "Acemi şansı… ama tebrikler.", "Hıh, bitirdi bak. Aferin {h}.")
LINES(kPileOutR, "Taşlar bitti, kimse bitiremedi. Kısmet.")
LINES(kPileOutM, "Taş bitti abi, kimse bitiremedi!")
LINES(kPileOutN, "Dedim size, bu el biter mi?")
// ---------------------------------------------------------------- end of match
LINES(kMatchWinR, "Oyun bizden, çaylar benden!", "Elhamdülillah, güzel oyundu.",
      "Kazanan da kaybeden de dost kalsın.")
LINES(kMatchWinM, "Maç benim beyler! Çaycı, herkese çay!", "Şampiyon kim? Mahmut!", "Kupayı getirin!")
LINES(kMatchWinN, "Maçı aldım. Tecrübe delikanlı, tecrübe.", "Bizim zamanımızda da hep ben kazanırdım.")
LINES(kMatchLoseR, "Güzel oyundu, hayırlı olsun.")
LINES(kMatchLoseM, "Rövanş isterim!", "Yarın aynı saatte, rövanş!")
LINES(kMatchLoseN, "Bu taşlar bozuk, yarın kendi taşlarımı getireceğim.")
LINES(kMatchHumanR, "Helal olsun {h}, bizi güzel yendin.")
LINES(kMatchHumanM, "{h}, bizi fena yendin! Rövanş var ama!")
LINES(kMatchHumanN, "Hıh… tebrikler {h}. Yarın yine gel.")
// ---------------------------------------------------------------- idle chatter
LINES(kIdleR, "Sabreden derviş muradına ermiş.", "Damlaya damlaya göl olur.",
      "Bir kahvenin kırk yıl hatırı vardır.", "Acele işe şeytan karışır.", "Komşu komşunun külüne muhtaçtır.",
      "Az olsun, öz olsun.", "İşleyen demir ışıldar.", "Bu kahvehane benim gençliğimden beri burada.",
      "Bu tespih kırk yıllık, hacdan getirdim.", "Akşama yağmur yağacak, belli.", "Sakla samanı, gelir zamanı.",
      "Çay demini aldı mı, ondan sonra güzel.")
LINES(kIdleM, "Fener yine kaybetmiş…", "Dün akşamki maçı gördünüz mü? Hakem rezalet!",
      "Bu sene şampiyonluk bizim abi!", "Ben gençken top oynardım, sol açık!",
      "Şu televizyonun sesini açın biraz!", "O penaltıyı ben atsam girerdi!", "Maç kaç kaç oldu?",
      "Abi transfer yapmıyorlar, ne olacak bu takımın hali?", "Bu akşam derbi var, kaçırmam!",
      "Hakem düdüğü yutmuş abi!", "Benim bir kuzenim vardı, az kalsın milli olacaktı!",
      "Ofsayt mı o? Değil o, değil!")
LINES(kIdleN, "Bizim zamanımızda böyle oynanmazdı…", "Eskiden çay kırk kuruştu.",
      "Yağmur yağacakmış, dizlerim sızlıyor.", "Şu gençler hep telefonda.",
      "Emekli maaşıyla çay içilmiyor artık.", "Bu taşlar eskiden kemikti, şimdi plastik.", "Hava da soğudu ha.",
      "Otuz yıl memurluk yaptım, bir gün geç kalmadım.", "Bu kahvenin eski sahibi Rıfat Efendi, ne adamdı!",
      "Radyoda eskiden ne güzel şarkılar çalardı.", "Doktor çayı azalt dedi, ben de oraleti çoğalttım.",
      "Torunum bilgisayarda okey oynuyormuş. Olur mu öyle şey!")
// ---------------------------------------------------------------- tea
LINES(kTeaR, "Çaycı! Bir çay daha evlat.", "Çaycı, tazele şunları, Allah razı olsun.",
      "Evlat, çaylar benden, getir bakalım.", "Çaycı, herkese birer çay.", "Çaycı! Demli olsun evlat.")
LINES(kTeaM, "Çaycı! İki çay bir oralet!", "Çaycı! Çaylar nerede kaldı abi?", "Oğlum, tavşan kanı olsun, hadi!",
      "Çaycııı!", "Çaycı, sıcak sıcak getir!")
LINES(kTeaN, "Çaycı! Bana bir oralet, portakallı!", "Evladım, oralet soğudu, yenisini getir.",
      "Çaycı, benimki açık olsun, tansiyon var.", "Bir oralet daha, ama bu sefer sıcak olsun!",
      "Bu çaycı da hep geç kalır.")
LINES(kThanksR, "Eline sağlık evlat.", "Allah razı olsun.")
LINES(kThanksM, "Sağ ol aslanım!", "Oh be, tavşan kanı!")
LINES(kThanksN, "Sıcak mı bu? Hah, iyi.", "Sağ ol evladım.")
LINES(kGoalR, "Maşallah, güzel gol.")
LINES(kGoalM, "Gooool!", "Oley! Gördünüz mü?", "Ofsayt o abi, ofsayt!")
LINES(kGoalN, "Neydi o savunma öyle!", "Bizim zamanımızda böyle gol yenmezdi.")

const Tbl kTables[S_Count][3] = {
    {T(kHandStartR), T(kHandStartM), T(kHandStartN)},
    {T(kTurnR), T(kTurnM), T(kTurnN)},
    {T(kWaitR), T(kWaitM), T(kWaitN)},
    {T(kDrawR), T(kDrawM), T(kDrawN)},
    {T(kTakeSelfR), T(kTakeSelfM), T(kTakeSelfN)},
    {T(kTakeVictimR), T(kTakeVictimM), T(kTakeVictimN)},
    {T(kHumanTookR), T(kHumanTookM), T(kHumanTookN)},
    {T(kOpenSelfR), T(kOpenSelfM), T(kOpenSelfN)},
    {T(kOpenPairsR), T(kOpenPairsM), T(kOpenPairsN)},
    {T(kOpenOtherR), T(kOpenOtherM), T(kOpenOtherN)},
    {T(kOpenBigR), T(kOpenBigM), T(kOpenBigN)},
    {T(kOpenOtherPairsR), T(kOpenOtherPairsM), T(kOpenOtherPairsN)},
    {T(kOpenHumanR), T(kOpenHumanM), T(kOpenHumanN)},
    {T(kAddSelfR), T(kAddSelfM), T(kAddSelfN)},
    {T(kAddOtherR), T(kAddOtherM), T(kAddOtherN)},
    {T(kSwapSelfR), T(kSwapSelfM), T(kSwapSelfN)},
    {T(kSwapOtherR), T(kSwapOtherM), T(kSwapOtherN)},
    {T(kPenSelfR), T(kPenSelfM), T(kPenSelfN)},
    {T(kPenOtherR), T(kPenOtherM), T(kPenOtherN)},
    {T(kPenHumanR), T(kPenHumanM), T(kPenHumanN)},
    {T(kLowR), T(kLowM), T(kLowN)},
    {T(kWinR), T(kWinM), T(kWinN)},
    {T(kWinOkeyR), T(kWinOkeyM), T(kWinOkeyN)},
    {T(kLoseR), T(kLoseM), T(kLoseN)},
    {T(kHumanWonR), T(kHumanWonM), T(kHumanWonN)},
    {T(kPileOutR), T(kPileOutM), T(kPileOutN)},
    {T(kMatchWinR), T(kMatchWinM), T(kMatchWinN)},
    {T(kMatchLoseR), T(kMatchLoseM), T(kMatchLoseN)},
    {T(kMatchHumanR), T(kMatchHumanM), T(kMatchHumanN)},
    {T(kIdleR), T(kIdleM), T(kIdleN)},
    {T(kTeaR), T(kTeaM), T(kTeaN)},
    {T(kThanksR), T(kThanksM), T(kThanksN)},
    {T(kGoalR), T(kGoalM), T(kGoalN)},
};

// Two-line exchanges for idle chatter: seat A says a, seat B answers b.
struct Dialogue {
    int a;
    const char* la;
    int b;
    const char* lb;
};
const Dialogue kDialogues[] = {
    {2, "Fener yine kaybetmiş…", 3, "Bizim zamanımızda futbolcular ter dökerdi!"},
    {3, "Eskiden çay kırk kuruştu.", 1, "Eski günler, eski günler…"},
    {2, "Dün akşamki maçı gördünüz mü? Hakem rezalet!", 1, "Maç maçtır evlat, sinirlenme."},
    {1, "Akşama yağmur yağacak, belli.", 3, "Dizlerim zaten söyledi."},
    {2, "Bu sene şampiyonluk bizim abi!", 3, "Her sene aynı lafı söylüyorsun Mahmut."},
    {3, "Şu gençler hep telefonda.", 2, "Haklısın Nuri Abi, ben de sadece maça bakıyorum."},
    {1, "Bir kahvenin kırk yıl hatırı vardır.", 2, "O zaman bir kahve ısmarla Hacı Abi!"},
    {3, "Doktor çayı azalt dedi.", 1, "Doktor ne bilir, çay şifadır."},
    {2, "Şu televizyonun sesini açın biraz!", 3, "Açma açma, kafam şişti zaten."},
};

constexpr float kGlobalGap = 2.4f;
constexpr float kSeatCooldown = 7.5f;

std::string lastWord(const std::string& s) {
    size_t e = s.find_last_not_of(' ');
    if (e == std::string::npos) return s;
    size_t b = s.find_last_of(' ', e);
    return s.substr(b == std::string::npos ? 0 : b + 1, e - (b == std::string::npos ? 0 : b + 1) + 1);
}

// Upper-case the first letter (ASCII + Turkish i -> İ); the rest stays as is.
std::string capitalizeFirst(const std::string& s) {
    if (s.empty()) return s;
    unsigned char c = (unsigned char)s[0];
    if (c == 'i') return std::string("\xC4\xB0") + s.substr(1);
    if (c >= 'a' && c <= 'z') {
        std::string r = s;
        r[0] = (char)(c - 32);
        return r;
    }
    return s;
}

} // namespace

// ---------------------------------------------------------------- setup
void Banter::reset(uint64_t seed) {
    rng_ = seed ? seed : 0x5EEDull;
    seatCool_.fill(0.f);
    globalCool_ = 0.f;
    idleIn_ = 16.f + rnd() * 10.f;
    humanWait_ = 0.f;
    nagLevel_ = 0;
    turnSeat_ = -1;
    handLive_ = false;
    lowPileSaid_ = false;
    teaOrderCool_ = 25.f;
    std::fill(std::begin(lastPick_), std::end(lastPick_), -1);
    std::fill(std::begin(lastDialogue_), std::end(lastDialogue_), -1);
    recent_.clear();
    queue_.clear();
}

void Banter::setNames(const std::array<std::string, 4>& names) { names_ = names; }

void Banter::setEnabled(bool on) {
    enabled_ = on;
    if (!on) queue_.clear();
}

void Banter::setGameRunning(bool running) { gameRunning_ = running; }

int Banter::lineCount() {
    int n = 0;
    for (const auto& row : kTables)
        for (const Tbl& t : row) n += t.n;
    return n + 2 * (int)(sizeof(kDialogues) / sizeof(kDialogues[0]));
}

// ---------------------------------------------------------------- helpers
float Banter::rnd() {
    uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (float)(z >> 40) * (1.0f / 16777216.0f);
}

int Banter::rndInt(int n) { return n <= 0 ? 0 : std::min(n - 1, (int)(rnd() * (float)n)); }

int Banter::pickBot() { return 1 + rndInt(3); }

int Banter::pickOther(int notSeat) {
    int s = 1 + rndInt(3);
    if (s == notSeat) s = (s % 3) + 1;
    return s;
}

std::string Banter::shortName(int seat) const {
    if (seat <= 0 || seat > 3) return "";
    std::string w = lastWord(names_[seat]);
    return w.empty() ? names_[seat] : w;
}

// How one regular addresses another at this table: Mahmut, the youngest, says "abi" to both elders; Nuri
// calls Rıza by his title; Rıza, the eldest, is fatherly with Mahmut. The human keeps humanAddress().
std::string Banter::address(int speaker, int target) const {
    if (target == 0) return humanAddress(speaker);
    static const char* const kAddr[4][4] = {
        {"", "", "", ""},
        {"", "", "Mahmut evladım", "Nuri"},  // Hacı Rıza ->
        {"", "Hacı Abi", "", "Nuri Abi"},    // Kel Mahmut ->
        {"", "Hacı", "Mahmut", ""},          // Emekli Nuri ->
    };
    if (speaker >= 1 && speaker <= 3 && target >= 1 && target <= 3 && kAddr[speaker][target][0] != 0)
        return kAddr[speaker][target];
    return shortName(target);
}

std::string Banter::humanAddress(int speaker) const {
    const std::string& n = names_[0];
    if (!n.empty() && n != "Sen" && n != "sen" && n != "Oyuncu") return n;
    switch (speaker) {
    case 1: return "evlat";
    case 2: return "abi";
    default: return "delikanlı";
    }
}

std::string Banter::fill(const std::string& tpl, int speaker, int value, int actor) const {
    std::string out;
    out.reserve(tpl.size() + 16);
    for (size_t i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == '{' && i + 2 < tpl.size() && tpl[i + 2] == '}') {
            char k = tpl[i + 1];
            if (k == 'v') {
                out += std::to_string(value);
            } else if (k == 'h') {
                out += humanAddress(speaker);
            } else if (k == 'p') {
                out += actor >= 0 ? address(speaker, actor) : std::string();
            } else if (k == 'g') {
                int giver = actor >= 0 ? okey::Game::leftOf(actor) : -1;
                out += giver >= 0 ? address(speaker, giver) : std::string();
            } else {
                out += tpl.substr(i, 3);
            }
            i += 2;
            continue;
        }
        out += tpl[i];
    }
    return capitalizeFirst(out);
}

bool Banter::say(int seat, int tableId, float delay, bool force, int value, int actor, float maxWait,
                 BanterCue cue) {
    if (!enabled_ || seat < 1 || seat > 3 || tableId < 0 || tableId >= S_Count) return false;
    if (!force && (seatCool_[seat] > 0.f || globalCool_ > 0.f)) return false;
    // never stack more than a couple of pending lines
    if (queue_.size() >= 3 && !force) return false;
    // one pending line per seat: an important line replaces a casual one
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->line.seat == seat) {
            if (!force) return false;
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    const Tbl& t = kTables[tableId][seat - 1];
    if (t.n <= 0) return false;
    int slot = std::min(127, tableId * 3 + (seat - 1));
    std::string text;
    int idx = 0;
    for (int attempt = 0; attempt < 6; ++attempt) { // prefer a line nobody said lately
        idx = rndInt(t.n);
        if (t.n > 1 && idx == lastPick_[slot]) idx = (idx + 1 + rndInt(t.n - 1)) % t.n;
        text = fill(t.lines[idx], seat, value, actor);
        if (!isRecent(text)) break;
    }
    if (isRecent(text) && !force) return false;
    lastPick_[slot] = idx;
    remember(text);

    Pending p;
    p.line.seat = seat;
    p.line.text = text;
    p.line.delay = delay;
    p.line.seconds = std::clamp(1.9f + 0.045f * (float)p.line.text.size(), 2.4f, 4.6f);
    p.line.maxWait = maxWait;
    p.line.cue = cue;
    queue_.push_back(p);
    seatCool_[seat] = std::max(seatCool_[seat], kSeatCooldown + delay);
    globalCool_ = std::max(globalCool_, kGlobalGap + delay);
    return true;
}

bool Banter::isRecent(const std::string& text) const {
    return std::find(recent_.begin(), recent_.end(), text) != recent_.end();
}

void Banter::remember(const std::string& text) {
    recent_.push_back(text);
    while (recent_.size() > 24) recent_.pop_front();
}

// ---------------------------------------------------------------- game events
void Banter::onEvent(const okey::GameEvent& e, const okey::Game& g) {
    if (!enabled_) return;
    const int actor = e.player;
    const bool actorBot = actor >= 1 && actor <= 3;
    using okey::EvType;
    switch (e.type) {
    case EvType::MatchStart:
        lowPileSaid_ = false;
        break;
    case EvType::HandStart:
        handLive_ = true;
        lowPileSaid_ = false;
        humanWait_ = 0.f;
        nagLevel_ = 0;
        if (chance(0.6f)) say(pickBot(), S_HandStart, 1.2f, true, 0, -1, 5.f);
        break;
    case EvType::TurnStart:
        turnSeat_ = actor;
        humanWait_ = 0.f;
        nagLevel_ = 0;
        if (actorBot && chance(0.05f)) say(actor, S_TurnSelf, 0.1f);
        break;
    case EvType::DrawPile:
        if (actorBot && chance(0.03f)) say(actor, S_DrawSelf, 0.3f);
        if (!lowPileSaid_ && g.pileCount() <= 6 && g.pileCount() > 0 && handLive_) {
            lowPileSaid_ = true;
            if (chance(0.85f)) say(pickBot(), S_LowPile, 0.6f, true, 0, -1, 5.f);
        }
        break;
    case EvType::TakeLeft: {
        int giver = okey::Game::leftOf(actor);
        if (actorBot) {
            if (chance(0.4f)) say(actor, S_TakeLeftSelf, 0.2f, false, 0, actor);
            else if (giver >= 1 && chance(0.4f)) say(giver, S_TakeLeftVictim, 0.4f, false, 0, actor);
        } else if (actor == 0) {
            if (giver >= 1 && chance(0.55f)) say(giver, S_HumanTookMine, 0.3f, false, 0, actor);
            else if (chance(0.25f)) say(pickOther(giver), S_HumanTookMine, 0.5f, false, 0, actor);
        }
        break;
    }
    case EvType::Open: {
        bool pairs = actor >= 0 && actor < 4 && g.player(actor).openedWithPairs;
        int v = e.amount;
        if (actorBot) {
            say(actor, pairs ? S_OpenSelfPairs : S_OpenSelf, 0.25f, true, v, actor, 4.f);
            if (chance(0.55f)) {
                int other = pickOther(actor);
                int sit = pairs ? S_OpenOtherPairs : (v >= 130 ? S_OpenOtherBig : S_OpenOther);
                say(other, sit, 2.4f, true, v, actor, 5.f);
            }
        } else if (actor == 0) {
            int sit = pairs ? S_OpenOtherPairs : (v >= 130 ? S_OpenOtherBig : S_OpenHuman);
            if (chance(0.8f)) say(pickBot(), sit, 0.6f, true, v, actor, 4.f);
        }
        break;
    }
    case EvType::LayMelds:
        if (actorBot && chance(0.15f)) say(actor, S_AddSelf, 0.2f);
        break;
    case EvType::AddToMeld:
        if (actorBot && chance(0.12f)) say(actor, S_AddSelf, 0.2f);
        else if (chance(0.06f)) say(pickOther(actor), S_AddOther, 0.5f, false, 0, actor);
        break;
    case EvType::SwapJoker:
        if (actorBot && chance(0.85f)) say(actor, S_SwapSelf, 0.2f, true, 0, actor);
        if (chance(0.5f)) say(pickOther(actor), S_SwapOther, 2.2f, false, 0, actor);
        break;
    case EvType::Penalty:
        if (actor == 0) {
            if (chance(0.85f)) say(pickBot(), S_PenaltyHuman, 0.5f, true, 0, actor, 4.f);
        } else if (actorBot) {
            if (chance(0.7f)) say(pickOther(actor), S_PenaltyOther, 0.4f, true, 0, actor, 4.f);
            if (chance(0.45f)) say(actor, S_PenaltySelf, 2.6f, false, 0, actor, 5.f);
        }
        break;
    case EvType::HandEnd: {
        handLive_ = false;
        turnSeat_ = -1;
        const okey::HandResult& r = g.lastHandResult();
        int w = actor;
        if (w >= 1 && w <= 3) {
            say(w, r.finishedWithJoker ? S_WinOkey : S_WinSelf, 0.3f, true, 0, w, 6.f);
            if (chance(0.7f)) say(pickOther(w), S_LoseGrumble, 2.6f, true, 0, w, 7.f);
        } else if (w == 0) {
            say(pickBot(), S_HumanWon, 0.4f, true, 0, 0, 6.f);
        } else {
            say(pickBot(), S_PileOut, 0.4f, true, 0, -1, 6.f);
        }
        break;
    }
    case EvType::MatchEnd: {
        int w = actor;
        // A shared first place (several seats on the lowest total, as the match-over screen shows): nobody
        // claims the match; a bot outside first place, if any, grumbles.
        int leaders = 0, outBots[3], nOut = 0;
        for (int s = 0; w >= 0 && w <= 3 && s <= 3; ++s) {
            if (g.player(s).totalScore == g.player(w).totalScore) ++leaders;
            else if (s >= 1) outBots[nOut++] = s;
        }
        if (leaders > 1) {
            if (nOut > 0) say(outBots[rndInt(nOut)], S_MatchLose, 1.6f, true, 0, w, 9.f);
        } else if (w >= 1 && w <= 3) {
            say(w, S_MatchWinSelf, 1.0f, true, 0, w, 8.f);
            say(pickOther(w), S_MatchLose, 3.6f, true, 0, w, 9.f);
        } else if (w == 0) {
            say(pickBot(), S_MatchHumanWon, 1.0f, true, 0, 0, 8.f);
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- time based
void Banter::update(float dt, bool teaLow, bool teaBoyBusy) {
    for (float& c : seatCool_) c = std::max(0.f, c - dt);
    globalCool_ = std::max(0.f, globalCool_ - dt);
    teaOrderCool_ = std::max(0.f, teaOrderCool_ - dt);
    for (Pending& p : queue_) p.line.delay -= dt;
    if (!enabled_) return;

    // the human is taking a long time (the clock stands still while the game is paused or behind a menu)
    if (handLive_ && turnSeat_ == 0 && gameRunning_) {
        humanWait_ += dt;
        float threshold = 24.f + 28.f * (float)nagLevel_;
        if (nagLevel_ < 3 && humanWait_ > threshold) {
            if (say(pickBot(), S_WaitHuman, 0.f, true, 0, 0, 4.f)) ++nagLevel_;
            else humanWait_ = threshold - 3.f; // retry a bit later
        }
    }

    // tea orders
    if (teaLow && !teaBoyBusy && teaOrderCool_ <= 0.f && globalCool_ <= 0.f) {
        int s = pickBot();
        if (say(s, S_TeaOrder, 0.f, false, 0, -1, 4.f, BanterCue::TeaOrder)) teaOrderCool_ = 45.f;
        else teaOrderCool_ = 4.f;
    }

    // idle chatter
    idleIn_ -= dt;
    if (idleIn_ <= 0.f) {
        idleIn_ = 26.f + rnd() * 26.f;
        if (globalCool_ <= 0.f && queue_.empty()) {
            if (chance(0.4f)) {
                const int nd = (int)(sizeof(kDialogues) / sizeof(kDialogues[0]));
                int di = rndInt(nd);
                for (int k = 0; k < nd && (di == lastDialogue_[0] || di == lastDialogue_[1] || di == lastDialogue_[2] ||
                                           isRecent(kDialogues[di].la));
                     ++k)
                    di = (di + 1) % nd;
                const Dialogue& d = kDialogues[di];
                if (seatCool_[d.a] <= 0.f && seatCool_[d.b] <= 0.f && !isRecent(d.la)) {
                    lastDialogue_[2] = lastDialogue_[1];
                    lastDialogue_[1] = lastDialogue_[0];
                    lastDialogue_[0] = di;
                    remember(d.la);
                    remember(d.lb);
                    Pending a, b;
                    a.line.seat = d.a;
                    a.line.text = d.la;
                    a.line.seconds = 3.2f;
                    a.line.maxWait = 4.f;
                    b.line.seat = d.b;
                    b.line.text = d.lb;
                    b.line.delay = 3.0f;
                    b.line.seconds = 3.4f;
                    b.line.maxWait = 6.f;
                    queue_.push_back(a);
                    queue_.push_back(b);
                    seatCool_[d.a] = kSeatCooldown;
                    seatCool_[d.b] = kSeatCooldown + 3.f;
                    globalCool_ = kGlobalGap + 3.f;
                }
            } else {
                say(pickBot(), S_Idle, 0.f, false, 0, -1, 4.f);
            }
        }
    }
}

void Banter::teaServed() {
    if (!enabled_) return;
    if (chance(0.6f)) say(pickBot(), S_TeaThanks, 0.8f, false, 0, -1, 3.f);
}

bool Banter::orderTea() {
    if (!enabled_ || teaOrderCool_ > 0.f) return false;
    if (!say(pickBot(), S_TeaOrder, 0.f, false, 0, -1, 4.f, BanterCue::TeaOrder)) return false;
    teaOrderCool_ = 40.f;
    return true;
}

void Banter::tvGoal() {
    if (!enabled_) return;
    if (chance(0.55f)) say(2, S_Goal, 0.3f, false, 0, -1, 2.5f);
    else if (chance(0.3f)) say(pickOther(2), S_Goal, 0.4f, false, 0, -1, 2.5f);
}

bool Banter::pop(BanterLine& out) {
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->line.delay <= 0.f) {
            out = it->line;
            queue_.erase(it);
            return true;
        }
    }
    return false;
}

void Banter::spoke(int seat) {
    if (seat >= 1 && seat <= 3) seatCool_[seat] = std::max(seatCool_[seat], 3.f);
}

} // namespace ui
