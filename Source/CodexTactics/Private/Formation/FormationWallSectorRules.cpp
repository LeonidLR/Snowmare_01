#include "Formation/FormationWallSectorRules.h"

namespace FormationWallSectorRulesLocal
{
	/** The rings' points as seen by every member: a clear line and the direction (cf. FormationWallRules' one-ring probe). */
	struct FSectorRings
	{
		TArray<TArray<bool>> Visible;
		TArray<TArray<double>> Directions;
		/** Per member: directions of the walls next to him (nobody looks into them; EndLooks' rule). */
		TArray<TArray<double>> WallCenters;
		int32 Count = 0;
	};

	FSectorRings BuildSectorRings(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config)
	{
		FSectorRings Rings;
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FFormationSlot& Slot : Slots)
		{
			Centre += Slot.Position;
		}
		if (Slots.Num() > 0)
		{
			Centre *= 1.0 / static_cast<double>(Slots.Num());
		}
		const int32 PerRing = FMath::RoundToInt32(360.0 / FormationWallRules::WallEndStepDeg);
		TArray<FVector2D> Points;
		for (const double Radius : { FormationWallSectorRules::NearRingCm, static_cast<double>(Config.WallEndProbeDist) })
		{
			for (int32 Index = 0; Index < PerRing; ++Index)
			{
				Points.Add(Centre + FormationMath::DirOf(-180.0 + Index * FormationWallRules::WallEndStepDeg) * Radius);
			}
		}
		Rings.Count = Points.Num();
		Rings.Visible.SetNum(Slots.Num());
		Rings.Directions.SetNum(Slots.Num());
		Rings.WallCenters.SetNum(Slots.Num());
		for (int32 Member = 0; Member < Slots.Num(); ++Member)
		{
			const FVector2D& From = Slots[Member].Position;
			Rings.Visible[Member].SetNum(Rings.Count);
			Rings.Directions[Member].SetNum(Rings.Count);
			for (int32 Index = 0; Index < Rings.Count; ++Index)
			{
				Rings.Visible[Member][Index] = !FormationMath::SegmentBlocked(From, Points[Index], Walls, FormationWallRules::WallEndRayRadius);
				Rings.Directions[Member][Index] = FormationMath::YawOfD(Points[Index] - From);
			}
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Closest = FormationMath::ClosestOnSegment(From, Wall.A, Wall.B);
				const double Distance = FVector2D::Distance(From, Closest);
				if (Distance < 1e-6 || FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > static_cast<double>(Config.LookWallDist))
				{
					continue;
				}
				Rings.WallCenters[Member].Add(FormationMath::YawOfD(Closest - From));
			}
		}
		return Rings;
	}

	bool SectorLookIntoWall(const FSectorRings& Rings, int32 Member, double Yaw, const FFormationConfig& Config)
	{
		for (const double Center : Rings.WallCenters[Member])
		{
			if (FMath::Abs(FormationMath::NormYaw(Yaw - Center)) < static_cast<double>(Config.LookWallAngle) - 1e-6)
			{
				return true;
			}
		}
		return false;
	}

	bool SectorHolds(const FSectorRings& Rings, int32 Member, int32 Point, double Yaw, double Half)
	{
		return Rings.Visible[Member][Point] && FMath::Abs(FormationMath::NormYaw(Rings.Directions[Member][Point] - Yaw)) <= Half + 1e-9;
	}

	/** How many members hold each point with these looks. */
	TArray<int32> SectorHolders(const FSectorRings& Rings, const TArray<double>& Looks, double Half)
	{
		TArray<int32> Holders;
		Holders.Init(0, Rings.Count);
		for (int32 Member = 0; Member < Looks.Num(); ++Member)
		{
			for (int32 Point = 0; Point < Rings.Count; ++Point)
			{
				Holders[Point] += SectorHolds(Rings, Member, Point, Looks[Member], Half) ? 1 : 0;
			}
		}
		return Holders;
	}

	bool SectorOpen(const FSectorRings& Rings, int32 Point)
	{
		for (const TArray<bool>& Each : Rings.Visible)
		{
			if (Each[Point])
			{
				return true;
			}
		}
		return false;
	}

	/** Points Member would hold at Yaw that nobody else holds (Holders = the counts with his CURRENT look Current included). */
	int32 SectorOwnPoints(const FSectorRings& Rings, const TArray<int32>& Holders, int32 Member, double Current, double Yaw, double Half)
	{
		int32 Own = 0;
		for (int32 Point = 0; Point < Rings.Count; ++Point)
		{
			const int32 Others = Holders[Point] - (SectorHolds(Rings, Member, Point, Current, Half) ? 1 : 0);
			Own += Others == 0 && SectorHolds(Rings, Member, Point, Yaw, Half) ? 1 : 0;
		}
		return Own;
	}

	FFormationArcCoverage SectorCoverage(const FSectorRings& Rings, const TArray<int32>& Holders)
	{
		FFormationArcCoverage Result;
		for (int32 Point = 0; Point < Rings.Count; ++Point)
		{
			if (SectorOpen(Rings, Point))
			{
				++Result.Open;
				Result.Covered += Holders[Point] > 0 ? 1 : 0;
			}
		}
		Result.Pct = Result.Open > 0 ? 100.0 * static_cast<double>(Result.Covered) / static_cast<double>(Result.Open) : 100.0;
		return Result;
	}

	TArray<double> SectorLooksOf(const TArray<FFormationSlot>& Slots)
	{
		TArray<double> Looks;
		for (const FFormationSlot& Slot : Slots)
		{
			Looks.Add(static_cast<double>(Slot.Yaw));
		}
		return Looks;
	}

	/** The best look for Member (whole degrees, not into a wall): the most points nobody else holds; ties the smaller turn. */
	void SectorBestLook(const FSectorRings& Rings, const TArray<int32>& Holders, int32 Member, double Current, double Half, const FFormationConfig& Config,
		int32& OutOwn, double& OutYaw)
	{
		OutOwn = -1;
		OutYaw = Current;
		double BestTurn = 0.0;
		for (int32 Degree = -179; Degree <= 180; ++Degree)
		{
			const double Yaw = static_cast<double>(Degree);
			if (SectorLookIntoWall(Rings, Member, Yaw, Config))
			{
				continue;
			}
			const int32 Own = SectorOwnPoints(Rings, Holders, Member, Current, Yaw, Half);
			const double Turn = FMath::Abs(FormationMath::NormYaw(Yaw - Current));
			if (Own > OutOwn || (Own == OutOwn && Turn < BestTurn - 1e-9))
			{
				OutOwn = Own;
				OutYaw = Yaw;
				BestTurn = Turn;
			}
		}
	}
}

namespace FormationWallSectorRules
{
	using namespace FormationWallSectorRulesLocal;

	FFormationArcCoverage RingCoverage(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config)
	{
		const FSectorRings Rings = BuildSectorRings(Walls, Slots, Config);
		return SectorCoverage(Rings, SectorHolders(Rings, SectorLooksOf(Slots), 0.5 * static_cast<double>(Config.SightFov)));
	}

	int32 CountRedundantWhileOpen(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config,
		const TArray<EFormationRole>& Fixed)
	{
		const double Half = 0.5 * static_cast<double>(Config.SightFov);
		const FSectorRings Rings = BuildSectorRings(Walls, Slots, Config);
		const TArray<double> Looks = SectorLooksOf(Slots);
		const TArray<int32> Holders = SectorHolders(Rings, Looks, Half);
		const FFormationArcCoverage Now = SectorCoverage(Rings, Holders);
		if (Now.Open == Now.Covered)
		{
			return 0; // nothing open is unwatched: overlapping cones are no fault
		}
		int32 Redundant = 0;
		for (int32 Member = 1; Member < Slots.Num(); ++Member)
		{
			if (Fixed.Contains(Slots[Member].Role))
			{
				continue;
			}
			const int32 Keep = SectorOwnPoints(Rings, Holders, Member, Looks[Member], Looks[Member], Half);
			int32 Own = 0;
			double Yaw = 0.0;
			SectorBestLook(Rings, Holders, Member, Looks[Member], Half, Config, Own, Yaw);
			Redundant += Own - Keep >= MinGainPoints ? 1 : 0;
		}
		return Redundant;
	}

	FFormationWallSectorResult Balance(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, float NormalYaw, const FFormationConfig& Config,
		const TArray<EFormationRole>& Fixed, float CornerLookDeg)
	{
		using FormationMath::NormYaw;
		FFormationWallSectorResult Result;
		Result.Slots = Slots;
		Result.Fixes.Init(EFormationWallSectorFix::None, Slots.Num());
		const double Half = 0.5 * static_cast<double>(Config.SightFov);
		const FSectorRings Rings = BuildSectorRings(Walls, Slots, Config);
		TArray<double> Looks = SectorLooksOf(Slots);
		const FFormationArcCoverage Base = SectorCoverage(Rings, SectorHolders(Rings, Looks, Half));
		Result.BasePct = Base.Pct;
		Result.Pct = Base.Pct;
		Result.BaseUncovered = Base.Open - Base.Covered;
		Result.Uncovered = Result.BaseUncovered;
		if (Slots.Num() < 3)
		{
			return Result;
		}
		const double Normal = static_cast<double>(NormalYaw);
		TArray<bool> Movable;
		for (int32 Member = 0; Member < Slots.Num(); ++Member)
		{
			Movable.Add(Member != 0 && !Fixed.Contains(Slots[Member].Role));
		}

		// Rule 2: each end man at an open corner looks round it - the other end man's look mirrored when he looks round his already, else
		// CornerLookDeg off the normal: both ends alike.
		const FVector2D Along = FormationMath::DirOf(Normal + 90.0); // a look turned to +Along has a positive offset from the normal
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FFormationSlot& Slot : Slots)
		{
			Centre += Slot.Position / static_cast<double>(Slots.Num());
		}
		int32 Low = 0;
		int32 High = 0;
		for (int32 Member = 1; Member < Slots.Num(); ++Member)
		{
			const double At = FVector2D::DotProduct(Slots[Member].Position - Centre, Along);
			Low = At < FVector2D::DotProduct(Slots[Low].Position - Centre, Along) ? Member : Low;
			High = At > FVector2D::DotProduct(Slots[High].Position - Centre, Along) ? Member : High;
		}
		const double Spread = FVector2D::DotProduct(Slots[High].Position - Slots[Low].Position, Along);
		if (Low != High && Spread >= EndSpreadShare * static_cast<double>(Config.WallSpacing))
		{
			const auto RoundOwnCorner = [&](int32 Member, double Side)
			{
				const double Rel = NormYaw(Looks[Member] - Normal);
				return FMath::Abs(Rel) > 90.0 + 1e-6 && Rel * Side > 0.0;
			};
			// His corner is open: the wall ends (or a gap opens) right beside him - a line from him CornerReachCm along the wall to his side and
			// CornerBackCm behind the wall line is clear (on a long face, or at an inner corner, it runs into the wall) - and ring points round
			// it (behind the wall line, on his side) he has a clear line to.
			const FVector2D Out = FormationMath::DirOf(Normal);
			const auto CornerOpen = [&](int32 Member, double Side)
			{
				const FVector2D From = Slots[Member].Position;
				if (FormationMath::SegmentBlocked(From, From + Along * (Side * CornerReachCm) - Out * CornerBackCm, Walls, FormationWallRules::WallEndRayRadius))
				{
					return false;
				}
				int32 Seen = 0;
				for (int32 Point = 0; Point < Rings.Count; ++Point)
				{
					const double Rel = NormYaw(Rings.Directions[Member][Point] - Normal);
					Seen += Rings.Visible[Member][Point] && Rel * Side > 0.0 && FMath::Abs(Rel) >= CornerOpenFromDeg ? 1 : 0;
				}
				return Seen >= MinGainPoints;
			};
			for (const TPair<int32, int32>& Pair : { TPair<int32, int32>(Low, High), TPair<int32, int32>(High, Low) })
			{
				const int32 Mine = Pair.Key;
				const int32 Other = Pair.Value;
				const double MySide = Mine == High ? 1.0 : -1.0;
				if (!Movable[Mine] || RoundOwnCorner(Mine, MySide) || !CornerOpen(Mine, MySide))
				{
					continue;
				}
				const bool bMirror = RoundOwnCorner(Other, -MySide);
				double TargetRel = bMirror ? NormYaw(Normal - Looks[Other]) : MySide * static_cast<double>(CornerLookDeg);
				// A wall next to him that way (the next wall across a narrow gap): the nearest look still round the corner that is clear of it.
				while (FMath::Abs(TargetRel) > 91.0 && SectorLookIntoWall(Rings, Mine, Normal + TargetRel, Config))
				{
					TargetRel -= MySide;
				}
				const double Target = NormYaw(Normal + TargetRel);
				if (SectorLookIntoWall(Rings, Mine, Target, Config))
				{
					continue;
				}
				const TArray<int32> Holders = SectorHolders(Rings, Looks, Half);
				const int32 Now = SectorOwnPoints(Rings, Holders, Mine, Looks[Mine], Looks[Mine], Half);
				const int32 Then = SectorOwnPoints(Rings, Holders, Mine, Looks[Mine], Target, Half);
				if (Then > 0 && Then >= Now)
				{
					Looks[Mine] = Target;
					Result.Fixes[Mine] = bMirror ? EFormationWallSectorFix::Mirror : EFormationWallSectorFix::Corner;
				}
			}
			// Both round their corners but not alike (EndLooks' greedy picks one end first): one takes the other's mirror - the variant that
			// holds the most of the rings (never less than now), on a tie the one nearer CornerLookDeg.
			const double LowRel = NormYaw(Looks[Low] - Normal);
			const double HighRel = NormYaw(Looks[High] - Normal);
			if (RoundOwnCorner(Low, -1.0) && RoundOwnCorner(High, 1.0) && FMath::Abs(FMath::Abs(LowRel) - FMath::Abs(HighRel)) > SymmetryToleranceDeg)
			{
				int32 BestCovered = SectorCoverage(Rings, SectorHolders(Rings, Looks, Half)).Covered;
				int32 BestMember = INDEX_NONE;
				double BestYaw = 0.0;
				double BestOff = 0.0;
				for (const TPair<int32, int32>& Pair : { TPair<int32, int32>(Low, High), TPair<int32, int32>(High, Low) })
				{
					const int32 Mine = Pair.Key;
					if (!Movable[Mine])
					{
						continue;
					}
					const double Mirror = NormYaw(2.0 * Normal - Looks[Pair.Value]);
					if (SectorLookIntoWall(Rings, Mine, Mirror, Config))
					{
						continue;
					}
					TArray<double> Trial = Looks;
					Trial[Mine] = Mirror;
					const int32 Covered = SectorCoverage(Rings, SectorHolders(Rings, Trial, Half)).Covered;
					const double Off = FMath::Abs(FMath::Abs(NormYaw(Mirror - Normal)) - static_cast<double>(CornerLookDeg));
					if (Covered > BestCovered || (Covered == BestCovered && (BestMember == INDEX_NONE || Off < BestOff - 1e-9)))
					{
						BestCovered = Covered;
						BestMember = Mine;
						BestYaw = Mirror;
						BestOff = Off;
					}
				}
				if (BestMember != INDEX_NONE)
				{
					Looks[BestMember] = BestYaw;
					Result.Fixes[BestMember] = EFormationWallSectorFix::Mirror;
				}
			}
		}

		// Rule 3: no duplicated sector while an open one is unwatched.
		for (int32 Pass = 0; Pass < Slots.Num(); ++Pass)
		{
			const TArray<int32> Holders = SectorHolders(Rings, Looks, Half);
			const FFormationArcCoverage Now = SectorCoverage(Rings, Holders);
			if (Now.Open == Now.Covered)
			{
				break;
			}
			int32 BestMember = INDEX_NONE;
			int32 BestGain = 0;
			double BestYaw = 0.0;
			double BestTurn = 0.0;
			for (int32 Member = 1; Member < Slots.Num(); ++Member)
			{
				if (!Movable[Member] || Result.Fixes[Member] != EFormationWallSectorFix::None)
				{
					continue;
				}
				const int32 Keep = SectorOwnPoints(Rings, Holders, Member, Looks[Member], Looks[Member], Half);
				int32 Own = 0;
				double Yaw = Looks[Member];
				SectorBestLook(Rings, Holders, Member, Looks[Member], Half, Config, Own, Yaw);
				const int32 Gain = Own - Keep;
				const double Turn = FMath::Abs(NormYaw(Yaw - Looks[Member]));
				if (Gain >= MinGainPoints && (BestMember == INDEX_NONE || Gain > BestGain || (Gain == BestGain && Turn < BestTurn - 1e-9)))
				{
					BestMember = Member;
					BestGain = Gain;
					BestYaw = Yaw;
					BestTurn = Turn;
				}
			}
			if (BestMember == INDEX_NONE)
			{
				break;
			}
			Looks[BestMember] = BestYaw;
			Result.Fixes[BestMember] = EFormationWallSectorFix::Dedup;
		}

		const FFormationArcCoverage After = SectorCoverage(Rings, SectorHolders(Rings, Looks, Half));
		for (int32 Member = 0; Member < Slots.Num(); ++Member)
		{
			if (Result.Fixes[Member] != EFormationWallSectorFix::None)
			{
				Result.bChanged = true;
				Result.Slots[Member].Yaw = static_cast<float>(NormYaw(Looks[Member]));
			}
		}
		Result.Pct = After.Pct;
		Result.Uncovered = After.Open - After.Covered;
		return Result;
	}
}
