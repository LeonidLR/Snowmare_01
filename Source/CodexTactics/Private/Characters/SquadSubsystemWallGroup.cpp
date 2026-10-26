// USquadSubsystem: the rules v2.2 wall group - one squad-level "re-allocate the wall group" call (ReallocateWallGroup) behind
// Codex.Formation.Sectors. The squad stands at a wall with the leader in a real cover slot (the group cover order's cover switch / wrap /
// slot path): his cover facing / corner aim is a FIXED look and FormationWallGroup::Allocate (FormationCoverRules::AllocateStatic) gives the
// followers the looks that close the open arc - nobody looks into a wall, one covers the rear - held by their cover side along the wall or
// the back-to-wall pose facing out.
// User playtest 2026-10-25 (docs/design/FORMATION_SECTORS.md v2.2 "A look decided elsewhere counts as cover"): "in group cover at a wall
// corner the leader dashed to another wall's edge and held his new corner, the two others stayed 'Hugging the wall, holding the corner!'
// looking the same way as he - a wide gap opened". Godot reference: none (UE-only; contract docs/design/FORMATION_CONTRACT.md 11.3).

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallGroupRules.h"
#include "Tactics/CoverFacingRules.h"

namespace SquadSubsystemWallGroupLocal
{
	/** The allocation runs again this often while the group stands (the leader's aim turns, members arrive), s. */
	constexpr float WallGroupRecheckSeconds = 0.5f;
	/** The leader out of cover (and not on his way into one) this long ends the group, s. */
	constexpr float WallGroupLeaderOutSeconds = 1.f;
	/** The wall-group probes trace this high above the feet (a low cover object counts, as the wall order's probes), cm. */
	constexpr float WallGroupProbeHeightCm = 50.f;
	/** Rays round each member for the walls next to him. */
	constexpr int32 WallGroupProbeRays = 16;
	/** A logged allocation differs from the last one by more than this, deg. */
	constexpr float WallGroupLogDeg = 5.f;

	const TCHAR* WallGroupKindName(EWallGroupLook Kind)
	{
		switch (Kind)
		{
		case EWallGroupLook::Fixed: return TEXT("fixed");
		case EWallGroupLook::AlongWall: return TEXT("along the wall");
		case EWallGroupLook::FacingOut: return TEXT("back to the wall, facing out");
		default: return TEXT("free");
		}
	}

	bool IsWallGroupAlive(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}
}

void USquadSubsystem::ReallocateWallGroup(AOperativeCharacter* GroupLeader, const TArray<AOperativeCharacter*>& NewMembers, const TCHAR* Why)
{
	if (!FormationRuntime::IsEnabled() || !GroupLeader)
	{
		return; // Codex.Formation.Sectors 0: the old facing (CoverFacingRules for everyone)
	}
	const bool bGroupMode = GroupLeader == Leader.Get() && !bIsSoloMode && !bFollowersHolding && !HasMultiSelection() && IsGroupCoverFormation();
	if (!bGroupMode)
	{
		if (WallGroup.bActive)
		{
			EndWallGroup(TEXT("not in group mode"));
		}
		return;
	}
	if (Sectors.bWallOrder && Sectors.WallOrderLeader.Get() == GroupLeader)
	{
		// The formation wall layout (the group cover order in group mode): nobody in a cover slot, its looks are the user's wall rule (L 8,
		// K 6, P 4 - FormationWallRules::Layout), which already leaves no open gap.
		if (WallGroup.bActive)
		{
			EndWallGroup(TEXT("the formation wall layout"));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] %s: %s - the wall layout's looks (the user's wall rule)"), *GroupLeader->DisplayName.ToString(), Why);
		return;
	}
	if (!WallGroup.bActive || WallGroup.Leader.Get() != GroupLeader)
	{
		if (WallGroup.bActive)
		{
			EndWallGroup(TEXT("another leader"));
		}
		WallGroup = FWallGroupState();
		WallGroup.bActive = true;
		WallGroup.Leader = GroupLeader;
	}
	WallGroup.LeaderSerial = GroupLeader->GetMoveOrderSerial(); // the order that made the group (his next one ends it)
	for (AOperativeCharacter* Member : NewMembers)
	{
		if (Member && Member != GroupLeader && Followers.ContainsByPredicate([Member](const FFollowerState& F) { return F.Operative.Get() == Member; }))
		{
			WallGroup.Members.AddUnique(Member);
		}
	}
	WallGroup.bDirty = true;
	WallGroup.Why = Why;
	UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] re-allocate the wall group: %s (leader %s, %d members)"), Why, *GroupLeader->DisplayName.ToString(),
		WallGroup.Members.Num());
}

void USquadSubsystem::NotifyWallGroupCoverEntered(AOperativeCharacter* Operative, bool bSwitchArrival, bool bWrapArrival)
{
	if (!WallGroup.bActive || !Operative)
	{
		return;
	}
	const bool bGroupLeader = Operative == WallGroup.Leader.Get();
	if (!bGroupLeader && !WallGroup.Members.Contains(Operative))
	{
		return;
	}
	ReallocateWallGroup(WallGroup.Leader.Get(), TArray<AOperativeCharacter*>(), bGroupLeader
		? (bSwitchArrival ? TEXT("the leader's cover-switch arrival") : bWrapArrival ? TEXT("the leader's wrap arrival") : TEXT("the leader's arrival"))
		: TEXT("a member's (re-)entry"));
}

bool USquadSubsystem::GetWallGroupLook(const AOperativeCharacter* Operative, float& OutYaw, EWallGroupLook& OutKind) const
{
	const FWallGroupLookResult* Look = WallGroup.bActive
		? WallGroup.Looks.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative))) : nullptr;
	if (!Look)
	{
		return false;
	}
	OutYaw = Look->Yaw;
	OutKind = Look->Kind;
	return true;
}

void USquadSubsystem::EndWallGroup(const TCHAR* Why)
{
	if (!WallGroup.bActive)
	{
		return;
	}
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : WallGroup.Members)
	{
		if (AOperativeCharacter* Operative = Member.Get())
		{
			Operative->SetCoverLookSide(TOptional<ECoverFacing>()); // his own cover facing again
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] over after %.1f s (%d allocations): %s"), WallGroup.Seconds, WallGroup.Allocations, Why);
	WallGroup = FWallGroupState();
}

void USquadSubsystem::UpdateWallGroup(float DeltaTime)
{
	using namespace SquadSubsystemWallGroupLocal;
	if (!WallGroup.bActive)
	{
		return;
	}
	AOperativeCharacter* GroupLeader = WallGroup.Leader.Get();
	const TCHAR* EndWhy = nullptr;
	if (!FormationRuntime::IsEnabled())
	{
		EndWhy = TEXT("Codex.Formation.Sectors 0");
	}
	else if (!IsWallGroupAlive(GroupLeader) || GroupLeader != Leader.Get())
	{
		EndWhy = TEXT("the leader changed");
	}
	else if (bIsSoloMode || bFollowersHolding || HasMultiSelection())
	{
		EndWhy = TEXT("Solo / a box selection");
	}
	else if (!IsGroupCoverFormation())
	{
		EndWhy = TEXT("the group cover formation is off (turn-based / Codex.GroupCover.InCombat 0 in a fight)");
	}
	else if (GroupLeader->GetMoveOrderSerial() != WallGroup.LeaderSerial)
	{
		EndWhy = TEXT("the leader's next order");
	}
	else
	{
		WallGroup.LeaderOutSeconds = GroupLeader->bInCover || GroupLeader->HasPendingCover() ? 0.f : WallGroup.LeaderOutSeconds + DeltaTime;
		if (WallGroup.LeaderOutSeconds > WallGroupLeaderOutSeconds)
		{
			EndWhy = TEXT("the leader left the cover");
		}
	}
	if (EndWhy)
	{
		EndWallGroup(EndWhy);
		return;
	}
	WallGroup.Seconds += DeltaTime;
	// A member no longer at the wall (another order of his own, gone, no longer a follower) leaves the group.
	for (int32 Index = WallGroup.Members.Num() - 1; Index >= 0; --Index)
	{
		AOperativeCharacter* Member = WallGroup.Members[Index].Get();
		const bool bFollower = Member && Followers.ContainsByPredicate([Member](const FFollowerState& F) { return F.Operative.Get() == Member; });
		const bool bWallPose = Member && WallGroup.WallPose.Contains(Member) && Member->IsInFormationWallPose() && !Member->HasOwnMoveOrder();
		const bool bAtWall = Member && (Member->bInCover || Member->HasPendingCover() || IsWaitingForGroupCoverLeader(Member) || HasGroupCoverProne(Member) || bWallPose);
		if (IsWallGroupAlive(Member) && bFollower && bAtWall)
		{
			continue;
		}
		if (Member)
		{
			Member->SetCoverLookSide(TOptional<ECoverFacing>());
			UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] %s leaves the wall group"), *Member->DisplayName.ToString());
		}
		WallGroup.WallPose.Remove(WallGroup.Members[Index]);
		WallGroup.Looks.Remove(WallGroup.Members[Index]);
		WallGroup.Members.RemoveAt(Index);
	}
	WallGroup.RecheckSeconds -= DeltaTime;
	if (WallGroup.bDirty || WallGroup.RecheckSeconds <= 0.f)
	{
		AllocateWallGroupNow();
	}
}

TArray<FFormationWall> USquadSubsystem::TraceLookWalls(const TArray<FVector>& Positions, float HeightAboveFeet, int32 Rays, float Length) const
{
	TArray<FFormationWall> Walls;
	UWorld* World = GetWorld();
	if (!World || Rays <= 0)
	{
		return Walls;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationLookProbe), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		Params.AddIgnoredActor(Member.Get());
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	for (const FVector& Feet : Positions)
	{
		const FVector Origin = Feet + FVector(0.f, 0.f, HeightAboveFeet);
		for (int32 Ray = 0; Ray < Rays; ++Ray)
		{
			const FVector Direction = FRotator(0.f, 360.f * static_cast<float>(Ray) / static_cast<float>(Rays), 0.f).Vector();
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * Length, Objects, Params) && FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
			{
				Walls.Add(FormationRuntime::WallFromHit(FVector2D(Hit.ImpactPoint), FVector2D(Hit.ImpactNormal), 30.f));
			}
		}
	}
	return Walls;
}

void USquadSubsystem::AllocateWallGroupNow()
{
	using namespace SquadSubsystemWallGroupLocal;
	WallGroup.RecheckSeconds = WallGroupRecheckSeconds;
	AOperativeCharacter* GroupLeader = WallGroup.Leader.Get();
	if (!GroupLeader || !GroupLeader->bInCover || GroupLeader->bShimmying)
	{
		return; // the leader still on his dash / side-stepping: his look is not decided yet (the group stays dirty)
	}
	WallGroup.bDirty = false;
	const FFormationConfig& Config = FormationTuning::Get();
	const float FeetDrop = GroupLeader->GetSimpleCollisionHalfHeight();
	TArray<FWallGroupMember> Places;
	TArray<AOperativeCharacter*> Who;
	TArray<FVector> ProbeAt;
	FWallGroupMember Lead;
	Lead.Role = EFormationRole::Commander;
	Lead.Pos = FVector2D(GroupLeader->GetCoverSlot().WorldLocation);
	Lead.FixedYaw = static_cast<float>(GroupLeader->GetSightFacing().Rotation().Yaw); // his own cover facing / corner aim stays his
	Places.Add(Lead);
	Who.Add(GroupLeader);
	ProbeAt.Add(FVector(Lead.Pos, GroupLeader->GetActorLocation().Z - FeetDrop));
	static const EFormationRole Labels[] = { EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear };
	for (const TWeakObjectPtr<AOperativeCharacter>& Weak : WallGroup.Members)
	{
		AOperativeCharacter* Member = Weak.Get();
		if (!Member || Places.Num() > 3) // K + three labels
		{
			continue;
		}
		FWallGroupMember Place;
		Place.Role = Labels[Places.Num() - 1];
		const FGroupCoverWaitOrder* Wait = GroupCoverWaitOrders.Find(Weak);
		const FGroupCoverProneOrder* Prone = GroupCoverProneOrders.Find(Weak);
		if (WallGroup.WallPose.Contains(Weak) && Member->IsInFormationWallPose())
		{
			Place.Pos = FVector2D(Member->GetActorLocation());
			if (Member->HasOwnFacing() || Member->IsFacingCombatTarget())
			{
				Place.FixedYaw = static_cast<float>(Member->GetActorRotation().Yaw);
			}
		}
		else if (Member->bInCover || Member->HasPendingCover() || Wait)
		{
			const FCoverSlot& Slot = Member->bInCover ? Member->GetCoverSlot() : Member->HasPendingCover() ? Member->GetPendingCoverSlot() : Wait->Slot;
			Place.Pos = FVector2D(Slot.WorldLocation);
			Place.bCoverPose = true;
			Place.WallNormalYaw = static_cast<float>(FormationMath::YawOfD(FVector2D(Slot.WallNormal)));
		}
		else if (Prone)
		{
			Place.Pos = FVector2D(Prone->Spot);
		}
		else
		{
			continue;
		}
		Places.Add(Place);
		Who.Add(Member);
		ProbeAt.Add(FVector(Place.Pos, Member->GetActorLocation().Z - Member->GetSimpleCollisionHalfHeight()));
	}
	const TArray<FFormationWall> Walls = TraceLookWalls(ProbeAt, WallGroupProbeHeightCm, WallGroupProbeRays, Config.LookWallDist);
	const TArray<FWallGroupLookResult> Result = FormationWallGroup::Allocate(Walls, Places, Config);
	++WallGroup.Allocations;
	bool bChanged = false;
	FString Line;
	for (const FWallGroupLookResult& Look : Result)
	{
		const int32 Index = Places.IndexOfByPredicate([&Look](const FWallGroupMember& Place) { return Place.Role == Look.Role; });
		if (!Who.IsValidIndex(Index))
		{
			continue;
		}
		AOperativeCharacter* Member = Who[Index];
		Line += FString::Printf(TEXT(" %s %s %.0f;"), *Member->DisplayName.ToString(), WallGroupKindName(Look.Kind), Look.Yaw);
		if (Index == 0)
		{
			continue; // the leader's look is his own
		}
		const FWallGroupLookResult* Old = WallGroup.Looks.Find(Member);
		bChanged |= !Old || Old->Kind != Look.Kind || FormationMath::AngleBetween(Old->Yaw, Look.Yaw) > WallGroupLogDeg;
		WallGroup.Looks.Add(Member, Look);
		if (Look.Kind == EWallGroupLook::AlongWall && Member->bInCover)
		{
			const FCoverSlot& Slot = Member->GetCoverSlot();
			const float LeftOff = FormationMath::AngleBetween(static_cast<float>(CoverFacingRules::AlongWallDirection(Slot, ECoverFacing::Left).Rotation().Yaw), Look.Yaw);
			const float RightOff = FormationMath::AngleBetween(static_cast<float>(CoverFacingRules::AlongWallDirection(Slot, ECoverFacing::Right).Rotation().Yaw), Look.Yaw);
			Member->SetCoverLookSide(LeftOff <= RightOff ? ECoverFacing::Left : ECoverFacing::Right);
		}
		else if (Look.Kind == EWallGroupLook::FacingOut && Member->bInCover)
		{
			Member->SetCoverLookSide(TOptional<ECoverFacing>());
			if (Member->LeaveCoverForWallPose())
			{
				WallGroup.WallPose.AddUnique(Member);
				UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] %s: out of the cover slot - back to the wall, facing out (yaw %.0f)"),
					*Member->DisplayName.ToString(), Look.Yaw);
			}
			else
			{
				WallGroup.bDirty = true; // busy (still settling): again next tick
			}
		}
		else if (FGroupCoverProneOrder* Prone = GroupCoverProneOrders.Find(Member))
		{
			Prone->FaceYaw = Look.Yaw; // he lies down facing it (the Facing phase), or turns to it lying there
			if (Prone->Phase == ESquadGroupCoverPronePhase::Holding)
			{
				Member->SetIdleFacingYaw(Look.Yaw);
			}
		}
	}
	if (bChanged)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WallGroup] allocation %d (%s, %d walls probed):%s"), WallGroup.Allocations, *WallGroup.Why, Walls.Num(), *Line);
	}
}

bool USquadSubsystem::HoldsWallGroupPose(FFollowerState& Follower, AOperativeCharacter& Operative)
{
	if (!WallGroup.bActive || !WallGroup.WallPose.Contains(&Operative) || !Operative.IsInFormationWallPose())
	{
		return false;
	}
	if (Operative.IsMoving() && !Operative.HasOwnMoveOrder())
	{
		Operative.StopOperative();
	}
	Follower.bParked = true;
	Follower.RepathTimeRemaining = 0.f;
	if (const FWallGroupLookResult* Look = WallGroup.Looks.Find(&Operative))
	{
		FrameLooks.Add(&Operative, Look->Yaw);
	}
	return true;
}
