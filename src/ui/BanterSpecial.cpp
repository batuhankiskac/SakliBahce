// Banter for özel günler and the rain in the garden (ozelgun agent; ui::specialLine in Banter.h). Pure data and a picker:
// the speakers keep their characters (DESIGN.md "Characters"): Hacı Rıza calm and pious with his proverbs, Kel Mahmut the
// loud football fan, Emekli Nuri the grumpy nostalgic retiree, the çaycı short and cheeky-respectful.
#include "ui/Banter.h"

#include <cstddef>

namespace ui {

namespace {

struct L {
    int seat;
    const char* text;
};

// ---- bayram (both); the Kurban lines are added on Kurban Bayramı, the Ramazan Bayramı lines on Ramazan Bayramı
const L kBayramGreet[] = {
    {1, "Bayramınız mübarek olsun beyler, nice bayramlara!"},
    {1, "Hayırlı bayramlar. Gel bakalım evlat, bayram eli öpülür; harçlık yok ama çay benden."},
    {2, "Bayramın kutlu olsun! Lokumdan al, ağzın tatlansın, belki elin de açılır!"},
    {3, "Bayramınız mübarek. Bizim zamanımızda bayram sabahı kapı kapı el öpmeye giderdik."},
    {4, "Bayramınız mübarek abiler! Kolonya ister misiniz? Limon kolonyası, ocaktan."},
    {2, "İyi bayramlar! Bugün kaybeden herkese şeker dağıtır, anlaştık mı?"},
};
const L kBayramChat[] = {
    {3, "Eskiden çocuklar bayramda el öpmeye gelirdi, cebimizde mendil mendil şeker. Şimdi kim gelir?"},
    {1, "Bayram dediğin küslerin barıştığı gündür. Mahmut, sen de Nuri'yle barış bakalım."},
    {2, "Barıştık barıştık! Ama bu el okeyi ben bitiriyorum, orası ayrı."},
    {4, "Lokumlar tazedir abi, bu sabah bakkaldan geldi!"},
    {3, "Bu lokum eskisi gibi değil. Eskiden lokum lokum gibiydi."},
    {1, "Elini öpen gençlere dua etmeyi unutma evlat, bayramın bereketi oradadır."},
    {2, "Bayramlığımı giydim, kravat bile taktım… yok yok, karanfil yeter bana."},
    {4, "Abiler, bayram şekeri tezgâhta, buyurun alın!"},
};
const L kRamazanBayramChat[] = {
    {1, "Bir ay oruç tuttuk, Allah kabul etsin. Bayram da bu ayın ikramı."},
    {2, "Otuz gün sonra öğlen çayı içmek ne güzelmiş be!"},
};
const L kKurbanChat[] = {
    {1, "Kurbanınız kabul olsun. Etinizi komşuya dağıttınız mı?"},
    {2, "Bu akşam kavurma bizde! Oyundan sonra herkes davetli."},
    {3, "Bizim zamanımızda kurbanı bahçede keserdik, mahalle toplanırdı."},
};

// ---- Ramazan evenings
const L kRamazanGreet[] = {
    {1, "Hayırlı iftarlar beyler. Allah oruçlarınızı kabul etsin."},
    {2, "İftarda öyle yedim ki taşları zor tutuyorum! Bir güllaç daha gelsin ama."},
    {3, "Teravihten çıktık, bir el okey iyi gelir. Sahura kadar oturmayalım ama."},
    {4, "Hayırlı iftarlar abiler! Çaylar iftardan beri demde, güllaç da taze."},
};
const L kRamazanChat[] = {
    {2, "Güllaç gelsin! Oruçtan sonra bir tatlı, bir çay… keyif bu işte."},
    {1, "Ramazan akşamları kahvehane başka olur. Herkes iftardan sonra buraya koşar."},
    {3, "Eskiden mahyalar ışıl ışıl olurdu, minareden minareye yazı yazarlardı."},
    {4, "Baklava da var abi, güllaçı beğenmeyene!"},
    {3, "Bu kalabalık sahura kadar dağılmaz, ben söyleyeyim."},
    {1, "Oruç sabrı öğretir. Okeyde de sabır lazım, Mahmut."},
    {2, "Sabır sabır… Ben iftarı bile beş dakika erken bekledim!"},
};
const L kDavul[] = {
    {3, "Davulcu geldi, sahur yaklaşıyor. Bizim zamanımızda mani de söylerdi."},
    {2, "Davulcu bu sene erken başladı! Bahşişi de erken ister bu."},
    {1, "Hayırlı sahurlar. Davulcunun sesi Ramazan'ın sesidir."},
    {4, "Davulcu abi geçiyor, ona da bir çay koyayım mı?"},
};

// ---- the derby (Sunday evening)
const L kDerbyGreet[] = {
    {2, "Bu akşam derbi var! Okeyi çabuk bitirelim, ilk yarıyı kaçırmayalım!"},
    {2, "Bu akşam bizimkiler üç atar, yazın bir kenara!"},
    {4, "Abiler, derbi başlıyor, çaylar televizyonun oraya da geliyor!"},
    {3, "Derbi derbi… Bizim zamanımızda derbiler başkaydı, oyuncular formayı terletirdi."},
};
const L kDerbyChat[] = {
    {1, "Mahmut, derbi var diye taşlarına bakmayı unutma."},
    {2, "Hakem kimmiş bu akşam? Ondan belli olur maç."},
    {3, "Bu kalabalık televizyonun önünden bir çekilse, ben de bir görsem."},
    {4, "Atkıyı televizyonun üstüne ben astım abi, uğur getirir."},
    {2, "Ben bu atkıyı yirmi yıldır takıyorum, yıkamadım, uğurdur!"},
    {1, "Maç da oyun, okey de oyun; ikisine birden yetişilmez."},
};
const L kDerbyGoal[] = {
    {2, "GOOOL! Gördün mü! Ben demedim mi!"},
    {2, "Oh be! Vur vur inlesin!"},
    {3, "Ofsayt o, ofsayt! Hakem görmüyor mu?"},
    {1, "Maşallah… Sakin olun beyler, taşlar dağılacak."},
    {4, "Gol mü oldu abi? Ocakta kaçırdım!"},
};

// ---- the rain in the garden
const L kRainBegins[] = {
    {3, "Damlıyor galiba… Ben demiştim, bu bulutlar boşuna toplanmadı."},
    {1, "Yağmur geliyor, hayırlısı. Bu eli bitirelim, sonra içeri geçeriz."},
    {2, "Yağmur mu başladı? Eli bitirelim, sonra içeri!"},
    {4, "Abiler yağmur başlıyor, el bitince masayı içeri alırız!"},
};
const L kUnderAwning[] = {
    {4, "Tenteyi indirdim abi, ıslanmazsınız!"},
    {2, "Yağmurda bahçe de güzel be, tentenin altında çay başka olur."},
    {3, "Tente tıpırdıyor… Eski kahvenin çinko çatısını hatırlattı."},
    {1, "Yağmur rahmettir. Biz tentenin altında oyunumuza bakalım."},
};
const L kMoveRain[] = {
    {2, "Yağmur başladı, içeri geçelim!"},
    {1, "Yağmur bastırdı, içeri geçelim. Taşları dağıtmayın, masa olduğu gibi gelsin."},
    {4, "Çayları içeri taşıyorum abiler, buyurun içeri!"},
    {3, "Hadi içeri, romatizmam azdı zaten."},
};
const L kMoveDerby[] = {
    {2, "Maç başlıyor, hadi içeri, televizyonun karşısına!"},
    {4, "Abiler derbi başladı, içerideyiz!"},
};
const L kMoveOther[] = {
    {3, "Hava serinledi, içeri geçelim."},
    {1, "Vakit değişti, yerimizi de değiştirelim bakalım."},
    {4, "Masanız hazır abiler, buyurun!"},
};

const L kMoveOut[] = {
    {2, "Hava açtı, hadi bahçeye!"},
    {4, "Masanızı bahçeye kurdum abiler, buyurun!"},
    {1, "Güneş yüzünü gösterdi, çınarın altına geçelim."},
};

template <size_t N>
bool pickFrom(const L (&t)[N], uint32_t pick, int& seat, std::string& text) {
    const L& l = t[pick % N];
    seat = l.seat;
    text = l.text;
    return true;
}

}  // namespace

bool specialLine(int day, SpecialSit sit, uint32_t pick, int& seat, std::string& text) {
    const bool bayram = day == 1 || day == 2;
    switch (sit) {
    case SpecialSit::Greeting:
        if (bayram) return pickFrom(kBayramGreet, pick, seat, text);
        if (day == 3) return pickFrom(kRamazanGreet, pick, seat, text);
        if (day == 4) return pickFrom(kDerbyGreet, pick, seat, text);
        return false;
    case SpecialSit::Chatter:
        if (bayram) {
            // a third of the time a line of this bayram's own
            if (pick % 3 == 2) return day == 2 ? pickFrom(kKurbanChat, pick / 3, seat, text) : pickFrom(kRamazanBayramChat, pick / 3, seat, text);
            return pickFrom(kBayramChat, pick, seat, text);
        }
        if (day == 3) return pickFrom(kRamazanChat, pick, seat, text);
        if (day == 4) return pickFrom(kDerbyChat, pick, seat, text);
        return false;
    case SpecialSit::Davul: return pickFrom(kDavul, pick, seat, text);
    case SpecialSit::Goal: return pickFrom(kDerbyGoal, pick, seat, text);
    case SpecialSit::RainBegins: return pickFrom(kRainBegins, pick, seat, text);
    case SpecialSit::UnderAwning: return pickFrom(kUnderAwning, pick, seat, text);
    case SpecialSit::MoveInRain: return pickFrom(kMoveRain, pick, seat, text);
    case SpecialSit::MoveInDerby: return pickFrom(kMoveDerby, pick, seat, text);
    case SpecialSit::MoveOther: return pickFrom(kMoveOther, pick, seat, text);
    case SpecialSit::MoveOut: return pickFrom(kMoveOut, pick, seat, text);
    }
    return false;
}

}  // namespace ui
