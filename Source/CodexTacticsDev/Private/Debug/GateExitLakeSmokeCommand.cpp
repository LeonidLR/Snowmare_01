// Dev-only check of the level end through the fully opened blast gate on the Demo 01 lake (game designer 2026-10-22: "once all three
// operatives cross the gate line the screen fades to black"; level JSON "gate_exit": "auto", GateExitRules / ULevelExitSubsystem):
//   Scripts/smoke_parallel.ps1 -Commands GateExitLakeSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   review clip (rendered, in-engine frames only): UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen
//       -ResX=1280 -ResY=720 -nosound -benchmark -fps=30 -LevelJson=L_Demo01_A_FrozenLake.json -ExecCmds="CodexTactics.GateExitLakeSmoke Clip"
//       -> Saved/Screenshots/GateExit/frames/f_NNNN.png (a fixed camera outside the door looking back at it)
//  0. the lake: one blast door (generator_waves), the exit enabled, not armed; the A4 zombies parked, the tutorial skipped (its cards
//     and locks are not part of this check), the beat checkpoints off;
//  1. the quest chain done through the dev shortcut (UQuestSubsystem::RestoreState: jerrycan, diesel, generator, console) - the door
//     only unlocks; the squad in the yard; the sudden waves 1..3 started and killed: between waves the panel ("next wave"), after the
//     LAST wave no panel / button - straight to post-combat (2026-10-22);
//  2. the generator is up: the door opens fully after the last wave; the exit arms only once the leaves are fully open (never while
//     they slide); the exit line's outward side is the road (the gate's +X, away from the yard's CombatStart);
//  3. the squad 8 m inside; the leader and the second run out through the doorway, the third holds (guard) - two of three out: no
//     fade, the objective "1/3", "2/3"; then the third runs out: the fade starts exactly when he crosses, the mission is complete,
//     the overlay goes black, the title fades in. A fade before the third is out fails the check.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Interactables/GateActor.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Quests/QuestChain.h"
#include "Quests/QuestSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/MinimapSubsystem.h"
#include "UI/VictoryPanelWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UnrealClient.h"

namespace GateExitLakeSmoke
{
	constexpr float StepSeconds = 0.1f;

	enum class EStage : uint8
	{
		Setup,
		Waves,
		GateOpening,
		Position,
		FirstTwo,
		Third,
		Fade,
		Tail
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		bool bClip = false;
		int32 LastWave = 0;
		bool bPanelBetweenWaves = false;
		bool bSawTwoOutNoFade = false;
		bool bFirstTwoOrdered = false;
		bool bArmedWhileSliding = false;
		bool bSawSliding = false;
		float TwoOutAt = -1.f;
		TWeakObjectPtr<AGateActor> Gate;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Order;
		TArray<FVector> OutTargets;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector CameraEye = FVector::ZeroVector;
		FVector CameraLook = FVector::ZeroVector;
		/** Clip frames (OnEndFrame while recording). */
		bool bRecording = false;
		int32 FrameCount = 0;
		FString FrameDir;
		FDelegateHandle FrameHandle;
		TWeakObjectPtr<UWorld> World;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Next(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (State.FrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
			State.FrameHandle.Reset();
		}
		if (State.bClip)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[GateExit] clip: %d frames in %s"), State.FrameCount, *State.FrameDir);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GateExitLakeSmoke"));
		return false;
	}

	/** The nearest navmesh point to Point (a few rings), or Point itself. */
	FVector OnNav(UWorld* World, const FVector& Point)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (const float Radius : { 0.f, 100.f, 200.f, 350.f })
		{
			for (int32 Step = 0; Step < (Radius <= 0.f ? 1 : 8); ++Step)
			{
				FNavLocation Found;
				const FVector Candidate = Point + FVector(Radius, 0.f, 0.f).RotateAngleAxis(Step * 45.f, FVector::UpVector);
				if (Nav && Nav->ProjectPointToNavigation(Candidate, Found, FVector(50.f, 50.f, 400.f)))
				{
					return Found.Location;
				}
			}
		}
		return Point;
	}

	/** Kills the running wave's enemies (late spawns too). */
	void KillWave(UWorld* World)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		for (AEnemyCharacter* Enemy : Waves ? Waves->GetAliveEnemies() : TArray<AEnemyCharacter*>())
		{
			if (Enemy && !Enemy->IsDying())
			{
				FDamageSpec Spec;
				Spec.Amount = 100000.f;
				Spec.AttackerSource = TEXT("Commander");
				Enemy->GetHealthComponent()->TakeDamage(Spec);
			}
		}
	}

	/** Clip runs: the camera outside the door looking back at it; one frame per rendered frame while recording. */
	void OnEndFrame(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC || !State->bClip || State->CameraEye.IsZero())
		{
			return; // the camera is placed once the door is open
		}
		ACameraActor* Camera = State->Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(State->CameraEye, (State->CameraLook - State->CameraEye).Rotation());
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(52.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
			State->Camera = Camera;
		}
		if (Camera)
		{
			Camera->SetActorLocationAndRotation(State->CameraEye, (State->CameraLook - State->CameraEye).Rotation());
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
		if (State->bRecording)
		{
			FScreenshotRequest::RequestScreenshot(State->FrameDir / FString::Printf(TEXT("f_%04d.png"), State->FrameCount++), /*bShowUI*/ true, false);
		}
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
		UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Quests || !Exit || !Mission || !Victory || !Leader)
		{
			if (State.Time > 120.f)
			{
				Check(State, false, TEXT("squad / flow / quests / exit / mission subsystems in the world"));
				return Finish(State, false);
			}
			return true;
		}
		if (State.Time > 420.f || (State.Stage != EStage::Setup && State.StageTime > 90.f))
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		// Never a fade while anyone alive is still inside.
		if (Exit->IsExiting() && Exit->GetCrossedCount() < Exit->GetRequiredCount())
		{
			Check(State, false, FString::Printf(TEXT("the fade began with %d of %d out"), Exit->GetCrossedCount(), Exit->GetRequiredCount()));
			return Finish(State, false);
		}
		AGateActor* Gate = State.Gate.Get();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation Probe;
			if ((Squad->GetMembers().Num() < 3 || !(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), Probe, FVector(60.f, 60.f, 200.f))))
				&& State.StageTime < 120.f)
			{
				return true;
			}
			if (State.StageTime < 3.f)
			{
				return true; // the far end's navmesh builds a moment longer
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false;
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial(); // this run only: the cards / locks are the tutorial smokes' business
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->SetActorTickEnabled(false);
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			int32 Gates = 0;
			for (TActorIterator<AGateActor> It(World); It; ++It)
			{
				++Gates;
				State.Gate = Gate = *It;
			}
			const FGateExitConfig Config = Exit->GetConfig();
			Check(State, Gates == 1 && Gate && Gate->UsesGeneratorPacing() && !Gate->IsOpen(), FString::Printf(TEXT("one shut blast door paced by the generator (%d)"), Gates));
			Check(State, Config.bEnabled && Config.Side == EGateExitSide::Auto, TEXT("the lake's level JSON: gate_exit auto"));
			Check(State, !Exit->IsArmed() && !Exit->IsExiting(), TEXT("the shut door: the exit is not armed"));
			if (!Gate)
			{
				return Finish(State, false);
			}
			// The quest chain through the dev shortcut (the click path is LakeQuestSmoke's): the console only unlocks the paced door.
			FQuestChainState Done;
			Done.bHasEmptyCanister = true;
			Done.bHasFuelCanister = true;
			Done.bIsGeneratorRunning = true;
			Done.bIsGatePowered = true;
			Quests->RestoreState(Done);
			Check(State, Gate->IsProgressUnlocked() && !Gate->IsOpen() && Gate->IsGeneratorPowering(), TEXT("chain done: door unlocked, generator up, still shut"));
			// The squad in the yard (CombatStart), the fight on (the console's wave 1).
			const AActor* CombatStart = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				CombatStart = It->ActorHasTag(Mission->CombatStartTag) ? *It : CombatStart;
			}
			if (CombatStart)
			{
				int32 Slot = 0;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					const FVector Spot = OnNav(World, CombatStart->GetActorLocation() + FVector(0.f, (Slot++ - 1) * 180.f, 0.f));
					Member->StopOperative();
					Member->TeleportTo(Spot + FVector(0.f, 0.f, 95.f), Member->GetActorRotation(), false, true);
				}
			}
			Check(State, Flow->TriggerCombatZone() == EGameFlowResult::Ok, TEXT("the fight starts (sudden wave 1)"));
			Next(State, EStage::Waves);
			return true;
		}
		case EStage::Waves:
		{
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				if (Flow->IsAwaitingHordeIntro() || Flow->IsFreePause())
				{
					SmokeUtils::ReleaseHordeIntroPause(Flow);
				}
				State.LastWave = Flow->GetWaveIndex();
				KillWave(World);
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				if (State.StageTime < 0.3f)
				{
					return true;
				}
				const APlayerController* PC = World->GetFirstPlayerController();
				const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
				const UVictoryPanelWidget* Panel = Hud ? Hud->GetVictoryPanel() : nullptr;
				const bool bLast = Flow->GetWaveIndex() >= Flow->GetConfig().TotalWaves;
				Check(State, !bLast && (!Panel || Panel->IsShown()), FString::Printf(TEXT("wave %d of %d cleared: the between-waves panel (\"next wave\")"),
					Flow->GetWaveIndex(), Flow->GetConfig().TotalWaves));
				State.bPanelBetweenWaves = true;
				Victory->ContinueAfterWave();
				State.StageTime = 0.f;
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::PostCombat)
			{
				Check(State, State.LastWave == Flow->GetConfig().TotalWaves && State.LastWave == 3 && State.bPanelBetweenWaves,
					FString::Printf(TEXT("the last wave (%d of %d) went straight to post-combat - no panel, no \"End combat\" button"), State.LastWave,
						Flow->GetConfig().TotalWaves));
				Next(State, EStage::GateOpening);
			}
			return true;
		}
		case EStage::GateOpening:
		{
			if (Gate->IsOpening())
			{
				State.bSawSliding = true;
				State.bArmedWhileSliding |= Exit->IsArmed();
			}
			if (!Gate->IsOpen() || !Exit->IsArmed())
			{
				return true;
			}
			Check(State, State.bSawSliding && !State.bArmedWhileSliding, TEXT("the door opens after the last wave (generator up); the exit arms only once it is FULLY open"));
			FGateExitLine Line;
			Exit->GetExitLine(Line);
			const FVector GateX = Gate->GetActorForwardVector().GetSafeNormal2D();
			Check(State, FVector::DotProduct(Line.Outward, GateX) > 0.9f, FString::Printf(TEXT("out = the road (the gate's +X, away from the yard): %s"),
				*Line.Outward.ToCompactString()));
			Check(State, Mission->GetObjective().ToString().Contains(TEXT("0/3")), FString::Printf(TEXT("objective: \"%s\""), *Mission->GetObjective().ToString()));
			Next(State, EStage::Position);
			return true;
		}
		case EStage::Position:
		{
			if (State.StageTime < 1.f)
			{
				return true; // the navmesh rebuilds round the opened leaves
			}
			FGateExitLine Line;
			Exit->GetExitLine(Line);
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			// 8 m inside, spread along the wall; the commander first.
			const float Lateral[] = { -170.f, 170.f, 0.f };
			const float Back[] = { 800.f, 820.f, 1000.f };
			for (int32 Index = 0; Index < Members.Num() && Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				const FVector Spot = OnNav(World, Line.Origin - Line.Outward * Back[Index] + Line.Along * Lateral[Index]);
				Member->StopOperative();
				Member->TeleportTo(Spot + FVector(0.f, 0.f, 95.f), Line.Outward.Rotation(), false, true);
				State.Order.Add(Member);
				// 7 m out, on the far side of the doorway from the review camera.
				const float OutLateral[] = { -260.f, -40.f, -150.f };
				State.OutTargets.Add(OnNav(World, Line.Origin + Line.Outward * 700.f + Line.Along * OutLateral[Index]));
			}
			Squad->SetLeader(Members[0]);
			Squad->ToggleGuard(Members[2]); // the third holds his spot inside
			// The review camera: outside, high and to the side, looking back at the doorway (the yard behind it in frame).
			State.CameraEye = Line.Origin + Line.Outward * 2000.f + Line.Along * 1000.f + FVector(0.f, 0.f, 950.f);
			State.CameraLook = Line.Origin + Line.Outward * 100.f + FVector(0.f, 0.f, 20.f);
			if (UMinimapSubsystem* Minimap = State.bClip ? UMinimapSubsystem::Get(World) : nullptr)
			{
				Minimap->SetPanelMode(EMinimapPanelMode::Map); // the clip shows the scene, not the message log over half of it
			}
			for (int32 Index = 0; Index < State.OutTargets.Num(); ++Index)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[GateExit] %s at %s -> out target %s (%.0f cm beyond)"), *State.Order[Index]->DisplayName.ToString(),
					*State.Order[Index]->GetActorLocation().ToCompactString(), *State.OutTargets[Index].ToCompactString(),
					GateExitRules::SignedDistance(Line, State.OutTargets[Index]));
			}
			Check(State, State.Order.Num() == 3 && Exit->GetRequiredCount() == 3, FString::Printf(TEXT("three living operatives must cross (%d)"), Exit->GetRequiredCount()));
			if (State.bClip)
			{
				State.bRecording = true;
			}
			Next(State, EStage::FirstTwo);
			return true;
		}
		case EStage::FirstTwo:
		{
			if (State.StageTime < 1.2f)
			{
				return true; // the establishing shot: the open door, the squad inside
			}
			AOperativeCharacter* First = State.Order[0].Get();
			AOperativeCharacter* Second = State.Order[1].Get();
			AOperativeCharacter* Third = State.Order[2].Get();
			if (!First || !Second || !Third)
			{
				Check(State, false, TEXT("the three operatives"));
				return Finish(State, false);
			}
			if (!State.bFirstTwoOrdered)
			{
				State.bFirstTwoOrdered = true;
				Check(State, First->OrderMoveTo(State.OutTargets[0], true) == EOperativeOrderResult::Accepted
					&& Second->OrderMoveTo(State.OutTargets[1], true) == EOperativeOrderResult::Accepted, TEXT("the leader and the second run out"));
				return true;
			}
			if (Exit->GetCrossedCount() == 1 && Mission->GetObjective().ToString().Contains(TEXT("1/3")))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[GateExit] objective \"%s\""), *Mission->GetObjective().ToString());
			}
			if (!(Exit->HasCrossed(First) && Exit->HasCrossed(Second)))
			{
				return true;
			}
			if (State.TwoOutAt < 0.f)
			{
				State.TwoOutAt = State.StageTime;
			}
			const bool bHolding = !Exit->HasCrossed(Third) && !Exit->IsExiting() && Exit->GetCrossedCount() == 2 && Exit->GetRequiredCount() == 3;
			State.bSawTwoOutNoFade |= bHolding;
			if (State.StageTime < State.TwoOutAt + 0.8f)
			{
				return true;
			}
			Check(State, State.bSawTwoOutNoFade && !Exit->IsExiting() && !Mission->IsMissionComplete() && Mission->GetObjective().ToString().Contains(TEXT("2/3")),
				FString::Printf(TEXT("two of three out, the third inside: no fade, mission not over (\"%s\")"), *Mission->GetObjective().ToString()));
			Squad->ToggleGuard(Third);
			Check(State, Third->OrderMoveTo(State.OutTargets[2], true) == EOperativeOrderResult::Accepted, TEXT("the third runs out"));
			Next(State, EStage::Third);
			return true;
		}
		case EStage::Third:
		{
			AOperativeCharacter* Third = State.Order[2].Get();
			if (!Exit->IsExiting())
			{
				if (Third && Exit->HasCrossed(Third))
				{
					Check(State, false, TEXT("the third is out but the level did not end"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, Third && Exit->HasCrossed(Third) && Exit->GetCrossedCount() == 3 && Exit->GetRequiredCount() == 3,
				FString::Printf(TEXT("the fade starts when the third crosses (%d / %d out, %.1f s after his order)"), Exit->GetCrossedCount(),
					Exit->GetRequiredCount(), State.StageTime));
			Check(State, Mission->IsMissionComplete() && !Mission->IsMissionFailed() && Mission->GetObjective().ToString().Contains(TEXT("3/3")),
				TEXT("the mission is complete (3/3 out)"));
			Next(State, EStage::Fade);
			return true;
		}
		case EStage::Fade:
		{
			if (Exit->GetFadeAlpha() < 1.f)
			{
				return true;
			}
			Check(State, State.StageTime >= Exit->GetConfig().FadeSeconds - 0.3f, FString::Printf(TEXT("black after %.1f s (fade %.1f s)"), State.StageTime,
				Exit->GetConfig().FadeSeconds));
			Next(State, EStage::Tail);
			return true;
		}
		case EStage::Tail:
		{
			if (State.StageTime < 2.2f)
			{
				return true;
			}
			Check(State, Exit->GetTitleAlpha() >= 1.f && Exit->GetFadeAlpha() >= 1.f, TEXT("black screen, \"MISSION COMPLETE\" shown"));
			Check(State, Mission->IsHeadlessStart() && World->GetName().Contains(TEXT("FrozenLake")), TEXT("a headless check stays on the black screen (no menu travel)"));
			return Finish(State, true);
		}
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->bClip = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Clip"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		if (State->bClip)
		{
			State->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("GateExit") / TEXT("frames"));
			IFileManager::Get().DeleteDirectory(*State->FrameDir, false, true);
			IFileManager::Get().MakeDirectory(*State->FrameDir, true);
			State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { OnEndFrame(State); });
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GateExitLakeSmoke"),
		TEXT("Dev check (2026-10-22): the Demo 01 lake ends through the fully opened blast door - waves cleared (no end-of-fight button), the door opens, "
			"two of three out = no fade, the third out = fade to black + mission complete. 'Clip' = rendered frames into Saved/Screenshots/GateExit/frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
