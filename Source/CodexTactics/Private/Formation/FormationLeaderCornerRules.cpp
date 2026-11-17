#include "Formation/FormationLeaderCornerRules.h"

namespace FormationLeaderCornerRules
{
	namespace
	{
		/** The reference point along the wall: the leader's slot. */
		double ReferenceAlong(const FFormationLeaderCorner& Claim)
		{
			return FVector2D::DotProduct(Claim.Slot - Claim.EndPoint, Claim.Along);
		}

		bool IsSameEnd(const TOptional<FFormationCornerWatch>& Watched, const FFormationLeaderCorner& Claim)
		{
			return Watched.IsSet() && Watched->Wall == Claim.Wall && Watched->End == Claim.End;
		}
	}

	TOptional<FFormationLeaderCorner> ClaimedEnd(const TArray<FFormationWall>& Walls, const FVector2D& Slot, float ClaimDist)
	{
		TOptional<FFormationLeaderCorner> Best;
		double BestAlong = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Walls.Num(); ++Index)
		{
			const FFormationWall& Wall = Walls[Index];
			if (Wall.bProbeHit)
			{
				continue; // a 20 cm probe piece of an obstacle of unknown size: its "ends" are no corners
			}
			const double Length = FVector2D::Distance(Wall.A, Wall.B);
			if (Length < 1.0)
			{
				continue;
			}
			for (int32 End = 0; End < 2; ++End)
			{
				if (!FormationCornerRules::IsFreeEnd(Walls, Index, End))
				{
					continue;
				}
				const FVector2D& E = End == 0 ? Wall.A : Wall.B;
				const FVector2D& O = End == 0 ? Wall.B : Wall.A;
				const FVector2D U = (E - O) / Length;
				const FVector2D N(-U.Y, U.X);
				const double T = FVector2D::DotProduct(Slot - E, U);
				const double Side = FVector2D::DotProduct(Slot - E, N);
				if (T < -static_cast<double>(ClaimDist) || T > static_cast<double>(ClaimPastCm) || T < -Length)
				{
					continue;
				}
				if (FMath::Abs(Side) - 0.5 * Wall.Thickness > static_cast<double>(SlotWallReachCm))
				{
					continue;
				}
				if (FMath::Abs(T) >= BestAlong)
				{
					continue;
				}
				BestAlong = FMath::Abs(T);
				FFormationLeaderCorner Claim;
				Claim.Wall = Index;
				Claim.End = End;
				Claim.EndPoint = E;
				Claim.Along = U;
				Claim.Out = Side >= 0.0 ? N : -N;
				Claim.Thickness = Wall.Thickness;
				Claim.Slot = Slot;
				Best = Claim;
			}
		}
		return Best;
	}

	TOptional<FFormationLeaderCorner> ClaimedEndOf(const TArray<FFormationWall>& Walls, const TOptional<FVector2D>& PendingSlot,
		const TOptional<FVector2D>& CurrentSlot, float ClaimDist)
	{
		if (PendingSlot.IsSet())
		{
			const TOptional<FFormationLeaderCorner> Pending = ClaimedEnd(Walls, PendingSlot.GetValue(), ClaimDist);
			if (Pending.IsSet())
			{
				return Pending;
			}
		}
		return CurrentSlot.IsSet() ? ClaimedEnd(Walls, CurrentSlot.GetValue(), ClaimDist) : TOptional<FFormationLeaderCorner>();
	}

	TOptional<FFormationLeaderCorner> ClaimedPeekEdge(const FVector2D& Slot, const FVector2D& EdgeOnFace, const FVector2D& FaceOut, float ClaimDist)
	{
		const FVector2D Out = FaceOut.GetSafeNormal();
		if (Out.IsNearlyZero())
		{
			return TOptional<FFormationLeaderCorner>();
		}
		// Along the face towards the edge (the slot's side of it decides the sign).
		const FVector2D Tangent(-Out.Y, Out.X);
		const double Toward = FVector2D::DotProduct(EdgeOnFace - Slot, Tangent);
		const FVector2D Along = Toward >= 0.0 ? Tangent : -Tangent;
		const double T = FVector2D::DotProduct(Slot - EdgeOnFace, Along);
		const double Off = FVector2D::DotProduct(Slot - EdgeOnFace, Out);
		if (T < -static_cast<double>(ClaimDist) || T > static_cast<double>(ClaimPastCm) || Off < -1.0 || Off > static_cast<double>(SlotWallReachCm))
		{
			return TOptional<FFormationLeaderCorner>();
		}
		FFormationLeaderCorner Claim;
		Claim.Wall = INDEX_NONE;
		Claim.End = 0;
		Claim.EndPoint = EdgeOnFace;
		Claim.Along = Along;
		Claim.Out = Out;
		Claim.Thickness = 0.f;
		Claim.Slot = Slot;
		return Claim;
	}

	bool IsAtClaimedCorner(const FFormationLeaderCorner& Claim, const FVector2D& Place, float KeepBack)
	{
		const double Reference = ReferenceAlong(Claim);
		const double T = FVector2D::DotProduct(Place - Claim.EndPoint, Claim.Along);
		const double Off = FMath::Abs(FVector2D::DotProduct(Place - Claim.EndPoint, Claim.Out));
		return T > Reference - static_cast<double>(KeepBack) && Off <= 0.5 * Claim.Thickness + static_cast<double>(ZoneOutCm);
	}

	FVector2D KeepBehind(const FFormationLeaderCorner& Claim, const FVector2D& Place, float KeepBack)
	{
		if (!IsAtClaimedCorner(Claim, Place, KeepBack))
		{
			return Place;
		}
		const double Reference = ReferenceAlong(Claim);
		const double T = FVector2D::DotProduct(Place - Claim.EndPoint, Claim.Along);
		const double Off = FMath::Abs(FVector2D::DotProduct(Place - Claim.EndPoint, Claim.Out));
		const double HalfThickness = 0.5 * Claim.Thickness;
		// Mirrored through the leader along the wall: a column ahead of him towards the end becomes the same column behind him.
		const double Behind = FMath::Min(2.0 * Reference - T, Reference - static_cast<double>(KeepBack));
		const double Out = FMath::Clamp(Off, HalfThickness + static_cast<double>(MinOutCm), HalfThickness + static_cast<double>(ZoneOutCm));
		return Claim.EndPoint + Claim.Along * Behind + Claim.Out * Out;
	}

	int32 KeepSlotsBehind(const FFormationLeaderCorner& Claim, TArray<FFormationSlot>& Slots, float KeepBack)
	{
		TArray<int32> MovedIndices;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			FFormationSlot& Slot = Slots[Index];
			if (Slot.Role == EFormationRole::Commander)
			{
				continue;
			}
			const FVector2D Kept = KeepBehind(Claim, Slot.Position, KeepBack);
			if (!Kept.Equals(Slot.Position, 0.01))
			{
				Slot.Position = Kept;
				MovedIndices.Add(Index);
			}
		}
		// A moved place never lands on a team-mate's: the nearest to the leader first, each one KeepBack behind any place it would crowd.
		MovedIndices.Sort([&Slots, &Claim](int32 A, int32 B)
		{
			return FVector2D::DotProduct(Slots[A].Position - Claim.EndPoint, Claim.Along) > FVector2D::DotProduct(Slots[B].Position - Claim.EndPoint, Claim.Along);
		});
		for (const int32 Index : MovedIndices)
		{
			for (int32 Step = 0; Step <= Slots.Num(); ++Step)
			{
				const FFormationSlot* Crowded = nullptr;
				for (int32 Other = 0; Other < Slots.Num(); ++Other)
				{
					if (Other != Index && FVector2D::Distance(Slots[Other].Position, Slots[Index].Position) < static_cast<double>(KeepBack) - 0.01)
					{
						Crowded = &Slots[Other];
						break;
					}
				}
				if (!Crowded)
				{
					break;
				}
				const double OtherAlong = FVector2D::DotProduct(Crowded->Position - Claim.EndPoint, Claim.Along);
				const double MyAlong = FVector2D::DotProduct(Slots[Index].Position - Claim.EndPoint, Claim.Along);
				Slots[Index].Position += Claim.Along * (FMath::Min(MyAlong, OtherAlong - static_cast<double>(KeepBack)) - MyAlong);
			}
		}
		return MovedIndices.Num();
	}

	FFormationCornerResult Watch(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const TArray<FFormationSlot>& Members,
		const FFormationCornerState& State, const TOptional<FFormationLeaderCorner>& Claim, const FFormationConfig& Config)
	{
		const FFormationCornerResult Plain = FormationCornerRules::Watch(Walls, LeaderPos, LeaderYaw, Members, State, Config);
		if (!Claim.IsSet())
		{
			return Plain;
		}
		FFormationCornerState Fixed = Plain.State;
		TOptional<FFormationCornerWatch>* Claimed = IsSameEnd(Fixed.Left, Claim.GetValue()) ? &Fixed.Left
			: (IsSameEnd(Fixed.Right, Claim.GetValue()) ? &Fixed.Right : nullptr);
		if (!Claimed || (*Claimed)->Role == EFormationRole::Commander)
		{
			return Plain; // the claimed end is not watched, or the leader watches it already
		}
		(*Claimed)->Role = EFormationRole::Commander;
		TOptional<FFormationCornerWatch>& Other = Claimed == &Fixed.Left ? Fixed.Right : Fixed.Left;
		if (Other.IsSet() && Other->Role == EFormationRole::Commander)
		{
			Other.Reset(); // the leader cannot watch both: that side picks its watcher again among the others
		}
		// The second step keeps the fixed state (the same inputs: the claimed end is still ahead) and gives the looks of the watchers in it.
		return FormationCornerRules::Watch(Walls, LeaderPos, LeaderYaw, Members, Fixed, Config);
	}
}
