# 087 - Level generator: PMC posts + their mines / tripwires (report)

Agent: claude-api (Claude Opus 5.5 via API). Worktree `CodexTactics-gemday`, branch `claude-api/087-pmc-posts` (from origin/main 501debd7).
Not pushed. Main not touched. No C++, no maps, no accepted / level JSON / props files written by hand, no worker / ssh.
Spec: PMC_DEFENCE.md D2, TACTICAL_MAP.md 6, designer clip notes 2026-11-18 (notes 1, 1b, 2).

## Handover

**Done (all 4 steps, one commit each):**

| Step | Commit | What |
|---|---|---|
| 1 | 167eae1f | `pmcPosts.ts`: seeded choice of PMC posts from the tactical map. Config: `levelgen_v2.json` new key `pmc_posts`. Tests. |
| 2 | 98cdac69 | `pmcCharges.ts`: charges of the defend posts (D2 rules). Locked props layer `pmc_charges`. Props autogen hook. `GenLayout.pmc`. Tests. |
| 3 | 573dd502 | `build_levelgen_map.py`: `layout.pmc.posts` → level JSON `pmc_squads` (with `post`). Tuned entries survive a rebuild. Python test. |
| 4 | e7470f09 | Page: «ЧВК» toggle, overlay, «перебросить ЧВК» (new seed, whole map). «Принять» saves the plan only while the layer is on. |

**Left:**
- **C++ spawn at the post (separate API-agent task).** `PmcEncounterRules::ParseSquad` does not read `post` yet. A squad that has `post` and no `spawn` still spawns by the default rule (random ring, `delay_s`).
- **C++ PMC ownership of the charges.** The game's props code treats any `mines`-kind layer (or a `mine*` variant) as mines (`PropsGameplayRules.cpp:57`) and keeps unknown keys in `Object.Extra`. So the `pmc_charges` objects should spawn as ordinary level mines (not verified in-engine). Their `owner: "pmc"` / `post` / `role` are not read yet, so PMC fighters are not immune to their own charges.
- **Region-frame re-roll.** 084's frame action is not on main yet, so «перебросить ЧВК» re-rolls the whole map, as the task allows.
- **Screenshots: not done.** I had no browser in this session. Checked instead: typecheck, and a `vite build` into a temp folder (OK).
- **Doorway overlap with the random mines layer.** The ordinary `mines` layer's doorway pass does not look at other layers' `door` ids, so a door can get a random tripwire as well as a PMC doorway mine. Placement spacing keeps field mines away from the PMC charges (the PMC layer is added before `rerollAll`); door mines are not checked.
- **No accepted layout has a `pmc` block yet**, so nothing in Content changed except the config key.

**Next exact step.** On the Training Ground page:
1. Open `gen_ref_01_02_depot_1_x4`.
2. Turn on «ЧВК» and check the posts on the plan.
3. «Принять».
4. «Собрать в Unreal».

`accepted/gen_ref_01_02_depot_1_x4.json` then gets `pmc`, the auto props file gets the `pmc_charges` layer (only if the props file is `source: auto`), and `L_Gen_Depot_X4.json` gets `pmc_squads`. Then the C++ task reads `pmc_squads[].post` (pos xyz world cm, yaw) in `ParseSquad` and spawns the squad there. Smoke on X4.

## Design: choosing the posts (`Tools/TrainingGround/src/levelgen/pmcPosts.ts`, config `pmc_posts`)

**Input.** The map's tactical map (`TacticalMapFile`: world xyz, a layer per item, from `tacticalMapFor` / the committed `tactical/<Map>.tactical.json`) and the layout (start, mandatory route).

**Candidates:**
- `ambush_exit`: a hiding spot that has a `place` (a chokepoint).
- `hiding_spot`: any other hiding spot.
- `overwatch_roof`: each roof spot, with its look yaw.
- `window`: the window's stand point (60 cm inside), with its yaw.

**Rejected when:**
- inside a chokepoint outline, or within `chokepoint_margin_m` 1 m of it;
- within `min_from_start_m` 40 m of the player start (`objectives.start`, else `spawn`);
- within `route_clear_m` 3 m of the mandatory route on the post's own layer;
- farther than `max_from_route_m` 60 m from the mandatory route. A post the squad never meets is useless.

**Draw.**
- Post count: uniform in `count_by_factor[expansion factor]` (1: 1-2, 2: 1-3, 4: 2-4, 6: 2-5).
- Each post: a weighted draw without replacement, weight = kind weight × (0.5 + 0.5 × quality).
  - Kind weights: ambush_exit 1.0, overwatch_roof 0.8, window 0.6, hiding_spot 0.4.
  - Quality: hiding / ambush = 1 − reveal/30 m (short reveal = good ambush); roof / window = covered_m / the map's best.
- Spacing: `squad_spacing_m` 30 m between posts, and one post per building.
- Posture: defend 0.75 / hunt 0.25.
- Squad size by kind: 3 / 2 / 2 / 3.
- `delay_s`: defend 0 (they are there at the start), hunt 120.

**Recorded per post:** the attack anchor (the mandatory-route point nearest the post) and its distance.

**Determinism.** RNG = `PropsRng(streamSeed(seed, 'pmc_posts'))`. The same layout + tactical map + seed always gives the same plan. The plan stores `geometry_hash` (the tactical map's).

## Design: charges (`pmcCharges.ts`, `pmc_posts.charges`)

Only **defend** posts get charges; a hunt squad leaves its post. Supply = `sappers_per_squad` 1 × `per_sapper` {2 mines, 1 tripwire} (designer Q1).

1. **Doorway mine.** For a window post, or a roof post with inside access: the door of the post's building nearest the anchor that is not on the mandatory route. Placed `door_inside_cm` 60 cm inside, tagged with the door id (the validator lets a door's own charge stand in its corridor).
2. **Approach mines.** On the straight line target → anchor, where target = the post on the ground / its entry door / the foot of its outside ladder or external stair. First mine at `first_mine_m` 5 m, then every `mine_spacing_m` 4 m. Mines left over go on a second line: target → the route point `second_line_back_m` 20 m upstream (the way the attackers come). A line stops at `route_clear_m` 2 m from the route. A blocked spot slides sideways 0 / ±0.5 / ±1 m before it is given up.
3. **Tripwire.** Rules by post type:
   - **Roof post, inside access** (stairwell up to the roof / hatch): 2.2 m from the access onto the roof, towards the post, trying 0 / ±45 / ±90 / ±135 / 180°. 2.2 m clears the validator's 175 cm stair keep-out capsule (stair half-width 75 + 100).
   - **Roof post, outside access** (ladder or external stair, note 1b): no access trap; the guard covers it.
   - **Window post:** across the building's other door.
   - **Fallback, and every ground post:** across the flank, `flank_m` 5 m to the side of the post (or of the building's target point), the side with cover near it (the covered way in) first. Wire yaw = the crossing direction, folded to [0, 180).

**Every charge must also be:**
- at least `retreat_clear_m` 2 m from the post's retreat line (`retreat_m` 12 m straight away from the anchor);
- at least 2 m from the mandatory route on its layer;
- at least `start_clear_m` 20 m from the start;
- at least 4 m from any other PMC charge;
- passed by `validateStatic` (the TS port of the game's `FPropsValidator`), so the game drops none at load.

**Kinds:** the same ones G003b uses. A mine is `mine_proximity` / trigger `proximity`. A tripwire is `mine_tripwire` / trigger `tripwire` / `width_cm` 300 (the door width for a door tripwire).

**Props layer.** `pmcChargesLayer`: id `pmc_charges`, kind `mines`, `locked: true` (so `rerollAll` and «перебросить» never touch it). Each object carries `origin generated`, `owner "pmc"`, `faction "hostile"`, `post` = the post id, and `role`. `withPmcCharges` replaces the layer in place. `generatePropsForLayout` adds it when `layout.pmc` has charges, before the random layers are placed, so they keep their spacing from it.

**Level JSON** (`build_levelgen_map.py`). Each post becomes `{count, weights: "pmc_commander.json", posture, delay_s, post: {id, kind, pos [x, y, z] world cm, yaw}}`, with no `spawn`. On a rebuild from the same layout:
- the old entry with the same post (id + kind) keeps its tuned keys, and the post position refreshes;
- hand entries without a post stay;
- posts that are gone are dropped;
- if the layout has no posts, the old list stays.

## Run on the accepted X4 layout (`gen_ref_01_02_depot_1_x4` + `tactical/L_Gen_Depot_X4.tactical.json`, hash 1f399583ed257447 = the layout's)

**Candidates** (the same for every seed): ambush_exit 90, overwatch_roof 44, window 163, hiding_spot 60.

**Rejected:** chokepoint 49, start 16, route 4, far 133.

### Seed 1 (= the layout's seed, what the page shows first): 4 posts wanted, 4 placed

| post | kind | posture | squad | delay_s | layer | pos (cm) | yaw | source | route_m |
|---|---|---|---|---|---|---|---|---|---|
| PMC_01 | window | defend | 2 | 0 | ground | 5640, -767, 0 | 0 | B_hall_n_win_north_f0_6 | 40.76 |
| PMC_02 | ambush_exit (CP_04) | defend | 3 | 0 | ground | 3975, -4675, 0 | 91 | HS_68 | 16.19 |
| PMC_03 | ambush_exit (CP_10) | hunt | 3 | 120 | ground | 475, -3975, 0 | 88 | HS_92 | 8.22 |
| PMC_04 | overwatch_roof | defend | 2 | 0 | roof:B_ruin | -2100, 1250, 600 | -125 | B_ruin#s0 | 8.31 |

| charge | post | role | type | pos (cm) | layer | yaw | door / width |
|---|---|---|---|---|---|---|---|
| pmc_charges_PMC_01_0 | PMC_01 | doorway | mine | 3260, -2060 | ground | 0 | B_hall_n#1 |
| pmc_charges_PMC_01_1 | PMC_01 | approach | mine | 2899, -2460 | ground | -127 | |
| pmc_charges_PMC_01_2 | PMC_01 | flank | tripwire | 2800, -1759 | ground | 143 | 300 |
| pmc_charges_PMC_02_0 | PMC_02 | approach | mine | 3475, -4675 | ground | 180 | |
| pmc_charges_PMC_02_1 | PMC_02 | approach | mine | 3075, -4675 | ground | 180 | |
| pmc_charges_PMC_02_2 | PMC_02 | flank | tripwire | 3975, -4175 | ground | 90 | 300 |
| pmc_charges_PMC_04_0 | PMC_04 | approach | mine | -3488, 1369 | ground | -30 | |
| pmc_charges_PMC_04_1 | PMC_04 | flank | tripwire | -4173, 1190 | ground | 60 | 300 |

- **PMC_01** (window): target door B_hall_n#1. 2 mines, 1 tripwire. The first flank spot was refused for spacing, so the wire went to the other side.
- **PMC_02** (ambush at the CP_04 exit): target = the post. 2 mines, 1 tripwire.
- **PMC_03** (hunt): no charges.
- **PMC_04** (roof of B_ruin, reached only by the external stair STAIR_ext_7): no access trap (outside access). 1 mine + 1 unused, then the flank tripwire at the stair foot.

### Seed 87: 2 posts

| post | kind | posture | layer | pos (cm) | yaw | source | route_m |
|---|---|---|---|---|---|---|---|
| PMC_01 | hiding_spot | defend | ground | -6075, -13525, 0 | 93 | HS_143 | 57.36 |
| PMC_02 | ambush_exit (CP_32) | hunt | ground | 6125, 6025, 0 | 3 | HS_83 | 54.32 |

Charges of PMC_01: approach mines at [-6044, -13026] and [-6069, -12624], flank tripwire at [-5576, -13556] (yaw 176).

### 40 seeds

**Posts:** 115 (defend 85, hunt 30). By kind: window 42, ambush_exit 44, overwatch_roof 19, hiding_spot 10.

**Charges placed against the supply:**

| | placed / supply | share | unused |
|---|---|---|---|
| Mines | 145 / 170 | 85 % | 25 |
| Tripwires | 82 / 85 | 96 % | 3 |

**Charges by role:** approach 116, flank 72, doorway 29, roof_access 10.

**Where the unused mines are:** posts only a few metres from the route, where both approach lines reach the 2 m route-clear zone.

**Speed:** about 17 ms per plan with charges on X4.

## Tests

- **`node --test 'src/levelgen/*.test.ts' 'src/levelgen/props/*.test.ts' 'src/levelgen/node/*.test.ts'`: 232 / 232 pass.** Includes the new `pmcPosts.test.ts` (7) and `pmcCharges.test.ts` (9).
  - **Posts:** each rejection rule on a hand map (chokepoint / start / route / far); spacing and one post per building; a kind with weight 0 is never drawn; posture / delay / squad size come from the config. On X4 over 25 seeds: the count fits the expansion factor, no post in a chokepoint, at the start or on the route, all within reach of the route, spaced. Deterministic per seed, and another seed gives another plan.
  - **Charges:** exact positions on a ground post (mines at 5 m and 9 m on the approach line, flank wire 5 m to the side, yaw 0); the budget (2 sappers = at most 4 mines + 2 tripwires); a hunt post gets nothing; the retreat line stays free (with `retreat_clear` 8 m nothing lands within 8 m and the flank wire is refused); `upstreamPoint`. On X4 over 25 seeds: budget per post, every rule held, `validateStatic` drops none, every role appears, an outside-access roof never gets an access trap, deterministic. The props layer: locked, tagged, replaced in place, untouched by `rerollAll`, passes `validateProps`. The autogen hook, run on the hand map: a full X4 autogen takes about 250 s, so it is not a unit test.
- **`npm run typecheck`: PASS.**
- **`vite build --outDir %TEMP%`: OK.** The new page imports pull in `node:fs` modules, which vite externalises; the existing missionGoals / props schema already work this way.
- **`python -m unittest discover -s Scripts/Editor/tests`: 18 / 18 OK.** pytest is not installed on this machine; the tests are unittest. This includes the new `test_build_levelgen_pmc.py` (5, writes only to a temp folder) and the unchanged legacy Depot plan hash.
