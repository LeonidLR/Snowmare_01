// Dev-only console command for a visual HUD / stance check (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.HudShot [close]"
// "turnbased": Gorky 17 grid with one enemy. "cutscene": pre-combat cutscene card; "prep": preparation banner. "dialogue": the intro briefing in the bottom window. "failed": an operative dies -> mission-failed screen. "mainmenu" (with -ForceMainMenu): the start menu. "weapons": the weapon selector open. "grenade": the grenade aim. "inventory": the inventory drawer open. "transfer": the hand-over dialog open. "pause" / "saves": the pause menu / the save dialog (a quicksave first). "ring": tactical pause + barricade placement radius ring. "susanin": the Susanin rescue event (distress dialogue). "floating": floating combat texts. "rage": the commander in rage. "labels": overhead labels of enemies and deployables. "hold": the Space-hold dome and charge bar. "profile": the commander levelled up, profile open.
// "shoot": Ctrl + click shot at a barrel with the world slowed down, to see the tracer, target flash and a plan marker.
// Otherwise puts the squad into all three stances, posts a feed message, saves Saved/Screenshots/.../HudShot.png and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Data/DialogueSequenceAsset.h"
#include "UI/DialogueSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/WaveSubsystem.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/RageComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Interactables/TurretActor.h"
#include "Interactables/BarricadeActor.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/OverheadLabel.h"
#include "GameFramework/WorldSettings.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/ActionBarWidget.h"
#include "UI/PauseMenuWidget.h"
#include "Core/SaveGameSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "UI/CodexTacticsHUD.h"
#include "UnrealClient.h"

namespace HudShot
{
	void Pose(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		const EOperativeStance Stances[] = { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			Members[Index]->SetStance(Stances[Index % 3]);
		}
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(FText::FromString(TEXT("ШТАБ")), FText::FromString(TEXT("✅ Проверка HUD: лента сообщений и стойки отряда.")));
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		// "mainmenu" (run with -ForceMainMenu): the world is paused behind the start menu, so use real time.
		if (Args.Contains(TEXT("mainmenu")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("HudShot.png"), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					FPlatformMisc::RequestExit(false, TEXT("HudShot"));
					return false;
				}), 1.5f);
				return false;
			}), 4.f);
			return;
		}
		// "close": zoom the tactical camera fully in to inspect characters.
		if (Args.Contains(TEXT("close")))
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->AddZoomNotches(-30.f);
				}
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		const bool bWalk = Args.Contains(TEXT("walk"));
		const bool bMenu = Args.Contains(TEXT("menu"));
		const bool bPlace = Args.Contains(TEXT("place"));
		const bool bShoot = Args.Contains(TEXT("shoot"));
		const bool bFailed = Args.Contains(TEXT("failed"));
		if (Args.Contains(TEXT("turnbased")))
		{
			TWeakObjectPtr<UWorld> TbWorld(World);
			FTimerHandle TbHandle;
			World->GetTimerManager().SetTimer(TbHandle, FTimerDelegate::CreateLambda([TbWorld]()
			{
				UWorld* W = TbWorld.Get();
				USquadSubsystem* SquadSystem = W ? W->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = SquadSystem ? SquadSystem->GetLeader() : nullptr;
				if (!Lead)
				{
					return;
				}
				UGameFlowSubsystem* Flow = W->GetSubsystem<UGameFlowSubsystem>();
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				for (TActorIterator<AEnemyCharacter> It(W); It; ++It)
				{
					It->Destroy();
				}
				W->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, Lead->GetActorLocation() + Lead->GetActorForwardVector() * 600.f);
				Flow->RequestEnterTurnBased(true);
			}), 2.f, false);
		}
		if (Args.Contains(TEXT("cutscene")) || Args.Contains(TEXT("prep")))
		{
			// The cutscene lasts 4 s: start it shortly before the screenshot (4.5 s).
			const bool bPrep = Args.Contains(TEXT("prep"));
			TWeakObjectPtr<UWorld> FlowWorld(World);
			FTimerHandle FlowHandle;
			World->GetTimerManager().SetTimer(FlowHandle, FTimerDelegate::CreateLambda([FlowWorld, bPrep]()
			{
				if (UGameFlowSubsystem* Flow = FlowWorld.IsValid() ? FlowWorld->GetSubsystem<UGameFlowSubsystem>() : nullptr)
				{
					Flow->TriggerCombatZone();
					if (bPrep)
					{
						Flow->FinishCutscene();
					}
				}
			}), 3.f, false);
		}
		if (Args.Contains(TEXT("grenade")))
		{
			// Grenade aim 7 m ahead of the commander (the controller keeps it on the cursor afterwards).
			TWeakObjectPtr<UWorld> AimWorld(World);
			FTimerHandle AimHandle;
			World->GetTimerManager().SetTimer(AimHandle, FTimerDelegate::CreateLambda([AimWorld]()
			{
				USquadSubsystem* Squad = AimWorld.IsValid() ? AimWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				UGrenadeSubsystem* Grenades = AimWorld.IsValid() ? AimWorld->GetSubsystem<UGrenadeSubsystem>() : nullptr;
				if (Lead && Grenades)
				{
					Lead->SwitchToWeaponById(TEXT("grenade"));
					Grenades->StartAim(Lead);
					Grenades->UpdateAim(Lead->GetActorLocation() + Lead->GetActorForwardVector() * 700.f);
				}
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("hold")))
		{
			// Space held ~0.9 s at the regular 4.5 s shot: the dome, the rings and the charge bar.
			TWeakObjectPtr<UWorld> HoldWorld(World);
			FTimerHandle HoldHandle;
			World->GetTimerManager().SetTimer(HoldHandle, FTimerDelegate::CreateLambda([HoldWorld]()
			{
				if (ACodexTacticsPlayerController* HoldPC = HoldWorld.IsValid() ? Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(HoldWorld.Get(), 0)) : nullptr)
				{
					HoldPC->SpacePressed();
				}
			}), 3.6f, false);
		}
		if (Args.Contains(TEXT("labels")))
		{
			// Overhead labels: three enemy types, a barricade and a turret in front of the commander.
			TWeakObjectPtr<UWorld> LabelWorld(World);
			FTimerHandle LabelHandle;
			World->GetTimerManager().SetTimer(LabelHandle, FTimerDelegate::CreateLambda([LabelWorld]()
			{
				USquadSubsystem* Squad = LabelWorld.IsValid() ? LabelWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				if (!Lead)
				{
					return;
				}
				// Around the leader as the camera sees it (screen up / right).
				const APlayerController* PC = UGameplayStatics::GetPlayerController(LabelWorld.Get(), 0);
				const FRotator View = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : Lead->GetActorRotation();
				const FVector Forward = FRotator(0.f, View.Yaw, 0.f).Vector();
				const FVector Right = FRotator(0.f, View.Yaw + 90.f, 0.f).Vector();
				UWaveSubsystem* Waves = LabelWorld->GetSubsystem<UWaveSubsystem>();
				const EEnemyArchetype Types[] = { EEnemyArchetype::FrostHound, EEnemyArchetype::Spitter, EEnemyArchetype::Brute };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (AEnemyCharacter* Enemy = Waves->SpawnEnemy(Types[Index], Lead->GetActorLocation() + Forward * 230.f + Right * (Index - 1) * 260.f))
					{
						Enemy->CustomTimeDilation = 0.f;
					}
				}
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Ground = Lead->GetActorLocation() - FVector(0.f, 0.f, Lead->GetSimpleCollisionHalfHeight());
				LabelWorld->SpawnActor<ABarricadeActor>(Ground - Forward * 180.f - Right * 330.f + FVector(0.f, 0.f, 50.f), FRotator(0.f, View.Yaw, 0.f), Params);
				for (TActorIterator<AEnemyCharacter> It(LabelWorld.Get()); It; ++It)
				{
					FOverheadLabel Label;
					const bool bLabel = It->GetOverheadLabel(Label);
					UE_LOG(LogCodexTactics, Display, TEXT("HudShot label: %s at (%.0f, %.0f, %.0f) label=%d '%s' leader (%.0f, %.0f)"), *It->GetName(),
						It->GetActorLocation().X, It->GetActorLocation().Y, It->GetActorLocation().Z, bLabel ? 1 : 0, *Label.Text,
						Lead->GetActorLocation().X, Lead->GetActorLocation().Y);
				}
				LabelWorld->SpawnActor<ATurretActor>(Ground - Forward * 180.f + Right * 330.f + FVector(0.f, 0.f, 60.f), FRotator(0.f, View.Yaw, 0.f), Params);
			}), 3.f, false);
		}
		if (Args.Contains(TEXT("rage")))
		{
			// The commander in rage: badge over the name plate, «В ЯРОСТИ!» rising.
			TWeakObjectPtr<UWorld> RageWorld(World);
			FTimerHandle RageHandle;
			World->GetTimerManager().SetTimer(RageHandle, FTimerDelegate::CreateLambda([RageWorld]()
			{
				USquadSubsystem* Squad = RageWorld.IsValid() ? RageWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				if (AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr; Lead && Lead->RageComponent)
				{
					Lead->RageComponent->EnterRage();
				}
			}), 4.2f, false);
		}
		if (Args.Contains(TEXT("floating")))
		{
			// Floating combat texts over the squad and an enemy, shot while they rise.
			TWeakObjectPtr<UWorld> FloatWorld(World);
			FTimerHandle FloatHandle;
			World->GetTimerManager().SetTimer(FloatHandle, FTimerDelegate::CreateLambda([FloatWorld]()
			{
				USquadSubsystem* Squad = FloatWorld.IsValid() ? FloatWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				if (!Squad || Squad->GetMembers().Num() < 3)
				{
					return;
				}
				TArray<AOperativeCharacter*> Members = Squad->GetMembers();
				Members[0]->ForcedDodgeRollForTesting = 0.f;
				Members[0]->TakeHit(24.f, TEXT("HudShot"), true);
				Members[1]->ForcedDodgeRollForTesting = 1.f;
				Members[1]->TakeHit(20.f, TEXT("HudShot"));
				UFloatingTextSubsystem::SpawnAboveOperative(Members[2], TEXT("❄️ ОСЕЧКА! (Затвор заклинил)"), FLinearColor(0.4f, 0.85f, 1.f));
				if (AEnemyCharacter* Brute = FloatWorld->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute,
					Members[0]->GetActorLocation() + Members[0]->GetActorForwardVector() * 500.f))
				{
					Brute->CustomTimeDilation = 0.f;
					FDamageSpec Spec;
					Spec.Amount = 60.f;
					Brute->FindComponentByClass<UHealthComponent>()->TakeDamage(Spec);
				}
			}), 4.3f, false); // the regular shot at 4.5 s catches them rising
		}
		if (Args.Contains(TEXT("susanin")))
		{
			// The rescue event: Susanin freezing at his spot with the distress dialogue (narrative pause: real-time shot).
			TWeakObjectPtr<UWorld> SusWorld(World);
			FTimerHandle SusHandle;
			World->GetTimerManager().SetTimer(SusHandle, FTimerDelegate::CreateLambda([SusWorld]()
			{
				URecruitSubsystem* Recruits = SusWorld.IsValid() ? SusWorld->GetSubsystem<URecruitSubsystem>() : nullptr;
				if (!Recruits)
				{
					return;
				}
				UGameFlowSubsystem* Flow = SusWorld->GetSubsystem<UGameFlowSubsystem>();
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				for (TActorIterator<AEnemyCharacter> It(SusWorld.Get()); It; ++It)
				{
					It->CustomTimeDilation = 0.f;
				}
				Recruits->TriggerRescueEvent();
				TSharedRef<int32> Frames = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Frames](float)
				{
					++(*Frames);
					if (*Frames == 30)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("HudShot.png"), true, false);
					}
					if (*Frames >= 60)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					return true;
				}), 0.05f);
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("ring")))
		{
			// Tactical pause + barricade placement: the leader's radius ring (the pause slows the world: shoot on real time).
			TWeakObjectPtr<UWorld> RingWorld(World);
			FTimerHandle RingHandle;
			World->GetTimerManager().SetTimer(RingHandle, FTimerDelegate::CreateLambda([RingWorld]()
			{
				USquadSubsystem* Squad = RingWorld.IsValid() ? RingWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				if (!Lead)
				{
					return;
				}
				UGameFlowSubsystem* Flow = RingWorld->GetSubsystem<UGameFlowSubsystem>();
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				for (TActorIterator<AEnemyCharacter> It(RingWorld.Get()); It; ++It)
				{
					It->CustomTimeDilation = 0.f; // killing the wave would end the fight (and the pause)
				}
				Flow->ToggleTacticalPause();
				URelocationSubsystem* Relocation = RingWorld->GetSubsystem<URelocationSubsystem>();
				Lead->BarricadesCount = FMath::Max(Lead->BarricadesCount, 1);
				Relocation->StartDeployPlacement(EDeployableType::Barricade, Lead);
				const FVector ToFloor = (SmokeUtils::LevelPoint(RingWorld.Get(), FVector::ZeroVector) - Lead->GetActorLocation()).GetSafeNormal2D();
				Relocation->UpdatePreview(Lead->GetActorLocation() + (ToFloor.IsNearlyZero() ? Lead->GetActorForwardVector() : ToFloor) * 400.f);
				TSharedRef<int32> Frames = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Frames](float)
				{
					++(*Frames);
					if (*Frames == 10)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("HudShot.png"), true, false);
					}
					if (*Frames >= 40)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					return true;
				}), 0.05f);
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("pause")) || Args.Contains(TEXT("saves")))
		{
			// The pause stops the world timers: open it last, shoot and quit on the real-time ticker.
			const bool bSaves = Args.Contains(TEXT("saves"));
			TWeakObjectPtr<UWorld> PauseWorld(World);
			FTimerHandle PauseHandle;
			World->GetTimerManager().SetTimer(PauseHandle, FTimerDelegate::CreateLambda([PauseWorld, bSaves]()
			{
				APlayerController* PC = PauseWorld.IsValid() ? UGameplayStatics::GetPlayerController(PauseWorld.Get(), 0) : nullptr;
				ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
				if (!Hud)
				{
					return;
				}
				if (bSaves)
				{
					if (USaveGameSubsystem* Saves = PauseWorld->GetSubsystem<USaveGameSubsystem>())
					{
						Saves->QuickSave(); // at least one card
					}
				}
				Hud->HandleEscape();
				if (bSaves && Hud->GetPauseMenu())
				{
					Hud->GetPauseMenu()->OpenSave();
				}
				TSharedRef<int32> Frames = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Frames](float)
				{
					++(*Frames);
					if (*Frames == 10)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("HudShot.png"), true, false);
					}
					if (*Frames >= 40)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					return true;
				}), 0.05f);
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("profile")))
		{
			// Level 2 with one point in luck, then the profile (Godot key P).
			TWeakObjectPtr<UWorld> ProfileWorld(World);
			FTimerHandle ProfileHandle;
			World->GetTimerManager().SetTimer(ProfileHandle, FTimerDelegate::CreateLambda([ProfileWorld]()
			{
				USquadSubsystem* Squad = ProfileWorld.IsValid() ? ProfileWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				APlayerController* PC = ProfileWorld.IsValid() ? UGameplayStatics::GetPlayerController(ProfileWorld.Get(), 0) : nullptr;
				ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
				if (Lead && Hud)
				{
					Lead->AddExp(320);
					Lead->IncreaseStat(EProgressStat::Luck);
					Hud->ToggleProfileDialog();
				}
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("transfer")))
		{
			TWeakObjectPtr<UWorld> TrWorld(World);
			FTimerHandle TrHandle;
			World->GetTimerManager().SetTimer(TrHandle, FTimerDelegate::CreateLambda([TrWorld]()
			{
				APlayerController* PC = TrWorld.IsValid() ? UGameplayStatics::GetPlayerController(TrWorld.Get(), 0) : nullptr;
				if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
				{
					Hud->ToggleTransferDialog();
				}
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("inventory")))
		{
			TWeakObjectPtr<UWorld> InvWorld(World);
			FTimerHandle InvHandle;
			World->GetTimerManager().SetTimer(InvHandle, FTimerDelegate::CreateLambda([InvWorld]()
			{
				APlayerController* PC = InvWorld.IsValid() ? UGameplayStatics::GetPlayerController(InvWorld.Get(), 0) : nullptr;
				if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
				{
					Hud->ToggleInventoryDrawer();
				}
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("weapons")))
		{
			// The weapon selector above the action bar.
			TWeakObjectPtr<UWorld> WeaponWorld(World);
			FTimerHandle WeaponHandle;
			World->GetTimerManager().SetTimer(WeaponHandle, FTimerDelegate::CreateLambda([WeaponWorld]()
			{
				APlayerController* PC = WeaponWorld.IsValid() ? UGameplayStatics::GetPlayerController(WeaponWorld.Get(), 0) : nullptr;
				const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
				if (UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr)
				{
					Bar->ToggleWeaponSelector();
				}
			}), 3.5f, false);
		}
		if (Args.Contains(TEXT("dialogue")))
		{
			if (const UDialogueSequenceAsset* Intro = LoadObject<UDialogueSequenceAsset>(nullptr, TEXT("/Game/Data/Dialogues/DA_DialogueIntro.DA_DialogueIntro")))
			{
				World->GetSubsystem<UDialogueSubsystem>()->StartDialogue(Intro);
				World->GetSubsystem<UDialogueSubsystem>()->AdvanceLine(); // the commander's line
			}
		}
		FTimerHandle PoseHandle;
		World->GetTimerManager().SetTimer(PoseHandle, FTimerDelegate::CreateLambda([WeakWorld, bWalk, bMenu, bPlace, bShoot, bFailed]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			USquadSubsystem* Squad = W->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if ((bShoot || bFailed) && Leader)
			{
				// World time stops (slowed shot / game over): take the screenshot and exit on real time.
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					const FString File = FPaths::ScreenShotDir() / TEXT("HudShot.png");
					FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}), 1.5f);
					return false;
				}), 0.5f);
			}
			if (bFailed && Squad && Squad->GetMembers().Num() > 1)
			{
				Squad->GetMembers()[1]->HealthComponent->ApplyDirectHealthLoss(10000.f, TEXT("HudShot"));
			}
			else if (bShoot && Leader)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					UCombatFeedbackSubsystem* Feedback = W->GetSubsystem<UCombatFeedbackSubsystem>();
					Feedback->SpawnWaypointMarker(Leader->GetActorLocation() - Leader->GetActorRightVector() * 300.f
						- FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight()));
					W->GetWorldSettings()->SetTimeDilation(0.01f); // freeze the fading effects for the screenshot
					Feedback->HighlightTarget(Barrel);
					Leader->ShootAtObject(Barrel);
				}
			}
			else if (bPlace && Leader)
			{
				// "place": placement mode for a barrel, ghost 4 m to the side.
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 250.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					URelocationSubsystem* Relocation = W->GetSubsystem<URelocationSubsystem>();
					Relocation->StartRelocate(Barrel, Leader);
					Relocation->UpdatePreview(Barrel->GetActorLocation() - Leader->GetActorRightVector() * 400.f);
				}
			}
			else if (bMenu && Leader)
			{
				// "menu": a barrel right in front of the leader, its action menu opens at once.
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 190.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					W->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(Barrel);
				}
			}
			else if (bWalk && Leader)
			{
				// "walk": the leader walks and the squad follows, to inspect locomotion.
				Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1500.f, false);
			}
			else
			{
				Pose(W);
			}
		}), 3.f, false);
		FTimerHandle ShotHandle;
		World->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateLambda([]()
		{
			const FString File = FPaths::ScreenShotDir() / TEXT("HudShot.png");
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("HudShot saved to %s"), *File);
		}), 4.5f, false);
		FTimerHandle ExitHandle;
		World->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateLambda([]()
		{
			FPlatformMisc::RequestExit(false, TEXT("HudShot"));
		}), 6.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HudShot"),
		TEXT("Dev check: squad in all stances + feed message, saves HudShot.png, then exits (needs rendering)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
