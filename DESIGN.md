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

**101 kuralları (variants).** Ayarlar has a "101 kuralları" page (Screens settings page 3) whose options map onto
`RulesConfig` in one place, `app/Rules101.h` (`apply101Rules`, used by App::startMatch, the analysis and the tests):
açma sınırı → `openThreshold` (51/81/101/121), bitiş katları → `finishMult` (`FinishMult::Stack` x2 per okeyle /
çiftten / elden and multiplied — the default — `Single` x2 at most, `None` no kat), açmayan yazar → `unopenedScore`
(202/404), and the existing `penaltyJokerDiscard`, `penaltyPlayableDiscard`, `penaltyReturnLeft`,
`waitTurnAfterOpening`, `katlamali`, `leftOpenPenalty`. `Game::finishMultiplier` is the one formula (engine, bots,
İpucu, analysis); `Game::handMultipliers()` keeps every hand's kat for the score sheet. The options are settings
keys (`y101acma`, `y101kat`, ...) and so part of a saved / replayed match's `k` lines; a save without them is read
with the defaults (`reset101Rules`). Rules, choices and sources: docs/kurallar_101.md.

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
  * Two-player card games at the tavla table (2026-10, `CardTableFrame` in `CardTable.h`): every card layout is
    written for our table ("layout space") and mapped through the table frame. At location 1 (seats 0 and 2) it is
    turned into `tavlaFrame()` and its depth squeezed by `TAVLA_HALF_D / FELT_HALF` (deck, dealt piles, trick, won
    piles fit the smaller top); the player's fan keeps its place in the view (`eyePose`: moved with the chair and
    turned with the tavla seat's steeper look). `CardTableBase` maps everything it lays out itself; a game maps its
    own poses / points with `layPose()` / `layPoint()`. İki kişilik Pişti (`pistimasa=2`) is played there.
  * Tavla has its own table (2026-10; `w3d::TAVLA_TABLE`, `tavlaFrame()`): a small two-seat table behind our seat,
    by the wall bench, turned 90° (the player looks toward the street door). Its frame keeps our table's
    proportions (same top height, same chair-to-edge distance), so every seated pose fits. `TableGame::location()` 1
    makes App call `setLocation`: `Room::setTavlaFocus` (its pendant becomes the key light, ours a point light),
    `Characters::setTavlaTable` (the chosen regular, `Settings::rakip` 1..3, Kel Mahmut by default, sits
    across; both glasses go along; Kel Mahmut's ash goes into its ashtray; the bystanders of a long match stand behind
    him; the çaycı serves the two there from nodes 9 / 16), `PlayerCamera::setTavlaSeat` (eye and base yaw from the
    frame, a steeper resting pitch, R left to the dice) and a 0.7 s fade in from black. `Tavla3D::setFrame` draws and
    picks the board in that frame. The other two regulars stay at the okey table and talk; the opponent's lines follow
    who it is (TavlaTable's `kOpp*` tables). Okey and card games, and the title, bring everything back.
  * **Rakip** (round 5): one opponent for every two-player game at that table — tavla, dama, altmışaltı, bezik and
    iki kişilik pişti — `Settings::rakip` (key `rakip`, 1 Hacı Rıza, 2 Kel Mahmut, 3 Emekli Nuri; the "Rakip" row of
    each of those games in Ayarlar). `ui::twoPlayerOpponent(settings, kind)` resolves it; a match's save / replay
    carries the `rakip` line, and a save from before it (no line: `rakip` 0) falls back to the old `tavlarakip` /
    `damarakip` keys for tavla / dama and to Kel Mahmut for the card games, which always seated him; an old settings
    file takes its tavla opponent. The card games lay out the opponent as layout seat 2 (across); `CardTableBase::
    oppSeat_` / `chr()` turn that into the Characters seat for his hands, bubbles, voice, head, `activeSeat()`, and
    `seats()` returns `{0, opp}` (ui::Memory, the tea round, achievements). Lines, `BotStyle` and the "Çaylar …'dan"
    titles follow who it is; the two who are not playing watch from the okey table (`watcherSeat`,
    `ui::bezikRemark(seat, situation, pick, opp)`). The analysis names him from the same setting.
  * The opponent's hands at that table (round 5, `src/r3d/CharactersBoard.cpp`, `r3d/PieceCarry.h`):
    `Characters::carry` takes a dama disc between thumb and fingers, carries it along its way (lifted, hopping over
    the discs it takes) and puts it down, then lifts each taken disc off the board and drops it on his side —
    `Dama3D::playMove(..., hand)` paces the move for a hand (the taken discs wait on the board until the hand comes for
    them) and fills in the way and the times. The same call carries a Bezik card from his declared row onto the trick
    and turns the altmışaltı koz over when he closes; `Characters::drawCard` takes a card off the stock into his held
    fan (`CardTableBase::drawFromStock`: the card waits for the fingers and flies with the hand; the player's drawn
    card now flies to the fan too instead of popping in). A Bezik declaration is laid from the fan with `playCard`,
    the declared cards leaving with the hand. `tools/board_snapshot.cpp` (`make boardsnapshot`) shows them close up.
  * Tavla uses `r3d::Tavla3D`: the box, 24 inlaid points, 30 checkers that hop between stacks (the engine's position
    is reconciled checker by checker), two dice thrown to their numbers, highlights (lit checkers, green targets)
    and ray picking of points / bar / bear-off. Optional katlama zarı (`tavla::Rules::doubling`: offer / take / drop, Crawford, a
    3D cube with 2–64 faces; the bots' cube decisions come from their win estimate), the "MARS!" banner and a
    "Hamleler" panel (`Game::gameLog()`, notation in the mover's own point numbers).
  * **Dama** (Türk daması, 2026-10; `src/core/Dama*`, `app/DamaTable.cpp`, `r3d/Dama3D`, rules in
    `docs/kurallar_dama.md`) is played at the two-seat table too (`location()` 1; the opponent is the shared `Settings::rakip`,
    key `rakip`, Kel Mahmut by default; the match goes to `damaWins` games, key `damahedef`, 1 / 3 / 5). The engine
    (`dama::Game`, squares `row * 8 + col` seen from the player) generates whole moves: men step forward / sideways,
    damas fly orthogonally; capturing is forced and only the routes taking the most pieces are legal (men and damas
    count alike), pieces leave as they are jumped (a later jump may cross their square), no 180° turn between two
    jumps, a man is crowned only where its move ends (mid-capture on the last row it goes on as a man). A game ends
    with no pieces / no move; draws: one piece each (no capture to make), the third repetition, 50 plies without a
    capture or a man stepping forward. The starter (light) comes from the seed and then alternates; the match lasts
    at most 2N + 1 games. Its action log is one line per move (`m player from landing...`, `n` next game). The bots
    (`dama::Bot`) are negamax alpha-beta (iterative deepening, TT, PVS, killers / history, late move reductions,
    captures searched past the horizon) over material (man 100, dama 300) + advance, centre, guards, home row, a dama's
    open lines and trading when ahead; Acemi 2 plies with noise, Usta 6 plies / 40k nodes, Kurt up to 24 plies /
    400k nodes (~0.15 s; every budget in nodes, never the clock, so a seed gives the same game), searched on a worker thread (`std::async`) while the opponent's pause runs. `Dama3D`: a parquet
    board of maple and walnut squares, wooden discs (a dama = two stacked discs with a brass ring between them), a
    slide for a step, a hop per jump, taken discs lifted to stacks at the taker's right, movable discs lit with a
    gold ring, targets green (colour-blind: striped sky blue), the pieces the shown move would take marked red;
    click-click or drag, arrows + Enter. "Hatalarım": `analysis::analyzeDama` (AnalysisDama.cpp) scores every legal
    move with a 4-ply search and reports losses of at least 0.8 pieces.
  * **Bezik** (2026-10; `src/core/Bezik*`, `app/BezikTable.cpp`, rules and every choice in `docs/kurallar_bezik.md`):
    two-handed bezique with a double piquet deck at the two-seat table (`location()` 1, seats 0 and 2: Kel Mahmut
    across, `CardTableFrame` maps the layout). Cards are `r3d::Cards3D` instance ids: the first copy the kart id, the
    second copy + 52 (`CARD_COUNT` is 104, `cardFace(id)` the atlas face; one-deck games never show 52..103). The engine
    (`bezik::Game`): 8 each, the turn-up makes koz (a 7: the dealer writes 10); stage 1 any card, the trick's winner gets
    a `Stage::Declare` window (one combination from `availableMelds`, one canonical card set each, table cards
    preferred, a new card from the hand required; a card is never used twice in the same kind nor in a smaller kind
    after a bigger one: seri -> koz evliliği, çift bezik -> bezik) plus the koz 7 (exchange, or show), then the winner
    draws first; stage 2 (stock and turn-up gone, table cards back in hand) follow suit and head the trick, else
    trump; brisks (A, 10) 10 each and the last trick 10 at the deal's end; the match to `Settings::bezikTarget`
    (`bezikhedef` 500 / 1000 / 1500). Log lines `kind seat value [cards]` (play / declare / koz7 / pass / next deal).
    Bots (`bezik::Bot`): Usta a keep-value policy (koz, brisks, live combinations), Acemi Usta with 40 % random cards
    and forgotten declarations, Kurt determinised Monte Carlo (opponent hand + stock sampled from the unseen cards and
    the publicly `exposed` ones, every candidate rolled out with Usta's policy, `1200 / n` paired samples) in stage 1,
    an exact memoised solver in stage 2 and declaration ordering (koz evliliği before the seri, a single bezik before
    the double while the stock lasts). `bezik_sim` (duplicate deals): Kurt +53 points a deal over Usta (z 6), Usta +95
    over Acemi (z 15), Kurt +158 over Acemi; Kel Mahmut's bold Kurt -8 ± 7 against a neutral one. The table: the stock with the koz crosswise under it (drawn cards wait on it until the trick is swept),
    each player's declared cards face up in a row before them, a centred declaration panel for the player (Koz 7'si,
    the combinations, Geç; İpucu marks Kurt's choice), Kel Mahmut's lines per combination, Hacı Rıza and Emekli Nuri
    calling over from the okey table (`ui::bezikRemark`). "Hatalarım": Kurt's card values (120 samples; exact in stage
    2) and a combination left unsaid.
  * Tavla çeşitleri (`tavla::Rules::variant`, Ayarlar → Tavla → Çeşit, settings key `tavlacesit`, also written as a
    `k tavlacesit=N` line into saves / replays; a save without it is klasik): **Gülbahar** and **Fevga** (Moultezim)
    share the board indices; player 1 starts on index 11 and both run the same way round (his home 12..17), no
    hitting, one checker holds a point. Gülbahar: from a player's 4th roll a double climbs the ladder to 6-6 (each
    rung its own record line and `EvType::Roll` with `amount = 1`; an unfinished rung ends the turn). Fevga: the
    first checker must pass the opponent's start before a second leaves, and no step may close a 6-run with all his
    checkers behind it. The klasik code paths are untouched (`tavla_sim` gives identical numbers). The bots use
    their own same-way evaluation (pips, blocks/primes, lost dice, stacking; per-çeşit Kurt weights). The cube works
    in both; no katmerli mars there. Player 1's borne-off checkers still go to the right-hand tray.
  * **Konken** (2026-10; `src/core/Konken*`, `app/KonkenTable.cpp`, rules in `docs/kurallar_konken.md`): four-player
    rummy with two decks and four jokers (ids 0..103 the decks, `id % 52` the kart face, 104..107 the jokers:
    `r3d::CARD_COUNT` is 108, `cardFace` maps them to the atlas' `KEY_JOKER` jester). 14 cards each and one turned up;
    draw from the stock or take the top discard (it must be used that turn or given back, `returnDiscard`), open with
    melds worth `Rules::openMin` (51; Ayarlar 40 / 71: `konkenacma`), then lay, add to anyone's melds and swap table
    jokers; a joker is discarded only as the last card. Finisher 0, opened players their hand points (A 11, figures 10,
    joker 25), unopened 100, a konken (opened and finished in one turn) doubles the others; a total that reaches
    `Rules::limit` (151; 101 / 201: `konkenyanma`) burns. Konken bitiş (`Rules::lastStanding`, `konkenbitis`, default
    1 "son kalan"): the burned leave (`SeatInfo::out` / `place`, `Game::active` / `nextActive`; the deal, the starter,
    the dealer and the turn go round the ones left, 4 -> 3 -> 2), an `EvType::Burn` follows the HandEnd, the match
    ends when one is left and `ranking()` is the order they burned in (`HandRecord::playing` / `burned` for the sheet);
    a burned regular stands behind his chair (`Characters::setSeatOut`, CharactersKonken.cpp), a burned player gets
    a panel: "Hızlı izle" (the table at 3x, the sheets move on by themselves: `TableGame::spectating`) or "Sonuca geç"
    (the bots play the rest at once, logged as usual). 0 ("ilk yanan", and every save from before the key): the
    match ends after the first burn, lowest total wins. Melds:
    `konken::makeMeld` (a run keeps the given order when it is one, else the engine's arrangement), `canAdd`,
    `swapIndex`; `bestPartition` is an exact DP over the real cards (memo on covered mask + jokers left, every joker
    count tried, spare jokers attached or kept) used by the bots, the "Aç" button and "Diz". Bots
    (`KonkenBot.cpp`): Acemi greedy and loose, Usta the exact partition plus outs-weighted near-melds and a
    feed-the-opened-neighbour guard, Kurt rollouts of Usta's best five discards (sampled draws, opponents finishing
    with a hand-size hazard) plus what the next player picked up / let go; `konken_sim --duplicate` shows the ladder
    (about Acemi 44 > Usta 31 > Kurt 25 points a hand). The table lays the player's fan itself
    (`CardTableBase::gameLaysOwnHand`): own order (drag along the fan, Shift+arrows, "Diz"), selection for "Aç",
    drag onto a meld (left / right half = front / back, onto a joker = swap) or onto the discard pile; melds lie face up
    in rows in front of their owner (readable from our seat, a hover label names them), stock and discard pile in the
    middle (only their top ten / eight cards are drawn). "Hatalarım": `analysis::analyzeKonken` (AnalysisKonken.cpp):
    discards by Kurt's rollouts, missed openings, a stock draw while the discard would have opened.
  * **Altmışaltı** (66, 2026-10; `src/core/Altmisalti*`, `app/AltmisaltiTable.cpp`, rules and every choice in
    `docs/kurallar_altmisalti.md`): two players, 24 kart ids (ranks 9..As of the 52), played with Kel Mahmut at the
    two-seat table (`location()` 1, seats 0 and 2 through `CardTableFrame`). The engine's core is a copyable
    `altmisalti::Deal` (hands as masks, the stock with the koz card at the bottom, points, pending marriages, the
    closer) shared with the bots; `Game` adds names, events, the sheet, the public facts (`shownCards`: a declared
    marriage's other half, the koz taken by exchange or with the last draw; `cannotHold`: what the strict rules showed)
    and the action log (`Play` / `Exchange` / `Close` / `NextHand`). Marriages are declared automatically by leading a
    Kız or Papaz with its partner in hand (they wait for the first trick), 66 is declared automatically; closing the
    stock or using it up switches to follow / head / ruff; game points 1 / 2 / 3, a failed closer gives 2 (3 if the
    other had no trick at the closing), nobody at 66 after the last trick: its winner takes 1; match to 7. Bots
    (`AltmisaltiBot.cpp`): Usta heuristics (cheap leads, koz kept for an As / 10, close on sure points, the strict
    endgame by top cards; also the rollout policy), Acemi a loose Usta that never closes, Kurt determinized Monte Carlo
    (64 samples while the stock is open, 40 after closing; fixed counts, no clock: a seed replays exactly) where every option — each card and closing — is played on
    by the Usta rules until the hand turns strict and then solved exactly by alpha-beta over both hands; once the stock
    is used up every card is known and Kurt plays perfectly. Closing must beat the best card by a margin (the exact
    endgame knows the sampled hands); Kel Mahmut's boldness lowers it (and Usta's close threshold). `altmisalti_sim`
    plays duplicate matches: Kurt wins 85 % of matches against Usta (+0.69 game points a hand), Usta 90 % against
    Acemi (+0.71), Kurt 100 % against Acemi; bold and cautious stay even (Kurt 49 / 51 %). The table keeps its own pieces: the stock beside the middle with the koz across under it
    (face down on top once closed), the drawn cards that stay on the stock until the trick is swept (`handOf` leaves
    them out), a declared marriage's partner shown face up for a moment; "Kapat" / "Kozu Al" are game buttons
    (`CardTableBase::gameButtons`), a click on the glowing face-up koz exchanges too. "Hatalarım":
    `analysis::analyzeAltmisalti` judges every card and closing by Kurt's `evaluate` (expected game points).
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
  katlama zarı, katmerli mars and the çeşit (klasik / Gülbahar / Fevga) — all in Ayarlar under the game's own section.
* **Memory** (`ui/Memory.{h,cpp}`, `hafiza.txt`): per regular and game the matches together, who won last, the best
  win / worst loss, mars and okey-finish moments, the days between visits and the rank last seen. `Banter::setMemory`;
  `Banter::matchStart` queues at most one remark (first visit, long absence, rematch, tease, streak, favourite game),
  `Banter::rankUp` a congratulation. App records at every match end (`MatchRecord::fromScores` for the okey family,
  `fromStandings` from the final sheet for the others).
* **Başarımlar** (`ui/Achievements.{h,cpp}`, `basarimlar.txt`, `app/AppAchievements.cpp`): 40 badges in three pages
  (taş ve tahta, kâğıt oyunları, kahvehane), some with a count (3/10), two secret. Fed by the okey hand results and
  the opening of exactly 101 (App), the tables' events (`TableGame::noteAchievement` -> `achievementEvents()`, read by
  App every frame: "pisti", "mars", "batak13", ...; the event names are listed in Achievements.h), the end of every
  hand and match (`MatchEndInfo`: game, level, won, the regulars beaten, the real clock's time of day / season) and a
  zero-mistake "Hatalarım". Counted by the record's rule (`setLive(recording(), !unattended())`); only "Seyirci"
  (the Yapay Zeka played a whole match) needs just an interactive session. An unlock: `Screens::showAchievementBanner`
  (with `Sfx::Chime`), `Banter::achievement`, `Characters::crowdReact(Cheer)`. `ScreenId::Achievements`;
  `--state basarimlar` and `--achievement-demo` for snapshots. (duzelt, round 5) A badge that opens at the end of a hand
  holds the score sheet 3.5 s more (`App::achHold_`) so the banner, the congratulation and the applause happen over the
  table, not behind the sheet. `--achievement-test` (developer check, implies `--autoplay`; `make basarimcheck`): the
  bot's match counts as the player's own in that run only, the book is read and written under `$HOME`; it passes when a
  badge opened and its banner was up, a regular named it while no sheet covered the table and the room shouted, and the
  book on disk has it (a Pişti mid-hand, a one-point tavla's "Hayırlı Olsun" at the match end).
* **Voices** (`Audio::speak`): every speech bubble is murmured by a small formant synthesiser (Turkish vowels,
  consonant onsets, falling / rising sentence pitch), one voice per regular plus the crowd and the çaycı. Rendered on
  the main thread into preallocated slots, mixed by its own stream; Ayarlar "Konuşma sesleri".
  (ses, round 5) Now a small Klatt-style talker, rendered on the VoiceWorker thread: an LF-like glottal pulse with
  per-period jitter, shimmer, roughness, tremor and pulse-synchronous breath; a cascade of five formants with a nasal
  pole/zero pair; formant transitions from consonant loci (labial, dental, post-alveolar, velar by vowel frontness);
  shaped noise for s/ş/ç/c/f/z, bursts and aspiration for stops, nasal murmurs, a tapped r. Prosody: per-sentence
  declination, Turkish final stress (with exceptions: şimdi, hadi, nasıl ...), rising pre-nuclear words and a
  nuclear accent before the verb, a peak before "mi", a rise for other yes/no questions, the wh-word accented,
  continuation rises at commas, phrase-final lengthening and a breathy, creaky end. Interjections are made as sounds
  (laughs with a breath in, "hah", "hıh", "ah/of/oh", "eh", "tüh", "cık", "hmm", "şşt", "vay", "aman", the particles
  "be/ya/ha"). Voices: Rıza warm and dark, Mahmut loud, pressed and rough, Nuri old (slower, tremor, breathy, nasal),
  the çaycı a quick lad, voice 6 the player. `tools/voice_render.cpp` writes the lines to WAV (+ `--plan`, `--speak`).
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
* **The garden (Mekân)** (`RoomGarden*.cpp`, `RoomGardenInternal.h`): Ayarlar "Mekân" (key `mekan`: 0 içerisi, 1 bahçe,
  2 otomatik = fair spring / summer days and evenings) goes to `Room::setVenue`; `Room::venue()` is the resolved place.
  The garden keeps the room's floor plan (our table, the tavla table, `BG_TABLES`, the counter at `COUNTER_POS`, the
  street door → the garden gate), so Table3D, the camera, the people and the cat need not know; the furniture statics
  are shared (`Static::venues`). By day the key light is the sun 25 m away (shadow far plane up to 40 m in Gfx), dappled
  by the çınar's and the vines' leaves; at night our pendant again. Awning in rain / winter, `SAKLI_MEKAN` (1 / 2)
  overrides otomatik for snapshots; `--view up | wide` look at the tree.
* **Özel günler and the rain move** (ozelgun: `r3d/SpecialDay.h`, `RoomSpecial.cpp`, `CharactersSpecial.cpp`,
  `ui/BanterSpecial.cpp`, `app/AppSpecial.cpp`): `w3d::resolveSpecialDay` (setting "Özel günler", key `ozelgun`, default
  on; dev switch `SAKLI_GUN=bayram|kurban|ramazan|mac|normal`) gives Ramazan / Kurban Bayramı, Ramazan (its evenings,
  akşam / gece, are the "after iftar" ones) or the Sunday-evening derby. The Hijri months are computed from Meeus' new
  moon with Diyanet's practical rule (conjunction before 12:00 UTC → the next day), checked against 2025–2027 in
  `tests/test_ozelgun.cpp`. App passes the day to `Room::setSpecialDay` (bunting, the flag / Ramazan sign / derby poster
  in the back-wall slot over Kel Mahmut's head, lokum or güllaç plates, the TV's scarf and pennants, "DERBİ" on the TV,
  in the garden banners and a portable TV that counts goals) and `Characters::setSpecialDay` (ties and carnations, team
  scarves draped on each torso mesh, men standing at the TV or by the card players); lines through
  `ui::specialLine` / `Characters::chat`; `Sfx::Davul` on Ramazan nights; `derbyGoal` = crowd cheer + shouts. With Mekân
  = otomatik App holds the room's venue (`Room::setVenueHold`); the room only notes `venuePending()` (1 rain, 2 derby, 3
  time / season) and App fades out and calls `applyVenue()` between hands (HandOver after 1 s, the sheets, the title).
  The rain that sent us in keeps us in until the phase changes. A passing shower (spring / summer, seed;
  `SAKLI_YAGMUR=<s>`) raises `rainTarget` over the phase's own `phaseRain`. Fixed Bahçe: the awning, and
  `Audio::setRainCanvas(Room::awningAmount())` drums the drops on its canvas. `--view tv | ocak` for snapshots.
* **The crowd**: `Characters::crowdReact(Cheer|Groan|Laugh)` at a big finish (okey / ×2), a mars, a lost match;
  their shouts come back through `consumeCrowdLine` and App draws them where they came from (`drawCrowdShouts`, the
  crowd voice murmurs them). Bystanders (`setSpectatorMatch`) walk in during a long match and stand behind Kel Mahmut.
  At a match end the winner buys tea (`orderTeaRound`): the çırak refills our glasses, then the busy tables.
* **The player's own hands** (`r3d/PlayerHands.{h,cpp}`, `r3d/HandCue.h`): forearms and hands at the bottom of the seat
  view, built from the regulars' tools (Characters' hand meshes, `chr::Track` Hermite tracks, two-bone IK; the shoulders
  sit under the camera and slide forward into a long reach). The tables tell them what the player's object does through
  two optional hooks, `HandCueFn` / `HandLeadFn` (`Table3D::handCue` / `handLead`, `TableContext::handCue` / `handLead`):
  `Take` / `Give` / `Carry` (a tile drawn / discarded / dragged, a card played or dragged, a checker moved; `where` gives
  the object's live grip point until it lands), `Dice` and `CardFan` (the left hand holds the fan,
  `cardlayout::humanFanFrame`). A table holds the player's object back by the hand's `lead` (Take 0.20 s, Give 0.13 s,
  Dice 0.45 s at speed 1; 0 when the hands are off or a screen is up) like `BOT_*_LEAD`. Idle: rest on the table edge
  beside the istaka (clear of the tiles and cards), now and then a sip from the player's glass (Characters lends it:
  `holdPlayerGlass` / `sipPlayerGlass`, `TeaGlass::holder` 0), a tespih flick, a cigarette drag; never started on the
  player's turn. They sink out of view while a screen covers the table and are drawn only from the seat camera.
  Ayarlar → "Sen": `eller`, `kol`, `kolrenk`, `ten`, `yuzuk`, `saat`, `tespih`, `bardak`, `sigara`. Dev switch
  `SAKLI_ELLER=sip|tespih|smoke` starts that idle at once (snapshots).
