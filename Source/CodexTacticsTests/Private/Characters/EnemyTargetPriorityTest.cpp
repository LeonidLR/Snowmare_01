// CodexTactics.Characters.EnemyAI.OperativesFirst.* - the game designer's rule 2026-10-25 (packaged-build play-test of the Demo 01
// tutorial: a lone lake zombie went for a barricade instead of the operative it had engaged): an enemy attacks an operative first;
// a barricade or a turret may distract it only when every operative it could go for already has enough OTHER enemies on him, or
// when the operative cannot be reached on foot (then the barricade in the way is the way through). Godot reference:
// Scenes/movements/enemy_base.gd _find_closest_squad_member / _find_blocking_barricade (the old weighting, kept behind a cap of 0).

#include "Misc/AutomationTest.h"
#include "Characters/EnemyAIRules.h"
#include "Characters/VaultRules.h"
#include "Data/AITuning.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstSaturationTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.Saturation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstSaturationTest::RunTest(const FString&)
{
	TestFalse(TEXT("a lone enemy (nobody else on the operative): the operative"), EnemyAIRules::MayBeDistractedByObjects({ 0 }, 2));
	TestFalse(TEXT("one other on him, cap 2: still the operative"), EnemyAIRules::MayBeDistractedByObjects({ 1 }, 2));
	TestTrue(TEXT("two others on him, cap 2: may be distracted"), EnemyAIRules::MayBeDistractedByObjects({ 2 }, 2));
	TestTrue(TEXT("more than the cap: may be distracted"), EnemyAIRules::MayBeDistractedByObjects({ 5 }, 2));
	TestFalse(TEXT("one operative busy, another free: the free one comes first"), EnemyAIRules::MayBeDistractedByObjects({ 3, 1 }, 2));
	TestTrue(TEXT("every operative busy"), EnemyAIRules::MayBeDistractedByObjects({ 2, 2, 4 }, 2));
	TestTrue(TEXT("no operative it could go for (unknown / out of reach / in a fire zone): objects allowed"),
		EnemyAIRules::MayBeDistractedByObjects({}, 2));
	TestTrue(TEXT("cap 0 = the rule off (the Godot weighting)"), EnemyAIRules::MayBeDistractedByObjects({ 0 }, 0));
	TestTrue(TEXT("a negative cap is off as well"), EnemyAIRules::MayBeDistractedByObjects({ 0, 0 }, -1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstTargetPickTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.TargetPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstTargetPickTest::RunTest(const FString&)
{
	const FEnemyAIConfig Config;
	const FVector Enemy(0.f, 0.f, 0.f);
	// The tutorial case: a small enemy (the lake's Military Zombie is a Frostbitten) with the squad's turret 2 m off and the operative 10 m off.
	const TArray<FEnemyTargetCandidate> TurretNear = { { EEnemyTargetKind::Operative, FVector(1000.f, 0.f, 100.f), true },
		{ EEnemyTargetKind::Turret, FVector(0.f, 200.f, 0.f), true } };
	TestEqual(TEXT("Godot weighting (allowed): the turret 2 m x 0.5 beats the operative 10 m"),
		EnemyAIRules::SelectTarget(Config, true, Enemy, TurretNear, false, true), 1);
	TestEqual(TEXT("operatives first: the zombie goes for the operative"), EnemyAIRules::SelectTarget(Config, true, Enemy, TurretNear, false, false), 0);
	TestEqual(TEXT("the default argument keeps the Godot weighting"), EnemyAIRules::SelectTarget(Config, true, Enemy, TurretNear, false), 1);

	// A large enemy: a turret within the threat distance, hit by the turret last - still the operative.
	const TArray<FEnemyTargetCandidate> TurretClose = { { EEnemyTargetKind::Operative, FVector(1500.f, 0.f, 100.f), true },
		{ EEnemyTargetKind::Turret, FVector(0.f, 800.f, 0.f), true } };
	TestEqual(TEXT("brute, turret in the threat distance and hitting it: allowed -> the turret"),
		EnemyAIRules::SelectTarget(Config, false, Enemy, TurretClose, true, true), 1);
	TestEqual(TEXT("brute, operatives first -> the operative"), EnemyAIRules::SelectTarget(Config, false, Enemy, TurretClose, true, false), 0);

	// No operative to go for (out of reach for a while / in a fire zone): the turret is fair game even when not "allowed".
	TArray<FEnemyTargetCandidate> NoOperative = TurretNear;
	NoOperative[0].bUsable = false;
	TestEqual(TEXT("operative unusable: the turret"), EnemyAIRules::SelectTarget(Config, true, Enemy, NoOperative, false, false), 1);

	// A manned APC is the squad too: no turret while it can be gone for.
	const TArray<FEnemyTargetCandidate> WithVehicle = { { EEnemyTargetKind::Vehicle, FVector(1200.f, 0.f, 0.f), true, 1.f },
		{ EEnemyTargetKind::Turret, FVector(0.f, 200.f, 0.f), true } };
	TestEqual(TEXT("a manned APC comes before the turret"), EnemyAIRules::SelectTarget(Config, true, Enemy, WithVehicle, false, false), 0);

	// The generator is no barricade / turret: small enemies keep its Godot weight (the wave objective).
	const TArray<FEnemyTargetCandidate> WithGenerator = { { EEnemyTargetKind::Operative, FVector(500.f, 0.f, 100.f), true },
		{ EEnemyTargetKind::Generator, FVector(1000.f, 0.f, 0.f), true } };
	TestEqual(TEXT("generator weight unchanged (10 m x 0.4 beats 5.1 m)"), EnemyAIRules::SelectTarget(Config, true, Enemy, WithGenerator, false, false), 1);

	// Turrets switched off by balance stay off whatever the rule says.
	FEnemyAIConfig NoTurrets;
	NoTurrets.bCanTargetTurrets = false;
	TestEqual(TEXT("enemy_target_turrets 0: never a turret"), EnemyAIRules::SelectTarget(NoTurrets, true, Enemy, NoOperative, false, true), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstObstacleTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.BlockingObstacle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstObstacleTest::RunTest(const FString&)
{
	TestFalse(TEXT("reachable operative, nobody else on him: round the barricade"), EnemyAIRules::ShouldSmashBlockingObstacle(true, true, false));
	TestTrue(TEXT("operative behind a closed barricade line: smash through"), EnemyAIRules::ShouldSmashBlockingObstacle(true, false, false));
	TestTrue(TEXT("operatives saturated: the barricade may distract it"), EnemyAIRules::ShouldSmashBlockingObstacle(true, true, true));
	TestTrue(TEXT("target is an object (turret / generator): the Godot rule"), EnemyAIRules::ShouldSmashBlockingObstacle(false, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstReachTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.Reachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstReachTest::RunTest(const FString&)
{
	const float Hound = 180.f; // attack range, cm; the target is his Godot position (1 m above his feet)
	TestTrue(TEXT("full path to his side"), EnemyAIRules::PathEndReachesTarget(FVector(40.f, 0.f, 0.f), FVector(0.f, 0.f, 100.f), Hound));
	TestTrue(TEXT("crouched right behind a 60 cm barricade, path ends on the near side 1.4 m off: bites across"),
		EnemyAIRules::PathEndReachesTarget(FVector::ZeroVector, FVector(140.f, 0.f, 100.f), Hound));
	TestFalse(TEXT("partial path stopped 2.5 m short by a closed line: unreachable"),
		EnemyAIRules::PathEndReachesTarget(FVector::ZeroVector, FVector(250.f, 0.f, 100.f), Hound));
	TestFalse(TEXT("he stands on a 1.5 m platform: unreachable"), EnemyAIRules::PathEndReachesTarget(FVector(50.f, 0.f, 0.f), FVector(0.f, 0.f, 250.f), Hound));
	TestTrue(TEXT("navmesh 25 cm under the ground is no false negative"),
		EnemyAIRules::PathEndReachesTarget(FVector(100.f, 0.f, -25.f), FVector(0.f, 0.f, 100.f), Hound));
	TestFalse(TEXT("without the height slack the same spot fails CanMelee (why the slack exists)"),
		EnemyAIRules::PathEndReachesTarget(FVector(100.f, 0.f, -25.f), FVector(0.f, 0.f, 100.f), Hound, 0.f));
	return true;
}

// The root of the tutorial glitch: the barricade's vault nav area (closed to enemies) was the engine's 2 x 2 m failsafe square
// round a 3 m box, so the enemies' paths round it ran through its solid ends and a zombie going round stuck there (in reach of
// the Godot smash rule). The area now covers the box plus an enemy capsule radius.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstVaultAreaTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.VaultAreaCoversBarricade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstVaultAreaTest::RunTest(const FString&)
{
	const FBox Barricade(FVector(-150.f, -30.f, -30.f), FVector(150.f, 30.f, 30.f)); // 3 m x 0.6 m x 0.6 m, pivot in the middle
	const FVector Extent = VaultRules::NavAreaExtent(Barricade, FVector::OneVector);
	TestEqual(TEXT("length: the whole 3 m + 40 cm each end"), Extent.X, 190.0);
	TestEqual(TEXT("depth: 0.6 m + 40 cm each side"), Extent.Y, 70.0);
	TestEqual(TEXT("height reaches 50 cm under the box"), Extent.Z, 80.0);
	TestTrue(TEXT("longer than the old 2 x 2 m failsafe (100)"), Extent.X > 150.f + 30.f);
	TestEqual(TEXT("a pivot at the base: mirrored to cover the whole height"), VaultRules::NavAreaExtent(FBox(FVector(-50.f, -50.f, 0.f),
		FVector(50.f, 50.f, 100.f)), FVector::OneVector).Z, 150.0);
	TestEqual(TEXT("actor scale applies"), VaultRules::NavAreaExtent(Barricade, FVector(2.f, 1.f, 1.f)).X, 340.0);
	TestEqual(TEXT("no collision: the engine failsafe"), VaultRules::NavAreaExtent(FBox(ForceInit), FVector::OneVector), FVector(100.f, 100.f, 100.f));
	return true;
}

// The cap is data: ai_tuning.json "cvars" sets the game value, the console variable exists for -dpcvars= / A/B (0 = Godot).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOperativesFirstDataTest, "CodexTactics.Characters.EnemyAI.OperativesFirst.CapByData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOperativesFirstDataTest::RunTest(const FString&)
{
	FString Text;
	if (!TestTrue(TEXT("ai_tuning.json readable"), FFileHelper::LoadFileToString(Text, *AITuning::GetDefaultPath())))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* CVars = nullptr;
	if (!TestTrue(TEXT("ai_tuning.json parses with a \"cvars\" object"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		&& Root.IsValid() && Root->TryGetObjectField(TEXT("cvars"), CVars) && CVars))
	{
		return false;
	}
	FString Value;
	TestTrue(TEXT("ai_tuning.json sets Codex.Enemy.OperativeAttackersBeforeObjects"),
		(*CVars)->TryGetStringField(TEXT("Codex.Enemy.OperativeAttackersBeforeObjects"), Value));
	TestTrue(TEXT("the cap is a positive whole number (the rule is on)"), Value.IsNumeric() && FCString::Atoi(*Value) > 0);
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Enemy.OperativeAttackersBeforeObjects"));
	TestNotNull(TEXT("the console variable exists"), CVar);
	return true;
}

#endif
