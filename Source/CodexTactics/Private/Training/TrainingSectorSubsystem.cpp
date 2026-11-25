#include "Training/TrainingSectorSubsystem.h"

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Components/BoxComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/LootCrateActor.h"
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

void UTrainingSectorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (Active)
	{
		EnsureFightTick();
	}
}

void UTrainingSectorSubsystem::Deinitialize()
{
	StopFightTick();
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

bool UTrainingSectorSubsystem::SelectSector(int32 Id, TOptional<int32> SeedOverride)
{
	UWorld* World = GetWorld();
	if (!IsTrainingMap() || World->GetNetMode() != NM_Standalone || Id < 0 || Id > 6)
	{
		return false;
	}
	if (Id == GetSectorId() && !SeedOverride)
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
		EndFight();
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
		if (SeedOverride)
		{
			Seed = *SeedOverride;
		}
		else if (!FParse::Value(FCommandLine::Get(), TEXT("SectorSeed="), Seed))
		{
			Seed = FMath::Rand();
		}
		TrainingSectorRules::Pick(*Active, Seed, Start, Encounter);
		Fight = TrainingSectorRules::FightConfig(*Json, Id);
		BeginFight(Seed);
		UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Sector %d: %.1f x %.1f m, seed %d, start %s, post %s"), Id,
			Active->Area.GetSize().X / 100.0, Active->Area.GetSize().Y / 100.0, Seed, *Start.Position.ToCompactString(), *Encounter.Post.Id);
	}
	if (World->HasBegunPlay())
	{
		if (USessionRecordingSubsystem* Recorder = USessionRecordingSubsystem::GetIfRecording(World))
		{
			Recorder->OnTrainingSectorChanged();
		}
		MovePlayersToSector(!SeedOverride.IsSet());
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

namespace TrainingSectorFightRuntime
{
	bool IsUp(const AOperativeCharacter& Op)
	{
		const bool bAlive = Op.HealthComponent && Op.HealthComponent->IsAlive() && !Op.IsKilledInAction();
		return !TrainingSectorRules::IsFighterDown(bAlive, Op.IsKnockedDown());
	}

	void CountSide(const AOperativeCharacter& Op, FSectorSideCount& Side)
	{
		if (Op.IsActorBeingDestroyed())
		{
			return;
		}
		++Side.Members;
		Side.Fighting += IsUp(Op) ? 1 : 0;
	}
}

void UTrainingSectorSubsystem::BeginFight(int32 Seed)
{
	FightSeed = Seed;
	RoundNumber = 0;
	ScoreVictory = 0;
	ScoreDefeat = 0;
	ScoreDraw = 0;
	RoundKillsPlayer = 0;
	RoundKillsPmc = 0;
	FMemory::Memzero(RoundShotsPlayer);
	FMemory::Memzero(RoundHitsPlayer);
	FMemory::Memzero(RoundShotsPmc);
	FMemory::Memzero(RoundHitsPmc);
	Banner.Empty();
	BannerUntil = -1.0;
	RoundStartedAt = 0.0;
	LastContactAt = 0.0;
	RoundRelocations = 0;
	LastRelocateDistanceM = -1.f;
	bLastRelocateLos = false;
	IntermissionUntil = 0.0;
	RespawnGraceUntil = -1.0;
	LastResult = ESectorRoundEnd::None;
	Phase = ESectorRoundPhase::Waiting;
	bRespawnApplied = false;
	Watched.Reset();
	CurrentPair = Active ? TrainingSectorRules::PairOf(Active.GetValue(), Start, Encounter) : FSectorPair();
	// UWorld::BeginPlay calls subsystem OnWorldBeginPlay before GameMode::StartPlay, and Prepare
	// selects the sector before Super::StartPlay, so HasBegunPlay is still false here. The ticker
	// has to start now: OnWorldBeginPlay will not run again.
	EnsureFightTick();
}

void UTrainingSectorSubsystem::EndFight()
{
	Phase = ESectorRoundPhase::Idle;
	Banner.Empty();
	BannerUntil = -1.0;
	LastContactAt = 0.0;
	RoundRelocations = 0;
	LastRelocateDistanceM = -1.f;
	bLastRelocateLos = false;
	Watched.Reset();
	RoundNumber = 0;
	StopFightTick();
}

void UTrainingSectorSubsystem::EnsureFightTick()
{
	UWorld* World = GetWorld();
	if (!World || !Active || FightTicker.IsValid())
	{
		return;
	}
	FightTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTrainingSectorSubsystem::TickFight), 0.f);
}

void UTrainingSectorSubsystem::StopFightTick()
{
	if (FightTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FightTicker);
		FightTicker.Reset();
	}
}

bool UTrainingSectorSubsystem::TickFight(float)
{
	if (!Active)
	{
		FightTicker.Reset();
		return false;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		FightTicker.Reset();
		return false;
	}
	const double Now = World->GetTimeSeconds();
	switch (Phase)
	{
	case ESectorRoundPhase::Waiting: TryStartRound(Now); break;
	case ESectorRoundPhase::Fighting: TickFighting(Now); break;
	case ESectorRoundPhase::Intermission: TickIntermission(Now); break;
	default: break;
	}
	return Active.IsSet();
}

FSectorRoundView UTrainingSectorSubsystem::GetRoundView() const
{
	FSectorRoundView View;
	View.Phase = Phase;
	View.LastResult = LastResult;
	View.Number = RoundNumber;
	View.Seed = FightSeed;
	View.ScoreVictory = ScoreVictory;
	View.ScoreDefeat = ScoreDefeat;
	View.ScoreDraw = ScoreDraw;
	View.KillsPlayer = RoundKillsPlayer;
	View.KillsPmc = RoundKillsPmc;
	View.Pair = CurrentPair;
	return View;
}

FString UTrainingSectorSubsystem::GetRoundBanner() const
{
	const UWorld* World = GetWorld();
	return Banner.IsEmpty() || !World || World->GetTimeSeconds() > BannerUntil ? FString() : Banner;
}

void UTrainingSectorSubsystem::TryStartRound(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FSectorSideCount Players;
	FSectorSideCount Pmc;
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		TrainingSectorFightRuntime::CountSide(**It, It->IsPlayerTeam() ? Players : Pmc);
	}
	if (Players.Members < 1 || Pmc.Members < 1)
	{
		return;
	}
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		It->ResetLauncherRoundCounter();
	}
	++RoundNumber;
	RoundStartedAt = Now;
	LastContactAt = Now;
	RoundRelocations = 0;
	LastRelocateDistanceM = -1.f;
	bLastRelocateLos = false;
	RespawnGraceUntil = (RoundNumber > 1 && Fight.RespawnGraceS > 0.f) ? (Now + Fight.RespawnGraceS) : -1.0;
	RoundKillsPlayer = 0;
	RoundKillsPmc = 0;
	FMemory::Memzero(RoundShotsPlayer);
	FMemory::Memzero(RoundHitsPlayer);
	FMemory::Memzero(RoundShotsPmc);
	FMemory::Memzero(RoundHitsPmc);
	Watched.Reset();
	Phase = ESectorRoundPhase::Fighting;
	NoteKills();
	RecordRound(true, ESectorRoundEnd::None, 0.0);
	UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Round %d start sector %d seed %d start %d post %s"), RoundNumber,
		GetSectorId(), FightSeed, CurrentPair.StartIndex, *Encounter.Post.Id);
}

bool UTrainingSectorSubsystem::HasStartPostLos(const FTrainingSector& Sector, const FSectorPair& Pair) const
{
	const UWorld* World = GetWorld();
	if (!World || !Sector.Starts.IsValidIndex(Pair.StartIndex) || !Sector.Posts.IsValidIndex(Pair.PostIndex))
	{
		return false;
	}
	const FTrainingSectorStart& S = Sector.Starts[Pair.StartIndex];
	const FPmcEncounterPost& P = Sector.Posts[Pair.PostIndex];
	const FRotator StartFacing = S.Transform().Rotator();
	const FRotator PostFacing(0.0, P.YawDeg, 0.0);
	const FVector PostRight = FRotationMatrix(PostFacing).GetScaledAxis(EAxis::Y);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SectorRespawnLos), false);
	const FVector EyeOffset(0.0, 0.0, 160.0);
	for (int32 StartSlot = 0; StartSlot < 3; ++StartSlot)
	{
		const FVector StartEye = S.Position + StartFacing.RotateVector(FVector(0.0, StartSlot * 120.0, 0.0)) + EyeOffset;
		for (int32 PmcSlot = 0; PmcSlot < 3; ++PmcSlot)
		{
			const FVector PmcEye = P.Pos + PostRight * ((PmcSlot - 1) * 150.0) + EyeOffset;
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, StartEye, PmcEye, ECC_Visibility, Params))
			{
				return true;
			}
		}
	}
	return false;
}

void UTrainingSectorSubsystem::TickFighting(double Now)
{
	if (RespawnGraceUntil > 0.0)
	{
		const bool bInGrace = Now < RespawnGraceUntil;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				if (!It->IsPlayerTeam())
				{
					It->bTacticalCeaseFire = bInGrace;
				}
			}
		}
		if (!bInGrace)
		{
			RespawnGraceUntil = -1.0;
		}
	}
	RefillReserves(Now);
	NoteKills();
	SyncContactClock(Now);
	UWorld* World = GetWorld();
	if (!World || !Active)
	{
		return;
	}
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bPausedMode = Flow && (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause || Flow->GetCombatMode() == ECodexCombatMode::TurnBased);
	if (!bPausedMode && TrainingSectorRules::ShouldRelocateNoContact(Now, LastContactAt, Fight.NoContactRelocateS, RoundRelocations,
		Fight.RelocateMaxPerRound, RespawnGraceUntil > 0.0))
	{
		RelocatePmcSquad(Now);
	}
	FSectorSideCount Players;
	FSectorSideCount Pmc;
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		TrainingSectorFightRuntime::CountSide(**It, It->IsPlayerTeam() ? Players : Pmc);
	}
	const ESectorRoundEnd Result = TrainingSectorRules::EvaluateRound(Players, Pmc, Now - RoundStartedAt, Fight.RoundTimeLimitS);
	if (Result == ESectorRoundEnd::None)
	{
		return;
	}
	switch (Result)
	{
	case ESectorRoundEnd::Victory: ++ScoreVictory; break;
	case ESectorRoundEnd::Defeat: ++ScoreDefeat; break;
	case ESectorRoundEnd::Draw: ++ScoreDraw; break;
	default: break;
	}
	LastResult = Result;
	Banner = TrainingSectorRules::RoundBanner(RoundNumber, Result);
	BannerUntil = Now + TrainingSectorRules::BannerSeconds;
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		It->bTacticalCeaseFire = true;
	}
	bool bPairFallback = false;
	NextPair = TrainingSectorRules::PickSafeRoundPair(*Active, FightSeed, RoundNumber + 1, CurrentPair, Fight.MinStartPostDistanceM,
		[this](const FSectorPair& Candidate) { return Active && HasStartPostLos(*Active, Candidate); }, &bPairFallback);
	if (bPairFallback)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] No start/post pair >= %.0f m without LOS; using farthest (%d, %d)"),
			Fight.MinStartPostDistanceM, NextPair.StartIndex, NextPair.PostIndex);
	}
	bRespawnApplied = false;
	Phase = ESectorRoundPhase::Intermission;
	IntermissionUntil = Now + Fight.RoundRestartDelayS;
	RecordRound(false, Result, Now - RoundStartedAt);
	UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Round %d end %s kills %d/%d playerShots=%d+%d+%d+%d(%d+%d+%d+%d) pmcShots=%d+%d+%d+%d(%d+%d+%d+%d) restart in %.1f s"),
		RoundNumber, TrainingSectorRules::RoundEndName(Result), RoundKillsPlayer, RoundKillsPmc,
		RoundShotsPlayer[0], RoundShotsPlayer[1], RoundShotsPlayer[2], RoundShotsPlayer[3],
		RoundHitsPlayer[0], RoundHitsPlayer[1], RoundHitsPlayer[2], RoundHitsPlayer[3],
		RoundShotsPmc[0], RoundShotsPmc[1], RoundShotsPmc[2], RoundShotsPmc[3],
		RoundHitsPmc[0], RoundHitsPmc[1], RoundHitsPmc[2], RoundHitsPmc[3],
		Fight.RoundRestartDelayS);
}

void UTrainingSectorSubsystem::TickIntermission(double Now)
{
	if (Now < IntermissionUntil)
	{
		return;
	}
	if (RespawnRound())
	{
		Phase = ESectorRoundPhase::Waiting;
	}
}

bool UTrainingSectorSubsystem::RespawnRound()
{
	UWorld* World = GetWorld();
	ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	UPmcEncounterSubsystem* Encounters = World ? UPmcEncounterSubsystem::Get(World) : nullptr;
	UPmcSquadSubsystem* Pmc = World ? UPmcSquadSubsystem::Get(World) : nullptr;
	if (!World || !GameMode || !Encounters || !Pmc || !Active || GameMode->SquadRoster.IsEmpty() || !NextPair.IsValid())
	{
		return false;
	}
	if (!bRespawnApplied)
	{
		TrainingSectorRules::ApplyPair(Active.GetValue(), NextPair, FightSeed, Start, Encounter);
		CurrentPair = NextPair;
		for (const int32 Team : Pmc->GetTeamIds())
		{
			if (Team > 0)
			{
				Pmc->DestroyTeam(Team);
			}
		}
		TArray<AOperativeCharacter*> Existing;
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			Existing.Add(*It);
		}
		for (AOperativeCharacter* Op : Existing)
		{
			if (Op && !Op->IsActorBeingDestroyed())
			{
				Op->Destroy();
			}
		}
		TArray<ALootCrateActor*> StaleCorpseLoot;
		for (TActorIterator<ALootCrateActor> It(World); It; ++It)
		{
			if (*It && It->ActorHasTag(TEXT("CorpseLoot")))
			{
				StaleCorpseLoot.Add(*It);
			}
		}
		for (ALootCrateActor* Crate : StaleCorpseLoot)
		{
			if (Crate && !Crate->IsActorBeingDestroyed())
			{
				Crate->Destroy();
			}
		}
		Encounters->RestartSchedule();
		bRespawnApplied = true;
		Watched.Reset();
	}
	int32 Spawned = 0;
	const FRotator Facing = Start.Transform().Rotator();
	for (int32 Index = 0; Index < GameMode->SquadRoster.Num(); ++Index)
	{
		const FSquadMemberSpawn& Member = GameMode->SquadRoster[Index];
		bool bApplyBody = false;
		const TSubclassOf<AOperativeCharacter> Class = GameMode->ResolveMemberClass(Member, bApplyBody);
		const AOperativeCharacter* Defaults = Class ? Class->GetDefaultObject<AOperativeCharacter>() : nullptr;
		const double Half = Defaults ? Defaults->GetSimpleCollisionHalfHeight() : 88.0;
		const FVector Feet = Start.Position + Facing.RotateVector(Member.Offset);
		if (GameMode->SpawnOperative(Member, Index, PlacePlayer(Feet, Half), Facing))
		{
			++Spawned;
		}
	}
	if (Spawned <= 0)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] Respawn spawned nobody; retrying."));
		return false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Respawn start %d post %s players %d"), CurrentPair.StartIndex, *Encounter.Post.Id, Spawned);
	return true;
}

void UTrainingSectorSubsystem::NoteKills()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		AOperativeCharacter* Op = *It;
		if (!Op || Op->IsActorBeingDestroyed())
		{
			continue;
		}
		const bool bOut = !TrainingSectorFightRuntime::IsUp(*Op);
		FSectorFighterWatch& Watch = Watched.FindOrAdd(Op);
		if (!Watch.bSeen)
		{
			Watch.bSeen = true;
			Watch.bOut = bOut;
			continue;
		}
		if (!Watch.bOut && bOut)
		{
			Watch.bOut = true;
			if (Op->IsPlayerTeam())
			{
				++RoundKillsPmc;
			}
			else
			{
				++RoundKillsPlayer;
			}
		}
	}
}

void UTrainingSectorSubsystem::RefillReserves(double Now)
{
	if (Fight.AmmoMode != ESectorAmmoMode::Resupply)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		AOperativeCharacter* Op = *It;
		if (!Op || Op->IsActorBeingDestroyed())
		{
			continue;
		}
		FSectorFighterWatch& Watch = Watched.FindOrAdd(Op);
		const UWeaponDataAsset* Weapon = Op->CurrentWeapon;
		const bool bReady = TrainingSectorFightRuntime::IsUp(*Op) && Op->UsesAmmo() && Weapon && !Op->IsLauncherEquipped() && Op->ReserveAmmo <= 0;
		const int32 Magazine = Weapon ? FMath::Max(1, Weapon->Stats.Ammo.MagazineSize) : 1;
		if (!bReady || Op->CurrentClip * 3 > Magazine)
		{
			Watch.EmptySince = -1.0;
			continue;
		}
		if (Watch.EmptySince < 0.0)
		{
			Watch.EmptySince = Now;
		}
		FSectorResupplyQuery Query;
		Query.bUsesAmmo = true;
		Query.bHasWeapon = true;
		Query.Reserve = Op->ReserveAmmo;
		Query.Clip = Op->CurrentClip;
		Query.Magazine = Magazine;
		Query.WeaponReserve = Weapon->Stats.Ammo.ReserveAmmo;
		Query.SecondsQuiet = TrainingSectorRules::QuietSeconds(Now, Watch.EmptySince, Op->GetLastShotTime());
		if (!TrainingSectorRules::ShouldRefillReserve(Query, Fight.AmmoMode, Fight.ResupplyDelayS) || TrainingSectorRules::ResupplyTouchesGrenades())
		{
			continue;
		}
		const int32 Filled = TrainingSectorRules::RefilledReserve(Magazine, Weapon->Stats.Ammo.ReserveAmmo);
		Op->ReserveAmmo = Filled;
		FWeaponAmmoState& Stored = Op->AmmoInventory.FindOrAdd(Weapon->WeaponId);
		Stored.Reserve = Filled;
		Stored.Clip = Op->CurrentClip;
		Watch.EmptySince = -1.0;
		UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Resupply %s %s reserve %d"), *Op->GetName(), *Weapon->WeaponId, Filled);
	}
}

void UTrainingSectorSubsystem::RecordShot(const AOperativeCharacter& Shooter, const AActor* Target, bool bHit)
{
	if (Phase != ESectorRoundPhase::Fighting || !Target)
	{
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		LastContactAt = FMath::Max(LastContactAt, World->GetTimeSeconds());
	}
	const float DistM = static_cast<float>(FVector::Dist2D(Shooter.GetActorLocation(), Target->GetActorLocation())) / 100.f;
	const int32 Bucket = FDemoRoundEvent::BucketForDistanceM(DistM);
	if (Shooter.IsPlayerTeam())
	{
		++RoundShotsPlayer[Bucket];
		if (bHit) { ++RoundHitsPlayer[Bucket]; }
	}
	else
	{
		++RoundShotsPmc[Bucket];
		if (bHit) { ++RoundHitsPmc[Bucket]; }
	}
}

void UTrainingSectorSubsystem::RecordRound(bool bStart, ESectorRoundEnd Result, double Duration) const
{
	USessionRecordingSubsystem* Recorder = USessionRecordingSubsystem::GetIfRecording(this);
	if (!Recorder)
	{
		return;
	}
	FDemoRoundEvent Event;
	Event.bStart = bStart;
	Event.SectorId = GetSectorId();
	Event.Seed = FightSeed;
	Event.Round = RoundNumber;
	Event.StartIndex = CurrentPair.StartIndex;
	Event.PostIndex = CurrentPair.PostIndex;
	Event.KillsPlayer = RoundKillsPlayer;
	Event.KillsPmc = RoundKillsPmc;
	FMemory::Memcpy(Event.ShotsPlayer, RoundShotsPlayer, sizeof(RoundShotsPlayer));
	FMemory::Memcpy(Event.HitsPlayer, RoundHitsPlayer, sizeof(RoundHitsPlayer));
	FMemory::Memcpy(Event.ShotsPmc, RoundShotsPmc, sizeof(RoundShotsPmc));
	FMemory::Memcpy(Event.HitsPmc, RoundHitsPmc, sizeof(RoundHitsPmc));
	Event.ScoreVictory = ScoreVictory;
	Event.ScoreDefeat = ScoreDefeat;
	Event.ScoreDraw = ScoreDraw;
	Event.DurationS = static_cast<float>(Duration);
	switch (Result)
	{
	case ESectorRoundEnd::Victory: Event.Result = EDemoRoundResult::Victory; break;
	case ESectorRoundEnd::Defeat: Event.Result = EDemoRoundResult::Defeat; break;
	case ESectorRoundEnd::Draw: Event.Result = EDemoRoundResult::Draw; break;
	default: Event.Result = EDemoRoundResult::None; break;
	}
	Recorder->RecordSectorRound(Event);
}

void UTrainingSectorSubsystem::SyncContactClock(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
	{
		const double Latest = Pmc->GetLatestContactActivityTime();
		if (Latest <= Now && Latest > LastContactAt)
		{
			LastContactAt = Latest;
		}
	}
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		if (*It && !It->IsActorBeingDestroyed())
		{
			const double ShotAt = It->GetLastShotTime();
			if (ShotAt <= Now && ShotAt > LastContactAt)
			{
				LastContactAt = ShotAt;
			}
		}
	}
}

bool UTrainingSectorSubsystem::RelocatePmcSquad(double Now)
{
	UWorld* World = GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
	if (!World || !Nav || !Pmc || !Active)
	{
		return false;
	}
	TArray<AOperativeCharacter*> Players;
	TArray<AOperativeCharacter*> Pmcs;
	FVector PlayerCentroid = FVector::ZeroVector;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SectorRelocate), false);
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		AOperativeCharacter* Op = *It;
		if (!Op || Op->IsActorBeingDestroyed())
		{
			continue;
		}
		Params.AddIgnoredActor(Op);
		if (!TrainingSectorFightRuntime::IsUp(*Op))
		{
			continue;
		}
		if (Op->IsPlayerTeam())
		{
			Players.Add(Op);
			PlayerCentroid += Op->GetActorLocation();
		}
		else
		{
			Pmcs.Add(Op);
		}
	}
	if (Players.IsEmpty() || Pmcs.IsEmpty())
	{
		return false;
	}
	PlayerCentroid /= static_cast<double>(Players.Num());

	auto ProjectGroundNav = [&](const FVector2D& XY, FVector& OutNavLoc) -> bool
	{
		if (!TrainingSectorRules::Contains(Active->Area, FVector(XY.X, XY.Y, 0.0)))
		{
			return false;
		}
		FHitResult Ground;
		const bool bHit = World->LineTraceSingleByChannel(Ground, FVector(XY.X, XY.Y, Active->Heights.Y + 500.0),
			FVector(XY.X, XY.Y, Active->Heights.X - 500.0), ECC_WorldStatic, Params);
		if (!bHit || Ground.ImpactNormal.Z <= 0.7f || Ground.ImpactPoint.Z > 110.0)
		{
			return false;
		}
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(120.0, 120.0, 120.0)))
		{
			return false;
		}
		if (OnNav.Location.Z > 110.0 || FMath::Abs(OnNav.Location.Z - Ground.ImpactPoint.Z) > 60.0)
		{
			return false;
		}
		if (!Allows(World, OnNav.Location, PlacementInset))
		{
			return false;
		}
		OutNavLoc = OnNav.Location;
		return true;
	};

	auto HasPlayerLos = [&](const FVector& Spot) -> bool
	{
		const FVector Eye = Spot + FVector(0.0, 0.0, 160.0);
		for (const AOperativeCharacter* Player : Players)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Eye, UTacticalSightSubsystem::EyePoint(*Player), ECC_Visibility, Params))
			{
				return true;
			}
		}
		return false;
	};

	auto HasNearbyCover = [&](const FVector& Spot) -> bool
	{
		const FVector Origin = Spot + FVector(0.0, 0.0, 65.0);
		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const double Rad = static_cast<double>(Dir) * (UE_DOUBLE_PI / 4.0);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Origin, Origin + FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0) * 180.0, ECC_WorldStatic, Params))
			{
				return true;
			}
		}
		return false;
	};

	const float TargetM = FMath::Max(5.f, Fight.RelocateDistanceM);
	const float BandHalfM = 5.f;
	const float RadiiM[] = {
		TargetM,
		TargetM + 2.f,
		FMath::Max(5.f, TargetM - 2.f),
		TargetM + 4.f,
		FMath::Max(5.f, TargetM - 4.f),
		TargetM + BandHalfM,
		FMath::Max(5.f, TargetM - BandHalfM),
		TargetM + BandHalfM + 4.f,
		TargetM + BandHalfM + 10.f
	};
	constexpr int32 AngleCount = 16;
	TArray<FSectorRelocateCandidate> Candidates;
	Candidates.Reserve(UE_ARRAY_COUNT(RadiiM) * AngleCount);
	for (const float RM : RadiiM)
	{
		const double RadiusCm = static_cast<double>(RM) * 100.0;
		for (int32 A = 0; A < AngleCount; ++A)
		{
			const double Theta = static_cast<double>(A) * (2.0 * UE_DOUBLE_PI / static_cast<double>(AngleCount));
			const FVector2D XY(PlayerCentroid.X + FMath::Cos(Theta) * RadiusCm, PlayerCentroid.Y + FMath::Sin(Theta) * RadiusCm);
			FVector Spot;
			if (!ProjectGroundNav(XY, Spot))
			{
				continue;
			}
			FSectorRelocateCandidate& Cand = Candidates.AddDefaulted_GetRef();
			Cand.Location = Spot;
			Cand.DistToCentroidM = static_cast<float>(FVector::Dist2D(Spot, PlayerCentroid) / 100.0);
			float MinP = TNumericLimits<float>::Max();
			for (const AOperativeCharacter* Player : Players)
			{
				MinP = FMath::Min(MinP, static_cast<float>(FVector::Dist2D(Spot, Player->GetActorLocation()) / 100.0));
			}
			Cand.MinPlayerDistM = MinP;
			Cand.bHasLos = HasPlayerLos(Spot);
			Cand.bNearCover = HasNearbyCover(Spot);
		}
	}

	const int32 PickSeed = FightSeed + RoundNumber * 31 + (RoundRelocations + 1) * 7;
	const FSectorRelocatePick Pick = TrainingSectorRules::PickRelocatePoint(Candidates, TargetM, BandHalfM, Fight.MinStartPostDistanceM, PickSeed);
	if (!Pick.bValid)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TrainingSector] Round %d: no valid no-contact relocate spot found (>= %.0f m); resetting clock"),
			RoundNumber, Fight.MinStartPostDistanceM);
		LastContactAt = Now;
		return false;
	}

	const FRotator Facing = (PlayerCentroid - Pick.Location).GetSafeNormal2D().Rotation();
	const FVector Right = FRotationMatrix(Facing).GetScaledAxis(EAxis::Y);
	const int32 Count = Pmcs.Num();
	TSet<int32> RelocatedTeams;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		AOperativeCharacter* Fighter = Pmcs[Index];
		const FVector Offset = Right * ((static_cast<double>(Index) - static_cast<double>(Count - 1) * 0.5) * 140.0);
		FVector SlotNav = Pick.Location;
		FVector TrySpot;
		if (ProjectGroundNav(FVector2D(Pick.Location.X + Offset.X, Pick.Location.Y + Offset.Y), TrySpot))
		{
			float MinSlotPlayerM = TNumericLimits<float>::Max();
			for (const AOperativeCharacter* Player : Players)
			{
				MinSlotPlayerM = FMath::Min(MinSlotPlayerM, static_cast<float>(FVector::Dist2D(TrySpot, Player->GetActorLocation()) / 100.0));
			}
			if (MinSlotPlayerM >= Fight.MinStartPostDistanceM && (Pick.bHasLos || !HasPlayerLos(TrySpot)))
			{
				SlotNav = TrySpot;
			}
		}
		Fighter->StopOperative();
		if (Fighter->bInCover)
		{
			Fighter->LeaveCover(TEXT("sector no-contact relocate"));
		}
		Fighter->ClearPendingCover();
		const FVector Dest(SlotNav.X, SlotNav.Y, SlotNav.Z + Fighter->GetSimpleCollisionHalfHeight() + 2.0);
		Fighter->TeleportTo(Dest, Facing, false, true);
		RelocatedTeams.Add(Fighter->TeamId);
	}
	for (const int32 TeamId : RelocatedTeams)
	{
		Pmc->EngageRelocatedTeam(TeamId, PlayerCentroid);
	}
	++RoundRelocations;
	LastContactAt = Now;
	LastRelocateDistanceM = Pick.DistToCentroidM;
	bLastRelocateLos = Pick.bHasLos;
	if (USessionRecordingSubsystem* Recorder = USessionRecordingSubsystem::GetIfRecording(this))
	{
		FDemoRoundEvent Event;
		Event.bPmcRelocated = true;
		Event.SectorId = GetSectorId();
		Event.Seed = FightSeed;
		Event.Round = RoundNumber;
		Event.RelocateDistanceM = Pick.DistToCentroidM;
		Event.bRelocateLos = Pick.bHasLos;
		Recorder->RecordSectorRound(Event);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TrainingSector] Round %d pmc_relocated #%d/%d: %.1f m from squad (LOS %s, cover %s, fallback %s) to %s"),
		RoundNumber, RoundRelocations, Fight.RelocateMaxPerRound, Pick.DistToCentroidM,
		Pick.bHasLos ? TEXT("yes") : TEXT("no"), Pick.bNearCover ? TEXT("yes") : TEXT("no"),
		Pick.bFallback ? TEXT("yes") : TEXT("no"), *Pick.Location.ToCompactString());
	return true;
}
