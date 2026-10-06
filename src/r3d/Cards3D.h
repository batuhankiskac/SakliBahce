#pragma once
// The 52 playing cards in 3D for the card games (Pişti, Batak, King): thin rounded cards textured from
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
constexpr int CARD_COUNT = 52;

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
    void hide(int card);
    bool visible(int card) const;
    CardPose pose(int card) const;       // where it is drawn now
    CardPose target(int card) const;     // where it is going
    bool flying(int card) const;
    // Per-frame look: a colour multiplier (dim illegal cards), and a lift along the card's face normal (hover).
    void setTint(int card, Color c);
    void setLift(int card, float metres);
    void setGlow(int card, float amount); // a soft warm halo under the card (legal / selected)
    void setHintGlow(int card, bool on);  // a green halo: the card the İpucu suggests

    void setSpeed(float s);              // animation speed (1 = normal)
    void update(float dt);
    void submit(Renderer& r);
    // The nearest visible card hit by `ray` among `among` (all visible cards when null); -1 if none.
    int pick(const Ray& ray, const std::vector<int>* among = nullptr) const;
    bool animating() const;

private:
    struct Card {
        bool visible = false;
        CardPose from, to, cur;
        float t = 1.f, dur = 0.f, delay = 0.f, arc = 0.f;
        Color tint = WHITE;
        float lift = 0.f, liftCur = 0.f;
        float glow = 0.f, glowCur = 0.f;
        bool hint = false;
    };
    std::array<Card, CARD_COUNT> cards_;
    std::array<Mesh, CARD_COUNT> meshes_{};
    std::array<Mat, CARD_COUNT> mats_{};
    Mesh halo_{};
    Mat haloMat_{};
    Mat hintMat_{};
    Texture2D haloTex_{};
    float speed_ = 1.f;
    bool ready_ = false;
};

// ---------------------------------------------------------------- layout helpers (world poses)
namespace cardlayout {
// Card `i` of `n` in seat `s`'s hand. The human's fan stands near the table edge, tilted toward the eye; the others
// hold theirs upright in front of them (we see the backs).
CardPose hand(int seat, int i, int n);
// Where seat `s` lays a card on the trick (in front of them, toward the middle), `k` = small stagger seed.
CardPose trick(int seat, int k);
// The won-cards pile of seat `s` (face down, beside their right hand), `i` = height index.
CardPose wonPile(int seat, int i);
// The face-down deck (dealer's side, `seat`), `i` from the bottom.
CardPose deck(int seat, int i);
// Pişti: the middle pile, `i` from the bottom (slightly scattered).
CardPose middle(int i, bool faceUp);
} // namespace cardlayout

} // namespace r3d
