#include "Formation/FormationSectorRules.h"

#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationTypes.h"

namespace FormationSectorRules
{
	float Dwell(float S, float Exp)
	{
		return FMath::Sign(S) * FMath::Pow(FMath::Abs(S), Exp);
	}

	float ScanValue(float Center, float Amp, float Period, float Phase, float Time, float Exp)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Time) / FMath::Max(static_cast<double>(Period), 1e-3) + static_cast<double>(Phase);
		return Center + Amp * Dwell(static_cast<float>(FMath::Sin(Angle)), Exp);
	}

	static float SectorEchelonValue(int32 Index, int32 MemberCount, const FFormationConfig& Config)
	{
		// v2.2: with three the last man (Z) takes the rear sector (sector_4), so 5-7 o'clock stays watched.
		if (MemberCount == 3 && Index == 2)
		{
			return Config.EchelonSector4;
		}
		switch (Index)
		{
		case 0: return Config.EchelonSectorLead;
		case 1: return Config.EchelonSector2;
		case 2: return Config.EchelonSector3;
		default: return Config.EchelonSector4;
		}
	}

	float LookOffset(EFormationRole Role, EFormationKind Formation, EFormationMode Mode, int32 MemberCount, float TimeInPatrol,
		const TOptional<float>& PassageWidth, const FFormationConfig& Config)
	{
		const int32 N = MemberCount;
		const bool bK = Role == EFormationRole::Commander;
		const bool bP = Role == EFormationRole::Right;
		const bool bL = Role == EFormationRole::Left;
		const bool bZ = Role == EFormationRole::Rear;
		if (Mode == EFormationMode::Run || Mode == EFormationMode::Forward)
		{
			return 0.f;
		}
		if (Mode == EFormationMode::Wall || Formation == EFormationKind::Wall)
		{
			if (N == 2)
			{
				return bK ? WallPairLookDeg : -WallPairLookDeg;
			}
			if (bK)
			{
				return 0.f;
			}
			if (bL)
			{
				return Config.WallFlankDeg;
			}
			if (bP)
			{
				return -Config.WallFlankDeg;
			}
			if (N == 3)
			{
				// v2.2: the triad's Z on the left end looks 8 (by position, not role).
				return Config.WallFlankDeg;
			}
			return -Config.WallEndDeg;
		}

		const bool bHalt = Mode == EFormationMode::Halt;
		const float Exp = Config.ScanDwellExp;
		switch (Formation)
		{
		case EFormationKind::Diamond:
		case EFormationKind::Wedge:
		case EFormationKind::Blizzard:
		{
			if (N == 2 && bZ)
			{
				// The pair's partner sweeps 3 -> 6 -> 9 (through 6, never through 12).
				const float Center = 0.5f * (Config.PairScanFrom + Config.PairScanTo);
				const float Amp = 0.5f * (Config.PairScanTo - Config.PairScanFrom);
				return bHalt ? Center : ScanValue(Center, Amp, Config.PairScanPeriod, 0.f, TimeInPatrol, Exp);
			}
			if (bZ)
			{
				return 180.f;
			}
			if (bK)
			{
				return bHalt ? 0.f : ScanValue(0.f, Config.ScanAmpLead, Config.ScanPeriod, LeadScanPhase, TimeInPatrol, Exp);
			}
			const float Sign = bP ? 1.f : -1.f;
			if (bHalt)
			{
				return Sign * Config.HaltSideDeg;
			}
			return ScanValue(Sign * Config.ScanCenterSide, Config.ScanAmpSide, Config.ScanPeriod, bP ? 0.f : PI, TimeInPatrol, Exp);
		}
		case EFormationKind::Column:
			if (bK)
			{
				return 0.f;
			}
			if (bZ)
			{
				return 180.f;
			}
			if (PassageWidth.IsSet() && PassageWidth.GetValue() < Config.ColumnNarrowWidth)
			{
				// Narrow passage: nobody looks into the walls - P forward over the shoulder, L back at a slant.
				return bP ? Config.ColumnNarrowFwdDeg : -Config.ColumnNarrowBackDeg;
			}
			return (bP ? 1.f : -1.f) * Config.ColumnSideDeg;
		case EFormationKind::Line:
		{
			const float Inner = Config.LineFanInner;
			const float Outer = Config.LineFanOuter;
			if (N == 2)
			{
				return bK ? -Inner : Inner;
			}
			if (N == 3)
			{
				return bK ? 0.f : (bP ? Outer : -Outer);
			}
			return bK ? -Inner : (bP ? Inner : (bL ? -Outer : Outer));
		}
		case EFormationKind::EchelonRight:
		case EFormationKind::EchelonLeft:
		{
			const float OpenSide = Formation == EFormationKind::EchelonRight ? 1.f : -1.f;
			return OpenSide * SectorEchelonValue(FormationShapeRules::EchelonIndex(Formation, Role, N), N, Config);
		}
		case EFormationKind::Hedgehog:
			if (bK)
			{
				return 0.f;
			}
			if (N == 2)
			{
				return 180.f;
			}
			if (N == 3)
			{
				return bP ? 120.f : -120.f;
			}
			return bP ? 90.f : (bL ? -90.f : 180.f);
		default:
			return 0.f;
		}
	}

	float LookYaw(EFormationRole Role, EFormationKind Formation, EFormationMode Mode, int32 MemberCount, float TimeInPatrol, float LeaderYaw,
		const TOptional<float>& PassageWidth, const FFormationConfig& Config)
	{
		return FRotator::NormalizeAxis(LeaderYaw + LookOffset(Role, Formation, Mode, MemberCount, TimeInPatrol, PassageWidth, Config));
	}

	float ClearLook(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float CurrentYaw, const FFormationConfig& Config,
		const TOptional<float>& Held, const TOptional<FFormationLookLimit>& Limit, const TOptional<FVector2D>& Ahead)
	{
		using FormationMath::NormYaw;
		const double Reach = Config.LookWallDist;
		const double Half = Config.LookWallAngle;
		const double MinFree = Config.LookMinFreeDeg;
		const double Current = CurrentYaw;
		// v2.3 part 7: with a limit, no candidate outside +-Limit.Half of Limit.Yaw; a wanted yaw outside it is clamped to the nearer edge.
		const auto InLimit = [&Limit](double Angle)
		{
			return !Limit.IsSet() || FMath::Abs(NormYaw(Angle - Limit->Yaw)) <= static_cast<double>(Limit->Half) + 1e-6;
		};
		double Wanted = Yaw;
		if (Limit.IsSet() && !InLimit(Wanted))
		{
			Wanted = static_cast<double>(Limit->Yaw) + (NormYaw(Wanted - Limit->Yaw) < 0.0 ? -Limit->Half : Limit->Half);
		}
		// v2.3 part 8: the walls are also seen from Ahead (ignored when the step line from Pos to it crosses a wall body).
		TArray<FVector2D> From = { Pos };
		if (Ahead.IsSet() && FVector2D::Distance(Ahead.GetValue(), Pos) > 1e-6 && !FormationMath::SegmentBlocked(Pos, Ahead.GetValue(), Walls, 0.0))
		{
			From.Add(Ahead.GetValue());
		}
		TArray<double> Centers;
		int32 NowCount = 0; // Centers[0 .. NowCount) are seen from Pos itself (the hold test uses only those)
		for (int32 Origin = 0; Origin < From.Num(); ++Origin)
		{
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Closest = FormationMath::ClosestOnSegment(From[Origin], Wall.A, Wall.B);
				const double Distance = FVector2D::Distance(From[Origin], Closest);
				if (FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > Reach || Distance < 1e-6)
				{
					continue;
				}
				Centers.Add(FormationMath::YawOfD(Closest - From[Origin]));
				if (Origin == 0)
				{
					NowCount = Centers.Num();
				}
			}
		}
		const auto Blocked = [&Centers, Half](double Angle)
		{
			for (const double Center : Centers)
			{
				if (FMath::Abs(NormYaw(Angle - Center)) < Half - 1e-6)
				{
					return true;
				}
			}
			return false;
		};
		// The free arc a direction lies in, whole degrees both ways (Dir 0) or one way.
		const auto FreeWidth = [&Blocked](double Angle, int32 Dir)
		{
			if (Blocked(Angle))
			{
				return 0;
			}
			int32 Width = 0;
			for (const int32 Sign : { 1, -1 })
			{
				if (Dir != 0 && Sign != Dir)
				{
					continue;
				}
				for (int32 Step = 1; Step <= 360 && !Blocked(Angle + Sign * Step); ++Step)
				{
					++Width;
				}
			}
			return Width;
		};
		// How many wall directions a turn from Current to To (the short way) sweeps across.
		const auto Swept = [&Centers, Current](double To)
		{
			const double Turn = NormYaw(To - Current);
			int32 Count = 0;
			for (const double Center : Centers)
			{
				const double K = NormYaw(Center - Current);
				if (FMath::Sign(K) == FMath::Sign(Turn) && FMath::Abs(K) < FMath::Abs(Turn) - 1e-6)
				{
					++Count;
				}
			}
			return Count;
		};
		const auto Lookable = [&Blocked, &FreeWidth, MinFree](double Angle, int32 Dir) { return !Blocked(Angle) && FreeWidth(Angle, Dir) >= MinFree; };
		if (Lookable(Wanted, 0) && InLimit(Wanted))
		{
			return static_cast<float>(NormYaw(Wanted));
		}
		TOptional<double> Best;
		double BestScore = TNumericLimits<double>::Max();
		// The held look (hysteresis) while no wall direction is within LookWallAngle - LookHoldMarginDeg of it.
		const double HoldDeg = Config.LookHoldDeg;
		const double HoldHalf = Half - Config.LookHoldMarginDeg;
		if (Held.IsSet() && HoldDeg > 0.0 && InLimit(Held.GetValue()))
		{
			const double HeldYaw = Held.GetValue();
			bool bNearWall = false;
			for (int32 Index = 0; Index < NowCount; ++Index)
			{
				bNearWall |= FMath::Abs(NormYaw(HeldYaw - Centers[Index])) < HoldHalf - 1e-6;
			}
			if (!bNearWall)
			{
				Best = NormYaw(HeldYaw);
				BestScore = FMath::Abs(NormYaw(HeldYaw - Wanted)) - HoldDeg;
			}
		}
		// A free but narrow wanted yaw still competes, LookNarrowPenalty worse.
		const double NarrowPenalty = Config.LookNarrowPenalty;
		if (NarrowPenalty > 0.0 && !Blocked(Wanted) && InLimit(Wanted))
		{
			const double Score = NarrowPenalty + 360.0 * Swept(Wanted);
			if (Score < BestScore - 1e-9)
			{
				BestScore = Score;
				Best = NormYaw(Wanted);
			}
		}
		for (const double Center : Centers)
		{
			for (const int32 Out : { -1, 1 })
			{
				const double Edge = Center + Out * Half;
				if (Blocked(Edge) || !InLimit(Edge))
				{
					continue;
				}
				const bool bWide = Lookable(Edge, Out);
				if (!bWide && NarrowPenalty <= 0.0)
				{
					continue;
				}
				const double Score = FMath::Abs(NormYaw(Edge - Wanted)) + 360.0 * Swept(Edge) + (bWide ? 0.0 : NarrowPenalty);
				if (Score < BestScore - 1e-9)
				{
					BestScore = Score;
					Best = NormYaw(Edge);
				}
			}
		}
		if (Best.IsSet())
		{
			return static_cast<float>(Best.GetValue());
		}
		// Enclosed: the longest clear ray (every 5 deg from the wanted yaw, + before -, capped at 2 x LookWallDist; ties: the nearest).
		double BestRay = -1.0;
		double BestYaw = NormYaw(Wanted);
		for (int32 Step = 0; Step <= 36; ++Step)
		{
			for (const int32 Sign : { 1, -1 })
			{
				if (Sign < 0 && (Step == 0 || Step == 36))
				{
					continue;
				}
				const double Angle = Wanted + Sign * 5.0 * Step;
				if (!InLimit(Angle))
				{
					continue;
				}
				const TOptional<float> Hit = FormationTerrainRules::RayHit(Walls, Pos, FormationMath::DirOf(Angle), static_cast<float>(2.0 * Reach));
				const double Ray = FMath::Min(2.0 * Reach, Hit.IsSet() ? static_cast<double>(Hit.GetValue()) : TNumericLimits<double>::Max());
				if (Ray > BestRay + 1e-6)
				{
					BestRay = Ray;
					BestYaw = NormYaw(Angle);
				}
			}
		}
		return static_cast<float>(BestYaw);
	}

	bool Decided(EFormationRole Role, EFormationKind Formation, int32 MemberCount, const TOptional<float>& PassageWidth, const FFormationConfig& Config)
	{
		return Formation == EFormationKind::Column && MemberCount >= 3 && Role != EFormationRole::Rear && PassageWidth.IsSet()
			&& PassageWidth.GetValue() < Config.ColumnNarrowWidth;
	}

	float SquadPace(EFormationMode Mode, const FFormationConfig& Config)
	{
		switch (Mode)
		{
		case EFormationMode::Walk: return PatrolSpeed(Config.PaceWalkSpeed, Config);
		case EFormationMode::Run: return Config.PaceRunSpeed;
		default: return 0.f;
		}
	}

	float PatrolSpeed(float WalkSpeed, const FFormationConfig& Config)
	{
		return WalkSpeed * Config.PacePatrol;
	}

	EFormationFacingSource ResolveFacing(const FFormationFacingInputs& Inputs, float& OutYaw)
	{
		if (Inputs.CombatTargetYaw.IsSet())
		{
			OutYaw = Inputs.CombatTargetYaw.GetValue();
			return EFormationFacingSource::CombatTarget;
		}
		if (Inputs.FacingOrderYaw.IsSet())
		{
			OutYaw = Inputs.FacingOrderYaw.GetValue();
			return EFormationFacingSource::FacingOrder;
		}
		if (Inputs.CoverYaw.IsSet())
		{
			OutYaw = Inputs.CoverYaw.GetValue();
			return EFormationFacingSource::Cover;
		}
		if (Inputs.SectorYaw.IsSet())
		{
			OutYaw = Inputs.SectorYaw.GetValue();
			return EFormationFacingSource::FormationSector;
		}
		OutYaw = Inputs.MovementYaw;
		return EFormationFacingSource::Movement;
	}
}
