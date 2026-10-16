// Dev-only headless check of the Demo 01 cover events (beat A6, cover taught as manual orders per operative) on
// L_MovementTest (nothing is saved):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverEventSmoke
// Exploration; an 8 m wide, 3 m high wall is spawned 4 m ahead of the leader; inline tutorial steps: "each" (CoverEntered,
// count 2, distinct_operatives) and "three" (CoverEntered, count 3).
//  1. the commander is ordered into cover: no event and no tutorial progress on the order;
//  2. he settles at the slot: one CoverEntered (+ CoverEntered.High) with him as the subject, kind High, the wall as the
//     target; "each" 1 / 2;
//  3. he leaves (CoverLeft) and takes another slot on the same wall: CoverEntered again, but "each" stays 1 / 2 (same
//     operative) while "three" counts 2;
//  4. the engineer is ordered in: still no new event on the order; he settles: CoverEntered with his id, "each" and
//     "three" complete;
//  5. the engineer side-steps along the wall: CoverShimmy (not CoverEntered).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/CodexEventBus.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace CoverEventSmoke
{
	const TCHAR* Steps = TEXT(R"JSON({"start_beat":"C","beats":[{"id":"C","lesson":false,"checkpoint":false}],"steps":[
		{"id":"each","beat":"C","kind":"objective","text_id":"OBJ_EACH","complete":{"event":"CoverEntered","count":2,"distinct_operatives":true}},
		{"id":"three","beat":"C","kind":"objective","text_id":"OBJ_THREE","complete":{"event":"CoverEntered","count":3}}]})JSON");

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot SlotA;
		FCoverSlot SlotMid;
		FCoverSlot SlotB;
		TStrongObjectPtr<UEventBusProbe> Probe;
		bool bSawGlide = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("CoverEventSmoke"));
		return false;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	int32 CountEntered(const FState& State, const AOperativeCharacter* Operative = nullptr)
	{
		return State.Probe->CountEvent(GameplayEvents::CoverEntered, Operative ? Operative->GetFName() : NAME_None);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				State.Commander = Member->SquadRole == EOperativeRole::Commander ? Member : State.Commander.Get();
				State.Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : State.Engineer.Get();
			}
			AOperativeCharacter* Op = State.Commander.Get();
			if (!Op || !State.Engineer.IsValid() || !Tutorial || (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat))
			{
				Check(State, false, TEXT("exploration with commander, engineer and the tutorial subsystem"));
				return Finish(State);
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			const float GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 400.f + State.R * (Index++ % 2 == 0 ? 250.f : -250.f), State.F.Rotation(), false, true);
				}
			}
			// A 3 m high, 8 m wide, 40 cm thick wall 4 m ahead across the leader's path.
			const FVector WallCentre = State.P + State.F * 400.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 8.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("8 m wall spawned 4 m ahead"));
			if (!UTutorialSubsystem::IsTutorialEnabled())
			{
				if (IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Tutorial.Enabled")))
				{
					Enabled->Set(1, ECVF_SetByCode);
				}
			}
			FTutorialStepsData Data;
			TArray<FString> Warnings;
			FString Error;
			Check(State, TutorialRules::ParseSteps(Steps, Data, Warnings, Error), TEXT("inline steps parse ") + Error);
			Tutorial->LoadSteps(Data);
			Tutorial->StartBeat(TEXT("C"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 2.f)
			{
				return true; // the navmesh rebuilds around the wall
			}
			Check(State, Tutorial->GetStepState(TEXT("each")) == ETutorialStepState::Active, TEXT("tutorial objective 'each operative into cover' active"));
			const auto Find = [&State, World](float Side, FCoverSlot& Out)
			{
				return CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + State.R * Side + FVector(0.f, 0.f, 90.f), State.F, Out)
					&& Out.Height == ECoverHeight::HighCover;
			};
			const bool bSlots = Find(-250.f, State.SlotA) && Find(0.f, State.SlotMid) && Find(250.f, State.SlotB);
			Check(State, bSlots, TEXT("three high cover slots on the wall"));
			if (!bSlots)
			{
				return Finish(State);
			}
			AOperativeCharacter* Op = State.Commander.Get();
			Check(State, Op->OrderTakeCover(State.SlotA, false) == EOperativeOrderResult::Accepted, TEXT("commander ordered into cover"));
			Check(State, CountEntered(State) == 0 && Tutorial->GetStepProgress(TEXT("each")) == 0, TEXT("no CoverEntered and no progress on the mere order"));
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Op = State.Commander.Get();
			if (CountEntered(State, Op) == 0 && State.Time < 15.f)
			{
				if (Op->bInCover && Op->GetVelocity().Size2D() > 20.f)
				{
					State.bSawGlide = true; // the run-in entered the slot while still gliding: the event must wait
				}
				if (State.Time > 0.5f && State.Time < 0.65f)
				{
					Check(State, CountEntered(State) == 0, TEXT("still no event while he walks to the slot"));
				}
				return true;
			}
			Check(State, Op->bInCover && Op->GetVelocity().Size2D() <= 20.f, FString::Printf(TEXT("commander settled in the slot (%.1f s)%s"), State.Time,
				State.bSawGlide ? TEXT(", no event while the enter clip glided him in") : TEXT("")));
			const int32 Last = State.Probe->DetailedNames.FindLast(GameplayEvents::CoverEntered);
			const FGameplayEventContext* Context = Last != INDEX_NONE ? &State.Probe->DetailedContexts[Last] : nullptr;
			Check(State, CountEntered(State) == 1 && CountEntered(State, Op) == 1
				&& State.Probe->CountEvent(FName(TEXT("CoverEntered.High")), Op->GetFName()) == 1,
				TEXT("one CoverEntered (+ CoverEntered.High) with the commander as the subject"));
			Check(State, Context && Context->Kind == FName(TEXT("High")) && Context->SubjectId == Op->GetFName() && State.Wall.IsValid()
				&& Context->TargetId == State.Wall->GetFName() && FVector::Dist2D(Context->Location, Op->GetCoverSlot().WorldLocation) < 1.f,
				FString::Printf(TEXT("context: kind %s, wall %s, slot location"), Context ? *Context->Kind.ToString() : TEXT("-"),
					Context ? *Context->TargetId.ToString() : TEXT("-")));
			Check(State, Tutorial->GetStepProgress(TEXT("each")) == 1 && Tutorial->GetStepState(TEXT("each")) == ETutorialStepState::Active,
				TEXT("tutorial 'each': 1 / 2"));
			Op->LeaveCover(TEXT("smoke"));
			Check(State, State.Probe->CountEvent(GameplayEvents::CoverLeft, Op->GetFName()) == 1, TEXT("CoverLeft on leaving"));
			Check(State, Op->OrderTakeCover(State.SlotMid, false) == EOperativeOrderResult::Accepted, TEXT("commander ordered to the wall's middle"));
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Op = State.Commander.Get();
			if (CountEntered(State, Op) < 2 && State.Time < 15.f)
			{
				return true;
			}
			Check(State, Op->bInCover && CountEntered(State, Op) == 2, TEXT("commander enters cover a second time"));
			Check(State, Tutorial->GetStepProgress(TEXT("each")) == 1 && Tutorial->GetStepProgress(TEXT("three")) == 2,
				FString::Printf(TEXT("same operative: 'each' stays 1 / 2, 'three' counts 2 / 3 (%d, %d)"),
					Tutorial->GetStepProgress(TEXT("each")), Tutorial->GetStepProgress(TEXT("three"))));
			AOperativeCharacter* Engineer = State.Engineer.Get();
			Check(State, Engineer->OrderTakeCover(State.SlotB, false) == EOperativeOrderResult::Accepted, TEXT("engineer ordered into cover"));
			Check(State, CountEntered(State, Engineer) == 0, TEXT("no event for the engineer on the order"));
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Engineer = State.Engineer.Get();
			if (CountEntered(State, Engineer) == 0 && State.Time < 20.f)
			{
				return true;
			}
			Check(State, Engineer->bInCover && CountEntered(State, Engineer) == 1, TEXT("engineer settles: CoverEntered with his id"));
			Check(State, Tutorial->GetStepState(TEXT("each")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("three")) == ETutorialStepState::Completed,
				TEXT("two distinct operatives complete 'each', the third entry completes 'three'"));
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f + State.R * 130.f + FVector(0.f, 0.f, 90.f);
			const bool bShimmy = CoverTraceRules::FindShimmySlot(World, Engineer->GetCoverSlot(), ClickAlong, Shimmy)
				&& Engineer->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bShimmy, TEXT("engineer shimmies along the wall"));
			State.Stage = bShimmy ? 5 : 6;
			State.Time = 0.f;
			return true;
		}
		case 5:
		{
			AOperativeCharacter* Engineer = State.Engineer.Get();
			if ((State.Probe->CountEvent(GameplayEvents::CoverShimmy, Engineer->GetFName()) == 0 || Engineer->bShimmying) && State.Time < 10.f)
			{
				return true;
			}
			Check(State, State.Probe->CountEvent(GameplayEvents::CoverShimmy, Engineer->GetFName()) == 1 && CountEntered(State, Engineer) == 1,
				TEXT("the side-step raises CoverShimmy, not another CoverEntered"));
			State.Stage = 6;
			return true;
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverEventSmoke"),
		TEXT("Dev check of the Demo 01 cover events: CoverEntered only on settling (not the order), distinct-operative tutorial completion, CoverLeft / CoverShimmy."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
