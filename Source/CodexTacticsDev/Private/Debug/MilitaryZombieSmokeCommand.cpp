// Dev-only check of the Military Zombie (game designer 2026-10-20; Scripts/Editor/create_military_zombie.py) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.MilitaryZombieSmoke                    (headless checks)
//   rendered: UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.MilitaryZombieSmoke shot"  -> Saved/Screenshots/military_zombie_<phase>.png
// BP_Enemy_MilitaryZombie (a child of BP_Enemy_Frostbitten; ABP_Enemy_MilitaryZombie = a child of ABP_Enemy_Frostbitten with the
// pack's own clips):
//   placed -> archetype Frostbitten, its HP, nameplate "Military Zombie"; look variants differ between instances and repeat for one seed;
//   a guard (placed, senses off) idles (seeded idle loop, not the reference pose, feet on the ground), breaks the idle with another
//   idle clip and with the devour activity; walks a search (a walk variant); BreakPatrol -> the rage roar standing still facing the
//   alarm, then runs (a run variant) to the leader and attacks (an attack clip, the leader loses HP); hit clip, Manny knockdown,
//   death from the ground; a wave spawn (UWaveSubsystem::SpawnEnemy with the class mapping pointed at the zombie for the run) plays
//   the transformation entrance standing, then hunts; it dies standing (anim_Dying held). No "missing bones" / ensure lines.
//   Codex.Knockdown.Chance is forced to 1 for the run.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationRuntime.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyAnimRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"
#include "UI/OverheadLabel.h"
#include "UnrealClient.h"

namespace MilitaryZombieSmoke
{
	constexpr float StepSeconds = 0.1f;
	const TCHAR* const ZombieClassPath = TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C");
	const TCHAR* const FrostAbpClassPath = TEXT("/Game/Characters/Enemies/Frostbitten/ABP_Enemy_Frostbitten.ABP_Enemy_Frostbitten_C");

	/** Counts engine log lines that mean a broken body: missing bones, an incompatible skeleton, an ensure. */
	class FBodyErrorListener : public FOutputDevice
	{
	public:
		int32 Count = 0;
		FString First;

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const FString Line(Text);
			if (Line.Contains(TEXT("missing bones")) || Line.Contains(TEXT("Ensure condition failed")) || Line.Contains(TEXT("is not compatible with")))
			{
				First = First.IsEmpty() ? Line.Left(240) : First;
				++Count;
			}
		}
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		bool bShotTaken = false;
		bool bFlag = false;
		float SavedChance = -1.f;
		float LeaderHpBefore = 0.f;
		float MaxSpeed = 0.f;
		float MaxPoseDeviation = 0.f;
		float MinToe = 1.0e6f;
		float MaxToe = -1.0e6f;
		float Mark = 0.f;
		/** What played when the lowest toe was sampled (diagnostics). */
		FString MinToeNote;
		TSet<FString> SeenClips;
		TWeakObjectPtr<UClass> ZombieClass;
		TWeakObjectPtr<AEnemyCharacter> Guard;
		TWeakObjectPtr<AEnemyCharacter> Spawned;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<ACameraActor> Camera;
		TSharedPtr<FBodyErrorListener> Listener;
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
		if (State.Listener.IsValid())
		{
			GLog->RemoveOutputDevice(State.Listener.Get());
			Check(State, State.Listener->Count == 0, FString::Printf(TEXT("no missing-bones / skeleton / ensure lines during the run (%d%s%s)"),
				State.Listener->Count, State.Listener->First.IsEmpty() ? TEXT("") : TEXT(": "), *State.Listener->First));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MilitaryZombieSmoke"));
		return false;
	}

	/** Shot mode: a camera 4.6 m off the enemy's front-left at chest height follows it every step (no motion blur, no HUD). */
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
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetHUD())
			{
				PC->GetHUD()->bShowHUD = false;
			}
		}
		// Centred on the pelvis (a dying zombie falls ~1.6 m forward of its capsule).
		const FVector Pelvis = Enemy->GetMesh() ? Enemy->GetMesh()->GetSocketLocation(TEXT("pelvis")) : Enemy->GetActorLocation();
		const FVector Centre(Pelvis.X, Pelvis.Y, Enemy->GetActorLocation().Z - 10.f);
		const FVector Eye = Centre + Enemy->GetActorForwardVector() * 380.f - Enemy->GetActorRightVector() * 250.f + FVector(0.f, 0.f, 60.f);
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}

	void Shot(FState& State, UWorld* World, const AEnemyCharacter* Enemy, const TCHAR* Name)
	{
		if (!State.bShots || !Enemy)
		{
			return;
		}
		AimCamera(State, World, Enemy);
		const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("military_zombie_%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot %s"), *FPaths::ConvertRelativePathToFull(File));
	}

	UEnemyAnimInstance* AnimOf(const AEnemyCharacter* Enemy)
	{
		return Enemy && Enemy->GetMesh() ? Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr;
	}

	/** Names of the clips the running (not stopped) montages play, e.g. "anim_rage(DefaultSlot 1.00)". */
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
		return Clips.ContainsByPredicate([&Names](const UAnimSequenceBase* Clip) { return Clip && Names.Contains(Clip->GetName()); });
	}

	bool SawClipFrom(const FState& State, const TArray<TObjectPtr<UAnimSequenceBase>>& Clips)
	{
		return Clips.ContainsByPredicate([&State](const UAnimSequenceBase* Clip) { return Clip && State.SeenClips.Contains(Clip->GetName()); });
	}

	/** Largest distance (cm, component space) of the hands / head / a foot from the mesh's reference pose: ~0 = T-pose. */
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
			if (Index != INDEX_NONE)
			{
				const FVector Ref = FAnimationRuntime::GetComponentSpaceTransformRefPose(Mesh->GetRefSkeleton(), Index).GetLocation();
				Deviation = FMath::Max(Deviation, FVector::Dist(Ref, Body->GetBoneLocation(Bone, EBoneSpaces::ComponentSpace)));
			}
		}
		return Deviation;
	}

	float ToeAboveFloor(const AEnemyCharacter* Enemy)
	{
		const USkeletalMeshComponent* Body = Enemy->GetMesh();
		const float Bottom = Enemy->GetActorLocation().Z - Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		return FMath::Min(Body->GetSocketLocation(TEXT("ball_l")).Z, Body->GetSocketLocation(TEXT("ball_r")).Z) - Bottom;
	}

	/** Health of the whole squad (the zombie bites the operative closest to it, not always the leader). */
	float SquadHealth(UWorld* World)
	{
		float Total = 0.f;
		for (const AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			Total += Member && Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f;
		}
		return Total;
	}

	void Sample(FState& State, const AEnemyCharacter* Enemy)
	{
		State.MaxPoseDeviation = FMath::Max(State.MaxPoseDeviation, PoseDeviation(Enemy));
		const float Toe = ToeAboveFloor(Enemy);
		if (Toe < State.MinToe)
		{
			const UEnemyAnimInstance* Anim = AnimOf(Enemy);
			State.MinToeNote = FString::Printf(TEXT("%.1f cm at %.1f s: speed %.0f, %s%s [%s]"), Toe, State.StageTime, Enemy->GetVelocity().Size2D(),
				Anim && Anim->bIsRunning ? TEXT("run ") : TEXT(""), Anim && Anim->bIsMoving ? TEXT("moving") : TEXT("standing"), *ActiveClips(Enemy));
		}
		State.MinToe = FMath::Min(State.MinToe, Toe);
		State.MaxToe = FMath::Max(State.MaxToe, Toe);
		ActiveClips(Enemy, &State.SeenClips);
	}

	void ResetSamples(FState& State)
	{
		State.MaxPoseDeviation = 0.f;
		State.MinToe = 1.0e6f;
		State.MaxToe = -1.0e6f;
		State.MaxSpeed = 0.f;
		State.SeenClips.Reset();
	}

	/** Spawned with the Frostbitten spawner scope (like a wave enemy, no entrance call); Placed = also marked a level enemy (guard). */
	AEnemyCharacter* SpawnZombie(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation, bool bGuard)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyCharacter* Enemy = nullptr;
		{
			const AEnemyCharacter::FScopedSpawnArchetype Scope(EEnemyArchetype::Frostbitten);
			Enemy = World->SpawnActor<AEnemyCharacter>(Class, Location, Rotation, Params);
		}
		if (!Enemy)
		{
			return nullptr;
		}
		Enemy->InitializeArchetype(EEnemyArchetype::Frostbitten);
		Enemy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (bGuard)
		{
			// Senses off: it guards and idles until the smoke alerts it.
			FEnemyPerceptionParams Deaf = Enemy->GetPerception();
			Deaf.SightRangeCm = 0.f;
			Deaf.ProximityCm = 0.f;
			Deaf.HearWalkCm = Deaf.HearRunCm = Deaf.HearCrouchWalkCm = Deaf.HearCrawlCm = Deaf.HearGunshotCm = Deaf.HearExplosionCm = 0.f;
			Enemy->PerceptionOverride = Deaf;
			Enemy->bOverridePerception = true;
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Enemy);
			}
		}
		return Enemy;
	}

	/** Placed like a level actor (no spawner scope): the archetype safety net, the look variants. */
	AEnemyCharacter* PlaceZombie(UWorld* World, UClass* Class, const FVector& Location, int32 SeedOverride)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		AEnemyCharacter* Placed = World->SpawnActorDeferred<AEnemyCharacter>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Placed)
		{
			Placed->VariantSeedOverride = SeedOverride;
			Placed->FinishSpawning(Transform);
			Placed->SetTurnBasedHeld(true);
			Placed->SetActorTickEnabled(false);
		}
		return Placed;
	}

	/** Setup + the placed checks (archetype, name, ABP route, look variants); spawns the guard. */
	bool StageSetup(UWorld* World, FState& State)
	{
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, ZombieClassPath);
		State.ZombieClass = Class;
		Check(State, Class != nullptr, TEXT("BP_Enemy_MilitaryZombie loads"));
		if (!Class)
		{
			return false;
		}
		SmokeUtils::StartWaveFight(World->GetSubsystem<UGameFlowSubsystem>());
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->Destroy();
		}
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == EInteractableType::Generator)
			{
				It->bGeneratorBroken = true;
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->HealthComponent->SetMaxHealth(5000.f, true);
			Member->bTacticalCeaseFire = true;
		}
		State.Leader = Squad->GetLeader();
		AOperativeCharacter* Leader = State.Leader.Get();
		if (!Leader)
		{
			Check(State, false, TEXT("squad leader"));
			return false;
		}
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (AEnemyCharacter* Keeper = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 3500.f + FVector(0.f, 0.f, 30.f)))
		{
			Keeper->CustomTimeDilation = 0.f;
			Keeper->SetActorTickEnabled(false);
		}

		// Placed: archetype, HP, name, the ABP route (a child of the Frostbitten's on the pack skeleton).
		const FVector PlacedAt = Leader->GetActorLocation() - Leader->GetActorRightVector() * 2500.f + FVector(0.f, 0.f, 30.f);
		AEnemyCharacter* Placed = PlaceZombie(World, Class, PlacedAt, -1);
		AEnemyCharacter* Reference = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, PlacedAt + FVector(0.f, 300.f, 0.f), FRotator::ZeroRotator);
		if (!Placed || !Reference)
		{
			Check(State, false, TEXT("placed zombie + a wave Frostbitten for reference"));
			return false;
		}
		FOverheadLabel Label;
		const bool bLabel = Placed->GetOverheadLabel(Label);
		Check(State, Placed->WasPlacedInLevel() && Placed->GetArchetype() == EEnemyArchetype::Frostbitten,
			FString::Printf(TEXT("placed BP_Enemy_MilitaryZombie -> archetype %s (Frostbitten via BP_Enemy_Frostbitten)"),
				*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Placed->GetArchetype()))));
		Check(State, FMath::IsNearlyEqual(Placed->GetHealthComponent()->GetMaxHealth(), Reference->GetHealthComponent()->GetMaxHealth()),
			FString::Printf(TEXT("HP %.0f like the Frostbitten (%.0f)"), Placed->GetHealthComponent()->GetMaxHealth(), Reference->GetHealthComponent()->GetMaxHealth()));
		Check(State, Placed->GetEnemyDisplayName() == TEXT("Military Zombie") && (!bLabel || Label.Text.StartsWith(TEXT("Military Zombie"))),
			FString::Printf(TEXT("name \"%s\", nameplate \"%s\""), *Placed->GetEnemyDisplayName(), *Label.Text.Replace(TEXT("\n"), TEXT(" | "))));
		const UEnemyAnimInstance* PlacedAnim = AnimOf(Placed);
		UClass* FrostAbp = LoadClass<UAnimInstance>(nullptr, FrostAbpClassPath);
		Check(State, PlacedAnim && FrostAbp && PlacedAnim->GetClass()->IsChildOf(FrostAbp) && PlacedAnim->GetClass() != FrostAbp,
			FString::Printf(TEXT("anim instance %s spawned on %s (a child of ABP_Enemy_Frostbitten)"), PlacedAnim ? *PlacedAnim->GetClass()->GetName() : TEXT("none"),
				*GetNameSafe(Placed->GetMesh()->GetSkinnedAsset())));
		Reference->Destroy();

		// Look variants: deterministic per seed, varied between instances.
		TSet<int32> Meshes;
		TSet<int32> MaterialSets;
		bool bLookOk = true;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			AEnemyCharacter* Look = PlaceZombie(World, Class, PlacedAt + FVector(400.f + 150.f * Index, 600.f, 0.f), -1);
			if (!Look)
			{
				continue;
			}
			Meshes.Add(Look->GetLookMeshIndex());
			MaterialSets.Add(Look->GetLookMaterialSetIndex());
			// Single-slot bodies (2026-10-21): a body of LookVariants.Bodies and one of THAT body's colour sets on slot 0.
			const FEnemyLookVariants& Variants = Look->LookVariants;
			const int32 BodyIndex = Look->GetLookMeshIndex();
			const FEnemyLookBody* Picked = Variants.Bodies.IsValidIndex(BodyIndex) ? &Variants.Bodies[BodyIndex] : nullptr;
			TArray<int32> SetsPerBody;
			for (const FEnemyLookBody& Each : Variants.Bodies)
			{
				SetsPerBody.Add(Each.Sets.Num());
			}
			int32 ExpectedBody = -1;
			int32 ExpectedSet = -1;
			EnemyAnimRules::PickLookBodyAndSet(Look->GetVariantSeed(), SetsPerBody, ExpectedBody, ExpectedSet);
			bLookOk &= Picked && Look->GetMesh()->GetSkeletalMeshAsset() == Picked->Mesh
				&& Picked->Sets.IsValidIndex(Look->GetLookMaterialSetIndex())
				&& Look->GetMesh()->GetMaterial(0) == Picked->Sets[Look->GetLookMaterialSetIndex()]
				&& BodyIndex == ExpectedBody && Look->GetLookMaterialSetIndex() == ExpectedSet
				&& AnimOf(Look) != nullptr;
			Look->Destroy();
		}
		Check(State, bLookOk && Meshes.Num() >= 2 && MaterialSets.Num() >= 2,
			FString::Printf(TEXT("8 placed zombies: %d different bodies, %d texture sets, each the seed's pick with its ABP running"), Meshes.Num(), MaterialSets.Num()));
		AEnemyCharacter* PinnedA = PlaceZombie(World, Class, PlacedAt + FVector(0.f, 900.f, 0.f), 4242);
		AEnemyCharacter* PinnedB = PlaceZombie(World, Class, PlacedAt + FVector(300.f, 1200.f, 0.f), 4242);
		Check(State, PinnedA && PinnedB && AnimOf(PinnedA) && AnimOf(PinnedB) && PinnedA->GetLookMeshIndex() == PinnedB->GetLookMeshIndex()
			&& PinnedA->GetLookMaterialSetIndex() == PinnedB->GetLookMaterialSetIndex()
			&& AnimOf(PinnedA)->WalkAnimation == AnimOf(PinnedB)->WalkAnimation && AnimOf(PinnedA)->RunAnimation == AnimOf(PinnedB)->RunAnimation,
			FString::Printf(TEXT("the same seed (VariantSeedOverride) -> the same body %d / %d, texture set %d / %d, walk %s / %s"),
				PinnedA ? PinnedA->GetLookMeshIndex() : -9, PinnedB ? PinnedB->GetLookMeshIndex() : -9,
				PinnedA ? PinnedA->GetLookMaterialSetIndex() : -9, PinnedB ? PinnedB->GetLookMaterialSetIndex() : -9,
				*GetNameSafe(PinnedA && AnimOf(PinnedA) ? AnimOf(PinnedA)->WalkAnimation.Get() : nullptr),
				*GetNameSafe(PinnedB && AnimOf(PinnedB) ? AnimOf(PinnedB)->WalkAnimation.Get() : nullptr)));
		for (AEnemyCharacter* Each : { Placed, PinnedA, PinnedB })
		{
			if (Each)
			{
				Each->Destroy();
			}
		}

		// The guard: placed 10 m ahead (inside the arena: the shot camera stands in front of it), facing away, senses off.
		const FVector GuardDesired = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 900.f + Leader->GetActorRightVector() * 450.f;
		const FVector GuardAt = SmokeUtils::FreeSpot(World, SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), GuardDesired)) + FVector(0.f, 0.f, 30.f);
		AEnemyCharacter* Guard = SpawnZombie(World, Class, GuardAt, Leader->GetActorForwardVector().Rotation(), true);
		State.Guard = Guard;
		Check(State, Guard && Guard->IsGuardingPost(), TEXT("the guard zombie guards its post"));
		if (!Guard)
		{
			return false;
		}
		Guard->GetHealthComponent()->SetMaxHealth(5000.f, true);
		return true;
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
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; State.bShotTaken = false; State.bFlag = false; State.Mark = 0.f; };
		AEnemyCharacter* Guard = State.Guard.Get();
		AEnemyCharacter* Spawned = State.Spawned.Get();
		AOperativeCharacter* Leader = State.Leader.Get();
		UEnemyAnimInstance* Anim = AnimOf(State.Stage >= 9 ? Spawned : Guard);
		AimCamera(State, World, State.Stage >= 9 ? Spawned : Guard);
		const bool bNeedsGuard = State.Stage >= 1 && State.Stage <= 8;
		if (State.Stage >= 1 && ((bNeedsGuard && (!Guard || !Anim)) || !Leader))
		{
			Check(State, false, FString::Printf(TEXT("stage %d: zombie with a UEnemyAnimInstance and the leader alive"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
			if (!StageSetup(World, State))
			{
				return Finish(State, false);
			}
			ResetSamples(State);
			Next();
			return true;

		case 1: // idle: the seeded idle loop, feet on the ground; then the idle breaks (a fidget, the devour)
		{
			Sample(State, Guard);
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (!State.bShotTaken)
			{
				State.bShotTaken = true;
				Check(State, Anim->IdleAnimation && Anim->IdleAnimations.Contains(Anim->IdleAnimation) && State.MaxPoseDeviation > 3.f,
					FString::Printf(TEXT("idle %s (seeded pick of %d) plays: hands / head up to %.1f cm off the reference pose (no T-pose)"),
						*GetNameSafe(Anim->IdleAnimation), Anim->IdleAnimations.Num(), State.MaxPoseDeviation));
				Check(State, State.MinToe > -6.f && State.MinToe < 10.f, FString::Printf(TEXT("idle: feet on the ground (lowest toe %.1f cm above the capsule bottom)"), State.MinToe));
				const FBox Bounds = Guard->GetMesh()->Bounds.GetBox();
				Check(State, Bounds.GetSize().Z > 120.f && Bounds.Min.Z > Guard->GetActorLocation().Z - Guard->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 15.f,
					FString::Printf(TEXT("body %.0f cm tall, bottom %.0f cm vs the capsule bottom (on the ground)"), Bounds.GetSize().Z,
						Bounds.Min.Z - (Guard->GetActorLocation().Z - Guard->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())));
				Shot(State, World, Guard, TEXT("idle"));
				// Speed the breaks up for the run (data: every 6-12 s, the devour 35 %).
				Anim->IdleBreakInterval = FVector2D(0.8f, 1.2f);
				Anim->IdleActivityChance = 0.5f;
				Anim->IdleBreakPlayRate = 3.f;
				State.SeenClips.Reset();
			}
			ActiveClips(Guard, &State.SeenClips);
			const UAnimSequenceBase* Break = Anim->GetIdleBreakClip();
			if (Break && Anim->IdleActivityAnimations.Contains(Break))
			{
				State.Mark += StepSeconds;
				if (State.Mark >= 2.f && !State.bFlag)
				{
					State.bFlag = true;
					Shot(State, World, Guard, TEXT("devour"));
				}
			}
			const bool bFidget = SawClipFrom(State, Anim->IdleBreakAnimations);
			const bool bDevour = SawClipFrom(State, Anim->IdleActivityAnimations);
			if ((bFidget && bDevour && State.bFlag) || State.StageTime > 70.f)
			{
				Check(State, bFidget, FString::Printf(TEXT("idle variety: another idle clip played as a break (%s)"), *FString::Join(State.SeenClips.Array(), TEXT(", "))));
				Check(State, bDevour, TEXT("the devour activity played while calm"));
				Check(State, Guard->GetVelocity().Size2D() < 5.f && Guard->IsGuardingPost(), TEXT("idle breaks keep it standing on its post"));
				Anim->IdleBreakPlayRate = 1.f;
				Anim->IdleActivityChance = 0.f;
				Anim->IdleBreakInterval = FVector2D(30.f, 30.f);
				ResetSamples(State);
				Next();
			}
			return true;
		}
		case 2: // patrol walk: a lure investigation walk away from the squad
		{
			if (!State.bFlag)
			{
				State.bFlag = true;
				// A stealth-lure investigation walk at half its speed (the patrol pace, below the run threshold).
				FPatrolSearchSpec Lure;
				Lure.bLure = true;
				Lure.ApproachSpeedFraction = 0.5f;
				Guard->StartPatrolSearchWith(SmokeUtils::ClearPoint(World, Guard->GetActorLocation(), Guard->GetActorLocation()
					+ Leader->GetActorForwardVector() * 900.f), Lure);
				return true;
			}
			const float Speed = Guard->GetVelocity().Size2D();
			State.MaxSpeed = FMath::Max(State.MaxSpeed, Speed);
			if (Anim->bIsMoving && !Anim->bIsRunning && Speed > 60.f)
			{
				State.Mark += StepSeconds;
				if (Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) < 0.01f)
				{
					Sample(State, Guard); // the walk itself (not the blend out of an idle break)
				}
			}
			if (State.Mark >= 1.5f && !State.bShotTaken)
			{
				State.bShotTaken = true;
				const int32 Walk = Anim->GetWalkVariantIndex();
				Check(State, Anim->WalkAnimationVariants.IsValidIndex(Walk) && Anim->WalkAnimation == Anim->WalkAnimationVariants[Walk].Clip
					&& Anim->GetIdleBreakClip() == nullptr,
					FString::Printf(TEXT("search walk at %.0f cm/s: walk variant %d %s (rate %.2f), no idle break while moving"), Speed, Walk,
						*GetNameSafe(Anim->WalkAnimation), Anim->WalkPlayRate));
				Check(State, State.MinToe > -8.f && State.MinToe < 12.f, FString::Printf(TEXT("walking: feet on the ground (toe %.1f..%.1f cm; lowest: %s)"),
					State.MinToe, State.MaxToe, *State.MinToeNote));
				Shot(State, World, Guard, TEXT("walk"));
			}
			if (State.bShotTaken && State.StageTime > 3.5f)
			{
				Next();
			}
			else if (State.StageTime > 15.f)
			{
				Check(State, false, FString::Printf(TEXT("the guard never walked its search (speed %.0f, moving %d, running %d)"), Speed, Anim->bIsMoving, Anim->bIsRunning));
				return Finish(State, false);
			}
			return true;
		}
		case 3: // alert: the rage roar, standing, facing the alarm
		{
			if (!State.bFlag)
			{
				State.bFlag = true;
				State.LeaderHpBefore = SquadHealth(World);
				Guard->BreakPatrol(EPatrolAlertCause::Sight, Leader->GetActorLocation());
				Check(State, Guard->IsPerforming() && PlaysClipFrom(Guard, Anim->AlertAnimations),
					FString::Printf(TEXT("it notices the squad: rage roar (%s)"), *ActiveClips(Guard)));
				ResetSamples(State);
				return true;
			}
			Sample(State, Guard);
			if (Guard->IsPerforming())
			{
				State.MaxSpeed = FMath::Max(State.MaxSpeed, Guard->GetVelocity().Size2D());
				State.Mark = State.StageTime;
				if (!State.bShotTaken && State.StageTime >= 1.2f)
				{
					State.bShotTaken = true;
					Shot(State, World, Guard, TEXT("rage"));
				}
				if (State.StageTime > 8.f)
				{
					Check(State, false, TEXT("the roar never ended"));
					return Finish(State, false);
				}
				return true;
			}
			const FVector ToLeader = (Leader->GetActorLocation() - Guard->GetActorLocation()).GetSafeNormal2D();
			Check(State, State.Mark > 1.8f && State.Mark < 4.f && State.MaxSpeed < 30.f && State.MaxPoseDeviation > 5.f,
				FString::Printf(TEXT("roared %.1f s standing (top speed %.0f cm/s), animated (%.1f cm)"), State.Mark, State.MaxSpeed, State.MaxPoseDeviation));
			Check(State, FVector::DotProduct(Guard->GetActorForwardVector().GetSafeNormal2D(), ToLeader) > 0.7f, TEXT("it turned to the alarm while roaring"));
			ResetSamples(State);
			Next();
			return true;
		}
		case 4: // the charge: a run variant, to the leader
		{
			const float Speed = Guard->GetVelocity().Size2D();
			if (Anim->bIsRunning && Speed > 150.f && Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) < 0.01f)
			{
				Sample(State, Guard); // the run itself (not the blend into the first blow)
			}
			State.MaxSpeed = FMath::Max(State.MaxSpeed, Speed);
			if (Anim->bIsRunning && Speed > 150.f)
			{
				State.Mark += StepSeconds;
				if (State.Mark >= 1.5f && !State.bShotTaken)
				{
					State.bShotTaken = true;
					const int32 Run = Anim->GetRunVariantIndex();
					Check(State, Anim->RunAnimationVariants.IsValidIndex(Run) && Anim->RunAnimation == Anim->RunAnimationVariants[Run].Clip,
						FString::Printf(TEXT("charging at %.0f cm/s: run variant %d %s (rate %.2f)"), Speed, Run, *GetNameSafe(Anim->RunAnimation), Anim->RunPlayRate));
					Shot(State, World, Guard, TEXT("run"));
				}
			}
			if (Anim->bIsAttacking)
			{
				Check(State, State.bShotTaken, FString::Printf(TEXT("ran to the leader (top speed %.0f cm/s)"), State.MaxSpeed));
				Check(State, State.MinToe > -8.f && State.MinToe < 15.f, FString::Printf(TEXT("running: feet on the ground (toe %.1f..%.1f cm; lowest: %s)"),
					State.MinToe, State.MaxToe, *State.MinToeNote));
				ResetSamples(State);
				Next();
				return true;
			}
			if (State.StageTime > 35.f)
			{
				Check(State, false, FString::Printf(TEXT("never attacked (distance %.0f cm, speed %.0f)"), Guard->GetDistanceTo(Leader), Speed));
				return Finish(State, false);
			}
			return true;
		}
		case 5: // attack
		{
			Sample(State, Guard);
			if (!State.bShotTaken && State.StageTime >= 0.6f)
			{
				State.bShotTaken = true;
				Check(State, PlaysClipFrom(Guard, Anim->AttackAnimations), FString::Printf(TEXT("attack clip %d of %d on the one-shot slot: %s"),
					Anim->AttackVariant + 1, Anim->AttackAnimations.Num(), *ActiveClips(Guard)));
				Shot(State, World, Guard, TEXT("attack"));
			}
			if (State.StageTime < 4.5f)
			{
				return true;
			}
			const float SquadHp = SquadHealth(World);
			Check(State, SquadHp < State.LeaderHpBefore, FString::Printf(TEXT("the blow landed on %s (squad %.0f -> %.0f HP)"),
				*GetNameSafe(Guard->GetCurrentTarget()), State.LeaderHpBefore, SquadHp));
			Check(State, State.MaxPoseDeviation > 5.f && State.MinToe > -8.f, FString::Printf(TEXT("attacking: animated (%.1f cm), feet %.1f cm"), State.MaxPoseDeviation, State.MinToe));
			Guard->SetActorTickEnabled(false); // scripted hits from here
			Guard->GetCharacterMovement()->StopMovementImmediately();
			Next();
			return true;
		}
		case 6: // hit reaction
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			if (!State.bFlag)
			{
				State.bFlag = true;
				State.SeenClips.Reset();
				FDamageSpec Spec;
				Spec.Amount = 10.f;
				Spec.ArmorPenetration = 1.f;
				Spec.AttackerSource = TEXT("Smoke");
				Guard->GetHealthComponent()->TakeDamage(Spec);
				State.StageTime = 2.5f;
				return true;
			}
			ActiveClips(Guard, &State.SeenClips);
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, SawClipFrom(State, Anim->HitAnimations), FString::Printf(TEXT("hit reaction clip: %s"), *ActiveClips(Guard)));
			Shot(State, World, Guard, TEXT("hit"));
			Next();
			return true;
		}
		case 7: // knockdown (the Epic Manny clips of /Game/Animations_KnockDown on the pack's Manny copy)
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Guard->IsKnockedDown() && !State.bFlag)
			{
				State.bFlag = true;
				FDamageSpec Heavy;
				Heavy.Amount = 70.f;
				Heavy.ArmorPenetration = 1.f;
				Heavy.AttackerSource = TEXT("Smoke");
				Guard->GetHealthComponent()->TakeDamage(Heavy);
				const UKnockdownComponent* Knock = Guard->FindComponentByClass<UKnockdownComponent>();
				const UAnimSequenceBase* Clip = Knock ? Knock->GetPlayingClip() : nullptr;
				Check(State, Guard->IsKnockedDown() && Clip && Clip->GetPackage()->GetName().StartsWith(TEXT("/Game/Animations_KnockDown/Knocked_")),
					FString::Printf(TEXT("a 70 HP hit knocks it down with the Manny clip (%s)"), Clip ? *Clip->GetPathName() : TEXT("none")));
				if (!Guard->IsKnockedDown())
				{
					return Finish(State, false);
				}
				ResetSamples(State);
				return true;
			}
			Sample(State, Guard);
			const UKnockdownComponent* Knock = Guard->FindComponentByClass<UKnockdownComponent>();
			if (Knock->GetPhase() != EKnockdownPhase::Downed)
			{
				if (State.StageTime > 8.f)
				{
					Check(State, false, TEXT("never lay down"));
					return Finish(State, false);
				}
				return true;
			}
			const float Bottom = Guard->GetActorLocation().Z - Guard->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Pelvis = Guard->GetMesh()->GetSocketLocation(TEXT("pelvis")).Z - Bottom;
			Check(State, Pelvis < 45.f && Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) > 0.5f,
				FString::Printf(TEXT("down: pelvis %.0f cm above the floor (%s)"), Pelvis, *ActiveClips(Guard)));
			Shot(State, World, Guard, TEXT("knockdown"));
			Guard->GetHealthComponent()->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
			Next();
			return true;
		}
		case 8: // died on the ground; then a wave spawn with the entrance
		{
			if (State.StageTime < 1.4f)
			{
				return true;
			}
			const UKnockdownComponent* Knock = Guard->FindComponentByClass<UKnockdownComponent>();
			const UAnimSequenceBase* Clip = Knock ? Knock->GetPlayingClip() : nullptr;
			Check(State, Guard->IsDying() && Knock && Knock->DiedWhileDown() && Clip && Clip->GetName().StartsWith(TEXT("Death_")),
				FString::Printf(TEXT("died while down: %s"), Clip ? *Clip->GetName() : TEXT("none")));
			Shot(State, World, Guard, TEXT("death_down"));
			// UWaveSubsystem::SpawnEnemy itself (the entrance call), the Frostbitten mapping pointed at the zombie for this spawn.
			ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			if (GameMode && Waves)
			{
				const TSoftClassPtr<AEnemyCharacter>* Old = GameMode->EnemyClasses.Find(EEnemyArchetype::Frostbitten);
				const TSoftClassPtr<AEnemyCharacter> Saved = Old ? *Old : TSoftClassPtr<AEnemyCharacter>();
				GameMode->EnemyClasses.Add(EEnemyArchetype::Frostbitten, TSoftClassPtr<AEnemyCharacter>(State.ZombieClass.Get()));
				const FVector At = SmokeUtils::FreeSpot(World, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1200.f) + FVector(0.f, 0.f, 30.f);
				State.Spawned = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, At, (-Leader->GetActorForwardVector()).Rotation());
				if (Old)
				{
					GameMode->EnemyClasses.Add(EEnemyArchetype::Frostbitten, Saved);
				}
				else
				{
					GameMode->EnemyClasses.Remove(EEnemyArchetype::Frostbitten);
				}
			}
			Spawned = State.Spawned.Get();
			Check(State, Spawned && Spawned->GetClass() == State.ZombieClass.Get(), TEXT("a wave spawn (UWaveSubsystem::SpawnEnemy) made a Military Zombie"));
			if (!Spawned)
			{
				return Finish(State, false);
			}
			Spawned->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Spawned->GetHealthComponent()->SetMaxHealth(5000.f, true);
			Check(State, Spawned->IsPerforming() && PlaysClipFrom(Spawned, AnimOf(Spawned)->EntranceAnimations),
				FString::Printf(TEXT("the wave spawn plays its entrance: %s"), *ActiveClips(Spawned)));
			ResetSamples(State);
			Next();
			return true;
		}
		case 9: // the entrance: standing, then it hunts
		{
			if (!Spawned || !Anim)
			{
				Check(State, false, TEXT("wave-spawned zombie alive"));
				return Finish(State, false);
			}
			Sample(State, Spawned);
			if (Spawned->IsPerforming())
			{
				State.MaxSpeed = FMath::Max(State.MaxSpeed, Spawned->GetVelocity().Size2D());
				State.Mark = State.StageTime;
				if (!State.bShotTaken && State.StageTime >= 1.5f)
				{
					State.bShotTaken = true;
					Shot(State, World, Spawned, TEXT("transformation"));
				}
				if (State.StageTime > 10.f)
				{
					Check(State, false, TEXT("the entrance never ended"));
					return Finish(State, false);
				}
				return true;
			}
			if (!State.bFlag)
			{
				State.bFlag = true;
				Check(State, State.Mark > 3.f && State.Mark < 5.f && State.MaxSpeed < 20.f && State.MaxPoseDeviation > 5.f,
					FString::Printf(TEXT("entrance %.1f s standing (top speed %.0f cm/s, pose %.1f cm off the reference, toe %.1f cm)"), State.Mark, State.MaxSpeed,
						State.MaxPoseDeviation, State.MinToe));
				State.Mark = State.StageTime;
			}
			if (Spawned->GetVelocity().Size2D() > 100.f || State.StageTime - State.Mark > 6.f)
			{
				Check(State, Spawned->GetVelocity().Size2D() > 100.f, TEXT("after the entrance it hunts the squad"));
				Spawned->SetActorTickEnabled(false);
				Spawned->GetCharacterMovement()->StopMovementImmediately();
				Next();
			}
			return true;
		}
		case 10: // dies standing: anim_Dying held
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (!Spawned->IsDying())
			{
				FDamageSpec Kill;
				Kill.Amount = 100000.f;
				Kill.ArmorPenetration = 1.f;
				Kill.AttackerSource = TEXT("Smoke");
				Spawned->GetHealthComponent()->TakeDamage(Kill);
				State.SeenClips.Reset();
				return true;
			}
			ActiveClips(Spawned, &State.SeenClips);
			if (State.StageTime < 4.5f)
			{
				return true;
			}
			const float Weight = Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot);
			const UKnockdownComponent* Knock = Spawned->FindComponentByClass<UKnockdownComponent>();
			const UAnimSequenceBase* FallClip = Knock ? Knock->GetPlayingClip() : nullptr;
			const bool bDeathClip = SawClipFrom(State, Anim->DeathAnimations) || (FallClip && FallClip->GetName().StartsWith(TEXT("Death_")));
			Check(State, bDeathClip && Weight > 0.9f, FString::Printf(TEXT("died standing: death clip held (one-shot weight %.2f, %s)"), Weight, *ActiveClips(Spawned)));
			Shot(State, World, Spawned, TEXT("death"));
			Next();
			return true;
		}
		default:
			if (State.StageTime < 1.f)
			{
				return true;
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
		State->Listener = MakeShared<FBodyErrorListener>();
		GLog->AddOutputDevice(State->Listener.Get());
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
		TEXT("CodexTactics.MilitaryZombieSmoke"),
		TEXT("Dev check: the Military Zombie (child of the Frostbitten ABP / BP, the pack's clips) idles with variety, devours, walks, roars, ")
		TEXT("runs, attacks, flinches, falls, dies, and a wave spawn plays its transformation; arg 'shot' saves Saved/Screenshots/military_zombie_*.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
