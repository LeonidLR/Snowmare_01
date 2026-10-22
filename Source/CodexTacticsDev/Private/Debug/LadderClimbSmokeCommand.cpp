// Dev-only check of the ladders (docs/design/LADDERS.md; user request 2026-10-22 "on the new map a ladder against a wall and a ladder without
// a wall"), on campaign level 0.1 with its two test ladders (Scripts/Editor/place_factory_test_ladders.py):
//   Scripts/smoke_parallel.ps1 -Commands LadderClimbSmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
//  1. the placed ladders: the wall ladder 11 rungs with brackets, the free-standing one 8 rungs without; both climbable, their link points on
//     the navmesh, a navmesh path from the start up onto each roof; a spawned 5-rung ladder is decoration (no link, not climbable);
//  2. for each ladder: a REAL click (ACodexTacticsPlayerController::HandleWorldHit) on the roof -> he walks to the ladder, AlignBottom ->
//     ClimbUp -> ExitTop -> standing on the roof; a click on the ground -> AlignTop -> EnterTop (IntoFromTop) -> ClimbDown -> ExitBottom ->
//     on the ground. Never below the floor, standing on the roof / ground after each leg; while climbing the gates hold (bIsClimbing, left-hand
//     IK 0, aim offset 0, the FullBody slot owns the body, the weapon hidden) and the gripping hands are within 3 cm of their rung grip;
//  3. a knockdown on the way down (feet ~2 m up): he falls to the bottom, lands knocked down, fall damage = clamp((H - 100) * 0.1, 0, 40).
// 'Clip' (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_B_FrozenFactory -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
// -benchmark -fps=30 -ExecCmds="CodexTactics.LadderClimbSmoke Clip"): the two ladders up and down filmed by a camera that follows the climber
// -> Saved/Screenshots/Ladder/frames/f_NNNN.png (in-engine frames only). Godot: none (UE-only system).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderRules.h"
#include "Ladders/LadderSubsystem.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavLinkCustomComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace LadderClimbSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FLeg
	{
		bool bWall = true;
		bool bUp = true;
		bool bKnockdown = false;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bClip = false;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<ALadderActor> Wall;
		TWeakObjectPtr<ALadderActor> Free;
		// Per leg.
		uint32 SeenStates = 0;
		float MinFeetOverFloor = 1e6f;
		float MaxHandError = 0.f;
		int32 HandSamples = 0;
		bool bGatesChecked = false;
		float HealthBefore = 0.f;
		float KnockFeetZ = -1.f;
		float DoneAt = -1.f;
		FVector Goal = FVector::ZeroVector;
		// Clip.
		bool bRecording = false;
		int32 FrameCount = 0;
		FString FrameDir;
		FDelegateHandle FrameHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector CameraEye = FVector::ZeroVector;
		FVector CameraLook = FVector::ZeroVector;
		bool bCameraPlaced = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (State.FrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
			UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] clip: %d frames in %s"), State.FrameCount, *State.FrameDir);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LadderClimbSmoke"));
		return false;
	}

	ALadderActor* LegLadder(const FState& State)
	{
		return State.Legs.IsValidIndex(State.Leg) ? (State.Legs[State.Leg].bWall ? State.Wall.Get() : State.Free.Get()) : nullptr;
	}

	/** A real click: a Visibility trace down onto Point (the roof / the ground), handed to the controller like the mouse's. */
	bool ClickAt(UWorld* World, const FVector& Point)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		FHitResult Hit;
		if (!PC || !World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 300.f), Point - FVector(0.f, 0.f, 300.f), ECC_Visibility))
		{
			return false;
		}
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] click on %s at %s"), *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString());
		return true;
	}

	/** Gripping hands (IK alpha 1): the wrist's distance to its rung grip point (lateral excluded - the hand may hold anywhere between the rails). */
	void SampleHands(FState& State, const AOperativeCharacter& Leader, const ULadderClimbComponent& Climb)
	{
		const FLadderIKFrame& Frame = Climb.GetIKFrame();
		const USkeletalMeshComponent* Mesh = Leader.GetMesh();
		if (!Frame.bActive || !Mesh)
		{
			return;
		}
		const TCHAR* Bones[] = { TEXT("hand_l"), TEXT("hand_r") };
		for (int32 Limb = 0; Limb < 2; ++Limb)
		{
			if (Frame.Alpha[Limb] < 0.99f)
			{
				continue;
			}
			const FVector Rel = Mesh->GetBoneLocation(Bones[Limb]) - Frame.RungCentre[Limb];
			const float Out = static_cast<float>(FVector::DotProduct(Rel, Frame.Outward)) - Frame.HandOffset.X;
			const float Up = static_cast<float>(Rel.Z) - Frame.HandOffset.Y;
			State.MaxHandError = FMath::Max(State.MaxHandError, FMath::Sqrt(Out * Out + Up * Up));
			if (State.HandSamples < 6 || State.HandSamples % 40 == 0)
			{
				const FVector Root = Mesh->GetBoneLocation(TEXT("root"));
				UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] grip %s: rung %d t %.2f, wrist %.1f out / %.1f up of the grip; root %.1f over the feet, %.1f out of the hang line"), Bones[Limb],
					Climb.GetLoopPos().Rung, Climb.GetLoopPos().Time, Out, Up, Root.Z - (Leader.GetActorLocation().Z - Leader.GetSimpleCollisionHalfHeight()),
					FVector::DotProduct(Root - Leader.GetActorLocation(), Frame.Outward));
			}
			++State.HandSamples;
		}
	}

	void StartLeg(FState& State, UWorld* World)
	{
		ALadderActor* Ladder = LegLadder(State);
		AOperativeCharacter* Leader = State.Leader.Get();
		const FLeg& Leg = State.Legs[State.Leg];
		State.SeenStates = 0;
		State.MinFeetOverFloor = 1e6f;
		State.MaxHandError = 0.f;
		State.HandSamples = 0;
		State.bGatesChecked = false;
		State.KnockFeetZ = -1.f;
		State.DoneAt = -1.f;
		// Up: the roof's middle, a metre beyond the top link point; down: the ground 2.5 m out from the bottom link.
		State.Goal = Leg.bUp ? Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 100.f : Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 250.f;
		const bool bOrdered = ClickAt(World, State.Goal);
		Check(State, bOrdered, FString::Printf(TEXT("leg %d: click on the %s by the %s ladder"), State.Leg, Leg.bUp ? TEXT("roof") : TEXT("ground"),
			Leg.bWall ? TEXT("wall") : TEXT("free-standing")));
		if (Leader && Leader->HealthComponent)
		{
			State.HealthBefore = Leader->HealthComponent->GetCurrentHealth();
		}
	}

	/** One leg; returns true when it is over (checks posted). */
	bool TickLeg(FState& State, UWorld* World)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		ALadderActor* Ladder = LegLadder(State);
		ULadderClimbComponent* Climb = Leader ? Leader->LadderClimb.Get() : nullptr;
		if (!Leader || !Ladder || !Climb)
		{
			Check(State, false, TEXT("leader / ladder / climb component"));
			return true;
		}
		const FLeg& Leg = State.Legs[State.Leg];
		const ELadderClimbState ClimbState = Climb->GetState();
		State.SeenStates |= 1u << static_cast<uint32>(ClimbState);
		const float HalfHeight = Leader->GetSimpleCollisionHalfHeight();
		const float FeetOverFloor = static_cast<float>(Leader->GetActorLocation().Z) - HalfHeight - Ladder->GetFloorZ();
		if (Climb->IsOnLadder() && Climb->GetLadder() == Ladder)
		{
			State.MinFeetOverFloor = FMath::Min(State.MinFeetOverFloor, FeetOverFloor);
		}
		if (ClimbState == ELadderClimbState::ClimbUp || ClimbState == ELadderClimbState::ClimbDown)
		{
			SampleHands(State, *Leader, *Climb);
			const UOperativeAnimInstance* Anim = Leader->GetMesh() ? Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()) : nullptr;
			if (!State.bGatesChecked && Anim && Climb->GetIKFrame().bActive && State.StageTime > 1.f)
			{
				State.bGatesChecked = true;
				const float SlotWeight = Anim->GetSlotMontageGlobalWeight(TEXT("FullBody"));
				Check(State, Anim->bIsClimbing && Anim->LeftHandIKAlpha < 0.01f && Anim->AimOffsetAlpha < 0.01f && SlotWeight > 0.9f
					&& !(Leader->WeaponMesh && Leader->WeaponMesh->IsVisible()),
					FString::Printf(TEXT("leg %d: climbing gates - bIsClimbing %d, left-hand IK %.2f, aim offset %.2f, FullBody slot %.2f, weapon hidden %d"), State.Leg,
						Anim->bIsClimbing ? 1 : 0, Anim->LeftHandIKAlpha, Anim->AimOffsetAlpha, SlotWeight, Leader->WeaponMesh && !Leader->WeaponMesh->IsVisible() ? 1 : 0));
			}
			// The knockdown leg: on the way down, feet ~2 m up.
			if (Leg.bKnockdown && State.KnockFeetZ < 0.f && ClimbState == ELadderClimbState::ClimbDown && FeetOverFloor > 150.f)
			{
				State.KnockFeetZ = FeetOverFloor;
				State.HealthBefore = Leader->HealthComponent ? Leader->HealthComponent->GetCurrentHealth() : 0.f;
				const bool bKnocked = Leader->KnockdownComponent && Leader->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyMelee);
				Check(State, bKnocked && Climb->IsDropping(), FString::Printf(TEXT("knocked down %.0f cm up the ladder: he drops (state %d)"), FeetOverFloor,
					static_cast<int32>(Climb->GetState())));
			}
		}
		if (State.StageTime > 60.f)
		{
			Check(State, false, FString::Printf(TEXT("leg %d timed out in state %d at %s (moving %d, %.0f cm from the goal)"), State.Leg,
				static_cast<int32>(ClimbState), *Leader->GetActorLocation().ToCompactString(), Leader->IsMoving() ? 1 : 0,
				FVector::Dist2D(Leader->GetActorLocation(), State.Goal)));
			return true;
		}
		const bool bExitSeen = Leg.bKnockdown ? (State.SeenStates & (1u << static_cast<uint32>(ELadderClimbState::Dropping))) != 0
			: Leg.bUp ? (State.SeenStates & (1u << static_cast<uint32>(ELadderClimbState::ExitTop))) != 0
			: (State.SeenStates & (1u << static_cast<uint32>(ELadderClimbState::ExitBottom))) != 0;
		if (!bExitSeen || ClimbState != ELadderClimbState::None || Leader->GetVelocity().Size2D() > 10.f || Leader->IsMoving())
		{
			State.DoneAt = -1.f;
			return false;
		}
		if (State.DoneAt < 0.f)
		{
			State.DoneAt = State.StageTime;
		}
		if (State.StageTime - State.DoneAt < 1.f)
		{
			return false; // settled for a second (no falling through the roof)
		}
		const auto Seen = [&State](ELadderClimbState S) { return (State.SeenStates & (1u << static_cast<uint32>(S))) != 0; };
		const TCHAR* Name = Leg.bWall ? TEXT("wall") : TEXT("free-standing");
		if (Leg.bKnockdown)
		{
			const float Expected = LadderRules::FallDamage(Climb->GetLastDropHeight());
			const float Lost = State.HealthBefore - (Leader->HealthComponent ? Leader->HealthComponent->GetCurrentHealth() : 0.f);
			Check(State, FMath::Abs(Climb->GetLastDropHeight() - State.KnockFeetZ) < 25.f && FMath::Abs(FeetOverFloor) < 15.f,
				FString::Printf(TEXT("he fell to the bottom (%.0f cm fall from %.0f cm; feet %.0f cm over the floor)"), Climb->GetLastDropHeight(), State.KnockFeetZ, FeetOverFloor));
			Check(State, Expected > 0.f && FMath::IsNearlyEqual(Lost, Expected, 0.6f) && FMath::IsNearlyEqual(Climb->GetLastFallDamage(), Expected, 0.01f),
				FString::Printf(TEXT("fall damage = clamp((H - 100) * 0.1, 0, 40) = %.1f HP (lost %.1f)"), Expected, Lost));
			Check(State, Leader->IsKnockedDown(), TEXT("he lies knocked down after the landing (the knockdown that caused the fall)"));
			return true;
		}
		const bool bStates = Leg.bUp
			? Seen(ELadderClimbState::AlignBottom) && Seen(ELadderClimbState::ClimbUp) && Seen(ELadderClimbState::ExitTop)
			: Seen(ELadderClimbState::AlignTop) && Seen(ELadderClimbState::EnterTop) && Seen(ELadderClimbState::ClimbDown) && Seen(ELadderClimbState::ExitBottom);
		Check(State, bStates, FString::Printf(TEXT("leg %d (%s, %s): the climb ran %s (states seen 0x%x)"), State.Leg, Name, Leg.bUp ? TEXT("up") : TEXT("down"),
			Leg.bUp ? TEXT("AlignBottom -> ClimbUp -> ExitTop") : TEXT("AlignTop -> EnterTop -> ClimbDown -> ExitBottom"), State.SeenStates));
		const float LevelZ = Leg.bUp ? Ladder->GetTopWorldZ() : Ladder->GetFloorZ();
		const float FeetOverLevel = static_cast<float>(Leader->GetActorLocation().Z) - HalfHeight - LevelZ;
		Check(State, Leader->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(FeetOverLevel) < 15.f
			&& FVector::Dist2D(Leader->GetActorLocation(), State.Goal) < 150.f,
			FString::Printf(TEXT("leg %d: standing %s at the click (feet %.0f cm off the level, %.0f cm from the goal)"), State.Leg, Leg.bUp ? TEXT("on the roof") : TEXT("on the ground"),
				FeetOverLevel, FVector::Dist2D(Leader->GetActorLocation(), State.Goal)));
		Check(State, State.MinFeetOverFloor > -10.f, FString::Printf(TEXT("leg %d: never below the ladder's floor on the ladder (min feet %.1f cm)"), State.Leg, State.MinFeetOverFloor));
		Check(State, State.HandSamples > 10 && State.MaxHandError <= 3.f, FString::Printf(TEXT("leg %d: gripping hands within 3 cm of their rung grip (max %.2f cm, %d samples)"),
			State.Leg, State.MaxHandError, State.HandSamples));
		Check(State, Climb->GetLastRefusal() == ELadderRefusal::None && !Ladder->GetLeaseHolder(), TEXT("no refusal, the ladder's lease released"));
		return true;
	}

	/** Clip: a camera beside the ladder of the current leg, following the climber (smoothed), one frame per rendered frame. */
	void OnEndFrame(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AOperativeCharacter* Leader = State->Leader.Get();
		const ALadderActor* Ladder = LegLadder(*State);
		if (!PC || !Leader || !Ladder || !State->bRecording)
		{
			return;
		}
		const float Delta = 1.f / 30.f;
		const FVector Out = Ladder->GetOutward();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
		const FVector Look = Leader->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const FVector Eye = Look + (Out * 0.8f + Side * 0.6f).GetSafeNormal() * 520.f + FVector(0.f, 0.f, 170.f);
		if (!State->bCameraPlaced)
		{
			State->CameraEye = Eye;
			State->CameraLook = Look;
			State->bCameraPlaced = true;
		}
		const float Alpha = 1.f - FMath::Exp(-3.f * Delta);
		State->CameraEye = FMath::Lerp(State->CameraEye, Eye, Alpha * 0.6f);
		State->CameraLook = FMath::Lerp(State->CameraLook, Look, Alpha);
		ACameraActor* Camera = State->Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(State->CameraEye, (State->CameraLook - State->CameraEye).Rotation());
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(55.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			}
			State->Camera = Camera;
		}
		if (Camera)
		{
			Camera->SetActorLocationAndRotation(State->CameraEye, (State->CameraLook - State->CameraEye).Rotation());
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
		}
		// Jump cuts keep the video short: the long walks between the ladders (frames only within 4.5 m of the leg's ladder) and the
		// standing still between the legs are left out.
		const bool bStill = !Leader->IsClimbingLadder() && Leader->GetVelocity().Size2D() < 5.f && State->FrameCount > 15;
		if ((FVector::Dist2D(Leader->GetActorLocation(), Ladder->GetBottomLinkWorld()) < 450.f || Leader->IsClimbingLadder()) && !bStill)
		{
			FScreenshotRequest::RequestScreenshot(State->FrameDir / FString::Printf(TEXT("f_%04d.png"), State->FrameCount++), /*bShowUI*/ false, false);
		}
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 420.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			// The level's start (fade-in, squad spawned on the navmesh) settles.
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad || !Squad->GetLeader() || State.StageTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			State.Leader = Squad->GetLeader();
			// Headless: no frame is rendered, so the bones must be refreshed every tick to measure the hands (rung IK).
			State.Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			const ULadderSubsystem* Ladders = World->GetSubsystem<ULadderSubsystem>();
			for (ALadderActor* Ladder : Ladders ? Ladders->GetLadders() : TArray<ALadderActor*>())
			{
				(Ladder->HasWall() ? State.Wall : State.Free) = Ladder;
			}
			ALadderActor* Wall = State.Wall.Get();
			ALadderActor* Free = State.Free.Get();
			Check(State, Wall && Free, TEXT("the map's two test ladders (one against a wall, one free-standing) registered"));
			if (!Wall || !Free)
			{
				return Finish(State, false);
			}
			Check(State, Wall->GetRungCount() == 11 && Wall->GetRungInstanceCount() == 11 && Wall->GetBracketInstanceCount() == 6 && Wall->IsClimbable(),
				FString::Printf(TEXT("wall ladder: %d rungs drawn, %d brackets, top %.1f cm"), Wall->GetRungInstanceCount(), Wall->GetBracketInstanceCount(), Wall->GetTopWorldZ() - Wall->GetFloorZ()));
			Check(State, Free->GetRungCount() == 8 && Free->GetRungInstanceCount() == 8 && Free->GetBracketInstanceCount() == 0 && Free->IsClimbable() && !Free->HasWall(),
				FString::Printf(TEXT("free-standing ladder: %d rungs, no brackets, no wall, top %.1f cm"), Free->GetRungInstanceCount(), Free->GetTopWorldZ() - Free->GetFloorZ()));
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (ALadderActor* Ladder : { Wall, Free })
			{
				FNavLocation Bottom, Top;
				const bool bBottom = Nav && Nav->ProjectPointToNavigation(Ladder->GetBottomLinkWorld(), Bottom, FVector(40.f, 40.f, 60.f));
				const bool bTop = Nav && Nav->ProjectPointToNavigation(Ladder->GetTopLinkWorld(), Top, FVector(40.f, 40.f, 60.f));
				Check(State, bBottom && bTop && Ladder->GetNavLink()->IsEnabled(), FString::Printf(TEXT("%s: both link points on the navmesh (%d / %d), link on"),
					*Ladder->GetName(), bBottom ? 1 : 0, bTop ? 1 : 0));
			}
			// A decoration ladder (5 rungs): no link, nothing climbs it.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FTransform Spot(FRotator(0.f, 90.f, 0.f), FVector(-1500.f, -6300.f, Wall->GetFloorZ()));
			if (ALadderActor* Short = World->SpawnActorDeferred<ALadderActor>(ALadderActor::StaticClass(), Spot))
			{
				Short->Height = 5.f * 36.4f;
				Short->FinishSpawning(Spot);
				Check(State, Short->GetRungCount() == 5 && !Short->IsClimbable() && !Short->GetNavLink()->IsEnabled(),
					TEXT("a 5-rung ladder is decoration: not climbable, its nav link off"));
			}
			State.Legs = { { true, true }, { true, false }, { false, true }, { false, false } };
			if (!State.bClip)
			{
				State.Legs.Add({ true, true });
				State.Legs.Add({ true, false, true });
			}
			State.bRecording = State.bClip;
			if (State.bClip)
			{
				// The clip shows the climber alone (the holding followers would wander through the shots).
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member != State.Leader.Get())
					{
						Member->SetActorHiddenInGame(true);
						Member->SetActorEnableCollision(false);
					}
				}
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// A path from the leader up onto each roof runs through its ladder (the nav link, area UNavArea_Ladder).
			if (State.StageTime < 1.f)
			{
				return true;
			}
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (ALadderActor* Ladder : { State.Wall.Get(), State.Free.Get() })
			{
				const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, State.Leader->GetActorLocation(), Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 100.f) : nullptr;
				Check(State, Path && Path->IsValid() && !Path->IsPartial(), FString::Printf(TEXT("a full navmesh path from the start onto %s's top (%d points)"), *Ladder->GetName(),
					Path ? Path->PathPoints.Num() : 0));
			}
			State.Leg = 0;
			StartLeg(State, World);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
			if (!TickLeg(State, World))
			{
				return true;
			}
			++State.Leg;
			if (!State.Legs.IsValidIndex(State.Leg))
			{
				return Finish(State, true);
			}
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		case 3:
			// A short pause between the legs (the clip shows him standing on the roof / at the foot).
			if (State.StageTime < (State.bClip ? 0.5f : 0.3f))
			{
				return true;
			}
			StartLeg(State, World);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->bClip = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Clip"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		if (State->bClip)
		{
			State->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("Ladder") / TEXT("frames"));
			IFileManager::Get().DeleteDirectory(*State->FrameDir, false, true);
			IFileManager::Get().MakeDirectory(*State->FrameDir, true);
			State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { OnEndFrame(State); });
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LadderClimbSmoke"),
		TEXT("Dev check (2026-10-22, docs/design/LADDERS.md): on L_Demo01_B_FrozenFactory the leader clicks onto the wall ladder's roof and the free-standing "
			"ladder's deck and back down (real clicks, nav links, the climb FSM, clips, rung IK), a decoration ladder is not climbable, a knockdown on the way "
			"down drops him with the fall damage. 'Clip' = rendered frames into Saved/Screenshots/Ladder/frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
