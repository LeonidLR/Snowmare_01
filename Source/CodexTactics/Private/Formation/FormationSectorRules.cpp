#include "Formation/FormationSectorRules.h"

#include "Formation/FormationShapeRules.h"

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

	static float SectorEchelonValue(int32 Index, const FFormationConfig& Config)
	{
		switch (Index)
		{
		case 0: return Config.EchelonSectorLead;
		case 1: return Config.EchelonSector2;
		case 2: return Config.EchelonSector3;
		default: return Config.EchelonSector4;
		}
	}

	float LookOffset(EFormationRole Role, EFormationKind Formation, EFormationMode Mode, int32 MemberCount, float TimeInPatrol, const FFormationConfig& Config)
	{
		const int32 N = MemberCount;
		const bool bK = Role == EFormationRole::Commander;
		const bool bP = Role == EFormationRole::Right;
		const bool bL = Role == EFormationRole::Left;
		const bool bZ = Role == EFormationRole::Rear;
		if (Mode == EFormationMode::Run)
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
			return OpenSide * SectorEchelonValue(FormationShapeRules::EchelonIndex(Formation, Role, N), Config);
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
		const FFormationConfig& Config)
	{
		return FRotator::NormalizeAxis(LeaderYaw + LookOffset(Role, Formation, Mode, MemberCount, TimeInPatrol, Config));
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
