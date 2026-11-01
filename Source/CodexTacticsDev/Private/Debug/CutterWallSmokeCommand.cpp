// Dev-only headless check of game designer bug 06 (2026-11-01, L_FormationLabCombat: "the Cutter attacks THROUGH walls, both during its
// LEAP and its NORMAL attack") on /Game/Maps/L_FormationLabCombat (its 3 m x 40 cm lab wall W_E_13 along y = 2500, x -4000..300):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CutterWallSmoke   (map + -LevelJson from test_map.json)
//  1. the squad (Passive: no fire) against the wall's south face, a Cutter right behind it on the north face - within bite reach through the
//     wall: no blow lands while it is on the far side (the squad's blocked hits do not rise; the lab's immortality counts them), it holds
//     its blow ("[Melee] ... within reach but ... is between") and goes round;
//  2. the Cutter 5.7 m behind the wall (pounce range): no pounce is launched, it is refused ("a wall between");
//  3. the Cutter 6 m out on the squad's side, a clear line: it pounces (launched, the landing hurts or is dodged);
//  4. next to the squad, a clear line: its blows land.
// Rules: MeleeLineRules (CodexTactics.Characters.EnemyAI.MeleeLine.*). UE-only (no Godot reference: Godot checks the distance only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"

namespace CutterWallSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 120.f;
	/** W_E_13 (formation_lab_01.json): the wall's line and its faces. */
	constexpr float WallY = 2500.f;
	constexpr float WallHalfThickness = 20.f;
	constexpr float SquadX = -1800.f;
	/** The squad's centres against the south face, the Cutter's against the north one: 135 cm apart through the wall (bite reach 150). */
	constexpr float SquadY = WallY - WallHalfThickness - 45.f;
	constexpr float CutterNearY = WallY + WallHalfThickness + 50.f;

	enum class EStage : uint8 { Setup, ThroughWall, LeapThroughWall, LeapClear, BlowClear, Done };

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Cutter;
		int32 HitsAtStage = 0;
		int32 LaunchesAtStage = 0;
		int32 HoldsAtStage = 0;
		int32 RefusalsAtStage = 0;
		/** Hits / launches while the Cutter stood on the far side of the wall (must stay 0). */
		int32 FarSideHits = 0;
		int32 FarSideLaunches = 0;
		float FarSideSeconds = 0.f;
		int32 LastHits = 0;
		int32 LastLaunches = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("CutterWallSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage, const AEnemyCharacter* Cutter, int32 Hits)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.HitsAtStage = Hits;
		State.LaunchesAtStage = Cutter ? Cutter->GetLeapsLaunched() : 0;
		State.HoldsAtStage = Cutter ? Cutter->GetMeleeWallHolds() : 0;
		State.RefusalsAtStage = Cutter ? Cutter->GetLeapWallRefusals() : 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[CutterWall] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	int32 BlockedHits(const USquadSubsystem& Squad)
	{
		int32 Hits = 0;
		for (const AOperativeCharacter* Member : Squad.GetMembers())
		{
			Hits += Member && Member->HealthComponent ? Member->HealthComponent->GetCheatBlockedHits() : 0;
		}
		return Hits;
	}

	void Teleport(ACharacter& Character, const FVector2D& Spot)
	{
		Character.SetActorLocation(FVector(Spot.X, Spot.Y, Character.GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	bool IsFarSide(const AEnemyCharacter& Cutter)
	{
		const FVector Location = Cutter.GetActorLocation();
		return Location.Y > WallY + WallHalfThickness && Location.X > -4000.f && Location.X < 300.f;
	}

	/** Counts hits / launches while the Cutter is on the far side of the wall. */
	void SampleFarSide(FState& State, const AEnemyCharacter& Cutter, int32 Hits, float Dt)
	{
		if (IsFarSide(Cutter))
		{
			State.FarSideSeconds += Dt;
			State.FarSideHits += FMath::Max(0, Hits - State.LastHits);
			State.FarSideLaunches += FMath::Max(0, Cutter.GetLeapsLaunched() - State.LastLaunches);
		}
		State.LastHits = Hits;
		State.LastLaunches = Cutter.GetLeapsLaunched();
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Waves || !Leader)
		{
			Check(State, false, TEXT("squad, wave subsystem and a leader"));
			return Finish(State, false);
		}
		AEnemyCharacter* Cutter = State.Cutter.Get();
		const int32 Hits = BlockedHits(*Squad);

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (Pressure)
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			Check(State, CombatCheats::IsImmortal(World), TEXT("immortality on (the lab's level JSON): every landed hit is counted, nobody dies"));
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // nobody shoots the Cutter
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Teleport(*Member, FVector2D(SquadX + (Index == 0 ? 0.f : (Index % 2 ? 110.f : -110.f) * ((Index + 1) / 2)), SquadY));
				++Index;
			}
			FHitResult WallHit;
			const bool bWall = World->LineTraceSingleByChannel(WallHit, FVector(SquadX, SquadY, 150.f), FVector(SquadX, CutterNearY, 150.f), ECC_Pawn,
				FCollisionQueryParams(SCENE_QUERY_STAT(CutterWallSmoke), false, Leader));
			Check(State, bWall && !WallHit.GetActor()->IsA<APawn>(), FString::Printf(TEXT("the lab wall between the squad and the Cutter's spot (%s)"),
				*GetNameSafe(WallHit.GetActor())));
			State.Cutter = Waves->SpawnEnemy(EEnemyArchetype::Cutter, FVector(SquadX, CutterNearY, 100.f), FRotator(0.f, -90.f, 0.f));
			Cutter = State.Cutter.Get();
			if (!Cutter)
			{
				Check(State, false, TEXT("a Cutter spawned"));
				return Finish(State, false);
			}
			Teleport(*Cutter, FVector2D(SquadX, CutterNearY));
			State.LastHits = Hits;
			State.LastLaunches = Cutter->GetLeapsLaunched();
			Enter(State, EStage::ThroughWall, Cutter, Hits);
			return true;
		}

		case EStage::ThroughWall:
		{
			if (!Cutter)
			{
				Check(State, false, TEXT("the Cutter lives"));
				return Finish(State, false);
			}
			SampleFarSide(State, *Cutter, Hits, Dt);
			if (State.StageTime < 6.f && IsFarSide(*Cutter))
			{
				return true;
			}
			Check(State, Cutter->GetMeleeWallHolds() > State.HoldsAtStage,
				FString::Printf(TEXT("within bite reach through the wall it held its blow (%d frames held)"), Cutter->GetMeleeWallHolds() - State.HoldsAtStage));
			Check(State, State.FarSideHits == 0, FString::Printf(TEXT("no blow through the wall: %d hits in %.1f s on the far side"), State.FarSideHits,
				State.FarSideSeconds));
			// 2. Pounce range behind the wall.
			Cutter->GetCharacterMovement()->StopMovementImmediately();
			Teleport(*Cutter, FVector2D(SquadX, WallY + 520.f));
			Enter(State, EStage::LeapThroughWall, Cutter, Hits);
			return true;
		}

		case EStage::LeapThroughWall:
		{
			if (!Cutter)
			{
				Check(State, false, TEXT("the Cutter lives"));
				return Finish(State, false);
			}
			SampleFarSide(State, *Cutter, Hits, Dt);
			if (State.StageTime < 3.f && IsFarSide(*Cutter))
			{
				return true;
			}
			Check(State, Cutter->GetLeapWallRefusals() > State.RefusalsAtStage,
				FString::Printf(TEXT("in pounce range behind the wall the pounce was refused (%d times)"), Cutter->GetLeapWallRefusals() - State.RefusalsAtStage));
			Check(State, State.FarSideLaunches == 0 && State.FarSideHits == 0,
				FString::Printf(TEXT("no pounce over / through the wall: %d launched, %d hits in %.1f s on the far side"), State.FarSideLaunches, State.FarSideHits,
					State.FarSideSeconds));
			// 3. A clear line on the squad's side, 6 m out.
			Cutter->GetCharacterMovement()->StopMovementImmediately();
			Teleport(*Cutter, FVector2D(SquadX, SquadY - 600.f));
			Enter(State, EStage::LeapClear, Cutter, Hits);
			return true;
		}

		case EStage::LeapClear:
		{
			const bool bLaunched = Cutter && Cutter->GetLeapsLaunched() > State.LaunchesAtStage;
			const UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
			const bool bLanded = bLaunched && !Cutter->IsJumpAttacking();
			if (!bLanded && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, bLaunched, FString::Printf(TEXT("with a clear line it pounces from 6 m (%.1f s)"), State.StageTime));
			Check(State, Hits > State.HitsAtStage || (Floating && Floating->HasShown(TEXT("DODGE"))),
				FString::Printf(TEXT("its landing hurt the squad (%d hits) or was dodged"), Hits - State.HitsAtStage));
			Enter(State, EStage::BlowClear, Cutter, Hits);
			return true;
		}

		case EStage::BlowClear:
		{
			if (Hits < State.HitsAtStage + 2 && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Hits >= State.HitsAtStage + 2, FString::Printf(TEXT("with a clear line its blows land (%d hits in %.1f s)"), Hits - State.HitsAtStage,
				State.StageTime));
			float Lost = 0.f;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Lost += Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() - Member->HealthComponent->GetCurrentHealth() : 0.f;
			}
			Check(State, Lost <= 0.01f, FString::Printf(TEXT("the immortality cheat absorbed them all (health lost %.1f)"), Lost));
			return Finish(State, true);
		}

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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CutterWallSmoke"),
		TEXT("Dev check (bug 06, 2026-11-01, L_FormationLabCombat): a Cutter behind a lab wall neither bites nor pounces through it; with a clear line ")
		TEXT("it pounces and bites. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
