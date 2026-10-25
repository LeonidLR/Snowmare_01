// Dev-only check of the level end through the fully opened blast gate on the Demo 01 lake (game designer 2026-10-22: "once all three
// operatives cross the gate line the screen fades to black"; level JSON "gate_exit": "auto", GateExitRules / ULevelExitSubsystem):
//   Scripts/smoke_parallel.ps1 -Commands GateExitLakeSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   review clip (rendered, in-engine frames only): UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen
//       -ResX=1280 -ResY=720 -nosound -benchmark -fps=30 -LevelJson=L_Demo01_A_FrozenLake.json -ExecCmds="CodexTactics.GateExitLakeSmoke Clip"
//       -> Saved/Screenshots/GateExit/frames/f_NNNN.png (a fixed camera outside the door looking back at it)
//  0. the lake: one blast door (generator_waves), the exit enabled, not armed; the A4 zombies parked, the tutorial skipped (its cards
//     and locks are not part of this check), the beat checkpoints off;
//  1. the quest chain done through the dev shortcut (UQuestSubsystem::RestoreState: jerrycan, diesel, generator, console) - the door
//     only unlocks; the squad in the yard; the sudden waves 1..3 started and killed: between waves NO panel (2026-10-25) - the rest
//     runs out and the next wave breaks through on its own (nobody clicks), after the LAST wave no panel / button - straight to
//     post-combat (2026-10-22);
//  2. the generator is up: the door opens fully after the last wave; the exit arms only once the leaves are fully open (never while
//     they slide); the exit line's outward side is the road (the gate's +X, away from the yard's CombatStart);
//  3. the squad 8 m inside; the leader and the second run out through the doorway, the third holds (guard) - two of three out: no
//     fade, the objective "1/3", "2/3"; then the third runs out: the fade starts exactly when he crosses, the mission is complete,
//     the overlay goes black, the title fades in. A fade before the third is out fails the check.
//  4. (2026-10-22 b, the campaign) a headless check stays on the black screen; then (SetLeaveInHeadless) the black screen ends: the
//     Commander (3) and the Medic-Sapper (2) have unspent points, the Engineer none -> the stat-points screen with exactly those two
//     (title "0.0 FROZEN DAM", the next level named, the character image / role letter follows the sheet, "Continue >"), one point
//     spent on the Commander's accuracy through the sheet, the Medic's left; the "Continue >" button -> the next campaign level
//     /Game/Maps/L_Demo01_B_FrozenFactory opens (its own level JSON, not the run's -LevelJson); the squad stands outside the open
//     lake door (set dressing) facing away from it, with the carried level / EXP / points / stats / kit and full health; the start fades
//     in from black with "0.1 Frozen Factory".
//  CodexTactics.GateExitLakeNoPointsSmoke: the same run with no unspent points -> no points screen, the factory opens at once.
//  CodexTactics.GateExitLakeKiaSmoke / GateExitLakeKiaNoPointsSmoke (bug 2026-10-23, "two crossed the gate line - the third was dead - and the
//  level got stuck on the black screen"): the Engineer is killed in the yard before the fight; the two living cross (the first, then the
//  second), the black screen leaves to the points screen (Commander + Medic-Sapper) / the factory at once; the fallen is handed over as
//  killed in action and is not spawned on the factory. (The designer's real cause - a save loaded in editor play counted as a headless
//  check - is the rule test CodexTactics.Mission.Rules.StartModeAndMenu: MissionRules::IsAutomatedRun.)

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Debug/FrontendSmokeUtils.h"
#include "GameFlow/CampaignSubsystem.h"
#include "UI/PointsReviewWidget.h"
#include "UI/ProfileDialogWidget.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
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
		Tail,
		Leave,
		PointsScreen,
		Travel,
		Arrival
	};

	/** What an operative carries out of the lake (checked again on the next level). */
	struct FCarryExpect
	{
		EOperativeRole Role = EOperativeRole::Commander;
		int32 Level = 1;
		int32 Exp = 0;
		int32 Points = 0;
		float Accuracy = 0.f;
		float Luck = 0.f;
		float MaxHealth = 0.f;
		int32 Medkits = 0;
		int32 Grenades = 0;
		int32 HeatPacks = 0;
		int32 RifleReserve = 0;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		bool bClip = false;
		/** GateExitLakeNoPointsSmoke: nobody has points left -> the next level at once. */
		bool bNoPoints = false;
		/** GateExitLakeKia*Smoke: the Engineer falls before the fight; two living operatives leave. */
		bool bKia = false;
		bool bKiaDone = false;
		/** The living operatives that must cross. */
		int32 Living = 3;
		/** 'Shot': one rendered frame of the points screen. */
		bool bShot = false;
		bool bShotTaken = false;
		TArray<FCarryExpect> Expect;
		TWeakObjectPtr<UGameInstance> GameInstance;
		float FirstArrivalFade = -1.f;
		float FirstArrivalRealTime = -1.f;
		int32 LastWave = 0;
		/** Rests between waves that ran out by themselves (2026-10-25: no "next wave" panel). */
		int32 RestsRunOut = 0;
		bool bInRest = false;
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

	/** 2026-10-22 b: what every operative carries out of the lake (read again on the next level). */
	void RecordCarry(FState& State, const USquadSubsystem* Squad)
	{
		State.Expect.Reset();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			FCarryExpect& Expect = State.Expect.AddDefaulted_GetRef();
			Expect.Role = Member->SquadRole;
			Expect.Level = Member->Level;
			Expect.Exp = Member->CurrentExp;
			Expect.Points = Member->UnspentStatPoints;
			Expect.Accuracy = Member->Accuracy;
			Expect.Luck = Member->Luck;
			Expect.MaxHealth = Member->GetStatValue(EProgressStat::Health);
			Expect.Medkits = Member->MedkitsCount;
			Expect.Grenades = Member->GrenadesCount;
			Expect.HeatPacks = Member->HeatPacksCount;
			Expect.RifleReserve = Member->GetAmmoState(TEXT("m16")).Reserve;
		}
	}

	AOperativeCharacter* FindRole(const USquadSubsystem* Squad, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	/** A button of the character sheet by its widget name (BtnPrev / BtnClose / BtnNext). */
	UButton* FindSheetButton(const UProfileDialogWidget* Profile, const TCHAR* Name)
	{
		return Profile && Profile->WidgetTree ? Cast<UButton>(Profile->WidgetTree->FindWidget(FName(Name))) : nullptr;
	}

	/** The stat-points screen: the two operatives with points, one point spent through the sheet, "Continue >". */
	bool StepPointsScreen(FState& State, UWorld* World, ULevelExitSubsystem* Exit)
	{
		UPointsReviewWidget* Screen = Exit->GetPointsScreen();
		UProfileDialogWidget* Profile = Screen ? Screen->GetProfile() : nullptr;
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = FindRole(Squad, EOperativeRole::Commander);
		AOperativeCharacter* Medic = FindRole(Squad, EOperativeRole::MedicSapper);
		if (!Exit->IsPointsScreenShown() || !Profile || !Commander || !Medic)
		{
			Check(State, false, TEXT("the stat-points screen is up with the character sheet"));
			return Finish(State, false);
		}
		if (State.StageTime < StepSeconds * 1.5f)
		{
			Check(State, Profile->IsReviewMode() && Profile->GetReviewCount() == 2 && Screen->GetShownMember() == Commander,
				FString::Printf(TEXT("the points screen shows exactly the two with points, the Commander first (%d on the sheet)"), Profile->GetReviewCount()));
			Check(State, Screen->GetTitleText().Contains(TEXT("0.0 FROZEN DAM")) && Screen->GetHintText().Contains(TEXT("0.1 Frozen Factory")),
				FString::Printf(TEXT("titles: \"%s\" / \"%s\""), *Screen->GetTitleText(), *Screen->GetHintText()));
			Check(State, Profile->GetCloseButtonText().Contains(TEXT("Continue")), FString::Printf(TEXT("the sheet's button reads \"%s\""), *Profile->GetCloseButtonText()));
			Check(State, Screen->HasPortraitImage() == FApp::CanEverRender() && (!Screen->HasPortraitImage() || Screen->GetPortraitTarget() == Commander),
				FString::Printf(TEXT("the character image: %s"), Screen->HasPortraitImage() ? TEXT("live portrait of the Commander") : TEXT("role letter (nothing renders)")));
			const float AccuracyBefore = Commander->Accuracy;
			Profile->ClickStat(EProgressStat::Accuracy, 1);
			Check(State, FMath::IsNearlyEqual(Commander->Accuracy, AccuracyBefore + 1.f) && Commander->UnspentStatPoints == 2,
				FString::Printf(TEXT("one point into the Commander's accuracy through the sheet (%.0f -> %.0f, %d left)"), AccuracyBefore, Commander->Accuracy,
					Commander->UnspentStatPoints));
			UButton* NextButton = FindSheetButton(Profile, TEXT("BtnNext"));
			if (NextButton)
			{
				NextButton->OnClicked.Broadcast();
			}
			Check(State, NextButton && Screen->GetShownMember() == Medic, TEXT("Next > turns to the Medic-Sapper (her 2 points stay unspent)"));
			return true;
		}
		if (State.bShot && FApp::CanEverRender())
		{
			if (!State.bShotTaken && State.StageTime >= 1.5f)
			{
				State.bShotTaken = true;
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("GateExit") / TEXT("points_screen.png"));
				FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, false);
				UE_LOG(LogCodexTactics, Display, TEXT("[GateExit] shot: %s"), *Path);
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
		}
		Check(State, !Screen->HasPortraitImage() || Screen->GetPortraitTarget() == Medic, TEXT("the character image follows the sheet"));
		RecordCarry(State, Squad); // the spent point is part of the carry
		UButton* Continue = FindSheetButton(Profile, TEXT("BtnClose"));
		if (Continue)
		{
			Continue->OnClicked.Broadcast();
		}
		Check(State, Continue && Screen->HasContinued() && Exit->IsTravelRequested(), TEXT("\"Continue >\" (points left unspent) opens the next level"));
		const UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World);
		const FCampaignSquadCarry* Carry = Campaign ? &Campaign->GetCarry() : nullptr;
		const int32 Fallen = Carry ? Carry->Members.FilterByPredicate([](const FCarriedOperative& Member) { return Member.bKilledInAction; }).Num() : -1;
		Check(State, Carry && Carry->ToMap == TEXT("L_Demo01_B_FrozenFactory") && Carry->FromMap == TEXT("L_Demo01_A_FrozenLake") && Carry->Members.Num() == 3
			&& Fallen == 3 - State.Living,
			FString::Printf(TEXT("the squad handed over: %s -> %s, %d operatives (%d killed in action)"), Carry ? *Carry->FromMap : TEXT("-"), Carry ? *Carry->ToMap : TEXT("-"),
				Carry ? Carry->Members.Num() : 0, Fallen));
		Next(State, EStage::Travel);
		return true;
	}

	/** The next level: the factory opened, the squad outside the open lake door with what it carried. */
	bool StepArrival(FState& State)
	{
		if (State.Time > 600.f)
		{
			Check(State, false, TEXT("time out after the travel"));
			return Finish(State, false);
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		const bool bFactory = World && World != State.World.Get() && World->GetMapName().Contains(TEXT("L_Demo01_B_FrozenFactory"));
		USquadSubsystem* Squad = bFactory ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		ULevelExitSubsystem* Exit = bFactory ? World->GetSubsystem<ULevelExitSubsystem>() : nullptr;
		if (State.Stage == EStage::Travel)
		{
			if (State.StageTime > 120.f)
			{
				Check(State, false, TEXT("the next level (L_Demo01_B_FrozenFactory) opened"));
				return Finish(State, false);
			}
			if (!Squad || !Exit || Squad->GetMembers().Num() < State.Living || !Exit->IsStartFadeResolved())
			{
				return true;
			}
			State.FirstArrivalFade = Exit->GetStartFadeAlpha();
			State.FirstArrivalRealTime = World->GetRealTimeSeconds();
			Check(State, true, FString::Printf(TEXT("the next level opened: %s (%.1f s after Continue)"), *World->GetMapName(), State.StageTime));
			Check(State, Squad->GetMembers().Num() == State.Living && (!State.bKia || !FindRole(Squad, EOperativeRole::Engineer)),
				FString::Printf(TEXT("the factory squad: %d living operatives%s"), Squad->GetMembers().Num(), State.bKia ? TEXT(" (the fallen Engineer stays behind)") : TEXT("")));
			Next(State, EStage::Arrival);
			return true;
		}
		if (!Squad || !Exit)
		{
			Check(State, false, TEXT("the factory world stays"));
			return Finish(State, false);
		}
		if (State.bShot && FApp::CanEverRender())
		{
			// 'Shot': the arrival under the fading black, then the start view (the door behind the squad).
			const TCHAR* Frame = State.StageTime >= 0.5f && State.StageTime < 0.6f ? TEXT("factory_start_fade.png")
				: (State.StageTime >= 3.5f && State.StageTime < 3.6f ? TEXT("factory_start.png") : nullptr);
			if (Frame)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("GateExit") / Frame), true, false);
			}
			if (State.StageTime < 4.f)
			{
				return true;
			}
		}
		if (State.StageTime < 1.f)
		{
			return true;
		}
		Check(State, FMath::IsNearlyEqual(Exit->GetStartFadeSeconds(), 2.f) && Exit->GetStartTitle() == TEXT("0.1 Frozen Factory")
			&& (State.FirstArrivalRealTime > CampaignRules::StartHoldSeconds || State.FirstArrivalFade >= 0.99f),
			FString::Printf(TEXT("the start fades in from black (%.1f s, \"%s\", alpha %.2f at %.2f s)"), Exit->GetStartFadeSeconds(), *Exit->GetStartTitle(),
				State.FirstArrivalFade, State.FirstArrivalRealTime));
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Check(State, Flow && Flow->GetConfig().TotalWaves == 1, FString::Printf(TEXT("the factory's own level JSON (1 wave; the run's -LevelJson is the lake's): %d"),
			Flow ? Flow->GetConfig().TotalWaves : -1));
		AGateActor* Dressing = nullptr;
		for (TActorIterator<AGateActor> It(World); It; ++It)
		{
			Dressing = It->bSetDressing ? *It : Dressing;
		}
		Check(State, Dressing && !Dressing->IsOpen() && AGateActor::FindLevelGate(World) != Dressing, TEXT("the lake's blast door stands CLOSED behind the start (set dressing, not the level's gate; user decision 2026-10-22: no way back)"));
		if (!Dressing)
		{
			return Finish(State, false);
		}
		const FVector Out = Dressing->GetActorForwardVector().GetSafeNormal2D();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FCarryExpect* Expect = State.Expect.FindByPredicate([Member](const FCarryExpect& E) { return E.Role == Member->SquadRole; });
			const UHealthComponent* Health = Member->HealthComponent;
			Check(State, Expect && Member->Level == Expect->Level && Member->CurrentExp == Expect->Exp && Member->UnspentStatPoints == Expect->Points
				&& FMath::IsNearlyEqual(Member->Accuracy, Expect->Accuracy) && FMath::IsNearlyEqual(Member->Luck, Expect->Luck)
				&& FMath::IsNearlyEqual(Member->GetStatValue(EProgressStat::Health), Expect->MaxHealth),
				FString::Printf(TEXT("%s carried: level %d, EXP %d, %d points, accuracy %.0f, luck %.0f, max HP %.0f"), *Member->DisplayName.ToString(),
					Member->Level, Member->CurrentExp, Member->UnspentStatPoints, Member->Accuracy, Member->Luck, Member->GetStatValue(EProgressStat::Health)));
			Check(State, Expect && Member->MedkitsCount == Expect->Medkits && Member->GrenadesCount == Expect->Grenades && Member->HeatPacksCount == Expect->HeatPacks
				&& Member->GetAmmoState(TEXT("m16")).Reserve == Expect->RifleReserve,
				FString::Printf(TEXT("%s kit: %d medkits, %d grenades, %d heat packs, M16 reserve %d"), *Member->DisplayName.ToString(), Member->MedkitsCount,
					Member->GrenadesCount, Member->HeatPacksCount, Member->GetAmmoState(TEXT("m16")).Reserve));
			Check(State, Health && FMath::IsNearlyEqual(Health->GetCurrentHealth(), Health->GetMaxHealth()) && Member->ColdLevel < 2.f * State.StageTime + 3.f,
				FString::Printf(TEXT("%s arrives at full health (%.0f / %.0f), warm (cold %.2f %.1f s after the start: no carried cold)"), *Member->DisplayName.ToString(),
					Health ? Health->GetCurrentHealth() : -1.f, Health ? Health->GetMaxHealth() : -1.f, Member->ColdLevel, State.StageTime));
			const float Beyond = FVector::DotProduct(Member->GetActorLocation() - Dressing->GetActorLocation(), Out);
			const float Facing = FVector::DotProduct(Member->GetActorForwardVector().GetSafeNormal2D(), Out);
			Check(State, Beyond > 150.f && Beyond < 1500.f && Facing > 0.7f, FString::Printf(TEXT("%s stands %.0f cm outside the open door, facing away from it (%.2f)"),
				*Member->DisplayName.ToString(), Beyond, Facing));
		}
		return Finish(State, true);
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
		if (State.Stage == EStage::Travel || State.Stage == EStage::Arrival)
		{
			return StepArrival(State);
		}
		UWorld* World = State.World.Get();
		if (!World && State.Stage == EStage::Leave && State.bNoPoints)
		{
			// No points: the exit opened the next level within the same frames (no screen waits for a click) - the lake is gone already.
			const UCampaignSubsystem* Campaign = State.GameInstance.IsValid() ? State.GameInstance->GetSubsystem<UCampaignSubsystem>() : nullptr;
			Check(State, Campaign && Campaign->GetCarry().ToMap == TEXT("L_Demo01_B_FrozenFactory")
				&& !Campaign->GetCarry().Members.ContainsByPredicate([](const FCarriedOperative& Member) { return Member.UnspentStatPoints > 0; }),
				TEXT("no unspent points: no points screen, the next level opened at once (squad handed over)"));
			Next(State, EStage::Travel);
			return StepArrival(State);
		}
		if (!World)
		{
			Check(State, false, TEXT("the lake world went away before the travel"));
			return Finish(State, false);
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
		if (State.Time > 600.f || (State.Stage != EStage::Setup && State.StageTime > 90.f))
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
			if ((Squad->GetMembers().Num() < State.Living || !(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), Probe, FVector(60.f, 60.f, 200.f))))
				&& State.StageTime < 120.f)
			{
				return true;
			}
			if (State.StageTime < 3.f)
			{
				return true; // the far end's navmesh builds a moment longer
			}
			if (State.bKia && !State.bKiaDone)
			{
				State.bKiaDone = true;
				if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
				{
					Tutorial->SkipTutorial(); // a lesson beat would down him instead of killing him
				}
				AOperativeCharacter* Engineer = FindRole(Squad, EOperativeRole::Engineer);
				if (Engineer && Engineer->HealthComponent)
				{
					FDamageSpec Spec;
					Spec.Amount = 100000.f;
					Spec.AttackerSource = TEXT("smoke");
					Engineer->HealthComponent->TakeDamage(Spec);
				}
				State.Living = Squad->GetMembers().Num();
				Check(State, Engineer && Engineer->IsKilledInAction() && State.Living == 2 && !Mission->IsMissionFailed(),
					FString::Printf(TEXT("[KIA] the Engineer is killed in action in the yard: %d living, the mission goes on"), State.Living));
				State.StageTime = 0.f;
				return true;
			}
			if (const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>(); DeathCam && DeathCam->IsActive() && State.StageTime < 20.f)
			{
				return true;
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
				if (State.bInRest)
				{
					State.bInRest = false;
					++State.RestsRunOut;
					Check(State, Flow->GetWaveIndex() == State.LastWave + 1, FString::Printf(TEXT("the rest ran out: wave %d broke through on its own"),
						Flow->GetWaveIndex()));
				}
				State.LastWave = Flow->GetWaveIndex();
				KillWave(World);
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Check(State, false, FString::Printf(TEXT("wave %d stopped on the victory panel (2026-10-25: the next wave comes by itself)"),
					Flow->GetWaveIndex()));
				return Finish(State, false);
			}
			if (Flow->IsRestBetweenWaves())
			{
				if (!State.bInRest)
				{
					const APlayerController* PC = World->GetFirstPlayerController();
					const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
					const UVictoryPanelWidget* Panel = Hud ? Hud->GetVictoryPanel() : nullptr;
					Check(State, (!Panel || !Panel->IsShown()) && Flow->GetWaveIndex() == State.LastWave + 1,
						FString::Printf(TEXT("wave %d of %d cleared: the rest before wave %d (%.0f s), no \"next wave\" panel"), State.LastWave,
							Flow->GetConfig().TotalWaves, Flow->GetWaveIndex(), Flow->GetPreparationTimeRemaining()));
					State.bInRest = true;
				}
				// Nobody clicks: the rest runs out and the wave breaks through on its own.
				return State.StageTime < 120.f || Finish(State, false);
			}
			if (Flow->GetPhase() == ECodexGamePhase::PostCombat)
			{
				Check(State, State.LastWave == Flow->GetConfig().TotalWaves && State.LastWave == 3 && State.RestsRunOut == 2,
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
			Check(State, Mission->GetObjective().ToString().Contains(FString::Printf(TEXT("0/%d"), State.Living)), FString::Printf(TEXT("objective: \"%s\""), *Mission->GetObjective().ToString()));
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
			const int32 Last = Members.Num() - 1; // the one who holds inside (guard) until the others are out
			for (int32 Index = 0; Index < Members.Num() && Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				const int32 Place = Index == Last ? 2 : Index;
				const FVector Spot = OnNav(World, Line.Origin - Line.Outward * Back[Place] + Line.Along * Lateral[Place]);
				Member->StopOperative();
				Member->TeleportTo(Spot + FVector(0.f, 0.f, 95.f), Line.Outward.Rotation(), false, true);
				State.Order.Add(Member);
				// 7 m out, on the far side of the doorway from the review camera.
				const float OutLateral[] = { -260.f, -40.f, -150.f };
				State.OutTargets.Add(OnNav(World, Line.Origin + Line.Outward * 700.f + Line.Along * OutLateral[Place]));
			}
			Squad->SetLeader(Members[0]);
			Squad->ToggleGuard(Members[Last]); // the last holds his spot inside
			// 2026-10-22 b: what the squad carries out - points on the Commander (3) and the Medic-Sapper (2), none on the Engineer
			// (no points at all in the NoPoints run), a level / EXP / kit marker each, the Commander wounded (he arrives healed).
			for (int32 Index = 0; Index < Members.Num() && Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				Member->Level = 2 + Index;
				Member->CurrentExp = 40 + 10 * Index;
				Member->UnspentStatPoints = State.bNoPoints ? 0 : (Member->SquadRole == EOperativeRole::Commander ? 3
					: (Member->SquadRole == EOperativeRole::MedicSapper ? 2 : 0));
				Member->MedkitsCount = 3 + Index;
				Member->GrenadesCount = 1;
				Member->HeatPacksCount = 2 + Index;
				Member->AddAmmo(TEXT("m16"), 77 + Index - Member->GetAmmoState(TEXT("m16")).Reserve); // the rifle in hands keeps its live reserve
			}
			if (Members[0]->HealthComponent)
			{
				Members[0]->HealthComponent->RestoreHealth(Members[0]->HealthComponent->GetMaxHealth() * 0.5f, Members[0]->HealthComponent->GetMaxHealth());
			}
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
			Check(State, State.Order.Num() == State.Living && Exit->GetRequiredCount() == State.Living,
				FString::Printf(TEXT("%d living operatives must cross (%d)"), State.Living, Exit->GetRequiredCount()));
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
			// All but the last run out first (two of three; with the Engineer fallen: one of two).
			const int32 Last = State.Order.Num() - 1;
			AOperativeCharacter* Third = Last > 0 ? State.Order[Last].Get() : nullptr;
			bool bAll = Third != nullptr;
			for (int32 Index = 0; Index < Last; ++Index)
			{
				bAll &= State.Order[Index].IsValid();
			}
			if (!bAll)
			{
				Check(State, false, TEXT("the living operatives"));
				return Finish(State, false);
			}
			if (!State.bFirstTwoOrdered)
			{
				State.bFirstTwoOrdered = true;
				bool bOrdered = true;
				for (int32 Index = 0; Index < Last; ++Index)
				{
					bOrdered &= State.Order[Index]->OrderMoveTo(State.OutTargets[Index], true) == EOperativeOrderResult::Accepted;
				}
				Check(State, bOrdered, Last == 1 ? TEXT("the leader runs out") : TEXT("the leader and the second run out"));
				return true;
			}
			if (Exit->GetCrossedCount() == 1 && Mission->GetObjective().ToString().Contains(FString::Printf(TEXT("1/%d"), State.Living)))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[GateExit] objective \"%s\""), *Mission->GetObjective().ToString());
			}
			bool bFirstOut = true;
			for (int32 Index = 0; Index < Last; ++Index)
			{
				bFirstOut &= Exit->HasCrossed(State.Order[Index].Get());
			}
			if (!bFirstOut)
			{
				return true;
			}
			if (State.TwoOutAt < 0.f)
			{
				State.TwoOutAt = State.StageTime;
			}
			const bool bHolding = !Exit->HasCrossed(Third) && !Exit->IsExiting() && Exit->GetCrossedCount() == Last && Exit->GetRequiredCount() == State.Living;
			State.bSawTwoOutNoFade |= bHolding;
			if (State.StageTime < State.TwoOutAt + 0.8f)
			{
				return true;
			}
			Check(State, State.bSawTwoOutNoFade && !Exit->IsExiting() && !Mission->IsMissionComplete()
				&& Mission->GetObjective().ToString().Contains(FString::Printf(TEXT("%d/%d"), Last, State.Living)),
				FString::Printf(TEXT("%d of %d out, the last inside: no fade, mission not over (\"%s\")"), Last, State.Living, *Mission->GetObjective().ToString()));
			Squad->ToggleGuard(Third);
			Check(State, Third->OrderMoveTo(State.OutTargets[Last], true) == EOperativeOrderResult::Accepted, TEXT("the last runs out"));
			Next(State, EStage::Third);
			return true;
		}
		case EStage::Third:
		{
			AOperativeCharacter* Third = State.Order.Num() > 0 ? State.Order.Last().Get() : nullptr;
			if (!Exit->IsExiting())
			{
				if (Third && Exit->HasCrossed(Third))
				{
					Check(State, false, TEXT("the third is out but the level did not end"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, Third && Exit->HasCrossed(Third) && Exit->GetCrossedCount() == State.Living && Exit->GetRequiredCount() == State.Living,
				FString::Printf(TEXT("the fade starts when the last crosses (%d / %d out, %.1f s after his order)"), Exit->GetCrossedCount(),
					Exit->GetRequiredCount(), State.StageTime));
			const FString AllOut = FString::Printf(TEXT("%d/%d"), State.Living, State.Living);
			Check(State, Mission->IsMissionComplete() && !Mission->IsMissionFailed() && Mission->GetObjective().ToString().Contains(AllOut),
				FString::Printf(TEXT("the mission is complete (%s out)"), *AllOut));
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
			Check(State, Mission->IsAutomatedRun() && World->GetName().Contains(TEXT("FrozenLake")) && !Exit->HasLeaveDecision(),
				TEXT("a headless check (an automated run) stays on the black screen (no menu travel)"));
			Check(State, Exit->GetNextLevelMap().EndsWith(TEXT("L_Demo01_B_FrozenFactory")), FString::Printf(TEXT("the campaign's next level: %s"),
				*Exit->GetNextLevelMap()));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 60.f; // chilled on the way out: the next level starts warm (cold is not carried)
			}
			RecordCarry(State, Squad);
			Exit->SetLeaveInHeadless(true); // from here on like a real game
			Next(State, EStage::Leave);
			return true;
		}
		case EStage::Leave:
		{
			if (!Exit->HasLeaveDecision())
			{
				return true;
			}
			const ELevelExitNext Want = State.bNoPoints ? ELevelExitNext::NextLevel : ELevelExitNext::PointsScreen;
			Check(State, Exit->GetLeaveDecision() == Want, FString::Printf(TEXT("after the black screen: %s (want %s)"),
				*UEnum::GetValueAsString(Exit->GetLeaveDecision()), *UEnum::GetValueAsString(Want)));
			if (State.bNoPoints)
			{
				Check(State, !Exit->IsPointsScreenShown() && !Exit->GetPointsScreen() && Exit->IsTravelRequested(),
					TEXT("no unspent points: no points screen, the next level opens at once"));
				Next(State, EStage::Travel);
				return true;
			}
			Next(State, EStage::PointsScreen);
			return true;
		}
		case EStage::PointsScreen:
			return StepPointsScreen(State, World, Exit);
		default:
			return Finish(State, false);
		}
	}

	void RunWith(const TArray<FString>& Args, UWorld* World, bool bNoPoints, bool bKia = false)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->GameInstance = World->GetGameInstance();
		State->bNoPoints = bNoPoints;
		State->bKia = bKia;
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
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

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, false);
	}

	void RunNoPoints(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true);
	}

	void RunKia(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, false, true);
	}

	void RunKiaNoPoints(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs KiaCommand(
		TEXT("CodexTactics.GateExitLakeKiaSmoke"),
		TEXT("Dev check (bug 2026-10-23): GateExitLakeSmoke with the Engineer killed in action - the two living cross, the black screen leaves to the "
			"stat-points screen and the next campaign level (the fallen stays behind)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunKia));

	static FAutoConsoleCommandWithWorldAndArgs KiaNoPointsCommand(
		TEXT("CodexTactics.GateExitLakeKiaNoPointsSmoke"),
		TEXT("Dev check (bug 2026-10-23): GateExitLakeKiaSmoke with no unspent points - the next campaign level opens at once after the black screen."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunKiaNoPoints));

	static FAutoConsoleCommandWithWorldAndArgs NoPointsCommand(
		TEXT("CodexTactics.GateExitLakeNoPointsSmoke"),
		TEXT("Dev check (2026-10-22 b): GateExitLakeSmoke with no unspent points - after the black screen no points screen, the next campaign level "
			"(L_Demo01_B_FrozenFactory) opens at once with the carried squad."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNoPoints));

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GateExitLakeSmoke"),
		TEXT("Dev check (2026-10-22): the Demo 01 lake ends through the fully opened blast door - waves cleared (no end-of-fight button), the door opens, "
			"two of three out = no fade, the third out = fade to black + mission complete; then the stat-points screen -> Continue -> the next campaign level "
			"(L_Demo01_B_FrozenFactory) with the carried squad outside the open lake door. 'Clip' = rendered frames into Saved/Screenshots/GateExit/frames, "
			"'Shot' = one frame of the points screen into Saved/Screenshots/GateExit/points_screen.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
