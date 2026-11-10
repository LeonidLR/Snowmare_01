# 004 - Training Ground pass 1: the TypeScript side ONLY

NO C++, nothing under `Source/`, `Content/` (except reading level JSONs), `Config/`. Claude writes / has written the C++ side.

## Where to work
- A NEW worktree from main (do NOT reuse CodexTactics-gemtg - it holds forbidden C++ / Config edits that will never be merged):
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name gemtgts -Branch gemini/tg-pass1-ts`
  Work only in `...\CodexTactics-gemtgts`. Rules: `docs/port/gemini/README.md`. Report: `docs/port/gemini/outbox/004-tg-pass1-typescript.report.md`.

## What
The old task `docs/port/gemini/GEMINI_TASK_TG_PER_LEVEL_2026-11-09.md` - do ONLY its Training Ground (TS / Vite) parts:
1. Environment panel per level (incl. the new `override_carry` checkbox, Russian label «Погода уровня важнее перенесённой»).
2. Per-level weapons picker: `loadouts`.
3. Per-level starting kit editor: `kit`.
4. Its TG bug fixes (the waves-tab save hazard, the "Run simulator" behaviour) as described there.

## The JSON the game reads (C++ side, agreed design - write EXACTLY this)
- `"loadouts": { "<key>": ["<weapon_id>", ...] }` - first = primary. Key = operative id `slot_0` / `slot_1` / `slot_2` (roster index:
  Commander, Engineer, Medic-Sapper) OR role key `commander` / `engineer` / `medic_sapper` / `recruit`; the operative id wins over the role.
  Keys and weapon ids lower-case. An empty list = absent. Weapon ids = the ones in `Content/Data/**/weapons_tuning.json` (read the list
  from there for the picker).
- `"kit": { "<key>": { "grenades": n, "medkits": n, "heat_packs": n, "ammo": { "<weapon_id>": n } } }` - same keys; a missing field keeps
  the game's value (show "по умолчанию" in the UI, do not write the field).
- `"environment": { ..., "override_carry": true|false }` - default false (do not write it when false).
If the C++ branch `feat/tg-pass1-cpp` lands with a different shape, Claude will tell you in the chat.

## Done when
`npm test` (or the TG's existing checks) pass, the three panels read / write a COPY of a level JSON correctly (round-trip, unknown keys
preserved, nothing else reformatted - the designer's file `L_Gen_Depot_1.json` is never written by your tests), screenshots of the three
panels in the report.
