# Codex 143 (Lead reviews) - red threat arrows + last known enemy position (designer 2026-11-26)

Designer: fights start fast, he cannot see where the fire comes from. Wanted: a red arrow pointing towards the enemies firing at the squad (as in
modern tactical games); if the enemies fired and moved away, the arrow points at their LAST KNOWN position.
Proposal (Lead; keep it simple, tunables in DA_GameBalanceConfig):
- Trigger: an operative is hit OR a shot passes near him (suppression / near miss) -> a red arrow on the ground ring of THAT operative, pointing
  at the shooter (direction is known from the shot even if the shooter is unseen). One arrow per shooter, merged when shooters are within ~10 deg.
- Off-screen: if the shooter is off-screen, also a red arrow at the screen edge in his direction.
- While the shooter is visible: solid red arrow, follows him. When he leaves sight: the arrow turns hollow / dashed and points to his LAST KNOWN
  position (reuse the sight memory: [Sight] ghosts / last known position); a small red marker on that spot (and on the minimap). Fades after
  ~20 s without new contact, or on contact again (back to solid).
- Fog of war rule: we never reveal an unseen enemy's CURRENT position - only the shot direction and the last known spot.
- Pure rules (merge by angle, solid -> last-known -> fade timeline) -> unit tests. Smoke on X6: PMC fires at the squad -> arrow appears for the
  hit operative pointing at the shooter (angle err <= 10 deg); shooter relocates out of sight -> arrow points at the last known spot; fades by ~20 s.
- Branch codex/143-threat-arrows from origin/main; report docs/port/codex/outbox/143-threat-arrows.report.md; FINISH `143 FINISH | ...`.
  Queue: after 142. TypeSafe audit: local boundary audit only.
