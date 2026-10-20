// Dev-only check of the Frozen PMC Fighter (game designer 2026-10-20; Scripts/Editor/create_frozen_pmc_fighter.py) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PmcFighterSmoke                      (headless checks)
//   rendered: UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.PmcFighterSmoke shot"  -> Saved/Screenshots/pmc_fighter_<phase>.png
// BP_Enemy_FrozenPMC_Fighter (a child of BP_Enemy_Frostbitten on the Cyberpunk_Explorer_A body, retargeted ABP):
//   placed like a level actor -> archetype Frostbitten (class mapping), the Frostbitten's HP, nameplate "Frozen PMC", vision cone;
//   spawned like a wave enemy -> idles (not the reference pose, feet on the ground), walks to the squad, attacks the leader (the
//   retargeted attack clip on the one-shot slot, the leader loses HP), takes a hit (hit clip), is knocked down (the UE5 Manny
//   Knocked_ clip of /Game/Animations_KnockDown), dies on the ground (Death_ clip, held); a second one dies standing (its
//   retargeted death clip, held). Codex.Knockdown.Chance is forced to 1 for the run.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationRuntime.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/OverheadLabel.h"
#include "UnrealClient.h"

namespace PmcFighterSmoke
{
	constexpr float StepSeconds = 0.1f;
	const TCHAR* const PmcClassPath = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C");
	const TCHAR* const PmcMeshPackage = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Mesh/SKM_FrozenPMC_Fighter"); // the copy of the pack mesh

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		bool bShotTaken = false;
		float SavedChance = -1.f;
		float LeaderHpBefore = 0.f;
		float MaxSpeed = 0.f;
		float MaxPoseDeviation = 0.f;
		float MinToe = 1.0e6f;
		float MaxToe = -1.0e6f;
		TSet<FString> SeenClips;
		TWeakObjectPtr<UClass> PmcClass;
		TWeakObjectPtr<AEnemyCharacter> Pmc;
		TWeakObjectPtr<AEnemyCharacter> Second;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	IConsoleVariable* ChanceVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance"));
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (IConsoleVariable* Chance = ChanceVar())
		{
			Chance->Set(State.SavedChance, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcFighterSmoke"));
		return false;
	}

	/**
	 * Shot mode: a camera 4.6 m off the enemy's front-left at chest height follows it every step (no motion blur, the HUD
	 * widgets removed), so a capture never sees a camera jump.
	 */
	void AimCamera(FState& State, UWorld* World, const AEnemyCharacter* Enemy)
	{
		if (!State.bShots || !Enemy)
		{
			return;
		}
		ACameraActor* Camera = State.Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(Enemy->GetActorLocation(), FRotator::ZeroRotator);
			if (!Camera)
			{
				return;
			}
			UCameraComponent* Lens = Camera->GetCameraComponent();
			Lens->SetFieldOfView(45.f);
			Lens->bConstrainAspectRatio = false;
			Lens->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Lens->PostProcessSettings.MotionBlurAmount = 0.f;
			State.Camera = Camera;
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (PC->GetHUD())
				{
					PC->GetHUD()->bShowHUD = false;
				}
			}
		}
		const FVector Centre = Enemy->GetActorLocation() - FVector(0.f, 0.f, 10.f);
		const FVector Eye = Centre + Enemy->GetActorForwardVector() * 380.f - Enemy->GetActorRightVector() * 250.f + FVector(0.f, 0.f, 60.f);
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	/** Saves Saved/Screenshots/pmc_fighter_<Name>.png (shot mode; the camera already follows the enemy). */
	void Shot(FState& State, UWorld* World, const AEnemyCharacter* Enemy, const TCHAR* Name)
	{
		if (!State.bShots || !Enemy)
		{
			return;
		}
		AimCamera(State, World, Enemy);
		const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("pmc_fighter_%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot %s"), *FPaths::ConvertRelativePathToFull(File));
	}

	UEnemyAnimInstance* AnimOf(const AEnemyCharacter* Enemy)
	{
		return Enemy && Enemy->GetMesh() ? Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr;
	}

	/** Names of the clips the running (not stopped) montages play, e.g. "anim_attack_1(DefaultSlot 1.00)". */
	FString ActiveClips(const AEnemyCharacter* Enemy, TSet<FString>* OutNames = nullptr)
	{
		const UEnemyAnimInstance* Anim = AnimOf(Enemy);
		FString Result;
		if (!Anim)
		{
			return Result;
		}
		for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		{
			const UAnimMontage* Montage = Instance ? Instance->Montage.Get() : nullptr;
			if (!Montage || Instance->IsStopped() || Montage->SlotAnimTracks.IsEmpty() || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
			{
				continue;
			}
			const FSlotAnimationTrack& Track = Montage->SlotAnimTracks[0];
			const UAnimSequenceBase* Clip = Track.AnimTrack.AnimSegments[0].GetAnimReference();
			const FString Name = Clip ? Clip->GetName() : TEXT("?");
			Result += FString::Printf(TEXT("%s%s(%s %.2f)"), Result.IsEmpty() ? TEXT("") : TEXT(" "), *Name, *Track.SlotName.ToString(),
				Anim->GetSlotMontageGlobalWeight(Track.SlotName));
			if (OutNames)
			{
				OutNames->Add(Name);
			}
		}
		return Result;
	}

	bool PlaysClipFrom(const AEnemyCharacter* Enemy, const TArray<TObjectPtr<UAnimSequenceBase>>& Clips)
	{
		TSet<FString> Names;
		ActiveClips(Enemy, &Names);
		for (const UAnimSequenceBase* Clip : Clips)
		{
			if (Clip && Names.Contains(Clip->GetName()))
			{
				return true;
			}
		}
		return false;
	}

	/** Largest distance (cm, component space) of the hands / head from the mesh's reference pose: ~0 = T-pose / no animation. */
	float PoseDeviation(const AEnemyCharacter* Enemy)
	{
		const USkeletalMeshComponent* Body = Enemy ? Enemy->GetMesh() : nullptr;
		const USkeletalMesh* Mesh = Body ? Cast<USkeletalMesh>(Body->GetSkinnedAsset()) : nullptr;
		if (!Mesh)
		{
			return 0.f;
		}
		float Deviation = 0.f;
		for (const TCHAR* Bone : { TEXT("hand_l"), TEXT("hand_r"), TEXT("head"), TEXT("foot_l") })
		{
			const int32 Index = Mesh->GetRefSkeleton().FindBoneIndex(Bone);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			const FVector Ref = FAnimationRuntime::GetComponentSpaceTransformRefPose(Mesh->GetRefSkeleton(), Index).GetLocation();
			const FVector Now = Body->GetBoneLocation(Bone, EBoneSpaces::ComponentSpace);
			Deviation = FMath::Max(Deviation, FVector::Dist(Ref, Now));
		}
		return Deviation;
	}

	/** The lower toe (ball_l / ball_r) above the capsule bottom, cm. */
	float ToeAboveFloor(const AEnemyCharacter* Enemy)
	{
		const USkeletalMeshComponent* Body = Enemy->GetMesh();
		const float Bottom = Enemy->GetActorLocation().Z - Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		return FMath::Min(Body->GetSocketLocation(TEXT("ball_l")).Z, Body->GetSocketLocation(TEXT("ball_r")).Z) - Bottom;
	}

	void Sample(FState& State, const AEnemyCharacter* Enemy)
	{
		State.MaxPoseDeviation = FMath::Max(State.MaxPoseDeviation, PoseDeviation(Enemy));
		const float Toe = ToeAboveFloor(Enemy);
		State.MinToe = FMath::Min(State.MinToe, Toe);
		State.MaxToe = FMath::Max(State.MaxToe, Toe);
		ActiveClips(Enemy, &State.SeenClips);
	}

	void ResetSamples(FState& State)
	{
		State.MaxPoseDeviation = 0.f;
		State.MinToe = 1.0e6f;
		State.MaxToe = -1.0e6f;
	}

	AEnemyCharacter* SpawnLikeWave(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyCharacter* Enemy = nullptr;
		{
			const AEnemyCharacter::FScopedSpawnArchetype Scope(EEnemyArchetype::Frostbitten);
			Enemy = World->SpawnActor<AEnemyCharacter>(Class, Location, Rotation, Params);
		}
		if (Enemy)
		{
			Enemy->InitializeArchetype(EEnemyArchetype::Frostbitten);
			Enemy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		return Enemy;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; State.bShotTaken = false; };
		AEnemyCharacter* Pmc = State.Pmc.Get();
		AOperativeCharacter* Leader = State.Leader.Get();
		UEnemyAnimInstance* Anim = AnimOf(Pmc);
		AimCamera(State, World, State.Stage >= 7 ? State.Second.Get() : Pmc);
		switch (State.Stage)
		{
		case 0:
		{
			UClass* Class = LoadClass<AEnemyCharacter>(nullptr, PmcClassPath);
			State.PmcClass = Class;
			Check(State, Class != nullptr, TEXT("BP_Enemy_FrozenPMC_Fighter loads"));
			if (!Class)
			{
				return Finish(State, false);
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true; // a small enemy would rush it
				}
			}
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true; // nobody shoots the test enemy
			}
			State.Leader = Squad->GetLeader();
			Leader = State.Leader.Get();
			if (!Leader)
			{
				Check(State, false, TEXT("squad leader"));
				return Finish(State, false);
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			// A frozen hound far behind keeps the wave alive (no "wave cleared" time stop when the test enemies go).
			if (AEnemyCharacter* Keeper = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 3500.f + FVector(0.f, 0.f, 30.f)))
			{
				Keeper->CustomTimeDilation = 0.f;
				Keeper->SetActorTickEnabled(false);
			}

			// 1. Placed like a level actor (no spawner): the archetype safety net, stats, name, cone.
			const FVector PlacedAt = Leader->GetActorLocation() - Leader->GetActorRightVector() * 2500.f + FVector(0.f, 0.f, 30.f);
			const FTransform Transform(FRotator::ZeroRotator, PlacedAt);
			AEnemyCharacter* Placed = World->SpawnActorDeferred<AEnemyCharacter>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Placed)
			{
				Placed->FinishSpawning(Transform);
				Placed->SetTurnBasedHeld(true);
			}
			AEnemyCharacter* Reference = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, PlacedAt + FVector(0.f, 300.f, 0.f), FRotator::ZeroRotator);
			if (!Placed || !Reference)
			{
				Check(State, false, TEXT("placed PMC + a wave Frostbitten for reference"));
				return Finish(State, false);
			}
			FOverheadLabel Label;
			const bool bLabel = Placed->GetOverheadLabel(Label);
			Check(State, Placed->WasPlacedInLevel() && Placed->GetArchetype() == EEnemyArchetype::Frostbitten,
				FString::Printf(TEXT("placed BP_Enemy_FrozenPMC_Fighter -> archetype %s (Frostbitten via BP_Enemy_Frostbitten)"),
					*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Placed->GetArchetype()))));
			Check(State, FMath::IsNearlyEqual(Placed->GetHealthComponent()->GetMaxHealth(), Reference->GetHealthComponent()->GetMaxHealth()),
				FString::Printf(TEXT("HP %.0f like the wave's Frostbitten (%.0f)"), Placed->GetHealthComponent()->GetMaxHealth(),
					Reference->GetHealthComponent()->GetMaxHealth()));
			Check(State, Placed->GetEnemyDisplayName() == TEXT("Frozen PMC") && Reference->GetEnemyDisplayName() == TEXT("Frostbitten"),
				FString::Printf(TEXT("name \"%s\" (the Frostbitten stays \"%s\")"), *Placed->GetEnemyDisplayName(), *Reference->GetEnemyDisplayName()));
			Check(State, !bLabel || Label.Text.StartsWith(TEXT("Frozen PMC")), FString::Printf(TEXT("nameplate \"%s\""), *Label.Text.Replace(TEXT("\n"), TEXT(" | "))));
			Check(State, Placed->GetVisionConeRange() > 0.f && FMath::IsNearlyEqual(Placed->GetVisionConeRange(), Reference->GetVisionConeRange()),
				FString::Printf(TEXT("vision cone %.0f cm like the Frostbitten's"), Placed->GetVisionConeRange()));
			const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Placed->GetMesh()->GetSkinnedAsset());
			Check(State, Mesh && Mesh->GetPackage()->GetName() == PmcMeshPackage && AnimOf(Placed)
				&& AnimOf(Placed)->GetClass()->GetName().Contains(TEXT("FrozenPMC")),
				FString::Printf(TEXT("body %s, anim %s (class %s, mode %d, instance %s)"), Mesh ? *Mesh->GetName() : TEXT("none"),
					AnimOf(Placed) ? *AnimOf(Placed)->GetClass()->GetName() : TEXT("none"), *GetNameSafe(Placed->GetMesh()->GetAnimClass()),
					static_cast<int32>(Placed->GetMesh()->GetAnimationMode()), *GetNameSafe(Placed->GetMesh()->GetAnimInstance())));
			Placed->Destroy();
			Reference->Destroy();

			// 2. The test fighter, spawned like a wave enemy 12 m in front of the leader, held for the idle check.
			State.Pmc = SpawnLikeWave(World, Class, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1200.f + FVector(0.f, 0.f, 30.f),
				(-Leader->GetActorForwardVector()).Rotation());
			Pmc = State.Pmc.Get();
			Check(State, Pmc != nullptr, TEXT("PMC fighter spawned like a wave enemy"));
			if (!Pmc)
			{
				return Finish(State, false);
			}
			Pmc->GetHealthComponent()->SetMaxHealth(5000.f, true);
			Pmc->SetActorTickEnabled(false); // no AI while idling
			Next();
			return true;
		}
		case 1: // idle
		{
			if (!Pmc || !Anim)
			{
				Check(State, false, TEXT("PMC fighter alive with a UEnemyAnimInstance"));
				return Finish(State, false);
			}
			Sample(State, Pmc);
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const UCapsuleComponent* Capsule = Pmc->GetCapsuleComponent();
			Check(State, Anim->IdleAnimation != nullptr && State.MaxPoseDeviation > 3.f,
				FString::Printf(TEXT("idle %s plays (hands / head up to %.1f cm off the reference pose: no T-pose)"),
					Anim->IdleAnimation ? *Anim->IdleAnimation->GetName() : TEXT("none"), State.MaxPoseDeviation));
			Check(State, State.MinToe > -6.f && State.MinToe < 8.f, FString::Printf(TEXT("idle: feet on the ground (lowest toe %.1f cm above the capsule bottom)"), State.MinToe));
			Check(State, FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), 40.f) && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), 90.f),
				FString::Printf(TEXT("capsule %.0f / %.0f"), Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight()));
			const FBox Bounds = Pmc->GetMesh()->Bounds.GetBox();
			Check(State, Bounds.GetSize().Z > 150.f && Bounds.GetSize().Z < 2.f * Capsule->GetScaledCapsuleHalfHeight() + 30.f,
				FString::Printf(TEXT("body %.0f cm tall in a %.0f cm capsule"), Bounds.GetSize().Z, 2.f * Capsule->GetScaledCapsuleHalfHeight()));
			Shot(State, World, Pmc, TEXT("idle"));
			State.LeaderHpBefore = Leader ? Leader->HealthComponent->GetCurrentHealth() : 0.f;
			ResetSamples(State);
			Pmc->SetActorTickEnabled(true); // hunts the squad
			Next();
			return true;
		}
		case 2: // walk to the squad
		{
			if (!Pmc || !Leader)
			{
				Check(State, false, TEXT("PMC fighter and leader alive"));
				return Finish(State, false);
			}
			Sample(State, Pmc);
			const float Speed = Pmc->GetVelocity().Size2D();
			State.MaxSpeed = FMath::Max(State.MaxSpeed, Speed);
			if (!State.bShotTaken && Anim && Anim->bIsMoving && Speed > 100.f && State.StageTime > 1.5f)
			{
				State.bShotTaken = true;
				Shot(State, World, Pmc, TEXT("walk"));
			}
			if (Anim && Anim->bIsAttacking)
			{
				Check(State, State.MaxSpeed > 150.f && State.MaxPoseDeviation > 5.f,
					FString::Printf(TEXT("walked / ran to the leader (top speed %.0f cm/s, %s %s, pose up to %.1f cm off the reference)"), State.MaxSpeed,
						Anim->WalkAnimation ? *Anim->WalkAnimation->GetName() : TEXT("-"), Anim->RunAnimation ? *Anim->RunAnimation->GetName() : TEXT("-"),
						State.MaxPoseDeviation));
				Check(State, State.MinToe > -8.f && State.MinToe < 10.f,
					FString::Printf(TEXT("moving: feet on the ground (toe %.1f..%.1f cm above the capsule bottom)"), State.MinToe, State.MaxToe));
				ResetSamples(State);
				Next();
				return true;
			}
			if (State.StageTime > 25.f)
			{
				Check(State, false, FString::Printf(TEXT("never attacked (distance %.0f cm, speed %.0f)"), Pmc->GetDistanceTo(Leader), Speed));
				return Finish(State, false);
			}
			return true;
		}
		case 3: // attack
		{
			Sample(State, Pmc);
			if (!State.bShotTaken && State.StageTime >= 0.5f)
			{
				State.bShotTaken = true;
				Check(State, PlaysClipFrom(Pmc, Anim->AttackAnimations), FString::Printf(TEXT("attack clip on the one-shot slot: %s"), *ActiveClips(Pmc)));
				Shot(State, World, Pmc, TEXT("attack"));
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			const float LeaderHp = Leader ? Leader->HealthComponent->GetCurrentHealth() : 0.f;
			Check(State, LeaderHp < State.LeaderHpBefore, FString::Printf(TEXT("the blow landed (leader %.0f -> %.0f HP)"), State.LeaderHpBefore, LeaderHp));
			Check(State, State.MaxPoseDeviation > 5.f && State.MinToe > -8.f, FString::Printf(TEXT("attacking: animated (%.1f cm), feet %.1f cm"),
				State.MaxPoseDeviation, State.MinToe));
			Pmc->SetActorTickEnabled(false); // no more AI: scripted hits from here
			Pmc->GetCharacterMovement()->StopMovementImmediately();
			Next();
			return true;
		}
		case 4: // hit reaction
		{
			if (State.StageTime < 2.f)
			{
				return true; // the attack clip ends
			}
			if (!State.bShotTaken)
			{
				State.bShotTaken = true;
				FDamageSpec Spec;
				Spec.Amount = 10.f;
				Spec.ArmorPenetration = 1.f;
				Spec.AttackerSource = TEXT("Smoke");
				Pmc->GetHealthComponent()->TakeDamage(Spec);
				State.StageTime = 2.f;
				return true;
			}
			ActiveClips(Pmc, &State.SeenClips);
			if (State.StageTime < 2.3f)
			{
				return true;
			}
			bool bHit = false;
			for (const UAnimSequenceBase* Clip : Anim->HitAnimations)
			{
				bHit |= Clip && State.SeenClips.Contains(Clip->GetName());
			}
			Check(State, bHit, FString::Printf(TEXT("hit reaction clip: %s"), *ActiveClips(Pmc)));
			Shot(State, World, Pmc, TEXT("hit"));
			Next();
			return true;
		}
		case 5: // knockdown
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (!Pmc->IsKnockedDown())
			{
				FDamageSpec Heavy;
				Heavy.Amount = 70.f;
				Heavy.ArmorPenetration = 1.f;
				Heavy.AttackerSource = TEXT("Smoke");
				Pmc->GetHealthComponent()->TakeDamage(Heavy);
				const UKnockdownComponent* Knock = Pmc->FindComponentByClass<UKnockdownComponent>();
				const UAnimSequenceBase* Clip = Knock ? Knock->GetPlayingClip() : nullptr;
				Check(State, Pmc->IsKnockedDown() && Clip && Clip->GetName().StartsWith(TEXT("Knocked_"))
					&& Clip->GetPackage()->GetName().StartsWith(TEXT("/Game/Animations_KnockDown/Knocked_")),
					FString::Printf(TEXT("a 70 HP hit knocks him down with the UE5 Manny clip (%s)"), Clip ? *Clip->GetPathName() : TEXT("none")));
				if (!Pmc->IsKnockedDown())
				{
					return Finish(State, false);
				}
				State.StageTime = 1.5f;
				ResetSamples(State);
				return true;
			}
			Sample(State, Pmc);
			const UKnockdownComponent* Knock = Pmc->FindComponentByClass<UKnockdownComponent>();
			if (Knock->GetPhase() != EKnockdownPhase::Downed)
			{
				if (State.StageTime > 6.f)
				{
					Check(State, false, TEXT("never lay down"));
					return Finish(State, false);
				}
				return true;
			}
			const float Bottom = Pmc->GetActorLocation().Z - Pmc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Pelvis = Pmc->GetMesh()->GetSocketLocation(TEXT("pelvis")).Z - Bottom;
			Check(State, Pelvis < 45.f && Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) > 0.5f,
				FString::Printf(TEXT("down: pelvis %.0f cm above the floor, one-shot weight %.2f (%s)"), Pelvis,
					Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot), *ActiveClips(Pmc)));
			Shot(State, World, Pmc, TEXT("knockdown"));
			Pmc->GetHealthComponent()->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
			Next();
			return true;
		}
		case 6: // died on the ground
		{
			if (!Pmc)
			{
				Check(State, false, TEXT("the body stays a while (death decay)"));
				return Finish(State, false);
			}
			if (State.StageTime < 1.4f)
			{
				return true;
			}
			const UKnockdownComponent* Knock = Pmc->FindComponentByClass<UKnockdownComponent>();
			const UAnimSequenceBase* Clip = Knock ? Knock->GetPlayingClip() : nullptr;
			Check(State, Pmc->IsDying() && Knock && Knock->DiedWhileDown() && Clip && Clip->GetName().StartsWith(TEXT("Death_")),
				FString::Printf(TEXT("died while down: %s (%s)"), Clip ? *Clip->GetName() : TEXT("none"), *ActiveClips(Pmc)));
			Shot(State, World, Pmc, TEXT("death_down"));
			// A second one dies standing: its own (retargeted) death clip, held.
			if (Leader)
			{
				State.Second = SpawnLikeWave(World, State.PmcClass.Get(), Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f
					- Leader->GetActorRightVector() * 300.f + FVector(0.f, 0.f, 30.f), (-Leader->GetActorForwardVector()).Rotation());
			}
			if (AEnemyCharacter* Second = State.Second.Get())
			{
				Second->SetActorTickEnabled(false);
			}
			Next();
			return true;
		}
		case 7:
		{
			AEnemyCharacter* Second = State.Second.Get();
			if (!Second)
			{
				Check(State, false, TEXT("second PMC fighter spawned"));
				return Finish(State, false);
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (!Second->IsDying())
			{
				FDamageSpec Kill;
				Kill.Amount = 100000.f;
				Kill.ArmorPenetration = 1.f;
				Kill.AttackerSource = TEXT("Smoke");
				Second->GetHealthComponent()->TakeDamage(Kill);
				State.SeenClips.Reset();
				return true;
			}
			ActiveClips(Second, &State.SeenClips);
			if (State.StageTime < 3.2f)
			{
				return true;
			}
			const UEnemyAnimInstance* SecondAnim = AnimOf(Second);
			const UKnockdownComponent* Knock = Second->FindComponentByClass<UKnockdownComponent>();
			bool bDeathClip = false;
			for (const UAnimSequenceBase* Clip : SecondAnim ? SecondAnim->DeathAnimations : TArray<TObjectPtr<UAnimSequenceBase>>())
			{
				bDeathClip |= Clip && State.SeenClips.Contains(Clip->GetName());
			}
			const UAnimSequenceBase* FallClip = Knock ? Knock->GetPlayingClip() : nullptr;
			bDeathClip |= FallClip && FallClip->GetName().StartsWith(TEXT("Death_")); // a death fall (knockdown component) also counts
			const float Weight = SecondAnim ? SecondAnim->GetSlotMontageGlobalWeight(SecondAnim->OneShotSlot) : 0.f;
			Check(State, bDeathClip && Weight > 0.9f, FString::Printf(TEXT("died standing: death clip held (one-shot weight %.2f, %s)"), Weight, *ActiveClips(Second)));
			Shot(State, World, Second, TEXT("death"));
			Next();
			return true;
		}
		default:
			if (State.StageTime < 1.f)
			{
				return true; // the last screenshot is written
			}
			return Finish(State, true);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase); });
		if (IConsoleVariable* Chance = ChanceVar())
		{
			State->SavedChance = Chance->GetFloat();
			Chance->Set(1.f, ECVF_SetByConsole);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && !Step(WeakWorld, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), StepSeconds, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcFighterSmoke"),
		TEXT("Dev check: the Frozen PMC Fighter (Frostbitten variant, retargeted ABP) idles, walks, attacks, flinches, falls and dies on its body; ")
		TEXT("arg 'shot' saves Saved/Screenshots/pmc_fighter_*.png (needs rendering)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
