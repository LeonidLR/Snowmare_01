# 037 - Triage + fix the 035 real reds (Claude, 2026-11-17) - MEDIUM, one smoke at a time
Why: 035 sweep (docs/port/codex/outbox/035-nightly-smoke-sweep.report.md) left 10 smokes failing twice. The sweep base aba810f1 is
BEFORE merges 074 (RetreatFacing) and 075 (cover stand-off) - so first re-check on newest main.
Branch codex/037-red-smokes from the NEWEST main (pull first), worktree CodexTactics-agents. NEVER build/test in main (designer plays there).
Step 0: build; re-run RetreatFacingSmoke + CoverSmoke once (-Jobs 1). Green -> drop from the list.
Then, in this order (cheapest first), for EACH red:
1. MilitaryZombieSmoke + OptimizedCharacterSmoke (death clip not playing / held - probably ONE shared cause, check together)
2. StealthModeSmoke (a fired round must break the shooter's stealth)
3. CoverHugSmoke (shimmy back to the edge ordered)
4. MantleDescentSmoke (140 cm island top: fall states expected, vault clip played; rerun also showed 230 cm parapet start-state issue)
5. FormationJitterSmoke (other follower jitters more with the D-1 man)
6. LevelGenRampSmoke (STAIR_in_8 group stuck at the top level)
7. RpgPmcSmoke + PmcEncounterHuntSmoke - DIAGNOSIS ONLY. If the cause is the PMC order lifecycle from your 030 diagnosis
   (FallBack / Suppress / cover stop / Heal / readiness), write "needs Claude" and do not fix - Claude owns that fix.
Per smoke: read the FAIL line + the smoke command + the game code it checks; decide: (a) smoke-side bug (stale expectation / timing),
(b) game regression - find the commit (git log / bisect on the touched files), (c) cannot tell. Fix (a) and SMALL (b) (<~40 lines,
one system); one commit per smoke, message names the smoke and the cause. Bigger (b) or unclear -> "needs Claude" with the evidence.
Gate after each fix: build + that smoke 3x green (-Jobs 1) + the smokes of the same system (`Scripts/test.ps1 -Smart` + related smokes).
Never: weaken / delete an assertion just to go green (a changed expectation needs the reason in the commit), knownFail/knownFlaky flags,
.uasset, ABPs, Content/Maps/**, Content/VFX, DefaultScalability.ini, LevelJson/L_Gen_Depot_1*.
Post ONE chat line per smoke finished: "037 <Smoke> | fixed <sha> / needs Claude | cause in 10 words".
Report docs/port/codex/outbox/037-red-smokes-triage.report.md: per smoke - verdict, cause, commit, gate result.
10 % limit rule: stop, commit, post the partial status + which smoke is next.
