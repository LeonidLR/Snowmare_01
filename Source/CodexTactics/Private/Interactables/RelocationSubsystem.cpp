#include "Interactables/RelocationSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
	if (Flow && !RelocationRules::CanRelocateNow(Flow->GetPhase(), Flow->GetCombatMode()))
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

float URelocationSubsystem::GetPlacementGroundZ() const
{
	const AInteractableActor* Object = PlacingObject.Get();
	return Object ? Object->GetActorLocation().Z - Object->Box->GetScaledBoxExtent().Z : 0.f;
}

void URelocationSubsystem::UpdatePreview(const FVector& GroundPoint)
{
	const AInteractableActor* Object = PlacingObject.Get();
	const AOperativeCharacter* Worker = PlacingWorker.Get();
	if (!Object || !Worker || !Ghost)
	{
		return;
	}
	Ghost->SetActorLocationAndRotation(FVector(GroundPoint.X, GroundPoint.Y, Object->GetActorLocation().Z), FRotator(0.f, PlacingYaw, 0.f));
	Ghost->SetValid(RelocationRules::IsWithinRadius(GetOrigin(*Worker), GroundPoint, GetRadius(*Worker)));
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
		// Godot clears the planned move of the worker: the relocation replaces it.
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			Squad->ClearPlannedOrder(Worker);
		}
		Post(Worker->DisplayName, FText::Format(LOCTEXT("Planned", "📋 [ПЛАН] {0}: Запланирован перенос ({1}) на новую позицию! [ПРОБЕЛ — исполнить]"),
			Worker->DisplayName, NameOf(Object)));
	}
	else if (!Flow || RelocationRules::CanRelocateNow(Flow->GetPhase(), Flow->GetCombatMode()))
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
}

void URelocationSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::RealTime)
	{
		PlannedTasks.Reset();
	}
	if (!RelocationRules::CanRelocateNow(Phase, CombatMode))
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
			const FVector Push = Worker->GetActorLocation() + Forward * RelocationRules::PushOffset;
			Object->SetActorLocationAndRotation(FVector(Push.X, Push.Y, Task.GroundZ), FRotator(0.f, Worker->GetActorRotation().Yaw, 0.f));
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
	const FVector Push = Worker->GetActorLocation() + Forward * RelocationRules::PushOffset;
	const FVector Current = Object->GetActorLocation();
	const FVector Next = FMath::Lerp(Current, FVector(Push.X, Push.Y, Task.GroundZ), FMath::Clamp(PushFollowSpeed * DeltaTime, 0.f, 1.f));
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

void URelocationSubsystem::DropAllTasks()
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
			Post(Worker->DisplayName, FText::Format(LOCTEXT("Alarm", "⚠️ Боевая тревога! Бросаю {0} и занимаю оборону!"), NameOf(Object)));
		}
	}
	Tasks.Reset();
}

void URelocationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Tasks.IsEmpty())
	{
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && !RelocationRules::CanRelocateNow(Flow->GetPhase(), Flow->GetCombatMode()))
	{
		DropAllTasks();
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
