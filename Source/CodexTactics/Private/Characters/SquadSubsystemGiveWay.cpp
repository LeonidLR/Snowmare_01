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
#include "Formation/RampFileRules.h"
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

bool USquadSubsystem::RampAxisUnder(const FVector& Point, FVector2D& OutUphill) const
{
	OutUphill = FVector2D::ZeroVector;
	const UWorld* World = GetWorld();
	if (!World || !IsRampUnder(FVector2D(Point), static_cast<float>(Point.Z)))
	{
		return false;
	}
	// The axis: the ramp component's up vector (one axis per flight, as IsOperativeOnRamp), else the floor normal.
	FHitResult Hit;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadGiveWayRampAxis), false);
	if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, 100.f), Point - FVector(0.f, 0.f, 400.f), Objects, Params))
	{
		const UPrimitiveComponent* Ramp = Hit.GetComponent();
		const FVector Up = Ramp ? Ramp->GetUpVector() : FVector::UpVector;
		const FVector2D FromUp = RampFileRules::UphillOf(Up);
		OutUphill = FromUp.IsNearlyZero() || Up.Z > 0.999 ? RampFileRules::UphillOf(Hit.ImpactNormal) : FromUp;
	}
	return true;
}

void USquadSubsystem::ReleaseGiveWaySlotHold(AOperativeCharacter& Walker, const TCHAR* Why)
{
	FGiveWaySlotHold Hold;
	if (!GiveWaySlotHolds.RemoveAndCopyValue(&Walker, Hold))
	{
		return;
	}
	Walker.SetTrafficHold(Hold.Mover.Get(), false);
	UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s walks to his formation slot again: %s"), *Walker.DisplayName.ToString(), Why);
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
		// v2: the flights on it (their approach zones), probed once per order.
		Cached.FlightFeet.Reset();
		Cached.FlightDirections.Reset();
		for (const SquadGiveWayRules::FFlightFoot& Foot : SquadGiveWayRules::FindFlightFeet(Cached.Points,
			[this](const FVector& Point, FVector2D& OutUphill) { return RampAxisUnder(Point, OutUphill); }, SquadGiveWayTuning::Get().RampSampleCm))
		{
			Cached.FlightFeet.Add(Foot.Foot);
			Cached.FlightDirections.Add(Foot.Direction);
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

bool USquadSubsystem::InMoverFlightApproach(const AOperativeCharacter& Mover, const FVector& Point, float ExtraCm)
{
	using namespace SquadSubsystemGiveWayLocal;
	const FSquadGiveWayTuning& Tuning = SquadGiveWayTuning::Get();
	if (CVarSquadGiveWay.GetValueOnGameThread() == 0 || !Tuning.bEnabled || !Mover.HasOwnMoveOrder())
	{
		return false;
	}
	TArray<FVector> Path;
	BuildGiveWayPath(Mover, Path);
	const FGiveWayPath* Cached = GiveWayPaths.Find(&Mover);
	if (!Cached || Cached->FlightFeet.IsEmpty())
	{
		return false;
	}
	TArray<SquadGiveWayRules::FFlightFoot> Feet;
	for (int32 Index = 0; Index < Cached->FlightFeet.Num(); ++Index)
	{
		Feet.Add({ Cached->FlightFeet[Index], Cached->FlightDirections[Index] });
	}
	const SquadGiveWayRules::FFlightApproach Approach = SquadGiveWayRules::SelectApproach(Feet,
		FVector(Mover.GetActorLocation().X, Mover.GetActorLocation().Y, GiveWayFeetZ(Mover)), Tuning);
	return Approach.bValid && FMath::Abs(Point.Z - Approach.FootZ) <= Tuning.SameLevelCm
		&& SquadGiveWayRules::InApproachZone(Approach, FVector2D(Point), ExtraCm, Tuning);
}

void USquadSubsystem::UpdateGiveWay()
{
	using namespace SquadSubsystemGiveWayLocal;
	const FSquadGiveWayTuning& Tuning = SquadGiveWayTuning::Get();
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	// Turn-based combat moves men only by the player's AP orders (API review): no give-way steps there.
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	TArray<TWeakObjectPtr<AOperativeCharacter>> SlotHeld;
	GiveWaySlotHolds.GetKeys(SlotHeld);
	if (CVarSquadGiveWay.GetValueOnGameThread() == 0 || !Tuning.bEnabled || !World || !Nav || (TurnBased && TurnBased->IsActive()))
	{
		GiveWayTargets.Reset();
		for (const TWeakObjectPtr<AOperativeCharacter>& Walker : SlotHeld)
		{
			if (Walker.IsValid())
			{
				ReleaseGiveWaySlotHold(*Walker.Get(), TEXT("give way off"));
			}
		}
		GiveWaySlotHolds.Reset();
		return;
	}
	// v2 (b): the slot walkers held this frame (the rest are released after the movers' loop).
	TSet<AOperativeCharacter*> SlotHoldsKept;
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
		// v2: the flight he walks onto next (its approach zone), from his order's cached path.
		SquadGiveWayRules::FFlightApproach Approach;
		auto EnsurePath = [&]()
		{
			if (bPathBuilt)
			{
				return;
			}
			BuildGiveWayPath(*Mover, Path);
			bPathBuilt = true;
			if (const FGiveWayPath* Cached = GiveWayPaths.Find(Mover); Cached && Cached->FlightFeet.Num() > 0)
			{
				TArray<SquadGiveWayRules::FFlightFoot> Feet;
				for (int32 Index = 0; Index < Cached->FlightFeet.Num(); ++Index)
				{
					Feet.Add({ Cached->FlightFeet[Index], Cached->FlightDirections[Index] });
				}
				Approach = SquadGiveWayRules::SelectApproach(Feet, FVector(Mover->GetActorLocation().X, Mover->GetActorLocation().Y, GiveWayFeetZ(*Mover)), Tuning);
			}
		};
		// v2 (b): a formation walker whose slot walk enters the flight's approach zone waits (a traffic hold) until the mover is up.
		EnsurePath();
		if (Approach.bValid)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& WalkerRef : Members)
			{
				AOperativeCharacter* Walker = WalkerRef.Get();
				if (!Walker || Walker == Mover || !IsGiveWayWalker(Walker) || Walker->HasOwnMoveOrder() || Walker->bInCover || GiveWayTargets.Contains(Walker)
					|| HolderYieldTargets.Contains(Walker) || IsRampHeld(Walker) || FMath::Abs(GiveWayFeetZ(*Walker) - Approach.FootZ) > Tuning.SameLevelCm)
				{
					continue;
				}
				const float Extra = CapsuleRadius(*Walker) + Tuning.ClearanceCm;
				if (const FGiveWaySlotHold* Hold = GiveWaySlotHolds.Find(Walker))
				{
					// Held: kept while the zone is still his slot walk's way and the hold is young.
					if (Hold->Mover.Get() == Mover && Now - Hold->Since < Tuning.SlotHoldMaxSeconds
						&& SquadGiveWayRules::SlotWalkEntersApproach(Approach, FVector2D(Walker->GetActorLocation()), FVector2D(Walker->GetLastMoveDestination()), Extra, Tuning))
					{
						SlotHoldsKept.Add(Walker);
					}
					continue;
				}
				const double* NextTry = GiveWayNextTry.Find(Walker);
				if (!Walker->IsMoving() || Walker->IsTrafficHeld() || (NextTry && *NextTry > Now)
					|| !SquadGiveWayRules::SlotWalkEntersApproach(Approach, FVector2D(Walker->GetActorLocation()), FVector2D(Walker->GetLastMoveDestination()), Extra, Tuning))
				{
					continue;
				}
				Walker->SetTrafficHold(Mover, true);
				if (Walker->GetVelocity().SizeSquared2D() > 25.f)
				{
					Walker->StopOperative();
				}
				FGiveWaySlotHold NewHold;
				NewHold.Mover = Mover;
				NewHold.Since = Now;
				GiveWaySlotHolds.Add(Walker, NewHold);
				SlotHoldsKept.Add(Walker);
				UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s waits: his slot walk to (%.0f, %.0f) crosses %s's approach to the flight foot (%.0f, %.0f) %.0f cm ahead of him"),
					*Walker->DisplayName.ToString(), Walker->GetLastMoveDestination().X, Walker->GetLastMoveDestination().Y, *Mover->DisplayName.ToString(),
					Approach.Foot.X, Approach.Foot.Y, Approach.ToFootCm);
			}
		}
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
			EnsurePath();
			if (Path.Num() < 2)
			{
				break;
			}
			const float IdleFeet = GiveWayFeetZ(*Idle);
			const FVector Feet(Idle->GetActorLocation().X, Idle->GetActorLocation().Y, IdleFeet);
			const FVector2D Feet2D(Feet);
			const float CrossCm = CapsuleRadius(*Mover) + CapsuleRadius(*Idle) + Tuning.ClearanceCm;
			SquadGiveWayRules::FCrossing Crossing = SquadGiveWayRules::FindCrossing(Path, Feet, CrossCm, Tuning);
			// v2 (a): beside the line but in the flight's approach zone (the stair mouth the mover has to fit through) counts too.
			const bool bInZone = Approach.bValid && FMath::Abs(IdleFeet - Approach.FootZ) <= Tuning.SameLevelCm
				&& SquadGiveWayRules::InApproachZone(Approach, Feet2D, CapsuleRadius(*Idle), Tuning);
			if (bInZone)
			{
				Crossing = SquadGiveWayRules::ApproachCrossing(Approach, Feet2D);
			}
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
			// One candidate spot: the reason it is refused (nullptr: free - he steps there).
			auto TrySpot = [&](const FVector2D& Point, const TCHAR* Kind) -> bool
			{
				FNavLocation OnNav;
				FHitResult Hit;
				const TCHAR* Refused = nullptr;
				if (!Nav->ProjectPointToNavigation(FVector(Point.X, Point.Y, IdleFeet), OnNav, FVector(Tuning.NavSnapCm, Tuning.NavSnapCm, Tuning.SameLevelCm)))
				{
					Refused = TEXT("off the navmesh");
				}
				else if (FVector2D::Distance(FVector2D(OnNav.Location), Point) > Tuning.NavSnapCm)
				{
					Refused = TEXT("nav snap too far");
				}
				else if (FMath::Abs(OnNav.Location.Z - IdleFeet) > Tuning.MaxRiseCm)
				{
					Refused = TEXT("rise");
				}
				else if (IsRampUnder(Point, IdleFeet))
				{
					Refused = TEXT("ramp");
				}
				// The navmesh projection may pull the spot back to the path (a wall beside it): still clear of it.
				else if (SquadGiveWayRules::DistanceToPath(Path, FVector2D(OnNav.Location), Crossing.AlongCm + Tuning.LookAheadCm) < CrossCm)
				{
					Refused = TEXT("snapped onto the path");
				}
				else if (Approach.bValid && SquadGiveWayRules::InApproachZone(Approach, FVector2D(OnNav.Location), CapsuleRadius(*Idle), Tuning))
				{
					Refused = TEXT("approach zone");
				}
				else if (Taken.ContainsByPredicate([&Point, CrossCm](const FVector2D& Other) { return FVector2D::Distance(Other, Point) < CrossCm; }))
				{
					Refused = TEXT("taken");
				}
				else if (World->LineTraceSingleByChannel(Hit, Feet + Lift, OnNav.Location + Lift, ECC_Visibility, WalkParams))
				{
					Refused = TEXT("wall trace");
				}
				if (Refused)
				{
					return false;
				}
				const EOperativeOrderResult Result = Idle->FollowTo(OnNav.Location, FormationTuning::Get().PaceWalkSpeed);
				UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s (idle) stands on %s's %s %.0f cm ahead of him: steps %s from (%.0f, %.0f) to (%.0f, %.0f) -> %d"),
					*Idle->DisplayName.ToString(), *Mover->DisplayName.ToString(), bInZone ? TEXT("flight approach") : TEXT("path"), Crossing.AlongCm, Kind, Feet.X, Feet.Y,
					OnNav.Location.X, OnNav.Location.Y, static_cast<int32>(Result));
				if (Result == EOperativeOrderResult::Accepted)
				{
					GiveWayTargets.Add(Idle, FVector2D(OnNav.Location));
				}
				return true;
			};
			for (const FVector2D& Point : SquadGiveWayRules::AsideSpots(Path, Crossing, CrossCm, Tuning, &Feet2D, Approach.bValid ? &Approach : nullptr,
					 CapsuleRadius(*Idle)))
			{
				if (TrySpot(Point, TEXT("aside")))
				{
					bStepped = true;
					break;
				}
			}
			// v3 (X6 B_x2_13): both sides walled - a spot behind the mover, away from his way / the flight.
			if (!bStepped)
			{
				for (const FVector2D& Point : SquadGiveWayRules::BehindSpots(Path, Crossing, CrossCm, Tuning, &Feet2D, Approach.bValid ? &Approach : nullptr,
						 CapsuleRadius(*Idle)))
				{
					if (TrySpot(Point, TEXT("behind")))
					{
						bStepped = true;
						break;
					}
				}
			}
			if (!bStepped)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s (idle) stands on %s's path %.0f cm ahead of him at (%.0f, %.0f): no free spot aside"),
					*Idle->DisplayName.ToString(), *Mover->DisplayName.ToString(), Crossing.AlongCm, Feet.X, Feet.Y);
			}
		}
	}
	// v2 (b): the slot walkers no mover holds any more (he is up / stopped, the walk no longer crosses, the hold timed out) walk on.
	for (const TWeakObjectPtr<AOperativeCharacter>& WalkerRef : SlotHeld)
	{
		AOperativeCharacter* Walker = WalkerRef.Get();
		if (!Walker)
		{
			GiveWaySlotHolds.Remove(WalkerRef);
			continue;
		}
		if (!SlotHoldsKept.Contains(Walker))
		{
			const FGiveWaySlotHold* Hold = GiveWaySlotHolds.Find(Walker);
			const bool bTimedOut = Hold && Now - Hold->Since >= Tuning.SlotHoldMaxSeconds;
			if (bTimedOut)
			{
				GiveWayNextTry.Add(Walker, Now + Tuning.SlotHoldMaxSeconds); // he walks his slot now, not held again at once
			}
			ReleaseGiveWaySlotHold(*Walker, bTimedOut ? TEXT("held too long") : TEXT("the way to the flight is clear"));
		}
	}
}