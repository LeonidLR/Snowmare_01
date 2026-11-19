// Dev-only console command (smart enemies T-05, GDD A-03 "positions that can fire"), on L_Gen_Depot_X4:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FiringPositionSmoke
// 1. The roof-edge DIAGNOSIS (task step 1): the muzzle line of every stance / rim step from one X4 roof spot to street points ahead of it -
//    what it hits, how far, how tall (the numbers of the report).
// 2. UFiringPositionSubsystem::PositionsCovering over a roof's street, the top chokepoints and a window's street: every returned position is
//    valid, re-traced (uncached) from its own muzzle to >= 1 standable sample of the area, one man per cover object; the second query is the
//    cache, a flushed one gives the same list; the kinds found are logged (a roof that cannot fire at all is a generator finding, logged).
// 3. A shooter (m16: the sniper rifle kneels for every shot - SniperRules::FiringStance - so a sniper query is Crouching only and is logged as a
//    finding below; Codex.Sight 0 so only the LINE is judged) placed on the stand point and stance of a returned roof-edge / window /
//    low-cover position with a passive target standing on a clear sample: the real EvaluateShotLine verdict is SHOT, the real muzzle is the
//    position's FireOrigin, and the shooter's own fire sends a round. Corner positions are re-trace checked only (the real corner shot needs
//    the man IN cover, which an uncommanded spawn is not).
// [seed=<n>]

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/FiringPositionRules.h"
#include "AI/FiringPositionSubsystem.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcOperatorRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "AI/SquadOrderCatalog.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/TacticalMapRules.h"
#include "LevelGen/TacticalMapSubsystem.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"

namespace FiringPositionSmoke
{
	constexpr float WarmupSeconds = 2.f;
	constexpr float SettleSeconds = 1.5f;
	constexpr float FireSeconds = 7.f;
	constexpr float TimeoutSeconds = 480.f;
	constexpr int32 ShooterTeam = 7;
	constexpr int32 TargetTeam = 8;
	constexpr int32 MaxTrials = 5;
	/** The weapon of the trial shooter: the m16 (60 m reach) fires from the stance the position says; the sniper rifle kneels whatever it is. */
	const TCHAR* const MarksmanWeapon = TEXT("m16");

	enum class EStage : uint8
	{
		Begin,
		Query,
		TrialSpawn,
		TrialSettle,
		TrialFire,
		Done
	};

	struct FArea
	{
		FString Name;
		FVector Centre = FVector::ZeroVector;
		float Radius = 1000.f;
		float BandMin = 1500.f;
		float BandMax = 6000.f;
	};

	struct FTrial
	{
		FFiringPosition Position;
		FArea Area;
		FVector TargetFloor = FVector::ZeroVector;
	};

	struct FState
	{
		EStage Stage = EStage::Begin;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 Seed = 7;
		TArray<FArea> Areas;
		TArray<FTrial> Trials;
		int32 TrialIndex = 0;
		int32 Rounds = 0;
		int32 KindCounts[6] = { 0, 0, 0, 0, 0, 0 };
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AOperativeCharacter> Target;
		FDelegateHandle FiredHandle;
		SmokeUtils::FPreloadGate PreloadGate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Cleanup(UWorld* World, FState& State)
	{
		if (AOperativeCharacter* Shooter = State.Shooter.Get(); Shooter && State.FiredHandle.IsValid())
		{
			Shooter->OnWeaponFiredNative.Remove(State.FiredHandle);
		}
		State.FiredHandle.Reset();
		if (UPmcSquadSubsystem* Pmc = World ? UPmcSquadSubsystem::Get(World) : nullptr)
		{
			Pmc->DestroyTeam(ShooterTeam);
			Pmc->DestroyTeam(TargetTeam);
		}
		State.Shooter.Reset();
		State.Target.Reset();
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		State.Stage = EStage::Done;
		Cleanup(World, State);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FiringPositionSmoke"));
		return false;
	}

	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	/** The player's squad out of the way (hidden, no collision, frozen, 1 km off). */
	void ParkPlayerSquad(UWorld* World)
	{
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->SetActorHiddenInGame(true);
				Member->SetActorEnableCollision(false);
				Member->CustomTimeDilation = 0.f;
				Member->SetActorLocation(Member->GetActorLocation() + FVector(100000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	FVector Ahead(const FVector& From, float YawDeg, float DistanceCm)
	{
		return From + FRotator(0.f, YawDeg, 0.f).Vector() * DistanceCm;
	}

	/** A street point (the first of Distances ahead of the spot that projects on the navmesh near the ground). */
	bool StreetPointAhead(UWorld* World, const FTacticalMapFile& File, const FVector& From, float YawDeg, float DistanceCm, FVector& Out)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		const FVector Probe = Ahead(FVector(From.X, From.Y, File.GroundZ), YawDeg, DistanceCm);
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(Probe, OnNav, FVector(300.f, 300.f, 150.f)) || FMath::Abs(OnNav.Location.Z - File.GroundZ) > 150.f)
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	}

	/** Step 1 of the task: what a real shot from the roof spot hits, per stance and rim step. */
	void DiagnoseRoof(UWorld* World, FState& State, UFiringPositionSubsystem& Service, const UTacticalMapSubsystem& Map)
	{
		const FTacticalMapFile& File = Map.GetFile();
		const FTacticalOverwatchRoof* Roof = nullptr;
		for (const FTacticalOverwatchRoof& Candidate : File.OverwatchRoofs)
		{
			if (Candidate.Building == TEXT("B_hall_n"))
			{
				Roof = &Candidate;
			}
		}
		if (!Roof && !File.OverwatchRoofs.IsEmpty())
		{
			Roof = &File.OverwatchRoofs[0];
		}
		Check(State, Roof != nullptr, TEXT("the tactical file has an overwatch roof to diagnose"));
		if (!Roof)
		{
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   DIAG roof %s z %.0f (%d storeys), parapet expectation 95-100 cm; muzzle heights standing %.0f crouching %.0f prone %.0f cm"),
			*Roof->Building, Roof->Z, Roof->Level, Service.GetConfig().MuzzleStandingCm, Service.GetConfig().MuzzleCrouchingCm, Service.GetConfig().MuzzleProneCm);
		int32 Rows = 0;
		for (int32 SpotIndex = 0; SpotIndex < Roof->Spots.Num(); ++SpotIndex)
		{
			const FTacticalOverwatchSpot& Spot = Roof->Spots[SpotIndex];
			for (const float Distance : { 1500.f, 2500.f, 4000.f })
			{
				FVector Target;
				if (!StreetPointAhead(World, File, Spot.Location, Spot.FacingYaw, Distance, Target))
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke   DIAG %s#%d spot %s facing %.0f: no street point %.0f cm ahead on the navmesh"), *Roof->Building, SpotIndex,
						*Spot.Location.ToCompactString(), Spot.FacingYaw, Distance);
					continue;
				}
				const TArray<FFiringTraceDiag> Lines = Service.DiagnoseRoofSpot(Spot.Location, Spot.FacingYaw, Target);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke   DIAG %s#%d spot %s facing %.0f -> street %s (%.0f cm ahead, aim +%.0f cm), the roof's rim is %.0f cm from the spot"), *Roof->Building,
					SpotIndex, *Spot.Location.ToCompactString(), Spot.FacingYaw, *Target.ToCompactString(), Distance, Service.GetConfig().SampleHeightCm,
					Lines.Num() > 0 ? Lines[0].RimDistanceCm : 0.f);
				for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone })
				{
					float FirstClear = -1.f;
					for (const FFiringTraceDiag& Line : Lines)
					{
						if (Line.Stance != Stance)
						{
							continue;
						}
						++Rows;
						if (!Line.bBlocked && FirstClear < 0.f)
						{
							FirstClear = Line.StepCm;
						}
						if (Line.bBlocked)
						{
							UE_LOG(LogCodexTactics, Display, TEXT("Smoke     %-9s step %2.0f cm (walk surface %d) muzzle +%.0f: BLOCKED by %s / %s at %.0f cm, impact +%.0f cm over the floor, its top %s"),
								FiringPositionRules::StanceName(Stance), Line.StepCm, Line.bOnWalkSurface ? 1 : 0, Line.Muzzle.Z - Spot.Location.Z, *Line.HitActor, *Line.HitComponent,
								Line.HitDistanceCm, Line.HitHeightCm, Line.HitTopCm >= 0.f ? *FString::Printf(TEXT("+%.0f cm"), Line.HitTopCm) : TEXT("not found"));
						}
						else
						{
							UE_LOG(LogCodexTactics, Display, TEXT("Smoke     %-9s step %2.0f cm (walk surface %d) muzzle +%.0f: clear"), FiringPositionRules::StanceName(Stance), Line.StepCm,
								Line.bOnWalkSurface ? 1 : 0, Line.Muzzle.Z - Spot.Location.Z);
						}
					}
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke     %-9s first clear stand distance: %s"), FiringPositionRules::StanceName(Stance),
						FirstClear >= 0.f ? *FString::Printf(TEXT("%.0f cm from the spot"), FirstClear) : TEXT("NONE (blocked at every point)"));
				}
			}
		}
		Check(State, Rows > 0, FString::Printf(TEXT("the roof diagnosis traced %d lines"), Rows));
	}

	void BuildAreas(UWorld* World, FState& State, const UTacticalMapSubsystem& Map)
	{
		const FTacticalMapFile& File = Map.GetFile();
		// A street in front of B_hall_n's first spot (the roof G008b used): the roof-edge areas.
		for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
		{
			if (Roof.Building != TEXT("B_hall_n") || Roof.Spots.IsEmpty())
			{
				continue;
			}
			FVector Street;
			for (const float Distance : { 2500.f, 3500.f, 1500.f })
			{
				if (StreetPointAhead(World, File, Roof.Spots[0].Location, Roof.Spots[0].FacingYaw, Distance, Street))
				{
					FArea Area;
					Area.Name = TEXT("roof_B_hall_n");
					Area.Centre = Street;
					Area.Radius = 800.f;
					Area.BandMax = 5500.f;
					State.Areas.Add(Area);
					break;
				}
			}
		}
		// The top-scored places at street level: kill zone candidates.
		TArray<int32> Order;
		for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
		{
			if (FMath::Abs(File.Chokepoints[Index].Location.Z - File.GroundZ) <= 150.f)
			{
				Order.Add(Index);
			}
		}
		Order.StableSort([&File](int32 A, int32 B) { return File.Chokepoints[A].Score > File.Chokepoints[B].Score; });
		for (int32 Rank = 0; Rank < FMath::Min(3, Order.Num()); ++Rank)
		{
			const FTacticalChokepoint& Place = File.Chokepoints[Order[Rank]];
			FArea Area;
			Area.Name = FString::Printf(TEXT("place_%s"), *Place.Id);
			Area.Centre = Place.Location;
			Area.Radius = 1000.f;
			Area.BandMax = 5500.f;
			State.Areas.Add(Area);
		}
		// A window's street: the window with the longest covered length.
		const FTacticalWindow* Best = nullptr;
		for (const FTacticalWindow& Window : File.Windows)
		{
			if (!Best || Window.CoveredM > Best->CoveredM)
			{
				Best = &Window;
			}
		}
		FVector Street;
		if (Best && StreetPointAhead(World, File, Best->Stand, Best->Yaw, 2500.f, Street))
		{
			FArea Area;
			Area.Name = FString::Printf(TEXT("window_%s"), *Best->Id);
			Area.Centre = Street;
			Area.Radius = 800.f;
			Area.BandMax = 5500.f;
			State.Areas.Add(Area);
		}
	}

	void CheckArea(UWorld* World, FState& State, UFiringPositionSubsystem& Service, FArea& Area)
	{
		const int32 GatherBefore = Service.GetGatherCount();
		const TArray<FFiringPosition> Positions = Service.PositionsCovering(Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, 12);
		const FFiringGatherStats Stats = Service.GetLastStats();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   area %s %s r %.0f: %d positions (gather: %d valid, %d traces, %.1f ms)"), *Area.Name, *Area.Centre.ToCompactString(), Area.Radius,
			Positions.Num(), Stats.Valid, Stats.Traces, Stats.Milliseconds);
		const FFiringPositionConfig& Config = Service.GetConfig();
		int32 Retraced = 0;
		bool bAllClear = true;
		bool bAllValid = true;
		TSet<FString> Keys;
		bool bUniqueKeys = true;
		for (const FFiringPosition& Position : Positions)
		{
			int32 Clear = 0;
			int32 Samples = 0;
			Service.RetracePosition(Position, Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, Clear, Samples);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke     %s | re-traced %d of %d samples clear"), *FiringPositionRules::LogLine(Position), Clear, Samples);
			bAllClear &= Clear >= 1 && FiringPositionRules::CoveredShare(Clear, Samples) >= Position.CoveredShare - 0.001f;
			bAllValid &= Position.bValid && Position.CoveredShare >= Config.MinCoveredShare;
			if (!Position.CoverKey.IsEmpty())
			{
				bool bAlready = false;
				Keys.Add(Position.CoverKey, &bAlready);
				bUniqueKeys &= !bAlready;
			}
			State.KindCounts[static_cast<int32>(Position.Kind)] += 1;
			++Retraced;
		}
		Check(State, bAllValid, FString::Printf(TEXT("%s: every returned position is valid (covered share >= %.2f)"), *Area.Name, Config.MinCoveredShare));
		Check(State, bAllClear, FString::Printf(TEXT("%s: all %d returned positions re-trace clear to >= 1 sample with the recorded share"), *Area.Name, Retraced));
		Check(State, bUniqueKeys, FString::Printf(TEXT("%s: one man per cover object (no repeated cover key)"), *Area.Name));
		// The cache: a second query does not gather again and answers the same list.
		const TArray<FFiringPosition> Again = Service.PositionsCovering(Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, 12);
		bool bSame = Again.Num() == Positions.Num();
		for (int32 Index = 0; bSame && Index < Positions.Num(); ++Index)
		{
			bSame &= Again[Index].SourceId == Positions[Index].SourceId && Again[Index].Stance == Positions[Index].Stance;
		}
		Check(State, Service.GetGatherCount() == GatherBefore + 1 && bSame, FString::Printf(TEXT("%s: the second query is served from the cache (%d gather) with the same list"), *Area.Name,
			Service.GetGatherCount() - GatherBefore));
		// A flushed cache gathers again and finds the same positions (deterministic).
		Service.InvalidateCache();
		const TArray<FFiringPosition> Fresh = Service.PositionsCovering(Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, 12);
		bool bDeterministic = Fresh.Num() == Positions.Num();
		for (int32 Index = 0; bDeterministic && Index < Positions.Num(); ++Index)
		{
			bDeterministic &= Fresh[Index].SourceId == Positions[Index].SourceId && FMath::IsNearlyEqual(Fresh[Index].CoveredShare, Positions[Index].CoveredShare, 0.001f);
		}
		Check(State, bDeterministic, FString::Printf(TEXT("%s: a flushed cache gathers the same %d positions again"), *Area.Name, Positions.Num()));
		// The trials: the best roof edge / window / low cover of the first areas that have one (the real shot below).
		for (const FFiringPosition& Position : Positions)
		{
			if (Position.Kind == EFiringPositionKind::Corner || State.Trials.Num() >= MaxTrials)
			{
				continue;
			}
			const bool bKindTaken = State.Trials.ContainsByPredicate([&Position](const FTrial& Trial) { return Trial.Position.Kind == Position.Kind
				&& (Position.Kind != EFiringPositionKind::RoofEdge || Trial.Position.SourceId == Position.SourceId); });
			int32 Clear = 0;
			int32 Samples = 0;
			FVector Floor = FVector::ZeroVector;
			Service.RetracePosition(Position, Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, Clear, Samples, &Floor);
			if (bKindTaken || Clear <= 0)
			{
				continue;
			}
			FTrial Trial;
			Trial.Position = Position;
			Trial.Area = Area;
			Trial.TargetFloor = Floor;
			State.Trials.Add(Trial);
		}
	}

	void StepQuery(UWorld* World, FState& State, UFiringPositionSubsystem& Service, const UTacticalMapSubsystem& Map)
	{
		const FFiringPositionConfig& Config = Service.GetConfig();
		const FFiringPositionConfig Defaults;
		Check(State, FMath::IsNearlyEqual(Config.MuzzleStandingCm, Defaults.MuzzleStandingCm) && Config.CrossfireMinDeg == Defaults.CrossfireMinDeg && Config.AreaSamples == 9,
			TEXT("the firing_positions block of pmc_commander.json was read"));
		DiagnoseRoof(World, State, Service, Map);
		BuildAreas(World, State, Map);
		Check(State, State.Areas.Num() >= 3, FString::Printf(TEXT("%d areas to query (a roof street, the top places, a window street)"), State.Areas.Num()));
		for (FArea& Area : State.Areas)
		{
			CheckArea(World, State, Service, Area);
		}
		// The sniper finding: he kneels for every shot, so only the crouched muzzle counts. A roof edge behind a parapet taller than the kneeling muzzle cannot fire.
		int32 SniperRoofs = 0;
		int32 SniperAll = 0;
		int32 RifleRoofs = 0;
		for (const FArea& Area : State.Areas)
		{
			const TArray<FFiringPosition> Rifle = Service.PositionsCovering(Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, 0);
			const TArray<FFiringPosition> Sniper = Service.PositionsCovering(Area.Centre, Area.Radius, Area.BandMin, Area.BandMax, 0, TArray<FString>(), FiringStances::Crouching);
			const int32 RifleRoof = Rifle.FilterByPredicate([](const FFiringPosition& P) { return P.Kind == EFiringPositionKind::RoofEdge; }).Num();
			const int32 SniperRoof = Sniper.FilterByPredicate([](const FFiringPosition& P) { return P.Kind == EFiringPositionKind::RoofEdge; }).Num();
			RifleRoofs += RifleRoof;
			SniperRoofs += SniperRoof;
			SniperAll += Sniper.Num();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   FINDING area %s: roof-edge positions for a rifleman (stand or crouch) %d, for a sniper (kneeling only) %d; all kinds %d vs %d"), *Area.Name,
				RifleRoof, SniperRoof, Rifle.Num(), Sniper.Num());
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   FINDING roof edges over %d areas: rifleman %d, sniper %d - the sniper rifle kneels (muzzle +%.0f cm) under the parapet"), State.Areas.Num(), RifleRoofs,
			SniperRoofs, Config.MuzzleCrouchingCm);
		const int32 Corners = State.KindCounts[static_cast<int32>(EFiringPositionKind::Corner)];
		const int32 Lows = State.KindCounts[static_cast<int32>(EFiringPositionKind::LowCover)];
		const int32 Windows = State.KindCounts[static_cast<int32>(EFiringPositionKind::Window)];
		const int32 Roofs = State.KindCounts[static_cast<int32>(EFiringPositionKind::RoofEdge)];
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   positions returned by kind: corner %d, low cover %d, window %d, roof edge %d (stair head / obstacle top are not built)"), Corners, Lows, Windows, Roofs);
		Check(State, Corners + Lows + Windows + Roofs >= 3, TEXT("the query returned positions"));
		Check(State, Roofs >= 1, TEXT("a roof-edge position is returned for a roof's street (the rim step finds a stance that clears the parapet)"));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %d real-shot trials: %s"), State.Trials.Num(),
			*FString::JoinBy(State.Trials, TEXT(", "), [](const FTrial& Trial) { return FString::Printf(TEXT("%s %s"), FiringPositionRules::KindName(Trial.Position.Kind), *Trial.Position.SourceId); }));
		State.TrialIndex = 0;
		State.Stage = State.Trials.IsEmpty() ? EStage::Done : EStage::TrialSpawn;
		State.StageTime = 0.f;
	}

	void StepTrialSpawn(UWorld* World, FState& State)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		const FTrial& Trial = State.Trials[State.TrialIndex];
		Cleanup(World, State);
		const FRotator Facing(0.f, Trial.Position.FacingYaw, 0.f);
		AOperativeCharacter* Shooter = Pmc->SpawnFighter(ShooterTeam, Trial.Position.StandPoint + FVector(0.f, 0.f, 100.f), Facing, State.Seed, 0);
		AOperativeCharacter* Target = Pmc->SpawnFighter(TargetTeam, Trial.TargetFloor + FVector(0.f, 0.f, 100.f), FRotator(0.f, Trial.Position.FacingYaw + 180.f, 0.f), State.Seed, 1);
		if (!Shooter || !Target)
		{
			Check(State, false, FString::Printf(TEXT("trial %d: both fighters spawned"), State.TrialIndex));
			++State.TrialIndex;
			State.Stage = State.TrialIndex < State.Trials.Num() ? EStage::TrialSpawn : EStage::Done;
			return;
		}
		for (AOperativeCharacter* Fighter : { Shooter, Target })
		{
			const FString WeaponId = Fighter == Shooter ? MarksmanWeapon : TEXT("m16");
			bool bHas = false;
			for (const TObjectPtr<UWeaponDataAsset>& Weapon : Fighter->AvailableWeapons)
			{
				bHas |= Weapon && Weapon->WeaponId == WeaponId;
			}
			if (!bHas)
			{
				if (UWeaponDataAsset* Weapon = LoadObject<UWeaponDataAsset>(nullptr, *PmcOperatorRules::WeaponAssetPath(WeaponId)))
				{
					Fighter->AddArsenalWeapon(Weapon);
				}
			}
			Fighter->SwitchToWeaponById(WeaponId);
		}
		// The target is passive (he must not shoot the marksman first); the marksman keeps the spawn's Aggressive posture.
		FSquadOrderContext Posture;
		Posture.Operative = Target;
		Posture.Posture = ESquadFirePosture::Passive;
		SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Posture);
		Shooter->SetStance(Trial.Position.Stance);
		State.Rounds = 0;
		State.FiredHandle = Shooter->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter*, AActor*, bool) { ++State.Rounds; });
		State.Shooter = Shooter;
		State.Target = Target;
		State.Stage = EStage::TrialSettle;
		State.StageTime = 0.f;
	}

	void StepTrialSettle(UWorld* World, FState& State)
	{
		AOperativeCharacter* Shooter = State.Shooter.Get();
		AOperativeCharacter* Target = State.Target.Get();
		const FTrial& Trial = State.Trials[State.TrialIndex];
		const FString Label = FString::Printf(TEXT("trial %d %s %s (%s)"), State.TrialIndex, FiringPositionRules::KindName(Trial.Position.Kind), *Trial.Position.SourceId,
			FiringPositionRules::StanceName(Trial.Position.Stance));
		if (!Shooter || !Target)
		{
			Check(State, false, Label + TEXT(": both fighters survived the settle"));
		}
		else
		{
			const FVector Feet = FeetOf(*Shooter);
			const FVector Muzzle = Shooter->GetMuzzleLocation();
			const float StandOffCm = static_cast<float>(FVector::Dist(Feet, Trial.Position.StandPoint));
			const float MuzzleHeight = static_cast<float>(Muzzle.Z - Feet.Z);
			const float Expected = Trial.Position.FireOrigin.Z - Trial.Position.StandPoint.Z;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %s: shooter feet %s (%.0f cm off the stand point), muzzle +%.0f cm (position +%.0f cm), stance %s"), *Label, *Feet.ToCompactString(),
				StandOffCm, MuzzleHeight, Expected, FiringPositionRules::StanceName(Shooter->GetStance()));
			Check(State, StandOffCm <= 60.f, Label + FString::Printf(TEXT(": the man stands on the stand point (%.0f cm off)"), StandOffCm));
			Check(State, FMath::Abs(MuzzleHeight - Expected) <= 3.f, Label + FString::Printf(TEXT(": the real muzzle height +%.0f cm = the position's +%.0f cm (muzzle_height_cm mirrors GetMuzzleLocation)"), MuzzleHeight, Expected));
			const FString Verdict = Shooter->DescribeShotLine(Target);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %s: real shot line to the target: %s"), *Label, *Verdict);
			Check(State, Verdict.StartsWith(TEXT("SHOT")), Label + TEXT(": the real EvaluateShotLine verdict is a shot at a target standing in the area"));
		}
		State.Stage = EStage::TrialFire;
		State.StageTime = 0.f;
	}

	void StepTrialFire(UWorld* World, FState& State)
	{
		const FTrial& Trial = State.Trials[State.TrialIndex];
		if (State.Rounds < 1 && State.StageTime < FireSeconds)
		{
			return;
		}
		Check(State, State.Rounds >= 1, FString::Printf(TEXT("trial %d %s %s: the marksman on the position fires at the target (%d round(s) in %.1f s)"), State.TrialIndex,
			FiringPositionRules::KindName(Trial.Position.Kind), *Trial.Position.SourceId, State.Rounds, State.StageTime));
		++State.TrialIndex;
		State.Stage = State.TrialIndex < State.Trials.Num() ? EStage::TrialSpawn : EStage::Done;
		State.StageTime = 0.f;
	}

	bool Step(UWorld* World, FState& State, float DeltaSeconds)
	{
		switch (State.PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.PreloadGate.FailReason);
			return Finish(State, World, false);
		default:
			break;
		}
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UFiringPositionSubsystem* Service = UFiringPositionSubsystem::Get(World);
		UTacticalMapSubsystem* Tactical = World->GetSubsystem<UTacticalMapSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Pmc || !Service || !Tactical || State.Time > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d, %.1f s)"), static_cast<int32>(State.Stage), State.Time));
			return Finish(State, World, false);
		}
		switch (State.Stage)
		{
		case EStage::Begin:
			if (State.StageTime < WarmupSeconds || !Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
			{
				return true;
			}
			Check(State, Tactical->IsLoaded() && !Tactical->GetFile().OverwatchRoofs.IsEmpty(), FString::Printf(TEXT("the map's tactical file is loaded (%s)"), *FPaths::GetCleanFilename(Tactical->GetLoadedPath())));
			ParkPlayerSquad(World);
			State.Stage = EStage::Query;
			State.StageTime = 0.f;
			return true;
		case EStage::Query:
			StepQuery(World, State, *Service, *Tactical);
			return State.Stage != EStage::Done || Finish(State, World, true);
		case EStage::TrialSpawn:
			StepTrialSpawn(World, State);
			return State.Stage != EStage::Done || Finish(State, World, true);
		case EStage::TrialSettle:
			if (State.StageTime >= SettleSeconds)
			{
				StepTrialSettle(World, State);
			}
			return true;
		case EStage::TrialFire:
			StepTrialFire(World, State);
			return State.Stage != EStage::Done || Finish(State, World, true);
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		for (const FString& Arg : Args)
		{
			FString Key;
			FString Value;
			if (Arg.Split(TEXT("="), &Key, &Value) && Key.Equals(TEXT("seed"), ESearchCase::IgnoreCase))
			{
				State->Seed = FCString::Atoi(*Value);
			}
		}
		// Nobody to meet: the pressure director, the level's PMC encounters and the horde stay off; Codex.Sight 0 so only the LINE is judged.
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Horde.Enabled") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		if (IConsoleVariable* Sight = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Sight")))
		{
			Sight->Set(0, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: FiringPositionSmoke on %s (seed %d)"), *World->GetMapName(), State->Seed);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return Finish(*State, nullptr, false);
			}
			return State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FiringPositionSmoke"),
		TEXT("Dev check (smart enemies T-05, GDD A-03): the roof-edge muzzle-trace diagnosis on L_Gen_Depot_X4, UFiringPositionSubsystem::PositionsCovering over a roof street, ")
		TEXT("the top places and a window street (every returned position re-traced clear, one man per cover object, cache + determinism), and a sniper placed on a ")
		TEXT("returned roof-edge / window / low-cover position fires at a target standing in its area (real shot-line verdict + a round). [seed=<n>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
