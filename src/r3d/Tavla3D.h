#pragma once
// A tavla board in 3D on our table, between the player (seat 0, bottom) and Kel Mahmut (seat 2, across): a walnut
// box with inlaid points (haneler), the middle bar, 30 checkers (pul) that slide / hop between points, two dice that
// are thrown and tumble to their numbers, highlights for the playable points and ray picking of points.
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
    void moveChecker(int player, int from, int to, float delay);

    // Dice: thrown by `player` (0 from the bottom, 1 from the top), landing showing d1 / d2 (d2 = 0: one die).
    // `opening`: the opening roll, one die each (d1 player 0's, d2 player 1's).
    void throwDice(int player, int d1, int d2, bool opening, float delay);
    void setDiceUsed(const std::array<bool, 4>& used, int n, bool isDouble); // dims used dice
    void hideDice();

    // Highlights: points the selected checker can go to, the points with playable checkers, the selected one.
    void setHighlights(const std::vector<int>& targets, const std::vector<int>& sources, int selected);

    void setSpeed(float s);
    void update(float dt);
    void submit(Renderer& r);
    bool animating() const;

    // The point (0..23), BAR or OFF under `ray` (OFF: the bear-off strip at the right; BAR: the middle bar), -1 none.
    int pick(const Ray& ray) const;
    // World position of a point's stack top (for a hand reaching there).
    Vector3 pointWorld(int idx, int player) const;

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
    Vector3 slotWorld(int where, int player, int slot) const;
    Quaternion faceUp(int value, float yawDeg) const;

    std::array<Checker, CHECKERS> chk_{};
    std::array<Die, 2> dice_{};
    std::vector<int> targets_, sources_;
    int selected_ = -1;
    float speed_ = 1.f, time_ = 0.f;
    bool ready_ = false;

    Mesh boardMesh_{}, fieldMesh_{}, pointMesh_[2]{}, checkerMesh_{}, dieMesh_{}, glowMesh_{};
    Mat matWood_{}, matField_{}, matPoint_[2]{}, matChecker_[2]{}, matCheckerHi_{}, matCheckerSel_{}, matDie_{}, matGlow_{},
        matGlowSel_{}, matGlowTarget_{};
    Texture2D woodTex_{}, dieTex_{}, glowTex_{};
};

} // namespace r3d
