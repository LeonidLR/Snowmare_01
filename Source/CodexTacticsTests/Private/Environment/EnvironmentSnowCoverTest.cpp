#include "Misc/AutomationTest.h"
#include "Data/EnvironmentTuning.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Environment/WeatherPresenterActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 4a: the persistent snow cover (FEnvironmentState::SnowAmount; docs/systems/environment-weather.md §6 / §12, user
// decision 2026-10-22: it grows while it snows and NEVER recedes, it only stops growing in clear weather), its carry-over / save, the
// level JSON "snow_cover", the MPC helper values and the wind / weather-amount API for the animation layer. UE-only, no Godot reference.

#define SNOW_COVER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.SnowCover." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A state that holds Weather (no transition for the whole dwell unless Elapsed is set into the blend). */
	FEnvironmentState SnowState(EWeatherState Weather, EWeatherState Next, float Snow)
	{
		FEnvironmentState State;
		State.Hour = 12.f;
		State.Weather = Weather;
		State.NextWeather = Next;
		State.WeatherDwellSeconds = 225.f;
		State.WeatherElapsedSeconds = 0.f;
		State.SnowAmount = Snow;
		State.RandomSeed = 17;
		return State;
	}

	TSharedPtr<FJsonObject> SnowTestJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}
}

SNOW_COVER_TEST(FSnowCoverRatesTest, "RatesPerWeather")
bool FSnowCoverRatesTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	TestEqual(TEXT("Snowfall: bare to full in ~8 real minutes"), 1.f / EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Snowfall), 480.f, 1.f);
	TestEqual(TEXT("Blizzard: bare to full in ~3 real minutes"), 1.f / EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Blizzard), 180.f, 1.f);
	TestEqual(TEXT("Clear: no growth"), EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Clear), 0.f);
	TestEqual(TEXT("Overcast: no growth"), EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Overcast), 0.f);

	FEnvironmentState Snowfall = SnowState(EWeatherState::Snowfall, EWeatherState::Overcast, 0.2f);
	const float Gained = EnvironmentRules::StepSnowCover(Config, Snowfall, 60.f);
	TestEqual(TEXT("Snowfall: 60 s add 60 x rate"), Gained, 60.f * Config.SnowCoverRateSnowfall, 1.e-5f);
	TestEqual(TEXT("Snowfall: the state holds it"), Snowfall.SnowAmount, 0.2f + 60.f / 480.f, 1.e-4f);

	FEnvironmentState Blizzard = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.2f);
	EnvironmentRules::StepSnowCover(Config, Blizzard, 60.f);
	TestEqual(TEXT("Blizzard: 60 s add 60 / 180"), Blizzard.SnowAmount, 0.2f + 60.f / 180.f, 1.e-4f);
	TestTrue(TEXT("Blizzard grows faster than Snowfall"), Blizzard.SnowAmount > Snowfall.SnowAmount);

	// Tuned rates are used as given.
	FEnvironmentConfig Fast;
	Fast.SnowCoverRateSnowfall = 0.01f;
	FEnvironmentState Tuned = SnowState(EWeatherState::Snowfall, EWeatherState::Blizzard, 0.f);
	EnvironmentRules::StepSnowCover(Fast, Tuned, 10.f);
	TestEqual(TEXT("tuned rate"), Tuned.SnowAmount, 0.1f, 1.e-5f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverBlendTest, "Blend")
bool FSnowCoverBlendTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config; // transition 45 s at the end of the dwell
	FEnvironmentState State = SnowState(EWeatherState::Snowfall, EWeatherState::Blizzard, 0.f);
	TestEqual(TEXT("before the blend: the Snowfall rate"), EnvironmentRules::GetBlendedSnowCoverRate(Config, State), Config.SnowCoverRateSnowfall, 1.e-7f);
	// Halfway through the 45 s blend.
	State.WeatherElapsedSeconds = State.WeatherDwellSeconds - 22.5f;
	TestEqual(TEXT("blend 0.5"), EnvironmentRules::GetTransitionBlend(Config, State), 0.5f, 1.e-4f);
	TestEqual(TEXT("halfway: the mean of Snowfall and Blizzard"), EnvironmentRules::GetBlendedSnowCoverRate(Config, State),
		0.5f * (Config.SnowCoverRateSnowfall + Config.SnowCoverRateBlizzard), 1.e-6f);

	// Overcast blending into Snowfall: the cover starts growing during the blend, from 0.
	FEnvironmentState Rising = SnowState(EWeatherState::Overcast, EWeatherState::Snowfall, 0.5f);
	TestEqual(TEXT("Overcast (no blend): 0"), EnvironmentRules::GetBlendedSnowCoverRate(Config, Rising), 0.f);
	Rising.WeatherElapsedSeconds = Rising.WeatherDwellSeconds - 9.f; // blend 0.8
	TestEqual(TEXT("Overcast -> Snowfall at 0.8: 0.8 x the Snowfall rate"), EnvironmentRules::GetBlendedSnowCoverRate(Config, Rising),
		0.8f * Config.SnowCoverRateSnowfall, 1.e-6f);

	// Snowfall blending out into Overcast: the growth tapers to 0 but the cover never shrinks.
	FEnvironmentState Easing = SnowState(EWeatherState::Snowfall, EWeatherState::Overcast, 0.5f);
	Easing.WeatherElapsedSeconds = Easing.WeatherDwellSeconds;
	TestEqual(TEXT("Snowfall -> Overcast at the end: 0"), EnvironmentRules::GetBlendedSnowCoverRate(Config, Easing), 0.f, 1.e-7f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverNeverRecedesTest, "NeverRecedes")
bool FSnowCoverNeverRecedesTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	for (const EWeatherState Weather : { EWeatherState::Clear, EWeatherState::Overcast })
	{
		FEnvironmentState State = SnowState(Weather, EWeatherState::Overcast == Weather ? EWeatherState::Clear : EWeatherState::Overcast, 0.7f);
		const float Gained = EnvironmentRules::StepSnowCover(Config, State, 3600.f);
		TestEqual(FString::Printf(TEXT("%s: an hour later still 0.7"), *EnvironmentRules::WeatherToString(Weather)), State.SnowAmount, 0.7f);
		TestEqual(TEXT("nothing gained"), Gained, 0.f);
	}
	// A broken (negative) rate from a hand-edited JSON still never lowers it.
	FEnvironmentConfig Broken;
	Broken.SnowCoverRateSnowfall = -0.5f;
	Broken.SnowCoverRateBlizzard = -0.5f;
	FEnvironmentState State = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.4f);
	EnvironmentRules::StepSnowCover(Broken, State, 100.f);
	TestEqual(TEXT("negative rate: held"), State.SnowAmount, 0.4f);
	// Zero / negative delta: nothing.
	FEnvironmentState Still = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.4f);
	EnvironmentRules::StepSnowCover(Config, Still, 0.f);
	EnvironmentRules::StepSnowCover(Config, Still, -10.f);
	TestEqual(TEXT("no delta: held"), Still.SnowAmount, 0.4f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverClampTest, "ClampAtOne")
bool FSnowCoverClampTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.95f);
	const float Gained = EnvironmentRules::StepSnowCover(Config, State, 600.f);
	TestEqual(TEXT("clamped at 1"), State.SnowAmount, 1.f);
	TestEqual(TEXT("gained only up to 1"), Gained, 0.05f, 1.e-5f);
	EnvironmentRules::StepSnowCover(Config, State, 600.f);
	TestEqual(TEXT("stays at 1"), State.SnowAmount, 1.f);
	// An out-of-range value (a hand-edited save) is brought back into [0, 1] by the next step.
	FEnvironmentState Over = SnowState(EWeatherState::Clear, EWeatherState::Overcast, 3.f);
	EnvironmentRules::StepSnowCover(Config, Over, 1.f);
	TestEqual(TEXT("over 1 clamps to 1"), Over.SnowAmount, 1.f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverFreezeTest, "Freeze")
bool FSnowCoverFreezeTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// A locked level (Demo 01: Night + Snowfall) never grows its cover.
	FEnvironmentState Locked = SnowState(EWeatherState::Snowfall, EWeatherState::Overcast, 0.6f);
	Locked.bLocked = true;
	TestEqual(TEXT("locked: nothing gained"), EnvironmentRules::StepSnowCover(Config, Locked, 600.f), 0.f);
	TestEqual(TEXT("locked: still 0.6"), Locked.SnowAmount, 0.6f);
	// The subsystem steps the cover only while the clock may run (the same gate as the hour and the weather).
	struct FCase { ECodexCombatMode Mode; bool bAIPaused; bool bRuns; const TCHAR* What; };
	const FCase Cases[] = {
		{ ECodexCombatMode::None, false, true, TEXT("exploration") },
		{ ECodexCombatMode::RealTime, false, true, TEXT("real-time combat") },
		{ ECodexCombatMode::TacticalPause, false, false, TEXT("tactical pause") },
		{ ECodexCombatMode::TurnBased, false, false, TEXT("turn-based combat") },
		{ ECodexCombatMode::None, true, false, TEXT("world AI pause") },
	};
	for (const FCase& Case : Cases)
	{
		FEnvironmentState State = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.3f);
		// What UEnvironmentSubsystem::Tick does (CanAdvance -> AdvanceClock, StepWeather, StepSnowCover).
		if (EnvironmentRules::ShouldAdvance(Case.Mode, Case.bAIPaused))
		{
			EnvironmentRules::StepWeather(Config, State, 30.f);
			EnvironmentRules::StepSnowCover(Config, State, 30.f);
		}
		TestEqual(FString::Printf(TEXT("%s: cover %s"), Case.What, Case.bRuns ? TEXT("grows") : TEXT("frozen")), State.SnowAmount > 0.3f, Case.bRuns);
	}
	return true;
}

SNOW_COVER_TEST(FSnowCoverCarryTest, "Carry")
bool FSnowCoverCarryTest::RunTest(const FString&)
{
	FEnvironmentConfig Config;
	Config.SnowCoverStart = 0.35f;
	const FEnvironmentLevelSettings Plain;
	// Fresh run: the config start value.
	const FEnvironmentState Fresh = UEnvironmentSubsystem::ResolveInitialState(Config, Plain, TOptional<FEnvironmentState>(), 7);
	TestEqual(TEXT("fresh run: snow_cover_start"), Fresh.SnowAmount, 0.35f);
	TestEqual(TEXT("MakeInitialState: snow_cover_start"), EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Clear, 3).SnowAmount, 0.35f);
	// A level "snow_cover" on a fresh start.
	FEnvironmentLevelSettings Snowy;
	Snowy.bHasSnowCover = true;
	Snowy.SnowCover = 0.8f;
	TestEqual(TEXT("fresh level with snow_cover 0.8"), UEnvironmentSubsystem::ResolveInitialState(Config, Snowy, TOptional<FEnvironmentState>(), 7).SnowAmount, 0.8f);
	// The locked tutorial (lake: snow_cover 0.6) ...
	FEnvironmentLevelSettings Lake;
	Lake.bLocked = true;
	Lake.bHasStartHour = true;
	Lake.StartHour = 23.f;
	Lake.bHasWeather = true;
	Lake.Weather = EWeatherState::Snowfall;
	Lake.bHasSnowCover = true;
	Lake.SnowCover = 0.6f;
	const FEnvironmentState LakeState = UEnvironmentSubsystem::ResolveInitialState(Config, Lake, TOptional<FEnvironmentState>(), 7);
	TestTrue(TEXT("lake locked"), LakeState.bLocked);
	TestEqual(TEXT("lake: 0.6"), LakeState.SnowAmount, 0.6f);
	// ... hands its cover to the next level, which keeps growing it unlocked (the carried cover wins over the level's / the config's).
	FEnvironmentState Carried = LakeState;
	const FEnvironmentState Next = UEnvironmentSubsystem::ResolveInitialState(Config, Snowy, TOptional<FEnvironmentState>(Carried), 7);
	TestFalse(TEXT("next level unlocked"), Next.bLocked);
	TestEqual(TEXT("next level continues the lake's 0.6"), Next.SnowAmount, 0.6f);
	FEnvironmentState Running = Next;
	EnvironmentRules::StepSnowCover(Config, Running, 48.f);
	TestTrue(TEXT("unlocked under Snowfall: it grows again"), Running.SnowAmount > 0.6f);
	// A locked level ignores the carried state (its own fixed start).
	Carried.SnowAmount = 0.9f;
	TestEqual(TEXT("a locked level starts with its own snow_cover"),
		UEnvironmentSubsystem::ResolveInitialState(Config, Lake, TOptional<FEnvironmentState>(Carried), 7).SnowAmount, 0.6f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverSaveRoundTripTest, "SaveRoundTrip")
bool FSnowCoverSaveRoundTripTest::RunTest(const FString&)
{
	FEnvironmentState Saved = SnowState(EWeatherState::Blizzard, EWeatherState::Snowfall, 0.734f);
	Saved.Hour = 21.375f;
	Saved.WeatherElapsedSeconds = 101.25f;
	Saved.WeatherDwellSeconds = 212.5f;
	Saved.RandomSeed = -123456789;
	Saved.bLocked = true;
	// Through text, as a save file.
	FString Text;
	FJsonSerializer::Serialize(EnvironmentRules::StateToSaveJson(Saved), TJsonWriterFactory<>::Create(&Text));
	const TSharedPtr<FJsonObject> Parsed = SnowTestJson(Text);
	if (!TestTrue(TEXT("the save object parses"), Parsed.IsValid()))
	{
		return false;
	}
	const FEnvironmentState Loaded = EnvironmentRules::StateFromSaveJson(*Parsed, FEnvironmentState());
	TestEqual(TEXT("hour"), Loaded.Hour, Saved.Hour, 1.e-4f);
	TestTrue(TEXT("weather"), Loaded.Weather == EWeatherState::Blizzard);
	TestTrue(TEXT("next weather"), Loaded.NextWeather == EWeatherState::Snowfall);
	TestEqual(TEXT("elapsed"), Loaded.WeatherElapsedSeconds, Saved.WeatherElapsedSeconds, 1.e-4f);
	TestEqual(TEXT("dwell"), Loaded.WeatherDwellSeconds, Saved.WeatherDwellSeconds, 1.e-4f);
	TestEqual(TEXT("seed (negative, exact)"), Loaded.RandomSeed, Saved.RandomSeed);
	TestTrue(TEXT("locked"), Loaded.bLocked);
	TestEqual(TEXT("snow amount"), Loaded.SnowAmount, 0.734f, 1.e-5f);
	TestTrue(TEXT("every key written"), Parsed->HasField(TEXT("snow_amount")) && Parsed->HasField(TEXT("next_weather")) && Parsed->HasField(TEXT("seed")));

	// Every key optional: an empty object / wrong types / an unknown weather keep the fallback; out-of-range values are brought back.
	FEnvironmentState Fallback = SnowState(EWeatherState::Overcast, EWeatherState::Clear, 0.42f);
	const FEnvironmentState Empty = EnvironmentRules::StateFromSaveJson(*SnowTestJson(TEXT("{}")), Fallback);
	TestEqual(TEXT("empty: snow from the fallback"), Empty.SnowAmount, 0.42f);
	TestTrue(TEXT("empty: weather from the fallback"), Empty.Weather == EWeatherState::Overcast && Empty.NextWeather == EWeatherState::Clear);
	const FEnvironmentState Odd = EnvironmentRules::StateFromSaveJson(
		*SnowTestJson(TEXT(R"({"weather": "Rain", "snow_amount": 4.5, "hour": 25.5, "weather_dwell": -3, "seed": "x"})")), Fallback);
	TestTrue(TEXT("unknown weather name keeps the fallback"), Odd.Weather == EWeatherState::Overcast);
	TestEqual(TEXT("snow clamped to 1"), Odd.SnowAmount, 1.f);
	TestEqual(TEXT("hour wrapped"), Odd.Hour, 1.5f, 1.e-4f);
	TestEqual(TEXT("dwell floored at 0"), Odd.WeatherDwellSeconds, 0.f);
	TestEqual(TEXT("a non-number seed keeps the fallback"), Odd.RandomSeed, Fallback.RandomSeed);
	return true;
}

SNOW_COVER_TEST(FSnowCoverLevelJsonTest, "LevelJson")
bool FSnowCoverLevelJsonTest::RunTest(const FString&)
{
	const FEnvironmentLevelSettings Present = LevelJsonRules::ParseEnvironmentSettings(*SnowTestJson(TEXT(R"({"locked": true, "snow_cover": 0.6})")));
	TestTrue(TEXT("snow_cover present"), Present.bHasSnowCover);
	TestEqual(TEXT("snow_cover 0.6"), Present.SnowCover, 0.6f);
	const FEnvironmentLevelSettings Absent = LevelJsonRules::ParseEnvironmentSettings(*SnowTestJson(TEXT("{}")));
	TestFalse(TEXT("absent: not set"), Absent.bHasSnowCover);
	const FEnvironmentLevelSettings High = LevelJsonRules::ParseEnvironmentSettings(*SnowTestJson(TEXT(R"({"snow_cover": 1.7})")));
	TestEqual(TEXT("clamped to 1"), High.SnowCover, 1.f);
	const FEnvironmentLevelSettings Low = LevelJsonRules::ParseEnvironmentSettings(*SnowTestJson(TEXT(R"({"snow_cover": -0.2})")));
	TestEqual(TEXT("clamped to 0"), Low.SnowCover, 0.f);
	TestEqual(TEXT("initial cover: the level's"), EnvironmentRules::GetInitialSnowCover(FEnvironmentConfig(), Present), 0.6f);
	TestEqual(TEXT("initial cover: the config's"), EnvironmentRules::GetInitialSnowCover(FEnvironmentConfig(), Absent), FEnvironmentConfig().SnowCoverStart);

	// The shipped Demo 01 lake: a snowy night, 0.6.
	FString Json;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Demo01_A_FrozenLake.json");
	if (TestTrue(TEXT("lake level JSON readable"), FFileHelper::LoadFileToString(Json, *Path)))
	{
		FLevelCombatConfig Level;
		FString Error;
		if (TestTrue(TEXT("lake level JSON parses"), LevelJsonRules::ParseLevel(Json, TEXT("L_Demo01_A_FrozenLake"), Level, Error)))
		{
			TestTrue(TEXT("lake: snow_cover set"), Level.Environment.bHasSnowCover);
			TestEqual(TEXT("lake: snow_cover 0.6"), Level.Environment.SnowCover, 0.6f);
		}
	}
	return true;
}

SNOW_COVER_TEST(FSnowCoverTuningTest, "Tuning")
bool FSnowCoverTuningTest::RunTest(const FString&)
{
	FEnvironmentConfig Changed;
	Changed.SnowCoverRateSnowfall = 0.003f;
	Changed.SnowCoverRateBlizzard = 0.009f;
	Changed.SnowCoverStart = 0.2f;
	const FEnvironmentConfig Back = EnvironmentTuning::FromJson(*EnvironmentTuning::ToJson(Changed));
	TestEqual(TEXT("rate snowfall round trip"), Back.SnowCoverRateSnowfall, 0.003f, 1.e-6f);
	TestEqual(TEXT("rate blizzard round trip"), Back.SnowCoverRateBlizzard, 0.009f, 1.e-6f);
	TestEqual(TEXT("start round trip"), Back.SnowCoverStart, 0.2f, 1.e-6f);
	const FEnvironmentConfig Clamped = EnvironmentTuning::FromJson(*SnowTestJson(TEXT(R"({"snow_cover_start": 2, "snow_cover_rate_snowfall": -1})")));
	TestEqual(TEXT("start clamped to 1"), Clamped.SnowCoverStart, 1.f);
	TestEqual(TEXT("negative rate clamped to 0"), Clamped.SnowCoverRateSnowfall, 0.f);

	// The shipped environment.json carries the design values.
	FString Json;
	if (TestTrue(TEXT("environment.json readable"), FFileHelper::LoadFileToString(Json, *EnvironmentTuning::GetDefaultPath())))
	{
		const TSharedPtr<FJsonObject> File = SnowTestJson(Json);
		if (TestTrue(TEXT("environment.json parses"), File.IsValid()))
		{
			const FEnvironmentConfig Shipped = EnvironmentTuning::FromJson(*File);
			TestEqual(TEXT("shipped: Snowfall ~8 min"), 1.f / Shipped.SnowCoverRateSnowfall, 480.f, 1.f);
			TestEqual(TEXT("shipped: Blizzard ~3 min"), 1.f / Shipped.SnowCoverRateBlizzard, 180.f, 1.f);
			TestEqual(TEXT("shipped: start 0.35"), Shipped.SnowCoverStart, 0.35f, 1.e-5f);
		}
	}
	return true;
}

SNOW_COVER_TEST(FSnowCoverPresenterParamsTest, "PresenterParams")
bool FSnowCoverPresenterParamsTest::RunTest(const FString&)
{
	TestEqual(TEXT("IsSnowing: Clear 0"), AWeatherPresenterActor::ComputeIsSnowing(0.f), 0.f);
	TestEqual(TEXT("IsSnowing: a light fall 0.5"), AWeatherPresenterActor::ComputeIsSnowing(0.125f), 0.5f, 1.e-5f);
	TestEqual(TEXT("IsSnowing: Snowfall preset 1"), AWeatherPresenterActor::ComputeIsSnowing(EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Snowfall).SnowIntensity), 1.f);
	TestEqual(TEXT("IsSnowing: clamped"), AWeatherPresenterActor::ComputeIsSnowing(3.f), 1.f);
	float MinYaw = 1000.f;
	float MaxYaw = -1000.f;
	for (int32 Step = 0; Step <= 36; ++Step)
	{
		const FVector Wind = AWeatherPresenterActor::ComputeWindDirection(Step * 5.f, 35.f, 25.f, 180.f);
		TestTrue(TEXT("wind direction is a horizontal unit vector"), FMath::IsNearlyEqual(Wind.Size(), 1.f, 1.e-4f) && FMath::IsNearlyZero(Wind.Z, 1.e-4f));
		const float Yaw = Wind.Rotation().Yaw;
		MinYaw = FMath::Min(MinYaw, Yaw);
		MaxYaw = FMath::Max(MaxYaw, Yaw);
	}
	TestTrue(TEXT("the wind drifts within base +- drift"), MinYaw >= 35.f - 25.01f && MaxYaw <= 35.f + 25.01f && MaxYaw - MinYaw > 40.f);
	TestTrue(TEXT("the wind changes slowly (1 s apart: < 1 degree)"),
		AWeatherPresenterActor::ComputeWindDirection(10.f, 35.f, 25.f, 180.f).Rotation().Yaw - AWeatherPresenterActor::ComputeWindDirection(11.f, 35.f, 25.f, 180.f).Rotation().Yaw < 1.f);
	return true;
}

SNOW_COVER_TEST(FSnowCoverWindAndWeatherAmountTest, "WindAndWeatherAmount")
bool FSnowCoverWindAndWeatherAmountTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = SnowState(EWeatherState::Snowfall, EWeatherState::Blizzard, 0.f);
	TestEqual(TEXT("no blend: Snowfall 1"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Snowfall), 1.f);
	TestEqual(TEXT("no blend: Blizzard 0"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Blizzard), 0.f);
	TestEqual(TEXT("no blend: Clear 0"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Clear), 0.f);
	State.WeatherElapsedSeconds = State.WeatherDwellSeconds - 11.25f; // blend 0.75
	TestEqual(TEXT("blend 0.75: Snowfall 0.25"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Snowfall), 0.25f, 1.e-4f);
	TestEqual(TEXT("blend 0.75: Blizzard 0.75"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Blizzard), 0.75f, 1.e-4f);
	float Sum = 0.f;
	for (const EWeatherState Weather : { EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard })
	{
		Sum += EnvironmentRules::GetWeatherAmount(Config, State, Weather);
	}
	TestEqual(TEXT("the four amounts sum to 1"), Sum, 1.f, 1.e-4f);
	State.bLocked = true;
	TestEqual(TEXT("locked: no blend, Snowfall fully"), EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Snowfall), 1.f);

	const FWeatherVisualPreset Clear = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Clear);
	const FWeatherVisualPreset Blizzard = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Blizzard);
	TestEqual(TEXT("wind: Clear preset"), EnvironmentVisualRules::GetBlendedWindStrength(Clear, Blizzard, 0.f), Clear.WindStrength, 1.e-6f);
	TestEqual(TEXT("wind: Blizzard preset"), EnvironmentVisualRules::GetBlendedWindStrength(Clear, Blizzard, 1.f), Blizzard.WindStrength, 1.e-6f);
	TestEqual(TEXT("wind: halfway"), EnvironmentVisualRules::GetBlendedWindStrength(Clear, Blizzard, 0.5f), 0.5f * (Clear.WindStrength + Blizzard.WindStrength), 1.e-6f);
	TestTrue(TEXT("wind: Blizzard is the windiest"), Blizzard.WindStrength >= EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Snowfall).WindStrength);
	TestEqual(TEXT("wind: blend clamped"), EnvironmentVisualRules::GetBlendedWindStrength(Clear, Blizzard, 2.f), Blizzard.WindStrength, 1.e-6f);
	return true;
}

#undef SNOW_COVER_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
