#include "Formation/FormationGuardRuntimeRules.h"

#include "Formation/FormationCornerRules.h"
#include "HAL/IConsoleManager.h"

namespace FormationGuardRuntime
{
	static TAutoConsoleVariable<int32> CVarFormationV21(TEXT("Codex.Formation.V21"), 1,
		TEXT("Rules v2.1 in the runtime (with Codex.Formation.Sectors 1; docs/design/FORMATION_CONTRACT.md 10): 1 (default) the wall guard (a formation a ")
		TEXT("wall cuts -> its mirror echelon, else the trail column along the leader's path; the squad never splits), the sharp-turn fold, the ")
		TEXT("bypass side of a free-standing wall by a known threat, the corner watch (slicing the pie) and ClearLook (nobody looks into a wall); ")
		TEXT("0: the v2.2 runtime without them (A/B)."));

	bool IsEnabled()
	{
		return CVarFormationV21.GetValueOnGameThread() != 0;
	}

	void PushTrail(TArray<FVector2D>& Trail, const FVector2D& Pos, float StepCm, float KeepCm)
	{
		if (Trail.Num() > 0 && FVector2D::Distance(Trail.Last(), Pos) < StepCm)
		{
			return;
		}
		Trail.Add(Pos);
		// Keep KeepCm of path counted back from the newest point (sim: the point where the kept length reaches KeepCm stays).
		double Kept = 0.0;
		int32 First = Trail.Num() - 1;
		while (First > 0 && Kept < KeepCm)
		{
			Kept += FVector2D::Distance(Trail[First - 1], Trail[First]);
			--First;
		}
		if (First > 0)
		{
			Trail.RemoveAt(0, First);
		}
	}

	TArray<FVector2D> TrailWithLeader(const TArray<FVector2D>& Trail, const FVector2D& LeaderPos)
	{
		TArray<FVector2D> Out = Trail;
		if (Out.Num() == 0 || FVector2D::Distance(Out.Last(), LeaderPos) > 1e-6)
		{
			Out.Add(LeaderPos);
		}
		return Out;
	}

	FFormationPose AheadPose(const TArray<FVector2D>& Path, float Heading, float Distance)
	{
		FFormationPose Pose;
		Pose.Yaw = Heading;
		if (Path.Num() == 0)
		{
			return Pose;
		}
		double Left = Distance;
		double Yaw = Heading;
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			const double Length = FVector2D::Distance(Path[Index - 1], Path[Index]);
			if (Length < 1e-6)
			{
				continue;
			}
			Yaw = FormationMath::YawOfD(Path[Index] - Path[Index - 1]);
			if (Left <= Length)
			{
				Pose.Pos = Path[Index - 1] + (Path[Index] - Path[Index - 1]) * (Left / Length);
				Pose.Yaw = static_cast<float>(Yaw);
				return Pose;
			}
			Left -= Length;
		}
		Pose.Pos = Path.Last();
		Pose.Yaw = static_cast<float>(Yaw);
		return Pose;
	}

	TArray<FVector2D> PathAheadFrom(const TArray<FVector2D>& Points, const FVector2D& LeaderPos)
	{
		TArray<FVector2D> Out;
		Out.Add(LeaderPos);
		if (Points.Num() < 2)
		{
			return Out;
		}
		int32 Piece = 1;
		double Best = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const double Distance = FVector2D::Distance(LeaderPos, FormationMath::ClosestOnSegment(LeaderPos, Points[Index - 1], Points[Index]));
			if (Distance <= Best + 1.0)
			{
				Best = FMath::Min(Best, Distance);
				Piece = Index;
			}
		}
		for (int32 Index = Piece; Index < Points.Num(); ++Index)
		{
			if (FVector2D::Distance(Out.Last(), Points[Index]) > 1.0)
			{
				Out.Add(Points[Index]);
			}
		}
		return Out;
	}

	void WallsFromBox(const FVector2D& Center, const FVector2D& HalfAxisA, const FVector2D& HalfAxisB, TArray<FFormationWall>& Out)
	{
		const double LengthA = HalfAxisA.Size();
		const double LengthB = HalfAxisB.Size();
		if (LengthA < 1.0 || LengthB < 1.0)
		{
			return;
		}
		const FVector2D& Long = LengthA >= LengthB ? HalfAxisA : HalfAxisB;
		const FVector2D& Short = LengthA >= LengthB ? HalfAxisB : HalfAxisA;
		const double HalfLong = FMath::Max(LengthA, LengthB);
		const double HalfShort = FMath::Min(LengthA, LengthB);
		if (HalfShort > BulkyHalfCm)
		{
			// Bulky: the four sides (thin), corner to corner - the corners are joints, no free ends to slice.
			const FVector2D Corners[4] = { Center + Long + Short, Center + Long - Short, Center - Long - Short, Center - Long + Short };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FFormationWall Side;
				Side.A = Corners[Index];
				Side.B = Corners[(Index + 1) % 4];
				Side.Thickness = 0.f;
				Out.Add(Side);
			}
			return;
		}
		const FVector2D Along = Long / HalfLong;
		const double Inset = FMath::Min(FMath::Max(0.0, HalfShort - LayoutCapCm), HalfLong);
		FFormationWall Wall;
		Wall.A = Center - Along * (HalfLong - Inset);
		Wall.B = Center + Along * (HalfLong - Inset);
		Wall.Thickness = static_cast<float>(2.0 * HalfShort);
		Out.Add(Wall);
	}

	bool IsFreeStanding(const TArray<FFormationWall>& Walls, int32 Index)
	{
		if (!Walls.IsValidIndex(Index))
		{
			return false;
		}
		const FFormationWall& Wall = Walls[Index];
		for (int32 Other = 0; Other < Walls.Num(); ++Other)
		{
			if (Other == Index)
			{
				continue;
			}
			const FFormationWall& OtherWall = Walls[Other];
			const double Reach = 0.5 * OtherWall.Thickness + 0.5 * Wall.Thickness + FormationCornerRules::CornerJoint;
			for (const FVector2D& End : { Wall.A, Wall.B })
			{
				if (FVector2D::Distance(End, FormationMath::ClosestOnSegment(End, OtherWall.A, OtherWall.B)) <= Reach)
				{
					return false;
				}
			}
		}
		return true;
	}

	TOptional<int32> FirstBlockingWall(const TArray<FFormationWall>& Walls, const FVector2D& From, const FVector2D& To, double Radius, bool bFreeStandingOnly)
	{
		TOptional<int32> Best;
		double BestAlong = TNumericLimits<double>::Max();
		const FVector2D Leg = To - From;
		for (int32 Index = 0; Index < Walls.Num(); ++Index)
		{
			const FFormationWall& Wall = Walls[Index];
			if (!FormationMath::SegmentBlocked(From, To, { Wall }, Radius))
			{
				continue;
			}
			if (bFreeStandingOnly && !IsFreeStanding(Walls, Index))
			{
				continue;
			}
			const double Along = FVector2D::DotProduct(FormationMath::ClosestOnSegment(From, Wall.A, Wall.B) - From, Leg);
			if (Along < BestAlong)
			{
				BestAlong = Along;
				Best = Index;
			}
		}
		return Best;
	}

	TArray<FVector2D> BypassWaypoints(const FFormationBypassChoice& Choice)
	{
		TArray<FVector2D> Out;
		for (int32 Index = 1; Index + 1 < Choice.Path.Num(); ++Index)
		{
			Out.Add(Choice.Path[Index]);
		}
		return Out;
	}

	int32 AdvanceWaypoint(const TArray<FVector2D>& Waypoints, int32 Index, const FVector2D& Pos, float HandoverCm)
	{
		int32 Next = FMath::Max(Index, 0);
		while (Next < Waypoints.Num() && FVector2D::Distance(Waypoints[Next], Pos) <= HandoverCm)
		{
			++Next;
		}
		return Next;
	}

	FString FormationLabel(EFormationKind Formation, int32 MemberCount)
	{
		if ((Formation == EFormationKind::Diamond || Formation == EFormationKind::Wedge) && MemberCount == 2)
		{
			return TEXT("Pair");
		}
		switch (Formation)
		{
		case EFormationKind::Diamond: return TEXT("Diamond");
		case EFormationKind::Wedge: return TEXT("Wedge");
		case EFormationKind::Column: return TEXT("Column");
		case EFormationKind::Line: return TEXT("Line");
		case EFormationKind::EchelonLeft: return TEXT("Echelon left");
		case EFormationKind::EchelonRight: return TEXT("Echelon right");
		case EFormationKind::Wall: return TEXT("Wall");
		case EFormationKind::Hedgehog: return TEXT("Hedgehog");
		case EFormationKind::Blizzard: return TEXT("Blizzard diamond");
		default: return TEXT("Diamond");
		}
	}

	FString GuardHudText(EFormationKind Requested, EFormationKind Effective, EFormationGuardLevel Level, int32 MemberCount)
	{
		if (Level == EFormationGuardLevel::Requested || Requested == Effective)
		{
			return FormationLabel(Effective, MemberCount);
		}
		return FString::Printf(TEXT("%s -> %s (wall)"), *FormationLabel(Requested, MemberCount), *FormationLabel(Effective, MemberCount));
	}

	const TCHAR* BypassReasonText(EFormationBypassReason Reason)
	{
		switch (Reason)
		{
		case EFormationBypassReason::Shielded: return TEXT("shielded");
		case EFormationBypassReason::TooLong: return TEXT("too_long");
		default: return TEXT("shortest");
		}
	}

	const TCHAR* WallSideText(EFormationWallSide Side)
	{
		return Side == EFormationWallSide::Right ? TEXT("right") : TEXT("left");
	}

	bool ShouldClearLook(EFormationKind Formation, EFormationMode Mode, bool bCornerWatcher, bool bLeader, const TOptional<float>& LeaderTravelYaw,
		float LookYaw)
	{
		if (Formation == EFormationKind::Wall || Mode == EFormationMode::Run || Mode == EFormationMode::Wall || bCornerWatcher)
		{
			return false;
		}
		if (bLeader && LeaderTravelYaw.IsSet() && FormationMath::AngleBetween(LookYaw, LeaderTravelYaw.GetValue()) <= WalkLookDeg)
		{
			return false;
		}
		return true;
	}

	TArray<FString> SplitCuts(const TArray<FFormationWall>& Walls, EFormationKind Formation, bool bTrail, int32 MemberCount, const TArray<FSplitMember>& Members)
	{
		TArray<FString> Cuts;
		if (Members.Num() == 0)
		{
			return Cuts;
		}
		const FVector2D LeaderPos = Members[0].Pos;
		if (!bTrail)
		{
			for (int32 Index = 1; Index < Members.Num(); ++Index)
			{
				const FSplitMember& Member = Members[Index];
				if (FormationMath::SegmentBlocked(LeaderPos, Member.Slot, Walls, 0.0))
				{
					Cuts.Add(FString::Printf(TEXT("%s's slot"), FormationNames::ToString(Member.Place)));
				}
				else if (FormationMath::SegmentBlocked(LeaderPos, Member.Pos, Walls, 0.0))
				{
					Cuts.Add(FormationNames::ToString(Member.Place));
				}
			}
		}
		const auto FindPlace = [&Members](EFormationRole Place) { return Members.FindByPredicate([Place](const FSplitMember& Each) { return Each.Place == Place; }); };
		for (const TPair<EFormationRole, EFormationRole>& Pair : FormationGuardRules::ChainPairs(bTrail ? EFormationKind::Column : Formation, MemberCount))
		{
			const FSplitMember* A = FindPlace(Pair.Key);
			const FSplitMember* B = FindPlace(Pair.Value);
			if (!A || !B)
			{
				continue;
			}
			if (FormationMath::SegmentBlocked(A->Pos, B->Pos, Walls, 0.0))
			{
				Cuts.Add(FString::Printf(TEXT("%s-%s"), FormationNames::ToString(Pair.Key), FormationNames::ToString(Pair.Value)));
			}
			else if (bTrail && FormationMath::SegmentBlocked(A->Slot, B->Slot, Walls, 0.0))
			{
				Cuts.Add(FString::Printf(TEXT("%s-%s slots"), FormationNames::ToString(Pair.Key), FormationNames::ToString(Pair.Value)));
			}
		}
		return Cuts;
	}

	void CornerCheckPoints(const FFormationWall& Wall, int32 End, const FVector2D& LeaderPos, FVector2D& OutGraze, FVector2D& OutHidden)
	{
		const FVector2D& E = End == 0 ? Wall.A : Wall.B;
		const FVector2D& O = End == 0 ? Wall.B : Wall.A;
		const double Length = FVector2D::Distance(E, O);
		const FVector2D U = Length > 1e-6 ? (E - O) / Length : FVector2D(1.0, 0.0);
		const FVector2D Normal(-U.Y, U.X);
		const double Side = FVector2D::DotProduct(LeaderPos - E, Normal) >= 0.0 ? 1.0 : -1.0;
		OutGraze = E + U * (0.5 * Wall.Thickness + FormationCornerRules::CornerGraze);
		OutHidden = OutGraze + Normal * (-Side * CornerCheckPeekCm);
	}
}
