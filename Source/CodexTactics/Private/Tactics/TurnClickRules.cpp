#include "Tactics/TurnClickRules.h"

namespace TurnClickRules
{
	ETurnClickAction ResolveClick(EGorkyOccupantType Occupant, bool bAttackOrder, bool bAdjacent, bool bAttackMode)
	{
		switch (Occupant)
		{
		case EGorkyOccupantType::Squad:
			return bAttackOrder ? ETurnClickAction::None : ETurnClickAction::SelectUnit;
		case EGorkyOccupantType::Enemy:
			return ETurnClickAction::Attack;
		case EGorkyOccupantType::Barrel:
		case EGorkyOccupantType::Barricade:
			if (bAttackOrder)
			{
				return ETurnClickAction::Attack;
			}
			return bAdjacent ? ETurnClickAction::Relocate : ETurnClickAction::NeedApproach;
		case EGorkyOccupantType::Turret:
			if (bAttackOrder)
			{
				return ETurnClickAction::None; // the squad's own turret is no target
			}
			return bAdjacent ? ETurnClickAction::Relocate : ETurnClickAction::NeedApproach;
		default:
			if (bAttackOrder)
			{
				return ETurnClickAction::NoTargetForAttackOrder;
			}
			return bAttackMode ? ETurnClickAction::NoTargetInAttackMode : ETurnClickAction::Walk;
		}
	}

	bool IsHeldUnitDisplaced(const FVector& Location, const FVector& Anchor, float ToleranceCm)
	{
		return FVector::DistSquared2D(Location, Anchor) > FMath::Square(FMath::Max(ToleranceCm, 0.f));
	}

	bool IsInsideGrid(const FVector& Point, const FVector& Origin, int32 Cells, float CellSize)
	{
		const float Size = Cells * CellSize;
		const float X = Point.X - Origin.X;
		const float Y = Point.Y - Origin.Y;
		return X >= 0.f && Y >= 0.f && X < Size && Y < Size;
	}

	float GetHeldUnitTolerance(float CellSize)
	{
		return CellSize * 0.5f;
	}
}
