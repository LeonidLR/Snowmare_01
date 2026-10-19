// Dev-only console command for a headless quick fire check (game designer request 2026-10-19; bound to Ctrl + click on a point,
// user decision 2026-10-19) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.QuickFireSmoke
// Everything on its own empty ground (SmokeUtils::FindClearArena); every Ctrl + click goes through the controller's real click
// dispatcher HandleWorldHit with Ctrl held (SetSimulatedAttackModifier - headless there is no keyboard):
//  A exploring, only the commander selected, the squad facing sideways: Ctrl + click on the ground 8 m ahead - he alone turns
//    to it and fires his burst (3 M16 rounds, aim within the cone), one "Fire!" line, one fire ping, the others fire nothing;
//  B the whole squad box-selected: one Ctrl + click - all three fire their burst;
//  C an RMB click (press, 0.1 s, release) only drives the camera drag: nothing fires;
//  D Ctrl + click 0.9 m beside a visible (frozen) brute, then on the brute: the targeted shot as before (his priority target),
//    no quick fire;
//  E Ctrl + click on a barrel / a trapped mine / a trapped crate: the targeted shot as before - each goes off at once, one round
//    each, no quick fire;
//  S the Medic-Sapper alone with the sniper rifle, standing: Ctrl + click on the ground - she kneels by herself, then one round;
//  K the commander in cover at the right end of a 3 m wall: Ctrl + click on a point round the corner - he fires from the cover
//    pose (stays in cover, cover shots); Ctrl + click on a point in front of the wall - the open shot (steps off the wall);
//  F a tutorial beat locking "Attack.*": the Ctrl + click is refused (one lock refusal), nothing fires; skipped: free again;
//  G a real-time wave fight (its enemies parked far away), tactical pause: the Ctrl + click is only planned (marker, no round);
//    the release fires it;
//  H turn-based with a brute 3 cells out: Ctrl + click on an empty spot = the grid's hint, no AP; on the brute = the grid attack
//    (AP spent, the brute hit), no real-time round.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/QuickFireRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace QuickFireSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.05f;
	constexpr float Timeout = 180.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<int32> RoundsBefore;
		int32 Burst = 3;
		int32 FireLines = 0;
		int32 Pings = 0;
		int32 Markers = 0;
		int32 Refusals = 0;
		int32 Hints = 0;
		int32 ApBefore = 0;
		int32 ClipBefore = 0;
		int32 CoverShotsBefore = 0;
		int32 OpenShotsBefore = 0;
		float HealthBefore = 0.f;
		FVector Point = FVector::ZeroVector;
		bool bPressed = false;
		bool bSawDragRotating = false;
		bool bLeftCover = false;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AActor> Object;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("QuickFireSmoke"));
		return false;
	}

	void GoTo(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bPressed = false;
	}

	void Next(FState& State)
	{
		GoTo(State, State.Stage + 1);
	}

	FVector Feet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	/** The world under a cursor at XY (a trace from above, the squad ignored): what a click there would hit. */
	FHitResult HitFromAbove(UWorld* World, const FVector& XY, const FState& State)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickFireSmoke), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
		{
			Params.AddIgnoredActor(Member.Get());
		}
		World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, State.P.Z + 600.f), FVector(XY.X, XY.Y, State.P.Z - 600.f), ECC_Visibility, Params);
		return Hit;
	}

	/** A click on Actor itself (its centre). */
	FHitResult HitOn(AActor* Actor)
	{
		return FHitResult(Actor, nullptr, Actor->GetActorLocation(), FVector::UpVector);
	}

	/** Ctrl + click through the controller's own click dispatcher (HandleWorldHit with Ctrl held). */
	void CtrlClick(ACodexTacticsPlayerController* PC, const FHitResult& Hit)
	{
		PC->SetSimulatedAttackModifier(true);
		PC->HandleWorldHit(Hit);
		PC->SetSimulatedAttackModifier(false);
	}

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (const FGameMessage& Message : Messages->GetHistory())
			{
				Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return Count;
	}

	int32 Rounds(const FState& State, int32 Index)
	{
		const AOperativeCharacter* Member = State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
		return Member ? Member->GetQuickFireRoundsFired() : -1;
	}

	void Snapshot(UWorld* World, FState& State)
	{
		State.RoundsBefore.Reset();
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			State.RoundsBefore.Add(Rounds(State, Index));
		}
		State.FireLines = CountMessages(World, TEXT("Fire!"));
		State.Pings = World->GetSubsystem<UCombatFeedbackSubsystem>()->GetFirePingsSpawned();
		State.Markers = World->GetSubsystem<UCombatFeedbackSubsystem>()->GetPlannedMarkerCount();
	}

	int32 NewRounds(const FState& State, int32 Index)
	{
		return State.RoundsBefore.IsValidIndex(Index) ? Rounds(State, Index) - State.RoundsBefore[Index] : -1;
	}

	bool AnyQuickFireOrder(const FState& State)
	{
		return State.Members.ContainsByPredicate([](const TWeakObjectPtr<AOperativeCharacter>& Member)
		{
			return Member.IsValid() && (Member->HasQuickFireOrder() || Member->HasPlannedQuickFire());
		});
	}

	void Freeze(AEnemyCharacter* Enemy)
	{
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f;
			Enemy->SetActorTickEnabled(false);
		}
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
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

	template <typename T>
	T* SpawnObject(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Squad || !Flow || !TurnBased || !Tutorial || !Feedback || !PC || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d at %.1fs"), State.Stage, State.Time);
			Check(State, false, TEXT("finished in time"));
			return Finish(State, false);
		}
		AOperativeCharacter* Commander = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		if (State.Stage > 0 && (!Commander || State.Members.Num() < 3 || !State.Members[1].IsValid() || !State.Members[2].IsValid()))
		{
			Check(State, false, TEXT("the squad is there"));
			return Finish(State, false);
		}
		if (State.Stage >= 81)
		{
			// The wave fight: whatever the wave spawns near the squad is parked far away (only the smoke's own brute stays).
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != State.Enemy.Get() && FVector::Dist2D(It->GetActorLocation(), State.P) < 20000.f)
				{
					SmokeUtils::ParkEnemyFarAway(*It);
					It->SetActorTickEnabled(false);
				}
			}
		}

		switch (State.Stage)
		{
		case 0: // Set-up: the smoke's own ground, the squad facing sideways, M16 in hands, passive (no fire of its own).
		{
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			if (Members.Num() < 3)
			{
				Check(State, false, TEXT("three operatives"));
				return Finish(State, false);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // the map may hold its own enemies: start from a known set
			}
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				State.Members.Add(Member);
				Ignore.Add(Member);
			}
			Ignore.Add(PC->GetPawn());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet(Members[0]), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-800.f, -700.f), FVector2D(1600.f, 700.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 24 x 14 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			const float Sides[] = { 0.f, 250.f, -250.f };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
				Member->TeleportTo(State.P + State.R * Sides[Index] + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f),
					State.R.Rotation(), false, true);
				Member->SwitchToWeaponById(TEXT("m16"));
				if (Member->CurrentWeapon)
				{
					Member->CurrentClip = Member->CurrentWeapon->MaxClipSize;
				}
				Member->bForceHitForTesting = true; // no spread, sure hits: deterministic
			}
			State.Burst = QuickFireRules::GetBurstRounds(Members[0]->UsesAmmo(), Members[0]->IsSniperWeaponEquipped(),
				Members[0]->CurrentWeapon ? Members[0]->CurrentWeapon->MaxClipSize : 30, QuickFireRules::GetConfig());
			Check(State, State.Burst == 3, FString::Printf(TEXT("the M16 fires a %d-round burst"), State.Burst));
			Squad->SetLeader(Members[0]);
			GoTo(State, 10);
			return true;
		}
		case 10: // A: one operative selected - Ctrl + click on the ground 8 m ahead.
		{
			if (State.StageTime < 0.5f)
			{
				return true; // settled after the teleport
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("A: exploring"));
			Check(State, !Squad->HasMultiSelection() && Squad->GetLeader() == Commander, TEXT("A: only the commander selected"));
			Snapshot(World, State);
			State.Point = State.P + State.F * 800.f;
			CtrlClick(PC, HitFromAbove(World, State.Point, State));
			Check(State, Commander->HasQuickFireOrder() && !State.Members[1]->HasQuickFireOrder() && !State.Members[2]->HasQuickFireOrder(),
				TEXT("A: Ctrl + click on the ground = quick fire, only the commander got it"));
			Check(State, Feedback->GetFirePingsSpawned() == State.Pings + 1, TEXT("A: a fire ping at the point"));
			Check(State, CountMessages(World, TEXT("Fire!")) == State.FireLines + 1, TEXT("A: one 'Fire!' feed line"));
			Next(State);
			return true;
		}
		case 11:
		{
			if (NewRounds(State, 0) < State.Burst && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == State.Burst, FString::Printf(TEXT("A: the commander fired his burst (%d rounds)"), NewRounds(State, 0)));
			Check(State, NewRounds(State, 1) == 0 && NewRounds(State, 2) == 0, TEXT("A: the engineer and the medic fired nothing"));
			Check(State, Commander->GetLastQuickFireAimErrorDeg() <= Commander->AimConeDeg + 0.5f,
				FString::Printf(TEXT("A: aimed at the point when firing (error %.1f deg <= %.0f)"), Commander->GetLastQuickFireAimErrorDeg(), Commander->AimConeDeg));
			const FVector ToPoint = (State.Point - Commander->GetActorLocation()).GetSafeNormal2D();
			const float TurnLeft = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Commander->GetActorForwardVector().GetSafeNormal2D(), ToPoint), -1.f, 1.f)));
			Check(State, TurnLeft <= 35.f, FString::Printf(TEXT("A: he turned from the side to the point (%.1f deg off)"), TurnLeft));
			Check(State, !Commander->HasQuickFireOrder(), TEXT("A: the order is done after the burst"));
			Squad->SetSelectedGroup({ Commander, State.Members[1].Get(), State.Members[2].Get() });
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				Member->SetActorRotation(State.R.Rotation());
			}
			Next(State);
			return true;
		}
		case 12: // B: the whole squad selected - one Ctrl + click, all three fire.
		{
			Check(State, Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 3, TEXT("B: the whole squad box-selected"));
			Snapshot(World, State);
			State.Point = State.P + State.F * 900.f + State.R * 100.f;
			CtrlClick(PC, HitFromAbove(World, State.Point, State));
			Check(State, Commander->HasQuickFireOrder() && State.Members[1]->HasQuickFireOrder() && State.Members[2]->HasQuickFireOrder(),
				TEXT("B: all three got the order"));
			Check(State, CountMessages(World, TEXT("Fire!")) == State.FireLines + 1, TEXT("B: one 'Fire!' line for the group"));
			Next(State);
			return true;
		}
		case 13:
		{
			const bool bAll = NewRounds(State, 0) >= State.Burst && NewRounds(State, 1) >= State.Burst && NewRounds(State, 2) >= State.Burst;
			if (!bAll && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("B: all three fired their burst (%d / %d / %d)"), NewRounds(State, 0), NewRounds(State, 1), NewRounds(State, 2)));
			Squad->SetLeader(Commander);
			GoTo(State, 20);
			return true;
		}
		case 20: // C: an RMB click is the camera drag only.
		{
			const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn());
			if (!State.bPressed)
			{
				Snapshot(World, State);
				PC->RightMousePressed();
				State.bPressed = true;
				State.StageTime = 0.f;
				State.bSawDragRotating = Camera && Camera->IsDragRotating();
				return true;
			}
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			PC->RightMouseReleased();
			Check(State, State.bSawDragRotating && Camera && !Camera->IsDragRotating(), TEXT("C: RMB press = the camera drag, released = off"));
			Check(State, !AnyQuickFireOrder(State), TEXT("C: an RMB click gives no fire order"));
			Next(State);
			return true;
		}
		case 21:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == 0 && NewRounds(State, 1) == 0 && NewRounds(State, 2) == 0, TEXT("C: nobody fired after the RMB click"));
			// D: a brute 10 m ahead, frozen (no senses, no steps).
			if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
			{
				State.Enemy = Waves->SpawnEnemy(EEnemyArchetype::Brute, State.P + State.F * 1000.f + FVector(0.f, 0.f, 110.f), (-State.F).Rotation());
			}
			Freeze(State.Enemy.Get());
			Check(State, State.Enemy.IsValid(), TEXT("D: a brute spawned 10 m ahead"));
			if (!State.Enemy.IsValid())
			{
				return Finish(State, false);
			}
			GoTo(State, 30);
			return true;
		}
		case 30: // D: Ctrl + click 0.9 m beside the visible brute, then on him: the targeted shot (unchanged), no quick fire.
		{
			AEnemyCharacter* Enemy = State.Enemy.Get();
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->Refresh();
				Check(State, Enemy && Sight->IsVisibleToSquad(Enemy) && !Enemy->IsHidden(), TEXT("D: the squad sees the brute"));
			}
			if (!Enemy)
			{
				return Finish(State, false);
			}
			Snapshot(World, State);
			const int32 PriorityLines = CountMessages(World, TEXT("Priority target set"));
			CtrlClick(PC, HitFromAbove(World, Feet(Enemy) + State.R * 90.f, State));
			Check(State, Commander->GetManualPriorityTarget() == Enemy && !AnyQuickFireOrder(State),
				TEXT("D: Ctrl + click on the snow beside the visible brute = the targeted shot at him (priority), no quick fire"));
			Commander->SetManualPriorityTarget(nullptr);
			CtrlClick(PC, HitOn(Enemy));
			Check(State, Commander->GetManualPriorityTarget() == Enemy && !AnyQuickFireOrder(State),
				TEXT("D: Ctrl + click on the brute = the targeted shot at him, no quick fire"));
			Check(State, CountMessages(World, TEXT("Priority target set")) == PriorityLines + 2 && CountMessages(World, TEXT("Fire!")) == State.FireLines,
				TEXT("D: the targeted shot lines, no 'Fire!'"));
			Commander->SetManualPriorityTarget(nullptr);
			Enemy->Destroy();
			// E: a barrel 9 m ahead, to the left.
			State.Object = SpawnObject<ABarrelActor>(World, State.P + State.F * 900.f - State.R * 150.f + FVector(0.f, 0.f, 60.f));
			Next(State);
			return true;
		}
		case 31: // E: the targeted shot on objects is unchanged: a barrel.
		{
			ABarrelActor* Barrel = Cast<ABarrelActor>(State.Object.Get());
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			if (!Barrel)
			{
				Check(State, false, TEXT("E: a barrel spawned"));
				return Finish(State, false);
			}
			Snapshot(World, State);
			State.ClipBefore = Commander->CurrentClip;
			CtrlClick(PC, HitOn(Barrel));
			Check(State, Barrel->IsBurning() && Commander->CurrentClip == State.ClipBefore - 1 && !AnyQuickFireOrder(State),
				TEXT("E: Ctrl + click on a barrel - it explodes at once, one round, no quick fire"));
			// A trapped mine far from the barrel's fire.
			if (AProximityMineActor* Mine = SpawnObject<AProximityMineActor>(World, State.P + State.F * 400.f + State.R * 550.f + FVector(0.f, 0.f, 20.f)))
			{
				Mine->bTrapped = true;
				State.Object = Mine;
			}
			Next(State);
			return true;
		}
		case 32: // E: a mine.
		{
			if (State.StageTime < 0.7f)
			{
				return true; // the shot cadence
			}
			AActor* Mine = State.Object.Get();
			if (!Mine)
			{
				Check(State, false, TEXT("E: a mine spawned"));
				return Finish(State, false);
			}
			State.ClipBefore = Commander->CurrentClip;
			CtrlClick(PC, HitOn(Mine));
			Check(State, !IsValid(Mine) && Commander->CurrentClip == State.ClipBefore - 1 && !AnyQuickFireOrder(State),
				TEXT("E: Ctrl + click on a mine - it goes off at once, one round, no quick fire"));
			if (ALootCrateActor* Crate = SpawnObject<ALootCrateActor>(World, State.P + State.F * 1300.f + State.R * 550.f + FVector(0.f, 0.f, 40.f)))
			{
				Crate->bTrapped = true;
				State.Object = Crate;
			}
			Next(State);
			return true;
		}
		case 33: // E: a trapped crate.
		{
			if (State.StageTime < 0.7f)
			{
				return true;
			}
			ALootCrateActor* Crate = Cast<ALootCrateActor>(State.Object.Get());
			if (!Crate)
			{
				Check(State, false, TEXT("E: a crate spawned"));
				return Finish(State, false);
			}
			State.ClipBefore = Commander->CurrentClip;
			CtrlClick(PC, HitOn(Crate));
			Check(State, Crate->IsDestroyed() && !Crate->bTrapped && Commander->CurrentClip == State.ClipBefore - 1 && !AnyQuickFireOrder(State),
				TEXT("E: Ctrl + click on a trapped crate - blown up at once, one round, no quick fire"));
			Check(State, NewRounds(State, 0) == 0 && CountMessages(World, TEXT("Fire!")) == State.FireLines, TEXT("E: no quick fire round, no 'Fire!'"));
			GoTo(State, 40);
			return true;
		}
		case 40: // S: the Medic-Sapper alone, standing, the sniper rifle in hands.
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			AOperativeCharacter* Medic = State.Members[2].Get();
			const int32 SniperIndex = Medic->AvailableWeapons.IndexOfByPredicate([](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->IsSniperRifle(); });
			const UWeaponDataAsset* Sniper = SniperIndex != INDEX_NONE ? Medic->AvailableWeapons[SniperIndex].Get() : nullptr;
			Check(State, Sniper && Medic->SwitchToWeaponById(Sniper->WeaponId) && Medic->IsSniperWeaponEquipped(), TEXT("S: the Medic-Sapper takes the sniper rifle"));
			Medic->SetStance(EOperativeStance::Standing);
			Squad->SetLeader(Medic);
			Next(State);
			return true;
		}
		case 41:
		{
			AOperativeCharacter* Medic = State.Members[2].Get();
			if (State.StageTime < 1.f)
			{
				return true; // the weapon swap / stand-up settle
			}
			Check(State, Medic->GetStance() == EOperativeStance::Standing && Medic->IsSniperWeaponEquipped(), TEXT("S: standing with the sniper rifle"));
			Snapshot(World, State);
			State.ClipBefore = Medic->CurrentClip;
			CtrlClick(PC, HitFromAbove(World, State.P + State.F * 1000.f - State.R * 250.f, State));
			Check(State, Medic->HasQuickFireOrder() && !Commander->HasQuickFireOrder(), TEXT("S: she alone got the order"));
			Next(State);
			return true;
		}
		case 42:
		{
			AOperativeCharacter* Medic = State.Members[2].Get();
			if (NewRounds(State, 2) < 1 && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 2) == 1 && Medic->CurrentClip == State.ClipBefore - 1, TEXT("S: one sniper round (a single shot, not a burst)"));
			Check(State, Medic->GetStance() == EOperativeStance::Crouching, TEXT("S: she knelt by herself for it (sniper rule)"));
			Medic->SwitchToWeaponById(TEXT("m16"));
			Medic->SetStance(EOperativeStance::Standing);
			Squad->SetLeader(Commander);
			// K: a 3 m high, 4 m long wall 2.5 m ahead; its right end 0.5 m right of the commander.
			State.Wall = SpawnBlock(World, State.P + State.F * 250.f - State.R * 150.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.4f, 4.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("K: a 3 m wall spawned"));
			GoTo(State, 50);
			return true;
		}
		case 50: // K: into cover at the wall's right end.
		{
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 225.f + FVector(0.f, 0.f, 90.f), State.F, State.Slot)
				&& State.Slot.Height == ECoverHeight::HighCover;
			Check(State, bSlot, TEXT("K: a high cover slot at the wall"));
			if (!bSlot)
			{
				return Finish(State, false);
			}
			Commander->SetActorRotation(State.F.Rotation());
			Check(State, Commander->OrderTakeCover(State.Slot, false) == EOperativeOrderResult::Accepted, TEXT("K: the commander ordered into cover"));
			Next(State);
			return true;
		}
		case 51:
		{
			const bool bSettled = Commander->bInCover && Commander->GetVelocity().Size2D() <= 20.f;
			if ((!bSettled || State.StageTime < 1.5f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, bSettled, TEXT("K: in cover at the wall"));
			// A point round the corner, beyond the wall's right end.
			State.Point = State.P + State.F * 800.f + State.R * 350.f;
			Check(State, Commander->IsCornerShotTarget(State.Point), TEXT("K: the point beyond the wall's end is a corner-shot target"));
			Snapshot(World, State);
			State.CoverShotsBefore = Commander->GetCoverLeanShots() + Commander->GetCoverBlindShots();
			State.OpenShotsBefore = Commander->GetCoverOpenShots();
			State.bLeftCover = false;
			CtrlClick(PC, HitFromAbove(World, State.Point, State));
			Check(State, Commander->HasQuickFireOrder(), TEXT("K: Ctrl + click round the corner = quick fire"));
			Next(State);
			return true;
		}
		case 52:
		{
			State.bLeftCover |= !Commander->bInCover;
			if (NewRounds(State, 0) < State.Burst && State.StageTime < 6.f)
			{
				return true;
			}
			const int32 CoverShots = Commander->GetCoverLeanShots() + Commander->GetCoverBlindShots() - State.CoverShotsBefore;
			Check(State, NewRounds(State, 0) == State.Burst && CoverShots >= State.Burst,
				FString::Printf(TEXT("K: the burst fired from the cover pose (%d rounds, %d cover shots)"), NewRounds(State, 0), CoverShots));
			Check(State, !State.bLeftCover && Commander->GetCoverOpenShots() == State.OpenShotsBefore, TEXT("K: he stayed in cover (corner shot, no open shot)"));
			Next(State);
			return true;
		}
		case 53: // K: a point in front of the wall (his side): the open shot.
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (!Commander->bInCover)
			{
				Check(State, false, TEXT("K: still in cover before the open-side click"));
				return Finish(State, false);
			}
			State.Point = State.P - State.F * 600.f - State.R * 100.f;
			Check(State, !Commander->IsCornerShotTarget(State.Point), TEXT("K: the point in front of the wall is no corner shot"));
			Snapshot(World, State);
			State.OpenShotsBefore = Commander->GetCoverOpenShots();
			CtrlClick(PC, HitFromAbove(World, State.Point, State));
			Next(State);
			return true;
		}
		case 54:
		{
			if (NewRounds(State, 0) < State.Burst && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == State.Burst && Commander->GetCoverOpenShots() - State.OpenShotsBefore >= State.Burst,
				FString::Printf(TEXT("K: the open shot - he stepped off the wall and fired (%d rounds, %d open shots)"), NewRounds(State, 0),
					Commander->GetCoverOpenShots() - State.OpenShotsBefore));
			if (Commander->bInCover)
			{
				Commander->LeaveCover(TEXT("smoke: cover part done"));
			}
			Commander->StopOperative();
			if (State.Wall.IsValid())
			{
				State.Wall->Destroy();
			}
			GoTo(State, 60);
			return true;
		}
		case 60: // F: a tutorial beat locking "Attack.*".
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Tutorial.Enabled")))
			{
				Enabled->Set(1, ECVF_SetByCode);
			}
			FTutorialStepsData Data;
			TArray<FString> Warnings;
			FString Error;
			const bool bParsed = TutorialRules::ParseSteps(TEXT(R"({"start_beat":"Q0","beats":[{"id":"Q0","checkpoint":false,"lock":["Attack.*"]}],"steps":[
				{"id":"qf_wait","beat":"Q0","complete":{"commands":["Squad.Guard"]}}]})"), Data, Warnings, Error);
			Check(State, bParsed, TEXT("F: inline steps parse ") + Error);
			Tutorial->LoadSteps(Data);
			Tutorial->StartBeat(TEXT("Q0"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::QuickFire), TEXT("F: Attack.QuickFire is locked by \"Attack.*\""));
			State.Refusals = Tutorial->GetLockedRefusalCount();
			Snapshot(World, State);
			CtrlClick(PC, HitFromAbove(World, State.P + State.F * 800.f, State));
			Check(State, Tutorial->GetLockedRefusalCount() == State.Refusals + 1, TEXT("F: the Ctrl + click is refused by the lock (one refusal)"));
			Check(State, !AnyQuickFireOrder(State) && CountMessages(World, TEXT("Fire!")) == State.FireLines, TEXT("F: no order, no 'Fire!'"));
			Next(State);
			return true;
		}
		case 61:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == 0, TEXT("F: nothing fired"));
			Tutorial->SkipTutorial();
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::QuickFire), TEXT("F: tutorial skipped - free again"));
			// G: the wave fight, its enemies parked far away; tactical pause.
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("G: real-time wave fight"));
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("G: tactical pause"));
			GoTo(State, 81);
			return true;
		}
		case 81:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Snapshot(World, State);
			CtrlClick(PC, HitFromAbove(World, State.P + State.F * 800.f - State.R * 100.f, State));
			Check(State, Commander->HasPlannedQuickFire() && !Commander->HasQuickFireOrder(), TEXT("G: the pause Ctrl + click is only planned"));
			Check(State, Feedback->GetPlannedMarkerCount() == State.Markers + 1, TEXT("G: a plan marker at the point"));
			Check(State, CountMessages(World, TEXT("[PLAN] Fire at the point planned")) >= 1, TEXT("G: the plan feed line"));
			Next(State);
			return true;
		}
		case 82:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == 0, TEXT("G: nothing fired during the pause"));
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("G: the pause released"));
			Next(State);
			return true;
		}
		case 83:
		{
			if (NewRounds(State, 0) < State.Burst && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, NewRounds(State, 0) == State.Burst && !Commander->HasPlannedQuickFire(), TEXT("G: the planned burst fired on the release"));
			Check(State, Feedback->GetPlannedMarkerCount() == 0, TEXT("G: the plan marker cleared"));
			// H: turn-based with a brute 3 cells out along the world axis nearest to the squad's forward.
			const FVector Axis = FMath::Abs(State.F.X) >= FMath::Abs(State.F.Y) ? FVector(FMath::Sign(State.F.X), 0.f, 0.f) : FVector(0.f, FMath::Sign(State.F.Y), 0.f);
			if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
			{
				State.Enemy = Waves->SpawnEnemy(EEnemyArchetype::Brute, Commander->GetActorLocation() + Axis * 450.f + FVector(0.f, 0.f, 20.f), (-Axis).Rotation());
			}
			Freeze(State.Enemy.Get());
			State.Point = Commander->GetActorLocation() - Axis * 300.f;
			Next(State);
			return true;
		}
		case 84:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			AEnemyCharacter* Enemy = State.Enemy.Get();
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->Refresh();
			}
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Enemy && Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("H: turn-based combat"));
			if (!Enemy || !TurnBased->IsActive())
			{
				return Finish(State, false);
			}
			TurnBased->SelectUnit(Commander);
			TurnBased->bGuaranteeAllHits = true;
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Commander);
			State.ApBefore = Unit ? Unit->AP : -1;
			State.Hints = CountMessages(World, TEXT("Ctrl + click to attack: pick"));
			Snapshot(World, State);
			// An empty spot: the grid's hint, no AP.
			CtrlClick(PC, HitFromAbove(World, State.Point, State));
			Unit = TurnBased->GetUnitState(Commander);
			Check(State, CountMessages(World, TEXT("Ctrl + click to attack: pick")) == State.Hints + 1, TEXT("H: Ctrl + click on an empty spot = the grid's hint"));
			Check(State, Unit && Unit->AP == State.ApBefore && !AnyQuickFireOrder(State), TEXT("H: ... no AP, no real-time order"));
			State.HealthBefore = Enemy->GetHealthComponent()->GetCurrentHealth();
			// The brute: the grid attack.
			CtrlClick(PC, HitOn(Enemy));
			Next(State);
			return true;
		}
		case 85:
		{
			if ((TurnBased->IsBusy() || State.StageTime < 0.5f) && State.StageTime < 8.f)
			{
				return true;
			}
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Commander);
			AEnemyCharacter* Enemy = State.Enemy.Get();
			Check(State, Unit && Unit->AP < State.ApBefore, FString::Printf(TEXT("H: Ctrl + click on the brute = the grid attack (AP %d -> %d)"), State.ApBefore, Unit ? Unit->AP : -1));
			Check(State, Enemy && Enemy->GetHealthComponent()->GetCurrentHealth() < State.HealthBefore, TEXT("H: the brute was hit"));
			Check(State, NewRounds(State, 0) == 0, TEXT("H: no real-time quick fire round on the grid"));
			Flow->ExitTurnBasedToRealTime();
			return Finish(State, true);
		}
		default:
			Check(State, false, FString::Printf(TEXT("unknown stage %d"), State.Stage));
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
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && Step(W, *State);
			}), StepSeconds);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.QuickFireSmoke"),
		TEXT("Dev check: Ctrl + click on a point = quick fire (one / the group, turn + burst, sniper, cover corner / open shot), Ctrl + click on an enemy / barrel / mine / trapped crate = the targeted shot as before, RMB click fires nothing, a tutorial lock, the tactical pause plan, the turn-based attack; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
