# 022 - RetreatFacingSmoke loadout correction

Status: bounded smoke-only task complete; original refire assertion passes 5/5, but the whole smoke fails 5/5 on its explicit-facing assertion. Keep knownFail. Needs Claude for any deeper diagnosis or gameplay work.

Branch: `codex/022-retreat-facing-fixture`. Routing: 2 | gpt-6.1-sol | 0.68. No local model used. WIP source/report checkpoint: `77804b34`; this finalization changes only this report. Validation resumed after Claude #531 authorized continuation following the designer STOP/full reset.

## Change and intent

The 12-line addition in `Source/CodexTacticsDev/Private/Debug/RetreatFacingSmokeCommand.cpp` loads the existing M16 asset, adds it to both selected operatives through `AddArsenalWeapon`, and explicitly switches/checks it through `SwitchToWeaponById`. This follows the existing GiveWeapon/FormationJitter smoke API pattern instead of assuming the first roster weapon. Both Commander and Engineer log a successful fixture check in every run.

The header describes turning away during a sprint, then facing and firing at the pursuing pack. Pinning this fixture's rifles preserves the original 14 m approach and 26 m retreat and removes the role-loadout range mismatch diagnosed in 021. Every original assertion, tolerance, timeout and park behavior remains unchanged. No gameplay, assets, configuration, tuning, or protected files changed. `Scripts/test_map.json` remains byte-unchanged because the required five PASS verdicts were not achieved.

## Validation

Every shell sets `CODEX_AGENT=codex` and `PYTHONIOENCODING=utf-8`. Fresh standard `Scripts/build.ps1` succeeded: actual wrapper exit 0, UBT total execution time 18.84 seconds. Evidence: `Saved/Logs/codex022-build.log` and `Saved/Logs/codex022-build-wrapper.txt`. This replaces the interrupted pre-reset queued build; no mutex bypass or other process termination was used in this resumed validation.

Each run used exactly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1
```

Default fixed step `-UseFixedTimeStep -FPS=60 -FixedSeed` and formation start were retained. No retry, extra arguments, loaded batches, or broader tests. Actual wrapper output and engine logs were archived immediately after each run while knownFail was still present. Each wrapper reports KNOWN-FAIL and exits 0; those exit codes do not indicate an engine PASS.

| Run | Engine verdict | Actual wrapper exit | Both M16 checks | Refire after facing | Terminal ordered / pack angle | Turning to threat | Engine log / wrapper capture (under Saved/Logs) |
|---|---|---|---|---|---|---|---|
| 1 | FAIL | 0 | PASS | 0.1 s, PASS | 65 / 115 deg | 0 | `codex022-run1.log` / `codex022-run1-wrapper.txt` |
| 2 | FAIL | 0 | PASS | 0.1 s, PASS | 65 / 115 deg | 0 | `codex022-run2.log` / `codex022-run2-wrapper.txt` |
| 3 | FAIL | 0 | PASS | 0.1 s, PASS | 65 / 115 deg | 0 | `codex022-run3.log` / `codex022-run3-wrapper.txt` |
| 4 | FAIL | 0 | PASS | 0.1 s, PASS | 65 / 115 deg | 0 | `codex022-run4.log` / `codex022-run4-wrapper.txt` |
| 5 | FAIL | 0 | PASS | 0.1 s, PASS | 65 / 115 deg | 0 | `codex022-run5.log` / `codex022-run5-wrapper.txt` |

All five have the same sole FAIL line:

```text
Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)
```

The original `both fire at the pack again` failure is resolved in these five samples; the complete smoke remains red on the later explicit-facing check. No deeper diagnosis was attempted, and no check or delay was adjusted to suppress that failure.

`rg -n 'Retreat' Scripts/test_map.json` finds only RetreatFacingSmoke as a Retreat smoke name; there is no RetreatSmoke or other Retreat smoke to run once. `python Scripts/Tools/check_smoke_names.py` passes (actual exit 0): every JSON smoke name is registered. Capture: `Saved/Logs/codex022-smoke-names.txt`. `git diff --check` passes.

## Hand-back

Retain the existing RetreatFacingSmoke knownFail entry and its existing reason unchanged, as required when the five-run gate fails. New signature above needs Claude if investigation beyond the authorized fixture correction is desired. No additional tests or gameplay edits are authorized by this task. Parent handles review, boundary audit and final commit; no push, merge, branch switch, external chat, or commit was performed by this worker.
