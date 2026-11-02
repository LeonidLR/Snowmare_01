// USquadSubsystem: the squad on a narrow ramp / stair (the game designer's bug report 2026-11-02 on L_Gen_Depot_1, item 7: "the squad at the
// foot of a ramp, one operative ordered up onto the roof - they all get STUCK at the bottom; ordered again, 2 of 3 went up, 1 stayed").
// (1) The ramp file: members on a ramp or in its mouth walk it single file - the man behind waits until the man ahead is FileGapCm along
// (RampFileRules::FindWaitBehind; an own walk is paused and resumed, a formation follower held by a traffic hold); (2) a follower's slot never
// lies on a ramp while the leader stands on a floor (KeepSlotOffRamp, called by UpdateSectorFollower); (3) holding team-mates (Solo, a
// perimeter hold) standing on the selected man's path step aside (UpdateHolderYield). CVar Codex.Squad.RampFile (1; 0 = the old behaviour).
// docs/systems/squad-and-orders.md "The squad on a narrow ramp". Godot reference: none (UE-only).

#include "Characters/SquadSubsystem.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadFormation.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/RampFileRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace SquadSubsystemRampFileLocal
{
	TAutoConsoleVariable<int32> CVarSquadRampFile(TEXT("Codex.Squad.RampFile"), 1,
		TEXT("Bug report 2026-11-02 item 7 (L_Gen_Depot_1): 1 = the squad walks a narrow ramp / stair single file (the man behind waits), no formation slot on a ramp while the leader stands on a floor, holding team-mates on the selected man's path step aside; 0 = the old behaviour (A/B)"));

	TAutoConsoleVariable<int32> CVarSquadRampFileDebug(TEXT("Codex.Squad.RampFile.Debug"), 0,
		TEXT("1: log why a holding team-mate does not step out of the selected man's path (every check, every frame - debugging only)"));

	const RampFileRules::FRampFileConfig& Config()
	{
		static const RampFileRules::FRampFileConfig Value;
		return Value;
	}

	bool IsTerrain(const UPrimitiveComponent* Component)
	{
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		// By class name: the Landscape module is no dependency of the game module (as URoofCutawaySubsystem).
		return !Component || !Component->IsA<UStaticMeshComponent>() || Component->GetClass()->GetName().Contains(TEXT("Landscape"))
			|| (Owner && Owner->GetClass()->GetName().Contains(TEXT("Landscape")));
	}

	float RampFeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetSimpleCollisionHalfHeight();
	}

	EPathFollowingStatus::Type MoveStatus(const AOperativeCharacter& Operative)
	{
		const AAIController* AI = Cast<AAIController>(Operative.GetController());
		return AI ? AI->GetMoveStatus() : EPathFollowingStatus::Idle;
	}

	/** A member the ramp file / the yield may look at (alive, on foot, not on a ladder, not mounted, not knocked down). */
	bool IsWalker(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKnockedDown()
			&& !Operative->IsClimbingLadder() && !VehicleQueries::IsMounted(Operative);
	}
}

bool USquadSubsystem::IsRampHeld(const AOperativeCharacter* Operative) const
{
	const FRampHoldState* State = Operative ? RampHolds.Find(Operative) : nullptr;
	return State && State->bHeld;
}

bool USquadSubsystem::IsOperativeOnRamp(const AOperativeCharacter& Operative, FVector2D* OutUphill, const UPrimitiveComponent** OutRamp) const
{
	using namespace SquadSubsystemRampFileLocal;
	const UCharacterMovementComponent* Move = Operative.GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround() || !Move->CurrentFloor.bBlockingHit)
	{
		return false;
	}
	const FHitResult& Floor = Move->CurrentFloor.HitResult;
	if (!RampFileRules::IsRampSurface(Floor.ImpactNormal, IsTerrain(Floor.GetComponent()), Config().MinSlopeDeg))
	{
		return false;
	}
	if (OutUphill)
	{
		// One axis per ramp for everyone on it (a box ramp's top face normal is its up vector; the contact normal wobbles at its edges).
		const UPrimitiveComponent* Ramp = Floor.GetComponent();
		const FVector Up = Ramp ? Ramp->GetUpVector() : FVector::UpVector;
		*OutUphill = RampFileRules::UphillOf(RampFileRules::IsRampSurface(Up, false, Config().MinSlopeDeg) ? Up : Floor.ImpactNormal);
	}
	if (OutRamp)
	{
		*OutRamp = Floor.GetComponent();
	}
	return true;
}

bool USquadSubsystem::IsRampUnder(const FVector2D& Point, float FromZ) const
{
	using namespace SquadSubsystemRampFileLocal;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadRampUnder), false);
	if (!World->LineTraceSingleByObjectType(Hit, FVector(Point.X, Point.Y, FromZ + 100.f), FVector(Point.X, Point.Y, FromZ - 400.f), Objects, Params))
	{
		return false;
	}
	return RampFileRules::IsRampSurface(Hit.ImpactNormal, IsTerrain(Hit.GetComponent()), Config().MinSlopeDeg);
}

FVector USquadSubsystem::KeepSlotOffRamp(const AOperativeCharacter& LeaderRef, const FVector& Slot) const
{
	using namespace SquadSubsystemRampFileLocal;
	if (CVarSquadRampFile.GetValueOnGameThread() == 0)
	{
		return Slot;
	}
	const float LeaderFeet = RampFeetZ(LeaderRef);
	if (!RampFileRules::ShouldDropRampSlot(IsOperativeOnRamp(LeaderRef), IsRampUnder(FVector2D(Slot), LeaderFeet)))
	{
		return Slot;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return Slot;
	}
	TArray<FVector> Usable;
	FNavLocation OnNav;
	for (const FVector& Candidate : SquadFormation::ComputeFallbackSlots(LeaderRef.GetActorLocation(), Slot, FormationConfig.StopRadius * 6.f))
	{
		// On the leader's floor (the navmesh there, not the ramp below), on his side of any wall, not on a ramp itself.
		if (!Nav->ProjectPointToNavigation(FVector(Candidate.X, Candidate.Y, LeaderFeet + 30.f), OnNav, FVector(40.f, 40.f, 60.f))
			|| FMath::Abs(OnNav.Location.Z - LeaderFeet) > 60.f || IsRampUnder(FVector2D(Candidate), LeaderFeet)
			|| IsSlotPathBlocked(LeaderRef, FVector(Candidate.X, Candidate.Y, LeaderRef.GetActorLocation().Z)))
		{
			continue;
		}
		Usable.Add(FVector(OnNav.Location.X, OnNav.Location.Y, Slot.Z));
	}
	const int32 Chosen = SquadFormation::ChooseFallbackSlot(Usable, FrameGivenSlots, FormationTuning::Get().SlotMinSpacing);
	if (Chosen == INDEX_NONE)
	{
		return Slot;
	}
	FrameGivenSlots.Add(FVector2D(Usable[Chosen]));
	UE_LOG(LogCodexTactics, Verbose, TEXT("[RampFile] slot (%.0f, %.0f) lies on a ramp, the leader on a floor: stand-in (%.0f, %.0f)"), Slot.X, Slot.Y,
		Usable[Chosen].X, Usable[Chosen].Y);
	return Usable[Chosen];
}

void USquadSubsystem::SetRampHold(AOperativeCharacter& Operative, bool bHold, int32 Direction, const TCHAR* Why)
{
	FRampHoldState& State = RampHolds.FindOrAdd(&Operative);
	if (State.bHeld == bHold)
	{
		return;
	}
	AAIController* AI = Cast<AAIController>(Operative.GetController());
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (bHold)
	{
		const double LastProgress = State.LastProgressSeconds;
		State = FRampHoldState();
		State.LastProgressSeconds = LastProgress;
		State.bHeld = true;
		State.Direction = Direction;
		++RampHoldCount;
		if (Operative.HasOwnMoveOrder() && Following && Following->GetStatus() == EPathFollowingStatus::Moving)
		{
			// His own order (the player's, a group move): paused, never dropped - it goes on once the way up / down is free.
			State.bPausedOwnWalk = true;
			State.PausedRequestId = Following->GetCurrentRequestId().GetID();
			Following->PauseMove(Following->GetCurrentRequestId(), EPathFollowingVelocityMode::Reset);
		}
		else
		{
			// A formation follower: the traffic hold keeps the formation's next follow off him (it stops him, re-paths him on release).
			Operative.SetTrafficHold(&Operative, true);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[RampFile] %s waits (%s): %s"), *Operative.DisplayName.ToString(), State.bPausedOwnWalk ? TEXT("his walk paused")
			: TEXT("formation hold"), Why);
		return;
	}
	Operative.SetTrafficHold(&Operative, false);
	if (State.bPausedOwnWalk && Following && Following->GetStatus() == EPathFollowingStatus::Paused
		&& Following->GetCurrentRequestId().GetID() == State.PausedRequestId)
	{
		Following->ResumeMove(Following->GetCurrentRequestId());
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[RampFile] %s walks on after %.1f s: %s"), *Operative.DisplayName.ToString(), State.HeldSeconds, Why);
	State.bHeld = false;
	State.bPausedOwnWalk = false;
}

void USquadSubsystem::BuildLeaderWay(const AOperativeCharacter& LeaderRef, TArray<FVector2D>& OutWay)
{
	OutWay.Reset();
	// His path as the corners of a string-pulled navmesh path (the crowd's own path keeps only its start and end), once per order.
	const uint32 Serial = LeaderRef.GetMoveOrderSerial();
	if (HolderYieldWayLeader.Get() != &LeaderRef || HolderYieldWaySerial != Serial)
	{
		HolderYieldWayLeader = &LeaderRef;
		HolderYieldWaySerial = Serial;
		HolderYieldWay.Reset();
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), LeaderRef.GetActorLocation(), LeaderRef.GetLastMoveDestination());
		if (Path && Path->IsValid())
		{
			for (const FVector& Point : Path->PathPoints)
			{
				HolderYieldWay.Add(FVector2D(Point));
			}
		}
	}
	if (HolderYieldWay.IsEmpty())
	{
		return;
	}
	// From where he is now: the corners after the one nearest to him.
	OutWay.Add(FVector2D(LeaderRef.GetActorLocation()));
	int32 Nearest = 0;
	for (int32 Index = 1; Index < HolderYieldWay.Num(); ++Index)
	{
		if (FVector2D::DistSquared(HolderYieldWay[Index], OutWay[0]) < FVector2D::DistSquared(HolderYieldWay[Nearest], OutWay[0]))
		{
			Nearest = Index;
		}
	}
	for (int32 Index = Nearest + 1; Index < HolderYieldWay.Num(); ++Index)
	{
		OutWay.Add(HolderYieldWay[Index]);
	}
}

void USquadSubsystem::UpdateHolderYield(AOperativeCharacter& Operative, const AOperativeCharacter& LeaderRef)
{
	using namespace SquadSubsystemRampFileLocal;
	const bool bDebug = CVarSquadRampFileDebug.GetValueOnGameThread() != 0;
	const uint32 Serial = LeaderRef.GetMoveOrderSerial();
	const uint32* Done = HolderYieldSerial.Find(&Operative);
	if (!LeaderRef.HasOwnMoveOrder() || !IsWalker(&Operative) || Operative.bInCover || Operative.HasOwnMoveOrder() || Operative.IsTrafficHeld()
		|| Operative.IsMoving() || (Done && *Done == Serial))
	{
		UE_CLOG(bDebug, LogCodexTactics, Display, TEXT("[RampFile] yield %s: leader's own walk %d, walker %d cover %d own %d held %d moving %d, done for order %d"),
			*Operative.DisplayName.ToString(), LeaderRef.HasOwnMoveOrder() ? 1 : 0, IsWalker(&Operative) ? 1 : 0, Operative.bInCover ? 1 : 0,
			Operative.HasOwnMoveOrder() ? 1 : 0, Operative.IsTrafficHeld() ? 1 : 0, Operative.IsMoving() ? 1 : 0, Done && *Done == Serial ? 1 : 0);
		return;
	}
	// Only a team-mate on the leader's level near him (a man on the roof above the ramp's foot is in nobody's way).
	if (FMath::Abs(RampFeetZ(Operative) - RampFeetZ(LeaderRef)) > 120.f || FVector::Dist2D(Operative.GetActorLocation(), LeaderRef.GetActorLocation()) > 800.f)
	{
		return;
	}
	TArray<FVector2D> Way;
	BuildLeaderWay(LeaderRef, Way);
	const FVector2D Here(Operative.GetActorLocation());
	if (FormationRuntime::YieldFromLeaderPath(Way, Here).IsEmpty())
	{
		UE_CLOG(bDebug, LogCodexTactics, Display, TEXT("[RampFile] yield %s at (%.0f, %.0f): not on the way %s"), *Operative.DisplayName.ToString(), Here.X, Here.Y,
			*FString::JoinBy(Way, TEXT(" "), [](const FVector2D& P) { return FString::Printf(TEXT("(%.0f, %.0f)"), P.X, P.Y); }));
		return;
	}
	HolderYieldSerial.Add(&Operative, Serial); // one step per order (a man with no room aside stays)
	// The spots others stand on or walk to are taken.
	TArray<FVector2D> Taken;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (Member.IsValid() && Member.Get() != &Operative)
		{
			Taken.Add(FVector2D(Member->GetActorLocation()));
		}
	}
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector2D>& Target : HolderYieldTargets)
	{
		if (Target.Key.IsValid() && Target.Key.Get() != &Operative)
		{
			Taken.Add(Target.Value);
		}
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const float Feet = RampFeetZ(Operative);
	FCollisionQueryParams WalkParams(SCENE_QUERY_STAT(SquadRampYieldWalk), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		WalkParams.AddIgnoredActor(Member.Get());
	}
	// A spot he walks straight to (nothing between: a spot behind the building's wall is a walk round through its door).
	const auto IsStraightWalk = [this, &Operative, &WalkParams](const FVector& To)
	{
		FHitResult Hit;
		const FVector Lift(0.f, 0.f, 50.f);
		return !GetWorld()->LineTraceSingleByChannel(Hit, Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight()) + Lift,
			To + Lift, ECC_Visibility, WalkParams);
	};
	for (const FVector2D& Point : RampFileRules::YieldSpots(Way, Here, RampFileRules::FRampFileConfig().YieldSpotClearCm, FormationRuntime::YieldAheadCm))
	{
		FNavLocation OnNav;
		if (!Nav || !Nav->ProjectPointToNavigation(FVector(Point.X, Point.Y, Operative.GetActorLocation().Z), OnNav, FVector(30.f, 30.f, 150.f))
			|| FVector2D::Distance(FVector2D(OnNav.Location), Point) > 30.f || FMath::Abs(OnNav.Location.Z - Feet) > 60.f || IsRampUnder(Point, Feet)
			|| Taken.ContainsByPredicate([&Point](const FVector2D& Other) { return FVector2D::Distance(Other, Point) < 100.0; }) || !IsStraightWalk(OnNav.Location))
		{
			continue;
		}
		HolderYieldTargets.Add(&Operative, FVector2D(OnNav.Location));
		const EOperativeOrderResult Result = Operative.FollowTo(OnNav.Location, FormationTuning::Get().PaceWalkSpeed);
		UE_LOG(LogCodexTactics, Display, TEXT("[RampFile] %s (holding) stands on %s's way: steps aside from (%.0f, %.0f) to (%.0f, %.0f) -> %d"),
			*Operative.DisplayName.ToString(), *LeaderRef.DisplayName.ToString(), Here.X, Here.Y, OnNav.Location.X, OnNav.Location.Y, static_cast<int32>(Result));
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[RampFile] %s (holding) stands on %s's way at (%.0f, %.0f): no free spot aside"), *Operative.DisplayName.ToString(),
		*LeaderRef.DisplayName.ToString(), Here.X, Here.Y);
}

void USquadSubsystem::UpdateRampFile(float DeltaTime)
{
	using namespace SquadSubsystemRampFileLocal;
	const RampFileRules::FRampFileConfig& Tune = Config();
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (CVarSquadRampFile.GetValueOnGameThread() == 0 || !LeaderRef)
	{
		for (auto It = RampHolds.CreateIterator(); It; ++It)
		{
			if (AOperativeCharacter* Held = It->Key.Get(); Held && It->Value.bHeld)
			{
				SetRampHold(*Held, false, 0, TEXT("the ramp file is off"));
			}
		}
		RampHolds.Reset();
		return;
	}
	// Holding team-mates (Solo, a perimeter hold) out of the selected man's way (not with a trap spotted: they stand where it is safe).
	if (bFollowersHolding && !IsTrapSolo())
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			if (AOperativeCharacter* Operative = Member.Get(); Operative && Operative != LeaderRef)
			{
				UpdateHolderYield(*Operative, *LeaderRef);
			}
		}
	}

	const double Now = GetWorld()->GetTimeSeconds();
	// Who is on a ramp (which one, its uphill axis), who changes level.
	struct FCandidate
	{
		AOperativeCharacter* Operative = nullptr;
		const UPrimitiveComponent* Ramp = nullptr;
		FVector2D Uphill = FVector2D::ZeroVector;
		int32 Direction = 0;
		int32 Priority = 0;
		/** The ramp file he walks in (index into Ramps), INDEX_NONE = none. */
		int32 File = INDEX_NONE;
	};
	TArray<FCandidate> Candidates;
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		AOperativeCharacter* Operative = Members[Index].Get();
		if (!IsWalker(Operative))
		{
			if (Operative && IsRampHeld(Operative))
			{
				SetRampHold(*Operative, false, 0, TEXT("he can no longer walk the ramp (ladder / vehicle / knocked down)"));
			}
			continue;
		}
		FRampHoldState& Hold = RampHolds.FindOrAdd(Operative);
		Hold.CooldownSeconds = FMath::Max(0.f, Hold.CooldownSeconds - DeltaTime);
		if (Operative->GetVelocity().Size2D() >= Tune.ProgressSpeed)
		{
			Hold.LastProgressSeconds = Now;
		}
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Operative = Operative;
		const bool bOnRamp = IsOperativeOnRamp(*Operative, &Candidate.Uphill, &Candidate.Ramp);
		if (!bOnRamp)
		{
			Candidate.Ramp = nullptr;
		}
		const bool bHasWalk = Hold.bHeld || MoveStatus(*Operative) != EPathFollowingStatus::Idle;
		const float UphillSpeed = static_cast<float>(FVector2D::DotProduct(FVector2D(Operative->GetVelocity()), Candidate.Uphill));
		Candidate.Direction = Hold.bHeld ? Hold.Direction
			: RampFileRules::TravelDirection(RampFeetZ(*Operative), static_cast<float>(Operative->GetLastMoveDestination().Z), bHasWalk, Tune.LevelChangeCm,
				bOnRamp, UphillSpeed, Tune.ProgressSpeed);
		Candidate.Priority = Operative == LeaderRef ? 0 : Index + 1;
	}
	// One file per ramp in use: the men on it, and each man in a ramp's mouth (off it, changing level) in the file of the nearest man on a
	// ramp. One axis per file and every man in one file only: the order is strict, nobody waits for a man who waits for him.
	TArray<const UPrimitiveComponent*> Ramps;
	TArray<FVector2D> Axes;
	for (FCandidate& Candidate : Candidates)
	{
		if (Candidate.Ramp && Candidate.Direction != 0)
		{
			Candidate.File = Ramps.Find(Candidate.Ramp);
			if (Candidate.File == INDEX_NONE)
			{
				Candidate.File = Ramps.Add(Candidate.Ramp);
				Axes.Add(Candidate.Uphill);
			}
		}
	}
	for (FCandidate& Candidate : Candidates)
	{
		if (Candidate.Ramp || Candidate.Direction == 0)
		{
			continue;
		}
		const FVector At = Candidate.Operative->GetActorLocation();
		float Best = TNumericLimits<float>::Max();
		for (const FCandidate& OnRamp : Candidates)
		{
			const FVector There = OnRamp.Operative->GetActorLocation();
			const float Distance = static_cast<float>(FVector::Dist2D(At, There));
			if (OnRamp.File != INDEX_NONE && OnRamp.Direction == Candidate.Direction && Distance < Best
				&& RampFileRules::IsInMouth(FVector2D(At - There), static_cast<float>(At.Z - There.Z), Axes[OnRamp.File], Candidate.Direction, Tune))
			{
				Best = Distance;
				Candidate.File = OnRamp.File;
			}
		}
	}
	TSet<AOperativeCharacter*> Wait;
	TMap<AOperativeCharacter*, FString> WaitWhy;
	// A holding team-mate stepping out of the selected man's way: the selected man waits while that team-mate is still on his way close
	// ahead (they would walk into each other in the ramp's mouth).
	for (auto It = HolderYieldTargets.CreateIterator(); It; ++It)
	{
		AOperativeCharacter* Holder = It->Key.Get();
		if (!Holder || !bFollowersHolding || HolderYieldSerial.FindRef(Holder) != LeaderRef->GetMoveOrderSerial()
			|| (!Holder->IsMoving() && FVector2D::Distance(FVector2D(Holder->GetActorLocation()), It->Value) < 60.0))
		{
			It.RemoveCurrent(); // there (or gone, or a new order)
			continue;
		}
		TArray<FVector2D> Way;
		BuildLeaderWay(*LeaderRef, Way);
		if (LeaderRef->HasOwnMoveOrder() && FVector::Dist2D(Holder->GetActorLocation(), LeaderRef->GetActorLocation()) < Tune.MouthCm
			&& !FormationRuntime::YieldFromLeaderPath(Way, FVector2D(Holder->GetActorLocation())).IsEmpty())
		{
			Wait.Add(LeaderRef);
			WaitWhy.Add(LeaderRef, FString::Printf(TEXT("%s steps out of his way"), *Holder->DisplayName.ToString()));
		}
	}
	for (int32 File = 0; File < Ramps.Num(); ++File)
	{
		TArray<RampFileRules::FRampWalker> Walkers;
		TArray<const FCandidate*> Who;
		for (const FCandidate& Candidate : Candidates)
		{
			if (Candidate.File != File)
			{
				continue;
			}
			RampFileRules::FRampWalker& Walker = Walkers.AddDefaulted_GetRef();
			Walker.Along = static_cast<float>(FVector2D::DotProduct(FVector2D(Candidate.Operative->GetActorLocation()), Axes[File]));
			Walker.Direction = Candidate.Direction;
			const FRampHoldState* Hold = RampHolds.Find(Candidate.Operative);
			Walker.bProgressing = IsRampHeld(Candidate.Operative) || (Hold && Now - Hold->LastProgressSeconds < Tune.ProgressMemorySeconds);
			Walker.Priority = Candidate.Priority;
			Who.Add(&Candidate);
		}
		for (int32 Index = 0; Index < Walkers.Num(); ++Index)
		{
			const int32 Behind = RampFileRules::FindWaitBehind(Index, Walkers, Tune, IsRampHeld(Who[Index]->Operative));
			if (Behind != INDEX_NONE)
			{
				Wait.Add(Who[Index]->Operative);
				WaitWhy.Add(Who[Index]->Operative, FString::Printf(TEXT("%s is %.0f cm ahead of him %s the ramp"), *Who[Behind]->Operative->DisplayName.ToString(),
					(Walkers[Behind].Along - Walkers[Index].Along) * static_cast<float>(Walkers[Index].Direction), Walkers[Index].Direction > 0 ? TEXT("up") : TEXT("down")));
			}
		}
	}
	for (const FCandidate& Candidate : Candidates)
	{
		FRampHoldState& Hold = RampHolds.FindOrAdd(Candidate.Operative);
		if (Hold.bHeld)
		{
			Hold.HeldSeconds += DeltaTime;
		}
		const bool bWait = Wait.Contains(Candidate.Operative) && Hold.CooldownSeconds <= 0.f;
		if (Hold.bHeld && RampFileRules::HoldExpired(Hold.HeldSeconds, Tune))
		{
			SetRampHold(*Candidate.Operative, false, 0, TEXT("waited too long - the man ahead does not move on"));
			Hold.CooldownSeconds = Tune.RetryAfterSeconds;
		}
		else if (bWait && !Hold.bHeld)
		{
			SetRampHold(*Candidate.Operative, true, Candidate.Direction, *WaitWhy.FindRef(Candidate.Operative));
		}
		else if (!bWait && Hold.bHeld)
		{
			SetRampHold(*Candidate.Operative, false, 0, TEXT("the way is free"));
		}
	}
	for (auto It = RampHolds.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
