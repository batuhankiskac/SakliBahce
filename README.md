# SaklıBahçe

Gece yarısı, dumanaltı bir mahalle kahvehanesi. Yeşil çuhalı okey masasına oturuyorsun; karşında Kel Mahmut
sigarasını tüttürüyor, sağında Hacı Rıza tespih çekiyor, solunda Emekli Nuri oraletini yudumluyor. Çaycı askılı
tepsisiyle masaların arasında dolaşıyor, arka masada tavla zarları şakırdıyor, köşedeki tüplü televizyonda maç var.
Bazı geceler dışarıda yağmur yağar: damlalar cama vurup süzülür, olukta su tıpırdar, kaldırımdan şemsiyeli
yolcular geçer. Kahvehanenin kedisi sobanın dibinde ya da boş bir sandalyede kıvrılıp uyur; arada bir uyanır,
gerinir, yalanır, başka bir köşeye geçer, bazen de dönüp sana bakar; masaların, saksıların ve ayakta duranların
arasından dolanır, yerini kapan olursa kalkıp gider. Tezgâhın başında yaşlı ocakçı durur: demliği
çaydanlıktan indirip demler, sipariş gelince çırağın tepsisindeki bardakları doldurup tepsiyi onun eline verir, boş
kaldıkça bardak yıkar, tezgâhı siler, ocağı açar, gazetesini okur, içerideyse maça bakar.

**SaklıBahçe**, kahvehanenin müdavimlerine karşı oynadığın birinci şahıs 3B bir masaüstü oyunudur
(C++17 + raylib 6.0; macOS, Linux'ta da derlenir). Masada on bir oyun var: **101**, **Eşli 101**, **Okey**, **Tavla** (Gülbahar ve Fevga çeşitleriyle),
**Pişti**, **Batak**, **King**, **Konken**, **Dama**, **Altmışaltı** ve **Bezik**. Görüntülerin tamamı kodla üretilir: hiçbir resim ya da model dosyası yoktur; yazı
tipleri macOS'un kendi yazı tiplerinden alınır. Canın oynamak istemediğinde **Yapay Zeka** modunu açıp arkana
yaslanabilirsin: taşlarını senin yerine yapay zeka oynar, sen çayını içip izlersin.

## Oyunlar

Giriş ekranında **Oyna**'ya basınca hangi oyunu oynayacağını seçersin; son seçtiğin oyun hatırlanır. Her oyunun
kuralları kendi "Nasıl Oynanır?" sayfasındadır (Kurallar), ayarları da Ayarlar'da o oyunun bölümündedir.

| Oyun | Kimle | Kısaca |
|---|---|---|
| **101** | üç rakip | Per aç, 101'i geç, elini bitir; düşük puan kazanır. Katlamalı oyun ve yandan açma cezası ayarlardan. |
| **Eşli 101** | Kel Mahmut ortağın | Biriniz bitirince ötekinin eli silinir; takım toplamı düşük olan kazanır. |
| **Okey** | üç rakip | 14 taşı per ya da yedi çifte diz, 15.'yi atıp bit (12-13-1 geçerli). Herkes 20 puanla başlar, bitiş 2, okey atarak ya da çiftten 4, gösterge 1 düşer; sıfıra inen olunca oyun biter. İsteğe bağlı renkli okey (kırmızı/siyah gösterge ×2). |
| **Tavla** | Kel Mahmut (Ayarlar → Rakip: Hacı Rıza ya da Emekli Nuri) | Klasik tavla: kırık pul, kapı, mars (2 sayı); maç 3, 5 ya da 7 sayıya. İsteğe bağlı katlama zarı ve katmerli mars; Hamleler paneli. Ayarlar → Tavla → Çeşit ile **Gülbahar** (çiftler düşeşe kadar merdiven) ya da **Fevga** (Moultezim): pullar tek köşeden aynı yöne yürür, kırma yok. Kendi masasında oynanır; öteki ikisi okey masasından seyredip laf atar. |
| **Pişti** | dört kişi, eşli ya da Mahmut'la ikili (ikili: tavla masasında) | Aynı kâğıt ya da vale yerdekileri alır; tek kâğıda pişti 10, valeye vale 20. 101 ya da 151'e. |
| **Batak** | tekli ya da eşli | İhaleli batak: ihaleyi al, kozu söyle; tutamazsan batarsın. 31, 51 ya da 71'e. "Önce koz açılmalı" kuralı ayarlardan. |
| **King** | üç rakip | 20 el: her oyuncu 2 koz, 3 ceza seçer (el almaz, kupa almaz, erkek almaz, kız almaz, rıfkı, son iki). Kısa King: 12 el, 1 koz 2 ceza. |
| **Altmışaltı** | Kel Mahmut (iki kişilik masada; Ayarlar → Rakip) | 24 kâğıt (9, 10, Vale, Kız, Papaz, As), 6'şar kâğıt ve altta açık koz. As 11, 10 10, Papaz 4, Kız 3, Vale 2; 66'yı ilk bulan eli alır. Evlilik (Kız + Papaz) 20, kozda 40; koz dokuzuyla açık kozu almak (**Kozu Al**); desteyi kapatınca (**Kapat**) renge uymak ve eli yükseltmek zorunlu, tutturamayan 2-3 oyun verir. El 1, 2 ya da 3 oyun; 7 oyuna. |
| **Dama** | Kel Mahmut (Ayarlar → Rakip: Hacı Rıza ya da Emekli Nuri) | Türk daması: taşlar ileri ve yana gider, almak zorunlu, en çok taşı alan yol seçilir; son sıraya varan dama olur ve uzaktan alır. 1, 3 ya da 5 oyun alan kazanır. İki kişilik masada oynanır. |
| **Bezik** | Kel Mahmut (iki kişilik masada; Ayarlar → Rakip) | İki deste (7'den As'a, her kâğıttan iki): el alan deklarasyon yapar — bezik (Maça Kız + Karo Vale) 40, çift bezik 500, koz serisi 250, dört as 100, dört papaz 80, dört kız 60, dört vale 40, evlilik 20, koz evliliği 40, koz 7'si 10. Deklarasyonlar önünde açık durur, oradan da oynanır. Deste bitince renge uymak ve eli geçmek zorunlu; her As ve 10 (brisk) 10, son el 10. 500, 1000 ya da 1500'e. |
| **Konken** | üç rakip | İki deste ve dört joker, 14'er kâğıt: seri ve grup yap, 51 ile aç (Ayarlar'dan 40 ya da 71), masadaki perlere işle, elinde kâğıt bırakma. Yerden aldığın kâğıdı o tur kullanmalısın. Açmayan 100 yazar; hiç açmadan tek turda bitirmek **konken**: herkes iki kat yazar. 151'i (101 / 201) bulan yanar ve masadan kalkar, kalanlar oynar: son kalan kazanır (Ayarlar → Bitiş: "İlk yanan" ile ilk yanan çıkınca biter). Sen yanarsan kalanları hızlı izler ya da sonuca geçersin. |

Rakiplerin üç seviyesi (Acemi, Usta, Kurt) her oyunda vardır. Botlar yalnızca masada görülebilenleri bilir;
Kurt kart oyunlarında görmediği kâğıtları olası dağılımlarla örnekleyip el sonuna kadar oynayarak (Monte Carlo),
tavlada rakibin 21 zar ihtimaline bakarak karar verir. Kâğıt oyunlarında kâğıtların elinde, gözünün hemen altında bir
yelpaze gibi durur: üzerine gelince kâğıt yelpazeden yukarı kalkıp hafifçe eğilir; tıklayarak ya da yukarı, masanın
üstüne sürükleyip bırakarak oynarsın (oynayabileceklerin parlak, ötekiler soluk). Tavlada önce pulunun durduğu
haneye sonra yeşil yanan haneye tıklarsın. Damada taşına tıklayıp yeşil kareye tıklarsın ya da taşı oraya sürüklersin;
gösterdiğin hamlenin alacağı taşların altı kırmızı yanar.
Altmışaltıda ele başlarken sağdaki **Kapat** desteyi kapatır, **Kozu Al** (ya da parlayan açık koza tıklamak) koz
dokuzuyla açık kozu alır; eşi elindeyken Kızı ya da Papazı açınca evlilik kendiliğinden söylenir. Klavyede düğmelere
Tab ya da 2 / 3 ile ulaşılır.

**Tavla kendi masasında oynanır.** Tavla seçilince ekran bir an kararır ve kendini senin sandalyenin arkasında,
sedirin önündeki iki kişilik küçük ceviz masada bulursun; rakibin (varsayılan Kel Mahmut; Ayarlar → Rakip ile
Hacı Rıza ya da Emekli Nuri; aynı seçim dama, altmışaltı, bezik ve iki kişilik piştide de geçerli) karşına oturur, çay bardaklarınız ve Mahmut'un kül tablası da oradadır. Masanın lambası
tahtayı aydınlatır, arkada sokak kapısı ve kâğıt oynayanlar görünür. Öteki iki müdavim okey masasında kalıp sizi
seyreder; uzun bir maçta kapıdan girenler tavla rakibinin arkasında durup izler, çırak çayları oraya getirir. Rakibin
eli taşı tutup kaldırır, alacağı taşların üstünden atlatıp bırakır, aldığı taşları da tek tek tahtadan toplar; kâğıt
oyunlarında desteden çektiği kâğıdı yelpazesine, deklarasyonlarını önüne eliyle koyar. Okey
oyunlarında ve kâğıt oyunlarında yine okey masasına dönersin.

Kâğıtlar gerçekten kâğıt gibi oynanır: dağıtan desteyi karıştırır (iki yarıyı birbirine geçirir) ve keser — pişti'de
kesilen alt kâğıt herkese gösterilir —, kâğıtları teker teker keçenin üstünde kaydırarak herkesin önüne dağıtır;
herkes kendi yığınını alıp elinde açar. Hacı Rıza, Kel Mahmut ve Emekli Nuri kâğıtlarını sol ellerinde yelpaze
yapıp tutar (sırtları bize dönük); sıraları gelince sağ elleriyle yelpazeden bir kâğıt çekip masaya koyar ya da
atarlar, yelpaze kâğıt eksildikçe toplanır. Batak ve King'de eli alan dört kâğıdı eliyle toplayıp önündeki yığına
kapalı koyar; aldığı eller üst üste binmiş küçük destelerden bir bakışta sayılır. Pişti'de ortaya atılan kâğıtlar
hafif dönük ve dağınık düşer, yerdekileri alan eliyle kendi tarafına süpürür; pişti olunca "Pişti!" diye bağırılır,
kahvehane de tepki verir. Karıştırma, kayma, keçeye çarpan kâğıt ve toplanan elin sesleri kodla üretilir. Hepsi
Ayarlar'daki animasyon hızına uyar (Yapay Zeka modu ve `--speed` yavaşlamaz).

## Kurulum ve çalıştırma

Gerekenler (macOS): Xcode komut satırı araçları (Apple clang) ve Homebrew. Linux için aşağıya bakın.

```sh
brew install raylib      # raylib 6.0 (/opt/homebrew)
make                     # ./saklibahce oluşur
./saklibahce
```

Diğer hedefler:

| Komut | Ne yapar |
|---|---|
| `make run` | derler ve oyunu başlatır |
| `make test` | bütün oyunların kural motoru ve yapay zekâ testleri, ardından kısa bot-bot simülasyonları |
| `make tablescheck` | tavla ve kâğıt oyunlarını gizli bir pencerede gerçek fare tıklamalarıyla oynatır (`TABLESCHECK_ARGS=--no-3d`: 3B çizimsiz) |
| `make asan` | AddressSanitizer + UBSan ile `build/asan/` altında oyunu ve testleri derler, testleri çalıştırır |
| `make clean` | derleme çıktılarını siler |

raylib başka bir yerdeyse: `make RAYLIB=/yol/raylib`.

### Linux

Makefile `uname` ile Linux'u tanır ve `-lGL -lm -lpthread -ldl -lrt -lX11` ile bağlar. raylib 6.0 kaynaktan kurulur:

```sh
sudo apt-get install clang libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev \
                     libasound2-dev fonts-dejavu-core fonts-liberation xvfb
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git
make -C raylib/src PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC
sudo make -C raylib/src install          # /usr/local/lib/libraylib.a ve başlıklar
make && make test
```

macOS yazı tipleri yoksa DejaVu / Liberation kullanılır; ayarlar ve kayıtlar `~/.local/share/SakliBahce/` altındadır
(`$XDG_DATA_HOME` varsa orası). Ekransız bir makinede (bulut, CI) görüntüler `xvfb-run` ile alınır; ses aygıtı yoksa
oyun sessiz sürer. Yazılımsal OpenGL yavaş olduğu için `--render-last N` 3B dünyayı yalnızca son N karede çizer,
`make tablescheck TABLESCHECK_ARGS=--no-3d` de 3B çizimi atlar (ekran yoksa `make tablescheck` kendiliğinden
`xvfb-run` kullanır):

```sh
xvfb-run -a ./saklibahce --seed 5 --game pisti --autoplay --speed 2 --frames 260 --render-last 30 \
    --snapshot pisti.png --state game --view seat --no-audio
```

**Sürekli tümleştirme:** `.github/workflows/ci.yml` her push'ta Linux'ta `make test`, 3B çizimsiz `tablescheck`,
iki görüntü ve `make asan`, macOS'ta da `make test` çalıştırır (raylib 6.0 kaynaktan derlenip önbelleğe alınır).

## Kontroller

Masada, kendi sandalyende oturuyorsun; ıstakan önünde, rakiplerin karşında ve iki yanında.

| Ne | Nasıl |
|---|---|
| Etrafa bakmak | farenin **sağ tuşunu** basılı tutup sürükle |
| Yakınlaşmak | **fare tekerleği** |
| Bakışı masaya ortalamak | **R** ya da sağ tuşa çift tıklama (tavlada R zar atar: orada yalnızca çift tıklama) |
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
- **101 kuralları (Ayarlar → Oyun → Diğer 101 kuralları):** masanın usulü seçilir: açma sınırı (51/81/101/121),
  bitiş katları (katlanır / tek kat / katsız), açmayan yazar (202/404), okey ve işlek taş cezası, geri verme cezası,
  açınca bir tur bekleme. Varsayılanlar eskisi gibidir; hesap kağıdı her elin katını (×2, ×4) kırmızı kalemle
  yazar. Ayrıntılar ve kaynaklar: `docs/kurallar_101.md`.
- **Kaynak:** Kurallar Türkiye'deki yaygın 101 (Zynga 101 Okey Plus ve Digitoy Yüzbir SSS'leri, kahvehane
  usulü) ile karşılaştırıldı. Bölgeden bölgeye değişen kurallar: yandan alınıp kullanılamayan taş burada cezasız
  geri verilir; yandan alınan taşla açılınca atana taş sayısının 10/20 katı ceza yazılır.

Kuralların tamamı oyunun içinde **Kurallar** ekranında örneklerle anlatılır.

## Kahvehane Defteri, İpucu, Rehber, Devam Et

- **İstatistik** (giriş ekranı): her oyun için maç, galibiyet, el, en uzun seri ve rekor el; galibiyetlerle
  kazanılan bir **rütbe** (Yeni Gelen'den Efsane'ye). Yapay zekanın oynadığı maçlar deftere yazılmaz.
- **İpucu** düğmesi (ya da **H**): Kurt'un senin yerinde ne yapacağını masada gösterir.
- **Oyun rehberi**: her oyunun ilk maçında kısa bir anlatım; Ayarlar'dan kapatılır, yeniden açınca hepsi tekrar gelir.
- **Devam Et**: yarım bırakılan maç (menüye dönünce, oyunu kapatınca) kaydedilir; giriş ekranından kaldığın yerden
  sürer. Yeni maç başlatınca eski kayıt silinir. Kayıt `~/Library/Application Support/SakliBahce/kayit.txt`.

- **Tekrarlar** (İstatistik ekranında): biten son 20 maç saklanır; **İzle** maçı baştan oynatır (Boşluk durdurur,
  sol / sağ ok hızı değiştirir), **Analiz** o maçın hatalarını gösterir.
- **Hatalarım**: maç bitince Kurt maçı baştan oynayıp en pahalı üç hatanı söyler ("Kırmızı 12'yi attın, Hacı Rıza
  onu alıp açtı. Kurt Sarı 3'ü atardı").
- **Hafıza**: müdavimler seni hatırlar: uzun süre gelmeyince, rövanş isterken, marsı ya da okeyle bitişi anarken,
  rütbe atlayınca laf atarlar (`hafiza.txt`).
- **Başarımlar** (İstatistik ekranında): her oyundan 40 rozet ("Pişti Üstüne Pişti", "Kurt'u Marsla Yen", "Batakta 13
  El", "Müdavim", iki de gizli olan); kilitliler soru işaretiyle, açılanlar tarihiyle, sayılanlar ilerleme çubuğuyla.
  Masada açılınca üstte bir şerit ve zil, bir müdavim tebrik eder, salon alkışlar. Yalnızca kendin oynadığın maçlar
  sayılır (Yapay Zeka, tekrar ve kendi kendine oynayan çalıştırmalar sayılmaz). Kayıt `basarimlar.txt`. El sonunda açılan
  rozette puan çizelgesi biraz bekler: kutlama masanın üstünde görünür.
- **Konuşma sesleri**: konuşma balonlarına her karakterin kendi sesiyle bir mırıltı eşlik eder (Ayarlar → Görünüm · Ses).
  Mırıltı Türkçenin vurgusunu ve ezgisini taşır (soruda yükselir, ünlemde parlar, "…"da söner); kahkaha, "hıh",
  "of", "tüh", "cık", "hmm" gibi sesler kelime gibi değil, kendi sesleriyle çıkar. Kel Mahmut gür ve hırıltılı, Emekli
  Nuri yavaş ve titrek, Hacı Rıza sıcak konuşur.
- **Yüz ifadeleri**: müdavimlerin kaşları, göz kapakları, ağız kenarları ve bıyıkları oyuna göre değişir: kazanınca
  güler, kötü taşta kızar, büyük bir hamlede şaşırır, sırası gelince düşünür; adı geçen laf yiyince tepki verir. Kel
  Mahmut çabuk parlar, kahkahayı basar; Emekli Nuri pek belli etmez. Konuşurken ağızları mırıltıyla (ses kapalıysa
  yazının hecelerine göre) açılıp kapanır.
- **Kendi ellerin** (Ayarlar → Sen): ekranın altında kendi ellerin ve kolların. Taş çekerken el desteye ya da soldaki
  yığına uzanır, taşı ıstakaya getirir, atarken taşı alıp yere bırakır, sürüklediğin taşı izler; kâğıt oyunlarında sol
  elin yelpazeyi tutar, sağ elin kâğıdı atar; tavlada zarı avucundan atar, pulları taşırsın. Boşken eller masanın
  kenarında durur; arada bir çayından bir yudum alır, tespihini çevirir, istersen sigaranı içersin. Menü ya da çizelge
  açılınca eller aşağı iner. Kıyafet (ceket, kolları sıvalı gömlek, kazak) ve rengi, ten rengi, yüzük, saat, tespih,
  kendi çay bardağın (ince belli ya da renkli) ve sigara (varsayılan kapalı) seçilir; "Ellerimi göster" ile kapanır.

- **Vakit ve mevsim** (Ayarlar → Görünüm · Ses): sabah güneşi, öğle, akşam, gece; ilkbahar çiçekleri, yaz, sonbahar
  yaprakları, kışın kar ve atkılar. "Otomatik" bilgisayarın saatine ve tarihine uyar.
- **Mekân: İçerisi / Bahçe / Otomatik** (Ayarlar → Görünüm · Ses): kahvehanenin bahçesi, koca bir çınarın altında.
  Asma çardağı, sardunyalar, fesleğenler, ampul zinciri, çay ocağı, kediler, serçeler, güvercinler, denizin üstünde
  martılar. Güneşli günlerde yaprakların gölgesi masada gezinir; akşam altın ışık, gece ampuller. Sonbaharda yapraklar
  dökülür, baharda erguvan açar; yağmurda ve kışın tente gerilir (kışın mangal yanar). "Otomatik": bahar ve yaz
  günlerinde ve akşamlarında, yağmur yoksa bahçe; gece, sonbahar ve kış içerisi. Bahçedeyken yağmur başlarsa
  "Otomatik"te el bitince (hamlenin ortasında asla) kısa bir kararmayla içeri geçilir ("Yağmur başladı, içeri
  geçelim!"); masa ve oyun olduğu gibi kalır. "Bahçe" seçiliyse tente iner, yağmur tentenin üstünde tıpırdar.
- **Özel günler: Açık / Kapalı** (Ayarlar → Görünüm · Ses): tarihe göre. **Bayram**da (Ramazan ve Kurban) bayrak ve
  flamalar, masalarda lokum ve akide şekeri, müdavimler kravatlı, karanfilli; "Bayramınız mübarek olsun!". **Ramazan
  akşamları** iftardan sonra kahvehane dolar, masalarda güllaç ve baklava, gece uzaktan sahur davulu geçer. **Pazar
  akşamı derbi**: "Bu akşam derbi" afişi, televizyonun üstünde atkı, iki takımın flamaları, ekranın önünde ayakta
  izleyenler; gollerde kahvehane ayağa kalkar. Bahçede ocakçı eski portatif televizyonu dışarı çıkarır; "Otomatik"te
  derbi gecesi herkes içeride, televizyonun karşısında.
- **Seyirciler ve çay ocağı**: uzun bir maçta kapıdan girenler Kel Mahmut'un arkasında durup izler; büyük bir bitişte
  ya da marsta öteki masalar "Vay be!" diye bağırır. Maçı kazanan herkese çay ısmarlar, çırak tepsiyle dolaşır.

### Klavye ve erişilebilirlik

Her oyun yalnızca klavyeyle oynanabilir: okeyde ok tuşları ıstakada gezer, **Boşluk** taşı alıp taşır, **D** desteden
çeker, **A** soldakini alır, **Enter** atar, **O** el açar, **I** taşın işleneceği peri bulur. Kâğıt oyunlarında oklar ve
Enter, düğmeler için Tab ya da 1–9; tavlada oklar pul ve hedef seçer, **R** zar atar, **U** geri alır; damada sol / sağ
ok taşı, yukarı / aşağı ok gideceği yeri seçer, **Enter** oynar, **H** ipucu verir; konkende oklar kâğıt seçer, **Shift+ok**
kâğıdı yelpazede taşır, **Boşluk** işaretler, **D** / **A** desteden / yerden çeker, **O** açar, **I** işler, **G** geri verir,
**Enter** atar; bezikte el alınca açılan deklarasyon panelinde oklar seçer, **Enter** söyler (önündeki açık kâğıtlar da
oklarla seçilip oynanır). Ayarlar'da
**Renk körü modu** (taşlarda renk başına bir şekil, dört renkli deste) ve **Büyük yazı** var.

## Rakipler

| Koltuk | Oyuncu | |
|---|---|---|
| sağ | **Hacı Rıza** | kasketli, gri bıyıklı, tespihli; sakin, ata sözleriyle konuşur |
| karşı | **Kel Mahmut** | iri, kara bıyıklı, çizgili gömlekli; sigara içer, gürültülü bir futbol sevdalısı |
| sol | **Emekli Nuri** | ak saçlı, gözlüklü, hırkalı; oralet içer, "bizim zamanımızda…" diye söylenir |

Her birinin kendi oyun tarzı var: **Kel Mahmut** atak oynar (soldan taşı kolay alır, batakta yüksek söyler, King'de
kozu erken seçer), **Emekli Nuri** temkinlidir (sağına taş vermekten kaçınır, ihaleye zor girer), **Hacı Rıza** ise
dengelidir.

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
| `--resume` | kayıtlı yarım maça doğrudan devam et |
| `--watch` | en son biten maçın tekrarını izle |
| `--analyze` | en son biten maçın "Hatalarım" ekranını aç |
| `--katlamali` | katlamalı oyun: her açan, öncekinden en az 1 fazlasıyla açar (yalnızca o oturum için) |
| `--game G` | oyun: `101`, `esli`, `okey`, `tavla`, `pisti`, `batak`, `king`, `dama`, `66` (altmışaltı) ya da `konken` |
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
| `--render-last N` | (`--snapshot` ile) 3B dünyayı yalnızca son N karede çiz (yazılımsal GL'de, bulutta hızlı) |
| `--state S` | (`--snapshot` ile) `title`, `game`, `summary`, `matchover`, `rules`, `settings`, `games` ya da `stats` |
| `--view V` | (`--snapshot` ile) `seat`, `left`, `right`, `back` ya da `corner` (tavlada `back`: rakibin arkasından) |
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
| `src/core/` | oyunların kuralları ve botları, raylib kullanmaz: 101 ve okey (`Game`, `Meld`, `Solver`, `OkeyHand`, `Bot`), tavla (`Tavla`, `TavlaBot`), iskambil (`Cards`), pişti (`Pisti`, `PistiBot`), batak (`Batak`, `BatakBot`), king (`King`, `KingBot`), dama (`Dama`, `DamaBot`), bezik (`Bezik`, `BezikBot`), altmışaltı (`Altmisalti`, `AltmisaltiBot`) |
| `src/r3d/` | 3B dünya: çizici (`Gfx`), kahvehane (`Room`), insanlar (`Characters`), masa, taşlar ve oyuncu kamerası (`Table3D`, `PlayerCamera`), kâğıtlar (`Cards3D`), tavla tahtası (`Tavla3D`), dama tahtası (`Dama3D`), öteki oyunların HUD'u (`GameHud`) |
| `src/ui/` | menüler, oyun seçimi ve skor kâğıdı (`Screens`), efektler, ortam sesi ve radyo (`Audio`), muhabbet (`Banter`), yazı tipleri ve düğmeler (`Common`), taş yüzleri (`TileRender`), kâğıt yüzleri (`CardRender`) |
| `src/app/` | pencere, oyun döngüsü, maç akışı, bot temposu, komut satırı; tavla ve kâğıt oyunlarının masaları (`TableGame`, `CardTable`, `TavlaTable`, `PistiTable`, `BatakTable`, `KingTable`, `BezikTable`, `AltmisaltiTable`) |
| `docs/` | tavla, pişti, batak, king, dama, bezik ve altmışaltının kural metinleri ve kaynakları (oyuna `tools/gen_rules.py` ile gömülür) |
| `tests/` | her oyunun motor ve yapay zekâ testleri, bot-bot simülasyonları (`sim`, `tavla_sim`, `pisti_sim`, `batak_sim`, `king_sim`, `dama_sim`, `bezik_sim`, `altmisalti_sim`) |
| `tools/` | geliştirme sırasında kullanılan görüntü alma araçları (oyuna derlenmez) |

Tasarım belgeleri: `DESIGN.md` (kurallar, motor, botlar, ses) ve `DESIGN3D.md` (3B dünya).
