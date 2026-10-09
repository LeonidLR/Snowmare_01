#include "Bot/PlaytestBotSubsystem.h"

#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SplineComponent.h"
#include "Data/EnemyPerception.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Tactics/CoverTraceRules.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PersonalItemRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Misc/CommandLine.h"
#include "NavigationSystem.h"
#include "Misc/Parse.h"
#include "Telemetry/RunTelemetrySubsystem.h"
#include "UI/DialogueSubsystem.h"

namespace
{
	/** bot_driver.gd decision_interval and the exploration limits (25 s overall, 4 s per target in Godot — walking in UE). */
	constexpr float BotDecisionInterval = 0.2f;
	constexpr float BotExploreLimit = 240.f;
	/** Seconds to reach a target: 8 s + its distance at a walk (3 m/s). */
	constexpr float BotTargetBaseLimit = 8.f;
	constexpr float BotWalkSpeedCm = 300.f;
	/** Barricades farther than this are not the fight's cover (loose pickups elsewhere on the map). */
	constexpr float BotCoverSearchRadius = 2000.f;
	constexpr float BotDeployWaitLimit = 25.f;
	constexpr float BotRetreatDistance = 380.f; // smart_tactical_bot retreat_distance_threshold 3.8 m
	constexpr float BotClusterRadius = 400.f;

	bool BotIsAlive(const AActor* Actor)
	{
		const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health && Health->IsAlive();
	}

	/** Stealth decisions every this many world seconds (the patrol perception ticks at 0.2 s). */
	constexpr float BotStealthInterval = 0.2f;
	/** The sneaking leader re-plans his approach walk this often, s. */
	constexpr float BotStealthMoveInterval = 2.5f;
}

namespace BotTuning
{
	// Set by Scripts/Tools/jev_ai_coach.py through -dpcvars= for its experiments.
	static TAutoConsoleVariable<int32> CVarAssault(TEXT("Codex.Bot.MarksmanAssault"), 1, TEXT("Bot storms lone marksmen (0 / 1)"));
	static TAutoConsoleVariable<float> CVarClearRadius(TEXT("Codex.Bot.AssaultClearRadius"), 1500.f,
		TEXT("No other enemy this close to the leader (cm) before the bot storms a marksman"));
	static TAutoConsoleVariable<float> CVarStopDistance(TEXT("Codex.Bot.AssaultStopDistance"), 900.f,
		TEXT("The storming squad stops this far from the marksman (cm)"));
	static TAutoConsoleVariable<int32> CVarWallCover(TEXT("Codex.Bot.WallCover"), 1,
		TEXT("Bot leader takes a Sprint 12 wall-cover slot when no barricade is near (0 / 1)"));
	static TAutoConsoleVariable<float> CVarWallCoverRadius(TEXT("Codex.Bot.WallCoverRadius"), 700.f,
		TEXT("Bot looks for wall cover this far around the leader (cm)"));
}

void UPlaytestBotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || !FParse::Param(FCommandLine::Get(), TEXT("CodexBot")))
	{
		return;
	}
	FString ProfileName = TEXT("NORMAL");
	FParse::Value(FCommandLine::Get(), TEXT("BotProfile="), ProfileName);
	FParse::Value(FCommandLine::Get(), TEXT("BotTimeout="), TimeoutSeconds);
	FString Loadout = TEXT("COLLECT");
	FParse::Value(FCommandLine::Get(), TEXT("BotLoadout="), Loadout);
	int32 RunSeed = 1;
	FParse::Value(FCommandLine::Get(), TEXT("BotSeed="), RunSeed);
	StartBot(PlaytestBotRules::ParseProfile(ProfileName), true, Loadout.ToUpper().StartsWith(TEXT("COLLECT")) || Loadout.ToUpper().StartsWith(TEXT("EXPLORE")),
		RunSeed);
}

TStatId UPlaytestBotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPlaytestBotSubsystem, STATGROUP_Tickables);
}

void UPlaytestBotSubsystem::StartBot(EBotProfile InProfile, bool bQuitAtEnd, bool bCollectLoot, int32 InSeed)
{
	Profile = InProfile;
	Config = PlaytestBotRules::GetProfileConfig(Profile);
	Seed = InSeed;
	bQuit = bQuitAtEnd;
	bCollect = bCollectLoot;
	bActive = true;
	StartRealTime = FPlatformTime::Seconds();
	Stage = bCollect ? EBotStage::Explore : EBotStage::EnterCombat;
	bRunSetUp = false; // SetupRun on the first tick with a squad (OnWorldBeginPlay runs before the actors' BeginPlay)
	if (URunTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<URunTelemetrySubsystem>())
	{
		Telemetry->SetTesterProfile(PlaytestBotRules::ProfileName(Profile));
	}
	if (UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnEnemySpawnedNative.AddUObject(this, &UPlaytestBotSubsystem::HandleEnemySpawned);
	}
}

void UPlaytestBotSubsystem::SetupRun()
{
	bRunSetUp = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("BotSpawnPatrols")))
	{
		SpawnRuntimePatrols();
	}
	// Stealth (user plan 2026-10-07): an ambush level still in exploration is sneaked through, never started by the
	// button. -BotStealth=0 keeps the old behaviour (the bot presses "Start combat" after exploring).
	int32 StealthSwitch = 1;
	FParse::Value(FCommandLine::Get(), TEXT("BotStealth="), StealthSwitch);
	const ULevelEncounterSubsystem* Encounter = GetWorld()->GetSubsystem<ULevelEncounterSubsystem>();
	const UGameFlowSubsystem* StartFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	bStealthLevel = StealthSwitch != 0 && Encounter && Encounter->IsAmbushCombatStart() && StartFlow
		&& StartFlow->GetPhase() == ECodexGamePhase::Exploration && !StartFlow->IsCombatUnlocked();
	Stage = bCollect ? EBotStage::Explore : (bStealthLevel ? EBotStage::Stealth : EBotStage::EnterCombat);
	// Fire posture (user request 2026-10-06): the bot fights with the squad's automatic fire (Aggressive) as before
	// postures existed; while sneaking the squad holds its fire (Passive) so no auto-shot gives it away. Time mode: real
	// time throughout — the bot drives the operatives directly (OrderMoveTo / OrderTakeCover), no tactical pause.
	if (USquadSubsystem* BotSquad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		BotSquad->SetSquadPosture(BotStealthRules::PostureFor(!bStealthLevel));
	}
	if (bStealthLevel)
	{
		// The level starts in "Game" mode by itself (no in-level start menu since 2026-10-08): the patrols walk, the squad
		// sneaks. (On a wave level the bot explores and then starts the fight with StartMission(Combat), as before.)
		AOperativeCharacter* Medic = Member(2);
		StealthConfig = BotStealthRules::MakeSeededConfig(Seed, Config.Mines > 0 && Medic && Medic->GetDeployableCount(EDeployableType::Mine) > 0);
		StealthStartTime = GetWorld()->GetTimeSeconds();
		bStealthStarted = true;
		TArray<AEnemyCharacter*> Patrols;
		TArray<FBotPatrolView> Views;
		GatherPatrols(Patrols, Views);
		for (AEnemyCharacter* Each : Patrols)
		{
			KnownPatrols.Add(Each);
		}
		UE_LOG(LogCodexTactics, Display,
			TEXT("[Stealth] bot sneaking: %d patrol enemies, posture passive, ambush range x%.2f, alarm %.2f, patience %.0f s, limit %.0f s, sight x%.2f, hearing x%.2f, approach %+.0f deg, trap %d (medic mines %d, seed %d)"),
			Patrols.Num(), StealthConfig.AmbushRangeFraction, StealthConfig.AlarmSuspicion, StealthConfig.MinSneakSeconds, StealthConfig.MaxStealthSeconds,
			StealthConfig.SightMargin, StealthConfig.HearingMargin, StealthConfig.ApproachAngleDeg, StealthConfig.bUseTrap ? 1 : 0,
			Medic ? Medic->GetDeployableCount(EDeployableType::Mine) : -1, Seed);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Time mode: real time (no tactical pause / turn-based); fire posture %s"),
		bStealthLevel ? TEXT("passive while sneaking, aggressive in the fight") : TEXT("aggressive"));
	// Loot crates and loose deployables lying on the map (bot_driver _process_bot_exploration).
	for (TActorIterator<ALootCrateActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsLooted() && !It->IsDestroyed())
		{
			ExploreTargets.Add(*It);
		}
	}
	for (TActorIterator<ADeployableActor> It(GetWorld()); It; ++It)
	{
		ExploreTargets.Add(*It);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Started: profile %s, collect %d (%d targets), timeout %.0f s, seed %d, stealth %d"),
		*PlaytestBotRules::ProfileName(Profile), bCollect ? 1 : 0, ExploreTargets.Num(), TimeoutSeconds, Seed, bStealthLevel ? 1 : 0);
}

AOperativeCharacter* UPlaytestBotSubsystem::Member(int32 Index) const
{
	const EOperativeRole Roles[] = { EOperativeRole::Commander, EOperativeRole::Engineer, EOperativeRole::MedicSapper };
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	for (AOperativeCharacter* Each : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Each && Each->SquadRole == Roles[Index] && BotIsAlive(Each))
		{
			return Each;
		}
	}
	return nullptr;
}

TArray<AActor*> UPlaytestBotSubsystem::LiveEnemies() const
{
	TArray<AActor*> Enemies;
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDying() && BotIsAlive(*It))
		{
			Enemies.Add(*It);
		}
	}
	return Enemies;
}

FVector UPlaytestBotSubsystem::GetFrontDirection() const
{
	// Godot's "forward" (-Z) is where the waves come from: towards the enemy spawn points.
	const AOperativeCharacter* Leader = Member(0);
	if (!Leader)
	{
		return FVector::ForwardVector;
	}
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (TActorIterator<AEnemySpawnPoint> It(GetWorld()); It; ++It)
	{
		Sum += It->GetActorLocation();
		++Count;
	}
	const FVector Front = Count > 0 ? (Sum / Count - Leader->GetActorLocation()).GetSafeNormal2D() : Leader->GetActorForwardVector().GetSafeNormal2D();
	return Front.IsNearlyZero() ? FVector::ForwardVector : Front;
}

void UPlaytestBotSubsystem::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}
	if (FPlatformTime::Seconds() - StartRealTime > TimeoutSeconds)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Timeout %.0f s"), TimeoutSeconds);
		Finish(TEXT("ABORTED"), 1);
		return;
	}
	if (!bRunSetUp)
	{
		if (!Member(0) && FPlatformTime::Seconds() - StartRealTime < 10.0)
		{
			return; // the squad registers at its BeginPlay
		}
		SetupRun();
	}
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		if (Dialogue->IsDialogueOpen())
		{
			Dialogue->SkipDialogue();
		}
	}
	// Ambush level: the fight starts by the bot's strike or by a detection — never by the button.
	if (bStealthLevel && (Stage == EBotStage::Explore || Stage == EBotStage::Stealth))
	{
		const UGameFlowSubsystem* StealthFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		if (StealthFlow && (StealthFlow->IsCombatUnlocked() || StealthFlow->GetPhase() != ECodexGamePhase::Exploration))
		{
			OnStealthCombatStarted();
			return;
		}
	}
	switch (Stage)
	{
	case EBotStage::Explore:
		TickExplore(DeltaTime);
		return;
	case EBotStage::Stealth:
		TickStealth(DeltaTime, false);
		return;
	case EBotStage::EnterCombat:
		// bot_driver: main._on_start_combat_pressed = "Start combat": the squad behind the gate, healed, warm, then the
		// combat cutscene / preparation.
		if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Mission->StartMission(EMissionStartMode::Combat);
			bPressedCombatStart = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Combat start (squad behind the gate)"));
		}
		Stage = EBotStage::Fight;
		return;
	case EBotStage::Fight:
		if (!bSpatialStarted)
		{
			// bot_driver: spatial_recorder.start_recording when the fight starts.
			bSpatialStarted = true;
			const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			Spatial.Start(GetWorld(), Level && !Level->Config.LevelId.IsEmpty() ? Level->Config.LevelId : FString(TEXT("outpost_gate_01")),
				PlaytestBotRules::ProfileName(Profile));
		}
		Spatial.Tick(DeltaTime);
		TickFight(DeltaTime);
		return;
	default:
		return;
	}
}

void UPlaytestBotSubsystem::TickExplore(float DeltaTime)
{
	ExploreTime += DeltaTime;
	TargetTime += DeltaTime;
	AOperativeCharacter* Leader = Member(0);
	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	const EBotStage AfterExplore = bStealthLevel ? EBotStage::Stealth : EBotStage::EnterCombat;
	if (!Leader || !Interactions)
	{
		Stage = AfterExplore;
		return;
	}
	// The menu / loot dialog of the reached target: take everything.
	if (Interactions->IsActionMenuOpen())
	{
		Interactions->ConfirmActionMenu();
		return;
	}
	if (Interactions->GetLootCrate())
	{
		Interactions->LootAll();
		Interactions->CloseLootDialog();
		ExploreTarget.Reset();
		return;
	}
	// Ambush level: the stealth step decides first (stance, hiding, striking); the loot walk waits while it holds the squad.
	if (bStealthLevel)
	{
		if (TickStealth(DeltaTime, true))
		{
			TargetTime = FMath::Max(0.f, TargetTime - DeltaTime); // hiding does not use up the walk's time limit
			bExploreWalkPaused = true;
			return;
		}
		if (bExploreWalkPaused)
		{
			bExploreWalkPaused = false;
			if (AInteractableActor* Resume = Cast<AInteractableActor>(ExploreTarget.Get()))
			{
				Interactions->RequestInteraction(Resume); // back on the way to the loot
			}
		}
	}
	AActor* Target = ExploreTarget.Get();
	const bool bTargetDone = !Target || (Cast<ALootCrateActor>(Target) && Cast<ALootCrateActor>(Target)->IsLooted());
	const float TargetLimit = Target ? BotTargetBaseLimit + FVector::Dist2D(Target->GetActorLocation(), Leader->GetActorLocation()) / BotWalkSpeedCm : 0.f;
	if (bTargetDone || TargetTime > TargetLimit)
	{
		ExploreTarget.Reset();
		ExploreTargets.RemoveAll([](const TWeakObjectPtr<AActor>& Each)
		{
			const ALootCrateActor* Crate = Cast<ALootCrateActor>(Each.Get());
			return !Each.IsValid() || (Crate && (Crate->IsLooted() || Crate->IsDestroyed()));
		});
		if (Target && TargetTime > TargetLimit)
		{
			ExploreTargets.Remove(Target); // unreachable: skip it
		}
		// Nearest next target.
		AActor* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AActor>& Each : ExploreTargets)
		{
			const float Distance = Each.IsValid() ? FVector::Dist2D(Each->GetActorLocation(), Leader->GetActorLocation()) : BestDistance;
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Each.Get();
			}
		}
		if (!Best || ExploreTime > BotExploreLimit)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Exploration done (%.0f s)"), ExploreTime);
			Stage = AfterExplore;
			return;
		}
		ExploreTarget = Best;
		TargetTime = 0.f;
		Interactions->RequestInteraction(Cast<AInteractableActor>(Best));
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Going to %s (%.0f m)"), *Best->GetName(), BestDistance / 100.f);
	}
}

void UPlaytestBotSubsystem::TickFight(float DeltaTime)
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!Flow)
	{
		return;
	}
	switch (Flow->GetPhase())
	{
	case ECodexGamePhase::Cutscene:
		Flow->FinishCutscene();
		return;
	case ECodexGamePhase::Preparation:
		if (TickGeneratorRepair(DeltaTime, false))
		{
			return; // the preparation waits for the repair
		}
		// The first preparation (wave 1 — or wave 2 after an ambush fight, whose wave 1 had none).
		if (!bDeployed)
		{
			DeployDefences();
			bDeployed = true;
			DeployWait = 0.f;
		}
		DeployWait += DeltaTime;
		if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>(); Relocation && DeployWait < BotDeployWaitLimit)
		{
			if (Relocation->GetActiveDeployCount() > 0)
			{
				return; // the defences are being carried and set up
			}
			AOperativeCharacter* Engineer = Member(1);
			FVector OnNav;
			if (PendingBarricade.IsSet() && Engineer && Engineer->GetDeployableCount(EDeployableType::Barricade) > 0
				&& ProjectToNav(PendingBarricade.GetValue(), OnNav))
			{
				Relocation->ExecuteDeploy(Engineer, EDeployableType::Barricade, OnNav, PendingYaw);
				++DeploysOrdered;
				PendingBarricade.Reset();
				return;
			}
			PendingBarricade.Reset();
		}
		Flow->FinishPreparation();
		return;
	case ECodexGamePhase::WaveCleared:
		if (UWaveVictorySubsystem* Victory = GetWorld()->GetSubsystem<UWaveVictorySubsystem>())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Wave %d cleared"), Flow->GetWaveIndex());
			Victory->ContinueAfterWave();
		}
		return;
	case ECodexGamePhase::PostCombat:
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] >>> VICTORY: all %d waves <<<"), Flow->GetConfig().TotalWaves);
		Finish(TEXT("VICTORY"), 0);
		return;
	case ECodexGamePhase::GameOver:
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] >>> DEFEAT in wave %d <<<"), Flow->GetWaveIndex());
		Finish(TEXT("DEFEAT"), 0);
		return;
	case ECodexGamePhase::WaveCombat:
		break;
	default:
		return;
	}

	StatusTimer += DeltaTime;
	if (StatusTimer >= 2.f)
	{
		StatusTimer = 0.f;
		FString Squad;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (const AOperativeCharacter* Each = Member(Index))
			{
				Squad += FString::Printf(TEXT(" %s %.0f HP cold %.0f%%;"), *Each->DisplayName.ToString(), Each->HealthComponent->GetCurrentHealth(), Each->ColdLevel);
			}
		}
		const TArray<AActor*> Enemies = LiveEnemies();
		UE_LOG(LogCodexTactics, Display, TEXT("[Battlefield] wave %d, enemies %d |%s"), Flow->GetWaveIndex(), Enemies.Num(), *Squad);
		if (Enemies.Num() > 0 && Enemies.Num() <= 3)
		{
			// bot_driver _print_battlefield_status: the last few enemies with their health and position.
			for (const AActor* Enemy : Enemies)
			{
				const AEnemyCharacter* Typed = Cast<AEnemyCharacter>(Enemy);
				const AActor* Target = Typed ? Typed->GetCurrentTarget() : nullptr;
				const float EnemyFeet = Enemy->GetActorLocation().Z - Enemy->GetSimpleCollisionHalfHeight();
				const float TargetFeet = Target ? Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight() : 0.f;
				UE_LOG(LogCodexTactics, Display, TEXT("   -> %s HP %.0f at %s, %.0f cm/s, target %s %.0f cm away, feet dz %.0f"), *Enemy->GetName(),
					Typed ? Typed->GetHealthComponent()->GetCurrentHealth() : 0.f, *Enemy->GetActorLocation().ToCompactString(),
					Enemy->GetVelocity().Size2D(), Target ? *Target->GetName() : TEXT("-"),
					Target ? FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation()) : 0.f, TargetFeet - EnemyFeet);
			}
		}
	}
	// Wave stall (not in Godot's bot): the last <= 3 enemies have not died for 30 s -> the squad goes after them, as a
	// player would; the log keeps who was stuck where.
	const TArray<AActor*> Remaining = LiveEnemies();
	StallTimer = Remaining.Num() == LastEnemyCount ? StallTimer + DeltaTime : 0.f;
	LastEnemyCount = Remaining.Num();
	if (Remaining.Num() > 0 && Remaining.Num() <= 3 && StallTimer > 30.f)
	{
		StallTimer = 0.f;
		++StallHunts;
		const AActor* Prey = Remaining[0];
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Stall: %d enemies alive for 30 s, the squad hunts %s at %s"), Remaining.Num(),
			*Prey->GetName(), *Prey->GetActorLocation().ToCompactString());
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (AOperativeCharacter* Each = Member(Index))
			{
				if (Each->bGuarding)
				{
					if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
					{
						Squad->ToggleGuard(Each);
					}
				}
				Each->OrderMoveTo(Prey->GetActorLocation() - (Prey->GetActorLocation() - Each->GetActorLocation()).GetSafeNormal2D() * 800.f, false);
			}
		}
		MoveCooldown = 8.f;
	}
	DecisionTimer += DeltaTime;
	if (DecisionTimer >= BotDecisionInterval)
	{
		DecisionTimer = 0.f;
		CombatAssist();
	}
	if (TickGeneratorRepair(DeltaTime, true))
	{
		return; // the repair walk owns the moves for now
	}
	SmartTactics(DeltaTime);
}

void UPlaytestBotSubsystem::DeployDefences()
{
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	AOperativeCharacter* Commander = Member(0);
	AOperativeCharacter* Engineer = Member(1);
	AOperativeCharacter* Medic = Member(2);
	if (!Relocation)
	{
		return;
	}
	const FVector Front = GetFrontDirection();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Front);
	const float Yaw = Front.Rotation().Yaw;
	auto Feet = [](const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	};
	auto Deploy = [this, Relocation, Yaw](AOperativeCharacter* Worker, EDeployableType Type, const FVector& Point)
	{
		FVector OnNav;
		if (Worker && Worker->GetDeployableCount(Type) > 0 && ProjectToNav(Point, OnNav))
		{
			Relocation->ExecuteDeploy(Worker, Type, OnNav, Yaw);
			++DeploysOrdered;
		}
		else if (Worker)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] %s: no walkable spot for the deployable at %s"), *Worker->DisplayName.ToString(), *Point.ToCompactString());
		}
	};
	// _veteran_deploy_defenses / _normal_deploy_defenses (Godot -Z = Front, metres -> cm).
	if (Config.Turrets > 0 && Commander)
	{
		Deploy(Commander, EDeployableType::Turret, Feet(Commander) + Front * 350.f);
	}
	if (Config.Barricades == 1 && Engineer)
	{
		Deploy(Engineer, EDeployableType::Barricade, Feet(Engineer) + Front * 300.f);
	}
	else if (Config.Barricades >= 2 && Engineer)
	{
		Deploy(Engineer, EDeployableType::Barricade, Feet(Engineer) + Front * 300.f - Right * 250.f);
		PendingBarricade = Feet(Engineer) + Front * 300.f + Right * 250.f; // after the first one stands
		PendingYaw = Yaw;
	}
	if (Config.Mines > 0 && Medic)
	{
		Deploy(Medic, EDeployableType::Mine, Feet(Medic) + Front * (Config.Barricades >= 2 ? 600.f : 500.f));
	}
	if (Config.bGuardAfterDeploy)
	{
		USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Guard : { Engineer, Medic })
		{
			if (Guard && Squad && !Guard->bGuarding)
			{
				Guard->SetStance(EOperativeStance::Crouching);
				Squad->ToggleGuard(Guard);
			}
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] %s defences ordered: %d"), *PlaytestBotRules::ProfileName(Profile), DeploysOrdered);
}

void UPlaytestBotSubsystem::CombatAssist(bool bAllowWarmMoves)
{
	// _veteran_combat_assist / _normal_combat_assist with the real supplies (deviation: Godot healed with pause charges
	// and lowered the cold by decree).
	WarmMoveCooldown = FMath::Max(0.f, WarmMoveCooldown - BotDecisionInterval);
	bool bNeedsWarmth = false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AOperativeCharacter* Each = Member(Index);
		if (!Each)
		{
			continue;
		}
		const UHealthComponent* Health = Each->HealthComponent;
		if (Config.HealBelowHealthFraction > 0.f && Health->GetCurrentHealth() < Health->GetMaxHealth() * Config.HealBelowHealthFraction
			&& Each->UsePersonalItem(EPersonalItem::Medkit))
		{
			++ItemsUsed;
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] %s: medkit"), *Each->DisplayName.ToString());
			return;
		}
		if (Each->ColdLevel > Config.WarmAboveCold)
		{
			if (Each->UsePersonalItem(EPersonalItem::Chocolate) || Each->UsePersonalItem(EPersonalItem::CannedFood))
			{
				++ItemsUsed;
				UE_LOG(LogCodexTactics, Display, TEXT("[Bot] %s: warming food (cold %.0f%%)"), *Each->DisplayName.ToString(), Each->ColdLevel);
				return;
			}
			bNeedsWarmth = true;
		}
	}
	// No food left: the squad steps into the nearest active heat zone (as a player would; Godot's bot had no need).
	AOperativeCharacter* Leader = Member(0);
	if (bNeedsWarmth && bAllowWarmMoves && Leader && WarmMoveCooldown <= 0.f)
	{
		const UHeatSourceComponent* Best = nullptr;
		float BestDistance = 4000.f;
		const double Now = GetWorld()->GetTimeSeconds();
		// The last trip did not warm the leader (cold not lower): that heat is skipped for 30 s (no stall loops).
		if (WarmTarget.IsValid() && Leader->ColdLevel >= WarmStartCold)
		{
			ColdHeatUntil.Add(WarmTarget, Now + 30.0);
		}
		WarmTarget.Reset();
		for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
		{
			// Only a heat that really warms: active, and a generator neither broken nor drained (Sprint 05-D).
			const AInteractableActor* Generator = Source.IsValid() ? Cast<AInteractableActor>(Source->GetOwner()) : nullptr;
			const bool bGeneratorDown = Generator && Generator->ObjectType == EInteractableType::Generator
				&& (Generator->bGeneratorBroken || Generator->GeneratorHealth <= 0.f);
			const double* Skip = Source.IsValid() ? ColdHeatUntil.Find(Source.Get()) : nullptr;
			const bool bUsable = Source.IsValid() && Source->GetWorld() == GetWorld() && Source->IsHeatActive() && !bGeneratorDown
				&& !(Skip && *Skip > Now);
			const float Distance = bUsable ? FVector::Dist2D(Source->GetComponentLocation(), Leader->GetActorLocation()) : BestDistance;
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Source.Get();
			}
		}
		if (Best && BestDistance > Best->Radius * 0.5f)
		{
			Leader->OrderMoveTo(Best->GetComponentLocation() + (Leader->GetActorLocation() - Best->GetComponentLocation()).GetSafeNormal2D() * Best->Radius * 0.4f, false);
			++WarmMoves;
			WarmTarget = Best;
			WarmStartCold = Leader->ColdLevel;
			MoveCooldown = 10.f;
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] No warming food: the squad goes to the heat at %.0f m"), BestDistance / 100.f);
		}
		WarmMoveCooldown = 10.f;
	}
}

void UPlaytestBotSubsystem::SmartTactics(float DeltaTime)
{
	GrenadeCooldown = FMath::Max(0.f, GrenadeCooldown - DeltaTime);
	MoveCooldown = FMath::Max(0.f, MoveCooldown - DeltaTime);
	AOperativeCharacter* Leader = Member(0);
	const TArray<AActor*> Enemies = LiveEnemies();
	if (!Leader || Enemies.IsEmpty())
	{
		return;
	}
	TArray<FVector> Positions;
	for (const AActor* Enemy : Enemies)
	{
		Positions.Add(Enemy->GetActorLocation());
	}
	// 0. A lone marksman out of reach: storm him; else his telegraphed aim at a squad member comes first (Sprint 05-D).
	if (AssaultMarksman() || ReactToMarksman())
	{
		return;
	}
	// 1. A grenade at a cluster.
	if (GrenadeCooldown <= 0.f && Leader->GrenadesCount > 0)
	{
		FVector Center;
		const int32 Count = PlaytestBotRules::FindCluster(Positions, BotClusterRadius, Center);
		if (PlaytestBotRules::ShouldThrowGrenade(Count, FVector::Dist2D(Leader->GetActorLocation(), Center), Leader->GrenadeThrowRange))
		{
			UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
			Leader->SwitchToWeaponById(TEXT("grenade"));
			if (Grenades && Grenades->StartAim(Leader) && Grenades->ThrowAtCursor(Center))
			{
				++GrenadesThrown;
				GrenadeCooldown = 4.5f;
				TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
				Details->SetNumberField(TEXT("cluster"), Count);
				Spatial.RecordEvent(TEXT("GRENADE_THROWN"), Leader->DisplayName.ToString(), FString(), Center, Details);
				UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Grenade at a cluster of %d"), Count);
				return;
			}
			if (Grenades && Grenades->IsAiming())
			{
				Grenades->CancelAim();
			}
			GrenadeCooldown = 1.f;
		}
	}
	// 2. Fall back when an enemy is too close.
	if (MoveCooldown <= 0.f)
	{
		const AActor* Closest = nullptr;
		float ClosestDistance = TNumericLimits<float>::Max();
		for (const AActor* Enemy : Enemies)
		{
			const float Distance = FVector::Dist2D(Enemy->GetActorLocation(), Leader->GetActorLocation());
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				Closest = Enemy;
			}
		}
		if (Closest && ClosestDistance < BotRetreatDistance)
		{
			Leader->OrderMoveTo(PlaytestBotRules::FallbackPosition(Leader->GetActorLocation(), Closest->GetActorLocation(), -GetFrontDirection()), false);
			MoveCooldown = 2.f;
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("reason"), TEXT("TACTICAL_RETREAT"));
			Details->SetNumberField(TEXT("threat_dist"), ClosestDistance / 100.f);
			Spatial.RecordEvent(TEXT("COVER_LEAVE"), Leader->DisplayName.ToString(), FString(), Leader->GetActorLocation(), Details);
			return;
		}
	}
	// 3. The best barricade cover for the leader.
	if (MoveCooldown <= 0.f && !Leader->IsBehindBarricade())
	{
		FVector Threat = FVector::ZeroVector;
		for (const FVector& Position : Positions)
		{
			Threat += Position;
		}
		Threat /= Positions.Num();
		FVector BestStand;
		FString BestId;
		float BestScore = 0.f;
		if (FindCover(Leader, Threat, BestStand, BestId, BestScore))
		{
			Leader->OrderMoveTo(BestStand, false);
			MoveCooldown = 3.f;
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetNumberField(TEXT("tps_score"), BestScore);
			Spatial.RecordEvent(TEXT("COVER_ENTER"), Leader->DisplayName.ToString(), BestId, BestStand, Details);
			return;
		}
		// No barricade: a Sprint 12 wall-cover slot close by with the wall towards the enemies (Codex.Bot.WallCover).
		FCoverSlot Slot;
		if (BotTuning::CVarWallCover.GetValueOnGameThread() != 0 && !Leader->bInCover && !Leader->HasPendingCover()
			&& FindWallCover(Leader, Threat, BotTuning::CVarWallCoverRadius.GetValueOnGameThread(), Slot)
			&& Leader->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted)
		{
			MoveCooldown = 4.f;
			++WallCoverMoves;
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("reason"), TEXT("WALL_COVER"));
			Details->SetBoolField(TEXT("high"), Slot.Height == ECoverHeight::HighCover);
			Spatial.RecordEvent(TEXT("COVER_ENTER"), Leader->DisplayName.ToString(), GetNameSafe(Slot.WallActor.Get()), Slot.WorldLocation, Details);
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] %s takes wall cover (%s) %.0f m away"), *Leader->DisplayName.ToString(),
				Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"), FVector::Dist2D(Slot.WorldLocation, Leader->GetActorLocation()) / 100.f);
			return;
		}
	}
	// 4. Stances: crouch behind a barricade, stand up when out of it.
	UpdateStances();
}

bool UPlaytestBotSubsystem::FindCover(const AOperativeCharacter* Leader, const FVector& Threat, FVector& OutStand, FString& OutId,
	float& OutScore) const
{
	OutScore = -TNumericLimits<float>::Max();
	for (TActorIterator<ABarricadeActor> It(GetWorld()); It; ++It)
	{
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (!Health || !Health->IsAlive() || FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation()) > BotCoverSearchRadius)
		{
			continue;
		}
		const FVector Stand = PlaytestBotRules::CoverStandPoint(It->GetActorLocation(), Threat);
		const float Score = PlaytestBotRules::CoverScore(FVector::Dist2D(Leader->GetActorLocation(), Stand),
			Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 1.f,
			It->GetActorLocation().Z - Leader->GetActorLocation().Z >= 150.f);
		if (Score > OutScore)
		{
			OutScore = Score;
			OutStand = Stand;
			OutId = It->GetName();
		}
	}
	return OutScore > -TNumericLimits<float>::Max();
}

bool UPlaytestBotSubsystem::AssaultMarksman()
{
	using namespace BotTuning;
	AOperativeCharacter* Leader = Member(0);
	if (!Leader || CVarAssault.GetValueOnGameThread() == 0 || MoveCooldown > 0.f)
	{
		return false;
	}
	const float Reach = Leader->CurrentWeapon ? Leader->CurrentWeapon->AttackRangeCm : 1400.f;
	const float ClearRadius = CVarClearRadius.GetValueOnGameThread();
	const AMarksmanEnemyCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (AActor* Enemy : LiveEnemies())
	{
		const float Distance = FVector::Dist2D(Enemy->GetActorLocation(), Leader->GetActorLocation());
		const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Enemy);
		if (!Marksman && Distance < ClearRadius)
		{
			return false; // the melee fight comes first
		}
		if (Marksman && Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Marksman;
		}
	}
	if (!Nearest || NearestDistance <= Reach)
	{
		return false;
	}
	const FVector Target = Nearest->GetActorLocation();
	const FVector Back = (Leader->GetActorLocation() - Target).GetSafeNormal2D();
	const float Stop = CVarStopDistance.GetValueOnGameThread();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AOperativeCharacter* Each = Member(Index);
		if (Each && BotIsAlive(Each))
		{
			// Fanned out: the middle one straight on, the others 35 deg to either side.
			const float Angle = Index == 0 ? 0.f : (Index == 1 ? 35.f : -35.f);
			Each->OrderMoveTo(Target + Back.RotateAngleAxis(Angle, FVector::UpVector) * Stop, true);
		}
	}
	MoveCooldown = 2.5f;
	++MarksmanAssaults;
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Assault marksman %s at %.0f m (reach %.0f m)"), *Nearest->GetName(), NearestDistance / 100.f,
		Reach / 100.f);
	return true;
}

bool UPlaytestBotSubsystem::ReactToMarksman()
{
	// A marksman whose telegraphed aim (the beam) is on a squad member: the squad crouches, the leader takes the nearest
	// barricade at once (no move cooldown); without a reachable cover they stay crouched.
	AOperativeCharacter* Leader = Member(0);
	for (TActorIterator<AMarksmanEnemyCharacter> It(GetWorld()); It; ++It)
	{
		const AOperativeCharacter* Target = Cast<AOperativeCharacter>(It->GetCurrentTarget());
		if (!It->IsAimingAtTarget() || It->IsDying() || !Target || !BotIsAlive(Target))
		{
			continue;
		}
		bool bReacted = false;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			AOperativeCharacter* Each = Member(Index);
			if (Each && Each->GetStance() == EOperativeStance::Standing && !Each->IsMoving())
			{
				Each->SetStance(EOperativeStance::Crouching);
				bReacted = true;
			}
		}
		FVector Stand;
		FString CoverId;
		float Score = 0.f;
		if (Leader && MoveCooldown <= 0.f && !Leader->IsBehindBarricade() && FindCover(Leader, It->GetActorLocation(), Stand, CoverId, Score))
		{
			Leader->OrderMoveTo(Stand, false);
			MoveCooldown = 3.f;
			bReacted = true;
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("reason"), TEXT("MARKSMAN_AIM"));
			Details->SetNumberField(TEXT("aim_progress"), It->GetAimProgress());
			Spatial.RecordEvent(TEXT("COVER_ENTER"), Leader->DisplayName.ToString(), CoverId, Stand, Details);
		}
		if (bReacted)
		{
			++MarksmanReactions;
			// One line per aim (the reaction repeats every tick of it).
			const double Now = GetWorld()->GetTimeSeconds();
			if (LastReactedMarksman.Get() != *It || Now - LastReactLogTime > 3.0)
			{
				LastReactedMarksman = *It;
				LastReactLogTime = Now;
				UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Marksman %s aims at %s (%.0f%%): crouch%s"), *It->GetName(),
					*Target->DisplayName.ToString(), It->GetAimProgress() * 100.f, CoverId.IsEmpty() ? TEXT("") : TEXT(", leader to cover"));
			}
		}
		return bReacted;
	}
	return false;
}

AInteractableActor* UPlaytestBotSubsystem::FindBrokenGenerator() const
{
	for (TActorIterator<AInteractableActor> It(GetWorld()); It; ++It)
	{
		if (It->ObjectType == EInteractableType::Generator && It->bGeneratorBroken)
		{
			return *It;
		}
	}
	return nullptr;
}

bool UPlaytestBotSubsystem::TickGeneratorRepair(float DeltaTime, bool bInWave)
{
	RepairCooldown = FMath::Max(0.f, RepairCooldown - DeltaTime);
	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AInteractableActor* Generator = RepairTarget.Get();
	if (Generator)
	{
		RepairTime += DeltaTime;
		// The repair menu opened on arrival: confirm it (the worker crouches and repairs, 2.5 s for the engineer).
		if (Interactions && Interactions->IsActionMenuOpen())
		{
			Interactions->ConfirmActionMenu();
		}
		const bool bDone = !Generator->bGeneratorBroken;
		if (bDone || RepairTime > 30.f || !RepairWorker.IsValid() || !BotIsAlive(RepairWorker.Get()))
		{
			if (bDone)
			{
				++GeneratorRepairs;
				UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Generator repaired by %s (%.1f s)"),
					RepairWorker.IsValid() ? *RepairWorker->DisplayName.ToString() : TEXT("?"), RepairTime);
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Generator repair given up after %.1f s"), RepairTime);
				RepairCooldown = 20.f;
			}
			RepairTarget.Reset();
			RepairWorker.Reset();
			if (Squad && Member(0))
			{
				Squad->SetLeader(Member(0)); // the commander leads again
			}
			return false;
		}
		return true;
	}
	if (RepairCooldown > 0.f || !Interactions || !Squad)
	{
		return false;
	}
	Generator = FindBrokenGenerator();
	if (!Generator)
	{
		return false;
	}
	if (bInWave)
	{
		for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsDying() && BotIsAlive(*It) && FVector::Dist2D(It->GetActorLocation(), Generator->GetActorLocation()) < 1200.f)
			{
				return false; // not under the enemies' noses
			}
		}
	}
	AOperativeCharacter* Worker = Member(1) ? Member(1) : Member(0); // the engineer repairs twice as fast
	if (!Worker)
	{
		return false;
	}
	if (Worker->bGuarding)
	{
		Squad->ToggleGuard(Worker);
	}
	Squad->SetLeader(Worker);
	if (!Interactions->RequestInteraction(Generator))
	{
		Squad->SetLeader(Member(0));
		RepairCooldown = 10.f;
		return false;
	}
	RepairTarget = Generator;
	RepairWorker = Worker;
	RepairTime = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Generator broken: %s goes to repair it (%s)"), *Worker->DisplayName.ToString(),
		bInWave ? TEXT("during the wave") : TEXT("in the preparation"));
	return true;
}

bool UPlaytestBotSubsystem::ProjectToNav(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Location;
	if (Nav && Nav->ProjectPointToNavigation(Point, Location, FVector(200.f, 200.f, 300.f)))
	{
		OutPoint = Location.Location;
		return true;
	}
	return !Nav; // no navigation system: trust the point
}

void UPlaytestBotSubsystem::UpdateStances()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AOperativeCharacter* Each = Member(Index);
		// A wall-cover slot runs its own stance (low cover crouched, high cover by the corner rules).
		if (!Each || Each->IsMoving() || Each->bInCover || Each->HasPendingCover())
		{
			continue;
		}
		const bool bCover = Each->IsBehindBarricade();
		if (bCover && Each->GetStance() != EOperativeStance::Crouching)
		{
			Each->SetStance(EOperativeStance::Crouching);
		}
		else if (!bCover && Each->GetStance() == EOperativeStance::Crouching && !Each->bGuarding)
		{
			Each->SetStance(EOperativeStance::Standing);
		}
	}
}

void UPlaytestBotSubsystem::HandleEnemySpawned(AEnemyCharacter* Enemy, EEnemyArchetype Archetype)
{
	if (UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr)
	{
		Health->OnDiedNative.AddUObject(this, &UPlaytestBotSubsystem::HandleEnemyDied);
	}
}

void UPlaytestBotSubsystem::HandleEnemyDied(AActor* Victim, const FString& Source)
{
	if (!Victim || !Spatial.IsRecording())
	{
		return;
	}
	// The killer's height above the victim (Godot killer_height: kills from elevated ground).
	float KillerHeight = 0.f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (const AOperativeCharacter* Each = Member(Index); Each && Each->DisplayName.ToString() == Source)
		{
			KillerHeight = (Each->GetActorLocation().Z - Each->GetSimpleCollisionHalfHeight() - (Victim->GetActorLocation().Z - Victim->GetSimpleCollisionHalfHeight())) / 100.f;
		}
	}
	TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
	Details->SetNumberField(TEXT("killer_height"), KillerHeight);
	Spatial.RecordEvent(TEXT("ENEMY_DEATH"), Source, Victim->GetName(), Victim->GetActorLocation(), Details);
}

void UPlaytestBotSubsystem::GatherPatrols(TArray<AEnemyCharacter*>& OutEnemies, TArray<FBotPatrolView>& OutViews) const
{
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDying() || !BotIsAlive(*It) || !It->IsOnPatrol())
		{
			continue;
		}
		FBotPatrolView View;
		View.Location = It->GetActorLocation();
		View.Forward = It->GetActorForwardVector().GetSafeNormal2D();
		// The enemy's own perception in force (data file + Codex.Perception.* knobs), heightened while searching.
		View.bSearching = It->IsSearching();
		View.Params = View.bSearching ? PerceptionRules::Scaled(It->GetPerception(), EnemyPerception::GetSearch().PerceptionMultiplier) : It->GetPerception();
		View.Suspicion = It->GetSuspicion();
		OutEnemies.Add(*It);
		OutViews.Add(View);
	}
}

bool UPlaytestBotSubsystem::IsEyeLineClear(const AEnemyCharacter* Enemy, const AOperativeCharacter* Operative) const
{
	if (!Enemy || !Operative || Operative->IsHiddenInCoverFrom(Enemy->GetActorLocation()))
	{
		return false;
	}
	// The enemy's own sight trace (AEnemyCharacter::FindVisibleOperative): eyes -> profile, pawns never block.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotStealthSight), false, Enemy);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, UTacticalSightSubsystem::EyePoint(*Enemy), UTacticalSightSubsystem::ProfilePoint(*Operative),
		ECC_Visibility, Params);
}

void UPlaytestBotSubsystem::ApplyStealthStance(EOperativeStance Stance, float DeltaTime)
{
	auto Loudness = [](EOperativeStance Each) { return Each == EOperativeStance::Standing ? 2 : (Each == EOperativeStance::Crouching ? 1 : 0); };
	if (Stance == StealthStance)
	{
		StanceLouderTime = 0.f;
		return;
	}
	// Quieter at once; louder only once it has been safe for a second (no stand-up / get-down churn).
	if (Loudness(Stance) > Loudness(StealthStance))
	{
		StanceLouderTime += DeltaTime;
		if (StanceLouderTime < 1.f)
		{
			return;
		}
	}
	StanceLouderTime = 0.f;
	StealthStance = Stance;
	++StanceChanges;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AOperativeCharacter* Each = Member(Index);
		if (Each && !Each->bInCover && Each->GetStance() != Stance)
		{
			Each->SetStance(Stance);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot stance %s at %.1f s"), BotStealthRules::StanceName(Stance), GetWorld()->GetTimeSeconds());
}

bool UPlaytestBotSubsystem::TickStealth(float DeltaTime, bool bExploring)
{
	StealthMoveCooldown = FMath::Max(0.f, StealthMoveCooldown - DeltaTime);
	StealthDecisionTimer += DeltaTime;
	const bool bTrapWalk = TrapState == 1 || TrapState == 2;
	if (StealthDecisionTimer < BotStealthInterval)
	{
		// Between decisions the last one stands: hiding / holding / the trap walk keep the squad.
		return StealthAction != EBotStealthAction::Sneak || bTrapWalk;
	}
	const float Step = StealthDecisionTimer;
	StealthDecisionTimer = 0.f;
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || !BotIsAlive(Leader))
	{
		Leader = Member(0);
	}
	if (!Leader || !Squad)
	{
		return false;
	}
	CombatAssist(false); // medkits / warming food while sneaking (no walks to a heat source)
	const double Now = World->GetTimeSeconds();
	TArray<AEnemyCharacter*> Patrols;
	TArray<FBotPatrolView> Views;
	GatherPatrols(Patrols, Views);
	for (AEnemyCharacter* Each : Patrols)
	{
		KnownPatrols.Add(Each);
	}
	// A known patrol enemy, alive, off its patrol and not by the bot's strike: it noticed something.
	if (FirstDetectionTime < 0.0 && !bAmbushIssued)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Known : KnownPatrols)
		{
			const AEnemyCharacter* Enemy = Known.Get();
			if (Enemy && !Enemy->IsDying() && BotIsAlive(Enemy) && !Enemy->IsOnPatrol())
			{
				FirstDetectionTime = Now;
				UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot detected at %.1f s by %s (sneaking %.1f s)"), Now, *Enemy->GetName(), Now - StealthStartTime);
				break;
			}
		}
	}

	// Suspicion, searches and the target (the nearest patrol enemy; the nearest searcher for a search contact).
	const FVector LeaderLocation = Leader->GetActorLocation();
	FBotStealthInput Input;
	AEnemyCharacter* Suspicious = nullptr;
	AEnemyCharacter* Searcher = nullptr;
	AEnemyCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Patrols.Num(); ++Index)
	{
		const float Distance = FVector::Dist2D(LeaderLocation, Views[Index].Location);
		if (Views[Index].Suspicion > Input.MaxSuspicion)
		{
			Input.MaxSuspicion = Views[Index].Suspicion;
			Input.SuspiciousDistanceCm = Distance;
			Suspicious = Patrols[Index];
		}
		if (Views[Index].bSearching && Distance < Input.SearcherDistanceCm)
		{
			Input.SearcherDistanceCm = Distance;
			Searcher = Patrols[Index];
		}
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Patrols[Index];
		}
	}
	const bool bSearchOn = Searcher != nullptr;
	if (bSearchOn && !bSearchWasOn)
	{
		++SearchesSeen;
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot sees a search at %.1f s (%s %.0f m away)"), Now, *Searcher->GetName(),
			Input.SearcherDistanceCm / 100.f);
	}
	bSearchWasOn = bSearchOn;
	AEnemyCharacter* Target = StealthTarget.Get();
	if (!Target || Target->IsDying() || !BotIsAlive(Target) || !Target->IsOnPatrol())
	{
		Target = Nearest;
		StealthTarget = Target;
	}
	if (!Target && !bExploring)
	{
		// No patrol left to sneak up on: strike the nearest enemy, else the classic start.
		AActor* Any = nullptr;
		float AnyDistance = TNumericLimits<float>::Max();
		for (AActor* Enemy : LiveEnemies())
		{
			const float Distance = FVector::Dist2D(Enemy->GetActorLocation(), LeaderLocation);
			if (Distance < AnyDistance)
			{
				AnyDistance = Distance;
				Any = Enemy;
			}
		}
		if (AEnemyCharacter* AnyEnemy = Cast<AEnemyCharacter>(Any))
		{
			StrikeFirst(AnyEnemy, EBotAmbushReason::Forced, AnyDistance);
		}
		else
		{
			StealthOutcome = TEXT("no_patrols");
			UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] outcome no_patrols at %.1f s: nothing to sneak up on, the bot starts the fight"), Now);
			Stage = EBotStage::EnterCombat;
		}
		return true;
	}

	// Eye lines (only where the sight could reach) and the stance that keeps the leader under every patrol's senses.
	TArray<bool> LeaderLines;
	Input.bSquadUnseen = true;
	for (int32 Index = 0; Index < Patrols.Num(); ++Index)
	{
		const float Reach = PerceptionRules::EffectiveSightRange(Views[Index].Params, EOperativeStance::Standing) * StealthConfig.SightMargin * 1.1f;
		LeaderLines.Add(FVector::Dist2D(LeaderLocation, Views[Index].Location) > Reach || IsEyeLineClear(Patrols[Index], Leader));
		for (int32 MemberIndex = 0; MemberIndex < 3 && Input.bSquadUnseen; ++MemberIndex)
		{
			const AOperativeCharacter* Each = Member(MemberIndex);
			if (!Each || FVector::Dist2D(Each->GetActorLocation(), Views[Index].Location) > Reach)
			{
				continue;
			}
			const bool bClear = Each == Leader ? LeaderLines.Last() : IsEyeLineClear(Patrols[Index], Each);
			Input.bSquadUnseen = BotStealthRules::SightRisk(Views[Index].Params, Views[Index].Location, Views[Index].Forward, Each->GetActorLocation(),
				Each->GetStance(), bClear, StealthConfig) < 1.f;
		}
	}
	const EOperativeStance Desired = BotStealthRules::ChooseSneakStance(Views, LeaderLines, LeaderLocation, StealthConfig);

	Input.bHasTarget = Target != nullptr;
	Input.TargetDistanceCm = Target ? FVector::Dist2D(LeaderLocation, Target->GetActorLocation()) : TNumericLimits<float>::Max();
	Input.RifleRangeCm = Leader->CurrentWeapon ? Leader->CurrentWeapon->AttackRangeCm : 1400.f;
	Input.bLeaderInCover = Leader->bInCover || Leader->IsBehindBarricade();
	Input.ElapsedSeconds = static_cast<float>(Now - StealthStartTime);
	Input.bTrapPending = TrapState == 3;
	// Frost hounds smell whatever the stance: strike from beyond their nose, and strike now when it is about to work.
	const float SmellReach = BotStealthRules::SmellReach(Views, StealthConfig);
	Input.MinAmbushRangeCm = SmellReach;
	FVector SmellerLocation = FVector::ZeroVector;
	for (int32 Index = 0; Index < Views.Num() && !Input.bSmellImminent; ++Index)
	{
		for (int32 MemberIndex = 0; MemberIndex < 3; ++MemberIndex)
		{
			const AOperativeCharacter* Each = Member(MemberIndex);
			if (Each && Views[Index].Params.SmellRadiusCm > 0.f
				&& FVector::Dist(Each->GetActorLocation(), Views[Index].Location) <= Views[Index].Params.SmellRadiusCm * 1.25f)
			{
				Input.bSmellImminent = true;
				SmellerLocation = Views[Index].Location;
			}
		}
	}

	// The trap opener first (not while a patrol grows suspicious): the medic lays a mine on the target's route.
	if (!bExploring && StealthConfig.bUseTrap && TrapState < 4 && Target && Input.MaxSuspicion < StealthConfig.AlarmSuspicion
		&& TickTrap(Target, Patrols))
	{
		ApplyStealthStance(Desired, Step);
		return true;
	}
	Input.bTrapPending = TrapState == 3;

	EBotAmbushReason Reason = EBotAmbushReason::None;
	const EBotStealthAction Action = BotStealthRules::Decide(Input, StealthConfig, Reason);
	++StealthDecisions;
	if (Action != StealthAction && Action != EBotStealthAction::Ambush) // StrikeFirst logs the ambush itself
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot %s at %.1f s (stance %s, target %s %.0f m, suspicion %.2f, search %d, cover %d)"),
			BotStealthRules::ActionName(Action), Now, BotStealthRules::StanceName(StealthStance), *GetNameSafe(Target), Input.TargetDistanceCm / 100.f,
			Input.MaxSuspicion, bSearchOn ? 1 : 0, Input.bLeaderInCover ? 1 : 0);
	}
	const EBotStealthAction Previous = StealthAction;
	StealthAction = Action;
	switch (Action)
	{
	case EBotStealthAction::Ambush:
	{
		AEnemyCharacter* Victim = Reason == EBotAmbushReason::PreEmptive && Suspicious ? Suspicious
			: (Reason == EBotAmbushReason::SearchContact && Searcher ? Searcher : Target);
		StrikeFirst(Victim, Reason, Victim ? FVector::Dist2D(LeaderLocation, Victim->GetActorLocation()) : 0.f);
		return true;
	}
	case EBotStealthAction::Hide:
		if (Previous != EBotStealthAction::Hide)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (AOperativeCharacter* Each = Member(Index); Each && !Each->bInCover && Each->IsMoving())
				{
					Each->StopOperative();
				}
			}
			Squad->SetFollowersHolding(true);
			bFollowersHeld = true;
		}
		// Still = silent; prone unless the wall already hides him.
		ApplyStealthStance(Input.bLeaderInCover ? EOperativeStance::Crouching : EOperativeStance::Prone, 1.f);
		return true;
	case EBotStealthAction::Evade:
	{
		// Away from the nose, at the quietest gait that still moves (no sprint: running is heard far).
		if (bFollowersHeld)
		{
			Squad->SetFollowersHolding(false);
			bFollowersHeld = false;
		}
		ApplyStealthStance(Desired == EOperativeStance::Prone ? EOperativeStance::Crouching : Desired, 1.f);
		if (Previous != EBotStealthAction::Evade || StealthMoveCooldown <= 0.f)
		{
			StealthMoveCooldown = 1.5f;
			FVector Away = (LeaderLocation - SmellerLocation).GetSafeNormal2D();
			Away = Away.IsNearlyZero() ? -Leader->GetActorForwardVector() : Away;
			FVector OnNav;
			for (const float Turn : { 0.f, 40.f, -40.f, 80.f, -80.f })
			{
				if (ProjectToNav(LeaderLocation + Away.RotateAngleAxis(Turn, FVector::UpVector) * 800.f, OnNav))
				{
					Leader->OrderMoveTo(OnNav, false);
					break;
				}
			}
		}
		return true;
	}
	case EBotStealthAction::Hold:
	{
		if (bFollowersHeld)
		{
			Squad->SetFollowersHolding(false);
			bFollowersHeld = false;
		}
		FCoverSlot Slot;
		if (!Input.bLeaderInCover && !Leader->HasPendingCover() && StealthMoveCooldown <= 0.f && Target)
		{
			StealthMoveCooldown = 4.f;
			if (FindWallCover(Leader, Target->GetActorLocation(), 800.f, Slot) && Leader->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted)
			{
				++WallCoverMoves;
				UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot takes cover (%s) %.0f m away at %.1f s"),
					Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"), FVector::Dist2D(Slot.WorldLocation, LeaderLocation) / 100.f, Now);
			}
			else
			{
				Leader->StopOperative();
				ApplyStealthStance(EOperativeStance::Prone, 1.f);
			}
		}
		return true;
	}
	default:
		break;
	}
	// Sneak: the quietest needed gait; exploring walks on to the loot, else the leader closes in behind the target.
	if (bFollowersHeld)
	{
		Squad->SetFollowersHolding(false);
		bFollowersHeld = false;
	}
	ApplyStealthStance(Desired, Step);
	if (bExploring || !Target)
	{
		return false;
	}
	if (StealthMoveCooldown <= 0.f)
	{
		StealthMoveCooldown = BotStealthMoveInterval;
		const float Standoff = FMath::Min(Input.RifleRangeCm * 0.95f, FMath::Max(Input.RifleRangeCm * StealthConfig.AmbushRangeFraction * 0.85f, SmellReach));
		const FVector TargetLocation = Target->GetActorLocation();
		const FVector Goal = TargetLocation + (BotStealthRules::ApproachPoint(TargetLocation, Target->GetActorForwardVector(), LeaderLocation, Standoff)
			- TargetLocation).RotateAngleAxis(StealthConfig.ApproachAngleDeg, FVector::UpVector);
		FVector OnNav;
		if (ProjectToNav(Goal, OnNav) || ProjectToNav((Goal + LeaderLocation) * 0.5f, OnNav))
		{
			Leader->OrderMoveTo(OnNav, false);
		}
	}
	return true;
}

bool UPlaytestBotSubsystem::TickTrap(AEnemyCharacter* Target, const TArray<AEnemyCharacter*>& Patrols)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
	AOperativeCharacter* Commander = Member(0);
	AOperativeCharacter* Medic = Member(2);
	const double Now = World->GetTimeSeconds();
	auto GiveUp = [this, Squad, Commander](const TCHAR* Why)
	{
		TrapState = 4;
		if (Squad && Commander && Squad->GetLeader() != Commander)
		{
			Squad->SetLeader(Commander);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot trap skipped: %s"), Why);
		return false;
	};
	switch (TrapState)
	{
	case 0:
	{
		const AEnemyCharacter* RouteOwner = Target->AssignedPatrolRoute ? Target : Target->GetEscortLeader();
		const APatrolRouteActor* Route = RouteOwner ? RouteOwner->AssignedPatrolRoute.Get() : nullptr;
		if (!Squad || !Relocation || !Commander || !Medic || Medic->GetDeployableCount(EDeployableType::Mine) <= 0 || !Route)
		{
			return GiveUp(TEXT("no medic with a mine or no route"));
		}
		// The waypoint nearest the squad that no patrol enemy stands near (it will walk into it later).
		int32 BestIndex = INDEX_NONE;
		float BestDistance = 4000.f;
		FVector BestPoint = FVector::ZeroVector;
		for (int32 Index = 0; Index < Route->GetNumberOfWaypoints(); ++Index)
		{
			const FVector Point = Route->GetWaypointWorldLocation(Index);
			bool bQuiet = true;
			for (const AEnemyCharacter* Enemy : Patrols)
			{
				bQuiet &= FVector::Dist2D(Enemy->GetActorLocation(), Point) > 1500.f;
			}
			const float Distance = FVector::Dist2D(Point, Commander->GetActorLocation());
			FVector OnNav;
			if (bQuiet && Distance < BestDistance && ProjectToNav(Point, OnNav))
			{
				BestDistance = Distance;
				BestIndex = Index;
				BestPoint = OnNav;
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			return GiveUp(TEXT("no quiet waypoint within 40 m"));
		}
		TrapRetreatPoint = Commander->GetActorLocation();
		TrapMinesBefore = Medic->GetDeployableCount(EDeployableType::Mine);
		Squad->SetLeader(Medic); // the squad sneaks with him (formation), the stealth stance follows his position
		Relocation->ExecuteDeploy(Medic, EDeployableType::Mine, BestPoint, 0.f, false);
		TrapState = 1;
		TrapStateTime = Now;
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot trap: %s lays a mine at waypoint %d of %s (%.0f m) at %.1f s"), *Medic->DisplayName.ToString(),
			BestIndex, *Route->GetName(), BestDistance / 100.f, Now);
		return true;
	}
	case 1:
	{
		const bool bLaid = Medic && Medic->GetDeployableCount(EDeployableType::Mine) < TrapMinesBefore;
		const bool bIdle = Relocation && Relocation->GetActiveDeployCount() == 0 && Now - TrapStateTime > 2.0;
		if (!bLaid && !bIdle && Now - TrapStateTime < 45.0 && Medic)
		{
			return true;
		}
		if (!bLaid)
		{
			return GiveUp(TEXT("the mine was not laid"));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot trap laid at %.1f s, the squad backs off"), Now);
		if (Squad && Commander)
		{
			Squad->SetLeader(Commander);
			Commander->OrderMoveTo(TrapRetreatPoint, false);
		}
		TrapState = 2;
		TrapStateTime = Now;
		return true;
	}
	case 2:
		if (Commander && FVector::Dist2D(Commander->GetActorLocation(), TrapRetreatPoint) > 250.f && Now - TrapStateTime < 30.0)
		{
			return true;
		}
		TrapState = 3;
		TrapStateTime = Now;
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot waits for the trap at %.1f s"), Now);
		return false;
	case 3:
		if (SearchesSeen > 0 || Now - TrapStateTime > StealthConfig.TrapWaitSeconds)
		{
			TrapState = 4;
			UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot trap wait over at %.1f s (%s)"), Now, SearchesSeen > 0 ? TEXT("search seen") : TEXT("timed out"));
		}
		return false;
	default:
		return false;
	}
}

void UPlaytestBotSubsystem::StrikeFirst(AEnemyCharacter* Target, EBotAmbushReason Reason, float DistanceCm)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Target || !Squad)
	{
		return;
	}
	bAmbushIssued = true;
	AmbushReason = Reason;
	const AOperativeCharacter* Leader = Squad->GetLeader();
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] bot ambush at %.1f s (%s, target %s %.0f m, leader cover %d, stance %s, sneaking %.1f s)"),
		World->GetTimeSeconds(), BotStealthRules::ReasonName(Reason), *Target->GetName(), DistanceCm / 100.f,
		Leader && (Leader->bInCover || Leader->IsBehindBarricade()) ? 1 : 0, BotStealthRules::StanceName(StealthStance),
		World->GetTimeSeconds() - StealthStartTime);
	Squad->SetSquadPosture(BotStealthRules::PostureFor(true));
	if (bFollowersHeld)
	{
		Squad->SetFollowersHolding(false);
		bFollowersHeld = false;
	}
	// The attack order of Ctrl + click (CodexTacticsPlayerController): the ambush fight starts, every operative fires at him.
	const bool bStarted = ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::AttackOrder, Target);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (AOperativeCharacter* Each = Member(Index))
		{
			if (Each->GetStance() == EOperativeStance::Prone && !Each->bInCover)
			{
				Each->SetStance(EOperativeStance::Crouching);
			}
			Each->SetManualPriorityTarget(Target);
		}
	}
	if (!bStarted)
	{
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow || !Flow->IsCombatUnlocked())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Stealth] the attack order did not start the fight: the bot presses the start button"));
			Stage = EBotStage::EnterCombat;
		}
	}
}

void UPlaytestBotSubsystem::OnStealthCombatStarted()
{
	UWorld* World = GetWorld();
	const ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
	const EAmbushTrigger Trigger = Encounter ? Encounter->GetLastTrigger() : EAmbushTrigger::AttackOrder;
	const double Now = World->GetTimeSeconds();
	if (bAmbushIssued && Trigger == EAmbushTrigger::AttackOrder)
	{
		StealthOutcome = FString(TEXT("ambush_")) + BotStealthRules::ReasonName(AmbushReason);
	}
	else
	{
		switch (Trigger)
		{
		case EAmbushTrigger::PatrolDetection: StealthOutcome = TEXT("detected"); break;
		case EAmbushTrigger::SquadAutoFire: StealthOutcome = TEXT("auto_fire"); break;
		case EAmbushTrigger::EnemyDamaged: StealthOutcome = TEXT("damage"); break;
		default: StealthOutcome = TEXT("attack_order"); break;
		}
		if (FirstDetectionTime < 0.0 && Trigger == EAmbushTrigger::PatrolDetection)
		{
			FirstDetectionTime = Now;
		}
	}
	UE_LOG(LogCodexTactics, Display,
		TEXT("[Stealth] outcome %s at %.1f s (sneaking %.1f s, first detection %s, searches seen %d, trap %d, stance changes %d, decisions %d, seed %d)"),
		*StealthOutcome, Now, Now - StealthStartTime, FirstDetectionTime >= 0.0 ? *FString::Printf(TEXT("%.1f s"), FirstDetectionTime) : TEXT("none"),
		SearchesSeen, TrapState >= 2 && TrapState <= 4 && TrapMinesBefore > 0 ? 1 : 0, StanceChanges, StealthDecisions, Seed);
	if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		Squad->SetSquadPosture(BotStealthRules::PostureFor(true));
		if (bFollowersHeld)
		{
			Squad->SetFollowersHolding(false);
			bFollowersHeld = false;
		}
		if (Member(0) && Squad->GetLeader() != Member(0))
		{
			Squad->SetLeader(Member(0));
		}
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (AOperativeCharacter* Each = Member(Index); Each && Each->GetStance() == EOperativeStance::Prone && !Each->bInCover)
		{
			Each->SetStance(EOperativeStance::Crouching);
		}
	}
	Stage = EBotStage::Fight;
}

bool UPlaytestBotSubsystem::FindWallCover(const AOperativeCharacter* Operative, const FVector& Threat, float RadiusCm, FCoverSlot& OutSlot) const
{
	UWorld* World = GetWorld();
	if (!Operative || !World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotWallCover), false, Operative);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	const FVector Origin = Feet + FVector(0.f, 0.f, 45.f); // knee height: finds 60 cm covers too
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Step * 22.5f, FVector::UpVector);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * RadiusCm, ECC_Visibility, Params) || FMath::Abs(Hit.ImpactNormal.Z) > 0.3f)
		{
			continue;
		}
		const FVector Normal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		// The face he would put his back to must look away from the threat (the wall between them).
		if (Normal.IsNearlyZero() || FVector::DotProduct(Normal, (Threat - Hit.ImpactPoint).GetSafeNormal2D()) > -0.3f)
		{
			continue;
		}
		FCoverSlot Slot;
		if (!CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, -Normal, Slot) || !Slot.IsValid())
		{
			continue;
		}
		const float Score = (Slot.Height == ECoverHeight::HighCover ? 3.f : 0.f) - FVector::Dist2D(Slot.WorldLocation, Feet) / 100.f;
		if (Score > BestScore)
		{
			BestScore = Score;
			OutSlot = Slot;
		}
	}
	return BestScore > -TNumericLimits<float>::Max();
}

int32 UPlaytestBotSubsystem::SpawnRuntimePatrols()
{
	UWorld* World = GetWorld();
	UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
	ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
	const AOperativeCharacter* Leader = Member(0);
	if (!Waves || !Encounter || !Leader || Encounter->LevelHasPatrols())
	{
		return 0;
	}
	// Fixed offsets from the commander's start (forward F, right R): route A a 10 x 10 m loop 20-30 m ahead (marksman +
	// hound escort), route B a ping-pong line 15-35 m ahead, 15 m to the right (hound + hound + frostbitten escorts).
	const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
	const FVector R = FVector::CrossProduct(FVector::UpVector, F);
	const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
	auto MakeRoute = [this, World](const TArray<FVector>& Points, bool bLoop) -> APatrolRouteActor*
	{
		TArray<FVector> OnNav;
		for (const FVector& Point : Points)
		{
			FVector Projected;
			if (ProjectToNav(Point, Projected))
			{
				OnNav.Add(Projected);
			}
		}
		if (OnNav.Num() < 2)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(OnNav[0], FRotator::ZeroRotator, Params);
		USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
		if (!Spline)
		{
			return nullptr;
		}
		Spline->ClearSplinePoints(false);
		for (const FVector& Point : OnNav)
		{
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		Spline->UpdateSpline();
		Route->bIsLoop = bLoop;
		Route->bPingPong = !bLoop;
		Route->DefaultWaitTimeSeconds = 3.f;
		return Route;
	};
	int32 Spawned = 0;
	auto Spawn = [&Spawned, Waves](EEnemyArchetype Type, const FVector& At, const FVector& Facing) -> AEnemyCharacter*
	{
		AEnemyCharacter* Enemy = Waves->SpawnEnemy(Type, At + FVector(0.f, 0.f, 100.f), Facing.Rotation());
		Spawned += Enemy ? 1 : 0;
		return Enemy;
	};
	const FVector A0 = Feet + F * 2000.f - R * 500.f;
	if (APatrolRouteActor* RouteA = MakeRoute({ A0, Feet + F * 2000.f + R * 500.f, Feet + F * 3000.f + R * 500.f, Feet + F * 3000.f - R * 500.f }, true))
	{
		AEnemyCharacter* Marksman = Spawn(EEnemyArchetype::Marksman, RouteA->GetWaypointWorldLocation(0), R);
		AEnemyCharacter* Hound = Spawn(EEnemyArchetype::FrostHound, RouteA->GetWaypointWorldLocation(0) - R * 300.f, R);
		if (Marksman)
		{
			Marksman->StartPatrol(RouteA, nullptr);
			if (Hound)
			{
				Hound->StartPatrol(nullptr, Marksman);
			}
		}
	}
	if (APatrolRouteActor* RouteB = MakeRoute({ Feet + F * 1500.f + R * 1500.f, Feet + F * 3500.f + R * 1500.f }, false))
	{
		AEnemyCharacter* Lead = Spawn(EEnemyArchetype::FrostHound, RouteB->GetWaypointWorldLocation(0), F);
		AEnemyCharacter* Second = Spawn(EEnemyArchetype::FrostHound, RouteB->GetWaypointWorldLocation(0) - F * 250.f, F);
		AEnemyCharacter* Walker = Spawn(EEnemyArchetype::Frostbitten, RouteB->GetWaypointWorldLocation(0) - F * 450.f, F);
		if (Lead)
		{
			Lead->StartPatrol(RouteB, nullptr);
			for (AEnemyCharacter* Escort : { Second, Walker })
			{
				if (Escort)
				{
					Escort->StartPatrol(nullptr, Lead);
				}
			}
		}
	}
	Encounter->SetCombatStartOverride(ECombatStartMode::Ambush);
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Runtime patrols: %d enemies on 2 routes ahead of the squad (the map is not saved)"), Spawned);
	return Spawned;
}

void UPlaytestBotSubsystem::Finish(const TCHAR* Result, int32 ExitCode)
{
	if (bStealthLevel && StealthOutcome.IsEmpty())
	{
		StealthOutcome = TEXT("none");
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] outcome none at %.1f s (sneaking %.1f s, first detection %s, searches seen %d, decisions %d, seed %d)"),
			GetWorld()->GetTimeSeconds(), GetWorld()->GetTimeSeconds() - StealthStartTime,
			FirstDetectionTime >= 0.0 ? *FString::Printf(TEXT("%.1f s"), FirstDetectionTime) : TEXT("none"), SearchesSeen, StealthDecisions, Seed);
	}
	bActive = false;
	{
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const int32 Wave = Flow ? Flow->GetWaveIndex() : 0;
		const bool bVictory = FCString::Strcmp(Result, TEXT("VICTORY")) == 0;
		const URunTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<URunTelemetrySubsystem>();
		if (Spatial.IsRecording() && (!Telemetry || Telemetry->IsEnabled()))
		{
			Spatial.Finish(Result, bVictory ? Wave : FMath::Max(0, Wave - 1), Flow ? Flow->GetConfig().TotalWaves : 0);
		}
	}
	Stage = EBotStage::Done;
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] RESULT %s (profile %s, %.1f s real, grenades %d, deploys %d, items %d)"), Result,
		*PlaytestBotRules::ProfileName(Profile), FPlatformTime::Seconds() - StartRealTime, GrenadesThrown, DeploysOrdered, ItemsUsed);
	if (bQuit)
	{
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
	}
}

namespace PlaytestBotCommand
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (UPlaytestBotSubsystem* Bot = World ? World->GetSubsystem<UPlaytestBotSubsystem>() : nullptr)
		{
			int32 RunSeed = 1;
			for (const FString& Arg : Args)
			{
				if (Arg.StartsWith(TEXT("seed=")))
				{
					RunSeed = FCString::Atoi(*Arg.Mid(5));
				}
			}
			Bot->StartBot(PlaytestBotRules::ParseProfile(Args.IsEmpty() ? FString(TEXT("NORMAL")) : Args[0]), false,
				!Args.Contains(TEXT("nocollect")), RunSeed);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.Bot"),
		TEXT("Starts the playtest bot in this game: CodexTactics.Bot [CASUAL|NORMAL|VETERAN] [nocollect] [seed=<n>]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
