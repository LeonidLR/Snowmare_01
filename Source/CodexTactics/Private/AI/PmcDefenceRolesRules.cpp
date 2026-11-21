// Defence roles (smart enemies T-08, first slice): pure rules, no world access. See PmcDefenceRolesRules.h.

#include "AI/PmcDefenceRolesRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcDefenceRolesLocal
{
	void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = static_cast<float>(Value);
		}
	}

	/** Metres in the file, centimetres in the struct. */
	void ReadMetres(const FJsonObject& Json, const TCHAR* Key, float& OutCm)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			OutCm = static_cast<float>(Value) * 100.f;
		}
	}

	void ReadBool(const FJsonObject& Json, const TCHAR* Key, bool& Out)
	{
		bool Value = Out;
		if (Json.TryGetBoolField(Key, Value))
		{
			Out = Value;
		}
	}

	/** The planar distance from Point to the segment A-B. */
	float DistToSegment2D(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector2D P(Point), S(A), E(B);
		const FVector2D Dir = E - S;
		const double LenSq = Dir.SizeSquared();
		if (LenSq <= UE_DOUBLE_SMALL_NUMBER)
		{
			return static_cast<float>(FVector2D::Distance(P, S));
		}
		const double T = FMath::Clamp(FVector2D::DotProduct(P - S, Dir) / LenSq, 0.0, 1.0);
		return static_cast<float>(FVector2D::Distance(P, S + Dir * T));
	}
}

namespace PmcDefenceRoles
{
	FPmcDefenceRolesConfig ConfigFromJson(const FJsonObject& Block, const FPmcDefenceRolesConfig& Base)
	{
		using namespace PmcDefenceRolesLocal;
		FPmcDefenceRolesConfig Out = Base;
		ReadBool(Block, TEXT("enabled"), Out.bEnabled);
		ReadBool(Block, TEXT("holder_cover"), Out.bHolderCover);
		ReadBool(Block, TEXT("fall_back"), Out.bFallBack);
		ReadBool(Block, TEXT("rule4_after_first_fire"), Out.bRule4AfterFirstFire);
		ReadMetres(Block, TEXT("zone_near_m"), Out.ZoneNearCm);
		Out.ZoneNearCm = FMath::Max(Out.ZoneNearCm, 0.f);
		ReadMetres(Block, TEXT("bypass_margin_m"), Out.BypassMarginCm);
		Out.BypassMarginCm = FMath::Max(Out.BypassMarginCm, 0.f);
		float Down = static_cast<float>(Out.FinishDownMin);
		ReadNumber(Block, TEXT("finish_down_min"), Down);
		Out.FinishDownMin = FMath::Clamp(FMath::RoundToInt(Down), 1, 10);
		ReadNumber(Block, TEXT("known_max_age_s"), Out.KnownMaxAgeS);
		Out.KnownMaxAgeS = FMath::Clamp(Out.KnownMaxAgeS, 0.f, 30.f);
		ReadNumber(Block, TEXT("not_in_time_margin_s"), Out.NotInTimeMarginS);
		Out.NotInTimeMarginS = FMath::Clamp(Out.NotInTimeMarginS, 0.f, 10.f);
		ReadNumber(Block, TEXT("bound_max_wait_s"), Out.BoundMaxWaitS);
		Out.BoundMaxWaitS = FMath::Clamp(Out.BoundMaxWaitS, 0.f, 60.f);
		ReadNumber(Block, TEXT("move_timeout_s"), Out.MoveTimeoutS);
		Out.MoveTimeoutS = FMath::Clamp(Out.MoveTimeoutS, 1.f, 120.f);
		ReadMetres(Block, TEXT("kill_zone_clear_m"), Out.KillZoneClearCm);
		Out.KillZoneClearCm = FMath::Max(Out.KillZoneClearCm, 0.f);
		ReadNumber(Block, TEXT("zone_min_quality"), Out.ZoneMinQuality);
		Out.ZoneMinQuality = FMath::Clamp(Out.ZoneMinQuality, 0.f, 1.f);
		ReadNumber(Block, TEXT("zone_no_line_factor"), Out.ZoneNoLineFactor);
		Out.ZoneNoLineFactor = FMath::Clamp(Out.ZoneNoLineFactor, 0.f, 1.f);
		ReadNumber(Block, TEXT("zone_dist_weight"), Out.ZoneDistWeight);
		Out.ZoneDistWeight = FMath::Clamp(Out.ZoneDistWeight, 0.f, 1.f);
		ReadMetres(Block, TEXT("zone_min_spacing_m"), Out.ZoneMinSpacingCm);
		Out.ZoneMinSpacingCm = FMath::Max(Out.ZoneMinSpacingCm, 0.f);
		ReadNumber(Block, TEXT("zone_ray_reach_cm"), Out.ZoneRayReachCm);
		Out.ZoneRayReachCm = FMath::Clamp(Out.ZoneRayReachCm, 100.f, 2000.f);
		float Posts = static_cast<float>(Out.MaxZonePosts);
		ReadNumber(Block, TEXT("max_zone_posts"), Posts);
		Out.MaxZonePosts = FMath::Clamp(FMath::RoundToInt(Posts), 1, 100);
		float Queries = static_cast<float>(Out.MaxPathQueries);
		ReadNumber(Block, TEXT("max_path_queries"), Queries);
		Out.MaxPathQueries = FMath::Clamp(FMath::RoundToInt(Queries), 1, 32);
		return Out;
	}

	FPmcDefenceRolesConfig LoadConfig()
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_commander.json");
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			return FPmcDefenceRolesConfig();
		}
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Json->TryGetObjectField(TEXT("defence_roles"), Block) || !Block || !Block->IsValid())
		{
			return FPmcDefenceRolesConfig();
		}
		return ConfigFromJson(**Block);
	}

	const TCHAR* RuleName(EPmcFallBackRule Rule)
	{
		switch (Rule)
		{
		case EPmcFallBackRule::ZoneEmptying: return TEXT("zone_emptying");
		case EPmcFallBackRule::Bypassed: return TEXT("bypassed");
		case EPmcFallBackRule::NotInTime: return TEXT("not_in_time");
		default: return TEXT("none");
		}
	}

	FString RuleList(uint8 Rules)
	{
		FString Out;
		const TPair<EPmcFallBackRule, const TCHAR*> Numbered[] = {
			{ EPmcFallBackRule::ZoneEmptying, TEXT("1") }, { EPmcFallBackRule::Bypassed, TEXT("2") }, { EPmcFallBackRule::NotInTime, TEXT("4") } };
		for (const TPair<EPmcFallBackRule, const TCHAR*>& Entry : Numbered)
		{
			if ((Rules & static_cast<uint8>(Entry.Key)) != 0)
			{
				Out += Out.IsEmpty() ? FString(Entry.Value) : FString(TEXT("+")) + Entry.Value;
			}
		}
		return Out.IsEmpty() ? FString(TEXT("-")) : Out;
	}

	FPmcFallBackDecision DecideFallBack(const FPmcFallBackSituation& Situation, const FPmcDefenceRolesConfig& Config)
	{
		FPmcFallBackDecision Out;
		const float Nearest = Situation.NearestAttackerFromObjectiveCm;
		if (Nearest >= 0.f)
		{
			const float EdgeCm = FMath::Max(0.f, Nearest - Situation.ObjectiveRadiusCm);
			// Rule 1: the zone is empty and an attacker is near its edge.
			if (!Situation.bZoneHeld && EdgeCm <= Config.ZoneNearCm)
			{
				Out.Rules |= static_cast<uint8>(EPmcFallBackRule::ZoneEmptying);
			}
			// Rule 2: an attacker nearer the objective than the kill zone - and the defence has fired (the ambush was not yet sprung before that).
			if (Situation.bDefenceHasFired && Situation.KillZoneFromObjectiveCm > 0.f && Nearest < Situation.KillZoneFromObjectiveCm - Config.BypassMarginCm)
			{
				Out.Rules |= static_cast<uint8>(EPmcFallBackRule::Bypassed);
			}
			// Rule 4: the attackers reach the zone edge before the hammer can be in it (+ margin). With rule4_after_first_fire (slice 2) only once the
			// defence has opened fire: before that the hammers are springing the ambush, not late for the capture.
			if (Situation.HammerPathCm >= 0.f && Situation.HammerSpeedCm > 0.f && Situation.AttackerSpeedCm > 0.f)
			{
				const float AttackerS = EdgeCm / Situation.AttackerSpeedCm;
				const float HammerS = Situation.HammerPathCm / Situation.HammerSpeedCm;
				Out.SlackS = FMath::Clamp(AttackerS - HammerS - Config.NotInTimeMarginS, -999.f, 999.f);
				if (Out.SlackS < 0.f && (!Config.bRule4AfterFirstFire || Situation.bDefenceHasFired))
				{
					Out.Rules |= static_cast<uint8>(EPmcFallBackRule::NotInTime);
				}
			}
		}
		if (Out.Rules != 0)
		{
			if (Situation.AttackersDown >= Config.FinishDownMin)
			{
				Out.bHeldByFinish = true;
			}
			else
			{
				Out.bFallBack = true;
			}
		}
		return Out;
	}

	int32 PickMover(TConstArrayView<FPmcMoverCandidate> Candidates, bool bAnyMoving, float SecondsSinceMoverStart, const FPmcDefenceRolesConfig& Config)
	{
		if (Candidates.IsEmpty() || (bAnyMoving && SecondsSinceMoverStart < Config.BoundMaxWaitS))
		{
			return INDEX_NONE;
		}
		int32 Best = INDEX_NONE;
		float BestTime = -1.f;
		for (const FPmcMoverCandidate& Candidate : Candidates)
		{
			if (Candidate.PathTimeS > BestTime || (Candidate.PathTimeS == BestTime && Candidate.Index < Best))
			{
				BestTime = Candidate.PathTimeS;
				Best = Candidate.Index;
			}
		}
		return Best;
	}

	float ZonePostScore(float Quality, bool bSeesApproach, float DistFromObjectiveCm, float ZoneRadiusCm, const FPmcDefenceRolesConfig& Config)
	{
		if (Quality <= 0.f)
		{
			return 0.f;
		}
		const float Edge = ZoneRadiusCm > 0.f ? FMath::Clamp(DistFromObjectiveCm / ZoneRadiusCm, 0.f, 1.f) : 0.f;
		return FMath::Min(Quality, 1.f) * (bSeesApproach ? 1.f : Config.ZoneNoLineFactor) * (1.f - Config.ZoneDistWeight * Edge);
	}

	int32 PickZonePost(TConstArrayView<FFiringCandidate> Posts, TConstArrayView<int32> Excluded, TConstArrayView<FVector> ClaimedStands,
		TConstArrayView<FString> ClaimedKeys, const FPmcDefenceRolesConfig& Config)
	{
		// Tier 0: a free cover object and the spacing; tier 1: a free cover object; tier 2: any stand point nobody stands on.
		for (int32 Tier = 0; Tier < 3; ++Tier)
		{
			for (int32 Index = 0; Index < Posts.Num(); ++Index)
			{
				const FFiringPosition& Pos = Posts[Index].Position;
				if (!Pos.bValid || Excluded.Contains(Index))
				{
					continue;
				}
				if (Tier < 2 && !Pos.CoverKey.IsEmpty() && ClaimedKeys.Contains(Pos.CoverKey))
				{
					continue;
				}
				bool bTooNear = false;
				for (const FVector& Stand : ClaimedStands)
				{
					const float Dist = static_cast<float>(FVector::Dist2D(Stand, Pos.StandPoint));
					if (Dist < (Tier == 0 ? Config.ZoneMinSpacingCm : 100.f))
					{
						bTooNear = true;
						break;
					}
				}
				if (!bTooNear)
				{
					return Index;
				}
			}
		}
		return INDEX_NONE;
	}

	bool PathCrossesKillZone(TConstArrayView<FVector> Path, const FVector& KillZone, float ClearCm)
	{
		if (Path.Num() < 2 || ClearCm <= 0.f)
		{
			return false;
		}
		const float StartCm = static_cast<float>(FVector::Dist2D(Path[0], KillZone));
		const float Limit = FMath::Min(ClearCm, 0.7f * StartCm);
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			if (PmcDefenceRolesLocal::DistToSegment2D(KillZone, Path[Index], Path[Index + 1]) < Limit)
			{
				return true;
			}
		}
		return false;
	}
}
