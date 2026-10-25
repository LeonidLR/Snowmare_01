#include "Formation/FormationCoverRules.h"

namespace FormationCoverRules
{
	using FormationMath::NormYaw;

	/** A run of whole degrees, From..To (To may exceed 180 when it wraps). */
	struct FCoverGap
	{
		double From = 0.0;
		double To = 0.0;
	};

	/** Whole degree of sample index I (-179..180). */
	static double CoverDegree(int32 Index)
	{
		return static_cast<double>(Index - 179);
	}

	/** The runs of free degrees round the circle (rules.ts gap walk), sorted by From. */
	static TArray<FCoverGap> CoverGaps(const TArray<bool>& Free)
	{
		TArray<FCoverGap> Gaps;
		bool bAll = true;
		bool bAny = false;
		for (const bool bFree : Free)
		{
			bAll &= bFree;
			bAny |= bFree;
		}
		if (bAll)
		{
			Gaps.Add({ -179.0, 180.0 });
			return Gaps;
		}
		if (!bAny)
		{
			return Gaps;
		}
		// Walk the circle once from a covered degree; a gap's To runs past 180 when it wraps.
		const int32 Start = Free.IndexOfByKey(false);
		int32 Current = INDEX_NONE;
		for (int32 Step = 1; Step <= 360; ++Step)
		{
			const int32 Index = (Start + Step) % 360;
			if (!Free[Index])
			{
				Current = INDEX_NONE;
				continue;
			}
			if (Current != INDEX_NONE)
			{
				Gaps[Current].To += 1.0;
			}
			else
			{
				Current = Gaps.Add({ CoverDegree(Index), CoverDegree(Index) });
			}
		}
		Gaps.StableSort([](const FCoverGap& A, const FCoverGap& B) { return A.From < B.From; });
		return Gaps;
	}

	static double CoverWidth(const FCoverGap& Gap)
	{
		return Gap.To - Gap.From + 1.0;
	}

	static double CoverCentre(const FCoverGap& Gap)
	{
		return NormYaw(0.5 * (Gap.From + Gap.To));
	}

	static double CoverDwell(double S, double Exp)
	{
		return FMath::Sign(S) * FMath::Pow(FMath::Abs(S), Exp);
	}

	static double CoverScan(double Center, double Amp, double Period, double Phase, double Time, double Exp)
	{
		return Center + Amp * CoverDwell(FMath::Sin(2.0 * UE_DOUBLE_PI * Time / FMath::Max(Period, 1e-3) + Phase), Exp);
	}

	/** Body distance to a wall from P, and the closest point (rules.ts: distance minus half the thickness, never below 0). */
	static double CoverBodyDistance(const FVector2D& P, const FFormationWall& Wall, FVector2D& OutClosest)
	{
		OutClosest = FormationMath::ClosestOnSegment(P, Wall.A, Wall.B);
		return FMath::Max(0.0, FVector2D::Distance(P, OutClosest) - 0.5 * Wall.Thickness);
	}

	TOptional<FFormationCoverResult> Allocate(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float Heading,
		const TArray<FFormationCoverMember>& Members, EFormationMode Mode, float Time, const FFormationConfig& Config, const TArray<FFormationRoleYaw>& Fixed)
	{
		if (Members.Num() < 3)
		{
			return TOptional<FFormationCoverResult>();
		}
		const bool bHalt = Mode == EFormationMode::Halt;
		const double H = Heading;
		const double Exp = Config.ScanDwellExp;
		const double Lead = Config.ScanAmpLead;
		const double RearHalf = Config.RearShareHalf;
		const double WallAngle = Config.LookWallAngle;
		const double Reach = Config.LookWallDist;
		const double FovHalf = 0.5 * Config.SightFov;
		const auto FindFixed = [&Fixed](EFormationRole Role) { return Fixed.FindByPredicate([Role](const FFormationRoleYaw& Look) { return Look.Role == Role; }); };

		// Rear guard: the largest Back (ties -> the later) among the members whose look is NOT fixed; all fixed -> the farthest back anyway.
		int32 Rear = 1;
		for (int32 Index = 1; Index < Members.Num(); ++Index)
		{
			if (Members[Index].Back >= Members[Rear].Back - 1e-6)
			{
				Rear = Index;
			}
		}
		if (FindFixed(Members[Rear].Role))
		{
			int32 FreeRear = INDEX_NONE;
			for (int32 Index = 1; Index < Members.Num(); ++Index)
			{
				if (!FindFixed(Members[Index].Role) && (FreeRear == INDEX_NONE || Members[Index].Back >= Members[FreeRear].Back - 1e-6))
				{
					FreeRear = Index;
				}
			}
			if (FreeRear != INDEX_NONE)
			{
				Rear = FreeRear;
			}
		}
		const FFormationCoverMember& RearMember = Members[Rear];

		// Shift towards the open side when walls stand on one side of his slot only.
		const FVector2D Dir = FormationMath::DirOf(H);
		bool bWallLeft = false;
		bool bWallRight = false;
		for (const FFormationWall& Wall : Walls)
		{
			FVector2D Q;
			if (CoverBodyDistance(RearMember.Pos, Wall, Q) > Reach)
			{
				continue;
			}
			if (Dir.X * (Q.Y - RearMember.Pos.Y) - Dir.Y * (Q.X - RearMember.Pos.X) >= 0.0)
			{
				bWallRight = true;
			}
			else
			{
				bWallLeft = true;
			}
		}
		const double Shift = Config.RearWallShiftDeg;
		const double RearCentre = (bWallLeft && !bWallRight) ? 180.0 - Shift : ((bWallRight && !bWallLeft) ? -180.0 + Shift : 180.0);

		// Directions blocked by walls (relative), members-major.
		TArray<double> Blocked;
		for (const FFormationCoverMember& Member : Members)
		{
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Q = FormationMath::ClosestOnSegment(Member.Pos, Wall.A, Wall.B);
				const double Distance = FVector2D::Distance(Member.Pos, Q);
				if (Distance < 1e-6 || FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > Reach)
				{
					continue;
				}
				Blocked.Add(NormYaw(FormationMath::YawOfD(Q - Member.Pos) - H));
			}
		}
		const auto IsBlocked = [&Blocked, WallAngle](double R)
		{
			for (const double B : Blocked)
			{
				if (FMath::Abs(NormYaw(R - B)) < WallAngle)
				{
					return true;
				}
			}
			return false;
		};

		const FFormationRoleYaw* KFixed = FindFixed(Members[0].Role);
		const FFormationRoleYaw* RearFixed = FindFixed(RearMember.Role);
		TArray<double> FixedRel;
		for (const FFormationRoleYaw& Look : Fixed)
		{
			FixedRel.Add(NormYaw(Look.Yaw - H));
		}
		// The rear guard keeps 5-7 o'clock inside his cone at every moment: shift + search <= fov / 2 - RearSector.
		const double RearAmp = FMath::Max(0.0, FMath::Min(RearHalf, FovHalf - RearSector - FMath::Abs(NormYaw(RearCentre - 180.0))));
		const double KSure = bHalt ? FovHalf : FovHalf - Lead;
		const double RearSure = bHalt ? FovHalf : FovHalf - RearAmp;
		const auto IsCovered = [&](double R)
		{
			if (!KFixed && FMath::Abs(R) <= KSure)
			{
				return true;
			}
			if (!RearFixed && FMath::Abs(NormYaw(R - RearCentre)) <= RearSure)
			{
				return true;
			}
			for (const double F : FixedRel)
			{
				if (FMath::Abs(NormYaw(R - F)) <= FovHalf)
				{
					return true;
				}
			}
			return IsBlocked(R);
		};
		TArray<bool> Free;
		Free.SetNum(360);
		for (int32 Index = 0; Index < 360; ++Index)
		{
			Free[Index] = !IsCovered(CoverDegree(Index));
		}
		TArray<FCoverGap> Open = CoverGaps(Free);

		const auto Bearing = [&LeaderPos, H](const FFormationCoverMember& Member) { return NormYaw(FormationMath::YawOfD(Member.Pos - LeaderPos) - H); };
		TArray<int32> Others;
		for (int32 Index = 1; Index < Members.Num(); ++Index)
		{
			if (Index != Rear && !FindFixed(Members[Index].Role))
			{
				Others.Add(Index);
			}
		}
		struct FCoverShareD
		{
			EFormationRole Role;
			double From;
			double To;
			double Primary;
		};
		TArray<FCoverShareD> Shares;
		TArray<int32> FreeMembers = Others;
		while (FreeMembers.Num() > 0 && Open.Num() > 0)
		{
			int32 BestGap = INDEX_NONE;
			int32 BestMember = INDEX_NONE;
			double BestWidth = 0.0;
			double BestDistance = 0.0;
			for (int32 Gap = 0; Gap < Open.Num(); ++Gap)
			{
				for (int32 Slot = 0; Slot < FreeMembers.Num(); ++Slot)
				{
					const double Width = CoverWidth(Open[Gap]);
					const double Distance = FMath::Abs(NormYaw(CoverCentre(Open[Gap]) - Bearing(Members[FreeMembers[Slot]])));
					if (BestGap == INDEX_NONE || Width > BestWidth || (Width == BestWidth && Distance < BestDistance - 1e-9))
					{
						BestGap = Gap;
						BestMember = Slot;
						BestWidth = Width;
						BestDistance = Distance;
					}
				}
			}
			const FCoverGap Taken = Open[BestGap];
			Open.RemoveAt(BestGap);
			const int32 MemberIndex = FreeMembers[BestMember];
			FreeMembers.RemoveAt(BestMember);
			Shares.Add({ Members[MemberIndex].Role, Taken.From, Taken.To, CoverCentre(Taken) });
		}
		// Gaps nobody took join the nearest share (the union runs through the front, never through the rear).
		for (const FCoverGap& Gap : Open)
		{
			if (Shares.Num() == 0)
			{
				break;
			}
			if (CoverWidth(Gap) <= Config.CoverageMaxGapDeg)
			{
				continue;
			}
			int32 Near = 0;
			for (int32 Index = 1; Index < Shares.Num(); ++Index)
			{
				if (FMath::Abs(NormYaw(CoverCentre(Gap) - 0.5 * (Shares[Index].From + Shares[Index].To)))
					< FMath::Abs(NormYaw(CoverCentre(Gap) - 0.5 * (Shares[Near].From + Shares[Near].To))) - 1e-9)
				{
					Near = Index;
				}
			}
			if (Gap.To > 180.0 || Shares[Near].To > 180.0)
			{
				continue;
			}
			const double Lo = FMath::Min(Shares[Near].From, Gap.From);
			const double Hi = FMath::Max(Shares[Near].To, Gap.To);
			bool bCrossesWall = false;
			for (double R = FMath::CeilToDouble(Lo); R <= Hi && !bCrossesWall; R += 1.0)
			{
				bCrossesWall = IsBlocked(R);
			}
			if (bCrossesWall)
			{
				continue;
			}
			Shares[Near].From = Lo;
			Shares[Near].To = Hi;
		}
		// Members without a gap double K's or the rear guard's share, whichever is nearer their bearing.
		for (const int32 MemberIndex : FreeMembers)
		{
			const double B = Bearing(Members[MemberIndex]);
			const bool bToRear = FMath::Abs(NormYaw(B - RearCentre)) < FMath::Abs(B);
			const double Centre = bToRear ? RearCentre : 0.0;
			const double Half = bToRear ? RearSure : KSure;
			Shares.Add({ Members[MemberIndex].Role, Centre - Half, Centre + Half, NormYaw(Centre) });
		}

		const double Period = Config.ScanPeriod;
		const double MaxSweep = Config.CoverageMaxSweep;
		const double SideAmp = Config.ScanAmpSide;
		const double MaxGap = Config.CoverageMaxGapDeg;
		struct FCoverSweep
		{
			double Mid;
			double Amp;
			double Period;
			double Phase;
		};
		const auto SweepOf = [&](int32 ShareIndex)
		{
			// A share one cone can hold within max gap: sweep only as far as keeps its far side within that; wider: the full sweep.
			const FCoverShareD& Share = Shares[ShareIndex];
			const double Width = Share.To - Share.From;
			const double Mid = 0.5 * (Share.From + Share.To);
			const double Amp = Width <= 2.0 * FovHalf + MaxGap ? FMath::Min3(0.5 * Width, MaxSweep, FMath::Max(0.0, 0.5 * (2.0 * FovHalf + MaxGap - Width)))
				: FMath::Min(0.5 * Width, MaxSweep);
			return FCoverSweep{ Mid, Amp, Period * FMath::Max(1.0, Amp / SideAmp), (ShareIndex % 2) ? UE_DOUBLE_PI : 0.0 };
		};
		// A share across the front: K's search runs against that sweep. Otherwise K searches on his own (as LookYaw).
		int32 Front = INDEX_NONE;
		for (int32 Index = 0; Index < Shares.Num(); ++Index)
		{
			if (Shares[Index].From < -Lead && Shares[Index].To > Lead)
			{
				Front = Index;
				break;
			}
		}
		double KRel = 0.0;
		if (!bHalt)
		{
			if (Front != INDEX_NONE)
			{
				const FCoverSweep Sweep = SweepOf(Front);
				KRel = -Lead * CoverDwell(FMath::Sin(2.0 * UE_DOUBLE_PI * Time / Sweep.Period + Sweep.Phase), Exp);
			}
			else
			{
				KRel = CoverScan(0.0, Lead, Period, 1.3, Time, Exp);
			}
		}

		FFormationCoverResult Result;
		Result.RearGuard = RearMember.Role;
		const auto AddLook = [&Result](EFormationRole Role, double Yaw) { Result.Looks.Add({ Role, static_cast<float>(NormYaw(Yaw)) }); };
		AddLook(Members[0].Role, KFixed ? KFixed->Yaw : H + KRel);
		const double RearRel = bHalt ? RearCentre : CoverScan(RearCentre, RearAmp, Period, 0.5 * UE_DOUBLE_PI, Time, Exp);
		AddLook(RearMember.Role, RearFixed ? RearFixed->Yaw : H + RearRel);
		for (const FFormationRoleYaw& Look : Fixed)
		{
			if (Look.Role != Members[0].Role && Look.Role != RearMember.Role)
			{
				AddLook(Look.Role, Look.Yaw);
			}
		}
		for (const int32 MemberIndex : Others)
		{
			const int32 ShareIndex = Shares.IndexOfByPredicate([&](const FCoverShareD& Share) { return Share.Role == Members[MemberIndex].Role; });
			if (ShareIndex == INDEX_NONE)
			{
				continue;
			}
			const FCoverSweep Sweep = SweepOf(ShareIndex);
			const double Rel = bHalt ? Shares[ShareIndex].Primary : CoverScan(Sweep.Mid, Sweep.Amp, Sweep.Period, Sweep.Phase, Time, Exp);
			AddLook(Members[MemberIndex].Role, H + Rel);
		}
		for (const FCoverShareD& Share : Shares)
		{
			Result.Shares.Add({ Share.Role, static_cast<float>(Share.From), static_cast<float>(Share.To), static_cast<float>(Share.Primary) });
		}
		return Result;
	}

	TArray<FFormationRoleYaw> AllocateStatic(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Members, const TArray<FFormationRoleYaw>& Fixed,
		const TArray<EFormationRole>& Free, const FFormationConfig& Config)
	{
		const double FovHalf = 0.5 * Config.SightFov;
		const double Reach = Config.LookWallDist;
		const double WallAngle = Config.LookWallAngle;
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FFormationSlot& Member : Members)
		{
			Centre += Member.Position;
		}
		if (Members.Num() > 0)
		{
			Centre *= 1.0 / static_cast<double>(Members.Num());
		}
		TArray<double> BlockedYaws;
		for (const FFormationSlot& Member : Members)
		{
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Q = FormationMath::ClosestOnSegment(Member.Position, Wall.A, Wall.B);
				const double Distance = FVector2D::Distance(Member.Position, Q);
				if (Distance < 1e-6 || FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > Reach)
				{
					continue;
				}
				BlockedYaws.Add(FormationMath::YawOfD(Q - Member.Position));
			}
		}
		const auto IsBlocked = [&BlockedYaws, WallAngle](double A)
		{
			for (const double B : BlockedYaws)
			{
				if (FMath::Abs(NormYaw(A - B)) < WallAngle)
				{
					return true;
				}
			}
			return false;
		};
		TArray<double> Cones;
		for (const FFormationRoleYaw& Look : Fixed)
		{
			Cones.Add(Look.Yaw);
		}
		const auto IsCovered = [&Cones, FovHalf](double A)
		{
			for (const double Y : Cones)
			{
				if (FMath::Abs(NormYaw(A - Y)) <= FovHalf)
				{
					return true;
				}
			}
			return false;
		};
		const auto NearestOpen = [&IsBlocked](double A)
		{
			double Best = A;
			double BestDistance = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < 360; ++Index)
			{
				const double D = CoverDegree(Index);
				if (IsBlocked(D))
				{
					continue;
				}
				const double Distance = FMath::Abs(NormYaw(D - A));
				if (Distance < BestDistance - 1e-9)
				{
					BestDistance = Distance;
					Best = D;
				}
			}
			return NormYaw(Best);
		};
		const auto Bearing = [&Members, &Centre](EFormationRole Role)
		{
			const FFormationSlot* Member = Members.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
			return (Member && FVector2D::Distance(Member->Position, Centre) > 1e-6) ? FormationMath::YawOfD(Member->Position - Centre) : 0.0;
		};
		TArray<FFormationRoleYaw> Out;
		TArray<EFormationRole> Left = Free;
		const auto Take = [&](EFormationRole Role, double Yaw)
		{
			Out.Add({ Role, static_cast<float>(NormYaw(Yaw)) });
			Cones.Add(Yaw);
			Left.RemoveSingle(Role);
		};
		if (Members.Num() >= 3 && Fixed.Num() > 0 && Left.Num() > 0)
		{
			double SX = 0.0;
			double SY = 0.0;
			for (const FFormationRoleYaw& Look : Fixed)
			{
				SX += FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Look.Yaw)));
				SY += FMath::Sin(FMath::DegreesToRadians(static_cast<double>(Look.Yaw)));
			}
			const double FrontYaw = FMath::Sqrt(SX * SX + SY * SY) > 1e-6 ? FMath::RadiansToDegrees(FMath::Atan2(SY, SX)) : static_cast<double>(Fixed[0].Yaw);
			const double RearYaw = NormYaw(FrontYaw + 180.0);
			EFormationRole Who = Left[0];
			for (const EFormationRole Role : Left)
			{
				if (FMath::Abs(NormYaw(Bearing(Role) - RearYaw)) < FMath::Abs(NormYaw(Bearing(Who) - RearYaw)) - 1e-9)
				{
					Who = Role;
				}
			}
			Take(Who, IsBlocked(RearYaw) ? NearestOpen(RearYaw) : RearYaw);
		}
		while (Left.Num() > 0)
		{
			TArray<bool> OpenDegrees;
			OpenDegrees.SetNum(360);
			for (int32 Index = 0; Index < 360; ++Index)
			{
				const double D = CoverDegree(Index);
				OpenDegrees[Index] = !IsBlocked(D) && !IsCovered(D);
			}
			const TArray<FCoverGap> Gaps = CoverGaps(OpenDegrees);
			if (Gaps.Num() == 0)
			{
				const TArray<EFormationRole> Rest = Left;
				for (const EFormationRole Role : Rest)
				{
					Take(Role, NearestOpen(Bearing(Role)));
				}
				break;
			}
			int32 BestGap = INDEX_NONE;
			EFormationRole BestRole = Left[0];
			double BestWidth = 0.0;
			double BestDistance = 0.0;
			for (int32 Gap = 0; Gap < Gaps.Num(); ++Gap)
			{
				for (const EFormationRole Role : Left)
				{
					const double Width = CoverWidth(Gaps[Gap]);
					const double Mid = NormYaw(0.5 * (Gaps[Gap].From + Gaps[Gap].To));
					const double Distance = FMath::Abs(NormYaw(Mid - Bearing(Role)));
					if (BestGap == INDEX_NONE || Width > BestWidth || (Width == BestWidth && Distance < BestDistance - 1e-9))
					{
						BestGap = Gap;
						BestRole = Role;
						BestWidth = Width;
						BestDistance = Distance;
					}
				}
			}
			Take(BestRole, NormYaw(0.5 * (Gaps[BestGap].From + Gaps[BestGap].To)));
		}
		TArray<FFormationRoleYaw> Ordered;
		for (const EFormationRole Role : Free)
		{
			if (const FFormationRoleYaw* Look = Out.FindByPredicate([Role](const FFormationRoleYaw& Candidate) { return Candidate.Role == Role; }))
			{
				Ordered.Add(*Look);
			}
		}
		return Ordered;
	}
}
