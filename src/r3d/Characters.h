#pragma once
// Every person in the kıraathane: the three opponents at our table (seats 1..3) with their chairs and
// personal props (tea glasses / oralet, cigarette, tespih, the human's own tea glass at GLASS_POS[0] too),
// background patrons at w3d::BG_TABLES, and the tea boy (çaycı) who walks around serving tea.
// Animations react to game events (reaching for tiles, slapping melds down, sipping tea, smoking,
// talking, celebrating). Speech bubbles use ui::Banter and are drawn as a 2D overlay. PUBLIC API FROZEN.
// Implementation: src/r3d/Characters*.cpp — characters owner (struct Characters::Impl).
#include "core/Game.h"
#include "r3d/Gfx.h"
#include "r3d/PieceCarry.h"
#include "ui/Audio.h"
#include "ui/Banter.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace r3d {

class Characters {
public:
    Characters();
    ~Characters();
    Characters(const Characters&) = delete;
    Characters& operator=(const Characters&) = delete;

    bool init(Renderer& r, uint64_t seed);
    void shutdown(Renderer& r);
    void setNames(const std::array<std::string, 4>& names); // seat 0 = the human (never drawn, never talks)
    void setActiveSeat(int seat);                           // whose turn (-1 none)
    void onEvent(const okey::GameEvent& e, const okey::Game& g);
    void onTvGoal();                                        // someone reacts to the TV
    void onCatMeow(Vector3 where);                          // heads turn to the kahvehane cat
    void say(int seat, const std::string& text, float seconds = 3.f);
    // The other games (tavla, the card games), which have no okey events: seat 1..3 reaches out — mode 0 takes
    // something from `target` to their hand, 1 puts something from their hand down at `target` (timed like the
    // tiles: w3d::BOT_TAKE_LEAD / BOT_GIVE_LEAD). `mood`: 0 none, 1 happy, 2 grumpy, 3 surprised.
    void reach(int seat, Vector3 target, int mode);
    // Tavla at its own table (CharactersTavla.cpp): `seat` (1..3) gets up and sits across the player at the tavla
    // table (w3d::tavlaFrame), their glass and the player's go along; false brings them back to the okey table.
    void setTavlaTable(bool on, int seat);
    int tavlaSeat() const; // 0: nobody plays at the tavla table
    // Konken "son kalan" (CharactersKonken.cpp): `seat` (1..3) burned — he gets up and watches the rest standing a step
    // behind his chair (his glass stays on the table); false brings him back to his chair. `instant`: no walk (a resumed
    // match). headPosition / bubbles follow him.
    void setSeatOut(int seat, bool out, bool instant = false);
    bool seatOut(int seat) const;
    // The people standing or walking on the floor right now (bystanders, the special days' men, the çaycı, a burned
    // Konken player): x, z of the feet and y = a radius that keeps clear of them (the cat walks round them).
    void floorPeople(std::vector<Vector3>& out) const;
    // ---- the card games (CharactersCards.cpp); seat 1..3, times at animation speed 1 (they follow setAnimationSpeed)
    // holdCards: the seat takes its hand up (picking the dealt cards off the felt at `pickUpAt` when given) and holds
    // it fanned in the left hand; false puts the hand down. While held, cardFan gives the fan's frame (world): origin
    // at the pivot between thumb and palm, x across the cards, y their faces' normal (toward the holder), -z up the
    // cards; it moves with the hand (cardlayout::fanCard places the cards in it).
    void holdCards(int seat, bool on, const Vector3* pickUpAt = nullptr);
    bool cardFan(int seat, Matrix& frame) const;
    // The right hand pulls a card from the fan and lays it at `target` (toss: lets it go from higher up), on the
    // cards' timeline: the card leaves the fan at w3d::BOT_GIVE_LEAD and lands about w3d::BOT_TILE_FLIGHT later.
    void playCard(int seat, Vector3 target, bool toss);
    // A free hand comes down on the cards at `from` (~0.45 s) and pushes them together to `to` (~0.8 s): a trick
    // taken, Pişti's middle captured.
    void gatherCards(int seat, Vector3 from, Vector3 to);
    // The dealer's hands: a riffle shuffle over the deck at `at` (`seconds` long), then dealing: the left hand holds
    // the deck, the right pushes card k toward to[k] at k * interval seconds.
    void shuffleDeck(int seat, Vector3 at, float seconds);
    void dealCards(int seat, Vector3 at, const std::vector<Vector3>& to, float interval);
    // ---- board pieces and loose cards (CharactersBoard.cpp; Rakip / round 5): seat 1..3's free hand takes a piece
    // up, carries it along c.path and puts it down, then takes the captured ones off the board (r3d/PieceCarry.h).
    // drawCard: the right hand takes the card lying at `from` (the stock) at w3d::BOT_TAKE_LEAD and brings it up into
    // the held fan over `seconds` (the card's own flight); without a fan it is carry() to the hand's side.
    void carry(int seat, const PieceCarry& c);
    void drawCard(int seat, Vector3 from, float seconds);
    void react(int seat, int mood, Vector3 lookAt);
    // A line for the banter system's rate limits (the other games' reactions): queued like Banter lines.
    bool chat(int seat, const std::string& text, bool important = false);
    void setTitleMode(bool on);                             // menu backdrop: no bubbles, relaxed idles
    void setAnimationSpeed(float speed);                    // Table3D's tile speed: reaches for tiles keep pace

    // `viewer` is the human's camera (for eye contact / glances toward the player).
    void update(float dt, const Camera3D& viewer);
    void submit(Renderer& r);             // all people + chairs at our table + personal props, before render()
    void drawOverlay(const Renderer& r);  // speech bubbles in ui virtual coords (inside App's BeginMode2D)
    Vector3 headPosition(int seat) const; // world position of each head (seat 0: approx. the camera)
    std::function<void(ui::Sfx)> playSfx;
    // Voice hook: called when a speech bubble starts (who 1..3 the regulars, 4 the çaycı) with its text.
    std::function<void(int who, const std::string& text)> speak;
    // Lip sync (CharactersFace.cpp): asked every frame while `who` (as for speak) talks — how open his mouth is
    // in the voice that says the line (0..1; ui::Audio::mouthOpen), or < 0 when no voice says it (voices off, no
    // audio): then the mouth follows a syllable schedule timed from the text, the same pace as the voice.
    std::function<float(int who)> voiceMouth;
    ui::Banter& banter();                 // the banter system (for the voice module)

    // ---- around the table (Daytime.h / CharactersLife.cpp) ----
    // Time of day / season, the same modes as Room::setTimeOfDay / setSeason (ui::Settings::dayTime / season, 0 =
    // otomatik). Cheap: call every frame or on change. Fewer patrons in the morning (tavla + the two old men), the
    // card players from noon, everybody in the evening and at night; scarves in winter.
    void setTimeOfDay(int mode);
    void setSeason(int mode);
    // (Ocakçı) Where we are (Room::venue(): 0 inside, 1 the garden): the ocakçı at the counter watches the TV only
    // inside. His small sounds (glasses set down, a tray handed over) are quieter than the table's: playSfxVol
    // (volume, pitch) — nothing is played while it is unset.
    void setVenue(int venue);
    std::function<void(ui::Sfx, float volume, float pitch)> playSfxVol;

    // The background crowd reacts to a big moment at our table: heads turn to `where`, some throw their arms up,
    // clap, half rise from their chairs (Cheer), groan / hold their heads (Groan) or laugh (Laugh). The bystanders
    // watching a long match react too. `strength` 0..1 scales how many join in.
    enum CrowdKind { CrowdCheer = 0, CrowdGroan = 1, CrowdLaugh = 2 };
    void crowdReact(int kind, Vector3 where = Vector3{0.f, 0.8f, 0.f}, float strength = 1.f);
    // What the crowd shouts (short Turkish lines: "Vay be!", "Helal olsun!", "Yazık oldu…", "Sağ ol!"), one per
    // call, oldest first; `where` = the speaker's head (world) for a small bubble / toast and a murmur in Audio.
    bool consumeCrowdLine(std::string& text, Vector3& where);

    // While a long match runs (App passes true every frame or on change), after a while one, later two men come
    // over from the street door and watch from behind Kel Mahmut's chair; false (the match ended) sends them off.
    void setSpectatorMatch(bool on);

    // Özel günler (CharactersSpecial.cpp, ozelgun): w3d::SpecialDay (r3d/SpecialDay.h) and whether we are in the garden.
    // Bayram: ties and carnations; the derby: team scarves and three men standing at the TV (crowdReact moves them too);
    // Ramazan evenings: two more men standing by the card players. Cheap: call every frame or on change.
    void setSpecialDay(int day, bool garden);

    // Tea. serveTea: the çaycı comes round our table now (round = then to every busy table too). orderTeaRound:
    // "Çaylar benden!" — payer 1..3 is the regular who treats everyone (he shouts it), 0 = the player won and a
    // regular calls the çaycı for him; the çaycı answers and brings a round to everyone, the patrons thank him.
    void serveTea(bool round);
    void orderTeaRound(int payerSeat);
    bool consumeTeaServed(int& seat);  // once per glass the çaycı refilled at our table (seat 0..3)

    // ---- the player's own hands (r3d::PlayerHands; CharactersPlayer.cpp)
    // The shared hand meshes (left: the mirrored set); pose in chr::HandPose order: 0 Rest, 1 Grip, 2 Fist, 3 Open,
    // 4 Pinch, 5 Cig, 6 Point, 7 Hold. Null before init.
    const Mesh* handMesh(bool left, int pose) const;
    // The player's tea glass: its base centre when it stands on its saucer (world) and how full it is (0..1); false
    // while the çaycı has it. glassHeight: the ince belli's rim above its base.
    bool playerGlass(Vector3& base, float& level) const;
    float glassHeight() const;
    // The player's hand holds the glass at `world` (glass-local -> world: the base at the origin, axis +Y), called
    // every frame it holds it; nullptr puts it back on its saucer (GlassSet). False: it can't be taken now.
    bool holdPlayerGlass(const Matrix* world);
    void sipPlayerGlass();                 // a sip: the level drops (the TeaSip sound is the caller's)
    void setPlayerGlassStyle(int style);   // 0 klasik ince belli, 1 yeşil, 2 mavi, 3 mor

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace r3d
