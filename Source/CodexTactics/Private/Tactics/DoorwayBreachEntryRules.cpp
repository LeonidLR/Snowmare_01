// R17-5 doorway breach entry: the pure geometry (DoorwayBreachEntryRules.h). Spec docs/design/DOORWAY_BREACH.md section 7. UE-only.

#include "Tactics/DoorwayBreachEntryRules.h"

namespace DoorwayBreachEntryRules
{
	FVector EntryDirection(const FVector& Outward)
	{
		return -FVector(Outward.X, Outward.Y, 0.f).GetSafeNormal();
	}

	FVector EntryRight(const FVector& Outward)
	{
		return FVector::CrossProduct(FVector::UpVector, EntryDirection(Outward)).GetSafeNormal();
	}

	int32 SideOf(const FVector& Position, const FVector& Center, const FVector& Outward)
	{
		return FVector::DotProduct(Position - Center, EntryRight(Outward)) < 0.0 ? -1 : 1;
	}

	FDoorwayBreachRoom MakeRoom(float ForwardWallCm, float RightWallCm, float LeftWallCm, float FloorZ)
	{
		FDoorwayBreachRoom Room;
		Room.DepthCm = FMath::Max(ForwardWallCm < 0.f ? RoomMaxDepthCm : FMath::Min(ForwardWallCm, RoomMaxDepthCm), RoomMinCm);
		Room.RightCm = FMath::Max(RightWallCm < 0.f ? RoomMaxSideCm : FMath::Min(RightWallCm, RoomMaxSideCm), RoomMinCm);
		Room.LeftCm = FMath::Max(LeftWallCm < 0.f ? RoomMaxSideCm : FMath::Min(LeftWallCm, RoomMaxSideCm), RoomMinCm);
		Room.FloorZ = FloorZ;
		Room.bValid = true;
		return Room;
	}

	bool IsInsideRoom(const FVector& Point, const FVector& Center, const FVector& Outward, const FDoorwayBreachRoom& Room)
	{
		if (!Room.bValid)
		{
			return false;
		}
		const double Depth = FVector::DotProduct(Point - Center, EntryDirection(Outward));
		const double Side = FVector::DotProduct(Point - Center, EntryRight(Outward));
		const double Up = Point.Z - Room.FloorZ;
		return Depth >= InsideMinDepthCm && Depth <= Room.DepthCm + RoomWallSlackCm && Side <= Room.RightCm + RoomWallSlackCm
			&& -Side <= Room.LeftCm + RoomWallSlackCm && Up >= -RoomBelowFloorCm && Up <= RoomHeightCm;
	}

	TArray<FDoorwayBreachEntryMove> PlanEntry(const FVector& Center, const FVector& Outward, float WidthCm, const TArray<EDoorwayBreachSlotRole>& Roles,
		const TArray<FVector>& Positions)
	{
		TArray<FDoorwayBreachEntryMove> Moves;
		const FVector In = EntryDirection(Outward);
		const FVector Right = EntryRight(Outward);
		if (In.IsNearlyZero() || Roles.Num() != Positions.Num())
		{
			return Moves;
		}
		const float HalfWidth = FMath::Max(WidthCm, 0.f) * 0.5f;
		// The jamb men: right-hand side first (man 1), then the left-hand side (man 2); a second man on one side follows the first one in.
		TArray<int32> Jambs;
		for (int32 Index = 0; Index < Roles.Num(); ++Index)
		{
			if (Roles[Index] == EDoorwayBreachSlotRole::LeftJamb || Roles[Index] == EDoorwayBreachSlotRole::RightJamb)
			{
				Jambs.Add(Index);
			}
		}
		Jambs.Sort([&](int32 A, int32 B)
		{
			const int32 SideA = SideOf(Positions[A], Center, Outward);
			const int32 SideB = SideOf(Positions[B], Center, Outward);
			if (SideA != SideB)
			{
				return SideA > SideB; // right first
			}
			return FVector::DistSquared2D(Positions[A], Center) < FVector::DistSquared2D(Positions[B], Center);
		});
		int32 Order = 0;
		TMap<int32, int32> UsedPerSide;
		for (const int32 Index : Jambs)
		{
			const int32 Side = SideOf(Positions[Index], Center, Outward);
			const int32 Already = UsedPerSide.FindOrAdd(Side)++;
			FDoorwayBreachEntryMove Move;
			Move.Participant = Index;
			// The corner man hooks along his side's inner wall; a second man on that side (the stacked fallback) stops short of him.
			Move.Destination = Center + In * (HookDepthCm - Already * 70.f) + Right * (Side * (HalfWidth + HookSideCm));
			Move.LookDirection = (In - Right * Side).GetSafeNormal(); // his sector: the deep corner across the room from his side
			Move.DelaySeconds = StaggerSeconds * Order++;
			Moves.Add(Move);
		}
		for (int32 Index = 0; Index < Roles.Num(); ++Index)
		{
			if (Roles[Index] == EDoorwayBreachSlotRole::CenterPoint)
			{
				FDoorwayBreachEntryMove Move;
				Move.Participant = Index;
				Move.Destination = Center + In * CentreDepthCm;
				Move.LookDirection = In;
				Move.DelaySeconds = StaggerSeconds * Order++;
				Move.bCrouch = true;
				Moves.Add(Move);
			}
		}
		return Moves; // rear security holds outside
	}
}
