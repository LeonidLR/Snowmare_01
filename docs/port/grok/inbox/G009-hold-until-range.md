# G009 - A man on a post waits until the enemy is in HIS range (smart enemies T-06)
Dispatched by the lead 2026-11-19 (designer approved T-01..T-12: "Да, передавай").
The full task (design gate, evidence, task, never, gate, allowed paths) is section **T-06** of `docs/design/gdd/smart-enemies-roadmap.md`
(section 6 "Tasks"), plus the "Common to every task" lines just above T-01 there. Read the GDD
`docs/design/gdd/smart-enemies.md` parts that section names. That text is binding; this file only fixes the number and branch.
Branch `grok/G009-hold-until-range`. Worktree `CodexTactics-grok`. Base: origin/main newest.
Limit rule: at 10 % of your limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Report: `docs/port/grok/outbox/G009-hold-until-range.report.md`; FINISH line in chat topic `grok-G009` (TASK_TEMPLATE format).
Model: grok-4.7-build-fast (cherry-pick of 07c27808 + 089c480e from grok/G008b-overwatch-hold; nothing else from that branch).
