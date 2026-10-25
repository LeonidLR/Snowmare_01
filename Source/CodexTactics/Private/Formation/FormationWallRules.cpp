#include "Formation/FormationWallRules.h"

namespace FormationWallRules
{
	/** The point of A -> B nearest to P (the parameter clamped to the segment). */
	static FVector2D WallClosestPoint(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = FVector2D::DotProduct(Edge, Edge);
		const double T = LengthSquared == 0.0 ? 0.0 : FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / LengthSquared, 0.0, 1.0);
		return A + Edge * T;
	}

	static FVector2D WallNormalAt(const FFormationWall& Wall, const FVector2D& ClickedSlot, const FVector2D& Closest)
	{
		FVector2D Normal = ClickedSlot - Closest;
		if (Normal.Size() < 1e-6)
		{
			const FVector2D Edge = Wall.B - Wall.A;
			Normal = FVector2D(-Edge.Y, Edge.X);
		}
		return Normal;
	}

	float OutwardYaw(const FFormationWall& Wall, const FVector2D& ClickedSlot)
	{
		return FormationMath::YawOf(WallNormalAt(Wall, ClickedSlot, WallClosestPoint(ClickedSlot, Wall.A, Wall.B)));
	}

	TArray<FFormationSlot> Layout(const FFormationWall& Wall, const FVector2D& ClickedSlot, int32 MemberCount, float RoomLeft, float RoomRight,
		const FFormationConfig& Config)
	{
		const FVector2D Closest = WallClosestPoint(ClickedSlot, Wall.A, Wall.B);
		const float NormalYaw = FormationMath::YawOf(WallNormalAt(Wall, ClickedSlot, Closest));
		const FVector2D Anchor = Closest + FormationMath::Forward(NormalYaw) * (0.5 * Wall.Thickness + Config.WallFaceOffset);
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const auto At = [&Anchor, &RightAlong, &Config](float Steps) { return Anchor + RightAlong * (Steps * Config.WallSpacing); };
		const auto Look = [NormalYaw](float Relative) { return FRotator::NormalizeAxis(NormalYaw + Relative); };
		const auto Make = [](EFormationRole Role, const FVector2D& Position, float Yaw)
		{
			FFormationSlot Slot;
			Slot.Role = Role;
			Slot.Position = Position;
			Slot.Yaw = Yaw;
			return Slot;
		};
		const bool bRightEnd = RoomRight >= RoomLeft;
		const float Flank = Config.WallFlankDeg;
		const float Neighbour = Config.WallNeighbourDeg;
		const float End = Config.WallEndDeg;
		using ER = EFormationRole;

		if (MemberCount <= 1)
		{
			return { Make(ER::Commander, Anchor, Look(0.f)) };
		}
		if (MemberCount == 2)
		{
			return bRightEnd ? TArray<FFormationSlot>{ Make(ER::Commander, Anchor, Look(PairLookDeg)), Make(ER::Rear, At(1.f), Look(-PairLookDeg)) }
				: TArray<FFormationSlot>{ Make(ER::Commander, Anchor, Look(-PairLookDeg)), Make(ER::Rear, At(-1.f), Look(PairLookDeg)) };
		}
		if (MemberCount == 3)
		{
			return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Flank)), Make(ER::Left, At(-1.f), Look(Flank)) };
		}
		if (bRightEnd)
		{
			return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Neighbour)), Make(ER::Left, At(-1.f), Look(Flank)),
				Make(ER::Rear, At(2.f), Look(-End)) };
		}
		return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Flank)), Make(ER::Left, At(-1.f), Look(Neighbour)),
			Make(ER::Rear, At(-2.f), Look(End)) };
	}

	void WallRoom(const FFormationWall& Wall, const FVector2D& ClickedSlot, float& OutRoomLeft, float& OutRoomRight)
	{
		const FVector2D Closest = WallClosestPoint(ClickedSlot, Wall.A, Wall.B);
		// As the reference: the normal from the nearest point (a slot ON the line gives yaw 0).
		const FVector2D RightAlong = FormationMath::Forward(FormationMath::YawOf(ClickedSlot - Closest) + 270.f);
		const double SideA = FVector2D::DotProduct(Wall.A - Closest, RightAlong);
		const double SideB = FVector2D::DotProduct(Wall.B - Closest, RightAlong);
		OutRoomRight = static_cast<float>(FMath::Max3(SideA, SideB, 0.0));
		OutRoomLeft = static_cast<float>(FMath::Max3(-SideA, -SideB, 0.0));
	}

	void BreakRoomTie(float& RoomLeft, float& RoomRight, float TravelLeft, float TravelRight)
	{
		if (RoomLeft == RoomRight && TravelLeft < TravelRight)
		{
			RoomLeft += 1.f;
		}
	}
}
