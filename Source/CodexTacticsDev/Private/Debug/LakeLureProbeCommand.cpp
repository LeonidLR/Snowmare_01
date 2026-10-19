// Dev probe of the Demo 01 A4 lure lesson on the lake (game designer play-test 2026-10-19: «lure the second enemy»):
//   UnrealEditor-Cmd <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -nullrhi -LevelJson=L_Demo01_A_FrozenLake.json
//       -ExecCmds="CodexTactics.LakeLureProbe [throw] [crouch] [ahead=M] [dropafter] [seconds=N]"
// Skips the tutorial, puts the squad at the overlook (the VOL_A4_OVERLOOK volume, where the lesson is taught), Passive and prone
// (crouched with 'crouch'; 'ahead=M' moves the spot M metres towards the nearest patrol route - e.g. 3.3 = right behind the
// overlook's low cover 4 m ahead; 'dropafter': the whole squad goes prone right after the lure, as the player would), and waits for the A4 patrols: the enemies placed in the map, or - none placed (the agents' copy of the
// map) - a Frostbitten and a hound spawned on the two longest patrol routes. It logs each route's closest point to the overlook,
// every enemy's closest approach, and lures as soon as a lure would be heard: the WHISTLE from the squad's spot (default) or a STONE
// thrown at the nearest enemy (clamped to the stance's range; 'throw'). Then it follows the listeners (walk to the noise, the
// search) and ends with "[LakeLure] VERDICT ..." lines: lured (who, how far), detected or not. Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/StealthLureRules.h"
#include "AI/StealthLureSubsystem.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tutorial/TutorialTriggerVolume.h"

namespace LakeLureProbe
{
	constexpr float StepSeconds = 0.1f;

	struct FTrack
	{
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		float ClosestCm = TNumericLimits<float>::Max();
		bool bLured = false;
		bool bReached = false;
		float LuredAt = -1.f;
		float ReachedAt = -1.f;
		float DistanceWhenLuredCm = -1.f;
	};

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bThrow = false;
		bool bCrouch = false;
		float MaxSeconds = 150.f;
		float AheadCm = 0.f;
		bool bDropAfter = false;
		float LureTime = -1.f;
		bool bDetected = false;
		FVector Spot = FVector::ZeroVector;
		TArray<FTrack> Tracks;
	};

	FVector GroundAt(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LakeLureGround), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 1000.f), Point - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params)
			? Hit.ImpactPoint : Point;
	}

	float RouteClosestCm(const APatrolRouteActor& Route, const FVector& Spot, FVector& OutPoint)
	{
		const USplineComponent* Spline = Route.GetRouteSpline();
		float Best = TNumericLimits<float>::Max();
		if (!Spline)
		{
			return Best;
		}
		const float Length = Spline->GetSplineLength();
		for (float At = 0.f; At <= Length; At += 100.f)
		{
			const FVector Point = Spline->GetLocationAtDistanceAlongSpline(At, ESplineCoordinateSpace::World);
			const float Distance = static_cast<float>(FVector::Dist2D(Point, Spot));
			if (Distance < Best)
			{
				Best = Distance;
				OutPoint = Point;
			}
		}
		return Best;
	}

	void Finish(const FState& State)
	{
		int32 Lured = 0;
		for (const FTrack& Track : State.Tracks)
		{
			const AEnemyCharacter* Enemy = Track.Enemy.Get();
			UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] %s: closest approach to the squad %.1f m, lured %d (at %.1f s, %.1f m away), reached the noise %d (%.1f s after)"),
				Enemy ? *Enemy->GetName() : TEXT("(gone)"), Track.ClosestCm / 100.f, Track.bLured ? 1 : 0, Track.LuredAt, Track.DistanceWhenLuredCm / 100.f,
				Track.bReached ? 1 : 0, Track.bReached ? Track.ReachedAt - Track.LuredAt : -1.f);
			Lured += Track.bLured ? 1 : 0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] VERDICT %s from the overlook (%s): %d of %d enemies lured, squad detected %d"),
			State.bThrow ? TEXT("stone") : TEXT("whistle"), State.bCrouch ? TEXT("crouched") : TEXT("prone"), Lured, State.Tracks.Num(), State.bDetected ? 1 : 0);
		FPlatformMisc::RequestExit(false, TEXT("LakeLureProbe"));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		UStealthLureSubsystem* Lure = World ? World->GetSubsystem<UStealthLureSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Squad || !Lure || !Leader)
		{
			if (State.Time > 20.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] no world / squad"));
				FPlatformMisc::RequestExit(false, TEXT("LakeLureProbe"));
				return false;
			}
			return true;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 4.f)
			{
				return true;
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial();
			}
			const ATutorialTriggerVolume* Overlook = nullptr;
			for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
			{
				if (It->GetEffectiveVolumeId() == FName(TEXT("VOL_A4_OVERLOOK")))
				{
					Overlook = *It;
				}
			}
			if (!Overlook)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] no VOL_A4_OVERLOOK on this map"));
				Finish(State);
				return false;
			}
			State.Spot = GroundAt(World, Overlook->GetActorLocation());
			if (State.AheadCm > 0.f)
			{
				// Towards the nearest patrol route (the patrols' side of the overlook).
				FVector Nearest = State.Spot;
				float Best = TNumericLimits<float>::Max();
				for (TActorIterator<APatrolRouteActor> It(World); It; ++It)
				{
					FVector Near;
					const float Distance = RouteClosestCm(**It, State.Spot, Near);
					if (Distance < Best)
					{
						Best = Distance;
						Nearest = Near;
					}
				}
				State.Spot = GroundAt(World, State.Spot + (Nearest - State.Spot).GetSafeNormal2D() * State.AheadCm);
			}
			// The A4 patrols: placed ones, else spawned on the two longest routes (the agents' copy of the map has none placed).
			ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
			TArray<AEnemyCharacter*> Enemies;
			for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
			{
				if (Enemy && !Enemy->IsDying())
				{
					Enemies.Add(Enemy);
				}
			}
			TArray<APatrolRouteActor*> Routes;
			for (TActorIterator<APatrolRouteActor> It(World); It; ++It)
			{
				FVector Near;
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] route %s: %.0f m long, closest point %.1f m from the overlook at %s"), *It->GetName(),
					It->GetRouteSpline() ? It->GetRouteSpline()->GetSplineLength() / 100.f : 0.f, RouteClosestCm(**It, State.Spot, Near) / 100.f, *Near.ToCompactString());
				Routes.Add(*It);
			}
			if (Enemies.IsEmpty())
			{
				Routes.Sort([](const APatrolRouteActor& A, const APatrolRouteActor& B)
				{
					return (A.GetRouteSpline() ? A.GetRouteSpline()->GetSplineLength() : 0.f) > (B.GetRouteSpline() ? B.GetRouteSpline()->GetSplineLength() : 0.f);
				});
				const EEnemyArchetype Types[] = { EEnemyArchetype::Frostbitten, EEnemyArchetype::FrostHound };
				for (int32 Index = 0; Index < FMath::Min(2, Routes.Num()); ++Index)
				{
					const FVector Start = Routes[Index]->GetRouteSpline()->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					AEnemyCharacter* Spawned = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Types[Index], Start + FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
					if (Spawned)
					{
						Spawned->StartPatrol(Routes[Index], nullptr);
						if (Encounter)
						{
							Encounter->MarkLevelEnemy(Spawned);
						}
						Enemies.Add(Spawned);
						UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] spawned %s on %s (no enemy placed in this copy of the map)"), *Spawned->GetName(), *Routes[Index]->GetName());
					}
				}
			}
			for (AEnemyCharacter* Enemy : Enemies)
			{
				FTrack Track;
				Track.Enemy = Enemy;
				State.Tracks.Add(Track);
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] enemy %s at %s, %.1f m from the overlook, patrol %d guard %d"), *Enemy->GetName(),
					*Enemy->GetActorLocation().ToCompactString(), FVector::Dist2D(Enemy->GetActorLocation(), State.Spot) / 100.f, Enemy->IsOnPatrol() ? 1 : 0,
					Enemy->IsGuardingPost() ? 1 : 0);
			}
			// The squad at the overlook, quiet and low.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector Offset = Member == Leader ? FVector::ZeroVector : FVector(-150.f, (Index++ % 2 == 0 ? 150.f : -150.f), 0.f);
				const FVector Feet = GroundAt(World, State.Spot + Offset);
				Member->StopOperative();
				Member->TeleportTo(Feet + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f), FRotator::ZeroRotator, false, true);
				Member->SetStance(State.bCrouch ? EOperativeStance::Crouching : EOperativeStance::Prone);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] squad at the overlook %s (%.1f m ahead of its volume, %s, Passive), waiting for a lure in earshot"),
				*State.Spot.ToCompactString(), State.AheadCm / 100.f, State.bCrouch ? TEXT("crouched") : TEXT("prone"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		case 2:
		{
			const FVector LeaderFeet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			for (FTrack& Track : State.Tracks)
			{
				const AEnemyCharacter* Enemy = Track.Enemy.Get();
				if (!Enemy)
				{
					continue;
				}
				Track.ClosestCm = FMath::Min(Track.ClosestCm, static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), LeaderFeet)));
				if (Track.bLured && !Track.bReached && Enemy->HasReachedSearchOrigin())
				{
					Track.bReached = true;
					Track.ReachedAt = State.Time;
				}
				if (Enemy->IsHuntingSquad() && !State.bDetected)
				{
					State.bDetected = true;
					UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] DETECTED by %s at %.1f s (%.1f m)"), *Enemy->GetName(), State.Time,
						FVector::Dist2D(Enemy->GetActorLocation(), LeaderFeet) / 100.f);
				}
			}
			if (State.Stage == 1 && !UWorldAIPauseSubsystem::IsPausedIn(World))
			{
				// Lure as soon as it would be heard.
				FVector NoisePoint = LeaderFeet;
				if (State.bThrow)
				{
					const AEnemyCharacter* Nearest = nullptr;
					for (const FTrack& Track : State.Tracks)
					{
						if (const AEnemyCharacter* Enemy = Track.Enemy.Get(); Enemy && (!Nearest
							|| FVector::Dist2D(Enemy->GetActorLocation(), LeaderFeet) < FVector::Dist2D(Nearest->GetActorLocation(), LeaderFeet)))
						{
							Nearest = Enemy;
						}
					}
					const float Range = StealthLureRules::GetThrowRange(UStealthLureSubsystem::GetParams(), Leader->GetStance());
					const FVector Dir = Nearest ? (Nearest->GetActorLocation() - LeaderFeet).GetSafeNormal2D() : FVector::ForwardVector;
					const float Reach = Nearest ? FMath::Min(Range, static_cast<float>(FVector::Dist2D(Nearest->GetActorLocation(), LeaderFeet))) : Range;
					NoisePoint = GroundAt(World, LeaderFeet + Dir * Reach);
				}
				const EStealthLureKind Kind = State.bThrow ? EStealthLureKind::Throw : EStealthLureKind::Whistle;
				const TArray<AEnemyCharacter*> Hearing = Lure->FindListeners(NoisePoint, Kind);
				if (!Hearing.IsEmpty() && Lure->GetAvailability(Leader, Kind) == EStealthLureRefusal::None)
				{
					const bool bDone = State.bThrow ? Lure->ThrowAt(Leader, NoisePoint) != nullptr : Lure->Whistle(Leader);
					UE_LOG(LogCodexTactics, Display, TEXT("[LakeLure] %s at %.1f s (noise %s, %.1f m from the squad): %d would hear it, sent %d"),
						State.bThrow ? TEXT("stone") : TEXT("whistle"), State.Time, *NoisePoint.ToCompactString(), FVector::Dist2D(NoisePoint, LeaderFeet) / 100.f,
						Hearing.Num(), bDone ? 1 : 0);
					if (bDone)
					{
						State.Stage = 2;
						State.LureTime = State.Time;
						for (AOperativeCharacter* Member : State.bDropAfter ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
						{
							Member->SetStance(EOperativeStance::Prone);
						}
					}
				}
			}
			if (State.Stage == 2)
			{
				// Who the noise sent (a stone lands later than it is thrown: keep collecting the listeners).
				for (const TWeakObjectPtr<AEnemyCharacter>& Weak : Lure->GetLastListeners())
				{
					for (FTrack& Track : State.Tracks)
					{
						if (Track.Enemy == Weak && !Track.bLured)
						{
							Track.bLured = true;
							Track.LuredAt = State.Time;
							Track.DistanceWhenLuredCm = Weak.IsValid() ? static_cast<float>(FVector::Dist2D(Weak->GetActorLocation(), LeaderFeet)) : -1.f;
						}
					}
				}
				bool bAllThere = true;
				bool bAny = false;
				for (const FTrack& Track : State.Tracks)
				{
					bAny |= Track.bLured;
					bAllThere &= !Track.bLured || Track.bReached;
				}
				if ((bAny && bAllThere && State.Time > State.LureTime + 10.f) || State.Time > State.LureTime + 45.f)
				{
					Finish(State);
					return false;
				}
			}
			if (State.Time > State.MaxSeconds || State.bDetected)
			{
				Finish(State);
				return false;
			}
			return true;
		}
		default:
			Finish(State);
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		for (const FString& Arg : Args)
		{
			State->bThrow |= Arg.Equals(TEXT("throw"), ESearchCase::IgnoreCase);
			State->bCrouch |= Arg.Equals(TEXT("crouch"), ESearchCase::IgnoreCase);
			State->bDropAfter |= Arg.Equals(TEXT("dropafter"), ESearchCase::IgnoreCase);
			if (Arg.StartsWith(TEXT("ahead="), ESearchCase::IgnoreCase))
			{
				State->AheadCm = FMath::Max(0.f, FCString::Atof(*Arg.Mid(6)) * 100.f);
			}
			if (Arg.StartsWith(TEXT("seconds="), ESearchCase::IgnoreCase))
			{
				State->MaxSeconds = FMath::Max(10.f, FCString::Atof(*Arg.Mid(8)));
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LakeLureProbe"),
		TEXT("Dev probe: can the Demo 01 A4 patrols be lured from the overlook? [throw] [crouch] [seconds=N]; logs [LakeLure] lines, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
