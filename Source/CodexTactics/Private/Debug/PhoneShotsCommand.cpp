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
// "sniperrt" / "snipertb" / "snipercover" (2026-10-09): sniper combat clips (real time, turn-based, cover), follow camera.
// "sniper" (L_MovementTest, 2026-10-09): the Female Soldier Medic-Sapper with the sniper rifle — standing idle, the order while
//   standing (kneel -> shot -> bolt, clip + still), kneeling fire (clip), prone fire (clip + stills); own camera framed on her.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
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

	/** The sniper combat clips' camera (defined with the sniper steps). */
	void UpdateSniperFollowCam(UWorld* World);

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
		if (UWorld* CamWorld = FrontendSmokeUtils::FindGameWorld())
		{
			UpdateSniperFollowCam(CamWorld); // every frame, also while a step waits
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
		/** Combat clips: the camera follows her and her target every frame; a dead priority target is replaced by the nearest enemy. */
		bool bFollow = false;
		bool bAutoRetarget = false;
		bool bCamPlaced = false;
		/** Cover clip: the camera stays behind her wall (never through it), looking past her corner. */
		bool bCoverCam = false;
		FVector CamLocation = FVector::ZeroVector;
		FVector CamLook = FVector::ZeroVector;
		float CamBack = 260.f;
		float CamSide = 520.f;
		float CamUp = 220.f;
		// Cover clip.
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		FCoverSlot Corner;
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
	// --- Sniper combat clips (user request 2026-10-09): real time, turn-based, cover. The camera follows her and her target
	// every frame (behind / beside her on the line to the target), never fixed map coordinates. HUD stays on. ---

	AEnemyCharacter* NearestLiveEnemy(UWorld* World, const FVector& From, float MaxCm)
	{
		AEnemyCharacter* Best = nullptr;
		float BestDist = MaxCm;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			const UHealthComponent* Health = It->GetHealthComponent();
			if (It->IsActorBeingDestroyed() || !Health || !Health->IsAlive() || It->ActorHasTag(TEXT("PhoneKeeper")))
			{
				continue;
			}
			const float Dist = FVector::Dist2D(From, It->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = *It;
			}
		}
		return Best;
	}

	void UpdateSniperFollowCam(UWorld* World)
	{
		if (!Sniper.IsValid() || !Sniper->bFollow)
		{
			return;
		}
		AOperativeCharacter* Medic = Sniper->Medic.Get();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Medic || !PC)
		{
			return;
		}
		AEnemyCharacter* Target = Sniper->Target.Get();
		const bool bTargetDead = !Target || !Target->GetHealthComponent() || !Target->GetHealthComponent()->IsAlive();
		if (bTargetDead && Sniper->bAutoRetarget)
		{
			Target = NearestLiveEnemy(World, Medic->GetActorLocation(), 4000.f);
			Sniper->Target = Target;
			if (Target)
			{
				Medic->SetManualPriorityTarget(Target);
			}
		}
		{
			// Per-frame trace for the review (yaw / pose continuity): actor + mesh yaw, pelvis height above the feet, stance, FullBody clip.
			const USkeletalMeshComponent* Body = Medic->GetMesh();
			const UOperativeAnimInstance* TraceAnim = Cast<UOperativeAnimInstance>(Body->GetAnimInstance());
			FString Clip;
			float W = 0.f;
			float SlotW = 0.f;
			if (TraceAnim)
			{
				TraceAnim->GetCoverPlayback(Clip, W, SlotW);
			}
			const float Feet = Medic->GetActorLocation().Z - Medic->GetSimpleCollisionHalfHeight();
			const AEnemyCharacter* TraceTarget = Sniper->Target.Get();
			const float TargetYaw = TraceTarget ? (TraceTarget->GetActorLocation() - Medic->GetActorLocation()).Rotation().Yaw : 0.f;
			UE_LOG(LogCodexTactics, Display, TEXT("[PoseTrace] f=%d yaw=%.1f mesh=%.1f pelvis=%.1f stance=%d cover=%d clip=%s w=%.2f target=%.1f"), Runner->ClipCount,
				Medic->GetActorRotation().Yaw, Body->GetComponentRotation().Yaw, Body->GetBoneLocation(TEXT("pelvis")).Z - Feet,
				static_cast<int32>(Medic->GetStance()), Medic->bInCover ? 1 : 0, *Clip, W, TargetYaw);
		}		const FVector Her = Medic->GetMesh()->Bounds.Origin;
		const FVector There = Target ? Target->GetActorLocation() : Her + Medic->GetActorForwardVector() * 800.f;
		FVector Dir = (There - Her).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
		{
			Dir = Medic->GetActorForwardVector();
		}
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir);
		FVector WantEye = Her - Dir * Sniper->CamBack + Right * Sniper->CamSide + FVector(0.f, 0.f, Sniper->CamUp);
		FVector WantLook = Her + (There - Her).GetClampedToMaxSize(1400.f) * 0.42f + FVector(0.f, 0.f, -20.f);
		if (Sniper->bCoverCam)
		{
			WantEye = Her - Sniper->F * 480.f + Sniper->R * 330.f + FVector(0.f, 0.f, 230.f);
			WantLook = Her + Sniper->F * 350.f - Sniper->R * 280.f + FVector(0.f, 0.f, -30.f);
		}
		if (!Sniper->bCamPlaced)
		{
			Sniper->CamLocation = WantEye;
			Sniper->CamLook = WantLook;
			Sniper->bCamPlaced = true;
		}
		Sniper->CamLocation = FMath::Lerp(Sniper->CamLocation, WantEye, 0.06f);
		Sniper->CamLook = FMath::Lerp(Sniper->CamLook, WantLook, 0.08f);
		if (!Sniper->Camera.IsValid())
		{
			Sniper->Camera = World->SpawnActor<ACameraActor>(Sniper->CamLocation, FRotator::ZeroRotator);
		}
		if (ACameraActor* Camera = Sniper->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Sniper->CamLocation, (Sniper->CamLook - Sniper->CamLocation).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(72.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	/** Shared set-up: the fight, the squad at the test start, the medic leads with the sniper rifle, the others 6 m back. */
	AOperativeCharacter* SetUpSniperFight(UWorld* World)
	{
		StartFight(World);
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->Tags.Add(TEXT("PhoneKeeper")); // the frozen far hound StartFight left: never a target
		}
		SmokeUtils::PlaceSquadAtTestStart(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		Squad->SetSquadPosture(ESquadFirePosture::Passive);
		AOperativeCharacter* Medic = Member(World, EOperativeRole::MedicSapper);
		Sniper->Medic = Medic;
		if (!Medic)
		{
			return nullptr;
		}
		Squad->SetLeader(Medic);
		Medic->SwitchToWeaponById(TEXT("sniper_rifle"));
		Medic->ColdLevel = 0.f;
		for (AOperativeCharacter* Each : Squad->GetMembers())
		{
			if (Each != Medic)
			{
				Each->TeleportTo(SmokeUtils::FreeSpot(World, Medic->GetActorLocation() - Medic->GetActorForwardVector() * 700.f
					+ Medic->GetActorRightVector() * (Each->SquadRole == EOperativeRole::Commander ? -300.f : 300.f), Each),
					Each->GetActorRotation(), false, true);
			}
		}
		return Medic;
	}

	AEnemyCharacter* SpawnSniperTarget(UWorld* World, EEnemyArchetype Type, const FVector& Where, bool bFrozen, float Health = -1.f)
	{
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, Where + FVector(0.f, 0.f, 30.f));
		if (Enemy)
		{
			if (Health > 0.f)
			{
				Enemy->GetHealthComponent()->SetMaxHealth(Health, true);
			}
			if (bFrozen)
			{
				Enemy->CustomTimeDilation = 0.f;
			}
		}
		return Enemy;
	}

	void AddSniperRealTimeSteps(TArray<FStep>& S)
	{
		Sniper = MakeShared<FSniperShots>();
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Medic = SetUpSniperFight(World);
			if (!Medic)
			{
				return 1;
			}
			Medic->CurrentClip = 2; // two shots, then the magazine reload is in the clip too
			const FVector From = Medic->GetActorLocation();
			const FVector Ahead = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorForwardVector() * 1600.f);
			const FVector Side = (Ahead - From).GetSafeNormal2D().RotateAngleAxis(90.f, FVector::UpVector);
			Sniper->Target = SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, Ahead + Side * 250.f, false, 130.f);
			SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, Ahead - Side * 300.f, false, 130.f);
			SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, Ahead + (Ahead - From).GetSafeNormal2D() * 300.f, false, 130.f);
			Sniper->bFollow = true;
			return 15;
		});
		// She walks across (standing, moving) when the order comes: she stops, kneels, fires, works the bolt, reloads.
		S.Add([](UWorld* World)
		{
			StartClip(TEXT("sniper_combat_realtime"), 1);
			if (AOperativeCharacter* Medic = Sniper->Medic.Get())
			{
				const FVector From = Medic->GetActorLocation();
				Medic->OrderMoveTo(SmokeUtils::ClearPoint(World, From, From + Medic->GetActorRightVector() * 700.f), false);
			}
			return 40;
		});
		S.Add([](UWorld* World)
		{
			Sniper->bAutoRetarget = true;
			SetSniperTarget(true); // the priority-target order while she walks
			return 520;
		});
		S.Add([](UWorld* World) { StopClip(); return 2; });
	}

	void AddSniperTurnBasedSteps(TArray<FStep>& S)
	{
		Sniper = MakeShared<FSniperShots>();
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Medic = SetUpSniperFight(World);
			if (!Medic)
			{
				return 1;
			}
			const FVector From = Medic->GetActorLocation();
			const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorForwardVector() * 900.f);
			Sniper->Target = SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, Spot, false, 100000.f);
			Medic->FaceAimAt(Spot);
			Sniper->bFollow = true;
			Sniper->CamBack = 420.f;
			Sniper->CamSide = 300.f;
			Sniper->CamUp = 300.f;
			return 20;
		});
		S.Add([](UWorld* World)
		{
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			Flow->RequestEnterTurnBased(true);
			if (AOperativeCharacter* Medic = Sniper->Medic.Get(); Medic && TurnBased->IsActive())
			{
				TurnBased->SelectUnit(Medic);
			}
			StartClip(TEXT("sniper_combat_turnbased"), 1);
			return 45;
		});
		// Her turn, standing with only 3 AP: the sniper attack is not offered (feed line).
		S.Add([](UWorld* World)
		{
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			if (FTurnUnitState* Unit = const_cast<FTurnUnitState*>(TurnBased->GetUnitState(Sniper->Medic.Get())))
			{
				Unit->AP = 3;
				TurnBased->EnterAttackMode();
			}
			return 75;
		});
		// Full AP: into a fire lane if needed.
		S.Add([](UWorld* World)
		{
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			AOperativeCharacter* Medic = Sniper->Medic.Get();
			FTurnUnitState* Unit = const_cast<FTurnUnitState*>(TurnBased->GetUnitState(Medic));
			const FTurnUnitState* EnemyState = TurnBased->GetUnitState(Sniper->Target.Get());
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			if (!Unit || !EnemyState || !Grid)
			{
				return 1;
			}
			Unit->AP = 8;
			if (!(TurnBasedRules::IsTargetInPattern(Medic->CurrentWeapon, EnemyState->GridPos - Unit->GridPos)
				&& GorkyLineOfSight::HasLineOfSight(Unit->GridPos, EnemyState->GridPos, *Grid)))
			{
				for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(Unit->GridPos, Unit->AP - 4))
				{
					if (Entry.Key != Unit->GridPos && Grid->IsCellWalkable(Entry.Key)
						&& TurnBasedRules::IsTargetInPattern(Medic->CurrentWeapon, EnemyState->GridPos - Entry.Key)
						&& GorkyLineOfSight::HasLineOfSight(Entry.Key, EnemyState->GridPos, *Grid))
					{
						TurnBased->MoveActiveUnitTo(Entry.Key);
						break;
					}
				}
			}
			return 10;
		});
		S.Add([](UWorld* World) { return World->GetSubsystem<UTurnBasedCombatSubsystem>()->IsUnitMoving() ? -1 : 20; });
		S.Add([](UWorld* World) { World->GetSubsystem<UTurnBasedCombatSubsystem>()->EnterAttackMode(); return 40; });
		// The shot from standing: kneel (stance AP) + shot (attack AP), the kneel clip, the shot, the bolt.
		S.Add([](UWorld* World)
		{
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			if (const FTurnUnitState* EnemyState = TurnBased->GetUnitState(Sniper->Target.Get()))
			{
				TurnBased->bGuaranteeAllHits = true;
				TurnBased->AttackCell(EnemyState->GridPos);
			}
			return 150;
		});
		S.Add([](UWorld* World) { StopClip(); return 2; });
	}

	void AddSniperCoverSteps(TArray<FStep>& S)
	{
		Sniper = MakeShared<FSniperShots>();
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Medic = SetUpSniperFight(World);
			if (!Medic)
			{
				return 1;
			}
			Sniper->F = Medic->GetActorForwardVector().GetSafeNormal2D();
			Sniper->R = FVector::CrossProduct(FVector::UpVector, Sniper->F);
			Sniper->P = Medic->GetActorLocation();
			Sniper->GroundZ = Sniper->P.Z - Medic->GetSimpleCollisionHalfHeight();
			// A 6 m x 3 m wall 4 m ahead (as CoverSmoke); the targets beyond it, round its right-hand corner (-R end).
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				const FVector Centre(Sniper->P.X + Sniper->F.X * 400.f, Sniper->P.Y + Sniper->F.Y * 400.f, Sniper->GroundZ + 150.f);
				const FTransform Xf(Sniper->F.Rotation(), Centre, FVector(0.4f, 6.f, 3.f));
				if (AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Xf, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
					Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
					Block->FinishSpawning(Xf);
				}
			}
			const FVector Beyond = Sniper->P + Sniper->F * 1500.f - Sniper->R * 650.f;
			Sniper->Target = SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, FVector(Beyond.X, Beyond.Y, Sniper->GroundZ + 60.f), true, 130.f);
			SpawnSniperTarget(World, EEnemyArchetype::Frostbitten, FVector(Beyond.X, Beyond.Y, Sniper->GroundZ + 60.f) + Sniper->F * 400.f - Sniper->R * 250.f, true, 130.f);
			return 60; // the navmesh rebuilds round the wall
		});
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Medic = Sniper->Medic.Get();
			const bool bCorner = Medic && CoverTraceRules::FindCoverSlotAt(World, Sniper->P + Sniper->F * 380.f - Sniper->R * 250.f + FVector(0.f, 0.f, 90.f),
				Sniper->F, Sniper->Corner);
			UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: cover corner %s (height %d, right edge exposed %d)"), bCorner ? TEXT("found") : TEXT("NOT found"),
				static_cast<int32>(Sniper->Corner.Height), Sniper->Corner.bRightEdgeExposed ? 1 : 0);
			Sniper->bFollow = true;
			Sniper->bCoverCam = true;
			Sniper->CamBack = 420.f;
			Sniper->CamSide = 380.f;
			Sniper->CamUp = 260.f;
			StartClip(TEXT("sniper_combat_cover"), 1);
			if (bCorner)
			{
				Medic->OrderTakeCover(Sniper->Corner, false);
			}
			return 30;
		});
		S.Add([](UWorld* World)
		{
			const AOperativeCharacter* Medic = Sniper->Medic.Get();
			return Medic && Medic->bInCover ? 45 : (Runner->ClipCount > 200 ? 1 : -1);
		});
		S.Add([](UWorld* World)
		{
			if (const AOperativeCharacter* Medic = Sniper->Medic.Get())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: in cover %d, %s, corner %d"), Medic->bInCover ? 1 : 0,
					*AOperativeCharacter::GetStanceDisplayName(Medic->GetStance()).ToString(), Medic->bAtCoverCorner ? 1 : 0);
			}
			Sniper->bAutoRetarget = true;
			SetSniperTarget(true);
			return 360;
		});
		S.Add([](UWorld* World)
		{
			if (const AOperativeCharacter* Medic = Sniper->Medic.Get())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("PhoneShots: after the fire: in cover %d, %s, shots %d, kneels %d, refused %d"), Medic->bInCover ? 1 : 0,
					*AOperativeCharacter::GetStanceDisplayName(Medic->GetStance()).ToString(), Medic->GetSniperShots(), Medic->GetSniperKneels(),
					Medic->GetSniperRefusedShots());
			}
			StopClip();
			return 2;
		});
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
		else if (Mode == TEXT("sniperrt"))
		{
			AddSniperRealTimeSteps(Runner->Steps);
		}
		else if (Mode == TEXT("snipertb"))
		{
			AddSniperTurnBasedSteps(Runner->Steps);
		}
		else if (Mode == TEXT("snipercover"))
		{
			AddSniperCoverSteps(Runner->Steps);
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
