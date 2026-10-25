#include "Misc/AutomationTest.h"
#include "Data/EnvironmentTuning.h"
#include "Dom/JsonObject.h"
#include "Environment/CharacterSnowRules.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

// Directional snow with memory on objects and characters, melting in the sun (docs/systems/environment-weather.md section 15, user
// decisions 2026-10-23). FDirectionalSnow pockets top / +X / -X / +Y / -Y: the top grows at the snow cover rates, a side at rate x
// max(0, -dot(wind, side)) x wind strength, a pocket keeps its snow when the wind turns, every pocket melts in Clear while the sun is up
// (the objects' top never below object_snow_melt_floor x the ground cover, which itself never recedes). Characters: body-local pockets,
// the Custom Primitive Data push throttle. UE-only, no Godot reference.

#define DIRECTIONAL_SNOW_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.DirectionalSnow." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A state that holds Weather for the whole test (no transition). */
	FEnvironmentState PocketState(EWeatherState Weather, float Ground)
	{
		FEnvironmentState State;
		State.Hour = 12.f;
		State.Weather = Weather;
		State.NextWeather = Weather == EWeatherState::Clear ? EWeatherState::Overcast : EWeatherState::Snowfall;
		if (Weather == EWeatherState::Snowfall)
		{
			State.NextWeather = EWeatherState::Overcast;
		}
		State.WeatherDwellSeconds = 100000.f;
		State.WeatherElapsedSeconds = 0.f;
		State.SnowAmount = Ground;
		State.RandomSeed = 5;
		return State;
	}

	/** The wind coming FROM +X travels toward -X. */
	const FVector2D WindFromPX(-1.f, 0.f);
	const FVector2D WindFromNY(0.f, 1.f);
	constexpr float SunHigh = 40.f;
	constexpr float SunDown = -10.f;

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowFillByWindTest, "PocketFillByWind")
bool FDirectionalSnowFillByWindTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	TestEqual(TEXT("side factor: facing the wind 1"), EnvironmentRules::GetSideWindFactor(WindFromPX, 1), 1.f, 1.e-6f);
	TestEqual(TEXT("side factor: leeward 0"), EnvironmentRules::GetSideWindFactor(WindFromPX, 2), 0.f);
	TestEqual(TEXT("side factor: along the wind 0"), EnvironmentRules::GetSideWindFactor(WindFromPX, 3), 0.f, 1.e-6f);
	TestEqual(TEXT("side factor: the top never"), EnvironmentRules::GetSideWindFactor(WindFromPX, 0), 0.f);
	TestEqual(TEXT("side factor: diagonal cos 45"), EnvironmentRules::GetSideWindFactor(FVector2D(-1.f, -1.f), 3), UE_INV_SQRT_2, 1.e-5f);
	TestEqual(TEXT("side factor: a non-unit wind is normalised"), EnvironmentRules::GetSideWindFactor(FVector2D(-5.f, 0.f), 1), 1.f, 1.e-6f);

	// A blizzard, full wind from +X for 60 s: the top and +X grow at the Blizzard rate, nothing else.
	FEnvironmentState State = PocketState(EWeatherState::Blizzard, 0.3f);
	const float Rate = EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Blizzard);
	EnvironmentRules::StepObjectSnow(Config, State, 60.f, WindFromPX, 1.f, SunHigh);
	TestEqual(TEXT("top grows at the cover rate"), State.ObjectSnow.Top, 60.f * Rate, 1.e-5f);
	TestEqual(TEXT("+X (windward) grows at the cover rate x wind 1"), State.ObjectSnow.PX, 60.f * Rate, 1.e-5f);
	TestEqual(TEXT("-X (leeward) stays bare"), State.ObjectSnow.NX, 0.f);
	TestEqual(TEXT("+Y stays bare"), State.ObjectSnow.PY, 0.f, 1.e-6f);
	TestEqual(TEXT("-Y stays bare"), State.ObjectSnow.NY, 0.f, 1.e-6f);

	// Half the wind: half the side growth; the side scale multiplies; no wind: the sides never grow.
	FEnvironmentState Half = PocketState(EWeatherState::Blizzard, 0.3f);
	EnvironmentRules::StepObjectSnow(Config, Half, 60.f, WindFromPX, 0.5f, SunHigh);
	TestEqual(TEXT("wind 0.5: half the side growth"), Half.ObjectSnow.PX, 30.f * Rate, 1.e-5f);
	FEnvironmentConfig Scaled;
	Scaled.ObjectSnowSideRateScale = 2.f;
	FEnvironmentState Double = PocketState(EWeatherState::Blizzard, 0.3f);
	EnvironmentRules::StepObjectSnow(Scaled, Double, 60.f, WindFromPX, 0.5f, SunHigh);
	TestEqual(TEXT("side scale 2"), Double.ObjectSnow.PX, 60.f * Rate, 1.e-5f);
	FEnvironmentState Calm = PocketState(EWeatherState::Snowfall, 0.3f);
	EnvironmentRules::StepObjectSnow(Config, Calm, 60.f, WindFromPX, 0.f, SunHigh);
	TestTrue(TEXT("no wind: only the top"), Calm.ObjectSnow.Top > 0.f && Calm.ObjectSnow.PX == 0.f);
	TestEqual(TEXT("Snowfall rate on the top"), Calm.ObjectSnow.Top, 60.f * EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Snowfall), 1.e-5f);

	// A diagonal wind fills two sides; long enough, everything caps at 1.
	FEnvironmentState Diagonal = PocketState(EWeatherState::Blizzard, 0.3f);
	EnvironmentRules::StepObjectSnow(Config, Diagonal, 60.f, FVector2D(-1.f, 1.f), 1.f, SunHigh);
	TestEqual(TEXT("diagonal: +X cos 45"), Diagonal.ObjectSnow.PX, 60.f * Rate * UE_INV_SQRT_2, 1.e-5f);
	TestEqual(TEXT("diagonal: -Y cos 45"), Diagonal.ObjectSnow.NY, 60.f * Rate * UE_INV_SQRT_2, 1.e-5f);
	EnvironmentRules::StepObjectSnow(Config, Diagonal, 100000.f, FVector2D(-1.f, 1.f), 1.f, SunHigh);
	TestEqual(TEXT("capped at 1"), Diagonal.ObjectSnow.GetMax(), 1.f);

	// Clear / Overcast: no growth at all; a locked state never changes.
	FEnvironmentState Overcast = PocketState(EWeatherState::Overcast, 0.3f);
	EnvironmentRules::StepObjectSnow(Config, Overcast, 60.f, WindFromPX, 1.f, SunHigh);
	TestTrue(TEXT("Overcast: nothing"), Overcast.ObjectSnow.IsZero());
	FEnvironmentState Locked = PocketState(EWeatherState::Blizzard, 0.3f);
	Locked.bLocked = true;
	EnvironmentRules::StepObjectSnow(Config, Locked, 60.f, WindFromPX, 1.f, SunHigh);
	TestTrue(TEXT("locked: nothing"), Locked.ObjectSnow.IsZero());
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowMemoryTest, "MemoryWhenWindTurns")
bool FDirectionalSnowMemoryTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = PocketState(EWeatherState::Blizzard, 0.3f);
	EnvironmentRules::StepObjectSnow(Config, State, 90.f, WindFromPX, 1.f, SunHigh);
	const float PX = State.ObjectSnow.PX;
	TestTrue(TEXT("+X filled first"), PX > 0.4f && State.ObjectSnow.NY == 0.f);
	// The wind turns: from -Y now. +X keeps its snow, -Y fills, the top keeps growing.
	EnvironmentRules::StepObjectSnow(Config, State, 60.f, WindFromNY, 1.f, SunHigh);
	TestEqual(TEXT("+X kept after the turn"), State.ObjectSnow.PX, PX, 1.e-6f);
	TestEqual(TEXT("-Y filled by the new wind"), State.ObjectSnow.NY, 60.f * EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Blizzard), 1.e-5f);
	TestEqual(TEXT("-X / +Y still bare"), State.ObjectSnow.NX + State.ObjectSnow.PY, 0.f, 1.e-6f);
	TestEqual(TEXT("top kept growing"), State.ObjectSnow.Top, FMath::Min(1.f, 150.f * EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Blizzard)), 1.e-5f);
	// The ground cover step is separate and unchanged by the pockets.
	EnvironmentRules::StepSnowCover(Config, State, 10.f);
	TestTrue(TEXT("ground cover still grows"), State.SnowAmount > 0.3f);
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowMeltFloorTest, "MeltFloor")
bool FDirectionalSnowMeltFloorTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = PocketState(EWeatherState::Clear, 0.6f);
	State.ObjectSnow.Top = 0.9f;
	State.ObjectSnow.PX = 0.5f;
	State.ObjectSnow.NY = 0.02f;
	const float Melt = EnvironmentRules::GetObjectSnowMeltRate(Config, State, SunHigh);
	TestEqual(TEXT("melt rate: the full rate in Clear with the sun high"), Melt, Config.ObjectSnowMeltRate, 1.e-7f);
	EnvironmentRules::StepObjectSnow(Config, State, 60.f, WindFromPX, 1.f, SunHigh);
	TestEqual(TEXT("top melts a little"), State.ObjectSnow.Top, 0.9f - 60.f * Melt, 1.e-5f);
	TestEqual(TEXT("+X melts a little"), State.ObjectSnow.PX, 0.5f - 60.f * Melt, 1.e-5f);
	TestEqual(TEXT("-Y melted away (floor 0 for the sides)"), State.ObjectSnow.NY, 0.f);
	// Long midday: the top stops at the floor (0.5 x the ground), the sides at 0; the ground never recedes.
	EnvironmentRules::StepSnowCover(Config, State, 3600.f);
	EnvironmentRules::StepObjectSnow(Config, State, 3600.f, WindFromPX, 1.f, SunHigh);
	TestEqual(TEXT("ground cover unchanged in Clear"), State.SnowAmount, 0.6f);
	TestEqual(TEXT("top at the floor 0.5 x 0.6"), State.ObjectSnow.Top, 0.3f, 1.e-6f);
	TestEqual(TEXT("sides at 0"), State.ObjectSnow.PX, 0.f);
	// A pocket already under its floor (it never grew that far) is not raised by the melt.
	FEnvironmentState Thin = PocketState(EWeatherState::Clear, 0.8f);
	Thin.ObjectSnow.Top = 0.1f;
	EnvironmentRules::StepObjectSnow(Config, Thin, 600.f, WindFromPX, 1.f, SunHigh);
	TestEqual(TEXT("below the floor: kept"), Thin.ObjectSnow.Top, 0.1f);
	// The floor share is tunable; 0 lets the top melt bare.
	FEnvironmentConfig NoFloor;
	NoFloor.ObjectSnowMeltFloor = 0.f;
	FEnvironmentState Bare = PocketState(EWeatherState::Clear, 0.8f);
	Bare.ObjectSnow.Top = 0.5f;
	EnvironmentRules::StepObjectSnow(NoFloor, Bare, 100000.f, WindFromPX, 1.f, SunHigh);
	TestEqual(TEXT("floor 0: the top melts bare"), Bare.ObjectSnow.Top, 0.f);
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowNoMeltTest, "NoMeltAtNightOrSnowing")
bool FDirectionalSnowNoMeltTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FDirectionalSnow Start;
	Start.Top = 0.8f;
	Start.PX = 0.6f;
	// Night (sun below the horizon), Clear: nothing melts.
	FEnvironmentState Night = PocketState(EWeatherState::Clear, 0.8f);
	Night.ObjectSnow = Start;
	TestEqual(TEXT("night: melt rate 0"), EnvironmentRules::GetObjectSnowMeltRate(Config, Night, SunDown), 0.f);
	EnvironmentRules::StepObjectSnow(Config, Night, 600.f, WindFromPX, 1.f, SunDown);
	TestEqual(TEXT("night: top kept"), Night.ObjectSnow.Top, 0.8f);
	TestEqual(TEXT("night: side kept"), Night.ObjectSnow.PX, 0.6f);
	// Overcast / snowing: no melt (snowing only grows).
	for (const EWeatherState Weather : { EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard })
	{
		FEnvironmentState State = PocketState(Weather, 0.8f);
		State.ObjectSnow = Start;
		TestEqual(FString::Printf(TEXT("%s: melt rate 0"), *EnvironmentRules::WeatherToString(Weather)), EnvironmentRules::GetObjectSnowMeltRate(Config, State, SunHigh), 0.f);
		EnvironmentRules::StepObjectSnow(Config, State, 60.f, WindFromNY, 1.f, SunHigh);
		TestTrue(FString::Printf(TEXT("%s: nothing lost"), *EnvironmentRules::WeatherToString(Weather)), State.ObjectSnow.Top >= 0.8f && State.ObjectSnow.PX >= 0.6f);
	}
	// The sun ramp: 0 at the horizon, half way at half the full elevation, full above it.
	TestEqual(TEXT("sun ramp: horizon 0"), EnvironmentRules::GetSunMeltFactor(Config, 0.f), 0.f);
	TestEqual(TEXT("sun ramp: half"), EnvironmentRules::GetSunMeltFactor(Config, Config.ObjectSnowMeltFullSunElevation * 0.5f), 0.5f, 1.e-6f);
	TestEqual(TEXT("sun ramp: full"), EnvironmentRules::GetSunMeltFactor(Config, 80.f), 1.f);
	// Clear -> Overcast blend half way: half the melt.
	FEnvironmentState Blend = PocketState(EWeatherState::Clear, 0.8f);
	Blend.WeatherDwellSeconds = 225.f;
	Blend.WeatherElapsedSeconds = 225.f - Config.WeatherTransitionSeconds * 0.5f;
	TestEqual(TEXT("half way to Overcast: half the melt"), EnvironmentRules::GetObjectSnowMeltRate(Config, Blend, SunHigh), Config.ObjectSnowMeltRate * 0.5f, 1.e-7f);
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowLocalFrameTest, "LocalFrame")
bool FDirectionalSnowLocalFrameTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const FVector WorldWind(-1.0, 0.0, 0.0); // from +X
	// An unrotated body: local = world.
	const FVector2D Same = CharacterSnowRules::ToLocalWind(WorldWind, FQuat::Identity);
	TestTrue(TEXT("identity: local wind = world wind"), Same.Equals(FVector2D(-1.f, 0.f), 1.e-5f));
	// A body turned 90 degrees (local +X = world +Y): the world +X face is the body's local -Y.
	const FQuat Turned = FRotator(0.f, 90.f, 0.f).Quaternion();
	const FVector2D Local = CharacterSnowRules::ToLocalWind(WorldWind, Turned);
	TestTrue(TEXT("yaw 90: the wind arrives at local -Y"), FMath::IsNearlyEqual(EnvironmentRules::GetSideWindFactor(Local, 4), 1.f, 1.e-5f)
		&& FMath::IsNearlyZero(EnvironmentRules::GetSideWindFactor(Local, 1), 1.e-5f));
	// A typical UE mannequin body: the mesh is turned -90 inside the actor; the actor faces the wind (yaw 0, the wind coming at its front
	// = world +X): the snow lands on the mesh's local +Y (the mesh's own forward).
	const FQuat Mannequin = FRotator(0.f, -90.f, 0.f).Quaternion();
	TestTrue(TEXT("mesh yaw -90: the wind arrives at local +Y"), FMath::IsNearlyEqual(EnvironmentRules::GetSideWindFactor(CharacterSnowRules::ToLocalWind(WorldWind, Mannequin), 3), 1.f, 1.e-5f));
	// A tilt is ignored (the wind is horizontal in the body frame); zero wind stays zero.
	TestTrue(TEXT("zero wind"), CharacterSnowRules::ToLocalWind(FVector::ZeroVector, Turned).IsZero());

	// Memory on the body: snow on the local back, the character turns 180 degrees, the snow stays in its local pocket and the wind now
	// fills the other side.
	FDirectionalSnow Body;
	CharacterSnowRules::FCharacterSnowConditions Outdoors;
	Outdoors.SnowCoverRate = EnvironmentRules::GetSnowCoverRate(Config, EWeatherState::Blizzard);
	Outdoors.WindStrength = 1.f;
	Outdoors.LocalWind = CharacterSnowRules::ToLocalWind(WorldWind, FQuat::Identity); // body faces -X... its +X side takes the wind
	EnvironmentRules::StepDirectionalSnow(Body, CharacterSnowRules::MakeInputs(Config, Outdoors), 60.f);
	const float Back = Body.PX;
	TestTrue(TEXT("local +X filled"), Back > 0.2f && Body.NX == 0.f);
	Outdoors.LocalWind = CharacterSnowRules::ToLocalWind(WorldWind, FRotator(0.f, 180.f, 0.f).Quaternion());
	EnvironmentRules::StepDirectionalSnow(Body, CharacterSnowRules::MakeInputs(Config, Outdoors), 30.f);
	TestEqual(TEXT("after the turn the local +X keeps its snow"), Body.PX, Back, 1.e-6f);
	TestTrue(TEXT("after the turn the local -X fills"), Body.NX > 0.1f);

	// Sheltered (heat / indoors): no growth, a fast clear down to exactly 0; in the sun the character melts with floor 0.
	CharacterSnowRules::FCharacterSnowConditions Warm = Outdoors;
	Warm.bSheltered = true;
	const FDirectionalSnowInputs WarmInputs = CharacterSnowRules::MakeInputs(Config, Warm);
	TestTrue(TEXT("sheltered: no growth, the clear rate"), WarmInputs.GrowRate == 0.f && WarmInputs.ClearRate == Config.CharacterSnowClearRate);
	FDirectionalSnow Warming = Body;
	EnvironmentRules::StepDirectionalSnow(Warming, WarmInputs, 1.f);
	TestEqual(TEXT("sheltered: -clear rate x 1 s"), Warming.PX, FMath::Max(0.f, Back - Config.CharacterSnowClearRate), 1.e-5f);
	EnvironmentRules::StepDirectionalSnow(Warming, WarmInputs, 60.f);
	TestTrue(TEXT("sheltered: exactly bare after a minute"), Warming.IsZero());
	CharacterSnowRules::FCharacterSnowConditions Sun;
	Sun.ObjectMeltRate = Config.ObjectSnowMeltRate;
	FDirectionalSnow Melting = Body;
	EnvironmentRules::StepDirectionalSnow(Melting, CharacterSnowRules::MakeInputs(Config, Sun), 100000.f);
	TestTrue(TEXT("sun: a character melts bare (floor 0)"), Melting.IsZero());

	// Rotating world pockets into a body frame (spawn cover): exact for quarter turns, the top kept.
	FDirectionalSnow World;
	World.Top = 0.7f;
	World.PX = 0.4f;
	World.NY = 0.2f;
	const FDirectionalSnow Quarter = CharacterSnowRules::RotateToLocal(World, Turned);
	TestEqual(TEXT("rotate: top kept"), Quarter.Top, 0.7f);
	TestEqual(TEXT("rotate 90: world +X -> local -Y"), Quarter.NY, 0.4f, 1.e-5f);
	TestEqual(TEXT("rotate 90: world -Y -> local -X"), Quarter.NX, 0.2f, 1.e-5f);
	TestEqual(TEXT("rotate 90: nothing on local +X / +Y"), Quarter.PX + Quarter.PY, 0.f, 1.e-5f);
	const FDirectionalSnow Diagonal = CharacterSnowRules::RotateToLocal(World, FRotator(0.f, 45.f, 0.f).Quaternion());
	TestEqual(TEXT("rotate 45: half of world +X on local +X"), Diagonal.PX, 0.2f, 1.e-4f);

	// Spawn cover: half the objects' pockets while it snows, nothing in Clear.
	const FDirectionalSnow Storm = CharacterSnowRules::MakeSpawnCover(Config, World, 1.f, FQuat::Identity);
	TestEqual(TEXT("spawn in a storm: top x 0.5"), Storm.Top, 0.35f, 1.e-5f);
	TestEqual(TEXT("spawn in a storm: side x 0.5"), Storm.PX, 0.2f, 1.e-5f);
	TestTrue(TEXT("spawn in Clear: bare"), CharacterSnowRules::MakeSpawnCover(Config, World, 0.f, FQuat::Identity).IsZero());
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowPushThrottleTest, "PushThrottle")
bool FDirectionalSnowPushThrottleTest::RunTest(const FString&)
{
	FDirectionalSnow Pushed;
	Pushed.Top = 0.5f;
	FDirectionalSnow Small = Pushed;
	Small.Top = 0.505f;
	FDirectionalSnow Big = Pushed;
	Big.PX = 0.02f;
	TestTrue(TEXT("CPD indices 8..12"), CharacterSnowRules::FirstCustomDataIndex == 8 && CharacterSnowRules::FirstCustomDataIndex + FDirectionalSnow::PocketCount - 1 == 12);
	TestFalse(TEXT("too soon (< 0.25 s) even for a big change"), CharacterSnowRules::ShouldPush(Pushed, Big, 0.2f));
	TestTrue(TEXT("a change > 0.01 after the interval"), CharacterSnowRules::ShouldPush(Pushed, Big, 0.25f));
	TestFalse(TEXT("a change <= 0.01 is not pushed"), CharacterSnowRules::ShouldPush(Pushed, Small, 5.f));
	TestFalse(TEXT("no change, no push"), CharacterSnowRules::ShouldPush(Pushed, Pushed, 5.f));
	// The last bit down to exactly 0 is always pushed (an empty body renders exactly like before the layer).
	FDirectionalSnow Tiny;
	Tiny.PY = 0.004f;
	TestTrue(TEXT("down to exactly 0: pushed"), CharacterSnowRules::ShouldPush(Tiny, FDirectionalSnow(), 0.3f));
	TestFalse(TEXT("down to exactly 0 but too soon"), CharacterSnowRules::ShouldPush(Tiny, FDirectionalSnow(), 0.1f));
	TestEqual(TEXT("max difference"), CharacterSnowRules::MaxDifference(Pushed, Big), 0.02f, 1.e-6f);
	// At the design rates a blizzard changes a pocket by ~0.0014 per 0.25 s: a push every ~2 s at most per character.
	const float PerStep = EnvironmentRules::GetSnowCoverRate(FEnvironmentConfig(), EWeatherState::Blizzard) * CharacterSnowRules::PushIntervalSeconds;
	TestTrue(TEXT("a blizzard step is below the threshold"), PerStep < CharacterSnowRules::PushThreshold);
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowSaveTest, "SaveAndBackCompat")
bool FDirectionalSnowSaveTest::RunTest(const FString&)
{
	FEnvironmentState Saved = PocketState(EWeatherState::Snowfall, 0.55f);
	Saved.ObjectSnow.Top = 0.61f;
	Saved.ObjectSnow.PX = 0.25f;
	Saved.ObjectSnow.NX = 0.05f;
	Saved.ObjectSnow.PY = 0.125f;
	Saved.ObjectSnow.NY = 0.75f;
	FString Text;
	FJsonSerializer::Serialize(EnvironmentRules::StateToSaveJson(Saved), TJsonWriterFactory<>::Create(&Text));
	const TSharedPtr<FJsonObject> Parsed = ParseJson(Text);
	if (!TestTrue(TEXT("save json parses"), Parsed.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("object_snow written"), Parsed->HasTypedField<EJson::Object>(TEXT("object_snow")));
	const FEnvironmentState Loaded = EnvironmentRules::StateFromSaveJson(*Parsed, FEnvironmentState());
	for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("pocket %d round trip"), Index), Loaded.ObjectSnow.Get(Index), Saved.ObjectSnow.Get(Index), 1.e-5f);
	}
	// An old save (no object_snow): the tops take the ground cover once, the sides are bare.
	FEnvironmentState Fallback = PocketState(EWeatherState::Clear, 0.2f);
	Fallback.ObjectSnow.PX = 0.9f;
	const FEnvironmentState Old = EnvironmentRules::StateFromSaveJson(*ParseJson(TEXT(R"({"snow_amount": 0.47})")), Fallback);
	TestEqual(TEXT("old save: top = snow_amount"), Old.ObjectSnow.Top, 0.47f, 1.e-6f);
	TestEqual(TEXT("old save: sides bare"), Old.ObjectSnow.PX + Old.ObjectSnow.NX + Old.ObjectSnow.PY + Old.ObjectSnow.NY, 0.f);
	// Partial / odd pockets: each key optional, clamped.
	const FEnvironmentState Partial = EnvironmentRules::StateFromSaveJson(*ParseJson(TEXT(R"({"object_snow": {"px": 3, "ny": -1, "top": "x"}})")), Fallback);
	TestEqual(TEXT("partial: px clamped to 1"), Partial.ObjectSnow.PX, 1.f);
	TestEqual(TEXT("partial: ny clamped to 0"), Partial.ObjectSnow.NY, 0.f);
	TestEqual(TEXT("partial: a non-number top keeps the fallback"), Partial.ObjectSnow.Top, Fallback.ObjectSnow.Top);
	// Seeding happens only when every pocket is 0 and the ground has snow.
	FEnvironmentState Seed = PocketState(EWeatherState::Clear, 0.35f);
	TestTrue(TEXT("seed: empty pockets take the ground"), EnvironmentRules::SeedObjectSnow(Seed) && Seed.ObjectSnow.Top == 0.35f);
	TestFalse(TEXT("seed: only once"), EnvironmentRules::SeedObjectSnow(Seed));
	FEnvironmentState Bare = PocketState(EWeatherState::Clear, 0.f);
	TestFalse(TEXT("seed: bare ground, nothing"), EnvironmentRules::SeedObjectSnow(Bare));
	// A fresh state starts with the cover on the tops.
	const FEnvironmentState Fresh = EnvironmentRules::MakeInitialState(FEnvironmentConfig(), 9.f, EWeatherState::Clear, 3);
	TestEqual(TEXT("fresh: top = the start cover"), Fresh.ObjectSnow.Top, Fresh.SnowAmount);
	FEnvironmentLevelSettings Lake;
	Lake.bLocked = true;
	Lake.bHasSnowCover = true;
	Lake.SnowCover = 0.6f;
	const FEnvironmentState Resolved = UEnvironmentSubsystem::ResolveInitialState(FEnvironmentConfig(), Lake, TOptional<FEnvironmentState>(), 7);
	TestEqual(TEXT("level snow_cover: top = 0.6"), Resolved.ObjectSnow.Top, 0.6f, 1.e-6f);
	return true;
}

DIRECTIONAL_SNOW_TEST(FDirectionalSnowTuningTest, "Tuning")
bool FDirectionalSnowTuningTest::RunTest(const FString&)
{
	FEnvironmentConfig Changed;
	Changed.ObjectSnowSideRateScale = 0.7f;
	Changed.ObjectSnowMeltRate = 0.004f;
	Changed.ObjectSnowMeltFloor = 0.25f;
	Changed.ObjectSnowMeltFullSunElevation = 30.f;
	Changed.CharacterSnowFillScale = 1.5f;
	Changed.CharacterSnowMeltScale = 2.f;
	Changed.CharacterSnowClearRate = 0.1f;
	Changed.CharacterSnowSpawnShare = 0.3f;
	const FEnvironmentConfig Back = EnvironmentTuning::FromJson(*EnvironmentTuning::ToJson(Changed));
	TestEqual(TEXT("side scale"), Back.ObjectSnowSideRateScale, 0.7f, 1.e-6f);
	TestEqual(TEXT("melt rate"), Back.ObjectSnowMeltRate, 0.004f, 1.e-6f);
	TestEqual(TEXT("melt floor"), Back.ObjectSnowMeltFloor, 0.25f, 1.e-6f);
	TestEqual(TEXT("full sun elevation"), Back.ObjectSnowMeltFullSunElevation, 30.f, 1.e-6f);
	TestEqual(TEXT("character fill"), Back.CharacterSnowFillScale, 1.5f, 1.e-6f);
	TestEqual(TEXT("character melt"), Back.CharacterSnowMeltScale, 2.f, 1.e-6f);
	TestEqual(TEXT("character clear"), Back.CharacterSnowClearRate, 0.1f, 1.e-6f);
	TestEqual(TEXT("spawn share"), Back.CharacterSnowSpawnShare, 0.3f, 1.e-6f);
	const FEnvironmentConfig Clamped = EnvironmentTuning::FromJson(*ParseJson(TEXT(R"({"object_snow_melt_floor": 3, "object_snow_melt_rate": -1, "character_snow_spawn_share": 2})")));
	TestEqual(TEXT("floor clamped to 1"), Clamped.ObjectSnowMeltFloor, 1.f);
	TestEqual(TEXT("negative melt clamped to 0"), Clamped.ObjectSnowMeltRate, 0.f);
	TestEqual(TEXT("spawn share clamped to 1"), Clamped.CharacterSnowSpawnShare, 1.f);

	// The shipped environment.json carries the design values.
	FString Json;
	if (TestTrue(TEXT("environment.json readable"), FFileHelper::LoadFileToString(Json, *EnvironmentTuning::GetDefaultPath())))
	{
		const TSharedPtr<FJsonObject> File = ParseJson(Json);
		if (TestTrue(TEXT("environment.json parses"), File.IsValid()))
		{
			const FEnvironmentConfig Defaults;
			const FEnvironmentConfig Shipped = EnvironmentTuning::FromJson(*File);
			for (const TCHAR* Key : { TEXT("object_snow_side_rate_scale"), TEXT("object_snow_melt_rate"), TEXT("object_snow_melt_floor"),
				TEXT("object_snow_melt_full_sun_elevation"), TEXT("character_snow_fill_scale"), TEXT("character_snow_melt_scale"),
				TEXT("character_snow_clear_rate"), TEXT("character_snow_spawn_share") })
			{
				TestTrue(FString::Printf(TEXT("shipped key %s"), Key), File->HasField(Key));
			}
			TestEqual(TEXT("shipped: melt ~10 min"), 1.f / Shipped.ObjectSnowMeltRate, 600.f, 1.f);
			TestEqual(TEXT("shipped: floor 0.5"), Shipped.ObjectSnowMeltFloor, Defaults.ObjectSnowMeltFloor, 1.e-6f);
			TestEqual(TEXT("shipped: clear ~20 s"), Shipped.CharacterSnowClearRate, Defaults.CharacterSnowClearRate, 1.e-6f);
			TestEqual(TEXT("shipped: spawn share 0.5"), Shipped.CharacterSnowSpawnShare, 0.5f, 1.e-6f);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
