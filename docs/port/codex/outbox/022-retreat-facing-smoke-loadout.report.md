# 022 - RetreatFacingSmoke loadout correction (WIP, limit stop)

Branch: codex/022-retreat-facing-fixture.

Prepared a minimal smoke-only change: load the existing M16 fixture asset, add it to both selected operatives' arsenals, switch to it through the existing API, and fail explicitly if equipping fails. Keeps the original 14 m approach, 26 m retreat, all assertions, tolerances and timeouts. Parent reviewed the 12-line source diff.

Validation is PENDING. Build was waiting for the shared Build.bat mutex; work was interrupted on designer/Claude STOP message #523. No five-run success claim. Scripts/test_map.json knownFail remains unchanged. Smoke-name validation and any remaining Retreat smoke checks are still pending. No gameplay, assets, or weapon tuning changed.

Next after full reset: verify/rebuild the WIP branch, run RetreatFacingSmoke -NoRetry -Jobs 1 five times with preserved actual logs, remove only its knownFail entry if all pass, check_smoke_names.py, then finalize report and send one result line to Claude.

Routing: 2 | gpt-6.1-sol | WIP (limit stop before validation). Reset communicated for 2026-11-16 13:08 Asia/Jerusalem (11:08 UTC). No local model used.
