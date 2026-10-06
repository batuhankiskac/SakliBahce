# SaklıBahçe — 3D Design Contract (supersedes DESIGN.md §1 ownership of Scene/TableView and §5 visuals)

The user's words: *"ortamın 3D olmasını istiyorum — gerçekten o ortamdaymış gibi hissetmek istiyorum"*.
The player must feel **seated at a table in a real, smoky Turkish kıraathane at night**: first-person,
looking down at their own istaka, opponents across and beside them, the room alive around them.
DESIGN.md still defines the RULES (§2–§4), engine semantics, bots, audio, text/widgets and build
conventions. The old 2D Scene/TableView were retired and removed; their logic was ported into the 3D modules.

## 1. Modules and ownership (3D)

| Path | Owner | Notes |
|---|---|---|
| `src/r3d/World.h` | architect | FROZEN world coordinate contract — read it first |
| `src/r3d/Gfx.h` / `Gfx*.cpp` | gfx | renderer (lit shader, shadows, fog/haze, particles, glows), mesh builder, procedural textures |
| `src/r3d/Room.h` / `Room*.cpp` | room | the interior + lights + atmosphere + scoreboard + ashtray |
| `src/r3d/Characters.h` / `Characters*.cpp` | characters | opponents, patrons, çaycı, personal props, bubbles (uses `ui::Banter`) |
| `src/r3d/Table3D.h` / `Table3D*.cpp`, `PlayerCamera.cpp` | table | table, racks, tiles, interaction, HUD, player camera |
| `src/ui/TileRender.*` | table | tile face atlas (2D, reused for 3D tile textures) |
| `src/ui/Banter.*` | characters | Turkish banter logic (already written; may extend) |
| `src/ui/Screens.*`, `src/ui/Audio.*`, `src/ui/Common.*` | done | menus/score sheet, procedural audio, fonts/widgets (only small fixes) |
| `src/core/*` | engine/AI | done (AI tuning continues in Bot.cpp) |
| `src/app/*`, `Makefile`, `README.md` | integration | main loop, flow, flags |

Headers marked FROZEN keep their public API. You own your `.cpp` files and your `Impl`. You may create
extra files named with your module prefix (e.g. `src/r3d/RoomProps.cpp`, `src/r3d/CharactersAnim.cpp`,
`src/r3d/Table3DHud.cpp`).

## 2. Rendering model

* Units metres, Y up, table centre at the origin, human at +Z looking toward −Z (see `World.h`).
* Every frame: `room.submit(r); characters.submit(r); table.submit(r); r.render(cam);` then the 2D layer:
  `BeginMode2D(ui::viewportCamera(vp))`, `characters.drawOverlay(r)`, `table.drawHUD(r)`, `screens.draw()`.
* All lit geometry uses `Renderer::makeMat` materials. Use `Transparent` for glass, `Additive` for light
  shafts, `emitSmoke` for smoke/steam, `submitGlow` for bulbs/embers/TV glow. Point lights ≤ 12 total:
  Room owns the budget (≤ 9), Characters may add ≤ 2 (cigarette ember, çaycı stove?), Table3D ≤ 1.
* Canvas textures (`makeCanvas` + `genCanvasQuad`) for anything with text/pictures on it: chalkboards,
  TV screen, clock face, pictures, signs.
* Style: **stylised-realistic, warm and moody** — think a cosy low-poly diorama lit like a film still:
  soft shadows, warm tungsten pools of light, dark smoky corners, blue night outside the windows.
  Smooth-shaded primitives, bevelled edges (rounded boxes), subtle procedural textures. Nothing flat or
  default-grey. No text rendered in 3D except through canvases.
* Performance budget (Apple Silicon, 1600×900, MSAA 4×): whole frame ≤ 8 ms. Room ≤ 250 draw submissions,
  Characters ≤ 250, Table3D ≤ 200 (tiles may be batched). Build meshes once at init; per-frame work is
  transforms only.

## 3. The room (Room)

A real corner kahvehane at night. Room 8.4 × 7 m, ceiling 3.1 m (`World.h`). Must include:
* Walls: lower wooden wainscot (~1 m), nicotine-yellowed plaster/wallpaper above, stains, a few cracks;
  a mosaic/terrazzo or worn tile floor (or old wooden boards), skirting; ceiling with beams or panels.
* Back wall (behind seat 2, the default view's backdrop): the SCOREBOARD chalkboard at
  `SCOREBOARD_POS` (chalk text via `setScoreboard`), a price board ("Çay 15 TL · Oralet 15 TL · Kahve
  40 TL · Soda 20 TL"), framed pictures (Bosphorus/Galata silhouette, an old football team photo, a
  pennant), a wall clock showing real local time, a calendar, a mirror with a gilded frame.
* The tea counter (ocak) at `COUNTER_POS`: counter with tiled front, a double teapot (çaydanlık) on a
  gas ring with a blue flame glow and steam, rows of ince belli glasses on saucers, sugar bowls, a
  samovar-ish urn, shelves with soda bottles, a small radio (the music source).
* An old CRT TV high on a wall bracket showing a football match (animated canvas: pitch, players as dots,
  scoreline, flicker, scanlines) and casting a flickering bluish light (a point light).
* Windows onto the night street: dark blue glass with condensation, street lamp glow, a shop sign across
  the street, occasional passing car light sweeping across the ceiling; a door with a bead/plastic
  strip curtain; a coat rack with caps and coats; a cast-iron stove (soba) with pipe and a warm glow.
* Background tables per `BG_TABLES` (0.9 m tables, 4 chairs each incl. empty ones, their own props:
  tavla board, okey tiles, playing cards, tea glasses, ashtrays).
* Lighting: pendant lamp over our table = the key light (`TABLE_LAMP`, shadows) with a conical enamel
  shade and a visible bulb glow; other pendants over the background tables and the counter (point
  lights, warm, gentle random flicker); cool window light; TV light. The room should be dim with warm
  pools — our table is the brightest area.
* Atmosphere ("dumanaltı"): set `Fog` so the air is visibly smoky (distance fog + a thick height haze
  under the ceiling), slow drifting smoke puffs around the room and in the lamp cones, dust motes in
  light shafts (additive cones under the lamps), smoke rising from the ashtray at `ASHTRAY_POS`.
  Keep our table readable (little haze below 1.4 m near the table).
* Weather & street life (`RoomWeather.cpp`): about half the nights it rains (from the room seed; the intensity
  drifts over minutes and `Room::rainAmount()` feeds `Audio::setRain`). Drops bead on the outside of the window
  glass, merge, run down and leave clear trails (a double-buffered canvas, one slot per pane, redrawn at 12 Hz:
  every render-to-texture pass makes the driver wait for the GPU, so keep canvas redraws rare); rain streaks
  fall outside the windows and the door (upright additive billboards, brighter near the street lamps).
  Passers-by walk the pavements of both streets (painted silhouettes on upright billboards, four walk frames,
  mirrored by their screen direction): umbrellas and caps glide past above the café curtains; on rainy nights
  umbrellas or a newspaper held over the head, on dry nights caps, a cane, a cigarette. The passing car's
  tyres are heard (`Sfx::CarPass`, wet or dry) peaking as its headlights sweep past our seat.
* The kahvehane cat (`RoomCat.cpp`, Room-owned): a procedural rig (two torso ellipsoids that pitch and bend, a
  waist, neck, head with ears/muzzle/eyes, two-bone IK legs with paws, an 8-segment tail that sways free or
  wraps round the body) posed by target poses the current pose eases toward (curl, loaf, sit, groom, bow,
  stand), a lateral-sequence walk, jumps onto/off an empty chair (the chair-scrape animation leaves that chair
  alone). Coat from the seed (ginger tabby / grey tabby / tuxedo). It naps by the stove or on an empty chair,
  sits under the scoreboard (its eyes glint above the table edge from our seat), under the windows, or at the
  street door watching the rain; now and then it looks at the camera; very rarely `Sfx::Meow`. Walks are
  planned on a 10 cm occupancy grid of the floor (A* + string pulling) that keeps 1.35 m off our table and clear
  of every table, chair, the counter, stove, coat rack and bench. ~25 draw submissions, no point light.
* Title mode: same room; App drives a slow cinematic camera.

## 4. People (Characters)

* Opponents (fixed by seat, see DESIGN.md "Characters"): **Hacı Rıza** (seat 1, right): flat cap
  (kasket), grey mustache, vest over a shirt, swinging tespih; **Kel Mahmut** (seat 2, across): bald,
  thick black mustache, big build, striped shirt, cigarette (ember glow + smoke); **Emekli Nuri**
  (seat 3, left): white hair, glasses, cardigan, drinks oralet. Seated on chairs at `seatPos(s)` facing
  the table, visible from the human seat (heads/shoulders/arms/hands on the table edge behind their
  racks). Stylised but characterful faces: eyes that blink and look at things (the active player, the
  speaker, the human's camera now and then), eyebrows, noses, mustaches, ears, mouths that move while
  their bubble is showing.
* Animations tied to events. Hand and tile share one timeline (`w3d::BOT_*_LEAD` in World.h): Table3D holds
  an opponent's tile flight back until the fingers are on the tile (taken tiles lift off at
  `BOT_TAKE_LEAD`, given ones leave the rack at `BOT_GIVE_LEAD` / `BOT_MELD_LEAD`), Characters times the
  reach to it (and follows Table3D's animation speed), App delays the tile sounds by the same lead.
  Arm tracks are C1 curves through their keys (cubic Hermite: the hand passes through intermediate keys,
  stops only at turning points, `ease` 1 keys and the end, hits `ease` 2 keys at speed) and start with the
  hand's current velocity, so a new track never jerks the arm; the body leans in ahead of a long reach,
  targets out of reach even leaning in are clamped (the hand lets the tile slide on), the elbow's swing
  around the shoulder-wrist line is rate limited (no flips at the lips), and spans over the owner's istaka
  are arched clear of it (`Key::touch` marks keys that are on the rack on purpose).
  DrawPile/TakeLeft → reach to `PILE_POS` / the left discard pile and back to the rack; Discard →
  reach to their `DISCARD_POS`; Open/LayMelds → both hands slap tiles down in their `MELD_ZONE` (+ a
  proud gesture); AddToMeld/SwapJoker → reach into the target zone; Penalty → facepalm/shrug;
  HandEnd → winner celebrates (arms up / fist on table), others grumble; idle → breathing, looking at
  their own rack while it's their turn (thinking pose: hand on chin), sipping tea (glass travels to the
  mouth and back, level drops), smoking (cigarette to the lips, exhale puff), tespih swinging.
* Card games (`CharactersCards.cpp`): `holdCards` puts the hand fanned into the left hand (an optional pick-up off
  the felt first; `Opponent::cards`, `fanRel` = the fan's frame in the hand's space, so `cardFan` follows the real
  hand; `updateCardHold` moves the hand half way along with the body's lean so a thinking head never comes down onto
  the cards). While it holds them `startTrack` refuses every other track on that arm (no sips, gestures, tespih
  flips; Rıza's tespih hangs from the card hand) and `reach` uses the right hand. `playCard` (the right hand pinches
  a card's top edge from above, draws it up and lays or tosses it), `gatherCards` (palm down on a trick, pushed to
  the pile), `shuffleDeck` / `dealCards` (the dealer's riffle; the left hand holds the deck while the right pushes
  each card off) — all `TK_Reach` / `TK_CardHold` tracks at the table's animation speed.
* Personal props on the table at `GLASS_POS[s]` (ince belli glass on a saucer with tea — translucent
  amber, spoon; seat 3 has an orange oralet), including the human's glass at `GLASS_POS[0]`.
* Background patrons (8–12) at `BG_TABLES` seats doing their activity (tavla dice throws, card slaps,
  chatting with hand gestures, reading a newspaper, drinking tea), lower detail but not lifeless.
* The çaycı: young man with a white shirt/apron and a hanging three-arm tea tray (askılı tepsi); walks
  from `COUNTER_POS` around the room (avoid walking through tables/chairs), serves our table
  periodically (refills glasses; Sfx::GlassSet/TeaClink), sometimes other tables.
* Speech bubbles: `ui::Banter` for the lines; bubbles drawn in 2D above the speaker's projected head
  (Ui font, word-wrapped, fade in/out, tail pointing at the head, clamped on-screen, never overlapping).

## 5. The table (Table3D + PlayerCamera)

* Table: square wooden okey table, green felt (`FELT_HALF`), raised wooden rim (`RIM_W`, `RIM_H`), apron
  and legs. Racks (istaka): wooden two-tier racks per seat at `RACK_DIST`, `RACK_LEN`; the human's rack
  tilts tiles toward the camera so they are easy to read; the bots' racks show tile backs (count visible).
* Tiles: rounded ivory boxes `TILE_W × TILE_H × TILE_T`, faces from `ui::tilegfx` (atlas → UVs per face),
  crisp and readable from the seat. Okey tiles on the human's rack get the star badge.
* All 106 tiles are always somewhere: pile (neat face-down stack), indicator (face-up, slightly raised on
  a little stand), racks, discard piles (top face-up, tidy stack), melds (flat rows in `MELD_ZONE[owner]`,
  auto-scaled if crowded, readable from the human seat), or in flight (arc, rotation, landing clack).
  Derive every tile's target transform from the game state every frame and tween toward it.
* Interaction (mouse ray picking): hover lift + outline; drag tiles along the rack between slots; draw
  by clicking/dragging the pile or the top of the left pile (`DISCARD_POS[3]`); discard by dragging onto
  `DISCARD_POS[0]` or double-click; işle by dragging onto a meld (left/right half = front/back, onto a
  joker = swap); rack groups (contiguous tiles in a row) drive "El Aç"/"Per Aç" with live hints; "Seri
  Diz"/"Çift Diz"; "Geri Ver"; confirm modal for işlek/okey discards; Turkish toasts for engine errors.
  (Rack slots, groups, modal, toasts and buttons were ported from the retired 2D TableView.)
* HUD (2D): buttons on the right edge (ui::drawButton Wood), a status line at the bottom centre above the
  rack area, live counter, toasts, name plates projected above each opponent's head (name, total score,
  "Açtı: 124"/"Çift" badge, glowing on their turn), pile count near the pile (projected). Keep it light:
  the 3D scene is the star. Scores also appear on the wall scoreboard (App feeds Room).
* Yapay Zeka mode (`setAiMode`): App lets an AI play the human's seat. The HUD's "Yapay Zeka" button (between
  "Çift Diz" and "Menü"; `consumeAiToggleRequest`) is lit, the status line carries a "YAPAY ZEKA" tag, the
  human's tiles ignore the mouse (App passes `humanInput=false` but the real mouse, for the buttons, peeks and
  mouse-look) and the istaka is re-arranged (series or pairs, whichever the AI is going for) whenever tiles
  come in or leave.
* PlayerCamera: seated first-person (`EYE`, `EYE_PITCH_DEG`, `FOVY_DEG`), RMB-drag look-around, wheel zoom,
  R/double-RMB recentre, subtle breathing; title mode = slow cinematic drift through the room.

## 6. Verification (everyone)

* The Mac's screen may be LOCKED/asleep while you work. raylib then fails with "Failed to determine
  Monitor". Wake the display right before launching any raylib program in the SAME command:
  `(caffeinate -u -t 3 &); sleep 1; ./build/<you>/harness` — this works while locked.
* Snapshot harnesses: hidden window, `RenderTexture2D` 1600×900, `renderer.setRenderSize(1600, 900)`,
  `ui::setCurrentViewport(ui::Viewport{})`, render with `renderer.render(cam, &rt, clearColor)`, draw 2D
  overlays with `BeginTextureMode(rt)` afterwards, export PNG (flip vertically), look at it with Read.
  Render from the human's seat AND from a few other angles (e.g. behind seat 2 looking back, from a room
  corner) to judge the space. Iterate visually several rounds.
* Compile flags: `-std=c++17 -O2 -Wall -Wextra -Isrc -isystem /opt/homebrew/include` (isystem silences
  raymath warnings). Link as in DESIGN.md §6. Build into `build/<owner>/`. Never run top-level `make`
  until integration.
