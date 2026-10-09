// Dev-only console command: rendered stills + frame sequences for a remote review (user on a phone, 2026-10-08).
// Needs rendering (no -nullrhi); run offscreen with a fixed frame time so the sequences play back at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MainMenu -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.PhoneShots frontend"
//   ... /Game/Maps/L_MovementTest ... -ExecCmds="CodexTactics.PhoneShots combat"
// Output: Saved/Screenshots/Phone/<name>.png and Saved/Screenshots/Phone/frames/<clip>/f_NNNN.png (encode with ffmpeg).
// The game is driven only from inside the engine (no key presses, no desktop capture).
// "frontend": title, main menu with three selected entries (camera anchors), menu-camera clip, quit confirm dialog,
//   NEW GAME -> pause menu with SAVE enabled, a wave fight -> pause menu with SAVE disabled.
// "combat": bunker camera zone in real time, an operative hit reaction (clip), the engineer's death cinematic (clip),
//   the commander's death -> «THE SQUAD HAS FALLEN» (clip + stills).
// "sniper" (L_MovementTest, 2026-10-09): the Female Soldier Medic-Sapper with the sniper rifle — standing idle, the order while
//   standing (kneel -> shot -> bolt, clip + still), kneeling fire (clip), prone fire (clip + stills); own camera framed on her.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueSubsystem.h"
#include "UI/Frontend/CodexMainMenuScreen.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexTitleScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"
#include "UnrealClient.h"

namespace PhoneShots
{
	/** One step: returns the frames to wait before the next step, or -1 to run again next frame (waiting for a condition). */
	using FStep = TFunction<int32(UWorld*)>;

	struct FRunner
	{
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Wait = 0;
		int32 Frame = 0;
		FString Dir;
		// Frame-sequence capture.
		FString ClipName;
		int32 ClipEvery = 1;
		int32 ClipFrame = 0;
		int32 ClipCount = 0;
		bool bClipShowUI = true;
		FDelegateHandle Handle;
	};

	TSharedPtr<FRunner> Runner;

	void Shot(const FString& Name, bool bShowUI = true)
	{
		const FString Path = Runner->Dir / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, bShowUI, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: still %s"), *Path);
	}

	void StartClip(const FString& Name, int32 Every, bool bShowUI = true)
	{
		Runner->bClipShowUI = bShowUI;
		Runner->ClipName = Name;
		Runner->ClipEvery = FMath::Max(1, Every);
		Runner->ClipFrame = 0;
		Runner->ClipCount = 0;
		IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("frames") / Name), false, true);
	}

	void StopClip()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: clip %s, %d frames"), *Runner->ClipName, Runner->ClipCount);
		Runner->ClipName.Reset();
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: done"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("PhoneShots"));
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		if (!R.ClipName.IsEmpty() && (R.ClipFrame++ % R.ClipEvery) == 0)
		{
			FScreenshotRequest::RequestScreenshot(R.Dir / TEXT("frames") / R.ClipName / FString::Printf(TEXT("f_%04d.png"), R.ClipCount++), R.bClipShowUI, false);
		}
		if (R.Frame > 30 * 240)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: timeout in step %d"), R.Index);
			Finish();
			return;
		}
		if (R.Wait > 0)
		{
			--R.Wait;
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (!World)
		{
			return;
		}
		if (!R.Steps.IsValidIndex(R.Index))
		{
			Finish();
			return;
		}
		const int32 Result = R.Steps[R.Index](World);
		if (Result >= 0)
		{
			++R.Index;
			R.Wait = Result;
		}
	}

	template <typename T>
	T* Top(UWorld* World, FGameplayTag Layer)
	{
		UCodexUISubsystem* UI = UCodexUISubsystem::Get(World);
		return UI ? Cast<T>(UI->GetTopScreen(Layer)) : nullptr;
	}

	ACodexTacticsHUD* HudOf(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
	}

	void StartFight(UWorld* World)
	{
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->Destroy();
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->HealthComponent->SetMaxHealth(5000.f, true);
			Member->bTacticalCeaseFire = true;
		}
		if (AOperativeCharacter* Leader = Squad->GetLeader())
		{
			// One frozen hound far away keeps the wave alive.
			if (AEnemyCharacter* Keeper = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() - Leader->GetActorForwardVector() * 3500.f + FVector(0.f, 0.f, 30.f)))
			{
				Keeper->CustomTimeDilation = 0.f;
			}
		}
	}

	void Zoom(UWorld* World, float Notches)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (ATacticalCameraPawn* Camera = PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr)
		{
			Camera->AddZoomNotches(Notches);
		}
	}

	AOperativeCharacter* Member(UWorld* World, EOperativeRole Role)
	{
		for (AOperativeCharacter* Each : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (Each->SquadRole == Role)
			{
				return Each;
			}
		}
		return nullptr;
	}

	void AddFrontendSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return FrontendSmokeUtils::EnsureFrontend(World) ? 75 : -1; });
		S.Add([](UWorld* World) { Shot(TEXT("01_title_press_any_key")); return 5; });
		S.Add([](UWorld* World)
		{
			UCodexTitleScreen* Title = Top<UCodexTitleScreen>(World, CodexUITags::Layer_Menu);
			if (!Title)
			{
				return -1;
			}
			Title->ContinueFromTitle();
			return 10;
		});
		struct FSelect { ECodexMainMenuEntry Entry; const TCHAR* Name; };
		for (const FSelect& Select : { FSelect{ ECodexMainMenuEntry::NewGame, TEXT("02_main_menu_new_game") },
			FSelect{ ECodexMainMenuEntry::LoadGame, TEXT("03_main_menu_load_game") }, FSelect{ ECodexMainMenuEntry::Credits, TEXT("04_main_menu_credits") } })
		{
			S.Add([Select](UWorld* World)
			{
				UCodexMainMenuScreen* Menu = Top<UCodexMainMenuScreen>(World, CodexUITags::Layer_Menu);
				if (!Menu)
				{
					return -1;
				}
				Menu->SelectMenuEntry(Select.Entry);
				return 60; // blend 1.2 s at 30 fps + settle
			});
			S.Add([Select](UWorld* World) { Shot(Select.Name); return 5; });
		}
		// Clip: the camera gliding between anchors as the selection moves down the list.
		S.Add([](UWorld* World) { StartClip(TEXT("menu_camera"), 2); return 10; });
		for (const ECodexMainMenuEntry Entry : { ECodexMainMenuEntry::Continue, ECodexMainMenuEntry::NewGame, ECodexMainMenuEntry::LoadGame,
			ECodexMainMenuEntry::Options, ECodexMainMenuEntry::Quit })
		{
			S.Add([Entry](UWorld* World)
			{
				if (UCodexMainMenuScreen* Menu = Top<UCodexMainMenuScreen>(World, CodexUITags::Layer_Menu))
				{
					Menu->SelectMenuEntry(Entry);
				}
				return 50;
			});
		}
		S.Add([](UWorld* World) { StopClip(); return 2; });
		S.Add([](UWorld* World)
		{
			if (UCodexMainMenuScreen* Menu = Top<UCodexMainMenuScreen>(World, CodexUITags::Layer_Menu))
			{
				Menu->ActivateMenuEntry(ECodexMainMenuEntry::Quit);
			}
			return 15;
		});
		S.Add([](UWorld* World) { Shot(TEXT("05_confirm_quit_dialog")); return 5; });
		S.Add([](UWorld* World)
		{
			if (UCodexActivatableScreen* Dialog = Top<UCodexActivatableScreen>(World, CodexUITags::Layer_Modal))
			{
				Dialog->RequestBack();
			}
			if (UCodexMainMenuScreen* Menu = Top<UCodexMainMenuScreen>(World, CodexUITags::Layer_Menu))
			{
				Menu->ActivateMenuEntry(ECodexMainMenuEntry::NewGame);
			}
			return 5;
		});
		// The campaign level: skip the intro, Esc -> pause menu (SAVE enabled).
		S.Add([](UWorld* World)
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			return Mission && Mission->IsMissionWorld() ? 90 : -1;
		});
		S.Add([](UWorld* World)
		{
			if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
			{
				Dialogue->SkipDialogue();
			}
			return 30;
		});
		S.Add([](UWorld* World)
		{
			if (ACodexTacticsHUD* Hud = HudOf(World))
			{
				Hud->HandleEscape();
			}
			return 10;
		});
		S.Add([](UWorld* World)
		{
			if (UCodexPauseMenuScreen* Pause = Top<UCodexPauseMenuScreen>(World, CodexUITags::Layer_GameMenu))
			{
				Pause->SelectEntry(TEXT("Save"));
			}
			return 8;
		});
		S.Add([](UWorld* World) { Shot(TEXT("06_pause_menu_save_enabled")); return 5; });
		S.Add([](UWorld* World)
		{
			if (UCodexPauseMenuScreen* Pause = Top<UCodexPauseMenuScreen>(World, CodexUITags::Layer_GameMenu))
			{
				Pause->Resume();
			}
			StartFight(World);
			return 45;
		});
		S.Add([](UWorld* World)
		{
			if (ACodexTacticsHUD* Hud = HudOf(World))
			{
				Hud->HandleEscape();
			}
			return 10;
		});
		S.Add([](UWorld* World)
		{
			if (UCodexPauseMenuScreen* Pause = Top<UCodexPauseMenuScreen>(World, CodexUITags::Layer_GameMenu))
			{
				Pause->SelectEntry(TEXT("Save"));
			}
			return 8;
		});
		S.Add([](UWorld* World) { Shot(TEXT("07_pause_menu_save_disabled_in_combat")); return 10; });
	}

	void AddCombatSteps(TArray<FStep>& S)
	{
		// Bunker camera zone in a real-time fight.
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			StartFight(World);
			TActorIterator<ACameraZoneVolume> It(World);
			AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
			if (It && Leader)
			{
				Leader->StopOperative();
				Leader->TeleportTo(SmokeUtils::FreeSpot(World, It->GetActorLocation(), Leader), Leader->GetActorRotation(), false, true);
			}
			return 45;
		});
		S.Add([](UWorld* World) { Shot(TEXT("08_bunker_camera_zone_real_time")); return 5; });
		// Back out of the zone: the squad at the test start, the engineer walking; hit reaction clip.
		S.Add([](UWorld* World)
		{
			SmokeUtils::PlaceSquadAtTestStart(World);
			return 45;
		});
		S.Add([](UWorld* World)
		{
			if (AOperativeCharacter* Engineer = Member(World, EOperativeRole::Engineer))
			{
				World->GetSubsystem<USquadSubsystem>()->SetLeader(Engineer);
				Engineer->OrderMoveTo(Engineer->GetActorLocation() + Engineer->GetActorRightVector() * 700.f, false);
			}
			Zoom(World, -30.f); // close camera on the walking engineer
			return 30;
		});
		S.Add([](UWorld* World)
		{
			StartClip(TEXT("hit_reaction"), 1);
			return 12;
		});
		S.Add([](UWorld* World)
		{
			if (AOperativeCharacter* Engineer = Member(World, EOperativeRole::Engineer))
			{
				Engineer->TakeHit(12.f, TEXT("PhoneShots"), false, true);
			}
			return 50;
		});
		S.Add([](UWorld* World) { StopClip(); Zoom(World, 12.f); return 15; });
		// The engineer dies: death cinematic (focus + slow motion), then the camera returns.
		S.Add([](UWorld* World)
		{
			StartClip(TEXT("death_cinematic"), 2);
			if (AOperativeCharacter* Commander = Member(World, EOperativeRole::Commander))
			{
				World->GetSubsystem<USquadSubsystem>()->SetLeader(Commander);
			}
			return 6;
		});
		S.Add([](UWorld* World)
		{
			if (AOperativeCharacter* Engineer = Member(World, EOperativeRole::Engineer))
			{
				Engineer->TakeHit(6000.f, TEXT("PhoneShots"), false, true);
			}
			return 210;
		});
		S.Add([](UWorld* World) { StopClip(); return 30; });
		// The commander dies: focus, fade, «THE SQUAD HAS FALLEN».
		S.Add([](UWorld* World)
		{
			StartClip(TEXT("squad_fallen"), 3);
			if (AOperativeCharacter* Commander = Member(World, EOperativeRole::Commander))
			{
				Commander->TakeHit(6000.f, TEXT("PhoneShots"), false, true);
			}
			return 300;
		});
		S.Add([](UWorld* World) { StopClip(); return 5; });
	}

	// --- "sniper" (user request 2026-10-09): the Female Soldier Medic-Sapper with the sniper rifle. A dedicated camera is placed
	// relative to HER (not at map coordinates: L_MovementTest's layout changes), looking at her mesh bounds. ---

	struct FSniperShots
	{
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<AEnemyCharacter> Target;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 ShotsSeen = 0;
	};
	TSharedPtr<FSniperShots> Sniper;

	/** The review camera: Side cm to her right, Front cm ahead, Height above her mesh centre, looking at the mesh centre. */
	void FrameMedic(UWorld* World, float Front, float Side, float Height)
	{
		AOperativeCharacter* Medic = Sniper->Medic.Get();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Medic || !PC)
		{
			return;
		}
		if (!Sniper->Camera.IsValid())
		{
			Sniper->Camera = World->SpawnActor<ACameraActor>(Medic->GetActorLocation(), FRotator::ZeroRotator);
		}
		ACameraActor* Camera = Sniper->Camera.Get();
		if (!Camera)
		{
			return;
		}
		const FVector Centre = Medic->GetMesh()->Bounds.Origin;
		const FVector Eye = Centre + Medic->GetActorForwardVector() * Front + Medic->GetActorRightVector() * Side + FVector(0.f, 0.f, Height);
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(50.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
	}

	/** Sniper fire clips the medic's anim started so far (a shot that misfired in the cold plays none). */
	int32 CountFireClips()
	{
		const AOperativeCharacter* Medic = Sniper->Medic.Get();
		const UOperativeAnimInstance* Anim = Medic ? Cast<UOperativeAnimInstance>(Medic->GetMesh()->GetAnimInstance()) : nullptr;
		int32 Count = 0;
		for (const FString& Clip : Anim ? Anim->GetSniperClipLog() : TArray<FString>())
		{
			Count += Clip.EndsWith(TEXT("_Fire")) ? 1 : 0;
		}
		return Count;
	}

	/** Runs again every frame until the medic's anim played another sniper fire clip. */
	int32 WaitForSniperShot(int32 ThenWait)
	{
		const int32 Fired = CountFireClips();
		if (Fired <= Sniper->ShotsSeen)
		{
			return -1;
		}
		Sniper->ShotsSeen = Fired;
		return ThenWait;
	}

	void SetSniperTarget(bool bFire)
	{
		AOperativeCharacter* Medic = Sniper->Medic.Get();
		if (!Medic)
		{
			return;
		}
		Medic->bTacticalCeaseFire = !bFire;
		Medic->SetManualPriorityTarget(bFire ? Sniper->Target.Get() : nullptr);
	}

	void AddSniperSteps(TArray<FStep>& S)
	{
		Sniper = MakeShared<FSniperShots>();
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			StartFight(World);
			SmokeUtils::PlaceSquadAtTestStart(World);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			AOperativeCharacter* Medic = Member(World, EOperativeRole::MedicSapper);
			Sniper->Medic = Medic;
			if (!Medic)
			{
				return 1;
			}
			Squad->SetLeader(Medic);
			Medic->SwitchToWeaponById(TEXT("sniper_rifle"));
			Medic->ColdLevel = 0.f;
			// The others step 6 m back (out of the frame); the target 12 m ahead, frozen and unkillable.
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				if (Each != Medic)
				{
					Each->TeleportTo(SmokeUtils::FreeSpot(World, Medic->GetActorLocation() - Medic->GetActorForwardVector() * 600.f
						+ Medic->GetActorRightVector() * (Each->SquadRole == EOperativeRole::Commander ? -250.f : 250.f), Each),
						Each->GetActorRotation(), false, true);
				}
			}
			const FVector From = Medic->GetActorLocation();
			const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorForwardVector() * 1200.f);
			if (AEnemyCharacter* Target = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, Spot + FVector(0.f, 0.f, 20.f)))
			{
				Target->GetHealthComponent()->SetMaxHealth(100000.f, true);
				Target->CustomTimeDilation = 0.f;
				Sniper->Target = Target;
				Medic->FaceAimAt(Target->GetActorLocation());
			}
			return 20;
		});
		// Standing idle with the rifle (HUD on: the weapon line shows the sniper rifle), then a clean frame.
		S.Add([](UWorld* World) { FrameMedic(World, 330.f, 300.f, -5.f); return 70; });
		S.Add([](UWorld* World) { Shot(TEXT("10_sniper_stand_idle_hud")); return 3; });
		S.Add([](UWorld* World)
		{
			if (ACodexTacticsHUD* Hud = HudOf(World))
			{
				Hud->bShowHUD = false; // clean frames of her from here on (the canvas HUD; bShowUI = false drops the UMG bar)
			}
			return 2;
		});
		S.Add([](UWorld* World) { Shot(TEXT("10_sniper_stand_idle"), false); return 5; });
		// Stand -> kneel -> fire (the order while standing): clip + a still at the shot.
		S.Add([](UWorld* World)
		{
			StartClip(TEXT("sniper_stand_kneel_fire"), 1, false);
			Sniper->ShotsSeen = CountFireClips();
			SetSniperTarget(true);
			return 1;
		});
		S.Add([](UWorld* World) { return WaitForSniperShot(3); });
		S.Add([](UWorld* World) { Shot(TEXT("11_sniper_kneel_fire"), false); return 75; }); // the bolt after the shot
		S.Add([](UWorld* World) { StopClip(); return 2; });
		// Kneeling fire again (shot + bolt), closer frame.
		S.Add([](UWorld* World) { FrameMedic(World, 260.f, 230.f, 5.f); StartClip(TEXT("sniper_kneel_fire"), 1, false); return 1; });
		S.Add([](UWorld* World) { return WaitForSniperShot(70); });
		S.Add([](UWorld* World) { StopClip(); return 2; });
		// Prone: down, then prone fire.
		S.Add([](UWorld* World)
		{
			SetSniperTarget(false);
			if (AOperativeCharacter* Medic = Sniper->Medic.Get())
			{
				Medic->SetStance(EOperativeStance::Prone);
			}
			StartClip(TEXT("sniper_prone_fire"), 1, false);
			return 60;
		});
		S.Add([](UWorld* World) { FrameMedic(World, 260.f, 200.f, 110.f); Sniper->ShotsSeen = CountFireClips(); SetSniperTarget(true); return 1; });
		S.Add([](UWorld* World) { return WaitForSniperShot(3); });
		S.Add([](UWorld* World) { Shot(TEXT("12_sniper_prone_fire"), false); return 90; });
		S.Add([](UWorld* World) { StopClip(); SetSniperTarget(false); return 20; });
		S.Add([](UWorld* World) { FrameMedic(World, -60.f, 330.f, 90.f); return 5; }); // from the side
		S.Add([](UWorld* World) { Shot(TEXT("13_sniper_prone_side"), false); return 5; });
	}
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Phone"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		const FString Mode = Args.IsEmpty() ? TEXT("frontend") : Args[0].ToLower();
		if (Mode == TEXT("combat"))
		{
			AddCombatSteps(Runner->Steps);
		}
		else if (Mode == TEXT("sniper"))
		{
			AddSniperSteps(Runner->Steps);
		}
		else
		{
			AddFrontendSteps(Runner->Steps);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: %s, %d steps -> %s"), *Mode, Runner->Steps.Num(), *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PhoneShots"),
		TEXT("Dev: rendered stills + frame sequences into Saved/Screenshots/Phone (frontend | combat); needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
