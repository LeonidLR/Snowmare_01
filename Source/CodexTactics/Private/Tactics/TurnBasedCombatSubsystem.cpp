#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Characters/EnemyAnimInstance.h"
#include "Tactics/TacticalEncounterRules.h"
#include "Camera/TacticalCameraPawn.h"
#include "GameFramework/PlayerController.h"
#include "UI/FloatingTextSubsystem.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DeployableActor.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeMovementRules.h"
#include "Interactables/RelocationGhostActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TurretActor.h"
#include "Misc/App.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/EnemyTurnRules.h"
#include "Data/WeaponTuning.h"
#include "Characters/EnemyTacticsRules.h"
#include "Tactics/TurnClickRules.h"
#include "Tactics/TurnGridOverlayActor.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	constexpr int32 TurnGridCells = 14;
	constexpr float TurnCellSize = 150.f; // Godot tactical_cell_size 1.5 m
	/** Godot main.gd _enter_turn_based_combat: select_participants(centre, 15.0, tree, 6). */
	constexpr float TurnEncounterRadius = 1500.f;
	constexpr int32 TurnEncounterMaxEnemies = 6;
	constexpr float TurnMineDamage = 50.f; // Godot _detonate_mine base_mine_dmg
	constexpr float TurnTurretSupportDistance = 4500.f; // Godot max_support_dist 45 m
	constexpr int32 TurnBarrelBurnRounds = 3;

	const TCHAR* TurnStanceName(EOperativeStance Stance)
	{
		return Stance == EOperativeStance::Prone ? TEXT("Prone") : (Stance == EOperativeStance::Crouching ? TEXT("Crouched") : TEXT("Standing"));
	}

	const TCHAR* TurnArcName(EGorkyArcZone Arc)
	{
		// Godot prints str(ArcZone) (an int); the UE feed names the arc.
		return Arc == EGorkyArcZone::Rear ? TEXT("rear") : (Arc == EGorkyArcZone::Flank ? TEXT("flank") : TEXT("front"));
	}

	FIntPoint TurnStepDir(const FIntPoint& Delta)
	{
		return FIntPoint(FMath::Clamp(Delta.X, -1, 1), FMath::Clamp(Delta.Y, -1, 1));
	}

	FEnemyTurnProfile TurnProfileOf(const AActor* Enemy)
	{
		const AEnemyCharacter* Character = Cast<AEnemyCharacter>(Enemy);
		const EEnemyArchetype Archetype = Character ? Character->GetArchetype() : EEnemyArchetype::Base;
		FEnemyTurnProfile Profile = EnemyTurnRules::ProfileFor(Archetype);
		WeaponTuning::ApplyEnemyTurnWeapon(Archetype, Profile); // Wave Editor "Enemy weapons"
		return Profile;
	}

	int32 TurnManhattan(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}
}

// --- Lifecycle ----------------------------------------------------------------------------------------------------

namespace
{
	// Godot main.gd camera choreography distances (camera.gd metres; UE camera distance is the same view distance in cm).
	constexpr float TurnEntryDistance = 1400.f;
	constexpr float SquadTurnDistance = 1600.f;
	constexpr float EnemyFocusDistance = 1150.f;
	constexpr float OverviewDistance = 1700.f;
}

void UTurnBasedCombatSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UTurnBasedCombatSubsystem::HandleGameFlowChanged);
	}
}

TStatId UTurnBasedCombatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTurnBasedCombatSubsystem, STATGROUP_Tickables);
}

void UTurnBasedCombatSubsystem::HandleGameFlowChanged(ECodexGamePhase NewPhase, ECodexCombatMode CombatMode)
{
	if (CombatMode == ECodexCombatMode::TurnBased && !IsActive())
	{
		StartCombat();
	}
	else if (CombatMode != ECodexCombatMode::TurnBased && IsActive())
	{
		EndCombat(false, false);
	}
}

void UTurnBasedCombatSubsystem::StartCombat()
{
	UWorld* World = GetWorld();
	USquadSubsystem* SquadSystem = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = SquadSystem ? SquadSystem->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	++CombatId;
	// Imported Godot balance (DA_Balance = resources/balance.tres).
	if (const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>())
	{
		if (const UGodotBalanceAsset* BalanceAsset = GameMode->TurnBasedBalance.LoadSynchronous())
		{
			Balance = TurnBasedRules::BalanceFromGodot(BalanceAsset);
			WeaponTuning::ApplyTurnRules(Balance); // Wave Editor "Turn-based rules"
			SquadStepDuration = BalanceAsset->GetNumber(TEXT("tactical_step_duration"), SquadStepDuration);
			EnemyStepDuration = BalanceAsset->GetNumber(TEXT("tactical_enemy_step_duration"), EnemyStepDuration);
			EnemyHitDelay = BalanceAsset->GetNumber(TEXT("tactical_enemy_hit_delay"), EnemyHitDelay);
			EnemyAttackDuration = BalanceAsset->GetNumber(TEXT("tactical_enemy_attack_duration"), EnemyAttackDuration);
			EnemyRetreatDelay = BalanceAsset->GetNumber(TEXT("tactical_enemy_retreat_delay"), EnemyRetreatDelay);
		}
	}
	States.Reset();
	Squad.Reset();
	Enemies.Reset();
	Turrets.Reset();
	BurningBarrels.Reset();
	Movers.Reset();

	const FVector Center = Leader->GetActorLocation();
	const float HalfExtent = TurnGridCells * TurnCellSize * 0.5f;
	auto OnGrid = [&Center, HalfExtent](const AActor* Actor)
	{
		const FVector Delta = Actor->GetActorLocation() - Center;
		return FMath::Abs(Delta.X) < HalfExtent && FMath::Abs(Delta.Y) < HalfExtent;
	};

	// Encounter: the whole squad, living enemies / barrels / mines / turrets / barricades inside the 14 x 14 area.
	TArray<AActor*> Ignore;
	for (AOperativeCharacter* Member : SquadSystem->GetMembers())
	{
		if (!IsDead(Member))
		{
			Squad.Add(Member);
			Ignore.Add(Member);
		}
	}
	TArray<AEnemyCharacter*> LivingEnemies;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (!It->IsDying() && !IsDead(*It))
		{
			LivingEnemies.Add(*It);
		}
	}
	TArray<AActor*> GridBarrels, GridMines, GridTurrets, GridBarricades;
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (OnGrid(*It))
		{
			GridBarrels.Add(*It);
		}
	}
	for (TActorIterator<AProximityMineActor> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (It->bTrapped && OnGrid(*It))
		{
			GridMines.Add(*It);
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (OnGrid(*It))
		{
			GridTurrets.Add(*It);
		}
	}
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (OnGrid(*It))
		{
			GridBarricades.Add(*It);
		}
	}

	// Floor under the leader (Godot ray down from the leader).
	float GroundZ = Center.Z - Leader->GetSimpleCollisionHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnGridFloor), false);
	Params.AddIgnoredActors(Ignore);
	FHitResult FloorHit;
	if (World->LineTraceSingleByChannel(FloorHit, Center + FVector(0.f, 0.f, 50.f), Center - FVector(0.f, 0.f, 2000.f), ECC_Visibility, Params))
	{
		GroundZ = FloorHit.ImpactPoint.Z;
	}
	// Godot TacticalEncounterSelector.select_participants(centre, 15 m, 6): at most six enemies fight on the grid,
	// every species within reach keeps a slot; the others wait in stasis.
	TArray<AEnemyCharacter*> GridEnemies;
	{
		const FVector CentreFloor(Center.X, Center.Y, GroundZ);
		TArray<TacticalEncounterRules::FCandidate> Candidates;
		for (const AEnemyCharacter* Enemy : LivingEnemies)
		{
			const FVector Feet = Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight());
			Candidates.Add({ FName(*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Enemy->GetArchetype()))),
				static_cast<float>(FVector::Dist(Feet, CentreFloor)) });
		}
		for (const int32 Index : TacticalEncounterRules::SelectEnemies(Candidates, TurnEncounterRadius, TurnEncounterMaxEnemies))
		{
			GridEnemies.Add(LivingEnemies[Index]);
		}
	}
	Grid = NewObject<UGorkyGridManager>(this);
	Grid->Setup(FVector(Center.X, Center.Y, GroundZ), FIntPoint(TurnGridCells, TurnGridCells), TurnCellSize, GroundZ);
	Grid->DiagonalAPCost = Balance.DiagonalAPCost;
	BakeObstacles(Ignore);

	for (AActor* Barricade : GridBarricades)
	{
		RegisterBarricadeCells(Barricade);
	}
	for (AActor* Barrel : GridBarrels)
	{
		const FIntPoint Cell = Grid->WorldToGrid(Barrel->GetActorLocation());
		Grid->SetOccupant(Cell, Barrel, EGorkyOccupantType::Barrel);
		PlaceOnCell(Barrel, Cell);
		FTurnUnitState& State = States.Add(Barrel);
		State.Actor = Barrel;
		State.GridPos = Cell;
		if (CastChecked<ABarrelActor>(Barrel)->IsBurning())
		{
			BurningBarrels.Add(Barrel, TurnBarrelBurnRounds);
		}
	}
	for (AActor* Mine : GridMines)
	{
		Grid->SetOccupant(Grid->WorldToGrid(Mine->GetActorLocation()), Mine, EGorkyOccupantType::Mine);
	}
	for (AActor* Turret : GridTurrets)
	{
		const FIntPoint Cell = Grid->WorldToGrid(Turret->GetActorLocation());
		Grid->SetOccupant(Cell, Turret, EGorkyOccupantType::Turret);
		Turrets.Add(Turret);
		FTurnUnitState& State = States.Add(Turret);
		State.Actor = Turret;
		State.GridPos = Cell;
	}

	FVector EnemyCentroid = FVector::ZeroVector;
	for (AEnemyCharacter* Enemy : GridEnemies)
	{
		EnemyCentroid += Enemy->GetActorLocation() / GridEnemies.Num();
	}
	auto FacingTowards = [](const FVector& From, const FVector& To, EGorkyFacing Fallback)
	{
		const FVector Dir = (To - From).GetSafeNormal2D();
		return Dir.IsNearlyZero() ? Fallback : FGorky17Utils::VectorToFacing(FIntPoint(FMath::RoundToInt(Dir.X), FMath::RoundToInt(Dir.Y)));
	};

	for (const TWeakObjectPtr<AOperativeCharacter>& Weak : Squad)
	{
		AOperativeCharacter* Member = Weak.Get();
		const FIntPoint Cell = Grid->FindNearestFreeCell(Grid->WorldToGrid(Member->GetActorLocation()));
		Grid->SetOccupant(Cell, Member, EGorkyOccupantType::Squad);
		Member->StopOperative();
		PlaceOnCell(Member, Cell);
		FTurnUnitState& State = States.Add(Member);
		State.Actor = Member;
		State.GridPos = Cell;
		State.bSquad = true;
		State.MaxAP = State.AP = Balance.SquadMaxAP;
		State.Armor = 5.f;
		State.BaseDamage = Balance.SquadBaseDamage;
		State.Stance = Member->GetStance();
		// Godot: the squad faces the enemies (SOUTH on its map).
		State.Facing = GridEnemies.IsEmpty() ? EGorkyFacing::South : FacingTowards(Member->GetActorLocation(), EnemyCentroid, EGorkyFacing::South);
		AlignFacing(Member, State.Facing);
	}
	TMap<EEnemyArchetype, int32> TypeCounts;
	for (AEnemyCharacter* Enemy : GridEnemies)
	{
		// Godot: same-type enemies get different idle clips (type index cycles through the idles).
		if (UEnemyAnimInstance* Anim = Enemy->GetMesh() ? Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr)
		{
			Anim->SetIdleVariation(TypeCounts.FindOrAdd(Enemy->GetArchetype())++);
		}
		const FIntPoint Cell = Grid->FindNearestFreeCell(Grid->WorldToGrid(Enemy->GetActorLocation()));
		Grid->SetOccupant(Cell, Enemy, EGorkyOccupantType::Enemy);
		PlaceOnCell(Enemy, Cell);
		Enemies.Add(Enemy);
		FTurnUnitState& State = States.Add(Enemy);
		State.Actor = Enemy;
		State.GridPos = Cell;
		const FEnemyTurnProfile TurnProfile = TurnProfileOf(Enemy);
		State.MaxAP = State.AP = EnemyTurnRules::MaxAP(TurnProfile, Balance.EnemyMaxAP);
		State.Armor = 3.f;
		State.BaseDamage = EnemyTurnRules::BaseDamage(TurnProfile, Balance.EnemyBaseDamage);
		State.Facing = FacingTowards(Enemy->GetActorLocation(), Center, EGorkyFacing::North);
		AlignFacing(Enemy, State.Facing);
	}

	// Godot start_combat: zones_manager.setup + evaluate_wave_roster (the grid's enemies).
	Zones.Reset();
	{
		TArray<FExposedZones::FRosterEntry> Roster;
		for (AEnemyCharacter* Enemy : GridEnemies)
		{
			Roster.Add({ Enemy->GetArchetype(), Enemy->GetHealthComponent()->GetMaxHealth(), Enemy->GetAttackDamage(),
				static_cast<float>(Balance.EnemyMaxAP) });
		}
		Zones.EvaluateRoster(Roster);
	}

	TSet<AActor*> OnGridSet;
	for (AEnemyCharacter* Enemy : GridEnemies)
	{
		OnGridSet.Add(Enemy);
	}
	FreezeWorld(OnGridSet);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Overlay = World->SpawnActor<ATurnGridOverlayActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Overlay)
	{
		Overlay->SetGrid(Grid);
	}

	// Godot _enter_turn_based_combat: the camera zooms in to 14 m on the leader; shots play as camera sequences
	// unless the run is headless (Godot can_tween).
	bCinematics = FApp::CanEverRender() || bForceCinematicsForTesting;
	bDramaticShotActive = false;
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->EnterTurnBasedZoom(TurnEntryDistance);
	}

	Phase = ETurnPhase::Squad;
	ActiveIndex = 0;
	Round = 1;
	ContactHitsThisFight = 0;
	GuardCorrections = 0;
	UE_LOG(LogCodexTactics, Display, TEXT("Turn-based combat: %d operatives, %d enemies, %d barrels, %d mines, %d turrets"),
		Squad.Num(), Enemies.Num(), GridBarrels.Num(), GridMines.Num(), GridTurrets.Num());
	StartPlayerTurn();
}

void UTurnBasedCombatSubsystem::EndCombat(bool bVictory, bool bLeaveFlow)
{
	if (!IsActive())
	{
		return;
	}
	Phase = ETurnPhase::Inactive;
	++CombatId;
	Movers.Reset();
	RelocateTarget.Reset();
	RelocateCells.Reset();

	bSquadUnitMoving = false;
	bDramaticShotActive = false;
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->ExitTurnBasedZoom(); // Godot _on_gorky17_combat_ended: the pre-combat zoom comes back
	}
	RestoreWorld();
	if (Overlay)
	{
		Overlay->Destroy();
		Overlay = nullptr;
	}
	States.Reset();
	Squad.Reset();
	Enemies.Reset();
	Turrets.Reset();
	EnemyQueue.Reset();
	TurretQueue.Reset();
	BurningBarrels.Reset();
	Grid = nullptr;
	if (bVictory)
	{
		Log(TEXT("🏆 TURN-BASED VICTORY! Enemies down. Tactical pause (20s) to regroup [SPACE]."));
	}
	if (bLeaveFlow)
	{
		if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>(); Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
		{
			Flow->ExitTurnBased();
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Turn-based combat ended (%s)"), bVictory ? TEXT("victory") : TEXT("exit"));
	Changed();
}

void UTurnBasedCombatSubsystem::FreezeWorld(const TSet<AActor*>& OnGrid)
{
	FrozenActors.Reset();
	UWorld* World = GetWorld();
	auto Freeze = [this](AActor* Actor)
	{
		if (Actor->IsActorTickEnabled())
		{
			Actor->SetActorTickEnabled(false);
			FrozenActors.Add(Actor);
		}
	};
	if (!StasisMaterial)
	{
		StasisMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_TacticalStasis.M_TacticalStasis"));
	}
	StasisMeshes.Reset();
	HeldAnchors.Reset();
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (AController* Controller = It->GetController())
		{
			Controller->StopMovement();
		}
		Freeze(*It);
		// Bug fix 2026-10-06: the actor tick alone left the movement / path following running — a hit could still send an
		// enemy (the marksman's kiting sprint) off the grid. Held: no AI moves, velocity zeroed; the anchor guard keeps it.
		It->SetTurnBasedHeld(true);
		HeldAnchors.Add(*It, It->GetActorLocation());
		// Godot: enemies outside the fight turn dark and translucent until it ends.
		if (!OnGrid.Contains(*It) && StasisMaterial)
		{
			TArray<UMeshComponent*> Meshes;
			It->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* Mesh : Meshes)
			{
				FTurnStasisMesh& Entry = StasisMeshes.AddDefaulted_GetRef();
				Entry.Mesh = Mesh;
				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
				{
					Entry.Materials.Add(Mesh->GetMaterial(Slot));
					Mesh->SetMaterial(Slot, StasisMaterial);
				}
			}
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Freeze(*It);
	}
	for (TActorIterator<AProximityMineActor> It(World); It; ++It)
	{
		Freeze(*It);
	}
}

void UTurnBasedCombatSubsystem::RestoreWorld()
{
	for (const TWeakObjectPtr<AActor>& Weak : FrozenActors)
	{
		if (AActor* Actor = Weak.Get())
		{
			Actor->SetActorTickEnabled(true);
		}
	}
	FrozenActors.Reset();
	for (const TPair<TWeakObjectPtr<AEnemyCharacter>, FVector>& Entry : HeldAnchors)
	{
		if (AEnemyCharacter* Enemy = Entry.Key.Get())
		{
			Enemy->SetTurnBasedHeld(false);
		}
	}
	HeldAnchors.Reset();
	for (const FTurnStasisMesh& Entry : StasisMeshes)
	{
		if (UMeshComponent* Mesh = Entry.Mesh.Get())
		{
			for (int32 Slot = 0; Slot < Entry.Materials.Num(); ++Slot)
			{
				Mesh->SetMaterial(Slot, Entry.Materials[Slot]);
			}
		}
	}
	StasisMeshes.Reset();
}

void UTurnBasedCombatSubsystem::BakeObstacles(const TArray<AActor*>& Ignore)
{
	// Godot _bake_environment_obstacles: static colliders (walls, vehicles) above the floor block their cells.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnGridObstacles), false);
	Params.AddIgnoredActors(Ignore);
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(TurnCellSize * 0.35f, TurnCellSize * 0.35f, 60.f));
	for (int32 X = 0; X < TurnGridCells; ++X)
	{
		for (int32 Y = 0; Y < TurnGridCells; ++Y)
		{
			const FVector Probe = Grid->GridToWorld(FIntPoint(X, Y)) + FVector(0.f, 0.f, 90.f);
			if (GetWorld()->OverlapAnyTestByChannel(Probe, FQuat::Identity, ECC_WorldStatic, Box, Params))
			{
				Grid->SetOccupant(FIntPoint(X, Y), nullptr, EGorkyOccupantType::Obstacle);
			}
		}
	}
}

// --- Helpers ------------------------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::PlaceOnCell(AActor* Actor, const FIntPoint& Cell) const
{
	const FVector World = Grid->GridToWorld(Cell);
	Actor->SetActorLocation(FVector(World.X, World.Y, Actor->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
}

void UTurnBasedCombatSubsystem::AlignFacing(AActor* Actor, EGorkyFacing Facing) const
{
	const FIntPoint Dir = FGorky17Utils::FacingToVector(Facing);
	Actor->SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X))), 0.f));
}

float UTurnBasedCombatSubsystem::GetHealth(const AActor* Actor) const
{
	const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
	return Health ? Health->GetCurrentHealth() : 0.f;
}

void UTurnBasedCombatSubsystem::ApplyDamage(AActor* Victim, float Amount, const FString& Source)
{
	if (UHealthComponent* Health = Victim ? Victim->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		Health->ApplyDirectHealthLoss(Amount, Source);
	}
}

void UTurnBasedCombatSubsystem::ApplyEnemyHit(AActor* Enemy, float Amount, const FString& Source)
{
	if (UHealthComponent* Health = Enemy ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		FDamageSpec Spec;
		Spec.Amount = Amount;
		Spec.DamageType = EDamageType::Kinetic;
		Spec.ArmorPenetration = 0.f;
		Spec.AttackerSource = Source;
		Health->TakeDamage(Spec);
	}
}

int32 UTurnBasedCombatSubsystem::ApplySquadHit(AActor* Victim, float Amount, const FString& Source, EKnockdownBlow Blow)
{
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Victim))
	{
		// Sprint 14: the grid path bypasses TakeHit's modifiers, so the downed multiplier is applied here, from the same
		// KnockdownRules helper TakeHit uses (ranged x0.6, melee x1.5, explosion x1).
		const float Scale = Operative->KnockdownComponent ? Operative->KnockdownComponent->GetBlowMultiplier(Blow) : 1.f;
		const int32 Dealt = Scale == 1.f ? FMath::RoundToInt(Amount) : FMath::Max(1, FMath::RoundToInt(Amount * Scale));
		Operative->TakeHit(Dealt, Source, false, true);
		return Dealt;
	}
	ApplyDamage(Victim, Amount, Source);
	return FMath::RoundToInt(Amount);
}

void UTurnBasedCombatSubsystem::ApplyBlast(AActor* Victim, bool bSquad, float Amount, const FString& Source)
{
	// Godot _detonate_barrel / _detonate_mine: the unit's own take_damage(amount) (enemy armor; operative dodge,
	// stance, fortitude), while the grid hp loses the raw amount and decides the kill. An enemy whose health could not
	// take the raw blast dies (Godot _on_enemy_killed -> die()). Operatives die by their real health only: Godot would
	// drop a still-living operative from the fight (grid hp) — treated as a bug.
	if (bSquad)
	{
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Victim))
		{
			Operative->TakeHit(Amount, Source);
			return;
		}
		ApplyDamage(Victim, Amount, Source);
		return;
	}
	UHealthComponent* Health = Victim ? Victim->FindComponentByClass<UHealthComponent>() : nullptr;
	const float HealthBefore = Health ? Health->GetCurrentHealth() : 0.f;
	ApplyEnemyHit(Victim, Amount, Source);
	if (Health && Health->IsAlive() && HealthBefore - Amount <= 0.f)
	{
		Health->ApplyDirectHealthLoss(Health->GetCurrentHealth(), Source);
	}
}

bool UTurnBasedCombatSubsystem::IsDead(const AActor* Actor) const
{
	const UHealthComponent* Health = IsValid(Actor) ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
	return !Health || !Health->IsAlive();
}

FString UTurnBasedCombatSubsystem::NameOf(const AActor* Actor) const
{
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor))
	{
		return Operative->DisplayName.ToString();
	}
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
	{
		return Enemy->GetEnemyDisplayName();
	}
	return Actor ? Actor->GetName() : TEXT("?");
}

const UWeaponDataAsset* UTurnBasedCombatSubsystem::WeaponOf(const AActor* Actor) const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	return Operative ? Operative->CurrentWeapon.Get() : nullptr;
}

void UTurnBasedCombatSubsystem::ShakeCamera(const FString& WeaponType) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr)
	{
		Camera->TriggerWeaponShake(WeaponType);
	}
}

void UTurnBasedCombatSubsystem::Log(const FString& Message) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(TEXT("GORKY 17")), FText::FromString(Message));
	}
}

void UTurnBasedCombatSubsystem::Post(const FString& Sender, const FString& Message) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(Sender), FText::FromString(Message));
	}
}

void UTurnBasedCombatSubsystem::Highlight(AActor* Target) const
{
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->HighlightTarget(Target);
	}
}

ATacticalCameraPawn* UTurnBasedCombatSubsystem::GetCamera() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
}

FVector UTurnBasedCombatSubsystem::GetSquadOverviewCenter() const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
	{
		if (Member.IsValid() && States.Contains(Member.Get()))
		{
			Sum += Member->GetActorLocation();
			++Count;
		}
	}
	if (Count > 0)
	{
		return Sum / Count;
	}
	const AOperativeCharacter* Unit = GetActiveUnit();
	return Unit ? Unit->GetActorLocation() : FVector::ZeroVector;
}

void UTurnBasedCombatSubsystem::FocusSquadTurn(AActor* Unit) const
{
	if (const UDeathCinematicSubsystem* DeathCam = GetWorld()->GetSubsystem<UDeathCinematicSubsystem>(); DeathCam && DeathCam->IsFocusActive())
	{
		return; // the death cinematic owns the camera and returns it to the active operative itself
	}
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->SmoothFocusOnTarget(Unit, 0.75f, SquadTurnDistance);
	}
}

void UTurnBasedCombatSubsystem::Changed()
{
	OnStateChanged.Broadcast();
}

const FTurnUnitState* UTurnBasedCombatSubsystem::GetUnitState(const AActor* Actor) const
{
	return States.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
}

AOperativeCharacter* UTurnBasedCombatSubsystem::GetActiveUnit() const
{
	return Squad.IsValidIndex(ActiveIndex) ? Squad[ActiveIndex].Get() : nullptr;
}

void UTurnBasedCombatSubsystem::After(float Seconds, TFunction<void()> Callback)
{
	const int32 Id = CombatId;
	TWeakObjectPtr<UTurnBasedCombatSubsystem> WeakThis(this);
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis, Id, Callback]()
	{
		if (WeakThis.IsValid() && WeakThis->CombatId == Id && WeakThis->IsActive())
		{
			Callback();
		}
	}), FMath::Max(Seconds, 0.01f), false);
}

// --- Movement animation -----------------------------------------------------------------------------------------

float UTurnBasedCombatSubsystem::PrepareSquadWalk(AOperativeCharacter* Unit, float& OutStepDuration)
{
	OutStepDuration = SquadStepDuration;
	if (!Unit)
	{
		return 0.f;
	}
	// User-found bug 2026-10-07: a unit pressed against a wall when the grid fight started slid across the grid in its
	// cover pose — the grid mover moves the actor directly and nothing left the cover, so the AnimInstance kept the
	// cover loop on its FullBody slot. Every grid walk (move, push, deploy) leaves the cover first; a pending cover (a
	// cover cell was clicked) is kept and entered on arrival (AOperativeCharacter::UpdateCover).
	if (Unit->bInCover)
	{
		Unit->LeaveCoverToMove(TEXT("grid walk"));
	}
	float Delay = 0.f;
	if (Unit->GetStance() == EOperativeStance::Prone)
	{
		Unit->SetStance(EOperativeStance::Crouching);
		if (Unit->GetStance() == EOperativeStance::Crouching) // a frostbitten operative cannot get up
		{
			if (FTurnUnitState* State = States.Find(Unit))
			{
				State->Stance = EOperativeStance::Crouching;
			}
			Log(FString::Printf(TEXT("🧍 %s rises to a crouch to move"), *NameOf(Unit)));
			const UOperativeAnimInstance* Anim = Unit->GetMesh() ? Cast<UOperativeAnimInstance>(Unit->GetMesh()->GetAnimInstance()) : nullptr;
			if (Anim && Anim->ProneToCrouchAnimation)
			{
				Delay = Anim->ProneToCrouchAnimation->GetPlayLength() / FMath::Max(Anim->StanceTransitionPlayRate, 0.1f)
					- Anim->StanceTransitionBlendTime;
			}
		}
	}
	if (Unit->GetStance() != EOperativeStance::Standing)
	{
		const float Ratio = OperativeMovementRules::GetStanceSpeedMultiplier(Unit->MovementConfig, Unit->GetStance());
		OutStepDuration = SquadStepDuration / FMath::Clamp(Ratio, 0.2f, 1.f);
	}
	return FMath::Max(0.f, Delay);
}

void UTurnBasedCombatSubsystem::StartMoverAfter(float Delay, AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path,
	float StepDuration, TFunction<void()> OnDone, bool bFaceSteps)
{
	if (Delay <= 0.f)
	{
		StartMover(Actor, From, Path, StepDuration, nullptr, MoveTemp(OnDone), bFaceSteps);
		return;
	}
	TWeakObjectPtr<AActor> WeakActor(Actor);
	After(Delay, [this, WeakActor, From, Path, StepDuration, OnDone = MoveTemp(OnDone), bFaceSteps]() mutable
	{
		if (AActor* Moving = WeakActor.Get(); Moving && IsActive())
		{
			StartMover(Moving, From, Path, StepDuration, nullptr, MoveTemp(OnDone), bFaceSteps);
		}
	});
}

void UTurnBasedCombatSubsystem::StartMover(AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path, float StepDuration,
	TFunction<bool(int32)> OnStep, TFunction<void()> OnDone, bool bFaceSteps)
{
	FMover& Mover = Movers.AddDefaulted_GetRef();
	Mover.Actor = Actor;
	Mover.bFaceSteps = bFaceSteps;
	Mover.StepDuration = StepDuration;
	Mover.Speed = Grid ? Grid->CellSize / FMath::Max(StepDuration, 0.01f) : 0.f;
	Mover.From = Actor->GetActorLocation();
	Mover.OnStep = MoveTemp(OnStep);
	Mover.OnDone = MoveTemp(OnDone);
	FIntPoint Previous = From;
	for (const FIntPoint& Cell : Path)
	{
		const FVector World = Grid->GridToWorld(Cell);
		Mover.Points.Add(FVector(World.X, World.Y, Mover.From.Z));
		Mover.Facings.Add(FGorky17Utils::VectorToFacing(TurnStepDir(Cell - Previous)));
		Previous = Cell;
	}
	if (Mover.Points.IsEmpty())
	{
		TFunction<void()> Done = MoveTemp(Mover.OnDone);
		Movers.Pop();
		if (Done)
		{
			Done();
		}
		return;
	}
	if (bFaceSteps)
	{
		const FIntPoint Dir = FGorky17Utils::FacingToVector(Mover.Facings[0]);
		Mover.TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
	}
	// Paths of 2+ cells (units and the objects they push, in step) share one continuous profile.
	if (Mover.Points.Num() >= 2)
	{
		Mover.bProfile = true;
		Mover.Origin = Mover.From;
		float Total = 0.f;
		FVector Previous3D = Mover.From;
		for (const FVector& Point : Mover.Points)
		{
			Total += FVector::Dist2D(Previous3D, Point);
			Mover.CumulativeLength.Add(Total);
			Previous3D = Point;
		}
		Mover.Time = 0.f;
	}
}

void UTurnBasedCombatSubsystem::SampleWalkProfile(float Total, float FirstLength, float LastLength, float TotalTime, float Time,
	float& OutDistance, float& OutSpeed)
{
	// Same total time as Godot: the cruise speed covers the distance plus half of the ramps, v = (D + d0 + dl) / T.
	const float Cruise = (Total + FirstLength + LastLength) / FMath::Max(TotalTime, KINDA_SMALL_NUMBER);
	const float AccelTime = 2.f * FirstLength / Cruise;
	const float DecelTime = 2.f * LastLength / Cruise;
	const float CruiseTime = FMath::Max(0.f, (Total - FirstLength - LastLength) / Cruise);
	if (Time <= 0.f)
	{
		OutDistance = 0.f;
		OutSpeed = 0.f;
	}
	else if (Time < AccelTime)
	{
		OutSpeed = Cruise * Time / AccelTime;
		OutDistance = 0.5f * OutSpeed * Time;
	}
	else if (Time < AccelTime + CruiseTime)
	{
		OutSpeed = Cruise;
		OutDistance = FirstLength + Cruise * (Time - AccelTime);
	}
	else
	{
		const float Tail = FMath::Min(Time - AccelTime - CruiseTime, DecelTime);
		OutSpeed = Cruise * (1.f - Tail / DecelTime);
		OutDistance = FirstLength + Cruise * CruiseTime + Cruise * Tail - 0.5f * (Cruise / DecelTime) * Tail * Tail;
	}
	OutDistance = FMath::Min(OutDistance, Total);
}

float UTurnBasedCombatSubsystem::GetTacticalMoveSpeed(const AActor* Actor) const
{
	for (const FMover& Mover : Movers)
	{
		if (Mover.bFaceSteps && Mover.Actor.Get() == Actor)
		{
			return Mover.CurrentSpeed;
		}
	}
	return -1.f;
}

void UTurnBasedCombatSubsystem::SetRelocationHover(const FVector& WorldPoint)
{
	AActor* Object = RelocateTarget.Get();
	if (!Object || !Grid)
	{
		return;
	}
	if (!RelocateGhost || RelocateGhostSource.Get() != Object)
	{
		if (RelocateGhost)
		{
			RelocateGhost->Destroy();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		RelocateGhost = GetWorld()->SpawnActor<ARelocationGhostActor>(Object->GetActorLocation(), Object->GetActorRotation(), Params);
		RelocateGhostSource = Object;
		if (RelocateGhost)
		{
			RelocateGhost->CopyFrom(Object->FindComponentByClass<UStaticMeshComponent>(), Object);
			// Godot _set_relocate_ghost_material_valid: green / red.
			RelocateGhost->SetColors(FLinearColor::FromSRGBColor(FColor(51, 242, 102)), FLinearColor::FromSRGBColor(FColor(255, 51, 51)));
		}
	}
	if (!RelocateGhost)
	{
		return;
	}
	const FIntPoint Cell = Grid->WorldToGrid(WorldPoint);
	const FVector CellWorld = Grid->GridToWorld(Cell);
	const float Yaw = IsRelocatingBarricade() ? RelocateYaw : Object->GetActorRotation().Yaw;
	RelocateGhost->SetActorLocationAndRotation(FVector(CellWorld.X, CellWorld.Y, Object->GetActorLocation().Z), FRotator(0.f, Yaw, 0.f));
	RelocateGhost->SetValid(RelocateCells.Contains(Cell));
}

bool UTurnBasedCombatSubsystem::IsActiveUnitKnockedDown() const
{
	const AOperativeCharacter* Unit = GetActiveUnit();
	return Unit && Unit->IsKnockedDown();
}

void UTurnBasedCombatSubsystem::UpdateKnockedDownTurn()
{
	if (Phase != ETurnPhase::Squad || IsBusy())
	{
		return;
	}
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	UKnockdownComponent* Knockdown = Unit ? Unit->KnockdownComponent.Get() : nullptr;
	if (!State || !Knockdown || Knockdown->GetPhase() != EKnockdownPhase::Downed)
	{
		return; // up, or still falling (decided once he lies)
	}
	const int32 Before = State->AP;
	const FKnockdownTurnDecision Decision = Knockdown->HandleTurn(State->AP);
	if (Decision.bGetUp)
	{
		Log(FString::Printf(TEXT("%s gets back on his feet (-%d AP, %d left)."), *NameOf(Unit), Before - State->AP, State->AP));
		RefreshOverlay();
		Changed();
	}
	else if (Decision.bSkipTurn)
	{
		Log(FString::Printf(TEXT("%s is knocked down with %d AP - not enough to get up, turn skipped."), *NameOf(Unit), State->AP));
		EndCurrentUnitTurn();
	}
}

void UTurnBasedCombatSubsystem::Tick(float DeltaTime)
{
	UpdateKnockedDownTurn();
	// The relocation hologram goes with the relocation (it ends in many places: confirm, cancel, turn end, combat end).
	if (RelocateGhost && (!RelocateTarget.IsValid() || RelocateGhostSource.Get() != RelocateTarget.Get()))
	{
		RelocateGhost->Destroy();
		RelocateGhost = nullptr;
		RelocateGhostSource.Reset();
	}
	for (int32 Index = Glides.Num() - 1; Index >= 0; --Index)
	{
		FObjectGlide& Glide = Glides[Index];
		AActor* Actor = Glide.Actor.Get();
		Glide.Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp((Glide.Elapsed - Glide.Delay) / Glide.Duration, 0.f, 1.f);
		// Quad ease-out, or back ease-out (Godot TRANS_BACK, overshoot 1.70158) for the grow-in.
		constexpr float Overshoot = 1.70158f;
		const float Eased = Glide.bBackEase
			? 1.f + (Overshoot + 1.f) * FMath::Pow(Alpha - 1.f, 3.f) + Overshoot * FMath::Square(Alpha - 1.f)
			: 1.f - FMath::Square(1.f - Alpha);
		if (Actor)
		{
			FTransform Blended;
			Blended.Blend(Glide.From, Glide.To, Eased);
			Actor->SetActorTransform(Blended, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (!Actor || Alpha >= 1.f)
		{
			Glides.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = Movers.Num() - 1; Index >= 0; --Index)
	{
		if (!Movers.IsValidIndex(Index))
		{
			continue;
		}
		FMover& Mover = Movers[Index];
		AActor* Actor = Mover.Actor.Get();
		bool bFinished = !Actor;
		if (Actor && Mover.bFaceSteps)
		{
			// Units turn to each step smoothly (~0.15 s) instead of Godot's snap.
			const FRotator Facing(0.f, Mover.TargetYaw, 0.f);
			Actor->SetActorRotation(FMath::RInterpTo(Actor->GetActorRotation(), Facing, DeltaTime, 15.f));
		}
		if (Actor && Mover.bProfile)
		{
			const int32 Steps = Mover.Points.Num();
			const float Total = Mover.CumulativeLength.Last();
			const float First = Mover.CumulativeLength[0];
			const float Last = Total - Mover.CumulativeLength[Steps - 2];
			const float TotalTime = Total / FMath::Max(Mover.Speed, 1.f);
			Mover.Time += DeltaTime;
			float Distance = 0.f;
			SampleWalkProfile(Total, First, Last, TotalTime, Mover.Time, Distance, Mover.CurrentSpeed);
			// Cells passed this frame: their callbacks in order (a mine stops the walk on its cell).
			bool bStopped = false;
			while (Mover.Index < Steps && Distance >= Mover.CumulativeLength[Mover.Index] - 0.5f)
			{
				const int32 Passed = Mover.Index;
				const bool bContinue = !Mover.OnStep || Mover.OnStep(Passed);
				if (!Movers.IsValidIndex(Index) || !Mover.Actor.IsValid())
				{
					bStopped = true;
					break; // combat ended from the step callback
				}
				++Mover.Index;
				if (!bContinue)
				{
					Actor->SetActorLocation(Mover.Points[Passed]);
					Mover.CurrentSpeed = 0.f;
					bFinished = true;
					bStopped = true;
					break;
				}
				if (Mover.Index < Steps)
				{
					const FIntPoint Dir = FGorky17Utils::FacingToVector(Mover.Facings[Mover.Index]);
					Mover.TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
				}
			}
			if (!Movers.IsValidIndex(Index))
			{
				continue;
			}
			if (!bStopped)
			{
				if (Mover.Index >= Steps)
				{
					Actor->SetActorLocation(Mover.Points.Last());
					Mover.CurrentSpeed = 0.f;
					bFinished = true;
				}
				else
				{
					const float SegmentStart = Mover.Index > 0 ? Mover.CumulativeLength[Mover.Index - 1] : 0.f;
					const FVector SegmentFrom = Mover.Index > 0 ? Mover.Points[Mover.Index - 1] : Mover.Origin;
					const float SegmentLength = FMath::Max(Mover.CumulativeLength[Mover.Index] - SegmentStart, 1.f);
					Actor->SetActorLocation(FMath::Lerp(SegmentFrom, Mover.Points[Mover.Index],
						FMath::Clamp((Distance - SegmentStart) / SegmentLength, 0.f, 1.f)));
				}
			}
		}
		else if (Actor)
		{
			const FVector Before = Actor->GetActorLocation();
			const FIntPoint Dir = FGorky17Utils::FacingToVector(Mover.Facings[Mover.Index]);
			const float Duration = Mover.StepDuration * (Dir.X != 0 && Dir.Y != 0 ? 1.414f : 1.f);
			Mover.Alpha = FMath::Min(1.f, Mover.Alpha + DeltaTime / Duration);
			// Godot tween per step: one step sine in-out; on a path the first step eases in, the last eases out and the
			// middle ones are linear, so the unit walks the whole path without stopping on every cell.
			// Pushed objects move linearly (Godot TRANS_LINEAR).
			float Eased = Mover.Alpha;
			const int32 Steps = Mover.Points.Num();
			if (Mover.bFaceSteps)
			{
				if (Steps == 1)
				{
					Eased = 0.5f - 0.5f * FMath::Cos(PI * Mover.Alpha);
				}
				else if (Mover.Index == 0)
				{
					Eased = 1.f - FMath::Cos(HALF_PI * Mover.Alpha);
				}
				else if (Mover.Index == Steps - 1)
				{
					Eased = FMath::Sin(HALF_PI * Mover.Alpha);
				}
			}
			Actor->SetActorLocation(FMath::Lerp(Mover.From, Mover.Points[Mover.Index], Eased));
			Mover.CurrentSpeed = DeltaTime > 0.f ? FVector::Dist2D(Before, Actor->GetActorLocation()) / DeltaTime : 0.f;
			if (Mover.Alpha >= 1.f)
			{
				const bool bContinue = !Mover.OnStep || Mover.OnStep(Mover.Index);
				if (!Movers.IsValidIndex(Index) || !Mover.Actor.IsValid())
				{
					continue; // combat ended from the step callback
				}
				Mover.From = Mover.Points[Mover.Index];
				++Mover.Index;
				Mover.Alpha = 0.f;
				bFinished = !bContinue || Mover.Index >= Mover.Points.Num();
				if (!bFinished && Mover.bFaceSteps)
				{
					const FIntPoint Next = FGorky17Utils::FacingToVector(Mover.Facings[Mover.Index]);
					Mover.TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Next.Y), static_cast<float>(Next.X)));
				}
			}
		}
		if (bFinished && Movers.IsValidIndex(Index))
		{
			TFunction<void()> Done = MoveTemp(Movers[Index].OnDone);
			Movers.RemoveAt(Index);
			if (Done)
			{
				Done();
			}
		}
	}
	EnforceHeldEnemies();
}

void UTurnBasedCombatSubsystem::EnforceHeldEnemies()
{
	if (!IsActive() || !Grid)
	{
		return;
	}
	// Guard (bug fix 2026-10-06): no frozen enemy may leave its place — a grid unit stays on its cell (outside its own
	// scripted walk), an enemy in stasis where the fight found it. Whatever pushed it (an AI move from an event, a
	// depenetration, a launch, root motion), it is put back and stopped.
	const float Tolerance = TurnClickRules::GetHeldUnitTolerance(TurnCellSize);
	for (TPair<TWeakObjectPtr<AEnemyCharacter>, FVector>& Entry : HeldAnchors)
	{
		AEnemyCharacter* Enemy = Entry.Key.Get();
		if (!Enemy || Enemy->IsDying() || IsDead(Enemy))
		{
			continue;
		}
		if (Movers.ContainsByPredicate([Enemy](const FMover& Mover) { return Mover.Actor.Get() == Enemy; }))
		{
			continue; // its own walk / step back on the grid
		}
		FVector Anchor = Entry.Value;
		if (const FTurnUnitState* State = States.Find(Enemy))
		{
			const FVector CellCentre = Grid->GridToWorld(State->GridPos);
			Anchor = FVector(CellCentre.X, CellCentre.Y, Entry.Value.Z);
		}
		const FVector Location = Enemy->GetActorLocation();
		const bool bOffGrid = States.Contains(Enemy) && !TurnClickRules::IsInsideGrid(Location, Grid->OriginWorld, TurnGridCells, TurnCellSize);
		if (!bOffGrid && !TurnClickRules::IsHeldUnitDisplaced(Location, Anchor, Tolerance))
		{
			continue;
		}
		UE_LOG(LogCodexTactics, Warning, TEXT("[TurnBased] guard: %s pushed %.0f cm off its place%s - put back"), *Enemy->GetName(),
			FVector::Dist2D(Location, Anchor), bOffGrid ? TEXT(" (outside the grid)") : TEXT(""));
		if (AController* Controller = Enemy->GetController())
		{
			Controller->StopMovement();
		}
		Enemy->GetCharacterMovement()->StopMovementImmediately();
		Enemy->SetActorLocation(FVector(Anchor.X, Anchor.Y, FMath::Max(Location.Z, Anchor.Z)), false, nullptr, ETeleportType::TeleportPhysics);
		++GuardCorrections;
	}
}

// --- Squad turn ---------------------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::StartPlayerTurn()
{
	Phase = ETurnPhase::Squad;
	bSquadUnitMoving = false;
	bAttackMode = false;

	// Burning barrels count their rounds down (Godot _start_player_turn).
	TArray<TWeakObjectPtr<AActor>> BurntOut;
	for (TPair<TWeakObjectPtr<AActor>, int32>& Entry : BurningBarrels)
	{
		if (!Entry.Key.IsValid())
		{
			BurntOut.Add(Entry.Key);
			continue;
		}
		if (--Entry.Value <= 0)
		{
			BurntOut.Add(Entry.Key);
		}
		else
		{
			Log(FString::Printf(TEXT("🔥 The barrel keeps burning (turns left: %d)"), Entry.Value));
		}
	}
	for (const TWeakObjectPtr<AActor>& Barrel : BurntOut)
	{
		BurningBarrels.Remove(Barrel);
		if (ABarrelActor* BarrelActor = Cast<ABarrelActor>(Barrel.Get()))
		{
			BarrelActor->ExtinguishNow();
			Log(TEXT("💨 The fuel barrel has burnt out!"));
		}
	}

	Squad.RemoveAll([this](const TWeakObjectPtr<AOperativeCharacter>& Member) { return IsDead(Member.Get()); });
	if (Squad.IsEmpty())
	{
		CheckBattleEnd();
		return;
	}
	ActiveIndex = FMath::Clamp(ActiveIndex, 0, Squad.Num() - 1);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
	{
		if (FTurnUnitState* State = States.Find(Member.Get()))
		{
			State->bHasAttacked = false;
			State->AP = State->MaxAP;
		}
	}
	if (USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		SquadSystem->SetLeader(GetActiveUnit()); // the camera follows the active operative
	}
	FocusSquadTurn(GetActiveUnit()); // Godot _start_player_turn -> turn_changed
	RefreshOverlay();
	Changed();
}

void UTurnBasedCombatSubsystem::RefreshOverlay()
{
	if (!Overlay || !Grid)
	{
		return;
	}
	Overlay->ClearLayer(ETurnOverlayLayer::EnemyReach);
	Overlay->ClearLayer(ETurnOverlayLayer::Warning);
	TArray<FIntPoint> Fear = GetFearCells().Array();
	Overlay->SetCells(ETurnOverlayLayer::Fear, Fear);
	const AOperativeCharacter* Unit = GetActiveUnit();
	const FTurnUnitState* State = GetUnitState(Unit);
	if (Phase != ETurnPhase::Squad || !State || bSquadUnitMoving)
	{
		Overlay->ClearLayer(ETurnOverlayLayer::Reachable);
		Overlay->ClearLayer(ETurnOverlayLayer::Attack);
		Overlay->ClearLayer(ETurnOverlayLayer::Active);
		return;
	}
	Overlay->SetCells(ETurnOverlayLayer::Active, { State->GridPos });
	if (IsRelocating())
	{
		// Godot: the relocation targets replace the walk cells, no attack preview meanwhile.
		TArray<FIntPoint> Targets;
		RelocateCells.GetKeys(Targets);
		Overlay->SetCells(ETurnOverlayLayer::Reachable, Targets);
		Overlay->ClearLayer(ETurnOverlayLayer::Attack);
		return;
	}
	TArray<FIntPoint> Reach;
	// Crouched (and prone: he walks crouched) every step costs CrouchMoveCostMultiplier times more (user decision 2026-10-04).
	const int32 WalkMultiplier = TurnBasedRules::MoveCostMultiplier(State->Stance, Balance);
	for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(State->GridPos, State->AP / WalkMultiplier))
	{
		if (Entry.Key != State->GridPos)
		{
			Reach.Add(Entry.Key);
		}
	}
	// Godot _update_reachable_overlay_for_active_unit: the attack mode shows only the weapon's dot matrix, the move mode
	// only the green walk cells.
	if (bAttackMode)
	{
		Overlay->ClearLayer(ETurnOverlayLayer::Reachable);
		AttackCells = TurnBasedRules::GetWeaponAttackCells(*Grid, State->GridPos, WeaponOf(Unit), State->Stance, Balance);
		TArray<FIntPoint> Cells;
		TArray<float> Falloff;
		for (const TPair<FIntPoint, FTurnBasedAttackCell>& Entry : AttackCells)
		{
			// Godot: lerp(1.0, 0.25, (distance - 1) / max(1, max_range - 1)).
			const float Span = FMath::Max(1.f, static_cast<float>(Entry.Value.MaxRange - 1));
			Cells.Add(Entry.Key);
			Falloff.Add(FMath::Lerp(1.f, 0.25f, static_cast<float>(Entry.Value.Distance - 1) / Span));
		}
		Overlay->SetAttackCells(Cells, Falloff);
		return;
	}
	AttackCells.Reset();
	Overlay->SetAttackCells(TArray<FIntPoint>(), TArray<float>());
	Overlay->SetCells(ETurnOverlayLayer::Reachable, Reach);
}

void UTurnBasedCombatSubsystem::EnterAttackMode()
{
	if (Phase != ETurnPhase::Squad || !GetActiveUnit())
	{
		return;
	}
	bAttackMode = true;
	const UWeaponDataAsset* Weapon = WeaponOf(GetActiveUnit());
	Log(FString::Printf(TEXT("🎯 Aim mode: %s"), Weapon && !Weapon->WeaponName.IsEmpty() ? *Weapon->WeaponName.ToString() : TEXT("M16")));
	RefreshOverlay();
	Changed();
}

void UTurnBasedCombatSubsystem::ExitAttackMode(const FString& Line)
{
	if (!bAttackMode)
	{
		return;
	}
	bAttackMode = false;
	if (!Line.IsEmpty())
	{
		Post(TEXT("TACTICS"), Line);
	}
	RefreshOverlay();
	Changed();
}

bool UTurnBasedCombatSubsystem::ToggleAttackMode()
{
	if (bAttackMode)
	{
		ExitAttackMode();
		return false;
	}
	EnterAttackMode();
	return bAttackMode;
}

void UTurnBasedCombatSubsystem::SetHoveredPoint(const FVector& WorldPoint, const AActor* HitActor)
{
	// The mouse over a unit's body (the hit point may lie over the next cell) means that unit's own cell.
	const FTurnUnitState* HitState = HitActor ? GetUnitState(HitActor) : nullptr;
	HoveredCell = HitState ? HitState->GridPos : (Grid ? Grid->WorldToGrid(WorldPoint) : FIntPoint(-999, -999));
	// Godot set_hovered_cell: the cursor frame on the hovered cell during the squad's turn (red over an enemy).
	if (Overlay)
	{
		const bool bShow = IsActive() && Phase == ETurnPhase::Squad && Grid && Grid->IsValidCell(HoveredCell);
		Overlay->SetCursorCell(bShow ? HoveredCell : FIntPoint(-999, -999),
			bShow && Grid->GetOccupantType(HoveredCell) == EGorkyOccupantType::Enemy);
	}
}

bool UTurnBasedCombatSubsystem::GetHoverHitChance(FVector& OutWorld, FString& OutText) const
{
	const FTurnBasedAttackCell* Info = bAttackMode && Grid ? AttackCells.Find(HoveredCell) : nullptr;
	if (!Info)
	{
		return false;
	}
	OutWorld = Grid->GridToWorld(HoveredCell) + FVector(0.f, 0.f, 160.f);
	OutText = FString::Printf(TEXT("🎯 %d%%"), FMath::RoundToInt(Info->HitChance * 100.f));
	if (Info->ProjectedDamage > 0.f)
	{
		OutText += FString::Printf(TEXT(" | 💥 %d"), FMath::RoundToInt(Info->ProjectedDamage));
	}
	return true;
}

bool UTurnBasedCombatSubsystem::SelectUnit(AOperativeCharacter* Unit)
{
	if (IsBusy() || Phase != ETurnPhase::Squad)
	{
		return false;
	}
	RelocateTarget.Reset();
	RelocateCells.Reset();
	const int32 Index = Squad.IndexOfByKey(Unit);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Index == ActiveIndex)
	{
		// Godot: the active operative's own number or a click on him brings the camera to him from anywhere.
		if (ATacticalCameraPawn* Camera = GetCamera())
		{
			Camera->SmoothFocusOnTarget(Unit);
		}
		Post(TEXT("CAMERA"), FString::Printf(TEXT("🎥 Camera on operative: %s"), *NameOf(Unit)));
		return true;
	}
	ActiveIndex = Index;
	bAttackMode = false; // Godot select_squad_unit
	if (USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		SquadSystem->SetLeader(Unit);
	}
	FocusSquadTurn(Unit); // Godot select_squad_unit -> turn_changed
	RefreshOverlay();
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::MoveActiveUnitTo(const FIntPoint& Cell)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (IsBusy() || Phase != ETurnPhase::Squad || !State || State->GridPos == Cell || IsActiveUnitKnockedDown())
	{
		return false;
	}
	// Only highlighted cells (Godot: reachable with the AP left); crouched / prone a step costs double (user decision 2026-10-04).
	const int32 WalkMultiplier = TurnBasedRules::MoveCostMultiplier(State->Stance, Balance);
	const TMap<FIntPoint, int32> Reach = Grid->GetReachableCells(State->GridPos, State->AP / WalkMultiplier);
	const int32* StepCost = Reach.Find(Cell);
	const TArray<FIntPoint> Path = Grid->FindPath(State->GridPos, Cell, State->AP / WalkMultiplier);
	const int32 WalkCost = StepCost ? *StepCost * WalkMultiplier : 0;
	const int32* Cost = StepCost ? &WalkCost : nullptr;
	if (!Cost || Path.IsEmpty() || *Cost > State->AP)
	{
		return false;
	}
	const FIntPoint From = State->GridPos;
	Grid->ClearOccupant(From);

	// A mine on the way stops the walk on its cell.
	TArray<FIntPoint> Actual;
	FIntPoint MineCell(-999, -999);
	AActor* Mine = nullptr;
	for (const FIntPoint& Step : Path)
	{
		Actual.Add(Step);
		if (Grid->GetOccupantType(Step) == EGorkyOccupantType::Mine)
		{
			MineCell = Step;
			Mine = Grid->GetOccupant(Step);
			break;
		}
	}
	const FIntPoint Final = Actual.Last();
	const FIntPoint LastFrom = Actual.Num() > 1 ? Actual[Actual.Num() - 2] : From;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Final - LastFrom));
	if (Mine)
	{
		State->AP = 0;
		Log(FString::Printf(TEXT("🛑 A mine blast cut short the move of %s!"), *NameOf(Unit)));
	}
	else
	{
		State->AP -= *Cost;
	}
	State->GridPos = Final;
	Grid->SetOccupant(Final, Unit, EGorkyOccupantType::Squad);

	bSquadUnitMoving = true;
	float StepDuration = SquadStepDuration;
	const float RiseDelay = PrepareSquadWalk(Unit, StepDuration);
	RefreshOverlay();
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	TWeakObjectPtr<AActor> WeakMine(Mine);
	StartMoverAfter(RiseDelay, Unit, From, Actual, StepDuration, [this, WeakUnit, WeakMine, MineCell]()
	{
		bSquadUnitMoving = false;
		AOperativeCharacter* Moved = WeakUnit.Get();
		if (Moved)
		{
			if (const FTurnUnitState* MovedState = GetUnitState(Moved))
			{
				AlignFacing(Moved, MovedState->Facing);
			}
			if (MineCell.X != -999)
			{
				DetonateMine(MineCell, WeakMine.Get(), Moved);
			}
		}
		if (IsActive())
		{
			RefreshOverlay();
			Changed();
		}
	});
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::SetActiveUnitStance(EOperativeStance NewStance)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (IsBusy() || Phase != ETurnPhase::Squad || !State || IsActiveUnitKnockedDown())
	{
		return false;
	}
	if (State->Stance == NewStance)
	{
		return true;
	}
	if (State->AP < Balance.StanceAPCost)
	{
		Log(FString::Printf(TEXT("⚠️ Not enough AP to change stance (%d/%d AP)!"), State->AP, Balance.StanceAPCost));
		return false;
	}
	State->AP -= Balance.StanceAPCost;
	State->Stance = NewStance;
	Unit->SetStance(NewStance);
	Log(FString::Printf(TEXT("🛡️ %s changes stance: %s (%d AP)"), *NameOf(Unit), TurnStanceName(NewStance), Balance.StanceAPCost));
	RefreshOverlay();
	Changed();
	return true;
}

EOperativeStance UTurnBasedCombatSubsystem::CycleActiveUnitStance()
{
	const FTurnUnitState* State = GetUnitState(GetActiveUnit());
	if (!State)
	{
		return EOperativeStance::Standing;
	}
	const EOperativeStance Next = static_cast<EOperativeStance>((static_cast<int32>(State->Stance) + 1) % 3);
	return SetActiveUnitStance(Next) ? Next : State->Stance;
}

bool UTurnBasedCombatSubsystem::TurnActiveUnitFacing(EGorkyFacing NewFacing)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (IsBusy() || Phase != ETurnPhase::Squad || !State || State->AP < 1 || State->Facing == NewFacing)
	{
		return false;
	}
	State->AP -= 1;
	State->Facing = NewFacing;
	AlignFacing(Unit, NewFacing);
	RefreshOverlay();
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::RotateActiveUnitClockwise()
{
	const FTurnUnitState* State = GetUnitState(GetActiveUnit());
	// Godot HUD turn_facing_pressed: (facing + 1) % 4 over the enum (N, E, S, W; diagonals fall back to the cardinals).
	return State && TurnActiveUnitFacing(static_cast<EGorkyFacing>((static_cast<int32>(State->Facing) + 1) % 4));
}

FTurnAttackResult UTurnBasedCombatSubsystem::AttackCell(const FIntPoint& Cell, bool bGuaranteeHit, bool bSkipShake)
{
	const FTurnAttackResult Result = ResolveAttackCell(Cell, bGuaranteeHit, bSkipShake);
	if (Result.bSuccess && bAttackMode && IsActive())
	{
		bAttackMode = false; // Godot attack_target_cell -> exit_attack_mode after a shot
		RefreshOverlay();
		Changed();
	}
	return Result;
}

FTurnAttackResult UTurnBasedCombatSubsystem::ResolveAttackCell(const FIntPoint& Cell, bool bGuaranteeHit, bool bSkipShake)
{
	FTurnAttackResult Result;
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (IsBusy() || Phase != ETurnPhase::Squad || !State || IsActiveUnitKnockedDown())
	{
		Result.Reason = TEXT("no_unit");
		return Result;
	}
	if (State->bHasAttacked)
	{
		Log(FString::Printf(TEXT("⚠️ %s has already attacked this round! Only 1 attack per turn."), *NameOf(Unit)));
		Result.Reason = TEXT("already_attacked");
		return Result;
	}
	if (State->AP < Balance.AttackAPCost)
	{
		Log(FString::Printf(TEXT("⚠️ Not enough AP to attack (%d/%d AP)!"), State->AP, Balance.AttackAPCost));
		Result.Reason = TEXT("not_enough_ap");
		return Result;
	}
	AActor* Target = Grid->GetOccupant(Cell);
	const EGorkyOccupantType Type = Grid->GetOccupantType(Cell);
	if (!Target)
	{
		Result.Reason = TEXT("no_target");
		return Result;
	}
	const UWeaponDataAsset* Weapon = WeaponOf(Unit);
	const FIntPoint Offset = Cell - State->GridPos;
	if (!TurnBasedRules::IsTargetInPattern(Weapon, Offset))
	{
		Log(Weapon ? FString::Printf(TEXT("⚠️ Target is not in the weapon's line of fire (%s)!"), *Weapon->WeaponName.ToString()) : FString(TEXT("⚠️ Target is not in the weapon's line of fire!")));
		Result.Reason = TEXT("not_in_fire_lane");
		return Result;
	}
	bool bThroughCover = false;
	if (!GorkyLineOfSight::HasLineOfFireThroughCover(State->GridPos, Cell, *Grid, bThroughCover))
	{
		Log(TEXT("⚠️ No line of sight (LoS) to the target!"));
		Result.Reason = TEXT("no_los");
		return Result;
	}
	// Cover (user rule 2026-10-06): a grid shot from cover goes round the corner / over the top like a real-time one;
	// a full wall without an exposed corner has no firing position. Only a target behind the wall / around the corner
	// needs that (CoverFacingRules::ShouldCornerShot); one out on the open side gets a normal shot off the wall and he
	// comes back to the slot afterwards (AOperativeCharacter::BeginOpenShotFromCover).
	const bool bCoverShot = Unit->bInCover && Unit->IsCornerShotTarget(Target->GetActorLocation());
	if (bCoverShot && !Unit->CanFireFromCover())
	{
		Log(TEXT("⚠️ No firing angle from this cover - move to the edge of the wall!"));
		Result.Reason = TEXT("no_cover_corner");
		return Result;
	}

	if (Unit->bInCover && !bCoverShot)
	{
		Unit->BeginOpenShotFromCover(Target);
	}

	State->AP -= Balance.AttackAPCost;
	State->bHasAttacked = true;
	// Godot main.gd squad attack: the camera shakes by the weapon (pistol / rifle).
	if (!bSkipShake)
	{
		ShakeCamera(Weapon && Weapon->WeaponId == TEXT("pistol") ? TEXT("pistol") : TEXT("rifle"));
	}
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Offset));
	if (!bCoverShot)
	{
		AlignFacing(Unit, State->Facing); // in cover the body stays along the wall (PlayCoverShot turns it to the target's side)
		Unit->FaceAimAt(Target->GetActorLocation()); // user rule 2026-10-07: the barrel on the target before the shot (not the 8-way grid facing)
	}
	const FVector Muzzle = bCoverShot ? Unit->GetCoverFireOrigin() : Unit->GetWeaponMuzzleLocation();
	const int32 Distance = TurnBasedRules::CellDistance(State->GridPos, Cell);

	if (Type == EGorkyOccupantType::Enemy)
	{
		// Past a barricade next to him or the target: less accurate (user decision 2026-10-04).
		const float Chance = TurnBasedRules::CalculateHitChance(Weapon, Distance, State->Stance, Balance)
			* (bThroughCover ? Balance.CoverFireAccuracyMultiplier : 1.f);
		if (bThroughCover)
		{
			Log(FString::Printf(TEXT("🧱 Firing over a barricade: accuracy x%.2f"), Balance.CoverFireAccuracyMultiplier));
		}
		const float Roll = FMath::FRand();
		Result.bSuccess = true;
		Result.HitChance = Chance;
		Result.bHit = bGuaranteeAllHits || bGuaranteeHit || Roll <= Chance;
		if (bCoverShot)
		{
			Unit->PlayCoverShot(Target, Result.bHit);
		}
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			FVector End = Target->GetActorLocation();
			if (!Result.bHit)
			{
				End += FVector(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(20.f, 120.f));
			}
			Feedback->SpawnTracer(Muzzle, End, Weapon ? Weapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
				Weapon ? Weapon->DamageType : EDamageType::Kinetic);
		}
		if (!Result.bHit)
		{
			Log(FString::Printf(TEXT("❌ MISS! Chance: %d%% (rolled: %d%%)"), FMath::RoundToInt(Chance * 100.f), FMath::RoundToInt(Roll * 100.f)));
		}
		else if (FTurnUnitState* EnemyState = States.Find(Target))
		{
			const FGorkyArcResult Arc = FGorky17Utils::CalculateAttackArc(State->GridPos, Cell, EnemyState->Facing);
			const float Base = TurnBasedRules::GetDamageForDistance(Weapon, Distance, State->BaseDamage);
			Result.Damage = TurnBasedRules::SquadAttackDamage(Base, Arc.DamageMultiplier, EnemyState->Armor, Arc.EffectiveArmorMultiplier);
			ApplyEnemyHit(Target, Result.Damage, NameOf(Unit));
			Log(FString::Printf(TEXT("💥 Attack on %s: %d damage (%s, x%.2f) [Accuracy: %d%%]"), *NameOf(Target), Result.Damage, TurnArcName(Arc.Arc),
				Arc.DamageMultiplier, FMath::RoundToInt(Chance * 100.f)));
			if (IsDead(Target))
			{
				OnEnemyKilled(Target, Cell);
			}
		}
	}
	else if (Type == EGorkyOccupantType::Barrel)
	{
		Result.bSuccess = true;
		Result.bBarrelExploded = true;
		if (bCoverShot)
		{
			Unit->PlayCoverShot(Target, true);
		}
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnTracer(Muzzle, Target->GetActorLocation(), UCombatFeedbackSubsystem::DefaultTracerColor());
		}
		DetonateBarrel(Cell, Target);
		if (!IsActive())
		{
			return Result; // the blast ended the fight
		}
	}
	else if (Type == EGorkyOccupantType::Barricade)
	{
		// Godot _damage_barricade (the trapped-barricade retaliation comes with the grid deployables).
		Result.bSuccess = true;
		if (bCoverShot)
		{
			Unit->PlayCoverShot(Target, true);
		}
		ApplyDamage(Target, State->BaseDamage, NameOf(Unit));
	}
	else
	{
		Result.Reason = TEXT("no_target");
	}
	RefreshOverlay();
	Changed();
	CheckBattleEnd();
	return Result;
}

bool UTurnBasedCombatSubsystem::CanAttackQuietly(const FIntPoint& Cell) const
{
	const AOperativeCharacter* Unit = GetActiveUnit();
	const FTurnUnitState* State = GetUnitState(Unit);
	if (IsBusy() || Phase != ETurnPhase::Squad || !State || State->bHasAttacked || State->AP < Balance.AttackAPCost || !Grid
		|| !Grid->GetOccupant(Cell))
	{
		return false;
	}
	return TurnBasedRules::IsTargetInPattern(WeaponOf(Unit), Cell - State->GridPos) && GorkyLineOfSight::HasLineOfSight(State->GridPos, Cell, *Grid);
}

void UTurnBasedCombatSubsystem::AttackCellCinematic(const FIntPoint& Cell)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	AActor* Target = Grid ? Grid->GetOccupant(Cell) : nullptr;
	ATacticalCameraPawn* Camera = GetCamera();
	if (!bCinematics || !Camera || !Unit || !Target || !CanAttackQuietly(Cell))
	{
		AttackCell(Cell);
		return;
	}
	// Phase 1: the shooter turns to the target, the camera frames both.
	bDramaticShotActive = true;
	Camera->SetDramaticShotActive(true);
	Highlight(Target);
	if (FTurnUnitState* State = States.Find(Unit))
	{
		State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Cell - State->GridPos));
		AlignFacing(Unit, State->Facing);
	}
	Camera->DramaticActionFocus(Unit, Target, 0.4f);
	RefreshOverlay();
	Changed();
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	After(0.4f, [this, WeakUnit, Cell]()
	{
		// Phase 2: the shot fires (fire animation: the AnimBP) and shakes the camera.
		const UWeaponDataAsset* Weapon = WeaponOf(WeakUnit.Get());
		ShakeCamera(Weapon && Weapon->WeaponId == TEXT("pistol") ? TEXT("pistol") : TEXT("rifle"));
		After(0.35f, [this, WeakUnit, Cell]()
		{
			// Phase 3: the round lands.
			bDramaticShotActive = false;
			AttackCell(Cell, false, /*bSkipShake*/ true);
			if (!IsActive())
			{
				return; // the last enemy fell: EndCombat reset the camera
			}
			bDramaticShotActive = true;
			After(0.65f, [this, WeakUnit]()
			{
				// Phase 4: back to the tactical view on the shooter.
				if (ATacticalCameraPawn* Cam = GetCamera())
				{
					Cam->SmoothFocusOnTarget(WeakUnit.Get(), 1.1f, Cam->Config.DistanceCombat);
				}
				After(1.1f, [this]()
				{
					bDramaticShotActive = false;
					if (ATacticalCameraPawn* Cam = GetCamera())
					{
						Cam->SetDramaticShotActive(false);
					}
					RefreshOverlay();
					Changed();
				});
			});
		});
	});
}

void UTurnBasedCombatSubsystem::EndCurrentUnitTurn()
{
	if (IsBusy() || Phase != ETurnPhase::Squad)
	{
		return;
	}
	RelocateTarget.Reset();
	RelocateCells.Reset();
	bAttackMode = false; // Godot end_current_unit_turn
	if (AOperativeCharacter* Ending = GetActiveUnit())
	{
		Ending->EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("turn over")); // the corner fire stance lasts his turn
	}
	++ActiveIndex;
	if (Squad.IsValidIndex(ActiveIndex))
	{
		if (FTurnUnitState* State = States.Find(Squad[ActiveIndex].Get()))
		{
			State->AP = State->MaxAP;
		}
		if (USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			SquadSystem->SetLeader(GetActiveUnit());
		}
		FocusSquadTurn(GetActiveUnit()); // Godot end_current_unit_turn -> turn_changed
		RefreshOverlay();
		Changed();
		return;
	}
	EndSquadPhase();
}

bool UTurnBasedCombatSubsystem::SwitchActiveUnitWeapon(const FString& WeaponId)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	if (!Unit || !Unit->SwitchToWeaponById(WeaponId))
	{
		return false;
	}
	const FString Name = Unit->CurrentWeapon && !Unit->CurrentWeapon->WeaponName.IsEmpty() ? Unit->CurrentWeapon->WeaponName.ToString() : WeaponId;
	Log(FString::Printf(TEXT("🔫 %s selects weapon: %s"), *NameOf(Unit), *Name));
	RefreshOverlay();
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::EndTurnAfterMedkit(AOperativeCharacter* Unit)
{
	FTurnUnitState* State = States.Find(Unit);
	if (!State || Phase != ETurnPhase::Squad || IsBusy() || GetActiveUnit() != Unit)
	{
		return false;
	}
	State->AP = 0;
	Log(FString::Printf(TEXT("💊 %s uses a medkit - turn over."), *NameOf(Unit)));
	EndCurrentUnitTurn();
	return true;
}

void UTurnBasedCombatSubsystem::PassSquadTurn()
{
	if (IsBusy() || Phase != ETurnPhase::Squad)
	{
		return;
	}
	Log(TEXT("🛑 Squad turn over. Enemy turn!"));
	EndSquadPhase();
}

void UTurnBasedCombatSubsystem::HandleWorldClick(const FVector& WorldPoint, AActor* HitActor, bool bAttackOrder)
{
	if (!IsActive() || Phase != ETurnPhase::Squad || IsBusy() || !Grid || IsActiveUnitKnockedDown())
	{
		return;
	}
	FIntPoint Cell(-1, -1);
	const FVector Local = WorldPoint - Grid->OriginWorld;
	const bool bOnGrid = Local.X >= 0.f && Local.Y >= 0.f && Local.X < TurnGridCells * TurnCellSize && Local.Y < TurnGridCells * TurnCellSize;

	// Godot 0.1: relocation mode — the click picks the target cell (floor point, else the clicked body's cell).
	if (IsRelocating())
	{
		if (bOnGrid)
		{
			Cell = Grid->WorldToGrid(WorldPoint);
		}
		else if (HitActor)
		{
			Cell = Grid->WorldToGrid(HitActor->GetActorLocation());
		}
		if (IsRelocatingBarricade())
		{
			AActor* Barricade = RelocateTarget.Get();
			if (Grid->IsValidCell(Cell) && CanPlaceBarricadeAt(Barricade, Cell, RelocateYaw))
			{
				const int32* Cost = RelocateCells.Find(Cell);
				const int32 APCost = Cost ? *Cost : Balance.PushBarrelAPCost;
				const float Yaw = RelocateYaw;
				RelocateTarget.Reset();
				RelocateCells.Reset();
				if (RelocateBarricade(Barricade, Cell, Yaw, APCost))
				{
					Highlight(Barricade);
					Post(TEXT("TACTICS"), FString::Printf(TEXT("✅ Barricade placed (-%d AP)."), APCost));
				}
				else
				{
					Post(TEXT("TACTICS"), TEXT("⚠️ Failed to place the barricade!"));
					RefreshOverlay();
				}
			}
			else if (Cell == RelocateOrigin && FMath::IsNearlyZero(FRotator::NormalizeAxis(RelocateYaw - Barricade->GetActorRotation().Yaw), 0.5f))
			{
				CancelRelocate();
			}
			else
			{
				Post(TEXT("TACTICS"), TEXT("⚠️ Cannot place the barricade on this cell at this rotation (or not enough AP)!"));
			}
			return;
		}
		if (const int32* Cost = RelocateCells.Find(Cell))
		{
			const int32 APCost = *Cost;
			AActor* Moved = RelocateTarget.Get();
			const FIntPoint From = RelocateOrigin;
			RelocateTarget.Reset();
			RelocateCells.Reset();
			if (RelocateObject(From, Cell, APCost))
			{
				Highlight(Moved);
				Post(TEXT("TACTICS"), FString::Printf(TEXT("✅ Object relocated (-%d AP)."), APCost));
			}
			else
			{
				Post(TEXT("TACTICS"), TEXT("⚠️ Failed to relocate the object!"));
				RefreshOverlay();
			}
		}
		else if (Cell == RelocateOrigin)
		{
			CancelRelocate();
		}
		else
		{
			Post(TEXT("TACTICS"), TEXT("⚠️ That cell cannot be reached (out of AP range or occupied)!"));
		}
		return;
	}

	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(HitActor); Operative && Squad.Contains(Operative))
	{
		if (!bAttackOrder)
		{
			SelectUnit(Operative); // Ctrl + click on a squad mate: no friendly fire, no selection
		}
		return;
	}
	if (const FTurnUnitState* State = GetUnitState(HitActor))
	{
		Cell = State->GridPos;
	}
	else if (bOnGrid)
	{
		Cell = Grid->WorldToGrid(WorldPoint);
	}
	else
	{
		return;
	}
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	const EGorkyOccupantType Occupant = Grid->GetOccupantType(Cell);
	AActor* Occupier = Grid->GetOccupant(Cell);
	// Relocation reach: a barrel is pushed from an orthogonal neighbour cell, a turret / barricade from any neighbour.
	bool bAdjacent = false;
	if (Occupant == EGorkyOccupantType::Barrel)
	{
		const FIntPoint Diff = UnitState ? Cell - UnitState->GridPos : FIntPoint(99, 99);
		bAdjacent = FMath::Abs(Diff.X) + FMath::Abs(Diff.Y) == 1;
	}
	else if (Occupant == EGorkyOccupantType::Turret || Occupant == EGorkyOccupantType::Barricade)
	{
		bAdjacent = IsUnitAdjacentToObject(GetActiveUnit(), Occupier);
	}
	// User request 2026-10-06: Ctrl + click is the attack order here too (Godot used Shift on the grid).
	switch (TurnClickRules::ResolveClick(Occupant, bAttackOrder, bAdjacent, bAttackMode))
	{
	case ETurnClickAction::SelectUnit:
		SelectUnit(Cast<AOperativeCharacter>(Occupier));
		break;
	case ETurnClickAction::Attack:
		AttackCellCinematic(Cell);
		break;
	case ETurnClickAction::Relocate:
		Highlight(Occupier);
		StartRelocate(Occupier);
		break;
	case ETurnClickAction::NeedApproach:
		Highlight(Occupier);
		Post(TEXT("TACTICS"), Occupant == EGorkyOccupantType::Barrel
			? TEXT("⚠️ Move right next to the barrel to relocate it (or Ctrl + click to shoot)!")
			: (Occupant == EGorkyOccupantType::Barricade
				? TEXT("⚠️ Move right next to the barricade to relocate it (or Ctrl + click to attack)!")
				: TEXT("⚠️ Move right next to the turret to relocate it!")));
		break;
	case ETurnClickAction::NoTargetInAttackMode:
		// Godot main.gd: in the attack mode an empty cell is no walk order.
		Post(TEXT("TACTICS"), TEXT("⚠️ No target on this cell! (RMB / Esc to return to movement)"));
		break;
	case ETurnClickAction::NoTargetForAttackOrder:
		Post(TEXT("TACTICS"), TEXT("⚠️ Ctrl + click to attack: pick an enemy, a barrel or a barricade."));
		break;
	case ETurnClickAction::Walk:
		MoveActiveUnitTo(Cell);
		break;
	default:
		break;
	}
}

// --- Object relocation ----------------------------------------------------------------------------------------------

bool UTurnBasedCombatSubsystem::IsUnitAdjacentToObject(const AActor* Unit, const AActor* Object) const
{
	const FTurnUnitState* UnitState = GetUnitState(Unit);
	if (!UnitState || !IsValid(Object) || !Grid)
	{
		return false;
	}
	TArray<FIntPoint> ObjectCells;
	for (int32 X = 0; X < Grid->GridSize.X; ++X)
	{
		for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
		{
			if (Grid->GetOccupant(FIntPoint(X, Y)) == Object)
			{
				ObjectCells.Add(FIntPoint(X, Y));
			}
		}
	}
	if (ObjectCells.IsEmpty())
	{
		const FTurnUnitState* ObjectState = GetUnitState(Object);
		ObjectCells.Add(ObjectState ? ObjectState->GridPos : Grid->WorldToGrid(Object->GetActorLocation()));
	}
	for (const FIntPoint& Cell : ObjectCells)
	{
		if (FMath::Max(FMath::Abs(UnitState->GridPos.X - Cell.X), FMath::Abs(UnitState->GridPos.Y - Cell.Y)) <= 1)
		{
			return true;
		}
	}
	return false;
}

bool UTurnBasedCombatSubsystem::StartRelocate(AActor* Object)
{
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	const FTurnUnitState* ObjectState = GetUnitState(Object);
	const bool bBarricade = IsValid(Object) && Object->IsA<ABarricadeActor>();
	if (Phase != ETurnPhase::Squad || IsBusy() || !UnitState || (!ObjectState && !bBarricade) || !Grid)
	{
		return false;
	}
	const int32 CostPerStep = Balance.PushBarrelAPCost;
	if (UnitState->AP < CostPerStep)
	{
		Post(TEXT("TACTICS"), FString::Printf(TEXT("⚠️ Not enough action points to move (at least %d AP needed)!"), CostPerStep));
		return false;
	}
	RelocateTarget = Object;
	RelocateOrigin = ObjectState ? ObjectState->GridPos : Grid->WorldToGrid(Object->GetActorLocation());
	RelocateYaw = Object->GetActorRotation().Yaw;
	RelocateCells.Reset();
	if (bBarricade)
	{
		RefreshBarricadeTargets();
		RefreshOverlay();
		Post(TEXT("TACTICS"), TEXT("🧱 Barricade placement: [Mouse wheel/Q/E - rotate 45°, LMB - confirm, RMB/[Esc] - cancel]"));
		Changed();
		return true;
	}
	// Godot: reachable in one step (orthogonal; a diagonal costs 2) and walkable.
	for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(RelocateOrigin, 1))
	{
		if (Entry.Value == 1 && Entry.Key != RelocateOrigin && Grid->IsCellWalkable(Entry.Key))
		{
			RelocateCells.Add(Entry.Key, CostPerStep);
		}
	}
	RefreshOverlay();
	Post(TEXT("TACTICS"), FString::Printf(TEXT("📦 Pick a free adjacent cell to move the %s (1 step = %d AP) [LMB - confirm, RMB/[Esc] - cancel]"),
		Object->IsA<ABarrelActor>() ? TEXT("barrel") : TEXT("object"), CostPerStep));
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::IsRelocatingBarricade() const
{
	return IsRelocating() && RelocateTarget->IsA<ABarricadeActor>();
}

void UTurnBasedCombatSubsystem::RotateRelocation(int32 Steps)
{
	if (!IsRelocating())
	{
		return;
	}
	RelocateYaw = FRotator::ClampAxis(RelocateYaw + 45.f * Steps);
	if (IsRelocatingBarricade())
	{
		RefreshBarricadeTargets();
		RefreshOverlay();
		Changed();
	}
}

void UTurnBasedCombatSubsystem::RefreshBarricadeTargets()
{
	RelocateCells.Reset();
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	AActor* Barricade = RelocateTarget.Get();
	if (!UnitState || !Barricade || UnitState->AP < Balance.PushBarrelAPCost)
	{
		return;
	}
	for (int32 DX = -2; DX <= 2; ++DX)
	{
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			const FIntPoint Candidate = UnitState->GridPos + FIntPoint(DX, DY);
			if (Grid->IsValidCell(Candidate) && CanPlaceBarricadeAt(Barricade, Candidate, RelocateYaw))
			{
				RelocateCells.Add(Candidate, Balance.PushBarrelAPCost);
			}
		}
	}
}

TArray<FIntPoint> UTurnBasedCombatSubsystem::GetBarricadeCellsAt(const AActor* Barricade, const FIntPoint& Cell, float Yaw) const
{
	TArray<FIntPoint> Result;
	if (!Grid || !Grid->IsValidCell(Cell))
	{
		return Result;
	}
	// Godot: half extent = 0.48 of the collision box length (default 1.45 m).
	float HalfExtent = 145.f;
	if (const UBoxComponent* Box = Barricade ? Barricade->FindComponentByClass<UBoxComponent>() : nullptr)
	{
		HalfExtent = Box->GetScaledBoxExtent().X * 2.f * 0.48f;
	}
	const FVector Center = Grid->GridToWorld(Cell);
	const FRotator Rotation(0.f, Yaw, 0.f);
	for (const float Offset : { -HalfExtent, -HalfExtent * 0.5f, 0.f, HalfExtent * 0.5f, HalfExtent })
	{
		const FIntPoint Sample = Grid->WorldToGrid(Center + Rotation.RotateVector(FVector(Offset, 0.f, 0.f)));
		if (Grid->IsValidCell(Sample))
		{
			Result.AddUnique(Sample);
		}
	}
	return Result;
}

void UTurnBasedCombatSubsystem::RegisterBarricadeCells(AActor* Barricade)
{
	if (!IsValid(Barricade) || !Grid)
	{
		return;
	}
	float HalfExtent = 145.f;
	if (const UBoxComponent* Box = Barricade->FindComponentByClass<UBoxComponent>())
	{
		HalfExtent = Box->GetScaledBoxExtent().X * 2.f * 0.48f;
	}
	const FVector Center = Barricade->GetActorLocation();
	const FRotator Rotation(0.f, Barricade->GetActorRotation().Yaw, 0.f);
	for (const float Offset : { -HalfExtent, -HalfExtent * 0.5f, 0.f, HalfExtent * 0.5f, HalfExtent })
	{
		const FIntPoint Sample = Grid->WorldToGrid(Center + Rotation.RotateVector(FVector(Offset, 0.f, 0.f)));
		if (Grid->IsValidCell(Sample))
		{
			Grid->SetOccupant(Sample, Barricade, EGorkyOccupantType::Barricade);
		}
	}
}

bool UTurnBasedCombatSubsystem::CanPlaceBarricadeAt(const AActor* Barricade, const FIntPoint& Cell, float Yaw) const
{
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	if (!Grid || !Grid->IsValidCell(Cell) || !UnitState)
	{
		return false;
	}
	const TArray<FIntPoint> Cells = GetBarricadeCellsAt(Barricade, Cell, Yaw);
	if (Cells.IsEmpty())
	{
		return false;
	}
	const FIntPoint UnitPos = UnitState->GridPos;
	const bool bNearSoldier = Cells.ContainsByPredicate([&UnitPos](const FIntPoint& C)
	{
		return FMath::Max(FMath::Abs(UnitPos.X - C.X), FMath::Abs(UnitPos.Y - C.Y)) <= 1;
	});
	if (!bNearSoldier)
	{
		return false;
	}
	for (const FIntPoint& C : Cells)
	{
		const AActor* Occupant = Grid->GetOccupant(C);
		if (C == UnitPos || (Occupant && Occupant != Barricade) || (!Grid->IsCellWalkable(C) && Occupant != Barricade))
		{
			return false;
		}
	}
	return true;
}

bool UTurnBasedCombatSubsystem::RelocateBarricade(AActor* Barricade, const FIntPoint& Cell, float Yaw, int32 CustomAPCost)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* UnitState = States.Find(Unit);
	if (IsBusy() || !UnitState || !IsValid(Barricade))
	{
		return false;
	}
	if (!CanPlaceBarricadeAt(Barricade, Cell, Yaw))
	{
		Log(TEXT("⚠️ Cannot place the barricade here!"));
		return false;
	}
	const int32 TotalAP = CustomAPCost >= 0 ? CustomAPCost : Balance.PushBarrelAPCost;
	if (UnitState->AP < TotalAP)
	{
		Log(FString::Printf(TEXT("⚠️ Not enough AP to move the barricade (%d AP needed, %d AP left)!"), TotalAP, UnitState->AP));
		return false;
	}
	UnitState->AP -= TotalAP;
	for (int32 X = 0; X < Grid->GridSize.X; ++X)
	{
		for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
		{
			if (Grid->GetOccupant(FIntPoint(X, Y)) == Barricade)
			{
				Grid->ClearOccupant(FIntPoint(X, Y));
			}
		}
	}
	const FVector Target = Grid->GridToWorld(Cell);
	const FTransform Before = Barricade->GetActorTransform();
	Barricade->SetActorLocationAndRotation(FVector(Target.X, Target.Y, Barricade->GetActorLocation().Z), FRotator(0.f, Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	RegisterBarricadeCells(Barricade);
	if (bCinematics)
	{
		// Godot: the barricade glides to its new place and turn in 0.25 s (quad ease-out); the grid already has it.
		FObjectGlide& Glide = Glides.AddDefaulted_GetRef();
		Glide.Actor = Barricade;
		Glide.From = Before;
		Glide.To = Barricade->GetActorTransform();
		Barricade->SetActorTransform(Before, false, nullptr, ETeleportType::TeleportPhysics);
	}
	// The operative turns to face the barricade.
	UnitState->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Cell - UnitState->GridPos));
	AlignFacing(Unit, UnitState->Facing);
	Log(FString::Printf(TEXT("🧱 %s repositions the barricade (rotation %d°, %d AP spent)."), *NameOf(Unit),
		FMath::RoundToInt(FRotator::ClampAxis(Yaw)), TotalAP));
	RefreshOverlay();
	Changed();
	return true;
}

void UTurnBasedCombatSubsystem::CancelRelocate()
{
	if (!IsRelocating())
	{
		return;
	}
	RelocateTarget.Reset();
	RelocateOrigin = FIntPoint(-1, -1);
	RelocateCells.Reset();
	RefreshOverlay();
	Post(TEXT("TACTICS"), TEXT("Object relocation cancelled."));
	Changed();
}

bool UTurnBasedCombatSubsystem::TryPushAdjacentBarrel()
{
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	if (!IsActive() || Phase != ETurnPhase::Squad || !UnitState)
	{
		return false;
	}
	for (const TPair<TWeakObjectPtr<AActor>, FTurnUnitState>& Entry : States)
	{
		AActor* Barrel = Entry.Key.Get();
		const FIntPoint Diff = Entry.Value.GridPos - UnitState->GridPos;
		if (Barrel && Barrel->IsA<ABarrelActor>() && FMath::Abs(Diff.X) + FMath::Abs(Diff.Y) == 1)
		{
			Highlight(Barrel);
			return StartRelocate(Barrel);
		}
	}
	Log(TEXT("⚠️ No fuel barrel nearby!"));
	return false;
}

bool UTurnBasedCombatSubsystem::RelocateObject(const FIntPoint& ObjectCell, const FIntPoint& Target, int32 CustomAPCost)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* UnitState = States.Find(Unit);
	if (IsBusy() || !UnitState || !Grid || !Grid->IsValidCell(ObjectCell) || !Grid->IsValidCell(Target)
		|| ObjectCell == Target || !Grid->IsCellWalkable(Target))
	{
		return false;
	}
	AActor* Object = Grid->GetOccupant(ObjectCell);
	const EGorkyOccupantType Type = Grid->GetOccupantType(ObjectCell);
	if (!IsValid(Object) || (Type != EGorkyOccupantType::Barrel && Type != EGorkyOccupantType::Turret))
	{
		return false; // barricades move with their footprint and rotation (Godot relocate_barricade)
	}
	if (!IsUnitAdjacentToObject(Unit, Object))
	{
		Log(TEXT("⚠️ Stand right next to the object to relocate it!"));
		return false;
	}
	TArray<FIntPoint> Path = Grid->FindPath(ObjectCell, Target, 20);
	if (Path.IsEmpty())
	{
		const FIntPoint Step = Target - ObjectCell;
		if (Step.X * Step.X + Step.Y * Step.Y != 1)
		{
			Log(TEXT("⚠️ No walkable path to that cell!"));
			return false;
		}
		Path = { Target };
	}
	const int32 CostPerStep = Balance.PushBarrelAPCost;
	int32 TotalAP = CustomAPCost >= 0 ? CustomAPCost : Path.Num() * CostPerStep;
	if (TotalAP <= 0)
	{
		TotalAP = CostPerStep;
	}
	if (UnitState->AP < TotalAP)
	{
		Log(FString::Printf(TEXT("⚠️ Not enough AP to relocate the object (%d AP needed, %d AP left)!"), TotalAP, UnitState->AP));
		return false;
	}
	// The operative first steps onto the object's cell, then onto each cell the object leaves.
	TArray<FIntPoint> SoldierPath;
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		SoldierPath.Add(Index == 0 ? ObjectCell : Path[Index - 1]);
	}
	const FIntPoint SoldierFinal = SoldierPath.Last();
	const EGorkyFacing FinalFacing = FGorky17Utils::VectorToFacing(TurnStepDir(Target - SoldierFinal));
	const FIntPoint UnitStart = UnitState->GridPos;

	UnitState->AP -= TotalAP;
	Grid->ClearOccupant(UnitStart);
	Grid->ClearOccupant(ObjectCell);
	Grid->SetOccupant(Target, Object, Type);
	if (FTurnUnitState* ObjectState = States.Find(Object))
	{
		ObjectState->GridPos = Target;
	}
	Grid->SetOccupant(SoldierFinal, Unit, EGorkyOccupantType::Squad);
	UnitState->GridPos = SoldierFinal;
	UnitState->Facing = FinalFacing;

	// Mines under the operative's steps (Godot checks the soldier path).
	for (const FIntPoint& Step : SoldierPath)
	{
		if (Grid->GetOccupantType(Step) == EGorkyOccupantType::Mine)
		{
			DetonateMine(Step, Grid->GetOccupant(Step), Unit);
			if (!IsActive())
			{
				return true; // the mine ended the fight
			}
		}
	}

	bSquadUnitMoving = true;
	float StepDuration = SquadStepDuration;
	const float RiseDelay = PrepareSquadWalk(Unit, StepDuration);
	RefreshOverlay();
	StartMoverAfter(RiseDelay, Object, ObjectCell, Path, StepDuration, nullptr, /*bFaceSteps*/ false);
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	StartMoverAfter(RiseDelay, Unit, UnitStart, SoldierPath, StepDuration, [this, WeakUnit, FinalFacing]()
	{
		bSquadUnitMoving = false;
		if (AOperativeCharacter* Moved = WeakUnit.Get())
		{
			AlignFacing(Moved, FinalFacing);
		}
		if (IsActive())
		{
			RefreshOverlay();
			Changed();
		}
	});
	const TCHAR* ObjectName = Type == EGorkyOccupantType::Barrel ? TEXT("the barrel") : TEXT("the turret");
	Log(FString::Printf(TEXT("📦 %s moves %s to a new position (%d AP spent)."), *NameOf(Unit), ObjectName, TotalAP));
	Changed();
	return true;
}

// --- Deployables on the grid ----------------------------------------------------------------------------------------

FTurnDeployCheck UTurnBasedCombatSubsystem::CanPlaceDeployable(EDeployableType Type, const FIntPoint& Cell, float Yaw) const
{
	FTurnDeployCheck Check;
	if (!Grid || !Grid->IsValidCell(Cell))
	{
		Check.Reason = TEXT("Cell outside the tactical grid");
		return Check;
	}
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	if (!UnitState)
	{
		Check.Reason = TEXT("No active operative");
		return Check;
	}
	const int32 DeployCost = Type == EDeployableType::Mine ? 2 : 3;
	Check.APCost = DeployCost;
	if (UnitState->AP < DeployCost)
	{
		Check.Reason = FString::Printf(TEXT("Not enough AP to deploy (%d needed, %d left)"), DeployCost, UnitState->AP);
		return Check;
	}
	const FIntPoint UnitPos = UnitState->GridPos;

	// 1. The object's cells must be free.
	TArray<FIntPoint> Cells;
	if (Type == EDeployableType::Barricade)
	{
		Cells = GetBarricadeCellsAt(nullptr, Cell, Yaw);
		if (Cells.IsEmpty())
		{
			Check.Reason = TEXT("Invalid barricade position");
			return Check;
		}
		for (const FIntPoint& C : Cells)
		{
			if (!Grid->IsValidCell(C))
			{
				Check.Reason = TEXT("Barricade extends past the grid");
				return Check;
			}
			if (C == UnitPos)
			{
				Check.Reason = TEXT("Barricade overlaps the operative");
				return Check;
			}
			if (Grid->GetOccupant(C))
			{
				Check.Reason = FString::Printf(TEXT("Cell (%d, %d) is occupied"), C.X, C.Y);
				return Check;
			}
			if (!Grid->IsCellWalkable(C))
			{
				Check.Reason = FString::Printf(TEXT("Obstacle on cell (%d, %d)"), C.X, C.Y);
				return Check;
			}
		}
	}
	else
	{
		Cells = { Cell };
		if (!Grid->IsCellWalkable(Cell))
		{
			Check.Reason = TEXT("Cell blocked by an obstacle");
			return Check;
		}
		if (Grid->GetOccupant(Cell))
		{
			Check.Reason = TEXT("Cell already occupied");
			return Check;
		}
	}

	// 2. Already next to it: set up in place.
	auto Chebyshev = [](const FIntPoint& A, const FIntPoint& B) { return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)); };
	for (const FIntPoint& C : Cells)
	{
		if (Chebyshev(UnitPos, C) <= 1)
		{
			Check.bCanPlace = true;
			Check.StandCell = UnitPos;
			Check.Reason = TEXT("OK");
			return Check;
		}
	}

	// 3. Otherwise walk to the cheapest free cell next to it.
	TArray<FIntPoint> Candidates;
	for (const FIntPoint& C : Cells)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				const FIntPoint Near = C + FIntPoint(DX, DY);
				if ((DX == 0 && DY == 0) || !Grid->IsValidCell(Near) || Cells.Contains(Near) || !Grid->IsCellWalkable(Near))
				{
					continue;
				}
				const AActor* Occupant = Grid->GetOccupant(Near);
				if (Occupant && Occupant != GetActiveUnit())
				{
					continue;
				}
				Candidates.AddUnique(Near);
			}
		}
	}
	if (Candidates.IsEmpty())
	{
		Check.Reason = TEXT("No free cell next to the object to deploy from");
		return Check;
	}
	// Crouched / prone the walk to the stand cell costs double (user decision 2026-10-04): the budget in steps.
	const int32 WalkMultiplier = TurnBasedRules::MoveCostMultiplier(UnitState->Stance, Balance);
	const int32 MaxWalkAP = (UnitState->AP - DeployCost) / WalkMultiplier;
	const TMap<FIntPoint, int32> Reach = Grid->GetReachableCells(UnitPos, MaxWalkAP);
	Candidates.StableSort([&UnitPos, &Chebyshev](const FIntPoint& A, const FIntPoint& B) { return Chebyshev(A, UnitPos) < Chebyshev(B, UnitPos); });
	int32 BestCost = MAX_int32;
	bool bFoundStand = false;
	for (const FIntPoint& Candidate : Candidates)
	{
		const int32* Cost = Reach.Find(Candidate);
		if (Cost && *Cost < BestCost)
		{
			TArray<FIntPoint> Path = Grid->FindPath(UnitPos, Candidate, MaxWalkAP);
			if (!Path.IsEmpty() || Candidate == UnitPos) // already standing there: no walk
			{
				BestCost = *Cost;
				Check.StandCell = Candidate;
				Check.Path = MoveTemp(Path);
				bFoundStand = true;
			}
		}
	}
	if (!bFoundStand)
	{
		Check.Reason = TEXT("Too far to walk and deploy this turn");
		return Check;
	}
	Check.bCanPlace = true;
	Check.APCost = BestCost * WalkMultiplier + DeployCost;
	Check.Reason = TEXT("OK");
	return Check;
}

void UTurnBasedCombatSubsystem::PlayWorkingDevice(AActor* Unit)
{
	// Godot play_action_animation("working_device", 0.8) while the item is assembled.
	const ACharacter* Character = Cast<ACharacter>(Unit);
	if (UOperativeAnimInstance* Anim = Character && Character->GetMesh() ? Cast<UOperativeAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->PlayWorkingDevice(0.8f);
	}
}

void UTurnBasedCombatSubsystem::StartGrowIn(AActor* Object)
{
	// Godot _handle_tactical_deployable_placement: after 0.2 s the item grows from 0.05 to full size in 0.45 s (back ease-out).
	if (!bCinematics || !IsValid(Object))
	{
		return;
	}
	FObjectGlide& Glide = Glides.AddDefaulted_GetRef();
	Glide.Actor = Object;
	Glide.To = Object->GetActorTransform();
	Glide.From = Glide.To;
	Glide.From.SetScale3D(Glide.To.GetScale3D() * 0.05f);
	Glide.Duration = 0.45f;
	Glide.Delay = 0.2f;
	Glide.bBackEase = true;
	Object->SetActorScale3D(Glide.From.GetScale3D());
}

void UTurnBasedCombatSubsystem::RegisterDeployable(EDeployableType Type, AActor* Object, const FIntPoint& Cell, float Yaw)
{
	if (!IsValid(Object) || !Grid)
	{
		return;
	}
	const FVector World = Grid->GridToWorld(Cell);
	Object->SetActorLocation(FVector(World.X, World.Y, Object->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	switch (Type)
	{
	case EDeployableType::Turret:
	{
		Turrets.AddUnique(Object);
		Grid->SetOccupant(Cell, Object, EGorkyOccupantType::Turret);
		FTurnUnitState& State = States.Add(Object);
		State.Actor = Object;
		State.GridPos = Cell;
		break;
	}
	case EDeployableType::Barricade:
		Object->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		RegisterBarricadeCells(Object);
		break;
	case EDeployableType::Mine:
		Grid->SetOccupant(Cell, Object, EGorkyOccupantType::Mine);
		break;
	}
	// Frozen like everything else off the grid's turns (Godot set_physics_process(false)).
	if (Object->IsActorTickEnabled())
	{
		Object->SetActorTickEnabled(false);
		FrozenActors.Add(Object);
	}
	Object->SetActorHiddenInGame(false);
}

bool UTurnBasedCombatSubsystem::DeployObject(EDeployableType Type, const FIntPoint& Cell, float Yaw, AActor* Spawned)
{
	const FTurnDeployCheck Check = CanPlaceDeployable(Type, Cell, Yaw);
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* UnitState = States.Find(Unit);
	if (!Check.bCanPlace || !UnitState || IsBusy() || !IsValid(Spawned))
	{
		return false;
	}
	const FString Name = Type == EDeployableType::Turret ? TEXT("Turret") : (Type == EDeployableType::Barricade ? TEXT("Barricade") : TEXT("Mine"));
	const int32 TotalAP = Check.APCost;
	auto FaceTarget = [this, Unit, Cell](FTurnUnitState& State)
	{
		const FIntPoint Dir = Cell - State.GridPos;
		if (Dir != FIntPoint::ZeroValue)
		{
			State.Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Dir));
			AlignFacing(Unit, State.Facing);
		}
	};

	// Case 1: already next to the spot.
	if (Check.Path.IsEmpty() || Check.StandCell == UnitState->GridPos)
	{
		UnitState->AP -= TotalAP;
		FaceTarget(*UnitState);
		RegisterDeployable(Type, Spawned, Cell, Yaw);
		StartGrowIn(Spawned);
		PlayWorkingDevice(Unit);
		Log(FString::Printf(TEXT("🛠️ %s assembles and deploys: %s (-%d AP)."), *NameOf(Unit), *Name, TotalAP));
		RefreshOverlay();
		Changed();
		return true;
	}

	// Case 2: walk up first (a mine on the way stops the walk and the object is lost).
	const FIntPoint From = UnitState->GridPos;
	Grid->ClearOccupant(From);
	TArray<FIntPoint> Actual;
	FIntPoint MineCell(-999, -999);
	AActor* Mine = nullptr;
	for (const FIntPoint& Step : Check.Path)
	{
		Actual.Add(Step);
		if (Grid->GetOccupantType(Step) == EGorkyOccupantType::Mine)
		{
			MineCell = Step;
			Mine = Grid->GetOccupant(Step);
			break;
		}
	}
	Spawned->SetActorHiddenInGame(true);
	bSquadUnitMoving = true;
	RefreshOverlay();
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	TWeakObjectPtr<AActor> WeakSpawned(Spawned);
	TWeakObjectPtr<AActor> WeakMine(Mine);
	const FIntPoint StandCell = Check.StandCell;
	float StepDuration = SquadStepDuration;
	const float RiseDelay = PrepareSquadWalk(Unit, StepDuration);
	StartMoverAfter(RiseDelay, Unit, From, Actual, StepDuration, [this, WeakUnit, WeakSpawned, WeakMine, MineCell, StandCell, Cell, Yaw, Type, Name, TotalAP, FaceTarget]()
	{
		AOperativeCharacter* Moved = WeakUnit.Get();
		FTurnUnitState* State = States.Find(Moved);
		if (!Moved || !State)
		{
			bSquadUnitMoving = false;
			return;
		}
		if (MineCell.X != -999)
		{
			bSquadUnitMoving = false;
			if (AActor* Lost = WeakSpawned.Get())
			{
				Lost->Destroy();
			}
			State->GridPos = MineCell;
			State->AP = 0;
			DetonateMine(MineCell, WeakMine.Get(), Moved);
			if (IsActive() && !IsDead(Moved))
			{
				Grid->SetOccupant(MineCell, Moved, EGorkyOccupantType::Squad);
			}
			After(0.75f, [this]() { EndCurrentUnitTurn(); });
			return;
		}
		State->GridPos = StandCell;
		Grid->SetOccupant(StandCell, Moved, EGorkyOccupantType::Squad);
		FaceTarget(*State);
		RegisterDeployable(Type, WeakSpawned.Get(), Cell, Yaw);
		StartGrowIn(WeakSpawned.Get());
		PlayWorkingDevice(Moved);
		State->AP -= TotalAP;
		Log(FString::Printf(TEXT("🛠️ %s walks up and deploys: %s (-%d AP)."), *NameOf(Moved), *Name, TotalAP));
		Changed();
		// Godot: 0.85 s of assembly before the next order.
		After(0.85f, [this]()
		{
			bSquadUnitMoving = false;
			RefreshOverlay();
			Changed();
		});
	});
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::HandleDeployPlacement(EDeployableType Type, const FVector& WorldPoint, float Yaw)
{
	if (!IsActive() || Phase != ETurnPhase::Squad || IsBusy() || !Grid)
	{
		return false;
	}
	const FVector Local = WorldPoint - Grid->OriginWorld;
	const bool bOnGrid = Local.X >= 0.f && Local.Y >= 0.f && Local.X < TurnGridCells * TurnCellSize && Local.Y < TurnGridCells * TurnCellSize;
	if (!bOnGrid)
	{
		Post(TEXT("ENGINEERING"), TEXT("⚠️ Deploy point is outside the tactical zone!"));
		return false;
	}
	const FIntPoint Cell = Grid->WorldToGrid(WorldPoint);
	AOperativeCharacter* Unit = GetActiveUnit();
	if (!Unit)
	{
		Post(TEXT("ENGINEERING"), TEXT("⚠️ No active operative to deploy the object!"));
		return false;
	}
	// Godot: a squad mate hands the item over when the active operative has none.
	if (Unit->GetDeployableCount(Type) <= 0)
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : Squad)
		{
			AOperativeCharacter* Carrier = Weak.Get();
			if (Carrier && Carrier != Unit && Carrier->GetDeployableCount(Type) > 0)
			{
				Carrier->AddDeployable(Type, -1);
				Unit->AddDeployable(Type, 1);
				break;
			}
		}
	}
	if (Unit->GetDeployableCount(Type) <= 0)
	{
		Post(Unit->DisplayName.ToString(), FString::Printf(TEXT("⚠️ The squad has none left: %s!"), *URelocationSubsystem::GetDeployableName(Type).ToString()));
		return true; // placement ends (Godot _cancel_placement_mode)
	}
	const FTurnDeployCheck Check = CanPlaceDeployable(Type, Cell, Yaw);
	if (!Check.bCanPlace)
	{
		Post(TEXT("ENGINEERING"), FString::Printf(TEXT("⚠️ %s!"), *Check.Reason));
		return false;
	}
	const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	const TSubclassOf<ADeployableActor> Class = Relocation ? Relocation->GetDeployableClass(Type) : nullptr;
	AActor* Spawned = nullptr;
	if (Class)
	{
		const float HalfHeight = Class->GetDefaultObject<ADeployableActor>()->Box->GetUnscaledBoxExtent().Z;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawned = GetWorld()->SpawnActor<ADeployableActor>(Class, Grid->GridToWorld(Cell) + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f), Params);
	}
	if (!Spawned)
	{
		Post(TEXT("ENGINEERING"), TEXT("⚠️ Failed to spawn the object!"));
		return false;
	}
	if (AProximityMineActor* PlacedMine = Cast<AProximityMineActor>(Spawned))
	{
		PlacedMine->SetPlacedBySquad();
	}
	if (DeployObject(Type, Cell, Yaw, Spawned))
	{
		Unit->AddDeployable(Type, -1);
		Highlight(Spawned);
		return true;
	}
	Spawned->Destroy();
	Post(TEXT("ENGINEERING"), TEXT("⚠️ Failed to place the object on the tactical grid!"));
	return false;
}

// --- Turrets and enemies ------------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::EndSquadPhase()
{
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
	{
		if (AOperativeCharacter* Unit = Member.Get())
		{
			Unit->EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("squad phase over"));
		}
	}
	RelocateTarget.Reset();
	RelocateCells.Reset();
	if (Overlay)
	{
		Overlay->ClearLayer(ETurnOverlayLayer::Reachable);
		Overlay->ClearLayer(ETurnOverlayLayer::Attack);
		Overlay->ClearLayer(ETurnOverlayLayer::Active);
	}
	if (IsActive() && Grid)
	{
		UpdateExposedZones();
	}
	ExecuteTurretPhase();
}

void UTurnBasedCombatSubsystem::UpdateExposedZones()
{
	bool Covered[FExposedZones::NumQuadrants] = { false, false, false, false };
	for (const TWeakObjectPtr<AOperativeCharacter>& Weak : Squad)
	{
		const FTurnUnitState* State = GetUnitState(Weak.Get());
		if (State && !IsDead(Weak.Get()))
		{
			Covered[FExposedZones::GetQuadrant(State->GridPos, Grid->GridSize)] = true;
		}
	}
	for (const TWeakObjectPtr<AActor>& Weak : Turrets)
	{
		const ATurretActor* Turret = Cast<ATurretActor>(Weak.Get());
		const FTurnUnitState* State = GetUnitState(Turret);
		if (Turret && State && !Turret->IsBroken() && GetHealth(Turret) > 0.f)
		{
			Covered[FExposedZones::GetQuadrant(State->GridPos, Grid->GridSize)] = true;
		}
	}
	int32 Alive = 0;
	for (const TWeakObjectPtr<AActor>& Weak : Enemies)
	{
		Alive += Weak.IsValid() && !IsDead(Weak.Get()) ? 1 : 0;
	}

	TArray<TPair<AActor*, FIntPoint>> Arrived;
	UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
	const FExposedZones::FTurnResult Result = Zones.EndSquadTurn(Covered, Alive,
		[]() { return FMath::RandRange(1, 2); },
		[this, Waves, &Arrived](int32 Quadrant, int32 Count)
		{
			TArray<EEnemyArchetype> Pool = Zones.GetPool();
			if (Pool.IsEmpty())
			{
				Pool.Add(EEnemyArchetype::FrostHound);
			}
			// Godot: variety — the non-elite types are shuffled.
			for (int32 Index = Pool.Num() - 1; Index > 0; --Index)
			{
				Pool.Swap(Index, FMath::RandRange(0, Index));
			}
			const TArray<FIntPoint> Cells = FindSpawnCells(Quadrant, Count);
			for (int32 Index = 0; Waves && Index < Cells.Num(); ++Index)
			{
				if (AEnemyCharacter* Enemy = Waves->SpawnEnemy(Pool[Index % Pool.Num()], Grid->GridToWorld(Cells[Index]) + FVector(0.f, 0.f, 100.f)))
				{
					Arrived.Emplace(Enemy, Cells[Index]);
				}
			}
			return Arrived.Num();
		});

	TArray<int32> Turns;
	for (int32 Quadrant = 0; Quadrant < FExposedZones::NumQuadrants; ++Quadrant)
	{
		Turns.Add(Result.Turns[Quadrant]);
		const FString Name = FExposedZones::GetQuadrantName(Quadrant);
		if (Result.Turns[Quadrant] == 1)
		{
			Log(FString::Printf(TEXT("⚠️ Warning: sector [%s] is exposed (1 turn uncovered)!"), *Name));
		}
		else if (Result.Turns[Quadrant] == 2)
		{
			Log(FString::Printf(TEXT("🚨 DANGER: sector [%s] exposed for 2 turns! Enemy breach possible next turn!"), *Name));
		}
	}
	if (Overlay)
	{
		Overlay->SetExposedZones(Turns);
	}
	for (const TPair<AActor*, FIntPoint>& Entry : Arrived)
	{
		RegisterReinforcement(Entry.Key, Entry.Value, FExposedZones::GetQuadrantName(Result.BreachQuadrant));
	}
}

TArray<FIntPoint> UTurnBasedCombatSubsystem::FindSpawnCells(int32 Quadrant, int32 Count) const
{
	const FIntRect Rect = FExposedZones::GetQuadrantRect(Quadrant, Grid->GridSize);
	TArray<FIntPoint> Edge, Interior;
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!Grid->IsCellWalkable(Cell))
			{
				continue;
			}
			const bool bOuter = X == 0 || Y == 0 || X == Grid->GridSize.X - 1 || Y == Grid->GridSize.Y - 1;
			(bOuter ? Edge : Interior).Add(Cell);
		}
	}
	auto Shuffle = [](TArray<FIntPoint>& Cells)
	{
		for (int32 Index = Cells.Num() - 1; Index > 0; --Index)
		{
			Cells.Swap(Index, FMath::RandRange(0, Index));
		}
	};
	Shuffle(Edge);
	Shuffle(Interior);
	Edge.Append(Interior);
	Edge.SetNum(FMath::Min(Count, Edge.Num()));
	return Edge;
}

void UTurnBasedCombatSubsystem::RegisterReinforcement(AActor* Enemy, const FIntPoint& Cell, const FString& QuadrantName)
{
	if (!IsValid(Enemy) || !Grid)
	{
		return;
	}
	Enemies.AddUnique(Enemy);
	Grid->SetOccupant(Cell, Enemy, EGorkyOccupantType::Enemy);
	PlaceOnCell(Enemy, Cell);
	// Frozen outside its turns like the enemies the fight started with.
	if (APawn* Pawn = Cast<APawn>(Enemy); Pawn && Pawn->GetController())
	{
		Pawn->GetController()->StopMovement();
	}
	if (Enemy->IsActorTickEnabled())
	{
		Enemy->SetActorTickEnabled(false);
		FrozenActors.Add(Enemy);
	}
	if (AEnemyCharacter* HeldEnemy = Cast<AEnemyCharacter>(Enemy))
	{
		HeldEnemy->SetTurnBasedHeld(true);
		HeldAnchors.Add(HeldEnemy, HeldEnemy->GetActorLocation());
	}
	FTurnUnitState& State = States.Add(Enemy);
	State.Actor = Enemy;
	State.GridPos = Cell;
	const FEnemyTurnProfile TurnProfile = TurnProfileOf(Enemy);
	State.MaxAP = State.AP = EnemyTurnRules::MaxAP(TurnProfile, Balance.EnemyMaxAP);
	State.Armor = 2.f;
	State.BaseDamage = EnemyTurnRules::BaseDamage(TurnProfile, Balance.EnemyBaseDamage);
	State.Facing = EGorkyFacing::North;
	AlignFacing(Enemy, State.Facing);
	Log(FString::Printf(TEXT("🚨 BREACH! Exposed sector [%s] was left uncovered! Reinforcement arrived: %s!"), *QuadrantName, *NameOf(Enemy)));
}

void UTurnBasedCombatSubsystem::ExecuteTurretPhase()
{
	Phase = ETurnPhase::Turrets;
	Changed();
	TurretQueue.Reset();
	for (const TWeakObjectPtr<AActor>& Weak : Turrets)
	{
		const ATurretActor* Turret = Cast<ATurretActor>(Weak.Get());
		if (Turret && Turret->IsPowered() && !Turret->IsBroken())
		{
			TurretQueue.Add(Weak);
		}
	}
	ProcessNextTurret();
}

void UTurnBasedCombatSubsystem::ProcessNextTurret()
{
	if (CheckBattleEnd())
	{
		return;
	}
	if (TurretQueue.IsEmpty())
	{
		ExecuteEnemyPhase();
		return;
	}
	AActor* Turret = TurretQueue[0].Get();
	TurretQueue.RemoveAt(0);
	if (!Turret)
	{
		ProcessNextTurret();
		return;
	}
	// Nearest living enemy within 45 m (Godot _process_next_turret_shot).
	AActor* Best = nullptr;
	float BestDistance = TurnTurretSupportDistance;
	for (const TWeakObjectPtr<AActor>& Enemy : Enemies)
	{
		if (Enemy.IsValid() && !IsDead(Enemy.Get()))
		{
			const float Distance = FVector::Dist(Turret->GetActorLocation(), Enemy->GetActorLocation());
			if (Distance <= BestDistance)
			{
				BestDistance = Distance;
				Best = Enemy.Get();
			}
		}
	}
	if (!Best)
	{
		ProcessNextTurret();
		return;
	}
	const FTurnUnitState* EnemyState = GetUnitState(Best);
	const int32 Cells = TurnBasedRules::CellDistance(Grid->WorldToGrid(Turret->GetActorLocation()), EnemyState ? EnemyState->GridPos : FIntPoint::ZeroValue);
	const float Chance = TurnBasedRules::CalculateTurretHitChance(Cells);
	const float Roll = FMath::FRand();
	const bool bHit = bGuaranteeAllHits || Roll <= Chance;
	ATacticalCameraPawn* Camera = GetCamera();
	if (!bCinematics || !Camera)
	{
		ResolveTurretShot(Turret, Best, bHit, Chance, Roll);
		After(0.55f, [this]() { ProcessNextTurret(); });
		return;
	}
	// Godot _on_gorky17_turret_shot_requested: the camera frames turret and target (0.4 s), the head turns (0.25 s),
	// the volley flies (0.15 s), the hit / miss reads (0.65 s), the camera glides to the squad overview (0.85 s).
	bDramaticShotActive = true;
	Camera->SetDramaticShotActive(true);
	Highlight(Best);
	Camera->DramaticActionFocus(Turret, Best, 0.4f);
	TWeakObjectPtr<AActor> WeakTurret(Turret), WeakTarget(Best);
	After(0.4f, [this, WeakTurret, WeakTarget, bHit, Chance, Roll]()
	{
		if (ATurretActor* TurretActor = Cast<ATurretActor>(WeakTurret.Get()); TurretActor && WeakTarget.IsValid() && TurretActor->Head)
		{
			const FVector Aim = WeakTarget->GetActorLocation() - TurretActor->GetActorLocation();
			TurretActor->Head->SetWorldRotation(FRotator(0.f, Aim.Rotation().Yaw, 0.f));
		}
		After(0.25f, [this, WeakTurret, WeakTarget, bHit, Chance, Roll]()
		{
			ResolveTurretShot(WeakTurret.Get(), WeakTarget.Get(), bHit, Chance, Roll);
			After(0.65f, [this]()
			{
				if (ATacticalCameraPawn* Cam = GetCamera())
				{
					Cam->SmoothFocusOnPosition(GetSquadOverviewCenter(), 0.85f, Cam->Config.DistanceCombat);
				}
				After(0.85f, [this]()
				{
					bDramaticShotActive = false;
					if (ATacticalCameraPawn* Cam = GetCamera())
					{
						Cam->SetDramaticShotActive(false);
					}
					ProcessNextTurret();
				});
			});
		});
	});
}

void UTurnBasedCombatSubsystem::ResolveTurretShot(AActor* Turret, AActor* Target, bool bHit, float Chance, float Roll)
{
	if (!Turret || !Target || !States.Contains(Target))
	{
		return;
	}
	// Godot _spawn_muzzle_tracer: at the target, or up to 1.6 m beside it on a miss.
	FVector End = Target->GetActorLocation();
	if (!bHit)
	{
		FVector Miss(FMath::FRandRange(-160.f, 160.f), FMath::FRandRange(-160.f, 160.f), FMath::FRandRange(-30.f, 60.f));
		if (Miss.SizeSquared() < 60.f * 100.f)
		{
			Miss = Miss.GetSafeNormal() * 120.f;
		}
		End += Miss;
	}
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTurretTracer(Turret->GetActorLocation() + FVector(0.f, 0.f, 70.f), End);
	}
	ShakeCamera(TEXT("turret")); // Godot _on_gorky17_turret_shot_requested
	if (bHit)
	{
		const int32 Damage = FMath::RoundToInt(Balance.TurretDamage);
		ApplyEnemyHit(Target, Damage, TEXT("Turret"));
		Log(FString::Printf(TEXT("🔫 Turret fires a burst at %s (-%d HP)! [Accuracy: %d%%]"), *NameOf(Target), Damage, FMath::RoundToInt(Chance * 100.f)));
		if (IsDead(Target))
		{
			OnEnemyKilled(Target, States[Target].GridPos);
		}
	}
	else
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(Target, TEXT("MISS!"), FLinearColor(0.85f, 0.85f, 0.85f));
		Log(FString::Printf(TEXT("❌ Turret misses %s! Chance: %d%% (rolled: %d%%)"), *NameOf(Target), FMath::RoundToInt(Chance * 100.f),
			FMath::RoundToInt(Roll * 100.f)));
	}
}

void UTurnBasedCombatSubsystem::ExecuteEnemyPhase()
{
	Phase = ETurnPhase::Enemies;
	EnemyQueue.Reset();
	EnemyPhaseTargets.Reset();
	for (const TWeakObjectPtr<AActor>& Enemy : Enemies)
	{
		if (FTurnUnitState* State = States.Find(Enemy.Get()))
		{
			State->AP = State->MaxAP;
			EnemyQueue.Add(Enemy);
		}
	}
	Changed();
	ProcessNextEnemy();
}

void UTurnBasedCombatSubsystem::ProcessNextEnemy()
{
	if (!IsActive())
	{
		return;
	}
	if (EnemyQueue.IsEmpty())
	{
		if (CheckBattleEnd())
		{
			return;
		}
		// New round: the squad moves again from the first operative.
		++Round;
		ActiveIndex = 0;
		StartPlayerTurn();
		return;
	}
	AActor* Enemy = EnemyQueue[0].Get();
	EnemyQueue.RemoveAt(0);
	if (!Enemy || !States.Contains(Enemy) || IsDead(Enemy))
	{
		ProcessNextEnemy();
		return;
	}
	// Sprint 06-C: contact barricades hurt an adjacent enemy once per its turn (their real-time timer stands still).
	for (TActorIterator<ABarricadeActor> It(GetWorld()); It; ++It)
	{
		if (It->ApplyTurnContact(Enemy))
		{
			++ContactHitsThisFight;
		}
	}
	if (IsDead(Enemy))
	{
		ProcessNextEnemy();
		return;
	}
	ExecuteEnemyTurn(Enemy);
}

TSet<FIntPoint> UTurnBasedCombatSubsystem::GetFearCells() const
{
	// Godot _get_all_active_fear_cells: 5 x 5 around every burning barrel.
	TSet<FIntPoint> Cells;
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Entry : BurningBarrels)
	{
		const FTurnUnitState* State = States.Find(Entry.Key);
		if (!State || !Grid)
		{
			continue;
		}
		for (int32 DX = -2; DX <= 2; ++DX)
		{
			for (int32 DY = -2; DY <= 2; ++DY)
			{
				if (Grid->IsValidCell(State->GridPos + FIntPoint(DX, DY)))
				{
					Cells.Add(State->GridPos + FIntPoint(DX, DY));
				}
			}
		}
	}
	return Cells;
}

void UTurnBasedCombatSubsystem::ExecuteEnemyTurn(AActor* Enemy)
{
	FTurnUnitState* State = States.Find(Enemy);
	// Sprint 14: a knocked-down enemy spends its turn getting up.
	if (AEnemyCharacter* Knocked = Cast<AEnemyCharacter>(Enemy); Knocked && Knocked->IsKnockedDown() && Knocked->KnockdownComponent)
	{
		const float GetUp = Knocked->KnockdownComponent->GetUpForTurn();
		Log(FString::Printf(TEXT("%s spends its turn getting up."), *NameOf(Enemy)));
		FinishEnemyTurn(FMath::Max(GetUp, 0.5f));
		return;
	}
	// Godot _on_gorky17_turn_changed (enemy): the camera glides to it.
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->SmoothFocusOnTarget(Enemy, 0.4f, EnemyFocusDistance);
	}
	const FIntPoint Pos = State->GridPos;
	const AEnemyCharacter* EnemyCharacter = Cast<AEnemyCharacter>(Enemy);
	const bool bFearsFire = EnemyCharacter && EnemyCharacter->DoesFearFire();
	const TSet<FIntPoint> Fear = bFearsFire && !BurningBarrels.IsEmpty() ? GetFearCells() : TSet<FIntPoint>();

	if (Overlay)
	{
		TArray<FIntPoint> Reach;
		Grid->GetReachableCells(Pos, State->AP, Fear).GetKeys(Reach);
		Overlay->SetCells(ETurnOverlayLayer::EnemyReach, Reach);
	}

	const FEnemyTurnProfile Profile = TurnProfileOf(Enemy);
	FIntPoint TargetPos = FIntPoint::ZeroValue;
	AActor* Target = ChooseEnemyTarget(Enemy, Profile.bRanged, Fear, TargetPos);
	if (!Target)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	if (Profile.bRanged)
	{
		if (Overlay)
		{
			Overlay->SetCells(ETurnOverlayLayer::Warning, { TargetPos });
		}
		ExecuteRangedEnemyTurn(Enemy, Target, TargetPos, Fear);
		return;
	}

	const bool bOrthogonal = TurnManhattan(Pos, TargetPos) == 1;
	if (Overlay)
	{
		Overlay->SetCells(ETurnOverlayLayer::Warning, { TargetPos }); // Godot show_target_warning
	}

	// The orthogonal cell to bite from: the archetype weighs the back / flank arc against the walk (EnemyTurnRules).
	TArray<FIntPoint> ChosenPath;
	bool bHasChoice = false;
	if (const FTurnUnitState* TargetState = GetUnitState(Target))
	{
		TArray<FEnemyMeleeCell> Cells;
		TArray<TArray<FIntPoint>> Paths;
		const FIntPoint Offsets[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (const FIntPoint& Offset : Offsets)
		{
			const FIntPoint Cell = TargetPos + Offset;
			TArray<FIntPoint> Path;
			int32 Cost = 0;
			if (Cell != Pos)
			{
				if (!Grid->IsValidCell(Cell) || !Grid->IsCellWalkable(Cell) || Fear.Contains(Cell))
				{
					continue;
				}
				Path = Grid->FindPath(Pos, Cell, State->AP + 8, Fear);
				if (Path.IsEmpty())
				{
					continue;
				}
				FIntPoint Current = Pos;
				for (const FIntPoint& Step : Path)
				{
					Cost += (Step.X != Current.X && Step.Y != Current.Y) ? Grid->DiagonalAPCost : 1;
					Current = Step;
				}
			}
			FEnemyMeleeCell& Candidate = Cells.AddDefaulted_GetRef();
			Candidate.Cell = Cell;
			Candidate.PathCost = Cost;
			Candidate.ArcMultiplier = FGorky17Utils::CalculateAttackArc(Cell, TargetPos, FGorky17Utils::FacingToVector(TargetState->Facing)).DamageMultiplier;
			Paths.Add(Path);
		}
		const int32 Chosen = EnemyTurnRules::ChooseMeleeCell(Profile, Cells, State->AP);
		if (Chosen != INDEX_NONE)
		{
			bHasChoice = true;
			ChosenPath = Paths[Chosen];
		}
	}

	bool bStoppedByFear = false;
	if ((bHasChoice ? !ChosenPath.IsEmpty() : !bOrthogonal) && State->AP > 0)
	{
		TArray<FIntPoint> Path = bHasChoice ? ChosenPath : Grid->FindPathToAdjacent(Pos, TargetPos, State->AP, true, Fear);
		if (Path.IsEmpty() && !Fear.IsEmpty())
		{
			Path = Grid->FindPathClosestOutsideForbidden(Pos, TargetPos, State->AP, Fear);
			bStoppedByFear = true;
		}
		// Only the steps the AP pays for; the walk stops on a mine.
		TArray<FIntPoint> Actual;
		int32 Spent = 0;
		FIntPoint Current = Pos;
		bool bMine = false;
		for (const FIntPoint& Step : Path)
		{
			const int32 Cost = (Step.X != Current.X && Step.Y != Current.Y) ? Grid->DiagonalAPCost : 1;
			if (Spent + Cost > State->AP)
			{
				break;
			}
			Actual.Add(Step);
			Spent += Cost;
			Current = Step;
			if (Grid->GetOccupantType(Step) == EGorkyOccupantType::Mine)
			{
				bMine = true;
				break;
			}
		}
		if (!Actual.IsEmpty())
		{
			const FIntPoint Destination = Actual.Last();
			AActor* MineActor = bMine ? Grid->GetOccupant(Destination) : nullptr;
			Grid->ClearOccupant(Pos);
			if (!bMine)
			{
				Grid->SetOccupant(Destination, Enemy, EGorkyOccupantType::Enemy);
			}
			State->GridPos = Destination;
			State->AP -= Spent;
			State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Destination - (Actual.Num() > 1 ? Actual[Actual.Num() - 2] : Pos)));
			TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakTarget(Target), WeakMine(MineActor);
			FocusMovingEnemy(Enemy);
			StartMover(Enemy, Pos, Actual, EnemyStepDuration, nullptr,
				[this, WeakEnemy, WeakTarget, WeakMine, bMine, bStoppedByFear, Destination, TargetPos]()
			{
				AActor* Moved = WeakEnemy.Get();
				if (!Moved || !States.Contains(Moved))
				{
					FinishEnemyTurn(0.45f);
					return;
				}
				AlignFacing(Moved, States[Moved].Facing);
				if (bMine)
				{
					DetonateMine(Destination, WeakMine.Get(), Moved);
					if (FTurnUnitState* MovedState = States.Find(Moved))
					{
						MovedState->AP = 0;
					}
					FinishEnemyTurn(1.2f);
					return;
				}
				if (bStoppedByFear)
				{
					Log(FString::Printf(TEXT("🐺 %s reaches the edge of the flames but fears the fire and snarls!"), *NameOf(Moved)));
					FinishEnemyTurn(0.45f);
					return;
				}
				EnemyAttack(Moved, WeakTarget.Get(), TargetPos);
			});
			return;
		}
	}
	if (bStoppedByFear)
	{
		Log(FString::Printf(TEXT("🐺 %s fears the burning barrel and cannot come closer!"), *NameOf(Enemy)));
		FinishEnemyTurn(0.45f);
		return;
	}
	EnemyAttack(Enemy, Target, TargetPos);
}

void UTurnBasedCombatSubsystem::EnemyAttack(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos)
{
	FTurnUnitState* State = States.Find(Enemy);
	if (!State || IsDead(Enemy))
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	// Enemies bite only orthogonally (front, back, side) for 2 AP.
	const FTurnUnitState* TargetState = GetUnitState(Target);
	const FEnemyTurnProfile Profile = TurnProfileOf(Enemy);
	if (TurnManhattan(State->GridPos, TargetPos) != 1 || !TargetState || IsDead(Target))
	{
		FinishEnemyTurn(0.25f); // still on the way (UE: no pointless step back after the approach)
		return;
	}
	if (State->AP < Profile.AttackAPCost)
	{
		if (Profile.bHitAndRun)
		{
			EnemyRetreat(Enemy, TargetPos);
		}
		else
		{
			FinishEnemyTurn(0.25f);
		}
		return;
	}
	State->AP -= Profile.AttackAPCost;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(TargetPos - State->GridPos));
	AlignFacing(Enemy, State->Facing);
	const FGorkyArcResult Arc = FGorky17Utils::CalculateAttackArc(State->GridPos, TargetPos, TargetState->Facing);
	const int32 Damage = TurnBasedRules::EnemyAttackDamage(State->BaseDamage, Arc.DamageMultiplier,
		TurnBasedRules::StanceDamageMultiplier(TargetState->Stance, Balance));
	Changed();
	TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakTarget(Target);
	// Godot turn_based_combat_manager.gd: 1.3 s of the yellow warning square, then the enemy plays its attack clip
	// (play_tactical_attack; tactical_enemy_attack_duration overrides its length), the bite lands tactical_enemy_hit_delay
	// into it (or at its end), the enemy goes back to the idle (force_idle_animation) and steps back
	// tactical_enemy_retreat_delay later.
	After(1.3f, [this, WeakEnemy, WeakTarget, Damage, TargetPos]()
	{
		AActor* Attacker = WeakEnemy.Get();
		UEnemyAnimInstance* Anim = nullptr;
		if (const AEnemyCharacter* EnemyCharacter = Cast<AEnemyCharacter>(Attacker))
		{
			Anim = EnemyCharacter->GetMesh() ? Cast<UEnemyAnimInstance>(EnemyCharacter->GetMesh()->GetAnimInstance()) : nullptr;
		}
		float AttackSeconds = 0.8f;
		if (Anim)
		{
			if (const float ClipSeconds = Anim->NotifyAttack(); ClipSeconds > 0.f)
			{
				AttackSeconds = ClipSeconds;
			}
		}
		if (EnemyAttackDuration > 0.f)
		{
			AttackSeconds = EnemyAttackDuration;
		}
		TSharedRef<bool> bApplied = MakeShared<bool>(false);
		auto ApplyBite = [this, WeakEnemy, WeakTarget, Damage, TargetPos, bApplied]()
		{
			if (*bApplied)
			{
				return;
			}
			*bApplied = true;
			AActor* Biter = WeakEnemy.Get();
			AActor* Victim = WeakTarget.Get();
			if (Biter && Victim && States.Contains(Victim))
			{
				// Sprint 14: a knocked-down operative takes the melee bonus; a Brute's blow knocks him down.
				const AOperativeCharacter* VictimOperative = Cast<AOperativeCharacter>(Victim);
				const int32 Dealt = ApplySquadHit(Victim, Damage, NameOf(Biter), EKnockdownBlow::Melee);
				Log(FString::Printf(TEXT("🐺 Enemy %s attacks %s: %d damage!"), *NameOf(Biter), *NameOf(Victim), Dealt));
				const AEnemyCharacter* BiterEnemy = Cast<AEnemyCharacter>(Biter);
				if (BiterEnemy && BiterEnemy->GetArchetype() == EEnemyArchetype::Brute && VictimOperative && VictimOperative->KnockdownComponent)
				{
					VictimOperative->KnockdownComponent->TryKnockDown(EKnockdownCause::HeavyMelee, Biter->GetActorLocation(), Dealt);
				}
				if (IsActive() && IsDead(Victim))
				{
					OnSquadMemberKilled(Victim, TargetPos);
				}
			}
		};
		if (EnemyHitDelay > 0.f && EnemyHitDelay < AttackSeconds)
		{
			After(EnemyHitDelay, ApplyBite);
		}
		TWeakObjectPtr<UEnemyAnimInstance> WeakAnim(Anim);
		After(AttackSeconds, [this, WeakEnemy, WeakAnim, TargetPos, ApplyBite]()
		{
			ApplyBite();
			if (!IsActive())
			{
				return; // the bite killed an operative: mission failed, the fight is over
			}
			if (UEnemyAnimInstance* EnemyAnim = WeakAnim.Get())
			{
				EnemyAnim->StopSlotAnimation(0.2f, EnemyAnim->OneShotSlot);
			}
			if (CheckBattleEnd())
			{
				return;
			}
			After(EnemyRetreatDelay, [this, WeakEnemy, TargetPos]()
			{
				AActor* Attacker2 = WeakEnemy.Get();
				if (Attacker2 && !TurnProfileOf(Attacker2).bHitAndRun)
				{
					FinishEnemyTurn(0.25f); // frostbitten / brutes stay on their victim
				}
				else if (Attacker2)
				{
					EnemyRetreat(Attacker2, TargetPos);
				}
				else
				{
					FinishEnemyTurn(0.25f);
				}
			});
		});
	});
}

AActor* UTurnBasedCombatSubsystem::ChooseEnemyTarget(AActor* Enemy, bool bRanged, const TSet<FIntPoint>& Fear, FIntPoint& OutTargetPos)
{
	const FTurnUnitState* State = States.Find(Enemy);
	if (!State)
	{
		return nullptr;
	}
	const AEnemyCharacter* Character = Cast<AEnemyCharacter>(Enemy);
	const FEnemyTacticsProfile Tactics = EnemyTacticsRules::ProfileFor(Character ? Character->GetArchetype() : EEnemyArchetype::Base);
	TArray<AOperativeCharacter*> Operatives;
	TArray<FIntPoint> Cells;
	TArray<FEnemyTacticsTarget> Targets;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
	{
		const FTurnUnitState* MemberState = GetUnitState(Member.Get());
		if (!MemberState || IsDead(Member.Get()))
		{
			continue;
		}
		const FIntPoint Facing = FGorky17Utils::FacingToVector(MemberState->Facing);
		FEnemyTacticsTarget& Target = Targets.AddDefaulted_GetRef();
		Target.Location = Grid->GridToWorld(MemberState->GridPos);
		Target.Forward = (Grid->GridToWorld(MemberState->GridPos + Facing) - Target.Location).GetSafeNormal2D();
		const UHealthComponent* Health = Member->HealthComponent;
		Target.HealthFraction = Health ? Health->GetCurrentHealth() / FMath::Max(Health->GetMaxHealth(), 1.f) : 1.f;
		Target.bInCover = MemberState->Stance != EOperativeStance::Standing && IsCoveredFrom(MemberState->GridPos, State->GridPos);
		Target.Attackers = EnemyPhaseTargets.FindRef(Member.Get());
		// Melee: only the ones it can get to (fire blocks the way).
		Target.bUsable = bRanged || Fear.IsEmpty()
			|| TurnManhattan(State->GridPos, MemberState->GridPos) == 1
			|| !Grid->FindPathToAdjacent(State->GridPos, MemberState->GridPos, State->AP + 8, true, Fear).IsEmpty();
		Operatives.Add(Member.Get());
		Cells.Add(MemberState->GridPos);
	}
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		Targets[Index].NearestMateDistance = 100000.f;
		for (int32 Other = 0; Other < Targets.Num(); ++Other)
		{
			if (Other != Index)
			{
				Targets[Index].NearestMateDistance = FMath::Min(Targets[Index].NearestMateDistance,
					static_cast<float>(FVector::Dist2D(Targets[Index].Location, Targets[Other].Location)));
			}
		}
	}
	int32 Chosen = EnemyTacticsRules::ChooseTarget(Tactics, Grid->GridToWorld(State->GridPos), Targets);
	if (Chosen == INDEX_NONE)
	{
		// Nobody reachable: the nearest one (it walks to the fire's edge and growls, Godot).
		int32 BestDistance = TNumericLimits<int32>::Max();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			const int32 Distance = TurnBasedRules::CellDistance(State->GridPos, Cells[Index]);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Chosen = Index;
			}
		}
	}
	if (Chosen == INDEX_NONE)
	{
		return nullptr;
	}
	EnemyPhaseTargets.FindOrAdd(Operatives[Chosen])++;
	OutTargetPos = Cells[Chosen];
	return Operatives[Chosen];
}

bool UTurnBasedCombatSubsystem::IsCoveredFrom(const FIntPoint& TargetCell, const FIntPoint& Shooter) const
{
	const FIntPoint Dir = TurnStepDir(Shooter - TargetCell);
	const FIntPoint Checks[] = { FIntPoint(Dir.X, 0), FIntPoint(0, Dir.Y), Dir };
	for (const FIntPoint& Check : Checks)
	{
		if (Check != FIntPoint::ZeroValue && Grid->GetOccupantType(TargetCell + Check) == EGorkyOccupantType::Barricade)
		{
			return true;
		}
	}
	return false;
}

bool UTurnBasedCombatSubsystem::WalkEnemy(AActor* Enemy, const TArray<FIntPoint>& Path, TFunction<void(AActor*)> OnArrived)
{
	FTurnUnitState* State = States.Find(Enemy);
	if (!State)
	{
		return false;
	}
	const FIntPoint Pos = State->GridPos;
	TArray<FIntPoint> Actual;
	int32 Spent = 0;
	FIntPoint Current = Pos;
	bool bMine = false;
	for (const FIntPoint& Step : Path)
	{
		const int32 Cost = (Step.X != Current.X && Step.Y != Current.Y) ? Grid->DiagonalAPCost : 1;
		if (Spent + Cost > State->AP)
		{
			break;
		}
		Actual.Add(Step);
		Spent += Cost;
		Current = Step;
		if (Grid->GetOccupantType(Step) == EGorkyOccupantType::Mine)
		{
			bMine = true;
			break;
		}
	}
	if (Actual.IsEmpty())
	{
		return false;
	}
	const FIntPoint Destination = Actual.Last();
	AActor* MineActor = bMine ? Grid->GetOccupant(Destination) : nullptr;
	Grid->ClearOccupant(Pos);
	if (!bMine)
	{
		Grid->SetOccupant(Destination, Enemy, EGorkyOccupantType::Enemy);
	}
	State->GridPos = Destination;
	State->AP -= Spent;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Destination - (Actual.Num() > 1 ? Actual[Actual.Num() - 2] : Pos)));
	TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakMine(MineActor);
	FocusMovingEnemy(Enemy);
	StartMover(Enemy, Pos, Actual, EnemyStepDuration, nullptr, [this, WeakEnemy, WeakMine, bMine, Destination, OnArrived]()
	{
		AActor* Moved = WeakEnemy.Get();
		if (!Moved || !States.Contains(Moved))
		{
			FinishEnemyTurn(0.45f);
			return;
		}
		AlignFacing(Moved, States[Moved].Facing);
		if (bMine)
		{
			DetonateMine(Destination, WeakMine.Get(), Moved);
			if (FTurnUnitState* MovedState = States.Find(Moved))
			{
				MovedState->AP = 0;
			}
			FinishEnemyTurn(1.2f);
			return;
		}
		OnArrived(Moved);
	});
	return true;
}

void UTurnBasedCombatSubsystem::ExecuteRangedEnemyTurn(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos, const TSet<FIntPoint>& Fear)
{
	FTurnUnitState* State = States.Find(Enemy);
	const FTurnUnitState* TargetState = GetUnitState(Target);
	if (!State || !TargetState)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	const FEnemyTurnProfile Profile = TurnProfileOf(Enemy);
	const FIntPoint Pos = State->GridPos;
	TMap<FIntPoint, int32> Reach = Grid->GetReachableCells(Pos, FMath::Max(0, State->AP), Fear);
	Reach.Add(Pos, 0);
	auto NextToOperative = [this](const FIntPoint& Cell)
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
		{
			const FTurnUnitState* MemberState = GetUnitState(Member.Get());
			if (MemberState && !IsDead(Member.Get()) && TurnBasedRules::CellDistance(Cell, MemberState->GridPos) <= 1)
			{
				return true;
			}
		}
		return false;
	};
	TArray<FEnemyFiringCell> Cells;
	for (const TPair<FIntPoint, int32>& Entry : Reach)
	{
		if (Entry.Key != Pos && Grid->GetOccupantType(Entry.Key) == EGorkyOccupantType::Mine)
		{
			continue; // it does not stop on a mine on purpose
		}
		FEnemyFiringCell& Cell = Cells.AddDefaulted_GetRef();
		Cell.Cell = Entry.Key;
		Cell.PathCost = Entry.Value;
		Cell.Distance = TurnBasedRules::CellDistance(Entry.Key, TargetPos);
		bool bPastCover = false;
		Cell.bLineOfFire = GorkyLineOfSight::HasLineOfFireThroughCover(Entry.Key, TargetPos, *Grid, bPastCover);
		Cell.bNextToOperative = NextToOperative(Entry.Key);
		Cell.HitChance = EnemyTurnRules::RangedHitChance(Profile, Cell.Distance, TargetState->Stance, IsCoveredFrom(TargetPos, Entry.Key),
			Balance.EnemyFireAtCoverMultiplier);
	}
	const int32 Chosen = EnemyTurnRules::ChooseFiringCell(Profile, Cells, State->AP);
	TWeakObjectPtr<AActor> WeakTarget(Target);
	if (Chosen != INDEX_NONE)
	{
		const FIntPoint Cell = Cells[Chosen].Cell;
		auto Shoot = [this, WeakTarget, TargetPos](AActor* Shooter)
		{
			EnemyRangedAttack(Shooter, WeakTarget.Get(), TargetPos);
		};
		if (Cell == Pos || !WalkEnemy(Enemy, Grid->FindPath(Pos, Cell, State->AP, Fear), Shoot))
		{
			EnemyRangedAttack(Enemy, Target, TargetPos);
		}
		return;
	}
	// No shot this turn: the cell with a line of fire nearest to the preferred band, else the one closest to the target.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FEnemyFiringCell& Cell = Cells[Index];
		const float Band = Cell.Distance < Profile.PreferredMin ? Profile.PreferredMin - Cell.Distance
			: (Cell.Distance > Profile.PreferredMax ? Cell.Distance - Profile.PreferredMax : 0.f);
		const float Score = Band + (Cell.bLineOfFire ? 0.f : 3.f) + (Cell.bNextToOperative ? 5.f : 0.f) + 0.05f * Cell.PathCost;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE || Cells[Best].Cell == Pos
		|| !WalkEnemy(Enemy, Grid->FindPath(Pos, Cells[Best].Cell, State->AP, Fear), [this](AActor*) { FinishEnemyTurn(0.35f); }))
	{
		FinishEnemyTurn(0.35f);
	}
}

void UTurnBasedCombatSubsystem::EnemyRangedAttack(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos)
{
	FTurnUnitState* State = States.Find(Enemy);
	const FTurnUnitState* TargetState = GetUnitState(Target);
	const FEnemyTurnProfile Profile = TurnProfileOf(Enemy);
	if (!State || !TargetState || IsDead(Target) || State->AP < Profile.AttackAPCost)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	State->AP -= Profile.AttackAPCost;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(TargetPos - State->GridPos));
	AlignFacing(Enemy, State->Facing);
	const int32 Distance = TurnBasedRules::CellDistance(State->GridPos, TargetPos);
	const float Chance = EnemyTurnRules::RangedHitChance(Profile, Distance, TargetState->Stance, IsCoveredFrom(TargetPos, State->GridPos),
		Balance.EnemyFireAtCoverMultiplier);
	const int32 Damage = TurnBasedRules::EnemyAttackDamage(State->BaseDamage, 1.f, TurnBasedRules::StanceDamageMultiplier(TargetState->Stance, Balance));
	if (Overlay)
	{
		Overlay->SetCells(ETurnOverlayLayer::Warning, { TargetPos });
	}
	Changed();
	TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakTarget(Target);
	// The warning square, then the shot (attack clip), the roll, then (hit and run, next to an operative) a step back.
	After(1.0f, [this, WeakEnemy, WeakTarget, TargetPos, Chance, Damage, Distance]()
	{
		AActor* Shooter = WeakEnemy.Get();
		AActor* Victim = WeakTarget.Get();
		if (!Shooter || !Victim || !States.Contains(Victim))
		{
			FinishEnemyTurn(0.25f);
			return;
		}
		if (const AEnemyCharacter* EnemyCharacter = Cast<AEnemyCharacter>(Shooter))
		{
			if (UEnemyAnimInstance* Anim = EnemyCharacter->GetMesh() ? Cast<UEnemyAnimInstance>(EnemyCharacter->GetMesh()->GetAnimInstance()) : nullptr)
			{
				Anim->NotifyAttack();
			}
		}
		const bool bHit = FMath::FRand() < Chance;
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			const FVector Miss(FMath::FRandRange(-90.f, 90.f), FMath::FRandRange(-90.f, 90.f), 40.f);
			Feedback->SpawnTracer(Shooter->GetActorLocation() + FVector(0.f, 0.f, 60.f), Victim->GetActorLocation() + (bHit ? FVector::ZeroVector : Miss),
				FLinearColor(0.4f, 0.8f, 1.f), EDamageType::Cryo);
		}
		if (bHit)
		{
			const int32 Dealt = ApplySquadHit(Victim, Damage, NameOf(Shooter), EKnockdownBlow::Ranged);
			Log(FString::Printf(TEXT("🎯 %s shoots at %s from %d cells (chance %d%%): %d damage!"), *NameOf(Shooter), *NameOf(Victim), Distance,
				FMath::RoundToInt(Chance * 100.f), Dealt));
			if (IsActive() && IsDead(Victim))
			{
				OnSquadMemberKilled(Victim, TargetPos);
			}
		}
		else
		{
			Log(FString::Printf(TEXT("💨 %s shoots at %s from %d cells (chance %d%%): miss."), *NameOf(Shooter), *NameOf(Victim), Distance,
				FMath::RoundToInt(Chance * 100.f)));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[EnemyTurn] %s ranged at %s: %d cells, chance %.2f, %s"), *NameOf(Shooter), *NameOf(Victim),
			Distance, Chance, bHit ? TEXT("hit") : TEXT("miss"));
		if (!IsActive() || CheckBattleEnd())
		{
			return;
		}
		After(0.6f, [this, WeakEnemy, TargetPos]()
		{
			AActor* Attacker = WeakEnemy.Get();
			const FTurnUnitState* AttackerState = Attacker ? States.Find(Attacker) : nullptr;
			if (AttackerState && TurnProfileOf(Attacker).bHitAndRun && TurnBasedRules::CellDistance(AttackerState->GridPos, TargetPos) <= 1)
			{
				EnemyRetreat(Attacker, TargetPos);
			}
			else
			{
				FinishEnemyTurn(0.25f);
			}
		});
	});
}

void UTurnBasedCombatSubsystem::EnemyRetreat(AActor* Enemy, const FIntPoint& TargetPos)
{
	FTurnUnitState* State = States.Find(Enemy);
	if (!State || State->AP < 1)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	const AEnemyCharacter* EnemyCharacter = Cast<AEnemyCharacter>(Enemy);
	const TSet<FIntPoint> Fear = EnemyCharacter && EnemyCharacter->DoesFearFire() ? GetFearCells() : TSet<FIntPoint>();
	const FIntPoint Pos = State->GridPos;
	// One step away from the operative, back turned to it (Godot _enemy_perform_retreat).
	const FIntPoint Desired = Pos + TurnStepDir(Pos - TargetPos);
	const int32 DesiredCost = (Desired.X != Pos.X && Desired.Y != Pos.Y) ? Grid->DiagonalAPCost : 1;
	FIntPoint Found(-999, -999);
	if (State->AP >= DesiredCost && Grid->IsValidCell(Desired) && Grid->IsCellWalkable(Desired) && !Fear.Contains(Desired))
	{
		Found = Desired;
	}
	else
	{
		const FIntPoint Offsets[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
		for (const FIntPoint& Offset : Offsets)
		{
			const int32 Cost = (Offset.X != 0 && Offset.Y != 0) ? Grid->DiagonalAPCost : 1;
			const FIntPoint Candidate = Pos + Offset;
			if (State->AP >= Cost && Grid->IsValidCell(Candidate) && Grid->IsCellWalkable(Candidate) && !Fear.Contains(Candidate)
				&& TurnBasedRules::CellDistance(Candidate, TargetPos) > 1)
			{
				Found = Candidate;
				break;
			}
		}
	}
	if (Found.X == -999)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	State->AP -= (Found.X != Pos.X && Found.Y != Pos.Y) ? Grid->DiagonalAPCost : 1;
	Grid->ClearOccupant(Pos);
	const bool bMine = Grid->GetOccupantType(Found) == EGorkyOccupantType::Mine;
	AActor* MineActor = bMine ? Grid->GetOccupant(Found) : nullptr;
	if (!bMine)
	{
		Grid->SetOccupant(Found, Enemy, EGorkyOccupantType::Enemy);
	}
	State->GridPos = Found;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Found - Pos));
	TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakMine(MineActor);
	FocusMovingEnemy(Enemy);
	StartMover(Enemy, Pos, { Found }, EnemyStepDuration, nullptr, [this, WeakEnemy, WeakMine, bMine, Found]()
	{
		if (bMine && WeakEnemy.IsValid())
		{
			DetonateMine(Found, WeakMine.Get(), WeakEnemy.Get());
		}
		FinishEnemyTurn(0.45f);
	});
}

void UTurnBasedCombatSubsystem::FinishEnemyTurn(float Delay)
{
	if (!IsActive())
	{
		return;
	}
	if (Overlay)
	{
		Overlay->ClearLayer(ETurnOverlayLayer::EnemyReach);
		Overlay->ClearLayer(ETurnOverlayLayer::Warning);
	}
	Changed();
	if (CheckBattleEnd())
	{
		return;
	}
	if (!EnemyQueue.IsEmpty())
	{
		After(Delay, [this]() { ProcessNextEnemy(); });
		return;
	}
	// Godot: after the last enemy the camera glides to the squad overview (17 m, 0.85 s) and the player turn follows.
	const bool bFly = bCinematics;
	After(Delay, [this, bFly]()
	{
		if (ATacticalCameraPawn* Camera = GetCamera())
		{
			const USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>();
			Camera->SetFollowTarget(SquadSystem ? SquadSystem->GetLeader() : nullptr);
			Camera->SmoothFocusOnPosition(GetSquadOverviewCenter(), 0.85f, OverviewDistance);
		}
		if (bFly)
		{
			After(0.85f, [this]() { ProcessNextEnemy(); });
		}
		else
		{
			ProcessNextEnemy();
		}
	});
}

void UTurnBasedCombatSubsystem::FocusMovingEnemy(AActor* Enemy) const
{
	// Godot _on_gorky17_enemy_movement_started.
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->SmoothFocusOnTarget(Enemy, 0.35f, EnemyFocusDistance);
	}
}

// --- Explosions, deaths, end --------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::DetonateBarrel(const FIntPoint& Cell, AActor* Barrel)
{
	Log(TEXT("💥 THE BARREL EXPLODES AND IGNITES! Blast zone 3x3 cells!"));
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const FIntPoint HitCell = Cell + FIntPoint(DX, DY);
			AActor* Occupant = Grid->GetOccupant(HitCell);
			const FTurnUnitState* State = GetUnitState(Occupant);
			if (!Occupant || Occupant == Barrel || !State || (!State->bSquad && !Enemies.Contains(Occupant)))
			{
				continue;
			}
			const bool bSquadMember = State->bSquad;
			const float Damage = bSquadMember
				? FMath::Max(1, FMath::RoundToInt(Balance.BarrelDamage * TurnBasedRules::StanceDamageMultiplier(State->Stance, Balance)))
				: Balance.BarrelDamage;
			ApplyBlast(Occupant, bSquadMember, Damage, TEXT("Barrel"));
			if (!IsActive())
			{
				return; // an operative's death failed the mission: the grid and the unit states are gone
			}
			if (IsDead(Occupant))
			{
				if (bSquadMember)
				{
					OnSquadMemberKilled(Occupant, HitCell);
				}
				else
				{
					OnEnemyKilled(Occupant, HitCell);
				}
			}
		}
	}
	// Sprint 14: units closer than 2.5 m are knocked down.
	if (Barrel)
	{
		UKnockdownComponent::NotifyExplosion(GetWorld(), Barrel->GetActorLocation());
	}
	// The barrel stays on its cell as an obstacle and burns for 3 rounds.
	if (ABarrelActor* BarrelActor = Cast<ABarrelActor>(Barrel))
	{
		BarrelActor->IgniteForTurnBased();
		BurningBarrels.Add(Barrel, TurnBarrelBurnRounds);
	}
}

void UTurnBasedCombatSubsystem::DetonateMine(const FIntPoint& Cell, AActor* Mine, AActor* Victim)
{
	Log(FString::Printf(TEXT("💣 MINE DETONATED under %s!"), *NameOf(Victim)));
	Grid->ClearOccupant(Cell);
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>(); Feedback && Mine)
	{
		Feedback->HighlightTarget(Mine);
	}
	if (Mine)
	{
		Mine->Destroy();
	}
	FTurnUnitState* State = States.Find(Victim);
	if (!State)
	{
		return;
	}
	const int32 Damage = FMath::Max(1, FMath::RoundToInt(TurnMineDamage * TurnBasedRules::StanceDamageMultiplier(State->Stance, Balance)));
	State->AP = 0;
	State->GridPos = Cell;
	PlaceOnCell(Victim, Cell);
	const bool bSquadMember = State->bSquad;
	ApplyBlast(Victim, bSquadMember, Damage, TEXT("Mine"));
	if (!IsActive())
	{
		return; // the blast ended the fight (mission failed / wave cleared): the states are gone
	}
	if (IsDead(Victim))
	{
		if (bSquadMember)
		{
			OnSquadMemberKilled(Victim, Cell);
		}
		else
		{
			OnEnemyKilled(Victim, Cell);
		}
		return;
	}
	Grid->SetOccupant(Cell, Victim, State->bSquad ? EGorkyOccupantType::Squad : EGorkyOccupantType::Enemy);
	UKnockdownComponent::NotifyExplosion(GetWorld(), Victim->GetActorLocation()); // Sprint 14: the mine floors him
	if (State->bSquad)
	{
		// Godot: a surviving operative's turn ends 0.75 s after the blast.
		After(0.75f, [this]() { EndCurrentUnitTurn(); });
	}
}

void UTurnBasedCombatSubsystem::OnEnemyKilled(AActor* Enemy, const FIntPoint& Cell)
{
	Log(FString::Printf(TEXT("☠️ Enemy %s destroyed!"), *NameOf(Enemy)));
	if (!Grid)
	{
		return; // the fight already ended (e.g. the kill cleared the wave)
	}
	if (const FTurnUnitState* State = GetUnitState(Enemy))
	{
		Grid->ClearOccupant(State->GridPos);
	}
	Grid->ClearOccupant(Cell);
	Enemies.Remove(Enemy);
	EnemyQueue.Remove(Enemy);
	States.Remove(Enemy);
}

void UTurnBasedCombatSubsystem::OnSquadMemberKilled(AActor* Member, const FIntPoint& Cell)
{
	Log(FString::Printf(TEXT("⚰️ %s has fallen in battle!"), *NameOf(Member)));
	if (!Grid)
	{
		return; // the fight is already over
	}
	Grid->ClearOccupant(Cell);
	NotifyOperativeKilled(Cast<AOperativeCharacter>(Member)); // usually done already by his own death handler
}

void UTurnBasedCombatSubsystem::NotifyOperativeKilled(AOperativeCharacter* Operative)
{
	if (!IsActive() || !Operative)
	{
		return;
	}
	const int32 Index = Squad.IndexOfByPredicate([Operative](const TWeakObjectPtr<AOperativeCharacter>& Weak) { return Weak.Get() == Operative; });
	if (const FTurnUnitState* State = States.Find(Operative); State && Grid)
	{
		Grid->ClearOccupant(State->GridPos);
	}
	States.Remove(Operative);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const bool bWasActive = Index == ActiveIndex;
	Squad.RemoveAt(Index);
	if (Index < ActiveIndex)
	{
		--ActiveIndex; // the same living operative stays active
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TurnBased] %s removed from the turn order (%d left, active %d)"), *NameOf(Operative), Squad.Num(), ActiveIndex);
	if (Squad.IsEmpty())
	{
		CheckBattleEnd();
		return;
	}
	if (!bWasActive || Phase != ETurnPhase::Squad)
	{
		RefreshOverlay();
		Changed();
		return;
	}
	// The active operative died on his own turn (a blast, a trap): the next one takes over, or the squad phase ends.
	bAttackMode = false;
	if (Squad.IsValidIndex(ActiveIndex))
	{
		if (USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			SquadSystem->SetLeader(GetActiveUnit());
		}
		RefreshOverlay();
		Changed();
		return;
	}
	ActiveIndex = Squad.Num() - 1;
	After(0.3f, [this]() { EndSquadPhaseAfterDeath(); });
}

void UTurnBasedCombatSubsystem::EndSquadPhaseAfterDeath()
{
	if (Phase != ETurnPhase::Squad)
	{
		return;
	}
	if (IsBusy())
	{
		After(0.3f, [this]() { EndSquadPhaseAfterDeath(); }); // the action that killed him finishes first
		return;
	}
	EndSquadPhase();
}

bool UTurnBasedCombatSubsystem::CheckBattleEnd()
{
	if (!IsActive())
	{
		return true;
	}
	Enemies.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });
	if (Enemies.IsEmpty())
	{
		EndCombat(true, true);
		return true;
	}
	if (Squad.IsEmpty())
	{
		EndCombat(false, true);
		return true;
	}
	return false;
}
