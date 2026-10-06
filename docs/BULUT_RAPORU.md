# Bulut iş planı — ara rapor (2026-10-06)

`docs/BULUT_PLANI.md`'deki planın ilk bulut oturumunun raporu. Önerilen sıra 0 → 1 → 2 → 7 → 3 → 4 → 5 → 6 idi;
**0, 1 ve 2 bitti ve main'e birleştirildi**, 7 yarıda kaldı (aşağıda ayrıntı), 3–6'ya başlanmadı.

## Yapılanlar

### Madde 0 — Linux derlemesi ve CI (`ff43128`)
- Makefile `uname` ile Linux'u tanıyor: raylib 6.0 `/usr/local`'dan (kaynaktan kurulur), `-lGL -lm -lpthread -ldl -lrt -lX11`.
- `r3d/Gfx.cpp` Linux'ta GL 3.3 prototiplerini `GL/glext.h`'den alıyor; `core/OkeyHand.cpp`'ye eksik `<cstdint>`.
- macOS yazı tipleri yoksa DejaVu / Liberation; kayıt dizini `$XDG_DATA_HOME` ya da `~/.local/share/SakliBahce/`.
- Ekransız makine: `make tablescheck` ekran yoksa kendiliğinden `xvfb-run` kullanır. Yazılımsal GL (llvmpipe) yavaş
  olduğu için iki hızlandırma eklendi, sonuçları tam çizimle birebir aynı:
  - `--render-last N`: `--snapshot`'ta 3B dünya yalnızca son N karede çizilir (`Renderer::discardFrame`).
  - `make tablescheck TABLESCHECK_ARGS=--no-3d`: 45 dk yerine ~3–4 dk.
- Ses aygıtı yoksa oyun sessiz sürüyor (denendi).
- ASan derlemesinde Kurt'un karar süresi denetimi (`test_ai`) yalnızca raporlanıyor (sanitizer kodu yavaşlatıyor).
- GitHub Actions (`.github/workflows/ci.yml`): Linux'ta `make test`, `tablescheck --no-3d`, iki görüntü (artifact),
  `make asan`; macOS'ta `make test`. raylib 6.0 kaynaktan derlenip önbelleğe alınıyor. **Üç commit'te de CI yeşil.**

### Madde 1 — Kâğıtlar kâğıt gibi oynanıyor (`e40b7c4`)
- Dağıtım bir durum makinesi (`CardTableBase::DealAnim`): karıştırma (riffle) + kesme (Pişti'de alt kâğıt bir an
  gösterilir, yalnızca görsel), kâğıtlar teker teker keçede kayarak herkesin önünde küçük yığına gider, sonra herkes
  yığınını alıp eline yelpaze yapar.
- Rıza, Mahmut, Nuri kâğıtları sol ellerinde yelpaze tutar (`Characters::holdCards` / `cardFan`, yeni
  `src/r3d/CharactersCards.cpp`); yelpaze ele bağlı (`Cards3D::follow`), kâğıt eksildikçe toplanır; düşünürken
  öne eğilince el de gövdeyle gelir. Kâğıt tutan el yudum/jest/tespih yapmaz.
- Sağ el yelpazeden kâğıt çekip masaya koyar ya da atar (`playCard`); eli alan löveyi eliyle toplar (`gatherCards`);
  Pişti'de yerdekiler süpürülür, ortaya atılan kâğıt kendi açısıyla dağınık düşer, pişti'de kahvehane tepki verir.
- Oyuncunun yelpazesi gözün altında alçakta; üzerine gelince kâğıt kalkıp eğilir; tık ya da masaya sürükle-bırak.
- Yeni sentez sesler: `CardSlide`, `CardGather`, `CardSnap` (`assets/sesler/KAYNAKLAR.md`).
- `tools/cards_snapshot.cpp` (`make cardsnapshot`): ellerin yakın çekimleri, hızlı görsel yineleme için.

### Madde 2 — Tavla kendi masasında (`b1ea327`)
- Oyuncunun sandalyesinin arkasında, sedirin önünde iki kişilik tavla masası (`w3d::TAVLA_TABLE`, `tavlaFrame()`,
  90° dönük; oyuncu sokak kapısına bakar). Room masayı, 2 sandalyeyi, lambayı, kül tablasını kurar; kedi ve çaycı
  onu engel bilir.
- `TableGame::location()` → `App::setLocation`: anahtar ışık tavla lambasına geçer (`Room::setTavlaFocus`), rakip
  karşıya oturur ve bardaklar taşınır (`Characters::setTavlaTable`, yeni `src/r3d/CharactersTavla.cpp`), kamera yeni
  koltuktan (`PlayerCamera::setTavlaSeat`), 0.7 s kararma geçişi. Tavlada R yalnızca zar atar (bilinen sorun çözüldü).
- Rakip seçilebilir: Ayarlar → Tavla → Rakip (`tavlarakip` 1..3, varsayılan Kel Mahmut); replikler rakibe göre
  (`TavlaTable.cpp`'deki `kOpp*` tabloları). Öteki ikisi okey masasında kalıp seyreder; uzun maçta seyirciler tavla
  rakibinin arkasında durur; çaycı oraya servis eder. Giriş ekranı kamera yolu yeni lambadan uzak geçer.
- `Tavla3D::setFrame` (çizim + ray seçimi çerçevede); `tables_check` tavlayı yeni koltuktan oynar; `--view back`
  tavlada rakibin arkasından.

### Nasıl test edildi
- Her madde sonunda: `make test` (hepsi 0 hata), `make tablescheck` (Madde 0'da tam 3B, sonra `--no-3d`; hepsi
  "0 failures"), `make asan` (Linux'ta geçti), CI (Linux + ASan + macOS) yeşil.
- `xvfb-run` altında başlık, okey, Pişti, Batak, King, tavla (seat/back/corner/left), ayarlar ekranı görüntüleri
  alınıp incelendi; kâğıt elleri `cards_snapshot` yakın çekimleriyle ayarlandı.

## Yarıda kalan: Madde 7 — App.cpp'yi bölmek

- Bölme betikle yapıldı ve derlendi (uyarısız), ama **commit edilmedi**: piksel karşılaştırması bitmedi.
  Yama ve betik main'de duruyor: `docs/wip/madde7_app_bolme.patch` (Makefile + yeni dosyalar),
  `docs/wip/madde7_app_bolme.py` (depo kökünden `python3 docs/wip/madde7_app_bolme.py` çalıştırılır; `src/app/App.cpp`'yi
  okuyup `AppInternal.h`, `App.cpp`, `AppFlow.cpp`, `AppBots.cpp`, `AppRecord.cpp`, `AppSettings.cpp`,
  `AppAnalysis.cpp`, `AppDebug.cpp` yazar). Ayrıca Makefile'da `TABLES_CHECK_OBJ` filtresi `App%.o` olmalı.
  App sınıfı anonim ad alanından `app::detail`'e taşınıyor; kod satırları değiştirilmeden kopyalanıyor.
- **Açık soru:** Bölmeden önce aynı tohumla iki koşu birebir aynıydı (yalnızca gerçek saati gösteren duvar saati
  farklı). Bölmeden sonraki koşuda oyun durumu aynıydı (aynı kâğıtlar, aynı balonlar) ama Batak/King/Pişti/okey/tavla
  görüntülerinde **karakterlerin boşta pozları ve duman parçacık sayısı farklı** çıktı. Sonraki adım: bölünmüş ikiliyle
  ikinci bir koşu alıp kendi içinde tutarlı mı bakmak. Tutarlıysa davranış farkı vardır (şüpheliler: üye/statik
  başlatma sırası, `static` yerel değişkenler, dosyalar arası `static` sayaçlar); tutarsızsa önceki A/A2 eşitliği
  şanstı ve koşular arası bir belirsizlik kaynağı (iş parçacığı, zamanlama) aranmalı.
- Karşılaştırma araçları (yeniden yazılması kolay): sabit tohumlu görüntü listesi — başlık, okey 101 (seat/corner),
  summary, matchover, rules, settings, games, stats, tavla, pisti, batak, king, okey; hepsi
  `--no-audio --set vakit=4 --set mevsim=3 --render-last 15`; PIL ile `ImageChops.difference`.
- Madde 7'nin geri kalanı: derleyici uyarılarını temizlemek (ör. `core/Game.cpp`'de `-Wmissing-field-initializers`),
  isteğe bağlı `clang-tidy` hedefi, `tools/screens_snapshot` öncesi/sonrası karşılaştırması.

## Kalan işler (başlanmadı)

- **Madde 3 — Yeni oyunlar:** Dama (iki kişilik masayı kullanacak: `TableGame::location()` = 1 hazır), 66, Bezik,
  Konken, tavla varyantları (Gülbahar, Moultezim/Fevga). Her biri motor + 3 seviyeli bot + masa + test + sim +
  `docs/kurallar_*.md` + `tools/gen_rules` + bütün entegrasyonlar (`saveState`/`restoreState`/`setReplayMode`/
  `replayStep`/`humanHandScore`, `analysis::analyzeMatch`, `App::guideFor`, `ui::StatsBook`, `ui::Memory`,
  klavye, renk körü, büyük yazı).
- **Madde 4 — Bahçe** (çınar altı açık mekân, Mekân ayarı, vakit/mevsim bağlantısı, performans ölçümü).
- **Madde 5 — Başarımlar** (İstatistik'te sekme, 25–40 rozet, ayrı dosya, masada kutlama).
- **Madde 6 — Oyuncu karakteri** (alttan görünen kendi ellerimiz, Ayarlar → "Sen" kişiselleştirme).
- Madde 2'nin isteğe bağlı parçası: iki kişilik Pişti'nin de tavla masasında oynanması (yapılmadı).

## Bilinen küçük sorunlar / notlar

- `tools/table3d_snapshot`'taki iki eski denetim (plandaki "Known small issues") henüz düzeltilmedi; `Audio::speak`
  maliyetine bakılmadı.
- Kâğıt oyunlarında dağıtım artık daha uzun sürüyor (karıştırma + tek tek dağıtım + toplama; normal hızda ~4–5 s);
  hepsi animasyon hızı ve `--speed` ile ölçekleniyor.
- Bulut ortamında hazırlık: `apt-get install` ile X11/GL geliştirme paketleri, `libclang-rt-18-dev` (ASan), raylib 6.0
  kaynaktan `/usr/local`'a; ayrıntılar README'nin "Linux" bölümünde. Görüntüler için `--render-last`, tablescheck için
  `TABLESCHECK_ARGS=--no-3d` kullanın.
