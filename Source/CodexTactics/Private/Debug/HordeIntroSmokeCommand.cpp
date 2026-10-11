// Dev-only headless check of the sudden horde start (user decision 2026-10-11, UHordeIntroSubsystem) on L_MovementTest
// with its level waves (wave 1: hounds + frostbitten; wave 2 adds cutters + marksmen; wave 3: hounds, cutters, marksmen):
//   Scripts/smoke.ps1 -Command CodexTactics.HordeIntroSmoke
// 1. the combat zone -> wave 1 spawns AT ONCE (no cutscene / preparation phase, nothing queued), the fight opens in the
//    FREE tactical pause (pause dilation, charges 3/3 kept, no timer), saving refused, the banner says "free pause"
//    (the autosave at the spawn moment: SaveRoundTripSmoke);
// 2. 1.5-2 s later the flyover runs on real time while the world stays slowed: wide shot on the moving anchor between
//    the squad and the horde, then one medium shot per type in spawn order (hound, frostbitten; a live representative of
//    that type followed), then back to the leader (camera input locked meanwhile, unlocked after); the world stays at the
//    pause dilation the whole time; still the free pause with 3/3 charges;
// 3. orders in the free pause: a planned move and a barricade set-up are planned; a Space tap releases the pause: real
//    time, charges still 3/3, the plans run;
// 4. wave 2 comes at once in a new free pause: the flyover shows only the new types (cutter, marksman); Esc (simulated
//    key) skips it during a type shot: camera at once on the leader, unlocked, still the free pause, no pause menu;
// 5. wave 3 (only known types): a short wide shot only; a left click (simulated) skips it without giving an order.

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
#include "Combat/WaveVictorySubsystem.h"
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
		int32 AutosavesBefore = 0;
		int32 LastShot = INDEX_NONE;
		float ShotStartTime = 0.f;
		int32 VerifiedShot = INDEX_NONE;
		TMap<TWeakObjectPtr<AEnemyCharacter>, FVector> SpawnSpots;
		float FlyoverStartReal = -1.f;
		FVector PlannedMove = FVector::ZeroVector;
		float MoveStartDistance = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Mover;
		bool bKeyDown = false;
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
				FString::Printf(TEXT("%s: camera on the moving anchor at %.0f m"), *Label, Camera.GetCurrentDistance() / 100.f));
			break;
		case EHordeIntroShotKind::Type:
		{
			const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Target);
			Check(State, Enemy && Enemy->GetArchetype() == Shot.Archetype && Camera.GetFollowTarget() == Enemy && !Enemy->IsHidden()
				&& FMath::IsNearlyEqual(Camera.GetCurrentDistance(), Config.TypeShotDistance, 60.f)
				&& FVector::Dist2D(Camera.GetFocus(), Enemy->GetActorLocation()) < 150.f,
				FString::Printf(TEXT("%s: camera follows %s at %.1f m (medium framing)"), *Label, Enemy ? *Enemy->GetName() : TEXT("-"),
					Camera.GetCurrentDistance() / 100.f));
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

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 150.f)
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
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				State.AutosavesBefore = Saves->GetAutosaveCount();
			}
			State.PhaseHandle = Flow->OnBeforeGameFlowChanged.AddLambda([&State](ECodexGamePhase, ECodexGamePhase NewPhase, ECodexCombatMode)
			{
				State.PhasesSeen.Add(NewPhase);
			});
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration before the horde"));
			Check(State, Flow->TriggerCombatZone() == EGameFlowResult::Ok, TEXT("combat zone triggered"));
			Check(State, State.PhasesSeen.Num() == 1 && State.PhasesSeen[0] == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("straight into the wave: %d phase change(s), no cutscene / preparation"), State.PhasesSeen.Num()));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause
				&& Flow->IsFreePause() && Flow->GetWaveIndex() == 1, TEXT("wave 1 opens in the free tactical pause"));
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			const int32 Expected = Level && Level->Config.Waves.Num() > 0 ? Level->Config.Waves[0].GetTotalEnemyCount() : -1;
			Check(State, Waves->GetAliveEnemyCount() > 0 && Waves->GetAliveEnemyCount() == Expected && Waves->GetRemainingSpawnCount() == 0,
				FString::Printf(TEXT("the whole wave spawned at once: %d alive (level wave 1: %d), %d queued"), Waves->GetAliveEnemyCount(),
					Expected, Waves->GetRemainingSpawnCount()));
			Check(State, FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f),
				FString::Printf(TEXT("pause dilation %.3f"), WorldDilation(World)));
			Check(State, Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges && Flow->GetPauseCooldownRemaining() <= 0.f,
				FString::Printf(TEXT("free: charges %d/%d"), Flow->GetPauseCharges(), FlowConfig.TacticalPauseMaxCharges));
			if (const USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				// The autosave itself (off in smokes: shared Saved/ files) is checked by SaveRoundTripSmoke.
				Check(State, !Saves->CanSaveNow(), TEXT("no saving in the fight (free pause)"));
			}
			Check(State, Intro->IsIntroPending() && !Intro->IsFlyoverActive(), TEXT("flyover pending"));
			const FString Banner = State.Banners.IsValid() ? State.Banners->GetPauseText().ToString() : FString();
			Check(State, Banner.Contains(TEXT("free pause")), FString::Printf(TEXT("banner: %s"), *Banner));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				State.SpawnSpots.Add(*It, It->GetActorLocation());
			}
			Next(State);
			return true;
		}
		case 1:
			// Wait for the flyover, then follow it shot by shot.
			if (!Intro->IsFlyoverActive() && State.FlyoverStartReal < 0.f)
			{
				if (State.StageTime > 4.f)
				{
					Check(State, false, TEXT("the flyover never started"));
					return Finish(State, false);
				}
				return true;
			}
			if (State.FlyoverStartReal < 0.f)
			{
				State.FlyoverStartReal = Intro->GetSecondsSinceSpawn();
				Check(State, State.FlyoverStartReal >= 1.4f && State.FlyoverStartReal <= 2.3f,
					FString::Printf(TEXT("flyover starts %.2f s (real) after the spawn"), State.FlyoverStartReal));
				Check(State, Camera->IsCinematicLocked(), TEXT("camera input locked during the flyover"));
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				bool bAllShown = true;
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					bAllShown &= It->IsDying() || !It->IsHidden();
				}
				Check(State, (!Sight || Sight->IsCinematicReveal()) && bAllShown, TEXT("the flyover reveals the whole horde (line of sight overridden)"));
				const TArray<EEnemyArchetype> Types = LiveTypes(World);
				TArray<FHordeIntroShot> Expected = HordeIntroRules::BuildShotList(Types, TSet<EEnemyArchetype>(), Config);
				FString ExpectedLabels;
				for (const FHordeIntroShot& Shot : Expected)
				{
					ExpectedLabels += (ExpectedLabels.IsEmpty() ? TEXT("") : TEXT(" ")) + HordeIntroRules::ShotLabel(Shot);
				}
				Check(State, ShotLabels(*Intro) == ExpectedLabels && Types.Num() >= 2,
					FString::Printf(TEXT("shots \"%s\" (wide + one per type in spawn order + return)"), *ShotLabels(*Intro)));
			}
			if (Intro->IsFlyoverActive() && !Flow->IsFreePause())
			{
				Check(State, false, TEXT("the free pause ended during the flyover"));
			}
			if (Intro->IsFlyoverActive() && !FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f))
			{
				Check(State, false, FString::Printf(TEXT("world left the pause dilation during the flyover (%.3f)"), WorldDilation(World)));
			}
			if (!TrackFlyover(State, *Intro, *Camera, Leader))
			{
				return true;
			}
			{
				Check(State, Intro->GetCompletedCount() == 1 && Intro->GetSkippedCount() == 0, TEXT("flyover played to the end"));
				Check(State, !Camera->IsCinematicLocked() && Camera->GetFollowTarget() == Leader, TEXT("camera back on the leader, input unlocked"));
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				Check(State, !Sight || !Sight->IsCinematicReveal(), TEXT("the squad's own line of sight is back"));
				Check(State, Flow->IsFreePause() && Flow->GetPauseCharges() == FlowConfig.TacticalPauseMaxCharges
					&& FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f),
					TEXT("still the free pause after the flyover (no timer, charges 3/3, pause dilation)"));
				for (const EEnemyArchetype Type : LiveTypes(World))
				{
					Check(State, Intro->GetShownTypes().Contains(Type), FString::Printf(TEXT("type %s marked shown"),
						*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Type))));
				}
				float Sum = 0.f;
				int32 Count = 0;
				for (const TPair<TWeakObjectPtr<AEnemyCharacter>, FVector>& Spot : State.SpawnSpots)
				{
					if (const AEnemyCharacter* Enemy = Spot.Key.Get())
					{
						Sum += FVector::Dist2D(Enemy->GetActorLocation(), Spot.Value);
						++Count;
					}
				}
				// Informational: at the pause dilation (0.02) the ~11 real s of the flyover are ~0.2 s of game time, so the
				// horde barely moves while it is filmed (and needs its first perception update to pick the squad).
				const float Mean = Count > 0 ? Sum / Count : 0.f;
				Check(State, Mean < 600.f, FString::Printf(TEXT("the horde stays slowed during %.1f real s (mean move %.1f cm)"),
					Intro->GetSecondsSinceSpawn(), Mean));
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
			// Space tap: the pause ends like any tactical pause.
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
			// The plan runs once the pause ends: he walks towards the point (the rushing horde may crowd him later on).
			const AOperativeCharacter* Mover = State.Mover.Get();
			const float Left = Mover ? FVector::Dist2D(Mover->GetActorLocation(), State.PlannedMove) : -1.f;
			const bool bOnTheWay = Mover && (Mover->IsMoving() || Left < 150.f || State.MoveStartDistance - Left > 60.f);
			if (!bOnTheWay && State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, bOnTheWay, FString::Printf(TEXT("the planned move runs (moving %d, %.0f of %.0f cm left)"),
				Mover && Mover->IsMoving() ? 1 : 0, Left, State.MoveStartDistance));
			// Wave 1 down -> wave 2 at once.
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 4:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Check(State, Flow->AdvanceAfterWave() == EGameFlowResult::Ok, TEXT("next wave"));
				Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsFreePause() && Flow->GetWaveIndex() == 2,
					TEXT("wave 2 comes at once (no rest countdown), free pause again"));
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
				return State.StageTime < 4.f || Finish(State, false);
			}
			const TSet<EEnemyArchetype> Shown1 = { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten };
			bool bNoOld = true;
			int32 TypeShots = 0;
			for (const FHordeIntroShot& Shot : Intro->GetShots())
			{
				bNoOld &= !(Shot.Kind == EHordeIntroShotKind::Type && Shown1.Contains(Shot.Archetype));
				TypeShots += Shot.Kind == EHordeIntroShotKind::Type ? 1 : 0;
			}
			Check(State, bNoOld && TypeShots >= 1, FString::Printf(TEXT("wave 2 shots \"%s\": only the new types"), *ShotLabels(*Intro)));
			Next(State);
			return true;
		}
		case 6:
		{
			// Esc during the second type shot (or the last one) skips the rest.
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
			Check(State, Flow->IsFreePause() && FMath::IsNearlyEqual(WorldDilation(World), FlowConfig.TacticalPauseTimeDilation, 0.001f),
				TEXT("the slowed free pause stays"));
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
				Check(State, Flow->IsFreePause() && Flow->GetWaveIndex() == 3, TEXT("wave 3 at once, free pause"));
				Next(State);
				return true;
			}
			return State.StageTime < 5.f || Finish(State, false);
		case 8:
		{
			if (!State.bKeyDown)
			{
				if (!Intro->IsFlyoverActive())
				{
					return State.StageTime < 4.f || Finish(State, false);
				}
				const TArray<FHordeIntroShot>& Shots = Intro->GetShots();
				const bool bKnownOnly = LiveTypes(World).Num() > 0 && Shots.Num() == 2 && Shots[0].Kind == EHordeIntroShotKind::Wide
					&& FMath::IsNearlyEqual(Shots[0].Seconds, Config.RepeatWideShotSeconds) && Shots[1].Kind == EHordeIntroShotKind::Return;
				bool bAllKnown = true;
				for (const EEnemyArchetype Type : LiveTypes(World))
				{
					bAllKnown &= Intro->GetShownTypes().Contains(Type);
				}
				Check(State, !bAllKnown || bKnownOnly, FString::Printf(TEXT("wave 3 (known types: %d): \"%s\""), bAllKnown ? 1 : 0, *ShotLabels(*Intro)));
				Squad->ClearPlannedOrder(Leader);
				PressKey(PC, EKeys::LeftMouseButton, IE_Pressed);
				State.bKeyDown = true;
				return true;
			}
			PressKey(PC, EKeys::LeftMouseButton, IE_Released);
			State.bKeyDown = false;
			Check(State, !Intro->IsIntroPending() && Intro->GetSkippedCount() == 2, TEXT("a click skipped the flyover"));
			Check(State, Camera->GetFollowTarget() == Leader && !Camera->IsCinematicLocked(), TEXT("camera on the leader"));
			Check(State, Squad->GetPlannedOrderCount() == 0 && Flow->IsFreePause(), TEXT("the click gave no order, the free pause stays"));
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
		TEXT("Dev check of the sudden horde start (free tactical pause + camera flyover, skips, next waves); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
