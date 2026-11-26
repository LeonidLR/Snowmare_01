# 121 - Full tactics recording

Status: validation in progress; NOT MERGE-READY. Existing lane and branch `gemini/121-recording-tactics` reused. Original Gemini revisions `be2e839e`, `fbad5357`; merged `origin/main` with merge commit `4d12aa59` before changes. No main/play checkout, merge or edits.

## Review and changes

- Preserved additive v1 event and snapshot format and Gemini's stealth/cover/climb hooks. Shot hits now include weapon; damage and lethal attribution accept only a synchronous matching attacker context, invalidating unrelated later damage and resetting on round boundaries. Unknown attacker awareness/support serialize as null.
- Common health recording captures ordinary hits, direct melee/freezing loss, burning and bleeding exactly once using actual health delta; actor callback duplicates are removed. TakeHit preserves the concrete attacker context. Damage arithmetic and health/death ordering are unchanged.
- Death records are kills for either side. Crossing the critical-health threshold records down separately; zero-health player death is no longer mislabeled down.
- Victim awareness and allied support use each observer's sight and obstruction/FOV checks. Replaced proximity-only support and team-shared visibility/selected-target observer counts. Optical `seen_by_enemy` counts live observers, not team reports.
- Ground height uses the existing sight system's downward ground-column measurement, including roof starts. Manual Shift-look pitch is captured before formation scan pitch. Active look yaw is additive so manual/torso looks can cover a sector even while the body faces elsewhere.
- Added threat facing and optional cover evidence for rear/lure classification; cover remains unknown for bodies that do not expose this state.
- Offline tagger sorts timestamps (round end follows same-time kills), isolates rounds, uses legacy `cov` and clip changes, and does not invent stealth, zero visibility, kills or open-ground evidence. High ground is relative to the nearest enemy; same-height roofs do not qualify. Distract/flank requires stealth displacement beyond 90 degrees, same-target allied engagement and a matching, nearby-in-time kill. Rear flank additionally needs recorded target facing. Overwatch requires a cover order; crawl resets on standing. Lure requires the same enemy to follow the retreat into explicitly open ground, then a long-distance kill by a different ally. Unwatched sectors require consecutive gaps, not a session sum. Fast rear flanks do not require stealth; approach_unseen requires explicitly zero enemy observers plus elevated overwatch.
- Added 19 Python positive/negative cases, plus C++ runtime hit-attribution and all-health-loss-path regression tests. No smoke assertions weakened.
- Removed implicit `git lfs push` from lane worker_sync scripts: code shipping uses bundles. Earlier already-running sync invocations executed the old automatic LFS object-upload command; branch refs were not pushed.

## Evidence so far

- Python `Scripts/Tools/test_demo_tactics.py`: PASS 19/19.
- `git diff --check`: PASS. worker_sync PowerShell parser: PASS.
- Legacy `2026-11-25_L_Gen_Depot_1_X6_094811.jsonl`: fallback at 124-130 seconds (three operatives); high ground C_1 at 146.1-209.1 seconds; continuous unwatched flank. No kill/down events exist, so no completed distract/flank inferred. Pitch is unrecorded. Output: `Saved/CodexValidation/121-094811-tactics.txt`.
- New-format synthetic offline fixture detects all three core tactics plus crawl, overwatch, rear flank and scan/unwatched metrics: `Saved/CodexValidation/121-new-format-fixture.jsonl` and `.txt`. This is a test fixture, NOT a PIE/human recording.
- Final C++ build, Smart and mapped smoke results: pending. First worker build PASS422.86s captured an earlier revision and is not final validation.

## Required closeout

Run final worker build, Smart tests and all 16 mapped smokes. Capture a new short PIE fight using this revision and analyze it with the tagger: all three core tactics must appear. The existing designer Editor has not been started, closed or operated. A human/PIE recording has not been manufactured from the synthetic fixture. Do not mark ready until this acceptance evidence exists.

No maps, .uasset, Content JSON, Config or VFX edited. No branch push.

PIE acceptance recipe for Lead: run final lane code for at least60seconds with an accepted order; on contact retreat into cover, engage a lower enemy from a roof, then keep ally fire on the same target while a stealth operative moves more than90degrees around it and kills it within6seconds of the observed flank. Close that PIE world to flush the recording, then run `python Scripts/Tools/demo_tactics.py <new-file.jsonl> --json`. Preserve the real file and show all three core tags.
