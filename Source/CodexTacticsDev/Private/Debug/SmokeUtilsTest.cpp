#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeStopLevelPmcTest, "CodexTactics.Smoke.Fixture.StopsPreloadedPmc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmokeStopLevelPmcTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
	IConsoleVariable* Encounters = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pmc.Encounters"));
	if (!TestNotNull(TEXT("PMC subsystem"), Pmc) || !TestNotNull(TEXT("PMC encounter switch"), Encounters))
	{
		return false;
	}
	const int32 PreviousValue = Encounters->GetInt();
	const EConsoleVariableFlags PreviousPriority = static_cast<EConsoleVariableFlags>(Encounters->GetFlags() & ECVF_SetByMask);
	ON_SCOPE_EXIT
	{
		if (PreviousPriority == ECVF_SetByConsole)
		{
			Encounters->Set(PreviousValue, ECVF_SetByConsole);
		}
		else
		{
			Encounters->Unset(ECVF_SetByConsole);
		}
	};
	Encounters->Set(1, ECVF_SetByConsole);

	AOperativeCharacter* Player = World->SpawnActor<AOperativeCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AOperativeCharacter* Hostile = World->SpawnActor<AOperativeCharacter>(FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Player operative"), Player) || !TestNotNull(TEXT("Preloaded PMC operative"), Hostile))
	{
		return false;
	}

	constexpr int32 HostileTeam = 1;
	Hostile->TeamId = HostileTeam;
	Pmc->RegisterOperative(Hostile);
	TestEqual(TEXT("A hostile is already registered before the smoke"), Pmc->GetTeamMembers(HostileTeam).Num(), 1);

	SmokeUtils::StopLevelPmc(World);

	TestEqual(TEXT("No more scheduled squads may spawn"), Encounters->GetInt(), 0);
	TestTrue(TEXT("The preloaded PMC operative was destroyed"), !IsValid(Hostile) || Hostile->IsActorBeingDestroyed());
	TestTrue(TEXT("The preloaded team has no fighters"), Pmc->GetTeamMembers(HostileTeam).IsEmpty());
	TestTrue(TEXT("The player operative survives fixture setup"), IsValid(Player) && !Player->IsActorBeingDestroyed());
	return true;
}

#endif
