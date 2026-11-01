// USquadSubsystem: the custom formation in the runtime (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md "Custom formation",
// contract docs/design/FORMATION_CONTRACT.md 17). Godot reference: none (UE-only); the reference simulation is Tools/TrainingGround/src/movement/sim.ts
// (requested 'custom'). The three stored shapes (the placement mode stores them, the save game / campaign carry keep them), the per-tick slots and
// fixed looks of the shape in the leader's frame (FormationCustomRules::CustomSlots), and its guard: a wall cutting the shape or a man cut off folds
// the squad into the trail column (the v2.1 guard's level 2), which opens back into the shape after classify.hold_distance of path.

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationTuning.h"

const FFormationCustomShape& USquadSubsystem::GetFormationCustomShape(int32 Slot) const
{
	static const FFormationCustomShape Empty;
	return CustomShapes.IsValidIndex(Slot - 1) ? CustomShapes[Slot - 1] : Empty;
}

void USquadSubsystem::SetFormationCustomShape(int32 Slot, const FFormationCustomShape& Shape)
{
	if (Slot < 1 || Slot > FormationCustomRules::SlotCount)
	{
		return;
	}
	if (CustomShapes.Num() < FormationCustomRules::SlotCount)
	{
		CustomShapes.SetNum(FormationCustomRules::SlotCount);
	}
	CustomShapes[Slot - 1] = FormationCustomRules::RoundShape(Shape);
	FString Line;
	for (const FFormationCustomMember& Member : CustomShapes[Slot - 1].Members)
	{
		Line += FString::Printf(TEXT(" [#%d fwd %.0f right %.0f look %.0f]"), Member.Squad, Member.Fwd, Member.Right, Member.Look);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] custom slot %d stored: %d member(s)%s"), Slot, CustomShapes[Slot - 1].Members.Num(), *Line);
}

TArray<int32> USquadSubsystem::GetFormationCustomPresent() const
{
	TArray<int32> Present;
	const AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef)
	{
		return Present;
	}
	Present.Add(LeaderRef->SquadIndex);
	TArray<int32> Others;
	for (const FFollowerState& Follower : Followers)
	{
		if (const AOperativeCharacter* Operative = Follower.Operative.Get())
		{
			Others.AddUnique(Operative->SquadIndex);
		}
	}
	Others.Remove(LeaderRef->SquadIndex);
	Others.Sort();
	Present.Append(Others);
	return Present;
}

TArray<FString> USquadSubsystem::ExportFormationCustomSlots() const
{
	TArray<FString> Out;
	for (int32 Slot = 1; Slot <= FormationCustomRules::SlotCount; ++Slot)
	{
		Out.Add(FormationCustomRules::ToJsonString(GetFormationCustomShape(Slot)));
	}
	return Out;
}

void USquadSubsystem::ImportFormationCustomSlots(const TArray<FString>& Slots)
{
	CustomShapes.Reset();
	CustomShapes.SetNum(FormationCustomRules::SlotCount);
	for (int32 Slot = 1; Slot <= FormationCustomRules::SlotCount; ++Slot)
	{
		CustomShapes[Slot - 1] = Slots.IsValidIndex(Slot - 1) ? FormationCustomRules::FromJsonString(Slots[Slot - 1]) : FFormationCustomShape();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] custom slots restored: %d / %d / %d member(s)"), CustomShapes[0].Members.Num(), CustomShapes[1].Members.Num(),
		CustomShapes[2].Members.Num());
}

void USquadSubsystem::StepFormationCustomGuard(const AOperativeCharacter& LeaderRef, bool bLeaderMoving)
{
	// As StepFormationGuard (rules v2.1 / v2.3 part 6) for the custom shape (contract 17.4): its slots at the leader and at his pose
	// guard.lookahead ahead, every member in sight of the leader; level 0 = the shape, level 2 = the trail column.
	const FFormationConfig& Config = FormationTuning::Get();
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	const TArray<FVector2D> Path = LeaderPathAhead(LeaderRef, bLeaderMoving);
	TOptional<FFormationPose> Ahead;
	if (Path.Num() > 1)
	{
		Ahead = FormationGuardRuntime::AheadPose(Path, Sectors.HeadingYaw, Config.GuardLookahead);
	}
	TArray<FVector2D> InFormation;
	for (const FFollowerState& Follower : Followers)
	{
		const AOperativeCharacter* Operative = Follower.Operative.Get();
		if (Operative && !IsOnOwnOrder(Operative) && !Operative->bInCover)
		{
			InFormation.Add(FVector2D(Operative->GetActorLocation()));
		}
	}
	const bool bAllInSight = FormationGuardRuntime::AllInSight(Sectors.WorldWalls, LeaderPos, InFormation);
	const FFormationCustomShape& Shape = GetFormationCustomShape(Sectors.CustomSlot);
	const TArray<int32> Present = GetFormationCustomPresent();
	const FFormationGuardState Before = Sectors.Guard;
	Sectors.Guard = FormationCustomRules::Resolve(Sectors.WorldWalls, Shape, Present, LeaderPos, Sectors.HeadingYaw, Ahead, Sectors.Guard, Sectors.LastStepDistance,
		Config, bAllInSight);
	const FFormationGuardEffective Effective = FormationGuardRules::Effective(Sectors.Guard);
	Sectors.Formation = Effective.Formation.Get(EFormationKind::Custom);
	Sectors.bTrail = Effective.bTrail;
	if (Before.Level != Sectors.Guard.Level || Before.Requested != Sectors.Guard.Requested)
	{
		const auto Cut = [&](const FVector2D& Pos, float Yaw)
		{
			FString Roles;
			for (const EFormationRole Role : FormationCustomRules::BlockedRoles(Sectors.WorldWalls, FormationCustomRules::CustomSlots(Shape, Present, Pos, Yaw, Config), Pos,
				Config))
			{
				Roles += FormationNames::ToString(Role);
			}
			return Roles.IsEmpty() ? FString(TEXT("-")) : Roles;
		};
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] custom guard: %s (level %d%s, cut here %s / ahead %s, heading %.0f, %d walls) at (%.0f, %.0f)"),
			*GetFormationHudText(), static_cast<int32>(Sectors.Guard.Level), bAllInSight ? TEXT("") : TEXT(", a member cut off"), *Cut(LeaderPos, Sectors.HeadingYaw),
			Ahead.IsSet() ? *Cut(Ahead->Pos, Ahead->Yaw) : TEXT("(standing)"), Sectors.HeadingYaw, Sectors.WorldWalls.Num(), LeaderPos.X, LeaderPos.Y);
	}
}

void USquadSubsystem::BuildCustomFrameSlots(const AOperativeCharacter& LeaderRef, const FVector2D& LeaderPos)
{
	const TArray<FFormationCustomSlot> Slots = FormationCustomRules::CustomSlots(GetFormationCustomShape(Sectors.CustomSlot), GetFormationCustomPresent(), LeaderPos,
		Sectors.HeadingYaw, FormationTuning::Get());
	Sectors.FrameSlots.Reset();
	Sectors.CustomRoles.Reset();
	for (const FFormationCustomSlot& Slot : Slots)
	{
		FFormationSlot& Frame = Sectors.FrameSlots.AddDefaulted_GetRef();
		Frame.Role = Slot.Role;
		Frame.Position = Slot.Position;
		Frame.Yaw = Slot.Yaw;
		if (Slot.Squad == LeaderRef.SquadIndex)
		{
			continue; // the leader is K (GetFormationRole)
		}
		for (const FFollowerState& Follower : Followers)
		{
			if (Follower.Operative.IsValid() && Follower.Operative->SquadIndex == Slot.Squad)
			{
				Sectors.CustomRoles.Add(Follower.Operative, Slot.Role);
				break;
			}
		}
	}
}
