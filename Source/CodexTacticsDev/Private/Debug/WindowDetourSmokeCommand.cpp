// Dev-only repro of G024 (designer, X6 fight 2026-11-25): from outside, a climb-in order must go through THAT window.
// The operative stands 6 m outside a ground-floor window of a sector building that has another opening nearby. One click on the
// sill. FAIL if he enters any other opening before this window, or if he ends still outside.
// 152: the single operative runs through the window to a goal beyond its landing, retaining sprint on the resumed path.
// G024b: the same window order with 3 operatives box-selected in Solo. FAIL if any of them enters another opening first.
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1_X6 -game -nullrhi -nosound -LevelJson=L_Gen_Depot_1_X6.json
//     -ExecCmds="CodexTactics.WindowDetourSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
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
	constexpr int32 MaxFrames = 60 * 180;
	constexpr float OutsideCm = 600.f;
	constexpr float OtherOpeningCm = 2000.f;
	constexpr float EnteredCm = 160.f;
	constexpr float CrossedCm = 300.f;

	enum class EStep : uint8 { Warmup, Place, Settle, Order, RunVault, Done };

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
		FVector RunGoal = FVector::ZeroVector;
		bool bCrossed = false;
		int32 CrossedFrame = -1;
		TArray<FOpening> Others;
		TArray<TPair<TWeakObjectPtr<AActor>, float>> Tried;
		struct FMan
		{
			TWeakObjectPtr<AOperativeCharacter> Op;
			FVector Feet = FVector::ZeroVector;
			bool bCrossed = false;
			int32 CrossedFrame = -1;
			bool bEnteredOther = false;
			FString Entered;
		};
		TArray<FMan> Men;
		bool bGroupCase = false;
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
				// Leave a real path leg beyond the vault landing, so arrival cannot hide a lost run gait.
				FNavLocation RunGoal;
				if (!ProjectFeet(Nav, Inside.Location - Through * (Side * 400.f), Box.Min.Z, RunGoal)
					|| SideOf(RunGoal.Location, Center, Through) == Side
					|| FVector::Dist2D(RunGoal.Location, Inside.Location) < 280.f)
				{
					continue;
				}
				Runner->RunGoal = RunGoal.Location;
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

	bool PlaceWing(UWorld& World, UNavigationSystemV1& Nav, USquadSubsystem& Squad)
	{
		TArray<AOperativeCharacter*> Living;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Living.Add(Member);
			}
		}
		if (Living.Num() < 3)
		{
			return false;
		}
		const FVector Along(-Runner->Through.Y, Runner->Through.X, 0.f);
		const float Yaw = static_cast<float>((-Runner->Through * Runner->StartSide).Rotation().Yaw);
		const float Offsets[3] = { 0.f, 140.f, -140.f };
		Runner->Men.Reset();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVector Feet = Runner->OutsideFeet;
			if (Index > 0)
			{
				FNavLocation Spot;
				const FVector Probe = Runner->OutsideFeet + Along * Offsets[Index];
				if (!ProjectFeet(Nav, Probe, Runner->OutsideFeet.Z, Spot) || HasCeiling(World, Spot.Location)
					|| SideOf(Spot.Location, Runner->SillCenter, Runner->Through) != Runner->StartSide)
				{
					return false;
				}
				Feet = Spot.Location;
			}
			Put(*Living[Index], Feet, Yaw);
			FRunner::FMan Man;
			Man.Op = Living[Index];
			Man.Feet = Feet;
			Runner->Men.Add(Man);
		}
		Squad.SetLeader(Living[0]);
		Squad.DisbandFormationToSoloBox({ Living[0], Living[1], Living[2] });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: box-selected %d outside %s, solo %d, multi %d"), Runner->Men.Num(),
			*GetNameSafe(Runner->Sill.Get()), Squad.IsSoloMode() ? 1 : 0, Squad.HasMultiSelection() ? 1 : 0);
		return Squad.IsSoloMode() && Squad.HasMultiSelection() && Runner->Men.Num() == 3;
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
			if (R.bGroupCase && Squad->GetMembers().Num() < 3)
			{
				Check(false, TEXT("three living operatives for the box selection"));
				Finish();
				return true;
			}
			if (TryPick(*World, *Nav, *Leader))
			{
				if (R.bGroupCase && !PlaceWing(*World, *Nav, *Squad))
				{
					R.Tried.Add({ R.Sill, R.StartSide });
					R.Sill = nullptr;
					return true;
				}
				Go(EStep::Settle);
				return true;
			}
			if (R.StepFrame >= PlaceFrames)
			{
				Check(false, R.bGroupCase
					? TEXT("a ground-floor window with room for 3 operatives 6 m outside and another opening within 20 m")
					: TEXT("a ground-floor window with another opening within 20 m and walkable ground 6 m outside"));
				Finish();
			}
			return true;
		case EStep::Settle:
			if (R.StepFrame == 1 && R.Sill.IsValid())
			{
				const float Yaw = static_cast<float>((-R.Through * R.StartSide).Rotation().Yaw);
				if (R.bGroupCase && R.Men.Num() == 3)
				{
					TArray<AOperativeCharacter*> Selected;
					for (FRunner::FMan& Man : R.Men)
					{
						if (AOperativeCharacter* Op = Man.Op.Get())
						{
							Put(*Op, Man.Feet, Yaw);
							Selected.Add(Op);
						}
					}
					if (Selected.Num() == 3)
					{
						Squad->DisbandFormationToSoloBox(Selected);
					}
				}
				else
				{
					Put(*Leader, R.OutsideFeet, Yaw);
				}
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
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: click on %s, window order %d, multi %d, %d other openings watched"),
					*R.Sill->GetName(), Squad->IsWindowOrder() ? 1 : 0, Squad->HasMultiSelection() ? 1 : 0, R.Others.Num());
				Check(Squad->IsWindowOrder(), R.bGroupCase ? TEXT("the group click is a window order") : TEXT("the click is a window order"));
				if (R.bGroupCase)
				{
					const bool bGroup = Squad->IsSoloMode() && Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 3;
					Check(bGroup, TEXT("Solo box selection of 3 is the group that was ordered"));
					if (!bGroup)
					{
						Finish();
						return true;
					}
				}
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
			if (R.bGroupCase)
			{
				bool bAllHeld = R.Men.Num() == 3;
				for (FRunner::FMan& Man : R.Men)
				{
					AOperativeCharacter* Op = Man.Op.Get();
					if (!Op)
					{
						bAllHeld = false;
						continue;
					}
					const FVector Pos = Op->GetActorLocation();
					if (!Man.bCrossed && SideOf(Pos, R.SillCenter, R.Through) != R.StartSide && FVector::Dist2D(Pos, R.SillCenter) <= CrossedCm)
					{
						Man.bCrossed = true;
						Man.CrossedFrame = R.StepFrame;
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s crossed the ordered window at %.1f s, %.0f cm from it"), *Op->GetName(),
							R.StepFrame / 60.f, FVector::Dist2D(Pos, R.SillCenter));
					}
					if (!Man.bCrossed && !Man.bEnteredOther)
					{
						for (const FOpening& Opening : R.Others)
						{
							if (SideOf(Pos, Opening.Center, Opening.Through) != Opening.StartSide && FVector::Dist2D(Pos, Opening.Center) <= EnteredCm)
							{
								Man.bEnteredOther = true;
								Man.Entered = Opening.Name;
								UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s entered %s at %.1f s before the ordered window"), *Op->GetName(),
									*Opening.Name, R.StepFrame / 60.f);
								break;
							}
						}
					}
					bAllHeld &= Man.bCrossed && R.StepFrame >= Man.CrossedFrame + HoldAfterCrossFrames;
				}
				if (R.StepFrame % 120 == 0 && R.Men.Num() > 0 && R.Men[0].Op.IsValid())
				{
					const FVector Pos = R.Men[0].Op->GetActorLocation();
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: group t=%.1f lead %s side %.0f dist %.0f"), R.StepFrame / 60.f, *Pos.ToCompactString(),
						SideOf(Pos, R.SillCenter, R.Through), FVector::Dist2D(Pos, R.SillCenter));
				}
				if (!bAllHeld && R.StepFrame < OrderFrames)
				{
					return true;
				}
				int32 Entered = 0;
				FString Names;
				for (const FRunner::FMan& Man : R.Men)
				{
					const AOperativeCharacter* Op = Man.Op.Get();
					const FString Who = Op ? Op->GetName() : TEXT("missing");
					if (Man.bEnteredOther)
					{
						++Entered;
						Names += TEXT(" ") + Who + TEXT(":") + Man.Entered;
					}
					const FVector Pos = Op ? Op->GetActorLocation() : FVector::ZeroVector;
					const float EndSide = Op ? SideOf(Pos, R.SillCenter, R.Through) : R.StartSide;
					Check(Man.bCrossed, FString::Printf(TEXT("%s crosses the ordered window (at %.1f s)"), *Who, Man.CrossedFrame / 60.f));
					Check(Op && EndSide != R.StartSide, FString::Printf(TEXT("%s ends on the far side (side %.0f, start %.0f, dist %.0f)"), *Who, EndSide,
						R.StartSide, Op ? FVector::Dist2D(Pos, R.SillCenter) : -1.f));
				}
				Check(Entered == 0, FString::Printf(TEXT("nobody enters another opening before the ordered window (%d:%s)"), Entered, *Names));
				Finish();
				return true;
			}
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
			if (R.Failures == 0)
			{
				Go(EStep::RunVault);
				return true;
			}
			Finish();
			return true;
		}
		case EStep::RunVault:
			if (R.StepFrame == 1)
			{
				const float Yaw = static_cast<float>((-R.Through * R.StartSide).Rotation().Yaw);
				Put(*Leader, R.OutsideFeet, Yaw);
				FVector Landing, Approach;
				float ClickYaw = 0.f;
				bool bApproachOnNav = false;
				const bool bTarget = R.Sill.IsValid() && PC->FindWindowSillTarget(*R.Sill.Get(), *Leader, Landing, ClickYaw, &Approach, &bApproachOnNav);
				Check(bTarget && bApproachOnNav, TEXT("run regression has a window approach on navmesh"));
				if (!bTarget || !bApproachOnNav)
				{
					Finish();
					return true;
				}
				Put(*Leader, Approach, Yaw);
				Check(Leader->OrderMoveTo(R.RunGoal, true) == EOperativeOrderResult::Accepted && Leader->IsSprinting(),
					TEXT("run order starts sprinting before the window traversal"));
				// Ordinary broken-window vault, outside the explicit window approach's completion guard.
				// This stops the path through HandleMoveFinished, reproducing the lost-gait bug.
				Check(Leader->TryVault(-R.Through * R.StartSide, true), TEXT("same run order starts the window vault"));
				if (!Leader->IsVaulting())
				{
					Finish();
				}
				return true;
			}
			if (Leader->IsVaulting() && R.StepFrame < 60 * 10)
			{
				return true;
			}
			Check(!Leader->IsVaulting() && Leader->IsMoving() && Leader->IsSprinting(),
				TEXT("same run order is still sprinting on its path after the window vault"));
			if (R.Failures > 0)
			{
				Finish();
				return true;
			}
			R.bGroupCase = true;
			R.bCrossed = false;
			R.CrossedFrame = -1;
			R.Others.Reset();
			R.Sill = nullptr;
			R.Men.Reset();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: single operative and run traversal passed, next is the box selection of 3"));
			Go(EStep::Place);
			return true;
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
		TEXT("Dev smoke: G024/G024b - from 6 m outside, a window climb-in goes through that window. Then a Solo box of 3."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
