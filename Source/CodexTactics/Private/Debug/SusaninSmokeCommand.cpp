// Dev-only console command for a headless Susanin rescue check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SusaninSmoke
// Rescue on wave 1 (instant events): 3 s into the wave Susanin appears at the SusaninSpawn point outside the squad,
// freezing (cold 85), with the narrative pause, the camera on him and the distress dialogue; closing it restores
// time, the camera and posts the HQ line / objective. An operative walking within 2.8 m opens the recruitment
// dialogue; accepting it adds him as member 4 (key 4 selects him, cold 0, radio lines, wave objective). Save / load
// restore toggles him out of and back into the squad (Godot main.gd _trigger_susanin_rescue_event, recruit_susanin.gd).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace SusaninSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SusaninSmoke"));
		return false;
	}

	bool HasMessage(UWorld* World, const FString& Speaker, const FString& Part)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (Message.Speaker.ToString() == Speaker && Message.Text.ToString().Contains(Part))
			{
				return true;
			}
		}
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		URecruitSubsystem* Recruits = World->GetSubsystem<URecruitSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
		if (!Flow || !Waves || !Recruits || !Dialogue || !Leader || !Camera || State.Time > 40.f)
		{
			Check(State, false, FString::Printf(TEXT("subsystems / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->CustomTimeDilation = 0.f; // the wave must not interfere
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		AOperativeCharacter* Susanin = Recruits->GetSusanin();
		switch (State.Stage)
		{
		case 0:
			Waves->SetRandomEventWaves(3, 1);
			Waves->bInstantRandomEvents = true;
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Next();
			return true;
		case 1: // Godot headless delay: 3 s into the wave.
			if (!Susanin)
			{
				return true;
			}
		{
			TArray<AActor*> Points;
			UGameplayStatics::GetAllActorsWithTag(World, URecruitSubsystem::SpawnTag, Points);
			Check(State, Points.Num() == 1 && FVector::Dist2D(Susanin->GetActorLocation(), Points[0]->GetActorLocation()) < 100.f,
				TEXT("Susanin at the SusaninSpawn point"));
			Check(State, !Susanin->bRecruited && !Squad->GetMembers().Contains(Susanin) && Squad->GetMembers().Num() == 3, TEXT("not in the squad"));
			Check(State, Recruits->IsInColdDistress() && FMath::IsNearlyEqual(Susanin->ColdLevel, 85.f, 1.f), TEXT("cold distress 85"));
			Check(State, Susanin->DisplayName.ToString() == TEXT("Иван Сусанин") && Susanin->SquadRole == EOperativeRole::Recruit
				&& Susanin->MedkitsCount == 0 && Susanin->CannedFoodCount == 1, TEXT("identity and civilian kit"));
			Check(State, Dialogue->IsDialogueOpen() && Dialogue->GetCurrentSequence() && Dialogue->GetCurrentSequence()->Title == TEXT("Сигнал бедствия: Иван Сусанин"),
				TEXT("distress dialogue open"));
			Check(State, World->GetWorldSettings()->TimeDilation < 0.01f, TEXT("narrative pause"));
			Check(State, Camera->GetFollowTarget() == Susanin, TEXT("camera on Susanin"));
			Dialogue->AdvanceLine();
			Next();
			return true;
		}
		case 2:
			Check(State, !Dialogue->IsDialogueOpen() && FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation, 1.f), TEXT("dialogue closed, time runs"));
			Check(State, Camera->GetFollowTarget() == Leader, TEXT("camera back on the leader"));
			Check(State, HasMessage(World, TEXT("ШТАБ"), TEXT("Сусанин замерзает на рубеже")), TEXT("HQ line"));
			Check(State, World->GetSubsystem<UMissionSubsystem>()->GetObjective().ToString() == TEXT("Спасти Сусанина: подойти к нему бойцом отряда!"),
				World->GetSubsystem<UMissionSubsystem>()->GetObjective().ToString());
			Check(State, !Squad->SetLeaderByIndex(3), TEXT("key 4 does nothing yet"));
			Leader->TeleportTo(Susanin->GetActorLocation() + FVector(200.f, 0.f, 0.f), Leader->GetActorRotation(), false, true);
			Next();
			return true;
		case 3:
			if (!Dialogue->IsDialogueOpen() && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Recruits->IsRecruitmentDialogueActive() && Dialogue->GetCurrentSequence() && Dialogue->GetCurrentSequence()->Title == TEXT("Присоединение к отряду")
				&& Dialogue->GetCurrentSequence()->CustomFinishButtonText.Contains(TEXT("Принять в отряд")), TEXT("operative within 2.8 m: recruitment dialogue"));
			Check(State, Susanin->GetActorForwardVector().Dot((Leader->GetActorLocation() - Susanin->GetActorLocation()).GetSafeNormal2D()) > 0.9f,
				TEXT("Susanin faces the rescuer"));
			Dialogue->AdvanceLine();
			Next();
			return true;
		case 4:
			Check(State, Susanin->bRecruited && Squad->GetMembers().Num() == 4 && Squad->GetMembers()[3] == Susanin && Susanin->ColdLevel < 5.f,
				FString::Printf(TEXT("recruited: member 4, warm (cold %.1f)"), Susanin->ColdLevel));
			Check(State, HasMessage(World, TEXT("Иван Сусанин"), TEXT("Я с вами")) && HasMessage(World, TEXT("ШТАБ"), TEXT("принят в боевой отряд")), TEXT("radio lines"));
			Check(State, World->GetSubsystem<UMissionSubsystem>()->GetObjective().ToString().StartsWith(TEXT("ОБОРОНА: Отразить волну 1")),
				World->GetSubsystem<UMissionSubsystem>()->GetObjective().ToString());
			Check(State, Squad->SetLeaderByIndex(3) && Squad->GetLeader() == Susanin, TEXT("key 4 selects Susanin"));
			Squad->SetLeaderByIndex(0);
			Recruits->RestoreRecruited(false);
			Check(State, !Susanin->bRecruited && Squad->GetMembers().Num() == 3, TEXT("load (not recruited): out of the squad"));
			Recruits->RestoreRecruited(true);
			Check(State, Susanin->bRecruited && Squad->GetMembers().Num() == 4 && Recruits->GetSusanin() == Susanin, TEXT("load (recruited): back in the squad"));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SusaninSmoke"),
		TEXT("Dev check: Susanin rescue event, distress / recruitment dialogues, joining as member 4, load toggles; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
