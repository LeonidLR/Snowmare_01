# 098 plan - walk-smoke digest (taken over by Claude)

- `Scripts/Tools/walk_smoke_digest.py <log|json> [...] [--json]`; stdlib only.
- Parse logs: `Smoke leg N/M <story> <kind> <from> -> <to> ...: ok|FAIL ...`, `Smoke RESULT:` header, `is stuck and failed to move` lines.
  Parse JSON `codex.buildings_walk/1`: header fields, failing legs from `reason` + `fail_pos`.
- Classes: ratio, follower_stuck, leader_stopped, refused (no complete navmesh path / off navmesh), other (TIMED OUT, follower STOPPED, ...).
- Clusters: single-link, 3D distance <= 150 cm, printed when N >= 2. Stuck meshes: count per StaticMeshActor.
- Delta: same map given twice (first = before, last = after) -> per-class delta.
- Tests: pytest on short excerpts in `Scripts/Tools/fixtures/walk_smoke/`.
