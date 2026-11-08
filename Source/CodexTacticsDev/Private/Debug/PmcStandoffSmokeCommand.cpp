// Dev-only check of the PMC engagement bands (user request 2026-11-04: "a man with an assault rifle keeps as far from his target as his weapon
// allows, fires from corners, prone, never fights at point-blank range; the shotgun is the close-range weapon"; docs/systems/pmc-squad-ai.md 4)
// on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PmcStandoffSmoke
// The squad (immortal, Passive, holding) on an empty arena; a 4 m x 0.85 m wall 19.6 m ahead, 4.5 m aside (its far side = a slot in the rifle's band).
//  1. three PMC riflemen (m16) spawned 8 m from the squad, facing it: each is "too close" -> a FALL BACK (the standoff), within 30 s every
//     rifleman stands >= the band's min (19 m) from the nearest operative, one of them in cover behind the wall (the band slot) firing;
//  2. three shotgunners spawned 15 m out: they close in (an Advance / a flank at the band) - one within 8 m of the squad inside 30 s.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "AI/PmcOperatorRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace PmcStandoffSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr int32 RifleTeam = 1;
	constexpr int32 ShotgunTeam = 2;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Origin = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TMap<TWeakObjectPtr<AOperativeCharacter>, int32> ClipAtArrival;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FPmcEngagementBand Band;
		bool bAllOut = false;
		bool bFiredFromCover = false;
		float BestCoverDistance = 0.f;
		FString FiredHow = TEXT("-");
		float ClosestShotgun = TNumericLimits<float>::Max();
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (AStaticMeshActor* Wall = State.Wall.Get())
		{
			Wall->Destroy();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcStandoffSmoke"));
		return false;
	}

	/** Planar distance from Fighter to the nearest live squad member. */
	float NearestSquad(const USquadSubsystem& Squad, const AOperativeCharacter& Fighter)
	{
		float Best = TNumericLimits<float>::Max();
		for (const AOperativeCharacter* Member : Squad.GetMembers())
		{
			Best = FMath::Min(Best, static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Fighter.GetActorLocation())));
		}
		return Best;
	}

	/** Spawns a team of Count fighters at Centre facing Yaw, every one with WeaponId in hands. */
	void SpawnTeam(UWorld* World, UPmcSquadSubsystem& Pmc, FState& State, int32 TeamId, const FVector& Centre, const FRotator& Facing, const TCHAR* WeaponId, int32 Seed)
	{
		State.Fighters.Reset();
		for (AOperativeCharacter* Fighter : Pmc.SpawnSquad(TeamId, Centre, Facing, 3, Seed))
		{
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
			State.Fighters.Add(Fighter);
		}
		FPmcWeights Weights;
		PmcCommanderRules::LoadWeightsFile(FString(), Weights);
		Pmc.SetTeamCommanded(TeamId, true, &Weights);
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Squad || !Pmc || Squad->GetMembers().Num() < 3)
		{
			Check(State, false, TEXT("squad of 3 and UPmcSquadSubsystem present"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-500.f, -1600.f), FVector2D(3000.f, 1600.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.Origin = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Variable->Set(1, ECVF_SetByConsole);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				Members[Index]->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, State.Origin + State.R * ((Index - 1) * 200.f), Members[Index]);
				Members[Index]->TeleportTo(Spot, State.F.Rotation(), false, true);
			}
			// The wall whose far side lies in the rifle's band: its near face 19.4 m out, 40 cm thick, 4 m wide, 1.1 m tall (low cover).
			// 4.5 m to the side: the riflemen's straight retreat passes it, the wall then stands between the band point and the squad
			// (a cover order never has to wrap round the wall from the threat's side).
			const FVector WallXY = State.Origin + State.F * 1960.f + State.R * 450.f;
			FHitResult Ground;
			FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(PmcStandoffGround), false);
			for (const AOperativeCharacter* Member : Members)
			{
				GroundParams.AddIgnoredActor(Member);
			}
			const float GroundZ = World->LineTraceSingleByChannel(Ground, WallXY + FVector(0.f, 0.f, 500.f), WallXY - FVector(0.f, 0.f, 1000.f), ECC_Visibility, GroundParams)
				? static_cast<float>(Ground.ImpactPoint.Z) : static_cast<float>(State.Origin.Z) - Members[0]->GetSimpleCollisionHalfHeight();
			// 85 cm: under a crouched man's eyes (95 cm) - he sees over it at 20 m with his own eyes (since #32 the over-the-top peek reaches
			// his sight range too, GetPeekSightRangeCm).
			State.Wall = SpawnWall(World, FVector(WallXY.X, WallXY.Y, GroundZ + 42.5f), State.F.Rotation(), FVector(0.4f, 4.f, 0.85f));
			Check(State, State.Wall.IsValid(), TEXT("a low wall (85 cm) 19.6 m ahead (the band slot behind it)"));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// The dynamic navmesh round the new wall: a path from 10 m to 21 m ahead must detour round it (up to 8 s).
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				const FVector From = State.Origin + State.F * 1000.f + State.R * 450.f;
				const FVector To = State.Origin + State.F * 2150.f + State.R * 450.f;
				const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
				const bool bDetour = Path && Path->IsValid() && !Path->IsPartial() && Path->GetPathLength() > FVector::Dist2D(From, To) + 80.f;
				if (!bDetour && State.StageTime < 8.f)
				{
					return true;
				}
				Check(State, bDetour, FString::Printf(TEXT("the navmesh goes round the wall after %.1f s (path %.0f cm for %.0f straight)"), State.StageTime,
					Path && Path->IsValid() ? Path->GetPathLength() : -1.f, FVector::Dist2D(From, To)));
			}
			SpawnTeam(World, *Pmc, State, RifleTeam, State.Origin + State.F * 800.f, (-State.F).Rotation(), TEXT("m16"), 31);
			const AOperativeCharacter* First = State.Fighters.Num() > 0 ? State.Fighters[0].Get() : nullptr;
			const FPmcWeights* Weights = Pmc->GetTeamWeights(RifleTeam);
			State.Band = First && First->CurrentWeapon && Weights ? PmcCommanderRules::ResolveBand(First->CurrentWeapon->WeaponId, First->CurrentWeapon->Stats.Range.MaxRangeCm, *Weights)
				: FPmcEngagementBand();
			Check(State, State.Fighters.Num() == 3 && First && First->CurrentWeapon && First->CurrentWeapon->WeaponId == TEXT("m16") && State.Band.IsValid(),
				FString::Printf(TEXT("three PMC riflemen (m16) 8 m from the squad; their band %.1f-%.1f m"), State.Band.MinCm / 100.f, State.Band.MaxCm / 100.f));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Up to 30 s: everybody out to the band, one firing from the wall's band slot.
			bool bAllOut = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
			{
				AOperativeCharacter* Fighter = Weak.Get();
				if (!Fighter)
				{
					continue;
				}
				const float Distance = NearestSquad(*Squad, *Fighter);
				bAllOut &= Distance >= State.Band.MinCm - 50.f;
				// Firing at band distance from cover or low (crouched / prone): the rounds he spends once there.
				if (Distance >= State.Band.MinCm - 50.f && (Fighter->bInCover || Fighter->GetStance() != EOperativeStance::Standing))
				{
					const int32 Rounds = Fighter->CurrentClip + Fighter->ReserveAmmo;
					if (const int32* AtArrival = State.ClipAtArrival.Find(Weak))
					{
						if (Rounds < *AtArrival && !State.bFiredFromCover)
						{
							State.bFiredFromCover = true;
							State.BestCoverDistance = Distance;
							State.FiredHow = Fighter->bInCover ? TEXT("in cover") : (Fighter->GetStance() == EOperativeStance::Prone ? TEXT("prone") : TEXT("crouched"));
						}
					}
					else
					{
						State.ClipAtArrival.Add(Weak, Rounds);
					}
				}
			}
			State.bAllOut = bAllOut;
			if (State.StageTime < 30.f && !(bAllOut && State.bFiredFromCover))
			{
				return true;
			}
			const FPmcTeamStats* Stats = Pmc->GetTeamStats(RifleTeam);
			FString Where;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
			{
				if (const AOperativeCharacter* Fighter = Weak.Get())
				{
					Where += FString::Printf(TEXT(" %.1f m%s%s %s;"), NearestSquad(*Squad, *Fighter) / 100.f, Fighter->bInCover ? TEXT(" in cover") : TEXT(""),
						Fighter->GetStance() == EOperativeStance::Prone ? TEXT(" prone") : (Fighter->GetStance() == EOperativeStance::Crouching ? TEXT(" crouched") : TEXT("")),
						PmcCommanderRules::OrderName(Pmc->GetFighterOrder(Fighter)));
				}
			}
			Check(State, Stats && Stats->Standoffs >= 1, FString::Printf(TEXT("the riflemen 8 m off backed away (%d standoff retreats)"), Stats ? Stats->Standoffs : -1));
			Check(State, State.bAllOut, FString::Printf(TEXT("every rifleman stands >= %.1f m (the band's min) from the squad after %.0f s:%s"), State.Band.MinCm / 100.f,
				State.StageTime, *Where));
			Check(State, State.bFiredFromCover, FString::Printf(TEXT("a rifleman fires at band distance from cover / low (%s, %.1f m)"), *State.FiredHow,
				State.BestCoverDistance / 100.f));
			Check(State, Stats && Stats->FriendlyFireHits == 0, TEXT("no friendly fire"));
			Pmc->SetTeamCommanded(RifleTeam, false);
			Pmc->DestroyTeam(RifleTeam);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// The shotgunners 15 m out to the side of the wall: they close in.
			SpawnTeam(World, *Pmc, State, ShotgunTeam, State.Origin + State.F * 1500.f + State.R * 900.f, (-State.F).Rotation(), TEXT("shotgun"), 47);
			const AOperativeCharacter* First = State.Fighters.Num() > 0 ? State.Fighters[0].Get() : nullptr;
			Check(State, State.Fighters.Num() == 3 && First && First->CurrentWeapon && First->CurrentWeapon->WeaponId == TEXT("shotgun"),
				TEXT("three PMC shotgunners 15 m from the squad"));
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
			{
				if (const AOperativeCharacter* Fighter = Weak.Get())
				{
					State.ClosestShotgun = FMath::Min(State.ClosestShotgun, NearestSquad(*Squad, *Fighter));
				}
			}
			if (State.StageTime < 30.f && State.ClosestShotgun > 800.f)
			{
				return true;
			}
			const FPmcTeamStats* Stats = Pmc->GetTeamStats(ShotgunTeam);
			Check(State, State.ClosestShotgun <= 800.f, FString::Printf(TEXT("a shotgunner closed in to %.1f m (<= 8 m) within %.0f s (%d advances, %d flanks)"),
				State.ClosestShotgun / 100.f, State.StageTime, Stats ? Stats->Advances : -1, Stats ? Stats->FlanksAttempted : -1));
			Check(State, Stats && Stats->Standoffs == 0, TEXT("... and no shotgunner backed away (close quarters)"));
			Pmc->SetTeamCommanded(ShotgunTeam, false);
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcStandoffSmoke"),
		TEXT("Dev check (user request 2026-11-04): PMC riflemen 8 m from the squad back off to their band (>= 19 m) and fire from cover there; shotgunners close in. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
