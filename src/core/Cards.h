#pragma once
// Playing cards (iskambil) shared by the card games (Pişti, Batak, King). Pure logic, header-only.
//
// 52 cards, ids 0..51: suit = id / 13, rank = id % 13 + 2 (2..14; 11 Vale/Bacak, 12 Kız, 13 Papaz, 14 As).
// Suits in Turkish: Maça (spades), Kupa (hearts), Karo (diamonds), Sinek (clubs).
// Seats as in the okey games: 0 = bottom (human), 1 = right, 2 = across, 3 = left; play goes 0 -> 1 -> 2 -> 3
// (counter-clockwise, "sağdan"). Partners (eşli games) sit across: 0 & 2, 1 & 3.
#include "core/Rng.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace kart {

using okey::Rng;

constexpr int NUM_CARDS = 52;
constexpr int NUM_SUITS = 4;

enum Suit : int { Maca = 0, Kupa = 1, Karo = 2, Sinek = 3 };
enum Rank : int { Vale = 11, Kiz = 12, Papaz = 13, As = 14 };

inline bool isValidCard(int id) { return id >= 0 && id < NUM_CARDS; }
inline int suitOf(int id) { return id / 13; }
inline int rankOf(int id) { return id % 13 + 2; }
inline int makeCard(int suit, int rank) { return suit * 13 + (rank - 2); }

inline int partnerOf(int seat) { return (seat + 2) % 4; }
inline int nextSeat(int seat) { return (seat + 1) % 4; }

inline const char* suitNameTR(int suit) {
    switch (suit) {
    case Maca: return "Maça";
    case Kupa: return "Kupa";
    case Karo: return "Karo";
    case Sinek: return "Sinek";
    default: return "?";
    }
}

// The suit in the middle of a sentence: "maça", "kupa", "karo", "sinek".
inline const char* suitLowerTR(int suit) {
    switch (suit) {
    case Maca: return "maça";
    case Kupa: return "kupa";
    case Karo: return "karo";
    case Sinek: return "sinek";
    default: return "?";
    }
}

inline std::string rankNameTR(int rank) {
    switch (rank) {
    case Vale: return "Vale";
    case Kiz: return "Kız";
    case Papaz: return "Papaz";
    case As: return "As";
    default: return std::to_string(rank);
    }
}

// Short corner index printed on the card: "2".."10", "V", "K", "P", "A" (Turkish decks: Vale, Kız, Papaz, As).
inline std::string rankIndexTR(int rank) {
    switch (rank) {
    case Vale: return "V";
    case Kiz: return "K";
    case Papaz: return "P";
    case As: return "A";
    default: return std::to_string(rank);
    }
}

// The plain spoken form: "Kupa Kız", "Sinek 2", "Maça As".
inline std::string cardNameTR(int id) {
    if (!isValidCard(id)) return "?";
    return std::string(suitNameTR(suitOf(id))) + " " + rankNameTR(rankOf(id));
}

// The card as a definite object (belirtme hali): "Maça Kızı", "Sinek Valeyi", "Kupa 7'yi", "Karo Ası".
inline std::string cardAccusativeTR(int id) {
    if (!isValidCard(id)) return "?";
    const std::string suit = suitNameTR(suitOf(id));
    switch (rankOf(id)) {
    case Vale: return suit + " Valeyi";
    case Kiz: return suit + " Kızı";
    case Papaz: return suit + " Papazı";
    case As: return suit + " Ası";
    default: break;
    }
    static const char* const suf[] = {"", "", "'yi", "'ü", "'ü", "'i", "'yı", "'yi", "'i", "'u", "'u"};
    const int r = rankOf(id);
    return suit + " " + std::to_string(r) + suf[r];
}

// Card sets as bit masks (bit i = card id i; the 52-card games).
inline uint64_t maskOf(const std::vector<int>& cards) {
    uint64_t m = 0;
    for (int c : cards)
        if (isValidCard(c)) m |= 1ull << c;
    return m;
}
// The cards of a mask, ascending ids (= by suit, then rank).
inline std::vector<int> cardsOf(uint64_t m) {
    std::vector<int> v;
    v.reserve((size_t)__builtin_popcountll(m));
    while (m) {
        v.push_back(__builtin_ctzll(m));
        m &= m - 1;
    }
    return v;
}

// The engines' event queues (all the table games): a safety valve for headless loops that never drain them — past
// the cap the older half goes.
constexpr size_t MAX_QUEUED_EVENTS = 20000;
template <class E>
inline void pushEvent(std::vector<E>& q, E&& e) {
    if (q.size() >= MAX_QUEUED_EVENTS) q.erase(q.begin(), q.begin() + (std::ptrdiff_t)(MAX_QUEUED_EVENTS / 2));
    q.push_back(std::move(e));
}

// A fresh shuffled deck.
inline std::vector<int> shuffledDeck(Rng& rng) {
    std::vector<int> d(NUM_CARDS);
    for (int i = 0; i < NUM_CARDS; ++i) d[i] = i;
    rng.shuffle(d);
    return d;
}

// The saved action lines of the games (one shared parser): space-separated integers (strtol), then nothing but spaces
// and a line end. At most `maxCount` of them into `out`; false on anything else.
inline bool parseIntList(const std::string& line, std::vector<int>& out, size_t maxCount) {
    out.clear();
    const char* p = line.c_str();
    while (true) {
        while (*p == ' ') ++p;
        const char* q = p;
        while (*q == ' ' || *q == '\r' || *q == '\n') ++q;
        if (*q == 0) return true;
        char* end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p || out.size() >= maxCount) return false;
        out.push_back((int)v);
        p = end;
    }
}

// Exactly `n` space-separated integers from `line` (the card games' saved action lines); false on anything else.
inline bool parseInts(const std::string& line, int* out, int n) {
    std::vector<int> v;
    if (n < 0 || !parseIntList(line, v, (size_t)n) || v.size() != (size_t)n) return false;
    for (int i = 0; i < n; ++i) out[i] = v[(size_t)i];
    return true;
}

} // namespace kart
