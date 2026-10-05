# SaklıBahçe

Gece yarısı, dumanaltı bir mahalle kahvehanesi. Yeşil çuhalı okey masasına oturuyorsun; karşında Kel Mahmut
sigarasını tüttürüyor, sağında Hacı Rıza tespih çekiyor, solunda Emekli Nuri oraletini yudumluyor. Çaycı askılı
tepsisiyle masaların arasında dolaşıyor, arka masada tavla zarları şakırdıyor, köşedeki tüplü televizyonda maç var.
Bazı geceler dışarıda yağmur yağar: damlalar cama vurup süzülür, olukta su tıpırdar, kaldırımdan şemsiyeli
yolcular geçer. Kahvehanenin kedisi sobanın dibinde ya da boş bir sandalyede kıvrılıp uyur; arada bir uyanır,
gerinir, yalanır, başka bir köşeye geçer, bazen de dönüp sana bakar.

**SaklıBahçe**, kahvehanenin müdavimlerine karşı oynadığın birinci şahıs 3B bir masaüstü oyunudur
(C++17 + raylib 6.0, macOS). Masada yedi oyun var: **101**, **Eşli 101**, **Okey**, **Tavla**, **Pişti**,
**Batak** ve **King**. Görüntülerin tamamı kodla üretilir: hiçbir resim ya da model dosyası yoktur; yazı
tipleri macOS'un kendi yazı tiplerinden alınır. Canın oynamak istemediğinde **Yapay Zeka** modunu açıp arkana
yaslanabilirsin: taşlarını senin yerine yapay zeka oynar, sen çayını içip izlersin.

## Oyunlar

Giriş ekranında **Oyna**'ya basınca hangi oyunu oynayacağını seçersin; son seçtiğin oyun hatırlanır. Her oyunun
kuralları kendi "Nasıl Oynanır?" sayfasındadır (Kurallar), ayarları da Ayarlar'da o oyunun bölümündedir.

| Oyun | Kimle | Kısaca |
|---|---|---|
| **101** | üç rakip | Per aç, 101'i geç, elini bitir; düşük puan kazanır. Katlamalı oyun ve yandan açma cezası ayarlardan. |
| **Eşli 101** | Kel Mahmut ortağın | Biriniz bitirince ötekinin eli silinir; takım toplamı düşük olan kazanır. |
| **Okey** | üç rakip | 14 taşı per ya da yedi çifte diz, 15.'yi atıp bit (12-13-1 geçerli). Herkes 20 puanla başlar, bitiş 2, okey atarak ya da çiftten 4, gösterge 1 düşer; sıfıra inen olunca oyun biter. |
| **Tavla** | Kel Mahmut | Klasik tavla: kırık pul, kapı, mars (2 sayı); maç 3, 5 ya da 7 sayıya. Hacı Rıza ile Emekli Nuri seyredip laf atar. |
| **Pişti** | dört kişi, eşli ya da Mahmut'la ikili | Aynı kâğıt ya da vale yerdekileri alır; tek kâğıda pişti 10, valeye vale 20. 101 ya da 151'e. |
| **Batak** | tekli ya da eşli | İhaleli batak: ihaleyi al, kozu söyle; tutamazsan batarsın. 31, 51 ya da 71'e. |
| **King** | üç rakip | 20 el: her oyuncu 2 koz, 3 ceza seçer (el almaz, kupa almaz, erkek almaz, kız almaz, rıfkı, son iki). |

Rakiplerin üç seviyesi (Acemi, Usta, Kurt) her oyunda vardır. Botlar yalnızca masada görülebilenleri bilir;
Kurt kart oyunlarında görmediği kâğıtları olası dağılımlarla örnekleyip el sonuna kadar oynayarak (Monte Carlo),
tavlada rakibin 21 zar ihtimaline bakarak karar verir. Kâğıt oyunlarında kâğıdına tıklarsın (oynayabileceklerin
parlak, ötekiler soluk), tavlada önce pulunun durduğu haneye sonra yeşil yanan haneye tıklarsın.

## Kurulum ve çalıştırma

Gerekenler: macOS, Xcode komut satırı araçları (Apple clang) ve Homebrew.

```sh
brew install raylib      # raylib 6.0 (/opt/homebrew)
make                     # ./saklibahce oluşur
./saklibahce
```

Diğer hedefler:

| Komut | Ne yapar |
|---|---|
| `make run` | derler ve oyunu başlatır |
| `make test` | yedi oyunun kural motoru ve yapay zekâ testleri, ardından kısa bot-bot simülasyonları |
| `make tablescheck` | tavla ve kâğıt oyunlarını gizli bir pencerede gerçek fare tıklamalarıyla oynatır |
| `make asan` | AddressSanitizer + UBSan ile `build/asan/` altında oyunu ve testleri derler, testleri çalıştırır |
| `make clean` | derleme çıktılarını siler |

raylib başka bir yerdeyse: `make RAYLIB=/yol/raylib`.

## Kontroller

Masada, kendi sandalyende oturuyorsun; ıstakan önünde, rakiplerin karşında ve iki yanında.

| Ne | Nasıl |
|---|---|
| Etrafa bakmak | farenin **sağ tuşunu** basılı tutup sürükle |
| Yakınlaşmak | **fare tekerleği** |
| Bakışı masaya ortalamak | **R** ya da sağ tuşa çift tıklama |
| Istakayı dizmek | taşları sürükleyerek 2 sıra × 16 yuvaya yerleştir; yan yana duran taşlar bir grup sayılır, boşluk grupları ayırır |
| Taş çekmek | ortadaki desteye ya da sol alttaki (soldaki oyuncunun) atık taşına tıkla veya ıstakana sürükle |
| Taş atmak | taşı sağ alttaki atık yerine sürükle ya da taşa çift tıkla |
| El Aç / Per Aç | ıstakadaki geçerli grupları masaya indirir (**Enter**) |
| İşlemek | taşı masadaki bir perin üzerine sürükle: sol yarısı başa, sağ yarısı sona; perdeki okeyin üzerine bırakırsan okeyi alırsın |
| Seri Diz / Çift Diz | ıstakayı kendiliğinden dizer (**S** / **C**) |
| Geri Ver | yandan aldığın ama kullanamadığın taşı cezasız geri verir |
| Uzaktaki bir per ya da atık yığını | üzerinde fareyle biraz bekle: büyütülmüş hâli açılır; taşı bir rakibin perinin üzerinde tutarsan taşının eklenmiş hâli görünür |
| Yapay Zeka modunu açıp kapamak | **Y**, sağ alttaki **Yapay Zeka** düğmesi ya da Çay Molası menüsü (ESC) |
| Duraklatmak | **ESC** ya da **Menü** düğmesi |

İpuçları açıkken geçerli gruplar ıstakada parlar, alttaki durum satırında "Seri 87/101 · Çift 3/5" gibi bir
sayaç görünür; işlek taşların köşesinde yeşil bir **+** vardır. İşlek taş ya da okey atmaya kalkarsan oyun seni
uyarır ve onay ister.

## Yapay Zeka modu

Taşları bir süre yapay zekaya bırakmak istersen oyunun herhangi bir anında **Y** tuşuna bas ya da sağ alttaki
**Yapay Zeka** düğmesine tıkla (Çay Molası menüsünde de "Yapay Zeka Oynasın" var). Giriş ekranındaki
**Yapay Zekayı İzle** ise maçı baştan bu modda başlatır. Mod açıkken:

- Senin koltuğunda en güçlü rakip seviyesi (**Kurt**) oynar, öbür oyuncular gibi düşünüp taşını sırayla oynar.
  Durum satırında "Yapay zeka düşünüyor…" ya da "Yapay zeka oynuyor…" yazar, **Yapay Zeka** düğmesi yanar.
- Istakan her taş gelip gidişinde kendiliğinden dizilir: yapay zeka hangi yola gidiyorsa (seri ya da çift)
  taşlar o gruplara ayrılır, böylece ne planladığını görebilirsin. Hamleleri "Yapay zeka yandan Kırmızı 5 aldı" gibi
  haber verilir.
- Taşlara dokunamazsın ama etrafa bakmak (sağ tuş), menüler, kurallar ve ayarlar çalışır. El sonundaki hesap
  kâğıdı ve maç sonu ekranı birkaç saniye sonra kendiliğinden geçer (düğmedeki sayaç gösterir); beklemeden
  kendin de basabilirsin.
- Aynı tuşa ya da düğmeye yeniden basınca (menüde "Kontrolü Geri Al") kontrol hemen sana döner; sıra sendeyse
  turun kaldığı yerden devam edersin (taş çekmek, açmak ya da atmak).
- Puanlar her zamanki gibi yazılır; hesap kâğıdında koltuğun "Yapay Zeka (Sen)" diye görünür.

Mod her açılışta kapalı başlar; `--ai` ile oyunu doğrudan bu modda bir maçla açabilirsin.

## 101 Okey kuralları (özet)

- **Taşlar:** 106 taş: Sarı, Mavi, Siyah ve Kırmızı renklerde 1–13, her taştan iki tane, bir de iki **sahte okey**.
- **Gösterge ve okey:** Karıştırmadan sonra bir taş açılır (gösterge). Aynı renkte bir üst sayı o elin **okey**idir
  (13'ün üstü 1). İki okey taşı jokerdir; sahte okey jokere değil, okeyin kendi rengine ve sayısına geçer.
- **Dağıtım:** Eli başlatan oyuncuya 22, diğerlerine 21 taş; kalan 20 taş ortada kapalı destedir. Başlatan oyuncu
  çekmeden atar; başlatma sırası her el sağa geçer.
- **Tur:** Sıra sende: ya ortadan çekersin ya da solundaki oyuncunun attığı son taşı alırsın. Yandan aldığın taşı
  aynı turda masada kullanmak zorundasın; kullanamazsan cezasız geri verip ortadan çekersin. Sonra istersen el açar,
  per indirir, işler ya da okey alırsın ve bir taş atarak turu bitirirsin.
- **Perler:** Aynı renkte ardışık en az üç taş (seri; seri 13'te biter: normal okeyin aksine 101'de 12-13-1 ve
  13-1-2 olmaz, 1 yalnızca 1-2-3 diye başta kullanılır), aynı sayının farklı renklerinden 3–4 taş (grup) ya da iki
  özdeş taş (çift).
- **El açmak:** Seri ve gruplarla toplam en az **101** sayı, ya da en az **5 çift**. İkisi aynı açılışta karışmaz.
  Açılışa istediğin kadar per koyabilirsin ama açtığın turda işleyemez, yeni per indiremez, okey alamazsın.
  Sonraki turlarda çiftle açan yalnızca çift indirir; seriyle açan seri indirir, masada çiftle açmış başka biri
  varsa çiftlerini de indirebilir. Açmış herkes masadaki seri ve gruplara taş işleyebilir, okeyi gerçek taşıyla
  değiştirip alabilir. Elinde atacak en az bir taş kalmalıdır.
- **Cezalar (+101):** okey atmak (bitiş taşı değilse), masadaki bir pere uyan (işlek) taşı atmak.
- **Yandan açma cezası (Ayarlar'dan kapatılabilir):** Biri senin attığın taşı alıp onunla elini açarsa, taşın
  sayısının seri açılışta 10, çift açılışta 20 katı sana ceza yazılır (yandan 7 alıp seriyle açtı: atana 70).
- **El sonu:** Son taşını atan eli bitirir. Deste biterse el kazanansız biter.
- **Puanlama (düşük puan iyidir):** Bitiren −101, hiç açmayan 202, açanlar elinde kalan taşların toplamını yazar
  (çiftle açanın eli iki katı yazılır); açmış birinin elinde okey kaldıysa her okey için **101 ceza**. Okeyle,
  çiftten ya da elden bitirmek (henüz kimse açmamışken bütün eli tek seferde açıp bitirmek) elin çarpanını her
  biri için ikiye katlar ve çarpan herkesin puanına uygulanır. Cezalar çarpana girmeden eklenir. Maç
  ayarlardaki el sayısı kadar sürer; toplamı en düşük olan kazanır (eşitlikte birincilik paylaşılır).
- **Katlamalı oyun (Ayarlar'dan ya da `--katlamali` ile):** Senden önce biri seriyle açtıysa onun toplamından en
  az 1 fazlasıyla açarsın (116 → 117), çiftle açtıysa ondan 1 çift fazlasıyla (5 → 6). Seri ve çift ayrı sayılır;
  masadaki sayaç ve skor tahtası bunu gösterir.
- **Kaynak:** Kurallar Türkiye'deki yaygın 101 (Zynga 101 Okey Plus ve Digitoy Yüzbir SSS'leri, kahvehane
  usulü) ile karşılaştırıldı. Bölgeden bölgeye değişen kurallar: yandan alınıp kullanılamayan taş burada cezasız
  geri verilir; yandan alınan taşla açılınca atana taş sayısının 10/20 katı ceza yazılır.

Kuralların tamamı oyunun içinde **Kurallar** ekranında örneklerle anlatılır.

## Rakipler

| Koltuk | Oyuncu | |
|---|---|---|
| sağ | **Hacı Rıza** | kasketli, gri bıyıklı, tespihli; sakin, ata sözleriyle konuşur |
| karşı | **Kel Mahmut** | iri, kara bıyıklı, çizgili gömlekli; sigara içer, gürültülü bir futbol sevdalısı |
| sol | **Emekli Nuri** | ak saçlı, gözlüklü, hırkalı; oralet içer, "bizim zamanımızda…" diye söylenir |

Zorluk **Ayarlar**'dan seçilir: **Acemi**, **Usta** ya da **Kurt**. Botlar hileye başvurmaz: yalnızca masada
görülebilen taşları ve kendi ellerini bilirler. Usta ile Kurt, henüz açmamış sağdaki oyuncuya yüksek taş
vermenin yandan açma cezası riskini hesaplar; büyük taşları o açana kadar ellerinde tutmaya çalışırlar.

Ayarlar (son oynanan oyun, oyunların kendi ayarları, zorluk, ses/müzik/ortam sesi, animasyon hızı, ipuçları,
oyuncu adı)
`~/Library/Application Support/SakliBahce/ayarlar.txt` dosyasında saklanır (oyunun eski adıyla kalmış
`Kiraathane101/ayarlar.txt` varsa ilk açılışta o okunur). `--hands`, `--level` ve
`--no-audio` yalnızca o oturum için geçerlidir; oturum sırasında Ayarlar'dan değiştirmediğin sürece kayıtlı
ayarlarına dokunmaz.

## Komut satırı seçenekleri

| Seçenek | Açıklama |
|---|---|
| `--seed N` | aynı dağıtımlar, aynı rakip kararları ve aynı kahvehane (tekrar oynatılabilir) |
| `--start` | giriş ekranını atlayıp doğrudan oyuna başla |
| `--hands N` | el sayısı (1–11) |
| `--level L` | rakip seviyesi: 0 Acemi, 1 Usta, 2 Kurt |
| `--ai` | Yapay Zeka modunda bir maçla başla (oyunda **Y** ile aç/kapa) |
| `--katlamali` | katlamalı oyun: her açan, öncekinden en az 1 fazlasıyla açar (yalnızca o oturum için) |
| `--game G` | oyun: `101`, `esli`, `okey`, `tavla`, `pisti`, `batak` ya da `king` |
| `--set K=V` | bir ayar, `ayarlar.txt` anahtarlarıyla (ör. `--set batakesli=1`, `--set pistimasa=2`, `--set tavla=3`) |
| `--autoplay` | senin yerine bir Usta bot oynar (izleme modu); maç bitince oyun kapanır |
| `--speed X` | oyunu X kat hızlı oynat (ör. `--speed 4`) |
| `--matches N` | (`--autoplay` ile) arka arkaya N maç; her ikincisi giriş ekranından geçer |
| `--no-audio` | sessiz |
| `--max-frames N` | N kareden sonra çık |
| `--perf` | dikey eşitleme kapalı; çıkışta kare süresi istatistikleri |
| `--screenshot DOSYA` | `--max-frames` ile: son karede pencerenin görüntüsünü PNG olarak kaydet (`--state title`, `game`, `rules` ya da `settings` ile başlangıç ekranı seçilir) |
| `--snapshot DOSYA` | gizli pencerede 1600×900 tek bir kare çizip PNG olarak kaydet ve çık |
| `--frames N` | (`--snapshot` ile) görüntüden önce simüle edilecek kare sayısı |
| `--state S` | (`--snapshot` ile) `title`, `game`, `summary`, `matchover`, `rules`, `settings` ya da `games` |
| `--view V` | (`--snapshot` ile) `seat`, `left`, `right`, `back` ya da `corner` |
| `--help` | yardım |

Örnekler:

```sh
./saklibahce --ai --level 2                           # yapay zekayı Kurt'lara karşı izle
./saklibahce --game tavla --start                      # doğrudan Kel Mahmut'la tavlaya otur
./saklibahce --game batak --set batakesli=1 --autoplay --speed 4   # eşli batak izle
./saklibahce --autoplay --speed 4 --hands 3            # botları izle
./saklibahce --seed 42 --start --level 2               # Kurt'lara karşı, hep aynı dağıtım
./saklibahce --seed 42 --autoplay --speed 3 --frames 2400 \
    --snapshot el_ortasi.png --state game --view seat     # bir oyun anının fotoğrafı
```

## Müzik

Duvardaki eski radyo, `assets/music/` klasöründeki gerçek kayıtları karışık sırayla çalar. Radyo tınısı için hafif
bir hoparlör süzgecinden geçerler; aralarda radyonun cızırtısı duyulur. Yeni bir şarkı başlayınca masada
"Radyoda: …" notu görünür. Kayıtların hepsi Wikimedia Commons'tan alındı:

* **Turku, Nomads of the Silk Road** — *Alleys of Istanbul* albümünden on türkü (Üsküdar'a Gider İken, Misket,
  Maçka Yolları, Harman Dalı, Ağrı Dağından Uçtum ve diğerleri), lisans
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
* **Eski plaklar (kamu malı):** Ahmed Cevdet'in *Hicaz Taksim*'i (Polydor, yaklaşık 1928) ile Hafız Kemal Bey ve
  Hayriye Hanım'ın *Hüseyni Saz Semaisi*.

Kaynak bağlantıları ve yapılan değişiklikler (sessizlik kırpma, ses eşitleme, mono MP3) için:
[`assets/music/KAYNAKLAR.md`](assets/music/KAYNAKLAR.md). Çalma listesini `assets/music/liste.tsv` belirler;
klasör yoksa radyo eski sentezlenmiş ezgilerine döner. Radyo, Ayarlar'daki müzik düğmesiyle kapatılabilir.

## Proje yapısı

| Klasör | İçerik |
|---|---|
| `src/core/` | oyunların kuralları ve botları, raylib kullanmaz: 101 ve okey (`Game`, `Meld`, `Solver`, `OkeyHand`, `Bot`), tavla (`Tavla`, `TavlaBot`), iskambil (`Cards`), pişti (`Pisti`, `PistiBot`), batak (`Batak`, `BatakBot`), king (`King`, `KingBot`) |
| `src/r3d/` | 3B dünya: çizici (`Gfx`), kahvehane (`Room`), insanlar (`Characters`), masa, taşlar ve oyuncu kamerası (`Table3D`, `PlayerCamera`), kâğıtlar (`Cards3D`), tavla tahtası (`Tavla3D`), öteki oyunların HUD'u (`GameHud`) |
| `src/ui/` | menüler, oyun seçimi ve skor kâğıdı (`Screens`), efektler, ortam sesi ve radyo (`Audio`), muhabbet (`Banter`), yazı tipleri ve düğmeler (`Common`), taş yüzleri (`TileRender`), kâğıt yüzleri (`CardRender`) |
| `src/app/` | pencere, oyun döngüsü, maç akışı, bot temposu, komut satırı; tavla ve kâğıt oyunlarının masaları (`TableGame`, `CardTable`, `TavlaTable`, `PistiTable`, `BatakTable`, `KingTable`) |
| `docs/` | tavla, pişti, batak ve king'in kural metinleri ve kaynakları (oyuna `tools/gen_rules.py` ile gömülür) |
| `tests/` | her oyunun motor ve yapay zekâ testleri, bot-bot simülasyonları (`sim`, `tavla_sim`, `pisti_sim`, `batak_sim`, `king_sim`) |
| `tools/` | geliştirme sırasında kullanılan görüntü alma araçları (oyuna derlenmez) |

Tasarım belgeleri: `DESIGN.md` (kurallar, motor, botlar, ses) ve `DESIGN3D.md` (3B dünya).
