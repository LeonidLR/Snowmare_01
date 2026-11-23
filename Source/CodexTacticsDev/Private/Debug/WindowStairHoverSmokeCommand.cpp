// Dev-only check of the window / stair-up silhouettes and their clicks (designer test 2026-11-22 on the dressed khrushchevka showcase: "no
// silhouette when I hover a window; the click made him look for a door", "the same with the stairs"). Real cursor rays (URoofCutawaySubsystem::
// TraceCursorRay, as the controller traces them) and the real click (ACodexTacticsPlayerController::HandlePlayerClick, the click preview included).
// W: the leader outside a ground-floor window (tag window_sill, its far side walkable), Solo; the ray from a camera on his side onto the glass
//    must resolve to that sill (WindowBreakage::FindWindowSillAt), the hover silhouette must show, the click must take him THROUGH the window
//    (on its far side within 20 s) and break it (tag window_broken).
// S: the leader on storey 0 of a 3+ storey house next to a stair flight (a piece tagged stair=<id>); the storeys above are cut; the ray onto the
//    flight must give the stair-up target (FindStairUpTarget), the hover silhouette must show and the click must take him up to storey 1 within
//    30 s.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -nullrhi -nosound -LevelJson=L_Gen_StoryShowcase_Kit.json
//     -ExecCmds="CodexTactics.WindowStairHoverSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/WindowBreakage.h"
#include "Tactics/CoverGhostActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"

namespace WindowStairHoverSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 SettleFrames = 90;
	constexpr int32 MaxFrames = 60 * 240;

	enum class EStep : uint8 { Warmup, WindowPlace, WindowHover, WindowShot, WindowWalk, StairPlace, StairHover, StairShot, StairWalk, Done };

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
		TArray<TWeakObjectPtr<AActor>> TriedSills;
		FString Building;
		float Storey1Z = 0.f;
		TWeakObjectPtr<UPrimitiveComponent> Stair;
		TArray<TWeakObjectPtr<UPrimitiveComponent>> TriedStairs;
		bool bShots = false;
		FHitResult PendingClick;
		TWeakObjectPtr<ACameraActor> ShotCamera;
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
		FPlatformMisc::RequestExit(false, TEXT("WindowStairHoverSmoke"));
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

	float FeetZ(const AOperativeCharacter& Member)
	{
		return static_cast<float>(Member.GetActorLocation().Z - Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	bool IsStair(const UPrimitiveComponent& Piece)
	{
		const auto IsStairTag = [](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("stair="), ESearchCase::CaseSensitive); };
		const AActor* Owner = Piece.GetOwner();
		return Piece.ComponentTags.ContainsByPredicate(IsStairTag) || (Owner && Owner->Tags.ContainsByPredicate(IsStairTag));
	}

	/** A cursor ray from a camera at Eye through Aim, refined through the cutaway for Leader (the controller's own trace). */
	bool CursorHit(UWorld& World, const FVector& Eye, const FVector& Aim, const AOperativeCharacter& Leader, FHitResult& OutHit)
	{
		const URoofCutawaySubsystem* Roofs = World.GetSubsystem<URoofCutawaySubsystem>();
		return Roofs && Roofs->TraceCursorRay(Eye, Aim + (Aim - Eye).GetSafeNormal() * 2000.f, ECC_Visibility, &Leader, OutHit);
	}


	/** Logs where a hover silhouette stands and whether it is visible (designer: "no silhouette" though the hover logic said shown). */
	void DescribeGhost(const TCHAR* Label, const ACoverGhostActor* Ghost, const FVector& Expected)
	{
		if (!Ghost)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s silhouette: none"), Label);
			return;
		}
		const FBox Body = Ghost->GetComponentsBoundingBox(true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s silhouette at %s (body %s), hidden %d, shown %d; expected near %s (%.0f cm)"), Label,
			*Ghost->GetActorLocation().ToCompactString(), *Body.ToString(), Ghost->IsHidden() ? 1 : 0, Ghost->IsShown() ? 1 : 0, *Expected.ToCompactString(),
			FVector::Dist(Body.GetCenter(), Expected));
	}

	/** shots=1 (needs rendering): a camera at Eye looking at Aim, a screenshot to Saved/StoryShowcase/hover/<Name>.png. */
	void Shot(UWorld& World, APlayerController& PC, const FVector& Eye, const FVector& Aim, const FString& Name)
	{
		if (!Runner->bShots)
		{
			return;
		}
		if (!Runner->ShotCamera.IsValid())
		{
			Runner->ShotCamera = World.SpawnActor<ACameraActor>(Eye, (Aim - Eye).Rotation());
		}
		if (ACameraActor* Camera = Runner->ShotCamera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Aim - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(55.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC.SetViewTarget(Camera);
		}
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StoryShowcase/hover") / (Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: shot %s"), *File);
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
		URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !Roofs || !PC || !Leader || !Nav)
		{
			return true;
		}
		FRunner& R = *Runner;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames)
			{
				Squad->EnterSoloMode();
				Go(EStep::WindowPlace);
			}
			return true;
		case EStep::WindowPlace:
		{
			// A ground-floor sill whose two sides are both walkable ground at its level; the leader on the side with no ceiling (outside).
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Sill = *It;
				if (!Sill->ActorHasTag(FName(TEXT("window_sill"))) || R.TriedSills.Contains(Sill))
				{
					continue;
				}
				R.TriedSills.Add(Sill);
				const FBox Local = Sill->CalculateComponentsBoundingBoxInLocalSpace(false);
				const FVector Size = Local.GetSize() * Sill->GetActorScale3D().GetAbs();
				const FVector Across = (Size.X < Size.Y ? Sill->GetActorForwardVector() : Sill->GetActorRightVector()).GetSafeNormal2D();
				const FVector Center = Sill->GetActorTransform().TransformPosition(Local.GetCenter());
				const FBox Box = Sill->GetComponentsBoundingBox(true);
				for (const float Side : { 1.f, -1.f })
				{
					const FVector Probe = Center + Across * (Side * 250.f);
					FNavLocation Here;
					FHitResult Ceiling;
					const bool bOutside = !World->LineTraceSingleByChannel(Ceiling, Probe + FVector(0.f, 0.f, 50.f), Probe + FVector(0.f, 0.f, 800.f), ECC_Visibility);
					if (!bOutside || !Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Box.Min.Z + 40.f), Here, FVector(60.f, 60.f, 120.f))
						|| FMath::Abs(Here.Location.Z - Box.Min.Z) > 80.f)
					{
						continue;
					}
					Put(*Leader, Here.Location, static_cast<float>((-Across * Side).Rotation().Yaw));
					FVector Feet;
					float Yaw = 0.f;
					if (!PC->FindWindowSillTarget(*Sill, *Leader, Feet, Yaw))
					{
						continue; // no walkable floor behind it
					}
					R.Sill = Sill;
					R.SillCenter = Center;
					R.Across = Across;
					R.StartSide = Side;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: W: sill %s at %s, the leader outside at %s, the far side %s"), *Sill->GetName(),
						*Center.ToCompactString(), *Here.Location.ToCompactString(), *Feet.ToCompactString());
					Go(EStep::WindowHover);
					return true;
				}
			}
			Check(false, FString::Printf(TEXT("W: a ground-floor window with walkable ground outside and inside (%d sills tried)"), R.TriedSills.Num()));
			Go(EStep::StairPlace);
			return true;
		}
		case EStep::WindowHover:
		{
			if (R.StepFrame < SettleFrames)
			{
				return true;
			}
			AActor* Sill = R.Sill.Get();
			const FVector Glass = R.SillCenter + FVector(0.f, 0.f, Sill ? Sill->GetComponentsBoundingBox(true).GetExtent().Z + 70.f : 120.f);
			const FVector Eye = Glass + R.Across * (R.StartSide * 900.f) + FVector(0.f, 0.f, 900.f);
			FHitResult Hit;
			const bool bHit = CursorHit(*World, Eye, Glass, *Leader, Hit);
			AActor* Found = bHit ? WindowBreakage::FindWindowSillAt(*World, Hit) : nullptr;
			Check(Found == Sill, FString::Printf(TEXT("W: the cursor on the window (hit %s %s at %s, normal z %.2f) resolves to its sill (%s)"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("-"),
				bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT(""), *Hit.ImpactPoint.ToCompactString(), Hit.ImpactNormal.Z, *GetNameSafe(Found)));
			PC->bFreezeMantleHoverForTesting = true; // the real (off-window) cursor must not clear the hover the smoke sets
			Check(bHit && PC->UpdateWindowHover(Leader, &Hit) && PC->IsWindowHoverShown(), TEXT("W: the hover silhouette shows past the window"));
			DescribeGhost(TEXT("W"), PC->GetWindowHoverGhost(), R.SillCenter + FVector(0.f, 0.f, 60.f));
			// Straight through the window opening at glass height (no wall between the camera and a silhouette standing in / past the window).
			const ACoverGhostActor* WGhost = PC->GetWindowHoverGhost();
			const FVector GhostAt = WGhost ? WGhost->GetComponentsBoundingBox(true).GetCenter() : Glass;
			Shot(*World, *PC, Glass + R.Across * (R.StartSide * 350.f) + FVector(0.f, 0.f, 20.f), GhostAt, TEXT("window_hover"));
			R.PendingClick = Hit;
			Go(EStep::WindowShot);
			return true;
		}
		case EStep::WindowShot:
			if (R.StepFrame < (R.bShots ? 20 : 1))
			{
				return true;
			}
			DescribeGhost(TEXT("W (after the shot)"), PC->GetWindowHoverGhost(), R.SillCenter + FVector(0.f, 0.f, 60.f));
			PC->bFreezeMantleHoverForTesting = false;
			PC->UpdateWindowHover(nullptr, nullptr);
			if (R.PendingClick.bBlockingHit)
			{
				PC->HandlePlayerClick(R.PendingClick);
			}
			Go(EStep::WindowWalk);
			return true;
		case EStep::WindowWalk:
		{
			AActor* Sill = R.Sill.Get();
			const float Side = static_cast<float>(FVector::DotProduct(Leader->GetActorLocation() - R.SillCenter, R.Across));
			const bool bThrough = Side * R.StartSide < -40.f;
			if (!bThrough && R.StepFrame < 60 * 20)
			{
				return true;
			}
			Check(bThrough, FString::Printf(TEXT("W: the click took him through the window (%.0f cm on the far side, %.1f s)"), -Side * R.StartSide, R.StepFrame / 60.f));
			Check(Sill && Sill->ActorHasTag(FName(TEXT("window_broken"))), TEXT("W: the window is broken (tag window_broken)"));
			Go(EStep::StairPlace);
			return true;
		}
		case EStep::StairPlace:
		{
			// A stair flight of storey 0 of a 3+ storey house; the leader on that storey's navmesh within 3 m of it.
			Roofs->RebuildStoreyIndex();
			for (const TPair<FString, int32>& Building : Roofs->GetStoreyBuildings())
			{
				float Base0 = 0.f;
				float Base1 = 0.f;
				if (Building.Value < 3 || !Roofs->GetStoreyBaseZ(Building.Key, 0, Base0) || !Roofs->GetStoreyBaseZ(Building.Key, 1, Base1))
				{
					continue;
				}
				for (const TPair<UPrimitiveComponent*, int32>& Piece : Roofs->GetStoreyPieces(Building.Key))
				{
					UPrimitiveComponent* Comp = Piece.Key;
					if (Piece.Value != 0 || !Comp || !IsStair(*Comp) || R.TriedStairs.Contains(Comp))
					{
						continue;
					}
					R.TriedStairs.Add(Comp);
					const FBox Bounds = Comp->Bounds.GetBox();
					if (Bounds.GetSize().Z < 60.f)
					{
						continue; // a landing, not a flight
					}
					FNavLocation Here;
					if (!Nav->ProjectPointToNavigation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Base0 + 40.f), Here, FVector(300.f, 300.f, 60.f))
						|| FMath::Abs(Here.Location.Z - Base0) > 40.f)
					{
						continue;
					}
					Put(*Leader, Here.Location, 0.f);
					R.Building = Building.Key;
					R.Storey1Z = Base1;
					R.Stair = Comp;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: S: %s stair piece %s.%s (bounds %s), the leader on storey 0 at %s, storey 1 at %.0f"),
						*Building.Key, *GetNameSafe(Comp->GetOwner()), *Comp->GetName(), *Bounds.ToString(), *Here.Location.ToCompactString(), Base1);
					Go(EStep::StairHover);
					return true;
				}
			}
			Check(false, FString::Printf(TEXT("S: a storey-0 stair flight with navmesh near it (%d pieces tried)"), R.TriedStairs.Num()));
			Finish();
			return true;
		}
		case EStep::StairHover:
		{
			if (R.StepFrame < SettleFrames)
			{
				return true;
			}
			UPrimitiveComponent* Stair = R.Stair.Get();
			const FBox Bounds = Stair ? Stair->Bounds.GetBox() : FBox(ForceInit);
			const FVector Aim(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z - 10.f);
			const FVector Eye = Aim + FVector(-700.f, -500.f, 1500.f);
			FHitResult Hit;
			const bool bHit = CursorHit(*World, Eye, Aim, *Leader, Hit);
			FVector Target;
			FString Why;
			const bool bTarget = bHit && Roofs->FindStairUpTarget(Hit, *Leader, Target, &Why);
			Check(bTarget, FString::Printf(TEXT("S: the cursor on the flight (hit %s %s at %s, stair %d) gives the stair-up target (%s)"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("-"),
				bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT(""), *Hit.ImpactPoint.ToCompactString(), bHit && Hit.GetComponent() && IsStair(*Hit.GetComponent()) ? 1 : 0,
				bTarget ? *Target.ToCompactString() : *Why));
			PC->bFreezeMantleHoverForTesting = true;
			Check(bHit && PC->UpdateStairHover(Leader, &Hit) && PC->IsStairHoverShown(), TEXT("S: the hover silhouette shows on the storey above"));
			DescribeGhost(TEXT("S"), PC->GetStairHoverGhost(), Target + FVector(0.f, 0.f, 90.f));
			Shot(*World, *PC, Eye, Aim, TEXT("stair_hover"));
			R.PendingClick = Hit;
			Go(EStep::StairShot);
			return true;
		}
		case EStep::StairShot:
			if (R.StepFrame < (R.bShots ? 20 : 1))
			{
				return true;
			}
			PC->bFreezeMantleHoverForTesting = false;
			PC->UpdateStairHover(nullptr, nullptr);
			if (R.PendingClick.bBlockingHit)
			{
				PC->HandlePlayerClick(R.PendingClick);
			}
			Go(EStep::StairWalk);
			return true;
		case EStep::StairWalk:
		{
			const bool bUp = FeetZ(*Leader) >= R.Storey1Z - 15.f;
			if (!bUp && R.StepFrame < 60 * 30)
			{
				return true;
			}
			Check(bUp, FString::Printf(TEXT("S: the click took him up to storey 1 (feet %.0f, storey 1 at %.0f, %.1f s)"), FeetZ(*Leader), R.Storey1Z, R.StepFrame / 60.f));
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
		for (const FString& Arg : Args)
		{
			Runner->bShots |= Arg == TEXT("shots=1");
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([]() { Tick(); });
		UE_LOG(LogCodexTactics, Display, TEXT("WindowStairHoverSmoke: start"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WindowStairHoverSmoke"),
		TEXT("Dev check (designer test 2026-11-22): the window silhouette + window click (through, breaks it) and the stair-up silhouette + click (up a storey) with ")
		TEXT("real cursor rays and the real click. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
