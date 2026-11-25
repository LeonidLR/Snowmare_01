#include "Interactables/DoorwaySubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Interactables/DoorRules.h"
#include "LevelGen/PropsLayoutView.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UDoorwaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Doorways.Reset();
}

void UDoorwaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	AutoDiscoverDoorways(InWorld);
}

void UDoorwaySubsystem::Deinitialize()
{
	Doorways.Reset();
	Super::Deinitialize();
}

bool UDoorwaySubsystem::LoadFromLayout(const FString& LayoutFilePath)
{
	FString ResolvedPath = LayoutFilePath;
	if (FPaths::IsRelative(ResolvedPath))
	{
		if (FPaths::FileExists(FPaths::ProjectDir() / ResolvedPath))
		{
			ResolvedPath = FPaths::ProjectDir() / ResolvedPath;
		}
		else if (FPaths::FileExists(FPaths::ProjectContentDir() / ResolvedPath))
		{
			ResolvedPath = FPaths::ProjectContentDir() / ResolvedPath;
		}
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
	{
		return false;
	}

	return LoadFromLayoutJson(JsonText);
}

bool UDoorwaySubsystem::LoadFromLayoutJson(const FString& JsonString)
{
	TSharedPtr<FJsonObject> Root;
	const bool bRoot = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), Root) && Root.IsValid();
	FString Schema;
	if (bRoot)
	{
		Root->TryGetStringField(TEXT("schema"), Schema);
	}
	// The props view accepts only codex.levelgen_layout/1 (the depot). The kept showcase layout is /2; its building doors are the same
	// openings DoorRules already reads (yaw is the outward normal). Stairs and windows are not breach doorways.
	if (Schema == TEXT("codex.levelgen_layout/2"))
	{
		TArray<DoorRules::FLayoutPlace> Places;
		TMap<FString, FBox2D> Footprints;
		DoorRules::ParseLayoutPlaces(*Root, Places, Footprints);
		const TMap<FString, DoorRules::FLayoutDoor> LayoutDoors = DoorRules::ParseLayoutDoors(*Root);
		int32 Added = 0;
		for (const DoorRules::FLayoutPlace& Place : Places)
		{
			if (Place.bStair || Place.Id.IsEmpty())
			{
				continue;
			}
			FRuntimeDoorway Door;
			Door.Id = Place.Id;
			Door.BuildingId = Place.BuildingId;
			Door.WidthCm = Place.WidthCm > 0.f ? Place.WidthCm : 120.f;
			Door.Center = Place.Center;
			const FVector2D Normal = Place.Normal.GetSafeNormal();
			Door.OutwardNormal = FVector(Normal.X, Normal.Y, 0.f);
			Door.WallTangent = FVector(-Normal.Y, Normal.X, 0.f);
			if (const DoorRules::FLayoutDoor* Layout = LayoutDoors.Find(Place.Id))
			{
				Door.Role = Layout->Role;
			}
			Door.WalkLayer = FMath::Abs(Place.Center.Z) < 80.f ? TEXT("ground") : TEXT("upper");
			RegisterDoorway(Door);
			++Added;
		}
		return Added > 0;
	}

	TArray<FString> Errors;
	FPropsLayoutView View;
	if (!FPropsLayoutView::Build(JsonString, View, Errors))
	{
		return false;
	}

	float GroundZ = 0.f;
	if (bRoot && Root->HasField(TEXT("ground_z")))
	{
		GroundZ = static_cast<float>(Root->GetNumberField(TEXT("ground_z")));
	}

	for (const FPropsDoorway& D : View.GetDoorways())
	{
		FRuntimeDoorway Door;
		Door.Id = D.Id;
		Door.bIsGate = (D.Kind == EPropsDoorwayKind::Gate);
		Door.BuildingId = D.Building;
		Door.WallGroupId = D.WallGroup;
		Door.WidthCm = (D.WidthCm > 0) ? static_cast<float>(D.WidthCm) : 120.f;
		Door.Floor = D.Floor;
		Door.WalkLayer = D.WalkLayer;
		Door.bOnMandatoryRoute = D.bOnMandatoryRoute;
		Door.Center = FVector(static_cast<float>(D.Pos.X), static_cast<float>(D.Pos.Y), GroundZ);
		Door.WallTangent = FVector(static_cast<float>(D.Along.X), static_cast<float>(D.Along.Y), 0.f).GetSafeNormal();
		Door.OutwardNormal = FVector(static_cast<float>(D.Outward.X), static_cast<float>(D.Outward.Y), 0.f).GetSafeNormal();

		RegisterDoorway(Door);
	}

	return true;
}

void UDoorwaySubsystem::RegisterDoorway(const FRuntimeDoorway& Doorway)
{
	// Replace if existing ID, otherwise append
	for (int32 I = 0; I < Doorways.Num(); ++I)
	{
		if (Doorways[I].Id.Equals(Doorway.Id, ESearchCase::CaseSensitive))
		{
			Doorways[I] = Doorway;
			return;
		}
	}
	Doorways.Add(Doorway);
}

void UDoorwaySubsystem::ClearDoorways()
{
	Doorways.Reset();
}

const FRuntimeDoorway* UDoorwaySubsystem::FindDoorwayById(const FString& Id) const
{
	for (const FRuntimeDoorway& D : Doorways)
	{
		if (D.Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			return &D;
		}
	}
	return nullptr;
}

bool UDoorwaySubsystem::FindDoorwayNear(const FVector& WorldPos, float RadiusCm, FRuntimeDoorway& OutDoorway) const
{
	const float RadiusSq = FMath::Square(RadiusCm);
	float BestDistSq = MAX_flt;
	const FRuntimeDoorway* Best = nullptr;

	for (const FRuntimeDoorway& D : Doorways)
	{
		const float DistSq = FVector::DistSquared2D(WorldPos, D.Center);
		if (DistSq <= RadiusSq && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = &D;
		}
	}

	if (Best)
	{
		OutDoorway = *Best;
		return true;
	}
	return false;
}

bool UDoorwaySubsystem::FindDoorwayAtCursor(const FVector& WorldHitPoint, float SlackCm, FRuntimeDoorway& OutDoorway) const
{
	for (const FRuntimeDoorway& D : Doorways)
	{
		FVector Outward = D.OutwardNormal;
		if (D.bIsGate && Outward.IsNearlyZero())
		{
			Outward = FVector(-D.WallTangent.Y, D.WallTangent.X, 0.f).GetSafeNormal();
		}

		if (DoorwayBreachRules::IsPointInDoorwayPortal(WorldHitPoint, D.Center, D.WidthCm, D.WallTangent, Outward, SlackCm))
		{
			OutDoorway = D;
			return true;
		}
	}
	return false;
}

bool UDoorwaySubsystem::GetBreachTriadForDoorway(
	const FRuntimeDoorway& Doorway,
	FDoorwayBreachTriad& OutTriad,
	const FVector* OptionalApproachOrigin) const
{
	FVector Outward = Doorway.OutwardNormal;
	if (Doorway.bIsGate && Outward.IsNearlyZero())
	{
		Outward = FVector(-Doorway.WallTangent.Y, Doorway.WallTangent.X, 0.f).GetSafeNormal();
		if (OptionalApproachOrigin)
		{
			if (FVector::DotProduct(Outward, *OptionalApproachOrigin - Doorway.Center) < 0.f)
			{
				Outward = -Outward;
			}
		}
	}

	OutTriad = DoorwayBreachRules::CalculateBreachSlots(Doorway.Center, Doorway.WidthCm, Doorway.WallTangent, Outward);
	OutTriad.DoorwayId = Doorway.Id;

	// Check NavMesh reachability on world if available
	if (const UWorld* World = GetWorld())
	{
		if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation NavLoc;
			const bool bLeftOk = Nav->ProjectPointToNavigation(OutTriad.LeftJamb.Location, NavLoc, FVector(80.f, 80.f, 120.f));
			const bool bRightOk = Nav->ProjectPointToNavigation(OutTriad.RightJamb.Location, NavLoc, FVector(80.f, 80.f, 120.f));

			if (!bLeftOk || !bRightOk)
			{
				DoorwayBreachRules::ApplyJambObstruction(OutTriad, !bLeftOk, !bRightOk);
			}
		}
	}

	return true;
}

bool UDoorwaySubsystem::GetBreachTriadNear(
	const FVector& WorldPos,
	float RadiusCm,
	FDoorwayBreachTriad& OutTriad,
	const FVector* OptionalApproachOrigin) const
{
	FRuntimeDoorway Door;
	if (FindDoorwayNear(WorldPos, RadiusCm, Door))
	{
		return GetBreachTriadForDoorway(Door, OutTriad, OptionalApproachOrigin);
	}
	return false;
}

void UDoorwaySubsystem::AutoDiscoverDoorways(UWorld& InWorld)
{
	if (Doorways.Num() > 0)
	{
		return;
	}

	const FString MapName = InWorld.GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
	FString LayoutPath;

	// 1. Try LevelJson mapping
	const FString LevelJsonFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
	FString LevelJsonText;
	TSharedPtr<FJsonObject> LevelJson;
	if (FFileHelper::LoadFileToString(LevelJsonText, *LevelJsonFile) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelJsonText), LevelJson) &&
		LevelJson.IsValid() && LevelJson->HasField(TEXT("levelgen_layout")))
	{
		LayoutPath = FPaths::ProjectDir() / LevelJson->GetStringField(TEXT("levelgen_layout"));
	}

	// 2. Try accepted layouts
	if (LayoutPath.IsEmpty() || !FPaths::FileExists(LayoutPath))
	{
		const FString AcceptedDirect = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted") / MapName + TEXT(".json");
		if (FPaths::FileExists(AcceptedDirect))
		{
			LayoutPath = AcceptedDirect;
		}
		else if (MapName.Contains(TEXT("Depot")))
		{
			const FString DepotLayout = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted/gen_ref_01_02_depot_1.json");
			if (FPaths::FileExists(DepotLayout))
			{
				LayoutPath = DepotLayout;
			}
		}
	}

	if (!LayoutPath.IsEmpty() && FPaths::FileExists(LayoutPath))
	{
		LoadFromLayout(LayoutPath);
	}
}
