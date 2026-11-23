// Dev-only check of window rule 2 (designer decision 2026-11-23, WindowOrderRules): the FORMATION never takes a whole window on its own, and
// on the leader's window order the followers go through only once he has broken it.
// P (plain): the squad in formation outside a ground-floor window (tag window_sill, walkable on both sides), a plain click on the floor behind
//    it - for 8 s nobody climbs the whole window (it stays unbroken, nobody gets to the far side through it).
// O (order): the click on that window (the window order) - the leader climbs it and breaks it; every follower follows to the far side within
//    40 s, and none crosses before it is broken.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -nullrhi -nosound -LevelJson=L_Gen_StoryShowcase_Kit.json
//     -ExecCmds="CodexTactics.FormationWindowSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/WindowBreakage.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"

namespace FormationWindowSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 SettleFrames = 60;
	constexpr int32 PlainFrames = 60 * 8;
	constexpr int32 OrderFrames = 60 * 40;
	constexpr int32 MaxFrames = 60 * 200;

	enum class EStep : uint8 { Warmup, Place, Plain, Replace, Order, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AActor> Sill;
		FVector SillCenter = FVector::ZeroVector;
		FVector Across = FVector::ForwardVector;
		float StartSide = 1.f;
		TArray<FVector> Starts;
		FVector FarFeet = FVector::ZeroVector;
		int32 BrokenFrame = -1;
		TArray<int32> CrossedFrame;
		TArray<TWeakObjectPtr<AActor>> TriedSills;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("FormationWindowSmoke"));
	}

	void Go(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
	}

	void Put(AOperativeCharacter& Member, const FVector& Feet, float Yaw)
	{
		Member.TeleportTo(Feet + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** +1 / -1: the side of the window he is on (along Across), as StartSide. */
	float SideOf(const AOperativeCharacter& Member)
	{
		return FVector::DotProduct(Member.GetActorLocation() - Runner->SillCenter, Runner->Across) >= 0.f ? 1.f : -1.f;
	}

	bool IsBroken()
	{
		const AActor* Sill = Runner->Sill.Get();
		return Sill && Sill->ActorHasTag(FName(TEXT("window_broken")));
	}

	void PlaceSquad(const TArray<AOperativeCharacter*>& Members)
	{
		const float Yaw = static_cast<float>((-Runner->Across * Runner->StartSide).Rotation().Yaw);
		for (int32 Index = 0; Index < Members.Num() && Index < Runner->Starts.Num(); ++Index)
		{
			Put(*Members[Index], Runner->Starts[Index], Yaw);
		}
	}

	bool Tick()
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				World = Context.World();
			}
		}
		if (!World || !Runner || Runner->Step == EStep::Done)
		{
			return true;
		}
		++Runner->Frame;
		++Runner->StepFrame;
		if (Runner->Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(Runner->Step)));
			Finish();
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !PC || !Nav || Squad->GetMembers().Num() < 2)
		{
			return true;
		}
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		FRunner& R = *Runner;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames)
			{
				if (Squad->IsSoloMode())
				{
					Squad->ExitSoloMode();
				}
				Squad->SetLeader(Members[0]);
				Go(EStep::Place);
			}
			return true;
		case EStep::Place:
		{
			// A ground-floor sill with walkable ground on both sides; the squad outside (no ceiling), 2.5 m off it, side by side.
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Sill = *It;
				if (!Sill->ActorHasTag(FName(TEXT("window_sill"))) || Sill->ActorHasTag(FName(TEXT("window_broken"))) || R.TriedSills.Contains(Sill))
				{
					continue;
				}
				R.TriedSills.Add(Sill);
				const FBox Local = Sill->CalculateComponentsBoundingBoxInLocalSpace(false);
				const FVector Size = Local.GetSize() * Sill->GetActorScale3D().GetAbs();
				const FVector Across = (Size.X < Size.Y ? Sill->GetActorForwardVector() : Sill->GetActorRightVector()).GetSafeNormal2D();
				const FVector Along(-Across.Y, Across.X, 0.f);
				const FVector Center = Sill->GetActorTransform().TransformPosition(Local.GetCenter());
				const FBox Box = Sill->GetComponentsBoundingBox(true);
				for (const float Side : { 1.f, -1.f })
				{
					TArray<FVector> Starts;
					for (int32 Index = 0; Index < Members.Num(); ++Index)
					{
						const float Offset = Index == 0 ? 0.f : (Index % 2 ? 1.f : -1.f) * 90.f * ((Index + 1) / 2);
						const FVector Probe = Center + Across * (Side * (Index == 0 ? 250.f : 340.f)) + Along * Offset;
						FNavLocation Here;
						FHitResult Ceiling;
						if (World->LineTraceSingleByChannel(Ceiling, Probe + FVector(0.f, 0.f, 50.f), Probe + FVector(0.f, 0.f, 800.f), ECC_Visibility)
							|| !Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Box.Min.Z + 40.f), Here, FVector(60.f, 60.f, 120.f))
							|| FMath::Abs(Here.Location.Z - Box.Min.Z) > 80.f)
						{
							break;
						}
						Starts.Add(Here.Location);
					}
					if (Starts.Num() < Members.Num())
					{
						continue;
					}
					R.Sill = Sill;
					R.SillCenter = Center;
					R.Across = Across;
					R.StartSide = Side;
					R.Starts = Starts;
					PlaceSquad(Members);
					float Yaw = 0.f;
					if (!PC->FindWindowSillTarget(*Sill, *Members[0], R.FarFeet, Yaw))
					{
						R.Sill = nullptr;
						continue; // no walkable floor behind it
					}
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: sill %s at %s, %d men outside, the far side %s"), *Sill->GetName(), *Center.ToCompactString(),
						Members.Num(), *R.FarFeet.ToCompactString());
					// E (Fable review 2026-11-23): a click on a pawn standing at the window (an enemy by it / his own man) is no window order:
					// a hit on that pawn at the sill's face, the ray from outside through the opening.
					{
						FHitResult OnPawn(Members[Members.Num() - 1], nullptr, Center + FVector(0.f, 0.f, 60.f), Across * Side);
						OnPawn.bBlockingHit = true;
						OnPawn.TraceStart = Center + Across * (Side * 600.f) + FVector(0.f, 0.f, 400.f);
						Check(WindowBreakage::FindWindowSillAt(*World, OnPawn) == nullptr,
							TEXT("E: a click on a pawn at the window is a click on him, not on the window"));
					}
					Go(EStep::Plain);
					return true;
				}
			}
			Check(false, FString::Printf(TEXT("a ground-floor window with walkable ground outside for the squad and inside (%d sills tried)"), R.TriedSills.Num()));
			Finish();
			return true;
		}
		case EStep::Plain:
		{
			if (R.StepFrame == SettleFrames)
			{
				FHitResult Click(nullptr, nullptr, R.FarFeet, FVector::UpVector);
				Click.bBlockingHit = true;
				Click.Location = R.FarFeet;
				Click.TraceStart = R.FarFeet + FVector(0.f, 0.f, 1200.f);
				PC->HandlePlayerClick(Click);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: P: a plain click on the floor behind the window, window order %d"), Squad->IsWindowOrder() ? 1 : 0);
			}
			if (R.StepFrame < SettleFrames + PlainFrames)
			{
				return true;
			}
			int32 Through = 0;
			for (const AOperativeCharacter* Member : Members)
			{
				Through += SideOf(*Member) != R.StartSide && FVector::Dist2D(Member->GetActorLocation(), R.SillCenter) < 400.f ? 1 : 0;
			}
			Check(!IsBroken() && !Squad->IsWindowOrder(), FString::Printf(TEXT("P: the formation does not take the whole window on its own (broken %d, window order %d, %d men just behind it)"),
				IsBroken() ? 1 : 0, Squad->IsWindowOrder() ? 1 : 0, Through));
			Go(EStep::Replace);
			return true;
		}
		case EStep::Replace:
			if (R.StepFrame == 1)
			{
				PlaceSquad(Members);
			}
			if (R.StepFrame >= SettleFrames)
			{
				const FBox Box = R.Sill.IsValid() ? R.Sill->GetComponentsBoundingBox(true) : FBox(ForceInit);
				const FVector Top(R.SillCenter.X, R.SillCenter.Y, Box.Max.Z);
				FHitResult Click(R.Sill.Get(), nullptr, Top, FVector::UpVector);
				Click.bBlockingHit = true;
				Click.Location = Top;
				Click.TraceStart = Top + R.Across * (R.StartSide * 600.f) + FVector(0.f, 0.f, 600.f);
				PC->HandlePlayerClick(Click);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: O: the click on the window, window order %d"), Squad->IsWindowOrder() ? 1 : 0);
				R.CrossedFrame.Init(-1, Members.Num());
				Go(EStep::Order);
			}
			return true;
		case EStep::Order:
		{
			if (R.BrokenFrame < 0 && IsBroken())
			{
				R.BrokenFrame = R.StepFrame;
			}
			bool bAll = true;
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				if (R.CrossedFrame[Index] < 0 && SideOf(*Members[Index]) != R.StartSide)
				{
					R.CrossedFrame[Index] = R.StepFrame;
				}
				bAll &= R.CrossedFrame[Index] >= 0;
			}
			if (!bAll && R.StepFrame < OrderFrames)
			{
				return true;
			}
			FString Who;
			bool bNoneBeforeBroken = true;
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				const bool bLeader = Members[Index] == Squad->GetFormationLeader();
				Who += FString::Printf(TEXT(" [%s%s %s]"), *Members[Index]->DisplayName.ToString(), bLeader ? TEXT(" (leader)") : TEXT(""),
					R.CrossedFrame[Index] >= 0 ? *FString::Printf(TEXT("through at %.1f s"), R.CrossedFrame[Index] / 60.f) : TEXT("NOT through"));
				if (!bLeader && R.CrossedFrame[Index] >= 0 && (R.BrokenFrame < 0 || R.CrossedFrame[Index] < R.BrokenFrame))
				{
					bNoneBeforeBroken = false;
				}
			}
			Check(R.BrokenFrame >= 0, FString::Printf(TEXT("O: the leader breaks the window climbing it (%.1f s)"), R.BrokenFrame / 60.f));
			Check(bAll, FString::Printf(TEXT("O: the whole formation follows him through it (%.1f s)%s"), R.StepFrame / 60.f, *Who));
			Check(bNoneBeforeBroken, TEXT("O: no follower goes through before the window is broken"));
			Finish();
			return true;
		}
		default:
			return true;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole); // no fight: rule 2 out of combat
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([]() { Tick(); });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: FormationWindowSmoke started"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationWindowSmoke"),
		TEXT("Dev smoke: window rule 2 - the formation never takes a whole window on its own; on the window order the followers go through once the leader broke it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
