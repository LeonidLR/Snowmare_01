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

	static bool ClassHasVisualMesh(const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}
		const AActor* CDO = Cast<AActor>(Class->GetDefaultObject());
		return ActorHasVisualMesh(CDO);
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
	SpawnPropsForMap(MapName, nullptr, /*bRequireMesh=*/ true);
}

void UCodexLevelPropsSubsystem::Deinitialize()
{
	ClearSpawnedProps();
	Super::Deinitialize();
}

int32 UCodexLevelPropsSubsystem::SpawnPropsForMap(
	const FString& MapName,
	const TSet<FString>* SkippedIds,
	bool bRequireMesh)
{
	const FString PropsPath = GetDefaultPropsPathForMap(MapName);
	if (!FPaths::FileExists(PropsPath))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Props] no props file found for map '%s' at %s"), *MapName, *PropsPath);
		return 0;
	}

	return SpawnPropsFromFile(PropsPath, FString(), SkippedIds, bRequireMesh);
}

int32 UCodexLevelPropsSubsystem::SpawnPropsFromFile(
	const FString& PropsFilePath,
	const FString& LayoutFilePath,
	const TSet<FString>* SkippedIds,
	bool bRequireMesh)
{
	if (!IsPropsEnabled())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Props] spawning skipped (Codex.Props.Enabled 0)"));
		return 0;
	}

	ClearSpawnedProps();

	const double StartTime = FPlatformTime::Seconds();

	FString PropsJson;
	if (!FFileHelper::LoadFileToString(PropsJson, *PropsFilePath))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[Props] failed to read props file: %s"), *PropsFilePath);
		return 0;
	}

	FLevelPropsFile File;
	TArray<FString> ParseErrors;
	if (!LevelPropsFile::Parse(PropsJson, File, ParseErrors))
	{
		for (const FString& Err : ParseErrors)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[Props] parse error in %s: %s"), *PropsFilePath, *Err);
		}
		return 0;
	}

	TArray<FString> OverrideWarnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File, &OverrideWarnings);

	// Load balance configuration
	LoadBalanceFile();

	// Resolve layout and validate if layout exists
	const FString PropsDir = FPaths::GetPath(PropsFilePath);
	const FString ResolvedLayoutPath = LayoutFilePath.IsEmpty()
		? ResolveLayoutPath(File.Layout, PropsDir)
		: LayoutFilePath;

	TArray<FLevelPropsResolvedObject> ObjectsToSpawn;
	FPropsLayoutView LayoutView;
	bool bHasLayout = false;

	if (FPaths::FileExists(ResolvedLayoutPath))
	{
		FString LayoutJson;
		if (FFileHelper::LoadFileToString(LayoutJson, *ResolvedLayoutPath))
		{
			TArray<FString> LayoutErrors;
			FPropsLayoutConfig LayoutConfig;

			// Read config from balance file if available
			FString BalanceJsonString;
			const FString BalancePath = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("Data/Balance/props_balance.json"));
			TSharedPtr<FJsonObject> BalanceJsonRoot;
			if (FFileHelper::LoadFileToString(BalanceJsonString, *BalancePath))
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BalanceJsonString), BalanceJsonRoot);
				if (BalanceJsonRoot.IsValid())
				{
					FPropsLayoutView::ReadConfig(*BalanceJsonRoot, LayoutConfig, LayoutErrors);
				}
			}

			if (FPropsLayoutView::Build(LayoutJson, LayoutView, LayoutErrors, LayoutConfig))
			{
				bHasLayout = true;
				FPropsValidationResult ValResult;
				FPropsValidator::ValidateResolved(LayoutView, Resolved, ValResult, BalanceJsonRoot.Get());

				ObjectsToSpawn = MoveTemp(ValResult.ValidObjects);
				for (const FLevelPropsResolvedObject& Dropped : ValResult.DroppedObjects)
				{
					DroppedPropIds.Add(Dropped.Object.Id);
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

	if (!bHasLayout)
	{
		// No layout file available: spawn all resolved objects directly
		ObjectsToSpawn = Resolved.Objects;
	}

	// Collect crate attachments (mined crates and loot crates)
	TMap<FString, FLevelPropsResolvedObject> AttachedTraps;
	TMap<FString, FLevelPropsResolvedObject> AttachedLoot;
	for (const FLevelPropsResolvedObject& ResObj : ObjectsToSpawn)
	{
		if (!ResObj.Object.Attach.IsEmpty())
		{
			if (ResObj.Object.Variant.Equals(TEXT("mine_crate"), ESearchCase::IgnoreCase)
				|| ResObj.Object.Trigger.Equals(TEXT("crate_open"), ESearchCase::IgnoreCase))
			{
				AttachedTraps.Add(ResObj.Object.Attach, ResObj);
			}
			else if (ResObj.Object.Variant.Equals(TEXT("loot_crate"), ESearchCase::IgnoreCase)
				|| !ResObj.Object.LootTable.IsEmpty())
			{
				AttachedLoot.Add(ResObj.Object.Attach, ResObj);
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	TMap<FString, ACodexPropCrate*> SpawnedCratesById;

	for (const FLevelPropsResolvedObject& ResObj : ObjectsToSpawn)
	{
		const FLevelPropsObject& Obj = ResObj.Object;

		// Check save state / skipped IDs
		if (SkippedIds && SkippedIds->Contains(Obj.Id))
		{
			continue;
		}

		// Objects with empty class_map path (e.g. mine_crate, loot_crate) do not spawn an actor
		if (VariantClassMap.Contains(Obj.Variant) && VariantClassMap[Obj.Variant].IsEmpty())
		{
			continue;
		}

		// Calculate location
		float BaseZ = 0.f;
		if (bHasLayout)
		{
			if (const FPropsWalkLayer* Layer = LayoutView.FindLayer(Obj.WalkLayer))
			{
				BaseZ = static_cast<float>(Layer->ZCm);
			}
		}
		const float ComputedZ = BaseZ + static_cast<float>(ResObj.ZOffsetCm);

		// Downward trace +-30 cm to snap to slab (PROPS_LAYERS.md 3.1)
		float FinalZ = ComputedZ;
		const FVector TraceStart(Obj.Pos.X, Obj.Pos.Y, ComputedZ + 30.f);
		const FVector TraceEnd(Obj.Pos.X, Obj.Pos.Y, ComputedZ - 30.f);
		FHitResult HitResult;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PropsSnapZ), false);
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

		UClass* PropClass = ResolvePropClass(Obj.Variant);
		if (!PropClass || !PropClass->IsChildOf(ACodexPropActor::StaticClass()))
		{
			PropClass = ACodexPropActor::StaticClass();
		}

		if (bRequireMesh && !CodexLevelPropsInternal::ClassHasVisualMesh(PropClass))
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACodexPropActor* PropActor = World->SpawnActor<ACodexPropActor>(PropClass, SpawnLocation, SpawnRotation, SpawnParams);
		if (!PropActor)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Props] failed to spawn actor for prop %s (%s)"), *Obj.Id, *Obj.Variant);
			continue;
		}

		if (bRequireMesh && !CodexLevelPropsInternal::ActorHasVisualMesh(PropActor))
		{
			PropActor->Destroy();
			continue;
		}

		const bool bNav = VariantNavAffecting.FindRef(Obj.Variant);
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
			SpawnedCratesById.Add(Obj.Id, Crate);

			// Attach trap if this crate is mined
			if (AttachedTraps.Contains(Obj.Id))
			{
				const FLevelPropsResolvedObject& TrapObj = AttachedTraps[Obj.Id];
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
					Trap->Arm(TrapObj.Object.Id, AreMinesInert());
				}
			}

			// Attach loot if this crate holds loot
			if (AttachedLoot.Contains(Obj.Id))
			{
				const FLevelPropsResolvedObject& LootObj = AttachedLoot[Obj.Id];
				Crate->LootTable = LootObj.Object.LootTable;
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
		else if (ACodexPropLootPile* Loot = Cast<ACodexPropLootPile>(PropActor))
		{
			Loot->LootTable = Obj.LootTable;
		}

		SpawnedProps.Add(PropActor);
		PropsByLayer.FindOrAdd(ResObj.LayerId).Add(PropActor);
	}

	// In case crate was spawned after the trap/loot attachment was processed
	for (const auto& Pair : AttachedTraps)
	{
		if (ACodexPropCrate** CratePtr = SpawnedCratesById.Find(Pair.Key))
		{
			UCodexPropTrapComponent* Trap = (*CratePtr)->EnsureTrapComponent();
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
	for (const auto& Pair : AttachedLoot)
	{
		if (ACodexPropCrate** CratePtr = SpawnedCratesById.Find(Pair.Key))
		{
			(*CratePtr)->LootTable = Pair.Value.Object.LootTable;
		}
	}

	LastSpawnDurationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	bHasSpawned = true;

	UE_LOG(LogCodexTactics, Display, TEXT("[Props] spawned %d props in %.2f ms (dropped: %d) from %s"),
		SpawnedProps.Num(), LastSpawnDurationMs, DroppedPropIds.Num(), *PropsFilePath);

	constexpr double WarnBudgetMs = 60.0;
	if (LastSpawnDurationMs > WarnBudgetMs)
	{
		UE_LOG(LogCodexTactics, Warning,
			TEXT("[Props] spawn duration %.2f ms exceeded performance budget %.0f ms (PROPS_LAYERS.md 3.3)"),
			LastSpawnDurationMs, WarnBudgetMs);
	}

	return SpawnedProps.Num();
}

void UCodexLevelPropsSubsystem::ClearSpawnedProps()
{
	for (TWeakObjectPtr<ACodexPropActor>& PropPtr : SpawnedProps)
	{
		if (ACodexPropActor* Prop = PropPtr.Get())
		{
			Prop->Destroy();
		}
	}
	SpawnedProps.Reset();
	PropsByLayer.Reset();
	DroppedPropIds.Reset();
	ResolvedClassCache.Reset();
	bHasSpawned = false;
	LastSpawnDurationMs = 0.0;
}

void UCodexLevelPropsSubsystem::DumpProps() const
{
	UE_LOG(LogCodexTactics, Display, TEXT("=== Codex Level Props Dump ==="));
	UE_LOG(LogCodexTactics, Display, TEXT("Total spawned props: %d (last spawn: %.2f ms)"),
		GetSpawnedCount(), LastSpawnDurationMs);

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
	return Count;
}

int32 UCodexLevelPropsSubsystem::GetSpawnedCountForLayer(const FString& LayerId) const
{
	const TArray<TWeakObjectPtr<ACodexPropActor>>* LayerList = PropsByLayer.Find(LayerId);
	if (!LayerList)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : *LayerList)
	{
		if (PropPtr.IsValid())
		{
			Count++;
		}
	}
	return Count;
}

UClass* UCodexLevelPropsSubsystem::ResolvePropClass(const FString& Variant) const
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

FString UCodexLevelPropsSubsystem::ResolveLayoutPath(const FString& LayoutName, const FString& PropsDir) const
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
