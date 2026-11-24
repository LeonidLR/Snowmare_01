#include "Training/TrainingSectorSubsystem.h"

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Telemetry/SessionRecordingRules.h"
#include "Telemetry/SessionRecordingSubsystem.h"

namespace TrainingSectorRuntime
{
	constexpr double WallHalfWidth = 10.0;
	constexpr double HeightPadding = 1000.0;
	constexpr double OutlineStep = 150.0;
	constexpr double PlacementInset = 135.0;

	TSharedPtr<FJsonObject> ReadSectorJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		}
		return Json;
	}

	void ClearBoundary(TArray<TWeakObjectPtr<AActor>>& Actors)
	{
		for (const TWeakObjectPtr<AActor>& Ref : Actors)
		{
			if (AActor* Actor = Ref.Get())
			{
				Actor->Destroy();
			}
		}
		Actors.Reset();
	}

	AActor* NewBoundaryActor(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<AActor>(Params);
	}

	void AddRoot(AActor& Actor, const FVector& Location)
	{
		USceneComponent* Root = NewObject<USceneComponent>(&Actor);
		Actor.SetRootComponent(Root);
		Actor.AddInstanceComponent(Root);
		Root->RegisterComponent();
		Actor.SetActorLocation(Location);
	}

	bool AddOutsideNavBox(UWorld& World, const FBox& Bounds, TArray<TWeakObjectPtr<AActor>>& Out)
	{
		AActor* Actor = NewBoundaryActor(World);
		if (!Actor)
		{
			return false;
		}
		Out.Add(Actor);
		AddRoot(*Actor, Bounds.GetCenter());
		UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(Actor);
		Actor->AddInstanceComponent(Modifier);
		Modifier->FailsafeExtent = Bounds.GetExtent();
		Modifier->SetAreaClass(UNavArea_Null::StaticClass());
		Modifier->RegisterComponent();
		return true;
	}

	bool AddPawnWall(UWorld& World, const FVector& Centre, const FVector& Extent, TArray<TWeakObjectPtr<AActor>>& Out)
	{
		AActor* Actor = NewBoundaryActor(World);
		if (!Actor)
		{
			return false;
		}
		Out.Add(Actor);
		AddRoot(*Actor, Centre);
		UBoxComponent* Wall = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Wall);
		Wall->SetupAttachment(Actor->GetRootComponent());
		Wall->InitBoxExtent(Extent);
		Wall->SetCanEverAffectNavigation(false);
		Wall->SetCollisionObjectType(ECC_WorldStatic);
		Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
		Wall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Wall->SetGenerateOverlapEvents(false);
		Wall->RegisterComponent();
		return true;
	}

	bool AddGroundOutline(UWorld& World, const FTrainingSector& Sector, TArray<TWeakObjectPtr<AActor>>& Out)
	{
		AActor* Actor = NewBoundaryActor(World);
		if (!Actor)
		{
			return false;
		}
		Out.Add(Actor);
		AddRoot(*Actor, FVector::ZeroVector);
		ULineBatchComponent* Lines = NewObject<ULineBatchComponent>(Actor);
		Actor->AddInstanceComponent(Lines);
		Lines->SetupAttachment(Actor->GetRootComponent());
		Lines->SetCanEverAffectNavigation(false);
		Lines->RegisterComponent();

		const FBox2D& Area = Sector.Area;
		const FVector2D Corners[] = { Area.Min, FVector2D(Area.Max.X, Area.Min.Y), Area.Max, FVector2D(Area.Min.X, Area.Max.Y) };
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TrainingSectorOutline), false, Actor);
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		TArray<FBatchedLine> Outline;
		for (int32 Edge = 0; Edge < UE_ARRAY_COUNT(Corners); ++Edge)
		{
			const FVector2D A = Corners[Edge];
			const FVector2D B = Corners[(Edge + 1) % UE_ARRAY_COUNT(Corners)];
			const int32 Steps = FMath::CeilToInt(FMath::Min(FVector2D::Distance(A, B) / OutlineStep, 2048.0));
			TOptional<FVector> Previous;
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const FVector2D XY = FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
				FHitResult Ground;
				if (World.LineTraceSingleByChannel(Ground, FVector(XY, Sector.Heights.Y + HeightPadding),
					FVector(XY, Sector.Heights.X - HeightPadding), ECC_Visibility, Params) && Ground.ImpactNormal.Z > 0.5f)
				{
					const FVector Point = Ground.ImpactPoint + FVector(0.0, 0.0, 8.0);
					if (Previous)
					{
						Outline.Emplace(*Previous, Point, FLinearColor(1.f, 0.62f, 0.12f), -1.f, 5.f, SDPG_World);
					}
					Previous = Point;
				}
				else
				{
					Previous.Reset();
				}
			}
		}
		Lines->DrawLines(Outline);
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs SectorCommand(TEXT("Codex.Sector"),
		TEXT("Select an X6 training sector (1–6). 0 restores the whole map. Does not edit the saved selection."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const TOptional<int32> Id = Args.Num() == 1 ? TrainingSectorRules::ParseId(Args[0]) : TOptional<int32>();
			UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(World);
			if (!Id || !Training || !Training->SelectSector(*Id))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Sector expects an available X6 sector id 0–6."));
			}
		}));
}

using namespace TrainingSectorRuntime;

UTrainingSectorSubsystem* UTrainingSectorSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UTrainingSectorSubsystem>() : nullptr;
}

FVector UTrainingSectorSubsystem::LimitGoal(const UWorld* World, const FVector& Point, double Inset)
{
	const UTrainingSectorSubsystem* Training = Get(World);
	const FBox2D* Area = Training ? Training->GetArea() : nullptr;
	return Area ? TrainingSectorRules::Clamp(*Area, Point, Inset) : Point;
}

bool UTrainingSectorSubsystem::Allows(const UWorld* World, const FVector& Point, double Inset)
{
	return LimitGoal(World, Point, Inset).Equals(Point, UE_KINDA_SMALL_NUMBER);
}

bool UTrainingSectorSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTrainingSectorSubsystem::Deinitialize()
{
	ClearBoundary(BoundaryActors);
	Super::Deinitialize();
}

bool UTrainingSectorSubsystem::IsTrainingMap() const
{
	const UWorld* World = GetWorld();
	return World && SessionRecordingRules::CleanMapName(FPaths::GetCleanFilename(World->GetMapName())) == TrainingSectorRules::MapName;
}

void UTrainingSectorSubsystem::Prepare()
{
	if (!IsTrainingMap())
	{
		return;
	}
	FString Argument;
	int32 Id = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Sector="), Argument))
	{
		const TOptional<int32> Parsed = TrainingSectorRules::ParseId(Argument);
		if (!Parsed)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] Invalid -Sector=%s. Using the whole map."), *Argument);
			return;
		}
		Id = *Parsed;
	}
	else if (const TSharedPtr<FJsonObject> Saved = ReadSectorJson(FPaths::ProjectSavedDir() / TEXT("Training/active_sector.json")))
	{
		Id = TrainingSectorRules::SavedId(*Saved);
	}
	if (Id != 0 && !SelectSector(Id))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] Sector %d unavailable. Using the whole map."), Id);
	}
}

bool UTrainingSectorSubsystem::SelectSector(int32 Id)
{
	UWorld* World = GetWorld();
	if (!IsTrainingMap() || World->GetNetMode() != NM_Standalone || Id < 0 || Id > 6)
	{
		return false;
	}
	if (Id == GetSectorId())
	{
		return true;
	}
	if (World->HasBegunPlay())
	{
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			const AOperativeCharacter& Op = **It;
			if (Op.IsPlayerTeam() && (Op.IsMantling() || Op.IsMantleDescending() || Op.IsVaulting() || Op.IsClimbingLadder() || Op.IsDroppingOffLedge()))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Wait for the current traversal to finish before switching."));
				return false;
			}
		}
	}
	if (Id == 0)
	{
		ClearBoundary(BoundaryActors);
		Active.Reset();
	}
	else
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Gen_Depot_1_X6.sectors.json");
		const TSharedPtr<FJsonObject> Json = ReadSectorJson(Path);
		if (!Json)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] Cannot read %s"), *Path);
			return false;
		}
		TArray<FString> Errors;
		const TArray<FTrainingSector> Sectors = TrainingSectorRules::ParseSectors(*Json, Errors);
		for (const FString& Error : Errors)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] %s"), *Error);
		}
		const FTrainingSector* Choice = Sectors.FindByPredicate([Id](const FTrainingSector& Sector) { return Sector.Id == Id; });
		TArray<TWeakObjectPtr<AActor>> NewBoundary;
		if (!Choice || !BuildBoundary(*Choice, NewBoundary))
		{
			ClearBoundary(NewBoundary);
			return false;
		}
		ClearBoundary(BoundaryActors);
		BoundaryActors = MoveTemp(NewBoundary);
		Active = *Choice;
		int32 Seed = 0;
		if (!FParse::Value(FCommandLine::Get(), TEXT("SectorSeed="), Seed))
		{
			Seed = FMath::Rand();
		}
		TrainingSectorRules::Pick(*Active, Seed, Start, Encounter);
		UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Sector %d: %.1f x %.1f m, seed %d, start %s, post %s"), Id,
			Active->Area.GetSize().X / 100.0, Active->Area.GetSize().Y / 100.0, Seed, *Start.Position.ToCompactString(), *Encounter.Post.Id);
	}
	if (World->HasBegunPlay())
	{
		if (USessionRecordingSubsystem* Recorder = USessionRecordingSubsystem::GetIfRecording(World))
		{
			Recorder->OnTrainingSectorChanged();
		}
		MovePlayersToSector(true);
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
		{
			for (const int32 Team : Pmc->GetTeamIds())
			{
				if (Team > 0)
				{
					Pmc->DestroyTeam(Team);
				}
			}
		}
		if (UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World))
		{
			Encounters->RestartSchedule();
		}
	}
	return true;
}

FVector UTrainingSectorSubsystem::PlacePlayer(const FVector& Feet, double HalfHeight) const
{
	FVector Position = LimitGoal(GetWorld(), Feet, PlacementInset);
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Position, OnNav, FVector(160.0, 160.0, 180.0)) && Allows(GetWorld(), OnNav.Location, WallHalfWidth + 50.0))
		{
			Position = OnNav.Location;
		}
	}
	Position.Z += HalfHeight + 2.0;
	return Position;
}

void UTrainingSectorSubsystem::MovePlayersToSector(bool bOnlyOutside) const
{
	if (!Active)
	{
		return;
	}
	const FRotator Facing = Start.Transform().Rotator();
	int32 Slot = 0;
	for (TActorIterator<AOperativeCharacter> It(GetWorld()); It; ++It)
	{
		AOperativeCharacter& Op = **It;
		if (!Op.IsPlayerTeam() || Op.IsKilledInAction())
		{
			continue;
		}
		const FVector Offset(0.0, Slot++ * 120.0, 0.0);
		Op.StopOperative(); // Existing navigation paths were built for the previous region.
		if (!bOnlyOutside || !Allows(GetWorld(), Op.GetActorLocation(), WallHalfWidth + Op.GetSimpleCollisionRadius()))
		{
			if (Op.bInCover)
			{
				Op.LeaveCover(TEXT("training sector changed"));
			}
			Op.ClearPendingCover();
			const FVector Destination = PlacePlayer(Start.Position + Facing.RotateVector(Offset), Op.GetSimpleCollisionHalfHeight());
			Op.TeleportTo(Destination, Facing, false, true);
		}
	}
}

bool UTrainingSectorSubsystem::BuildBoundary(const FTrainingSector& Sector, TArray<TWeakObjectPtr<AActor>>& Out) const
{
	UWorld& World = *GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	const FBox NavBounds = Nav ? Nav->GetWorldBounds() : FBox(ForceInit);
	if (!NavBounds.IsValid)
	{
		return false;
	}
	const FBox2D& Area = Sector.Area;
	const double MinZ = FMath::Min(NavBounds.Min.Z, Sector.Heights.X) - HeightPadding;
	const double MaxZ = FMath::Max(NavBounds.Max.Z, Sector.Heights.Y) + HeightPadding;
	const double CentreZ = (MinZ + MaxZ) * 0.5;
	const double HalfHeight = (MaxZ - MinZ) * 0.5;
	const FVector2D Centre = Area.GetCenter();
	const FVector2D HalfSize = Area.GetExtent();
	const FVector Low(FMath::Min(NavBounds.Min.X, Area.Min.X) - WallHalfWidth, FMath::Min(NavBounds.Min.Y, Area.Min.Y) - WallHalfWidth, MinZ);
	const FVector High(FMath::Max(NavBounds.Max.X, Area.Max.X) + WallHalfWidth, FMath::Max(NavBounds.Max.Y, Area.Max.Y) + WallHalfWidth, MaxZ);

	return AddOutsideNavBox(World, FBox(Low, FVector(Area.Min.X, High.Y, MaxZ)), Out)
		&& AddOutsideNavBox(World, FBox(FVector(Area.Max.X, Low.Y, MinZ), High), Out)
		&& AddOutsideNavBox(World, FBox(FVector(Area.Min.X, Low.Y, MinZ), FVector(Area.Max.X, Area.Min.Y, MaxZ)), Out)
		&& AddOutsideNavBox(World, FBox(FVector(Area.Min.X, Area.Max.Y, MinZ), FVector(Area.Max.X, High.Y, MaxZ)), Out)
		&& AddPawnWall(World, FVector(Area.Min.X - WallHalfWidth, Centre.Y, CentreZ), FVector(WallHalfWidth, HalfSize.Y + WallHalfWidth, HalfHeight), Out)
		&& AddPawnWall(World, FVector(Area.Max.X + WallHalfWidth, Centre.Y, CentreZ), FVector(WallHalfWidth, HalfSize.Y + WallHalfWidth, HalfHeight), Out)
		&& AddPawnWall(World, FVector(Centre.X, Area.Min.Y - WallHalfWidth, CentreZ), FVector(HalfSize.X + WallHalfWidth, WallHalfWidth, HalfHeight), Out)
		&& AddPawnWall(World, FVector(Centre.X, Area.Max.Y + WallHalfWidth, CentreZ), FVector(HalfSize.X + WallHalfWidth, WallHalfWidth, HalfHeight), Out)
		&& AddGroundOutline(World, Sector, Out);
}
