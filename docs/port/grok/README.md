# Grok mailbox (Claude -> Grok, from 2026-11-17)

Grok (xAI) joins CodexTactics as a WORKER (it takes over from Codex, whose subscription ends). Claude is the lead: Claude writes the tasks,
reviews every branch (diff + build + tests + smokes) and is the ONLY one who merges into main and pushes. Other workers: Gemini in
Antigravity (day worker, worktree CodexTactics-gemday), Gemini CLI (night only), Claude's own agents. The designer (the user) plays and
tests in the main checkout.

Project: CodexTactics - a tactical arctic survival game, Unreal Engine 5.8.2, C++ first (no GAS), Windows. Repo (main checkout, READ ONLY
for you): `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics`. Read `CLAUDE.md` and the top of `docs/port/HANDOFF.md` first.

| Folder | Who writes | What |
|---|---|---|
| `docs/port/grok/inbox/G<NNN>-<name>.md` | Claude | one task per file: goal, files, allowed paths, the exact checks that must pass |
| `docs/port/grok/outbox/G<NNN>-<name>.report.md` | Grok | written on YOUR branch: what changed, why, evidence, test / smoke results, what is NOT done |

## Where you work
- ONLY your worktree: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-grok` (made for you, branch `grok/base` = main,
  not built yet - the first build takes ~10-15 min). Each task = its own branch from the newest main: `git switch -c grok/G<NNN>-<slug> main`.
- Never write in: the main checkout, `CodexTactics-agents`, `CodexTactics-gemday`, `CodexTactics-gemcli`, `CodexTactics-claude`.
- Every shell: `$env:CODEX_AGENT = "grok"; $env:PYTHONIOENCODING = "utf-8"`.

## Hard rules
1. NEVER push, merge, rebase main, or touch `main`. A pre-push hook blocks main for everyone but Claude.
2. Commit with explicit paths (never `git add -A` / `git add .`), Conventional Commits, the body names the task (G<NNN>), small commits;
   commit after every working step (never more than ~1 hour uncommitted - your limit can end mid-task).
3. Only the task's "Allowed paths". A file outside them -> ask in the chat first and wait for Claude.
   Never: `Content/Maps/**`, any `.uasset` / Animation Blueprint, `Config/**`, `Content/VFX/**`, `Content/Data/LevelJson/L_Gen_Depot_1.json`,
   `Content/Data/LevelGen/props/L_Gen_Depot_1.props.json`, the designer's asset packs, `CLAUDE.md`.
4. No editor: never open / save assets. Command line only:
   - build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (5-15 min; ONE engine build runs at a time on this PC - your build may
     queue behind others, just wait, never kill another process);
   - unit tests: `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.<System>` (or `-Smart`);
   - smokes (in-game checks): `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands <FullName>`
     with FULL registered names (e.g. `MovementSmoke`, list in `Scripts/test_map.json` "smokeRegistry"); a red smoke: read
     `Saved/Logs/Smoke-<Name>.log` and quote the FAIL lines. `-Multiprocess` is required.
5. Diagnose before fixing: prove the cause with log lines / numbers. NEVER "fix" a check by loosening its limit or shrinking what it
   measures, never mark a smoke knownFail / knownFlaky, never skip or delete a test. A changed expectation needs the reason in the commit.
6. Numbers go to the system's JSON config + its config struct, not magic literals in C++. Epic C++ standard (prefixes, IWYU, TObjectPtr).
7. Before "done": `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` must PASS.
8. Spitter and CryoDrone do not exist: never mention, plan, test or touch them.
9. The designer has priority: the build / smoke scripts wait while his editor starts or a Training Ground map build runs - let them.

## Chat (how you talk to Claude and the others)
- Read: `python Scripts/Tools/agent_chat.py read --last 10`
- Post: `python Scripts/Tools/agent_chat.py post --as grok --to claude --topic grok-G<NNN> --text "<one line>"`
- Wait for an answer: `python Scripts/Tools/agent_chat.py wait --as grok --timeout 1800` (repeat on NO_MESSAGES). Never redirect its output
  to a file / null - it would swallow the message.
- Post only real results (with a commit hash) and real questions - each post wakes Claude. One line per real step is fine.
- After each task ONE FINISH line: `G<NNN> FINISH | status | last commit | done | NOT done | needs Claude | gate | report`.
- TOPIC_LIMIT ("ask the designer"): stop posting in that topic and wait - only the designer can unblock it. Never switch to a new topic to
  get around it.
- Russian for the chat and the reports; English for code, comments and commit messages.

## Workflow per task
1. Read the task file in `docs/port/grok/inbox/` (from the main checkout) + the files it names.
2. FIRST commit on the task branch = a short PLAN `docs/port/grok/outbox/G<NNN>-<slug>.plan.md` (diagnosis, files, how you will prove it)
   -> chat line with the hash -> wait up to 20 min for Claude (no answer -> continue with your plan).
3. Implement in small commits; build; tests; smokes from the task's gate.
4. Report `docs/port/grok/outbox/G<NNN>-<slug>.report.md` in Russian: Claim | Evidence table, every claim with a commit hash, pasted
   command output or file:line. "PASS" without pasted output does not count. List what you did NOT do and why.
5. FINISH line in the chat, then wait - Claude reviews, merges or answers with fixes / the next task.
6. Stuck > 45 min on one point, or the build breaks in code you did not touch -> commit WIP, write the problem in the report + chat,
   ask Claude. Do not rewrite other systems to get unstuck. If a rule or task looks wrong, say so in ONE chat line and continue.

## Model router (designer 2026-11-17 - ALWAYS)
Before each task and each sub-part run `python Scripts/Tools/model_router.py --classify "<one-line task>"`. It returns a level 1-4
(model-agnostic; ignore the Claude model names it prints):
- level 1-2 (trivial / routine: data / JSON edits, renames, docs, logs, test boilerplate, small helpers, reading logs) -> your CHEAPER / faster
  model (e.g. grok-code-fast or a mini model - say which one);
- level 3-4 (C++ logic, diagnosis across systems, AI / movement / animation code) -> your main / strongest model.
Log every sub-part in your report as `level | model | result (pass / redo)`. If the cheaper model's work has to be redone often, say so.

## Limit rule (designer)
At 10 % of your token / quota limit LEFT: stop, commit (WIP ok), update the report, post ONE line
`--topic grok-limit "limit 10%: <state>, last commit <hash>, next <step>, reset at <time>"`, then only wait until the limit resets.
