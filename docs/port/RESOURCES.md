# Who does what (designer decision 2026-11-12, review 2026-11-19)

Six resources. Rule: the cheapest one that can do the job reliably; the expensive ones only where they are irreplaceable.

| Resource | Role | Gets | Never |
|---|---|---|---|
| **Claude (Max subscription)** - scarce, protect | Orchestrator + gatekeeper | merges into main, final reviews (diff skim + tests), architecture / design docs, hard bugs nobody else cracked, designer questions | routine code, reading big logs, babysitting chat (the dispatcher does it) |
| **OpenAI Codex (Pro, 1 month)** | Main C++ implementer | all gameplay C++ / AI / anim layers / smokes on codex/* or interim branches; integrates Gemini branches into interim; uses the router + local model for small parts | main, push, maps / Config / packs; holding work uncommitted > 1 h |
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
