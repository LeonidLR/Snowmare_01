# Codex 140 (L3; Lead reviews) - control groups: split the squad into sub-squads, formation per group (designer 2026-11-26, asked before)

Designer: select 1..5 operatives (click / box select), Ctrl+1 assigns them to group 1 (Ctrl+2 / Ctrl+3 ... likewise); F1 / F2 / F3 ... switches to
that group. While working with a group, EVERY squad order applies only to that group - in particular B (formation) forms only that group: a
group of 2 gets a 2-man formation, of 3 a 3-man, ... of 5 a 5-man; a group of 1 is solo. Example: 6 operatives as 3 groups of 2 on F1 / F2 / F3,
F1 + B -> those two form up; the other groups keep their own state. A split 2 + 1 -> group 1 = 1 man, group 2 = 2 men.
Rules:
- An operative belongs to at most one group; Ctrl+N with a selection moves those operatives into group N (out of their old group). Empty groups vanish.
- F-key: selects the group, its leader = the group's first member (or the last leader picked in it), camera jumps to it (double tap = follow).
- Formation (formation shapes, confined queue, fence / ladder queues, rear guard, places by travel) must work for any group size 1..6 and
  run independently per group (two groups in formation at once). Group of 1 = Solo behaviour.
- Keys taken today: 1-4 select member, 5 / 6 lure, F5 quicksave, F6 map panel, F7 full map, F8 controls, F9 quickload. Group keys: see open question.
- HUD: group number next to each operative's name in the roster; the active group highlighted.
- Save / load keeps the groups.
DECIDED (designer 2026-11-26): at most 4 groups, Ctrl+1..4 assign, F1..F4 select; F5+ keep their current keys.
- Pure rules (group membership, reassign, leader pick, per-group formation slot count) -> unit tests. Smoke: X6, 3 groups of 1-2, F1+B forms
  only group 1, F2 orders move only group 2, both groups in formation at once.
- Branch codex/140-control-groups from origin/main; report docs/port/codex/outbox/140-control-groups.report.md; FINISH `140 FINISH | ...`. Queue: after 138, before G022. TypeSafe audit: local boundary audit only.
