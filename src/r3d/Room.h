#pragma once
// The kıraathane interior in 3D: architecture, furniture, background tables & chairs, the tea counter
// (ocak) with çaydanlık, TV with football, clock, pictures, windows/street, lamps (the lights), ceiling fan,
// stove, coat rack, chalkboards (prices + the SCOREBOARD), the shared ashtray, and the room atmosphere
// (fog/haze settings, ambient smoke, dust in light shafts), the weather and passers-by outside the windows and
// the kahvehane cat. PUBLIC API FROZEN (additions only).
// Implementation: src/r3d/Room*.cpp — room owner (struct Room::Impl).
#include "r3d/Gfx.h"
#include "ui/Audio.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace r3d {

class Room {
public:
    Room();
    ~Room();
    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    bool init(Renderer& r, uint64_t seed);  // build meshes, textures, canvases (after InitWindow)
    void shutdown(Renderer& r);
    void update(float dt);                  // clock, TV match, fan, lamp flicker, street, smoke emitters
    // Every frame before Renderer::render: sets ambient/fog/key light, adds the room's point lights and
    // submits all room geometry, glows and smoke. Must NOT touch the okey table area (Table3D owns it)
    // except the ashtray at w3d::ASHTRAY_POS.
    void submit(Renderer& r);

    // Chalked on the wall scoreboard (w3d::SCOREBOARD_POS): a title line (e.g. "El 2 / 5") and one line per
    // player (e.g. "Hacı Rıza ....... 143"). Redraw only when the content changes.
    void setScoreboard(const std::string& title, const std::vector<std::string>& lines);
    void setTitleMode(bool on);             // menu backdrop (may dim/stage lighting differently)
    // Tavla is played at its own table (w3d::TAVLA_TABLE): its pendant becomes the key light (shadows on the board),
    // our okey table's lamp a plain pendant.
    void setTavlaFocus(bool on);
    bool consumeTvGoal();                   // true once after a goal on the TV (banter hook)
    // Rain outside tonight: 0 on a dry night, else 0..1 and drifting slowly (App feeds it to Audio::setRain).
    float rainAmount() const;
    // True once after the cat meowed; `where` = about its head (people at the tables glance over).
    bool consumeCatMeow(Vector3& where);

    // Time of day and season (ui::Settings::dayTime / season: 0 otomatik = the computer's clock / date, re-checked
    // every 20 s; dayTime 1 sabah, 2 öğle, 3 akşam, 4 gece; season 1 ilkbahar, 2 yaz, 3 sonbahar, 4 kış). Cheap:
    // call every frame or on change. Daylight through the windows (sun shafts in the morning, golden light in the
    // evening, the lamps over empty tables off by day), the street outside by day or night, rain (mostly autumn /
    // winter nights; snow in winter, rainAmount() stays 0 for snow), the lit soba in winter, the fan in summer.
    void setTimeOfDay(int mode);
    void setSeason(int mode);
    int dayPhase() const;  // resolved: w3d::DayPhase (0 sabah, 1 öğle, 2 akşam, 3 gece), see r3d/Daytime.h
    int season() const;    // resolved: w3d::Season (0 ilkbahar, 1 yaz, 2 sonbahar, 3 kış)
    float snowAmount() const;  // 0..1 snowfall outside (winter)
    std::function<void(ui::Sfx)> playSfx;   // occasional room sounds (e.g. Chair, Dice, GlassSet)

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace r3d
