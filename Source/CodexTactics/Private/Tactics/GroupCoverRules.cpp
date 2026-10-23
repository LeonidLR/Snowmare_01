#include "Tactics/GroupCoverRules.h"

// UE-only (game designer 2026-10-20, the group cover order); see the header.

namespace GroupCoverRules
{
	float ProbeOffsetCm(int32 Side, int32 K, const FGroupCoverConfig& Config)
	{
		return (Side < 0 ? -1.f : 1.f) * static_cast<float>(FMath::Max(K, 1)) * Config.SlotSpacingCm;
	}

	int32 ProbesPerSide(int32 Others, const FGroupCoverConfig& Config)
	{
		return FMath::Max(0, Others) + FMath::Max(0, Config.ExtraProbesPerSide);
	}

	EGroupCoverProbe ClassifyProbe(bool bSlotFound, bool bSameWall, float AlongErrorCm, bool bOuterEdgeExposed, float OuterEdgeCm, bool bOccupied,
		const FGroupCoverConfig& Config)
	{
		if (!bSlotFound || !bSameWall || FMath::Abs(AlongErrorCm) > Config.AlongToleranceCm)
		{
			return EGroupCoverProbe::NoWall;
		}
		if (bOuterEdgeExposed && OuterEdgeCm < Config.MinEdgeClearanceCm)
		{
			return EGroupCoverProbe::NoWall; // his body would stick out past the end
		}
		return bOccupied ? EGroupCoverProbe::Occupied : EGroupCoverProbe::Free;
	}

	TArray<float> CollectFreeOffsets(const TArray<EGroupCoverProbe>& LeftProbes, const TArray<EGroupCoverProbe>& RightProbes, const FGroupCoverConfig& Config)
	{
		TArray<float> Free;
		for (int32 Side : { -1, 1 })
		{
			const TArray<EGroupCoverProbe>& Probes = Side < 0 ? LeftProbes : RightProbes;
			for (int32 Index = 0; Index < Probes.Num(); ++Index)
			{
				if (Probes[Index] == EGroupCoverProbe::NoWall)
				{
					break; // the cover ends here (nobody is sent past a gap)
				}
				if (Probes[Index] == EGroupCoverProbe::Free)
				{
					Free.Add(ProbeOffsetCm(Side, Index + 1, Config));
				}
			}
		}
		return Free;
	}

	TArray<float> ChooseNearestOffsets(const TArray<float>& FreeOffsets, int32 Count, float PreferSign)
	{
		TArray<float> Sorted = FreeOffsets;
		const float Prefer = PreferSign < 0.f ? -1.f : 1.f;
		Sorted.Sort([Prefer](float A, float B)
		{
			const float DistA = FMath::Abs(A);
			const float DistB = FMath::Abs(B);
			if (!FMath::IsNearlyEqual(DistA, DistB, 0.5f))
			{
				return DistA < DistB;
			}
			return A * Prefer > B * Prefer; // a tie: the preferred side first
		});
		if (Sorted.Num() > FMath::Max(Count, 0))
		{
			Sorted.SetNum(FMath::Max(Count, 0));
		}
		Sorted.Sort();
		return Sorted;
	}

	FVector2D ComputeCoverSpan(bool bLeftExposed, float LeftEdgeCm, bool bRightExposed, float RightEdgeCm, float FarthestLeftWallCm,
		float FarthestRightWallCm, const FGroupCoverConfig& Config)
	{
		const float HalfSpacing = 0.5f * Config.SlotSpacingCm;
		const float Min = bLeftExposed ? -FMath::Max(LeftEdgeCm, 0.f)
			: FMath::Min(-Config.UnknownReachCm, FMath::Min(FarthestLeftWallCm, 0.f) - HalfSpacing);
		const float Max = bRightExposed ? FMath::Max(RightEdgeCm, 0.f)
			: FMath::Max(Config.UnknownReachCm, FMath::Max(FarthestRightWallCm, 0.f) + HalfSpacing);
		return FVector2D(Min, Max);
	}

	TArray<FVector2D> ComputeProneSpots(int32 Count, const FVector2D& Span, int32 Spare, const FGroupCoverConfig& Config)
	{
		TArray<FVector2D> Spots;
		if (Count <= 0 && Spare <= 0)
		{
			return Spots;
		}
		const int32 RowCount = FMath::Max(Count, 0);
		const float Width = FMath::Max(RowCount - 1, 0) * Config.ProneSpacingCm;
		const float Lo = Span.X + Config.ProneEdgeMarginCm;
		const float Hi = Span.Y - Config.ProneEdgeMarginCm;
		// Directly behind the clicked slot when the row fits inside the cover there; a cover narrower than the row: its middle.
		const float Centre = Hi - Lo >= Width ? FMath::Clamp(0.f, Lo + 0.5f * Width, Hi - 0.5f * Width) : 0.5f * (Span.X + Span.Y);
		const float First = Centre - 0.5f * Width;
		for (int32 Index = 0; Index < RowCount; ++Index)
		{
			Spots.Add(FVector2D(First + Index * Config.ProneSpacingCm, Config.ProneDepthCm));
		}
		// Spares alternately past the row's ends, the roomier side first.
		const float RowMin = First;
		const float RowMax = First + Width;
		const bool bRightFirst = (Hi - RowMax) >= (RowMin - Lo);
		int32 RightStep = RowCount > 0 ? 1 : 0;
		int32 LeftStep = 1;
		for (int32 Index = 0; Index < Spare; ++Index)
		{
			const bool bRight = (Index % 2 == 0) == bRightFirst;
			if (bRight)
			{
				Spots.Add(FVector2D(RowMax + RightStep++ * Config.ProneSpacingCm, Config.ProneDepthCm));
			}
			else
			{
				Spots.Add(FVector2D(RowMin - LeftStep++ * Config.ProneSpacingCm, Config.ProneDepthCm));
			}
		}
		return Spots;
	}

	namespace
	{
		struct FGroupCoverSearch
		{
			const TArray<FVector2D>* Members = nullptr;
			const TArray<FVector2D>* Targets = nullptr;
			int32 ToAssign = 0;
			TArray<int32> Current;
			TArray<int32> Best;
			TArray<bool> Used;
			double BestCost = TNumericLimits<double>::Max();
		};

		void GroupCoverSearchFrom(FGroupCoverSearch& Search, int32 Member, int32 Assigned, double Cost)
		{
			if (Cost >= Search.BestCost - 1.0e-3)
			{
				return; // no better than the best so far (ties keep the earlier = lower indices)
			}
			const int32 MemberCount = Search.Members->Num();
			if (Member == MemberCount)
			{
				if (Assigned == Search.ToAssign)
				{
					Search.BestCost = Cost;
					Search.Best = Search.Current;
				}
				return;
			}
			// Members left must still be able to take the targets left.
			if (Search.ToAssign - Assigned > MemberCount - Member)
			{
				return;
			}
			for (int32 Target = 0; Target < Search.Targets->Num(); ++Target)
			{
				if (Search.Used[Target] || Assigned >= Search.ToAssign)
				{
					continue;
				}
				Search.Used[Target] = true;
				Search.Current[Member] = Target;
				GroupCoverSearchFrom(Search, Member + 1, Assigned + 1, Cost + FVector2D::Distance((*Search.Members)[Member], (*Search.Targets)[Target]));
				Search.Used[Target] = false;
			}
			if (Search.ToAssign - Assigned < MemberCount - Member) // he may stay without a target
			{
				Search.Current[Member] = INDEX_NONE;
				GroupCoverSearchFrom(Search, Member + 1, Assigned, Cost);
			}
		}
	}

	TArray<int32> AssignMinimalTravel(const TArray<FVector2D>& Members, const TArray<FVector2D>& Targets)
	{
		TArray<int32> Result;
		Result.Init(INDEX_NONE, Members.Num());
		const int32 ToAssign = FMath::Min(Members.Num(), Targets.Num());
		if (ToAssign == 0)
		{
			return Result;
		}
		if (Members.Num() <= 8 && Targets.Num() <= 8)
		{
			FGroupCoverSearch Search;
			Search.Members = &Members;
			Search.Targets = &Targets;
			Search.ToAssign = ToAssign;
			Search.Current.Init(INDEX_NONE, Members.Num());
			Search.Used.Init(false, Targets.Num());
			GroupCoverSearchFrom(Search, 0, 0, 0.0);
			return Search.Best.Num() == Members.Num() ? Search.Best : Result;
		}
		// Large groups: the globally nearest free pair, again and again.
		TArray<bool> TargetUsed;
		TargetUsed.Init(false, Targets.Num());
		for (int32 Round = 0; Round < ToAssign; ++Round)
		{
			double Best = TNumericLimits<double>::Max();
			int32 BestMember = INDEX_NONE;
			int32 BestTarget = INDEX_NONE;
			for (int32 Member = 0; Member < Members.Num(); ++Member)
			{
				if (Result[Member] != INDEX_NONE)
				{
					continue;
				}
				for (int32 Target = 0; Target < Targets.Num(); ++Target)
				{
					if (TargetUsed[Target])
					{
						continue;
					}
					const double Distance = FVector2D::Distance(Members[Member], Targets[Target]);
					if (Distance < Best)
					{
						Best = Distance;
						BestMember = Member;
						BestTarget = Target;
					}
				}
			}
			if (BestMember == INDEX_NONE)
			{
				break;
			}
			Result[BestMember] = BestTarget;
			TargetUsed[BestTarget] = true;
		}
		return Result;
	}

	TArray<FGroupCoverAssignment> PlanAssignments(const TArray<FVector2D>& Members, const TArray<FVector2D>& SlotPoints, const TArray<FVector2D>& PronePoints)
	{
		const int32 SlotCount = FMath::Min(Members.Num(), SlotPoints.Num());
		const int32 ProneCount = FMath::Min(Members.Num() - SlotCount, PronePoints.Num());
		TArray<FVector2D> Targets;
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			Targets.Add(SlotPoints[Index]);
		}
		for (int32 Index = 0; Index < ProneCount; ++Index)
		{
			Targets.Add(PronePoints[Index]);
		}
		const TArray<int32> Chosen = AssignMinimalTravel(Members, Targets);
		TArray<FGroupCoverAssignment> Plan;
		Plan.SetNum(Members.Num());
		for (int32 Member = 0; Member < Members.Num(); ++Member)
		{
			const int32 Target = Chosen[Member];
			if (Target == INDEX_NONE)
			{
				Plan[Member].Kind = EGroupCoverSpotKind::Prone;
				Plan[Member].Index = INDEX_NONE;
			}
			else if (Target < SlotCount)
			{
				Plan[Member].Kind = EGroupCoverSpotKind::Slot;
				Plan[Member].Index = Target;
			}
			else
			{
				Plan[Member].Kind = EGroupCoverSpotKind::Prone;
				Plan[Member].Index = Target - SlotCount;
			}
		}
		return Plan;
	}

	bool ShouldReleaseToSlot(const FGroupCoverWaitFacts& Facts, const FGroupCoverConfig& Config)
	{
		if (Facts.bLeaderInCover || Facts.WaitSeconds >= Config.MaxWaitSeconds)
		{
			return true;
		}
		const bool bLeaderLost = Facts.bLeaderDown || !Facts.bLeaderGoingToCover;
		return bLeaderLost && Facts.LeaderIdleSeconds >= Config.LeaderFailGraceSeconds;
	}

	FVector ComputeWaitPoint(const FVector& SlotLocation, const FVector& WallNormal, const FGroupCoverConfig& Config)
	{
		return SlotLocation + WallNormal.GetSafeNormal2D() * Config.WaitStandOffCm;
	}

	int32 PickFreeSlot(const TArray<FVector2D>& Candidates, const TArray<bool>& Taken, const FVector2D& Preferred)
	{
		int32 Best = INDEX_NONE;
		double BestDistance = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Taken.IsValidIndex(Index) && Taken[Index])
			{
				continue;
			}
			const double Distance = FVector2D::DistSquared(Candidates[Index], Preferred);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
		return Best;
	}
}
