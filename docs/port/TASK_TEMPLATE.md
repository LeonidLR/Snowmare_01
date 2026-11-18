# Agent task template (inbox) + lead review checklist

Adopted 2026-11-18 (designer approved the process review: a design gate, one definition-of-done template, reviews with context).
Every task for Codex / Antigravity / Grok / an API agent is written from this template - copy it to
`docs/port/<agent>/inbox/NNN-<slug>.md` and fill the `<...>` parts. Do not drop a section; write "none" if it does not apply.

---

```markdown
# NNN - <one-line goal in player terms> (<GAME fix | TOOL | DOCS | TEST>) - <LIGHT | MEDIUM | HEAVY>, <C++ | TS | Python | docs>
Limit rule: at 10 % of your <daily | weekly> limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main <short sha or "newest">. Branch <agent>/NNN-<slug>. Worktree <CodexTactics-agents | -gemday | -grok | -claude>.
Before switching branches in the worktree: no UnrealEditor-Cmd / build may run on it.

## Design gate (lead fills BEFORE dispatch)
- Spec: <doc + section, e.g. docs/design/PMC_DEFENCE.md D1> - if there is none, write the 3-6 acceptance lines HERE.
- Acceptance (what the designer would see): <...>
- Proved by: <smoke / unit test name + the exact check line(s)>.
- Designer decision needed? <none | the open fork - then the task does not go out until he answers>

## Evidence so far
<the red line from the log / report, where it was seen, earlier rejected attempts and WHY they were rejected>

## Task
1. Diagnose with evidence (log lines, numbers) before changing code.
2. Fix it in <the game | the tool>. Numbers in the existing JSON config, no new literals. Pure logic -> rules function + unit test.
3. Stop rule: if the cause is bigger than ~<N> h, write the diagnosis + the next exact step and commit WIP - a good result too.

## Never
- Change / weaken a smoke or test check, its timing or its fixture to make it pass (unless the task IS the smoke).
- Raise a budget (ms, MB, %) to make a check pass.
- Push or merge main; touch maps, Content/VFX, Config/DefaultScalability.ini, the designer's uncommitted files.

## Gate (all before FINISH)
build: `Scripts/build.ps1 -Remote worker` (exit 2 -> local) | smokes: <named list> via `Scripts/smoke_parallel.ps1` (default -Remote auto)
| `Scripts/test.ps1 -Smart` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent <agent>` | `git diff --check`.
Allowed paths: <explicit list>.

## Report
`docs/port/<agent>/outbox/NNN-<slug>.report.md`: handover (done / left / next exact step), cause with evidence, what changed (file:line),
gate results with log paths, before -> after numbers. FINISH one line in chat topic `<agent>-NNN`:
`NNN FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
```

---

## Lead review checklist (before every merge of agent work)

1. Read the diff myself first; note my own concerns.
2. API review (`Scripts/Tools/api_task.py`, Opus, `--effort low`) gets the diff **plus the full touched functions and their callers**
   (not the diff alone), and the prompt asks it to mark claims the shown context does not prove as `UNVERIFIED`.
3. Verify EVERY finding against the code before applying it (11-18: 2 of 6 findings on 046 were false; applying one unchecked broke
   PeekMinesSmoke). Rejected findings are named in the merge / fix commit body with the reason.
4. Re-run the task's smokes after any lead fix; never merge on the agent's gate alone when I changed code.
5. Merge `--no-ff` with a body naming the agent, the gate and the reviews; TASK_BOARD done-list row; FINISH acknowledged in chat.
6. Main build only when the designer's editor is closed; otherwise say in the merge report that main is merged-not-built.
