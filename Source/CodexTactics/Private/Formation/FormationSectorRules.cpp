#include "Formation/FormationSectorRules.h"

#include "Formation/FormationShapeRules.h"
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

	float ClearLook(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float CurrentYaw, const FFormationConfig& Config)
	{
		using FormationMath::NormYaw;
		const double Reach = Config.LookWallDist;
		const double Half = Config.LookWallAngle;
		const double Wanted = Yaw;
		const double Current = CurrentYaw;
		TArray<double> Centers;
		for (const FFormationWall& Wall : Walls)
		{
			const FVector2D Closest = FormationMath::ClosestOnSegment(Pos, Wall.A, Wall.B);
			const double Distance = FVector2D::Distance(Pos, Closest);
			if (FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > Reach || Distance < 1e-6)
			{
				continue;
			}
			Centers.Add(FormationMath::YawOfD(Closest - Pos));
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
		if (!Blocked(Wanted))
		{
			return static_cast<float>(NormYaw(Wanted));
		}
		// A turn from Current to To (the short way) sweeps across a wall direction strictly inside it.
		const auto Sweeps = [&Centers, Current](double To)
		{
			const double Turn = NormYaw(To - Current);
			for (const double Center : Centers)
			{
				const double K = NormYaw(Center - Current);
				if (FMath::Sign(K) == FMath::Sign(Turn) && FMath::Abs(K) < FMath::Abs(Turn) - 1e-6)
				{
					return true;
				}
			}
			return false;
		};
		TOptional<double> Best;
		double BestScore = TNumericLimits<double>::Max();
		for (const double Center : Centers)
		{
			for (const double Edge : { Center - Half, Center + Half })
			{
				if (Blocked(Edge))
				{
					continue;
				}
				const double Score = FMath::Abs(NormYaw(Edge - Wanted)) + (Sweeps(Edge) ? 360.0 : 0.0);
				if (Score < BestScore - 1e-9)
				{
					BestScore = Score;
					Best = NormYaw(Edge);
				}
			}
		}
		return static_cast<float>(Best.IsSet() ? Best.GetValue() : NormYaw(Wanted));
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
