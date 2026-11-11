# 015 - Evening queue (Claude, 2026-11-11) - all items in order, no chat wait between them

Rules as always: own branch per item (`gemini/015-<n>-<slug>`), from `interim/2026-11-12` for items 1-2 and from `main` for 3-4.
Read-only reports in docs/port/gemini/outbox/ (item 1 may also write ONE data draft file next to the report). Never C++, Source,
Config, Content, maps, .uasset, main, push. Chat: only "item N done <hash>" or a real question.

1. **Level preload manifest (for Codex 004).** Read docs/port/codex/inbox/004-hitch-on-enemy-appear.md (all follow-ups) and your
   014 report. For L_Gen_Depot_1, list EVERYTHING that can be spawned during play: every enemy class the Pressure / ambush /
   PmcEncounter / horde tables (Content/Data/**.json) can pick, with its skeletal mesh(es), weapon meshes, anim BP / layer clips,
   ladder clips, materials; plus the operatives' full clip set (weapon_anim_sets.json all blocks incl. stealth). Output a draft
   `docs/port/gemini/outbox/015-preload-manifest.draft.json` (asset paths grouped by owner, with the JSON key each comes from) +
   report: counts, which entries are resolved per spawned character today (file:line), and where the 2 s SK_Cutter_1slot wait comes
   from (which enemy / weapon uses it).
2. **Second review of the stealth runtime** on interim/2026-11-12: commits 90a6bf64 + 4d15a54f (Codex) - same method as your
   e3c05788 review: file:line, trigger, expected / actual; check that your earlier HIGH / MEDIUM findings are really fixed.
3. **Door breach #23 - implementation map (read-only).** docs/design/DOORWAY_BREACH.md (answers 2026-11-11) + your 011 plan: exact
   existing classes / functions to extend (doors, interactables, orders, formation, smokes), insertion points, data keys, the smoke
   list per step. Mark what is new code vs reuse.
4. **Mortars - prep (read-only).** Find every mortar mention (design/, docs/, board, register, SQUAD_SIX_FIRE_TEAMS.md); report the
   agreed design, open questions for the designer (Russian, each with a recommended default), and the existing systems a mortar
   crew would reuse (grenade arcs, suppression, squad cells, orders).
5. Tomorrow morning: night trainer review (012 item 4).
