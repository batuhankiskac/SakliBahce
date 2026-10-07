#pragma once
// The player's own hands and forearms, seen from the seat camera at the bottom of the view (plan item 6).
//
// They rest on the table edge, reach for what the tables say the player is moving (HandCue: a tile drawn, discarded or
// dragged, a card played, the dice thrown, a checker moved), hold the card fan in the left hand, and now and then lift
// the tea glass (the player's own glass belongs to Characters: holdPlayerGlass / sipPlayerGlass), flick the tespih or
// draw on a cigarette. They sink out of view while a screen covers the table and keep clear of the tiles and cards the
// player needs to see (they rest beside the istaka and come in only to act). The arms use the regulars' tools
// (CharactersInternal.h: Hermite key tracks, two-bone IK, the hand meshes and poses).
// Implementation: src/r3d/PlayerHands.cpp.
#include "r3d/Gfx.h"
#include "r3d/HandCue.h"
#include "ui/Audio.h"

#include <cstdint>
#include <functional>

namespace r3d {

class Characters;

// Ayarlar → "Sen" (settings keys in parentheses).
struct PlayerLook {
    int kol = 0;          // (kol) sleeves: 0 ceket, 1 gömlek (rolled up), 2 kazak
    int kolRenk = 0;      // (kolrenk) 0 lacivert, 1 kahverengi, 2 gri, 3 bordo, 4 krem
    int ten = 1;          // (ten) skin: 0 açık, 1 buğday, 2 esmer, 3 koyu
    bool yuzuk = false;   // (yuzuk) a gold ring on the right hand
    bool saat = true;     // (saat) a watch on the left wrist
    int tespih = 0;       // (tespih) 0 none, 1 kehribar, 2 oltu (black), 3 yeşil, 4 mercan (red)
    int bardak = 0;       // (bardak) the own tea glass: 0 klasik ince belli, 1 yeşil, 2 mavi, 3 mor
    bool sigara = false;  // (sigara) a cigarette in the right hand
    bool operator==(const PlayerLook& o) const {
        return kol == o.kol && kolRenk == o.kolRenk && ten == o.ten && yuzuk == o.yuzuk && saat == o.saat &&
               tespih == o.tespih && bardak == o.bardak && sigara == o.sigara;
    }
    bool operator!=(const PlayerLook& o) const { return !(*this == o); }
};

class PlayerHands {
public:
    enum Scene { Hidden = 0, Okey = 1, Cards = 2, Tavla = 3 };

    PlayerHands();
    ~PlayerHands();
    PlayerHands(const PlayerHands&) = delete;
    PlayerHands& operator=(const PlayerHands&) = delete;

    // After Characters::init (the hand meshes and the player's glass are theirs).
    bool init(Renderer& r, Characters* people, uint64_t seed);
    void shutdown(Renderer& r);

    void setLook(const PlayerLook& look);  // rebuilds the sleeves when it changes (cheap)
    void setEnabled(bool on);              // Ayarlar "Ellerimi göster"
    // Where we sit and what we play: the rest spots, the reach of the arms and which hand does what. A change drops
    // every running cue (the old table's `where` callbacks).
    void setScene(Scene s);
    void setLowered(bool lowered);         // a menu or a screen covers the table: the hands sink out of view
    void setMyTurn(bool mine);             // no tea or cigarette is started while the player has to act
    void setAnimationSpeed(float s);       // the tables' animation speed (reaches keep pace with the objects)
    void reset();                          // drop every cue (a table game is destroyed)

    // ---- the tables' hooks (HandCue.h)
    void cue(const HandCue& c);
    float lead(HandCueKind k) const;       // seconds at animation speed 1; 0 when the hands are not shown

    // `eye`: the player's camera (the mouth for the tea and the cigarette follows the head).
    void update(float dt, const Camera3D& eye);
    void submit(Renderer& r);              // only from the seat camera: there is no body to go with the arms
    bool shown() const;                    // drawn at all this frame
    std::function<void(ui::Sfx)> playSfx;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace r3d
