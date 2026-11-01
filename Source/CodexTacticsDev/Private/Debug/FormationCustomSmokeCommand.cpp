// CodexTactics.FormationCustomSmoke - the custom formation in the game (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md
// "Custom formation", contract docs/design/FORMATION_CONTRACT.md 16) on /Game/Maps/L_FormationLab (-LevelJson=L_FormationLab.json).
// Godot reference: none (UE-only). The real input paths: hold B with the menu's virtual cursor on the Custom 1 disc (an empty slot -> the
// placement mode), the placement clicks through the controller's LMB down / up handlers (an operative, then a ground press + drag), Space
// confirms. Checks: the world stops while placing (and the tactical pause's charges are untouched), refused spots (too close / too far), the
// anchor moves the frame, the stored shape, each member keeps his place relative to the leader in the open (standing and walking), the looks
// follow the set directions, the fold into the trail column in lane D's 4.6 m corridor and the unfold after, Ctrl = edit (the stored ghosts,
// Esc keeps the slot), a three-man shape (the 4th on guard) and the 4th back as the extra behind it, the campaign carry (capture -> clear ->
// apply) and save -> clear -> load (in place) keep the three slots. Codex.Formation.Sectors 0: the custom row is inert (skipped, PASS).
// 'shot': rendered stills to Saved/Screenshots/FormationCustom/ (the placement mode with ghosts and look arrows; the squad walking the shape).

#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/SaveGameSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "GameFlow/CampaignCarry.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UI/FormationGhostActor.h"
#include "UI/FormationMenuWidget.h"
#include "UnrealClient.h"

namespace FormationCustomSmoke
{
	/** Lane A's open start and lane D's corridor approach (Content/Data/FormationLab/formation_lab_01.json), cm. */
	const FVector2D OpenStart(-5000.0, -4500.0);
	const FVector2D CorridorStart(-5200.0, 800.0);
	/** The lane D walk ends 1.7 m before the wide corridor (W_D_11 / 12 from x 1000), past the narrow one (to x -1000) by its hold distance. */
	const FVector2D CorridorEnd(700.0, 800.0);
	/** A member standing at his custom place: within this of it (the walk's stop radius + the crowd's push), cm. */
	constexpr double StandTolCm = 90.0;
	/** Walking: the p90 of a member's distance to his place (the follow's lag), cm. */
	constexpr double WalkTolCm = 170.0;
	/** A standing member's look within this of the shape's (no wall near), deg. */
	constexpr float LookTolDeg = 25.f;

	enum class EStage : uint8
	{
		Setup,
		OpenMenu,
		Place,
		Confirm,
		Settle,
		Walk,
		WalkStop,
		Corridor,
		Edit,
		Three,
		ThreeWalk,
		Carry,
		Save,
		Done
	};

	/** One planned placement: the operative (by follower index; -1 = the leader), the spot and the drag in the leader's frame at the start. */
	struct FPlacement
	{
		int32 Who = 0;
		FVector2D Spot;
		/** The drag's look relative to the heading; unset = a plain click (the squad's heading). */
		TOptional<float> Look;
		/** The expected answer. */
		EFormationPlacementResult Expect = EFormationPlacementResult::Placed;
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 Index = 0;
		bool bShot = false;
		bool bShotTaken = false;
		int32 ShotFrames = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Followers;
		TWeakObjectPtr<AOperativeCharacter> Fourth;
		FVector2D Origin;
		float Heading = 0.f;
		TArray<FPlacement> Plan;
		FFormationCustomShape Expected;
		TArray<double> WalkErrors;
		bool bSawTrail = false;
		bool bSawUnfold = false;
		bool bSawCorridorHud = false;
		bool bWalkShotDone = false;
		int32 NotCustomFrames = 0;
		FFormationCustomShape EditStored;
		TArray<FFormationCustomGhost> EditGhosts;
		float GroundZ = 0.f;
		TArray<FString> SlotsBefore;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.Index = 0;
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		if (UWorld* W = World)
		{
			UGameplayStatics::SetGlobalTimeDilation(W, 1.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("FormationCustomSmoke"));
		return false;
	}

	void SendKey(APlayerController* PC, const FKey& Which, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Which, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	FVector2D Frame(const FVector2D& Origin, float Heading, const FVector2D& Local)
	{
		const FVector2D F = FormationMath::Forward(Heading);
		const FVector2D R = FormationMath::Right(Heading);
		return Origin + F * Local.X + R * Local.Y;
	}

	/** A ground hit at a 2D point (the controller's LMB down sees the visibility trace's hit; the smoke builds the same). */
	FHitResult GroundHit(UWorld* World, const FVector2D& Point, float AroundZ)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationCustomSmoke), false);
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point, AroundZ + 300.f), FVector(Point, AroundZ - 500.f), ECC_Visibility, Params))
		{
			Hit = FHitResult();
			Hit.ImpactPoint = FVector(Point, AroundZ);
			Hit.Location = Hit.ImpactPoint;
			Hit.ImpactNormal = FVector::UpVector;
			Hit.bBlockingHit = true;
		}
		return Hit;
	}

	/** A click on an operative's body (the hit actor is he). */
	FHitResult BodyHit(AOperativeCharacter& Operative)
	{
		FHitResult Hit(&Operative, Operative.GetCapsuleComponent(), Operative.GetActorLocation(), FVector::UpVector);
		Hit.ImpactPoint = Operative.GetActorLocation();
		Hit.bBlockingHit = true;
		return Hit;
	}

	void Teleport(UWorld* World, FState& State, AOperativeCharacter& Each, const FVector2D& Spot, float Yaw)
	{
		Each.LeaveCover(TEXT("smoke reset"));
		Each.StopOperative();
		const FVector Location(Spot.X, Spot.Y, State.GroundZ + Each.GetSimpleCollisionHalfHeight() + 5.f);
		Each.TeleportTo(Location, FRotator(0.f, Yaw, 0.f), false, true);
	}

	/** The squad at Start facing Yaw in a loose diamond (the lab smoke's start), the formation state reset. */
	void PlaceSquad(UWorld* World, FState& State, const FVector2D& Start, float Yaw)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		Teleport(World, State, *Leader, Start, Yaw);
		const FVector2D Offsets[] = { FVector2D(-280.0, 260.0), FVector2D(-280.0, -260.0), FVector2D(-560.0, 0.0) };
		int32 Index = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Followers)
		{
			if (AOperativeCharacter* Each = Member.Get(); Each && !Each->bGuarding)
			{
				Teleport(World, State, *Each, Frame(Start, Yaw, Offsets[FMath::Min(Index++, 2)]), Yaw);
			}
		}
		Squad->ResetFormationState(FRotator(0.f, Yaw, 0.f).Vector());
	}

	/** The world spot / look the shape gives a follower now (the slot of the current frame), via GetFormationTarget. */
	bool PlaceOf(const USquadSubsystem& Squad, const AOperativeCharacter& Member, FVector2D& OutSpot, float& OutLook)
	{
		FVector Slot;
		if (!Squad.GetFormationTarget(&Member, Slot, OutLook))
		{
			return false;
		}
		OutSpot = FVector2D(Slot);
		return true;
	}

	/** The shape's expected local place of a follower (squad index) relative to the leader; false when none. */
	bool ExpectedLocal(const FFormationCustomShape& Shape, int32 Squad, FVector2D& OutLocal, float& OutLook)
	{
		const FFormationCustomMember* Member = Shape.Find(Squad);
		if (!Member)
		{
			return false;
		}
		OutLocal = FVector2D(Member->Fwd, Member->Right);
		OutLook = Member->Look;
		return true;
	}

	/** Every follower stands at his custom place relative to the leader (and looks as set when bLooks). */
	void CheckStanding(FState& State, USquadSubsystem& Squad, const FFormationCustomShape& Shape, const TCHAR* Where, bool bLooks)
	{
		const AOperativeCharacter* Leader = Squad.GetLeader();
		const float Heading = Squad.GetFormationHeadingYaw();
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Followers)
		{
			const AOperativeCharacter* Each = Member.Get();
			FVector2D Local;
			float Look = 0.f;
			if (!Each || Each->bGuarding || !ExpectedLocal(Shape, Each->SquadIndex, Local, Look))
			{
				continue;
			}
			const FVector2D Want = Frame(FVector2D(Leader->GetActorLocation()), Heading, Local);
			const double Off = FVector2D::Distance(FVector2D(Each->GetActorLocation()), Want);
			Check(State, Off <= StandTolCm, FString::Printf(TEXT("%s: %s at his custom place (%.0f cm off; fwd %.0f right %.0f)"), Where, *Each->DisplayName.ToString(), Off,
				Local.X, Local.Y));
			if (bLooks)
			{
				const float Facing = static_cast<float>(Each->GetSightFacing().Rotation().Yaw);
				const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, Heading + Look));
				Check(State, Delta <= LookTolDeg, FString::Printf(TEXT("%s: %s looks his set direction (%.0f vs %.0f, %.0f deg off)"), Where, *Each->DisplayName.ToString(),
					Facing, FRotator::NormalizeAxis(Heading + Look), Delta));
			}
		}
	}

	bool StandingStill(const USquadSubsystem& Squad)
	{
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			if (Each && !Each->bGuarding && (Each->IsMoving() || Each->GetVelocity().Size2D() > 15.f))
			{
				return false;
			}
		}
		return true;
	}

	/** A rendered still from above (shot runs only): the camera over Target looking down at Pitch. True when taken (after a few frames). */
	bool Still(UWorld* World, FState& State, const FString& Name, const FVector& Target, float Yaw, float Pitch, float Distance)
	{
		if (!State.bShot)
		{
			return true;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			return true;
		}
		if (!State.Camera.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Camera = World->SpawnActor<ACameraActor>(Target, FRotator::ZeroRotator, Params);
		}
		const FRotator View(Pitch, Yaw, 0.f);
		State.Camera->GetCameraComponent()->SetFieldOfView(60.f);
		State.Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		State.Camera->SetActorLocationAndRotation(Target - View.Vector() * Distance, View);
		PC->SetViewTarget(State.Camera.Get());
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = true;
		}
		++State.ShotFrames;
		const bool bShadersDone = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
		// 60 frames: the shaders settle and a refused ghost's red flash (0.6 s real time) is over.
		if (!State.bShotTaken && State.ShotFrames >= 60 && (bShadersDone || State.ShotFrames >= 900))
		{
			State.bShotTaken = true;
			State.ShotFrames = 0;
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/FormationCustom") / (Name + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
		}
		if (State.bShotTaken && State.ShotFrames >= 15)
		{
			State.bShotTaken = false;
			State.ShotFrames = 0;
			return true;
		}
		return false;
	}

	void EndStill(UWorld* World, FState& State)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (State.bShot && PC && PC->GetPawn())
		{
			PC->SetViewTarget(PC->GetPawn());
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		UWorld* World = WeakWorld.Get();
		if (!World || State.Stage == EStage::Done)
		{
			return false;
		}
		State.StageTime += DeltaTime;
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!PC || !Squad || !Leader)
		{
			if (State.StageTime > 20.f)
			{
				Check(State, false, TEXT("a controller, the squad and its leader"));
				return Finish(State, World, false);
			}
			return true;
		}
		const auto TimedOut = [&State](float Seconds, const TCHAR* What)
		{
			if (State.StageTime <= Seconds)
			{
				return false;
			}
			Check(State, false, FString::Printf(TEXT("timed out: %s"), What));
			return true;
		};

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (!FormationRuntime::IsEnabled())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: Codex.Formation.Sectors is 0 - the custom formation is inert (no custom row): skipped"));
				PC->SetFormationMenuCursorOffset(FVector2D(0.0, FormationMenuRules::CustomRowY));
				return Finish(State, World, true);
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			if (ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>(); GameMode && Squad->GetMembers().Num() < 4)
			{
				State.Fourth = GameMode->SpawnOperative(GameMode->RecruitSusanin, 3, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 560.f,
					Leader->GetActorRotation(), true);
			}
			State.GroundZ = static_cast<float>(Leader->GetActorLocation().Z) - Leader->GetSimpleCollisionHalfHeight();
			State.Followers.Reset();
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				if (Each != Leader)
				{
					State.Followers.Add(Each);
					State.Fourth = State.Fourth.IsValid() ? State.Fourth : TWeakObjectPtr<AOperativeCharacter>(Each);
				}
			}
			Check(State, Squad->GetMembers().Num() == 4, FString::Printf(TEXT("4 operatives in the squad (%d)"), Squad->GetMembers().Num()));
			Check(State, !Squad->IsFormationCustomSlotFilled(1) && !Squad->IsFormationCustomSlotFilled(2) && !Squad->IsFormationCustomSlotFilled(3),
				TEXT("the three custom slots start empty"));
			PlaceSquad(World, State, OpenStart, 0.f);
			Enter(State, EStage::OpenMenu);
			return true;
		}
		case EStage::OpenMenu:
		{
			if (State.StageTime < 2.f)
			{
				return true; // settle, the heading valid
			}
			if (State.Index == 0)
			{
				PC->SetFormationMenuCursorOffset(FVector2D(-FormationMenuRules::CustomRowSpacing, FormationMenuRules::CustomRowY)); // the Custom 1 disc
				SendKey(PC, EKeys::B, IE_Pressed);
				State.Index = 1;
				return true;
			}
			if (State.Index == 1)
			{
				if (!PC->IsFormationMenuOpen())
				{
					return !TimedOut(5.f, TEXT("the hold-B menu opens")) || Finish(State, World, false);
				}
				const UFormationMenuWidget* Widget = PC->GetFormationMenuWidget();
				Check(State, Widget && Widget->IsCustomRowShown(), TEXT("the menu shows the custom row"));
				SendKey(PC, EKeys::B, IE_Released);
				State.Index = 2;
				State.StageTime = 0.f;
				return true;
			}
			if (State.Index == 2)
			{
				if (State.StageTime < 0.1f)
				{
					return true; // the release reaches the controller on its next tick
				}
				PC->SetFormationMenuCursorOffset(TOptional<FVector2D>());
				Check(State, PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Edit, TEXT("release on the empty Custom 1: the placement mode (effect edit)"));
				Check(State, PC->IsFormationPlacementActive() && PC->GetFormationPlacement().GetSlot() == 1, TEXT("the placement mode runs for Custom 1"));
				Check(State, UGameplayStatics::GetGlobalTimeDilation(World) <= 0.001f, FString::Printf(TEXT("time stopped while placing (dilation %.4f)"),
					UGameplayStatics::GetGlobalTimeDilation(World)));
				const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
				Check(State, !Flow || Flow->GetCombatMode() != ECodexCombatMode::TacticalPause, TEXT("not the tactical pause (no charges)"));
				Check(State, PC->GetFormationPlacement().GetGhosts().Num() == 4, FString::Printf(TEXT("a ghost per member (%d)"), PC->GetFormationPlacement().GetGhosts().Num()));
				int32 Shown = 0;
				for (const FFormationCustomGhost& Ghost : PC->GetFormationPlacement().GetGhosts())
				{
					const AFormationGhostActor* Actor = PC->GetFormationGhost(Ghost.Squad);
					Shown += Actor && Actor->IsShown() && !Actor->IsHidden() ? 1 : 0;
				}
				Check(State, Shown == 4, FString::Printf(TEXT("the hologram ghosts are shown (%d)"), Shown));
				State.Origin = FVector2D(Leader->GetActorLocation());
				State.Heading = PC->GetFormationPlacement().DefaultYaw();
				// The plan in the leader's frame: two refusals, three placements (two drags, one plain click), the anchor moved 1 m forward.
				State.Plan.Reset();
				State.Plan.Add({ 0, FVector2D(0.0, 70.0), TOptional<float>(), EFormationPlacementResult::TooClose }); // past the ghost pick radius (60), inside slot.min_spacing (80)
				State.Plan.Add({ 0, FVector2D(1200.0, 0.0), TOptional<float>(), EFormationPlacementResult::TooFar });
				State.Plan.Add({ 0, FVector2D(-200.0, 350.0), TOptional<float>(70.f), EFormationPlacementResult::Placed });
				State.Plan.Add({ 1, FVector2D(150.0, -300.0), TOptional<float>(-45.f), EFormationPlacementResult::Placed });
				State.Plan.Add({ 2, FVector2D(-500.0, -100.0), TOptional<float>(), EFormationPlacementResult::Placed });
				State.Plan.Add({ -1, FVector2D(100.0, 0.0), TOptional<float>(0.f), EFormationPlacementResult::Placed });
				Enter(State, EStage::Place);
			}
			return true;
		}
		case EStage::Place:
		{
			if (State.Index < State.Plan.Num())
			{
				const FPlacement& Step = State.Plan[State.Index];
				AOperativeCharacter* Who = Step.Who < 0 ? Leader : State.Followers.IsValidIndex(Step.Who) ? State.Followers[Step.Who].Get() : nullptr;
				if (!Who)
				{
					Check(State, false, TEXT("the planned operative"));
					return Finish(State, World, false);
				}
				// The real click path: LMB on his body (select), LMB down on the spot, LMB up at the drag's end.
				PC->HandleFormationPlacementPress(BodyHit(*Who));
				Check(State, PC->GetFormationPlacement().GetSelected() == TOptional<int32>(Who->SquadIndex), FString::Printf(TEXT("click on %s selects him"),
					*Who->DisplayName.ToString()));
				const FVector2D Spot = Frame(State.Origin, State.Heading, Step.Spot);
				PC->HandleFormationPlacementPress(GroundHit(World, Spot, State.GroundZ));
				const FVector2D DragTo = Step.Look.IsSet() ? Spot + FormationMath::Forward(State.Heading + Step.Look.GetValue()) * 160.0 : Spot + FVector2D(12.0, 5.0);
				PC->HandleFormationPlacementRelease(FVector(DragTo, State.GroundZ));
				const EFormationPlacementResult Result = PC->GetLastFormationPlacementResult();
				Check(State, Result == Step.Expect, FString::Printf(TEXT("placement %d of %s at (%.0f, %.0f): result %d, expected %d"), State.Index,
					*Who->DisplayName.ToString(), Step.Spot.X, Step.Spot.Y, static_cast<int32>(Result), static_cast<int32>(Step.Expect)));
				++State.Index;
				return true;
			}
			// The shape: the anchor moved 1 m forward -> the others' places 1 m further back; P / L / Z looks relative to the heading.
			const FFormationCustomShape Shape = PC->GetFormationPlacement().Shape();
			State.Expected = FFormationCustomShape();
			State.Expected.Members.Add({ Leader->SquadIndex, 0.f, 0.f, 0.f });
			const FVector4f Rows[] = { FVector4f(-300.f, 350.f, 70.f, 0.f), FVector4f(50.f, -300.f, -45.f, 0.f), FVector4f(-600.f, -100.f, 0.f, 0.f) };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (const AOperativeCharacter* Each = State.Followers[Index].Get())
				{
					State.Expected.Members.Add({ Each->SquadIndex, Rows[Index].X, Rows[Index].Y, Rows[Index].Z });
				}
			}
			bool bMatch = Shape.Members.Num() == State.Expected.Members.Num();
			for (const FFormationCustomMember& Want : State.Expected.Members)
			{
				const FFormationCustomMember* Got = Shape.Find(Want.Squad);
				bMatch &= Got && FMath::Abs(Got->Fwd - Want.Fwd) < 2.f && FMath::Abs(Got->Right - Want.Right) < 2.f
					&& FMath::Abs(FMath::FindDeltaAngleDegrees(Got->Look, Want.Look)) < 1.f;
			}
			Check(State, bMatch && Shape.Members[0].Squad == Leader->SquadIndex, TEXT("the placed shape (the leader the anchor, his move shifts the frame, the looks set)"));
			const FVector Middle(State.Origin, State.GroundZ);
			if (!Still(World, State, TEXT("01_placement_ghosts"), Middle + FVector(-100.0, 0.0, 0.0), 0.f, -50.f, 2200.f))
			{
				return true;
			}
			EndStill(World, State);
			Enter(State, EStage::Confirm);
			return true;
		}
		case EStage::Confirm:
		{
			if (State.Index == 0)
			{
				SendKey(PC, EKeys::SpaceBar, IE_Pressed);
				State.Index = 1;
				return true;
			}
			if (State.Index == 1)
			{
				SendKey(PC, EKeys::SpaceBar, IE_Released);
				State.Index = 2;
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			Check(State, !PC->IsFormationPlacementActive(), TEXT("Space confirms: the placement mode ends"));
			Check(State, UGameplayStatics::GetGlobalTimeDilation(World) > 0.9f, FString::Printf(TEXT("time resumes (dilation %.3f)"), UGameplayStatics::GetGlobalTimeDilation(World)));
			Check(State, Squad->IsFormationCustomSlotFilled(1), TEXT("Custom 1 stored"));
			Check(State, Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom1), TEXT("Custom 1 is the formation choice"));
			Enter(State, EStage::Settle);
			return true;
		}
		case EStage::Settle:
		{
			if (State.StageTime < 1.f || (!StandingStill(*Squad) && State.StageTime < 12.f))
			{
				return true;
			}
			Check(State, Squad->GetFormationKind() == EFormationKind::Custom && Squad->GetFormationCustomSlot() == 1, FString::Printf(TEXT("the squad in Custom 1 (%s)"),
				FormationNames::ToString(Squad->GetFormationKind())));
			Check(State, Squad->GetFormationHudText() == TEXT("Custom 1"), FString::Printf(TEXT("HUD \"%s\""), *Squad->GetFormationHudText()));
			Check(State, FVector2D::Distance(FVector2D(Leader->GetActorLocation()), Frame(State.Origin, State.Heading, FVector2D(100.0, 0.0))) < 90.0,
				TEXT("the leader walked to his moved spot"));
			CheckStanding(State, *Squad, Squad->GetFormationCustomShape(1), TEXT("after the confirm"), true);
			// Walk 25 m east across lane A's open ground (the real ground click).
			PC->HandleWorldHit(GroundHit(World, FVector2D(Leader->GetActorLocation()) + FVector2D(2500.0, 0.0), State.GroundZ));
			State.WalkErrors.Reset();
			Enter(State, EStage::Walk);
			return true;
		}
		case EStage::Walk:
		{
			if (State.StageTime > 1.5f && Leader->IsMoving())
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Followers)
				{
					FVector2D Spot;
					float Look = 0.f;
					if (const AOperativeCharacter* Each = Member.Get(); Each && PlaceOf(*Squad, *Each, Spot, Look))
					{
						State.WalkErrors.Add(FVector2D::Distance(FVector2D(Each->GetActorLocation()), Spot));
					}
				}
				State.NotCustomFrames += Squad->GetFormationKind() == EFormationKind::Custom ? 0 : 1;
			}
			if (State.bShot && !State.bWalkShotDone && State.StageTime > 5.f && Leader->IsMoving())
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 0.0001f); // the squad held mid-stride for the still
				if (!Still(World, State, TEXT("02_custom_shape_walking"), Leader->GetActorLocation() - FVector(150.0, 0.0, 0.0), 0.f, -50.f, 2200.f))
				{
					return true;
				}
				EndStill(World, State);
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				State.bWalkShotDone = true;
			}
			if (Leader->IsMoving() || State.StageTime < 2.f)
			{
				return State.StageTime < 60.f || (Check(State, false, TEXT("the open walk ends")), Finish(State, World, false));
			}
			State.WalkErrors.Sort();
			const double P90 = State.WalkErrors.IsEmpty() ? 0.0 : State.WalkErrors[FMath::Min(State.WalkErrors.Num() - 1, State.WalkErrors.Num() * 9 / 10)];
			Check(State, State.NotCustomFrames == 0, FString::Printf(TEXT("the open walk stays in the custom shape (%d frames not)"), State.NotCustomFrames));
			Check(State, State.WalkErrors.Num() > 30 && P90 <= WalkTolCm, FString::Printf(TEXT("walking: each keeps his relative place (p90 %.0f cm over %d samples)"), P90,
				State.WalkErrors.Num()));
			Enter(State, EStage::WalkStop);
			return true;
		}
		case EStage::WalkStop:
		{
			if (State.StageTime < 1.f || (!StandingStill(*Squad) && State.StageTime < 10.f))
			{
				return true;
			}
			CheckStanding(State, *Squad, Squad->GetFormationCustomShape(1), TEXT("after the open walk"), true);
			// Lane D: the narrow corridor (4.6 m) folds the shape into the trail column, it opens again after.
			PlaceSquad(World, State, CorridorStart, 0.f);
			Enter(State, EStage::Corridor);
			return true;
		}
		case EStage::Corridor:
		{
			if (State.Index == 0)
			{
				if (State.StageTime < 1.5f)
				{
					return true;
				}
				PC->HandleWorldHit(GroundHit(World, CorridorEnd, State.GroundZ));
				State.Index = 1;
				return true;
			}
			const float X = static_cast<float>(Leader->GetActorLocation().X);
			if (Squad->IsFormationTrail() && X > -3800.f && X < -1000.f)
			{
				State.bSawTrail = true;
				State.bSawCorridorHud |= Squad->GetFormationHudText().StartsWith(TEXT("Custom 1 -> Column"));
			}
			if (State.bSawTrail && Squad->GetFormationKind() == EFormationKind::Custom && X > -1000.f)
			{
				State.bSawUnfold = true;
			}
			if (Leader->IsMoving() || State.StageTime < 3.f)
			{
				return State.StageTime < 90.f || (Check(State, false, TEXT("the corridor walk ends")), Finish(State, World, false));
			}
			if (!StandingStill(*Squad) && State.StageTime < 100.f)
			{
				return true;
			}
			Check(State, State.bSawTrail, TEXT("the narrow corridor folds the custom shape into the trail column"));
			Check(State, State.bSawCorridorHud, TEXT("HUD \"Custom 1 -> Column (wall)\" while folded"));
			Check(State, State.bSawUnfold, TEXT("past the corridor the shape unfolds (the hold distance)"));
			Check(State, Squad->GetFormationKind() == EFormationKind::Custom, TEXT("standing past the corridor: Custom 1"));
			CheckStanding(State, *Squad, Squad->GetFormationCustomShape(1), TEXT("after the corridor"), false);
			// Ctrl on the filled Custom 1: the placement mode with its stored ghosts; Esc keeps the slot.
			PlaceSquad(World, State, OpenStart, 0.f);
			Enter(State, EStage::Edit);
			return true;
		}
		case EStage::Edit:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (State.Index == 0)
			{
				PC->SetSimulatedFormationMenuEdit(true);
				PC->SetFormationMenuCursorOffset(FVector2D(-FormationMenuRules::CustomRowSpacing, FormationMenuRules::CustomRowY));
				SendKey(PC, EKeys::B, IE_Pressed);
				State.Index = 1;
				return true;
			}
			if (State.Index == 1)
			{
				if (!PC->IsFormationMenuOpen())
				{
					return !TimedOut(5.f, TEXT("the menu opens for the edit")) || Finish(State, World, false);
				}
				SendKey(PC, EKeys::B, IE_Released);
				State.Index = 2;
				State.StageTime = 2.f;
				return true;
			}
			if (State.Index == 2)
			{
				if (State.StageTime < 2.1f)
				{
					return true;
				}
				PC->SetSimulatedFormationMenuEdit(false);
				PC->SetFormationMenuCursorOffset(TOptional<FVector2D>());
				Check(State, PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Edit && PC->IsFormationPlacementActive(), TEXT("Ctrl + release on the filled Custom 1: edit"));
				State.EditStored = Squad->GetFormationCustomShape(1);
				State.EditGhosts = PC->GetFormationPlacement().GetGhosts();
				State.Index = 3;
				State.StageTime = 2.f;
				SendKey(PC, EKeys::Escape, IE_Pressed);
				SendKey(PC, EKeys::Escape, IE_Released);
				return true;
			}
			if (State.StageTime < 2.1f)
			{
				return true;
			}
			const FFormationCustomShape& Stored = State.EditStored;
			const TArray<FFormationCustomSlot> Slots = FormationCustomRules::CustomSlots(Stored, Squad->GetFormationCustomPresent(), FVector2D(Leader->GetActorLocation()),
				Squad->GetFormationHeadingYaw());
			bool bGhostsAtShape = Slots.Num() == State.EditGhosts.Num();
			for (const FFormationCustomSlot& Slot : Slots)
			{
				const FFormationCustomGhost* Ghost = State.EditGhosts.FindByPredicate([&Slot](const FFormationCustomGhost& Each) { return Each.Squad == Slot.Squad; });
				bGhostsAtShape &= Ghost && FVector2D::Distance(Ghost->Position, Slot.Position) < 5.0;
			}
			Check(State, bGhostsAtShape, TEXT("editing: the ghosts stand at the stored shape"));
			Check(State, !PC->IsFormationPlacementActive() && UGameplayStatics::GetGlobalTimeDilation(World) > 0.9f, TEXT("Esc cancels the edit, time resumes"));
			Check(State, FormationCustomRules::ToJsonString(Squad->GetFormationCustomShape(1)) == FormationCustomRules::ToJsonString(Stored)
				&& Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom1), TEXT("the cancelled edit keeps Custom 1 and the choice"));
			// Three: the 4th on guard (out of the formation); Custom 2 for the three.
			if (AOperativeCharacter* Fourth = State.Fourth.Get(); Fourth && !Fourth->bGuarding)
			{
				Squad->ToggleGuard(Fourth);
				Fourth->TeleportTo(FVector(OpenStart.X - 2500.0, OpenStart.Y - 800.0, State.GroundZ + Fourth->GetSimpleCollisionHalfHeight() + 5.f), FRotator::ZeroRotator, false, true);
			}
			PlaceSquad(World, State, OpenStart, 0.f);
			Enter(State, EStage::Three);
			return true;
		}
		case EStage::Three:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (State.Index == 0)
			{
				PC->SetFormationMenuCursorOffset(FVector2D(0.0, FormationMenuRules::CustomRowY)); // Custom 2
				SendKey(PC, EKeys::B, IE_Pressed);
				State.Index = 1;
				return true;
			}
			if (State.Index == 1)
			{
				if (!PC->IsFormationMenuOpen())
				{
					return !TimedOut(5.f, TEXT("the menu opens for Custom 2")) || Finish(State, World, false);
				}
				SendKey(PC, EKeys::B, IE_Released);
				State.Index = 10;
				State.StageTime = 2.f;
				return true;
			}
			if (State.Index == 10)
			{
				if (State.StageTime < 2.1f)
				{
					return true;
				}
				PC->SetFormationMenuCursorOffset(TOptional<FVector2D>());
				Check(State, PC->IsFormationPlacementActive() && PC->GetFormationPlacement().GetSlot() == 2 && PC->GetFormationPlacement().GetGhosts().Num() == 3,
					FString::Printf(TEXT("Custom 2: the placement with three ghosts (%d)"), PC->GetFormationPlacement().GetGhosts().Num()));
				State.Origin = FVector2D(Leader->GetActorLocation());
				State.Heading = PC->GetFormationPlacement().DefaultYaw();
				// A line abreast, both looking out to the flanks.
				int32 Placed = 0;
				const FVector2D Spots[] = { FVector2D(0.0, 300.0), FVector2D(0.0, -300.0) };
				const float Looks[] = { 90.f, -90.f };
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Followers)
				{
					AOperativeCharacter* Each = Member.Get();
					if (!Each || Each->bGuarding || Placed >= 2)
					{
						continue;
					}
					PC->HandleFormationPlacementPress(BodyHit(*Each));
					const FVector2D Spot = Frame(State.Origin, State.Heading, Spots[Placed]);
					PC->HandleFormationPlacementPress(GroundHit(World, Spot, State.GroundZ));
					PC->HandleFormationPlacementRelease(FVector(Spot + FormationMath::Forward(State.Heading + Looks[Placed]) * 150.0, State.GroundZ));
					Check(State, PC->GetLastFormationPlacementResult() == EFormationPlacementResult::Placed, FString::Printf(TEXT("Custom 2: %s placed"), *Each->DisplayName.ToString()));
					++Placed;
				}
				// The menu's same slot confirms (as Space).
				PC->SetFormationMenuCursorOffset(FVector2D(0.0, FormationMenuRules::CustomRowY));
				SendKey(PC, EKeys::B, IE_Pressed);
				State.Index = 2;
				State.StageTime = 2.f;
				return true;
			}
			if (State.Index == 2)
			{
				if (!PC->IsFormationMenuOpen())
				{
					return !TimedOut(8.f, TEXT("the menu opens in the placement mode")) || Finish(State, World, false);
				}
				SendKey(PC, EKeys::B, IE_Released);
				State.Index = 11;
				State.StageTime = 2.f;
				return true;
			}
			if (State.Index == 11)
			{
				if (State.StageTime < 2.1f)
				{
					return true;
				}
				PC->SetFormationMenuCursorOffset(TOptional<FVector2D>());
				Check(State, !PC->IsFormationPlacementActive() && Squad->GetFormationCustomShape(2).Members.Num() == 3, TEXT("the same Custom 2 in the menu confirms a 3-man shape"));
				Check(State, Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom2), TEXT("Custom 2 picked"));
				State.Index = 3;
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 1.f || (!StandingStill(*Squad) && State.StageTime < 10.f))
			{
				return true;
			}
			CheckStanding(State, *Squad, Squad->GetFormationCustomShape(2), TEXT("three men, Custom 2"), true);
			PC->HandleWorldHit(GroundHit(World, FVector2D(Leader->GetActorLocation()) + FVector2D(1500.0, 0.0), State.GroundZ));
			Enter(State, EStage::ThreeWalk);
			return true;
		}
		case EStage::ThreeWalk:
		{
			if (Leader->IsMoving() || State.StageTime < 2.f || (!StandingStill(*Squad) && State.StageTime < 40.f))
			{
				return State.StageTime < 60.f || (Check(State, false, TEXT("the three-man walk ends")), Finish(State, World, false));
			}
			if (State.Index == 0)
			{
				CheckStanding(State, *Squad, Squad->GetFormationCustomShape(2), TEXT("three men after a walk"), true);
				// The 4th back: the shape lacks him - he stands behind the rearmost place.
				if (AOperativeCharacter* Fourth = State.Fourth.Get(); Fourth && Fourth->bGuarding)
				{
					Fourth->TeleportTo(Leader->GetActorLocation() - FVector(700.0, 0.0, 0.0), FRotator::ZeroRotator, false, true);
					Squad->ToggleGuard(Fourth);
				}
				State.Index = 1;
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 4.f)
			{
				return true;
			}
			FVector2D Spot;
			float Look = 0.f;
			const AOperativeCharacter* Fourth = State.Fourth.Get();
			const bool bPlace = Fourth && PlaceOf(*Squad, *Fourth, Spot, Look);
			const FVector2D Local = bPlace ? FVector2D(FVector2D::DotProduct(Spot - FVector2D(Leader->GetActorLocation()), FormationMath::Forward(Squad->GetFormationHeadingYaw())),
				FVector2D::DotProduct(Spot - FVector2D(Leader->GetActorLocation()), FormationMath::Right(Squad->GetFormationHeadingYaw()))) : FVector2D::ZeroVector;
			Check(State, bPlace && Squad->GetFormationKind() == EFormationKind::Custom && Local.X < -100.0 && FMath::Abs(Local.Y) < 20.0,
				FString::Printf(TEXT("the 4th (not in Custom 2) walks behind the shape (fwd %.0f right %.0f)"), Local.X, Local.Y));
			Enter(State, EStage::Carry);
			return true;
		}
		case EStage::Carry:
		{
			// The level change: the campaign carry's capture -> a fresh level's empty slots -> the arrival's apply (CampaignCarry, as SpawnSquad).
			FCampaignSquadCarry Carry;
			Carry.ToMap = TEXT("next");
			CampaignCarry::CaptureSquadState(*Squad, Carry);
			State.SlotsBefore = Squad->ExportFormationCustomSlots();
			const TOptional<EFormationMenuItem> ChoiceBefore = Squad->GetFormationMenuChoice();
			Squad->ImportFormationCustomSlots(TArray<FString>());
			Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>());
			CampaignCarry::ApplySquadState(*Squad, Carry);
			Check(State, Squad->ExportFormationCustomSlots() == State.SlotsBefore && Squad->GetFormationMenuChoice() == ChoiceBefore,
				TEXT("the campaign carry keeps the three slots and the choice"));
			Enter(State, EStage::Save);
			return true;
		}
		case EStage::Save:
		{
			USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
			if (!Saves)
			{
				Check(State, false, TEXT("the save game subsystem"));
				return Finish(State, World, false);
			}
			if (State.Index == 0)
			{
				Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves/FormationCustom");
				IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
				Check(State, Saves->SaveGame(TEXT("custom_slots")), TEXT("saved"));
				State.SlotsBefore = Squad->ExportFormationCustomSlots();
				Squad->ImportFormationCustomSlots(TArray<FString>());
				Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>());
				Check(State, Saves->LoadGame(TEXT("custom_slots")), TEXT("loaded (in place)"));
				State.Index = 1;
				return true;
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			USquadSubsystem* After = World->GetSubsystem<USquadSubsystem>();
			Check(State, After && After->ExportFormationCustomSlots() == State.SlotsBefore, TEXT("save -> load keeps the three custom slots"));
			Check(State, After && After->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom2), TEXT("save -> load keeps the Custom 2 choice"));
			IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
			return Finish(State, World, true);
		}
		default:
			return Finish(State, World, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Contains(TEXT("shot"));
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
			{
				return Step(WeakWorld, *State, DeltaTime);
			}));
		}), 3.f, false); // the nav mesh warms up (as FormationLabSmoke)
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationCustomSmoke"),
		TEXT("Dev check (2026-11-01): the custom formation - hold-B Custom 1 -> the placement mode (time stops, ghosts), placement by clicks + drags, ")
		TEXT("refusals, Space confirms, the shape kept walking in the open, the fold in lane D's corridor and the unfold, Ctrl = edit / Esc, a three-man ")
		TEXT("Custom 2 confirmed in the menu, the 4th as the extra, the campaign carry and save -> load keep the slots; [shot]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
