// Dev-only console command for a headless item hand-over check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TransferSmoke
// Sprint 13 drag & drop (the drop handlers are called directly — no real mouse in a headless run): the «ПЕРЕД» button is
// gone, the drawer shows the ammo and builds the drag payload; near the engineer 30 M16 rounds open the split dialog
// ([-] / [+] by 5, 17 snaps to 15, confirm -> 15 / +15), Esc cancels it, a remainder below 5 and a single medkit go
// without a dialog, a portrait (action bar slot) drop works, 3 medkits split by 1, the turret capacity clamps; far away
// «under fire» is blocked with the feed line, out of combat the commander walks over and hands over on arrival, and
// another move order cancels the pending hand-over.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Characters/TransferRules.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DeployableRules.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryDrawerWidget.h"
#include "UI/QuantitySplitDialogWidget.h"
#include "Core/SaveGameSubsystem.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Misc/Paths.h"

namespace TransferSmoke
{
	constexpr float StepSeconds = 0.25f;

	enum class EPhase : uint8
	{
		Instant,
		WaitApproach,
		StartCancel,
		WaitCancel,
		Ground,
		WaitGroundApproach,
		Crate
	};

	struct FState
	{
		float Time = 0.f;
		float PhaseTime = 0.f;
		int32 Failures = 0;
		EPhase Phase = EPhase::Instant;
		int32 LeaderMedkits = 0;
		int32 EngineerMedkits = 0;
		FVector DropPoint = FVector::ZeroVector;
	};

	/** The pile nearest to Point within Radius (null when none). */
	ADroppedItemActor* PileNear(UWorld* World, const FVector& Point, float Radius)
	{
		ADroppedItemActor* Best = nullptr;
		float BestDistance = Radius;
		for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
		{
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Point);
			if (!It->IsActorBeingDestroyed() && Distance <= BestDistance)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	bool PileGone(ADroppedItemActor* Pile)
	{
		return !IsValid(Pile) || Pile->IsActorBeingDestroyed();
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TransferSmoke"));
		return false;
	}

	void Teleport(AOperativeCharacter* Operative, const FVector& Location)
	{
		Operative->StopOperative();
		Operative->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void SetReserve(AOperativeCharacter* Operative, const TCHAR* WeaponId, int32 Count)
	{
		Operative->TakeReserve(WeaponId, 1000000);
		Operative->AddAmmo(WeaponId, Count);
	}

	FString LastFeed(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages && Messages->GetHistory().Num() > 0 ? Messages->GetHistory().Last().Text.ToString() : FString();
	}

	/** A navigable spot about Distance cm from the leader, on the side of Direction. */
	FVector FarSpot(UWorld* World, const AOperativeCharacter* Leader, const AOperativeCharacter* Ignore, const FVector& Direction, float Distance)
	{
		return SmokeUtils::FreeSpot(World, Leader->GetActorLocation() + Direction.GetSafeNormal2D() * Distance, Ignore);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		AOperativeCharacter* Engineer = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
		}
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UInventoryDrawerWidget* Drawer = Hud ? Hud->GetInventoryDrawer() : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		UQuantitySplitDialogWidget* Dialog = Hud ? Hud->GetQuantityDialog() : nullptr;
		USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
		if (!Leader || !Engineer || Leader == Engineer || !Drawer || !Bar || !Dialog || !Transfer)
		{
			Check(State, false, TEXT("squad, drawer, bar, quantity dialog, transfer subsystem"));
			return Finish(State, false);
		}
		auto Drop = [&](ETransferItem Item)
		{
			return Hud->HandleTransferDropOnActor(Leader, Item, Engineer, Engineer->GetActorLocation());
		};
		auto PlaceNear = [&]()
		{
			Teleport(Engineer, Leader->GetActorLocation() + FVector(120.f, 0.f, 0.f));
		};

		if (State.Phase == EPhase::Instant)
		{
			Check(State, Bar->WidgetTree && !Bar->WidgetTree->FindWidget(TEXT("BarTransferButton")), TEXT("the «ПЕРЕД» button is gone from the action bar"));
			SetReserve(Leader, TEXT("m16"), 30);
			Drawer->Open();
			Check(State, Drawer->IsSlotVisible(EInventoryDrawerSlot::RifleAmmo) && Drawer->GetSlotText(EInventoryDrawerSlot::RifleAmmo).ToString().Contains(TEXT("M16: 30")),
				Drawer->GetSlotText(EInventoryDrawerSlot::RifleAmmo).ToString());
			Check(State, !Drawer->IsSlotVisible(EInventoryDrawerSlot::PlasmaAmmo), TEXT("plasma line hidden without plasma"));
			const UInventoryDragDropOperation* Operation = Drawer->CreateDragOperation(EInventoryDrawerSlot::RifleAmmo);
			Check(State, Operation && Operation->Item == ETransferItem::RifleAmmo && Operation->Sender.Get() == Leader && Operation->Available == 30
				&& Operation->DefaultDragVisual, TEXT("drag payload: M16 rounds, the commander, 30, a drag visual"));
			Check(State, !Drawer->CreateDragOperation(EInventoryDrawerSlot::Tripwire), TEXT("the tripwire line is not draggable"));

			// Near (<= 2 m): 30 rounds -> split dialog, choose 15.
			PlaceNear();
			Check(State, TransferRules::CanTransferTo(*Leader, *Engineer), TEXT("engineer within 2 m"));
			const int32 EngineerRounds = Engineer->GetReserve(TEXT("m16"));
			Check(State, Drop(ETransferItem::RifleAmmo) == ETransferRequestOutcome::DialogOpened && Dialog->IsOpen() && Dialog->GetMaxQuantity() == 30
				&& Dialog->GetQuantity() == 30, TEXT("30 rounds near: the split dialog opens at the whole stack (max 30)"));
			Dialog->Decrement();
			const int32 AfterMinus = Dialog->GetQuantity();
			Dialog->Increment();
			const int32 AfterPlus = Dialog->GetQuantity();
			Dialog->SetQuantity(17);
			Check(State, AfterMinus == 25 && AfterPlus == 30 && Dialog->GetQuantity() == 15,
				FString::Printf(TEXT("[-] 25 / [+] 30 / 17 snaps to 15 (%d / %d / %d)"), AfterMinus, AfterPlus, Dialog->GetQuantity()));
			Check(State, Dialog->GetQuantityText().ToString().Contains(TEXT("макс. 30")), Dialog->GetQuantityText().ToString());
			Check(State, Dialog->Confirm() == ETransferRequestOutcome::Transferred && !Dialog->IsOpen() && Leader->GetReserve(TEXT("m16")) == 15
				&& Engineer->GetReserve(TEXT("m16")) == EngineerRounds + 15, TEXT("confirm 15: commander -15, engineer +15"));

			// Esc cancels the dialog.
			Check(State, Drop(ETransferItem::RifleAmmo) == ETransferRequestOutcome::DialogOpened && Dialog->GetMaxQuantity() == 15, TEXT("15 left: dialog again"));
			Hud->HandleEscape();
			Check(State, !Dialog->IsOpen() && Leader->GetReserve(TEXT("m16")) == 15 && Engineer->GetReserve(TEXT("m16")) == EngineerRounds + 15,
				TEXT("Esc closes the dialog, nothing moves"));

			// Fewer than 5 rounds: the whole remainder, no dialog.
			SetReserve(Leader, TEXT("m16"), 3);
			Check(State, Drop(ETransferItem::RifleAmmo) == ETransferRequestOutcome::Transferred && !Dialog->IsOpen() && Leader->GetReserve(TEXT("m16")) == 0
				&& Engineer->GetReserve(TEXT("m16")) == EngineerRounds + 18, TEXT("3 rounds: handed over at once, no dialog"));

			// A single medkit: no dialog.
			Leader->MedkitsCount = 1;
			int32 EngineerMedkits = Engineer->MedkitsCount;
			Check(State, Drop(ETransferItem::Medkit) == ETransferRequestOutcome::Transferred && !Dialog->IsOpen() && Leader->MedkitsCount == 0
				&& Engineer->MedkitsCount == EngineerMedkits + 1, TEXT("single medkit: instant, no dialog"));

			// Portrait drop (action bar squad slot).
			int32 EngineerSlot = INDEX_NONE;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				EngineerSlot = Bar->GetSlotMember(Index) == Engineer ? Index : EngineerSlot;
			}
			Leader->CannedFoodCount = 1;
			const int32 EngineerFood = Engineer->CannedFoodCount;
			Check(State, EngineerSlot != INDEX_NONE && Bar->HandleTransferDropOnSlot(EngineerSlot, Leader, ETransferItem::CannedFood) == ETransferRequestOutcome::Transferred
				&& Leader->CannedFoodCount == 0 && Engineer->CannedFoodCount == EngineerFood + 1, TEXT("drop on the engineer's portrait hands the can over"));

			// 3 medkits: a dialog stepping by 1.
			Leader->MedkitsCount = 3;
			EngineerMedkits = Engineer->MedkitsCount;
			Check(State, Drop(ETransferItem::Medkit) == ETransferRequestOutcome::DialogOpened && Dialog->GetQuantity() == 3, TEXT("3 medkits: dialog"));
			Dialog->Decrement();
			Check(State, Dialog->GetQuantity() == 2 && Dialog->Confirm() == ETransferRequestOutcome::Transferred && Leader->MedkitsCount == 1
				&& Engineer->MedkitsCount == EngineerMedkits + 2, TEXT("medkits step by 1: 2 handed over"));

			// Capacity: the engineer has room for one turret only.
			const int32 MaxTurrets = DeployableRules::GetMaxCarried(EDeployableType::Turret);
			Engineer->AddDeployable(EDeployableType::Turret, MaxTurrets - 1 - Engineer->GetDeployableCount(EDeployableType::Turret));
			Leader->AddDeployable(EDeployableType::Turret, 2 - Leader->GetDeployableCount(EDeployableType::Turret));
			Check(State, Drop(ETransferItem::Turret) == ETransferRequestOutcome::Transferred && !Dialog->IsOpen()
				&& Leader->GetDeployableCount(EDeployableType::Turret) == 1 && Engineer->GetDeployableCount(EDeployableType::Turret) == MaxTurrets,
				TEXT("turret: clamped to the one free place, no dialog"));
			Check(State, Drop(ETransferItem::Turret) == ETransferRequestOutcome::Failed && Leader->GetDeployableCount(EDeployableType::Turret) == 1,
				TEXT("turret to a full engineer: refused"));

			Check(State, Hud->HandleTransferDropOnActor(Leader, ETransferItem::Medkit, Leader, Leader->GetActorLocation()) == ETransferRequestOutcome::Failed,
				TEXT("drop on himself: nothing happens"));
			Drawer->Close();

			// Far away, under fire: blocked with the feed line.
			Teleport(Engineer, FarSpot(World, Leader, Engineer, FVector(-1.f, 0.f, 0.f), 600.f));
			Leader->MedkitsCount = 1;
			State.LeaderMedkits = 1;
			State.EngineerMedkits = Engineer->MedkitsCount;
			Check(State, !TransferRules::CanTransferTo(*Leader, *Engineer), FString::Printf(TEXT("engineer far (%.0f cm)"),
				FVector::Dist2D(Leader->GetActorLocation(), Engineer->GetActorLocation())));
			Transfer->bForceUnderFireForTesting = true;
			// Outcome and feed read in order (function arguments have no evaluation order).
			const ETransferRequestOutcome BlockedOutcome = Drop(ETransferItem::Medkit);
			const FString BlockedFeed = LastFeed(World);
			Check(State, BlockedOutcome == ETransferRequestOutcome::Blocked && Leader->MedkitsCount == 1 && !Transfer->HasPendingTransfer()
				&& BlockedFeed == TEXT("Слишком далеко для передачи (макс. 2 метра)"), TEXT("far under fire: blocked, feed line: ") + BlockedFeed);
			Transfer->bForceUnderFireForTesting = false;

			// Far, out of combat: the commander walks over.
			Check(State, Drop(ETransferItem::Medkit) == ETransferRequestOutcome::Approaching && Transfer->HasPendingTransfer()
				&& Transfer->GetPendingSender() == Leader && Leader->MedkitsCount == 1, TEXT("far out of combat: the commander walks over"));
			State.Phase = EPhase::WaitApproach;
			State.PhaseTime = 0.f;
			return true;
		}

		if (State.Phase == EPhase::WaitApproach)
		{
			if (Transfer->HasPendingTransfer() && State.PhaseTime < 25.f)
			{
				return true;
			}
			Check(State, !Transfer->HasPendingTransfer() && Leader->MedkitsCount == State.LeaderMedkits - 1
				&& Engineer->MedkitsCount == State.EngineerMedkits + 1,
				FString::Printf(TEXT("arrived (%.1f s, %.0f cm): medkit handed over"), State.PhaseTime,
					FVector::Dist2D(Leader->GetActorLocation(), Engineer->GetActorLocation())));
			State.Phase = EPhase::StartCancel;
			State.PhaseTime = 0.f;
			return true;
		}

		if (State.Phase == EPhase::StartCancel)
		{
			const FVector Side = FVector(0.f, 1.f, 0.f);
			Teleport(Engineer, FarSpot(World, Leader, Engineer, Side, 600.f));
			Leader->MedkitsCount = 1;
			State.LeaderMedkits = 1;
			State.EngineerMedkits = Engineer->MedkitsCount;
			Check(State, Drop(ETransferItem::Medkit) == ETransferRequestOutcome::Approaching, TEXT("second approach started"));
			const FVector Elsewhere = FarSpot(World, Leader, Leader, -Side, 500.f);
			Check(State, Leader->OrderMoveTo(Elsewhere, false) == EOperativeOrderResult::Accepted, TEXT("another move order (away)"));
			State.Phase = EPhase::WaitCancel;
			State.PhaseTime = 0.f;
			return true;
		}

		if (State.Phase == EPhase::WaitCancel)
		{
			if (Transfer->HasPendingTransfer() && State.PhaseTime < 3.f)
			{
				return true;
			}
			const FString CancelFeed = LastFeed(World);
			Check(State, !Transfer->HasPendingTransfer() && Leader->MedkitsCount == State.LeaderMedkits && Engineer->MedkitsCount == State.EngineerMedkits
				&& CancelFeed == TEXT("Передача отменена."), TEXT("another order cancels the pending hand-over: ") + CancelFeed);
			Leader->StopOperative();
			State.Phase = EPhase::Ground;
			State.PhaseTime = 0.f;
			return true;
		}

		if (State.Phase == EPhase::Ground)
		{
			// Ground drop near: 30 rounds -> dialog, 15 on the ground.
			Teleport(Engineer, FarSpot(World, Leader, Engineer, FVector(0.f, 1.f, 0.f), 700.f));
			SetReserve(Leader, TEXT("m16"), 30);
			const FVector Point = Leader->GetActorLocation() + FVector(110.f, 0.f, 0.f);
			const ETransferRequestOutcome DropOutcome = Hud->HandleTransferDropOnActor(Leader, ETransferItem::RifleAmmo, nullptr, Point);
			Check(State, DropOutcome == ETransferRequestOutcome::DialogOpened && Dialog->GetAction() == ETransferAction::DropToGround && Dialog->GetMaxQuantity() == 30,
				TEXT("ground drop of 30 rounds: the split dialog (drop)"));
			Dialog->SetQuantity(15);
			const ETransferRequestOutcome Dropped = Dialog->Confirm();
			const FString DropFeed = LastFeed(World);
			ADroppedItemActor* Pile = PileNear(World, Point, 150.f);
			Check(State, Dropped == ETransferRequestOutcome::Transferred && Leader->GetReserve(TEXT("m16")) == 15 && Pile
				&& Pile->GetStash()->GetCount(ETransferItem::RifleAmmo) == 15 && DropFeed.StartsWith(TEXT("Выбросил(а)")),
				TEXT("15 rounds on the ground: commander -15, a pile with 15 (") + DropFeed + TEXT(")"));
			Check(State, Pile && Pile->GetContentsText() == TEXT("Патроны M16 x15"), Pile ? Pile->GetContentsText() : FString(TEXT("no pile")));

			// Another operative picks it up (what arriving at a clicked pile does).
			const int32 EngineerRounds = Engineer->GetReserve(TEXT("m16"));
			Teleport(Engineer, Point + FVector(0.f, 80.f, 0.f));
			if (Pile)
			{
				Pile->HandleDirectInteraction(Engineer);
			}
			Check(State, Engineer->GetReserve(TEXT("m16")) == EngineerRounds + 15 && PileGone(Pile) && LastFeed(World).StartsWith(TEXT("Подобрал(а)")),
				TEXT("the engineer picks the 15 rounds up, the pile is gone: ") + LastFeed(World));

			// Pick-up into a nearly full engineer: one mine stays on the ground (mines lie as items, never armed).
			Teleport(Engineer, FarSpot(World, Leader, Engineer, FVector(0.f, 1.f, 0.f), 700.f));
			const int32 MaxMines = DeployableRules::GetMaxCarried(EDeployableType::Mine);
			Leader->AddDeployable(EDeployableType::Mine, 2 - Leader->GetDeployableCount(EDeployableType::Mine));
			Engineer->AddDeployable(EDeployableType::Mine, MaxMines - 1 - Engineer->GetDeployableCount(EDeployableType::Mine));
			Check(State, Hud->HandleTransferDropOnActor(Leader, ETransferItem::Mine, nullptr, Point) == ETransferRequestOutcome::DialogOpened,
				TEXT("2 mines: dialog"));
			Dialog->SelectAll();
			Dialog->Confirm();
			ADroppedItemActor* TurretPile = PileNear(World, Point, 150.f);
			Check(State, TurretPile && TurretPile->GetStash()->GetCount(ETransferItem::Mine) == 2 && Leader->GetDeployableCount(EDeployableType::Mine) == 0,
				TEXT("2 mines lie on the ground as items"));
			Teleport(Engineer, Point + FVector(0.f, 80.f, 0.f));
			if (TurretPile)
			{
				TurretPile->HandleDirectInteraction(Engineer);
			}
			Check(State, TurretPile && !PileGone(TurretPile) && TurretPile->GetStash()->GetCount(ETransferItem::Mine) == 1
				&& Engineer->GetDeployableCount(EDeployableType::Mine) == MaxMines && LastFeed(World).Contains(TEXT("не поместилось")),
				TEXT("full engineer: 1 taken, 1 stays (") + LastFeed(World) + TEXT(")"));
			Teleport(Engineer, FarSpot(World, Leader, Engineer, FVector(0.f, 1.f, 0.f), 700.f));

			// Save / load keeps the pile (and the crate stashes).
			USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
			if (Saves && TurretPile)
			{
				Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSavesTransfer");
				IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
				const FVector PileAt = TurretPile->GetActorLocation();
				const bool bSaved = Saves->SaveGame(TEXT("transfer_smoke"));
				TurretPile->GetStash()->Clear(); // the pile disappears before the load
				const bool bLoaded = Saves->LoadGame(TEXT("transfer_smoke"));
				ADroppedItemActor* Loaded = PileNear(World, PileAt, 100.f);
				Check(State, bSaved && bLoaded && Loaded && Loaded->GetStash()->GetCount(ETransferItem::Mine) == 1, TEXT("save / load restores the pile"));
				if (Loaded)
				{
					Loaded->HandleDirectInteraction(Leader); // clean up
				}
				IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
				Saves->SaveDirectoryOverride.Reset();
			}
			else
			{
				Check(State, false, TEXT("save subsystem for the pile round trip"));
			}

			// Far, out of combat: walks there, then drops.
			Leader->MedkitsCount = 1;
			State.DropPoint = FarSpot(World, Leader, Leader, FVector(-1.f, 0.f, 0.f), 600.f);
			Check(State, Hud->HandleTransferDropOnActor(Leader, ETransferItem::Medkit, nullptr, State.DropPoint) == ETransferRequestOutcome::Approaching
				&& Transfer->GetPendingAction() == ETransferAction::DropToGround && Leader->MedkitsCount == 1, TEXT("medkit far: the commander walks there first"));
			State.Phase = EPhase::WaitGroundApproach;
			State.PhaseTime = 0.f;
			return true;
		}

		if (State.Phase == EPhase::WaitGroundApproach)
		{
			if (Transfer->HasPendingTransfer() && State.PhaseTime < 25.f)
			{
				return true;
			}
			ADroppedItemActor* MedkitPile = PileNear(World, State.DropPoint, 150.f);
			Check(State, !Transfer->HasPendingTransfer() && Leader->MedkitsCount == 0 && MedkitPile && MedkitPile->GetStash()->GetCount(ETransferItem::Medkit) == 1,
				FString::Printf(TEXT("arrived (%.1f s): the medkit lies at the drop point"), State.PhaseTime));
			if (MedkitPile)
			{
				MedkitPile->HandleDirectInteraction(Leader);
			}
			// Far, blocked (under fire): put down at his feet.
			Leader->MedkitsCount = 1;
			Transfer->bForceUnderFireForTesting = true;
			const ETransferRequestOutcome FeetOutcome = Hud->HandleTransferDropOnActor(Leader, ETransferItem::Medkit, nullptr,
				FarSpot(World, Leader, Leader, FVector(1.f, 0.f, 0.f), 600.f));
			Transfer->bForceUnderFireForTesting = false;
			ADroppedItemActor* FeetPile = PileNear(World, Leader->GetActorLocation(), 150.f);
			Check(State, FeetOutcome == ETransferRequestOutcome::DroppedAtFeet && Leader->MedkitsCount == 0 && FeetPile
				&& FeetPile->GetStash()->GetCount(ETransferItem::Medkit) == 1, TEXT("far under fire: dropped at his feet"));
			if (FeetPile)
			{
				FeetPile->HandleDirectInteraction(Leader);
			}
			State.Phase = EPhase::Crate;
			State.PhaseTime = 0.f;
			return true;
		}

		// Two-way crate.
		ALootCrateActor* Crate = nullptr;
		for (TActorIterator<ALootCrateActor> It(World); It && !Crate; ++It)
		{
			Crate = !It->IsDestroyed() ? *It : nullptr;
		}
		if (!Crate)
		{
			Check(State, false, TEXT("a supply crate on the map"));
			return Finish(State, false);
		}
		Crate->bTrapped = false;
		Teleport(Leader, Crate->GetApproachPoint(Crate->GetActorLocation() + FVector(300.f, 0.f, 0.f)));
		Teleport(Engineer, Crate->GetApproachPoint(Crate->GetActorLocation() - FVector(300.f, 0.f, 0.f)));
		Check(State, Crate->GetDistanceTo(Leader->GetActorLocation()) <= TransferRules::MaxTransferDistance
			&& Crate->GetDistanceTo(Engineer->GetActorLocation()) <= TransferRules::MaxTransferDistance, TEXT("both at the crate"));
		const int32 CrateRounds = Crate->GetStoredCount(ETransferItem::RifleAmmo);
		SetReserve(Leader, TEXT("m16"), 10);
		Check(State, Hud->HandleTransferDropOnActor(Leader, ETransferItem::RifleAmmo, Crate, Crate->GetActorLocation()) == ETransferRequestOutcome::DialogOpened
			&& Dialog->GetAction() == ETransferAction::Store, TEXT("10 rounds on the crate: the split dialog (store)"));
		Dialog->SelectAll();
		const ETransferRequestOutcome Stored = Dialog->Confirm();
		const FString StoreFeed = LastFeed(World);
		Check(State, Stored == ETransferRequestOutcome::Transferred && Leader->GetReserve(TEXT("m16")) == 0
			&& Crate->GetStoredCount(ETransferItem::RifleAmmo) == CrateRounds + 10 && StoreFeed.StartsWith(TEXT("📦 Положил(а) в ящик")),
			TEXT("stored 10: commander -10, crate +10 (") + StoreFeed + TEXT(")"));
		const int32 EngineerRounds = Engineer->GetReserve(TEXT("m16"));
		Check(State, Hud->HandleTakeDrop(Crate, ETransferItem::RifleAmmo, Engineer) == ETransferRequestOutcome::DialogOpened
			&& Dialog->GetAction() == ETransferAction::Take, TEXT("crate line dropped on the engineer: the split dialog (take)"));
		Dialog->SetQuantity(5);
		const ETransferRequestOutcome Taken = Dialog->Confirm();
		const FString TakeFeed = LastFeed(World);
		Check(State, Taken == ETransferRequestOutcome::Transferred && Engineer->GetReserve(TEXT("m16")) == EngineerRounds + 5
			&& Crate->GetStoredCount(ETransferItem::RifleAmmo) == CrateRounds + 5 && TakeFeed.StartsWith(TEXT("📦 Взял(а) из ящика")),
			TEXT("the engineer takes 5: +5, crate -5 (") + TakeFeed + TEXT(")"));
		// Crate nearly full: 3 places left -> 3 go in, then refused.
		UItemStashComponent* CrateStash = Crate->GetSyncedStash();
		const int32 OldCapacity = CrateStash->Capacity;
		CrateStash->Capacity = CrateStash->GetTotal() + 3;
		SetReserve(Leader, TEXT("m16"), 10);
		Check(State, Hud->HandleTransferDropOnActor(Leader, ETransferItem::RifleAmmo, Crate, Crate->GetActorLocation()) == ETransferRequestOutcome::Transferred
			&& Leader->GetReserve(TEXT("m16")) == 7, TEXT("crate with room for 3: 3 stored at once"));
		const ETransferRequestOutcome Full = Hud->HandleTransferDropOnActor(Leader, ETransferItem::RifleAmmo, Crate, Crate->GetActorLocation());
		const FString FullFeed = LastFeed(World);
		Check(State, Full == ETransferRequestOutcome::Failed && Leader->GetReserve(TEXT("m16")) == 7 && FullFeed.Contains(TEXT("нет места")),
			TEXT("full crate: refused (") + FullFeed + TEXT(")"));
		CrateStash->Capacity = OldCapacity;
		Check(State, !Crate->IsLooted(), TEXT("a crate with stored items is not empty"));
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
		TEXT("CodexTactics.TransferSmoke"),
		TEXT("Dev check: Sprint 13 drag & drop hand-over (split dialog, Esc, instant single items, portrait drop, capacity, 2 m range: blocked / walk over / cancel); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
