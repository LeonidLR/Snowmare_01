#include "Power/PowerRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PowerRulesInternal
{
	/** Metres in the file, cm in the code. */
	constexpr float CmPerMetre = 100.f;

	const FJsonObject* Child(const FJsonObject& Root, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return Root.TryGetObjectField(Key, Found) && Found && Found->IsValid() ? Found->Get() : nullptr;
	}

	void ReadTuningFloat(const FJsonObject* Object, const TCHAR* Key, float& Out, float Scale = 1.f, float Min = 0.f)
	{
		double Value = 0.0;
		if (Object && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value))
		{
			Out = FMath::Max(Min, static_cast<float>(Value) * Scale);
		}
	}

	void ReadTuningInt(const FJsonObject* Object, const TCHAR* Key, int32& Out, int32 Min = 0)
	{
		double Value = 0.0;
		if (Object && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value))
		{
			Out = FMath::Max(Min, FMath::RoundToInt(Value));
		}
	}

	void ReadTuningBool(const FJsonObject* Object, const TCHAR* Key, bool& Out)
	{
		bool Value = false;
		if (Object && Object->TryGetBoolField(Key, Value))
		{
			Out = Value;
		}
	}
}

FString PowerRules::GetTuningPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Balance/turret_power.json");
}

const FPowerTuning& PowerRules::GetTuning()
{
	static FPowerTuning Tuning;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetTuningPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Power] %s not found - the code defaults are used"), *GetTuningPath());
		}
		else if (!ParseTuning(Text, Tuning))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Power] %s is not a JSON object - the code defaults are used"), *GetTuningPath());
		}
	}
	return Tuning;
}

bool PowerRules::ParseTuning(const FString& Json, FPowerTuning& Out)
{
	using namespace PowerRulesInternal;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return false;
	}
	FPowerTuning T = Out;
	if (const FJsonObject* Time = Child(*Root, TEXT("time")))
	{
		ReadTuningFloat(Time, TEXT("nominal_hour_real_s"), T.NominalHourRealSeconds, 1.f, 0.01f);
		ReadTuningBool(Time, TEXT("burn_before_contact"), T.bBurnBeforeContact);
		ReadTuningFloat(Time, TEXT("contact_wake_m"), T.ContactWakeCm, CmPerMetre);
	}
	if (const FJsonObject* Generator = Child(*Root, TEXT("generator")))
	{
		ReadTuningFloat(Generator, TEXT("tank_l"), T.TankL, 1.f, 0.1f);
		ReadTuningFloat(Generator, TEXT("jerrycan_l"), T.JerrycanL);
		ReadTuningFloat(Generator, TEXT("burn_base_l_per_h"), T.BurnBaseLPerHour);
		ReadTuningFloat(Generator, TEXT("burn_per_turret_l_per_h"), T.BurnPerTurretLPerHour);
		ReadTuningFloat(Generator, TEXT("hp"), T.GeneratorHp, 1.f, 1.f);
		ReadTuningFloat(Generator, TEXT("fuel_lost_on_break_frac"), T.FuelLostOnBreakFrac);
		T.FuelLostOnBreakFrac = FMath::Clamp(T.FuelLostOnBreakFrac, 0.f, 1.f);
		ReadTuningFloat(Generator, TEXT("heat_radius_m"), T.HeatRadiusCm, CmPerMetre);
		const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
		if (Generator->TryGetArrayField(TEXT("low_fuel_warn_s"), Warnings) && Warnings)
		{
			T.LowFuelWarnSeconds.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Warnings)
			{
				double Seconds = 0.0;
				if (Value.IsValid() && Value->TryGetNumber(Seconds) && FMath::IsFinite(Seconds) && Seconds > 0.0)
				{
					T.LowFuelWarnSeconds.Add(static_cast<float>(Seconds));
				}
			}
			T.LowFuelWarnSeconds.Sort(TGreater<float>());
		}
		ReadTuningFloat(Generator, TEXT("refuel_s"), T.RefuelSeconds);
		ReadTuningFloat(Generator, TEXT("start_s"), T.StartSeconds);
		ReadTuningFloat(Generator, TEXT("stop_s"), T.StopSeconds);
		ReadTuningFloat(Generator, TEXT("seize_s"), T.SeizeSeconds);
		ReadTuningFloat(Generator, TEXT("seize_dark_after_s"), T.SeizeDarkAfterSeconds);
		ReadTuningFloat(Generator, TEXT("repair_s"), T.RepairSeconds);
		ReadTuningFloat(Generator, TEXT("engineer_time_mult"), T.EngineerTimeMult);
		ReadTuningFloat(Generator, TEXT("start_s_blizzard_mult"), T.StartBlizzardMult);
		ReadTuningFloat(Generator, TEXT("interrupt_reach_m"), T.InterruptReachCm, CmPerMetre);
		if (const FJsonObject* Noise = Child(*Generator, TEXT("noise")))
		{
			ReadTuningFloat(Noise, TEXT("start_m"), T.StartNoiseCm, CmPerMetre);
			ReadTuningFloat(Noise, TEXT("work_m"), T.WorkNoiseCm, CmPerMetre);
		}
	}
	if (const FJsonObject* Network = Child(*Root, TEXT("network")))
	{
		ReadTuningFloat(Network, TEXT("cable_min_m"), T.CableMinCm, CmPerMetre);
		ReadTuningFloat(Network, TEXT("cable_max_m"), T.CableMaxCm, CmPerMetre);
		ReadTuningInt(Network, TEXT("max_turrets"), T.MaxTurrets, 1);
		ReadTuningFloat(Network, TEXT("plug_range_m"), T.PlugRangeCm, CmPerMetre);
		ReadTuningFloat(Network, TEXT("spin_up_s"), T.SpinUpSeconds);
		ReadTuningFloat(Network, TEXT("wind_down_s"), T.WindDownSeconds);
	}
	if (const FJsonObject* Knowledge = Child(*Root, TEXT("knowledge")))
	{
		ReadTuningFloat(Knowledge, TEXT("observe_m"), T.ObserveCm, CmPerMetre);
		ReadTuningFloat(Knowledge, TEXT("observe_s"), T.ObserveSeconds);
	}
	if (const FJsonObject* Carry = Child(*Root, TEXT("carry")))
	{
		ReadTuningInt(Carry, TEXT("jerrycans_per_operative"), T.JerrycansPerOperative);
	}
	if (const FJsonObject* Teams = Child(*Root, TEXT("teams")))
	{
		ReadTuningInt(Teams, TEXT("hostile_owner_team"), T.HostileOwnerTeam, 1);
	}
	Out = T;
	return true;
}

EGeneratorState PowerRules::DeriveState(bool bBroken, float FuelL, bool bSwitchOn)
{
	if (bBroken)
	{
		return EGeneratorState::Broken;
	}
	if (FuelL <= 0.f)
	{
		return EGeneratorState::Dry;
	}
	return bSwitchOn ? EGeneratorState::Running : EGeneratorState::Fuelled;
}

float PowerRules::BurnLPerHour(const FPowerTuning& Tuning, int32 LiveTurrets)
{
	return Tuning.BurnBaseLPerHour + Tuning.BurnPerTurretLPerHour * FMath::Max(0, LiveTurrets);
}

float PowerRules::BurnLPerSecond(const FPowerTuning& Tuning, int32 LiveTurrets)
{
	return BurnLPerHour(Tuning, LiveTurrets) / FMath::Max(0.01f, Tuning.NominalHourRealSeconds);
}

float PowerRules::SecondsLeft(const FPowerTuning& Tuning, float FuelL, int32 LiveTurrets)
{
	const float Rate = BurnLPerSecond(Tuning, LiveTurrets);
	return FuelL > 0.f && Rate > 0.f ? FuelL / Rate : (FuelL > 0.f ? MAX_flt : 0.f);
}

bool PowerRules::BurnsNow(const FPowerTuning& Tuning, bool bRunning, bool bContact, bool bTurnBased)
{
	return bRunning && !bTurnBased && (bContact || Tuning.bBurnBeforeContact);
}

float PowerRules::FuelAfterBurn(const FPowerTuning& Tuning, float FuelL, int32 LiveTurrets, float DeltaSeconds)
{
	return FMath::Max(0.f, FuelL - BurnLPerSecond(Tuning, LiveTurrets) * FMath::Max(0.f, DeltaSeconds));
}

float PowerRules::FuelAfterRefuel(const FPowerTuning& Tuning, float FuelL)
{
	return FMath::Min(Tuning.TankL, FMath::Max(0.f, FuelL) + Tuning.JerrycanL);
}

float PowerRules::FuelAfterBreak(const FPowerTuning& Tuning, float FuelL)
{
	return FMath::Max(0.f, FuelL) * (1.f - Tuning.FuelLostOnBreakFrac);
}

int32 PowerRules::CrossedWarning(const FPowerTuning& Tuning, float SecondsBefore, float SecondsAfter)
{
	int32 Crossed = INDEX_NONE;
	for (int32 Index = 0; Index < Tuning.LowFuelWarnSeconds.Num(); ++Index)
	{
		const float Mark = Tuning.LowFuelWarnSeconds[Index];
		if (SecondsBefore > Mark && SecondsAfter <= Mark && SecondsAfter > 0.f)
		{
			if (Crossed == INDEX_NONE || Mark < Tuning.LowFuelWarnSeconds[Crossed])
			{
				Crossed = Index;
			}
		}
	}
	return Crossed;
}

EPowerAllegiance PowerRules::AllegianceOf(int32 OwnerTeam, int32 ViewerTeam)
{
	if (OwnerTeam == NoOwner)
	{
		return EPowerAllegiance::Neutral;
	}
	return OwnerTeam == ViewerTeam ? EPowerAllegiance::Friendly : EPowerAllegiance::Hostile;
}

bool PowerRules::MayTargetHuman(int32 OwnerTeam, int32 HumanTeam)
{
	return OwnerTeam != NoOwner && HumanTeam != OwnerTeam;
}

EGeneratorAction PowerRules::PrimaryAction(EGeneratorState State, int32 OwnerTeam, int32 ActorTeam)
{
	switch (State)
	{
	case EGeneratorState::Broken: return EGeneratorAction::Repair;
	case EGeneratorState::Running: return OwnerTeam == ActorTeam ? EGeneratorAction::Stop : EGeneratorAction::Seize;
	case EGeneratorState::Fuelled: return EGeneratorAction::Start;
	default: return EGeneratorAction::Refuel;
	}
}

EGeneratorRefusal PowerRules::CheckAction(const FPowerTuning& Tuning, EGeneratorAction Action, EGeneratorState State, float FuelL,
	int32 JerrycansCarried, int32 OwnerTeam, int32 ActorTeam, bool bTurnBased)
{
	if (Action == EGeneratorAction::None)
	{
		return EGeneratorRefusal::None;
	}
	if (bTurnBased)
	{
		return EGeneratorRefusal::TurnBased;
	}
	const bool bBroken = State == EGeneratorState::Broken;
	const bool bRunning = State == EGeneratorState::Running;
	switch (Action)
	{
	case EGeneratorAction::Repair:
		return bBroken ? EGeneratorRefusal::None : EGeneratorRefusal::NotBroken;
	case EGeneratorAction::Refuel:
		if (bBroken)
		{
			return EGeneratorRefusal::Broken;
		}
		if (JerrycansCarried <= 0)
		{
			return EGeneratorRefusal::NoJerrycan;
		}
		return FuelL >= Tuning.TankL - KINDA_SMALL_NUMBER ? EGeneratorRefusal::TankFull : EGeneratorRefusal::None;
	case EGeneratorAction::Start:
		if (bBroken)
		{
			return EGeneratorRefusal::Broken;
		}
		if (bRunning)
		{
			return EGeneratorRefusal::AlreadyRunning;
		}
		return FuelL > 0.f ? EGeneratorRefusal::None : EGeneratorRefusal::NoFuel;
	case EGeneratorAction::Stop:
		return bRunning ? EGeneratorRefusal::None : EGeneratorRefusal::NotRunning;
	case EGeneratorAction::Seize:
		if (!bRunning)
		{
			return EGeneratorRefusal::NotRunning;
		}
		return OwnerTeam == ActorTeam ? EGeneratorRefusal::AlreadyOurs : EGeneratorRefusal::None;
	default:
		return EGeneratorRefusal::None;
	}
}

float PowerRules::ActionSeconds(const FPowerTuning& Tuning, EGeneratorAction Action, bool bEngineer, bool bBlizzard)
{
	float Seconds = 0.f;
	switch (Action)
	{
	case EGeneratorAction::Refuel: Seconds = Tuning.RefuelSeconds; break;
	case EGeneratorAction::Start: Seconds = Tuning.StartSeconds; break;
	case EGeneratorAction::Stop: Seconds = Tuning.StopSeconds; break;
	case EGeneratorAction::Seize: Seconds = Tuning.SeizeSeconds; break;
	case EGeneratorAction::Repair: Seconds = Tuning.RepairSeconds; break;
	default: return 0.f;
	}
	if (bBlizzard && (Action == EGeneratorAction::Start || Action == EGeneratorAction::Seize))
	{
		Seconds *= Tuning.StartBlizzardMult;
	}
	return bEngineer ? Seconds * Tuning.EngineerTimeMult : Seconds;
}

int32 PowerRules::JerrycanRoom(const FPowerTuning& Tuning, int32 Carried)
{
	return FMath::Max(0, Tuning.JerrycansPerOperative - FMath::Max(0, Carried));
}

bool PowerRules::ParseStart(const FString& Text, EGeneratorStart& Out)
{
	const FString Key = Text.TrimStartAndEnd().ToLower();
	if (Key == TEXT("dry")) { Out = EGeneratorStart::Dry; return true; }
	if (Key == TEXT("fuelled_off")) { Out = EGeneratorStart::FuelledOff; return true; }
	if (Key == TEXT("running_hostile")) { Out = EGeneratorStart::RunningHostile; return true; }
	if (Key == TEXT("running_neutral")) { Out = EGeneratorStart::RunningNeutral; return true; }
	if (Key == TEXT("broken")) { Out = EGeneratorStart::Broken; return true; }
	return false;
}

FGeneratorSetup PowerRules::StartSetup(const FPowerTuning& Tuning, EGeneratorStart Start, TOptional<float> FuelL)
{
	FGeneratorSetup Setup;
	const auto Clamp = [&Tuning](float Litres) { return FMath::Clamp(Litres, 0.f, Tuning.TankL); };
	switch (Start)
	{
	case EGeneratorStart::RunningHostile:
	case EGeneratorStart::RunningNeutral:
		Setup.FuelL = Clamp(FuelL.Get(Tuning.TankL));
		Setup.bSwitchOn = Setup.FuelL > 0.f;
		Setup.OwnerTeam = Setup.bSwitchOn && Start == EGeneratorStart::RunningHostile ? Tuning.HostileOwnerTeam : NoOwner;
		Setup.bContact = Tuning.bBurnBeforeContact;
		break;
	case EGeneratorStart::FuelledOff:
		Setup.FuelL = Clamp(FuelL.Get(Tuning.JerrycanL));
		break;
	case EGeneratorStart::Broken:
		Setup.bBroken = true;
		Setup.FuelL = Clamp(FuelL.Get(0.f));
		break;
	default:
		break;
	}
	return Setup;
}

TArray<int32> PowerRules::PickTurretsInReach(const FPowerTuning& Tuning, const FVector& Generator, const TArray<FVector>& Turrets)
{
	TArray<TPair<double, int32>> InReach;
	for (int32 Index = 0; Index < Turrets.Num(); ++Index)
	{
		const double Distance = FVector::Dist(Generator, Turrets[Index]);
		if (Distance <= Tuning.PlugRangeCm)
		{
			InReach.Emplace(Distance, Index);
		}
	}
	InReach.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
	{
		return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value;
	});
	TArray<int32> Out;
	for (const TPair<double, int32>& Entry : InReach)
	{
		if (Out.Num() >= Tuning.MaxTurrets)
		{
			break;
		}
		Out.Add(Entry.Value);
	}
	return Out;
}

float PowerRules::ObserveStep(float Accumulated, float DeltaSeconds, bool bWatching)
{
	return bWatching ? FMath::Max(0.f, Accumulated) + FMath::Max(0.f, DeltaSeconds) : 0.f;
}

bool PowerRules::IsObserved(const FPowerTuning& Tuning, float Accumulated)
{
	return Accumulated >= Tuning.ObserveSeconds;
}

FString PowerRules::AllegianceName(EPowerAllegiance Allegiance)
{
	switch (Allegiance)
	{
	case EPowerAllegiance::Friendly: return TEXT("FRIENDLY");
	case EPowerAllegiance::Hostile: return TEXT("HOSTILE");
	default: return TEXT("NEUTRAL");
	}
}

FString PowerRules::StateName(EGeneratorState State)
{
	switch (State)
	{
	case EGeneratorState::Fuelled: return TEXT("FUELLED");
	case EGeneratorState::Running: return TEXT("RUNNING");
	case EGeneratorState::Broken: return TEXT("BROKEN");
	default: return TEXT("DRY");
	}
}

FString PowerRules::FormatMinutes(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::CeilToInt(FMath::Min(Seconds, 359999.f)));
	return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}
