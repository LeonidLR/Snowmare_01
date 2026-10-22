// Weather idle poses (user request 2026-10-12; UE-only, no Godot reference). See WarmIdleSubsystem.h.

#include "Survival/WarmIdleSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/CampfireActor.h"
#include "Components/BoxComponent.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/PortableStoveActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "NavigationSystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Survival/TacticalHeatRules.h"

namespace
{
	/** The capsule probe of a spot (an operative standing / sitting there), cm. */
	constexpr float SpotProbeRadius = 28.f;
	constexpr float SpotProbeHalfHeight = 60.f;
	/** A walk that ends farther than this from the spot after stopping counts as failed, cm. */
	constexpr float WalkGiveUpDistance = 120.f;
	/** A squad pull that was not asked about for this long lapses, s (the follower repath runs every 0.2 s; a stance change may hold it a while). */
	constexpr double PullLapseSeconds = 3.0;

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetMesh() ? Cast<UOperativeAnimInstance>(Operative.GetMesh()->GetAnimInstance()) : nullptr;
	}

	float SeatDistance(const FTacticalHeatConfig& Config, EWarmHeatKind Kind)
	{
		return Kind == EWarmHeatKind::Stove ? Config.StoveSeatDistance : Config.CampfireSeatDistance;
	}

	float StandDistance(const FTacticalHeatConfig& Config, EWarmHeatKind Kind)
	{
		return Kind == EWarmHeatKind::Barrel ? Config.BarrelStandDistance
			: Kind == EWarmHeatKind::Stove ? Config.StoveStandDistance : Config.CampfireStandDistance;
	}
}

bool UWarmIdleSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UWarmIdleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWarmIdleSubsystem, STATGROUP_Tickables);
}

void UWarmIdleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Accumulator += DeltaTime;
	if (Accumulator >= EvaluateInterval)
	{
		Evaluate(Accumulator);
		Accumulator = 0.f;
	}
}

bool UWarmIdleSubsystem::ShouldFollowerHoldWarmSpot(const AOperativeCharacter& Follower, const AOperativeCharacter& Leader)
{
	const AActor* Source = Follower.WarmSpotSource.Get();
	return Source && FVector::Dist2D(Leader.GetActorLocation(), Source->GetActorLocation()) <= Follower.WarmSpotReach;
}

UWarmIdleSubsystem::FDebugState UWarmIdleSubsystem::GetDebugState(const AOperativeCharacter* Operative) const
{
	FDebugState Out;
	const FMemberState* State = States.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
	if (!State)
	{
		return Out;
	}
	Out.Pose = State->Pose;
	Out.Role = State->Role;
	Out.Heat = State->Heat;
	Out.Source = State->Source;
	Out.bInnerRing = State->bInnerRing;
	Out.bArrived = State->bArrived;
	Out.IdleSeconds = State->IdleSeconds;
	if (const FGroup* Group = Groups.Find(State->Source); Group && State->Slot != INDEX_NONE)
	{
		const TArray<FVector>& Ring = State->bInnerRing ? Group->Inner : Group->Outer;
		if (Ring.IsValidIndex(State->Slot))
		{
			Out.bHasSpot = true;
			Out.Spot = Ring[State->Slot];
		}
	}
	return Out;
}

EWarmHeatKind UWarmIdleSubsystem::ClassifySource(const UHeatSourceComponent& Source)
{
	const AActor* Owner = Source.GetOwner();
	if (Cast<ABarrelActor>(Owner))
	{
		return EWarmHeatKind::Barrel;
	}
	if (Cast<ACampfireActor>(Owner))
	{
		return EWarmHeatKind::Campfire;
	}
	if (Cast<APortableStoveActor>(Owner))
	{
		return EWarmHeatKind::Stove;
	}
	return EWarmHeatKind::Other;
}

UHeatSourceComponent* UWarmIdleSubsystem::FindHeatSource(const AOperativeCharacter& Operative, EWarmHeatKind& OutKind) const
{
	UHeatSourceComponent* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	OutKind = EWarmHeatKind::None;
	const FVector Location = Operative.GetActorLocation();
	for (const TWeakObjectPtr<UHeatSourceComponent>& Weak : UHeatSourceComponent::GetAllSources())
	{
		UHeatSourceComponent* Source = Weak.Get();
		if (!Source || Source->GetWorld() != GetWorld() || !Source->IsLocationWarm(Location))
		{
			continue;
		}
		const EWarmHeatKind Kind = ClassifySource(*Source);
		// Sources with spots first (a flare inside a campfire's zone does not stop the sitting), then the nearest.
		const float Score = FVector::Dist(Source->GetComponentLocation(), Location) + (WarmIdleRules::UsesWarmSpots(Kind) ? 0.f : 100000.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Source;
			OutKind = Kind;
		}
	}
	return Best;
}

bool UWarmIdleSubsystem::IsSpotUsable(const FVector& Spot, const AActor* SourceActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FVector Ground = Spot;
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation Projected;
		if (Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr)
		{
			if (!Nav->ProjectPointToNavigation(Spot, Projected, FVector(30.f, 30.f, 150.f)))
			{
				return false; // off the NavMesh (inside a wall, over a drop)
			}
			Ground = Projected.Location;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WarmSpotProbe), false, SourceActor);
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	const FVector Center = Ground + FVector(0.f, 0.f, SpotProbeHalfHeight + 15.f);
	return !World->OverlapBlockingTestByChannel(Center, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(SpotProbeRadius, SpotProbeHalfHeight), Params);
}

void UWarmIdleSubsystem::BuildRings(FGroup& Group, const FTacticalHeatConfig& Config, const FVector& FirstMember) const
{
	// The first ring spot points at the first operative (no crossing walk); the outer ring sits between the seats.
	const float StartYaw = WarmIdleRules::FaceYaw(Group.Center, FirstMember);
	const AActor* Owner = Group.Source.Get();
	// The rings never cut into the source itself (a 1.2 m fuel barrel): standing places clear its box by a capsule, seats by the
	// sitting legs (~55 cm towards the fire).
	const AInteractableActor* Interactable = Cast<AInteractableActor>(Owner);
	const float Footprint = Interactable && Interactable->Box
		? static_cast<float>(FMath::Max(Interactable->Box->GetScaledBoxExtent().X, Interactable->Box->GetScaledBoxExtent().Y)) : 0.f;
	if (Group.Kind != EWarmHeatKind::Barrel)
	{
		const float Seat = FMath::Max(SeatDistance(Config, Group.Kind), Footprint + 55.f);
		Group.Inner = WarmIdleRules::ComputeRing(Group.Center, Seat, WarmIdleRules::RingCapacity(Seat, Config.WarmSpotSpacing), StartYaw);
	}
	const float Stand = FMath::Max(StandDistance(Config, Group.Kind), Footprint + SpotProbeRadius + 15.f);
	const int32 OuterCount = WarmIdleRules::RingCapacity(Stand, Config.WarmSpotSpacing);
	Group.Outer = WarmIdleRules::ComputeRing(Group.Center, Stand, OuterCount, StartYaw + (Group.Kind == EWarmHeatKind::Barrel ? 0.f : 180.f / OuterCount));
	Group.InnerBlocked.Init(false, Group.Inner.Num());
	Group.OuterBlocked.Init(false, Group.Outer.Num());
	for (int32 Index = 0; Index < Group.Inner.Num(); ++Index)
	{
		Group.InnerBlocked[Index] = !IsSpotUsable(Group.Inner[Index], Owner);
	}
	for (int32 Index = 0; Index < Group.Outer.Num(); ++Index)
	{
		Group.OuterBlocked[Index] = !IsSpotUsable(Group.Outer[Index], Owner);
	}
	Group.bRingsBuilt = true;
}

void UWarmIdleSubsystem::AssignSlot(FGroup& Group, AOperativeCharacter& Operative, FMemberState& State)
{
	const bool bInner = State.Role == EWarmFireRole::Sit;
	const TArray<FVector>& Ring = bInner ? Group.Inner : Group.Outer;
	TArray<bool> Taken = bInner ? Group.InnerBlocked : Group.OuterBlocked;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Group.Members)
	{
		const FMemberState* Other = States.Find(Member);
		if (Member.Get() != &Operative && Other && Other->bInnerRing == bInner && Taken.IsValidIndex(Other->Slot))
		{
			Taken[Other->Slot] = true;
		}
	}
	State.bInnerRing = bInner;
	State.Slot = WarmIdleRules::PickSlot(Ring, Taken, Group.Center, Operative.GetActorLocation());
	UE_LOG(LogCodexTactics, Display, TEXT("[WarmIdle] %s: %s at %s, %s ring spot %d of %d (cold %.0f)"), *Operative.DisplayName.ToString(),
		State.Role == EWarmFireRole::Sit ? TEXT("sits") : TEXT("stands"), Group.Source.IsValid() ? *Group.Source->GetName() : TEXT("-"),
		bInner ? TEXT("inner") : TEXT("outer"), State.Slot, Ring.Num(), Operative.ColdLevel);
	State.bWalkIssued = false;
	State.bArrived = false;
}

void UWarmIdleSubsystem::RequestPose(AOperativeCharacter& Operative, FMemberState& State, EWarmIdlePose Pose, EWarmExitReason Reason)
{
	State.Pose = Pose;
	if (UOperativeAnimInstance* Anim = AnimOf(Operative))
	{
		Anim->SetWarmIdleRequest(Pose, Reason);
	}
}

void UWarmIdleSubsystem::Release(AOperativeCharacter& Operative, FMemberState& State)
{
	if (State.Source.IsValid())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WarmIdle] %s leaves the warm spot at %s%s"), *Operative.DisplayName.ToString(), *State.Source->GetName(),
			State.GaveUpSource == State.Source ? TEXT(" (no reachable spot)") : TEXT(""));
	}
	if (FGroup* Group = Groups.Find(State.Source))
	{
		Group->Members.Remove(TWeakObjectPtr<AOperativeCharacter>(&Operative));
	}
	State.Source.Reset();
	State.Role = EWarmFireRole::Unassigned;
	State.Slot = INDEX_NONE;
	State.bWalkIssued = false;
	State.bArrived = false;
	State.WalkFailures = 0;
	Operative.WarmSpotSource.Reset();
	Operative.WarmSpotReach = 0.f;
}

void UWarmIdleSubsystem::UpdateGroup(FGroup& Group, const TArray<AOperativeCharacter*>& Candidates, const FTacticalHeatConfig& Config)
{
	// Leavers.
	bool bChanged = false;
	for (int32 Index = Group.Members.Num() - 1; Index >= 0; --Index)
	{
		AOperativeCharacter* Member = Group.Members[Index].Get();
		if (!Member || !Candidates.Contains(Member))
		{
			if (Member)
			{
				if (FMemberState* State = States.Find(Member))
				{
					Release(*Member, *State);
				}
			}
			else
			{
				Group.Members.RemoveAt(Index);
			}
			bChanged = true;
		}
	}
	// Newcomers.
	bool bArrival = false;
	for (AOperativeCharacter* Candidate : Candidates)
	{
		if (!Group.Members.Contains(Candidate))
		{
			if (!Group.bRingsBuilt)
			{
				BuildRings(Group, Config, Candidate->GetActorLocation());
			}
			Group.Members.Add(Candidate);
			FMemberState& State = States.FindOrAdd(Candidate);
			State.Source = Group.Source;
			State.Role = EWarmFireRole::Unassigned;
			State.Slot = INDEX_NONE;
			Candidate->WarmSpotSource = Group.Source;
			Candidate->WarmSpotReach = Group.Reach;
			bChanged = true;
			bArrival = true;
		}
	}
	if (!bChanged)
	{
		return;
	}
	// Roles (campfire / stove: the coldest sit; barrel: everyone stands). User design 2026-10-12: re-evaluated by the current cold
	// when someone ARRIVES (or the fire is relit - a new group); when someone leaves (ordered away) the others keep their roles and
	// only the coldest stander sits down if fewer than WarmMinSitters remain seated.
	TArray<EWarmFireRole> Current;
	TArray<float> Colds;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Group.Members)
	{
		const FMemberState* State = States.Find(Member);
		Current.Add(State && !bArrival ? State->Role : EWarmFireRole::Unassigned);
		Colds.Add(Member.IsValid() ? Member->ColdLevel : 0.f);
	}
	int32 FreeSeats = 0;
	for (const bool bBlocked : Group.InnerBlocked)
	{
		FreeSeats += bBlocked ? 0 : 1;
	}
	const TArray<EWarmFireRole> Roles = Group.Kind == EWarmHeatKind::Barrel
		? TArray<EWarmFireRole>()
		: WarmIdleRules::AssignFireRoles(Current, Colds, Config.WarmMinSitters, Config.WarmExtraSitterCold, FreeSeats);
	for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
	{
		AOperativeCharacter* Member = Group.Members[Index].Get();
		FMemberState* State = Member ? States.Find(Member) : nullptr;
		if (!State)
		{
			continue;
		}
		const EWarmFireRole Role = Roles.IsValidIndex(Index) ? Roles[Index] : EWarmFireRole::Stand;
		if (Role != State->Role || State->Slot == INDEX_NONE)
		{
			const bool bWasPosed = State->Role != EWarmFireRole::Unassigned;
			State->Role = Role;
			AssignSlot(Group, *Member, *State);
			if (bWasPosed)
			{
				// Promoted from the outer ring to a seat: the hand warming ends, he walks to the seat (a short step).
				RequestPose(*Member, *State, EWarmIdlePose::None, EWarmExitReason::NoLongerWanted);
			}
		}
	}
}

void UWarmIdleSubsystem::DriveMember(FGroup& Group, AOperativeCharacter& Operative, FMemberState& State, const FTacticalHeatConfig& Config)
{
	const TArray<FVector>& Ring = State.bInnerRing ? Group.Inner : Group.Outer;
	if (!Ring.IsValidIndex(State.Slot))
	{
		// No free spot on his ring: he warms where he stands, no pose.
		State.GaveUpSource = Group.Source;
		Release(Operative, State);
		RequestPose(Operative, State, EWarmIdlePose::None, EWarmExitReason::NoLongerWanted);
		return;
	}
	const FVector Spot = Ring[State.Slot];
	const float Distance = FVector::Dist2D(Operative.GetActorLocation(), Spot);
	const bool bMoving = Operative.GetVelocity().SizeSquared2D() > 10.f * 10.f;
	if (!State.bArrived)
	{
		if (Distance <= Config.WarmSpotArriveTolerance && !bMoving)
		{
			State.bArrived = true;
			Operative.StopOperative();
		}
		else if (!State.bWalkIssued)
		{
			// The path follower stops AcceptanceRadius short of its goal: aim that far past the spot so he ends ON it (the
			// hands reach the barrel rim / the seat is at the ring).
			const FVector Past = Spot + (Spot - Operative.GetActorLocation()).GetSafeNormal2D() * Operative.MovementConfig.AcceptanceRadius;
			if (Operative.FollowTo(Past, Config.WarmSpotWalkSpeed) == EOperativeOrderResult::Accepted)
			{
				State.bWalkIssued = true;
			}
			else
			{
				State.WalkFailures++;
			}
		}
		else if (!bMoving && !Operative.IsMoving())
		{
			// The walk ended short of the spot (blocked): close enough counts, else try another spot.
			if (Distance <= WalkGiveUpDistance * 0.5f)
			{
				State.bArrived = true;
			}
			else
			{
				State.WalkFailures++;
				(State.bInnerRing ? Group.InnerBlocked : Group.OuterBlocked)[State.Slot] = true;
				AssignSlot(Group, Operative, State);
			}
		}
		if (State.WalkFailures > 2)
		{
			State.GaveUpSource = Group.Source;
			Release(Operative, State);
			RequestPose(Operative, State, EWarmIdlePose::None, EWarmExitReason::NoLongerWanted);
			return;
		}
		if (!State.bArrived)
		{
			return;
		}
	}
	// At the spot: face the fire, stand up for the hand warming, then the pose.
	const float FaceYaw = WarmIdleRules::FaceYaw(Operative.GetActorLocation(), Group.Center);
	Operative.SetIdleFacingYaw(FaceYaw);
	const EWarmIdlePose Pose = Group.Kind == EWarmHeatKind::Barrel ? EWarmIdlePose::HandWarm : WarmIdleRules::PoseForFireRole(State.Role);
	if (Pose == EWarmIdlePose::HandWarm && Operative.GetStance() != EOperativeStance::Standing)
	{
		Operative.SetStance(EOperativeStance::Standing);
		return;
	}
	if (State.Pose == Pose || FMath::Abs(FMath::FindDeltaAngleDegrees(Operative.GetActorRotation().Yaw, FaceYaw)) <= FacingToleranceDegrees)
	{
		RequestPose(Operative, State, Pose, EWarmExitReason::NoLongerWanted);
	}
}

void UWarmIdleSubsystem::Evaluate(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || UWorldAIPauseSubsystem::IsPausedIn(World))
	{
		return; // a dialogue / cutscene holds the world: everyone keeps what he does
	}
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bExploration = !Flow || WarmIdleRules::IsExplorationPhase(Flow->GetPhase(), Flow->GetCombatMode());
	const double Now = World->GetTimeSeconds();

	TMap<TWeakObjectPtr<AActor>, TArray<AOperativeCharacter*>> Candidates;
	TMap<TWeakObjectPtr<AActor>, TPair<EWarmHeatKind, float>> SourceInfo;
	TArray<AOperativeCharacter*> Seen;
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		AOperativeCharacter* Operative = *It;
		Seen.Add(Operative);
		FMemberState& State = States.FindOrAdd(Operative);
		const bool bOrdered = State.bSerialKnown && State.OrderSerial != Operative->GetMoveOrderSerial();
		State.OrderSerial = Operative->GetMoveOrderSerial();
		State.bSerialKnown = true;
		// The walk to his own spot is not "moving"; anything else is (an order, the formation, a push).
		const bool bOwnWalk = State.bWalkIssued && !State.bArrived && State.Source.IsValid();
		const bool bMoving = !bOwnWalk && Operative->IsMoving();
		State.IdleSeconds = bOrdered || bMoving ? 0.f : State.IdleSeconds + DeltaSeconds;

		const UHealthComponent* Health = Operative->HealthComponent;
		const UColdSurvivalComponent* Cold = Operative->ColdSurvival;
		FWarmIdleInput Input;
		Input.bExploration = bExploration;
		Input.bAlive = !Health || Health->IsAlive();
		Input.bKnockedDown = Operative->IsKnockedDown();
		Input.bFrostbitten = Cold && Cold->IsFrostbitten();
		Input.bInCover = Operative->bInCover;
		Input.bCarrying = Operative->bCarrying;
		Input.bVaulting = Operative->IsVaulting() || Operative->IsClimbingLadder();
		Input.bBusy = IsBusy(*Operative);
		Input.bMoving = bMoving || bOrdered;
		Input.bThreat = IsThreatened(*Operative, Now, bExploration);
		Input.Stance = Operative->GetStance();
		Input.Cold = Operative->ColdLevel;
		Input.IdleSeconds = State.IdleSeconds;
		Input.bInColdIdle = State.Pose == EWarmIdlePose::ColdIdle;
		UHeatSourceComponent* Source = FindHeatSource(*Operative, Input.Heat);
		State.Heat = Input.Heat;

		const EWarmExitReason Reason = !Input.bAlive || Input.bKnockedDown || Input.bInCover || Input.bVaulting ? EWarmExitReason::BodyTaken
			: Input.bMoving ? EWarmExitReason::Moved
			: Input.bThreat ? EWarmExitReason::Threat
			: !bExploration ? EWarmExitReason::CombatStarted : EWarmExitReason::NoLongerWanted;
		const bool bCanPose = WarmIdleRules::CanHoldWarmPose(Input, Config);
		AActor* SourceActor = Source ? Source->GetOwner() : nullptr;
		if (State.GaveUpSource.IsValid() && State.GaveUpSource.Get() != SourceActor)
		{
			State.GaveUpSource.Reset();
		}
		const bool bHeldSpot = State.Source.IsValid();
		const bool bSpotSource = WarmIdleRules::UsesWarmSpots(Input.Heat) && SourceActor && !Operative->bGuarding
			&& State.GaveUpSource.Get() != SourceActor;
		// A spot already held keeps him in the group through his own walk to it (the idle delay has passed once).
		FWarmIdleInput Held = Input;
		Held.IdleSeconds = FMath::Max(Input.IdleSeconds, Config.WarmIdleDelaySeconds);
		const bool bKeepsSpot = bHeldSpot && State.Source.Get() == SourceActor && WarmIdleRules::CanHoldWarmPose(Held, Config);
		if (bSpotSource && (bKeepsSpot || (!bHeldSpot && bCanPose)))
		{
			Candidates.FindOrAdd(SourceActor).Add(Operative);
			SourceInfo.Add(SourceActor, TPair<EWarmHeatKind, float>(Input.Heat, Source->GetWarmReach()));
			continue;
		}
		if (bHeldSpot)
		{
			Release(*Operative, State); // ordered away / threat / combat / the fire went out: only HIS pose ends
		}
		// Out of a spot group: the cold idle on the spot, or nothing (a heat zone with spots waits for the idle delay).
		const EWarmIdlePose Pose = !bSpotSource && WarmIdleRules::ChooseSoloPose(Input, Config) == EWarmIdlePose::ColdIdle
			? EWarmIdlePose::ColdIdle : EWarmIdlePose::None;
		RequestPose(*Operative, State, Pose, Reason);
	}

	// Groups: build / update, drop the cold ones.
	for (TPair<TWeakObjectPtr<AActor>, TArray<AOperativeCharacter*>>& Entry : Candidates)
	{
		FGroup& Group = Groups.FindOrAdd(Entry.Key);
		if (!Group.Source.IsValid())
		{
			Group.Source = Entry.Key;
			Group.Kind = SourceInfo[Entry.Key].Key;
			Group.Reach = SourceInfo[Entry.Key].Value;
			Group.Center = Entry.Key->GetActorLocation();
		}
		UpdateGroup(Group, Entry.Value, Config);
	}
	for (auto It = Groups.CreateIterator(); It; ++It)
	{
		FGroup& Group = It.Value();
		if (!Candidates.Contains(It.Key()))
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : TArray<TWeakObjectPtr<AOperativeCharacter>>(Group.Members))
			{
				if (AOperativeCharacter* Operative = Member.Get(); Operative && States.Contains(Operative))
				{
					FMemberState& State = States[Operative];
					Release(*Operative, State);
					RequestPose(*Operative, State, EWarmIdlePose::None, EWarmExitReason::NoLongerWanted);
				}
			}
			It.RemoveCurrent(); // the fire went out / everyone left: the rings are rebuilt for the next group
			continue;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : TArray<TWeakObjectPtr<AOperativeCharacter>>(Group.Members))
		{
			AOperativeCharacter* Operative = Member.Get();
			FMemberState* State = Operative ? States.Find(Operative) : nullptr;
			if (State && State->Source == Group.Source)
			{
				DriveMember(Group, *Operative, *State, Config);
			}
		}
	}
	// Forget operatives that are gone.
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	// A pull the squad stopped asking about (solo mode, held followers, a guard, a fight: UpdateFollower no longer runs) lapses, so a
	// stale source never reads as "being pulled" and the reserved ring slot is free again.
	for (auto It = Pulls.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
		else if (It.Value().Source.IsValid() && (!bExploration || Now - It.Value().LastQuery > PullLapseSeconds))
		{
			It.Value().Source.Reset();
			It.Value().Slot = INDEX_NONE;
		}
	}
}

bool UWarmIdleSubsystem::IsThreatened(const AOperativeCharacter& Operative, double Now, bool bExploration) const
{
	return Now - Operative.GetLastAttackedTime() < ThreatHoldSeconds || (bExploration && Operative.IsEnemySpottedInCone());
}

bool UWarmIdleSubsystem::IsBusy(const AOperativeCharacter& Operative) const
{
	const UWorld* World = GetWorld();
	const UInteractionSubsystem* Interaction = World ? World->GetSubsystem<UInteractionSubsystem>() : nullptr;
	const URelocationSubsystem* Relocation = World ? World->GetSubsystem<URelocationSubsystem>() : nullptr;
	return Operative.IsPanicking() || Operative.IsRaging() || (Interaction && Interaction->HasUseOrder(&Operative))
		|| (Relocation && (Relocation->HasActiveTask(&Operative) || Relocation->HasPlannedTask(&Operative)));
}

FVector UWarmIdleSubsystem::ChoosePullTarget(const AOperativeCharacter& Follower, const AActor& Source, float Reach, int32& OutSlot) const
{
	OutSlot = INDEX_NONE;
	const FVector From = Follower.GetActorLocation();
	if (const FGroup* Group = Groups.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(&Source))); Group && Group->bRingsBuilt && !Group->Outer.IsEmpty())
	{
		// The standing ring of the source: not a blocked place, not one a member holds, not one another pulled follower reserved.
		TArray<bool> Taken = Group->OuterBlocked;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Group->Members)
		{
			const FMemberState* State = States.Find(Member);
			if (State && !State->bInnerRing && Taken.IsValidIndex(State->Slot))
			{
				Taken[State->Slot] = true;
			}
		}
		for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FPullState>& Other : Pulls)
		{
			if (Other.Key.Get() != &Follower && Other.Value.Source.Get() == &Source && Taken.IsValidIndex(Other.Value.Slot))
			{
				Taken[Other.Value.Slot] = true;
			}
		}
		OutSlot = WarmIdleRules::PickSlot(Group->Outer, Taken, Group->Center, From);
		if (Group->Outer.IsValidIndex(OutSlot))
		{
			return Group->Outer[OutSlot];
		}
		OutSlot = INDEX_NONE;
	}
	// No ring: a point inside the zone on the side he comes from.
	const FVector Center = Source.GetActorLocation();
	return Center + (From - Center).GetSafeNormal2D() * FMath::Min(Reach * 0.6f, 150.f);
}

AActor* UWarmIdleSubsystem::GetFollowerPullSource(const AOperativeCharacter* Follower) const
{
	const FPullState* Pull = Pulls.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Follower)));
	return Pull ? Pull->Source.Get() : nullptr;
}

bool UWarmIdleSubsystem::GetFollowerPullTarget(AOperativeCharacter& Follower, const AOperativeCharacter& Leader, FFollowerPull& OutPull)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	FPullState& Pull = Pulls.FindOrAdd(&Follower);
	Pull.LastQuery = World->GetTimeSeconds();
	const bool bOrdered = Pull.bSerialKnown && Pull.OrderSerial != Follower.GetMoveOrderSerial();
	Pull.OrderSerial = Follower.GetMoveOrderSerial();
	Pull.bSerialKnown = true;

	const auto EndPull = [&Pull]()
	{
		Pull.Source.Reset();
		Pull.Slot = INDEX_NONE;
	};

	// The fire: the one he is pulled to, else the one the leader holds a warm spot at.
	const bool bPulling = Pull.Source.IsValid();
	AActor* Source = Pull.Source.Get();
	float Reach = Pull.Reach;
	if (!Source)
	{
		Source = Leader.WarmSpotSource.Get();
		Reach = Leader.WarmSpotReach;
	}
	const FVector LeaderAt = Leader.GetActorLocation();
	if (Pull.OptOutSource.IsValid() && FVector::Dist2D(LeaderAt, Pull.OptOutSource->GetActorLocation()) > Pull.OptOutReach)
	{
		Pull.OptOutSource.Reset(); // the leader left that fire: the next one pulls him again
	}
	if (!Source)
	{
		EndPull();
		return false;
	}
	if (bOrdered)
	{
		Pull.OptOutSource = Source; // an explicit order: that follower leaves the pull
		Pull.OptOutReach = Reach;
		EndPull();
		return false;
	}
	if (Pull.OptOutSource.Get() == Source)
	{
		EndPull();
		return false;
	}
	// The fire must still burn (a burnt-out barrel / a spent stove gathers nobody).
	const UHeatSourceComponent* Heat = Source->FindComponentByClass<UHeatSourceComponent>();
	if (!Heat || !Heat->IsHeatActive())
	{
		EndPull();
		return false;
	}

	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	FWarmIdleInput Free;
	Free.bExploration = !UWorldAIPauseSubsystem::IsPausedIn(World) && (!Flow || WarmIdleRules::IsExplorationPhase(Flow->GetPhase(), Flow->GetCombatMode()));
	const UHealthComponent* Health = Follower.HealthComponent;
	const UColdSurvivalComponent* Cold = Follower.ColdSurvival;
	Free.bAlive = !Health || Health->IsAlive();
	Free.bKnockedDown = Follower.IsKnockedDown();
	Free.bFrostbitten = Cold && Cold->IsFrostbitten();
	Free.bInCover = Follower.bInCover;
	Free.bCarrying = Follower.bCarrying;
	Free.bVaulting = Follower.IsVaulting() || Follower.IsClimbingLadder();
	Free.bBusy = IsBusy(Follower);
	Free.bThreat = IsThreatened(Follower, World->GetTimeSeconds(), Free.bExploration);
	Free.Stance = Follower.GetStance();
	Free.IdleSeconds = Config.WarmIdleDelaySeconds; // walking to the fire is not "moving" for the pull

	FWarmPullInput Input;
	Input.bExploration = Free.bExploration;
	Input.bInFormation = !Follower.bGuarding;
	Input.bLeaderAtFire = FVector::Dist2D(LeaderAt, Source->GetActorLocation()) <= Reach;
	Input.bPulling = bPulling;
	Input.bFollowerFree = WarmIdleRules::CanHoldWarmPose(Free, Config);
	Input.FollowerToSource = FVector::Dist2D(Follower.GetActorLocation(), Source->GetActorLocation());
	if (!WarmIdleRules::ShouldPullFollower(Input, Config))
	{
		EndPull();
		return false;
	}
	if (!bPulling)
	{
		Pull.Source = Source;
		Pull.Reach = Reach;
		Pull.Target = ChoosePullTarget(Follower, *Source, Reach, Pull.Slot);
		UE_LOG(LogCodexTactics, Display, TEXT("[WarmIdle] %s is pulled to %s (%.0f cm away, ring slot %d)"), *Follower.DisplayName.ToString(),
			*Source->GetName(), Input.FollowerToSource, Pull.Slot);
	}
	OutPull.Target = Pull.Target;
	OutPull.Fire = Source->GetActorLocation();
	const float ToTarget = FVector::Dist2D(Follower.GetActorLocation(), Pull.Target);
	OutPull.bArrived = ToTarget <= Config.WarmSpotArriveTolerance + Follower.MovementConfig.AcceptanceRadius
		|| (Input.FollowerToSource <= Reach && !Follower.IsMoving());
	return true;
}
