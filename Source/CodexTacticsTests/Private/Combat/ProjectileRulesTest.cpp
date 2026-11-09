// CodexTactics.Weapons.Projectile.* - the shared projectile layer (2026-11-09, docs/design/WEAPONS_FRAMEWORK.md 16): the launch that reaches an aim
// point (flat / the low arc / the high plunging arc), the fixed-step flight (the same path whatever the frame rate), arming, the high-angle
// fire band, and the schema's projectile block (trajectory, max_flight_s, the legacy high_angle key).

#include "Combat/ProjectileRules.h"
#include "Data/WeaponStats.h"
#include "Data/WeaponStatsRules.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ProjectileRulesTest
{
	constexpr float Gravity = -980.f;

	/** Flies a launch to the target's horizontal distance (or 15 s); returns the closest the path came to the target, cm. */
	float ClosestApproach(const FVector& From, const FVector& Velocity, float GravityZ, const FVector& Target, float* OutFlightSeconds = nullptr)
	{
		FProjectileFlightState State;
		State.Position = From;
		State.Velocity = Velocity;
		State.GravityZ = GravityZ;
		float Best = TNumericLimits<float>::Max();
		float BestTime = 0.f;
		for (int32 Step = 0; Step < 15 * 120; ++Step)
		{
			const FVector Previous = State.Position;
			ProjectileRules::Step(State);
			const float Distance = FMath::PointDistToSegment(Target, Previous, State.Position);
			if (Distance < Best)
			{
				Best = Distance;
				BestTime = State.FlightSeconds();
			}
			if (State.Position.Z < Target.Z - 2000.f)
			{
				break;
			}
		}
		if (OutFlightSeconds)
		{
			*OutFlightSeconds = BestTime;
		}
		return Best;
	}

	TSharedPtr<FJsonObject> ParseProjectileJson(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	float ElevationDeg(const FVector& Velocity)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Velocity.Z, FVector(Velocity.X, Velocity.Y, 0.f).Size()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileLaunchTest, "CodexTactics.Weapons.Projectile.Launch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectileLaunchTest::RunTest(const FString&)
{
	using namespace ProjectileRulesTest;
	const FVector From(0.f, 0.f, 150.f);

	// Flat (a rocket): straight at the aim point at the launch speed, whatever the gravity.
	FVector Velocity;
	const FVector RocketTarget(2500.f, 400.f, 100.f);
	TestTrue(TEXT("flat: solved"), ProjectileRules::SolveLaunch(From, RocketTarget, 4000.f, Gravity, EProjectileTrajectory::Flat, Velocity));
	TestEqual(TEXT("flat: the launch speed"), static_cast<float>(Velocity.Size()), 4000.f, 0.01f);
	TestTrue(TEXT("flat: straight at the target"), Velocity.GetSafeNormal().Equals((RocketTarget - From).GetSafeNormal(), 1e-5f));
	TestTrue(TEXT("flat: reaches the target (no gravity)"), ClosestApproach(From, Velocity, 0.f, RocketTarget) < 1.f);

	// Ballistic and high-angle to the same point 40 m away (a mortar at 25 m/s).
	const FVector Target(4000.f, 0.f, 0.f);
	FVector Low;
	FVector High;
	TestTrue(TEXT("ballistic: solved"), ProjectileRules::SolveLaunch(From, Target, 2500.f, Gravity, EProjectileTrajectory::Ballistic, Low));
	TestTrue(TEXT("high-angle: solved"), ProjectileRules::SolveLaunch(From, Target, 2500.f, Gravity, EProjectileTrajectory::HighAngle, High));
	TestEqual(TEXT("ballistic: the launch speed"), static_cast<float>(Low.Size()), 2500.f, 0.05f);
	TestEqual(TEXT("high-angle: the launch speed"), static_cast<float>(High.Size()), 2500.f, 0.05f);
	TestTrue(FString::Printf(TEXT("ballistic: the low arc (%.1f deg < 45)"), ElevationDeg(Low)), ElevationDeg(Low) < 45.f);
	TestTrue(FString::Printf(TEXT("high-angle: the plunging arc (%.1f deg > 45)"), ElevationDeg(High)), ElevationDeg(High) > 45.f);
	float LowTime = 0.f;
	float HighTime = 0.f;
	const float LowMiss = ClosestApproach(From, Low, Gravity, Target, &LowTime);
	const float HighMiss = ClosestApproach(From, High, Gravity, Target, &HighTime);
	TestTrue(FString::Printf(TEXT("ballistic: the fixed-step path reaches the target (%.2f cm)"), LowMiss), LowMiss < 2.f);
	TestTrue(FString::Printf(TEXT("high-angle: the fixed-step path reaches the target (%.2f cm)"), HighMiss), HighMiss < 2.f);
	TestTrue(FString::Printf(TEXT("high-angle flies longer (%.2f s > %.2f s)"), HighTime, LowTime), HighTime > LowTime);
	TestEqual(TEXT("the flight time = the horizontal distance / the horizontal speed"), ProjectileRules::FlightTimeTo(High, 4000.f), HighTime, 0.02f);

	// Uphill and out of reach.
	const FVector Uphill(3000.f, 1000.f, 900.f);
	TestTrue(TEXT("uphill high-angle: solved"), ProjectileRules::SolveLaunch(From, Uphill, 2500.f, Gravity, EProjectileTrajectory::HighAngle, High));
	TestTrue(TEXT("uphill high-angle: reaches it"), ClosestApproach(From, High, Gravity, Uphill) < 2.f);
	TestFalse(TEXT("out of reach: not solved"), ProjectileRules::SolveLaunch(From, FVector(80000.f, 0.f, 0.f), 2500.f, Gravity,
		EProjectileTrajectory::Ballistic, Velocity));
	TestEqual(TEXT("out of reach: the 45-degree shot towards it"), ElevationDeg(Velocity), 45.f, 0.01f);
	TestFalse(TEXT("no speed: not solved"), ProjectileRules::SolveLaunch(From, Target, 0.f, Gravity, EProjectileTrajectory::Flat, Velocity));
	TestTrue(TEXT("a ballistic round with no gravity flies flat"),
		ProjectileRules::SolveLaunch(From, Target, 2500.f, 0.f, EProjectileTrajectory::Ballistic, Velocity) && FMath::Abs(ElevationDeg(Velocity)
			- ElevationDeg(Target - From)) < 0.01f);

	// The weapon block's gravity / flight time.
	FWeaponProjectileStats Stats;
	Stats.GravityScale = 1.f;
	TestEqual(TEXT("flat trajectory: no gravity"), ProjectileRules::GravityFor(Stats, Gravity), 0.f);
	Stats.Trajectory = EProjectileTrajectory::HighAngle;
	Stats.GravityScale = 0.5f;
	TestEqual(TEXT("high-angle: world gravity x scale"), ProjectileRules::GravityFor(Stats, Gravity), -490.f);
	TestEqual(TEXT("max_flight_s 0 = the default"), ProjectileRules::MaxFlightSeconds(Stats), ProjectileRules::DefaultMaxFlightSeconds);
	Stats.MaxFlightSeconds = 3.f;
	TestEqual(TEXT("max_flight_s as given"), ProjectileRules::MaxFlightSeconds(Stats), 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileFixedStepTest, "CodexTactics.Weapons.Projectile.FixedStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectileFixedStepTest::RunTest(const FString&)
{
	// The same launch flown at 30 fps, 60 fps, 120 fps and random frame times: every step lands on the same point.
	auto Fly = [](TFunctionRef<float(int32)> FrameTime, int32 StepsWanted)
	{
		FProjectileFlightState State;
		State.Position = FVector(0.f, 0.f, 150.f);
		State.Velocity = FVector(1800.f, 300.f, 1700.f);
		State.GravityZ = -980.f;
		TArray<FVector> Points;
		for (int32 Frame = 0; Points.Num() < StepsWanted && Frame < 100000; ++Frame)
		{
			ProjectileRules::Advance(State, FrameTime(Frame), [&Points](const FVector&, const FVector& To, int32 /*StepIndex*/)
			{
				Points.Add(To);
				return false;
			});
		}
		return Points;
	};
	const int32 Steps = 360; // 3 s
	const TArray<FVector> At30 = Fly([](int32) { return 1.f / 30.f; }, Steps);
	const TArray<FVector> At60 = Fly([](int32) { return 1.f / 60.f; }, Steps);
	const TArray<FVector> At120 = Fly([](int32) { return 1.f / 120.f; }, Steps);
	FRandomStream Random(1109);
	const TArray<FVector> AtRandom = Fly([&Random](int32) { return Random.FRandRange(0.002f, 0.05f); }, Steps);
	int32 Same = 0;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const bool bSame = At30.IsValidIndex(Index) && At60.IsValidIndex(Index) && At120.IsValidIndex(Index) && AtRandom.IsValidIndex(Index)
			&& At30[Index] == At60[Index] && At60[Index] == At120[Index] && At120[Index] == AtRandom[Index];
		Same += bSame ? 1 : 0;
	}
	TestEqual(TEXT("every step bit-identical at 30 / 60 / 120 fps and random frames"), Same, Steps);

	// Whole frames add up to whole steps: 1 s at 60 fps = 120 steps, 1 s at 30 fps = 120 steps.
	FProjectileFlightState State;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		ProjectileRules::Advance(State, 1.f / 60.f, [](const FVector&, const FVector&, int32) { return false; });
	}
	TestEqual(TEXT("1 s at 60 fps = 120 steps"), State.Steps, 120);
	State = FProjectileFlightState();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		ProjectileRules::Advance(State, 1.f / 30.f, [](const FVector&, const FVector&, int32) { return false; });
	}
	TestEqual(TEXT("1 s at 30 fps = 120 steps"), State.Steps, 120);
	TestEqual(TEXT("the flight time"), State.FlightSeconds(), 1.f, 1e-4f);

	// A segment that reports an impact stops the flight there.
	State = FProjectileFlightState();
	State.Velocity = FVector(1200.f, 0.f, 0.f);
	const bool bStopped = ProjectileRules::Advance(State, 1.f, [](const FVector&, const FVector& To, int32) { return To.X >= 495.f; });
	TestTrue(TEXT("an impact stops the flight"), bStopped);
	TestEqual(TEXT("... at the step that struck"), State.Steps, 50);
	TestEqual(TEXT("... with nothing left in the accumulator"), static_cast<float>(State.Accumulator), 0.f);

	// A long hitch flies at most half a second.
	State = FProjectileFlightState();
	ProjectileRules::Advance(State, 5.f, [](const FVector&, const FVector&, int32) { return false; });
	TestEqual(TEXT("a 5 s hitch flies 60 steps"), State.Steps, ProjectileRules::MaxStepsPerAdvance);

	// Arming: the warhead arms after its flight distance.
	State = FProjectileFlightState();
	State.Velocity = FVector(1200.f, 0.f, 0.f);
	TestFalse(TEXT("not armed at the muzzle"), ProjectileRules::IsArmed(State, 1000.f));
	ProjectileRules::Advance(State, 0.5f, [](const FVector&, const FVector&, int32) { return false; });
	TestFalse(TEXT("not armed at 6 m"), ProjectileRules::IsArmed(State, 1000.f));
	ProjectileRules::Advance(State, 0.5f, [](const FVector&, const FVector&, int32) { return false; });
	TestTrue(TEXT("armed at 12 m"), ProjectileRules::IsArmed(State, 1000.f));
	TestTrue(TEXT("arming distance 0 = armed at once"), ProjectileRules::IsArmed(FProjectileFlightState(), 0.f));

	// High-angle fire's band (SQUAD_SIX_FIRE_TEAMS.md 7.3).
	TestFalse(TEXT("band: 20 m is too close"), ProjectileRules::IsWithinFireBand(2000.f, 3000.f, 25000.f));
	TestTrue(TEXT("band: 30 m"), ProjectileRules::IsWithinFireBand(3000.f, 3000.f, 25000.f));
	TestTrue(TEXT("band: 250 m"), ProjectileRules::IsWithinFireBand(25000.f, 3000.f, 25000.f));
	TestFalse(TEXT("band: 260 m is too far"), ProjectileRules::IsWithinFireBand(26000.f, 3000.f, 25000.f));
	TestTrue(TEXT("band: no max"), ProjectileRules::IsWithinFireBand(90000.f, 3000.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileSchemaTest, "CodexTactics.Weapons.Projectile.Schema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectileSchemaTest::RunTest(const FString&)
{
	using namespace ProjectileRulesTest;
	// Read + the projectile part of the validation (the rest of a default FWeaponStats is not this test's business).
	auto Apply = [](const TCHAR* Text, FWeaponStats& Stats, TArray<FString>& Errors)
	{
		TArray<FString> Unknown;
		const TSharedPtr<FJsonObject> Json = ParseProjectileJson(Text);
		const bool bRead = Json.IsValid() && WeaponStatsRules::ApplyJson(*Json, Stats, &Unknown, &Errors);
		Errors.Append(Unknown);
		TArray<FString> Invalid;
		WeaponStatsRules::Validate(Stats, Invalid, TEXT("test"));
		const int32 ProjectileErrors = Invalid.FilterByPredicate([](const FString& Line) { return Line.Contains(TEXT("projectile")); }).Num();
		Errors.Append(Invalid);
		return bRead && Unknown.Num() == 0 && ProjectileErrors == 0;
	};

	FWeaponStats Mortar;
	TArray<FString> Errors;
	TestTrue(TEXT("a mortar block reads"), Apply(TEXT("{\"kind\": \"projectile\", \"min_range_m\": 30, \"attack_range_m\": 250, \"projectile\": {\"speed_mps\": 60,")
		TEXT(" \"gravity_scale\": 1, \"trajectory\": \"high_angle\", \"max_flight_s\": 12, \"arming_distance_m\": 5}}"), Mortar, Errors));
	TestTrue(TEXT("mortar: high-angle"), Mortar.Projectile.Trajectory == EProjectileTrajectory::HighAngle);
	TestEqual(TEXT("mortar: 12 s max flight"), Mortar.Projectile.MaxFlightSeconds, 12.f);
	TestEqual(TEXT("mortar: arming 5 m"), Mortar.Projectile.ArmingDistanceCm, 500.f);
	TestEqual(TEXT("mortar: 60 m/s"), Mortar.Projectile.SpeedCmS, 6000.f);

	FWeaponStats Legacy;
	Errors.Reset();
	TestTrue(TEXT("the legacy high_angle key reads"), Apply(TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 80, \"gravity_scale\": 1,")
		TEXT(" \"high_angle\": true}}"), Legacy, Errors));
	TestTrue(TEXT("legacy high_angle true = the high-angle trajectory"), Legacy.Projectile.IsHighAngle());

	FWeaponStats Rocket;
	Errors.Reset();
	TestTrue(TEXT("a rocket block reads"), Apply(TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 115, \"trajectory\": \"flat\"}}"), Rocket, Errors));
	TestTrue(TEXT("rocket: flat"), Rocket.Projectile.Trajectory == EProjectileTrajectory::Flat);

	// The round trip keeps the trajectory and the flight time.
	FWeaponStats Back;
	Errors.Reset();
	const TSharedRef<FJsonObject> Written = WeaponStatsRules::ToJson(Mortar);
	TestTrue(TEXT("round trip reads"), WeaponStatsRules::ApplyJson(*Written, Back, nullptr, &Errors) && Errors.Num() == 0);
	TestTrue(TEXT("round trip: trajectory"), Back.Projectile.Trajectory == Mortar.Projectile.Trajectory);
	TestEqual(TEXT("round trip: max flight"), Back.Projectile.MaxFlightSeconds, Mortar.Projectile.MaxFlightSeconds);

	// Bad data is refused.
	const TCHAR* const Bad[] = {
		TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 60, \"trajectory\": \"lob\"}}"),
		TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 60, \"trajectory\": \"ballistic\", \"gravity_scale\": 0}}"),
		TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 60, \"max_flight_s\": -1}}"),
	};
	for (const TCHAR* Text : Bad)
	{
		FWeaponStats Stats;
		Errors.Reset();
		TestFalse(FString::Printf(TEXT("refused: %s"), Text), Apply(Text, Stats, Errors));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
