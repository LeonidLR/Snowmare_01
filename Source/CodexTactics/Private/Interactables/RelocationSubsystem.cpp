#include "Interactables/RelocationSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TurretActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationGhostActor.h"
#include "Interactables/RelocationRules.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TripwireRules.h"
#include "Interactables/VaultNavigation.h"
#include "NavModifierComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "RelocationSubsystem"

namespace
{
	/** How often a stalled worker gets its move order again, s. */
	constexpr float RetryInterval = 1.f;
	/** Push follow speeds (Godot lerp 24 * dt for position, 16 * dt for yaw). */
	constexpr float PushFollowSpeed = 24.f;
	constexpr float PushTurnSpeed = 16.f;
	/** Set-up arrival distance, cm (Godot 1.8 m; 3.2 m when stuck). */
	constexpr float DeployArriveDistance = 180.f;
	constexpr float DeployArriveDistanceStuck = 320.f;
	/** Godot deploy ghost colours: valid (0.2, 0.95, 0.4), invalid (1.0, 0.2, 0.2). */
	const FLinearColor DeployValidColor = FLinearColor::FromSRGBColor(FColor(51, 242, 102));
	const FLinearColor DeployInvalidColor = FLinearColor::FromSRGBColor(FColor(255, 51, 51));

	/** Worker step-back after setting up, cm (Godot: mine 2.2 m, barricade 1.6 m, turret 1.1 m). */
	float DeployStepBack(EDeployableType Type)
	{
		return Type == EDeployableType::Mine ? 220.f : (Type == EDeployableType::Barricade ? 160.f : 110.f);
	}

	FText NameOf(const AInteractableActor* Object)
	{
		return Object ? Object->DisplayName : LOCTEXT("SomeObject", "the object");
	}
}

bool URelocationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URelocationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URelocationSubsystem, STATGROUP_Tickables);
}

void URelocationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &URelocationSubsystem::HandleGameFlowChanged);
		Flow->OnTacticalPauseReleased.AddDynamic(this, &URelocationSubsystem::HandlePauseReleased);
	}
}

void URelocationSubsystem::Post(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

float URelocationSubsystem::GetRadius(const AOperativeCharacter& Worker) const
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!Flow)
	{
		return Worker.PlacementRadius;
	}
	return RelocationRules::GetPlacementRadius(Flow->GetPhase(), Flow->GetCombatMode(), Flow->GetConfig().PauseOrderRadius,
		Worker.PlacementRadius);
}

FVector URelocationSubsystem::GetOrigin(const AOperativeCharacter& Worker) const
{
	// In the pause the radius is measured from where the worker stood when the pause began.
	FVector Origin = Worker.GetActorLocation();
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->GetPauseOrigin(&Worker, Origin);
	}
	return Origin;
}

bool URelocationSubsystem::CheckLift(const AOperativeCharacter& Worker, const AInteractableActor* Object) const
{
	const UHealthComponent* Health = Worker.HealthComponent;
	const float HealthFraction = Health && Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 1.f;
	const ELiftBlocker Blocker = RelocationRules::GetLiftBlocker(Worker.ColdLevel, HealthFraction, Worker.MaxColdToLiftObjects,
		Worker.MinHealthFractionToLift);
	if (Blocker == ELiftBlocker::None)
	{
		return true;
	}
	const FText Name = Worker.DisplayName;
	if (Blocker == ELiftBlocker::TooCold)
	{
		Post(Name, Object
			? FText::Format(LOCTEXT("TooColdTask", "🥶 {0} is frozen ({1}% cold) and can't lift {2}! Warm the operative up first."),
				Name, FMath::FloorToInt(Worker.ColdLevel), NameOf(Object))
			: FText::Format(LOCTEXT("TooCold", "🥶 {0} is frozen ({1}% cold) and can't lift heavy loads! Warm the operative up by a fire."),
				Name, FMath::FloorToInt(Worker.ColdLevel)));
	}
	else
	{
		const int32 Current = Health ? FMath::FloorToInt(Health->GetCurrentHealth()) : 0;
		const int32 Max = Health ? FMath::FloorToInt(Health->GetMaxHealth()) : 0;
		Post(Name, Object
			? FText::Format(LOCTEXT("WoundedTask", "🩹 {0} is badly wounded ({1}/{2} HP) and can't lift {3}! Heal the operative first."),
				Name, Current, Max, NameOf(Object))
			: FText::Format(LOCTEXT("Wounded", "🩹 {0} is badly wounded ({1}/{2} HP) and can't lift heavy loads! Heal the operative with a medkit."),
				Name, Current, Max));
	}
	return false;
}

bool URelocationSubsystem::StartRelocate(AInteractableActor* Target, AOperativeCharacter* Worker)
{
	if (!Target)
	{
		return false;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && !CanRelocateNow())
	{
		Post(LOCTEXT("HQ", "HQ"), LOCTEXT("NotInCombat",
			"⚠️ Objects can't be moved during combat! Use the tactical pause [SPACE]."));
		return false;
	}
	if (!Target->bCanBeRelocated)
	{
		Post(LOCTEXT("Engineering", "ENGINEERING"), LOCTEXT("Fixed", "This object is fixed in place and can't be moved."));
		return false;
	}
	if (!Worker)
	{
		const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
		Worker = Squad ? Squad->GetLeader() : nullptr;
	}
	if (!Worker || !CheckLift(*Worker, nullptr))
	{
		return false;
	}

	CancelPlacement();
	PlacingObject = Target;
	PlacingWorker = Worker;
	PlacingYaw = Target->GetActorRotation().Yaw;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ghost = GetWorld()->SpawnActor<ARelocationGhostActor>(Target->GetActorLocation(), Target->GetActorRotation(), Params);
	if (Ghost)
	{
		Ghost->CopyFrom(Target->Mesh, Target);
	}

	Post(Worker->DisplayName, FText::Format(LOCTEXT("PlacePrompt",
		"📐 2️⃣ Pick a new spot for {0} within {1}m ({2}). Wheel / [R] - rotate 45°. LMB - confirm (RMB - cancel)."),
		NameOf(Target), FMath::FloorToInt(GetRadius(*Worker) / 100.f), Worker->DisplayName));
	return true;
}

float URelocationSubsystem::GetWorkerGroundZ(const AOperativeCharacter& Worker) const
{
	return Worker.GetActorLocation().Z - Worker.GetSimpleCollisionHalfHeight();
}

float URelocationSubsystem::GetPlacementGroundZ() const
{
	if (const AInteractableActor* Object = PlacingObject.Get())
	{
		return Object->GetActorLocation().Z - Object->Box->GetScaledBoxExtent().Z;
	}
	const AOperativeCharacter* Worker = PlacingWorker.Get();
	return Worker ? GetWorkerGroundZ(*Worker) : 0.f;
}

FText URelocationSubsystem::GetDeployableName(EDeployableType Type)
{
	switch (Type)
	{
	case EDeployableType::Turret: return LOCTEXT("TurretName", "Turret");
	case EDeployableType::Barricade: return LOCTEXT("BarricadeName", "Barricade");
	default: return LOCTEXT("MineName", "Mine");
	}
}

TSubclassOf<ADeployableActor> URelocationSubsystem::GetDeployableClass(EDeployableType Type) const
{
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	if (!GameMode)
	{
		return nullptr;
	}
	switch (Type)
	{
	case EDeployableType::Barricade: return GameMode->BarricadeClass;
	case EDeployableType::Mine: return GameMode->MineClass;
	default: return GameMode->TurretClass;
	}
}

void URelocationSubsystem::SpawnGhostForType(EDeployableType Type)
{
	if (Ghost)
	{
		Ghost->Destroy();
		Ghost = nullptr;
	}
	const TSubclassOf<ADeployableActor> Class = GetDeployableClass(Type);
	if (!Class)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ghost = GetWorld()->SpawnActor<ARelocationGhostActor>(FVector::ZeroVector, FRotator(0.f, PlacingYaw, 0.f), Params);
	if (Ghost)
	{
		Ghost->CopyFromTemplate(Class->GetDefaultObject<ADeployableActor>()->Mesh);
		Ghost->SetColors(DeployValidColor, DeployInvalidColor);
	}
}

void URelocationSubsystem::HandleDeployKey(AOperativeCharacter* Leader)
{
	if (!Leader)
	{
		return;
	}
	if (PlacingType.IsSet())
	{
		// Godot: F while placing switches to the next type.
		const EDeployableType Next = Leader->CycleDeployableType();
		PlacingType = Next;
		DeployStage = 1;
		SpawnGhostForType(Next);
		Post(Leader->DisplayName, FText::Format(LOCTEXT("Switched", "🔄 Selected to deploy: {0} (x{1}) [F - switch]"),
			GetDeployableName(Next), Leader->GetDeployableCount(Next)));
		return;
	}

	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const int32 Total = Leader->TurretsCount + Leader->BarricadesCount + Leader->MinesCount;
	if (Total <= 0)
	{
		// Godot _on_ability_button_pressed: a squad mate hands one item over.
		AOperativeCharacter* Carrier = nullptr;
		EDeployableType CarrierType = EDeployableType::Turret;
		for (EDeployableType Type : { EDeployableType::Turret, EDeployableType::Barricade, EDeployableType::Mine })
		{
			for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
			{
				if (Member != Leader && Member->GetDeployableCount(Type) > 0)
				{
					Carrier = Member;
					CarrierType = Type;
					break;
				}
			}
			if (Carrier)
			{
				break;
			}
		}
		if (!Carrier)
		{
			Post(Leader->DisplayName, LOCTEXT("NoItems", "⚠️ The squad has nothing to deploy (Turrets: 0, Barricades: 0, Mines: 0)!"));
			return;
		}
		Carrier->AddDeployable(CarrierType, -1);
		Leader->AddDeployable(CarrierType, 1);
		Leader->SelectedDeployType = CarrierType;
		Post(Leader->DisplayName, FText::Format(LOCTEXT("HandOver", "🛠️ {0} hands the gear to {1} to deploy!"),
			Carrier->DisplayName, Leader->DisplayName));
	}
	EDeployableType Type = Leader->SelectedDeployType;
	if (Leader->GetDeployableCount(Type) <= 0)
	{
		Type = Leader->CycleDeployableType();
	}
	StartDeployPlacement(Type, Leader);
}

bool URelocationSubsystem::StartDeployPlacement(EDeployableType Type, AOperativeCharacter* Worker)
{
	if (!Worker || Worker->GetDeployableCount(Type) <= 0)
	{
		return false;
	}
	if (!GetDeployableClass(Type))
	{
		Post(Worker->DisplayName, FText::Format(LOCTEXT("NoClass", "⚠️ {0}: this gear type can't be deployed yet."), GetDeployableName(Type)));
		return false;
	}
	CancelPlacement();
	PlacingType = Type;
	PlacingWorker = Worker;
	PlacingYaw = 0.f;
	DeployStage = 1;
	SpawnGhostForType(Type);
	Post(LOCTEXT("Engineering", "ENGINEERING"), FText::Format(LOCTEXT("DeployPrompt",
		"1️⃣ Click the map to pick a position ({0}). 2️⃣ Rotate 45° with the wheel. 3️⃣ Click again to deploy!"),
		GetDeployableName(Type)));
	if (Type == EDeployableType::Mine)
	{
		const bool bSapper = Worker->SquadRole == EOperativeRole::MedicSapper;
		const float ColdPenalty = Worker->ColdLevel / 100.f * 20.f;
		const FText ColdInfo = ColdPenalty > 0.1f
			? FText::Format(LOCTEXT("FrostInfo", " (Frost: +{0}%)"), FText::AsNumber(ColdPenalty, &FNumberFormattingOptions().SetMaximumFractionalDigits(1)))
			: FText::GetEmpty();
		Post(LOCTEXT("Engineering", "ENGINEERING"), FText::Format(LOCTEXT("MineRisk",
			"💣 Detonation risk while placing: {0}% ({1}{2}). A sapper's base chance is only 2%, but the cold raises the risk of a slip!"),
			FText::AsNumber(DeployableRules::GetMineMishapChance(bSapper, Worker->ColdLevel), &FNumberFormattingOptions().SetMaximumFractionalDigits(1)),
			bSapper ? LOCTEXT("Sapper", "Sapper") : LOCTEXT("Regular", "Regular operative"), ColdInfo));
	}
	return true;
}

void URelocationSubsystem::ExecuteDeploy(AOperativeCharacter* Worker, EDeployableType Type, const FVector& GroundPoint, float Yaw, bool bSprint)
{
	if (!Worker)
	{
		return;
	}
	FDeployTask& Task = DeployTasks.AddDefaulted_GetRef();
	Task.Worker = Worker;
	Task.Type = Type;
	Task.Target = GroundPoint;
	Task.Yaw = Yaw;
	Task.bSprint = bSprint;
	Worker->OrderMoveTo(GroundPoint, bSprint);
	Post(Worker->DisplayName, bSprint ? LOCTEXT("DeployRunning", "Running to the deploy point!") : LOCTEXT("DeployMoving", "Moving to the deploy point!"));
}

AOperativeCharacter* URelocationSubsystem::PickPreparationWorker(AOperativeCharacter* Fallback, EDeployableType Type, const FVector& GroundPoint)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return Fallback;
	}
	const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	TArray<FVector> Positions;
	TArray<bool> Available;
	for (const AOperativeCharacter* Member : Members)
	{
		const bool bBusy = DeployTasks.ContainsByPredicate([Member](const FDeployTask& Task) { return Task.Worker.Get() == Member; });
		Positions.Add(Member->GetActorLocation());
		Available.Add(Member->HealthComponent && Member->HealthComponent->IsAlive() && !Member->bCarrying && !Member->IsVaulting()
			&& !Member->IsPanicking() && !Member->IsRaging() && !bBusy);
	}
	const int32 Index = RelocationRules::ChooseNearestWorker(Positions, Available, GroundPoint);
	AOperativeCharacter* Chosen = Members.IsValidIndex(Index) ? Members[Index] : nullptr;
	if (!Chosen || Chosen == Fallback)
	{
		return Fallback;
	}
	if (Chosen->GetDeployableCount(Type) <= 0)
	{
		// The squad's items are shared: the one who opened the inventory (or whoever carries one) hands it over.
		AOperativeCharacter* Carrier = Fallback && Fallback->GetDeployableCount(Type) > 0 ? Fallback : nullptr;
		for (AOperativeCharacter* Member : Members)
		{
			if (!Carrier && Member->GetDeployableCount(Type) > 0)
			{
				Carrier = Member;
			}
		}
		if (!Carrier)
		{
			return Fallback;
		}
		Carrier->AddDeployable(Type, -1);
		Chosen->AddDeployable(Type, 1);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Preparation: %s (closest, %.1f m) sets up the %s"), *Chosen->DisplayName.ToString(),
		FVector::Dist2D(Chosen->GetActorLocation(), GroundPoint) / 100.f, *GetDeployableName(Type).ToString());
	return Chosen;
}

bool URelocationSubsystem::TickDeploy(FDeployTask& Task, float DeltaTime)
{
	AOperativeCharacter* Worker = Task.Worker.Get();
	if (!Worker)
	{
		return true;
	}
	Task.RetryTime -= DeltaTime;
	const float Distance = FVector::Dist2D(Worker->GetActorLocation(), Task.Target);
	const bool bStalled = !Worker->IsMoving();
	if (Distance > DeployArriveDistance && !(bStalled && Distance <= DeployArriveDistanceStuck))
	{
		if (bStalled && Task.RetryTime <= 0.f)
		{
			Task.RetryTime = RetryInterval;
			Worker->OrderMoveTo(Task.Target, Task.bSprint);
		}
		return false;
	}

	Worker->StopOperative();
	const TSubclassOf<ADeployableActor> Class = GetDeployableClass(Task.Type);
	if (!Class || Worker->GetDeployableCount(Task.Type) <= 0)
	{
		return true;
	}
	Worker->AddDeployable(Task.Type, -1);
	const float HalfHeight = Class->GetDefaultObject<ADeployableActor>()->Box->GetUnscaledBoxExtent().Z;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADeployableActor* Placed = GetWorld()->SpawnActor<ADeployableActor>(Class, Task.Target + FVector(0.f, 0.f, HalfHeight),
		FRotator(0.f, Task.Yaw, 0.f), Params);
	const FText Name = Worker->DisplayName;

	if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Placed))
	{
		const bool bSapper = Worker->SquadRole == EOperativeRole::MedicSapper;
		const float Chance = DeployableRules::GetMineMishapChance(bSapper, Worker->ColdLevel);
		const FNumberFormattingOptions OneDigit = FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		if (FMath::FRand() * 100.f < Chance)
		{
			Post(Name, FText::Format(LOCTEXT("Mishap", "💥 MINE-LAYING ERROR! {0}'s detonator slipped (Risk: {1}%, Cold: {2}%)! The mine went off while placing!"),
				Name, FText::AsNumber(Chance, &OneDigit), FMath::FloorToInt(Worker->ColdLevel)));
			UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("💥 FUZE SLIPPED!"), FLinearColor(1.f, 0.2f, 0.1f));
			Mine->Reveal();
			Mine->Detonate();
			return true;
		}
		Mine->SetPlacedBySquad();
		UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("💣 MINE PLACED"), FLinearColor(0.3f, 0.9f, 0.4f));
		const float ColdPenalty = Worker->ColdLevel / 100.f * 20.f;
		Post(Name, FText::Format(LOCTEXT("MinePlaced", "💣 Anti-personnel mine placed (arms in 3.0s, slip risk was {0}%{1})!"),
			FText::AsNumber(Chance, &OneDigit),
			ColdPenalty > 0.1f ? FText::Format(LOCTEXT("ColdInfo", " (Cold +{0}%)"), FText::AsNumber(ColdPenalty, &OneDigit)) : FText::GetEmpty()));
	}
	else if (Task.Type == EDeployableType::Barricade)
	{
		Post(Name, LOCTEXT("BarricadePlaced", "Barricade assembled and placed!"));
	}
	else
	{
		Post(Name, LOCTEXT("TurretPlaced", "Auto-turret deployed!"));
	}

	// Step back from the new object (Godot safe_dist), crouch behind a barricade once combat is unlocked.
	FVector Away = (Worker->GetActorLocation() - Task.Target).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Worker->GetActorForwardVector();
	}
	const FVector Spot = Task.Target + Away * DeployStepBack(Task.Type);
	Worker->TeleportTo(FVector(Spot.X, Spot.Y, Worker->GetActorLocation().Z), Worker->GetActorRotation(), false, true);
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Task.Type == EDeployableType::Barricade && Flow && Flow->GetPhase() != ECodexGamePhase::Exploration)
	{
		Worker->SetStance(EOperativeStance::Crouching);
		UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("🛡️ IN COVER (-35% damage)"), FLinearColor(0.3f, 0.9f, 1.f));
	}
	return true;
}

void URelocationSubsystem::UpdatePreview(const FVector& GroundPoint)
{
	if (bPlacingTripwire)
	{
		UpdateTripwirePreview(GroundPoint);
		return;
	}
	if (PlacingType.IsSet())
	{
		const AOperativeCharacter* DeployWorker = PlacingWorker.Get();
		const TSubclassOf<ADeployableActor> Class = GetDeployableClass(*PlacingType);
		if (!DeployWorker || !Ghost || !Class)
		{
			return;
		}
		const FVector Point = DeployStage == 2 ? DeployAnchor : GroundPoint;
		const float HalfHeight = Class->GetDefaultObject<ADeployableActor>()->Box->GetUnscaledBoxExtent().Z;
		Ghost->SetActorLocationAndRotation(FVector(Point.X, Point.Y, GetWorkerGroundZ(*DeployWorker) + HalfHeight), FRotator(0.f, PlacingYaw, 0.f));
		// Godot: validity only matters in the pause (tactical radius); a fixed spot is always shown valid.
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const bool bPause = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
		bGhostValid = DeployStage == 2 || !bPause || RelocationRules::IsWithinRadius(GetOrigin(*DeployWorker), Point, GetRadius(*DeployWorker));
		Ghost->SetValid(bGhostValid);
		return;
	}
	const AInteractableActor* Object = PlacingObject.Get();
	const AOperativeCharacter* Worker = PlacingWorker.Get();
	if (!Object || !Worker || !Ghost)
	{
		return;
	}
	Ghost->SetActorLocationAndRotation(FVector(GroundPoint.X, GroundPoint.Y, Object->GetActorLocation().Z), FRotator(0.f, PlacingYaw, 0.f));
	bGhostValid = RelocationRules::IsWithinRadius(GetOrigin(*Worker), GroundPoint, GetRadius(*Worker));
	Ghost->SetValid(bGhostValid);
}

void URelocationSubsystem::RotatePreview(int32 Steps)
{
	PlacingYaw = FRotator::NormalizeAxis(PlacingYaw + Steps * RelocationRules::RotationStep);
	if (Ghost)
	{
		Ghost->SetActorRotation(FRotator(0.f, PlacingYaw, 0.f));
	}
}

void URelocationSubsystem::CancelPlacement()
{
	bPlacingTripwire = false;
	TripwireStage = 1;
	if (TripwirePreview)
	{
		TripwirePreview->Destroy();
		TripwirePreview = nullptr;
	}
	PlacingType.Reset();
	DeployStage = 1;
	PlacingObject.Reset();
	PlacingWorker.Reset();
	if (Ghost)
	{
		Ghost->Destroy();
		Ghost = nullptr;
	}
}

void URelocationSubsystem::ConfirmPlacement(const FVector& GroundPoint)
{
	if (bPlacingTripwire)
	{
		ConfirmTripwirePoint(GroundPoint);
		return;
	}
	if (PlacingType.IsSet())
	{
		AOperativeCharacter* DeployWorker = PlacingWorker.Get();
		if (!DeployWorker)
		{
			CancelPlacement();
			return;
		}
		const UGameFlowSubsystem* DeployFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const bool bDeployPause = DeployFlow && DeployFlow->GetCombatMode() == ECodexCombatMode::TacticalPause;
		if (DeployStage == 1)
		{
			const float Radius = GetRadius(*DeployWorker);
			if (bDeployPause && !RelocationRules::IsWithinRadius(GetOrigin(*DeployWorker), GroundPoint, Radius))
			{
				Post(DeployWorker->DisplayName, FText::Format(LOCTEXT("DeployTooFar",
					"Too far! Deploy only inside the tactical zone ({0} m)."), FMath::FloorToInt(Radius / 100.f)));
				return;
			}
			DeployAnchor = FVector(GroundPoint.X, GroundPoint.Y, GetWorkerGroundZ(*DeployWorker));
			DeployStage = 2;
			Post(LOCTEXT("Engineering", "ENGINEERING"), LOCTEXT("Anchored",
				"📐 Position locked! Rotate with the mouse wheel (45°) and LMB to confirm (RMB - reset)."));
			return;
		}
		const EDeployableType Type = *PlacingType;
		if (bDeployPause)
		{
			PlannedDeploys.RemoveAll([DeployWorker](const FDeployTask& Plan) { return Plan.Worker.Get() == DeployWorker; });
			FDeployTask& Plan = PlannedDeploys.AddDefaulted_GetRef();
			Plan.Worker = DeployWorker;
			Plan.Type = Type;
			Plan.Target = DeployAnchor;
			Plan.Yaw = PlacingYaw;
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->SpawnWaypointMarker(DeployAnchor);
			}
			if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
			{
				Squad->ClearPlannedOrder(DeployWorker);
			}
			Post(DeployWorker->DisplayName, FText::Format(LOCTEXT("DeployPlanned", "📋 [PLAN] Deployment planned ({0})!"),
				GetDeployableName(Type)));
		}
		else if (DeployFlow && DeployFlow->GetPhase() == ECodexGamePhase::Preparation)
		{
			// User decision 2026-10-05: saving preparation time — the closest free operative runs there.
			ExecuteDeploy(PickPreparationWorker(DeployWorker, Type, DeployAnchor), Type, DeployAnchor, PlacingYaw, true);
		}
		else
		{
			ExecuteDeploy(DeployWorker, Type, DeployAnchor, PlacingYaw);
		}
		CancelPlacement();
		return;
	}

	AInteractableActor* Object = PlacingObject.Get();
	AOperativeCharacter* Worker = PlacingWorker.Get();
	if (!Object || !Worker)
	{
		CancelPlacement();
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bPause = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const FVector Target(GroundPoint.X, GroundPoint.Y, Object->GetActorLocation().Z);

	if (bPause)
	{
		const float Radius = GetRadius(*Worker);
		if (!RelocationRules::IsWithinRadius(GetOrigin(*Worker), Target, Radius))
		{
			// Godot keeps placement mode open so the player can pick another spot.
			Post(Worker->DisplayName, FText::Format(LOCTEXT("OutOfZone", "Point is outside the tactical zone ({0} m)!"),
				FMath::FloorToInt(Radius / 100.f)));
			return;
		}
		PlannedTasks.RemoveAll([Worker](const FRelocateTask& Task) { return Task.Worker.Get() == Worker; });
		FRelocateTask& Plan = PlannedTasks.AddDefaulted_GetRef();
		Plan.Worker = Worker;
		Plan.Object = Object;
		Plan.Target = Target;
		Plan.TargetYaw = PlacingYaw;
		Plan.GroundZ = Object->GetActorLocation().Z;
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnWaypointMarker(Target);
		}
		// Godot clears the planned move of the worker: the relocation replaces it (and a planned use order, 2026-10-08).
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			Squad->ClearPlannedOrder(Worker);
		}
		if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
		{
			Interactions->CancelUseOrder(Worker);
		}
		Post(Worker->DisplayName, FText::Format(LOCTEXT("Planned", "📋 [PLAN] {0}: Relocation of {1} to a new position planned! [SPACE - execute]"),
			Worker->DisplayName, NameOf(Object)));
	}
	else if (CanRelocateNow())
	{
		ExecuteRelocate(Worker, Object, Target, PlacingYaw);
	}
	else
	{
		Post(LOCTEXT("HQ", "HQ"), LOCTEXT("ForbiddenInCombat", "⚠️ Moving objects is forbidden during combat! Use the tactical pause [SPACE]."));
	}
	CancelPlacement();
}

void URelocationSubsystem::ExecuteRelocate(AOperativeCharacter* Worker, AInteractableActor* Object, const FVector& TargetLocation, float TargetYaw,
	bool bPlannedInPause)
{
	if (!Worker || !Object)
	{
		return;
	}
	Tasks.RemoveAll([Worker, Object](const FRelocateTask& Task) { return Task.Worker.Get() == Worker || Task.Object.Get() == Object; });
	FRelocateTask& Task = Tasks.AddDefaulted_GetRef();
	Task.Worker = Worker;
	Task.Object = Object;
	Task.Target = TargetLocation;
	Task.TargetYaw = TargetYaw;
	Task.GroundZ = Object->GetActorLocation().Z;
	Task.Stage = 1;
	Task.bPlannedInPause = bPlannedInPause;
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->CancelUseOrder(Worker); // the carry replaces a walk-up to light a barrel
	}
	SyncRelocatingTags();
	Worker->OrderMoveTo(Object->GetApproachPoint(Worker->GetActorLocation()), false);
	Post(Worker->DisplayName, FText::Format(LOCTEXT("MovingOut", "Moving out to carry {0} to the new position!"), NameOf(Object)));
}

void URelocationSubsystem::SyncRelocatingTags()
{
	for (int32 Index = TaggedObjects.Num() - 1; Index >= 0; --Index)
	{
		AInteractableActor* Object = TaggedObjects[Index].Get();
		if (!Object || !Tasks.ContainsByPredicate([Object](const FRelocateTask& Task) { return Task.Object.Get() == Object; }))
		{
			if (Object)
			{
				Object->Tags.Remove(VaultNavigation::RelocatingTag);
			}
			TaggedObjects.RemoveAt(Index);
		}
	}
	for (const FRelocateTask& Task : Tasks)
	{
		AInteractableActor* Object = Task.Object.Get();
		if (Object && !Object->ActorHasTag(VaultNavigation::RelocatingTag))
		{
			Object->Tags.Add(VaultNavigation::RelocatingTag);
			TaggedObjects.Add(Object);
		}
	}
}

bool URelocationSubsystem::HasPlannedTask(const AOperativeCharacter* Worker) const
{
	return PlannedTasks.ContainsByPredicate([Worker](const FRelocateTask& Task) { return Task.Worker.Get() == Worker; });
}

void URelocationSubsystem::ClearPlannedTask(const AOperativeCharacter* Worker)
{
	PlannedTasks.RemoveAll([Worker](const FRelocateTask& Task) { return Task.Worker.Get() == Worker; });
}

void URelocationSubsystem::HandlePauseReleased()
{
	TArray<FRelocateTask> Plans = MoveTemp(PlannedTasks);
	PlannedTasks.Reset();
	for (const FRelocateTask& Plan : Plans)
	{
		ExecuteRelocate(Plan.Worker.Get(), Plan.Object.Get(), Plan.Target, Plan.TargetYaw, /*bPlannedInPause*/ true);
	}
	TArray<FDeployTask> Deploys = MoveTemp(PlannedDeploys);
	PlannedDeploys.Reset();
	for (const FDeployTask& Plan : Deploys)
	{
		ExecuteDeploy(Plan.Worker.Get(), Plan.Type, Plan.Target, Plan.Yaw);
	}
	TArray<FTripwireTask> Wires = MoveTemp(PlannedTripwires);
	PlannedTripwires.Reset();
	for (const FTripwireTask& Plan : Wires)
	{
		ExecuteTripwire(Plan.Worker.Get(), Plan.A, Plan.B, Plan.bAOnObject, Plan.bBOnObject, false);
	}
}

void URelocationSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::RealTime)
	{
		PlannedTasks.Reset();
		PlannedDeploys.Reset();
		PlannedTripwires.Reset();
	}
	if (!CanRelocateNow() && PlacingObject.IsValid())
	{
		CancelPlacement();
	}
}

void URelocationSubsystem::SetObjectCarried(AInteractableActor& Object, bool bCarried) const
{
	// A carried object must not block the worker or rebuild the NavMesh every frame.
	Object.Box->SetCollisionEnabled(bCarried ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	Object.Box->SetCanEverAffectNavigation(!bCarried);
	// A vaultable barricade marks its spot with a «vault» nav area the squad's filter avoids: carried along in front of the
	// worker it blocked his own path, so the push crawled (bug 2026-10-08, PauseBarrelOrderSmoke: 28 s for 5 m).
	if (UNavModifierComponent* Modifier = Object.FindComponentByClass<UNavModifierComponent>())
	{
		Modifier->SetCanEverAffectNavigation(!bCarried);
	}
}

void URelocationSubsystem::StepBack(AOperativeCharacter& Worker, const AInteractableActor& Object, float Distance) const
{
	FVector Away = (Worker.GetActorLocation() - Object.GetActorLocation()).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Worker.GetActorForwardVector();
	}
	const FVector Spot = Object.GetActorLocation() + Away * Distance;
	Worker.StopOperative();
	Worker.TeleportTo(FVector(Spot.X, Spot.Y, Worker.GetActorLocation().Z), Worker.GetActorRotation(), false, true);
}

bool URelocationSubsystem::TickTask(FRelocateTask& Task, float DeltaTime)
{
	AOperativeCharacter* Worker = Task.Worker.Get();
	AInteractableActor* Object = Task.Object.Get();
	if (!Worker || !Object)
	{
		if (Worker)
		{
			Worker->SetCarrying(false);
		}
		if (Object)
		{
			SetObjectCarried(*Object, false);
		}
		return true;
	}
	Task.RetryTime -= DeltaTime;
	const bool bStalled = !Worker->IsMoving();

	if (Task.Stage == 1)
	{
		// Stage 1: walk up to the object.
		const float Distance = FVector::Dist2D(Worker->GetActorLocation(), Object->GetActorLocation());
		if (Distance <= RelocationRules::ReachDistance || (bStalled && Distance <= RelocationRules::ReachDistanceStuck))
		{
			if (!CheckLift(*Worker, Object))
			{
				Worker->StopOperative();
				return true;
			}
			Task.Stage = 2;
			SetObjectCarried(*Object, true);
			Worker->SetCarrying(true);
			const FVector Forward = Worker->GetActorForwardVector().GetSafeNormal2D();
			Object->SetActorRotation(FRotator(0.f, Worker->GetActorRotation().Yaw, 0.f));
			const FVector Push = Worker->GetActorLocation() + Forward * GetPushOffset(*Worker, *Object, Forward);
			Object->SetActorLocation(FVector(Push.X, Push.Y, Task.GroundZ));
			Worker->OrderMoveTo(Task.Target, false);
			Post(Worker->DisplayName, FText::Format(LOCTEXT("Pushing", "Bracing against {0}, pushing it to the new position!"), NameOf(Object)));
		}
		else if (bStalled && Task.RetryTime <= 0.f)
		{
			Task.RetryTime = RetryInterval;
			Worker->OrderMoveTo(Object->GetApproachPoint(Worker->GetActorLocation()), false);
		}
		return false;
	}

	// Stage 2: the object rides in front of the worker.
	const FVector Forward = Worker->GetActorForwardVector().GetSafeNormal2D();
	const float Offset = GetPushOffset(*Worker, *Object, Forward);
	const FVector Push = Worker->GetActorLocation() + Forward * Offset;
	const FVector Current = Object->GetActorLocation();
	FVector Next = FMath::Lerp(Current, FVector(Push.X, Push.Y, Task.GroundZ), FMath::Clamp(PushFollowSpeed * DeltaTime, 0.f, 1.f));
	// Anti-clipping (Sprint 06-F): the follow lag must never let the object close in on the worker — clamp it out to the
	// push distance along the worker's facing.
	const FVector ToNext = FVector(Next.X - Worker->GetActorLocation().X, Next.Y - Worker->GetActorLocation().Y, 0.f);
	if (FVector::DotProduct(ToNext, Forward) < Offset)
	{
		const FVector Side = ToNext - Forward * FVector::DotProduct(ToNext, Forward);
		const FVector Clamped = Worker->GetActorLocation() + Forward * Offset + Side;
		Next = FVector(Clamped.X, Clamped.Y, Task.GroundZ);
	}
	const float Yaw = FMath::Lerp(Object->GetActorRotation().Yaw,
		Object->GetActorRotation().Yaw + FMath::FindDeltaAngleDegrees(Object->GetActorRotation().Yaw, Worker->GetActorRotation().Yaw),
		FMath::Clamp(PushTurnSpeed * DeltaTime, 0.f, 1.f));
	Object->SetActorLocationAndRotation(Next, FRotator(0.f, Yaw, 0.f));

	const float Distance = FVector::Dist2D(Worker->GetActorLocation(), Task.Target);
	if (Distance <= RelocationRules::ArriveDistance || (bStalled && Distance <= RelocationRules::ArriveDistanceStuck))
	{
		// Stage 3: set it down at the chosen spot, restore collision, step back.
		Object->SetActorLocationAndRotation(FVector(Task.Target.X, Task.Target.Y, Task.GroundZ), FRotator(0.f, Task.TargetYaw, 0.f));
		SetObjectCarried(*Object, false);
		Worker->SetCarrying(false);
		StepBack(*Worker, *Object, RelocationRules::StepBackPlaced);
		Post(Worker->DisplayName, FText::Format(LOCTEXT("Placed", "{0} is set at the new position!"), NameOf(Object)));
		UE_LOG(LogCodexTactics, Display, TEXT("Relocated %s to (%.0f, %.0f)"), *Object->GetName(), Task.Target.X, Task.Target.Y);
		return true;
	}
	if (bStalled && Task.RetryTime <= 0.f)
	{
		Task.RetryTime = RetryInterval;
		Worker->OrderMoveTo(Task.Target, false);
	}
	return false;
}

void URelocationSubsystem::DropAllForMine()
{
	if (PlacingObject.IsValid())
	{
		CancelPlacement();
	}
	DropAllTasks(LOCTEXT("MineAhead", "⚠️ Mine ahead! Dropping {0} and stopping!"));
}

bool URelocationSubsystem::CanRelocateNow() const
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	return !Flow || RelocationRules::CanRelocateNow(Flow->GetPhase(), Flow->GetCombatMode(), Leader && Leader->bInCameraZone);
}

void URelocationSubsystem::DropAllForCombat()
{
	if (PlacingObject.IsValid())
	{
		CancelPlacement();
	}
	DropAllTasks(LOCTEXT("Alarm", "⚠️ Combat alert! Dropping {0} and taking a defensive position!"));
}

bool URelocationSubsystem::CancelActiveTask(AOperativeCharacter* Worker)
{
	if (!Worker)
	{
		return false;
	}
	bool bCancelled = false;
	for (int32 Index = Tasks.Num() - 1; Index >= 0; --Index)
	{
		FRelocateTask& Task = Tasks[Index];
		if (Task.Worker.Get() != Worker)
		{
			continue;
		}
		AInteractableActor* Object = Task.Object.Get();
		if (Task.Stage == 2 && Object)
		{
			// Pushing: set it down here, on its ground.
			const FVector Location = Object->GetActorLocation();
			Object->SetActorLocation(FVector(Location.X, Location.Y, Task.GroundZ));
			SetObjectCarried(*Object, false);
			Worker->SetCarrying(false);
			Worker->StopOperative();
			StepBack(*Worker, *Object, RelocationRules::StepBackDropped);
		}
		else
		{
			Worker->SetCarrying(false);
			Worker->StopOperative();
		}
		Tasks.RemoveAt(Index);
		bCancelled = true;
	}
	for (int32 Index = DeployTasks.Num() - 1; Index >= 0; --Index)
	{
		if (DeployTasks[Index].Worker.Get() == Worker)
		{
			Worker->StopOperative();
			DeployTasks.RemoveAt(Index);
			bCancelled = true;
		}
	}
	if (bCancelled)
	{
		Post(Worker->DisplayName, LOCTEXT("Cancelled", "❌ Object delivery cancelled."));
		UE_LOG(LogCodexTactics, Display, TEXT("%s: relocation / deploy cancelled (RMB)"), *Worker->DisplayName.ToString());
	}
	return bCancelled;
}

float URelocationSubsystem::GetPushOffset(const AOperativeCharacter& Worker, const AInteractableActor& Object, const FVector& Forward)
{
	// Sprint 06-F: the fixed 135 cm put the worker 15 cm inside a 150 cm barricade. The box's extent along the push
	// direction (in its own frame) + the capsule radius + a 25 cm margin; the old value is the floor.
	if (!Object.Box)
	{
		return RelocationRules::PushOffset;
	}
	const FVector Extent = Object.Box->GetScaledBoxExtent();
	const FVector Local = Object.GetActorRotation().UnrotateVector(Forward.GetSafeNormal2D());
	const float Along = FMath::Abs(Local.X) * Extent.X + FMath::Abs(Local.Y) * Extent.Y;
	const float Radius = Worker.GetCapsuleComponent() ? Worker.GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
	return FMath::Max(RelocationRules::PushOffset, Radius + Along + 25.f);
}

void URelocationSubsystem::DropTask(FRelocateTask& Task, const FText& LineFormat)
{
	AOperativeCharacter* Worker = Task.Worker.Get();
	AInteractableActor* Object = Task.Object.Get();
	if (Object)
	{
		const FVector Location = Object->GetActorLocation();
		Object->SetActorLocation(FVector(Location.X, Location.Y, Task.GroundZ));
		SetObjectCarried(*Object, false);
	}
	if (Worker)
	{
		Worker->SetCarrying(false);
		if (Object)
		{
			StepBack(*Worker, *Object, RelocationRules::StepBackDropped);
		}
		Post(Worker->DisplayName, FText::Format(LineFormat, NameOf(Object)));
	}
}

void URelocationSubsystem::DropAllTasks(const FText& LineFormat)
{
	for (FRelocateTask& Task : Tasks)
	{
		DropTask(Task, LineFormat);
	}
	Tasks.Reset();
}

void URelocationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	for (int32 Index = DeployTasks.Num() - 1; Index >= 0; --Index)
	{
		if (TickDeploy(DeployTasks[Index], DeltaTime))
		{
			DeployTasks.RemoveAt(Index);
		}
	}
	for (int32 Index = TripwireTasks.Num() - 1; Index >= 0; --Index)
	{
		if (TickTripwire(TripwireTasks[Index], DeltaTime))
		{
			TripwireTasks.RemoveAt(Index);
		}
	}
	SyncRelocatingTags();
	if (Tasks.IsEmpty())
	{
		return;
	}
	// Live combat drops the carries started outside the pause; a carry ordered in the pause runs on after the release
	// (bug 2026-10-08: the resume dropped it the next frame, so the paused order never ran).
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const bool bZoneSolo = Leader && Leader->bInCameraZone;
	for (int32 Index = Tasks.Num() - 1; Index >= 0; --Index)
	{
		if (Flow && RelocationRules::ShouldDropActiveTask(Flow->GetPhase(), Flow->GetCombatMode(), bZoneSolo, Tasks[Index].bPlannedInPause))
		{
			DropTask(Tasks[Index], LOCTEXT("Alarm", "⚠️ Combat alert! Dropping {0} and taking a defensive position!"));
			Tasks.RemoveAt(Index);
			continue;
		}
		if (TickTask(Tasks[Index], DeltaTime))
		{
			Tasks.RemoveAt(Index);
		}
	}
}

// --- Tripwire (Sprint 09) ---

int32 URelocationSubsystem::GetSquadGrenades() const
{
	int32 Total = 0;
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Total += Member->HealthComponent && Member->HealthComponent->IsAlive() ? FMath::Max(0, Member->GrenadesCount) : 0;
		}
	}
	return Total;
}

bool URelocationSubsystem::TakeSquadGrenades(AOperativeCharacter* First, int32 Count)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad || GetSquadGrenades() < Count)
	{
		return false;
	}
	TArray<AOperativeCharacter*> Order = Squad->GetMembers();
	if (First)
	{
		Order.Remove(First);
		Order.Insert(First, 0);
	}
	for (AOperativeCharacter* Member : Order)
	{
		const int32 Taken = FMath::Min(Count, FMath::Max(0, Member->GrenadesCount));
		Member->GrenadesCount -= Taken;
		Count -= Taken;
		if (Count <= 0)
		{
			break;
		}
	}
	return true;
}

bool URelocationSubsystem::StartTripwirePlacement(AOperativeCharacter* Worker)
{
	if (!Worker)
	{
		return false;
	}
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	if (TurnBased && TurnBased->IsActive())
	{
		Post(Worker->DisplayName, LOCTEXT("TripwireTurnBased", "⚠️ A tripwire can't be set in turn-based combat."));
		return false;
	}
	if (GetSquadGrenades() < TripwireRules::GrenadeCost)
	{
		Post(Worker->DisplayName, LOCTEXT("TripwireNoGrenades", "⚠️ A tripwire needs 2 grenades in the squad!"));
		return false;
	}
	CancelPlacement();
	bPlacingTripwire = true;
	TripwireStage = 1;
	PlacingWorker = Worker;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TripwirePreview = GetWorld()->SpawnActor<ATripwireActor>(Worker->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (TripwirePreview)
	{
		TripwirePreview->SetActorHiddenInGame(true); // shown once the first anchor is set
	}
	Post(LOCTEXT("Engineering", "ENGINEERING"), LOCTEXT("TripwirePrompt",
		"🪤 Tripwire (2 grenades): 1️⃣ click - first anchor (clamp on an object, stake in the snow), 2️⃣ click - second anchor (1-5 m)."));
	return true;
}

bool URelocationSubsystem::HasAnchorObject(const FVector& GroundPoint) const
{
	// Something blocking at wire height right there (a tree, a pole, a wall, a barricade): the bracket goes on it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TripwireAnchor), false);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	if (TripwirePreview)
	{
		Params.AddIgnoredActor(TripwirePreview);
	}
	return GetWorld()->OverlapAnyTestByChannel(GroundPoint + FVector(0.f, 0.f, TripwireRules::WireHeightCm), FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(25.f), Params);
}

bool URelocationSubsystem::IsTripwireValid(const FVector& A, const FVector& B) const
{
	if (!TripwireRules::IsSpanValid(FVector::Dist2D(A, B)))
	{
		return false;
	}
	// A wall across the span (anchor objects at the very ends are fine: the trace stops 30 cm short of them).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TripwireSpan), false);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	if (TripwirePreview)
	{
		Params.AddIgnoredActor(TripwirePreview);
	}
	const FVector Up(0.f, 0.f, TripwireRules::WireHeightCm);
	const FVector Dir = (B - A).GetSafeNormal2D();
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, A + Up + Dir * 30.f, B + Up - Dir * 30.f, ECC_Visibility, Params);
}

void URelocationSubsystem::UpdateTripwirePreview(const FVector& GroundPoint)
{
	const AOperativeCharacter* Worker = PlacingWorker.Get();
	if (!Worker || !TripwirePreview)
	{
		return;
	}
	const FVector Point(GroundPoint.X, GroundPoint.Y, GetWorkerGroundZ(*Worker));
	if (TripwireStage == 1)
	{
		// Before the first anchor: a short stub at the cursor.
		TripwirePreview->SetActorHiddenInGame(false);
		TripwirePreview->Setup(Point - FVector(5.f, 0.f, 0.f), Point + FVector(5.f, 0.f, 0.f), HasAnchorObject(Point), false, true);
		return;
	}
	bTripwireValid = IsTripwireValid(TripwireA, Point);
	TripwirePreview->Setup(TripwireA, Point, bTripwireAOnObject, HasAnchorObject(Point), true);
	TripwirePreview->SetPreviewValid(bTripwireValid);
}

void URelocationSubsystem::ConfirmTripwirePoint(const FVector& GroundPoint)
{
	AOperativeCharacter* Worker = PlacingWorker.Get();
	if (!Worker)
	{
		CancelPlacement();
		return;
	}
	const FVector Point(GroundPoint.X, GroundPoint.Y, GetWorkerGroundZ(*Worker));
	if (TripwireStage == 1)
	{
		TripwireA = Point;
		bTripwireAOnObject = HasAnchorObject(Point);
		TripwireStage = 2;
		return;
	}
	if (!IsTripwireValid(TripwireA, Point))
	{
		Post(Worker->DisplayName, LOCTEXT("TripwireInvalid", "⚠️ Tripwire: 1 to 5 m, with no wall in the way."));
		return;
	}
	const bool bBOnObject = HasAnchorObject(Point);
	// Rigged by the medic-sapper when he is there and free (Sprint 09-B), else by the one who opened it.
	AOperativeCharacter* Rigger = Worker;
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const bool bBusy = TripwireTasks.ContainsByPredicate([Member](const FTripwireTask& Task) { return Task.Worker.Get() == Member; });
			if (Member->SquadRole == EOperativeRole::MedicSapper && Member->HealthComponent && Member->HealthComponent->IsAlive() && !bBusy
				&& !Member->IsPanicking() && !Member->IsRaging())
			{
				Rigger = Member;
			}
		}
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const FVector A = TripwireA;
	const bool bAOnObject = bTripwireAOnObject;
	CancelPlacement();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		FTripwireTask& Plan = PlannedTripwires.AddDefaulted_GetRef();
		Plan.Worker = Rigger;
		Plan.A = A;
		Plan.B = Point;
		Plan.bAOnObject = bAOnObject;
		Plan.bBOnObject = bBOnObject;
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnWaypointMarker((A + Point) * 0.5f);
		}
		Post(Rigger->DisplayName, LOCTEXT("TripwirePlanned", "📋 [PLAN] The tripwire will be set after the pause."));
		return;
	}
	ExecuteTripwire(Rigger, A, Point, bAOnObject, bBOnObject, Flow && Flow->GetPhase() == ECodexGamePhase::Preparation);
}

void URelocationSubsystem::ExecuteTripwire(AOperativeCharacter* Worker, const FVector& GroundA, const FVector& GroundB, bool bAOnObject,
	bool bBOnObject, bool bSprint)
{
	if (!Worker)
	{
		return;
	}
	FTripwireTask& Task = TripwireTasks.AddDefaulted_GetRef();
	Task.Worker = Worker;
	Task.A = GroundA;
	Task.B = GroundB;
	Task.bAOnObject = bAOnObject;
	Task.bBOnObject = bBOnObject;
	Task.bSprint = bSprint;
	// He works from beside the middle of the wire, on his own side (never astride it).
	const FVector Mid = (GroundA + GroundB) * 0.5f;
	FVector Side = FVector::CrossProduct((GroundB - GroundA).GetSafeNormal2D(), FVector::UpVector);
	if (FVector::DotProduct(Side, Worker->GetActorLocation() - Mid) < 0.f)
	{
		Side = -Side;
	}
	Task.WorkPoint = Mid + Side * 90.f;
	Worker->OrderMoveTo(Task.WorkPoint, bSprint);
	Post(Worker->DisplayName, LOCTEXT("TripwireMoving", "🪤 Moving to set the tripwire!"));
}

bool URelocationSubsystem::TickTripwire(FTripwireTask& Task, float DeltaTime)
{
	AOperativeCharacter* Worker = Task.Worker.Get();
	if (!Worker || !Worker->HealthComponent || !Worker->HealthComponent->IsAlive())
	{
		return true;
	}
	if (Task.RigLeft < 0.f)
	{
		Task.RetryTime -= DeltaTime;
		const float Distance = FVector::Dist2D(Worker->GetActorLocation(), Task.WorkPoint);
		if (Distance > 120.f && !(Distance <= 250.f && !Worker->IsMoving()))
		{
			if (!Worker->IsMoving() && Task.RetryTime <= 0.f)
			{
				Task.RetryTime = 1.f;
				Worker->OrderMoveTo(Task.WorkPoint, Task.bSprint);
			}
			return false;
		}
		Worker->StopOperative();
		Worker->SetFacingPoint((Task.A + Task.B) * 0.5f);
		Worker->SetStance(EOperativeStance::Crouching);
		Task.RigLeft = TripwireRules::RigSeconds;
		Post(Worker->DisplayName, LOCTEXT("TripwireRigging", "🪤 Stringing the wire, screwing in the MUV fuze..."));
		return false;
	}
	Task.RigLeft -= DeltaTime;
	if (Task.RigLeft > 0.f)
	{
		return false;
	}
	if (!TakeSquadGrenades(Worker, TripwireRules::GrenadeCost))
	{
		Post(Worker->DisplayName, LOCTEXT("TripwireNoGrenadesLate", "⚠️ Not enough grenades - can't set the tripwire."));
		return true;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATripwireActor* Wire = GetWorld()->SpawnActor<ATripwireActor>((Task.A + Task.B) * 0.5f, FRotator::ZeroRotator, Params))
	{
		Wire->Setup(Task.A, Task.B, Task.bAOnObject, Task.bBOnObject, false, Worker);
		Post(Worker->DisplayName, LOCTEXT("TripwireDone", "🪤 Tripwire set (arms in 1.5 s). You can crawl under it."));
		UE_LOG(LogCodexTactics, Display, TEXT("Tripwire rigged by %s: %.1f m"), *Worker->DisplayName.ToString(), FVector::Dist2D(Task.A, Task.B) / 100.f);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
