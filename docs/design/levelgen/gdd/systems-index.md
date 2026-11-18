# Systems Index - Buildings (levelgen v3)

Studio standard: `Codex/templates/game-studio/design/CLAUDE.md` (8 required sections per GDD, `/design-review` per GDD,
`/review-all-gdds` after the set); the studio path `design/gdd/` maps to `docs/design/levelgen/gdd/` in this repo. This index covers the **building generator** systems only; the rest of the game is still
documented in `docs/design/*.md` (pre-standard) and is not indexed here yet.

Owner: Claude (building generator, designer 2026-11-18: «Бери генератор на себя»). Source: the single draft
`docs/design/levelgen/BUILDINGS_GDD.md` (commit 9210a435), split 2026-11-18 into the documents below without dropping content.

## Buildings pillars (shared by every GDD below)

Designer 2026-11-18: «Я хочу, чтобы каждое здание было небольшой историей… чтобы маршруты были нелинейными, чтобы не
только зашел, поднялся на лестнице, и вот ты уже наверху.»

1. **Non-linear, never impossible.** A storey may need a detour (outside, a balcony, a neighbour's flat, a breach), but every
   walkable storey and roof stays reachable **without explosives** - charges can run out. The validator enforces it.
2. **Readable.** What can be opened, kicked, broken, climbed or blown is visible: a door with a lock plate, a cracked wall
   panel, boarded windows, a fire-escape ladder. No invisible rules.
3. **Costs, not walls.** A route is chosen by its price (time, noise, explosives, exposure), not only by whether it exists.
   The same building is entered quietly through a broken back window or fast and loud by kicking the front door.
4. **The setting tells it.** Soviet industrial towns in the Arctic: panel blocks, hospitals, depots, halls; snow drifts,
   frost, collapse, barricades left by survivors and PMC defenders.
5. **Gameplay first, then the kit.** All rules live in the generator and the validator on a greybox; the art kit swaps in by
   module name (`docs/design/levelgen/BUILDING_GENERATOR.md` section 7) without changing a rule.

## Systems

| # | System | GDD | Category | Layer | Depends on | Status |
|---|---|---|---|---|---|---|
| 1 | Building archetypes + circulation | [building-archetypes.md](building-archetypes.md) | Gameplay (level/world) | Foundation | BUILDING_GENERATOR.md (grammar, contract), MANTLING.md, LADDERS.md | Draft v2 |
| 2 | Opening states (doors, windows) | [opening-states.md](opening-states.md) | Gameplay (level/world) | Core | 1 | Draft v2 |
| 3 | Building route model | [building-route-model.md](building-route-model.md) | Gameplay (AI/pathfinding) | Core | 1, 2 | Draft v2 |
| 4 | Breach panels | [breach-panels.md](breach-panels.md) | Gameplay (level/world) | Feature | 1, 2, 3, 5 (roles), DOORWAY_BREACH.md, WEAPONS_FRAMEWORK.md | Draft v2 (S4: waits for studio decisions, charges, R3 spike) |
| 5 | Building stories | [building-stories.md](building-stories.md) | Gameplay (level/world) | Feature | 1, 2, 3 (4 downstream) | Draft v2 |

Technical specification (data contract, builder + kit, runtime C++, Training Ground page, tests, phases):
[docs/design/levelgen/BUILDINGS_TECH_SPEC.md](../BUILDINGS_TECH_SPEC.md).

Design order: 1 -> 2 -> 3 -> 5 -> 4 (Foundation -> Core -> Feature). Status values: Draft / In review / Approved.

## Review history

| Date | Review | Result | Log |
|---|---|---|---|
| 2026-11-18 | `/design-review` lean, workflow full (skill applied by hand: the studio helper scripts are not installed) | all 6 documents NEEDS REVISION; content preserved | [reviews/](reviews/) |
| 2026-11-18 | Studio roles: game designer, level designer, narrative director, technical director | consensus below | - |
| 2026-11-18 | Rework v2 of all 6 documents after both reviews | awaiting re-review | - |

Studio consensus (2026-11-18) and how v2 applies it:

1. **Role first, not a catalogue first** (level + narrative + game): tactical roles and district themes before stories,
   story budget per role, pacing along the route - building-stories 1, 2, F3, F4.
2. **Measure choice, not detour length** (level + game): fast vs quiet entries must differ for choice stories; `D` is a
   report - building-route-model rule 7, F4.
3. **Costs need an opponent** (all): louder actions (kick 15 m, glass 10 m), glazed windows by default, exposure on all
   outside legs, squad single-file cost; LOS from PMC positions in S3 - opening-states 3, route model F1.
4. **Readable from the camera** (level + game): tells at the standard zoom, state overlay on a key and hover highlight -
   opening-states UI, stories Visual.
5. **Engine navmesh is the truth; prove P0 in engine first** (technical director): ADR-1..3, drift gate, schema `/2`,
   P0-proof phase before S1, R3 spike before S4 - BUILDINGS_TECH_SPEC 2, 6, 7, 8.
6. **Authors and outcomes** (narrative): `author` field with trace sets, `held` / `fell` outcome - building-stories 4.

Designer decisions 2026-11-18: rework before committing; **survivors exist** in the canon (NARRATIVE_BIBLE entry to add);
the **`author` field** is accepted; **S1 cut to stories 3, 4, 7, 10** + roles (more stories later).

Not taken (Claude, with reasons): LOS from neighbouring buildings in S1 (the generator does not know PMC positions yet - S3);
cutting story 11 (one data flag, a strong cold story); cutting 16 / 17 (kept, tied to the garrison role).

## Open questions (studio discussion) -> where each lives now

| Old # (BUILDINGS_GDD.md section 15) | Question | GDD |
|---|---|---|
| 1 | Story shares and per-archetype weights | building-stories |
| 2 | Locked doors: kick only? kick time, noise radius | opening-states |
| 3 | Glazed windows: cut / slow? noise radius | opening-states |
| 4 | Breach: marked panels only or whole materials | breach-panels |
| 5 | Breach size per weapon | breach-panels |
| 6 | Dynamic navmesh cost for breaches | breach-panels |
| 7 | Hospital / school and shop + flats archetypes, sizes | building-archetypes |
| 8 | Balcony rails 95 (vaultable drop) | building-archetypes |
| 9 | Mezzanine / landing edge rail (parked) | building-archetypes |
| 10 | PMC defenders create stories at runtime | building-stories |
| 11 | Basement level -1 in scope | building-stories |
| 12 | Owners of the runtime tasks R1-R4 | BUILDINGS_TECH_SPEC |
