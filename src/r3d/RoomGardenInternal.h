#pragma once
// Private to the room module: the garden kahvehane (src/r3d/RoomGarden*.cpp, room owner). Not part of any contract.
//
// The garden keeps the room's floor plan (w3d::*): our table, the tavla table, the four background tables with their
// chairs and the tea counter (now an outdoor ocak under a little tiled roof) stand where they stand inside, so the table,
// the camera, the people (the çaycı's waypoints, the bystanders' way in) and the cat need not know where we are.
//   left  (-X): a low rubble-stone wall with the garden gate where the street door is; the lane and its passers-by
//               beyond (the street canvas across the lane)
//   back  (-Z): the çınar's trunk (the scoreboard hangs on it, at w3d::SCOREBOARD_POS), a stone parapet and the sea
//   right (+X): the kahvehane's front: door, windows with shutters, the sign, a tiled eave
//   front (+Z): a tall whitewashed garden wall with geraniums and basil, the wall bench in front of it
//   above:      a vine trellis (asma) at the old ceiling height (the pendants hang from it), string lights, the çınar's
//               canopy over everything; in rain and in winter a striped awning (tente) is stretched over the trellis
#include "r3d/RoomInternal.h"

#include <vector>

namespace r3d {
namespace rm {

// Garden geometry builders, one per material / flag group (RoomGarden.cpp, RoomGardenTree.cpp).
struct GardenBuilders {
    MeshBuilder ground, cobble, cobble2, stone, stoneCast, white, whiteCast, facade, roof, roofCast, wood, woodCast, paint, paintCast,
        metal, metalCast, clay, clayCast, leafy, bark, barkCast, cloth, clothCast, street, signs, art, shutter, brass;
};

// ---- textures (RoomGardenTex.cpp)
Texture2D genGroundTexture(uint32_t seed);     // sandy mortar between the cobbles, a few pebbles
Texture2D genCobbleTexture(uint32_t seed);     // worn stone grain for the cobble tops (vertex colours vary each stone)
Texture2D genRubbleTexture(uint32_t seed);     // dry-laid rubble stone wall (moloz taş), 1 repeat = 1.6 m
Texture2D genBarkTexture(uint32_t seed);       // the plane tree's mottled, peeling bark (cream, olive, grey patches)
Texture2D genRoofTileTexture(uint32_t seed);   // terracotta Turkish tiles (alaturka kiremit) in rows
Texture2D genAwningTexture(uint32_t seed);     // the awning: green and cream stripes, canvas weave, rain stains
Texture2D genShutterTexture(uint32_t seed);    // louvred wooden shutter, faded green paint
Texture2D genStrawTexture(uint32_t seed);      // woven straw (hasır) for the chair seats
// The view around the garden on a cylinder (360° x -14°..+31°, see kPano*): the sea with the far shore, its hills, a
// mosque and the bridge, ferries; the neighbourhood's houses and cypresses toward the front. By phase (w3d::DayPhase),
// season and weather (overcast). The sky above it is transparent (the dome shows through).
constexpr int PANO_W = 4096, PANO_H = 512;
constexpr float kPanoR = 42.f, kPanoLo = -14.f, kPanoHi = 31.f;  // radius (m), elevations (degrees) of the band
void drawGardenView(RenderTexture2D& rt, uint32_t seed, int phase, int season, bool overcast, float sunAzDeg);
// Sky colour at an elevation (degrees) for the dome (and the haze painted into the view at the horizon).
Color gardenSky(int phase, bool overcast, float elevDeg);
// Shop sign over the door ("SaklıBahçe Kıraathanesi") and the menu board by the gate, painted once.
constexpr int GSIGN_W = 1024, GSIGN_H = 512;
namespace gsign {
constexpr Rectangle SIGN{0, 0, 1024, 200};
constexpr Rectangle MENU{0, 216, 360, 296};     // "Bahçemiz açıktır" chalk board by the gate
constexpr Rectangle WINDOW{376, 216, 320, 296}; // a lit window from outside: lace, warm room, a lamp
constexpr Rectangle PLATE{712, 216, 312, 120};  // street number / name plate
}  // namespace gsign
void drawGardenSigns(RenderTexture2D& rt, uint32_t seed);

// ---- the garden's state
struct Garden {
    bool built = false;
    // textures / canvases
    Texture2D texGround{}, texCobble{}, texRubble{}, texWhite{}, texFacade{}, texBark{}, texRoof{}, texAwning{}, texShutter{},
        texStraw{};
    RenderTexture2D cvPano{}, cvSigns{};
    int panoPhase = -1, panoSeason = -1;
    bool panoOvercast = false;
    // materials
    Mat mGround, mCobble, mRubble, mWhite, mFacade, mBark, mRoof, mAwning, mShutter, mPano, mSky, mSigns, mLeaf, mBlossom,
        mWindow, mStrBulb, mClay, mBird, mCoal, mStraw, mChairWood, mStar;
    // dynamic meshes
    Mesh sky{};                    // dome (vertex colours), rebuilt with the look
    int skyPhase = -1;
    bool skyOvercast = false;
    Mesh pano{};                   // the view's cylinder band
    Mesh chair{};                  // hasır chair (wooden frame, straw seat), sitter faces -Z
    Mesh chairSeat{};
    Mesh awning{};                 // the tente over the trellis (rain / winter)
    Mesh windows{};                // facade window glass (lit from inside at night)
    Mesh strBulbs{};               // string-light bulbs
    std::vector<Vector3> bulbPos;  // their centres (glows)
    Mesh coals{};                  // the mangal's coals (winter)
    Vector3 mangalTop{};
    // the çınar (RoomGardenTree.cpp): branches are static; leaves in sway groups, rebuilt for the season
    struct Sway {
        Mesh mesh{};
        Vector3 pivot{};
        float amp = 1.f, phase = 0.f, freq = 1.f;
    };
    std::vector<Sway> canopy;      // çınar leaf clusters
    std::vector<Vector3> canopyCentres, erguvanTips;  // where the leaves grow (branch tips)
    std::vector<Sway> vines;       // grape vine on the trellis (with grapes in late summer / autumn)
    Mesh erguvan{};                // the Judas tree's blossoms / leaves by the parapet
    Mesh groundLeaves{};           // fallen leaves on the cobbles (autumn)
    int foliageSeason = -1;
    Vector3 trunkBase{};
    float trunkR = 0.7f;
    // the sun (key light by day) eases toward the phase's direction
    Vector3 sunDir{0.3f, -0.85f, -0.3f};  // direction the light travels
    Vector3 sunDirTarget{0.3f, -0.85f, -0.3f};
    float awningK = 0.f;           // 0 open trellis .. 1 the awning is up (rain, winter)
    float bulbsK = 0.f;            // string lights on (evening / night)
    float mangalK = 0.f;           // the brazier burns (winter)
    float wind = 0.f;              // gusts (sway, falling leaves)
    float coalAcc = 0.f;

    // ---- life (RoomGardenLife.cpp)
    Mesh birdBody[2]{}, birdHead[2]{}, birdWing[2]{};          // [0] sparrow, [1] pigeon (local: forward -Z, feet at 0)
    Mesh gullBody{}, gullWing{};
    struct Bird {
        int kind = 0;              // 0 sparrow, 1 pigeon
        Vector3 pos{}, from{}, to{};
        float yaw = 0.f, headYaw = 0.f, headPitch = 0.f, peck = 0.f;
        float t = 0.f, dur = 1.f;  // current move
        int state = 0;             // 0 idle / pecking, 1 hop, 2 fly, 3 walk
        float timer = 1.f, flap = 0.f, bob = 0.f, scale = 1.f;
        int spot = -1;
        Color body{}, head{}, wing{};
    };
    std::vector<Bird> birds;
    struct Gull {
        Vector3 c{};
        float r = 10.f, ang = 0.f, speed = 0.1f, h = 18.f, flap = 0.f, flapT = 0.f, bank = 0.f;
    };
    std::vector<Gull> gulls;
    struct Fall {
        Vector3 p{};
        float speed = 0.5f, sway = 0.4f, phase = 0.f, size = 0.08f, spin = 0.f;
        int kind = 0;
    };
    std::vector<Fall> falls;       // autumn leaves / spring petals drifting down
    Texture2D texPetal{};
    float fallAcc = 0.f;
};

}  // namespace rm
}  // namespace r3d
