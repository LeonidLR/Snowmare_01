// Dev-only check of the art pipeline's optimized operative body (docs/pipeline/CHARACTER_SINGLE_SLOT.md, stage 8;
// UE-only, no Godot reference): SK_Post_Apo_Survivor_a_1slot (one material slot, ~50k triangles, baked atlas) next to the
// original SK_Post_Apo_Survivor_a (14 slots, 213k triangles), swapped at RUNTIME on two operatives (no Blueprint is edited).
//   Headless asserts:  Scripts/smoke_parallel.ps1 -Commands OptimizedCharacterSmoke
//   Rendered stills:   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720
//                        -nosound -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.OptimizedCharacterSmoke"
//     -> Saved/Screenshots/<Platform>/OptimizedCharacter/NN_<state>_<tactical|close>.png, front view: the leader ("Commander" tag,
//        original body) on the right of the picture, the second operative (optimized body) on the left.
// Its own empty arena (SmokeUtils::FindClearArena); the leader wears the original body, the second operative the optimized
// one with the leader's anim class and mesh transform; the others wait behind. States: idle, walk, crouch, prone, aim (a frozen
// hound, one forced shot), vault (a barricade across each lane), low cover. Asserts: one material slot (the MI of
// M_CharacterMaster with all four textures), LOD0 ~50k triangles, >= 3 LODs, same skeleton and physics asset as the original,
// bounds within 10 % of the original's, no load warnings about the new assets, and in every state both bodies are animated
// (the hands are far from the reference pose: no T-pose). Logs triangles / sections per LOD and the texture memory of both.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "PixelFormat.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interactables/BarricadeActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace OptimizedCharacterSmoke
{
	const TCHAR* OptimizedPath = TEXT("/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/SK_Post_Apo_Survivor_a_1slot.SK_Post_Apo_Survivor_a_1slot");
	const TCHAR* OriginalPath = TEXT("/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a.SK_Post_Apo_Survivor_a");
	const TCHAR* MasterPath = TEXT("/Game/Characters/Materials/M_CharacterMaster.M_CharacterMaster");
	constexpr int32 TargetTriangles = 50000;
	constexpr float LaneHalfCm = 110.f;

	/** Load warnings / errors that mention the new assets (missing skeleton, texture, shader compile failure ...). */
	class FWarningCatcher : public FOutputDevice
	{
	public:
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity > ELogVerbosity::Warning)
			{
				return;
			}
			const FString Line(V);
			for (const TCHAR* Key : { TEXT("Post_Apo_Survivor_a_1slot"), TEXT("MI_Post_Apo_Survivor_a"), TEXT("M_CharacterMaster"),
				TEXT("T_Post_Apo_Survivor_a_"), TEXT("Optimized/") })
			{
				if (Line.Contains(Key))
				{
					Lines.Add(FString::Printf(TEXT("[%s] %s"), *Category.ToString(), *Line));
					return;
				}
			}
		}
	};

	using FStep = TFunction<int32(UWorld*)>;

	struct FRunner
	{
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Wait = 0;
		int32 Frame = 0;
		int32 Failures = 0;
		int32 Shots = 0;
		bool bRender = false;
		FString Dir;
		FDelegateHandle Handle;
		TWeakObjectPtr<AOperativeCharacter> Original;
		TWeakObjectPtr<AOperativeCharacter> Optimized;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TArray<TWeakObjectPtr<AActor>> Props;
		SmokeUtils::FClearArena Arena;
		FCoverSlot Slots[2];
		float VaultStartFrame = -1;
		TUniquePtr<FWarningCatcher> Catcher;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke %s: %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish(bool bComplete)
	{
		FRunner& R = *Runner;
		if (R.Catcher)
		{
			GLog->RemoveOutputDevice(R.Catcher.Get());
			for (const FString& Line : R.Catcher->Lines)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke warning: %s"), *Line);
			}
			Check(R.Catcher->Lines.Num() == 0, FString::Printf(TEXT("%d warnings / errors about the new assets"), R.Catcher->Lines.Num()));
		}
		Check(bComplete, TEXT("all states ran"));
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: %d screenshots in %s"), R.Shots, *R.Dir);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), R.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(R.Handle);
		FPlatformMisc::RequestExit(false, TEXT("OptimizedCharacterSmoke"));
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		if (R.Frame > 60 * 240)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), R.Index));
			Finish(false);
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
			Finish(true);
			return;
		}
		const int32 Result = R.Steps[R.Index](World);
		if (Result == -2)
		{
			Finish(false);
			return;
		}
		if (Result >= 0)
		{
			++R.Index;
			R.Wait = Result;
		}
	}

	// ------------------------------------------------------------------------------------------------ asset checks

	int32 Triangles(const USkeletalMesh* Mesh, int32 Lod, int32* OutSections = nullptr)
	{
		const FSkeletalMeshRenderData* Data = Mesh ? Mesh->GetResourceForRendering() : nullptr;
		if (!Data || !Data->LODRenderData.IsValidIndex(Lod))
		{
			return -1;
		}
		const FSkeletalMeshLODRenderData& LodData = Data->LODRenderData[Lod];
		if (OutSections)
		{
			*OutSections = LodData.RenderSections.Num();
		}
		return static_cast<int32>(LodData.GetTotalFaces());
	}

	/** GPU size of the cooked mip chain from the platform data (no RHI resource needed: works under -nullrhi). */
	int64 PlatformBytes(const UTexture* Texture)
	{
		const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		const FTexturePlatformData* Data = Texture2D ? Texture2D->GetPlatformData() : nullptr;
		if (!Data || Data->PixelFormat >= PF_MAX)
		{
			return 0;
		}
		const FPixelFormatInfo& Format = GPixelFormats[Data->PixelFormat];
		int64 Bytes = 0;
		for (const FTexture2DMipMap& Mip : Data->Mips)
		{
			const int64 BlocksX = FMath::DivideAndRoundUp<int64>(Mip.SizeX, FMath::Max<int64>(1, Format.BlockSizeX));
			const int64 BlocksY = FMath::DivideAndRoundUp<int64>(Mip.SizeY, FMath::Max<int64>(1, Format.BlockSizeY));
			Bytes += BlocksX * BlocksY * Format.BlockBytes;
		}
		UE_LOG(LogCodexTactics, Verbose, TEXT("OptimizedCharacterSmoke: %s %dx%d %s %.2f MB"), *Texture->GetName(), Data->SizeX, Data->SizeY,
			Format.Name, Bytes / 1048576.0);
		return Bytes;
	}

	int64 TextureBytes(const UMaterialInterface* Material, TSet<const UTexture*>& Seen)
	{
		int64 Bytes = 0;
		if (!Material)
		{
			return 0;
		}
		// Referenced (not "used"): the used list comes from compiled shaders, which a -nullrhi run does not have.
		TSet<const UTexture*> Used;
		Material->GetReferencedTexturesAndOverrides(Used);
		for (const UTexture* Texture : Used)
		{
			if (Texture && !Seen.Contains(Texture))
			{
				Seen.Add(Texture);
				Bytes += PlatformBytes(Texture);
			}
		}
		return Bytes;
	}

	int32 CheckAssets(UWorld* World)
	{
		USkeletalMesh* Optimized = LoadObject<USkeletalMesh>(nullptr, OptimizedPath);
		USkeletalMesh* Original = LoadObject<USkeletalMesh>(nullptr, OriginalPath);
		Check(Optimized && Original, TEXT("both meshes load"));
		if (!Optimized || !Original)
		{
			return -2;
		}
		const TArray<FSkeletalMaterial>& Materials = Optimized->GetMaterials();
		Check(Materials.Num() == 1, FString::Printf(TEXT("one material slot (%d; original %d)"), Materials.Num(), Original->GetMaterials().Num()));
		const UMaterialInstance* Instance = Materials.Num() ? Cast<UMaterialInstance>(Materials[0].MaterialInterface) : nullptr;
		const UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, MasterPath);
		Check(Instance && Master && Instance->Parent == Master, FString::Printf(TEXT("slot 0 = %s, parent %s"), *GetPathNameSafe(Instance),
			Instance ? *GetPathNameSafe(Instance->Parent) : TEXT("-")));
		for (const TCHAR* Param : { TEXT("BaseColor"), TEXT("Normal"), TEXT("ORM"), TEXT("Sheen") })
		{
			UTexture* Texture = nullptr;
			const bool bHas = Instance && Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(FName(Param)), Texture) && Texture;
			Check(bHas, FString::Printf(TEXT("texture %s = %s"), Param, *GetNameSafe(Texture)));
		}
		Check(Optimized->GetSkeleton() && Optimized->GetSkeleton() == Original->GetSkeleton(),
			FString::Printf(TEXT("same skeleton %s"), *GetPathNameSafe(Optimized->GetSkeleton())));
		Check(Optimized->GetPhysicsAsset() == Original->GetPhysicsAsset(), FString::Printf(TEXT("physics asset %s"), *GetPathNameSafe(Optimized->GetPhysicsAsset())));
		Check(Optimized->GetLODNum() >= 3, FString::Printf(TEXT("%d LODs (original %d)"), Optimized->GetLODNum(), Original->GetLODNum()));
		for (int32 Lod = 0; Lod < Optimized->GetLODNum(); ++Lod)
		{
			int32 Sections = 0;
			const int32 Tris = Triangles(Optimized, Lod, &Sections);
			UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: optimized LOD%d %d triangles, %d sections"), Lod, Tris, Sections);
			if (Lod == 0)
			{
				Check(FMath::Abs(Tris - TargetTriangles) <= TargetTriangles / 10, FString::Printf(TEXT("LOD0 %d triangles ~ %d"), Tris, TargetTriangles));
				Check(Sections == 1, FString::Printf(TEXT("LOD0 one section (%d)"), Sections));
			}
		}
		for (int32 Lod = 0; Lod < Original->GetLODNum(); ++Lod)
		{
			int32 Sections = 0;
			const int32 Tris = Triangles(Original, Lod, &Sections);
			UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: original LOD%d %d triangles, %d sections"), Lod, Tris, Sections);
		}
		const FVector A = Optimized->GetBounds().BoxExtent;
		const FVector B = Original->GetBounds().BoxExtent;
		const bool bBounds = A.X > 1.f && FMath::Abs(A.X - B.X) <= B.X * 0.1f && FMath::Abs(A.Y - B.Y) <= B.Y * 0.1f && FMath::Abs(A.Z - B.Z) <= B.Z * 0.1f;
		Check(bBounds, FString::Printf(TEXT("bounds extent %s vs original %s"), *A.ToCompactString(), *B.ToCompactString()));
		TSet<const UTexture*> SeenA;
		TSet<const UTexture*> SeenB;
		int64 BytesOptimized = TextureBytes(Instance, SeenA);
		int64 BytesOriginal = 0;
		for (const FSkeletalMaterial& Each : Original->GetMaterials())
		{
			BytesOriginal += TextureBytes(Each.MaterialInterface, SeenB);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: texture memory (all mips) optimized %.1f MB in %d textures, original %.1f MB in %d textures"),
			BytesOptimized / 1048576.0, SeenA.Num(), BytesOriginal / 1048576.0, SeenB.Num());
		return 0;
	}

	// ------------------------------------------------------------------------------------------------ scene

	FVector LaneStart(int32 Lane)
	{
		const SmokeUtils::FClearArena& A = Runner->Arena;
		return A.Origin + A.Right * (Lane == 0 ? -LaneHalfCm : LaneHalfCm) + FVector(0.f, 0.f, 92.f);
	}

	void ResetPair(UWorld* World)
	{
		int32 Lane = 0;
		for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
		{
			if (Op)
			{
				if (Op->bInCover)
				{
					Op->LeaveCover(TEXT("OptimizedCharacterSmoke"));
				}
				Op->StopOperative();
				Op->SetStance(EOperativeStance::Standing);
				Op->TeleportTo(LaneStart(Lane), Runner->Arena.Forward.Rotation(), false, true);
			}
			++Lane;
		}
	}

	int32 SetUp(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad || !Squad->GetLeader() || Squad->GetMembers().Num() < 2)
		{
			return -1;
		}
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		AOperativeCharacter* Lead = Members[0];
		AOperativeCharacter* Second = Members[1];
		if (!Squad->IsSoloMode())
		{
			Squad->ToggleSoloMode();
		}
		TArray<const AActor*> Ignore;
		for (const AOperativeCharacter* Each : Members)
		{
			Ignore.Add(Each);
		}
		Runner->Arena = SmokeUtils::FindClearArena(World, Lead->GetActorLocation(), Lead->GetActorForwardVector(),
			FBox2D(FVector2D(-500.f, -450.f), FVector2D(1300.f, 450.f)), Ignore);
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: arena %s at %s facing %s (%d tried)"), Runner->Arena.bFound ? TEXT("found") : TEXT("NOT found"),
			*Runner->Arena.Origin.ToCompactString(), *Runner->Arena.Forward.ToCompactString(), Runner->Arena.Tried);
		Check(Runner->Arena.bFound, TEXT("an empty arena on the map"));
		if (!Runner->Arena.bFound)
		{
			return -2;
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			SmokeUtils::ParkEnemyFarAway(*It);
		}
		USkeletalMesh* Optimized = LoadObject<USkeletalMesh>(nullptr, OptimizedPath);
		USkeletalMesh* Original = LoadObject<USkeletalMesh>(nullptr, OriginalPath);
		// Both bodies on the leader's anim class and mesh transform, LOD0 forced so the stills compare the meshes, not LOD distance.
		USkeletalMeshComponent* LeadMesh = Lead->GetMesh();
		UClass* AnimClass = LeadMesh->GetAnimClass();
		const FTransform MeshTransform = LeadMesh->GetRelativeTransform();
		LeadMesh->SetSkeletalMeshAsset(Original);
		USkeletalMeshComponent* SecondMesh = Second->GetMesh();
		SecondMesh->SetSkeletalMeshAsset(Optimized);
		SecondMesh->SetRelativeTransform(MeshTransform);
		if (AnimClass && SecondMesh->GetAnimClass() != AnimClass)
		{
			SecondMesh->SetAnimInstanceClass(AnimClass);
		}
		for (USkeletalMeshComponent* Mesh : { LeadMesh, SecondMesh })
		{
			Mesh->SetForcedLOD(1);
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		Check(SecondMesh->GetNumMaterials() == 1, FString::Printf(TEXT("the swapped body renders %d material(s) (original %d)"),
			SecondMesh->GetNumMaterials(), LeadMesh->GetNumMaterials()));
		Runner->Original = Lead;
		Runner->Optimized = Second;
		for (AOperativeCharacter* Each : Members)
		{
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
			if (Each->HealthComponent)
			{
				Each->HealthComponent->SetMaxHealth(100000.f);
			}
			if (Each != Lead && Each != Second)
			{
				Each->StopOperative();
				Each->TeleportTo(SmokeUtils::FreeSpot(World, Runner->Arena.Origin - Runner->Arena.Forward * 450.f, Each), Runner->Arena.Forward.Rotation(), false, true);
			}
		}
		ResetPair(World);
		if (Runner->bRender)
		{
			// No motion blur on the stills (the vault). The HUD panels stay (the game HUD, not removable from here).
			if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
			{
				Blur->Set(0, ECVF_SetByConsole);
			}
		}
		Runner->Camera = World->SpawnActor<ACameraActor>(Runner->Arena.Origin + FVector(0.f, 0.f, 500.f), FRotator(-60.f, 0.f, 0.f));
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
			}
		}
		return 90;
	}

	/** Max distance of the hands from the reference pose (component space): an animated body moves them, a T-pose does not. */
	float HandsFromRefPose(const USkeletalMeshComponent* Mesh)
	{
		const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!Asset)
		{
			return 0.f;
		}
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		float Max = 0.f;
		for (const TCHAR* Bone : { TEXT("hand_l"), TEXT("hand_r") })
		{
			const int32 Index = Ref.FindBoneIndex(Bone);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			FTransform RefComponent = Ref.GetRefBonePose()[Index];
			for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
			{
				RefComponent = RefComponent * Ref.GetRefBonePose()[Parent];
			}
			const FVector Posed = Mesh->GetBoneTransform(Index, FTransform::Identity).GetLocation();
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(Posed, RefComponent.GetLocation())));
		}
		return Max;
	}

	void CheckPose(const TCHAR* State)
	{
		const AOperativeCharacter* A = Runner->Original.Get();
		const AOperativeCharacter* B = Runner->Optimized.Get();
		const float HandsA = A ? HandsFromRefPose(A->GetMesh()) : 0.f;
		const float HandsB = B ? HandsFromRefPose(B->GetMesh()) : 0.f;
		const UAnimInstance* AnimB = B ? B->GetMesh()->GetAnimInstance() : nullptr;
		Check(AnimB && HandsB > 8.f, FString::Printf(TEXT("%s: optimized body animated (hands %.0f cm from the ref pose; original %.0f cm), anim %s"),
			State, HandsB, HandsA, *GetNameSafe(AnimB ? AnimB->GetClass() : nullptr)));
	}

	void AimCamera(bool bClose, float TargetZ)
	{
		const AOperativeCharacter* A = Runner->Original.Get();
		const AOperativeCharacter* B = Runner->Optimized.Get();
		ACameraActor* Camera = Runner->Camera.Get();
		if (!A || !B || !Camera)
		{
			return;
		}
		const FVector Mid = (A->GetActorLocation() + B->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, TargetZ);
		const FVector F = Runner->Arena.Forward;
		const float Pitch = bClose ? 12.f : 55.f;
		// Straight from the front: both bodies at the same distance (a 3/4 yaw puts one in front of the other).
		const FVector Dir = F * FMath::Cos(FMath::DegreesToRadians(Pitch)) + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(Pitch));
		const FVector Eye = Mid + Dir * (bClose ? 600.f : 1300.f);
		Camera->SetActorLocationAndRotation(Eye, (Mid - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(bClose ? 50.f : 45.f);
	}

	/** Pose check + (when rendering) the tactical and the close 3/4 still of the state. */
	void AddShots(TArray<FStep>& S, const TCHAR* State, float TargetZ = 90.f)
	{
		S.Add([State](UWorld*) { CheckPose(State); AimCamera(false, 0.f); return 3; });
		S.Add([State](UWorld*)
		{
			if (Runner->bRender)
			{
				FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s_tactical.png"), Runner->Shots++, State), false, false);
			}
			return 3;
		});
		S.Add([TargetZ](UWorld*) { AimCamera(true, TargetZ); return 3; });
		S.Add([State](UWorld*)
		{
			if (Runner->bRender)
			{
				FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s_close.png"), Runner->Shots++, State), false, false);
			}
			return 3;
		});
	}

	ABarricadeActor* SpawnBarricade(UWorld* World, const FVector& Centre)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const float GroundZ = Runner->Arena.Origin.Z;
		ABarricadeActor* Barricade = World->SpawnActor<ABarricadeActor>(FVector(Centre.X, Centre.Y, GroundZ + ABarricadeActor::HeightCm * 0.5f),
			FRotator(0.f, Runner->Arena.Forward.Rotation().Yaw + 90.f, 0.f), Params);
		Runner->Props.Add(Barricade);
		return Barricade;
	}

	void ClearProps()
	{
		for (TWeakObjectPtr<AActor>& Prop : Runner->Props)
		{
			if (AActor* Actor = Prop.Get())
			{
				Actor->Destroy();
			}
		}
		Runner->Props.Reset();
	}

	/** Waits until this body is in the air over the barricade (8 s at most), then its stills. */
	void AddVaultShots(TArray<FStep>& S, bool bOriginal)
	{
		S.Add([bOriginal](UWorld*)
		{
			const AOperativeCharacter* Op = bOriginal ? Runner->Original.Get() : Runner->Optimized.Get();
			if (Op && Op->IsVaulting())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: %s vaulting at frame %d"), bOriginal ? TEXT("original") : TEXT("optimized"),
					Runner->Frame - static_cast<int32>(Runner->VaultStartFrame));
				return 8;
			}
			if (Runner->Frame - Runner->VaultStartFrame > 60 * 8)
			{
				Check(false, FString::Printf(TEXT("vault: the %s body did not vault within 8 s"), bOriginal ? TEXT("original") : TEXT("optimized")));
				return 0;
			}
			return -1;
		});
		AddShots(S, bOriginal ? TEXT("vault_original_airborne") : TEXT("vault_optimized_airborne"));
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return CheckAssets(World) == -2 ? -2 : 30; });
		S.Add([](UWorld* World) { return SetUp(World); });
		// idle
		AddShots(S, TEXT("idle"));
		// walk: both straight ahead, stills ~1.3 s in
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				const FVector From = LaneStart(Lane++);
				Op->OrderMoveTo(From + Runner->Arena.Forward * 1000.f, false);
			}
			return 80;
		});
		AddShots(S, TEXT("walk"));
		S.Add([](UWorld* World) { ResetPair(World); return 60; });
		// crouch / prone
		S.Add([](UWorld*) { Runner->Original->SetStance(EOperativeStance::Crouching); Runner->Optimized->SetStance(EOperativeStance::Crouching); return 90; });
		AddShots(S, TEXT("crouch"), 60.f);
		S.Add([](UWorld*) { Runner->Original->SetStance(EOperativeStance::Prone); Runner->Optimized->SetStance(EOperativeStance::Prone); return 150; });
		AddShots(S, TEXT("prone"), 20.f);
		S.Add([](UWorld* World) { ResetPair(World); return 120; });
		// aim: a frozen hound 8 m ahead between the lanes, one forced shot each
		S.Add([](UWorld* World)
		{
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			const FVector At = Runner->Arena.Origin + Runner->Arena.Forward * 800.f + FVector(0.f, 0.f, 60.f);
			AEnemyCharacter* Hound = Waves ? Waves->SpawnEnemy(EEnemyArchetype::FrostHound, At, (-Runner->Arena.Forward).Rotation()) : nullptr;
			if (Hound)
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f);
				Runner->Props.Add(Hound);
			}
			Check(Hound != nullptr, TEXT("aim: target hound spawned"));
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				Op->bForceHitForTesting = true;
				if (Hound)
				{
					Op->ShootAtTarget(Hound, 0.f);
				}
			}
			return 18;
		});
		AddShots(S, TEXT("aim"));
		S.Add([](UWorld* World)
		{
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				Op->bForceHitForTesting = false;
			}
			ClearProps();
			ResetPair(World);
			return 120;
		});
		// vault: one barricade (3 m) across both lanes 2.5 m ahead, both walk 5.5 m over it; stills when each body is in the air
		S.Add([](UWorld* World)
		{
			SpawnBarricade(World, Runner->Arena.Origin + Runner->Arena.Forward * 250.f);
			return 120; // navmesh rebuild
		});
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				const EOperativeOrderResult Order = Op->OrderMoveTo(LaneStart(Lane++) + Runner->Arena.Forward * 550.f, false);
				Check(Order == EOperativeOrderResult::Accepted, FString::Printf(TEXT("vault: move order over the barricade (%d)"), static_cast<int32>(Order)));
			}
			Runner->VaultStartFrame = Runner->Frame;
			return 0;
		});
		AddVaultShots(S, true);
		AddVaultShots(S, false);
		S.Add([](UWorld* World) { ClearProps(); ResetPair(World); return 120; });
		// low cover: a barricade 3 m ahead of each lane, cover order to it
		S.Add([](UWorld* World)
		{
			SpawnBarricade(World, Runner->Arena.Origin + Runner->Arena.Forward * 300.f);
			return 120;
		});
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				const FVector Probe = LaneStart(Lane) + Runner->Arena.Forward * 275.f + FVector(0.f, 0.f, -62.f);
				const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Probe, Runner->Arena.Forward, Runner->Slots[Lane]);
				Check(bSlot, FString::Printf(TEXT("cover slot at lane %d"), Lane));
				Op->bIgnoreCoverThreatForTesting = true;
				if (bSlot)
				{
					// Straight onto the slot (no walk: two neighbours ordered side by side shuffle for the same wall).
					Op->StopOperative();
					Op->TeleportTo(Runner->Slots[Lane].WorldLocation + FVector(0.f, 0.f, 92.f), Runner->Slots[Lane].WallNormal.Rotation(), false, true);
					Op->EnterCover(Runner->Slots[Lane]);
				}
				++Lane;
			}
			Runner->VaultStartFrame = Runner->Frame;
			return 0;
		});
		S.Add([](UWorld*)
		{
			const bool bBoth = Runner->Original->bInCover && Runner->Optimized->bInCover;
			if (bBoth)
			{
				return 90;
			}
			if (Runner->Frame - Runner->VaultStartFrame > 60 * 10)
			{
				Check(false, FString::Printf(TEXT("cover: in cover original %d optimized %d after 10 s"), Runner->Original->bInCover ? 1 : 0,
					Runner->Optimized->bInCover ? 1 : 0));
				return 0;
			}
			return -1;
		});
		AddShots(S, TEXT("cover"), 50.f);
		S.Add([](UWorld* World)
		{
			for (AOperativeCharacter* Op : { Runner->Original.Get(), Runner->Optimized.Get() })
			{
				Op->bIgnoreCoverThreatForTesting = false;
			}
			ClearProps();
			ResetPair(World);
			return 30; // the last screenshot is written
		});
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->bRender = FApp::CanEverRender();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("OptimizedCharacter"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->Catcher = MakeUnique<FWarningCatcher>();
		GLog->AddOutputDevice(Runner->Catcher.Get());
		AddSteps(Runner->Steps);
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: %d steps, rendering %d -> %s"), Runner->Steps.Num(), Runner->bRender ? 1 : 0, *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.OptimizedCharacterSmoke"),
		TEXT("Dev: the art pipeline's one-slot operative body swapped at runtime next to the original (asserts; stills when rendering), then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
