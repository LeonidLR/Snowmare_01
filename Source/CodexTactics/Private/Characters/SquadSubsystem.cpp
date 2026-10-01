#include "Characters/SquadSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "SquadSubsystem"

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

void USquadSubsystem::SetSelectedGroup(const TArray<AOperativeCharacter*>& Group, bool bSwitchLeader)
{
	for (const TWeakObjectPtr<AOperativeCharacter>& Old : SelectedGroup)
	{
		if (AOperativeCharacter* Operative = Old.Get())
		{
			Operative->SetGroupSelected(false, false);
		}
	}
	SelectedGroup.Reset();
	for (AOperativeCharacter* Operative : Group)
	{
		if (Operative)
		{
			SelectedGroup.Add(Operative);
		}
	}
	const bool bMulti = SelectedGroup.Num() > 1;
	for (const TWeakObjectPtr<AOperativeCharacter>& Selected : SelectedGroup)
	{
		Selected->SetGroupSelected(true, bMulti);
	}
	if (bSwitchLeader && !SelectedGroup.IsEmpty() && !SelectedGroup.Contains(Leader))
	{
		SetLeader(SelectedGroup[0].Get(), false);
	}
}

TArray<AOperativeCharacter*> USquadSubsystem::GetSelectedGroup() const
{
	TArray<AOperativeCharacter*> Result;
	for (const TWeakObjectPtr<AOperativeCharacter>& Selected : SelectedGroup)
	{
		AOperativeCharacter* Operative = Selected.Get();
		if (Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive())
		{
			Result.Add(Operative);
		}
	}
	if (Result.IsEmpty() && Leader.IsValid())
	{
		Result.Add(Leader.Get());
	}
	return Result;
}

bool USquadSubsystem::IsGroupSelected(const AOperativeCharacter* Operative) const
{
	return Operative && SelectedGroup.ContainsByPredicate([Operative](const TWeakObjectPtr<AOperativeCharacter>& Selected) { return Selected.Get() == Operative; });
}

bool USquadSubsystem::HasMultiSelection() const
{
	return GetSelectedGroup().Num() > 1;
}

bool USquadSubsystem::SetLeader(AOperativeCharacter* NewLeader, bool bResetGroup)
{
	if (!NewLeader || !Members.Contains(NewLeader))
	{
		return false;
	}
	const bool bDropGroup = bResetGroup && (SelectedGroup.Num() > 1 || (SelectedGroup.Num() == 1 && SelectedGroup[0].Get() != NewLeader));
	if (Leader.Get() != NewLeader)
	{
		Leader = NewLeader;
		// Drop the formation move the new leader was following; it now waits for player orders.
		NewLeader->StopOperative();
		FormationHeading = NewLeader->GetActorForwardVector();
		RebuildFollowers();
		if (bIsSoloMode)
		{
			// Reapply holding and crouch to followers in solo mode
			for (FFollowerState& Follower : Followers)
			{
				if (AOperativeCharacter* Operative = Follower.Operative.Get())
				{
					Operative->StopOperative();
					Operative->SetStance(EOperativeStance::Crouching);
				}
				Follower.bParked = true;
				Follower.RepathTimeRemaining = 0.f;
			}
		}
		UE_LOG(LogCodexTactics, Log, TEXT("Squad leader: %s"), *NewLeader->DisplayName.ToString());
	}
	if (bDropGroup)
	{
		SetSelectedGroup({ NewLeader }, false); // after the switch, so the rings see the new leader
	}
	OnLeaderChanged.Broadcast(NewLeader);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSquadMemberSelected.Broadcast(NewLeader);
	}
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

void USquadSubsystem::SyncSquadStance(EOperativeStance Stance)
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (bIsSoloMode || (Flow && Flow->GetPhase() == ECodexGamePhase::Preparation))
	{
		return;
	}
	for (AOperativeCharacter* Member : GetMembers())
	{
		if (Member != GetLeader() && !Member->bGuarding && !Member->bHasCustomStance)
		{
			Member->SetStance(Stance);
		}
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

void USquadSubsystem::ToggleSoloMode()
{
	if (bIsSoloMode)
	{
		ExitSoloMode(false, 0.f);
	}
	else
	{
		EnterSoloMode();
	}
}

void USquadSubsystem::EnterSoloMode()
{
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef)
	{
		return;
	}

	bIsSoloMode = true;
	SetFollowersHolding(true);

	// Followers crouch when holding in solo mode (Godot solo_wait_stance = 1 / CROUCHING)
	for (FFollowerState& Follower : Followers)
	{
		if (AOperativeCharacter* Operative = Follower.Operative.Get())
		{
			Operative->SetStance(EOperativeStance::Crouching);
			UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🛡️ ОБОРОНА: ПРИСЕВ"), FLinearColor(0.3f, 0.9f, 0.4f));
		}
	}
	UFloatingTextSubsystem::SpawnAboveOperative(LeaderRef, TEXT("👤 РЕЖИМ СОЛО [B]"), FLinearColor(0.2f, 0.9f, 1.f));

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LeaderRef->DisplayName,
			FText::Format(LOCTEXT("SoloModeOn", "👤 [РЕЖИМ СОЛО: ВКЛ] {0} идёт на разведку один (макс. 25м). Напарники закрепились на позициях в присядке!"),
				LeaderRef->DisplayName));
	}
}

void USquadSubsystem::ExitSoloMode(bool bCausedByLeash, float Distance)
{
	if (!bIsSoloMode)
	{
		return;
	}

	bIsSoloMode = false;
	SetFollowersHolding(false);

	AOperativeCharacter* LeaderRef = Leader.Get();
	const FText LeaderName = LeaderRef ? LeaderRef->DisplayName : LOCTEXT("SquadDefaultName", "Отряд");

	// Followers sync stance to leader and resume formation
	if (LeaderRef)
	{
		for (FFollowerState& Follower : Followers)
		{
			if (AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				Operative->SetStance(LeaderRef->GetStance());
				UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🏃 ВОЗВРАТ В СТРОЙ"), FLinearColor(1.f, 0.85f, 0.2f));
			}
		}
	}

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		if (bCausedByLeash)
		{
			Messages->PostMessage(LeaderName,
				FText::Format(LOCTEXT("SoloModeLeash", "⚠️ Превышена дистанция соло ({0} м > 25.0 м)! Напарники поднимаются и возвращаются в строй!"),
					FText::AsNumber(FMath::RoundToFloat(Distance * 10.f) / 10.f)));
		}
		else
		{
			Messages->PostMessage(LeaderName,
				LOCTEXT("SoloModeOff", "👥 [РЕЖИМ СОЛО: ВЫКЛ] Напарники выходят из укрытия и возвращаются в строй!"));
		}
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
		for (AOperativeCharacter* Member : GetMembers())
		{
			Member->ClearPlannedTargetedShots();
		}
	}
	LastCombatMode = CombatMode;
}

void USquadSubsystem::HandleTacticalPauseReleased()
{
	// Godot execute_planned_tactical_orders: targeted shots first, then moves.
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->ExecutePlannedTargetedShots();
	}
	ExecutePlannedOrders();
}

void USquadSubsystem::BeginOrderPlanning()
{
	PlannedOrders.Reset();
	PauseOrigins.Reset();
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->ClearPlannedTargetedShots();
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

bool USquadSubsystem::GetPauseOrigin(const AOperativeCharacter* Operative, FVector& OutOrigin) const
{
	const FVector* Origin = PauseOrigins.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
	if (Origin)
	{
		OutOrigin = *Origin;
	}
	return Origin != nullptr;
}

void USquadSubsystem::ClearPlannedOrder(const AOperativeCharacter* Operative)
{
	PlannedOrders.Remove(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
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

void USquadSubsystem::ToggleGuard(AOperativeCharacter* Operative)
{
	if (!Operative)
	{
		return;
	}
	Operative->bGuarding = !Operative->bGuarding;
	const FText Name = Operative->DisplayName;
	if (Operative->bGuarding)
	{
		Operative->StopOperative();
	}
	RebuildFollowers(); // Godot assign_formation_slots skips guards
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, Operative->bGuarding ? TEXT("🛡️ ОБОРОНА: ФИКСАЦИЯ") : TEXT("🏃 В СТРОЙ"),
		Operative->bGuarding ? FLinearColor(0.3f, 0.9f, 0.5f) : FLinearColor(1.f, 0.85f, 0.2f));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Name, FText::Format(Operative->bGuarding
			? NSLOCTEXT("SquadSubsystem", "GuardOn", "🛡️ [{0}]: Точка обороны зафиксирована! Держу позицию сектора.")
			: NSLOCTEXT("SquadSubsystem", "GuardOff", "👥 [{0}]: Снят(а) с позиции обороны, возвращается в строй!"), Name));
	}
}

void USquadSubsystem::RebuildFollowers()
{
	Followers.Reset();
	int32 NextSlot = 0;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (Member.IsValid() && Member != Leader && !Member->bGuarding)
		{
			FFollowerState& State = Followers.AddDefaulted_GetRef();
			State.Operative = Member;
			State.Slot = NextSlot++;
			State.bParked = bFollowersHolding;
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

	if (bIsSoloMode)
	{
		float MaxDist = 0.f;
		for (const FFollowerState& Follower : Followers)
		{
			if (const AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				const float Dist = FVector::Dist2D(LeaderRef->GetActorLocation(), Operative->GetActorLocation());
				if (Dist > MaxDist)
				{
					MaxDist = Dist;
				}
			}
		}

		if (MaxDist > SoloModeMaxDistance)
		{
			ExitSoloMode(true, MaxDist / 100.f);
		}
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

	// Followers copy the moving leader's stance and sprint (Godot player.gd can_sync_stance: not guarding, no stance of
	// their own, not in the preparation).
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (bLeaderMoving && !Operative->bGuarding && !Operative->bHasCustomStance && !(Flow && Flow->GetPhase() == ECodexGamePhase::Preparation))
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

#undef LOCTEXT_NAMESPACE
