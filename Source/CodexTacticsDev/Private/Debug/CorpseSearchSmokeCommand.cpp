// Dev-only headless X6 check for Task 130 (search fallen hostile PMC & squad member bodies):
//   Scripts/smoke_parallel.ps1 -Commands CorpseSearchSmoke
// Verifies in L_Gen_Depot_1_X6 sector 2 round:
//  1. Fallen hostile PMC & fallen own squad member both spawn searchable remains (even in sector mode).
//  2. Cursor hover applies M_TargetFresnel overlay to RemainsBody and clears it on unhover.
//  3. Clicking hostile PMC remains walks the leader to the body, plays short search, grants 20-60% M16 mag ammo.
//  4. Second search attempt on the same hostile body gives nothing and does not reopen the loot window.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Training/TrainingSectorSubsystem.h"
#include "UI/OverheadLabel.h"

namespace CorpseSearchSmoke
{
	enum class EStage : uint8
	{
		WaitRound,
		WaitRemains,
		VerifyHoverOn,
		VerifyHoverOff,
		ClickHostileBody,
		WaitLootDialog,
		WaitLootAll,
		VerifySecondSearch
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Preload;
		EStage Stage = EStage::WaitRound;
		bool bSectorApplied = false;
		double StageAt = 0.0;
		double StartAt = 0.0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> FallenMate;
		TWeakObjectPtr<AOperativeCharacter> FallenPmc;
		TWeakObjectPtr<ALootCrateActor> HostileRemains;
		TWeakObjectPtr<ALootCrateActor> SquadRemains;
		int32 ReserveBefore = 0;
		int32 ReserveAfterFirst = 0;
		FHitResult BodyHit;
	};

	bool Finish(bool bPass, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bPass ? TEXT("ok  ") : TEXT("FAIL"), *What);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CorpseSearchSmoke"));
		return false;
	}

	void KillOperative(AOperativeCharacter* Op)
	{
		if (Op && Op->HealthComponent && Op->HealthComponent->IsAlive())
		{
			FDamageSpec Spec;
			Spec.Amount = 250000.f;
			Spec.ArmorPenetration = 1.f;
			Spec.AttackerSource = TEXT("CorpseSearchSmoke");
			Op->HealthComponent->TakeDamage(Spec);
		}
	}

	ALootCrateActor* FindRemainsForBody(UWorld* World, const USkeletalMeshComponent* Body, bool bHostile)
	{
		for (TActorIterator<ALootCrateActor> It(World); It; ++It)
		{
			ALootCrateActor* Crate = *It;
			if (!Crate || !Crate->IsRemains() || Crate->IsHostileRemains() != bHostile)
			{
				continue;
			}
			// Match by closest remains crate to the body's location / name.
			if (!Body || Crate->GetRemainsOwnerName().ToString() == Cast<AOperativeCharacter>(Body->GetOwner())->DisplayName.ToString())
			{
				return Crate;
			}
		}
		return nullptr;
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			It->bTacticalCeaseFire = true;
		}
		switch (State->Preload.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed: return Finish(false, TEXT("gameplay preload failed"));
		default: break;
		}
		const double Now = World->GetTimeSeconds();
		if (Now - State->StartAt > 120.0)
		{
			return Finish(false, FString::Printf(TEXT("timeout at stage %d"), static_cast<int32>(State->Stage)));
		}

		UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Training || !Squad || !Interactions || !PC)
		{
			return Finish(false, TEXT("missing required subsystems or PlayerController"));
		}
		if (!State->bSectorApplied)
		{
			State->bSectorApplied = true;
			if (!Training->SelectSector(2, 1))
			{
				return Finish(false, TEXT("failed to select sector 2 seed 1"));
			}
			State->StageAt = Now;
		}

		auto Enter = [&State, Now](EStage Next)
		{
			State->Stage = Next;
			State->StageAt = Now;
		};

		switch (State->Stage)
		{
		case EStage::WaitRound:
		{
			if (Training->GetRoundView().Phase != ESectorRoundPhase::Fighting)
			{
				return Now - State->StageAt < 30.0 || Finish(false, TEXT("sector round never entered Fighting"));
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			AOperativeCharacter* Mate = nullptr;
			AOperativeCharacter* TargetPmc = nullptr;
			TArray<AOperativeCharacter*> OtherPmcs;
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				AOperativeCharacter* Op = *It;
				if (!Op || Op->IsActorBeingDestroyed() || Op->IsKilledInAction())
				{
					continue;
				}
				if (Op->IsPlayerTeam())
				{
					if (Op != Leader && !Mate && Op->ReserveAmmo > 0)
					{
						Mate = Op;
					}
				}
				else
				{
					if (!TargetPmc && Op->AvailableWeapons.ContainsByPredicate([](const UWeaponDataAsset* W) { return W && W->WeaponId == TEXT("m16"); }))
					{
						TargetPmc = Op;
					}
					else
					{
						OtherPmcs.Add(Op);
					}
				}
			}
			if (!Leader || !Mate || !TargetPmc || OtherPmcs.IsEmpty())
			{
				return Now - State->StageAt < 30.0 || Finish(false, TEXT("missing Leader/Mate/M16 PMC/anchor PMC"));
			}
			// Keep at least one hostile PMC parked away so the sector round stays Fighting during search.
			for (AOperativeCharacter* Anchor : OtherPmcs)
			{
				Anchor->SetActorLocation(FVector(Anchor->GetActorLocation().X, Anchor->GetActorLocation().Y, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
				Anchor->SetActorTickEnabled(false);
			}
			// Move TargetPmc close in front of Leader so walk-to-body search finishes promptly on valid ground.
			const FVector DropPos = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 220.f;
			TargetPmc->SetActorLocation(DropPos, false, nullptr, ETeleportType::TeleportPhysics);
			State->Leader = Leader;
			State->FallenMate = Mate;
			State->FallenPmc = TargetPmc;
			KillOperative(TargetPmc);
			KillOperative(Mate);
			Enter(EStage::WaitRemains);
			return true;
		}
		case EStage::WaitRemains:
		{
			if (Now - State->StageAt < 0.7)
			{
				return true;
			}
			AOperativeCharacter* FallenPmc = State->FallenPmc.Get();
			AOperativeCharacter* FallenMate = State->FallenMate.Get();
			ALootCrateActor* HostileRemains = FallenPmc ? FindRemainsForBody(World, FallenPmc->GetMesh(), true) : nullptr;
			ALootCrateActor* SquadRemains = FallenMate ? FindRemainsForBody(World, FallenMate->GetMesh(), false) : nullptr;
			if (!HostileRemains || !SquadRemains)
			{
				return Finish(false, FString::Printf(TEXT("missing remains actors (hostile=%d, squad=%d)"),
					HostileRemains != nullptr ? 1 : 0, SquadRemains != nullptr ? 1 : 0));
			}
			if (HostileRemains->IsLooted() || HostileRemains->IsRemainsSearched())
			{
				return Finish(false, TEXT("fresh hostile remains unexpectedly marked looted/searched"));
			}
			if (SquadRemains->GetItems().IsEmpty())
			{
				return Finish(false, TEXT("fallen squad member in sector round has empty remains"));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke ok  : spawned hostile remains '%s' and squad remains '%s' (%d carried stacks)"),
				*HostileRemains->GetName(), *SquadRemains->GetName(), SquadRemains->GetItems().Num());
			State->HostileRemains = HostileRemains;
			State->SquadRemains = SquadRemains;
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CorpseSearchSmoke), false);
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				TraceParams.AddIgnoredActor(*It);
			}
			const FVector Center = HostileRemains->GetActorLocation();
			if (!World->LineTraceSingleByChannel(State->BodyHit, Center + FVector(0.f, 0.f, 200.f), Center - FVector(0.f, 0.f, 200.f), ECC_Visibility, TraceParams)
				|| State->BodyHit.GetActor() != HostileRemains)
			{
				State->BodyHit = FHitResult(HostileRemains, HostileRemains->Box, Center, FVector::UpVector);
				State->BodyHit.bBlockingHit = true;
				State->BodyHit.Location = Center;
				State->BodyHit.ImpactPoint = Center;
			}
			PC->bSimulateCursorForTesting = true;
			PC->SimulatedCursorHitForTesting = State->BodyHit;
			Enter(EStage::VerifyHoverOn);
			return true;
		}
		case EStage::VerifyHoverOn:
		{
			if (Now - State->StageAt < 0.15)
			{
				return true;
			}
			ALootCrateActor* HostileRemains = State->HostileRemains.Get();
			const AOperativeCharacter* FallenPmc = State->FallenPmc.Get();
			UMaterialInterface* Overlay = (FallenPmc && FallenPmc->GetMesh()) ? FallenPmc->GetMesh()->GetOverlayMaterial() : nullptr;
			const bool bFresnel = Overlay && Overlay->GetMaterial() && Overlay->GetMaterial()->GetName() == TEXT("M_TargetFresnel");
			if (!HostileRemains || !HostileRemains->IsRemainsHovered() || !bFresnel)
			{
				return Finish(false, FString::Printf(TEXT("hover Fresnel missing (hovered=%d, overlay=%s)"),
					HostileRemains && HostileRemains->IsRemainsHovered() ? 1 : 0, Overlay ? *Overlay->GetName() : TEXT("none")));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke ok  : M_TargetFresnel hover overlay active on hostile RemainsBody"));
			PC->SimulatedCursorHitForTesting = FHitResult();
			Enter(EStage::VerifyHoverOff);
			return true;
		}
		case EStage::VerifyHoverOff:
		{
			if (Now - State->StageAt < 0.15)
			{
				return true;
			}
			PC->bSimulateCursorForTesting = false;
			ALootCrateActor* HostileRemains = State->HostileRemains.Get();
			const AOperativeCharacter* FallenPmc = State->FallenPmc.Get();
			UMaterialInterface* Overlay = (FallenPmc && FallenPmc->GetMesh()) ? FallenPmc->GetMesh()->GetOverlayMaterial() : nullptr;
			const bool bStillFresnel = Overlay && Overlay->GetMaterial() && Overlay->GetMaterial()->GetName() == TEXT("M_TargetFresnel");
			if (!HostileRemains || HostileRemains->IsRemainsHovered() || bStillFresnel)
			{
				return Finish(false, TEXT("hover Fresnel failed to clear on unhover"));
			}
			Enter(EStage::ClickHostileBody);
			return true;
		}
		case EStage::ClickHostileBody:
		{
			AOperativeCharacter* Leader = State->Leader.Get();
			ALootCrateActor* HostileRemains = State->HostileRemains.Get();
			if (!Leader || !HostileRemains)
			{
				return Finish(false, TEXT("missing Leader or HostileRemains before click"));
			}
			State->ReserveBefore = Leader->GetReserve(TEXT("m16"));
			PC->HandleWorldHit(State->BodyHit);
			if (Interactions->GetPendingInteraction() != HostileRemains && Interactions->GetLootCrate() != HostileRemains
				&& HostileRemains->GetSessionState() == ELootSessionState::Closed)
			{
				return Finish(false, TEXT("click on hostile body did not trigger walk/search interaction"));
			}
			Enter(EStage::WaitLootDialog);
			return true;
		}
		case EStage::WaitLootDialog:
		{
			ALootCrateActor* HostileRemains = State->HostileRemains.Get();
			if (Interactions->GetLootCrate() != HostileRemains)
			{
				return Now - State->StageAt < 25.0 || Finish(false, TEXT("loot dialog did not open after walking to hostile body"));
			}
			Interactions->LootAll();
			Enter(EStage::WaitLootAll);
			return true;
		}
		case EStage::WaitLootAll:
		{
			ALootCrateActor* HostileRemains = State->HostileRemains.Get();
			AOperativeCharacter* Leader = State->Leader.Get();
			if (!HostileRemains->IsLooted() || Interactions->GetLootCrate() != nullptr)
			{
				return Now - State->StageAt < 10.0 || Finish(false, TEXT("LootAll did not complete on hostile remains"));
			}
			const int32 Gained = Leader->GetReserve(TEXT("m16")) - State->ReserveBefore;
			if (!HostileRemains->IsRemainsSearched() || Gained < 6 || Gained > 18)
			{
				return Finish(false, FString::Printf(TEXT("unexpected first search result (searched=%d, m16_gained=%d)"),
					HostileRemains->IsRemainsSearched() ? 1 : 0, Gained));
			}
			State->ReserveAfterFirst = Leader->GetReserve(TEXT("m16"));
			FOverheadLabel Label;
			if (!HostileRemains->GetOverheadLabel(Label) || !Label.Text.Contains(TEXT("[Searched]")))
			{
				return Finish(false, TEXT("overhead prompt did not update to [Searched]"));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke ok  : first search granted +%d M16 rounds and marked searched"), Gained);
			if (HostileRemains->HandleDirectInteraction(Leader))
			{
				return Finish(false, TEXT("second direct interaction unexpectedly succeeded"));
			}
			PC->HandleWorldHit(State->BodyHit);
			Enter(EStage::VerifySecondSearch);
			return true;
		}
		case EStage::VerifySecondSearch:
		{
			if (Now - State->StageAt < 1.0)
			{
				return true;
			}
			AOperativeCharacter* Leader = State->Leader.Get();
			if (Interactions->GetLootCrate() != nullptr || Leader->GetReserve(TEXT("m16")) != State->ReserveAfterFirst)
			{
				return Finish(false, TEXT("second search opened loot or granted extra ammo"));
			}
			return Finish(true, TEXT("hostile + squad corpse search, Fresnel hover, and once-only loot passed on X6"));
		}
		default:
			return Finish(false, TEXT("unknown stage"));
		}
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->StartAt = World->GetTimeSeconds();
		State->StageAt = State->StartAt;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), 0.f);
	}

	FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.CorpseSearchSmoke"),
		TEXT("Task 130 X6 check: fallen PMC & squad remains spawn in sector rounds, light Fresnel hover works, walk-to-body search loots once."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
