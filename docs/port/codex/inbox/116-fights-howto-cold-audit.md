# Codex 116-fights-howto-cold-audit (night, read-mostly)

1. HOW-TO for the designer (RUSSIAN, short, phone-readable, max ~25 lines): docs/port/FIGHTS_HOWTO_RU.md on origin/main 75b703bc.
   How to start recorded 3 v 3 fights in an X6 sector this morning: open X6 in the editor / game, choose the sector (Training Ground tab
   "Тренировка ИИ" -> "Сделать активным", or the console Codex.Sector N, or -Sector=N), turn recording on (Codex.Demo.Record ... - check
   the real command and where files land: Saved/Demos), what a round looks like (wipe -> 8 s -> new start/post, HUD "Раунд N"), ammo
   (finite + resupply after 20 s out of fire), where to change round / ammo numbers (L_Gen_Depot_1_X6.sectors.json fields), how to go back
   to the whole map (Codex.Sector 0). Verify every command name in the code, do not guess.
2. COLD AUDIT (designer: cold fully OFF for now): Grok G018 saw "weapon frozen after frostbite" in a door smoke on the Showcase Kit.
   List every place cold can still apply on the 5 kept maps: LevelJson cold_rate_mult per map, smokes that force cold on, defaults in
   C++ / data when a LevelJson has no cold_rate_mult, frostbite / weapon freeze paths independent of the rate. Report; fix only data
   (LevelJson cold_rate_mult 0 with _cold_paused note, minimal line edits) where a kept map still has cold; C++ changes -> describe only.
Branch codex/116-howto-cold. FINISH as usual.
