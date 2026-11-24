# Codex 107-tg-ai-training-tab (after 106; fold 104 into it)

Designer 2026-11-24: a new Training Ground tab "Тренировка ИИ" (AI training) so he controls where the bots train:
1. Map picker: only the 5 kept maps (StoryShowcase_Kit, Depot_1_X6, Demo01_A_FrozenLake, VehicleTest, MainMenu) - this also
   fixes 104 (the stale L_Gen_Bgen_Test_X6 entry; move its stale props / tactical files to the archive, do not delete).
2. Sector picker for maps that have "sectors" in their LevelJson (X6: 1-6, top row 1 2 3, bottom row 4 5 6 - Gemini 105 data,
   Codex 106 runtime): the top-down map with the 6 boxes, numbers, player start and PMC post drawn; click a box to select it.
3. "Set active" writes active_sector into the map's LevelJson (0 = whole map); per sector show its contents (buildings,
   covers, fences, ladders) and its PMC squad (count, posture), editable count / posture.
4. Show which sector the last recorded sessions / arena runs used (Saved/Demos summary via Codex 075 demo_summary.py if cheap).
Cold is OFF on X6 / Showcase / Lake (cold_rate_mult 0, designer: fights only) - the tab must not change cold_rate_mult.
Branch codex/107-tg-ai-training-tab, TG tests + build of the TG; screenshot of the tab in outbox for the designer.
