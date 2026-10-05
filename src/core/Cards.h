#pragma once
// Playing cards (iskambil) shared by the card games (Pişti, Batak, King). Pure logic, header-only.
//
// 52 cards, ids 0..51: suit = id / 13, rank = id % 13 + 2 (2..14; 11 Vale/Bacak, 12 Kız, 13 Papaz, 14 As).
// Suits in Turkish: Maça (spades), Kupa (hearts), Karo (diamonds), Sinek (clubs).
// Seats as in the okey games: 0 = bottom (human), 1 = right, 2 = across, 3 = left; play goes 0 -> 1 -> 2 -> 3
// (counter-clockwise, "sağdan"). Partners (eşli games) sit across: 0 & 2, 1 & 3.
#include "core/Rng.h"
#include <string>
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
inline bool isRed(int id) { return suitOf(id) == Kupa || suitOf(id) == Karo; }

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

// A fresh shuffled deck.
inline std::vector<int> shuffledDeck(Rng& rng) {
    std::vector<int> d(NUM_CARDS);
    for (int i = 0; i < NUM_CARDS; ++i) d[i] = i;
    rng.shuffle(d);
    return d;
}

} // namespace kart
