#pragma once
// A dama board in 3D at the two-seat tavla table (w3d::tavlaFrame): a wooden board with 8 x 8 inlaid squares (maple
// and walnut), 32 wooden discs (light maple / dark walnut), damas shown as two stacked discs, moves that slide one
// square or hop from jump to jump, captured discs lifted off to the side of the one who took them, highlights
// (movable discs, the chosen one, its targets, the discs a move would take) and ray picking of squares.
// Squares follow dama::Board: s = row * 8 + col, row 0 nearest the player (local +z), col 0 on his left (local -x).
#include "r3d/Gfx.h"
#include "r3d/PieceCarry.h"

#include <raymath.h>

#include <array>
#include <cstdint>
#include <vector>

namespace r3d {

class Dama3D {
public:
    static constexpr int PIECES = 32; // 0..15 light, 16..31 dark

    bool init(Renderer& r);
    void shutdown(Renderer& r);

    // The board as the engine has it (signed cells: + player 0, - player 1, |2| a dama) and who plays the light discs.
    // Discs go to their places (animated unless `snap`); a new game's setup flies them back from the trays.
    void setBoard(const std::array<int8_t, 64>& cells, int lightPlayer, bool snap);
    // A move: the disc on `from` slides (one plain step) or hops along `path`; each disc on `captured` is lifted off
    // to the capturer's side as soon as it has been jumped; `promotes` crowns it when it lands. Call before setBoard()
    // of the new board. `delay` holds it back (a hand reaching for the disc first).
    // `hand` (Rakip / round 5): the move is made by a regular's hand (Characters::carry): the disc is lifted higher, and
    // the taken discs stay on the board until it has landed, then go one by one as the hand takes them off; `hand` is
    // filled in with the way (world: the disc's top at every point) and the times for Characters::carry.
    void playMove(int player, int from, const std::vector<int>& path, const std::vector<int>& captured, bool promotes,
                  float delay, PieceCarry* hand = nullptr);

    // Highlights: discs that can move (warm gold), the selected one (lifted), the squares it may go to (green; in the
    // colour-blind mode sky blue with stripes), the keyboard's / hovered target (brighter) and the discs the shown move
    // would take (a red glow under them).
    void setHighlights(const std::vector<int>& movable, int selected, const std::vector<int>& targets);
    void setKeyFocus(int square) { keyFocus_ = square; }
    void setDoomed(const std::vector<int>& squares) { doomed_ = squares; }
    // The player drags the disc on `square` (-1: none) to `local` (board space, on the board's surface).
    void setDrag(int square, Vector3 local);

    void setSpeed(float s);
    void update(float dt);
    void submit(Renderer& r);
    bool animating() const;

    // The square (0..63) under `ray`, -1 none. pickLocal: where the ray meets the board's surface (board space).
    int pick(const Ray& ray) const;
    bool pickLocal(const Ray& ray, Vector3& local) const;
    Vector3 squareLocal(int s) const;
    Vector3 squareWorld(int s) const { return toWorld(squareLocal(s)); }
    // The board's frame: local x across (to the player's right), z toward the player, y up, origin at the board's
    // centre on the table top. w3d::tavlaFrame() puts it on the tavla table.
    void setFrame(const Matrix& frame);
    Vector3 toWorld(Vector3 local) const { return Vector3Transform(local, frame_); }

private:
    struct Piece {
        int light = 1;       // 1 light, 0 dark
        int square = -1;     // on the board, -1 off it (in a tray)
        int tray = -1;       // the capturer's tray (0 near / 1 far) while off the board
        int slot = 0;
        bool king = false;
        float crown = 0.f;   // 0..1: the second disc settling on top
        std::vector<Vector3> keys; // the way: keys[0] = start
        std::vector<float> hops;   // per segment
        float t = 1.f;       // 0..keys.size()-1
        float delay = 0.f;
        float segDur = 0.3f; // per segment
        Vector3 cur{};
    };
    Vector3 trayLocal(int tray, int slot) const;
    void glide(Piece& p, Vector3 to, float hop, float delay);
    int pieceAt(int square) const;
    int trayCount(int tray) const;

    Matrix frame_ = MatrixIdentity(), frameInv_ = MatrixIdentity();
    std::array<Piece, PIECES> pcs_{};
    int lightPlayer_ = 0;
    std::vector<int> movable_, targets_, doomed_;
    int selected_ = -1, keyFocus_ = -1, drag_ = -1;
    Vector3 dragLocal_{};
    float speed_ = 1.f, time_ = 0.f;
    bool ready_ = false;

    Mesh boardMesh_{}, sqMesh_[2]{}, pieceMesh_{}, glowMesh_{}, ringMesh_{};
    Mat matFrame_{}, matSq_[2]{}, matPiece_[2]{}, matPieceHi_[2]{}, matPieceSel_{}, matGlowTarget_{}, matGlowTargetCB_{},
        matGlowFocus_{}, matDoomed_{}, matRing_{}, matShadowRim_{};
    Texture2D frameTex_{}, sqTex_[2]{}, pieceTex_[2]{}, glowTex_{}, stripeTex_{};
};

} // namespace r3d
