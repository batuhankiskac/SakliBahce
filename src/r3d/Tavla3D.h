#pragma once
// A tavla board in 3D on our table, between the player (seat 0, bottom) and Kel Mahmut (seat 2, across): a walnut
// box with inlaid points (haneler), the middle bar, 30 checkers (pul) that slide / hop between points, two dice that
// are thrown and tumble to their numbers, the katlama zarı (doubling cube: 2..64, a bigger die), highlights for the
// playable points and ray picking of points.
// Board indices follow tavla::Game: 0..23 as seen by player 0 (the bottom row runs 0..11 from right to left, the top
// row 12..23 from left to right), BAR = 24, OFF = 25.
#include "r3d/Gfx.h"

#include <raymath.h>

#include <array>
#include <vector>

namespace r3d {

class Tavla3D {
public:
    static constexpr int BAR = 24, OFF = 25;
    static constexpr int CHECKERS = 30; // 0..14 player 0 (light), 15..29 player 1 (dark)

    bool init(Renderer& r);
    void shutdown(Renderer& r);

    // The checkers as the engine has them: signed counts per point (+ player 0, - player 1), bar and off counts.
    // Checkers move to their new places (animated unless `snap`).
    void setPosition(const std::array<int8_t, 24>& pts, const std::array<int8_t, 2>& bar, const std::array<int8_t, 2>& off,
                     bool snap);
    // One checker of `player` goes from -> to (as a step: a lifted hop); call before setPosition of the new position
    // so exactly that checker makes the move. `delay` holds it back (a bot's hand reaching for it first).
    // Returns the checker's index (-1: none there).
    int moveChecker(int player, int from, int to, float delay);
    // The player's own hand (PlayerHands): the top of checker `i` while it is on its way (world; false once it has
    // landed), and where `player`'s dice of the last throw leave the hand (world; false when none are in the air).
    bool checkerMoving(int i, Vector3& world) const;
    bool diceThrowPoint(Vector3& world, int player = 0) const;

    // Dice: thrown by `player` (0 from the bottom, 1 from the top), landing showing d1 / d2 (d2 = 0: one die).
    // `opening`: the opening roll, one die each (d1 player 0's, d2 player 1's).
    void throwDice(int player, int d1, int d2, bool opening, float delay);
    void placeDice(int player, int d1, int d2); // the dice already lying there (a resumed match)
    void setDiceUsed(const std::array<bool, 4>& used, int n, bool isDouble); // dims used dice
    void hideDice();

    // Katlama zarı. `show`: in play at all. `value` faces the player (1 = the centred cube, showing 64). On the felt
    // left of the box: in the middle while `owner` is -1, else on that player's side (0 near, 1 far). `offered` > 0:
    // held up over the bar, turned to the offered value, while the other player decides. Changes hop and turn it there.
    void setCube(bool show, int value, int owner, int offered);

    // Highlights: points the selected checker can go to, the points with playable checkers, the selected one.
    void setHighlights(const std::vector<int>& targets, const std::vector<int>& sources, int selected);
    // The keyboard's chosen target (one of the highlighted ones) is lit brighter; -1 none. In the colour-blind mode
    // (ui::colorBlind) the targets are sky blue with diagonal stripes instead of plain green.
    void setKeyFocus(int point) { keyFocus_ = point; }

    void setSpeed(float s);
    void update(float dt);
    void submit(Renderer& r);
    bool animating() const;

    // The point (0..23), BAR or OFF under `ray` (OFF: the bear-off strip at the right; BAR: the middle bar), -1 none.
    int pick(const Ray& ray) const;
    // World position of a point's stack top (for a hand reaching there); pointLocal: the same on the board.
    Vector3 pointWorld(int idx, int player) const;
    Vector3 pointLocal(int idx, int player) const;
    // Where the board stands: its local frame (x across, z toward player 0, y up, origin at the board's centre on
    // the table top level) to the world. Identity = the middle of our okey table; tavla now has its own table
    // (w3d::tavlaFrame()).
    void setFrame(const Matrix& frame);
    Vector3 toWorld(Vector3 local) const { return Vector3Transform(local, frame_); }

private:
    struct Checker {
        int player = 0;
        int where = -1;      // point / BAR / OFF
        int slot = 0;        // position in the stack
        Vector3 from{}, to{}, cur{};
        float t = 1.f, dur = 0.4f, delay = 0.f, hop = 0.f;
    };
    struct Die {
        bool visible = false;
        int value = 1;
        Vector3 from{}, to{}, cur{};
        Quaternion qTo{0, 0, 0, 1}, qCur{0, 0, 0, 1};
        Vector3 spinAxis{1, 0, 0};
        float spin = 0.f;
        float t = 1.f, dur = 0.8f, delay = 0.f;
        bool used = false;
    };
    struct Cube {
        bool visible = false;
        int value = 1, owner = -1, offered = 0;
        Vector3 from{}, to{}, cur{};
        Quaternion qFrom{0, 0, 0, 1}, qTo{0, 0, 0, 1}, qCur{0, 0, 0, 1};
        float t = 1.f, dur = 0.6f, hop = 0.f;
    };
    Vector3 slotWorld(int where, int player, int slot) const;
    Quaternion faceUp(int value, float yawDeg) const;
    Quaternion cubeFaceUp(int value) const; // the cube turned so the player reads `value` upright on the side facing him
    Vector3 cubeWorld(int owner, int offered) const;

    Matrix frame_ = MatrixIdentity(), frameInv_ = MatrixIdentity();
    std::array<Checker, CHECKERS> chk_{};
    std::array<Die, 2> dice_{};
    Cube cube_;
    std::vector<int> targets_, sources_;
    int selected_ = -1;
    int keyFocus_ = -1;
    float speed_ = 1.f, time_ = 0.f;
    bool ready_ = false;

    Mesh boardMesh_{}, fieldMesh_{}, pointMesh_[2]{}, checkerMesh_{}, dieMesh_{}, glowMesh_{}, cubeMesh_{};
    Mat matWood_{}, matField_{}, matPoint_[2]{}, matChecker_[2]{}, matCheckerHi_{}, matCheckerSel_{}, matDie_{}, matGlow_{},
        matGlowSel_{}, matGlowTarget_{};
    Mat matCube_{}, matDieUsed_{};
    Mat matGlowTargetCB_{}, matGlowFocus_{};
    Texture2D woodTex_{}, dieTex_{}, glowTex_{}, cubeTex_{}, stripeTex_{};
};

} // namespace r3d
