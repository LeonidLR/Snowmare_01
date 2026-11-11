# 006 - The game reads the per-fighter look-sweep toggle (designer 2026-11-11) - small, after 005

Gemini adds `"scan": true|false` per member to formation_shapes.json (codex.formation_shapes/1; absent = true = today's sweep) in
his 018 editor task. Game side, branch `codex/formation-scan-toggle` on interim: the shape reader (FormationShapeRules / the
formation_shapes.json loader with live reload) carries the flag per slot; FormationSectorRules uses the slot's fixed look (the
sector centre, no ScanValue sweep) when scan == false - while moving and at halt, pair / lead / side cases alike. Unit tests in the
FormationSectorRules / shape-rules tests (scan off -> constant look over time; absent key -> unchanged behaviour), FormationLabSmoke
and the formation custom smokes green. Report docs/port/codex/outbox/006-formation-scan-toggle.report.md.
