# Firing gaps in the roof parapet - handover (2026-11-19 evening)

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

Open question to the lead, sent 11-19: may the ACCEPTED X4 layout
(`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json`) and its tactical file be refreshed in this branch? The refresh
writes a fresh analysis and `firing_gaps`. If yes, run the gate:
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
