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
#include "Formation/FormationWallRules.h"

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
	const FFormationSlideResult Slide = FormationWallRules::Slide(Wall, Start, LeaderPos, Destination, Sectors.PatrolTime, Config);
	if (!Slide.bActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the leader's order from the wall layout is no slide (%.0f cm along, room %.0f / %.0f cm)"), Along,
			RoomLeft, RoomRight);
		return false;
	}
	// The 4th (the pair's partner) keeps the end he is at when the line stops (contract 11.5).
	const FFormationSlot* Partner = Start.FindByPredicate([](const FFormationSlot& Slot) { return Slot.Role == EFormationRole::Rear; });
	const bool bEndRight = !Partner || FVector2D::DotProduct(Partner->Position - Start[0].Position, RightAlong) >= 0.0;
	const bool bWasSliding = Sectors.bSlide;
	// The leader walks to his slot moved along the wall (his offset from it kept) at the patrol pace; the others side-step with him.
	if (LeaderRef.OrderMoveTo(FVector(Slide.LeaderTarget.X, Slide.LeaderTarget.Y, Destination3.Z), false) != EOperativeOrderResult::Accepted)
	{
		return false;
	}
	Sectors.bSlide = true;
	Sectors.bSlideFromOrder = bFromOrder || (bWasSliding && Sectors.bSlideFromOrder);
	Sectors.bSlideEndRight = bEndRight;
	Sectors.SlideWall = Wall;
	Sectors.SlideStartSlots = Start;
	Sectors.SlideDestination = Destination;
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
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall slide %d: %d members %.0f cm to the %s along the wall (order %s, lead %s, rear guard %s, room %.0f / %.0f cm)%s"),
		Sectors.SlideCount, Start.Num(), FMath::Abs(Along), Along >= 0.f ? TEXT("right") : TEXT("left"), *Order,
		Slide.Lead.IsSet() ? FormationNames::ToString(Slide.Lead.GetValue()) : TEXT("-"),
		Slide.RearGuard.IsSet() ? FormationNames::ToString(Slide.RearGuard.GetValue()) : TEXT("-"), RoomLeft, RoomRight,
		bWasSliding ? TEXT(" (restarted)") : TEXT(""));
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
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall slide over after %.1f s (%.0f cm off its end): %s"), Sectors.SlideSeconds,
		FVector2D::Distance(FVector2D(LeaderRef.GetActorLocation()), Sectors.SlideLeaderTarget), Why);
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
	if (!Sectors.bWallOrder)
	{
		// The automatic layout's places (by follower slot) become the held layout's roles.
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
	Sectors.WallSlots = MoveTemp(Slots);
	Sectors.WallNormalYaw = FormationWallRules::OutwardYaw(Wall, LeaderPos);
	Sectors.bWallLayoutValid = true;
	Sectors.WallAnchor = LeaderPos;
	Sectors.bWallOrder = true;
	Sectors.bWallOrderCrouch = bCrouch;
	Sectors.WallOrderLeader = &LeaderRef;
	Sectors.WallOrderSerial = LeaderRef.GetMoveOrderSerial();
	Sectors.WallOrderSeconds = 0.f;
	Sectors.Formation = EFormationKind::Wall;
	for (FFollowerState& Follower : Followers)
	{
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the wall layout again (%d members, outward yaw %.0f, room %.0f / %.0f cm%s)"), Count, Sectors.WallNormalYaw,
		RoomLeft, RoomRight, bCrouch ? TEXT(", crouched") : TEXT(""));
	return true;
}
