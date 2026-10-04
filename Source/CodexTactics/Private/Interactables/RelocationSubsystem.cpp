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
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/RelocationGhostActor.h"
#include "Interactables/RelocationRules.h"
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
		return Object ? Object->DisplayName : LOCTEXT("SomeObject", "объект");
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
			? FText::Format(LOCTEXT("TooColdTask", "🥶 {0} замерз(ла) ({1}% холода) и не может поднять {2}! Сначала согрейте бойца."),
				Name, FMath::FloorToInt(Worker.ColdLevel), NameOf(Object))
			: FText::Format(LOCTEXT("TooCold", "🥶 {0} замерз(ла) ({1}% холода) и не может поднимать тяжести! Согрейте бойца у огня."),
				Name, FMath::FloorToInt(Worker.ColdLevel)));
	}
	else
	{
		const int32 Current = Health ? FMath::FloorToInt(Health->GetCurrentHealth()) : 0;
		const int32 Max = Health ? FMath::FloorToInt(Health->GetMaxHealth()) : 0;
		Post(Name, Object
			? FText::Format(LOCTEXT("WoundedTask", "🩹 {0} тяжело ранен(а) ({1}/{2} HP) и не может поднять {3}! Сначала вылечите бойца."),
				Name, Current, Max, NameOf(Object))
			: FText::Format(LOCTEXT("Wounded", "🩹 {0} тяжело ранен(а) ({1}/{2} HP) и не может поднимать тяжести! Вылечите бойца аптечкой."),
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
		Post(LOCTEXT("HQ", "ШТАБ"), LOCTEXT("NotInCombat",
			"⚠️ Во время боя менять расположение объектов нельзя! Используйте тактическую паузу [ПРОБЕЛ]."));
		return false;
	}
	if (!Target->bCanBeRelocated)
	{
		Post(LOCTEXT("Engineering", "Инженерия"), LOCTEXT("Fixed", "Этот объект зафиксирован и не может быть перемещен."));
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
		"📐 2️⃣ Укажите новое место для {0} в радиусе {1}м ({2}). Колесико / [R] — поворот на 45°. ЛКМ — подтвердить (ПКМ — отмена)."),
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
	case EDeployableType::Turret: return LOCTEXT("TurretName", "Турель");
	case EDeployableType::Barricade: return LOCTEXT("BarricadeName", "Баррикада");
	default: return LOCTEXT("MineName", "Мина");
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
		Post(Leader->DisplayName, FText::Format(LOCTEXT("Switched", "🔄 Выбрано для установки: {0} ({1} шт.) [F — сменить]"),
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
			Post(Leader->DisplayName, LOCTEXT("NoItems", "⚠️ У отряда нет инженерных средств для установки (Турели: 0, Баррикады: 0, Мины: 0)!"));
			return;
		}
		Carrier->AddDeployable(CarrierType, -1);
		Leader->AddDeployable(CarrierType, 1);
		Leader->SelectedDeployType = CarrierType;
		Post(Leader->DisplayName, FText::Format(LOCTEXT("HandOver", "🛠️ {0} передает снаряжение бойцу {1} для установки!"),
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
		Post(Worker->DisplayName, FText::Format(LOCTEXT("NoClass", "⚠️ {0}: этот тип снаряжения пока нельзя установить."), GetDeployableName(Type)));
		return false;
	}
	CancelPlacement();
	PlacingType = Type;
	PlacingWorker = Worker;
	PlacingYaw = 0.f;
	DeployStage = 1;
	SpawnGhostForType(Type);
	Post(LOCTEXT("Engineering", "Инженерия"), FText::Format(LOCTEXT("DeployPrompt",
		"1️⃣ Кликните на карте для выбора позиции ({0}). 2️⃣ Колесиком поверните на 45°. 3️⃣ Кликните повторно для установки!"),
		GetDeployableName(Type)));
	if (Type == EDeployableType::Mine)
	{
		const bool bSapper = Worker->SquadRole == EOperativeRole::MedicSapper;
		const float ColdPenalty = Worker->ColdLevel / 100.f * 20.f;
		const FText ColdInfo = ColdPenalty > 0.1f
			? FText::Format(LOCTEXT("FrostInfo", " (Мороз: +{0}%)"), FText::AsNumber(ColdPenalty, &FNumberFormattingOptions().SetMaximumFractionalDigits(1)))
			: FText::GetEmpty();
		Post(LOCTEXT("Engineering", "Инженерия"), FText::Format(LOCTEXT("MineRisk",
			"💣 Риск детонации при установке: {0}% ({1}{2}). У сапёра базовая вероятность всего 2%, но холод увеличивает риск срыва!"),
			FText::AsNumber(DeployableRules::GetMineMishapChance(bSapper, Worker->ColdLevel), &FNumberFormattingOptions().SetMaximumFractionalDigits(1)),
			bSapper ? LOCTEXT("Sapper", "Сапёр") : LOCTEXT("Regular", "Обычный боец"), ColdInfo));
	}
	return true;
}

void URelocationSubsystem::ExecuteDeploy(AOperativeCharacter* Worker, EDeployableType Type, const FVector& GroundPoint, float Yaw)
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
	Worker->OrderMoveTo(GroundPoint, false);
	Post(Worker->DisplayName, LOCTEXT("DeployMoving", "Выдвигаюсь на точку для установки!"));
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
			Worker->OrderMoveTo(Task.Target, false);
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
			Post(Name, FText::Format(LOCTEXT("Mishap", "💥 ОШИБКА МИНИРОВАНИЯ! У {0} сорвался детонатор (Риск: {1}%, Холод: {2}%)! Мина сдетонировала при установке!"),
				Name, FText::AsNumber(Chance, &OneDigit), FMath::FloorToInt(Worker->ColdLevel)));
			UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("💥 СРЫВ ВЗРЫВАТЕЛЯ!"), FLinearColor(1.f, 0.2f, 0.1f));
			Mine->Reveal();
			Mine->Detonate();
			return true;
		}
		Mine->SetPlacedBySquad();
		UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("💣 МИНА УСТАНОВЛЕНА"), FLinearColor(0.3f, 0.9f, 0.4f));
		const float ColdPenalty = Worker->ColdLevel / 100.f * 20.f;
		Post(Name, FText::Format(LOCTEXT("MinePlaced", "💣 Противопехотная мина установлена (взведение 3.0с, риск срыва был {0}%{1})!"),
			FText::AsNumber(Chance, &OneDigit),
			ColdPenalty > 0.1f ? FText::Format(LOCTEXT("ColdInfo", " (Холод +{0}%)"), FText::AsNumber(ColdPenalty, &OneDigit)) : FText::GetEmpty()));
	}
	else if (Task.Type == EDeployableType::Barricade)
	{
		Post(Name, LOCTEXT("BarricadePlaced", "Баррикада собрана и установлена!"));
	}
	else
	{
		Post(Name, LOCTEXT("TurretPlaced", "Автоматическая турель развернута!"));
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
		UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("🛡️ В УКРЫТИИ (-35% урона)"), FLinearColor(0.3f, 0.9f, 1.f));
	}
	return true;
}

void URelocationSubsystem::UpdatePreview(const FVector& GroundPoint)
{
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
					"Слишком далеко! Устанавливать можно только внутри тактической зоны ({0} метров)."), FMath::FloorToInt(Radius / 100.f)));
				return;
			}
			DeployAnchor = FVector(GroundPoint.X, GroundPoint.Y, GetWorkerGroundZ(*DeployWorker));
			DeployStage = 2;
			Post(LOCTEXT("Engineering", "Инженерия"), LOCTEXT("Anchored",
				"📐 Позиция зафиксирована! Поверните колесиком мыши (45°) и кликните ЛКМ для подтверждения (ПКМ — сброс)."));
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
			Post(DeployWorker->DisplayName, FText::Format(LOCTEXT("DeployPlanned", "📋 [ПЛАН] Запланирована установка объекта ({0})!"),
				GetDeployableName(Type)));
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
			Post(Worker->DisplayName, FText::Format(LOCTEXT("OutOfZone", "Точка вне тактической зоны ({0} метров)!"),
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
		// Godot clears the planned move of the worker: the relocation replaces it.
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			Squad->ClearPlannedOrder(Worker);
		}
		Post(Worker->DisplayName, FText::Format(LOCTEXT("Planned", "📋 [ПЛАН] {0}: Запланирован перенос ({1}) на новую позицию! [ПРОБЕЛ — исполнить]"),
			Worker->DisplayName, NameOf(Object)));
	}
	else if (CanRelocateNow())
	{
		ExecuteRelocate(Worker, Object, Target, PlacingYaw);
	}
	else
	{
		Post(LOCTEXT("HQ", "ШТАБ"), LOCTEXT("ForbiddenInCombat", "⚠️ Во время боя перемещение объектов запрещено! Используйте тактическую паузу [ПРОБЕЛ]."));
	}
	CancelPlacement();
}

void URelocationSubsystem::ExecuteRelocate(AOperativeCharacter* Worker, AInteractableActor* Object, const FVector& TargetLocation, float TargetYaw)
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
	Worker->OrderMoveTo(Object->GetApproachPoint(Worker->GetActorLocation()), false);
	Post(Worker->DisplayName, FText::Format(LOCTEXT("MovingOut", "Выдвигаюсь, чтобы перенести {0} на новую позицию!"), NameOf(Object)));
}

bool URelocationSubsystem::HasPlannedTask(const AOperativeCharacter* Worker) const
{
	return PlannedTasks.ContainsByPredicate([Worker](const FRelocateTask& Task) { return Task.Worker.Get() == Worker; });
}

void URelocationSubsystem::HandlePauseReleased()
{
	TArray<FRelocateTask> Plans = MoveTemp(PlannedTasks);
	PlannedTasks.Reset();
	for (const FRelocateTask& Plan : Plans)
	{
		ExecuteRelocate(Plan.Worker.Get(), Plan.Object.Get(), Plan.Target, Plan.TargetYaw);
	}
	TArray<FDeployTask> Deploys = MoveTemp(PlannedDeploys);
	PlannedDeploys.Reset();
	for (const FDeployTask& Plan : Deploys)
	{
		ExecuteDeploy(Plan.Worker.Get(), Plan.Type, Plan.Target, Plan.Yaw);
	}
}

void URelocationSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::RealTime)
	{
		PlannedTasks.Reset();
		PlannedDeploys.Reset();
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
			Post(Worker->DisplayName, FText::Format(LOCTEXT("Pushing", "Уперся в {0}, толкаю на новую позицию!"), NameOf(Object)));
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
		Post(Worker->DisplayName, FText::Format(LOCTEXT("Placed", "Объект {0} успешно установлен на новой позиции!"), NameOf(Object)));
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
	DropAllTasks(LOCTEXT("MineAhead", "⚠️ Впереди мина! Бросаю {0} и останавливаюсь!"));
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
	DropAllTasks(LOCTEXT("Alarm", "⚠️ Боевая тревога! Бросаю {0} и занимаю оборону!"));
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
		Post(Worker->DisplayName, LOCTEXT("Cancelled", "❌ Доставка объекта отменена."));
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

void URelocationSubsystem::DropAllTasks(const FText& LineFormat)
{
	for (FRelocateTask& Task : Tasks)
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
	if (Tasks.IsEmpty())
	{
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && !CanRelocateNow())
	{
		DropAllTasks(LOCTEXT("Alarm", "⚠️ Боевая тревога! Бросаю {0} и занимаю оборону!"));
		return;
	}
	for (int32 Index = Tasks.Num() - 1; Index >= 0; --Index)
	{
		if (TickTask(Tasks[Index], DeltaTime))
		{
			Tasks.RemoveAt(Index);
		}
	}
}

#undef LOCTEXT_NAMESPACE
