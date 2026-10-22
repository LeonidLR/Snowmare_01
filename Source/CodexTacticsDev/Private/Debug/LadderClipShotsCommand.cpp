// Dev-only console command: rendered frames of the ladder climbing clips (ladder system, user request 2026-10-22; clips from
// Scripts/Editor/import_ladder_animations.py). Needs rendering (no -nullrhi); run offscreen with a fixed frame time:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.LadderClipShots"
// On a slab 30 m above the map it places the UE5 Manny (the clips' own skeleton) and, 2.6 m to his right, the operative
// body (BP_Operative's mesh, UE4 skeleton, playing the UE5 clip by bone name), each on a graybox ladder laid out from the
// clip itself: rung pitch 36.4 cm (the hand plateaus of the up loop), rung plane at the hands of the loop's first frame, a
// platform at the clip's top for ClimbOutTop / IntoFromTop. Every clip is stepped frame by frame (single-node animation,
// paused; the root bone carries the climb) and shot from three cameras: 'a' = 3/4 from behind (both bodies), 'm' = Manny's
// side profile, 'o' = the operative's side profile (the other body hidden). Output:
// Saved/Screenshots/LadderClips/<clip>/<view>_<frame>.png. Per frame and body the log gets a CONTACT line: the Z of
// root / hand_l / hand_r / ball_l / ball_r over the ladder's floor and their signed distance to the rung plane (+ = past
// it, towards the wall), so the contact error can be measured offline.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace LadderClipShots
{
	constexpr float RungPitchCm = 36.4f;
	constexpr float RailHalfSpacingCm = 30.f;
	constexpr float BodySpacingCm = 260.f;
	constexpr int32 WarmUpFrames = 20;
	const TCHAR* const ClipDir = TEXT("/Game/Characters/Operatives/Anims/Ladder/");
	const TCHAR* const MannyMesh = TEXT("/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* const OperativeClass = TEXT("/Game/Characters/Operatives/BP_Operative.BP_Operative_C");
	const TCHAR* const ShapeMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* const Limbs[] = { TEXT("root"), TEXT("hand_l"), TEXT("hand_r"), TEXT("ball_l"), TEXT("ball_r") };

	enum class EClipKind : uint8 { Loop, ClimbOut, IntoFromTop };

	struct FClip
	{
		const TCHAR* Asset;
		const TCHAR* Short;
		EClipKind Kind;
	};

	const FClip Clips[] = {
		{ TEXT("AS_Ladder_UpLoop"), TEXT("up_loop"), EClipKind::Loop },
		{ TEXT("AS_Ladder_ClimbOutTop"), TEXT("climb_out_top"), EClipKind::ClimbOut },
		{ TEXT("AS_Ladder_IntoFromTop"), TEXT("into_from_top"), EClipKind::IntoFromTop },
	};

	enum class EPhase : uint8 { Setup, MeasureStart, MeasureEnd, Build, Pass, Done };

	struct FRunner
	{
		FDelegateHandle Handle;
		FString Dir;
		int32 Frame = 0;
		int32 Wait = 0;
		EPhase Phase = EPhase::Setup;
		int32 ClipIndex = 0;
		int32 View = 0; // 0 = 'a', 1 = 'm', 2 = 'o'
		int32 ShotIndex = 0;
		bool bShotPending = false;
		FVector Base = FVector::ZeroVector;
		/** Climbing direction (from the root to the rungs) and the right vector, measured on the up loop. */
		FVector Forward = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		/** Rung plane distance from the hanging root along Forward, and the ladder centre's offset along Right. */
		float PlaneCm = 0.f;
		float LateralCm = 0.f;
		FVector RootStart = FVector::ZeroVector;
		FVector RootEnd = FVector::ZeroVector;
		float FloorZ = 0.f;
		float TopZ = 0.f;
		float MaxRootZ = 0.f;
		TWeakObjectPtr<ASkeletalMeshActor> Manny;
		TWeakObjectPtr<ASkeletalMeshActor> Operative;
		TArray<TWeakObjectPtr<AActor>> MannyProps;
		TArray<TWeakObjectPtr<AActor>> OperativeProps;
		TArray<TWeakObjectPtr<AActor>> SharedProps;
		TWeakObjectPtr<ACameraActor> Camera;
		TObjectPtr<UAnimSequence> Anim = nullptr;
		bool bOperativePlays = false;
	};

	TSharedPtr<FRunner> Runner;

	void Finish(const TCHAR* Why)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: done (%s)"), Why);
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("LadderClipShots"));
	}

	USkeletalMeshComponent* MeshOf(const TWeakObjectPtr<ASkeletalMeshActor>& Actor)
	{
		return Actor.IsValid() ? Actor->GetSkeletalMeshComponent() : nullptr;
	}

	void SetTime(float Seconds)
	{
		for (const TWeakObjectPtr<ASkeletalMeshActor>& Body : { Runner->Manny, Runner->Operative })
		{
			if (USkeletalMeshComponent* Mesh = MeshOf(Body))
			{
				Mesh->SetPosition(Seconds, false);
			}
		}
	}

	AStaticMeshActor* Shape(UWorld* World, const TCHAR* MeshPath, const FVector& Centre, const FRotator& Rotation, const FVector& Scale,
		const FLinearColor& Color, TArray<TWeakObjectPtr<AActor>>& Owner)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Centre, Rotation, Params);
		if (!Actor || !Mesh)
		{
			return Actor;
		}
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Actor->SetActorScale3D(Scale);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ShapeMaterial))
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Actor);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Component->SetMaterial(0, Material);
		}
		Owner.Add(Actor);
		return Actor;
	}

	/** A box between two corners given in the ladder frame (Forward, Right, Up) around Origin. */
	void Box(UWorld* World, const FVector& Origin, const FVector& Lo, const FVector& Hi, const FLinearColor& Color, TArray<TWeakObjectPtr<AActor>>& Owner)
	{
		const FRunner& R = *Runner;
		const FVector Mid = (Lo + Hi) * 0.5f;
		const FVector Size = Hi - Lo;
		const FVector Centre = Origin + R.Forward * Mid.X + R.Right * Mid.Y + FVector::UpVector * Mid.Z;
		Shape(World, TEXT("/Engine/BasicShapes/Cube.Cube"), Centre, R.Forward.Rotation(), Size / 100.f, Color, Owner);
	}

	/** Rails, rungs and (for the top clips) a platform in front of one body; Hang = the root while hanging, on the floor. */
	void BuildLadder(UWorld* World, const FVector& Hang, const FClip& Clip, TArray<TWeakObjectPtr<AActor>>& Owner)
	{
		const FRunner& R = *Runner;
		const FLinearColor Steel(0.32f, 0.34f, 0.37f);
		const FLinearColor RungColor(0.9f, 0.62f, 0.08f);
		const FLinearColor Concrete(0.42f, 0.4f, 0.37f);
		const float Height = R.TopZ - R.FloorZ;
		const int32 RungCount = Clip.Kind == EClipKind::Loop ? 11 : FMath::FloorToInt((Height + 5.f) / RungPitchCm);
		const float RailTop = Clip.Kind == EClipKind::Loop ? RungCount * RungPitchCm + 30.f : Height + 110.f;
		const FVector Origin(Hang.X, Hang.Y, R.FloorZ);
		for (const float Side : { -1.f, 1.f })
		{
			const float Y = R.LateralCm + Side * RailHalfSpacingCm;
			Box(World, Origin, FVector(R.PlaneCm - 3.5f, Y - 2.5f, 0.f), FVector(R.PlaneCm + 3.5f, Y + 2.5f, RailTop), Steel, Owner);
		}
		const FRotator Across = FRotationMatrix::MakeFromZ(R.Right).Rotator();
		for (int32 K = 1; K <= RungCount; ++K)
		{
			const FVector Centre = Origin + R.Forward * R.PlaneCm + R.Right * R.LateralCm + FVector(0.f, 0.f, K * RungPitchCm);
			Shape(World, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Centre, Across, FVector(0.036f, 0.036f, RailHalfSpacingCm * 2.f / 100.f),
				RungColor, Owner);
		}
		if (Clip.Kind != EClipKind::Loop)
		{
			// The upper floor starts just past the rung plane and runs 2.5 m on; its top is the clip's top.
			Box(World, Origin, FVector(R.PlaneCm + 5.f, R.LateralCm - 140.f, 0.f), FVector(R.PlaneCm + 255.f, R.LateralCm + 140.f, Height),
				Concrete, Owner);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: LADDER clip=%s rungs=%d pitch=%.1f top=%.1f plane=%.1f lateral=%.1f"),
			Clip.Short, RungCount, RungPitchCm, Height, R.PlaneCm, R.LateralCm);
	}

	void ClearProps(TArray<TWeakObjectPtr<AActor>>& Props)
	{
		for (const TWeakObjectPtr<AActor>& Prop : Props)
		{
			if (Prop.IsValid())
			{
				Prop->Destroy();
			}
		}
		Props.Reset();
	}

	ASkeletalMeshActor* SpawnBody(UWorld* World, USkeletalMesh* Mesh, const FVector& Location, const USkeletalMeshComponent* Look)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASkeletalMeshActor* Body = World->SpawnActor<ASkeletalMeshActor>(Location, FRotator::ZeroRotator, Params);
		if (!Body)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Component = Body->GetSkeletalMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetSkeletalMeshAsset(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Component->SetForcedLOD(1);
		if (Look)
		{
			for (int32 Index = 0; Index < Look->GetNumMaterials(); ++Index)
			{
				Component->SetMaterial(Index, Look->GetMaterial(Index));
			}
		}
		return Body;
	}

	/** Starts Clip on both bodies (paused at 0); false when the clip asset is missing. */
	bool StartClip(const FClip& Clip)
	{
		FRunner& R = *Runner;
		R.Anim = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s%s.%s"), ClipDir, Clip.Asset, Clip.Asset));
		if (!R.Anim)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("LadderClipShots: clip %s missing (run Scripts/Editor/import_ladder_animations.py)"), Clip.Asset);
			return false;
		}
		// With root motion on, the single-node pose locks the root bone (the climb would happen on the spot). In this -game
		// run only (in memory, never saved) the root track is played as a plain bone so the body climbs the ladder.
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: %s asset root motion %d (played as a plain root track here)"), Clip.Short,
			R.Anim->bEnableRootMotion ? 1 : 0);
		R.Anim->bEnableRootMotion = false;
		for (const TWeakObjectPtr<ASkeletalMeshActor>& Body : { R.Manny, R.Operative })
		{
			if (USkeletalMeshComponent* Mesh = MeshOf(Body))
			{
				Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				Mesh->SetAnimation(R.Anim);
				Mesh->Stop();
				Mesh->SetPosition(0.f, false);
			}
		}
		const UAnimSingleNodeInstance* OperativeNode = MeshOf(R.Operative) ? MeshOf(R.Operative)->GetSingleNodeInstance() : nullptr;
		R.bOperativePlays = OperativeNode && OperativeNode->GetAnimationAsset() == R.Anim;
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: CLIP %s length %.3f s, %d frames at %.1f fps, root motion %d; operative body plays it by bone name: %s"),
			Clip.Short, R.Anim->GetPlayLength(), R.Anim->GetNumberOfSampledKeys(), R.Anim->GetSamplingFrameRate().AsDecimal(),
			R.Anim->bEnableRootMotion ? 1 : 0, R.bOperativePlays ? TEXT("YES") : TEXT("NO (incompatible skeleton)"));
		return true;
	}

	int32 FrameCount()
	{
		return Runner->Anim ? FMath::Max(1, Runner->Anim->GetNumberOfSampledKeys()) : 1;
	}

	float FrameTime(int32 Index)
	{
		const UAnimSequence* Anim = Runner->Anim;
		return FMath::Min(Index / Anim->GetSamplingFrameRate().AsDecimal(), static_cast<double>(Anim->GetPlayLength()));
	}

	void LogContact(const TCHAR* ClipName, const TCHAR* BodyName, const TWeakObjectPtr<ASkeletalMeshActor>& Body, const FVector& Hang, int32 Index)
	{
		const USkeletalMeshComponent* Mesh = MeshOf(Body);
		if (!Mesh)
		{
			return;
		}
		const FRunner& R = *Runner;
		FString Line;
		for (const TCHAR* Limb : Limbs)
		{
			const FVector P = Mesh->GetBoneLocation(Limb, EBoneSpaces::WorldSpace);
			const float Z = P.Z - R.FloorZ;
			const float Past = FVector::DotProduct(P - Hang, R.Forward) - R.PlaneCm;
			Line += FString::Printf(TEXT(" %s=%.1f/%.1f"), Limb, Z, Past);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: CONTACT clip=%s body=%s frame=%d%s"), ClipName, BodyName, Index, *Line);
	}

	FVector HangOf(const TWeakObjectPtr<ASkeletalMeshActor>& Body)
	{
		const FRunner& R = *Runner;
		const float Offset = Body == R.Operative ? BodySpacingCm : 0.f;
		return FVector(R.Base.X, R.Base.Y, R.FloorZ) + R.Right * Offset;
	}

	/** Places the camera for the current view; hides the body the side profile does not show. */
	void AimView(UWorld* World)
	{
		FRunner& R = *Runner;
		const float Low = R.FloorZ;
		// The loop frames the climber, not the whole 11-rung ladder; the top clips also frame the platform.
		const bool bLoop = Clips[R.ClipIndex].Kind == EClipKind::Loop;
		const float High = bLoop ? R.MaxRootZ + 210.f : FMath::Max(R.MaxRootZ + 200.f, R.TopZ + 190.f);
		const float Mid = (Low + High) * 0.5f;
		const float Span = High - Low + 60.f;
		const FVector Ahead = R.Forward * (R.PlaneCm * 0.6f);
		FVector Target, Eye;
		float Fov;
		if (R.View == 0)
		{
			Target = FVector(R.Base.X, R.Base.Y, Mid) + R.Right * (BodySpacingCm * 0.5f) + Ahead;
			const FVector Direction = (-R.Forward * 0.72f + R.Right * 0.69f + FVector(0.f, 0.f, 0.3f)).GetSafeNormal();
			Fov = 60.f;
			const float VerticalHalf = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * 720.f / 1280.f);
			Eye = Target + Direction * FMath::Max(Span * 0.5f / FMath::Tan(VerticalHalf) * 1.1f, 650.f);
		}
		else
		{
			const TWeakObjectPtr<ASkeletalMeshActor>& Shown = R.View == 1 ? R.Manny : R.Operative;
			Target = HangOf(Shown) + FVector(0.f, 0.f, Mid - R.FloorZ) + Ahead + R.Right * R.LateralCm;
			Fov = 50.f;
			const float VerticalHalf = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * 720.f / 1280.f);
			// ~20 deg off the pure profile (towards the climber's back) so the rungs read as bars, not dots.
			const FVector Direction = (R.Right * 0.94f - R.Forward * 0.34f).GetSafeNormal();
			Eye = Target + Direction * (Span * 0.5f / FMath::Tan(VerticalHalf) * 1.08f);
		}
		const bool bShowManny = R.View != 2;
		const bool bShowOperative = R.View != 1;
		if (R.Manny.IsValid())
		{
			R.Manny->SetActorHiddenInGame(!bShowManny);
		}
		if (R.Operative.IsValid())
		{
			R.Operative->SetActorHiddenInGame(!bShowOperative);
		}
		for (const TWeakObjectPtr<AActor>& Prop : R.MannyProps)
		{
			if (Prop.IsValid())
			{
				Prop->SetActorHiddenInGame(!bShowManny);
			}
		}
		for (const TWeakObjectPtr<AActor>& Prop : R.OperativeProps)
		{
			if (Prop.IsValid())
			{
				Prop->SetActorHiddenInGame(!bShowOperative);
			}
		}
		if (!R.Camera.IsValid())
		{
			R.Camera = World->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
			if (ACameraActor* Camera = R.Camera.Get())
			{
				UCameraComponent* Component = Camera->GetCameraComponent();
				Component->bConstrainAspectRatio = false;
				Component->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Component->PostProcessSettings.MotionBlurAmount = 0.f;
			}
		}
		if (ACameraActor* Camera = R.Camera.Get())
		{
			Camera->GetCameraComponent()->SetFieldOfView(Fov);
			Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	const TCHAR* ViewName(int32 View)
	{
		return View == 0 ? TEXT("a") : View == 1 ? TEXT("m") : TEXT("o");
	}

	void Setup(UWorld* World)
	{
		FRunner& R = *Runner;
		USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, MannyMesh);
		UClass* OperativeBp = LoadClass<ACharacter>(nullptr, OperativeClass);
		const ACharacter* OperativeCdo = OperativeBp ? OperativeBp->GetDefaultObject<ACharacter>() : nullptr;
		const USkeletalMeshComponent* OperativeLook = OperativeCdo ? OperativeCdo->GetMesh() : nullptr;
		USkeletalMesh* OperativeMesh = OperativeLook ? OperativeLook->GetSkeletalMeshAsset() : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: Manny %s, operative %s (skeleton %s)"), Manny ? *Manny->GetPathName() : TEXT("MISSING"),
			OperativeMesh ? *OperativeMesh->GetPathName() : TEXT("MISSING"),
			OperativeMesh && OperativeMesh->GetSkeleton() ? *OperativeMesh->GetSkeleton()->GetPathName() : TEXT("-"));
		// A free stage high above the map (the map's own sky and sun light it).
		R.Base = FVector(0.f, 0.f, 3000.f);
		R.Manny = SpawnBody(World, Manny, R.Base, nullptr);
		R.Operative = SpawnBody(World, OperativeMesh, R.Base + FVector(0.f, BodySpacingCm, 0.f), OperativeLook);
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		if (R.Frame > 30 * 600)
		{
			Finish(TEXT("timeout"));
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
		const FClip& Clip = Clips[R.ClipIndex];
		switch (R.Phase)
		{
		case EPhase::Setup:
			Setup(World);
			if (!R.Manny.IsValid() || !R.Operative.IsValid() || !StartClip(Clip))
			{
				Finish(TEXT("setup failed"));
				return;
			}
			R.Phase = EPhase::MeasureStart;
			R.Wait = 3;
			return;
		case EPhase::MeasureStart:
		{
			const USkeletalMeshComponent* Mesh = MeshOf(R.Manny);
			R.RootStart = Mesh->GetBoneLocation(TEXT("root"), EBoneSpaces::WorldSpace);
			if (Clip.Kind == EClipKind::Loop)
			{
				// The rungs sit at the hands of the loop's first frame (+2 cm: wrist bone -> bar centre).
				const FVector Hands = (Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace)
					+ Mesh->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace)) * 0.5f;
				// The UE mannequin faces the mesh's +Y; the hands sit a little to one side, which moves the ladder centre, not its facing.
				const FVector Reach = FVector(Hands.X - R.RootStart.X, Hands.Y - R.RootStart.Y, 0.f);
				R.Forward = Mesh->GetComponentTransform().TransformVectorNoScale(FVector(0.f, 1.f, 0.f)).GetSafeNormal2D();
				R.Right = FVector::CrossProduct(FVector::UpVector, R.Forward);
				R.PlaneCm = FVector::DotProduct(Reach, R.Forward) + 2.f;
				const FVector Feet = (Mesh->GetBoneLocation(TEXT("ball_l"), EBoneSpaces::WorldSpace)
					+ Mesh->GetBoneLocation(TEXT("ball_r"), EBoneSpaces::WorldSpace)) * 0.5f;
				R.LateralCm = FVector::DotProduct((Hands + Feet) * 0.5f - R.RootStart, R.Right);
				UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: forward %s, hands %.1f cm ahead of the root, ladder centre %.1f cm to the right"),
					*R.Forward.ToCompactString(), Reach.Size(), R.LateralCm);
			}
			SetTime(R.Anim->GetPlayLength());
			R.Phase = EPhase::MeasureEnd;
			R.Wait = 2;
			return;
		}
		case EPhase::MeasureEnd:
		{
			const USkeletalMeshComponent* Mesh = MeshOf(R.Manny);
			R.RootEnd = Mesh->GetBoneLocation(TEXT("root"), EBoneSpaces::WorldSpace);
			const FVector Delta = R.RootEnd - R.RootStart;
			UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: %s root delta forward %.1f right %.1f up %.1f cm"), Clip.Short,
				FVector::DotProduct(Delta, R.Forward), FVector::DotProduct(Delta, R.Right), Delta.Z);
			// Move both bodies so the hanging root (start, or the end of IntoFromTop) stands on the stage at Base.
			const FVector HangRoot = Clip.Kind == EClipKind::IntoFromTop ? R.RootEnd : R.RootStart;
			const FVector Shift = R.Base - HangRoot;
			R.Manny->AddActorWorldOffset(Shift);
			R.Operative->SetActorLocation(R.Manny->GetActorLocation() + R.Right * BodySpacingCm);
			R.FloorZ = R.Base.Z;
			R.TopZ = Clip.Kind == EClipKind::ClimbOut ? R.RootEnd.Z + Shift.Z
				: Clip.Kind == EClipKind::IntoFromTop ? R.RootStart.Z + Shift.Z : R.FloorZ + 11 * RungPitchCm;
			R.MaxRootZ = FMath::Max(R.RootStart.Z, R.RootEnd.Z) + Shift.Z;
			R.Phase = EPhase::Build;
			return;
		}
		case EPhase::Build:
			if (R.SharedProps.Num() == 0)
			{
				// One dark stage slab under both bodies, shared by every clip.
				Box(World, FVector(R.Base.X, R.Base.Y, R.Base.Z), FVector(-900.f, -700.f, -20.f), FVector(900.f, 1000.f, 0.f),
					FLinearColor(0.12f, 0.13f, 0.15f), R.SharedProps);
			}
			BuildLadder(World, HangOf(R.Manny), Clip, R.MannyProps);
			BuildLadder(World, HangOf(R.Operative), Clip, R.OperativeProps);
			R.View = 0;
			R.ShotIndex = 0;
			R.bShotPending = false;
			AimView(World);
			SetTime(FrameTime(0));
			IFileManager::Get().MakeDirectory(*(R.Dir / Clip.Short), true);
			R.Phase = EPhase::Pass;
			R.Wait = WarmUpFrames;
			return;
		case EPhase::Pass:
		{
			if (R.bShotPending)
			{
				// The screenshot was taken in this frame's draw: move to the next frame of the clip.
				R.bShotPending = false;
				++R.ShotIndex;
				if (R.ShotIndex >= FrameCount())
				{
					R.ShotIndex = 0;
					++R.View;
					if (R.View > 2)
					{
						ClearProps(R.MannyProps);
						ClearProps(R.OperativeProps);
						++R.ClipIndex;
						if (R.ClipIndex >= UE_ARRAY_COUNT(Clips))
						{
							R.Phase = EPhase::Done;
							Finish(TEXT("all clips"));
							return;
						}
						R.Manny->SetActorLocation(R.Base);
						R.Operative->SetActorLocation(R.Base + R.Right * BodySpacingCm);
						StartClip(Clips[R.ClipIndex]);
						R.Phase = EPhase::MeasureStart;
						R.Wait = 3;
						return;
					}
					AimView(World);
					SetTime(FrameTime(0));
					R.Wait = WarmUpFrames;
					return;
				}
				SetTime(FrameTime(R.ShotIndex));
				return; // the pose is evaluated in the next frame's tick
			}
			if (R.View == 0)
			{
				LogContact(Clip.Short, TEXT("manny"), R.Manny, HangOf(R.Manny), R.ShotIndex);
				if (R.bOperativePlays)
				{
					LogContact(Clip.Short, TEXT("operative"), R.Operative, HangOf(R.Operative), R.ShotIndex);
				}
			}
			const FString Path = R.Dir / Clip.Short / FString::Printf(TEXT("%s_%04d.png"), ViewName(R.View), R.ShotIndex);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
			R.bShotPending = true;
			return;
		}
		default:
			return;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/LadderClips"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		UE_LOG(LogCodexTactics, Display, TEXT("LadderClipShots: %d clips -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Clips)), *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LadderClipShots"),
		TEXT("Dev: rendered frames of the ladder clips (UE5 Manny + the operative body on graybox ladders, 3 views) into "
			"Saved/Screenshots/LadderClips with CONTACT log lines; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
