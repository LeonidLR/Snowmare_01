# Who does what (designer decision 2026-11-12, review 2026-11-19)

**Update 2026-11-12 afternoon:** Codex removed. C++ now: Gemini (well-specified systems on named paths) + Claude via Sonnet (cross-system / half-finished / risky parts). Claude also integrates Gemini's branches (interim -> main).

Six resources. Rule: the cheapest one that can do the job reliably; the expensive ones only where they are irreplaceable.

| Resource | Role | Gets | Never |
|---|---|---|---|
| **Claude (Max subscription)** - scarce, protect | Orchestrator + gatekeeper | merges into main, final reviews (diff skim + tests), architecture / design docs, hard bugs nobody else cracked, designer questions | routine code, reading big logs, babysitting chat (the dispatcher does it) |
| **OpenAI Codex** | LEFT for good 2026-11-19 (designer); earlier REMOVED 2026-11-12 (designer: limits drain too fast) | his work handed over (interim/2026-11-12 + docs/port/codex/outbox/004 report) | - |
| **Gemini (Pro, Antigravity)** | Reader + TS / Python + reviewer | implementation maps, code reviews of Codex branches, Training Ground (TS), Python tooling, docs (RU guides), log / trainer analysis, merge plans; C++ only as approved trials on named paths | C++ outside an approved trial, main, push |
| **Anthropic API** (designer's credit, ledger cap) | Second opinion + gap filler | Sonnet: independent reviews of risky diffs (~0.1-0.3 $), patch drafts when Codex is out of limit; Opus: rare design drafts. Always `--label`, `--budget-usd` | bulk reading, anything a subscription can do now |
| **Jev (TypeSafe)** | Cheap judgment | boundary audits before merges, `test.ps1 -Smart`, telemetry digests, model-router classification, typed yes / no checks in tools | C++ / syntax / boilerplate |
| **Local model** | REMOVED 2026-11-12 (designer: 18 GB is too much for message sorting) | the dispatcher sorts messages by deterministic rules; a tiny model (1-2 GB) only if the rules prove too weak | - |
| **Designer** | Decisions + feel | design choices, playtests, Animation Blueprints / shaders polish, logins / installs / admin prompts | - |

## Flow of a feature
1. Designer idea -> Claude (or Gemini) writes the task with checks.
2. Gemini: implementation map (read-only) -> Codex implements (C++) -> Jev audit + `-Smart` tests + smokes.
3. Gemini reviews the branch; risky diff -> Sonnet API second opinion.
4. Codex merges into interim; Claude reviews interim and merges into main (weekly or per milestone).
5. Local model writes the RU summary / changelog; Claude checks it in one glance.

## Token rules
- Claude wakes only for: designer messages, questions / blockers, merges. Watchers / the dispatcher filter the rest.
- Big logs: never raw - Jev telemetry or a script.
- Each agent commits after every working part; limits ending = commit + one chat line first.
- Live task board (`Tools/AgentChat/index.html` tab `Задачи`): every chat message about a task MUST pass `--topic <task-id>` (e.g. `codex-008`, `codex-004`, `gemini-030`, `gemini-031`) so the board links chat activity automatically.

## Task ownership moves (read by `Scripts/Tools/task_board.py`)
Syntax: `<owner> <task> -> <new owner>`, one per line. Designer re-plan 2026-11-17:
- gemini 081 -> codex 043 (docs/port/codex/inbox/043-ramp-fight.md)
- gemini 078 -> grok G004
- gemini 079 -> grok G005
- gemini 080 -> claude (review + merge)

- grok G003 -> claude (designer 2026-11-17: generator testing tomorrow; Grok busy with G002)

- codex 042 -> grok G006
- codex 043 -> grok G007
- codex 049 -> claude T004 (Codex left 2026-11-19; Sonnet subagent continues from its WIP)
- codex 050 -> claude T012 (after T04, Sonnet subagent)
- gemini 088 -> claude (Antigravity unavailable 2026-11-19; Sonnet subagent finishes its WIP)
- claude (lead) -> grok G010 (2026-11-19: PmcRoofDefence 6/10 flake after G009)
- claude (lead) -> gemini 092 + 093 (2026-11-20: TG building move carries props / tactical; paired A/B digest)
- claude (lead) -> gemini 089 -> 091 -> 094 -> 090 (2026-11-20, designer «Да, передай»; 092b tactical carry later)
- claude (lead) -> grok G011 then G012 (2026-11-20: arena dead metrics; G012 then done by claude - designer: Grok budget 83 %)
