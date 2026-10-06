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
   (`returnLeftTile`, free unless `penaltyReturnLeft`): the tile returns to the pile it came from, and you
   must draw from the pile (you can't take from the left again this turn). Opening with that tile charges
   the left neighbour its number x10 (x20 for a pair opening) when `leftOpenPenalty` is on.
2. **Play** (optional): open, lay further melds, add tiles to table melds (işlemek), swap jokers.
3. **Discard** one tile onto your own discard pile (it becomes takeable by your right neighbour).

**Melds.** Runs, groups, pairs — see `Meld.h`. A run ends at 13: unlike plain okey, 101 has no '1' after
13 (12‑13‑1 is not a run) and no wrap-around; a '1' only starts a run (1‑2‑3).

**Opening (el açmak).** A player who hasn't opened may open by laying either
* series melds (runs/groups) whose total value ≥ 101, or
* at least 5 pairs (çift açmak).
Series and pairs can't be mixed in the opening. **Katlamalı** (`RulesConfig::katlamali`, a setting): a series
opening needs one more than the highest series opening already on the table this hand (116 → 117), a pair
opening one pair more than the most pairs opened (5 → 6) — `Game::seriesOpenNeed` / `pairsOpenNeed`. After opening:
* pair openers may later lay only pairs; series openers may later lay series melds, and pairs too once
  another player has opened with pairs (Zynga/Digitoy rule);
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
(okeyle bitiş), ×2 if the winner had opened with pairs (çiftten bitiş), ×2 if nobody else had opened and the
winner opened in their finishing turn, laying the whole hand at once (elden bitiş). Hand scores:
* winner: `winnerScore × m` (−101, −202, …),
* never opened: `unopenedScore × m` (202, 404, …),
* opened with series: `handPoints × m`,
* opened with pairs: `handPoints × 2 × m`,
* plus penalties (not multiplied), including +101 for every okey left in an opened loser's hand.
`handPoints` = sum of the face numbers left in hand (okeys not counted).
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
With the yandan açma cezası on, Usta and Kurt weigh the expected penalty of every discard to an unopened right
neighbour (`Bot.cpp` `feedCost`: a logistic model of "they open with it", fitted on `sim --feed-log` data —
about 7% for a 1, 21% for a 12/13, almost never a face they threw away themselves); Acemi only a little.
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
* **Geri Ver**: return a tile taken from the left (free; `RulesConfig::penaltyReturnLeft` makes it +101).
* **Yandan açma cezası** (`RulesConfig::leftOpenPenalty`, Ayarlar toggle): opening with the tile taken from the
  left writes its number x10 (series opening) or x20 (pair opening) on the player who discarded it.
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
  Linux (the cloud, CI): clang or gcc, raylib 6.0 built from source into `/usr/local`; the Makefile picks
  `-lGL -lm -lpthread -ldl -lrt -lX11` by `uname`. `r3d/Gfx.cpp` takes the GL 3.3 prototypes from `GL/glext.h`;
  fonts fall back to DejaVu / Liberation; the save directory is `$XDG_DATA_HOME` or `~/.local/share`.
  Headless: `xvfb-run` (Mesa llvmpipe, ~0.8 s a frame for the whole room), `--render-last N` and
  `tables_check --no-3d` skip the 3D pass where nobody looks (`Renderer::discardFrame`). No audio device: silent.
  CI (`.github/workflows/ci.yml`): Linux `make test`, `tablescheck --no-3d`, two snapshots, `make asan`; macOS
  `make test`.
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

## 7. The other games (2026-10)

Seven games share the room, the people, the camera, the audio and the screens. `ui::GameKind` (Screens.h) names
them; `ui::Settings::game` is the one the next match plays (the title's "Oyna" opens the game list,
`ScreenId::GameSelect`).

* **The okey family** stays on `okey::Game` + `okey::Bot` + `r3d::Table3D`:
  * *101* is the default `RulesConfig`.
  * *Eşli 101* sets `RulesConfig::teams`: partners across, the finisher's partner writes no hand (written
    penalties stay), `teamTotal()` decides the match. The bots play for the team: in Kurt's rollouts the hand may end by an
    opponent (my hand plus the partner's expected hand count) or by the partner (mine is wiped), and a partner
    close to finishing is not treated as a threat (`Bot` Knowledge `team`, `roundHazard`; `sim --team-ab L`
    compares team bots against self-playing ones on duplicate deals).
  * *Klasik okey* sets `RulesConfig::variant = Variant::Okey`: 14 tiles (the starter 15), no table play, the
    left tile is simply taken, `finishHand()` puts the 15th tile down when the other 14 are runs/groups (12-13-1 is
    a run here) or seven pairs (`OkeyHand.{h,cpp}`: an exact cover search, plus a cached value-only count for the
    bots), `showIndicator()` for the gösterge's twin. Everyone starts with `okeyStartPoints` (20) and counts down
    (2 per finish, x2 okey / x2 pairs, 1 for the gösterge); the game ends when someone reaches 0. The bots
    (`Bot::classicNext`) keep the hand with the most tiles in melds; Kurt also counts the next draw's chances (its
    edge over Usta).
* **Tavla, Pişti, Batak, King** are `app::TableGame`s (`src/app/TableGame.h`), built by `makeTableGame()`. Each owns
  its engine and bots (`src/core/{Tavla,Pisti,Batak,King}*`, written to the same conventions as the okey engine:
  Turkish event texts, `ActionResult` errors, deterministic seeds, fair bots at three levels, sims with paired
  duplicate deals), its 3D pieces and a `r3d::GameHud`. App drives it (`startOtherMatch`, `updateFlow`) while
  `Table3D::setFurnitureOnly(true)` leaves only the table and its felt.
  * The card games derive from `app::CardTableBase` (CardTable.h): `r3d::Cards3D` (52 rounded cards textured from
    `ui::cardgfx`'s procedural atlas, each flying to a target pose; `cardlayout` gives hands, the trick, won piles,
    the deck, Pişti's middle), dealing, the trick held on the felt before it is swept, picking and highlighting the
    player's cards, bot pacing and `Characters::reach` / `react` / `chat` for the people.
  * Cards play like cards (2026-10): a deal is a little state machine (`CardTableBase::DealAnim`, game time,
    everything / `speed_`): the dealer riffles (`deckSlot`: the deck splits into two halves that fall back card by
    card) and cuts — Pişti turns the deck's bottom card up for a moment (`showCutCard`, visual only) —, the cards slide
    one by one round the table into a little face-down pile per seat (`cardlayout::dealtPile`; `dealtExtras` such as
    Pişti's table cards come last; the cards that stay in the deck lie under them, `deckRemaining`), then each seat
    picks its pile up (`pickAt` / `grabAt`). The regulars hold their hand fanned in the left hand
    (`Characters::holdCards` / `cardFan`, `cardlayout::fanCard`; `Cards3D::follow` keeps the cards on the moving
    hand, its small glides don't count as `animating()`); a bot's card is pulled out of the fan by the right hand and
    laid or tossed (`Characters::playCard`, the card leaves at `BOT_GIVE_LEAD`); a trick is pushed together
    (`cardlayout::gathered`) by the taker's hand (`Characters::gatherCards`) and then goes on to the won pile;
    Pişti's capture is swept the same way (`sweepTo`), a played card lands with its own turn and spread
    (`cardlayout::middle(i, up, card)`), a pişti makes the crowd react. The player's fan is held low in front of the
    eye (`cardlayout::hand(0, …)`); hover raises and tilts a card (`Cards3D::setRaise` / `setTilt`), a click or a
    drag up onto the felt plays it (`mouseCards`). Sounds: `Sfx::CardShuffle`, `CardSlide`, `CardPlace`,
    `CardSnap`, `CardGather` (synthesised in Audio.cpp). `tools/cards_snapshot` (`make cardsnapshot`) pictures the
    hands close up.
  * Tavla uses `r3d::Tavla3D`: the box, 24 inlaid points, 30 checkers that hop between stacks (the engine's position
    is reconciled checker by checker), two dice thrown to their numbers, highlights (lit checkers, green targets)
    and ray picking of points / bar / bear-off. Optional katlama zarı (`tavla::Rules::doubling`: offer / take / drop, Crawford, a
    3D cube with 2–64 faces; the bots' cube decisions come from their win estimate), the "MARS!" banner and a
    "Hamleler" panel (`Game::gameLog()`, notation in the mover's own point numbers).
  * Between hands the game fills a `ui::SheetModel` (Screens.h): the same hand-written sheet and final standings as
    the okey games, from rows the game chooses.
  * `tools/tables_check` (`make tablescheck`) plays all four with real mouse clicks through
    `TableGame::debugHumanClick`.
* Rules pages: the okey family's are written in Screens.cpp; the others come from `docs/kurallar_*.md`, embedded by
  `tools/gen_rules.py` into `src/ui/RulesText.inc` and parsed at runtime (headings become the page's tabs).

## 8. Around the games (2026-10)

* **Personalities** (`core/BotStyle.h`): `BotStyle::forSeat` makes Kel Mahmut bold (+1) and Emekli Nuri careful (-1),
  Hacı Rıza neutral; each bot maps boldness to a few knobs (feeding weights, pair openings and left tiles in 101 /
  okey, bids in Batak, koz choices in King). Neutral reproduces the tuned behaviour exactly; the styled bots stay
  within a few per cent of neutral strength (`sim --styles`).
* **Record** (`ui/Stats.{h,cpp}`, `istatistik.txt`): per game matches / wins / hands / streaks / wins per level and
  a best hand (`TableGame::humanHandScore`); rank points 1 / 2 / 3 per match won against Acemi / Usta / Kurt.
  App writes only matches the player played himself (no unattended runs, no Yapay Zeka). `ScreenId::Stats`.
* **İpucu**: what a Kurt would do in the player's seat now (`App::showOkeyHint` -> `Table3D::showHint`; the other
  games ask their own Kurt). `--hint-demo` shows one in a snapshot.
* **Rehber**: a short guide card (`ScreenId::Guide`, `App::guideFor`) at each game's first match, then one tip at the
  player's first turn; `Settings::guideSeen` keeps which were shown.
* **Save / resume** (`kayit.txt`): a match is its seed, the settings it started with and its engine's action log
  (`okey::Game::actionLog()` / `replay()`, `LoggedAction` text lines; the other games through
  `TableGame::saveState` / `restoreState`). Leaving a match, closing the window and every hand end write it; "Devam
  Et" on the title starts the same match again and replays it; Table3D rebuilds from the game state. A new match
  replaces the save. `--resume` continues it at once.
* **Variants**: renkli okey (`RulesConfig::okeyColorDouble`), Batak "önce koz açılmalı", Kısa King (12 el), tavla
  katlama zarı and katmerli mars — all in Ayarlar under the game's own section.
* **Memory** (`ui/Memory.{h,cpp}`, `hafiza.txt`): per regular and game the matches together, who won last, the best
  win / worst loss, mars and okey-finish moments, the days between visits and the rank last seen. `Banter::setMemory`;
  `Banter::matchStart` queues at most one remark (first visit, long absence, rematch, tease, streak, favourite game),
  `Banter::rankUp` a congratulation. App records at every match end (`MatchRecord::fromScores` for the okey family,
  `fromStandings` from the final sheet for the others).
* **Voices** (`Audio::speak`): every speech bubble is murmured by a small formant synthesiser (Turkish vowels,
  consonant onsets, falling / rising sentence pitch), one voice per regular plus the crowd and the çaycı. Rendered on
  the main thread into preallocated slots, mixed by its own stream; Ayarlar "Konuşma sesleri".
* **Replays** (`tekrarlar/`, newest 20): every finished match is archived in the save format plus `d` (date) and `r`
  (result). İstatistik -> Tekrarlar lists them; "İzle" restarts the match from its seed and feeds the actions one by
  one (`okey::Game::replay`, `TableGame::setReplayMode` / `replayStep`), nobody else moves. Space pauses, the arrows
  change the speed (0.5–8×). `--watch` watches the newest.
* **Analysis** (`app/Analysis.{h,cpp}`, `analysis::analyzeMatch`): a Kurt replays the match and judges each of the
  player's decisions (discards / openings / draws in 101, finishing chance in okey, 2-ply equity in tavla, Monte Carlo
  in the card games, bids / koz / contracts); the three costliest that are clear of the noise are shown on "Hatalarım"
  (`ScreenId::Analysis`, MatchOver button and Tekrarlar "Analiz"). App runs it on a worker thread (cancelled on a new
  match or quit); `--analyze` opens it for the newest replay.
* **Accessibility**: every table plays from the keyboard (okey: arrows walk the rack, Space carries, D / A draw,
  Enter discards, O opens, I finds a meld to add to; cards: arrows + Enter, Tab / 1–9 for the buttons; tavla: arrows
  for source and target, R / Enter rolls, U undoes). A key-help strip shows while the keyboard is in use.
  `ui::setColorBlind` / `tilegfx::setColorBlind` paint tiles with lightness-separated inks and a shape per colour,
  `cardgfx::setFourColour` a four-colour deck; `ui::setHudTextScale(1.25)` is "Büyük yazı". `tables_check --keys`.
* **Time of day and season** (`r3d/Daytime.h`, `RoomDaylight.cpp`, `CharactersLife.cpp`): Ayarlar "Vakit" (0 from the
  clock, 1 sabah, 2 öğle, 3 akşam, 4 gece) and "Mevsim" (0 from the date, 1–4 ilkbahar..kış) go to
  `Room::setTimeOfDay / setSeason` and the same on Characters every frame. Daylight paints the street, sun shafts
  and floor patches; fewer patrons in the morning; rain mostly on autumn / winter nights, snow and scarves in winter,
  leaves in autumn; the stove burns in the cold. Dev switches `SAKLI_SAAT`, `SAKLI_MEVSIM`, `SAKLI_KALABALIK`.
* **The crowd**: `Characters::crowdReact(Cheer|Groan|Laugh)` at a big finish (okey / ×2), a mars, a lost match;
  their shouts come back through `consumeCrowdLine` and App draws them where they came from (`drawCrowdShouts`, the
  crowd voice murmurs them). Bystanders (`setSpectatorMatch`) walk in during a long match and stand behind Kel Mahmut.
  At a match end the winner buys tea (`orderTeaRound`): the çırak refills our glasses, then the busy tables.
