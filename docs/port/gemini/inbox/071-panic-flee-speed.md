# 071 - Finish Codex 013: panic flee speed respects stance and wounds (board 062) - taken over from Codex (limit spent)
Суть: паникующий боец больше не «уползает как одержимый»: лёжа ползёт со скоростью ползания, раненый — с поправкой на ранение.
Реестр: panic
Base: codex/013-panic-speed (51ef0afb, Codex's diagnosis checkpoint, no gameplay edits yet); your branch gemini/071-panic-speed
(`git switch -c gemini/071-panic-speed codex/013-panic-speed` in CodexTactics-interim). Do it AFTER 068, BEFORE 070.

Read first: the full task docs/port/codex/inbox/013-panic-flee-speed.md (goal, allowed paths, checks - they apply to you unchanged)
and Codex's diagnosis in docs/port/codex/outbox/013-panic-flee-speed.report.md ON THAT BRANCH
(`git show codex/013-panic-speed:docs/port/codex/outbox/013-panic-flee-speed.report.md`).
Codex's next steps: implement the PanicFleeSpeed rule + tests, stop the forced-standing and the config-override bypass, add the
prone-wounded measured smoke, run the gates. Verify his diagnosis before building on it (quote the file:line that proves it).
Checks: as in 013 + `gemini_gate.ps1 -Task 071 -SonnetReview`.
Report: docs/port/gemini/outbox/071-panic-flee-speed.report.md (Claim | Evidence table, Russian).
