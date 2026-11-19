// USquadSubsystem: give way (the game designer's bug report 2026-11-02 item 7, "formation stuck at a ramp foot"). Each frame an idle member
// (no own move order, not in cover, no combat action, standing) whose capsule a squadmate's own walk is about to cross (SquadGiveWayRules::
// FindCrossing on the mover's navmesh path, look_ahead_cm ahead) steps aside - perpendicular to that path, onto the navmesh, off the path's
// corridor, off a ramp (SquadGiveWayRules::AsideSpots) - and is idle there. Squad and Solo mode, ramps / stairs / ladder feet / doorways
// alike. Tuning: Content/Data/AI/squad_regroup.json "give_way". CVar Codex.Squad.GiveWay (1; 0 = off). Godot reference: none (UE-only).

#include "Characters/SquadSubsystem.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadGiveWayRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Formation/FormationTuning.h"
#include "HAL/IConsoleManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace SquadSubsystemGiveWayLocal
{
	TAutoConsoleVariable<int32> CVarSquadGiveWay(TEXT("Codex.Squad.GiveWay"), 1,
		TEXT("Bug report 2026-11-02 item 7 ('formation stuck at a ramp foot'): 1 = an idle squadmate whose capsule a squadmate's own walk crosses steps aside (squad_regroup.json give_way); 0 = off (A/B)"));

	bool IsGiveWayWalker(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKnockedDown()
			&& !Operative->IsClimbingLadder() && !VehicleQueries::IsMounted(Operative);
	}

	EPathFollowingStatus::Type GiveWayMoveStatus(const AOperativeCharacter& Operative)
	{
		const AAIController* AI = Cast<AAIController>(Operative.GetController());
		return AI ? AI->GetMoveStatus() : EPathFollowingStatus::Idle;
	}

	bool IsInCombatAction(const AOperativeCharacter& Operative)
	{
		return Operative.IsObjectShotPending() || Operative.IsHoldingForSniperShot() || Operative.IsPeekingRoundCorner() || Operative.IsRaging()
			|| Operative.IsPanicking() || Operative.IsMantling() || Operative.IsVaulting() || Operative.IsProneDiving() || Operative.HasPendingCover()
			|| Operative.IsDroppingOffLedge() || Operative.IsHeldDownByCriticalWound();
	}

	float GiveWayFeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetSimpleCollisionHalfHeight();
	}

	float CapsuleRadius(const AOperativeCharacter& Operative)
	{
		const UCapsuleComponent* Capsule = Operative.GetCapsuleComponent();
		return Capsule ? Capsule->GetScaledCapsuleRadius() : 0.f;
	}
}

void USquadSubsystem::BuildGiveWayPath(const AOperativeCharacter& Mover, TArray<FVector>& OutPath)
{
	OutPath.Reset();
	// The mover's string-pulled navmesh path (the crowd's own keeps only its start and end), once per order / destination.
	FGiveWayPath& Cached = GiveWayPaths.FindOrAdd(&Mover);
	const FVector Destination = Mover.GetLastMoveDestination();
	if (Cached.Serial != Mover.GetMoveOrderSerial() || !Cached.Destination.Equals(Destination, 1.0))
	{
		Cached.Serial = Mover.GetMoveOrderSerial();
		Cached.Destination = Destination;
		Cached.Points.Reset();
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Mover.GetActorLocation(), Destination);
		if (Path && Path->IsValid())
		{
			Cached.Points = Path->PathPoints;
		}
	}
	if (Cached.Points.Num() < 2)
	{
		return;
	}
	// From where he is now: his nearest leg's far end on.
	const FVector2D Here(Mover.GetActorLocation());
	int32 Leg = 0;
	double Best = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index + 1 < Cached.Points.Num(); ++Index)
	{
		const FVector2D A(Cached.Points[Index]);
		const FVector2D B(Cached.Points[Index + 1]);
		const FVector2D AB = B - A;
		const double LengthSq = AB.SizeSquared();
		const double T = LengthSq > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Here - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
		const double Distance = FVector2D::DistSquared(A + AB * T, Here);
		if (Distance < Best)
		{
			Best = Distance;
			Leg = Index;
		}
	}
	OutPath.Add(Mover.GetActorLocation() - FVector(0.f, 0.f, Mover.GetSimpleCollisionHalfHeight()));
	for (int32 Index = Leg + 1; Index < Cached.Points.Num(); ++Index)
	{
		OutPath.Add(Cached.Points[Index]);
	}
}

void USquadSubsystem::UpdateGiveWay()
{
	using namespace SquadSubsystemGiveWayLocal;
	const FSquadGiveWayTuning& Tuning = SquadGiveWayTuning::Get();
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	// Turn-based combat moves men only by the player's AP orders (API review): no give-way steps there.
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (CVarSquadGiveWay.GetValueOnGameThread() == 0 || !Tuning.bEnabled || !World || !Nav || (TurnBased && TurnBased->IsActive()))
	{
		GiveWayTargets.Reset();
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (auto It = GiveWayTargets.CreateIterator(); It; ++It)
	{
		const AOperativeCharacter* Who = It->Key.Get();
		if (!Who || (!Who->IsMoving() && GiveWayMoveStatus(*Who) == EPathFollowingStatus::Idle))
		{
			It.RemoveCurrent(); // there (or stopped, or gone): idle again
		}
	}
	for (auto It = GiveWayPaths.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	// The formation following the leader (squad mode, no hold) moves its followers out of his walk itself (FormationRuntime::YieldFromLeaderWay,
	// the wall layout's own order - GroupCoverSmoke): only another member's walk asks them here.
	const bool bFormationFollows = !bFollowersHolding && !IsSoloMode();
	const AOperativeCharacter* LeaderRef = Leader.Get();
	TArray<FVector> Path;
	for (const TWeakObjectPtr<AOperativeCharacter>& MoverRef : Members)
	{
		AOperativeCharacter* Mover = MoverRef.Get();
		if (bFormationFollows && Mover == LeaderRef)
		{
			continue;
		}
		// A man stepping aside is no mover himself (no cascade / ping-pong, API review).
		if (GiveWayTargets.Contains(MoverRef))
		{
			continue;
		}
		// A walk into cover (CoverSmoke / GroupCoverSmoke: the men round the cover spot are its group, not in his way) moves nobody.
		if (!IsGiveWayWalker(Mover) || !Mover->HasOwnMoveOrder() || GiveWayMoveStatus(*Mover) != EPathFollowingStatus::Moving || Mover->bInCover
			|| Mover->HasPendingCover())
		{
			continue;
		}
		Path.Reset();
		bool bPathBuilt = false;
		for (const TWeakObjectPtr<AOperativeCharacter>& IdleRef : Members)
		{
			AOperativeCharacter* Idle = IdleRef.Get();
			if (!Idle || Idle == Mover || GiveWayTargets.Contains(Idle) || HolderYieldTargets.Contains(Idle))
			{
				continue;
			}
			SquadGiveWayRules::FIdleState State;
			State.bWalker = IsGiveWayWalker(Idle);
			State.bOwnMoveOrder = Idle->HasOwnMoveOrder();
			State.bInCover = Idle->bInCover;
			State.bCombatAction = State.bWalker && IsInCombatAction(*Idle);
			State.bMoving = Idle->IsMoving() || GiveWayMoveStatus(*Idle) != EPathFollowingStatus::Idle;
			State.bHeld = Idle->IsTrafficHeld() || IsRampHeld(Idle);
			State.bAtOwnPost = bFormationFollows && FVector::Dist2D(Idle->GetActorLocation(), Idle->GetLastMoveDestination())
				< CapsuleRadius(*Mover) + CapsuleRadius(*Idle) + Tuning.ClearanceCm;
			const double* NextTry = GiveWayNextTry.Find(Idle);
			if (!SquadGiveWayRules::IsIdle(State) || (NextTry && *NextTry > Now))
			{
				continue;
			}
			if (!bPathBuilt)
			{
				BuildGiveWayPath(*Mover, Path);
				bPathBuilt = true;
			}
			if (Path.Num() < 2)
			{
				break;
			}
			const float IdleFeet = GiveWayFeetZ(*Idle);
			const FVector Feet(Idle->GetActorLocation().X, Idle->GetActorLocation().Y, IdleFeet);
			const float CrossCm = CapsuleRadius(*Mover) + CapsuleRadius(*Idle) + Tuning.ClearanceCm;
			const SquadGiveWayRules::FCrossing Crossing = SquadGiveWayRules::FindCrossing(Path, Feet, CrossCm, Tuning);
			if (!Crossing.bCrosses || Idle->IsBehindBarricade())
			{
				continue; // clear of the path, or holding a barricade (a cover spot without the cover state)
			}
			GiveWayNextTry.Add(Idle, Now + Tuning.RetrySeconds);
			// The spots the others stand on or walk to are taken.
			TArray<FVector2D> Taken;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
			{
				if (Member.IsValid() && Member.Get() != Idle)
				{
					Taken.Add(FVector2D(Member->GetActorLocation()));
				}
			}
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector2D>& Target : GiveWayTargets)
			{
				Taken.Add(Target.Value);
			}
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector2D>& Target : HolderYieldTargets)
			{
				Taken.Add(Target.Value);
			}
			FCollisionQueryParams WalkParams(SCENE_QUERY_STAT(SquadGiveWayWalk), false);
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
			{
				WalkParams.AddIgnoredActor(Member.Get());
			}
			const FVector Lift(0.f, 0.f, Tuning.MaxRiseCm);
			bool bStepped = false;
			for (const FVector2D& Point : SquadGiveWayRules::AsideSpots(Path, Crossing, CrossCm, Tuning))
			{
				FNavLocation OnNav;
				FHitResult Hit;
				if (!Nav->ProjectPointToNavigation(FVector(Point.X, Point.Y, IdleFeet), OnNav, FVector(Tuning.NavSnapCm, Tuning.NavSnapCm, Tuning.SameLevelCm))
					|| FVector2D::Distance(FVector2D(OnNav.Location), Point) > Tuning.NavSnapCm || FMath::Abs(OnNav.Location.Z - IdleFeet) > Tuning.MaxRiseCm
					|| IsRampUnder(Point, IdleFeet)
					// The navmesh projection may pull the spot back to the path (a wall beside it): still clear of it.
					|| SquadGiveWayRules::DistanceToPath(Path, FVector2D(OnNav.Location), Crossing.AlongCm + Tuning.LookAheadCm) < CrossCm
					|| Taken.ContainsByPredicate([&Point, CrossCm](const FVector2D& Other) { return FVector2D::Distance(Other, Point) < CrossCm; })
					|| World->LineTraceSingleByChannel(Hit, Feet + Lift, OnNav.Location + Lift, ECC_Visibility, WalkParams))
				{
					continue;
				}
				const EOperativeOrderResult Result = Idle->FollowTo(OnNav.Location, FormationTuning::Get().PaceWalkSpeed);
				UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s (idle) stands on %s's path %.0f cm ahead of him: steps aside from (%.0f, %.0f) to (%.0f, %.0f) -> %d"),
					*Idle->DisplayName.ToString(), *Mover->DisplayName.ToString(), Crossing.AlongCm, Feet.X, Feet.Y, OnNav.Location.X, OnNav.Location.Y,
					static_cast<int32>(Result));
				if (Result == EOperativeOrderResult::Accepted)
				{
					GiveWayTargets.Add(Idle, FVector2D(OnNav.Location));
				}
				bStepped = true;
				break;
			}
			if (!bStepped)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s (idle) stands on %s's path %.0f cm ahead of him at (%.0f, %.0f): no free spot aside"),
					*Idle->DisplayName.ToString(), *Mover->DisplayName.ToString(), Crossing.AlongCm, Feet.X, Feet.Y);
			}
		}
	}
}