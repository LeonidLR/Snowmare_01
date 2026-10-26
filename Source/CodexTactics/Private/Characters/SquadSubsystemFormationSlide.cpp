// USquadSubsystem: formation rules v2.2 in the runtime (behind Codex.Formation.Sectors) - the coverage allocator's patrol / halt looks
// (FormationCoverRules::Allocate: the rear guard, the open-arc shares), the places by minimal travel, and the wall slide
// (FormationWallRules::Slide: from the wall layout a move along the same wall side-steps the whole line, the order kept).
// User decisions 2026-10-25 (docs/design/FORMATION_SECTORS.md v2.2; contract docs/design/FORMATION_CONTRACT.md 11). The user's slide report:
// "from the wall layout a move along the wall broke the formation - one man went round behind the wall, then an echelon".
// Godot reference: none (UE-only; reference simulation Tools/TrainingGround/src/movement/sim.ts).

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallEndRules.h"
#include "Formation/FormationWallRules.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace SquadSubsystemSlideLocal
{
	/** The slide's wall probe traces this high above the feet (a low cover counts, as the wall order's), cm. */
	constexpr float SlideProbeHeightCm = 50.f;
	/** The wall face behind K's slot is looked for this far, cm. */
	constexpr float SlideWallReachCm = 250.f;
	/** The leader stands still this long at the end of the slide: the wall layout again, s. */
	constexpr float SlideArriveStillSeconds = 0.3f;
	/** A slide that never arrives ends after this, s. */
	constexpr float SlideMaxSeconds = 60.f;
	/** Rules v2.1: on a new formation the places change for any real saving (ties keep the places held: no needless swap), cm. */
	constexpr float NewFormationGainCm = 1.f;
	/** Bug B (2026-10-26) wall layout fit: the world walls round K's place this far, cm. */
	constexpr float WallFitReachCm = 600.f;
	/** ... a wall body this close to the line from K's place to a place cuts it off, cm. */
	constexpr float WallFitPassClearanceCm = 10.f;
	/** ... a place this close to a wall body cannot be stood on (the capsule), cm. */
	constexpr float WallFitBodyClearanceCm = 30.f;
	/** ... and a place must lie on the nav mesh within this, cm. */
	constexpr float WallFitNavExtentCm = 30.f;

	float SlideFeetZ(const AActor& Actor)
	{
		return static_cast<float>(Actor.GetActorLocation().Z) - Actor.GetSimpleCollisionHalfHeight();
	}
}

bool USquadSubsystem::GetFormationRearGuard(EFormationRole& OutRole) const
{
	if (!Sectors.bActiveThisFrame || !Sectors.RearGuard.IsSet())
	{
		return false;
	}
	OutRole = Sectors.RearGuard.GetValue();
	return true;
}

bool USquadSubsystem::AllocateFormationLooks(const AOperativeCharacter& LeaderRef, const FVector2D& LeaderPos)
{
	const FFormationConfig& Config = FormationTuning::Get();
	const int32 Count = Sectors.FrameSlots.Num();
	Sectors.ShoulderLooks.Reset();
	if (!FormationRuntime::IsCoverAllocatorEnabled() || !FormationRuntime::UsesCoverAllocator(Sectors.Formation, Sectors.Mode, Count)
		|| Sectors.FrameSlots[0].Role != EFormationRole::Commander)
	{
		Sectors.RearGuard.Reset();
		return false;
	}
	TArray<FFormationCoverMember> Places;
	for (int32 Index = 0; Index < Sectors.FrameSlots.Num(); ++Index)
	{
		const FFormationSlot& Slot = Sectors.FrameSlots[Index];
		FFormationCoverMember Place;
		Place.Role = Slot.Role;
		Place.Pos = Slot.Position;
		// Rules v2.1 trail column: how far back = the column order (a bent column's tail may stand beside the leader's heading).
		Place.Back = Sectors.bTrail ? static_cast<float>(Index) : FormationRuntime::PlaceBack(Slot.Position, LeaderPos, Sectors.HeadingYaw);
		// Rules v2.3 part 7 (contract 13.7): the trail column's places travel along the trail (its direction at the slot); else the heading.
		if (Sectors.bTrail)
		{
			if (const FFormationRoleYaw* Travel = Sectors.TrailTravel.FindByPredicate([&Slot](const FFormationRoleYaw& Each) { return Each.Role == Slot.Role; }))
			{
				Place.Travel = Travel->Yaw;
			}
		}
		Places.Add(Place);
	}
	// A look decided elsewhere (a facing order of his own, a quick fire, a combat target) counts as cover; the others fill the rest.
	TArray<FFormationRoleYaw> Fixed;
	const auto AddFixed = [&Fixed](const AOperativeCharacter* Operative, EFormationRole Role)
	{
		if (Operative && (Operative->HasOwnFacing() || Operative->IsFacingCombatTarget()))
		{
			Fixed.Add({ Role, static_cast<float>(Operative->GetActorRotation().Yaw) });
		}
	};
	AddFixed(&LeaderRef, EFormationRole::Commander);
	for (const FFollowerState& Follower : Followers)
	{
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (Role.IsSet() && FindFrameSlot(Role.GetValue()))
		{
			AddFixed(Follower.Operative.Get(), Role.GetValue());
		}
	}
	// Rules v2.1: the corner watcher's look round the wall end is fixed too (contract 11.2).
	for (const FFormationCornerLook& Corner : Sectors.CornerLooks)
	{
		if (!Fixed.ContainsByPredicate([&Corner](const FFormationRoleYaw& Each) { return Each.Role == Corner.Role; }))
		{
			Fixed.Add({ Corner.Role, Corner.Yaw });
		}
	}
	// Rules v2.3 (contract 13.3, user decision 1A clarified): the narrow column's K / P / L looks are the user's decision - fixed for the
	// allocator (P looks forward over the leader's shoulder), which shares out only what is left (Z the rear guard).
	if (FormationGuardRuntime::IsEnabled())
	{
		Fixed.Append(FormationGuardRuntime::DecidedFixedLooks(Sectors.FrameSlots, Sectors.Formation, Sectors.bTrail, Sectors.PassageWidth, Fixed, Config));
	}
	TArray<FFormationWall> Walls = Sectors.ProbeWalls;
	Walls.Append(Sectors.LookWalls);
	const TOptional<FFormationCoverResult> Result = FormationCoverRules::Allocate(Walls, LeaderPos, Sectors.HeadingYaw, Places, Sectors.Mode,
		Sectors.PatrolTime, Config, Fixed);
	if (!Result.IsSet())
	{
		Sectors.RearGuard.Reset();
		return false;
	}
	for (const FFormationRoleYaw& Look : Result->Looks)
	{
		if (FFormationSlot* Slot = Sectors.FrameSlots.FindByPredicate([&Look](const FFormationSlot& Each) { return Each.Role == Look.Role; }))
		{
			Slot->Yaw = Look.Yaw;
		}
	}
	if (Sectors.RearGuard != Result->RearGuard && Result->RearGuard.IsSet())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] rear guard: %s (%s)"), FormationNames::ToString(Result->RearGuard.GetValue()),
			FormationNames::ToString(Sectors.Formation));
	}
	Sectors.RearGuard = Result->RearGuard;
	Sectors.ShoulderLooks = Result->Shoulder;
	return true;
}

void USquadSubsystem::ReassignFormationPlaces()
{
	// Rules v2.1 (contract 10.6): a new formation in effect (a fold, the mirror, the trail column on / off, any switch) re-assigns the places
	// at once by minimal travel - whatever the saving, the cooldown not waited for.
	const bool bNewFormation = FormationGuardRuntime::IsEnabled()
		&& (!Sectors.bPlacesKeyValid || Sectors.PlacesFormation != Sectors.Formation || Sectors.bPlacesTrail != Sectors.bTrail);
	if ((SlotSwapCooldownRemaining > 0.f && !bNewFormation) || Sectors.bWallOrder || Sectors.bSlide || Sectors.Formation == EFormationKind::Wall
		|| HasMultiSelection())
	{
		return;
	}
	const int32 Count = FMath::Min(Sectors.MemberCount, 4);
	TArray<FVector2D> Places;
	for (int32 SlotIndex = 0; SlotIndex < Count - 1; ++SlotIndex)
	{
		const TOptional<EFormationRole> Role = FormationRuntime::RoleForFollowerSlot(SlotIndex, Count);
		const FFormationSlot* Slot = Role.IsSet() ? FindFrameSlot(Role.GetValue()) : nullptr;
		if (!Slot)
		{
			return;
		}
		Places.Add(Slot->Position);
	}
	TArray<FFollowerState*> Who;
	TArray<FVector2D> Positions;
	TArray<int32> Current;
	for (FFollowerState& Follower : Followers)
	{
		const AOperativeCharacter* Operative = Follower.Operative.Get();
		if (!Operative || Follower.Slot < 0 || Follower.Slot >= Count - 1)
		{
			continue;
		}
		if (IsOnOwnOrder(Operative) || Operative->bInCover || HasGroupCoverProne(Operative) || Operative->IsTrafficHeld())
		{
			return; // somebody is not walking in his place: keep the places
		}
		Who.Add(&Follower);
		Positions.Add(FVector2D(Operative->GetActorLocation()));
		Current.Add(Follower.Slot);
	}
	if (Who.Num() != Count - 1)
	{
		return;
	}
	// The key is taken once the places were weighed for it (a member busy at the switch: weighed again on the next frame).
	Sectors.PlacesFormation = Sectors.Formation;
	Sectors.bPlacesTrail = Sectors.bTrail;
	Sectors.bPlacesKeyValid = true;
	const TOptional<TArray<int32>> Best = FormationRuntime::ReassignPlaces(Positions, Current, Places,
		bNewFormation ? SquadSubsystemSlideLocal::NewFormationGainCm : FormationRuntime::ReassignGainCm);
	if (!Best.IsSet())
	{
		return;
	}
	FString Line;
	for (int32 Index = 0; Index < Who.Num(); ++Index)
	{
		FFollowerState& Follower = *Who[Index];
		Follower.Slot = Best.GetValue()[Index];
		Follower.RepathTimeRemaining = 0.f;
		Follower.PrevSectorSlotTime = -1.0;
		const TOptional<EFormationRole> Role = FormationRuntime::RoleForFollowerSlot(Follower.Slot, Count);
		Line += FString::Printf(TEXT(" %s -> %s;"), *Follower.Operative->DisplayName.ToString(), Role.IsSet() ? FormationNames::ToString(Role.GetValue()) : TEXT("-"));
	}
	SlotSwapCooldownRemaining = FormationConfig.SlotSwapCooldown;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] places by travel (%s):%s"), FormationNames::ToString(Sectors.Formation), *Line);
}

bool USquadSubsystem::TryStartFormationSlide(AOperativeCharacter& LeaderRef, bool bFromOrder)
{
	using namespace SquadSubsystemSlideLocal;
	if (!FormationRuntime::IsEnabled() || LeaderRef.IsSprinting())
	{
		return false; // a run along the wall is the normal formation (the slide walks at the patrol pace)
	}
	const FVector Destination3 = LeaderRef.GetLastMoveDestination();
	if (Destination3.IsZero())
	{
		return false;
	}
	const FVector2D Destination(Destination3);
	// From the layout in force - mid-slide from where the slide stands now (its targets).
	const TArray<FFormationSlot> Start = Sectors.bSlide ? Sectors.FrameSlots : Sectors.WallSlots;
	if (Start.Num() < 2 || Start[0].Role != EFormationRole::Commander)
	{
		return false;
	}
	const FFormationConfig& Config = FormationTuning::Get();
	const float NormalYaw = Sectors.WallNormalYaw;
	const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
	const float Along = static_cast<float>(FVector2D::DotProduct(Destination - Start[0].Position, RightAlong));
	// The wall behind K's slot, measured along far enough for the destination (the layout's own room stops at 4 m).
	const FVector Origin(Start[0].Position.X, Start[0].Position.Y, SlideFeetZ(LeaderRef) + SlideProbeHeightCm);
	FVector2D Face;
	float FaceNormalYaw = 0.f;
	float RoomLeft = 0.f;
	float RoomRight = 0.f;
	if (!BuildFormationWall(Origin, -FormationMath::Forward(NormalYaw), SlideWallReachCm, Face, FaceNormalYaw, RoomLeft, RoomRight,
		FormationRuntime::SlideRoomCm(Along, Config.WallSpacing)))
	{
		return false;
	}
	const FFormationWall Wall = FormationRuntime::WallAlong(Face, FaceNormalYaw, RoomLeft, RoomRight);
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	const bool bWasSliding = Sectors.bSlide;
	const bool bCrouchAfter = bFromOrder || (bWasSliding && Sectors.bSlideFromOrder);
	// Bug A (game designer 2026-10-26): a destination at a FREE end of this wall is the corner stack - the slide runs until the place nearest
	// the end stands at the edge (the point man), the others keep the order behind him; on the stop he peeks round the corner.
	FVector2D SlideTo = Destination;
	const TOptional<FFormationStackEnd> Stack = FormationGuardRuntime::IsEnabled()
		? FormationWallEnd::FindStackEnd(Sectors.WorldWalls, Start[0].Position, NormalYaw, Destination, Config) : TOptional<FFormationStackEnd>();
	if (Stack.IsSet())
	{
		const float Shift = FormationWallEnd::StackShift(Start, Stack.GetValue());
		SlideTo = Start[0].Position + Stack->Along * Shift;
		if (FMath::Abs(Shift) < FormationWallRules::SlideMin)
		{
			// Already at the edge: the stack where they stand (the leader to his place in it).
			const FVector2D KPlace = SlideTo;
			if (FVector2D::Distance(KPlace, LeaderPos) > FormationRuntime::StopRadiusCm)
			{
				LeaderRef.OrderMoveTo(FVector(KPlace.X, KPlace.Y, Destination3.Z), false);
			}
			else
			{
				LeaderRef.StopOperative();
			}
			Sectors.bSlide = false;
			Sectors.bSlideStack = false;
			HoldCornerStack(LeaderRef, Start, Stack.GetValue(), bCrouchAfter);
			Sectors.LastLeaderSerial = LeaderRef.GetMoveOrderSerial();
			Sectors.bLeaderOrderPending = false;
			return true;
		}
	}
	const FFormationSlideResult Slide = FormationWallRules::Slide(Wall, Start, LeaderPos, SlideTo, Sectors.PatrolTime, Config);
	if (!Slide.bActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the leader's order from the wall layout is no slide (%.0f cm along, room %.0f / %.0f cm)"), Along,
			RoomLeft, RoomRight);
		return false;
	}
	// The 4th (the pair's partner) keeps the end he is at when the line stops (contract 11.5).
	const FFormationSlot* Partner = Start.FindByPredicate([](const FFormationSlot& Slot) { return Slot.Role == EFormationRole::Rear; });
	const bool bEndRight = !Partner || FVector2D::DotProduct(Partner->Position - Start[0].Position, RightAlong) >= 0.0;
	// The leader walks to his slot moved along the wall (his offset from it kept) at the patrol pace; the others side-step with him.
	if (LeaderRef.OrderMoveTo(FVector(Slide.LeaderTarget.X, Slide.LeaderTarget.Y, Destination3.Z), false) != EOperativeOrderResult::Accepted)
	{
		return false;
	}
	Sectors.bSlide = true;
	Sectors.bSlideFromOrder = bCrouchAfter;
	Sectors.bSlideEndRight = bEndRight;
	Sectors.bSlideStack = Stack.IsSet();
	Sectors.SlideStackEnd = Stack.Get(FFormationStackEnd());
	Sectors.bCornerStack = false;
	Sectors.SlideWall = Wall;
	Sectors.SlideStartSlots = Start;
	Sectors.SlideDestination = SlideTo;
	Sectors.SlideLeaderTarget = Slide.LeaderTarget;
	Sectors.SlideSeconds = 0.f;
	Sectors.SlideSerial = LeaderRef.GetMoveOrderSerial();
	Sectors.LastLeaderSerial = Sectors.SlideSerial;
	Sectors.bLeaderOrderPending = false;
	if (Sectors.bWallOrder)
	{
		Sectors.WallOrderSerial = Sectors.SlideSerial; // the order (and its roles) goes on along the wall
	}
	Sectors.Formation = EFormationKind::Wall;
	Sectors.FrameSlots = Slide.Targets;
	++Sectors.SlideCount;
	// Up from the squad's crouch: the whole line side-steps standing.
	for (FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		if (!Operative)
		{
			continue;
		}
		Follower.bParked = false;
		Follower.RepathTimeRemaining = 0.f;
		Follower.PrevSectorSlotTime = -1.0;
		if (Operative->IsInFormationWallPose())
		{
			Operative->SetFormationWallPose(false);
			if (Operative->GetStance() == EOperativeStance::Crouching && Operative->IsStanceFromSquad() && LeaderRef.GetStance() == EOperativeStance::Standing)
			{
				Operative->SetStance(EOperativeStance::Standing);
				Operative->MarkStanceFromSquad();
				Follower.RepathTimeRemaining = Operative->GetMoveHoldSeconds();
			}
		}
	}
	FString Order;
	for (const EFormationRole Role : Slide.Order)
	{
		Order += FormationNames::ToString(Role);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall slide %d: %d members %.0f cm to the %s along the wall (order %s, lead %s, rear guard %s, room %.0f / %.0f cm)%s%s"),
		Sectors.SlideCount, Start.Num(), FMath::Abs(Along), Along >= 0.f ? TEXT("right") : TEXT("left"), *Order,
		Slide.Lead.IsSet() ? FormationNames::ToString(Slide.Lead.GetValue()) : TEXT("-"),
		Slide.RearGuard.IsSet() ? FormationNames::ToString(Slide.RearGuard.GetValue()) : TEXT("-"), RoomLeft, RoomRight,
		bWasSliding ? TEXT(" (restarted)") : TEXT(""),
		Stack.IsSet() ? *FString::Printf(TEXT(" - to the free wall end at (%.0f, %.0f): the corner stack, the leader %.0f cm along"), Stack->End.X, Stack->End.Y,
			FVector2D::Distance(Slide.LeaderTarget, Start[0].Position)) : TEXT(""));
	return true;
}

void USquadSubsystem::UpdateFormationSlide(AOperativeCharacter& LeaderRef, float DeltaTime)
{
	using namespace SquadSubsystemSlideLocal;
	Sectors.SlideSeconds += DeltaTime;
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	const FFormationSlideResult Slide = FormationWallRules::Slide(Sectors.SlideWall, Sectors.SlideStartSlots, LeaderPos, Sectors.SlideDestination,
		Sectors.PatrolTime, FormationTuning::Get());
	if (!Slide.bActive)
	{
		EndFormationSlide(LeaderRef, false, TEXT("the destination is no longer along the wall"));
		return;
	}
	Sectors.Formation = EFormationKind::Wall;
	Sectors.FrameSlots = Slide.Targets;
	Sectors.RearGuard = Slide.RearGuard;
	const bool bArrived = !LeaderRef.IsMoving() && Sectors.LeaderStillSeconds >= SlideArriveStillSeconds;
	if (bArrived || Sectors.SlideSeconds > SlideMaxSeconds)
	{
		EndFormationSlide(LeaderRef, true, bArrived ? TEXT("the leader stopped") : TEXT("too long"));
	}
}

void USquadSubsystem::EndFormationSlide(AOperativeCharacter& LeaderRef, bool bArrived, const TCHAR* Why)
{
	if (!Sectors.bSlide)
	{
		return;
	}
	Sectors.bSlide = false;
	const bool bStack = Sectors.bSlideStack;
	Sectors.bSlideStack = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall slide over after %.1f s (%.0f cm off its end): %s"), Sectors.SlideSeconds,
		FVector2D::Distance(FVector2D(LeaderRef.GetActorLocation()), Sectors.SlideLeaderTarget), Why);
	if (bArrived && bStack)
	{
		// Bug A: the slide to a free wall end stops as the corner stack (the point man at the edge peeks round it).
		HoldCornerStack(LeaderRef, Sectors.SlideStartSlots, Sectors.SlideStackEnd, Sectors.bSlideFromOrder);
		return;
	}
	if (bArrived && HoldWallLayoutAt(LeaderRef, Sectors.WallNormalYaw, Sectors.bSlideFromOrder, Sectors.bSlideEndRight))
	{
		return;
	}
	if (Sectors.bWallOrder)
	{
		EndFormationWallOrder(Why);
	}
	else
	{
		Sectors.bWallLayoutValid = false;
		Sectors.Formation = Sectors.MoveFormation;
	}
}

bool USquadSubsystem::HoldWallLayoutAt(AOperativeCharacter& LeaderRef, float WallNormalYaw, bool bCrouch, const TOptional<bool>& KeepEndRight)
{
	using namespace SquadSubsystemSlideLocal;
	const FFormationConfig& Config = FormationTuning::Get();
	const int32 Count = FMath::Clamp(Sectors.MemberCount, 1, 4);
	const FVector Origin(LeaderRef.GetActorLocation().X, LeaderRef.GetActorLocation().Y, SlideFeetZ(LeaderRef) + SlideProbeHeightCm);
	FVector2D Face;
	float NormalYaw = 0.f;
	float RoomLeft = 0.f;
	float RoomRight = 0.f;
	if (!BuildFormationWall(Origin, -FormationMath::Forward(WallNormalYaw), SlideWallReachCm, Face, NormalYaw, RoomLeft, RoomRight))
	{
		return false;
	}
	if (KeepEndRight.IsSet())
	{
		FormationRuntime::KeepEndAfterSlide(RoomLeft, RoomRight, KeepEndRight.GetValue(), Count, Config.WallSpacing);
	}
	const FFormationWall Wall = FormationRuntime::WallAlong(Face, NormalYaw, RoomLeft, RoomRight);
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	TArray<FFormationSlot> Slots = FormationWallRules::Layout(Wall, LeaderPos, Count, RoomLeft, RoomRight, Config);
	if (Slots.IsEmpty())
	{
		return false;
	}
	const float OutwardYaw = FormationWallRules::OutwardYaw(Wall, LeaderPos);
	// Bug B (2026-10-26): no place on the far side of a wall from K's place (a T / L junction, the wall's end) - such a place moves to K's other side.
	Slots = FitWallLayoutToWorld(Slots, OutwardYaw, SlideFeetZ(LeaderRef));
	// The automatic layout's (or a new leader's) places by minimal travel become the held layout's roles; a wall order keeps its own.
	AssignWallRolesByTravel(Slots);
	Sectors.WallSlots = MoveTemp(Slots);
	Sectors.WallNormalYaw = OutwardYaw;
	Sectors.bWallLayoutValid = true;
	Sectors.WallAnchor = LeaderPos;
	Sectors.bWallOrder = true;
	Sectors.bWallOrderCrouch = bCrouch;
	Sectors.WallOrderLeader = &LeaderRef;
	Sectors.WallOrderSerial = LeaderRef.GetMoveOrderSerial();
	Sectors.WallOrderSeconds = 0.f;
	Sectors.Formation = EFormationKind::Wall;
	Sectors.bCornerStack = false;
	for (FFollowerState& Follower : Followers)
	{
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the wall layout again (%d members, outward yaw %.0f, room %.0f / %.0f cm%s)"), Count, Sectors.WallNormalYaw,
		RoomLeft, RoomRight, bCrouch ? TEXT(", crouched") : TEXT(""));
	return true;
}

void USquadSubsystem::AssignWallRolesByTravel(const TArray<FFormationSlot>& Slots)
{
	if (Sectors.bWallOrder && Sectors.WallOrderRoles.Num() > 0)
	{
		return; // the order's roles (OrderFormationWall's assignment) go on
	}
	TArray<FVector2D> To;
	TArray<EFormationRole> PlaceRoles;
	for (const FFormationSlot& Slot : Slots)
	{
		if (Slot.Role != EFormationRole::Commander)
		{
			To.Add(Slot.Position);
			PlaceRoles.Add(Slot.Role);
		}
	}
	TArray<TWeakObjectPtr<AOperativeCharacter>> Who;
	TArray<FVector2D> From;
	for (const FFollowerState& Follower : Followers)
	{
		if (Follower.Operative.IsValid() && Who.Num() < To.Num())
		{
			Who.Add(Follower.Operative);
			From.Add(FVector2D(Follower.Operative->GetActorLocation()));
		}
	}
	Sectors.WallOrderRoles.Reset();
	const TArray<int32> Assignment = FormationRuntime::AssignByTravel(From, To);
	FString Line;
	for (int32 Index = 0; Index < Who.Num() && Index < Assignment.Num(); ++Index)
	{
		if (PlaceRoles.IsValidIndex(Assignment[Index]))
		{
			Sectors.WallOrderRoles.Add(Who[Index], PlaceRoles[Assignment[Index]]);
			Line += FString::Printf(TEXT(" %s -> %s;"), *Who[Index]->DisplayName.ToString(), FormationNames::ToString(PlaceRoles[Assignment[Index]]));
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the wall layout's places by travel:%s"), *Line);
}

void USquadSubsystem::HoldCornerStack(AOperativeCharacter& LeaderRef, const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End, bool bCrouch)
{
	const FFormationConfig& Config = FormationTuning::Get();
	TArray<FFormationSlot> Slots = FormationWallEnd::StackSlots(StartSlots, End, Sectors.WallNormalYaw, Config);
	if (Slots.IsEmpty())
	{
		return;
	}
	if (!Sectors.bWallOrder)
	{
		// The automatic layout's slide: its places are the follower slots' roles (the slide's targets were by those roles).
		Sectors.WallOrderRoles.Reset();
		for (const FFollowerState& Follower : Followers)
		{
			const TOptional<EFormationRole> Role = FormationRuntime::RoleForFollowerSlot(Follower.Slot, Sectors.MemberCount);
			if (Role.IsSet() && Follower.Operative.IsValid())
			{
				Sectors.WallOrderRoles.Add(Follower.Operative, Role.GetValue());
			}
		}
	}
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	const EFormationRole Point = FormationWallEnd::PointRole(Slots, End);
	const FFormationSlot* PointSlot = Slots.FindByPredicate([Point](const FFormationSlot& Each) { return Each.Role == Point; });
	Sectors.WallSlots = MoveTemp(Slots);
	Sectors.bWallLayoutValid = true;
	Sectors.WallAnchor = LeaderPos;
	Sectors.bWallOrder = true;
	Sectors.bWallOrderCrouch = bCrouch;
	Sectors.WallOrderLeader = &LeaderRef;
	Sectors.WallOrderSerial = LeaderRef.GetMoveOrderSerial();
	Sectors.WallOrderSeconds = 0.f;
	Sectors.Formation = EFormationKind::Wall;
	Sectors.bCornerStack = true;
	Sectors.CornerStackEnd = End;
	for (FFollowerState& Follower : Followers)
	{
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
	}
	FString Line;
	for (const FFormationSlot& Slot : Sectors.WallSlots)
	{
		Line += FString::Printf(TEXT(" %s (%.0f, %.0f) looks %.0f;"), FormationNames::ToString(Slot.Role), Slot.Position.X, Slot.Position.Y, Slot.Yaw);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the corner stack at the wall's end (%.0f, %.0f): the point man %s peeks round it (yaw %.0f)%s:%s"), End.End.X,
		End.End.Y, FormationNames::ToString(Point), PointSlot ? PointSlot->Yaw : 0.f, bCrouch ? TEXT(", crouched") : TEXT(""), *Line);

}

void USquadSubsystem::PassWallOrderToLeader(AOperativeCharacter& NewLeader)
{
	if (!FormationRuntime::IsEnabled() || !(Sectors.bWallOrder || Sectors.bSlide) || bIsSoloMode || bFollowersHolding || Followers.IsEmpty() || HasMultiSelection())
	{
		return;
	}
	const bool bCrouch = Sectors.bWallOrder ? Sectors.bWallOrderCrouch : Sectors.bSlideFromOrder;
	if (IsOnOwnOrder(&NewLeader))
	{
		return; // he walks on: the order ends with "the leader changed" (UpdateFormationWallOrder)
	}
	// The order goes on with him: the layout again round him at the same wall (K in the middle), the order's crouch kept, the places by
	// minimal travel. A running slide was the old leader's move: it stops here (he stopped on becoming the leader).
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;
	Sectors.bCornerStack = false;
	Sectors.bWallOrder = false;
	Sectors.WallOrderRoles.Reset();
	if (HoldWallLayoutAt(NewLeader, Sectors.WallNormalYaw, bCrouch, TOptional<bool>()))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the wall order goes on with the new leader %s (the layout round him%s)"), *NewLeader.DisplayName.ToString(),
			bCrouch ? TEXT(", crouched") : TEXT(""));
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall order over: the new leader %s has no wall behind him"), *NewLeader.DisplayName.ToString());
	Sectors.bWallOrderCrouch = true;
	Sectors.bWallLayoutValid = false;
	Sectors.WallOrderLeader.Reset();
	Sectors.Formation = Sectors.MoveFormation;
}

TArray<FFormationWall> USquadSubsystem::GatherWallsAround(const FVector2D& Center, float HalfExtentCm, float FeetZ) const
{
	return GatherFormationWalls(Center, FVector2D(HalfExtentCm, HalfExtentCm), 0.f, FeetZ + FormationRuntime::ProbeHeightCm);
}

TArray<FFormationSlot> USquadSubsystem::FitWallLayoutToWorld(const TArray<FFormationSlot>& Slots, float NormalYaw, float FeetZ) const
{
	using namespace SquadSubsystemSlideLocal;
	if (Slots.Num() < 2 || !FormationGuardRuntime::IsEnabled())
	{
		return Slots;
	}
	const TArray<FFormationWall> Walls = GatherWallsAround(Slots[0].Position, WallFitReachCm, FeetZ);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const auto Usable = [&Walls, Nav, FeetZ](const FVector2D& KPlace, const FVector2D& Place)
	{
		if (FormationMath::SegmentBlocked(KPlace, Place, Walls, WallFitPassClearanceCm))
		{
			return false; // a wall body between K's place and this one
		}
		for (const FFormationWall& Wall : Walls)
		{
			if (FormationMath::DistToWall(Place, Wall) < WallFitBodyClearanceCm)
			{
				return false; // in a wall's clearance (a man cannot stand there)
			}
		}
		FNavLocation OnNav;
		return !Nav || (Nav->ProjectPointToNavigation(FVector(Place, FeetZ), OnNav, FVector(WallFitNavExtentCm, WallFitNavExtentCm, 150.f))
			&& FVector2D::Distance(FVector2D(OnNav.Location), Place) <= WallFitNavExtentCm);
	};
	TArray<FFormationSlot> Fitted = FormationWallEnd::FitLayout(Slots, NormalYaw, Usable, FormationTuning::Get());
	for (int32 Index = 1; Index < Fitted.Num() && Index < Slots.Num(); ++Index)
	{
		if (!Fitted[Index].Position.Equals(Slots[Index].Position, 1.0))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall layout fitted: %s (%.0f, %.0f) is cut off from K's place / no floor -> (%.0f, %.0f) on K's other side"),
				FormationNames::ToString(Fitted[Index].Role), Slots[Index].Position.X, Slots[Index].Position.Y, Fitted[Index].Position.X, Fitted[Index].Position.Y);
		}
	}
	return Fitted;
}

bool USquadSubsystem::PreviewFormationMove(const FVector& Destination, TArray<TArray<FVector>>& OutPaths) const
{
	OutPaths.Reset();
	const AOperativeCharacter* LeaderRef = Leader.Get();
	UWorld* World = GetWorld();
	if (!LeaderRef || !World || !FormationRuntime::IsEnabled() || bIsSoloMode || bFollowersHolding || HasMultiSelection() || !IsFormationActive()
		|| Sectors.Formation != EFormationKind::Wall || !Sectors.bWallLayoutValid || LeaderRef->bInCover)
	{
		return false;
	}
	const TArray<FFormationSlot>& Start = Sectors.bSlide ? Sectors.FrameSlots : Sectors.WallSlots;
	if (Start.IsEmpty() || Start[0].Role != EFormationRole::Commander)
	{
		return false;
	}
	const FFormationConfig& Config = FormationTuning::Get();
	const float FeetZ = static_cast<float>(LeaderRef->GetActorLocation().Z) - LeaderRef->GetSimpleCollisionHalfHeight();
	const TArray<FFormationWall> Walls = Sectors.bWorldWallsValid ? Sectors.WorldWalls : GatherWallsAround(FVector2D(LeaderRef->GetActorLocation()), 1600.f, FeetZ);
	const int32 SquadWall = FormationWallEnd::FindSquadWall(Walls, Start[0].Position, Sectors.WallNormalYaw);
	const FVector2D Target(Destination);
	if (SquadWall == INDEX_NONE || FormationMath::DistToWall(Target, Walls[SquadWall]) > Config.SlideMaxOffWall)
	{
		return false; // not along / round the squad's wall: a plain move
	}
	// Who stands at which place (the leader at K).
	const auto MemberAt = [this, LeaderRef](EFormationRole Role) -> const AOperativeCharacter*
	{
		if (Role == EFormationRole::Commander)
		{
			return LeaderRef;
		}
		for (const FFollowerState& Follower : Followers)
		{
			const TOptional<EFormationRole> Each = SectorRoleOf(Follower);
			if (Each.IsSet() && Each.GetValue() == Role)
			{
				return Follower.Operative.Get();
			}
		}
		return nullptr;
	};
	const auto Feet = [](const AOperativeCharacter& Member) { return Member.GetActorLocation() - FVector(0.f, 0.f, Member.GetSimpleCollisionHalfHeight()); };
	// A slide along the wall (to the corner stack at a free end): every member's line to his place.
	TArray<FFormationSlot> Targets;
	if (const TOptional<FFormationStackEnd> Stack = FormationWallEnd::FindStackEnd(Walls, Start[0].Position, Sectors.WallNormalYaw, Target, Config); Stack.IsSet())
	{
		Targets = FormationWallEnd::StackSlots(Start, Stack.GetValue(), Sectors.WallNormalYaw, Config);
	}
	else
	{
		const FFormationSlideResult Slide = FormationWallRules::Slide(Walls[SquadWall], Start, Start[0].Position, Target, 0.f, Config);
		if (Slide.bActive)
		{
			Targets = FormationWallRules::Slide(Walls[SquadWall], Start, Slide.LeaderTarget, Target, 0.f, Config).Targets;
		}
	}
	if (!Targets.IsEmpty())
	{
		for (const FFormationSlot& Slot : Targets)
		{
			if (const AOperativeCharacter* Member = MemberAt(Slot.Role))
			{
				OutPaths.Add({ Feet(*Member), FVector(Slot.Position.X, Slot.Position.Y, Feet(*Member).Z) });
			}
		}
		return !OutPaths.IsEmpty();
	}
	// Round the wall's end / to its other face: the leader's way there on the nav mesh (the others follow it).
	TArray<FVector> Path;
	if (const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(World, Feet(*LeaderRef), Destination, const_cast<AOperativeCharacter*>(LeaderRef));
		NavPath && NavPath->IsValid() && NavPath->PathPoints.Num() >= 2)
	{
		Path = NavPath->PathPoints;
		Path[0] = Feet(*LeaderRef);
	}
	else
	{
		Path = { Feet(*LeaderRef), Destination };
	}
	OutPaths.Add(MoveTemp(Path));
	return true;
}
