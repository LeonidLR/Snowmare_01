// Dev-only console command for a headless supply crate check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.LootSmoke
// 1. an intact crate opens without a menu and shows the loot dialog; one stack, then «Забрать ВСЁ» go to the leader;
// 2. the empty crate only posts the Godot line; 3. a trapped crate (medic-sapper, prone) is defused, opened and its
// turret / barricades / mines are taken; 4. a trapped crate that blows up loses its contents.
// Interaction clips (game designer 2026-10-15): the medic-sapper, crouched after the defusal, opens / searches on the upper body.
// Rev 2026-10-19 (game designer play-test: the animation FIRST, then the loot): the standing commander's chest-open clip plays on the
// full body and the window opens only at its lid-open moment (~2.7 s after the clip started; while it plays no take is possible);
// Take (one stack) and Take All each play the chest-search clip and the items move only at its grab moment (~1.6 s), the window
// shows "Searching..." meanwhile (a second take is refused), Take All closes it after the grab.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ItemStashComponent.h"
#include "TimerManager.h"

namespace LootSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 60.f;

	enum class EPhase : uint8 { OpenCrate, SearchOne, SearchAll, EmptyClick, TrappedMenu, TrappedOpen, TrappedSearch, Blast, Done };

	struct FState
	{
		EPhase Phase = EPhase::OpenCrate;
		float Time = 0.f;
		float PhaseTime = 0.f;
		TWeakObjectPtr<ALootCrateActor> Crate;
		TWeakObjectPtr<ALootCrateActor> Trapped;
		int32 MedkitsBefore = 0;
		int32 AmmoBefore = 0;
		bool bOpen = false;
		bool bTake = false;
		bool bEmpty = false;
		bool bDefuse = false;
		bool bBlast = false;
		int32 DefuseTries = 0;
		/** Interaction clips: game time the chest-open clip was first seen, the checks. */
		float ChestOpenSeenAt = -1.f;
		bool bOpenClip = false;
		bool bSearchClip = false;
		bool bCrouchClips = false;
		/** Rev 2026-10-19: the window / a take came before its clip moment (any sample). */
		bool bEffectFirst = false;
		/** Game time a search was asked for. */
		float SearchAskedAt = -1.f;
	};

	bool HasClip(const AOperativeCharacter* Operative, const TCHAR* Entry)
	{
		const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Operative);
		return Anim && Anim->GetInteractionClipLog().Contains(FString(Entry));
	}

	int32 CountClip(const AOperativeCharacter* Operative, const TCHAR* Entry)
	{
		const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Operative);
		return Anim ? Anim->GetInteractionClipLog().FilterByPredicate([Entry](const FString& Each) { return Each == Entry; }).Num() : 0;
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

	ALootCrateActor* SpawnCrate(UWorld* World, const FVector& Ground, bool bTrapped)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALootCrateActor* Crate = World->SpawnActor<ALootCrateActor>(Ground + FVector(0.f, 0.f, 40.f), FRotator::ZeroRotator, Params);
		if (Crate && bTrapped)
		{
			Crate->CrateName = FText::FromString(TEXT("📦 Booby-trapped outpost crate"));
			Crate->bTrapped = true;
			Crate->Contents.Turrets = 1;
			Crate->Contents.Barricades = 2;
			Crate->Contents.Mines = 2;
		}
		return Crate;
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke open=%d take=%d empty=%d defuse=%d blast=%d openClip=%d searchClip=%d crouchClips=%d effectFirst=%d"),
			State.bOpen ? 1 : 0, State.bTake ? 1 : 0, State.bEmpty ? 1 : 0, State.bDefuse ? 1 : 0, State.bBlast ? 1 : 0,
			State.bOpenClip ? 1 : 0, State.bSearchClip ? 1 : 0, State.bCrouchClips ? 1 : 0, State.bEffectFirst ? 1 : 0);
		const bool bPass = State.bOpen && State.bTake && State.bEmpty && State.bDefuse && State.bBlast && State.bOpenClip && State.bSearchClip
			&& State.bCrouchClips && !State.bEffectFirst;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("LootSmoke"));
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Commander || !Medic || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d at %.1fs"), static_cast<int32>(State.Phase), State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::OpenCrate:
			if (Interactions->IsActionMenuOpen())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke unexpected menu on an intact crate"));
				Interactions->CancelActionMenu();
			}
			if (State.ChestOpenSeenAt < 0.f && HasClip(Commander, TEXT("ChestOpen@FullBody")))
			{
				State.ChestOpenSeenAt = State.Time;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke chest-open clip started at %.2f s, window open=%d"), State.Time,
					Interactions->GetLootCrate() ? 1 : 0);
			}
			if (State.Crate.IsValid() && State.Crate->GetSessionState() == ELootSessionState::Opening)
			{
				// While the lid opens: no window, and nothing can be taken.
				const bool bTookEarly = State.Crate->RequestSearch(Commander, ELootItem::Medkit, false);
				if (Interactions->GetLootCrate() || bTookEarly || State.ChestOpenSeenAt < 0.f)
				{
					State.bEffectFirst = true;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke WRONG: during the opening window=%d take=%d clip=%d"), Interactions->GetLootCrate() ? 1 : 0,
						bTookEarly ? 1 : 0, State.ChestOpenSeenAt >= 0.f ? 1 : 0);
				}
			}
			if (Interactions->GetLootCrate() == State.Crate.Get() && State.Crate.IsValid())
			{
				State.bOpen = true;
				// The clip came first and the window waited for its lid-open moment.
				const float Waited = State.ChestOpenSeenAt >= 0.f ? State.Time - State.ChestOpenSeenAt : -1.f;
				State.bOpenClip = State.ChestOpenSeenAt >= 0.f && State.Crate->GetLastOpenDelaySeconds() >= 2.f
					&& Waited >= State.Crate->GetLastOpenDelaySeconds() - StepSeconds - 0.05f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke window opened %.2f s after the chest-open clip (delay %.2f s): %s"), Waited,
					State.Crate->GetLastOpenDelaySeconds(), State.bOpenClip ? TEXT("ok") : TEXT("WRONG"));
				// Take one stack: the search clip starts, the stack stays in the crate until the grab.
				Interactions->LootItem(ELootItem::Medkit);
				State.SearchAskedAt = State.Time;
				const bool bSearching = State.Crate->IsSearching() && HasClip(Commander, TEXT("ChestSearch@FullBody"));
				const bool bNotYet = Commander->MedkitsCount == State.MedkitsBefore && State.Crate->GetStoredCount(ETransferItem::Medkit) == 2;
				Interactions->LootAll(); // refused while searching
				const bool bRefused = State.Crate->IsSearching() && Interactions->GetLootCrate() == State.Crate.Get()
					&& Commander->ReserveAmmo == State.AmmoBefore;
				if (!bSearching || !bNotYet || !bRefused)
				{
					State.bEffectFirst = true;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke take asked: searching=%d items not moved yet=%d second take refused=%d"), bSearching ? 1 : 0,
					bNotYet ? 1 : 0, bRefused ? 1 : 0);
				Enter(State, EPhase::SearchOne);
			}
			return true;

		case EPhase::SearchOne:
			if (State.Crate->IsSearching())
			{
				if (Commander->MedkitsCount != State.MedkitsBefore)
				{
					State.bEffectFirst = true; // moved before the grab
				}
				return true;
			}
			{
				const float Waited = State.Time - State.SearchAskedAt;
				const bool bOne = Commander->MedkitsCount == State.MedkitsBefore + 2 && State.Crate->GetStoredCount(ETransferItem::Medkit) == 0;
				const bool bTimed = State.Crate->GetLastSearchDelaySeconds() >= 1.f && Waited >= State.Crate->GetLastSearchDelaySeconds() - StepSeconds - 0.05f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke medkits moved %.2f s after the take (grab at %.2f s): %d -> %d, window open=%d"), Waited,
					State.Crate->GetLastSearchDelaySeconds(), State.MedkitsBefore, Commander->MedkitsCount, Interactions->GetLootCrate() ? 1 : 0);
				if (!bOne || !bTimed || Interactions->GetLootCrate() != State.Crate.Get())
				{
					State.bEffectFirst = true;
				}
				Interactions->LootAll();
				State.SearchAskedAt = State.Time;
				if (!State.Crate->IsSearching() || Commander->ReserveAmmo != State.AmmoBefore)
				{
					State.bEffectFirst = true;
				}
				Enter(State, EPhase::SearchAll);
			}
			return true;

		case EPhase::SearchAll:
			if (State.Crate->IsSearching())
			{
				if (Commander->ReserveAmmo != State.AmmoBefore || !Interactions->GetLootCrate())
				{
					State.bEffectFirst = true;
				}
				return true;
			}
			{
				const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Commander);
				// Each take is its own search: two clips (the second restarted the first if it still played).
				State.bSearchClip = CountClip(Commander, TEXT("ChestSearch@FullBody")) == 2;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke chest searches %d, playing %d, all taken %.2f s after Take All"),
					CountClip(Commander, TEXT("ChestSearch@FullBody")), Anim ? static_cast<int32>(Anim->InteractionAnim) : -1, State.Time - State.SearchAskedAt);
				State.bTake = Commander->ReserveAmmo == State.AmmoBefore + 60 && State.Crate->IsLooted() && !Interactions->GetLootCrate()
					&& !State.bEffectFirst;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke looted: medkits %d -> %d, reserve %d -> %d, looted=%d, effect before its clip=%d"),
					State.MedkitsBefore, Commander->MedkitsCount, State.AmmoBefore, Commander->ReserveAmmo, State.Crate->IsLooted() ? 1 : 0,
					State.bEffectFirst ? 1 : 0);
				Interactions->RequestInteraction(State.Crate.Get());
				Enter(State, EPhase::EmptyClick);
			}
			return true;

		case EPhase::EmptyClick:
			State.bEmpty = !Interactions->IsActionMenuOpen() && !Interactions->GetLootCrate();
			// The medic-sapper leads to the trapped crate, the others hold.
			Squad->SetLeader(Medic);
			Squad->EnterSoloMode();
			State.Trapped = SpawnCrate(World, Feet(Medic) + Medic->GetActorForwardVector() * 300.f, true);
			Interactions->RequestInteraction(State.Trapped.Get());
			Enter(State, EPhase::TrappedMenu);
			return true;

		case EPhase::TrappedMenu:
			if (Interactions->IsActionMenuOpen())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trapped menu \"%s\" / \"%s\""), *Interactions->GetActionMenu().Title.ToString(),
					*Interactions->GetActionMenu().ConfirmText.ToString());
				// The defuser crouches for the work (Godot): luck 100 keeps the odds at 99 %, failures are retried.
				Medic->Luck = 100.f;
				++State.DefuseTries;
				Medic->SetStance(EOperativeStance::Prone);
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::TrappedOpen);
			}
			return true;

		case EPhase::TrappedOpen:
			if (State.Trapped.IsValid() && Interactions->GetLootCrate() == State.Trapped.Get())
			{
				Interactions->LootAll();
				Enter(State, EPhase::TrappedSearch);
			}
			else if (State.PhaseTime > 3.f && State.Trapped.IsValid() && State.Trapped->bTrapped && !State.Trapped->IsDestroyed()
				&& State.DefuseTries < 4)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke crate defusal attempt %d failed, retrying"), State.DefuseTries);
				Interactions->RequestInteraction(State.Trapped.Get());
				Enter(State, EPhase::TrappedMenu);
			}
			else if (State.PhaseTime > 8.f)
			{
				Enter(State, EPhase::Blast);
			}
			return true;

		case EPhase::TrappedSearch:
			if (State.Trapped.IsValid() && State.Trapped->IsSearching() && State.PhaseTime < 8.f)
			{
				return true; // the items move at the search's grab
			}
			{
				// Crouched after the defusal work: both crate clips on the upper body (the legs keep the crouch).
				State.bCrouchClips = Medic->GetStance() == EOperativeStance::Crouching && HasClip(Medic, TEXT("ChestOpen@UpperBody"))
					&& HasClip(Medic, TEXT("ChestSearch@UpperBody")) && !HasClip(Medic, TEXT("ChestOpen@FullBody"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke medic-sapper (stance %d) crate clips: %s"), static_cast<int32>(Medic->GetStance()),
					UOperativeAnimInstance::Of(Medic) ? *FString::Join(UOperativeAnimInstance::Of(Medic)->GetInteractionClipLog(), TEXT(", ")) : TEXT("-"));
				State.bDefuse = State.Trapped.IsValid() && !State.Trapped->bTrapped && Medic->TurretsCount == 1 && Medic->BarricadesCount == 2 && Medic->MinesCount == 2;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defused crate: turrets=%d barricades=%d mines=%d"), Medic->TurretsCount,
					Medic->BarricadesCount, Medic->MinesCount);
				Enter(State, EPhase::Blast);
			}
			return true;

		case EPhase::Blast:
		{
			ALootCrateActor* Doomed = SpawnCrate(World, Feet(Commander) + Commander->GetActorRightVector() * 600.f, true);
			Doomed->DetonateTrap();
			State.bBlast = Doomed->IsDestroyed() && Doomed->GetItems().IsEmpty();
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
		State->Crate = SpawnCrate(World, Feet(Commander) + Commander->GetActorForwardVector() * 400.f, false);
		State->Crate->Contents.Medkits = 2;
		State->MedkitsBefore = Commander->MedkitsCount;
		State->AmmoBefore = Commander->ReserveAmmo;
		World->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(State->Crate.Get());

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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
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
		TEXT("CodexTactics.LootSmoke"),
		TEXT("Dev check: supply crate opening and loot dialog, empty crate, trapped crate defusal and loot, detonation; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
