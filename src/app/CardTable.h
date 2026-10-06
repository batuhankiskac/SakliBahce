#pragma once
// What the card games (Pişti, Batak, King) share at the table: the 3D cards and their layout (hands, the trick
// in the middle, won piles, the deck), dealing, the trick that stays on the felt for a moment before it is swept to
// its taker, the player's hovering / clicking on their cards, bot pacing, the HUD kit and the people's reactions.
// Each game derives from CardTableBase and supplies its engine through the hooks below.
#include "app/TableGame.h"
#include "core/Rng.h"
#include "r3d/Cards3D.h"
#include "r3d/GameHud.h"

#include <array>
#include <string>
#include <vector>

namespace app {

class CardTableBase : public TableGame {
public:
    bool init(const TableContext& ctx) override;
    void shutdown() override;
    void update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput, bool aiSeat) override;
    void submit(r3d::Renderer& r) override;
    bool animating() const override;
    int activeSeat() const override;
    bool mouseBusy() const override { return hover_ >= 0 || hud_.mouseOverHud(); }
    std::string debugPhase() const override { return phase_; }
    void setLevel(int level) override;
    void setAnimationSpeed(float s) override;
    void setHints(bool on) override { hints_ = on; }
    bool consumeMenuRequest() override;
    bool consumeAiToggleRequest() override;
    void toast(const std::string& text, Color c, float seconds) override { hud_.toast(text, c, seconds); }
    std::string lastLogLine() const override { return log_; }
    bool debugHumanClick(const r3d::Renderer& r, Vector2& out) const override;
    // Maç tekrarı: no bots, no player input; replayStep waits for the table (0) and hands the line to replayLine.
    bool setReplayMode(bool on) override;
    int replayStep(const std::string& line) override;

protected:
    // ---- the game's hooks ----
    virtual void pumpEngine() = 0;                         // drain engine events: call the on*() helpers below
    virtual std::vector<int> handOf(int seat) const = 0;   // cards in seat's hand right now
    virtual int actor() const = 0;                         // seat whose decision it is (-1: nobody)
    virtual bool humanTurn() const = 0;                    // the player has to decide something now
    virtual std::vector<int> playableCards() const = 0;    // the player's cards that may be played now
    virtual void playHumanCard(int card) = 0;              // the player clicked one of their cards
    virtual std::vector<int> clickable() const { return handOf(0); } // cards the player may click (hover)
    virtual void botStep(int seat) = 0;                    // one bot decision for `seat` (applied to the engine)
    virtual float botDelay(int seat) const { (void)seat; return 0.9f; } // thinking time before botStep
    virtual void onHudClick(int id) { (void)id; }          // a game button / panel button
    virtual void layoutExtra() {}                          // cards outside hands / trick / piles (Pişti's middle)
    virtual bool faceUpHand(int seat) const { return seat == 0; } // eşli batak's open dummy
    virtual bool extraBusy() const { return false; }      // the game's own animation the bots must wait for
    // İpucu: what a Kurt bot of the game would do in the player's seat now (a card to play, or a panel choice), with
    // the Turkish line to show ("İpucu: Kupa Kızı oyna"). False: no hint here.
    struct Hint {
        int card = -1;     // a card of the player's hand (or the open dummy) to lift and glow
        int choice = -1;   // a panel decision (bid value, koz suit, contract): the game marks that button
        int suit = -1;     // King's koz contract: the suit
        std::string text;
    };
    virtual bool computeHint(Hint& out) { (void)out; return false; }
    // Changes whenever the player's decision changes (e.g. the engine's action log length): a hint lasts until then.
    virtual size_t decisionStamp() const { return 0; }
    // Restore (save / resume): the trick on the felt now, and every seat's won pile, from the engine.
    virtual std::vector<std::pair<int, int>> trickOnTable() const { return {}; } // (seat, card) in play order
    virtual std::array<std::vector<int>, 4> wonPiles() const { return {}; }
    // Maç tekrarı: one saved action line, applied to the engine with its events pumped (the usual animations):
    // 1 done, 0 not now (e.g. a "next hand" line while the sheet is up), -1 does not apply. Called only when the table
    // is idle.
    virtual int replayLine(const std::string& line) = 0;

    // ---- helpers for the games ----
    void resetTable();                                     // all cards hidden, piles and the trick cleared
    void dealFrom(int dealer);                             // every card of every hand flies out from the deck
    // Cards that just arrived in hands (a re-deal mid-hand) fly out from `from` instead of appearing there.
    void dealNew(const r3d::CardPose& from);
    void onPlayed(int seat, int card, bool bot);           // a card goes from seat's hand to the trick
    void onTrickWon(int winner, const std::vector<int>& cards); // the trick is swept to the winner after a moment
    void sweepNow();                                       // (hand end) the shown trick goes at once
    void collectTo(int seat, const std::vector<int>& cards, float delay); // cards fly to seat's won pile
    bool tableBusy() const;                                // deal / trick hold / flights: bots wait
    void say(int seat, const std::string& line, bool important = false);
    void sound(ui::Sfx s);
    void soundAt(ui::Sfx s, float delaySeconds);           // a sound when the card lands (game time)
    // layoutExtra(): put a card that is in none of the hands / trick / piles (keeps it visible this frame)
    void placeExtra(int card, const r3d::CardPose& p, bool animate = true, float delay = 0.f);
    std::string seatName(int s) const { return names_[(size_t)s]; }
    // Maç tekrarı: the status while the player's seat is to move ("Sıra sende" for the default name "Sen").
    std::string replaySelfStatus() const { return names_[0] == "Sen" ? std::string("Sıra sende") : names_[0] + " düşünüyor…"; }
    // After a resume: the table shows the engine's state at once (hands, the trick, won piles, extras via layoutExtra;
    // nothing in flight, no toasts), and the player's / bots' turns go on from there.
    void restoreView();
    // The button column: "İpucu" (the game's only own button), then "Yapay Zeka" and "Menü"; and the key-help strip
    // while the keyboard is in use (call it after the game's panel, before hud_.toasts()).
    void drawButtons(Vector2 mouse, bool aiSeat);
    bool hintAvailable() const;                           // the player decides now (not the AI, nothing moving)
    void requestHint();                                   // "İpucu" / H: ask the game's Kurt bot, show its answer
    const Hint* activeHint() const;                       // the hint still standing for this decision (nullptr: none)
    static constexpr int HINT_BUTTON = 0;                 // GameHud click id of "İpucu"

    TableContext ctx_;
    r3d::Cards3D cards_;
    r3d::GameHud hud_;
    std::array<std::string, 4> names_{};
    int level_ = 1;
    float speed_ = 1.f;
    bool hints_ = true;
    bool aiSeat_ = false;
    float now_ = 0.f;
    float botWait_ = 0.f;
    int botSeat_ = -1;
    okey::Rng rng_{7};
    std::string log_;
    // the trick on the felt
    struct ShownCard {
        int seat = -1, card = -1;
    };
    std::vector<ShownCard> shown_;
    int sweepTo_ = -1;
    float sweepAt_ = -1.f;
    std::array<std::vector<int>, 4> won_{};               // cards in each won pile (bottom first)
    float dealUntil_ = 0.f;
    std::array<float, r3d::CARD_COUNT> dealDelay_{};      // a dealt card's start delay (used once)
    std::array<bool, r3d::CARD_COUNT> extraPlaced_{};     // layoutExtra() placed it this frame
    int hover_ = -1;
    bool menuReq_ = false, aiReq_ = false;
    std::vector<std::pair<float, ui::Sfx>> delayedSfx_;  // (game time, sound)
    Camera3D cam_{};
    bool built_ = false;
    bool humanInput_ = false;
    bool replay_ = false;                                  // maç tekrarı: nobody moves but the saved lines
    Hint hint_;
    size_t hintStamp_ = 0;
    bool hintOn_ = false;
    int kbCard_ = -1;                                      // the keyboard's card (among the playable ones)
    std::string phase_;                                    // debugPhase()

private:
    void layoutAll();
    std::vector<int> playableInOrder() const;             // playable cards left to right as the hand shows them
    void keyboardCards(bool hudKeys);
};

} // namespace app
