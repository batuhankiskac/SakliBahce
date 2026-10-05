// King engine + bot tests: deal, chooser rotation, choice constraints, every contract's play rules and
// scoring, early end, koz rules, totals, event texts, determinism, bots, and a random fuzzer.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/King.cpp src/core/KingBot.cpp tests/test_king.cpp
//        -o build/king/test_king
#include "core/King.h"
#include "core/KingBot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace king;
using kart::makeCard;

namespace {

int g_checks = 0;
int g_failures = 0;

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    if (g_failures <= 60) std::printf("%s:%d: CHECK failed: %s\n", file, line, what.c_str());
    else if (g_failures == 61) std::printf("... further failures suppressed\n");
}

template <class T>
std::string show(const T& v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
std::string show(bool b) { return b ? "true" : "false"; }
std::string show(Stage s) { return std::to_string((int)s); }
std::string show(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

} // namespace

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, #cond);                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        ++g_checks;                                                                                              \
        const auto va_ = (a);                                                                                    \
        const auto vb_ = (b);                                                                                    \
        if (!(va_ == vb_))                                                                                       \
            reportFailure(__FILE__, __LINE__,                                                                    \
                          std::string(#a " == " #b "   [") + show(va_) + " vs " + show(vb_) + "]");            \
    } while (0)

namespace {

using V = std::vector<int>;
using Hands = std::array<std::vector<int>, 4>;

constexpr int MACA = kart::Maca, KUPA = kart::Kupa, KARO = kart::Karo, SINEK = kart::Sinek;
constexpr int VALE = kart::Vale, KIZ = kart::Kiz, PAPAZ = kart::Papaz, AS = kart::As;
int C(int suit, int rank) { return makeCard(suit, rank); }

// Seat s holds the whole suit s (Maça, Kupa, Karo, Sinek).
Hands suitLayout() {
    Hands h;
    for (int s = 0; s < 4; ++s)
        for (int r = 2; r <= 14; ++r) h[s].push_back(C(s, r));
    return h;
}
void swapCards(Hands& h, int a, int b) {
    for (auto& v : h)
        for (int& x : v) {
            if (x == a) x = b;
            else if (x == b) x = a;
        }
}

// A fresh match whose first deal is replaced by `h`, with `chooser` to choose.
void setup(Game& g, const Hands& h, int chooser, uint64_t seed = 7) {
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Emekli Nuri", false);
    g.setPlayer(2, "Hacı Rıza", false);
    g.setPlayer(3, "Kel Mahmut", false);
    g.startMatch(seed);
    g.debugSetHands(h);
    g.debugSetChooser(chooser);
    g.drainEvents();
}

bool hasText(const std::vector<GameEvent>& ev, EvType t, const std::string& text) {
    for (const GameEvent& e : ev)
        if (e.type == t && e.text == text) return true;
    return false;
}
const GameEvent* findEv(const std::vector<GameEvent>& ev, EvType t) {
    for (const GameEvent& e : ev)
        if (e.type == t) return &e;
    return nullptr;
}

// Plays random legal cards until the hand is over.
void playOutRandom(Game& g, okey::Rng& rng) {
    int guard = 0;
    while (g.stage() == Stage::Playing && guard++ < 100) {
        const int s = g.current();
        const V l = g.legalCards(s);
        if (l.empty()) break;
        g.playCard(s, l[rng.range((int)l.size())]);
    }
}

// ---------------------------------------------------------------------------------------------------------

void testDeal() {
    Game g;
    g.startMatch(42);
    CHECK_EQ(g.stage(), Stage::Choosing);
    CHECK_EQ(g.handIndex(), 0);
    CHECK_EQ(g.numHands(), 20);
    std::set<int> all;
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ((int)g.hand(s).size(), 13);
        CHECK(std::is_sorted(g.hand(s).begin(), g.hand(s).end()));
        for (int c : g.hand(s)) all.insert(c);
    }
    CHECK_EQ((int)all.size(), 52);
    // The Karo 2 holder chooses first.
    const int k2 = C(KARO, 2);
    int holder = -1;
    for (int s = 0; s < 4; ++s)
        if (std::count(g.hand(s).begin(), g.hand(s).end(), k2)) holder = s;
    CHECK_EQ(g.chooser(), holder);
    CHECK_EQ(g.current(), holder);
    const auto ev = g.drainEvents();
    CHECK(ev.size() >= 3);
    if (ev.size() >= 3) {
        CHECK(ev[0].type == EvType::MatchStart);
        CHECK(ev[1].type == EvType::HandStart);
        CHECK_EQ(ev[1].seat, holder);
        CHECK(ev[2].type == EvType::Deal);
    }
    // Not playing yet.
    CHECK(g.legalCards(holder).empty());
    CHECK(!g.playCard(holder, g.hand(holder)[0]).ok);
    CHECK_EQ(g.playCard(holder, g.hand(holder)[0]).error, std::string("Şu an kart atma zamanı değil"));

    // Fixed first chooser rule.
    Rules r;
    r.firstChooser = 2;
    Game g2(r);
    g2.startMatch(42);
    CHECK_EQ(g2.chooser(), 2);
}

void testRotationAndConstraints() {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.startMatch(5);
    const int first = g.chooser();
    okey::Rng rng(9);
    std::array<int, 4> chooseCount{};
    for (int h = 0; h < 20; ++h) {
        CHECK_EQ(g.handIndex(), h);
        CHECK_EQ(g.chooser(), (first + h) % 4);
        const int s = g.chooser();
        ++chooseCount[s];
        // Pick the first allowed ceza while ceza rights remain, else koz.
        const auto opts = g.options(s);
        CHECK_EQ((int)opts.size(), NUM_CONTRACTS);
        Contract pick = Contract::Koz;
        if (g.cezaLeft(s) > 0) {
            for (const ContractOption& o : opts)
                if (o.allowed && o.contract != Contract::Koz) {
                    pick = o.contract;
                    break;
                }
        }
        CHECK(g.canChoose(s, pick));
        if (g.cezaLeft(s) == 0) {
            std::string why;
            CHECK(!g.canChoose(s, Contract::ElAlmaz, &why));
            CHECK_EQ(why, std::string("Ceza hakkın bitti, koz seçmelisin"));
        }
        if (g.kozLeft(s) == 0) {
            const ActionResult r = g.chooseContract(s, Contract::Koz, MACA);
            CHECK(!r.ok);
            CHECK_EQ(r.error, std::string("Koz hakkın bitti, ceza seçmelisin"));
        }
        // Wrong seat.
        CHECK_EQ(g.chooseContract((s + 1) % 4, pick, MACA).error, std::string("Seçim sırası sende değil"));
        if (pick == Contract::Koz) CHECK_EQ(g.chooseContract(s, Contract::Koz, -1).error, std::string("Koz rengini seçmelisin"));
        CHECK(g.chooseContract(s, pick, MACA).ok);
        CHECK_EQ(g.stage(), Stage::Playing);
        CHECK_EQ(g.current(), s); // the chooser leads
        playOutRandom(g, rng);
        if (h < 19) {
            CHECK_EQ(g.stage(), Stage::HandOver);
            g.startNextHand();
        }
    }
    CHECK_EQ(g.stage(), Stage::MatchOver);
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ(chooseCount[s], 5);
        CHECK_EQ(g.kozLeft(s), 0);
        CHECK_EQ(g.cezaLeft(s), 0);
    }
    for (int c = 0; c < NUM_CEZA; ++c) CHECK_EQ(g.timesChosen((Contract)c), 2);
    CHECK_EQ(g.timesChosen(Contract::Koz), 8);
    CHECK_EQ((int)g.sheet().size(), 20);

    // Max two per penalty type; remaining counts.
    Game h;
    h.startMatch(3);
    h.debugSetTimesChosen(Contract::Rifki, 2);
    h.debugSetTimesChosen(Contract::KizAlmaz, 1);
    const int s = h.chooser();
    const auto opts = h.options(s);
    CHECK(!opts[(int)Contract::Rifki].allowed);
    CHECK_EQ(opts[(int)Contract::Rifki].remaining, 0);
    CHECK_EQ(opts[(int)Contract::Rifki].reason, std::string("Rıfkı 2 kez oynandı, artık seçilemez"));
    CHECK(opts[(int)Contract::KizAlmaz].allowed);
    CHECK_EQ(opts[(int)Contract::KizAlmaz].remaining, 1);
    CHECK_EQ(opts[(int)Contract::ElAlmaz].remaining, 2);
    CHECK_EQ(opts[(int)Contract::Koz].remaining, 2);
    CHECK(opts[(int)Contract::Koz].allowed);
    CHECK(!h.chooseContract(s, Contract::Rifki).ok);
    h.debugSetUsage(s, 2, 0);
    CHECK(!h.options(s)[(int)Contract::Koz].allowed);
    CHECK_EQ(h.options(s)[(int)Contract::Koz].remaining, 0);

    // Variant: first round penalties only.
    Rules r;
    r.firstRoundCezaOnly = true;
    Game v(r);
    v.startMatch(3);
    std::string why;
    CHECK(!v.canChoose(v.chooser(), Contract::Koz, &why));
    CHECK_EQ(why, std::string("İlk turda koz seçilemez, ceza seçmelisin"));
    v.debugSetHandIndex(4);
    CHECK(v.canChoose(v.chooser(), Contract::Koz));

    // Rules sanity.
    CHECK(Rules().valid());
    Rules bad;
    bad.maxPerCeza = 1; // 6 slots < 12 penalty choices
    CHECK(!bad.valid());
    Rules quick;
    quick.kozPerPlayer = 1;
    quick.cezaPerPlayer = 2;
    CHECK(quick.valid());
    CHECK_EQ(quick.numHands(), 12);
}

void testElAlmazAndKupaAlmaz() {
    // El almaz: seat 0 holds all spades and leads every trick -> takes all 13 tricks.
    {
        Game g;
        setup(g, suitLayout(), 0);
        CHECK(g.chooseContract(0, Contract::ElAlmaz).ok);
        okey::Rng rng(1);
        playOutRandom(g, rng);
        CHECK_EQ(g.stage(), Stage::HandOver);
        CHECK_EQ(g.sheet().back().points[0], -650);
        CHECK_EQ(g.sheet().back().tricks[0], 13);
        CHECK_EQ(g.sheet().back().units[0], 13);
        CHECK_EQ(g.total(0), -650);
        CHECK(!g.sheet().back().endedEarly);
    }
    // Kupa almaz rules.
    {
        Hands h = suitLayout();
        swapCards(h, C(MACA, 2), C(KUPA, 2)); // seat 0 gets Kupa 2, seat 1 gets Maça 2
        Game g;
        setup(g, h, 0);
        CHECK(g.chooseContract(0, Contract::KupaAlmaz).ok);
        // Leading a heart before hearts are broken.
        ActionResult r = g.playCard(0, C(KUPA, 2));
        CHECK(!r.ok);
        CHECK_EQ(r.error, std::string("Kupa henüz açılmadı, kupayla başlayamazsın"));
        CHECK(!g.isLegal(0, C(KUPA, 2)));
        CHECK(g.playCard(0, C(MACA, 5)).ok);
        // Seat 1 has Maça 2: must follow.
        r = g.playCard(1, C(KUPA, 9));
        CHECK_EQ(r.error, std::string("Elinde maça varken başka renk atamazsın"));
        CHECK_EQ(g.legalCards(1), V{C(MACA, 2)});
        CHECK(g.playCard(1, C(MACA, 2)).ok);
        // Seat 2 (diamonds only) is void: no hearts -> anything.
        CHECK_EQ((int)g.legalCards(2).size(), 13);
        CHECK(g.playCard(2, C(KARO, 9)).ok);
        CHECK(g.playCard(3, C(SINEK, 9)).ok);
        CHECK_EQ(g.lastTrick().winner, 0);
        CHECK_EQ(g.lastTrick().points, 0);
        CHECK_EQ(g.current(), 0);
        CHECK(!g.heartsBroken());
        // Seat 0 leads again; seat 1 now void in spades -> must throw a heart.
        CHECK(g.playCard(0, C(MACA, 6)).ok);
        r = g.playCard(1, C(MACA, 9));
        CHECK(!r.ok);
        CHECK_EQ(r.error, std::string("Bu kart elinde yok"));
        CHECK(g.playCard(1, C(KUPA, 14)).ok);
        CHECK(g.heartsBroken());
        CHECK(g.shownVoid(1, MACA));
        CHECK(!g.shownVoid(0, MACA));
        CHECK(g.playCard(2, C(KARO, 10)).ok);
        auto ev = g.drainEvents();
        CHECK(g.playCard(3, C(SINEK, 10)).ok);
        ev = g.drainEvents();
        CHECK_EQ(g.lastTrick().points, -30);
        CHECK_EQ(g.handPoints(0), -30);
        CHECK(hasText(ev, EvType::PenaltyCard, "Kupa Ası sen aldın -30"));
        // Now hearts are broken: Kupa 2 may be led.
        CHECK(g.isLegal(0, C(KUPA, 2)));
    }
    // Kupa almaz void rule: a void player must throw a heart.
    {
        Game g;
        setup(g, suitLayout(), 0);
        CHECK(g.chooseContract(0, Contract::KupaAlmaz).ok);
        CHECK(g.playCard(0, C(MACA, 2)).ok);
        const ActionResult r = g.playCard(1, C(KARO, 2)); // not in hand anyway
        CHECK(!r.ok);
        CHECK(g.legalCards(1).size() == 13); // seat 1 has only hearts
        CHECK(g.playCard(1, C(KUPA, 3)).ok);
        okey::Rng rng(2);
        playOutRandom(g, rng);
        CHECK_EQ(g.sheet().back().points[0], -390); // all 13 hearts fall on seat 0's tricks
        CHECK_EQ(g.sheet().back().units[0], 13);
    }
    {
        Hands h = suitLayout();
        swapCards(h, C(KARO, 2), C(KUPA, 2)); // seat 2 gets Kupa 2
        Game g;
        setup(g, h, 0);
        CHECK(g.chooseContract(0, Contract::KupaAlmaz).ok);
        CHECK(g.playCard(0, C(MACA, 2)).ok);
        CHECK(g.playCard(1, C(KUPA, 5)).ok);
        const ActionResult r = g.playCard(2, C(KARO, 5));
        CHECK_EQ(r.error, std::string("Elinde maça yok, kupa atmak zorundasın"));
        CHECK_EQ(g.legalCards(2), V{C(KUPA, 2)});
        // Variant off: free discard.
        Rules rr;
        rr.kupaAlmazVoidMustHeart = false;
        Game g2(rr);
        setup(g2, h, 0);
        CHECK(g2.chooseContract(0, Contract::KupaAlmaz).ok);
        CHECK(g2.playCard(0, C(MACA, 2)).ok);
        CHECK(g2.playCard(1, C(KUPA, 5)).ok);
        CHECK(g2.playCard(2, C(KARO, 5)).ok);
    }
    // Only hearts in hand: a heart lead is allowed before hearts are broken.
    {
        Game g;
        setup(g, suitLayout(), 1);
        CHECK(g.chooseContract(1, Contract::KupaAlmaz).ok);
        CHECK(g.playCard(1, C(KUPA, 2)).ok);
    }
}

void testKizErkek() {
    // Kız almaz: void -> must throw a Kız; early end when all four fall.
    {
        Game g;
        setup(g, suitLayout(), 0);
        CHECK(g.chooseContract(0, Contract::KizAlmaz).ok);
        CHECK(g.playCard(0, C(MACA, 2)).ok);
        ActionResult r = g.playCard(1, C(KUPA, 2));
        CHECK_EQ(r.error, std::string("Elinde maça yok, kız atmak zorundasın"));
        CHECK_EQ(g.legalCards(1), V{C(KUPA, KIZ)});
        CHECK(g.playCard(1, C(KUPA, KIZ)).ok);
        CHECK(g.playCard(2, C(KARO, KIZ)).ok);
        g.drainEvents();
        CHECK(g.playCard(3, C(SINEK, KIZ)).ok);
        auto ev = g.drainEvents();
        CHECK_EQ(g.handPoints(0), -300);
        CHECK(hasText(ev, EvType::PenaltyCard, "Kupa Kızı sen aldın -100"));
        CHECK(hasText(ev, EvType::TrickWon, "Eli sen aldın"));
        CHECK_EQ((int)g.seat(0).taken.size(), 3);
        // Seat 0 leads its own Kız: all four out -> the hand ends early.
        CHECK(g.playCard(0, C(MACA, KIZ)).ok);
        CHECK(g.playCard(1, C(KUPA, 5)).ok);
        CHECK(g.playCard(2, C(KARO, 5)).ok);
        CHECK(g.playCard(3, C(SINEK, 5)).ok);
        ev = g.drainEvents();
        CHECK_EQ(g.stage(), Stage::HandOver);
        CHECK(hasText(ev, EvType::HandEndEarly, "Bütün kızlar çıktı, el bitti"));
        CHECK(g.sheet().back().endedEarly);
        CHECK_EQ(g.sheet().back().tricksPlayed, 2);
        CHECK_EQ(g.sheet().back().points[0], -400);
        CHECK_EQ(g.sheet().back().units[0], 4);
        const GameEvent* he = findEv(ev, EvType::HandEnd);
        CHECK(he != nullptr);
        if (he) {
            CHECK_EQ(he->text, std::string("1. el bitti: Kız Almaz"));
            CHECK_EQ((int)he->lines.size(), 4);
            if (he->lines.size() == 4) {
                CHECK_EQ(he->lines[0], std::string("Sen: 4 kız, -400 (toplam -400)"));
                CHECK_EQ(he->lines[1], std::string("Emekli Nuri: temiz, 0 (toplam 0)"));
            }
        }
    }
    // Kız almaz: a higher card of the suit is on the table -> must drop the Kız of that suit.
    {
        Hands h = suitLayout();
        swapCards(h, C(MACA, KIZ), C(KUPA, 2));
        swapCards(h, C(MACA, 3), C(KUPA, 3)); // seat 1: Maça Kız, Maça 3 + hearts
        Game g;
        setup(g, h, 0);
        CHECK(g.chooseContract(0, Contract::KizAlmaz).ok);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        const ActionResult r = g.playCard(1, C(MACA, 3));
        CHECK_EQ(r.error, std::string("Yerde büyüğü varken Maça Kızı atmak zorundasın"));
        CHECK_EQ(g.legalCards(1), V{C(MACA, KIZ)});
        // Rule off -> both spades legal.
        Rules rr;
        rr.forceDropUnderHigher = false;
        Game g2(rr);
        setup(g2, h, 0);
        CHECK(g2.chooseContract(0, Contract::KizAlmaz).ok);
        CHECK(g2.playCard(0, C(MACA, AS)).ok);
        CHECK_EQ(g2.legalCards(1), (V{C(MACA, 3), C(MACA, KIZ)}));
        // A lower lead: free choice within the suit.
        Game g3;
        setup(g3, h, 0);
        CHECK(g3.chooseContract(0, Contract::KizAlmaz).ok);
        CHECK(g3.playCard(0, C(MACA, 2)).ok);
        CHECK_EQ(g3.legalCards(1), (V{C(MACA, 3), C(MACA, KIZ)}));
    }
    // Erkek almaz.
    {
        Hands h = suitLayout();
        swapCards(h, C(MACA, PAPAZ), C(KUPA, 2));
        swapCards(h, C(MACA, VALE), C(KUPA, 3));
        swapCards(h, C(MACA, 3), C(KUPA, 4)); // seat 1: Maça P, V, 3 + hearts
        Game g;
        setup(g, h, 0);
        CHECK(g.chooseContract(0, Contract::ErkekAlmaz).ok);
        CHECK(g.playCard(0, C(MACA, AS)).ok);
        ActionResult r = g.playCard(1, C(MACA, 3));
        CHECK_EQ(r.error, std::string("Yerde büyüğü varken erkeğini atmak zorundasın"));
        CHECK_EQ(g.legalCards(1), (V{C(MACA, VALE), C(MACA, PAPAZ)}));
        CHECK(g.playCard(1, C(MACA, PAPAZ)).ok);
        // Seat 2 (diamonds) void: must throw a Papaz or Vale.
        r = g.playCard(2, C(KARO, AS));
        CHECK_EQ(r.error, std::string("Elinde maça yok, erkek (papaz ya da vale) atmak zorundasın"));
        CHECK_EQ(g.legalCards(2), (V{C(KARO, VALE), C(KARO, PAPAZ)}));
        CHECK(g.playCard(2, C(KARO, VALE)).ok);
        g.drainEvents();
        CHECK(g.playCard(3, C(SINEK, PAPAZ)).ok);
        const auto ev = g.drainEvents();
        CHECK_EQ(g.lastTrick().points, -180);
        CHECK(hasText(ev, EvType::PenaltyCard, "Maça Papazı sen aldın -60"));
        CHECK(hasText(ev, EvType::PenaltyCard, "Karo Valeyi sen aldın -60"));
        okey::Rng rng(3);
        playOutRandom(g, rng);
        CHECK_EQ(g.stage(), Stage::HandOver);
        int sum = 0;
        for (int s = 0; s < 4; ++s) sum += g.sheet().back().points[s];
        CHECK_EQ(sum, -480);
    }
}

void testRifki() {
    // Void -> must throw the Rıfkı; the hand ends when it falls.
    {
        Game g;
        setup(g, suitLayout(), 0);
        CHECK(g.chooseContract(0, Contract::Rifki).ok);
        CHECK(g.playCard(0, C(MACA, 2)).ok);
        const ActionResult r = g.playCard(1, C(KUPA, 2));
        CHECK_EQ(r.error, std::string("Elinde maça yok, rıfkıyı atmak zorundasın"));
        CHECK_EQ(g.legalCards(1), V{RIFKI});
        CHECK(g.playCard(1, RIFKI).ok);
        CHECK(g.playCard(2, C(KARO, 2)).ok);
        g.drainEvents();
        CHECK(g.playCard(3, C(SINEK, 2)).ok);
        const auto ev = g.drainEvents();
        CHECK(hasText(ev, EvType::PenaltyCard, "Rıfkıyı sen aldın! -320"));
        CHECK(hasText(ev, EvType::HandEndEarly, "Rıfkı düştü, el bitti"));
        CHECK_EQ(g.stage(), Stage::HandOver);
        CHECK_EQ(g.sheet().back().points[0], -320);
        CHECK_EQ(g.sheet().back().tricksPlayed, 1);
    }
    // Bot text; only-hearts lead allowed; own Rıfkı.
    {
        Game g;
        setup(g, suitLayout(), 1);
        CHECK(g.chooseContract(1, Contract::Rifki).ok);
        CHECK(g.playCard(1, C(KUPA, AS)).ok);
        CHECK(g.playCard(2, C(KARO, 3)).ok);
        CHECK(g.playCard(3, C(SINEK, 3)).ok);
        CHECK(g.playCard(0, C(MACA, 3)).ok);
        CHECK_EQ(g.lastTrick().winner, 1);
        g.drainEvents();
        CHECK(g.playCard(1, RIFKI).ok);
        CHECK(g.playCard(2, C(KARO, 4)).ok);
        CHECK(g.playCard(3, C(SINEK, 4)).ok);
        CHECK(g.playCard(0, C(MACA, 4)).ok);
        const auto ev = g.drainEvents();
        CHECK(hasText(ev, EvType::PenaltyCard, "Emekli Nuri rıfkıyı aldı! -320"));
        CHECK(hasText(ev, EvType::TrickWon, "Emekli Nuri eli aldı"));
        CHECK_EQ(g.sheet().back().points[1], -320);
    }
    // Kupa As on the table -> the Rıfkı holder must drop it; void without Rıfkı -> must throw a heart.
    {
        Hands h = suitLayout();
        swapCards(h, C(MACA, 2), C(KUPA, AS)); // seat 0: Kupa As; seat 1: Maça 2
        swapCards(h, C(KARO, 2), C(KUPA, 3));  // seat 2: Kupa 3
        Game g;
        setup(g, h, 0);
        CHECK(g.chooseContract(0, Contract::Rifki).ok);
        CHECK_EQ(g.playCard(0, C(KUPA, AS)).error, std::string("Kupa henüz açılmadı, kupayla başlayamazsın"));
        CHECK(g.playCard(0, C(MACA, 3)).ok);
        CHECK(g.playCard(1, C(MACA, 2)).ok);
        CHECK_EQ(g.playCard(2, C(KARO, 9)).error, std::string("Elinde maça yok, kupa atmak zorundasın"));
        CHECK(g.playCard(2, C(KUPA, 3)).ok);
        CHECK(g.playCard(3, C(SINEK, 9)).ok);
        CHECK(g.heartsBroken());
        CHECK(g.playCard(0, C(KUPA, AS)).ok);
        CHECK_EQ(g.playCard(1, C(KUPA, 4)).error, std::string("Yerde büyüğü varken Kupa Papazı atmak zorundasın"));
        CHECK(g.playCard(1, RIFKI).ok);
        CHECK(g.playCard(2, C(KARO, 10)).ok);
        CHECK(g.playCard(3, C(SINEK, 10)).ok);
        CHECK_EQ(g.sheet().back().points[0], -320);
    }
}

void testSonIki() {
    Game g;
    setup(g, suitLayout(), 0);
    CHECK(g.chooseContract(0, Contract::SonIki).ok);
    okey::Rng rng(4);
    int tricksBefore = 0;
    while (g.stage() == Stage::Playing) {
        const int s = g.current();
        const V l = g.legalCards(s);
        g.playCard(s, l[rng.range((int)l.size())]);
        if (g.trickNumber() != tricksBefore) {
            tricksBefore = g.trickNumber();
            const int expect = tricksBefore >= 12 ? -180 : 0;
            CHECK_EQ(g.lastTrick().points, expect);
        }
    }
    CHECK_EQ(g.sheet().back().points[0], -360);
    CHECK_EQ(g.sheet().back().units[0], 2);
    CHECK_EQ(g.sheet().back().tricks[0], 13);
}

void testKoz() {
    Hands h = suitLayout();
    swapCards(h, C(MACA, 2), C(KUPA, 2)); // seat 0: Kupa 2; seat 1: Maça 2
    swapCards(h, C(MACA, 3), C(KARO, 2)); // seat 0: Karo 2; seat 2: Maça 3
    Game g;
    setup(g, h, 0);
    CHECK_EQ(g.chooseContract(0, Contract::Koz).error, std::string("Koz rengini seçmelisin"));
    CHECK(g.chooseContract(0, Contract::Koz, MACA).ok);
    CHECK_EQ(g.trump(), MACA);
    CHECK_EQ(g.contractLabel(), std::string("Koz (Maça)"));
    CHECK_EQ(g.playCard(0, C(MACA, AS)).error, std::string("Koz henüz açılmadı, kozla başlayamazsın"));
    CHECK(g.playCard(0, C(KUPA, 2)).ok);
    CHECK(g.playCard(1, C(KUPA, AS)).ok);
    CHECK_EQ(g.playCard(2, C(KARO, 5)).error, std::string("Elinde kupa yok, koz atmak zorundasın"));
    CHECK_EQ(g.legalCards(2), V{C(MACA, 3)});
    CHECK(g.playCard(2, C(MACA, 3)).ok);
    CHECK_EQ(g.trickWinnerSoFar(), 2);
    CHECK(g.trumpBroken());
    g.drainEvents();
    CHECK(g.playCard(3, C(SINEK, 5)).ok);
    auto ev = g.drainEvents();
    CHECK_EQ(g.lastTrick().winner, 2);
    CHECK_EQ(g.lastTrick().points, 50);
    CHECK(hasText(ev, EvType::TrickWon, "Hacı Rıza eli aldı +50"));
    CHECK_EQ(g.current(), 2);
    // Seat 2 leads Karo As: seat 3 void without trumps -> anything; seat 0 follows with Karo 2; seat 1 ruffs.
    CHECK(g.playCard(2, C(KARO, AS)).ok);
    CHECK(g.playCard(3, C(SINEK, 6)).ok);
    CHECK_EQ(g.legalCards(0), V{C(KARO, 2)});
    CHECK(g.playCard(0, C(KARO, 2)).ok);
    CHECK_EQ(g.legalCards(1), V{C(MACA, 2)});
    CHECK(g.playCard(1, C(MACA, 2)).ok);
    CHECK_EQ(g.lastTrick().winner, 1);
    // Trump broken: seat 1 has no trumps left; hand continues. Play out and check +50 per trick, sum 650.
    okey::Rng rng(5);
    playOutRandom(g, rng);
    int sum = 0;
    for (int s = 0; s < 4; ++s) {
        sum += g.sheet().back().points[s];
        CHECK_EQ(g.sheet().back().points[s], 50 * g.sheet().back().tricks[s]);
    }
    CHECK_EQ(sum, 650);

    // Trump led: must raise if possible.
    {
        Hands k;
        k[0] = {C(MACA, 5)}; // only trumps: a trump lead is allowed before trumps are broken
        k[1] = {C(MACA, 2), C(MACA, PAPAZ)};
        k[2] = {C(KARO, 3)};
        k[3] = {C(SINEK, 3)};
        Game q;
        setup(q, k, 0);
        CHECK(q.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q.playCard(0, C(MACA, 5)).ok); // seat 0 holds only spades: trump lead allowed
        CHECK_EQ(q.playCard(1, C(MACA, 2)).error, std::string("Kozu yükseltmek zorundasın: elinde daha büyük koz var"));
        CHECK_EQ(q.legalCards(1), V{C(MACA, PAPAZ)});
        CHECK(q.playCard(1, C(MACA, PAPAZ)).ok);
        Rules rr;
        rr.mustRaiseOnTrumpLead = false;
        Game q2(rr);
        setup(q2, k, 0);
        CHECK(q2.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q2.playCard(0, C(MACA, 5)).ok);
        CHECK(q2.playCard(1, C(MACA, 2)).ok);
    }
    // Ruffing over a ruff: undertrumping allowed by default; with the variant, must overtrump.
    {
        Hands k;
        k[0] = {C(KUPA, 2), C(KUPA, 5)};
        k[1] = {C(KARO, 3), C(MACA, 6)};
        k[2] = {C(SINEK, 3), C(MACA, 2), C(MACA, 9)};
        k[3] = {C(KUPA, 9)};
        Game q;
        setup(q, k, 0);
        CHECK(q.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q.playCard(0, C(KUPA, 2)).ok);
        CHECK(q.playCard(1, C(MACA, 6)).ok);
        CHECK_EQ(q.legalCards(2), (V{C(MACA, 2), C(MACA, 9)}));
        Rules rr;
        rr.mustOvertrumpWhenRuffing = true;
        Game q2(rr);
        setup(q2, k, 0);
        CHECK(q2.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q2.playCard(0, C(KUPA, 2)).ok);
        CHECK(q2.playCard(1, C(MACA, 6)).ok);
        CHECK_EQ(q2.playCard(2, C(MACA, 2)).error, std::string("Yerdeki kozdan büyük koz atmak zorundasın"));
        CHECK_EQ(q2.legalCards(2), V{C(MACA, 9)});
        // Plain suit: no need to beat by default; Batak-style variant must beat.
        Hands b;
        b[0] = {C(KARO, 9)};
        b[1] = {C(KARO, 3), C(KARO, AS)};
        b[2] = {C(KUPA, 3)};
        b[3] = {C(KUPA, 4)};
        Game q3;
        setup(q3, b, 0);
        CHECK(q3.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q3.playCard(0, C(KARO, 9)).ok);
        CHECK_EQ(q3.legalCards(1), (V{C(KARO, 3), C(KARO, AS)}));
        Rules rb;
        rb.mustBeatLedSuit = true;
        Game q4(rb);
        setup(q4, b, 0);
        CHECK(q4.chooseContract(0, Contract::Koz, MACA).ok);
        CHECK(q4.playCard(0, C(KARO, 9)).ok);
        CHECK_EQ(q4.legalCards(1), V{C(KARO, AS)});
        CHECK_EQ(q4.playCard(1, C(KARO, 3)).error, std::string("Yerdekinden büyük karo atmak zorundasın"));
    }
    // Winner function: trump beats, highest of led suit otherwise, off-suit never.
    {
        int t1[4] = {C(KUPA, 5), C(KUPA, AS), C(MACA, 2), C(KARO, AS)};
        CHECK_EQ(trickWinnerIndex(t1, 4, Contract::Koz, MACA), 2);
        CHECK_EQ(trickWinnerIndex(t1, 4, Contract::ElAlmaz, -1), 1);
        int t2[4] = {C(SINEK, 5), C(MACA, 3), C(MACA, 9), C(SINEK, 7)};
        CHECK_EQ(trickWinnerIndex(t2, 4, Contract::Koz, MACA), 2);
        CHECK_EQ(trickWinnerIndex(t2, 4, Contract::KizAlmaz, -1), 3);
    }
}

void testTextsAndTurns() {
    Game g;
    setup(g, suitLayout(), 1);
    CHECK(g.chooseContract(1, Contract::KizAlmaz).ok);
    auto ev = g.drainEvents();
    CHECK(hasText(ev, EvType::ContractChosen, "Emekli Nuri kız almaz seçti"));
    CHECK(hasText(ev, EvType::TurnStart, "Sıra Emekli Nuri'de"));
    const GameEvent* cc = findEv(ev, EvType::ContractChosen);
    CHECK(cc && cc->contract == Contract::KizAlmaz && cc->seat == 1);
    CHECK(g.playCard(1, C(KUPA, 2)).ok);
    ev = g.drainEvents();
    CHECK(hasText(ev, EvType::Play, "Emekli Nuri Kupa 2 attı"));
    CHECK(hasText(ev, EvType::TurnStart, "Sıra Hacı Rıza'da"));
    CHECK_EQ(g.playCard(0, C(MACA, 2)).error, std::string("Sıra sende değil"));
    CHECK(g.playCard(2, C(KARO, KIZ)).ok);
    CHECK(g.playCard(3, C(SINEK, KIZ)).ok);
    ev = g.drainEvents();
    CHECK(hasText(ev, EvType::TurnStart, "Sıra sende"));
    CHECK(g.playCard(0, C(MACA, KIZ)).ok);
    ev = g.drainEvents();
    CHECK(hasText(ev, EvType::Play, "Maça Kız attın"));
    CHECK(hasText(ev, EvType::PenaltyCard, "Emekli Nuri Karo Kızı aldı -100"));

    Game k;
    setup(k, suitLayout(), 0);
    CHECK(k.chooseContract(0, Contract::Koz, KUPA).ok);
    ev = k.drainEvents();
    CHECK(hasText(ev, EvType::ContractChosen, "Koz seçtin: Kupa"));
    Game k2;
    setup(k2, suitLayout(), 3);
    CHECK(k2.chooseContract(3, Contract::Koz, SINEK).ok);
    ev = k2.drainEvents();
    CHECK(hasText(ev, EvType::ContractChosen, "Kel Mahmut koz seçti: Sinek"));
    CHECK(hasText(ev, EvType::TurnStart, "Sıra Kel Mahmut'ta"));
    Game e;
    setup(e, suitLayout(), 0);
    CHECK(e.chooseContract(0, Contract::ElAlmaz).ok);
    ev = e.drainEvents();
    CHECK(hasText(ev, EvType::ContractChosen, "El almaz seçtin"));
    // HandStart text.
    Game h;
    h.setPlayer(0, "Sen", true);
    h.setPlayer(1, "Emekli Nuri", false);
    Rules r;
    r.firstChooser = 1;
    h.setRules(r);
    h.startMatch(1);
    ev = h.drainEvents();
    CHECK(hasText(ev, EvType::HandStart, "1. el — seçim sırası Emekli Nuri'de"));
    CHECK(hasText(ev, EvType::MatchStart, "Yeni parti başladı (20 el)"));
    // Son iki text.
    Game s;
    setup(s, suitLayout(), 0);
    CHECK(s.chooseContract(0, Contract::SonIki).ok);
    okey::Rng rng(1);
    std::vector<GameEvent> all;
    while (s.stage() == Stage::Playing) {
        const V l = s.legalCards(s.current());
        s.playCard(s.current(), l[rng.range((int)l.size())]);
        for (GameEvent& x : s.drainEvents()) all.push_back(x);
    }
    CHECK(hasText(all, EvType::TrickWon, "Son eli sen aldın! -180"));
    CHECK(hasText(all, EvType::TrickWon, "Sondan ikinci eli sen aldın! -180"));
    CHECK(hasText(all, EvType::TrickWon, "Eli sen aldın"));
}

// Full random matches through the public API with invariant checks.
struct FuzzStats {
    long long matches = 0, plays = 0, illegalTried = 0, early = 0;
};

void fuzzMatch(uint64_t seed, const Rules& rules, FuzzStats& fs, bool botsToo) {
    Game g(rules);
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Emekli Nuri", false);
    g.setPlayer(2, "Hacı Rıza", false);
    g.setPlayer(3, "Kel Mahmut", false);
    okey::Rng rng(seed ^ 0xABCDEFull);
    g.startMatch(seed);
    std::array<Bot, 4> bots = {Bot(BotLevel::Acemi, seed), Bot(BotLevel::Usta, seed + 1), Bot(BotLevel::Acemi, seed + 2),
                               Bot(BotLevel::Usta, seed + 3)};
    std::array<int, 4> handStartTotals{};
    int guard = 0;
    while (g.stage() != Stage::MatchOver && guard++ < 3000) {
        g.drainEvents();
        if (g.stage() == Stage::HandOver) {
            g.startNextHand();
            continue;
        }
        if (g.stage() == Stage::Choosing) {
            const int s = g.chooser();
            for (int i = 0; i < 4; ++i) handStartTotals[i] = g.total(i);
            // Hands: 13 each, disjoint.
            CardMask all = 0;
            for (int i = 0; i < 4; ++i) {
                CHECK_EQ(g.handSize(i), 13);
                all |= maskOf(g.hand(i));
            }
            CHECK_EQ(all, ALL_CARDS);
            std::vector<ContractOption> ok;
            for (const ContractOption& o : g.options(s))
                if (o.allowed) ok.push_back(o);
            CHECK(!ok.empty());
            if (ok.empty()) return;
            if (botsToo && rng.chance(0.5f)) {
                const BotAction a = bots[s].next(g, s);
                CHECK(applyBotAction(g, s, a).ok);
            } else {
                const ContractOption& o = ok[rng.range((int)ok.size())];
                CHECK(g.chooseContract(s, o.contract, rng.range(4)).ok);
            }
            continue;
        }
        // Playing.
        const int s = g.current();
        const CardMask hm = maskOf(g.hand(s));
        const TrickContext t = g.trickContext();
        const V legal = g.legalCards(s);
        CHECK(!legal.empty());
        if (legal.empty()) return;
        // legalMask and whyIllegal agree; illegal cards are rejected with a message.
        for (int c : g.hand(s)) {
            const bool isL = std::find(legal.begin(), legal.end(), c) != legal.end();
            CHECK_EQ(whyIllegal(rules, t, hm, c) == PlayRule::None, isL);
        }
        if ((int)legal.size() < g.handSize(s) && rng.chance(0.3f)) {
            for (int c : g.hand(s))
                if (std::find(legal.begin(), legal.end(), c) == legal.end()) {
                    const ActionResult r = g.playCard(s, c);
                    CHECK(!r.ok);
                    CHECK(!r.error.empty());
                    ++fs.illegalTried;
                    break;
                }
        }
        // Wrong seat rejected.
        CHECK(!g.playCard((s + 1) % 4, g.hand((s + 1) % 4).empty() ? 0 : g.hand((s + 1) % 4)[0]).ok);
        int card;
        if (botsToo && rng.chance(0.5f)) {
            const BotAction a = bots[s].next(g, s);
            CHECK(a.kind == BotAction::Kind::Play);
            card = a.card;
        } else {
            card = legal[rng.range((int)legal.size())];
        }
        // Inference principle used by the bots: every filter before the first one containing the played card
        // has no card in the player's hand.
        {
            PlayFilter f[MAX_FILTERS];
            const int k = playFilters(rules, t, f);
            for (int i = 0; i < k; ++i) {
                if (f[i].mask & cardBit(card)) break;
                CHECK((f[i].mask & hm) == 0);
            }
        }
        const int tricksBefore = g.trickNumber();
        const int handBefore = g.handIndex();
        const ActionResult r = g.playCard(s, card);
        CHECK(r.ok);
        if (!r.ok) return;
        ++fs.plays;
        if (g.handIndex() == handBefore && (g.stage() == Stage::HandOver || g.stage() == Stage::MatchOver)) {
            const HandRecord& rec = g.sheet().back();
            int sum = 0;
            for (int i = 0; i < 4; ++i) {
                sum += rec.points[i];
                CHECK_EQ(rec.totals[i], handStartTotals[i] + rec.points[i]);
                CHECK_EQ(g.total(i), rec.totals[i]);
            }
            const Contract c = rec.contract;
            int expect = 0;
            switch (c) {
            case Contract::Koz: expect = 13 * rules.unit[6]; break;
            case Contract::ElAlmaz: expect = -13 * rules.unit[0]; break;
            case Contract::SonIki: expect = -2 * rules.unit[5]; break;
            default: expect = popcount(penaltyCards(c)) * rules.points(c); break;
            }
            CHECK_EQ(sum, expect);
            if (rec.endedEarly) {
                ++fs.early;
                CHECK(isCardCeza(c));
                CHECK(rules.endEarly);
                CHECK(rec.tricksPlayed < 13);
            } else {
                CHECK_EQ(rec.tricksPlayed, 13);
            }
            int tricks = 0;
            for (int i = 0; i < 4; ++i) tricks += rec.tricks[i];
            CHECK_EQ(tricks, rec.tricksPlayed);
        } else if (g.trickNumber() > tricksBefore) {
            CHECK(g.currentTrick().cards.empty());
            CHECK_EQ(g.current(), g.lastTrick().winner);
        }
    }
    g.drainEvents();
    CHECK_EQ(g.stage(), Stage::MatchOver);
    CHECK_EQ((int)g.sheet().size(), rules.numHands());
    for (int s = 0; s < 4; ++s) {
        CHECK_EQ(g.kozLeft(s), 0);
        CHECK_EQ(g.cezaLeft(s), 0);
    }
    int sum = 0;
    for (int s = 0; s < 4; ++s) sum += g.total(s);
    const bool standard = rules.unit == Rules().unit && rules.kozPerPlayer == 2 && rules.cezaPerPlayer == 3 &&
                          rules.maxPerCeza == 2;
    if (standard) CHECK_EQ(sum, 0); // 8 x 650 = 5200 = 2 x (650 + 390 + 480 + 400 + 320 + 360)
    ++fs.matches;
}

void testFuzz() {
    FuzzStats fs;
    okey::Rng rr(77);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 2500; ++i) {
        Rules r;
        if (i % 2) { // random variants
            r.firstRoundCezaOnly = rr.chance(0.5f);
            r.endEarly = rr.chance(0.7f);
            r.voidMustDiscardPenalty = rr.chance(0.7f);
            r.kupaAlmazVoidMustHeart = rr.chance(0.7f);
            r.forceDropUnderHigher = rr.chance(0.7f);
            r.heartLeadNeedsBreak = rr.chance(0.7f);
            r.trumpLeadNeedsBreak = rr.chance(0.7f);
            r.mustTrumpWhenVoid = rr.chance(0.7f);
            r.mustRaiseOnTrumpLead = rr.chance(0.7f);
            r.mustOvertrumpWhenRuffing = rr.chance(0.4f);
            r.mustBeatLedSuit = rr.chance(0.3f);
            if (rr.chance(0.2f)) {
                r.kozPerPlayer = 1;
                r.cezaPerPlayer = 2;
            }
            r.firstChooser = rr.range(5) - 1;
        }
        fuzzMatch(1000 + (uint64_t)i, r, fs, i % 10 == 0);
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("fuzz: %lld matches, %lld plays, %lld illegal attempts rejected, %lld early ends (%.1f s)\n", fs.matches,
                fs.plays, fs.illegalTried, fs.early, s);
    CHECK_EQ(fs.matches, 2500LL);
}

// Same seed -> identical match (engine + bots).
std::vector<std::string> botMatchLog(uint64_t seed, std::array<BotLevel, 4> lv, int hands, double* maxMs = nullptr,
                                     int* rejected = nullptr) {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.setPlayer(1, "Emekli Nuri", false);
    g.setPlayer(2, "Hacı Rıza", false);
    g.setPlayer(3, "Kel Mahmut", false);
    std::array<Bot, 4> bots = {Bot(lv[0], seed + 11), Bot(lv[1], seed + 12), Bot(lv[2], seed + 13), Bot(lv[3], seed + 14)};
    g.startMatch(seed);
    std::vector<std::string> log;
    int guard = 0;
    while (g.stage() != Stage::MatchOver && guard++ < 3000) {
        for (const GameEvent& e : g.drainEvents()) {
            log.push_back(e.text);
            for (int s = 0; s < 4; ++s) bots[s].observe(e, g);
        }
        if (g.stage() == Stage::HandOver) {
            if ((int)g.sheet().size() >= hands) break;
            for (Bot& b : bots) b.resetForHand();
            g.startNextHand();
            continue;
        }
        const int s = g.stage() == Stage::Choosing ? g.chooser() : g.current();
        const auto t0 = std::chrono::steady_clock::now();
        BotAction a = bots[s].next(g, s);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (maxMs) *maxMs = std::max(*maxMs, ms);
        ActionResult r = applyBotAction(g, s, a);
        if (!r.ok) {
            if (rejected) ++*rejected;
            applyBotAction(g, s, fallbackAction(g, s));
        }
    }
    for (const GameEvent& e : g.drainEvents()) log.push_back(e.text);
    for (int s = 0; s < 4; ++s) log.push_back(std::to_string(g.total(s)));
    return log;
}

void testDeterminismAndBots() {
    const std::array<BotLevel, 4> lv = {BotLevel::Kurt, BotLevel::Usta, BotLevel::Acemi, BotLevel::Kurt};
    const auto a = botMatchLog(99, lv, 20);
    const auto b = botMatchLog(99, lv, 20);
    CHECK(a == b);
    CHECK(a.size() > 1000);
    const auto c = botMatchLog(100, lv, 20);
    CHECK(a != c);
    // Engine-only determinism: same seed, same deals.
    Game g1, g2;
    g1.startMatch(5);
    g2.startMatch(5);
    for (int s = 0; s < 4; ++s) CHECK(g1.hand(s) == g2.hand(s));
    // Every level, every seat: no rejected actions, all hands finish.
    for (int m = 0; m < 6; ++m) {
        int rejected = 0;
        double maxMs = 0;
        const std::array<BotLevel, 4> l2 = {(BotLevel)(m % 3), (BotLevel)((m + 1) % 3), (BotLevel)((m + 2) % 3),
                                            (BotLevel)(m % 3)};
        const auto log = botMatchLog(500 + (uint64_t)m, l2, 20, &maxMs, &rejected);
        CHECK_EQ(rejected, 0);
        CHECK(maxMs < 1000.0); // generous (sanitizer builds); real budgets are measured by king_sim
    }
}

void testBotFallbackAndChoice() {
    Game g;
    g.setPlayer(0, "Sen", true);
    g.startMatch(8);
    const int s = g.chooser();
    BotAction f = fallbackAction(g, s);
    CHECK(f.kind == BotAction::Kind::Choose);
    CHECK(g.canChoose(s, f.contract));
    // Bots must respect exhausted rights.
    g.debugSetUsage(s, 0, 3); // only koz left
    for (int lvl = 0; lvl < 3; ++lvl) {
        Bot b((BotLevel)lvl, 3);
        const BotAction a = b.next(g, s);
        CHECK(a.kind == BotAction::Kind::Choose);
        CHECK(a.contract == Contract::Koz);
        CHECK(a.trump >= 0 && a.trump < 4);
    }
    g.debugSetUsage(s, 2, 0); // only ceza left
    g.debugSetTimesChosen(Contract::Rifki, 2);
    for (int lvl = 0; lvl < 3; ++lvl) {
        Bot b((BotLevel)lvl, 3);
        const BotAction a = b.next(g, s);
        CHECK(a.contract != Contract::Koz && a.contract != Contract::Rifki);
        CHECK(g.canChoose(s, a.contract));
    }
    // A hand with the whole spade suit: Usta and Kurt take koz in spades when allowed.
    Game k;
    setup(k, suitLayout(), 0);
    for (int lvl = 1; lvl < 3; ++lvl) {
        Bot b((BotLevel)lvl, 4);
        const BotAction a = b.next(k, 0);
        CHECK(a.contract == Contract::Koz);
        CHECK_EQ(a.trump, MACA);
    }
}

} // namespace

int main() {
    testDeal();
    testRotationAndConstraints();
    testElAlmazAndKupaAlmaz();
    testKizErkek();
    testRifki();
    testSonIki();
    testKoz();
    testTextsAndTurns();
    testBotFallbackAndChoice();
    testDeterminismAndBots();
    testFuzz();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
