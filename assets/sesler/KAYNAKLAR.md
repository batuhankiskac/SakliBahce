# Ses efektlerinin kaynakları

Oyundaki bütün ses efektleri (taşlar, zarlar, pullar, çay bardakları, kâğıtlar, kahvehane uğultusu, yağmur, sokak)
**dosyadan okunmaz, açılışta kodla sentezlenir** (`src/ui/Audio.cpp`): gürültü patlamaları, süzgeçler ve sönümlü
tınlama kipleriyle (modal sentez). Dışarıdan alınmış bir ses örneği yoktur, dolayısıyla lisans yükümlülüğü de yoktur.

Kâğıt oyunlarının sesleri (2026-10):

| Ses | Ne zaman | Nasıl üretilir |
|---|---|---|
| `card_shuffle` | dağıtan desteyi karıştırırken | hızlanıp sönen kısa tıkırtılar (iki yarının birbirine geçmesi), ardından desteyi düzelten iki vuruş |
| `card_slide` | dağıtılan kâğıt keçenin üstünde kayarken | yükselip sönen dar bantlı gürültü, yığına değince kısa bir "fıs" |
| `card_place` | kâğıt masaya konunca | havada kısa bir hışırtı ve keçeye yumuşak bir düşüş |
| `card_snap` | atılan kâğıt keçeye çarpınca | daha parlak ve kısa bir şaklama |
| `card_slap` | pişti, hızla vurulan kâğıt | avuç vuruşu (alçak kipler) |
| `card_gather` | el ya da yerdekiler toplanınca | art arda birkaç kâğıt hışırtısı ve düzeltme vuruşu |

Müziğin kaynakları için: [`../music/KAYNAKLAR.md`](../music/KAYNAKLAR.md).
