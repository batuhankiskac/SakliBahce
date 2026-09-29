// Banter lines and the logic deciding who says what, when. Turkish, friendly, no profanity.
//
// Voices (keep them consistent):
//   Hacı Rıza  (1) — the eldest; calm, pious-polite, fatherly, proverbs, tespih, tea. "Mahmut evladım", "Nuri".
//   Kel Mahmut (2) — the youngest; loud, boastful, football-obsessed ("bizim takım", never a real club), smokes,
//                    "Hacı Abi", "Nuri Abi", says "abi" a lot.
//   Emekli Nuri(3) — grumpy nostalgic retired civil servant (30 years memur), glasses, bad knees, oralet
//                    (doctor banned tea), "bizim zamanımızda…", prices, youth, phones; secretly sentimental.
//   Çaycı      (4) — young tea boy, quick, cheeky-respectful, short lines ("Geliyor abi!").
// Table geometry: seat s takes the discards of leftOf(s): Rıza takes the human's, Mahmut takes Rıza's, Nuri takes
// Mahmut's, the human takes Nuri's. So in Rıza's "took from the left" lines {g} is always the human, and Nuri is
// only ever the human's victim (kHumanTook*).
#include "ui/Banter.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ui {

namespace {

// Situations. Each has four line tables: [0] Hacı Rıza (seat 1), [1] Kel Mahmut (seat 2), [2] Emekli Nuri
// (seat 3), [3] the çaycı (seat 4, usually empty). Templates: {v} value, {p} actor, {g} the actor's left
// neighbour (giver), {h} human address.
enum Sit {
    S_Welcome,          // match start: greeting the human
    S_HandStart,
    S_HandLast,         // the last hand of the match is dealt
    S_LeadSelf,         // hand start: the speaker leads the match
    S_LeadOther,        // hand start: {p} (a bot) leads the match
    S_LeadHuman,        // hand start: the human leads
    S_BehindSelf,       // hand start: the speaker is last
    S_BehindHuman,      // hand start: the human is last
    S_TurnSelf,
    S_HumanTurn,        // the human's turn begins (rare)
    S_WaitHuman,
    S_DrawSelf,
    S_TakeLeftSelf,
    S_TakeLeftVictim,
    S_HumanTookMine,
    S_DiscardSelf,      // a bot discards; {p} = the neighbour who may take it
    S_HumanDiscard,     // comment on the human's discard (Rıza is the one who may take it)
    S_ReturnSelf,       // a bot gives a taken tile back
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
    S_SwapHuman,
    S_PenaltySelf,
    S_PenaltyOther,
    S_PenaltyHuman,
    S_PenOkeyOther,     // {p} discarded the okey
    S_PenOkeyHuman,
    S_LowPile,
    S_WinSelf,
    S_WinOkey,
    S_LoseGrumble,
    S_StreakOther,      // {p} won again
    S_HumanWon,
    S_HumanStreak,
    S_PileOut,
    S_MatchWinSelf,
    S_MatchLose,
    S_MatchHumanWon,
    S_Idle,
    S_TeaOrder,
    S_TeaThanks,
    S_Goal,
    S_CayciComing,      // the tea boy answers an order
    S_CayciServed,      // the tea boy puts the glasses down
    S_CayciIdle,        // the tea boy's own cheek
    S_CayciGoal,
    S_Cat,              // the kahvehane cat meowed
    S_Count
};

struct Tbl {
    const char* const* lines;
    int n;
};

#define LINES(name, ...) static const char* const name[] = {__VA_ARGS__};
#define T(name) Tbl{name, (int)(sizeof(name) / sizeof(name[0]))}
#define TN Tbl{nullptr, 0}

// ---------------------------------------------------------------- welcome (match start, {h})
LINES(kWelcomeR, "Hoş geldin {h}, otur şöyle.", "{h} geldi, masa tamam. Bismillah.", "Buyur {h}, çayın geliyor.",
      "Hoş geldin evlat, bugün nasibimiz bol olsun.", "Gel {h}, şu sandalye seni bekliyordu.",
      "Hoş geldin {h}; sakin oynarız, kimse kızmaz.")
LINES(kWelcomeM, "{h} geldi beyler, oyun başlıyor!", "Hoş geldin {h}, bugün seni yeniyorum!",
      "Otur {h}, maç başlamadan bir el oynarız!", "Hah, {h} geldi! Dördüncü tamam, dağıtın!",
      "Hoş geldin abi, taşlar ısındı, sen soğuk gelme!", "{h}, bugün formdayım, haberin olsun!")
LINES(kWelcomeN, "Hıh, {h} geldi. Geç kaldın.", "Otur {h}, sandalye soğuk ama masa sıcak.",
      "Hoş geldin {h}. Bizim zamanımızda erken gelinirdi.", "{h} geldi, artık dört kişiyiz. Şükür.",
      "Gel {h}, gözlüğümü taktım, seni de görüyorum.", "Hoş geldin {h}, oraletim bitmeden başlayalım.")
// ---------------------------------------------------------------- hand start
LINES(kHandStartR, "Hayırlısı olsun beyler.", "Bismillah, dağıtın bakalım.", "Bu el kısmet kimeyse.",
      "Sabır ve dikkat, gerisi gelir.", "Allah bereket versin, başlayalım.", "Herkese hayırlı taşlar dilerim.",
      "Yeni el, yeni nasip.", "Ne verdiyse ona razıyız, dizin taşları.", "Çaylar geldi, eller yıkandı, başlayalım.",
      "Bu el sakin oynayalım, kimse kızmasın.", "Göstergeye bakın da öyle dizin evlatlar.",
      "Tespihi bir kenara koyduk, taşlar gelsin.")
LINES(kHandStartM, "Bu el benim abi!", "Hadi hadi, dağıt şu taşları!", "Bu sefer kimse beni tutamaz!",
      "Isındım beyler, dikkat edin!", "Taşlar bu sefer bana güzel gelecek, hissediyorum!",
      "Maç başlamadan bu eli bitiririm!", "Kollar sıvandı, taşlar dizildi!", "Bu el gol sırası bende abi!",
      "Hacı Abi dua et, taşlar güzel gelsin!", "Bakın şimdi, ilk elden açıyorum!", "Bu el kupa finali beyler!",
      "Nuri Abi, gözlüğünü tak, bu el hızlı oynanacak!")
LINES(kHandStartN, "Yeni el, eski dert.", "Bakalım bu sefer ne felaket gelecek.",
      "İyi karıştırdınız mı? Bizim zamanımızda iyice karıştırılırdı.",
      "Hadi başlayalım da akşam ezanına yetişeyim.", "Gözlüğümü taktım, artık hiçbir şey kaçmaz.",
      "Oraletimi aldım, dizimi uzattım, başlayabiliriz.", "Bu taşlar bana hep aynı geliyor, fark eden yok mu?",
      "Bizim zamanımızda dağıtan taşa bir de üflerdi.", "Hıh, yine on dört taş. Bir fazlasını vermiyorlar.",
      "Eskiden bu masada Rıfat Efendi otururdu, rahmetli.", "Sesinizi biraz kısın da taşlarımı duyayım.",
      "Torunum sordu: dede yine mi okey? Yine, dedim.")
LINES(kHandLastR, "Son el beyler, hayırlısı.", "Son el; kazanan da kaybeden de dost kalsın.",
      "Son el, herkes dikkatli oynasın.", "Son el; sabreden kazanır.", "Son el evlatlar, güzel bitirelim.")
LINES(kHandLastM, "Son el abi, uzatmalar!", "Son el beyler, son düdük çalmadı henüz!",
      "Bu son elde her şey değişir, izleyin!", "Son el! Tabelayı ben belirlerim!", "Son el, doksan artı üç!")
LINES(kHandLastN, "Son el. Şükür, dizlerim daha fazla dayanmaz.", "Son el, sonra eve. Torun bekliyor.",
      "Son el mi? Bu sefer bitirmeden kalkmam.", "Son el; bizim zamanımızda son el iki misli yazılırdı.",
      "Son el. Oralet de son, sabır da son.")
// ---------------------------------------------------------------- hand start, score aware
LINES(kLeadSelfR, "Öndeyim ama nazar değmesin, tahtaya vurun.", "Önde olan sabırlı olur; sakin oynuyoruz.",
      "Kısmet bugün bizden yana, şükür.", "Tabelada önde olmak yetmez, oyun bitmedi.",
      "Öndeyim diye şımarmam evlatlar, kısmet bu.")
LINES(kLeadSelfM, "Tabelaya bakın beyler, lider kim?", "Puan durumunda zirve bizim abi!",
      "Şampiyonluk yolunda bir el daha!", "Önde olan konuşur, gerisi dinler!", "Kupayı şimdiden hazırlayın!",
      "Lider Mahmut, takipçiler siz!")
LINES(kLeadSelfN, "Öndeyim ama sevinmem, hep son elde bozulur.", "Tecrübe konuşuyor beyler, tecrübe.",
      "Otuz yıl memurluk boşuna değil, hesap bilirim.", "Öndeyim de kimse tebrik etmiyor, hıh.",
      "Önde olmak güzel de dizlerim aynı ağrıyor.")
LINES(kLeadOtherR, "{p} önde gidiyor, maşallah.", "Nazar değmesin {p}, güzel gidiyorsun.",
      "{p} önde; biraz durduralım beyler.", "{p} önde ama oyun bitene kadar bitmez.",
      "Maşallah {p}, tabelanın tepesindesin.")
LINES(kLeadOtherM, "{p} önde ha! Bu el bitiyor o saltanat!", "Hakem yardımıyla önde {p}!",
      "{p} önde diye şımarma, uzatmalar var!", "Dur bakalım {p}, lig uzun!", "{p} lider ama biz geliyoruz!")
LINES(kLeadOtherN, "{p} önde. Şans işte, başka bir şey değil.", "Bizim zamanımızda önde olan çayları öderdi {p}.",
      "{p} önde gidiyor da hava atmasın.", "Hıh, {p} lider olmuş. Görelim sonunu.",
      "{p} önde. Ben otuz yıl önde gittim, kimse alkışlamadı.")
LINES(kLeadHumanR, "{h} önde gidiyor, maşallah.", "Nazar değmesin {h}, güzel oynuyorsun.",
      "{h} önde; ama oyun bitene kadar bitmez.", "Maşallah {h}, tabelanın tepesindesin.",
      "{h} önde; evlatlar, biraz toparlanın.")
LINES(kLeadHumanM, "{h} önde ha! Bu el o saltanat bitiyor!", "Dur bakalım {h}, lig daha uzun!",
      "{h} bizi tabelada geçti beyler, utanın!", "{h} önde diye şımarma, uzatmalar var!",
      "{h} lider ama biz geliyoruz!")
LINES(kLeadHumanN, "{h} önde. Acemi şansı işte.", "Bizim zamanımızda önde olan çayları öderdi {h}.",
      "Hıh, {h} lider olmuş. Görelim sonunu.", "{h} önde gidiyor da bize ders veriyor sanki.",
      "{h} önde. Gençlik, şans, gözlük; hepsi onda.")
LINES(kBehindSelfR, "Sondayım ama tevekkül ettim, gerisi kısmet.", "Sonuncu olmak ayıp değil, umutsuz olmak ayıp.",
      "Bu elde toparlarız inşallah.", "Sonda olan sabreder, sabreden kazanır.", "Geride kaldık; eh, yol uzun.")
LINES(kBehindSelfM, "Sondayım ama küme düşme yok abi, toparlarız!", "Devre arası abi, ikinci yarı bizim!",
      "Bu el geri dönüş başlıyor, yazın bir kenara!", "Sonuncu olmak geçici, Mahmut kalıcı!",
      "Geride kaldım da hoca beni oyundan almaz!")
LINES(kBehindSelfN, "Sondayım. Bu taşlarla kim olsa sonda olur.", "Emekli maaşı gibi puanım, hep aşağıda.",
      "Bizim zamanımızda böyle geride kalmazdım.", "Sonuncu ben. Oralet de soğuk. Güzel gün.",
      "Sondayım. Gözlüğü değiştirmek lazım, belli.")
LINES(kBehindHumanR, "Toparla {h}, daha oyun bitmedi.", "Sonda olmak ayıp değil {h}, sabret.",
      "Bu el sana {h}, hayırlısıyla.", "Üzülme {h}, kervan yolda düzülür.", "{h}, sondasın ama yol uzun.")
LINES(kBehindHumanM, "{h} sonda kaldı, hadi be abi toparla!", "Küme düşme hattındasın {h}, dikkat!",
      "{h}, ikinci yarı başlıyor, hadi!", "{h} sonda; hoca seni kulübeye çeker!", "Toparla {h}, tribün bekliyor!")
LINES(kBehindHumanN, "Sonda kaldın {h}. Bizim zamanımızda çıraklar da böyle başlardı.",
      "Üzülme {h}, ben de otuz yıl memurlukta sondaydım.", "Hıh, {h} sonda. Gençlik işte.",
      "{h} sonda. Oralet ısmarlarsan yardım ederim.", "Sondasın {h}; bak, ben hiç sıkılmadım, sen de sıkılma.")
// ---------------------------------------------------------------- own turn
LINES(kTurnR, "Bismillah…", "Bakalım kısmette ne var.", "Acele işe şeytan karışır…", "Hmm, bir düşüneyim.",
      "Yavaş yavaş…", "Bir taş çek, bir taş at; dünya bu.", "Sıra bende mi? Hayırlısı.",
      "Şu tespihi bir bırakayım da…", "Hele bir bakalım.")
LINES(kTurnM, "Gel gel gel…", "Hadi güzel bir taş!", "Bakın şimdi ne yapacağım!", "Sıra bende, izleyin!",
      "Hah, işte şimdi oldu!", "Top bende abi, izleyin!", "Şimdi hücum zamanı!", "Hadi Mahmut, hadi aslanım!",
      "Bu taş girer, hissediyorum!")
LINES(kTurnN, "Hıh, yine ne gelecek kim bilir.", "Gözlüğüm nerede… ha, burada.", "Ah şu dizlerim…",
      "Sıra bende mi? Tamam tamam.", "Acele ettirmeyin beni.", "Dur bakayım, şu oraletten bir yudum.",
      "Hıh, sıra bana da geliyormuş.", "Sakin, sakin. Memur sakinliği.", "Bu taş da elime yapıştı, ne oldu böyle.")
LINES(kHumanTurnR, "Sıra sende {h}, hayırlısı.", "Buyur {h}, sıra sende.", "{h}, taşlar seni bekliyor.",
      "Sıra sende evlat, acele yok.")
LINES(kHumanTurnM, "Sıra sende {h}, hadi!", "{h}, top sende!", "Hadi {h}, göster kendini!", "Sıra sende {h}, şut!")
LINES(kHumanTurnN, "Sıra sende {h}. Uyuma.", "{h}, sıra sende; ben oraletimi içerken bekleyeyim.",
      "Hıh, sıra sende {h}. Düşün ama fazla değil.", "Sıra sende {h}. Bizim zamanımızda hızlı oynanırdı.")
// ---------------------------------------------------------------- waiting for the human
LINES(kWaitR, "Acele etme {h}, ama sabahı da etme.", "Düşün taşın, sonra at taşı {h}.", "{h}, çayın soğuyor.",
      "Sabreden derviş muradına ermiş… ama bu kadar da değil.", "Hayırdır {h}, taşlarla sohbete mi daldın?",
      "Kervan yolda düzülür {h}, at bir taş.", "{h}, çay soğudu, taş bekliyor.",
      "Sabır iyidir {h}, ama akşam oluyor.", "Evlat, bir taş at da tespihi bitireyim.",
      "{h}, taşlarla istişare bitmedi mi?", "Hayırlısı {h}, ama hayırlısı bugün gelsin.")
LINES(kWaitM, "Hadi be {h}, uyuma!", "{h}, taş atacaksın, roket fırlatmıyorsun!", "Maç başlayacak, hadi {h}!",
      "Uyudun mu {h}? Hadi!", "Hadi ama, sabah oldu!", "{h}, VAR'a mı baktırdın? Hadi!", "Devre arası bitti {h}, hadi!",
      "{h} abi, taşlar ısındı elinde!", "Uzatmalara gidiyoruz {h}, hadi!", "Hadi be {h}, maç bitecek, biz bitmeyecek!",
      "{h}, hakem süre eklemiyor, hadi!")
LINES(kWaitN, "Bizim zamanımızda bu kadar düşünen olmazdı…", "Ben bu sırada iki oralet içtim {h}.",
      "Hadi {h}, torunumu okuldan alacağım.", "Gençler hep böyle, taşa telefon gibi bakıyor.",
      "{h}, emekli maaşım yatacak, hadi!", "{h}, bu hızla emekli maaşı da yetişmez.",
      "Bizim zamanımızda bu kadar düşünen kahveden atılırdı.", "{h}, oraletim soğudu, ikincisini de bitirdim.",
      "Dizlerim tutuldu {h}, bir taş at da kıpırdayalım.", "Hıh, {h} taşlarla konuşuyor. Cevap veriyorlar mı?",
      "{h}, ben otuz yılda bu kadar dosya beklemedim.")
// ---------------------------------------------------------------- drawing from the pile (rare)
LINES(kDrawR, "Hayırlısı…", "Bu da bir kısmet.", "Ne çıkarsa bahtıma.", "Eh, bu da gelir.", "Allah ne verdiyse.")
LINES(kDrawM, "Yine işe yaramaz taş!", "Hah, güzel geldi!", "Ne bu be!", "Off, yine mi bu?", "Gel bakalım, gel!",
      "Tam istediğim! Yok, değil.")
LINES(kDrawN, "Hıh, yine boş taş.", "Bu taşı kim çekti ya… ha, ben.", "Hıh, bunu da mı ben çektim?",
      "Bizim zamanımızda çekilen taş işe yarardı.", "Gözlüğü sileyim de neymiş bu.")
// ---------------------------------------------------------------- taking from the left ({g} = giver)
LINES(kTakeSelfR, "Sağ ol {g}, tam lazımdı.", "Allah razı olsun {g}, işime yaradı.", "Bu taş bana nasipmiş.",
      "Eyvallah {g}, komşu hakkı.", "Sağ olasın {g}, bereketli taş.", "Bunu atacağını biliyordum {g}, sağ ol.",
      "Hayır işledin {g}, bu taş bize lazımdı.", "Eline sağlık {g}, tam yerine oturdu.")
LINES(kTakeSelfM, "Sağ ol {g}!", "Oh be, tam aradığım taş!", "Bunu bana mı attın {g}? Eyvallah!",
      "Ellerine sağlık {g}!", "{g}, iyi ki varsın!", "Yandan aldım, gol pası gibi geldi!",
      "{g} attı, Mahmut aldı, ver gol!", "Bu taş için sana bir çay borçlandım {g}!", "İşte bu! {g}, seni seviyorum!")
LINES(kTakeSelfN, "Hah, bu taş benim işime yarar.", "Sağ ol {g}, eline sağlık.",
      "Bizim zamanımızda böyle cömert taş atılmazdı!", "Al bakalım; {g} attı, biz aldık.",
      "{g}, sen taş atmayı bilmiyorsun ama sağ ol.", "Hıh, gençlik işte, elindeki kıymeti bilmiyor.",
      "Bu taş bana lazımdı {g}, borcum olsun.", "{g}, bu taşı atarken bir bana baksaydın.")
// victim of a bot taking from the left ({p} = taker; Rıza's taker is Mahmut, Mahmut's is Nuri)
LINES(kTakeVictimR, "Hayırlı olsun, benden sana.", "Eh, kısmetin varmış {p}.", "Al {p}, hayrını gör.",
      "Komşuya taş vermek sevaptır {p}.", "Sen al {p}, benim işime yaramazdı zaten.",
      "Verdik gitti {p}. Allah bereket versin.", "Al {p}, sen daha gençsin, lazım olur.")
LINES(kTakeVictimM, "Taşımı kaptı ya!", "Keşke atmasaydım o taşı…", "Dur be {p}, o taş bana lazımdı!",
      "{p}, sen o taşı beklemiyordun, biliyorum!", "Ver geri {p}, şaka yaptım!", "Hakem! Taşımı aldı!",
      "{p} yine benden besleniyor!", "Ofsayttan aldı o taşı {p}!")
LINES(kTakeVictimN, "Aman al, senin olsun…", "Gözün taşımda mıydı {p}?")
// the human took Nuri's tile: Nuri is the victim, the others observe
LINES(kHumanTookR, "Hayırlı olsun {h}, işine yarasın.", "Yandan aldın {h}, kısmet.", "{h} gözünü açmış, maşallah.",
      "Al {h}, komşu hakkı.", "Nuri'nin taşı sana nasipmiş {h}.")
LINES(kHumanTookM, "Vay, yandan aldın ha {h}! Dikkat beyler!", "{h} Nuri Abi'den taş kaptı, ha ha!",
      "Uyanık çıktın {h}, bak sen!", "{h}, Nuri Abi'nin taşını almak kolay, beni gör!",
      "Kaptı {h}! Nuri Abi, savunma nerede?")
LINES(kHumanTookN, "Al o da senin olsun {h}…", "Gözün taşımdaymış demek {h}.", "Bak sen, taşımı kaptı!",
      "Hıh, o taşı atmayaydım keşke.", "{h}, benden aldığın taşla açarsan ayıp olur.",
      "Ben attım, sen aldın; adalet bu mu {h}?", "Torunum da böyle kapıyor kurabiyeleri.",
      "Al {h}, bizim zamanımızda teşekkür edilirdi.")
// ---------------------------------------------------------------- discards (rare)
// a bot discards; {p} = who may take it (Rıza -> Mahmut, Mahmut -> Nuri, Nuri -> the human)
LINES(kDiscardSelfR, "Al {p}, sana hediye.", "Bu taş benim değil {p}, belki senindir.", "Buyur {p}, hayrını gör.",
      "Attık gitti; {p} bakar artık.", "Bir taş eksildi, gönül rahatladı.")
LINES(kDiscardSelfM, "Al {p}, bu sana hediye!", "Buyur {p}, gözlüğünü tak da bak!",
      "Bunu almazsan hiçbir şey almazsın {p}!", "Gitti bir taş, {p} seyretsin!", "Bu taşı atıyorum, kimse üzülmesin!")
LINES(kDiscardSelfN, "Al {h}, bak bakalım işine yarar mı.", "Bunu atıyorum, {h} almasın inşallah.",
      "Buyur {h}, bizim zamanımızda böyle atardık.", "Hıh, bunu atıyorum ama gözün üstünde olmasın {h}.",
      "Attım. Bu taşla otuz yıl memurluk yapılmaz.")
// the human discards; Rıza is the one who may take it
LINES(kHumanDiscardR, "Onu mu attın {h}? Bir bakalım…", "Hmm, {h} ne attı bakayım.", "Eh {h}, güzel taş ama benim değil.",
      "Bu taşı sen mi attın {h}? Not aldım.", "Sağ ol {h}, düşüneyim şunu.", "Attığın taş konuşuyor {h}, dikkat.")
LINES(kHumanDiscardM, "Onu mu atıyorsun {h}? Hacı Abi'ye hediye!", "{h}, Hacı Abi zaten her şeyi alıyor, dikkat!",
      "Vay {h}, cömert adamsın!", "Böyle taş atılır mı {h}?", "{h} attı, Hacı Abi gözünü dikti!")
LINES(kHumanDiscardN, "Hıh, {h} da benim gibi atıyor.", "O taşı ben olsam atmazdım {h}.",
      "Bizim zamanımızda o taş atılmazdı {h}.", "Onu mu attın {h}? Eh, senin bileceğin iş.",
      "{h} taş attı, Hacı da düşünüyor. Akşam olacak.")
LINES(kReturnSelfR, "Tüh, yanlış aldım, geri veriyorum.", "Bu taş benim değil, geri.", "Yaşlılık evlat, yanlış gördüm.")
LINES(kReturnSelfM, "Pardon abi, yanlış aldım!", "Geri geri, bu değildi!", "Hakem, geri alıyorum!")
LINES(kReturnSelfN, "Gözlük yüzünden, geri veriyorum.", "Hıh, bu değil. Al geri.",
      "Bizim zamanımızda taş geri verilmezdi, ayıp.")
// ---------------------------------------------------------------- opening
LINES(kOpenSelfR, "Bismillah, {v} ile açtım.", "Sabreden derviş muradına ermiş: {v}!", "Eh, {v}… hayırlısı.",
      "Yavaş yavaş, {v} ile açıldık.", "Elhamdülillah, {v} ile açıldık.", "Bu taşlar {v} etti, hayırlısı.",
      "{v}. Açtım evlatlar, dikkat.", "Yavaş ama sağlam: {v}.", "Sabrın sonu selamet: {v} ile açtım.")
LINES(kOpenSelfM, "Al sana {v}!", "{v}! Bu el benim abi!", "İşte böyle açılır el! {v}!",
      "Açtım gitti! {v}, hadi bakalım!", "Görün beyler: {v}!", "{v} beyler! Gol!", "Bakın bakın, {v}! Tribünler ayakta!",
      "{v} ile açtım, şampiyonluk yakın!", "Kapı açıldı beyler: {v}!", "Vay be Mahmut, {v}! Helal olsun sana!")
LINES(kOpenSelfN, "{v} ile açtım. Bizim zamanımızda 101'i ilk turda geçerdik.", "Açtım, rahat bırakın beni artık.",
      "Hıh, {v} ile açtım. Fena değil.", "{v} ile açtım. Torunuma anlatırım bunu.",
      "Açtım. {v}. Bizim zamanımızda alkışlanırdı.", "Hıh, {v}. Otuz yıl memurluk boşuna değil.",
      "{v} ile açıldık. Şimdi oraletimi içebilirim.", "Açtım {v} ile. Gözlük işe yarıyormuş.")
LINES(kOpenPairsR, "Çiftten gidiyorum, hayırlısı.", "{v} çift, kısmet böyleymiş.", "{v} çift ile açıldık, elhamdülillah.",
      "Çiftten açtım; sabır işi bu.")
LINES(kOpenPairsM, "Çiftten gidiyorum beyler!", "{v} çift! Korkun benden!", "{v} çift beyler, ikili oynuyoruz!",
      "Çift açtım, çifte kavrulmuş!")
LINES(kOpenPairsN, "Çift açtım, gençler öğrensin.", "{v} çift. Eskiden de çiftten giderdim.",
      "{v} çift. Hıh, yaşlı kurt böyle açar.", "Çiftten açıldık, gençler not alsın.")
LINES(kOpenOtherR, "Hayırlı olsun {p}.", "Maşallah, açtı bile.", "Güzel açtın {p}, nazar değmesin.",
      "Eh, {p} açtı; biz de sabredelim.", "Hayırlı uğurlu olsun {p}.", "Açtı, dikkat edin beyler.")
LINES(kOpenOtherM, "Vay be, açtı bak!", "Dikkat beyler, {p} açtı!", "{p} açtı, savunmaya dönün!", "Erken açtın {p}, şans!",
      "Oha, {p} açtı, ben daha ısınmadım!", "Hakem, ofsayt yok mu buna?")
LINES(kOpenOtherN, "Açtı mı? Eh, 101'i zor geçti.", "Hıh, şanslı adam.", "{p} açtı. Bizim zamanımızda daha yüksekle açılırdı.",
      "Açtın da ne oldu {p}, bitirene bak.", "Hıh, {p} açtı; ben daha oraletimi bitirmedim.",
      "Açtı bak. Gözlüğümü taksam ben de açardım.")
LINES(kOpenBigR, "{v} mü? Maşallah, nazar değmesin!", "{v}! Allah bereket versin {p}.", "{v} ile açmak… Maşallah {p}.")
LINES(kOpenBigM, "{v} mü? Hile var bunda!", "Oha, {v}! Bu ne be!", "{v}! Hakem, VAR'a bakın!",
      "{v} ha! Taşlar hep sana mı geliyor {p}?")
LINES(kOpenBigN, "{v}! Bizim zamanımızda bu kadar taş bir arada gelmezdi.", "{v}? Gözlüğümü sileyim de bir daha bakayım.",
      "{v}. Hıh, elinde deste mi tutuyorsun {p}?")
LINES(kOpenOtherPairsR, "Çiftten mi? Cesaret ister.", "Çift açtı {p}, hayırlı olsun.", "Çiftten gidiyor; sabırlı adam.")
LINES(kOpenOtherPairsM, "Çift açtı, eyvah! Dikkat!", "{p} çiftten gidiyor, ikili oynuyor!", "Çift ha! Kimse kıpırdamasın!")
LINES(kOpenOtherPairsN, "Çiftten gidiyor, cesur adam.", "Çift açtı. Bizim zamanımızda çift açan çay ısmarlardı.",
      "Hıh, çift. Bitirirse alkışlarım.")
LINES(kOpenHumanR, "Maşallah {h}, güzel açtın.", "Hayırlı olsun {h}, {v} ile açtın.", "Eline sağlık {h}, güzel el.",
      "Açtın {h}, nazar değmesin.", "Bak sen {h}, sessiz sessiz açtı.")
LINES(kOpenHumanM, "Vay, açtın ha {h}! Bak sen!", "{v} mü? Fena değilsin {h}!", "{h} açtı beyler, savunma!",
      "Erken açtın {h}, şans!", "Oha {h}, ben daha ısınmadım!")
LINES(kOpenHumanN, "Hıh, {h} açtı. Acemi şansı.", "{v} ile açtın {h}. Eh, olur.", "Açtın da bitirene bak {h}.",
      "Bizim zamanımızda daha yüksekle açılırdı {h}.", "Hıh, {h} açtı; oraletim bile bitmedi.")
// ---------------------------------------------------------------- işleme / joker
LINES(kAddSelfR, "Bir taş da buraya.", "Damlaya damlaya göl olur.", "Şunu da yerine koyalım.", "Taş taş üstüne.",
      "Bu da işlendi, elhamdülillah.")
LINES(kAddSelfM, "İşledim gitti!", "Bir tane daha, al buraya!", "İşle işle, bitir!", "Bunu da yapıştırdım!",
      "Pas, şut, işledim!")
LINES(kAddSelfN, "Şunu da buraya koyalım.", "Hıh, bu da buraya.", "İşledik. Bizim zamanımızda buna işleme denirdi zaten.",
      "Bir taş eksildi, dizim rahatladı.", "Buraya da bir taş, kimse görmedi.")
LINES(kAddOtherR, "Güzel işledin {p}.", "İşliyor, maşallah.", "Bir taş daha eksildi {p}, hayırlısı.")
LINES(kAddOtherM, "Yine mi işledin {p}!", "{p} işliyor, dikkat beyler!", "Bırak biraz da bize {p}!")
LINES(kAddOtherN, "Ne kadar da işliyor, maşallah.", "Hıh, {p} yine işledi.", "İşlemesi güzel de bitirsin bakalım.")
LINES(kSwapSelfR, "Okey yerine geldi, elhamdülillah.", "Emanet okeyi alıyorum.", "Okeyi aldım, hayırlısı.",
      "Bu okey bize nasipmiş.")
LINES(kSwapSelfM, "Okeyi kaptım!", "Okey benim abi!", "Gel bakalım okey, gel!", "Okey! İşte bu!",
      "Okeyi aldım, şimdi oyun başlıyor!")
LINES(kSwapSelfN, "Okeyi aldım, kimse görmedi sandınız.", "Hah, okey bende.", "Okey. Otuz yıl memurluk, fırsatı görürüm.",
      "Hıh, okeyi aldım. Gözlük işe yarıyor.")
LINES(kSwapOtherR, "Okeyi aldı, dikkat edin.", "{p} okeyi aldı, hayırlısı.", "Okey gitti; sabır.")
LINES(kSwapOtherM, "Eyvah, okey gitti!", "Nasıl kaptı o okeyi!", "{p} okeyi kaptı, hakem!", "Okey gitti abi, savunma dağıldı!")
LINES(kSwapOtherN, "Okeyi kaptırdık, bravo bize.", "Hıh, {p} okeyi aldı.", "Bizim zamanımızda okeyi böyle kolay vermezlerdi.")
LINES(kSwapHumanR, "Okeyi aldın ha {h}, hayırlısı.", "{h} okeyi aldı, dikkat edin.")
LINES(kSwapHumanM, "{h} okeyi kaptı, eyvah!", "Nasıl kaptın o okeyi {h}!")
LINES(kSwapHumanN, "Hıh, {h} okeyi aldı. Şans.", "Okeyi kaptırdık {h}, bravo sana.")
// ---------------------------------------------------------------- penalties
LINES(kPenSelfR, "Hay Allah, dikkat etmedim.", "Olur böyle şeyler, sabır.", "Kul hata yapar, yazın.",
      "Yaşlılık evlat, göz görmüyor.", "Tüh, hesaba yazın, itiraz yok.")
LINES(kPenSelfM, "Of be, görmedim!", "Tüh! Olsun, bir dahakine.", "Hakem hatası abi!", "Ceza mı? VAR'a baksınlar!",
      "Off, kendi kaleme gol!")
LINES(kPenSelfN, "Gözlüğüm buğulandı, ondan!", "Göremedim valla, ışık az burada.", "Hıh, ceza. Emekli maaşından kesin.",
      "Bizim zamanımızda buna ceza yazılmazdı.", "Oralet gözüme kaçtı, ondan.")
LINES(kPenOtherR, "Dikkat {p}, hesaba yazıldı.", "Acele işe şeytan karışır {p}.", "Olur {p}, kul hata yapar.",
      "Yazın beyler, {p} ceza yedi.", "Sabır {p}, oyun daha bitmedi.")
LINES(kPenOtherM, "Ceza yazın ceza!", "Hesaba bir 101 daha!", "Ha ha! {p}, gözünü seveyim!", "Kırmızı kart {p}!",
      "{p} kendi kalesine attı!", "Yüz bir daha, yazın deftere!")
LINES(kPenOtherN, "Ceza! Kalemi verin, ben yazarım.", "Gözlüğü tak da öyle at {p}!", "Hıh, {p} ceza yedi. Gençlik.",
      "Bizim zamanımızda böyle hata yapan çay ısmarlardı.", "Yazdım {p}, defterime yazdım.")
LINES(kPenHumanR, "Olur öyle {h}, dikkat et.", "Hesaba yazıldı {h}, üzülme.", "Kul hata yapar {h}, oyun devam.",
      "Dikkat {h}, işlek taş atılmaz.")
LINES(kPenHumanM, "Ceza yazın ceza! 101'i yedin {h}!", "Ha ha, hoş geldin 101!", "Kırmızı kart {h}!",
      "{h} kendi kalesine attı beyler!")
LINES(kPenHumanN, "Bizim zamanımızda böyle hatayı çıraklar yapardı.", "Hıh, {h} ceza yedi. Deftere yazdım.",
      "Gözlüğümü ödünç vereyim mi {h}?", "Ceza. Bizim zamanımızda çay ısmarlanırdı {h}.")
LINES(kPenOkeyOtherR, "Okeyi attı! Hay Allah {p}.", "Okeyi attın {p}, olur böyle şeyler.")
LINES(kPenOkeyOtherM, "Okeyi attı! Oha {p}!", "{p} okeyi attı beyler, görün!", "Okeyi atmak ne demek {p}!")
LINES(kPenOkeyOtherN, "Okeyi attı. Bizim zamanımızda böylesi görülmemişti.", "Hıh, {p} okeyi attı. Gözlük lazım sana.")
LINES(kPenOkeyHumanR, "Okeyi mi attın {h}? Hay Allah.", "Olur {h}, okey de gider bazen.")
LINES(kPenOkeyHumanM, "{h} okeyi attı beyler! Oha!", "Okeyi attın {h}! Kendi kaleye gol!")
LINES(kPenOkeyHumanN, "Okeyi attın {h}. Gözlüğümü vereyim mi?", "Hıh, {h} okeyi attı. Gençlik.")
// ---------------------------------------------------------------- low pile
LINES(kLowR, "Taşlar azaldı, dikkatli olun.", "Son taşlar, hayırlısı.", "Bitmeye yakın, sabır.",
      "Deste inceldi, akıllı oynayın.", "Kısmet son taşlardaymış demek.", "Sona geldik, kimin nasibi varsa.",
      "Deste bitiyor, sabır tükenmesin.")
LINES(kLowM, "Taşlar bitiyor beyler…", "Hadi bitirin artık, taş bitiyor!", "Ortada birkaç taş kaldı abi!",
      "Az kaldı, son düzlük!", "Son taşlar beyler, dikkat!", "Uzatmalar başladı beyler!", "Doksan dakika bitiyor, hadi!")
LINES(kLowN, "Taşlar bitiyor, kimse bitiremeyecek bu gidişle.", "Son taşlar… bizim zamanımızda çoktan biterdi.",
      "Eh, ortada birkaç taş kaldı.", "Hıh, ortada üç beş taş kaldı.", "Bu el de böyle biter, görürsünüz.",
      "Taşlar bitiyor, oralet de bitiyor.", "Hıh, deste de emekli oluyor.")
// ---------------------------------------------------------------- end of hand
LINES(kWinR, "Elhamdülillah, bitti.", "Sabreden derviş muradına ermiş!", "Hayırlı olsun, bu el bizden.",
      "Bitti, elhamdülillah. Hayırlı olsun herkese.", "Nasip bugün bizeymiş.", "Bitirdik evlatlar, çaylar benden.",
      "Sabır bitirdi bu eli.")
LINES(kWinM, "Bitti! Eller havaya!", "İşte bu! Bu el benim abi!", "Gooool! Ay pardon, bitti!", "Kim tutar beni!",
      "Bitti abi! Maç bitti, Mahmut kazandı!", "Şampiyonlar böyle bitirir beyler!", "Doksanda golü attım! Bitti!",
      "Tribünler ayağa! Bitti!")
LINES(kWinN, "Bitirdim. Bizim zamanımızda da böyle bitirirdik.", "Hıh, gördünüz mü? Yaşlı kurt!",
      "Bitti. Şimdi çaylar sizden.", "Bitti. Torunuma anlatacağım bunu.", "Hıh, bitirdim. Gözlük işe yaradı.",
      "Otuz yıl memurluk, bitirmeyi bilirim.", "Bitti. Şimdi oraletimi rahat içerim.")
LINES(kWinOkeyR, "Okeyle bitti, hayırlısı.", "Okeyle bitirdim, çift yazın evlatlar.", "Elhamdülillah, okeyle bitti.")
LINES(kWinOkeyM, "Okeyle bitirdim abi, çift yazın!", "Okeyle bitirdim! Röveşata gol!", "Çift yazın beyler, okeyle bitti!")
LINES(kWinOkeyN, "Okeyle bitirdim, ders olsun.", "Okeyle bitti. Bizim zamanımızda üç misli yazılırdı.",
      "Hıh, okeyle bitirdim. Çift yazın, gözlüğüm görüyor.")
LINES(kLoseR, "Hayırlısı, bir sonraki ele.", "Kısmet değilmiş.", "Hayırlı olsun {p}, güzel bitirdin.",
      "Eh, bu el onun; hayırlısı.", "Kazanana helal olsun.", "Bir el gider, bir el gelir.")
LINES(kLoseM, "Bu elde şans yoktu abi.", "Taşlar hep ona geliyor!", "Bir dahaki el benim, yazın bir kenara!",
      "Hakem bitirdi bu eli, ben değil!", "Bir taş kalmıştı be, bir taş!", "Rövanş var {p}!", "Of, bir el daha gitti!")
LINES(kLoseN, "Bu taşlarla kim bitirir ki?", "Bizim zamanımızda böyle şans olmazdı.", "Hıh, yine kaybettik.",
      "Taşları ben karıştırsam böyle olmazdı.", "Bitirdi. Hıh, tebrikler {p}, isteksizce.",
      "Bir taş eksikti. Hep bir taş eksik.", "Eh, oralet de bitti, el de bitti.")
LINES(kStreakOtherR, "Yine {p}! Maşallah, nazar değmesin.", "İki el üst üste {p}, kısmet bol.",
      "{p} yine bitirdi; tahtaya vurun beyler.")
LINES(kStreakOtherM, "Yine mi {p}! Hile var bunda!", "{p} seri yaptı, hakem!", "Yine {p}! Taşları değiştirin abi!")
LINES(kStreakOtherN, "Yine {p} bitirdi. Taşları değiştirin.", "Hıh, {p} yine. Bizim zamanımızda sırayla bitirilirdi.",
      "{p} yine mi? Gözlüğümü sileyim.")
LINES(kHumanWonR, "Maşallah {h}, helal olsun!", "Tebrikler {h}, güzel bitirdin.", "Hayırlı olsun {h}, hakkıyla bitirdin.",
      "Elhamdülillah, {h} bitirdi; çaylar benden.", "Bitirdin {h}, nazar değmesin.")
LINES(kHumanWonM, "Vay be {h}, helal olsun!", "Şansına şaşayım {h}!", "Bitirdin ha {h}! Rövanş!",
      "{h} doksanda golü attı beyler!", "Helal olsun {h}, ama bir dahakine ben!")
LINES(kHumanWonN, "Acemi şansı… ama tebrikler.", "Hıh, bitirdi bak. Aferin {h}.", "Bitirdin {h}. Torunuma anlatmam bunu.",
      "Hıh, {h} bitirdi. Gözlüğümü sileyim.", "Tebrikler {h}. Bizim zamanımızda kazanan çay ısmarlardı.")
LINES(kHumanStreakR, "Yine {h}! Maşallah, nazar değmesin.", "İki el üst üste {h}, güzel gidiyorsun.",
      "{h} yine bitirdi; tahtaya vurun evlatlar.")
LINES(kHumanStreakM, "Yine mi {h}! Hile var bunda!", "{h} seri yaptı beyler, durdurun şunu!", "Yine {h}! Taşları değiştirin abi!")
LINES(kHumanStreakN, "Yine {h} bitirdi. Taşları değiştirin.", "Hıh, {h} yine. Acemi şansı uzadı.",
      "{h} yine mi? Bizim zamanımızda sırayla bitirilirdi.")
LINES(kPileOutR, "Taşlar bitti, kimse bitiremedi. Kısmet.", "Deste bitti, el berabere. Hayırlısı.",
      "Kimse bitiremedi; sabır, sonraki el.", "Taş bitti evlatlar, herkes elini yazsın.")
LINES(kPileOutM, "Taş bitti abi, kimse bitiremedi!", "Golsüz berabere, of be!", "Taş bitti, uzatma yok mu bunun?",
      "Kimse bitiremedi, hakem düdüğü çaldı!")
LINES(kPileOutN, "Dedim size, bu el biter mi?", "Taş bitti. Bizim zamanımızda böyle el olmazdı.",
      "Hıh, kimse bitiremedi. Elleri sayın bakalım.", "Deste bitti, oralet bitti, sabır bitti.")
// ---------------------------------------------------------------- end of match
LINES(kMatchWinR, "Oyun bizden, çaylar benden!", "Elhamdülillah, güzel oyundu.", "Kazanan da kaybeden de dost kalsın.",
      "Hayırlısıyla bitti, çaylar benden.", "Kazandık ama oyun oyundur evlatlar.")
LINES(kMatchWinM, "Maç benim beyler! Çaycı, herkese çay!", "Şampiyon kim? Mahmut!", "Kupayı getirin!",
      "Şampiyon Mahmut! Çaycı, herkese çay benden!", "Kupa bizim beyler, tur atıyorum!")
LINES(kMatchWinN, "Maçı aldım. Tecrübe delikanlı, tecrübe.", "Bizim zamanımızda da hep ben kazanırdım.",
      "Maçı aldım. Torunuma anlatırım.", "Hıh, kazandım. Oraletler benden… bir tane.")
LINES(kMatchLoseR, "Güzel oyundu, hayırlı olsun.", "Kazanana helal olsun, oyun oyundur.", "Yarın yine buradayız evlatlar.")
LINES(kMatchLoseM, "Rövanş isterim!", "Yarın aynı saatte, rövanş!", "Hakem yüzünden abi, hakem!",
      "Bu sefer olmadı, yarın şampiyonluk!")
LINES(kMatchLoseN, "Bu taşlar bozuk, yarın kendi taşlarımı getireceğim.", "Kaybettik. Dizlerim de bitti zaten.",
      "Hıh, bitti. Torunum sorar: dede kazandın mı? Hayır derim.")
LINES(kMatchHumanR, "Helal olsun {h}, bizi güzel yendin.", "Hayırlı olsun {h}, oyunu hak ettin.",
      "{h} kazandı; çaylar benden, tebrik senden.")
LINES(kMatchHumanM, "{h}, bizi fena yendin! Rövanş var ama!", "Şampiyon {h}! Yarın rövanş!",
      "{h} kupayı kaldırdı, biz seyrettik!")
LINES(kMatchHumanN, "Hıh… tebrikler {h}. Yarın yine gel.", "Kazandın {h}. Bizim zamanımızda kazanan çay ısmarlardı.",
      "Tebrikler {h}. Torunuma seni anlatırım, iyi manada.")
// ---------------------------------------------------------------- idle chatter
LINES(kIdleR, "Sabreden derviş muradına ermiş.", "Damlaya damlaya göl olur.",
      "Bir kahvenin kırk yıl hatırı vardır.", "Acele işe şeytan karışır.", "Komşu komşunun külüne muhtaçtır.",
      "Az olsun, öz olsun.", "İşleyen demir ışıldar.", "Bu kahvehane benim gençliğimden beri burada.",
      "Bu tespih kırk yıllık, hacdan getirdim.", "Akşama yağmur yağacak, belli.", "Sakla samanı, gelir zamanı.",
      "Çay demini aldı mı, ondan sonra güzel.", "Rıfat Efendi bu masada kırk yıl oturdu, rahmetli.",
      "Tespihin ipi eskidi, kendi eskimedi.", "Kahvenin duvarı bile bizi tanır artık.",
      "Torunum geldi hafta sonu, boyu beni geçti.", "Ayaz var dışarıda, çaylar bir güzel oldu.",
      "Vakit nasıl geçiyor, akşam olmuş bile.", "Gençken bu masada babamla otururdum.",
      "Bin bilsen de bir bilene danış.", "Ne ekersen onu biçersin evlatlar.",
      "Ağaç yaşken eğilir; gençleri iyi yetiştirin.", "Tatlı dil yılanı deliğinden çıkarır.",
      "Şu çaycı çocuk çalışkan, hayrını görsün.")
LINES(kIdleM, "Bizim takım yine kaybetmiş…", "Dün akşamki maçı gördünüz mü? Hakem rezalet!",
      "Bu sene şampiyonluk bizim abi!", "Ben gençken top oynardım, sol açık!",
      "Şu televizyonun sesini açın biraz!", "O penaltıyı ben atsam girerdi!", "Maç kaç kaç oldu?",
      "Abi transfer yapmıyorlar, ne olacak bu takımın hali?", "Bu akşam derbi var, kaçırmam!",
      "Hakem düdüğü yutmuş abi!", "Benim bir kuzenim vardı, az kalsın milli olacaktı!",
      "Ofsayt mı o? Değil o, değil!", "Bizim takım bu sene şampiyon, yazın bir kenara!",
      "Hoca bizim takımı berbat etti abi, berbat!", "Ben olsam o forveti orta sahaya çekerim!",
      "Askerde bölüğün kaptanıydım, sormayın!", "Bu sene stada gidiyorum, kombine aldım!",
      "Rakip takım kaç yıldır şampiyon olamıyor, ha ha!", "Simit iki ekmek parası olmuş abi, olacak iş mi?",
      "Şu televizyona bir yenisini alsak Hacı Abi, bu kar yağdırıyor.",
      "Ben gençken boğazda mangal yapardık abi, ne günlerdi.", "Mahallede halı saha yaptılar, hafta sonu oradayım!",
      "Kelim ama yakışıklıyım abi, ne var!", "Hacı Abi'nin torunu bile bizim takımı tutuyor!")
LINES(kIdleN, "Bizim zamanımızda böyle oynanmazdı…", "Eskiden çay kırk kuruştu.",
      "Yağmur yağacakmış, dizlerim sızlıyor.", "Şu gençler hep telefonda.",
      "Emekli maaşıyla çay içilmiyor artık.", "Bu taşlar eskiden kemikti, şimdi plastik.", "Hava da soğudu ha.",
      "Otuz yıl memurluk yaptım, bir gün geç kalmadım.", "Bu kahvenin eski sahibi Rıfat Efendi, ne adamdı!",
      "Radyoda eskiden ne güzel şarkılar çalardı.", "Doktor çayı azalt dedi, ben de oraleti çoğalttım.",
      "Torunum bilgisayarda okey oynuyormuş. Olur mu öyle şey!",
      "Simit bir liraydı, şimdi fiyatını söylemeye korkuyorum.", "Otuz yıl devlet dairesi… mühür, kaşe, imza.",
      "Torunum diyor ki dede telefondan konuş. Ben böyle konuşurum.", "Dizlerim havayı benden önce anlıyor.",
      "Oralet portakallı iyi de, çay gibi olmuyor.", "Askerde bu okeyi ben öğrettim bütün bölüğe.",
      "Eskiden bu kahvede radyo çalardı, şimdi televizyon bağırıyor.", "Rıfat Efendi çayı kendi demlerdi, tavşan kanı.",
      "Doktor yürü dedi, ben de kahveye yürüyorum.", "Emekli ikramiyesiyle ne aldım biliyor musunuz? Gözlük.",
      "Boğazda balığa çıkardık, şimdi balık da emekli oldu.", "Güvercinlerim vardı gençken, taklacı. Uçurdum hepsini.")
// ---------------------------------------------------------------- tea
LINES(kTeaR, "Çaycı! Bir çay daha evlat.", "Çaycı, tazele şunları, Allah razı olsun.",
      "Evlat, çaylar benden, getir bakalım.", "Çaycı, herkese birer çay.", "Çaycı! Demli olsun evlat.",
      "Çaycı, bir demli çay, evlat.", "Evlat, çaylarımız bitti, bir el at.")
LINES(kTeaM, "Çaycı! İki çay bir oralet!", "Çaycı! Çaylar nerede kaldı abi?", "Oğlum, tavşan kanı olsun, hadi!",
      "Çaycııı!", "Çaycı, sıcak sıcak getir!", "Çaycı! Devre arası, çaylar!", "Oğlum çaycı, biz burada çöl gibiyiz!")
LINES(kTeaN, "Çaycı! Bana bir oralet, portakallı!", "Evladım, oralet soğudu, yenisini getir.",
      "Çaycı, benimki açık olsun, tansiyon var.", "Bir oralet daha, ama bu sefer sıcak olsun!",
      "Bu çaycı da hep geç kalır.", "Çaycı! Oralet, portakallı, sıcak, çabuk.", "Evlat, bir oralet; doktor çayı yasakladı.")
LINES(kThanksR, "Eline sağlık evlat.", "Allah razı olsun.", "Sağ ol evlat, bereket versin.", "Ha şöyle, demli olmuş.")
LINES(kThanksM, "Sağ ol aslanım!", "Oh be, tavşan kanı!", "İşte bu, sağ ol oğlum!", "Geldi mi çaylar? Helal!")
LINES(kThanksN, "Sıcak mı bu? Hah, iyi.", "Sağ ol evladım.", "Portakallı mı? Hıh, tamam.", "Geç geldi ama geldi, sağ ol.")
LINES(kCatR, "Pisi pisi… gel bakalım, gel.", "Bu kedi bu kahvenin demirbaşı.", "Acıkmış garip, bir şey verin.",
      "Kedi de misafirdir, hoş geldi.")
LINES(kCatM, "Tekir, sen hangi takımı tutuyorsun?", "Pisi, gel buraya, uğur getir!", "Bu kedi benden çok maç izliyor.",
      "Sus pisi, düşünüyorum!")
LINES(kCatN, "Bu kedi benden yaşlı, bilirim.", "Pisi, dizlerime çıkma, ağrıyor.", "Rıfat Efendi'nin zamanından kalma bu kedi.",
      "Hıh, o da çay istiyor herhalde.")
LINES(kCatC, "Aç değil abi, az önce yedi!", "Pisi, ocağa yaklaşma!")
LINES(kGoalR, "Maşallah, güzel gol.", "Gol mü oldu? Hayırlı olsun.", "Güzel vurmuş, maşallah.")
LINES(kGoalM, "Gooool!", "Oley! Gördünüz mü?", "Ofsayt o abi, ofsayt!", "İşte bu! Gooool!", "Nasıl attı, nasıl!",
      "Hakem, o gol değil!")
LINES(kGoalN, "Neydi o savunma öyle!", "Bizim zamanımızda böyle gol yenmezdi.", "Gol mü? Gözlüğümü almadım.",
      "Hıh, bir gol için bu kadar bağırılır mı?")
// ---------------------------------------------------------------- the çaycı (seat 4; short, he may be far away)
LINES(kCayciComing, "Geliyor abi!", "Hemen abi!", "Bir dakika abi, demleniyor!", "Tamam abi, geliyor!",
      "Bir saniye abi!", "Yetiştiriyorum abi!", "Duydum abi, geliyor!")
LINES(kCayciServed, "Buyurun abiler.", "Çaylar geldi!", "Afiyet olsun abiler.", "Tavşan kanı abi, buyur.",
      "Oralet de geldi Nuri Amca.", "Sıcak sıcak, dikkat abi.")
LINES(kCayciIdle, "Çay parası birikti ha abiler.", "Abi ben de oynayabilirim istersen.", "Nuri Amca, oralet mi çay mı bugün?",
      "Mahmut Abi, maç kaç kaç?", "Abi, çay ocağı benden çok çalışıyor.", "Bardaklar boş, abiler dolu.")
LINES(kCayciGoal, "Gol mü oldu abi?", "Oley!", "Kim attı abi?")

const Tbl kTables[S_Count][4] = {
    {T(kWelcomeR), T(kWelcomeM), T(kWelcomeN), TN},
    {T(kHandStartR), T(kHandStartM), T(kHandStartN), TN},
    {T(kHandLastR), T(kHandLastM), T(kHandLastN), TN},
    {T(kLeadSelfR), T(kLeadSelfM), T(kLeadSelfN), TN},
    {T(kLeadOtherR), T(kLeadOtherM), T(kLeadOtherN), TN},
    {T(kLeadHumanR), T(kLeadHumanM), T(kLeadHumanN), TN},
    {T(kBehindSelfR), T(kBehindSelfM), T(kBehindSelfN), TN},
    {T(kBehindHumanR), T(kBehindHumanM), T(kBehindHumanN), TN},
    {T(kTurnR), T(kTurnM), T(kTurnN), TN},
    {T(kHumanTurnR), T(kHumanTurnM), T(kHumanTurnN), TN},
    {T(kWaitR), T(kWaitM), T(kWaitN), TN},
    {T(kDrawR), T(kDrawM), T(kDrawN), TN},
    {T(kTakeSelfR), T(kTakeSelfM), T(kTakeSelfN), TN},
    {T(kTakeVictimR), T(kTakeVictimM), T(kTakeVictimN), TN},
    {T(kHumanTookR), T(kHumanTookM), T(kHumanTookN), TN},
    {T(kDiscardSelfR), T(kDiscardSelfM), T(kDiscardSelfN), TN},
    {T(kHumanDiscardR), T(kHumanDiscardM), T(kHumanDiscardN), TN},
    {T(kReturnSelfR), T(kReturnSelfM), T(kReturnSelfN), TN},
    {T(kOpenSelfR), T(kOpenSelfM), T(kOpenSelfN), TN},
    {T(kOpenPairsR), T(kOpenPairsM), T(kOpenPairsN), TN},
    {T(kOpenOtherR), T(kOpenOtherM), T(kOpenOtherN), TN},
    {T(kOpenBigR), T(kOpenBigM), T(kOpenBigN), TN},
    {T(kOpenOtherPairsR), T(kOpenOtherPairsM), T(kOpenOtherPairsN), TN},
    {T(kOpenHumanR), T(kOpenHumanM), T(kOpenHumanN), TN},
    {T(kAddSelfR), T(kAddSelfM), T(kAddSelfN), TN},
    {T(kAddOtherR), T(kAddOtherM), T(kAddOtherN), TN},
    {T(kSwapSelfR), T(kSwapSelfM), T(kSwapSelfN), TN},
    {T(kSwapOtherR), T(kSwapOtherM), T(kSwapOtherN), TN},
    {T(kSwapHumanR), T(kSwapHumanM), T(kSwapHumanN), TN},
    {T(kPenSelfR), T(kPenSelfM), T(kPenSelfN), TN},
    {T(kPenOtherR), T(kPenOtherM), T(kPenOtherN), TN},
    {T(kPenHumanR), T(kPenHumanM), T(kPenHumanN), TN},
    {T(kPenOkeyOtherR), T(kPenOkeyOtherM), T(kPenOkeyOtherN), TN},
    {T(kPenOkeyHumanR), T(kPenOkeyHumanM), T(kPenOkeyHumanN), TN},
    {T(kLowR), T(kLowM), T(kLowN), TN},
    {T(kWinR), T(kWinM), T(kWinN), TN},
    {T(kWinOkeyR), T(kWinOkeyM), T(kWinOkeyN), TN},
    {T(kLoseR), T(kLoseM), T(kLoseN), TN},
    {T(kStreakOtherR), T(kStreakOtherM), T(kStreakOtherN), TN},
    {T(kHumanWonR), T(kHumanWonM), T(kHumanWonN), TN},
    {T(kHumanStreakR), T(kHumanStreakM), T(kHumanStreakN), TN},
    {T(kPileOutR), T(kPileOutM), T(kPileOutN), TN},
    {T(kMatchWinR), T(kMatchWinM), T(kMatchWinN), TN},
    {T(kMatchLoseR), T(kMatchLoseM), T(kMatchLoseN), TN},
    {T(kMatchHumanR), T(kMatchHumanM), T(kMatchHumanN), TN},
    {T(kIdleR), T(kIdleM), T(kIdleN), TN},
    {T(kTeaR), T(kTeaM), T(kTeaN), TN},
    {T(kThanksR), T(kThanksM), T(kThanksN), TN},
    {T(kGoalR), T(kGoalM), T(kGoalN), TN},
    {TN, TN, TN, T(kCayciComing)},
    {TN, TN, TN, T(kCayciServed)},
    {TN, TN, TN, T(kCayciIdle)},
    {TN, TN, TN, T(kCayciGoal)},
    {T(kCatR), T(kCatM), T(kCatN), T(kCatC)},
};

// Multi-line idle exchanges (2–5 lines): seats 1 Rıza, 2 Mahmut, 3 Nuri, 4 the çaycı. Lines may use {h}.
// The list ends at the first entry with seat 0.
struct DLine {
    int seat;
    const char* text;
};
struct Dialogue {
    DLine l[5];
};
const Dialogue kDialogues[] = {
    {{{2, "Bu sene şampiyonluk bizim abi!"}, {3, "Her sene aynı lafı söylüyorsun Mahmut."},
      {2, "Bu sene farklı Nuri Abi, hissediyorum!"}, {3, "Geçen sene de hissetmiştin."},
      {1, "Umut fakirin ekmeğidir evlatlar, bırakın yesin."}}},
    {{{3, "Eskiden çay kırk kuruştu."}, {2, "Nuri Abi, o zaman maaş da kırk liraydı."},
      {3, "Kırk lirayla ev alınırdı Mahmut, ev!"}, {1, "Eski günler, eski günler…"}}},
    {{{2, "Dün akşamki maçı gördünüz mü? Hakem rezalet!"}, {1, "Maç maçtır evlat, sinirlenme."},
      {2, "Hacı Abi, penaltıyı vermedi, penaltıyı!"}, {3, "Bizim zamanımızda hakem düdük çalmazdı, saygıdan."}}},
    {{{1, "Akşama yağmur yağacak, belli."}, {3, "Dizlerim zaten söyledi, iki saat önce."},
      {2, "Nuri Abi'nin dizleri meteorolojiden iyi abi."}}},
    {{{3, "Şu gençler hep telefonda."}, {2, "Haklısın Nuri Abi, ben de sadece maça bakıyorum."},
      {3, "Sen de telefondan bakıyorsun Mahmut!"}, {2, "O farklı abi, o maç!"}}},
    {{{1, "Bir kahvenin kırk yıl hatırı vardır."}, {2, "O zaman bir kahve ısmarla Hacı Abi!"},
      {1, "Kırk yıl sonra hatırlatırsın evladım."}}},
    {{{3, "Doktor çayı azalt dedi."}, {1, "Doktor ne bilir Nuri, çay şifadır."},
      {3, "Ben de oraleti çoğalttım Hacı, denge."}, {2, "Oralet çayın yedek kulübesi abi, oynamaz!"}}},
    {{{2, "Şu televizyonun sesini açın biraz!"}, {3, "Açma açma, kafam şişti zaten."},
      {2, "Nuri Abi, maç bu, radyo değil!"}, {3, "Radyo olsa hiç açmazdım."}}},
    {{{1, "Bu tespih kırk yıllık, hacdan getirdim."}, {2, "Kırk yıl aynı tespih mi Hacı Abi?"},
      {1, "Tespih eskimez evladım, çeken eskir."}, {3, "Benim dizler gibi."}}},
    {{{3, "Rıfat Efendi bu kahveyi işletirken böyle gürültü olmazdı."}, {1, "Rahmetli, çayı kendi demlerdi."},
      {2, "Ben yetişemedim Rıfat Efendi'ye, nasıl adamdı?"}, {3, "Sana bir bakışta çayı iki kere ödetirdi."},
      {1, "Allah rahmet eylesin, hakkı çoktur bu masada."}}},
    {{{2, "Çaycı nerede kaldı ya?"}, {4, "Geliyor abi, demleniyor!"},
      {3, "Bizim zamanımızda çay demlenmez, hazır beklerdi."}, {4, "Nuri Amca, o zaman sen çaycıydın herhalde!"}}},
    {{{3, "Simit kaç lira olmuş biliyor musunuz?"}, {2, "Söyleme Nuri Abi, iştahım kaçacak."},
      {3, "Ben gençken simitle deniz manzarası bedavaydı."}, {1, "Deniz hâlâ bedava Nuri, şükret."}}},
    {{{2, "Ben gençken sol açık oynardım, çok hızlıydım!"}, {3, "Şimdi de hızlısın Mahmut, konuşmada."},
      {2, "Nuri Abi, ciddiyim, menajer gelmişti!"}, {3, "Kahveye mi gelmişti?"}}},
    {{{1, "Torunum geldi hafta sonu, boyu beni geçti."}, {3, "Benimki telefonda okey oynuyor; dede, sen eskisin diyor."},
      {2, "Torunlar bizi kahveye yollar abi, huzur bulalım."}}},
    {{{2, "Askerde bölüğün kaptanıydım abi!"}, {3, "Ben de otuz yıl memurdum, ne olmuş?"},
      {2, "Nuri Abi, futbol kaptanı, mühür kaptanı değil!"}, {1, "İkiniz de vatan için çalıştınız, kavga etmeyin."}}},
    {{{3, "Boğazda balığa çıkardık gençken. Lüfer, palamut…"}, {2, "Şimdi çıksan ne tutarsın Nuri Abi?"},
      {3, "Soğuk tutarım Mahmut, soğuk."}}},
    {{{3, "Güvercinlerim vardı, taklacı. Uçururdum sabahları."}, {1, "Güvercin sadık hayvandır, geri gelir."},
      {3, "Benimkiler gelmedi Hacı. Komşunun damında kaldılar."}, {2, "Rakip takıma transfer olmuşlar abi!"}}},
    {{{2, "Bizim takım bu hafta rakibi ezer!"}, {3, "Geçen hafta da ezecekti."},
      {2, "Geçen hafta hakem vardı Nuri Abi!"}, {3, "Bu hafta hakem olmayacak mı?"},
      {1, "Sabır Mahmut evladım, sabır."}}},
    {{{1, "Çay demini aldı mı, ondan sonra güzel."}, {4, "Hacı Amca, bu çay bir saat demlendi!"},
      {1, "Eline sağlık evlat, yeter."}, {3, "Bir saat mi? Bizim zamanımızda üç saat demlenirdi."}}},
    {{{3, "Bizim zamanımızda 101'de çift açmak yoktu."}, {2, "Nuri Abi, o zaman sen hep kaybediyordun herhalde."},
      {3, "Kaybetmiyordum, kurallar farklıydı!"}, {1, "Her devrin kendi kuralı var Nuri."}}},
    {{{2, "Bu el {h} sessiz, bir şeyler dönüyor!"}, {3, "Sessiz sular derin akar Mahmut."},
      {1, "Sen kendi taşına bak evladım."}}},
    {{{3, "Havalar da ısındı, oralet buz gibi oldu."}, {2, "Nuri Abi, yaz geliyor, bahaneler tükeniyor!"},
      {3, "Yazın da dizim ağrır, merak etme."}}},
    {{{1, "Sakla samanı, gelir zamanı."}, {2, "Hacı Abi ben taşları sakladım, zamanı gelmiyor!"},
      {1, "Sabır evladım, saman da bekler."}}},
    {{{2, "Şu maçın sonucunu öğrenen var mı?"}, {4, "Bir sıfır abi, doksanda!"}, {2, "Kim attı? Kim?"},
      {4, "Bilmem abi, çay dağıtıyordum!"}}},
    {{{3, "Kahvede eskiden herkes birbirini tanırdı."}, {1, "Şimdi de tanıyoruz Nuri; işte {h} bile bizden."},
      {2, "{h} bizden ama taşları bize vermiyor abi!"}}},
    {{{2, "Kel dediler, kel oldum. Adım Mahmut ama!"}, {3, "Saç gider, isim kalır Mahmut."},
      {1, "Kelin merhemi olsa kendi başına sürer."}, {2, "Hacı Abi, o ne alaka şimdi!"}}},
    {{{3, "Ah şu dizlerim, merdiven düşmanım."}, {2, "Nuri Abi, asansör al!"},
      {3, "Emekli maaşıyla asansör mü alınır Mahmut?"}, {1, "Sağlık olsun Nuri, gerisi boş."}}},
    {{{1, "Mahalle eskisi gibi değil, herkes kapısını kilitliyor."}, {3, "Bizim zamanımızda kapı açık, sofra açıktı."},
      {2, "Şimdi de açık abi, sipariş kapıya geliyor!"}}},
    {{{2, "Çaycı! Bir çay daha!"}, {4, "Abi bu üçüncü, hesabı biliyorsun!"}, {2, "Hesabı Hacı Abi'ye yaz!"},
      {1, "Yaz evlat, ben öderim; ama Mahmut çayı azaltsın."}}},
    {{{3, "Radyoda eskiden ne güzel şarkılar çalardı."}, {1, "Şimdi de çalıyor Nuri, kulak vermiyoruz."},
      {2, "Ben maç anlatan radyoyu severim abi!"}}},
    {{{2, "{h}, sen hangi takımı tutuyorsun?"}, {3, "Sorma Mahmut, cevabı beğenmezsen kavga çıkarırsın."},
      {2, "Ben sadece soruyorum abi!"}, {1, "Herkesin gönlü kendinin, evladım."}}},
    {{{3, "Bizim zamanımızda okey elden bitince iki misli yazılırdı."}, {1, "Hâlâ yazılıyor Nuri, sen dikkat etmiyorsun."},
      {3, "Hıh, ben otuz yıl hesap tuttum Hacı!"}}},
    {{{1, "İşleyen demir ışıldar."}, {3, "Ben otuz yıl işledim, ışıldamadım Hacı."},
      {2, "Nuri Abi kel olsaydın ışıldardın, bana bak!"}}},
    {{{2, "Boğazda balık ekmek yedim geçen gün, kral gibi!"}, {3, "Kaça Mahmut?"},
      {2, "Sorma Nuri Abi, kralın hazinesi gitti."}}},
    {{{4, "Abiler, çay ocağı yarım saate kapanıyor."}, {2, "Ne kapanması, maç bitmedi!"},
      {3, "Bizim zamanımızda kahve sabaha kadar açıktı."}, {4, "Nuri Amca, sizin zamanınızda ben doğmamıştım!"}}},
    {{{1, "Komşu komşunun külüne muhtaçtır."}, {2, "Hacı Abi, komşum bana hiç taş vermiyor ama!"},
      {1, "Sen de komşuna veriyor musun evladım?"}, {2, "Konuyu değiştirelim abi."}}},
    {{{3, "Otuz yıl memurluk yaptım, bir gün geç kalmadım."}, {2, "Kahveye de geç kalmıyorsun Nuri Abi, helal!"},
      {3, "Disiplin Mahmut, disiplin."}, {1, "Bir de sabır Nuri, sabır."}}},
    {{{2, "Yeni forma çıkmış abi, alacağım!"}, {3, "Kaç para Mahmut?"}, {2, "Bir emekli maaşı Nuri Abi."},
      {3, "O zaman benim formam yok."}}},
    {{{1, "Hava soğudu, kışlıkları çıkarma vakti."}, {3, "Ben hiç kaldırmadım Hacı, dizlerim yaz kış üşür."},
      {2, "Nuri Abi, dizlerine forma al!"}}},
    {{{3, "Torunum sordu: dede bu okey ne işe yarar? Ne dedim biliyor musunuz?"}, {2, "Ne dedin Nuri Abi?"},
      {3, "Dedim: bu masada dostluk yarar, gerisi taş."}, {1, "Güzel demişsin Nuri, gel sarılayım."},
      {3, "Sarılma Hacı, dizim."}}},
    {{{4, "Abi, o taşı atmasaydın!"}, {2, "Oğlum sen çayına bak, taşa karışma!"}, {4, "Tamam abi, ama görüyorum."},
      {3, "Bizim zamanımızda çaycı konuşmazdı."}}},
    {{{2, "{h}, çay içiyor musun? Çaycı, bir de {h} için!"}, {4, "Geliyor abi!"},
      {1, "Çaylar benden, kimse elini cebine atmasın."}}},
    {{{3, "Bu taşlar eskiden kemikti, şimdi plastik."}, {2, "Kemik taş mı? Nuri Abi kaç yaşındasın sen?"},
      {3, "Senin saçın dökülmeden çok önce, Mahmut."}}},
    {{{1, "Bu kahvehane benim gençliğimden beri burada."}, {2, "Hacı Abi, sen mi eskisin kahve mi?"},
      {1, "İkimiz de eskiyiz evladım, ikimiz de ayaktayız."}}},
    {{{2, "Nuri Abi, o gözlükle taşları görüyor musun gerçekten?"}, {3, "Taşları görüyorum Mahmut, seni görmesem de olur."},
      {1, "Gözlük göze, edep söze yakışır evlatlar."}}},
    {{{3, "Bu oralet de soğudu. Çaycı!"}, {4, "Nuri Amca, üç dakika önce getirdim!"},
      {3, "Bizim zamanımızda üç dakikada soğumazdı."}, {2, "Nuri Abi, sizin zamanınızda güneş de sıcaktı herhalde."}}},
};
constexpr int kNumDialogues = (int)(sizeof(kDialogues) / sizeof(kDialogues[0]));

int dialogueLen(const Dialogue& d) {
    int n = 0;
    while (n < 5 && d.l[n].seat > 0) ++n;
    return n;
}

constexpr float kGlobalGap = 2.4f;
constexpr float kSeatCooldown = 7.5f;

float bubbleSeconds(const std::string& text) {
    return std::clamp(1.9f + 0.045f * (float)text.size(), 2.4f, 4.6f);
}

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
    sinceSpoke_.fill(99.f);
    globalCool_ = 0.f;
    idleIn_ = 16.f + rnd() * 10.f;
    humanWait_ = 0.f;
    nagLevel_ = 0;
    turnSeat_ = -1;
    handLive_ = false;
    lowPileSaid_ = false;
    lastDiscardJoker_ = false;
    lastWinner_ = -2;
    teaOrderCool_ = 25.f;
    dialogueSerial_ = 0;
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
    // distinct texts: a few classic remarks live both in the idle table and as the opener of an exchange
    std::set<std::string> all;
    for (const auto& row : kTables)
        for (const Tbl& t : row)
            for (int i = 0; i < t.n; ++i) all.insert(t.lines[i]);
    for (const Dialogue& d : kDialogues)
        for (int i = 0; i < dialogueLen(d); ++i) all.insert(d.l[i].text);
    return (int)all.size();
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
// calls Rıza by his title; Rıza, the eldest, is fatherly with Mahmut; the çaycı says "amca"/"abi". The human
// keeps humanAddress().
std::string Banter::address(int speaker, int target) const {
    if (target == 0) return humanAddress(speaker);
    static const char* const kAddr[5][4] = {
        {"", "", "", ""},
        {"", "", "Mahmut evladım", "Nuri"},     // Hacı Rıza ->
        {"", "Hacı Abi", "", "Nuri Abi"},       // Kel Mahmut ->
        {"", "Hacı", "Mahmut", ""},             // Emekli Nuri ->
        {"", "Hacı Amca", "Mahmut Abi", "Nuri Amca"}, // the çaycı ->
    };
    if (speaker >= 1 && speaker <= 4 && target >= 1 && target <= 3 && kAddr[speaker][target][0] != 0)
        return kAddr[speaker][target];
    return shortName(target);
}

std::string Banter::humanAddress(int speaker) const {
    const std::string& n = names_[0];
    if (!n.empty() && n != "Sen" && n != "sen" && n != "Oyuncu") return n;
    switch (speaker) {
    case 1: return "evlat";
    case 2: return "abi";
    case 4: return "abi";
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
    if (!enabled_ || seat < 1 || seat > 4 || tableId < 0 || tableId >= S_Count) return false;
    if (!force && (seatCool_[seat] > 0.f || globalCool_ > 0.f)) return false;
    // even an important line waits a moment if the same man has just said something
    if (force && sinceSpoke_[seat] < 2.5f) delay = std::max(delay, 2.5f - sinceSpoke_[seat]);
    // never stack more than a couple of pending lines
    if (queue_.size() >= 3 && !force) return false;
    // one pending line per seat: an important line replaces a casual one (and, if that line belonged to an
    // exchange, the rest of the interrupted exchange goes with it)
    bool mine = false;
    int grp = -1;
    for (const Pending& p : queue_)
        if (p.line.seat == seat) {
            mine = true;
            grp = std::max(grp, p.group);
        }
    if (mine) {
        if (!force) return false;
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                    [&](const Pending& p) {
                                        return p.line.seat == seat || (grp >= 0 && p.group == grp);
                                    }),
                     queue_.end());
    }
    const Tbl& t = kTables[tableId][seat - 1];
    if (t.n <= 0) return false;
    int slot = std::min(255, tableId * 4 + (seat - 1));
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
    p.sit = tableId;
    p.line.seat = seat;
    p.line.text = text;
    p.line.delay = delay;
    p.line.seconds = bubbleSeconds(text);
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
    while (recent_.size() > 40) recent_.pop_front();
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
        lastWinner_ = -2;
        if (chance(0.75f)) say(pickBot(), S_Welcome, 0.8f, true, 0, 0, 6.f);
        break;
    case EvType::HandStart: {
        handLive_ = true;
        lowPileSaid_ = false;
        lastDiscardJoker_ = false;
        humanWait_ = 0.f;
        nagLevel_ = 0;
        const int hi = g.handIndex();
        if (hi == 0) {
            // the greeting may still be on its way: a hand-start line only a little later
            if (chance(0.5f)) say(pickBot(), S_HandStart, 3.4f, true, 0, -1, 6.f);
            break;
        }
        if (!chance(0.7f)) break;
        // who leads / trails the match (lowest total is best); ties: no score talk
        int lead = 0, last = 0;
        bool tieLead = false, tieLast = false;
        for (int s = 1; s <= 3; ++s) {
            const int t = g.player(s).totalScore;
            if (t < g.player(lead).totalScore) { lead = s; tieLead = false; }
            else if (t == g.player(lead).totalScore) tieLead = true;
            if (t > g.player(last).totalScore) { last = s; tieLast = false; }
            else if (t == g.player(last).totalScore) tieLast = true;
        }
        const float r = rnd();
        if (hi == g.numHands() - 1 && r < 0.45f) {
            say(pickBot(), S_HandLast, 1.2f, true, 0, -1, 5.f);
        } else if (r < 0.75f && !tieLead && !tieLast) {
            switch (rndInt(4)) {
            case 0:
                if (lead >= 1) say(lead, S_LeadSelf, 1.2f, true, 0, lead, 5.f);
                else say(pickBot(), S_LeadHuman, 1.2f, true, 0, 0, 5.f);
                break;
            case 1:
                if (lead >= 1) say(pickOther(lead), S_LeadOther, 1.2f, true, 0, lead, 5.f);
                else say(pickBot(), S_LeadHuman, 1.2f, true, 0, 0, 5.f);
                break;
            case 2:
                if (last >= 1) say(last, S_BehindSelf, 1.2f, true, 0, last, 5.f);
                else say(pickBot(), S_BehindHuman, 1.2f, true, 0, 0, 5.f);
                break;
            default:
                say(pickBot(), S_HandStart, 1.2f, true, 0, -1, 5.f);
                break;
            }
        } else {
            say(pickBot(), S_HandStart, 1.2f, true, 0, -1, 5.f);
        }
        break;
    }
    case EvType::TurnStart:
        turnSeat_ = actor;
        humanWait_ = 0.f;
        nagLevel_ = 0;
        lastDiscardJoker_ = false;
        if (actorBot && chance(0.035f)) say(actor, S_TurnSelf, 0.1f);
        else if (actor == 0 && chance(0.04f)) say(pickBot(), S_HumanTurn, 0.4f, false, 0, 0);
        break;
    case EvType::DrawPile:
        if (actorBot && chance(0.02f)) say(actor, S_DrawSelf, 0.3f);
        if (!lowPileSaid_ && g.pileCount() <= 6 && g.pileCount() > 0 && handLive_) {
            lowPileSaid_ = true;
            if (chance(0.7f)) say(pickBot(), S_LowPile, 0.6f, true, 0, -1, 5.f);
        }
        break;
    case EvType::TakeLeft: {
        int giver = okey::Game::leftOf(actor);
        if (actorBot) {
            if (chance(0.3f)) say(actor, S_TakeLeftSelf, 0.2f, false, 0, actor);
            else if (giver >= 1 && chance(0.35f)) say(giver, S_TakeLeftVictim, 0.4f, false, 0, actor);
        } else if (actor == 0) {
            if (giver >= 1 && chance(0.45f)) say(giver, S_HumanTookMine, 0.3f, false, 0, actor);
            else if (chance(0.2f)) say(pickOther(giver), S_HumanTookMine, 0.5f, false, 0, actor);
        }
        break;
    }
    case EvType::ReturnLeft:
        lastDiscardJoker_ = false;
        if (actorBot && chance(0.5f)) say(actor, S_ReturnSelf, 0.2f, false, 0, actor);
        break;
    case EvType::Discard: {
        lastDiscardJoker_ = g.okey().isJoker(e.tile);
        // no small talk over a finishing discard (the hand-end reaction follows at once)
        if (!handLive_ || actor < 0 || actor > 3 || g.player(actor).hand.empty() || g.pileCount() == 0) break;
        if (actor == 0) {
            if (chance(0.06f)) say(1, S_HumanDiscard, 0.7f, false, 0, 0);
            else if (chance(0.03f)) say(pickOther(1), S_HumanDiscard, 0.7f, false, 0, 0);
        } else if (chance(0.035f)) {
            say(actor, S_DiscardSelf, 0.3f, false, 0, okey::Game::rightOf(actor));
        }
        break;
    }
    case EvType::Open: {
        bool pairs = actor >= 0 && actor < 4 && g.player(actor).openedWithPairs;
        int v = e.amount;
        if (actorBot) {
            if (chance(0.9f)) say(actor, pairs ? S_OpenSelfPairs : S_OpenSelf, 0.25f, true, v, actor, 4.f);
            if (chance(0.35f)) {
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
        if (actorBot && chance(0.1f)) say(actor, S_AddSelf, 0.2f);
        break;
    case EvType::AddToMeld:
        if (actorBot && chance(0.08f)) say(actor, S_AddSelf, 0.2f);
        else if (chance(0.05f)) say(pickOther(actor), S_AddOther, 0.5f, false, 0, actor);
        break;
    case EvType::SwapJoker:
        if (actorBot) {
            if (chance(0.65f)) say(actor, S_SwapSelf, 0.2f, true, 0, actor);
            if (chance(0.4f)) say(pickOther(actor), S_SwapOther, 2.2f, false, 0, actor);
        } else if (actor == 0) {
            if (chance(0.6f)) say(pickBot(), S_SwapHuman, 0.5f, false, 0, actor);
        }
        break;
    case EvType::Penalty:
        if (actor == 0) {
            if (chance(0.85f))
                say(pickBot(), lastDiscardJoker_ ? S_PenOkeyHuman : S_PenaltyHuman, 0.5f, true, 0, actor, 4.f);
        } else if (actorBot) {
            if (chance(0.7f))
                say(pickOther(actor), lastDiscardJoker_ ? S_PenOkeyOther : S_PenaltyOther, 0.4f, true, 0, actor, 4.f);
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
            const bool streak = (w == lastWinner_);
            if (chance(0.6f)) say(pickOther(w), streak ? S_StreakOther : S_LoseGrumble, 2.6f, true, 0, w, 7.f);
        } else if (w == 0) {
            const int s = pickBot();
            say(s, lastWinner_ == 0 ? S_HumanStreak : S_HumanWon, 0.4f, true, 0, 0, 6.f);
            if (chance(0.45f)) say(pickOther(s), S_LoseGrumble, 2.8f, true, 0, 0, 7.f);
        } else {
            const int s = pickBot();
            say(s, S_PileOut, 0.4f, true, 0, -1, 6.f);
            if (chance(0.3f)) say(pickOther(s), S_PileOut, 2.8f, true, 0, -1, 7.f);
        }
        lastWinner_ = w;
        break;
    }
    case EvType::MatchEnd: {
        int w = actor;
        // the hand-end grumble ("next hand is mine") makes no sense any more: the match is over
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                    [](const Pending& p) {
                                        return p.sit == S_LoseGrumble || p.sit == S_StreakOther;
                                    }),
                     queue_.end());
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
            const int s = pickBot();
            say(s, S_MatchHumanWon, 1.0f, true, 0, 0, 8.f);
            if (chance(0.6f)) say(pickOther(s), S_MatchHumanWon, 3.8f, true, 0, 0, 9.f);
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- idle exchanges
void Banter::startDialogue() {
    int di = rndInt(kNumDialogues);
    for (int k = 0; k < kNumDialogues; ++k, di = (di + 1) % kNumDialogues) {
        bool used = false;
        for (int u : lastDialogue_) used = used || (u == di);
        if (used) continue;
        const Dialogue& d = kDialogues[di];
        const int n = dialogueLen(d);
        if (n < 2) continue;
        // the opener must be able to speak now; later speakers only need to be free by their own turn
        std::string texts[5];
        float delays[5];
        float t = 0.f;
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) {
            texts[i] = fill(d.l[i].text, d.l[i].seat, 0, -1);
            delays[i] = t;
            if (seatCool_[d.l[i].seat] > t) ok = false;
            t += bubbleSeconds(texts[i]) * 0.8f + 0.45f;
        }
        if (!ok || isRecent(texts[0])) continue;

        for (int i = 7; i > 0; --i) lastDialogue_[i] = lastDialogue_[i - 1];
        lastDialogue_[0] = di;
        const int group = dialogueSerial_++;
        for (int i = 0; i < n; ++i) {
            remember(texts[i]);
            Pending p;
            p.group = group;
            p.line.seat = d.l[i].seat;
            p.line.text = texts[i];
            p.line.delay = delays[i];
            p.line.seconds = bubbleSeconds(texts[i]);
            p.line.maxWait = 4.f + 0.5f * (float)i;
            queue_.push_back(p);
            seatCool_[d.l[i].seat] = std::max(seatCool_[d.l[i].seat], kSeatCooldown + delays[i]);
        }
        globalCool_ = std::max(globalCool_, kGlobalGap + delays[n - 1]);
        return;
    }
}

// ---------------------------------------------------------------- time based
void Banter::update(float dt, bool teaLow, bool teaBoyBusy) {
    for (float& c : seatCool_) c = std::max(0.f, c - dt);
    for (float& s : sinceSpoke_) s = std::min(99.f, s + dt);
    globalCool_ = std::max(0.f, globalCool_ - dt);
    teaOrderCool_ = std::max(0.f, teaOrderCool_ - dt);
    for (Pending& p : queue_) p.line.delay -= dt;
    if (!enabled_) return;

    // the human is taking a long time (the clock stands still while the game is paused or behind a menu)
    if (handLive_ && turnSeat_ == 0 && gameRunning_) {
        humanWait_ += dt;
        float threshold = 28.f + 30.f * (float)nagLevel_;
        if (nagLevel_ < 3 && humanWait_ > threshold) {
            if (say(pickBot(), S_WaitHuman, 0.f, true, 0, 0, 4.f)) ++nagLevel_;
            else humanWait_ = threshold - 3.f; // retry a bit later
        }
    }

    // tea orders (the boy answers from wherever he is)
    if (teaLow && !teaBoyBusy && teaOrderCool_ <= 0.f && globalCool_ <= 0.f) {
        int s = pickBot();
        if (say(s, S_TeaOrder, 0.f, false, 0, -1, 4.f, BanterCue::TeaOrder)) {
            teaOrderCool_ = 45.f;
            if (chance(0.6f)) say(4, S_CayciComing, 1.5f + rnd() * 0.8f, true, 0, -1, 4.f);
        } else {
            teaOrderCool_ = 4.f;
        }
    }

    // idle chatter: a single remark, the çaycı's cheek, or a short exchange
    idleIn_ -= dt;
    if (idleIn_ <= 0.f) {
        idleIn_ = 45.f + rnd() * 50.f;
        if (globalCool_ <= 0.f && queue_.empty()) {
            const float r = rnd();
            if (r < 0.42f) startDialogue();
            else if (r < 0.50f) say(4, S_CayciIdle, 0.f, false, 0, -1, 4.f);
            else say(pickBot(), S_Idle, 0.f, false, 0, -1, 4.f);
        }
    }
}

void Banter::teaServed() {
    if (!enabled_) return;
    if (chance(0.4f)) say(4, S_CayciServed, 0.3f, true, 0, -1, 3.f);
    if (chance(0.6f)) say(pickBot(), S_TeaThanks, 1.6f, false, 0, -1, 3.f);
}

bool Banter::orderTea() {
    if (!enabled_ || teaOrderCool_ > 0.f) return false;
    if (!say(pickBot(), S_TeaOrder, 0.f, false, 0, -1, 4.f, BanterCue::TeaOrder)) return false;
    teaOrderCool_ = 40.f;
    if (chance(0.6f)) say(4, S_CayciComing, 1.5f + rnd() * 0.8f, true, 0, -1, 4.f);
    return true;
}

void Banter::tvGoal() {
    if (!enabled_) return;
    if (chance(0.55f)) say(2, S_Goal, 0.3f, false, 0, -1, 2.5f);
    else if (chance(0.3f)) say(pickOther(2), S_Goal, 0.4f, false, 0, -1, 2.5f);
    if (chance(0.12f)) say(4, S_CayciGoal, 1.2f, false, 0, -1, 2.5f);
}

void Banter::catMeow() {
    if (!enabled_ || !chance(0.35f)) return;
    if (chance(0.15f)) say(4, S_Cat, 0.8f, false, 0, -1, 3.f);
    else say(pickBot(), S_Cat, 0.6f, false, 0, -1, 3.f);
}

BanterLine Banter::aiModeLine(bool on, uint32_t pick) {
    struct L {
        int seat;
        const char* text;
    };
    static const L kOn[] = {
        {2, "Abi sen çayını iç, taşlar kendi kendine oynuyor!"},
        {1, "Maşallah, taşlar kendi kendine dizilir oldu evladım."},
        {3, "Bizim zamanımızda okeyi makineye oynatmazdık!"},
        {2, "Makineyle mi oynuyoruz şimdi? Olsun, onu da yeneriz!"},
    };
    static const L kOff[] = {
        {1, "Hoş geldin evladım, taşlar seni bekliyordu."},
        {3, "Hah, nihayet! Makineden bıkmıştım."},
        {2, "Usta geri döndü abi, şimdi oyun başlıyor!"},
    };
    const L& l = on ? kOn[pick % (sizeof kOn / sizeof kOn[0])] : kOff[pick % (sizeof kOff / sizeof kOff[0])];
    BanterLine out;
    out.seat = l.seat;
    out.text = l.text;
    out.seconds = 3.4f;
    return out;
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
    if (seat < 1 || seat > 4) return;
    seatCool_[seat] = std::max(seatCool_[seat], 3.f);
    sinceSpoke_[seat] = 0.f;
}

} // namespace ui
