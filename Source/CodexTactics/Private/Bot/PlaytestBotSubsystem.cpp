#include "Bot/PlaytestBotSubsystem.h"

#include "Characters/EnemyCharacter.h"
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
	StartBot(PlaytestBotRules::ParseProfile(ProfileName), true, Loadout.ToUpper().StartsWith(TEXT("COLLECT")) || Loadout.ToUpper().StartsWith(TEXT("EXPLORE")));
}

TStatId UPlaytestBotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPlaytestBotSubsystem, STATGROUP_Tickables);
}

void UPlaytestBotSubsystem::StartBot(EBotProfile InProfile, bool bQuitAtEnd, bool bCollectLoot)
{
	Profile = InProfile;
	Config = PlaytestBotRules::GetProfileConfig(Profile);
	bQuit = bQuitAtEnd;
	bCollect = bCollectLoot;
	bActive = true;
	Stage = bCollect ? EBotStage::Explore : EBotStage::EnterCombat;
	StartRealTime = FPlatformTime::Seconds();
	if (URunTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<URunTelemetrySubsystem>())
	{
		Telemetry->SetTesterProfile(PlaytestBotRules::ProfileName(Profile));
	}
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
	if (UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnEnemySpawnedNative.AddUObject(this, &UPlaytestBotSubsystem::HandleEnemySpawned);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Started: profile %s, collect %d (%d targets), timeout %.0f s"),
		*PlaytestBotRules::ProfileName(Profile), bCollect ? 1 : 0, ExploreTargets.Num(), TimeoutSeconds);
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
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		if (Dialogue->IsDialogueOpen())
		{
			Dialogue->SkipDialogue();
		}
	}
	switch (Stage)
	{
	case EBotStage::Explore:
		TickExplore(DeltaTime);
		return;
	case EBotStage::EnterCombat:
		// bot_driver: main._on_start_combat_pressed = «Начать бой»: the squad behind the gate, healed, warm, then the
		// combat cutscene / preparation.
		if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Mission->StartMission(EMissionStartMode::Combat);
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
	if (!Leader || !Interactions)
	{
		Stage = EBotStage::EnterCombat;
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
			Stage = EBotStage::EnterCombat;
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
		if (!bDeployed && Flow->GetWaveIndex() <= 1)
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

void UPlaytestBotSubsystem::CombatAssist()
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
	if (bNeedsWarmth && Leader && WarmMoveCooldown <= 0.f)
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
	// 0. A marksman's telegraphed aim at a squad member comes first (Sprint 05-D).
	if (ReactToMarksman())
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
		if (Leader && !Leader->IsBehindBarricade() && FindCover(Leader, It->GetActorLocation(), Stand, CoverId, Score))
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
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Marksman %s aims at %s (%.0f%%): crouch%s"), *It->GetName(),
				*Target->DisplayName.ToString(), It->GetAimProgress() * 100.f, CoverId.IsEmpty() ? TEXT("") : TEXT(", leader to cover"));
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
		if (!Each || Each->IsMoving())
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

void UPlaytestBotSubsystem::Finish(const TCHAR* Result, int32 ExitCode)
{
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
			Bot->StartBot(PlaytestBotRules::ParseProfile(Args.IsEmpty() ? FString(TEXT("NORMAL")) : Args[0]), false,
				!Args.Contains(TEXT("nocollect")));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.Bot"),
		TEXT("Starts the playtest bot in this game: CodexTactics.Bot [CASUAL|NORMAL|VETERAN] [nocollect]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
