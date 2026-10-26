#include "Formation/FormationGuardRules.h"

#include "Formation/FormationShapeRules.h"

namespace FormationGuardRules
{
	using ER = EFormationRole;

	TArray<TPair<EFormationRole, EFormationRole>> ChainPairs(EFormationKind Formation, int32 MemberCount)
	{
		TArray<TPair<EFormationRole, EFormationRole>> Pairs;
		if (MemberCount <= 2)
		{
			return Pairs;
		}
		const TArray<EFormationRole> Have = FormationShapeRules::Roles(MemberCount);
		const auto Chain = [&Have, &Pairs](std::initializer_list<EFormationRole> Order, bool bRing)
		{
			TArray<EFormationRole> Present;
			for (const EFormationRole Role : Order)
			{
				if (Have.Contains(Role))
				{
					Present.Add(Role);
				}
			}
			for (int32 Index = 1; Index < Present.Num(); ++Index)
			{
				Pairs.Emplace(Present[Index - 1], Present[Index]);
			}
			if (bRing && Present.Num() > 2)
			{
				Pairs.Emplace(Present.Last(), Present[0]);
			}
		};
		switch (Formation)
		{
		case EFormationKind::EchelonRight: Chain({ ER::Commander, ER::Left, ER::Right, ER::Rear }, false); break;
		case EFormationKind::EchelonLeft:
		case EFormationKind::Column: Chain({ ER::Commander, ER::Right, ER::Left, ER::Rear }, false); break;
		case EFormationKind::Line:
			// v2.2: the line of three runs Z, K, P (Z on the left).
			if (MemberCount == 3)
			{
				Chain({ ER::Rear, ER::Commander, ER::Right }, false);
			}
			else
			{
				Chain({ ER::Left, ER::Commander, ER::Right, ER::Rear }, false);
			}
			break;
		case EFormationKind::Hedgehog: Chain({ ER::Commander, ER::Right, ER::Rear, ER::Left }, true); break;
		case EFormationKind::Diamond:
		case EFormationKind::Wedge:
		case EFormationKind::Blizzard:
			Pairs.Emplace(ER::Right, ER::Rear);
			if (MemberCount >= 4)
			{
				Pairs.Emplace(ER::Left, ER::Rear);
			}
			break;
		default:
			break;
		}
		return Pairs;
	}

	TArray<EFormationRole> BlockedRoles(const TArray<FFormationWall>& Walls, EFormationKind Formation, int32 MemberCount, const FVector2D& LeaderPos,
		float LeaderYaw, const FFormationConfig& Config)
	{
		TArray<EFormationRole> Result;
		if (Formation == EFormationKind::Wall)
		{
			return Result;
		}
		const double Radius = Config.GuardClearance;
		const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(Formation, MemberCount, LeaderPos, LeaderYaw, Config);
		const auto PosOf = [&Slots](EFormationRole Role)
		{
			const FFormationSlot* Slot = Slots.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
			return Slot ? Slot->Position : FVector2D::ZeroVector;
		};
		TArray<EFormationRole> Blocked;
		for (int32 Index = 1; Index < Slots.Num(); ++Index)
		{
			if (FormationMath::SegmentBlocked(LeaderPos, Slots[Index].Position, Walls, Radius))
			{
				Blocked.AddUnique(Slots[Index].Role);
			}
		}
		for (const TPair<EFormationRole, EFormationRole>& Pair : ChainPairs(Formation, MemberCount))
		{
			if (!FormationMath::SegmentBlocked(PosOf(Pair.Key), PosOf(Pair.Value), Walls, Radius))
			{
				continue;
			}
			if (Pair.Key != ER::Commander)
			{
				Blocked.AddUnique(Pair.Key);
			}
			if (Pair.Value != ER::Commander)
			{
				Blocked.AddUnique(Pair.Value);
			}
		}
		for (const FFormationSlot& Slot : Slots)
		{
			if (Blocked.Contains(Slot.Role))
			{
				Result.Add(Slot.Role);
			}
		}
		return Result;
	}

	float TurnAhead(const TArray<FVector2D>& Path, float Heading, const FFormationConfig& Config)
	{
		const double Look = Config.TurnLookahead;
		double Walked = 0.0;
		double Worst = 0.0;
		for (int32 Index = 1; Index < Path.Num() && Walked <= Look; ++Index)
		{
			const double Length = FVector2D::Distance(Path[Index - 1], Path[Index]);
			if (Length < 1e-6)
			{
				continue;
			}
			Worst = FMath::Max(Worst, FMath::Abs(FormationMath::NormYaw(FormationMath::YawOfD(Path[Index] - Path[Index - 1]) - Heading)));
			Walked += Length;
		}
		return static_cast<float>(Worst);
	}

	static TOptional<EFormationKind> GuardMirrorOf(EFormationKind Formation)
	{
		if (Formation == EFormationKind::EchelonRight)
		{
			return EFormationKind::EchelonLeft;
		}
		if (Formation == EFormationKind::EchelonLeft)
		{
			return EFormationKind::EchelonRight;
		}
		return TOptional<EFormationKind>();
	}

	static EFormationKind GuardFormationAt(EFormationKind Requested, EFormationGuardLevel Level)
	{
		switch (Level)
		{
		case EFormationGuardLevel::Requested: return Requested;
		case EFormationGuardLevel::Mirror: return GuardMirrorOf(Requested).Get(EFormationKind::Column);
		default: return EFormationKind::Column;
		}
	}

	static bool GuardIsWide(EFormationKind Formation)
	{
		return Formation == EFormationKind::EchelonRight || Formation == EFormationKind::EchelonLeft || Formation == EFormationKind::Line;
	}

	FFormationGuardState Resolve(const TArray<FFormationWall>& Walls, EFormationKind Requested, int32 MemberCount, const FVector2D& LeaderPos,
		float LeaderYaw, const TOptional<FFormationPose>& Ahead, float TurnAheadDeg, const FFormationGuardState& State, float StepDistance,
		const FFormationConfig& Config, bool bAllInSight)
	{
		const auto Whole = [&](EFormationKind Formation)
		{
			return BlockedRoles(Walls, Formation, MemberCount, LeaderPos, LeaderYaw, Config).Num() == 0
				&& (!Ahead.IsSet() || BlockedRoles(Walls, Formation, MemberCount, Ahead->Pos, Ahead->Yaw, Config).Num() == 0);
		};
		EFormationGuardLevel Raw = EFormationGuardLevel::Requested;
		TOptional<EFormationGuardCause> Cause;
		// A sharp turn ahead folds a wide formation into the trail column (no long swing of its far end); v2.3 part 9: a wall cutting the
		// formation at the same time names the cause Wall.
		const bool bTurning = GuardIsWide(Requested) && TurnAheadDeg > Config.TurnFoldDeg;
		if (Requested != EFormationKind::Wall && !Whole(Requested))
		{
			const TOptional<EFormationKind> Mirror = GuardMirrorOf(Requested);
			Raw = (!bTurning && Mirror.IsSet() && Whole(Mirror.GetValue())) ? EFormationGuardLevel::Mirror : EFormationGuardLevel::Trail;
			Cause = EFormationGuardCause::Wall;
		}
		else if (bTurning)
		{
			Raw = EFormationGuardLevel::Trail;
			Cause = EFormationGuardCause::Turn;
		}
		// v2.3 part 6: a member already cut off from the leader -> the trail column at once (it leads him round the corner on the leader's path).
		if (Requested != EFormationKind::Wall && !bAllInSight)
		{
			Raw = EFormationGuardLevel::Trail;
			Cause = EFormationGuardCause::Wall;
		}
		const auto At = [Requested](EFormationGuardLevel Level, const TOptional<EFormationGuardCause>& Why, const TOptional<EFormationGuardLevel>& Candidate,
			float Distance)
		{
			FFormationGuardState Out;
			Out.Requested = Requested;
			Out.Level = Level;
			Out.Effective = GuardFormationAt(Requested, Level);
			Out.Candidate = Candidate;
			Out.CandidateDistance = Distance;
			if (Level != EFormationGuardLevel::Requested)
			{
				Out.Cause = Why;
			}
			return Out;
		};
		const TOptional<EFormationGuardLevel> NoCandidate;
		if (!State.Requested.IsSet() || State.Requested.GetValue() != Requested || static_cast<uint8>(Raw) >= static_cast<uint8>(State.Level))
		{
			return At(Raw, Cause, NoCandidate, 0.f);
		}
		if (State.Level != EFormationGuardLevel::Trail && !Whole(GuardFormationAt(Requested, State.Level)))
		{
			return At(Raw, Cause, NoCandidate, 0.f);
		}
		const bool bSameCandidate = State.Candidate.IsSet() && State.Candidate.GetValue() == Raw;
		const float Walked = (bSameCandidate ? State.CandidateDistance : 0.f) + FMath::Max(StepDistance, 0.f);
		if (Walked >= Config.ClassifyHoldDistance)
		{
			return At(Raw, Cause, NoCandidate, 0.f);
		}
		return At(State.Level, State.Cause, Raw, Walked);
	}

	FFormationGuardEffective Effective(const FFormationGuardState& State)
	{
		FFormationGuardEffective Out;
		Out.Formation = State.Effective;
		Out.bTrail = State.Level == EFormationGuardLevel::Trail;
		return Out;
	}
}
