#include "Misc/AutomationTest.h"

#include "CodexCookRules.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tester build 2026-10-24: the packaged game showed capsules - the character Blueprints named only by C++ class defaults
// were not cooked. The cook hook must see them (and every path it adds must exist). UE-only, no Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodexCookNativeSoftRefsTest, "CodexTactics.Editor.Cook.NativeSoftReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexCookNativeSoftRefsTest::RunTest(const FString&)
{
	const TSet<FSoftObjectPath> Paths = CodexCookRules::CollectNativeSoftReferences();
	TSet<FString> Packages;
	for (const FSoftObjectPath& Path : Paths)
	{
		Packages.Add(Path.GetLongPackageName());
	}
	for (const TCHAR* Wanted : {
			 TEXT("/Game/Characters/Operatives/BP_Operative"),
			 TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier"),
			 TEXT("/Game/Characters/Operatives/Optimized/Female_Soldier/SK_Female_Soldier_1slot"),
			 TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound"),
			 TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute"),
			 TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten"),
			 TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter"),
			 TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman"),
			 TEXT("/Game/Data/Balance/DA_GameBalanceConfig") })
	{
		TestTrue(FString::Printf(TEXT("cook sees %s"), Wanted), Packages.Contains(Wanted));
	}
	int32 Missing = 0;
	for (const FString& Package : Packages)
	{
		if (!FPackageName::DoesPackageExist(Package))
		{
			AddInfo(FString::Printf(TEXT("C++ default names a missing asset: %s"), *Package));
			++Missing;
		}
	}
	AddInfo(FString::Printf(TEXT("%d package(s) named by C++ class defaults, %d missing on disk"), Packages.Num(), Missing));
	return true;
}

#endif
