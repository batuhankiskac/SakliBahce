// Engine tests for SaklıBahçe: meld rules, game rules / scoring, and a fuzzer.
// Build: clang++ -std=c++17 -O2 -Wall -Wextra -Isrc src/core/Meld.cpp src/core/Game.cpp tests/test_engine.cpp
//        -o build/engine/test_engine
#include "core/Game.h"
#include "core/Meld.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace okey;

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
using G = std::vector<std::vector<int>>;

constexpr int Y = Yellow, B = Blue, K = Black, R = Red;
int T(int color, int number, int copy = 0) { return makeTileId(color, number, copy); }

V nums(const Meld& m) {
    V v;
    for (const PlacedTile& t : m.tiles) v.push_back(t.number);
    return v;
}
V cols(const Meld& m) {
    V v;
    for (const PlacedTile& t : m.tiles) v.push_back(t.color);
    return v;
}
bool contains(const V& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool containsIn(const G& g, int x) {
    for (const V& v : g)
        if (contains(v, x)) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------
// Meld tests. Okey = Black 5 (indicator Black 4): the two Black 5s are wild, the fakes play as Black 5.

const OkeyInfo MOK = OkeyInfo::fromIndicator(T(K, 4));
const int J1 = T(K, 5, 0), J2 = T(K, 5, 1);
const int F1 = FAKE_JOKER_A, F2 = FAKE_JOKER_B;

void testTileBasics() {
    CHECK_EQ(MOK.color, (int)Black);
    CHECK_EQ(MOK.number, 5);
    CHECK(MOK.isJoker(J1) && MOK.isJoker(J2));
    CHECK(!MOK.isJoker(F1) && !MOK.isJoker(T(K, 4)) && !MOK.isJoker(T(R, 5)));
    CHECK_EQ(MOK.faceColor(F1), (int)Black);
    CHECK_EQ(MOK.faceNumber(F2), 5);
    CHECK_EQ(MOK.handValue(J1), JOKER_IN_HAND_VALUE);
    CHECK_EQ(MOK.handValue(F1), 5);
    CHECK_EQ(MOK.handValue(T(R, 12)), 12);

    const OkeyInfo o13 = OkeyInfo::fromIndicator(T(R, 13, 1));
    CHECK_EQ(o13.color, (int)Red);
    CHECK_EQ(o13.number, 1);
    CHECK(o13.isJoker(T(R, 1, 0)) && o13.isJoker(T(R, 1, 1)));
    CHECK(!o13.isJoker(T(R, 13)));
    CHECK_EQ(o13.faceNumber(F1), 1);
    CHECK_EQ(tileNameTR(T(B, 7), o13), std::string("Mavi 7"));
    CHECK_EQ(tileNameTR(T(R, 1), o13), std::string("Okey"));
}

void testRuns() {
    Meld m;
    std::string why;

    // plain ascending / descending
    CHECK(makeRun({T(R, 3), T(R, 4), T(R, 5)}, MOK, m, true, &why));
    CHECK(m.kind == MeldKind::Run);
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK_EQ(cols(m), (V{R, R, R}));
    CHECK_EQ(m.value(), 12);
    CHECK(!m.hasJoker());
    CHECK_EQ(m.ids(), (V{T(R, 3), T(R, 4), T(R, 5)}));
    CHECK(makeRun({T(R, 5), T(R, 4), T(R, 3)}, MOK, m, false));
    CHECK_EQ(m.ids(), (V{T(R, 3), T(R, 4), T(R, 5)}));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(makeRun({T(B, 9), T(B, 10), T(B, 11), T(B, 12), T(B, 13)}, MOK, m));
    CHECK_EQ(m.value(), 55);

    // jokers at the start / middle / end (strict positional)
    CHECK(makeRun({J1, T(R, 4), T(R, 5)}, MOK, m));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(m.tiles[0].joker && m.tiles[0].id == J1 && m.tiles[0].color == R);
    CHECK(m.hasJoker());
    CHECK(makeRun({T(R, 3), J1, T(R, 5)}, MOK, m, false));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(m.tiles[1].joker && m.tiles[1].number == 4);
    CHECK(makeRun({T(R, 3), T(R, 4), J1}, MOK, m, false));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(m.tiles[2].joker);
    // descending with a trailing joker: 5-4-J means 5,4,3
    CHECK(makeRun({T(R, 5), T(R, 4), J1}, MOK, m, false));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK_EQ(m.tiles[0].id, J1);
    // two jokers
    CHECK(makeRun({J1, J2, T(R, 5)}, MOK, m));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(makeRun({T(R, 5), J1, J2}, MOK, m));
    CHECK_EQ(nums(m), (V{5, 6, 7}));
    CHECK_EQ(m.value(), 18);
    CHECK(makeRun({J1, T(R, 7), J2}, MOK, m));
    CHECK_EQ(nums(m), (V{6, 7, 8}));
    CHECK(makeRun({T(R, 1), J1, J2}, MOK, m));
    CHECK_EQ(nums(m), (V{1, 2, 3}));
    // a run ends at 13: 13-J-J is read descending (11-12-13), 12-13-J only with the okey in front
    CHECK(makeRun({T(R, 13), J1, J2}, MOK, m));
    CHECK_EQ(nums(m), (V{11, 12, 13}));
    CHECK(!makeRun({T(R, 12), T(R, 13), J1}, MOK, m, false));
    CHECK(makeRun({T(R, 12), T(R, 13), J1}, MOK, m, true));
    CHECK_EQ(nums(m), (V{11, 12, 13}));
    CHECK(m.tiles[0].joker);
    CHECK_EQ(m.value(), 36);

    // lenient re-placement
    CHECK(!makeRun({T(R, 3), T(R, 5), J1}, MOK, m, false, &why));
    CHECK_EQ(why, std::string("Sayılar ardışık değil"));
    CHECK(makeRun({T(R, 3), T(R, 5), J1}, MOK, m, true));
    CHECK_EQ(nums(m), (V{3, 4, 5}));
    CHECK(m.tiles[1].joker && m.tiles[1].id == J1);
    CHECK(makeRun({T(R, 5), T(R, 3), T(R, 4)}, MOK, m));
    CHECK_EQ(m.ids(), (V{T(R, 3), T(R, 4), T(R, 5)}));
    CHECK(makeRun({T(R, 7), J1, T(R, 8)}, MOK, m)); // spare joker goes to the high end
    CHECK_EQ(nums(m), (V{7, 8, 9}));
    CHECK(m.tiles[2].joker);
    CHECK_EQ(m.value(), 24);
    CHECK(makeRun({T(R, 12), J1, T(R, 13)}, MOK, m)); // ... up to 13, then the low end
    CHECK_EQ(nums(m), (V{11, 12, 13}));
    CHECK(m.tiles[0].joker);
    CHECK(makeRun({T(R, 13), J1, T(R, 11), T(R, 12), J2}, MOK, m)); // high end full: low end
    CHECK_EQ(nums(m), (V{9, 10, 11, 12, 13}));
    CHECK_EQ(m.value(), 9 + 10 + 11 + 12 + 13);
    CHECK(m.tiles[0].joker && m.tiles[1].joker && m.tiles[4].id == T(R, 13));
    CHECK(makeRun({T(Y, 2), J1, T(Y, 6), J2, T(Y, 4)}, MOK, m)); // two internal gaps
    CHECK_EQ(nums(m), (V{2, 3, 4, 5, 6}));

    // 12-13-1 is not a run in 101 (it is in plain okey): a run ends at 13
    CHECK(!makeRun({T(R, 12), T(R, 13), T(R, 1)}, MOK, m, false));
    CHECK(!makeRun({T(R, 1), T(R, 13), T(R, 12)}, MOK, m, false)); // written descending
    CHECK(!makeRun({T(R, 12), T(R, 13), T(R, 1)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Sayılar ardışık değil"));
    CHECK(!makeRun({J1, T(R, 13), T(R, 1)}, MOK, m, true));
    CHECK(!makeRun({T(R, 1), T(R, 12), T(R, 13)}, MOK, m, true));
    CHECK(makeRun({T(R, 1), T(R, 2), T(R, 3)}, MOK, m));
    CHECK_EQ(m.value(), 6);
    {
        V full;
        for (int n = 1; n <= 13; ++n) full.push_back(T(R, n));
        CHECK(makeRun(full, MOK, m, false));
        CHECK_EQ(m.size(), 13);
        CHECK_EQ(m.value(), 91);
        CHECK_EQ(m.tiles.front().number, 1);
        CHECK_EQ(m.tiles.back().number, 13);
        V shuffled = full;
        std::reverse(shuffled.begin(), shuffled.end());
        std::swap(shuffled[3], shuffled[7]);
        CHECK(makeRun(shuffled, MOK, m, true));
        CHECK_EQ(m.value(), 91);
        V plusOne = full;
        plusOne.push_back(T(R, 1, 1)); // 1..13 and a second 1 after 13: not a run
        CHECK(!makeRun(plusOne, MOK, m, true, &why));
        CHECK_EQ(why, std::string("Seri en fazla 13 taş olabilir"));
        full.push_back(J1);
        CHECK(!makeRun(full, MOK, m, true, &why));
        CHECK_EQ(why, std::string("Seri en fazla 13 taş olabilir"));
    }

    // no wrap-around
    CHECK(!makeRun({T(R, 13), T(R, 1), T(R, 2)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Sayılar ardışık değil"));
    CHECK(!makeRun({T(R, 12), T(R, 13), T(R, 1), T(R, 2)}, MOK, m, true));
    CHECK(!makeRun({J1, T(R, 13), T(R, 1), T(R, 2)}, MOK, m, true));
    CHECK(!makeRun({T(R, 13), T(R, 1), J1}, MOK, m, false)); // 13-14-15 / 1 after J: no

    // fake joker = okey face (Black 5), not wild
    CHECK(makeRun({T(K, 4), F1, T(K, 6)}, MOK, m, false));
    CHECK_EQ(nums(m), (V{4, 5, 6}));
    CHECK(!m.tiles[1].joker && m.tiles[1].id == F1 && m.tiles[1].color == K);
    CHECK(!m.hasJoker());
    CHECK(makeRun({F1, T(K, 6), T(K, 7)}, MOK, m));
    CHECK_EQ(nums(m), (V{5, 6, 7}));
    CHECK(makeRun({F1, J1, T(K, 7)}, MOK, m));
    CHECK_EQ(nums(m), (V{5, 6, 7}));
    CHECK(!makeRun({T(R, 4), F1, T(R, 6)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Seri aynı renkten olmalı"));
    CHECK(!makeRun({F1, F2, T(K, 6)}, MOK, m, true, &why)); // both fakes are Black 5
    CHECK_EQ(why, std::string("Seride aynı sayı iki kez olamaz"));

    // mixed colors, duplicates, sizes, bad ids
    CHECK(!makeRun({T(R, 3), T(B, 4), T(R, 5)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Seri aynı renkten olmalı"));
    CHECK(!makeRun({T(R, 3), T(R, 3, 1), T(R, 4)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Seride aynı sayı iki kez olamaz"));
    CHECK(!makeRun({T(R, 3), T(R, 4), T(R, 4, 1), T(R, 5)}, MOK, m, true));
    CHECK(!makeRun({T(R, 3), T(R, 3), T(R, 4)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Aynı taş iki kez kullanılamaz"));
    CHECK(!makeRun({T(R, 3), T(R, 4)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Seri en az 3 taş olmalı"));
    CHECK(!makeRun({}, MOK, m, true));
    CHECK(!makeRun({-1, T(R, 3), T(R, 4)}, MOK, m, true, &why));
    CHECK_EQ(why, std::string("Geçersiz taş"));
    CHECK(!makeRun({T(R, 3), T(R, 4), NUM_TILES}, MOK, m, true));
    CHECK(!makeRun({T(R, 3), T(R, 4), T(R, 7)}, MOK, m, true)); // gap without jokers

    // failure leaves `out` untouched, why may be null
    Meld keep;
    CHECK(makeRun({T(Y, 1), T(Y, 2), T(Y, 3)}, MOK, keep));
    keep.owner = 3;
    CHECK(!makeRun({T(R, 3), T(B, 4), T(R, 5)}, MOK, keep));
    CHECK_EQ(keep.owner, 3);
    CHECK_EQ(nums(keep), (V{1, 2, 3}));
}

void testGroups() {
    Meld m;
    std::string why;
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7)}, MOK, m, &why));
    CHECK(m.kind == MeldKind::Group);
    CHECK_EQ(m.value(), 21);
    CHECK_EQ(cols(m), (V{Y, B, K}));
    CHECK(makeGroup({T(R, 7), T(Y, 7), T(B, 7)}, MOK, m)); // ordered by color
    CHECK_EQ(m.ids(), (V{T(Y, 7), T(B, 7), T(R, 7)}));
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7), T(R, 7, 1)}, MOK, m));
    CHECK_EQ(m.value(), 28);
    CHECK(makeGroup({T(Y, 1), T(B, 1), T(K, 1)}, MOK, m));
    CHECK_EQ(m.value(), 3);

    // jokers take the missing colors, lowest first
    CHECK(makeGroup({T(Y, 7), T(B, 7), J1}, MOK, m));
    CHECK_EQ(cols(m), (V{Y, B, K}));
    CHECK(m.tiles[2].joker && m.tiles[2].number == 7 && m.tiles[2].id == J1);
    CHECK_EQ(m.value(), 21);
    CHECK(makeGroup({T(R, 7), J1, T(Y, 7)}, MOK, m));
    CHECK_EQ(cols(m), (V{Y, B, R}));
    CHECK_EQ(m.tiles[1].id, J1);
    CHECK(makeGroup({T(Y, 7), J1, T(K, 7), T(R, 7)}, MOK, m));
    CHECK_EQ(cols(m), (V{Y, B, K, R}));
    CHECK(m.tiles[1].joker);
    CHECK_EQ(m.value(), 28);
    CHECK(makeGroup({J1, T(R, 7), J2}, MOK, m));
    CHECK_EQ(cols(m), (V{Y, B, R}));
    CHECK_EQ(m.ids(), (V{J1, J2, T(R, 7)}));
    CHECK(makeGroup({T(Y, 7), J1, J2, T(R, 7)}, MOK, m));
    CHECK_EQ(m.ids(), (V{T(Y, 7), J1, J2, T(R, 7)}));
    CHECK_EQ(cols(m), (V{Y, B, K, R}));

    // fake joker plays as Black 5
    CHECK(makeGroup({T(Y, 5), T(B, 5), F1}, MOK, m));
    CHECK_EQ(cols(m), (V{Y, B, K}));
    CHECK(!m.hasJoker());
    CHECK(!makeGroup({T(Y, 5), F1, F2}, MOK, m, &why));
    CHECK_EQ(why, std::string("Grupta aynı renk iki kez olamaz"));
    CHECK(!makeGroup({T(Y, 6), T(B, 6), F1}, MOK, m, &why));
    CHECK_EQ(why, std::string("Gruptaki sayılar aynı olmalı"));

    // invalid groups
    CHECK(!makeGroup({T(Y, 7), T(Y, 7, 1), T(B, 7)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Grupta aynı renk iki kez olamaz"));
    CHECK(!makeGroup({T(Y, 7), T(B, 8), T(K, 7)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Gruptaki sayılar aynı olmalı"));
    CHECK(!makeGroup({T(Y, 7), T(B, 7), T(K, 7), T(R, 7), T(Y, 7, 1)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Grup en fazla 4 taş olabilir"));
    CHECK(!makeGroup({T(Y, 7), T(B, 7)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Grup en az 3 taş olmalı"));
    CHECK(!makeGroup({T(Y, 7), T(B, 7), T(K, 7), J1, J2}, MOK, m));
    CHECK(!makeGroup({T(Y, 7), T(B, 7), T(B, 7)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Aynı taş iki kez kullanılamaz"));
}

void testPairs() {
    Meld m;
    std::string why;
    CHECK(makePair({T(R, 5), T(R, 5, 1)}, MOK, m, &why));
    CHECK(m.kind == MeldKind::Pair);
    CHECK_EQ(m.value(), 10);
    CHECK(makePair({T(R, 5), J1}, MOK, m));
    CHECK(m.tiles[1].joker && m.tiles[1].color == R && m.tiles[1].number == 5);
    CHECK(makePair({J1, T(R, 5)}, MOK, m));
    CHECK(m.tiles[0].joker && m.tiles[0].color == R && m.tiles[0].number == 5);
    CHECK_EQ(m.ids(), (V{J1, T(R, 5)}));
    CHECK(makePair({J1, J2}, MOK, m));
    CHECK(m.tiles[0].color == K && m.tiles[0].number == 5 && m.tiles[1].number == 5);
    CHECK(makePair({F1, F2}, MOK, m)); // both fakes are Black 5
    CHECK(!m.hasJoker());
    CHECK(m.tiles[0].color == K && m.tiles[0].number == 5);
    CHECK(makePair({F1, J1}, MOK, m));
    CHECK(!makePair({T(R, 5), T(B, 5)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Çiftin taşları aynı olmalı"));
    CHECK(!makePair({T(R, 5), T(R, 6)}, MOK, m));
    CHECK(!makePair({F1, T(R, 5)}, MOK, m));
    CHECK(!makePair({T(R, 5)}, MOK, m, &why));
    CHECK_EQ(why, std::string("Çift iki taştan oluşmalı"));
    CHECK(!makePair({T(R, 5), T(R, 5, 1), J1}, MOK, m));
    CHECK(!makePair({T(R, 5), T(R, 5)}, MOK, m));
}

void testMakeMeld() {
    Meld m;
    std::string why;
    CHECK(makeMeld({J1, J2, T(R, 5)}, MOK, m, false)); // group 15 > run 3-4-5 (12)
    CHECK(m.kind == MeldKind::Group);
    CHECK_EQ(m.value(), 15);
    CHECK(makeMeld({T(R, 5), J1, J2}, MOK, m, false)); // run 5-6-7 (18) > group 15
    CHECK(m.kind == MeldKind::Run);
    CHECK_EQ(m.value(), 18);
    CHECK(makeMeld({T(R, 12), J1, J2}, MOK, m, false)); // group 12-12-12 (36) > run 10-11-12 (33): no 14
    CHECK(m.kind == MeldKind::Group);
    CHECK_EQ(m.value(), 36);
    CHECK(makeMeld({T(R, 1), J1, J2}, MOK, m, false)); // 1-2-3 (6) > 1-1-1 (3)
    CHECK(m.kind == MeldKind::Run);
    CHECK(makeMeld({J1, T(R, 4), J2}, MOK, m, false)); // tie 12/12 -> run
    CHECK(m.kind == MeldKind::Run);
    CHECK_EQ(m.value(), 12);
    CHECK(makeMeld({T(Y, 7), T(B, 7), T(K, 7)}, MOK, m, false));
    CHECK(m.kind == MeldKind::Group);
    CHECK(makeMeld({T(R, 3), T(R, 4), T(R, 5)}, MOK, m, false));
    CHECK(m.kind == MeldKind::Run);
    CHECK(makeMeld({T(R, 5), T(R, 5, 1)}, MOK, m, true));
    CHECK(m.kind == MeldKind::Pair);
    CHECK(!makeMeld({T(R, 3), T(R, 4), T(R, 5)}, MOK, m, true)); // pairMode -> pair only
    CHECK(!makeMeld({T(R, 5), T(R, 5, 1)}, MOK, m, false, &why));
    CHECK_EQ(why, std::string("Per en az 3 taş olmalı"));
    CHECK(!makeMeld({T(R, 5), T(R, 5, 1), T(B, 5)}, MOK, m, false, &why));
    CHECK_EQ(why, std::string("Grupta aynı renk iki kez olamaz"));
    CHECK(!makeMeld({T(R, 5), T(R, 6), T(B, 7)}, MOK, m, false, &why));
    CHECK_EQ(why, std::string("Seri aynı renkten olmalı"));
    CHECK(!makeMeld({T(R, 5), T(R, 6), T(R, 9)}, MOK, m, false, &why));
    CHECK_EQ(why, std::string("Sayılar ardışık değil"));
}

void testAddTile() {
    Meld run, out;
    CHECK(makeRun({T(R, 3), T(R, 4), T(R, 5)}, MOK, run));
    run.owner = 2;
    CHECK(tryAddTile(run, T(R, 6), MOK, AddSide::Back, out));
    CHECK_EQ(nums(out), (V{3, 4, 5, 6}));
    CHECK_EQ(out.owner, 2);
    CHECK(tryAddTile(run, T(R, 6), MOK, AddSide::Auto, out));
    CHECK_EQ(out.ids().back(), T(R, 6));
    CHECK(!tryAddTile(run, T(R, 6), MOK, AddSide::Front, out));
    CHECK(tryAddTile(run, T(R, 2), MOK, AddSide::Front, out));
    CHECK_EQ(nums(out), (V{2, 3, 4, 5}));
    CHECK_EQ(out.ids().front(), T(R, 2));
    CHECK(tryAddTile(run, T(R, 2, 1), MOK, AddSide::Auto, out));
    CHECK_EQ(out.ids().front(), T(R, 2, 1));
    CHECK(!tryAddTile(run, T(R, 2), MOK, AddSide::Back, out));
    CHECK(!tryAddTile(run, T(R, 7), MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(run, T(B, 6), MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(run, T(R, 5, 1), MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(run, T(R, 5), MOK, AddSide::Auto, out)); // already in it
    CHECK(!tryAddTile(run, -1, MOK, AddSide::Auto, out));
    // jokers: Auto prefers the back
    CHECK(tryAddTile(run, J1, MOK, AddSide::Auto, out));
    CHECK_EQ(nums(out), (V{3, 4, 5, 6}));
    CHECK(out.tiles.back().joker && out.tiles.back().color == R);
    CHECK(tryAddTile(run, J1, MOK, AddSide::Front, out));
    CHECK_EQ(nums(out), (V{2, 3, 4, 5}));
    CHECK(out.tiles.front().joker);

    Meld hi;
    CHECK(makeRun({T(B, 11), T(B, 12), T(B, 13)}, MOK, hi));
    CHECK(tryAddTile(hi, J1, MOK, AddSide::Auto, out)); // ends at 13: the joker goes to the front
    CHECK_EQ(nums(out), (V{10, 11, 12, 13}));
    CHECK(out.tiles.front().joker);
    CHECK(!tryAddTile(hi, J1, MOK, AddSide::Back, out));
    CHECK(!tryAddTile(hi, T(B, 1), MOK, AddSide::Auto, out)); // no 1 after 13
    CHECK(!tryAddTile(hi, T(B, 1), MOK, AddSide::Back, out));
    CHECK(!tryAddTile(hi, T(B, 1), MOK, AddSide::Front, out));
    CHECK(tryAddTile(hi, T(B, 10), MOK, AddSide::Auto, out));
    CHECK_EQ(nums(out), (V{10, 11, 12, 13}));
    CHECK(!fitsAnyMeld({hi}, T(B, 1), MOK)); // a '1' is never işlek on a run ending at 13

    Meld low;
    CHECK(makeRun({T(Y, 1), T(Y, 2), T(Y, 3)}, MOK, low));
    CHECK(!tryAddTile(low, J1, MOK, AddSide::Front, out));
    CHECK(tryAddTile(low, J1, MOK, AddSide::Auto, out));
    CHECK_EQ(nums(out), (V{1, 2, 3, 4}));
    CHECK(!tryAddTile(low, T(Y, 1, 1), MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(low, T(Y, 13), MOK, AddSide::Auto, out));

    Meld two;
    CHECK(makeRun({T(Y, 2), T(Y, 3), T(Y, 4)}, MOK, two));
    CHECK(tryAddTile(two, T(Y, 1), MOK, AddSide::Auto, out)); // back impossible -> front
    CHECK_EQ(nums(out), (V{1, 2, 3, 4}));

    Meld long12;
    V ids;
    for (int n = 2; n <= 13; ++n) ids.push_back(T(Y, n));
    CHECK(makeRun(ids, MOK, long12));
    CHECK(tryAddTile(long12, T(Y, 1), MOK, AddSide::Auto, out)); // back impossible (13) -> front
    CHECK_EQ(out.tiles.front().number, 1);
    CHECK(!tryAddTile(long12, T(Y, 1), MOK, AddSide::Back, out));
    Meld full = out;
    CHECK_EQ(full.size(), 13);
    CHECK(!tryAddTile(full, T(Y, 1, 1), MOK, AddSide::Auto, out)); // 1..13: nothing fits any more
    CHECK(!tryAddTile(full, J1, MOK, AddSide::Auto, out));

    // fake joker extends a black run as Black 5
    Meld blk;
    CHECK(makeRun({T(K, 2), T(K, 3), T(K, 4)}, MOK, blk));
    CHECK(tryAddTile(blk, F1, MOK, AddSide::Auto, out));
    CHECK(!out.tiles.back().joker && out.tiles.back().number == 5);

    // groups
    Meld grp;
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7)}, MOK, grp));
    grp.owner = 1;
    CHECK(tryAddTile(grp, T(R, 7), MOK, AddSide::Front, out));
    CHECK_EQ(out.size(), 4);
    CHECK_EQ(out.owner, 1);
    CHECK_EQ(cols(out), (V{Y, B, K, R}));
    CHECK(!tryAddTile(grp, T(Y, 7, 1), MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(grp, T(R, 8), MOK, AddSide::Auto, out));
    CHECK(tryAddTile(grp, J1, MOK, AddSide::Auto, out));
    CHECK(out.tiles[3].joker && out.tiles[3].color == R);
    Meld grp4;
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7), T(R, 7)}, MOK, grp4));
    CHECK(!tryAddTile(grp4, J1, MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(grp4, T(R, 7, 1), MOK, AddSide::Auto, out));
    Meld gj;
    CHECK(makeGroup({T(Y, 7), T(B, 7), J1}, MOK, gj)); // J stands for Black
    CHECK(tryAddTile(gj, T(K, 7), MOK, AddSide::Auto, out)); // joker slides to Red
    CHECK_EQ(cols(out), (V{Y, B, K, R}));
    CHECK(out.tiles[3].joker);

    // pairs never accept tiles
    Meld pr;
    CHECK(makePair({T(R, 9), T(R, 9, 1)}, MOK, pr));
    CHECK(!tryAddTile(pr, J1, MOK, AddSide::Auto, out));
    CHECK(!tryAddTile(pr, T(R, 10), MOK, AddSide::Back, out));
}

void testSwapJoker() {
    Meld run, out;
    int freed = -1;
    CHECK(makeRun({T(R, 3), J1, T(R, 5)}, MOK, run));
    run.owner = 3;
    CHECK(trySwapJoker(run, T(R, 4), MOK, out, freed));
    CHECK_EQ(freed, J1);
    CHECK_EQ(out.tiles[1].id, T(R, 4));
    CHECK(!out.tiles[1].joker && out.tiles[1].number == 4);
    CHECK(!out.hasJoker());
    CHECK_EQ(out.owner, 3);
    CHECK(trySwapJoker(run, T(R, 4, 1), MOK, out, freed));
    CHECK(!trySwapJoker(run, T(R, 6), MOK, out, freed));
    CHECK(!trySwapJoker(run, T(B, 4), MOK, out, freed));
    CHECK(!trySwapJoker(run, T(R, 3, 1), MOK, out, freed));
    CHECK(!trySwapJoker(run, J2, MOK, out, freed));

    Meld hi;
    CHECK(makeRun({T(R, 12), T(R, 13), J1}, MOK, hi)); // J-12-13: the okey stands for 11
    CHECK(!trySwapJoker(hi, T(R, 1), MOK, out, freed));
    CHECK(trySwapJoker(hi, T(R, 11), MOK, out, freed));
    CHECK_EQ(out.tiles[0].number, 11);
    CHECK_EQ(out.value(), 36);
    Meld lo;
    CHECK(makeRun({J1, T(R, 2), T(R, 3)}, MOK, lo));
    CHECK(trySwapJoker(lo, T(R, 1), MOK, out, freed));
    CHECK_EQ(out.tiles[0].number, 1);
    Meld two;
    CHECK(makeRun({T(R, 5), J1, J2}, MOK, two)); // 5 6 7
    CHECK(trySwapJoker(two, T(R, 7), MOK, out, freed));
    CHECK_EQ(freed, J2);
    CHECK(out.tiles[1].joker && !out.tiles[2].joker);
    Meld blk;
    CHECK(makeRun({T(K, 3), T(K, 4), J1}, MOK, blk)); // joker = Black 5 = fake joker's face
    CHECK(trySwapJoker(blk, F1, MOK, out, freed));
    CHECK_EQ(freed, J1);

    // groups (incl. the 3-group ambiguity)
    Meld grp;
    CHECK(makeGroup({T(Y, 7), T(B, 7), J1}, MOK, grp));
    CHECK(trySwapJoker(grp, T(K, 7), MOK, out, freed));
    CHECK_EQ(freed, J1);
    CHECK_EQ(cols(out), (V{Y, B, K}));
    CHECK(!out.hasJoker());
    CHECK(trySwapJoker(grp, T(R, 7), MOK, out, freed));
    CHECK_EQ(cols(out), (V{Y, B, R}));
    CHECK(!trySwapJoker(grp, T(Y, 7, 1), MOK, out, freed));
    CHECK(!trySwapJoker(grp, T(Y, 8), MOK, out, freed));
    Meld g4;
    CHECK(makeGroup({T(Y, 7), J1, T(K, 7), T(R, 7)}, MOK, g4));
    CHECK(trySwapJoker(g4, T(B, 7), MOK, out, freed));
    CHECK_EQ(cols(out), (V{Y, B, K, R}));
    CHECK(!trySwapJoker(g4, T(Y, 7, 1), MOK, out, freed));
    Meld g2;
    CHECK(makeGroup({J1, J2, T(R, 7)}, MOK, g2)); // J1 = Yellow, J2 = Blue
    CHECK(trySwapJoker(g2, T(B, 7), MOK, out, freed));
    CHECK_EQ(freed, J2); // the joker standing for exactly that color
    CHECK(trySwapJoker(g2, T(K, 7), MOK, out, freed));
    CHECK_EQ(freed, J1); // otherwise the first joker
    CHECK(out.hasJoker());
    CHECK_EQ(out.size(), 3);
    Meld plain;
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7)}, MOK, plain));
    CHECK(!trySwapJoker(plain, T(R, 7), MOK, out, freed));

    Meld pr;
    CHECK(makePair({T(R, 5), J1}, MOK, pr));
    CHECK(!trySwapJoker(pr, T(R, 5, 1), MOK, out, freed));
}

void testFits() {
    std::vector<Meld> table(3);
    CHECK(makeRun({T(R, 3), T(R, 4), T(R, 5)}, MOK, table[0]));
    CHECK(makeGroup({T(Y, 7), T(B, 7), T(K, 7)}, MOK, table[1]));
    CHECK(makePair({T(Y, 9), T(Y, 9, 1)}, MOK, table[2]));
    CHECK(fitsAnyMeld(table, T(R, 6), MOK));
    CHECK(fitsAnyMeld(table, T(R, 2), MOK));
    CHECK(fitsAnyMeld(table, T(R, 7), MOK));
    CHECK(!fitsAnyMeld(table, T(R, 8), MOK));
    CHECK(!fitsAnyMeld(table, T(Y, 8), MOK));
    CHECK(fitsAnyMeld(table, J1, MOK));
    std::vector<Meld> onlyPair(1, table[2]);
    CHECK(!fitsAnyMeld(onlyPair, J1, MOK));
    CHECK(!fitsAnyMeld({}, T(R, 6), MOK));
}

// ---------------------------------------------------------------------------------------------------------
// Game helpers

// Serialises every piece of observable state so rejected actions can be proven side-effect free.
V snapshot(Game& g) {
    V v;
    auto add = [&v](int x) { v.push_back(x); };
    auto addVec = [&](const V& x) {
        add((int)x.size());
        for (int i : x) add(i);
    };
    add((int)g.handState());
    add(g.handIndex());
    add(g.current());
    add((int)g.stage());
    add(g.turnNumber());
    add(g.starter());
    add(g.pendingLeftTile());
    add(g.okey().indicatorId);
    addVec(g.debugPile());
    add((int)g.discardHistory().size());
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        const PlayerInfo& p = g.player(s);
        addVec(p.hand);
        addVec(p.discards);
        add(p.opened);
        add(p.openedWithPairs);
        add(p.openedTurn);
        add(p.openValue);
        add(p.handPenalty);
        add(p.totalScore);
        addVec(p.handScores);
        add(g.canTakeFromLeft(s));
    }
    add((int)g.table().size());
    for (const Meld& m : g.table()) {
        add((int)m.kind);
        add(m.owner);
        for (const PlacedTile& t : m.tiles) {
            add(t.id);
            add(t.color);
            add(t.number);
            add(t.joker);
        }
    }
    return v;
}

bool conserved(Game& g) {
    std::vector<int> cnt(NUM_TILES, 0);
    bool ok = true;
    auto add = [&](int id) {
        if (!isValidTile(id)) ok = false;
        else ++cnt[id];
    };
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        for (int id : g.player(s).hand) add(id);
        for (int id : g.player(s).discards) add(id);
    }
    for (int id : g.debugPile()) add(id);
    for (const Meld& m : g.table())
        for (const PlacedTile& t : m.tiles) add(t.id);
    add(g.okey().indicatorId);
    for (int c : cnt)
        if (c != 1) ok = false;
    return ok;
}

bool meldConsistent(const Meld& m, const OkeyInfo& ok) {
    const int n = m.size();
    for (const PlacedTile& t : m.tiles) {
        if (!isValidTile(t.id) || t.joker != ok.isJoker(t.id)) return false;
        if (!t.joker) {
            if (ok.faceColor(t.id) != t.color) return false;
            const int f = ok.faceNumber(t.id);
            if (f != t.number) return false;
        }
    }
    if (m.kind == MeldKind::Run) {
        if (n < 3 || n > NUM_NUMBERS) return false;
        for (int i = 0; i < n; ++i) {
            if (m.tiles[i].color != m.tiles[0].color) return false;
            if (m.tiles[i].number != m.tiles[0].number + i) return false;
        }
        return m.tiles[0].number >= 1 && m.tiles[n - 1].number <= NUM_NUMBERS;
    }
    if (m.kind == MeldKind::Group) {
        if (n < 3 || n > 4) return false;
        for (int i = 0; i < n; ++i) {
            if (m.tiles[i].number != m.tiles[0].number || m.tiles[i].number > 13) return false;
            if (i > 0 && m.tiles[i].color <= m.tiles[i - 1].color) return false; // distinct, sorted
        }
        return true;
    }
    return n == 2 && m.tiles[0].color == m.tiles[1].color && m.tiles[0].number == m.tiles[1].number;
}

// Invariants that hold after every action (tile conservation, hand sizes, table sanity).
void checkInvariants(Game& g) {
    CHECK(conserved(g));
    CHECK(!isFakeJoker(g.okey().indicatorId));
    const bool playing = g.handState() == HandState::Playing;
    for (int s = 0; s < NUM_PLAYERS; ++s) {
        const int n = (int)g.player(s).hand.size();
        CHECK(n <= 22);
        if (playing) {
            if (n < 1)
                std::printf("  empty hand while playing: seat %d current %d stage %d turn %d opened %d table %d\n", s,
                            g.current(), (int)g.stage(), g.turnNumber(), (int)g.player(s).opened,
                            (int)g.table().size());
            CHECK(n >= 1);
            if (s != g.current() || g.stage() == TurnStage::NeedDraw) CHECK(n <= 21);
        }
    }
    if (g.pendingLeftTile() >= 0) {
        CHECK(playing && g.stage() == TurnStage::Play);
        CHECK(contains(g.player(g.current()).hand, g.pendingLeftTile()));
        CHECK(g.player(g.current()).hand.size() >= 2); // a discardable tile besides the pending one
    }
    if (playing && g.stage() == TurnStage::NeedDraw) CHECK(g.pileCount() >= 1);
    for (const Meld& m : g.table()) {
        CHECK(meldConsistent(m, g.okey()));
        CHECK(m.owner >= 0 && m.owner < NUM_PLAYERS);
        if (m.owner >= 0 && m.owner < NUM_PLAYERS) {
            const PlayerInfo& p = g.player(m.owner);
            CHECK(p.opened);
            // pair openers lay only pairs; a series opener's pairs need someone else who opened with pairs
            if (p.openedWithPairs) CHECK(m.kind == MeldKind::Pair);
            else if (m.kind == MeldKind::Pair) CHECK(g.pairsOpenedByOther(m.owner));
        }
    }
}

// Randomised properties of the meld builders (permutation invariance, consistency, add/swap/fits agreement).
V sortedIds(V v) {
    std::sort(v.begin(), v.end());
    return v;
}

void testMeldProperties() {
    Rng rng(20260928);
    long runs = 0, groups = 0, adds = 0, swaps = 0;
    for (int iter = 0; iter < 120000; ++iter) {
        const OkeyInfo ok = OkeyInfo::fromIndicator(rng.range(104)); // any numbered tile can be the gösterge
        // candidate tiles: run-ish, group-ish or random
        V ids;
        const int mode = rng.range(3);
        if (mode == 0) {
            const int c = rng.range(4), len = rng.range(3, 8), start = rng.range(1, 14 - len + 1);
            for (int k = 0; k < len; ++k) {
                const int n = start + k == 14 ? 1 : start + k;
                ids.push_back(makeTileId(c, n, rng.range(2)));
            }
        } else if (mode == 1) {
            const int n = rng.range(1, 13), cnt = rng.range(3, 4);
            V colors = {0, 1, 2, 3};
            rng.shuffle(colors);
            for (int k = 0; k < cnt; ++k) ids.push_back(makeTileId(colors[k], n, rng.range(2)));
        } else {
            const int cnt = rng.range(2, 6);
            for (int k = 0; k < cnt; ++k) ids.push_back(rng.range(NUM_TILES));
        }
        const int jk = rng.range(3); // replace some tiles by wild / fake jokers
        for (int k = 0; k < jk && !ids.empty(); ++k) {
            const int pos = rng.range((int)ids.size());
            const int kind = rng.range(3);
            ids[pos] = kind == 0 ? makeTileId(ok.color, ok.number, 0)
                     : kind == 1 ? makeTileId(ok.color, ok.number, 1)
                                 : FAKE_JOKER_A + rng.range(2);
        }
        if (rng.chance(0.2f) && !ids.empty()) ids[rng.range((int)ids.size())] = rng.range(NUM_TILES); // perturb
        {
            V u = sortedIds(ids);
            if (std::adjacent_find(u.begin(), u.end()) != u.end()) continue; // physical tiles are unique
        }
        rng.shuffle(ids);
        V perm = ids;
        rng.shuffle(perm);

        Meld r1, r2, g1, g2, m1;
        const bool okR1 = makeRun(ids, ok, r1, true), okR2 = makeRun(perm, ok, r2, true);
        const bool okStrict = makeRun(ids, ok, m1, false);
        CHECK_EQ(okR1, okR2); // the lenient reading only depends on the set of tiles
        if (okStrict) {
            CHECK(okR1);
            CHECK_EQ(m1.value(), r1.value()); // a valid strict reading is kept as is
        }
        const bool okG1 = makeGroup(ids, ok, g1), okG2 = makeGroup(perm, ok, g2);
        CHECK_EQ(okG1, okG2);
        if (okR1) {
            ++runs;
            CHECK(meldConsistent(r1, ok) && r1.kind == MeldKind::Run);
            CHECK_EQ(sortedIds(r1.ids()), sortedIds(ids));
            int sum = 0;
            for (const PlacedTile& t : r1.tiles) sum += t.number;
            CHECK_EQ(r1.value(), sum);
        }
        if (okG1) {
            ++groups;
            CHECK(meldConsistent(g1, ok) && g1.kind == MeldKind::Group);
            CHECK_EQ(sortedIds(g1.ids()), sortedIds(ids));
            if (!g1.hasJoker()) CHECK_EQ(g1.ids(), g2.ids()); // canonical color order
        }
        Meld best;
        const bool okM = makeMeld(ids, ok, best, false);
        CHECK_EQ(okM, okR1 || okG1);
        if (okM) CHECK_EQ(best.value(), std::max(okR1 ? r1.value() : 0, okG1 ? g1.value() : 0));
        if (ids.size() == 2) {
            Meld pr;
            const bool okP = makePair(ids, ok, pr);
            const bool expect = ok.isJoker(ids[0]) || ok.isJoker(ids[1]) ||
                                (ok.faceColor(ids[0]) == ok.faceColor(ids[1]) &&
                                 ok.faceNumber(ids[0]) == ok.faceNumber(ids[1]));
            CHECK_EQ(okP, expect);
            if (okP) CHECK(meldConsistent(pr, ok));
        }

        // işleme / joker swap against every tile
        for (int which = 0; which < 2; ++which) {
            if (which == 0 && !okR1) continue;
            if (which == 1 && !okG1) continue;
            Meld m = which == 0 ? r1 : g1;
            m.owner = rng.range(4);
            const std::vector<Meld> table(1, m);
            for (int t = 0; t < NUM_TILES; ++t) {
                Meld a, f, b;
                const bool okA = tryAddTile(m, t, ok, AddSide::Auto, a);
                CHECK_EQ(fitsAnyMeld(table, t, ok), okA);
                if (m.kind == MeldKind::Run) {
                    const bool okF = tryAddTile(m, t, ok, AddSide::Front, f);
                    const bool okB = tryAddTile(m, t, ok, AddSide::Back, b);
                    CHECK_EQ(okA, okF || okB);
                    if (okB) CHECK(meldConsistent(b, ok) && b.tiles.back().id == t);
                    if (okF) CHECK(meldConsistent(f, ok) && f.tiles.front().id == t);
                }
                if (okA) {
                    ++adds;
                    CHECK(meldConsistent(a, ok));
                    CHECK_EQ(a.size(), m.size() + 1);
                    CHECK_EQ(a.owner, m.owner);
                    CHECK(contains(a.ids(), t));
                }
                Meld s;
                int freed = -1;
                if (trySwapJoker(m, t, ok, s, freed)) {
                    ++swaps;
                    CHECK(meldConsistent(s, ok));
                    CHECK_EQ(s.size(), m.size());
                    CHECK_EQ(s.owner, m.owner);
                    CHECK(contains(s.ids(), t) && !contains(s.ids(), freed));
                    CHECK(ok.isJoker(freed) && contains(m.ids(), freed));
                    CHECK_EQ(s.value(), m.value());
                    CHECK(!ok.isJoker(t));
                }
            }
        }
    }
    CHECK(runs > 1000 && groups > 1000 && adds > 10000 && swaps > 1000);
    std::printf("  properties: %ld runs, %ld groups, %ld additions, %ld joker swaps checked\n", runs, groups, adds,
                swaps);
}

// A fully consistent hand-made position: every tile not listed goes to the pile.
struct Setup {
    int indicator = T(K, 2); // okey = Black 3
    std::array<V, 4> hands;
    std::array<V, 4> discards;
    std::vector<Meld> table;
    V pileTop; // pileTop.back() is drawn first
    int seat = 0;
    TurnStage stage = TurnStage::Play;
    RulesConfig cfg;
};
const OkeyInfo GOK = OkeyInfo::fromIndicator(T(K, 2));
const int GJ1 = T(K, 3, 0), GJ2 = T(K, 3, 1);

Meld mk(const V& ids, int owner, bool pair = false) {
    Meld m;
    CHECK(makeMeld(ids, GOK, m, pair));
    m.owner = owner;
    return m;
}

void apply(Game& g, const Setup& s) {
    g.setRules(s.cfg);
    g.startMatch(777);
    g.drainEvents();
    std::vector<int> used(NUM_TILES, 0);
    used[s.indicator]++;
    for (int p = 0; p < 4; ++p) {
        for (int id : s.hands[p]) used[id]++;
        for (int id : s.discards[p]) used[id]++;
    }
    for (const Meld& m : s.table)
        for (int id : m.ids()) used[id]++;
    for (int id : s.pileTop) used[id]++;
    bool unique = true;
    for (int c : used)
        if (c > 1) unique = false;
    CHECK(unique); // test setup error otherwise
    V pile;
    for (int id = 0; id < NUM_TILES; ++id)
        if (!used[id]) pile.push_back(id);
    for (int id : s.pileTop) pile.push_back(id);
    g.debugPile() = pile;
    for (int p = 0; p < 4; ++p) {
        PlayerInfo& pl = g.debugPlayer(p);
        pl.hand = s.hands[p];
        pl.discards = s.discards[p];
        pl.opened = false;
        pl.openedWithPairs = false;
        pl.openedTurn = -1;
        pl.openValue = 0;
        pl.handPenalty = 0;
    }
    g.debugTable() = s.table;
    g.debugSetOkey(s.indicator);
    g.debugSetTurn(s.seat, s.stage);
    CHECK(conserved(g));
}

void markOpened(Game& g, int seat, bool pairs) {
    PlayerInfo& p = g.debugPlayer(seat);
    p.opened = true;
    p.openedWithPairs = pairs;
    p.openedTurn = 0; // some earlier turn
    p.openValue = pairs ? 5 : 101;
}

int countType(const std::vector<GameEvent>& ev, EvType t) {
    int n = 0;
    for (const GameEvent& e : ev)
        if (e.type == t) ++n;
    return n;
}
const GameEvent* findEv(std::vector<GameEvent>&&, EvType) = delete; // would dangle
const GameEvent* findEv(const std::vector<GameEvent>& ev, EvType t) {
    for (const GameEvent& e : ev)
        if (e.type == t) return &e;
    return nullptr;
}
int firstNonJoker(const Game& g, int seat) {
    for (int id : g.player(seat).hand)
        if (!g.okey().isJoker(id)) return id;
    return g.player(seat).hand.front();
}

// Filler tiles that never combine with the tiles the scenarios use.
const V FILL = {T(Y, 1), T(B, 3), T(R, 9), T(Y, 11), T(B, 6)};

// ---------------------------------------------------------------------------------------------------------
// Game tests

void testDeal() {
    Game g;
    g.startMatch(42);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 3);
    if (ev.size() == 3) {
        CHECK(ev[0].type == EvType::MatchStart);
        CHECK(ev[1].type == EvType::HandStart);
        CHECK_EQ(ev[1].amount, 0);
        CHECK_EQ(ev[1].tile, g.okey().indicatorId);
        CHECK(ev[2].type == EvType::TurnStart);
        CHECK_EQ(ev[2].player, g.starter());
    }
    CHECK(g.drainEvents().empty());

    std::array<int, 4> starterSeen{};
    int indicator13 = 0;
    for (uint64_t seed = 1; seed <= 1500; ++seed) {
        g.startMatch(seed);
        g.drainEvents();
        const int st = g.starter();
        ++starterSeen[st];
        CHECK(g.handState() == HandState::Playing);
        CHECK_EQ(g.handIndex(), 0);
        CHECK_EQ(g.current(), st);
        CHECK(g.stage() == TurnStage::Play);
        CHECK_EQ(g.pendingLeftTile(), -1);
        for (int s = 0; s < 4; ++s) {
            CHECK_EQ((int)g.player(s).hand.size(), s == st ? 22 : 21);
            CHECK(g.player(s).discards.empty());
            CHECK(!g.player(s).opened);
            CHECK_EQ(g.player(s).totalScore, 0);
        }
        CHECK_EQ(g.pileCount(), 20);
        CHECK(g.table().empty());
        const int ind = g.okey().indicatorId;
        CHECK(isValidTile(ind) && !isFakeJoker(ind));
        CHECK(!contains(g.debugPile(), ind));
        for (int s = 0; s < 4; ++s) CHECK(!contains(g.player(s).hand, ind));
        CHECK_EQ(g.okey().color, printedColor(ind));
        const int n = printedNumber(ind);
        CHECK_EQ(g.okey().number, n == 13 ? 1 : n + 1);
        if (n == 13) {
            ++indicator13;
            CHECK(g.okey().isJoker(makeTileId(printedColor(ind), 1, 0)));
        }
        CHECK(conserved(g));
    }
    for (int s = 0; s < 4; ++s) CHECK(starterSeen[s] > 250); // random starter in hand 0
    CHECK(indicator13 > 0);
}

void testDeterminismOfDeal() {
    Game a, b;
    a.startMatch(123456789);
    b.startMatch(123456789);
    CHECK_EQ(snapshot(a), snapshot(b));
    Game c;
    c.startMatch(123456790);
    CHECK(snapshot(a) != snapshot(c));
    a.startMatch(123456789); // restarting reproduces the same deal
    CHECK_EQ(snapshot(a), snapshot(b));
}

void testTurnOrder() {
    Game g;
    g.startMatch(7);
    g.drainEvents();
    const int s0 = g.starter();
    const int s1 = Game::rightOf(s0);
    CHECK_EQ(Game::rightOf(3), 0);
    CHECK_EQ(Game::leftOf(0), 3);
    CHECK_EQ(Game::leftOf(s1), s0);

    ActionResult r = g.drawFromPile(s1);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Sıra sende değil"));
    r = g.drawFromPile(s0);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Başlayan oyuncu ilk turda taş çekmez, bir taş at"));
    CHECK(!g.canTakeFromLeft(s0));
    CHECK_EQ(g.discard(s0, -5).error, std::string("Bu taş sende yok"));

    const int t = firstNonJoker(g, s0);
    const int turn0 = g.turnNumber();
    r = g.discard(s0, t);
    CHECK(r.ok);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
        CHECK(ev[0].type == EvType::Discard);
        CHECK_EQ(ev[0].tile, t);
        CHECK_EQ(ev[0].player, s0);
        CHECK(ev[1].type == EvType::TurnStart);
        CHECK_EQ(ev[1].player, s1);
    }
    CHECK_EQ(g.current(), s1);
    CHECK_EQ(g.turnNumber(), turn0 + 1);
    CHECK(g.stage() == TurnStage::NeedDraw);
    CHECK_EQ(g.topDiscard(s0), t);
    CHECK_EQ((int)g.discardHistory().size(), 1);
    CHECK_EQ(g.discardHistory()[0].player, s0);
    CHECK_EQ((int)g.player(s0).hand.size(), 21);

    r = g.discard(s1, g.player(s1).hand[0]);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Önce taş çekmelisin"));
    r = g.openHand(s1, {{g.player(s1).hand[0], g.player(s1).hand[1], g.player(s1).hand[2]}});
    CHECK_EQ(r.error, std::string("Önce taş çekmelisin"));
    CHECK(g.drainEvents().empty());

    const int top = g.debugPile().back();
    r = g.drawFromPile(s1);
    CHECK(r.ok);
    CHECK_EQ(g.pileCount(), 19);
    CHECK_EQ((int)g.player(s1).hand.size(), 22);
    CHECK_EQ(g.player(s1).hand.back(), top);
    CHECK(g.stage() == TurnStage::Play);
    ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].type == EvType::DrawPile && ev[0].tile == top && ev[0].player == s1);
    r = g.drawFromPile(s1);
    CHECK_EQ(r.error, std::string("Zaten taş çektin"));
    r = g.takeFromLeft(s1);
    CHECK_EQ(r.error, std::string("Zaten taş çektin"));
    r = g.discard(s1, -3);
    CHECK_EQ(r.error, std::string("Bu taş sende yok"));

    // play a full round: 0 -> 1 -> 2 -> 3
    int seat = s1;
    for (int k = 0; k < 8; ++k) {
        CHECK_EQ(g.current(), seat);
        if (g.stage() == TurnStage::NeedDraw) CHECK(g.drawFromPile(seat).ok);
        CHECK(g.discard(seat, firstNonJoker(g, seat)).ok);
        seat = Game::rightOf(seat);
    }
    CHECK_EQ(g.current(), seat);
    CHECK_EQ(g.pileCount(), 20 - 8); // s1's first draw + 7 more
    CHECK(conserved(g));
}

void testTurnTexts() {
    Game g;
    g.setPlayer(1, "Hacı Rıza", false);
    g.setPlayer(2, "Kel Mahmut", false);
    g.setPlayer(3, "Emekli Nuri", false);
    g.startMatch(3);
    g.drainEvents();
    std::map<int, std::string> texts;
    for (int k = 0; k < 5; ++k) {
        const int seat = g.current();
        if (g.stage() == TurnStage::NeedDraw) {
            CHECK(g.drawFromPile(seat).ok);
            std::vector<GameEvent> ev = g.drainEvents();
            CHECK(ev.size() == 1);
            if (!ev.empty()) {
                const std::string face = tileNameTR(ev[0].tile, g.okey());
                if (seat == 0) {
                    CHECK_EQ(ev[0].text, "Ortadan " + (g.okey().isJoker(ev[0].tile) ? std::string("okey") : face) +
                                             " çektin");
                } else {
                    CHECK_EQ(ev[0].text, g.player(seat).name + " ortadan taş çekti");
                }
            }
        }
        CHECK(g.discard(seat, firstNonJoker(g, seat)).ok);
        for (const GameEvent& e : g.drainEvents())
            if (e.type == EvType::TurnStart) texts[e.player] = e.text;
    }
    CHECK_EQ(texts[0], std::string("Sıra sende"));
    CHECK_EQ(texts[1], std::string("Sıra Hacı Rıza'da"));
    CHECK_EQ(texts[2], std::string("Sıra Kel Mahmut'ta"));
    CHECK_EQ(texts[3], std::string("Sıra Emekli Nuri'de"));

    const char* names[] = {"Batuhan", "Burak", "Ayşe", "Selim", "Oğuz", "Gül"};
    const char* expect[] = {"Sıra Batuhan'da", "Sıra Burak'ta", "Sıra Ayşe'de", "Sıra Selim'de", "Sıra Oğuz'da",
                            "Sıra Gül'de"};
    for (int i = 0; i < 6; ++i) {
        Game h;
        for (int s = 0; s < 4; ++s) h.setPlayer(s, names[i], false);
        h.startMatch(5);
        const std::vector<GameEvent> ev = h.drainEvents();
        const GameEvent* ts = findEv(ev, EvType::TurnStart);
        CHECK(ts && ts->text == expect[i]);
    }
    // the human is addressed as "sen" whatever name they chose
    const char* seatTurn[] = {"Sıra sende", "Sıra Hacı Rıza'da", "Sıra Kel Mahmut'ta", "Sıra Emekli Nuri'de"};
    bool sawHuman = false;
    for (uint64_t seed = 1; seed < 40; ++seed) {
        Game h;
        h.setPlayer(0, "Batuhan", true);
        h.startMatch(seed);
        const std::vector<GameEvent> ev = h.drainEvents();
        const GameEvent* ts = findEv(ev, EvType::TurnStart);
        CHECK(ts && ts->player >= 0 && ts->player < 4);
        if (ts && ts->player >= 0 && ts->player < 4) {
            CHECK_EQ(ts->text, std::string(seatTurn[ts->player]));
            sawHuman = sawHuman || ts->player == 0;
        }
    }
    CHECK(sawHuman);
}

// Expected definite-object form of a tile name, written out per number (bir'i, iki'yi, üç'ü ... on üç'ü).
std::string tileAcc(int id, const OkeyInfo& ok) {
    if (ok.isJoker(id)) return "okeyi";
    const std::string name = tileNameTR(id, ok);
    if (isFakeJoker(id)) return "Sahte Okeyi" + name.substr(std::string("Sahte Okey").size());
    static const std::map<int, std::string> kSuffix = {{1, "'i"},  {2, "'yi"}, {3, "'ü"},   {4, "'ü"},  {5, "'i"},
                                                       {6, "'yı"}, {7, "'yi"}, {8, "'i"},   {9, "'u"},  {10, "'u"},
                                                       {11, "'i"}, {12, "'yi"}, {13, "'ü"}};
    return name + kSuffix.at(printedNumber(id));
}

void testTakeReturnLeft() {
    Game g;
    g.startMatch(11);
    g.drainEvents();
    const int s0 = g.starter();
    const int s1 = Game::rightOf(s0);
    const int t = firstNonJoker(g, s0);
    CHECK(g.discard(s0, t).ok);
    g.drainEvents();
    CHECK(g.canTakeFromLeft(s1));
    CHECK(!g.canTakeFromLeft(s0));
    CHECK(!g.takeFromLeft(s0).ok);

    ActionResult r = g.returnLeftTile(s1);
    CHECK_EQ(r.error, std::string("Geri verilecek taş yok"));
    r = g.takeFromLeft(s1);
    CHECK(r.ok);
    CHECK_EQ(g.pendingLeftTile(), t);
    CHECK(contains(g.player(s1).hand, t));
    CHECK_EQ(g.topDiscard(s0), -1);
    CHECK(g.stage() == TurnStage::Play);
    CHECK_EQ(g.pileCount(), 20);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].type == EvType::TakeLeft && ev[0].tile == t);
    if (!ev.empty()) CHECK(ev[0].text.find(tileNameTR(t, g.okey())) != std::string::npos);

    r = g.discard(s1, t);
    CHECK_EQ(r.error, std::string("Yandan aldığın taşı önce masada kullan ya da Geri Ver (101 ceza)"));
    r = g.discard(s1, firstNonJoker(g, s1));
    CHECK_EQ(r.error, std::string("Yandan aldığın taşı önce masada kullan ya da Geri Ver (101 ceza)"));
    r = g.addToMeld(s1, t, 0);
    CHECK_EQ(r.error, std::string("Önce elini açmalısın"));
    r = g.layMelds(s1, {{t, g.player(s1).hand[0], g.player(s1).hand[1]}});
    CHECK_EQ(r.error, std::string("Önce elini açmalısın"));
    CHECK(g.drainEvents().empty());

    V before = g.player(s1).hand;
    r = g.returnLeftTile(Game::rightOf(s1));
    CHECK_EQ(r.error, std::string("Sıra sende değil"));
    r = g.returnLeftTile(s1);
    CHECK(r.ok);
    CHECK_EQ(g.topDiscard(s0), t);
    CHECK(!contains(g.player(s1).hand, t));
    CHECK_EQ((int)g.player(s1).hand.size(), 21);
    CHECK_EQ(g.player(s1).handPenalty, 101);
    CHECK(g.stage() == TurnStage::NeedDraw);
    CHECK_EQ(g.pendingLeftTile(), -1);
    CHECK(!g.canTakeFromLeft(s1));
    ev = g.drainEvents();
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
        CHECK(ev[0].type == EvType::ReturnLeft && ev[0].tile == t);
        CHECK(ev[1].type == EvType::Penalty && ev[1].amount == 101 && ev[1].player == s1);
        const std::string acc = tileAcc(t, g.okey());
        const std::string what = g.player(s1).human ? "Yandan aldığın " + acc + " geri verdin"
                                                    : g.player(s1).name + " yandan aldığı " + acc + " geri verdi";
        CHECK_EQ(ev[0].text, what);
        CHECK_EQ(ev[1].text, what + ": 101 ceza"); // the penalty alone tells the whole story
    }
    r = g.takeFromLeft(s1);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Geri verdiğin taşı bu turda tekrar alamazsın"));
    CHECK(g.drawFromPile(s1).ok);
    CHECK(g.discard(s1, firstNonJoker(g, s1)).ok);
    CHECK(conserved(g));

    // the seat after takes from s1; s1's own pile of its earlier returns is unaffected
    const int s2 = Game::rightOf(s1);
    CHECK(g.canTakeFromLeft(s2));
    CHECK(g.drawFromPile(s2).ok);
    CHECK(g.discard(s2, firstNonJoker(g, s2)).ok);
    // s3's left is s2 (non-empty); empty-left case:
    Setup s;
    s.hands[0] = {T(Y, 5), T(B, 5), T(R, 5)};
    s.hands[1] = FILL;
    s.hands[2] = {T(K, 9)};
    s.hands[3] = {T(K, 10)};
    s.stage = TurnStage::NeedDraw;
    apply(g, s);
    CHECK(!g.canTakeFromLeft(0));
    r = g.takeFromLeft(0);
    CHECK_EQ(r.error, std::string("Yandan alınacak taş yok"));
}

// Returning the left tile: the Penalty repeats the ReturnLeft text and names the tile in the object case; the human
// reads it in the second person without a pronoun, and the first letter is capitalised the Turkish way.
void testReturnLeftTexts() {
    std::vector<int> tiles;
    for (int n = 1; n <= 13; ++n) tiles.push_back(T(Y, n, 1));
    tiles.push_back(GJ2);          // the okey itself
    tiles.push_back(FAKE_JOKER_B); // sahte okey
    for (int seat : {0, 2}) {
        for (int tile : tiles) {
            Game g;
            g.setPlayer(0, "Batuhan", true);
            g.setPlayer(2, "Kel Mahmut", false);
            Setup s;
            s.hands[seat] = FILL;
            if (seat != 0) s.hands[0] = {T(K, 13)};
            s.discards[Game::leftOf(seat)] = {tile};
            s.seat = seat;
            s.stage = TurnStage::NeedDraw;
            apply(g, s);
            CHECK(g.takeFromLeft(seat).ok);
            std::vector<GameEvent> ev = g.drainEvents();
            CHECK(ev.size() == 1 && ev[0].type == EvType::TakeLeft);
            const std::string face = GOK.isJoker(tile) ? std::string("okey") : tileNameTR(tile, GOK);
            if (!ev.empty())
                CHECK_EQ(ev[0].text, seat == 0 ? "Yandan " + face + " aldın" : "Kel Mahmut yandan " + face + " aldı");
            CHECK(g.returnLeftTile(seat).ok);
            ev = g.drainEvents();
            CHECK(ev.size() == 2);
            if (ev.size() != 2) continue;
            const std::string what = seat == 0 ? "Yandan aldığın " + tileAcc(tile, GOK) + " geri verdin"
                                               : "Kel Mahmut yandan aldığı " + tileAcc(tile, GOK) + " geri verdi";
            CHECK(ev[0].type == EvType::ReturnLeft);
            CHECK_EQ(ev[0].text, what);
            CHECK(ev[1].type == EvType::Penalty);
            CHECK_EQ(ev[1].text, what + ": 101 ceza");
        }
    }
    // spot checks of the object case against hand-written Turkish
    CHECK_EQ(tileAcc(T(R, 10), GOK), std::string("Kırmızı 10'u"));
    CHECK_EQ(tileAcc(T(B, 6), GOK), std::string("Mavi 6'yı"));
    CHECK_EQ(tileAcc(T(Y, 13), GOK), std::string("Sarı 13'ü"));
    CHECK_EQ(tileAcc(FAKE_JOKER_A, GOK), std::string("Sahte Okeyi (Siyah 3)"));

    // the discard error names the way out, with the configured penalty
    Game g;
    Setup s;
    s.cfg.penalty = 50;
    s.hands[0] = FILL;
    s.discards[3] = {T(R, 1)};
    s.stage = TurnStage::NeedDraw;
    apply(g, s);
    CHECK(g.takeFromLeft(0).ok);
    CHECK_EQ(g.discard(0, T(Y, 1)).error, std::string("Yandan aldığın taşı önce masada kullan ya da Geri Ver (50 ceza)"));
}

// 100 fails, 101 passes; value counters; errors.
void testOpeningThreshold() {
    Game g;
    Setup s;
    V melds100 = {T(R, 11), T(R, 12), T(R, 13), T(B, 11), T(B, 12), T(B, 13), T(Y, 7), T(B, 7), T(K, 7), T(R, 7)};
    s.hands[0] = melds100;
    s.hands[0].insert(s.hands[0].end(), FILL.begin(), FILL.end());
    apply(g, s);
    G groups100 = {{T(R, 11), T(R, 12), T(R, 13)}, {T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 7), T(B, 7), T(K, 7), T(R, 7)}};
    V snap = snapshot(g);
    OpenCheck c = g.checkOpen(0, groups100);
    CHECK_EQ(snapshot(g), snap); // pure
    CHECK(!c.valid);
    CHECK(!c.pairs);
    CHECK_EQ(c.value, 100);
    CHECK_EQ(c.error, std::string("Açmak için en az 101 gerekli (şu an 100)"));
    ActionResult r = g.openHand(0, groups100);
    CHECK(!r.ok);
    CHECK_EQ(r.error, c.error);
    CHECK_EQ(snapshot(g), snap);
    CHECK(g.drainEvents().empty());

    // counters are filled even when it's not your turn
    c = g.checkOpen(1, groups100);
    CHECK_EQ(c.error, std::string("Sıra sende değil"));
    CHECK_EQ(c.value, 100);

    // 12-13-1 is not a run in 101: it can't count toward an opening
    s.hands[0] = {T(R, 12), T(R, 13), T(R, 1), T(B, 11), T(B, 12), T(B, 13), T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)};
    s.hands[0].insert(s.hands[0].end(), {T(K, 13), T(Y, 13), T(B, 3), T(R, 9), T(Y, 11), T(B, 6), T(K, 12)});
    apply(g, s);
    c = g.checkOpen(0, {{T(R, 12), T(R, 13), T(R, 1)}, {T(B, 13), T(B, 12), T(B, 11)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}});
    CHECK(!c.valid);
    CHECK_EQ(c.value, 62);

    // exactly 101 (13-13-13 is worth 39)
    G groups101 = {{T(R, 13), T(K, 13), T(Y, 13)}, {T(B, 13), T(B, 12), T(B, 11)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}};
    c = g.checkOpen(0, groups101);
    CHECK(c.valid);
    CHECK_EQ(c.value, 101);
    CHECK(c.error.empty());
    const int turn = g.turnNumber();
    r = g.openHand(0, groups101);
    CHECK(r.ok);
    const PlayerInfo& p = g.player(0);
    CHECK(p.opened && !p.openedWithPairs);
    CHECK_EQ(p.openValue, 101);
    CHECK_EQ(p.openedTurn, turn);
    CHECK_EQ((int)p.hand.size(), 7);
    CHECK_EQ((int)g.table().size(), 3);
    for (const Meld& m : g.table()) CHECK_EQ(m.owner, 0);
    CHECK_EQ(g.table()[1].ids(), (V{T(B, 11), T(B, 12), T(B, 13)})); // descending normalised
    CHECK(g.openedThisTurn(0));
    CHECK(!g.canWorkTable(0));
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 1);
    if (ev.size() == 1) {
        CHECK(ev[0].type == EvType::Open);
        CHECK_EQ(ev[0].meld, 0);
        CHECK_EQ(ev[0].count, 3);
        CHECK_EQ(ev[0].amount, 101);
        CHECK_EQ(ev[0].text, std::string("Eli açtın (101)"));
    }
    // already opened
    r = g.openHand(0, {{T(B, 3), T(R, 9), T(Y, 11)}});
    CHECK_EQ(r.error, std::string("Elini zaten açtın"));
    // waitTurnAfterOpening
    r = g.addToMeld(0, T(Y, 11), 0);
    CHECK_EQ(r.error, std::string("Açtığın turda işleyemezsin, sonraki turunu bekle"));
    r = g.layMelds(0, {{T(B, 3), T(R, 9), T(Y, 11)}});
    CHECK_EQ(r.error, std::string("Açtığın turda yeni per açamazsın, sonraki turunu bekle"));
    r = g.swapJoker(0, T(B, 3), 0);
    CHECK_EQ(r.error, std::string("Açtığın turda okey alamazsın, sonraki turunu bekle"));
    CHECK(!g.checkLay(0, {{T(K, 12), T(K, 11), T(K, 10)}}).valid);
    CHECK(g.discard(0, T(B, 3)).ok);
    CHECK(!g.openedThisTurn(0));

    // a human who chose a name is still addressed in the second person, without the pronoun
    Game h;
    h.setPlayer(0, "Batuhan", true);
    apply(h, s);
    CHECK(h.openHand(0, groups101).ok);
    ev = h.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Eli açtın (101)");
    // every other seat gets third-person texts with its name
    Game b;
    b.setPlayer(0, "Batuhan", false);
    apply(b, s);
    CHECK(b.openHand(0, groups101).ok);
    ev = b.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Batuhan eli açtı (101)");
}

void testOpeningErrors() {
    Game g;
    Setup s;
    V open = {T(R, 13), T(K, 13), T(Y, 13), T(B, 11), T(B, 12), T(B, 13), T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)};
    G gOpen = {{T(R, 13), T(K, 13), T(Y, 13)}, {T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}};
    s.hands[0] = open;
    s.hands[0].push_back(T(R, 12));
    s.hands[0].push_back(T(R, 1));
    s.hands[0].push_back(T(Y, 9));
    s.hands[0].push_back(T(B, 9));
    s.hands[0].push_back(T(K, 9));
    s.hands[0].push_back(T(B, 9, 1));
    s.hands[0].push_back(T(R, 5));
    s.hands[0].push_back(T(R, 5, 1));
    apply(g, s);
    V snap = snapshot(g);
    auto expectFail = [&](const G& groups, const std::string& err) {
        const OpenCheck c = g.checkOpen(0, groups);
        CHECK(!c.valid);
        CHECK_EQ(c.error, err);
        const ActionResult r = g.openHand(0, groups);
        CHECK(!r.ok);
        CHECK_EQ(r.error, err);
        CHECK_EQ(snapshot(g), snap);
        CHECK(g.drainEvents().empty());
    };
    expectFail({}, "Per seçmedin");
    expectFail({{T(R, 5), T(R, 5, 1)}, {T(Y, 9), T(B, 9), T(K, 9)}}, "Seri ve çift karıştırılamaz");
    expectFail({{T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 9), T(B, 9), T(B, 9, 1)}}, "Grupta aynı renk iki kez olamaz");
    expectFail({{T(R, 12), T(R, 13), T(K, 1)}}, "Bu taş sende yok");
    expectFail({{T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 12), T(B, 12), T(K, 12)}}, "Bu taş sende yok");
    expectFail({{T(R, 12), T(R, 13), T(R, 1)}}, "Sayılar ardışık değil"); // 101: no 12-13-1
    expectFail({{T(Y, 9), T(B, 9), T(K, 9)}, {T(K, 9), T(B, 9, 1), T(R, 5)}}, "Aynı taş iki kez kullanılamaz");
    expectFail({{T(R, 5), T(R, 5, 1)}, {T(R, 1)}}, "Çift iki taştan oluşmalı");
    expectFail({{T(R, 12), T(R, 13), T(R, 1), T(Y, 5)}}, "Seri aynı renkten olmalı");
    expectFail({{T(R, 5), T(R, 5, 1)}, {T(B, 9), T(B, 9, 1)}, {T(R, 12), T(R, 13)}},
               "Çiftin taşları aynı olmalı");
    expectFail({{T(R, 5), T(R, 5, 1)}, {T(B, 9), T(B, 9, 1)}}, "Çift açmak için en az 5 çift gerekli (şu an 2)");

    // keep at least one tile
    s.hands[0] = open;
    s.hands[0].insert(s.hands[0].end(), {T(Y, 9), T(B, 9), T(K, 9)});
    apply(g, s);
    snap = snapshot(g);
    G all = gOpen;
    all.push_back({T(Y, 9), T(B, 9), T(K, 9)});
    expectFail(all, "Elde en az bir taş kalmalı");
    CHECK(g.checkOpen(0, gOpen).valid);
    CHECK(g.openHand(0, gOpen).ok);
    CHECK_EQ((int)g.player(0).hand.size(), 3);

    // wrong stage / seat
    s.hands[0] = open;
    s.hands[0].push_back(T(K, 9));
    s.stage = TurnStage::NeedDraw;
    apply(g, s);
    CHECK_EQ(g.checkOpen(0, gOpen).error, std::string("Önce taş çekmelisin"));
    CHECK_EQ(g.checkOpen(2, gOpen).error, std::string("Sıra sende değil"));
    CHECK_EQ(g.checkOpen(7, gOpen).error, std::string("Geçersiz oyuncu"));
    CHECK_EQ(g.checkOpen(-1, gOpen).error, std::string("Geçersiz oyuncu"));
}

void testPairsOpening() {
    Game g;
    Setup s;
    G five = {{T(R, 5), T(R, 5, 1)}, {T(B, 9), T(B, 9, 1)}, {T(Y, 1), T(Y, 1, 1)}, {T(K, 12), T(K, 12, 1)},
              {T(R, 13), GJ1}};
    for (const V& p : five) s.hands[0].insert(s.hands[0].end(), p.begin(), p.end());
    s.hands[0].insert(s.hands[0].end(), {T(B, 2), T(Y, 7)});
    apply(g, s);
    G four(five.begin(), five.begin() + 4);
    OpenCheck c = g.checkOpen(0, four);
    CHECK(!c.valid);
    CHECK(c.pairs);
    CHECK_EQ(c.pairCount, 4);
    CHECK_EQ(c.value, 0);
    CHECK_EQ(c.error, std::string("Çift açmak için en az 5 çift gerekli (şu an 4)"));
    CHECK(!g.openHand(0, four).ok);
    c = g.checkOpen(0, five);
    CHECK(c.valid && c.pairs);
    CHECK_EQ(c.pairCount, 5);
    CHECK(g.openHand(0, five).ok);
    CHECK(g.player(0).opened && g.player(0).openedWithPairs);
    CHECK_EQ(g.player(0).openValue, 5);
    CHECK_EQ((int)g.table().size(), 5);
    for (const Meld& m : g.table()) CHECK(m.kind == MeldKind::Pair);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].type == EvType::Open && ev[0].amount == 5 && ev[0].count == 5);
    if (!ev.empty()) CHECK_EQ(ev[0].text, std::string("Çiftten açtın (5 çift)"));

    // a bot opening with pairs
    Setup b = s;
    b.hands[2] = b.hands[0];
    b.hands[0] = {T(K, 1)};
    b.seat = 2;
    g.setPlayer(2, "Kel Mahmut", false);
    apply(g, b);
    CHECK(g.openHand(2, five).ok);
    ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Kel Mahmut çiftten açtı (5 çift)");

    // minPairsToOpen is configurable
    Setup s4 = s;
    s4.cfg.minPairsToOpen = 4;
    apply(g, s4);
    CHECK(g.checkOpen(0, four).valid);
}

// A tile taken from the left must be used in the same turn.
void testPendingLeft() {
    Game g;
    Setup s;
    s.hands[0] = {T(R, 12), T(R, 13), T(B, 11), T(B, 12), T(B, 13), T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8),
                  T(Y, 10), T(B, 10), T(K, 10), T(K, 11), T(K, 12), T(K, 13), T(R, 4)};
    s.discards[3] = {T(Y, 2), T(R, 11)};
    s.stage = TurnStage::NeedDraw;
    apply(g, s);
    CHECK(g.canTakeFromLeft(0));
    CHECK(g.takeFromLeft(0).ok);
    CHECK_EQ(g.pendingLeftTile(), T(R, 11));
    CHECK_EQ(g.topDiscard(3), T(Y, 2));
    g.drainEvents();
    G without = {{T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)},
                 {T(Y, 10), T(B, 10), T(K, 10)}, {T(K, 11), T(K, 12), T(K, 13)}}; // 128 without R11
    OpenCheck c = g.checkOpen(0, without);
    CHECK(!c.valid);
    CHECK_EQ(c.value, 36 + 26 + 30 + 36);
    CHECK_EQ(c.error, std::string("Yandan aldığın taş açılışta yer almalı"));
    CHECK_EQ(g.openHand(0, without).error, std::string("Yandan aldığın taş açılışta yer almalı"));
    G with = without;
    with.push_back({T(R, 11), T(R, 12), T(R, 13)});
    CHECK(g.openHand(0, with).ok);
    CHECK_EQ(g.pendingLeftTile(), -1);
    CHECK_EQ(g.player(0).openValue, 128 + 36);
    CHECK(g.discard(0, T(R, 4)).ok);

    // already-opened player uses the left tile by işleme
    Setup t;
    t.table = {mk({T(R, 3), T(R, 4), T(R, 5)}, 1)};
    t.hands[0] = {T(B, 2), T(Y, 9), T(K, 13)};
    t.hands[1] = {T(K, 8)};
    t.discards[3] = {T(R, 6)};
    t.stage = TurnStage::NeedDraw;
    apply(g, t);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    CHECK(g.takeFromLeft(0).ok);
    CHECK(g.canWorkTable(0));
    CHECK_EQ(g.discard(0, T(B, 2)).error, std::string("Yandan aldığın taşı önce masada kullan ya da Geri Ver (101 ceza)"));
    CHECK(g.addToMeld(0, T(R, 6), 0).ok);
    CHECK_EQ(g.pendingLeftTile(), -1);
    CHECK(g.discard(0, T(B, 2)).ok);

    // ... or by laying a new meld with it
    Setup u = t;
    u.hands[0] = {T(B, 7), T(B, 8), T(Y, 9), T(K, 13)};
    u.discards[3] = {T(B, 9)};
    apply(g, u);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    CHECK(g.takeFromLeft(0).ok);
    CHECK(g.layMelds(0, {{T(B, 7), T(B, 8), T(B, 9)}}).ok);
    CHECK_EQ(g.pendingLeftTile(), -1);
    CHECK(g.discard(0, T(K, 13)).ok);

    // ... or by a joker swap
    Setup w = t;
    w.table = {mk({T(R, 3), GJ1, T(R, 5)}, 1)};
    w.discards[3] = {T(R, 4)};
    apply(g, w);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    CHECK(g.takeFromLeft(0).ok);
    CHECK(g.swapJoker(0, T(R, 4), 0).ok);
    CHECK_EQ(g.pendingLeftTile(), -1);
    CHECK(contains(g.player(0).hand, GJ1));
    CHECK(g.discard(0, T(B, 2)).ok);

    // unopened player who can't open: give it back
    Setup v = t;
    apply(g, v);
    CHECK(g.takeFromLeft(0).ok);
    CHECK_EQ(g.addToMeld(0, T(R, 6), 0).error, std::string("Önce elini açmalısın"));
    CHECK(g.returnLeftTile(0).ok);
    CHECK_EQ(g.topDiscard(3), T(R, 6));
    CHECK_EQ(g.player(0).handPenalty, 101);
    CHECK(g.drawFromPile(0).ok);
    CHECK(g.discard(0, T(B, 2)).ok);
    CHECK(conserved(g));

    // With a pending left tile the tile kept for the discard can't be the pending one, otherwise giving
    // it back would leave an empty hand.
    Setup x = t;
    x.hands[0] = {T(R, 2)};
    apply(g, x);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    CHECK(g.takeFromLeft(0).ok);
    V snap = snapshot(g);
    CHECK_EQ(g.addToMeld(0, T(R, 2), 0).error, std::string("Yandan aldığın taş dışında elde bir taş kalmalı"));
    CHECK_EQ(snapshot(g), snap);
    CHECK(g.addToMeld(0, T(R, 6), 0).ok); // using the left tile itself is fine
    CHECK_EQ((int)g.player(0).hand.size(), 1);
    CHECK(g.discard(0, T(R, 2)).ok);       // ... and finishes the hand
    CHECK_EQ(g.lastHandResult().winner, 0);
    Setup y = t;
    y.hands[0] = {T(Y, 1), T(Y, 2), T(Y, 3)};
    apply(g, y);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    CHECK(g.takeFromLeft(0).ok);
    const G lay = {{T(Y, 1), T(Y, 2), T(Y, 3)}};
    CHECK_EQ(g.checkLay(0, lay).error, std::string("Yandan aldığın taş dışında elde bir taş kalmalı"));
    CHECK_EQ(g.layMelds(0, lay).error, std::string("Yandan aldığın taş dışında elde bir taş kalmalı"));
    CHECK(g.checkLay(0, {{T(Y, 1), T(Y, 2), T(Y, 3), T(R, 6)}}).error == "Seri aynı renkten olmalı");
}

void testWaitTurnOff() {
    Game g;
    Setup s;
    s.cfg.waitTurnAfterOpening = false;
    s.table = {mk({T(K, 5), T(K, 6), T(K, 7)}, 2)};
    s.hands[0] = {T(R, 13), T(Y, 13), T(K, 13), T(B, 11), T(B, 12), T(B, 13), T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8),
                  T(K, 8), T(Y, 1), T(B, 2), T(B, 3), T(B, 4)};
    apply(g, s);
    markOpened(g, 2, false);
    CHECK(g.openHand(0, {{T(R, 13), T(Y, 13), T(K, 13)}, {T(B, 11), T(B, 12), T(B, 13)},
                         {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}})
              .ok);
    CHECK(g.canWorkTable(0));
    CHECK(g.addToMeld(0, T(K, 8), 0).ok);
    CHECK(g.checkLay(0, {{T(B, 2), T(B, 3), T(B, 4)}}).valid);
    CHECK(g.layMelds(0, {{T(B, 2), T(B, 3), T(B, 4)}}).ok);
    CHECK_EQ((int)g.player(0).hand.size(), 1);
}

// Katlamalı oyun: every opening must beat the previous one of its kind by at least one (116 -> 117, 5 çift -> 6).
void testKatlamali() {
    Game g;
    Setup s;
    // 13-13-13 (39) + 11-12-13 (36) + 5-6-7-8 (26) = 101; with 9-9-9 (27) = 128
    s.hands[0] = {T(R, 13), T(K, 13), T(Y, 13), T(B, 11), T(B, 12), T(B, 13), T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8),
                  T(Y, 9), T(B, 9), T(K, 9), T(R, 1)};
    const G g101 = {{T(R, 13), T(K, 13), T(Y, 13)}, {T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}};
    G g128 = g101;
    g128.push_back({T(Y, 9), T(B, 9), T(K, 9)});

    // off: a later opener still needs just 101
    apply(g, s);
    markOpened(g, 1, false);
    g.debugPlayer(1).openValue = 116;
    CHECK_EQ(g.seriesOpenNeed(), 101);
    CHECK_EQ(g.pairsOpenNeed(), 5);
    CHECK(g.checkOpen(0, g101).valid);

    // on: one more than the highest series opening on the table
    s.cfg.katlamali = true;
    apply(g, s);
    CHECK_EQ(g.seriesOpenNeed(), 101); // nobody opened yet
    markOpened(g, 1, false);
    g.debugPlayer(1).openValue = 116;
    markOpened(g, 3, false);
    g.debugPlayer(3).openValue = 108;
    CHECK_EQ(g.seriesOpenNeed(), 117);
    CHECK_EQ(g.pairsOpenNeed(), 5); // pair openings are counted apart
    OpenCheck c = g.checkOpen(0, g101);
    CHECK(!c.valid);
    CHECK_EQ(c.error, std::string("Katlamalı: açmak için en az 117 gerekli (şu an 101)"));
    CHECK_EQ(g.openHand(0, g101).error, c.error);
    CHECK(g.checkOpen(0, g128).valid);
    CHECK(g.openHand(0, g128).ok);
    CHECK_EQ(g.player(0).openValue, 128);
    CHECK_EQ(g.seriesOpenNeed(), 129); // the bar moves on for whoever opens next

    // pairs: one pair more than the last pair opening
    Setup p = s;
    p.hands[0] = {T(R, 5), T(R, 5, 1), T(B, 9), T(B, 9, 1), T(Y, 1), T(Y, 1, 1), T(K, 12), T(K, 12, 1), T(R, 13),
                  T(R, 13, 1), T(Y, 7), T(Y, 7, 1), T(B, 2)};
    apply(g, p);
    markOpened(g, 2, true); // 5 pairs
    CHECK_EQ(g.pairsOpenNeed(), 6);
    CHECK_EQ(g.seriesOpenNeed(), 101);
    const G five = {{T(R, 5), T(R, 5, 1)}, {T(B, 9), T(B, 9, 1)}, {T(Y, 1), T(Y, 1, 1)}, {T(K, 12), T(K, 12, 1)},
                    {T(R, 13), T(R, 13, 1)}};
    G six = five;
    six.push_back({T(Y, 7), T(Y, 7, 1)});
    c = g.checkOpen(0, five);
    CHECK(!c.valid);
    CHECK_EQ(c.error, std::string("Katlamalı: çift açmak için en az 6 çift gerekli (şu an 5)"));
    CHECK(g.openHand(0, six).ok);
    CHECK_EQ(g.pairsOpenNeed(), 7);
}

void testLayMelds() {
    Game g;
    Setup s;
    s.table = {mk({T(R, 5), T(R, 5, 1)}, 0, true), mk({T(K, 5), T(K, 6), T(K, 7)}, 2)};
    s.hands[0] = {T(B, 9), T(B, 9, 1), T(R, 11), T(R, 12), T(R, 13), T(K, 8), T(Y, 4), T(Y, 4, 1)};
    apply(g, s);
    markOpened(g, 0, true);
    markOpened(g, 2, false);
    V snap = snapshot(g);
    ActionResult r = g.layMelds(0, {{T(R, 11), T(R, 12), T(R, 13)}});
    CHECK_EQ(r.error, std::string("Çiftle açan yeni seri açamaz, sadece çift açabilir"));
    CHECK_EQ(g.checkLay(0, {{T(R, 11), T(R, 12), T(R, 13)}}).error,
             std::string("Çiftle açan yeni seri açamaz, sadece çift açabilir"));
    CHECK_EQ(g.layMelds(0, {{T(B, 9), T(B, 9, 1)}, {T(R, 11), T(R, 12), T(R, 13)}}).error,
             std::string("Seri ve çift karıştırılamaz"));
    CHECK_EQ(g.layMelds(0, {}).error, std::string("Per seçmedin"));
    CHECK_EQ(g.layMelds(0, {{T(B, 9), T(R, 11)}}).error, std::string("Çiftin taşları aynı olmalı"));
    CHECK_EQ(snapshot(g), snap);
    OpenCheck c = g.checkLay(0, {{T(B, 9), T(B, 9, 1)}, {T(Y, 4), T(Y, 4, 1)}});
    CHECK(c.valid && c.pairs && c.pairCount == 2);
    r = g.layMelds(0, {{T(B, 9), T(B, 9, 1)}, {T(Y, 4), T(Y, 4, 1)}});
    CHECK(r.ok);
    CHECK_EQ((int)g.table().size(), 4);
    CHECK(g.table()[2].kind == MeldKind::Pair && g.table()[2].owner == 0);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].type == EvType::LayMelds && ev[0].meld == 2 && ev[0].count == 2);
    if (!ev.empty()) CHECK_EQ(ev[0].text, std::string("2 yeni çift açtın"));
    // pair openers may still işle onto runs/groups of others
    CHECK(g.addToMeld(0, T(K, 8), 1).ok);
    CHECK_EQ(g.table()[1].owner, 2);

    // series openers can't lay pairs while nobody else opened with pairs; any value is fine after opening
    Setup t;
    t.table = {mk({T(K, 5), T(K, 6), T(K, 7)}, 0)};
    t.hands[0] = {T(B, 9), T(B, 9, 1), T(Y, 1), T(Y, 2), T(Y, 3), T(R, 13)};
    apply(g, t);
    markOpened(g, 0, false);
    CHECK(!g.pairsOpenedByOther(0));
    CHECK_EQ(g.layMelds(0, {{T(B, 9), T(B, 9, 1)}}).error,
             std::string("Seriyle açan, masada çift açan biri yokken çift açamaz"));
    CHECK(g.layMelds(0, {{T(Y, 1), T(Y, 2), T(Y, 3)}}).ok);
    ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Yeni per açtın");
    CHECK_EQ(g.layMelds(0, {{T(B, 9), T(B, 9, 1), T(R, 13)}}).error, std::string("Seri aynı renkten olmalı"));
    // keep one tile
    Setup u = t;
    u.hands[0] = {T(Y, 1), T(Y, 2), T(Y, 3)};
    apply(g, u);
    markOpened(g, 0, false);
    CHECK_EQ(g.layMelds(0, {{T(Y, 1), T(Y, 2), T(Y, 3)}}).error, std::string("Elde en az bir taş kalmalı"));
    // not opened
    apply(g, u);
    CHECK_EQ(g.layMelds(0, {{T(Y, 1), T(Y, 2), T(Y, 3)}}).error, std::string("Önce elini açmalısın"));
    CHECK_EQ(g.checkLay(0, {{T(Y, 1), T(Y, 2), T(Y, 3)}}).error, std::string("Önce elini açmalısın"));

    // ... but once someone else opened with pairs, a series opener may lay (any number of) pairs too
    Setup v;
    v.table = {mk({T(K, 5), T(K, 6), T(K, 7)}, 0), mk({T(R, 2), T(R, 2, 1)}, 1, true)};
    v.hands[0] = {T(B, 9), T(B, 9, 1), T(Y, 4), T(Y, 4, 1), T(R, 13)};
    apply(g, v);
    markOpened(g, 0, false);
    markOpened(g, 1, true);
    CHECK(g.pairsOpenedByOther(0));
    CHECK(!g.pairsOpenedByOther(1));
    OpenCheck pc = g.checkLay(0, {{T(B, 9), T(B, 9, 1)}});
    CHECK(pc.valid && pc.pairs && pc.pairCount == 1);
    CHECK(g.layMelds(0, {{T(B, 9), T(B, 9, 1)}, {T(Y, 4), T(Y, 4, 1)}}).ok);
    CHECK_EQ((int)g.player(0).hand.size(), 1);
    CHECK(!g.player(0).openedWithPairs); // still a series opener (scored as one)
    ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "2 yeni çift açtın");
}

void testIsleme() {
    Game g;
    Setup s;
    s.table = {mk({T(R, 3), T(R, 4), T(R, 5)}, 1), mk({T(Y, 7), T(B, 7), T(K, 7)}, 2),
               mk({T(Y, 9), T(Y, 9, 1)}, 3, true)};
    s.hands[0] = {T(R, 6), T(R, 2), T(R, 7), T(B, 3), T(B, 11), T(K, 13)};
    apply(g, s);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    V snap = snapshot(g);
    CHECK_EQ(g.addToMeld(0, T(R, 6), 2).error, std::string("Çiftlere taş işlenemez"));
    CHECK_EQ(g.addToMeld(0, T(R, 6), 3).error, std::string("Böyle bir per yok"));
    CHECK_EQ(g.addToMeld(0, T(R, 6), -1).error, std::string("Böyle bir per yok"));
    CHECK_EQ(g.addToMeld(0, T(R, 8), 0).error, std::string("Bu taş sende yok"));
    CHECK_EQ(g.addToMeld(0, T(B, 3), 0).error, std::string("Bu taş o pere işlenemez"));
    CHECK_EQ(g.addToMeld(0, T(R, 6), 0, AddSide::Front).error, std::string("Bu taş o pere işlenemez"));
    CHECK_EQ(g.addToMeld(1, T(R, 6), 0).error, std::string("Sıra sende değil"));
    CHECK_EQ(snapshot(g), snap);
    CHECK(g.drainEvents().empty());

    CHECK(g.addToMeld(0, T(R, 6), 0, AddSide::Back).ok);
    CHECK_EQ(g.table()[0].owner, 1);
    CHECK_EQ(nums(g.table()[0]), (V{3, 4, 5, 6}));
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].type == EvType::AddToMeld && ev[0].tile == T(R, 6) && ev[0].meld == 0);
    if (!ev.empty()) CHECK_EQ(ev[0].text, std::string("Kırmızı 6'yı işledin"));
    CHECK(g.addToMeld(0, T(R, 2), 0).ok);
    CHECK_EQ(nums(g.table()[0]), (V{2, 3, 4, 5, 6}));
    ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].text == "Kırmızı 2'yi işledin");
    CHECK(g.addToMeld(0, T(R, 7), 1).ok); // red 7 completes the group
    ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].text == "Kırmızı 7'yi işledin");
    CHECK_EQ(g.table()[1].size(), 4);
    CHECK_EQ(g.table()[1].owner, 2);
    CHECK_EQ((int)g.player(0).hand.size(), 3);

    // keep-1
    Setup t = s;
    t.hands[0] = {T(R, 6)};
    apply(g, t);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    CHECK_EQ(g.addToMeld(0, T(R, 6), 0).error, std::string("Elde en az bir taş kalmalı"));

    // bots' işleme text
    Setup u = s;
    u.hands[1] = u.hands[0];
    u.hands[0] = {T(K, 12)};
    u.seat = 1;
    g.setPlayer(1, "Hacı Rıza", false);
    apply(g, u);
    markOpened(g, 0, false);
    markOpened(g, 1, false);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    CHECK(!g.addToMeld(1, T(B, 3), 0).ok);
    CHECK(g.addToMeld(1, T(R, 2), 0).ok);
    ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Hacı Rıza Kırmızı 2'yi işledi");
}

void testSwapJokerGame() {
    Game g;
    Setup s;
    s.table = {mk({T(R, 3), GJ1, T(R, 5)}, 2), mk({T(Y, 7), T(B, 7), T(K, 7)}, 1),
               mk({T(Y, 9), GJ2}, 3, true)};
    s.hands[0] = {T(R, 4), T(R, 6, 1), T(Y, 9, 1), T(K, 11)};
    apply(g, s);
    for (int p = 0; p < 3; ++p) markOpened(g, p, false);
    markOpened(g, 3, true);
    V snap = snapshot(g);
    CHECK_EQ(g.swapJoker(0, T(R, 6, 1), 0).error, std::string("Bu taş okeyin yerine konamaz"));
    CHECK_EQ(g.swapJoker(0, T(R, 4), 1).error, std::string("Bu perde okey yok"));
    CHECK_EQ(g.swapJoker(0, T(Y, 9, 1), 2).error, std::string("Çiftteki okey alınamaz"));
    CHECK_EQ(g.swapJoker(0, T(R, 4, 1), 0).error, std::string("Bu taş sende yok"));
    CHECK_EQ(g.swapJoker(0, T(R, 4), 9).error, std::string("Böyle bir per yok"));
    CHECK_EQ(snapshot(g), snap);
    CHECK(g.swapJoker(0, T(R, 4), 0).ok);
    CHECK(contains(g.player(0).hand, GJ1));
    CHECK(!contains(g.player(0).hand, T(R, 4)));
    CHECK_EQ(g.table()[0].tiles[1].id, T(R, 4));
    CHECK(!g.table()[0].hasJoker());
    CHECK_EQ(g.table()[0].owner, 2);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 1 && ev[0].type == EvType::SwapJoker && ev[0].amount == GJ1 && ev[0].tile == T(R, 4) &&
          ev[0].meld == 0);
    if (!ev.empty()) CHECK_EQ(ev[0].text, std::string("Okeyi aldın"));
    CHECK(conserved(g));
    // the freed okey can be laid again and swapping a joker with a joker is refused
    CHECK(g.addToMeld(0, GJ1, 1).ok);
    CHECK(g.table()[1].hasJoker());
    Setup t = s;
    t.hands[0] = {T(K, 11), T(K, 12)};
    t.table = {mk({T(R, 3), GJ1, T(R, 5)}, 2)};
    t.hands[1] = {GJ2};
    t.seat = 1;
    apply(g, t);
    markOpened(g, 1, false);
    markOpened(g, 2, false);
    CHECK_EQ(g.swapJoker(1, GJ2, 0).error, std::string("Okeyin yerine okey konmaz"));
    // group ambiguity: any missing color may replace the joker of a 3-group
    Setup u;
    u.table = {mk({T(Y, 7), T(B, 7), GJ1}, 2)};
    u.hands[0] = {T(R, 7), T(K, 11)};
    apply(g, u);
    markOpened(g, 0, false);
    markOpened(g, 2, false);
    CHECK(g.swapJoker(0, T(R, 7), 0).ok);
    CHECK_EQ(cols(g.table()[0]), (V{Y, B, R}));
    CHECK(contains(g.player(0).hand, GJ1));
}

void testDiscardPenalties() {
    Game g;
    g.setPlayer(1, "Hacı Rıza", false);
    g.setPlayer(2, "Kel Mahmut", false);
    Setup s;
    s.hands[0] = {GJ1, T(R, 9), T(B, 2)};
    apply(g, s);
    CHECK(g.discard(0, GJ1).ok);
    CHECK_EQ(g.player(0).handPenalty, 101);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK_EQ(countType(ev, EvType::Penalty), 1);
    const GameEvent* pe = findEv(ev, EvType::Penalty);
    CHECK(pe && pe->text == "Okey attın: 101 ceza" && pe->amount == 101 && pe->player == 0);
    const GameEvent* de = findEv(ev, EvType::Discard);
    CHECK(de && de->text == "Okey attın");
    CHECK(ev.size() == 3 && ev[0].type == EvType::Discard && ev[1].type == EvType::Penalty &&
          ev[2].type == EvType::TurnStart);

    // işlek
    Setup t;
    t.table = {mk({T(R, 3), T(R, 4), T(R, 5)}, 3)};
    t.hands[0] = {T(R, 6), T(R, 8), T(B, 2)};
    apply(g, t);
    markOpened(g, 3, false);
    CHECK(g.isPlayableOnTable(T(R, 6)));
    CHECK(!g.isPlayableOnTable(T(R, 8)));
    CHECK(g.discard(0, T(R, 6)).ok);
    CHECK_EQ(g.player(0).handPenalty, 101);
    ev = g.drainEvents();
    pe = findEv(ev, EvType::Penalty);
    CHECK(pe && pe->text == "İşlek taş attın: 101 ceza");
    // not işlek
    apply(g, t);
    markOpened(g, 3, false);
    CHECK(g.discard(0, T(R, 8)).ok);
    CHECK_EQ(g.player(0).handPenalty, 0);
    CHECK_EQ(countType(g.drainEvents(), EvType::Penalty), 0);
    // a bot's işlek discard
    Setup t2 = t;
    t2.hands[2] = t2.hands[0];
    t2.hands[0] = {T(K, 12)};
    t2.seat = 2;
    apply(g, t2);
    markOpened(g, 3, false);
    CHECK(g.discard(2, T(R, 6)).ok);
    ev = g.drainEvents();
    pe = findEv(ev, EvType::Penalty);
    CHECK(pe && pe->text == "Kel Mahmut işlek taş attı: 101 ceza");
    // okey that also fits the table: only one penalty
    Setup t3 = t;
    t3.hands[0] = {GJ1, T(R, 8), T(B, 2)};
    apply(g, t3);
    markOpened(g, 3, false);
    CHECK(g.isPlayableOnTable(GJ1));
    CHECK(g.discard(0, GJ1).ok);
    CHECK_EQ(g.player(0).handPenalty, 101);
    CHECK_EQ(countType(g.drainEvents(), EvType::Penalty), 1);
    // bot okey discard text
    Setup t4 = t3;
    t4.hands[1] = t4.hands[0];
    t4.hands[0] = {T(K, 12)};
    t4.seat = 1;
    apply(g, t4);
    CHECK(g.discard(1, GJ1).ok);
    ev = g.drainEvents();
    pe = findEv(ev, EvType::Penalty);
    CHECK(pe && pe->text == "Hacı Rıza okey attı: 101 ceza");
    // rule switches
    Setup t5 = t3;
    t5.cfg.penaltyJokerDiscard = false;
    apply(g, t5);
    markOpened(g, 3, false);
    CHECK(g.discard(0, GJ1).ok);
    CHECK_EQ(g.player(0).handPenalty, 0); // and never the işlek penalty for a joker
    Setup t6 = t;
    t6.cfg.penaltyPlayableDiscard = false;
    apply(g, t6);
    markOpened(g, 3, false);
    CHECK(g.discard(0, T(R, 6)).ok);
    CHECK_EQ(g.player(0).handPenalty, 0);
    // custom penalty size
    Setup t7 = t;
    t7.cfg.penalty = 50;
    apply(g, t7);
    markOpened(g, 3, false);
    CHECK(g.discard(0, T(R, 6)).ok);
    CHECK_EQ(g.player(0).handPenalty, 50);
    ev = g.drainEvents();
    pe = findEv(ev, EvType::Penalty);
    CHECK(pe && pe->text == "İşlek taş attın: 50 ceza");

    // finishing discards are never penalised
    Setup f = t;
    f.hands[0] = {T(R, 6)};
    apply(g, f);
    markOpened(g, 0, false);
    markOpened(g, 3, false);
    CHECK(g.discard(0, T(R, 6)).ok);
    CHECK_EQ(g.player(0).handPenalty, 0);
    CHECK(g.handState() == HandState::HandOver);
    CHECK_EQ(g.lastHandResult().score[0], -101);
    Setup f2 = t;
    f2.hands[0] = {GJ1};
    apply(g, f2);
    markOpened(g, 0, false);
    markOpened(g, 3, false);
    CHECK(g.discard(0, GJ1).ok);
    CHECK_EQ(g.player(0).handPenalty, 0);
    CHECK(g.lastHandResult().finishedWithJoker);
    CHECK_EQ(g.lastHandResult().score[0], -202);
}

// Winner / unopened / series / pairs scoring and the multipliers.
void testScoring() {
    Game g;
    Setup s;
    s.hands[0] = {T(R, 9)};
    s.hands[1] = {T(Y, 1), T(Y, 2), T(B, 13)}; // unopened
    s.hands[2] = {T(B, 5), GJ1};               // series opener: 5, and 101 for the okey left in hand
    s.hands[3] = {T(Y, 4), T(K, 6)};           // pairs opener: 10 x2
    apply(g, s);
    markOpened(g, 0, false);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    g.debugPlayer(1).handPenalty = 101;
    std::array<int, 4> before;
    for (int p = 0; p < 4; ++p) before[p] = g.player(p).totalScore;
    const int starterBefore = g.starter();
    CHECK(g.discard(0, T(R, 9)).ok);
    CHECK(g.handState() == HandState::HandOver);
    const HandResult& r = g.lastHandResult();
    CHECK(r.reason == HandEndReason::PlayerFinished);
    CHECK_EQ(r.winner, 0);
    CHECK_EQ(r.multiplier, 1);
    CHECK(!r.finishedWithJoker && !r.finishedWithPairs && !r.finishedInOneGo);
    CHECK_EQ(r.score[0], -101);
    CHECK_EQ(r.score[1], 202 + 101);
    CHECK_EQ(r.score[2], 106);
    CHECK_EQ(r.score[3], 20);
    CHECK_EQ(r.remaining[0], 0);
    CHECK_EQ(r.remaining[1], 16);
    CHECK_EQ(r.remaining[2], 5);
    CHECK_EQ(r.remaining[3], 10);
    CHECK_EQ(r.penalties[1], 101);
    CHECK_EQ(r.penalties[2], 101); // elinde okey kalan: 101 ceza
    CHECK_EQ(r.penalties[0], 0);
    for (int p = 0; p < 4; ++p) {
        CHECK_EQ(g.player(p).totalScore, before[p] + r.score[p]);
        CHECK_EQ(g.player(p).handScores.back(), r.score[p]);
    }
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(ev.size() == 2 && ev[0].type == EvType::Discard && ev[1].type == EvType::HandEnd);
    if (ev.size() == 2) {
        CHECK_EQ(ev[1].player, 0);
        CHECK_EQ(ev[1].text, std::string("Eli bitirdin!"));
    }
    // actions after the hand are refused
    CHECK_EQ(g.drawFromPile(1).error, std::string("Şu an oynanan bir el yok"));
    CHECK(!g.discard(0, T(R, 9)).ok);
    // next hand: starter moves right, scores carry over
    g.startNextHand();
    CHECK(g.handState() == HandState::Playing);
    CHECK_EQ(g.handIndex(), 1);
    CHECK_EQ(g.starter(), Game::rightOf(starterBefore));
    CHECK_EQ(g.current(), g.starter());
    CHECK_EQ((int)g.player(g.starter()).hand.size(), 22);
    CHECK_EQ(g.player(1).totalScore, 303);
    CHECK_EQ(g.player(1).handPenalty, 0);
    CHECK(!g.player(0).opened);
    CHECK(g.table().empty());
    CHECK(conserved(g));
    ev = g.drainEvents();
    CHECK(ev.size() == 2 && ev[0].type == EvType::HandStart && ev[0].amount == 1 && ev[1].type == EvType::TurnStart);

    // okey finish: x2 for everybody except penalties
    Setup o = s;
    o.hands[0] = {GJ1};
    o.hands[2] = {T(B, 5), T(K, 9)};
    apply(g, o);
    markOpened(g, 0, false);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    g.debugPlayer(1).handPenalty = 101;
    CHECK(g.discard(0, GJ1).ok);
    const HandResult& ro = g.lastHandResult();
    CHECK(ro.finishedWithJoker);
    CHECK_EQ(ro.multiplier, 2);
    CHECK_EQ(ro.score[0], -202);
    CHECK_EQ(ro.score[1], 404 + 101);
    CHECK_EQ(ro.score[2], 14 * 2);
    CHECK_EQ(ro.score[3], 10 * 2 * 2);
    ev = g.drainEvents();
    const GameEvent* he = findEv(ev, EvType::HandEnd);
    CHECK(he && he->text == "Eli bitirdin! (okeyle bitiş, ×2)" && he->amount == 2);

    // pairs winner
    apply(g, s);
    markOpened(g, 0, true);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    CHECK(g.discard(0, T(R, 9)).ok);
    CHECK(g.lastHandResult().finishedWithPairs);
    CHECK_EQ(g.lastHandResult().multiplier, 2);
    CHECK_EQ(g.lastHandResult().score[0], -202);
    CHECK_EQ(g.lastHandResult().score[1], 404);
    CHECK_EQ(g.lastHandResult().score[2], 5 * 2 + 101); // the okey penalty is not doubled
    CHECK_EQ(g.lastHandResult().score[3], 40);
}

// Elden bitiş: while nobody has opened, a player lays the whole hand at once and finishes (x2), combined
// with okey (x4) and pairs (x8). Opening and finishing in one turn after someone else opened is a normal finish.
void testEldenBitis() {
    Game g;
    g.setPlayer(3, "Emekli Nuri", false);
    Setup s;
    G series = {{T(R, 13), T(K, 13), T(Y, 13)}, {T(B, 11), T(B, 12), T(B, 13)}, {T(Y, 5), T(Y, 6), T(Y, 7), T(Y, 8)}};
    for (const V& m : series) s.hands[3].insert(s.hands[3].end(), m.begin(), m.end());
    s.hands[3].push_back(T(B, 4));
    s.hands[0] = {T(Y, 1), T(Y, 2), T(B, 13, 1)}; // unopened
    s.hands[1] = {T(K, 9), T(K, 10)};             // 19 (opened in the first case)
    s.hands[2] = {T(R, 2)};                       // unopened
    s.seat = 3;

    // somebody (seat 1) had already opened: not elden
    apply(g, s);
    markOpened(g, 1, false);
    CHECK(g.openHand(3, series).ok);
    std::vector<GameEvent> ev = g.drainEvents();
    CHECK(!ev.empty() && ev[0].text == "Emekli Nuri eli açtı (101)");
    CHECK(g.discard(3, T(B, 4)).ok);
    HandResult r = g.lastHandResult();
    CHECK(!r.finishedInOneGo);
    CHECK_EQ(r.multiplier, 1);
    CHECK_EQ(r.score[3], -101);
    CHECK_EQ(r.score[0], 202);
    CHECK_EQ(r.score[1], 19);
    ev = g.drainEvents();
    const GameEvent* he = findEv(ev, EvType::HandEnd);
    CHECK(he && he->text == "Emekli Nuri eli bitirdi!");

    // nobody had opened: elden
    apply(g, s);
    CHECK(g.openHand(3, series).ok);
    g.drainEvents();
    CHECK(g.discard(3, T(B, 4)).ok);
    r = g.lastHandResult();
    CHECK(r.finishedInOneGo);
    CHECK(!r.finishedWithJoker && !r.finishedWithPairs);
    CHECK_EQ(r.multiplier, 2);
    CHECK_EQ(r.score[3], -202);
    CHECK_EQ(r.score[0], 404);
    CHECK_EQ(r.score[1], 404);
    CHECK_EQ(r.score[2], 404);
    ev = g.drainEvents();
    he = findEv(ev, EvType::HandEnd);
    CHECK(he && he->text == "Emekli Nuri eli bitirdi! (elden bitiş, ×2)");

    // elden + okey
    Setup s2 = s;
    s2.hands[3].back() = GJ1;
    apply(g, s2);
    CHECK(g.openHand(3, series).ok);
    CHECK(g.discard(3, GJ1).ok);
    r = g.lastHandResult();
    CHECK(r.finishedInOneGo && r.finishedWithJoker);
    CHECK_EQ(r.multiplier, 4);
    CHECK_EQ(r.score[3], -404);
    CHECK_EQ(r.score[0], 808);

    // elden + okey + pairs
    Setup s3;
    G pairs = {{T(R, 5), T(R, 5, 1)}, {T(B, 9), T(B, 9, 1)}, {T(Y, 1), T(Y, 1, 1)}, {T(K, 12), T(K, 12, 1)},
               {T(R, 13), T(R, 13, 1)}};
    for (const V& m : pairs) s3.hands[3].insert(s3.hands[3].end(), m.begin(), m.end());
    s3.hands[3].push_back(GJ2);
    s3.hands[0] = {T(Y, 2)};
    s3.hands[1] = {T(B, 2)};
    s3.hands[2] = {T(R, 2)};
    s3.seat = 3;
    apply(g, s3);
    CHECK(g.openHand(3, pairs).ok);
    CHECK(g.discard(3, GJ2).ok);
    r = g.lastHandResult();
    CHECK(r.finishedInOneGo && r.finishedWithJoker && r.finishedWithPairs);
    CHECK_EQ(r.multiplier, 8);
    CHECK_EQ(r.score[3], -808);
    CHECK_EQ(r.score[0], 202 * 8);
    ev = g.drainEvents();
    he = findEv(ev, EvType::HandEnd);
    CHECK(he && he->text == "Emekli Nuri eli bitirdi! (okeyle, çiftten, elden bitiş, ×8)");
    CHECK_EQ(g.lastHandResult().penalties[3], 0);
}

void testPileExhaustion() {
    Game g;
    Setup s;
    s.hands[0] = {T(R, 9), T(B, 2), T(Y, 1)};
    s.hands[1] = {T(Y, 3), T(Y, 4)};
    s.hands[2] = {T(B, 5)};
    s.hands[3] = {T(Y, 12), T(K, 12)};
    s.stage = TurnStage::NeedDraw;
    apply(g, s);
    markOpened(g, 2, false);
    markOpened(g, 3, true);
    g.debugPlayer(1).handPenalty = 101;
    // leave exactly one tile in the pile (the rest goes into seat 1's discard pile)
    V& pile = g.debugPile();
    const int last = pile.back();
    pile.pop_back();
    for (int id : pile) g.debugPlayer(1).discards.push_back(id);
    pile.assign(1, last);
    CHECK(conserved(g));
    CHECK(g.drawFromPile(0).ok);
    CHECK_EQ(g.pileCount(), 0);
    g.drainEvents();
    int toss = T(R, 9);
    CHECK(g.discard(0, toss).ok);
    CHECK(g.handState() == HandState::HandOver);
    const HandResult& r = g.lastHandResult();
    CHECK(r.reason == HandEndReason::PileExhausted);
    CHECK_EQ(r.winner, -1);
    CHECK_EQ(r.multiplier, 1);
    CHECK_EQ(r.score[0], 202 + g.player(0).handPenalty);
    CHECK_EQ(r.score[1], 202 + 101);
    CHECK_EQ(r.score[2], 5);
    CHECK_EQ(r.score[3], 24 * 2);
    std::vector<GameEvent> ev = g.drainEvents();
    const GameEvent* he = findEv(ev, EvType::HandEnd);
    CHECK(he && he->player == -1 && he->text == "Ortada taş kalmadı, el bitti");
    CHECK_EQ(countType(ev, EvType::TurnStart), 0);
}

// Ends the current hand at once through pile exhaustion (the pile moves into a discard pile).
void exhaustHand(Game& g) {
    V& pile = g.debugPile();
    const int holder = Game::rightOf(g.current());
    for (int id : pile) g.debugPlayer(holder).discards.push_back(id);
    pile.clear();
    const int seat = g.current();
    if (g.stage() == TurnStage::NeedDraw) {
        // put one tile back so the seat can draw
        pile.push_back(g.debugPlayer(holder).discards.back());
        g.debugPlayer(holder).discards.pop_back();
        CHECK(g.drawFromPile(seat).ok);
    }
    CHECK(g.discard(seat, firstNonJoker(g, seat)).ok);
}

void testMatchEnd() {
    Game g;
    RulesConfig cfg;
    cfg.numHands = 3;
    g.setRules(cfg);
    CHECK_EQ(g.numHands(), 3);
    g.startMatch(2024);
    g.drainEvents();
    const int first = g.starter();
    int lead = -1;
    for (int h = 0; h < 3; ++h) {
        CHECK_EQ(g.handIndex(), h);
        CHECK_EQ(g.starter(), (first + h) % 4);
        if (h == 2) {
            // a seat that isn't moving gets a tiny opened hand in the last hand: it must become the leader
            lead = Game::rightOf(g.current());
            PlayerInfo& p = g.debugPlayer(lead);
            p.opened = true;
            p.openedTurn = 0;
            V keep;
            V give;
            for (int id : p.hand) {
                if (keep.empty() && !g.okey().isJoker(id)) keep.push_back(id);
                else give.push_back(id);
            }
            p.hand = keep;
            for (int id : give) p.discards.push_back(id);
        }
        exhaustHand(g);
        std::vector<GameEvent> ev = g.drainEvents();
        CHECK_EQ(countType(ev, EvType::HandEnd), 1);
        if (h < 2) {
            CHECK(g.handState() == HandState::HandOver);
            CHECK_EQ(countType(ev, EvType::MatchEnd), 0);
            g.startNextHand();
            g.drainEvents();
        } else {
            CHECK(g.handState() == HandState::MatchOver);
            const GameEvent* me = findEv(ev, EvType::MatchEnd);
            CHECK(me != nullptr);
            CHECK(ev.back().type == EvType::MatchEnd);
            if (me) {
                CHECK_EQ(me->player, g.leaderSeat());
                CHECK_EQ(me->player, lead);
                CHECK_EQ(me->text, lead == 0 ? std::string("Maç bitti! Sen kazandın!")
                                             : "Maç bitti! Kazanan: " + g.player(lead).name);
            }
        }
    }
    for (int p = 0; p < 4; ++p) {
        CHECK_EQ((int)g.player(p).handScores.size(), 3);
        int sum = 0;
        for (int x : g.player(p).handScores) sum += x;
        CHECK_EQ(sum, g.player(p).totalScore);
    }
    V snap = snapshot(g);
    g.startNextHand(); // no-op after the match
    CHECK_EQ(snapshot(g), snap);
    CHECK(g.handState() == HandState::MatchOver);

    // leaderSeat ties -> lower seat; human winner text
    Game t;
    t.setRules(cfg);
    t.startMatch(1);
    CHECK_EQ(t.leaderSeat(), 0);
    t.debugPlayer(3).totalScore = -50;
    t.debugPlayer(1).totalScore = -50;
    CHECK_EQ(t.leaderSeat(), 1);
    RulesConfig one;
    one.numHands = 1;
    t.setRules(one);
    t.startMatch(9);
    t.drainEvents();
    t.debugPlayer(0).totalScore = -1000;
    exhaustHand(t);
    const std::vector<GameEvent> tev = t.drainEvents();
    const GameEvent* me = findEv(tev, EvType::MatchEnd);
    CHECK(me && me->player == 0 && me->text == "Maç bitti! Sen kazandın!");
    // tied lowest totals: a shared first place (player stays leaderSeat)
    auto tieText = [&](std::vector<int> tied, int& player) {
        Game u;
        u.setRules(one);
        u.startMatch(9);
        u.drainEvents();
        Game probe = u; // learn this hand's scores, then pick totals so the tied seats end level
        exhaustHand(probe);
        for (int s = 0; s < 4; ++s) {
            const bool co = std::find(tied.begin(), tied.end(), s) != tied.end();
            u.debugPlayer(s).totalScore = (co ? -1000 : 0) - probe.lastHandResult().score[s];
        }
        exhaustHand(u);
        const std::vector<GameEvent> uev = u.drainEvents();
        const GameEvent* ue = findEv(uev, EvType::MatchEnd);
        player = ue ? ue->player : -2;
        return ue ? ue->text : std::string();
    };
    int who = -1;
    CHECK_EQ(tieText({2, 3}, who), std::string("Maç berabere bitti! Birinciliği Kel Mahmut ve Emekli Nuri paylaştı"));
    CHECK_EQ(who, 2);
    CHECK_EQ(tieText({0, 2}, who), std::string("Maç berabere bitti! Birinciliği Kel Mahmut ile paylaştın"));
    CHECK_EQ(who, 0);
    CHECK_EQ(tieText({1, 2, 3}, who),
             std::string("Maç berabere bitti! Birinciliği Hacı Rıza, Kel Mahmut ve Emekli Nuri paylaştı"));
    CHECK_EQ(who, 1);
    CHECK_EQ(tieText({0, 1, 2, 3}, who), std::string("Maç berabere bitti! Birincilik herkesin"));
    CHECK_EQ(tieText({3}, who), std::string("Maç bitti! Kazanan: Emekli Nuri"));
    CHECK_EQ(who, 3);
    // startMatch resets the scores
    t.startMatch(9);
    for (int p = 0; p < 4; ++p) {
        CHECK_EQ(t.player(p).totalScore, 0);
        CHECK(t.player(p).handScores.empty());
    }
}

// ---------------------------------------------------------------------------------------------------------
// Fuzzer

struct Stats {
    long hands = 0, matches = 0, finishes = 0, exhausted = 0, opens = 0, pairOpens = 0, lays = 0, adds = 0,
         swaps = 0, takes = 0, returns = 0, okeyFinish = 0, pairFinish = 0, eldenFinish = 0, penalties = 0,
         rejected = 0, actions = 0, probes = 0;
};

enum class Policy { Random, Greedy };

int meldValue(const V& ids, const OkeyInfo& ok, bool pair) {
    Meld m;
    if (!makeMeld(ids, ok, m, pair)) return -1;
    return pair ? 1 : m.value();
}

// Greedy series finder: plain runs/groups first (random order), then melds completed with jokers.
G greedySeries(const V& hand, const OkeyInfo& ok, Rng& rng) {
    V jokers, plain;
    for (int id : hand) (ok.isJoker(id) ? jokers : plain).push_back(id);
    rng.shuffle(plain);
    std::vector<char> used(plain.size(), 0);
    G melds;

    auto runs = [&](bool withJokers) {
        for (int c = 0; c < NUM_COLORS; ++c) {
            int slot[NUM_NUMBERS + 2];
            std::fill(slot, slot + NUM_NUMBERS + 2, -1);
            for (int i = 0; i < (int)plain.size(); ++i) {
                if (used[i] || ok.faceColor(plain[i]) != c) continue;
                const int n = ok.faceNumber(plain[i]);
                if (slot[n] < 0) slot[n] = i;
            }
            int n = 1;
            while (n <= NUM_NUMBERS) {
                if (slot[n] < 0) {
                    ++n;
                    continue;
                }
                int e = n;
                while (e + 1 <= NUM_NUMBERS && slot[e + 1] >= 0) ++e;
                const int len = e - n + 1;
                int next = e + 1;
                V m;
                if (len >= 3) {
                    for (int k = n; k <= e; ++k) m.push_back(plain[slot[k]]);
                } else if (withJokers && !jokers.empty()) {
                    if (len == 2 && e + 1 <= NUM_NUMBERS) m = {plain[slot[n]], plain[slot[e]], jokers.back()};
                    else if (len == 2 && n > 1) m = {jokers.back(), plain[slot[n]], plain[slot[e]]};
                    else if (len == 1 && n + 2 <= NUM_NUMBERS && slot[n + 2] >= 0) {
                        m = {plain[slot[n]], jokers.back(), plain[slot[n + 2]]};
                        next = n + 3;
                    }
                }
                if (!m.empty()) {
                    for (int id : m) {
                        if (ok.isJoker(id)) {
                            jokers.pop_back();
                        } else {
                            for (int i = 0; i < (int)plain.size(); ++i)
                                if (plain[i] == id) used[i] = 1;
                        }
                    }
                    melds.push_back(m);
                }
                n = next;
            }
        }
    };
    auto groups = [&](bool withJokers) {
        for (int n = 1; n <= NUM_NUMBERS; ++n) {
            int pick[NUM_COLORS] = {-1, -1, -1, -1};
            for (int i = 0; i < (int)plain.size(); ++i) {
                if (used[i] || ok.faceNumber(plain[i]) != n) continue;
                const int c = ok.faceColor(plain[i]);
                if (pick[c] < 0) pick[c] = i;
            }
            V m;
            for (int c = 0; c < NUM_COLORS; ++c)
                if (pick[c] >= 0) m.push_back(plain[pick[c]]);
            if (m.size() == 2 && withJokers && !jokers.empty()) {
                m.push_back(jokers.back());
                jokers.pop_back();
            }
            if (m.size() < 3) continue;
            for (int c = 0; c < NUM_COLORS; ++c)
                if (pick[c] >= 0) used[pick[c]] = 1;
            melds.push_back(m);
        }
    };
    const bool runsFirst = rng.chance(0.5f);
    if (runsFirst) {
        runs(false);
        groups(false);
    } else {
        groups(false);
        runs(false);
    }
    if (rng.chance(0.5f)) {
        runs(true);
        groups(true);
    } else {
        groups(true);
        runs(true);
    }
    G valid;
    for (const V& m : melds) {
        const bool okMeld = meldValue(m, ok, false) > 0;
        CHECK(okMeld); // the finder must only produce legal melds
        if (okMeld) valid.push_back(m);
    }
    return valid;
}

G greedyPairs(const V& hand, const OkeyInfo& ok) {
    std::map<int, V> byFace;
    V jokers;
    for (int id : hand) {
        if (ok.isJoker(id)) jokers.push_back(id);
        else byFace[ok.faceColor(id) * 16 + ok.faceNumber(id)].push_back(id);
    }
    G pairs;
    V singles;
    for (auto& kv : byFace) {
        V& v = kv.second;
        size_t i = 0;
        for (; i + 1 < v.size(); i += 2) pairs.push_back({v[i], v[i + 1]});
        if (i < v.size()) singles.push_back(v[i]);
    }
    std::sort(singles.begin(), singles.end(),
              [&](int a, int b) { return ok.faceNumber(a) > ok.faceNumber(b); });
    size_t si = 0;
    while (!jokers.empty() && si < singles.size()) {
        pairs.push_back({singles[si++], jokers.back()});
        jokers.pop_back();
    }
    if (jokers.size() == 2) pairs.push_back({jokers[0], jokers[1]});
    return pairs;
}

int totalTiles(const G& g) {
    int n = 0;
    for (const V& v : g) n += (int)v.size();
    return n;
}

// Drops the weakest melds (never the one holding `keep`) until at least one tile stays in hand.
void keepOne(G& melds, int handSize, int keep, const OkeyInfo& ok, bool pairs) {
    while (!melds.empty() && totalTiles(melds) >= handSize) {
        int worst = -1, worstValue = 1 << 30;
        for (int i = 0; i < (int)melds.size(); ++i) {
            if (keep >= 0 && contains(melds[i], keep)) continue;
            const int v = meldValue(melds[i], ok, pairs);
            if (v < worstValue) {
                worstValue = v;
                worst = i;
            }
        }
        if (worst < 0) {
            melds.clear();
            return;
        }
        melds.erase(melds.begin() + worst);
    }
}

int seriesTotal(const G& melds, const OkeyInfo& ok) {
    int t = 0;
    for (const V& m : melds) t += meldValue(m, ok, false);
    return t;
}

class Fuzzer {
public:
    Fuzzer(const RulesConfig& cfg, std::array<Policy, 4> pol, uint64_t policySeed, Stats& st)
        : cfg_(cfg), pol_(pol), rng_(policySeed), st_(st) {
        g_.setPlayer(0, "Sen", true);
        g_.setPlayer(1, "Hacı Rıza", false);
        g_.setPlayer(2, "Kel Mahmut", false);
        g_.setPlayer(3, "Emekli Nuri", false);
    }

    std::vector<std::string> log; // every event, for determinism checks

    void runMatch(uint64_t seed) {
        g_.setRules(cfg_);
        g_.startMatch(seed);
        std::vector<GameEvent> ev = g_.drainEvents();
        CHECK(ev.size() == 3 && ev[0].type == EvType::MatchStart && ev[1].type == EvType::HandStart &&
              ev[2].type == EvType::TurnStart);
        record(ev);
        checkInvariants(g_);
        for (;;) {
            beginHandBookkeeping();
            int turns = 0;
            bool stuck = false;
            while (g_.handState() == HandState::Playing) {
                if (++turns > 1500) {
                    ++g_checks;
                    reportFailure(__FILE__, __LINE__, "hand did not terminate");
                    stuck = true;
                    break;
                }
                const int seat = g_.current();
                const int turnBefore = g_.turnNumber();
                if (pol_[seat] == Policy::Greedy) turnGreedy(seat);
                else turnRandom(seat);
                CHECK(g_.handState() != HandState::Playing || g_.turnNumber() == turnBefore + 1);
            }
            if (stuck) return;
            verifyHandEnd();
            ++st_.hands;
            if (g_.handState() == HandState::MatchOver) {
                ++st_.matches;
                CHECK(sawMatchEnd_);
                CHECK_EQ(matchEndPlayer_, g_.leaderSeat());
                CHECK_EQ(g_.handIndex() + 1, cfg_.numHands);
                return;
            }
            CHECK(!sawMatchEnd_);
            const int prevStarter = g_.starter();
            g_.startNextHand();
            ev = g_.drainEvents();
            CHECK(ev.size() == 2 && ev[0].type == EvType::HandStart && ev[1].type == EvType::TurnStart);
            record(ev);
            CHECK_EQ(g_.starter(), Game::rightOf(prevStarter));
            CHECK(g_.stage() == TurnStage::Play);
            checkInvariants(g_);
        }
    }

private:
    RulesConfig cfg_;
    std::array<Policy, 4> pol_;
    Game g_;
    Rng rng_;
    Stats& st_;
    std::array<int, 4> totalsBefore_{};
    std::array<int, 4> penaltySum_{};
    bool sawHandEnd_ = false, sawMatchEnd_ = false;
    int matchEndPlayer_ = -1;

    void beginHandBookkeeping() {
        for (int s = 0; s < 4; ++s) {
            totalsBefore_[s] = g_.player(s).totalScore;
            penaltySum_[s] = 0;
        }
        sawHandEnd_ = sawMatchEnd_ = false;
        matchEndPlayer_ = -1;
    }

    void record(const std::vector<GameEvent>& ev) {
        for (const GameEvent& e : ev) {
            CHECK(!e.text.empty());
            if (e.type == EvType::DrawPile && isValidTile(e.tile) && !g_.player(e.player).human)
                CHECK(e.text.find(tileNameTR(e.tile, g_.okey())) == std::string::npos); // face stays hidden
            if (e.type == EvType::Penalty) {
                penaltySum_[e.player] += e.amount;
                ++st_.penalties;
            }
            if (e.type == EvType::HandEnd) sawHandEnd_ = true;
            if (e.type == EvType::MatchEnd) {
                sawMatchEnd_ = true;
                matchEndPlayer_ = e.player;
            }
            log.push_back(std::to_string((int)e.type) + "|" + std::to_string(e.player) + "|" + std::to_string(e.tile) +
                          "|" + std::to_string(e.meld) + "|" + std::to_string(e.count) + "|" +
                          std::to_string(e.amount) + "|" + e.text);
        }
    }

    // Runs one action; rejected actions must leave no trace.
    ActionResult act(const std::function<ActionResult()>& f) {
        const V before = snapshot(g_);
        const ActionResult r = f();
        const std::vector<GameEvent> ev = g_.drainEvents();
        ++st_.actions;
        if (!r.ok) {
            ++st_.rejected;
            CHECK(!r.error.empty());
            CHECK(ev.empty());
            CHECK(snapshot(g_) == before);
        } else {
            CHECK(r.error.empty());
            CHECK(!ev.empty());
        }
        record(ev);
        checkInvariants(g_);
        return r;
    }

    void verifyHandEnd() {
        CHECK(sawHandEnd_);
        const HandResult& r = g_.lastHandResult();
        int mult = 1;
        if (r.reason == HandEndReason::PlayerFinished) {
            ++st_.finishes;
            CHECK(r.winner >= 0 && r.winner < 4);
            if (r.winner < 0 || r.winner >= 4) return;
            const PlayerInfo& w = g_.player(r.winner);
            CHECK(w.hand.empty());
            CHECK(w.opened);
            CHECK(!w.discards.empty());
            if (!w.discards.empty()) CHECK_EQ(r.finishedWithJoker, g_.okey().isJoker(w.discards.back()));
            CHECK_EQ(r.finishedWithPairs, w.openedWithPairs);
            bool othersOpened = false;
            for (int s = 0; s < 4; ++s) othersOpened = othersOpened || (s != r.winner && g_.player(s).opened);
            CHECK_EQ(r.finishedInOneGo, w.openedTurn == g_.turnNumber() && !othersOpened);
            if (r.finishedWithJoker) ++st_.okeyFinish;
            if (r.finishedWithPairs) ++st_.pairFinish;
            if (r.finishedInOneGo) ++st_.eldenFinish;
            mult = (r.finishedWithJoker ? 2 : 1) * (r.finishedWithPairs ? 2 : 1) * (r.finishedInOneGo ? 2 : 1);
        } else {
            ++st_.exhausted;
            CHECK(r.reason == HandEndReason::PileExhausted);
            CHECK_EQ(r.winner, -1);
            CHECK_EQ(g_.pileCount(), 0);
            CHECK(!r.finishedWithJoker && !r.finishedWithPairs && !r.finishedInOneGo);
        }
        CHECK_EQ(r.multiplier, mult);
        for (int s = 0; s < 4; ++s) {
            const PlayerInfo& p = g_.player(s);
            int expect;
            if (s == r.winner) expect = cfg_.winnerScore * mult;
            else if (!p.opened) expect = cfg_.unopenedScore * mult;
            else if (p.openedWithPairs) expect = g_.handPoints(s) * 2 * mult;
            else expect = g_.handPoints(s) * mult;
            expect += p.handPenalty;
            CHECK_EQ(r.score[s], expect);
            CHECK_EQ(r.remaining[s], g_.handPoints(s));
            CHECK_EQ(r.penalties[s], p.handPenalty);
            // (+ the end-of-hand penalty for every okey left in an opened loser's hand)
            const int okeyLeft = (s != r.winner && p.opened) ? cfg_.penalty * g_.jokersInHand(s) : 0;
            CHECK_EQ(p.handPenalty, penaltySum_[s] + okeyLeft);
            CHECK_EQ(p.totalScore, totalsBefore_[s] + r.score[s]);
            CHECK(!p.handScores.empty() && p.handScores.back() == r.score[s]);
            int sum = 0;
            for (int x : p.handScores) sum += x;
            CHECK_EQ(sum, p.totalScore);
        }
    }

    // ---- shared pieces ----

    void draw(int seat) {
        const ActionResult r = act([&] { return g_.drawFromPile(seat); });
        CHECK(r.ok);
    }

    void giveBack(int seat) {
        const int before = g_.player(seat).handPenalty;
        const ActionResult r = act([&] { return g_.returnLeftTile(seat); });
        CHECK(r.ok);
        CHECK_EQ(g_.player(seat).handPenalty, before + cfg_.penalty);
        ++st_.returns;
    }

    void discardTile(int seat, int tile) {
        const PlayerInfo& p = g_.player(seat);
        const bool finishing = p.hand.size() == 1;
        const bool joker = g_.okey().isJoker(tile);
        int expectPenalty = 0;
        if (!finishing) {
            if (joker) expectPenalty = cfg_.penaltyJokerDiscard ? cfg_.penalty : 0;
            else if (cfg_.penaltyPlayableDiscard && fitsAnyMeld(g_.table(), tile, g_.okey())) expectPenalty = cfg_.penalty;
        }
        const int before = p.handPenalty;
        const int pileBefore = g_.pileCount();
        const ActionResult r = act([&] { return g_.discard(seat, tile); });
        CHECK(r.ok);
        if (g_.handState() != HandState::Playing && !finishing && g_.player(seat).opened)
            expectPenalty += cfg_.penalty * g_.jokersInHand(seat); // the hand ended with okeys still in hand
        CHECK_EQ(g_.player(seat).handPenalty, before + expectPenalty);
        CHECK_EQ(g_.player(seat).discards.back(), tile);
        if (finishing) {
            CHECK(g_.handState() != HandState::Playing);
            CHECK_EQ(g_.lastHandResult().winner, seat);
        } else if (pileBefore == 0) {
            CHECK(g_.handState() != HandState::Playing);
            CHECK(g_.lastHandResult().reason == HandEndReason::PileExhausted);
        } else {
            CHECK(g_.handState() == HandState::Playing);
            CHECK_EQ(g_.current(), Game::rightOf(seat));
            CHECK(g_.stage() == TurnStage::NeedDraw);
        }
    }

    G randomGroups(int seat) {
        V hand = g_.player(seat).hand;
        rng_.shuffle(hand);
        const int mode = rng_.range(3); // 0 pairs, 1 series, 2 mixed
        const int ng = 1 + rng_.range(5);
        G groups;
        size_t pos = 0;
        for (int i = 0; i < ng; ++i) {
            int size = mode == 0 ? 2 : mode == 1 ? 3 + rng_.range(3) : 2 + rng_.range(3);
            if (rng_.chance(0.05f)) size = rng_.range(2);
            V gr;
            for (int k = 0; k < size; ++k) {
                if (rng_.chance(0.03f)) gr.push_back(rng_.range(-2, NUM_TILES + 1)); // maybe not ours / invalid
                else if (rng_.chance(0.03f) && !gr.empty()) gr.push_back(gr.back()); // duplicate
                else if (pos < hand.size()) gr.push_back(hand[pos++]);
            }
            groups.push_back(gr);
        }
        return groups;
    }

    // Random (mostly illegal) actions; each must be rejected cleanly unless it happens to be legal.
    void probe(int seat) {
        ++st_.probes;
        const V hand = g_.player(seat).hand;
        const int handTile = hand.empty() ? 0 : hand[rng_.range((int)hand.size())];
        const int anyTile = rng_.range(-1, NUM_TILES);
        const int nt = (int)g_.table().size();
        const int meldIdx = rng_.range(-1, nt);
        switch (rng_.range(9)) {
        case 0: {
            const G gr = randomGroups(seat);
            const V s0 = snapshot(g_);
            const OpenCheck c = g_.checkOpen(seat, gr);
            CHECK(snapshot(g_) == s0);
            const ActionResult r = act([&] { return g_.openHand(seat, gr); });
            CHECK_EQ(r.ok, c.valid);
            if (!r.ok) CHECK_EQ(r.error, c.error);
            else ++st_.opens;
            break;
        }
        case 1: {
            const G gr = randomGroups(seat);
            const V s0 = snapshot(g_);
            const OpenCheck c = g_.checkLay(seat, gr);
            CHECK(snapshot(g_) == s0);
            const ActionResult r = act([&] { return g_.layMelds(seat, gr); });
            CHECK_EQ(r.ok, c.valid);
            if (!r.ok) CHECK_EQ(r.error, c.error);
            else ++st_.lays;
            break;
        }
        case 2: {
            const int tile = rng_.chance(0.8f) ? handTile : anyTile;
            const AddSide side = (AddSide)rng_.range(3);
            Meld out;
            const int pend = g_.pendingLeftTile();
            const size_t minHand = (pend >= 0 && tile != pend) ? 2 : 1;
            const bool expect = g_.canWorkTable(seat) && contains(hand, tile) && meldIdx >= 0 && meldIdx < nt &&
                                g_.table()[meldIdx].kind != MeldKind::Pair && hand.size() > minHand &&
                                tryAddTile(g_.table()[meldIdx], tile, g_.okey(), side, out);
            const ActionResult r = act([&] { return g_.addToMeld(seat, tile, meldIdx, side); });
            CHECK_EQ(r.ok, expect);
            if (r.ok) {
                ++st_.adds;
                CHECK(g_.pendingLeftTile() != tile);
            }
            break;
        }
        case 3: {
            const int tile = rng_.chance(0.8f) ? handTile : anyTile;
            Meld out;
            int freed = -1;
            const bool expect = g_.canWorkTable(seat) && contains(hand, tile) && meldIdx >= 0 && meldIdx < nt &&
                                trySwapJoker(g_.table()[meldIdx], tile, g_.okey(), out, freed);
            const ActionResult r = act([&] { return g_.swapJoker(seat, tile, meldIdx); });
            CHECK_EQ(r.ok, expect);
            if (r.ok) {
                ++st_.swaps;
                CHECK(contains(g_.player(seat).hand, freed));
            }
            break;
        }
        case 4: { // wrong seat
            const int other = (seat + 1 + rng_.range(3)) % 4;
            const int oTile = g_.player(other).hand.empty() ? 0 : g_.player(other).hand[0];
            ActionResult r;
            switch (rng_.range(7)) {
            case 0: r = act([&] { return g_.drawFromPile(other); }); break;
            case 1: r = act([&] { return g_.takeFromLeft(other); }); break;
            case 2: r = act([&] { return g_.returnLeftTile(other); }); break;
            case 3: r = act([&] { return g_.discard(other, oTile); }); break;
            case 4: r = act([&] { return g_.openHand(other, {{oTile}}); }); break;
            case 5: r = act([&] { return g_.addToMeld(other, oTile, 0); }); break;
            default: r = act([&] { return g_.swapJoker(other, oTile, 0); }); break;
            }
            CHECK(!r.ok);
            CHECK_EQ(r.error, std::string("Sıra sende değil"));
            break;
        }
        case 5: { // discard something we don't hold
            int tile = anyTile;
            if (contains(hand, tile)) tile = -7;
            const ActionResult r = act([&] { return g_.discard(seat, tile); });
            CHECK(!r.ok);
            break;
        }
        case 6: { // wrong stage
            if (g_.stage() == TurnStage::Play) {
                CHECK(!act([&] { return g_.drawFromPile(seat); }).ok);
                CHECK(!act([&] { return g_.takeFromLeft(seat); }).ok);
            } else {
                CHECK(!act([&] { return g_.discard(seat, handTile); }).ok);
            }
            break;
        }
        case 7: { // return when nothing is pending
            if (g_.pendingLeftTile() < 0) CHECK(!act([&] { return g_.returnLeftTile(seat); }).ok);
            break;
        }
        default: { // pure queries never change anything
            const V s0 = snapshot(g_);
            (void)g_.canWorkTable(seat);
            (void)g_.canTakeFromLeft(seat);
            (void)g_.isPlayableOnTable(handTile);
            (void)g_.handPoints(seat);
            (void)g_.openedThisTurn(seat);
            (void)g_.checkOpen(seat, randomGroups(seat));
            (void)g_.checkLay(seat, randomGroups(seat));
            CHECK(snapshot(g_) == s0);
            CHECK(g_.drainEvents().empty());
            break;
        }
        }
    }

    // ---- random policy ----

    void turnRandom(int seat) {
        if (g_.stage() == TurnStage::NeedDraw) {
            if (g_.canTakeFromLeft(seat) && rng_.chance(0.35f)) {
                CHECK(act([&] { return g_.takeFromLeft(seat); }).ok);
                ++st_.takes;
            } else {
                draw(seat);
            }
        }
        const int probes = rng_.range(4);
        for (int i = 0; i < probes && g_.handState() == HandState::Playing; ++i) probe(seat);
        if (g_.handState() != HandState::Playing || g_.current() != seat) return;
        if (g_.pendingLeftTile() >= 0) {
            giveBack(seat);
            draw(seat);
            if (rng_.chance(0.5f)) probe(seat);
        }
        if (g_.handState() != HandState::Playing || g_.current() != seat) return;
        const V& hand = g_.player(seat).hand;
        discardTile(seat, hand[rng_.range((int)hand.size())]);
    }

    // ---- greedy policy ----

    bool leftTileUseful(int seat, int t) {
        const PlayerInfo& p = g_.player(seat);
        V hand = p.hand;
        hand.push_back(t);
        const OkeyInfo& ok = g_.okey();
        if (!p.opened) {
            G ser = greedySeries(hand, ok, rng_);
            keepOne(ser, (int)hand.size(), t, ok, false);
            if (containsIn(ser, t) && seriesTotal(ser, ok) >= g_.seriesOpenNeed()) return true;
            G prs = greedyPairs(hand, ok);
            keepOne(prs, (int)hand.size(), t, ok, true);
            return containsIn(prs, t) && (int)prs.size() >= g_.pairsOpenNeed();
        }
        if (fitsAnyMeld(g_.table(), t, ok)) return true;
        G more = p.openedWithPairs ? greedyPairs(hand, ok) : greedySeries(hand, ok, rng_);
        keepOne(more, (int)hand.size(), t, ok, p.openedWithPairs);
        return containsIn(more, t);
    }

    void tryOpen(int seat) {
        const OkeyInfo& ok = g_.okey();
        const int pend = g_.pendingLeftTile();
        for (int attempt = 0; attempt < 3; ++attempt) {
            const V hand = g_.player(seat).hand;
            G ser = greedySeries(hand, ok, rng_);
            keepOne(ser, (int)hand.size(), pend, ok, false);
            const bool serOk = !ser.empty() && seriesTotal(ser, ok) >= g_.seriesOpenNeed() &&
                               (pend < 0 || containsIn(ser, pend));
            G prs = greedyPairs(hand, ok);
            keepOne(prs, (int)hand.size(), pend, ok, true);
            const bool prOk = (int)prs.size() >= g_.pairsOpenNeed() && (pend < 0 || containsIn(prs, pend));
            if (!serOk && !prOk) continue;
            const bool usePairs = prOk && (!serOk || rng_.chance(0.5f));
            const G& melds = usePairs ? prs : ser;
            const OpenCheck c = g_.checkOpen(seat, melds);
            CHECK(c.valid);
            CHECK_EQ(c.pairs, usePairs);
            if (usePairs) CHECK_EQ(c.pairCount, (int)prs.size());
            else CHECK_EQ(c.value, seriesTotal(ser, ok));
            const ActionResult r = act([&] { return g_.openHand(seat, melds); });
            CHECK(r.ok);
            if (r.ok) {
                ++st_.opens;
                if (usePairs) ++st_.pairOpens;
                CHECK(g_.player(seat).opened);
                CHECK_EQ(g_.pendingLeftTile(), -1);
            }
            return;
        }
    }

    void workTable(int seat) {
        const OkeyInfo& ok = g_.okey();
        const bool pairs = g_.player(seat).openedWithPairs;
        // 1. more melds
        {
            const V hand = g_.player(seat).hand;
            const int pend = g_.pendingLeftTile();
            G more = pairs ? greedyPairs(hand, ok) : greedySeries(hand, ok, rng_);
            if (pend >= 0 && containsIn(more, pend)) keepOne(more, (int)hand.size(), pend, ok, pairs);
            else keepOne(more, (int)hand.size() - (pend >= 0 ? 1 : 0), -1, ok, pairs);
            if (!more.empty() && rng_.chance(0.9f)) {
                const OpenCheck c = g_.checkLay(seat, more);
                CHECK(c.valid);
                const ActionResult r = act([&] { return g_.layMelds(seat, more); });
                CHECK(r.ok);
                if (r.ok) ++st_.lays;
            }
        }
        // 2. grab jokers
        for (int pass = 0; pass < 4; ++pass) {
            bool did = false;
            const V hand = g_.player(seat).hand;
            for (int mi = 0; mi < (int)g_.table().size() && !did; ++mi) {
                const Meld& m = g_.table()[mi];
                if (m.kind == MeldKind::Pair || !m.hasJoker()) continue;
                for (int tile : hand) {
                    Meld out;
                    int freed = -1;
                    if (ok.isJoker(tile) || !trySwapJoker(m, tile, ok, out, freed)) continue;
                    const ActionResult r = act([&] { return g_.swapJoker(seat, tile, mi); });
                    CHECK(r.ok);
                    if (r.ok) {
                        ++st_.swaps;
                        CHECK(contains(g_.player(seat).hand, freed));
                    }
                    did = true;
                    break;
                }
            }
            if (!did) break;
        }
        // 3. işleme
        for (int guard = 0; guard < 60 && g_.player(seat).hand.size() > 1; ++guard) {
            bool did = false;
            V hand = g_.player(seat).hand;
            rng_.shuffle(hand);
            const int nt = (int)g_.table().size();
            for (int tile : hand) {
                if (ok.isJoker(tile) && !rng_.chance(0.3f)) continue;
                const int pend = g_.pendingLeftTile();
                if (pend >= 0 && tile != pend && hand.size() <= 2) continue; // keep a discardable tile
                const int start = nt ? rng_.range(nt) : 0;
                for (int k = 0; k < nt && !did; ++k) {
                    const int mi = (start + k) % nt;
                    const AddSide side = (AddSide)rng_.range(3);
                    Meld out;
                    if (!tryAddTile(g_.table()[mi], tile, ok, side, out)) continue;
                    const ActionResult r = act([&] { return g_.addToMeld(seat, tile, mi, side); });
                    CHECK(r.ok);
                    if (r.ok) ++st_.adds;
                    did = true;
                }
                if (did) break;
            }
            if (!did) break;
        }
    }

    void discardGreedy(int seat) {
        const V hand = g_.player(seat).hand;
        const OkeyInfo& ok = g_.okey();
        int tile = hand[rng_.range((int)hand.size())];
        if (hand.size() > 1 && !rng_.chance(0.05f)) {
            int best = -1, bestVal = -1;
            for (int t : hand) {
                if (ok.isJoker(t) || g_.isPlayableOnTable(t)) continue;
                const int v = ok.faceNumber(t) * 8 + rng_.range(8);
                if (v > bestVal) {
                    bestVal = v;
                    best = t;
                }
            }
            if (best >= 0) tile = best;
        }
        discardTile(seat, tile);
    }

    void turnGreedy(int seat) {
        if (g_.stage() == TurnStage::NeedDraw) {
            bool take = false;
            if (g_.canTakeFromLeft(seat)) {
                const int t = g_.topDiscard(Game::leftOf(seat));
                take = leftTileUseful(seat, t) || rng_.chance(0.04f);
            }
            if (take) {
                CHECK(act([&] { return g_.takeFromLeft(seat); }).ok);
                ++st_.takes;
            } else {
                draw(seat);
            }
        }
        if (rng_.chance(0.15f)) probe(seat);
        if (g_.handState() != HandState::Playing || g_.current() != seat) return;
        if (!g_.player(seat).opened) tryOpen(seat);
        if (g_.canWorkTable(seat)) workTable(seat);
        if (g_.pendingLeftTile() >= 0) {
            giveBack(seat);
            draw(seat);
            if (!g_.player(seat).opened) tryOpen(seat);
            if (g_.canWorkTable(seat)) workTable(seat);
        }
        if (rng_.chance(0.15f)) probe(seat);
        if (g_.handState() != HandState::Playing || g_.current() != seat) return;
        discardGreedy(seat);
    }
};

void runFuzz(const char* name, const RulesConfig& cfg, std::array<Policy, 4> pol, int matches, uint64_t seed0,
             Stats& total) {
    Stats st;
    const int failuresBefore = g_failures;
    for (int m = 0; m < matches; ++m) {
        Fuzzer f(cfg, pol, seed0 * 7919 + (uint64_t)m, st);
        f.runMatch(seed0 * 104729 + (uint64_t)m * 31 + 1);
        if (g_failures > failuresBefore + 20) break;
    }
    std::printf("  fuzz %-26s hands %5ld  finished %4ld  exhausted %4ld  opens %5ld (pairs %4ld)  lays %4ld  "
                "adds %5ld  swaps %4ld  takes %4ld  returns %4ld  okey/pairs/elden finish %ld/%ld/%ld  "
                "penalties %4ld  actions %7ld  rejected %6ld\n",
                name, st.hands, st.finishes, st.exhausted, st.opens, st.pairOpens, st.lays, st.adds, st.swaps,
                st.takes, st.returns, st.okeyFinish, st.pairFinish, st.eldenFinish, st.penalties, st.actions,
                st.rejected);
    total.hands += st.hands;
    total.matches += st.matches;
    total.finishes += st.finishes;
    total.exhausted += st.exhausted;
    total.opens += st.opens;
    total.pairOpens += st.pairOpens;
    total.lays += st.lays;
    total.adds += st.adds;
    total.swaps += st.swaps;
    total.takes += st.takes;
    total.returns += st.returns;
    total.okeyFinish += st.okeyFinish;
    total.pairFinish += st.pairFinish;
    total.eldenFinish += st.eldenFinish;
    total.penalties += st.penalties;
    total.actions += st.actions;
    total.rejected += st.rejected;
}

void testFuzz(int scale) {
    Stats total;
    const auto R_ = Policy::Random, G_ = Policy::Greedy;
    RulesConfig def;
    runFuzz("random/default", def, {R_, R_, R_, R_}, 100 * scale, 1, total);
    runFuzz("greedy/default", def, {G_, G_, G_, G_}, 250 * scale, 2, total);
    runFuzz("mixed/default", def, {G_, R_, G_, R_}, 100 * scale, 3, total);
    RulesConfig easy;
    easy.openThreshold = 45;
    easy.minPairsToOpen = 4;
    easy.waitTurnAfterOpening = false;
    runFuzz("greedy/easy-open,no-wait", easy, {G_, G_, G_, G_}, 250 * scale, 4, total);
    RulesConfig odd;
    odd.openThreshold = 60;
    odd.numHands = 3;
    odd.penalty = 50;
    odd.unopenedScore = 150;
    odd.winnerScore = -75;
    odd.penaltyPlayableDiscard = false;
    runFuzz("mixed/custom-scores", odd, {R_, G_, G_, G_}, 150 * scale, 5, total);
    RulesConfig kat;
    kat.katlamali = true;
    kat.openThreshold = 70; // (so that several players reach an opening and the bar really climbs)
    runFuzz("greedy/katlamali", kat, {G_, G_, G_, G_}, 150 * scale, 6, total);

    // the greedy fuzz must really reach the interesting paths
    CHECK(total.finishes > 100);
    CHECK(total.exhausted > 100);
    CHECK(total.opens > 500);
    CHECK(total.pairOpens > 20);
    CHECK(total.adds > 500);
    CHECK(total.swaps > 20);
    CHECK(total.takes > 100);
    CHECK(total.returns > 50);
    CHECK(total.okeyFinish > 0);
    CHECK(total.pairFinish > 0);
    // (elden bitiş needs the first opener to lay the whole hand at once: rare for the greedy fuzz, covered by
    // testEldenBitis)
    CHECK(total.penalties > 100);
    std::printf("  fuzz total: %ld matches, %ld hands, %ld actions (%ld rejected)\n", total.matches, total.hands,
                total.actions, total.rejected);
}

// Same seeds + same action sequence -> identical game.
void testDeterminism() {
    Stats st;
    const std::array<Policy, 4> pol = {Policy::Greedy, Policy::Random, Policy::Greedy, Policy::Greedy};
    Fuzzer a(RulesConfig(), pol, 99, st), b(RulesConfig(), pol, 99, st), c(RulesConfig(), pol, 100, st);
    a.runMatch(4242);
    b.runMatch(4242);
    c.runMatch(4243);
    CHECK(!a.log.empty());
    CHECK(a.log == b.log);
    CHECK(a.log != c.log);
}

} // namespace

int main(int argc, char** argv) {
    int scale = 1;
    if (argc > 1) scale = std::max(1, std::atoi(argv[1]));
    struct Case {
        const char* name;
        std::function<void()> fn;
    };
    const std::vector<Case> cases = {
        {"tile basics", testTileBasics},
        {"meld: runs", testRuns},
        {"meld: groups", testGroups},
        {"meld: pairs", testPairs},
        {"meld: makeMeld", testMakeMeld},
        {"meld: tryAddTile", testAddTile},
        {"meld: trySwapJoker", testSwapJoker},
        {"meld: fitsAnyMeld", testFits},
        {"meld: random properties", testMeldProperties},
        {"game: deal", testDeal},
        {"game: deal determinism", testDeterminismOfDeal},
        {"game: turn order", testTurnOrder},
        {"game: event texts", testTurnTexts},
        {"game: take/return left", testTakeReturnLeft},
        {"game: return-left texts", testReturnLeftTexts},
        {"game: opening threshold", testOpeningThreshold},
        {"game: opening errors", testOpeningErrors},
        {"game: pairs opening", testPairsOpening},
        {"game: pending left tile", testPendingLeft},
        {"game: waitTurnAfterOpening off", testWaitTurnOff},
        {"game: katlamalı", testKatlamali},
        {"game: layMelds", testLayMelds},
        {"game: işleme", testIsleme},
        {"game: joker swap", testSwapJokerGame},
        {"game: discard penalties", testDiscardPenalties},
        {"game: scoring", testScoring},
        {"game: elden bitiş", testEldenBitis},
        {"game: pile exhaustion", testPileExhaustion},
        {"game: match end", testMatchEnd},
        {"game: determinism", testDeterminism},
        {"fuzz", [scale] { testFuzz(scale); }},
    };
    for (const Case& c : cases) {
        const int f0 = g_failures, c0 = g_checks;
        c.fn();
        std::printf("%-34s %s (%d checks)\n", c.name, g_failures == f0 ? "ok" : "FAILED", g_checks - c0);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
