// G025 torso drift. Sequence: cover, climb, look left, look right, neutral look, walk 10 m.
// Gate: after 0.5 s of that walk, mesh forward versus velocity yaw stays within 10 deg.
//   Scripts/smoke_parallel.ps1 -Commands TorsoDriftSmoke -Remote off -ExtraArgs "-Multiprocess"

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/MantleRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/MantleNavigation.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"

namespace TorsoDriftSmoke
{
	constexpr float MaxYawErrorDeg = 10.f;
	constexpr double SettleSeconds = 0.5;

	enum class EStep : uint8
	{
		Warmup, Cover, Leave, Climb, LookLeft, LookRight, Neutral, Walk, Measure, Done
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Failures = 0;
		double StepSince = 0.0;
		bool bActed = false;
		bool bClimbed = false;
		bool bSawMantle = false;
		int32 MeasureFrames = 0;
		float WorstError = 0.f;
		bool bLoggedDrift = false;
		float RestMeshYaw = 0.f;
		FVector WalkFrom = FVector::ZeroVector;
		/** A direction from the climb's landing that stays on navmesh for 10 m. */
		FVector WalkDir = FVector(1.f, 0.f, 0.f);
		FDelegateHandle EndFrameHandle;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
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
		FPlatformMisc::RequestExit(false, TEXT("TorsoDriftSmoke"));
	}

	void Go(EStep Step, double Now)
	{
		Runner->Step = Step;
		Runner->StepSince = Now;
		Runner->bActed = false;
	}

	void Arm(AOperativeCharacter& Operative)
	{
		if (USkeletalMeshComponent* Mesh = Operative.GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bPauseAnims = false;
		}
	}

	bool FindCover(UWorld& World, AOperativeCharacter& Operative, FCoverSlot& OutSlot)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorsoDriftSmoke), false, &Operative);
		const FVector Origin = Operative.GetActorLocation();
		for (int32 Ray = 0; Ray < 16; ++Ray)
		{
			const FVector Dir = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
			FHitResult Hit;
			if (!World.LineTraceSingleByChannel(Hit, Origin, Origin + Dir * 2500.f, ECC_Visibility, Params) || !Hit.bBlockingHit)
			{
				continue;
			}
			if (CoverTraceRules::FindCoverSlotAt(&World, Hit.ImpactPoint, -Hit.ImpactNormal, OutSlot) && OutSlot.IsValid()
				&& OutSlot.Height == ECoverHeight::HighCover)
			{
				return true;
			}
		}
		return false;
	}

	/** The landing has a 10 m walk on the same floor. A crate top has nowhere to go, so the velocity yaws while he shuffles the edge. */
	bool FindWalkDir(UWorld& World, const FVector& Landing, FVector& OutDir)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!Nav)
		{
			return false;
		}
		FNavLocation OnLanding;
		if (!Nav->ProjectPointToNavigation(Landing + FVector(0.f, 0.f, 40.f), OnLanding, FVector(100.f, 100.f, 250.f)))
		{
			return false;
		}
		const FVector Dirs[] = {
			FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f),
			FVector(0.707f, 0.707f, 0.f), FVector(-0.707f, 0.707f, 0.f), FVector(0.707f, -0.707f, 0.f), FVector(-0.707f, -0.707f, 0.f)
		};
		for (const FVector& Dir : Dirs)
		{
			// The whole 10 m segment has to lie on the navmesh. A far point that projects around a corner still yaws the velocity.
			bool bCorridor = true;
			FVector Far = OnLanding.Location;
			for (float Dist = 200.f; Dist <= 1000.f; Dist += 200.f)
			{
				const FVector Sample = OnLanding.Location + Dir * Dist;
				FNavLocation On;
				if (!Nav->ProjectPointToNavigation(Sample + FVector(0.f, 0.f, 40.f), On, FVector(50.f, 50.f, 200.f))
					|| FMath::Abs(On.Location.Z - OnLanding.Location.Z) > 40.f
					|| FVector::Dist2D(On.Location, Sample) > 45.f)
				{
					bCorridor = false;
					break;
				}
				Far = On.Location;
			}
			if (!bCorridor)
			{
				continue;
			}
			OutDir = (Far - OnLanding.Location).GetSafeNormal2D();
			if (!OutDir.IsNearlyZero())
			{
				return true;
			}
		}
		return false;
	}

	bool StartClimb(AOperativeCharacter& Operative)
	{
		UWorld* World = Operative.GetWorld();
		UMantleSubsystem* Mantle = World ? World->GetSubsystem<UMantleSubsystem>() : nullptr;
		if (!Mantle || Mantle->GetLinks().IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: no mantle links"));
			return false;
		}
		const TArray<FMantleLink>& Links = Mantle->GetLinks();
		TArray<int32> Order;
		Order.Reserve(Links.Num());
		for (int32 Index = 0; Index < Links.Num(); ++Index)
		{
			const FMantleLink& Link = Links[Index];
			if (!Link.bDown && (Link.ClassId == TEXT("low_mantle") || Link.ClassId == TEXT("high_mantle")))
			{
				Order.Add(Index);
			}
		}
		const FVector Here = Operative.GetActorLocation();
		Order.Sort([&Links, &Here](int32 A, int32 B)
		{
			const bool bLowA = Links[A].ClassId == TEXT("low_mantle");
			const bool bLowB = Links[B].ClassId == TEXT("low_mantle");
			if (bLowA != bLowB)
			{
				return bLowA;
			}
			return FVector::DistSquared2D(Here, Links[A].Start) < FVector::DistSquared2D(Here, Links[B].Start);
		});
		int32 Tried = 0;
		int32 Checked = 0;
		for (const int32 Index : Order)
		{
			if (Tried >= 8)
			{
				break;
			}
			++Checked;
			const FMantleLink& Link = Links[Index];
			FVector Dir = FVector::ZeroVector;
			if (!FindWalkDir(*World, Link.End, Dir))
			{
				continue;
			}
			Operative.StopOperative();
			// Link.Start is the nav point (feet). The actor origin is the capsule centre.
			const UCapsuleComponent* Capsule = Operative.GetCapsuleComponent();
			const float Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
			const FVector Place = Link.Start + FVector(0.f, 0.f, Half);
			if (!Operative.TeleportTo(Place, Link.Dir.Rotation(), false, true))
			{
				continue;
			}
			++Tried;
			EMantleRefusal Refusal = EMantleRefusal::None;
			if (Operative.TryStartMantle(Link.Dir, &Refusal, true))
			{
				if (Runner.IsValid())
				{
					Runner->WalkDir = Dir;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: climb %s at %s, %.0f cm, walk %s"), *Link.ClassId.ToString(), *Link.Start.ToCompactString(), Link.HeightCm, *Dir.ToCompactString());
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: link %s refused (%s)"), *Link.ClassId.ToString(), MantleRules::RefusalName(Refusal));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: no low/high mantle with a 10 m walk (%d checked)"), Checked);
		return false;
	}

	float MeshVelocityErrorDeg(const FRunner& R, const AOperativeCharacter& Operative)
	{
		const USkeletalMeshComponent* Mesh = Operative.GetMesh();
		if (!Mesh || Operative.GetVelocity().SizeSquared2D() < 50.f * 50.f)
		{
			return -1.f;
		}
		// The bind yaw (usually -90) is the mesh's place on the capsule, not a turn. Drift is any extra relative yaw
		// plus the actor not facing the walk. Formation scan is off for this check: that look is meant to leave the velocity.
		const float Extra = FMath::FindDeltaAngleDegrees(R.RestMeshYaw, Mesh->GetRelativeRotation().Yaw);
		const float BodyYaw = FRotator::NormalizeAxis(Operative.GetActorRotation().Yaw + Extra);
		return FMath::Abs(FMath::FindDeltaAngleDegrees(BodyYaw, Operative.GetVelocity().Rotation().Yaw));
	}

	void OnFrame()
	{
		FRunner& R = *Runner;
		UWorld* World = R.World.Get();
		AOperativeCharacter* Op = R.Leader.Get();
		if (!World || !Op || R.Step == EStep::Done)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		const double T = Now - R.StepSince;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (T < 2.0)
			{
				return;
			}
			Arm(*Op);
			if (USkeletalMeshComponent* Mesh = Op->GetMesh())
			{
				R.RestMeshYaw = Mesh->GetRelativeRotation().Yaw;
			}
			Go(EStep::Cover, Now);
			return;
		case EStep::Cover:
		{
			if (!R.bActed)
			{
				R.bActed = true;
				FCoverSlot Slot;
				const bool bFound = FindCover(*World, *Op, Slot);
				Check(bFound, TEXT("a high cover wall is in reach"));
				if (!bFound)
				{
					Finish();
					return;
				}
				Check(Op->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted, TEXT("take cover"));
			}
			if (Op->bInCover)
			{
				Go(EStep::Leave, Now);
			}
			else if (T > 20.0)
			{
				Check(false, TEXT("never reached the cover"));
				Finish();
			}
			return;
		}
		case EStep::Leave:
			if (!R.bActed)
			{
				R.bActed = true;
				const FVector Away = Op->GetActorLocation() + Op->GetCoverSlot().WallNormal.GetSafeNormal() * 400.f;
				Check(Op->OrderMoveTo(Away, false) == EOperativeOrderResult::Accepted, TEXT("walk off the cover"));
			}
			if (!Op->bInCover && T > 1.0)
			{
				Go(EStep::Climb, Now);
			}
			else if (T > 15.0)
			{
				Check(false, TEXT("still in cover"));
				Finish();
			}
			return;
		case EStep::Climb:
			if (!R.bActed)
			{
				R.bActed = true;
				R.bClimbed = StartClimb(*Op);
				Check(R.bClimbed, TEXT("a climb starts"));
				if (!R.bClimbed)
				{
					Finish();
					return;
				}
			}
			if (Op->IsMantling())
			{
				R.bSawMantle = true;
				return;
			}
			if (R.bSawMantle)
			{
				Check(true, TEXT("the climb ended"));
				Go(EStep::LookLeft, Now);
			}
			else if (T > 8.0)
			{
				Check(false, TEXT("the climb never played"));
				Finish();
			}
			return;
		case EStep::LookLeft:
		case EStep::LookRight:
			if (!R.bActed)
			{
				R.bActed = true;
				const float Side = R.Step == EStep::LookLeft ? -1.f : 1.f;
				const FVector Point = Op->GetActorLocation() + Op->GetActorForwardVector() * 200.f + Op->GetActorRightVector() * (Side * 500.f);
				Op->SetManualLookPoint(Point);
				Check(Op->HasManualLookPoint(), R.Step == EStep::LookLeft ? TEXT("look left") : TEXT("look right"));
			}
			if (T > 0.6)
			{
				Go(R.Step == EStep::LookLeft ? EStep::LookRight : EStep::Neutral, Now);
			}
			return;
		case EStep::Neutral:
			if (!R.bActed)
			{
				R.bActed = true;
				Op->ClearManualLookPoint();
				Check(!Op->HasManualLookPoint(), TEXT("neutral look clears the point"));
			}
			if (T > 0.3)
			{
				Go(EStep::Walk, Now);
			}
			return;
		case EStep::Walk:
			if (!R.bActed)
			{
				R.bActed = true;
				R.WalkFrom = Op->GetActorLocation();
				const FVector Dest = R.WalkFrom + R.WalkDir * 1000.f;
				Check(Op->OrderMoveTo(Dest, false) == EOperativeOrderResult::Accepted, TEXT("walk 10 m"));
			}
			if (Op->GetVelocity().SizeSquared2D() > 50.f * 50.f)
			{
				Go(EStep::Measure, Now);
			}
			else if (T > 8.0)
			{
				Check(false, TEXT("the walk never got up to speed"));
				Finish();
			}
			return;
		case EStep::Measure:
		{
			const float Error = MeshVelocityErrorDeg(R, *Op);
			if (Error < 0.f)
			{
				if (T > 3.0)
				{
					Check(false, TEXT("lost the walk during the measure"));
					Finish();
				}
				return;
			}
			if (T < SettleSeconds)
			{
				return;
			}
			if (Error > R.WorstError)
			{
				R.WorstError = Error;
				if (Error > MaxYawErrorDeg && !R.bLoggedDrift)
				{
					R.bLoggedDrift = true;
					const USkeletalMeshComponent* Mesh = Op->GetMesh();
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: drift %.1f deg actor %.1f mesh %.1f rest %.1f vel %.1f speed %.0f sector %d look %d combat %d"),
						Error, Op->GetActorRotation().Yaw, Mesh ? Mesh->GetRelativeRotation().Yaw : 0.f, R.RestMeshYaw,
						Op->GetVelocity().Rotation().Yaw, Op->GetVelocity().Size2D(), Op->IsFacingFormationSector() ? 1 : 0,
						Op->HasManualLookPoint() ? 1 : 0, Op->IsFacingCombatTarget() ? 1 : 0);
				}
			}
			++R.MeasureFrames;
			const float Walked = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), R.WalkFrom));
			// A rifle walk is ~176 cm/s, so 2 m takes longer than the 0.5 s yaw settle. The gate stays the worst error after that settle.
			if (R.MeasureFrames < 30 || (Walked <= 200.f && T < 8.0))
			{
				return;
			}
			Check(R.WorstError <= MaxYawErrorDeg, FString::Printf(TEXT("mesh versus velocity %.1f deg after 0.5 s (gate %.0f)"), R.WorstError, MaxYawErrorDeg));
			Check(Walked > 200.f, FString::Printf(TEXT("he actually walked (%.0f cm)"), Walked));
			Finish();
			return;
		}
		default:
			return;
		}
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World || Runner.IsValid())
		{
			return;
		}
		if (IConsoleVariable* Sectors = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors")))
		{
			Sectors->Set(0, ECVF_SetByCode);
		}
		Runner = MakeShared<FRunner>();
		Runner->World = World;
		Runner->StepSince = World->GetTimeSeconds();
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Runner->Leader = Squad->GetLeader();
		}
		if (!Runner->Leader.IsValid())
		{
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				Runner->Leader = *It;
				break;
			}
		}
		if (!Runner->Leader.IsValid())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: no operative"));
			FPlatformMisc::RequestExit(false, TEXT("TorsoDriftSmoke"));
			return;
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnFrame);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TorsoDriftSmoke"),
		TEXT("G025: cover, climb, look left, look right, neutral, walk 10 m. Mesh forward versus velocity <= 10 deg after 0.5 s. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
