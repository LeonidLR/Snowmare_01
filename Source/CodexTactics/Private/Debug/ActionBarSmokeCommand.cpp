// Dev-only console command for a headless action bar check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ActionBarSmoke
// 1. the stance slot cycles Standing -> Crouching -> Prone -> Standing for the selected operative only (user decision
// 2026-10-01), Alt + Z / C / V (SetEntireSquadStance) sets the whole squad; 2. «ПЕР» toggles the
// object-pick mode on and off; 3. squad slot 2 makes the engineer the leader; 4. in the preparation each operative
// keeps a stance of his own (Godot has_custom_stance: medic prone, engineer crouched, commander standing), the squad
// stance sync leaves them alone.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace ActionBarSmoke
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			int32 Failures = 0;
			auto Check = [&Failures](bool bOk, const TCHAR* What)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What);
				Failures += bOk ? 0 : 1;
			};
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(W, 0));
			USquadSubsystem* Squad = W->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (!PC || !Leader || Squad->GetMembers().Num() < 3)
			{
				Check(false, TEXT("controller and squad"));
			}
			else
			{
				Leader->StopOperative();
				PC->CycleLeaderStance();
				Check(Leader->GetStance() == EOperativeStance::Crouching && Squad->GetMembers()[2]->GetStance() == EOperativeStance::Standing,
					TEXT("stance slot: only the selected operative crouches"));
				PC->CycleLeaderStance();
				Check(Leader->GetStance() == EOperativeStance::Prone, TEXT("stance slot: prone"));
				PC->CycleLeaderStance();
				Check(Leader->GetStance() == EOperativeStance::Standing, TEXT("stance slot: back to standing"));
				PC->SetEntireSquadStance(EOperativeStance::Crouching);
				bool bAllCrouched = true;
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					bAllCrouched &= Member->GetStance() == EOperativeStance::Crouching;
				}
				Check(bAllCrouched, TEXT("Alt + C: the whole squad crouches"));
				PC->SetEntireSquadStance(EOperativeStance::Standing);
				PC->ToggleRelocateSelectMode();
				Check(PC->IsRelocateSelectMode(), TEXT("relocate slot: pick mode on"));
				PC->ToggleRelocateSelectMode();
				Check(!PC->IsRelocateSelectMode(), TEXT("relocate slot: pick mode off"));
				PC->SelectSquadMember(1);
				Check(Squad->GetLeader() && Squad->GetLeader()->SquadRole == EOperativeRole::Engineer, TEXT("slot 2 selects the engineer"));

				UGameFlowSubsystem* Flow = W->GetSubsystem<UGameFlowSubsystem>();
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Check(Flow->GetPhase() == ECodexGamePhase::Preparation, TEXT("preparation"));
				TArray<AOperativeCharacter*> Members = Squad->GetMembers();
				Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
				for (AOperativeCharacter* Member : Members)
				{
					Member->StopOperative();
				}
				PC->SelectSquadMember(2);
				PC->CycleLeaderStance();
				PC->CycleLeaderStance();
				PC->SelectSquadMember(1);
				PC->CycleLeaderStance();
				PC->SelectSquadMember(0);
				Check(Members[0]->GetStance() == EOperativeStance::Standing && Members[1]->GetStance() == EOperativeStance::Crouching
					&& Members[2]->GetStance() == EOperativeStance::Prone,
					*FString::Printf(TEXT("preparation: own stances kept (commander %d, engineer %d, medic %d)"), static_cast<int32>(Members[0]->GetStance()),
						static_cast<int32>(Members[1]->GetStance()), static_cast<int32>(Members[2]->GetStance())));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("ActionBarSmoke"));
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ActionBarSmoke"),
		TEXT("Dev check: action bar stance cycle, relocation pick mode, squad slot selection; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
