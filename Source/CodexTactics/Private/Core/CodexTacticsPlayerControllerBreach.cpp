// ACodexTacticsPlayerController: R17-5, the doorway breach in the game (designer 2026-11-17, together with R17-3; docs/design/DOORWAY_BREACH.md with the
// designer's answers 2026-11-11; Codex scout docs/port/codex/outbox/breach-hookup-scout.report.md). The R17-3 click ghost's first click on a doorway's
// opening shows the breach stack (DoorwayBreachRules slots: the two jamb men against the wall leaning in cross, the centre man 2.2 m back crouched,
// rear security), the second click orders it: the jamb men run into their jamb's corner and peek in (the run-and-peek: OrderRunCornerPeek on the
// real wall at the jamb), the centre / rear men walk to their spots and hold there facing their sectors. Once stacked, a click inside the room
// is the entry (DoorwayBreachEntryRules: right-hand jamb man hooks right, the left one 0.3 s later left, the centre man drives in). Solo: one man
// at the nearest jamb; a box selection: the selected ones; formation: the squad. Godot reference: none (UE-only).

#include "Core/CodexTacticsPlayerController.h"
#include "Telemetry/SessionRecordingSubsystem.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Interactables/DoorwaySubsystem.h"
#include "Interactables/InteractionSubsystem.h"
#include "NavigationSystem.h"
#include "Tactics/ClickPreviewRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/DoorwayBreachEntryRules.h"
#include "Tactics/DoorwayBreachGhostActor.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace ControllerBreachLocal
{
	/** The opening's height (the level generator's standard door: the registry has no top), cm. */
	constexpr float OpeningHeightCm = 210.f;
	/** A doorway on another floor than the leader's is not his, cm (its threshold this far from his feet). */
	constexpr float MaxFloorGapCm = 250.f;
	/** The jamb man's wall is looked for at this height over the slot's feet, cm. */
	constexpr float JambChestCm = 110.f;
	/** He looks this far along his sector (the look hold point), cm. */
	constexpr float LookDistanceCm = 600.f;
	/** The stack waits this long for its last man, then the entry is offered with whoever is there, s. */
	constexpr float StackTimeoutSeconds = 30.f;
	/** The entry gives up waiting for its men after this long, s. */
	constexpr float EntryTimeoutSeconds = 20.f;

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	/** The room behind the doorway (review 2026-11-17: the entry click must lie in it): chest-high traces from just inside the threshold. */
	FDoorwayBreachRoom MeasureRoom(UWorld* World, const FVector& Center, const FVector& Outward)
	{
		using namespace DoorwayBreachEntryRules;
		const FVector In = EntryDirection(Outward);
		const FVector Right = EntryRight(Outward);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BreachRoom), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		auto Reach = [World, &Params](const FVector& From, const FVector& Direction, float MaxCm)
		{
			FHitResult Hit;
			return World->LineTraceSingleByChannel(Hit, From, From + Direction * MaxCm, ECC_Visibility, Params) && !Hit.bStartPenetrating
				? static_cast<float>(Hit.Distance) : -1.f;
		};
		const FVector Chest = Center + FVector(0.f, 0.f, JambChestCm);
		const float Forward = Reach(Chest + In * 60.f, In, RoomMaxDepthCm - 60.f);
		const FVector Side = Chest + In * 100.f;
		return MakeRoom(Forward < 0.f ? -1.f : Forward + 60.f, Reach(Side, Right, RoomMaxSideCm), Reach(Side, -Right, RoomMaxSideCm), static_cast<float>(Center.Z));
	}

	FVector OnNav(UWorld* World, const FVector& Point)
	{
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation Location;
			if (Nav->ProjectPointToNavigation(Point, Location, FVector(80.f, 80.f, 150.f)))
			{
				return Location.Location;
			}
		}
		return Point;
	}
}

bool ACodexTacticsPlayerController::FindBreachForHit(const FHitResult& Hit, FBreachPlan& OutPlan, const FBreachPlan* Cached) const
{
	using namespace ControllerBreachLocal;
	OutPlan = FBreachPlan();
	UWorld* World = GetWorld();
	const UDoorwaySubsystem* Doors = World ? World->GetSubsystem<UDoorwaySubsystem>() : nullptr;
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Doors || Doors->GetDoorwayCount() == 0 || !Leader || !Hit.bBlockingHit)
	{
		return false;
	}
	const FDoorwayBreachConfig& Config = DoorwayBreachRules::GetConfig();
	const FVector Eye = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : FVector(Hit.TraceStart);
	const FVector LeaderFeet = Feet(*Leader);
	const FRuntimeDoorway* Best = nullptr;
	FVector BestOutward = FVector::ZeroVector;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FRuntimeDoorway& Door : Doors->GetAllDoorways())
	{
		if (FMath::Abs(Door.Center.Z - LeaderFeet.Z) > MaxFloorGapCm || Door.WidthCm <= 0.f || Door.WidthCm > Config.MaxGateWidthCm)
		{
			continue;
		}
		FVector Outward = Door.OutwardNormal.GetSafeNormal2D();
		if (Outward.IsNearlyZero())
		{
			Outward = FVector(-Door.WallTangent.Y, Door.WallTangent.X, 0.f).GetSafeNormal();
		}
		if (Door.bIsGate && FVector::DotProduct(Outward, LeaderFeet - Door.Center) < 0.0)
		{
			Outward = -Outward; // a gate: its approach side is his
		}
		// Review 2026-11-17: only the opening itself (a jamb face, the threshold - the hit at the portal plane) arms the breach; a ray through the
		// opening onto the room's floor / a wall inside is a plain move through the doorway.
		FVector Point;
		if (!ClickPreviewRules::IsAtPortalPlane(Hit.ImpactPoint, Door.Center, Outward, Config.PortalDepthSlackCm)
			|| !ClickPreviewRules::RayThroughPortal(Eye, Hit.ImpactPoint, Door.Center, Door.WallTangent, Outward, Door.WidthCm, OpeningHeightCm, Config.PortalSlackCm,
			Config.PortalDepthSlackCm, Point))
		{
			continue;
		}
		const double Distance = FVector::Dist(Point, Hit.ImpactPoint);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = &Door;
			BestOutward = Outward;
		}
	}
	if (!Best)
	{
		return false;
	}
	// Who goes: a box selection - the selected ones; Solo - the selected operative alone; formation - the squad (its free followers).
	TArray<AOperativeCharacter*> Members;
	if (Squad->HasMultiSelection())
	{
		Members = Squad->GetSelectedGroup();
		Members.Remove(Leader);
		Members.Insert(Leader, 0);
	}
	else
	{
		Members.Add(Leader);
		if (!Squad->IsSoloMode())
		{
			Members.Append(Squad->GetGroupCoverMembers(Leader));
		}
	}
	Members.RemoveAll([](const AOperativeCharacter* Each) { return !Each || Each->IsKnockedDown(); });
	if (Members.IsEmpty())
	{
		return false;
	}
	// The same doorway for the same men as the shown plan: it stands (the triad and the slot assignment are not re-planned per cursor step).
	if (Cached && Cached->DoorwayId == Best->Id && Cached->Members == Members)
	{
		OutPlan = *Cached;
		return true;
	}
	FRuntimeDoorway Door = *Best;
	Door.OutwardNormal = BestOutward;
	FDoorwayBreachTriad Triad;
	if (!Doors->GetBreachTriadForDoorway(Door, Triad, &LeaderFeet))
	{
		return false;
	}
	TArray<FVector> Positions;
	for (const AOperativeCharacter* Member : Members)
	{
		Positions.Add(Member->GetActorLocation());
	}
	OutPlan.Assignments = DoorwayBreachRules::AssignSlots(Positions, Triad, Members.Num() == 1, 0);
	if (OutPlan.Assignments.IsEmpty())
	{
		return false;
	}
	OutPlan.DoorwayId = Door.Id;
	OutPlan.Center = Door.Center;
	OutPlan.Outward = BestOutward;
	OutPlan.Tangent = Door.WallTangent.GetSafeNormal2D();
	OutPlan.WidthCm = Door.WidthCm;
	OutPlan.Triad = Triad;
	OutPlan.Members = Members;
	return true;
}

void ACodexTacticsPlayerController::ShowBreachGhost(const FBreachPlan& Plan)
{
	if (!BreachGhost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BreachGhost = GetWorld()->SpawnActor<ADoorwayBreachGhostActor>(Plan.Center, FRotator::ZeroRotator, Params);
	}
	if (BreachGhost)
	{
		BreachGhost->ShowBreachPreviewForAssignments(Plan.Triad, Plan.Assignments, Plan.Members, true);
	}
}

void ACodexTacticsPlayerController::HideBreachGhost()
{
	if (BreachGhost && BreachGhost->IsPreviewShown())
	{
		BreachGhost->HideBreachPreview();
	}
}

int32 ACodexTacticsPlayerController::GetBreachGhostCount() const
{
	return BreachGhost && BreachGhost->IsPreviewShown() ? BreachGhost->GetVisibleGhostCount() : 0;
}

int32 ACodexTacticsPlayerController::GetBreachArrivedCount() const
{
	int32 Count = 0;
	for (const FBreachMember& Member : Breach.Members)
	{
		Count += Member.bArrived ? 1 : 0;
	}
	return Count;
}

int32 ACodexTacticsPlayerController::GetBreachEnteredCount() const
{
	int32 Count = 0;
	for (const FBreachMember& Member : Breach.Members)
	{
		Count += Member.bEntered ? 1 : 0;
	}
	return Count;
}

bool ACodexTacticsPlayerController::OrderBreach(const FBreachPlan& Plan, bool bSprint)
{
	FDemoOrderScope Demo(this, EDemoOrderKind::Breach, Plan.Members.IsEmpty() ? nullptr : Plan.Members[0], Plan.Center, bSprint);
	using namespace ControllerBreachLocal;
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Plan.Members.IsEmpty() ? nullptr : Plan.Members[0];
	if (!World || !Squad || !Leader || Plan.Assignments.IsEmpty())
	{
		return Demo.SetResult(false);
	}
	EndBreach(TEXT("a new breach order"));
	Squad->HandOverLeadForOrder(Leader, TEXT("a breach order"));
	// Formation: the others hold where their own breach order leaves them (not pulled back into the formation).
	const bool bFormation = !Squad->IsSoloMode() && !Squad->HasMultiSelection() && Plan.Members.Num() > 1;
	if (bFormation)
	{
		Breach.bHoldBefore = Squad->AreFollowersHolding();
		Breach.bHoldSet = true;
		Squad->SetFollowersHolding(true);
	}
	UInteractionSubsystem* UseOrders = World->GetSubsystem<UInteractionSubsystem>();
	UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
	UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
	FString Line;
	for (const FDoorwaySlotAssignment& Assignment : Plan.Assignments)
	{
		AOperativeCharacter* Member = Plan.Members.IsValidIndex(Assignment.OperativeIndex) ? Plan.Members[Assignment.OperativeIndex] : nullptr;
		if (!Member)
		{
			continue;
		}
		if (UseOrders)
		{
			UseOrders->CancelUseOrder(Member);
		}
		if (Vehicles)
		{
			Vehicles->CancelBoarding(Member);
		}
		Member->CancelQuickFire();
		Member->CancelSuppressFire(TEXT("a breach order"));
		Member->ClearManualLookPoint();
		FBreachMember Entry;
		Entry.Operative = Member;
		Entry.Slot = Assignment.Slot;
		EOperativeOrderResult Result = EOperativeOrderResult::Refused;
		const bool bJamb = Assignment.Slot.bRequiresCover
			&& (Assignment.Slot.Role == EDoorwayBreachSlotRole::LeftJamb || Assignment.Slot.Role == EDoorwayBreachSlotRole::RightJamb);
		if (bJamb)
		{
			// The jamb's real wall (the slot is synthetic geometry): its cover slot at the stand point, the opening's edge on its face line.
			FCoverSlot Jamb;
			const FVector Probe = Assignment.Slot.Location + FVector(0.f, 0.f, JambChestCm);
			if (CoverTraceRules::FindCoverSlotAt(World, Probe, -Plan.Outward, Jamb) && Jamb.IsValid())
			{
				const double Side = FVector::DotProduct(Assignment.Slot.Location - Plan.Center, Plan.Tangent) < 0.0 ? -1.0 : 1.0;
				const FVector EdgeWorld = Plan.Center + Plan.Tangent * (Side * Plan.WidthCm * 0.5);
				const FVector Right = Jamb.RightTangent();
				const FVector EdgeOnFace = Jamb.WallPoint + Right * FVector::DotProduct(EdgeWorld - Jamb.WallPoint, Right);
				Result = Member->OrderRunCornerPeek(Jamb, EdgeOnFace, bSprint);
				Entry.Target = Jamb.WorldLocation;
			}
		}
		if (Result != EOperativeOrderResult::Accepted)
		{
			Entry.Target = OnNav(World, Assignment.Slot.Location);
			Result = Member->OrderMoveTo(Entry.Target, bSprint);
		}
		if (Result != EOperativeOrderResult::Accepted)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Breach] %s: no way to his slot (%d)"), *Member->DisplayName.ToString(), static_cast<int32>(Result));
			continue;
		}
		if (Feedback)
		{
			Feedback->SpawnMovePing(Entry.Target, Member == Leader);
		}
		Line += FString::Printf(TEXT(" %s -> %s%s;"), *Member->DisplayName.ToString(), *UEnum::GetValueAsString(Assignment.Slot.Role), bJamb ? TEXT(" (jamb peek)") : TEXT(""));
		Breach.Members.Add(Entry);
	}
	if (Breach.Members.IsEmpty())
	{
		EndBreach(TEXT("nobody can reach the doorway"));
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("BreachUnreachable", "⚠️ No way to that doorway."));
		}
		return Demo.SetResult(false);
	}
	Breach.Phase = EBreachPhase::Stacking;
	Breach.DoorwayId = Plan.DoorwayId;
	Breach.Center = Plan.Center;
	Breach.Outward = Plan.Outward;
	Breach.WidthCm = Plan.WidthCm;
	Breach.Room = MeasureRoom(World, Plan.Center, Plan.Outward);
	Breach.Leader = Leader;
	Breach.bSolo = Squad->IsSoloMode();
	Breach.Seconds = 0.f;
	Breach.bSprint = bSprint;
	UE_LOG(LogCodexTactics, Display, TEXT("[Breach] doorway %s: the stack goes to its slots (%s):%s"), *Plan.DoorwayId, bSprint ? TEXT("at a run") : TEXT("walking"), *Line);
	ControllerCommands::Report(this, PlayerCommands::Move, nullptr, false, Breach.Members.Num() > 1, Leader, Plan.Center);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Leader->DisplayName, Breach.Members.Num() > 1 ? LOCTEXT("BreachStack", "🚪 Stack on the door!") : LOCTEXT("BreachSolo", "🚪 Covering the doorway."));
	}
	return Demo.SetResult(true);
}

FHitResult ACodexTacticsPlayerController::BreachEntryHit(const FHitResult& Hit) const
{
	// The stacked jamb men lean into the frame: a click into the doorway often lands on one of their bodies. That click means the room behind
	// him (the same ray past the stack's bodies), not "select him".
	const AOperativeCharacter* Body = Cast<AOperativeCharacter>(Hit.GetActor());
	const bool bStackBody = Body && Breach.Members.ContainsByPredicate([Body](const FBreachMember& Member) { return Member.Operative.Get() == Body; });
	UWorld* World = GetWorld();
	if (!bStackBody || !World)
	{
		return Hit;
	}
	const FVector Eye = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : FVector(Hit.TraceStart);
	const FVector Direction = (FVector(Hit.ImpactPoint) - Eye).GetSafeNormal();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BreachEntryHit), false);
	for (const FBreachMember& Member : Breach.Members)
	{
		if (AOperativeCharacter* Operative = Member.Operative.Get())
		{
			Params.AddIgnoredActor(Operative);
			TArray<AActor*> Attached; // his weapon / gear actors
			Operative->GetAttachedActors(Attached, true, true);
			Params.AddIgnoredActors(Attached);
		}
	}
	FHitResult Past;
	if (Direction.IsNearlyZero() || !World->LineTraceSingleByChannel(Past, Eye, Eye + Direction * 100000.0, ECC_Visibility, Params) || !Past.bBlockingHit)
	{
		return Hit;
	}
	return Past;
}

bool ACodexTacticsPlayerController::TryBreachEntryClick(const FHitResult& Hit)
{
	using namespace ControllerBreachLocal;
	if (Breach.Phase != EBreachPhase::Stacked)
	{
		return false;
	}
	const FVector Eye = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : FVector(Hit.TraceStart);
	FVector Through;
	const FDoorwayBreachConfig& Config = DoorwayBreachRules::GetConfig();
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Breach.Outward).GetSafeNormal();
	// Inside the measured room (its floor / walls on the threshold's storey - review 2026-11-17: not the yard behind the building), or into the
	// stacked doorway itself (the opening + 40 cm at its plane, as the first click): from outside the camera often sees no floor through the
	// opening, only the jambs the stack leans round - "go in" is a click into that doorway again.
	const bool bInside = DoorwayBreachEntryRules::IsInsideRoom(Hit.ImpactPoint, Breach.Center, Breach.Outward, Breach.Room)
		|| (ClickPreviewRules::IsAtPortalPlane(Hit.ImpactPoint, Breach.Center, Breach.Outward, Config.PortalDepthSlackCm)
			&& ClickPreviewRules::RayThroughPortal(Eye, Hit.ImpactPoint, Breach.Center, Tangent, Breach.Outward, Breach.WidthCm, OpeningHeightCm, Config.PortalSlackCm,
				Config.PortalDepthSlackCm, Through));
	if (!bInside)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Breach] doorway %s: the click on %s at %s is not inside the room (%.0f cm in) - no entry"), *Breach.DoorwayId,
			*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString(), FVector::DotProduct(FVector(Hit.ImpactPoint) - Breach.Center, DoorwayBreachEntryRules::EntryDirection(Breach.Outward)));
		return false;
	}
	TArray<EDoorwayBreachSlotRole> Roles;
	TArray<FVector> Positions;
	for (const FBreachMember& Member : Breach.Members)
	{
		Roles.Add(Member.Slot.Role);
		Positions.Add(Member.Operative.IsValid() ? Member.Operative->GetActorLocation() : Member.Target);
	}
	const TArray<FDoorwayBreachEntryMove> Moves = DoorwayBreachEntryRules::PlanEntry(Breach.Center, Breach.Outward, Breach.WidthCm, Roles, Positions, Breach.Room);
	if (Moves.IsEmpty())
	{
		return false;
	}
	for (const FDoorwayBreachEntryMove& Move : Moves)
	{
		if (Breach.Members.IsValidIndex(Move.Participant))
		{
			FBreachMember& Member = Breach.Members[Move.Participant];
			Member.Entry = Move;
			Member.Entry.Destination = OnNav(GetWorld(), Move.Destination);
			Member.bEntryStarted = false;
			Member.bEntered = false;
		}
	}
	// Rear security (no entry move) counts as in place.
	for (FBreachMember& Member : Breach.Members)
	{
		if (Member.Entry.Participant == INDEX_NONE)
		{
			Member.bEntered = true;
		}
	}
	Breach.Phase = EBreachPhase::Entering;
	Breach.EntrySeconds = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Breach] doorway %s: the entry (%d men in, a click at %s)"), *Breach.DoorwayId, Moves.Num(), *Hit.ImpactPoint.ToCompactString());
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), LOCTEXT("BreachEntry", "🚪 Go, go, go!"));
	}
	return true;
}

void ACodexTacticsPlayerController::TickBreach(float DeltaSeconds)
{
	using namespace ControllerBreachLocal;
	if (Breach.Phase == EBreachPhase::None)
	{
		return;
	}
	// Review 2026-11-17: whatever ends the session restores what it set (the followers' hold, the sector look points) - another leader, a Solo <->
	// formation switch, the turn-based fight, and the cleared room (the callout is made; the men are free again).
	const USquadSubsystem* Squad = GetSquad();
	if (!Squad || Squad->GetLeader() != Breach.Leader.Get() || Squad->IsSoloMode() != Breach.bSolo || GetActiveTurnBased())
	{
		EndBreach(TEXT("the leader / mode changed"));
		return;
	}
	if (Breach.Phase == EBreachPhase::Cleared)
	{
		EndBreach(TEXT("the room is clear"));
		return;
	}
	Breach.Members.RemoveAll([](const FBreachMember& Member) { return !Member.Operative.IsValid() || !Member.Operative->HealthComponent || !Member.Operative->HealthComponent->IsAlive(); });
	if (Breach.Members.IsEmpty())
	{
		EndBreach(TEXT("nobody left"));
		return;
	}
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (Breach.Phase == EBreachPhase::Stacking)
	{
		Breach.Seconds += DeltaSeconds;
		bool bAll = true;
		for (FBreachMember& Member : Breach.Members)
		{
			AOperativeCharacter* Operative = Member.Operative.Get();
			if (!Member.bArrived)
			{
				const bool bJambPeek = Operative->IsManualCornerPeek();
				const bool bAtSpot = FVector::Dist2D(Operative->GetActorLocation(), Member.Target) <= DoorwayBreachEntryRules::ArrivalCm && !Operative->IsMoving();
				if (bJambPeek || bAtSpot)
				{
					Member.bArrived = true;
					if (!bJambPeek)
					{
						Operative->SetStance(Member.Slot.Stance);
						Operative->SetManualLookPoint(Feet(*Operative) + FVector(0.f, 0.f, 150.f) + Member.Slot.AimDirection.GetSafeNormal2D() * LookDistanceCm);
					}
					UE_LOG(LogCodexTactics, Display, TEXT("[Breach] %s at his %s slot%s"), *Operative->DisplayName.ToString(), *UEnum::GetValueAsString(Member.Slot.Role),
						bJambPeek ? TEXT(" (peeking in round the jamb)") : TEXT(""));
				}
			}
			bAll &= Member.bArrived;
		}
		if (bAll || Breach.Seconds >= StackTimeoutSeconds)
		{
			Breach.Phase = EBreachPhase::Stacked;
			UE_LOG(LogCodexTactics, Display, TEXT("[Breach] doorway %s: stacked (%d of %d at their slots) - a click inside the room enters"), *Breach.DoorwayId,
				GetBreachArrivedCount(), Breach.Members.Num());
			if (Messages)
			{
				Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), LOCTEXT("BreachReady", "🚪 Stacked - click inside the room to go in."));
			}
		}
		return;
	}
	if (Breach.Phase == EBreachPhase::Entering)
	{
		Breach.EntrySeconds += DeltaSeconds;
		bool bAll = true;
		for (FBreachMember& Member : Breach.Members)
		{
			AOperativeCharacter* Operative = Member.Operative.Get();
			if (Member.Entry.Participant != INDEX_NONE && !Member.bEntryStarted && Breach.EntrySeconds >= Member.Entry.DelaySeconds)
			{
				Member.bEntryStarted = true;
				Operative->ClearManualLookPoint();
				Operative->OrderMoveTo(Member.Entry.Destination, true);
			}
			if (Member.bEntryStarted && !Member.bEntered && FVector::Dist2D(Operative->GetActorLocation(), Member.Entry.Destination) <= DoorwayBreachEntryRules::ArrivalCm
				&& !Operative->IsMoving())
			{
				Member.bEntered = true;
				if (Member.Entry.bCrouch)
				{
					Operative->SetStance(EOperativeStance::Crouching);
				}
				Operative->SetManualLookPoint(Feet(*Operative) + FVector(0.f, 0.f, 150.f) + Member.Entry.LookDirection * LookDistanceCm);
				UE_LOG(LogCodexTactics, Display, TEXT("[Breach] %s is in (%s)"), *Operative->DisplayName.ToString(), *UEnum::GetValueAsString(Member.Slot.Role));
			}
			bAll &= Member.bEntered;
		}
		if (bAll || Breach.EntrySeconds >= EntryTimeoutSeconds)
		{
			Breach.Phase = EBreachPhase::Cleared;
			UE_LOG(LogCodexTactics, Display, TEXT("[Breach] doorway %s: room clear (%d of %d in)"), *Breach.DoorwayId, GetBreachEnteredCount(), Breach.Members.Num());
			if (Messages)
			{
				Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), LOCTEXT("BreachClear", "🚪 Room clear! Hold sectors."));
			}
			++BreachClearedCount;
			LastBreachParticipantCount = Breach.Members.Num();
			EndBreach(TEXT("the room is clear")); // the holds / sector looks it set are given back at once
		}
	}
}

void ACodexTacticsPlayerController::EndBreach(const TCHAR* Why)
{
	if (Breach.Phase == EBreachPhase::None && Breach.Members.IsEmpty())
	{
		return;
	}
	for (FBreachMember& Member : Breach.Members)
	{
		if (AOperativeCharacter* Operative = Member.Operative.Get())
		{
			Operative->ClearManualLookPoint(); // the sector looks of the breach end with it
		}
	}
	if (Breach.bHoldSet)
	{
		if (USquadSubsystem* Squad = GetSquad())
		{
			Squad->SetFollowersHolding(Breach.bHoldBefore);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Breach] session ends (%s)"), Why);
	Breach = FBreachSession();
}

#undef LOCTEXT_NAMESPACE
