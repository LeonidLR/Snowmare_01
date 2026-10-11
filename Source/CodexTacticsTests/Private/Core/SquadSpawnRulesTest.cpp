#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/SquadSpawnRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// User request 2026-10-11: a squad member may spawn from its own Blueprint (the Medic-Sapper's Female Soldier).

#define SQUAD_SPAWN_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Core.SquadSpawn." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SQUAD_SPAWN_TEST(FSquadSpawnMemberClassTest, "MemberBlueprintSelection")
bool FSquadSpawnMemberClassTest::RunTest(const FString&)
{
	UClass* Default = AOperativeCharacter::StaticClass();
	UClass* Member = ACharacter::StaticClass(); // any other class stands in for the member Blueprint

	const SquadSpawnRules::FMemberClassChoice Own = SquadSpawnRules::ChooseMemberClass(true, Member, Default);
	TestTrue(TEXT("set and loaded: the member Blueprint"), Own.Class == Member);
	TestFalse(TEXT("set and loaded: the Blueprint owns the look, no BodyMesh"), Own.bApplyBodyMesh);
	TestFalse(TEXT("set and loaded: not missing"), Own.bMemberBlueprintMissing);

	const SquadSpawnRules::FMemberClassChoice Missing = SquadSpawnRules::ChooseMemberClass(true, nullptr, Default);
	TestTrue(TEXT("set but missing: the operative Blueprint"), Missing.Class == Default);
	TestTrue(TEXT("set but missing: BodyMesh still applies"), Missing.bApplyBodyMesh);
	TestTrue(TEXT("set but missing: reported"), Missing.bMemberBlueprintMissing);

	const SquadSpawnRules::FMemberClassChoice Unset = SquadSpawnRules::ChooseMemberClass(false, nullptr, Default);
	TestTrue(TEXT("unset: the operative Blueprint"), Unset.Class == Default);
	TestTrue(TEXT("unset: BodyMesh applies"), Unset.bApplyBodyMesh);
	TestFalse(TEXT("unset: nothing to report"), Unset.bMemberBlueprintMissing);

	const SquadSpawnRules::FMemberClassChoice Ignored = SquadSpawnRules::ChooseMemberClass(false, Member, Default);
	TestTrue(TEXT("unset: a stray class is ignored"), Ignored.Class == Default && Ignored.bApplyBodyMesh);
	return true;
}

SQUAD_SPAWN_TEST(FSquadSpawnRosterDefaultsTest, "RosterDefaults")
bool FSquadSpawnRosterDefaultsTest::RunTest(const FString&)
{
	const ACodexTacticsGameMode* GameMode = GetDefault<ACodexTacticsGameMode>();
	if (!TestEqual(TEXT("roster size"), GameMode->SquadRoster.Num(), 3))
	{
		return false;
	}
	for (const FSquadMemberSpawn& Entry : GameMode->SquadRoster)
	{
		if (Entry.Role == EOperativeRole::MedicSapper)
		{
			TestEqual(TEXT("Medic-Sapper member Blueprint"), Entry.MemberBlueprint.ToString(),
				FString(TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C")));
			TestFalse(TEXT("Medic-Sapper keeps the BodyMesh fallback"), Entry.BodyMesh.IsNull());
			TestEqual(TEXT("Medic-Sapper keeps the sniper rifle"), Entry.ExtraWeapons.Num(), 1);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s has no member Blueprint"), *Entry.DisplayName.ToString()), Entry.MemberBlueprint.IsNull());
		}
	}
	TestTrue(TEXT("the recruit has no member Blueprint"), GameMode->RecruitSusanin.MemberBlueprint.IsNull());
	return true;
}

#endif
