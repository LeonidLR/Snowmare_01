#include "Data/SquadROE.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace SquadROE
{
	static FSquadROE Current;

	template <typename EnumType>
	struct FEnumName
	{
		EnumType Value;
		const TCHAR* Name;
	};

	static const FEnumName<ELeashStrictness> LeashNames[] = { { ELeashStrictness::Flexible, TEXT("Flexible") }, { ELeashStrictness::Strict, TEXT("Strict") } };
	static const FEnumName<EOpenGroundStance> OpenNames[] = {
		{ EOpenGroundStance::Crouch, TEXT("Crouch") }, { EOpenGroundStance::Prone, TEXT("Prone") }, { EOpenGroundStance::Standing, TEXT("Standing") } };
	static const FEnumName<ECoverStance> CoverNames[] = { { ECoverStance::Crouch, TEXT("Crouch") }, { ECoverStance::Standing, TEXT("Standing") } };
	static const FEnumName<ESniperReaction> SniperNames[] = {
		{ ESniperReaction::DiveToCover, TEXT("DiveToCover") }, { ESniperReaction::DropProne, TEXT("DropProne") } };
	static const FEnumName<ETargetPriorityPolicy> PolicyNames[] = { { ETargetPriorityPolicy::ThreatLevel, TEXT("ThreatLevel") },
		{ ETargetPriorityPolicy::ClosestFirst, TEXT("ClosestFirst") }, { ETargetPriorityPolicy::LowestHP, TEXT("LowestHP") },
		{ ETargetPriorityPolicy::AssistLeader, TEXT("AssistLeader") } };

	template <typename EnumType, int32 N>
	static void ReadEnum(const FJsonObject& Json, const TCHAR* Key, const FEnumName<EnumType> (&Names)[N], EnumType& Out)
	{
		FString Value;
		if (!Json.TryGetStringField(Key, Value))
		{
			return;
		}
		for (const FEnumName<EnumType>& Entry : Names)
		{
			if (Value.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				Out = Entry.Value;
				return;
			}
		}
		UE_LOG(LogCodexTactics, Warning, TEXT("Squad ROE: %s = \"%s\" unknown, kept"), Key, *Value);
	}

	template <typename EnumType, int32 N>
	static FString EnumToName(EnumType Value, const FEnumName<EnumType> (&Names)[N])
	{
		for (const FEnumName<EnumType>& Entry : Names)
		{
			if (Entry.Value == Value)
			{
				return Entry.Name;
			}
		}
		return Names[0].Name;
	}

	static void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value), Min, Max);
		}
	}

	static void ReadBool(const FJsonObject& Json, const TCHAR* Key, bool& Out)
	{
		bool Value = false;
		if (Json.TryGetBoolField(Key, Value))
		{
			Out = Value;
		}
	}
}

FString SquadROE::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_roe.json");
}

const FSquadROE& SquadROE::Get()
{
	return Current;
}

void SquadROE::Set(const FSquadROE& ROE)
{
	Current = ROE;
}

FSquadROE SquadROE::FromJson(const FJsonObject& Json, const FSquadROE& Base)
{
	FSquadROE ROE = Base;
	ReadNumber(Json, TEXT("anchor_radius_meters"), ROE.AnchorRadiusMeters, 1.f, 30.f);
	ReadEnum(Json, TEXT("leash_strictness"), LeashNames, ROE.LeashStrictness);
	ReadBool(Json, TEXT("prefer_high_ground"), ROE.bPreferHighGround);
	ReadEnum(Json, TEXT("open_ground_stance"), OpenNames, ROE.OpenGroundStance);
	ReadEnum(Json, TEXT("cover_stance"), CoverNames, ROE.CoverStance);
	ReadEnum(Json, TEXT("sniper_reaction"), SniperNames, ROE.SniperReaction);
	ReadEnum(Json, TEXT("target_priority_policy"), PolicyNames, ROE.TargetPriorityPolicy);
	ReadNumber(Json, TEXT("flank_defense_angle_deg"), ROE.FlankDefenseAngleDeg, 0.f, 180.f);
	ReadNumber(Json, TEXT("aid_health_threshold_pct"), ROE.AidHealthThresholdPct, 0.f, 100.f);
	ReadBool(Json, TEXT("require_safe_route_for_aid"), ROE.bRequireSafeRouteForAid);
	ReadBool(Json, TEXT("reserve_personal_medkit"), ROE.bReservePersonalMedkit);
	ReadNumber(Json, TEXT("auto_reload_threshold_pct"), ROE.AutoReloadThresholdPct, 0.f, 100.f);
	ReadNumber(Json, TEXT("emergency_sidearm_dist_m"), ROE.EmergencySidearmDistMeters, 0.f, 15.f);
	ReadNumber(Json, TEXT("defense_intercept_radius_m"), ROE.DefenseInterceptRadiusMeters, 2.f, 40.f);
	ReadEnum(Json, TEXT("defense_leash_strictness"), LeashNames, ROE.DefenseLeashStrictness);
	ReadBool(Json, TEXT("defense_body_block_priority"), ROE.bDefenseBodyBlockPriority);
	ReadBool(Json, TEXT("defense_ignore_distant_aid"), ROE.bDefenseIgnoreDistantAid);
	return ROE;
}

TSharedRef<FJsonObject> SquadROE::ToJson(const FSquadROE& ROE)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("anchor_radius_meters"), ROE.AnchorRadiusMeters);
	Json->SetStringField(TEXT("leash_strictness"), EnumToName(ROE.LeashStrictness, LeashNames));
	Json->SetBoolField(TEXT("prefer_high_ground"), ROE.bPreferHighGround);
	Json->SetStringField(TEXT("open_ground_stance"), EnumToName(ROE.OpenGroundStance, OpenNames));
	Json->SetStringField(TEXT("cover_stance"), EnumToName(ROE.CoverStance, CoverNames));
	Json->SetStringField(TEXT("sniper_reaction"), EnumToName(ROE.SniperReaction, SniperNames));
	Json->SetStringField(TEXT("target_priority_policy"), EnumToName(ROE.TargetPriorityPolicy, PolicyNames));
	Json->SetNumberField(TEXT("flank_defense_angle_deg"), ROE.FlankDefenseAngleDeg);
	Json->SetNumberField(TEXT("aid_health_threshold_pct"), ROE.AidHealthThresholdPct);
	Json->SetBoolField(TEXT("require_safe_route_for_aid"), ROE.bRequireSafeRouteForAid);
	Json->SetBoolField(TEXT("reserve_personal_medkit"), ROE.bReservePersonalMedkit);
	Json->SetNumberField(TEXT("auto_reload_threshold_pct"), ROE.AutoReloadThresholdPct);
	Json->SetNumberField(TEXT("emergency_sidearm_dist_m"), ROE.EmergencySidearmDistMeters);
	Json->SetNumberField(TEXT("defense_intercept_radius_m"), ROE.DefenseInterceptRadiusMeters);
	Json->SetStringField(TEXT("defense_leash_strictness"), EnumToName(ROE.DefenseLeashStrictness, LeashNames));
	Json->SetBoolField(TEXT("defense_body_block_priority"), ROE.bDefenseBodyBlockPriority);
	Json->SetBoolField(TEXT("defense_ignore_distant_aid"), ROE.bDefenseIgnoreDistantAid);
	return Json;
}

bool SquadROE::ApplyFile(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	Current = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad ROE from %s: anchor %.1f m, policy %s"), *Path, Current.AnchorRadiusMeters,
		*SquadAutonomyRules::PolicyName(Current.TargetPriorityPolicy));
	return true;
}

namespace SquadROE
{
	static void DumpCommand(const TArray<FString>& Args)
	{
		FString Out;
		FJsonSerializer::Serialize(ToJson(Current), TJsonWriterFactory<>::Create(&Out));
		UE_LOG(LogCodexTactics, Display, TEXT("Squad ROE: %s"), *Out);
	}

	static FAutoConsoleCommand DumpROE(TEXT("CodexTactics.DumpSquadROE"), TEXT("Logs the Commander Mode ROE in force (squad_roe.json)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
}
