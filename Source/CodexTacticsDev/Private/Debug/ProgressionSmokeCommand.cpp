// Dev-only console command for a headless progression check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ProgressionSmoke
// Godot player.gd add_exp / _on_level_up / increase_stat / decrease_stat, enemy_base.gd kill EXP for the whole squad,
// main.gd profile dialog (key P, number keys, wave-clear auto open, Esc).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/ProfileDialogWidget.h"

namespace ProgressionSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<int32> ExpBefore;
		int32 KillReward = 0;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ProgressionSmoke"));
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
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UProfileDialogWidget* Profile = Hud ? Hud->GetProfileDialog() : nullptr;
		if (!Leader || !Profile || State.Time > 25.f)
		{
			Check(State, false, TEXT("leader / profile dialog / timeout"));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			// Level-up: 260 EXP at level 1 -> level 2 with 10 EXP, +3 points, full heal, floating text and radio line.
			Check(State, Leader->Level == 1 && Leader->CurrentExp == 0 && Leader->UnspentStatPoints == 0 && Leader->GetNextLevelExp() == 250,
				TEXT("fresh operative: level 1, 0 / 250 EXP"));
			Leader->HealthComponent->ApplyDirectHealthLoss(40.f, TEXT("Smoke"));
			Leader->AddExp(260);
			Check(State, Leader->Level == 2 && Leader->CurrentExp == 10 && Leader->UnspentStatPoints == 3,
				FString::Printf(TEXT("+260 EXP: level %d, %d EXP, %d points"), Leader->Level, Leader->CurrentExp, Leader->UnspentStatPoints));
			Check(State, FMath::IsNearlyEqual(Leader->HealthComponent->GetCurrentHealth(), Leader->HealthComponent->GetMaxHealth()),
				TEXT("level-up heals fully"));
			const UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
			Check(State, Floating && Floating->HasShown(TEXT("LEVEL 2")), TEXT("floating \"LEVEL 2!\""));

			// Kill EXP: every squad member gets the hound reward.
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.ExpBefore.Add(Member->CurrentExp);
			}
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				SmokeUtils::LevelPoint(World, FVector(1500.f, 1500.f, 100.f)), FRotator::ZeroRotator);
			if (!State.Hound.IsValid())
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State, false);
			}
			State.KillReward = State.Hound->GetKillExpReward();
			Check(State, State.KillReward == 15, FString::Printf(TEXT("hound reward %d (game_balance_config.tres 15)"), State.KillReward));
			State.Hound->GetHealthComponent()->ApplyDirectHealthLoss(10000.f, TEXT("Smoke"));
			bool bAll = true;
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				bAll &= Members[Index]->CurrentExp == State.ExpBefore[Index] + State.KillReward;
			}
			Check(State, bAll && Members.Num() >= 3, FString::Printf(TEXT("kill: all %d members +%d EXP"), Members.Num(), State.KillReward));
			Next();
			return true;
		}
		case 1:
		{
			// Profile: P opens the leader's card; + / - spend and refund points within the bounds.
			PC->ProfilePressed();
			Check(State, Profile->IsOpen() && Profile->GetMember() == Leader, TEXT("P opens the leader's profile"));
			Check(State, Profile->GetHeaderText().Contains(TEXT("Level: 2")) && Profile->GetRowText(0) == FString::Printf(TEXT("📈 Experience: %d / 500 XP"), Leader->CurrentExp),
				FString::Printf(TEXT("header / EXP row: %s"), *Profile->GetRowText(0)));
			const float Luck = Leader->Luck;
			const float MaxHealth = Leader->HealthComponent->GetMaxHealth();
			Check(State, !Profile->IsStatButtonEnabled(EProgressStat::Luck, -1), TEXT("no refund below the starting luck"));
			Profile->ClickStat(EProgressStat::Luck, 1);
			Check(State, FMath::IsNearlyEqual(Leader->Luck, Luck + 1.f) && Leader->UnspentStatPoints == 2, TEXT("+ luck: +1, 2 points left"));
			Profile->ClickStat(EProgressStat::Luck, -1);
			Check(State, FMath::IsNearlyEqual(Leader->Luck, Luck) && Leader->UnspentStatPoints == 3, TEXT("- luck: refunded"));
			Profile->ClickStat(EProgressStat::Health, 1);
			Check(State, FMath::IsNearlyEqual(Leader->HealthComponent->GetMaxHealth(), MaxHealth + 5.f)
				&& FMath::IsNearlyEqual(Leader->HealthComponent->GetCurrentHealth(), MaxHealth + 5.f), TEXT("+ HP: max and current +5"));
			Profile->ClickStat(EProgressStat::Fortitude, 1);
			Profile->ClickStat(EProgressStat::Accuracy, 1);
			Check(State, Leader->UnspentStatPoints == 0 && !Profile->IsStatButtonEnabled(EProgressStat::Luck, 1), TEXT("all points spent: + disabled"));
			Check(State, Profile->GetRowText(4).Contains(TEXT("Damage cut: -")), Profile->GetRowText(4));

			// Paging and the number keys (Godot _select_squad_member_by_index).
			Profile->SwitchMember(1);
			AOperativeCharacter* Second = Squad->GetMembers()[1];
			Check(State, Profile->GetMember() == Second, TEXT("\"Next\" shows the next member"));
			Profile->SwitchMember(-1);
			Check(State, Profile->GetMember() == Leader, TEXT("\"Prev\" goes back"));
			PC->SelectMember(1);
			Check(State, Squad->GetLeader() == Second && Profile->GetMember() == Second, TEXT("number key: new leader, the open profile follows"));
			Hud->HandleEscape();
			Check(State, !Profile->IsOpen(), TEXT("Esc closes the profile"));
			PC->SelectMember(1);
			Check(State, Profile->IsOpen() && Profile->GetMember() == Second, TEXT("the leader's own number opens his profile"));
			PC->ProfilePressed();
			Check(State, !Profile->IsOpen(), TEXT("P closes it"));

			// Wave cleared: the first member with free points gets his profile opened.
			Second->AddExp(Second->GetNextLevelExp());
			World->GetSubsystem<UWaveSubsystem>()->OnWaveCleared.Broadcast(1);
			Check(State, Profile->IsOpen() && Profile->GetMember() == Second, TEXT("wave cleared: profile of the member with points"));
			Profile->Close();
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
		TEXT("CodexTactics.ProgressionSmoke"),
		TEXT("Dev check: EXP, level-up, kill rewards, stat points and the profile dialog; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
