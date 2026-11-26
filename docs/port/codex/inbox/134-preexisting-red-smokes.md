# Codex 134 (light; Lead reviews) - two smokes red on origin/main itself: FormationStairSmoke, WindowStairHoverSmoke (Lead 2026-11-26)

Both fail on base main in every merge gate since 11-25 (accepted as PRE-EXISTING) - they must go green again so the gate stops needing -AcceptPreExisting.
1. Reproduce each on origin/main (f25a9b2c+): `powershell -ExecutionPolicy Bypass -File Scripts/smoke_remote.ps1` / worker_sync per TANDEM, or locally
   via Scripts/Tools/machine_dispatch.py smoke --lane <your worktree> --smokes CodexTactics.FormationStairSmoke,CodexTactics.WindowStairHoverSmoke.
2. For each: find the first failing check in the log, decide SMOKE BUG (map / ids / timing changed) or GAME BUG; bisect with git if needed
   (good candidates: G026 f43e2f69, G025 9f953e50, 127 3c3159e6, 113 FormationStair fix).
3. Smoke bug -> fix the smoke. Game bug -> smallest fix + unit test if pure logic. Never weaken a check just to pass.
4. Run each smoke 3x green. Branch codex/134-red-smokes from origin/main, report docs/port/codex/outbox/134-red-smokes.report.md.
Rules: build only via machine_dispatch.py or Scripts/build.ps1 -Remote worker; never touch the main checkout or CodexTactics-play; at 10 % limit left:
commit, post status, stop. FINISH line in chat: `python Scripts/Tools/agent_chat.py post --as codex --to claude --topic codex-134 --text "134 FINISH | PASS/FAIL | <hash> | ..."`.
