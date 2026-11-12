# Interim hand-back and status report - 2026-11-11, updated 2026-11-11

**Current authority:** Codex leads interim integration again through Saturday 2026-11-14; main remains frozen. Stealth 003 is verified and ready for designer playtest in the interim branch. Do not merge main without Claude's review. Next: Claude-relayed tasks 004 and 005, then C5/C6; those tasks are not claimed complete.

**Only own working location:** C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-interim, branch interim/2026-11-12. Frozen tag before-interim-2026-11-11 = 8b0bb579. Handover/tool docs already present before our first commit: 56e1d1f2, 11065194, a1100f29. Own commits are directly on interim because the designer explicitly required work only on that branch/worktree. No push, main build, scheduler edits or source asset writes.

## Local commits / verified state

| Commit | Result |
|---|---|
| c671bb23 | Initialized interim handback / queue / handoff pointers. |
| c6a68eb4 | TS saved formation lookup matches C++: exact shape wins; pair/2 fallback only for wedge/diamond. 18 tests + typecheck PASS, golden unchanged. Gemini independent review PASS (#126). |
| 8ff5d194 | Stealth pure rules/data: selected toggle, break, range/buildup/noise multipliers, optional JSON tuning; 4 focused tests PASS. |
| 1843f002 | Exactly 124 explicitly approved new animation copies (62 Male + 62 Female), LFS, reproducible guarded script. Source SHA-256 unchanged, target skeleton/rebake checks PASS. |
| 4a29d093 | Four-item read-only Gemini queue 012 + checkpoint documentation. |
| 90a6bf64 | Claude runtime checkpoint for stealth. |
| 4d15a54f | Runtime material lifecycle fix: late weapon parts/rounds and original override restoration. |
| 561a29ed | Expanded integration tests and smoke fixtures. |

Tasks 001/002 were completed and merged by Claude before the frozen base; no duplicate work. Props C4 TS already merged before interim. No Gemini branch has been merged by Codex.

## Stealth 003 - final verified status

STEALTH action-bar button / key 7, selected group toggle, native eight-direction Slow/Fast animation, wall/lean/peek, runtime translucent materials, patrol/combat detection scaling, and firearm/sprint breaks are verified. Late pistol/RPG parts restore correctly. Female smoke, real acoustic patrol brackets, knockdown recovery/death cleanup, controlled fast gait samples, and rendered evidence are now covered. Details, logs and test limitations are in `docs/port/codex/outbox/003-stealth-mode.report.md`.

- Corrected full-header build PASS (384.38 s); final smoke-only build PASS (28.15 s). Expanded male stealth smoke PASS (113.1 s); female full scenario PASS; 44/44 relevant rules/animation/perception/sight tests PASS; impact map and rendered smoke PASS.
- Three visually inspected 1280x720 game-camera frames are in main `Saved/Orchestrator/stealth/`. These three PNGs are the only main-worktree writes; no main source/assets changed.
- Expanded coverage includes late weapon parts and restoration, four wall movement legs, controlled 120/300/480 cm/s animation sampling, real patrol hearing and wall occlusion, female asset/skeleton/calibration/root drift, and knockdown/get-up/death stealth cleanup. The female scenario applies the existing optimized female body to Medic in memory; the committed `L_MovementTest` roster remains all male.
- Earlier regression baseline smokes all passed but were not rerun in the final expanded pass; see 003 report for exact durations. Gemini review `1a05804e`: PASS with minor recommendations. No known implementation follow-up remains from the prior late-parts concern.

## Gemini coordination (historical handover, updated below)

- Branch gemini/interim-c5-prep, worktree CodexTactics-gemc5. Actual commit 2f50e91a: C5 read-only prep (one report file); e3c05788: stealth read-only review. Both verified via git show; not merged. Reports reside on that branch and remain available for Claude.
- Four-item queue committed in docs/port/gemini/inbox/012-interim-review-queue.md: stealth review (done), C5 schema verification, C6 reuse/integration map, read-only existing night-trainer review. Designer requested batch queues, chat only status. After #141 Gemini was told to finish current review and stand by; he acknowledged #146. Do not assume remaining items completed.
- Review C5 report's suggested hand/origin fields against actual schema before implementing; it is analysis, not an approved data contract.

Current prep inputs: C5 addendum 03fea4c6 and C6 runtime map 14a91b4e. Gemini 013 items 1-4 and review 1a05804e are integrated by Claude; queue 016 and task-005 ragdoll mapping are current work. The older 012 handover notes above are historical, not pending assignments.

## Remaining queue - not started

1. Stealth 003 is verified; designer playtest is ready in interim. Next are Claude-relayed tasks 004 (hitch) and 005 (dead enemies / ragdoll), in that order.
2. C5 props static validator: PROPS_LAYERS 2.4 / 5, generated invalid/dangling props dropped with IDs; locked/hand props flagged and kept; fixtures and idempotence. Gemini prep is input, not implementation.
3. C6 UCodexLevelPropsSubsystem runtime spawner: greybox barrel cylinders / blue-red crates, tags/nav modifiers, no tickers, LevelPropsSpawnSmoke counts match committed Depot props file. Designer waits for visible props.
4. Chat attention indicator is ALREADY DONE by Claude/Gemini: 56d26c46, 8/8 Python tests. Do not repeat it.
5. Morning night trainer review: read-only main Saved/Trainer/pmc-v4 and scheduler.log via Gemini; do not touch task schedule/main binaries and never consume massive raw telemetry (use TypeSafe telemetry).

## Boundaries / reproducibility

- Main frozen; no push. Do not touch maps, Config, VFX, Depot designer JSON, CLAUDE.md or source packs. Only asset-write exception was explicitly re-approved in primary chat: exactly these 124 new Stealth copies, with editor closed. Initial auto-review rejection was resolved by that explicit approval; no bypass.
- Source pack Content/StealthLocomotion and TrainingGround node_modules are junctions for read-only use. Do not remove through their targets. Existing unrelated untracked packs/config/cache in worktree are not ours and were never staged.
- Each own commit passed python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex; explicit paths only.
- Set CODEX_AGENT=codex in shells and PYTHONIOENCODING=utf-8 for agent chat. Chat post advances seen state: read incoming messages before posting; avoid multiple concurrent waiters.
- Anthropic API spend by Codex: **$0**. Main API ledger observed read-only: $14.6072, cap $30. No key printed. TypeSafe audits used separately.
- Earlier compiler C4458 local Mesh/member shadowing fixed before successful runtime builds. No unresolved build/test failure at this checkpoint.

## Codex resumed interim leadership (2026-11-11, after Claude cover; status updated 2026-11-11)
- Runtime checkpoint is committed by Claude as 90a6bf64; chat banner 56d26c46 and Gemini 013 reports are integrated. Current queue: Claude-relayed tasks 004 and 005, then C5 (03fea4c6), C6 (14a91b4e), then chat loop. Earlier hand-back headings are historical.
- Router script/config are absent in interim but available in main; classify was invoked read-only from its main path with PYTHONDONTWRITEBYTECODE=1, shell cwd interim. No main writes or router settings changes.
- Routing trial: 3 | Codex main session | weapon-material lifecycle + expanded integration coverage: PASS after test-fixture corrections.
- Routing trial: 3 | Codex main session | game-camera diagnosis/captures: PASS; three inspected frames copied to the permitted main evidence path.
- Routing trial: 3 | Codex main session | six Gemini integration-test gaps: PASS (see final 003 report).
- Routing trial: 2 | gpt-6-luna | read-only API lookup: partial REDO (first answer incorrectly called protected TickPatrolPerception public; access modifier verified and corrected). Corrected route uses public Enemy Tick after real StartPatrol; no files changed. Other useful corrections: invented ResetPatrolAlert/ApplyKnockdown signatures, actual female body path, death helper signature.
- Working part 1 is complete: original override slots (including null) restore correctly; late parts and rounds are enrolled around ApplyWeaponVisual. See runtime fix `4d15a54f` and final 003 report.
- Working part 2 is complete: expanded male/female, acoustic, animation, knockdown/death, and rendered verification passed. The documentation closeout is level 1 | gpt-6-luna | REDO: primary corrected date and stale queue/banner status; final reviewed. Latest status: tasks 004 and 005 then C5/C6; no completion is claimed for those queued tasks.

### Stealth designer feedback reopened (2026-11-11)

First follow-up: passive on entry and healthy prone crawl retained (build 51.41 s, expanded smoke 54.3 s PASS). Router `3 | current Codex session | PASS`. Detection, PMC parity, last-seen-point fire and pack-specific wall/peek corrections remain ahead of 004/005/C5/C6; see updated 003 report.

### Designer model routing update (2026-11-11)

Designer superseded the fixed cheap/main mapping: Jev remains the classifier and directly chooses ONLY Luna (`gpt-6-luna`), Sol (`gpt-6.1-sol`), Terra (`gpt-6.1-terra`), or Astra (`gpt-6-astra`). Interim now contains its own enabled `.claude/model_router.json` and `Scripts/Tools/model_router.py`; main and global app settings were not changed. Routing is advisory: the coordinator passes the returned real model_id to collaboration.spawn_agent; no promise of switching the current session. Below 0.6 model confidence or on errors/follow-ups it does not dispatch. Complexity 1-4 is independent and retained for reports.

Validation: 9 mocked tests PASS (all four allowed models, independent level, malformed choices/config/probabilities, NaN, exception, timeout, context, enabled/disabled CLI/hook). Live classify returned `3 | gpt-6.1-terra | route=true, confidence 0.94` for stealth perception. Router implementation `2 | gpt-6.1-terra | PASS after review adjustments` (initial bootstrap selection had 0.57, so it was manually assigned under the explicit designer request, not claimed an automatic dispatch). No Claude API used. Jev audits/classification remain authorized by the latest instruction.

### Resume checkpoint — 2026-11-12

Read RESOURCES.md, README local-model rules, INTERIM_LEAD, chat #202/#210, Gemini 025 merge plan and board reconciliation. User queue controls: finish stealth -> 004 -> 005 -> C5 -> C6 -> #42 -> #4 -> #25/#35. 006 belongs to Gemini 020 (c438662c / 9ae763f8), review only. T11 is implemented at fa8299a0 awaiting integration, and AI Director #47 was cancelled; the older board labels are stale.

Stealth core feedback build PASS 175.76 s, male wall/movement smoke PASS 141.2 s, PMC detection/real fixed-point fire smoke PASS 133.2 s. The wall pose now uses the pack's along-wall basis and physically exposes the head 27.4 cm beyond the edge. Effective Passive preserves the previous posture. No assets/main/push. Final focused tests 70/70 and impact-map coverage PASS. Female/rendered revalidation and independent Marksman bypass remain explicit follow-ups in the 003 report. Routing `3 | current Codex | PASS (core)`; small RU queue digest `1 (contextual fallback) | local qwen3-coder:30b | PASS`, reviewed. Local helper cache released after use to avoid build memory contention.

### Gemini integration ledger — 2026-11-12

- `gemini/016-3-implementation-maps` / `9d0cd4dc`: reviewed documentation-only merge, including corrected physics-asset/API map from review #196. Chat idle regression suite 8/8 PASS; Jev boundary audit PASS, risk 0. Markdown hard-break trailing spaces are intentional; no C++/assets changed by this merge.

- Stealth validation follow-up: female and rendered male scenarios PASS; three fresh interim-only game-camera images inspected. Female head 26.8 cm beyond physical edge, male 27.4 cm. Main untouched. Local report draft `2 | codex-local | PARTIAL REDO` (parent corrected linkage between independent results). Remaining stealth subpart: independent Marksman live-target bypass.

- `gemini/017-1-implementation-maps` / `d80ff93a`: documentation-only merge; checked governing narrative/trap facts, kept proposed implementation maps separate from completed gameplay claims. Chat idle regression 8/8 PASS, Markdown-aware diff check PASS, Jev boundary audit PASS (risk 0).

- Marksman stealth follow-up: real FOV/LOS/range even for hordes, frozen aim point, bounded no-direct-hidden-hit fire, block/reacquisition/expiry. Build 291.65 s + final rebuild 15.48 s PASS; new MarksmanStealthSmoke 47.9 s and PMC detection smoke 46.4 s PASS; focused 80/80 tests and impact map PASS. Routing `3 | gpt-6.1-terra | PASS after hound-fixture isolation`. Point-fire limitation documented in 003; broad 003 regressions are next.

- 003 feedback final gate closed: all eight required/marksman regression smokes PASS on final runtime (264.6 s batch, no retries), plus 80 focused tests, PMC/Marksman detection scenarios, female and rendered checks. Code 558e4a8c / 57fa466e; ready for designer repeat playtest, key 7. Known point-round limitation remains documented. Final RU draft `2 | codex-local | PASS`. Next implementation 004; documentation merge step 021-maps is held pending Gemini contract correction requested in chat #232 (MON_* / 5 cm versus SENSOR_* / 15 cm).

### Task 004 measurement checkpoint — 2026-11-12

Diagnostic-only HitchProbeSmoke and startup/animation/ladder scopes prepared. Impact map and diff checks PASS; build/runtime pending because the designer's interim editor is open (build.ps1 lock guard refused). No hitch fix is claimed. Routing 4 | gpt-6.1-terra | read-only plan PASS; 3 | gpt-6.1-terra | diagnostic static review PASS, execution pending. See 004 report. Next: build and fresh-process baseline after editor closes, then measured async preload/cache fixes. Gemini021 MON_* correction f288ab1f verified; remaining map factual/stencil contradictions returned in chat #244, integration still held.

- `gemini/021-implementation-maps` / `24e22207` (includes f288ab1f): four documentation maps reviewed and integrated after author corrections. QA now follows MON_* and 5 cm; Depot immortal=false fact and PMC stencil contradiction corrected. Remaining architecture/tuning entries are planning proposals, not approved runtime/API evidence; recheck against source/governing docs when implementing. Chat-idle 8/8 PASS; Markdown-aware diff check PASS. Routing `2 | current Codex session (Sol advisory confidence below dispatch threshold) | PASS after author corrections`. No gameplay/maps/assets changed.

- `gemini/021-local-model-grades` / `0c8816bf`: archived review of the earlier qwen2.5-coder:14b trial, not the current codex-local/qwen3-coder:30b. Its summary grades contain inconsistencies (e.g. commit-subject compliance vs measured 83 chars); use the detailed defects as historical observations, not quantitative model-quality evidence or current routing policy. RESOURCES.md remains authoritative, including no local-model C++/code-review work. Chat-idle 8/8 PASS; Markdown-aware diff check and Jev audit PASS. Router returned contextual follow-up/no dispatch; reviewed in current session, no local GPU contention during designer playtest.

- `gemini/023-stealth-review` / `162db498`: historical read-only WIP review, not a new gameplay patch or acceptance gate. The valid Passive-override concern was resolved by 558e4a8c using effective temporary posture; final wall orientation, physical-edge evidence and independent Marksman behavior are verified by 003 report and 57fa466e/66917e4e. Earlier DONE labels and suggested obstruction bypass are not current authority; retain real obstruction checks. Chat-idle 8/8 PASS; Markdown-aware diff check and Jev audit PASS. Routing contextual follow-up | current Codex session | PASS as historical review only.

- `gemini/016-2-auto-qa-morning` / `91cd0322`: Python QA aggregator reviewed; reproduced severity-order defect and author corrected it with both-order regression. Parent clarified no-incidents output so an empty scan cannot claim all runs passed, with an empty-input regression. QA tests 7/7 and chat-idle 8/8 PASS; diff check and Jev audit PASS. This is an offline log digest, not proof of complete runtime sensor/incident-capture coverage; report its inputs/limitations. Router returned contextual follow-up/no dispatch; current session review, no local model started during editor playtest. No live trainer/scheduler/server automation was launched.

- 004 source-history investigation (4 | gpt-6.1-terra | PASS, read-only): animation-load growth in Nov08–11 is a plausible startup suspect; base PMC/wave/ladder sync paths and overlapping 30s schedules are older. Exact code/asset introduction hashes are recorded in 004 report; no causal/bisect claim without trace baseline. Editor remains open; no builds/games were forced.

### Shared model-router restoration — 2026-11-12 (designer correction)

Restored Scripts/Tools/model_router.py and .claude/model_router.json byte-for-byte from the main checkout's existing shared Claude router (main was READ ONLY). SHA256: script 1ce9950cadaf495845bee0b0d378367c8177ca3fe049eb8d251f56822bef2172; config 4e731186b4b9a31325c134c07db473eb3505871fe5636bbe8d1fbb93964676a2. Codex reads only the returned complexity level and maps it through Scripts/Tools/model_routes.json providers.codex: 1 Luna, 2 Sol, 3 Terra, 4 Astra. Disabled local_first_for_level_1 for Codex; no Ollama invocation. The shared classifier's displayed Claude agent/model remains Claude-facing and does not choose the Codex provider.

Replaced obsolete Codex-only tests with nine mocked shared-classifier/provider/hook tests, all PASS, including Claude hook compatibility, no-network/child-process checks, follow-up/error/timeout behavior. Diff check PASS. Routing: 1 | gpt-6-luna | byte restoration PASS; 3 (Jev .61) | gpt-6.1-terra | test adaptation PASS. New queue supersedes older ledger: 004 -> 008 (4 then5 then1–3) -> Gemini028/030/031/022/029; 005/C5/C6 and older025 remaining merges deferred until that queue is complete. Main/push/assets untouched.

- 004 measured baseline: build46.89s PASS; cold fresh-process frame3641.83ms and second traced process1755.95ms FAIL250ms gate, actual leader movement575.9cm. Trace identifies PMC anim-init1.251966s and military-zombie FlushAsyncLoading0.407535s in the same frame. First catalog part (paths only, no runtime change) build9.07s +2 automation tests PASS, impact map PASS; M4/default clips and Blueprint overrides still incomplete. Next async readiness/deferred binding/cache changes; not a completion claim. See updated004 report for evidence paths and limitations.
