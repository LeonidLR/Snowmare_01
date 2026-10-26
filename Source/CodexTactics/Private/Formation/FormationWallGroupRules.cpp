#include "Formation/FormationWallGroupRules.h"

#include "Formation/FormationCoverRules.h"

namespace FormationWallGroup
{
	/** A wall's closest point to P and the body distance (centre line minus half the thickness, never below 0). */
	static double WallGroupBodyDistance(const FVector2D& P, const FFormationWall& Wall, FVector2D& OutClosest)
	{
		OutClosest = FormationMath::ClosestOnSegment(P, Wall.A, Wall.B);
		return FMath::Max(0.0, FVector2D::Distance(P, OutClosest) - 0.5 * Wall.Thickness);
	}

	/** Direction Yaw from Pos runs within HalfDeg of the closest point of a wall whose body is within Reach (AllocateStatic's blocked rule). */
	static bool WallGroupIsBlocked(const TArray<FFormationWall>& Walls, const FVector2D& Pos, double Yaw, double Reach, double HalfDeg)
	{
		for (const FFormationWall& Wall : Walls)
		{
			FVector2D Closest;
			const double Body = WallGroupBodyDistance(Pos, Wall, Closest);
			if (FVector2D::Distance(Pos, Closest) < 1e-6 || Body > Reach)
			{
				continue;
			}
			if (FMath::Abs(FormationMath::NormYaw(Yaw - FormationMath::YawOfD(Closest - Pos))) < HalfDeg)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * AllocateStatic's step 2 alone (no second rear duty - the first pass gave it): the widest open gap no cone covers (Fixed's cones, a
	 * direction blocked at any member's place is not open) to the free member whose bearing from the group centre is nearest its middle,
	 * looking at the middle; no gap left -> outward along his bearing (the nearest open degree). Whole degrees -179..180, as AllocateStatic.
	 */
	static TArray<FFormationRoleYaw> WallGroupFillGaps(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots,
		const TArray<FFormationRoleYaw>& Fixed, const TArray<EFormationRole>& Free, const FFormationConfig& Config)
	{
		const double FovHalf = 0.5 * Config.SightFov;
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FFormationSlot& Slot : Slots)
		{
			Centre += Slot.Position;
		}
		Centre *= Slots.Num() > 0 ? 1.0 / static_cast<double>(Slots.Num()) : 1.0;
		const auto IsBlockedAnywhere = [&](double Yaw)
		{
			for (const FFormationSlot& Slot : Slots)
			{
				if (WallGroupIsBlocked(Walls, Slot.Position, Yaw, Config.LookWallDist, Config.LookWallAngle))
				{
					return true;
				}
			}
			return false;
		};
		const auto Bearing = [&](EFormationRole Role)
		{
			const FFormationSlot* Slot = Slots.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; });
			return Slot && FVector2D::Distance(Slot->Position, Centre) > 1e-6 ? FormationMath::YawOfD(Slot->Position - Centre) : 0.0;
		};
		TArray<double> Cones;
		for (const FFormationRoleYaw& Look : Fixed)
		{
			Cones.Add(Look.Yaw);
		}
		TArray<bool> Blocked;
		Blocked.SetNum(360);
		for (int32 Index = 0; Index < 360; ++Index)
		{
			Blocked[Index] = IsBlockedAnywhere(static_cast<double>(Index - 179));
		}
		TArray<FFormationRoleYaw> Out;
		TArray<EFormationRole> Left = Free;
		while (Left.Num() > 0)
		{
			// The open, uncovered runs round the circle (From = the first degree, Width in degrees).
			TArray<bool> Open;
			Open.SetNum(360);
			bool bAnyClosed = false;
			for (int32 Index = 0; Index < 360; ++Index)
			{
				const double Degree = static_cast<double>(Index - 179);
				bool bCovered = Blocked[Index];
				for (const double Cone : Cones)
				{
					bCovered |= FMath::Abs(FormationMath::NormYaw(Degree - Cone)) <= FovHalf;
				}
				Open[Index] = !bCovered;
				bAnyClosed |= bCovered;
			}
			double BestMid = 0.0;
			int32 BestWidth = 0;
			if (!bAnyClosed)
			{
				BestWidth = 360;
			}
			else
			{
				const int32 Start = Open.IndexOfByKey(false);
				int32 Run = 0;
				for (int32 Step = 1; Step <= 360; ++Step)
				{
					const int32 Index = (Start + Step) % 360;
					Run = Open[Index] ? Run + 1 : 0;
					if (Run > BestWidth)
					{
						BestWidth = Run;
						BestMid = static_cast<double>(Index - 179) - 0.5 * (Run - 1);
					}
				}
			}
			EFormationRole Who = Left[0];
			double Yaw = 0.0;
			if (BestWidth > 0)
			{
				for (const EFormationRole Role : Left)
				{
					if (FMath::Abs(FormationMath::NormYaw(BestMid - Bearing(Role))) < FMath::Abs(FormationMath::NormYaw(BestMid - Bearing(Who))) - 1e-9)
					{
						Who = Role;
					}
				}
				Yaw = FormationMath::NormYaw(BestMid);
			}
			else
			{
				// No gap left: outward along his bearing, the nearest open degree.
				const double Want = Bearing(Who);
				double BestDistance = TNumericLimits<double>::Max();
				Yaw = Want;
				for (int32 Index = 0; Index < 360; ++Index)
				{
					const double Degree = static_cast<double>(Index - 179);
					const double Distance = FMath::Abs(FormationMath::NormYaw(Degree - Want));
					if (!Blocked[Index] && Distance < BestDistance - 1e-9)
					{
						BestDistance = Distance;
						Yaw = Degree;
					}
				}
			}
			Out.Add({ Who, static_cast<float>(Yaw) });
			Cones.Add(Yaw);
			Left.RemoveSingle(Who);
		}
		return Out;
	}

	TArray<FWallGroupLookResult> Allocate(const TArray<FFormationWall>& Walls, const TArray<FWallGroupMember>& Members, const FFormationConfig& Config)
	{
		TArray<FWallGroupLookResult> Out;
		TArray<FFormationSlot> Slots;
		TArray<FFormationRoleYaw> Fixed;
		TArray<EFormationRole> Free;
		for (const FWallGroupMember& Member : Members)
		{
			FFormationSlot Slot;
			Slot.Role = Member.Role;
			Slot.Position = Member.Pos;
			Slots.Add(Slot);
			if (Member.FixedYaw.IsSet())
			{
				const float Yaw = static_cast<float>(FormationMath::NormYaw(Member.FixedYaw.GetValue()));
				Fixed.Add({ Member.Role, Yaw });
				Out.Add({ Member.Role, Yaw, EWallGroupLook::Fixed });
			}
			else
			{
				Free.Add(Member.Role);
			}
		}
		const auto Find = [&Members](EFormationRole Role) { return Members.FindByPredicate([Role](const FWallGroupMember& Member) { return Member.Role == Role; }); };
		// One cover member's look snapped to his cover side per pass; the rest are allocated again round it (at most one pass per member).
		// The first pass gives the rear duty (AllocateStatic); later passes only fill the gaps left (no second rear man).
		bool bFirstPass = true;
		while (Free.Num() > 0)
		{
			const TArray<FFormationRoleYaw> Looks = bFirstPass ? FormationCoverRules::AllocateStatic(Walls, Slots, Fixed, Free, Config)
				: WallGroupFillGaps(Walls, Slots, Fixed, Free, Config);
			bFirstPass = false;
			bool bSnapped = false;
			for (const FFormationRoleYaw& Look : Looks)
			{
				const FWallGroupMember* Member = Find(Look.Role);
				if (!Member || !Member->bCoverPose)
				{
					continue;
				}
				const double Rel = FormationMath::NormYaw(static_cast<double>(Look.Yaw) - Member->WallNormalYaw);
				if (FMath::Abs(Rel) <= FacingOutMaxDeg)
				{
					continue; // away from the wall: the back-to-wall pose holds the allocated look as it is
				}
				const double Along = FormationMath::NormYaw(Member->WallNormalYaw + (Rel > 0.0 ? 90.0 : -90.0));
				if (WallGroupIsBlocked(Walls, Member->Pos, Along, Config.LookWallDist, Config.LookWallAngle))
				{
					continue; // an inner corner: along the wall he would stare into the other wall - the back-to-wall pose instead
				}
				Out.Add({ Look.Role, static_cast<float>(Along), EWallGroupLook::AlongWall });
				Fixed.Add({ Look.Role, static_cast<float>(Along) });
				Free.RemoveSingle(Look.Role);
				bSnapped = true;
				break;
			}
			if (bSnapped)
			{
				continue;
			}
			for (const FFormationRoleYaw& Look : Looks)
			{
				const FWallGroupMember* Member = Find(Look.Role);
				Out.Add({ Look.Role, static_cast<float>(FormationMath::NormYaw(Look.Yaw)),
					Member && Member->bCoverPose ? EWallGroupLook::FacingOut : EWallGroupLook::Free });
			}
			break;
		}
		return Out;
	}

	float LargestOpenGap(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions, const TArray<float>& LookYaws, const TArray<float>& FovDeg,
		float WallDistCm, float BlockHalfDeg, float StepDeg)
	{
		const double Step = FMath::Max(0.5, static_cast<double>(StepDeg));
		const int32 Samples = FMath::Max(1, FMath::RoundToInt32(360.0 / Step));
		TArray<bool> Open;
		Open.SetNum(Samples);
		int32 OpenCount = 0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double Yaw = FormationMath::NormYaw(-180.0 + Index * Step);
			bool bCovered = false;
			for (int32 Member = 0; Member < LookYaws.Num() && !bCovered; ++Member)
			{
				const double Half = 0.5 * (FovDeg.IsValidIndex(Member) ? FovDeg[Member] : 140.0);
				bCovered = FMath::Abs(FormationMath::NormYaw(Yaw - LookYaws[Member])) <= Half;
			}
			for (int32 Member = 0; Member < Positions.Num() && !bCovered; ++Member)
			{
				bCovered = WallGroupIsBlocked(Walls, Positions[Member], Yaw, WallDistCm, BlockHalfDeg);
			}
			Open[Index] = !bCovered;
			OpenCount += bCovered ? 0 : 1;
		}
		if (OpenCount == Samples)
		{
			return 360.f;
		}
		// The longest circular run of open samples, from a covered one.
		const int32 Start = Open.IndexOfByKey(false);
		int32 Best = 0;
		int32 Run = 0;
		for (int32 Offset = 1; Offset <= Samples; ++Offset)
		{
			Run = Open[(Start + Offset) % Samples] ? Run + 1 : 0;
			Best = FMath::Max(Best, Run);
		}
		return static_cast<float>(Best * Step);
	}

	float CoveredFraction(const TArray<float>& LookYaws, const TArray<float>& FovDeg, float CentreYaw, float HalfWidthDeg, float StepDeg)
	{
		const double Step = FMath::Max(0.5, static_cast<double>(StepDeg));
		int32 Total = 0;
		int32 Covered = 0;
		for (double Offset = -HalfWidthDeg; Offset <= HalfWidthDeg + 1e-6; Offset += Step)
		{
			const double Yaw = FormationMath::NormYaw(CentreYaw + Offset);
			++Total;
			for (int32 Member = 0; Member < LookYaws.Num(); ++Member)
			{
				const double Half = 0.5 * (FovDeg.IsValidIndex(Member) ? FovDeg[Member] : 140.0);
				if (FMath::Abs(FormationMath::NormYaw(Yaw - LookYaws[Member])) <= Half)
				{
					++Covered;
					break;
				}
			}
		}
		return Total > 0 ? static_cast<float>(Covered) / static_cast<float>(Total) : 0.f;
	}

	bool FacesWall(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float DistCm, float AngleDeg)
	{
		return WallGroupIsBlocked(Walls, Pos, Yaw, DistCm, AngleDeg);
	}
}
