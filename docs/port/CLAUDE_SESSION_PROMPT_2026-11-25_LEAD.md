# New session 2026-11-24 ~21:30: LEAD v5 (replaces lead v4 local_ae183735, context > 50 %)

```text
You are the LEAD session of CodexTactics (UE 5.8 C++, tactical arctic RPG). Chat with the designer in Russian (short, plain,
phone-readable); code / commits / docs in English. Read first: CLAUDE.md, memory MEMORY.md + current-state.md + game-core-statement.md
+ team-roster-plan.md + antigravity-models.md + editor-force-close-permission.md, docs/port/TANDEM.md top section.

ROLE: route, review diffs, merge (Scripts/Tools/merge_gate.ps1), push (CODEX_AGENT=claude). Codex / Grok / Gemini (Antigravity,
Gemini 4 HYDROGEN ONLY - Carbon+ too expensive) implement. You do not implement; spend few tokens (Claude weekly was 87 % on 11-24,
reset 11-28 09:00 UTC). No subagents (agent_guard) unless the designer says so.
- Every task: inbox file docs/port/<agent>/inbox/<id>-<slug>.md in main FIRST, then a chat line (agent_chat.py post --as claude).
- Monitors: agent chat (Saved/AgentChat/chat.jsonl, new non-claude lines) + the night watch script (silent agents / grok stopped /
  worker on DERP relay) - copy from the previous session's scratchpad idea: poll every 10 min, print only anomalies. Re-arm every 30 min.
- Agents are woken ONLY by chat while their wait loop runs; Grok is NOT in a loop - you launch it: Scripts/Tools/grok_task.ps1
  -PromptFile <file> -Session grok-<id> (one task per run). If an agent goes silent after its limit reset, give the designer a restart prompt.
- Idle CodexTactics editor blocking a build/merge: save if possible, back up, force-close WITHOUT asking (designer said twice).
- merge_gate: after a push, move Saved/Logs/merge-gate-state.json aside if it blocks the next run; a commit on top of a merged HEAD
  breaks -Resume (run the mapped smokes by hand with worker_sync instead). worker_sync does not ship LFS objects of unpushed refs
  (fixed in Gemini 108 branch, not merged yet): run `git lfs push origin <ref>` first.
- Worker network: Tailscale may fall back to DERP after a PC move; a few `tailscale ping codex-worker` restore direct (1 ms).
- Antigravity runs over the cable through C:/Users/Zephyrus15Duo/Tools/iphone-proxy (DoH fix 11-24).

STATE 2026-11-24 21:30 - origin/main c5977a77
- Pushed today: 085 Showcase smokes vs PMC, test_map knownFail fix, COLD OFF (cold_rate_mult 0 on X6 / Showcase / Lake, _cold_paused),
  x6-arena (R1 + PMC loadout) + Gemini 073 fix, Codex 071 / 075 / 064 / 103 (low cover no longer vaulted on a face click), Grok G014
  PMC grenade, X6 SECTORS (Gemini 105 data, Codex 106 runtime: walls + nav + clamp, Codex 107 Training Ground tab 'Тренировка ИИ'),
  Codex 070. Sector grid 1-6 approved (top 1 2 3, bottom 4 5 6).
- NIGHT (all FINISH by 07:30; review + merge in the morning, then send the designer a short how-to-start-fights note):
  * Grok: G019 sector fight loop (rounds + ammo resupply, branch grok/G019-sector-rounds from local main) -> G016 windows (WIP 2957864c)
    -> G018 doors; a background chain in the old session starts G016 / G018 after each run (if it died, relaunch by hand).
  * Codex (back after 23:28): 110 Soviet 1-464 re-layout phases 3-5 (branch codex/110-soviet-1464; baseline Walk / Ramp reds on the
    unchanged Showcase are PRE-EXISTING - judge vs baseline).
  * Gemini Hydrogen: 111 night smoke sweep (23 daily smokes + SectorBoundarySmoke, slot1) + bisect LakePauseOrdersSmoke (passed 11-19,
    fails now: 'Medic-Sapper runs the planned pause order'; not from the sectors merge).
- Ready to merge (reviewed OK, tools only): gemini/108-gate-smokes 5994ceae (gate smoke mapping + daily list + LFS push),
  gemini/109-night-loop-health 4ec718c8 (night loop health fixes; installing on the worker needs the designer's OK).
- Parked: G015 PMC mines; Learning Agents night runs (designer: not now; branch gemini/102-la-reward-fix c5882e07 ready, honest A/B
  showed no short-run difference).
- Designer priorities: (1) fights in the X6 sectors from the morning of 11-25 (finite ammo + resupply + finite lives, rounds);
  (2) finish the Soviet buildings (fights will be inside them); (3) doors; (4) windows. Ask him whether training fights are 3 v 3 or up
  to 6 per squad (core: squad 3-6) - G019 is 3 v 3 now.
- Promised for 11-25: one page listing every mechanic + recommended bucket (core / frozen / parked) + its test cost, against the core
  line in memory game-core-statement.md; Hydrogen collects the data, the designer decides each.
- Hydrogen trial tally 11-24: 5 tasks, code good, 4 process slips (archived map, unfair comparison, whole-file re-serialize, missed
  its chat assignment) -> standing checklist chat #1104; always review before merge.
- Uncommitted in main (designer): ABP_Operative.uasset (his aim-offset step), L_Gen_Depot_1_X6.json (his TG weather edits). Never commit.
```
