// Dev-only headless check of the sudden horde start (user decisions / feedback 2026-10-11, UHordeIntroSubsystem) on
// L_MovementTest with its level waves (wave 1: hounds + frostbitten; wave 2 adds cutters + marksmen; wave 3: hounds,
// cutters, marksmen); the squad waits 30 m from the spawn points so the horde has a run-up:
//   Scripts/smoke.ps1 -Command CodexTactics.HordeIntroSmoke
// User decisions 2026-10-26: (a) the warning never opens the tactical pause - only the player's Space does (the wave's first Space is the
// free pause); (b) only the fight's FIRST wave is warned - later waves come silently (only the HUD's incoming arrows).
// 1. the combat zone -> wave 1 spawns AT ONCE (no cutscene / preparation phase, nothing queued) into the RUNNING fight:
//    real time, no pause, the camera stays on the squad, saving refused, the horde intro waits;
// 2. the horde becomes a running mass -> trigger (not before MinDelaySeconds, at MaxWaitSeconds at the latest): the warning - the
//    «HORDE INCOMING» alert, the flyover in slow motion (FlyoverTimeDilation): wide shot on the running mass, a close tracking shot
//    (7 m) of a running front-runner per type in spawn order (hound, frostbitten), back to the leader (camera locked meanwhile, the
//    whole horde revealed) - and NO pause: the flow stays in real time; after the flyover real time (dilation 1), camera unlocked;
// 3. 2 s of real time - nothing pauses by itself; the player's Space opens the FREE pause (charges 3/3);
// 4. orders in the free pause: a planned move and a barricade set-up; a Space tap releases it: real time, charges 3/3;
// 5. (game designer 2026-10-25: no "Start the next wave" plate) wave 1 cleared -> straight into the rest before wave 2: no victory
//    panel / WaveCleared phase, no «Start Battle» button, the banner counts down "NEXT WAVE IN", the world runs; NOBODY clicks
//    anything and wave 2 breaks through on its own when the rest runs out;
// 6. wave 2 (a later wave of the fight): no warning at all - for as long as a warning could take: no alert, no flyover, no slow
//    motion, no pause, the camera on the squad;
// 7. wave 3 (the rest cut short like the bot does) with the old rule Codex.HordeIntro.LaterWaves 1 (the warning's other paths): a click
//    before the trigger is a normal click, Esc gives the warning at once (no flyover, no pause);
// 8. a load resumes wave 3 (RestoreForLoad): its flyover flies only to the types never shown on the level (cutter, marksman - wave 2
//    came silently), no wide shot (2026-10-25), in real time; Esc during its last type shot skips it: camera on the leader, real time;
// 9. a second load of wave 3: every type known -> at the trigger the alert, NO flyover, real time; Space: the free pause; Space: real time.
// Framing (game designer play-test 2026-10-22, "the fly-in sometimes shows nothing"): a level hound parked 60 m away (NOT part
// of the wave, like the lake's patrols) must not pull the frame; every settled wide shot holds the wave's main cluster in the
// camera frustum (feet and heads), visible, each at least 2.5 % of the frame height; a type shot holds its runner, >= 12 %.

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
#include "HAL/IConsoleManager.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/HordeAlertWidget.h"
#include "UI/PhaseBannersWidget.h"
#include "UI/VictoryPanelWidget.h"

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
		/** Melee close-in (2026-10-18): the mover was knocked down while the planned move should start (a pounce interrupts it). */
		bool bMoverKnockedDown = false;
		float MoverUpAt = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Mover;
		bool bKeyDown = false;
		int32 SubStep = 0;
		int32 StartedBefore = 0;
		TWeakObjectPtr<UPhaseBannersWidget> Banners;
		/** A level enemy far away, not in the wave (the lake's patrols): the flyover must ignore it. */
		TWeakObjectPtr<AEnemyCharacter> Decoy;
		/** Game seconds the rest before wave 2 ran until the wave came by itself (2026-10-25). */
		float RestSeconds = 0.f;
		/** The intro's counters before a stage (warnings given, flyovers skipped). */
		int32 WarningsBefore = 0;
		int32 SkippedBefore = 0;
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
			if (!It->IsDying() && It->IsActorTickEnabled()) // the parked decoy is no part of the horde
			{
				Types.AddUnique(It->GetArchetype());
			}
		}
		return Types;
	}

	FVector EnemyFeet(const AEnemyCharacter* Enemy)
	{
		return Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight());
	}

	/** The real camera's view (the pawn's transform is the view: TacticalCameraPawn places itself there each tick). */
	FHordeFrameView CameraView(const ATacticalCameraPawn& Camera)
	{
		FHordeFrameView View;
		View.Location = Camera.GetActorLocation();
		View.Rotation = Camera.GetActorRotation();
		View.VerticalFovDeg = Camera.Config.VerticalFov;
		View.Aspect = 16.f / 9.f;
		return View;
	}

	/** Wide shot framing check: the wave's main cluster in the frustum, visible, big enough; the decoy never filmed. */
	void VerifyWideFrame(FState& State, const UHordeIntroSubsystem& Intro, const ATacticalCameraPawn& Camera, const FString& Label)
	{
		const FHordeIntroConfig Config = HordeIntroRules::GetConfig();
		const TArray<AEnemyCharacter*> Filmed = Intro.GetFilmedEnemies();
		Check(State, !Filmed.Contains(State.Decoy.Get()), FString::Printf(TEXT("%s: the level hound 60 m away is not part of the horde intro"), *Label));
		TArray<FVector> Feet;
		for (const AEnemyCharacter* Enemy : Filmed)
		{
			Feet.Add(EnemyFeet(Enemy));
		}
		const TArray<int32> Cluster = HordeIntroRules::MainCluster(Feet, Config.WideClusterRadiusCm);
		const FHordeFrameView View = CameraView(Camera);
		int32 InFrame = 0;
		int32 ClusterIn = 0;
		int32 Hidden = 0;
		float MinFraction = 1.f;
		for (int32 Index = 0; Index < Filmed.Num(); ++Index)
		{
			const bool bIn = HordeIntroRules::IsInFrame(View, Feet[Index], 0.f)
				&& HordeIntroRules::IsInFrame(View, Feet[Index] + FVector(0.f, 0.f, Filmed[Index]->GetSimpleCollisionHalfHeight() * 2.f), 0.f);
			InFrame += bIn ? 1 : 0;
			if (Cluster.Contains(Index))
			{
				ClusterIn += bIn ? 1 : 0;
				Hidden += Filmed[Index]->IsHidden() ? 1 : 0;
				MinFraction = FMath::Min(MinFraction, HordeIntroRules::ScreenHeightFraction(View, Feet[Index], Filmed[Index]->GetSimpleCollisionHalfHeight() * 2.f));
			}
		}
		Check(State, !Filmed.IsEmpty() && !Cluster.IsEmpty() && ClusterIn == Cluster.Num() && Hidden == 0 && InFrame * 10 >= Filmed.Num() * 6,
			FString::Printf(TEXT("%s: the wave in the frustum - main cluster %d / %d in frame, %d / %d of the wave, %d hidden"), *Label,
				ClusterIn, Cluster.Num(), InFrame, Filmed.Num(), Hidden));
		Check(State, MinFraction >= 0.025f, FString::Printf(TEXT("%s: big enough - the smallest framed enemy fills %.1f %% of the frame height (camera %.0f m)"),
			*Label, MinFraction * 100.f, Camera.GetCurrentDistance() / 100.f));
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
			VerifyWideFrame(State, Intro, Camera, Label);
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
			if (Enemy)
			{
				const FHordeFrameView View = CameraView(Camera);
				const float Height = Enemy->GetSimpleCollisionHalfHeight() * 2.f;
				const float Fraction = HordeIntroRules::ScreenHeightFraction(View, EnemyFeet(Enemy), Height);
				Check(State, Enemy != State.Decoy.Get() && HordeIntroRules::IsInFrame(View, EnemyFeet(Enemy), 0.05f)
					&& HordeIntroRules::IsInFrame(View, EnemyFeet(Enemy) + FVector(0.f, 0.f, Height), 0.05f) && Fraction >= 0.12f,
					FString::Printf(TEXT("%s: the runner in the frustum, a wave enemy, %.0f %% of the frame height"), *Label, Fraction * 100.f));
			}
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

	/** The intro's trigger has fired (2026-10-26): the warning in REAL TIME - slow-motion flyover, alert, locked camera, no pause. */
	void CheckTrigger(FState& State, UWorld* World, const UGameFlowSubsystem& Flow, const UHordeIntroSubsystem& Intro,
		const ATacticalCameraPawn& Camera)
	{
		const FHordeIntroConfig Config = HordeIntroRules::GetConfig();
		const FGameFlowConfig& FlowConfig = Flow.GetConfig();
		Check(State, Intro.GetTriggerGameSeconds() >= Config.MinDelaySeconds - 0.05f && Intro.GetTriggerGameSeconds() <= Config.MaxWaitSeconds + 0.3f,
			FString::Printf(TEXT("trigger %.2f game s after the spawn (%s, %.0f %% running)"), Intro.GetTriggerGameSeconds(),
				Intro.WasTriggerByMass() ? TEXT("running mass") : TEXT("timeout"), Intro.GetTriggerRunningFraction() * 100.f));
		Check(State, Flow.GetCombatMode() == ECodexCombatMode::RealTime && !Flow.IsFreePause() && !Flow.IsAwaitingHordeIntro()
			&& Flow.IsFreePauseOffered() && Flow.GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges,
			TEXT("the warning keeps the fight in REAL TIME: no pause opened (2026-10-26), the first Space stays free, charges 3/3"));
		Check(State, FMath::IsNearlyEqual(WorldDilation(World), Config.FlyoverTimeDilation, 0.001f)
			&& WorldDilation(World) > FlowConfig.TacticalPauseTimeDilation * 2.f,
			FString::Printf(TEXT("flyover slow motion %.2f (the pause would be %.2f)"), WorldDilation(World), FlowConfig.TacticalPauseTimeDilation));
		const UHordeAlertWidget* Alert = Intro.GetAlertWidget();
		Check(State, Alert && Alert->IsInViewport() && Intro.GetAlertTitle().ToString() == TEXT("HORDE INCOMING")
			&& Intro.GetAlertSubtitle().ToString().Contains(TEXT("[SPACE] FREE TACTICAL PAUSE")),
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

	void SetLaterWaves(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.HordeIntro.LaterWaves")))
		{
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 300.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			SetLaterWaves(0);
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
		const bool bRealTimeDilation = FMath::IsNearlyEqual(WorldDilation(World), 1.f, 0.001f);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			SetLaterWaves(0); // the default rule (2026-10-26): only the fight's first wave is warned
			State.PhaseHandle = Flow->OnBeforeGameFlowChanged.AddLambda([&State](ECodexGamePhase, ECodexGamePhase NewPhase, ECodexCombatMode)
			{
				State.PhasesSeen.Add(NewPhase);
			});
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration before the horde"));
			if (const ACodexTacticsGameMode* DecoyMode = World->GetAuthGameMode<ACodexTacticsGameMode>())
			{
				// A level hound 60 m behind the squad, parked (not part of any wave): the lake's patrols during its horde.
				const TSoftClassPtr<AEnemyCharacter>* Soft = DecoyMode->EnemyClasses.Find(EEnemyArchetype::FrostHound);
				UClass* DecoyClass = Soft ? Soft->LoadSynchronous() : nullptr;
				const FVector DecoyAt = SmokeUtils::FreeSpot(World, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 6000.f, Leader);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AEnemyCharacter* Decoy = nullptr;
				{
					const AEnemyCharacter::FScopedSpawnArchetype Scope(EEnemyArchetype::FrostHound);
					Decoy = World->SpawnActor<AEnemyCharacter>(DecoyClass ? DecoyClass : AEnemyCharacter::StaticClass(), DecoyAt, FRotator::ZeroRotator, Params);
				}
				if (Decoy)
				{
					Decoy->InitializeArchetype(EEnemyArchetype::FrostHound);
					Decoy->SetActorTickEnabled(false);
					Decoy->CustomTimeDilation = 0.f;
					State.Decoy = Decoy;
				}
				Check(State, State.Decoy.IsValid(), FString::Printf(TEXT("a parked level hound %.0f m away (not in the wave)"),
					Decoy ? FVector::Dist2D(Decoy->GetActorLocation(), Leader->GetActorLocation()) / 100.f : -1.f));
			}
			Check(State, Flow->TriggerCombatZone() == EGameFlowResult::Ok, TEXT("combat zone triggered"));
			Check(State, State.PhasesSeen.Num() == 1 && State.PhasesSeen[0] == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("straight into the wave: %d phase change(s), no cutscene / preparation"), State.PhasesSeen.Num()));
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Flow->IsAwaitingHordeIntro() && !Flow->IsFreePause()
				&& Flow->IsFreePauseOffered() && FMath::IsNearlyEqual(WorldDilation(World), Config.PreTriggerTimeDilation, 0.001f),
				TEXT("no pause at the spawn: the fight runs, horde intro armed, the first Space offered free"));
			Check(State, Intro->GetStage() == EHordeIntroStage::Waiting && !Camera->IsCinematicLocked(), TEXT("intro waits, camera free"));
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			// Environment phase 2b: the written wave under the weather / time-of-day roster (resolved at the wave start).
			const int32 Written = Level && Level->Config.Waves.Num() > 0 ? Level->Config.Waves[0].GetTotalEnemyCount() : -1;
			const int32 Expected = Waves->GetCurrentWaveSpawns().Num() > 0 ? Waves->GetCurrentWaveSpawnTotal() : Written;
			Check(State, Waves->GetAliveEnemyCount() > 0 && Waves->GetAliveEnemyCount() == Expected && Waves->GetRemainingSpawnCount() == 0,
				FString::Printf(TEXT("the whole wave spawned at once: %d alive (level wave 1: %d after the roster, %d written)"),
					Waves->GetAliveEnemyCount(), Expected, Written));
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
				for (const FHordeIntroShot& Shot : HordeIntroRules::BuildShotList(LiveTypes(World), FHordeIntroMemory(), Config))
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
			if (Intro->IsFlyoverActive() && Flow->GetCombatMode() != ECodexCombatMode::RealTime)
			{
				Check(State, false, TEXT("the flow left real time during the flyover (a pause opened by itself)"));
			}
			if (!TrackFlyover(State, *Intro, *Camera, Leader))
			{
				return true;
			}
			{
				Check(State, Intro->GetCompletedCount() == 1 && Intro->GetSkippedCount() == 0, TEXT("flyover played to the end"));
				Check(State, !Camera->IsCinematicLocked() && Camera->GetFollowTarget() == Leader, TEXT("camera back on the leader, input unlocked"));
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && !Flow->IsFreePause() && bRealTimeDilation
					&& Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges,
					FString::Printf(TEXT("after the flyover: REAL TIME (dilation %.3f), no pause, charges 3/3"), WorldDilation(World)));
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				Check(State, !Sight || !Sight->IsCinematicReveal(), TEXT("the squad's own line of sight is back"));
				if (UPhaseBannersWidget* Banners = State.Banners.Get())
				{
					Banners->Refresh();
					const FString Combat = Banners->GetCombatText().ToString();
					Check(State, Banners->GetPauseText().IsEmpty() && Combat.Contains(TEXT("[SPACE] free tactical pause")),
						FString::Printf(TEXT("banners: no pause banner, the combat banner offers the free pause (\"%s\")"), *Combat));
				}
				for (const EEnemyArchetype Type : LiveTypes(World))
				{
					Check(State, Intro->GetShownTypes().Contains(Type), FString::Printf(TEXT("type %s marked shown"),
						*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Type))));
				}
				Check(State, Intro->GetMemory().bWideShotShown, TEXT("the level remembers its wide shot (2026-10-25: once per level)"));
				if (AEnemyCharacter* Decoy = State.Decoy.Get())
				{
					Decoy->Destroy(); // the later waves count every enemy of the map
				}
			}
			Next(State);
			return true;
		case 2:
			// 2026-10-26: nothing opens the pause by itself - the fight runs until the player presses Space.
			if (Flow->GetCombatMode() != ECodexCombatMode::RealTime || !bRealTimeDilation)
			{
				Check(State, false, FString::Printf(TEXT("%.1f s after the warning: the fight left real time by itself (mode %d, dilation %.3f)"),
					State.StageTime, static_cast<int32>(Flow->GetCombatMode()), WorldDilation(World)));
				return Finish(State, false);
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, true, TEXT("2 s of real time after the warning, no pause"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->IsFreePause() && !Flow->IsFreePauseOffered() && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges
				&& FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f),
				TEXT("the player's Space: the FREE tactical pause (no charge, the pause dilation)"));
			if (UPhaseBannersWidget* Banners = State.Banners.Get())
			{
				const FString Banner = Banners->GetPauseText().ToString();
				Check(State, Banner.Contains(TEXT("free pause")), FString::Printf(TEXT("banner: %s"), *Banner));
			}
			Next(State);
			return true;
		case 3:
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
			// User decision 2026-10-11: the free pause is an unrestricted preparation - no radius ring, no distance limit on orders
			// and placements (the paid pause keeps its 12 m ring and limit: RadiusRingSmoke).
			{
				const URadiusRingSubsystem* Rings = World->GetSubsystem<URadiusRingSubsystem>();
				// (The selection rings / defence markers are radius ring actors too: only the pause's own ring counts.)
				const ARadiusRingActor* PauseRing = Rings ? Rings->GetRing() : nullptr;
				Check(State, Rings && Rings->GetMode() == ERadiusRingMode::Hidden && (!PauseRing || PauseRing->IsHidden()),
					FString::Printf(TEXT("free pause: no radius ring (mode %d, ring %s)"), Rings ? static_cast<int32>(Rings->GetMode()) : -1,
						!PauseRing ? TEXT("not spawned") : (PauseRing->IsHidden() ? TEXT("hidden") : TEXT("VISIBLE"))));
				Check(State, !Flow->IsPauseRadiusLimited() && Flow->GetPauseOrderRadius() > FlowConfig.PauseOrderRadius * 100.f,
					FString::Printf(TEXT("free pause: order radius %.0f cm (a paid pause: %.0f)"), Flow->GetPauseOrderRadius(), FlowConfig.PauseOrderRadius));
				if (Mover)
				{
					const FVector Far = Mover->GetActorLocation() - Mover->GetActorForwardVector() * (FlowConfig.PauseOrderRadius * 3.f);
					const FVector FarPlanned = Squad->PlanMove(Mover, Far, false, Flow->GetPauseOrderRadius());
					Check(State, FVector::Dist2D(FarPlanned, Far) < 1.f,
						FString::Printf(TEXT("free pause: a move order %.0f m away is planned as given (planned %.0f m)"),
							FlowConfig.PauseOrderRadius * 3.f / 100.f, FVector::Dist2D(FarPlanned, Mover->GetActorLocation()) / 100.f));
					Squad->ClearPlannedOrder(Mover);
				}
			}
			if (Mover)
			{
				State.PlannedMove = Squad->PlanMove(Mover, Mover->GetActorLocation() - Mover->GetActorForwardVector() * 500.f, false,
					Flow->GetPauseOrderRadius());
				State.MoveStartDistance = FVector::Dist2D(Mover->GetActorLocation(), State.PlannedMove);
			}
			Check(State, Squad->GetPlannedOrderCount() >= 1, TEXT("a move order planned in the free pause"));
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			Engineer->BarricadesCount = FMath::Max(Engineer->BarricadesCount, 1);
			if (Relocation && Relocation->StartDeployPlacement(EDeployableType::Barricade, Engineer))
			{
				// A spot beyond the old pause radius is a valid barricade spot in the free pause.
				const FVector FarSpot = Engineer->GetActorLocation() + Engineer->GetActorForwardVector() * (FlowConfig.PauseOrderRadius * 2.5f);
				Relocation->UpdatePreview(FarSpot);
				Check(State, Relocation->IsGhostValid() && Relocation->GetRadius(*Engineer) > FlowConfig.PauseOrderRadius * 100.f,
					FString::Printf(TEXT("free pause: a barricade %.0f m away is valid (radius %.0f cm)"), FlowConfig.PauseOrderRadius * 2.5f / 100.f,
						Relocation->GetRadius(*Engineer)));
				Relocation->CancelPlacement();
			}
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
			// Melee close-in (2026-10-18): the horde's hounds may reach the squad, a pounce may have knocked the mover down before the
			// order - the order is buffered and runs once she is up (Sprint 14). No new knockdown during the check: the horde is frozen.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorTickEnabled(false);
			}
			Next(State);
			return true;
		}
		case 4:
		{
			const AOperativeCharacter* Mover = State.Mover.Get();
			const float Left = Mover ? FVector::Dist2D(Mover->GetActorLocation(), State.PlannedMove) : -1.f;
			const bool bOnTheWay = Mover && (Mover->IsMoving() || Left < 150.f || State.MoveStartDistance - Left > 60.f);
			State.bMoverKnockedDown |= Mover && Mover->IsKnockedDown();
			// Down: the buffered order waits for the get-up (cap 6 s), then 1.5 s to start.
			if (Mover && Mover->IsKnockedDown() && State.StageTime < 6.f)
			{
				State.MoverUpAt = State.StageTime;
				return true;
			}
			if (!bOnTheWay && State.StageTime < State.MoverUpAt + 1.5f)
			{
				return true;
			}
			Check(State, bOnTheWay, FString::Printf(TEXT("the planned move runs (moving %d, %.0f of %.0f cm left%s)"),
				Mover && Mover->IsMoving() ? 1 : 0, Left, State.MoveStartDistance, State.bMoverKnockedDown ? TEXT("; she was down first: the buffered order") : TEXT("")));
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 5:
		{
			// Game designer 2026-10-25: no "Start the next wave" plate - wave 1 cleared goes straight into the rest before wave 2.
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				return State.StageTime < 5.f || Finish(State, false);
			}
			const APlayerController* HudOwner = PC;
			const ACodexTacticsHUD* Hud = HudOwner ? Cast<ACodexTacticsHUD>(HudOwner->GetHUD()) : nullptr;
			const UVictoryPanelWidget* Panel = Hud ? Hud->GetVictoryPanel() : nullptr;
			Check(State, Flow->GetPhase() == ECodexGamePhase::Preparation && Flow->IsRestBetweenWaves() && Flow->GetWaveIndex() == 2,
				FString::Printf(TEXT("wave 1 cleared: straight into the rest before wave 2 (phase %s, wave %d)"),
					*UEnum::GetValueAsString(Flow->GetPhase()), Flow->GetWaveIndex()));
			Check(State, !State.PhasesSeen.Contains(ECodexGamePhase::WaveCleared) && (!Panel || !Panel->IsShown()),
				TEXT("no victory panel / \"Start the next wave\" plate (no WaveCleared phase)"));
			if (UPhaseBannersWidget* Banners = State.Banners.Get())
			{
				Banners->Refresh();
				const FString Combat = Banners->GetCombatText().ToString();
				Check(State, !Banners->IsStartBattleShown() && Combat.Contains(TEXT("NEXT WAVE IN")),
					FString::Printf(TEXT("banner: \"%s\", no Start Battle button"), *Combat));
			}
			Check(State, bRealTimeDilation && Flow->GetPreparationTimeRemaining() > FlowConfig.WaveRestDuration - 1.f,
				FString::Printf(TEXT("the world runs in the rest (dilation %.2f), %.1f of %.0f s left"), WorldDilation(World),
					Flow->GetPreparationTimeRemaining(), FlowConfig.WaveRestDuration));
			SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
			State.RestSeconds = 0.f;
			State.WarningsBefore = Intro->GetWarningCount();
			State.StartedBefore = Intro->GetStartedCount();
			Next(State);
			return true;
		}
		case 6:
		{
			// Nobody touches anything: the rest runs out and wave 2 breaks through on its own.
			if (Flow->GetPhase() == ECodexGamePhase::Preparation)
			{
				State.RestSeconds = State.StageTime;
				return State.StageTime < FlowConfig.WaveRestDuration + 5.f || Finish(State, false);
			}
			// User decision 2026-10-26: a later wave of the fight - no warning at all, only the incoming arrows.
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime
				&& Flow->GetWaveIndex() == 2 && !Flow->IsAwaitingHordeIntro() && Flow->IsFreePauseOffered(),
				FString::Printf(TEXT("wave 2 broke through on its own after the %.1f s rest: real time, no intro armed (the first Space still free)"),
					State.RestSeconds));
			Check(State, State.RestSeconds > FlowConfig.WaveRestDuration - 2.f, FString::Printf(TEXT("... after the level's rest (%.0f s)"),
				FlowConfig.WaveRestDuration));
			Check(State, Intro->GetStage() == EHordeIntroStage::Idle && Intro->GetSilentWaveCount() == 1,
				TEXT("wave 2: the horde intro stays idle (a silent wave, 2026-10-26)"));
			Next(State);
			return true;
		}
		case 7:
		{
			// Watch wave 2 as long as a warning could take: nothing interrupts the fight.
			const bool bQuiet = !Intro->IsIntroPending() && !Intro->IsFlyoverActive() && Flow->GetCombatMode() == ECodexCombatMode::RealTime
				&& bRealTimeDilation && !Camera->IsCinematicLocked() && Camera->GetFollowTarget() == Leader && Intro->GetAlertAlpha() <= 0.f;
			if (!bQuiet)
			{
				Check(State, false, FString::Printf(TEXT("wave 2 at %.1f s: the fight was interrupted (intro %d, mode %d, dilation %.3f, camera locked %d, alert %.2f)"),
					State.StageTime, static_cast<int32>(Intro->GetStage()), static_cast<int32>(Flow->GetCombatMode()), WorldDilation(World),
					Camera->IsCinematicLocked() ? 1 : 0, Intro->GetAlertAlpha()));
				return Finish(State, false);
			}
			if (State.StageTime < Config.MaxWaitSeconds + Config.SpawnGraceSeconds + 1.f)
			{
				return true;
			}
			Check(State, Intro->GetWarningCount() == State.WarningsBefore && Intro->GetStartedCount() == State.StartedBefore,
				FString::Printf(TEXT("wave 2 for %.0f s: no warning, no flyover, no slow motion, no pause, the camera on the squad"), State.StageTime));
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 8:
			// The rest before wave 3 (no panel); cut short like the bot does. The old rule (Codex.HordeIntro.LaterWaves 1) warns it,
			// so the warning's other paths (Esc, the type shots of new types, a wave of known types) are checked on wave 3.
			if (Flow->IsRestBetweenWaves())
			{
				Check(State, !State.PhasesSeen.Contains(ECodexGamePhase::WaveCleared) && Flow->GetWaveIndex() == 3,
					TEXT("wave 2 cleared: the rest before wave 3, no panel"));
				SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
				SetLaterWaves(1);
				Flow->FinishPreparation();
				Check(State, Flow->IsAwaitingHordeIntro() && Flow->GetWaveIndex() == 3 && Intro->GetStage() == EHordeIntroStage::Waiting,
					TEXT("wave 3 (rest cut short, Codex.HordeIntro.LaterWaves 1), intro waiting"));
				State.StartedBefore = Intro->GetStartedCount();
				State.SkippedBefore = Intro->GetSkippedCount();
				Next(State);
				return true;
			}
			return State.StageTime < 5.f || Finish(State, false);
		case 9:
			// Before the trigger: a click is a normal click, Esc gives the warning at once (no flyover, no pause - 2026-10-26).
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
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && !Flow->IsFreePause() && bRealTimeDilation
					&& Flow->IsFreePauseOffered() && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges,
					TEXT("Esc before the trigger: the warning at once, the fight stays in real time (no pause)"));
				Check(State, Intro->GetStage() == EHordeIntroStage::Idle && Intro->GetStartedCount() == State.StartedBefore
					&& Intro->GetAlertAlpha() > 0.f && (!Hud || !Hud->IsPauseMenuOpen()),
					TEXT("no flyover, the alert shows, no pause menu"));
				SmokeUtils::ReleaseHordeIntroPause(Flow);
				Waves->ClearAllEnemies();
				Next(State);
				return true;
			}
			default:
				return true;
			}
		case 10:
		case 13:
			// A load resumes wave 3 in its preparation: it comes suddenly again on the next flow tick.
			if (State.SubStep == 0)
			{
				// The last wave cleared: post-combat at once since 2026-10-22 (no victory panel), the panel on longer levels.
				if (Flow->GetPhase() != ECodexGamePhase::WaveCleared && Flow->GetPhase() != ECodexGamePhase::PostCombat)
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
			State.StartedBefore = Intro->GetStartedCount();
			State.SkippedBefore = Intro->GetSkippedCount();
			State.WarningsBefore = Intro->GetWarningCount();
			Next(State);
			return true;
		case 11:
		{
			if (!Intro->IsFlyoverActive())
			{
				return State.StageTime < Config.MaxWaitSeconds + Config.SpawnGraceSeconds + 4.f || Finish(State, false);
			}
			// 2026-10-25: the level showed its horde once - a later flyover flies only to its NEW types (wave 2 came silently, so wave 3's
			// cutters / marksmen are new), no wide shot.
			const TSet<EEnemyArchetype> Shown1 = { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten };
			bool bNoOld = true;
			bool bNoWide = true;
			int32 TypeShots = 0;
			for (const FHordeIntroShot& Shot : Intro->GetShots())
			{
				bNoOld &= !(Shot.Kind == EHordeIntroShotKind::Type && Shown1.Contains(Shot.Archetype));
				bNoWide &= Shot.Kind != EHordeIntroShotKind::Wide;
				TypeShots += Shot.Kind == EHordeIntroShotKind::Type ? 1 : 0;
			}
			Check(State, bNoOld && bNoWide && TypeShots >= 1 && Intro->GetShots()[0].Kind == EHordeIntroShotKind::Type
				&& Intro->GetShots().Last().Kind == EHordeIntroShotKind::Return,
				FString::Printf(TEXT("loaded wave 3 shots \"%s\": only the new types, no wide shot of the horde again"), *ShotLabels(*Intro)));
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && !Flow->IsFreePause(), TEXT("... the flyover runs in real time (no pause)"));
			Next(State);
			return true;
		}
		case 12:
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
			Check(State, !Intro->IsIntroPending() && Intro->GetSkippedCount() == State.SkippedBefore + 1, TEXT("Esc skipped the flyover"));
			Check(State, Camera->GetFollowTarget() == Leader && FVector::Dist2D(Camera->GetFocus(), Leader->GetActorLocation()) < 120.f
				&& !Camera->IsCinematicLocked(), TEXT("camera at once on the leader, unlocked"));
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && !Flow->IsFreePause() && bRealTimeDilation,
				TEXT("real time at once (dilation 1, no pause)"));
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
		case 14:
		{
			// Every type of wave 3 was shown on this level: at the trigger the alert, NO flyover (2026-10-25), real time (2026-10-26).
			if (Intro->GetWarningCount() == State.WarningsBefore || Intro->IsIntroPending())
			{
				if (Intro->IsFlyoverActive())
				{
					Check(State, false, FString::Printf(TEXT("wave 3 of known types flew over again: \"%s\""), *ShotLabels(*Intro)));
					SetLaterWaves(0);
					return Finish(State, false);
				}
				return State.StageTime < Config.MaxWaitSeconds + Config.SpawnGraceSeconds + 4.f || Finish(State, false);
			}
			FString Known;
			for (const EEnemyArchetype Type : LiveTypes(World))
			{
				Known += FString::Printf(TEXT("%s%s%s"), Known.IsEmpty() ? TEXT("") : TEXT(", "),
					*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Type)),
					Intro->GetShownTypes().Contains(Type) ? TEXT("") : TEXT(" (NEW)"));
			}
			Check(State, HordeIntroRules::BuildShotList(LiveTypes(World), Intro->GetMemory(), Config).IsEmpty(),
				FString::Printf(TEXT("wave 3 holds only types shown on this level: %s"), *Known));
			Check(State, !Intro->IsFlyoverActive() && Intro->GetStage() == EHordeIntroStage::Idle && Intro->GetStartedCount() == State.StartedBefore,
				FString::Printf(TEXT("wave 3 of known types: no flyover (%d flyovers in all)"), Intro->GetStartedCount()));
			Check(State, Intro->GetAlertWidget() && Intro->GetAlertWidget()->IsInViewport() && Intro->GetAlertTitle().ToString() == TEXT("HORDE INCOMING"),
				TEXT("... the «HORDE INCOMING» alert still shows"));
			Check(State, bRealTimeDilation && Flow->GetCombatMode() == ECodexCombatMode::RealTime && Flow->IsFreePauseOffered(),
				TEXT("... real time, no pause (the first Space offered free)"));
			Check(State, !Camera->IsCinematicLocked() && Camera->GetFollowTarget() == Leader, TEXT("... the camera stays on the squad, unlocked"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->IsFreePause() && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges, TEXT("Space: the free pause"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && FMath::IsNearlyEqual(WorldDilation(World), 1.f, 0.001f),
				TEXT("Space again resumes real time"));
			Flow->OnBeforeGameFlowChanged.Remove(State.PhaseHandle);
			SetLaterWaves(0);
			return Finish(State, true);
		}
		default:
			SetLaterWaves(0);
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
