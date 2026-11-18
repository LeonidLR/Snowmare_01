#include "LevelGen/CodexLevelPropsSubsystem.h"
#include "CodexTactics.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "LevelGen/CodexPropActors.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsGreybox.h"
#include "Characters/TransferRules.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "Power/PowerGeneratorActor.h"
#include "LevelGen/PropsGameplayRules.h"
#include "LevelGen/PropsLayoutView.h"
#include "LevelGen/PropsValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tasks/Task.h"

namespace CodexLevelPropsInternal
{
	static bool ActorHasVisualMesh(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		for (const UMeshComponent* MeshComp : MeshComponents)
		{
			if (MeshComp && MeshComp->IsVisible())
			{
				if (const UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(MeshComp))
				{
					if (StaticMeshComp->GetStaticMesh() != nullptr)
					{
						return true;
					}
				}
				else if (const USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(MeshComp))
				{
					if (SkelMeshComp->GetSkeletalMeshAsset() != nullptr)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	static TAutoConsoleVariable<int32> CVarPropsEnabled(
		TEXT("Codex.Props.Enabled"),
		1,
		TEXT("Controls levelgen props spawning: 1 (default) enabled, 0 disabled."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPropsMinesInert(
		TEXT("Codex.Props.MinesInert"),
		0,
		TEXT("When 1, mines and trapped crates are armed but inert (no damage/explosion; for smokes). Default 0."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPropsReal(
		TEXT("Codex.Props.Real"),
		1,
		TEXT("Live maps (G003b): 1 (default) the props spawn as the game's real objects (props_balance.json gameplay.classes: barrels, loot ")
		TEXT("crates / piles, mines, PMC wires, turrets), the greybox only for a kind without one; 0 greybox props only."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPropsGreybox(
		TEXT("Codex.Props.Greybox"),
		1,
		TEXT("Greybox look of levelgen props without a real class (G003): 1 (default) for Training Ground autogen props files (source auto), ")
		TEXT("2 for every props file, 0 off (meshless props as before: the live-map guard drops the nav-affecting ones)."),
		ECVF_Default);

	/** True when this props file gets the greybox look (Mode = Codex.Props.Greybox; the file's top-level "source"). */
	static bool UsesGreybox(const FLevelPropsFile& File, int32 Mode)
	{
		if (Mode >= 2)
		{
			return true;
		}
		if (Mode <= 0)
		{
			return false;
		}
		const TSharedPtr<FJsonValue>* Source = File.Extra.Find(TEXT("source"));
		FString SourceName;
		return Source && Source->IsValid() && (*Source)->TryGetString(SourceName) && SourceName == TEXT("auto");
	}

	static FAutoConsoleCommandWithWorld CmdPropsDump(
		TEXT("Codex.Props.Dump"),
		TEXT("Dumps spawned levelgen props counts per layer and dropped IDs."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (World)
			{
				if (UCodexLevelPropsSubsystem* Sub = World->GetSubsystem<UCodexLevelPropsSubsystem>())
				{
					Sub->DumpProps();
				}
			}
		}));
}

bool UCodexLevelPropsSubsystem::IsPropsEnabled()
{
	return CodexLevelPropsInternal::CVarPropsEnabled.GetValueOnGameThread() != 0;
}

bool UCodexLevelPropsSubsystem::AreMinesInert()
{
	return CodexLevelPropsInternal::CVarPropsMinesInert.GetValueOnGameThread() != 0;
}

FString UCodexLevelPropsSubsystem::GetDefaultPropsPathForMap(const FString& MapName)
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/LevelGen/props") / (MapName + TEXT(".props.json")));
}

FString UCodexLevelPropsSubsystem::GetDefaultLayoutPathForMap(const FString& LayoutName)
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted") / (LayoutName + TEXT(".json")));
}

bool UCodexLevelPropsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor;
}

void UCodexLevelPropsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!IsPropsEnabled())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Props] spawning disabled via Codex.Props.Enabled 0 on %s"),
			*InWorld.GetMapName());
		return;
	}

	const FString MapName = UWorld::RemovePIEPrefix(InWorld.GetMapName());
	if (MapName.IsEmpty())
	{
		return;
	}

	// On live maps (e.g. L_Gen_Depot_1), only spawn props that have a valid visual mesh.
	// Meshless/invisible props with NavArea_Null would puncture the navmesh and break pathfinding.
	SpawnPropsForMap(MapName, nullptr, /*bRequireMesh=*/ true, /*bLive=*/ true);
}

void UCodexLevelPropsSubsystem::Deinitialize()
{
	// The frame-slice ticker lives on the core ticker: it must not outlive the world.
	StopSpawnTicker();
	const UWorld* World = GetWorld();
	if (World && World->bIsTearingDown)
	{
		// The world destroys its actors itself; only drop the bookkeeping.
		SpawnedProps.Reset();
		SpawnedGameplay.Reset();
		GameplayByLayer.Reset();
		PropsByLayer.Reset();
		bHasSpawned = false;
	}
	else
	{
		ClearSpawnedProps();
	}
	OnPropsSpawned.Clear();
	OnPropsSpawnedDynamic.Clear();
	Super::Deinitialize();
}

void UCodexLevelPropsSubsystem::StopSpawnTicker()
{
	if (SpawnTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SpawnTicker);
		SpawnTicker.Reset();
	}
	Session.Reset();
}

int32 UCodexLevelPropsSubsystem::SpawnPropsForMap(
	const FString& MapName,
	const TSet<FString>* SkippedIds,
	bool bRequireMesh,
	bool bLive)
{
	const FString PropsPath = GetDefaultPropsPathForMap(MapName);
	if (!FPaths::FileExists(PropsPath))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Props] no props file found for map '%s' at %s"), *MapName, *PropsPath);
		return 0;
	}

	return SpawnPropsFromFile(PropsPath, FString(), SkippedIds, bRequireMesh, bLive);
}

/** One spawn of a props file: what is left to spawn and what the objects need from each other (G003b: spread over frames). */
struct UCodexLevelPropsSubsystem::FPropsSpawnSession
{
	// --- prepare inputs (set on the game thread before PrepareSession) ---
	FString PropsFilePath;
	/** An explicit layout file (empty: resolved from the props file's layout name). */
	FString LayoutFilePath;
	/** The parsed props_balance.json (only read while the prepare runs). */
	TSharedPtr<FJsonObject> Balance;
	/** Codex.Props.Greybox, read on the game thread. */
	int32 GreyboxMode = 1;

	// --- prepare outputs (PrepareSession: pure data, no UObject - a live spawn runs it on a worker thread) ---
	/** The prepare ran and the game thread took its result (TakePreparedSession). */
	bool bPrepared = false;
	bool bPrepareOk = false;
	/** The worker-thread prepare of a live spawn (invalid for a direct spawn). */
	UE::Tasks::FTask PrepareTask;
	TArray<FLevelPropsResolvedObject> Objects;
	TArray<FString> DroppedIds;
	bool bHasLayout = false;
	TMap<FString, int32> LayerZ;
	/** Layer id -> layer kind (crates / barrels / turrets / mines / loot). */
	TMap<FString, FString> KindOfLayer;
	TMap<FString, FLevelPropsResolvedObject> AttachedTraps;
	TMap<FString, FLevelPropsResolvedObject> AttachedLoot;
	/** Level turret start states by object id (PropsGameplayRules::ResolveTurrets). */
	TMap<FString, PropsGameplayRules::FTurretSetup> TurretSetups;
	int32 PowerWarnings = 0;
	/** The file's own greybox rule (UsesGreybox); the game thread adds bGameplay. */
	bool bFileGreybox = false;
	/** File + layout + validation time (ms) and its parts: props file read + parse, layout read + build + validation + rules. */
	double PrepareMs = 0.0;
	double PrepParseMs = 0.0;
	double PrepLayoutMs = 0.0;

	// --- spawn state (game thread only) ---
	int32 Next = 0;
	TSet<FString> Skipped;
	bool bGreybox = false;
	bool bGameplay = false;
	bool bRequireMesh = false;
	TMap<FString, TWeakObjectPtr<ACodexPropCrate>> CratesById;
	/** gameplay.traps_faction: level mines / wires are the enemy's. */
	bool bTrapsHostile = true;
	int32 PoweredTurrets = 0;
	/** TURRET_POWER.md phase 1: the spawned generators / turrets by object id and each generator's cable links (FinishSession links them). */
	TMap<FString, TWeakObjectPtr<APowerGeneratorActor>> GeneratorsById;
	TMap<FString, TWeakObjectPtr<ATurretActor>> TurretsById;
	TMap<FString, TArray<FString>> GeneratorLinks;
	/** Every actor spawned so far: the slab snap ignores them. */
	TArray<TWeakObjectPtr<AActor>> ForTrace;
	/** Game-thread time of the spawn call itself (balance file, session set-up; all of the prepare for a direct spawn): counted in frame 0. */
	double GameThreadSetupMs = 0.0;
	double SpawnMs = 0.0;
	double MaxFrameMs = 0.0;
	/** Game-thread ms of the latest spawn frame (FinishSession adds its link pass to it). */
	double LastFrameMs = 0.0;
	int32 Frames = 0;
	/** Frames the game thread waited for the worker-thread prepare (no props work on them). */
	int32 WaitFrames = 0;
	/** Diagnostics (X6 spawn hitch 2026-11-18): spawn cost per layer kind / variant and the slowest object. */
	struct FKindCost
	{
		int32 Count = 0;
		double TotalMs = 0.0;
		double MaxMs = 0.0;
		FString MaxId;
	};
	TMap<FString, FKindCost> CostByKind;
	double SlowestObjectMs = 0.0;
	FString SlowestObject;
	int32 MaxFrame = 0;
	int32 MaxFrameObjects = 0;
};

void UCodexLevelPropsSubsystem::PrepareSession(FPropsSpawnSession& S)
{
	// Pure data only (a worker thread for a live spawn): file IO, JSON, layout view, validator, turret / power rules. No UObject, no CVar.
	const double StartTime = FPlatformTime::Seconds();
	const FJsonObject* Balance = S.Balance.Get();
	S.bPrepareOk = false;

	FString PropsJson;
	if (!FFileHelper::LoadFileToString(PropsJson, *S.PropsFilePath))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[Props] failed to read props file: %s"), *S.PropsFilePath);
		return;
	}

	FLevelPropsFile File;
	TArray<FString> ParseErrors;
	if (!LevelPropsFile::Parse(PropsJson, File, ParseErrors))
	{
		for (const FString& Err : ParseErrors)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[Props] parse error in %s: %s"), *S.PropsFilePath, *Err);
		}
		return;
	}

	const double ParsedTime = FPlatformTime::Seconds();
	TArray<FString> OverrideWarnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File, &OverrideWarnings);
	S.bFileGreybox = CodexLevelPropsInternal::UsesGreybox(File, S.GreyboxMode);
	for (const FLevelPropsLayer& Layer : File.Layers)
	{
		S.KindOfLayer.Add(Layer.Id, Layer.Kind);
	}

	// Resolve layout and validate if layout exists
	const FString PropsDir = FPaths::GetPath(S.PropsFilePath);
	const FString ResolvedLayoutPath = S.LayoutFilePath.IsEmpty()
		? ResolveLayoutPath(File.Layout, PropsDir)
		: S.LayoutFilePath;

	if (FPaths::FileExists(ResolvedLayoutPath))
	{
		FString LayoutJson;
		if (FFileHelper::LoadFileToString(LayoutJson, *ResolvedLayoutPath))
		{
			TArray<FString> LayoutErrors;
			FPropsLayoutConfig LayoutConfig;
			if (Balance)
			{
				FPropsLayoutView::ReadConfig(*Balance, LayoutConfig, LayoutErrors);
			}

			FPropsLayoutView LayoutView;
			if (FPropsLayoutView::Build(LayoutJson, LayoutView, LayoutErrors, LayoutConfig))
			{
				S.bHasLayout = true;
				for (const FPropsWalkLayer& Layer : LayoutView.GetLayers())
				{
					S.LayerZ.Add(Layer.Name, Layer.ZCm);
				}
				FPropsValidationResult ValResult;
				FPropsValidator::ValidateResolved(LayoutView, Resolved, ValResult, Balance);

				S.Objects = MoveTemp(ValResult.ValidObjects);
				for (const FLevelPropsResolvedObject& Dropped : ValResult.DroppedObjects)
				{
					S.DroppedIds.Add(Dropped.Object.Id);
				}
			}
			else
			{
				for (const FString& Err : LayoutErrors)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("[Props] layout build warning: %s"), *Err);
				}
			}
		}
	}

	if (!S.bHasLayout)
	{
		// No layout file available: spawn all resolved objects directly
		S.Objects = Resolved.Objects;
	}

	// Collect crate attachments (mined crates and loot crates)
	for (const FLevelPropsResolvedObject& ResObj : S.Objects)
	{
		if (!ResObj.Object.Attach.IsEmpty())
		{
			if (ResObj.Object.Variant.Equals(TEXT("mine_crate"), ESearchCase::IgnoreCase)
				|| ResObj.Object.Trigger.Equals(TEXT("crate_open"), ESearchCase::IgnoreCase))
			{
				S.AttachedTraps.Add(ResObj.Object.Attach, ResObj);
			}
			else if (ResObj.Object.Variant.Equals(TEXT("loot_crate"), ESearchCase::IgnoreCase)
				|| !ResObj.Object.LootTable.IsEmpty())
			{
				S.AttachedLoot.Add(ResObj.Object.Attach, ResObj);
			}
		}
	}

	// Level turrets (designer 2026-11-17): inactive by default, a share of them powered + hostile (PropsGameplayRules::ResolveTurrets).
	TArray<PropsGameplayRules::FTurretInput> Turrets;
	for (const FLevelPropsResolvedObject& ResObj : S.Objects)
	{
		if (S.KindOfLayer.FindRef(ResObj.LayerId) == TEXT("turrets") || ResObj.Object.Variant.StartsWith(TEXT("turret")))
		{
			Turrets.Add(PropsGameplayRules::TurretInputOf(ResObj.Object));
		}
	}
	S.TurretSetups = PropsGameplayRules::ResolveTurrets(Balance, PropsGameplayRules::TurretPoweredShare(Balance, &File.Extra), Turrets);
	// TURRET_POWER.md phase 1: the power network's links are checked (warnings only: a broken link leaves its turret dark).
	for (const FPropsValidationIssue& Issue : FPropsValidator::ValidatePowerLinks(S.Objects, S.KindOfLayer, Balance))
	{
		++S.PowerWarnings;
		UE_LOG(LogCodexTactics, Warning, TEXT("[Props] power: %s: %s"), *Issue.ObjectId, *Issue.Reason);
	}

	const double EndTime = FPlatformTime::Seconds();
	S.PrepParseMs = (ParsedTime - StartTime) * 1000.0;
	S.PrepLayoutMs = (EndTime - ParsedTime) * 1000.0;
	S.PrepareMs = (EndTime - StartTime) * 1000.0;
	S.bPrepareOk = true;
}

int32 UCodexLevelPropsSubsystem::SpawnPropsFromFile(
	const FString& PropsFilePath,
	const FString& LayoutFilePath,
	const TSet<FString>* SkippedIds,
	bool bRequireMesh,
	bool bLive)
{
	// A spawn still running (frame-sliced) is dropped first, so there is never a second ticker.
	StopSpawnTicker();
	if (!IsPropsEnabled())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Props] spawning skipped (Codex.Props.Enabled 0)"));
		return 0;
	}

	ClearSpawnedProps();
	if (!GetWorld())
	{
		return 0;
	}

	const double StartTime = FPlatformTime::Seconds();
	// Load balance configuration (game thread: it fills the variant tables; BalanceRoot is only read from here on).
	LoadBalanceFile();
	GreyboxCount = 0;

	TSharedRef<FPropsSpawnSession> NewSession = MakeShared<FPropsSpawnSession>();
	FPropsSpawnSession& S = *NewSession;
	S.PropsFilePath = PropsFilePath;
	S.LayoutFilePath = LayoutFilePath;
	S.Balance = BalanceRoot;
	S.GreyboxMode = CodexLevelPropsInternal::CVarPropsGreybox.GetValueOnGameThread();
	S.bRequireMesh = bRequireMesh;
	// G003b: a live map spawns the real game objects (barrels, loot crates / piles, mines, wires, turrets); the greybox look is the
	// fallback of a kind with no gameplay class (plain crates = cover) - on a live map for every props file, else autogen files only.
	S.bGameplay = bLive && CodexLevelPropsInternal::CVarPropsReal.GetValueOnGameThread() != 0;
	S.bTrapsHostile = PropsGameplayRules::AreTrapsHostile(BalanceRoot.Get());
	if (SkippedIds)
	{
		S.Skipped = *SkippedIds;
	}

	Session = NewSession;
	const uint32 Generation = ++SpawnGeneration;

	if (!bLive)
	{
		// A direct call (tests, tools): everything now, on this thread.
		PrepareSession(S);
		S.GameThreadSetupMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
		if (TakePreparedSession())
		{
			ProcessSession(0.0);
		}
		return GetSpawnedCount();
	}

	// A live map (X6 spawn hitch 2026-11-18): the file / layout / validation work (66 of the 68 ms X6 frame, a 1 MB layout file) runs on a
	// worker thread; the spawn starts on the first frame after it is done and is spread over frames (gameplay.spawn_frame_budget_ms).
	S.PrepareTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Prepared = NewSession]() { PrepareSession(*Prepared); });
	S.GameThreadSetupMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double FrameBudgetMs = PropsGameplayRules::SpawnFrameBudgetMs(BalanceRoot.Get());
	TWeakObjectPtr<UCodexLevelPropsSubsystem> WeakThis(this);
	SpawnTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, FrameBudgetMs, Generation](float)
	{
		UCodexLevelPropsSubsystem* Self = WeakThis.Get();
		if (!Self || Self->SpawnGeneration != Generation)
		{
			return false; // gone, or a newer spawn replaced this one (and owns SpawnTicker now)
		}
		const UWorld* World = Self->GetWorld();
		if (!World || World->bIsTearingDown || !World->HasBegunPlay() || !Self->Session.IsValid())
		{
			Self->SpawnTicker.Reset();
			Self->Session.Reset();
			return false;
		}
		FPropsSpawnSession& Pending = *Self->Session;
		if (!Pending.bPrepared)
		{
			if (!Pending.PrepareTask.IsCompleted())
			{
				++Pending.WaitFrames;
				return true; // the worker is still reading / validating: no props work on this frame
			}
			if (!Self->TakePreparedSession())
			{
				Self->SpawnTicker.Reset();
				return false;
			}
		}
		const bool bMore = Self->ProcessSession(FrameBudgetMs);
		if (Self->SpawnGeneration != Generation)
		{
			return false; // an OnPropsSpawned listener started a new spawn
		}
		if (!bMore)
		{
			Self->SpawnTicker.Reset();
		}
		return bMore;
	}), 0.f);
	return GetSpawnedCount();
}

bool UCodexLevelPropsSubsystem::TakePreparedSession()
{
	if (!Session.IsValid())
	{
		return false;
	}
	FPropsSpawnSession& S = *Session;
	S.bPrepared = true;
	if (!S.bPrepareOk)
	{
		Session.Reset(); // the props file did not read / parse (logged): nothing spawns, as before
		return false;
	}
	DroppedPropIds = S.DroppedIds;
	S.bGreybox = S.bGameplay || S.bFileGreybox;
	return true;
}

bool UCodexLevelPropsSubsystem::ProcessSession(double BudgetMs)
{
	if (!Session.IsValid())
	{
		return false;
	}
	FPropsSpawnSession& S = *Session;
	// Frame 0 also carries the spawn call's own game-thread time (all of the prepare for a direct spawn).
	const double CarriedMs = S.Frames == 0 ? S.GameThreadSetupMs : 0.0;
	if (BudgetMs > 0.0)
	{
		BudgetMs = FMath::Max(BudgetMs - CarriedMs, 1.0);
	}
	const double FrameStart = FPlatformTime::Seconds();
	const int32 FirstObject = S.Next;
	while (S.Next < S.Objects.Num())
	{
		const double ObjectStart = FPlatformTime::Seconds();
		const FLevelPropsResolvedObject& ResObj = S.Objects[S.Next++];
		SpawnOne(S, ResObj);
		const double ObjectMs = (FPlatformTime::Seconds() - ObjectStart) * 1000.0;
		const FString KindKey = S.KindOfLayer.FindRef(ResObj.LayerId) + TEXT("/") + ResObj.Object.Variant;
		FPropsSpawnSession::FKindCost& Cost = S.CostByKind.FindOrAdd(KindKey);
		++Cost.Count;
		Cost.TotalMs += ObjectMs;
		if (ObjectMs > Cost.MaxMs)
		{
			Cost.MaxMs = ObjectMs;
			Cost.MaxId = ResObj.Object.Id;
		}
		if (ObjectMs > S.SlowestObjectMs)
		{
			S.SlowestObjectMs = ObjectMs;
			S.SlowestObject = KindKey + TEXT(" ") + ResObj.Object.Id;
		}
		if (BudgetMs > 0.0 && (FPlatformTime::Seconds() - FrameStart) * 1000.0 >= BudgetMs && S.Next < S.Objects.Num())
		{
			break;
		}
	}
	const double FrameMs = (FPlatformTime::Seconds() - FrameStart) * 1000.0;
	S.SpawnMs += FrameMs;
	S.LastFrameMs = FrameMs + CarriedMs;
	if (S.LastFrameMs > S.MaxFrameMs)
	{
		S.MaxFrameMs = S.LastFrameMs;
		S.MaxFrame = S.Frames;
		S.MaxFrameObjects = S.Next - FirstObject;
	}
	++S.Frames;
	if (S.Next < S.Objects.Num())
	{
		return true;
	}
	FinishSession();
	return false;
}

void UCodexLevelPropsSubsystem::FinishSession()
{
	FPropsSpawnSession& S = *Session;
	const double LinkStart = FPlatformTime::Seconds();
	// In case crate was spawned after the trap/loot attachment was processed
	for (const auto& Pair : S.AttachedTraps)
	{
		if (ACodexPropCrate* Crate = S.CratesById.FindRef(Pair.Key).Get())
		{
			UCodexPropTrapComponent* Trap = Crate->EnsureTrapComponent();
			if (Trap)
			{
				if (const float* BlastR = VariantBlastRadius.Find(TEXT("mine")))
				{
					Trap->BlastRadiusCm = *BlastR;
				}
				if (const float* BlastD = VariantBlastDamage.Find(TEXT("mine")))
				{
					Trap->BlastDamage = *BlastD;
				}
				Trap->Arm(Pair.Value.Object.Id, AreMinesInert());
			}
		}
	}
	for (const auto& Pair : S.AttachedLoot)
	{
		if (ACodexPropCrate* Crate = S.CratesById.FindRef(Pair.Key).Get())
		{
			Crate->LootTable = Pair.Value.Object.LootTable;
		}
	}
	// TURRET_POWER.md phase 1: each generator links the turrets on its cable; their power and side follow its state from now on.
	int32 LinkedTurrets = 0;
	for (const TPair<FString, TArray<FString>>& Pair : S.GeneratorLinks)
	{
		APowerGeneratorActor* Generator = S.GeneratorsById.FindRef(Pair.Key).Get();
		for (const FString& TurretId : Pair.Value)
		{
			ATurretActor* Turret = S.TurretsById.FindRef(TurretId).Get();
			LinkedTurrets += Generator && Turret && Generator->LinkTurret(Turret) ? 1 : 0;
		}
	}
	// The attach / link pass runs on the last spawn frame: it counts into that frame.
	const double LinkMs = (FPlatformTime::Seconds() - LinkStart) * 1000.0;
	S.SpawnMs += LinkMs;
	if (S.LastFrameMs + LinkMs > S.MaxFrameMs)
	{
		S.MaxFrameMs = S.LastFrameMs + LinkMs;
		S.MaxFrame = S.Frames - 1;
	}
	if (S.GeneratorsById.Num() > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Props] power: %d generator(s), %d turret(s) linked, %d warning(s)"), S.GeneratorsById.Num(),
			LinkedTurrets, S.PowerWarnings);
	}
	FString Costs;
	for (const TPair<FString, FPropsSpawnSession::FKindCost>& Pair : S.CostByKind)
	{
		Costs += FString::Printf(TEXT("%s %d x %.2f ms (max %.2f %s); "), *Pair.Key, Pair.Value.Count, Pair.Value.TotalMs / FMath::Max(Pair.Value.Count, 1),
			Pair.Value.MaxMs, *Pair.Value.MaxId);
	}
	UE_LOG(LogCodexTactics, Display,
		TEXT("[Props] spawn cost: max frame #%d (%d objects), slowest object %.2f ms (%s), link pass %.2f ms, prepare %s %.2f ms (parse %.2f / layout + validate %.2f; %d frame(s) waited), game-thread setup %.2f ms; %s"),
		S.MaxFrame, S.MaxFrameObjects, S.SlowestObjectMs, *S.SlowestObject, LinkMs, S.PrepareTask.IsValid() ? TEXT("on a worker thread") : TEXT("on the game thread"),
		S.PrepareMs, S.PrepParseMs, S.PrepLayoutMs, S.WaitFrames, S.GameThreadSetupMs, *Costs);

	LastPrepareMs = S.PrepareMs;
	LastMaxFrameMs = S.MaxFrameMs;
	LastSpawnFrames = S.Frames;
	// Wall time of the spawn work: the game-thread set-up + spawn frames, plus the prepare when it ran on a worker thread (a direct spawn's
	// prepare is inside its set-up already).
	LastSpawnDurationMs = S.GameThreadSetupMs + S.SpawnMs + (S.PrepareTask.IsValid() ? S.PrepareMs : 0.0);
	bHasSpawned = true;

	UE_LOG(LogCodexTactics, Display,
		TEXT("[Props] spawned %d props (%d game objects, greybox: %d, powered turrets: %d) in %.2f ms over %d frame(s), max frame %.2f ms (prepare %.2f ms) (dropped: %d, layout: %s, real: %s) from %s"),
		GetSpawnedCount(), GetSpawnedGameplayCount(), GreyboxCount, S.PoweredTurrets, LastSpawnDurationMs, LastSpawnFrames, LastMaxFrameMs, LastPrepareMs,
		DroppedPropIds.Num(), S.bHasLayout ? TEXT("yes") : TEXT("NO - flat Z"), S.bGameplay ? TEXT("yes") : TEXT("no"), *S.PropsFilePath);

	constexpr double WarnBudgetMs = 60.0;
	if (LastMaxFrameMs > WarnBudgetMs)
	{
		UE_LOG(LogCodexTactics, Warning,
			TEXT("[Props] spawn frame %.2f ms exceeded performance budget %.0f ms (PROPS_LAYERS.md 3.3)"),
			LastMaxFrameMs, WarnBudgetMs);
	}
	Session.Reset();
	// G003b: smokes / AI wait for the frame-sliced spawn to end.
	OnPropsSpawned.Broadcast(this);
	OnPropsSpawnedDynamic.Broadcast(GetSpawnedCount());
}

void UCodexLevelPropsSubsystem::TagProp(AActor* Actor, const FString& LayerId, const FString& PropId)
{
	if (!Actor)
	{
		return;
	}
	Actor->Tags.AddUnique(FName(TEXT("CodexProp")));
	if (!LayerId.IsEmpty())
	{
		Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("prop_layer=%s"), *LayerId)));
	}
	if (!PropId.IsEmpty())
	{
		Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("prop_id=%s"), *PropId)));
	}
}

bool UCodexLevelPropsSubsystem::SpawnGameplayActor(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj, const FVector& Ground,
	const FRotator& Rotation)
{
	const FLevelPropsObject& Obj = ResObj.Object;
	const FString Kind = S.KindOfLayer.FindRef(ResObj.LayerId);
	const bool bMined = S.AttachedTraps.Contains(Obj.Id);
	const FLevelPropsResolvedObject* Loot = S.AttachedLoot.Find(Obj.Id);
	const bool bCrate = Obj.Variant.StartsWith(TEXT("crate"));
	const FString Key = PropsGameplayRules::ClassKeyFor(Kind, Obj, bCrate && (bMined || Loot != nullptr));
	if (Key.StartsWith(TEXT("mine")) && AreMinesInert())
	{
		return false; // Codex.Props.MinesInert: the inert greybox mine instead of a live one
	}
	const FString Path = PropsGameplayRules::ClassPathFor(BalanceRoot.Get(), Key);
	if (Path.IsEmpty())
	{
		return false;
	}
	UClass* Class = nullptr;
	const FString CacheKey = TEXT("gameplay:") + Key;
	if (const TSubclassOf<AActor>* Cached = ResolvedClassCache.Find(CacheKey))
	{
		Class = *Cached;
	}
	else
	{
		Class = FSoftClassPath(Path).TryLoadClass<AActor>();
		ResolvedClassCache.Add(CacheKey, Class);
		if (!Class)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Props] gameplay class '%s' for '%s' not found: greybox instead"), *Path, *Key);
		}
	}
	if (!Class || Class->IsChildOf(ACodexPropActor::StaticClass()))
	{
		return false;
	}
	UWorld* World = GetWorld();
	const float Reveal = PropsGameplayRules::RevealCm(BalanceRoot.Get());

	// A loose pile: the game's ground pile (ADroppedItemActor) with the rolled items.
	if (Class->IsChildOf(ADroppedItemActor::StaticClass()))
	{
		const TMap<FString, int32> Items = PropsGameplayRules::RollLoot(BalanceRoot.Get(), Obj.LootTable, Obj.Id, /*bPile=*/ true);
		const UEnum* ItemEnum = StaticEnum<ETransferItem>();
		TArray<TPair<ETransferItem, int32>> Contents;
		for (const TPair<FString, int32>& Item : Items)
		{
			int64 Value = ItemEnum ? ItemEnum->GetValueByNameString(Item.Key) : INDEX_NONE;
			if (Value == INDEX_NONE && ItemEnum)
			{
				Value = ItemEnum->GetValueByNameString(TEXT("ETransferItem::") + Item.Key);
			}
			if (Value == INDEX_NONE)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[Props] loot item '%s' is not a ground item (ETransferItem)"), *Item.Key);
				continue;
			}
			Contents.Emplace(static_cast<ETransferItem>(Value), Item.Value);
		}
		// A fresh pile of its own (never merged into a pile lying there, e.g. a player's drop): ClearSpawnedProps removes only this one.
		ADroppedItemActor* Pile = ADroppedItemActor::SpawnFresh(World, Ground, Contents);
		if (!Pile)
		{
			return false;
		}
		TagProp(Pile, ResObj.LayerId, Obj.Id);
		SpawnedGameplay.AddUnique(Pile);
		GameplayByLayer.FindOrAdd(ResObj.LayerId).AddUnique(Pile);
		S.ForTrace.AddUnique(Pile);
		return true;
	}

	FTransform Transform(Rotation, Ground);
	AActor* Actor = World->SpawnActorDeferred<AActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Actor)
	{
		return false;
	}
	if (const AInteractableActor* Interactable = Cast<AInteractableActor>(Actor))
	{
		// An interactable stands on its box's centre: lift the box onto the slab.
		if (Interactable->Box)
		{
			Transform.AddToTranslation(FVector(0.0, 0.0, Interactable->Box->GetScaledBoxExtent().Z));
		}
	}
	if (ALootCrateActor* Crate = Cast<ALootCrateActor>(Actor))
	{
		const FString Table = Loot ? Loot->Object.LootTable : PropsGameplayRules::MinedCrateTable(BalanceRoot.Get());
		const TArray<FString> Unknown = PropsGameplayRules::ApplyToContents(
			PropsGameplayRules::RollLoot(BalanceRoot.Get(), Table, Obj.Id, /*bPile=*/ false), Crate->Contents);
		for (const FString& Field : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Props] loot field '%s' is not in FLootContents"), *Field);
		}
		// Codex.Props.MinesInert: a mined crate is a safe crate (as the inert greybox trap).
		Crate->bTrapped = bMined && !AreMinesInert();
	}
	else if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Actor))
	{
		Mine->DetectionRange = Reveal;
		// gameplay.traps_faction: a hostile level mine (hidden, trips on operatives) or the squad's own (made visible after the spawn).
		Mine->bPlacedBySquad = !S.bTrapsHostile;
	}
	else if (ATurretActor* Turret = Cast<ATurretActor>(Actor))
	{
		PropsGameplayRules::FTurretSetup Setup;
		if (const PropsGameplayRules::FTurretSetup* Found = S.TurretSetups.Find(Obj.Id))
		{
			Setup = *Found;
		}
		const ETurretFaction Faction = Setup.Side == PropsGameplayRules::ETurretSide::Friendly ? ETurretFaction::Friendly
			: (Setup.Side == PropsGameplayRules::ETurretSide::Neutral ? ETurretFaction::Neutral : ETurretFaction::Hostile);
		Turret->InitLevelTurret(Faction, Setup.bPowered);
		S.PoweredTurrets += Setup.bPowered ? 1 : 0;
		S.TurretsById.Add(Obj.Id, Turret);
	}
	else if (APowerGeneratorActor* Generator = Cast<APowerGeneratorActor>(Actor))
	{
		// A power layer generator: its start state now, its cable links once every turret is spawned (FinishSession).
		const PropsGameplayRules::FGeneratorInput Input = PropsGameplayRules::GeneratorInputOf(Obj);
		Generator->bAutoLinkTurrets = false;
		Generator->InitLevelGenerator(Input.Start, Input.FuelL);
		S.GeneratorsById.Add(Obj.Id, Generator);
		S.GeneratorLinks.Add(Obj.Id, Input.Links);
	}
	else if (ATripwireActor* Wire = Cast<ATripwireActor>(Actor))
	{
		float WidthCm = 180.f;
		if (Obj.Extra.Contains(TEXT("width_cm")) && Obj.Extra[TEXT("width_cm")]->Type == EJson::Number)
		{
			WidthCm = static_cast<float>(Obj.Extra[TEXT("width_cm")]->AsNumber());
		}
		// Across the doorway along the object's y (as the props tripwire box, ACodexPropMine::SetupTrigger). Each peg stops 10 cm short of
		// the jamb: a peg inside the wall makes the wire's ground trace start in it (G003b: 5 X6 wires hung at 90 cm).
		const FVector Right = Rotation.RotateVector(FVector::RightVector);
		FCollisionQueryParams JambParams(SCENE_QUERY_STAT(PropsTripwireJamb), false);
		const auto HalfSpan = [&](float Sign)
		{
			const FVector From = Ground + FVector(0.0, 0.0, 50.0);
			FHitResult Hit;
			float Half = WidthCm * 0.5f;
			if (World->LineTraceSingleByChannel(Hit, From, From + Right * (Sign * (Half + 10.f)), ECC_WorldStatic, JambParams) && !Hit.bStartPenetrating)
			{
				Half = FMath::Min(Half, static_cast<float>(Hit.Distance) - 10.f);
			}
			return FMath::Max(Half, 50.f);
		};
		Wire->AnchorA = FVector(0.f, -HalfSpan(-1.f), 0.f);
		Wire->AnchorB = FVector(0.f, HalfSpan(1.f), 0.f);
		Wire->SpotRangeCm = Reveal;
		// gameplay.traps_faction: the PMC's hidden wire (trips on operatives) or the squad's own; laid and armed at level start either way.
		Wire->Faction = S.bTrapsHostile ? ETripwireFaction::PMC : ETripwireFaction::Squad;
		Wire->bArmedAtLevelStart = true;
	}
	Actor->FinishSpawning(Transform);
	if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Actor); Mine && !S.bTrapsHostile)
	{
		Mine->RestoreSaved(/*bInPlacedBySquad=*/ true, /*bInTrapped=*/ true, /*bInRevealed=*/ true, /*InArmingTimeLeft=*/ 0.f);
	}
	TagProp(Actor, ResObj.LayerId, Obj.Id);
	// The crate's mine / loot objects are this actor (the TG shows them as markers on it).
	if (bCrate && bMined)
	{
		const FLevelPropsResolvedObject& Trap = S.AttachedTraps[Obj.Id];
		TagProp(Actor, Trap.LayerId, Trap.Object.Id);
	}
	if (bCrate && Loot)
	{
		TagProp(Actor, Loot->LayerId, Loot->Object.Id);
	}
	SpawnedGameplay.Add(Actor);
	GameplayByLayer.FindOrAdd(ResObj.LayerId).Add(Actor);
	S.ForTrace.Add(Actor);
	return true;
}

void UCodexLevelPropsSubsystem::SpawnOne(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj)
{
	UWorld* World = GetWorld();
	const FLevelPropsObject& Obj = ResObj.Object;

	// Check save state / skipped IDs
	if (S.Skipped.Contains(Obj.Id))
	{
		return;
	}

	// Objects with empty class_map path (e.g. mine_crate, loot_crate) do not spawn an actor
	if (VariantClassMap.Contains(Obj.Variant) && VariantClassMap[Obj.Variant].IsEmpty())
	{
		return;
	}

	// Calculate location
	float BaseZ = 0.f;
	if (const int32* LayerZ = S.LayerZ.Find(Obj.WalkLayer))
	{
		BaseZ = static_cast<float>(*LayerZ);
	}
	const float ComputedZ = BaseZ + static_cast<float>(ResObj.ZOffsetCm);

	// Downward trace +-30 cm to snap to slab (PROPS_LAYERS.md 3.1); the props spawned before are ignored
	float FinalZ = ComputedZ;
	const FVector TraceStart(Obj.Pos.X, Obj.Pos.Y, ComputedZ + 30.f);
	const FVector TraceEnd(Obj.Pos.X, Obj.Pos.Y, ComputedZ - 30.f);
	FHitResult HitResult;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PropsSnapZ), false);
	for (const TWeakObjectPtr<AActor>& Spawned : S.ForTrace)
	{
		if (const AActor* Ignored = Spawned.Get())
		{
			TraceParams.AddIgnoredActor(Ignored);
		}
	}
	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
	{
		FinalZ = HitResult.ImpactPoint.Z;
	}
	else
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Props] snap Z trace missed for %s at (%d, %d, %.1f), using computed Z"),
			*Obj.Id, Obj.Pos.X, Obj.Pos.Y, ComputedZ);
	}

	const FVector SpawnLocation(Obj.Pos.X, Obj.Pos.Y, FinalZ);
	const FRotator SpawnRotation(0.f, static_cast<float>(Obj.Yaw), 0.f);

	if (S.bGameplay && SpawnGameplayActor(S, ResObj, SpawnLocation, SpawnRotation))
	{
		return;
	}

	UClass* PropClass = ResolvePropClass(Obj.Variant);
	if (!PropClass || !PropClass->IsChildOf(ACodexPropActor::StaticClass()))
	{
		PropClass = ACodexPropActor::StaticClass();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACodexPropActor* PropActor = World->SpawnActor<ACodexPropActor>(PropClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (!PropActor)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Props] failed to spawn actor for prop %s (%s)"), *Obj.Id, *Obj.Variant);
		return;
	}

	const bool bNav = VariantNavAffecting.FindRef(Obj.Variant);
	if (S.bGreybox && !CodexLevelPropsInternal::ActorHasVisualMesh(PropActor))
	{
		float WidthCm = 180.f;
		if (Obj.Extra.Contains(TEXT("width_cm")) && Obj.Extra[TEXT("width_cm")]->Type == EJson::Number)
		{
			WidthCm = static_cast<float>(Obj.Extra[TEXT("width_cm")]->AsNumber());
		}
		const bool bDangerous = S.AttachedTraps.Contains(Obj.Id);
		PropActor->ApplyGreybox(PropsGreybox::Resolve(BalanceRoot.Get(), Obj.Variant, bDangerous, Obj.Trigger, WidthCm));
		GreyboxCount += CodexLevelPropsInternal::ActorHasVisualMesh(PropActor) ? 1 : 0;
	}
	if (S.bRequireMesh && bNav && !CodexLevelPropsInternal::ActorHasVisualMesh(PropActor))
	{
		PropActor->Destroy();
		return;
	}

	const FVector Extent = VariantExtents.FindRef(Obj.Variant);

	PropActor->InitProp(Obj.Id, ResObj.LayerId, Obj.Variant, Obj.WalkLayer, bNav, Extent);

	// Apply balance properties from props_balance.json if present
	if (const float* HPVal = VariantHP.Find(Obj.Variant))
	{
		if (ACodexPropCrate* Crate = Cast<ACodexPropCrate>(PropActor))
		{
			Crate->HP = *HPVal;
		}
		else if (ACodexPropBarrel* Barrel = Cast<ACodexPropBarrel>(PropActor))
		{
			Barrel->HP = *HPVal;
		}
		else if (ACodexPropTurret* Turret = Cast<ACodexPropTurret>(PropActor))
		{
			Turret->HP = *HPVal;
		}
	}

	// Specialize actor based on subclass
	if (ACodexPropCrate* Crate = Cast<ACodexPropCrate>(PropActor))
	{
		S.CratesById.Add(Obj.Id, Crate);

		// Attach trap if this crate is mined
		if (const FLevelPropsResolvedObject* TrapObj = S.AttachedTraps.Find(Obj.Id))
		{
			UCodexPropTrapComponent* Trap = Crate->EnsureTrapComponent();
			if (Trap)
			{
				if (const float* BlastR = VariantBlastRadius.Find(TEXT("mine")))
				{
					Trap->BlastRadiusCm = *BlastR;
				}
				if (const float* BlastD = VariantBlastDamage.Find(TEXT("mine")))
				{
					Trap->BlastDamage = *BlastD;
				}
				Trap->Arm(TrapObj->Object.Id, AreMinesInert());
			}
		}

		// Attach loot if this crate holds loot
		if (const FLevelPropsResolvedObject* LootObj = S.AttachedLoot.Find(Obj.Id))
		{
			Crate->LootTable = LootObj->Object.LootTable;
		}
	}
	else if (ACodexPropBarrel* Barrel = Cast<ACodexPropBarrel>(PropActor))
	{
		if (Obj.Variant.Equals(TEXT("barrel_explosive"), ESearchCase::IgnoreCase))
		{
			Barrel->bExplosive = true;
			if (const float* ExpR = VariantExplosionRadius.Find(Obj.Variant))
			{
				Barrel->ExplosionRadiusCm = *ExpR;
			}
			if (const float* ExpD = VariantExplosionDamage.Find(Obj.Variant))
			{
				Barrel->ExplosionDamage = *ExpD;
			}
		}
	}
	else if (ACodexPropTurret* Turret = Cast<ACodexPropTurret>(PropActor))
	{
		if (Obj.Arc.IsSet())
		{
			Turret->ArcDeg = static_cast<float>(Obj.Arc.GetValue());
		}
		if (Obj.Extra.Contains(TEXT("covered_m")) && Obj.Extra[TEXT("covered_m")]->Type == EJson::Number)
		{
			Turret->CoveredM = static_cast<int32>(Obj.Extra[TEXT("covered_m")]->AsNumber());
		}
	}
	else if (ACodexPropMine* Mine = Cast<ACodexPropMine>(PropActor))
	{
		Mine->AttachedDoorId = Obj.Door;
		Mine->bMinesInert = AreMinesInert();
		if (const float* BlastR = VariantBlastRadius.Find(TEXT("mine")))
		{
			Mine->BlastRadiusCm = *BlastR;
		}
		if (const float* BlastD = VariantBlastDamage.Find(TEXT("mine")))
		{
			Mine->BlastDamage = *BlastD;
		}
		float Width = 180.f;
		if (Obj.Extra.Contains(TEXT("width_cm")) && Obj.Extra[TEXT("width_cm")]->Type == EJson::Number)
		{
			Width = static_cast<float>(Obj.Extra[TEXT("width_cm")]->AsNumber());
		}
		float TriggerRadius = 120.f;
		if (const float* TrigR = VariantTriggerRadius.Find(TEXT("mine")))
		{
			TriggerRadius = *TrigR;
		}
		Mine->SetupTrigger(Obj.Trigger, Width, TriggerRadius);
	}
	else if (ACodexPropLootPile* LootPile = Cast<ACodexPropLootPile>(PropActor))
	{
		LootPile->LootTable = Obj.LootTable;
	}

	SpawnedProps.Add(PropActor);
	S.ForTrace.Add(PropActor);
	PropsByLayer.FindOrAdd(ResObj.LayerId).Add(PropActor);
}

void UCodexLevelPropsSubsystem::ClearSpawnedProps()
{
	StopSpawnTicker();
	for (TWeakObjectPtr<ACodexPropActor>& PropPtr : SpawnedProps)
	{
		if (ACodexPropActor* Prop = PropPtr.Get())
		{
			Prop->Destroy();
		}
	}
	for (TWeakObjectPtr<AActor>& ActorPtr : SpawnedGameplay)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actor->Destroy();
		}
	}
	SpawnedProps.Reset();
	SpawnedGameplay.Reset();
	GameplayByLayer.Reset();
	PropsByLayer.Reset();
	DroppedPropIds.Reset();
	ResolvedClassCache.Reset();
	bHasSpawned = false;
	LastSpawnDurationMs = 0.0;
	GreyboxCount = 0;
}

void UCodexLevelPropsSubsystem::DumpProps() const
{
	UE_LOG(LogCodexTactics, Display, TEXT("=== Codex Level Props Dump ==="));
	UE_LOG(LogCodexTactics, Display, TEXT("Total spawned props: %d (last spawn: %.2f ms, greybox: %d)"),
		GetSpawnedCount(), LastSpawnDurationMs, GreyboxCount);

	for (const auto& Pair : PropsByLayer)
	{
		int32 AliveInLayer = 0;
		for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Pair.Value)
		{
			if (PropPtr.IsValid())
			{
				AliveInLayer++;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("  Layer '%s': %d props"), *Pair.Key, AliveInLayer);
	}
	for (const auto& Pair : GameplayByLayer)
	{
		int32 Alive = 0;
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pair.Value)
		{
			Alive += ActorPtr.IsValid() ? 1 : 0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("  Layer '%s': %d game objects"), *Pair.Key, Alive);
	}

	UE_LOG(LogCodexTactics, Display, TEXT("Dropped IDs (%d):"), DroppedPropIds.Num());
	for (const FString& DroppedId : DroppedPropIds)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("  - %s"), *DroppedId);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("=============================="));
}

int32 UCodexLevelPropsSubsystem::GetSpawnedCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : SpawnedProps)
	{
		if (PropPtr.IsValid())
		{
			Count++;
		}
	}
	return Count + GetSpawnedGameplayCount();
}

int32 UCodexLevelPropsSubsystem::GetSpawnedGameplayCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : SpawnedGameplay)
	{
		Count += ActorPtr.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 UCodexLevelPropsSubsystem::GetSpawnedCountForLayer(const FString& LayerId) const
{
	int32 Count = 0;
	if (const TArray<TWeakObjectPtr<ACodexPropActor>>* LayerList = PropsByLayer.Find(LayerId))
	{
		for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : *LayerList)
		{
			Count += PropPtr.IsValid() ? 1 : 0;
		}
	}
	if (const TArray<TWeakObjectPtr<AActor>>* GameplayList = GameplayByLayer.Find(LayerId))
	{
		for (const TWeakObjectPtr<AActor>& ActorPtr : *GameplayList)
		{
			Count += ActorPtr.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

UClass* UCodexLevelPropsSubsystem::ResolvePropClass(const FString& Variant)
{
	if (const TSubclassOf<AActor>* CachedClass = ResolvedClassCache.Find(Variant))
	{
		return *CachedClass;
	}

	UClass* ResultClass = nullptr;

	// 1. Check if class_map provided a soft class path
	if (const FString* SoftPath = VariantClassMap.Find(Variant))
	{
		if (SoftPath->IsEmpty())
		{
			ResolvedClassCache.Add(Variant, nullptr);
			return nullptr; // Explicitly no actor
		}

		const FSoftClassPath SoftClassPath(*SoftPath);
		// Check if package exists before TryLoadClass to avoid engine warning spam
		if (SoftClassPath.IsValid() && FPackageName::DoesPackageExist(SoftClassPath.GetLongPackageName()))
		{
			ResultClass = SoftClassPath.TryLoadClass<AActor>();
		}
	}

	// 2. Validate IsChildOf or fallback
	if (ResultClass && !ResultClass->IsChildOf(ACodexPropActor::StaticClass()))
	{
		UE_LOG(LogCodexTactics, Warning,
			TEXT("[Props] Class '%s' for variant '%s' is not child of ACodexPropActor, falling back"),
			*ResultClass->GetName(), *Variant);
		ResultClass = nullptr;
	}

	// 3. Native C++ fallback classes
	if (!ResultClass)
	{
		if (Variant.StartsWith(TEXT("barrel")))
		{
			ResultClass = ACodexPropBarrel::StaticClass();
		}
		else if (Variant.StartsWith(TEXT("crate")))
		{
			ResultClass = ACodexPropCrate::StaticClass();
		}
		else if (Variant.StartsWith(TEXT("turret")))
		{
			ResultClass = ACodexPropTurret::StaticClass();
		}
		else if (Variant.StartsWith(TEXT("mine")))
		{
			ResultClass = ACodexPropMine::StaticClass();
		}
		else if (Variant.StartsWith(TEXT("loot")))
		{
			ResultClass = ACodexPropLootPile::StaticClass();
		}
		else
		{
			ResultClass = ACodexPropActor::StaticClass();
		}
	}

	ResolvedClassCache.Add(Variant, ResultClass);
	return ResultClass;
}

void UCodexLevelPropsSubsystem::LoadBalanceFile()
{
	if (bBalanceLoaded)
	{
		return;
	}

	const FString BalancePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/Balance/props_balance.json"));

	FString BalanceJsonString;
	if (!FFileHelper::LoadFileToString(BalanceJsonString, *BalancePath))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BalanceJsonString), Root) || !Root.IsValid())
	{
		return;
	}
	BalanceRoot = Root;

	// Parse class_map
	const TSharedPtr<FJsonObject>* ClassMapObj = nullptr;
	if (Root->TryGetObjectField(TEXT("class_map"), ClassMapObj) && ClassMapObj && ClassMapObj->IsValid())
	{
		for (const auto& Entry : (*ClassMapObj)->Values)
		{
			const FString Key(*Entry.Key);
			if (Key.Contains(TEXT("comment")))
			{
				continue;
			}
			FString Path;
			if (Entry.Value->TryGetString(Path))
			{
				VariantClassMap.Add(Key, Path);
			}
		}
	}

	// Parse kinds
	const TSharedPtr<FJsonObject>* KindsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("kinds"), KindsObj) && KindsObj && KindsObj->IsValid())
	{
		for (const auto& Entry : (*KindsObj)->Values)
		{
			const FString VariantName(*Entry.Key);
			const TSharedPtr<FJsonObject> KindData = Entry.Value->AsObject();
			if (!KindData.IsValid())
			{
				continue;
			}

			bool bNavAffecting = false;
			KindData->TryGetBoolField(TEXT("nav_affecting"), bNavAffecting);
			VariantNavAffecting.Add(VariantName, bNavAffecting);

			// Extents
			FVector Extent(50.f, 50.f, 50.f);
			if (KindData->HasField(TEXT("size_cm")))
			{
				const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
				if (KindData->TryGetArrayField(TEXT("size_cm"), SizeArray) && SizeArray && SizeArray->Num() == 3)
				{
					Extent = FVector(
						(*SizeArray)[0]->AsNumber() * 0.5f,
						(*SizeArray)[1]->AsNumber() * 0.5f,
						(*SizeArray)[2]->AsNumber() * 0.5f);
				}
			}
			else
			{
				float Radius = 50.f;
				float Height = 100.f;
				if (KindData->HasField(TEXT("radius_cm")))
				{
					Radius = static_cast<float>(KindData->GetNumberField(TEXT("radius_cm")));
				}
				if (KindData->HasField(TEXT("height_cm")))
				{
					Height = static_cast<float>(KindData->GetNumberField(TEXT("height_cm")));
				}
				Extent = FVector(Radius, Radius, Height * 0.5f);
			}
			VariantExtents.Add(VariantName, Extent);

			// Stats from kinds in props_balance.json
			double ValDouble = 0.0;
			if (KindData->TryGetNumberField(TEXT("hp"), ValDouble))
			{
				VariantHP.Add(VariantName, static_cast<float>(ValDouble));
			}
			if (KindData->TryGetNumberField(TEXT("explosion_radius_cm"), ValDouble))
			{
				VariantExplosionRadius.Add(VariantName, static_cast<float>(ValDouble));
			}
			if (KindData->TryGetNumberField(TEXT("explosion_damage"), ValDouble))
			{
				VariantExplosionDamage.Add(VariantName, static_cast<float>(ValDouble));
			}
			if (KindData->TryGetNumberField(TEXT("blast_radius_cm"), ValDouble))
			{
				VariantBlastRadius.Add(VariantName, static_cast<float>(ValDouble));
			}
			if (KindData->TryGetNumberField(TEXT("blast_damage"), ValDouble))
			{
				VariantBlastDamage.Add(VariantName, static_cast<float>(ValDouble));
			}
			if (KindData->TryGetNumberField(TEXT("trigger_radius_cm"), ValDouble))
			{
				VariantTriggerRadius.Add(VariantName, static_cast<float>(ValDouble));
			}
		}
	}

	bBalanceLoaded = true;
}

FString UCodexLevelPropsSubsystem::ResolveLayoutPath(const FString& LayoutName, const FString& PropsDir)
{
	if (LayoutName.IsEmpty())
	{
		return FString();
	}

	// 1. Content/Data/LevelGen/accepted/<LayoutName>.json
	const FString AcceptedPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted") / (LayoutName + TEXT(".json")));
	if (FPaths::FileExists(AcceptedPath))
	{
		return AcceptedPath;
	}

	// 2. Relative to props directory: <LayoutName>.layout.json
	const FString FixtureLayoutPath = FPaths::ConvertRelativePathToFull(PropsDir / (LayoutName + TEXT(".layout.json")));
	if (FPaths::FileExists(FixtureLayoutPath))
	{
		return FixtureLayoutPath;
	}

	// 3. Relative to props directory: <LayoutName>.json
	const FString FixturePath = FPaths::ConvertRelativePathToFull(PropsDir / (LayoutName + TEXT(".json")));
	if (FPaths::FileExists(FixturePath))
	{
		return FixturePath;
	}

	// 4. Content/Data/LevelGen/props/fixtures/<LayoutName>.layout.json
	const FString FixturesDirLayout = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/LevelGen/props/fixtures") / (LayoutName + TEXT(".layout.json")));
	if (FPaths::FileExists(FixturesDirLayout))
	{
		return FixturesDirLayout;
	}

	return AcceptedPath;
}
