// Dev-only console command for a headless squad control check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SquadControlSmoke
// Godot player.gd / main.gd: below 50 % health an operative is wounded (no sprint, slower); Alt + stance changes the
// whole squad («👥 ОТРЯД», order line); Alt + V while the leader walks: rev 5e (user request 2026-10-14) replaced Godot's
// refusal - a walking / running member dives prone on the move, a slow one stops first, the still ones drop in place;
// Shift + click turns the leader; an operative arriving next to a barricade in combat crouches in cover («IN COVER»).
// Formation sectors (bug 2026-10-26): the leader switched to another member and back - as a follower he turned to his place's look; leading
// again he looks forward (his own facing as the leader) within ~0.5 s (Codex.Formation.Sectors 0: the old facing, reported only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace SquadControlSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 100; // the leader switch first (exploration), then the stance / cover stages 0..3
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Mover;
		TWeakObjectPtr<AOperativeCharacter> FirstLeader;
		float FirstLeaderYaw = 0.f;
		double SwitchTime = 0.0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SquadControlSmoke"));
		return false;
	}

	bool HasMessage(UWorld* World, const FString& Part)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (Message.Text.ToString().Contains(Part))
			{
				return true;
			}
		}
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad->GetLeader();
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		if (!Flow || !PC || !Leader || Squad->GetMembers().Num() < 3 || State.Time > 50.f)
		{
			Check(State, false, FString::Printf(TEXT("controller, squad / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->CustomTimeDilation = 0.f;
		}
		switch (State.Stage)
		{
		case 100:
		{
			// Exploration, group mode, everybody standing still: the first leader's own facing.
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			State.FirstLeader = Leader;
			State.FirstLeaderYaw = static_cast<float>(Leader->GetActorRotation().Yaw);
			AOperativeCharacter* Other = Squad->GetMembers()[0] == Leader ? Squad->GetMembers()[1] : Squad->GetMembers()[0];
			Squad->SetLeader(Other);
			State.SwitchTime = World->GetTimeSeconds();
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 101:
		{
			// He follows another leader for a moment (his follower look), then leads again.
			if (World->GetTimeSeconds() - State.SwitchTime < 1.5)
			{
				return true;
			}
			AOperativeCharacter* First = State.FirstLeader.Get();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s as a follower faces %.0f (led facing %.0f)"), First ? *First->DisplayName.ToString() : TEXT("-"),
				First ? First->GetActorRotation().Yaw : 0.f, State.FirstLeaderYaw);
			if (First)
			{
				Squad->SetLeader(First);
			}
			State.SwitchTime = World->GetTimeSeconds();
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 102:
		{
			if (World->GetTimeSeconds() - State.SwitchTime < 0.5)
			{
				return true;
			}
			const AOperativeCharacter* First = State.FirstLeader.Get();
			const float Off = First ? FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(First->GetActorRotation().Yaw), State.FirstLeaderYaw)) : 999.f;
			const FString What = FString::Printf(TEXT("leader switched away and back: %.1f s later he looks forward again (facing %.0f, led facing %.0f, %.0f deg off)"),
				World->GetTimeSeconds() - State.SwitchTime, First ? First->GetActorRotation().Yaw : 0.f, State.FirstLeaderYaw, Off);
			static const IConsoleVariable* SectorsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors"));
			if (SectorsVar && SectorsVar->GetInt() != 0)
			{
				Check(State, First && Squad->GetLeader() == First && Off <= 30.f, What);
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: (Codex.Formation.Sectors 0, the old facing) %s"), *What);
			}
			State.Stage = 0;
			State.StageTime = 0.f;
			return true;
		}
		case 0:
		{
			// Wounded below 50 %: no sprint, slower.
			const float FullSpeed = Leader->GetMaxSpeed();
			Leader->HealthComponent->ApplyDirectHealthLoss(Leader->HealthComponent->GetMaxHealth() * 0.6f, TEXT("Smoke"));
			Check(State, Leader->IsWounded() && !Leader->CanSprint() && Leader->GetMaxSpeed() < FullSpeed
				&& FMath::IsNearlyEqual(Leader->GetCharacterMovement()->MaxWalkSpeed, Leader->GetMaxSpeed()),
				FString::Printf(TEXT("wounded: speed %.0f -> %.0f, no sprint"), FullSpeed, Leader->GetMaxSpeed()));
			Leader->HealthComponent->Heal(Leader->HealthComponent->GetMaxHealth());
			Check(State, !Leader->IsWounded() && Leader->CanSprint(), TEXT("healed: sprint again"));

			// Alt + stance: the whole squad.
			PC->SetEntireSquadStance(EOperativeStance::Crouching);
			bool bAllCrouched = true;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAllCrouched &= Member->GetStance() == EOperativeStance::Crouching;
			}
			Check(State, bAllCrouched && Floating->HasShown(TEXT("SQUAD: CROUCHED")) && HasMessage(World, TEXT("[SQUAD ORDER]")), TEXT("Alt + C: squad crouches"));
			PC->SetEntireSquadStance(EOperativeStance::Standing);

			// Shift + click: face the point.
			const FVector Right = Leader->GetActorLocation() + Leader->GetActorRightVector() * 500.f;
			Leader->SetFacingPoint(Right);
			Check(State, Leader->GetActorForwardVector().Dot((Right - Leader->GetActorLocation()).GetSafeNormal2D()) > 0.99f, TEXT("Shift + click: leader faces the point"));

			// Combat: Alt + V while the leader walks (rev 5e: each member goes prone his own way, nobody is refused).
			SmokeUtils::StartWaveFight(Flow); // sudden horde start, its free pause released
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorForwardVector() * 800.f), false);
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// Wait until he really walks (a loaded machine may still be accelerating at a fixed 0.5 s).
			const float Speed = static_cast<float>(Leader->GetVelocity().Size2D());
			if (State.StageTime < 0.5f || (Speed < 100.f && State.StageTime < 5.f))
			{
				return true;
			}
			PC->SetEntireSquadStance(EOperativeStance::Prone);
			bool bAllProne = true;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAllProne &= Member->GetStance() == EOperativeStance::Prone;
			}
			Check(State, bAllProne && !HasMessage(World, TEXT("while moving")) && Floating->HasShown(TEXT("SQUAD: PRONE")),
				FString::Printf(TEXT("Alt + V while the leader walks (%.0f cm/s): nobody refused, the whole squad goes prone"), Speed));
			Check(State, Leader->IsProneDiving() || !Leader->HasOwnMoveOrder(),
				FString::Printf(TEXT("the walking leader dives on the move or stops first (dive %d)"), static_cast<int32>(Leader->GetProneDive())));
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// The dive ends prone (root motion carried him; the clip may take a while on a loaded machine).
			if (Leader->IsProneDiving() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, !Leader->IsProneDiving() && Leader->GetStance() == EOperativeStance::Prone,
				FString::Printf(TEXT("the leader lies prone after the dive (%.1f s)"), State.StageTime));
			Leader->StopOperative();
			PC->SetEntireSquadStance(EOperativeStance::Standing);

			// Arrival next to a barricade in combat: cover.
			AOperativeCharacter* Mover = Squad->GetMembers()[1];
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Barricade = SmokeUtils::LevelPoint(World, FVector(-300.f, -700.f, 50.f));
			World->SpawnActor<ABarricadeActor>(Barricade, SmokeUtils::LayoutTransform(World).Rotator(), Params);
			Mover->SetStance(EOperativeStance::Standing);
			Mover->OrderMoveTo(SmokeUtils::LevelPoint(World, FVector(-300.f, -550.f, 100.f)), false);
			State.Mover = Mover;
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Mover = State.Mover.Get();
			if (Mover && Mover->GetStance() != EOperativeStance::Crouching && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Mover && Mover->IsInBarricadeCover() && Floating->HasShown(TEXT("IN COVER")),
				TEXT("arrived next to the barricade in combat: crouched in cover"));
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
		TEXT("CodexTactics.SquadControlSmoke"),
		TEXT("Dev check: wounded state, Alt squad stance, Shift facing, cover behind a barricade; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
