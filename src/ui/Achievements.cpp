// Başarımlar (see Achievements.h).
#include "ui/Achievements.h"
#include "ui/SaveFile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace ui {

namespace {

using I = BadgeIcon;

// The badges. Ids are stable (the file and the tables' events use them); names and lines can change.
const AchievementDef kDefs[] = {
    // ---- taş ve tahta: 101, okey, tavla, dama
    {"okey_bitis", "Okeyle Bitiş", "Son taşın olarak okeyi atıp elini bitir.", I::Okey, 1, 0, false},
    {"cift_bitis", "Çifte Bitiş", "Elini çiftlerle bitir.", I::Pairs, 1, 0, false},
    {"elden_bitis", "Elden Bitiş", "101'de hiç açmadan, tek seferde açıp elini bitir.", I::Tile, 1, 0, false},
    {"gosterge", "Göstergeyi Gördüm", "Klasik okeyde göstergenin eşini göster.", I::Indicator, 1, 0, false},
    {"esli5", "Can Ciğer Ortak", "Eşli 101'de ortağınla 5 maç kazan.", I::People, 5, 0, false},
    {"tam101", "Kılı Kılına", "101'de elini tam 101 ile aç.", I::Number, 1, 0, true},
    {"mars", "Mars!", "Tavlada rakibini mars et.", I::Checker, 1, 0, false},
    {"kurt_mars", "Kurt'u Marsla Yen", "Kurt seviyesindeki rakibini tavlada mars et.", I::CheckerStar, 1, 0, false},
    {"katmerli", "Katmerli Mars", "Rakibin pulu kırıkken ya da evindeyken mars et.", I::Checkers, 1, 0, false},
    {"duses", "Düşeş!", "Tavlada kendi zarında düşeş (6-6) at.", I::Dice, 1, 0, false},
    {"gulbahar", "Gülbahar", "Gülbahar tavlada bir maç kazan.", I::Rose, 1, 0, false},
    {"fevga", "Fevga", "Fevga (Moultezim) tavlada bir maç kazan.", I::Dice, 1, 0, false},
    {"dama", "Dama Çıktı", "Dama'da bir taşını en son sıraya vardırıp dama yap.", I::Disc, 1, 0, false},
    {"dama_uclu", "Üçü Birden", "Dama'da tek hamlede üç taş al.", I::Disc, 1, 0, false},
    // ---- kâğıt oyunları
    {"pisti", "Pişti!", "İlk piştini yap.", I::Card, 1, 1, false},
    {"pisti_ustune", "Pişti Üstüne Pişti", "Bir piştinin hemen ardından sen de pişti yap.", I::Cards, 1, 1, false},
    {"vale_pisti", "Valeye Vale", "Yerdeki tek valeye valeyle pişti yap.", I::Jack, 1, 1, false},
    {"batak13", "Batakta 13 El", "Batakta bir elde on üç elin hepsini al.", I::Spade, 1, 1, false},
    {"ihale8", "Sözünün Eri", "Batakta en az 8'lik ihaleyi al ve tut.", I::CardEight, 1, 1, false},
    {"king_cezasiz", "Ceza Yemem", "King'de 10 ceza elini hiç ceza almadan bitir.", I::Crown, 10, 1, false},
    {"rifki_yok", "Rıfkı Benden Uzak", "Bir King maçını rıfkıyı hiç almadan bitir.", I::Heart, 1, 1, false},
    {"66_kapat", "Kapattım!", "Altmışaltı'da desteyi kapatıp eli kazan.", I::Deck, 1, 1, false},
    {"66_kirk", "Kırk!", "Altmışaltı'da kozun papazıyla kızını birlikte ilan et.", I::Crown, 1, 1, false},
    {"bezik", "Bezik!", "Bezik'te maça kızıyla karo valeyi birlikte ilan et.", I::Bezik, 1, 1, false},
    {"cift_bezik", "Çifte Bezik", "Bezik'te çift bezik ilan et.", I::Cards, 1, 1, false},
    {"konken_elden", "Elden Konken", "Konken'de hiç açmadan elindeki kâğıtların hepsini birden açıp bitir.", I::Fan, 1, 1,
     false},
    // ---- kahvehane
    {"ilk_galibiyet", "Hayırlı Olsun", "Kahvehanede ilk maçını kazan.", I::Trophy, 1, 2, false},
    {"her_oyun", "Her Telden", "Her oyunda en az bir maç kazan.", I::Star, 0, 2, false},
    {"seri10", "On Numara", "Üst üste 10 maç kazan.", I::Flame, 10, 2, false},
    {"kurt_yen", "Kurt Avcısı", "Kurt seviyesindeki rakiplere karşı bir maç kazan.", I::Medal, 1, 2, false},
    {"mudavimler", "Hepsini Yendim", "Hacı Rıza'yı, Kel Mahmut'u ve Emekli Nuri'yi birer maçta yen.", I::People, 3, 2,
     false},
    {"cay10", "Çaylar Benden", "Maçı kazanıp masaya 10 kez çay ısmarla.", I::Tea, 10, 2, false},
    {"ziyaret7", "Müdavim", "7 gün üst üste kahvehaneye gelip oyna.", I::Calendar, 7, 2, false},
    {"gece_sabah", "Gece Kuşu, Sabah Kuşu", "Bir maçı gece, birini sabah bitir.", I::Moon, 2, 2, false},
    {"dort_mevsim", "Dört Mevsim", "Dört mevsimin her birinde bir maç bitir.", I::Leaf, 4, 2, false},
    {"hatasiz", "Kusursuz", "Kurt'un \"Hatalarım\"da tek hata bulamadığı bir maç oyna.", I::Pencil, 1, 2, false},
    {"yz_izle", "Seyirci", "Bir maçı baştan sona Yapay Zeka'ya oynatıp izle.", I::Eye, 1, 2, false},
    {"mac100", "Emektar", "Kahvehanede 100 maç bitir.", I::Hourglass, 100, 2, false},
    {"sabaha_karsi", "Sabaha Karşı", "Gece üçle beş arasında bir maç bitir.", I::Sunrise, 1, 2, true},
    {"kara_gun", "Bugün de Olmadı", "Üst üste 5 maç kaybet. Olur böyle günler.", I::Cloud, 5, 2, true},
};
constexpr int kCount = (int)(sizeof kDefs / sizeof kDefs[0]);

int popcount(unsigned v) {
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

// The badges App counts itself (from matches, hands, days): never opened by a table's event.
bool appOnly(const AchievementDef& d) {
    const std::string id = d.id;
    return d.group == 2 || id == "esli5" || id == "kurt_mars";
}

void append(std::vector<int>& to, const std::vector<int>& more) { to.insert(to.end(), more.begin(), more.end()); }

} // namespace

Achievements::Achievements() : progress_((size_t)kCount, 0), day_((size_t)kCount, -1) {}

const AchievementDef& Achievements::def(int i) { return kDefs[std::clamp(i, 0, kCount - 1)]; }
int Achievements::count() { return kCount; }
int Achievements::find(const std::string& id) {
    for (int i = 0; i < kCount; ++i)
        if (id == kDefs[i].id) return i;
    return -1;
}

int Achievements::goal(int i) const {
    if (i < 0 || i >= kCount) return 1;
    return kDefs[i].goal > 0 ? kDefs[i].goal : std::max(1, popcount(games_));
}
int Achievements::progress(int i) const { return i >= 0 && i < kCount ? progress_[(size_t)i] : 0; }
bool Achievements::unlocked(int i) const { return i >= 0 && i < kCount && day_[(size_t)i] >= 0; }
int Achievements::unlockedDay(int i) const { return i >= 0 && i < kCount ? day_[(size_t)i] : -1; }
std::string Achievements::unlockedDate(int i) const { return unlocked(i) ? dateText(day_[(size_t)i]) : std::string(); }
int Achievements::unlockedCount() const {
    int n = 0;
    for (int d : day_) n += d >= 0 ? 1 : 0;
    return n;
}

// The count of badge `i` goes up by `value` (or to at least `value`); it opens when it reaches the goal.
std::vector<int> Achievements::bump(int i, int value, bool absolute, int day) {
    if (i < 0 || i >= kCount || unlocked(i)) return {};
    int& p = progress_[(size_t)i];
    const int g = goal(i);
    p = std::min(g, absolute ? std::max(p, value) : p + std::max(0, value));
    if (p < g) return {};
    day_[(size_t)i] = std::max(0, day);
    return {i};
}

std::vector<int> Achievements::add(const std::string& id, int amount, int day) {
    if (!playing_) return {};
    return bump(find(id), amount, false, day);
}

std::vector<int> Achievements::reach(const std::string& id, int value, int day) {
    if (!playing_) return {};
    return bump(find(id), value, true, day);
}

std::vector<int> Achievements::event(const std::string& ev, int level, int day) {
    if (!playing_) return {};
    const int i = find(ev);
    if (i < 0 || appOnly(kDefs[i])) return {};
    std::vector<int> out = bump(i, 1, false, day);
    if (ev == "mars" && level >= 2) append(out, bump(find("kurt_mars"), 1, false, day));
    return out;
}

std::vector<int> Achievements::handPlayed(int day) {
    if (!playing_ || day < 0) return {};
    if (day != lastDay_) {
        dayStreak_ = lastDay_ >= 0 && day == lastDay_ + 1 ? dayStreak_ + 1 : 1;
        lastDay_ = day;
    }
    return reach("ziyaret7", dayStreak_, day);
}

std::vector<int> Achievements::matchEnd(const MatchEndInfo& m, int day) {
    if (!playing_) return {};
    std::vector<int> out;
    append(out, add("mac100", 1, day));
    if (m.phase == 0) phases_ |= 1u;
    if (m.phase == 3) phases_ |= 2u;
    append(out, reach("gece_sabah", popcount(phases_), day));
    if (m.season >= 0 && m.season < 4) seasons_ |= 1u << m.season;
    append(out, reach("dort_mevsim", popcount(seasons_), day));
    if (m.hour >= 3 && m.hour < 5) append(out, add("sabaha_karsi", 1, day));
    if (m.won) {
        loseStreak_ = 0;
        ++winStreak_;
        append(out, add("ilk_galibiyet", 1, day));
        if (m.game >= 0 && m.game < 32) wonGames_ |= 1u << m.game;
        append(out, reach("her_oyun", popcount(wonGames_ & games_), day));
        append(out, reach("seri10", winStreak_, day));
        if (m.level >= 2) append(out, add("kurt_yen", 1, day));
        beaten_ |= m.beaten & 0xEu;
        append(out, reach("mudavimler", popcount(beaten_), day));
        append(out, add("cay10", 1, day));
        if (m.game == 1) append(out, add("esli5", 1, day)); // GameKind::YuzbirEsli
    } else {
        winStreak_ = 0;
        ++loseStreak_;
        append(out, reach("kara_gun", loseStreak_, day));
    }
    return out;
}

std::vector<int> Achievements::analysis(int mistakes, int day) {
    if (!playing_ || mistakes != 0) return {};
    return add("hatasiz", 1, day);
}

std::vector<int> Achievements::watchedAi(int day) {
    if (!interactive_) return {};
    return bump(find("yz_izle"), 1, false, day);
}

// ---------------------------------------------------------------- file
// "id=progress,day" per badge that has either, then the counters behind the counts ("durum.*").
std::string Achievements::text() const {
    std::ostringstream o;
    o << "# SaklıBahçe: başarımlar\n";
    for (int i = 0; i < kCount; ++i)
        if (progress_[(size_t)i] > 0 || day_[(size_t)i] >= 0)
            o << kDefs[i].id << "=" << progress_[(size_t)i] << "," << day_[(size_t)i] << "\n";
    o << "durum.galibiyetserisi=" << winStreak_ << "\ndurum.yenilgiserisi=" << loseStreak_ << "\ndurum.songun=" << lastDay_
      << "\ndurum.gunserisi=" << dayStreak_ << "\ndurum.kazanilanoyunlar=" << wonGames_ << "\ndurum.yenilenler=" << beaten_
      << "\ndurum.saatler=" << phases_ << "\ndurum.mevsimler=" << seasons_ << "\n";
    return o.str();
}

void Achievements::parse(const std::string& text) {
    constexpr long kBig = 1000000000;
    forEachKeyValue(text, [&](const std::string& k, const std::string& v) {
        const long n = parseLong(v, -kBig, kBig);
        const unsigned u = (unsigned)parseLong(v, 0, 0xFFFFFFFFL);
        if (k == "durum.galibiyetserisi") winStreak_ = (int)std::max(0L, n);
        else if (k == "durum.yenilgiserisi") loseStreak_ = (int)std::max(0L, n);
        else if (k == "durum.songun") lastDay_ = (int)n;
        else if (k == "durum.gunserisi") dayStreak_ = (int)std::max(0L, n);
        else if (k == "durum.kazanilanoyunlar") wonGames_ = u;
        else if (k == "durum.yenilenler") beaten_ = u & 0xEu;
        else if (k == "durum.saatler") phases_ = u & 3u;
        else if (k == "durum.mevsimler") seasons_ = u & 15u;
        else {
            const int i = find(k);
            if (i < 0) return;
            const size_t comma = v.find(',');
            progress_[(size_t)i] = (int)parseLong(v, 0, 1000000);
            day_[(size_t)i] = comma == std::string::npos ? -1 : (int)parseLong(v.substr(comma + 1), -1, kBig);
        }
    });
}

void Achievements::load(const std::string& path) {
    std::string text;
    if (readFileText(path, text)) parse(text);
}

bool Achievements::save(const std::string& path) const { return writeFileAtomic(path, text()); }

int Achievements::today() { return todayDays(); }

std::string Achievements::dateText(int day) {
    if (day < 0) return {};
    // civil_from_days (Howard Hinnant)
    const int z = day + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y0 = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int y = y0 + (m <= 2 ? 1 : 0);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u.%02u.%04d", d, m, y);
    return buf;
}

} // namespace ui
