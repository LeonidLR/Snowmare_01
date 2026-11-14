# 036 - The gate: one command that checks your own work before "done" (Claude, 2026-11-12)
Суть: автоматическая проверка работы Gemini перед словом «готово» - меньше ошибок и меньше ручной проверки Claude.
Реестр: -
Base: main; branch gemini/036-gate. Small - do it NOW (pause 034 after its current commit), then finish 034 and gate it.

## Goal
`powershell -File Scripts/Tools/gemini_gate.ps1 -Task <NNN> [-Worktree <path>] [-SkipBuild]` prints a short PASS / FAIL table:
1. **Location:** runs only in a worktree on a `gemini/<NNN>-*` branch; FAIL if run in the main checkout.
2. **Allowed paths:** parses the task file's "Allowed paths" (docs/port/gemini/inbox/<NNN>-*.md) and FAILs on any changed file
   (vs the base branch) outside them; always FAIL on Content/Maps, Config, Content/VFX, .uasset, packs, L_Gen_Depot_1.json.
3. **Build + tests:** build.ps1 (if C++ changed), `test.ps1 -Smart`, `npm test` + tsc if Tools/TrainingGround changed, Python
   unittest for changed Scripts/Tools; paste the last lines.
4. **Jev audit:** typesafe_triage.py --audit-diff --agent gemini.
5. **Citation check:** every `path:line` and every repo path mentioned in the task's report must exist (line <= file length);
   list the broken ones -> FAIL.
6. **Evidence check:** the report has the "Claim | Evidence" table and no empty evidence cell.
7. Optional `-SonnetReview` (C++ diffs only): sends the diff to `Scripts/Tools/api_task.py --model claude-sonnet-5-5 --effort medium
   --label gemini-review-<NNN> --budget-usd <cap from docs/port/PLAN_WEEK_*.md>` with a short reviewer brief; saves the answer next
   to the report; FAIL only on findings marked HIGH. Never print keys.
Output also saved to Saved/Orchestrator/gate/<NNN>-<time>.md. Unit tests for 2, 5, 6 on fixtures (Python helper).
