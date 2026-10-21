// Dev-only read-out of the Marksman's feet height (user bug report 2026-10-21 "the Marksman floats"; UE-only, no Godot reference).
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//       -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.MarksmanFeetProbe [Clips] [Mesh=<path>] [Modes=proposed] [Out=<name>]"
// Spawns BP_Enemy_Marksman (deaf) on an empty spot and logs how high it stands above the ground (line trace):
//  - default: its own ABP in idle / walk (as the game plays it);
//  - Clips: every clip of ABP_Enemy_Marksman's class defaults (idle, walk, run, aim, attack, crouch, prone, hit, death, stance
//    changes) played on the body one by one, sampled at 0 / 25 / 50 / 75 / 98 % of its length: the lowest feet bone and the
//    body's lowest point (mesh bounds) above the ground, against the same numbers in the reference pose (= feet on the ground).
//    Prone / death are judged by the lowest point, the standing clips by the feet.
//  - Mesh=: a runtime-only body swap (same skeleton); Modes=proposed: the skeleton's translation retargeting modes set IN MEMORY
//    (root / ik_* Animation, pelvis AnimationScaled, the rest Skeleton) to measure a retarget fix before saving it.
// A still per clip (last sample) / state when rendering: Saved/Screenshots/<Platform>/<Out or MarksmanFeet>/. Changes nothing on disk.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "EngineUtils.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace MarksmanFeetProbe
{
	struct FClip
	{
		FString Property;
		TObjectPtr<UAnimationAsset> Asset;
	};

	struct FRunner
	{
		int32 Frame = 0;
		int32 Samples = 0;
		bool bRender = false;
		bool bClips = false;
		bool bProposedModes = false;
		/** Pelvis mode for Modes=proposed (PelvisMode=Animation|AnimationScaled|AnimationRelative|OrientAndScale|Skeleton). */
		FString PelvisMode = TEXT("AnimationScaled");
		FString Dir;
		FString MeshOverride;
		/** Extra=<path>[,<path>]: more clips to measure (e.g. the pack's own anim_death, not on the ABP). */
		FString Extra;
		FDelegateHandle Handle;
		TWeakObjectPtr<AEnemyCharacter> Marksman;
		TWeakObjectPtr<ACameraActor> Camera;
		SmokeUtils::FClearArena Arena;
		TArray<FClip> Clips;
		int32 ClipIndex = 0;
		int32 SampleIndex = 0;
		float RefFeet = 0.f;
		float RefLowest = 0.f;
		float WorstStanding = 0.f;
		float WorstLying = 0.f;
		float ClipFeetMin = 0.f;
		float ClipFeetMax = 0.f;
		float ClipLowMin = 0.f;
		float ClipLowMax = 0.f;
		float ClipLowLast = 0.f;
	};

	TSharedPtr<FRunner> Runner;
	const float Fractions[] = { 0.f, 0.25f, 0.5f, 0.75f, 0.98f };

	float GroundZ(UWorld* World, const FVector& At, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MarksmanFeetProbe), false, Ignore);
		return World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 200.f), At - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params)
			? Hit.ImpactPoint.Z : -99999.f;
	}

	/** Lowest of the feet bones above the ground. */
	float FeetZ(const USkeletalMeshComponent* Mesh, float Ground)
	{
		float Low = TNumericLimits<float>::Max();
		for (const TCHAR* Bone : { TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") })
		{
			if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
			{
				Low = FMath::Min(Low, static_cast<float>(Mesh->GetBoneLocation(Bone).Z) - Ground);
			}
		}
		return Low;
	}

	/** The body's lowest point above the ground: its lowest skinned vertex (LOD0 on the CPU); the bounds when unavailable. */
	float LowestZ(USkeletalMeshComponent* Mesh, float Ground)
	{
		const FSkeletalMeshRenderData* Render = Mesh->GetSkeletalMeshRenderData();
		if (Render && Render->LODRenderData.Num() > 0)
		{
			const FSkeletalMeshLODRenderData& Lod = Render->LODRenderData[0];
			TArray<FMatrix44f> RefToLocals;
			Mesh->CacheRefToLocalMatrices(RefToLocals);
			TArray<FVector3f> Positions;
			USkinnedMeshComponent::ComputeSkinnedPositions(Mesh, Positions, RefToLocals, Lod, Lod.SkinWeightVertexBuffer);
			if (Positions.Num() > 0)
			{
				const FTransform ToWorld = Mesh->GetComponentTransform();
				double Low = TNumericLimits<double>::Max();
				for (const FVector3f& P : Positions)
				{
					Low = FMath::Min(Low, ToWorld.TransformPosition(FVector(P)).Z);
				}
				return static_cast<float>(Low) - Ground;
			}
		}
		Mesh->UpdateBounds();
		return static_cast<float>(Mesh->Bounds.GetBox().Min.Z) - Ground;
	}

	bool IsLyingClip(const FString& Property)
	{
		return Property.Contains(TEXT("prone")) || Property.Contains(TEXT("death")) || Property.Contains(TEXT("crawl"));
	}

	void Shot(const FString& Name)
	{
		if (Runner->bRender)
		{
			FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s.png"), Runner->Samples, *Name), false, false);
		}
		++Runner->Samples;
	}

	void SampleState(UWorld* World, const TCHAR* Label)
	{
		AEnemyCharacter* M = Runner->Marksman.Get();
		if (!M)
		{
			return;
		}
		USkeletalMeshComponent* Mesh = M->GetMesh();
		const float Ground = GroundZ(World, M->GetActorLocation(), M);
		const float CapsuleBottom = M->GetActorLocation().Z - M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FString Bones;
		for (const TCHAR* Bone : { TEXT("root"), TEXT("pelvis"), TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") })
		{
			if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
			{
				Bones += FString::Printf(TEXT(" %s %+.1f"), Bone, Mesh->GetBoneLocation(Bone).Z - Ground);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe [%s]: mesh %s, anim %s | above the ground (cm): capsule bottom %+.1f, mesh component %+.1f, bounds min %+.1f |%s"),
			Label, *GetNameSafe(Mesh->GetSkeletalMeshAsset()), *GetNameSafe(Mesh->GetAnimInstance() ? Mesh->GetAnimInstance()->GetClass() : nullptr),
			CapsuleBottom - Ground, Mesh->GetComponentLocation().Z - Ground, LowestZ(Mesh, Ground), *Bones);
		Shot(Label);
	}

	/** The clips on ABP_Enemy_Marksman's class defaults (single assets and arrays). */
	void CollectClips(USkeletalMeshComponent* Mesh)
	{
		UClass* AnimClass = Mesh->GetAnimClass();
		const UObject* Cdo = AnimClass ? AnimClass->GetDefaultObject() : nullptr;
		if (!Cdo)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(AnimClass); It; ++It)
		{
			const FString Name = It->GetName();
			if (const FObjectPropertyBase* Single = CastField<FObjectPropertyBase>(*It))
			{
				if (UAnimationAsset* Asset = Cast<UAnimationAsset>(Single->GetObjectPropertyValue_InContainer(Cdo)))
				{
					Runner->Clips.Add({ Name, Asset });
				}
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(*It))
			{
				if (const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Array->Inner))
				{
					FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Cdo));
					for (int32 Index = 0; Index < Helper.Num(); ++Index)
					{
						if (UAnimationAsset* Asset = Cast<UAnimationAsset>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
						{
							Runner->Clips.Add({ FString::Printf(TEXT("%s[%d]"), *Name, Index), Asset });
						}
					}
				}
			}
		}
	}

	EBoneTranslationRetargetingMode::Type PelvisMode()
	{
		const FString& M = Runner->PelvisMode;
		return M == TEXT("Animation") ? EBoneTranslationRetargetingMode::Animation
			: M == TEXT("AnimationRelative") ? EBoneTranslationRetargetingMode::AnimationRelative
			: M == TEXT("OrientAndScale") ? EBoneTranslationRetargetingMode::OrientAndScale
			: M == TEXT("Skeleton") ? EBoneTranslationRetargetingMode::Skeleton : EBoneTranslationRetargetingMode::AnimationScaled;
	}

	void ApplyProposedModes(USkeleton* Skeleton)
	{
		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		int32 Changed = 0;
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			const FString Bone = Ref.GetBoneName(Index).ToString();
			const EBoneTranslationRetargetingMode::Type Want = (Runner->PelvisMode == TEXT("AllAnimation") || Bone == TEXT("root") || Bone.StartsWith(TEXT("ik_")))
				? EBoneTranslationRetargetingMode::Animation
				: Bone == TEXT("pelvis") ? PelvisMode() : EBoneTranslationRetargetingMode::Skeleton;
			if (Skeleton->GetBoneTranslationRetargetingMode(Index) != Want)
			{
				Skeleton->SetBoneTranslationRetargetingMode(Index, Want, false);
				++Changed;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe: proposed retargeting modes (pelvis %s) set IN MEMORY on %s (%d bones changed, not saved)"),
			*Runner->PelvisMode, *Skeleton->GetPathName(), Changed);
	}

	void Spawn(UWorld* World)
	{
		FRunner& R = *Runner;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		TArray<const AActor*> Ignore;
		for (const AOperativeCharacter* Each : Members)
		{
			Ignore.Add(Each);
		}
		const FVector Hint = Members.IsEmpty() ? FVector::ZeroVector : Members[0]->GetActorLocation();
		const FVector HintForward = Members.IsEmpty() ? FVector::ForwardVector : Members[0]->GetActorForwardVector();
		R.Arena = SmokeUtils::FindClearArena(World, Hint, HintForward, FBox2D(FVector2D(-400.f, -400.f), FVector2D(1900.f, 400.f)), Ignore);
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			SmokeUtils::ParkEnemyFarAway(*It);
		}
		int32 Slot = -1;
		for (AOperativeCharacter* Each : Members)
		{
			Each->StopOperative();
			Each->bTacticalCeaseFire = true;
			const FVector Spot = R.Arena.Origin + R.Arena.Forward * 1600.f + R.Arena.Right * (200.f * Slot++) + FVector(0.f, 0.f, 95.f);
			Each->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Each), (-R.Arena.Forward).Rotation(), false, true);
		}
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C"));
		const FTransform At(R.Arena.Forward.Rotation(), R.Arena.Origin + FVector(0.f, 0.f, 120.f));
		AEnemyCharacter* M = Class ? World->SpawnActorDeferred<AEnemyCharacter>(Class, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (M)
		{
			FEnemyPerceptionParams Deaf = M->GetPerception();
			Deaf.SightRangeCm = 0.f;
			Deaf.ProximityCm = 0.f;
			Deaf.HearWalkCm = Deaf.HearRunCm = Deaf.HearCrouchWalkCm = Deaf.HearCrawlCm = Deaf.HearGunshotCm = Deaf.HearExplosionCm = 0.f;
			M->PerceptionOverride = Deaf;
			M->bOverridePerception = true;
			M->FinishSpawning(At);
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(M);
			}
			M->GetHealthComponent()->SetMaxHealth(100000.f, true);
			USkeletalMeshComponent* Mesh = M->GetMesh();
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			if (!R.MeshOverride.IsEmpty())
			{
				if (USkeletalMesh* Other = LoadObject<USkeletalMesh>(nullptr, *R.MeshOverride))
				{
					Mesh->EmptyOverrideMaterials();
					Mesh->SetSkeletalMeshAsset(Other);
				}
			}
			if (R.bProposedModes && Mesh->GetSkeletalMeshAsset())
			{
				ApplyProposedModes(Mesh->GetSkeletalMeshAsset()->GetSkeleton());
				Mesh->InitAnim(true);
			}
			Mesh->SetTextureForceResidentFlag(true);
			Mesh->PrestreamTextures(60.f, true);
			if (R.bClips)
			{
				CollectClips(Mesh);
				TArray<FString> ExtraPaths;
				R.Extra.ParseIntoArray(ExtraPaths, TEXT(","));
				for (const FString& Path : ExtraPaths)
				{
					if (UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *Path))
					{
						R.Clips.Add({ FString::Printf(TEXT("extra_death_%s"), *Asset->GetName()), Asset });
					}
				}
				if (AAIController* AIC = Cast<AAIController>(M->GetController()))
				{
					AIC->StopMovement();
				}
				M->GetCharacterMovement()->DisableMovement();
				Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				Mesh->SetAnimation(nullptr);
			}
		}
		R.Marksman = M;
		R.Camera = World->SpawnActor<ACameraActor>(R.Arena.Origin + R.Arena.Right * 420.f + FVector(0.f, 0.f, 90.f), (-R.Arena.Right).Rotation());
		if (ACameraActor* Camera = R.Camera.Get())
		{
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->GetCameraComponent()->SetFieldOfView(50.f);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe: spawned %s with %s at %s (arena %d), %d clips"), *GetNameSafe(M),
			M ? *GetNameSafe(M->GetMesh()->GetSkeletalMeshAsset()) : TEXT("-"), *At.GetLocation().ToCompactString(), R.Arena.bFound ? 1 : 0, R.Clips.Num());
	}

	void Finish()
	{
		FRunner& R = *Runner;
		if (R.bClips)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe SUMMARY: worst standing feet offset %+.1f cm, worst lying lowest-point offset %+.1f cm (0 = on the ground as in the reference pose)"),
				R.WorstStanding, R.WorstLying);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe: %d stills in %s"), R.Samples, *R.Dir);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: PASS"));
		FCoreDelegates::OnEndFrame.Remove(R.Handle);
		FPlatformMisc::RequestExit(false, TEXT("MarksmanFeetProbe"));
	}

	/** Clips mode: one sample every 3 frames (set the position, let it evaluate, read). */
	void TickClips(UWorld* World)
	{
		FRunner& R = *Runner;
		AEnemyCharacter* M = R.Marksman.Get();
		if (!M)
		{
			Finish();
			return;
		}
		USkeletalMeshComponent* Mesh = M->GetMesh();
		const float Ground = GroundZ(World, M->GetActorLocation(), M);
		const int32 Local = R.Frame - 60;
		if (Local < 0)
		{
			return;
		}
		if (Local == 0)
		{
			return; // the reference pose evaluates
		}
		if (Local == 2)
		{
			R.RefFeet = FeetZ(Mesh, Ground);
			R.RefLowest = LowestZ(Mesh, Ground);
			UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe REF: mesh %s, reference pose: feet %+.1f, lowest point %+.1f cm above the ground"),
				*GetNameSafe(Mesh->GetSkeletalMeshAsset()), R.RefFeet, R.RefLowest);
			Shot(TEXT("ref_pose"));
			return;
		}
		const int32 Step = (Local - 3) / 3;
		const int32 Phase = (Local - 3) % 3;
		const int32 PerClip = UE_ARRAY_COUNT(Fractions);
		const int32 ClipIndex = Step / PerClip;
		const int32 SampleIndex = Step % PerClip;
		if (!R.Clips.IsValidIndex(ClipIndex))
		{
			Finish();
			return;
		}
		const FClip& Clip = R.Clips[ClipIndex];
		if (Phase == 0)
		{
			if (SampleIndex == 0)
			{
				Mesh->PlayAnimation(Clip.Asset, true);
				Mesh->SetPlayRate(0.f);
				R.ClipFeetMin = R.ClipLowMin = TNumericLimits<float>::Max();
				R.ClipFeetMax = R.ClipLowMax = -TNumericLimits<float>::Max();
			}
			const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Clip.Asset);
			const float Length = Sequence ? Sequence->GetPlayLength() : 1.f;
			Mesh->SetPosition(Length * Fractions[SampleIndex], false);
			return;
		}
		if (Phase == 1)
		{
			return;
		}
		const float Feet = FeetZ(Mesh, Ground) - R.RefFeet;
		const float Low = LowestZ(Mesh, Ground) - R.RefLowest;
		R.ClipFeetMin = FMath::Min(R.ClipFeetMin, Feet);
		R.ClipFeetMax = FMath::Max(R.ClipFeetMax, Feet);
		R.ClipLowMin = FMath::Min(R.ClipLowMin, Low);
		R.ClipLowMax = FMath::Max(R.ClipLowMax, Low);
		R.ClipLowLast = Low;
		if (SampleIndex == PerClip - 1)
		{
			const bool bLying = IsLyingClip(Clip.Property);
			// standing: the lowest foot over the clip (a foot planted); lying: the lowest body point at the end / over the clip
			const float Verdict = bLying ? (Clip.Property.Contains(TEXT("death")) ? R.ClipLowLast : R.ClipLowMin) : R.ClipFeetMin;
			float& Worst = bLying ? R.WorstLying : R.WorstStanding;
			if (FMath::Abs(Verdict) > FMath::Abs(Worst))
			{
				Worst = Verdict;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe CLIP %-28s %-32s %s %+6.1f cm | feet %+.1f..%+.1f, lowest point %+.1f..%+.1f (last %+.1f)"),
				*Clip.Property, *GetNameSafe(Clip.Asset), bLying ? TEXT("lowest") : TEXT("feet  "), Verdict, R.ClipFeetMin, R.ClipFeetMax,
				R.ClipLowMin, R.ClipLowMax, R.ClipLowLast);
			if (bLying || Clip.Property.Contains(TEXT("idle")) || Clip.Property.Contains(TEXT("hit")) || Clip.Property.Contains(TEXT("crouch")))
			{
				Shot(Clip.Property.Replace(TEXT("["), TEXT("_")).Replace(TEXT("]"), TEXT("")));
			}
		}
	}

	void Tick()
	{
		FRunner& R = *Runner;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (!World)
		{
			return;
		}
		++R.Frame;
		if (R.Frame == 30)
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Squad || Squad->GetMembers().IsEmpty())
			{
				R.Frame = 29;
				return;
			}
			Spawn(World);
			return;
		}
		if (R.Frame < 30)
		{
			return;
		}
		if (R.bClips)
		{
			TickClips(World);
			return;
		}
		if (R.Frame == 150)
		{
			SampleState(World, TEXT("idle_2s"));
		}
		else if (R.Frame == 300)
		{
			SampleState(World, TEXT("idle_4s"));
		}
		else if (R.Frame == 330)
		{
			if (AEnemyCharacter* M = R.Marksman.Get())
			{
				if (AAIController* AIC = Cast<AAIController>(M->GetController()))
				{
					AIC->MoveToLocation(M->GetActorLocation() + R.Arena.Forward * 600.f, 20.f);
				}
			}
		}
		else if (R.Frame == 400)
		{
			SampleState(World, TEXT("walk"));
		}
		else if (R.Frame > 420)
		{
			Finish();
		}
	}

	void Run(const TArray<FString>& Args, UWorld*)
	{
		Runner = MakeShared<FRunner>();
		Runner->bRender = FApp::CanEverRender();
		FString Out = TEXT("MarksmanFeet");
		for (const FString& Arg : Args)
		{
			FParse::Value(*Arg, TEXT("Mesh="), Runner->MeshOverride);
			FParse::Value(*Arg, TEXT("Out="), Out);
			FParse::Value(*Arg, TEXT("PelvisMode="), Runner->PelvisMode);
			FParse::Value(*Arg, TEXT("Extra="), Runner->Extra);
			Runner->bClips |= Arg.Equals(TEXT("Clips"), ESearchCase::IgnoreCase);
			Runner->bProposedModes |= Arg.Equals(TEXT("Modes=proposed"), ESearchCase::IgnoreCase);
		}
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / Out);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MarksmanFeetProbe"),
		TEXT("Dev: logs how high the Marksman's feet / lowest point are above the ground (its ABP in idle / walk, or 'Clips': every ABP "
			"clip vs the reference pose; Mesh=<path> swaps the body, Modes=proposed sets the retargeting modes in memory); exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
