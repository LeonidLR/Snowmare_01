# Codex 117-walk-ramp-pmc (morning, urgent - verifies the rebuilt Showcase)

The Lead rebuilt the dressed /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit in main + all 3 worker roots from the 115 layout (E7187A80,
map SHA bb9787f9...) and KEPT the Showcase LevelJson from main (1 PMC squad of 6, cold 0) - the rebuild had reset it to monster waves and
no PMC. On the worker (Saved/Logs/Worker/20261125-065200): LevelGenBuildingsWalkSmoke 0/6 buildings (flat entries 47/100, "no complete
nav", followers left behind), LevelGenRampSmoke 33/36 (S04 / S10 stuck); the Walk log has 10 PMC spawn / killed-in-action lines.
Your 110/115 PASS ran with an empty PMC roster. Most likely the same as 085 / 113: the preloaded Showcase PMC squad fights the walk squad.
Fix: SmokeUtils::StopLevelPmc(World) (+ the intact-roster guard from 113) in LevelGenBuildingsWalkSmoke, LevelGenRampSmoke and every other
smoke that can run on a kept map with a PMC roster; then on slot1 (-Map /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -ForceMap) prove
Walk 6/6 + Ramp 36/36 on the rebuilt map. If they still fail without PMC, report the real map cause (doors from G018 now register from the
v2 layout - closed / locked inner doors may cut nav). Branch codex/117-walk-ramp-pmc.
