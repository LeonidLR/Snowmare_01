// Dev-only check of the ladders (docs/design/LADDERS.md; user request 2026-10-22 "on the new map a ladder against a wall and a ladder without
// a wall"), on campaign level 0.1 with its two test ladders (Scripts/Editor/place_factory_test_ladders.py):
//   Scripts/smoke_parallel.ps1 -Commands LadderClimbSmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
//  1. the placed ladders: the wall ladder 11 rungs with brackets, the free-standing one 8 rungs without; both climbable, their link points on
//     the navmesh, a navmesh path from the start up onto each roof; a spawned 5-rung ladder is decoration (no link, not climbable);
//  2. for each ladder: a REAL click (ACodexTacticsPlayerController::HandleWorldHit) on the roof -> he walks to the ladder, AlignBottom ->
//     ClimbUp -> ExitTop -> standing on the roof; a click on the ground -> AlignTop -> EnterTop (IntoFromTop) -> ClimbDown -> ExitBottom ->
//     on the ground. Never below the floor, standing on the roof / ground after each leg; while climbing the gates hold (bIsClimbing, left-hand
//     IK 0, aim offset 0, the FullBody slot owns the body, the weapon hidden) and the gripping hands are within 3 cm of their rung grip;
//  3. a knockdown on the way down (feet ~2 m up): he falls to the bottom, lands knocked down, fall damage = clamp((H - 100) * 0.1, 0, 40);
//     Each leg also (LADDERS.md 7.3): the camera height never pumps or jumps while he climbs (spring-smoothed focus Z: at most one
//     reversal of its vertical motion, <= 3 cm per frame), the floor reference of the roof cutaway / minimap stays the spot the climb
//     started from until he is off, the minimap floor does not change mid-ladder;
//  3b. turning back (2026-10-23): climbing up the wall ladder (2 rungs up) a click on the ground turns him round at the next rung and he
//     climbs back down (no ExitTop); climbing down the free ladder a click on the deck turns him round and he climbs back up;
//  4. turn-based (LADDERS.md 7.6, user decision 2026-10-22): the fight starts while he climbs the free-standing ladder and a Military Zombie
//     climbs the wall ladder - the grid forms without both, they finish the climb at normal speed (he does not walk on to his click), the
//     zombie is held once off, and on the next round both join (he on the deck, the zombie on the grid or in stasis).
// 'Clip' (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_B_FrozenFactory -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
// -benchmark -fps=30 -ExecCmds="CodexTactics.LadderClimbSmoke Clip"): the two ladders up and down filmed by a camera that follows the climber
// -> Saved/Screenshots/Ladder/frames/f_NNNN.png (in-engine frames only). Godot: none (UE-only system).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "AIController.h"
#include "Camera/TacticalCameraPawn.h"
#include "UI/MinimapSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "SmokeUtils.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
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
		/** A click on the other level mid-climb: he turns round and ends where he started. */
		bool bReverse = false;
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
		// Descent facing (user feedback 2026-10-23): once the pelvis is 10 cm below its standing height on top, he faces the ladder.
		float MinActorFacing = 2.f;
		float MinBodyFacing = 2.f;
		int32 FacingSamples = 0;
		// Knees on the loop (user report 2026-10-23), per frame, [0] left, [1] right.
		float MinKneeAngle[2] = { 360.f, 360.f };
		float MaxKneeJump[2] = { 0.f, 0.f };
		float MinKneeForward[2] = { 1e6f, 1e6f };
		int32 KneeSamples = 0;
		FVector LastKnee[2];
		FVector PrevKnee[2];
		int32 KneeHistory = 0;
		bool bHasLastKnee = false;
		float MaxKneeJumpTime[2] = { 0.f, 0.f };
		bool bNoIK = false;
		// Clip.
		bool bRecording = false;
		int32 FrameCount = 0;
		FString FrameDir;
		FDelegateHandle FrameHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector CameraEye = FVector::ZeroVector;
		FVector CameraLook = FVector::ZeroVector;
		bool bCameraPlaced = false;
		// The climb out on top (user bug 2026-10-23 "at the very end he sinks halfway into the object"): the lowest foot bone over the
		// real top surface (a trace under him) from 0.5 s before ExitTop ends until 1 s after, and the capsule bottom over it.
		float ExitTopTime = 0.f;
		float OffTopTime = -1.f;
		float MinFootOverTop = 1e6f;
		float MaxFootOverTop = -1e6f;
		float MinCapsuleOverTop = 1e6f;
		float MaxCapsuleOverTop = -1e6f;
		int32 TopSamples = 0;
		int32 TopLogCount = 0;
		float MaxInside = 0.f;
		/** The lowest foot (sole estimate) over the ground when he stands on flat ground at the start of the leg. */
		float StandingFoot = 0.f;
		/** When the lowest foot was measured: ExitTop time, or 100 + the time after it. */
		float MinFootAt = 0.f;
		float MinCapsuleAfter = 1e6f;
		float MaxCapsuleAfter = -1e6f;
		// Camera / floors on the ladder (7.3), per leg.
		float CamLastZ = 0.f;
		float CamLastStep = 0.f;
		bool bCamSampled = false;
		float CamMaxStep = 0.f;
		int32 CamReversals = 0;
		int32 BodyReversals = 0;
		float BodyLastZ = 0.f;
		float BodyLastStep = 0.f;
		int32 CamSamples = 0;
		FVector FloorRef = FVector::ZeroVector;
		bool bFloorRefSet = false;
		float FloorRefDrift = 0.f;
		int32 FloorAtClimbStart = -1;
		int32 FloorChangesOnLadder = 0;
		int32 FloorAfter = -1;
		// Turning back (per leg).
		float ReverseClickAt = -1.f;
		float ReverseDoneAt = -1.f;
		// Turn-based (stage 4+).
		TWeakObjectPtr<AEnemyCharacter> Zombie;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		float TurnBasedAt = -1.f;
		float LeaderOffAt = -1.f;
		bool bZombieHeldSeen = false;
		bool bZombieOrdered = false;
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
			if (State.bClip)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] clip: %d frames in %s"), State.FrameCount, *State.FrameDir);
			}
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
		State.MinActorFacing = State.MinBodyFacing = 2.f;
		State.FacingSamples = 0;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			State.MinKneeAngle[Side] = 360.f;
			State.MaxKneeJump[Side] = 0.f;
			State.MinKneeForward[Side] = 1e6f;
		}
		State.KneeSamples = 0;
		State.bHasLastKnee = false;
		{
			// Where the platform's edge stands behind the rung plane (the ladder's top edge: a trace at TopZ - 5 cm towards the wall).
			FHitResult Edge;
			FCollisionQueryParams EdgeParams(SCENE_QUERY_STAT(LadderSmokeEdge), false, Leader);
			EdgeParams.AddIgnoredActor(Ladder);
			const FVector From = Ladder->GetActorLocation() + FVector(0.f, 0.f, Ladder->GetTopWorldZ() - Ladder->GetFloorZ() - 5.f) + Ladder->GetOutward() * 60.f;
			const bool bEdge = World->LineTraceSingleByChannel(Edge, From, From - Ladder->GetOutward() * 200.f, ECC_Visibility, EdgeParams);
			UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] leg %d: the platform's edge %.1f cm behind the rung plane (%s)"), State.Leg,
				bEdge ? static_cast<float>(FVector::DotProduct(Edge.ImpactPoint - Ladder->GetActorLocation(), -Ladder->GetOutward())) : -1.f, *GetNameSafe(Edge.GetActor()));
		}
		if (const USkeletalMeshComponent* Body = Leader ? Leader->GetMesh() : nullptr)
		{
			float Lowest = 1e6f;
			FCollisionQueryParams FootParams(SCENE_QUERY_STAT(LadderSmokeStandFoot), false, Leader);
			FootParams.AddIgnoredActor(Ladder);
			for (const TPair<const TCHAR*, float> Bone : { TPair<const TCHAR*, float>(TEXT("ball_l"), 2.f), TPair<const TCHAR*, float>(TEXT("ball_r"), 2.f),
				TPair<const TCHAR*, float>(TEXT("foot_l"), 7.f), TPair<const TCHAR*, float>(TEXT("foot_r"), 7.f) })
			{
				const FVector P = Body->GetBoneLocation(Bone.Key);
				FHitResult Ground;
				if (World->LineTraceSingleByChannel(Ground, P + FVector(0.f, 0.f, 50.f), P - FVector(0.f, 0.f, 200.f), ECC_Visibility, FootParams))
				{
					Lowest = FMath::Min(Lowest, static_cast<float>(P.Z - Ground.ImpactPoint.Z) - Bone.Value);
				}
			}
			State.StandingFoot = Lowest < 1e5f ? Lowest : 0.f;
		}
		State.ExitTopTime = 0.f;
		State.OffTopTime = -1.f;
		State.MinFootOverTop = State.MinCapsuleOverTop = 1e6f;
		State.MaxFootOverTop = State.MaxCapsuleOverTop = -1e6f;
		State.TopSamples = 0;
		State.TopLogCount = 0;
		State.MaxInside = 0.f;
		State.MinCapsuleAfter = 1e6f;
		State.MaxCapsuleAfter = -1e6f;
		State.ReverseClickAt = State.ReverseDoneAt = -1.f;
		State.bCamSampled = false;
		State.CamMaxStep = 0.f;
		State.CamReversals = State.BodyReversals = 0;
		State.CamSamples = 0;
		State.bFloorRefSet = false;
		State.FloorRefDrift = 0.f;
		State.FloorAtClimbStart = -1;
		State.FloorChangesOnLadder = 0;
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
			// The knockdown leg: on the way down, feet > 1.2 m up (the loop starts 7 rungs below the platform since the descent faces the ladder).
			if (Leg.bKnockdown && State.KnockFeetZ < 0.f && ClimbState == ELadderClimbState::ClimbDown && FeetOverFloor > 120.f)
			{
				State.KnockFeetZ = FeetOverFloor;
				State.HealthBefore = Leader->HealthComponent ? Leader->HealthComponent->GetCurrentHealth() : 0.f;
				const bool bKnocked = Leader->KnockdownComponent && Leader->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyMelee);
				Check(State, bKnocked && Climb->IsDropping(), FString::Printf(TEXT("knocked down %.0f cm up the ladder: he drops (state %d)"), FeetOverFloor,
					static_cast<int32>(Climb->GetState())));
			}
		}
		// Turning back: two rungs into the climb, a click on the level he came from.
		if (Leg.bReverse && State.ReverseClickAt < 0.f && Climb->GetLadder() == Ladder
			&& ((Leg.bUp && ClimbState == ELadderClimbState::ClimbUp && Climb->GetLoopPos().Rung >= 2)
				|| (!Leg.bUp && ClimbState == ELadderClimbState::ClimbDown && Climb->GetLoopPos().Rung <= 1)))
		{
			State.Goal = Leg.bUp ? Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 250.f : Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 100.f;
			State.ReverseClickAt = State.StageTime;
			Check(State, ClickAt(World, State.Goal), FString::Printf(TEXT("leg %d: mid-climb (rung %d) a click back on the %s"), State.Leg, Climb->GetLoopPos().Rung,
				Leg.bUp ? TEXT("ground") : TEXT("deck")));
		}
		if (State.ReverseClickAt >= 0.f && State.ReverseDoneAt < 0.f && Climb->GetReversals() > 0)
		{
			State.ReverseDoneAt = State.StageTime;
		}
		if (State.StageTime > 60.f)
		{
			Check(State, false, FString::Printf(TEXT("leg %d timed out in state %d at %s (moving %d, %.0f cm from the goal)"), State.Leg,
				static_cast<int32>(ClimbState), *Leader->GetActorLocation().ToCompactString(), Leader->IsMoving() ? 1 : 0,
				FVector::Dist2D(Leader->GetActorLocation(), State.Goal)));
			return true;
		}
		const bool bEndUp = Leg.bReverse ? !Leg.bUp : Leg.bUp; // the level he ends on
		const bool bExitSeen = Leg.bKnockdown ? (State.SeenStates & (1u << static_cast<uint32>(ELadderClimbState::Dropping))) != 0
			: bEndUp ? (State.SeenStates & (1u << static_cast<uint32>(ELadderClimbState::ExitTop))) != 0
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
		const bool bStates = Leg.bReverse
			? (Leg.bUp ? Seen(ELadderClimbState::AlignBottom) && Seen(ELadderClimbState::ClimbUp) && Seen(ELadderClimbState::ClimbDown) && Seen(ELadderClimbState::ExitBottom)
					&& !Seen(ELadderClimbState::ExitTop)
				: Seen(ELadderClimbState::AlignTop) && Seen(ELadderClimbState::EnterTop) && Seen(ELadderClimbState::ClimbDown) && Seen(ELadderClimbState::ClimbUp)
					&& Seen(ELadderClimbState::ExitTop) && !Seen(ELadderClimbState::ExitBottom))
			: Leg.bUp
			? Seen(ELadderClimbState::AlignBottom) && Seen(ELadderClimbState::ClimbUp) && Seen(ELadderClimbState::ExitTop)
			: Seen(ELadderClimbState::AlignTop) && Seen(ELadderClimbState::EnterTop) && Seen(ELadderClimbState::ClimbDown) && Seen(ELadderClimbState::ExitBottom);
		Check(State, bStates, FString::Printf(TEXT("leg %d (%s, %s%s): the climb ran %s (states seen 0x%x)"), State.Leg, Name, Leg.bUp ? TEXT("up") : TEXT("down"),
			Leg.bReverse ? TEXT(", turned back") : TEXT(""),
			Leg.bReverse ? (Leg.bUp ? TEXT("AlignBottom -> ClimbUp -> ClimbDown -> ExitBottom") : TEXT("AlignTop -> EnterTop -> ClimbDown -> ClimbUp -> ExitTop"))
				: Leg.bUp ? TEXT("AlignBottom -> ClimbUp -> ExitTop") : TEXT("AlignTop -> EnterTop -> ClimbDown -> ExitBottom"), State.SeenStates));
		if (Leg.bReverse)
		{
			// At the next rung: within half a loop of clip time (+ a frame) of the click.
			const float Latency = State.ReverseDoneAt - State.ReverseClickAt;
			Check(State, State.ReverseClickAt >= 0.f && State.ReverseDoneAt >= 0.f && Climb->GetReversals() == 1 && Latency <= Climb->GetLoopLength() * 0.5f + 0.1f,
				FString::Printf(TEXT("leg %d: he turned round at the next rung, %.2f s after the click (half a loop %.2f s), once (%d)"), State.Leg, Latency,
					Climb->GetLoopLength() * 0.5f, Climb->GetReversals()));
		}
		const float LevelZ = bEndUp ? Ladder->GetTopWorldZ() : Ladder->GetFloorZ();
		const float FeetOverLevel = static_cast<float>(Leader->GetActorLocation().Z) - HalfHeight - LevelZ;
		Check(State, Leader->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(FeetOverLevel) < 15.f
			&& FVector::Dist2D(Leader->GetActorLocation(), State.Goal) < 150.f,
			FString::Printf(TEXT("leg %d: standing %s at the click (feet %.0f cm off the level, %.0f cm from the goal)"), State.Leg, bEndUp ? TEXT("on the roof") : TEXT("on the ground"),
				FeetOverLevel, FVector::Dist2D(Leader->GetActorLocation(), State.Goal)));
		Check(State, State.MinFeetOverFloor > -10.f, FString::Printf(TEXT("leg %d: never below the ladder's floor on the ladder (min feet %.1f cm)"), State.Leg, State.MinFeetOverFloor));
		Check(State, State.HandSamples > 10 && State.MaxHandError <= 3.f, FString::Printf(TEXT("leg %d: gripping hands within 3 cm of their rung grip (max %.2f cm, %d samples)"),
			State.Leg, State.MaxHandError, State.HandSamples));
		UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] leg %d facing: actor %.2f body %.2f"), State.Leg, State.MinActorFacing, State.MinBodyFacing);
		if (!Leg.bUp)
		{
			Check(State, State.FacingSamples > 10 && State.MinActorFacing > 0.9f && State.MinBodyFacing > 0.6f /* the loop pose holds the hips ~42 deg turned (0.74 with no IK) */,
				FString::Printf(TEXT("leg %d: going down he faces the ladder once the pelvis is 10 cm below his standing height on top (actor %.2f, body %.2f, %d frames)"),
					State.Leg, State.MinActorFacing, State.MinBodyFacing, State.FacingSamples));
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Check(State, State.KneeSamples > 10 && State.MinKneeAngle[Side] >= 60.f && State.MaxKneeJump[Side] <= 4.f && State.MinKneeForward[Side] > 0.f,
				FString::Printf(TEXT("leg %d: %s knee on the loop - angle >= 60 deg (min %.0f; the clip's own 75), the IK never jumps it > 4 cm per frame (its correction changes max %.1f cm), bent towards the ladder (min %.1f cm), %d frames (max jump at rung*100+t %.2f)"),
					State.Leg, Side == 0 ? TEXT("left") : TEXT("right"), State.MinKneeAngle[Side], State.MaxKneeJump[Side], State.MinKneeForward[Side], State.KneeSamples, State.MaxKneeJumpTime[Side]));
		}
		// The climb out on top / the start down (user bug 2026-10-23): no leg inside the platform, standing on it after the exit.
		Check(State, State.MaxInside <= 3.f, FString::Printf(TEXT("leg %d: no leg / foot inside the platform on ClimbOutTop (max %.1f cm; pushed off the wall at most %.1f cm)"), State.Leg,
			State.MaxInside, Climb->GetMaxEdgePush()));
		if (bEndUp)
		{
			// The standing feet on flat ground measured the same way (start of the leg): the same sole height on the top = no sink.
			Check(State, State.TopSamples > 10 && State.MinFootOverTop >= State.StandingFoot - 3.f && State.MinCapsuleAfter >= -3.f && State.MaxCapsuleAfter <= 3.f,
				FString::Printf(TEXT("leg %d: he lands ON the top - the lowest foot from 0.5 s before the end on: %.1f cm over the surface under it (standing on flat ground: %.1f, -3 cm allowed); the capsule bottom on the floor after (%.1f..%.1f cm); lowest at %.2f"),
					State.Leg, State.MinFootOverTop, State.StandingFoot, State.MinCapsuleAfter, State.MaxCapsuleAfter, State.MinFootAt));
		}
		if (!State.bClip)
		{
			const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
			const int32 FloorNow = Minimap ? Minimap->GetOperativeFloor(Leader) : -1;
			Check(State, State.CamSamples > 20 && State.CamReversals <= (Leg.bReverse ? 2 : 1) && State.CamMaxStep <= 3.f,
				FString::Printf(TEXT("leg %d: the camera height follows the climb smoothly (%d reversals of its vertical motion - the body's own: %d; max %.2f cm per frame, %d frames)"),
					State.Leg, State.CamReversals, State.BodyReversals, State.CamMaxStep, State.CamSamples));
			// The level JSON's minimap floor at 400 cm (2026-10-23): the wall ladder's roof is floor 1 (the free ladder's 2.9 m deck stays
			// under the band: floor 0).
			const int32 FloorExpected = Minimap && Minimap->GetFloorCount() > 1 && bEndUp && Leg.bWall ? 1 : 0;
			Check(State, State.bFloorRefSet && State.FloorRefDrift < 1.f && State.FloorChangesOnLadder == 0 && FloorNow == FloorExpected
				&& FVector::Dist(Leader->GetFloorReferenceLocation(), Leader->GetActorLocation()) < 1.f,
				FString::Printf(TEXT("leg %d: on the ladder the roof cutaway / minimap count him on the floor he left (reference drift %.1f cm, minimap floor %d at the start, %d changes mid-ladder; %d now, expected %d, of %d floors); off it, where he stands"),
					State.Leg, State.FloorRefDrift, State.FloorAtClimbStart, State.FloorChangesOnLadder, FloorNow, FloorExpected, Minimap ? Minimap->GetFloorCount() : 0));
		}
		Check(State, Climb->GetLastRefusal() == ELadderRefusal::None && !Ladder->GetLeaseHolder(), TEXT("no refusal, the ladder's lease released"));
		return true;
	}

	/** Every frame: the descent facing and the knees on the loop (user reports 2026-10-23). */
	void SampleFrame(FState& State)
	{
		const AOperativeCharacter* Leader = State.Leader.Get();
		const ALadderActor* Ladder = LegLadder(State);
		const ULadderClimbComponent* Climb = Leader ? Leader->LadderClimb.Get() : nullptr;
		const USkeletalMeshComponent* Mesh = Leader ? Leader->GetMesh() : nullptr;
		if (!Climb || !Ladder || !Mesh || State.Stage != 2 || Climb->GetLadder() != Ladder)
		{
			State.bHasLastKnee = false;
			return;
		}
		const ELadderClimbState S = Climb->GetState();
		const FVector Inward = -Ladder->GetOutward();
		// The climb out on top (user bug 2026-10-23): every frame of ExitTop and 1 s after it, each foot / leg / pelvis bone against the
		// platform under it (a trace down at the bone's XY from above the top): how deep a bone is inside the platform, and, over the
		// last 0.5 s and after, how far the lowest foot is above the surface it stands on.
		{
			const float Delta = 1.f / 60.f;
			const bool bExitTop = S == ELadderClimbState::ExitTop;
			if (bExitTop)
			{
				State.ExitTopTime += Delta;
				State.OffTopTime = -1.f;
			}
			else if (S == ELadderClimbState::None && State.ExitTopTime > 0.f)
			{
				State.OffTopTime = State.OffTopTime < 0.f ? 0.f : State.OffTopTime + Delta;
			}
			const bool bAfter = State.OffTopTime >= 0.f && State.OffTopTime <= 1.f;
			UWorld* World = Leader->GetWorld();
			if ((bExitTop || bAfter) && World)
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderSmokeTop), false, Leader);
				Params.AddIgnoredActor(Ladder); // its click proxy blocks Visibility
				const float TopZ = Ladder->GetTopWorldZ();
				float Inside = 0.f;
				const TCHAR* InsideBone = TEXT("-");
				float InsidePast = 0.f;
				float LowestFoot = 1e6f;
				// Each bone with how far over the sole / its skin it sits (ball 2, ankle 7, knee 8, hip / pelvis 10 cm).
				struct FBoneSkin { const TCHAR* Name; float OverSole; };
				const FBoneSkin Bones[] = { { TEXT("ball_l"), 2.f }, { TEXT("ball_r"), 2.f }, { TEXT("foot_l"), 7.f }, { TEXT("foot_r"), 7.f }, { TEXT("calf_l"), 8.f },
					{ TEXT("calf_r"), 8.f }, { TEXT("thigh_l"), 10.f }, { TEXT("thigh_r"), 10.f }, { TEXT("pelvis"), 10.f } };
				for (const FBoneSkin& Skin : Bones)
				{
					const TCHAR* Bone = Skin.Name;
					const FVector P = Mesh->GetBoneLocation(Bone);
					FHitResult Hit;
					if (!World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, TopZ + 100.f), FVector(P.X, P.Y, TopZ - 600.f), ECC_Visibility, Params))
					{
						continue;
					}
					const float Over = static_cast<float>(P.Z - Hit.ImpactPoint.Z) - Skin.OverSole; // the sole / skin over the surface under it
					// Inside the geometry: the bone's centre overlaps a blocking surface; how deep = the nearer of the top surface above it and the
					// face in front of it (towards the ladder) - a thin deck the feet stand under does not count.
					if (World->OverlapBlockingTestByChannel(P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(0.5f), Params))
					{
						FHitResult Above, Face;
						const FVector Out = Ladder->GetOutward();
						// (Each trace runs 2 cm past the bone: a sphere just touching the surface from outside has no surface before its centre.)
						const float Vertical = World->LineTraceSingleByChannel(Above, P + FVector(0.f, 0.f, 150.f), P - FVector(0.f, 0.f, 2.f), ECC_Visibility, Params)
							? static_cast<float>(Above.ImpactPoint.Z - P.Z) : 0.f;
						const float Horizontal = World->LineTraceSingleByChannel(Face, P + Out * 150.f, P - Out * 2.f, ECC_Visibility, Params)
							? static_cast<float>(FVector::DotProduct(Face.ImpactPoint - P, Out)) : 0.f;
						// A knee / hip / pelvis joint centre sits a few cm inside its skin (kneeling on the edge puts the knee centre that deep).
						const float Depth = FMath::Max(0.f, FMath::Min(Vertical, Horizontal) - (Skin.OverSole >= 8.f ? 4.f : 0.f));
						if (Depth > Inside)
						{
							Inside = Depth;
							InsideBone = Bone;
							InsidePast = static_cast<float>(FVector::DotProduct(P - Ladder->GetActorLocation(), -Out));
						}
					}
					if (FCString::Strncmp(Bone, TEXT("ball"), 4) == 0 || FCString::Strncmp(Bone, TEXT("foot"), 4) == 0)
					{
						LowestFoot = FMath::Min(LowestFoot, Over);
					}
				}
				// During the clip, and after it while he stands (the walk on to the click is the locomotion's own business).
				if (bExitTop || Leader->GetVelocity().Size2D() < 20.f)
				{
					State.MaxInside = FMath::Max(State.MaxInside, Inside);
				}
				const float ExitLength = 3.233f; // ClimbOutTop's length (the clip log)
				// The last 0.5 s of the clip, and after it while he still stands there (a walk on to the click has its own lower heel strike).
				if ((bExitTop && State.ExitTopTime > ExitLength - 0.5f) || (bAfter && Leader->GetVelocity().Size2D() < 20.f))
				{
					if (LowestFoot < State.MinFootOverTop)
					{
						State.MinFootAt = State.OffTopTime >= 0.f ? 100.f + State.OffTopTime : State.ExitTopTime;
					}
					State.MinFootOverTop = FMath::Min(State.MinFootOverTop, LowestFoot);
					State.MaxFootOverTop = FMath::Max(State.MaxFootOverTop, LowestFoot);
					const float Capsule = static_cast<float>(Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - TopZ;
					State.MinCapsuleOverTop = FMath::Min(State.MinCapsuleOverTop, Capsule);
					State.MaxCapsuleOverTop = FMath::Max(State.MaxCapsuleOverTop, Capsule);
					if (bAfter && State.OffTopTime > 0.1f)
					{
						State.MinCapsuleAfter = FMath::Min(State.MinCapsuleAfter, Capsule);
						State.MaxCapsuleAfter = FMath::Max(State.MaxCapsuleAfter, Capsule);
					}
					++State.TopSamples;
				}
				if (Inside > 3.f)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[LadderClimb] top exit t %.2f / off %.2f: %s %.1f cm inside the platform (%.1f cm past the rung plane); lowest foot %.1f cm over the surface under it; capsule bottom %.1f over the top"),
						State.ExitTopTime, State.OffTopTime, InsideBone, Inside, InsidePast, LowestFoot,
						static_cast<float>(Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - TopZ);
				}
			}
		}
		// Camera height and the floor reference while he climbs (7.3).
		if (Climb->IsClimbing() && !State.bClip)
		{
			UWorld* World = Leader->GetWorld();
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const ATacticalCameraPawn* Cam = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
			if (Cam)
			{
				const float CamZ = static_cast<float>(Cam->GetActorLocation().Z);
				const float BodyZ = static_cast<float>(Leader->GetActorLocation().Z);
				if (State.bCamSampled)
				{
					const float Step = CamZ - State.CamLastZ;
					const float BodyStep = BodyZ - State.BodyLastZ;
					State.CamMaxStep = FMath::Max(State.CamMaxStep, FMath::Abs(Step));
					// A reversal: the vertical motion changes sign (dead band 0.02 cm per frame).
					State.CamReversals += (FMath::Abs(Step) > 0.02f && FMath::Abs(State.CamLastStep) > 0.02f && Step * State.CamLastStep < 0.f) ? 1 : 0;
					State.BodyReversals += (FMath::Abs(BodyStep) > 0.02f && FMath::Abs(State.BodyLastStep) > 0.02f && BodyStep * State.BodyLastStep < 0.f) ? 1 : 0;
					if (FMath::Abs(Step) > 0.02f)
					{
						State.CamLastStep = Step;
					}
					if (FMath::Abs(BodyStep) > 0.02f)
					{
						State.BodyLastStep = BodyStep;
					}
					++State.CamSamples;
				}
				State.CamLastZ = CamZ;
				State.BodyLastZ = BodyZ;
				State.bCamSampled = true;
			}
			const FVector Ref = Leader->GetFloorReferenceLocation();
			if (!State.bFloorRefSet)
			{
				State.FloorRef = Ref;
				State.bFloorRefSet = true;
			}
			State.FloorRefDrift = FMath::Max(State.FloorRefDrift, static_cast<float>(FVector::Dist(Ref, State.FloorRef)));
			if (const UMinimapSubsystem* Minimap = World ? World->GetSubsystem<UMinimapSubsystem>() : nullptr)
			{
				const int32 Floor = Minimap->GetOperativeFloor(Leader);
				State.FloorChangesOnLadder += State.FloorAtClimbStart >= 0 && Floor != State.FloorAtClimbStart ? 1 : 0;
				State.FloorAtClimbStart = State.FloorAtClimbStart >= 0 ? State.FloorAtClimbStart : Floor;
			}
		}
		// Facing on the way down: below the standing pelvis on top (- 10 cm) the actor and the body (hips) face the ladder.
		const bool bDown = S == ELadderClimbState::EnterTop || S == ELadderClimbState::ClimbDown || S == ELadderClimbState::ExitBottom
			|| S == ELadderClimbState::ClimbUp || S == ELadderClimbState::ExitTop; // the way up too: the clips' own baseline
		if (bDown && Mesh->GetBoneLocation(TEXT("pelvis")).Z < Ladder->GetTopWorldZ() + 85.f)
		{
			const FVector Hips = Mesh->GetBoneLocation(TEXT("thigh_r")) - Mesh->GetBoneLocation(TEXT("thigh_l"));
			const FVector Body = FVector::CrossProduct(Hips, FVector::UpVector).GetSafeNormal2D();
			State.MinActorFacing = FMath::Min(State.MinActorFacing, static_cast<float>(FVector::DotProduct(Leader->GetActorForwardVector().GetSafeNormal2D(), Inward)));
			State.MinBodyFacing = FMath::Min(State.MinBodyFacing, static_cast<float>(FVector::DotProduct(Body, Inward)));
			++State.FacingSamples;
		}
		// Knees on the loop.
		if (S != ELadderClimbState::ClimbUp && S != ELadderClimbState::ClimbDown)
		{
			State.bHasLastKnee = false;
			return;
		}
		const TCHAR* Hip[2] = { TEXT("thigh_l"), TEXT("thigh_r") };
		const TCHAR* Knee[2] = { TEXT("calf_l"), TEXT("calf_r") };
		const TCHAR* Ankle[2] = { TEXT("foot_l"), TEXT("foot_r") };
		const FTransform Actor = Leader->GetActorTransform();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector H = Mesh->GetBoneLocation(Hip[Side]);
			const FVector K = Mesh->GetBoneLocation(Knee[Side]);
			const FVector A = Mesh->GetBoneLocation(Ankle[Side]);
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct((H - K).GetSafeNormal(), (A - K).GetSafeNormal())), -1.f, 1.f)));
			State.MinKneeAngle[Side] = FMath::Min(State.MinKneeAngle[Side], Angle);
			if (Angle < 165.f)
			{
				State.MinKneeForward[Side] = FMath::Min(State.MinKneeForward[Side], static_cast<float>(FVector::DotProduct(K - (H + A) * 0.5f, Inward)));
			}
			const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance());
			const FVector Local = Anim ? Anim->GetLadderKneeIKOffset(Side) : FVector::ZeroVector;
			if (State.bHasLastKnee)
			{
				const float Jump = static_cast<float>(FVector::Dist(Local, State.LastKnee[Side]));
				if (Jump > State.MaxKneeJump[Side])
				{
					State.MaxKneeJump[Side] = Jump;
					State.MaxKneeJumpTime[Side] = Climb->GetLoopPos().Time + Climb->GetLoopPos().Rung * 100.f;
				}
			}
			State.PrevKnee[Side] = State.LastKnee[Side];
			State.LastKnee[Side] = Local;
		}
		State.KneeHistory = State.bHasLastKnee ? State.KneeHistory + 1 : 1;
		State.bHasLastKnee = true;
		++State.KneeSamples;
	}

	/** Clip: a camera beside the ladder of the current leg, following the climber (smoothed), one frame per rendered frame. */
	void OnEndFrame(TSharedRef<FState> State)
	{
		SampleFrame(*State);
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
		// Jump cuts keep the video short: the long walks between the ladders (frames only within 3 m of the leg's ladder) and the
		// standing still between the legs are left out.
		const bool bStill = !Leader->IsClimbingLadder() && Leader->GetVelocity().Size2D() < 5.f && State->FrameCount > 15;
		if ((FVector::Dist2D(Leader->GetActorLocation(), Ladder->GetBottomLinkWorld()) < 300.f || Leader->IsClimbingLadder()) && !bStill)
		{
			FScreenshotRequest::RequestScreenshot(State->FrameDir / FString::Printf(TEXT("f_%04d.png"), State->FrameCount++), /*bShowUI*/ false, false);
		}
	}

	const TCHAR* const ZombiePath = TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C");

	float FeetOver(const ACharacter& Character, float LevelZ)
	{
		return static_cast<float>(Character.GetActorLocation().Z) - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - LevelZ;
	}

	/** Stage 4: he is up again after the knockdown leg; a zombie (its AI off) is sent up the wall ladder, he clicks the free deck. */
	bool StepTurnBasedStart(FState& State, UWorld* World)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		ALadderActor* Wall = State.Wall.Get();
		ALadderActor* Free = State.Free.Get();
		if (!Leader || !Wall || !Free)
		{
			return Finish(State, false);
		}
		if (State.StageTime < 1.f || Leader->IsKnockedDown() || Leader->IsMoving())
		{
			return true;
		}
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, ZombiePath);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FVector Spot = SmokeUtils::FreeSpot(World, Wall->GetBottomLinkWorld() + Wall->GetOutward() * 200.f, Leader);
		AEnemyCharacter* Zombie = Class ? World->SpawnActor<AEnemyCharacter>(Class, Spot + FVector(0.f, 0.f, 10.f), FRotator::ZeroRotator, Params) : nullptr;
		Check(State, Zombie != nullptr, TEXT("turn-based part: a Military Zombie spawned by the wall ladder"));
		if (!Zombie)
		{
			return Finish(State, false);
		}
		// Its AI off (the smoke sends it up) and unseen by the squad until the fight (no real-time shots at it).
		Zombie->SetActorTickEnabled(false);
		if (UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>())
		{
			Registry->UnregisterEnemy(Zombie);
		}
		Zombie->Tags.Remove(FName(TEXT("Enemy")));
		State.Zombie = Zombie; // sent up the wall ladder once he is on his (stage 5): both climb when the fight starts
		State.Goal = Free->GetTopLinkWorld() - Free->GetOutward() * 100.f;
		Check(State, ClickAt(World, State.Goal), TEXT("turn-based part: click on the free-standing ladder's deck"));
		State.Stage = 5;
		State.StageTime = 0.f;
		return true;
	}

	/** Stage 5: both on their ladders (feet > 60 cm up) -> the fight starts. */
	bool StepTurnBasedEntry(FState& State, UWorld* World)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		AEnemyCharacter* Zombie = State.Zombie.Get();
		ALadderActor* Free = State.Free.Get();
		ALadderActor* Wall = State.Wall.Get();
		const ULadderClimbComponent* Climb = Leader ? Leader->LadderClimb.Get() : nullptr;
		const ULadderClimbComponent* ZombieClimb = Zombie ? Zombie->LadderClimb.Get() : nullptr;
		if (!Leader || !Zombie || !Climb || !ZombieClimb || !Free || !Wall)
		{
			return Finish(State, false);
		}
		if (Climb->IsClimbing() && !State.bZombieOrdered)
		{
			State.bZombieOrdered = true;
			if (AAIController* Controller = Cast<AAIController>(Zombie->GetController()))
			{
				Controller->MoveToLocation(Wall->GetTopLinkWorld() - Wall->GetOutward() * 100.f, 30.f, false, true);
			}
		}
		const bool bLeaderUp = Climb->IsClimbing() && FeetOver(*Leader, Free->GetFloorZ()) > 20.f;
		const bool bZombieUp = ZombieClimb->IsClimbing() && FeetOver(*Zombie, Wall->GetFloorZ()) > 20.f;
		if (!bLeaderUp || !bZombieUp)
		{
			if (State.StageTime > 60.f)
			{
				Check(State, false, FString::Printf(TEXT("turn-based part: both on their ladders (leader state %d, zombie state %d)"), static_cast<int32>(Climb->GetState()),
					static_cast<int32>(ZombieClimb->GetState())));
				return Finish(State, false);
			}
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		SmokeUtils::StartWaveFight(Flow);
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (*It != Zombie)
			{
				It->Destroy(); // the fight is ours: the zombie on the ladder and one hound on the ground
			}
		}
		const FVector HoundSpot = SmokeUtils::FreeSpot(World, Free->GetBottomLinkWorld() + Free->GetOutward() * 600.f, Leader);
		State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, HoundSpot + FVector(0.f, 0.f, 20.f));
		const EGameFlowResult Result = Flow ? Flow->RequestEnterTurnBased(true) : EGameFlowResult::WrongPhase;
		const bool bActive = Result == EGameFlowResult::Ok && TurnBased && TurnBased->IsActive();
		Check(State, bActive, TEXT("turn-based part: the fight starts while he and the zombie climb"));
		if (!bActive)
		{
			return Finish(State, false);
		}
		State.TurnBasedAt = State.StageTime;
		Check(State, !TurnBased->GetUnitState(Leader) && TurnBased->GetLadderSquadWaiting() == 1 && Climb->IsClimbing(),
			FString::Printf(TEXT("the grid forms without him (on the grid %d, waiting on ladders %d, still climbing %d)"), TurnBased->GetUnitState(Leader) ? 1 : 0,
				TurnBased->GetLadderSquadWaiting(), Climb->IsClimbing() ? 1 : 0));
		Check(State, !TurnBased->GetUnitState(Zombie) && !Zombie->IsTurnBasedHeld() && TurnBased->GetLadderEnemiesWaiting() == 1 && ZombieClimb->IsClimbing(),
			FString::Printf(TEXT("... and without the climbing zombie (on the grid %d, held %d, waiting %d)"), TurnBased->GetUnitState(Zombie) ? 1 : 0,
				Zombie->IsTurnBasedHeld() ? 1 : 0, TurnBased->GetLadderEnemiesWaiting()));
		Check(State, State.Hound.IsValid() && TurnBased->GetUnitState(State.Hound.Get()) != nullptr && TurnBased->GetSquadCount() >= 1,
			FString::Printf(TEXT("the hound and the other %d operatives are on the grid"), TurnBased->GetSquadCount()));
		State.Stage = 6;
		State.StageTime = 0.f;
		return true;
	}

	/** Stage 6: both finish their climbs at normal speed; he stays on the deck, the zombie is held once off. */
	bool StepTurnBasedClimbOff(FState& State, UWorld* World)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		AEnemyCharacter* Zombie = State.Zombie.Get();
		ALadderActor* Free = State.Free.Get();
		ALadderActor* Wall = State.Wall.Get();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		if (!Leader || !Zombie || !Free || !Wall || !TurnBased || !TurnBased->IsActive())
		{
			Check(State, false, TEXT("turn-based part: the fight runs on"));
			return Finish(State, false);
		}
		const ULadderClimbComponent* Climb = Leader->LadderClimb.Get();
		const ULadderClimbComponent* ZombieClimb = Zombie->LadderClimb.Get();
		if (Climb->GetState() != ELadderClimbState::None && State.StageTime > 0.f)
		{
			State.LeaderOffAt = -1.f;
		}
		else if (State.LeaderOffAt < 0.f)
		{
			State.LeaderOffAt = State.StageTime;
		}
		State.bZombieHeldSeen |= ZombieClimb->GetState() == ELadderClimbState::None && Zombie->IsTurnBasedHeld();
		const bool bBothOff = State.LeaderOffAt >= 0.f && ZombieClimb->GetState() == ELadderClimbState::None;
		if (!bBothOff || State.StageTime - State.LeaderOffAt < 1.5f)
		{
			if (State.StageTime > 60.f)
			{
				Check(State, false, FString::Printf(TEXT("turn-based part: both off their ladders in time (leader state %d, zombie state %d)"),
					static_cast<int32>(Climb->GetState()), static_cast<int32>(ZombieClimb->GetState())));
				return Finish(State, false);
			}
			return true;
		}
		// Normal speed: the climb's own clip time (no speed-up, no abort).
		Check(State, Climb->ClimbPlayRate == 1.f && Climb->GetLastAbort() == ELadderAbortReason::None && Climb->GetClimbsUp() >= 1 && State.LeaderOffAt < 30.f,
			FString::Printf(TEXT("he finished the climb to the top at normal speed in %.1f s (no abort)"), State.LeaderOffAt));
		Check(State, FMath::Abs(FeetOver(*Leader, Free->GetTopWorldZ())) < 15.f && !Leader->IsMoving() && FVector::Dist2D(Leader->GetActorLocation(), Free->GetTopExitWorld()) < 80.f
			&& !TurnBased->GetUnitState(Leader),
			FString::Printf(TEXT("he stays on the deck where the ladder left him (feet %.0f cm off the deck, %.0f cm from the ladder head, moving %d, on the grid %d)"),
				FeetOver(*Leader, Free->GetTopWorldZ()), FVector::Dist2D(Leader->GetActorLocation(), Free->GetTopExitWorld()), Leader->IsMoving() ? 1 : 0,
				TurnBased->GetUnitState(Leader) ? 1 : 0));
		Check(State, State.bZombieHeldSeen && Zombie->IsTurnBasedHeld() && !Zombie->IsActorTickEnabled() && FMath::Abs(FeetOver(*Zombie, Wall->GetTopWorldZ())) < 15.f,
			FString::Printf(TEXT("the zombie finished its climb onto the roof and is held at once (held %d, feet %.0f cm off the roof)"), Zombie->IsTurnBasedHeld() ? 1 : 0,
				FeetOver(*Zombie, Wall->GetTopWorldZ())));
		if (TurnBased->GetPhase() == ETurnPhase::Squad)
		{
			TurnBased->PassSquadTurn();
		}
		State.Stage = 7;
		State.StageTime = 0.f;
		return true;
	}

	/** Stage 7: the next round - both join. */
	bool StepTurnBasedJoin(FState& State, UWorld* World)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		AEnemyCharacter* Zombie = State.Zombie.Get();
		ALadderActor* Free = State.Free.Get();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		if (!TurnBased || !TurnBased->IsActive() || !Leader || !Free)
		{
			Check(State, false, TEXT("turn-based part: the fight runs on to the next round"));
			return Finish(State, false);
		}
		if (TurnBased->GetRound() < 2 || TurnBased->GetPhase() != ETurnPhase::Squad)
		{
			if (State.StageTime > 120.f)
			{
				Check(State, false, FString::Printf(TEXT("turn-based part: round 2 (round %d, phase %d)"), TurnBased->GetRound(), static_cast<int32>(TurnBased->GetPhase())));
				return Finish(State, false);
			}
			// A squad turn of the others in round 1 that is still open: pass it.
			if (TurnBased->GetPhase() == ETurnPhase::Squad && TurnBased->GetRound() < 2)
			{
				TurnBased->PassSquadTurn();
			}
			return true;
		}
		Check(State, TurnBased->GetUnitState(Leader) != nullptr && TurnBased->GetLadderSquadWaiting() == 0 && FMath::Abs(FeetOver(*Leader, Free->GetTopWorldZ())) < 15.f,
			FString::Printf(TEXT("round %d: he joins the grid on the deck (on the grid %d, feet %.0f cm off the deck)"), TurnBased->GetRound(),
				TurnBased->GetUnitState(Leader) ? 1 : 0, FeetOver(*Leader, Free->GetTopWorldZ())));
		const bool bZombieIn = Zombie && (TurnBased->GetUnitState(Zombie) != nullptr || TurnBased->GetStasisMeshCount() > 0);
		Check(State, bZombieIn && TurnBased->GetLadderEnemiesWaiting() == 0 && Zombie->IsTurnBasedHeld(),
			FString::Printf(TEXT("round %d: the zombie joins (on the grid %d, else in stasis; waiting %d)"), TurnBased->GetRound(),
				Zombie && TurnBased->GetUnitState(Zombie) ? 1 : 0, TurnBased->GetLadderEnemiesWaiting()));
		return Finish(State, true);
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
		if (State.Time > 700.f)
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
			if (State.bNoIK && State.Leader->LadderClimb)
			{
				State.Leader->LadderClimb->bRungIK = false; // diagnostics: the clips' own limbs
			}
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
				// Turning back mid-climb: up the wall ladder and back down first; on the deck, down the free ladder and back up.
				State.Legs.Insert({ true, true, false, true }, 0);
				State.Legs.Insert({ false, false, false, true }, 4);
			}
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
				if (State.bClip)
				{
					return Finish(State, true);
				}
				State.Stage = 4; // the turn-based part
				State.StageTime = 0.f;
				return true;
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
		case 4:
			return StepTurnBasedStart(State, World);
		case 5:
			return StepTurnBasedEntry(State, World);
		case 6:
			return StepTurnBasedClimbOff(State, World);
		case 7:
			return StepTurnBasedJoin(State, World);
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
		State->bNoIK = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("NoIK"), ESearchCase::IgnoreCase); });
		State->bClip = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Clip"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		if (State->bClip)
		{
			State->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("Ladder") / TEXT("frames"));
			IFileManager::Get().DeleteDirectory(*State->FrameDir, false, true);
			IFileManager::Get().MakeDirectory(*State->FrameDir, true);
		}
		// Every frame: the facing / knee samples, and (Clip) the camera and the recording.
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { OnEndFrame(State); });
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
