# Gemini 105 - X6 TRAINING SECTORS (DESIGNER PRIORITY; replaces the "new small map" version of this file)

Designer 2026-11-24 ~15:55: X6 is too big for 3 v 3 ("just wandering looking for enemies"). Instead of a new map, split X6
(/Game/Maps/Generated/L_Gen_Depot_1_X6) into 6 SECTORS - his drawing: docs/port/gemini/inbox/105-x6-sectors-designer.webp
(editor top view, red rectangles: 3 columns x 2 rows, all of the built area). A fight happens inside ONE sector only.
No new map; the map, props and his X6 edits stay untouched.

You do the sector DATA (Codex 106 does the runtime boundary walls + sector choice in C++ - agree the JSON shape with it via chat):
1. Bounds: turn the 6 drawn rectangles into X6 world boxes in cm (use the layout Content/Data/LevelGen/accepted/
   gen_ref_01_02_depot_1_x6.json / the props file / the PmcArena play_area on origin/ai/x6-arena as the reference frame).
   Snap each edge to a street / open strip so no building is cut in half (say which you moved and why).
2. Per sector: player squad start (3), one PMC squad of 3 (pmc_commander.json) on a post 40-60 m away inside the sector,
   posture that makes first contact < 90 s; list what the sector holds (enterable buildings, cover kinds, fences, ladders).
3. JSON: proposal = a "sectors" array in Content/Data/LevelJson/L_Gen_Depot_1_X6.json (id, name, box_cm, player_start,
   pmc_squad) + "active_sector" default; or one LevelJson file per sector if the loader makes that simpler - your call, explain.
   X6 keeps its current behaviour when no sector is active.
4. A top-down PNG of X6 with the 6 boxes, ids, starts and PMC posts (outbox) - the designer approves it from his phone.
5. Check (worker only, after 106 lands): per sector 5 seeds, first-contact time, no unit leaves the box, no stuck units.
Branch gemini/105-x6-sectors. Pause 102 (commit WIP), return to it after. Report in outbox + chat FINISH. Never push.

NUMBERING (designer): top row left->right = sectors 1, 2, 3; bottom row left->right = 4, 5, 6 (as seen in his top view).
Use these ids everywhere (JSON id, Codex.Sector N, PNG labels, recordings, reports: "training in sector 5").
