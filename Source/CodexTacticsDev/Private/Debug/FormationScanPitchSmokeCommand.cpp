// G025 formation scan pitch. A formation walk on open ground must swing aim pitch by at least 20 deg,
// and a walk toward an X6 building must record aim pitch above +10 deg (upper windows / roofs).
//   Scripts/smoke_parallel.ps1 -Commands FormationScanPitchSmoke -Remote off -ExtraArgs "-Multiprocess"

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"

namespace FormationScanPitchSmoke
{
	constexpr double SampleSeconds = 4.0;

	enum class EStep : uint8 { Warmup, Open, Building, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Failures = 0;
		double StepSince = 0.0;
		bool bActed = false;
		float OpenMin = 90.f;
		float OpenMax = -90.f;
		int32 OpenSamples = 0;
		float BuildingMax = -90.f;
		int32 BuildingSamples = 0;
		FDelegateHandle EndFrameHandle;
		TWeakObjectPtr<UWorld> World;
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
		FPlatformMisc::RequestExit(false, TEXT("FormationScanPitchSmoke"));
	}

	void Arm(AOperativeCharacter& Operative)
	{
		if (USkeletalMeshComponent* Mesh = Operative.GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bPauseAnims = false;
		}
	}

	void Sample(FRunner& R, bool bOpen)
	{
		UWorld* World = R.World.Get();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad)
		{
			return;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member || !Member->IsFacingFormationSector())
			{
				continue;
			}
			const UOperativeAnimInstance* Anim = Member->GetMesh() ? Cast<UOperativeAnimInstance>(Member->GetMesh()->GetAnimInstance()) : nullptr;
			if (!Anim)
			{
				continue;
			}
			const float Pitch = Anim->AimPitch;
			if (bOpen)
			{
				R.OpenMin = FMath::Min(R.OpenMin, Pitch);
				R.OpenMax = FMath::Max(R.OpenMax, Pitch);
				++R.OpenSamples;
			}
			else
			{
				R.BuildingMax = FMath::Max(R.BuildingMax, Pitch);
				++R.BuildingSamples;
			}
		}
	}

	FVector BuildingAim(UWorld& World, const AOperativeCharacter& Leader)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationScanPitchSmoke), false, &Leader);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		const FVector Origin = Leader.GetActorLocation();
		FVector Best = Origin + Leader.GetActorForwardVector() * 1500.f;
		float BestZ = -1.0e9f;
		for (int32 Ray = 0; Ray < 16; ++Ray)
		{
			const FVector Dir = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
			FHitResult Hit;
			if (World.LineTraceSingleByObjectType(Hit, Origin + FVector(0.f, 0.f, 160.f), Origin + Dir * 4000.f + FVector(0.f, 0.f, 900.f), Objects, Params)
				&& Hit.ImpactPoint.Z - Origin.Z > BestZ)
			{
				BestZ = static_cast<float>(Hit.ImpactPoint.Z - Origin.Z);
				Best = Origin + Dir * FMath::Max(Hit.Distance - 400.f, 300.f);
			}
		}
		return Best;
	}

	void OnFrame()
	{
		FRunner& R = *Runner;
		UWorld* World = R.World.Get();
		if (!World || R.Step == EStep::Done)
		{
			return;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			Check(false, TEXT("no formation leader"));
			Finish();
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
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Arm(*Member);
				}
			}
			R.bActed = true;
			Check(Leader->OrderMoveTo(Leader->GetActorLocation() + FVector(1800.f, 0.f, 0.f), false) == EOperativeOrderResult::Accepted,
				TEXT("open-ground formation walk"));
			R.Step = EStep::Open;
			R.StepSince = Now;
			return;
		case EStep::Open:
			Sample(R, true);
			if (T < SampleSeconds)
			{
				return;
			}
			Check(R.OpenSamples > 0, FString::Printf(TEXT("open ground sampled the scan (%d)"), R.OpenSamples));
			Check(R.OpenMax - R.OpenMin >= 20.f, FString::Printf(TEXT("open ground aim pitch span %.1f deg (gate 20)"), R.OpenMax - R.OpenMin));
			R.Step = EStep::Building;
			R.StepSince = Now;
			R.bActed = false;
			return;
		case EStep::Building:
			if (!R.bActed)
			{
				R.bActed = true;
				const FVector Dest = BuildingAim(*World, *Leader);
				Check(Leader->OrderMoveTo(Dest, false) == EOperativeOrderResult::Accepted, TEXT("walk toward the building"));
			}
			Sample(R, false);
			if (T < SampleSeconds)
			{
				return;
			}
			Check(R.BuildingSamples > 0, FString::Printf(TEXT("building walk sampled the scan (%d)"), R.BuildingSamples));
			Check(R.BuildingMax > 10.f, FString::Printf(TEXT("aim pitch toward the building %.1f deg (gate +10)"), R.BuildingMax));
			Finish();
			return;
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
		Runner = MakeShared<FRunner>();
		Runner->World = World;
		Runner->StepSince = World->GetTimeSeconds();
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnFrame);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationScanPitchSmoke"),
		TEXT("G025: formation walk, open-ground aim pitch span >= 20 deg, and > +10 deg toward an X6 building. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
