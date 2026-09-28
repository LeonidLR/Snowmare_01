// Dev-only console command for a headless phase-banner check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BannersSmoke
// 1. the gate opens -> cutscene card with a countdown; Space skips it; 2. the squad starts the preparation warm and
// healed; 3. preparation banner «ПОДГОТОВКА К БОЮ»; «Начать бой» -> wave banner «ВОЛНА 1 | ВРАГОВ ОСТАЛОСЬ»;
// 4. tactical pause -> «РЕЖИМ ПРИКАЗОВ» banner.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/UserWidget.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UI/PhaseBannersWidget.h"

namespace BannersSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<UPhaseBannersWidget> Banners;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BannersSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 30.f)
		{
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!PC || !Squad)
		{
			return Finish(State, false);
		}
		UPhaseBannersWidget* Banners = State.Banners.Get();
		if (!Banners)
		{
			Banners = CreateWidget<UPhaseBannersWidget>(PC, UPhaseBannersWidget::StaticClass());
			State.Banners = Banners;
		}
		switch (State.Stage)
		{
		case 0:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 60.f;
				Member->HealthComponent->ApplyDirectHealthLoss(40.f, TEXT("Smoke"));
			}
			Flow->TriggerCombatZone();
			Check(State, Banners->IsCutsceneShown(), TEXT("cutscene card shown"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetPhase() == ECodexGamePhase::Preparation, TEXT("Space skipped the cutscene"));
			{
				bool bFresh = true;
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					bFresh &= Member->ColdLevel < 1.f && Member->HealthComponent->GetCurrentHealth() >= Member->HealthComponent->GetMaxHealth();
				}
				Check(State, bFresh, TEXT("squad warm and healed for the preparation"));
			}
			Check(State, Banners->GetCombatText().ToString().StartsWith(TEXT("⏱ ПОДГОТОВКА К БОЮ: ")), Banners->GetCombatText().ToString());
			Flow->FinishPreparation();
			Next(State);
			return true;
		case 1:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Banners->GetCombatText().ToString().StartsWith(TEXT("⚔️ ВОЛНА 1 | ВРАГОВ ОСТАЛОСЬ: ")), Banners->GetCombatText().ToString());
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Banners->GetPauseText().ToString().StartsWith(TEXT("⏱️ РЕЖИМ ПРИКАЗОВ | Зарядов в волне: 2/3")), Banners->GetPauseText().ToString());
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.BannersSmoke"),
		TEXT("Dev check: cutscene card + skip, squad reset, preparation / wave / pause banners; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
