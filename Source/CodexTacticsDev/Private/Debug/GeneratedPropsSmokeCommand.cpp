// Dev-only console commands (G003, designer 2026-11-17: "opened X4 and X6 - no barrels, no loot, nothing"), on the generated maps:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands GeneratedPropsSmoke,GeneratedPropsX6Smoke
// UCodexLevelPropsSubsystem spawned the map's Training Ground autogen props file (Content/Data/LevelGen/props/<Map>.props.json,
// source auto) at BeginPlay. Checks: the count per kind (crates / barrels / turrets / mines / loot; a mined crate counts as a mine,
// a loot crate as loot - they are the crate actor's trap / loot table) equals what the Training Ground shows (the file's resolved
// objects) within 5 %; every spawned prop has a visible mesh (the greybox look), lies inside the layout bounds and stands within
// 30 cm of the surface under it (ground / roof / floor / the cut's floor; props ignored by the trace); nothing more was dropped by the
// validator beyond the 5 % (a few genuinely invalid ones are dropped); the layout view was built (walk-layer Z). GeneratedPropsSmoke = L_Gen_Depot_X4, GeneratedPropsX6Smoke = L_Gen_Depot_1_X6;
// [map=<L_Gen_...>] for another generated map.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/CodexLevelPropsSubsystem.h"
#include "LevelGen/CodexPropActors.h"
#include "LevelGen/LevelPropsFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace GeneratedPropsSmoke
{
	constexpr float WarmupSeconds = 1.f;
	constexpr float TimeoutSeconds = 120.f;
	/** Count tolerance against the Training Ground's numbers (G003: within 5 %). */
	constexpr double CountTolerance = 0.05;
	/** A prop's origin may be this far from the surface under it (G003: within 30 cm of the ground / roof). */
	constexpr double SurfaceToleranceCm = 30.0;

	struct FState
	{
		bool bDone = false;
		float Time = 0.f;
		int32 Failures = 0;
		FString MapName;
		FString CommandName;
		SmokeUtils::FPreloadGate PreloadGate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		State.bDone = true;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, *State.CommandName);
		return false;
	}

	bool HasVisibleMesh(const ACodexPropActor* Prop)
	{
		if (!Prop || Prop->IsHidden())
		{
			return false;
		}
		TArray<UMeshComponent*> Meshes;
		Prop->GetComponents(Meshes);
		for (const UMeshComponent* Mesh : Meshes)
		{
			const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh);
			if (Static && Static->IsVisible() && Static->GetStaticMesh() && !Static->GetComponentScale().IsNearlyZero())
			{
				return true;
			}
		}
		return false;
	}

	/** The layout's bounds {min, max} (cm). */
	bool LayoutBounds(const FString& LayoutName, FVector2D& OutMin, FVector2D& OutMax)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *UCodexLevelPropsSubsystem::GetDefaultLayoutPathForMap(LayoutName))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Bounds = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Min = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Max = nullptr;
		if (!Root->TryGetObjectField(TEXT("bounds"), Bounds) || !(*Bounds)->TryGetArrayField(TEXT("min"), Min)
			|| !(*Bounds)->TryGetArrayField(TEXT("max"), Max) || Min->Num() < 2 || Max->Num() < 2)
		{
			return false;
		}
		OutMin = FVector2D((*Min)[0]->AsNumber(), (*Min)[1]->AsNumber());
		OutMax = FVector2D((*Max)[0]->AsNumber(), (*Max)[1]->AsNumber());
		return true;
	}

	void RunChecks(UWorld* World, FState& State)
	{
		UCodexLevelPropsSubsystem* Props = World->GetSubsystem<UCodexLevelPropsSubsystem>();
		if (!Props)
		{
			Check(State, false, TEXT("props subsystem exists"));
			return;
		}
		Props->DumpProps();

		// What the Training Ground shows: the file's resolved objects per layer kind.
		FString Json;
		FLevelPropsFile File;
		TArray<FString> Errors;
		const FString PropsPath = UCodexLevelPropsSubsystem::GetDefaultPropsPathForMap(State.MapName);
		if (!FFileHelper::LoadFileToString(Json, *PropsPath) || !LevelPropsFile::Parse(Json, File, Errors))
		{
			Check(State, false, FString::Printf(TEXT("props file %s reads"), *PropsPath));
			return;
		}
		const TSharedPtr<FJsonValue>* Source = File.Extra.Find(TEXT("source"));
		Check(State, Source && (*Source)->Type == EJson::String && (*Source)->AsString() == TEXT("auto"),
			TEXT("the props file is the Training Ground autogen output (source auto)"));
		TMap<FString, FString> KindOfLayer;
		for (const FLevelPropsLayer& Layer : File.Layers)
		{
			KindOfLayer.Add(Layer.Id, Layer.Kind);
		}
		const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File);
		TMap<FString, int32> Expected;
		for (const FLevelPropsResolvedObject& Obj : Resolved.Objects)
		{
			Expected.FindOrAdd(KindOfLayer.FindRef(Obj.LayerId))++;
		}

		FVector2D BoundsMin;
		FVector2D BoundsMax;
		const bool bBounds = LayoutBounds(File.Layout, BoundsMin, BoundsMax);
		Check(State, bBounds, FString::Printf(TEXT("layout %s bounds read"), *File.Layout));

		TMap<FString, int32> Spawned;
		TMap<FString, int32> Visible;
		int32 Outside = 0;
		int32 OffSurface = 0;
		int32 Invisible = 0;
		double WorstGapCm = 0.0;
		FString WorstId;
		TArray<AActor*> AllProps;
		for (const TWeakObjectPtr<ACodexPropActor>& Weak : Props->GetSpawnedProps())
		{
			if (Weak.IsValid())
			{
				AllProps.Add(Weak.Get());
			}
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GeneratedPropsSmoke), false);
		Params.AddIgnoredActors(AllProps);
		for (AActor* Actor : AllProps)
		{
			const ACodexPropActor* Prop = CastChecked<ACodexPropActor>(Actor);
			const FString Kind = KindOfLayer.FindRef(Prop->LayerId);
			const bool bVisible = HasVisibleMesh(Prop);
			Spawned.FindOrAdd(Kind)++;
			Visible.FindOrAdd(Kind) += bVisible ? 1 : 0;
			Invisible += bVisible ? 0 : 1;
			if (const ACodexPropCrate* Crate = Cast<ACodexPropCrate>(Prop))
			{
				// A mined crate / a loot crate: the mines / loot object the Training Ground shows is this crate's trap / loot table.
				if (Crate->TrapComponent && Crate->TrapComponent->bArmed)
				{
					Spawned.FindOrAdd(TEXT("mines"))++;
					Visible.FindOrAdd(TEXT("mines")) += bVisible ? 1 : 0;
				}
				if (!Crate->LootTable.IsEmpty())
				{
					Spawned.FindOrAdd(TEXT("loot"))++;
					Visible.FindOrAdd(TEXT("loot")) += bVisible ? 1 : 0;
				}
			}
			const FVector Location = Prop->GetActorLocation();
			if (bBounds && (Location.X < BoundsMin.X || Location.X > BoundsMax.X || Location.Y < BoundsMin.Y || Location.Y > BoundsMax.Y))
			{
				++Outside;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s outside the bounds at %s"), *Prop->PropId, *Location.ToCompactString());
			}
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 60.0), Location - FVector(0.0, 0.0, 400.0),
				ECC_WorldStatic, Params);
			const double Gap = bHit ? FMath::Abs(Location.Z - Hit.ImpactPoint.Z) : 1e9;
			if (Gap > SurfaceToleranceCm)
			{
				++OffSurface;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s (%s, %s) %s - surface %s"), *Prop->PropId, *Prop->Variant, *Prop->WalkLayer,
					*Location.ToCompactString(), bHit ? *FString::Printf(TEXT("at Z %.1f (%s)"), Hit.ImpactPoint.Z, *GetNameSafe(Hit.GetActor())) : TEXT("none"));
			}
			if (Gap < 1e8 && Gap > WorstGapCm)
			{
				WorstGapCm = Gap;
				WorstId = Prop->PropId;
			}
		}

		Check(State, Props->HasSpawned(), TEXT("props spawned at BeginPlay"));
		// The validator may drop a few genuinely invalid objects (e.g. a kept Depot crate overlapping a Depot cover); the per-kind 5 %
		// check below bounds them.
		Check(State, Props->GetDroppedIds().Num() <= CountTolerance * Resolved.Objects.Num(),
			FString::Printf(TEXT("validator dropped %d of %d (<= 5 %%): %s"), Props->GetDroppedIds().Num(), Resolved.Objects.Num(),
				*FString::Join(Props->GetDroppedIds(), TEXT(", "))));
		for (const TCHAR* Kind : { TEXT("crates"), TEXT("barrels"), TEXT("turrets"), TEXT("mines"), TEXT("loot") })
		{
			const int32 Want = Expected.FindRef(Kind);
			const int32 Got = Spawned.FindRef(Kind);
			const int32 Seen = Visible.FindRef(Kind);
			Check(State, FMath::Abs(Got - Want) <= CountTolerance * Want && Seen == Got,
				FString::Printf(TEXT("%-7s Training Ground %3d, spawned %3d, visible %3d"), Kind, Want, Got, Seen));
		}
		Check(State, Invisible == 0, FString::Printf(TEXT("every prop has a visible mesh (%d without)"), Invisible));
		Check(State, Outside == 0, FString::Printf(TEXT("every prop inside the layout bounds (%d outside)"), Outside));
		Check(State, OffSurface == 0, FString::Printf(TEXT("every prop within %.0f cm of the surface under it (%d off; worst on-surface gap %.1f cm, %s)"),
			SurfaceToleranceCm, OffSurface, WorstGapCm, *WorstId));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %d props, greybox %d, spawn %.1f ms"), *State.MapName, AllProps.Num(),
			Props->GetGreyboxCount(), Props->GetLastSpawnDurationMs());
	}

	bool Step(UWorld* World, FState& State, float DeltaSeconds)
	{
		switch (State.PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.PreloadGate.FailReason);
			return Finish(State, false);
		default:
			break;
		}
		State.Time += DeltaSeconds;
		if (State.Time > TimeoutSeconds)
		{
			Check(State, false, TEXT("finished in time"));
			return Finish(State, false);
		}
		if (State.Time < WarmupSeconds)
		{
			return true;
		}
		RunChecks(World, State);
		return Finish(State, true);
	}

	void Start(const TArray<FString>& Args, UWorld* World, const TCHAR* CommandName)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->CommandName = CommandName;
		State->MapName = UWorld::RemovePIEPrefix(World->GetMapName());
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("map=")))
			{
				State->MapName = FPaths::GetBaseFilename(Arg.Mid(4));
			}
		}
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s on %s"), CommandName, *State->MapName);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return Finish(*State, false);
			}
			return !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GeneratedPropsSmoke"),
		TEXT("Dev check (G003): the generated map's Training Ground autogen props - count per kind = the TG's within 5 %, every prop visible, ")
		TEXT("inside the layout bounds, within 30 cm of the surface under it. Run on L_Gen_Depot_X4. [map=<L_Gen_...>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Start(Args, World, TEXT("GeneratedPropsSmoke"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CommandX6(
		TEXT("CodexTactics.GeneratedPropsX6Smoke"),
		TEXT("Dev check (G003): GeneratedPropsSmoke on L_Gen_Depot_1_X6 (the same checks). PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Start(Args, World, TEXT("GeneratedPropsX6Smoke"));
		}));
}

#endif // !UE_BUILD_SHIPPING
