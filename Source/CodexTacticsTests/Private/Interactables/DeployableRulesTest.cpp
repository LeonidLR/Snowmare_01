#include "Misc/AutomationTest.h"
#include "Interactables/DeployableRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Defusal / mine / blast parity with Godot deployables/mine.gd, barricade.gd, interactable.gd.

#define DEPLOYABLE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Deployable." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DEPLOYABLE_TEST(FDefusalChanceTest, "DefusalChanceByRoleStanceLuckCold")
bool FDefusalChanceTest::RunTest(const FString&)
{
	using namespace DeployableRules;
	// Medic-sapper (60) prone (+25), luck 35 (+17.5), warm: 102.5 -> 99.
	TestEqual(TEXT("Sapper prone"), CalculateDefusal(EOperativeRole::MedicSapper, EOperativeStance::Prone, 35.f, 0.f, 0, false).Chance, 99.f);
	// Commander (35) standing (-15), luck 25 (+12.5), cold 60 (-30): 2.5, dangerous.
	const FDefusalChance Cold = CalculateDefusal(EOperativeRole::Commander, EOperativeStance::Standing, 25.f, 60.f, 0, false);
	TestEqual(TEXT("Cold commander"), Cold.Chance, 2.5f, 0.001f);
	TestTrue(TEXT("Dangerous"), Cold.bDangerous);
	// Engineer (45) crouching (+10), luck 30 (+15), one failure (-25): 45, dangerous after a failure.
	const FDefusalChance Retry = CalculateDefusal(EOperativeRole::Engineer, EOperativeStance::Crouching, 30.f, 0.f, 1, false);
	TestEqual(TEXT("Engineer retry"), Retry.Chance, 45.f, 0.001f);
	TestTrue(TEXT("Retry is dangerous"), Retry.bDangerous);
	TestEqual(TEXT("Stance names"), GetDefusalStanceName(EOperativeStance::Crouching).ToString(), FString(TEXT("Присев")));
	return true;
}

DEPLOYABLE_TEST(FDefusalResolveTest, "WarningThenRiskyFailure")
bool FDefusalResolveTest::RunTest(const FString&)
{
	using namespace DeployableRules;
	FDefusalChance Risky;
	Risky.Chance = 30.f;
	Risky.bDangerous = true;
	bool bWarned = false;
	int32 Failed = 0;
	TestTrue(TEXT("First dangerous attempt only warns"), ResolveDefusal(Risky, bWarned, Failed, 1.f, 1.f) == EDefusalResult::Warning);
	TestTrue(TEXT("Warned"), bWarned);
	TestTrue(TEXT("Roll under chance succeeds"), ResolveDefusal(Risky, bWarned, Failed, 29.f, 1.f) == EDefusalResult::Success);
	TestTrue(TEXT("After a warning a miss detonates at 80 % risk"), ResolveDefusal(Risky, bWarned, Failed, 50.f, 79.f) == EDefusalResult::Detonation);
	TestEqual(TEXT("Failure counted"), Failed, 1);

	FDefusalChance Safe;
	Safe.Chance = 70.f;
	bool bSafeWarned = false;
	int32 SafeFailed = 0;
	TestTrue(TEXT("Safe miss with 40 % risk and a high roll just fails"),
		ResolveDefusal(Safe, bSafeWarned, SafeFailed, 90.f, 41.f) == EDefusalResult::Failure);
	return true;
}

DEPLOYABLE_TEST(FMineBlastTest, "MishapAndBlastFalloff")
bool FMineBlastTest::RunTest(const FString&)
{
	using namespace DeployableRules;
	TestEqual(TEXT("Sapper warm"), GetMineMishapChance(true, 0.f), 2.f);
	TestEqual(TEXT("Regular at 50 % cold"), GetMineMishapChance(false, 50.f), 20.f);
	TestEqual(TEXT("Mine at the centre"), GetBlastDamage(120.f, 0.f, 350.f, EnemyFalloff), 120.f);
	TestEqual(TEXT("Enemy at the edge"), GetBlastDamage(120.f, 350.f, 350.f, EnemyFalloff), 72.f, 0.001f);
	TestEqual(TEXT("Squad at half radius"), GetBlastDamage(120.f * SquadDamageScale, 175.f, 350.f, SquadFalloff), 67.5f, 0.001f);
	TestEqual(TEXT("Outside"), GetBlastDamage(120.f, 351.f, 350.f, EnemyFalloff), 0.f);
	TestEqual(TEXT("Carry limits"), GetMaxCarried(EDeployableType::Barricade) * 10 + GetMaxCarried(EDeployableType::Mine), 45);
	return true;
}

#undef DEPLOYABLE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
