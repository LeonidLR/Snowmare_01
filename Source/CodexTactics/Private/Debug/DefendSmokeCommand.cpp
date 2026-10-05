// Dev-only headless check of the Sprint 10 defense line («Рубеж обороны») on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.DefendSmoke -Log Smoke-Defend.log
// A wave fight with the wave parked frozen far away, Commander Mode on. The commander is ordered to hold a barricade
// 6 m off (the defended object). Checks: he walks there and stays inside the 5 m leash; a frozen hound at the object
// is his target while a frozen marksman (tier 3) stands farther off; a wounded mate 15 m away gets no aid from him
// while the hound is there; the hound at point-blank range: he holds the spot (no walk) and draws the knife.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/DefenseMarkerSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Characters/SquadAutonomySubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"

namespace DefendSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Start = FVector::ZeroVector;
		FVector Object = FVector::ZeroVector;
		FVector Away = FVector::ForwardVector;
		float GroundZ = 0.f;
		float WorstLeashCm = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Defender;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AEnemyCharacter> Marksman;
		int32 AidRefusedBefore = 0;
		int32 AidMovesBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DefendSmoke"));
		return false;
	}

	AEnemyCharacter* SpawnFrozen(UWorld* World, EEnemyArchetype Type, const FVector& Ground)
	{
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, Ground + FVector(0.f, 0.f, 120.f));
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f;
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
			Enemy->SetActorLocation(Ground + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight() + 2.f));
		}
		return Enemy;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USquadAutonomySubsystem* Autonomy = World->GetSubsystem<USquadAutonomySubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		// The level wave (its spawn points are around the squad on this map): parked frozen 40 m away.
		if (State.Stage > 0)
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != State.Hound.Get() && *It != State.Marksman.Get())
				{
					It->CustomTimeDilation = 0.f;
					if (FVector::Dist2D(It->GetActorLocation(), State.Start) < 3000.f)
					{
						It->SetActorLocation(State.Start + FVector(4000.f, 4000.f, 2000.f), false, nullptr, ETeleportType::TeleportPhysics);
					}
				}
			}
		}
		AOperativeCharacter* Defender = State.Defender.Get();
		if (Defender && Defender->TacticalAnchor.Defense.IsActive())
		{
			State.WorstLeashCm = FMath::Max(State.WorstLeashCm, static_cast<float>(FVector::Dist2D(Defender->GetActorLocation(), Defender->TacticalAnchor.Location))
				* (Defender->IsMoving() ? 0.f : 1.f));
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			State.Defender = Squad->GetLeader();
			Defender = State.Defender.Get();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true; // targets are checked, nobody needs to die
				if (Member != Defender && !State.Patient.IsValid())
				{
					State.Patient = Member;
				}
			}
			State.Start = Defender->GetActorLocation();
			State.GroundZ = Defender->GetActorLocation().Z - Defender->GetSimpleCollisionHalfHeight();
			State.Away = Defender->GetActorForwardVector().GetSafeNormal2D();
			State.Object = SmokeUtils::ClearPoint(World, State.Start, State.Start + State.Away * 600.f);
			State.Object.Z = State.GroundZ;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Barricade = World->SpawnActor<ABarricadeActor>(State.Object + FVector(0.f, 0.f, ABarricadeActor::HeightCm * 0.5f),
				FRotator(0.f, State.Away.Rotation().Yaw + 90.f, 0.f), Params);
			Squad->SetAutonomousSquadCombat(true);
			Check(State, State.Barricade.IsValid() && Autonomy->SetDefenseObjective(Defender, State.Barricade.Get(), State.Object)
				&& Defender->TacticalAnchor.Defense.IsActive(), TEXT("defense line set on a barricade 6 m off"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 8.f)
			{
				return true;
			}
			const float ToObject = FVector::Dist2D(Defender->GetActorLocation(), State.Object);
			Check(State, ToObject <= 650.f, FString::Printf(TEXT("he went to the object: %.1f m from it"), ToObject / 100.f));
			// The visuals: one marker, faded in, the green fresnel overlay on the barricade's mesh.
			const UDefenseMarkerSubsystem* Markers = World->GetSubsystem<UDefenseMarkerSubsystem>();
			const UMeshComponent* Mesh = State.Barricade.IsValid() ? State.Barricade->FindComponentByClass<UMeshComponent>() : nullptr;
			const bool bOverlay = Mesh && Cast<UMaterialInstanceDynamic>(Mesh->GetOverlayMaterial())
				&& Mesh->GetOverlayMaterial()->GetMaterial()->GetName().Contains(TEXT("TargetFresnel"));
			Check(State, Markers && Markers->GetMarkerCount() == 1 && Markers->GetMarkerAlpha(State.Barricade.Get()) >= 0.99f && bOverlay
				&& Markers->GetMarkers().Num() == 1 && Markers->GetMarkers()[0].Defenders == 1,
				FString::Printf(TEXT("visuals: marker %d, alpha %.2f, green fresnel overlay %d"), Markers ? Markers->GetMarkerCount() : -1,
					Markers ? Markers->GetMarkerAlpha(State.Barricade.Get()) : -1.f, bOverlay ? 1 : 0));
			// The hound right at the object (an intruder), the marksman farther off to the side (tier 3, not an intruder).
			const FVector Side = FVector::CrossProduct(FVector::UpVector, State.Away);
			State.Hound = SpawnFrozen(World, EEnemyArchetype::FrostHound, State.Object + State.Away * 250.f);
			State.Marksman = SpawnFrozen(World, EEnemyArchetype::Marksman, State.Object + State.Away * 400.f + Side * 1400.f);
			// A wounded mate far off.
			if (AOperativeCharacter* Patient = State.Patient.Get())
			{
				Patient->StopOperative();
				Patient->TeleportTo(SmokeUtils::ClearPoint(World, State.Start, State.Start - State.Away * 1200.f) + FVector(0.f, 0.f, Patient->GetSimpleCollisionHalfHeight() + 5.f),
					Patient->GetActorRotation(), false, true);
				Patient->HealthComponent->ApplyDirectHealthLoss(85000.f, TEXT("smoke"));
			}
			Defender->MedkitsCount = 2;
			State.AidRefusedBefore = Autonomy->GetStats().AidRefusedDefense;
			State.AidMovesBefore = Autonomy->GetStats().AidMoves;
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			const AActor* Target = Defender->GetAutonomyTarget();
			Check(State, State.Hound.IsValid() && Target == State.Hound.Get(), FString::Printf(TEXT("target: the hound at the object, not the marksman (%s)"),
				Target ? *Target->GetName() : TEXT("none")));
			const FSquadAutonomyStats& Stats = Autonomy->GetStats();
			// No aid run (the counter of refusals depends on which rule stopped him first: the point-blank hold, the leash or the line).
			Check(State, Stats.AidMoves == State.AidMovesBefore && FVector::Dist2D(Defender->GetActorLocation(), State.Object) <= 550.f,
				FString::Printf(TEXT("no aid run to the far wounded mate while the hound is there (aid runs %d, refused for the line %d)"),
					Stats.AidMoves - State.AidMovesBefore, Stats.AidRefusedDefense - State.AidRefusedBefore));
			// The hound at point-blank range.
			if (AEnemyCharacter* Hound = State.Hound.Get())
			{
				const FVector Feet = Defender->GetActorLocation() - FVector(0.f, 0.f, Defender->GetSimpleCollisionHalfHeight());
				Hound->SetActorLocation(Feet + State.Away * 140.f + FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight() + 2.f));
			}
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			const FSquadAutonomyStats& Stats = Autonomy->GetStats();
			const bool bKnife = Defender->CurrentWeapon && Defender->CurrentWeapon->WeaponId == TEXT("knife");
			const bool bHasKnife = Defender->AvailableWeapons.ContainsByPredicate([](const UWeaponDataAsset* W) { return W && W->WeaponId == TEXT("knife"); });
			Check(State, Stats.DefenseHolds > 0 && !Defender->IsMoving(), FString::Printf(TEXT("point-blank: he holds the spot (holds %d)"), Stats.DefenseHolds));
			Check(State, bKnife || !bHasKnife, FString::Printf(TEXT("body-block: the knife drawn (%s)"),
				Defender->CurrentWeapon ? *Defender->CurrentWeapon->WeaponId : TEXT("-")));
			Check(State, State.WorstLeashCm <= 550.f, FString::Printf(TEXT("never beyond the 5 m defense leash (worst %.1f m)"), State.WorstLeashCm / 100.f));
			// A new player order lifts the line: the marker fades out.
			if (AEnemyCharacter* Hound = State.Hound.Get())
			{
				Hound->Destroy();
			}
			Defender->OrderMoveTo(State.Start, false);
			Check(State, !Defender->TacticalAnchor.Defense.IsActive(), TEXT("a move order lifts the line"));
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			const UDefenseMarkerSubsystem* Markers = World->GetSubsystem<UDefenseMarkerSubsystem>();
			if (State.Time < 0.4f)
			{
				return true;
			}
			if (State.Stage == 4 && State.Time < 0.5f)
			{
				const float Alpha = Markers ? Markers->GetMarkerAlpha(State.Barricade.Get()) : -1.f;
				Check(State, Alpha > 0.f && Alpha < 1.f, FString::Printf(TEXT("the marker fades out softly (alpha %.2f after 0.4 s)"), Alpha));
			}
			if (State.Time < 2.f)
			{
				return true;
			}
			const UMeshComponent* Mesh = State.Barricade.IsValid() ? State.Barricade->FindComponentByClass<UMeshComponent>() : nullptr;
			Check(State, Markers && Markers->GetMarkerCount() == 0 && Mesh && !Mesh->GetOverlayMaterial(),
				TEXT("faded out after the order: no marker, the overlay removed"));
			return Finish(State);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DefendSmoke"),
		TEXT("Dev check of the Sprint 10 defense line: walk to the object, intruder target, no distant aid, hold + knife, leash."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
