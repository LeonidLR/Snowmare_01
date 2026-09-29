// Dev-only console command for a headless save / load check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SaveLoadSmoke
// Slots go to Saved/SmokeSaves. The squad is put in a known state (hurt, cold, pistol with 5 rounds, no medkits,
// engineer on guard, empty canister taken, a crate looted), saved to «Леонид_01», changed, loaded: everything comes
// back; F5 writes «quicksave»; the slot list, metadata and the next suggested name are checked
// (Godot Scripts/managers/save_manager.gd, main.gd _perform_quick_save).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/LootCrateActor.h"
#include "Misc/Paths.h"
#include "Quests/QuestSubsystem.h"
#include "TimerManager.h"

namespace SaveLoadSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SaveLoadSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		AOperativeCharacter* Commander = Squad->GetLeader();
		AOperativeCharacter* Engineer = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
		}
		ALootCrateActor* Crate = nullptr;
		for (TActorIterator<ALootCrateActor> It(World); It && !Crate; ++It)
		{
			Crate = *It;
		}
		if (!Commander || !Engineer || !Saves || !Quests || !Crate)
		{
			Check(State, false, TEXT("squad, save subsystem, quests, a supply crate"));
			return Finish(State, false);
		}
		Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves");
		IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);

		// Known state.
		Commander->HealthComponent->ApplyDirectHealthLoss(40.f, TEXT("Smoke"));
		Commander->ColdLevel = 30.f;
		Commander->MedkitsCount = 0;
		Commander->SwitchToWeaponById(TEXT("pistol"));
		Commander->CurrentClip = 5;
		Squad->ToggleGuard(Engineer);
		FQuestChainState Chain;
		Chain.bHasEmptyCanister = true;
		Quests->RestoreState(Chain);
		Crate->RestoreSaved(true, false, false);
		const FVector Spot = Commander->GetActorLocation();
		const float Health = Commander->HealthComponent->GetCurrentHealth();

		Check(State, Saves->SaveGame(TEXT("Леонид_01")) && Saves->HasSave(TEXT("Леонид_01")), TEXT("saved to «Леонид_01»"));
		FSaveSlotInfo Info;
		const bool bInfo = Saves->GetSaveInfo(TEXT("Леонид_01"), Info);
		Check(State, bInfo && Info.StageName == TEXT("Периметр КПП (Поиск дизеля)")
			&& Info.SaveType == TEXT("manual") && Info.SquadCount == 3, FString::Printf(TEXT("metadata: %s | %s | %s"), *Info.StageName, *Info.SquadSummary, *Info.DateTime));
		Check(State, Saves->SuggestNextSlotName() == TEXT("Леонид_02"), Saves->SuggestNextSlotName());

		// Change everything.
		Commander->HealthComponent->Heal(1000.f);
		Commander->ColdLevel = 0.f;
		Commander->MedkitsCount = 3;
		Commander->SwitchToWeaponById(TEXT("m16"));
		Commander->TeleportTo(Spot + FVector(500.f, 0.f, 0.f), Commander->GetActorRotation(), false, true);
		Squad->ToggleGuard(Engineer);
		Quests->RestoreState(FQuestChainState());

		Check(State, Saves->LoadFromSlotWithMessage(TEXT("Леонид_01")), TEXT("loaded «Леонид_01»"));
		Check(State, FMath::IsNearlyEqual(Commander->HealthComponent->GetCurrentHealth(), Health) && FMath::IsNearlyEqual(Commander->ColdLevel, 30.f),
			FString::Printf(TEXT("health %.0f, cold %.0f back"), Commander->HealthComponent->GetCurrentHealth(), Commander->ColdLevel));
		Check(State, FVector::Dist2D(Commander->GetActorLocation(), Spot) < 5.f, TEXT("position back"));
		Check(State, Commander->MedkitsCount == 0, TEXT("items back"));
		Check(State, Commander->CurrentWeapon && Commander->CurrentWeapon->WeaponId == TEXT("pistol") && Commander->CurrentClip == 5,
			TEXT("pistol in hands with 5 rounds"));
		Check(State, Engineer->bGuarding && Squad->GetFormationSlot(Engineer) == INDEX_NONE, TEXT("the engineer guards again"));
		Check(State, Quests->HasEmptyCanister() && !Quests->HasFuelCanister(), TEXT("quest chain back"));
		Check(State, Crate->IsLooted(), TEXT("the crate stays looted"));

		Check(State, Saves->QuickSave() && Saves->HasSave(TEXT("quicksave")), TEXT("F5: quicksave written"));
		const TArray<FSaveSlotInfo> All = Saves->GetAllSaves();
		Check(State, All.Num() == 2, FString::Printf(TEXT("%d slots listed"), All.Num()));
		Check(State, Saves->DeleteSave(TEXT("quicksave")) && !Saves->HasSave(TEXT("quicksave")), TEXT("slot deleted"));
		IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
		return Finish(State, true);
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
		TEXT("CodexTactics.SaveLoadSmoke"),
		TEXT("Dev check: save a known squad / quest / crate state, change it, load it back, quicksave, slot list; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
