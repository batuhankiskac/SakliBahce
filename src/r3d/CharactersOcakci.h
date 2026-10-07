#pragma once
// Internal to the characters module: the ocakçı, the older tea maker who never leaves the tea counter (ocak). He stands
// at the counter's left end (inside and in the garden alike: the counter stands at the same place), brews (the demlik
// off the çaydanlık, water from the big kettle into it), fills the çaycı's hanging tray when a trip is due and hands it
// over, rinses and wipes glasses at his crate, turns up the flame, wipes the counter, reads the paper, chats and (inside)
// watches the match on the TV. Owner: src/r3d/CharactersOcakci.cpp (+ CharactersOcakciMesh.inc for his body, modelled
// with CharactersMesh.cpp's SDF helpers). The rest of the module reaches him through the small Cast::ocak* hooks
// (CharactersState.h).
#include "r3d/CharactersState.h"

namespace r3d {
namespace chr {

// His head, torso (white shirt, sleeves rolled up, a navy bib apron), brows, mustache, arms (CharactersOcakciMesh.inc,
// compiled inside CharactersMesh.cpp) and the apron's skirt over his legs (local: the standing root, hips at y 0.94).
PersonLook lookOcakci();
void buildOcakciPerson(PersonMeshes& pm, Mesh& apronSkirt, Renderer& r);

// A thing he picks up: where it is (world), who holds it (0 nobody: it rests at `rest`, 1 his right hand, 2 his left),
// and a short blend when it changes hands so it never pops.
struct OcakProp {
    int holder = 0;
    Matrix world = MatrixIdentity(), from = MatrixIdentity(), rest = MatrixIdentity();
    float blend = 1.f;
};

struct Ocakci {
    // ---- meshes & materials (built at init)
    PersonLook L;
    PersonMeshes pm;
    Mesh apron{}, kettleMesh{}, demlikMesh{}, crate{}, basin{}, water{}, bez{}, paperFolded{};
    Mat chrome{}, crateMat{}, copper{}, waterMat{}, bezMat{};
    bool built = false;
    int venue = 0;            // Room::venue(): 0 inside (the TV), 1 the garden

    // ---- body
    Vector3 pos{};
    float yaw = 0.f, yawRate = 0.f;   // radians (facing = (-sin, 0, -cos))
    float speed = 0.f, phase = 0.f;   // a half step now and then (to hand the tray over)
    float lean = 0.1f, leanV = 0.f;
    float hYaw = 0.f, hYawV = 0.f, hPitch = -0.2f, hPitchV = 0.f;
    Vector3 gaze{2.4f, 1.1f, -3.1f};
    Matrix root = MatrixIdentity(), torsoW = MatrixIdentity(), headW = MatrixIdentity(), apronW = MatrixIdentity();
    Matrix thighW[2]{}, shinW[2]{};
    struct Hand {
        Vector3 wrist{}, fingers{0, 0, -1}, palm{0, -1, 0};  // smoothed targets (world)
        HandPose pose = HandPose::Rest;
        Vector3 shoulder{}, elbow{};
        Matrix upper = MatrixIdentity(), fore = MatrixIdentity(), hand = MatrixIdentity();
        bool init = false;
    } arm[2];  // 0 right, 1 left
    // face
    float blinkIn = 2.f, blinkT = -1.f, eYaw = 0.f, ePitch = 0.f, jaw = 0.f;
    Matrix eyeW[2]{}, lidW[2]{}, browW[2]{}, mouthW{}, lipW{};

    // ---- what he is doing
    int act = 0;              // OcakAct (CharactersOcakci.cpp)
    float t = 0.f, dur = 1.f, wait = 0.f;
    int partner = -1;         // chat: -1 the çaycı, else a patron index
    float nextBrew = 14.f;    // seconds until he brews again
    Vector3 idleGaze{2.4f, 1.1f, -3.1f};
    float idleGazeIn = 0.f;
    std::string forceAct;     // SAKLI_OCAKCI (snapshots): the first thing he does
    float tl[16]{};           // the current action's timeline (CharactersOcakci.cpp: FillT)
    int nFill = 0, fillIdx[3]{};

    // ---- props
    OcakProp kettle, demlik, glass, paper, tray;
    int bezAt = 0;            // 0 over his left shoulder, 1 in his right hand
    bool paperOpen = false;   // read open in both hands
    float flameUp = 0.f;      // the burner turned up (a brighter flame)
    float steamAcc = 0.f, glassSteamAcc = 0.f;

    // ---- the çaycı's hanging tray
    int trayOwner = 0;        // 0 the çaycı, 1 the ocakçı (tray.holder: 0 on the counter, 1 in his right hand)
    float trayLvl[3] = {0.f, 0.f, 0.f};  // its three glasses (0 empty .. 1 full)
    int order = 0;            // 0 none, 1 the çaycı waits for a filled tray, 2 handed over: his trip may start
    float orderT = 0.f;       // seconds since the order (a safety valve lets the trip go anyway)
    int boyReach = 0;         // the çaycı's tray hand: 0 his own, 1 reaching to the meeting point, 2 free (rest)
};

}  // namespace chr
}  // namespace r3d
