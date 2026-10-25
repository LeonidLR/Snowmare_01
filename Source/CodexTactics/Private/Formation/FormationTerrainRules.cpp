#include "Formation/FormationTerrainRules.h"

namespace FormationTerrainRules
{
	/** Probe directions off the heading, deg: side rays (forward-side, side, back-side) and the forward fan. */
	static const float TerrainSideRaysDeg[] = { 60.f, 90.f, 120.f };
	static const float TerrainAheadRaysDeg[] = { 0.f, -20.f, 20.f };

	static double TerrainCross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Distance along the ray to one wall (its faces / centre line), infinity when none (geometry.ts raySegment). */
	static double TerrainRaySegment(const FVector2D& Origin, const FVector2D& Dir, const FFormationWall& Wall)
	{
		const double Half = 0.5 * Wall.Thickness;
		const FVector2D Edge = Wall.B - Wall.A;
		const double Length = Edge.Size();
		if (Length == 0.0)
		{
			return TNumericLimits<double>::Max();
		}
		const FVector2D Normal(-Edge.Y / Length, Edge.X / Length);
		double Best = TNumericLimits<double>::Max();
		const double Offsets[2] = { Half, -Half };
		const int32 OffsetCount = Half > 0.0 ? 2 : 1;
		for (int32 Index = 0; Index < OffsetCount; ++Index)
		{
			const FVector2D A = Wall.A + Normal * (Half > 0.0 ? Offsets[Index] : 0.0);
			const double Denominator = TerrainCross2D(Dir, Edge);
			if (FMath::Abs(Denominator) < 1e-9)
			{
				continue;
			}
			const FVector2D ToA = A - Origin;
			const double T = TerrainCross2D(ToA, Edge) / Denominator;
			const double U = TerrainCross2D(ToA, Dir) / Denominator;
			if (T >= 0.0 && U >= 0.0 && U <= 1.0 && T < Best)
			{
				Best = T;
			}
		}
		if (Half > 0.0)
		{
			// Started inside the wall body: distance 0.
			const double LengthSquared = Length * Length;
			const double Along = FVector2D::DotProduct(Origin - Wall.A, Edge) / LengthSquared;
			const double Clamped = FMath::Clamp(Along, 0.0, 1.0);
			const FVector2D Closest = Wall.A + Edge * Clamped;
			if (Clamped > 0.0 && Clamped < 1.0 && FVector2D::Distance(Origin, Closest) < Half)
			{
				return 0.0;
			}
		}
		return Best;
	}

	TOptional<float> RayHit(const TArray<FFormationWall>& Walls, const FVector2D& Origin, const FVector2D& Dir, float MaxDistance)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FFormationWall& Wall : Walls)
		{
			Best = FMath::Min(Best, TerrainRaySegment(Origin, Dir, Wall));
		}
		return Best <= static_cast<double>(MaxDistance) ? TOptional<float>(static_cast<float>(Best)) : TOptional<float>();
	}

	bool IsAcrossHeading(const FFormationWall& Wall, float Yaw, float IgnorePerpDeg)
	{
		const float Along = FMath::Abs(FRotator::NormalizeAxis(FormationMath::YawOf(Wall.B - Wall.A) - Yaw));
		const float OffParallel = FMath::Min(Along, 180.f - Along);
		return OffParallel >= 90.f - IgnorePerpDeg;
	}

	TOptional<float> SideClearance(const TArray<FFormationWall>& AllWalls, const FVector2D& Pos, float Yaw, int32 Side, float Reach, float IgnorePerpDeg)
	{
		TArray<FFormationWall> Beside;
		if (IgnorePerpDeg > 0.f)
		{
			Beside.Reserve(AllWalls.Num());
			for (const FFormationWall& Wall : AllWalls)
			{
				if (!IsAcrossHeading(Wall, Yaw, IgnorePerpDeg))
				{
					Beside.Add(Wall);
				}
			}
		}
		const TArray<FFormationWall>& Walls = IgnorePerpDeg > 0.f ? Beside : AllWalls;
		TOptional<double> Best;
		for (const float RayDeg : TerrainSideRaysDeg)
		{
			const double Sine = FMath::Sin(FMath::DegreesToRadians(static_cast<double>(RayDeg)));
			const TOptional<float> Hit = RayHit(Walls, Pos, FormationMath::Forward(Yaw + static_cast<float>(Side) * RayDeg), static_cast<float>(Reach / Sine));
			if (Hit.IsSet())
			{
				const double Lateral = static_cast<double>(Hit.GetValue()) * Sine;
				Best = Best.IsSet() ? FMath::Min(Best.GetValue(), Lateral) : Lateral;
			}
		}
		return Best.IsSet() ? TOptional<float>(static_cast<float>(Best.GetValue())) : TOptional<float>();
	}

	TOptional<float> SideWidth(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, const FFormationConfig& Config)
	{
		const double Reach = Config.ClassifyProbeSide;
		const FVector2D Heading = FormationMath::DirOf(Yaw);
		double Right = TNumericLimits<double>::Max();
		double Left = TNumericLimits<double>::Max();
		for (const FFormationWall& Wall : Walls)
		{
			// A wall ACROSS the heading is ahead / behind, never a side wall (a look into its end is ClearLook's job).
			if (IsAcrossHeading(Wall, Yaw, Config.ClassifySideIgnorePerpDeg))
			{
				continue;
			}
			const FVector2D Closest = FormationMath::ClosestOnSegment(Pos, Wall.A, Wall.B);
			const double Distance = FMath::Max(0.0, FVector2D::Distance(Pos, Closest) - 0.5 * Wall.Thickness);
			if (Distance > Reach)
			{
				continue;
			}
			if (Heading.X * (Closest.Y - Pos.Y) - Heading.Y * (Closest.X - Pos.X) >= 0.0)
			{
				Right = FMath::Min(Right, Distance);
			}
			else
			{
				Left = FMath::Min(Left, Distance);
			}
		}
		if (Right > Reach && Left > Reach)
		{
			return TOptional<float>();
		}
		return static_cast<float>(FMath::Min(Right, Reach) + FMath::Min(Left, Reach));
	}

	bool IsArriving(const FVector2D& LeaderPos, const TOptional<FVector2D>& Destination, const FFormationConfig& Config)
	{
		return Destination.IsSet() && FVector2D::Distance(LeaderPos, Destination.GetValue()) <= static_cast<double>(Config.ClassifyArriveDist);
	}

	EFormationTerrainClass ClassifyRaw(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const FFormationConfig& Config,
		const TOptional<FVector2D>& Destination)
	{
		// at_wall only on arrival: walking past a wall or turning at a corridor bend never triggers the wall layout.
		if (IsArriving(LeaderPos, Destination, Config))
		{
			for (const float RayDeg : TerrainAheadRaysDeg)
			{
				if (RayHit(Walls, LeaderPos, FormationMath::Forward(LeaderYaw + RayDeg), Config.ClassifyAtWallDist).IsSet())
				{
					return EFormationTerrainClass::AtWall;
				}
			}
		}
		const float Reach = Config.ClassifyProbeSide;
		const TOptional<float> Right = SideClearance(Walls, LeaderPos, LeaderYaw, 1, Reach, Config.ClassifySideIgnorePerpDeg);
		const TOptional<float> Left = SideClearance(Walls, LeaderPos, LeaderYaw, -1, Reach, Config.ClassifySideIgnorePerpDeg);
		const bool bRightBlocked = Right.IsSet() && Right.GetValue() <= Reach;
		const bool bLeftBlocked = Left.IsSet() && Left.GetValue() <= Reach;
		if (bRightBlocked && bLeftBlocked)
		{
			// Any passage between two walls is a column (user decision); PassageWidth picks the narrow / wide sectors.
			return EFormationTerrainClass::Corridor;
		}
		if (bLeftBlocked)
		{
			return EFormationTerrainClass::WallLeft;
		}
		if (bRightBlocked)
		{
			return EFormationTerrainClass::WallRight;
		}
		return EFormationTerrainClass::Open;
	}

	TOptional<float> PassageWidth(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const FFormationConfig& Config)
	{
		const float Reach = Config.ClassifyProbeSide;
		const TOptional<float> Right = SideClearance(Walls, LeaderPos, LeaderYaw, 1, Reach, Config.ClassifySideIgnorePerpDeg);
		const TOptional<float> Left = SideClearance(Walls, LeaderPos, LeaderYaw, -1, Reach, Config.ClassifySideIgnorePerpDeg);
		if (Right.IsSet() && Left.IsSet() && Right.GetValue() <= Reach && Left.GetValue() <= Reach)
		{
			return Left.GetValue() + Right.GetValue();
		}
		return TOptional<float>();
	}

	int32 ClassRank(EFormationTerrainClass Class)
	{
		switch (Class)
		{
		case EFormationTerrainClass::Open: return 0;
		case EFormationTerrainClass::Corridor: return 2;
		case EFormationTerrainClass::AtWall: return 3;
		default: return 1;
		}
	}

	FFormationClassifyState AdvanceState(EFormationTerrainClass RawClass, const TOptional<float>& MeasuredWidth, const FFormationClassifyState& State,
		float StepDistance, const FFormationConfig& Config)
	{
		const auto Next = [RawClass, &MeasuredWidth, &State](EFormationTerrainClass Current, const TOptional<EFormationTerrainClass>& Candidate, float Distance)
		{
			FFormationClassifyState Out;
			Out.Current = Current;
			Out.Candidate = Candidate;
			Out.CandidateDistance = Distance;
			if (Current == EFormationTerrainClass::Corridor)
			{
				// Measured while in the passage, held with the class after the leader has left it.
				Out.PassageWidth = RawClass == EFormationTerrainClass::Corridor ? MeasuredWidth : State.PassageWidth;
			}
			return Out;
		};
		const TOptional<EFormationTerrainClass> NoCandidate;
		if (!State.Current.IsSet() || RawClass == State.Current.GetValue())
		{
			return Next(RawClass, NoCandidate, 0.f);
		}
		const EFormationTerrainClass Current = State.Current.GetValue();
		if (Current == EFormationTerrainClass::AtWall || ClassRank(RawClass) > ClassRank(Current))
		{
			// Tighter at once; leaving the wall (a new move) at once.
			return Next(RawClass, NoCandidate, 0.f);
		}
		const bool bSameCandidate = State.Candidate.IsSet() && State.Candidate.GetValue() == RawClass;
		const float Walked = (bSameCandidate ? State.CandidateDistance : 0.f) + FMath::Max(StepDistance, 0.f);
		if (Walked >= Config.ClassifyHoldDistance)
		{
			return Next(RawClass, NoCandidate, 0.f);
		}
		return Next(Current, RawClass, Walked);
	}

	FFormationClassifyState Classify(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const TOptional<FVector2D>& Destination,
		const FFormationClassifyState& State, float StepDistance, const FFormationConfig& Config)
	{
		const EFormationTerrainClass Raw = ClassifyRaw(Walls, LeaderPos, LeaderYaw, Config, Destination);
		const TOptional<float> Measured = Raw == EFormationTerrainClass::Corridor ? PassageWidth(Walls, LeaderPos, LeaderYaw, Config) : TOptional<float>();
		return AdvanceState(Raw, Measured, State, StepDistance, Config);
	}

	EFormationKind FormationFor(EFormationTerrainClass Class, int32 MemberCount, bool bBlizzard)
	{
		switch (Class)
		{
		case EFormationTerrainClass::WallLeft: return EFormationKind::EchelonRight;
		case EFormationTerrainClass::WallRight: return EFormationKind::EchelonLeft;
		case EFormationTerrainClass::Corridor: return EFormationKind::Column;
		case EFormationTerrainClass::AtWall: return EFormationKind::Wall;
		default:
			// Open ground; in a blizzard the tight diamond (walls / passages keep their rules; the menu choice overrides).
			if (bBlizzard)
			{
				return EFormationKind::Blizzard;
			}
			return MemberCount >= 4 ? EFormationKind::Diamond : EFormationKind::Wedge;
		}
	}
}

EFormationTerrainClass FFormationTerrainTracker::Step(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
	const TOptional<FVector2D>& Destination, float StepDistance, const FFormationConfig& Config)
{
	State = FormationTerrainRules::Classify(Walls, LeaderPos, LeaderYaw, Destination, State, StepDistance, Config);
	return GetClass();
}

EFormationTerrainClass FFormationTerrainTracker::GetClass() const
{
	return State.Current.Get(EFormationTerrainClass::Open);
}

TOptional<float> FFormationTerrainTracker::GetPassageWidth() const
{
	return State.PassageWidth;
}

void FFormationTerrainTracker::Reset()
{
	State = FFormationClassifyState();
}
