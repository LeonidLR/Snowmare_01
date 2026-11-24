# Codex 113-formation-stair-regression (after 112; may share the cause)

FormationStairSmoke (L_Gen_StoryShowcase_Kit) FAILS on origin/main f6e6a60e: "0 / 3 stair legs" - S04 storey 0->1 not up together
(Engineer stopped 0 cm from the leader), S10 Engineer STUCK on the stair, and "a click on storey 1 behind 4 locked door(s) sends the leader
to try one". Log: Saved/Logs/Worker/20261125-000028/Smoke-FormationStairSmoke*.log (night sweep, slot1, -Jobs 1).
It PASSED in the merge gate of 2026-11-24 16:17 (base 8ff14f3e... before: merge of x6-arena/071/075/064 = d1e23cbd).
Merged after that WITHOUT worker smokes: codex/103-low-cover-click (TryVault pending-cover guard), grok/G014 (PMC grenade),
then sectors 105/106/107 (UTrainingSectorSubsystem::Allows checks in cover / mantle / movement - should be inactive off X6),
grok/G019 (sector rounds), codex/110 (generator only). Bisect on the worker between d1e23cbd (good) and f6e6a60e, find the first bad
commit and the cause, fix with a unit test. Check it is not just flaky first (run it 3x alone at f6e6a60e). Branch codex/113-formation-stair.
