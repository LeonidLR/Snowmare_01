#include "Bot/SpatialTelemetryRecorder.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	double SpatialRound(double Value, double Scale)
	{
		return FMath::RoundToDouble(Value * Scale) / Scale;
	}

	TArray<TSharedPtr<FJsonValue>> SpatialVec(const FVector& Godot)
	{
		return { MakeShared<FJsonValueNumber>(SpatialRound(Godot.X, 100.0)), MakeShared<FJsonValueNumber>(SpatialRound(Godot.Y, 100.0)),
			MakeShared<FJsonValueNumber>(SpatialRound(Godot.Z, 100.0)) };
	}

	FVector SpatialFeet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	/** Godot rotation.y (radians, around the up axis) from an Unreal yaw: Godot's z is Unreal's Y. */
	double SpatialYaw(const AActor* Actor)
	{
		return SpatialRound(-FMath::DegreesToRadians(Actor->GetActorRotation().Yaw), 1000.0);
	}
}

FString FSpatialTelemetryRecorder::GetDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("Telemetry/spatial_runs");
}

void FSpatialTelemetryRecorder::Start(UWorld* World, const FString& InLevelId, const FString& InProfile)
{
	WorldPtr = World;
	LevelId = InLevelId;
	Profile = InProfile;
	SessionId = FString::Printf(TEXT("%s_%lld"), *FGuid::NewGuid().ToString(EGuidFormats::Short), FDateTime::UtcNow().ToUnixTimestamp());
	SimTime = 0.0;
	TimeAccum = 0.0;
	Frames.Reset();
	Events.Reset();
	CoverUse.Reset();
	Stalls.Reset();
	Congestion.Reset();
	HeightUsageTime = 0.0;
	KillsFromHeight = 0;
	bRecording = World != nullptr;
	CaptureLayout();
}

void FSpatialTelemetryRecorder::CaptureLayout()
{
	UWorld* World = WorldPtr.Get();
	if (!World)
	{
		return;
	}
	// Bounds: the fight area around the squad, the spawn points and the covers, with a 10 m margin.
	FBox Area(ForceInit);
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Area += Member->GetActorLocation();
		}
	}
	TArray<TSharedPtr<FJsonValue>> Spawns;
	for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
	{
		Area += It->GetActorLocation();
		TSharedRef<FJsonObject> Spawn = MakeShared<FJsonObject>();
		Spawn->SetStringField(TEXT("id"), It->GetName());
		Spawn->SetArrayField(TEXT("position"), SpatialVec(ToGodot(It->GetActorLocation())));
		Spawns.Add(MakeShared<FJsonValueObject>(Spawn));
	}
	Area = Area.ExpandBy(1000.f);

	TArray<TSharedPtr<FJsonValue>> Covers;
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		if (!Area.IsInsideXY(It->GetActorLocation()))
		{
			continue;
		}
		FVector Origin, Extent;
		It->GetActorBounds(true, Origin, Extent);
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		TSharedRef<FJsonObject> Cover = MakeShared<FJsonObject>();
		Cover->SetStringField(TEXT("id"), It->GetName());
		Cover->SetStringField(TEXT("type"), TEXT("BARRICADE"));
		Cover->SetArrayField(TEXT("position"), SpatialVec(ToGodot(SpatialFeet(*It))));
		Cover->SetNumberField(TEXT("rotation_y"), SpatialYaw(*It));
		Cover->SetArrayField(TEXT("size"), SpatialVec(ToGodot(Extent * 2.f)));
		Cover->SetNumberField(TEXT("max_health"), Health ? Health->GetMaxHealth() : 0.f);
		Covers.Add(MakeShared<FJsonValueObject>(Cover));
		FCoverUse& Use = CoverUse.Add(It->GetName());
		Use.Location = It->GetActorLocation();
		Use.MaxHp = Health ? Health->GetMaxHealth() : 0.f;
	}

	// Obstacles: static meshes with collision standing in the area (not the floor: footprint up to 40 m, at least 0.8 m tall).
	TArray<TSharedPtr<FJsonValue>> Obstacles;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		FVector Origin, Extent;
		It->GetActorBounds(true, Origin, Extent);
		const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
		if (!Area.IsInsideXY(Origin) || !Root || Root->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			|| Extent.Z * 2.f < 80.f || FMath::Max(Extent.X, Extent.Y) * 2.f > 4000.f)
		{
			continue;
		}
		TSharedRef<FJsonObject> Obstacle = MakeShared<FJsonObject>();
		Obstacle->SetStringField(TEXT("id"), It->GetName());
		Obstacle->SetStringField(TEXT("type"), TEXT("STATIC"));
		Obstacle->SetStringField(TEXT("shape"), TEXT("box"));
		Obstacle->SetArrayField(TEXT("position"), SpatialVec(ToGodot(Origin)));
		Obstacle->SetArrayField(TEXT("size"), SpatialVec(ToGodot(Extent * 2.f)));
		Obstacle->SetNumberField(TEXT("rotation_y"), 0.0); // world-aligned bounds
		Obstacles.Add(MakeShared<FJsonValueObject>(Obstacle));
	}

	TArray<TSharedPtr<FJsonValue>> Defend;
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (It->ObjectType == EInteractableType::Generator)
		{
			TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
			Point->SetStringField(TEXT("id"), It->GetName());
			Point->SetArrayField(TEXT("position"), SpatialVec(ToGodot(SpatialFeet(*It))));
			Point->SetNumberField(TEXT("radius"), 4.0);
			Defend.Add(MakeShared<FJsonValueObject>(Point));
		}
	}

	TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
	const FVector Min = ToGodot(Area.Min);
	const FVector Max = ToGodot(Area.Max);
	Bounds->SetNumberField(TEXT("min_x"), SpatialRound(Min.X, 10.0));
	Bounds->SetNumberField(TEXT("max_x"), SpatialRound(Max.X, 10.0));
	Bounds->SetNumberField(TEXT("min_z"), SpatialRound(Min.Z, 10.0));
	Bounds->SetNumberField(TEXT("max_z"), SpatialRound(Max.Z, 10.0));
	Layout = MakeShared<FJsonObject>();
	Layout->SetObjectField(TEXT("bounds"), Bounds);
	Layout->SetArrayField(TEXT("obstacles"), Obstacles);
	Layout->SetArrayField(TEXT("covers"), Covers);
	Layout->SetArrayField(TEXT("elevations"), {});
	Layout->SetArrayField(TEXT("spawn_points"), Spawns);
	Layout->SetArrayField(TEXT("defend_points"), Defend);
}

FString FSpatialTelemetryRecorder::FindCoverId(const FVector& UnrealPosition) const
{
	FString Best;
	double BestDistance = 300.0; // Godot 3 m
	for (const TPair<FString, FCoverUse>& Cover : CoverUse)
	{
		const double Distance = FVector::Dist2D(UnrealPosition, Cover.Value.Location);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Cover.Key;
		}
	}
	return Best;
}

void FSpatialTelemetryRecorder::Tick(float DeltaTime)
{
	if (!bRecording)
	{
		return;
	}
	SimTime += DeltaTime;
	TimeAccum += DeltaTime;
	if (TimeAccum >= TickInterval)
	{
		TimeAccum -= TickInterval;
		RecordFrame();
	}
}

void FSpatialTelemetryRecorder::RecordFrame()
{
	UWorld* World = WorldPtr.Get();
	if (!World)
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Squad;
	if (const USquadSubsystem* SquadSystem = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : SquadSystem->GetMembers())
		{
			const FVector Feet = SpatialFeet(Member);
			const bool bInCover = Member->IsInBarricadeCover();
			const FString CoverId = bInCover ? FindCoverId(Feet) : FString();
			if (FCoverUse* Use = CoverUse.Find(CoverId))
			{
				Use->TimeSec += TickInterval;
			}
			const FVector Godot = ToGodot(Feet);
			if (Godot.Y >= 1.5)
			{
				HeightUsageTime += TickInterval;
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("id"), Member->DisplayName.ToString());
			Entry->SetNumberField(TEXT("x"), SpatialRound(Godot.X, 100.0));
			Entry->SetNumberField(TEXT("y"), SpatialRound(Godot.Y, 100.0));
			Entry->SetNumberField(TEXT("z"), SpatialRound(Godot.Z, 100.0));
			Entry->SetNumberField(TEXT("hp"), SpatialRound(Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f, 10.0));
			Entry->SetNumberField(TEXT("stance"), static_cast<int32>(Member->GetStance()));
			Entry->SetBoolField(TEXT("in_cover"), bInCover);
			if (CoverId.IsEmpty())
			{
				Entry->SetField(TEXT("cover_id"), MakeShared<FJsonValueNull>());
			}
			else
			{
				Entry->SetStringField(TEXT("cover_id"), CoverId);
			}
			Entry->SetStringField(TEXT("weapon"), Member->CurrentWeapon ? Member->CurrentWeapon->WeaponId : FString(TEXT("m16")));
			Entry->SetNumberField(TEXT("cold"), FMath::RoundToDouble(Member->ColdLevel));
			Squad.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Enemies;
	TMap<FString, int32> CellCounts;
	TMap<FString, int32> CellStalled;
	TMap<FString, float> CellMaxStall;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (It->IsDying() || !Health || !Health->IsAlive())
		{
			continue;
		}
		const FVector Location = It->GetActorLocation();
		const FVector Godot = ToGodot(SpatialFeet(*It));
		const FVector Velocity = ToGodot(It->GetVelocity());
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), It->GetName());
		Entry->SetStringField(TEXT("type"), StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(It->GetArchetype())).ToUpper());
		Entry->SetNumberField(TEXT("x"), SpatialRound(Godot.X, 100.0));
		Entry->SetNumberField(TEXT("y"), SpatialRound(Godot.Y, 100.0));
		Entry->SetNumberField(TEXT("z"), SpatialRound(Godot.Z, 100.0));
		Entry->SetNumberField(TEXT("hp"), SpatialRound(Health->GetCurrentHealth(), 10.0));
		Entry->SetNumberField(TEXT("vx"), SpatialRound(Velocity.X, 10.0));
		Entry->SetNumberField(TEXT("vz"), SpatialRound(Velocity.Z, 10.0));
		if (const AActor* Target = It->GetCurrentTarget())
		{
			Entry->SetStringField(TEXT("target_id"), Target->IsA<AOperativeCharacter>() ? Cast<AOperativeCharacter>(Target)->DisplayName.ToString() : Target->GetName());
		}
		else
		{
			Entry->SetField(TEXT("target_id"), MakeShared<FJsonValueNull>());
		}
		Enemies.Add(MakeShared<FJsonValueObject>(Entry));

		// Godot stall tracking: under 0.35 m/s and within 1.5 m of its anchor.
		FStall& Stall = Stalls.FindOrAdd(It->GetName());
		if (Stall.Anchor.IsZero())
		{
			Stall.Anchor = Location;
		}
		if (It->GetVelocity().Size2D() < 35.f && FVector::Dist2D(Location, Stall.Anchor) < 150.f)
		{
			Stall.Seconds += TickInterval;
		}
		else
		{
			Stall.Anchor = Location;
			Stall.Seconds = 0.f;
		}
		// 3 x 3 m congestion cells in Godot coordinates.
		const FString Key = FString::Printf(TEXT("%d,%d"), FMath::FloorToInt(Godot.X / 3.0), FMath::FloorToInt(Godot.Z / 3.0));
		++CellCounts.FindOrAdd(Key);
		if (Stall.Seconds >= 0.1f)
		{
			++CellStalled.FindOrAdd(Key);
			float& Max = CellMaxStall.FindOrAdd(Key);
			Max = FMath::Max(Max, Stall.Seconds);
		}
	}
	for (const TPair<FString, int32>& Cell : CellCounts)
	{
		const int32 Count = Cell.Value;
		const float MaxStall = CellMaxStall.FindRef(Cell.Key);
		if (Count < 3 && !(CellStalled.FindRef(Cell.Key) >= 1 && MaxStall >= 0.3f))
		{
			continue;
		}
		FCongestion* Data = Congestion.Find(Cell.Key);
		if (!Data)
		{
			FString X, Z;
			Cell.Key.Split(TEXT(","), &X, &Z);
			Data = &Congestion.Add(Cell.Key);
			Data->WorldPos = FVector2D((FCString::Atoi(*X) + 0.5) * 3.0, (FCString::Atoi(*Z) + 0.5) * 3.0);
		}
		++Data->Hits;
		Data->DurationSec = FMath::Max(Data->DurationSec, MaxStall);
		Data->PeakEnemies = FMath::Max(Data->PeakEnemies, Count);
		Data->bSingleEnemyStuck = Count == 1 && Data->PeakEnemies == 1 ? true : (Count > 1 ? false : Data->bSingleEnemyStuck);
		if (MaxStall >= 3.f && Data->Hits % 15 == 1)
		{
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetNumberField(TEXT("enemy_density"), Count);
			Details->SetNumberField(TEXT("duration_sec"), SpatialRound(MaxStall, 10.0));
			Details->SetStringField(TEXT("stuck_type"), Count == 1 && Data->bSingleEnemyStuck ? TEXT("SINGLE_ENEMY_STUCK") : TEXT("MASS_CONGESTION"));
			Details->SetStringField(TEXT("cell"), Cell.Key);
			// Back to Unreal cm for RecordEvent (it converts).
			RecordEvent(TEXT("CHOKE_CONGESTION"), FString(), FString(), FVector(Data->WorldPos.X * 100.0, Data->WorldPos.Y * 100.0, 0.0), Details);
		}
	}

	TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetNumberField(TEXT("t"), SpatialRound(SimTime, 100.0));
	Frame->SetArrayField(TEXT("squad"), Squad);
	Frame->SetArrayField(TEXT("enemies"), Enemies);
	Frames.Add(MakeShared<FJsonValueObject>(Frame));
}

void FSpatialTelemetryRecorder::RecordEvent(const FString& Type, const FString& ActorId, const FString& TargetId,
	const FVector& UnrealPosition, const TSharedPtr<FJsonObject>& Details)
{
	if (!bRecording)
	{
		return;
	}
	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetNumberField(TEXT("t"), SpatialRound(SimTime, 100.0));
	Event->SetStringField(TEXT("type"), Type);
	Event->SetStringField(TEXT("actor_id"), ActorId);
	Event->SetStringField(TEXT("target_id"), TargetId);
	Event->SetArrayField(TEXT("position"), SpatialVec(ToGodot(UnrealPosition)));
	Event->SetObjectField(TEXT("details"), Details.IsValid() ? Details.ToSharedRef() : MakeShared<FJsonObject>());
	Events.Add(MakeShared<FJsonValueObject>(Event));
	double KillerHeight = 0.0;
	if (Type == TEXT("ENEMY_DEATH") && Details.IsValid() && Details->TryGetNumberField(TEXT("killer_height"), KillerHeight) && KillerHeight >= 1.5)
	{
		++KillsFromHeight;
	}
	FString CoverId;
	if ((Type == TEXT("COVER_DAMAGED") || Type == TEXT("COVER_DESTROYED")) && Details.IsValid() && Details->TryGetStringField(TEXT("cover_id"), CoverId))
	{
		if (FCoverUse* Use = CoverUse.Find(CoverId))
		{
			++Use->HitsAbsorbed;
		}
	}
}

FString FSpatialTelemetryRecorder::Finish(const FString& Result, int32 WavesCleared, int32 TotalWaves)
{
	if (!bRecording)
	{
		return FString();
	}
	bRecording = false;

	struct FChoke
	{
		double Score;
		TSharedRef<FJsonObject> Object;
	};
	TArray<FChoke> Chokes;
	for (const TPair<FString, FCongestion>& Cell : Congestion)
	{
		const FCongestion& Data = Cell.Value;
		if (Data.DurationSec < 3.f && !(Data.Hits >= 5 && Data.PeakEnemies >= 2))
		{
			continue;
		}
		TSharedRef<FJsonObject> Choke = MakeShared<FJsonObject>();
		const double Score = SpatialRound(FMath::Max(Data.DurationSec, 1.f) * Data.PeakEnemies, 10.0);
		Choke->SetArrayField(TEXT("position"), { MakeShared<FJsonValueNumber>(Data.WorldPos.X), MakeShared<FJsonValueNumber>(Data.WorldPos.Y) });
		Choke->SetNumberField(TEXT("congestion_score"), Score);
		Choke->SetNumberField(TEXT("enemy_count_peak"), Data.PeakEnemies);
		Choke->SetNumberField(TEXT("duration_sec"), SpatialRound(Data.DurationSec, 10.0));
		Choke->SetStringField(TEXT("stuck_type"), Data.bSingleEnemyStuck && Data.PeakEnemies == 1 ? TEXT("SINGLE") : TEXT("MASS"));
		Chokes.Add({ Score, Choke });
	}
	Chokes.Sort([](const FChoke& A, const FChoke& B) { return A.Score > B.Score; });
	TArray<TSharedPtr<FJsonValue>> ChokeValues;
	for (const FChoke& Choke : Chokes)
	{
		ChokeValues.Add(MakeShared<FJsonValueObject>(Choke.Object));
	}
	TSharedRef<FJsonObject> CoverMetrics = MakeShared<FJsonObject>();
	for (const TPair<FString, FCoverUse>& Cover : CoverUse)
	{
		TSharedRef<FJsonObject> Use = MakeShared<FJsonObject>();
		Use->SetNumberField(TEXT("time_sec"), SpatialRound(Cover.Value.TimeSec, 10.0));
		Use->SetNumberField(TEXT("hits_absorbed"), Cover.Value.HitsAbsorbed);
		Use->SetNumberField(TEXT("max_hp"), Cover.Value.MaxHp);
		const FVector Godot = ToGodot(Cover.Value.Location);
		Use->SetArrayField(TEXT("world_pos"), { MakeShared<FJsonValueNumber>(SpatialRound(Godot.X, 100.0)), MakeShared<FJsonValueNumber>(SpatialRound(Godot.Z, 100.0)) });
		CoverMetrics->SetObjectField(Cover.Key, Use);
	}
	TSharedRef<FJsonObject> Height = MakeShared<FJsonObject>();
	Height->SetNumberField(TEXT("elevation_usage_time_sec"), SpatialRound(HeightUsageTime, 10.0));
	Height->SetNumberField(TEXT("kills_from_height"), KillsFromHeight);
	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("result"), Result);
	Summary->SetNumberField(TEXT("waves_cleared"), WavesCleared);
	Summary->SetNumberField(TEXT("total_waves"), TotalWaves);
	Summary->SetObjectField(TEXT("cover_metrics"), CoverMetrics);
	Summary->SetArrayField(TEXT("choke_points_detected"), ChokeValues);
	Summary->SetObjectField(TEXT("height_metrics"), Height);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("session_id"), SessionId);
	Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%S")));
	Root->SetStringField(TEXT("level_id"), LevelId);
	Root->SetStringField(TEXT("tester_profile"), Profile);
	Root->SetStringField(TEXT("engine"), TEXT("unreal"));
	Root->SetNumberField(TEXT("duration_sec"), SpatialRound(SimTime, 100.0));
	Root->SetNumberField(TEXT("tick_rate_hz"), 1.0 / TickInterval);
	Root->SetObjectField(TEXT("level_layout"), Layout.IsValid() ? Layout.ToSharedRef() : MakeShared<FJsonObject>());
	Root->SetArrayField(TEXT("frames"), Frames);
	Root->SetArrayField(TEXT("events"), Events);
	Root->SetObjectField(TEXT("summary"), Summary);

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const FString Path = GetDirectory() / FString::Printf(TEXT("run_%s.json"), *SessionId);
	IFileManager::Get().MakeDirectory(*GetDirectory(), true);
	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[SpatialTelemetry] could not write %s"), *Path);
		return FString();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[SpatialTelemetry] %s (frames %d, events %d)"), *Path, Frames.Num(), Events.Num());
	return Path;
}
