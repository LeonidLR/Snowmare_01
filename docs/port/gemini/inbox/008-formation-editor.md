# 008 - Formation editor in the Training Ground (Movement tab) - TypeScript ONLY

The game designer (2026-11-10): "in the Movement tab I want to change the formations myself - move the members inside the diagram,
set their look vectors, and it goes into the game" (instead of describing fixes in words). NO C++ (Claude writes the game side
tomorrow from the format below), nothing under `Source/`, `Content/` except the ONE new data file, `Config/`.

## Where to work
New worktree from main: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name gemformed -Branch gemini/formation-editor`.
Rules: `docs/port/gemini/README.md`. Report: `docs/port/gemini/outbox/008-formation-editor.report.md`.

## The data file (new): `Content/Data/FormationLab/formation_shapes.json`, schema `codex.formation_shapes/1`
Reuse the custom formation's frame (`Tools/TrainingGround/src/movement/custom.ts`, FORMATION_CONTRACT.md 17: `fwd` = cm forward
along the heading, `right` = cm to the right, `look` = deg relative to the heading, (-180, 180]).
```json
{
  "schema": "codex.formation_shapes/1",
  "formations": {
    "diamond": { "4": { "members": [ { "role": "commander", "fwd": 0, "right": 0, "look": 0, "arc": 90 },
                                     { "role": "right", "fwd": -280, "right": 260, "look": 60, "arc": 90 }, ... ] },
                 "3": { ... } },
    "wedge": { ... }, "line": { ... }, "echelon_left": { ... }, "echelon_right": { ... }, "column": { ... }, "pair": { "2": { ... } }
  },
  "custom": { "custom_1": { "members": [ { "squad": 0, "fwd": 0, "right": 0, "look": 0 }, ... ] }, "custom_2": { ... }, "custom_3": { ... } }
}
```
- Roles: `commander` (always 0, 0), `right`, `left`, `rear` (rules.ts `Role`). Member counts 2..4 (CUSTOM_MAX_MEMBERS 4).
- `arc` (deg, optional) = the width of that member's look sector; absent = the rules' default.
- An ABSENT formation / count = the game keeps computing it from `formation_config.json` (today's behaviour). Only shapes the designer
  saved are written. `wall`, `hedgehog`, `blizzard` are rule-driven - not editable here.
- Integer cm and degrees; stable key order; unknown keys kept (same writer rules as the props file - see the review
  `docs/port/gemini/reviews/t1-props-schema-review.md`).

## The editor (Movement tab)
1. Pick a formation + member count (or custom_1..3). Show the current shape: from the file if saved, else computed by the existing TS
   rules from `formation_config.json` (so the starting point is what the game does today).
2. Drag members on a grid (snap 10 cm, the leader fixed at 0, 0, the heading arrow up); drag the look arrow from a member; drag the arc
   edge; a "mirror" button (echelon_left <-> echelon_right, or mirror the current shape); "reset to the game's default" (removes the entry).
3. Live checks while dragging: members closer than `slot.min_spacing`, a member outside CUSTOM_MAX_RADIUS, uncovered sectors / duplicated
   looks (the 360 deg coverage ring: show what nobody watches).
4. Save -> writes the file (via the TG dev server, atomic temp + rename, only that file). Russian UI labels like the rest of the TG.
5. The TS simulator (Movement tab's run) USES the saved shapes when present (so the preview = what the game will do once Claude ports it).
   Golden vectors: add `formation_Shapes*.json` cases (a saved diamond-4, an echelon mirror) for the C++ port to match.

## Done when
`npm test` + typecheck pass; tests for the file round-trip, the mirror, reset, the coverage check; screenshots of the editor in the report;
a short list of what the C++ side must read (for Claude).
