// Dev-only check of a PACKAGED (cooked) build (tester build 2026-10-24: the first one showed capsules instead of the operatives -
// the character Blueprints named only by C++ class defaults were not cooked, and the -nullrhi smokes never looked at the bodies).
// Run the packaged Development game, rendered offscreen so the stills show what a tester sees:
//   Saved/Packages/Development/Windows/CodexTactics.exe /Game/Maps/L_Demo01_A_FrozenLake -RenderOffscreen -ResX=1280 -ResY=720
//     -nosound -unattended -ExecCmds="CodexTactics.PackagedCheck"
// Checks (log "Smoke ok / FAIL", "Smoke RESULT: PASS / FAIL"):
//  1. every operative wears its real body (not UsesPlaceholderBody), every placed enemy has its skeletal mesh;
//  2. one enemy of EVERY archetype the game mode knows spawns with its skeletal mesh (the wave / horde classes);
//  3. every /Game soft path held by a CodexTactics class default loads (BP_Operative, enemy Blueprints, balance, dialogues, ...),
//     except the optional art that does not exist yet;
//  4. the runtime JSON the game reads by path is staged (Data/AI, Environment, Weapons, LevelJson, Tutorial, Campaign, ...).
// Stills (rendered runs only): <Saved>/Screenshots/Packaged/<map>_squad.png, <map>_enemy_<archetype>.png.
// Also runs in the editor (-game) where it must pass the same way. UE-only, no Godot reference.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace PackagedCheck
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		TArray<EEnemyArchetype> ToShoot;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Spawned;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<UWorld> World;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PackagedCheck"));
		return false;
	}

	FString ShotPath(const UWorld* World, const FString& Name)
	{
		return FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Packaged") / FString::Printf(TEXT("%s_%s.png"),
			*FPackageName::GetShortName(World->GetOutermost()->GetName()), *Name);
	}

	void Shoot(const UWorld* World, const FString& Name)
	{
		if (FApp::CanEverRender())
		{
			const FString Path = ShotPath(World, Name);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("PackagedCheck still: %s"), *Path);
		}
	}

	// --- soft paths of the C++ class defaults (the same set the cook adds: CodexTacticsEditor CodexCookRules) ---
	void CollectFromValue(const FProperty* Property, const void* Value, TSet<FSoftObjectPath>& Out);

	void CollectFromStruct(const UStruct* Struct, const void* Container, TSet<FSoftObjectPath>& Out)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			{
				CollectFromValue(*It, It->ContainerPtrToValuePtr<void>(Container, Index), Out);
			}
		}
	}

	void CollectFromValue(const FProperty* Property, const void* Value, TSet<FSoftObjectPath>& Out)
	{
		if (CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPath Path = static_cast<const FSoftObjectPtr*>(Value)->ToSoftObjectPath();
			if (!Path.IsNull())
			{
				Out.Add(Path);
			}
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get() || StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
			{
				if (!static_cast<const FSoftObjectPath*>(Value)->IsNull())
				{
					Out.Add(*static_cast<const FSoftObjectPath*>(Value));
				}
			}
			else
			{
				CollectFromStruct(StructProperty->Struct, Value, Out);
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				CollectFromValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Out);
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, Value);
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				CollectFromValue(MapProperty->KeyProp, Helper.GetKeyPtr(It), Out);
				CollectFromValue(MapProperty->ValueProp, Helper.GetValuePtr(It), Out);
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProperty, Value);
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				CollectFromValue(SetProperty->ElementProp, Helper.GetElementPtr(It), Out);
			}
		}
	}

	void CheckClassDefaultSoftPaths(FState& State)
	{
		// Art that does not exist yet (the C++ falls back by design): CodexCookRulesTest lists the same five.
		static const TSet<FString> Optional = { TEXT("/Game/VFX/Heat/NS_Campfire"), TEXT("/Game/VFX/Heat/NS_FirePuddle"),
			TEXT("/Game/VFX/Heat/NS_PortableStove"), TEXT("/Game/VFX/Heat/NS_SignalFlare"), TEXT("/Game/Environment/Weather/BP_WeatherPresenter") };
		static const FName RuntimePackage(TEXT("/Script/CodexTactics"));
		TSet<FSoftObjectPath> Paths;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->HasAnyClassFlags(CLASS_Native) || It->GetOutermost()->GetFName() != RuntimePackage)
			{
				continue;
			}
			if (UObject* Defaults = It->GetDefaultObject(true))
			{
				CollectFromStruct(*It, Defaults, Paths);
				TArray<UObject*> Subobjects;
				Defaults->GetDefaultSubobjects(Subobjects);
				for (const UObject* Subobject : Subobjects)
				{
					CollectFromStruct(Subobject->GetClass(), Subobject, Paths);
				}
			}
		}
		int32 Loaded = 0;
		int32 Missing = 0;
		for (const FSoftObjectPath& Path : Paths)
		{
			const FString Package = Path.GetLongPackageName();
			if (!Package.StartsWith(TEXT("/Game/")) || Optional.Contains(Package))
			{
				continue;
			}
			if (Path.TryLoad())
			{
				++Loaded;
			}
			else
			{
				++Missing;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: a C++ default names an asset the build cannot load: %s"), *Path.ToString());
			}
		}
		State.Failures += Missing;
		Check(State, Missing == 0, FString::Printf(TEXT("every asset named by the C++ class defaults loads (%d loaded, %d missing)"), Loaded, Missing));
	}

	void CheckStagedJson(FState& State)
	{
		const FString Data = FPaths::ProjectContentDir() / TEXT("Data");
		for (const TCHAR* File : { TEXT("AI/ai_tuning.json"), TEXT("AI/enemy_perception.json"), TEXT("AI/horde.json"), TEXT("AI/squad_roe.json"),
				 TEXT("Environment/environment.json"), TEXT("Weapons/weapons_tuning.json"), TEXT("Campaign/campaign.json"),
				 TEXT("Tutorial/demo_01_steps.json"), TEXT("LevelJson/L_Demo01_A_FrozenLake.json"), TEXT("LevelJson/L_Demo01_B_FrozenFactory.json") })
		{
			Check(State, IFileManager::Get().FileExists(*(Data / File)), FString::Printf(TEXT("runtime JSON staged: Data/%s"), File));
		}
	}

	void CheckBodies(FState& State, UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			const USkeletalMesh* Mesh = Member && Member->GetMesh() ? Member->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			Check(State, Member && !Member->UsesPlaceholderBody() && Mesh,
				FString::Printf(TEXT("operative %s (%s) wears %s"), Member ? *Member->GetName() : TEXT("?"),
					Member ? *Member->GetClass()->GetName() : TEXT("?"), Mesh ? *Mesh->GetPathName() : TEXT("NO BODY (placeholder)")));
		}
		int32 Enemies = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			const USkeletalMesh* Mesh = It->GetMesh() ? It->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			Check(State, Mesh != nullptr, FString::Printf(TEXT("placed enemy %s (%s) wears %s"), *It->GetName(), *It->GetClass()->GetName(),
				Mesh ? *Mesh->GetPathName() : TEXT("NO BODY (placeholder cylinder)")));
			++Enemies;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("PackagedCheck: %d placed enemies"), Enemies);
	}

	/** A camera in front of Target (Distance, longer for big bodies), a little above, looking at its bounds' centre. */
	void Frame(FState& State, UWorld* World, const AActor* Target, float Distance)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !Target)
		{
			return;
		}
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
		}
		// Big bodies (the Brute) need a longer shot: at least 3x the body's half size.
		FVector Centre, Extent;
		Target->GetActorBounds(/*bOnlyColliding*/ false, Centre, Extent);
		const float Shot = FMath::Max(Distance, Extent.Size() * 3.f);
		const FVector Look = Centre;
		const FVector Eye = Look + Target->GetActorForwardVector() * Shot + FVector(0.f, 0.f, Shot * 0.3f);
		State.Camera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
		PC->SetViewTarget(State.Camera.Get());
	}

	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0: // the level is up, the squad spawned, the start fade / camera settled
		{
			if (!Squad || Squad->GetMembers().Num() < 3 || State.StageTime < 6.f)
			{
				return true;
			}
			CheckStagedJson(State);
			CheckClassDefaultSoftPaths(State);
			CheckBodies(State, World);
			Shoot(World, TEXT("squad"));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1: // the squad close up (the leader), then one enemy of every archetype
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Frame(State, World, Squad->GetLeader(), 380.f);
			ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			if (GameMode && Waves && Squad->GetLeader())
			{
				int32 Index = 0;
				for (const TPair<EEnemyArchetype, TSoftClassPtr<AEnemyCharacter>>& Entry : GameMode->EnemyClasses)
				{
					// 40 m off the squad, spread out, so nothing fights during the stills.
					const FVector At = Squad->GetLeader()->GetActorLocation() + FVector(4000.f, -1200.f + 600.f * Index++, 0.f);
					AEnemyCharacter* Enemy = Waves->SpawnEnemy(Entry.Key, At);
					const USkeletalMesh* Mesh = Enemy && Enemy->GetMesh() ? Enemy->GetMesh()->GetSkeletalMeshAsset() : nullptr;
					Check(State, Mesh != nullptr, FString::Printf(TEXT("wave enemy %s spawns as %s with %s"), *UEnum::GetValueAsString(Entry.Key),
						Enemy ? *Enemy->GetClass()->GetName() : TEXT("nothing"), Mesh ? *Mesh->GetPathName() : TEXT("NO BODY (placeholder cylinder)")));
					if (Enemy)
					{
						State.Spawned.Add(Enemy);
						State.ToShoot.Add(Entry.Key);
					}
				}
			}
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			Shoot(World, TEXT("leader"));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3: // one still per spawned archetype, 1 s each (framed every step: it may walk)
		{
			const int32 Index = FMath::FloorToInt(State.StageTime);
			if (Index >= State.Spawned.Num())
			{
				return Finish(State, true);
			}
			Frame(State, World, State.Spawned[Index].Get(), 420.f);
			const float Phase = State.StageTime - Index;
			if (Phase >= 0.6f && Phase < 0.6f + StepSeconds)
			{
				Shoot(World, FString::Printf(TEXT("enemy_%s"), *UEnum::GetValueAsString(State.ToShoot[Index]).RightChop(17)));
			}
			return true;
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PackagedCheck"),
		TEXT("Dev check (2026-10-24) for a packaged build: real bodies on the squad and every enemy archetype (no placeholder capsules), every "
			"asset named by C++ class defaults loads, the runtime JSON is staged; rendered runs also write stills to Saved/Screenshots/Packaged."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
