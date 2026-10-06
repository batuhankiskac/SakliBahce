# SaklıBahçe: bulut iş planı

This is a list of long-running jobs for a cloud session. Read `README.md`, `DESIGN.md` (especially §7–8) and
`DESIGN3D.md` first; they describe how the project is put together.

## Fixed rules

- UI text and in-game lines are **Turkish**. Reports are written in Turkish too.
- **Do not change Pişti's rules or scoring.**
- Work on **a separate branch**, never directly on `main`. Commit at the end of each job, with a short Turkish
  message that says what changed.
- Every job ends with:
  - `make test` and `make tablescheck` passing.
  - Headless snapshots where relevant (`--snapshot OUT --state game --view seat|corner|left`) that you have looked at.
  - The relevant README/DESIGN sections updated.
- Engines are deterministic (seed). Every game action goes into an action log, and save, resume, replay
  ("Tekrarlar"), "Hatalarım" analysis, İpucu, rehber and the stats book all depend on it. **A new game must hook into
  all of them:**
  - `TableGame`: `saveState` / `restoreState` / `setReplayMode` / `replayStep` / `humanHandScore`
  - `analysis::analyzeMatch`
  - `App::guideFor`
  - `ui::StatsBook` (`STATS_GAMES`)
  - `ui::Memory`
- A new table must also support the keyboard, colour-blind mode and big text (see `CardTable`, `TavlaTable`).
- Bots come in three levels: Acemi < Usta < Kurt. Show the ladder in a sim (`tests/*_sim.cpp`, a new sim for a new
  game).

## 0. First: the build on Linux and CI (item 8a)

The Makefile is written for macOS (`-framework ...`, Homebrew raylib). Make the cloud build work first:

- Add Linux link flags to the Makefile (`uname` check: `-lGL -lm -lpthread -ldl -lrt -lX11`). Bring raylib 6 from
  source or a package.
- Run headless snapshots under `xvfb-run`. Audio may be missing; then the game must keep going silently.
- GitHub Actions: on every push, `make test` (Linux, plus macOS if possible) and an ASan build (`make asan`).

## 1. Card animations: the cards should look like cards (top priority)

Right now in Pişti, Batak and King the cards hang in the air like the 101 istaka; it doesn't feel any different
from okey. Goal: the cards are held in hands and play like real cards.

- **Players' hands:**
  - Rıza, Mahmut and Nuri hold their cards **fanned out in their hands**, backs toward us.
  - When they play, an arm reaches out, pulls one card from the fan and **lays it** (or tosses it) on the felt.
  - The fan closes up as cards leave. Arms work through Characters' existing reach/arm animation system (World.h
    `BOT_*_LEAD`, CharactersAnim).
- **The player's hand:**
  - Low and close to the camera, a natural fan as if held in two hands.
  - Hovering lifts and tilts a card; a click or drag plays it.
  - The cards should not stand on the table edge in a row like the istaka.
- **Dealing:**
  - The dealer shuffles (riffle or overhand) and cuts. Pişti: the cut card is shown.
  - Cards are dealt one by one around the table, **sliding across the felt** to each player.
  - Players pick their cards up and fan them out.
- **Tricks (Batak, King):**
  - At the end of a trick the winner **gathers** the four cards with one hand and puts them face down in their own
    pile in front of them.
  - Tricks taken show as overlapping piles on the table, so the count can be read at a glance.
- **Pişti:**
  - Cards played onto the middle pile land with a small random rotation and spread.
  - When you take the pile, the player's arm sweeps it toward their side.
  - On a pişti, a "Pişti!" shout plus the crowd reaction (`crowdReact`).
- Card sounds: shuffling, sliding, the snap of a card hitting the table, gathering a trick. Synthesised or from free
  sources (CC0/CC BY; write the source down in `assets/.../KAYNAKLAR.md`).
- Speed: everything scales with the Ayarlar animation speed. AI mode and `--speed` must not slow down.
- Files: `r3d/Cards3D`, `app/CardTable*`, `r3d/Characters*` (new "holding cards" pose). `tables_check` must keep
  passing.

## 2. Tavla is played at its own table

Today tavla is played at the 101 table while the other two sit beside us. In real life tavla is a two-player game
with the players facing each other.

- When tavla is chosen, **the camera moves to a two-seat tavla table**, for example a small table by the window.
  The player and the opponent sit facing each other; **nobody sits beside us**.
- Meanwhile the other regulars stay at the okey table, or move to watch: they stand behind the opponent, as the
  existing bystander system does.
- Who the opponent is can be chosen (tavla is Kel Mahmut by default; Rıza and Nuri could play too). Their personality
  and memory (`ui::Memory`) carry on.
- Moving between tables: on game select or a new match, a short camera move (or a fade). The okey games stay at the
  existing table.
- Lighting, the lamp over the table, the tea glasses and the ashtray are placed for the new table. Check
  `PlayerCamera` limits and the snapshot views (`--view`) for the new table.
- Two-player card games (two-player Pişti) can also use this table.

## 3. New games (item 4)

Each new game is its own job: engine + bot (3 levels) + table + tests + sim + rules text (`docs/kurallar_*.md`,
`tools/gen_rules`) + all the integrations under the fixed rules. Suggested order:

1. **Dama (Türk daması):**
   - Orthogonal moves; forced capture with the longest chain chosen; the dama (king) moves any distance.
   - Uses the two-player table from item 2.
   - Bot: alpha-beta.
2. **Altmışaltı (66):** two players, 24 cards, koz, marriages (evlilik), 66 points.
3. **Bezik:** two players, a double deck, combinations. Research the Turkish rules; write the rules you settle on
   down in the rules text.
4. **Konken:** 4 players, rummy-like, with the 101 feel.
5. **Tavla variants:** Gülbahar and Moultezim/Fevga. A mode setting on the existing tavla engine.

Research the rules from reliable Turkish sources and **write the rules you settle on into `docs/kurallar_*.md`**.
Where something is unclear, write down the choice you made.

## 4. Second place: SaklıBahçe's garden (item 2)

The game is named after a **garden kahvehane**. Add an outdoor space to go with the inside:

- A garden under a big **plane tree (çınar)**: low walls, cobblestones, wicker chairs, string lights, tea stove,
  vine trellis, a cat or two, birds (sparrows, pigeons) and a distant call to prayer or seagulls.
- Ties into the time-of-day/season system (`r3d/Daytime.h`, `RoomDaylight.cpp`):
  - Leaf shadows on sunny days.
  - The garden is closed in winter, or covered with a tarp and a stove.
  - In rain the game goes inside, or plays under the awning.
- Ayarlar gets "Mekân: İçerisi / Bahçe / Otomatik" (automatic picks the garden on fair spring and summer evenings).
- Background patrons, the çırak and the bystanders work the same way in the garden.
- Watch performance: keep the `--perf` frame time on an M-series Mac around today's level (check with numbers).

## 5. Achievements (item 6)

- A "Başarımlar" tab in İstatistik, with locked and unlocked badges and a short description for each.
- 25–40 achievements, for example:
  - "İlk okeyle bitiş"
  - "Kurt'u marsla yen"
  - "Batakta 13 el"
  - "King'de rıfkı hiç almadan"
  - "Pişti üstüne pişti"
  - "7 gün üst üste kahvehaneye gel"
  - "Her oyunda en az bir galibiyet"
  - "Hatalarım ekranında sıfır hata"
- Stored in a separate file in the save directory (like `istatistik.txt`). Matches played by AI mode or in replays
  don't count.
- When one opens: a small banner at the table and a congratulation from one of the regulars (`Banter`); the crowd
  claps.

## 6. Player character (item 7)

- At the bottom of the screen the player's **own hands and arms** show: reaching for a tile, holding cards, throwing
  the dice, picking up the tea glass. An owner-pose animation that follows mouse and keyboard actions.
- Personalisation (Ayarlar → "Sen"):
  - sleeve colour or jacket
  - a ring or watch
  - a tespih (prayer beads)
  - your own tea glass (ince belli, a different colour)
  - a cigarette or none
- The regulars use the name you choose (`playerName`).

## 7. Technical cleanup (item 8b)

- `src/app/App.cpp` is over 2600 lines. Split it into parts with **no behaviour change**, for example:
  - `AppFlow` (match flow)
  - `AppRecord` (stats, memory, save, replays)
  - `AppSettings`
  - `AppAnalysis`
  - `AppDebug` (snapshot, autoplay, chaos)
- After the split, `make test`, `tablescheck`, `screens_snapshot` and the same-seed snapshots must be the same as
  before (pixel diff).
- Clean up compiler warnings; add an optional `clang-tidy` run.

## Known small issues (can be fixed along the way)

- `tools/table3d_snapshot` has 2 outdated checks:
  - "Geri Ver returns the tile with a penalty" (`penaltyReturnLeft` is now off by default).
  - "crowded validate: tiles still flying after settling".
- In tavla the R key both rolls the dice and recentres the camera (`PlayerCamera`); turn off the camera's R during
  tavla.
- `Audio::speak` takes about 0.3–1.8 ms per line on the main thread; if it causes frame hitches, make the rendering
  cheaper.

## Suggested order

0 (Linux + CI) → 1 (cards) → 2 (tavla table) → 7 (App split; makes the later work easier) → 3 (new games, one at a
time) → 4 (garden) → 5 (achievements) → 6 (player character).

Each item is its own commit or commits. At the end, write a Turkish summary report: what was done, how it was
tested, what is left.
