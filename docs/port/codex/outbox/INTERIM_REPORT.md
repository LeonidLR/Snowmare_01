# Interim hand-back report - designer handover to Claude, 2026-11-11

**Current authority:** designer chat #141 requests hand-back to Claude for the next two hours. Codex is finishing this checkpoint and then listening; C5/C6/chat-indicator implementation has NOT started. Do not merge main without Claude's review.

**Only own working location:** C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-interim, branch interim/2026-11-12. Frozen tag before-interim-2026-11-11 = 8b0bb579. Handover/tool docs already present before our first commit: 56e1d1f2, 11065194, a1100f29. Own commits are directly on interim because the designer explicitly required work only on that branch/worktree. No push, main build, scheduler edits or source asset writes.

## Local commits / verified state

| Commit | Result |
|---|---|
| c671bb23 | Initialized interim handback / queue / handoff pointers. |
| c6a68eb4 | TS saved formation lookup matches C++: exact shape wins; pair/2 fallback only for wedge/diamond. 18 tests + typecheck PASS, golden unchanged. Gemini independent review PASS (#126). |
| 8ff5d194 | Stealth pure rules/data: selected toggle, break, range/buildup/noise multipliers, optional JSON tuning; 4 focused tests PASS. |
| 1843f002 | Exactly 124 explicitly approved new animation copies (62 Male + 62 Female), LFS, reproducible guarded script. Source SHA-256 unchanged, target skeleton/rebake checks PASS. |
| 4a29d093 | Four-item read-only Gemini queue 012 + checkpoint documentation. |
| runtime checkpoint | Native stealth / actor / HUD / perception / smoke; hash recorded after final audit. See 003-stealth-mode.report.md. |

Tasks 001/002 were completed and merged by Claude before the frozen base; no duplicate work. Props C4 TS already merged before interim. No Gemini branch has been merged by Codex.

## Stealth 003 - current implementation and evidence

STEALTH bottom action-bar button / key 7, selected mixed/all-on toggle, crouched native eight-direction Slow/Fast animation layer, wall/lean clips, actual corner reveal in shared minimap sight, runtime grey translucent materials, patrol/combat detection and noise reduction, fire/sprint breaks. Native clip timings use measured per-body speeds / left-foot strike offsets. Default exit stays crouched; fire posture and hound scent unchanged. Peek visibility lasts while LoS holds; existing exploration last-known ghost 12 s and combat memory unchanged.

- Latest native build PASS, 21.02 s after review fixes. Earlier integration build PASS 173.85 s; 44/44 Animation / StealthMode / AI.Perception / Combat.Sight tests PASS. Unit evidence Saved/Logs/StealthNative-unit.log.
- Impact map check PASS.
- All required smokes PASS without retries: StealthModeSmoke 57.4 s; InjuredLocomotionSmoke 76.6 s; ProneCrawlSmoke 67.6 s; CoverSwitchSmoke 59.9 s; FormationLabSmoke 201.3 s; SoloStartSmoke 56.6 s. Saved/Logs/StealthRegression-results.json.
- Gemini review e3c05788 confirmed wall phase sign bug: actor faces wall normal; now uses existing IsShimmyForward. Reload fade softened, post-shot aim priority preserved. Final StealthModeSmoke after these fixes PASS 53.9 s; actual wall movement sampled on 92 frames. Saved/Logs/StealthMode-final-results.json.
- Rendered smoke first PASS. Final capture is being repeated with explicit holds so screenshots represent stealth/wall/peek before the next command. Three requested game-camera frames will be in main Saved/Orchestrator/stealth/ (the task's explicit evidence-path exception); no main source/assets changed.
- Full per-file/authorization/review details: docs/port/codex/outbox/003-stealth-mode.report.md. This is a review checkpoint, not a claim that every edge case has dedicated runtime coverage.

### Stealth review follow-ups for Claude

1. Concrete remaining limitation: new weapon mesh components created after entering stealth are not part of its initial material snapshot. A weapon change creating modular parts may show those parts opaque. Smoke currently uses the initial weapon.
2. Female copies have skeleton/rebake/phase measurements, but no female-specific game smoke yet. Fast gait, acoustic end-to-end detection, reload/damage/death and action transition edges lack dedicated runtime assertions.
3. Gemini's hypothesized hit-flash SLOT-material corruption is not reproduced by current code: CombatFeedbackActor changes/restores OverlayMaterial only (131-163). Current logic lets feedback own the overlay and cleans a late restore of the stealth overlay. Re-check any other material writers if expanding that review.
4. Uncrouch copy is retained because all 62 copies were explicitly authorized; toggle exit deliberately remains crouched. Grenade throw is not a firearm shot; designer default questions were proposed #123, no contrary answer observed.

## Gemini handover

- Branch gemini/interim-c5-prep, worktree CodexTactics-gemc5. Actual commit 2f50e91a: C5 read-only prep (one report file); e3c05788: stealth read-only review. Both verified via git show; not merged. Reports reside on that branch and remain available for Claude.
- Four-item queue committed in docs/port/gemini/inbox/012-interim-review-queue.md: stealth review (done), C5 schema verification, C6 reuse/integration map, read-only existing night-trainer review. Designer requested batch queues, chat only status. After #141 Gemini was told to finish current review and stand by; he acknowledged #146. Do not assume remaining items completed.
- Review C5 report's suggested hand/origin fields against actual schema before implementing; it is analysis, not an approved data contract.

## Remaining queue - not started

1. Stealth review/edge-case follow-ups above, then Claude acceptance.
2. C5 props static validator: PROPS_LAYERS 2.4 / 5, generated invalid/dangling props dropped with IDs; locked/hand props flagged and kept; fixtures and idempotence. Gemini prep is input, not implementation.
3. C6 UCodexLevelPropsSubsystem runtime spawner: greybox barrel cylinders / blue-red crates, tags/nav modifiers, no tickers, LevelPropsSpawnSmoke counts match committed Depot props file. Designer waits for visible props.
4. Designer #139 follow-up AFTER stealth: agent_chat_server.py detects messages to gemini/all unanswered by Gemini >10 min, Tools/AgentChat red 'Gemini спит' + elapsed time, Python test. No server/UI implementation started.
5. Morning night trainer review: read-only main Saved/Trainer/pmc-v4 and scheduler.log via Gemini; do not touch task schedule/main binaries and never consume massive raw telemetry (use TypeSafe telemetry).

## Boundaries / reproducibility

- Main frozen; no push. Do not touch maps, Config, VFX, Depot designer JSON, CLAUDE.md or source packs. Only asset-write exception was explicitly re-approved in primary chat: exactly these 124 new Stealth copies, with editor closed. Initial auto-review rejection was resolved by that explicit approval; no bypass.
- Source pack Content/StealthLocomotion and TrainingGround node_modules are junctions for read-only use. Do not remove through their targets. Existing unrelated untracked packs/config/cache in worktree are not ours and were never staged.
- Each own commit passed python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex; explicit paths only.
- Set CODEX_AGENT=codex in shells and PYTHONIOENCODING=utf-8 for agent chat. Chat post advances seen state: read incoming messages before posting; avoid multiple concurrent waiters.
- Anthropic API spend by Codex: **$0**. Main API ledger observed read-only: $14.6072, cap $30. No key printed. TypeSafe audits used separately.
- Earlier compiler C4458 local Mesh/member shadowing fixed before successful runtime builds. No unresolved build/test failure at this checkpoint.
