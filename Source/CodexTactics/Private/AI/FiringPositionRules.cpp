// Firing positions (smart enemies T-05): pure rules, no world access. See FiringPositionRules.h.

#include "AI/FiringPositionRules.h"

#include "AI/PmcDefenceRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FiringPositionRulesLocal
{
	float FiringClamp01(float Value)
	{
		return FMath::Clamp(Value, 0.f, 1.f);
	}

	void FiringReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = static_cast<float>(Value);
		}
	}

	/** Metres in the file, centimetres in the struct. */
	void FiringReadMetres(const FJsonObject& Json, const TCHAR* Key, float& OutCm)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			OutCm = static_cast<float>(Value) * 100.f;
		}
	}
}

float FFiringPositionConfig::MuzzleHeight(EOperativeStance Stance) const
{
	switch (Stance)
	{
	case EOperativeStance::Prone:
		return MuzzleProneCm;
	case EOperativeStance::Crouching:
		return MuzzleCrouchingCm;
	default:
		return MuzzleStandingCm;
	}
}

namespace FiringPositionRules
{
	const TCHAR* KindName(EFiringPositionKind Kind)
	{
		switch (Kind)
		{
		case EFiringPositionKind::Corner: return TEXT("corner");
		case EFiringPositionKind::LowCover: return TEXT("low_cover");
		case EFiringPositionKind::Window: return TEXT("window");
		case EFiringPositionKind::RoofEdge: return TEXT("roof_edge");
		case EFiringPositionKind::StairHead: return TEXT("stair_head");
		case EFiringPositionKind::ObstacleTop: return TEXT("obstacle_top");
		}
		return TEXT("?");
	}

	const TCHAR* StanceName(EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Prone: return TEXT("prone");
		case EOperativeStance::Crouching: return TEXT("crouching");
		default: return TEXT("standing");
		}
	}

	FFiringPositionConfig ConfigFromJson(const FJsonObject& Block, const FFiringPositionConfig& Base)
	{
		using namespace FiringPositionRulesLocal;
		FFiringPositionConfig Out = Base;
		FiringReadNumber(Block, TEXT("crossfire_min_deg"), Out.CrossfireMinDeg);
		Out.CrossfireMinDeg = FMath::Clamp(Out.CrossfireMinDeg, 0.f, 180.f);
		FiringReadNumber(Block, TEXT("line_clear_cm"), Out.LineClearCm);
		Out.LineClearCm = FMath::Max(Out.LineClearCm, 0.f);
		float Samples = static_cast<float>(Out.AreaSamples);
		FiringReadNumber(Block, TEXT("area_samples"), Samples);
		Out.AreaSamples = FMath::RoundToInt(Samples) >= 9 ? 9 : 1;
		FiringReadNumber(Block, TEXT("sample_height_cm"), Out.SampleHeightCm);
		Out.SampleHeightCm = FMath::Clamp(Out.SampleHeightCm, 0.f, 200.f);
		FiringReadNumber(Block, TEXT("min_covered_share"), Out.MinCoveredShare);
		Out.MinCoveredShare = FiringClamp01(Out.MinCoveredShare);
		FiringReadNumber(Block, TEXT("rim_step_cm"), Out.RimStepCm);
		Out.RimStepCm = FMath::Clamp(Out.RimStepCm, 1.f, 100.f);
		FiringReadNumber(Block, TEXT("rim_step_max_cm"), Out.RimStepMaxCm);
		Out.RimStepMaxCm = FMath::Clamp(Out.RimStepMaxCm, 0.f, 300.f);
		FiringReadNumber(Block, TEXT("rim_search_max_cm"), Out.RimSearchMaxCm);
		Out.RimSearchMaxCm = FMath::Clamp(Out.RimSearchMaxCm, 0.f, 20000.f);
		if (const TSharedPtr<FJsonObject>* Muzzle = nullptr; Block.TryGetObjectField(TEXT("muzzle_height_cm"), Muzzle) && Muzzle && Muzzle->IsValid())
		{
			FiringReadNumber(**Muzzle, TEXT("standing"), Out.MuzzleStandingCm);
			FiringReadNumber(**Muzzle, TEXT("crouching"), Out.MuzzleCrouchingCm);
			FiringReadNumber(**Muzzle, TEXT("prone"), Out.MuzzleProneCm);
		}
		FiringReadNumber(Block, TEXT("w_danger"), Out.DangerWeight);
		Out.DangerWeight = FiringClamp01(Out.DangerWeight);
		FiringReadNumber(Block, TEXT("roof_protect"), Out.RoofProtect);
		FiringReadNumber(Block, TEXT("high_ground"), Out.HighGroundPerLevel);
		FiringReadNumber(Block, TEXT("window_protect"), Out.WindowProtect);
		FiringReadNumber(Block, TEXT("escape_second_way"), Out.EscapeSecondWay);
		FiringReadNumber(Block, TEXT("escape_one_way"), Out.EscapeOneWay);
		FiringReadNumber(Block, TEXT("escape_ladder_only"), Out.EscapeLadderOnly);
		FiringReadMetres(Block, TEXT("min_spacing_m"), Out.MinSpacingCm);
		FiringReadNumber(Block, TEXT("spacing_penalty"), Out.SpacingPenalty);
		FiringReadNumber(Block, TEXT("scan_step_cm"), Out.ScanStepCm);
		Out.ScanStepCm = FMath::Max(Out.ScanStepCm, 100.f);
		float Rays = static_cast<float>(Out.ScanRays);
		FiringReadNumber(Block, TEXT("scan_rays"), Rays);
		Out.ScanRays = FMath::Clamp(FMath::RoundToInt(Rays), 4, 16);
		FiringReadNumber(Block, TEXT("scan_reach_cm"), Out.ScanReachCm);
		Out.ScanReachCm = FMath::Max(Out.ScanReachCm, 100.f);
		float MaxPoints = static_cast<float>(Out.MaxScanPoints);
		FiringReadNumber(Block, TEXT("max_scan_points"), MaxPoints);
		Out.MaxScanPoints = FMath::Max(1, FMath::RoundToInt(MaxPoints));
		float MaxSlots = static_cast<float>(Out.MaxCoverSlots);
		FiringReadNumber(Block, TEXT("max_cover_slots"), MaxSlots);
		Out.MaxCoverSlots = FMath::Max(1, FMath::RoundToInt(MaxSlots));
		FiringReadNumber(Block, TEXT("min_cover_quality"), Out.MinCoverQuality);
		Out.MinCoverQuality = FiringClamp01(Out.MinCoverQuality);
		FiringReadNumber(Block, TEXT("scan_dense_cm"), Out.ScanDenseCm);
		Out.ScanDenseCm = FMath::Max(Out.ScanDenseCm, 0.f);
		float FarStride = static_cast<float>(Out.ScanFarStride);
		FiringReadNumber(Block, TEXT("scan_far_stride"), FarStride);
		Out.ScanFarStride = FMath::Clamp(FMath::RoundToInt(FarStride), 1, 8);
		FiringReadNumber(Block, TEXT("band_min_factor"), Out.BandMinFactor);
		Out.BandMinFactor = FMath::Clamp(Out.BandMinFactor, 0.f, 1.f);
		FiringReadNumber(Block, TEXT("band_max_factor"), Out.BandMaxFactor);
		Out.BandMaxFactor = FMath::Clamp(Out.BandMaxFactor, 0.1f, 4.f);
		FiringReadNumber(Block, TEXT("band_max_factor_tier2"), Out.BandMaxFactorTier2);
		Out.BandMaxFactorTier2 = FMath::Clamp(Out.BandMaxFactorTier2, Out.BandMaxFactor, 6.f); // the fallback band is never narrower than the first
		FiringReadNumber(Block, TEXT("area_radius_floor_cm"), Out.AreaRadiusFloorCm);
		Out.AreaRadiusFloorCm = FMath::Max(Out.AreaRadiusFloorCm, 0.f);
		FiringReadNumber(Block, TEXT("roof_reach_factor"), Out.RoofReachFactor);
		Out.RoofReachFactor = FMath::Clamp(Out.RoofReachFactor, 0.f, 1.f);
		FiringReadNumber(Block, TEXT("kill_zone_reach_factor"), Out.KillZoneReachFactor);
		Out.KillZoneReachFactor = FMath::Clamp(Out.KillZoneReachFactor, 0.f, 2.f);
		FiringReadNumber(Block, TEXT("default_reach_cm"), Out.DefaultReachCm);
		Out.DefaultReachCm = FMath::Max(Out.DefaultReachCm, 100.f);
		FiringReadNumber(Block, TEXT("forward_margin_cm"), Out.ForwardMarginCm);
		Out.ForwardMarginCm = FMath::Max(Out.ForwardMarginCm, 0.f);
		FiringReadNumber(Block, TEXT("reach_margin_s"), Out.ReachMarginS);
		Out.ReachMarginS = FMath::Clamp(Out.ReachMarginS, 0.f, 10.f);
		FiringReadNumber(Block, TEXT("attacker_speed_factor"), Out.AttackerSpeedFactor);
		Out.AttackerSpeedFactor = FMath::Clamp(Out.AttackerSpeedFactor, 0.1f, 5.f);
		float PathQueries = static_cast<float>(Out.MaxPathQueries);
		FiringReadNumber(Block, TEXT("max_path_queries"), PathQueries);
		Out.MaxPathQueries = FMath::Clamp(FMath::RoundToInt(PathQueries), 1, 64);
		bool bReach = Out.bReachBeforeContact;
		if (Block.TryGetBoolField(TEXT("reach_before_contact"), bReach))
		{
			Out.bReachBeforeContact = bReach;
		}
		bool bUse = Out.bDefenceUse;
		if (Block.TryGetBoolField(TEXT("defence_use"), bUse))
		{
			Out.bDefenceUse = bUse;
		}
		return Out;
	}

	FFiringPositionConfig LoadConfig()
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_commander.json");
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			return FFiringPositionConfig();
		}
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Json->TryGetObjectField(TEXT("firing_positions"), Block) || !Block || !Block->IsValid())
		{
			return FFiringPositionConfig();
		}
		return ConfigFromJson(**Block);
	}

	float ScoreF2(const FFiringScoreInputs& Inputs, float DangerWeight)
	{
		using namespace FiringPositionRulesLocal;
		if (Inputs.Fire <= 0.f)
		{
			return 0.f; // cannot fire: not a position
		}
		return FiringClamp01(Inputs.Fire) * FiringClamp01(Inputs.Protect) * FiringClamp01(Inputs.Escape) * (1.f - FiringClamp01(DangerWeight) * FiringClamp01(Inputs.Danger))
			* FiringClamp01(Inputs.Spacing) * FiringClamp01(Inputs.RoleFit);
	}

	float CoveredShare(int32 Hits, int32 Samples)
	{
		return Samples > 0 ? FiringPositionRulesLocal::FiringClamp01(static_cast<float>(Hits) / static_cast<float>(Samples)) : 0.f;
	}

	float CrossfireAngleDeg(const FVector& A, const FVector& B, const FVector& KillZone)
	{
		const FVector2D ToA = FVector2D(A.X - KillZone.X, A.Y - KillZone.Y);
		const FVector2D ToB = FVector2D(B.X - KillZone.X, B.Y - KillZone.Y);
		if (ToA.SizeSquared() < KINDA_SMALL_NUMBER || ToB.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float Dot = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(ToA.GetSafeNormal(), ToB.GetSafeNormal())), -1.f, 1.f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}

	bool InLineOfFire(const FVector& Shooter, const FVector& Mate, const FVector& KillZone, float HalfWidthCm)
	{
		const FVector2D Line = FVector2D(KillZone.X - Shooter.X, KillZone.Y - Shooter.Y);
		const float Length = static_cast<float>(Line.Size());
		if (Length < KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVector2D Dir = Line / Length;
		const FVector2D ToMate = FVector2D(Mate.X - Shooter.X, Mate.Y - Shooter.Y);
		const float Along = static_cast<float>(FVector2D::DotProduct(ToMate, Dir));
		if (Along <= 0.f || Along >= Length)
		{
			return false;
		}
		const float Lateral = FMath::Abs(static_cast<float>(ToMate.X * Dir.Y - ToMate.Y * Dir.X));
		return Lateral < HalfWidthCm;
	}

	bool CrossfireOk(const FVector& H1, const FVector& H2, const FVector& KillZone, float MinDeg, float LineClearCm)
	{
		return CrossfireAngleDeg(H1, H2, KillZone) >= MinDeg
			&& !InLineOfFire(H1, H2, KillZone, LineClearCm)
			&& !InLineOfFire(H2, H1, KillZone, LineClearCm);
	}

	float SpacingOf(const FFiringPosition& Pos, TConstArrayView<FFiringPosition> Chosen, TConstArrayView<FString> ClaimedKeys,
		const FFiringPositionConfig& Config)
	{
		TArray<FVector, TInlineAllocator<8>> Points;
		TArray<FString, TInlineAllocator<8>> Keys;
		for (const FFiringPosition& Other : Chosen)
		{
			Points.Add(Other.StandPoint);
			Keys.Add(Other.CoverKey);
		}
		for (const FString& Key : ClaimedKeys)
		{
			// A claimed key has no point: it is far away for the distance test, but still the same object for the key test.
			Points.Add(FVector(1.0e12, 1.0e12, 0.0));
			Keys.Add(Key);
		}
		FPmcDefenceConfig SpacingConfig;
		SpacingConfig.MinSpacingCm = Config.MinSpacingCm;
		SpacingConfig.SpacingPenalty = Config.SpacingPenalty;
		return PmcDefenceRules::SpacingFactor(Pos.StandPoint, Pos.CoverKey, Points, Keys, SpacingConfig);
	}

	TArray<int32> PickN(TConstArrayView<FFiringCandidate> Candidates, int32 N, TConstArrayView<FString> ExcludeKeys, const FFiringPositionConfig& Config)
	{
		TArray<int32> Picked;
		TArray<FFiringPosition> Chosen;
		while (Picked.Num() < N)
		{
			int32 Best = INDEX_NONE;
			float BestScore = 0.f;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				const FFiringCandidate& Candidate = Candidates[Index];
				if (!Candidate.Position.bValid || Candidate.Score <= 0.f || Picked.Contains(Index))
				{
					continue;
				}
				const float Score = Candidate.Score * SpacingOf(Candidate.Position, Chosen, ExcludeKeys, Config);
				if (Score > BestScore)
				{
					Best = Index;
					BestScore = Score;
				}
			}
			if (Best == INDEX_NONE)
			{
				break;
			}
			Picked.Add(Best);
			Chosen.Add(Candidates[Best].Position);
		}
		return Picked;
	}

	bool PickCrossfirePair(TConstArrayView<FFiringCandidate> Candidates, const FVector& KillZone, TConstArrayView<FString> ExcludeKeys,
		const FFiringPositionConfig& Config, int32& OutFirst, int32& OutSecond)
	{
		// The first hammer: candidates by score, best first; the first one with a crossfire partner wins.
		TArray<int32> Order;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index].Position.bValid && Candidates[Index].Score > 0.f
				&& SpacingOf(Candidates[Index].Position, TConstArrayView<FFiringPosition>(), ExcludeKeys, Config) > 0.f)
			{
				Order.Add(Index);
			}
		}
		Order.StableSort([&Candidates](int32 A, int32 B) { return Candidates[A].Score > Candidates[B].Score; });
		for (const int32 First : Order)
		{
			const FFiringPosition& H1 = Candidates[First].Position;
			int32 Partner = INDEX_NONE;
			float BestScore = 0.f;
			for (const int32 Second : Order)
			{
				if (Second == First)
				{
					continue;
				}
				const FFiringPosition& H2 = Candidates[Second].Position;
				if (!CrossfireOk(H1.StandPoint, H2.StandPoint, KillZone, Config.CrossfireMinDeg, Config.LineClearCm))
				{
					continue;
				}
				const float Score = Candidates[Second].Score * SpacingOf(H2, MakeArrayView(&H1, 1), ExcludeKeys, Config);
				if (Score > BestScore)
				{
					Partner = Second;
					BestScore = Score;
				}
			}
			if (Partner != INDEX_NONE)
			{
				OutFirst = First;
				OutSecond = Partner;
				return true;
			}
		}
		return false;
	}

	TArray<FVector> CoverScanPoints(const FVector& Centre, float RingMaxCm, const FFiringPositionConfig& Config)
	{
		TArray<FVector> Points;
		const double Step = FMath::Max(Config.ScanStepCm, 100.f);
		const int32 Stride = FMath::Max(1, Config.ScanFarStride);
		const double Ring = FMath::Max(RingMaxCm, 0.f);
		const int32 Reach = FMath::CeilToInt(static_cast<float>(Ring / Step)) + 1;
		const int32 CentreX = FMath::RoundToInt(static_cast<float>(Centre.X / Step));
		const int32 CentreY = FMath::RoundToInt(static_cast<float>(Centre.Y / Step));
		for (int32 IY = CentreY - Reach; IY <= CentreY + Reach; ++IY)
		{
			for (int32 IX = CentreX - Reach; IX <= CentreX + Reach; ++IX)
			{
				const FVector Point(IX * Step, IY * Step, Centre.Z);
				const double Distance = FVector::Dist2D(Point, Centre);
				if (Distance > Ring)
				{
					continue;
				}
				if (Distance > Config.ScanDenseCm && ((IX % Stride) != 0 || (IY % Stride) != 0))
				{
					continue;
				}
				Points.Add(Point);
			}
		}
		Points.StableSort([&Centre](const FVector& A, const FVector& B)
		{
			const double DA = FVector::Dist2D(A, Centre);
			const double DB = FVector::Dist2D(B, Centre);
			if (DA != DB)
			{
				return DA < DB;
			}
			return A.X != B.X ? A.X < B.X : A.Y < B.Y;
		});
		if (Points.Num() > Config.MaxScanPoints)
		{
			Points.SetNum(Config.MaxScanPoints);
		}
		return Points;
	}

	int32 HitsInBand(TConstArrayView<float> AimDistancesCm, uint32 ClearMask, float BandMinCm, float BandMaxCm)
	{
		int32 Hits = 0;
		for (int32 Index = 0; Index < AimDistancesCm.Num() && Index < 32; ++Index)
		{
			if ((ClearMask & (1u << Index)) != 0 && SampleInBand(AimDistancesCm[Index], BandMinCm, BandMaxCm, 0.f))
			{
				++Hits;
			}
		}
		return Hits;
	}

	void WeaponBand(float ReachCm, int32 Tier, const FFiringPositionConfig& Config, float& OutMinCm, float& OutMaxCm)
	{
		const float Reach = ReachCm > 0.f ? ReachCm : Config.DefaultReachCm;
		OutMinCm = Config.BandMinFactor * Reach;
		OutMaxCm = (Tier <= 0 ? Config.BandMaxFactor : Config.BandMaxFactorTier2) * Reach;
	}

	bool IsForwardOfKillZone(const FVector& StandPoint, const FVector& KillZone, const FVector& ApproachPoint, float MarginCm)
	{
		const FVector2D Axis = FVector2D(KillZone.X - ApproachPoint.X, KillZone.Y - ApproachPoint.Y);
		if (Axis.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVector2D ToStand = FVector2D(StandPoint.X - KillZone.X, StandPoint.Y - KillZone.Y);
		// Along the axis from the attackers toward the zone: negative = nearer the attackers than the zone.
		const float Along = static_cast<float>(FVector2D::DotProduct(ToStand, Axis.GetSafeNormal()));
		return Along < -FMath::Max(MarginCm, 0.f);
	}

	float PolylineLength(TConstArrayView<FVector> Points)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector::Dist(Points[Index - 1], Points[Index]);
		}
		return static_cast<float>(Length);
	}

	bool PointAlongPathBefore(TConstArrayView<FVector> Points, float DistanceCm, FVector& OutPoint, float& OutUsedCm)
	{
		if (Points.Num() < 2)
		{
			return false;
		}
		const float Total = PolylineLength(Points);
		OutUsedCm = FMath::Clamp(DistanceCm, 0.f, Total);
		// Walk back from the end.
		float Remaining = OutUsedCm;
		for (int32 Index = Points.Num() - 1; Index >= 1; --Index)
		{
			const FVector& Tail = Points[Index];
			const FVector& Head = Points[Index - 1];
			const float Segment = static_cast<float>(FVector::Dist(Head, Tail));
			if (Remaining <= Segment)
			{
				OutPoint = Segment > KINDA_SMALL_NUMBER ? FMath::Lerp(Tail, Head, Remaining / Segment) : Tail;
				return true;
			}
			Remaining -= Segment;
		}
		OutPoint = Points[0];
		return true;
	}

	float PathLengthUntilWithin(TConstArrayView<FVector> Points, const FVector& Point, float ReachCm)
	{
		if (Points.Num() < 2)
		{
			return 0.f;
		}
		const double Reach = FMath::Max(ReachCm, 0.f);
		if (FVector::Dist(Points[0], Point) <= Reach)
		{
			return 0.f;
		}
		double Walked = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector Head = Points[Index - 1];
			const FVector Tail = Points[Index];
			const FVector Leg = Tail - Head;
			const double Length = Leg.Size();
			if (Length > KINDA_SMALL_NUMBER)
			{
				// |Head + t Leg - Point|^2 = Reach^2, the smaller root t in [0, 1] (the start of the leg is outside the circle).
				const FVector Offset = Head - Point;
				const double A = Leg.SizeSquared();
				const double B = 2.0 * FVector::DotProduct(Offset, Leg);
				const double C = Offset.SizeSquared() - Reach * Reach;
				const double Discriminant = B * B - 4.0 * A * C;
				if (Discriminant >= 0.0)
				{
					const double T = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
					if (T >= 0.0 && T <= 1.0)
					{
						return static_cast<float>(Walked + T * Length);
					}
				}
			}
			Walked += Length;
		}
		return static_cast<float>(Walked);
	}

	bool ReachableBeforeContact(float DefenderPathCm, float DefenderSpeedCm, float AttackerPathToContactCm, float AttackerSpeedCm, float MarginSeconds, float& OutSlackSeconds)
	{
		constexpr float Cap = 999.f;
		if (DefenderSpeedCm <= KINDA_SMALL_NUMBER)
		{
			OutSlackSeconds = -Cap;
			return false;
		}
		const float DefenderSeconds = FMath::Max(DefenderPathCm, 0.f) / DefenderSpeedCm;
		const float AttackerSeconds = AttackerSpeedCm > KINDA_SMALL_NUMBER ? FMath::Max(AttackerPathToContactCm, 0.f) / AttackerSpeedCm : Cap;
		OutSlackSeconds = FMath::Clamp(AttackerSeconds - DefenderSeconds - MarginSeconds, -Cap, Cap);
		return OutSlackSeconds >= 0.f;
	}

	float MedianOf(TArray<float> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return (Values.Num() % 2 == 1) ? Values[Mid] : 0.5f * (Values[Mid - 1] + Values[Mid]);
	}

	TArray<FVector> AreaSamplePoints(const FVector& Centre, float RadiusCm, int32 AreaSamples)
	{
		TArray<FVector> Points;
		if (AreaSamples < 9 || RadiusCm <= 0.f)
		{
			Points.Add(Centre);
			return Points;
		}
		const float Step = RadiusCm * 2.f / 3.f;
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				Points.Add(Centre + FVector(X * Step, Y * Step, 0.f));
			}
		}
		return Points;
	}

	bool SampleInBand(float DistanceCm, float BandMinCm, float BandMaxCm, float MaxRangeCm)
	{
		if (BandMinCm > 0.f && DistanceCm < BandMinCm)
		{
			return false;
		}
		if (BandMaxCm > 0.f && DistanceCm > BandMaxCm)
		{
			return false;
		}
		return MaxRangeCm <= 0.f || DistanceCm <= MaxRangeCm;
	}

	float RoofEscape(int32 AccessCount, bool bLadderOnly, const FFiringPositionConfig& Config)
	{
		if (bLadderOnly)
		{
			return Config.EscapeLadderOnly;
		}
		return AccessCount >= 2 ? Config.EscapeSecondWay : Config.EscapeOneWay;
	}

	float RoofProtectOf(int32 Levels, const FFiringPositionConfig& Config)
	{
		return FMath::Min(1.f, Config.RoofProtect + Config.HighGroundPerLevel * static_cast<float>(FMath::Max(Levels, 0)));
	}

	TArray<float> RimSteps(const FFiringPositionConfig& Config)
	{
		TArray<float> Steps;
		Steps.Add(0.f);
		const float Step = FMath::Max(Config.RimStepCm, 1.f);
		for (float Cm = Step; Cm <= Config.RimStepMaxCm + KINDA_SMALL_NUMBER; Cm += Step)
		{
			Steps.Add(Cm);
		}
		return Steps;
	}

	FString LogLine(const FFiringPosition& Pos)
	{
		return FString::Printf(TEXT("[FiringPos] %s %s stand %s %s yaw %.0f covered %.2f%s"), KindName(Pos.Kind), *Pos.SourceId, *Pos.StandPoint.ToCompactString(),
			StanceName(Pos.Stance), Pos.FacingYaw, Pos.CoveredShare, Pos.RimStepCm > 0.f ? *FString::Printf(TEXT(" rim step %.0f cm"), Pos.RimStepCm) : TEXT(""));
	}
}
