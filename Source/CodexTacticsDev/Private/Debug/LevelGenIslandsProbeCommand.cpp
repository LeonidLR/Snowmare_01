// Dev-only probe (engine check of the S2.3 building plans, 2026-11-20): the navmesh ISLANDS inside every enterable building of the
// loaded generated map. Samples the navmesh on a grid inside each footprint at every storey height and on the roof, then clusters
// the samples by complete operative paths (a sample joins the first cluster whose representative it can reach). One cluster per
// building = a single walkable island; extra clusters are listed with their sample counts and a position.
//   CodexTactics.LevelGenIslandsProbe [only=<substring of a building id>] [step=<cm, default 150>]
// Writes Saved/LevelGen/islands_<map>.json and exits. Layout lookup is the one of LevelGenBuildingsSmoke (LevelJson -> accepted layout).
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
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"

namespace LevelGenRampSmoke
{
	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, FString& Error);
}

namespace LevelGenIslandsProbe
{
	using FJsonValues = TArray<TSharedPtr<FJsonValue>>;

	struct FLevel
	{
		FString Name;
		double Z = 0.0;
	};

	struct FCluster
	{
		FVector Representative = FVector::ZeroVector;
		int32 Samples = 0;
	};

	FVector2D ReadXY(const FJsonValues& Values)
	{
		return FVector2D(Values[0]->AsNumber(), Values[1]->AsNumber());
	}

	TSharedRef<FJsonObject> ProbeBuilding(const FJsonObject& Building, double GroundZ, double Step, const AOperativeCharacter& Agent,
		UNavigationSystemV1& Nav, const ANavigationData& Data)
	{
		const FJsonValues& Footprint = Building.GetArrayField(TEXT("footprint"));
		const FVector2D Min = ReadXY(Footprint[0]->AsArray());
		const FVector2D Max = ReadXY(Footprint[1]->AsArray());
		const int32 Floors = Building.GetIntegerField(TEXT("floors"));
		double Rotation = 0.0;
		Building.TryGetNumberField(TEXT("rotation"), Rotation);
		TMap<int32, double> Heights;
		const FJsonValues* Detail = nullptr;
		if (Building.TryGetArrayField(TEXT("floors_detail"), Detail))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Detail)
			{
				const FJsonObject& Floor = *Value->AsObject();
				double Height = Floor.GetIntegerField(TEXT("floor")) * 300.0;
				Floor.TryGetNumberField(TEXT("height_z"), Height);
				Heights.Add(Floor.GetIntegerField(TEXT("floor")), Height);
			}
		}
		TArray<FLevel> Levels;
		for (int32 Index = 0; Index < Floors; ++Index)
		{
			const double* Height = Heights.Find(Index);
			Levels.Add({ FString::Printf(TEXT("storey_%d"), Index), GroundZ + (Height ? *Height : Index * 300.0) });
		}
		bool bRoof = false;
		Building.TryGetBoolField(TEXT("roof_walkable"), bRoof);
		if (bRoof)
		{
			Levels.Add({ TEXT("roof"), GroundZ + Floors * 300.0 });
		}

		TArray<FCluster> Clusters;
		int32 Samples = 0;
		const FVector2D Centre = (Min + Max) * 0.5;
		for (const FLevel& Level : Levels)
		{
			for (double X = Min.X + 60.0; X <= Max.X - 60.0; X += Step)
			{
				for (double Y = Min.Y + 60.0; Y <= Max.Y - 60.0; Y += Step)
				{
					const FVector2D Local(X, Y);
					const FVector2D World = Centre + (Local - Centre).GetRotated(Rotation);
					FNavLocation Point;
					if (!Nav.ProjectPointToNavigation(FVector(World, Level.Z + 15.0), Point, FVector(40.0, 40.0, 50.0), &Data)
						|| FMath::Abs(Point.Location.Z - Level.Z) > 50.0)
					{
						continue;
					}
					++Samples;
					bool bJoined = false;
					for (FCluster& Cluster : Clusters)
					{
						FPathFindingQuery Query(&Agent, Data, Cluster.Representative, Point.Location);
						Query.SetAllowPartialPaths(false);
						const FPathFindingResult Path = Nav.FindPathSync(Agent.GetNavAgentPropertiesRef(), Query);
						if (Path.IsSuccessful() && Path.Path.IsValid() && !Path.Path->IsPartial())
						{
							++Cluster.Samples;
							bJoined = true;
							break;
						}
					}
					if (!bJoined)
					{
						Clusters.Add({ Point.Location, 1 });
					}
				}
			}
		}
		FJsonValues ClusterValues;
		for (const FCluster& Cluster : Clusters)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("samples"), Cluster.Samples);
			Entry->SetStringField(TEXT("at"), Cluster.Representative.ToString());
			ClusterValues.Add(MakeShared<FJsonValueObject>(Entry));
		}
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("id"), Building.GetStringField(TEXT("id")));
		Result->SetNumberField(TEXT("floors"), Floors);
		Result->SetNumberField(TEXT("levels_sampled"), Levels.Num());
		Result->SetNumberField(TEXT("samples"), Samples);
		Result->SetNumberField(TEXT("islands"), Clusters.Num());
		Result->SetArrayField(TEXT("clusters"), ClusterValues);
		return Result;
	}

	bool Finish(const FString& Result)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), *Result);
		FPlatformMisc::RequestExit(false, TEXT("LevelGenIslandsProbe"));
		return false;
	}

	bool Probe(UWorld& World, const FString& Only, double Step, const AOperativeCharacter& Agent, UNavigationSystemV1& Nav, const ANavigationData& Data)
	{
		const FString MapName = FPackageName::GetShortName(World.GetMapName());
		FString Error;
		const TSharedPtr<FJsonObject> Layout = LevelGenRampSmoke::LoadLayout(MapName, Error);
		if (!Layout)
		{
			return Finish(TEXT("FAIL LevelGenIslandsProbe: ") + Error);
		}
		double GroundZ = 0.0;
		Layout->TryGetNumberField(TEXT("ground_z"), GroundZ);
		FJsonValues Results;
		int32 Multi = 0;
		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("buildings")))
		{
			const FJsonObject& Building = *Value->AsObject();
			bool bEnterable = false;
			const FString Id = Building.GetStringField(TEXT("id"));
			if (!Building.TryGetBoolField(TEXT("enterable"), bEnterable) || !bEnterable || (!Only.IsEmpty() && !Id.Contains(Only)))
			{
				continue;
			}
			const TSharedRef<FJsonObject> Result = ProbeBuilding(Building, GroundZ, Step, Agent, Nav, Data);
			Multi += Result->GetIntegerField(TEXT("islands")) > 1 ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("IslandsProbe %s: floors %d, samples %d, islands %d"), *Id, Result->GetIntegerField(TEXT("floors")),
				Result->GetIntegerField(TEXT("samples")), Result->GetIntegerField(TEXT("islands")));
			Results.Add(MakeShared<FJsonValueObject>(Result));
		}
		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("map"), MapName);
		Report->SetNumberField(TEXT("step_cm"), Step);
		Report->SetNumberField(TEXT("buildings"), Results.Num());
		Report->SetNumberField(TEXT("buildings_with_more_than_one_island"), Multi);
		Report->SetArrayField(TEXT("results"), Results);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("LevelGen");
		const FString ReportPath = Dir / FString::Printf(TEXT("islands_%s.json"), *MapName);
		FString Json;
		if (!FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			|| !FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir)
			|| !FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return Finish(TEXT("FAIL LevelGenIslandsProbe: could not write ") + ReportPath);
		}
		return Finish(FString::Printf(TEXT("%s LevelGenIslandsProbe on %s: %d buildings, %d with more than one island; report: %s"),
			Results.IsEmpty() ? TEXT("FAIL") : TEXT("DONE"), *MapName, Results.Num(), Multi, *ReportPath));
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		FString Only;
		double Step = 150.0;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				Only = Arg.Mid(5);
			}
			else if (Arg.StartsWith(TEXT("step=")))
			{
				Step = FMath::Max(50.0, FCString::Atod(*Arg.Mid(5)));
			}
		}
		const double Deadline = FPlatformTime::Seconds() + 120.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakWorld = TWeakObjectPtr<UWorld>(World), Only, Step, Deadline](float)
			{
				UWorld* MapWorld = WeakWorld.Get();
				if (!MapWorld || FPlatformTime::Seconds() >= Deadline)
				{
					return Finish(TEXT("FAIL LevelGenIslandsProbe: timed out waiting for an operative and ready navmesh"));
				}
				const USquadSubsystem* Squad = MapWorld->GetSubsystem<USquadSubsystem>();
				AOperativeCharacter* Agent = Squad ? Squad->GetLeader() : nullptr;
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(MapWorld);
				if (!Agent || !Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(MapWorld))
				{
					return true;
				}
				const ANavigationData* Data = Nav->GetNavDataForProps(Agent->GetNavAgentPropertiesRef(), Agent->GetActorLocation());
				return Data ? Probe(*MapWorld, Only, Step, *Agent, *Nav, *Data) : true;
			}), 0.25f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenIslandsProbe"),
		TEXT("Count the navmesh islands inside every enterable building of the loaded generated map (grid samples per storey + roof, clustered by complete operative paths). ")
		TEXT("[only=<id substring>] [step=<cm>]. Writes Saved/LevelGen/islands_<map>.json, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
