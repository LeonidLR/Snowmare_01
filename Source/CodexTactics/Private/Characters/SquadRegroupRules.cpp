#include "Characters/SquadRegroupRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SquadRegroupRules
{
	float FormationGapCm(float ToSlotCm, float ToLeaderCm)
	{
		return FMath::Max(0.f, FMath::Min(ToSlotCm, ToLeaderCm));
	}

	bool IsCriticallyWounded(float Health, float MaxHealth, const FSquadRegroupTuning& Tuning)
	{
		return MaxHealth > 0.f && Health / MaxHealth < Tuning.CriticalHealthFraction;
	}

	bool ShouldStartFastRegroup(float GapCm, bool bCriticallyWounded, const FSquadRegroupTuning& Tuning)
	{
		return !bCriticallyWounded && GapCm > Tuning.StartCm;
	}

	bool KeepsFastRegroup(float GapCm, bool bCriticallyWounded, const FSquadRegroupTuning& Tuning)
	{
		return !bCriticallyWounded && GapCm > Tuning.JoinCm;
	}

	ESquadRegroupPace ChoosePace(bool bCombat, bool bWounded, bool bCanSprint, const FSquadRegroupTuning& Tuning, bool bMayRun)
	{
		if (bWounded)
		{
			return bMayRun ? ESquadRegroupPace::WoundedRun : ESquadRegroupPace::WoundedWalk;
		}
		return !bCombat && bCanSprint && Tuning.bSprintOutOfCombat ? ESquadRegroupPace::Sprint : ESquadRegroupPace::Run;
	}

	float PaceSpeed(ESquadRegroupPace Pace, float SprintSpeed, float WoundedMultiplier, float ColdMultiplier, const FSquadRegroupTuning& Tuning)
	{
		const float Cold = FMath::Max(ColdMultiplier, 0.f);
		switch (Pace)
		{
		case ESquadRegroupPace::Sprint:
		case ESquadRegroupPace::WoundedWalk:
			return FMath::Max(SprintSpeed, 0.f);
		case ESquadRegroupPace::WoundedRun:
			return Tuning.RunSpeed * FMath::Max(WoundedMultiplier, 0.f) * Cold;
		default:
			return Tuning.RunSpeed * Cold;
		}
	}

	const TCHAR* PaceName(ESquadRegroupPace Pace)
	{
		switch (Pace)
		{
		case ESquadRegroupPace::Sprint:
			return TEXT("sprint");
		case ESquadRegroupPace::WoundedRun:
			return TEXT("wounded run");
		case ESquadRegroupPace::WoundedWalk:
			return TEXT("wounded walk (heavy wound: no run)");
		default:
			return TEXT("run");
		}
	}
}

namespace SquadRegroupTuningLocal
{
	FSquadRegroupTuning RegroupTuningCurrent;
	bool bRegroupTuningLoaded = false;

	void ReadRegroupNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max, float Scale = 1.f)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value) * Scale, Min, Max);
		}
	}

	void ReadRegroupFlag(const FJsonObject& Json, const TCHAR* Key, bool& Out)
	{
		double Value = 0.0;
		bool bValue = false;
		if (Json.TryGetBoolField(Key, bValue))
		{
			Out = bValue;
		}
		else if (Json.TryGetNumberField(Key, Value))
		{
			Out = Value != 0.0;
		}
	}
}

FString SquadRegroupTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_regroup.json");
}

FSquadRegroupTuning SquadRegroupTuning::FromJson(const FJsonObject& Json, const FSquadRegroupTuning& Base)
{
	using namespace SquadRegroupTuningLocal;
	FSquadRegroupTuning Tuning = Base;
	ReadRegroupNumber(Json, TEXT("start_m"), Tuning.StartCm, 100.f, 20000.f, 100.f);
	ReadRegroupNumber(Json, TEXT("join_m"), Tuning.JoinCm, 100.f, 20000.f, 100.f);
	ReadRegroupNumber(Json, TEXT("run_speed_cm_s"), Tuning.RunSpeed, 100.f, 1500.f);
	ReadRegroupNumber(Json, TEXT("critical_health_pct"), Tuning.CriticalHealthFraction, 0.f, 100.f, 0.01f);
	ReadRegroupNumber(Json, TEXT("repath_s"), Tuning.RepathSeconds, 0.1f, 5.f);
	ReadRegroupFlag(Json, TEXT("sprint_out_of_combat"), Tuning.bSprintOutOfCombat);
	ReadRegroupFlag(Json, TEXT("stand_up_to_run"), Tuning.bStandUpToRun);
	Tuning.JoinCm = FMath::Min(Tuning.JoinCm, Tuning.StartCm); // a join line beyond the start line would end every regroup at once
	return Tuning;
}

bool SquadRegroupTuning::Reload()
{
	using namespace SquadRegroupTuningLocal;
	bRegroupTuningLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	RegroupTuningCurrent = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad regroup tuning from %s: fast beyond %.0f cm, joins within %.0f cm, run %.0f cm/s, sprint out of combat %d"),
		*GetDefaultPath(), RegroupTuningCurrent.StartCm, RegroupTuningCurrent.JoinCm, RegroupTuningCurrent.RunSpeed,
		RegroupTuningCurrent.bSprintOutOfCombat ? 1 : 0);
	return true;
}

const FSquadRegroupTuning& SquadRegroupTuning::Get()
{
	using namespace SquadRegroupTuningLocal;
	if (!bRegroupTuningLoaded)
	{
		Reload();
	}
	return RegroupTuningCurrent;
}
