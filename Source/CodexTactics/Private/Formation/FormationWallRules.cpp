#include "Formation/FormationWallRules.h"

#include "Formation/FormationCoverRules.h"
#include "Formation/FormationSectorRules.h"

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
			// v2.2: the triad is K, P, Z - the man on the LEFT end (Z) looks 8: the user's wall rule is by position.
			return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Flank)), Make(ER::Rear, At(-1.f), Look(Flank)) };
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

	FFormationSlideResult Slide(const FFormationWall& Wall, const TArray<FFormationSlot>& StartSlots, const FVector2D& LeaderPos, const FVector2D& Destination,
		float Time, const FFormationConfig& Config)
	{
		using FormationMath::NormYaw;
		FFormationSlideResult Result;
		if (StartSlots.Num() == 0)
		{
			return Result;
		}
		const FVector2D K0 = StartSlots[0].Position;
		const double Length = FVector2D::Distance(Wall.A, Wall.B);
		const FVector2D U = Length > 1e-6 ? (Wall.B - Wall.A) * (1.0 / Length) : FVector2D(1.0, 0.0);
		const FVector2D Foot = FormationMath::ClosestOnSegment(K0, Wall.A, Wall.B);
		FVector2D Normal = K0 - Foot;
		if (Normal.Size() < 1e-6)
		{
			Normal = FVector2D(-U.Y, U.X);
		}
		const FVector2D N = Normal * (1.0 / Normal.Size());
		const double NormalYaw = FormationMath::YawOfD(N);
		double DestT = 0.0;
		const FVector2D DestFoot = FormationMath::ClosestOnSegment(Destination, Wall.A, Wall.B, &DestT);
		const double Off = FVector2D::Distance(Destination, DestFoot) - 0.5 * Wall.Thickness;
		const double AlongDest = FVector2D::DotProduct(Destination - K0, U);
		const bool bSameSide = FVector2D::DotProduct(Destination - DestFoot, N) >= 0.0;
		const bool bInside = DestT > 0.0 && DestT < 1.0;
		Result.bActive = Off <= static_cast<double>(Config.SlideMaxOffWall) && bSameSide && bInside && FMath::Abs(AlongDest) >= SlideMin;
		Result.NormalYaw = static_cast<float>(NormalYaw);
		if (!Result.bActive)
		{
			Result.LeaderTarget = K0;
			Result.Targets = StartSlots;
			for (const FFormationSlot& Slot : StartSlots)
			{
				Result.Order.Add(Slot.Role);
			}
			return Result;
		}
		Result.LeaderTarget = K0 + U * AlongDest;
		const FVector2D D = U * FMath::Sign(AlongDest);
		const double Moved = FVector2D::DotProduct(LeaderPos - K0, D);
		const FVector2D Shift = D * FMath::Max(0.0, FMath::Min(FMath::Abs(AlongDest), Moved));
		TArray<FFormationSlot> Sorted = StartSlots;
		Sorted.StableSort([&D](const FFormationSlot& A, const FFormationSlot& B) { return FVector2D::DotProduct(A.Position, D) > FVector2D::DotProduct(B.Position, D); });
		for (const FFormationSlot& Slot : Sorted)
		{
			Result.Order.Add(Slot.Role);
		}
		const EFormationRole LeadRole = Result.Order[0];
		const EFormationRole TrailRole = Result.Order.Last();
		const double Out = Config.SlideLeadOutDeg;
		const double DYaw = FormationMath::YawOfD(D);
		// "Outward" = turned from the wall direction towards the normal.
		const auto TowardNormal = [NormalYaw, Out](double Yaw)
		{
			const double Sign = FMath::Sign(NormYaw(NormalYaw - Yaw));
			return NormYaw(Yaw + (Sign != 0.0 ? Sign : 1.0) * Out);
		};
		const auto PosOfRole = [&StartSlots, &Shift](EFormationRole Role)
		{
			const FFormationSlot* Slot = StartSlots.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
			return (Slot ? Slot->Position : FVector2D::ZeroVector) + Shift;
		};
		// Looks in insertion order (a later set of the same role keeps its place, as a JS Map).
		TArray<FFormationRoleYaw> Looks;
		const auto SetLook = [&Looks](EFormationRole Role, double Yaw)
		{
			if (FFormationRoleYaw* Existing = Looks.FindByPredicate([Role](const FFormationRoleYaw& Look) { return Look.Role == Role; }))
			{
				Existing->Yaw = static_cast<float>(Yaw);
			}
			else
			{
				Looks.Add({ Role, static_cast<float>(Yaw) });
			}
		};
		const auto HasLook = [&Looks](EFormationRole Role) { return Looks.ContainsByPredicate([Role](const FFormationRoleYaw& Look) { return Look.Role == Role; }); };
		SetLook(LeadRole, TowardNormal(DYaw));
		SetLook(TrailRole, TowardNormal(NormYaw(DYaw + 180.0)));
		const EFormationRole KRole = StartSlots[0].Role;
		if (!HasLook(KRole))
		{
			SetLook(KRole, NormYaw(NormalYaw + FormationSectorRules::ScanValue(0.f, Config.ScanAmpLead, Config.ScanPeriod, FormationSectorRules::LeadScanPhase, Time,
				Config.ScanDwellExp)));
		}
		TArray<EFormationRole> Middle;
		for (const EFormationRole Role : Result.Order)
		{
			if (!HasLook(Role))
			{
				Middle.Add(Role);
			}
		}
		if (Middle.Num() > 0)
		{
			TArray<FFormationSlot> Members;
			for (const FFormationSlot& Slot : StartSlots)
			{
				FFormationSlot Member;
				Member.Role = Slot.Role;
				Member.Position = PosOfRole(Slot.Role);
				Members.Add(Member);
			}
			const TArray<FFormationRoleYaw> Fixed = Looks;
			for (const FFormationRoleYaw& Look : FormationCoverRules::AllocateStatic({ Wall }, Members, Fixed, Middle, Config))
			{
				SetLook(Look.Role, Look.Yaw);
			}
		}
		Result.Lead = LeadRole;
		if (StartSlots.Num() >= 2)
		{
			Result.RearGuard = TrailRole;
		}
		for (const FFormationSlot& Slot : StartSlots)
		{
			FFormationSlot Target;
			Target.Role = Slot.Role;
			Target.Position = PosOfRole(Slot.Role);
			const FFormationRoleYaw* Look = Looks.FindByPredicate([&Slot](const FFormationRoleYaw& Candidate) { return Candidate.Role == Slot.Role; });
			Target.Yaw = Look ? Look->Yaw : Slot.Yaw;
			Result.Targets.Add(Target);
		}
		return Result;
	}

	void BreakRoomTie(float& RoomLeft, float& RoomRight, float TravelLeft, float TravelRight)
	{
		if (RoomLeft == RoomRight && TravelLeft < TravelRight)
		{
			RoomLeft += 1.f;
		}
	}
}
