// Dev-only traversal gate for the accepted layout of the currently loaded map.
// Default (048): complete operative navmesh paths from every exterior door to every room, landing and walkable roof of every
// enterable building (Saved/LevelGen/buildings_smoke_<map>.json).
// walk=1 / -LevelGenBuildingsSmokeWalk / CodexTactics.LevelGenBuildingsWalkSmoke (S2.9 / P0b, docs/design/levelgen/S2_ROOMS_PLAN.md,
// risk rows 1 and 4; building-route-model.md F5): after the path check the squad leader really WALKS every building in Solo (the
// player's click, ACodexTacticsPlayerController::HandleWorldHit, as LevelGenRampSmoke orders it): from the first exterior door approach
// to each storey landing and the walkable roof in storey order; then per flat entry (units[].entries, BuildingWalkRules) a solo leg and a
// squad leg (squad mode, the same click: the formation follows) from the room outside the flat to the flat's room behind the door - gate
// squad <= 2 x solo. Per leg: arrived (80 cm, storey Z), game seconds, navmesh path length, walked length, stuck (no progress 4 s) + where.
// minpass=<0..1> (-LevelGenBuildingsSmokeMinPass=): PASS when the share of passing buildings >= minpass (walk default 1). Writes
// Saved/LevelGen/buildings_walk_<map>.json. F5 drift is NOT computed (the layout carries no TS key-route cost; see the report's "drift").
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Algo/StableSort.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "LevelGen/BuildingWalkRules.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/ColdSurvivalComponent.h"

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
			// The room's walkable anchor (building frame, like the bounds) when the layout has one: the centre can lie on a floor gap or a flight.
			FVector2D Probe = Box.GetCenter();
			const FJsonObjects* Anchor = nullptr;
			if (Room.TryGetArrayField(TEXT("anchor"), Anchor) && Anchor->Num() >= 2)
			{
				Probe = Point(*Anchor);
			}
			Targets.Add({ Id, TEXT("room"), FVector(Frame.Rotate(Probe, Frame.Rotation),
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
			// The footprint centre, unless it lies in the roof's opening over a switchback core that reaches the roof (a kit block 10 m
			// deep has its core mid-building: B_b18 2026-11-21 "target off navmesh roof"): then just past that opening along the long axis.
			FVector2D RoofPoint = Centre;
			for (const auto& Value : Array(Building, TEXT("stairs")))
			{
				const FJsonObject& Stair = *Value->AsObject();
				const FJsonObjects* MidRect = nullptr;
				if (Stair.GetIntegerField(TEXT("to")) != Floors || !Stair.TryGetArrayField(TEXT("mid_rect"), MidRect) || MidRect->Num() < 2)
				{
					continue;
				}
				FBox2D Hole(ForceInit);
				Hole += Point(Stair, TEXT("pos"));
				Hole += Point(Stair, TEXT("top"));
				Hole += Point((*MidRect)[0]->AsArray());
				Hole += Point((*MidRect)[1]->AsArray());
				if (!Hole.ExpandBy(60.0).IsInsideOrOn(RoofPoint))
				{
					continue;
				}
				const FVector2D Size = Frame.Footprint.GetSize();
				const int32 Axis = Size.X >= Size.Y ? 0 : 1;
				const double Sign = RoofPoint[Axis] >= Hole.GetCenter()[Axis] ? 1.0 : -1.0;
				RoofPoint[Axis] = (Sign > 0.0 ? Hole.Max[Axis] : Hole.Min[Axis]) + Sign * 120.0;
			}
			Targets.Add({ TEXT("roof"), TEXT("roof"), FVector(RoofPoint, GroundZ + Floors * StoreyHeight), Frame.Footprint });
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

	struct FOptions
	{
		FString Only;
		int32 MaxFail = -1;
		/** < 0: not set (the path check keeps its maxfail verdict; walk mode requires every building). */
		double MinPass = -1.0;
		bool bWalk = false;
		/** walk mode: also the flat entry legs (squad=0 skips them). */
		bool bSquad = true;
		/** walk mode: wall-clock budget of the whole run, seconds (under the runner's timeout). */
		double BudgetSeconds = 540.0;
	};

	void StartWalk(UWorld& World, const FOptions& Options, const TSharedPtr<FJsonObject>& Layout, const FJsonObjects& PathResults,
		AOperativeCharacter& Agent, UNavigationSystemV1& Nav, const ANavigationData& Data, double WallStart);

	bool CheckMap(UWorld& World, const FOptions& Options, AOperativeCharacter& Agent, UNavigationSystemV1& Nav, const ANavigationData& Data,
		double WallStart)
	{
		const FString& Only = Options.Only;
		const int32 MaxFail = Options.MaxFail;
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
		const bool bPass = Options.MinPass >= 0.0 ? BuildingWalkRules::PassesShare(Results.Num() - Failed, Results.Num(), Options.MinPass)
			: Failed == 0 || MaxFail >= 0;
		const FString Line = FString::Printf(TEXT("%s LevelGenBuildingsSmoke on %s: %d/%d enterable passed, %d failed (%d total); ids: %s; report: %s"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), *MapName, Results.Num() - Failed, Results.Num(), Failed, Buildings.Num(),
			*FString::Join(FailedIds, TEXT(", ")), *ReportPath) + (Options.MinPass >= 0.0 ? FString::Printf(TEXT("; minpass %.2f"), Options.MinPass) : FString());
		if (!Options.bWalk)
		{
			return Finish(Line);
		}
		// Walk mode: the path check is a stage, not the verdict ("Smoke RESULT: PASS" anywhere in the log is a pass for smoke_parallel).
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke path check: %s"), *Line);
		StartWalk(World, Options, Layout, Results, Agent, Nav, Data, WallStart);
		return false;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Walk mode (S2.9 / P0b)

	enum class ELegKind : uint8 { Walk, EntrySolo, EntrySquad };
	enum class EWalkPhase : uint8 { Settle, Place, Wait, Run, Done };

	constexpr double TeleportSettleSeconds = 1.0;
	constexpr double SoloHoldSeconds = 0.3;  // the leader within the arrival radius this long = arrived
	constexpr double SquadHoldSeconds = 1.0; // every member ok this long = the squad is in

	const TCHAR* LegKindName(ELegKind Kind)
	{
		switch (Kind)
		{
		case ELegKind::Walk: return TEXT("walk");
		case ELegKind::EntrySolo: return TEXT("entry_solo");
		case ELegKind::EntrySquad: return TEXT("entry_squad");
		}
		return TEXT("?");
	}

	struct FLeg
	{
		ELegKind Kind = ELegKind::Walk;
		int32 Building = INDEX_NONE;
		FString From;
		FString To;
		FString ToKind;
		FString Entry;
		FString Unit;
		FString Role;
		/** Walk legs: teleport the leader to Start first (the building's first leg; after a failed leg). Entry legs always teleport. */
		bool bFirstOfBuilding = false;
		FVector Start = FVector::ZeroVector;
		bool bStartOk = false;
		FVector Goal = FVector::ZeroVector;
		bool bGoalOk = false;
		/** Flat rooms (building frame), entry legs. */
		TArray<FBox2D> Flat;
		// Result
		bool bRan = false;
		bool bPass = false;
		bool bArrived = false;
		bool bStuck = false;
		double Seconds = -1.0;
		double PathCm = -1.0;
		double WalkedCm = 0.0;
		double Ratio = -1.0;
		int32 FollowersInFlat = 0;
		int32 Followers = 0;
		FVector EndPos = FVector::ZeroVector;
		FVector FailPos = FVector::ZeroVector;
		FString Reason;
	};

	struct FWalkBuilding
	{
		FString Id;
		FBuildingFrame Frame{ FBox2D(ForceInit), 0.0 };
		TSharedPtr<FJsonObject> PathResult;
		bool bPathPass = false;
		FString PlanError;
		FVector PlanErrorPos = FVector::ZeroVector;
		TArray<int32> Legs;
		FBuildingFlatEntries Scan;
	};

	struct FWalk
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		FOptions Options;
		double WallStart = 0.0;
		double WallDeadline = 0.0;
		TArray<FWalkBuilding> Buildings;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		EWalkPhase Phase = EWalkPhase::Settle;
		double PhaseStart = 0.0;
		double SettleStart = -1.0;
		/** [0] = the leader, the rest the followers; Park = where each stood after the start (Solo legs park the followers there). */
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<FVector> Park;
		// The running leg
		bool bTeleported = false;
		double LegStart = 0.0;
		FVector LastLeaderPos = FVector::ZeroVector;
		TArray<FBuildingWalkProgress> Progress;
		TArray<double> OkSince;
		/** Each member's feet when the order was given (BuildingWalkRules::FollowerMovedCm). */
		TArray<FVector> MemberStart;
		bool bBudgetHit = false;
	};

	double Now(const UWorld& World)
	{
		return World.GetTimeSeconds();
	}

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0, 0, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V)
	{
		return { MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.X)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.Y)),
			MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.Z)) };
	}

	double Round1(double Value)
	{
		return FMath::RoundToDouble(Value * 10.0) / 10.0;
	}

	void Teleport(AOperativeCharacter& Member, const FVector& NavPoint, const FVector& Facing)
	{
		const FVector Dir = Facing.GetSafeNormal2D();
		Member.TeleportTo(NavPoint + FVector(0, 0, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0),
			Dir.IsNearlyZero() ? Member.GetActorRotation() : Dir.Rotation(), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** The player's click on Goal (HandleWorldHit; a single click = walk), the same order LevelGenRampSmoke gives. */
	bool Click(UWorld& World, const FVector& Goal)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World.GetFirstPlayerController());
		if (!PC)
		{
			return false;
		}
		FHitResult Hit;
		Hit.ImpactPoint = Goal;
		Hit.Location = Goal;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		PC->HandleWorldHit(Hit);
		return true;
	}

	bool InFlat(const FLeg& Leg, const FBuildingFrame& Frame, const FVector& Position)
	{
		const FVector2D Local = Frame.Rotate(FVector2D(Position), -Frame.Rotation);
		return Leg.Flat.ContainsByPredicate([&Local](const FBox2D& Box) { return Box.ExpandBy(20.0).IsInsideOrOn(Local); });
	}

	void PlanBuilding(FWalk& Walk, const FJsonObject& Building, double GroundZ, const UNavigationSystemV1& Nav, const ANavigationData& Data)
	{
		FWalkBuilding& Record = Walk.Buildings.Last();
		const int32 BuildingIndex = Walk.Buildings.Num() - 1;
		const FJsonObjects& Footprint = Building.GetArrayField(TEXT("footprint"));
		Record.Frame = FBuildingFrame{ FBox2D(Point(Footprint[0]->AsArray()), Point(Footprint[1]->AsArray())), 0.0 };
		Building.TryGetNumberField(TEXT("rotation"), Record.Frame.Rotation);
		const FBuildingFrame& Frame = Record.Frame;
		const TArray<FTarget> Doors = MakeDoorStarts(Building, Frame, GroundZ);
		const TArray<FTarget> Targets = MakeTargets(Building, Frame, GroundZ);
		const auto AddLeg = [&Walk, &Record, BuildingIndex]() -> FLeg&
		{
			Record.Legs.Add(Walk.Legs.Num());
			FLeg& Leg = Walk.Legs.AddDefaulted_GetRef();
			Leg.Building = BuildingIndex;
			return Leg;
		};

		// 1. The walk: the first exterior door approach -> every storey landing -> the walkable roof, in storey order.
		FNavLocation DoorNav;
		if (Doors.IsEmpty())
		{
			Record.PlanError = TEXT("no exterior door");
			Record.PlanErrorPos = FVector(Frame.Footprint.GetCenter(), GroundZ);
		}
		else if (!Project(Nav, Data, Frame, Doors[0], true, DoorNav))
		{
			Record.PlanError = FString::Printf(TEXT("exterior approach of %s off navmesh"), *Doors[0].Id);
			Record.PlanErrorPos = Doors[0].Position;
		}
		else
		{
			TArray<const FTarget*> Stops;
			for (const FTarget& Target : Targets)
			{
				if (FCString::Strcmp(Target.Kind, TEXT("room")) != 0)
				{
					Stops.Add(&Target);
				}
			}
			Algo::StableSortBy(Stops, [](const FTarget* Target) { return Target->Position.Z; });
			FString From = Doors[0].Id;
			FVector Start = DoorNav.Location;
			bool bFirst = true;
			for (const FTarget* Stop : Stops)
			{
				FLeg& Leg = AddLeg();
				Leg.Kind = ELegKind::Walk;
				Leg.From = From;
				Leg.To = Stop->Id;
				Leg.ToKind = Stop->Kind;
				Leg.bFirstOfBuilding = bFirst;
				Leg.Start = Start;
				Leg.bStartOk = true;
				FNavLocation GoalNav;
				Leg.bGoalOk = Project(Nav, Data, Frame, *Stop, false, GoalNav);
				Leg.Goal = Leg.bGoalOk ? GoalNav.Location : Stop->Position;
				if (Leg.bGoalOk)
				{
					// The next leg walks on from here (or is teleported here when this one fails).
					From = Stop->Id;
					Start = Leg.Goal;
				}
				bFirst = false;
			}
		}

		// 2. Flat entries: a solo leg, then a squad leg, from the room outside the flat to the flat's room behind the door.
		if (!Walk.Options.bSquad)
		{
			return;
		}
		Record.Scan = BuildingWalkRules::ScanFlatEntries(Building);
		TMap<FString, const FTarget*> Rooms;
		for (const FTarget& Target : Targets)
		{
			if (FCString::Strcmp(Target.Kind, TEXT("room")) == 0)
			{
				Rooms.Add(Target.Id, &Target);
			}
		}
		TMap<FString, TArray<FString>> UnitRooms;
		for (const auto& Value : Array(Building, TEXT("units")))
		{
			const FJsonObject& Unit = *Value->AsObject();
			TArray<FString>& List = UnitRooms.Add(Unit.GetStringField(TEXT("id")));
			for (const auto& Room : Array(Unit, TEXT("rooms")))
			{
				List.Add(Room->AsString());
			}
		}
		for (const FBuildingFlatEntry& Entry : Record.Scan.Entries)
		{
			const FTarget* const* FromRoom = Rooms.Find(Entry.FromRoom);
			const FTarget* const* ToRoom = Rooms.Find(Entry.ToRoom);
			FNavLocation StartNav, GoalNav;
			const bool bStartOk = FromRoom && Project(Nav, Data, Frame, **FromRoom, false, StartNav);
			const bool bGoalOk = ToRoom && Project(Nav, Data, Frame, **ToRoom, false, GoalNav);
			TArray<FBox2D> Flat;
			for (const FString& RoomId : UnitRooms.FindRef(Entry.Unit))
			{
				if (const FTarget* const* Room = Rooms.Find(RoomId))
				{
					Flat.Add((*Room)->Bounds);
				}
			}
			for (const ELegKind Kind : { ELegKind::EntrySolo, ELegKind::EntrySquad })
			{
				FLeg& Leg = AddLeg();
				Leg.Kind = Kind;
				Leg.From = Entry.FromRoom;
				Leg.To = Entry.ToRoom;
				Leg.ToKind = TEXT("room");
				Leg.Entry = Entry.Entry;
				Leg.Unit = Entry.Unit;
				Leg.Role = Entry.Role;
				Leg.bStartOk = bStartOk;
				Leg.Start = bStartOk ? StartNav.Location : (FromRoom ? (*FromRoom)->Position : FVector::ZeroVector);
				Leg.bGoalOk = bGoalOk;
				Leg.Goal = bGoalOk ? GoalNav.Location : (ToRoom ? (*ToRoom)->Position : FVector::ZeroVector);
				Leg.Flat = Flat;
			}
		}
	}

	TSharedRef<FJsonObject> LegJson(const FLeg& Leg)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("kind"), LegKindName(Leg.Kind));
		Out->SetStringField(TEXT("from"), Leg.From);
		Out->SetStringField(TEXT("to"), Leg.To);
		Out->SetStringField(TEXT("to_kind"), Leg.ToKind);
		if (!Leg.Entry.IsEmpty())
		{
			Out->SetStringField(TEXT("entry"), Leg.Entry);
			Out->SetStringField(TEXT("unit"), Leg.Unit);
			Out->SetStringField(TEXT("role"), Leg.Role);
		}
		Out->SetBoolField(TEXT("ran"), Leg.bRan);
		Out->SetBoolField(TEXT("pass"), Leg.bPass);
		Out->SetBoolField(TEXT("arrived"), Leg.bArrived);
		Out->SetBoolField(TEXT("stuck"), Leg.bStuck);
		Out->SetNumberField(TEXT("seconds"), Round1(Leg.Seconds));
		Out->SetNumberField(TEXT("path_cm"), FMath::RoundToDouble(Leg.PathCm));
		Out->SetNumberField(TEXT("walked_cm"), FMath::RoundToDouble(Leg.WalkedCm));
		Out->SetArrayField(TEXT("start"), VectorJson(Leg.Start));
		Out->SetArrayField(TEXT("goal"), VectorJson(Leg.Goal));
		Out->SetArrayField(TEXT("end_pos"), VectorJson(Leg.EndPos));
		if (Leg.Kind == ELegKind::EntrySquad)
		{
			Out->SetNumberField(TEXT("ratio_to_solo"), FMath::RoundToDouble(Leg.Ratio * 100.0) / 100.0);
			Out->SetNumberField(TEXT("followers"), Leg.Followers);
			Out->SetNumberField(TEXT("followers_in_flat"), Leg.FollowersInFlat);
		}
		if (!Leg.bPass)
		{
			Out->SetStringField(TEXT("reason"), Leg.Reason);
			Out->SetArrayField(TEXT("fail_pos"), VectorJson(Leg.FailPos));
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> StringsJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	void FinishWalk(FWalk& Walk)
	{
		Walk.Phase = EWalkPhase::Done;
		int32 Passed = 0, WalkOk = 0, WalkTotal = 0, EntryOk = 0, EntryTotal = 0, PathOk = 0, Mismatch = 0;
		double MaxRatio = 0.0;
		TArray<FString> Failures;
		FJsonObjects BuildingsJson;
		for (const FWalkBuilding& Record : Walk.Buildings)
		{
			TSharedPtr<FJsonObject> First;
			const auto SetFirst = [&First](const FString& Stage, const FString& Reason, const FVector& Pos)
			{
				if (!First)
				{
					First = MakeShared<FJsonObject>();
					First->SetStringField(TEXT("stage"), Stage);
					First->SetStringField(TEXT("reason"), Reason);
					First->SetArrayField(TEXT("pos"), VectorJson(Pos));
				}
			};
			PathOk += Record.bPathPass ? 1 : 0;
			if (!Record.bPathPass && Record.PathResult)
			{
				const TSharedPtr<FJsonObject>* Unreachable = nullptr;
				if (Record.PathResult->TryGetObjectField(TEXT("first_unreachable"), Unreachable) && Unreachable && Unreachable->IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>& Pos = (*Unreachable)->GetArrayField(TEXT("pos"));
					SetFirst(TEXT("path"), FString::Printf(TEXT("%s %s from %s"), *(*Unreachable)->GetStringField(TEXT("reason")),
						*(*Unreachable)->GetStringField(TEXT("target")), *(*Unreachable)->GetStringField(TEXT("door"))),
						FVector(Pos[0]->AsNumber(), Pos[1]->AsNumber(), Pos[2]->AsNumber()));
				}
				else
				{
					SetFirst(TEXT("path"), TEXT("path check failed"), FVector::ZeroVector);
				}
			}
			if (!Record.PlanError.IsEmpty())
			{
				SetFirst(TEXT("walk"), Record.PlanError, Record.PlanErrorPos);
			}
			FJsonObjects WalkLegs, EntryLegs;
			int32 BuildingWalkOk = 0, BuildingWalkTotal = 0, BuildingEntryOk = 0, BuildingEntryTotal = 0;
			for (const int32 Index : Record.Legs)
			{
				const FLeg& Leg = Walk.Legs[Index];
				const bool bWalkLeg = Leg.Kind == ELegKind::Walk;
				(bWalkLeg ? WalkLegs : EntryLegs).Add(MakeShared<FJsonValueObject>(LegJson(Leg)));
				(bWalkLeg ? BuildingWalkTotal : BuildingEntryTotal) += 1;
				(bWalkLeg ? BuildingWalkOk : BuildingEntryOk) += Leg.bPass ? 1 : 0;
				if (Leg.Kind == ELegKind::EntrySquad && Leg.Ratio > MaxRatio)
				{
					MaxRatio = Leg.Ratio;
				}
				if (!Leg.bPass)
				{
					SetFirst(LegKindName(Leg.Kind), FString::Printf(TEXT("%s -> %s%s: %s"), *Leg.From, *Leg.To,
						Leg.Entry.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (entry %s)"), *Leg.Entry), *Leg.Reason), Leg.FailPos);
				}
			}
			WalkOk += BuildingWalkOk;
			WalkTotal += BuildingWalkTotal;
			EntryOk += BuildingEntryOk;
			EntryTotal += BuildingEntryTotal;
			Mismatch += Record.Scan.EntriesWithoutRole.Num() + Record.Scan.RoleNotAnEntry.Num() + Record.Scan.Unresolved.Num();
			const bool bPass = !First.IsValid();
			Passed += bPass ? 1 : 0;
			const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("id"), Record.Id);
			Out->SetBoolField(TEXT("pass"), bPass);
			Out->SetBoolField(TEXT("path_pass"), Record.bPathPass);
			Out->SetNumberField(TEXT("walk_legs_passed"), BuildingWalkOk);
			Out->SetNumberField(TEXT("walk_legs"), BuildingWalkTotal);
			Out->SetNumberField(TEXT("entry_legs_passed"), BuildingEntryOk);
			Out->SetNumberField(TEXT("entry_legs"), BuildingEntryTotal);
			if (First)
			{
				Out->SetObjectField(TEXT("first_failure"), First);
				Failures.Add(FString::Printf(TEXT("%s [%s] %s at %s"), *Record.Id, *First->GetStringField(TEXT("stage")),
					*First->GetStringField(TEXT("reason")), *FString::JoinBy(First->GetArrayField(TEXT("pos")), TEXT(" "),
						[](const TSharedPtr<FJsonValue>& V) { return FString::Printf(TEXT("%.0f"), V->AsNumber()); })));
			}
			else
			{
				Out->SetField(TEXT("first_failure"), MakeShared<FJsonValueNull>());
			}
			Out->SetArrayField(TEXT("walk"), WalkLegs);
			Out->SetArrayField(TEXT("entries"), EntryLegs);
			const TSharedRef<FJsonObject> Plan = MakeShared<FJsonObject>();
			Plan->SetNumberField(TEXT("flat_entries"), Record.Scan.Entries.Num());
			Plan->SetArrayField(TEXT("entries_without_unit_entry_role"), StringsJson(Record.Scan.EntriesWithoutRole));
			Plan->SetArrayField(TEXT("unit_entry_not_an_entry"), StringsJson(Record.Scan.RoleNotAnEntry));
			Plan->SetArrayField(TEXT("unresolved_entries"), StringsJson(Record.Scan.Unresolved));
			Out->SetObjectField(TEXT("plan_roles"), Plan);
			if (Record.PathResult)
			{
				Out->SetObjectField(TEXT("path_check"), Record.PathResult);
			}
			BuildingsJson.Add(MakeShared<FJsonValueObject>(Out));
		}
		const int32 Checked = Walk.Buildings.Num();
		const double MinPass = Walk.Options.MinPass >= 0.0 ? Walk.Options.MinPass : 1.0;
		const bool bPass = !Walk.bBudgetHit && BuildingWalkRules::PassesShare(Passed, Checked, MinPass);
		const double WallSeconds = FPlatformTime::Seconds() - Walk.WallStart;
		const UWorld* World = Walk.World.Get();

		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("schema"), TEXT("codex.buildings_walk/1"));
		Report->SetStringField(TEXT("map"), Walk.MapName);
		Report->SetBoolField(TEXT("pass"), bPass);
		Report->SetNumberField(TEXT("minpass"), MinPass);
		Report->SetNumberField(TEXT("checked"), Checked);
		Report->SetNumberField(TEXT("passed"), Passed);
		Report->SetNumberField(TEXT("share"), Checked > 0 ? FMath::RoundToDouble(1000.0 * Passed / Checked) / 1000.0 : 0.0);
		Report->SetNumberField(TEXT("path_check_passed"), PathOk);
		Report->SetNumberField(TEXT("walk_legs_passed"), WalkOk);
		Report->SetNumberField(TEXT("walk_legs"), WalkTotal);
		Report->SetNumberField(TEXT("entry_legs_passed"), EntryOk);
		Report->SetNumberField(TEXT("entry_legs"), EntryTotal);
		Report->SetNumberField(TEXT("max_squad_ratio"), FMath::RoundToDouble(MaxRatio * 100.0) / 100.0);
		Report->SetNumberField(TEXT("squad_ratio_gate"), BuildingWalkRules::MaxSquadRatio);
		Report->SetNumberField(TEXT("plan_role_mismatches"), Mismatch);
		Report->SetBoolField(TEXT("budget_hit"), Walk.bBudgetHit);
		Report->SetNumberField(TEXT("wall_seconds"), Round1(WallSeconds));
		Report->SetNumberField(TEXT("game_seconds"), World ? Round1(Now(*World)) : -1.0);
		const TSharedRef<FJsonObject> Drift = MakeShared<FJsonObject>();
		Drift->SetBoolField(TEXT("computed"), false);
		Drift->SetStringField(TEXT("why"), TEXT("F5 drift = |C_engine(key_route) - C(key_route)| / C(key_route) needs the TS route model's key-route cost C ")
			TEXT("(building_route_costs.json over the plan graph); the accepted layout carries no key_route / cost, so only the engine side is ")
			TEXT("recorded: per leg path_cm (navmesh) and seconds (game time, walk order), to be joined with C offline."));
		Report->SetObjectField(TEXT("drift"), Drift);
		Report->SetArrayField(TEXT("buildings"), BuildingsJson);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("LevelGen");
		const FString ReportPath = Dir / FString::Printf(TEXT("buildings_walk_%s.json"), *Walk.MapName);
		FString Json;
		const bool bWritten = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir)
			&& FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		for (const FString& Failure : Failures)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke walk failure: %s"), *Failure);
		}
		const int32 Shown = Walk.Options.MaxFail >= 0 ? Walk.Options.MaxFail : 10;
		TArray<FString> ShownFailures = Failures;
		ShownFailures.SetNum(FMath::Min(Failures.Num(), Shown));
		Finish(FString::Printf(TEXT("%s LevelGenBuildingsWalk on %s: %d/%d buildings passed (share %.2f, minpass %.2f); walk legs %d/%d; ")
			TEXT("flat entry legs %d/%d (squad <= %.0f x solo, max %.2f); path check %d/%d; plan role mismatches %d; %.0f s wall%s; failed: %s; report: %s"),
			bPass && bWritten ? TEXT("PASS") : TEXT("FAIL"), *Walk.MapName, Passed, Checked, Checked > 0 ? double(Passed) / Checked : 0.0, MinPass,
			WalkOk, WalkTotal, EntryOk, EntryTotal, BuildingWalkRules::MaxSquadRatio, MaxRatio, PathOk, Checked, Mismatch, WallSeconds,
			Walk.bBudgetHit ? TEXT(" (WALL BUDGET HIT, legs not run)") : TEXT(""), Failures.IsEmpty() ? TEXT("none") : *FString::Join(ShownFailures, TEXT("; ")),
			bWritten ? *ReportPath : *(TEXT("could not write ") + ReportPath)));
	}

	AOperativeCharacter* Member(const FWalk& Walk, int32 Index)
	{
		return Walk.Members.IsValidIndex(Index) ? Walk.Members[Index].Get() : nullptr;
	}

	void EndLeg(FWalk& Walk, UWorld& World, bool bPass, const FString& Reason, const FVector& FailPos)
	{
		FLeg& Leg = Walk.Legs[Walk.Leg];
		Leg.bPass = bPass;
		Leg.Reason = Reason;
		Leg.FailPos = FailPos;
		if (const AOperativeCharacter* Leader = Member(Walk, 0))
		{
			Leg.EndPos = Feet(*Leader);
		}
		if (Leg.Kind == ELegKind::EntrySquad && Walk.Leg > 0)
		{
			const FLeg& Solo = Walk.Legs[Walk.Leg - 1];
			if (Solo.Kind == ELegKind::EntrySolo && Solo.Entry == Leg.Entry && Solo.bArrived && Solo.Seconds > 0.0 && Leg.bArrived)
			{
				Leg.Ratio = Leg.Seconds / Solo.Seconds;
				if (Leg.bPass && !BuildingWalkRules::SquadWithinRatio(Leg.Seconds, Solo.Seconds))
				{
					Leg.bPass = false;
					Leg.Reason = FString::Printf(TEXT("squad %.1f s > %.0f x solo %.1f s (ratio %.2f)"), Leg.Seconds, BuildingWalkRules::MaxSquadRatio,
						Solo.Seconds, Leg.Ratio);
					Leg.FailPos = Leg.Goal;
				}
			}
			else if (Leg.bPass)
			{
				Leg.bPass = false;
				Leg.Reason = TEXT("no solo time to compare (the solo leg failed)");
				Leg.FailPos = Leg.Goal;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke leg %d/%d %s %s %s -> %s%s: %s %.1f s, path %.0f cm, walked %.0f cm%s%s"), Walk.Leg + 1,
			Walk.Legs.Num(), *Walk.Buildings[Leg.Building].Id, LegKindName(Leg.Kind), *Leg.From, *Leg.To,
			Leg.Entry.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (entry %s, role %s)"), *Leg.Entry, *Leg.Role), Leg.bPass ? TEXT("ok") : TEXT("FAIL"),
			Leg.Seconds, Leg.PathCm, Leg.WalkedCm, Leg.Ratio >= 0.0 ? *FString::Printf(TEXT(", ratio %.2f"), Leg.Ratio) : TEXT(""),
			Leg.bPass ? TEXT("") : *FString::Printf(TEXT(" - %s at %s"), *Leg.Reason, *Leg.FailPos.ToCompactString()));
		++Walk.Leg;
		Walk.Phase = EWalkPhase::Place;
	}

	/** Followers round the squad leg's start, behind the leader (navmesh, same storey, a straight navmesh line from the start). */
	void PlaceFollowers(FWalk& Walk, UWorld& World, const FLeg& Leg)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		const FVector2D Dir = FVector2D(Leg.Goal - Leg.Start).GetSafeNormal();
		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D Offsets[] = { -Dir * 110.0 + Side * 50.0, -Dir * 110.0 - Side * 50.0, -Dir * 200.0, Side * 110.0, -Side * 110.0,
			-Dir * 60.0 + Side * 90.0, -Dir * 60.0 - Side * 90.0 };
		TArray<FVector> Taken{ Leg.Start };
		for (int32 Index = 1; Index < Walk.Members.Num(); ++Index)
		{
			AOperativeCharacter* Follower = Member(Walk, Index);
			if (!Follower)
			{
				continue;
			}
			FVector Spot = Leg.Start + FVector(-Dir * (60.0 * Index), 0.0);
			for (const FVector2D& Offset : Offsets)
			{
				FNavLocation OnNav;
				FVector Hit;
				const FVector Try = Leg.Start + FVector(Offset, 0.0);
				if (Nav && Nav->ProjectPointToNavigation(Try + FVector(0, 0, 15), OnNav, FVector(40.0, 40.0, NavHeightTolerance))
					&& FMath::Abs(OnNav.Location.Z - Leg.Start.Z) <= BuildingWalkRules::ArriveZToleranceCm
					&& !UNavigationSystemV1::NavigationRaycast(&World, Leg.Start, OnNav.Location, Hit)
					&& !Taken.ContainsByPredicate([&OnNav](const FVector& Other) { return FVector::Dist2D(Other, OnNav.Location) < 70.0; }))
				{
					Spot = OnNav.Location;
					break;
				}
			}
			Taken.Add(Spot);
			Teleport(*Follower, Spot, FVector(Dir, 0.0));
		}
	}

	void PlaceLeg(FWalk& Walk, UWorld& World)
	{
		FLeg& Leg = Walk.Legs[Walk.Leg];
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Member(Walk, 0);
		Leg.bRan = true;
		if (!Squad || !Leader)
		{
			EndLeg(Walk, World, false, TEXT("no squad leader"), Leg.Start);
			return;
		}
		if (!Leg.bGoalOk)
		{
			EndLeg(Walk, World, false, FString::Printf(TEXT("%s %s off navmesh"), *Leg.ToKind, *Leg.To), Leg.Goal);
			return;
		}
		const FLeg* Previous = Walk.Leg > 0 ? &Walk.Legs[Walk.Leg - 1] : nullptr;
		const bool bTeleport = Leg.Kind != ELegKind::Walk || Leg.bFirstOfBuilding || !Previous || !Previous->bPass;
		if (bTeleport && !Leg.bStartOk)
		{
			EndLeg(Walk, World, false, FString::Printf(TEXT("start %s off navmesh"), *Leg.From), Leg.Start);
			return;
		}
		if (Leg.Kind == ELegKind::EntrySquad)
		{
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(false);
		}
		else
		{
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
			// The followers wait at their start spots, out of the buildings.
			for (int32 Index = 1; Index < Walk.Members.Num(); ++Index)
			{
				AOperativeCharacter* Follower = Member(Walk, Index);
				if (Follower && Walk.Park.IsValidIndex(Index) && FVector::Dist(Feet(*Follower), Walk.Park[Index]) > 200.0)
				{
					Teleport(*Follower, Walk.Park[Index], Follower->GetActorForwardVector());
				}
			}
		}
		if (bTeleport)
		{
			Teleport(*Leader, Leg.Start, Leg.Goal - Leg.Start);
		}
		if (Leg.Kind == ELegKind::EntrySquad)
		{
			PlaceFollowers(Walk, World, Leg);
		}
		Walk.bTeleported = bTeleport || Leg.Kind == ELegKind::EntrySquad;
		Walk.PhaseStart = Now(World);
		Walk.Phase = EWalkPhase::Wait;
	}

	void OrderLeg(FWalk& Walk, UWorld& World)
	{
		FLeg& Leg = Walk.Legs[Walk.Leg];
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Member(Walk, 0);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		const ANavigationData* Data = Nav && Leader ? Nav->GetNavDataForProps(Leader->GetNavAgentPropertiesRef(), Leader->GetActorLocation()) : nullptr;
		if (!Squad || !Leader || !Data)
		{
			EndLeg(Walk, World, false, TEXT("no leader / navmesh"), Leg.Start);
			return;
		}
		FNavLocation From;
		const FVector LeaderFeet = Feet(*Leader);
		if (!Nav->ProjectPointToNavigation(LeaderFeet + FVector(0, 0, 15), From, FVector(85.0, 85.0, NavHeightTolerance), Data))
		{
			EndLeg(Walk, World, false, TEXT("the leader stands off the navmesh before the order"), LeaderFeet);
			return;
		}
		FPathFindingQuery Query(Leader, *Data, From.Location, Leg.Goal);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Path = Nav->FindPathSync(Leader->GetNavAgentPropertiesRef(), Query);
		if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
		{
			EndLeg(Walk, World, false, TEXT("no complete navmesh path"), LeaderFeet);
			return;
		}
		Leg.PathCm = Path.Path->GetLength();
		Squad->SetLeader(Leader);
		if (!Click(World, Leg.Goal))
		{
			EndLeg(Walk, World, false, TEXT("no player controller to click with"), LeaderFeet);
			return;
		}
		Walk.LegStart = Now(World);
		Walk.LastLeaderPos = LeaderFeet;
		Walk.Progress.Init(FBuildingWalkProgress(), Walk.Members.Num());
		Walk.OkSince.Init(-1.0, Walk.Members.Num());
		Walk.MemberStart.Init(FVector::ZeroVector, Walk.Members.Num());
		for (int32 Index = 0; Index < Walk.Members.Num(); ++Index)
		{
			if (const AOperativeCharacter* Operative = Member(Walk, Index))
			{
				Walk.MemberStart[Index] = Feet(*Operative);
			}
		}
		Walk.Phase = EWalkPhase::Run;
	}

	void RunLeg(FWalk& Walk, UWorld& World)
	{
		FLeg& Leg = Walk.Legs[Walk.Leg];
		AOperativeCharacter* Leader = Member(Walk, 0);
		if (!Leader)
		{
			EndLeg(Walk, World, false, TEXT("the leader is gone"), Leg.Goal);
			return;
		}
		const double T = Now(World);
		const double Elapsed = T - Walk.LegStart;
		const FVector LeaderFeet = Feet(*Leader);
		Leg.WalkedCm += FVector::Dist(LeaderFeet, Walk.LastLeaderPos);
		Walk.LastLeaderPos = LeaderFeet;
		const bool bLeaderOk = BuildingWalkRules::IsArrived(LeaderFeet, Leg.Goal);
		const bool bSquad = Leg.Kind == ELegKind::EntrySquad;
		const FBuildingFrame& Frame = Walk.Buildings[Leg.Building].Frame;
		bool bAllOk = true;
		int32 InFlatCount = 0, Followers = 0;
		for (int32 Index = 0; Index < Walk.Members.Num(); ++Index)
		{
			AOperativeCharacter* Operative = Member(Walk, Index);
			if (!Operative || (!bSquad && Index > 0))
			{
				continue;
			}
			const FVector At = Feet(*Operative);
			const UPathFollowingComponent* Follow = nullptr;
			if (const AAIController* AI = Cast<AAIController>(Operative->GetController()))
			{
				Follow = AI->GetPathFollowingComponent();
			}
			const bool bMoving = Follow && Follow->GetStatus() != EPathFollowingStatus::Idle;
			bool bOk = bLeaderOk;
			if (Index > 0)
			{
				++Followers;
				const bool bStorey = FMath::Abs(At.Z - Leg.Goal.Z) <= BuildingWalkRules::ArriveZToleranceCm;
				const bool bInFlat = bStorey && InFlat(Leg, Frame, At);
				InFlatCount += bInFlat ? 1 : 0;
				// In the flat, or settled in his formation slot by the arrived leader (the column stops 330 / 480 cm behind him).
				const bool bSettled = !bMoving && Operative->GetVelocity().Size2D() < 20.0;
				bOk = BuildingWalkRules::IsFollowerIn(bStorey, bInFlat, bLeaderOk, bSettled, FVector::Dist2D(At, LeaderFeet),
					FVector::Dist2D(At, Walk.MemberStart.IsValidIndex(Index) ? Walk.MemberStart[Index] : At));
			}
			double& Since = Walk.OkSince[Index];
			Since = bOk ? (Since < 0.0 ? T : Since) : -1.0;
			bAllOk &= bOk;
			if (!bOk && Walk.Progress[Index].IsStuck(T, At, BuildingWalkRules::StuckMoveCm, BuildingWalkRules::StuckSeconds))
			{
				Leg.bStuck = true;
				Leg.Seconds = Elapsed;
				Leg.FollowersInFlat = InFlatCount;
				Leg.Followers = Followers;
				EndLeg(Walk, World, false, FString::Printf(TEXT("%s%s %s after %.1f s (no progress %.0f s; %.0f cm from the goal 2D, dz %.0f%s)"),
					Index == 0 ? TEXT("leader ") : TEXT("follower "), *Operative->DisplayName.ToString(),
					bMoving ? TEXT("STUCK (move active)") : TEXT("STOPPED (no move order running)"), Elapsed, BuildingWalkRules::StuckSeconds,
					FVector::Dist2D(At, Leg.Goal), At.Z - Leg.Goal.Z,
					Index == 0 ? *FString::Printf(TEXT(", his order to %s"), *Operative->GetLastMoveDestination().ToCompactString())
						: *FString::Printf(TEXT(", %.0f cm from the leader"), FVector::Dist2D(At, LeaderFeet))), At);
				return;
			}
			if (bOk)
			{
				Walk.Progress[Index].Reset(T, At);
			}
		}
		if (bAllOk)
		{
			double LastIn = Walk.OkSince[0];
			for (int32 Index = 1; bSquad && Index < Walk.OkSince.Num(); ++Index)
			{
				LastIn = FMath::Max(LastIn, Walk.OkSince[Index]);
			}
			if (T - LastIn >= (bSquad ? SquadHoldSeconds : SoloHoldSeconds))
			{
				Leg.bArrived = true;
				Leg.Seconds = LastIn - Walk.LegStart;
				Leg.FollowersInFlat = InFlatCount;
				Leg.Followers = Followers;
				EndLeg(Walk, World, true, FString(), FVector::ZeroVector);
				return;
			}
		}
		if (Elapsed > BuildingWalkRules::LegLimitSeconds(Leg.PathCm))
		{
			Leg.Seconds = Elapsed;
			Leg.FollowersInFlat = InFlatCount;
			Leg.Followers = Followers;
			EndLeg(Walk, World, false, FString::Printf(TEXT("TIMED OUT after %.0f s (leader %.0f cm from the goal 2D, dz %.0f, %d/%d followers in the flat)"),
				Elapsed, FVector::Dist2D(LeaderFeet, Leg.Goal), LeaderFeet.Z - Leg.Goal.Z, InFlatCount, Followers), LeaderFeet);
		}
	}

	bool StepWalk(const TSharedRef<FWalk>& WalkRef)
	{
		FWalk& Walk = *WalkRef;
		UWorld* World = Walk.World.Get();
		if (Walk.Phase == EWalkPhase::Done)
		{
			return false;
		}
		if (!World)
		{
			Finish(TEXT("FAIL LevelGenBuildingsWalk: the world is gone"));
			return false;
		}
		if (FPlatformTime::Seconds() > Walk.WallDeadline)
		{
			Walk.bBudgetHit = true;
			for (int32 Index = Walk.Leg; Index < Walk.Legs.Num(); ++Index)
			{
				FLeg& Leg = Walk.Legs[Index];
				Leg.bPass = false;
				Leg.Reason = FString::Printf(TEXT("not run: the %.0f s wall-clock budget ran out"), Walk.Options.BudgetSeconds);
				Leg.FailPos = Leg.Start;
			}
			FinishWalk(Walk);
			return false;
		}
		const double T = Now(*World);
		switch (Walk.Phase)
		{
		case EWalkPhase::Settle:
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (Walk.SettleStart < 0.0)
			{
				Walk.SettleStart = T;
			}
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			const double Waited = T - Walk.SettleStart;
			if (!Leader || Waited < 3.5 || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && Waited < 15.0))
			{
				return true;
			}
			Walk.Members.Add(Leader);
			for (AOperativeCharacter* Operative : Squad->GetMembers())
			{
				if (Operative && Operative != Leader)
				{
					Walk.Members.Add(Operative);
				}
			}
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : Walk.Members)
			{
				AOperativeCharacter* Operative = Weak.Get();
				Walk.Park.Add(Feet(*Operative));
				// The walk checks the buildings, not the cold (as LevelGenRampSmoke): no frostbite crawl mid-leg.
				Operative->ColdLevel = 0.f;
				if (Operative->ColdSurvival)
				{
					Operative->ColdSurvival->SetComponentTickEnabled(false);
				}
				Operative->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenBuildingsWalk on %s: %d buildings, %d legs, %d operatives (leader %s)"), *Walk.MapName,
				Walk.Buildings.Num(), Walk.Legs.Num(), Walk.Members.Num(), *Leader->DisplayName.ToString());
			Walk.Phase = EWalkPhase::Place;
			return true;
		}
		case EWalkPhase::Place:
			if (Walk.Leg >= Walk.Legs.Num())
			{
				FinishWalk(Walk);
				return false;
			}
			PlaceLeg(Walk, *World);
			return true;
		case EWalkPhase::Wait:
			if (T - Walk.PhaseStart >= (Walk.bTeleported ? TeleportSettleSeconds : 0.25))
			{
				OrderLeg(Walk, *World);
			}
			return true;
		case EWalkPhase::Run:
			RunLeg(Walk, *World);
			return true;
		default:
			return false;
		}
	}

	void StartWalk(UWorld& World, const FOptions& Options, const TSharedPtr<FJsonObject>& Layout, const FJsonObjects& PathResults,
		AOperativeCharacter& Agent, UNavigationSystemV1& Nav, const ANavigationData& Data, double WallStart)
	{
		const TSharedRef<FWalk> Walk = MakeShared<FWalk>();
		Walk->World = &World;
		Walk->MapName = FPackageName::GetShortName(World.GetMapName());
		Walk->Options = Options;
		Walk->WallStart = WallStart;
		Walk->WallDeadline = WallStart + Options.BudgetSeconds;
		double GroundZ = 0.0;
		Layout->TryGetNumberField(TEXT("ground_z"), GroundZ);
		TMap<FString, TSharedPtr<FJsonObject>> PathById;
		for (const auto& Value : PathResults)
		{
			PathById.Add(Value->AsObject()->GetStringField(TEXT("id")), Value->AsObject());
		}
		for (const auto& Value : Array(*Layout, TEXT("buildings")))
		{
			const FJsonObject& Building = *Value->AsObject();
			const FString Id = Building.GetStringField(TEXT("id"));
			const TSharedPtr<FJsonObject>* Path = PathById.Find(Id);
			if (!Path)
			{
				continue; // not enterable / not the only= one
			}
			FWalkBuilding& Record = Walk->Buildings.AddDefaulted_GetRef();
			Record.Id = Id;
			Record.PathResult = *Path;
			Record.bPathPass = (*Path)->GetBoolField(TEXT("pass"));
			PlanBuilding(*Walk, Building, GroundZ, Nav, Data);
		}
		int32 Mismatch = 0;
		for (const FWalkBuilding& Record : Walk->Buildings)
		{
			Mismatch += Record.Scan.EntriesWithoutRole.Num() + Record.Scan.RoleNotAnEntry.Num();
			if (!Record.Scan.EntriesWithoutRole.IsEmpty() || !Record.Scan.RoleNotAnEntry.IsEmpty() || !Record.Scan.Unresolved.IsEmpty())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke plan roles %s: flat entries without the unit_entry role [%s]; unit_entry doors no flat lists [%s]; unresolved [%s]"),
					*Record.Id, *FString::Join(Record.Scan.EntriesWithoutRole, TEXT(", ")), *FString::Join(Record.Scan.RoleNotAnEntry, TEXT(", ")),
					*FString::Join(Record.Scan.Unresolved, TEXT(", ")));
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenBuildingsWalk planned %d legs over %d buildings (%d plan role mismatches); budget %.0f s wall"),
			Walk->Legs.Num(), Walk->Buildings.Num(), Mismatch, Options.BudgetSeconds);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Walk](float)
		{
			return StepWalk(Walk);
		}), 0.f);
	}

	bool ParseOptions(const TArray<FString>& Args, FOptions& Options)
	{
		// The sweep runner can supply launch flags via smokeOptions.extra.
		FParse::Value(FCommandLine::Get(), TEXT("-LevelGenBuildingsSmokeMaxFail="), Options.MaxFail);
		FParse::Value(FCommandLine::Get(), TEXT("-LevelGenBuildingsSmokeMinPass="), Options.MinPass);
		Options.bWalk |= FParse::Param(FCommandLine::Get(), TEXT("LevelGenBuildingsSmokeWalk"));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				Options.Only = Arg.Mid(5);
			}
			else if (Arg.StartsWith(TEXT("maxfail=")))
			{
				if (!LexTryParseString(Options.MaxFail, *Arg.Mid(8)) || Options.MaxFail < 0)
				{
					Finish(TEXT("FAIL LevelGenBuildingsSmoke: maxfail must be a nonnegative integer"));
					return false;
				}
			}
			else if (Arg.StartsWith(TEXT("minpass=")))
			{
				if (!LexTryParseString(Options.MinPass, *Arg.Mid(8)) || Options.MinPass < 0.0 || Options.MinPass > 1.0)
				{
					Finish(TEXT("FAIL LevelGenBuildingsSmoke: minpass must be a number in 0..1"));
					return false;
				}
			}
			else if (Arg.StartsWith(TEXT("walk=")))
			{
				Options.bWalk = Arg.Mid(5) != TEXT("0");
			}
			else if (Arg.StartsWith(TEXT("squad=")))
			{
				Options.bSquad = Arg.Mid(6) != TEXT("0");
			}
			else if (Arg.StartsWith(TEXT("budget=")))
			{
				if (!LexTryParseString(Options.BudgetSeconds, *Arg.Mid(7)) || Options.BudgetSeconds <= 0.0)
				{
					Finish(TEXT("FAIL LevelGenBuildingsSmoke: budget must be a positive number of seconds"));
					return false;
				}
			}
		}
		if (Options.MinPass > 1.0)
		{
			Finish(TEXT("FAIL LevelGenBuildingsSmoke: minpass must be a number in 0..1"));
			return false;
		}
		return true;
	}

	void RunWith(FOptions Options, const TArray<FString>& Args, UWorld* World)
	{
		if (!ParseOptions(Args, Options))
		{
			return;
		}
		const double WallStart = FPlatformTime::Seconds();
		// The path check waits up to 60 s for an operative and a ready navmesh; walk mode allows a slow cold start on a big map.
		const double Deadline = WallStart + (Options.bWalk ? 180.0 : 60.0);
		if (Options.bWalk)
		{
			// A walk check, not a fight (as LevelGenLaddersSmoke / LevelGenRampSmoke): no pressure, no PMC squads, no manoeuvre under fire.
			for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Variable->Set(0, ECVF_SetByConsole);
				}
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakWorld = TWeakObjectPtr<UWorld>(World), Options, Deadline, WallStart](float)
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
				return Data ? CheckMap(*MapWorld, Options, *Agent, *Nav, *Data, WallStart) : true;
			}), 0.25f);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(FOptions(), Args, World);
	}

	void RunWalk(const TArray<FString>& Args, UWorld* World)
	{
		FOptions Options;
		Options.bWalk = true;
		RunWith(Options, Args, World);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenBuildingsSmoke"),
		TEXT("Check complete operative nav paths from each exterior door to every room, landing and walkable roof. ")
		TEXT("[only=<building id>] [maxfail=<n> to report without failing for unreachable buildings] [minpass=<0..1> share gate] ")
		TEXT("[walk=1: then walk the leader door -> landings -> roof and the squad through every flat entry (squad=0: no entry legs), budget=<s>]. ")
		TEXT("Writes JSON, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	static FAutoConsoleCommandWithWorldAndArgs WalkCommand(
		TEXT("CodexTactics.LevelGenBuildingsWalkSmoke"),
		TEXT("S2.9 / P0b: CodexTactics.LevelGenBuildingsSmoke walk=1 - the path check, then the leader walks every enterable building (door -> ")
		TEXT("landings -> roof) and the squad goes through every flat entry (squad <= 2 x solo); [minpass=<0..1>, default 1] [only=] [squad=0] [budget=<s>]. ")
		TEXT("Writes Saved/LevelGen/buildings_walk_<map>.json, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunWalk));
}

#endif // !UE_BUILD_SHIPPING
