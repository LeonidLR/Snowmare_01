# 019 - Training Ground: "Commit" button for the designer's data + remove the Level Design tab (designer 2026-11-11)

TypeScript / node dev-server only (Tools/TrainingGround, server/*Api.ts pattern), branch `gemini/019-tg-commit-tabs` from main.
After 018. Report to Codex; Claude is offline. Security matters here (the props panel once had a path-traversal hole) - read 3.

1. **Commit button** on the pages that save game data (formation editor, HUD labels, props, levelgen saves, movement):
   a server endpoint `POST /api/git/commit` that commits ONLY files from a fixed whitelist the TG itself writes
   (Content/Data/FormationLab/formation_shapes.json, Content/Data/UI/hud_labels.json, the props / levelgen output folders the TG saves
   to - list them exactly in the report). Never Content/Maps, .uasset, Config, Source, and NEVER Content/Data/LevelJson/L_Gen_Depot_1.json
   unless the designer ticks it explicitly in the dialog.
   UI: the button shows a dialog with the changed whitelisted files + `git diff --stat`, a pre-filled message
   `data(<area>): designer edit via Training Ground (<date>)`, Confirm / Cancel. **Commit only - no push** (Claude / the integrator
   pushes; the pre-push guard allows only Claude). Show the resulting hash. If the index already has other staged files, refuse.
2. **Remove the "Левел-дизайн" tab** from the top navigation (the designer: no use any more). Before deleting, check what else uses its
   code / validators (redteam layout fixtures, npm tests, the level generator); keep anything still referenced (e.g. move the layout
   check into "Генератор уровней" if useful) and delete the rest. Say in the report what was removed and what was kept.
3. Security: same-origin check like Scripts/Tools/agent_chat_server.py (no CORS *), paths resolved and compared against the whitelist
   after normalisation (no `..`, no absolute paths from the client - the client sends area ids, not paths), git called with an argument
   array (no shell string). Tests: whitelist, traversal attempts, staged-foreign-file refusal, message escaping; npm test + typecheck.
