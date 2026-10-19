#include "Characters/SquadSubsystem.h"
#include "Survival/WarmIdleSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadOrderRules.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TrapSpotRules.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Combat/TargetedShotRules.h"
#include "Data/TacticalHeatTuning.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "SquadSubsystem"

namespace
{
	/** Height above the actor origin for the slot visibility trace, cm (Godot: 0.6 m). */
	constexpr float SlotTraceHeight = 60.f;
	/** A parked follower stays parked until its slot is this much farther than StopRadius, cm. */
	constexpr float ParkedRestartMargin = 40.f;

	TAutoConsoleVariable<int32> CVarPostureDefensiveSquadWide(TEXT("Codex.Posture.DefensiveSquadWide"), 0,
		TEXT("1: an attack on any squad member provokes every Defensive operative; 0: only the one attacked (user request 2026-10-06)"));
	TAutoConsoleVariable<int32> CVarPostureAggressiveExplorationFire(TEXT("Codex.Posture.AggressiveExplorationFire"), 1,
		TEXT("1: Aggressive operatives open fire on enemies they see while exploring an ambush level (starting the fight); 0: never"));
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
	SelectedGroup.Remove(Operative); // a fallen member drops out of the selection too
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
		// Drop the formation move the new leader was following; it now waits for player orders. Rev 5g (game designer
		// 2026-10-14): an order of his own runs on (selecting him must not cancel it - SquadOrderRules).
		if (SquadOrderRules::ShouldStopOnBecomingLeader(IsOnOwnOrder(NewLeader)))
		{
			NewLeader->StopOperative();
		}
		FormationHeading = NewLeader->GetActorForwardVector();
		RebuildFollowers();
		if (bIsSoloMode)
		{
			// Reapply holding and crouch to followers in solo mode. Rev 5g: the old leader (or anyone) still executing his own
			// order finishes it first, then holds where it ended (bug: selecting another operative in Solo stopped him).
			for (FFollowerState& Follower : Followers)
			{
				if (AOperativeCharacter* Operative = Follower.Operative.Get();
					Operative && SquadOrderRules::ShouldHoldNow(IsOnOwnOrder(Operative)))
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
	// The old leader loses, the new one gets the gold ring at once (Sprint 06-A).
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		if (AOperativeCharacter* Operative = Member.Get())
		{
			Operative->UpdateSelectionRing();
		}
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

void USquadSubsystem::SetFollowersHolding(bool bHold)
{
	if (bFollowersHolding == bHold)
	{
		return;
	}
	bFollowersHolding = bHold;
	for (FFollowerState& Follower : Followers)
	{
		// Rev 5g: holding stops the formation walks; an order of his own still finishes (he holds where it ends).
		if (AOperativeCharacter* Operative = Follower.Operative.Get();
			Operative && bHold && SquadOrderRules::ShouldHoldNow(IsOnOwnOrder(Operative)))
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

void USquadSubsystem::SetAutonomousSquadCombat(bool bEnabled)
{
	if (bAutonomousSquadCombat == bEnabled)
	{
		return;
	}
	bAutonomousSquadCombat = bEnabled;
	UE_LOG(LogCodexTactics, Display, TEXT("Commander Mode (autonomous squad combat): %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
	{
		Messages->PostMessage(FText::FromString(TEXT("Commander")), FText::FromString(bEnabled
			? TEXT("Autonomy ON. Operatives hold positions within 7 m of the order point on their own (Ctrl + T - turn off).")
			: TEXT("Autonomy OFF. Full manual control.")));
	}
}

FFirePostureConfig USquadSubsystem::GetPostureConfig()
{
	FFirePostureConfig Config;
	Config.bDefensiveSquadWideProvocation = CVarPostureDefensiveSquadWide.GetValueOnGameThread() != 0;
	Config.bAggressiveOpensFireInExploration = CVarPostureAggressiveExplorationFire.GetValueOnGameThread() != 0;
	return Config;
}

void USquadSubsystem::SetSquadPosture(ESquadFirePosture Posture)
{
	SquadPosture = Posture;
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->bHasPostureOverride = false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Squad fire posture: %s"), *FirePostureRules::GetLabel(Posture));
}

TArray<AOperativeCharacter*> USquadSubsystem::GetPostureOrderTargets() const
{
	if (HasMultiSelection())
	{
		return GetSelectedGroup();
	}
	TArray<AOperativeCharacter*> Targets;
	if (AOperativeCharacter* Controlled = GetLeader())
	{
		Targets.Add(Controlled);
	}
	return Targets;
}

int32 USquadSubsystem::ApplyPostureOrder(ESquadFirePosture Posture, bool bSquadWide)
{
	const FText Name = FText::FromString(FirePostureRules::GetLabel(Posture));
	UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	const TArray<AOperativeCharacter*> Targets = GetPostureOrderTargets();
	if (FirePostureRules::GetOrderScope(bSquadWide, Targets.Num()) == EPostureOrderScope::Squad)
	{
		SetSquadPosture(Posture);
		for (AOperativeCharacter* Member : GetMembers())
		{
			UFloatingTextSubsystem::SpawnAboveOperative(Member, FString::Printf(TEXT("🎯 %s"), *Name.ToString()), FLinearColor(1.f, 0.8f, 0.3f));
		}
		if (Messages)
		{
			Messages->PostMessage(LOCTEXT("PostureSpeaker", "SQUAD"), FText::Format(LOCTEXT("SquadPosture", "🎯 Squad fire posture: {0}"), Name));
		}
		return GetMembers().Num();
	}
	for (AOperativeCharacter* Member : Targets)
	{
		Member->bHasPostureOverride = true;
		Member->PostureOverride = Posture;
		UFloatingTextSubsystem::SpawnAboveOperative(Member, FString::Printf(TEXT("🎯 %s"), *Name.ToString()), FLinearColor(1.f, 0.8f, 0.3f));
	}
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("PostureSpeaker", "SQUAD"), Targets.Num() == 1
			? FText::Format(LOCTEXT("UnitPosture", "🎯 Fire posture of {0}: {1} (Alt - whole squad)"), Targets[0]->DisplayName, Name)
			: FText::Format(LOCTEXT("GroupPosture", "🎯 Fire posture of selected operatives ({0}): {1}"), Targets.Num(), Name));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Fire posture %s for %d selected operatives"), *FirePostureRules::GetLabel(Posture), Targets.Num());
	return Targets.Num();
}

ESquadFirePosture USquadSubsystem::GetEffectivePosture(const AOperativeCharacter* Operative) const
{
	return Operative ? FirePostureRules::Resolve(SquadPosture, Operative->bHasPostureOverride, Operative->PostureOverride) : SquadPosture;
}

int32 USquadSubsystem::CountPostureOverrides() const
{
	int32 Count = 0;
	for (const AOperativeCharacter* Member : GetMembers())
	{
		Count += Member->bHasPostureOverride && Member->PostureOverride != SquadPosture ? 1 : 0;
	}
	return Count;
}

void USquadSubsystem::NotifyMemberAttacked(AOperativeCharacter* Operative)
{
	if (!bSquadProvoked)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Squad provoked: %s was attacked"), Operative ? *Operative->DisplayName.ToString() : TEXT("?"));
	}
	bSquadProvoked = true;
}

bool USquadSubsystem::ToggleAutonomousSquadCombat()
{
	SetAutonomousSquadCombat(!bAutonomousSquadCombat);
	return bAutonomousSquadCombat;
}

void USquadSubsystem::EnterSoloMode()
{
	EnterSoloModeInternal(false);
}

void USquadSubsystem::EnterSoloModeInternal(bool bForTrap)
{
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef)
	{
		return;
	}

	bIsSoloMode = true;
	bTrapSolo = bForTrap;
	SetFollowersHolding(true);

	// Followers crouch when holding in solo mode (Godot solo_wait_stance = 1 / CROUCHING); one still on his own order walks on in
	// his stance (rev 5g). Rev 5h: the trap hold leaves an operative in cover as he is (a crouch would drop a corner peek's pose).
	for (FFollowerState& Follower : Followers)
	{
		if (AOperativeCharacter* Operative = Follower.Operative.Get();
			Operative && SquadOrderRules::ShouldHoldNow(IsOnOwnOrder(Operative)) && !(bForTrap && Operative->bInCover))
		{
			Operative->SetStance(EOperativeStance::Crouching);
			UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🛡️ GUARD: CROUCHED"), FLinearColor(0.3f, 0.9f, 0.4f));
		}
	}
	UFloatingTextSubsystem::SpawnAboveOperative(LeaderRef, TEXT("👤 SOLO MODE [B]"), FLinearColor(0.2f, 0.9f, 1.f));

	if (UGameMessageSubsystem* Messages = bForTrap ? nullptr : GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LeaderRef->DisplayName,
			FText::Format(LOCTEXT("SoloModeOn", "👤 [SOLO MODE: ON] {0} scouts alone (max 25 m). Teammates hold their positions, crouched!"),
				LeaderRef->DisplayName));
	}
}

void USquadSubsystem::RestoreSoloForLoad(bool bSolo, bool bInTrapSolo)
{
	bIsSoloMode = bSolo;
	bTrapSolo = bSolo && bInTrapSolo;
	SetFollowersHolding(bSolo);
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (LeaderRef && HasMultiSelection())
	{
		SetSelectedGroup({ LeaderRef }, false); // the next order goes to the selected operative alone
	}
	if (bSolo)
	{
		for (FFollowerState& Follower : Followers)
		{
			if (AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				Operative->StopOperative(); // they hold where the save put them
			}
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Squad: Solo restored by a load: %s%s"), bSolo ? TEXT("ON") : TEXT("off"), bTrapSolo ? TEXT(" (trap Solo)") : TEXT(""));
}

void USquadSubsystem::HoldForSpottedTrap(AOperativeCharacter* Spotter, const FText& TrapName)
{
	// Godot _on_mine_spotted: the whole squad stops.
	int32 Living = 0;
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->StopOperative();
		Living += Member->HealthComponent && Member->HealthComponent->IsAlive() ? 1 : 0;
	}
	const ETrapSpotResponse Response = TrapSpotRules::DecideSquadResponse(IsFormationActive(), bAutonomousSquadCombat, Living);
	UE_LOG(LogCodexTactics, Display, TEXT("Trap spotted (%s) by %s: squad holds%s"), *TrapName.ToString(),
		Spotter ? *Spotter->DisplayName.ToString() : TEXT("-"), Response == ETrapSpotResponse::HoldAndSolo ? TEXT(", Solo on")
		: bAutonomousSquadCombat ? TEXT(" (AUTO: paths go round it, no Solo)") : TEXT(" (no Solo)"));
	if (Response != ETrapSpotResponse::HoldAndSolo)
	{
		return;
	}
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (LeaderRef && HasMultiSelection())
	{
		SetSelectedGroup({ LeaderRef }, false); // the next order goes to the selected operative alone
	}
	if (!bIsSoloMode)
	{
		EnterSoloModeInternal(true);
	}
	else
	{
		bTrapSolo = true; // already scouting alone: the leash is off now as well
	}
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("TrapSpeaker", "SQUAD"),
			FText::Format(LOCTEXT("TrapHold", "{0} spotted - squad holding. Move them one by one (B regroups)."), TrapName));
	}
}

void USquadSubsystem::ExitSoloMode(bool bCausedByLeash, float Distance)
{
	if (!bIsSoloMode)
	{
		return;
	}

	bIsSoloMode = false;
	bTrapSolo = false;
	SetFollowersHolding(false);

	AOperativeCharacter* LeaderRef = Leader.Get();
	const FText LeaderName = LeaderRef ? LeaderRef->DisplayName : LOCTEXT("SquadDefaultName", "Squad");

	// Followers sync stance to leader and resume formation
	if (LeaderRef)
	{
		for (FFollowerState& Follower : Followers)
		{
			if (AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				// Rev 5g: Regroup ends a follower's own move order - everybody comes back to the leader (a use / object task
				// still finishes first, SquadOrderRules).
				Operative->ReleaseOwnMoveOrder();
				// ... and a follower hugging a wall (e.g. stacked at a door in Solo) leaves it ("Teammates leave cover and regroup").
				if (Operative->bInCover)
				{
					Operative->LeaveCoverToMove(TEXT("regroup"));
				}
				Operative->SetStance(LeaderRef->GetStance());
				UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🏃 REGROUPING"), FLinearColor(1.f, 0.85f, 0.2f));
			}
		}
	}

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		if (bCausedByLeash)
		{
			Messages->PostMessage(LeaderName,
				FText::Format(LOCTEXT("SoloModeLeash", "⚠️ Solo range exceeded ({0} m > 25.0 m)! Teammates get up and regroup!"),
					FText::AsNumber(FMath::RoundToFloat(Distance * 10.f) / 10.f)));
		}
		else
		{
			Messages->PostMessage(LeaderName,
				LOCTEXT("SoloModeOff", "👥 [SOLO MODE: OFF] Teammates leave cover and regroup!"));
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
	// Fire posture: a provocation lasts for one fight (user request 2026-10-06).
	if (FirePostureRules::ClearsProvocation(Phase))
	{
		bSquadProvoked = false;
		for (AOperativeCharacter* Member : GetMembers())
		{
			Member->bProvokedThisFight = false;
		}
	}
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
		DeferredShots.Reset();
	}
	LastCombatMode = CombatMode;
}

void USquadSubsystem::HandleTacticalPauseReleased()
{
	// Godot execute_planned_tactical_orders: targeted shots first, then moves. Quick fire (Ctrl + click on a point, 2026-10-19) before them: the
	// planned burst becomes an order now (it waits for its own turn / reload, see AOperativeCharacter::OrderQuickFire).
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->ExecutePlannedQuickFire();
	}
	for (AOperativeCharacter* Member : GetMembers())
	{
		if (Member->ExecutePlannedTargetedShots())
		{
			DeferredShots.Remove(Member);
			continue;
		}
		// Bug 2026-10-08: the shooter cannot fire yet (reloading after the auto-fire, misfire, frozen weapon) - the shot
		// used to be dropped silently; it is retried in the real-time fight instead.
		DeferredShots.Add(Member, 0.f);
		UE_LOG(LogCodexTactics, Display, TEXT("%s: planned targeted shot deferred (weapon not ready)"), *Member->DisplayName.ToString());
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Member->DisplayName, NSLOCTEXT("SquadSubsystem", "ShotDeferred",
				"⏳ Weapon not ready - I'll take the shot as soon as I can!"));
		}
	}
	ExecutePlannedOrders();
}

void USquadSubsystem::RetryPlannedShots(float DeltaTime)
{
	if (DeferredShots.IsEmpty())
	{
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	// Keys copied: a shot can end the wave (barrel blast), and the flow change resets DeferredShots.
	TArray<TWeakObjectPtr<AOperativeCharacter>> Shooters;
	DeferredShots.GetKeys(Shooters);
	for (const TWeakObjectPtr<AOperativeCharacter>& Key : Shooters)
	{
		float* Elapsed = DeferredShots.Find(Key);
		AOperativeCharacter* Operative = Key.Get();
		if (!Elapsed)
		{
			continue;
		}
		if (!Operative || Operative->GetPlannedTargetedShotCount() == 0)
		{
			DeferredShots.Remove(Key);
			continue;
		}
		const EPlannedShotRetry Retry = Flow
			? TargetedShotRules::GetPlannedShotRetry(Flow->GetPhase(), Flow->GetCombatMode(), *Elapsed)
			: EPlannedShotRetry::GiveUp;
		if (Retry == EPlannedShotRetry::Wait)
		{
			continue; // paused again: the next release fires it
		}
		if (Retry == EPlannedShotRetry::GiveUp)
		{
			Operative->ClearPlannedTargetedShots();
			DeferredShots.Remove(Key);
			if (Flow && Flow->GetCombatMode() == ECodexCombatMode::RealTime)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("%s: deferred targeted shot given up"), *Operative->DisplayName.ToString());
				if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
				{
					Messages->PostMessage(Operative->DisplayName, NSLOCTEXT("SquadSubsystem", "ShotGivenUp",
						"❌ Can't take the shot - order cancelled."));
				}
			}
			continue;
		}
		*Elapsed += DeltaTime;
		if (Operative->ExecutePlannedTargetedShots())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("%s: deferred targeted shot fired"), *Operative->DisplayName.ToString());
			DeferredShots.Remove(Key);
		}
	}
}

void USquadSubsystem::BeginOrderPlanning()
{
	PlannedOrders.Reset();
	PauseOrigins.Reset();
	for (AOperativeCharacter* Member : GetMembers())
	{
		// A shot deferred from the last release stays planned (the next release fires it); other plans start fresh.
		if (!DeferredShots.Contains(Member))
		{
			Member->ClearPlannedTargetedShots();
		}
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

bool USquadSubsystem::IsOnOwnOrder(const AOperativeCharacter* Operative) const
{
	if (!Operative)
	{
		return false;
	}
	SquadOrderRules::FOwnOrderFacts Facts;
	Facts.bOwnMoveOrder = Operative->HasOwnMoveOrder();
	const UWorld* World = GetWorld();
	if (const UInteractionSubsystem* Interaction = World ? World->GetSubsystem<UInteractionSubsystem>() : nullptr)
	{
		Facts.bUseOrder = Interaction->HasUseOrder(Operative);
	}
	if (const URelocationSubsystem* Relocation = World ? World->GetSubsystem<URelocationSubsystem>() : nullptr)
	{
		Facts.bObjectTask = Relocation->HasActiveTask(Operative);
	}
	return SquadOrderRules::IsOnOwnOrder(Facts);
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
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, Operative->bGuarding ? TEXT("🛡️ GUARD: HELD") : TEXT("🏃 REGROUP"),
		Operative->bGuarding ? FLinearColor(0.3f, 0.9f, 0.5f) : FLinearColor(1.f, 0.85f, 0.2f));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Name, FText::Format(Operative->bGuarding
			? NSLOCTEXT("SquadSubsystem", "GuardOn", "🛡️ [{0}]: Guard point set! Holding the sector.")
			: NSLOCTEXT("SquadSubsystem", "GuardOff", "👥 [{0}]: Off guard, regrouping!"), Name));
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
	RetryPlannedShots(DeltaTime);

	AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef)
	{
		return;
	}

	if (bIsSoloMode && !bTrapSolo) // rev 5h: no leash after a spotted trap (a regroup would walk the formation through it)
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
	// APC P1: aboard a vehicle he rides; with the leader aboard the ones outside hold their spots. P2: walking to a door
	// he is not pulled back to his formation slot (the leader waiting at the door for the group fade stood still).
	if (VehicleQueries::IsMounted(Operative) || VehicleQueries::IsMounted(&LeaderRef) || VehicleQueries::IsBoarding(Operative))
	{
		return;
	}
	// Weather idle (user request 2026-10-12): a follower at his seat / hand-warming place by a fire stays there while the leader is
	// within that fire's warm zone (only the operatives actually ordered get up); the leader leaving it takes the squad along.
	if (UWarmIdleSubsystem::ShouldFollowerHoldWarmSpot(*Operative, LeaderRef))
	{
		return;
	}
	// Level obstacles (2026-10-13): a follower held at a one-at-a-time bridge or a low fence waits where he stands until the
	// actor releases him (the formation would otherwise push him back in every repath).
	if (Operative->IsTrafficHeld())
	{
		if (Operative->GetVelocity().SizeSquared2D() > 25.f)
		{
			Operative->StopOperative();
		}
		Follower.RepathTimeRemaining = 0.f;
		return;
	}

	// Rev 5g (game designer 2026-10-14): a follower executing an order of his own (the old leader after a selection switch, a group
	// move, a use / push task) finishes it; the formation takes him on the next repath after it ended.
	if (IsOnOwnOrder(Operative))
	{
		Follower.bParked = false;
		Follower.RepathTimeRemaining = 0.f;
		return;
	}

	// Followers copy the moving leader's sprint. Deviation (user decision 2026-10-01): every operative keeps the stance
	// he was given — no copy of the leader's stance (Godot can_sync_stance); only Alt + Z / C / V sets the whole squad.
	Operative->SetSprinting(LeaderRef.IsSprinting());

	// Bug 2026-10-11 (solo off: prone followers slid towards the leader while still rising): a stance change in progress roots
	// the follower - he stays put until the clip ends, then the next repath sends him to his slot.
	if (const float MoveHold = Operative->GetMoveHoldSeconds(); OperativeMovementRules::ShouldDeferMove(MoveHold))
	{
		if (Operative->GetVelocity().SizeSquared2D() > 25.f && !Operative->IsProneDiving()) // rev 5f: a dive slides on (root motion)
		{
			Operative->StopOperative(); // walking when the stance change started
		}
		Follower.RepathTimeRemaining = MoveHold;
		return;
	}

	Follower.RepathTimeRemaining -= DeltaTime;
	Follower.ColumnTimeRemaining = FMath::Max(0.f, Follower.ColumnTimeRemaining - DeltaTime);
	if (Follower.RepathTimeRemaining > 0.f)
	{
		return;
	}
	Follower.RepathTimeRemaining = FormationConfig.RepathInterval;

	// Squad gathers at the fire (user decision 2026-10-12): while the leader warms at a barrel / campfire / stove the follower's follow
	// target is that source's ring instead of his formation slot; once he stands in the zone the weather idle gives him a seat / place.
	// Exploration formation only (solo and held followers returned above, guards are not followers); an explicit order to him leaves
	// the pull, the leader leaving the zone ends it.
	if (UWarmIdleSubsystem* WarmIdle = GetWorld()->GetSubsystem<UWarmIdleSubsystem>())
	{
		UWarmIdleSubsystem::FFollowerPull Pull;
		if (WarmIdle->GetFollowerPullTarget(*Operative, LeaderRef, Pull))
		{
			if (Pull.bArrived)
			{
				if (!Follower.bParked)
				{
					Operative->StopOperative();
					Follower.bParked = true;
				}
				Operative->SetIdleFacingYaw(WarmIdleRules::FaceYaw(Operative->GetActorLocation(), Pull.Fire));
			}
			else
			{
				Follower.bParked = false;
				// The path follower stops AcceptanceRadius short of its goal: aim that far past the spot (as UWarmIdleSubsystem::DriveMember).
				const FVector Past = Pull.Target + (Pull.Target - Operative->GetActorLocation()).GetSafeNormal2D() * Operative->MovementConfig.AcceptanceRadius;
				Operative->FollowTo(Past, TacticalHeatTuning::Get().WarmSpotWalkSpeed);
			}
			return;
		}
	}

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
	// Rev 5g (game designer 2026-10-14): in group mode a follower hugging a wall leaves the cover and follows once the leader walks
	// away (Solo / holding returned above, guards are no followers, the formation is off in a fight - the autonomy / ROE rules
	// decide there). A leader stacking up at cover himself keeps the ones already in cover where they are.
	if (Operative->bInCover)
	{
		SquadOrderRules::FCoverFollowFacts CoverFacts;
		CoverFacts.bInCover = true;
		CoverFacts.bFollowersHolding = bFollowersHolding;
		CoverFacts.bGuarding = Operative->bGuarding;
		CoverFacts.bFormationActive = IsFormationActive();
		CoverFacts.bLeaderMoving = bLeaderMoving;
		CoverFacts.bLeaderTakingCover = LeaderRef.bInCover || LeaderRef.HasPendingCover();
		CoverFacts.DistanceToSlotCm = Distance;
		if (!SquadOrderRules::ShouldLeaveCoverToFollow(CoverFacts))
		{
			return; // he holds his wall (FollowTo would refuse anyway)
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: the leader walks away (slot %.0f cm off) - leaves the cover and follows"),
			*Operative->DisplayName.ToString(), Distance);
		Operative->LeaveCoverToMove(TEXT("follows the leader"));
		// Crouched at a low wall: he stands up to walk with a standing leader (the clip roots him, the next repath walks him).
		if (Operative->GetStance() == EOperativeStance::Crouching && LeaderRef.GetStance() == EOperativeStance::Standing)
		{
			Operative->SetStance(EOperativeStance::Standing);
		}
		if (const float LeaveHold = Operative->GetMoveHoldSeconds(); OperativeMovementRules::ShouldDeferMove(LeaveHold))
		{
			Follower.bParked = false;
			Follower.RepathTimeRemaining = LeaveHold;
			return;
		}
	}
	float Speed = SquadFormation::ComputeFollowerSpeed(FormationConfig, Operative->GetMaxSpeed(), Distance, Follower.Slot, TimeSeconds, bLeaderMoving);
	// A parked follower restarts only once the slot (it wanders) is clearly away: stopping and restarting every repath
	// made the legs tremble between the idle and the walk (user report 2026-10-01).
	if (Follower.bParked && Distance < FormationConfig.StopRadius + ParkedRestartMargin)
	{
		Speed = 0.f;
	}

	if (Speed <= 0.f)
	{
		if (!Follower.bParked)
		{
			Operative->StopOperative();
			Follower.bParked = true;
		}
		// Parked followers face where the leader faces (Godot _align_rotation_with_leader), turned every frame by the
		// operative's facing (FacingRules).
		Operative->SetIdleFacingYaw(LeaderRef.GetActorRotation().Yaw);
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
