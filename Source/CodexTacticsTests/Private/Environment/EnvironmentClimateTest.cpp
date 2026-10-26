#include "Misc/AutomationTest.h"
#include "Data/EnvironmentTuning.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Misc/FileHelper.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Section 17 (docs/systems/environment-weather.md, user decision 2026-10-26): the per-level CLIMATE PROFILE. The whole game stays
// Arctic; narrative levels may be Temperate ("environment": {"climate": "temperate"}): the same one-step ladder read as SunnyDay /
// Overcast / Rain / Storm, no snow growth, mild cold, wetness + puddles. UE-only, no Godot reference.

#define CLIMATE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.Climate." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FEnvironmentState ClimateState(EClimateProfile Climate, EWeatherState Weather, EWeatherState Next, float Hour = 12.f)
	{
		FEnvironmentState State;
		State.Climate = Climate;
		State.Hour = Hour;
		State.Weather = Weather;
		State.NextWeather = Next;
		State.WeatherDwellSeconds = 225.f;
		State.WeatherElapsedSeconds = 0.f;
		State.RandomSeed = 4242;
		return State;
	}

	TSharedPtr<FJsonObject> ClimateJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	FEnvironmentLevelSettings ParseEnv(const FString& Text, FString* OutError = nullptr)
	{
		const TSharedPtr<FJsonObject> Json = ClimateJson(Text);
		return Json.IsValid() ? LevelJsonRules::ParseEnvironmentSettings(*Json, OutError) : FEnvironmentLevelSettings();
	}

	/** Every field a step can touch, compared exactly. */
	bool SameState(const FEnvironmentState& A, const FEnvironmentState& B)
	{
		return A.Hour == B.Hour && A.Weather == B.Weather && A.NextWeather == B.NextWeather && A.WeatherElapsedSeconds == B.WeatherElapsedSeconds
			&& A.WeatherDwellSeconds == B.WeatherDwellSeconds && A.RandomSeed == B.RandomSeed && A.bLocked == B.bLocked && A.SnowAmount == B.SnowAmount
			&& A.ObjectSnow.Top == B.ObjectSnow.Top && A.ObjectSnow.PX == B.ObjectSnow.PX && A.ObjectSnow.NX == B.ObjectSnow.NX
			&& A.ObjectSnow.PY == B.ObjectSnow.PY && A.ObjectSnow.NY == B.ObjectSnow.NY && A.Climate == B.Climate && A.Wetness == B.Wetness
			&& A.PuddleAmount == B.PuddleAmount;
	}
}

CLIMATE_TEST(FClimateProfileParseTest, "ProfileParse")
bool FClimateProfileParseTest::RunTest(const FString&)
{
	EClimateProfile Climate = EClimateProfile::Arctic;
	TestTrue(TEXT("\"temperate\" parses"), EnvironmentRules::ClimateFromString(TEXT(" Temperate "), Climate) && Climate == EClimateProfile::Temperate);
	TestTrue(TEXT("\"arctic\" parses"), EnvironmentRules::ClimateFromString(TEXT("ARCTIC"), Climate) && Climate == EClimateProfile::Arctic);
	TestFalse(TEXT("\"tropical\" is unknown"), EnvironmentRules::ClimateFromString(TEXT("tropical"), Climate));
	TestEqual(TEXT("to string"), EnvironmentRules::ClimateToString(EClimateProfile::Temperate), FString(TEXT("temperate")));

	// Absent = Arctic (every existing level).
	const FEnvironmentLevelSettings Absent = ParseEnv(TEXT(R"({"start_hour": 9})"));
	TestEqual(TEXT("absent climate = arctic"), Absent.Climate, EClimateProfile::Arctic);
	TestEqual(TEXT("a default level setting is arctic"), FEnvironmentLevelSettings().Climate, EClimateProfile::Arctic);

	// A temperate narrative level fixed at Rain (its own weather names), with start wetness.
	FString Error;
	const FEnvironmentLevelSettings Rainy = ParseEnv(TEXT(R"({"weather": "Rain", "locked": true, "climate": "temperate", "wetness": 0.7, "puddles": 1.4})"), &Error);
	TestEqual(TEXT("temperate"), Rainy.Climate, EClimateProfile::Temperate);
	TestTrue(TEXT("Rain -> the Snowfall rung (parsed whatever the key order)"), Rainy.bHasWeather && Rainy.Weather == EWeatherState::Snowfall);
	TestTrue(TEXT("locked"), Rainy.bLocked);
	TestTrue(TEXT("wetness 0.7"), Rainy.bHasWetness && FMath::IsNearlyEqual(Rainy.Wetness, 0.7f));
	TestTrue(TEXT("puddles clamped to 1"), Rainy.bHasPuddles && Rainy.Puddles == 1.f);
	TestTrue(TEXT("no error"), Error.IsEmpty());

	TestEqual(TEXT("Sunny -> Clear"), ParseEnv(TEXT(R"({"climate": "temperate", "weather": "Sunny"})")).Weather, EWeatherState::Clear);
	TestEqual(TEXT("SunnyDay -> Clear"), ParseEnv(TEXT(R"({"climate": "temperate", "weather": "sunnyday"})")).Weather, EWeatherState::Clear);
	TestEqual(TEXT("Storm -> Blizzard"), ParseEnv(TEXT(R"({"climate": "temperate", "weather": "Storm"})")).Weather, EWeatherState::Blizzard);
	TestEqual(TEXT("the ladder names stay valid on temperate"), ParseEnv(TEXT(R"({"climate": "temperate", "weather": "Overcast"})")).Weather, EWeatherState::Overcast);

	// On an arctic level the temperate names are unknown (ignored + reported).
	FString ArcticError;
	const FEnvironmentLevelSettings ArcticRain = ParseEnv(TEXT(R"({"weather": "Rain"})"), &ArcticError);
	TestFalse(TEXT("arctic: Rain ignored"), ArcticRain.bHasWeather);
	TestTrue(TEXT("arctic: Rain reported"), ArcticError.Contains(TEXT("Rain")));

	FString ClimateError;
	const FEnvironmentLevelSettings Unknown = ParseEnv(TEXT(R"({"climate": "tropical", "weather": "Snowfall"})"), &ClimateError);
	TestEqual(TEXT("unknown climate stays arctic"), Unknown.Climate, EClimateProfile::Arctic);
	TestTrue(TEXT("unknown climate reported"), ClimateError.Contains(TEXT("tropical")));
	TestTrue(TEXT("its weather still parses"), Unknown.bHasWeather && Unknown.Weather == EWeatherState::Snowfall);

	// The shipped test level.
	FString Text;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_WeatherRainTest.json");
	if (TestTrue(TEXT("L_WeatherRainTest.json exists"), FFileHelper::LoadFileToString(Text, *Path)))
	{
		const TSharedPtr<FJsonObject> Root = ClimateJson(Text);
		const TSharedPtr<FJsonObject>* Environment = nullptr;
		if (TestTrue(TEXT("it has an environment block"), Root.IsValid() && Root->TryGetObjectField(TEXT("environment"), Environment)))
		{
			TestEqual(TEXT("the rain test map is temperate"), LevelJsonRules::ParseEnvironmentSettings(**Environment).Climate, EClimateProfile::Temperate);
		}
	}
	return true;
}

CLIMATE_TEST(FClimateNameMappingTest, "NameMapping")
bool FClimateNameMappingTest::RunTest(const FString&)
{
	const EWeatherState All[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};
	for (const EWeatherState Weather : All)
	{
		TestEqual(TEXT("arctic display name = the ladder name"), EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Arctic, Weather),
			EnvironmentRules::WeatherToString(Weather));
		EWeatherState Back = EWeatherState::Clear;
		TestTrue(TEXT("a temperate display name parses back on temperate"),
			EnvironmentRules::WeatherFromString(EClimateProfile::Temperate, EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, Weather), Back)
				&& Back == Weather);
	}
	TestEqual(TEXT("Clear = SunnyDay"), EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, EWeatherState::Clear), FString(TEXT("SunnyDay")));
	TestEqual(TEXT("Overcast = Overcast"), EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, EWeatherState::Overcast), FString(TEXT("Overcast")));
	TestEqual(TEXT("Snowfall = Rain"), EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, EWeatherState::Snowfall), FString(TEXT("Rain")));
	TestEqual(TEXT("Blizzard = Storm"), EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, EWeatherState::Blizzard), FString(TEXT("Storm")));

	EWeatherState Weather = EWeatherState::Clear;
	TestFalse(TEXT("arctic: Rain unknown"), EnvironmentRules::WeatherFromString(EClimateProfile::Arctic, TEXT("Rain"), Weather));
	TestFalse(TEXT("arctic: Sunny unknown"), EnvironmentRules::WeatherFromString(EClimateProfile::Arctic, TEXT("Sunny"), Weather));
	TestFalse(TEXT("the plain parser (saves) never takes Rain"), EnvironmentRules::WeatherFromString(TEXT("Rain"), Weather));
	TestTrue(TEXT("arctic: Blizzard"), EnvironmentRules::WeatherFromString(EClimateProfile::Arctic, TEXT("blizzard"), Weather) && Weather == EWeatherState::Blizzard);
	TestTrue(TEXT("temperate: storm (any case)"), EnvironmentRules::WeatherFromString(EClimateProfile::Temperate, TEXT(" STORM "), Weather) && Weather == EWeatherState::Blizzard);
	TestFalse(TEXT("temperate: Hail unknown"), EnvironmentRules::WeatherFromString(EClimateProfile::Temperate, TEXT("Hail"), Weather));
	return true;
}

CLIMATE_TEST(FClimateTemperateLadderTest, "TemperateLadder")
bool FClimateTemperateLadderTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// The SAME ladder and RNG: a temperate state walks exactly the rungs an arctic one does with the same seed.
	FEnvironmentState Arctic = ClimateState(EClimateProfile::Arctic, EWeatherState::Overcast, EWeatherState::Snowfall);
	FEnvironmentState Temperate = Arctic;
	Temperate.Climate = EClimateProfile::Temperate;
	EWeatherState Previous = Temperate.Weather;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		EnvironmentRules::StepWeather(Config, Arctic, 7.5f);
		EnvironmentRules::StepWeather(Config, Temperate, 7.5f);
		TestTrue(TEXT("one ladder step at a time"), Temperate.Weather == Previous || EnvironmentRules::AreNeighbours(Previous, Temperate.Weather));
		Previous = Temperate.Weather;
	}
	TestTrue(TEXT("same weather sequence as arctic"), Arctic.Weather == Temperate.Weather && Arctic.NextWeather == Temperate.NextWeather
		&& Arctic.RandomSeed == Temperate.RandomSeed && Arctic.WeatherElapsedSeconds == Temperate.WeatherElapsedSeconds);
	FRandomStream Stream(1);
	TestEqual(TEXT("Storm steps down to Rain (Blizzard -> Snowfall forced)"), EnvironmentRules::PickNextWeather(Config, EWeatherState::Blizzard, Stream),
		EWeatherState::Snowfall);

	// Rain intensity: 0 SunnyDay / Overcast, 0.6 Rain, 1 Storm, blended; always 0 on arctic.
	TestEqual(TEXT("SunnyDay: no rain"), EnvironmentRules::GetRainIntensity(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast)), 0.f);
	TestEqual(TEXT("Overcast: no rain"), EnvironmentRules::GetRainIntensity(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Overcast, EWeatherState::Clear)), 0.f);
	TestEqual(TEXT("Rain 0.6"), EnvironmentRules::GetRainIntensity(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast)), 0.6f, 1.e-5f);
	TestEqual(TEXT("Storm 1"), EnvironmentRules::GetRainIntensity(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Blizzard, EWeatherState::Snowfall)), 1.f, 1.e-5f);
	FEnvironmentState Blending = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Blizzard);
	Blending.WeatherElapsedSeconds = Blending.WeatherDwellSeconds - 0.5f * Config.WeatherTransitionSeconds; // half-way through the blend
	TestEqual(TEXT("Rain -> Storm half-way: 0.8"), EnvironmentRules::GetRainIntensity(Config, Blending), 0.8f, 1.e-4f);
	TestEqual(TEXT("arctic Snowfall: no rain"), EnvironmentRules::GetRainIntensity(Config, ClimateState(EClimateProfile::Arctic, EWeatherState::Snowfall, EWeatherState::Blizzard)), 0.f);

	// MPC helpers: IsRaining / ScreenDrops (user decision: ON while it rains).
	TestEqual(TEXT("IsRaining saturates at 0.25"), EnvironmentRules::ComputeIsRaining(0.6f), 1.f);
	TestEqual(TEXT("IsRaining 0 dry"), EnvironmentRules::ComputeIsRaining(0.f), 0.f);
	TestTrue(TEXT("screen drops in Rain"), EnvironmentRules::ShouldShowScreenDrops(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast)));
	TestFalse(TEXT("no screen drops in SunnyDay"), EnvironmentRules::ShouldShowScreenDrops(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast)));
	TestFalse(TEXT("no screen drops on arctic"), EnvironmentRules::ShouldShowScreenDrops(Config, ClimateState(EClimateProfile::Arctic, EWeatherState::Blizzard, EWeatherState::Snowfall)));
	FEnvironmentConfig NoDrops;
	NoDrops.Temperate.bScreenDrops = false;
	TestFalse(TEXT("screen_drops false"), EnvironmentRules::ShouldShowScreenDrops(NoDrops, ClimateState(EClimateProfile::Temperate, EWeatherState::Blizzard, EWeatherState::Snowfall)));

	// Gameplay switches per profile.
	TestTrue(TEXT("arctic roster active"), EnvironmentRules::IsRosterActive(Config, EClimateProfile::Arctic));
	TestFalse(TEXT("temperate roster off by default (waves as written)"), EnvironmentRules::IsRosterActive(Config, EClimateProfile::Temperate));
	TestTrue(TEXT("arctic falling snow"), EnvironmentRules::IsFallingSnowAllowed(Config, EClimateProfile::Arctic));
	TestFalse(TEXT("temperate: no falling snow"), EnvironmentRules::IsFallingSnowAllowed(Config, EClimateProfile::Temperate));

	// Cold: mild, rain colder than sun, the arctic table untouched.
	const float Sunny = EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, EWeatherState::Clear);
	const float Rain = EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, EWeatherState::Snowfall);
	const float Storm = EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, EWeatherState::Blizzard);
	TestTrue(TEXT("sun < overcast < rain < storm"), Sunny < EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, EWeatherState::Overcast)
		&& EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, EWeatherState::Overcast) < Rain && Rain < Storm);
	for (const EWeatherState Weather : {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard})
	{
		const float Ratio = EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Temperate, Weather) / EnvironmentRules::GetColdWeatherMultiplier(Config, Weather);
		TestTrue(*FString::Printf(TEXT("%s: 0.2..0.4 of arctic (%.2f)"), *EnvironmentRules::GetWeatherDisplayName(EClimateProfile::Temperate, Weather), Ratio),
			Ratio >= 0.2f && Ratio <= 0.4f);
	}
	TestEqual(TEXT("temperate night phase"), EnvironmentRules::GetColdPhaseMultiplier(Config, EClimateProfile::Temperate, ETimeOfDayPhase::Night), Config.Temperate.ColdMultNight);
	const FEnvironmentState RainState = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast);
	TestEqual(TEXT("blended cold reads the temperate table"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, RainState), Rain);
	return true;
}

CLIMATE_TEST(FClimateWetnessTest, "WetnessRates")
bool FClimateWetnessTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const FTemperateClimateConfig& T = Config.Temperate;
	constexpr float HighSun = 52.f;
	constexpr float BelowHorizon = -20.f;

	// Rain: +1/120 per second; Storm faster.
	FEnvironmentState Rain = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast);
	EnvironmentRules::StepWetness(Config, Rain, 30.f, HighSun);
	TestEqual(TEXT("Rain 30 s: +30/120"), Rain.Wetness, 30.f * T.WetnessRateRain, 1.e-5f);
	TestEqual(TEXT("no puddles below the threshold"), Rain.PuddleAmount, 0.f);
	FEnvironmentState Storm = ClimateState(EClimateProfile::Temperate, EWeatherState::Blizzard, EWeatherState::Snowfall);
	EnvironmentRules::StepWetness(Config, Storm, 30.f, HighSun);
	TestTrue(TEXT("Storm wets faster than Rain"), Storm.Wetness > Rain.Wetness);

	// The blend: half-way Rain -> Overcast the rate is half rain, half overcast drying.
	FEnvironmentState Blend = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast);
	Blend.WeatherElapsedSeconds = Blend.WeatherDwellSeconds - 0.5f * Config.WeatherTransitionSeconds;
	TestEqual(TEXT("blend-aware rate"), EnvironmentRules::GetWetnessRate(Config, Blend, HighSun), 0.5f * T.WetnessRateRain - 0.5f * T.DryingRateOvercast, 1.e-6f);

	// Drying: SunnyDay with the sun high at the sunny rate, at night / under cloud at the overcast rate.
	TestEqual(TEXT("SunnyDay noon dries at drying_rate_sunny"),
		EnvironmentRules::GetWetnessRate(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast), HighSun), -T.DryingRateSunny, 1.e-6f);
	TestEqual(TEXT("SunnyDay night dries like overcast"),
		EnvironmentRules::GetWetnessRate(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast, 23.f), BelowHorizon),
		-T.DryingRateOvercast, 1.e-6f);
	TestEqual(TEXT("SunnyDay low sun (10 deg) half-way"),
		EnvironmentRules::GetWetnessRate(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast), 10.f),
		-FMath::Lerp(T.DryingRateOvercast, T.DryingRateSunny, 0.5f), 1.e-6f);
	TestEqual(TEXT("Overcast dries slowly"),
		EnvironmentRules::GetWetnessRate(Config, ClimateState(EClimateProfile::Temperate, EWeatherState::Overcast, EWeatherState::Clear), HighSun), -T.DryingRateOvercast, 1.e-6f);
	TestTrue(TEXT("sun dries faster than overcast"), T.DryingRateSunny > T.DryingRateOvercast);

	// Puddles lag: they fill only after the ground is wet, only while it rains, and drain slower than the ground dries.
	FEnvironmentState Soak = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast);
	float FirstPuddleAt = -1.f;
	for (int32 Second = 1; Second <= 300; ++Second)
	{
		EnvironmentRules::StepWetness(Config, Soak, 1.f, HighSun);
		if (FirstPuddleAt < 0.f && Soak.PuddleAmount > 0.f)
		{
			FirstPuddleAt = static_cast<float>(Second);
		}
		TestTrue(TEXT("puddles never above their target"), Soak.PuddleAmount <= EnvironmentRules::GetPuddleTarget(Config, Soak.Wetness) + 1.e-6f);
	}
	TestTrue(*FString::Printf(TEXT("puddles start once the ground is wetter than %.2f (at %.0f s)"), T.PuddleWetnessThreshold, FirstPuddleAt),
		FirstPuddleAt > T.PuddleWetnessThreshold / T.WetnessRateRain - 1.f);
	TestEqual(TEXT("soaked after 300 s of rain"), Soak.Wetness, 1.f);
	TestTrue(*FString::Printf(TEXT("puddles filled (%.2f)"), Soak.PuddleAmount), Soak.PuddleAmount > 0.95f);
	TestEqual(TEXT("puddle target at 0.75 wetness"), EnvironmentRules::GetPuddleTarget(Config, 0.75f), 0.5f, 1.e-6f);

	FEnvironmentState Dry = Soak;
	Dry.Weather = EWeatherState::Clear;
	Dry.NextWeather = EWeatherState::Overcast;
	Dry.WeatherElapsedSeconds = 0.f;
	EnvironmentRules::StepWetness(Config, Dry, 60.f, HighSun);
	TestEqual(TEXT("SunnyDay 60 s: the ground dried by 60 x sunny rate"), Dry.Wetness, 1.f - 60.f * T.DryingRateSunny, 1.e-4f);
	TestEqual(TEXT("puddles drained by 60 x drain rate (slower)"), Dry.PuddleAmount, Soak.PuddleAmount - 60.f * T.PuddleDrainRate, 1.e-4f);
	TestTrue(TEXT("puddles drain slower than the ground dries"), Soak.PuddleAmount - Dry.PuddleAmount < 1.f - Dry.Wetness);
	// A drying ground never fills puddles, even below their target.
	FEnvironmentState Drying = ClimateState(EClimateProfile::Temperate, EWeatherState::Clear, EWeatherState::Overcast);
	Drying.Wetness = 1.f;
	Drying.PuddleAmount = 0.f;
	EnvironmentRules::StepWetness(Config, Drying, 5.f, HighSun);
	TestEqual(TEXT("no puddles fill while drying"), Drying.PuddleAmount, 0.f);

	// Frozen: a locked or an arctic state never changes; nothing goes outside 0..1.
	FEnvironmentState Locked = ClimateState(EClimateProfile::Temperate, EWeatherState::Snowfall, EWeatherState::Overcast);
	Locked.bLocked = true;
	Locked.Wetness = 0.3f;
	EnvironmentRules::StepWetness(Config, Locked, 100.f, HighSun);
	TestEqual(TEXT("locked: unchanged"), Locked.Wetness, 0.3f);
	FEnvironmentState Arctic = ClimateState(EClimateProfile::Arctic, EWeatherState::Blizzard, EWeatherState::Snowfall);
	EnvironmentRules::StepWetness(Config, Arctic, 100.f, HighSun);
	TestTrue(TEXT("arctic: no wetness"), Arctic.Wetness == 0.f && Arctic.PuddleAmount == 0.f);
	TestEqual(TEXT("arctic rate 0"), EnvironmentRules::GetWetnessRate(Config, Arctic, HighSun), 0.f);

	// Start values: a temperate level that starts in rain is already wet; the level's keys win; arctic 0 / 0.
	FEnvironmentLevelSettings Level;
	Level.Climate = EClimateProfile::Temperate;
	float Wetness = -1.f;
	float Puddles = -1.f;
	EnvironmentRules::GetInitialWetness(Config, Level, EWeatherState::Snowfall, Wetness, Puddles);
	TestTrue(TEXT("Rain start: wet, half puddles"), Wetness == T.StartWetnessRain && Puddles == T.StartPuddlesRain);
	EnvironmentRules::GetInitialWetness(Config, Level, EWeatherState::Clear, Wetness, Puddles);
	TestTrue(TEXT("SunnyDay start: dry"), Wetness == 0.f && Puddles == 0.f);
	Level.bHasWetness = true;
	Level.Wetness = 0.2f;
	EnvironmentRules::GetInitialWetness(Config, Level, EWeatherState::Blizzard, Wetness, Puddles);
	TestTrue(TEXT("level wetness wins"), Wetness == 0.2f && Puddles == T.StartPuddlesStorm);
	FEnvironmentLevelSettings ArcticLevel;
	EnvironmentRules::GetInitialWetness(Config, ArcticLevel, EWeatherState::Blizzard, Wetness, Puddles);
	TestTrue(TEXT("arctic start 0 / 0"), Wetness == 0.f && Puddles == 0.f);
	return true;
}

CLIMATE_TEST(FClimateNoSnowTest, "NoSnowGrowthInTemperate")
bool FClimateNoSnowTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// Storm (the Blizzard rung) for 10 minutes: nothing whitens on a temperate level.
	FEnvironmentState Storm = ClimateState(EClimateProfile::Temperate, EWeatherState::Blizzard, EWeatherState::Snowfall);
	Storm.SnowAmount = 0.f;
	TestEqual(TEXT("blended snow cover rate 0"), EnvironmentRules::GetBlendedSnowCoverRate(Config, Storm), 0.f);
	TestEqual(TEXT("no snowing amount (characters)"), EnvironmentRules::GetSnowingAmount(Config, Storm), 0.f);
	for (int32 Step = 0; Step < 600; ++Step)
	{
		EnvironmentRules::StepSnowCover(Config, Storm, 1.f);
		EnvironmentRules::StepObjectSnow(Config, Storm, 1.f, FVector2D(-1.f, 0.f), 1.f, 30.f);
	}
	TestEqual(TEXT("snow cover stays 0"), Storm.SnowAmount, 0.f);
	TestTrue(TEXT("object snow stays 0 (top and the windward side)"), Storm.ObjectSnow.IsZero());

	// A temperate level's start cover is 0 unless the level sets it; arctic keeps snow_cover_start.
	FEnvironmentLevelSettings Temperate;
	Temperate.Climate = EClimateProfile::Temperate;
	TestEqual(TEXT("temperate start cover 0"), EnvironmentRules::GetInitialSnowCover(Config, Temperate), 0.f);
	Temperate.bHasSnowCover = true;
	Temperate.SnowCover = 0.4f;
	TestEqual(TEXT("a temperate level may override it"), EnvironmentRules::GetInitialSnowCover(Config, Temperate), 0.4f);
	TestEqual(TEXT("arctic start cover = snow_cover_start"), EnvironmentRules::GetInitialSnowCover(Config, FEnvironmentLevelSettings()), Config.SnowCoverStart);

	// The arctic Snowfall still grows the cover and snows on the characters.
	FEnvironmentState Snowfall = ClimateState(EClimateProfile::Arctic, EWeatherState::Snowfall, EWeatherState::Overcast);
	TestEqual(TEXT("arctic Snowfall rate"), EnvironmentRules::GetBlendedSnowCoverRate(Config, Snowfall), Config.SnowCoverRateSnowfall);
	TestEqual(TEXT("arctic snowing amount 1"), EnvironmentRules::GetSnowingAmount(Config, Snowfall), 1.f);

	// A fresh temperate level resolves with no snow and the level's wetness.
	FEnvironmentLevelSettings Level;
	Level.Climate = EClimateProfile::Temperate;
	Level.bHasWeather = true;
	Level.Weather = EWeatherState::Snowfall;
	const FEnvironmentState Fresh = UEnvironmentSubsystem::ResolveInitialState(Config, Level, TOptional<FEnvironmentState>(), 77);
	TestTrue(TEXT("fresh temperate: climate, Rain, no snow, wet"), Fresh.Climate == EClimateProfile::Temperate && Fresh.Weather == EWeatherState::Snowfall
		&& Fresh.SnowAmount == 0.f && Fresh.ObjectSnow.IsZero() && Fresh.Wetness == Config.Temperate.StartWetnessRain);
	return true;
}

CLIMATE_TEST(FClimateArcticUnchangedTest, "ArcticUnchanged")
bool FClimateArcticUnchangedTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	TestEqual(TEXT("a default state is arctic"), FEnvironmentState().Climate, EClimateProfile::Arctic);
	TestTrue(TEXT("a default state is dry"), FEnvironmentState().Wetness == 0.f && FEnvironmentState().PuddleAmount == 0.f);

	// The full tick of the subsystem (clock, weather, snow cover, object snow, wetness) on an arctic state equals the pre-section-17 tick
	// (no wetness step): the profile adds nothing.
	FEnvironmentState WithClimate = ClimateState(EClimateProfile::Arctic, EWeatherState::Snowfall, EWeatherState::Blizzard, 8.f);
	WithClimate.SnowAmount = 0.35f;
	WithClimate.ObjectSnow.Top = 0.35f;
	FEnvironmentState Old = WithClimate;
	for (int32 Step = 0; Step < 2400; ++Step)
	{
		const float Dt = 0.5f;
		for (FEnvironmentState* State : {&WithClimate, &Old})
		{
			State->Hour = EnvironmentRules::AdvanceClock(Config, State->Hour, Dt);
			EnvironmentRules::StepWeather(Config, *State, Dt);
			EnvironmentRules::StepSnowCover(Config, *State, Dt);
			EnvironmentRules::StepObjectSnow(Config, *State, Dt, FVector2D(-0.8f, 0.6f), 0.6f, 20.f);
		}
		EnvironmentRules::StepWetness(Config, WithClimate, Dt, 20.f);
	}
	TestTrue(TEXT("20 design minutes: identical state"), SameState(WithClimate, Old));
	TestTrue(TEXT("the snow grew there (it is a real check)"), WithClimate.SnowAmount > 0.35f);

	for (const EWeatherState Weather : {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard})
	{
		TestEqual(TEXT("arctic cold weather table"), EnvironmentRules::GetColdWeatherMultiplier(Config, EClimateProfile::Arctic, Weather),
			EnvironmentRules::GetColdWeatherMultiplier(Config, Weather));
		const FWeatherVisualPreset A = EnvironmentVisualRules::GetDefaultPreset(Weather);
		const FWeatherVisualPreset B = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Arctic, Weather);
		TestTrue(TEXT("arctic presets unchanged"), A.SunIntensityScale == B.SunIntensityScale && A.FogDensity == B.FogDensity && A.SnowIntensity == B.SnowIntensity
			&& A.FrostAmount == B.FrostAmount && A.Saturation == B.Saturation && A.ExposureBias == B.ExposureBias && A.WindStrength == B.WindStrength);
		const FWeatherVisualPreset Temperate = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Temperate, Weather);
		TestTrue(TEXT("temperate presets: no snow, no frost"), Temperate.SnowIntensity == 0.f && Temperate.FrostAmount == 0.f);
	}
	for (const ETimeOfDayPhase Phase : {ETimeOfDayPhase::EarlyMorning, ETimeOfDayPhase::Midday, ETimeOfDayPhase::Afternoon, ETimeOfDayPhase::Night})
	{
		TestEqual(TEXT("arctic cold phase table"), EnvironmentRules::GetColdPhaseMultiplier(Config, EClimateProfile::Arctic, Phase),
			EnvironmentRules::GetColdPhaseMultiplier(Config, Phase));
	}

	// A fresh arctic level resolves exactly as before (arctic, dry, snow_cover_start).
	FEnvironmentLevelSettings Level;
	Level.bHasWeather = true;
	Level.Weather = EWeatherState::Blizzard;
	const FEnvironmentState Fresh = UEnvironmentSubsystem::ResolveInitialState(Config, Level, TOptional<FEnvironmentState>(), 5);
	TestTrue(TEXT("fresh arctic: arctic, dry, the start cover"), Fresh.Climate == EClimateProfile::Arctic && Fresh.Wetness == 0.f && Fresh.PuddleAmount == 0.f
		&& Fresh.SnowAmount == Config.SnowCoverStart && Fresh.ObjectSnow.Top == Config.SnowCoverStart);
	TestEqual(TEXT("arctic: no rain"), EnvironmentRules::GetRainIntensity(Config, Fresh), 0.f);
	// An arctic carry into an arctic level continues exactly.
	FEnvironmentState Carry = Fresh;
	Carry.Hour = 16.25f;
	Carry.SnowAmount = 0.71f;
	Carry.WeatherElapsedSeconds = 99.f;
	const FEnvironmentState Continued = UEnvironmentSubsystem::ResolveInitialState(Config, FEnvironmentLevelSettings(), TOptional<FEnvironmentState>(Carry), 9);
	TestTrue(TEXT("arctic -> arctic continues exactly"), SameState(Continued, Carry));

	// The shipped Demo 01 lake stays arctic, locked Night + Snowfall.
	FString Text;
	if (TestTrue(TEXT("lake JSON"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Demo01_A_FrozenLake.json")))))
	{
		const TSharedPtr<FJsonObject> Root = ClimateJson(Text);
		const TSharedPtr<FJsonObject>* Environment = nullptr;
		if (TestTrue(TEXT("lake environment"), Root.IsValid() && Root->TryGetObjectField(TEXT("environment"), Environment)))
		{
			const FEnvironmentLevelSettings Lake = LevelJsonRules::ParseEnvironmentSettings(**Environment);
			TestTrue(TEXT("lake: arctic, locked, Snowfall"), Lake.Climate == EClimateProfile::Arctic && Lake.bLocked && Lake.Weather == EWeatherState::Snowfall);
		}
	}
	return true;
}

CLIMATE_TEST(FClimateSaveCarryTest, "SaveAndCarry")
bool FClimateSaveCarryTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// Save round trip of a temperate state.
	FEnvironmentState Wet = ClimateState(EClimateProfile::Temperate, EWeatherState::Blizzard, EWeatherState::Snowfall, 14.5f);
	Wet.Wetness = 0.83f;
	Wet.PuddleAmount = 0.41f;
	const TSharedRef<FJsonObject> Json = EnvironmentRules::StateToSaveJson(Wet);
	TestEqual(TEXT("saved climate"), Json->GetStringField(TEXT("climate")), FString(TEXT("temperate")));
	TestEqual(TEXT("saved weather keeps the ladder name"), Json->GetStringField(TEXT("weather")), FString(TEXT("Blizzard")));
	const FEnvironmentState Back = EnvironmentRules::StateFromSaveJson(*Json, FEnvironmentState());
	TestTrue(TEXT("round trip"), Back.Climate == EClimateProfile::Temperate && FMath::IsNearlyEqual(Back.Wetness, 0.83f)
		&& FMath::IsNearlyEqual(Back.PuddleAmount, 0.41f) && Back.Weather == EWeatherState::Blizzard);
	// An older save (no climate keys) keeps the fallback's (the level's) climate and a dry ground.
	FEnvironmentState Fallback;
	Fallback.Climate = EClimateProfile::Arctic;
	const FEnvironmentState Older = EnvironmentRules::StateFromSaveJson(*ClimateJson(TEXT(R"({"hour": 9, "weather": "Overcast"})")), Fallback);
	TestTrue(TEXT("older save: arctic, dry"), Older.Climate == EClimateProfile::Arctic && Older.Wetness == 0.f && Older.PuddleAmount == 0.f);
	const FEnvironmentState Bad = EnvironmentRules::StateFromSaveJson(*ClimateJson(TEXT(R"({"climate": "tropical", "wetness": 3, "puddles": -1})")), Fallback);
	TestTrue(TEXT("unknown climate keeps the fallback, values clamped"), Bad.Climate == EClimateProfile::Arctic && Bad.Wetness == 1.f && Bad.PuddleAmount == 0.f);

	// Carry arctic -> temperate: the weather starts over for the new profile, the hour continues, no snow, deterministic.
	FEnvironmentState Arctic = ClimateState(EClimateProfile::Arctic, EWeatherState::Blizzard, EWeatherState::Snowfall, 17.75f);
	Arctic.SnowAmount = 0.9f;
	Arctic.ObjectSnow.Top = 0.9f;
	Arctic.ObjectSnow.PX = 0.4f;
	FEnvironmentLevelSettings TemperateLevel;
	TemperateLevel.Climate = EClimateProfile::Temperate;
	const FEnvironmentState IntoTemperate = UEnvironmentSubsystem::ResolveInitialState(Config, TemperateLevel, TOptional<FEnvironmentState>(Arctic), 3);
	TestEqual(TEXT("arctic -> temperate: temperate"), IntoTemperate.Climate, EClimateProfile::Temperate);
	TestEqual(TEXT("arctic -> temperate: the time of day continues"), IntoTemperate.Hour, 17.75f, 1.e-4f);
	TestTrue(TEXT("arctic -> temperate: no snow carried"), IntoTemperate.SnowAmount == 0.f && IntoTemperate.ObjectSnow.IsZero());
	TestTrue(TEXT("arctic -> temperate: a fresh dwell"), IntoTemperate.WeatherElapsedSeconds == 0.f && IntoTemperate.WeatherDwellSeconds > 0.f
		&& EnvironmentRules::AreNeighbours(IntoTemperate.Weather, IntoTemperate.NextWeather));
	TestFalse(TEXT("arctic -> temperate: unlocked"), IntoTemperate.bLocked);
	const FEnvironmentState Again = UEnvironmentSubsystem::ResolveInitialState(Config, TemperateLevel, TOptional<FEnvironmentState>(Arctic), 99);
	TestTrue(TEXT("deterministic from the carried RNG"), Again.Weather == IntoTemperate.Weather && Again.RandomSeed == IntoTemperate.RandomSeed);
	// The level's own weather wins (a narrative level fixed at Rain): wet from the start.
	FEnvironmentLevelSettings RainLevel = TemperateLevel;
	RainLevel.bHasWeather = true;
	RainLevel.Weather = EWeatherState::Snowfall;
	const FEnvironmentState IntoRain = UEnvironmentSubsystem::ResolveInitialState(Config, RainLevel, TOptional<FEnvironmentState>(Arctic), 3);
	TestTrue(TEXT("arctic -> a Rain level: Rain, wet"), IntoRain.Weather == EWeatherState::Snowfall && IntoRain.Wetness == Config.Temperate.StartWetnessRain
		&& FMath::IsNearlyEqual(IntoRain.Hour, 17.75f));

	// Temperate -> temperate continues exactly (the wet ground too).
	FEnvironmentState Temperate = IntoRain;
	Temperate.Wetness = 0.66f;
	Temperate.PuddleAmount = 0.12f;
	Temperate.WeatherElapsedSeconds = 50.f;
	const FEnvironmentState Continued = UEnvironmentSubsystem::ResolveInitialState(Config, TemperateLevel, TOptional<FEnvironmentState>(Temperate), 3);
	TestTrue(TEXT("temperate -> temperate continues exactly"), SameState(Continued, Temperate));

	// Temperate -> arctic: the winter starts over with the arctic start cover; the hour continues; dry.
	const FEnvironmentState BackToArctic = UEnvironmentSubsystem::ResolveInitialState(Config, FEnvironmentLevelSettings(), TOptional<FEnvironmentState>(Temperate), 3);
	TestTrue(TEXT("temperate -> arctic: arctic, snow_cover_start, dry, same hour"), BackToArctic.Climate == EClimateProfile::Arctic
		&& BackToArctic.SnowAmount == Config.SnowCoverStart && BackToArctic.Wetness == 0.f && BackToArctic.PuddleAmount == 0.f
		&& FMath::IsNearlyEqual(BackToArctic.Hour, Temperate.Hour));

	// A locked temperate level ignores the carry like the tutorial does (its own hour / weather).
	FEnvironmentLevelSettings Locked = RainLevel;
	Locked.bLocked = true;
	Locked.bHasStartHour = true;
	Locked.StartHour = 10.f;
	const FEnvironmentState LockedState = UEnvironmentSubsystem::ResolveInitialState(Config, Locked, TOptional<FEnvironmentState>(Arctic), 3);
	TestTrue(TEXT("locked temperate Rain level"), LockedState.bLocked && LockedState.Climate == EClimateProfile::Temperate
		&& LockedState.Weather == EWeatherState::Snowfall && FMath::IsNearlyEqual(LockedState.Hour, 10.f) && LockedState.SnowAmount == 0.f);
	return true;
}

CLIMATE_TEST(FClimateTuningTest, "Tuning")
bool FClimateTuningTest::RunTest(const FString&)
{
	// The "temperate" block over the defaults; absent keys keep them; out-of-range values clamp.
	const TSharedPtr<FJsonObject> Json = ClimateJson(TEXT(R"({"temperate": {"cold_mult_rain": 0.5, "roster_enabled": true, "rain_intensity_storm": 3, "screen_drops": false}})"));
	const FEnvironmentConfig Config = EnvironmentTuning::FromJson(*Json);
	TestEqual(TEXT("cold_mult_rain"), Config.Temperate.ColdMultRain, 0.5f);
	TestTrue(TEXT("roster_enabled"), Config.Temperate.bRosterEnabled);
	TestEqual(TEXT("rain_intensity_storm clamped"), Config.Temperate.RainIntensityStorm, 1.f);
	TestFalse(TEXT("screen_drops"), Config.Temperate.bScreenDrops);
	TestEqual(TEXT("absent keeps the default"), Config.Temperate.ColdMultSunny, FTemperateClimateConfig().ColdMultSunny);
	TestEqual(TEXT("the arctic values are untouched"), Config.ColdMultBlizzard, FEnvironmentConfig().ColdMultBlizzard);
	const TSharedRef<FJsonObject> Out = EnvironmentTuning::ToJson(Config);
	const TSharedPtr<FJsonObject>* Written = nullptr;
	TestTrue(TEXT("ToJson writes the temperate block"), Out->TryGetObjectField(TEXT("temperate"), Written) && (*Written)->HasField(TEXT("wetness_rate_rain")));

	// The shipped environment.json carries the block with the design values.
	FString Text;
	if (TestTrue(TEXT("environment.json"), FFileHelper::LoadFileToString(Text, *EnvironmentTuning::GetDefaultPath())))
	{
		const TSharedPtr<FJsonObject> File = ClimateJson(Text);
		const TSharedPtr<FJsonObject>* Temperate = nullptr;
		if (TestTrue(TEXT("file has the temperate block"), File.IsValid() && File->TryGetObjectField(TEXT("temperate"), Temperate)))
		{
			const FEnvironmentConfig Shipped = EnvironmentTuning::FromJson(*File);
			const FTemperateClimateConfig Defaults;
			TestEqual(TEXT("shipped sunny cold"), Shipped.Temperate.ColdMultSunny, Defaults.ColdMultSunny, 1.e-5f);
			TestEqual(TEXT("shipped storm cold"), Shipped.Temperate.ColdMultStorm, Defaults.ColdMultStorm, 1.e-5f);
			TestEqual(TEXT("shipped rain wetting"), Shipped.Temperate.WetnessRateRain, Defaults.WetnessRateRain, 1.e-6f);
			TestEqual(TEXT("shipped sunny drying"), Shipped.Temperate.DryingRateSunny, Defaults.DryingRateSunny, 1.e-6f);
			TestEqual(TEXT("shipped puddle drain"), Shipped.Temperate.PuddleDrainRate, Defaults.PuddleDrainRate, 1.e-6f);
			TestFalse(TEXT("shipped: roster off"), Shipped.Temperate.bRosterEnabled);
			TestFalse(TEXT("shipped: no falling snow"), Shipped.Temperate.bFallingSnow);
			TestTrue(TEXT("shipped: screen drops on"), Shipped.Temperate.bScreenDrops);
			TestEqual(TEXT("shipped arctic blizzard cold unchanged"), Shipped.ColdMultBlizzard, 2.5f);
		}
	}

	// The temperate looks: SunnyDay is a bright warm day (~5800 K at noon), the rain dimmer and foggier, the storm the darkest.
	const FEnvironmentDayCurves Curves;
	const FWeatherVisualPreset Sunny = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Temperate, EWeatherState::Clear);
	const FWeatherVisualPreset Rain = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Temperate, EWeatherState::Snowfall);
	const FWeatherVisualPreset Storm = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Temperate, EWeatherState::Blizzard);
	const FWeatherVisualFrame Noon = EnvironmentVisualRules::ComputeFrame(Curves, Sunny, Sunny, EWeatherState::Clear, EWeatherState::Clear, 0.f, 12.f);
	TestTrue(*FString::Printf(TEXT("SunnyDay noon %.0f K in 5500..6000"), Noon.SunTemperature), Noon.SunTemperature >= 5500.f && Noon.SunTemperature <= 6000.f);
	TestTrue(TEXT("SunnyDay brighter than the arctic Clear"), Sunny.SunIntensityScale > EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Clear).SunIntensityScale);
	TestTrue(TEXT("SunnyDay saturated"), Sunny.Saturation > 1.f && Sunny.RayleighScatteringScale == 1.f);
	TestTrue(TEXT("sun > rain > storm"), Sunny.SunIntensityScale > Rain.SunIntensityScale && Rain.SunIntensityScale > Storm.SunIntensityScale);
	TestTrue(TEXT("fog: sun < rain < storm"), Sunny.FogDensity < Rain.FogDensity && Rain.FogDensity < Storm.FogDensity);
	TestTrue(TEXT("wind: storm strongest"), Storm.WindStrength > Rain.WindStrength && Rain.WindStrength > Sunny.WindStrength);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
