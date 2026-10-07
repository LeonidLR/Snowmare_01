// Dev-only headless check of the CROUCHED cover (user request 2026-10-07; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverCrouchSmoke -Log Smoke-CoverCrouch.log
// Every standing-cover fix, crouched, asserting the clip the mesh REALLY plays (FullBody montage + slot weight > 0.9):
//  low cover (the 60 cm barricade): crouched loop; a threat beyond it -> cvr_crch_fire_idle anywhere along it, the rifle popped over the
//   top in place (the clip's sideways lean cancelled); Ctrl + click -> a burst of >= 3 shots from the held crouched fire
//   stance (no crch idle / fire_to_idle in between); no target -> back after the grace; an enemy on his side -> open shot;
//  high wall, crouched at his right corner: stand -> crouch plays cvr_stand_idle_L_to_cvr_crch_idle_L (pack _L = his own
//   right), the threat round the corner -> cvr_crch_fire_idle_L, a crouched burst (crch fire_L), he stands the crouched
//   _L stand-off from the real edge; crouch-shimmies with the threat on his right: left = crch walk_bwd_loop_R, right =
//   crch walk_fwd_loop_L; crouch -> stand plays cvr_crch_idle_L_to_cvr_stand_idle_L.
// Nothing is saved (the wall / barricade are spawned at runtime).

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
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"

namespace CoverCrouchSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		FCoverSlot Low;
		FCoverSlot Corner;
		bool bAllowFire = false;
		int32 LeanBefore = 0;
		int32 OpenBefore = 0;
		int32 LogMark = 0;
		bool bBurstBroken = false;
		FString BurstBrokenBy;
		bool bSawLeftCover = false;
		bool bSampled = false;
		float MaxLateral = 0.f;
		float IKGapOn = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CoverCrouchSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString Playing(const AOperativeCharacter* Op, float* OutMontage = nullptr, float* OutSlot = nullptr)
	{
		FString Clip;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		if (const UOperativeAnimInstance* Anim = AnimOf(Op))
		{
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
		}
		if (OutMontage)
		{
			*OutMontage = MontageWeight;
		}
		if (OutSlot)
		{
			*OutSlot = SlotWeight;
		}
		return Clip;
	}

	/** The clip is really on the character: the active FullBody montage plays it at weight > 0.9 and the slot node is live. */
	void CheckPlaying(FState& State, const AOperativeCharacter* Op, const TCHAR* Contains, const FString& What)
	{
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		const FString Clip = Playing(Op, &MontageWeight, &SlotWeight);
		Check(State, Clip.Contains(Contains) && MontageWeight > 0.9f && SlotWeight > 0.9f,
			FString::Printf(TEXT("%s - actually playing '%s' (montage %.2f, FullBody slot %.2f; wanted *%s*)"), *What, *Clip, MontageWeight, SlotWeight, Contains));
	}

	bool LogHas(const AOperativeCharacter* Op, int32 From, const TCHAR* Contains)
	{
		if (const UOperativeAnimInstance* Anim = AnimOf(Op))
		{
			const TArray<FString>& Log = Anim->GetCoverClipLog();
			for (int32 Index = FMath::Clamp(From, 0, Log.Num()); Index < Log.Num(); ++Index)
			{
				if (Log[Index].Contains(Contains))
				{
					return true;
				}
			}
		}
		return false;
	}

	int32 LogNum(const AOperativeCharacter* Op)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		return Anim ? Anim->GetCoverClipLog().Num() : 0;
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

	void PlaceHound(FState& State, const FVector& Where)
	{
		if (AEnemyCharacter* Hound = State.Hound.Get())
		{
			Hound->SetActorLocation(FVector(Where.X, Where.Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f));
		}
	}

	void ClickHound(FState& State, UWorld* World)
	{
		State.bAllowFire = true;
		if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
		{
			PC->IssueTargetedShot(State.Hound.Get());
		}
		else if (AOperativeCharacter* Op = State.Op.Get())
		{
			Op->SetManualPriorityTarget(State.Hound.Get());
		}
	}

	void HoldFire(FState& State)
	{
		State.bAllowFire = false;
		if (AOperativeCharacter* Op = State.Op.Get())
		{
			Op->AssignPriorityTarget(nullptr);
		}
	}

	/** The burst: the playing clip stays the crouched fire stance (fire_idle / fire) between the shots. */
	void SampleBurst(FState& State, const AOperativeCharacter* Op)
	{
		float MontageWeight = 0.f;
		const FString Clip = Playing(Op, &MontageWeight);
		const bool bFireStance = Clip.Contains(TEXT("crch_fire_idle")) || Clip.EndsWith(TEXT("crch_fire_L")) || Clip.EndsWith(TEXT("crch_fire_R"));
		if (!bFireStance && MontageWeight > 0.5f && !State.bBurstBroken)
		{
			State.bBurstBroken = true;
			State.BurstBrokenBy = Clip;
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float& StageTime)
	{
		StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = !(State.bAllowFire && Member == Op);
		}
		if (Sight)
		{
			Sight->Refresh();
		}
		auto Next = [&State, &StageTime](int32 Stage)
		{
			State.Stage = Stage;
			StageTime = 0.f;
			return true;
		};
		switch (State.Stage)
		{
		case 0:
		{
			if (StageTime < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Op = Squad->GetLeader();
			State.Op = Op;
			// The legacy per-shot lean / sustained-aim mode (see CoverSmoke); the corner hold is CornerHoldSmoke.
			Op->bCornerHoldAtEdge = false;
			Op->bCornerAutoDuck = true;
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) - State.F * 6000.f, State.F.Rotation());
			if (!Hound)
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State);
			}
			Hound->CustomTimeDilation = 0.f;
			Hound->SetActorTickEnabled(false);
			Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			State.Hound = Hound;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Hound)
				{
					It->Destroy();
				}
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 400.f + State.R * (Index++ % 2 == 0 ? 250.f : -250.f), State.F.Rotation(), false, true);
				}
			}
			// The high wall (6 m x 3 m) 4 m ahead as in CoverSmoke; a 60 cm low cover (3 m) 7.5 m to the side (-R).
			const FVector WallCentre = State.P + State.F * 400.f;
			SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			// The game's 60 cm barricade beyond the wall's +R end (as CoverSmoke: the map has props on the -R side).
			const FVector LowCentre = State.P + State.F * 400.f + State.R * 550.f;
			FActorSpawnParameters BarricadeParams;
			BarricadeParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<ABarricadeActor>(FVector(LowCentre.X, LowCentre.Y, State.GroundZ + ABarricadeActor::HeightCm * 0.5f),
				FRotator(0.f, State.F.Rotation().Yaw + 90.f, 0.f), BarricadeParams);
			return Next(1);
		}
		case 1:
		{
			if (StageTime < 2.f)
			{
				return true; // the navmesh rebuilds
			}
			const bool bLow = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 375.f + State.R * 550.f + FVector(0.f, 0.f, 30.f),
				State.F, State.Low);
			Check(State, bLow && State.Low.Height == ECoverHeight::LowCover, TEXT("the 60 cm barricade is low cover"));
			if (!bLow)
			{
				return Finish(State);
			}
			Op->bIgnoreCoverThreatForTesting = true;
			Check(State, Op->OrderTakeCover(State.Low, true) == EOperativeOrderResult::Accepted, TEXT("cover order to the low cover"));
			return Next(2);
		}
		case 2:
		{
			if ((!Op->bInCover || StageTime < 2.5f) && StageTime < 12.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover && Op->GetStance() == EOperativeStance::Crouching,
				TEXT("low cover: crouched behind it"));
			Check(State, LogHas(Op, 0, TEXT("crch_idle_fwd_to_cvr_crch_idle")), TEXT("... entered through the CROUCHED enter clip"));
			CheckPlaying(State, Op, TEXT("cvr_crch_"), TEXT("... the crouched cover loop"));
			// A threat BEYOND the low cover: the fire stance anywhere along it.
			Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(State, State.P + State.F * 1000.f + State.R * 550.f);
			State.MaxLateral = 0.f;
			return Next(3);
		}
		case 3:
		{
			if (StageTime > 2.f)
			{
				const FVector Pelvis = Op->GetMesh()->GetBoneLocation(TEXT("pelvis"));
				State.MaxLateral = FMath::Max(State.MaxLateral, FMath::Abs(static_cast<float>(FVector::DotProduct(Pelvis - Op->GetActorLocation(), Op->GetCoverSlot().RightTangent()))));
			}
			if (StageTime < 3.5f)
			{
				return true;
			}
			Check(State, Op->IsCoverFireReady(), FString::Printf(TEXT("low cover, threat beyond it: fire-ready (at an edge: %d; no corner needed at a low cover)"), Op->bAtCoverCorner ? 1 : 0));
			CheckPlaying(State, Op, TEXT("cvr_crch_fire_idle_"), TEXT("... the crouched fire stance over the top"));
			Check(State, State.MaxLateral <= 25.f, FString::Printf(TEXT("... popped up in place: the clip's sideways lean cancelled (pelvis %.0f cm off the slot along the wall)"),
				State.MaxLateral));
			State.LeanBefore = Op->GetCoverLeanShots();
			State.bBurstBroken = false;
			State.BurstBrokenBy.Reset();
			State.LogMark = LogNum(Op);
			Op->RecentIncomingDamage = 0.f;
			ClickHound(State, World);
			return Next(4);
		}
		case 4:
		{
			if (Op->GetCoverLeanShots() > State.LeanBefore)
			{
				SampleBurst(State, Op);
			}
			if (Op->GetCoverLeanShots() < State.LeanBefore + 3 && StageTime < 8.f)
			{
				return true;
			}
			Check(State, Op->GetCoverLeanShots() >= State.LeanBefore + 3, FString::Printf(TEXT("low cover: a burst of %d shots over the top (%.1f s)"),
				Op->GetCoverLeanShots() - State.LeanBefore, StageTime));
			Check(State, Op->IsCornerAimActive() && !State.bBurstBroken, FString::Printf(TEXT("... held crouched fire stance, the clip stays crch fire_idle / fire (broken by '%s')"),
				*State.BurstBrokenBy));
			Check(State, !LogHas(Op, State.LogMark + 1, TEXT("fire_to_")), TEXT("... no fire_to_idle between the shots"));
			HoldFire(State);
			return Next(5);
		}
		case 5:
		{
			if (Op->IsCornerAimActive() && StageTime < 6.f)
			{
				return true;
			}
			Check(State, !Op->IsCornerAimActive() && Op->GetLastCornerAimBreak() == ECornerAimDecision::ReturnNoTargets,
				FString::Printf(TEXT("low cover: no target -> back down after the grace (%.1f s)"), StageTime));
			// An enemy on his side of the barricade: the open shot.
			PlaceHound(State, Op->GetActorLocation() - State.F * 600.f - State.R * 150.f);
			State.OpenBefore = Op->GetCoverOpenShots();
			State.bSawLeftCover = false;
			return Next(6);
		}
		case 6:
		{
			if (StageTime < 1.5f)
			{
				return true;
			}
			if (StageTime < 1.6f)
			{
				ClickHound(State, World);
			}
			State.bSawLeftCover |= !Op->bInCover && Op->IsCoverOpenShotActive();
			if (Op->GetCoverOpenShots() <= State.OpenBefore && StageTime < 7.f)
			{
				return true;
			}
			Check(State, Op->GetCoverOpenShots() > State.OpenBefore && State.bSawLeftCover, TEXT("low cover, enemy on his side: the open shot off the cover"));
			HoldFire(State);
			return Next(7);
		}
		case 7:
		{
			if (!Op->bInCover && StageTime < 6.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("... and back down behind the low cover"));
			// The high wall's corner on his RIGHT (-R end).
			Op->bIgnoreCoverThreatForTesting = true;
			PlaceHound(State, State.P - State.F * 6000.f);
			Op->LeaveCover(TEXT("smoke: to the high wall"));
			const bool bCorner = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f), State.F, State.Corner);
			Check(State, bCorner && State.Corner.Height == ECoverHeight::HighCover && State.Corner.bRightEdgeExposed, TEXT("the high wall's right-hand corner"));
			if (!bCorner)
			{
				return Finish(State);
			}
			Op->OrderTakeCover(State.Corner, true);
			return Next(8);
		}
		case 8:
		{
			if ((!Op->bInCover || StageTime < 2.5f) && StageTime < 12.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->GetStance() == EOperativeStance::Standing && Op->CoverFacing == ECoverFacing::Right,
				TEXT("at the high wall's right corner, standing, facing his right"));
			State.LogMark = LogNum(Op);
			Op->SetStance(EOperativeStance::Crouching);
			return Next(9);
		}
		case 9:
		{
			if (!State.bSampled && StageTime >= 0.4f)
			{
				State.bSampled = true;
				CheckPlaying(State, Op, TEXT("cvr_stand_idle_L_to_cvr_crch_idle_L"), TEXT("stand -> crouch at the wall: the pack's cover switch on his right side"));
			}
			if (StageTime < 2.5f)
			{
				return true;
			}
			State.bSampled = false;
			Check(State, Op->bInCover && Op->GetStance() == EOperativeStance::Crouching, TEXT("... crouched at the high wall, still in cover"));
			if (const UOperativeAnimInstance* IdleAnim = AnimOf(Op))
			{
				// 2026-10-07 (the node's effector pin connected): the cover idle keeps the IK; only a target out of reach fades it.
				Check(State, IdleAnim->LeftHandIKAlpha >= 0.99f || IdleAnim->LeftHandIKExcessCm > 0.f,
					FString::Printf(TEXT("... cover idle: left-hand IK on (alpha %.2f, excess %.1f cm)"), IdleAnim->LeftHandIKAlpha, IdleAnim->LeftHandIKExcessCm));
			}
			CheckPlaying(State, Op, TEXT("cvr_crch_"), TEXT("... the crouched cover pose"));
			// The threat round his right corner, behind the wall.
			Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(State, State.P + State.F * 700.f - State.R * 800.f);
			return Next(10);
		}
		case 10:
		{
			if (StageTime < 3.5f)
			{
				return true;
			}
			const float EdgeRight = 300.f + static_cast<float>(FVector::DotProduct(Op->GetActorLocation() - State.P, State.R));
			Check(State, EdgeRight <= 77.f - 10.f, FString::Printf(TEXT("crouched: the crch _L peek (77 cm) clears the real edge (%.0f cm away, stand-off %.0f)"),
				EdgeRight, CoverFacingRules::CornerStandOff(ECoverFacing::Right, true, Op->GetCoverFacingConfig())));
			CheckPlaying(State, Op, TEXT("cvr_crch_fire_idle_L"), TEXT("crouched at his right corner, threat round it: crch fire_idle_L"));
			// Left-hand IK (user-approved plan 2026-10-07): on in the crouched fire stance; the effector (hand_r space) lands
			// on the rifle's LeftHandGrip socket; the gap to the clip's left hand is what the ABP's Two Bone IK closes.
			if (const UOperativeAnimInstance* Anim = AnimOf(Op))
			{
				const FTransform HandR = Op->GetMesh()->GetSocketTransform(TEXT("hand_r"), RTS_World);
				const FVector Target = HandR.TransformPosition(Anim->LeftHandIKOffset);
				const FVector Socket = Op->WeaponMesh->GetSocketLocation(Anim->LeftHandGripSocket);
				const FVector HandL = Op->GetMesh()->GetBoneLocation(TEXT("hand_l"));
				const FTransform WeaponXf = Op->WeaponMesh->GetComponentTransform();
				const FVector Grip = HandR.GetLocation();
				const FVector Barrel = (WeaponXf.TransformPosition(Op->MuzzleOffset) - WeaponXf.TransformPosition(FVector(0.f, 0.f, 0.f))).GetSafeNormal();
				const FVector FromGrip = Target - Grip;
				const float OffBarrel = static_cast<float>((FromGrip - Barrel * FVector::DotProduct(FromGrip, Barrel)).Size());
				const float AlongBarrel = static_cast<float>(FVector::DotProduct(FromGrip, Barrel));
				Check(State, Anim->bLeftHandIKGripValid && Anim->LeftHandIKAlpha >= 0.99f,
					FString::Printf(TEXT("left-hand IK on in the crouched fire stance (alpha %.2f, grip socket %d)"), Anim->LeftHandIKAlpha, Anim->bLeftHandIKGripValid ? 1 : 0));
				const FVector Root = Op->GetMesh()->GetBoneLocation(Anim->LeftHandIKChainRoot);
				const float RootToTarget = static_cast<float>(FVector::Dist(Root, Target));
				Check(State, FMath::Abs(static_cast<float>(FVector::Dist(Target, Socket)) - Anim->LeftHandIKSlideCm) <= 1.f
					&& RootToTarget <= Anim->LeftHandArmReach * Anim->LeftHandIKReachFraction + 1.f,
					FString::Printf(TEXT("... the IK target: the handguard socket slid %.1f cm back along the barrel into reach (%s -> target %.1f cm, reach %.1f x %.2f; %.0f cm along the barrel from the grip, %.1f cm off its axis)"),
						Anim->LeftHandIKSlideCm, *Anim->LeftHandIKChainRoot.ToString(), RootToTarget, Anim->LeftHandArmReach, Anim->LeftHandIKReachFraction, AlongBarrel, OffBarrel));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: left hand (clip) %.1f cm from the IK target: the gap the ABP's Two Bone IK closes"),
					FVector::Dist(HandL, Target));
				// What a clavicle-rooted chain (FABRIK clavicle_l -> hand_l) would reach: the socket itself?
				const FReferenceSkeleton& Ref = Op->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
				const int32 UpperIndex = Ref.FindBoneIndex(TEXT("upperarm_l"));
				const float ClavicleReach = Anim->LeftHandArmReach + (UpperIndex != INDEX_NONE ? static_cast<float>(Ref.GetRefBonePose()[UpperIndex].GetLocation().Size()) : 0.f);
				const float ClavicleToSocket = static_cast<float>(FVector::Dist(Op->GetMesh()->GetBoneLocation(TEXT("clavicle_l")), Socket));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: with a clavicle_l chain: socket %.1f cm from clavicle_l, reach %.1f (x %.2f = %.1f): %s"),
					ClavicleToSocket, ClavicleReach, Anim->LeftHandIKReachFraction, ClavicleReach * Anim->LeftHandIKReachFraction,
					ClavicleToSocket <= ClavicleReach * Anim->LeftHandIKReachFraction ? TEXT("the handguard itself is reachable") : TEXT("still short"));
			}
			// What the ABP's IK node does: the final left hand vs the target with alpha 1, then forced 0 (stage 30).
			if (UOperativeAnimInstance* Anim = AnimOf(Op))
			{
				const FTransform HandR = Op->GetMesh()->GetSocketTransform(TEXT("hand_r"), RTS_World);
				State.IKGapOn = static_cast<float>(FVector::Dist(Op->GetMesh()->GetBoneLocation(TEXT("hand_l")), HandR.TransformPosition(Anim->LeftHandIKOffset)));
				Anim->LeftHandIKAlphaOverrideForTesting = 0.f;
			}
			return Next(30);
		}
		case 30:
		{
			if (StageTime < 0.3f)
			{
				return true;
			}
			if (UOperativeAnimInstance* Anim = AnimOf(Op))
			{
				const FTransform HandR = Op->GetMesh()->GetSocketTransform(TEXT("hand_r"), RTS_World);
				const float GapOff = static_cast<float>(FVector::Dist(Op->GetMesh()->GetBoneLocation(TEXT("hand_l")), HandR.TransformPosition(Anim->LeftHandIKOffset)));
				Anim->LeftHandIKAlphaOverrideForTesting = -1.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: ABP IK node effect: final hand_l -> IK target %.1f cm with alpha 1, %.1f cm with alpha 0 (%s)"),
					State.IKGapOn, GapOff, FMath::Abs(GapOff - State.IKGapOn) < 1.f ? TEXT("NO EFFECT: node missing / after Output Pose / alpha or effector pin unconnected")
					: State.IKGapOn < 2.f ? TEXT("the node reaches the target") : TEXT("the node moves the hand but not onto the target: check its effector space / target bone"));
			}
			State.LeanBefore = Op->GetCoverLeanShots();
			State.bBurstBroken = false;
			State.BurstBrokenBy.Reset();
			State.LogMark = LogNum(Op);
			Op->RecentIncomingDamage = 0.f;
			ClickHound(State, World);
			return Next(11);
		}
		case 11:
		{
			if (Op->GetCoverLeanShots() > State.LeanBefore)
			{
				SampleBurst(State, Op);
			}
			if (Op->GetCoverLeanShots() < State.LeanBefore + 3 && StageTime < 8.f)
			{
				return true;
			}
			Check(State, Op->GetCoverLeanShots() >= State.LeanBefore + 3 && LogHas(Op, State.LogMark, TEXT("crch_fire_L")),
				FString::Printf(TEXT("crouched corner burst: %d shots, crch fire_L"), Op->GetCoverLeanShots() - State.LeanBefore));
			Check(State, Op->IsCornerAimActive() && !State.bBurstBroken, FString::Printf(TEXT("... the held crouched fire stance (broken by '%s')"), *State.BurstBrokenBy));
			HoldFire(State);
			return Next(12);
		}
		case 12:
		{
			if (Op->IsCornerAimActive() && StageTime < 6.f)
			{
				return true;
			}
			// Crouch-shimmies with the threat on his right (in front, to his right: the facing stays right).
			PlaceHound(State, State.P - State.F * 400.f - State.R * 900.f);
			if (StageTime < 1.5f)
			{
				return true;
			}
			FCoverSlot Target;
			const bool bOk = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), State.P + State.F * 380.f + FVector(0.f, 0.f, 90.f), Target)
				&& Op->OrderShimmyTo(Target) == EOperativeOrderResult::Accepted;
			Check(State, bOk && Op->ShimmyDirection < 0.f && !Op->IsShimmyForward() && Op->GetStance() == EOperativeStance::Crouching,
				TEXT("crouch-shimmy to his left, threat on his right: backwards"));
			return Next(13);
		}
		case 13:
		{
			if (!State.bSampled && StageTime >= 0.6f)
			{
				State.bSampled = true;
				CheckPlaying(State, Op, TEXT("cvr_crch_walk_bwd_loop_R"), TEXT("... crch walk_bwd_loop_R"));
			}
			if (Op->bShimmying && StageTime < 8.f)
			{
				return true;
			}
			State.bSampled = false;
			FCoverSlot Target;
			const bool bOk = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), State.P + State.F * 380.f - State.R * 150.f + FVector(0.f, 0.f, 90.f), Target)
				&& Op->OrderShimmyTo(Target) == EOperativeOrderResult::Accepted;
			Check(State, bOk && Op->ShimmyDirection > 0.f && Op->IsShimmyForward(), TEXT("crouch-shimmy back to his right, towards the threat: forward"));
			return Next(14);
		}
		case 14:
		{
			if (!State.bSampled && StageTime >= 0.6f)
			{
				State.bSampled = true;
				CheckPlaying(State, Op, TEXT("cvr_crch_walk_fwd_loop_L"), TEXT("... crch walk_fwd_loop_L"));
			}
			if (Op->bShimmying && StageTime < 8.f)
			{
				return true;
			}
			if (StageTime < 1.5f)
			{
				return true;
			}
			State.bSampled = false;
			State.LogMark = LogNum(Op);
			Op->bIgnoreCoverThreatForTesting = true; // the plain idle (no fire stance) before standing up
			Op->SetStance(EOperativeStance::Standing);
			return Next(15);
		}
		case 15:
		{
			if (!State.bSampled && StageTime >= 0.4f)
			{
				State.bSampled = true;
				const FString Side = Op->CoverFacing == ECoverFacing::Right ? TEXT("L") : TEXT("R");
				CheckPlaying(State, Op, *FString::Printf(TEXT("cvr_crch_idle_%s_to_cvr_stand_idle_%s"), *Side, *Side),
					TEXT("crouch -> stand at the wall: the pack's cover switch on the facing side"));
			}
			if (StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->GetStance() == EOperativeStance::Standing, TEXT("... standing at the wall again"));
			Op->bIgnoreCoverThreatForTesting = false;
			// A reload: the left hand leaves the handguard for the magazine (IK alpha 0).
			Op->CurrentClip = 0;
			Op->ReserveAmmo = FMath::Max(Op->ReserveAmmo, 30);
			// The reload timer runs in the combat tick, which a cease-fire skips: fire allowed, nobody in reach.
			PlaceHound(State, State.P - State.F * 6000.f);
			State.bAllowFire = true;
			Op->StartReload();
			return Next(16);
		}
		case 16:
		{
			if (StageTime < 0.4f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Op);
			Check(State, Op->bIsReloading && Anim && Anim->LeftHandIKAlpha <= 0.01f,
				FString::Printf(TEXT("reloading: left-hand IK off (alpha %.2f)"), Anim ? Anim->LeftHandIKAlpha : -1.f));
			State.bSampled = false;
			return Next(17);
		}
		case 17:
		{
			if (Op->bIsReloading && StageTime < 6.f)
			{
				return true;
			}
			if (!State.bSampled)
			{
				State.bSampled = true; // the reload is over: out of cover (the IK is on in the locomotion; the cover idle keeps it off)
				Op->LeaveCover(TEXT("smoke: left-hand IK out of cover"));
				StageTime = 0.f;
				return true;
			}
			if (StageTime < 0.5f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Op);
			Check(State, !Op->bIsReloading && Anim && Anim->LeftHandIKAlpha >= 0.99f,
				FString::Printf(TEXT("... and back on after the reload, out of cover (alpha %.2f; grip %d, anim reloading %d, weapon visible %d, stance %d, %.1f s)"),
					Anim ? Anim->LeftHandIKAlpha : -1.f, Anim && Anim->bLeftHandIKGripValid ? 1 : 0, Anim && Anim->bIsReloading ? 1 : 0,
					Op->WeaponMesh && Op->WeaponMesh->IsVisible() ? 1 : 0, static_cast<int32>(Op->GetStance()), StageTime));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: reload diag: panicking %d, cease %d, alive %d"), Op->IsPanicking() ? 1 : 0,
				Op->bTacticalCeaseFire ? 1 : 0, Op->HealthComponent && Op->HealthComponent->IsAlive() ? 1 : 0);
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		TSharedRef<float> StageTime = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State, StageTime](float)
		{
			State->Time += 0.05f;
			if (State->Time > 150.f)
			{
				Check(*State, false, TEXT("timed out"));
				return Finish(*State);
			}
			return Step(WeakWorld, *State, *StageTime);
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverCrouchSmoke"),
		TEXT("Dev check of the crouched cover (low cover pop-up, crouched corner, burst, shimmies, stance switches); PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
