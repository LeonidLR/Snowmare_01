# T-11 (first half) - generator-placed PMC squads spawn at their post (report)

Agent: Claude (sub-agent, worktree `CodexTactics-claude`), branch `claude/pmc-posts-in-game` from origin/main 070ca3d3. Not merged, main not touched.
Spec: `docs/design/gdd/smart-enemies-roadmap.md` T-11, `docs/design/PMC_DEFENCE.md` D2, report 087 (what the generator writes).

## Handover

**Done (spawn-at-post half):** a level JSON `pmc_squads` entry with `post` {id, kind, pos [x, y, z] cm, yaw deg} spawns its men ON that post,
facing its yaw, posture defend, no random ring, no delay. Entries without `post` take exactly the old path.

**Left (OUT OF SCOPE, waits on designer decision OQ-21):** the charge half - `owner: "pmc"` of the `pmc_charges` props layer
(`PropsGameplayRules.cpp:57` still treats the layer as ordinary mines), the PMC's own path cost / avoidance of them, immunity of its men.
Nothing of it was started; no file under `LevelGen/Props*` or `Interactables/Deployable*` was touched.

**Decisions made (check them):**
- A `post` replaces `spawn` (it wins if both are present).
- `post` with no `posture` is a defence; a defend post always has delay 0 (the spec: "no delay"), a hunt post keeps its `delay_s` (the generator
  writes 120 for hunt posts), 0 when absent.
- A post squad does NOT go through `AssignTacticalPosts` (that re-posts defenders along the player's route and would move them off the post).
  The commander's Ambush then runs as for any defend squad; what it does to the men after the spawn frame is not part of this task.
- No "out of the squad's view" test and no 40..90 m ring for a post squad (the post is where the generator put it). The men still spawn
  concealed by the fog of war like every PMC squad.
- Ground posts carry Z 0 in the generator output (`5640, -767, 0`): treated as "height unknown" like `spawn: [x, y]` (navmesh probe with the tall
  extent); roof posts carry the floor Z (600) and are probed with the short extent.

## What changed

| File | What |
|---|---|
| `Source/CodexTactics/Public/AI/PmcEncounterRules.h` | `FPmcEncounterPost`, `FPmcPostSpawnConfig`, `FPmcEncounterSpec::bHasPost/Post`, `PostSlots`, `PostFacing`, `PostSpawnConfigFromJson`, `LoadPostSpawnConfig` |
| `Source/CodexTactics/Private/AI/PmcEncounterRules.cpp` | `ParseSquad` reads `post` (line ~104-132); the pure placement / facing / config functions (line ~140-215) |
| `Source/CodexTactics/Private/AI/PmcEncounterSubsystem.cpp` | `SpawnEncounter`: the post branch of the placement loop (line ~281-310), facing = post yaw, no tactical-post reassignment (line ~353), log line |
| `Content/Data/AI/pmc_operators.json` | new `post_spawn` block: line_spacing_cm 150, rank_spacing_cm 150, rank_count 2, ring_cm 140 (the numbers live here; the struct defaults mirror them and a test checks they are equal) |
| `Source/CodexTacticsTests/Private/AI/PmcEncounterPostSpawnTest.cpp` | `CodexTactics.AI.PmcEncounter.PostSpawnParse / PostSpawnPlacement / PostSpawnConfig` |
| `Source/CodexTacticsDev/Private/Debug/PmcPostsSmokeCommand.cpp` | `CodexTactics.PmcPostsSmoke` |
| `Scripts/test_map.json` | `PmcPostsSmoke` in `smokeRegistry`, its map entry (L_Gen_Depot_1 + L_Gen_Depot_1.json), and the PMC rule's smoke list |

Placement: man 0 is exactly on the post; the others alternate right / left of the facing, `line_spacing_cm` apart; a slot that is off the navmesh
or taken falls back to `rank_count` ranks behind it, then the `ring_cm` ring, then the post. Deterministic, no RNG, no seed.

## Smoke

`PmcPostsSmoke` needs no map or level JSON edit: it feeds two level-JSON-shaped entries (with a post, `delay_s` 45 in the JSON) through the real
`ParseSquad` into `SpawnEncounter` at navmesh points 25..70 m from the player's squad on L_Gen_Depot_1, and checks per squad: parsed delay 0, all men
spawned, posture defend, one man within 60 cm of the post (measured 0 cm), all men within 5 m (measured 150 cm), no two within 90 cm (150 cm), every
man's yaw equals the post yaw (error 0.00 deg). It does not cover a roof post or X4 (no accepted layout has a `pmc` block yet, and the lead's X4
«ЧВК» accept/rebuild is a precondition of the roadmap's X4 version); it does not follow what the commander does with the men afterwards.

## Gate results (all in the worktree, `CODEX_AGENT=claude`)

| Gate | Result | Log |
|---|---|---|
| `Scripts/build.ps1` | PASS (exit 0; first full build 540 s, a compile error in my test fixed, rebuild 9 s) | `Saved/Logs/T11/build.log` (Build.log is overwritten) |
| `Scripts/test.ps1 -Filter CodexTactics.AI.PmcEncounter` | PASS, 7 tests (4 old + 3 new), 0 failed | `Saved/Logs/T11/test-PmcEncounter.log` |
| `PmcPostsSmoke` | PASS (58 s) | `Saved/Logs/T11/Smoke-PmcPostsSmoke.log` |
| `PmcEncounterSmoke` | PASS (54 s) | `Saved/Logs/T11/Smoke-PmcEncounterSmoke.log` |
| `PmcEncounterHuntSmoke` | PASS (56 s) | `Saved/Logs/T11/Smoke-PmcEncounterHuntSmoke.log` |
| `git diff --check` | PASS | - |

Not run: `PeekMinesSmoke` / `GeneratedPropsX6Smoke` (charge half, not touched), `-Smart`, the triage audit (not in this task's gate).
The unity-clash finder still prints one clash (`ParseJson`, two files of other systems) that was there before; the two clashes my first test file caused were renamed away.

## Next steps

1. Lead: accept the «ЧВК» layer for X4 on the Training Ground page, rebuild; then a real `pmc_squads[].post` reaches the game and the roadmap's
   `PmcPostsSmoke on X4` can be added (check the men on the generated posts, including the roof post's Z).
2. OQ-21 decided -> charge ownership (props spawn tag, path cost / avoidance for the PMC side).
3. Open question for the designer / lead: what the Ambush order does to post men after the spawn (hold on the post vs walk to a hiding spot).
