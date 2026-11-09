#include "Combat/MortarSubsystem.h"

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/MortarNestActor.h"
#include "Combat/MortarRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

UMortarSubsystem* UMortarSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMortarSubsystem>() : nullptr;
}

void UMortarSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	// After the game mode's StartPlay (the level data, the squad, the PMC subsystem are up).
	TWeakObjectPtr<UMortarSubsystem> WeakThis(this);
	FTimerHandle Handle;
	InWorld.GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis]()
	{
		if (UMortarSubsystem* Self = WeakThis.Get())
		{
			Self->SpawnFromLevelJson();
		}
	}), 0.5f, false);
}

void UMortarSubsystem::RegisterNest(AMortarNestActor* Nest)
{
	Nests.RemoveAll([](const TWeakObjectPtr<AMortarNestActor>& Entry) { return !Entry.IsValid(); });
	Nests.AddUnique(Nest);
}

void UMortarSubsystem::UnregisterNest(AMortarNestActor* Nest)
{
	Nests.RemoveAll([Nest](const TWeakObjectPtr<AMortarNestActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Nest; });
}

TArray<AMortarNestActor*> UMortarSubsystem::GetNests(int32 TeamId) const
{
	TArray<AMortarNestActor*> Out;
	for (const TWeakObjectPtr<AMortarNestActor>& Entry : Nests)
	{
		if (AMortarNestActor* Nest = Entry.Get(); Nest && (TeamId < 0 || Nest->TeamId == TeamId))
		{
			Out.Add(Nest);
		}
	}
	return Out;
}

AMortarNestActor* UMortarSubsystem::FindNestOf(const AOperativeCharacter* Man) const
{
	for (const TWeakObjectPtr<AMortarNestActor>& Entry : Nests)
	{
		if (AMortarNestActor* Nest = Entry.Get(); Nest && Nest->IsCrewing(Man))
		{
			return Nest;
		}
	}
	return nullptr;
}

bool UMortarSubsystem::IsCrewing(const UWorld* World, const AOperativeCharacter* Man)
{
	const UMortarSubsystem* Mortars = Get(World);
	return Mortars && Mortars->FindNestOf(Man) != nullptr;
}

AOperativeCharacter* UMortarSubsystem::FindSpotter(int32 TeamId, const FVector& Point) const
{
	UWorld* World = GetWorld();
	const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	if (!Sight)
	{
		return nullptr;
	}
	TArray<AOperativeCharacter*> Side;
	if (TeamId == 0)
	{
		if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Side = Squad->GetMembers();
		}
	}
	else if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
	{
		Side = Pmc->GetTeamMembers(TeamId);
	}
	const FVector Spot = Point + FVector(0.f, 0.f, MortarRules::Get().SpotHeightCm);
	for (AOperativeCharacter* Man : Side)
	{
		if (Man && Man->HealthComponent && Man->HealthComponent->IsAlive() && Sight->CanOperativeSee(*Man, Spot))
		{
			return Man;
		}
	}
	return nullptr;
}

AMortarNestActor* UMortarSubsystem::SpawnNest(const FMortarNestPlacement& Placement)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FVector Location = Placement.Location;
	if (!Placement.bHasZ)
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MortarNestGround), false);
		if (World->LineTraceSingleByChannel(Ground, FVector(Location.X, Location.Y, 50000.f), FVector(Location.X, Location.Y, -50000.f), ECC_Visibility, Params))
		{
			Location.Z = Ground.ImpactPoint.Z;
		}
	}
	const int32 TeamId = Placement.TeamId < 0 ? FreePmcTeamId() : Placement.TeamId;
	const FRotator Facing(0.f, Placement.YawDeg, 0.f);
	const FTransform Transform(Facing, Location);
	AMortarNestActor* Nest = World->SpawnActorDeferred<AMortarNestActor>(AMortarNestActor::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Nest)
	{
		return nullptr;
	}
	Nest->TeamId = TeamId;
	Nest->NestId = Placement.Id;
	Nest->Ammo = Placement.Ammo;
	Nest->FinishSpawning(Transform);
	const int32 Manned = TeamId > 0 && Placement.Crew > 0 ? SpawnPmcCrew(Nest, Placement.Crew, TeamId) : 0;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] nest %s spawned at %s (team %d, crew %d / %d, ammo %d)"), *Nest->NestId, *Location.ToCompactString(), TeamId,
		Manned, MortarRules::Get().CrewNeeded, Nest->Ammo);
	return Nest;
}

int32 UMortarSubsystem::FreePmcTeamId() const
{
	int32 TeamId = 1;
	if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld()))
	{
		for (const int32 Existing : Pmc->GetTeamIds())
		{
			TeamId = FMath::Max(TeamId, Existing + 1);
		}
	}
	for (const AMortarNestActor* Nest : GetNests())
	{
		TeamId = FMath::Max(TeamId, Nest->TeamId + 1);
	}
	return TeamId;
}

int32 UMortarSubsystem::SpawnPmcCrew(AMortarNestActor* Nest, int32 Count, int32 TeamId)
{
	UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld());
	if (!Nest || !Pmc || Count <= 0)
	{
		return 0;
	}
	if (TeamId < 0)
	{
		TeamId = Nest->TeamId > 0 && Nest->GetCrewAssigned() == 0 ? Nest->TeamId : FreePmcTeamId();
	}
	const FRotator Facing(0.f, Nest->GetActorRotation().Yaw, 0.f);
	const TArray<AOperativeCharacter*> Crew = Pmc->SpawnSquad(TeamId, Nest->GetActorLocation() - Facing.Vector() * 300.f, Facing, Count,
		static_cast<int32>(GetTypeHash(Nest->NestId)) & 0xffff);
	int32 Manned = 0;
	for (AOperativeCharacter* Man : Crew)
	{
		Manned += Nest->AssignCrew(Man, true) >= 0 ? 1 : 0;
	}
	FPmcWeights Weights;
	PmcCommanderRules::LoadWeightsFile(PmcCommanderRules::DefaultWeightsPath(), Weights);
	Pmc->SetTeamCommanded(TeamId, true, &Weights);
	Pmc->RefreshSight();
	UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s: a PMC crew of %d (team %d) at the nest"), *Nest->NestId, Manned, TeamId);
	return Manned;
}

int32 UMortarSubsystem::SpawnFromLevelJson()
{
	UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	if (!GameMode || bSpawnedFromLevel)
	{
		return 0;
	}
	bSpawnedFromLevel = true;
	FString CommandLineJson;
	FParse::Value(FCommandLine::Get(), TEXT("LevelJson="), CommandLineJson);
	const FString File = LevelJsonRules::ResolveLevelJsonFile(World->GetMapName(), GameMode->LevelJsonFile, CommandLineJson);
	if (File.IsEmpty())
	{
		return 0;
	}
	const FString Path = FPaths::IsRelative(File) ? LevelJsonRules::GetLevelsDirectory() / File : File;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return 0;
	}
	TArray<FString> Errors;
	const TArray<FMortarNestPlacement> Placements = MortarRules::ParsePlacements(*Root, &Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mortar] %s: %s"), *File, *Error);
	}
	int32 Spawned = 0;
	for (const FMortarNestPlacement& Placement : Placements)
	{
		Spawned += SpawnNest(Placement) ? 1 : 0;
	}
	return Spawned;
}

namespace MortarConsole
{
	AMortarNestActor* NestArg(UWorld* World, const TArray<FString>& Args, int32& InOutArg)
	{
		const TArray<AMortarNestActor*> All = UMortarSubsystem::Get(World) ? UMortarSubsystem::Get(World)->GetNests() : TArray<AMortarNestActor*>();
		int32 Index = 0;
		if (Args.IsValidIndex(InOutArg) && Args[InOutArg].IsNumeric() && !Args[InOutArg].Contains(TEXT(".")))
		{
			Index = FCString::Atoi(*Args[InOutArg]);
			++InOutArg;
		}
		if (!All.IsValidIndex(Index))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mortar] no nest #%d (%d nests)"), Index, All.Num());
			return nullptr;
		}
		return All[Index];
	}

	void List(const TArray<FString>& Args, UWorld* World)
	{
		const TArray<AMortarNestActor*> All = UMortarSubsystem::Get(World) ? UMortarSubsystem::Get(World)->GetNests() : TArray<AMortarNestActor*>();
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			const AMortarNestActor* Nest = All[Index];
			UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] #%d %s team %d at %s: crew %d at the nest / %d assigned, ammo %d, %s"), Index, *Nest->NestId, Nest->TeamId,
				*Nest->GetActorLocation().ToCompactString(), Nest->GetCrewAtNest(), Nest->GetCrewAssigned(), Nest->Ammo, Nest->IsFiring() ? TEXT("firing") : TEXT("idle"));
		}
	}

	void Man(const TArray<FString>& Args, UWorld* World)
	{
		int32 Arg = 0;
		AMortarNestActor* Nest = NestArg(World, Args, Arg);
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Nest || !Squad)
		{
			return;
		}
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		TArray<AOperativeCharacter*> Crew;
		for (; Arg < Args.Num(); ++Arg)
		{
			const int32 Index = FCString::Atoi(*Args[Arg]);
			if (Members.IsValidIndex(Index))
			{
				Crew.Add(Members[Index]);
			}
		}
		if (Crew.IsEmpty())
		{
			Crew = Members;
		}
		for (AOperativeCharacter* Member : Crew)
		{
			Nest->AssignCrew(Member);
		}
	}

	void Fire(const TArray<FString>& Args, UWorld* World)
	{
		int32 Arg = 0;
		AMortarNestActor* Nest = NestArg(World, Args, Arg);
		if (!Nest)
		{
			return;
		}
		FVector Point;
		if (Args.IsValidIndex(Arg) && Args[Arg].Equals(TEXT("cursor"), ESearchCase::IgnoreCase))
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			FHitResult Hit;
			if (!Controller || !Controller->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[Mortar] no ground under the cursor"));
				return;
			}
			Point = Hit.ImpactPoint;
		}
		else if (Args.IsValidIndex(Arg + 1))
		{
			Point = FVector(FCString::Atof(*Args[Arg]), FCString::Atof(*Args[Arg + 1]), 0.f);
			if (Args.IsValidIndex(Arg + 2))
			{
				Point.Z = FCString::Atof(*Args[Arg + 2]);
			}
			else
			{
				FHitResult Ground;
				if (World->LineTraceSingleByChannel(Ground, FVector(Point.X, Point.Y, 50000.f), FVector(Point.X, Point.Y, -50000.f), ECC_Visibility))
				{
					Point.Z = Ground.ImpactPoint.Z;
				}
			}
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mortar] usage: CodexTactics.MortarFire [nest] <x y [z] | cursor>"));
			return;
		}
		const EMortarRefusal Refusal = Nest->OrderFire(Point);
		UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] fire order at %s: %s"), *Point.ToCompactString(),
			Refusal == EMortarRefusal::None ? TEXT("firing") : *MortarRules::RefusalText(Refusal));
	}

	void Cease(const TArray<FString>& Args, UWorld* World)
	{
		int32 Arg = 0;
		if (AMortarNestActor* Nest = NestArg(World, Args, Arg))
		{
			Nest->CeaseFire();
		}
	}

	void CrewPmc(const TArray<FString>& Args, UWorld* World)
	{
		int32 Arg = 0;
		if (AMortarNestActor* Nest = NestArg(World, Args, Arg))
		{
			UMortarSubsystem::Get(World)->SpawnPmcCrew(Nest, MortarRules::Get().CrewNeeded);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CrewPmcCommand(TEXT("CodexTactics.MortarCrewPmc"),
		TEXT("Dev: a PMC crew (a new commanded PMC team) mans a nest - CodexTactics.MortarCrewPmc [nest]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CrewPmc));
	static FAutoConsoleCommandWithWorldAndArgs ListCommand(TEXT("CodexTactics.MortarList"), TEXT("Lists the mortar nests (crew, ammo, firing)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&List));
	static FAutoConsoleCommandWithWorldAndArgs ManCommand(TEXT("CodexTactics.MortarMan"),
		TEXT("Order: squad members man a mortar nest - CodexTactics.MortarMan [nest] [squad indices] (no indices = the whole squad)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Man));
	static FAutoConsoleCommandWithWorldAndArgs FireCommand(TEXT("CodexTactics.MortarFire"),
		TEXT("Order: a manned nest fires at a point someone of its side sees - CodexTactics.MortarFire [nest] <x y [z] | cursor>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Fire));
	static FAutoConsoleCommandWithWorldAndArgs CeaseCommand(TEXT("CodexTactics.MortarCease"), TEXT("Order: a nest ceases fire - CodexTactics.MortarCease [nest]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Cease));
}
