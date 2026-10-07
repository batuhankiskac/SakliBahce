#pragma once
// Internal runtime state of the characters module (Characters.cpp, CharactersAnim.cpp, CharactersCrowd.cpp).
#include "r3d/CharactersInternal.h"

namespace r3d {
namespace chr {

// ============================================================================ tea glasses on our table
struct TeaGlass {
    Vector3 rest{};           // base centre when standing on its saucer (world)
    Vector3 saucer{};         // saucer position (world, on the felt)
    float yaw = 0.f;          // saucer/glass rotation (radians)
    float level = 0.85f;      // 0..1
    bool oralet = false;
    int holder = -1;          // -1 on the saucer, 0 the player (PlayerHands), 1..3 an opponent, 4 the çaycı
    int holderArm = 0;
    Matrix inHand = MatrixIdentity(); // glass-local -> hand-local (while held)
    Matrix world = MatrixIdentity();  // current transform (glass-local -> world)
    Matrix from = MatrixIdentity();   // world transform when the last attach/detach began
    float blend = 1.f;        // 0..1 progress of the attach/detach blend
    float steamAcc = 0.f;
    float spoonYaw = 0.f;
};

// Every change of who holds a glass goes through here. `blend`: the glass eases from where it is now into the new
// holder's hand (or down onto its saucer); without it the caller places it at once (a teleport).
inline void setGlassHolder(TeaGlass& g, int who, int arm = 0, bool blend = true) {
    g.holder = who;
    g.holderArm = arm;
    if (blend) {
        g.from = g.world;
        g.blend = 0.f;
    }
}

// ============================================================================ a seated body (opponents + patrons)
struct Seated {
    int who = 0;              // 1..3 opponents, 10+ patrons
    PersonLook L;
    const PersonMeshes* pm = nullptr;
    Matrix root = MatrixIdentity(), rootInv = MatrixIdentity();
    Vector3 pos{};            // chair position (world)
    float yawDeg = 0.f;
    uint32_t seed = 1;
    // body channels (radians); *Goal is what behaviours ask for, the springs follow
    float lean = 0.2f, leanV = 0.f, leanBase = 0.2f, leanAdd = 0.f;
    float twist = 0.f, twistV = 0.f, twistAdd = 0.f, gazeTwist = 0.f;
    float roll = 0.f, rollV = 0.f, rollAdd = 0.f;
    float shrug = 0.f, shrugV = 0.f, shrugGoal = 0.f;
    float breath = 0.f, breathRate = 1.f, breathDepth = 1.f;
    // head relative to the torso
    float hYaw = 0.f, hYawV = 0.f, hPitch = -0.2f, hPitchV = 0.f, hRoll = 0.f, hRollV = 0.f;
    float nodPitch = 0.f, nodYaw = 0.f, nodRoll = 0.f;  // gesture offsets added on top
    float headStiff = 7.f;    // spring omega
    Vector3 gaze{0, 0.8f, 0}; // current look target (world)
    Vector3 gazeGoal{0, 0.8f, 0};
    float gazeHold = 0.f;
    int gazeKind = 0;
    Arm arm[2];               // 0 right, 1 left
    // computed each frame
    Matrix torsoW = MatrixIdentity(), headW = MatrixIdentity();
    Matrix headLocal = MatrixIdentity();  // head space -> char-local (resolves head-relative keys)
};

// Verlet chain (Rıza's tespih).
struct Chain {
    std::vector<Vector3> p, prev;
    std::vector<Vector3> tail, tailPrev;   // imame + tassel hanging from the grip
    float seg = TESPIH_SEG;
    float acc = 0.f;   // simulated time not yet stepped (fixed steps of TESPIH_STEP)
    bool init = false;
};

// ============================================================================ Yüz: faces (CharactersFace.cpp)
// One syllable of a line's mouth schedule (timed from the text at the speaker's own pace, like ui::Audio's murmur).
struct MouthSyl {
    float t0 = 0.f, dur = 0.f;  // seconds from the line's start
    float open = 0.f;           // how far the jaw drops on its vowel (0..1)
    float closeTo = 0.3f;       // how far it closes at its end (0 a full closure: m/b/p next, a word's end)
    float round = 0.f;          // rounded vowel (o ö u ü): the lips narrow
};
// Talking: where the mouth's timing comes from.
struct TalkMouth {
    std::vector<MouthSyl> syl;
    int src = 0;                // 0 not decided yet, 1 the voice (Audio::mouthOpen), 2 the text schedule
    float open = 0.f, round = 0.f;
    char punct = 0;             // the line's last mark ('!' '?' or 0)
};

// ============================================================================ opponents (seats 1..3)
struct Opponent : Seated {
    int seat = 1;
    int kind = 0;             // 0 Rıza, 1 Mahmut, 2 Nuri
    int irisMesh = 0;
    Face face, faceGoal;
    Mood mood = Mood::Neutral;
    float moodT = 0.f;
    float eYaw = 0.f, ePitch = 0.f;
    Vector2 micro{0, 0};
    float microIn = 1.f;
    float blinkIn = 2.f, blinkT = -1.f;
    float lidClose = 0.f;     // 0..1 current blink closure
    // speech
    std::string talk;
    float talkT = -1.f, talkDur = 0.f;
    float jaw = 0.f, jawV = 0.f;
    bool talkToHuman = false;
    // behaviour timers
    float sipIn = 20.f, idleArmIn[2] = {3.f, 5.f}, lookIn = 1.f, smokeIn = 8.f, fidgetIn = 12.f, tespihIn = 6.f;
    float posture = 0.f, postureGoal = 0.f, postureIn = 6.f;  // slow lean changes
    float think = 0.f;        // 0..1 (their turn)
    float turnT = 0.f;        // seconds since their turn began
    bool chinRest = false;
    // body gesture (celebrate / grumble / proud / laugh ...)
    int gest = 0;
    float gestT = -1.f, gestDur = 0.f;
    // Mahmut's cigarette
    float drag = 0.f;         // ember brightness boost 0..1
    float exhaleT = -1.f;
    float cigSmokeAcc = 0.f;
    Matrix cigW = MatrixIdentity();
    // Rıza's tespih
    Chain tespih;
    // computed face-part transforms
    Matrix eyeW[2]{}, lidW[2]{}, browW[2]{}, mouthW{}, lipW{};
    int lipVariant = 2;
    // Yüz (CharactersFace.cpp): talking mouth, the expression's age, a delayed reaction (teased), mustache halves
    TalkMouth tm;
    Mood faceMood = Mood::Neutral;
    float moodAge = 0.f;
    float emph = 0.f;                  // brows flick up on loud syllables
    Mood pendMood = Mood::Neutral;
    float pendIn = -1.f, pendFor = 0.f;
    int pendGest = 0;
    float stacheAng = 0.f, stacheLift = 0.f;
    Matrix stacheW[2]{};
    // card games (CharactersCards.cpp): the hand held fanned in the left hand
    bool cards = false;
    Matrix fanRel = MatrixIdentity();  // the fan's frame in the left hand's space (world: fanRel then arm[1].hand)
    Key fanKey;                        // the holding hand at the posture's own lean (updateCardHold moves it along)
};

enum Gesture {
    G_None = 0,
    G_Proud,
    G_Celebrate,
    G_Grumble,
    G_Laugh,
    G_Facepalm,
    G_Shrug,
    G_Nod,
    G_HeadShake,
    G_Drink,
    G_Exhale,
    G_TvCheer,
    G_Clap,
    G_Point,
    G_Disbelief,
    G_CallTea,
};

// ============================================================================ background patrons
struct Patron : Seated {
    bool present = true;      // at this hour (w3d::bgTableBusy)
    bool scarf = false;       // wears one in winter (CharactersLife.cpp)
    // a reaction to a big moment at our table (CharactersLife.cpp)
    float reactIn = -1.f;     // delay before it starts
    float reactT = -1.f, reactDur = 0.f;
    int reactKind = 0, reactStyle = 0;
    bool shout = false;       // says something when the reaction starts
    Vector3 reactAt{};
    int table = 0, side = 0, variant = 0;
    int role = 0;             // index at the table
    float talk = 0.f;         // mouthless: talking makes the head bob and hands gesture
    float nextLook = 1.f;
    int prop = 0;             // 0 none, 1 card fan (left hand), 2 tea glass (right), 3 newspaper (both)
    float propBlend = 0.f;
    bool reading = false;
};

struct BgTable {
    int kind = 0;
    Vector3 c{};
    std::vector<int> patrons;  // indices into Impl::patrons
    int turn = 0;
    float timer = 2.f;
    int step = 0;
    // tavla dice
    Vector3 dicePos[2]{}, diceFrom[2]{}, diceRot[2]{}, diceSpin[2]{};
    float diceT = -1.f;
    bool diceVisible = false;
    bool diceSound = false;
    // cards slapped on the table
    std::vector<Matrix> pileCards;
    float talkTimer = 3.f;
    int speaker = 0;
};

// ============================================================================ the çaycı
struct Cayci {
    PersonLook L;
    const PersonMeshes* pm = nullptr;
    Vector3 pos{};            // on the floor
    float yaw = 0.f;          // radians, facing = (-sin, 0, -cos) of yaw (raylib convention)
    float speed = 0.f;
    float phase = 0.f;        // gait phase (0..1 per two steps)
    std::vector<Vector3> path;
    size_t pathI = 0;
    int state = 0;            // 0 at the counter, 1 walking, 2 serving our table, 3 serving a bg table
    int plan = 0;             // what the current trip is for: 0 none, 1 our table, 2 a bg table
    int bgTarget = -1;
    float timer = 0.f;
    float nextOurs = 28.f, nextBg = 14.f;
    bool called = false;
    float replyIn = -1.f;     // answers a tea order from the counter ("Geliyor!")
    int serveIdx = 0;         // which glass of the tour
    int serveStep = 0;
    float serveT = 0.f;
    std::vector<int> tour;    // glass indices in visiting order
    // body
    float lean = 0.f, leanV = 0.f;
    float hYaw = 0.f, hYawV = 0.f, hPitch = -0.1f, hPitchV = 0.f;
    Vector3 gaze{0, 1.f, 0};
    Arm arm[2];
    float blinkIn = 2.f, blinkT = -1.f;
    float eYaw = 0.f, ePitch = 0.f;
    float smile = 0.3f;
    float talkT = -1.f, talkDur = 0.f, jaw = 0.f;
    std::string talk;
    TalkMouth tm;             // (Yüz) his lips follow his voice too
    // tray pendulum (world)
    Vector3 trayP{}, trayPrev{};
    Vector3 trayPivotF{};     // the grip, low-passed: the swing only answers to his smoothed motion
    float trayAcc = 0.f;      // simulated time not yet stepped (fixed steps)
    bool trayInit = false;
    Matrix trayW = MatrixIdentity();
    // computed
    Matrix torsoW = MatrixIdentity(), headW = MatrixIdentity();
    Matrix thighW[2]{}, shinW[2]{};
    Matrix eyeW[2]{}, lidW[2]{}, browW[2]{}, mouthW{}, lipW{};
    float counterIdleT = 0.f;
    // a round for everyone: after our table, the busy background tables in turn
    std::vector<int> roundBg;
    bool roundQueued = false;
    bool roundReply = false;
};

// ============================================================================ bystanders (a long match)
struct Watcher {
    int state = -1;           // -1 away, 0 walking in, 1 watching, 2 walking out
    bool came = false;        // already came during this match
    int variant = 0;
    PersonLook L;
    const PersonMeshes* pm = nullptr;
    Vector3 pos{}, spot{};
    float yaw = 0.f, speed = 0.f, phase = 0.f;
    std::vector<Vector3> path;
    size_t pathI = 0;
    float wait = 0.f;         // standing still (hands behind his back), or the delay before leaving
    float hYaw = 0.f, hYawV = 0.f, hPitch = 0.f, hPitchV = 0.f;
    Vector3 gaze{0, 0.8f, 0};
    float gazeHold = 0.f;
    float reactIn = -1.f, reactT = -1.f, reactDur = 0.f;
    int reactKind = 0;
    float lean = 0.f, leanV = 0.f;
    Matrix torsoW = MatrixIdentity(), headW = MatrixIdentity();
    Matrix thighW[2]{}, shinW[2]{};
    Matrix upperW[2]{}, foreW[2]{}, handW[2]{};
    HandPose pose[2]{HandPose::Rest, HandPose::Rest};
    Vector3 wristL[2]{};      // smoothed wrist targets (character-local)
    bool wristInit = false;
};

// (Konken son kalan, CharactersKonken.cpp) a regular who burned: up from his chair, he watches the rest standing a step
// behind it (a Watcher body in his own clothes; his face parts ride on the standing head as they sat on the seated one)
struct SeatOut {
    bool on = false;
    Watcher w;
    Matrix rel[10]{};         // eyes, lids, brows, mouth, lip, mustache halves: relative to the head when he got up
};

struct Special;  // (ozelgun) özel günler: bayram clothes, team scarves, the men standing by the TV (CharactersSpecial.cpp)
struct Ocakci;   // (Ocakçı) the tea maker at the counter (r3d/CharactersOcakci.h)

struct CrowdLine {
    std::string text;
    Vector3 where{};
};

// ============================================================================ bubbles
struct BubbleState {
    std::array<std::deque<Bubble>, 5> pending;
    std::array<Bubble, 5> cur;
    std::array<bool, 5> on{};
    float lastStart = -10.f;
};

struct CayciFrame;  // (CharactersCrowd.cpp)

// Everything the module keeps at runtime ("the cast"). Characters::Impl derives from it.
struct Cast {
    Characters* owner = nullptr;
    Renderer* renderer = nullptr;  // set by init (smoke/steam particles are emitted during update)
    Meshes M;
    bool ready = false;
    uint64_t seed = 1;
    Rng rng{1};
    std::array<std::string, 4> names{{"Sen", "Hacı Rıza", "Kel Mahmut", "Emekli Nuri"}};
    std::array<std::string, 4> plateName{};  // drawOverlay: the name plates' widths, measured once per name
    std::array<float, 4> plateW{};
    float animSpeed = 1.f;               // the table's animation speed: tile reaches keep pace with the tiles
    int activeSeat = -1;
    int lastTurnSeat = -1;               // the last real turn (activeSeat is -1 while paused, too)
    bool titleMode = false;
    float time = 0.f;
    Camera3D viewer{};
    bool viewerSet = false;
    ui::Banter banter;
    Opponent opp[4];                // [1..3]
    TeaGlass glass[4];
    std::vector<Patron> patrons;
    std::vector<BgTable> bgTables;
    Cayci boy;
    BubbleState bubbles;
    // --- time of day, season, crowd reactions, bystanders, tea rounds (CharactersLife.cpp)
    int dayMode = 0, seasonMode = 0, phase = 3, seasonNow = 2;
    float lookCheckT = 0.f;
    std::deque<CrowdLine> crowdLines;
    float crowdLineCd = 0.f;      // seconds until the crowd may shout again
    std::deque<int> teaServed;
    Watcher watchers[2];
    bool spectate = false;
    float spectateT = 0.f;
    Mesh scarf[3]{};
    Mat scarfMat{};
    // --- the tavla table (CharactersTavla.cpp): who plays there (0 nobody), where the bystanders look
    int tavlaSeat = 0;
    Vector3 tableFocus{0.f, w3d::TABLE_Y, 0.f};
    std::array<Vector3, 4> glassHome{};  // the saucers' places at our table
    Vector3 ashtrayFor(const Opponent& o) const;
    float humanWait = 0.f;               // seconds the human has been on turn
    float lastHumanEventT = -100.f;
    int submitCount = 0;                 // debug: submissions last frame
    // --- özel günler (CharactersSpecial.cpp, ozelgun): w3d::SpecialDay, where we are (the derby's TV)
    Special* sp = nullptr;
    int specialDay = 0;
    bool specialGarden = false;
    // the TV the room watches: high in the back-left corner inside; on a derby night in the garden the portable one
    // the ocakçı carried out onto a stand by the ocak (RoomSpecial.cpp, Room::Impl::specialTvOut)
    bool gardenTv() const { return specialGarden && specialDay == 4; }
    Vector3 tvPosition() const { return gardenTv() ? Vector3{1.42f, 1.15f, -2.95f} : kTvPos; }
    void initSpecial(Renderer& r);
    void freeSpecial(Renderer& r);
    void updateSpecial(float dt);
    void submitSpecial(Renderer& r);
    void specialCrowdReact(int kind, Vector3 where);
    void specialFloorPeople(std::vector<Vector3>& out) const;  // (duzelt: the standing men for the cat, x / r / z)
    long rackPokes = 0;                  // debug: fingertip-frames inside an opponent's own istaka (harness check;
                                         // only brief grazes of a few mm are expected)
    // --- Konken son kalan (CharactersKonken.cpp): seats 1..3 that burned and left the table
    std::array<SeatOut, 4> seatOut{};
    void updateSeatOut(Opponent& o, float dt);
    void clearSeatedActivity(Opponent& o);
    void submitSeatOut(Renderer& r, const Opponent& o);
    // --- the player's own hands (CharactersPlayer.cpp): glass[0] held by PlayerHands is TeaGlass::holder 0
    Matrix playerGlassW = MatrixIdentity();
    int playerGlassStyle = 0;            // 0 klasik, 1 yeşil, 2 mavi, 3 mor (playerGlassMat[style])
    Mat playerGlassMat[4]{};
    void freePlayerGlass(Renderer& r);
    // --- Ocakçı: the tea maker at the counter (CharactersOcakci.cpp, r3d/CharactersOcakci.h)
    Ocakci* ocak = nullptr;
    void queueOcakci(MeshJobs& J, Renderer& r);  // his body into buildAll's mesh batch (before initOcakci)
    void initOcakci(Renderer& r);
    void freeOcakci(Renderer& r);
    void updateOcakci(float dt);
    void submitOcakci(Renderer& r);
    bool ocakTrayGate();                                          // false: the çaycı waits, the ocakçı fills his tray
    void ocakBoyAtCounter(float dt);                              // the çaycı steps over for the handover (state 0)
    void ocakBoyTrayHand(Key& lk, const Matrix& rootInv, float hs);  // his tray hand while the tray changes hands
    void ocakTrayServed();                                        // a glass left the tray
    void submitTray(Renderer& r, const Matrix& boyTrayW);         // the tray, its glasses as he filled them

    // --- CharactersAnim.cpp
    void setupOpponents();
    void updateOpponent(Opponent& o, float dt);
    void poseSeated(Seated& s, float dt, float headOmega);
    void resolveArm(Seated& s, int a, float dt, float handScale);
    void startTrack(Seated& s, int arm, int kind, std::vector<Key> keys);
    void clearRack(const Opponent& o, int arm, Track& tr) const;
    void onArmEvent(Seated& s, int arm, int ev);
    void react(const okey::GameEvent& e, const okey::Game& g);
    void idleOpponent(Opponent& o, float dt);
    void pickGaze(Opponent& o);
    void setMood(Opponent& o, Mood m, float seconds);
    void startGesture(Opponent& o, int g, float dur);
    void startSip(Opponent& o);
    void startSmoke(Opponent& o, bool ashTap);
    void startTespihFlip(Opponent& o);
    void reachTo(Opponent& o, int arm, Vector3 world, int mode);
    void slamMelds(Opponent& o, int zoneSeat, bool proud);
    void restPose(Opponent& o, int arm, int variant, Key& k);
    // --- CharactersFace.cpp (Yüz): expressions, blinking, the talking mouth, the mustache
    void faceBody(Opponent& o, float dt);             // before poseSeated: the head goes with the expression
    void updateFace(Opponent& o, float dt, float jawGest);
    void startTalk(TalkMouth& tm, const std::string& text, int kind);
    float talkOpen(TalkMouth& tm, int who, float t, float dt);  // < 0: not talking (the line is over)
    void onLineFace(int who, const std::string& text);         // a bubble started: who was named in it?
    void faceReact(int seat, int mood, Vector3 lookAt);        // Characters::react, each in his own way
    void faceCrowd(int kind);                                   // the regulars at a big moment (crowdReact)
    void moodNudge(Opponent& o, Mood m, float seconds);        // only over a calm face
    void updateGlasses(float dt);
    void updateTespih(Opponent& o, float dt);
    void emitSteam(float dt);
    Vector3 viewerPos() const;
    Vector3 headTarget(int seat) const;  // a point to look at for a seat's head
    // --- CharactersCards.cpp
    Key cardHoldKey(const Opponent& o, Matrix& fanRel) const;
    void holdCards(Opponent& o, bool on, const Vector3* pickUpAt);
    bool cardFan(const Opponent& o, Matrix& frame) const;
    void updateCardHold(Opponent& o);
    void playCardFromFan(Opponent& o, Vector3 world, bool toss);
    void gatherCards(Opponent& o, Vector3 from, Vector3 to);
    void shuffleDeck(Opponent& o, Vector3 at, float seconds);
    void dealFromDeck(Opponent& o, Vector3 at, const std::vector<Vector3>& to, float interval);
    // --- CharactersBoard.cpp (Rakip / round 5): a dama disc or a loose card carried by hand, a card drawn into the fan
    void carryPiece(Opponent& o, const PieceCarry& c);
    void drawIntoFan(Opponent& o, Vector3 from, float seconds);
    // --- CharactersCrowd.cpp
    void setupCrowd();
    void updateCrowd(float dt);
    void updatePatron(Patron& p, BgTable& t, float dt);
    void updateCayci(float dt);
    void cayciDecide(float dt, CayciFrame& F);
    void cayciWalk(float dt, CayciFrame& F);
    void cayciServe(float dt, CayciFrame& F);
    void cayciBody(float dt, CayciFrame& F);
    void cayciFace(float dt);
    void cayciGoHome();
    void planTrip(bool ours, int bgTable, const std::vector<int>* onlyGlasses = nullptr);
    void finishOurTour();
    void replanOurTrip();
    void submitCrowd(Renderer& r);
    // --- CharactersLife.cpp
    void initLife(Renderer& r);
    void freeLife(Renderer& r);
    void evalLook(bool force);
    void updateLife(float dt);
    void patronReact(Patron& p, float dt);
    void startPatronReaction(Patron& p);
    Key patronRest(const Patron& p, int a, float t) const;
    void crowdShout(int kind, Vector3 where);
    void updateWatcher(Watcher& w, float dt);
    void submitLife(Renderer& r);
    int pickBgTable();
    // --- Characters.cpp
    void sfx(ui::Sfx s);
    void pushLine(int who, const std::string& text, float seconds, float maxWait, bool teaOrder = false);
    void updateBubbles(float dt);
    void submitOpponent(Renderer& r, const Opponent& o);
    void submitSeatedBody(Renderer& r, const Seated& s, bool detailed);
};

// ---- arm helpers (CharactersAnim.cpp), shared with CharactersCards.cpp
// Reaching across the table: the body leans in (up to kMaxExtraLean past its posture) and the shoulder rolls
// forward (up to kProtract) before the arm is at full stretch.
constexpr float kMaxExtraLean = 0.62f;
constexpr float BY = w3d::BG_TABLE_Y;  // the background tables' top (CharactersCrowd.cpp, CharactersLife.cpp)
constexpr float kProtract = 0.04f;
constexpr float kReachFrac = 0.97f;  // of the arm's length: the elbow stays a little bent
Vector3 shoulderAt(const PersonLook& L, float sd, float lean);           // shoulder joint (character-local)
float leanNeeded(const PersonLook& L, float leanBase, float sd, Vector3 w);
// `tL` (character-local) pulled back until hand-space point `off` is within reach of the fully leaning body.
Vector3 clampReach(const Opponent& o, int arm, Vector3 tL, Vector3 f, Vector3 p, Vector3 off);
const std::array<std::array<Vector3, 5>, HAND_POSES>& handTips();
// Wrist position that puts hand-space point `off` at `point`.
Vector3 wristFor(Vector3 point, Vector3 fingers, Vector3 palm, Vector3 off, float scale, bool left);
void gripDirs(Vector3 a, Vector3 f, bool left, Vector3& fingers, Vector3& palm);
Key mk(float t, Vector3 pos, Vector3 fingers, Vector3 palm, HandPose pose, float lift = 0.f, int ease = 0, int ev = 0);
// mk with the palm made perpendicular to the fingers first (the crowd's and the çaycı's keys)
Key mkOrtho(float t, Vector3 pos, Vector3 fingers, Vector3 palm, HandPose pose, float lift = 0.f, int ease = 0, int ev = 0);
Key touching(Key k);
Vector3 mirrorL(Vector3 v, bool left);
// The standing men's heads (CharactersFace.cpp): the head's yaw/pitch springs toward `gaze` (torso space), a blink
// now and then (every 2..maxGap s, `dur` long; returns the lids' closure), eyes / lids / brows, mouth and lower lip.
void headLookSpring(const Matrix& torsoW, const PersonLook& L, Vector3 gaze, float pitchGain, float pitchMin, float omega,
                    float dt, float& yaw, float& yawV, float& pitch, float& pitchV);
float blinkStep(Rng& rng, float& blinkIn, float& blinkT, float dt, float maxGap, float dur);
void standingEyes(const FaceGeo& fg, const Matrix& headW, Vector3 gaze, float lidBase, float lidClose, float browTilt, float dt,
                  float& eYaw, float& ePitch, Matrix eyeW[2], Matrix lidW[2], Matrix browW[2]);
void standingMouth(const FaceGeo& fg, const Matrix& headW, float jaw, Matrix& mouthW, Matrix& lipW);
// Standing / walking legs (CharactersLife.cpp): thigh and shin frames for the gait phase (0..1) and walk (0..1).
void walkLegs(const Matrix& root, Vector3 hip, float phase, float walk, Matrix thighW[2], Matrix shinW[2]);
// A hand lying on the felt with its palm centre over (x, z) (character-local).
Key onFelt(float x, float z, Vector3 f, Vector3 p, HandPose pose, float hs, bool left);

// For tools/characters_snapshot.cpp only: the state of the most recently initialised Characters.
Cast* debugLastCast();

} // namespace chr

struct Characters::Impl : chr::Cast {};

} // namespace r3d
