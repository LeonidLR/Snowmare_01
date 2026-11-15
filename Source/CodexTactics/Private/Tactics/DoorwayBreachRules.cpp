#include "Tactics/DoorwayBreachRules.h"

#include "Tactics/CoverTypes.h"

const FDoorwayBreachSlot* FDoorwayBreachTriad::GetSlotByRole(EDoorwayBreachSlotRole Role) const
{
	switch (Role)
	{
	case EDoorwayBreachSlotRole::LeftJamb: return &LeftJamb;
	case EDoorwayBreachSlotRole::RightJamb: return &RightJamb;
	case EDoorwayBreachSlotRole::CenterPoint: return &CenterPoint;
	case EDoorwayBreachSlotRole::RearSecurity: return &RearSecurity;
	default: return nullptr;
	}
}

FDoorwayBreachSlot* FDoorwayBreachTriad::GetSlotByRole(EDoorwayBreachSlotRole Role)
{
	switch (Role)
	{
	case EDoorwayBreachSlotRole::LeftJamb: return &LeftJamb;
	case EDoorwayBreachSlotRole::RightJamb: return &RightJamb;
	case EDoorwayBreachSlotRole::CenterPoint: return &CenterPoint;
	case EDoorwayBreachSlotRole::RearSecurity: return &RearSecurity;
	default: return nullptr;
	}
}

TArray<FDoorwayBreachSlot> FDoorwayBreachTriad::GetAllSlots() const
{
	TArray<FDoorwayBreachSlot> Slots;
	Slots.Add(LeftJamb);
	Slots.Add(RightJamb);
	Slots.Add(CenterPoint);
	if (bHasRearSecurity)
	{
		Slots.Add(RearSecurity);
	}
	return Slots;
}

namespace DoorwayBreachRules
{
	namespace
	{
		void PermuteSlots(
			int32 Start,
			int32 Count,
			TArray<int32>& Indices,
			const TArray<FVector>& Positions,
			const TArray<FDoorwayBreachSlot>& CandidateSlots,
			float& BestDist,
			TArray<int32>& BestIndices)
		{
			if (Start == Count)
			{
				float CurrentDist = 0.f;
				for (int32 I = 0; I < Count; ++I)
				{
					CurrentDist += FVector::Dist(Positions[I], CandidateSlots[Indices[I]].Location);
				}
				if (CurrentDist < BestDist)
				{
					BestDist = CurrentDist;
					BestIndices = Indices;
				}
				return;
			}

			for (int32 I = Start; I < Indices.Num(); ++I)
			{
				Indices.Swap(Start, I);
				PermuteSlots(Start + 1, Count, Indices, Positions, CandidateSlots, BestDist, BestIndices);
				Indices.Swap(Start, I);
			}
		}
	}

	const FDoorwayBreachConfig& GetConfig()
	{
		static const FDoorwayBreachConfig Config;
		return Config;
	}

	FDoorwayBreachTriad CalculateBreachSlots(
		const FVector& DoorwayCenter,
		float OpeningWidthCm,
		const FVector& WallTangent,
		const FVector& OutwardNormal,
		const FDoorwayBreachConfig& Config)
	{
		FDoorwayBreachTriad Triad;
		Triad.DoorwayCenter = DoorwayCenter;
		Triad.OpeningWidthCm = FMath::Clamp(OpeningWidthCm, Config.MinDoorWidthCm, Config.MaxGateWidthCm);

		FVector Outward = FVector(OutwardNormal.X, OutwardNormal.Y, 0.f).GetSafeNormal();
		FVector Tangent = FVector(WallTangent.X, WallTangent.Y, 0.f).GetSafeNormal();

		if (Outward.IsNearlyZero())
		{
			if (Tangent.IsNearlyZero())
			{
				Outward = FVector::ForwardVector;
				Tangent = FVector::RightVector;
			}
			else
			{
				Outward = FVector(-Tangent.Y, Tangent.X, 0.f).GetSafeNormal();
			}
		}

		// When looking into the room (facing -Outward), Right is Cross(Up, Outward)
		const FVector RightDir = FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal();
		const FVector LeftDir = -RightDir;
		Triad.OutwardNormal = Outward;
		Triad.WallTangent = RightDir;

		const float HalfWidth = Triad.OpeningWidthCm * 0.5f;
		const FVector IntoRoom = -Outward;
		const float TanAim = FMath::Tan(FMath::DegreesToRadians(Config.AimCrossAngleDeg));

		// 1. Left Jamb (Slot L)
		{
			FDoorwayBreachSlot& Slot = Triad.LeftJamb;
			Slot.Role = EDoorwayBreachSlotRole::LeftJamb;
			const FVector WallEdgePoint = DoorwayCenter + LeftDir * HalfWidth;
			Slot.Location = WallEdgePoint + LeftDir * Config.CornerStandoffCm + Outward * Config.WallOffsetCm;
			Slot.Location.Z = DoorwayCenter.Z;
			Slot.Rotation = Outward.Rotation(); // Back to wall, facing outward normal
			Slot.AimDirection = (IntoRoom + RightDir * TanAim).GetSafeNormal(); // Cross-right into opening
			Slot.bRequiresCover = true;
			Slot.PeekFacing = ECoverFacing::Right; // Peeks right past the jamb frame
			Slot.Stance = EOperativeStance::Crouching;
			Slot.bBlocked = false;

			Slot.CoverSlot.WorldLocation = Slot.Location;
			Slot.CoverSlot.WallPoint = WallEdgePoint + LeftDir * Config.CornerStandoffCm;
			Slot.CoverSlot.WallNormal = Outward;
			Slot.CoverSlot.Height = ECoverHeight::HighCover;
			Slot.CoverSlot.bRightEdgeExposed = true;
			Slot.CoverSlot.RightEdgeDistanceCm = Config.CornerStandoffCm;
			Slot.CoverSlot.bLeftEdgeExposed = false;
		}

		// 2. Right Jamb (Slot R)
		{
			FDoorwayBreachSlot& Slot = Triad.RightJamb;
			Slot.Role = EDoorwayBreachSlotRole::RightJamb;
			const FVector WallEdgePoint = DoorwayCenter + RightDir * HalfWidth;
			Slot.Location = WallEdgePoint + RightDir * Config.CornerStandoffCm + Outward * Config.WallOffsetCm;
			Slot.Location.Z = DoorwayCenter.Z;
			Slot.Rotation = Outward.Rotation(); // Back to wall, facing outward normal
			Slot.AimDirection = (IntoRoom + LeftDir * TanAim).GetSafeNormal(); // Cross-left into opening
			Slot.bRequiresCover = true;
			Slot.PeekFacing = ECoverFacing::Left; // Peeks left past the jamb frame
			Slot.Stance = EOperativeStance::Crouching;
			Slot.bBlocked = false;

			Slot.CoverSlot.WorldLocation = Slot.Location;
			Slot.CoverSlot.WallPoint = WallEdgePoint + RightDir * Config.CornerStandoffCm;
			Slot.CoverSlot.WallNormal = Outward;
			Slot.CoverSlot.Height = ECoverHeight::HighCover;
			Slot.CoverSlot.bLeftEdgeExposed = true;
			Slot.CoverSlot.LeftEdgeDistanceCm = Config.CornerStandoffCm;
			Slot.CoverSlot.bRightEdgeExposed = false;
		}

		// 3. Center Point (Slot C)
		{
			FDoorwayBreachSlot& Slot = Triad.CenterPoint;
			Slot.Role = EDoorwayBreachSlotRole::CenterPoint;
			Slot.Location = DoorwayCenter + Outward * Config.CenterStandoffCm;
			Slot.Location.Z = DoorwayCenter.Z;
			Slot.Rotation = IntoRoom.Rotation(); // Facing straight through the opening
			Slot.AimDirection = IntoRoom;
			Slot.bRequiresCover = false;
			Slot.Stance = EOperativeStance::Crouching;
			Slot.bBlocked = false;
		}

		// 4. Rear Security (Slot Z)
		{
			FDoorwayBreachSlot& Slot = Triad.RearSecurity;
			Slot.Role = EDoorwayBreachSlotRole::RearSecurity;
			Slot.Location = DoorwayCenter + Outward * Config.RearStandoffCm;
			Slot.Location.Z = DoorwayCenter.Z;
			Slot.Rotation = Outward.Rotation(); // 180 deg reverse facing
			Slot.AimDirection = Outward;
			Slot.bRequiresCover = false;
			Slot.Stance = EOperativeStance::Crouching;
			Slot.bBlocked = false;
		}

		Triad.bHasRearSecurity = true;
		return Triad;
	}

	void ApplyJambObstruction(
		FDoorwayBreachTriad& Triad,
		bool bLeftBlocked,
		bool bRightBlocked,
		const FDoorwayBreachConfig& Config)
	{
		const FVector RightDir = Triad.WallTangent;
		const FVector LeftDir = -RightDir;

		if (bLeftBlocked && !bRightBlocked)
		{
			// Left jamb is blocked: open right jamb is kept, center point covers,
			// third operative stacks in-line behind the right jamb operative.
			Triad.LeftJamb.bBlocked = false;
			Triad.LeftJamb.Location = Triad.RightJamb.Location + RightDir * Config.FallbackStackSpacingCm;
			Triad.LeftJamb.Location.Z = Triad.RightJamb.Location.Z;
			Triad.LeftJamb.Rotation = Triad.RightJamb.Rotation;
			Triad.LeftJamb.AimDirection = Triad.RightJamb.AimDirection;
			Triad.LeftJamb.bRequiresCover = true;
			Triad.LeftJamb.PeekFacing = ECoverFacing::Left;

			Triad.LeftJamb.CoverSlot = Triad.RightJamb.CoverSlot;
			Triad.LeftJamb.CoverSlot.WorldLocation = Triad.LeftJamb.Location;
			Triad.LeftJamb.CoverSlot.WallPoint = Triad.RightJamb.CoverSlot.WallPoint + RightDir * Config.FallbackStackSpacingCm;
			Triad.LeftJamb.CoverSlot.bLeftEdgeExposed = false;
			Triad.LeftJamb.CoverSlot.bRightEdgeExposed = false;
		}
		else if (bRightBlocked && !bLeftBlocked)
		{
			// Right jamb is blocked: open left jamb is kept, center point covers,
			// third operative stacks in-line behind the left jamb operative.
			Triad.RightJamb.bBlocked = false;
			Triad.RightJamb.Location = Triad.LeftJamb.Location + LeftDir * Config.FallbackStackSpacingCm;
			Triad.RightJamb.Location.Z = Triad.LeftJamb.Location.Z;
			Triad.RightJamb.Rotation = Triad.LeftJamb.Rotation;
			Triad.RightJamb.AimDirection = Triad.LeftJamb.AimDirection;
			Triad.RightJamb.bRequiresCover = true;
			Triad.RightJamb.PeekFacing = ECoverFacing::Right;

			Triad.RightJamb.CoverSlot = Triad.LeftJamb.CoverSlot;
			Triad.RightJamb.CoverSlot.WorldLocation = Triad.RightJamb.Location;
			Triad.RightJamb.CoverSlot.WallPoint = Triad.LeftJamb.CoverSlot.WallPoint + LeftDir * Config.FallbackStackSpacingCm;
			Triad.RightJamb.CoverSlot.bLeftEdgeExposed = false;
			Triad.RightJamb.CoverSlot.bRightEdgeExposed = false;
		}
		else if (bLeftBlocked && bRightBlocked)
		{
			Triad.LeftJamb.bBlocked = true;
			Triad.RightJamb.bBlocked = true;
		}
	}

	TArray<FDoorwaySlotAssignment> AssignSlots(
		const TArray<FVector>& OperativePositions,
		const FDoorwayBreachTriad& Triad,
		bool bSoloMode,
		int32 SelectedIndex,
		const FDoorwayBreachConfig& Config)
	{
		TArray<FDoorwaySlotAssignment> Assignments;
		if (OperativePositions.Num() == 0)
		{
			return Assignments;
		}

		// 1. Solo Mode: ONLY the selected operative moves to the nearest valid jamb
		if (bSoloMode || OperativePositions.Num() == 1)
		{
			const int32 OpIndex = OperativePositions.IsValidIndex(SelectedIndex) ? SelectedIndex : 0;
			const FVector& Pos = OperativePositions[OpIndex];

			FDoorwaySlotAssignment Assignment;
			Assignment.OperativeIndex = OpIndex;

			const bool bLeftValid = Triad.LeftJamb.IsValid();
			const bool bRightValid = Triad.RightJamb.IsValid();

			if (bLeftValid && bRightValid)
			{
				const float DistL = FVector::DistSquared(Pos, Triad.LeftJamb.Location);
				const float DistR = FVector::DistSquared(Pos, Triad.RightJamb.Location);
				Assignment.Slot = (DistL <= DistR) ? Triad.LeftJamb : Triad.RightJamb;
			}
			else if (bLeftValid)
			{
				Assignment.Slot = Triad.LeftJamb;
			}
			else if (bRightValid)
			{
				Assignment.Slot = Triad.RightJamb;
			}
			else
			{
				Assignment.Slot = Triad.CenterPoint;
			}

			Assignments.Add(Assignment);
			return Assignments;
		}

		// 2. Coordinated Squad Assignment (2, 3, or 4 operatives)
		TArray<FDoorwayBreachSlot> CandidateSlots;
		if (OperativePositions.Num() == 2)
		{
			if (Triad.LeftJamb.IsValid() && Triad.RightJamb.IsValid())
			{
				CandidateSlots.Add(Triad.LeftJamb);
				CandidateSlots.Add(Triad.RightJamb);
			}
			else if (Triad.LeftJamb.IsValid())
			{
				CandidateSlots.Add(Triad.LeftJamb);
				CandidateSlots.Add(Triad.CenterPoint);
			}
			else if (Triad.RightJamb.IsValid())
			{
				CandidateSlots.Add(Triad.RightJamb);
				CandidateSlots.Add(Triad.CenterPoint);
			}
			else
			{
				CandidateSlots.Add(Triad.CenterPoint);
				CandidateSlots.Add(Triad.RearSecurity);
			}
		}
		else if (OperativePositions.Num() == 3)
		{
			CandidateSlots.Add(Triad.LeftJamb);
			CandidateSlots.Add(Triad.RightJamb);
			CandidateSlots.Add(Triad.CenterPoint);
		}
		else
		{
			CandidateSlots.Add(Triad.LeftJamb);
			CandidateSlots.Add(Triad.RightJamb);
			CandidateSlots.Add(Triad.CenterPoint);
			CandidateSlots.Add(Triad.RearSecurity);
		}

		const int32 Count = FMath::Min(OperativePositions.Num(), CandidateSlots.Num());
		TArray<int32> Indices;
		for (int32 I = 0; I < CandidateSlots.Num(); ++I)
		{
			Indices.Add(I);
		}

		float BestDist = MAX_flt;
		TArray<int32> BestIndices = Indices;
		PermuteSlots(0, Count, Indices, OperativePositions, CandidateSlots, BestDist, BestIndices);

		for (int32 I = 0; I < Count; ++I)
		{
			FDoorwaySlotAssignment Assignment;
			Assignment.OperativeIndex = I;
			Assignment.Slot = CandidateSlots[BestIndices[I]];
			Assignments.Add(Assignment);
		}

		return Assignments;
	}

	bool IsPointInDoorwayPortal(
		const FVector& Point,
		const FVector& DoorwayCenter,
		float OpeningWidthCm,
		const FVector& WallTangent,
		const FVector& OutwardNormal,
		float LateralSlackCm,
		float DepthSlackCm)
	{
		const FVector Delta = Point - DoorwayCenter;

		const FVector Tangent = FVector(WallTangent.X, WallTangent.Y, 0.f).GetSafeNormal();
		const FVector Outward = FVector(OutwardNormal.X, OutwardNormal.Y, 0.f).GetSafeNormal();

		const float LateralDist = FMath::Abs(FVector::DotProduct(Delta, Tangent));
		const float MaxLateral = (OpeningWidthCm * 0.5f) + LateralSlackCm;
		if (LateralDist > MaxLateral)
		{
			return false;
		}

		const float DepthDist = FMath::Abs(FVector::DotProduct(Delta, Outward));
		if (DepthDist > DepthSlackCm)
		{
			return false;
		}

		return true;
	}
}
