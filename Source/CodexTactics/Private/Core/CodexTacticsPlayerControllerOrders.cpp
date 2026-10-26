// ACodexTacticsPlayerController: orders - real-time order lock, fire postures, autonomy, guard, group moves,
// HandleWorldHit (the click dispatcher), defence lines, Ctrl + click targeted shots, Ctrl + click quick fire at a point, cease fire, vehicle keys.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Characters/SquadAutonomySubsystem.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Characters/SquadFormation.h"
#include "Camera/InteriorExitArrowActor.h"
#include "Camera/InteriorExitSubsystem.h"
#include "Characters/RecruitSubsystem.h"
#include "Combat/CombatTimeModeRules.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DeployableActor.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/QuickFireRules.h"
#include "Combat/SuppressionSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Ladders/LadderActor.h"
#include "Components/BoxComponent.h"
#include "Misc/App.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace OrderLock
{
	// User request 2026-10-06 (RTS control): orders run at once in the real-time fight by default; 0 restores the
	// 2026-10-05 lock (orders only in the tactical pause).
	TAutoConsoleVariable<int32> CVarRealTimeOrders(TEXT("Codex.RealTimeOrders"), 1,
		TEXT("1: orders run at once in a real-time wave fight (RTS control, default, user request 2026-10-06); 0: only in the tactical pause (2026-10-05 lock)"));
}

bool ACodexTacticsPlayerController::IsRealTimeOrderLocked() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Flow->GetCombatMode(),
		OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0) == ECombatOrderDispatch::Blocked;
}

void ACodexTacticsPlayerController::ApplyFirePosture(ESquadFirePosture Posture, bool bSquadWide)
{
	const FName PostureCommand = Posture == ESquadFirePosture::Passive ? PlayerCommands::PosturePassive
		: (Posture == ESquadFirePosture::Defensive ? PlayerCommands::PostureDefensive : PlayerCommands::PostureAggressive);
	if (IsDialogueOpen() || RefuseIfLocked(PostureCommand))
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad())
	{
		Squad->ApplyPostureOrder(Posture, bSquadWide);
		ControllerCommands::Report(this, PostureCommand, nullptr, false, bSquadWide);
	}
}

bool ACodexTacticsPlayerController::BlockRealTimeOrder()
{
	if (!IsRealTimeOrderLocked())
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastOrderLockHintTime > 2.0)
	{
		LastOrderLockHintTime = Now;
		PostHeadquarters(LOCTEXT("OrdersLocked",
			"⏸ In combat, orders are given only in tactical pause [SPACE]. In real time the squad acts autonomously [Ctrl+T]."));
	}
	return true;
}

void ACodexTacticsPlayerController::ToggleAutonomy()
{
	if (RefuseIfLocked(PlayerCommands::Autonomy))
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad())
	{
		Squad->ToggleAutonomousSquadCombat();
		ControllerCommands::Report(this, PlayerCommands::Autonomy);
	}
}

void ACodexTacticsPlayerController::GuardKey()
{
	if (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl))
	{
		return; // Ctrl + T is Commander Mode
	}
	if ((!GetActiveTurnBased() && BlockRealTimeOrder()) || RefuseIfLocked(PlayerCommands::Guard))
	{
		return;
	}
	if (const UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsBusy())
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad(); Squad && Squad->GetLeader())
	{
		Squad->ToggleGuard(Squad->GetLeader());
		ControllerCommands::Report(this, PlayerCommands::Guard);
	}
}

void ACodexTacticsPlayerController::OrderGroupMove(const FVector& Destination, bool bSprint, bool bPlan)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	// The leader first, then the others in selection order (Godot sorted_group).
	TArray<AOperativeCharacter*> Group = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	Group.Remove(Leader);
	Group.Insert(Leader, 0);
	// APC P1: the ones aboard ride with the vehicle (Y gets them out first).
	Group.RemoveAll([](const AOperativeCharacter* Member) { return VehicleQueries::IsMounted(Member); });
	if (Group.IsEmpty())
	{
		return;
	}
	FVector Average = FVector::ZeroVector;
	for (const AOperativeCharacter* Member : Group)
	{
		Average += Member->GetActorLocation();
	}
	Average /= Group.Num();
	const FVector CameraForward = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
	const TArray<FVector> Targets = SquadFormation::ComputeGroupTargets(Destination, Average, CameraForward, Group.Num());

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	UInteractionSubsystem* UseOrders = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	for (int32 Index = 0; Index < Group.Num(); ++Index)
	{
		if (UseOrders)
		{
			UseOrders->CancelUseOrder(Group[Index]); // a move order replaces a planned / running "Ignite"
		}
		if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
		{
			Vehicles->CancelBoarding(Group[Index]); // ... and a walk to the APC's door
		}
		Group[Index]->CancelQuickFire(); // ... and a quick fire burst (2026-10-19)
		Group[Index]->CancelSuppressFire(TEXT("a move order")); // ... and suppressive fire (2026-10-26)
		if (bPlan)
		{
			const FVector Planned = Squad->PlanMove(Group[Index], Targets[Index], bSprint, Flow ? Flow->GetPauseOrderRadius() : 1200.f);
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->SpawnWaypointMarker(Planned);
			}
		}
		else
		{
			Group[Index]->OrderMoveTo(Targets[Index], bSprint);
			// Sprint 06-B: where each operative was sent (a fading ping; the new order replaces the old pings).
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->SpawnMovePing(Targets[Index], Index == 0);
			}
		}
	}
	if (Messages && Group.Num() > 1)
	{
		const FText Line = bPlan
			? FText::Format(bSprint ? LOCTEXT("GroupPlanSprint", "🏃 [PLAN] Group sprint planned ({0} operatives)!")
				: LOCTEXT("GroupPlanMove", "📋 [PLAN] Group move planned ({0} operatives)!"), Group.Num())
			: FText::Format(LOCTEXT("GroupMove", "🏃 Group ({0} operatives) moving into position!"), Group.Num());
		Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), Line);
	}
}

void ACodexTacticsPlayerController::HandleWorldHit(const FHitResult& InHit)
{
	// Rev 5e (user bug 2026-10-14, "the tripwire cannot be disarmed"): the wire is an 8 cm bar at shin height - a click on the ground
	// next to a spotted wire (TripwireRules::ClickSnapCm) means the wire (its Disarm menu), as hovering shows (the hand cursor).
	FHitResult Hit = InHit;
	if (!Cast<AInteractableActor>(InHit.GetActor()) && !Cast<APawn>(InHit.GetActor()))
	{
		if (ATripwireActor* Wire = ATripwireActor::FindClickableWireNear(GetWorld(), InHit.ImpactPoint))
		{
			Hit = FHitResult(Wire, Wire->Box.Get(), InHit.ImpactPoint, InHit.ImpactNormal);
			UE_LOG(LogCodexTactics, Display, TEXT("Click next to the spotted wire %s: taken as a click on it"), *Wire->GetName());
		}
	}
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	// Ladders (docs/design/LADDERS.md 7.1): a click on a climbable ladder (its click proxy) = a move to the link point at its other end
	// (the path then runs through the ladder's nav link); a decoration ladder has no proxy collision.
	if (const ALadderActor* Ladder = Cast<ALadderActor>(Hit.GetActor()); Ladder && Ladder->IsClimbable())
	{
		const bool bLeaderAbove = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight() > (Ladder->GetFloorZ() + Ladder->GetTopWorldZ()) * 0.5f;
		const FVector Target = bLeaderAbove ? Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 60.f : Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 60.f;
		Hit = FHitResult(nullptr, nullptr, Target, FVector::UpVector);
		Hit.bBlockingHit = true;
		UE_LOG(LogCodexTactics, Display, TEXT("Click on ladder %s: a move to its %s end"), *Ladder->GetName(), bLeaderAbove ? TEXT("bottom") : TEXT("top"));
	}

	// Rev 5g (user request 2026-10-14): an EXIT arrow at a door of the building whose interior camera shows - the squad (Solo: the
	// selected operatives) walks out. Never locked; the arrows are hidden in turn-based combat.
	if (AInteriorExitArrowActor* ExitArrow = Cast<AInteriorExitArrowActor>(Hit.GetActor()))
	{
		if (UInteriorExitSubsystem* Exits = GetWorld()->GetSubsystem<UInteriorExitSubsystem>(); Exits && Exits->OrderExit(ExitArrow) > 0)
		{
			ControllerCommands::Report(this, PlayerCommands::ExitBuilding, ExitArrow, false, !Squad->IsSoloMode(), nullptr, ExitArrow->GetExitTarget());
		}
		return;
	}

	// Turn-based combat: the grid handles every click (select, attack, walk). Ctrl + click = the attack order, as in
	// real time and in the tactical pause (user request 2026-10-06; Shift no longer attacks on the grid).
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		// Sprint 12: a wall click on the grid previews / confirms a cover slot snapped to a cell.
		if (!IsAttackModifierDown() && TryHandleCoverClick(Hit, TurnBased))
		{
			return;
		}
		HideCoverPreview();
		TurnBased->HandleWorldClick(Hit.ImpactPoint, Hit.GetActor(), IsAttackModifierDown());
		ControllerCommands::Report(this, IsAttackModifierDown() ? PlayerCommands::TurnBasedAttack : PlayerCommands::TurnBasedOrder, Hit.GetActor(),
			false, false, TurnBased->GetActiveUnit(), Hit.ImpactPoint);
		return;
	}

	// Real-time fight (user decision 2026-10-05): a click picks an operative (leader + camera), nothing else.
	if (IsRealTimeOrderLocked())
	{
		bRelocateSelectMode = false;
		if (AOperativeCharacter* Picked = FindClickedMember(Hit))
		{
			Squad->SetLeader(Picked);
			ControllerCommands::Report(this, PlayerCommands::SelectMember, nullptr, false, false, Picked);
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), FText::Format(LOCTEXT("UnitSelected", "👤 Operative selected: {0}"), Picked->DisplayName));
			}
		}
		else
		{
			BlockRealTimeOrder();
		}
		return;
	}

	// Game designer 2026-10-22 ("a failed defuse - clicking the mine again does nothing, I have to walk away and come back"): he works
	// ~60 cm from the charge, so the click on the mine / wire in front of him hits his own body or picks him by the select radius. A click
	// at a live trap he stands at (not picking another operative) is the trap: the disarm menu reopens at once.
	if (!Cast<AInteractableActor>(Hit.GetActor()) && !IsAttackModifierDown())
	{
		const AOperativeCharacter* Picked = FindClickedMember(Hit);
		if (UInteractionSubsystem* RetryInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>(); RetryInteractions && (!Picked || Picked == Leader))
		{
			if (AInteractableActor* Trap = RetryInteractions->FindRetryTrapAt(Hit.ImpactPoint, *Leader, Picked && Picked != Leader))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Click at %s, who stands at it: taken as a click on the trap (retry)"), *Trap->GetName());
				Hit = FHitResult(Trap, Trap->Box.Get(), Hit.ImpactPoint, Hit.ImpactNormal);
			}
		}
	}

	// Action bar "MOVE OBJ": the clicked object is picked up for relocation.
	// Bug 2026-10-25 (game designer, packaged build: "after the pause no click order was taken at all; relocating a barrel unstuck it"):
	// the pick swallowed every other click silently - the button reads like a move order. Now a click on anything that is not a movable
	// object (or a pick left over from before a pause was entered / left) drops the pick with a line and goes on as the usual order.
	if (bRelocateSelectMode)
	{
		const UGameFlowSubsystem* PickFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const bool bSamePause = !PickFlow || PickFlow->GetCombatMode() == RelocateSelectCombatMode;
		AInteractableActor* Object = Cast<AInteractableActor>(Hit.GetActor());
		bRelocateSelectMode = false;
		if (bSamePause && Object)
		{
			if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
			{
				Relocation->StartRelocate(Object, Leader);
			}
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Relocate pick dropped by a click on %s (%s): the click is a normal order"),
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("the ground"), bSamePause ? TEXT("not an object") : TEXT("the pause changed"));
		if (UGameMessageSubsystem* PickMessages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			PickMessages->PostMessage(LOCTEXT("Engineering", "ENGINEERING"), LOCTEXT("RelocatePickDropped", "Object move cancelled - orders as usual."));
		}
	}

	// APC P1: with the leader aboard a click on the ground drives his vehicle (double click: full speed); a click on an
	// operative still selects him; Ctrl + click on an enemy = the turret's priority target (P2); Shift + click waits
	// (watch sector, later).
	if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>(); Vehicles && Vehicles->IsMounted(Leader))
	{
		HideCoverPreview();
		if (AOperativeCharacter* Picked = FindClickedMember(Hit))
		{
			Squad->SetLeader(Picked);
			ControllerCommands::Report(this, PlayerCommands::SelectMember, nullptr, false, false, Picked);
			return;
		}
		if (IsAttackModifierDown())
		{
			Vehicles->HandleTurretTargetClick(Leader, Hit.GetActor());
			return;
		}
		if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)
			|| Hit.GetActor() == Vehicles->GetVehicleOf(Leader))
		{
			return;
		}
		FVector2D DriveMouse;
		GetMousePosition(DriveMouse.X, DriveMouse.Y);
		const double DriveNow = FApp::GetCurrentTime();
		const bool bDriveDoubleClick = LastClickTime >= 0.0 && DriveNow - LastClickTime <= DoubleClickSeconds
			&& FVector2D::Distance(DriveMouse, LastClickPosition) <= DoubleClickPixels;
		LastClickTime = bDriveDoubleClick ? -1.0 : DriveNow;
		LastClickPosition = DriveMouse;
		Vehicles->HandleDriveClick(Leader, Hit.ImpactPoint, bDriveDoubleClick);
		return;
	}

	// Sprint 08-F: a click on a silhouette (last known / heard position) orders blind fire at it (-80 % accuracy, a fight only); the
	// hidden enemy itself cannot be clicked (2026-10-19: exploring too - symmetric sight).
	if (UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>(); Sight && Sight->IsSquadSightActive())
	{
		if (AEnemyGhostActor* Ghost = Sight->IsActive() ? Sight->FindGhostNear(Hit.ImpactPoint, 150.f) : nullptr)
		{
			TArray<AOperativeCharacter*> Shooters = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
			for (AOperativeCharacter* Shooter : Shooters)
			{
				Shooter->SetBlindFireTarget(Ghost);
			}
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("BlindFire", "🎯 Blind fire at the silhouette (accuracy −80%)!"));
			}
			return;
		}
		if (AActor* HitActor = Hit.GetActor(); HitActor && HitActor->ActorHasTag(FName(TEXT("Enemy"))) && !Sight->IsVisibleToSquad(HitActor))
		{
			// The unseen enemy is no target; Ctrl + click there is fire at that point (it may still be hit, 2026-10-19).
			if (IsAttackModifierDown())
			{
				HideCoverPreview();
				IssueQuickFire(Hit);
			}
			return;
		}
	}

	// Ctrl + click: fire only, never a move or a selection - the targeted shot at an enemy / object, else at the point.
	if (IsAttackModifierDown())
	{
		HideCoverPreview();
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow || Flow->GetCombatMode() != ECodexCombatMode::TurnBased)
		{
			IssueCtrlClick(Hit);
		}
		return;
	}

	// Sprint 12: a click on a wall (Alt + click on a barricade / any object) = cover preview / confirm / shimmy; every
	// other click drops a pending preview and goes on as before (a ground order also leaves the cover: OrderMoveTo).
	if (TryHandleCoverClick(Hit, nullptr))
	{
		return;
	}
	// 2026-10-20: in cover, the ground within ~1 m round the same object = along its wall to that point (round its corners), in cover.
	if (TryHandleCoverWrapClick(Hit))
	{
		return;
	}
	HideCoverPreview();

	// Godot main.gd plain-click rules during a fight (before the usual walk-up):
	const UGameFlowSubsystem* ClickFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bWave = ClickFlow && ClickFlow->GetPhase() == ECodexGamePhase::WaveCombat;
	const bool bClickPause = ClickFlow && ClickFlow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const bool bPreparation = ClickFlow && ClickFlow->GetPhase() == ECodexGamePhase::Preparation;
	UGameMessageSubsystem* ClickMessages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	// 0.5: a live enemy becomes the priority target (no Ctrl needed while a wave is on).
	if (AActor* HitActor = Hit.GetActor(); bWave && HitActor && HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		const UHealthComponent* EnemyHealth = HitActor->FindComponentByClass<UHealthComponent>();
		if (!EnemyHealth || EnemyHealth->IsAlive())
		{
			Leader->AssignPriorityTarget(HitActor);
			ControllerCommands::Report(this, PlayerCommands::PriorityTarget, HitActor);
			if (ClickMessages)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
				ClickMessages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🎯 Priority target set: %s!"),
					Enemy ? *Enemy->GetEnemyDisplayName() : TEXT("Enemy"))));
			}
			return;
		}
	}
	// 0: set-up items and movable objects can't be moved mid-fight; in the pause / preparation a set-up item opens its
	// menu at once and a movable object is picked up for relocation right away.
	if (AInteractableActor* Object = Cast<AInteractableActor>(Hit.GetActor()))
	{
		const bool bDeployable = Object->IsA<ADeployableActor>();
		// Godot is_zone_solo: the leader alone in a camera zone may move / take objects mid-wave.
		const bool bZoneSolo = Leader && Leader->bInCameraZone;
		// User decision 2026-10-08: a barrel in the fight (real time or pause) opens its menu at once - "Ignite (1 match)"
		// sends the operative to light it (planned in the pause), "Push out" is offered in the pause only.
		if (Object->IsA<ABarrelActor>() && bWave)
		{
			if (UInteractionSubsystem* ClickInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
			{
				ClickInteractions->OpenMenuNow(Object);
			}
			return;
		}
		if ((bDeployable || Object->bCanBeRelocated) && bWave && !bClickPause && !bZoneSolo)
		{
			if (ClickMessages)
			{
				ClickMessages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("NoMoveInFight", "⚠️ Objects cannot be relocated during combat! Use tactical pause [SPACE]."));
			}
			if (UInteractionSubsystem* ClickInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
			{
				ClickInteractions->CancelInteraction();
			}
			return;
		}
		if (bDeployable && (bPreparation || bClickPause))
		{
			if (UInteractionSubsystem* ClickInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
			{
				ClickInteractions->OpenMenuNow(Object);
			}
			return;
		}
		if (!bDeployable && Object->bCanBeRelocated && (bClickPause || (bWave && bZoneSolo)))
		{
			if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
			{
				Relocation->StartRelocate(Object, Leader);
			}
			return;
		}
	}

	// Godot "is_unrecruited": a click on the recruit rescues / talks to him (or walks the leader up to him).
	if (URecruitSubsystem* Recruits = GetWorld()->GetSubsystem<URecruitSubsystem>(); Recruits && Recruits->IsRecruit(Hit.GetActor()))
	{
		if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
		{
			Interactions->CancelInteraction();
		}
		Recruits->HandleRecruitClicked(false);
		return;
	}

	AOperativeCharacter* SelectedMember = FindClickedMember(Hit);

	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	if (SelectedMember)
	{
		if (Interactions)
		{
			Interactions->CancelInteraction();
		}
		Squad->SetLeader(SelectedMember);
		ControllerCommands::Report(this, PlayerCommands::SelectMember, nullptr, false, false, SelectedMember);
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"),
				FText::Format(LOCTEXT("UnitSelected", "👤 Operative selected: {0}"), SelectedMember->DisplayName));
		}
		LastClickTime = -1.0;
		return;
	}

	FVector2D MousePosition = FVector2D::ZeroVector; // stays zero without a viewport (headless smokes)
	GetMousePosition(MousePosition.X, MousePosition.Y);
	const double Now = FApp::GetCurrentTime(); // real time, but follows a fixed-step (bot) run
	const bool bDoubleClick = LastClickTime >= 0.0
		&& Now - LastClickTime <= DoubleClickSeconds
		&& FVector2D::Distance(MousePosition, LastClickPosition) <= DoubleClickPixels;
	LastClickTime = bDoubleClick ? -1.0 : Now;
	LastClickPosition = MousePosition;

	// Tutorial command lock (Demo 01, 2026-10-13): no move orders at all while HQ talks (the A0 intro locks "Order.Move");
	// the refusal raises "Tutorial.Locked.Order.Move" for the one-time feed line, nothing else happens.
	if (UTutorialSubsystem::FilterCommand(this, PlayerCommands::Move).IsNone())
	{
		return;
	}
	// Clicking an object sends the leader to it (running on a double click); its action menu opens on arrival.
	if (AInteractableActor* Interactable = Cast<AInteractableActor>(Hit.GetActor()))
	{
		if (Interactions)
		{
			// Tutorial command lock (Demo 01: no running until warmed up in A1): a locked sprint walks there instead.
			Interactions->RequestInteraction(Interactable, bDoubleClick && UTutorialSubsystem::FilterCommand(this, PlayerCommands::Sprint) == PlayerCommands::Sprint);
			ControllerCommands::Report(this, PlayerCommands::Interact, Interactable);
		}
		return;
	}
	if (Interactions)
	{
		Interactions->CancelInteraction();
	}

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const ECodexCombatMode Mode = Flow ? Flow->GetCombatMode() : ECodexCombatMode::None;
	if (Mode == ECodexCombatMode::TurnBased)
	{
		// Grid combat handles clicks itself (turn-based step).
		return;
	}
	// Godot Shift + click on the ground: turn the leader and fix the observation sector.
	if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
	{
		if (RefuseIfLocked(PlayerCommands::WatchSector))
		{
			return;
		}
		Leader->SetFacingPoint(Hit.ImpactPoint);
		ControllerCommands::Report(this, PlayerCommands::WatchSector, nullptr, false, false, Leader, Hit.ImpactPoint);
		UFloatingTextSubsystem::SpawnAboveOperative(Leader, TEXT("👁️ WATCH SECTOR"), FLinearColor(0.2f, 0.9f, 1.f));
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("Sector", "👁️ [{0}]: Watch sector locked!"), Leader->DisplayName));
		}
		return;
	}
	// RTS control (user request 2026-10-06): a ground click moves the squad at once in the real-time fight and is planned
	// in the tactical pause; only the old lock (Codex.RealTimeOrders 0) keeps the Godot «moves only in the pause» rule.
	const ECombatOrderDispatch Dispatch = Flow ? FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Mode,
		OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0) : ECombatOrderDispatch::Execute;
	if (Dispatch == ECombatOrderDispatch::Blocked)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("MoveOnlyInPause", "During combat, moving is only possible in tactical pause [SPACE]!"));
		}
		return;
	}
	// Rev 5i (game designer 2026-10-15): a tutorial confinement holds the squad in an area - a click outside it is refused with one
	// feed line and reported as no order (the members' own orders are judged again in OrderMoveTo).
	if (!UTutorialSubsystem::CheckConfinedOrder(this, Leader, Hit.ImpactPoint))
	{
		return;
	}
	// Tutorial command lock (Demo 01 user decision 2026-10-12: no running until warmed up in A1): a locked sprint is
	// degraded to a walk without any error line; the lock raises "Tutorial.Locked" for the one-time hint.
	const bool bSprint = bDoubleClick && UTutorialSubsystem::FilterCommand(this, PlayerCommands::Sprint) == PlayerCommands::Sprint;
	// Godot double click: sprint, or why not.
	if (bSprint)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			if (!Leader->CanSprint() && Leader->ColdLevel >= Leader->MovementConfig.MaxColdToSprint)
			{
				Messages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🥶 %s is frozen (%d%% cold) and cannot run! Walking."),
					*Leader->DisplayName.ToString(), FMath::FloorToInt(Leader->ColdLevel))));
			}
			else if (!Leader->CanSprint() && Leader->IsWounded() && Leader->HealthComponent)
			{
				Messages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🩹 %s is badly wounded (%d/%d HP) and cannot run! Walking."),
					*Leader->DisplayName.ToString(), FMath::FloorToInt(Leader->HealthComponent->GetCurrentHealth()), FMath::FloorToInt(Leader->HealthComponent->GetMaxHealth()))));
			}
			else if (Leader->CanSprint())
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("SprintOrder", "🏃 Run to position!"));
			}
		}
	}
	OrderGroupMove(Hit.ImpactPoint, bSprint, Dispatch == ECombatOrderDispatch::Queue);
	ControllerCommands::Report(this, bSprint ? PlayerCommands::Sprint : PlayerCommands::Move, nullptr, Dispatch == ECombatOrderDispatch::Queue,
		Squad->HasMultiSelection(), Leader, Hit.ImpactPoint);
}

void ACodexTacticsPlayerController::AssignDefenseUnderCursor()
{
	USquadSubsystem* Squad = GetSquad();
	USquadAutonomySubsystem* Autonomy = GetWorld()->GetSubsystem<USquadAutonomySubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || !Autonomy || GetActiveTurnBased() || BlockRealTimeOrder() || RefuseIfLocked(PlayerCommands::HoldLine))
	{
		return;
	}
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		return;
	}
	AInteractableActor* Object = Cast<AInteractableActor>(Hit.GetActor());
	TArray<AOperativeCharacter*> Defenders = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	for (AOperativeCharacter* Defender : Defenders)
	{
		Autonomy->SetDefenseObjective(Defender, Object, Hit.ImpactPoint);
	}
	ControllerCommands::Report(this, PlayerCommands::HoldLine, Object, false, Defenders.Num() > 1, Leader, Hit.ImpactPoint);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("DefenseLine", "🛡 [HOLD LINE] {0}: hold at all costs!{1}"),
			Object ? Object->DisplayName : LOCTEXT("DefensePoint", "Point"),
			Squad->IsAutonomousSquadCombat() ? FText::GetEmpty() : LOCTEXT("DefenseNeedsAuto", " (turn on autonomy [Ctrl+T] - the operatives will hold the line themselves)")));
	}
}

void ACodexTacticsPlayerController::IssueTargetedShot(AActor* HitActor)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Messages || RefuseIfLocked(PlayerCommands::TargetedShot))
	{
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bPaused = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const ETargetedShotKind Kind = AOperativeCharacter::ClassifyShotTarget(HitActor);
	// 2026-10-19 (symmetric sight): exploring, an enemy no operative sees is no target - the order would start the fight at an
	// enemy the squad cannot see (in a fight the priority is kept until it shows up, as before).
	if (const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>(); Sight && Kind == ETargetedShotKind::Enemy
		&& Sight->IsSquadSightActive() && !Sight->IsActive() && !Sight->IsVisibleToSquad(HitActor))
	{
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("TargetUnseen", "Can't see it from here."));
		return;
	}
	UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>();
	if (Kind != ETargetedShotKind::None && Feedback)
	{
		// Godot _highlight_target_feedback (not on destroyed crates) + a plan marker in the pause.
		const ALootCrateActor* Crate = Cast<ALootCrateActor>(HitActor);
		if (!Crate || !Crate->IsDestroyed())
		{
			Feedback->HighlightTarget(HitActor);
		}
		if (bPaused)
		{
			FVector Origin;
			FVector Extent;
			HitActor->GetActorBounds(true, Origin, Extent);
			Feedback->SpawnWaypointMarker(FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z));
		}
	}
	if (bPaused && Kind != ETargetedShotKind::None)
	{
		Leader->PlanTargetedShot(HitActor);
	}
	if (Kind != ETargetedShotKind::None)
	{
		ControllerCommands::Report(this, PlayerCommands::TargetedShot, HitActor, bPaused);
	}

	switch (Kind)
	{
	case ETargetedShotKind::Enemy:
	{
		// Ambush level (user request 2026-10-06): attacking an enemy opens the fight — no "Start combat" needed. The
		// priority target below then makes the squad fire at once (the real-time fight is on).
		if (!bPaused)
		{
			ULevelEncounterSubsystem::NotifyHostileContactIn(GetWorld(), EAmbushTrigger::AttackOrder, HitActor);
		}
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
		const FText EnemyName = Enemy ? FText::FromString(Enemy->GetEnemyDisplayName()) : LOCTEXT("Enemy", "Enemy");
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("PlanEnemy", "📋 [PLAN] Aimed fire assigned on: {0}!"), EnemyName));
		}
		else
		{
			Leader->SetManualPriorityTarget(HitActor);
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("PriorityEnemy", "🎯 Priority target set: {0}!"), EnemyName));
		}
		break;
	}
	case ETargetedShotKind::Barrel:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanBarrel", "📋 [PLAN] Shot at the fuel barrel planned! [SPACE - fire]"));
		}
		else if (Leader->ShootAtObject(HitActor))
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("ShotBarrel", "💥 Aimed shot at the fuel barrel!"));
		}
		break;
	case ETargetedShotKind::Mine:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanMine", "📋 [PLAN] Aimed shot at the mine planned! [SPACE - fire]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	case ETargetedShotKind::Crate:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanCrate", "📋 [PLAN] Shot at the supply crate planned! [SPACE - fire]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	case ETargetedShotKind::TrappedObject:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanTrapped", "📋 [PLAN] Remote detonation of the tripwire planned! [SPACE - fire]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	default:
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("TargetHint", "Pick an enemy, barrel, mine or crate for aimed fire [Ctrl+Click]!"));
		break;
	}
}

void ACodexTacticsPlayerController::IssueCtrlClick(const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	// A shootable actor under the cursor (a live enemy the squad sees, a barrel, a mine, a crate, a trapped object): the
	// targeted shot, unchanged. A squad mate: its hint (no fire at the squad).
	if (AOperativeCharacter::ClassifyShotTarget(HitActor) != ETargetedShotKind::None || Cast<AOperativeCharacter>(HitActor))
	{
		IssueTargetedShot(HitActor);
		return;
	}
	// A visible live enemy within ~1.5 m of the point: the targeted shot at it (a click on the snow at its feet).
	UWorld* World = GetWorld();
	const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	TArray<AActor*> EnemyActors;
	TArray<FQuickFireCandidate> Candidates;
	for (AActor* Each : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		if (!IsValid(Each))
		{
			continue;
		}
		const UHealthComponent* Health = Each->FindComponentByClass<UHealthComponent>();
		FQuickFireCandidate Candidate;
		Candidate.Location = Each->GetActorLocation();
		Candidate.bAlive = !Health || Health->IsAlive();
		Candidate.bVisible = !Each->IsHidden() && (!Sight || Sight->IsVisibleToSquad(Each));
		Candidates.Add(Candidate);
		EnemyActors.Add(Each);
	}
	const int32 Picked = QuickFireRules::PickTarget(Hit.ImpactPoint, Candidates, QuickFireRules::GetConfig().EnemyPickRadiusCm);
	if (Picked != INDEX_NONE)
	{
		IssueTargetedShot(EnemyActors[Picked]);
		return;
	}
	// Nothing to aim at: the selection fires at the point.
	IssueQuickFire(Hit);
}

void ACodexTacticsPlayerController::IssueQuickFire(const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	if (!World || !Leader || IsDialogueOpen() || RefuseIfLocked(PlayerCommands::QuickFire))
	{
		return;
	}
	const FQuickFireConfig& Config = QuickFireRules::GetConfig();
	const FVector Point = Hit.ImpactPoint;

	// A visible live enemy near the point (squad sight: what nobody sees is never picked - a round at the point may still hit
	// it by the trace). IssueCtrlClick sends such a click to the targeted shot; a direct call fires the burst at it.
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	TArray<AActor*> EnemyActors;
	TArray<FQuickFireCandidate> Candidates;
	for (AActor* Each : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		if (!IsValid(Each))
		{
			continue;
		}
		const UHealthComponent* Health = Each->FindComponentByClass<UHealthComponent>();
		FQuickFireCandidate Candidate;
		Candidate.Location = Each->GetActorLocation();
		Candidate.bAlive = !Health || Health->IsAlive();
		Candidate.bVisible = !Each->IsHidden() && (!Sight || Sight->IsVisibleToSquad(Each));
		Candidates.Add(Candidate);
		EnemyActors.Add(Each);
	}
	const int32 Picked = QuickFireRules::PickTarget(Point, Candidates, Config.EnemyPickRadiusCm);
	AActor* Enemy = Picked != INDEX_NONE ? EnemyActors[Picked] : nullptr;

	// Turn-based: the active operative's grid attack (its AP rules) on the enemy / object there; no fire at an empty spot.
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		if (TurnBased->IsBusy())
		{
			return;
		}
		AActor* GridTarget = Enemy ? Enemy
			: (AOperativeCharacter::ClassifyShotTarget(Hit.GetActor()) != ETargetedShotKind::None ? Hit.GetActor() : nullptr);
		if (!GridTarget)
		{
			if (Messages)
			{
				Messages->PostMessage(LOCTEXT("TacticsSpeaker", "TACTICS"), LOCTEXT("QuickFireTurnBased",
					"Turn-based: Ctrl + click an enemy, a barrel or a trapped object to attack it (its AP). Fire at an empty spot is real time only."));
			}
			return;
		}
		HideCoverPreview();
		AOperativeCharacter* Active = TurnBased->GetActiveUnit();
		TurnBased->HandleWorldClick(GridTarget == Hit.GetActor() ? Point : GridTarget->GetActorLocation(), GridTarget, /*bAttackOrder*/ true);
		ControllerCommands::Report(this, PlayerCommands::QuickFire, GridTarget, false, false, Active, Point);
		return;
	}
	if (BlockRealTimeOrder())
	{
		return; // only with the optional real-time order lock (Codex.RealTimeOrders 0)
	}

	// Who: the box-selected group, else the operative you control (Solo too); nobody aboard a vehicle.
	TArray<AOperativeCharacter*> Shooters = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	Shooters.RemoveAll([](const AOperativeCharacter* Member) { return !Member || VehicleQueries::IsMounted(Member); });
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bPlan = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	int32 Accepted = 0;
	for (AOperativeCharacter* Shooter : Shooters)
	{
		Accepted += (bPlan ? Shooter->PlanQuickFire(Point, Enemy) : Shooter->OrderQuickFire(Point, Enemy)) ? 1 : 0;
	}
	if (Accepted == 0)
	{
		return; // each refusal shows over the operative (enraged, panicking, knife, frozen weapon)
	}
	HideCoverPreview();
	// Firing at a visible enemy is an attack order (an ambush level / a placed enemy: the fight starts, as Ctrl + click); the
	// gunshot itself is heard by the patrols in any case.
	if (Enemy && !bPlan)
	{
		ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::AttackOrder, Enemy);
	}
	if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		const FVector Marker = Enemy ? Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()) : Point;
		if (bPlan)
		{
			Feedback->SpawnWaypointMarker(Marker);
		}
		else
		{
			Feedback->SpawnFirePing(Marker);
		}
		if (Enemy)
		{
			Feedback->HighlightTarget(Enemy);
		}
	}
	if (Messages)
	{
		const FText Speaker = Accepted > 1 ? LOCTEXT("SquadSpeaker", "SQUAD") : Leader->DisplayName;
		Messages->PostMessage(Speaker, bPlan
			? FText::Format(LOCTEXT("QuickFirePlanned", "📋 [PLAN] Fire at the point planned ({0})! [SPACE - fire]"), Accepted)
			: LOCTEXT("QuickFire", "Fire!"));
	}
	ControllerCommands::Report(this, PlayerCommands::QuickFire, Enemy, bPlan, Accepted > 1, Leader, Point);
}

bool ACodexTacticsPlayerController::IsSelectionSuppressing() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return false;
	}
	const TArray<AOperativeCharacter*> Shooters = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	return Shooters.ContainsByPredicate([](const AOperativeCharacter* Member) { return Member && (Member->HasSuppressOrder() || Member->HasPlannedSuppress()); });
}

void ACodexTacticsPlayerController::SuppressKey()
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	if (!World || !Leader || IsDialogueOpen() || RefuseIfLocked(PlayerCommands::Suppress))
	{
		return;
	}
	if (bSuppressAimArmed)
	{
		bSuppressAimArmed = false; // the key again: no order
		return;
	}
	// Decision 5: no suppression in turn-based combat; the A/B switch off: no order.
	if (GetActiveTurnBased() || !USuppressionSubsystem::IsEnabled())
	{
		if (Messages)
		{
			Messages->PostMessage(LOCTEXT("TacticsSpeaker", "TACTICS"), GetActiveTurnBased()
				? LOCTEXT("SuppressTurnBased", "Suppressive fire is real time only.")
				: LOCTEXT("SuppressOff", "Suppressive fire is switched off."));
		}
		return;
	}
	if (BlockRealTimeOrder())
	{
		return;
	}
	// The selection is suppressing: the key ceases it (planned ones too).
	if (IsSelectionSuppressing())
	{
		const TArray<AOperativeCharacter*> Shooters = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
		for (AOperativeCharacter* Shooter : Shooters)
		{
			Shooter->CancelSuppressFire(TEXT("ceased by the player"));
			Shooter->ClearPlannedSuppress();
		}
		if (Messages)
		{
			Messages->PostMessage(Shooters.Num() > 1 ? LOCTEXT("SquadSpeaker", "SQUAD") : Leader->DisplayName, LOCTEXT("SuppressCeased", "Cease suppressive fire."));
		}
		ControllerCommands::Report(this, PlayerCommands::Suppress, nullptr, false, Shooters.Num() > 1, Leader);
		return;
	}
	HideCoverPreview();
	bSuppressAimArmed = true;
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("TacticsSpeaker", "TACTICS"), LOCTEXT("SuppressAim",
			"SUPPRESS: click an enemy or a spot (RMB / Esc: cancel). Full-auto bursts - lots of ammo."));
	}
}

void ACodexTacticsPlayerController::IssueSuppress(const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	bSuppressAimArmed = false;
	if (!World || !Leader || IsDialogueOpen() || RefuseIfLocked(PlayerCommands::Suppress))
	{
		return;
	}
	if (GetActiveTurnBased() || !USuppressionSubsystem::IsEnabled())
	{
		if (Messages)
		{
			Messages->PostMessage(LOCTEXT("TacticsSpeaker", "TACTICS"), GetActiveTurnBased()
				? LOCTEXT("SuppressTurnBased", "Suppressive fire is real time only.")
				: LOCTEXT("SuppressOff", "Suppressive fire is switched off."));
		}
		return;
	}
	if (BlockRealTimeOrder())
	{
		return;
	}
	const FVector Point = Hit.ImpactPoint;
	// The enemy: a live one under the cursor (seen or not - unseen, his spot is the point), else a visible live one near the point.
	AActor* Enemy = nullptr;
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	if (AActor* HitActor = Hit.GetActor(); HitActor && HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		const UHealthComponent* Health = HitActor->FindComponentByClass<UHealthComponent>();
		if ((!Health || Health->IsAlive()) && (!Sight || Sight->IsVisibleToSquad(HitActor)))
		{
			Enemy = HitActor;
		}
	}
	if (!Enemy)
	{
		TArray<AActor*> EnemyActors;
		TArray<FQuickFireCandidate> Candidates;
		for (AActor* Each : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
		{
			if (!IsValid(Each))
			{
				continue;
			}
			const UHealthComponent* Health = Each->FindComponentByClass<UHealthComponent>();
			FQuickFireCandidate Candidate;
			Candidate.Location = Each->GetActorLocation();
			Candidate.bAlive = !Health || Health->IsAlive();
			Candidate.bVisible = !Each->IsHidden() && (!Sight || Sight->IsVisibleToSquad(Each));
			Candidates.Add(Candidate);
			EnemyActors.Add(Each);
		}
		const int32 Picked = QuickFireRules::PickTarget(Point, Candidates, QuickFireRules::GetConfig().EnemyPickRadiusCm);
		Enemy = Picked != INDEX_NONE ? EnemyActors[Picked] : nullptr;
	}

	// Who: the box-selected group, else the operative you control; nobody aboard a vehicle.
	TArray<AOperativeCharacter*> Shooters = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	Shooters.RemoveAll([](const AOperativeCharacter* Member) { return !Member || VehicleQueries::IsMounted(Member); });
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bPlan = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	int32 Accepted = 0;
	for (AOperativeCharacter* Shooter : Shooters)
	{
		Accepted += (bPlan ? Shooter->PlanSuppress(Enemy, Point) : Shooter->OrderSuppress(Enemy, Point)) ? 1 : 0;
	}
	if (Accepted == 0)
	{
		return; // each refusal shows over the operative (enraged, panicking, knife, frozen weapon, low ammo)
	}
	HideCoverPreview();
	// Firing at a visible enemy is an attack order (an ambush level / a placed enemy: the fight starts, as Ctrl + click).
	if (Enemy && !bPlan)
	{
		ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::AttackOrder, Enemy);
	}
	if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		const FVector Marker = Enemy ? Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()) : Point;
		if (bPlan)
		{
			Feedback->SpawnWaypointMarker(Marker);
		}
		else
		{
			Feedback->SpawnFirePing(Marker);
		}
		if (Enemy)
		{
			Feedback->HighlightTarget(Enemy);
		}
	}
	if (Messages)
	{
		const FText Speaker = Accepted > 1 ? LOCTEXT("SquadSpeaker", "SQUAD") : Leader->DisplayName;
		Messages->PostMessage(Speaker, bPlan
			? FText::Format(LOCTEXT("SuppressPlanned", "[PLAN] Suppressive fire planned ({0})! [SPACE - fire]"), Accepted)
			: LOCTEXT("SuppressGo", "Suppressive fire! Keep their heads down!"));
	}
	ControllerCommands::Report(this, PlayerCommands::Suppress, Enemy, bPlan, Accepted > 1, Leader, Point);
}

void ACodexTacticsPlayerController::SetSquadCeaseFire(bool bCease)
{
	if (bCeaseFireSet == bCease)
	{
		return;
	}
	bCeaseFireSet = bCease;
	if (USquadSubsystem* Squad = GetSquad())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->bTacticalCeaseFire = bCease;
		}
	}
}

void ACodexTacticsPlayerController::VehicleKey(bool bSquadWide)
{
	if (RefuseIfLocked(PlayerCommands::VehicleBoard))
	{
		return;
	}
	if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
	{
		// The hovered vehicle wins when several are near (design 1.1).
		FHitResult Hover;
		AVehiclePawn* Hovered = GetHitResultUnderCursor(ECC_Visibility, false, Hover) ? Cast<AVehiclePawn>(Hover.GetActor()) : nullptr;
		HideCoverPreview();
		Vehicles->HandleBoardKey(bSquadWide, Hovered);
	}
}

void ACodexTacticsPlayerController::VehicleKeyPressed()
{
	if (IsDialogueOpen())
	{
		return;
	}
	VehicleKey(IsSquadWideModifierDown());
}

#undef LOCTEXT_NAMESPACE
