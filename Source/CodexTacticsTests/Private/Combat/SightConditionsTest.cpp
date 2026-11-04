// CodexTactics.Combat.Sight.Conditions* - the sight range by the weather and the time of day (user request 2026-11-04, L_Gen_Depot_1:
// "on a clear day the cone covers practically everything visible; night cuts it drastically whatever the weather; snowfall / blizzard / fog
// reduce it; the same for the enemies"). Pure rules (SightRules::FSightConditionsConfig) + the data section (EnemyPerception).

#include "Combat/SightRules.h"
#include "Data/EnemyPerception.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightConditionsMultiplierTest, "CodexTactics.Combat.Sight.ConditionsMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightConditionsMultiplierTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	const FSightConditionsConfig Config;
	// The defaults (pending the user): clear-day squad 120 m (2 x the old 60), armed humans x2 over their rows.
	TestEqual(TEXT("squad base 120 m"), Config.SquadBaseRangeCm, 12000.f);
	TestEqual(TEXT("humans x2"), Config.HumanEnemyRangeMultiplier, 2.f);
	// Weather.
	TestEqual(TEXT("clear x1"), WeatherSightMultiplier(Config, EWeatherState::Clear), 1.f, 0.0001f);
	TestEqual(TEXT("overcast x0.85"), WeatherSightMultiplier(Config, EWeatherState::Overcast), 0.85f, 0.0001f);
	TestEqual(TEXT("snowfall x0.6"), WeatherSightMultiplier(Config, EWeatherState::Snowfall), 0.6f, 0.0001f);
	TestEqual(TEXT("blizzard x0.5"), WeatherSightMultiplier(Config, EWeatherState::Blizzard), 0.5f, 0.0001f);
	TestEqual(TEXT("an unknown state x1"), WeatherSightMultiplier(Config, static_cast<EWeatherState>(17)), 1.f, 0.0001f);
	// A weather transition blends (no pop): halfway Snowfall -> Blizzard = 0.55; the blend is clamped.
	TestEqual(TEXT("blend 0 = from"), BlendedWeatherSightMultiplier(Config, EWeatherState::Snowfall, EWeatherState::Blizzard, 0.f), 0.6f, 0.0001f);
	TestEqual(TEXT("blend 0.5"), BlendedWeatherSightMultiplier(Config, EWeatherState::Snowfall, EWeatherState::Blizzard, 0.5f), 0.55f, 0.0001f);
	TestEqual(TEXT("blend 1 = to"), BlendedWeatherSightMultiplier(Config, EWeatherState::Snowfall, EWeatherState::Blizzard, 1.f), 0.5f, 0.0001f);
	TestEqual(TEXT("blend clamped"), BlendedWeatherSightMultiplier(Config, EWeatherState::Clear, EWeatherState::Overcast, 3.f), 0.85f, 0.0001f);
	// Time of day by the sun's elevation: day >= 10 deg, twilight at the horizon, night <= -10 deg, linear between.
	TestEqual(TEXT("noon (52 deg) x1"), DaylightSightMultiplier(Config, 52.f), 1.f, 0.0001f);
	TestEqual(TEXT("10 deg = full day"), DaylightSightMultiplier(Config, 10.f), 1.f, 0.0001f);
	TestEqual(TEXT("5 deg: halfway twilight -> day"), DaylightSightMultiplier(Config, 5.f), 0.85f, 0.0001f);
	TestEqual(TEXT("horizon = twilight 0.7"), DaylightSightMultiplier(Config, 0.f), 0.7f, 0.0001f);
	TestEqual(TEXT("-5 deg: halfway twilight -> night"), DaylightSightMultiplier(Config, -5.f), 0.525f, 0.0001f);
	TestEqual(TEXT("-10 deg = night 0.35"), DaylightSightMultiplier(Config, -10.f), 0.35f, 0.0001f);
	TestEqual(TEXT("midnight (-35 deg) x0.35"), DaylightSightMultiplier(Config, -35.f), 0.35f, 0.0001f);
	// Continuous: no step larger than the slope anywhere from -40 to +40 deg in 0.1 deg steps.
	float MaxStep = 0.f;
	for (float Elevation = -40.f; Elevation < 40.f; Elevation += 0.1f)
	{
		MaxStep = FMath::Max(MaxStep, FMath::Abs(DaylightSightMultiplier(Config, Elevation + 0.1f) - DaylightSightMultiplier(Config, Elevation)));
	}
	TestTrue(FString::Printf(TEXT("no pop over the day (max step %.4f per 0.1 deg)"), MaxStep), MaxStep <= 0.004f);
	// Combined: night cuts the range whatever the weather; the weather multiplies on top.
	TestEqual(TEXT("clear noon x1 (120 m)"), ConditionsSightMultiplier(Config, EWeatherState::Clear, EWeatherState::Clear, 0.f, 40.f), 1.f, 0.0001f);
	TestEqual(TEXT("clear night x0.35 (42 m)"), ConditionsSightMultiplier(Config, EWeatherState::Clear, EWeatherState::Clear, 0.f, -30.f), 0.35f, 0.0001f);
	TestEqual(TEXT("blizzard noon x0.5 (60 m)"), ConditionsSightMultiplier(Config, EWeatherState::Blizzard, EWeatherState::Blizzard, 0.f, 40.f), 0.5f, 0.0001f);
	TestEqual(TEXT("blizzard night x0.175 (21 m)"), ConditionsSightMultiplier(Config, EWeatherState::Blizzard, EWeatherState::Blizzard, 0.f, -30.f), 0.175f, 0.0001f);
	TestTrue(TEXT("night is below every daytime weather"), ConditionsSightMultiplier(Config, EWeatherState::Clear, EWeatherState::Clear, 0.f, -30.f)
		< ConditionsSightMultiplier(Config, EWeatherState::Blizzard, EWeatherState::Blizzard, 0.f, 40.f));
	// Silly thresholds that do not bracket the horizon are clamped round it.
	FSightConditionsConfig Odd;
	Odd.DayElevationDeg = -5.f;
	Odd.NightElevationDeg = 5.f;
	TestEqual(TEXT("clamped thresholds: day above the horizon"), DaylightSightMultiplier(Odd, 1.f), 1.f, 0.0001f);
	TestEqual(TEXT("... night below it"), DaylightSightMultiplier(Odd, -1.f), 0.35f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightConditionsDataTest, "CodexTactics.Combat.Sight.ConditionsData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightConditionsDataTest::RunTest(const FString& Parameters)
{
	// The "sight_conditions" section of enemy_perception.json.
	EnemyPerception::ResetToDefaults();
	TestEqual(TEXT("built-in base 120 m"), EnemyPerception::GetSightConditions().SquadBaseRangeCm, 12000.f);
	TestTrue(TEXT("json with the section applied"), EnemyPerception::ApplyJson(TEXT(
		"{\"sight_conditions\":{\"squad_base_range_m\":90,\"human_enemy_range_multiplier\":1.5,"
		"\"weather\":{\"clear\":0.9,\"blizzard\":0.3},\"time_of_day\":{\"night\":0.2,\"twilight\":0.6,\"day_sun_elevation_deg\":6,\"night_sun_elevation_deg\":-12}},"
		"\"archetypes\":{}}")));
	const SightRules::FSightConditionsConfig Tuned = EnemyPerception::GetSightConditions();
	TestEqual(TEXT("base m -> cm"), Tuned.SquadBaseRangeCm, 9000.f, 0.01f);
	TestEqual(TEXT("humans"), Tuned.HumanEnemyRangeMultiplier, 1.5f, 0.0001f);
	TestEqual(TEXT("clear"), SightRules::WeatherSightMultiplier(Tuned, EWeatherState::Clear), 0.9f, 0.0001f);
	TestEqual(TEXT("overcast kept"), SightRules::WeatherSightMultiplier(Tuned, EWeatherState::Overcast), 0.85f, 0.0001f);
	TestEqual(TEXT("blizzard"), SightRules::WeatherSightMultiplier(Tuned, EWeatherState::Blizzard), 0.3f, 0.0001f);
	TestEqual(TEXT("night"), Tuned.NightMultiplier, 0.2f, 0.0001f);
	TestEqual(TEXT("twilight"), Tuned.TwilightMultiplier, 0.6f, 0.0001f);
	TestEqual(TEXT("day elevation"), Tuned.DayElevationDeg, 6.f, 0.0001f);
	TestEqual(TEXT("night elevation (negative read raw)"), Tuned.NightElevationDeg, -12.f, 0.0001f);
	TestTrue(TEXT("json without the section applied"), EnemyPerception::ApplyJson(TEXT("{\"archetypes\":{}}")));
	TestEqual(TEXT("no section: the built-in base"), EnemyPerception::GetSightConditions().SquadBaseRangeCm, 12000.f);
	// The shipped file carries the section with the requested numbers.
	TestTrue(TEXT("the shipped enemy_perception.json applies"), EnemyPerception::ApplyFile(EnemyPerception::GetDefaultPath()));
	const SightRules::FSightConditionsConfig Shipped = EnemyPerception::GetSightConditions();
	TestEqual(TEXT("shipped: 120 m"), Shipped.SquadBaseRangeCm, 12000.f, 0.01f);
	TestEqual(TEXT("shipped: humans x2"), Shipped.HumanEnemyRangeMultiplier, 2.f, 0.0001f);
	TestEqual(TEXT("shipped: snowfall 0.6"), SightRules::WeatherSightMultiplier(Shipped, EWeatherState::Snowfall), 0.6f, 0.0001f);
	TestEqual(TEXT("shipped: night 0.35"), Shipped.NightMultiplier, 0.35f, 0.0001f);
	EnemyPerception::ResetToDefaults();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
