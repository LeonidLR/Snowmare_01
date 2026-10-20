// Dev-only check of the art pipeline's optimized character bodies (docs/pipeline/CHARACTER_SINGLE_SLOT.md, stage 8;
// UE-only, no Godot reference): every SK_<Asset>_1slot (one material slot, triangle budget, baked atlas) next to its original
// body, swapped at RUNTIME (no Blueprint is edited). One case per asset (Cases below); a case whose optimized mesh is not
// imported yet is skipped (logged), so the smoke grows with the roll-out.
//   Headless asserts:  Scripts/smoke_parallel.ps1 -Commands OptimizedCharacterSmoke
//   Rendered stills:   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720
//                        -nosound -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.OptimizedCharacterSmoke [Asset=<Name>]"
//     -> Saved/Screenshots/<Platform>/OptimizedCharacter/<Asset>/NN_<state>_<tactical|close>.png, front view: the ORIGINAL body on
//        the right of the picture, the OPTIMIZED one on the left.
// Its own empty arena (SmokeUtils::FindClearArena).
//  Operatives: the leader wears the original body, the second operative the optimized one (both with the case Blueprint's anim
//   class and mesh transform); idle, walk, crouch, prone, aim (a frozen hound, one forced shot), vault (each body airborne), cover.
//  Enemies: two enemies of the case Blueprint placed as deaf guards (they idle), the right one keeps the original body (for a look
//   variant: the variant's source materials from the pipeline's material_sets.json), the left one gets the optimized body + the variant's MI; idle, move (a search walk),
//   attack (AttackTarget on an operative put in front), hit (a damage spec), death. Further look variants: an idle still each.
// Asserts per case: one material slot (an MI of M_CharacterMaster with its four textures, one MI per look variant), LOD0 within
// the triangle budget, >= 3 LODs, same skeleton and physics asset as the original, bounds within 10 %, no load warnings about
// the new assets, and in every state the optimized body is animated (a bone >= 5 cm off the reference pose: no T-pose). Logs
// triangles / sections per LOD and the texture memory of both bodies.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
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
#include "PixelFormat.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace OptimizedCharacterSmoke
{
	enum class EKind : uint8 { Operative, Enemy };

	/** One optimized body. Original = the body it replaces; Blueprint = the class it is seen on (anim class, mesh transform). */
	struct FCase
	{
		const TCHAR* Asset;
		EKind Kind;
		const TCHAR* Original;
		const TCHAR* Blueprint;
		int32 MaxTriangles;
		/** Unused (the look variants are discovered: MI_<Asset>[_<Variant>] in the asset's folder). */
		int32 Variants;
	};

	const FCase Cases[] = {
		{ TEXT("Post_Apo_Survivor_a"), EKind::Operative, TEXT("/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a.SK_Post_Apo_Survivor_a"),
			TEXT("/Game/Characters/Operatives/BP_Operative.BP_Operative_C"), 50000, 0 },
		{ TEXT("Female_Soldier"), EKind::Operative, TEXT("/Game/Female_Soldier/Mesh/SK_Female_soldier.SK_Female_Soldier"),
			TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C"), 50000, 0 },
		{ TEXT("FrozenPMC_Fighter"), EKind::Enemy, TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Mesh/SKM_FrozenPMC_Fighter.SKM_FrozenPMC_Fighter"),
			TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C"), 20000, 0 },
		{ TEXT("MilitaryZombie_1"), EKind::Enemy, TEXT("/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_1.SKM_base_mesh_set2_1"),
			TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C"), 20000, 3 },
		{ TEXT("MilitaryZombie_2"), EKind::Enemy, TEXT("/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_2.SKM_base_mesh_set2_2"),
			TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C"), 20000, 3 },
		{ TEXT("MilitaryZombie_3"), EKind::Enemy, TEXT("/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_3.SKM_base_mesh_set2_3"),
			TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C"), 20000, 3 },
		{ TEXT("Brute"), EKind::Enemy, TEXT("/Game/Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1.SK_base_mesh_1"),
			TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C"), 25000, 0 },
		{ TEXT("Marksman"), EKind::Enemy, TEXT("/Game/Biochemical_Monster_1/baze_mech/ue4/SKM_baze_mesh.SKM_baze_mesh"),
			TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C"), 20000, 0 },
		{ TEXT("Cutter"), EKind::Enemy, TEXT("/Game/Biochemical_Monster_2/baze_mech/ue4/SKM_baze_mesh.SKM_baze_mesh"),
			TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter.BP_Enemy_Cutter_C"), 20000, 0 },
		{ TEXT("Hound"), EKind::Enemy, TEXT("/Game/Combat_Dog/Mesh/SK_Combat_Dog.SK_Combat_Dog"),
			TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C"), 10000, 0 },
		{ TEXT("Frostbitten"), EKind::Enemy, TEXT("/Game/mutant_monster_2/base_mesh/SK_base_mesh.SK_base_mesh"),
			TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C"), 20000, 0 },
	};

	constexpr float LaneHalfCm = 110.f;

	FString Folder(const FCase& C)
	{
		return FString::Printf(TEXT("/Game/Characters/%s/Optimized/%s"), C.Kind == EKind::Operative ? TEXT("Operatives") : TEXT("Enemies"), C.Asset);
	}

	FString OptimizedPath(const FCase& C)
	{
		return FString::Printf(TEXT("%s/SK_%s_1slot.SK_%s_1slot"), *Folder(C), C.Asset, C.Asset);
	}

	constexpr int32 MaxVariants = 8;

	/** The look variants of an asset (texture sets): MI_<Asset> and MI_<Asset>_<Default|Set1..Set12>, as imported. */
	TArray<FString> FindInstances(const FCase& C)
	{
		TArray<FString> Found;
		TArray<FString> Suffixes = { TEXT(""), TEXT("_Default") };
		for (int32 Index = 1; Index <= 12; ++Index)
		{
			Suffixes.Add(FString::Printf(TEXT("_Set%d"), Index));
		}
		for (const FString& Suffix : Suffixes)
		{
			const FString Name = FString::Printf(TEXT("MI_%s%s"), C.Asset, *Suffix);
			const FString Path = FString::Printf(TEXT("%s/%s.%s"), *Folder(C), *Name, *Name);
			if (LoadObject<UMaterialInstance>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Found.Add(Path);
			}
		}
		return Found;
	}

	FString InstancePath(const FCase& C, int32 Variant);

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
			// The game's hit-flash overlay (M_CombatFeedback, VFX) lacks the skeletal-mesh usage flag: it warns for the
			// original bodies exactly the same way - not a problem of the optimized assets.
			if (Line.Contains(TEXT("Overlay material with missing skel mesh usage")))
			{
				return;
			}
			if (Line.Contains(TEXT("_1slot")) || Line.Contains(TEXT("M_CharacterMaster")) || Line.Contains(TEXT("/Optimized/")))
			{
				Lines.Add(FString::Printf(TEXT("[%s] %s"), *Category.ToString(), *Line));
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
		int32 CasesRun = 0;
		bool bRender = false;
		FString RootDir;
		FString Dir;
		FDelegateHandle Handle;
		const FCase* Case = nullptr;
		int32 Variant = 0;
		/** Original (lane 0, right of the picture) and optimized (lane 1) bodies of the current case. */
		TWeakObjectPtr<ACharacter> Original;
		TWeakObjectPtr<ACharacter> Optimized;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<AActor>> Props;
		SmokeUtils::FClearArena Arena;
		FCoverSlot Slots[2];
		int32 MarkFrame = 0;
		/** MI paths of the current case; [0] = the mesh's own slot material. */
		TArray<FString> Instances;
		/** A variant step found no such variant: its stills are skipped. */
		bool bSkip = false;
		TUniquePtr<FWarningCatcher> Catcher;
	};

	TSharedPtr<FRunner> Runner;

	FString InstancePath(const FCase& C, int32 Variant)
	{
		return Runner->Instances.IsValidIndex(Variant) ? Runner->Instances[Variant] : FString();
	}

	FString VariantLabel(int32 Variant)
	{
		const FString Path = Runner->Instances.IsValidIndex(Variant) ? Runner->Instances[Variant] : FString();
		FString Name = FPaths::GetExtension(Path); // "MI_<Asset>_<Variant>"
		Name.RemoveFromStart(FString::Printf(TEXT("MI_%s"), Runner->Case ? Runner->Case->Asset : TEXT("")));
		Name.RemoveFromStart(TEXT("_"));
		return Name.IsEmpty() ? FString(TEXT("default")) : Name;
	}

	/**
	 * The original body's materials of a look variant, by slot name: ArtPipeline/Characters/<Group>/<Asset>/material_sets.json
	 * (the pipeline's variant list; [{"name": "Set1", "slots": {slot: material}}] or [{slot: material}] = Set1..). Empty when the
	 * asset has no such file or the variant is not listed (the original keeps its own look).
	 */
	TMap<FName, FString> SourceLook(const FCase& C, int32 Variant)
	{
		TMap<FName, FString> Look;
		const FString Label = VariantLabel(Variant);
		const FString File = FPaths::Combine(FPaths::ProjectDir(), TEXT("ArtPipeline/Characters"),
			C.Kind == EKind::Operative ? TEXT("Operatives") : TEXT("Enemies"), C.Asset, TEXT("material_sets.json"));
		FString Text;
		TArray<TSharedPtr<FJsonValue>> Sets;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Sets))
		{
			return Look;
		}
		for (int32 Index = 0; Index < Sets.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Set = Sets[Index].IsValid() ? Sets[Index]->AsObject() : nullptr;
			if (!Set.IsValid())
			{
				continue;
			}
			FString Name = FString::Printf(TEXT("Set%d"), Index + 1);
			Set->TryGetStringField(TEXT("name"), Name);
			if (Name != Label)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Slots = nullptr;
			const TSharedPtr<FJsonObject> Map = Set->TryGetObjectField(TEXT("slots"), Slots) ? *Slots : Set;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Map->Values)
			{
				FString Path;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Path))
				{
					Look.Add(FName(*Pair.Key), Path);
				}
			}
		}
		return Look;
	}

	/** Dresses the ORIGINAL body in the source materials of the look variant (so both bodies show the same texture set). */
	void WearSourceLook(USkeletalMeshComponent* Mesh, const FCase& C, int32 Variant)
	{
		for (const TPair<FName, FString>& Slot : SourceLook(C, Variant))
		{
			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Slot.Value, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Mesh->SetMaterialByName(Slot.Key, Material);
			}
		}
	}

	/** Full-resolution textures for the stills: a freshly swapped look otherwise renders its lowest mips for a while. */
	void Prestream(USkeletalMeshComponent* Mesh)
	{
		if (Mesh)
		{
			Mesh->SetTextureForceResidentFlag(true);
			Mesh->PrestreamTextures(60.f, true);
		}
		// a look swapped in at runtime may still compile its shaders (drawn with a fallback): finish before the stills
#if WITH_EDITOR
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
#endif
	}

	AOperativeCharacter* OpA() { return Cast<AOperativeCharacter>(Runner->Original.Get()); }
	AOperativeCharacter* OpB() { return Cast<AOperativeCharacter>(Runner->Optimized.Get()); }
	AEnemyCharacter* EnA() { return Cast<AEnemyCharacter>(Runner->Original.Get()); }
	AEnemyCharacter* EnB() { return Cast<AEnemyCharacter>(Runner->Optimized.Get()); }

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke %s: [%s] %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"),
			Runner->Case ? Runner->Case->Asset : TEXT("-"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish(bool bComplete)
	{
		FRunner& R = *Runner;
		R.Case = nullptr;
		if (R.Catcher)
		{
			GLog->RemoveOutputDevice(R.Catcher.Get());
			for (const FString& Line : R.Catcher->Lines)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke warning: %s"), *Line);
			}
			Check(R.Catcher->Lines.Num() == 0, FString::Printf(TEXT("%d warnings / errors about the new assets"), R.Catcher->Lines.Num()));
		}
		Check(bComplete, FString::Printf(TEXT("all steps ran (%d cases)"), R.CasesRun));
		Check(R.CasesRun > 0, TEXT("at least one optimized body is imported"));
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: %d screenshots in %s"), R.Shots, *R.RootDir);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), R.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(R.Handle);
		FPlatformMisc::RequestExit(false, TEXT("OptimizedCharacterSmoke"));
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		if (R.Frame > 60 * 900)
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

	/** GPU size of the cooked mip chain from the platform data (no RHI resource needed). 0 under -nullrhi (no platform data). */
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

	/** -2: the case cannot run; -3: not imported yet (skipped). */
	int32 CheckAssets(const FCase& C)
	{
		USkeletalMesh* Optimized = LoadObject<USkeletalMesh>(nullptr, *OptimizedPath(C), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Optimized)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] not imported yet (%s) - skipped"), C.Asset, *OptimizedPath(C));
			return -3;
		}
		++Runner->CasesRun;
		USkeletalMesh* Original = LoadObject<USkeletalMesh>(nullptr, C.Original);
		UClass* Class = LoadClass<ACharacter>(nullptr, C.Blueprint);
		Check(Original != nullptr && Class != nullptr, FString::Printf(TEXT("original %s and Blueprint %s load"), C.Original, C.Blueprint));
		if (!Original || !Class)
		{
			return -2;
		}
		const TArray<FSkeletalMaterial>& Materials = Optimized->GetMaterials();
		Check(Materials.Num() == 1, FString::Printf(TEXT("one material slot (%d; original %d)"), Materials.Num(), Original->GetMaterials().Num()));
		int64 BytesOptimized = 0;
		TSet<const UTexture*> SeenA;
		Runner->Instances = FindInstances(C);
		{
			// the mesh's own slot material first: variant 0 = the look the mesh carries
			const UMaterialInterface* Own = Materials.Num() ? Materials[0].MaterialInterface.Get() : nullptr;
			const int32 OwnIndex = Own ? Runner->Instances.IndexOfByKey(Own->GetPathName()) : INDEX_NONE;
			Check(OwnIndex != INDEX_NONE, FString::Printf(TEXT("slot 0 = %s is one of the %d look variants"), *GetPathNameSafe(Own), Runner->Instances.Num()));
			if (OwnIndex > 0)
			{
				Runner->Instances.Swap(0, OwnIndex);
			}
		}
		for (int32 Variant = 0; Variant < Runner->Instances.Num(); ++Variant)
		{
			const UMaterialInstance* Instance = LoadObject<UMaterialInstance>(nullptr, *InstancePath(C, Variant));
			// M_CharacterMaster or one of its _Masked / _TwoSided twins (bodies with cut-outs / two-sided cloth)
			const bool bMaster = Instance && Instance->Parent && Instance->Parent->GetName().StartsWith(TEXT("M_CharacterMaster"))
				&& Instance->Parent->GetPathName().StartsWith(TEXT("/Game/Characters/Materials/"));
			Check(bMaster, FString::Printf(TEXT("%s, parent %s"), *InstancePath(C, Variant),
				Instance ? *GetPathNameSafe(Instance->Parent) : TEXT("-")));
			for (const TCHAR* Param : { TEXT("BaseColor"), TEXT("Normal"), TEXT("ORM"), TEXT("Sheen") })
			{
				UTexture* Texture = nullptr;
				const bool bHas = Instance && Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(FName(Param)), Texture) && Texture
					&& Texture->GetPathName().StartsWith(Folder(C));
				Check(bHas, FString::Printf(TEXT("variant %d texture %s = %s"), Variant, Param, *GetNameSafe(Texture)));
			}
			BytesOptimized += TextureBytes(Instance, SeenA);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] %d look variant(s): %s"), C.Asset, Runner->Instances.Num(),
			*FString::Join(Runner->Instances, TEXT(", ")));
		Check(Optimized->GetSkeleton() && Optimized->GetSkeleton() == Original->GetSkeleton(),
			FString::Printf(TEXT("same skeleton %s"), *GetPathNameSafe(Optimized->GetSkeleton())));
		{
			// the same bones in the same hierarchy as the original (no extra root above the skeleton)
			const FReferenceSkeleton& RefA = Optimized->GetRefSkeleton();
			const FReferenceSkeleton& RefB = Original->GetRefSkeleton();
			bool bSame = RefA.GetNum() == RefB.GetNum();
			for (int32 Bone = 0; bSame && Bone < RefA.GetNum(); ++Bone)
			{
				const int32 Other = RefB.FindBoneIndex(RefA.GetBoneName(Bone));
				const int32 ParentA = RefA.GetParentIndex(Bone);
				const int32 ParentB = Other == INDEX_NONE ? INDEX_NONE : RefB.GetParentIndex(Other);
				bSame = Other != INDEX_NONE && (ParentA == INDEX_NONE ? ParentB == INDEX_NONE
					: (ParentB != INDEX_NONE && RefA.GetBoneName(ParentA) == RefB.GetBoneName(ParentB)));
			}
			Check(bSame, FString::Printf(TEXT("same bones and hierarchy (%d bones, original %d, root %s)"), RefA.GetNum(), RefB.GetNum(),
				RefA.GetNum() ? *RefA.GetBoneName(0).ToString() : TEXT("-")));
		}
		Check(Optimized->GetPhysicsAsset() == Original->GetPhysicsAsset(), FString::Printf(TEXT("physics asset %s"), *GetPathNameSafe(Optimized->GetPhysicsAsset())));
		Check(Optimized->GetLODNum() >= 3, FString::Printf(TEXT("%d LODs (original %d)"), Optimized->GetLODNum(), Original->GetLODNum()));
		for (int32 Lod = 0; Lod < Optimized->GetLODNum(); ++Lod)
		{
			int32 Sections = 0;
			const int32 Tris = Triangles(Optimized, Lod, &Sections);
			UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] optimized LOD%d %d triangles, %d sections"), C.Asset, Lod, Tris, Sections);
			if (Lod == 0)
			{
				Check(Tris > 0 && Tris <= C.MaxTriangles * 102 / 100, FString::Printf(TEXT("LOD0 %d triangles <= budget %d"), Tris, C.MaxTriangles));
				Check(Sections == 1, FString::Printf(TEXT("LOD0 one section (%d)"), Sections));
			}
		}
		int32 OriginalSections = 0;
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] original LOD0 %d triangles, %d sections, %d LODs"), C.Asset,
			Triangles(Original, 0, &OriginalSections), OriginalSections, Original->GetLODNum());
		const FVector A = Optimized->GetBounds().BoxExtent;
		const FVector B = Original->GetBounds().BoxExtent;
		const bool bBounds = A.X > 1.f && FMath::Abs(A.X - B.X) <= B.X * 0.1f && FMath::Abs(A.Y - B.Y) <= B.Y * 0.1f && FMath::Abs(A.Z - B.Z) <= B.Z * 0.1f;
		Check(bBounds, FString::Printf(TEXT("bounds extent %s vs original %s"), *A.ToCompactString(), *B.ToCompactString()));
		TSet<const UTexture*> SeenB;
		int64 BytesOriginal = 0;
		for (const FSkeletalMaterial& Each : Original->GetMaterials())
		{
			BytesOriginal += TextureBytes(Each.MaterialInterface, SeenB);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] texture memory (all mips) optimized %.1f MB in %d textures, original %.1f MB in %d textures"),
			C.Asset, BytesOptimized / 1048576.0, SeenA.Num(), BytesOriginal / 1048576.0, SeenB.Num());
		return 0;
	}

	// ------------------------------------------------------------------------------------------------ scene

	FVector LaneStart(int32 Lane, float Up = 92.f)
	{
		const SmokeUtils::FClearArena& A = Runner->Arena;
		return A.Origin + A.Right * (Lane == 0 ? -LaneHalfCm : LaneHalfCm) + FVector(0.f, 0.f, Up);
	}

	TArray<AOperativeCharacter*> SortedMembers(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		return Members;
	}

	void ResetPair(UWorld* World)
	{
		int32 Lane = 0;
		for (AOperativeCharacter* Op : { OpA(), OpB() })
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

	/** Once: the arena, the camera, the squad in solo mode and safe. */
	int32 SetUpArena(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = SortedMembers(World);
		if (!Squad || Members.Num() < 3)
		{
			return -1;
		}
		if (!Squad->IsSoloMode())
		{
			Squad->ToggleSoloMode();
		}
		TArray<const AActor*> Ignore;
		for (const AOperativeCharacter* Each : Members)
		{
			Ignore.Add(Each);
		}
		Runner->Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
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
		for (AOperativeCharacter* Each : Members)
		{
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
			if (Each->HealthComponent)
			{
				Each->HealthComponent->SetMaxHealth(100000.f);
			}
		}
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
		return 30;
	}

	/** Max distance of any bone from the reference pose (component space): an animated body moves some bone, a T-pose none. */
	float MaxBoneFromRefPose(const USkeletalMeshComponent* Mesh)
	{
		const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!Asset)
		{
			return 0.f;
		}
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		TArray<FTransform> RefComponent;
		RefComponent.SetNum(Ref.GetNum());
		float Max = 0.f;
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			RefComponent[Index] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Index] : Ref.GetRefBonePose()[Index] * RefComponent[Parent];
			if (Parent == INDEX_NONE)
			{
				continue; // the root moves with root motion only
			}
			const FVector Posed = Mesh->GetBoneTransform(Index, FTransform::Identity).GetLocation();
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(Posed, RefComponent[Index].GetLocation())));
		}
		return Max;
	}

	void CheckPose(const TCHAR* State)
	{
		const ACharacter* A = Runner->Original.Get();
		const ACharacter* B = Runner->Optimized.Get();
		const float BoneA = A ? MaxBoneFromRefPose(A->GetMesh()) : 0.f;
		const float BoneB = B ? MaxBoneFromRefPose(B->GetMesh()) : 0.f;
		const UAnimInstance* AnimB = B ? B->GetMesh()->GetAnimInstance() : nullptr;
		Check(AnimB && BoneB > 5.f, FString::Printf(TEXT("%s: optimized body animated (a bone %.0f cm off the ref pose; original %.0f cm), anim %s"),
			State, BoneB, BoneA, *GetNameSafe(AnimB ? AnimB->GetClass() : nullptr)));
	}

	void AimCamera(bool bClose, float TargetZ, float YawDeg = 0.f)
	{
		const ACharacter* A = Runner->Original.Get();
		const ACharacter* B = Runner->Optimized.Get();
		ACameraActor* Camera = Runner->Camera.Get();
		if (!A || !B || !Camera)
		{
			return;
		}
		// Frame by the body size (a hound is small, a brute big); both bodies at the same distance (straight from the front).
		const float Radius = FMath::Max(A->GetMesh()->Bounds.SphereRadius, 60.f);
		const FVector Mid = (A->GetMesh()->Bounds.Origin + B->GetMesh()->Bounds.Origin) * 0.5f + FVector(0.f, 0.f, TargetZ);
		const FVector F = Runner->Arena.Forward.RotateAngleAxis(YawDeg, FVector::UpVector);
		const float Pitch = bClose ? 12.f : 55.f;
		const FVector Dir = F * FMath::Cos(FMath::DegreesToRadians(Pitch)) + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(Pitch));
		const FVector Eye = Mid + Dir * (bClose ? FMath::Max(450.f, Radius * 4.2f) : 1300.f);
		Camera->SetActorLocationAndRotation(Eye, (Mid - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(bClose ? 50.f : 45.f);
	}

	/** Pose check + (when rendering) the tactical and the close still of the state. */
	void AddShots(TArray<FStep>& S, FString State, float TargetZ = 0.f, float YawDeg = 0.f)
	{
		S.Add([State, YawDeg](UWorld*) { if (Runner->bSkip) { return 0; } CheckPose(*State); AimCamera(false, 0.f, YawDeg); return 3; });
		S.Add([State](UWorld*)
		{
			if (Runner->bSkip)
			{
				return 0;
			}
			if (Runner->bRender)
			{
				FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s_tactical.png"), Runner->Shots++, *State), false, false);
			}
			return 3;
		});
		S.Add([TargetZ, YawDeg](UWorld*) { if (Runner->bSkip) { return 0; } AimCamera(true, TargetZ, YawDeg); return 3; });
		S.Add([State](UWorld*)
		{
			if (Runner->bSkip)
			{
				return 0;
			}
			if (Runner->bRender)
			{
				FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s_close.png"), Runner->Shots++, *State), false, false);
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

	void StartCase(const FCase* C)
	{
		Runner->Case = C;
		Runner->Variant = 0;
		Runner->Shots = 0;
		Runner->Dir = Runner->RootDir / C->Asset;
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
	}

	/** Wraps a case: the asset check decides whether the case's steps run (skipped = jump over them). */
	void AddCase(TArray<FStep>& S, const FCase& C, TFunction<void(TArray<FStep>&)> Body)
	{
		TArray<FStep> Inner;
		Body(Inner);
		const int32 Count = Inner.Num();
		const FCase* Ptr = &C;
		S.Add([Ptr, Count](UWorld*)
		{
			StartCase(Ptr);
			const int32 Result = CheckAssets(*Ptr);
			if (Result == -3 || Result == -2)
			{
				Runner->Index += Count; // skip this case's steps
				Runner->Case = nullptr;
			}
			return 10;
		});
		S.Append(Inner);
	}

	// ------------------------------------------------------------------------------------------------ operatives

	int32 SetUpOperatives(UWorld* World)
	{
		const FCase& C = *Runner->Case;
		TArray<AOperativeCharacter*> Members = SortedMembers(World);
		AOperativeCharacter* Lead = Members[0];
		AOperativeCharacter* Second = Members[1];
		UClass* Class = LoadClass<AOperativeCharacter>(nullptr, C.Blueprint);
		const AOperativeCharacter* Defaults = Class ? Class->GetDefaultObject<AOperativeCharacter>() : nullptr;
		USkeletalMesh* Original = LoadObject<USkeletalMesh>(nullptr, C.Original);
		USkeletalMesh* Optimized = LoadObject<USkeletalMesh>(nullptr, *OptimizedPath(C));
		if (!Defaults || !Original || !Optimized)
		{
			return -2;
		}
		UClass* AnimClass = Defaults->GetMesh()->GetAnimClass();
		const FTransform MeshTransform = Defaults->GetMesh()->GetRelativeTransform();
		int32 Lane = 0;
		for (AOperativeCharacter* Op : { Lead, Second })
		{
			USkeletalMeshComponent* Mesh = Op->GetMesh();
			Mesh->EmptyOverrideMaterials();
			Mesh->SetSkeletalMeshAsset(Lane == 0 ? Original : Optimized);
			if (Lane == 0)
			{
				WearSourceLook(Mesh, C, 0);
			}
			Mesh->SetRelativeTransform(MeshTransform);
			if (AnimClass && Mesh->GetAnimClass() != AnimClass)
			{
				Mesh->SetAnimInstanceClass(AnimClass);
			}
			Mesh->SetForcedLOD(1);
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Prestream(Mesh);
			++Lane;
		}
		Check(Second->GetMesh()->GetNumMaterials() == 1, FString::Printf(TEXT("the swapped body renders %d material(s) (original %d)"),
			Second->GetMesh()->GetNumMaterials(), Lead->GetMesh()->GetNumMaterials()));
		Runner->Original = Lead;
		Runner->Optimized = Second;
		for (int32 Index = 2; Index < Members.Num(); ++Index)
		{
			Members[Index]->StopOperative();
			Members[Index]->TeleportTo(SmokeUtils::FreeSpot(World, Runner->Arena.Origin - Runner->Arena.Forward * 450.f, Members[Index]),
				Runner->Arena.Forward.Rotation(), false, true);
		}
		ResetPair(World);
		return 90;
	}

	void AddVaultShots(TArray<FStep>& S, bool bOriginal)
	{
		S.Add([bOriginal](UWorld*)
		{
			const AOperativeCharacter* Op = bOriginal ? OpA() : OpB();
			if (Op && Op->IsVaulting())
			{
				return 8;
			}
			if (Runner->Frame - Runner->MarkFrame > 60 * 8)
			{
				Check(false, FString::Printf(TEXT("vault: the %s body did not vault within 8 s"), bOriginal ? TEXT("original") : TEXT("optimized")));
				return 0;
			}
			return -1;
		});
		AddShots(S, bOriginal ? TEXT("vault_original_airborne") : TEXT("vault_optimized_airborne"));
	}

	void AddOperativeSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return SetUpOperatives(World); });
		AddShots(S, TEXT("idle"));
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { OpA(), OpB() })
			{
				Op->OrderMoveTo(LaneStart(Lane++) + Runner->Arena.Forward * 1000.f, false);
			}
			return 80;
		});
		AddShots(S, TEXT("walk"));
		S.Add([](UWorld* World) { ResetPair(World); return 60; });
		S.Add([](UWorld*) { OpA()->SetStance(EOperativeStance::Crouching); OpB()->SetStance(EOperativeStance::Crouching); return 90; });
		AddShots(S, TEXT("crouch"));
		S.Add([](UWorld*) { OpA()->SetStance(EOperativeStance::Prone); OpB()->SetStance(EOperativeStance::Prone); return 150; });
		AddShots(S, TEXT("prone"));
		S.Add([](UWorld* World) { ResetPair(World); return 120; });
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
			for (AOperativeCharacter* Op : { OpA(), OpB() })
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
			for (AOperativeCharacter* Op : { OpA(), OpB() })
			{
				Op->bForceHitForTesting = false;
			}
			ClearProps();
			ResetPair(World);
			return 120;
		});
		S.Add([](UWorld* World) { SpawnBarricade(World, Runner->Arena.Origin + Runner->Arena.Forward * 250.f); return 120; });
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { OpA(), OpB() })
			{
				const EOperativeOrderResult Order = Op->OrderMoveTo(LaneStart(Lane++) + Runner->Arena.Forward * 550.f, false);
				Check(Order == EOperativeOrderResult::Accepted, FString::Printf(TEXT("vault: move order over the barricade (%d)"), static_cast<int32>(Order)));
			}
			Runner->MarkFrame = Runner->Frame;
			return 0;
		});
		AddVaultShots(S, true);
		AddVaultShots(S, false);
		S.Add([](UWorld* World) { ClearProps(); ResetPair(World); return 120; });
		S.Add([](UWorld* World) { SpawnBarricade(World, Runner->Arena.Origin + Runner->Arena.Forward * 300.f); return 120; });
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AOperativeCharacter* Op : { OpA(), OpB() })
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
			Runner->MarkFrame = Runner->Frame;
			return 0;
		});
		S.Add([](UWorld*)
		{
			if (OpA()->bInCover && OpB()->bInCover)
			{
				return 90;
			}
			if (Runner->Frame - Runner->MarkFrame > 60 * 10)
			{
				Check(false, FString::Printf(TEXT("cover: in cover original %d optimized %d after 10 s"), OpA()->bInCover ? 1 : 0, OpB()->bInCover ? 1 : 0));
				return 0;
			}
			return -1;
		});
		AddShots(S, TEXT("cover"));
		S.Add([](UWorld* World)
		{
			for (AOperativeCharacter* Op : { OpA(), OpB() })
			{
				Op->bIgnoreCoverThreatForTesting = false;
			}
			ClearProps();
			ResetPair(World);
			return 30;
		});
		// the other look variants on the optimized body: an idle still each
		for (int32 Variant = 1; Variant < MaxVariants; ++Variant)
		{
			S.Add([Variant](UWorld*)
			{
				Runner->bSkip = !Runner->Instances.IsValidIndex(Variant);
				if (Runner->bSkip)
				{
					return 0;
				}
				UMaterialInterface* Look = LoadObject<UMaterialInterface>(nullptr, *Runner->Instances[Variant]);
				OpB()->GetMesh()->SetMaterial(0, Look);
				WearSourceLook(OpA()->GetMesh(), *Runner->Case, Variant);
				Prestream(OpA()->GetMesh());
				Prestream(OpB()->GetMesh());
				UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] variant %d = %s"), Runner->Case->Asset, Variant, *VariantLabel(Variant));
				Check(Look != nullptr, FString::Printf(TEXT("variant %d loads"), Variant));
				return 30;
			});
			AddShots(S, FString::Printf(TEXT("variant%d_idle"), Variant));
		}
		S.Add([](UWorld*) { Runner->bSkip = false; return 10; });
	}

	// ------------------------------------------------------------------------------------------------ enemies

	/** A deaf guard of the case Blueprint (it idles until scripted); Optimized: the 1-slot body + the variant's MI. */
	AEnemyCharacter* SpawnGuard(UWorld* World, int32 Lane, bool bOptimized)
	{
		const FCase& C = *Runner->Case;
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, C.Blueprint);
		const FTransform Transform(Runner->Arena.Forward.Rotation(), LaneStart(Lane, 120.f));
		AEnemyCharacter* Enemy = Class ? World->SpawnActorDeferred<AEnemyCharacter>(Class, Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Enemy)
		{
			return nullptr;
		}
		Enemy->VariantSeedOverride = 1000 + Lane;
		FEnemyPerceptionParams Deaf = Enemy->GetPerception();
		Deaf.SightRangeCm = 0.f;
		Deaf.ProximityCm = 0.f;
		Deaf.HearWalkCm = Deaf.HearRunCm = Deaf.HearCrouchWalkCm = Deaf.HearCrawlCm = Deaf.HearGunshotCm = Deaf.HearExplosionCm = 0.f;
		Enemy->PerceptionOverride = Deaf;
		Enemy->bOverridePerception = true;
		Enemy->FinishSpawning(Transform);
		if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
		{
			Encounter->MarkLevelEnemy(Enemy);
		}
		Enemy->GetHealthComponent()->SetMaxHealth(100000.f, true);
		USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetForcedLOD(1);
		// The look: BeginPlay picked a random variant body / material set; pin the case's body and the variant's materials.
		Mesh->EmptyOverrideMaterials();
		if (bOptimized)
		{
			Mesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, *OptimizedPath(C)));
			Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, *InstancePath(C, Runner->Variant)));
		}
		else
		{
			Mesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, C.Original));
			WearSourceLook(Mesh, C, Runner->Variant); // the same texture set as the optimized MI
		}
		Prestream(Mesh);
		Runner->Props.Add(Enemy);
		return Enemy;
	}

	int32 SetUpEnemies(UWorld* World)
	{
		ClearProps();
		TArray<AOperativeCharacter*> Members = SortedMembers(World);
		// The squad 16 m ahead, facing the guards (behind the cameras): an enemy no operative sees is drawn as a faded ghost
		// (tactical sight). The guards are deaf, the squad holds fire; no knockdowns from the scripted hits.
		int32 Slot = -1;
		for (AOperativeCharacter* Each : Members)
		{
			Each->StopOperative();
			Each->bTacticalCeaseFire = true;
			const FVector Spot = Runner->Arena.Origin + Runner->Arena.Forward * 1600.f + Runner->Arena.Right * (200.f * Slot++);
			Each->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Each), (-Runner->Arena.Forward).Rotation(), false, true);
		}
		if (IConsoleVariable* Knockdown = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
		{
			Knockdown->Set(0.f, ECVF_SetByConsole);
		}
		AEnemyCharacter* A = SpawnGuard(World, 0, false);
		AEnemyCharacter* B = SpawnGuard(World, 1, true);
		Check(A && B, TEXT("two guards of the Blueprint spawned"));
		if (!A || !B)
		{
			return -2;
		}
		Check(B->GetMesh()->GetNumMaterials() == 1, FString::Printf(TEXT("variant %d: the swapped body renders %d material(s) (original %d)"),
			Runner->Variant, B->GetMesh()->GetNumMaterials(), A->GetMesh()->GetNumMaterials()));
		Runner->Original = A;
		Runner->Optimized = B;
		return 90;
	}

	void AddEnemySteps(TArray<FStep>& S, int32 /*Unused*/)
	{
		S.Add([](UWorld* World) { Runner->Variant = 0; Runner->bSkip = false; return SetUpEnemies(World); });
		AddShots(S, TEXT("idle"));
		// move: a search walk 8 m ahead
		S.Add([](UWorld* World)
		{
			int32 Lane = 0;
			for (AEnemyCharacter* Enemy : { EnA(), EnB() })
			{
				const FVector Goal = SmokeUtils::ClearPoint(World, Enemy->GetActorLocation(), LaneStart(Lane++, 0.f) + Runner->Arena.Forward * 800.f);
				Enemy->StartPatrolSearchWith(Goal, FPatrolSearchSpec());
			}
			return 90;
		});
		AddShots(S, TEXT("move"));
		// hit: a light hit each
		S.Add([](UWorld*)
		{
			for (AEnemyCharacter* Enemy : { EnA(), EnB() })
			{
				FDamageSpec Spec;
				Spec.Amount = 50.f;
				Spec.AttackerSource = TEXT("OptimizedCharacterSmoke");
				Enemy->GetCharacterMovement()->StopMovementImmediately();
				Enemy->GetHealthComponent()->TakeDamage(Spec);
			}
			return 10;
		});
		AddShots(S, TEXT("hit"));
		// attack: an operative 1.8 m in front of each, AttackTarget
		S.Add([](UWorld* World)
		{
			TArray<AOperativeCharacter*> Members = SortedMembers(World);
			int32 Lane = 0;
			for (AEnemyCharacter* Enemy : { EnA(), EnB() })
			{
				AOperativeCharacter* Target = Members[Lane];
				const FVector At = Enemy->GetActorLocation() + Enemy->GetActorForwardVector() * 180.f;
				Target->TeleportTo(SmokeUtils::FreeSpot(World, At, Target), (-Enemy->GetActorForwardVector()).Rotation(), false, true);
				Enemy->AttackTarget(Target);
				++Lane;
			}
			return 15;
		});
		AddShots(S, TEXT("attack"), 0.f, 90.f); // from the side: the operative in front would hide the blow
		// death (the attack targets back to their place first: they would stand in the picture)
		S.Add([](UWorld* World)
		{
			int32 Slot = -1;
			for (AOperativeCharacter* Each : SortedMembers(World))
			{
				const FVector Spot = Runner->Arena.Origin + Runner->Arena.Forward * 1600.f + Runner->Arena.Right * (200.f * Slot++);
				Each->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Each), (-Runner->Arena.Forward).Rotation(), false, true);
			}
			for (AEnemyCharacter* Enemy : { EnA(), EnB() })
			{
				Enemy->GetHealthComponent()->ApplyDirectHealthLoss(1000000.f, TEXT("OptimizedCharacterSmoke"));
			}
			return 90;
		});
		AddShots(S, TEXT("death"));
		// the other look variants: an idle still each (steps for up to MaxVariants; missing ones are skipped)
		for (int32 Variant = 1; Variant < MaxVariants; ++Variant)
		{
			S.Add([Variant](UWorld* World)
			{
				Runner->bSkip = !Runner->Instances.IsValidIndex(Variant);
				if (Runner->bSkip)
				{
					return 0;
				}
				Runner->Variant = Variant;
				UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: [%s] variant %d = %s"), Runner->Case->Asset, Variant, *VariantLabel(Variant));
				return SetUpEnemies(World);
			});
			AddShots(S, FString::Printf(TEXT("variant%d_idle"), Variant));
		}
		S.Add([](UWorld*) { Runner->bSkip = false; ClearProps(); return 10; });
	}

	// ------------------------------------------------------------------------------------------------ run

	void AddSteps(TArray<FStep>& S, const FString& Only)
	{
		S.Add([](UWorld* World) { return SetUpArena(World); });
		for (const FCase& C : Cases)
		{
			if (!Only.IsEmpty() && !Only.Equals(C.Asset, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (C.Kind == EKind::Operative)
			{
				AddCase(S, C, [](TArray<FStep>& Inner) { AddOperativeSteps(Inner); });
			}
			else
			{
				const int32 Variants = C.Variants;
				AddCase(S, C, [Variants](TArray<FStep>& Inner) { AddEnemySteps(Inner, Variants); });
			}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->bRender = FApp::CanEverRender();
		Runner->RootDir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("OptimizedCharacter"));
		FPaths::CollapseRelativeDirectories(Runner->RootDir);
		FString Only;
		for (const FString& Arg : Args)
		{
			FParse::Value(*Arg, TEXT("Asset="), Only);
		}
		Runner->Catcher = MakeUnique<FWarningCatcher>();
		GLog->AddOutputDevice(Runner->Catcher.Get());
		AddSteps(Runner->Steps, Only);
		UE_LOG(LogCodexTactics, Display, TEXT("OptimizedCharacterSmoke: %d steps, asset filter '%s', rendering %d -> %s"), Runner->Steps.Num(), *Only,
			Runner->bRender ? 1 : 0, *Runner->RootDir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.OptimizedCharacterSmoke"),
		TEXT("Dev: every imported one-slot character body swapped at runtime next to its original (asserts; stills when rendering), then exits. "
			"Optional Asset=<Name> runs one case."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
