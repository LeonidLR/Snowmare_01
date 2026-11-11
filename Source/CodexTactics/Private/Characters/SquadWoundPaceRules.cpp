#include "Characters/SquadWoundPaceRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SquadWoundPaceLocal
{
	TAutoConsoleVariable<int32> CVarSquadWoundPace(TEXT("Codex.Squad.WoundPace"), -1,
		TEXT("The game designer's decision 2026-11-11: -1 = Content/Data/AI/squad_wound_pace.json \"enabled\" (default), 1 = the formation walks at its most ")
		TEXT("wounded member's pace (the wounded follower capped at his allowed walk, the leader's sprint ends), 0 = the old pacing (A/B)"));

	FSquadWoundPaceTuning WoundPaceTuningCurrent;
	bool bWoundPaceTuningLoaded = false;

	void ReadFlag(const FJsonObject& Json, const TCHAR* Key, bool& Out)
	{
		bool bValue = false;
		double Value = 0.0;
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

namespace SquadWoundPaceRules
{
	bool IsEnabled(const FSquadWoundPaceTuning& Tuning)
	{
		const int32 Console = SquadWoundPaceLocal::CVarSquadWoundPace.GetValueOnGameThread();
		return Console < 0 ? Tuning.bEnabled : Console != 0;
	}

	TOptional<FSquadWoundPace> SquadPace(TArrayView<const FSquadWoundPaceMember> Members, const FSquadWoundPaceTuning& Tuning, bool bEnabled)
	{
		if (!bEnabled)
		{
			return {};
		}
		FSquadWoundPace Pace;
		float Slowest = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FSquadWoundPaceMember& Member = Members[Index];
			// The critically wounded crawls: the squad does not crawl with him (he crawls after his slot at his own speed).
			if (!Member.bInFormation || !Member.bWounded || Member.bCritical || Member.AllowedSpeed <= 0.f)
			{
				continue;
			}
			if (Member.AllowedSpeed < Slowest)
			{
				Slowest = Member.AllowedSpeed;
				Pace.SlowestIndex = Index;
			}
		}
		if (Pace.SlowestIndex == INDEX_NONE)
		{
			return {};
		}
		Pace.CapSpeed = Slowest * FMath::Clamp(Tuning.PaceShare, 0.1f, 1.f);
		return Pace;
	}

	float LeaderPaceMultiplier(float BasePace, float LeaderWalkSpeed, const TOptional<FSquadWoundPace>& Pace)
	{
		const float Base = FMath::Clamp(BasePace, KINDA_SMALL_NUMBER, 1.f);
		if (!Pace.IsSet() || LeaderWalkSpeed <= KINDA_SMALL_NUMBER)
		{
			return Base;
		}
		return FMath::Clamp(Pace->CapSpeed / LeaderWalkSpeed, KINDA_SMALL_NUMBER, Base);
	}

	float FollowerTopSpeed(float MaxSpeed, bool bWounded, float CatchupMult, const FSquadWoundPaceTuning& Tuning, bool bEnabled)
	{
		const float Top = FMath::Max(MaxSpeed, 0.f);
		if (!bEnabled || !bWounded || !Tuning.bCapWoundedFollower || CatchupMult <= 1.f)
		{
			return Top;
		}
		return Top / CatchupMult;
	}

	float CapFixedSpeed(float Speed, float MaxSpeed, bool bWounded, const FSquadWoundPaceTuning& Tuning, bool bEnabled)
	{
		return bEnabled && bWounded && Tuning.bCapWoundedFollower ? FMath::Min(Speed, FMath::Max(MaxSpeed, 0.f)) : Speed;
	}

	bool EndsLeaderSprint(bool bLeaderSprinting, const TOptional<FSquadWoundPace>& Pace, const FSquadWoundPaceTuning& Tuning)
	{
		return bLeaderSprinting && Pace.IsSet() && Tuning.bLimitSprint;
	}
}

FString SquadWoundPaceTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_wound_pace.json");
}

FSquadWoundPaceTuning SquadWoundPaceTuning::FromJson(const FJsonObject& Json, const FSquadWoundPaceTuning& Base)
{
	using namespace SquadWoundPaceLocal;
	FSquadWoundPaceTuning Tuning = Base;
	ReadFlag(Json, TEXT("enabled"), Tuning.bEnabled);
	double Share = 0.0;
	if (Json.TryGetNumberField(TEXT("pace_share"), Share))
	{
		Tuning.PaceShare = FMath::Clamp(static_cast<float>(Share), 0.1f, 1.f);
	}
	ReadFlag(Json, TEXT("limit_sprint"), Tuning.bLimitSprint);
	ReadFlag(Json, TEXT("cap_wounded_follower"), Tuning.bCapWoundedFollower);
	return Tuning;
}

bool SquadWoundPaceTuning::Reload()
{
	using namespace SquadWoundPaceLocal;
	bWoundPaceTuningLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	WoundPaceTuningCurrent = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad wound pace tuning from %s: enabled %d, pace share %.2f, limit sprint %d, cap the wounded follower %d"), *GetDefaultPath(),
		WoundPaceTuningCurrent.bEnabled ? 1 : 0, WoundPaceTuningCurrent.PaceShare, WoundPaceTuningCurrent.bLimitSprint ? 1 : 0,
		WoundPaceTuningCurrent.bCapWoundedFollower ? 1 : 0);
	return true;
}

const FSquadWoundPaceTuning& SquadWoundPaceTuning::Get()
{
	using namespace SquadWoundPaceLocal;
	if (!bWoundPaceTuningLoaded)
	{
		Reload();
	}
	return WoundPaceTuningCurrent;
}
