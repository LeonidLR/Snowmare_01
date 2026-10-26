// Dev-only console commands for headless checks of the CLIMATE PROFILES (environment section 17, user decision 2026-10-26; UE-only,
// docs/systems/environment-weather.md section 17). Work under -nullrhi (the presenter applies, the MPC instance holds the values):
//   Scripts/smoke.ps1 -Command CodexTactics.ClimateSmoke        -Map /Game/Environment/Weather/Test/L_WeatherRainTest
//   Scripts/smoke.ps1 -Command CodexTactics.ClimateArcticSmoke  -Map /Game/Maps/L_MovementTest
// ClimateSmoke (the temperate test map; the dev run starts neutral = SunnyDay 15:00):
//   1. the level is Temperate, the weather is named SunnyDay, no snow (cover 0, object pockets 0), the presenter uses the temperate presets,
//      MPC_Environment has the 6 rain scalars (distinct ids) and holds ClimateTemperate 1, RainIntensity 0, IsSnowing 0; the cold uses the
//      temperate table (SunnyDay x cold_mult_sunny); the roster is off on this level;
//   2. Rain (forced) at ClockScale x20 for 30 design s: the wetness rises by the rain rate, MPC Wetness / RainIntensity 0.6 / IsRaining 1 /
//      ScreenDrops 1 match, the snow cover and the object snow stay 0;
//   3. SunnyDay 12:00 at x20 for 30 design s: the ground dries by the sunny rate (sun high), MPC Wetness follows, RainIntensity / ScreenDrops 0,
//      the puddles drain slower than the ground;
//   4. Codex.Env.Climate arctic (debug): the profile is Arctic, the hour continues, the snow cover = snow_cover_start, the wetness 0 and every
//      MPC rain scalar back to 0; Codex.Env.Climate temperate + Codex.Env.SetWeather Storm: temperate again, Storm = the Blizzard rung,
//      RainIntensity 1, no snow.
// ClimateArcticSmoke (an arctic map, L_MovementTest): the profile is Arctic, every MPC rain scalar 0; a forced Snowfall at x20 grows the snow
//   cover as before and leaves the rain scalars / wetness at 0; Codex.Env.SetWeather Rain is refused on an arctic level.
// Both print "[ClimateSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"

namespace ClimateSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float ApplyTimeoutSeconds = 12.f;
	constexpr float ClockScale = 20.f;
	/** Game seconds of each running stage (x20: 30 design seconds). */
	constexpr float RunSeconds = 1.5f;
	const TCHAR* const RainParams[] = { TEXT("RainIntensity"), TEXT("Wetness"), TEXT("PuddleAmount"), TEXT("IsRaining"), TEXT("ClimateTemperate"), TEXT("ScreenDrops") };

	enum class EStage : uint8
	{
		WaitApplying,
		Rain,
		Sunny,
		ArcticSnow,
	};

	struct FState
	{
		EStage Stage = EStage::WaitApplying;
		bool bArcticRun = false;
		float RunStart = -1.f;
		float StageStart = 0.f;
		float StageTime = 0.f;
		float TotalTime = 0.f;
		int32 Failures = 0;
		float AdvancedStart = 0.f;
		float WetnessStart = 0.f;
		float PuddlesStart = 0.f;
		float SnowStart = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[ClimateSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State, const TCHAR* Name)
	{
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[ClimateSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, Name);
	}

	void UpdateTime(FState& State, const UWorld& World)
	{
		const float Now = World.GetTimeSeconds();
		if (State.RunStart < 0.f)
		{
			State.RunStart = Now;
			State.StageStart = Now;
		}
		State.StageTime = Now - State.StageStart;
		State.TotalTime = Now - State.RunStart;
	}

	void NextStage(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageStart += State.StageTime;
		State.StageTime = 0.f;
	}

	/** The MPC_Environment scalar after a fresh application (-1 when unreadable). */
	float Mpc(AWeatherPresenterActor& Presenter, const TCHAR* Name)
	{
		Presenter.ApplyNow();
		float Value = -1.f;
		return Presenter.GetEnvironmentScalar(FName(Name), Value) ? Value : -1.f;
	}

	void CheckMpc(FState& State, AWeatherPresenterActor& Presenter, const TCHAR* Name, float Expected, const FString& Label, float Tolerance = 1.e-3f)
	{
		const float Value = Mpc(Presenter, Name);
		Check(State, FMath::IsNearlyEqual(Value, Expected, Tolerance), FString::Printf(TEXT("%s MPC_Environment.%s %.4f = %.4f"), *Label, Name, Value, Expected));
	}

	void CheckRainParamsZero(FState& State, AWeatherPresenterActor& Presenter, const FString& Label)
	{
		for (const TCHAR* Name : RainParams)
		{
			CheckMpc(State, Presenter, Name, 0.f, Label, 0.f);
		}
	}

	void Exec(UWorld& World, const TCHAR* Command)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[ClimateSmoke] > %s"), Command);
		GEngine->Exec(&World, Command);
	}

	/** The MPC carries the 6 section-17 scalars with valid, distinct ids (create_environment_mpc.py appends them; ids are not readable from Python). */
	void CheckMpcLayout(FState& State, const AWeatherPresenterActor& Presenter)
	{
		const UMaterialParameterCollection* Collection = Presenter.GetEnvironmentCollection();
		Check(State, Collection != nullptr, TEXT("1 MPC_Environment loaded"));
		if (!Collection)
		{
			return;
		}
		TSet<FGuid> Ids;
		int32 Found = 0;
		for (const TCHAR* Name : RainParams)
		{
			if (const FCollectionScalarParameter* Param = Collection->GetScalarParameterByName(FName(Name)))
			{
				++Found;
				Ids.Add(Param->Id);
				Check(State, Param->DefaultValue == 0.f, FString::Printf(TEXT("1 MPC %s default 0 (%.2f)"), Name, Param->DefaultValue));
			}
		}
		Check(State, Found == 6 && Ids.Num() == 6 && !Ids.Contains(FGuid()), FString::Printf(TEXT("1 MPC_Environment: the 6 rain scalars, distinct valid ids (%d found, %d ids)"),
			Found, Ids.Num()));
		// The older parameters keep their ids (a re-run of the script never re-creates them): they are present and distinct from the new ones.
		const FCollectionScalarParameter* Snow = Collection->GetScalarParameterByName(FName(TEXT("SnowAmount")));
		Check(State, Snow && !Ids.Contains(Snow->Id), TEXT("1 MPC SnowAmount still present with its own id"));
	}

	/** Stage 1 (temperate): the start, the presets, the MPC, the cold table; then Rain at x20. */
	void StartTemperate(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const FEnvironmentConfig& Config = Env.GetConfig();
		UE_LOG(LogCodexTactics, Display, TEXT("[ClimateSmoke] start: %s; %s"), *Env.Describe(), *Presenter.DescribeAdopted());
		Check(State, Env.GetClimate() == EClimateProfile::Temperate, TEXT("1 the level JSON climate is temperate"));
		Check(State, Env.GetWeather() == EWeatherState::Clear && Env.GetWeatherDisplayName() == TEXT("SunnyDay"),
			FString::Printf(TEXT("1 the dev run starts neutral: %s (SunnyDay)"), *Env.GetWeatherDisplayName()));
		Check(State, Env.GetSnowAmount() == 0.f && Env.GetObjectSnow().IsZero(), FString::Printf(TEXT("1 no snow on a temperate level (cover %.3f)"), Env.GetSnowAmount()));
		Check(State, Env.GetWetness() == 0.f && Env.GetRainIntensity() == 0.f, TEXT("1 SunnyDay start: dry, no rain"));
		const FWeatherVisualPreset& Sunny = Presenter.GetPreset(EWeatherState::Clear);
		Check(State, Presenter.GetActiveClimate() == EClimateProfile::Temperate && Sunny.SnowIntensity == 0.f && Sunny.FrostAmount == 0.f
			&& Sunny.SunIntensityScale > Presenter.GetPreset(EClimateProfile::Arctic, EWeatherState::Clear).SunIntensityScale,
			FString::Printf(TEXT("1 the presenter uses the temperate presets (SunnyDay sun x%.2f, %s)"), Sunny.SunIntensityScale,
				Presenter.IsPresetFromAsset(EWeatherState::Clear) ? TEXT("DA_Weather_Temperate_Sunny") : TEXT("C++ default")));
		Check(State, FMath::IsNearlyEqual(Presenter.GetLastFrame().Preset.FogDensity, Sunny.FogDensity, 1.e-4f),
			FString::Printf(TEXT("1 the applied fog %.4f = the SunnyDay preset"), Presenter.GetLastFrame().Preset.FogDensity));
		CheckMpcLayout(State, Presenter);
		CheckMpc(State, Presenter, TEXT("ClimateTemperate"), 1.f, TEXT("1 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("RainIntensity"), 0.f, TEXT("1 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("IsSnowing"), 0.f, TEXT("1 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("SnowAmount"), 0.f, TEXT("1 SunnyDay:"));
		Check(State, FMath::IsNearlyEqual(EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, Env.GetStateRef()), Config.Temperate.ColdMultSunny),
			FString::Printf(TEXT("1 the cold uses the temperate table (x%.2f = cold_mult_sunny)"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, Env.GetStateRef())));
		Check(State, !EnvironmentRules::IsRosterActive(Config, Env.GetClimate()), TEXT("1 the roster is off on a temperate level (waves as written)"));

		Exec(*Env.GetWorld(), TEXT("Codex.Env.SetWeather Rain instant"));
		Check(State, Env.GetWeather() == EWeatherState::Snowfall && Env.GetWeatherDisplayName() == TEXT("Rain"), TEXT("2 Codex.Env.SetWeather Rain -> the Snowfall rung, named Rain"));
		State.WetnessStart = Env.GetWetness();
		State.AdvancedStart = Env.GetAdvancedSeconds();
		Env.SetClockScale(ClockScale);
	}

	void CheckRain(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const FEnvironmentConfig& Config = Env.GetConfig();
		const float Ran = Env.GetAdvancedSeconds() - State.AdvancedStart;
		const float Expected = FMath::Min(1.f, State.WetnessStart + Ran * Config.Temperate.WetnessRateRain);
		Check(State, Env.GetWeather() == EWeatherState::Snowfall && Ran > 20.f, FString::Printf(TEXT("2 Rain ran %.1f design s at x%.0f"), Ran, ClockScale));
		Check(State, Env.GetWetness() > State.WetnessStart + 0.1f && FMath::IsNearlyEqual(Env.GetWetness(), Expected, 0.01f),
			FString::Printf(TEXT("2 Rain: the wetness rose %.3f -> %.3f (expected %.3f)"), State.WetnessStart, Env.GetWetness(), Expected));
		Check(State, FMath::IsNearlyEqual(Env.GetRainIntensity(), Config.Temperate.RainIntensityRain, 1.e-4f),
			FString::Printf(TEXT("2 Rain: rain intensity %.2f"), Env.GetRainIntensity()));
		CheckMpc(State, Presenter, TEXT("Wetness"), Env.GetWetness(), TEXT("2 Rain:"));
		CheckMpc(State, Presenter, TEXT("PuddleAmount"), Env.GetPuddleAmount(), TEXT("2 Rain:"));
		CheckMpc(State, Presenter, TEXT("RainIntensity"), Config.Temperate.RainIntensityRain, TEXT("2 Rain:"));
		CheckMpc(State, Presenter, TEXT("IsRaining"), 1.f, TEXT("2 Rain:"));
		CheckMpc(State, Presenter, TEXT("ScreenDrops"), 1.f, TEXT("2 Rain (screen drops ON while raining):"));
		CheckMpc(State, Presenter, TEXT("IsSnowing"), 0.f, TEXT("2 Rain:"));
		Check(State, Env.GetSnowAmount() == 0.f && Env.GetObjectSnow().IsZero(),
			FString::Printf(TEXT("2 Rain: no snow grows (cover %.3f, object top %.3f)"), Env.GetSnowAmount(), Env.GetObjectSnow().Top));
		CheckMpc(State, Presenter, TEXT("SnowAmount"), 0.f, TEXT("2 Rain:"));

		// Soak the ground (puddles to drain later), then SunnyDay at noon.
		FEnvironmentState Wet = Env.GetState();
		Wet.Wetness = 1.f;
		Wet.PuddleAmount = 0.8f;
		Wet.Weather = EWeatherState::Clear;
		Wet.NextWeather = EWeatherState::Overcast;
		Wet.WeatherElapsedSeconds = 0.f;
		Wet.Hour = 12.f;
		Env.SetState(Wet);
		Check(State, Env.GetWeatherDisplayName() == TEXT("SunnyDay"), TEXT("3 SunnyDay 12:00 set (soaked ground, puddles 0.8)"));
		State.WetnessStart = Env.GetWetness();
		State.PuddlesStart = Env.GetPuddleAmount();
		State.AdvancedStart = Env.GetAdvancedSeconds();
	}

	void CheckSunny(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const FEnvironmentConfig& Config = Env.GetConfig();
		const float Ran = Env.GetAdvancedSeconds() - State.AdvancedStart;
		const float Expected = FMath::Max(0.f, State.WetnessStart - Ran * Config.Temperate.DryingRateSunny);
		Check(State, Env.GetSunElevation() >= Config.Temperate.DryingFullSunElevation,
			FString::Printf(TEXT("3 the sun is high (%.1f deg)"), Env.GetSunElevation()));
		Check(State, Env.GetWetness() < State.WetnessStart - 0.05f && FMath::IsNearlyEqual(Env.GetWetness(), Expected, 0.01f),
			FString::Printf(TEXT("3 SunnyDay: the ground dried %.3f -> %.3f over %.1f design s (expected %.3f)"), State.WetnessStart, Env.GetWetness(), Ran, Expected));
		const float PuddleLoss = State.PuddlesStart - Env.GetPuddleAmount();
		Check(State, PuddleLoss > 0.f && PuddleLoss < State.WetnessStart - Env.GetWetness(),
			FString::Printf(TEXT("3 SunnyDay: the puddles drain slower (%.3f) than the ground dries (%.3f)"), PuddleLoss, State.WetnessStart - Env.GetWetness()));
		CheckMpc(State, Presenter, TEXT("Wetness"), Env.GetWetness(), TEXT("3 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("PuddleAmount"), Env.GetPuddleAmount(), TEXT("3 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("RainIntensity"), 0.f, TEXT("3 SunnyDay:"));
		CheckMpc(State, Presenter, TEXT("ScreenDrops"), 0.f, TEXT("3 SunnyDay:"));
		Check(State, Env.GetSnowAmount() == 0.f, TEXT("3 SunnyDay: the snow cover stays 0"));
		Env.SetClockScale(0.f);

		// 4: the debug climate switch.
		UWorld& World = *Env.GetWorld();
		const float Hour = Env.GetHour();
		Exec(World, TEXT("Codex.Env.Climate arctic"));
		Check(State, Env.GetClimate() == EClimateProfile::Arctic && FMath::IsNearlyEqual(Env.GetHour(), Hour, 1.e-3f),
			FString::Printf(TEXT("4 Codex.Env.Climate arctic: arctic, the hour continues (%.2f)"), Env.GetHour()));
		Check(State, FMath::IsNearlyEqual(Env.GetSnowAmount(), Config.SnowCoverStart) && Env.GetWetness() == 0.f && Env.GetPuddleAmount() == 0.f,
			FString::Printf(TEXT("4 arctic: the winter starts over (cover %.2f), dry ground"), Env.GetSnowAmount()));
		Check(State, Presenter.GetActiveClimate() == EClimateProfile::Arctic && FMath::IsNearlyEqual(Presenter.GetLastFrame().Preset.FogDensity,
			Presenter.GetPreset(EClimateProfile::Arctic, Env.GetWeather()).FogDensity, 1.e-4f), TEXT("4 arctic: the presenter applies the arctic presets"));
		CheckRainParamsZero(State, Presenter, TEXT("4 arctic:"));
		CheckMpc(State, Presenter, TEXT("SnowAmount"), Env.GetSnowAmount(), TEXT("4 arctic:"));

		Exec(World, TEXT("Codex.Env.Climate temperate"));
		Exec(World, TEXT("Codex.Env.SetWeather Storm instant"));
		Check(State, Env.GetClimate() == EClimateProfile::Temperate && Env.GetWeather() == EWeatherState::Blizzard && Env.GetWeatherDisplayName() == TEXT("Storm"),
			FString::Printf(TEXT("4 temperate again, Storm = the Blizzard rung (%s)"), *Env.Describe()));
		Check(State, Env.GetSnowAmount() == 0.f && Env.GetObjectSnow().IsZero(), TEXT("4 temperate again: no snow"));
		CheckMpc(State, Presenter, TEXT("RainIntensity"), Config.Temperate.RainIntensityStorm, TEXT("4 Storm:"));
		CheckMpc(State, Presenter, TEXT("ClimateTemperate"), 1.f, TEXT("4 Storm:"));
		Check(State, Env.GetWindStrength() > 0.5f, FString::Printf(TEXT("4 Storm: strong wind (%.2f)"), Env.GetWindStrength()));
	}

	/** ClimateArcticSmoke: an arctic map keeps every rain value at 0 and its snow rules. */
	void StartArctic(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[ClimateSmoke] arctic start: %s"), *Env.Describe());
		Check(State, Env.GetClimate() == EClimateProfile::Arctic, TEXT("A the level is arctic (no climate key)"));
		Check(State, !Env.Describe().Contains(TEXT("climate")), TEXT("A the arctic Describe line is unchanged (no climate part)"));
		CheckRainParamsZero(State, Presenter, TEXT("A start:"));
		// Section 18: an arctic level never creates the rain component (zero cost), and its overlay stays in snow mode.
		Check(State, Presenter.GetRainFx() == nullptr, TEXT("A no rain FX component on an arctic level"));
		Exec(*Env.GetWorld(), TEXT("Codex.Env.SetWeather Rain instant"));
		Check(State, Env.GetWeather() == EWeatherState::Clear, TEXT("A Codex.Env.SetWeather Rain is refused on an arctic level (still Clear)"));
		Env.ForceWeather(EWeatherState::Snowfall, true);
		Check(State, Env.GetWeatherDisplayName() == TEXT("Snowfall"), TEXT("A the Snowfall rung is named Snowfall"));
		if (Env.GetSnowAmount() > 0.8f)
		{
			Env.SetSnowAmount(0.3f);
		}
		State.SnowStart = Env.GetSnowAmount();
		State.AdvancedStart = Env.GetAdvancedSeconds();
		Env.SetClockScale(ClockScale);
	}

	void CheckArcticSnow(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const float Ran = Env.GetAdvancedSeconds() - State.AdvancedStart;
		const float Expected = FMath::Min(1.f, State.SnowStart + Ran * EnvironmentRules::GetSnowCoverRate(Env.GetConfig(), EWeatherState::Snowfall));
		Check(State, FMath::IsNearlyEqual(Env.GetSnowAmount(), Expected, 0.01f) && Env.GetSnowAmount() > State.SnowStart,
			FString::Printf(TEXT("A Snowfall: the cover grows as before %.3f -> %.3f (expected %.3f)"), State.SnowStart, Env.GetSnowAmount(), Expected));
		Check(State, Env.GetWetness() == 0.f && Env.GetPuddleAmount() == 0.f && Env.GetRainIntensity() == 0.f, TEXT("A Snowfall: no rain, dry ground"));
		CheckRainParamsZero(State, Presenter, TEXT("A Snowfall:"));
		CheckMpc(State, Presenter, TEXT("IsSnowing"), 1.f, TEXT("A Snowfall:"));
		Env.SetClockScale(1.f);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, const TCHAR* Name)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		if (!Env)
		{
			Check(State, false, TEXT("environment subsystem present"));
			Finish(State, Name);
			return false;
		}
		switch (State.Stage)
		{
		case EStage::WaitApplying:
			if (!Presenter || !Presenter->IsApplying() || Presenter->GetApplyCount() == 0)
			{
				if (State.TotalTime > ApplyTimeoutSeconds)
				{
					Check(State, false, FString::Printf(TEXT("presenter applying within %.0f game s"), ApplyTimeoutSeconds));
					Finish(State, Name);
					return false;
				}
				return true;
			}
			if (State.bArcticRun)
			{
				StartArctic(State, *Env, *Presenter);
				NextStage(State, EStage::ArcticSnow);
			}
			else
			{
				StartTemperate(State, *Env, *Presenter);
				NextStage(State, EStage::Rain);
			}
			return true;
		case EStage::Rain:
			if (State.StageTime >= RunSeconds && Presenter)
			{
				CheckRain(State, *Env, *Presenter);
				NextStage(State, EStage::Sunny);
			}
			return true;
		case EStage::Sunny:
			if (State.StageTime >= RunSeconds && Presenter)
			{
				CheckSunny(State, *Env, *Presenter);
				Env->SetClockScale(1.f);
				Finish(State, Name);
				return false;
			}
			return true;
		case EStage::ArcticSnow:
			if (State.StageTime >= RunSeconds && Presenter)
			{
				CheckArcticSnow(State, *Env, *Presenter);
				Finish(State, Name);
				return false;
			}
			return true;
		}
		return true;
	}

	void Start(UWorld* World, bool bArctic, const TCHAR* Name)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bArcticRun = bArctic;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State, Name](float)
		{
			return Step(WeakWorld, *State, Name);
		}), StepSeconds);
	}

	void RunTemperate(const TArray<FString>& Args, UWorld* World)
	{
		Start(World, false, TEXT("ClimateSmoke"));
	}

	void RunArctic(const TArray<FString>& Args, UWorld* World)
	{
		Start(World, true, TEXT("ClimateArcticSmoke"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ClimateSmoke"),
		TEXT("Dev check (section 17, on /Game/Environment/Weather/Test/L_WeatherRainTest): the temperate profile - SunnyDay start, Rain wets the ground, SunnyDay dries it, no snow, MPC rain scalars, Codex.Env.Climate switch; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTemperate));

	static FAutoConsoleCommandWithWorldAndArgs ArcticCommand(
		TEXT("CodexTactics.ClimateArcticSmoke"),
		TEXT("Dev check (section 17, an arctic map e.g. L_MovementTest): the profile is Arctic, the MPC rain scalars stay 0, the snow cover grows as before, Rain is refused; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunArctic));
}

#endif // !UE_BUILD_SHIPPING
