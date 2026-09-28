#include "Characters/SquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"

namespace
{
	/** Height above the actor origin for the slot visibility trace, cm (Godot: 0.6 m). */
	constexpr float SlotTraceHeight = 60.f;
	/** Yaw interpolation speed for parked followers turning with the leader. */
	constexpr float ParkedAlignSpeed = 4.f;
}

bool USquadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USquadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USquadSubsystem, STATGROUP_Tickables);
}

void USquadSubsystem::RegisterOperative(AOperativeCharacter* Operative)
{
	if (!Operative || Members.Contains(Operative))
	{
		return;
	}
	Members.Add(Operative);
	Members.Sort([](const TWeakObjectPtr<AOperativeCharacter>& A, const TWeakObjectPtr<AOperativeCharacter>& B)
	{
		return A.IsValid() && B.IsValid() && A->SquadIndex < B->SquadIndex;
	});

	if (!Leader.IsValid())
	{
		SetLeader(Members[0].Get());
	}
	else
	{
		RebuildFollowers();
	}
}

void USquadSubsystem::UnregisterOperative(AOperativeCharacter* Operative)
{
	Members.Remove(Operative);
	if (Leader.Get() == Operative)
	{
		Leader.Reset();
		if (Members.Num() > 0)
		{
			SetLeader(Members[0].Get());
			return;
		}
	}
	RebuildFollowers();
}

bool USquadSubsystem::SetLeaderByIndex(int32 RosterIndex)
{
	return Members.IsValidIndex(RosterIndex) && SetLeader(Members[RosterIndex].Get());
}

bool USquadSubsystem::SetLeader(AOperativeCharacter* NewLeader)
{
	if (!NewLeader || !Members.Contains(NewLeader))
	{
		return false;
	}
	if (Leader.Get() != NewLeader)
	{
		Leader = NewLeader;
		// Drop the formation move the new leader was following; it now waits for player orders.
		NewLeader->StopOperative();
		FormationHeading = NewLeader->GetActorForwardVector();
		RebuildFollowers();
		UE_LOG(LogCodexTactics, Log, TEXT("Squad leader: %s"), *NewLeader->DisplayName.ToString());
	}
	OnLeaderChanged.Broadcast(NewLeader);
	return true;
}

TArray<AOperativeCharacter*> USquadSubsystem::GetMembers() const
{
	TArray<AOperativeCharacter*> Result;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (Member.IsValid())
		{
			Result.Add(Member.Get());
		}
	}
	return Result;
}

void USquadSubsystem::SetSquadStance(EOperativeStance Stance)
{
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->SetStance(Stance);
	}
}

void USquadSubsystem::SetFollowersHolding(bool bHold)
{
	if (bFollowersHolding == bHold)
	{
		return;
	}
	bFollowersHolding = bHold;
	for (FFollowerState& Follower : Followers)
	{
		if (AOperativeCharacter* Operative = Follower.Operative.Get(); Operative && bHold)
		{
			Operative->StopOperative();
		}
		Follower.bParked = bHold;
		Follower.RepathTimeRemaining = 0.f;
	}
}

void USquadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &USquadSubsystem::HandleGameFlowChanged);
		Flow->OnTacticalPauseReleased.AddDynamic(this, &USquadSubsystem::HandleTacticalPauseReleased);
	}
}

bool USquadSubsystem::IsFormationActive() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	// Godot: `is_tactical_mode` is set for everyone when preparation starts; formation only in exploration.
	return !Flow || Flow->GetPhase() == ECodexGamePhase::Exploration;
}

void USquadSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (CombatMode == ECodexCombatMode::TacticalPause && LastCombatMode != ECodexCombatMode::TacticalPause)
	{
		BeginOrderPlanning();
	}
	else if (CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::RealTime)
	{
		// Leaving the pause into turn-based combat or out of the wave drops the plans; a release to real time
		// is followed by OnTacticalPauseReleased, which executes them.
		PlannedOrders.Reset();
		PauseOrigins.Reset();
	}
	LastCombatMode = CombatMode;
}

void USquadSubsystem::HandleTacticalPauseReleased()
{
	ExecutePlannedOrders();
}

void USquadSubsystem::BeginOrderPlanning()
{
	PlannedOrders.Reset();
	PauseOrigins.Reset();
	for (AOperativeCharacter* Member : GetMembers())
	{
		PauseOrigins.Add(Member, Member->GetActorLocation());
	}
}

FVector USquadSubsystem::PlanMove(AOperativeCharacter* Operative, const FVector& Destination, bool bSprint, float Radius)
{
	if (!Operative)
	{
		return Destination;
	}
	const FVector* Origin = PauseOrigins.Find(Operative);
	const FVector Planned = SquadFormation::ClampToRadius2D(Origin ? *Origin : Operative->GetActorLocation(), Destination, Radius);
	FPlannedOrder& Order = PlannedOrders.FindOrAdd(Operative);
	Order.Destination = Planned;
	Order.bSprint = bSprint;
	return Planned;
}

void USquadSubsystem::ExecutePlannedOrders()
{
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FPlannedOrder>& Entry : PlannedOrders)
	{
		if (AOperativeCharacter* Operative = Entry.Key.Get())
		{
			Operative->OrderMoveTo(Entry.Value.Destination, Entry.Value.bSprint);
		}
	}
	PlannedOrders.Reset();
	PauseOrigins.Reset();
}

int32 USquadSubsystem::GetFormationSlot(const AOperativeCharacter* Operative) const
{
	for (const FFollowerState& Follower : Followers)
	{
		if (Follower.Operative.Get() == Operative)
		{
			return Follower.Slot;
		}
	}
	return INDEX_NONE;
}

void USquadSubsystem::RebuildFollowers()
{
	Followers.Reset();
	int32 NextSlot = 0;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (Member.IsValid() && Member != Leader)
		{
			FFollowerState& State = Followers.AddDefaulted_GetRef();
			State.Operative = Member;
			State.Slot = NextSlot++;
		}
	}
	SlotSwapCooldownRemaining = 0.f;
}

void USquadSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef)
	{
		return;
	}

	FormationHeading = SquadFormation::SmoothHeading(FormationConfig, FormationHeading, LeaderRef->GetActorForwardVector(), DeltaTime);
	UpdateSlotSwap(DeltaTime);

	if (!IsFormationActive())
	{
		return;
	}

	const bool bLeaderMoving = LeaderRef->IsMoving();
	const float TimeSeconds = GetWorld()->GetTimeSeconds();
	for (FFollowerState& Follower : Followers)
	{
		UpdateFollower(Follower, *LeaderRef, bLeaderMoving, DeltaTime, TimeSeconds);
	}
}

void USquadSubsystem::UpdateSlotSwap(float DeltaTime)
{
	if (SlotSwapCooldownRemaining > 0.f)
	{
		SlotSwapCooldownRemaining -= DeltaTime;
		return;
	}

	FFollowerState* Slot0 = Followers.FindByPredicate([](const FFollowerState& F) { return F.Slot == 0; });
	FFollowerState* Slot1 = Followers.FindByPredicate([](const FFollowerState& F) { return F.Slot == 1; });
	if (!Slot0 || !Slot1 || !Slot0->Operative.IsValid() || !Slot1->Operative.IsValid())
	{
		return;
	}

	if (SquadFormation::ShouldSwapSlots(FormationConfig, Leader->GetActorLocation(), FormationHeading,
		Slot0->Operative->GetActorLocation(), Slot1->Operative->GetActorLocation()))
	{
		Slot0->Slot = 1;
		Slot1->Slot = 0;
		Slot0->RepathTimeRemaining = 0.f;
		Slot1->RepathTimeRemaining = 0.f;
		SlotSwapCooldownRemaining = FormationConfig.SlotSwapCooldown;
	}
}

void USquadSubsystem::UpdateFollower(FFollowerState& Follower, AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime, float TimeSeconds)
{
	AOperativeCharacter* Operative = Follower.Operative.Get();
	if (!Operative || bFollowersHolding)
	{
		return;
	}

	// Followers copy the moving leader's stance and sprint (Godot: can_sync_stance, leader_sprinting).
	if (bLeaderMoving)
	{
		Operative->SetStance(LeaderRef.GetStance());
	}
	Operative->SetSprinting(LeaderRef.IsSprinting());

	Follower.RepathTimeRemaining -= DeltaTime;
	Follower.ColumnTimeRemaining = FMath::Max(0.f, Follower.ColumnTimeRemaining - DeltaTime);
	if (Follower.RepathTimeRemaining > 0.f)
	{
		return;
	}
	Follower.RepathTimeRemaining = FormationConfig.RepathInterval;

	const FVector LeaderLocation = LeaderRef.GetActorLocation();
	FVector SlotLocation = SquadFormation::ComputeSlotPosition(FormationConfig, LeaderLocation, FormationHeading, Follower.Slot, false);
	if (IsSlotPathBlocked(LeaderRef, SlotLocation))
	{
		Follower.ColumnTimeRemaining = FormationConfig.ColumnHoldTime;
	}
	if (Follower.ColumnTimeRemaining > 0.f)
	{
		SlotLocation = SquadFormation::ComputeSlotPosition(FormationConfig, LeaderLocation, FormationHeading, Follower.Slot, true);
	}
	if (bLeaderMoving)
	{
		SlotLocation += SquadFormation::ComputeWanderOffset(FormationConfig, FormationHeading, Follower.Slot, TimeSeconds);
	}
	SlotLocation.Z = Operative->GetActorLocation().Z;

	const float Distance = FVector::Dist2D(Operative->GetActorLocation(), SlotLocation);
	const float Speed = SquadFormation::ComputeFollowerSpeed(FormationConfig, Operative->GetMaxSpeed(), Distance, Follower.Slot, TimeSeconds, bLeaderMoving);

	if (Speed <= 0.f)
	{
		if (!Follower.bParked)
		{
			Operative->StopOperative();
			Follower.bParked = true;
		}
		// Parked followers face where the leader faces (Godot _align_rotation_with_leader).
		const FRotator Target(0.f, LeaderRef.GetActorRotation().Yaw, 0.f);
		Operative->SetActorRotation(FMath::RInterpTo(Operative->GetActorRotation(), Target, FormationConfig.RepathInterval, ParkedAlignSpeed));
		return;
	}

	Follower.bParked = false;
	Operative->FollowTo(SlotLocation, Speed);
}

bool USquadSubsystem::IsSlotPathBlocked(const AOperativeCharacter& LeaderRef, const FVector& SlotLocation) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadSlotTrace), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (Member.IsValid())
		{
			Params.AddIgnoredActor(Member.Get());
		}
	}
	const FVector Offset(0.f, 0.f, SlotTraceHeight);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, LeaderRef.GetActorLocation() + Offset, SlotLocation + Offset, ECC_Visibility, Params);
}
