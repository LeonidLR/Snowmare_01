#include "Formation/FormationWallEndRules.h"

#include "Formation/FormationCornerRules.h"
#include "Formation/FormationWallRules.h"

namespace FormationWallEnd
{
	int32 FindSquadWall(const TArray<FFormationWall>& Walls, const FVector2D& KPlace, float NormalYaw)
	{
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const FVector2D Normal = FormationMath::Forward(NormalYaw);
		int32 Best = INDEX_NONE;
		double BestSurface = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Walls.Num(); ++Index)
		{
			const FFormationWall& Wall = Walls[Index];
			const double Length = FVector2D::Distance(Wall.A, Wall.B);
			if (Length < 1.0)
			{
				continue;
			}
			const FVector2D U = (Wall.B - Wall.A) / Length;
			if (FMath::Abs(FVector2D::DotProduct(U, RightAlong)) < ParallelCos)
			{
				continue;
			}
			double T = 0.0;
			const FVector2D Closest = FormationMath::ClosestOnSegment(KPlace, Wall.A, Wall.B, &T);
			if (T <= 0.0 || T >= 1.0 || FVector2D::DotProduct(KPlace - Closest, Normal) <= 0.0)
			{
				continue; // not over its span, or on its far side
			}
			const double Surface = FVector2D::Distance(KPlace, Closest) - 0.5 * Wall.Thickness;
			if (Surface <= SquadWallReachCm && Surface < BestSurface)
			{
				BestSurface = Surface;
				Best = Index;
			}
		}
		return Best;
	}

	TOptional<FFormationStackEnd> FindStackEnd(const TArray<FFormationWall>& Walls, const FVector2D& KPlace, float NormalYaw, const FVector2D& Destination,
		const FFormationConfig& Config)
	{
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const FVector2D Normal = FormationMath::Forward(NormalYaw);
		const double AlongDestination = FVector2D::DotProduct(Destination - KPlace, RightAlong);
		if (FMath::Abs(AlongDestination) < FormationWallRules::SlideMin)
		{
			return TOptional<FFormationStackEnd>();
		}
		const int32 Index = FindSquadWall(Walls, KPlace, NormalYaw);
		if (Index == INDEX_NONE)
		{
			return TOptional<FFormationStackEnd>();
		}
		const FFormationWall& Wall = Walls[Index];
		const FVector2D Along = RightAlong * (AlongDestination > 0.0 ? 1.0 : -1.0);
		const FVector2D End = FVector2D::DotProduct(Wall.A, Along) >= FVector2D::DotProduct(Wall.B, Along) ? Wall.A : Wall.B;
		if (FVector2D::DotProduct(End - KPlace, Along) <= 0.0)
		{
			return TOptional<FFormationStackEnd>();
		}
		// A joint (the corner of a room, a T): nothing to peek round.
		for (int32 Other = 0; Other < Walls.Num(); ++Other)
		{
			if (Other == Index)
			{
				continue;
			}
			const FFormationWall& OtherWall = Walls[Other];
			if (FVector2D::Distance(End, FormationMath::ClosestOnSegment(End, OtherWall.A, OtherWall.B))
				<= 0.5 * OtherWall.Thickness + 0.5 * Wall.Thickness + FormationCornerRules::CornerJoint)
			{
				return TOptional<FFormationStackEnd>();
			}
		}
		const double PastEnd = FVector2D::DotProduct(Destination - End, Along);
		const double OffWall = FVector2D::DotProduct(Destination - End, Normal) - 0.5 * Wall.Thickness;
		if (PastEnd < -StackReachCm || PastEnd > StackPastCm || OffWall < 0.0 || OffWall > Config.SlideMaxOffWall)
		{
			return TOptional<FFormationStackEnd>();
		}
		FFormationStackEnd Result;
		Result.End = End;
		Result.Along = Along;
		Result.Thickness = Wall.Thickness;
		Result.Wall = Index;
		return Result;
	}

	float StackShift(const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End)
	{
		double Lead = -TNumericLimits<double>::Max();
		for (const FFormationSlot& Slot : StartSlots)
		{
			Lead = FMath::Max(Lead, FVector2D::DotProduct(Slot.Position - End.End, End.Along));
		}
		return StartSlots.IsEmpty() ? 0.f : static_cast<float>(-EdgeInsetCm - Lead);
	}

	float CornerPeekYaw(const FFormationStackEnd& End, const FVector2D& PointPos)
	{
		const FVector2D Graze = End.End + End.Along * (0.5 * End.Thickness + FormationCornerRules::CornerGraze);
		return static_cast<float>(FormationMath::NormYaw(FormationMath::YawOfD(Graze - PointPos)));
	}

	EFormationRole PointRole(const TArray<FFormationSlot>& Slots, const FFormationStackEnd& End)
	{
		const FFormationSlot* Point = nullptr;
		for (const FFormationSlot& Slot : Slots)
		{
			if (!Point || FVector2D::DotProduct(Slot.Position - Point->Position, End.Along) > 1e-6)
			{
				Point = &Slot;
			}
		}
		return Point ? Point->Role : EFormationRole::Commander;
	}

	TArray<FFormationSlot> StackSlots(const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End, float NormalYaw, const FFormationConfig& Config)
	{
		TArray<FFormationSlot> Slots = StartSlots;
		if (Slots.IsEmpty())
		{
			return Slots;
		}
		const FVector2D Shift = End.Along * StackShift(StartSlots, End);
		for (FFormationSlot& Slot : Slots)
		{
			Slot.Position += Shift;
		}
		const EFormationRole Point = PointRole(Slots, End);
		// The trailing end: the place farthest back along the wall.
		const FFormationSlot* Trail = nullptr;
		for (const FFormationSlot& Slot : Slots)
		{
			if (!Trail || FVector2D::DotProduct(Slot.Position - Trail->Position, End.Along) < -1e-6)
			{
				Trail = &Slot;
			}
		}
		const EFormationRole TrailRole = Trail ? Trail->Role : Point;
		const double Out = Config.SlideLeadOutDeg;
		const double BackYaw = FormationMath::YawOfD(-End.Along);
		// "Outward" = turned from the wall direction towards the normal (FormationWallRules::Slide's TowardNormal).
		const double Sign = FMath::Sign(FormationMath::NormYaw(static_cast<double>(NormalYaw) - BackYaw));
		const float TrailLook = static_cast<float>(FormationMath::NormYaw(BackYaw + (Sign != 0.0 ? Sign : 1.0) * Out));
		// A fourth man between K and the rear (not an end, not K): out from the wall turned towards the rear by wall.neighbour_deg (the
		// wall layout's neighbour of an end) - K holds the outward front, the point man the corner, the rear man the wall's run back.
		const double MiddleSign = FMath::Sign(FormationMath::NormYaw(BackYaw - static_cast<double>(NormalYaw)));
		const float MiddleLook = static_cast<float>(FormationMath::NormYaw(static_cast<double>(NormalYaw) + (MiddleSign != 0.0 ? MiddleSign : 1.0) * Config.WallNeighbourDeg));
		const EFormationRole KRole = Slots[0].Role;
		for (FFormationSlot& Slot : Slots)
		{
			if (Slot.Role == Point)
			{
				Slot.Yaw = CornerPeekYaw(End, Slot.Position);
			}
			else if (Slot.Role == TrailRole)
			{
				Slot.Yaw = TrailLook;
			}
			else if (Slot.Role == KRole)
			{
				Slot.Yaw = FRotator::NormalizeAxis(NormalYaw);
			}
			else
			{
				Slot.Yaw = MiddleLook;
			}
		}
		return Slots;
	}

	TArray<FFormationSlot> FitLayout(const TArray<FFormationSlot>& Slots, float NormalYaw, TFunctionRef<bool(const FVector2D& KPlace, const FVector2D& Place)> Usable,
		const FFormationConfig& Config)
	{
		if (Slots.Num() < 2 || Config.WallSpacing <= 1.f)
		{
			return Slots;
		}
		const FVector2D KPlace = Slots[0].Position;
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);		const auto StepOf = [&](const FVector2D& Place) { return FMath::RoundToInt32(FVector2D::DotProduct(Place - KPlace, RightAlong) / Config.WallSpacing); };
		TArray<int32> Steps;
		TArray<bool> Ok;
		bool bAnyBad = false;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			Steps.Add(Index == 0 ? 0 : StepOf(Slots[Index].Position));
			Ok.Add(Index == 0 || Usable(KPlace, Slots[Index].Position));
			bAnyBad |= !Ok.Last();
		}
		if (!bAnyBad)
		{
			return Slots;
		}
		TArray<FFormationSlot> Fitted = Slots;
		bool bMoved = false;
		for (int32 Index = 1; Index < Fitted.Num(); ++Index)
		{
			if (Ok[Index])
			{
				continue;
			}
			const int32 Side = Steps[Index] > 0 ? -1 : 1;
			for (int32 Reach = 1; Reach <= Fitted.Num(); ++Reach)
			{
				const int32 Step = Side * Reach;
				if (Steps.Contains(Step))
				{
					continue; // taken (K, or a member already there)
				}
				const FVector2D Place = KPlace + RightAlong * (static_cast<double>(Step) * Config.WallSpacing);
				if (Usable(KPlace, Place))
				{
					Fitted[Index].Position = Place;
					Steps[Index] = Step;
					Ok[Index] = true;
					bMoved = true;
				}
				break; // the next free step only: past an unusable one the line would have a hole
			}
		}
		if (!bMoved)
		{
			return Slots;
		}
		const auto Look = [NormalYaw](float Relative) { return FRotator::NormalizeAxis(NormalYaw + Relative); };
		if (Fitted.Num() == 2)
		{
			const float Side = Steps[1] > 0 ? 1.f : -1.f; // the partner's side: right = + (FormationWallRules::Layout's pair)
			Fitted[0].Yaw = Look(Side * FormationWallRules::PairLookDeg);
			Fitted[1].Yaw = Look(-Side * FormationWallRules::PairLookDeg);
			return Fitted;
		}
		for (const int32 Side : { 1, -1 })
		{
			TArray<int32> OnSide;
			for (int32 Index = 1; Index < Fitted.Num(); ++Index)
			{
				if (FMath::Sign(Steps[Index]) == Side)
				{
					OnSide.Add(Index);
				}
			}
			OnSide.Sort([&Steps](int32 A, int32 B) { return FMath::Abs(Steps[A]) < FMath::Abs(Steps[B]); });
			for (int32 Rank = 0; Rank < OnSide.Num(); ++Rank)
			{
				const bool bOuter = Rank == OnSide.Num() - 1;
				const float Deg = OnSide.Num() == 1 ? Config.WallFlankDeg : (bOuter ? Config.WallEndDeg : Config.WallNeighbourDeg);
				// Right (+ steps) looks right of the normal (N - deg), left looks N + deg (FormationWallRules::Layout).
				Fitted[OnSide[Rank]].Yaw = Look(Side > 0 ? -Deg : Deg);
			}
		}
		return Fitted;
	}

	FFormationSquadPeek PlanSquadPeek(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& StartSlots, float NormalYaw, const FVector2D& EdgeOnFace,
		const TArray<FFormationSlot>& Members, const FFormationConfig& Config)
	{
		FFormationSquadPeek Plan;
		Plan.StackDestination = EdgeOnFace + FormationMath::Forward(NormalYaw) * PeekDestinationOutCm;
		const TOptional<FFormationStackEnd> Stack = StartSlots.IsEmpty() || StartSlots[0].Role != EFormationRole::Commander
			? TOptional<FFormationStackEnd>() : FindStackEnd(Walls, StartSlots[0].Position, NormalYaw, Plan.StackDestination, Config);
		if (Stack.IsSet())
		{
			Plan.bStack = true;
			Plan.End = Stack.GetValue();
			Plan.StackSlots = StackSlots(StartSlots, Plan.End, NormalYaw, Config);
			Plan.Peeker = PointRole(Plan.StackSlots, Plan.End);
			// The point man must be one of the members (a place nobody holds: the nearest member instead, no stack).
			if (Members.ContainsByPredicate([&Plan](const FFormationSlot& Each) { return Each.Role == Plan.Peeker; }))
			{
				return Plan;
			}
			Plan = FFormationSquadPeek();
			Plan.StackDestination = EdgeOnFace + FormationMath::Forward(NormalYaw) * PeekDestinationOutCm;
		}
		double Nearest = TNumericLimits<double>::Max();
		for (const FFormationSlot& Each : Members)
		{
			const double Distance = FVector2D::Distance(Each.Position, EdgeOnFace);
			if (Distance < Nearest)
			{
				Nearest = Distance;
				Plan.Peeker = Each.Role;
			}
		}
		return Plan;
	}

	TOptional<FVector2D> StepRound(const FVector2D& Here, const FVector2D& Place, const TArray<FVector2D>& Others, float NormalYaw)
	{
		const FVector2D Way = Place - Here;
		const double Length = Way.Size();
		if (Length <= 2.0 * StepRoundLaneCm)
		{
			return TOptional<FVector2D>();
		}
		const FVector2D Dir = Way / Length;
		double Nearest = TNumericLimits<double>::Max();
		TOptional<FVector2D> Step;
		for (const FVector2D& Other : Others)
		{
			const double Along = FVector2D::DotProduct(Other - Here, Dir);
			if (Along <= StepRoundLaneCm || Along >= Length - StepRoundLaneCm)
			{
				continue;
			}
			const double Lateral = FMath::Abs(Dir.X * (Other.Y - Here.Y) - Dir.Y * (Other.X - Here.X));
			if (Lateral <= StepRoundLaneCm && Along < Nearest)
			{
				Nearest = Along;
				// Out from him on the open side and a little past him along the wall: from there the way to the place is clear.
				const FVector2D Normal = FormationMath::Forward(NormalYaw);
				const FVector2D AlongWall = (Dir - Normal * FVector2D::DotProduct(Dir, Normal)).GetSafeNormal();
				Step = Other + Normal * StepRoundOutCm + AlongWall * StepRoundLaneCm;
			}
		}
		return Step;
	}
}
