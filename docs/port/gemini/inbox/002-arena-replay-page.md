# 002 - PMC arena tactical replay page (top-down, instead of video)

Tooling task: Python + one HTML page. NO game code, NO C++, nothing under `Source/`, `Content/` (except reading), `Config/`.

## Where to work
- Worktree: from the main checkout `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics` run
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name gemreplay -Branch gemini/arena-replay`
  and work ONLY in `...\CodexTactics-gemreplay`. Rules: `docs/port/gemini/README.md` (`$env:CODEX_AGENT = "gemini"`, explicit-path
  commits, no push / merge). Report: `docs/port/gemini/outbox/002-arena-replay-page.report.md` in the main checkout.

## Why
The game designer watches the night trainer's PMC arena fights on his PHONE and cannot read 3D clips (who is where, who shoots,
why they stand). A top-down replay of the round data reads far better and costs nothing to render.

## Input that already exists (do not change the game)
- Round tracks: `Saved/Arena/<run>/round_<n>.track.json` in the main checkout (read them, copy a few as test fixtures into
  `Scripts/Tools/arena_replay/fixtures/`). Schema `codex.pmc_arena_track/1`: `dt` (0.2 s), `units` (cm), `map`, `scenario`,
  `winner`, `duration_s`, `objective {x,y,radius,attacker}`, `fighters [{id, side A|B, team}]`, `frames [[ [x,y] per fighter ]]`,
  `states [[ [yaw, stance, hp, order] | null when down ]]`, `events [{t, type, who, side, decision, score, ...}]` - inspect all event
  types present (tactic, shots / hits / kills if present). The reader `Scripts/Trainer/nighttrainer/stalemate_stats.py` shows how
  they are parsed.
- Map outline: level JSON `Content/Data/LevelJson/<map>.json` -> `levelgen_layout` (e.g.
  `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json`) has the buildings / walls / fences; draw them as grey shapes. If a map
  has no layout (e.g. L_FormationLabCombat), draw only the fighters, the objective and a grid.

## What to build
1. `Scripts/Tools/arena_replay/build_replay.py <track.json> [...] --out <dir>` -> ONE self-contained HTML per run (all rounds of the run
   in it, data inlined, no external requests except optional pinned CDN libs), plus `--latest` (newest run under `Saved/Arena`).
2. The page (phone first, 375 px wide, works in landscape; dark + light):
   - top-down map, team A red, team B green dots with a facing tick (yaw), a short name label on tap, dimmed cross when down;
   - shot lines (from shooter to target) for ~0.5 s when the events carry them; the objective circle coloured by who holds it;
   - the current order of each fighter (Objective / Hold / Flank / ...) as a small text tag, toggle on / off;
   - play / pause, speed x1 / x4 / x16, a scrub bar with marks at kills and long quiet spells (> 30 s without a shot);
   - round picker, round result (winner, why, duration), per-team counts alive.
3. `python Scripts/Tools/arena_replay/build_replay.py --test` - parses every fixture, checks frame / fighter counts, writes the HTML.

## Done when
`--test` passes, an HTML built from the latest real run opens in a browser, you looked at it at 375 px and 1280 px yourself (screenshots
in the report), and the report lists: event types found, what the page shows, what data is MISSING for a better replay (e.g. shot
targets, cover, orders) - Claude adds that to the game next week.
