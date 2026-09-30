// Dev-only console command for a headless check of the global event bus on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EventBusSmoke
// Godot Scripts/events/event_bus.gd: the signals the game emits reach a listener — leader selection, stat points,
// inventory items, feed lines, the dialogue window, rage, the generator, save / load, a fallen operative.

#include "Debug/EventBusProbe.h"
#include "Subsystems/CodexEventBus.h"

void UEventBusProbe::Listen(UCodexEventBus* Bus)
{
	Bus->OnSquadMemberSelected.AddDynamic(this, &UEventBusProbe::OnSelected);
	Bus->OnSoldierStatsUpdated.AddDynamic(this, &UEventBusProbe::OnStats);
	Bus->OnItemUsed.AddDynamic(this, &UEventBusProbe::OnItem);
	Bus->OnSoldierDowned.AddDynamic(this, &UEventBusProbe::OnDowned);
	Bus->OnDialogueLineDisplayed.AddDynamic(this, &UEventBusProbe::OnLine);
	Bus->OnDialogueFinished.AddDynamic(this, &UEventBusProbe::OnDialogueFinished);
	Bus->OnRageStarted.AddDynamic(this, &UEventBusProbe::OnRageStarted);
	Bus->OnRageEnded.AddDynamic(this, &UEventBusProbe::OnRageEnded);
	Bus->OnGeneratorStateChanged.AddDynamic(this, &UEventBusProbe::OnGenerator);
	Bus->OnGameSaved.AddDynamic(this, &UEventBusProbe::OnSaved);
	Bus->OnGameLoaded.AddDynamic(this, &UEventBusProbe::OnLoaded);
}

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/InventoryDrawerWidget.h"
#include "UI/ProfileDialogWidget.h"
#include "UObject/StrongObjectPtr.h"

namespace EventBusSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		int32 Failures = 0;
		bool bDone = false;
		TStrongObjectPtr<UEventBusProbe> Probe;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EventBusSmoke"));
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
		UCodexEventBus* Bus = UCodexEventBus::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		if (!Bus || !Squad || !Hud || Squad->GetMembers().Num() < 3)
		{
			Check(State, false, TEXT("event bus / squad / HUD"));
			return Finish(State, false);
		}
		UEventBusProbe* Probe = NewObject<UEventBusProbe>();
		State.Probe.Reset(Probe);
		Probe->Listen(Bus);

		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Squad->SetLeader(Members[1]);
		Check(State, Probe->Count(TEXT("selected")) == 1, TEXT("squad_member_selected on a new leader"));
		Squad->SetLeader(Members[0]);

		Members[0]->UnspentStatPoints = 1;
		Hud->ToggleProfileDialog();
		Hud->GetProfileDialog()->ClickStat(EProgressStat::Luck, 1);
		Hud->GetProfileDialog()->Close();
		Check(State, Probe->Count(TEXT("stats")) == 1, TEXT("soldier_stats_updated on a stat point"));

		Members[0]->HealthComponent->ApplyDirectHealthLoss(30.f, TEXT("Smoke"));
		Hud->GetInventoryDrawer()->Activate(EInventoryDrawerSlot::Medkit);
		Check(State, Probe->Count(TEXT("item")) == 1 && Probe->LastItem == TEXT("MEDKIT"), TEXT("item_used «MEDKIT» from the drawer"));

		const int32 Lines = Probe->Count(TEXT("line"));
		World->GetSubsystem<UGameMessageSubsystem>()->PostMessage(FText::FromString(TEXT("ШТАБ")), FText::FromString(TEXT("Проверка шины событий")));
		Check(State, Probe->Count(TEXT("line")) == Lines + 1, TEXT("dialogue_line_displayed for a feed line"));

		const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		UDialogueSubsystem* Dialogues = World->GetSubsystem<UDialogueSubsystem>();
		if (const UDialogueSequenceAsset* Intro = GameMode ? GameMode->DialogueMissionStart.LoadSynchronous() : nullptr)
		{
			Dialogues->StartDialogue(Intro);
			Dialogues->SkipDialogue();
		}
		Check(State, Probe->Count(TEXT("dialogue")) == 1, TEXT("dialogue_finished when the window closes"));

		Members[0]->RageComponent->EnterRage();
		Members[0]->RageComponent->ExitRage(TEXT("Smoke"));
		Check(State, Probe->Count(TEXT("rage+")) == 1 && Probe->Count(TEXT("rage-")) == 1, TEXT("rage_started / rage_ended"));

		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == EInteractableType::Generator)
			{
				It->TakeGeneratorDamage(100000.f);
				const bool bOff = Probe->Count(TEXT("generator")) == 1 && !Probe->bLastPowered;
				It->RepairGenerator();
				Check(State, bOff && Probe->Count(TEXT("generator")) == 2 && Probe->bLastPowered, TEXT("generator_state_changed: down, then repaired"));
				break;
			}
		}

		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		Check(State, Saves && Saves->SaveGame(TEXT("autosave_eventbus")) && Probe->Count(TEXT("saved")) == 1 && Probe->LastSlot == TEXT("autosave_eventbus"),
			TEXT("game_saved with the slot"));
		Check(State, Saves && Saves->LoadGame(TEXT("autosave_eventbus")) && Probe->Count(TEXT("loaded")) == 1, TEXT("game_loaded"));
		if (Saves)
		{
			Saves->DeleteSave(TEXT("autosave_eventbus"));
		}

		Members[2]->HealthComponent->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
		Check(State, Probe->Count(TEXT("downed")) == 1, TEXT("soldier_downed when an operative falls"));
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
		TEXT("CodexTactics.EventBusSmoke"),
		TEXT("Dev check: the Godot EventBus signals reach a listener; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
