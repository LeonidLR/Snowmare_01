// Dev-only console command for a visual HUD / stance check (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.HudShot [close]"
// "turnbased": Gorky 17 grid with one enemy. "cutscene": pre-combat cutscene card; "prep": preparation banner. "dialogue": the intro briefing in the bottom window. "failed": an operative dies -> mission-failed screen. "mainmenu" (with -ForceMainMenu): the start menu. "weapons": the weapon selector open. "grenade": the grenade aim. "inventory": the inventory drawer open. "transfer": the drawer with the Sprint 13 hand-over quantity dialog (M16 rounds to a squad mate, 15 chosen). "pause" / "saves": the pause menu / the save dialog (a quicksave first). "ring": tactical pause + barricade placement radius ring. "susanin": the Susanin rescue event (distress dialogue). "floating": floating combat texts. "rage": the commander in rage. "labels": overhead labels of enemies and deployables. "hold": the Space-hold dome and charge bar. "profile": the commander levelled up, profile open. "victory": the wave-cleared panel with kill statistics. "duel" (with "turnbased"): the dramatic shot framing. "frost": the frost vignette of a freezing squad (cold 90 %).
// "shoot": Ctrl + click shot at a barrel with the world slowed down, to see the tracer, target flash and a plan marker.
// Otherwise puts the squad into all three stances, posts a feed message, saves Saved/Screenshots/.../HudShot.png and exits.

#include "CoreMinimal.h"
#include "Combat/WaveVictorySubsystem.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadAutonomySubsystem.h"
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
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/ActionBarWidget.h"
#include "UI/PauseMenuWidget.h"
#include "Core/SaveGameSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/QuantitySplitDialogWidget.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/TransferRules.h"
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
			Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(TEXT("✅ HUD check: message feed and squad stances.")));
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
		if (Args.Contains(TEXT("duel")))
		{
			// Godot _perform_dramatic_tactical_attack: the shot 0.5 s before the screenshot (framing done, round in flight).
			TWeakObjectPtr<UWorld> DuelWorld(World);
			FTimerHandle DuelHandle;
			World->GetTimerManager().SetTimer(DuelHandle, FTimerDelegate::CreateLambda([DuelWorld]()
			{
				UTurnBasedCombatSubsystem* TurnBased = DuelWorld.IsValid() ? DuelWorld->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
				if (!TurnBased || !TurnBased->IsActive())
				{
					return;
				}
				for (TActorIterator<AEnemyCharacter> It(DuelWorld.Get()); It; ++It)
				{
					if (const FTurnUnitState* EnemyState = TurnBased->GetUnitState(*It))
					{
						TurnBased->AttackCellCinematic(EnemyState->GridPos);
						break;
					}
				}
			}), 4.0f, false);
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
				UFloatingTextSubsystem::SpawnAboveOperative(Members[2], TEXT("❄️ MISFIRE! (Bolt jammed)"), FLinearColor(0.4f, 0.85f, 1.f));
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
		if (Args.Contains(TEXT("vault")))
		{
			// Vault clip check: a barricade 2.5 m ahead of the leader, he walks through it; HudShot_Vault_{1,2,3}.png mid-vault.
			TWeakObjectPtr<UWorld> VaultWorld(World);
			FTimerHandle VaultHandle;
			World->GetTimerManager().SetTimer(VaultHandle, FTimerDelegate::CreateLambda([VaultWorld]()
			{
				USquadSubsystem* Squad = VaultWorld.IsValid() ? VaultWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				if (!Lead)
				{
					FPlatformMisc::RequestExit(false, TEXT("HudShot"));
					return;
				}
				const FVector Ground = Lead->GetActorLocation() - FVector(0.f, 0.f, Lead->GetSimpleCollisionHalfHeight());
				const FVector Forward = Lead->GetActorForwardVector().GetSafeNormal2D();
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				VaultWorld->SpawnActor<ABarricadeActor>(Ground + Forward * 250.f + FVector(0.f, 0.f, ABarricadeActor::HeightCm * 0.5f),
					FRotator(0.f, Forward.Rotation().Yaw + 90.f, 0.f), Params);
				Lead->OrderMoveTo(Ground + Forward * 700.f + FVector(0.f, 0.f, Lead->GetSimpleCollisionHalfHeight()), false);
				TWeakObjectPtr<AOperativeCharacter> WeakLead(Lead);
				TSharedRef<int32> Frames = MakeShared<int32>(0);
				TSharedRef<int32> Shots = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Frames, Shots, WeakLead](float)
				{
					AOperativeCharacter* L = WeakLead.Get();
					const int32 F = ++(*Frames);
					// Three shots 0.25 s apart once the vault has begun.
					if (L && L->IsVaulting() && *Shots < 3 && (*Shots == 0 || F % 5 == 0))
					{
						++(*Shots);
						FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("HudShot_Vault_%d.png"), *Shots), true, false);
						UE_LOG(LogCodexTactics, Display, TEXT("HudShot vault shot %d at frame %d"), *Shots, F);
					}
					if (F >= 200 || (*Shots >= 3 && L && !L->IsVaulting() && F % 20 == 0))
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					return true;
				}), 0.05f);
			}), 3.5f, false);
			return; // its own timeline
		}
		if (Args.Contains(TEXT("rifle2")))
		{
			// Rifle_2 locomotion check: the leader gets ABP_Operative_Rifle2 for this run, walks off, stops, turns about;
			// HudShot_Rifle2_{start,walk,stop,turn}.png with the machine's state logged at each shot.
			TWeakObjectPtr<UWorld> RifleWorld(World);
			FTimerHandle RifleHandle;
			World->GetTimerManager().SetTimer(RifleHandle, FTimerDelegate::CreateLambda([RifleWorld]()
			{
				USquadSubsystem* Squad = RifleWorld.IsValid() ? RifleWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				UClass* Rifle2 = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Characters/Operatives/ABP_Operative_Rifle2.ABP_Operative_Rifle2_C"));
				if (!Lead || !Rifle2)
				{
					UE_LOG(LogCodexTactics, Error, TEXT("HudShot rifle2: no leader / ABP_Operative_Rifle2"));
					FPlatformMisc::RequestExit(false, TEXT("HudShot"));
					return;
				}
				Lead->GetMesh()->SetAnimInstanceClass(Rifle2);
				TWeakObjectPtr<AOperativeCharacter> WeakLead(Lead);
				TSharedRef<int32> Frames = MakeShared<int32>(0);
				const FVector Start = Lead->GetActorLocation();
				const FVector Forward = Lead->GetActorForwardVector().GetSafeNormal2D();
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Frames, WeakLead, Start, Forward](float)
				{
					AOperativeCharacter* L = WeakLead.Get();
					if (!L)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					const int32 F = ++(*Frames);
					auto Shot = [L](const TCHAR* Name)
					{
						const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(L->GetMesh()->GetAnimInstance());
						UE_LOG(LogCodexTactics, Display, TEXT("HudShot rifle2 %s: state %d, speed %.0f, dir %.0f, root yaw %.0f, start %s, stop %s, turn %s"), Name,
							Anim ? static_cast<int32>(Anim->GetRifleLocoState()) : -1, Anim ? Anim->Speed : 0.f, Anim ? Anim->Direction : 0.f,
							Anim ? Anim->RootYawOffset : 0.f, Anim && Anim->LocoStartClip ? *Anim->LocoStartClip->GetName() : TEXT("-"),
							Anim && Anim->LocoStopClip ? *Anim->LocoStopClip->GetName() : TEXT("-"), Anim && Anim->LocoTurnClip ? *Anim->LocoTurnClip->GetName() : TEXT("-"));
						FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("HudShot_Rifle2_%s.png"), Name), true, false);
					};
					if (F == 10)
					{
						L->OrderMoveTo(Start + Forward * 700.f, false);
					}
					else if (F == 17)
					{
						Shot(TEXT("start"));
					}
					else if (F == 50)
					{
						Shot(TEXT("walk"));
					}
					else if (F == 70)
					{
						L->StopOperative();
					}
					else if (F == 77)
					{
						Shot(TEXT("stop"));
					}
					else if (F == 130)
					{
						L->SetFacingPoint(L->GetActorLocation() - L->GetActorForwardVector() * 500.f);
					}
					else if (F == 140)
					{
						Shot(TEXT("turn"));
					}
					else if (F >= 190)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}
					return true;
				}), 0.05f);
			}), 3.5f, false);
	return; // its own timeline: no stance pose, no default shot / exit
		}
		if (Args.Contains(TEXT("defend")))
		{
			// Sprint 10 visuals: tactical pause, the leader holds a barricade 5 m ahead — green fresnel, the ring, the shield.
			TWeakObjectPtr<UWorld> DefendWorld(World);
			FTimerHandle DefendHandle;
			World->GetTimerManager().SetTimer(DefendHandle, FTimerDelegate::CreateLambda([DefendWorld]()
			{
				USquadSubsystem* Squad = DefendWorld.IsValid() ? DefendWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				if (!Lead)
				{
					return;
				}
				UGameFlowSubsystem* Flow = DefendWorld->GetSubsystem<UGameFlowSubsystem>();
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				for (TActorIterator<AEnemyCharacter> It(DefendWorld.Get()); It; ++It)
				{
					It->CustomTimeDilation = 0.f;
					It->SetActorLocation(Lead->GetActorLocation() + FVector(4000.f, 4000.f, 2000.f));
				}
				Flow->ToggleTacticalPause();
				const FVector Ground = Lead->GetActorLocation() - FVector(0.f, 0.f, Lead->GetSimpleCollisionHalfHeight());
				const FVector Spot = SmokeUtils::ClearPoint(DefendWorld.Get(), Ground, Ground + Lead->GetActorForwardVector().GetSafeNormal2D() * 500.f);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ABarricadeActor* Barricade = DefendWorld->SpawnActor<ABarricadeActor>(FVector(Spot.X, Spot.Y, Ground.Z + ABarricadeActor::HeightCm * 0.5f),
					FRotator(0.f, Lead->GetActorRotation().Yaw + 90.f, 0.f), Params);
				if (USquadAutonomySubsystem* Autonomy = DefendWorld->GetSubsystem<USquadAutonomySubsystem>(); Autonomy && Barricade)
				{
					Autonomy->SetDefenseObjective(Lead, Barricade, Spot);
				}
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
		if (Args.Contains(TEXT("frost")))
		{
			// Godot UI/FrostOverlay: the coldest operative at 90 % (kept there until the screenshot).
			TWeakObjectPtr<UWorld> FrostWorld(World);
			FTimerHandle FrostHandle;
			World->GetTimerManager().SetTimer(FrostHandle, FTimerDelegate::CreateLambda([FrostWorld]()
			{
				USquadSubsystem* Squad = FrostWorld.IsValid() ? FrostWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
				{
					Member->ColdLevel = 90.f;
				}
			}), 0.25f, true);
		}
		if (Args.Contains(TEXT("victory")))
		{
			// Wave 1 cleared with a few kills in the statistics.
			TWeakObjectPtr<UWorld> VictoryWorld(World);
			FTimerHandle VictoryHandle;
			World->GetTimerManager().SetTimer(VictoryHandle, FTimerDelegate::CreateLambda([VictoryWorld]()
			{
				UGameFlowSubsystem* Flow = VictoryWorld.IsValid() ? VictoryWorld->GetSubsystem<UGameFlowSubsystem>() : nullptr;
				UWaveVictorySubsystem* Victory = VictoryWorld.IsValid() ? VictoryWorld->GetSubsystem<UWaveVictorySubsystem>() : nullptr;
				if (Flow && Victory)
				{
					Flow->TriggerCombatZone();
					Flow->FinishCutscene();
					Flow->FinishPreparation();
					VictoryWorld->GetSubsystem<UWaveSubsystem>()->ClearAllEnemies();
					Victory->RegisterEnemyKill(EEnemyArchetype::FrostHound, TEXT("Commander"));
					Victory->RegisterEnemyKill(EEnemyArchetype::Spitter, TEXT("Turret"));
					Victory->RegisterEnemyKill(EEnemyArchetype::Brute, TEXT("Engineer"));
					Victory->RegisterEnemyKill(EEnemyArchetype::FrostHound, TEXT("Mine"));
					Flow->NotifyWaveCleared();
					// Time stands still while the panel is up: shoot and exit on real time.
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
					}));
				}
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
				ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
				const USquadSubsystem* Squad = TrWorld.IsValid() ? TrWorld->GetSubsystem<USquadSubsystem>() : nullptr;
				AOperativeCharacter* Lead = Squad ? Squad->GetLeader() : nullptr;
				AOperativeCharacter* Mate = nullptr;
				for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
				{
					Mate = !Mate && Member != Lead ? Member : Mate;
				}
				if (Hud && Lead && Mate && Hud->GetQuantityDialog())
				{
					Hud->ToggleInventoryDrawer();
					Hud->GetQuantityDialog()->OpenFor(Lead, Mate, ETransferItem::RifleAmmo, TransferRules::GetMaxTransferQuantity(*Lead, *Mate, ETransferItem::RifleAmmo));
					Hud->GetQuantityDialog()->SetQuantity(15);
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
