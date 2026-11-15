// Dev check for 059 look hold outside stealth (docs/design/LOOK_HOLD_SOLO.md) on L_MovementTest:
//   powershell .\Scripts\smoke.ps1 -Command CodexTactics.LookHoldSmoke -TimeoutSeconds 300
// Tests:
//  1. Shift + click ground outside stealth sets ManualLookPoint; operative rotates onto look anchor;
//  2. Walk forward 6 m: yaw stays on anchor, moving sideways (strafe);
//  3. Jog 6 m: yaw stays on anchor, moving sideways;
//  4. Prone crawl: body faces anchor while crawling sideways;
//  5. Double-click sprint clears ManualLookPoint;
//  6. Shift + click on self clears ManualLookPoint ("LOOK CLEARED");
//  7. Shift + click on enemy issues Suppress order (suppress precedence preserved);
//  8. Entering cover clears ManualLookPoint.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/LookHoldRules.h"

namespace LookHoldSmoke
{
	constexpr float Timeout = 120.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bDone = false;

		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		FVector LookPoint = FVector::ZeroVector;
		FVector MoveDest = FVector::ZeroVector;

		int32 WalkSamples = 0;
		int32 WalkWrongYaw = 0;
		int32 JogSamples = 0;
		int32 JogWrongYaw = 0;
		int32 CrawlSamples = 0;
		int32 CrawlWrongYaw = 0;

		TWeakObjectPtr<AEnemyCharacter> TestEnemy;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LookHoldSmoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LookHoldSmoke RESULT: %s (failures=%d complete=%d)"),
			State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"), State.Failures, bComplete ? 1 : 0);
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("LookHoldSmoke"));
		return false;
	}

	void Next(FState& State, int32 NextStage)
	{
		State.Stage = NextStage;
		State.StageTime = 0.f;
	}

	bool Step(UWorld* World, FState& State, float DeltaSeconds)
	{
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;

		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("Timed out at stage %d"), State.Stage));
			return Finish(State, false);
		}

		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!PC || !Squad)
		{
			return true;
		}

		AOperativeCharacter* Op = Squad->GetLeader();
		if (!Op || !Op->HealthComponent || !Op->HealthComponent->IsAlive())
		{
			return true;
		}

		switch (State.Stage)
		{
		case 0: // Setup solo operative on L_MovementTest
		{
			if (State.StageTime < 0.5f) { return true; }
			Squad->EnterSoloMode();
			Op->SetStealthMode(false);
			Op->SetStance(EOperativeStance::Standing);
			Op->StopOperative();
			Op->ClearManualLookPoint();

			State.Origin = Op->GetActorLocation();
			State.Forward = Op->GetActorForwardVector().GetSafeNormal2D();
			State.Right = Op->GetActorRightVector().GetSafeNormal2D();

			// Anchor point 90 deg right (600 cm away)
			State.LookPoint = State.Origin + State.Right * 600.f;

			Check(State, Squad->IsSoloMode(), TEXT("Operative in solo mode"));
			Check(State, !Op->IsInStealthMode(), TEXT("Operative not in stealth"));
			Check(State, !Op->HasManualLookPoint(), TEXT("No look anchor initially"));

			Next(State, 1);
			break;
		}
		case 1: // Shift + click on ground to set look anchor
		{
			PC->SetSimulatedShiftModifier(true);
			FHitResult Hit(nullptr, nullptr, State.LookPoint, FVector::UpVector);
			Hit.bBlockingHit = true;
			PC->HandleWorldHit(Hit);
			PC->SetSimulatedShiftModifier(false);

			FVector ActiveAnchor = FVector::ZeroVector;
			const bool bHasAnchor = Op->GetManualLookPoint(ActiveAnchor);
			Check(State, bHasAnchor && FVector::Dist2D(ActiveAnchor, State.LookPoint) < 10.f,
				TEXT("Shift + LMB on ground sets ManualLookPoint outside stealth"));

			Next(State, 2);
			break;
		}
		case 2: // Wait for operative to rotate onto look anchor
		{
			if (State.StageTime < 0.4f) { return true; }
			const float ExpectedYaw = static_cast<float>((State.LookPoint - Op->GetActorLocation()).Rotation().Yaw);
			const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), ExpectedYaw));
			Check(State, DeltaYaw <= 15.f, FString::Printf(TEXT("Operative rotates onto look anchor (delta=%.1f deg)"), DeltaYaw));

			// Order move forward 6 m (walk)
			State.MoveDest = State.Origin + State.Forward * 600.f;
			State.WalkSamples = State.WalkWrongYaw = 0;
			Op->OrderMoveTo(State.MoveDest, false);

			Next(State, 3);
			break;
		}
		case 3: // Sample walking strafe while watching anchor
		{
			const float Speed = Op->GetVelocity().Size2D();
			if (Speed > 20.f)
			{
				++State.WalkSamples;
				const float ExpectedYaw = static_cast<float>((State.LookPoint - Op->GetActorLocation()).Rotation().Yaw);
				const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), ExpectedYaw));
				if (DeltaYaw > 15.f)
				{
					++State.WalkWrongYaw;
				}
			}

			if ((State.StageTime < 0.5f || Speed > 10.f) && State.StageTime < 8.f)
			{
				return true;
			}

			Check(State, State.WalkSamples > 3 && State.WalkWrongYaw == 0,
				FString::Printf(TEXT("Walk strafe: yaw held within 15 deg of anchor (samples=%d wrong=%d)"),
					State.WalkSamples, State.WalkWrongYaw));

			// Order jog back to origin
			State.MoveDest = State.Origin;
			State.JogSamples = State.JogWrongYaw = 0;
			Op->OrderMoveTo(State.MoveDest, false);

			Next(State, 4);
			break;
		}
		case 4: // Sample jogging strafe while watching anchor
		{
			const float Speed = Op->GetVelocity().Size2D();
			if (Speed > 20.f)
			{
				++State.JogSamples;
				const float ExpectedYaw = static_cast<float>((State.LookPoint - Op->GetActorLocation()).Rotation().Yaw);
				const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), ExpectedYaw));
				if (DeltaYaw > 15.f)
				{
					++State.JogWrongYaw;
				}
			}

			if ((State.StageTime < 0.5f || Speed > 10.f) && State.StageTime < 8.f)
			{
				return true;
			}

			Check(State, State.JogSamples > 3 && State.JogWrongYaw == 0,
				FString::Printf(TEXT("Jog strafe: yaw held within 15 deg of anchor (samples=%d wrong=%d)"),
					State.JogSamples, State.JogWrongYaw));

			// Transition to prone stance
			Op->SetStance(EOperativeStance::Prone);
			Next(State, 5);
			break;
		}
		case 5: // Wait for prone stance and order crawl move
		{
			UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Op->GetStance() != EOperativeStance::Prone || (Anim && Anim->IsPlayingStanceTransition()))
			{
				if (State.StageTime < 5.f) { return true; }
			}

			// Anchor point relative to current prone position (90 deg to right)
			State.LookPoint = Op->GetActorLocation() + State.Right * 600.f;
			Op->SetManualLookPoint(State.LookPoint);

			State.MoveDest = Op->GetActorLocation() + State.Forward * 400.f;
			State.CrawlSamples = State.CrawlWrongYaw = 0;
			Op->OrderMoveTo(State.MoveDest, false);

			Next(State, 6);
			break;
		}
		case 6: // Sample prone crawl sideways while facing anchor
		{
			const float Speed = Op->GetVelocity().Size2D();
			if (Speed > 10.f)
			{
				++State.CrawlSamples;
				const float ExpectedYaw = static_cast<float>((State.LookPoint - Op->GetActorLocation()).Rotation().Yaw);
				const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), ExpectedYaw));
				if (DeltaYaw > 20.f)
				{
					++State.CrawlWrongYaw;
				}
			}

			if ((State.StageTime < 0.5f || Speed > 5.f) && State.StageTime < 8.f)
			{
				return true;
			}

			Check(State, State.CrawlSamples > 2 && State.CrawlWrongYaw == 0,
				FString::Printf(TEXT("Prone crawl strafe: body faces anchor while moving sideways (samples=%d wrong=%d)"),
					State.CrawlSamples, State.CrawlWrongYaw));

			// Stand up and test sprint clearing
			Op->SetStance(EOperativeStance::Standing);
			Next(State, 7);
			break;
		}
		case 7: // Test Sprint clearing manual look point
		{
			UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Op->GetStance() != EOperativeStance::Standing || (Anim && Anim->IsPlayingStanceTransition()))
			{
				if (State.StageTime < 5.f) { return true; }
			}

			State.LookPoint = Op->GetActorLocation() + State.Right * 600.f;
			Op->SetManualLookPoint(State.LookPoint);
			Check(State, Op->HasManualLookPoint(), TEXT("Re-armed manual look point before sprint"));

			// Order sprint
			Op->OrderMoveTo(Op->GetActorLocation() + State.Forward * 500.f, true);
			Check(State, !Op->HasManualLookPoint(), TEXT("Sprint order clears ManualLookPoint"));

			Op->StopOperative();
			Next(State, 8);
			break;
		}
		case 8: // Test Shift + click on self clears look point
		{
			if (State.StageTime < 0.3f) { return true; }

			State.LookPoint = Op->GetActorLocation() + State.Right * 600.f;
			Op->SetManualLookPoint(State.LookPoint);
			Check(State, Op->HasManualLookPoint(), TEXT("Manual look point active before self-click"));

			// Shift + click on self
			PC->SetSimulatedShiftModifier(true);
			FHitResult SelfHit(Op, nullptr, Op->GetActorLocation(), FVector::UpVector);
			SelfHit.bBlockingHit = true;
			PC->HandleWorldHit(SelfHit);
			PC->SetSimulatedShiftModifier(false);

			Check(State, !Op->HasManualLookPoint(), TEXT("Shift + click on self clears ManualLookPoint outside stealth"));

			Next(State, 9);
			break;
		}
		case 9: // Test Shift + click on enemy issues Suppress order (precedence over look hold)
		{
			if (State.StageTime < 0.2f) { return true; }

			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			if (Waves)
			{
				const FVector EnemyPos = Op->GetActorLocation() + State.Forward * 800.f;
				if (AEnemyCharacter* Enemy = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, EnemyPos, (-State.Forward).Rotation()))
				{
					Enemy->SetActorTickEnabled(false);
					Enemy->CustomTimeDilation = 0.f;
					Enemy->bOverridePerception = true;
					Enemy->PerceptionOverride.SightRangeCm = 0.f;
					State.TestEnemy = Enemy;
				}
			}
			if (Sight)
			{
				Sight->Refresh();
			}

			Check(State, State.TestEnemy.IsValid(), TEXT("Spawned enemy for suppression test"));

			// Re-arm look point first
			State.LookPoint = Op->GetActorLocation() + State.Right * 600.f;
			Op->SetManualLookPoint(State.LookPoint);
			Check(State, Op->HasManualLookPoint(), TEXT("Manual look point active before enemy click"));

			// Shift + click on enemy
			PC->SetSimulatedShiftModifier(true);
			FHitResult EnemyHit(State.TestEnemy.Get(), nullptr, State.TestEnemy->GetActorLocation(), FVector::UpVector);
			EnemyHit.bBlockingHit = true;
			PC->HandleWorldHit(EnemyHit);
			PC->SetSimulatedShiftModifier(false);

			Check(State, Op->HasSuppressOrder(), TEXT("Shift + click on enemy issues Suppress order (precedence preserved)"));

			Op->CancelSuppressFire(TEXT("end of test"));
			if (State.TestEnemy.IsValid())
			{
				State.TestEnemy->Destroy();
			}

			Next(State, 10);
			break;
		}
		case 10: // Test entering cover clears look point
		{
			if (State.StageTime < 0.2f) { return true; }

			State.LookPoint = Op->GetActorLocation() + State.Right * 600.f;
			Op->SetManualLookPoint(State.LookPoint);
			Check(State, Op->HasManualLookPoint(), TEXT("Manual look point active before cover entry"));

			// Simulate cover entry
			FCoverSlot DummySlot;
			DummySlot.WorldLocation = Op->GetActorLocation();
			DummySlot.WallNormal = -State.Forward;
			DummySlot.Height = ECoverHeight::HighCover;
			Op->EnterCover(DummySlot);

			Check(State, Op->bInCover, TEXT("Operative entered cover"));
			Check(State, !Op->HasManualLookPoint(), TEXT("Entering cover clears ManualLookPoint"));

			Op->LeaveCover(TEXT("test finished"));
			return Finish(State, true);
		}
		default:
			return false;
		}

		return true;
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LookHoldSmoke"),
		TEXT("Dev check for 059 look hold outside stealth on L_MovementTest: walk / jog / prone strafe, sprint clear, self clear, suppress precedence, cover clear. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
