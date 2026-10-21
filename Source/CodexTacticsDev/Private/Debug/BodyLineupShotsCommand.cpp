// Dev-only in-game stills of the Blueprints on their single-slot optimized bodies (user 2026-10-21; UE-only, no Godot reference).
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//       -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.BodyLineupShots"
//   -> Saved/Screenshots/<Platform>/BodyLineup/NN_<group>.png
// The squad (its own Blueprints: BP_Operative x2 with their squad outfits, the Medic-Sapper's BP_Operative_FemaleSoldier) and
// lineups of deaf enemies spawned from their Blueprints with pinned variant seeds chosen so that every body / colour set of
// FEnemyLookVariants::Bodies shows: Military Zombie (3 bodies x 3 sets), Hound (5 sets), Frostbitten, Brute, Cutter (4 sets each). Asserts that each
// enemy wears the seed's body and set; logs every pick. Headless it only asserts.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyAnimRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace BodyLineupShots
{
	using FStep = TFunction<int32(UWorld*)>; // >= 0: frames to wait before the next step, -1: retry next frame, -2: abort

	struct FGroup
	{
		const TCHAR* Name;
		const TCHAR* Class;
		/** How many enemies: enough to show every body x set pair (capped). */
		int32 Count;
		float SpacingCm;
	};

	const FGroup Groups[] = {
		{ TEXT("zombies"), TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C"), 9, 130.f },
		{ TEXT("hounds"), TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C"), 5, 140.f },
		{ TEXT("frostbitten"), TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C"), 4, 150.f },
		{ TEXT("brutes"), TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C"), 4, 430.f },
		{ TEXT("cutters"), TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter.BP_Enemy_Cutter_C"), 4, 170.f },
	};

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
		SmokeUtils::FClearArena Arena;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<AActor>> Props;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("BodyLineupShots %s: %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void FinishShaders()
	{
#if WITH_EDITOR
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
#endif
	}

	void Prestream(USkeletalMeshComponent* Mesh)
	{
		if (Mesh)
		{
			Mesh->SetTextureForceResidentFlag(true);
			Mesh->PrestreamTextures(60.f, true);
			Mesh->SetForcedLOD(1);
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	void Finish(bool bComplete)
	{
		Check(bComplete, TEXT("all steps ran"));
		UE_LOG(LogCodexTactics, Display, TEXT("BodyLineupShots: %d screenshots in %s"), Runner->Shots, *Runner->Dir);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("BodyLineupShots"));
	}

	void Tick()
	{
		FRunner& R = *Runner;
		if (++R.Frame > 60 * 300)
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

	TArray<AOperativeCharacter*> SortedMembers(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		return Members;
	}

	/** A point of the lineup row: Index of Count, across the arena, Depth ahead of the camera's target line. */
	FVector RowPoint(int32 Index, int32 Count, float SpacingCm, float Up)
	{
		const SmokeUtils::FClearArena& A = Runner->Arena;
		return A.Origin + A.Right * ((Index - (Count - 1) * 0.5f) * SpacingCm) + FVector(0.f, 0.f, Up);
	}

	/** Camera in front of the row (looking back along -Forward), framing Count bodies. */
	void FrameRow(int32 Count, float SpacingCm, float TargetZ, float BodyRadius = 0.f)
	{
		ACameraActor* Camera = Runner->Camera.Get();
		if (!Camera)
		{
			return;
		}
		const SmokeUtils::FClearArena& A = Runner->Arena;
		const FVector Mid = A.Origin + FVector(0.f, 0.f, TargetZ);
		// wide enough for the row and far enough for tall bodies (a brute)
		const float Width = FMath::Max3(300.f, Count * SpacingCm, BodyRadius * 3.f);
		const FVector Eye = Mid + A.Forward * (Width * 1.05f + 150.f + BodyRadius * 2.f) + FVector(0.f, 0.f, Width * 0.18f + 40.f);
		Camera->SetActorLocationAndRotation(Eye, (Mid - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(55.f);
	}

	void Shot(const TCHAR* Name)
	{
		FinishShaders();
		if (Runner->bRender)
		{
			FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s.png"), Runner->Shots++, Name), false, false);
		}
	}

	void ClearProps()
	{
		for (const TWeakObjectPtr<AActor>& Each : Runner->Props)
		{
			if (AActor* Actor = Each.Get())
			{
				Actor->Destroy();
			}
		}
		Runner->Props.Reset();
	}

	int32 SetUp(UWorld* World)
	{
		TArray<AOperativeCharacter*> Members = SortedMembers(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
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
			FBox2D(FVector2D(-300.f, -750.f), FVector2D(1900.f, 750.f)), Ignore);
		Check(Runner->Arena.bFound, FString::Printf(TEXT("an empty arena at %s"), *Runner->Arena.Origin.ToCompactString()));
		if (!Runner->Arena.bFound)
		{
			return -2;
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			SmokeUtils::ParkEnemyFarAway(*It);
		}
		Runner->Camera = World->SpawnActor<ACameraActor>(Runner->Arena.Origin + FVector(0.f, 0.f, 400.f), FRotator(-30.f, 0.f, 0.f));
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
			}
		}
		// The squad in a row facing the camera, in its own Blueprints' looks.
		int32 Slot = 0;
		for (AOperativeCharacter* Each : Members)
		{
			Each->StopOperative();
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
			if (Each->HealthComponent)
			{
				Each->HealthComponent->SetMaxHealth(100000.f);
			}
			Each->TeleportTo(RowPoint(Slot++, Members.Num(), 160.f, 95.f), Runner->Arena.Forward.Rotation(), false, true);
			USkeletalMeshComponent* Mesh = Each->GetMesh();
			Prestream(Mesh);
			const USkeletalMesh* Body = Mesh->GetSkeletalMeshAsset();
			UE_LOG(LogCodexTactics, Display, TEXT("BodyLineupShots: %s (%s, squad %d) body %s, slot 0 %s"), *Each->DisplayName.ToString(),
				*Each->GetClass()->GetName(), Each->SquadIndex, *GetNameSafe(Body), *GetNameSafe(Mesh->GetMaterial(0)));
			Check(Body && Body->GetName().EndsWith(TEXT("_1slot")) && Mesh->GetNumMaterials() == 1,
				FString::Printf(TEXT("%s wears a single-slot body (%s)"), *Each->DisplayName.ToString(), *GetNameSafe(Body)));
		}
		// The Female Soldier's own Blueprint next to them (on this map's roster the Medic-Sapper wears BP_Operative).
		if (UClass* Female = LoadClass<AOperativeCharacter>(nullptr, TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C")))
		{
			const FTransform At(Runner->Arena.Forward.Rotation(), RowPoint(Members.Num(), Members.Num() + 1, 160.f, 95.f));
			if (AOperativeCharacter* Her = World->SpawnActor<AOperativeCharacter>(Female, At))
			{
				Runner->Props.Add(Her);
				USkeletalMeshComponent* Mesh = Her->GetMesh();
				Prestream(Mesh);
				const USkeletalMesh* Body = Mesh->GetSkeletalMeshAsset();
				UE_LOG(LogCodexTactics, Display, TEXT("BodyLineupShots: BP_Operative_FemaleSoldier body %s, slot 0 %s"), *GetNameSafe(Body), *GetNameSafe(Mesh->GetMaterial(0)));
				Check(Body && Body->GetName() == TEXT("SK_Female_Soldier_1slot") && Mesh->GetNumMaterials() == 1,
					FString::Printf(TEXT("BP_Operative_FemaleSoldier wears its single-slot body (%s, %s)"), *GetNameSafe(Body), *GetNameSafe(Mesh->GetMaterial(0))));
			}
		}
		// the squad row re-spaced for four
		Slot = 0;
		for (AOperativeCharacter* Each : Members)
		{
			Each->TeleportTo(RowPoint(Slot++, Members.Num() + 1, 160.f, 95.f), Runner->Arena.Forward.Rotation(), false, true);
		}
		FrameRow(Members.Num() + 1, 160.f, 100.f);
		return 120;
	}

	/** The squad behind the camera, facing the lineup (an enemy no operative sees is drawn as a faded ghost). */
	void SquadWatches(UWorld* World)
	{
		int32 Slot = -1;
		for (AOperativeCharacter* Each : SortedMembers(World))
		{
			const FVector Spot = Runner->Arena.Origin + Runner->Arena.Forward * 1700.f + Runner->Arena.Right * (200.f * Slot++) + FVector(0.f, 0.f, 95.f);
			Each->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Each), (-Runner->Arena.Forward).Rotation(), false, true);
		}
	}

	/** Seeds whose (body, set) picks cover the Blueprint's Bodies (all pairs first), Count of them. */
	TArray<int32> SeedsCoveringBodies(const FEnemyLookVariants& Look, int32 Count)
	{
		TArray<int32> SetsPerBody;
		for (const FEnemyLookBody& Each : Look.Bodies)
		{
			SetsPerBody.Add(Each.Sets.Num());
		}
		TArray<int32> Seeds;
		TSet<int32> Pairs;
		for (int32 Seed = 0; Seed < 5000 && Seeds.Num() < Count; ++Seed)
		{
			int32 Body = -1;
			int32 Set = -1;
			EnemyAnimRules::PickLookBodyAndSet(static_cast<uint32>(Seed), SetsPerBody, Body, Set);
			if (!Pairs.Contains(Body * 100 + Set))
			{
				Pairs.Add(Body * 100 + Set);
				Seeds.Add(Seed);
			}
		}
		return Seeds;
	}

	int32 SpawnGroup(UWorld* World, const FGroup& G)
	{
		ClearProps();
		SquadWatches(World);
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, G.Class);
		Check(Class != nullptr, FString::Printf(TEXT("%s loads"), G.Class));
		if (!Class)
		{
			return 10;
		}
		const AEnemyCharacter* Cdo = Class->GetDefaultObject<AEnemyCharacter>();
		const TArray<int32> Seeds = SeedsCoveringBodies(Cdo->LookVariants, G.Count);
		for (int32 Index = 0; Index < Seeds.Num(); ++Index)
		{
			// facing the camera (it stands on the +Forward side of the row)
			const FTransform Facing(Runner->Arena.Forward.Rotation(), RowPoint(Index, Seeds.Num(), G.SpacingCm, 120.f));
			AEnemyCharacter* Enemy = World->SpawnActorDeferred<AEnemyCharacter>(Class, Facing, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Enemy)
			{
				continue;
			}
			Enemy->VariantSeedOverride = Seeds[Index];
			FEnemyPerceptionParams Deaf = Enemy->GetPerception();
			Deaf.SightRangeCm = 0.f;
			Deaf.ProximityCm = 0.f;
			Deaf.HearWalkCm = Deaf.HearRunCm = Deaf.HearCrouchWalkCm = Deaf.HearCrawlCm = Deaf.HearGunshotCm = Deaf.HearExplosionCm = 0.f;
			Enemy->PerceptionOverride = Deaf;
			Enemy->bOverridePerception = true;
			Enemy->FinishSpawning(Facing);
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Enemy);
			}
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f, true);
			USkeletalMeshComponent* Mesh = Enemy->GetMesh();
			Prestream(Mesh);
			Runner->Props.Add(Enemy);
			const FEnemyLookVariants& Look = Enemy->LookVariants;
			const int32 BodyIndex = Enemy->GetLookMeshIndex();
			const int32 SetIndex = Enemy->GetLookMaterialSetIndex();
			const FEnemyLookBody* Picked = Look.Bodies.IsValidIndex(BodyIndex) ? &Look.Bodies[BodyIndex] : nullptr;
			const bool bOk = Picked && Mesh->GetSkeletalMeshAsset() == Picked->Mesh && Picked->Sets.IsValidIndex(SetIndex)
				&& Mesh->GetMaterial(0) == Picked->Sets[SetIndex] && Mesh->GetNumMaterials() == 1;
			Check(bOk, FString::Printf(TEXT("%s #%d seed %d: body %d %s, set %d %s"), G.Name, Index, Seeds[Index], BodyIndex,
				*GetNameSafe(Mesh->GetSkeletalMeshAsset()), SetIndex, *GetNameSafe(Mesh->GetMaterial(0))));
		}
		Check(Seeds.Num() == G.Count, FString::Printf(TEXT("%s: %d distinct looks spawned (%d expected)"), G.Name, Seeds.Num(), G.Count));
		float Radius = 0.f;
		for (const TWeakObjectPtr<AActor>& Each : Runner->Props)
		{
			if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Each.Get()))
			{
				Radius = FMath::Max(Radius, Enemy->GetMesh()->Bounds.SphereRadius);
			}
		}
		FrameRow(Seeds.Num(), G.SpacingCm, FMath::Max(90.f, Radius * 0.9f), Radius);
		return 150;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->bRender = FApp::CanEverRender();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("BodyLineup"));
		TArray<FStep>& S = Runner->Steps;
		S.Add([](UWorld* W) { return SetUp(W); });
		S.Add([](UWorld*) { Shot(TEXT("squad")); return 10; });
		for (const FGroup& G : Groups)
		{
			S.Add([&G](UWorld* W) { return SpawnGroup(W, G); });
			S.Add([&G](UWorld*) { Shot(G.Name); return 10; });
		}
		S.Add([](UWorld*) { ClearProps(); return 5; });
		UE_LOG(LogCodexTactics, Display, TEXT("BodyLineupShots: rendering %d -> %s"), Runner->bRender ? 1 : 0, *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.BodyLineupShots"),
		TEXT("Dev: the squad and lineups of Military Zombies / Hounds / Frostbitten on their single-slot bodies, every body x colour set "
			"(asserts the seed's pick; stills when rendering), then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
