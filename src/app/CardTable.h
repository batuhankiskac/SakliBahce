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

// ---------------------------------------------------------------- the table frame (2026-10)
// Where a card game is laid out: our okey table, or the two-seat tavla table for two-player card games.
//
// Every card layout (r3d::cardlayout::*, w3d::seatLocal / SEAT_DIR / SEAT_RIGHT) is written for the okey table:
// origin at its centre, seat 0 (the player) at +Z, 1 on the right, 2 across, 3 on the left. Call the coordinates of
// that layout "layout space". CardTableFrame maps layout space onto the table the game is really played at
// (TableGame::location()):
//  - location 0 (our okey table): identity, nothing changes.
//  - location 1 (the two-seat tavla table, w3d::tavlaFrame; seats 0 and 2 only, the opponent sits across as seat 2):
//    the layout is turned into the tavla table's frame, and its depth (local z, toward the players) is squeezed by
//    DEPTH so that what lies on our felt fits the smaller top (the deck, the dealt piles, the won piles, the trick);
//    across (local x) stays as it is (the tavla table is about as wide as our felt). The player's own fan is held
//    relative to the eye, not to the felt: eyePose() moves it with the chair (EYE_SHIFT) and does not squeeze it.
//    The opponent's fan needs nothing: it is in their hand (Characters::cardFan is in world space already).
//
// CardTableBase applies the frame to everything it lays out itself (the hands, the deal and the cut, the trick, the
// gathered trick, the won piles, the deck, the dragged card, the people's reaches and gathers): a game that only uses
// those needs nothing but `int location() const override { return 1; }` (and seats 0 and 2). A game that places cards
// of its own (layoutExtra / dealtExtras / placeExtra, like Pişti's middle pile), or points (HUD labels, reactions,
// Characters calls), passes its layout-space poses and points through CardTableBase::layPose() / layPoint().
// Poses read back from r3d::Cards3D (pose(), target()) and Characters (cardFan, headPosition) are world already.
struct CardTableFrame {
    bool tavla = false;                                    // location 1: the two-seat tavla table
    static constexpr float DEPTH = w3d::TAVLA_HALF_D / w3d::FELT_HALF;    // the felt's depth onto the tavla top
    static constexpr float EYE_SHIFT = w3d::SEAT_DIST - w3d::TAVLA_SEAT_DIST; // our chair is this much closer there

    // A layout-space point on / over the table -> world.
    Vector3 point(Vector3 l) const {
        return tavla ? w3d::tavlaToWorld({l.x, l.y, l.z * DEPTH}) : l;
    }
    // World -> layout space (the inverse of point()).
    Vector3 toLayout(Vector3 w) const {
        if (!tavla) return w;
        Vector3 l = w3d::tavlaToLocal(w);
        l.z /= DEPTH;
        return l;
    }
    // A layout-space card pose -> world (position as point(), the orientation turned with the table).
    r3d::CardPose pose(const r3d::CardPose& p) const {
        if (!tavla) return p;
        r3d::CardPose o;
        o.pos = point(p.pos);
        o.rot = QuaternionMultiply(yaw(), p.rot);
        return o;
    }
    // The player's own fan (cardlayout::hand(0, ...), placed relative to the eye at our table) -> world: it keeps its
    // place in the view, i.e. it turns about the eye with the tavla seat's steeper resting look (EYE_PITCH_DELTA) and
    // moves with the chair.
    static constexpr Vector3 OKEY_EYE{0.f, 1.21f, 0.85f};  // PlayerCamera's resting eye at our table (DEF_EYE) ...
    static constexpr float EYE_PITCH_DELTA = -36.f - -24.5f; // ... and its pitch there vs at the tavla seat (degrees)
    r3d::CardPose eyePose(const r3d::CardPose& p) const {
        if (!tavla) return p;
        const Quaternion pitch = QuaternionFromAxisAngle({1.f, 0.f, 0.f}, EYE_PITCH_DELTA * DEG2RAD);
        Vector3 l = Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(p.pos, OKEY_EYE), pitch), OKEY_EYE);
        l.z -= EYE_SHIFT;
        r3d::CardPose o;
        o.pos = w3d::tavlaToWorld(l);
        o.rot = QuaternionMultiply(yaw(), QuaternionMultiply(pitch, p.rot));
        return o;
    }
    static Quaternion yaw() { return QuaternionFromAxisAngle({0.f, 1.f, 0.f}, w3d::TAVLA_YAW_DEG * DEG2RAD); }
};

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
    // Konken: the player's fan (seat 0, once the deal has brought it to the hand) is laid out by the game itself in
    // layoutExtra() — its own order, selection, dragging; the base still deals it and keeps it held. Default: the base.
    virtual bool gameLaysOwnHand() const { return false; }
    // Dealing: cards outside the hands that come off the deck too (Pişti's table cards), dealt after the hands, and
    // how many cards stay in the deck (they lie under the ones being dealt). The extras' poses are world (layPose()).
    virtual std::vector<std::pair<int, r3d::CardPose>> dealtExtras() const { return {}; }
    virtual int deckRemaining() const { return 0; }
    virtual bool showCutCard() const { return false; }    // Pişti: the deck's bottom card is shown at the cut
    virtual int cutCard() const { return -1; }
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
    // A new hand: the dealer shuffles (a riffle and a cut), deals the cards one by one round the table, sliding
    // across the felt into a little pile in front of each seat (dealtExtras last), then everyone picks theirs up.
    void dealFrom(int dealer);
    // Cards that just arrived in hands (a re-deal mid-hand): dealt the same way from the deck, without the shuffle.
    void dealNew(int dealer);
    // The deck's slot `i` (from the bottom) by the dealer: riffling while the shuffle runs (world: in the table frame).
    r3d::CardPose deckSlot(int dealer, int i) const;
    bool dealing() const { return deal_.on; }
    void onPlayed(int seat, int card, bool bot);           // a card goes from seat's hand to the trick
    // A bot's card leaving its fan for `p` (pulled out by the hand, laid or tossed), with its sound on landing.
    void playFromHand(int seat, int card, const r3d::CardPose& p, bool toss);
    void onTrickWon(int winner, const std::vector<int>& cards); // the trick is swept to the winner after a moment
    void sweepNow();                                       // (hand end) the shown trick goes at once
    void collectTo(int seat, const std::vector<int>& cards, float delay); // cards fly to seat's won pile
    // Pişti's capture: the taker's hand comes down on the middle and pushes the cards to its pile.
    void sweepTo(int seat, const std::vector<int>& cards, Vector3 from);
    // The player's own hand (r3d::PlayerHands via ctx_.handCue): the player's card on its way (or dragged: lead < 0).
    void handCueCard(int card, float lead);
    int handDragged_ = -1;  // the card the mouse let go of last (it is in the hand already)
    bool tableBusy() const;                                // deal / trick hold / flights: bots wait
    void say(int seat, const std::string& line, bool important = false);
    void sound(ui::Sfx s);
    void soundAt(ui::Sfx s, float delaySeconds);           // a sound when the card lands (game time)
    // layoutExtra(): put a card that is in none of the hands / trick / piles (keeps it visible this frame)
    void placeExtra(int card, const r3d::CardPose& p, bool animate = true, float delay = 0.f);
    // The table frame (see CardTableFrame above): layout space (the okey table's cardlayout / w3d seat frames) -> the
    // world at the table this game is played at (location()). placeExtra(), dealtExtras(), Characters calls and HUD
    // labels take world poses / points: pass a game's own layout through these.
    CardTableFrame tableFrame() const { return CardTableFrame{location() == 1}; }
    r3d::CardPose layPose(const r3d::CardPose& p) const { return tableFrame().pose(p); }
    Vector3 layPoint(Vector3 p) const { return tableFrame().point(p); }
    std::string seatName(int s) const { return names_[(size_t)s]; }
    // Rakip (2026-10): at the two-seat table the opponent plays layout seat 2 (across), but the regular sitting there
    // is the one chosen in Ayarlar (ui::twoPlayerOpponent): oppSeat_ (1..3). chr() turns a layout seat into the
    // Characters seat (his hands, his voice, his bubbles); the base applies it to every Characters call it makes, the
    // games to their own (react, headPosition). names_[2] holds his name. watcherSeat(k): the k-th (0, 1) of the two
    // regulars who are not playing (they stay at the okey table and talk from there).
    int oppSeat_ = 2;
    int chr(int seat) const { return seat == 2 ? oppSeat_ : seat; }
    int watcherSeat(int k) const {
        int n = 0;
        for (int s = 1; s <= 3; ++s)
            if (s != oppSeat_ && n++ == k) return s;
        return 1;
    }
    // A card that has just left the stock for seat's hand (call from layoutExtra the frame it leaves the stock): it
    // flies there from where it lies instead of popping into the hand; a regular's right hand takes it off the stock and
    // brings it up into his fan (Characters::drawCard). `stagger`: a second card drawn at once waits this much longer.
    void drawFromStock(int seat, int card, float stagger = 0.f);
    // Call in startMatch of a two-player game played at the tavla table (after names_ = names).
    void seatOpponent(int seat) {
        oppSeat_ = std::clamp(seat, 1, 3);
        names_[2] = names_[(size_t)oppSeat_];
    }
    // Maç tekrarı: the status while the player's seat is to move ("Sıra sende" for the default name "Sen").
    std::string replaySelfStatus() const { return names_[0] == "Sen" ? std::string("Sıra sende") : names_[0] + " düşünüyor…"; }
    // The status line's ladder every card table shares, around the game's own words: `ended` ("El bitti", the match's
    // end; null while neither), `dealText` while `dealingNow`, the replay's or the Yapay Zeka's turn in our seat, a
    // regular's turn (his name + `thinking`). The player's own decision is the game's: `mine()`, shown highlighted
    // (`sc`). Empty when nobody acts.
    template <class Mine>
    std::string statusLine(const char* ended, bool dealingNow, const char* dealText, int a, bool aiSeat, Color& sc,
                           Mine mine, const char* thinking = " düşünüyor…") const {
        if (ended) return ended;
        if (dealingNow) return dealText;
        if (a == 0 && replay_) return replaySelfStatus();
        if (a == 0 && aiSeat) return "Yapay zeka düşünüyor…";
        if (a == 0) {
            sc = ui::pal::Highlight;
            return mine();
        }
        if (a > 0) return names_[(size_t)a] + thinking;
        return {};
    }
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
    // Altmışaltı (2026-10): more buttons of the game's own under "İpucu" (Kapat, Kozu Al); their clicks come to
    // onHudClick with id 1, 2, ... in this order. Default none: the column is "İpucu" alone as before.
    struct GameButton {
        std::string label;
        bool enabled = true;
        bool glow = false;
    };
    virtual std::vector<GameButton> gameButtons() const { return {}; }

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
    float collectAt_ = -1.f;                               // the gathered trick goes on to the pile then
    std::array<std::vector<int>, 4> won_{};               // cards in each won pile (bottom first)
    float dealUntil_ = 0.f;
    std::array<float, r3d::CARD_COUNT> dealDelay_{};      // a card's start delay (used once)
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
    // The deal in progress (times are game time, now_).
    struct DealAnim {
        bool on = false;
        bool shuffle = false;
        int dealer = 0;
        int total = 0;                                     // cards dealt
        int base = 0;                                      // deck slots under them (cards that stay in the deck)
        float start = 0.f, shuffleEnd = 0.f, dealStart = 0.f, interval = 0.05f;
        std::array<float, r3d::CARD_COUNT> leave{};        // when the card leaves the deck (< 0: not dealt now)
        std::array<int, r3d::CARD_COUNT> order{};          // 0 = dealt first
        std::array<int, r3d::CARD_COUNT> pile{};           // index in its seat's dealt pile
        std::array<int, r3d::CARD_COUNT> seat{};           // -1: an extra (dealtExtras)
        std::array<r3d::CardPose, r3d::CARD_COUNT> extra{};
        std::array<float, 4> pickAt{};                     // when the seat picks its pile up
        std::array<float, 4> grabAt{};                     // ... and the cards leave the felt (the fingers are there)
        std::array<bool, 4> picked{};
        bool handsStarted = false, shuffled = false;
        int slid = 0;                                      // cards whose slide was heard
        int cut = -1;                                      // Pişti: the card shown at the cut
        float cutFrom = 0.f, cutTo = 0.f;
        float end = 0.f;
    };
    void startDeal(int dealer, bool shuffle);
    void updateDeal();
    bool dealPose(int card, r3d::CardPose& p) const;       // where a card of the running deal is now (false: in hand)
    void updateHolding();
    void layoutAll();
    std::vector<int> playableInOrder() const;             // playable cards left to right as the hand shows them
    void keyboardCards(bool hudKeys);
    void mouseCards(Vector2 mouse, bool canClick);
    DealAnim deal_;
    std::array<bool, 4> holding_{};                        // the seat holds its fan (Characters::holdCards)
    int press_ = -1;                                       // the player's card under the mouse button
    Vector2 pressAt_{};
    bool dragging_ = false;
};

} // namespace app
