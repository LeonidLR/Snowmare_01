#include "Characters/SquadSubsystem.h"
#include "Survival/WarmIdleSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadOrderRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
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
#include "NavigationSystem.h"
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
	TAutoConsoleVariable<int32> CVarSquadFollowerSprintStandUp(TEXT("Codex.Squad.SprintStandUp"), 1,
		TEXT("1 (default): a follower crouched by the squad (Regroup's copy, Solo's guard crouch, a confinement) stands up to sprint with the sprinting leader (bug 2026-10-20); 0: the old rule (he walks crouched behind him)."));
	TAutoConsoleVariable<int32> CVarSquadPostCombatFormation(TEXT("Codex.Squad.PostCombatFormation"), 1,
		TEXT("1 (default): the formation runs after the final fight too (PostCombat) and Regroup (B) ends the post-fight hold (bug 2026-10-23); 0: the old rule (no follower walks after the final fight, B leaves the hold)."));
	TAutoConsoleVariable<int32> CVarSquadSlotFallback(TEXT("Codex.Squad.SlotFallback"), 1,
		TEXT("1 (default): a formation slot off the navmesh (wall, generator, a fallen mate's remains) is replaced by the nearest navigable spot round the leader (bug 2026-10-23); 0: the old rule (the follower stays where he is)."));
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
		AOperativeCharacter* OldLeader = Leader.Get();
		const bool bSectorLook = FormationRuntime::IsEnabled();
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const TOptional<float> SquadHeading = Sectors.bHeadingValid && !FormationHeading.IsNearlyZero()
			? TOptional<float>(static_cast<float>(FormationHeading.Rotation().Yaw)) : TOptional<float>();
		if (bSectorLook && OldLeader)
		{
			LeaderFacingMemory.Add(OldLeader, { static_cast<float>(OldLeader->GetActorRotation().Yaw), Now }); // his own facing as the leader
		}
		Leader = NewLeader;
		// Drop the formation move the new leader was following; it now waits for player orders. Rev 5g (game designer
		// 2026-10-14): an order of his own runs on (selecting him must not cancel it - SquadOrderRules).
		if (SquadOrderRules::ShouldStopOnBecomingLeader(IsOnOwnOrder(NewLeader)))
		{
			NewLeader->StopOperative();
		}
		if (bSectorLook)
		{
			// Bug 2026-10-26 (formation sectors): the leader's sector is 12 - a member made the leader keeps no follower look (the rear guard's
			// 6 o'clock he turned to while another led). The heading (= his halt look for FormationRuntime::LeaderHaltLookSeconds) starts from
			// FormationRuntime::NewLeaderLookYaw: his walk, his pending order, his own facing from when he last led, else the squad's heading.
			FormationRuntime::FNewLeaderLookInputs Look;
			Look.TravelYaw = FormationRuntime::HeadingFromVelocity(FVector2D(NewLeader->GetVelocity()));
			const FVector2D ToOrder = FVector2D(NewLeader->GetLastMoveDestination() - NewLeader->GetActorLocation());
			if (NewLeader->HasOwnMoveOrder() && ToOrder.Size() > FormationRuntime::NewLeaderOrderMinCm)
			{
				Look.PendingOrderYaw = FormationMath::YawOf(ToOrder);
			}
			if (const TPair<float, double>* Remembered = LeaderFacingMemory.Find(NewLeader);
				Remembered && Now - Remembered->Value <= FormationRuntime::NewLeaderRememberSeconds)
			{
				Look.FacingAsLeaderYaw = Remembered->Key;
			}
			Look.SquadHeadingYaw = SquadHeading;
			Look.FacingYaw = static_cast<float>(NewLeader->GetActorRotation().Yaw);
			const float Yaw = FormationRuntime::NewLeaderLookYaw(Look);
			FormationHeading = FRotator(0.f, Yaw, 0.f).Vector();
			Sectors.HeadingYaw = Yaw;
			Sectors.bHeadingValid = true;
			Sectors.LeaderStillSeconds = 0.f; // standing: his halt look at 12 (= Yaw) turns him first
			Sectors.bHeadingTickPosValid = false; // the switch is no teleport of the leader
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] new leader %s looks %.0f (facing %.0f; walk %s, order %s, as leader %s, squad %s)"),
				*NewLeader->DisplayName.ToString(), Yaw, Look.FacingYaw, Look.TravelYaw.IsSet() ? *FString::Printf(TEXT("%.0f"), Look.TravelYaw.GetValue()) : TEXT("-"),
				Look.PendingOrderYaw.IsSet() ? *FString::Printf(TEXT("%.0f"), Look.PendingOrderYaw.GetValue()) : TEXT("-"),
				Look.FacingAsLeaderYaw.IsSet() ? *FString::Printf(TEXT("%.0f"), Look.FacingAsLeaderYaw.GetValue()) : TEXT("-"),
				SquadHeading.IsSet() ? *FString::Printf(TEXT("%.0f"), SquadHeading.GetValue()) : TEXT("-"));
		}
		else
		{
			FormationHeading = NewLeader->GetActorForwardVector();
			Sectors.bHeadingValid = false; // formation sectors: the heading starts from the new leader's facing
		}
		RebuildFollowers();
		if (bIsSoloMode)
		{
			// Reapply holding and crouch to followers in solo mode. Rev 5g: the old leader (or anyone) still executing his own
			// order finishes it first, then holds where it ended (bug: selecting another operative in Solo stopped him).
			for (FFollowerState& Follower : Followers)
			{
				// (2026-10-20: one lying behind a cover on a group cover order keeps lying there.)
				if (AOperativeCharacter* Operative = Follower.Operative.Get();
					Operative && SquadOrderRules::ShouldHoldNow(IsOnOwnOrder(Operative)) && !HasGroupCoverProne(Operative))
				{
					Operative->StopOperative();
					Operative->SetStance(EOperativeStance::Crouching);
					Operative->MarkStanceFromSquad(); // Solo's guard crouch (bug 2026-10-20)
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
		Member->ClearStanceFromSquad(); // a squad stance order is the player's
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
			Operative && SquadOrderRules::ShouldHoldNow(IsOnOwnOrder(Operative)) && !(bForTrap && Operative->bInCover)
			&& !HasGroupCoverProne(Operative)) // 2026-10-20: lying behind a cover on a group cover order: stays prone
		{
			Operative->SetStance(EOperativeStance::Crouching);
			Operative->MarkStanceFromSquad(); // the guard crouch is the squad's, not the player's (ShouldStandToSprintWithLeader)
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
	// Bug 2026-10-23 (lake, after the horde): Regroup is an order to come back - the post-fight hold ends with it, not only when the
	// leader moves.
	if (bPostFightHold && CVarSquadPostCombatFormation.GetValueOnGameThread() != 0)
	{
		bPostFightHold = SquadOrderRules::KeepsPostFightHold(bPostFightHold, false, /*bRegroup*/ true);
		UE_LOG(LogCodexTactics, Display, TEXT("Squad: Regroup ends the post-fight hold"));
	}

	AOperativeCharacter* LeaderRef = Leader.Get();
	const FText LeaderName = LeaderRef ? LeaderRef->DisplayName : LOCTEXT("SquadDefaultName", "Squad");

	// Followers sync stance to leader and resume formation
	if (LeaderRef)
	{
		for (FFollowerState& Follower : Followers)
		{
			if (AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				// 2026-10-23 ("lock_in_place"): an operative the tutorial holds at his spot (stacked on the hut door) stays there - his wall,
				// his stance; the formation leaves him alone (RequestMove refuses its moves) until the lesson releases him.
				if (UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>(); Tutorial && Tutorial->IsHeldInPlace(*Operative))
				{
					Tutorial->ReportPlaceLockRefusal(Operative);
					continue;
				}
				// Rev 5g: Regroup ends a follower's own move order - everybody comes back to the leader (a use / object task
				// still finishes first, SquadOrderRules).
				Operative->ReleaseOwnMoveOrder();
				// ... and a follower hugging a wall (e.g. stacked at a door in Solo) leaves it ("Teammates leave cover and regroup").
				if (Operative->bInCover)
				{
					Operative->LeaveCoverToMove(TEXT("regroup"));
				}
				DropGroupCoverProne(Operative, TEXT("regroup")); // ... and one lying behind a cover after a group cover order
				DropGroupCoverWait(Operative, TEXT("regroup")); // ... and one waiting off the wall for the leader (2026-10-23)
				Operative->SetStance(LeaderRef->GetStance());
				// Bug 2026-10-20: the copied stance is the squad's - crouched after the Solo guard crouch, he stands up to sprint with the
				// leader (UpdateFollower; Solo on / off again used to be the only cure).
				Operative->MarkStanceFromSquad();
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
	// Godot: `is_tactical_mode` is set for everyone when preparation starts and cleared at the fight's end: the formation runs while
	// exploring and after the final fight (PostCombat, bug 2026-10-23).
	return !Flow || SquadOrderRules::IsFormationPhase(Flow->GetPhase(), CVarSquadPostCombatFormation.GetValueOnGameThread() != 0);
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
	// Game designer play-test 2026-10-22: the fight is over -> nobody is walked or snapped anywhere; the followers keep the spots
	// they fought from (stance, cover) until the leader moves.
	if (Phase != LastPhase && SquadOrderRules::ArmsPostFightHold(LastPhase, Phase))
	{
		bPostFightHold = true;
		for (FFollowerState& Follower : Followers)
		{
			Follower.bParked = true;
			Follower.RepathTimeRemaining = 0.f;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Squad: the fight is over - the followers hold their spots until the leader moves"));
	}
	LastPhase = Phase;
}

void USquadSubsystem::HandleTacticalPauseReleased()
{
	// Godot execute_planned_tactical_orders: targeted shots first, then moves. Quick fire (Ctrl + click on a point, 2026-10-19) before them: the
	// planned burst becomes an order now (it waits for its own turn / reload, see AOperativeCharacter::OrderQuickFire).
	for (AOperativeCharacter* Member : GetMembers())
	{
		Member->ExecutePlannedQuickFire();
		Member->ExecutePlannedSuppress(); // the SUPPRESS order planned in the pause (2026-10-26)
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
	for (auto It = GroupCoverProneOrders.CreateIterator(); It; ++It)
	{
		if (It->Value.Phase == ESquadGroupCoverPronePhase::Planned)
		{
			It.RemoveCurrent(); // an old plan (never released): plans start fresh
		}
	}
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
	StartPlannedGroupCoverProne();
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
	// Group cover leader priority (2026-10-23): waiting off the wall for the leader is his order too (the formation leaves him alone).
	Facts.bObjectTask |= IsWaitingForGroupCoverLeader(Operative);
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
	UpdateGroupCoverProne(DeltaTime); // fights too (the formation is off there, a prone order still lies down on arrival)
	UpdateGroupCoverWait(DeltaTime); // group cover leader priority (2026-10-23)
	UpdateWallGroup(DeltaTime); // rules v2.2 wall group (Codex.Formation.Sectors): the followers' looks round the leader's fixed corner aim

	// Formation look sectors (Codex.Formation.Sectors): the follow below fills this frame's looks / the leader's pace; every member not
	// given one this frame (console off, Solo, a fight, an order of his own, ...) gets none - his old facing and speed.
	FrameLooks.Reset();
	FrameGivenByMember.Reset();
	// Rules v2.3 (contract 13.1): a ClearLook memory not refreshed last frame is gone (his look was not computed / not cleared then).
	FormationGuardRuntime::AgeClearHeld(ClearHeld);
	FramePaceLeader.Reset();
	FramePace = 1.f;
	Sectors.bActiveThisFrame = false;
	TickFormationFollow(DeltaTime);
	ApplyFormationFrame();
}

void USquadSubsystem::TickFormationFollow(float DeltaTime)
{
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

	// Formation sectors: the heading is the leader's smoothed TRAVEL direction (his body scans his sector while he walks).
	const bool bSectors = FormationRuntime::IsEnabled();
	if (bSectors != Sectors.bWasEnabled)
	{
		// The console switched (an A/B in a smoke): each formation starts from its own heading - the old one the leader's facing.
		Sectors.bWasEnabled = bSectors;
		Sectors.bHeadingValid = false;
		FormationHeading = LeaderRef->GetActorForwardVector();
		for (FFollowerState& Follower : Followers)
		{
			AOperativeCharacter* Operative = Follower.Operative.Get();
			if (Operative && Operative->IsMoving() && !IsOnOwnOrder(Operative) && !Operative->bInCover)
			{
				Operative->StopOperative(); // the other formation's move does not run on
			}
			Follower.RepathTimeRemaining = 0.f;
			Follower.PrevSectorSlotTime = -1.0;
		}
	}
	FVector HeadingTarget = LeaderRef->GetActorForwardVector();
	if (bSectors)
	{
		// A leader put somewhere (a teleport, a load) did not walk there: the heading starts from his facing, not his old travel.
		const FVector2D LeaderHere(LeaderRef->GetActorLocation());
		const float LeaderYaw = static_cast<float>(LeaderRef->GetActorRotation().Yaw);
		if (Sectors.bHeadingTickPosValid && (FVector2D::Distance(LeaderHere, Sectors.HeadingTickPos) > FormationRuntime::MaxStepDistanceCm
			|| FMath::Abs(FMath::FindDeltaAngleDegrees(Sectors.HeadingTickYaw, LeaderYaw)) > FormationRuntime::MaxTurnStepDeg))
		{
			FormationHeading = LeaderRef->GetActorForwardVector();
			Sectors.LeaderStillSeconds = FormationRuntime::LeaderHaltLookSeconds; // no turn back to an old heading
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Formation] %s was set to (%.0f, %.0f) yaw %.0f: the heading follows his facing"),
				*LeaderRef->DisplayName.ToString(), LeaderHere.X, LeaderHere.Y, LeaderYaw);
		}
		Sectors.HeadingTickPos = LeaderHere;
		Sectors.HeadingTickYaw = LeaderYaw;
		Sectors.bHeadingTickPosValid = true;
		const TOptional<float> TravelYaw = FormationRuntime::HeadingFromVelocity(FVector2D(LeaderRef->GetVelocity()));
		if (TravelYaw.IsSet())
		{
			HeadingTarget = FRotator(0.f, TravelYaw.GetValue(), 0.f).Vector();
		}
		else if (Sectors.bHeadingValid && !FormationHeading.IsNearlyZero()
			&& (Sectors.LeaderStillSeconds < FormationRuntime::LeaderHaltLookSeconds || Sectors.Formation == EFormationKind::Wall))
		{
			HeadingTarget = FormationHeading; // just stopped: the last travel direction holds while he turns his search back to 12
		}
		// (At a wall the heading stays into the wall while he faces out.) Standing longer: the heading follows his facing again (the old formation's rule) - a turn, a facing order, a teleport turn the squad.
	}
	FormationHeading = SquadFormation::SmoothHeading(FormationConfig, FormationHeading, HeadingTarget, DeltaTime);
	UpdateSlotSwap(DeltaTime);

	if (!IsFormationActive())
	{
		if (Sectors.bWallOrder)
		{
			EndFormationWallOrder(TEXT("the formation is off (a fight)"));
		}
		Sectors.bSlide = false;
		return;
	}

	const bool bLeaderMoving = LeaderRef->IsMoving();
	bPostFightHold = SquadOrderRules::KeepsPostFightHold(bPostFightHold, bLeaderMoving, false);
	if (bPostFightHold)
	{
		return;
	}
	if (bSectors)
	{
		UpdateFormationSectors(*LeaderRef, bLeaderMoving, DeltaTime);
	}
	else if (Sectors.bWallOrder)
	{
		EndFormationWallOrder(TEXT("Codex.Formation.Sectors 0"));
	}
	const float TimeSeconds = GetWorld()->GetTimeSeconds();
	// Rules v2.3 (contract 13.5): the slots given out this tick - a stand-in keeps slot.min_spacing from the leader, from every WALKABLE slot of
	// this frame (whoever resolves it later: FormationLabSmoke R_v2_gaps - Z resolved before P took the stand-in on P's spot) and from the
	// stand-ins given before it.
	FrameGivenSlots.Reset();
	FrameGivenSlots.Add(FVector2D(LeaderRef->GetActorLocation()));
	if (Sectors.bActiveThisFrame)
	{
		SeedFrameGivenSlots(*LeaderRef);
	}
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
	if (FormationRuntime::IsEnabled())
	{
		return; // formation sectors: places by minimal travel over every role (ReassignFormationPlaces), not the old left / right swap
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
	// Rules v2.2 wall group (Codex.Formation.Sectors): taken out of his cover slot into the back-to-wall pose, he holds it facing his look.
	if (HoldsWallGroupPose(Follower, *Operative))
	{
		return;
	}

	// Followers copy the moving leader's sprint. Deviation (user decision 2026-10-01): every operative keeps the stance
	// he was given — no copy of the leader's stance (Godot can_sync_stance); only Alt + Z / C / V sets the whole squad.
	// Bug 2026-10-20: a crouch the SQUAD gave him (Regroup's copy, Solo's guard crouch, a confinement) does not keep him from running with
	// the sprinting leader - he stands up (the clip defers the move below), then sprints.
	if (CVarSquadFollowerSprintStandUp.GetValueOnGameThread() != 0
		&& SquadOrderRules::ShouldStandToSprintWithLeader(LeaderRef.IsSprinting(), Operative->GetStance() == EOperativeStance::Crouching,
			Operative->IsStanceFromSquad(), Operative->CanSprint()))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Squad: %s stands up to sprint with the leader (squad-given crouch)"), *Operative->DisplayName.ToString());
		Operative->SetStance(EOperativeStance::Standing);
		Operative->MarkStanceFromSquad();
	}
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

	// Formation look sectors (Codex.Formation.Sectors): slots by role, his sector look, the speed with the slot's velocity lead.
	if (Sectors.bActiveThisFrame && UpdateSectorFollower(Follower, *Operative, LeaderRef, bLeaderMoving, DeltaTime, TimeSeconds))
	{
		return;
	}

	Follower.RepathTimeRemaining -= DeltaTime;
	Follower.ColumnTimeRemaining = FMath::Max(0.f, Follower.ColumnTimeRemaining - DeltaTime);
	if (Follower.RepathTimeRemaining > 0.f)
	{
		return;
	}
	Follower.RepathTimeRemaining = FormationConfig.RepathInterval;

	if (TryWarmPull(Follower, *Operative, LeaderRef))
	{
		return;
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
	SlotLocation = ResolveReachableSlot(LeaderRef, SlotLocation);

	const float Distance = FVector::Dist2D(Operative->GetActorLocation(), SlotLocation);
	if (HoldsCoverOrLeavesIt(Follower, *Operative, LeaderRef, bLeaderMoving, Distance))
	{
		return;
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

bool USquadSubsystem::TryWarmPull(FFollowerState& Follower, AOperativeCharacter& OperativeRef, const AOperativeCharacter& LeaderRef)
{
	AOperativeCharacter* Operative = &OperativeRef;
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
			return true;
		}
	}
	return false;
}

bool USquadSubsystem::HoldsCoverOrLeavesIt(FFollowerState& Follower, AOperativeCharacter& OperativeRef, const AOperativeCharacter& LeaderRef,
	bool bLeaderMoving, float Distance)
{
	AOperativeCharacter* Operative = &OperativeRef;
	// Rev 5g (game designer 2026-10-14): in group mode a follower hugging a wall leaves the cover and follows once the leader walks
	// away (Solo / holding returned above, guards are no followers, the formation is off in a fight - the autonomy / ROE rules
	// decide there). A leader stacking up at cover himself keeps the ones already in cover where they are.
	// Group cover order (2026-10-20): one lying behind the cover on that order holds it by the same rule as one in cover.
	const bool bGroupCoverProne = HasGroupCoverProne(Operative);
	if (Operative->bInCover || bGroupCoverProne)
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
			return true; // he holds his wall (FollowTo would refuse anyway)
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: the leader walks away (slot %.0f cm off) - leaves the cover and follows"),
			*Operative->DisplayName.ToString(), Distance);
		Operative->LeaveCoverToMove(TEXT("follows the leader"));
		if (bGroupCoverProne)
		{
			DropGroupCoverProne(Operative, TEXT("follows the leader"));
			// He lay down for the cover, not by the player's stance key: up to the leader's stance to walk with him.
			if (Operative->GetStance() == EOperativeStance::Prone && LeaderRef.GetStance() != EOperativeStance::Prone)
			{
				Operative->SetStance(LeaderRef.GetStance());
			}
		}
		// Crouched at a low wall: he stands up to walk with a standing leader (the clip roots him, the next repath walks him).
		if (Operative->GetStance() == EOperativeStance::Crouching && LeaderRef.GetStance() == EOperativeStance::Standing)
		{
			Operative->SetStance(EOperativeStance::Standing);
		}
		if (const float LeaveHold = Operative->GetMoveHoldSeconds(); OperativeMovementRules::ShouldDeferMove(LeaveHold))
		{
			Follower.bParked = false;
			Follower.RepathTimeRemaining = LeaveHold;
			return true;
		}
	}
	return false;
}

FVector USquadSubsystem::ResolveReachableSlot(const AOperativeCharacter& LeaderRef, const FVector& Slot, bool bLeaderSide) const
{
	UNavigationSystemV1* Nav = CVarSquadSlotFallback.GetValueOnGameThread() != 0 ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	FNavLocation OnNav;
	if (!Nav || Nav->ProjectPointToNavigation(Slot, OnNav))
	{
		FrameGivenSlots.Add(FVector2D(Slot));
		return Slot;
	}
	TArray<FVector> Usable;
	for (const FVector& Candidate : SquadFormation::ComputeFallbackSlots(LeaderRef.GetActorLocation(), Slot, FormationConfig.StopRadius * 2.f))
	{
		// Rules v2.1 (contract 7): never a walkable point on the far side of a wall from the leader.
		if (bLeaderSide && IsSlotPathBlocked(LeaderRef, FVector(Candidate.X, Candidate.Y, LeaderRef.GetActorLocation().Z)))
		{
			continue;
		}
		if (Nav->ProjectPointToNavigation(Candidate, OnNav))
		{
			Usable.Add(FVector(OnNav.Location.X, OnNav.Location.Y, Slot.Z));
		}
	}
	// Rules v2.3 (contract 13.5): the nearest stand-in that keeps slot.min_spacing from the slots given out this tick.
	const int32 Chosen = SquadFormation::ChooseFallbackSlot(Usable, FrameGivenSlots, FormationTuning::Get().SlotMinSpacing);
	const FVector Resolved = Chosen != INDEX_NONE ? Usable[Chosen] : Slot;
	static const IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Debug"));
	if (DebugVar && DebugVar->GetInt() >= 2)
	{
		FString GivenText;
		for (const FVector2D& Given : FrameGivenSlots)
		{
			GivenText += FString::Printf(TEXT(" (%.0f, %.0f)"), Given.X, Given.Y);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] fallback: slot (%.0f, %.0f) unwalkable -> (%.0f, %.0f), %.0f cm away; candidate %d of %d usable (nearest (%.0f, %.0f)); given%s"),
			Slot.X, Slot.Y, Resolved.X, Resolved.Y, FVector::Dist2D(Slot, Resolved), Chosen, Usable.Num(), Usable.Num() > 0 ? Usable[0].X : 0.0,
			Usable.Num() > 0 ? Usable[0].Y : 0.0, *GivenText);
	}
	FrameGivenSlots.Add(FVector2D(Resolved));
	return Resolved;
}

void USquadSubsystem::SeedFrameGivenSlots(const AOperativeCharacter& LeaderRef)
{
	UNavigationSystemV1* Nav = CVarSquadSlotFallback.GetValueOnGameThread() != 0 ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	if (!Nav)
	{
		return; // no fallback either: nothing to keep apart
	}
	const double Z = LeaderRef.GetActorLocation().Z;
	FNavLocation OnNav;
	for (const FFormationSlot& Slot : Sectors.FrameSlots)
	{
		if (Nav->ProjectPointToNavigation(FVector(Slot.Position.X, Slot.Position.Y, Z), OnNav))
		{
			FrameGivenSlots.Add(Slot.Position);
		}
	}
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
