// Dev-only headless check of Sprint 15 «Mobile & Tactical Heat» (TANDEM request #14) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TacticalHeatSmoke
// A cold commander in the open (exploration):
//  1. frostbite at 100 % -> U (heat pack): cold -35, frostbite gone, up to a crouch, 45 s buffer halves the accumulation;
//     a heat pack dragged onto a cold mate is cracked on him;
//  2. the drawer's «Campfire» line: placement, walk, kindled from 1 match + 1 firewood: 3.5 m warm zone, 75 s, a patrol 9 m
//     away starts a search (noise / smoke); the commander warms up next to it; one turn-based round burns 75 / 4 s;
//     a dialogue blocker freezes the burn timer;
//  3. the drawer's «Gas stove» line: the engineer hands the stove (with its 180 s of gas) to the commander, who sets it up:
//     2.2 m warm zone, gas burns; «Pack up» returns it with the rest of the gas;
//  4. Shift + G flare: thrown ~12 m along the arc, lands and burns 40 s with a 1.8 m warm zone, reveals within 10 m; a click at
//     the feet lights the second one there;
//  5. a fuel barrel blown up leaves a fire puddle: the engineer in the core burns, the medic in the radiant ring warms at
//     12 %/s without damage and her frozen weapon thaws;
//  6. wave fight -> tactical pause: every burn timer stands still; released, they run again.
// "shot" (rendered: UnrealEditor.exe -game) saves Saved/Screenshots/Phone/heat_sources.png with campfire + stove + flare.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "CodexTactics.h"
#include "Combat/FirePuddleActor.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SignalFlareActor.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/TacticalHeatTuning.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/CampfireActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/PortableStoveActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Survival/TacticalHeatSubsystem.h"
#include "Survival/TacticalHeatTimerComponent.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace TacticalHeatSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 150.f;

	enum class EStage : uint8
	{
		HeatPack,
		HeatPackMate,
		CampfireOrder,
		CampfireWait,
		CampfireWarm,
		StoveOrder,
		StoveWait,
		StoveRun,
		StovePackMenu,
		StovePackWait,
		FlareThrow,
		FlareWait,
		Shot,
		Puddle,
		PuddleCheck,
		PauseFreeze,
		PauseRelease,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::HeatPack;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		float Value = 0.f;
		float Value2 = 0.f;
		int32 Count = 0;
		FVector Spot = FVector::ZeroVector;
		TWeakObjectPtr<ACampfireActor> Campfire;
		TWeakObjectPtr<APortableStoveActor> Stove;
		TWeakObjectPtr<ASignalFlareActor> Flare;
		TWeakObjectPtr<AFirePuddleActor> Puddle;
		TWeakObjectPtr<AEnemyCharacter> Patrol;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("TacticalHeatSmoke"));
		return false;
	}

	AOperativeCharacter* FindRole(UWorld* World, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	FVector Feet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	/** Puts Operative at a free spot near Point (no move order running). */
	void Place(UWorld* World, AOperativeCharacter* Operative, const FVector& Point)
	{
		Operative->StopOperative();
		Operative->TeleportTo(SmokeUtils::FreeSpot(World, Point + FVector(0.f, 0.f, 92.f), Operative), Operative->GetActorRotation(), false, true);
	}

	/** Places Operative on the exact ground point (heat zone edges are tight). */
	void PlaceExact(AOperativeCharacter* Operative, const FVector& Ground)
	{
		Operative->StopOperative();
		Operative->TeleportTo(Ground + FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight() + 2.f), Operative->GetActorRotation(), false, true);
	}

	/** A senseless frostbitten walking a short runtime route near Point (only a search can disturb it). */
	AEnemyCharacter* SpawnPatrol(UWorld* World, const FVector& Point)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Waves)
		{
			return nullptr;
		}
		const FVector A = SmokeUtils::ClearPoint(World, Point, Point);
		const FVector B = SmokeUtils::ClearPoint(World, A, A + FVector(500.f, 0.f, 0.f));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
		if (USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr)
		{
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
			Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);
			Spline->UpdateSpline();
			Route->bIsLoop = true;
			Route->DefaultWaitTimeSeconds = 2.f;
		}
		AEnemyCharacter* Enemy = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, A + FVector(0.f, 0.f, 100.f));
		if (!Enemy || !Route)
		{
			return nullptr;
		}
		FEnemyPerceptionParams Senseless;
		Senseless.SightRangeCm = 0.f;
		Senseless.ProximityCm = 0.f;
		Senseless.HearWalkCm = Senseless.HearRunCm = Senseless.HearCrouchWalkCm = Senseless.HearCrawlCm = 0.f;
		Senseless.HearGunshotCm = Senseless.HearExplosionCm = 0.f;
		Senseless.SmellRadiusCm = 0.f;
		Enemy->bOverridePerception = true;
		Enemy->PerceptionOverride = Senseless;
		Enemy->StartPatrol(Route, nullptr);
		return Enemy;
	}

	template <typename T>
	T* FindNear(UWorld* World, const FVector& Point, float Radius)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && FVector::Dist2D(It->GetActorLocation(), Point) <= Radius)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Two clicks of the F placement at Point (relative to the worker's ground). */
	void OrderPlacement(URelocationSubsystem* Relocation, const FVector& Point)
	{
		Relocation->UpdatePreview(Point);
		Relocation->ConfirmPlacement(Point);
		Relocation->ConfirmPlacement(Point);
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		UTacticalHeatSubsystem* Heat = World->GetSubsystem<UTacticalHeatSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
		if (!Squad || !Relocation || !Interactions || !Grenades || !Heat || !Flow || !PC || !Commander || !Engineer || !Medic)
		{
			Check(State, false, TEXT("world pieces (squad, subsystems, controller)"));
			return Finish(State, false);
		}
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		UColdSurvivalComponent* Cold = Commander->ColdSurvival.Get();

		switch (State.Stage)
		{
		case EStage::HeatPack:
		{
			Squad->SetLeader(Commander);
			Check(State, Commander->HeatPacksCount == 1 && Engineer->PortableStovesCount == 1 && FMath::IsNearlyEqual(Engineer->StoveGasSeconds, 180.f)
				&& Medic->HeatPacksCount == 2 && Commander->FlaresCount == 1 && Commander->FirewoodCount == 1,
				FString::Printf(TEXT("starting loadout: commander packs %d wood %d flares %d, engineer stove %d (%.0f s gas), medic packs %d"),
					Commander->HeatPacksCount, Commander->FirewoodCount, Commander->FlaresCount, Engineer->PortableStovesCount,
					Engineer->StoveGasSeconds, Medic->HeatPacksCount));
			Check(State, !Cold->IsNearHeatSource(), TEXT("the commander stands in the cold"));
			Commander->ColdLevel = 100.f;
			Cold->StepCold(0.05f);
			Check(State, Cold->IsFrostbitten() && Commander->GetStance() == EOperativeStance::Prone, TEXT("100 % cold: frostbite, collapsed prone"));
			PC->UseSquadItem(EPersonalItem::HeatPack); // U
			Check(State, !Cold->IsFrostbitten() && FMath::IsNearlyEqual(Commander->ColdLevel, 65.f, 0.5f) && Commander->HeatPacksCount == 0
				&& Commander->GetStance() == EOperativeStance::Crouching && FMath::IsNearlyEqual(Cold->GetHeatBufferSecondsLeft(), 45.f, 0.5f),
				FString::Printf(TEXT("heat pack: cold %.1f, frostbite %d, stance %d, buffer %.1f s, packs %d"), Commander->ColdLevel,
					Cold->IsFrostbitten() ? 1 : 0, static_cast<int32>(Commander->GetStance()), Cold->GetHeatBufferSecondsLeft(), Commander->HeatPacksCount));
			// The buffer halves the accumulation (same stance / place, one step each).
			Commander->ColdLevel = 30.f;
			Cold->StepCold(2.f);
			const float Buffered = Commander->ColdLevel - 30.f;
			Cold->RestoreHeatBuffer(0.f);
			Commander->ColdLevel = 30.f;
			Cold->StepCold(2.f);
			const float Plain = Commander->ColdLevel - 30.f;
			Check(State, Plain > 0.1f && FMath::IsNearlyEqual(Buffered / Plain, 0.5f, 0.02f),
				FString::Printf(TEXT("heat buffer: +%.3f vs +%.3f in 2 s (x%.2f)"), Buffered, Plain, Plain > 0.f ? Buffered / Plain : 0.f));
			Commander->SetStance(EOperativeStance::Standing);
			Enter(State, EStage::HeatPackMate);
			return true;
		}

		case EStage::HeatPackMate:
		{
			// The medic drops a heat pack on the cold engineer standing next to her: cracked on him (no hand-over).
			USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
			Place(World, Engineer, Feet(Medic) + Medic->GetActorRightVector() * 120.f);
			Engineer->ColdLevel = 50.f;
			const int32 MedicPacks = Medic->HeatPacksCount;
			const ETransferRequestOutcome Outcome = Transfer ? Transfer->RequestTransfer(Medic, Engineer, ETransferItem::HeatPack, 1)
				: ETransferRequestOutcome::Failed;
			Check(State, Outcome == ETransferRequestOutcome::Transferred && Medic->HeatPacksCount == MedicPacks - 1 && Engineer->HeatPacksCount == 1
				&& FMath::IsNearlyEqual(Engineer->ColdLevel, 15.f, 0.5f),
				FString::Printf(TEXT("pack dropped on the cold engineer: used on him (cold %.1f, medic packs %d -> %d, engineer packs %d)"),
					Engineer->ColdLevel, MedicPacks, Medic->HeatPacksCount, Engineer->HeatPacksCount));
			Enter(State, EStage::CampfireOrder);
			return true;
		}

		case EStage::CampfireOrder:
		{
			// A patrol 9 m away, then the drawer's campfire line: placement 4 m ahead of the commander.
			const FVector Ahead = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 400.f);
			State.Spot = FVector(Ahead.X, Ahead.Y, Feet(Commander).Z);
			State.Patrol = SpawnPatrol(World, State.Spot + Commander->GetActorRightVector() * 900.f);
			Check(State, State.Patrol.IsValid() && State.Patrol->IsOnPatrol() && !State.Patrol->IsSearching(), TEXT("a patrol 9 m from the fire spot"));
			State.Value = static_cast<float>(Commander->MatchesCount);
			State.Count = Commander->FirewoodCount;
			Commander->ColdLevel = 60.f;
			PC->StartPlacementForType(EDeployableType::Campfire);
			Check(State, Relocation->IsPlacingDeployable() && Relocation->GetPlacingType() == EDeployableType::Campfire, TEXT("campfire placement started"));
			OrderPlacement(Relocation, State.Spot);
			Check(State, Relocation->GetActiveDeployCount() == 1, TEXT("campfire ordered: the commander walks there"));
			Enter(State, EStage::CampfireWait);
			return true;
		}

		case EStage::CampfireWait:
			State.Campfire = FindNear<ACampfireActor>(World, State.Spot, 60.f);
			if (State.Campfire.IsValid() && Relocation->GetActiveDeployCount() == 0)
			{
				ACampfireActor* Fire = State.Campfire.Get();
				const float Expected = TacticalHeatRules::GetCampfireBurnSeconds(Config, Fire->IsWindExposed());
				Check(State, Fire->IsBurning() && FMath::IsNearlyEqual(Fire->HeatSource->Radius, 350.f) && Fire->GetTimeLeft() <= Expected
					&& Fire->GetTimeLeft() > Expected - 2.f,
					FString::Printf(TEXT("campfire kindled: radius %.0f, %.1f s of %.0f (wind %d)"), Fire->HeatSource->Radius, Fire->GetTimeLeft(), Expected,
						Fire->IsWindExposed() ? 1 : 0));
				Check(State, Commander->MatchesCount == static_cast<int32>(State.Value) - 1 && Commander->FirewoodCount == State.Count - 1,
					FString::Printf(TEXT("crafted from 1 match + 1 firewood (matches %d, firewood %d)"), Commander->MatchesCount, Commander->FirewoodCount));
				Check(State, State.Patrol.IsValid() && State.Patrol->IsSearching(), TEXT("the patrol noticed the smoke and searches"));
				PlaceExact(Commander, Feet(Fire) + FVector(200.f, 0.f, 0.f));
				Commander->ColdLevel = 60.f;
				State.Value = Commander->ColdLevel;
				Enter(State, EStage::CampfireWarm);
			}
			else if (State.StageTime > 20.f)
			{
				Check(State, false, TEXT("campfire built within 20 s"));
				return Finish(State, false);
			}
			return true;

		case EStage::CampfireWarm:
			if (State.StageTime >= 1.5f)
			{
				ACampfireActor* Fire = State.Campfire.Get();
				Check(State, Cold->IsNearHeatSource() && Commander->ColdLevel < State.Value - 5.f,
					FString::Printf(TEXT("2 m from the fire: warm, cold %.1f -> %.1f"), State.Value, Commander->ColdLevel));
				// Turn-based: one squad round burns a quarter of the fire.
				const float Before = Fire->GetTimeLeft();
				Heat->AdvanceRound();
				Check(State, FMath::IsNearlyEqual(Before - Fire->GetTimeLeft(), Fire->GetTimer()->GetSecondsPerRound(), 0.01f)
					&& FMath::IsNearlyEqual(Fire->GetTimer()->GetSecondsPerRound(), Fire->IsWindExposed() ? 15.f : 18.75f, 0.01f),
					FString::Printf(TEXT("one turn-based round: %.2f -> %.2f s"), Before, Fire->GetTimeLeft()));
				// A dialogue / cutscene blocker freezes the burn timer.
				UWorldAIPauseSubsystem* Pause = World->GetSubsystem<UWorldAIPauseSubsystem>();
				Pause->AddPauseBlocker(TEXT("TacticalHeatSmoke"));
				State.Value2 = Fire->GetTimeLeft();
				Enter(State, EStage::StoveOrder);
			}
			return true;

		case EStage::StoveOrder:
			if (State.StageTime >= 1.f)
			{
				UWorldAIPauseSubsystem* Pause = World->GetSubsystem<UWorldAIPauseSubsystem>();
				ACampfireActor* Fire = State.Campfire.Get();
				Check(State, Fire && FMath::IsNearlyEqual(Fire->GetTimeLeft(), State.Value2, 0.001f),
					FString::Printf(TEXT("dialogue blocker: burn timer frozen (%.2f s)"), Fire ? Fire->GetTimeLeft() : -1.f));
				Pause->RemovePauseBlocker(TEXT("TacticalHeatSmoke"));
				// The stove: the engineer hands it over (with the gas), the commander sets it up 3.5 m to his side.
				PlaceExact(Commander, Feet(Fire) + FVector(-450.f, 0.f, 0.f));
				Place(World, Engineer, Feet(Commander) - Commander->GetActorRightVector() * 200.f);
				PC->StartPlacementForType(EDeployableType::PortableStove);
				Check(State, Commander->PortableStovesCount == 1 && Engineer->PortableStovesCount == 0 && FMath::IsNearlyEqual(Commander->StoveGasSeconds, 180.f)
					&& Engineer->StoveGasSeconds == 0.f, FString::Printf(TEXT("stove handed over with its gas (%.0f s)"), Commander->StoveGasSeconds));
				const FVector Side = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) - FVector(0.f, 350.f, 0.f));
				State.Spot = FVector(Side.X, Side.Y, Feet(Commander).Z);
				OrderPlacement(Relocation, State.Spot);
				Enter(State, EStage::StoveWait);
			}
			return true;

		case EStage::StoveWait:
			State.Stove = FindNear<APortableStoveActor>(World, State.Spot, 60.f);
			if (State.Stove.IsValid() && Relocation->GetActiveDeployCount() == 0)
			{
				APortableStoveActor* Stove = State.Stove.Get();
				Check(State, Stove->IsBurning() && FMath::IsNearlyEqual(Stove->HeatSource->Radius, 220.f) && Stove->GetGasLeft() > 170.f
					&& Commander->PortableStovesCount == 0 && Commander->StoveGasSeconds == 0.f,
					FString::Printf(TEXT("stove deployed: burning, radius %.0f, gas %.1f s, commander stoves %d"), Stove->HeatSource->Radius,
						Stove->GetGasLeft(), Commander->PortableStovesCount));
				PlaceExact(Medic, Feet(Stove) + FVector(150.f, 0.f, 0.f));
				Medic->ColdLevel = 50.f;
				State.Value = Medic->ColdLevel;
				State.Value2 = Stove->GetGasLeft();
				Enter(State, EStage::StoveRun);
			}
			else if (State.StageTime > 20.f)
			{
				Check(State, false, TEXT("stove deployed within 20 s"));
				return Finish(State, false);
			}
			return true;

		case EStage::StoveRun:
			if (State.StageTime >= 1.5f)
			{
				APortableStoveActor* Stove = State.Stove.Get();
				Check(State, Medic->ColdSurvival->IsNearHeatSource() && Medic->ColdLevel < State.Value - 5.f && Stove && Stove->GetGasLeft() < State.Value2 - 1.f,
					FString::Printf(TEXT("1.5 m from the stove: cold %.1f -> %.1f, gas %.1f -> %.1f"), State.Value, Medic->ColdLevel, State.Value2,
						Stove ? Stove->GetGasLeft() : -1.f));
				Squad->SetLeader(Commander);
				Interactions->RequestInteraction(Stove);
				Enter(State, EStage::StovePackMenu);
			}
			return true;

		case EStage::StovePackMenu:
			if (Interactions->IsActionMenuOpen())
			{
				State.Value2 = State.Stove.IsValid() ? State.Stove->GetGasLeft() : 0.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stove menu \"%s\" / \"%s\""), *Interactions->GetActionMenu().Title.ToString(),
					*Interactions->GetActionMenu().ConfirmText.ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EStage::StovePackWait);
			}
			else if (State.StageTime > 15.f)
			{
				Check(State, false, TEXT("stove menu opened"));
				return Finish(State, false);
			}
			return true;

		case EStage::StovePackWait:
			if (!State.Stove.IsValid() || State.StageTime > 4.f)
			{
				Check(State, !State.Stove.IsValid() && Commander->PortableStovesCount == 1 && Commander->StoveGasSeconds > 100.f
					&& Commander->StoveGasSeconds <= State.Value2 + 0.01f,
					FString::Printf(TEXT("packed up: stove back with the commander (%d), gas kept %.1f s"), Commander->PortableStovesCount, Commander->StoveGasSeconds));
				Enter(State, EStage::FlareThrow);
			}
			return true;

		case EStage::FlareThrow:
		{
			// Shift + G: aim, throw ~12 m ahead.
			Commander->FlaresCount = 2;
			Commander->SetStance(EOperativeStance::Standing);
			Check(State, Grenades->StartFlareAim(Commander) && Grenades->IsAimingFlare(), TEXT("flare aim started"));
			const FVector Target = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 1200.f);
			State.Spot = FVector(Target.X, Target.Y, Feet(Commander).Z);
			Grenades->UpdateAim(State.Spot);
			Grenades->ThrowAtCursor(State.Spot);
			Check(State, !Grenades->IsAiming() && Commander->FlaresCount == 1, FString::Printf(TEXT("flare thrown (left %d)"), Commander->FlaresCount));
			Enter(State, EStage::FlareWait);
			return true;
		}

		case EStage::FlareWait:
			State.Flare = FindNear<ASignalFlareActor>(World, State.Spot, 80.f);
			if (State.Flare.IsValid() && State.Flare->IsBurning())
			{
				ASignalFlareActor* Flare = State.Flare.Get();
				Check(State, FMath::IsNearlyEqual(Flare->HeatSource->Radius, 180.f) && Flare->GetTimeLeft() > 37.f && Flare->GetTimeLeft() <= 40.f,
					FString::Printf(TEXT("flare landed and burns: radius %.0f, %.1f s"), Flare->HeatSource->Radius, Flare->GetTimeLeft()));
				Check(State, UTacticalHeatSubsystem::IsRevealedByFlare(World, Flare->GetActorLocation() + FVector(900.f, 0.f, 0.f))
					&& !UTacticalHeatSubsystem::IsRevealedByFlare(World, Flare->GetActorLocation() + FVector(0.f, 1150.f, 0.f)),
					TEXT("its light reveals within 10 m, not at 11.5 m"));
				PlaceExact(Medic, Flare->GetActorLocation() + FVector(0.f, 120.f, 0.f));
				Medic->ColdLevel = 40.f;
				Medic->ColdSurvival->StepCold(0.5f);
				Check(State, Medic->ColdSurvival->IsNearHeatSource() && Medic->ColdLevel < 40.f, FString::Printf(TEXT("1.2 m from the flare: warm (cold %.1f)"), Medic->ColdLevel));
				// The second flare lit at the feet.
				Grenades->StartFlareAim(Commander);
				Grenades->ThrowAtCursor(Feet(Commander) + Commander->GetActorForwardVector() * 50.f);
				ASignalFlareActor* AtFeet = FindNear<ASignalFlareActor>(World, Feet(Commander), 150.f);
				Check(State, AtFeet && AtFeet->IsBurning() && Commander->FlaresCount == 0, TEXT("a click at the feet lights the flare there"));
				Enter(State, State.bShot ? EStage::Shot : EStage::Puddle);
			}
			else if (State.StageTime > 8.f)
			{
				Check(State, false, TEXT("flare landed within 8 s"));
				return Finish(State, false);
			}
			return true;

		case EStage::Shot:
			if (State.StageTime <= StepSeconds)
			{
				// Frame the campfire, the stove (set up again) and the flare together.
				const FVector FireFeet = State.Campfire.IsValid() ? Feet(State.Campfire.Get()) : Feet(Commander);
				Relocation->ExecuteDeploy(Commander, EDeployableType::PortableStove, FireFeet + FVector(0.f, -300.f, 0.f), 0.f);
				ASignalFlareActor::SpawnLit(World, FireFeet + FVector(250.f, 250.f, 0.f));
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SnapFocusOnTarget(State.Campfire.Get(), 1500.f);
				}
			}
			if (State.StageTime >= 6.f && State.StageTime < 6.f + StepSeconds)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Phone") / TEXT("heat_sources.png"), true, false);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot Saved/Screenshots/Phone/heat_sources.png"));
			}
			if (State.StageTime >= 7.f)
			{
				Enter(State, EStage::Puddle);
			}
			return true;

		case EStage::Puddle:
		{
			// A fuel barrel blown up 5 m away from everyone: the puddle at its foot.
			const FVector Ground = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorRightVector() * 800.f);
			const float HalfHeight = GetDefault<ABarrelActor>()->Box->GetUnscaledBoxExtent().Z;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(FVector(Ground.X, Ground.Y, Feet(Commander).Z + HalfHeight), FRotator::ZeroRotator, Params);
			Check(State, Barrel && Barrel->Explode(FText::FromString(TEXT("Smoke"))), TEXT("fuel barrel exploded"));
			State.Puddle = Barrel ? FindNear<AFirePuddleActor>(World, Barrel->GetActorLocation(), 50.f) : nullptr;
			AFirePuddleActor* Puddle = State.Puddle.Get();
			Check(State, Puddle && Puddle->IsBurning() && FMath::IsNearlyEqual(Puddle->HeatSource->Radius, 350.f) && FMath::IsNearlyEqual(Puddle->HeatSource->WarmthRate, 12.f)
				&& Puddle->GetTimeLeft() >= 11.9f && Puddle->GetTimeLeft() <= 15.f,
				FString::Printf(TEXT("fire puddle left: ring %.0f cm at %.0f %%/s, %.1f s"), Puddle ? Puddle->HeatSource->Radius : 0.f,
					Puddle ? Puddle->HeatSource->WarmthRate : 0.f, Puddle ? Puddle->GetTimeLeft() : 0.f));
			if (!Puddle)
			{
				return Finish(State, false);
			}
			Barrel->Destroy(); // the charred barrel would push the engineer out of the core
			// Engineer in the core, medic in the ring with a frozen weapon.
			PlaceExact(Engineer, Puddle->GetActorLocation() + FVector(60.f, 0.f, 0.f));
			State.Value = Engineer->HealthComponent->GetCurrentHealth();
			PlaceExact(Medic, Puddle->GetActorLocation() + FVector(-1400.f, 0.f, 0.f));
			Medic->ColdLevel = 95.f;
			Medic->ColdSurvival->StepCold(0.01f);
			Check(State, Medic->ColdSurvival->IsWeaponFrozen(), TEXT("medic away from the fire at 95 %: weapon frozen"));
			PlaceExact(Medic, Puddle->GetActorLocation() + FVector(-250.f, 0.f, 0.f));
			State.Value2 = Medic->HealthComponent->GetCurrentHealth();
			Medic->ColdLevel = 60.f;
			Enter(State, EStage::PuddleCheck);
			return true;
		}

		case EStage::PuddleCheck:
			if (State.StageTime >= 1.25f)
			{
				const float Lost = State.Value - Engineer->HealthComponent->GetCurrentHealth();
				const FVector PuddleAt = State.Puddle.IsValid() ? State.Puddle->GetActorLocation() : FVector::ZeroVector;
				Check(State, Lost > 1.f, FString::Printf(TEXT("engineer in the core (%.0f cm, dz %.0f) burns: -%.1f HP"), FVector::Dist2D(Feet(Engineer), PuddleAt),
					Feet(Engineer).Z - PuddleAt.Z, Lost));
				Check(State, Medic->HealthComponent->GetCurrentHealth() >= State.Value2 && !Medic->ColdSurvival->IsWeaponFrozen()
					&& Medic->ColdLevel < 60.f - 10.f,
					FString::Printf(TEXT("medic in the ring (2.5 m): no damage, weapon thawed, cold 60 -> %.1f (12 %%/s)"), Medic->ColdLevel));
				// The wave fight, then the tactical pause.
				SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
				SmokeUtils::StartWaveFight(Flow);
				Flow->ToggleTacticalPause();
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tactical pause"));
				State.Value = State.Campfire.IsValid() ? State.Campfire->GetTimeLeft() : -1.f;
				State.Value2 = State.Flare.IsValid() ? State.Flare->GetTimeLeft() : -1.f;
				Enter(State, EStage::PauseFreeze);
			}
			return true;

		case EStage::PauseFreeze:
			if (State.StageTime >= 1.5f)
			{
				const float Fire = State.Campfire.IsValid() ? State.Campfire->GetTimeLeft() : -2.f;
				const float FlareLeft = State.Flare.IsValid() ? State.Flare->GetTimeLeft() : -2.f;
				Check(State, FMath::IsNearlyEqual(Fire, State.Value, 0.001f) && FMath::IsNearlyEqual(FlareLeft, State.Value2, 0.001f) && !Heat->CanAdvanceRealTime(),
					FString::Printf(TEXT("tactical pause: campfire %.3f -> %.3f s, flare %.3f -> %.3f s (frozen)"), State.Value, Fire, State.Value2, FlareLeft));
				Flow->ToggleTacticalPause();
				Enter(State, EStage::PauseRelease);
			}
			return true;

		case EStage::PauseRelease:
			if (State.StageTime >= 1.f)
			{
				const float Fire = State.Campfire.IsValid() ? State.Campfire->GetTimeLeft() : State.Value;
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Fire < State.Value - 0.5f,
					FString::Printf(TEXT("released: the campfire burns again (%.2f -> %.2f s)"), State.Value, Fire));
				return Finish(State, true);
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bShot)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = bShot;
		SmokeUtils::PlaceSquadAtTestStart(World);
		// Real-time ticker: the tactical pause slows the world (and its timers) down to 0.02.
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State);
		}), StepSeconds);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const bool bShot = Args.Contains(TEXT("shot"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, bShot]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, bShot);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TacticalHeatSmoke"),
		TEXT("Dev check (Sprint 15): heat pack, campfire, gas stove deploy / pack up, signal flare, fire puddle, timers frozen in the pause; PASS/FAIL, then exits. [shot]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
