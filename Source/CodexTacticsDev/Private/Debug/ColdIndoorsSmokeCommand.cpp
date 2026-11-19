// Dev-only console command for the cold indoors of a GENERATED map (T-021, designer 2026-11-19):
//   Scripts/smoke_parallel.ps1 -Commands ColdIndoorsSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1_X6, see Scripts/test_map.json)
// Diagnosis first: the map has no ACameraZoneVolume (the zone multiplier of an operative inside a building was 1 = open air). Then the
// UColdShelterSubsystem's model on the real layout: the leader teleported into a SEALED building (ground-floor door, no open opening)
// -> zone multiplier 0 and no cold accumulates; outside -> the open-air rate; a door kicked in play (SetOpeningOpen, event-driven) -> the
// building's leak is the door's share, the cold near the door is the outdoor value and falls toward the far wall; sealed again -> 0.
// The rates are read through GatherEnvironment + ColdRules::StepCold (a 1 s step from 20 %), like ColdSmoke, so preparation / sprint /
// the weather in force do not matter.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcDefenceRules.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdShelterRules.h"
#include "Survival/ColdShelterSubsystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"

namespace ColdIndoorsSmoke
{
	constexpr float WarmupSeconds = 3.f;

	struct FState
	{
		int32 Checks = 0;
		int32 Failed = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		++State.Checks;
		State.Failed += bOk ? 0 : 1;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke check %s: %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	/** A 1 s cold step from 20 % standing in the open at the leader's current position; the zone multiplier of the gather out. */
	float RatePerSecond(UColdSurvivalComponent& Cold, float& OutZone)
	{
		FColdEnvironment Environment = Cold.GatherEnvironment();
		Environment.bWarm = false;
		Environment.bPreparation = false;
		Environment.bSprinting = false;
		Environment.bElevated = false;
		Environment.bInCover = false;
		OutZone = Environment.ZoneMultiplier;
		return ColdRules::StepCold(Cold.Config, 20.f, 1.f, Environment, EOperativeStance::Standing, Cold.Fortitude) - 20.f;
	}

	void Put(AOperativeCharacter& Leader, float GroundZ, const FVector2D& XY)
	{
		const float HalfHeight = Leader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Leader.SetActorLocation(FVector(XY.X, XY.Y, GroundZ + HalfHeight + 4.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	void Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke checks %d, failed %d"), State.Checks, State.Failed);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failed == 0 && State.Checks > 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ColdIndoorsSmoke"));
	}

	void Start(UWorld* World)
	{
		FState State;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UColdSurvivalComponent* Cold = Leader ? Leader->ColdSurvival.Get() : nullptr;
		UColdShelterSubsystem* Shelter = World->GetSubsystem<UColdShelterSubsystem>();
		if (!Cold || !Shelter)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: leader / cold component / UColdShelterSubsystem missing"));
			Check(State, false, TEXT("leader, cold component and shelter subsystem exist"));
			Finish(State);
			return;
		}

		// 0. Diagnosis: no camera zone on a generated map, so the old rule could never lower the cold indoors.
		int32 Zones = 0;
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
		{
			++Zones;
		}
		const FPmcLayoutAccess* Layout = Shelter->GetLayout();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke diagnosis: %d ACameraZoneVolume on %s, layout %d buildings"), Zones, *World->GetMapName(),
			Layout ? Layout->Buildings.Num() : 0);
		Check(State, Zones == 0, FString::Printf(TEXT("diagnosis: the generated map has no camera zone (%d)"), Zones));
		if (!Layout || Layout->Buildings.IsEmpty())
		{
			Check(State, false, TEXT("the map's levelgen layout loaded (run with -LevelJson=L_Gen_Depot_1_X6.json)"));
			Finish(State);
			return;
		}
		const FColdShelterConfig& Config = Shelter->GetConfig();

		// The test building: the largest one with a sealed ground-floor door (an enterable hall).
		int32 Pick = INDEX_NONE;
		int32 PickDoor = INDEX_NONE;
		float PickArea = 0.f;
		for (int32 Index = 0; Index < Layout->Buildings.Num(); ++Index)
		{
			const FPmcLayoutBuilding& Building = Layout->Buildings[Index];
			const float Area = (Building.Max.X - Building.Min.X) * (Building.Max.Y - Building.Min.Y);
			if (Building.RotationDeg != 0.f || Shelter->GetBuildingLeak(Building.Id) != 0.f || Area <= PickArea)
			{
				continue;
			}
			for (int32 OpeningIndex = 0; OpeningIndex < Building.Openings.Num(); ++OpeningIndex)
			{
				const FPmcLayoutOpening& Opening = Building.Openings[OpeningIndex];
				if (Opening.Kind == TEXT("door") && Opening.Floor == 0 && Opening.bHasPos && !Opening.Id.IsEmpty())
				{
					Pick = Index;
					PickDoor = OpeningIndex;
					PickArea = Area;
					break;
				}
			}
		}
		if (Pick == INDEX_NONE)
		{
			Check(State, false, TEXT("a sealed building with a ground-floor door exists in the layout"));
			Finish(State);
			return;
		}
		const FPmcLayoutBuilding& Building = Layout->Buildings[Pick];
		const FPmcLayoutOpening& Door = Building.Openings[PickDoor];
		const FVector2D Centre = (Building.Min + Building.Max) * 0.5f;
		float WeightAll = 0.f;
		for (const FPmcLayoutOpening& Opening : Building.Openings)
		{
			WeightAll += ColdShelterRules::GetOpeningWeight(Opening, Config);
		}
		const float DoorShare = ColdShelterRules::GetOpeningWeight(Door, Config) / FMath::Max(WeightAll, 1.f);
		// A spot 1.5 m inside from the door (toward the centre), and the far one: the footprint corner furthest from the door, inset 1.5 m.
		const FVector2D ToCentre = (Centre - Door.Pos).GetSafeNormal();
		const FVector2D NearSpot = Door.Pos + ToCentre * 150.f;
		FVector2D FarSpot = Centre;
		float FarDistance = 0.f;
		for (const FVector2D& Corner : { FVector2D(Building.Min.X + 150.f, Building.Min.Y + 150.f), FVector2D(Building.Max.X - 150.f, Building.Min.Y + 150.f),
			FVector2D(Building.Min.X + 150.f, Building.Max.Y - 150.f), FVector2D(Building.Max.X - 150.f, Building.Max.Y - 150.f) })
		{
			const float Distance = FVector2D::Distance(Corner, Door.Pos);
			if (Distance > FarDistance)
			{
				FarDistance = Distance;
				FarSpot = Corner;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke building %s (%.0f x %.0f cm, %d openings, floors %d), door %s at (%.0f, %.0f), door share %.3f, far spot %.1f m from the door"),
			*Building.Id, Building.Max.X - Building.Min.X, Building.Max.Y - Building.Min.Y, Building.Openings.Num(), Building.Floors, *Door.Id,
			Door.Pos.X, Door.Pos.Y, DoorShare, FarDistance / 100.f);

		// 1. Outside: a point 20 m west of the building's west edge, outside every footprint.
		FVector2D OutsideSpot(Building.Min.X - 2000.f, Centre.Y);
		for (int32 Attempt = 0; Attempt < 8 && PmcDefenceRules::BuildingAt(*Layout, FVector(OutsideSpot.X, OutsideSpot.Y, 0.f)) != INDEX_NONE; ++Attempt)
		{
			OutsideSpot.X -= 1500.f;
		}
		float OutsideZone = -1.f;
		Put(*Leader, Layout->GroundZ, OutsideSpot);
		const float OutsideRate = RatePerSecond(*Cold, OutsideZone);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke outside (%.0f, %.0f): zone multiplier %.3f, cold rate %.3f %%/s"), OutsideSpot.X, OutsideSpot.Y, OutsideZone, OutsideRate);
		Check(State, OutsideZone > 0.99f && OutsideRate > 0.05f, FString::Printf(TEXT("outside: the cold accumulates (zone %.2f, %.3f %%/s)"), OutsideZone, OutsideRate));

		// 2. Sealed building: no accumulation.
		float SealedZone = -1.f;
		Put(*Leader, Layout->GroundZ, Centre);
		const float SealedRate = RatePerSecond(*Cold, SealedZone);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke sealed %s centre: zone multiplier %.3f, cold rate %.3f %%/s"), *Building.Id, SealedZone, SealedRate);
		Check(State, SealedZone <= 0.001f && SealedRate <= 0.001f, FString::Printf(TEXT("sealed building: zone %.3f, no accumulation (%.3f %%/s)"), SealedZone, SealedRate));
		float SealedDoorZone = -1.f;
		Put(*Leader, Layout->GroundZ, NearSpot);
		RatePerSecond(*Cold, SealedDoorZone);
		Check(State, SealedDoorZone <= 0.001f, FString::Printf(TEXT("sealed building: also 0 beside the closed door (%.3f)"), SealedDoorZone));

		// 3. Kick the door in play.
		int32 Events = 0;
		float LastNew = -1.f;
		const FDelegateHandle Handle = Shelter->OnLeakChanged.AddLambda([&Events, &LastNew, &Building](const FString& Id, float, float NewLeak)
		{
			if (Id == Building.Id)
			{
				++Events;
				LastNew = NewLeak;
			}
		});
		const bool bKicked = Shelter->SetOpeningOpen(Building.Id, Door.Id, true);
		Check(State, bKicked && Events == 1 && FMath::IsNearlyEqual(LastNew, DoorShare, 1.e-4f),
			FString::Printf(TEXT("a door kicked in play fires one event and the leak is its share (%.3f vs %.3f)"), LastNew, DoorShare));
		Check(State, FMath::IsNearlyEqual(Shelter->GetBuildingLeak(Building.Id), DoorShare, 1.e-4f), TEXT("GetBuildingLeak = the door's share"));

		float NearZone = -1.f;
		float FarZone = -1.f;
		float MidZone = -1.f;
		Put(*Leader, Layout->GroundZ, NearSpot);
		const float NearRate = RatePerSecond(*Cold, NearZone);
		Put(*Leader, Layout->GroundZ, FarSpot);
		const float FarRate = RatePerSecond(*Cold, FarZone);
		const FVector2D MidSpot = Door.Pos + (FarSpot - Door.Pos).GetSafeNormal() * FMath::Min(675.f, FarDistance);
		Put(*Leader, Layout->GroundZ, MidSpot);
		RatePerSecond(*Cold, MidZone);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke door kicked: near (1.5 m) zone %.3f rate %.3f, mid zone %.3f, far (%.1f m) zone %.3f rate %.3f; leak %.3f"),
			NearZone, NearRate, MidZone, FarDistance / 100.f, FarZone, FarRate, DoorShare);
		Check(State, NearZone > 0.99f && NearRate > 0.05f, FString::Printf(TEXT("beside the kicked door the cold is the outdoor value (zone %.2f, %.3f %%/s)"), NearZone, NearRate));
		const float ExpectedFar = FarDistance >= Config.DraughtFadeCm ? DoorShare
			: FarDistance <= Config.DraughtFullCm ? 1.f : FMath::Lerp(1.f, DoorShare, (FarDistance - Config.DraughtFullCm) / (Config.DraughtFadeCm - Config.DraughtFullCm));
		Check(State, FMath::IsNearlyEqual(FarZone, FMath::Max(DoorShare, ExpectedFar), 0.02f) && FarZone < NearZone,
			FString::Printf(TEXT("far from the door it is warmer: zone %.3f (expected %.3f) < %.3f"), FarZone, FMath::Max(DoorShare, ExpectedFar), NearZone));
		Check(State, FarRate < NearRate && FarRate > SealedRate, FString::Printf(TEXT("the drain rises with the door open but is lower at the far wall (sealed %.3f < far %.3f < near %.3f)"), SealedRate, FarRate, NearRate));
		Check(State, MidZone < NearZone + 0.001f && MidZone >= FarZone - 0.001f, FString::Printf(TEXT("between the two the draught fades (mid zone %.3f)"), MidZone));

		// 4. Seal it again.
		const bool bSealed = Shelter->SetOpeningOpen(Building.Id, Door.Id, false);
		float ResealedZone = -1.f;
		Put(*Leader, Layout->GroundZ, NearSpot);
		RatePerSecond(*Cold, ResealedZone);
		Check(State, bSealed && Events == 2 && ResealedZone <= 0.001f, FString::Printf(TEXT("sealed again: a second event, zone back to %.3f"), ResealedZone));
		Check(State, !Shelter->SetOpeningOpen(Building.Id, TEXT("no_such_opening"), true) && !Shelter->SetOpeningOpen(TEXT("no_such_building"), Door.Id, true), TEXT("unknown building / opening ids are refused"));
		Shelter->OnLeakChanged.Remove(Handle);

		// 5. The roof is outside: standing on the building's slab the cold is the open-air one again.
		float RoofZone = -1.f;
		Put(*Leader, Layout->GroundZ + Building.Floors * Layout->LevelHeightCm, Centre);
		RatePerSecond(*Cold, RoofZone);
		Check(State, RoofZone > 0.99f, FString::Printf(TEXT("on the roof slab it is open air (zone %.2f)"), RoofZone));

		Finish(State);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ColdIndoorsSmoke"),
		TEXT("Dev check (generated map): the cold indoors from the levelgen layout - sealed building 0, a kicked door leaks and draughts, outside open air; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
