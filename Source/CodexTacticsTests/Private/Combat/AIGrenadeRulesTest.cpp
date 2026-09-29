#include "Misc/AutomationTest.h"
#include "Combat/AIGrenadeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/player.gd _evaluate_ai_grenade_opportunity parity: cluster of 3 within the blast, stance range,
// thrower safety distance, friendly fire, biggest cluster wins.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIGrenadeRulesTest, "CodexTactics.Combat.Grenade.AIRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIGrenadeRulesTest::RunTest(const FString&)
{
	const FAIGrenadeConfig Config;
	const FVector Thrower(0.f, 0.f, 100.f); // centre
	const TArray<FVector> Pack = { FVector(900.f, 0.f, 0.f), FVector(1000.f, 100.f, 0.f), FVector(950.f, -150.f, 0.f) };
	const FAIGrenadeOpportunity Throw = AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Standing, 1200.f, 400.f, Pack, {});
	TestTrue(TEXT("Pack of 3 at 9-10 m: throw"), Throw.bCanThrow && Throw.EnemyCount == 3);

	TestFalse(TEXT("Two enemies are not a cluster"),
		AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Standing, 1200.f, 400.f, { Pack[0], Pack[1] }, {}).bCanThrow);
	TestFalse(TEXT("Prone: 6 m range"), AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Prone, 1200.f, 400.f, Pack, {}).bCanThrow);
	TestFalse(TEXT("Ally within 5 m of the aim point"),
		AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Standing, 1200.f, 400.f, Pack, { FVector(700.f, 0.f, 100.f) }).bCanThrow);

	const TArray<FVector> Close = { FVector(300.f, 0.f, 0.f), FVector(350.f, 50.f, 0.f), FVector(320.f, -60.f, 0.f) };
	TestFalse(TEXT("Too close to the thrower (< 5 m)"),
		AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Standing, 1200.f, 400.f, Close, {}).bCanThrow);

	TArray<FVector> Two = Pack;
	Two.Append({ FVector(600.f, 800.f, 0.f), FVector(650.f, 850.f, 0.f), FVector(620.f, 780.f, 0.f), FVector(580.f, 820.f, 0.f) });
	const FAIGrenadeOpportunity Bigger = AIGrenadeRules::Evaluate(Config, Thrower, EOperativeStance::Standing, 1200.f, 400.f, Two, {});
	TestTrue(TEXT("The bigger cluster wins"), Bigger.bCanThrow && Bigger.EnemyCount == 4 && Bigger.Target.Y > 700.f);

	const FAIGrenadeConfig Defaults = AIGrenadeRules::ConfigFromBalance(nullptr);
	TestTrue(TEXT("Defaults"), Defaults.MinCluster == 3 && Defaults.Cooldown == 3.f && Defaults.MinThrowerDistance == 500.f);
	return true;
}

#endif
