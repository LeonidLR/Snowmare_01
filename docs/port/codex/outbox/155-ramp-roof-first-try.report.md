# 155 ? first roof order via ramp: fix-round report

Status: **MERGE-READY for Lead review**, with local runtime acceptance. The previous MERGE-READY claim was rejected for missing acceptance evidence; the results below replace it.

Merged `origin/main` `b4848608` in `b46c4ca3`, in this lane. No main checkout, push, designer JSON, map, asset, Config or infrastructure-script edits. Existing untracked content junctions and Config file left untouched. The branch's existing `Scripts/test_map.json` smoke mappings remain; infrastructure scripts equal main.

## Fix-round changes and findings

The failed gate sampled a partially built **dynamic infantry navmesh**. The new audit logs `RecastNavMesh-Default`, agent `Default`, 2001 active tiles and `building=1` after preload. Its bounds cover X -16796..16796, Y -22724..22724, Z -290..1210. Waiting until navigation finishes changes the table from widespread missing endpoints to **50 inspected accesses / 7 findings**. The Vehicle data has invalid bounds; it is not the selected infantry data. Neither agent selection nor the 60 cm endpoint extent needed changing. Both smokes now wait for navigation and fail on timeout; the audit does not produce a table while navigation is still building.

`designer155` now constructs the recorded round-four leg directly, independently of layout stair eligibility. It stages Commander in cover at **(-7398,-12825)** (asserted), sends **ONE `HandlePlayerClick`** to **(-9111,-11096,900)**, switches selection at **+10 seconds**, and requires actual feet at roof height within 400 cm of that goal. Removed the old double-click for this scenario. It no longer calls the generic stair-mouth placement before staging the recording. Cold simulation is disabled as in the generic ramp smoke.

Initial measured arrival: **(-9094.53,-11114.61), feet Z902**, 25 cm from the requested goal, after 47.6 smoke seconds. The other two operatives stayed on the starting level. The observed traversal is a high-fence mantle before the ramp; this does not independently establish that the historical legacy-vault ownership loss caused the designer's recorded stop. The existing branch preserves own-order ownership across legacy vault/step-off resumes; no gameplay changes were added in this fix round.

The audit now requires positive controls: building **39 B_hall_n**, its hatch ladder and ramp must both have bottom/top navigation, complete paths, and no findings. The full actor table is [155-roof-access-audit.md](155-roof-access-audit.md). Audit PASS proves execution and controls, not a defect-free map. The table is for Lead/designer review; no data repair was applied.

## Local validation

All runs use this lane, `-Remote off -Jobs 1 -NoRetry -ExtraArgs '-Multiprocess'`, fixed 60 FPS and fixed seed. No remote worker transport.

- Initial local build: **PASS**, exit 0, UBT 25.13 s.
- Initial `LevelGenRampSmoke,RoofAccessAuditSmoke,FormationFenceCrossSmoke -Repeat 3`: **9/9 PASS**, no retries, 368 s wall. Evidence: `Saved/Logs/155-initial-repeat3.json`; fence logs `Smoke-FormationFenceCrossSmoke-r1.log` through `-r3.log`.
- Fence-cross specifically: **3/3 PASS** (39.5 / 39.1 / 38.3 s). No excess failures versus the reported flaky base, so no base rebuild or gameplay fence fix was needed.
- Final local build after adding explicit cover/control/timeout assertions: **PASS**, exit 0, UBT 9.89 s. `Saved/Logs/Build.log`.
- Final two repeats of the two changed 155 smokes: **4/4 PASS**, no retries, 155 s wall: roof **2/2**, audit **2/2**. Evidence: `Saved/Logs/155-final-repeat2.json` and `Smoke-LevelGenRampSmoke-r1/r2.log`, `Smoke-RoofAccessAuditSmoke-r1/r2.log`. Both final roof arrivals matched the measured position above; both audits passed the known-good ladder/ramp, case-2 dispatch, and visible refusal checks. **13/13 local smoke executions passed overall** (including the initial batch).
- `git diff --check` PASS; infrastructure-script diff against origin/main empty.
- The prior Lead gate's build and Smart tests passed. Smart tests were not rerun for this dev-smoke-only fix round. Both modified C++ files compile. Seven existing unity-name warnings remain in unchanged files.

## Map findings for designer review

All 50 accesses now project both endpoints onto infantry navigation. Seven still require review:

| Building | Access | Finding |
|---|---|---|
| 26 B_x1_3_1_t1004_gallery | LadderActor_22 | Endpoint path disconnected; inspect ladder attachment / exit clearance. |
| 45 B_ruin | StaticMeshActor_634 | Top connection takes more than the permitted direct join; inspect seam / obstruction. |
| 64 B_x1_16_t7_block (case 2) | StaticMeshActor_1094, lower ramp | Endpoints exist but no complete path; top does not join adjacent floor directly. |
| 60 B_x1_25_4_t1003_block | StaticMeshActor_1286, lower ramp | Same disconnected top join. |
| 14 B_x2_12_t8_block | StaticMeshActor_1527, lower ramp | Same disconnected top join. |
| 23 B_x2_13_t8_block | StaticMeshActor_1602, lower ramp | Same disconnected top join. |
| 2 B_x2_23_t8_block | StaticMeshActor_1854, lower ramp | Same disconnected top join. |

The table includes heights and proposed data investigations for every actor. These are measured path/join findings, not a claim that changing geometry alone will repair every case. Case 2's current ladder reaches Z600 and has a connected path; the historical screenshot's short-ladder appearance is not reproduced by this runtime audit.

## Existing gameplay implementation retained

The supplied 07:05:30 excerpt contains ground orders around (-2597,-11215), not the actual round-four roof attempt. Recording round four starts at t825.9511; roof orders at t875.9157/t876.0885 target (-9107,-11116,900)/(-9111,-11096,900), with the stop at (-7763,-11531,feet251). The relevant access is B_x1_10_t7_store_stair_0.

The existing branch's click fixes preserve elevated ramp/slab/parapet/window hits, validate lower-hit identity and opening/storey bounds, and retain elevated order height. Elevated own orders require landing nav at the requested height and a complete path; refusal produces the HUD message **No way up** and live floating **NO WAY UP ? CHECK RAMP / LADDER** text. The audit exercises case-2 roof dispatch and both refusal channels. Existing traversal-gait behavior from 152 remains separate from own-order restoration. No new cover, window or order features were introduced in this fix round.
