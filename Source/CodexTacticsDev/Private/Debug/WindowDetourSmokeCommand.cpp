// Dev-only repro of G024 (designer, X6 fight 2026-11-25): from outside, a climb-in order must go through THAT window.
// The operative stands 6 m outside a ground-floor window of a sector building that has another opening nearby. One click on the
// sill. FAIL if he enters any other opening before this window, or if he ends still outside.
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1_X6 -game -nullrhi -nosound -LevelJson=L_Gen_Depot_1_X6.json
//     -ExecCmds="CodexTactics.WindowDetourSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
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
#include "Interactables/DoorActor.h"
#include "Interactables/DoorSubsystem.h"
#include "Interactables/WindowOrderRules.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"

namespace WindowDetourSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 PlaceFrames = 60 * 10;
	constexpr int32 SettleFrames = 60;
	constexpr int32 OrderFrames = 60 * 45;
	constexpr int32 HoldAfterCrossFrames = 60 * 2;
	constexpr int32 MaxFrames = 60 * 90;
	constexpr float OutsideCm = 600.f;
	constexpr float OtherOpeningCm = 2000.f;
	constexpr float EnteredCm = 160.f;
	constexpr float CrossedCm = 300.f;

	enum class EStep : uint8 { Warmup, Place, Settle, Order, Done };

	struct FOpening
	{
		FVector Center = FVector::ZeroVector;
		FVector Through = FVector::ForwardVector;
		float StartSide = 1.f;
		FString Name;
		bool bEntered = false;
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AActor> Sill;
		FVector SillCenter = FVector::ZeroVector;
		FVector Through = FVector::ForwardVector;
		float StartSide = 1.f;
		FVector OutsideFeet = FVector::ZeroVector;
		FVector FarFeet = FVector::ZeroVector;
		bool bCrossed = false;
		int32 CrossedFrame = -1;
		TArray<FOpening> Others;
		TArray<TPair<TWeakObjectPtr<AActor>, float>> Tried;
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
		FPlatformMisc::RequestExit(false, TEXT("WindowDetourSmoke"));
	}

	void Go(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
	}

	float SideOf(const FVector& Location, const FVector& Center, const FVector& Through)
	{
		return FVector::DotProduct(Location - Center, Through) >= 0.f ? 1.f : -1.f;
	}

	void Put(AOperativeCharacter& Member, const FVector& Feet, float Yaw)
	{
		Member.TeleportTo(Feet + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	bool ProjectFeet(UNavigationSystemV1& Nav, const FVector& Probe, float FloorZ, FNavLocation& Out)
	{
		return Nav.ProjectPointToNavigation(FVector(Probe.X, Probe.Y, FloorZ + 40.f), Out, FVector(60.f, 60.f, 120.f))
			&& FMath::Abs(Out.Location.Z - FloorZ) <= 120.f;
	}

	bool HasCeiling(UWorld& World, const FVector& Feet)
	{
		FHitResult Ceiling;
		return World.LineTraceSingleByChannel(Ceiling, Feet + FVector(0.f, 0.f, 50.f), Feet + FVector(0.f, 0.f, 800.f), ECC_Visibility);
	}

	bool AlreadyTried(const AActor& Sill, float Side)
	{
		for (const TPair<TWeakObjectPtr<AActor>, float>& Tried : Runner->Tried)
		{
			if (Tried.Key.Get() == &Sill && Tried.Value == Side)
			{
				return true;
			}
		}
		return false;
	}

	int32 CountOtherOpenings(UWorld& World, const AActor& Sill, const FVector& Center)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			const AActor* Other = *It;
			if (Other == &Sill || !Other->ActorHasTag(FName(TEXT("window_sill"))))
			{
				continue;
			}
			const FBox Box = Other->GetComponentsBoundingBox(true);
			const FVector OtherCenter = Box.GetCenter();
			if (FVector::Dist2D(OtherCenter, Center) <= OtherOpeningCm && FMath::Abs(OtherCenter.Z - Center.Z) < 250.f)
			{
				++Count;
			}
		}
		if (const UDoorSubsystem* Doors = World.GetSubsystem<UDoorSubsystem>())
		{
			for (const ADoorActor* Door : Doors->GetDoors())
			{
				if (!Door)
				{
					continue;
				}
				const FVector DoorCenter = Door->GetDoorwayCenter();
				if (FVector::Dist2D(DoorCenter, Center) <= OtherOpeningCm && FMath::Abs(DoorCenter.Z - Center.Z) < 250.f)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	void CollectOthers(UWorld& World, const AOperativeCharacter& Leader)
	{
		Runner->Others.Reset();
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			AActor* Other = *It;
			if (Other == Runner->Sill.Get() || !Other->ActorHasTag(FName(TEXT("window_sill"))))
			{
				continue;
			}
			const FBox Local = Other->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FVector Size = Local.GetSize() * Other->GetActorScale3D().GetAbs();
			const FVector Center = Other->GetActorTransform().TransformPosition(Local.GetCenter());
			if (FMath::Abs(Center.Z - Runner->SillCenter.Z) >= 250.f)
			{
				continue;
			}
			const FWindowSideChoice Axis = WindowOrderRules::ChooseSide(Center, Center, Other->GetActorForwardVector(), Other->GetActorRightVector(),
				static_cast<float>(Size.X), static_cast<float>(Size.Y), 80.f, 120.f);
			FOpening Opening;
			Opening.Center = Center;
			Opening.Through = Axis.Through;
			Opening.StartSide = SideOf(Leader.GetActorLocation(), Center, Axis.Through);
			Opening.Name = Other->GetName();
			Runner->Others.Add(Opening);
		}
		if (const UDoorSubsystem* Doors = World.GetSubsystem<UDoorSubsystem>())
		{
			for (const ADoorActor* Door : Doors->GetDoors())
			{
				if (!Door)
				{
					continue;
				}
				const FVector Center = Door->GetDoorwayCenter();
				const FVector Through = Door->GetNormal().GetSafeNormal2D();
				if (Through.IsNearlyZero() || FMath::Abs(Center.Z - Runner->SillCenter.Z) >= 250.f)
				{
					continue;
				}
				FOpening Opening;
				Opening.Center = Center;
				Opening.Through = Through;
				Opening.StartSide = SideOf(Leader.GetActorLocation(), Center, Through);
				Opening.Name = Door->GetName();
				Runner->Others.Add(Opening);
			}
		}
	}

	bool TryPick(UWorld& World, UNavigationSystemV1& Nav, AOperativeCharacter& Leader)
	{
		const FWindowOrderTuning& Tuning = WindowOrderRules::Get();
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			AActor* Sill = *It;
			if (!Sill->ActorHasTag(FName(TEXT("window_sill"))) || Sill->ActorHasTag(FName(TEXT("window_broken"))))
			{
				continue;
			}
			const FBox Local = Sill->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FVector Size = Local.GetSize() * Sill->GetActorScale3D().GetAbs();
			const FVector Center = Sill->GetActorTransform().TransformPosition(Local.GetCenter());
			const FBox Box = Sill->GetComponentsBoundingBox(true);
			if (CountOtherOpenings(World, *Sill, Center) < 1)
			{
				continue;
			}
			for (const float Side : { 1.f, -1.f })
			{
				if (AlreadyTried(*Sill, Side))
				{
					continue;
				}
				const FWindowSideChoice Axis = WindowOrderRules::ChooseSide(Center + FVector(Side, 0.f, 0.f), Center, Sill->GetActorForwardVector(),
					Sill->GetActorRightVector(), static_cast<float>(Size.X), static_cast<float>(Size.Y), Tuning.ApproachPastCm, Tuning.LandingPastCm);
				const FVector Through = Axis.Through;
				const FVector Probe = Center + Through * (Side * OutsideCm);
				if (HasCeiling(World, Probe))
				{
					continue;
				}
				FNavLocation Outside;
				if (!ProjectFeet(Nav, Probe, Box.Min.Z, Outside))
				{
					continue;
				}
				const float OutsideDist = FVector::Dist2D(Outside.Location, Center);
				if (OutsideDist < 500.f || OutsideDist > 700.f || SideOf(Outside.Location, Center, Through) != Side)
				{
					continue;
				}
				const FVector Feet(Outside.Location.X, Outside.Location.Y, Outside.Location.Z);
				const FWindowSideChoice Choice = WindowOrderRules::ChooseSide(Feet, Center, Sill->GetActorForwardVector(), Sill->GetActorRightVector(),
					static_cast<float>(Size.X), static_cast<float>(Size.Y), Tuning.ApproachPastCm, Tuning.LandingPastCm);
				FNavLocation Inside;
				if (!ProjectFeet(Nav, Choice.Landing, Box.Min.Z, Inside) || SideOf(Inside.Location, Center, Through) == Side)
				{
					continue;
				}
				if (HasCeiling(World, Outside.Location))
				{
					continue;
				}
				Runner->Sill = Sill;
				Runner->SillCenter = Center;
				Runner->Through = Through;
				Runner->StartSide = Side;
				Runner->OutsideFeet = Outside.Location;
				Runner->FarFeet = Inside.Location;
				const float Yaw = static_cast<float>((-Through * Side).Rotation().Yaw);
				Put(Leader, Outside.Location, Yaw);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: sill %s centre %s, outside %.0f cm on side %.0f, far %s, %d other openings"),
					*Sill->GetName(), *Center.ToCompactString(), OutsideDist, Side, *Inside.Location.ToCompactString(), CountOtherOpenings(World, *Sill, Center));
				return true;
			}
		}
		return false;
	}

	void Judge()
	{
		FString Entered;
		int32 EnteredCount = 0;
		for (const FOpening& Opening : Runner->Others)
		{
			if (Opening.bEntered)
			{
				++EnteredCount;
				Entered += TEXT(" ") + Opening.Name;
			}
		}
		Check(Runner->bCrossed, FString::Printf(TEXT("he crosses the ordered window (at %.1f s)"), Runner->CrossedFrame / 60.f));
		Check(EnteredCount == 0, FString::Printf(TEXT("he enters no other opening before it (%d:%s)"), EnteredCount, *Entered));
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
		if (!Squad || !PC || !Nav || Squad->GetMembers().Num() < 1)
		{
			return true;
		}
		AOperativeCharacter* Leader = Squad->GetMembers()[0];
		if (!Leader)
		{
			return true;
		}
		FRunner& R = *Runner;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames)
			{
				Squad->SetLeader(Leader);
				if (!Squad->IsSoloMode())
				{
					Squad->EnterSoloMode();
				}
				Squad->SetSelectedGroup({ Leader }, true);
				Go(EStep::Place);
			}
			return true;
		case EStep::Place:
			if (TryPick(*World, *Nav, *Leader))
			{
				Go(EStep::Settle);
				return true;
			}
			if (R.StepFrame >= PlaceFrames)
			{
				Check(false, TEXT("a ground-floor window with another opening within 20 m and walkable ground 6 m outside"));
				Finish();
			}
			return true;
		case EStep::Settle:
			if (R.StepFrame == 1 && R.Sill.IsValid())
			{
				const float Yaw = static_cast<float>((-R.Through * R.StartSide).Rotation().Yaw);
				Put(*Leader, R.OutsideFeet, Yaw);
				float ClickYaw = 0.f;
				FVector Confirmed;
				if (!PC->FindWindowSillTarget(*R.Sill.Get(), *Leader, Confirmed, ClickYaw))
				{
					R.Tried.Add({ R.Sill, R.StartSide });
					R.Sill = nullptr;
					Go(EStep::Place);
					return true;
				}
				R.FarFeet = Confirmed;
			}
			if (R.StepFrame >= SettleFrames && R.Sill.IsValid())
			{
				const FBox Box = R.Sill->GetComponentsBoundingBox(true);
				const FVector Top(R.SillCenter.X, R.SillCenter.Y, Box.Max.Z);
				FHitResult Click(R.Sill.Get(), nullptr, Top, FVector::UpVector);
				Click.bBlockingHit = true;
				Click.Location = Top;
				Click.TraceStart = Top + R.Through * (R.StartSide * OutsideCm) + FVector(0.f, 0.f, 600.f);
				PC->HandlePlayerClick(Click);
				CollectOthers(*World, *Leader);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: click on %s, window order %d, %d other openings watched"),
					*R.Sill->GetName(), Squad->IsWindowOrder() ? 1 : 0, R.Others.Num());
				Check(Squad->IsWindowOrder(), TEXT("the click is a window order"));
				if (!Squad->IsWindowOrder())
				{
					Finish();
					return true;
				}
				Go(EStep::Order);
			}
			return true;
		case EStep::Order:
		{
			const FVector Pos = Leader->GetActorLocation();
			if (!R.bCrossed && SideOf(Pos, R.SillCenter, R.Through) != R.StartSide && FVector::Dist2D(Pos, R.SillCenter) <= CrossedCm)
			{
				R.bCrossed = true;
				R.CrossedFrame = R.StepFrame;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: crossed the ordered window at %.1f s, %.0f cm from it"), R.StepFrame / 60.f,
					FVector::Dist2D(Pos, R.SillCenter));
			}
			if (!R.bCrossed)
			{
				for (FOpening& Opening : R.Others)
				{
					if (!Opening.bEntered && SideOf(Pos, Opening.Center, Opening.Through) != Opening.StartSide
						&& FVector::Dist2D(Pos, Opening.Center) <= EnteredCm)
					{
						Opening.bEntered = true;
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: entered %s at %.1f s before the ordered window"), *Opening.Name, R.StepFrame / 60.f);
					}
				}
			}
			if (R.StepFrame % 120 == 0)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: t=%.1f pos %s side %.0f dist %.0f"), R.StepFrame / 60.f, *Pos.ToCompactString(),
					SideOf(Pos, R.SillCenter, R.Through), FVector::Dist2D(Pos, R.SillCenter));
			}
			const bool bHeld = R.bCrossed && R.StepFrame >= R.CrossedFrame + HoldAfterCrossFrames;
			if (!bHeld && R.StepFrame < OrderFrames)
			{
				return true;
			}
			const float EndSide = SideOf(Pos, R.SillCenter, R.Through);
			Judge();
			Check(EndSide != R.StartSide, FString::Printf(TEXT("he ends on the far side (side %.0f, start %.0f, dist %.0f)"), EndSide, R.StartSide,
				FVector::Dist2D(Pos, R.SillCenter)));
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
			Pressure->Set(0, ECVF_SetByConsole);
		}
		SmokeUtils::StopLevelPmc(World);
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([]() { Tick(); });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: WindowDetourSmoke started"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WindowDetourSmoke"),
		TEXT("Dev smoke: G024 - from 6 m outside, a window climb-in goes through that window and ends inside."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
