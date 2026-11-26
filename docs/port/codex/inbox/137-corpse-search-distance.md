# Codex 137 (light; Lead reviews) - corpse search: the searcher stands too far from the body (designer playtest 2026-11-26, X6)

Searching a fallen enemy or squad mate (Gemini 130 corpse search, 6dd83864 + 130b): the operative stops ~1.5-2 m from the body and "searches" from there.
He must walk up to the body: stop at arm's reach (~60-80 cm from the torso, beside it, not on it), face the torso, then play the search.
- Find the move/accept radius used by the search order (CorpseSubsystem / OperativeCharacter search order / loot interaction) - likely the generic
  interact acceptance radius. Use a dedicated search reach; pure rule + unit test (stop point beside the torso, reachable on the navmesh, fallback
  to the nearest reachable point if the body lies against a wall).
- Smoke: extend the corpse-search smoke (or add a step) asserting searcher-to-torso distance <= 90 cm at search start.
- Branch codex/137-corpse-search-distance from origin/main; build via machine_dispatch.py / build.ps1 -Remote worker; report docs/port/codex/outbox/137-corpse-search-distance.report.md;
  FINISH `137 FINISH | PASS/FAIL | <hash> | ...`. Queue: after 136, before G022. TypeSafe audit: local boundary audit only.
