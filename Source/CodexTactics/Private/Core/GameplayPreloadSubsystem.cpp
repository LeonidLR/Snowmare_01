#include "Core/GameplayPreloadSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"
#include "CodexTactics.h"
#include "Components/SkinnedMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/AssetManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Ladders/LadderClimbComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	const FName PreloadAIBlocker(TEXT("GameplayPreload"));
	constexpr double PreparationTimeoutSeconds = 120.0;
	TAutoConsoleVariable<int32> CVarPreloadEnabled(TEXT("Codex.Preload.Enabled"), 1,
		TEXT("1: prepare the world asynchronously (classes, bodies, weapons, animation sets, ladder clips) before the squad spawns and the clocks "
			"start (004 enemy-appear hitch). 0: skip it - the old synchronous first-use loads (A/B). Read when a level starts; the dev command line -CodexPreload=0/1 overrides it (smokes)."));
}

namespace
{
	TAutoConsoleVariable<int32> CVarPreloadWorldPause(TEXT("Codex.Preload.WorldPause"), 1,
		TEXT("1 (default): also pause the world (engine pause) while the preload prepares, on top of the explicit squad tick / clock / input / AI "
			"gates - the level's own beats (tutorial cards, fades, placed enemies, quest timers) must not run under the loading cover. 0: the explicit "
			"gates only (A/B; -PreloadNoWorldPause does the same for one process)."));
}

bool UGameplayPreloadSubsystem::UsesWorldPause()
{
	return CVarPreloadWorldPause.GetValueOnGameThread() != 0;
}

bool UGameplayPreloadSubsystem::IsPreloadEnabled()
{
#if !UE_BUILD_SHIPPING
	// -CodexPreload=0|1 on the command line wins: -ExecCmds run after the first level's StartPlay, too late for the A/B of a smoke.
	int32 CommandLine = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("CodexPreload="), CommandLine))
	{
		return CommandLine != 0;
	}
#endif
	return CVarPreloadEnabled.GetValueOnGameThread() != 0;
}

bool UGameplayPreloadSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

bool UGameplayPreloadSubsystem::IsPreparing(const UWorld* World)
{
	const auto* Preload = World ? World->GetSubsystem<UGameplayPreloadSubsystem>() : nullptr;
	if (!Preload)
	{
		return false;
	}
	if (Preload->Phase == EPhase::WaitingForMode)
	{
		return World->GetAuthGameMode<ACodexTacticsGameMode>() != nullptr;
	}
	return Preload->Phase == EPhase::Initial || Preload->Phase == EPhase::Dependencies || Preload->Phase == EPhase::Compiling || Preload->Phase == EPhase::Binding;
}

EGameplayPreloadState UGameplayPreloadSubsystem::GetState(const UWorld* World)
{
	const auto* Preload = World ? World->GetSubsystem<UGameplayPreloadSubsystem>() : nullptr;
	if (!Preload)
	{
		return EGameplayPreloadState::NotApplicable;
	}
	if (IsPreparing(World))
	{
		return EGameplayPreloadState::Preparing;
	}
	switch (Preload->Phase)
	{
	case EPhase::Disabled: return EGameplayPreloadState::Disabled;
	case EPhase::Ready: return EGameplayPreloadState::Ready;
	case EPhase::Failed: return EGameplayPreloadState::Failed;
	case EPhase::Stopped: return Preload->bFailed ? EGameplayPreloadState::Failed : EGameplayPreloadState::NotApplicable;
	default: return EGameplayPreloadState::NotApplicable; // WaitingForMode without a Codex game mode
	}
}

FGameplayPreloadProgress UGameplayPreloadSubsystem::GetProgress(const UWorld* World)
{
	FGameplayPreloadProgress Progress;
	Progress.State = GetState(World);
	// Include the interval before StartPreparation without changing the startup gates' state semantics.
	if (Progress.IsVisible() && !IsPreloadEnabled()) { Progress.State = EGameplayPreloadState::Disabled; }
	const auto* Preload = World ? World->GetSubsystem<UGameplayPreloadSubsystem>() : nullptr;
	if (!Preload) { return Progress; }
	for (const auto& Handle : Preload->Handles)
	{
		if (!Handle.IsValid()) { continue; }
		int32 Completed = 0, Total = 0;
		Handle->GetLoadedCount(Completed, Total); // Failed requests count as completed, as in the engine API.
		Progress.TotalPaths += FMath::Max(Total, 0);
		Progress.CompletedPaths += FMath::Clamp(Completed, 0, FMath::Max(Total, 0));
	}
	switch (Preload->Phase)
	{
	case EPhase::Initial: Progress.PhaseLabel = TEXT("Core assets"); break;
	case EPhase::Dependencies: Progress.PhaseLabel = TEXT("Level assets"); break;
	case EPhase::Compiling: Progress.PhaseLabel = TEXT("Preparing assets"); break;
	case EPhase::Binding: Progress.PhaseLabel = TEXT("Preparing level"); break;
	default: Progress.PhaseLabel = TEXT("Preparing to load"); break;
	}
	return Progress;
}

FString UGameplayPreloadSubsystem::StateName(EGameplayPreloadState State)
{
	switch (State)
	{
	case EGameplayPreloadState::Disabled: return TEXT("disabled");
	case EGameplayPreloadState::Preparing: return TEXT("preparing");
	case EGameplayPreloadState::Ready: return TEXT("ready");
	case EGameplayPreloadState::Failed: return TEXT("failed");
	default: return TEXT("not-applicable");
	}
}

bool UGameplayPreloadSubsystem::IsGameplayReady(const UWorld* World)
{
	// Ready and Failed are distinct: a failure still releases gameplay (IsGameplayReleased, fail-open, so a damaged installation cannot
	// leave a permanent loading cover), but it is never "ready" - smokes and diagnostics treat it as a failure.
	const EGameplayPreloadState State = GetState(World);
	return State != EGameplayPreloadState::Preparing && State != EGameplayPreloadState::Failed;
}

bool UGameplayPreloadSubsystem::TryClaimAutomaticSpawn()
{
	if (IsPreparing(GetWorld()) || LastAutomaticSpawnFrame == GFrameCounter)
	{
		return false;
	}
	LastAutomaticSpawnFrame = GFrameCounter;
	return true;
}

void UGameplayPreloadSubsystem::StartPreparation(ACodexTacticsGameMode& Mode)
{
	if (Phase != EPhase::WaitingForMode)
	{
		return;
	}
	if (!IsPreloadEnabled())
	{
		Phase = EPhase::Disabled;
		UE_LOG(LogCodexTactics, Display, TEXT("[GameplayPreload] disabled (Codex.Preload.Enabled 0): synchronous first-use loads"));
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Codex_GameplayPreloadInitial);
	Phase = EPhase::Initial;
	StartSeconds = FPlatformTime::Seconds();
	PhaseSeconds = StartSeconds;
	ModeOwner = &Mode;
	if (auto* AI = GetWorld()->GetSubsystem<UWorldAIPauseSubsystem>())
	{
		AI->AddPauseBlocker(PreloadAIBlocker);
		bOwnsAIBlocker = true;
	}
	TArray<FGameplayPreloadPath> Paths;
	GameplayPreloadCatalog::GatherInitial(Mode, Paths);
	RequestBatch(MoveTemp(Paths));
}

void UGameplayPreloadSubsystem::RequestBatch(TArray<FGameplayPreloadPath> Paths)
{
	Batch = MoveTemp(Paths);
	TArray<FSoftObjectPath> Unique;
	TSet<FSoftObjectPath> Seen;
	for (const FGameplayPreloadPath& Item : Batch)
	{
		if (!Item.Path.IsNull() && !Seen.Contains(Item.Path))
		{
			Seen.Add(Item.Path);
			Unique.Add(Item.Path);
		}
	}
	if (Unique.IsEmpty())
	{
		OnBatchLoaded();
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[GameplayPreload] requesting %d paths (phase %d)"), Unique.Num(), int32(Phase));
	auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Unique),
		FStreamableDelegate::CreateUObject(this, &UGameplayPreloadSubsystem::OnBatchLoaded));
	if (!Handle.IsValid())
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[GameplayPreload] async request rejected"));
		Finish(false);
		return;
	}
	Handles.Add(MoveTemp(Handle));
}

void UGameplayPreloadSubsystem::GatherDependencies(UObject* Object, TArray<FGameplayPreloadPath>& Out)
{
	auto AddSoft = [&Out](const FSoftObjectPath& Path)
	{
		if (!Path.IsNull())
		{
			Out.Add({Path, false});
		}
	};
	if (const UWeaponDataAsset* Weapon = Cast<UWeaponDataAsset>(Object))
	{
		AddSoft(Weapon->HandMesh.ToSoftObjectPath());
		AddSoft(Weapon->HandSkeletalMesh.ToSoftObjectPath());
		for (const auto& Part : Weapon->HandMeshParts)
		{
			AddSoft(Part.ToSoftObjectPath());
		}
		AddSoft(Weapon->LoadedRoundMesh.ToSoftObjectPath());
		AddSoft(Weapon->ProjectileMesh.ToSoftObjectPath());
		AddSoft(Weapon->MuzzleFlashFx.ToSoftObjectPath());
		AddSoft(Weapon->BackblastFx.ToSoftObjectPath());
		AddSoft(Weapon->TrailFx.ToSoftObjectPath());
	}
	UClass* Class = Cast<UClass>(Object);
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		return;
	}
	if (const auto* Enemy = Cast<AEnemyCharacter>(Class->GetDefaultObject()))
	{
		for (USkeletalMesh* Mesh : Enemy->LookVariants.Meshes)
		{
			if (Mesh) { Meshes.AddUnique(Mesh); }
		}
		for (const FEnemyLookBody& Body : Enemy->LookVariants.Bodies)
		{
			if (Body.Mesh) { Meshes.AddUnique(Body.Mesh.Get()); }
		}
	}
	auto InspectComponent = [this, &AddSoft](UActorComponent* Component)
	{
		if (const auto* Skinned = Cast<USkinnedMeshComponent>(Component))
		{
			if (auto* Mesh = Skinned->GetSkinnedAsset())
			{
				Meshes.AddUnique(Mesh);
			}
		}
		if (const auto* Ladder = Cast<ULadderClimbComponent>(Component))
		{
			AddSoft(Ladder->UpLoopClip.ToSoftObjectPath());
			AddSoft(Ladder->ClimbOutClip.ToSoftObjectPath());
			AddSoft(Ladder->IntoFromTopClip.ToSoftObjectPath());
		}
	};
	for (auto* Component : Class->GetDefaultObject<AActor>()->GetComponents())
	{
		InspectComponent(Component);
	}
	auto* Actual = Cast<UBlueprintGeneratedClass>(Class);
	for (UClass* Parent = Actual; Parent; Parent = Parent->GetSuperClass())
	{
		auto* Blueprint = Cast<UBlueprintGeneratedClass>(Parent);
		if (Blueprint && Blueprint->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node)
				{
					InspectComponent(Node->GetActualComponentTemplate(Actual));
				}
			}
		}
	}
}

void UGameplayPreloadSubsystem::OnBatchLoaded()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Codex_GameplayPreloadBatchComplete);
	if (Phase != EPhase::Initial && Phase != EPhase::Dependencies)
	{
		return;
	}
	TArray<FGameplayPreloadPath> Dependencies;
	if (Phase == EPhase::Initial && !GetWorld()->HasBegunPlay())
	{
		bInitialBatchAwaitingBeginPlay = true;
		return;
	}
	int32 OptionalMissCount = 0;
	bool bMissingRequired = false;
	for (const FGameplayPreloadPath& Item : Batch)
	{
		UObject* Object = Item.Path.ResolveObject();
		if (!Object)
		{
			if (Item.bRequired)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("[GameplayPreload] missing required %s"), *Item.Path.ToString());
				bMissingRequired = true;
			}
			else
			{
				++OptionalMissCount;
				OptionalMisses.Add(Item.Path);
			}
			continue;
		}
		OptionalMisses.Remove(Item.Path);
		if (USkinnedAsset* Mesh = Cast<USkinnedAsset>(Object))
		{
			Meshes.AddUnique(Mesh);
		}
		if (Phase == EPhase::Initial)
		{
			GatherDependencies(Object, Dependencies);
		}
	}
	const double Now = FPlatformTime::Seconds();
	UE_LOG(LogCodexTactics, Display, TEXT("[GameplayPreload] phase %d %.2f ms, %d optional misses"), int32(Phase), (Now - PhaseSeconds) * 1000.0, OptionalMissCount);
	PhaseSeconds = Now;
	if (bMissingRequired)
	{
		Finish(false);
		return;
	}
	if (Phase == EPhase::Initial)
	{
		Phase = EPhase::Dependencies;
		if (auto* Mode = ModeOwner.Get())
		{
			Mode->CompleteGameplayPreloadSquadSpawn();
		}
		GameplayPreloadCatalog::GatherAnimations(Dependencies);
		for (TObjectIterator<UOperativeAnimInstance> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld())
			{
				TArray<FSoftObjectPath> Defaults;
				It->GatherDefaultAnimationPaths(Defaults);
				for (const FSoftObjectPath& Path : Defaults) { Dependencies.Add({ Path, false }); }
			}
		}
		RequestBatch(MoveTemp(Dependencies));
		return;
	}
	Phase = EPhase::Compiling;
}

void UGameplayPreloadSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Phase != EPhase::Initial && Phase != EPhase::Dependencies && Phase != EPhase::Compiling)
	{
		return;
	}
	if (FPlatformTime::Seconds() - StartSeconds >= PreparationTimeoutSeconds)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[GameplayPreload] timed out in phase %d"), int32(Phase));
		Finish(false);
		return;
	}
	if (bInitialBatchAwaitingBeginPlay && GetWorld()->HasBegunPlay())
	{
		bInitialBatchAwaitingBeginPlay = false;
		OnBatchLoaded();
	}
	if (Phase != EPhase::Compiling)
	{
		return;
	}
#if WITH_EDITOR
	for (const USkinnedAsset* Mesh : Meshes)
	{
		if (Mesh && Mesh->IsCompiling())
		{
			return;
		}
	}
#endif
	Finish(true);
}

void UGameplayPreloadSubsystem::ReleaseStartupGates()
{
	if (bOwnsAIBlocker)
	{
		if (auto* AI = GetWorld()->GetSubsystem<UWorldAIPauseSubsystem>())
		{
			AI->RemovePauseBlocker(PreloadAIBlocker);
		}
		bOwnsAIBlocker = false;
	}
	if (auto* Mode = ModeOwner.Get())
	{
		Mode->ReleaseGameplayPreloadPause();
	}
}

void UGameplayPreloadSubsystem::Finish(bool bSuccess)
{
	bFailed = !bSuccess;
	Phase = EPhase::Binding;
	if (bFailed)
	{
		if (auto* Mode = ModeOwner.Get())
		{
			Mode->CompleteGameplayPreloadSquadSpawn();
		}
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Codex_GameplayPreloadBindAnimations);
		// Prepare the shared ladder tracks while startup gates still hold. New actors reuse the same clip/basis entry.
		for (TObjectIterator<ULadderClimbComponent> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld() && It->GetOwner() && It->IsRegistered()) { It->EnsureClips(); }
		}
		OnAssetsPrepared.Broadcast();
	}
	if (Phase != EPhase::Binding)
	{
		return;
	}
	Phase = bSuccess ? EPhase::Ready : EPhase::Failed;
	ReleaseStartupGates();
	UE_LOG(LogCodexTactics, Display, TEXT("[GameplayPreload] %s after %.2f ms, %d skinned assets"), bSuccess ? TEXT("ready") : TEXT("failed; continuing gameplay"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0, Meshes.Num());
}

void UGameplayPreloadSubsystem::Deinitialize()
{
	Phase = EPhase::Stopped;
	OnAssetsPrepared.Clear();
	for (const auto& Handle : Handles)
	{
		Handle->CancelHandle();
	}
	Handles.Reset();
	ReleaseStartupGates();
	Meshes.Reset();
	OptionalMisses.Reset();
	Batch.Reset();
	Super::Deinitialize();
}

TStatId UGameplayPreloadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGameplayPreloadSubsystem, STATGROUP_Tickables);
}
