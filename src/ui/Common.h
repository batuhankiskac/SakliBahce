#pragma once
// Shared UI foundation: virtual canvas, fonts (Turkish glyphs), palette, easing, and the SCREEN LAYOUT
// CONTRACT that Scene and TableView both follow. PUBLIC API FROZEN (architect-owned, Common.cpp).
//
// Everything is drawn in a virtual 1600x900 canvas; App sets a Camera2D that letterboxes it into the
// window. Use ui::virtualMouse() for input — never raw GetMousePosition().
#include "ui/Utf8.h" // utf8Count

#include <raylib.h>
#include <string>
#include <vector>

namespace ui {

constexpr float VW = 1600.f;
constexpr float VH = 900.f;

// ---------------------------------------------------------------- fonts
enum class FontId {
    Ui,      // Trebuchet MS — general UI text
    UiBold,  // Trebuchet MS Bold — buttons, names
    Chalk,   // Chalkboard SE — chalkboard / price list / scoreboard on the wall
    Hand,    // Noteworthy — handwriting (score sheet "hesap kağıdı")
    Sign,    // Rockwell — signage, titles ("SAKLI BAHÇE")
    Tile,    // Arial Black — numbers on tiles
    Count
};
void loadFonts();   // call after InitWindow; falls back to raylib's default font if a file is missing
void unloadFonts();
const Font& font(FontId id);
// UTF-8 text helpers (Turkish characters supported). `size` is the pixel height in virtual units.
Vector2 measureText(FontId f, const std::string& s, float size, float spacing = 0.f);
void drawText(FontId f, const std::string& s, Vector2 pos, float size, Color c, float spacing = 0.f);
void drawTextCentered(FontId f, const std::string& s, Vector2 center, float size, Color c,
                      float spacing = 0.f);
void drawTextShadow(FontId f, const std::string& s, Vector2 pos, float size, Color c,
                    Color shadow = Color{0, 0, 0, 160}, float offset = 2.f, float spacing = 0.f);
// Word-wrapped text inside `box` (left aligned). Returns the used height.
float drawTextWrapped(FontId f, const std::string& s, Rectangle box, float size, Color c,
                      float lineGap = 4.f);
// The height drawTextWrapped would return for `s` in a box `width` wide at y = originY, without drawing.
float measureWrapped(FontId f, const std::string& s, float width, float size, float lineGap = 4.f, float originY = 0.f);
// The lines drawTextWrapped draws (cached by font, size, width and text; the reference stays valid until the next
// call to this function, so copy it if you keep it).
const std::vector<std::string>& wrapLines(FontId f, const std::string& s, float width, float size);

// ---------------------------------------------------------------- virtual canvas
struct Viewport {
    float scale = 1.f;
    Vector2 offset{0, 0}; // screen-space offset of the virtual canvas' top-left
};
Viewport computeViewport();                  // from GetScreenWidth/GetScreenHeight
Camera2D viewportCamera(const Viewport& vp); // BeginMode2D(viewportCamera(vp)) draws in virtual units
Vector2 virtualMouse(const Viewport& vp);    // mouse in virtual coordinates
void setCurrentViewport(const Viewport& vp); // App sets it each frame...
const Viewport& currentViewport();           // ...so modules can query it
Vector2 virtualMouse();                      // == virtualMouse(currentViewport())

// ---------------------------------------------------------------- palette
namespace pal {
constexpr Color Felt{30, 88, 56, 255};        // worn green çuha
constexpr Color FeltDark{17, 58, 36, 255};
constexpr Color FeltLight{44, 112, 72, 255};
constexpr Color Wood{112, 66, 36, 255};
constexpr Color WoodDark{68, 38, 20, 255};
constexpr Color WoodLight{156, 102, 60, 255};
constexpr Color RackWood{132, 84, 46, 255};   // istaka
constexpr Color TileFace{246, 238, 216, 255}; // ivory
constexpr Color TileFaceShade{226, 214, 186, 255};
constexpr Color TileEdge{186, 170, 138, 255};
constexpr Color TileBack{228, 218, 192, 255};
constexpr Color InkYellow{222, 146, 12, 255};  // "sarı" on okey tiles reads orange-yellow
constexpr Color InkBlue{28, 86, 186, 255};
constexpr Color InkBlack{28, 28, 30, 255};
constexpr Color InkRed{200, 28, 38, 255};
constexpr Color Brass{214, 176, 96, 255};
constexpr Color Chalk{236, 234, 222, 255};
constexpr Color ChalkBoard{38, 52, 44, 255};
constexpr Color Paper{244, 236, 210, 255};
constexpr Color PencilGray{70, 70, 78, 255};
constexpr Color Tea{168, 58, 18, 255};
constexpr Color Oralet{242, 124, 28, 255};
constexpr Color Wall{150, 118, 76, 255};       // nicotine-yellowed wall
constexpr Color WallDark{96, 72, 44, 255};
constexpr Color TextLight{246, 236, 214, 255};
constexpr Color TextDark{42, 28, 18, 255};
constexpr Color Highlight{255, 214, 110, 255}; // selection / active-turn glow
constexpr Color Good{110, 200, 110, 255};
constexpr Color Bad{230, 80, 70, 255};
constexpr Color Smoke{205, 200, 190, 255};
} // namespace pal
Color tileInk(int tileColor);                 // pal::InkYellow.. for okey::TileColor
Color withAlpha(Color c, float alpha01);
Color lerpColor(Color a, Color b, float t);

// ---------------------------------------------------------------- shared widgets (consistent look)
// Immediate-mode button. Returns true on the frame the left mouse button is released over it (and it
// was pressed over it). Draws hover/pressed/disabled states. Wood = carved wooden plaque with brass
// text (in-game HUD, menus); Chalk = chalk writing on a small board; Paper = ink on paper (score sheet).
enum class ButtonStyle { Wood, Chalk, Paper };
bool drawButton(Rectangle r, const std::string& label, Vector2 mouse, bool enabled = true,
                ButtonStyle style = ButtonStyle::Wood, float fontSize = 24.f);
// Panels: Wood = framed wooden board, Paper = cream sheet with a soft shadow, Dark = translucent dark
// backdrop card. Draw content on top yourself.
enum class PanelStyle { Wood, Paper, Dark };
void drawPanel(Rectangle r, PanelStyle style);
// Full-screen dim layer for modal overlays (0..1).
void drawDim(float amount01);
// App calls uiBeginFrame() before drawing and uiEndFrame() after; widgets (and any module, via
// requestHandCursor) ask for the pointing-hand cursor, which is applied once per frame.
void uiBeginFrame();
void uiEndFrame();
void requestHandCursor();

// ---------------------------------------------------------------- accessibility (Ayarlar: Büyük yazı, Renk körü modu)
// HUD text scale: 1 normal, 1.25 "Büyük yazı". The table HUDs (Table3D, GameHud) and the speech bubbles read it.
void setHudTextScale(float s);
float hudTextScale();
// Colour-blind mode: tileInk() and the highlights use deuteranopia/protanopia-safe colours and shapes. The tile and
// card textures follow tilegfx::setColorBlind / cardgfx::setFourColour.
void setColorBlind(bool on);
bool colorBlind();

// ---------------------------------------------------------------- keyboard navigation
// "The keyboard was used last": shows key-help strips and focus rings. A module that acts on a navigation key calls
// noteKeyboardNav(); updateInputMode(mouse) (each frame, by the games' update) ends it when the mouse moves or clicks.
bool keyboardNav();
void noteKeyboardNav();
void updateInputMode(Vector2 mouse);
// IsKeyPressed, or held long enough to repeat (arrows).
bool keyPressedRepeat(int key);
bool shiftDown();

// ---------------------------------------------------------------- math helpers
float clamp01(float t);
float easeOutCubic(float t);
float easeInOutCubic(float t);
float easeOutBack(float t);
float approach(float current, float target, float speed, float dt); // exponential smoothing
bool pointInRect(Vector2 p, Rectangle r);

// ---------------------------------------------------------------- layout contract (virtual units)
// Scene draws the room/table/props/characters at these places; TableView draws game objects and must
// keep the PROP circles clear. Seats: 0 human (bottom), 1 right, 2 top, 3 left.
namespace layout {
constexpr Rectangle WALL{0, 0, 1600, 190};           // back wall visible above the table
constexpr Rectangle TABLE_RIM{130, 175, 1340, 760};  // wooden table edge (extends below the screen)
constexpr Rectangle FELT{155, 200, 1290, 735};       // green cloth inside the rim
constexpr Rectangle HUMAN_RACK{392, 728, 816, 168};  // human istaka (2 rows x 16 slots)
constexpr Rectangle BOT_RACK[4] = {                  // backs of the bots' istakas
    {0, 0, 0, 0}, {1392, 340, 44, 330}, {600, 205, 400, 44}, {164, 340, 44, 330}};
constexpr Vector2 DISCARD[4] = {                     // centre of each seat's discard pile
    {1290, 800}, {1340, 290}, {260, 290}, {310, 800}};
constexpr Vector2 PILE{740, 470};                    // face-down draw pile (ortadaki taşlar)
constexpr Vector2 INDICATOR{860, 470};               // gösterge
constexpr Rectangle MELD_ZONE[4] = {                 // where each seat's opened melds are laid
    {420, 575, 760, 140}, {1030, 410, 340, 150}, {420, 262, 760, 140}, {230, 410, 340, 150}};
constexpr Vector2 PROP_TEA[4] = {                    // each seat's tea glass (seat 3 drinks oralet)
    {208, 800}, {1388, 706}, {1080, 226}, {212, 706}};
constexpr Vector2 PROP_ASHTRAY{1395, 800};
constexpr float PROP_TEA_RADIUS = 30.f;              // keep-out radius around PROP_TEA
constexpr float PROP_ASHTRAY_RADIUS = 36.f;
constexpr Vector2 CHARACTER[4] = {                   // head centre of each seated character
    {800, 1000} /* human: not drawn */, {1535, 470}, {800, 112}, {65, 470}};
constexpr Vector2 BUBBLE[4] = {                      // speech-bubble anchor (tail points to character)
    {800, 690}, {1420, 330}, {930, 60}, {180, 330}};
constexpr Vector2 NAMEPLATE[4] = {                   // name + score plate centres (HUD)
    {65, 600}, {1535, 372}, {660, 160}, {65, 372}};
constexpr Rectangle CHALKBOARD{30, 18, 330, 160};    // wall chalkboard: Scene draws frame+board,
                                                     // TableView writes scores in chalk on it
constexpr Rectangle BUTTON_COLUMN{1478, 560, 116, 330}; // right floor strip: HUD buttons
constexpr Rectangle LEFT_STRIP{6, 640, 118, 250};    // left floor strip below the human nameplate: HUD info
} // namespace layout

} // namespace ui
