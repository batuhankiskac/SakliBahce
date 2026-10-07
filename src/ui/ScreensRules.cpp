// Screens (screens owner): the rules pages ("Nasıl Oynanır?") — the okey and 101 pages, the other games' pages
// from their embedded docs (RulesText.inc, tools/gen_rules.py), the rich text layout, scrolling and drawing.
#include "ui/ScreensInternal.h"

namespace ui {

namespace screens_detail {

RichText layoutRich(const std::string& src, float width, float size, float lineH) {
    struct Tok {
        std::string s;
        bool bold = false, accent = false, space = false, newline = false;
    };
    std::vector<Tok> toks;
    bool bold = false, accent = false, pendingSpace = false;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        toks.push_back({cur, bold, accent, pendingSpace, false});
        cur.clear();
        pendingSpace = false;
    };
    for (char ch : src) {
        if (ch == '*') {
            flush();
            bold = !bold;
        } else if (ch == '^') {
            flush();
            accent = !accent;
        } else if (ch == ' ') {
            flush();
            pendingSpace = true;
        } else if (ch == '\n') {
            flush();
            toks.push_back({"", bold, accent, false, true});
            pendingSpace = false;
        } else {
            cur += ch;
        }
    }
    flush();

    RichText out;
    const float spaceW = measureText(FontId::Ui, " ", size).x;
    float x = 0.f, y = 0.f;
    bool lineEmpty = true;
    size_t i = 0;
    while (i < toks.size()) {
        if (toks[i].newline) {
            x = 0.f;
            y += lineH;
            lineEmpty = true;
            ++i;
            continue;
        }
        // a word = this token plus following tokens glued to it (no space before them)
        size_t j = i + 1;
        while (j < toks.size() && !toks[j].newline && !toks[j].space) ++j;
        float ww = 0.f;
        for (size_t k = i; k < j; ++k) {
            const FontId f = (toks[k].bold || toks[k].accent) ? FontId::UiBold : FontId::Ui;
            ww += measureText(f, toks[k].s, size).x;
        }
        if (!lineEmpty && x + spaceW + ww > width) {
            x = 0.f;
            y += lineH;
            lineEmpty = true;
        }
        if (!lineEmpty) x += spaceW;
        for (size_t k = i; k < j; ++k) {
            Piece p;
            p.text = toks[k].s;
            p.font = (toks[k].bold || toks[k].accent) ? FontId::UiBold : FontId::Ui;
            p.color = toks[k].accent ? kRedPencil : kInk;
            p.pos = {x, y};
            x += measureText(p.font, p.text, size).x;
            out.pieces.push_back(std::move(p));
        }
        lineEmpty = false;
        i = j;
    }
    out.height = y + lineH;
    return out;
}

std::vector<RuleBlock> buildRules101(bool esli) {
    std::vector<RuleBlock> v;
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto P = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Para; b.text = s; v.push_back(b); };
    auto B = [&](const char* s, const char* label = "") {
        RuleBlock b;
        b.kind = RuleBlock::Bullet;
        b.text = s;
        b.label = label;
        v.push_back(b);
    };
    auto X = [&](std::vector<RuleItem> items) {
        RuleBlock b;
        b.kind = RuleBlock::Tiles;
        b.items = std::move(items);
        v.push_back(b);
    };
    enum { Y = 0, M = 1, K = 2, R = 3 };

    H("Oyunun amacı");
    P("101, dört kişiyle oynanan bir okey oyunudur. Taşlarını seri, grup ya da çift olarak masaya açar, "
      "sonra kalanları işleyerek elini bitirmeye çalışırsın. Her elin puanı hesap kağıdına yazılır ve "
      "*düşük puan iyidir*: maçın sonunda toplamı en düşük olan kazanır.");
    if (esli) {
        H("Eşli oyun");
        P("Eşli 101'de karşında oturan oyuncu *ortağındır*: sen ve Kel Mahmut bir takım, Hacı Rıza ile Emekli Nuri "
          "öbür takım. Kurallar tekli 101'le aynıdır, şu farklarla:");
        B("Ortaklardan biri elini bitirince *ötekinin eli silinir*: o el elinde kalan taşlar sayılmaz, açmamış olsa "
          "bile 202 yazılmaz. Yazılmış cezaları (işlek taş, okey atmak) yine de sayılır.");
        B("Takımın puanı iki ortağın toplamıdır; maçın sonunda *toplamı düşük olan takım* kazanır.");
        B("Attığın taşı her zaman bir rakip alır: sağındaki de solundaki de karşı takımdandır. Ortağının perlerine "
          "de herkesinki gibi taş işleyebilirsin.");
        B("*Yandan açma cezası* karşı takıma yazılır: senin attığın taşla açılırsa ceza sana gelir.");
    }

    H("Taşlar");
    P("Toplam 106 taş vardır: Sarı, Mavi, Siyah ve Kırmızı renklerde 1'den 13'e kadar sayılar, her taştan "
      "iki tane. Bunlara iki *sahte okey* eklenir.");
    X({{{T(Y, 3), T(M, 7), T(K, 11), T(R, 13)}, "Dört renk"}, {{TFake()}, "Sahte okey"}});

    H("Gösterge ve okey");
    P("Taşlar karıldıktan sonra bir taş açılır: bu *gösterge*dir ve oyundan çıkar. Göstergenin aynı renkte "
      "bir üst sayısı o elin *okey*idir (13'ün üstü 1'dir). İki okey taşı da joker gibidir, istediğin taşın "
      "yerine geçer. *Sahte okey* joker değildir; okeyin rengi ve sayısı yerine oynanır.");
    X({{{T(M, 6)}, "Gösterge: Mavi 6"}, {{TOkey()}, "Okey: Mavi 7 (joker)"}, {{TFake()}, "Sahte okey = Mavi 7"}});

    H("Dağıtım");
    P("Eli başlatan oyuncuya 22, diğerlerine 21 taş dağıtılır; kalan 20 taş ortada kapalı durur. Başlayan "
      "oyuncu ilk turunda taş çekmez, doğrudan oynar ve bir taş atar. İlk eli kimin başlatacağını kura "
      "belirler; sonraki her elde bu görev bir sağdaki oyuncuya geçer.");

    H("Sıra ve tur");
    P("Sıra sağa doğru döner: sen, sağındaki, karşındaki, solundaki. Sıra sana geldiğinde:");
    B("*Taş al:* ortadaki desteden bir taş çek ya da solundaki oyuncunun en son attığı taşı al.", "1.");
    B("*Oyna (isteğe bağlı):* elini aç, yeni per indir, masadaki perlere taş işle, okey al.", "2.");
    B("*Taş at:* bir taşı kendi atık yerine at. Bu taşı sağındaki oyuncu alabilir.", "3.");

    H("Yandan taş almak");
    P("Solundan aldığın taşı aynı turda masada kullanmak zorundasın: henüz açmadıysan açılışında, açtıysan "
      "yeni bir perde, işlerken ya da okey alırken. Kullanamazsan *Geri Ver*: taş cezasız yerine döner ve "
      "desteden çekersin. Aynı turda bir daha yandan alamazsın.");
    P("*Yandan açma cezası:* yandan aldığın taşla elini açarsan, o taşı atan oyuncuya taşın sayısının "
      "seri açılışta ^10 katı^, çift açılışta ^20 katı^ ceza yazılır (yandan 7 alıp seriyle açtın: atana 70). "
      "Kahveden kahveye değişen bir kural olduğu için ayarlardan kapatılabilir.");

    H("Perler: seri, grup, çift");
    B("*Seri:* aynı renkten en az 3 ardışık sayı. Seri 13'te biter: normal okeyin aksine 101'de 1, 13'ün "
      "arkasından gelmez (12-13-1 per değildir) ve başa dönülmez (13-1-2 olmaz). 1 yalnızca serinin başında "
      "olur: 1-2-3.");
    {
        std::vector<RuleItem> it = {{{T(R, 5), T(R, 6), T(R, 7)}, "Kırmızı 5-6-7"},
                                    {{T(M, 12), T(M, 13), T(M, 1)}, "12-13-1 olmaz"},
                                    {{T(K, 13), T(K, 1), T(K, 2)}, "13-1-2 olmaz"}};
        it[1].wrong = true;
        it[2].wrong = true;
        X(it);
    }
    B("*Grup:* aynı sayının farklı renkleri, 3 ya da 4 taş. Bir renk iki kez kullanılamaz.");
    X({{{T(Y, 9), T(M, 9), T(K, 9)}, "Üçlü grup"}, {{T(Y, 9), T(M, 9), T(K, 9), T(R, 9)}, "Dörtlü grup"}});
    B("*Çift:* birebir aynı iki taş (aynı renk, aynı sayı).");
    X({{{T(K, 4), T(K, 4)}, "Siyah 4 çifti"}, {{T(Y, 11), T(Y, 11)}, "Sarı 11 çifti"}});
    B("*Okey* eksik taşın yerine geçer ve yerine geçtiği sayı kadar değerlidir.");
    X({{{T(R, 5), TOkey(), T(R, 7)}, "Kırmızı 5, okey, 7 = 18 puan"}});

    H("Elini açmak");
    P("Masaya ilk kez per indirmeye *el açmak* denir. İki yolu vardır:");
    B("*Seriyle açmak:* indirdiğin seri ve grupların toplamı en az ^101^ olmalı. Toplam, taşların üzerindeki "
      "sayılarla hesaplanır; okey, yerine geçtiği sayı kadar sayılır.");
    B("*Çiftle açmak:* en az ^5 çift^ indirerek.");
    X({{{T(R, 10), T(R, 11), T(R, 12), T(R, 13)}, "46"},
       {{T(Y, 12), T(M, 12), T(K, 12)}, "36"},
       {{T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}, "26"}});
    P("46 + 36 + 26 = 108, yani 101'i geçiyor: bu açılış geçerli. Açılışta seri ile çift karıştırılamaz. "
      "Açarken istediğin kadar per indirebilirsin, ama açtığın turda işleyemez, yeni per indiremez, okey "
      "alamazsın; bunlar için bir sonraki turunu beklersin. Taş atabilmek için elinde her zaman en az bir "
      "taş kalmalı.");
    P("Çiftle açan sonradan yeni seri indiremez, yalnızca çift indirebilir. Seriyle açan sonradan seri ve "
      "grup indirir; masada çiftle açmış başka biri varsa elindeki çiftleri de indirebilir.");

    H("Katlamalı oyun");
    P("Ayarlar'dan açılan bir oyun türüdür. Katlamalı oyunda senden önce biri seriyle açtıysa, sen onun "
      "toplamından *en az 1 fazlasıyla* açmak zorundasın: önceki açılış 116 ise sana en az ^117^ gerekir, "
      "sen 128'le açarsan sonrakine 129 gerekir. Çiftte de aynısı: önce biri 5 çiftle açtıysa sana en az "
      "^6 çift^ gerekir. Seri ve çift ayrı sayılır. Masadaki sayaç o anki gereken sayıyı gösterir.");

    H("İşlemek ve okey almak");
    P("Elini açtıktan sonraki turlarında masadaki seri ve gruplara, kimin olursa olsun, taş ekleyebilirsin: "
      "serinin başına ya da sonuna, gruba da eksik renk olarak (en fazla 4 taş). Buna *işlemek* denir. "
      "Çiftlere taş işlenmez.");
    X({{{T(R, 5), T(R, 6), T(R, 7), TM(R, 8)}, "Kırmızı 8 işlenir"},
       {{T(Y, 9), T(M, 9), T(K, 9), TM(R, 9)}, "Kırmızı 9 işlenir"}});
    P("Masadaki bir seri ya da grupta okey varsa, onun yerine geçtiği gerçek taşı koyup okeyi eline "
      "alabilirsin (*okey almak*). Üç taşlı bir grupta eksik renklerin herhangi biri okeyin yerine konabilir.");
    X({{{T(R, 5), TOkey(), T(R, 7)}, "Önce"}, {{T(R, 5), TM(R, 6), T(R, 7)}, "Kırmızı 6'yı koy, okeyi al"}});

    H("Cezalar");
    P("Aşağıdakilerin her biri için o elin puanına ^101 ceza^ eklenir:");
    B("Okey atmak (elini bitirdiğin son taş hariç).");
    B("*İşlek taş* atmak: masadaki bir seri ya da gruba işlenebilecek bir taşı atmak (son taş hariç). "
      "Okey attığında yalnızca okey cezası yazılır.");
    P("*Yandan açma cezası* (ayarlardan kapatılabilir): attığın taşı sağındaki oyuncu alıp onunla elini açarsa "
      "taşın sayısının ^10 katı^ (çiftle açtıysa ^20 katı^) sana yazılır.");

    H("El sonu");
    P("Elindeki son taşı atan oyuncu eli *bitirir*. Ortadaki taşlar tükenirse, son taş çekilip atıldığında el "
      "kimse bitirmeden sona erer.");

    H("Puanlama");
    P("Her elin sonunda herkesin puanı hesap kağıdına yazılır. Düşük puan iyidir.");
    B("Eli bitiren: *\xE2\x80\x93" "101*");
    B("Hiç açmayan: *202*");
    B("Seriyle açan: elinde kalan taşların toplamı");
    B("Çiftle açan: elinde kalanın *iki katı*");
    B("Açmış bir oyuncunun elinde okey kaldıysa her okey için ^101 ceza^");
    B("Bunlara o elin cezaları eklenir.");
    P("*Katlar:* Bitiren oyuncu son taş olarak okeyi attıysa (*okeyle bitiş*), çiftle açmışsa (*çiftten "
      "bitiş*) ya da henüz kimse açmamışken bütün elini tek seferde açıp bitirdiyse (*elden bitiş*) diğer "
      "oyuncuların puanları ikiye katlanır, bitirenin puanı da (\xE2\x80\x93" "202); cezalar katlanmaz. Katlar "
      "birbiriyle çarpılır: okeyle ve elden birlikte \xC3\x97" "4 olur. Taşlar tükenerek biten elde kat yoktur.");

    // 101 kuralları: the variants of Ayarlar "101 kuralları" (docs/kurallar_101.md)
    H("Masadan masaya değişenler");
    P("101 her kahvede biraz farklı oynanır. Ayarlar'daki *101 kuralları* sayfasından masanın usulünü "
      "seçebilirsin; seçimler yeni maçta geçerli olur, yarım kalan maç ve tekrarlar kendi kurallarıyla açılır. "
      "Yukarıda yazılanlar varsayılan usuldür.");
    B("*Açma sınırı:* seriyle açmak için gereken toplam ^51^, ^81^, ^101^ (varsayılan) ya da ^121^.");
    B("*Katlamalı oyun:* her açan, aynı yoldan açmış olanın bir fazlasıyla açar (yukarıda).");
    B("*Bitiş katları:* *Katlanır* (varsayılan): okeyle, çiftten ve elden bitişin her biri \xC3\x97" "2'dir ve "
      "çarpılır; okeyle ve çiftten birlikte \xC3\x97" "4, üçü birden \xC3\x97" "8. *Tek kat:* kaç kat olursa olsun "
      "en çok \xC3\x97" "2. *Katsız:* kat yoktur, bitiren her zaman \xE2\x80\x93" "101 yazar. Çiftle açanın "
      "elinde kalanın iki katı her usulde geçerlidir.");
    B("*Açmayan yazar:* elini hiç açamayan ^202^ (varsayılan) ya da ^404^ yazar; katlar bunu da çarpar.");
    B("*Okey atma cezası* ve *işlek taş cezası* (ikisi de varsayılan açık) kapatılabilir.");
    B("*Geri verme cezası* (varsayılan kapalı): yandan alıp kullanamadığın taşı geri verirsen ^101 ceza^.");
    B("*Açınca bir tur bekle* (varsayılan açık): kapalıysa açtığın turda işleyebilir, per ekleyebilir, okey "
      "alabilirsin.");
    P("Hesap kağıdı o elin katını kırmızı kalemle yazar (\xC3\x97" "2, \xC3\x97" "4); geçmiş ellerin katı da "
      "satırının yanında durur.");

    H("Maçın sonu");
    P("Maç, ayarlardan seçtiğin el sayısı kadar (1 ile 11 arası) sürer. Son elden sonra toplam puanı en düşük "
      "olan maçı kazanır. En düşük toplam birden fazla oyuncudaysa birincilik paylaşılır.");

    H("Kontroller");
    B("*İpucu:* takılırsan *İpucu* düğmesi (ya da *H*) Kurt'un senin yerinde ne yapacağını gösterir.");
    B("*Etrafa bakmak:* farenin sağ tuşunu basılı tutup sürükle. *Fare tekerleği* yakınlaştırır, *R* ya da sağ "
      "tuşa çift tıklamak bakışını yeniden masaya ortalar.");
    B("*Istaka:* taşları sürükleyerek 2 sıra \xC3\x97 16 yuvaya dilediğin gibi diz. Yan yana duran taşlar "
      "bir grup sayılır; araya boşluk bırakınca gruplar ayrılır.");
    B("*Taş çekmek:* ortadaki desteye ya da sol alttaki atık taşına tıkla veya onu ıstakana sürükle.");
    B("*Taş atmak:* taşı sağ alttaki atık yerine sürükle ya da taşa çift tıkla.");
    B("*El Aç / Per Aç:* ıstakadaki grupları masaya indirir. İpuçları açıkken geçerli gruplar parlar ve bir "
      "sayaç gösterilir: \xE2\x80\x9CSeri 87/101 \xC2\xB7 Çift 3/5\xE2\x80\x9D.");
    B("*İşlemek:* taşı masadaki bir perin üzerine sürükle; sol yarısına bırakırsan başa, sağ yarısına "
      "bırakırsan sona eklenir. Perdeki okeyin üzerine bırakırsan okeyi almayı dener.");
    B("*Seri Diz / Çift Diz:* ıstakanı seri ya da çift düzenine göre kendiliğinden dizer. Kısayollar: *S* "
      "Seri Diz, *C* Çift Diz, *Enter* El Aç / Per Aç.");
    B("Masanın uzak ucundaki bir perin ya da bir atık yığınının üzerinde fareyle biraz beklersen büyütülmüş "
      "hâli açılır.");
    B("*Geri Ver:* yandan aldığın taşı cezasız geri verir; sonra desteden çekersin.");
    B("İşlek taş ya da okey atarken oyun seni uyarır ve onay ister.");
    B("Okey taşlarının köşesinde küçük bir yıldız bulunur.");
    B("İpuçları açıkken *işlek* taşların köşesinde yeşil bir *+* görünür: bunları atarsan ^101 ceza^ yersin.");
    B("*Yapay Zeka:* *Y* tuşu ya da *Yapay Zeka* düğmesi taşlarını yapay zekaya bırakır; o senin yerine "
      "oynar, sen izlersin. Yeniden basınca kontrol, turun kaldığı yerden sana döner.");
    B("*ESC* ya da *Menü* düğmesi oyunu duraklatır; menülerde bir önceki ekrana döner.");
    return v;
}

std::vector<RuleBlock> buildRulesOkey() {
    std::vector<RuleBlock> v;
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto P = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Para; b.text = s; v.push_back(b); };
    auto B = [&](const char* s, const char* label = "") {
        RuleBlock b;
        b.kind = RuleBlock::Bullet;
        b.text = s;
        b.label = label;
        v.push_back(b);
    };
    auto X = [&](std::vector<RuleItem> items) {
        RuleBlock b;
        b.kind = RuleBlock::Tiles;
        b.items = std::move(items);
        v.push_back(b);
    };
    enum { Y = 0, M = 1, K = 2, R = 3 };

    H("Oyunun amacı");
    P("Okey, kahvenin en çok oynanan oyunudur. Elindeki 14 taşı *per*lere (seri ya da grup) ya da *yedi çifte* "
      "diz; sıran gelince bir taş çek, fazla taşı atıp elini göster: *bittin*. 101'deki gibi masaya per açılmaz, "
      "el bitene kadar taşlar ıstakanda kalır.");

    H("Taşlar ve okey");
    P("106 taşla oynanır: dört renkte 1'den 13'e kadar her taştan iki tane ve iki *sahte okey*. Dağıtımdan "
      "sonra açılan taş *gösterge*dir; aynı renkte bir üst sayısı o elin *okey*idir ve her taşın yerine geçer. "
      "Sahte okey joker değildir, okeyin rengi ve sayısı yerine oynanır.");
    X({{{T(M, 6)}, "Gösterge: Mavi 6"}, {{TOkey()}, "Okey: Mavi 7 (joker)"}, {{TFake()}, "Sahte okey = Mavi 7"}});

    H("Dağıtım ve tur");
    P("Herkese 14 taş dağıtılır; eli başlatan oyuncu 15 taşla başlar, ilk turunda taş çekmeden bir taş atar. "
      "Sıra sağa döner. Sıran gelince:");
    B("*Taş al:* ortadan bir taş çek ya da solundakinin attığı son taşı al. 101'den farklı olarak yandan aldığın "
      "taşı kullanma zorunluluğu yoktur.", "1.");
    B("*Taş at:* bir taşı kendi atık yerine at. Sağındaki oyuncu onu alabilir.", "2.");

    H("Perler");
    B("*Seri:* aynı renkten en az 3 ardışık sayı. Okeyde 1, 13'ün arkasından da gelebilir (*12-13-1*), ama "
      "serinin son taşı olur: 13-1-2 olmaz.");
    {
        std::vector<RuleItem> it = {{{T(R, 5), T(R, 6), T(R, 7)}, "Kırmızı 5-6-7"},
                                    {{T(M, 12), T(M, 13), T(M, 1)}, "12-13-1 olur"},
                                    {{T(K, 13), T(K, 1), T(K, 2)}, "13-1-2 olmaz"}};
        it[2].wrong = true;
        X(it);
    }
    B("*Grup:* aynı sayının farklı renkleri, 3 ya da 4 taş.");
    X({{{T(Y, 9), T(M, 9), T(K, 9)}, "Üçlü grup"}, {{T(R, 5), TOkey(), T(R, 7)}, "Okey eksiği tamamlar"}});
    B("*Çift:* birebir aynı iki taş. Elinde *yedi çift* toplarsan çiftten bitersin (*çifte gitmek*).");

    H("Bitmek");
    P("Taşını çektikten sonra elindeki 15 taştan 14'ü perlere ya da yedi çifte oturuyorsa, fazla taşı atıp "
      "elini gösterirsin: *bittin*. Masada bunu yapmak için taşı ortaya, *Bitir* yerine sürükle (ya da taşı "
      "seçip *Bitir*'e bas). Oyun ıstakanı kendisi kontrol eder; perlerin dizili olmasa da olur.");
    P("Elin bittiği hâlde okeyi fazla taş olarak atabiliyorsan *okey atarak* bitersin (*okeye dönmek*): "
      "puan iki katına çıkar.");

    H("Gösterge");
    P("Elinde göstergenin eşi (aynı renk ve sayıdaki öbür taş) varsa, ilk taşını atmadan önce onu "
      "gösterebilirsin: *Göster* düğmesine bas. Diğer herkesin puanından ^1^ düşülür. Bir elde gösterge bir "
      "kez gösterilir.");

    H("Puanlama");
    P("Herkes oyuna aynı puanla başlar (ayarlardan *6, 12 ya da 20*; çoğu kahvede 20). Puanlar geriye sayılır, "
      "*yüksek puan iyidir*:");
    B("Biri bitince diğer üç oyuncunun puanından ^2^ düşülür.");
    B("*Okey atarak* bitişte ^4^, *çiftten* bitişte ^4^, ikisi birden olursa ^8^ düşülür.");
    B("Gösterge gösterilince diğerlerinden ^1^ düşülür.");
    B("Ortadaki taşlar biter ve kimse bitemezse el berabere biter; yalnızca gösterge sayılır.");
    B("*Renkli okey* (ayarlardan): gösterge kırmızı ya da siyahsa o elde bütün puanlar iki katına çıkar.");
    P("Puanı sıfıra (ya da altına) inen biri olunca oyun biter; *en yüksek puanda kalan* kazanır.");

    H("Kontroller");
    B("*Taş çekmek:* ortadaki desteye ya da sol alttaki atık taşına tıkla veya onu ıstakana sürükle.");
    B("*Taş atmak:* taşı sağ alttaki atık yerine sürükle ya da taşa çift tıkla.");
    B("*Bitmek:* fazla taşı masanın ortasına (*Bitir* yazan yere) sürükle. El bitmeye hazırsa ipucu açıkken "
      "*Bitir* parlar.");
    B("*Seri Diz / Çift Diz:* ıstakanı seri ya da çift düzenine göre kendiliğinden dizer (*S* / *C*).");
    B("*Göster:* göstergenin eşini gösterir (yalnızca ilk taşını atmadan önce).");
    B("*İpucu:* Kurt'un senin yerinde ne yapacağını gösterir (*H*).");
    B("*Etrafa bakmak:* sağ tuşla sürükle; *Y* yapay zekaya bırakır, *ESC* duraklatır.");
    return v;
}

#include "ui/RulesText.inc"

// The other games' rules pages, from their embedded docs (light markdown, see tools/gen_rules.py).
std::vector<RuleBlock> parseRulesMd(const char* md) {
    std::vector<RuleBlock> v;
    std::string para;
    auto bold = [](std::string t) { // **x** -> *x* (the rich text's bold)
        size_t p;
        while ((p = t.find("**")) != std::string::npos) t.replace(p, 2, "*");
        return t;
    };
    auto flush = [&]() {
        if (para.empty()) return;
        RuleBlock b;
        b.kind = RuleBlock::Para;
        b.text = bold(para);
        v.push_back(b);
        para.clear();
    };
    std::string line;
    const std::string src = md;
    size_t pos = 0;
    bool tableHeader = true;
    while (pos <= src.size()) {
        const size_t nl = src.find('\n', pos);
        line = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? src.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        if (line.empty()) {
            flush();
            tableHeader = true;
            continue;
        }
        if (line.rfind("## ", 0) == 0) {
            flush();
            RuleBlock b;
            b.kind = RuleBlock::Heading;
            b.text = line.substr(3);
            v.push_back(b);
            continue;
        }
        if (line[0] == '|') {
            flush();
            std::vector<std::string> cells;
            size_t a = 1;
            while (a < line.size()) {
                const size_t e = line.find('|', a);
                if (e == std::string::npos) break;
                std::string c = line.substr(a, e - a);
                while (!c.empty() && c.front() == ' ') c.erase(c.begin());
                while (!c.empty() && c.back() == ' ') c.pop_back();
                cells.push_back(c);
                a = e + 1;
            }
            const bool sep = !cells.empty() && cells[0].find_first_not_of("-: ") == std::string::npos;
            if (sep || tableHeader) { // the header row and the |---| row
                tableHeader = false;
                continue;
            }
            if (cells.size() >= 2) {
                RuleBlock b;
                b.kind = RuleBlock::Bullet;
                b.text = "*" + bold(cells[0]) + "*: " + bold(cells[1]);
                for (size_t i = 2; i < cells.size(); ++i) b.text += " \xC2\xB7 " + bold(cells[i]);
                v.push_back(b);
            }
            continue;
        }
        size_t digits = 0;
        while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
        if (line.rfind("- ", 0) == 0 || (digits > 0 && line.compare(digits, 2, ". ") == 0)) {
            flush();
            RuleBlock b;
            b.kind = RuleBlock::Bullet;
            if (line[0] == '-') {
                b.text = bold(line.substr(2));
            } else {
                b.label = line.substr(0, digits + 1);
                b.text = bold(line.substr(digits + 2));
            }
            v.push_back(b);
            continue;
        }
        if (!v.empty() && v.back().kind == RuleBlock::Bullet && para.empty() && line[0] == ' ') { // a bullet's 2nd line
            v.back().text += " " + bold(line.substr(line.find_first_not_of(' ')));
            continue;
        }
        para += (para.empty() ? "" : " ") + line;
    }
    flush();
    return v;
}

void addControls(std::vector<RuleBlock>& v, GameKind g) {
    auto H = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Heading; b.text = s; v.push_back(b); };
    auto B = [&](const char* s) { RuleBlock b; b.kind = RuleBlock::Bullet; b.text = s; v.push_back(b); };
    H("Kontroller");
    if (g == GameKind::Tavla) {
        B("*Zar At* düğmesi (ya da *boşluk*) zarları atar; ilk elde kimin başlayacağını da böyle belirlersiniz.");
        B("Oynamak için önce pulunun durduğu haneye tıkla, sonra yeşil yanan haneye. Kırık pulun varsa önce o seçilir.");
        B("Pul toplarken sağdaki toplama alanına tıkla. Yanlış oynadıysan *Geri Al* (ya da *geri tuşu*) son hamleni geri alır.");
        B("Sağ tık seçimi bırakır. İpuçları açıkken oynayabileceğin pullar parlar, pip sayıları da altta görünür.");
    } else if (g == GameKind::Dama) { // Dama
        B("Önce oynatacağın taşa tıkla, sonra yeşil yanan kareye; ya da taşı tutup oraya sürükle. Sağ tık seçimi bırakır.");
        B("Almak zorundaysan yalnızca alabilen taşların parlar; birkaç taş alan bir yolda taş son kareye kadar kendisi gider. "
          "Gösterdiğin yolun alacağı taşların altı kırmızı yanar.");
        B("Klavyeyle: *sol / sağ ok* taşı, *yukarı / aşağı ok* gideceği yeri seçer, *Enter* ya da *boşluk* oynar.");
        B("*İpucu* (ya da *H*) Kurt'un senin yerinde ne oynayacağını gösterir; *Hamleler* oyunun kaydını açar.");
    } else if (g == GameKind::Konken) { // Konken
        B("Çekmek için ortadaki desteye ya da yerdeki kâğıda tıkla (*D* desteden, *A* yerden).");
        B("Kâğıtlarını yelpazede sürükleyerek istediğin gibi diz; *Diz* düğmesi perleri başa, ikinci basış renge göre dizer.");
        B("Açmak için per yapacak kâğıtlara tıklayıp işaretle (*boşluk*), sonra *Aç* (*O*). Durum satırı açışın kaçta olduğunu gösterir.");
        B("İşlemek için kâğıdı masadaki bir perin üzerine sürükle: sol yarısı başa, sağ yarısı sona; jokerin üzerine bırakırsan jokeri alırsın (*I*: ilk uyduğu pere).");
        B("Atmak için kâğıdı yerdeki kâğıtların üstüne sürükle, kâğıda çift tıkla ya da *Enter*. Yerden aldığın kâğıdı kullanamazsan *Geri Ver* (*G*).");
        B("Klavyeyle *sol / sağ ok* kâğıt seçer, *Shift + ok* seçili kâğıdı yelpazede taşır. Masadaki bir perin üstünde durunca adı yazılır.");
    } else {
        B("Sıra sende olunca oynayabileceğin kâğıtlar parlak, oynayamayacakların soluk görünür. Kâğıdın üstüne gelip tıkla.");
        if (g == GameKind::Batak) B("İhalede ortada açılan panelden sayını ya da *Pas*'ı, ihaleyi alınca kozu seç.");
        if (g == GameKind::King) B("Seçme sırası sendeyken ortadaki panelden oyunu seç; koz seçersen hangi rengin koz olacağını da sorar.");
        if (g == GameKind::Altmisalti) { // Altmışaltı
            B("Ele başlarken *Kapat* düğmesi desteyi kapatır; *Kozu Al* (ya da parlayan açık koza tıklamak) koz dokuzuyla açık kozu alır.");
            B("Eşi elindeyken Kızı ya da Papazı açınca evlilik kendiliğinden söylenir; durum satırı puanını gösterir.");
        }
        if (g == GameKind::Bezik) { // Bezik
            B("Deklarasyonlarını önündeki açık sıradan da oynayabilirsin: kâğıda tıklaman yeter.");
            B("El alınca ortada bir panel açılır: koz 7'si, söyleyebileceğin deklarasyonlar ve *Geç*. Kâğıtları kendin seçmezsin, en uygun kâğıtlar seçilir.");
        }
        B("Masaya atılan kâğıtlar bir an ortada kalır, sonra eli alanın önüne gider.");
    }
    B("*Yapay Zeka* düğmesi ya da *Y* seni yapay zekaya bırakır; *ESC* ya da *Menü* oyunu duraklatır.");
}

std::vector<RuleBlock> buildRules(GameKind g) {
    switch (g) {
    case GameKind::Tavla:
    case GameKind::Pisti:
    case GameKind::Batak:
    case GameKind::King: {
        const char* md = g == GameKind::Tavla ? kRules_tavla : g == GameKind::Pisti ? kRules_pisti
                         : g == GameKind::Batak ? kRules_batak : kRules_king;
        std::vector<RuleBlock> v = parseRulesMd(md);
        addControls(v, g);
        return v;
    }
    case GameKind::Altmisalti: { // Altmışaltı: docs/kurallar_altmisalti.md
        std::vector<RuleBlock> v = parseRulesMd(kRules_altmisalti);
        addControls(v, g);
        return v;
    }
    case GameKind::Bezik: { // Bezik: docs/kurallar_bezik.md
        std::vector<RuleBlock> v = parseRulesMd(kRules_bezik);
        addControls(v, g);
        return v;
    }
    case GameKind::Dama: { // Dama: docs/kurallar_dama.md
        std::vector<RuleBlock> v = parseRulesMd(kRules_dama);
        addControls(v, g);
        return v;
    }
    case GameKind::Konken: { // Konken: docs/kurallar_konken.md
        std::vector<RuleBlock> v = parseRulesMd(kRules_konken);
        addControls(v, g);
        return v;
    }
    case GameKind::Okey: return buildRulesOkey();
    case GameKind::YuzbirEsli: return buildRules101(true);
    default: return buildRules101(false);
    }
}

std::vector<RuleTab> ruleTabsFor(GameKind g) {
    if (g == GameKind::Tavla || g == GameKind::Pisti || g == GameKind::Batak || g == GameKind::King ||
        g == GameKind::Altmisalti || g == GameKind::Bezik || g == GameKind::Dama /* Dama */ ||
        g == GameKind::Konken /* Konken */) {
        static const char* const shortNames[][2] = {
            {"Birinci bölüm: deste bitene kadar", "1. bölüm"}, {"İkinci bölüm: son 8 el", "2. bölüm"}, // Bezik
            {"Deste ve dağıtım", "Dağıtım"}, // Bezik
            {"Masadan masaya değişenler", "Farklar"}, {"Kahvehane usulleri (değişebilenler)", "Usuller"},
            {"Kapı, açık pul, kırık pul", "Kırık pul"}, {"Zarların adları", "Zar adları"}, {"Dağıtım ve ihale", "İhale"},
            {"Koz ve ilk el", "Koz"}, {"Oyun kuralları", "Kurallar"}, {"Kimlerle oynanır?", "Kimlerle"},
            {"Kim başlar?", "Başlangıç"}, {"Kim seçer?", "Kim seçer"}, {"Puanlar ve maç", "Puanlar"},
            {"Diziliş ve yön", "Diziliş"}, {"Zarı oynamak", "Zar"}, {"Pul toplamak", "Toplamak"},
            {"Cezalar (12 el)", "Cezalar"}, {"Koz (8 el)", "Koz"}, {"Elde oyun", "Oyun"}, {"Oyunun sonu", "Son"},
            {"Eşli batak", "Eşli"}};
        std::vector<RuleTab> tabs;
        for (const RuleBlock& b : buildRules(g)) {
            if (b.kind != RuleBlock::Heading) continue;
            std::string label = b.text;
            for (const auto& sn : shortNames)
                if (b.text == sn[0]) label = sn[1];
            if (utf8Count(label) > 14) label = label.substr(0, label.find(' '));
            tabs.push_back({label, b.text});
        }
        if (g == GameKind::Tavla) // Tavla çeşitleri: Gülbahar and Fevga get tabs; these short ones read on under the tab before
            tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [](const RuleTab& t) {
                           return t.heading == "Kim başlar?" || t.heading == "Katlama zarı" ||
                                  t.heading == "Hamleler ve ipucu" || t.heading == "Zarların adları";
                       }), tabs.end());
        if (tabs.size() > 8) tabs.erase(tabs.begin() + 7, tabs.end() - 1); // keep the controls tab
        return tabs;
    }
    switch (g) {
    case GameKind::Okey:
        return {{"Taşlar", "Taşlar ve okey"}, {"Tur", "Dağıtım ve tur"}, {"Perler", "Perler"}, {"Bitmek", "Bitmek"},
                {"Gösterge", "Gösterge"},    {"Puanlama", "Puanlama"},     {"Kontroller", "Kontroller"}};
    // (101 kuralları: "Usuller" jumps to the variants; "Taşlar" gave its place, it is the top of the page anyway)
    case GameKind::YuzbirEsli:
        return {{"Eşli", "Eşli oyun"},   {"Tur", "Sıra ve tur"},
                {"Perler", "Perler: seri, grup, çift"}, {"El açmak", "Elini açmak"}, {"Cezalar", "Cezalar"},
                {"Puanlama", "Puanlama"}, {"Usuller", "Masadan masaya değişenler"}, {"Kontroller", "Kontroller"}};
    default:
        return {{"Okey", "Gösterge ve okey"}, {"Tur", "Sıra ve tur"},
                {"Perler", "Perler: seri, grup, çift"}, {"El açmak", "Elini açmak"}, {"Cezalar", "Cezalar"},
                {"Puanlama", "Puanlama"}, {"Usuller", "Masadan masaya değişenler"}, {"Kontroller", "Kontroller"}};
    }
}

} // namespace screens_detail

// ============================================================================ Screens::Impl: rules

void Screens::Impl::ensureRulesLayout() {
    const int game = std::clamp(settings.game, 0, (int)GameKind::Count - 1);
    if (rulesLaidOut && rulesGame == game) return;
    rulesGame = game;
    rules = buildRules((GameKind)game);
    tabs = ruleTabsFor((GameKind)game);
    tabY.assign(tabs.size(), 0.f);
    const float width = L::RulesView.width;
    const float size = 21.f, lineH = 29.f;
    float y = 6.f;
    for (size_t i = 0; i < rules.size(); ++i) {
        RuleBlock& b = rules[i];
        switch (b.kind) {
        case RuleBlock::Heading:
            if (i > 0) y += 22.f;
            b.y = y;
            b.h = 42.f;
            for (size_t t = 0; t < tabs.size(); ++t)
                if (b.text == tabs[t].heading) tabY[t] = y;
            y += b.h + 6.f;
            break;
        case RuleBlock::Para:
            if (i > 0 && rules[i - 1].kind == RuleBlock::Bullet) y += 8.f;
            b.rich = layoutRich(b.text, width, size, lineH);
            b.y = y;
            b.h = b.rich.height;
            y += b.h + 8.f;
            break;
        case RuleBlock::Bullet:
            b.rich = layoutRich(b.text, width - 34.f, size, lineH);
            b.y = y;
            b.h = b.rich.height;
            y += b.h + 4.f;
            break;
        case RuleBlock::Tiles: {
            float x = 34.f, rowY = 4.f, rowH = 0.f;
            for (RuleItem& it : b.items) {
                const float n = (float)it.tiles.size();
                const float tilesW = n * kMiniW + (n - 1.f) * kMiniGap;
                const float capW = measureText(FontId::Ui, it.caption, 17.f).x;
                const float w = std::max(tilesW, capW);
                const float h = kMiniH + 30.f;
                if (x > 34.f && x + w > width) {
                    x = 34.f;
                    rowY += rowH + 10.f;
                    rowH = 0.f;
                }
                it.box = {x, rowY, w, h};
                x += w + 38.f;
                rowH = std::max(rowH, h);
            }
            b.y = y;
            b.h = rowY + rowH + 4.f;
            y += b.h + 10.f;
            break;
        }
        }
    }
    rulesContentH = y + 24.f;
    rulesLaidOut = true;
}

Rectangle Screens::Impl::thumbRect() const {
    const Rectangle tr = L::RulesTrack;
    const float ms = maxScroll();
    const float frac = rulesContentH > 0.f ? std::min(1.f, L::RulesView.height / rulesContentH) : 1.f;
    const float th = std::max(48.f, tr.height * frac);
    const float t = ms > 0.f ? scroll / ms : 0.f;
    return {tr.x - 3.f, tr.y + (tr.height - th) * t, tr.width + 6.f, th};
}

void Screens::Impl::updateRules(float dt, Vector2 m) {
    if (!rulesLaidOut) return;
    const float ms = maxScroll();
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.f) scrollTarget -= wheel * 90.f;
    const float page = L::RulesView.height * 0.85f;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) scrollTarget += 70.f;
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) scrollTarget -= 70.f;
    if (IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) scrollTarget += page;
    if (IsKeyPressed(KEY_PAGE_UP) || IsKeyPressedRepeat(KEY_PAGE_UP)) scrollTarget -= page;
    if (IsKeyPressed(KEY_HOME)) scrollTarget = 0.f;
    if (IsKeyPressed(KEY_END)) scrollTarget = ms;

    const Rectangle thumb = thumbRect();
    const Rectangle track{L::RulesTrack.x - 8.f, L::RulesTrack.y, L::RulesTrack.width + 16.f, L::RulesTrack.height};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ms > 0.f) {
        if (pointInRect(m, thumb)) {
            draggingThumb = true;
            dragOffset = m.y - thumb.y;
        } else if (pointInRect(m, track)) {
            scrollTarget += (m.y < thumb.y ? -page : page);
        }
    }
    if (draggingThumb) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const float range = L::RulesTrack.height - thumb.height;
            const float t = range > 0.f ? (m.y - dragOffset - L::RulesTrack.y) / range : 0.f;
            scrollTarget = clamp01(t) * ms;
            scroll = scrollTarget;
        } else {
            draggingThumb = false;
        }
    }
    scrollTarget = std::clamp(scrollTarget, 0.f, ms);
    scroll = approach(scroll, scrollTarget, 16.f, dt);
    if (std::fabs(scroll - scrollTarget) < 0.25f) scroll = scrollTarget;
}

void Screens::Impl::drawRules(Vector2 m) {
    ensureRulesLayout();
    drawDim(0.6f);
    drawPanel(L::RulesPanel, PanelStyle::Wood);
    drawHeader(std::string(gameInfo((GameKind)std::clamp(settings.game, 0, (int)GameKind::Count - 1)).name) +
                   " Nasıl Oynanır?",
               {800.f, 56.f}, 46.f, kGold);
    drawRuleTabs(m);
    const Rectangle S = L::RulesSheet;
    drawPanel(S, PanelStyle::Paper);

    const Rectangle V = L::RulesView;
    beginClip({S.x + 2.f, S.y + 2.f, S.width - 4.f, S.height - 4.f});
    for (const RuleBlock& b : rules) {
        const float top = V.y + b.y - scroll;
        if (top + b.h < S.y - 20.f || top > S.y + S.height + 20.f) continue;
        drawRuleBlock(b, V.x, top);
    }
    EndScissorMode();
    // soft fade at the sheet's top and bottom edges
    const Color paper0 = alphaMul(pal::Paper, 0.f);
    DrawRectangleGradientV((int)S.x + 2, (int)S.y + 1, (int)S.width - 34, 22, pal::Paper, paper0);
    DrawRectangleGradientV((int)S.x + 2, (int)(S.y + S.height - 23.f), (int)S.width - 34, 22, paper0, pal::Paper);

    // scrollbar
    if (maxScroll() > 0.f) {
        const Rectangle tr = L::RulesTrack;
        DrawRectangleRounded(tr, 1.f, 8, rgba(120, 100, 80, 0.22f));
        const Rectangle th = thumbRect();
        const bool hover = pointInRect(m, th) || draggingThumb;
        if (hover) requestHandCursor();
        DrawRectangleRounded(th, 1.f, 8, hover ? pal::Wood : alphaMul(pal::WoodLight, 0.85f));
        DrawRectangleRoundedLinesEx(th, 1.f, 8, 1.f, alphaMul(pal::WoodDark, 0.6f));
    }
    drawText(FontId::Ui, "Fare tekerleği, ok tuşları ya da kaydırma çubuğu", {L::RulesPanel.x + 40.f, 826.f},
             16.f, alphaMul(pal::TextLight, 0.5f));
    if (drawButton(L::RulesBack, "Geri", m, true, ButtonStyle::Wood, 28.f)) click(C_Back);
}

int Screens::Impl::activeRuleTab() const {
    if (maxScroll() > 0.f && scroll >= maxScroll() - 1.f) return (int)tabs.size() - 1;
    int a = 0;
    for (int t = 0; t < (int)tabs.size(); ++t)
        if (tabY[(size_t)t] <= scroll + 60.f) a = t;
    return a;
}

void Screens::Impl::drawRuleTabs(Vector2 m) {
    const float fs = 19.f, h = 32.f, gap = 8.f, pad = 28.f;
    float total = 0.f;
    std::vector<float> w(tabs.size());
    for (size_t t = 0; t < tabs.size(); ++t) {
        w[t] = measureText(FontId::Chalk, tabs[t].label, fs).x + pad;
        total += w[t] + (t ? gap : 0.f);
    }
    float x = 800.f - total * 0.5f;
    const int active = activeRuleTab();
    for (int t = 0; t < (int)tabs.size(); ++t) {
        const Rectangle r{x, L::RulesTabsY - h * 0.5f, w[t], h};
        if (t == active) {
            DrawRectangleRounded({r.x - 2.f, r.y - 2.f, r.width + 4.f, r.height + 4.f}, 0.35f, 8,
                                 alphaMul(pal::Highlight, 0.85f));
        }
        if (drawButton(r, tabs[(size_t)t].label, m, true, ButtonStyle::Chalk, fs)) click(C_RulesTab, t);
        x += w[(size_t)t] + gap;
    }
}

void Screens::Impl::drawRich(const RichText& rt, float x, float y, float size) {
    for (const Piece& p : rt.pieces) drawText(p.font, p.text, {x + p.pos.x, y + p.pos.y}, size, p.color);
}

void Screens::Impl::drawRuleBlock(const RuleBlock& b, float x, float y) const {
    switch (b.kind) {
    case RuleBlock::Heading: {
        const float fs = 31.f;
        const Vector2 ms = measureText(FontId::Sign, b.text, fs);
        DrawRectangleRec({x - 18.f, y + 11.f, 6.f, 20.f}, kSignRed);
        drawText(FontId::Sign, b.text, {x, y + 4.f}, fs, kSignRed);
        DrawLineEx({x + ms.x + 16.f, y + 24.f}, {x + L::RulesView.width, y + 24.f}, 1.5f, rgba(150, 100, 60, 0.35f));
        break;
    }
    case RuleBlock::Para:
        drawRich(b.rich, x, y, 21.f);
        break;
    case RuleBlock::Bullet:
        if (b.label.empty()) {
            DrawCircleV({x + 14.f, y + 14.f}, 4.f, kSignRed);
        } else {
            drawText(FontId::UiBold, b.label, {x + 4.f, y + 2.f}, 21.f, kSignRed);
        }
        drawRich(b.rich, x + 34.f, y, 21.f);
        break;
    case RuleBlock::Tiles:
        for (const RuleItem& it : b.items) {
            const float n = (float)it.tiles.size();
            const float tilesW = n * kMiniW + (n - 1.f) * kMiniGap;
            float tx = x + it.box.x + (it.box.width - tilesW) * 0.5f;
            const float ty = y + it.box.y;
            for (const MiniTile& t : it.tiles) {
                drawMiniTile({tx, ty - (t.mark ? 3.f : 0.f), kMiniW, kMiniH}, t);
                tx += kMiniW + kMiniGap;
            }
            if (it.wrong) {
                const float x0 = x + it.box.x + (it.box.width - tilesW) * 0.5f - 4.f;
                pencilLine({x0, ty - 2.f}, {x0 + tilesW + 8.f, ty + kMiniH + 2.f}, 3.f, alphaMul(kRedPencil, 0.9f), 7u);
                pencilLine({x0, ty + kMiniH + 2.f}, {x0 + tilesW + 8.f, ty - 2.f}, 3.f, alphaMul(kRedPencil, 0.9f), 9u);
            }
            drawTextCentered(FontId::Ui, it.caption, {x + it.box.x + it.box.width * 0.5f, ty + kMiniH + 16.f}, 17.f,
                             it.wrong ? kRedPencil : kGraphite);
        }
        break;
    }
}

} // namespace ui
