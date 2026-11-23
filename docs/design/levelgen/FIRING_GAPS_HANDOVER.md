# Firing gaps in the roof parapet - handover (2026-11-19 evening)

> **HANDOVER 2026-11-19 late (worker stand-in lead, slot1, branch `worker/bgen-firing-gaps`).**
> - **Done (steps 1-7, code + tests + docs):** config test; analysis fix (`rim_stand_inset_m` 0.6, `rim_corner_clear_m` 1.65, `outward_only`,
>   `ANALYSIS_VERSION` 2); `src/levelgen/firingGaps.ts` (`planFiringGaps`, `withFiringGaps`, `gapForSpot`); `accept()` + `scripts/add-firing-gaps.ts`
>   (`npm run levelgen:firing-gaps`); `tacticalMap.ts` (`gap` id on spots, top-level `firing_gaps`); `build_levelgen_map.py` (gap spans, sill
>   pieces, rotated buildings, `tests/test_build_levelgen_firing_gaps.py`); BUILDING_GENERATOR.md section 8, TACTICAL_MAP.md.
> - **Deviation from the design (found by running it on X4):** the old candidate ring put 39 of 44 best spots at roof CORNERS, where a gap
>   cannot stand (corner post solid): only 5 of 44 spots could link to a gap. `chokepoints.json threats.overwatch.rim_corner_clear_m` 1.65
>   (= 1.2 + 0.9 / 2, pinned by a test) keeps candidates clear of the corners; now 40 of 44 link (the other 4 sit at a ladder top).
> - **X4 refresh (gate step a) is committed on this branch only:** `accepted/gen_ref_01_02_depot_1_x4.json` (now carries `analysis` v2 + 159
>   `firing_gaps` on 22 roofs) and `tactical/L_Gen_Depot_X4.tactical.json`. main's X4 files are untouched. NOTE for the merge into main:
>   `tacticalMap.test.ts` pins the committed X4 tactical file against the layout's analysis, so the analysis change cannot reach main
>   without this refresh (or a re-pin) - the lead decides. The C++ pin `TacticalMapRulesTest.cpp CommittedFiles` (46 -> 44 places) is changed
>   on this branch too (not built here; the gate built and ran it).
> - **Gate DONE on the throwaway `origin/worker/bgen-gate`** (= this branch + `origin/worker/t05-firing-positions`, test_map.json keeps both
>   smoke lists; + 2 gate-only commits: the gap-aware C++ below and the rebuilt `L_Gen_Depot_X4.umap` with 159 sills). On codex-worker, slot1:
>   build OK (7 min full, 3 min incremental); `CodexTactics.AI.*` 123 / 123, `CodexTactics.LevelGen.TacticalMap.*` 11 / 11 (new `ParseFiringGaps`);
>   smokes (`-Remote off -Jobs 3`) FiringPositionSmoke, TacticalPmcSmoke, LevelGenRouteSmoke, TacticalMapSmoke, PmcArenaTacticalSmoke = all PASS.
> - **C++ change on the gate branch (clearly required, T-05 side):** with the plain T-05 code FiringPositionSmoke FAILED (trial `roof_edge
>   B_gallery#1`: the rim search along the area ray put the stand point 15 cm from the parapet, the man was pushed 64 cm off, limit 60).
>   `TacticalMapRules` now reads the spot's `gap` and the file's `firing_gaps`; `UFiringPositionSubsystem::GatherRoofs` puts a spot with a gap
>   at the gap's stand point (no rim search); spots without a gap keep the old logic. This must be ported when T-05 merges into main.
> - **Gate numbers (X4, FiringPositionSmoke, 5 areas, kneeling-only sniper query vs rifleman):** T-05 report (main, spots 1 m in, 95 cm parapet,
>   no gaps): sniper 0 roof-edge positions, rifleman 16, 8-9 of ~44 spots blocked per area. Old map + new rim spots (intermediate run): sniper 5,
>   rifleman 22. Gaps + gap-aware stand: **sniper 7, rifleman 19**; real-shot trials with a CROUCHING (muzzle +85) shooter over the sill pass
>   (`roof_edge B_gallery#0`, `B_warehouse#0`: `SHOT: ok`, 2 cm off the stand point). Roof faults (`parapet blocks every stance`) per sniper
>   query: 7/7, 8/10, 3/4, 7/8, 1/4 spots (26 of 33) - a spot only sees through ITS 90 cm gap from 60 cm behind it, so most spots cannot fire at an
>   area that is not in front of them (the analysis picks spots for the routes, not for the queried area).
> - **Left / next exact step:** (1) the lead decides the merge (code on `worker/bgen-firing-gaps`; X4 data refresh + rebuilt umap + C++ on the
>   gate only); (2) levers if more kneeling positions are wanted: `firing_gaps.json width_cm` 100 (designer max), `stand_inset_cm` 40 (and
>   `chokepoints.json rim_stand_inset_m` 0.4: the capsule needs ~45 cm), or let `max_per_roof` / the street-side extras follow the spots;
>   (3) S2.3 blocks (S2_ROOMS_PLAN.md section 5). Not done: nothing of steps 1-7; the engine run of `CodexTactics.Bot.Route.ParseMantle` stays open.

Owner: Claude (building generator), branch `claude/building-generator`. Status: **design done, code NOT started**
(only `Content/Data/LevelGen/firing_gaps.json` exists; nothing reads it yet). Main PC went offline; the stand-in lead on
codex-worker continues from here.

## Why (designer decision 2026-11-19)

T-05 diagnosis on L_Gen_Depot_X4 (`claude/firing-positions`, `docs/port/claude/outbox/T05-firing-positions.report.md`):
1. Roof parapets are 95 cm. A sniper always fires kneeling (`SniperRules::FiringStance`, muzzle 85 cm over the floor), so no
   kneeling roof-edge position exists anywhere.
2. The tactical file's overwatch spots are not at the rim. The analysis (`chokepoints.ts` threat 3) calls `sight.los(..., b.id)`,
   which ignores the spot's own building, so a spot on one rim "covers" a street on the far side, about 28 m away across the slab.

Decision: put FIRING GAPS (a low sill a kneeling muzzle clears) in the parapets of overwatch roofs, and put the overwatch spots at
the rim behind those gaps. Every number goes in JSON.

## Design (agreed with the lead; implement in this order)

1. **Config** `Content/Data/LevelGen/firing_gaps.json` (DONE, commented):

   | Key | Value |
   |---|---|
   | sill | 60 cm |
   | width | 90 cm |
   | spacing | 4 m |
   | corner clear | 1.2 m |
   | max gaps | 3 per side, 8 per roof |
   | stand inset | 60 cm |
   | street probe | 8 m |
   | ladder clear | 60 cm |
   | roles | ["overwatch"] |

   Add a TS test that the code reads exactly these keys.
2. **Analysis fix** (`chokepoints.ts` threat 3, ~lines 493-530) with new keys in `chokepoints.json threats.overwatch`:
   - `rim_stand_inset_m` 0.6 replaces `edge_inset_m` 1 for the candidate ring. Remove the old key only together with the code change.
   - `outward_only` true: a target sample counts for a spot only when it lies outward of the spot's nearest roof edge, that is
     `dot(t - sp, n_out) > 0`; at a corner, outward of either adjacent edge.
   - Footprints may be rotated: use `buildingRectPoly` / `footprintPoly`.
   - Bump `ANALYSIS_VERSION`.
   - Update tests that pinned the old numbers, and add one: a spot does not count samples across its own roof.
3. **`src/levelgen/firingGaps.ts`** `planFiringGaps(l, analysis): FiringGap[]`, pure and deterministic.
   - **Which roofs:** walkable roofs that have parapets, where the building's role is in `roles` or the analysis lists the roof in
     `overwatchRoofs`.
   - **Spot gaps first:** one gap at each analysis spot of that roof, on its nearest parapet, at the spot's projection (clamped to
     the corner clear).
   - **Then extra gaps** every `spacing_m` on street-facing parapets. A parapet faces the street when the point `street_probe_m` out
     from its midpoint is inside the map bounds and outside every other footprint.
   - **Skip** a position within `spacing_m` of another gap, or within `ladder_clear_cm` of a ladder top (`l.ladders` with
     `to === 'roof:<id>'`) or an external stair top.
   - **Caps:** per side and per roof. Ordering is deterministic: spot gaps first, then sides N, S, W, E.
   - **`FiringGap` fields:** `{ id 'FG_<building>_<n>', building, parapet (wall_id), center Vec2 world cm, width_cm, sill_cm,
     outward unit Vec2, stand = center - outward*stand_inset_cm, facing_yaw (yawOf) }`.
   - **Storage:** a TOP-LEVEL `GenLayout.firing_gaps?`, not inside buildings, because `geometryHash` hashes `l.buildings` and a
     derived field there would make the analysis stale. The schema validator checks its shape; `geometryHash` must not include it.
   - `withFiringGaps(l, a)` returns the layout with `firing_gaps` set. It leaves the key out when the config is disabled or the
     list is empty.
4. **Applied** in two places:
   - `LevelGenDepartment.tsx accept()`: the posted layout is `withFiringGaps(layout, an)`.
   - New `scripts/add-firing-gaps.ts` with npm script `levelgen:firing-gaps`, modelled on `add-room-anchors.ts`. It takes explicit
     paths only and has a `--dry` mode. It recomputes a stale analysis, writes `analysis` and `firing_gaps`, and keeps every other
     key in order.
5. **Tactical export** (`tacticalMap.ts`):
   - Spots stay the analysis spots, which are now at the rim.
   - Each spot gets an optional `gap` id: the gap whose centre is within `width/2 + 30` cm of the spot's projection on its parapet.
   - A top-level `firing_gaps` list is written only when the layout has gaps, so files without gaps stay byte-identical.
6. **Builder** (`Scripts/Editor/build_levelgen_map.py` ~lines 1012-1028):
   - Each gap of the building is a span cut from its parapet, in the same data shape as the `levelgen_ladders.exit_gaps` spans fed
     to `split_walls`.
   - A sill piece is spawned over each span with the parapet's thickness and base z, height `sill_cm`.
   - Sill tags: `parapet`, `firing_gap`, `CodexMantleEdge`, `building=<id>`, `floor=<n>`, `gap=<id>`. Label `<gap id>_sill`,
     folder `Buildings/<id>/Parapets`.
   - A sill that overlaps a ladder / stair cut is clipped to the parts still standing.
   - All numbers come from the gap entries; the builder has no constants of its own.
   - Unit tests: one gap; a gap overlapping a ladder exit gap; no `firing_gaps` key gives output identical to before.
7. **Docs:** a section in `BUILDING_GENERATOR.md` (next free number) with the rule, the JSON, the data flow and the gate.

## Gate

**Lead's answer 2026-11-19: YES, on this branch / a throwaway branch only.** Refresh the ACCEPTED X4 layout
(`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json`) and its tactical file there; the refresh writes a fresh analysis
and `firing_gaps`. main's X4 accepted + tactical files stay unchanged: they are the T-05 / T-06 baseline, and the worker's night
loop checks out main, so its X4 runs use main's map again. Also in WORKER_LEAD_2026-11-20.md. Then run the gate:
- Rebuild `L_Gen_Depot_X4` on the worker only.
- Run T-05's `FiringPositionSmoke` there, from a throwaway ref that merges this branch with `claude/firing-positions` 64bae31c.
- PASS needs kneeling roof-edge positions on its roofs.

Never touch X6: those are the designer's uncommitted files in the main checkout.

## Exact next commands (from this worktree)

```
cd Tools/TrainingGround && npm test && npm run typecheck
cd Scripts/Editor && python -m unittest discover -s tests -p "test_build_levelgen_*.py"
python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude
node Tools/TrainingGround/scripts/add-firing-gaps.ts --dry Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json   # after step 4
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -ShipHead -Build -Commands "FiringPositionSmoke,TacticalPmcSmoke,LevelGenRouteSmoke" -Jobs 2 -NoRetry -AllLogs   # gate, after the lead's yes + X4 rebuild
```

After this: S2.3 blocks (S2_ROOMS_PLAN.md section 5). The 1200-core engine check goes in S2.3's first worker build.
