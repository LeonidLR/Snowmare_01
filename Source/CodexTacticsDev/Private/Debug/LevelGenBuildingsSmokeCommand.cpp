// Dev-only traversal gate for the accepted layout of the currently loaded map.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"

namespace LevelGenRampSmoke
{
	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, FString& Error);
}

namespace LevelGenBuildingsSmoke
{
	constexpr double StoreyHeight = 300.0;
	constexpr double DoorStandOff = 100.0;
	constexpr double NavHeightTolerance = 65.0;
	using FJsonObjects = TArray<TSharedPtr<FJsonValue>>;

	struct FTarget
	{
		FString Id;
		const TCHAR* Kind;
		FVector Position;
		FBox2D Bounds;
	};

	struct FBuildingFrame
	{
		FBox2D Footprint;
		double Rotation;

		FVector2D Rotate(const FVector2D& Point, double Angle) const
		{
			const FVector2D Centre = Footprint.GetCenter();
			return Centre + (Point - Centre).GetRotated(Angle);
		}
	};

	const FJsonObjects& Array(const FJsonObject& Object, const TCHAR* Field)
	{
		static const FJsonObjects Empty;
		const FJsonObjects* Values = nullptr;
		return Object.TryGetArrayField(Field, Values) ? *Values : Empty;
	}

	FVector2D Point(const FJsonObjects& XY)
	{
		return FVector2D(XY[0]->AsNumber(), XY[1]->AsNumber());
	}

	FVector2D Point(const FJsonObject& Object, const TCHAR* Field)
	{
		return Point(Object.GetArrayField(Field));
	}

	void AddRooms(const FJsonObjects& Rooms, const FBuildingFrame& Frame, double GroundZ, const TMap<int32, double>& FloorHeights,
		TSet<FString>& SeenRooms, TArray<FTarget>& Targets)
	{
		for (const auto& Value : Rooms)
		{
			const FJsonObject& Room = *Value->AsObject();
			const FString Id = Room.GetStringField(TEXT("id"));
			if (SeenRooms.Contains(Id))
			{
				continue;
			}
			SeenRooms.Add(Id);
			const FJsonObject& Bounds = *Room.GetObjectField(TEXT("bounds"));
			const FBox2D Box(Point(Bounds, TEXT("min")), Point(Bounds, TEXT("max")));
			const int32 Floor = Room.GetIntegerField(TEXT("floor"));
			const double* Height = FloorHeights.Find(Floor);
			Targets.Add({ Id, TEXT("room"), FVector(Frame.Rotate(Box.GetCenter(), Frame.Rotation),
				GroundZ + (Height ? *Height : Floor * StoreyHeight)), Box });
		}
	}

	TArray<FTarget> MakeTargets(const FJsonObject& Building, const FBuildingFrame& Frame, double GroundZ)
	{
		const int32 Floors = Building.GetIntegerField(TEXT("floors"));
		const FVector2D Centre = Frame.Footprint.GetCenter();
		TMap<int32, double> FloorHeights;
		for (const auto& Value : Array(Building, TEXT("floors_detail")))
		{
			const FJsonObject& Floor = *Value->AsObject();
			const int32 Level = Floor.GetIntegerField(TEXT("floor"));
			double Height = Level * StoreyHeight;
			Floor.TryGetNumberField(TEXT("height_z"), Height);
			FloorHeights.Add(Level, Height);
		}
		TArray<FTarget> Targets;
		TSet<FString> Rooms;
		AddRooms(Array(Building, TEXT("rooms")), Frame, GroundZ, FloorHeights, Rooms, Targets);
		for (const auto& Value : Array(Building, TEXT("floors_detail")))
		{
			AddRooms(Array(*Value->AsObject(), TEXT("rooms")), Frame, GroundZ, FloorHeights, Rooms, Targets);
		}

		TSet<int32> FloorsWithLanding;
		for (const auto& Value : Array(Building, TEXT("stairs")))
		{
			const FJsonObject& Stair = *Value->AsObject();
			const int32 Level = Stair.GetIntegerField(TEXT("to"));
			if (Level <= 0 || Level >= Floors)
			{
				continue;
			}
			const FVector2D Top = Point(Stair, TEXT("top"));
			const FVector2D Approach = Point(Stair, Stair.HasField(TEXT("mid")) ? TEXT("mid") : TEXT("pos"));
			const double* Height = FloorHeights.Find(Level);
			Targets.Add({ Stair.GetStringField(TEXT("id")), TEXT("landing"),
				FVector(Top + (Top - Approach).GetSafeNormal() * 65.0, GroundZ + (Height ? *Height : Level * StoreyHeight)), Frame.Footprint });
			FloorsWithLanding.Add(Level);
		}
		bool bHollow = false;
		Building.TryGetBoolField(TEXT("hollow"), bHollow);
		for (int32 Level = 0; Level < Floors; ++Level)
		{
			const double* Height = FloorHeights.Find(Level);
			if (FloorsWithLanding.Contains(Level) || (Level > 0 && bHollow && !Height))
			{
				continue;
			}
			const double Z = GroundZ + (Height ? *Height : Level * StoreyHeight);
			const FTarget* Room = Targets.FindByPredicate([Z](const FTarget& Target)
			{
				return FCString::Strcmp(Target.Kind, TEXT("room")) == 0 && FMath::IsNearlyEqual(Target.Position.Z, Z);
			});
			const FVector Position = Room ? Room->Position : FVector(Centre, Z);
			const FBox2D Bounds = Room ? Room->Bounds : Frame.Footprint;
			Targets.Add({ FString::Printf(TEXT("storey_%d"), Level), TEXT("landing"), Position, Bounds });
		}
		bool bWalkableRoof = false;
		if (Building.TryGetBoolField(TEXT("roof_walkable"), bWalkableRoof) && bWalkableRoof)
		{
			Targets.Add({ TEXT("roof"), TEXT("roof"), FVector(Centre, GroundZ + Floors * StoreyHeight), Frame.Footprint });
		}
		return Targets;
	}

	TArray<FTarget> MakeDoorStarts(const FJsonObject& Building, const FBuildingFrame& Frame, double GroundZ)
	{
		TArray<FTarget> Doors;
		for (const auto& Value : Array(Building, TEXT("doors")))
		{
			const FJsonObject& Door = *Value->AsObject();
			const FVector2D Local = Frame.Rotate(Point(Door, TEXT("pos")), -Frame.Rotation);
			const FBox2D& Box = Frame.Footprint;
			const double Distances[] = { FMath::Abs(Local.X - Box.Min.X), FMath::Abs(Local.X - Box.Max.X),
				FMath::Abs(Local.Y - Box.Min.Y), FMath::Abs(Local.Y - Box.Max.Y) };
			const FVector2D Normals[] = { FVector2D(-1, 0), FVector2D(1, 0), FVector2D(0, -1), FVector2D(0, 1) };
			int32 Edge = 0;
			for (int32 Index = 1; Index < UE_ARRAY_COUNT(Distances); ++Index)
			{
				if (Distances[Index] < Distances[Edge])
				{
					Edge = Index;
				}
			}
			// Door coordinates are in world space. Interior openings belong to partitions.
			if (Distances[Edge] > 30.0)
			{
				continue;
			}
			FString Id;
			Door.TryGetStringField(TEXT("id"), Id);
			if (Id.IsEmpty())
			{
				Id = FString::Printf(TEXT("door_%d"), Doors.Num() + 1);
			}
			Doors.Add({ Id, TEXT("door"), FVector(Frame.Rotate(Local + Normals[Edge] * DoorStandOff, Frame.Rotation),
				GroundZ + Door.GetIntegerField(TEXT("floor")) * StoreyHeight), Box });
		}
		return Doors;
	}

	bool Project(const UNavigationSystemV1& Nav, const ANavigationData& Data, const FBuildingFrame& Frame, const FTarget& Target,
		bool bExterior, FNavLocation& Location)
	{
		constexpr double HorizontalTolerance = 85.0;
		const FVector Probe = Target.Position + FVector(0, 0, 15);
		if (!Nav.ProjectPointToNavigation(Probe, Location, FVector(HorizontalTolerance, HorizontalTolerance, NavHeightTolerance), &Data)
			|| FMath::Abs(Location.Location.Z - Target.Position.Z) > NavHeightTolerance)
		{
			return false;
		}
		const FVector2D Local = Frame.Rotate(FVector2D(Location.Location), -Frame.Rotation);
		return Target.Bounds.IsInsideOrOn(Local) != bExterior;
	}

	TSharedPtr<FJsonObject> Failure(const FTarget& Target, const FString& Door, const TCHAR* Reason)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("target"), Target.Id);
		Result->SetStringField(TEXT("kind"), Target.Kind);
		Result->SetArrayField(TEXT("pos"), { MakeShared<FJsonValueNumber>(Target.Position.X),
			MakeShared<FJsonValueNumber>(Target.Position.Y), MakeShared<FJsonValueNumber>(Target.Position.Z) });
		Result->SetStringField(TEXT("door"), Door);
		Result->SetStringField(TEXT("reason"), Reason);
		return Result;
	}

	TSharedRef<FJsonObject> CheckBuilding(const FJsonObject& Building, double GroundZ, const AOperativeCharacter& Agent,
		UNavigationSystemV1& Nav, const ANavigationData& Data)
	{
		const FJsonObjects& Footprint = Building.GetArrayField(TEXT("footprint"));
		FBuildingFrame Frame{ FBox2D(Point(Footprint[0]->AsArray()), Point(Footprint[1]->AsArray())), 0.0 };
		Building.TryGetNumberField(TEXT("rotation"), Frame.Rotation);
		const TArray<FTarget> Doors = MakeDoorStarts(Building, Frame, GroundZ);
		const TArray<FTarget> Targets = MakeTargets(Building, Frame, GroundZ);

		TSharedPtr<FJsonObject> FirstFailure;
		int32 Passed = 0;
		if (Doors.IsEmpty())
		{
			FirstFailure = Failure({ TEXT("exterior_door"), TEXT("door"), FVector(Frame.Footprint.GetCenter(), GroundZ),
				Frame.Footprint }, FString(), TEXT("no exterior door"));
		}
		TArray<FNavLocation> Goals;
		TArray<bool> OnNav;
		for (const FTarget& Target : Targets)
		{
			FNavLocation& Goal = Goals.AddDefaulted_GetRef();
			OnNav.Add(Project(Nav, Data, Frame, Target, false, Goal));
		}

		for (const FTarget& Door : Doors)
		{
			FNavLocation Start;
			if (!Project(Nav, Data, Frame, Door, true, Start))
			{
				if (!FirstFailure)
				{
					FirstFailure = Failure(Door, Door.Id, TEXT("exterior approach off navmesh"));
				}
				continue;
			}
			for (int32 Index = 0; Index < Targets.Num(); ++Index)
			{
				const TCHAR* Reason = TEXT("target off navmesh");
				if (OnNav[Index])
				{
					FPathFindingQuery Query(&Agent, Data, Start.Location, Goals[Index].Location);
					Query.SetAllowPartialPaths(false);
					const FPathFindingResult Path = Nav.FindPathSync(Agent.GetNavAgentPropertiesRef(), Query);
					if (Path.IsSuccessful() && Path.Path.IsValid() && !Path.Path->IsPartial())
					{
						++Passed;
						continue;
					}
					Reason = TEXT("no complete path");
				}
				if (!FirstFailure)
				{
					FirstFailure = Failure(Targets[Index], Door.Id, Reason);
				}
			}
		}

		const int32 Checks = Doors.Num() * Targets.Num();
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("id"), Building.GetStringField(TEXT("id")));
		Result->SetBoolField(TEXT("pass"), !FirstFailure.IsValid());
		if (FirstFailure)
		{
			Result->SetObjectField(TEXT("first_unreachable"), FirstFailure);
		}
		else
		{
			Result->SetField(TEXT("first_unreachable"), MakeShared<FJsonValueNull>());
		}
		Result->SetNumberField(TEXT("doors"), Doors.Num());
		Result->SetNumberField(TEXT("targets"), Targets.Num());
		Result->SetNumberField(TEXT("passed"), Passed);
		Result->SetNumberField(TEXT("failed"), Checks - Passed);
		return Result;
	}

	bool Finish(const FString& Result)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), *Result);
		FPlatformMisc::RequestExit(false, TEXT("LevelGenBuildingsSmoke"));
		return false;
	}

	bool CheckMap(UWorld& World, const FString& Only, int32 MaxFail, AOperativeCharacter& Agent,
		UNavigationSystemV1& Nav, const ANavigationData& Data)
	{
		const FString MapName = FPackageName::GetShortName(World.GetMapName());
		FString Error;
		const TSharedPtr<FJsonObject> Layout = LevelGenRampSmoke::LoadLayout(MapName, Error);
		if (!Layout)
		{
			return Finish(TEXT("FAIL LevelGenBuildingsSmoke: ") + Error);
		}
		double GroundZ = 0.0;
		Layout->TryGetNumberField(TEXT("ground_z"), GroundZ);
		const FJsonObjects& Buildings = Array(*Layout, TEXT("buildings"));
		FJsonObjects Results;
		TArray<FString> FailedIds;
		for (const auto& Value : Buildings)
		{
			const FJsonObject& Building = *Value->AsObject();
			bool bEnterable = false;
			const FString Id = Building.GetStringField(TEXT("id"));
			if (!Building.TryGetBoolField(TEXT("enterable"), bEnterable) || !bEnterable || (!Only.IsEmpty() && Id != Only))
			{
				continue;
			}
			const TSharedRef<FJsonObject> Result = CheckBuilding(Building, GroundZ, Agent, Nav, Data);
			if (!Result->GetBoolField(TEXT("pass")))
			{
				FailedIds.Add(Id);
			}
			Results.Add(MakeShared<FJsonValueObject>(Result));
		}
		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("map"), MapName);
		Report->SetNumberField(TEXT("buildings_total"), Buildings.Num());
		Report->SetNumberField(TEXT("checked"), Results.Num());
		Report->SetNumberField(TEXT("passed"), Results.Num() - FailedIds.Num());
		Report->SetNumberField(TEXT("failed"), FailedIds.Num());
		Report->SetArrayField(TEXT("buildings"), Results);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("LevelGen");
		const FString ReportPath = Dir / FString::Printf(TEXT("buildings_smoke_%s.json"), *MapName);
		FString Json;
		if (!FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			|| !FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir)
			|| !FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return Finish(TEXT("FAIL LevelGenBuildingsSmoke: could not write ") + ReportPath);
		}
		if (Buildings.IsEmpty() || (!Only.IsEmpty() && Results.IsEmpty()))
		{
			return Finish(TEXT("FAIL LevelGenBuildingsSmoke: no matching building in ") + MapName);
		}
		const int32 Failed = FailedIds.Num();
		FailedIds.SetNum(FMath::Min(Failed, MaxFail >= 0 ? MaxFail : 10));
		return Finish(FString::Printf(TEXT("%s LevelGenBuildingsSmoke on %s: %d/%d enterable passed, %d failed (%d total); ids: %s; report: %s"),
			Failed == 0 || MaxFail >= 0 ? TEXT("PASS") : TEXT("FAIL"), *MapName, Results.Num() - Failed, Results.Num(), Failed, Buildings.Num(),
			*FString::Join(FailedIds, TEXT(", ")), *ReportPath));
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		FString Only;
		int32 MaxFail = -1;
		// The sweep runner can supply launch flags via smokeOptions.extra.
		FParse::Value(FCommandLine::Get(), TEXT("-LevelGenBuildingsSmokeMaxFail="), MaxFail);
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				Only = Arg.Mid(5);
			}
			else if (Arg.StartsWith(TEXT("maxfail=")))
			{
				if (!LexTryParseString(MaxFail, *Arg.Mid(8)) || MaxFail < 0)
				{
					Finish(TEXT("FAIL LevelGenBuildingsSmoke: maxfail must be a nonnegative integer"));
					return;
				}
			}
		}
		const double Deadline = FPlatformTime::Seconds() + 60.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakWorld = TWeakObjectPtr<UWorld>(World), Only, MaxFail, Deadline](float)
			{
				UWorld* MapWorld = WeakWorld.Get();
				if (!MapWorld || FPlatformTime::Seconds() >= Deadline)
				{
					return Finish(TEXT("FAIL LevelGenBuildingsSmoke: timed out waiting for an operative and ready navmesh"));
				}
				const USquadSubsystem* Squad = MapWorld->GetSubsystem<USquadSubsystem>();
				AOperativeCharacter* Agent = Squad ? Squad->GetLeader() : nullptr;
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(MapWorld);
				if (!Agent || !Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(MapWorld))
				{
					return true;
				}
				const ANavigationData* Data = Nav->GetNavDataForProps(Agent->GetNavAgentPropertiesRef(), Agent->GetActorLocation());
				return Data ? CheckMap(*MapWorld, Only, MaxFail, *Agent, *Nav, *Data) : true;
			}), 0.25f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenBuildingsSmoke"),
		TEXT("Check complete operative nav paths from each exterior door to every room, landing and walkable roof. ")
		TEXT("[only=<building id>] [maxfail=<n> to report without failing for unreachable buildings]. Writes JSON, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
