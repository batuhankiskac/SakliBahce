# SaklıBahçe — Design Contract

> **3D update:** the game is now fully 3D and first-person. `DESIGN3D.md` supersedes this file's module
> ownership for Scene/TableView and all of §5 (visuals/layout). Rules, engine, bots, audio, text and build
> conventions below still apply.

A desktop C++17 game (raylib 6.0, macOS) where the player plays **101 Okey** against three bots in a
smoky Turkish coffeehouse (kıraathane): nicotine-yellow walls, green baize (çuha) table, ince belli tea
glasses, oralet, an ashtray with a burning cigarette, a CRT TV showing football, the tea boy (çaycı)
walking by, prayer beads (tespih), and banter. All user-facing text is **Turkish**.

This file is the single source of truth. Headers marked *PUBLIC API FROZEN* must not change their public
API; implementers own their `.cpp` files and `struct X::Impl` (pimpl). If you think a frozen API is wrong,
work around it and note it in your final report — don't edit another owner's files.

## 1. Modules and ownership

| Path | Owner | Notes |
|---|---|---|
| `src/core/Tile.h`, `Rng.h` | architect | header-only, frozen |
| `src/core/Meld.h/.cpp` | engine | meld validation, işleme |
| `src/core/Game.h/.cpp` | engine | rules/turns/scoring state machine |
| `tests/test_engine.cpp` | engine | assertion tests |
| `src/core/Solver.h/.cpp` | AI | best meld partition |
| `src/core/Bot.h/.cpp` | AI | bots |
| `tests/test_ai.cpp`, `tests/sim.cpp` | AI | solver tests, headless bot-vs-bot simulation |
| `src/ui/Common.h/.cpp` | architect | fonts, viewport, palette, layout contract |
| `src/ui/Banter.*` | characters | banter lines (the 2D `Scene` was removed — see DESIGN3D.md) |
| `src/ui/TileRender.*` | table | tile face atlas (the 2D `TableView` was removed — see DESIGN3D.md) |
| `src/ui/Screens.*` | screens | menus, settings, rules, score sheet |
| `src/ui/Audio.*` | audio | procedural sound |
| `src/app/*` , `Makefile` | integration | game loop, bot pacing, wiring, CLI flags |
| `tools/*` | anyone | put your own snapshot harness in `tools/<owner>_*.cpp` |

Core (`src/core`) never includes raylib. UI modules talk to the engine only through `okey::Game`'s public
API (read-only except TableView, which performs the human's actions).

## 2. Rules of 101 Okey (implement exactly this)

**Tiles.** 106 tiles: 4 colors (Sarı, Mavi, Siyah, Kırmızı) × 1..13 × 2 copies, plus 2 *sahte okey*.
See `Tile.h` for ids.

**Indicator & okey.** After shuffling, draw tiles until a numbered tile appears: that is the *gösterge*
(indicator), removed from play and shown face up; skipped fake jokers go back into the deck (reshuffle
that tile in). The *okey* is the same color, number+1 (13 → 1). The two physical okey tiles are wild.
The two *sahte okey* tiles play as the okey's face value (not wild).

**Deal.** The hand's *starter* receives 22 tiles, everyone else 21. The rest (20 tiles) is the face-down
pile. The starter is random in hand 0, then moves to the right (`rightOf`) each hand. The starter begins
in `TurnStage::Play` (no draw).

**Turn.** Order 0→1→2→3→0. On your turn:
1. **Draw**: take the top of the pile, **or** take the top discard of your left neighbour (`leftOf(seat)`
   discard pile). A tile taken from the left must be used on the table in the same turn (in the
   opening if not yet opened, otherwise via layMelds/addToMeld/swapJoker). If you can't, you give it back
   (`returnLeftTile`): **+101 penalty**, the tile returns to the pile it came from, and you must draw from
   the pile (you can't take from the left again this turn).
2. **Play** (optional): open, lay further melds, add tiles to table melds (işlemek), swap jokers.
3. **Discard** one tile onto your own discard pile (it becomes takeable by your right neighbour).

**Melds.** Runs, groups, pairs — see `Meld.h`. A '1' may follow 13 (12‑13‑1, the 1 is worth 14), no
wrap-around.

**Opening (el açmak).** A player who hasn't opened may open by laying either
* series melds (runs/groups) whose total value ≥ 101, or
* at least 5 pairs (çift açmak).
Series and pairs can't be mixed in the opening. After opening:
* series openers may later lay more series melds, pair openers may later lay more pairs;
* both may add tiles to any run/group on the table (any owner) and swap jokers;
* with `waitTurnAfterOpening` (default on) none of this is allowed in the same turn as the opening;
  the opening itself may contain as many melds as you like.
A player must always keep ≥ 1 tile in hand after laying (to discard).

**İşleme.** Add a tile to either end of a run or as a missing color to a group (max 4). Pairs never
accept tiles.

**Joker swap (okey alma).** An opened player (subject to waitTurnAfterOpening) may replace a joker in
a run/group with the real tile it represents and take the joker into hand. In a 3-tile group any missing
color of that number may replace the joker.

**Penalties (+101 each, added to the hand score):**
* discarding the okey (unless it is your finishing discard),
* discarding a tile that fits a table run/group ("işlek taş") — only non-finishing discards; a joker
  discard only gets the okey penalty, never both,
* giving back a tile taken from the left.

**End of hand.**
* *Finish*: a player whose hand becomes empty by discarding their last tile wins the hand.
* *Pile exhausted*: if the pile is empty right after a discard (and nobody finished), the hand ends with
  no winner.

**Scoring (lower is better).** Multiplier `m` = 1, ×2 if the winner's last discard was the okey
(okeyle bitiş), ×2 if the winner had opened with pairs (çiftten bitiş), ×2 if the winner opened in their
finishing turn (elden bitiş). Hand scores:
* winner: `winnerScore × m` (−101, −202, …),
* never opened: `unopenedScore × m` (202, 404, …),
* opened with series: `handPoints × m`,
* opened with pairs: `handPoints × 2 × m`,
* plus penalties (not multiplied).
`handPoints` = sum of face numbers, okey = 101 (`OkeyInfo::handValue`).
Pile exhausted: m = 1, no winner, same formulas for everybody.
The match lasts `numHands` hands; lowest total wins.

## 3. Engine semantics (Game)

* All actions validate `seat == current()` and the stage; on failure return `ActionResult::fail` with a
  **Turkish** message and change nothing.
* Events are appended for every state change (see `EvType`), text in Turkish with player names, e.g.
  `"Hacı Rıza yerden taş çekti"`, `"Kel Mahmut eli açtı (124)"`; the human seat's own events are in the
  second person without a name (`"Okey attın: 101 ceza"`).
* `startMatch(seed)` → `MatchStart`, then deals hand 0 (`HandStart`, `TurnStart`). `discard` of the
  finishing tile → `Discard`, `HandEnd` (then `MatchEnd` if it was the last hand). `HandState` becomes
  `HandOver` or `MatchOver`; `startNextHand()` deals the next hand.
* `checkOpen` / `checkLay` never mutate; the UI calls them every frame for live counters.
* Determinism: the same seed and the same action sequence reproduce the same game.

## 4. Bots (AI)

Three levels: **Acemi** (Easy — plays simply, sometimes suboptimal, never illegal), **Usta** (Normal —
solid), **Kurt** (Hard — remembers discards, avoids feeding the next player, plans the opening, times
pair vs series openings, picks up left tiles to open, grabs jokers). Bots must be *fair* (see `Bot.h`).
Every bot turn must end with a legal discard; `sim` runs thousands of hands without a single rejected
action on Normal/Hard (rejections are counted and reported).

**Yapay Zeka mode** (App): a Kurt can take over the human's seat at any moment and hand it back at any
moment, mid-turn included (`Bot::next` rebuilds its plan from the public state, so it continues from whatever
stage the turn is in; App joins a decision still running on the worker thread before the player acts again).
`--ai-chaos` (hidden developer flag) flips the mode at random for hours of game time and reports stuck turns
and rejected actions.

## 5. Screen & interaction design (1600×900 virtual canvas)

Composition: a 3/4 view. The back wall occupies the top ~190 px (behind the top player); the table with
green baize fills the rest; the left/right players sit at the screen edges; the human's istaka is at the
bottom (the viewer sits at the table). `ui::layout` in `Common.h` fixes all shared coordinates:

* Discard piles sit at the table corners: seat 0 → bottom‑right, seat 1 → top‑right, seat 2 → top‑left,
  seat 3 → bottom‑left. The human takes from bottom‑left and discards to bottom‑right.
* Pile + indicator in the centre. Each seat's opened melds in `MELD_ZONE[seat]` (auto-scale down if they
  overflow). Props at `PROP_TEA` / `PROP_ASHTRAY` — TableView keeps these circles clear.
* HUD buttons live in `BUTTON_COLUMN` (right floor strip); name plates at `NAMEPLATE`; scores are chalked
  on the wall chalkboard (`CHALKBOARD`).

**Human controls.**
* Rack: 2 rows × 16 slots. Drag tiles between slots (free placement, gaps allowed — gaps separate groups,
  like a real istaka). Contiguous tiles in a row form a *group*.
* Draw: click/drag the pile, or click/drag the top of the left discard pile (bottom‑left).
* Discard: drag a tile onto your discard spot (bottom‑right), or double‑click a tile.
* **El Aç** (open) / **Per Aç** (lay more after opening): uses the rack groups. Valid series groups
  (≥3) and pairs (2) are highlighted live; a live counter shows "Seri: 87 / 101 · Çift: 3 / 5".
* İşle: drag a tile from the rack onto a table meld (drop on the left half = front, right half = back;
  dropping onto a joker inside the meld tries a joker swap).
* **Seri Diz** / **Çift Diz**: auto-arrange the rack (Solver::arrangeSeries / arrangePairs).
* **Geri Ver**: return a tile taken from the left (+101).
* Discarding an işlek tile or the okey asks for confirmation ("Bu taş işlek, 101 ceza! Yine de at?").
* The okey tiles get a small star badge on the human's rack (as most okey apps do).

**Tiles.** Ivory rounded rectangles with a subtle bevel/shadow; big bold number in the tile color;
a small filled circle under the number; sahte okey = a four-leaf clover/star emblem drawn with shapes.
Backs are plain ivory with a small emblem. Bots' racks show backs only (with a tile count).

**Atmosphere (Scene).** Warm dim light from hanging bulbs (gentle flicker), nicotine-stained wallpaper,
wall clock showing real time, framed picture(s), a price chalkboard ("Çay 15 TL · Oralet 15 TL · Kahve 40 TL ·
Soda 20 TL"), an old CRT TV with a tiny football match, a window with street light at night, ceiling fan,
background patrons (silhouettes playing tavla/cards in the haze), layered cigarette smoke drifting from
the ashtray and characters, and a thick haze under the ceiling ("dumanaltı"). Characters (seats 1..3):
stylised older men — flat caps (kasket), mustaches, vests/cardigans, one with a tespih swinging —
each with idle animations (breathing, blinking, sipping tea, smoking). The çaycı occasionally walks by
with a tray and refills glasses. Banter lines are friendly and funny (no slurs/profanity):
"Çaycı! Bir çay daha!", "Okey kimde lan?"-style lines toned down to "Okey kimde ya?", "Hadi be abi,
uyuma!", "Al sana 124!", "Ceza yazın ceza!", "Taşlar bitiyor beyler…", "Fener yine kaybetmiş…".

**Audio.** Fully procedural: tile clicks/clacks, shuffle rattle, tea spoon clinks, crowd murmur,
distant tavla dice, ceiling fan hum, faint TV crowd, and an old radio playing synthesized plucked
bağlama-like melodies in makam scales (Hicaz/Uşşak) through a lo-fi filter.

**Characters (fixed by seat).** App names them; Scene picks looks/personality by seat:
* seat 1 (right) **Hacı Rıza** — flat cap (kasket), grey mustache, vest, swinging tespih; calm, proverbs
  ("Sabreden derviş muradına ermiş.").
* seat 2 (top) **Kel Mahmut** — bald, thick black mustache, big build, striped shirt, smokes; loud,
  football fan, brags ("Bu el benim abi!").
* seat 3 (left) **Emekli Nuri** — white hair, glasses, cardigan, drinks oralet; grumpy nostalgic retiree
  ("Bizim zamanımızda böyle oynanmazdı…").
* seat 0 is the human (default name "Sen").

**Text & widgets.** Only the Sign and Hand fonts have the ₺ glyph — write "TL" elsewhere. Use the shared
`ui::drawButton` / `ui::drawPanel` / `ui::drawDim` from `Common.h` so every screen looks consistent.
App calls `ui::uiBeginFrame()` / `ui::uiEndFrame()` around each frame (cursor handling); call
`ui::requestHandCursor()` when hovering something clickable that isn't a `drawButton`.

## 6. Build & test conventions

* Toolchain: Apple clang, C++17, raylib 6.0 at `/opt/homebrew` (static lib `/opt/homebrew/lib/libraylib.a`).
* Compile flags: `-std=c++17 -O2 -Wall -Wextra -Isrc -I/opt/homebrew/include`.
* Link (UI binaries): `/opt/homebrew/lib/libraylib.a -framework Cocoa -framework IOKit -framework OpenGL
  -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation`.
* **While working in parallel, never run the top-level `make` and never write outside your own files.**
  Build into `build/<owner>/` with direct `clang++` commands (e.g. `build/scene/`), so parallel agents
  don't clobber each other's objects.
* Visual self-check: write a harness in `tools/<owner>_snapshot.cpp` that opens a hidden window
  (`SetConfigFlags(FLAG_WINDOW_HIDDEN)`), renders into a `RenderTexture2D` of 1600×900, then
  `LoadImageFromTexture` → `ImageFlipVertical` → `ExportImage("build/<owner>/shot.png")`, and look at
  the PNG with the Read tool. Iterate until it looks good. (Verified to work on this machine.)
* Warnings must be clean for your files. No exceptions for control flow; no global mutable state other
  than what's inside your module.
* Code style: 4-space indent, `camelCase` functions, `PascalCase` types, `trailing_` members, short
  comments only where they add information.
