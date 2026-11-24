// Dev-only console commands (G003 / G003b, designer 2026-11-17: the level generator is tested with ALL assets), on the generated maps:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands GeneratedPropsSmoke,GeneratedPropsX6Smoke
// UCodexLevelPropsSubsystem spawned the map's Training Ground autogen props file (Content/Data/LevelGen/props/<Map>.props.json,
// source auto) at BeginPlay as the game's REAL objects (props_balance.json gameplay.classes): barrels = ABarrelActor, loot crates /
// mined crates = ALootCrateActor, loose loot = ADroppedItemActor, field mines = AProximityMineActor, doorway mines = APMCTripwireActor,
// turrets = ATurretActor; plain crates stay greybox cover. Checks:
// 1. every object the Training Ground shows is in the game (count per kind within 5 %; nothing dropped by the validator now that the
//    TG autogen runs the same static validator), each a visible mesh or a hidden-until-spotted trap, inside the layout bounds, its
//    bottom within 30 cm of the surface under it; the spawn's longest frame (ms) against the 60 ms budget;
// 2. behaviour: a barrel explodes on a shot (the operative's targeted-shot path), a loot crate is emptied into an operative, a mine / wire
//    goes off when an operative walks onto it;
// 3. level turrets (TURRET_POWER.md phase 1): each on its power layer generator - powered exactly while it runs, its owner's side, about
//    three quarters dark at start (the generator count = the TG power layer); a turret taken off its cable and forced
//    unpowered does not fire at an operative in its range, the same turret forced powered + hostile engages him; switched to neutral
//    (TURRET_POWER.md: monsters only) it shoots a monster spawned in its range and no longer the operative.
// GeneratedPropsSmoke = L_Gen_Depot_X4, GeneratedPropsX6Smoke = L_Gen_Depot_1_X6; [map=<L_Gen_...>] for another generated map.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "Power/PowerGeneratorActor.h"
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
	constexpr float TimeoutSeconds = 150.f;
	/** Count tolerance against the Training Ground's numbers (G003: within 5 %). */
	constexpr double CountTolerance = 0.05;
	/** A prop's bottom may be this far from the surface under it (G003: within 30 cm of the ground / roof). */
	constexpr double SurfaceToleranceCm = 30.0;
	/** Seconds a turret / a mine gets to react. */
	constexpr float ReactSeconds = 6.f;
	/** Seconds an unpowered turret is watched for (it must not fire). */
	constexpr float SilentSeconds = 2.f;

	enum class EStage : uint8
	{
		Begin,
		TurretOff,
		TurretOn,
		TurretNeutral,
		Mine,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::Begin;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		FString MapName;
		FString CommandName;
		SmokeUtils::FPreloadGate PreloadGate;
		TWeakObjectPtr<ATurretActor> Turret;
		/** The operative standing in the turret's range. */
		TWeakObjectPtr<AOperativeCharacter> Target;
		float TargetHealthBefore = 0.f;
		/** Hits the immortal cheat (CombatCheats) absorbed on the target so far: a hit counts even when it costs no health. */
		int32 TargetBlockedBefore = 0;
		int32 ShotsBefore = 0;
		TWeakObjectPtr<AActor> Monster;
		float MonsterHealthBefore = 0.f;
		/** The neutral turret aimed a shot at the operative. */
		bool bNeutralShotOperative = false;
		TWeakObjectPtr<AActor> Mine;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		State.Stage = EStage::Done;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, *State.CommandName);
		return false;
	}

	bool HasVisibleMesh(const AActor* Actor)
	{
		if (!Actor || Actor->IsHidden())
		{
			return false;
		}
		TArray<UMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (const UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || !Mesh->IsVisible() || Mesh->GetComponentScale().IsNearlyZero())
			{
				continue;
			}
			const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh);
			const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Mesh);
			if ((Static && Static->GetStaticMesh()) || (Skeletal && Skeletal->GetSkeletalMeshAsset()))
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

	/** Every spawned prop actor (greybox props + the real game objects). */
	TArray<AActor*> AllPropActors(const UCodexLevelPropsSubsystem* Props)
	{
		TArray<AActor*> Out;
		for (const TWeakObjectPtr<ACodexPropActor>& Weak : Props->GetSpawnedProps())
		{
			if (Weak.IsValid())
			{
				Out.AddUnique(Weak.Get());
			}
		}
		for (const TWeakObjectPtr<AActor>& Weak : Props->GetSpawnedGameplayActors())
		{
			if (Weak.IsValid())
			{
				Out.AddUnique(Weak.Get());
			}
		}
		return Out;
	}

	/** The prop ids an actor stands for (its prop_id=<id> tags: a loot / mined crate carries its loot / mine object too). */
	TArray<FString> PropIdsOf(const AActor* Actor)
	{
		TArray<FString> Ids;
		for (const FName& Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(TEXT("prop_id=")))
			{
				Ids.Add(Text.Mid(8));
			}
		}
		return Ids;
	}

	/** Height of the actor's bottom: an interactable stands on its box's centre, a greybox prop on its origin. */
	double BottomZ(const AActor* Actor)
	{
		if (const AInteractableActor* Interactable = Cast<AInteractableActor>(Actor); Interactable && Interactable->Box)
		{
			return Interactable->Box->GetComponentLocation().Z - Interactable->Box->GetScaledBoxExtent().Z;
		}
		return Actor->GetActorLocation().Z;
	}

	TArray<AOperativeCharacter*> LivingOperatives(UWorld* World)
	{
		TArray<AOperativeCharacter*> Out;
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
			if (!Health || Health->IsAlive())
			{
				Out.Add(*It);
			}
		}
		return Out;
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
		TMap<FString, FString> KindOfId;
		for (const FLevelPropsResolvedObject& Obj : Resolved.Objects)
		{
			const FString Kind = KindOfLayer.FindRef(Obj.LayerId);
			Expected.FindOrAdd(Kind)++;
			KindOfId.Add(Obj.Object.Id, Kind);
		}

		FVector2D BoundsMin;
		FVector2D BoundsMax;
		const bool bBounds = LayoutBounds(File.Layout, BoundsMin, BoundsMax);
		Check(State, bBounds, FString::Printf(TEXT("layout %s bounds read"), *File.Layout));

		const TArray<AActor*> All = AllPropActors(Props);
		TMap<FString, TSet<FString>> SpawnedIds;
		TMap<FString, int32> Real;
		int32 Outside = 0;
		int32 OffSurface = 0;
		int32 Invisible = 0;
		int32 HiddenTraps = 0;
		double WorstGapCm = 0.0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GeneratedPropsSmoke), false);
		Params.AddIgnoredActors(All);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (AActor* Actor : All)
		{
			for (const FString& Id : PropIdsOf(Actor))
			{
				SpawnedIds.FindOrAdd(KindOfId.FindRef(Id)).Add(Id);
			}
			const bool bGreybox = Actor->IsA<ACodexPropActor>();
			Real.FindOrAdd(bGreybox ? FString(TEXT("greybox")) : Actor->GetClass()->GetName())++;
			const AInteractableActor* Interactable = Cast<AInteractableActor>(Actor);
			const bool bHiddenTrap = Interactable && Interactable->IsHiddenTrap();
			HiddenTraps += bHiddenTrap ? 1 : 0;
			if (!bHiddenTrap && !HasVisibleMesh(Actor))
			{
				++Invisible;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s (%s) has no visible mesh"), *Actor->GetName(), *Actor->GetClass()->GetName());
			}
			const FVector Location = Actor->GetActorLocation();
			if (bBounds && (Location.X < BoundsMin.X || Location.X > BoundsMax.X || Location.Y < BoundsMin.Y || Location.Y > BoundsMax.Y))
			{
				++Outside;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s outside the bounds at %s"), *Actor->GetName(), *Location.ToCompactString());
			}
			const double Bottom = BottomZ(Actor);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(Location.X, Location.Y, Bottom + 60.0),
				FVector(Location.X, Location.Y, Bottom - 400.0), ECC_WorldStatic, Params);
			const double Gap = bHit ? FMath::Abs(Bottom - Hit.ImpactPoint.Z) : 1e9;
			if (Gap > SurfaceToleranceCm)
			{
				++OffSurface;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s (%s) bottom %.1f - surface %s"), *Actor->GetName(), *Actor->GetClass()->GetName(), Bottom,
					bHit ? *FString::Printf(TEXT("at Z %.1f (%s)"), Hit.ImpactPoint.Z, *GetNameSafe(Hit.GetActor())) : TEXT("none"));
			}
			else
			{
				WorstGapCm = FMath::Max(WorstGapCm, Gap);
			}
		}

		Check(State, Props->HasSpawned() && !Props->IsSpawnPending(), TEXT("props spawned (the frame-sliced spawn finished)"));
		Check(State, Props->GetDroppedIds().Num() == 0,
			FString::Printf(TEXT("validator dropped %d (the TG autogen runs the same validator now): %s"), Props->GetDroppedIds().Num(),
				*FString::Join(Props->GetDroppedIds(), TEXT(", "))));
		for (const TCHAR* Kind : { TEXT("crates"), TEXT("barrels"), TEXT("turrets"), TEXT("mines"), TEXT("loot"), TEXT("power") })
		{
			const int32 Want = Expected.FindRef(Kind);
			const int32 Got = SpawnedIds.FindRef(Kind).Num();
			Check(State, FMath::Abs(Got - Want) <= CountTolerance * Want,
				FString::Printf(TEXT("%-7s Training Ground %3d, in the game %3d"), Kind, Want, Got));
		}
		FString Classes;
		for (const TPair<FString, int32>& Pair : Real)
		{
			Classes += FString::Printf(TEXT("%s %d, "), *Pair.Key, Pair.Value);
		}
		const bool bExpectLoot = Expected.FindRef(TEXT("loot")) > 0;
		Check(State, Real.FindRef(TEXT("BarrelActor")) > 0 && Real.FindRef(TEXT("LootCrateActor")) > 0 && Real.FindRef(TEXT("TurretActor")) > 0
			&& (!bExpectLoot || Real.FindRef(TEXT("DroppedItemActor")) > 0) && (Real.FindRef(TEXT("ProximityMineActor")) + Real.FindRef(TEXT("PMCTripwireActor"))) > 0,
			FString::Printf(TEXT("the props are the game's real objects: %s"), *Classes));
		// Level turrets (TURRET_POWER.md phase 1): every turret of an autogen map hangs on a power layer generator; it is powered exactly
		// while its generator runs, its side is the generator owner's, and about three quarters of them start dark.
		int32 TurretCount = 0;
		int32 Powered = 0;
		int32 Unlinked = 0;
		int32 Mismatch = 0;
		int32 Generators = 0;
		for (AActor* Actor : All)
		{
			if (const ATurretActor* Turret = Cast<ATurretActor>(Actor))
			{
				++TurretCount;
				Powered += Turret->IsPowered() ? 1 : 0;
				const APowerGeneratorActor* Generator = Turret->GetPowerSource();
				if (!Generator)
				{
					++Unlinked;
					continue;
				}
				const EPowerAllegiance Side = Generator->GetAllegiance();
				const ETurretFaction Want = Side == EPowerAllegiance::Friendly ? ETurretFaction::Friendly
					: (Side == EPowerAllegiance::Hostile ? ETurretFaction::Hostile : ETurretFaction::Neutral);
				Mismatch += Turret->IsPowered() != Generator->IsRunning() || (Turret->IsPowered() && Turret->GetFaction() != Want) ? 1 : 0;
			}
			Generators += Actor && Actor->IsA<APowerGeneratorActor>() ? 1 : 0;
		}
		Check(State, Generators > 0 && Unlinked == 0 && Mismatch == 0 && Powered * 2 <= TurretCount, FString::Printf(
			TEXT("level turrets: %d generator(s), %d of %d turrets powered (~75 %% dark), %d unlinked, %d not matching their generator"), Generators,
			Powered, TurretCount, Unlinked, Mismatch));
		Check(State, Invisible == 0, FString::Printf(TEXT("every prop has a visible mesh or is a hidden-until-spotted trap (%d without, %d hidden traps)"),
			Invisible, HiddenTraps));
		Check(State, Outside == 0, FString::Printf(TEXT("every prop inside the layout bounds (%d outside)"), Outside));
		Check(State, OffSurface == 0, FString::Printf(TEXT("every prop's bottom within %.0f cm of the surface under it (%d off; worst %.1f cm)"),
			SurfaceToleranceCm, OffSurface, WorstGapCm));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %d actors (%d game objects), spawn %.1f ms in %d frames, longest frame %.1f ms (prepare %.1f ms)"),
			*State.MapName, All.Num(), Props->GetSpawnedGameplayCount(), Props->GetLastSpawnDurationMs(), Props->GetLastSpawnFrames(),
			Props->GetLastMaxFrameMs(), Props->GetLastPrepareMs());
		Check(State, Props->GetLastMaxFrameMs() <= 60.0, FString::Printf(TEXT("longest spawn frame %.1f ms <= 60 ms budget"), Props->GetLastMaxFrameMs()));

		// --- behaviour: barrel, loot crate (now); turret, mine (staged) ---------------------------------------------------------
		ABarrelActor* Barrel = nullptr;
		ALootCrateActor* LootCrate = nullptr;
		for (AActor* Actor : All)
		{
			Barrel = Barrel ? Barrel : Cast<ABarrelActor>(Actor);
			if (ALootCrateActor* Crate = Cast<ALootCrateActor>(Actor); Crate && !LootCrate && !Crate->bTrapped && !Crate->IsLooted())
			{
				LootCrate = Crate;
			}
			if (ATurretActor* Turret = Cast<ATurretActor>(Actor); Turret && !State.Turret.IsValid())
			{
				State.Turret = Turret;
			}
			if (Cast<AProximityMineActor>(Actor) && !State.Mine.IsValid())
			{
				State.Mine = Actor;
			}
		}
		if (!State.Mine.IsValid())
		{
			for (AActor* Actor : All)
			{
				if (Cast<ATripwireActor>(Actor))
				{
					State.Mine = Actor;
					break;
				}
			}
		}

		const TArray<AOperativeCharacter*> Squad = LivingOperatives(World);
		Check(State, Squad.Num() >= 1, FString::Printf(TEXT("%d operatives for the behaviour checks"), Squad.Num()));
		if (Barrel && !Squad.IsEmpty())
		{
			const ETargetedShotKind Kind = AOperativeCharacter::ClassifyShotTarget(Barrel);
			const bool bExploded = Kind == ETargetedShotKind::Barrel && AOperativeCharacter::DetonateShotTarget(Barrel, Kind, Squad[0]->DisplayName);
			Check(State, bExploded && Barrel->IsBurning(), FString::Printf(TEXT("barrel %s explodes on a shot (burning %d)"), *Barrel->GetName(), Barrel->IsBurning()));
		}
		else
		{
			Check(State, false, TEXT("a barrel to shoot"));
		}
		if (LootCrate && !Squad.IsEmpty())
		{
			const int32 ItemsBefore = LootCrate->GetItems().Num();
			LootCrate->TakeAll(Squad.Last());
			Check(State, ItemsBefore > 0 && LootCrate->IsLooted(), FString::Printf(TEXT("loot crate %s (%d stacks) is emptied into %s"), *LootCrate->GetName(),
				ItemsBefore, *Squad.Last()->DisplayName.ToString()));
		}
		else
		{
			Check(State, false, TEXT("a closed, safe loot crate"));
		}
	}

	/** A spot in the turret's range with a line of fire, on the turret's surface (a roof turret: on its roof). */
	bool TurretTargetSpot(UWorld* World, const ATurretActor* Turret, FVector& Out, const FVector* Avoid = nullptr)
	{
		const FVector Base = Turret->GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GeneratedPropsTurretSpot), false, Turret);
		const double TurretFloor = BottomZ(Turret);
		for (const double Distance : { 500.0, 350.0, 250.0 })
		{
			for (int32 Step = 0; Step < 16; ++Step)
			{
				const FVector Dir = FRotator(0.0, Turret->GetActorRotation().Yaw + Step * 22.5, 0.0).Vector();
				const FVector Probe = Base + Dir * Distance;
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, FVector(Probe.X, Probe.Y, TurretFloor + 120.0), FVector(Probe.X, Probe.Y, TurretFloor - 80.0),
						ECC_WorldStatic, Params)
					&& FMath::Abs(Hit.ImpactPoint.Z - TurretFloor) <= 40.0)
				{
					if (Avoid && FVector::Dist2D(Hit.ImpactPoint, *Avoid) < 200.0)
					{
						continue;
					}
					FHitResult Sight;
					if (!World->LineTraceSingleByChannel(Sight, Base + FVector(0.0, 0.0, 40.0), Hit.ImpactPoint + FVector(0.0, 0.0, 80.0), ECC_Visibility, Params))
					{
						Out = Hit.ImpactPoint + FVector(0.0, 0.0, 100.0);
						return true;
					}
				}
			}
		}
		return false;
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
		State.StageTime += DeltaSeconds;
		if (State.Time > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		const UCodexLevelPropsSubsystem* Props = World->GetSubsystem<UCodexLevelPropsSubsystem>();
		switch (State.Stage)
		{
		case EStage::Begin:
		{
			if (State.StageTime < WarmupSeconds || (Props && Props->IsSpawnPending()))
			{
				return true;
			}
			RunChecks(World, State);
			// The map's powered hostile turrets are switched off for the behaviour checks (only the test turret fires, and only when told).
			for (TActorIterator<APowerGeneratorActor> It(World); It; ++It)
			{
				It->StopEngine(nullptr, false);
			}
			for (TActorIterator<ATurretActor> It(World); It; ++It)
			{
				It->SetPowered(false);
			}
			// Turret: an operative in its range; first the turret forced unpowered (it must hold fire), then powered + hostile.
			const TArray<AOperativeCharacter*> Squad = LivingOperatives(World);
			FVector Spot;
			if (State.Turret.IsValid() && !Squad.IsEmpty() && TurretTargetSpot(World, State.Turret.Get(), Spot))
			{
				ATurretActor* Turret = State.Turret.Get();
				AOperativeCharacter* Target = Squad.Last();
				// Off its generator's cable: the forced power / side below are the turret's own again (TURRET_POWER.md network turrets).
				if (APowerGeneratorActor* Generator = Turret->GetPowerSource())
				{
					Generator->UnlinkTurret(Turret);
				}
				Turret->SetFaction(ETurretFaction::Hostile);
				Turret->SetPowered(false);
				Target->StopOperative();
				Target->SetActorLocation(Spot - FVector(0.0, 0.0, 100.0) + FVector(0.0, 0.0, Target->GetSimpleCollisionHalfHeight() + 2.0), false, nullptr,
					ETeleportType::TeleportPhysics);
				const UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
				State.Target = Target;
				State.TargetHealthBefore = Health ? Health->GetCurrentHealth() : 0.f;
				State.ShotsBefore = Turret->GetShotsFired();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s stands %.1f m from turret %s (unpowered)"), *Target->DisplayName.ToString(),
					FVector::Dist(Target->GetActorLocation(), Turret->GetActorLocation()) / 100.f, *Turret->GetName());
			}
			else
			{
				Check(State, false, TEXT("a turret with a spot in its range and an operative to stand there"));
			}
			State.Stage = EStage::TurretOff;
			State.StageTime = 0.f;
			return true;
		}
		case EStage::TurretOff:
		{
			ATurretActor* Turret = State.Turret.Get();
			if (Turret && State.Target.IsValid() && State.StageTime < SilentSeconds)
			{
				return true;
			}
			if (Turret && State.Target.IsValid())
			{
				Check(State, Turret->GetShotsFired() == State.ShotsBefore,
					FString::Printf(TEXT("an unpowered turret does not fire (%d shots in %.0f s)"), Turret->GetShotsFired() - State.ShotsBefore, SilentSeconds));
				Turret->SetPowered(true);
				State.ShotsBefore = Turret->GetShotsFired();
				const UHealthComponent* Health = State.Target->FindComponentByClass<UHealthComponent>();
				State.TargetHealthBefore = Health ? Health->GetCurrentHealth() : 0.f;
				State.TargetBlockedBefore = Health ? Health->GetCheatBlockedHits() : 0;
			}
			State.Stage = EStage::TurretOn;
			State.StageTime = 0.f;
			return true;
		}
		case EStage::TurretOn:
		{
			ATurretActor* Turret = State.Turret.Get();
			const UHealthComponent* Health = State.Target.IsValid() ? State.Target->FindComponentByClass<UHealthComponent>() : nullptr;
			const float Now = Health ? Health->GetCurrentHealth() : 0.f;
			const bool bFired = Turret && Turret->GetShotsFired() > State.ShotsBefore;
			const int32 Blocked = Health ? Health->GetCheatBlockedHits() - State.TargetBlockedBefore : 0;
			const bool bHit = bFired && Health && (Now < State.TargetHealthBefore || Blocked > 0);
			if (Turret && State.Target.IsValid() && !bHit && State.StageTime < ReactSeconds)
			{
				return true;
			}
			if (Turret && State.Target.IsValid())
			{
				Check(State, bHit, FString::Printf(TEXT("a powered hostile turret engages the operative (%d shots, health %.0f -> %.0f, %d hits absorbed by the immortal cheat)"),
					Turret->GetShotsFired() - State.ShotsBefore, State.TargetHealthBefore, Now, Blocked));
			}
			// Neutral (monsters only): a monster in range is shot, the operative standing there is not.
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			FVector MonsterSpot;
			const FVector TargetSpot = State.Target.IsValid() ? State.Target->GetActorLocation() : FVector::ZeroVector;
			if (Turret && State.Target.IsValid() && Waves && TurretTargetSpot(World, Turret, MonsterSpot, &TargetSpot))
			{
				Turret->SetFaction(ETurretFaction::Neutral);
				AEnemyCharacter* Monster = Waves->SpawnEnemy(EEnemyArchetype::Brute, MonsterSpot);
				State.Monster = Monster;
				const UHealthComponent* MonsterHealth = Monster ? Monster->FindComponentByClass<UHealthComponent>() : nullptr;
				State.MonsterHealthBefore = MonsterHealth ? MonsterHealth->GetCurrentHealth() : 0.f;
				const UHealthComponent* TargetHealth = State.Target->FindComponentByClass<UHealthComponent>();
				State.TargetHealthBefore = TargetHealth ? TargetHealth->GetCurrentHealth() : 0.f;
				State.TargetBlockedBefore = TargetHealth ? TargetHealth->GetCheatBlockedHits() : 0;
				State.ShotsBefore = Turret->GetShotsFired();
				State.bNeutralShotOperative = false;
				Check(State, Monster != nullptr, FString::Printf(TEXT("a monster spawned %.1f m from the neutral turret"),
					Monster ? FVector::Dist(Monster->GetActorLocation(), Turret->GetActorLocation()) / 100.f : 0.f));
				State.Stage = EStage::TurretNeutral;
				State.StageTime = 0.f;
				return true;
			}
			Check(State, false, TEXT("a second spot in the turret's range for the neutral check"));
			State.Stage = EStage::TurretNeutral;
			State.StageTime = ReactSeconds;
			return true;
		}
		case EStage::TurretNeutral:
		{
			ATurretActor* Turret = State.Turret.Get();
			const UHealthComponent* MonsterHealth = State.Monster.IsValid() ? State.Monster->FindComponentByClass<UHealthComponent>() : nullptr;
			const float MonsterNow = MonsterHealth ? MonsterHealth->GetCurrentHealth() : 0.f;
			const bool bShotMonster = Turret && Turret->GetShotsFired() > State.ShotsBefore && Turret->GetLastTarget() == State.Monster.Get();
			State.bNeutralShotOperative |= Turret && Turret->GetShotsFired() > State.ShotsBefore && Turret->GetLastTarget() == State.Target.Get();
			const bool bMonsterHit = bShotMonster && MonsterHealth && MonsterNow < State.MonsterHealthBefore;
			if (State.Monster.IsValid() && !bMonsterHit && State.StageTime < ReactSeconds)
			{
				return true;
			}
			if (State.Monster.IsValid() && State.Target.IsValid())
			{
				Check(State, bMonsterHit, FString::Printf(TEXT("a powered neutral turret shoots the monster (health %.0f -> %.0f)"),
					State.MonsterHealthBefore, MonsterNow));
				Check(State, !State.bNeutralShotOperative, FString::Printf(TEXT("a neutral turret does not shoot the operative (%d shots, last at %s)"),
					Turret ? Turret->GetShotsFired() - State.ShotsBefore : 0, Turret ? *GetNameSafe(Turret->GetLastTarget()) : TEXT("-")));
			}
			if (AActor* Monster = State.Monster.Get())
			{
				Monster->Destroy();
			}
			if (Turret)
			{
				Turret->SetPowered(false); // let the operative live for the mine check
			}
			// A mine / wire: the first living operative walks onto it.
			const TArray<AOperativeCharacter*> Squad = LivingOperatives(World);
			AActor* Mine = State.Mine.Get();
			if (!Mine || Squad.IsEmpty())
			{
				Check(State, false, TEXT("a mine / wire and an operative to walk onto it"));
				return Finish(State, true);
			}
			FVector Ground = Mine->GetActorLocation();
			Ground.Z = Cast<ATripwireActor>(Mine) ? Ground.Z - 30.0 : BottomZ(Mine); // the wire hangs 30 cm over the ground
			Squad[0]->StopOperative();
			Squad[0]->SetActorLocation(Ground + FVector(0.0, 0.0, Squad[0]->GetSimpleCollisionHalfHeight() + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s walks onto %s (%s)"), *Squad[0]->DisplayName.ToString(), *Mine->GetName(), *Mine->GetClass()->GetName());
			State.Stage = EStage::Mine;
			State.StageTime = 0.f;
			return true;
		}
		case EStage::Mine:
		{
			const AActor* Mine = State.Mine.Get();
			const ATripwireActor* Wire = Cast<ATripwireActor>(Mine);
			const bool bWentOff = !Mine || Mine->IsActorBeingDestroyed() || (Wire && Wire->IsTripped());
			if (!bWentOff && State.StageTime < ReactSeconds)
			{
				return true;
			}
			Check(State, bWentOff, TEXT("the mine / wire goes off when an operative walks onto it"));
			return Finish(State, true);
		}
		default:
			return false;
		}
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
			return State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GeneratedPropsSmoke"),
		TEXT("Dev check (G003 / G003b): the generated map's Training Ground autogen props as the game's real objects - count per kind = the TG's, ")
		TEXT("visible (or a hidden trap), inside the bounds, on the surface, the longest spawn frame <= 60 ms; a barrel explodes on a shot, a loot ")
		TEXT("crate is emptied, level turrets inactive but a quarter powered + hostile, an unpowered turret holds fire and a powered hostile one engages an operative, ")
		TEXT("a mine / wire goes off under an operative. Run on L_Gen_Depot_X4. [map=<L_Gen_...>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Start(Args, World, TEXT("GeneratedPropsSmoke"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CommandX6(
		TEXT("CodexTactics.GeneratedPropsX6Smoke"),
		TEXT("Dev check (G003 / G003b): GeneratedPropsSmoke on L_Gen_Depot_1_X6 (the same checks). PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Start(Args, World, TEXT("GeneratedPropsX6Smoke"));
		}));
}

#endif // !UE_BUILD_SHIPPING
