// Dev-only check of the left-hand IK on every cover pose (2026-10-07; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.LeftHandIKSmoke -Log Smoke-LeftHandIK.log
// Needs the user's ABP_Operative with the Two Bone IK node (effector = LeftHandIKOffset in hand_r bone space, alpha =
// LeftHandIKAlpha). A 6 m x 3 m wall 4 m ahead (nothing saved). Poses sampled (standing and crouched): cover idle,
// shimmy forward / backward, the corner look-around, the fire stance, the stance switch, the cover enter. Per pose: the
// clip playing, alpha, slide / excess, the FINAL left hand vs the IK target, the arm extension (upperarm_l -> hand_l over
// upper + lower arm) and the left hand's distance from the wall face. Checks: with alpha 1 the hand is on the target
// (<= 3 cm) and the arm is never straight (<= 96 % extension); a pose whose clip puts the left hand on the wall is
// reported (a candidate for LeftHandIKFreeClips).

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
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverTraceRules.h"

namespace LeftHandIKSmoke
{
	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		int32 Samples = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		bool bSampled = false;
		float MaxExtensionOn = 0.f;
		float MaxGapOn = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %d poses, with the IK on: max hand -> target %.1f cm, max arm extension %.0f %%"),
			State.Samples, State.MaxGapOn, State.MaxExtensionOn * 100.f);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LeftHandIKSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	void Sample(FState& State, const TCHAR* Pose)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		if (!Op || !Anim)
		{
			return;
		}
		USkeletalMeshComponent* Body = Op->GetMesh();
		FString Clip;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
		const FTransform HandR = Body->GetSocketTransform(TEXT("hand_r"), RTS_World);
		const FVector Target = HandR.TransformPosition(Anim->LeftHandIKOffset);
		const FVector HandL = Body->GetBoneLocation(TEXT("hand_l"));
		const FVector Upper = Body->GetBoneLocation(TEXT("upperarm_l"));
		const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 Lower = Ref.FindBoneIndex(TEXT("lowerarm_l"));
		const int32 Hand = Ref.FindBoneIndex(TEXT("hand_l"));
		const float TwoBoneReach = Lower != INDEX_NONE && Hand != INDEX_NONE
			? static_cast<float>(Ref.GetRefBonePose()[Lower].GetLocation().Size() + Ref.GetRefBonePose()[Hand].GetLocation().Size()) : 57.f;
		const float Extension = static_cast<float>(FVector::Dist(Upper, HandL)) / FMath::Max(TwoBoneReach, 1.f);
		const float Gap = static_cast<float>(FVector::Dist(HandL, Target));
		float WallDistance = -1.f;
		if (Op->bInCover)
		{
			const FCoverSlot& Slot = Op->GetCoverSlot();
			WallDistance = static_cast<float>(FVector::DotProduct(HandL - Slot.WallPoint, Slot.WallNormal.GetSafeNormal2D()));
		}
		++State.Samples;
		UE_LOG(LogCodexTactics, Display, TEXT("IK pose %-22s clip %-44s alpha %.2f slide %4.1f excess %4.1f | final hand_l -> target %5.1f cm | arm %3.0f %% | hand %5.1f cm off the wall"),
			Pose, *Clip, Anim->LeftHandIKAlpha, Anim->LeftHandIKSlideCm, Anim->LeftHandIKExcessCm, Gap, Extension * 100.f, WallDistance);
		if (Anim->LeftHandIKAlpha >= 0.99f)
		{
			State.MaxGapOn = FMath::Max(State.MaxGapOn, Gap);
			State.MaxExtensionOn = FMath::Max(State.MaxExtensionOn, Extension);
			Check(State, Gap <= 3.f, FString::Printf(TEXT("%s: the left hand on the IK target (%.1f cm)"), Pose, Gap));
		}
		Check(State, Extension <= 0.96f, FString::Printf(TEXT("%s: the left arm is not stretched straight (%.0f %%)"), Pose, Extension * 100.f));
		if (WallDistance >= 0.f && WallDistance < 8.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - the clip's left hand is on the wall (%.1f cm): a LeftHandIKFreeClips candidate (%s)"), Pose, WallDistance, *Clip);
		}
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

	bool OrderCover(FState& State, UWorld* World, float Along)
	{
		FCoverSlot Slot;
		AOperativeCharacter* Op = State.Op.Get();
		if (Op->bInCover)
		{
			Op->LeaveCover(TEXT("LeftHandIKSmoke"));
		}
		return CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + State.R * Along + FVector(0.f, 0.f, 90.f), State.F, Slot)
			&& Op->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted;
	}

	bool Shimmy(FState& State, UWorld* World, float Along)
	{
		FCoverSlot Target;
		AOperativeCharacter* Op = State.Op.Get();
		return CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), State.P + State.F * 380.f + State.R * Along + FVector(0.f, 0.f, 90.f), Target)
			&& Op->OrderShimmyTo(Target) == EOperativeOrderResult::Accepted;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.05f;
		State.StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 160.f)
		{
			return World ? Finish(State) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
		{
			Sight->Refresh();
		}
		auto Next = [&State]()
		{
			++State.Stage;
			State.StageTime = 0.f;
			State.bSampled = false;
			return true;
		};
		auto SampleAt = [&State](float At, const TCHAR* Pose)
		{
			if (!State.bSampled && State.StageTime >= At)
			{
				State.bSampled = true;
				Sample(State, Pose);
			}
		};
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Op = Squad->GetLeader();
			State.Op = Op;
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) - State.F * 6000.f, State.F.Rotation());
			if (Hound)
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			}
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
			const FVector WallCentre = State.P + State.F * 400.f;
			SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			Op->bIgnoreCoverThreatForTesting = true;
			return Next();
		}
		case 1:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, OrderCover(State, World, 0.f), TEXT("cover at the wall's middle ordered (walk in)"));
			return Next();
		case 2:
			// The enter clip while it plays, then the idle.
			if (!Op->bInCover && State.StageTime < 10.f)
			{
				return true;
			}
			SampleAt(0.f, TEXT("std cover enter"));
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			return Next();
		case 3:
			SampleAt(2.5f, TEXT("std cover idle"));
			if (State.StageTime < 2.6f)
			{
				return true;
			}
			Check(State, Shimmy(State, World, -120.f), TEXT("shimmy to his right (no threat: forward)"));
			return Next();
		case 4:
			SampleAt(0.7f, TEXT("std shimmy fwd"));
			if (Op->bShimmying || State.StageTime < 1.f)
			{
				return State.StageTime < 8.f ? true : Next();
			}
			// A threat in front to his right: a shimmy to his left walks backwards.
			Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(State, State.P - State.F * 400.f - State.R * 900.f);
			return Next();
		case 5:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Shimmy(State, World, 60.f), TEXT("shimmy to his left, threat on his right (backwards)"));
			return Next();
		case 6:
			SampleAt(0.7f, TEXT("std shimmy bwd"));
			if (Op->bShimmying || State.StageTime < 1.f)
			{
				return State.StageTime < 8.f ? true : Next();
			}
			Op->bIgnoreCoverThreatForTesting = true;
			PlaceHound(State, State.P - State.F * 6000.f);
			Check(State, OrderCover(State, World, -250.f), TEXT("cover at his right corner (no threat: look-around)"));
			return Next();
		case 7:
			if (!Op->bInCover && State.StageTime < 10.f)
			{
				return true;
			}
			SampleAt(3.f, TEXT("std corner look-around"));
			if (State.StageTime < 3.1f)
			{
				return true;
			}
			Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(State, State.P + State.F * 700.f - State.R * 800.f); // behind the wall, round the corner
			return Next();
		case 8:
			SampleAt(3.5f, TEXT("std fire stance"));
			if (State.StageTime < 3.6f)
			{
				return true;
			}
			Op->bIgnoreCoverThreatForTesting = true;
			PlaceHound(State, State.P - State.F * 6000.f);
			return Next();
		case 9:
			if (State.StageTime < 3.f)
			{
				return true; // back to the plain corner pose
			}
			Op->SetStance(EOperativeStance::Crouching);
			return Next();
		case 10:
			SampleAt(0.5f, TEXT("stand -> crouch switch"));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Sample(State, TEXT("crch corner look-around"));
			Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(State, State.P + State.F * 700.f - State.R * 800.f);
			return Next();
		case 11:
			SampleAt(3.5f, TEXT("crch fire stance"));
			if (State.StageTime < 3.6f)
			{
				return true;
			}
			Op->bIgnoreCoverThreatForTesting = true;
			PlaceHound(State, State.P - State.F * 6000.f);
			return Next();
		case 12:
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Shimmy(State, World, -50.f), TEXT("crouch-shimmy to his left"));
			return Next();
		case 13:
			SampleAt(0.7f, TEXT("crch shimmy fwd"));
			if (Op->bShimmying || State.StageTime < 1.f)
			{
				return State.StageTime < 8.f ? true : Next();
			}
			return Next();
		case 14:
			SampleAt(2.5f, TEXT("crch cover idle"));
			if (State.StageTime < 2.6f)
			{
				return true;
			}
			Op->SetStance(EOperativeStance::Standing);
			return Next();
		case 15:
			SampleAt(0.5f, TEXT("crouch -> stand switch"));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Op->bIgnoreCoverThreatForTesting = false;
			return Finish(State);
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LeftHandIKSmoke"),
		TEXT("Dev check: the left-hand IK on every cover pose (needs the ABP's Two Bone IK node); PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
