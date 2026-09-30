#include "Misc/AutomationTest.h"
#include "Characters/VaultRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot locomotion_controller.gd check_vault_obstacle / start_vault / the vault arc parity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaultRulesTest, "CodexTactics.Characters.Vault.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaultRulesTest::RunTest(const FString&)
{
	TestTrue(TEXT("A 1 m barricade with ground behind"), VaultRules::CanVault(100.f, true, 0.f));
	TestFalse(TEXT("Too high (over 1.05 m)"), VaultRules::CanVault(120.f, true, 0.f));
	TestFalse(TEXT("Too low (under 25 cm: just walk)"), VaultRules::CanVault(20.f, true, 0.f));
	TestFalse(TEXT("Nothing to land on"), VaultRules::CanVault(80.f, false, 0.f));
	TestFalse(TEXT("A drop of more than 1 m behind"), VaultRules::CanVault(80.f, true, -150.f));
	TestTrue(TEXT("Walking vault 1.25 s / 1.15"), FMath::IsNearlyEqual(VaultRules::Duration(false), 1.25f / 1.15f, 0.001f));
	TestTrue(TEXT("Running vault 0.95 s / 1.15"), FMath::IsNearlyEqual(VaultRules::Duration(true), 0.95f / 1.15f, 0.001f));

	const FVector Start(0.f, 0.f, 90.f);
	const FVector Landing(250.f, 0.f, 90.f);
	TestTrue(TEXT("Starts at the start"), VaultRules::Position(Start, Landing, 100.f, 0.f).Equals(Start, 0.1f));
	TestTrue(TEXT("Ends on the landing spot"), VaultRules::Position(Start, Landing, 100.f, 1.f).Equals(Landing, 0.1f));
	const FVector Middle = VaultRules::Position(Start, Landing, 100.f, 0.5f);
	TestTrue(TEXT("Apex over the obstacle (height + 15 cm) halfway"), FMath::IsNearlyEqual(Middle.Z, 90.f + 115.f, 0.5f)
		&& FMath::IsNearlyEqual(Middle.X, 125.f, 0.5f));
	return true;
}

#endif
