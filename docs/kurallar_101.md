# 101 Okey: kurallar ve masadan masaya değişenler

Bu sayfa SaklıBahçe'deki 101'in (tekli ve eşli) kurallarını ve Ayarlar'daki **101 kuralları** sayfasından
seçilebilen usulleri yazar. Oyunun içindeki kurallar sayfası (Screens.cpp, `buildRules101`) aynı kuralları
anlatır; buradaki her seçim orada da kısaca geçer. Varsayılanlar, oyunun bu sayfadan önceki kurallarının
aynısıdır: hiçbir şey seçmeyen oyuncu için hiçbir şey değişmez.

## Temel kurallar (varsayılan usul)

- 106 taş; eli başlatana 22, diğerlerine 21 taş. Gösterge, aynı renkte bir üstü okey. Sahte okey okeyin yerine
  oynanır. 101'de seri 13'te biter (12-13-1 yok).
- **Açmak:** seri ve grupların toplamı en az **101**, ya da en az **5 çift** (çifte gitmek). Açılışta seri ile
  çift karışmaz. Çiftle açan sonra yalnızca çift indirir; seriyle açan, masada çiftle açan varsa çift de indirir.
- **İşlemek:** yalnızca açtıktan sonraki turlarda (açtığın turda işlenmez, per eklenmez, okey alınmaz).
  Çiftlere taş işlenmez. Açmamış oyuncu işleyemez.
- **Yandan almak:** soldakinin son attığı taş alınabilir, ama aynı turda masada kullanılmalı (açılışta, yeni
  perde, işlerken). Kullanılamazsa **Geri Ver** ile cezasız geri verilir.
- **Yandan açma cezası:** yandan aldığı taşla açan olursa, o taşı atana taşın sayısının seri açılışta ×10, çift
  açılışta ×20 katı ceza yazılır.
- **Cezalar (101):** okey atmak (bitiren son taş hariç), işlek taş atmak (masadaki bir pere gidecek taş).
  Açmış oyuncunun elinde kalan her okey için 101.
- **Puan:** bitiren −101; açmayan 202; seriyle açan elinde kalanın toplamı; çiftle açan elinde kalanın iki
  katı; üstüne o elin cezaları.
- **Katlar:** okeyle bitiş (son taş okey), çiftten bitiş (çiftle açmış biri bitirirse), elden bitiş (kimse
  açmamışken bütün elini tek seferde açıp bitirmek) ×2'dir; katlar birbiriyle çarpılır (×4, ×8). Kat herkesin
  hesabını çarpar (bitirenin −101'i dahil), ama ayrıca yazılan cezaları çarpmaz.
- **Eşli 101:** karşılıklı oturanlar ortak; biri bitirince ortağının eli silinir (cezaları kalır), takım toplamı
  düşük olan kazanır.

## Masadan masaya değişenler (Ayarlar: 101 kuralları)

Kahveden kahveye, uygulamadan uygulamaya en çok değişen kuralları seçenek yaptık. Her seçim yeni maçta geçerli
olur; yarım kalan maç ve tekrarlar kendi ayar satırlarıyla (`k y101...`) kaydedilir, böylece devam eden ya da
yeniden izlenen maç başladığı kurallarla oynanır.

| Seçenek | Değerler (varsayılan kalın) | Ayar anahtarı | Motor |
|---|---|---|---|
| Açma sınırı | 51 · 81 · **101** · 121 | `y101acma` | `RulesConfig::openThreshold` |
| Katlamalı oyun | **kapalı** · açık | `katlamali` | `katlamali` |
| Bitiş katları | **Katlanır** · Tek kat · Katsız | `y101kat` | `finishMult` |
| Açmayan yazar | **202** · 404 | `y101acmayan` | `unopenedScore` |
| Okey atma cezası | **açık** · kapalı | `y101okeyceza` | `penaltyJokerDiscard` |
| İşlek taş cezası | **açık** · kapalı | `y101islekceza` | `penaltyPlayableDiscard` |
| Yandan açma cezası | **açık** · kapalı | `yandanceza` | `leftOpenPenalty` |
| Geri verme cezası | **kapalı** · açık | `y101geriver` | `penaltyReturnLeft` |
| Açınca bir tur bekle | **açık** · kapalı | `y101bekle` | `waitTurnAfterOpening` |

### Katlamalı oyun

Mynet'in ve Zynga'nın 101 masalarında "katlamalı / katlamasız" diye ayrılan oyun budur: katlamalı masada senden
önce seriyle açan olduysa, onun toplamının **en az bir fazlasıyla** açarsın (116 açıldıysa sana 117 gerekir);
çiftte de aynısı (5 çift açıldıysa 6 çift). Seri ve çift ayrı sayılır. Masadaki sayaç o anki gereken sayıyı
gösterir. Bu oyunda "katlamalı" kelimesi bunun için kullanıldığından, puanların katlanması ayrı bir seçenektir
(aşağıda).

### Bitiş katları: katlanır, tek kat, katsız

Kahvede "katlar katlanır mı?" diye sorulan şey budur.

- **Katlanır (varsayılan):** okeyle, çiftten, elden bitişin her biri ×2 ve çarpılır. Okeyle + çiftten ×4, üçü
  birden ×8. Örnek: çiftle açmış oyuncu okeyi atarak biterse ×4: bitiren −404, açmayan 202 × 4 = 808, seriyle
  açıp elinde 20 kalan 80 yazar.
- **Tek kat:** bitiş ne kadar süslü olursa olsun en çok ×2. Aynı örnekte bitiren −202, açmayan 404, 20'si kalan
  40.
- **Katsız:** hiç kat yazılmaz; bitiren her zaman −101. Çiftle açanın elinde kalanın iki katı yine geçerlidir
  (o bir kat değil, çifte gitmenin bedelidir).

Hesap kağıdı o elin katını kırmızı kalemle yazar ("okeyle bitiş ×2 · çiftten bitiş ×2 = ×4 katlandı", tek
katta "= ×2 (tek kat)", katsızda "katsız oyun: kat yok"); geçmiş ellerin katı da satırlarının yanında ("3. el
×4") durur. Kağıdın köşesinde masanın usulü yazar (katlamalı, tek kat, katsız).

### Açma sınırı

Varsayılan 101. Hızlı ya da acemi masaları için 81 ve 51, zor masa için 121. Katlamalı oyunda bu, ilk açılışın
sınırıdır; sonrakiler bir öncekinin bir fazlasıyla açar. Rehber kartı ve masadaki sayaç seçilen sınırı yazar.

### Açmayan yazar

Elini hiç açamayan 202 (varsayılan) ya da 404 yazar. Katlar bunu da çarpar (katlanırda en çok ×8). 404 masada
açmamak çok pahalı olduğu için botlar açılışa daha istekli oynar (rollout'larında açmayan puanı bu sayıdır).

### Cezalar

- **Okey atma cezası** ve **işlek taş cezası**: ikisi de 101; bazı masalar oynamaz, kapatılabilir. Kapalıyken
  oyun o taşları atarken uyarmaz, botlar da onları cezasız sayar.
- **Yandan açma cezası:** yukarıda; kapatılabilir.
- **Geri verme cezası:** yandan alıp kullanamadığın taşı geri verene 101. Bazı kahvelerde yandan "deneyip" geri
  vermek ayıptır; varsayılan kapalıdır.

### İşleme: açınca bir tur bekle

Açık (varsayılan): açtığın turda işleyemez, yeni per indiremez, okey alamazsın. Kapalı: açtığın turda hepsini
yapabilirsin (elden bitiş de kolaylaşır). Açmadan işlemek hiçbir usulde yoktur.

## Bakıp almadığımız usuller

- **Gösterge katlaması / göstergeyi gösterme:** klasik okeyde göstergenin eşini gösteren puan alır (oyunda
  "Okey" masasında var). 101'de bunun yaygın bir karşılığını bulamadık; 101 sitelerinin ve uygulamalarının
  kurallarında gösterge yalnızca okeyi belirler. Eklemedik.
- **Renkli 101:** "renkli" (kırmızı ya da siyah göstergede puanların iki katı) klasik okeyin bir usulüdür ve
  oyunda Okey masasının ayarıdır. 101 için ortak bir kural olarak geçmiyor; eklemedik.
- **Cezaların da katlanması:** bazı masalarda okey cezası da kat ile çarpılır; yaygın değil, cezalar katlanmaz.
- **Elden bitişte açmayana 404 sınırı:** Pagat'ın anlattığı bir usulde açmayanın yazdığı en çok 404'tür; bizde
  açmayan da katla çarpılır (katlanırda ×8'e kadar). Tek kat seçeneği bu sınırı pratikte verir.

## Botlar, İpucu ve analiz

Botlar kuralları motorun kendisinden okur: bitişin değerini `Game::finishMultiplier` ile (tek katta ×2'den fazla
beklemezler, katsızda elden bitişe değer biçmezler), açılışı `seriesOpenNeed()` ile, açmayan puanını ve cezaları
`RulesConfig` ile hesaplar. İpucu ve "Hatalarım" analizi maçın kendi ayarlarıyla (app/Rules101.h) aynı motoru
kurar. Masadaki ceza uyarıları (okey, işlek taş, geri verme) ancak o ceza açıksa çıkar.

## Kaynaklar

- Pagat, "Okey 101": https://pagat.com/rummy/okey101.html (puanlama tablosu ve katlar)
- Mynet 101 Çanak Okey ve 101 Okey uygulamalarının tanıtımları (katlamalı / katlamasız masa, "yüksek serilerle
  rakipleri zor duruma düşürmek"; okeyle, elden, çifte bitiş): App Store ve tamindir.com sayfaları
- Kahvehane usulü: ekibin ve oyuncuların bildiği masa kuralları (yandan açma cezası, geri verme, açınca bekleme)
