// ULedgeFallComponent: the soft-lock safety net (user bug 2026-11-03, editor PIE on L_Gen_Depot_1: "When dropping through a hole in the roof
// the characters just got stuck. All three got hard stuck."). After a fall an operative off the navmesh, or on a navmesh island he can never
// walk out of, is put on the nearest point that reaches the main area (FDropLinkReach, FallRules::PickRescuePoint) and his order goes on.
// UE-only system (no Godot reference).

#include "Falls/LedgeFallComponent.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkReach.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderClimbComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace LedgeFallRescueInternal
{
	static TAutoConsoleVariable<int32> CVarFallRescue(
		TEXT("Codex.Fall.Rescue"),
		1,
		TEXT("The soft-lock safety net (user bug 2026-11-03): 1 (default) an operative off the navmesh / in a dead-end pocket after a fall (rescue.*), or ")
		TEXT("whose order cannot start off the navmesh, is put on the nearest navmesh point that reaches the main area; 0 off."),
		ECVF_Default);

	/** The off-navmesh test: a navmesh poly within this box under his feet (the AI's own start projection is about this big). */
	const FVector OnNavExtent(50.f, 50.f, 120.f);
	constexpr float CheckIntervalSeconds = 0.5f;

	const TCHAR* StandingName(FallRules::ENavStanding Standing)
	{
		switch (Standing)
		{
		case FallRules::ENavStanding::OffNavmesh: return TEXT("off the navmesh");
		case FallRules::ENavStanding::DeadEnd: return TEXT("in a dead-end pocket");
		default: return TEXT("fine");
		}
	}
}

void ULedgeFallComponent::ArmRescueWatch()
{
	if (!Cast<AOperativeCharacter>(GetOwner()) || LedgeFallRescueInternal::CVarFallRescue.GetValueOnGameThread() <= 0)
	{
		return; // the squad's soft-lock: the enemies have their own stuck rules
	}
	RescueWatchTime = 0.f;
	RescueBadSeconds = 0.f;
	RescueCheckClock = 0.f;
	bRescueDeadEndCleared = false;
}

bool ULedgeFallComponent::CanBeRescued() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (!Operative || State != ELedgeFallState::None || PendingLink != INDEX_NONE || !Operative->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}
	const UHealthComponent* Health = Operative->FindComponentByClass<UHealthComponent>();
	const ULadderClimbComponent* Ladder = Operative->FindComponentByClass<ULadderClimbComponent>();
	return (!Health || Health->IsAlive()) && !Operative->IsKnockedDown() && !Operative->IsVaulting() && !(Ladder && Ladder->IsBusy())
		&& !VehicleQueries::IsMounted(Operative);
}

FallRules::ENavStanding ULedgeFallComponent::EvaluateStanding(bool bOffNavmeshOnly) const
{
	using FallRules::ENavStanding;
	UWorld* World = GetWorld();
	const ACharacter* Character = GetCharacter();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (!Character || !NavData)
	{
		return ENavStanding::Fine; // no navmesh at all: nothing to judge
	}
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	FNavLocation On;
	if (!Nav->ProjectPointToNavigation(Feet, On, LedgeFallRescueInternal::OnNavExtent, NavData)
		|| FMath::Abs(On.Location.Z - Feet.Z) > LedgeFallRescueInternal::OnNavExtent.Z)
	{
		return ENavStanding::OffNavmesh;
	}
	if (bOffNavmeshOnly)
	{
		return ENavStanding::Fine;
	}
	// A dead end: he gets neither back to where the fall started nor to the squad's spawn (a complete path, not a partial one).
	auto Reaches = [World, &On, Character](const FVector& Goal)
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, On.Location, Goal, const_cast<ACharacter*>(Character));
		return Path && Path->IsValid() && !Path->IsPartial();
	};
	if (!FallStartFeet.IsZero() && Reaches(FallStartFeet))
	{
		return ENavStanding::Fine;
	}
	bool bAnyStart = false;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		bAnyStart = true;
		if (Reaches(It->GetActorLocation()))
		{
			return ENavStanding::Fine;
		}
	}
	return bAnyStart ? ENavStanding::DeadEnd : ENavStanding::Fine; // without a player start the main area is unknown: never a dead end
}

void ULedgeFallComponent::TickRescueWatch(float DeltaTime)
{
	if (RescueWatchTime < 0.f)
	{
		return;
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	RescueWatchTime += DeltaTime;
	if (RescueWatchTime > Config.RescueWatchSeconds)
	{
		RescueWatchTime = -1.f;
		return;
	}
	RescueCheckClock -= DeltaTime;
	if (RescueCheckClock > 0.f)
	{
		return;
	}
	RescueCheckClock = LedgeFallRescueInternal::CheckIntervalSeconds;
	if (!CanBeRescued())
	{
		RescueBadSeconds = 0.f; // a knockdown / a ladder / a vehicle owns him meanwhile
		return;
	}
	const FallRules::ENavStanding Standing = EvaluateStanding(bRescueDeadEndCleared);
	if (Standing == FallRules::ENavStanding::Fine)
	{
		RescueBadSeconds = 0.f;
		bRescueDeadEndCleared = true; // the dear dead-end paths once; the off-navmesh test for the rest of the watch
		return;
	}
	RescueBadSeconds += LedgeFallRescueInternal::CheckIntervalSeconds;
	if (FallRules::ShouldRescue(Standing, RescueBadSeconds, RescueWatchTime, Config))
	{
		RescueNow(Standing, TEXT("after a fall"));
		RescueWatchTime = -1.f;
	}
}

bool ULedgeFallComponent::RescueIfOffNavmesh(const TCHAR* Why, bool bResumeOrder)
{
	const UWorld* World = GetWorld();
	const bool bCooling = World && LastRescueTime >= 0.0 && World->GetTimeSeconds() - LastRescueTime < FallRules::GetConfig().RescueCooldownSeconds;
	if (LedgeFallRescueInternal::CVarFallRescue.GetValueOnGameThread() <= 0 || bCooling || !CanBeRescued()
		|| EvaluateStanding(true) != FallRules::ENavStanding::OffNavmesh)
	{
		return false;
	}
	return RescueNow(FallRules::ENavStanding::OffNavmesh, Why, bResumeOrder);
}

bool ULedgeFallComponent::RescueNow(FallRules::ENavStanding Standing, const TCHAR* Why, bool bResumeOrder)
{
	UWorld* World = GetWorld();
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (!World || !Operative)
	{
		return false;
	}
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	FDropLinkReach Reach;
	FVector Point = FVector::ZeroVector;
	if (!Reach.Build(*World) || !Reach.FindRescuePoint(*World, Feet, Point))
	{
		LastRescueTime = World->GetTimeSeconds(); // the next try waits rescue.cooldown_s too (no graph build every formation tick)
		UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] rescue: %s is %s at %s (%s) but no navmesh point of his level that reaches the main area lies within %.0f cm"),
			*Operative->DisplayName.ToString(), LedgeFallRescueInternal::StandingName(Standing), *Feet.ToCompactString(), Why, FallRules::GetConfig().RescueMaxCm);
		return false;
	}
	// His own order goes on from there (a formation follower is pulled by the squad as before).
	const bool bOwnOrder = bResumeOrder && Operative->HasOwnMoveOrder();
	const FVector Destination = Operative->GetLastMoveDestination();
	if (AAIController* Controller = Cast<AAIController>(Operative->GetController()))
	{
		Controller->StopMovement();
	}
	Operative->TeleportTo(Point + FVector(0.f, 0.f, GetHalfHeight() + 2.f), Operative->GetActorRotation(), false, true);
	Operative->GetCharacterMovement()->StopMovementImmediately();
	++RescueCount;
	LastRescueTime = World->GetTimeSeconds();
	LastRescueFrom = Feet;
	LastRescueTo = Point;
	UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] rescue: %s was %s at %s (%s) - put on %s (%.0f cm away, the reach graph %.1f ms)%s"),
		*Operative->DisplayName.ToString(), LedgeFallRescueInternal::StandingName(Standing), *Feet.ToCompactString(), Why, *Point.ToCompactString(),
		FVector::Dist(Feet, Point), Reach.GetBuildMs(), bOwnOrder ? TEXT("; his order goes on") : TEXT(""));
	if (bOwnOrder && FVector::Dist2D(Destination, Point) > 60.f)
	{
		Operative->ResumeMoveAfterLadder(Destination, true);
	}
	return true;
}
