// CodexTactics.Movement.Vault.LevelGenCovers - #36 (user 2026-11-04, L_Gen_Depot_1: the operatives did not vault over the low obstacles).
// The level generator's covers carry their layout kind as a tag and were never "Vault"; the low kinds that fit the vault are made vaultable
// at the start of play (VaultRules::IsVaultableLevelObject). The Depot's sizes (Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json):
// sandbag 250 x 80 x 100, jersey 300 x 60 x 90, barrel 60 x 60 x 90, crate 120 x 120 x 110, wreck 420 x 180 x 150, boiler 200 x 120 x 160.

#include "Characters/VaultRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaultLevelGenCoversTest, "CodexTactics.Movement.Vault.LevelGenCovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaultLevelGenCoversTest::RunTest(const FString&)
{
	TestTrue(TEXT("a sandbag wall (100 cm, 80 deep) is vaulted"), VaultRules::IsVaultableLevelObject(TEXT("sandbag"), 100.f, 80.f));
	TestTrue(TEXT("a jersey barrier (90 cm, 60 deep)"), VaultRules::IsVaultableLevelObject(TEXT("jersey"), 90.f, 60.f));
	TestTrue(TEXT("a barrel (90 cm, 60 wide)"), VaultRules::IsVaultableLevelObject(TEXT("barrel"), 90.f, 60.f));
	TestTrue(TEXT("a low wall (60 cm, 30 deep)"), VaultRules::IsVaultableLevelObject(TEXT("low_wall"), 60.f, 30.f));
	TestFalse(TEXT("a crate (110 cm) is cover: over the 105 cm vault"), VaultRules::IsVaultableLevelObject(TEXT("crate"), 110.f, 120.f));
	TestFalse(TEXT("a wreck (150 cm) is cover"), VaultRules::IsVaultableLevelObject(TEXT("wreck"), 150.f, 180.f));
	TestFalse(TEXT("a boiler (160 cm) is cover"), VaultRules::IsVaultableLevelObject(TEXT("boiler"), 160.f, 120.f));
	TestFalse(TEXT("a 'sandbag' 120 cm tall: over the vault"), VaultRules::IsVaultableLevelObject(TEXT("sandbag"), 120.f, 80.f));
	TestFalse(TEXT("a 'sandbag' 2 m deep: no landing past it"), VaultRules::IsVaultableLevelObject(TEXT("sandbag"), 100.f, 200.f));
	TestFalse(TEXT("a 20 cm kerb: walked over, not vaulted"), VaultRules::IsVaultableLevelObject(TEXT("low_wall"), 20.f, 30.f));
	TestFalse(TEXT("an untagged kind"), VaultRules::IsVaultableLevelObject(TEXT("cover"), 90.f, 60.f));
	TestTrue(TEXT("a ground window sill (90 cm, 30 deep) is vaulted"), VaultRules::IsVaultableLevelObject(TEXT("window_sill"), 90.f, 30.f));
	TestFalse(TEXT("an upper mantle sill is not auto-vaultable"), VaultRules::IsVaultableLevelObject(TEXT("mantle_sill"), 90.f, 30.f));
	TestFalse(TEXT("a loophole sill (120 cm) is not vaultable"), VaultRules::IsVaultableLevelObject(TEXT("loophole_sill"), 120.f, 30.f));
	TestFalse(TEXT("a roof parapet is not auto-vaultable"), VaultRules::IsVaultableLevelObject(TEXT("parapet"), 100.f, 20.f));
	TestTrue(TEXT("the kinds"), VaultRules::IsVaultableCoverKind(TEXT("jersey")) && VaultRules::IsVaultableCoverKind(TEXT("window_sill")) && !VaultRules::IsVaultableCoverKind(TEXT("wall")) && !VaultRules::IsVaultableCoverKind(TEXT("fence")));
	return true;
}

#endif
