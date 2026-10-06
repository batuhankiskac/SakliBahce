#pragma once
// Harness-only hooks into Table3D (tools/table3d_snapshot.cpp). Never used by the game. Table owner.
#include "r3d/Table3D.h"
#include <string>
#include <vector>

namespace r3d {
namespace table3dtest {

// One simulated frame. button: 0 up, 1 pressed this frame, 2 held, 3 released this frame.
// key: 'S', 'C', '\n' or 0. `cam` is the camera used for picking this frame.
void input(Table3D& t, float dt, Vector2 mouse, int button, int key, bool humanInput, const Camera3D& cam);
// Runs frames until nothing flies / deals any more (then a few more so tweens converge).
void settle(Table3D& t, const Camera3D& cam, bool humanInput = true);

std::vector<int> rackSlots(Table3D& t);                      // 32 slots, -1 = empty
void setRackSlots(Table3D& t, const std::vector<int>& slots);
// 1 El/Per Aç, 2 Geri Ver, 3 Seri Diz, 4 Çift Diz, 5 Menü, 6 confirm yes, 7 confirm no, 8 Yapay Zeka
void pressButton(Table3D& t, int which);
int draggedTile(Table3D& t);
bool confirmActive(Table3D& t);
int selectedTile(Table3D& t);
int hoverTile(Table3D& t);
// the keyboard's cursor: put it on a rack slot / the tile it is on now (-1 while the keyboard is not in use)
void setKeyboardSlot(Table3D& t, int slot);
int keyboardTile(Table3D& t);
int rackTileUnderRay(Table3D& t, const Ray& ray);            // human rack tile id hit by the ray, -1 if none
Vector3 tileFaceCenter(Table3D& t, int id);                  // world centre of a tile's (current) face
Vector3 tileCenter(Table3D& t, int id);
Vector3 slotFaceCenter(Table3D& t, int slot);                // resting face centre of a human rack slot
Vector3 meldPoint(Table3D& t, int meld, float along);        // along 0 = front end .. 1 = back end
Vector3 pileTopPoint(Table3D& t);
Vector3 leftTopPoint(Table3D& t);
Vector3 discardPoint(Table3D& t);
std::string validate(Table3D& t, bool atRest);               // "" if consistent with the game
std::string lastToast(Table3D& t);
int flyingCount(Table3D& t);
int submitCount(Table3D& t);                                 // draw submissions of the last submit()

// pure rack logic
std::vector<std::vector<int>> parseGroups(const std::vector<int>& slots);
bool layoutArrangement(const std::vector<std::vector<int>>& melds, const std::vector<int>& leftovers,
                       std::vector<int>& slots);
int chooseFreeSlot(const std::vector<int>& slots, int pref);
bool moveTile(std::vector<int>& slots, int from, int to, bool after);

} // namespace table3dtest
} // namespace r3d
