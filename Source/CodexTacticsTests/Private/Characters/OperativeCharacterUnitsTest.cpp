#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/ProgressionRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/DeployableRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Cleanup day 2026-10-14 (docs/port/REFACTOR_OPERATIVE_2026-10.md): AOperativeCharacter's members now live in one
// translation unit per cluster. This net calls world-free members of several of those units on one operative and checks
// them against the pure rules they wrap, so a definition lost or duplicated by a future move shows up as a test, not
// only as a smoke. UE-only, no Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOperativeUnitsWorldFreeTest, "CodexTactics.Characters.OperativeUnits.WorldFreeMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOperativeUnitsWorldFreeTest::RunTest(const FString&)
{
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	if (!TestNotNull(TEXT("operative"), Operative))
	{
		return false;
	}

	// OperativeCharacter.cpp (constructor) + OperativeCharacterStance.cpp: the stance shapes the constructor sets up.
	TestEqual(TEXT("standing capsule"), Operative->GetStanceShape(EOperativeStance::Standing).CapsuleHalfHeight, 90.f);
	TestEqual(TEXT("crouching capsule"), Operative->GetStanceShape(EOperativeStance::Crouching).CapsuleHalfHeight, 58.5f);
	TestEqual(TEXT("prone capsule"), Operative->GetStanceShape(EOperativeStance::Prone).CapsuleHalfHeight, 40.f);
	TestEqual(TEXT("prone name"), AOperativeCharacter::GetStanceDisplayName(EOperativeStance::Prone).ToString(), FString(TEXT("PRONE")));
	TestEqual(TEXT("standing name"), AOperativeCharacter::GetStanceDisplayName(EOperativeStance::Standing).ToString(), FString(TEXT("STANDING")));

	// OperativeCharacterMovement.cpp: speed / sprint follow OperativeMovementRules, carrying pushes the speed into the CMC.
	const FOperativeMovementConfig& Config = Operative->MovementConfig;
	TestEqual(TEXT("max speed = rules"), Operative->GetMaxSpeed(),
		OperativeMovementRules::ComputeMaxSpeed(Config, EOperativeStance::Standing, false, Operative->IsWounded(), false));
	TestEqual(TEXT("can sprint = rules"), Operative->CanSprint(),
		OperativeMovementRules::CanSprint(Config, EOperativeStance::Standing, Operative->ColdLevel, Operative->IsWounded()));
	TestFalse(TEXT("standing still"), Operative->IsMoving());
	Operative->SetCarrying(true);
	TestFalse(TEXT("no sprint while carrying"), Operative->CanSprint());
	TestEqual(TEXT("carry speed = rules"), Operative->GetMaxSpeed(),
		OperativeMovementRules::ComputeMaxSpeed(Config, EOperativeStance::Standing, false, Operative->IsWounded(), true));
	TestEqual(TEXT("carry speed applied to the movement"), Operative->GetCharacterMovement()->MaxWalkSpeed, Operative->GetMaxSpeed());
	Operative->SetCarrying(false);

	// OperativeCharacterInventory.cpp: deployables clamp to the carry limit; the cycle skips empty types.
	Operative->MinesCount = 0;
	Operative->AddDeployable(EDeployableType::Mine, 99);
	TestEqual(TEXT("mines clamped"), Operative->GetDeployableCount(EDeployableType::Mine), DeployableRules::GetMaxCarried(EDeployableType::Mine));
	Operative->AddDeployable(EDeployableType::Mine, -100);
	TestEqual(TEXT("mines floor 0"), Operative->GetDeployableCount(EDeployableType::Mine), 0);
	Operative->TurretsCount = 0;
	Operative->BarricadesCount = 1;
	Operative->SelectedDeployType = EDeployableType::Turret;
	TestEqual(TEXT("cycle to the barricade"), Operative->CycleDeployableType(), EDeployableType::Barricade);
	TestEqual(TEXT("medkits"), Operative->GetItemCount(EPersonalItem::Medkit), Operative->MedkitsCount);

	// OperativeCharacterProgression.cpp: one stat point spent on luck.
	Operative->UnspentStatPoints = 1;
	const float LuckBefore = Operative->Luck;
	TestEqual(TEXT("next level exp = rules"), Operative->GetNextLevelExp(), ProgressionRules::NextLevelExp(Operative->Level));
	TestTrue(TEXT("can raise luck"), Operative->CanIncreaseStat(EProgressStat::Luck));
	TestTrue(TEXT("luck raised"), Operative->IncreaseStat(EProgressStat::Luck));
	TestEqual(TEXT("luck step"), Operative->Luck, LuckBefore + ProgressionRules::StatStep(EProgressStat::Luck));
	TestEqual(TEXT("point spent"), Operative->UnspentStatPoints, 0);
	TestFalse(TEXT("no point left"), Operative->CanIncreaseStat(EProgressStat::Luck));

	// OperativeCharacterCover.cpp / CornerAim / TargetedShots / Weapons / Health / Overlays: out of cover, nothing armed.
	TestTrue(TEXT("fires freely out of cover"), Operative->CanFireFromCover());
	TestFalse(TEXT("not hidden out of cover"), Operative->IsHiddenInCoverFrom(FVector(500.f, 0.f, 0.f)));
	TestFalse(TEXT("no corner hold out of cover"), Operative->IsCornerHoldSpot());
	TestFalse(TEXT("no corner shot out of cover"), Operative->IsCornerShotTarget(FVector(500.f, 0.f, 0.f)));
	TestEqual(TEXT("nothing to shoot at"), AOperativeCharacter::ClassifyShotTarget(nullptr), ETargetedShotKind::None);
	TestTrue(TEXT("no weapon uses ammo (Godot default)"), Operative->UsesAmmo());
	TestFalse(TEXT("not knocked down"), Operative->IsKnockedDown());
	FVector HitSource;
	TestFalse(TEXT("no hit yet"), Operative->GetLastHitSource(HitSource));
	TestFalse(TEXT("no selection ring"), Operative->IsSelectionRingShown());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
