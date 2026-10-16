// Dev-only headless check of reversing the shimmy direction in cover (user report 2026-10-11: "moves along the wall to the
// left, then I order a move to the right: he moves right while still playing the move-left clip and does not turn") and of
// the body-to-wall clearance in cover (user report 2026-10-11: the Female Soldier sinks into the wall):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverReverseSmoke -Log Smoke-CoverReverse.log
//   Scripts/smoke.ps1 -Command "CodexTactics.CoverReverseSmoke medic"   (the Medic-Sapper = SK_Female_soldier)
// A 10 m wide wall is spawned 6 m ahead of the leader on L_MovementTest (nothing is saved); he sprints into the cover and
//  1. shimmies to one side, then (while the first shimmy is STILL running) is ordered to the other side: the shimmy
//     direction, the playing loop clip (walk_*_loop_L/R) and the movement all flip within a short time;
//  2. the same after the first shimmy stopped (reverse from standing still);
//  3. a plain ground move order to the opposite side while shimmying: the cover is left, no cover clip keeps playing and
//     he turns to the walking direction.
// Every frame in cover the closest bone of the mesh to the wall plane is sampled (clearance, cm; negative = inside the
// wall); the minimum per stage is logged and must stay above the limit.
// "crouch" repeats 1 + 2 with a 60 cm low cover (crouched clips).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"

namespace CoverReverseSmoke
{
	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		float GroundZ = 0.f;
		bool bCrouch = false;
		bool bMedic = false;
		bool bCorner = false; // a 5 m wall: the first shimmy ends at its left corner, the reverse starts from the corner pose
		bool bApproach = false; // several approach angles to the same wall: the body clearance only
		int32 Approach = 0;
		FString ApproachName;
		bool bThreat = false; // a frozen hound on his side-A flank: the facing is fixed, direction flips = forward / backwards
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		float MinClearance = 1.0e9f;
		FName MinBone;
		FString MinDetail;
		float SecondsInside = 0.f; // time with a bone inside the wall (the push reacts with a short delay)
		float CapsuleClearance = 1.0e9f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CoverReverseSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString LoopName(const UOperativeAnimInstance* Anim)
	{
		return Anim && Anim->GetCoverLoopClip() ? Anim->GetCoverLoopClip()->GetName() : FString(TEXT("none"));
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
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

	/** Orders a shimmy to the point `Along` cm from the current position along the wall (+ = RightTangent). */
	bool OrderShimmyAlong(UWorld* World, AOperativeCharacter& Op, FState& State, float Along)
	{
		const FVector Click = Op.GetActorLocation() + State.Slot.RightTangent() * Along - State.Slot.WallNormal * 40.f + FVector(0.f, 0.f, State.bCrouch ? 30.f : 90.f);
		FCoverSlot Shimmy;
		return CoverTraceRules::FindShimmySlot(World, Op.GetCoverSlot(), Click, Shimmy) && Op.OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
	}

	/** Signed speed along the wall (+ = RightTangent). */
	float AlongVelocity(const AOperativeCharacter& Op, const FState& State)
	{
		return static_cast<float>(FVector::DotProduct(Op.GetVelocity(), State.Slot.RightTangent()));
	}

	/** Unknown threat: he faces forward in the move direction, so the clip is walk_fwd_loop_L (to his right) / _R. */
	FString ExpectedLoop(const AOperativeCharacter& Op, bool bThreat)
	{
		if (bThreat)
		{
			// Threat on his side A (left): towards it = forward (_R = his left), away = backwards (_L), still facing it.
			return Op.ShimmyDirection < 0.f ? FString(TEXT("walk_fwd_loop_R")) : FString(TEXT("walk_bwd_loop_L"));
		}
		return FString::Printf(TEXT("walk_fwd_loop_%s"), Op.ShimmyDirection > 0.f ? TEXT("L") : TEXT("R"));
	}

	/** Closest bone of the mesh to the wall plane, cm (negative = inside the wall). Weapon / hand IK bones included. */
	void SampleClearance(FState& State, const AOperativeCharacter& Op, float DeltaTime)
	{
		USkeletalMeshComponent* Mesh = Op.GetMesh();
		if (!Mesh || !Op.bInCover)
		{
			return;
		}
		const FVector Normal = State.Slot.WallNormal.GetSafeNormal2D();
		bool bInside = false;
		TArray<FName> Names;
		Mesh->GetBoneNames(Names);
		for (const FName Bone : Names)
		{
			if (Bone.ToString().StartsWith(TEXT("ik_")) || Bone.ToString().StartsWith(TEXT("VB ")))
			{
				continue; // IK targets / virtual bones are not skin
			}
			const FVector BoneLocation = Mesh->GetBoneLocation(Bone);
			if (!CoverFacingRules::IsWithinWallSpan(Op.GetCoverSlot(), BoneLocation))
			{
				continue; // past an exposed edge (the corner fire stance): no wall behind the face plane there
			}
			const float Depth = static_cast<float>(FVector::DotProduct(BoneLocation - State.Slot.WallPoint, Normal));
			bInside |= Depth < 0.f;
			if (Depth < State.MinClearance)
			{
				State.MinClearance = Depth;
				State.MinBone = Bone;
				FString Clip;
				float MW = 0.f, SW = 0.f;
				const UOperativeAnimInstance* A = AnimOf(&Op);
				if (A)
				{
					A->GetCoverPlayback(Clip, MW, SW);
				}
				State.MinDetail = FString::Printf(TEXT("t %.2f clip %s w%.2f enterW %.2f meshYaw %.1f meshLoc %s actorYaw-normal %.0f actorDepth %.1f speed %.0f"), State.StageTime, *Clip, MW,
					A ? A->GetCoverEnterBlendWeight() : -1.f, Mesh->GetRelativeRotation().Yaw, *Mesh->GetRelativeLocation().ToString(),
					Op.GetActorRotation().Yaw - State.Slot.WallNormal.Rotation().Yaw,
					FVector::DotProduct(Op.GetActorLocation() - State.Slot.WallPoint, State.Slot.WallNormal), Op.GetVelocity().Size2D());
			}
		}
		State.SecondsInside += bInside ? DeltaTime : 0.f;
		const float Capsule = static_cast<float>(FVector::DotProduct(Op.GetActorLocation() - State.Slot.WallPoint, Normal)) - Op.GetCapsuleComponent()->GetScaledCapsuleRadius();
		State.CapsuleClearance = FMath::Min(State.CapsuleClearance, Capsule);
	}

	void ReportClearance(FState& State, const TCHAR* Stage, float Limit)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: worst frame: %s"), *State.MinDetail);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: closest bone %s %.1f cm from the wall plane, capsule gap %.1f cm"), Stage, *State.MinBone.ToString(),
			State.MinClearance, State.CapsuleClearance);
		Check(State, State.MinClearance >= Limit || State.SecondsInside <= 0.15f,
			FString::Printf(TEXT("%s: no bone inside the wall for more than 0.15 s (min %.1f cm, %.2f s inside)"), Stage, State.MinClearance, State.SecondsInside));
		State.SecondsInside = 0.f;
		State.MinClearance = 1.0e9f;
		State.CapsuleClearance = 1.0e9f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		if (Op && State.Stage >= 2)
		{
			SampleClearance(State, *Op, DeltaTime);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The sudden horde start opens in a free tactical pause; the shared helper releases it (2026-10-11).
			SmokeUtils::StartWaveFight(Flow);
			AEnemyCharacter* Kept = nullptr;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (!Kept && !It->IsDying())
				{
					Kept = *It;
				}
				else
				{
					It->Destroy();
				}
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			Op = Leader;
			if (State.bMedic)
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member->SquadRole == EOperativeRole::MedicSapper)
					{
						Op = Member;
					}
				}
			}
			State.Op = Op;
			if (!Op || !Leader || !Kept)
			{
				Check(State, false, TEXT("operative and a wave enemy"));
				return Finish(State, false);
			}
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.P = Leader->GetActorLocation();
			State.GroundZ = State.P.Z - Leader->GetSimpleCollisionHalfHeight();
			Kept->SetActorLocation(State.P - State.F * 4000.f);
			Kept->CustomTimeDilation = 0.f;
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			int32 Index = 0;
			const FVector R = FVector::CrossProduct(FVector::UpVector, State.F);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 300.f + R * (Index++ % 2 == 0 ? 300.f : -300.f), State.F.Rotation(), false, true);
				}
			}
			if (Op != Leader)
			{
				Op->StopOperative();
				Op->TeleportTo(State.P, State.F.Rotation(), false, true);
			}
			const FVector WallCentre = State.P + State.F * 600.f;
			const float WallHeight = State.bCrouch ? 60.f : 300.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + WallHeight * 0.5f), State.F.Rotation(),
				FVector(0.4f, State.bCorner ? 5.f : 10.f, WallHeight / 100.f));
			Check(State, State.Wall.IsValid(), TEXT("a 10 m wide wall spawned 6 m ahead"));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FVector Click = State.bCrouch ? FVector(0.f, 0.f, 30.f) : FVector(0.f, 0.f, 90.f);
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 580.f + Click, State.F, State.Slot);
			Check(State, bFound, TEXT("cover slot at the wall's middle"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			if (State.bCrouch)
			{
				Op->SetStance(EOperativeStance::Crouching);
			}
			if (State.bApproach)
			{
				State.Stage = 30;
				State.StageTime = 0.f;
				Op->bIgnoreCoverThreatForTesting = true;
				return true;
			}
			Op->bIgnoreCoverThreatForTesting = !State.bThreat; // unknown threat: face forward in the move direction
			Check(State, Op->OrderTakeCover(State.Slot, !State.bCrouch) == EOperativeOrderResult::Accepted, TEXT("run into the cover ordered"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!Op->bInCover || State.StageTime < 3.f)
			{
				if (State.StageTime > 15.f)
				{
					Check(State, false, TEXT("cover entered"));
					return Finish(State, false);
				}
				return true;
			}
			ReportClearance(State, TEXT("entry + idle"), -3.f);
			State.Slot = Op->GetCoverSlot();
			if (State.bThreat)
			{
				if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
					Op->GetActorLocation() + State.Slot.WallNormal * 800.f - State.Slot.RightTangent() * 500.f + FVector(0.f, 0.f, 20.f), (-State.Slot.WallNormal).Rotation()))
				{
					Hound->CustomTimeDilation = 0.f;
					Hound->GetHealthComponent()->SetMaxHealth(100000.f);
				}
				World->GetSubsystem<UTacticalSightSubsystem>()->Refresh();
				State.Stage = 20;
				State.StageTime = 0.f;
				return true;
			}
			Check(State, OrderShimmyAlong(World, *Op, State, State.bCorner ? -215.f : -400.f), TEXT("shimmy to his far side A ordered"));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 30:
		{
			// Next approach: out of the cover, onto the start point, a sprint / crouch-walk to the same slot.
			const FVector N = State.Slot.WallNormal.GetSafeNormal2D();
			const FVector T = State.Slot.RightTangent();
			const FVector Starts[] = { N * 500.f, N * 120.f + T * 450.f, N * 120.f - T * 450.f, N * 60.f + T * 180.f, N * 60.f - T * 180.f, N * 300.f + T * 300.f };
			const TCHAR* Names[] = { TEXT("head-on 5 m"), TEXT("along the wall from the right"), TEXT("along the wall from the left"), TEXT("skimming from the right"), TEXT("skimming from the left"), TEXT("diagonal") };
			if (State.Approach >= UE_ARRAY_COUNT(Starts))
			{
				return Finish(State, true);
			}
			if (Op->bInCover)
			{
				Op->LeaveCover(TEXT("smoke: next approach"));
			}
			Op->StopOperative();
			const FVector At = State.Slot.WorldLocation + Starts[State.Approach];
			Op->TeleportTo(FVector(At.X, At.Y, Op->GetActorLocation().Z), (-N).Rotation(), false, true);
			State.ApproachName = Names[State.Approach];
			State.MinClearance = 1.0e9f;
			State.CapsuleClearance = 1.0e9f;
			Op->OrderTakeCover(State.Slot, !State.bCrouch);
			State.Stage = 31;
			State.StageTime = 0.f;
			return true;
		}
		case 31:
		{
			if ((!Op->bInCover && State.StageTime < 10.f) || State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Op->bInCover, FString::Printf(TEXT("approach %s: in cover"), *State.ApproachName));
			{
				FString Clip;
				float MW = 0.f, SW = 0.f;
				if (Anim)
				{
					Anim->GetCoverPlayback(Clip, MW, SW);
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: actor depth %.1f cm (slot %.1f), clip %s w%.2f, loop %s, corner %d, firepose %d, yaw to normal %.0f"),
					FVector::DotProduct(Op->GetActorLocation() - State.Slot.WallPoint, State.Slot.WallNormal),
					FVector::DotProduct(State.Slot.WorldLocation - State.Slot.WallPoint, State.Slot.WallNormal), *Clip, MW, *LoopName(Anim),
					Op->bAtCoverCorner ? 1 : 0, Anim && Anim->IsInCoverFirePose() ? 1 : 0, Op->GetActorRotation().Yaw - State.Slot.WallNormal.Rotation().Yaw);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: mesh rel yaw %.1f loc %s, actor yaw %.1f"), Op->GetMesh()->GetRelativeRotation().Yaw,
				*Op->GetMesh()->GetRelativeLocation().ToString(), Op->GetActorRotation().Yaw);
			ReportClearance(State, *FString::Printf(TEXT("approach %s"), *State.ApproachName), 0.f);
			++State.Approach;
			State.Stage = 30;
			return true;
		}
		case 20:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			World->GetSubsystem<UTacticalSightSubsystem>()->Refresh();
			Check(State, Op->bHasCoverThreat && Op->CoverFacing == ECoverFacing::Left, FString::Printf(TEXT("threat on his left: faces left (threat %d)"), Op->bHasCoverThreat ? 1 : 0));
			Check(State, OrderShimmyAlong(World, *Op, State, State.bCorner ? -215.f : -400.f), TEXT("shimmy to the threat side A ordered"));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 1.0f)
			{
				return true;
			}
			Check(State, Op->bShimmying && Op->ShimmyDirection < 0.f && AlongVelocity(*Op, State) < -20.f,
				FString::Printf(TEXT("side A shimmy running (dir %.0f, v %.0f)"), Op->ShimmyDirection, AlongVelocity(*Op, State)));
			Check(State, Anim && LoopName(Anim).EndsWith(ExpectedLoop(*Op, State.bThreat)), FString::Printf(TEXT("... playing %s (loop %s)"), *ExpectedLoop(*Op, State.bThreat), *LoopName(Anim)));
			Check(State, OrderShimmyAlong(World, *Op, State, State.bCorner ? 215.f : 85.f), TEXT("reverse shimmy (side B) ordered while the first is running"));
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			// The target is still on the old side of the slot he started from (the bug: direction measured from that slot).
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->bShimmying && Op->ShimmyDirection > 0.f,
				FString::Printf(TEXT("after the reverse: still in cover, shimmying, direction %.0f"), Op->ShimmyDirection));
			Check(State, AlongVelocity(*Op, State) > 20.f, FString::Printf(TEXT("... moving to side B (v %.0f)"), AlongVelocity(*Op, State)));
			Check(State, Anim && Anim->bShimmying && Anim->ShimmyDirection > 0.f, TEXT("... the anim instance shimmies to side B"));
			Check(State, Anim && LoopName(Anim).EndsWith(ExpectedLoop(*Op, State.bThreat)), FString::Printf(TEXT("... playing %s (loop %s)"), *ExpectedLoop(*Op, State.bThreat), *LoopName(Anim)));
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			if (Op->bShimmying && State.StageTime < 12.f)
			{
				return true;
			}
			ReportClearance(State, TEXT("shimmy + reverse"), -3.f);
			Check(State, Op->bInCover && !Op->bShimmying, FString::Printf(TEXT("side B shimmy finished, still in cover (%.1f s)"), State.StageTime));
			State.Slot = Op->GetCoverSlot();
			Check(State, OrderShimmyAlong(World, *Op, State, -150.f), TEXT("shimmy back to side A ordered from standing"));
			State.Stage = 6;
			State.StageTime = 0.f;
			return true;
		}
		case 6:
		{
			if (State.StageTime < 0.9f)
			{
				return true;
			}
			Check(State, Op->bShimmying && Op->ShimmyDirection < 0.f && AlongVelocity(*Op, State) < -20.f,
				FString::Printf(TEXT("reverse from standing: direction %.0f, v %.0f"), Op->ShimmyDirection, AlongVelocity(*Op, State)));
			Check(State, Anim && LoopName(Anim).EndsWith(ExpectedLoop(*Op, State.bThreat)), FString::Printf(TEXT("... playing %s (loop %s)"), *ExpectedLoop(*Op, State.bThreat), *LoopName(Anim)));
			if (State.bCrouch)
			{
				ReportClearance(State, TEXT("crouched shimmies"), -3.f);
				return Finish(State, true);
			}
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		}
		case 7:
		{
			ReportClearance(State, TEXT("reverse from standing"), -3.f);
			const FVector Ground = Op->GetActorLocation() + State.Slot.RightTangent() * 350.f - State.Slot.WallNormal * 250.f;
			Check(State, Op->OrderMoveTo(Ground, false) == EOperativeOrderResult::Accepted, TEXT("a ground move order to the opposite side"));
			State.Stage = 8;
			State.StageTime = 0.f;
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			Check(State, !Op->bInCover && !Op->bShimmying, TEXT("the ground order left the cover"));
			Check(State, !Anim || !Anim->bInCover, FString::Printf(TEXT("... no cover state in the anim instance (loop %s)"), *LoopName(Anim)));
			const FVector Vel = Op->GetVelocity().GetSafeNormal2D();
			const float YawOff = FMath::Abs(FMath::FindDeltaAngleDegrees(Op->GetActorRotation().Yaw, static_cast<float>(Vel.Rotation().Yaw)));
			Check(State, Vel.SizeSquared2D() > 0.5f && YawOff < 60.f, FString::Printf(TEXT("... he turned to the walking direction (%.0f deg off)"), YawOff));
			return Finish(State, true);
		}
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
		State->bCrouch = Args.Contains(TEXT("crouch"));
		State->bMedic = Args.Contains(TEXT("medic"));
		State->bThreat = Args.Contains(TEXT("threat"));
		State->bCorner = Args.Contains(TEXT("corner"));
		State->bApproach = Args.Contains(TEXT("approach"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverReverseSmoke"),
		TEXT("Dev check: reversing the shimmy direction in cover flips the clip / movement (also mid-move); a ground order leaves the cover; body clearance to the wall; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	void RunWall(const TArray<FString>& Args, UWorld* World)
	{
		TArray<FString> All = Args;
		All.Add(TEXT("approach"));
		Run(All, World);
	}

	void RunWallMedic(const TArray<FString>& Args, UWorld* World)
	{
		TArray<FString> All = Args;
		All.Add(TEXT("medic"));
		RunWall(All, World);
	}

	static FAutoConsoleCommandWithWorldAndArgs WallCommand(
		TEXT("CodexTactics.CoverWallSmoke"),
		TEXT("Dev check: six approach angles into the same wall - no skin bone of the leader's mesh stays inside the wall; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunWall));

	static FAutoConsoleCommandWithWorldAndArgs WallMedicCommand(
		TEXT("CodexTactics.CoverWallMedicSmoke"),
		TEXT("Dev check: the same wall-clearance check for the Medic-Sapper (SK_Female_soldier); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunWallMedic));
}

#endif // !UE_BUILD_SHIPPING
