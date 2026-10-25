#include "Formation/FormationCornerRules.h"

#include "Formation/FormationTerrainRules.h"

namespace FormationCornerRules
{
	static const FVector2D& CornerEndPoint(const FFormationWall& Wall, int32 End)
	{
		return End == 0 ? Wall.A : Wall.B;
	}

	static const FVector2D& CornerOtherPoint(const FFormationWall& Wall, int32 End)
	{
		return End == 0 ? Wall.B : Wall.A;
	}

	TOptional<float> CornerAhead(const TArray<FFormationWall>& Walls, int32 WallIndex, int32 End, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config)
	{
		if (!Walls.IsValidIndex(WallIndex))
		{
			return TOptional<float>();
		}
		const FFormationWall& Wall = Walls[WallIndex];
		if (FormationTerrainRules::IsAcrossHeading(Wall, LeaderYaw, Config.ClassifySideIgnorePerpDeg))
		{
			return TOptional<float>();
		}
		const FVector2D& E = CornerEndPoint(Wall, End);
		const FVector2D& O = CornerOtherPoint(Wall, End);
		const FVector2D H = FormationMath::DirOf(LeaderYaw);
		const double S = FVector2D::DotProduct(E - LeaderPos, H);
		const double Back = FVector2D::DotProduct(O - LeaderPos, H);
		const double Lateral = FMath::Abs(H.X * (E.Y - LeaderPos.Y) - H.Y * (E.X - LeaderPos.X));
		if (Back >= S || Lateral > static_cast<double>(Config.ClassifyProbeSide))
		{
			return TOptional<float>();
		}
		for (int32 Other = 0; Other < Walls.Num(); ++Other)
		{
			if (Other == WallIndex)
			{
				continue;
			}
			const FFormationWall& OtherWall = Walls[Other];
			const double Distance = FVector2D::Distance(E, FormationMath::ClosestOnSegment(E, OtherWall.A, OtherWall.B));
			if (Distance <= 0.5 * OtherWall.Thickness + 0.5 * Wall.Thickness + CornerJoint)
			{
				return TOptional<float>();
			}
		}
		return static_cast<float>(S);
	}

	FFormationCornerResult Watch(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const TArray<FFormationSlot>& Members,
		const FFormationCornerState& State, const FFormationConfig& Config)
	{
		const double Lead = Config.CornerLeadDist;
		const double Hold = Config.CornerHoldDist;
		const FVector2D H = FormationMath::DirOf(LeaderYaw);
		// 0 = left, 1 = right of the leader.
		const auto SideOf = [&H, &LeaderPos](const FVector2D& E) { return H.X * (E.Y - LeaderPos.Y) - H.Y * (E.X - LeaderPos.X) > 0.0 ? 1 : 0; };
		const auto FindMember = [&Members](EFormationRole Role)
		{
			return Members.FindByPredicate([Role](const FFormationSlot& Member) { return Member.Role == Role; });
		};
		const auto Keep = [&](const TOptional<FFormationCornerWatch>& Watched, int32 Side) -> TOptional<FFormationCornerWatch>
		{
			if (!Watched.IsSet() || Watched->Wall >= Walls.Num() || Watched->Wall < 0 || !FindMember(Watched->Role))
			{
				return TOptional<FFormationCornerWatch>();
			}
			const TOptional<float> S = CornerAhead(Walls, Watched->Wall, Watched->End, LeaderPos, LeaderYaw, Config);
			if (!S.IsSet() || S.GetValue() < -Hold || S.GetValue() > Lead || SideOf(CornerEndPoint(Walls[Watched->Wall], Watched->End)) != Side)
			{
				return TOptional<FFormationCornerWatch>();
			}
			return Watched;
		};
		TOptional<FFormationCornerWatch> Next[2] = { Keep(State.Left, 0), Keep(State.Right, 1) };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (Next[Side].IsSet())
			{
				continue;
			}
			double BestS = TNumericLimits<double>::Max();
			int32 FoundWall = INDEX_NONE;
			int32 FoundEnd = 0;
			for (int32 Index = 0; Index < Walls.Num(); ++Index)
			{
				for (int32 End = 0; End < 2; ++End)
				{
					const TOptional<float> S = CornerAhead(Walls, Index, End, LeaderPos, LeaderYaw, Config);
					if (!S.IsSet() || S.GetValue() <= 0.f || S.GetValue() > Lead || S.GetValue() >= BestS)
					{
						continue;
					}
					if (SideOf(CornerEndPoint(Walls[Index], End)) != Side)
					{
						continue;
					}
					BestS = S.GetValue();
					FoundWall = Index;
					FoundEnd = End;
				}
			}
			if (FoundWall == INDEX_NONE)
			{
				continue;
			}
			const FVector2D& E = CornerEndPoint(Walls[FoundWall], FoundEnd);
			const TOptional<FFormationCornerWatch>& OtherWatch = Next[1 - Side];
			const FFormationSlot* Closest = nullptr;
			for (const FFormationSlot& Member : Members)
			{
				if (OtherWatch.IsSet() && OtherWatch->Role == Member.Role)
				{
					continue;
				}
				if (!Closest || FVector2D::Distance(Member.Position, E) < FVector2D::Distance(Closest->Position, E) - 1e-6)
				{
					Closest = &Member;
				}
			}
			if (!Closest)
			{
				continue;
			}
			FFormationCornerWatch Watched;
			Watched.Wall = FoundWall;
			Watched.End = FoundEnd;
			Watched.Role = Closest->Role;
			Next[Side] = Watched;
		}

		FFormationCornerResult Result;
		Result.State.Left = Next[0];
		Result.State.Right = Next[1];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (!Next[Side].IsSet())
			{
				continue;
			}
			const FFormationCornerWatch& Watched = Next[Side].GetValue();
			const FFormationWall& Wall = Walls[Watched.Wall];
			const FVector2D& E = CornerEndPoint(Wall, Watched.End);
			const FVector2D& O = CornerOtherPoint(Wall, Watched.End);
			const FFormationSlot* Watcher = FindMember(Watched.Role);
			if (!Watcher)
			{
				continue;
			}
			const double Length = FVector2D::Distance(E, O);
			const FVector2D U = Length > 1e-6 ? (E - O) * (1.0 / Length) : H;
			const FVector2D NRaw(-U.Y, U.X);
			const double SquadSide = FVector2D::DotProduct(LeaderPos - E, NRaw) >= 0.0 ? 1.0 : -1.0;
			const FVector2D Graze = E + U * (0.5 * Wall.Thickness + CornerGraze);
			const bool bPast = FVector2D::DotProduct(Watcher->Position - E, U) >= 0.0;
			const FVector2D Target = bPast ? Graze + NRaw * (-SquadSide * Config.CornerPeekDist) : Graze;
			FFormationCornerLook Look;
			Look.Role = Watched.Role;
			Look.Yaw = static_cast<float>(FormationMath::NormYaw(FormationMath::YawOfD(Target - Watcher->Position)));
			Result.Looks.Add(Look);
		}
		return Result;
	}
}
