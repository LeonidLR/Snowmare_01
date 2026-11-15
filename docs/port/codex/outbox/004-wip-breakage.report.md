# 004 WIP smoke breakage scout
Code-only comparison; no logs, checkout, merge, edits to code, builds or smokes. Exact failing assertions remain unverified.
Requested diff 42d5df5a...50de2e98: 28 files, +1024/-69; merge base da8c7199. Main HEAD at final read: 95e8a565.
Line convention: WIP = 50de2e98; smoke/current-main references = inspected main sources. Paths abbreviated below.
C = Source/CodexTactics/Private/Characters/; G = Source/CodexTactics/Private/Core/; D = Source/CodexTacticsDev/Private/Debug/.

## What WIP changes
- WIP G/CodexTacticsGameMode.cpp:138-147 starts async preparation before Super::StartPlay and defers SpawnSquad; :191-218 spawns later and disables operative/movement ticks; :228-245 can pause the world.
- WIP G/GameplayPreloadSubsystem.cpp:103 requests async batches; :240-257 spawns squad after initial classes load, THEN gathers/loads animation dependencies. Squad existence is no longer equivalent to gameplay readiness.
- WIP C/OperativeAnimInstance.cpp:96-105 defers binding through OnAssetsPrepared; :109-121 loads weapon sets/defaults only on preparation completion. C/OperativeAnimInstanceShotgun.cpp:98-100 refuses early loads; :131-136 includes shotgun, crawl and injured sets in that deferred bind.
- WIP G/GameplayPreloadSubsystem.cpp:288-294 also waits for mesh compilation; :316-338 binds animations, marks Ready/Failed, then releases startup gates. Header Public/Core/GameplayPreloadSubsystem.h:35 permits ticking while paused.

## Common cause supported by code
All three smokes run from FTSTicker::GetCoreTicker, advance their own counters independently of world pause, and have no GameplayPreload readiness guard. Fixed startup delays no longer guarantee squad spawn, animation binding, or live movement/animation ticks.
Counter increments: D/ProneCrawlSmokeCommand.cpp:248-249; D/InjuredLocomotionSmokeCommand.cpp:586-587; D/ShotgunCoverSmokeCommand.cpp:197-198.
Ticker registrations: same files :485, :1228, :476 respectively. This is a concrete lifecycle mismatch; which race branch produced the reported failures needs runtime evidence.

## Per-smoke breakage paths
1. ProneCrawlSmoke: D/ProneCrawlSmokeCommand.cpp:271-280 waits only 3 harness seconds, then fails immediately if leader/PMC missing. Initial preload may not have spawned the squad yet.
   If squad exists during dependency loading, :331-335 changes stance/spawns a fighter while startup gates still hold; :242-243 later require crawl weight >0.5 and direction-specific clip. WIP delays CrawlSetData binding and movement ticks, so these checks can run ahead of simulation.
   Minimal fix: readiness gate before counters/setup; start the existing settling period after successful readiness. Risk: low for dev-only timing change; do not weaken clip/facing assertions.
2. InjuredLocomotionSmoke: D/InjuredLocomotionSmokeCommand.cpp:609-618 likewise assumes leader/AnimInstance after 3 harness seconds; :664-669 explicitly fails HasInjuredSet when the dependency phase has spawned the squad but deferred set binding remains pending.
   Minimal fix: same gate before counters/setup, then retain HasInjuredSet as a real missing-content assertion after Ready. Risk: low; waiting merely for actor existence is insufficient.
3. ShotgunCoverSmoke: D/ShotgunCoverSmokeCommand.cpp:217 already waits for >=3 members, so missing-squad immediate failure is not its main path. It can enter stage 1 during dependency loading, then :264-274 spends 2 harness seconds and checks Full shotgun mask/orders cover while animation/movement remain gated.
   Subsequent :287-294 times out cover arrival and checks layer weight despite paused simulation. Global 120 s budget (:199) also includes preparation time.
   Additional race: :469-472 schedules PlaceSquadAtTestStart on a 0.5 s WORLD timer, while the smoke uses CORE time. Pause can delay that teleport until after core-driven test setup has begun.
   Minimal fix: gate startup before all smoke clocks/frame sampling; schedule/finish placement after successful readiness, then begin setup/settling. Risk: low/med (preserve the intended placement/order sequence).

## Minimal shared repair / safeguards
- Gate each Step before State.Time/StageTime increments; while preparing, return without consuming gameplay budgets or collecting phase/frame evidence. Use a separate bounded real-time readiness timeout.
- Distinguish successful Ready from failure: WIP G/GameplayPreloadSubsystem.cpp:47-50 IsGameplayReady means !IsPreparing and also accepts Failed; explicitly reject HasFailed and report preparation failure.
- Reuse the lifecycle pattern in WIP D/HitchProbeSmokeCommand.cpp:170-195, which already waits during preparation; the three other smokes were not updated by this WIP.
- Do not run setup during preparation: WIP G/CodexTacticsGameMode.cpp:258,266 restores held ticks, potentially undoing a smoke's intervening actor-freeze choices.

## Missing clips vs readiness / conflicts with current main
- No obvious omitted crawl/injured block: WIP G/GameplayPreloadCatalog.cpp:183 explicitly gathers shotgun/pistol/launcher/prone_crawl/stealth/injured; :146-176 includes existing retarget copies. Missing-set observation before Ready is not proof assets are absent.
- Secondary hazard if a path is actually missed: WIP C/OperativeAnimInstance.cpp:129-140 returns null for an unresolved animation after Ready; Shotgun.cpp:104-106,129 caches load completion by skeleton. Audit catalog parity/retry if a set remains missing AFTER Ready; do not restore unconditional synchronous gameplay loads as the first fix.
- Main-only changes since shared base include C/OperativeAnimInstance.cpp:1420 copying StealthLookYawDeg and Public/Characters/OperativeAnimInstance.h stealth proxy/start-state additions. Preserve these when integrating the initialization/header refactor.
- Main C/OperativeAnimInstanceShotgun.cpp:548-564 now keeps default walk/jog clip speeds unless an override is positive; WIP snapshot predates this. Keep that fix when taking the preload resolver hunk.
- No current-main GameMode/catalog edits appeared in the requested base-to-42d5df5a comparison. This is overlap analysis, not a performed merge/conflict check; never replace whole current files with the WIP snapshot.

## Open questions
Which assertion failed in each run, and was preload Initial/Dependencies/Ready/Failed at that instant? Are these failures cold-load-only, and did ShotgunCover's delayed placement execute after stage 0? Code establishes the races, not their observed occurrence.
