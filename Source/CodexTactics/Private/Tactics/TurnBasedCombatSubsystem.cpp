#include "Tactics/TurnBasedCombatSubsystem.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
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
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TurretActor.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnGridOverlayActor.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	constexpr int32 TurnGridCells = 14;
	constexpr float TurnCellSize = 150.f; // Godot tactical_cell_size 1.5 m
	constexpr float TurnMineDamage = 50.f; // Godot _detonate_mine base_mine_dmg
	constexpr float TurnTurretSupportDistance = 4500.f; // Godot max_support_dist 45 m
	constexpr int32 TurnBarrelBurnRounds = 3;

	const TCHAR* TurnStanceName(EOperativeStance Stance)
	{
		return Stance == EOperativeStance::Prone ? TEXT("Лёжа") : (Stance == EOperativeStance::Crouching ? TEXT("Присев") : TEXT("Стоя"));
	}

	const TCHAR* TurnArcName(EGorkyArcZone Arc)
	{
		// Godot prints str(ArcZone) (an int); the UE feed names the arc.
		return Arc == EGorkyArcZone::Rear ? TEXT("тыл") : (Arc == EGorkyArcZone::Flank ? TEXT("фланг") : TEXT("фронт"));
	}

	FIntPoint TurnStepDir(const FIntPoint& Delta)
	{
		return FIntPoint(FMath::Clamp(Delta.X, -1, 1), FMath::Clamp(Delta.Y, -1, 1));
	}

	int32 TurnManhattan(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}
}

// --- Lifecycle ----------------------------------------------------------------------------------------------------

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
			SquadStepDuration = BalanceAsset->GetNumber(TEXT("tactical_step_duration"), SquadStepDuration);
			EnemyStepDuration = BalanceAsset->GetNumber(TEXT("tactical_enemy_step_duration"), EnemyStepDuration);
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
	TArray<AEnemyCharacter*> GridEnemies;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		Ignore.Add(*It);
		if (!It->IsDying() && !IsDead(*It) && OnGrid(*It))
		{
			GridEnemies.Add(*It);
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
	for (AEnemyCharacter* Enemy : GridEnemies)
	{
		const FIntPoint Cell = Grid->FindNearestFreeCell(Grid->WorldToGrid(Enemy->GetActorLocation()));
		Grid->SetOccupant(Cell, Enemy, EGorkyOccupantType::Enemy);
		PlaceOnCell(Enemy, Cell);
		Enemies.Add(Enemy);
		FTurnUnitState& State = States.Add(Enemy);
		State.Actor = Enemy;
		State.GridPos = Cell;
		State.MaxAP = State.AP = Balance.EnemyMaxAP;
		State.Armor = 3.f;
		State.BaseDamage = Balance.EnemyBaseDamage;
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

	Phase = ETurnPhase::Squad;
	ActiveIndex = 0;
	Round = 1;
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
		Log(TEXT("🏆 ПОБЕДА В ПОШАГОВОМ БОЮ! Враги повержены. Включена тактическая пауза (20с) для перегруппировки [ПРОБЕЛ]."));
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
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (AController* Controller = It->GetController())
		{
			Controller->StopMovement();
		}
		Freeze(*It);
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

void UTurnBasedCombatSubsystem::StartMover(AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path, float StepDuration,
	TFunction<bool(int32)> OnStep, TFunction<void()> OnDone, bool bFaceSteps)
{
	FMover& Mover = Movers.AddDefaulted_GetRef();
	Mover.Actor = Actor;
	Mover.bFaceSteps = bFaceSteps;
	Mover.StepDuration = StepDuration;
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
		AlignFacing(Actor, Mover.Facings[0]);
	}
}

void UTurnBasedCombatSubsystem::Tick(float DeltaTime)
{
	for (int32 Index = Movers.Num() - 1; Index >= 0; --Index)
	{
		if (!Movers.IsValidIndex(Index))
		{
			continue;
		}
		FMover& Mover = Movers[Index];
		AActor* Actor = Mover.Actor.Get();
		bool bFinished = !Actor;
		if (Actor)
		{
			const FIntPoint Dir = FGorky17Utils::FacingToVector(Mover.Facings[Mover.Index]);
			const float Duration = Mover.StepDuration * (Dir.X != 0 && Dir.Y != 0 ? 1.414f : 1.f);
			Mover.Alpha = FMath::Min(1.f, Mover.Alpha + DeltaTime / Duration);
			Actor->SetActorLocation(FMath::Lerp(Mover.From, Mover.Points[Mover.Index], FMath::SmoothStep(0.f, 1.f, Mover.Alpha)));
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
					AlignFacing(Actor, Mover.Facings[Mover.Index]);
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
}

// --- Squad turn ---------------------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::StartPlayerTurn()
{
	Phase = ETurnPhase::Squad;
	bSquadUnitMoving = false;

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
			Log(FString::Printf(TEXT("🔥 Бочка продолжает пылать (осталось ходов: %d)"), Entry.Value));
		}
	}
	for (const TWeakObjectPtr<AActor>& Barrel : BurntOut)
	{
		BurningBarrels.Remove(Barrel);
		if (ABarrelActor* BarrelActor = Cast<ABarrelActor>(Barrel.Get()))
		{
			BarrelActor->ExtinguishNow();
			Log(TEXT("💨 Горючая бочка полностью прогорела и погасла!"));
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
	for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(State->GridPos, State->AP))
	{
		if (Entry.Key != State->GridPos)
		{
			Reach.Add(Entry.Key);
		}
	}
	Overlay->SetCells(ETurnOverlayLayer::Reachable, Reach);
	// Attack preview: targets the weapon can reach now (enemies, barrels, barricades).
	TArray<FIntPoint> Targets;
	if (!State->bHasAttacked && State->AP >= Balance.AttackAPCost)
	{
		for (const TPair<FIntPoint, FTurnBasedAttackCell>& Entry : TurnBasedRules::GetWeaponAttackCells(*Grid, State->GridPos, WeaponOf(Unit), State->Stance, Balance))
		{
			const EGorkyOccupantType Type = Grid->GetOccupantType(Entry.Key);
			if ((Type == EGorkyOccupantType::Enemy || Type == EGorkyOccupantType::Barrel || Type == EGorkyOccupantType::Barricade)
				&& GorkyLineOfSight::HasLineOfSight(State->GridPos, Entry.Key, *Grid))
			{
				Targets.Add(Entry.Key);
			}
		}
	}
	Overlay->SetCells(ETurnOverlayLayer::Attack, Targets);
}

bool UTurnBasedCombatSubsystem::SelectUnit(AOperativeCharacter* Unit)
{
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad)
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
	ActiveIndex = Index;
	if (USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		SquadSystem->SetLeader(Unit);
	}
	RefreshOverlay();
	Changed();
	return true;
}

bool UTurnBasedCombatSubsystem::MoveActiveUnitTo(const FIntPoint& Cell)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad || !State || State->GridPos == Cell)
	{
		return false;
	}
	// Only highlighted cells (Godot: reachable with the AP left).
	const TMap<FIntPoint, int32> Reach = Grid->GetReachableCells(State->GridPos, State->AP);
	const int32* Cost = Reach.Find(Cell);
	const TArray<FIntPoint> Path = Grid->FindPath(State->GridPos, Cell, State->AP);
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
		Log(FString::Printf(TEXT("🛑 Взрыв мины прервал ход бойца %s!"), *NameOf(Unit)));
	}
	else
	{
		State->AP -= *Cost;
	}
	State->GridPos = Final;
	Grid->SetOccupant(Final, Unit, EGorkyOccupantType::Squad);

	bSquadUnitMoving = true;
	RefreshOverlay();
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	TWeakObjectPtr<AActor> WeakMine(Mine);
	StartMover(Unit, From, Actual, SquadStepDuration, nullptr, [this, WeakUnit, WeakMine, MineCell]()
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
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad || !State)
	{
		return false;
	}
	if (State->Stance == NewStance)
	{
		return true;
	}
	if (State->AP < Balance.StanceAPCost)
	{
		Log(FString::Printf(TEXT("⚠️ Недостаточно AP для смены стойки (%d/%d AP)!"), State->AP, Balance.StanceAPCost));
		return false;
	}
	State->AP -= Balance.StanceAPCost;
	State->Stance = NewStance;
	Unit->SetStance(NewStance);
	Log(FString::Printf(TEXT("🛡️ %s сменил стойку: %s (расход %d AP)"), *NameOf(Unit), TurnStanceName(NewStance), Balance.StanceAPCost));
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
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad || !State || State->AP < 1 || State->Facing == NewFacing)
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

FTurnAttackResult UTurnBasedCombatSubsystem::AttackCell(const FIntPoint& Cell, bool bGuaranteeHit)
{
	FTurnAttackResult Result;
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* State = States.Find(Unit);
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad || !State)
	{
		Result.Reason = TEXT("no_unit");
		return Result;
	}
	if (State->bHasAttacked)
	{
		Log(FString::Printf(TEXT("⚠️ Боец %s уже атаковал в этом раунде! Доступна только 1 атака за ход."), *NameOf(Unit)));
		Result.Reason = TEXT("already_attacked");
		return Result;
	}
	if (State->AP < Balance.AttackAPCost)
	{
		Log(FString::Printf(TEXT("⚠️ Недостаточно AP для атаки (%d/%d AP)!"), State->AP, Balance.AttackAPCost));
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
		Log(Weapon ? FString::Printf(TEXT("⚠️ Цель не на линии огня оружия (%s)!"), *Weapon->WeaponName.ToString()) : FString(TEXT("⚠️ Цель не на линии огня оружия!")));
		Result.Reason = TEXT("not_in_fire_lane");
		return Result;
	}
	if (!GorkyLineOfSight::HasLineOfSight(State->GridPos, Cell, *Grid))
	{
		Log(TEXT("⚠️ Нет прямой видимости (LoS) до цели!"));
		Result.Reason = TEXT("no_los");
		return Result;
	}

	State->AP -= Balance.AttackAPCost;
	State->bHasAttacked = true;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Offset));
	AlignFacing(Unit, State->Facing);
	const int32 Distance = TurnBasedRules::CellDistance(State->GridPos, Cell);

	if (Type == EGorkyOccupantType::Enemy)
	{
		const float Chance = TurnBasedRules::CalculateHitChance(Weapon, Distance, State->Stance, Balance);
		const float Roll = FMath::FRand();
		Result.bSuccess = true;
		Result.HitChance = Chance;
		Result.bHit = bGuaranteeAllHits || bGuaranteeHit || Roll <= Chance;
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			FVector End = Target->GetActorLocation();
			if (!Result.bHit)
			{
				End += FVector(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(20.f, 120.f));
			}
			Feedback->SpawnTracer(Unit->GetMuzzleLocation(), End, Weapon ? Weapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
				Weapon ? Weapon->DamageType : EDamageType::Kinetic);
		}
		if (!Result.bHit)
		{
			Log(FString::Printf(TEXT("❌ ПРОМАХ! Шанс: %d%% (выпало: %d%%)"), FMath::RoundToInt(Chance * 100.f), FMath::RoundToInt(Roll * 100.f)));
		}
		else if (FTurnUnitState* EnemyState = States.Find(Target))
		{
			const FGorkyArcResult Arc = FGorky17Utils::CalculateAttackArc(State->GridPos, Cell, EnemyState->Facing);
			const float Base = TurnBasedRules::GetDamageForDistance(Weapon, Distance, State->BaseDamage);
			Result.Damage = TurnBasedRules::SquadAttackDamage(Base, Arc.DamageMultiplier, EnemyState->Armor, Arc.EffectiveArmorMultiplier);
			ApplyDamage(Target, Result.Damage, NameOf(Unit));
			Log(FString::Printf(TEXT("💥 Атака по %s: %d урона (%s, x%.2f) [Меткость: %d%%]"), *NameOf(Target), Result.Damage, TurnArcName(Arc.Arc),
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
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnTracer(Unit->GetMuzzleLocation(), Target->GetActorLocation(), UCombatFeedbackSubsystem::DefaultTracerColor());
		}
		DetonateBarrel(Cell, Target);
	}
	else if (Type == EGorkyOccupantType::Barricade)
	{
		// Godot _damage_barricade (the trapped-barricade retaliation comes with the grid deployables).
		Result.bSuccess = true;
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

void UTurnBasedCombatSubsystem::EndCurrentUnitTurn()
{
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad)
	{
		return;
	}
	RelocateTarget.Reset();
	RelocateCells.Reset();
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
		RefreshOverlay();
		Changed();
		return;
	}
	EndSquadPhase();
}

void UTurnBasedCombatSubsystem::PassSquadTurn()
{
	if (bSquadUnitMoving || Phase != ETurnPhase::Squad)
	{
		return;
	}
	Log(TEXT("🛑 Ход отряда завершен. Ход переходит к врагам!"));
	EndSquadPhase();
}

void UTurnBasedCombatSubsystem::HandleWorldClick(const FVector& WorldPoint, AActor* HitActor, bool bShift)
{
	if (!IsActive() || Phase != ETurnPhase::Squad || bSquadUnitMoving || !Grid)
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
					Post(TEXT("ТАКТИКА"), FString::Printf(TEXT("✅ Баррикада успешно развернута (-%d AP)."), APCost));
				}
				else
				{
					Post(TEXT("ТАКТИКА"), TEXT("⚠️ Ошибка при установке баррикады!"));
					RefreshOverlay();
				}
			}
			else if (Cell == RelocateOrigin && FMath::IsNearlyZero(FRotator::NormalizeAxis(RelocateYaw - Barricade->GetActorRotation().Yaw), 0.5f))
			{
				CancelRelocate();
			}
			else
			{
				Post(TEXT("ТАКТИКА"), TEXT("⚠️ Нельзя установить баррикаду в этой клетке с текущим углом поворота (или не хватает AP)!"));
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
				Post(TEXT("ТАКТИКА"), FString::Printf(TEXT("✅ Объект успешно перемещен (-%d AP)."), APCost));
			}
			else
			{
				Post(TEXT("ТАКТИКА"), TEXT("⚠️ Ошибка при перемещении объекта!"));
				RefreshOverlay();
			}
		}
		else if (Cell == RelocateOrigin)
		{
			CancelRelocate();
		}
		else
		{
			Post(TEXT("ТАКТИКА"), TEXT("⚠️ Выбранная клетка недоступна для перемещения (вне радиуса AP или занята)!"));
		}
		return;
	}

	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(HitActor); Operative && Squad.Contains(Operative))
	{
		SelectUnit(Operative);
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
	switch (Grid->GetOccupantType(Cell))
	{
	case EGorkyOccupantType::Barrel:
	{
		// Godot: an orthogonally adjacent barrel is pushed, Shift shoots it; a distant one needs Shift.
		AActor* Barrel = Grid->GetOccupant(Cell);
		const FIntPoint Diff = UnitState ? Cell - UnitState->GridPos : FIntPoint(99, 99);
		if (!bShift)
		{
			Highlight(Barrel);
			if (FMath::Abs(Diff.X) + FMath::Abs(Diff.Y) == 1)
			{
				StartRelocate(Barrel);
			}
			else
			{
				Post(TEXT("ТАКТИКА"), TEXT("⚠️ Боец должен подойти вплотную к бочке, чтобы переместить её (или зажмите Shift для выстрела)!"));
			}
			return;
		}
		AttackCell(Cell);
		break;
	}
	case EGorkyOccupantType::Turret:
	{
		AActor* Turret = Grid->GetOccupant(Cell);
		if (!bShift)
		{
			Highlight(Turret);
			if (IsUnitAdjacentToObject(GetActiveUnit(), Turret))
			{
				StartRelocate(Turret);
			}
			else
			{
				Post(TEXT("ТАКТИКА"), TEXT("⚠️ Боец должен подойти вплотную к турели, чтобы переместить её!"));
			}
		}
		break;
	}
	case EGorkyOccupantType::Barricade:
	{
		// Godot: an adjacent barricade is relocated (with rotation), Shift attacks it.
		AActor* Barricade = Grid->GetOccupant(Cell);
		if (!bShift)
		{
			Highlight(Barricade);
			if (IsUnitAdjacentToObject(GetActiveUnit(), Barricade))
			{
				StartRelocate(Barricade);
			}
			else
			{
				Post(TEXT("ТАКТИКА"), TEXT("⚠️ Боец должен подойти вплотную к баррикаде, чтобы переместить её (или зажмите Shift для атаки)!"));
			}
			return;
		}
		AttackCell(Cell);
		break;
	}
	case EGorkyOccupantType::Enemy:
		AttackCell(Cell);
		break;
	case EGorkyOccupantType::Squad:
		SelectUnit(Cast<AOperativeCharacter>(Grid->GetOccupant(Cell)));
		break;
	default:
		MoveActiveUnitTo(Cell);
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
	if (Phase != ETurnPhase::Squad || bSquadUnitMoving || !UnitState || (!ObjectState && !bBarricade) || !Grid)
	{
		return false;
	}
	const int32 CostPerStep = Balance.PushBarrelAPCost;
	if (UnitState->AP < CostPerStep)
	{
		Post(TEXT("ТАКТИКА"), FString::Printf(TEXT("⚠️ Недостаточно очков действия для перемещения (нужно минимум %d AP)!"), CostPerStep));
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
		Post(TEXT("ТАКТИКА"), TEXT("🧱 Размещение баррикады: [Колёсико мыши/Q/E — поворот на 45°, ЛКМ — подтвердить, ПКМ/[Esc] — отмена]"));
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
	Post(TEXT("ТАКТИКА"), FString::Printf(TEXT("📦 Выберите соседнюю свободную клетку для перемещения %s (1 деление = %d AP) [ЛКМ — подтвердить, ПКМ/[Esc] — отмена]"),
		Object->IsA<ABarrelActor>() ? TEXT("бочки") : TEXT("объекта"), CostPerStep));
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
	if (bSquadUnitMoving || !UnitState || !IsValid(Barricade))
	{
		return false;
	}
	if (!CanPlaceBarricadeAt(Barricade, Cell, Yaw))
	{
		Log(TEXT("⚠️ Нельзя установить баррикаду в этой позиции!"));
		return false;
	}
	const int32 TotalAP = CustomAPCost >= 0 ? CustomAPCost : Balance.PushBarrelAPCost;
	if (UnitState->AP < TotalAP)
	{
		Log(FString::Printf(TEXT("⚠️ Недостаточно AP для перемещения баррикады (требуется %d AP, доступно %d AP)!"), TotalAP, UnitState->AP));
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
	Barricade->SetActorLocationAndRotation(FVector(Target.X, Target.Y, Barricade->GetActorLocation().Z), FRotator(0.f, Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	RegisterBarricadeCells(Barricade);
	// The operative turns to face the barricade.
	UnitState->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(Cell - UnitState->GridPos));
	AlignFacing(Unit, UnitState->Facing);
	Log(FString::Printf(TEXT("🧱 %s развернул(а) баррикаду (поворот %d°, потрачено %d AP)."), *NameOf(Unit),
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
	Post(TEXT("ТАКТИКА"), TEXT("Перемещение объекта отменено."));
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
	Log(TEXT("⚠️ Рядом нет горючей бочки!"));
	return false;
}

bool UTurnBasedCombatSubsystem::RelocateObject(const FIntPoint& ObjectCell, const FIntPoint& Target, int32 CustomAPCost)
{
	AOperativeCharacter* Unit = GetActiveUnit();
	FTurnUnitState* UnitState = States.Find(Unit);
	if (bSquadUnitMoving || !UnitState || !Grid || !Grid->IsValidCell(ObjectCell) || !Grid->IsValidCell(Target)
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
		Log(TEXT("⚠️ Боец должен стоять вплотную к объекту, чтобы переместить его!"));
		return false;
	}
	TArray<FIntPoint> Path = Grid->FindPath(ObjectCell, Target, 20);
	if (Path.IsEmpty())
	{
		const FIntPoint Step = Target - ObjectCell;
		if (Step.X * Step.X + Step.Y * Step.Y != 1)
		{
			Log(TEXT("⚠️ Нет проходимого пути к выбранной клетке!"));
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
		Log(FString::Printf(TEXT("⚠️ Недостаточно AP для перемещения объекта (требуется %d AP, доступно %d AP)!"), TotalAP, UnitState->AP));
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
		}
	}

	bSquadUnitMoving = true;
	RefreshOverlay();
	StartMover(Object, ObjectCell, Path, SquadStepDuration, nullptr, nullptr, /*bFaceSteps*/ false);
	TWeakObjectPtr<AOperativeCharacter> WeakUnit(Unit);
	StartMover(Unit, UnitStart, SoldierPath, SquadStepDuration, nullptr, [this, WeakUnit, FinalFacing]()
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
	const TCHAR* ObjectName = Type == EGorkyOccupantType::Barrel ? TEXT("бочку") : TEXT("турель");
	Log(FString::Printf(TEXT("📦 %s переместил(а) %s на новую позицию (потрачено %d AP)."), *NameOf(Unit), ObjectName, TotalAP));
	Changed();
	return true;
}

// --- Deployables on the grid ----------------------------------------------------------------------------------------

FTurnDeployCheck UTurnBasedCombatSubsystem::CanPlaceDeployable(EDeployableType Type, const FIntPoint& Cell, float Yaw) const
{
	FTurnDeployCheck Check;
	if (!Grid || !Grid->IsValidCell(Cell))
	{
		Check.Reason = TEXT("Клетка вне тактической сетки");
		return Check;
	}
	const FTurnUnitState* UnitState = GetUnitState(GetActiveUnit());
	if (!UnitState)
	{
		Check.Reason = TEXT("Нет активного бойца");
		return Check;
	}
	const int32 DeployCost = Type == EDeployableType::Mine ? 2 : 3;
	Check.APCost = DeployCost;
	if (UnitState->AP < DeployCost)
	{
		Check.Reason = FString::Printf(TEXT("Недостаточно AP для сборки (требуется %d, есть %d)"), DeployCost, UnitState->AP);
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
			Check.Reason = TEXT("Недопустимое положение баррикады");
			return Check;
		}
		for (const FIntPoint& C : Cells)
		{
			if (!Grid->IsValidCell(C))
			{
				Check.Reason = TEXT("Баррикада выходит за границы сетки");
				return Check;
			}
			if (C == UnitPos)
			{
				Check.Reason = TEXT("Баррикада задевает самого бойца");
				return Check;
			}
			if (Grid->GetOccupant(C))
			{
				Check.Reason = FString::Printf(TEXT("Клетка (%d, %d) занята"), C.X, C.Y);
				return Check;
			}
			if (!Grid->IsCellWalkable(C))
			{
				Check.Reason = FString::Printf(TEXT("Препятствие на клетке (%d, %d)"), C.X, C.Y);
				return Check;
			}
		}
	}
	else
	{
		Cells = { Cell };
		if (!Grid->IsCellWalkable(Cell))
		{
			Check.Reason = TEXT("Клетка заблокирована препятствием");
			return Check;
		}
		if (Grid->GetOccupant(Cell))
		{
			Check.Reason = TEXT("Клетка уже занята");
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
		Check.Reason = TEXT("Нет свободных клеток рядом с объектом для сборки");
		return Check;
	}
	const int32 MaxWalkAP = UnitState->AP - DeployCost;
	const TMap<FIntPoint, int32> Reach = Grid->GetReachableCells(UnitPos, MaxWalkAP);
	Candidates.StableSort([&UnitPos, &Chebyshev](const FIntPoint& A, const FIntPoint& B) { return Chebyshev(A, UnitPos) < Chebyshev(B, UnitPos); });
	int32 BestCost = MAX_int32;
	for (const FIntPoint& Candidate : Candidates)
	{
		const int32* Cost = Reach.Find(Candidate);
		if (Cost && *Cost < BestCost)
		{
			TArray<FIntPoint> Path = Grid->FindPath(UnitPos, Candidate, MaxWalkAP);
			if (!Path.IsEmpty())
			{
				BestCost = *Cost;
				Check.StandCell = Candidate;
				Check.Path = MoveTemp(Path);
			}
		}
	}
	if (Check.Path.IsEmpty())
	{
		Check.Reason = TEXT("Слишком далеко для перехода и установки за текущий ход");
		return Check;
	}
	Check.bCanPlace = true;
	Check.APCost = BestCost + DeployCost;
	Check.Reason = TEXT("OK");
	return Check;
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
	if (!Check.bCanPlace || !UnitState || bSquadUnitMoving || !IsValid(Spawned))
	{
		return false;
	}
	const FString Name = Type == EDeployableType::Turret ? TEXT("Турель") : (Type == EDeployableType::Barricade ? TEXT("Баррикада") : TEXT("Мина"));
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
		Log(FString::Printf(TEXT("🛠️ %s собрал(а) и установил(а) %s (-%d AP)."), *NameOf(Unit), *Name, TotalAP));
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
	StartMover(Unit, From, Actual, SquadStepDuration, nullptr, [this, WeakUnit, WeakSpawned, WeakMine, MineCell, StandCell, Cell, Yaw, Type, Name, TotalAP, FaceTarget]()
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
		State->AP -= TotalAP;
		Log(FString::Printf(TEXT("🛠️ %s подошел(а) и установил(а) %s (-%d AP)."), *NameOf(Moved), *Name, TotalAP));
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
	if (!IsActive() || Phase != ETurnPhase::Squad || bSquadUnitMoving || !Grid)
	{
		return false;
	}
	const FVector Local = WorldPoint - Grid->OriginWorld;
	const bool bOnGrid = Local.X >= 0.f && Local.Y >= 0.f && Local.X < TurnGridCells * TurnCellSize && Local.Y < TurnGridCells * TurnCellSize;
	if (!bOnGrid)
	{
		Post(TEXT("Инженерия"), TEXT("⚠️ Точка установки вне тактической зоны!"));
		return false;
	}
	const FIntPoint Cell = Grid->WorldToGrid(WorldPoint);
	AOperativeCharacter* Unit = GetActiveUnit();
	if (!Unit)
	{
		Post(TEXT("Инженерия"), TEXT("⚠️ Нет активного бойца для установки объекта!"));
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
		Post(Unit->DisplayName.ToString(), FString::Printf(TEXT("⚠️ У отряда нет в наличии: %s!"), *URelocationSubsystem::GetDeployableName(Type).ToString()));
		return true; // placement ends (Godot _cancel_placement_mode)
	}
	const FTurnDeployCheck Check = CanPlaceDeployable(Type, Cell, Yaw);
	if (!Check.bCanPlace)
	{
		Post(TEXT("Инженерия"), FString::Printf(TEXT("⚠️ %s!"), *Check.Reason));
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
		Post(TEXT("Инженерия"), TEXT("⚠️ Не удалось создать объект на сцене!"));
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
	Post(TEXT("Инженерия"), TEXT("⚠️ Ошибка при установке объекта на тактической сетке!"));
	return false;
}

// --- Turrets and enemies ------------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::EndSquadPhase()
{
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
			Log(FString::Printf(TEXT("⚠️ Внимание: Сектор [%s] оголён (1 ход без прикрытия)!"), *Name));
		}
		else if (Result.Turns[Quadrant] == 2)
		{
			Log(FString::Printf(TEXT("🚨 ОПАСНОСТЬ: Сектор [%s] оголён 2 хода! На следующем ходу возможен прорыв врага!"), *Name));
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
	FTurnUnitState& State = States.Add(Enemy);
	State.Actor = Enemy;
	State.GridPos = Cell;
	State.MaxAP = State.AP = Balance.EnemyMaxAP;
	State.Armor = 2.f;
	State.BaseDamage = Balance.EnemyBaseDamage;
	State.Facing = EGorkyFacing::North;
	AlignFacing(Enemy, State.Facing);
	Log(FString::Printf(TEXT("🚨 ПРОРЫВ! Оголённая зона [%s] осталась без прикрытия! Прибыло подкрепление: %s!"), *QuadrantName, *NameOf(Enemy)));
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
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTurretTracer(Turret->GetActorLocation() + FVector(0.f, 0.f, 70.f), Best->GetActorLocation());
	}
	if (bHit)
	{
		const int32 Damage = FMath::RoundToInt(Balance.TurretDamage);
		ApplyDamage(Best, Damage, TEXT("Турель"));
		Log(FString::Printf(TEXT("🔫 Турель произвела залп по %s (-%d HP)! [Меткость: %d%%]"), *NameOf(Best), Damage, FMath::RoundToInt(Chance * 100.f)));
		if (IsDead(Best) && EnemyState)
		{
			OnEnemyKilled(Best, EnemyState->GridPos);
		}
	}
	else
	{
		Log(FString::Printf(TEXT("❌ Промах турели по %s! Шанс: %d%% (выпало: %d%%)"), *NameOf(Best), FMath::RoundToInt(Chance * 100.f),
			FMath::RoundToInt(Roll * 100.f)));
	}
	After(0.55f, [this]() { ProcessNextTurret(); });
}

void UTurnBasedCombatSubsystem::ExecuteEnemyPhase()
{
	Phase = ETurnPhase::Enemies;
	EnemyQueue.Reset();
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

	// Nearest living operative (Chebyshev).
	AActor* Target = nullptr;
	FIntPoint TargetPos = FIntPoint::ZeroValue;
	int32 BestDistance = TNumericLimits<int32>::Max();
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
	{
		const FTurnUnitState* MemberState = GetUnitState(Member.Get());
		if (MemberState && !IsDead(Member.Get()))
		{
			const int32 Distance = TurnBasedRules::CellDistance(Pos, MemberState->GridPos);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Target = Member.Get();
				TargetPos = MemberState->GridPos;
			}
		}
	}
	if (!Target)
	{
		FinishEnemyTurn(0.25f);
		return;
	}
	// Fire blocks the way to the nearest one: look for another operative it can reach.
	if (!Fear.IsEmpty() && Grid->FindPathToAdjacent(Pos, TargetPos, State->AP + 8, true, Fear).IsEmpty())
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Squad)
		{
			const FTurnUnitState* MemberState = GetUnitState(Member.Get());
			if (Member.Get() != Target && MemberState && !Grid->FindPathToAdjacent(Pos, MemberState->GridPos, State->AP + 8, true, Fear).IsEmpty())
			{
				Target = Member.Get();
				TargetPos = MemberState->GridPos;
				break;
			}
		}
	}

	const bool bOrthogonal = TurnManhattan(Pos, TargetPos) == 1;
	if (Overlay)
	{
		Overlay->SetCells(ETurnOverlayLayer::Warning, { TargetPos }); // Godot show_target_warning
	}

	bool bStoppedByFear = false;
	if (!bOrthogonal && State->AP > 0)
	{
		TArray<FIntPoint> Path = Grid->FindPathToAdjacent(Pos, TargetPos, State->AP, true, Fear);
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
					Log(FString::Printf(TEXT("🐺 %s подошел к границе пламени, но боится огня и рычит!"), *NameOf(Moved)));
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
		Log(FString::Printf(TEXT("🐺 %s боится огня горящей бочки и не может подойти ближе!"), *NameOf(Enemy)));
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
	if (TurnManhattan(State->GridPos, TargetPos) != 1 || State->AP < 2 || !TargetState || IsDead(Target))
	{
		EnemyRetreat(Enemy, TargetPos);
		return;
	}
	State->AP -= 2;
	State->Facing = FGorky17Utils::VectorToFacing(TurnStepDir(TargetPos - State->GridPos));
	AlignFacing(Enemy, State->Facing);
	const FGorkyArcResult Arc = FGorky17Utils::CalculateAttackArc(State->GridPos, TargetPos, TargetState->Facing);
	const int32 Damage = TurnBasedRules::EnemyAttackDamage(State->BaseDamage, Arc.DamageMultiplier,
		TurnBasedRules::StanceDamageMultiplier(TargetState->Stance, Balance));
	Changed();
	TWeakObjectPtr<AActor> WeakEnemy(Enemy), WeakTarget(Target);
	// Godot: 1.3 s of the yellow warning square, then the bite; the step back follows 0.35 s later.
	After(1.3f, [this, WeakEnemy, WeakTarget, Damage, TargetPos]()
	{
		AActor* Attacker = WeakEnemy.Get();
		AActor* Victim = WeakTarget.Get();
		if (Attacker && Victim && States.Contains(Victim))
		{
			ApplyDamage(Victim, Damage, NameOf(Attacker));
			Log(FString::Printf(TEXT("🐺 Враг %s атаковал %s: %d урона!"), *NameOf(Attacker), *NameOf(Victim), Damage));
			if (IsDead(Victim))
			{
				OnSquadMemberKilled(Victim, TargetPos);
			}
		}
		if (CheckBattleEnd())
		{
			return;
		}
		After(0.35f, [this, WeakEnemy, TargetPos]()
		{
			if (AActor* Attacker2 = WeakEnemy.Get())
			{
				EnemyRetreat(Attacker2, TargetPos);
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
	After(Delay, [this]() { ProcessNextEnemy(); });
}

// --- Explosions, deaths, end --------------------------------------------------------------------------------------

void UTurnBasedCombatSubsystem::DetonateBarrel(const FIntPoint& Cell, AActor* Barrel)
{
	Log(TEXT("💥 БОЧКА ВЗОРВАЛАСЬ И ЗАГОРЕЛАСЬ! Зона поражения 3х3 клетки!"));
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
			const float Damage = State->bSquad
				? FMath::Max(1, FMath::RoundToInt(Balance.BarrelDamage * TurnBasedRules::StanceDamageMultiplier(State->Stance, Balance)))
				: Balance.BarrelDamage;
			ApplyDamage(Occupant, Damage, TEXT("Бочка"));
			if (IsDead(Occupant))
			{
				if (State->bSquad)
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
	// The barrel stays on its cell as an obstacle and burns for 3 rounds.
	if (ABarrelActor* BarrelActor = Cast<ABarrelActor>(Barrel))
	{
		BarrelActor->IgniteForTurnBased();
		BurningBarrels.Add(Barrel, TurnBarrelBurnRounds);
	}
}

void UTurnBasedCombatSubsystem::DetonateMine(const FIntPoint& Cell, AActor* Mine, AActor* Victim)
{
	Log(FString::Printf(TEXT("💣 МИНА СДЕТОНИРОВАЛА под %s!"), *NameOf(Victim)));
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
	ApplyDamage(Victim, Damage, TEXT("Мина"));
	if (IsDead(Victim))
	{
		if (State->bSquad)
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
	if (State->bSquad)
	{
		// Godot: a surviving operative's turn ends 0.75 s after the blast.
		After(0.75f, [this]() { EndCurrentUnitTurn(); });
	}
}

void UTurnBasedCombatSubsystem::OnEnemyKilled(AActor* Enemy, const FIntPoint& Cell)
{
	Log(FString::Printf(TEXT("☠️ Враг %s уничтожен!"), *NameOf(Enemy)));
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
	Log(FString::Printf(TEXT("⚰️ Боец %s пал в бою!"), *NameOf(Member)));
	Grid->ClearOccupant(Cell);
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Member);
	Squad.RemoveAll([Operative](const TWeakObjectPtr<AOperativeCharacter>& Weak) { return Weak.Get() == Operative || !Weak.IsValid(); });
	States.Remove(Member);
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
