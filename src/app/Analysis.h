#pragma once
// Hata analizi: after a match the game tells the player (seat 0) his most expensive mistakes, e.g.
//   "3. el, 14. tur: Kırmızı 12'yi attın, Hacı Rıza onu alıp açtı. Kurt Sarı 3'ü atardı (yaklaşık 38 puan kayıp)".
// The match is replayed from its seed and action lines (the same ones App saves for "Devam Et"); at each of the
// player's decisions a Kurt judges the move actually made against its best alternative, from what the player knew
// then (public information + his own hand), in the game's own units:
//   101 / eşli 101  expected hand score (Kurt's rollouts of the discard; take-left vs the pile; opening now; finishing)
//   klasik okey     the chance to finish the hand with the draws left ("bitme şansı", percentage points)
//   tavla           equity after the play (2-ply), in points: equity x the cube; take / drop of a double
//   pişti           expected point difference for the rest of the hand (Monte Carlo)
//   batak           expected score difference of the hand: cards, ihale (bid / pas), koz
//   king            expected points of the hand: cards, the contract chosen
// Forced moves are skipped, near-ties and differences inside the sampling noise are not reported, one decision gives at
// most one mistake, and a mistake repeated over a hand (not opening turn after turn) is told once, at its costliest. Pure logic (no raylib, no rendering): run it on a worker thread at the end of a match —
// a whole match takes a few seconds at most (Kurt's work per decision is capped) and is deterministic.
#include "core/Batak.h"
#include "core/Game.h"
#include "core/King.h"
#include "core/Pisti.h"
#include "core/Tavla.h"
#include "core/AltmisaltiBot.h" // Altmışaltı
#include "core/Bezik.h" // Bezik
#include "core/Dama.h" // Dama
#include "core/Konken.h" // Konken

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {
enum class GameKind;
struct Settings;
} // namespace ui

namespace analysis {

struct Mistake {
    int hand = 0;          // 1-based hand (el) / game (tavla: oyun)
    int turn = 0;          // 1-based: okey round (tur), the player's move in the game (tavla), trick (cards)
    std::string when;      // "3. el, 14. tur", "2. oyun, 7. hamle", "4. el, ihale"
    std::string played;    // "Kırmızı 12'yi attın"
    std::string better;    // "Kurt Sarı 3'ü atardı"
    std::string why;       // what happened next / why it cost ("Hacı Rıza onu alıp açtı"), may be empty
    double cost = 0.0;     // loss against Kurt's choice, >= 0, in `unit`
    std::string unit;      // "puan", or "%" (klasik okey: the finishing chance, percentage points)
    // what was decided: "atış" (a tile), "açış" (not opening when Kurt would), "bitiş" (not finishing), "çekiş"
    // (pile vs the left tile), "hamle" (tavla), "katlama", "kart", "ihale", "koz", "oyun" (king's contract)
    std::string topic;
    // ---- for tests / tuning
    double noise = 0.0;    // standard error of the cost (sampling), 0 when exact
    bool notable = false;  // worth telling: above the game's threshold and clear of the noise
};

// The seat names used in the texts when none are given (seat 0 = the player).
const std::array<std::string, 4>& defaultNames();

// The match from its start: `kind` and `rules` (the settings it was started with: App's matchSettings_), its seed and
// its action lines (okey: okey::LoggedAction::encode() of game_.actionLog(); the other games: TableGame::saveState()).
// Returns the `maxResults` most expensive notable mistakes, most expensive first (empty: none worth telling, or the
// lines do not replay). `cancel` (optional) stops the work early (returns what was found so far).
std::vector<Mistake> analyzeMatch(ui::GameKind kind, const ui::Settings& rules, uint64_t seed,
                                  const std::vector<std::string>& actionLines,
                                  const std::array<std::string, 4>& names = defaultNames(), int maxResults = 3,
                                  const std::atomic<bool>* cancel = nullptr);

// One line for the screen: "3. el, 14. tur: Kırmızı 12'yi attın, Hacı Rıza onu alıp açtı. Kurt Sarı 3'ü atardı
// (yaklaşık 38 puan kayıp)".
std::string describeMistake(const Mistake& m);

// ---- single decisions (what analyzeMatch does at each decision; tests build positions with the engines' debug hooks).
// Each returns false when there is nothing to judge (a forced move, wrong stage); otherwise `out` holds the judgement
// (cost 0 when the move was Kurt's choice) without `when` and with a `why` from the values only.
// 101 (also eşli): `seat` discards `tile` now (Play stage, before the discard).
bool judgeOkeyDiscard(const okey::Game& g, int seat, int tile, Mistake& out,
                      const std::array<std::string, 4>& names = defaultNames());
// Klasik okey: `seat` discards `tile` now (instead of finishing, when it could).
bool judgeClassicDiscard(const okey::Game& g, int seat, int tile, Mistake& out);
// Tavla: `player` (0 / 1) rolled d1-d2 in `before` and played to `after`; `cube` = the stake; `variant` the çeşit
// (Gülbahar: d1 = d2 is one rung of the ladder).
bool judgeTavlaPlay(const tavla::Position& before, int player, int d1, int d2, const tavla::Position& after, int cube,
                    Mistake& out, tavla::Variant variant = tavla::Variant::Klasik);
// The card games: `seat` (== g.current()) plays `card` now.
bool judgePistiCard(const pisti::Game& g, int seat, int card, Mistake& out);
bool judgeBatakCard(const batak::Game& g, int seat, int card, Mistake& out);
bool judgeKingCard(const king::Game& g, int seat, int card, Mistake& out);
// Altmışaltı: `player` (== g.current()) plays a card or closes the stock now (an exchange is never judged); expected
// game points of the hand (won +, lost -).
bool judgeAltmisalti(const altmisalti::Game& g, int player, const altmisalti::BotAction& act, Mistake& out);
// Bezik: `player` (== g.current()) plays `card` now; the player's points minus the opponent's for the rest of the deal
// (Monte Carlo in the first stage, exact in the second).
bool judgeBezikCard(const bezik::Game& g, int player, int card, Mistake& out);
// Dama (AnalysisDama.cpp): `player` makes `played` on `before`; its loss against Kurt's best move in pieces ("taş":
// a man 1, a dama about 3), each move scored by a fixed-depth search. analyzeDama: the whole match (analyzeMatch).
bool judgeDamaMove(const dama::Board& before, int player, const dama::Move& played, Mistake& out);
std::vector<Mistake> analyzeDama(const ui::Settings& rules, uint64_t seed, const std::vector<std::string>& lines,
                                 const std::array<std::string, 4>& names, const std::atomic<bool>* cancel);
// Konken (AnalysisKonken.cpp): `seat` (== g.current(), Play stage, nothing taken pending) discards `card` now; expected
// points of the hand (lower is better) by Kurt's rollouts against Kurt's best discard. analyzeKonken: the whole match
// (discards, missed openings, a stock draw while the discard on top opened the hand).
bool judgeKonkenDiscard(const konken::Game& g, int seat, int card, Mistake& out);
std::vector<Mistake> analyzeKonken(const ui::Settings& rules, uint64_t seed, const std::vector<std::string>& lines,
                                   const std::array<std::string, 4>& names, const std::atomic<bool>* cancel);

} // namespace analysis
