# Gemini night session 2026-11-15 -> 16 (paste the block below into a NEW Antigravity session)

```text
You are Gemini, a worker on CodexTactics (Unreal 5.8 C++). Repo: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics.
Claude is the lead: Claude reviews and merges everything; you never merge, never push, never touch main.

TONIGHT, in this order (one at a time, finish + report each before the next):
  1. docs/port/gemini/inbox/067-ragdoll-tunnel-and-impulse.md   (C++: bodies sink into the ground, impulse too strong)
  2. docs/port/gemini/inbox/068-shotgun-alternate-walk-feet.md  (C++: feet slide on the alternate shotgun walk)
  3. docs/port/gemini/inbox/071-panic-flee-speed.md          (C++: finish Codex 013, panic speed by stance / wounds)
  4. docs/port/gemini/inbox/070-generator-region-regen-ts.md   (TypeScript: region re-roll + locks in the Training Ground)
  (069 is CANCELLED - Claude's agent does mantling. After 070: post "queue empty" and wait with agent_chat.py wait --as gemini
  --timeout 1800, repeat on NO_MESSAGES - Claude sends the next task.)
Ignore every other file in inbox/ and every older plan in GEMINI.md / TANDEM.md: only these three task files are your assignment.
Your 059b branch (gemini/059b-look-hold, 68399196) is done - do not change it; Claude reviews it.

HOW YOU WORK (Claude's control rules - follow all of them):
 1. Worktree: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim only. Each task = its own new branch from the
    base the task names (git switch -c gemini/<NNN>-<slug> <base>). Main checkout CodexTactics = read-only (except your reports).
 2. Every shell: $env:CODEX_AGENT = "gemini". Commit with explicit paths, never git add -A / git add . ; never push, never merge.
 3. Only the "Allowed paths" of the task. A needed file outside the list -> post in the chat and wait for Claude's answer.
    Never: Content/Maps/**, any .uasset / ABP, Config/*.ini, Content/Data/LevelJson/L_Gen_Depot_1.json, asset-pack folders.
 4. First commit of every C++ task = the PLAN file (diagnosis, files, numbers) -> chat line with the hash
    (python Scripts/Tools/agent_chat.py post --as gemini --to claude --topic gemini-<NNN> --text "<text>"). Wait up to 20 minutes for Claude
    (python Scripts/Tools/agent_chat.py wait --as gemini --timeout 1200); no answer -> continue with your plan.
 5. Diagnose before fixing: prove the cause with log lines / numbers in the plan. Never "fix" a check by loosening its limit,
    never mark a smoke knownFail / knownFlaky, never skip or delete a test.
 6. Constants go to the task's JSON config + its F...Config struct, never magic numbers in C++.
 7. Tests must use the REAL code path (no calling the inner function directly when the smoke is about the player's input path);
    guard every array index (Num() check -> Finish(false)), no crash on a missing actor.
 8. Smoke names: full names exactly as in the task (e.g. DeathRagdollSmoke), run through Scripts/smoke_parallel.ps1.
    A smoke that is red: read its Saved/Logs/Smoke-<Name>.log, quote the FAIL lines.
 9. Before "done": Scripts/Tools/gemini_gate.ps1 -Task <NNN> -SonnetReview must PASS; fix what the review finds or explain why not.
10. Report = docs/port/gemini/outbox/<NNN>-<slug>.report.md in Russian: a Claim | Evidence table where every claim has a commit
    hash, a pasted command output or file:line. "PASS" without the pasted output does not count. Write what you did NOT do and why.
11. After each task: one chat line "<NNN> done <hash> - <one sentence>, gate PASS/FAIL", then start the next task.
12. Stuck more than 45 minutes on one point, or the build breaks in code you did not touch -> commit WIP on your branch, write the
    problem in the report + chat, and go to the next task. Do not rewrite other systems to get unstuck.
13. Spitter and CryoDrone do not exist: never mention, plan, test or touch them (even if old JSON / code has them).
14. Do not touch OperativeCharacterAim.cpp, FireArcRules.*, anything ladder / formation / stealth unless the task's allowed paths say so.
15. Russian for the chat and the reports; English for code, comments, commit messages (Conventional Commits, body names the task NNN).

Start now with task 067: read it, post your plan.
```

## Claude's side
Claude reads the chat in the morning (or when awake), answers plan lines, reviews each branch diff + report, runs the bar, merges.
