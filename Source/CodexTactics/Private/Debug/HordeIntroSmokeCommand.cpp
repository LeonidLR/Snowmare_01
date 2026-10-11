// Dev-only headless check of the sudden horde start (user decisions / feedback 2026-10-11, UHordeIntroSubsystem) on
// L_MovementTest with its level waves (wave 1: hounds + frostbitten; wave 2 adds cutters + marksmen; wave 3: hounds,
// cutters, marksmen); the squad waits 30 m from the spawn points so the horde has a run-up:
//   Scripts/smoke.ps1 -Command CodexTactics.HordeIntroSmoke
// 1. the combat zone -> wave 1 spawns AT ONCE (no cutscene / preparation phase, nothing queued) into the RUNNING fight:
//    real time, no pause, the camera stays on the squad, saving refused, the horde intro waits;
// 2. the horde becomes a running mass -> trigger (not before MinDelaySeconds, at MaxWaitSeconds at the latest): the free
//    tactical pause opens (charges 3/3 kept), the «HORDE INCOMING» alert shows, the flyover runs in slow motion
//    (FlyoverTimeDilation, faster than the 0.02 pause): wide shot on the running mass, a close tracking shot (7 m) of a
//    running front-runner per type in spawn order (hound, frostbitten), back to the leader (camera locked meanwhile,
//    the whole horde revealed); then the free pause at 0.02, camera unlocked;
// 3. orders in the free pause: a planned move and a barricade set-up; a Space tap releases it: real time, charges 3/3;
// 4. wave 2 comes at once (real time, intro armed); its flyover shows only the new types (cutter, marksman); Esc
//    (simulated key) skips it during a type shot: camera at once on the leader, unlocked, the free pause at 0.02, no menu;
// 5. wave 3 before its trigger: a click is a normal click (no pause), Esc opens the free pause at once (no flyover);
// 6. a load resumes wave 3 (RestoreForLoad): it comes suddenly again; known types only: wide shot + return; a click skips.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/HordeAlertWidget.h"
#include "UI/PhaseBannersWidget.h"

namespace HordeIntroSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		TArray<ECodexGamePhase> PhasesSeen;
		FDelegateHandle PhaseHandle;
		int32 LastShot = INDEX_NONE;
		float ShotStartTime = 0.f;
		int32 VerifiedShot = INDEX_NONE;
		bool bFlyoverSeen = false;
		bool bWaitChecked = false;
		FVector PlannedMove = FVector::ZeroVector;
		float MoveStartDistance = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Mover;
		bool bKeyDown = false;
		int32 SubStep = 0;
		int32 StartedBefore = 0;
		TWeakObjectPtr<UPhaseBannersWidget> Banners;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HordeIntroSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		State.SubStep = 0;
		State.bKeyDown = false;
	}

	void PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	float WorldDilation(const UWorld* World)
	{
		const AWorldSettings* Settings = World->GetWorldSettings();
		return Settings ? Settings->TimeDilation : 1.f;
	}

	FString ShotLabels(const UHordeIntroSubsystem& Intro)
	{
		FString Text;
		for (const FHordeIntroShot& Shot : Intro.GetShots())
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT(" ")) + HordeIntroRules::ShotLabel(Shot);
		}
		return Text;
	}

	/** Unique types of the live enemies in spawn (actor) order. */
	TArray<EEnemyArchetype> LiveTypes(UWorld* World)
	{
		TArray<EEnemyArchetype> Types;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (!It->IsDying())
			{
				Types.AddUnique(It->GetArchetype());
			}
		}
		return Types;
	}

	/** Checks the running shot once its glide is over: the camera frames what the shot is about. */
	void VerifyShot(FState& State, const UHordeIntroSubsystem& Intro, const ATacticalCameraPawn& Camera, const AActor* Leader)
	{
		const int32 Index = Intro.GetCurrentShotIndex();
		if (!Intro.GetShots().IsValidIndex(Index) || State.VerifiedShot == Index)
		{
			return;
		}
		const FHordeIntroConfig Config = HordeIntroRules::GetConfig();
		const FHordeIntroShot& Shot = Intro.GetShots()[Index];
		const float Settle = Shot.Kind == EHordeIntroShotKind::Return ? Shot.Seconds - 0.15f : Config.ShotBlendSeconds + 0.25f;
		if (State.Total - State.ShotStartTime < Settle)
		{
			return;
		}
		State.VerifiedShot = Index;
		const AActor* Target = Intro.GetCurrentShotTarget();
		const FString Label = HordeIntroRules::ShotLabel(Shot);
		switch (Shot.Kind)
		{
		case EHordeIntroShotKind::Wide:
			Check(State, Target && Camera.GetFollowTarget() == Target && Camera.GetCurrentDistance() >= Config.WideMinDistance - 50.f
				&& Camera.GetCurrentDistance() <= Config.WideMaxDistance + 50.f,
				FString::Printf(TEXT("%s: camera on the running mass at %.0f m (yaw %.0f)"), *Label, Camera.GetCurrentDistance() / 100.f,
					Camera.GetCurrentYaw()));
			break;
		case EHordeIntroShotKind::Type:
		{
			const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Target);
			const float Speed = Enemy ? Enemy->GetVelocity().Size2D() : 0.f;
			Check(State, Enemy && Enemy->GetArchetype() == Shot.Archetype && Camera.GetFollowTarget() == Enemy && !Enemy->IsHidden()
				&& FMath::IsNearlyEqual(Camera.GetCurrentDistance(), Config.TypeShotDistance, 60.f)
				&& FVector::Dist2D(Camera.GetFocus(), Enemy->GetActorLocation()) < 150.f,
				FString::Printf(TEXT("%s: camera tracks %s close at %.1f m"), *Label, Enemy ? *Enemy->GetName() : TEXT("-"),
					Camera.GetCurrentDistance() / 100.f));
			Check(State, Speed > 50.f, FString::Printf(TEXT("%s: the tracked one moves (%.0f cm/s)"), *Label, Speed));
			break;
		}
		default:
			Check(State, Leader && Camera.GetFollowTarget() == Leader, FString::Printf(TEXT("%s: back on the leader"), *Label));
			break;
		}
	}

	/** Records shot changes and verifies them; true once the flyover is no longer running. */
	bool TrackFlyover(FState& State, const UHordeIntroSubsystem& Intro, const ATacticalCameraPawn& Camera, const AActor* Leader)
	{
		const int32 Index = Intro.GetCurrentShotIndex();
		if (Index != State.LastShot)
		{
			State.LastShot = Index;
			State.ShotStartTime = State.Total;
		}
		VerifyShot(State, Intro, Camera, Leader);
		return !Intro.IsFlyoverActive();
	}

	/** The intro's trigger has fired: the free pause, slow motion, alert, locked camera. */
	void CheckTrigger(FState& State, UWorld* World, const UGameFlowSubsystem& Flow, const UHordeIntroSubsystem& Intro,
		const ATacticalCameraPawn& Camera)
	{
		const FHordeIntroConfig Config = HordeIntroRules::GetConfig();
		const FGameFlowConfig& FlowConfig = Flow.GetConfig();
		Check(State, Intro.GetTriggerGameSeconds() >= Config.MinDelaySeconds - 0.05f && Intro.GetTriggerGameSeconds() <= Config.MaxWaitSeconds + 0.3f,
			FString::Printf(TEXT("trigger %.2f game s after the spawn (%s, %.0f %% running)"), Intro.GetTriggerGameSeconds(),
				Intro.WasTriggerByMass() ? TEXT("running mass") : TEXT("timeout"), Intro.GetTriggerRunningFraction() * 100.f));
		Check(State, Flow.IsFreePause() && Flow.GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges,
			TEXT("the free tactical pause opened (charges 3/3)"));
		Check(State, FMath::IsNearlyEqual(WorldDilation(World), Config.FlyoverTimeDilation, 0.001f)
			&& WorldDilation(World) > FlowConfig.TacticalPauseTimeDilation * 2.f,
			FString::Printf(TEXT("flyover slow motion %.2f (the pause would be %.2f)"), WorldDilation(World), FlowConfig.TacticalPauseTimeDilation));
		const UHordeAlertWidget* Alert = Intro.GetAlertWidget();
		Check(State, Alert && Alert->IsInViewport() && Intro.GetAlertTitle().ToString() == TEXT("HORDE INCOMING")
			&& Intro.GetAlertSubtitle().ToString().Contains(TEXT("TACTICAL PAUSE")),
			FString::Printf(TEXT("alert \"%s\" / \"%s\""), *Intro.GetAlertTitle().ToString(), *Intro.GetAlertSubtitle().ToString()));
		Check(State, Camera.IsCinematicLocked(), TEXT("camera input locked during the flyover"));
		const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		bool bAllShown = true;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			bAllShown &= It->IsDying() || !It->IsHidden();
		}
		Check(State, (!Sight || Sight->IsCinematicReveal()) && bAllShown, TEXT("the flyover reveals the whole horde"));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 180.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
		if (!Flow || !Squad || !Waves || !Intro || !PC || !Camera || !Squad->GetLeader())
		{
			return State.Total < 6.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // the horde must live long enough to be filmed
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		if (!State.Banners.IsValid())
		{
			State.Banners = CreateWidget<UPhaseBannersWidget>(PC, UPhaseBannersWidget::StaticClass());
		}
		AOperativeCharacter* Leader = Squad->GetLeader();
		const FGameFlowConfig& FlowConfig = Flow->GetConfig();
		const FHordeIntroConfig Config = HordeIntroRules::GetConfig();
		const bool bPauseDilation = FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			State.PhaseHandle = Flow->OnBeforeGameFlowChanged.AddLambda([&State](ECodexGamePhase, ECodexGamePhase NewPhase, ECodexCombatMode)
			{
				State.PhasesSeen.Add(NewPhase);
			});
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration before the horde"));
			Check(State, Flow->TriggerCombatZone() == EGameFlowResult::Ok, TEXT("combat zone triggered"));
			Check(State, State.PhasesSeen.Num() == 1 && State.PhasesSeen[0] == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("straight into the wave: %d phase change(s), no cutscene / preparation"), State.PhasesSeen.Num()));
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Flow->IsAwaitingHordeIntro() && !Flow->IsFreePause()
				&& FMath::IsNearlyEqual(WorldDilation(World), Config.PreTriggerTimeDilation, 0.001f),
				TEXT("no pause at the spawn: the fight runs, horde intro armed"));
			Check(State, Intro->GetStage() == EHordeIntroStage::Waiting && !Camera->IsCinematicLocked(), TEXT("intro waits, camera free"));
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			const int32 Expected = Level && Level->Config.Waves.Num() > 0 ? Level->Config.Waves[0].GetTotalEnemyCount() : -1;
			Check(State, Waves->GetAliveEnemyCount() > 0 && Waves->GetAliveEnemyCount() == Expected && Waves->GetRemainingSpawnCount() == 0,
				FString::Printf(TEXT("the whole wave spawned at once: %d alive (level wave 1: %d)"), Waves->GetAliveEnemyCount(), Expected));
			Check(State, Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges, TEXT("charges 3/3"));
			if (const USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				// The autosave itself (off in smokes: shared Saved/ files) is checked by SaveRoundTripSmoke.
				Check(State, !Saves->CanSaveNow(), TEXT("no saving in the fight"));
			}
			Next(State);
			return true;
		}
		case 1:
			if (!Intro->IsFlyoverActive() && !State.bFlyoverSeen)
			{
				if (!State.bWaitChecked && State.StageTime > 1.f && Intro->GetStage() == EHordeIntroStage::Waiting)
				{
					State.bWaitChecked = true;
					Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Camera->GetFollowTarget() == Leader
						&& !Camera->IsCinematicLocked(), TEXT("1 s after the spawn: real time, the camera stays on the squad"));
				}
				if (State.StageTime > Config.MaxWaitSeconds / FMath::Max(0.05f, Config.PreTriggerTimeDilation) + 3.f)
				{
					Check(State, false, TEXT("the flyover never started"));
					return Finish(State, false);
				}
				return true;
			}
			if (!State.bFlyoverSeen)
			{
				State.bFlyoverSeen = true;
				CheckTrigger(State, World, *Flow, *Intro, *Camera);
				FString ExpectedLabels;
				for (const FHordeIntroShot& Shot : HordeIntroRules::BuildShotList(LiveTypes(World), TSet<EEnemyArchetype>(), Config))
				{
					ExpectedLabels += (ExpectedLabels.IsEmpty() ? TEXT("") : TEXT(" ")) + HordeIntroRules::ShotLabel(Shot);
				}
				Check(State, ShotLabels(*Intro) == ExpectedLabels && LiveTypes(World).Num() >= 2,
					FString::Printf(TEXT("shots \"%s\" (wide + one per type in spawn order + return)"), *ShotLabels(*Intro)));
			}
			if (Intro->IsFlyoverActive() && !FMath::IsNearlyEqual(WorldDilation(World), Config.FlyoverTimeDilation, 0.001f))
			{
				Check(State, false, FString::Printf(TEXT("the world left the flyover slow motion (%.3f)"), WorldDilation(World)));
			}
			if (Intro->IsFlyoverActive() && !Flow->IsFreePause())
			{
				Check(State, false, TEXT("the free pause ended during the flyover"));
			}
			if (!TrackFlyover(State, *Intro, *Camera, Leader))
			{
				return true;
			}
			{
				Check(State, Intro->GetCompletedCount() == 1 && Intro->GetSkippedCount() == 0, TEXT("flyover played to the end"));
				Check(State, !Camera->IsCinematicLocked() && Camera->GetFollowTarget() == Leader, TEXT("camera back on the leader, input unlocked"));
				Check(State, Flow->IsFreePause() && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges && bPauseDilation,
					FString::Printf(TEXT("settled into the free pause (dilation %.3f, charges 3/3, no timer)"), WorldDilation(World)));
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				Check(State, !Sight || !Sight->IsCinematicReveal(), TEXT("the squad's own line of sight is back"));
				const FString Banner = State.Banners.IsValid() ? State.Banners->GetPauseText().ToString() : FString();
				Check(State, Banner.Contains(TEXT("free pause")), FString::Printf(TEXT("banner: %s"), *Banner));
				for (const EEnemyArchetype Type : LiveTypes(World))
				{
					Check(State, Intro->GetShownTypes().Contains(Type), FString::Printf(TEXT("type %s marked shown"),
						*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Type))));
				}
			}
			Next(State);
			return true;
		case 2:
		{
			// Orders in the free pause: a planned move and a barricade set-up.
			AOperativeCharacter* Mover = nullptr;
			AOperativeCharacter* Engineer = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
			}
			Engineer = Engineer ? Engineer : Leader;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				// The mover is neither the leader nor the barricade worker (a set-up order replaces a worker's move plan).
				Mover = Member != Leader && Member != Engineer && !Mover ? Member : Mover;
			}
			State.Mover = Mover;
			if (Mover)
			{
				State.PlannedMove = Squad->PlanMove(Mover, Mover->GetActorLocation() - Mover->GetActorForwardVector() * 500.f, false,
					FlowConfig.PauseOrderRadius);
				State.MoveStartDistance = FVector::Dist2D(Mover->GetActorLocation(), State.PlannedMove);
			}
			Check(State, Squad->GetPlannedOrderCount() >= 1, TEXT("a move order planned in the free pause"));
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			Engineer->BarricadesCount = FMath::Max(Engineer->BarricadesCount, 1);
			const FVector Spot = Engineer->GetActorLocation() - Engineer->GetActorForwardVector() * 300.f;
			const bool bStarted = Relocation && Relocation->StartDeployPlacement(EDeployableType::Barricade, Engineer);
			if (bStarted)
			{
				Relocation->UpdatePreview(Spot);
				Relocation->ConfirmPlacement(Spot); // anchor
				Relocation->ConfirmPlacement(Spot); // confirm: planned in the pause
			}
			Check(State, bStarted && !Relocation->IsPlacingDeployable() && Relocation->GetActiveDeployCount() == 0,
				TEXT("barricade placement works in the free pause (planned, not started yet)"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && !Flow->IsFreePause(), TEXT("Space: real time"));
			Check(State, Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges && Flow->GetPauseCooldownRemaining() <= 0.f,
				FString::Printf(TEXT("pause budget untouched: %d/%d, no cooldown"), Flow->GetPauseCharges(), FlowConfig.TacticalPauseMaxCharges));
			Check(State, FMath::IsNearlyEqual(WorldDilation(World), 1.f, 0.001f), TEXT("real time dilation 1"));
			Check(State, Relocation && Relocation->GetActiveDeployCount() >= 1, TEXT("the planned barricade set-up runs"));
			Check(State, Squad->GetPlannedOrderCount() == 0, TEXT("the planned move was handed out on release"));
			Next(State);
			return true;
		}
		case 3:
		{
			const AOperativeCharacter* Mover = State.Mover.Get();
			const float Left = Mover ? FVector::Dist2D(Mover->GetActorLocation(), State.PlannedMove) : -1.f;
			const bool bOnTheWay = Mover && (Mover->IsMoving() || Left < 150.f || State.MoveStartDistance - Left > 60.f);
			if (!bOnTheWay && State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, bOnTheWay, FString::Printf(TEXT("the planned move runs (moving %d, %.0f of %.0f cm left)"),
				Mover && Mover->IsMoving() ? 1 : 0, Left, State.MoveStartDistance));
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 4:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
				Check(State, Flow->AdvanceAfterWave() == EGameFlowResult::Ok, TEXT("next wave"));
				Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsAwaitingHordeIntro() && Flow->GetWaveIndex() == 2,
					TEXT("wave 2 comes at once (no rest countdown), real time, intro armed"));
				State.LastShot = INDEX_NONE;
				State.VerifiedShot = INDEX_NONE;
				Next(State);
				return true;
			}
			return State.StageTime < 5.f || Finish(State, false);
		case 5:
		{
			if (!Intro->IsFlyoverActive())
			{
				return State.StageTime < Config.MaxWaitSeconds + 4.f || Finish(State, false);
			}
			const TSet<EEnemyArchetype> Shown1 = { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten };
			bool bNoOld = true;
			int32 TypeShots = 0;
			for (const FHordeIntroShot& Shot : Intro->GetShots())
			{
				bNoOld &= !(Shot.Kind == EHordeIntroShotKind::Type && Shown1.Contains(Shot.Archetype));
				TypeShots += Shot.Kind == EHordeIntroShotKind::Type ? 1 : 0;
			}
			Check(State, bNoOld && TypeShots >= 1 && Intro->GetShots()[0].Kind == EHordeIntroShotKind::Wide,
				FString::Printf(TEXT("wave 2 shots \"%s\": the running mass + only the new types"), *ShotLabels(*Intro)));
			Next(State);
			return true;
		}
		case 6:
		{
			// Esc during the last type shot skips the rest.
			TrackFlyover(State, *Intro, *Camera, Leader);
			const int32 Index = Intro->GetCurrentShotIndex();
			const TArray<FHordeIntroShot>& Shots = Intro->GetShots();
			int32 LastType = INDEX_NONE;
			for (int32 Each = 0; Each < Shots.Num(); ++Each)
			{
				LastType = Shots[Each].Kind == EHordeIntroShotKind::Type ? Each : LastType;
			}
			if (!State.bKeyDown)
			{
				if (Index != LastType || State.Total - State.ShotStartTime < 0.6f)
				{
					return Intro->IsFlyoverActive() || Finish(State, false);
				}
				PressKey(PC, EKeys::Escape, IE_Pressed);
				State.bKeyDown = true;
				return true;
			}
			PressKey(PC, EKeys::Escape, IE_Released);
			State.bKeyDown = false;
			const ACodexTacticsHUD* Hud = Cast<ACodexTacticsHUD>(PC->GetHUD());
			Check(State, !Intro->IsIntroPending() && Intro->GetSkippedCount() == 1, TEXT("Esc skipped the flyover"));
			Check(State, Camera->GetFollowTarget() == Leader && FVector::Dist2D(Camera->GetFocus(), Leader->GetActorLocation()) < 120.f
				&& !Camera->IsCinematicLocked(), TEXT("camera at once on the leader, unlocked"));
			Check(State, Flow->IsFreePause() && bPauseDilation, TEXT("the free pause stays, at the pause dilation"));
			Check(State, (!Hud || !Hud->IsPauseMenuOpen()) && !UGameplayStatics::IsGamePaused(World), TEXT("Esc opened no pause menu"));
			for (const FHordeIntroShot& Shot : Shots)
			{
				if (Shot.Kind == EHordeIntroShotKind::Type)
				{
					Check(State, Intro->GetShownTypes().Contains(Shot.Archetype), FString::Printf(TEXT("%s counts as shown"), *HordeIntroRules::ShotLabel(Shot)));
				}
			}
			SmokeUtils::ReleaseHordeIntroPause(Flow);
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 7:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Flow->AdvanceAfterWave();
				Check(State, Flow->IsAwaitingHordeIntro() && Flow->GetWaveIndex() == 3 && Intro->GetStage() == EHordeIntroStage::Waiting,
					TEXT("wave 3 at once, intro waiting"));
				State.StartedBefore = Intro->GetStartedCount();
				Next(State);
				return true;
			}
			return State.StageTime < 5.f || Finish(State, false);
		case 8:
			// Before the trigger: a click is a normal click, Esc opens the free pause at once.
			switch (State.SubStep++)
			{
			case 0:
				PressKey(PC, EKeys::LeftMouseButton, IE_Pressed);
				return true;
			case 1:
				PressKey(PC, EKeys::LeftMouseButton, IE_Released);
				return true;
			case 2:
				Check(State, Intro->GetStage() == EHordeIntroStage::Waiting && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
					TEXT("a click before the trigger skips nothing (normal click, real time)"));
				PressKey(PC, EKeys::Escape, IE_Pressed);
				return true;
			case 3:
			{
				PressKey(PC, EKeys::Escape, IE_Released);
				const ACodexTacticsHUD* Hud = Cast<ACodexTacticsHUD>(PC->GetHUD());
				Check(State, Flow->IsFreePause() && bPauseDilation && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges,
					TEXT("Esc before the trigger: the free pause at once (0.02, charges 3/3)"));
				Check(State, Intro->GetStage() == EHordeIntroStage::Idle && Intro->GetStartedCount() == State.StartedBefore
					&& Intro->GetAlertAlpha() > 0.f && (!Hud || !Hud->IsPauseMenuOpen()),
					TEXT("no flyover, the alert explains the pause, no pause menu"));
				SmokeUtils::ReleaseHordeIntroPause(Flow);
				Waves->ClearAllEnemies();
				Next(State);
				return true;
			}
			default:
				return true;
			}
		case 9:
			// A load resumes wave 3 in its preparation: it comes suddenly again on the next flow tick.
			if (State.SubStep == 0)
			{
				if (Flow->GetPhase() != ECodexGamePhase::WaveCleared)
				{
					return State.StageTime < 5.f || Finish(State, false);
				}
				SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
				Flow->RestoreForLoad(true, true, 3);
				State.SubStep = 1;
				return true;
			}
			if (!Flow->IsAwaitingHordeIntro())
			{
				return State.StageTime < 3.f || Finish(State, false);
			}
			Check(State, Flow->GetWaveIndex() == 3 && Waves->GetAliveEnemyCount() > 0, TEXT("loaded wave 3 came suddenly again"));
			State.LastShot = INDEX_NONE;
			State.VerifiedShot = INDEX_NONE;
			Next(State);
			return true;
		case 10:
		{
			if (!State.bKeyDown)
			{
				if (!Intro->IsFlyoverActive())
				{
					return State.StageTime < Config.MaxWaitSeconds + 4.f || Finish(State, false);
				}
				TrackFlyover(State, *Intro, *Camera, Leader);
				if (State.Total - State.ShotStartTime < 0.8f)
				{
					return true;
				}
				const TArray<FHordeIntroShot>& Shots = Intro->GetShots();
				const bool bKnownOnly = Shots.Num() == 2 && Shots[0].Kind == EHordeIntroShotKind::Wide
					&& FMath::IsNearlyEqual(Shots[0].Seconds, Config.RepeatWideShotSeconds) && Shots[1].Kind == EHordeIntroShotKind::Return;
				Check(State, bKnownOnly, FString::Printf(TEXT("wave 3 again (known types): \"%s\""), *ShotLabels(*Intro)));
				Squad->ClearPlannedOrder(Leader);
				PressKey(PC, EKeys::LeftMouseButton, IE_Pressed);
				State.bKeyDown = true;
				return true;
			}
			PressKey(PC, EKeys::LeftMouseButton, IE_Released);
			State.bKeyDown = false;
			Check(State, !Intro->IsIntroPending() && Intro->GetSkippedCount() == 3, TEXT("a click skipped the flyover"));
			Check(State, Camera->GetFollowTarget() == Leader && !Camera->IsCinematicLocked(), TEXT("camera on the leader"));
			Check(State, Squad->GetPlannedOrderCount() == 0 && Flow->IsFreePause() && bPauseDilation,
				TEXT("the click gave no order, the free pause stays"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && FMath::IsNearlyEqual(WorldDilation(World), 1.f, 0.001f),
				TEXT("Space resumes real time"));
			Flow->OnBeforeGameFlowChanged.Remove(State.PhaseHandle);
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			SmokeUtils::PlaceSquadAwayFromWaveSpawns(PlaceWorld.Get(), 3000.f);
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeIntroSmoke"),
		TEXT("Dev check of the sudden horde start (running mass trigger, slow-motion flyover, free pause, skips, next waves); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
