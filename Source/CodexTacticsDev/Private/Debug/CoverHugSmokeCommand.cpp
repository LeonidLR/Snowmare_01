// Dev-only headless check of wall hugging in EXPLORATION (user bugs 2026-10-11: "wall hugging doesn't work again" - at the
// corner of a high wall the feed said "Hugging the wall, holding the corner" but the body stood off the wall in a rifle
// stance looking somewhere else; "ordered along the wall he walks backwards"):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverHugSmoke -Log Smoke-CoverHug.log
//   Scripts/smoke.ps1 -Command "CodexTactics.CoverHugSmoke medic"   (the Medic-Sapper = SK_Female_soldier)
//   Scripts/smoke.ps1 -Command "CodexTactics.CoverHugSmoke legacy"  (bCornerHoldOnlyWhenEngaged off: must FAIL at the corner)
// No fight is started and every enemy is removed (game start, nobody known). A 5 m wide, 3 m high wall is spawned 6 m
// ahead of the leader on L_MovementTest (nothing is saved); he runs into the cover
//  1. at the wall's middle: the hug - capsule edge close to the wall, no mesh push away from it, the plain cover idle;
//  2. 80 cm from its left edge (a corner, the only threat-free case of the corner hold): still the hug - no corner aim /
//     fire stance (no target), the look-around corner clip, capsule and mesh at the wall;
//  3. shimmies away from the edge and back to it: with no threat he walks FORWARD both ways (walk_fwd_loop_*), never the
//     backward clip, and stands hugging at the corner again.
// Every frame in cover the closest skin bone to the wall plane is sampled: nothing stays inside the wall (no clipping).
// Finally (rev 5c) a roof slab is laid over the wall's front (eaves): clicks on the wall under it still find the high cover.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
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

namespace CoverHugSmoke
{
	/** Hug limits: the capsule edge this close to the wall, the mesh pushed off it at most this much (cm). */
	constexpr float MaxCapsuleGapCm = 12.f;
	constexpr float MaxMeshPushCm = 4.f;
	/**
	 * Hugging: the closest skin bone at most this far from the wall plane (male: the backpack bone 26-29 cm; the Female
	 * Soldier has no backpack bone, her closest is ~43 cm) and the pelvis at most this far (both ~58 cm in the cover idle).
	 */
	constexpr float MaxClosestBoneCm = 50.f;
	constexpr float MaxPelvisCm = 70.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		float InCoverTime = 0.f; // since the last cover entry
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ForwardVector;
		FVector F = FVector::ForwardVector;
		bool bMedic = false;
		bool bLegacy = false; // the 2026-10-07 corner hold at every exposed edge (shows the bug)
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		FVector DefaultMeshLocation = FVector::ZeroVector;
		float DefaultMeshYaw = 0.f;
		float MinClearance = 1.0e9f;
		float SecondsInside = 0.f;
		bool bSawBackward = false;
		bool bSawCornerAim = false;
		bool bSawFirePose = false;
		FString Clips;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CoverHugSmoke"));
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

	/** Closest skin bone (no IK / virtual / root bone) to the wall plane, cm; negative = inside the wall. */
	float ClosestBoneDepth(const AOperativeCharacter& Op, const FCoverSlot& Slot, FName* OutBone = nullptr)
	{
		USkeletalMeshComponent* Mesh = Op.GetMesh();
		float Min = 1.0e9f;
		if (!Mesh)
		{
			return Min;
		}
		const FVector Normal = Slot.WallNormal.GetSafeNormal2D();
		TArray<FName> Names;
		Mesh->GetBoneNames(Names);
		for (const FName Bone : Names)
		{
			const FString Name = Bone.ToString();
			if (Name.StartsWith(TEXT("ik_")) || Name.StartsWith(TEXT("VB ")) || Name.Equals(TEXT("root")))
			{
				continue;
			}
			const FVector BoneLocation = Mesh->GetBoneLocation(Bone);
			if (!CoverFacingRules::IsWithinWallSpan(Op.GetCoverSlot(), BoneLocation))
			{
				continue; // past an exposed edge: no wall behind the face plane there
			}
			const float Depth = static_cast<float>(FVector::DotProduct(BoneLocation - Slot.WallPoint, Normal));
			if (Depth < Min)
			{
				Min = Depth;
				if (OutBone)
				{
					*OutBone = Bone;
				}
			}
		}
		return Min;
	}

	/** The mesh's offset from its default place relative to the capsule, world cm along the wall normal (+ = off the wall). */
	float MeshPush(const AOperativeCharacter& Op, const FState& State)
	{
		const FVector Offset = Op.GetMesh()->GetRelativeLocation() - State.DefaultMeshLocation;
		return static_cast<float>(FVector::DotProduct(Op.GetActorRotation().RotateVector(Offset), State.Slot.WallNormal.GetSafeNormal2D()));
	}

	void Sample(FState& State, const AOperativeCharacter& Op, const UOperativeAnimInstance* Anim, float DeltaTime)
	{
		if (!Op.bInCover)
		{
			return;
		}
		const float Depth = ClosestBoneDepth(Op, State.Slot);
		State.MinClearance = FMath::Min(State.MinClearance, Depth);
		State.SecondsInside += Depth < 0.f ? DeltaTime : 0.f;
		const FString Loop = LoopName(Anim);
		State.bSawBackward |= Loop.Contains(TEXT("walk_bwd"));
		State.bSawCornerAim |= Op.IsCornerAimActive();
		State.bSawFirePose |= Anim && Anim->IsInCoverFirePose();
		if (!State.Clips.EndsWith(Loop))
		{
			State.Clips += (State.Clips.IsEmpty() ? TEXT("") : TEXT(" > ")) + Loop;
		}
	}

	/** The hug at rest: capsule at the wall, mesh not pushed off it, the anim in cover, no fire stance without a target. */
	void CheckHug(FState& State, const AOperativeCharacter& Op, const UOperativeAnimInstance* Anim, const TCHAR* Where, bool bCorner)
	{
		const FVector Normal = State.Slot.WallNormal.GetSafeNormal2D();
		const float CapsuleGap = static_cast<float>(FVector::DotProduct(Op.GetActorLocation() - State.Slot.WallPoint, Normal)) - Op.GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float Push = MeshPush(Op, State);
		FName Bone;
		const float Closest = ClosestBoneDepth(Op, State.Slot, &Bone);
		const float Pelvis = static_cast<float>(FVector::DotProduct(Op.GetMesh()->GetBoneLocation(TEXT("pelvis")) - State.Slot.WallPoint, Normal));
		FString Clip;
		float MontageWeight = 0.f, SlotWeight = 0.f;
		if (Anim)
		{
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
		}
		const float ActorYawToNormal = FRotator::NormalizeAxis(Op.GetActorRotation().Yaw - static_cast<float>(Normal.Rotation().Yaw));
		const float MeshYaw = FRotator::NormalizeAxis(Op.GetMesh()->GetRelativeRotation().Yaw - State.DefaultMeshYaw);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: capsule gap %.1f cm, mesh push %.1f cm, closest bone %s %.1f cm, pelvis %.1f cm, clip %s w%.2f slot %.2f, loop %s, corner %d, corner aim %d, fire ready %d, fire pose %d, threat %d, facing %s, actor yaw - normal %.1f, mesh yaw %+.1f, edges L %.0f%s R %.0f%s"),
			Where, CapsuleGap, Push, *Bone.ToString(), Closest, Pelvis, *Clip, MontageWeight, SlotWeight, *LoopName(Anim), Op.bAtCoverCorner ? 1 : 0,
			Op.IsCornerAimActive() ? 1 : 0, Op.IsCoverFireReady() ? 1 : 0, Anim && Anim->IsInCoverFirePose() ? 1 : 0, Op.bHasCoverThreat ? 1 : 0,
			Op.CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), ActorYawToNormal, MeshYaw,
			Op.GetCoverSlot().LeftEdgeDistanceCm, Op.GetCoverSlot().bLeftEdgeExposed ? TEXT("") : TEXT(" (closed)"),
			Op.GetCoverSlot().RightEdgeDistanceCm, Op.GetCoverSlot().bRightEdgeExposed ? TEXT("") : TEXT(" (closed)"));
		Check(State, Op.bInCover && Anim && Anim->bInCover, FString::Printf(TEXT("%s: in cover, the anim instance too"), Where));
		Check(State, CapsuleGap <= MaxCapsuleGapCm, FString::Printf(TEXT("%s: capsule edge %.1f cm from the wall (<= %.0f)"), Where, CapsuleGap, MaxCapsuleGapCm));
		Check(State, FMath::Abs(Push) <= MaxMeshPushCm, FString::Printf(TEXT("%s: mesh pushed %.1f cm off the wall (<= %.0f)"), Where, Push, MaxMeshPushCm));
		Check(State, Closest >= -1.f && Closest <= MaxClosestBoneCm && Pelvis <= MaxPelvisCm,
			FString::Printf(TEXT("%s: body at the wall - closest bone %s %.1f cm (0 .. %.0f), pelvis %.1f cm (<= %.0f)"), Where, *Bone.ToString(), Closest, MaxClosestBoneCm, Pelvis, MaxPelvisCm));
		Check(State, FMath::Abs(ActorYawToNormal) <= 10.f && FMath::Abs(MeshYaw) <= 2.f,
			FString::Printf(TEXT("%s: back to the wall (actor yaw %.1f off the normal, mesh yaw %+.1f)"), Where, ActorYawToNormal, MeshYaw));
		Check(State, !Op.IsCornerAimActive() && !(Anim && Anim->IsInCoverFirePose()) && !Clip.Contains(TEXT("fire")),
			FString::Printf(TEXT("%s: no target -> no corner fire stance (clip %s)"), Where, *Clip));
		const TCHAR* Expected = bCorner ? TEXT("look_at_idle") : TEXT("_idle");
		Check(State, Clip.Contains(Expected) && MontageWeight > 0.9f && SlotWeight > 0.9f,
			FString::Printf(TEXT("%s: the hug clip plays (%s, wants *%s*, w%.2f, slot %.2f)"), Where, *Clip, Expected, MontageWeight, SlotWeight));
	}

	/** The body's yaw on the mesh vs its default (an entry turn held on the mesh must be gone), deg. */
	float MeshYawOff(const AOperativeCharacter& Op, const FState& State)
	{
		return FRotator::NormalizeAxis(Op.GetMesh()->GetRelativeRotation().Yaw - State.DefaultMeshYaw);
	}

	/** Orders a shimmy to the point `Along` cm from the current position along the wall (+ = RightTangent). */
	bool OrderShimmyAlong(UWorld* World, AOperativeCharacter& Op, const FState& State, float Along)
	{
		const FVector Click = Op.GetActorLocation() + State.Slot.RightTangent() * Along - State.Slot.WallNormal * 40.f + FVector(0.f, 0.f, 90.f);
		FCoverSlot Shimmy;
		return CoverTraceRules::FindShimmySlot(World, Op.GetCoverSlot(), Click, Shimmy) && Op.OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
	}

	bool OrderCoverAt(UWorld* World, AOperativeCharacter& Op, FState& State, float Along, const TCHAR* What)
	{
		const FVector Click = State.P + State.F * 580.f + FVector::CrossProduct(FVector::UpVector, State.F) * Along + FVector(0.f, 0.f, 90.f);
		const bool bFound = CoverTraceRules::FindCoverSlotAt(World, Click, State.F, State.Slot);
		Check(State, bFound, FString::Printf(TEXT("cover slot %s found"), What));
		return bFound && Op.OrderTakeCover(State.Slot, /*bSprint*/ true) == EOperativeOrderResult::Accepted;
	}

	void ResetSamples(FState& State)
	{
		State.MinClearance = 1.0e9f;
		State.SecondsInside = 0.f;
		State.bSawBackward = false;
		State.bSawCornerAim = false;
		State.bSawFirePose = false;
		State.Clips.Reset();
	}

	void CheckNoClipping(FState& State, const TCHAR* Where)
	{
		Check(State, State.MinClearance >= -3.f || State.SecondsInside <= 0.15f,
			FString::Printf(TEXT("%s: no bone inside the wall for more than 0.15 s (min %.1f cm, %.2f s inside)"), Where, State.MinClearance, State.SecondsInside));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += FMath::Min(DeltaTime, 0.5f); // a loading hitch does not eat the time budget
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 120.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		State.InCoverTime = Op && Op->bInCover ? State.InCoverTime + DeltaTime : 0.f;
		if (Op && State.Stage >= 2)
		{
			Sample(State, *Op, Anim, DeltaTime);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// Exploration as at the game start: no fight, nobody known (the user's report).
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			int32 Enemies = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				++Enemies;
				It->Destroy();
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: phase %d, wave active %d, %d enemies removed"), Flow ? static_cast<int32>(Flow->GetPhase()) : -1,
				Flow && Flow->IsWaveActive() ? 1 : 0, Enemies);
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
			if (!Op || !Leader)
			{
				Check(State, false, TEXT("operative"));
				return Finish(State, false);
			}
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.P = Leader->GetActorLocation();
			const float GroundZ = State.P.Z - Leader->GetSimpleCollisionHalfHeight();
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Op->bCornerHoldOnlyWhenEngaged = !State.bLegacy;
			State.DefaultMeshLocation = Op->GetMesh()->GetRelativeLocation();
			State.DefaultMeshYaw = Op->GetMesh()->GetRelativeRotation().Yaw;
			int32 Index = 0;
			const FVector R = FVector::CrossProduct(FVector::UpVector, State.F);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
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
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 5.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("a 5 m wide, 3 m high wall spawned 6 m ahead"));
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
			if (!OrderCoverAt(World, *Op, State, 0.f, TEXT("at the wall's middle")))
			{
				return Finish(State, false);
			}
			ResetSamples(State);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!Op->bInCover || State.InCoverTime < 3.f)
			{
				if (State.StageTime > 15.f)
				{
					Check(State, false, TEXT("middle: cover entered"));
					return Finish(State, false);
				}
				return true;
			}
			if (Op->bShimmying && State.StageTime < 8.f)
			{
				return true;
			}
			State.Slot = Op->GetCoverSlot();
			CheckHug(State, *Op, Anim, TEXT("middle"), false);
			CheckNoClipping(State, TEXT("middle entry"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: middle clips: %s"), *State.Clips);
			// Back to the start, then run to the wall 80 cm in from its left edge (the user's corner).
			Op->LeaveCover(TEXT("smoke: next entry"));
			Op->StopOperative();
			Op->TeleportTo(State.P, State.F.Rotation(), false, true);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// Left = -RightTangent of a slot facing back towards the start = -R; the wall spans +-250 cm.
			if (!OrderCoverAt(World, *Op, State, 170.f, TEXT("80 cm from an edge")))
			{
				return Finish(State, false);
			}
			ResetSamples(State);
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			if (!Op->bInCover || State.InCoverTime < 4.f || (Op->bShimmying && State.StageTime < 12.f))
			{
				if (State.StageTime > 18.f)
				{
					Check(State, false, TEXT("corner: cover entered"));
					return Finish(State, false);
				}
				return true;
			}
			State.Slot = Op->GetCoverSlot();
			CheckHug(State, *Op, Anim, TEXT("corner"), true);
			Check(State, Op->bAtCoverCorner, TEXT("corner: at the exposed edge"));
			Check(State, !State.bSawCornerAim && !State.bSawFirePose, FString::Printf(TEXT("corner: never stepped out into the fire stance on the way in (clips %s)"), *State.Clips));
			CheckNoClipping(State, TEXT("corner entry"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: corner clips: %s"), *State.Clips);
			ResetSamples(State);
			// Away from the edge along the wall.
			const float Away = Op->CoverFacing == ECoverFacing::Left ? 160.f : -160.f;
			Check(State, OrderShimmyAlong(World, *Op, State, Away), TEXT("shimmy away from the edge ordered"));
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			if (State.StageTime < 0.8f)
			{
				return true;
			}
			Check(State, Op->bShimmying && Anim && Anim->bShimmying, TEXT("away from the edge: shimmying"));
			Check(State, LoopName(Anim).Contains(TEXT("walk_fwd")) && Op->IsShimmyForward(),
				FString::Printf(TEXT("away from the edge: walks FORWARD (loop %s, forward %d, facing %s, dir %.0f)"), *LoopName(Anim), Op->IsShimmyForward() ? 1 : 0,
					Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Op->ShimmyDirection));
			Check(State, FMath::Abs(MeshYawOff(*Op, State)) <= 2.f, FString::Printf(TEXT("away from the edge: the body faces along the wall, not turned round (mesh yaw %+.1f)"), MeshYawOff(*Op, State)));
			State.Stage = 6;
			State.StageTime = 0.f;
			return true;
		}
		case 6:
		{
			if (Op->bShimmying && State.StageTime < 10.f)
			{
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, !State.bSawBackward, FString::Printf(TEXT("away from the edge: no backward clip (clips %s)"), *State.Clips));
			ResetSamples(State);
			State.Slot = Op->GetCoverSlot();
			const ECoverFacing EdgeSide = State.Slot.bLeftEdgeExposed && (!State.Slot.bRightEdgeExposed || State.Slot.LeftEdgeDistanceCm < State.Slot.RightEdgeDistanceCm)
				? ECoverFacing::Left : ECoverFacing::Right;
			const float EdgeDistance = EdgeSide == ECoverFacing::Left ? State.Slot.LeftEdgeDistanceCm : State.Slot.RightEdgeDistanceCm;
			const float Back = (EdgeSide == ECoverFacing::Left ? -1.f : 1.f) * FMath::Max(EdgeDistance - 40.f, 20.f);
			Check(State, OrderShimmyAlong(World, *Op, State, Back), TEXT("shimmy back to the edge ordered"));
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.8f)
			{
				return true;
			}
			Check(State, LoopName(Anim).Contains(TEXT("walk_fwd")) && Op->IsShimmyForward(),
				FString::Printf(TEXT("back to the edge: walks FORWARD (loop %s, facing %s, dir %.0f)"), *LoopName(Anim),
					Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Op->ShimmyDirection));
			Check(State, FMath::Abs(MeshYawOff(*Op, State)) <= 2.f, FString::Printf(TEXT("back to the edge: the body faces along the wall, not turned round (mesh yaw %+.1f)"), MeshYawOff(*Op, State)));
			State.Stage = 8;
			State.StageTime = 0.f;
			return true;
		}
		case 8:
		{
			if ((Op->bShimmying && State.StageTime < 10.f) || State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, !State.bSawBackward, FString::Printf(TEXT("back to the edge: no backward clip (clips %s)"), *State.Clips));
			State.Slot = Op->GetCoverSlot();
			CheckHug(State, *Op, Anim, TEXT("corner after the shimmy"), true);
			Check(State, !State.bSawCornerAim && !State.bSawFirePose, TEXT("corner after the shimmy: never stepped out into the fire stance"));
			CheckNoClipping(State, TEXT("shimmies"));
			{
				// Eaves (rev 5c, the Demo 01 fishermen's hut): a roof slab 3.3-3.8 m up juts 1.2 m out over the wall's front. A click
				// on the wall under it still finds the high cover (CoverTraceRules::FindGround once started inside the roof).
				const float GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
				const FVector Roof = State.P + State.F * 560.f;
				AStaticMeshActor* Eaves = SpawnBlock(World, FVector(Roof.X, Roof.Y, GroundZ + 355.f), State.F.Rotation(), FVector(2.f, 5.f, 0.5f));
				const FVector R = FVector::CrossProduct(FVector::UpVector, State.F);
				for (const float ClickZ : { 100.f, 200.f, 290.f })
				{
					FCoverSlot Slot;
					const FVector Click = State.P + State.F * 580.f + R * 100.f + FVector(0.f, 0.f, GroundZ - State.P.Z + ClickZ);
					const bool bFound = Eaves && CoverTraceRules::FindCoverSlotAt(World, Click, State.F, Slot);
					Check(State, bFound && Slot.Height == ECoverHeight::HighCover && FMath::Abs(Slot.WorldLocation.Z - GroundZ) < 30.f,
						FString::Printf(TEXT("under eaves: a click %.0f cm up the wall finds the high cover (found %d, height %d, slot z %.0f, ground %.0f)"),
							ClickZ, bFound ? 1 : 0, static_cast<int32>(Slot.Height), Slot.WorldLocation.Z, GroundZ));
				}
			}
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
		State->bMedic = Args.Contains(TEXT("medic"));
		State->bLegacy = Args.Contains(TEXT("legacy"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverHugSmoke"),
		TEXT("Dev check: in exploration (no target) the operative hugs a high wall at its middle and at a corner (capsule / mesh at the wall, cover idle / look-around clip, no corner fire stance) and shimmies forward both ways; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	void RunMedic(const TArray<FString>& Args, UWorld* World)
	{
		TArray<FString> All = Args;
		All.Add(TEXT("medic"));
		Run(All, World);
	}

	static FAutoConsoleCommandWithWorldAndArgs MedicCommand(
		TEXT("CodexTactics.CoverHugMedicSmoke"),
		TEXT("Dev check: the same wall-hug check for the Medic-Sapper (SK_Female_soldier); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMedic));
}

#endif // !UE_BUILD_SHIPPING
