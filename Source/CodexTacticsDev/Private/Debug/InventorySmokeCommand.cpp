// Dev-only console command for a headless personal inventory check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.InventorySmoke
// 1. "INV" opens the drawer with the leader's title and lines; the weapon selector closes it; 2. H uses a medkit
// (+HP, one spent, feed line), the drawer's canned food warms up; 3. the drawer's turret line with no turret on the
// commander: a squad mate hands one over and the placement starts (Godot inventory_drawer.gd, main.gd use_squad_item,
// _start_placement_for_type, player.gd heal_with_item).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/InventoryDrawerWidget.h"

namespace InventorySmoke
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
		FPlatformMisc::RequestExit(false, TEXT("InventorySmoke"));
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
		AOperativeCharacter* Leader = Squad->GetLeader();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UInventoryDrawerWidget* Drawer = Hud ? Hud->GetInventoryDrawer() : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		if (!Leader || !Drawer || !Bar)
		{
			Check(State, false, TEXT("leader, drawer and action bar"));
			return Finish(State, false);
		}

		// 1. Drawer.
		Check(State, !Drawer->IsOpen(), TEXT("drawer closed at start"));
		Hud->ToggleInventoryDrawer();
		Check(State, Drawer->IsOpen(), TEXT("\"INV\" opens the drawer"));
		Check(State, Drawer->GetTitleText().ToString().Contains(TEXT("PERSONAL INVENTORY: COMMANDER")), Drawer->GetTitleText().ToString());
		const FString MedkitLine = Drawer->GetSlotText(EInventoryDrawerSlot::Medkit).ToString();
		Check(State, MedkitLine.Contains(FString::Printf(TEXT("Medkit [H]: x%d"), Leader->MedkitsCount)), MedkitLine);
		Bar->ToggleWeaponSelector();
		Check(State, !Drawer->IsOpen() && Bar->IsWeaponSelectorOpen(), TEXT("the weapon selector closes the drawer"));
		Bar->ToggleWeaponSelector();
		Hud->ToggleInventoryDrawer();

		// 2. Items.
		Leader->MedkitsCount = FMath::Max(1, Leader->MedkitsCount);
		const int32 Medkits = Leader->MedkitsCount;
		Leader->HealthComponent->ApplyDirectHealthLoss(100.f, TEXT("Smoke"));
		const float Hurt = Leader->HealthComponent->GetCurrentHealth();
		PC->UseSquadItem(EPersonalItem::Medkit); // H
		Check(State, Leader->MedkitsCount == Medkits - 1 && FMath::IsNearlyEqual(Leader->HealthComponent->GetCurrentHealth(), Hurt + 80.f),
			FString::Printf(TEXT("H: medkit +80 HP (%.0f -> %.0f)"), Hurt, Leader->HealthComponent->GetCurrentHealth()));
		Leader->CannedFoodCount = FMath::Max(1, Leader->CannedFoodCount);
		Leader->ColdLevel = 50.f;
		Drawer->Activate(EInventoryDrawerSlot::CannedFood);
		Check(State, FMath::IsNearlyEqual(Leader->ColdLevel, 25.f, 1.f), FString::Printf(TEXT("drawer canned food: cold 50 -> %.0f"), Leader->ColdLevel));

		// 3. Turret from a squad mate.
		Leader->AddDeployable(EDeployableType::Turret, -Leader->GetDeployableCount(EDeployableType::Turret));
		AOperativeCharacter* Mate = Squad->GetMembers().Last();
		Mate->AddDeployable(EDeployableType::Turret, 1);
		const int32 MateTurrets = Mate->GetDeployableCount(EDeployableType::Turret);
		Check(State, Drawer->IsSlotEnabled(EInventoryDrawerSlot::Turret) && Drawer->GetSlotText(EInventoryDrawerSlot::Turret).ToString().Contains(TEXT("in squad")),
			Drawer->GetSlotText(EInventoryDrawerSlot::Turret).ToString());
		Drawer->Activate(EInventoryDrawerSlot::Turret);
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		Check(State, !Drawer->IsOpen() && Relocation->IsPlacingDeployable() && Leader->GetDeployableCount(EDeployableType::Turret) == 1
			&& Mate->GetDeployableCount(EDeployableType::Turret) == MateTurrets - 1, TEXT("turret handed over, placement started"));
		Relocation->CancelPlacement();
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
		TEXT("CodexTactics.InventorySmoke"),
		TEXT("Dev check: inventory drawer, provisions (H / drawer), turret hand-over from the drawer; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
