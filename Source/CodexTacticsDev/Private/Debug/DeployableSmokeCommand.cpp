// Dev-only console command for a headless deployables check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
// 1. pick up an abandoned barricade (goes to the engineer), 2. F: the engineer hands it to the commander, who sets it
// up (two clicks), 3. a hidden mine next to the squad is spotted, 4. the medic-sapper defuses it lying down and
// takes it, 5. a barrel is trapped with a grenade, 6. a mine blast hurts an operative nearby.
// 2026-10-15 (game designer): the medic-sapper walks right up to the mine (<= 0.9 m from the charge, not on it, facing it), the mine
// ignores her while she stands inside its trigger radius (a cancelled menu included), and a standing defuser plays the pickup clip
// on the full body after the success.
// Rev 2026-10-19 (game designer: "the mine disappeared first, then he bent down"): after the success the mine STAYS (harmless, no
// menu) while the pickup clip plays and goes only at its grab moment (~0.44 s into the standing clip that starts low); the kit and
// the feed lines ("disarmed the mine", "picked up") come at the grab, not before.
// 2026-10-22 (game designer: "a failed defuse with the warning - clicking the mine again does not select it, I have to walk away and come
// back"): her first attempt (luck 0, 80 % cold: dangerous) only WARNS; then a click on her own body over the mine (the cursor on the
// mine lands on her) and a click on the mine itself reopen the Defuse menu at once - she does not move, she stays the leader - and the
// confirmed second attempt disarms it.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace DeployableSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 70.f;

	enum class EPhase : uint8 { PickUpMenu, PickUpWait, DeployWait, MineSpot, DefuseMenu, RetryWarn, DefuseWait, Trap, Blast, Done };

	struct FState
	{
		EPhase Phase = EPhase::PickUpMenu;
		float Time = 0.f;
		float PhaseTime = 0.f;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<AProximityMineActor> Mine;
		FVector DeploySpot = FVector::ZeroVector;
		bool bPickUp = false;
		bool bDeploy = false;
		bool bSpotted = false;
		bool bDefused = false;
		bool bTrap = false;
		bool bBlast = false;
		int32 DefuseTries = 0;
		/** 2026-10-15: walk-up distance / facing, the defuser lease through a cancelled menu, the standing pickup clip. */
		bool bApproach = false;
		bool bLease = false;
		bool bPickup = false;
		bool bCancelled = false;
		/** Rev 2026-10-19: the disarmed mine was seen waiting for the grab (with the clip playing, nothing in the kit yet), when / how long. */
		bool bSawAwaiting = false;
		bool bGrabOrder = true;
		float AwaitingSeenAt = -1.f;
		float PickupDelay = 0.f;
		int32 MinesBefore = 0;
		int32 FeedBefore = 0;
		/** 2026-10-22: the warning attempt happened and the clicks right at the mine reopened its menu. */
		bool bRetryTried = false;
		bool bRetry = false;
	};

	/** A feed line containing Needle was posted after the first Since lines. */
	bool SmokeFeedSince(UWorld* World, int32 Since, const TCHAR* Needle)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		const TArray<FGameMessage>& History = Messages ? Messages->GetHistory() : TArray<FGameMessage>();
		for (int32 Index = FMath::Max(0, Since); Index < History.Num(); ++Index)
		{
			if (History[Index].Text.ToString().Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}

	AOperativeCharacter* FindRole(UWorld* World, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	template <typename T>
	T* SpawnAt(UWorld* World, const FVector& Ground)
	{
		const float HalfHeight = GetDefault<T>()->Box->GetUnscaledBoxExtent().Z;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Ground + FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator, Params);
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke pickUp=%d deploy=%d spotted=%d defused=%d trap=%d blast=%d approach=%d lease=%d pickupClip=%d retry=%d"), State.bPickUp ? 1 : 0,
			State.bDeploy ? 1 : 0, State.bSpotted ? 1 : 0, State.bDefused ? 1 : 0, State.bTrap ? 1 : 0, State.bBlast ? 1 : 0, State.bApproach ? 1 : 0,
			State.bLease ? 1 : 0, State.bPickup ? 1 : 0, State.bRetry ? 1 : 0);
		const bool bPass = State.bPickUp && State.bDeploy && State.bSpotted && State.bDefused && State.bTrap && State.bBlast && State.bApproach
			&& State.bLease && State.bPickup && State.bRetry;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("DeployableSmoke"));
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Commander || !Engineer || !Medic || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d at %.1fs"), static_cast<int32>(State.Phase), State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::PickUpMenu:
			if (Interactions->IsActionMenuOpen())
			{
				const FActionMenuSpec& Menu = Interactions->GetActionMenu();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu \"%s\" / \"%s\" trap=\"%s\""), *Menu.Title.ToString(),
					*Menu.ConfirmText.ToString(), *Menu.TrapText.ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::PickUpWait);
			}
			return true;

		case EPhase::PickUpWait:
			if (State.PhaseTime >= 2.f)
			{
				State.bPickUp = !State.Barricade.IsValid() && Engineer->BarricadesCount == 1;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke picked up: barricade gone=%d engineer=%d commander=%d"),
					State.Barricade.IsValid() ? 0 : 1, Engineer->BarricadesCount, Commander->BarricadesCount);
				// F with an empty commander: the engineer hands the barricade over; two clicks set it up.
				Relocation->HandleDeployKey(Commander);
				State.DeploySpot = Feet(Commander) + Commander->GetActorForwardVector() * 500.f + Commander->GetActorRightVector() * 300.f;
				Relocation->UpdatePreview(State.DeploySpot);
				Relocation->ConfirmPlacement(State.DeploySpot);
				Relocation->RotatePreview(+2);
				Relocation->ConfirmPlacement(State.DeploySpot);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke deploy ordered: commander=%d engineer=%d tasks=%d"), Commander->BarricadesCount,
					Engineer->BarricadesCount, Relocation->GetActiveDeployCount());
				Enter(State, EPhase::DeployWait);
			}
			return true;

		case EPhase::DeployWait:
			if (Relocation->GetActiveDeployCount() == 0)
			{
				ABarricadeActor* Placed = nullptr;
				for (TActorIterator<ABarricadeActor> It(World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), State.DeploySpot) < 30.f)
					{
						Placed = *It;
					}
				}
				State.bDeploy = Placed && Commander->BarricadesCount == 0 && FMath::IsNearlyEqual(FMath::Abs(Placed->GetActorRotation().Yaw), 90.f, 1.f);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke deployed at %.1fs: found=%d yaw=%.0f commander=%d"), State.Time, Placed ? 1 : 0,
					Placed ? Placed->GetActorRotation().Yaw : 0.f, Commander->BarricadesCount);
				// A hidden mine 3 m to the medic's side, the rest of the squad holds position.
				Squad->SetLeader(Medic);
				Squad->EnterSoloMode();
				State.Mine = SpawnAt<AProximityMineActor>(World, Feet(Medic) + Medic->GetActorRightVector() * 380.f);
				Enter(State, EPhase::MineSpot);
			}
			return true;

		case EPhase::MineSpot:
			if (State.Mine.IsValid() && State.Mine->IsRevealed())
			{
				State.bSpotted = true;
				Interactions->RequestInteraction(State.Mine.Get());
				Enter(State, EPhase::DefuseMenu);
			}
			else if (State.PhaseTime > 3.f)
			{
				Enter(State, EPhase::DefuseMenu);
			}
			return true;

		case EPhase::DefuseMenu:
			if (Interactions->IsActionMenuOpen() && State.Mine.IsValid() && !State.bCancelled)
			{
				// 2026-10-15: she stands right at the mine (<= 0.9 m from the charge, not on it), facing it; a cancel leaves her there.
				const FVector ToMine = (State.Mine->GetActorLocation() - Medic->GetActorLocation()).GetSafeNormal2D();
				const float Distance = static_cast<float>(FVector::Dist2D(Medic->GetActorLocation(), State.Mine->GetActorLocation()));
				const float FacingDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Medic->GetActorForwardVector().GetSafeNormal2D(), ToMine), -1.0, 1.0)));
				State.bApproach = Distance <= 90.f && Distance >= 30.f && FacingDeg <= 30.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine walk-up: %.0f cm from the charge (trigger %.0f), facing %.0f deg: %s"), Distance,
					State.Mine->TriggerRadius, FacingDeg, State.bApproach ? TEXT("ok") : TEXT("WRONG"));
				Interactions->CancelActionMenu();
				State.bCancelled = true;
				State.PhaseTime = 0.f;
				return true;
			}
			if (State.bCancelled && !State.bLease && State.PhaseTime >= 1.f)
			{
				// Inside the trigger radius with the menu closed: the lease keeps the mine quiet.
				State.bLease = State.Mine.IsValid() && State.Mine->GetDefuserLease() == Medic;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu cancelled inside the trigger radius: mine %s, lease %d"), State.Mine.IsValid() ? TEXT("quiet") : TEXT("BLEW UP"),
					State.Mine.IsValid() && State.Mine->GetDefuserLease() == Medic ? 1 : 0);
				if (State.Mine.IsValid())
				{
					Interactions->RequestInteraction(State.Mine.Get());
				}
				return true;
			}
			if (Interactions->IsActionMenuOpen() && State.bLease && !State.bRetryTried)
			{
				// 2026-10-22: the first attempt with numb fingers and no luck - dangerous: only the warning.
				State.bRetryTried = true;
				Medic->Luck = 0.f;
				Medic->ColdLevel = 80.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine: a dangerous first attempt (luck 0, 80 %% cold): %s"), *State.Mine->DescribeDefusal(Medic).ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::RetryWarn);
				return true;
			}
			if (Interactions->IsActionMenuOpen() && State.bLease)
			{
				// Dismantling crouches the defuser (Godot): luck 100 keeps the odds at 99 %, failures are retried. She stays STANDING
				// before the work (2026-10-15: the pickup after the success is then the full-body clip, rising from the crouch).
				Medic->Luck = 100.f;
				++State.DefuseTries;
				State.MinesBefore = Medic->MinesCount;
				State.FeedBefore = World->GetSubsystem<UGameMessageSubsystem>()->GetHistory().Num();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine menu \"%s\" / \"%s\""), *Interactions->GetActionMenu().Title.ToString(),
					*Interactions->GetActionMenu().ConfirmText.ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::DefuseWait);
			}
			else if (!State.Mine.IsValid())
			{
				Enter(State, EPhase::Trap); // blew up on the way
			}
			return true;

		case EPhase::RetryWarn:
		{
			if (State.PhaseTime < 2.f)
			{
				return true; // the 1.1 s crouched work
			}
			AProximityMineActor* Mine = State.Mine.Get();
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			const bool bWarned = Mine && Mine->bDefusalWarned && Mine->bTrapped && Medic->HealthComponent->GetCurrentHealth() >= Medic->HealthComponent->GetMaxHealth();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine retry: the first attempt only warned=%d (mine %s)"), bWarned ? 1 : 0, Mine ? TEXT("there") : TEXT("GONE"));
			if (!Mine || !PC)
			{
				Enter(State, EPhase::Trap);
				return true;
			}
			// Warm and lucky again for the second attempt; a fixed good roll (the warned mine has no second warning).
			Medic->ColdLevel = 0.f;
			Medic->Luck = 100.f;
			Mine->DefusalRollForTesting = 0.f;
			const FVector Before = Medic->GetActorLocation();
			// The cursor on the mine right in front of her lands on her own body.
			FHitResult BodyHit(Medic, Medic->GetCapsuleComponent(), Mine->GetActorLocation(), FVector::UpVector);
			BodyHit.bBlockingHit = true;
			PC->HandleWorldHit(BodyHit);
			const bool bBodyClick = Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == Mine && Squad->GetLeader() == Medic;
			Interactions->CancelActionMenu();
			// The mine itself under the cursor (it used to pick her by the 1.2 m select radius).
			FHitResult MineHit(Mine, Mine->Box.Get(), Mine->GetActorLocation(), FVector::UpVector);
			MineHit.bBlockingHit = true;
			PC->HandleWorldHit(MineHit);
			const bool bMineClick = Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == Mine;
			const float Moved = static_cast<float>(FVector::Dist2D(Medic->GetActorLocation(), Before));
			State.bRetry = bWarned && bBodyClick && bMineClick && Moved < 20.f && State.Mine.IsValid();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine retry: body click reopens the menu=%d, mine click=%d, moved %.0f cm, still the leader=%d: %s"), bBodyClick ? 1 : 0,
				bMineClick ? 1 : 0, Moved, Squad->GetLeader() == Medic ? 1 : 0, State.bRetry ? TEXT("ok") : TEXT("WRONG"));
			Enter(State, EPhase::DefuseMenu); // the menu is open: the second attempt (the usual path)
			return true;
		}

		case EPhase::DefuseWait:
			if (State.Mine.IsValid() && State.Mine->IsAwaitingPickup())
			{
				// Disarmed, being picked up: still there, nothing in the kit, the clip already playing, no "disarmed" line yet.
				const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Medic);
				const bool bClip = Anim && Anim->GetInteractionClipLog().Contains(TEXT("PickupGround@FullBody"));
				const bool bQuiet = !SmokeFeedSince(World, State.FeedBefore, TEXT("disarmed the mine"));
				if (!State.bSawAwaiting)
				{
					State.bSawAwaiting = true;
					State.AwaitingSeenAt = State.Time;
					State.PickupDelay = State.Mine->GetLastPickupDelaySeconds();
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine disarmed, waiting for the grab (%.2f s): clip=%d kit unchanged=%d feed quiet=%d"),
						State.PickupDelay, bClip ? 1 : 0, Medic->MinesCount == State.MinesBefore ? 1 : 0, bQuiet ? 1 : 0);
				}
				if (!bClip || Medic->MinesCount != State.MinesBefore || !bQuiet || State.Mine->IsHidden())
				{
					State.bGrabOrder = false;
				}
				return true;
			}
			if (State.PhaseTime >= 2.f && State.Mine.IsValid() && State.DefuseTries < 4)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defusal attempt %d did not clear the mine, retrying"), State.DefuseTries);
				Interactions->RequestInteraction(State.Mine.Get());
				Enter(State, EPhase::DefuseMenu);
			}
			else if (State.PhaseTime >= 2.f)
			{
				// The grab came after the clip's effect moment, then the kit and the feed.
				const float Held = State.bSawAwaiting ? State.Time - State.AwaitingSeenAt : -1.f;
				State.bGrabOrder = State.bGrabOrder && State.bSawAwaiting && State.PickupDelay > 0.2f && Held >= State.PickupDelay - StepSeconds - 0.05f
					&& SmokeFeedSince(World, State.FeedBefore, TEXT("disarmed the mine"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine stayed %.2f s after the disarm (grab at %.2f s), feed after the grab=%d: %s"), Held,
					State.PickupDelay, SmokeFeedSince(World, State.FeedBefore, TEXT("disarmed the mine")) ? 1 : 0, State.bGrabOrder ? TEXT("ok") : TEXT("WRONG"));
				State.bDefused = !State.Mine.IsValid() && Medic->MinesCount == State.MinesBefore + 1
					&& Medic->HealthComponent->GetCurrentHealth() >= Medic->HealthComponent->GetMaxHealth() && State.bGrabOrder;
				const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Medic);
				State.bPickup = Anim && Anim->GetInteractionClipLog().Contains(TEXT("PickupGround@FullBody")) && Medic->GetStance() == EOperativeStance::Standing;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke pickup after the mine: stance %d, clips %s"), static_cast<int32>(Medic->GetStance()),
					Anim ? *FString::Join(Anim->GetInteractionClipLog(), TEXT(", ")) : TEXT("-"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defused: mine gone=%d medic mines=%d hp=%.0f"), State.Mine.IsValid() ? 0 : 1,
					Medic->MinesCount, Medic->HealthComponent->GetCurrentHealth());
				Enter(State, EPhase::Trap);
			}
			return true;

		case EPhase::Trap:
		{
			ABarrelActor* Barrel = SpawnAt<ABarrelActor>(World, Feet(Medic) - Medic->GetActorForwardVector() * 250.f);
			const int32 Grenades = Medic->GrenadesCount;
			State.bTrap = Barrel && Barrel->CanReceiveTrap() && Barrel->TrapWithGrenade(Medic) && Barrel->bTrapped
				&& Medic->GrenadesCount == Grenades - 1 && !Barrel->CanReceiveTrap();
			// A level mine going off 2.5 m from the engineer.
			const float Before = Engineer->HealthComponent->GetCurrentHealth();
			AProximityMineActor* Blast = SpawnAt<AProximityMineActor>(World, Feet(Engineer) + Engineer->GetActorRightVector() * 250.f);
			if (Blast)
			{
				Blast->Detonate();
			}
			const float Lost = Before - Engineer->HealthComponent->GetCurrentHealth();
			// 120 * 0.75 * (1 - 250 / 350 * 0.5) = 57.9
			State.bBlast = FMath::IsNearlyEqual(Lost, 57.86f, 1.5f);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trap=%d grenades %d -> %d, blast damage %.1f"), State.bTrap ? 1 : 0, Grenades,
				Medic->GrenadesCount, Lost);
			Finish(State);
			return false;
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		if (!Commander)
		{
			FState Failed;
			Finish(Failed);
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Barricade = SpawnAt<ABarricadeActor>(World, Feet(Commander) + Commander->GetActorForwardVector() * 450.f);
		World->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(State->Barricade.Get());

		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && !Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), StepSeconds, true);
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
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DeployableSmoke"),
		TEXT("Dev check: barricade pick-up and set-up, mine spotting and defusal, grenade trap, mine blast; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
