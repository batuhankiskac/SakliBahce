#pragma once
// The playing cards in 3D for the card games (Pişti, Batak, King; Bezik's double deck): thin rounded cards textured from
// ui::cardgfx's atlas, each with a target pose it flies to (an arc, eased, turning on the way), a tint, a lift and
// ray picking. The games decide where every card belongs each frame (CardLayout helpers below); this module only
// moves and draws them.
#include "r3d/Gfx.h"
#include "r3d/World.h"

#include <raymath.h>

#include <array>
#include <vector>

namespace r3d {

constexpr float CARD_W = 0.063f;   // metres (poker size)
constexpr float CARD_H = 0.088f;
constexpr float CARD_T = 0.0004f;  // thickness
// Card instances. Ids 0..51 are the 52 faces of ui::cardgfx (kart ids); a second deck follows (ids 52..103, id k shows
// face k % CARD_FACES), for the games played with two copies of each card (Bezik's double piquet deck). The one-deck
// games use only 0..51: the rest stay hidden.
// Konken: four jokers follow the two decks (ids 104..107, the atlas' joker face CARD_JOKER_KEY = ui::cardgfx::KEY_JOKER).
constexpr int CARD_FACES = 52;
constexpr int CARD_JOKERS = 4;
constexpr int CARD_JOKER_KEY = 54;
constexpr int CARD_COUNT = 2 * CARD_FACES + CARD_JOKERS;
inline int cardFace(int id) { return id >= 2 * CARD_FACES ? CARD_JOKER_KEY : id % CARD_FACES; }

struct CardPose {
    Vector3 pos{0, w3d::TABLE_Y, 0};
    Quaternion rot{0, 0, 0, 1};  // card local: x across, z along (the top edge at -z), y out of the face
};

// Orientation helpers. Yaw is about +Y in degrees (0: the card's top points away from the human, readable from seat
// 0). `faceUp` false turns it over. `tiltDeg` stands the card up about its own x axis (top edge rising).
Quaternion cardFlat(float yawDeg, bool faceUp);
Quaternion cardStanding(float yawDeg, float tiltDeg, bool faceUp);
Matrix cardMatrix(const CardPose& p);

class Cards3D {
public:
    bool init(Renderer& r);
    void shutdown(Renderer& r);

    void hideAll();
    // Sends the card to `p` (animated: an arc of `arc` metres, starting after `delay` seconds) or puts it there now.
    void place(int card, const CardPose& p, bool animate = true, float delay = 0.f, float arc = 0.05f);
    // Keeps the card on a pose that moves every frame (a fan in a bot's hand): a settled card sticks to it, a card in
    // flight steers to it, a jump of more than a few mm (the fan closing up) glides there.
    void follow(int card, const CardPose& p);
    void hide(int card);
    bool visible(int card) const;
    CardPose pose(int card) const;       // where it is drawn now
    CardPose target(int card) const;     // where it is going
    bool flying(int card) const;
    // Per-frame look: a colour multiplier (dim illegal cards), and a lift along the card's face normal (hover).
    void setTint(int card, Color c);
    void setLift(int card, float metres);
    void setRaise(int card, float metres); // slides the card up out of a held fan (along its top edge)
    void setTilt(int card, float deg);     // leans its top edge back toward the eye (hover in the player's fan)
    void setGlow(int card, float amount); // a soft warm halo under the card (legal / selected)
    void setHintGlow(int card, bool on);  // a green halo: the card the İpucu suggests

    void setSpeed(float s);              // animation speed (1 = normal)
    void update(float dt);
    void submit(Renderer& r);
    // The nearest visible card hit by `ray` among `among` (all visible cards when null); -1 if none.
    int pick(const Ray& ray, const std::vector<int>* among = nullptr) const;
    bool animating() const;              // a card in flight (follow()'s small glides don't count)

private:
    struct Card {
        bool visible = false;
        CardPose from, to, cur;
        float t = 1.f, dur = 0.f, delay = 0.f, arc = 0.f;
        Color tint = WHITE;
        float lift = 0.f, liftCur = 0.f;
        float raise = 0.f, raiseCur = 0.f, tilt = 0.f, tiltCur = 0.f;
        bool soft = false; // a follow() glide: not a flight anyone waits for
        float glow = 0.f, glowCur = 0.f;
        bool hint = false;
    };
    static CardPose drawn(const Card& c);
    std::array<Card, CARD_COUNT> cards_;
    std::array<Mesh, CARD_COUNT> meshes_{};
    std::array<Mat, CARD_COUNT> mats_{};
    Mesh halo_{};
    std::array<Mat, CARD_COUNT> haloMats_{};
    Mat hintMat_{};
    Texture2D haloTex_{};
    float speed_ = 1.f;
    bool ready_ = false;
};

// ---------------------------------------------------------------- layout helpers (world poses)
namespace cardlayout {
// Card `i` of `n` in seat `s`'s hand. The human holds a fan low in front of the eye (as if in both hands); the others
// hold theirs in their left hand (fanCard in Characters::cardFan's frame) — hand() is their stand-in without people
// (an upright fan where the hand would be).
CardPose hand(int seat, int i, int n);
// The frame of the player's fan (hand(0, ...)): origin at its pivot, x across, y the faces (toward the eye), -z up the
// cards. The player's left hand holds it there (PlayerHands).
Matrix humanFanFrame();
// Card `i` of `n` fanned in a frame from Characters::cardFan (x across, y the faces, -z up the cards).
CardPose fanCard(const Matrix& frame, int i, int n);
// The little face-down pile a seat's cards are dealt into on the felt (before they are picked up), `i` from the bottom.
CardPose dealtPile(int seat, int i);
// A trick pushed together into one bundle (face up) on its way to the taker, `i` from the bottom.
CardPose gathered(int seat, int i);
// Where seat `s` lays a card on the trick (in front of them, toward the middle), `k` = small stagger seed.
CardPose trick(int seat, int k);
// The won-cards pile of seat `s` (face down, beside their right hand), `i` = height index.
CardPose wonPile(int seat, int i);
// The face-down deck (dealer's side, `seat`), `i` from the bottom.
CardPose deck(int seat, int i);
// Pişti: the middle pile, `i` from the bottom (slightly scattered); a played `card` lands with its own small turn and
// spread (the same every frame).
CardPose middle(int i, bool faceUp, int card = -1);
} // namespace cardlayout

} // namespace r3d
