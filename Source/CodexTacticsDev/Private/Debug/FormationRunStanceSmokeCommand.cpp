// Task 150: formation run (double-click / sprint) order stands crouched and prone followers up and sprints them too;
// returning to walk (single-click) or crouch (C key) drops sprint and follows leader stance rules; Solo stays untouched.
//   Scripts/smoke_parallel.ps1 -Commands FormationRunStanceSmoke

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "Formation/FormationTuning.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace FormationRunStanceSmoke
{
	enum class EStep : uint8
	{
		Warmup,
		WaitCrouchWalk,
		SprintWithin1s,
		SingleClickWalk,
		LeaderCrouchReturn,
		ProneFollowerSprint,
		SoloUnchanged,
		Done
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		SmokeUtils::FPreloadGate Gate;
		bool bPmcStopped = false;
		int32 Failures = 0;
		double StepSince = 0.0;
		FVector SprintDest = FVector::ZeroVector;
		FVector StartLoc = FVector::ZeroVector;
		FDelegateHandle EndFrameHandle;
		TWeakObjectPtr<UWorld> World;
		TMap<TWeakObjectPtr<AOperativeCharacter>, double> StandTimes;
		TMap<TWeakObjectPtr<AOperativeCharacter>, double> RunSpeedTimes;
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
		FPlatformMisc::RequestExit(false, TEXT("FormationRunStanceSmoke"));
	}

	FVector FindReachablePoint(UWorld& World, const FVector& Origin, float PreferredDistCm)
	{
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!Nav)
		{
			return Origin + FVector(PreferredDistCm, 0.f, 0.f);
		}
		for (int32 DirIdx = 0; DirIdx < 16; ++DirIdx)
		{
			const float Yaw = DirIdx * 22.5f;
			const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
			for (float Dist = PreferredDistCm; Dist >= 1200.f; Dist -= 300.f)
			{
				const FVector Query = Origin + Dir * Dist;
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Query, OnNav, FVector(200.f, 200.f, 300.f)))
				{
					continue;
				}
				if (FMath::Abs(static_cast<float>(OnNav.Location.Z - Origin.Z)) > 100.f)
				{
					continue;
				}
				const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(&World, Origin, OnNav.Location);
				if (Path && Path->IsValid() && !Path->IsPartial() && FVector::Dist2D(Origin, OnNav.Location) >= 1200.f)
				{
					return OnNav.Location;
				}
			}
		}
		return SmokeUtils::ClearPoint(&World, Origin, Origin + FVector(PreferredDistCm, 0.f, 0.f));
	}

	TArray<AOperativeCharacter*> GetFollowers(USquadSubsystem& Squad, const AOperativeCharacter* Leader)
	{
		TArray<AOperativeCharacter*> Out;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != Leader && !Member->bGuarding && !Member->IsKilledInAction())
			{
				Out.Add(Member);
			}
		}
		return Out;
	}

	void OnFrame()
	{
		FRunner& R = *Runner;
		UWorld* World = R.World.Get();
		if (!World || R.Step == EStep::Done)
		{
			return;
		}
		const SmokeUtils::EPreloadGate PreloadState = R.Gate.Poll(World);
		if (PreloadState == SmokeUtils::EPreloadGate::Waiting)
		{
			R.StepSince = World->GetTimeSeconds();
			return;
		}
		if (PreloadState == SmokeUtils::EPreloadGate::Failed)
		{
			Check(false, FString::Printf(TEXT("world preload failed: %s"), *R.Gate.FailReason));
			Finish();
			return;
		}
		if (!R.bPmcStopped)
		{
			SmokeUtils::StopLevelPmc(World);
			R.bPmcStopped = true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			Check(false, TEXT("missing squad, controller, or leader"));
			Finish();
			return;
		}
		const double Now = World->GetTimeSeconds();
		const double T = Now - R.StepSince;
		const TArray<AOperativeCharacter*> Followers = GetFollowers(*Squad, Leader);
		const float RunGate = FMath::Max(320.f, FormationTuning::Get().PaceRunSpeed * 0.85f);

		switch (R.Step)
		{
		case EStep::Warmup:
		{
			if (T < 1.5)
			{
				return;
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false);
			}
			Check(!Squad->IsSoloMode() && Followers.Num() >= 1,
				FString::Printf(TEXT("formation active with %d follower(s)"), Followers.Num()));
			if (Followers.Num() < 1)
			{
				Finish();
				return;
			}
			R.StartLoc = Leader->GetActorLocation();
			R.SprintDest = FindReachablePoint(*World, R.StartLoc, 2200.f);
			const FVector ToSprint = (R.SprintDest - R.StartLoc).GetSafeNormal2D();

			PC->SetEntireSquadStance(EOperativeStance::Crouching);
			bool bAllCrouched = Leader->GetStance() == EOperativeStance::Crouching;
			for (AOperativeCharacter* F : Followers)
			{
				bAllCrouched &= (F->GetStance() == EOperativeStance::Crouching);
			}
			Check(bAllCrouched, TEXT("Alt+C puts leader and all followers into CROUCHED"));
			Leader->OrderMoveTo(R.StartLoc + ToSprint * 600.f, false);
			R.Step = EStep::WaitCrouchWalk;
			R.StepSince = Now;
			return;
		}
		case EStep::WaitCrouchWalk:
		{
			if (T < 0.8)
			{
				return;
			}
			R.StandTimes.Reset();
			R.RunSpeedTimes.Reset();
			Check(Leader->OrderMoveTo(R.SprintDest, true) == EOperativeOrderResult::Accepted,
				TEXT("double-click sprint order 20 m ahead accepted"));
			R.Step = EStep::SprintWithin1s;
			R.StepSince = Now;
			return;
		}
		case EStep::SprintWithin1s:
		{
			bool bAllStanding = true;
			bool bAllAtRunSpeed = true;
			for (AOperativeCharacter* F : Followers)
			{
				const float Speed2D = static_cast<float>(F->GetVelocity().Size2D());
				if (F->GetStance() == EOperativeStance::Standing && !R.StandTimes.Contains(F))
				{
					R.StandTimes.Add(F, T);
				}
				if (Speed2D >= RunGate && !R.RunSpeedTimes.Contains(F))
				{
					R.RunSpeedTimes.Add(F, T);
				}
				bAllStanding &= (F->GetStance() == EOperativeStance::Standing);
				bAllAtRunSpeed &= (Speed2D >= RunGate && F->IsSprinting());
			}
			if (!(bAllStanding && bAllAtRunSpeed) && T < 1.0)
			{
				return;
			}
			for (AOperativeCharacter* F : Followers)
			{
				const float Speed2D = static_cast<float>(F->GetVelocity().Size2D());
				const double* StandT = R.StandTimes.Find(F);
				const double* RunT = R.RunSpeedTimes.Find(F);
				Check(F->GetStance() == EOperativeStance::Standing && F->IsSprinting() && Speed2D >= RunGate,
					FString::Printf(TEXT("follower %s STANDING & run within 1 s (stance %d, sprinting %d, speed %.0f >= %.0f, stand@%.2fs, run@%.2fs)"),
						*F->DisplayName.ToString(), static_cast<int32>(F->GetStance()), F->IsSprinting() ? 1 : 0,
						Speed2D, RunGate, StandT ? *StandT : -1.0, RunT ? *RunT : -1.0));
			}
			Check(Leader->OrderMoveTo(FindReachablePoint(*World, Leader->GetActorLocation(), 1800.f), false) == EOperativeOrderResult::Accepted,
				TEXT("single-click walk order accepted after run"));
			R.Step = EStep::SingleClickWalk;
			R.StepSince = Now;
			return;
		}
		case EStep::SingleClickWalk:
		{
			if (T < 1.2)
			{
				return;
			}
			for (AOperativeCharacter* F : Followers)
			{
				const float Speed2D = static_cast<float>(F->GetVelocity().Size2D());
				const float DistToLeader = static_cast<float>(FVector::Dist2D(F->GetActorLocation(), Leader->GetActorLocation()));
				Check(F->GetStance() == EOperativeStance::Standing && !F->IsSprinting() && Speed2D > 80.f && Speed2D < RunGate && DistToLeader < 600.f,
					FString::Printf(TEXT("single click -> follower %s walks (stance %d, sprinting %d, speed %.0f, distLeader %.0f)"),
						*F->DisplayName.ToString(), static_cast<int32>(F->GetStance()), F->IsSprinting() ? 1 : 0, Speed2D, DistToLeader));
			}
			PC->StanceCrouch(); // C key -> Leader crouches -> stood-up followers follow leader back to crouch
			R.Step = EStep::LeaderCrouchReturn;
			R.StepSince = Now;
			return;
		}
		case EStep::LeaderCrouchReturn:
		{
			if (T < 0.3)
			{
				return;
			}
			Check(Leader->GetStance() == EOperativeStance::Crouching, TEXT("C key crouched leader"));
			for (AOperativeCharacter* F : Followers)
			{
				Check(F->GetStance() == EOperativeStance::Crouching && !F->IsSprinting(),
					FString::Printf(TEXT("C key -> follower %s returned to CROUCHED (stance %d)"),
						*F->DisplayName.ToString(), static_cast<int32>(F->GetStance())));
			}
			// Arm prone follower + test sprint stands prone follower up too
			Leader->StopOperative();
			Followers[0]->StopOperative();
			Followers[0]->SetStance(EOperativeStance::Prone);
			Check(Followers[0]->GetStance() == EOperativeStance::Prone, TEXT("follower 0 set to PRONE"));
			Check(Leader->OrderMoveTo(FindReachablePoint(*World, Leader->GetActorLocation(), 2000.f), true) == EOperativeOrderResult::Accepted,
				TEXT("double-click sprint order with prone follower accepted"));
			R.Step = EStep::ProneFollowerSprint;
			R.StepSince = Now;
			return;
		}
		case EStep::ProneFollowerSprint:
		{
			const float Speed2D = static_cast<float>(Followers[0]->GetVelocity().Size2D());
			const bool bUpAndRunning = Followers[0]->GetStance() == EOperativeStance::Standing && Followers[0]->IsSprinting()
				&& Speed2D >= RunGate;
			if (!bUpAndRunning && T < 3.0)
			{
				return;
			}
			Check(bUpAndRunning,
				FString::Printf(TEXT("prone follower stood up and running (stance %d, sprint %d, speed %.0f >= %.0f, %.2fs)"),
					static_cast<int32>(Followers[0]->GetStance()), Followers[0]->IsSprinting() ? 1 : 0, Speed2D, RunGate, T));

			// Verify Solo remains untouched while Leader actually sprints
			Squad->EnterSoloMode();
			PC->SetEntireSquadStance(EOperativeStance::Crouching);
			Check(Leader->OrderMoveTo(FindReachablePoint(*World, Leader->GetActorLocation(), 1600.f), true) == EOperativeOrderResult::Accepted,
				TEXT("Solo mode leader double-click sprint accepted"));
			R.Step = EStep::SoloUnchanged;
			R.StepSince = Now;
			return;
		}
		case EStep::SoloUnchanged:
		{
			if (T < 0.8)
			{
				return;
			}
			const float LeaderSpeed2D = static_cast<float>(Leader->GetVelocity().Size2D());
			Check(Leader->GetStance() == EOperativeStance::Standing && Leader->IsSprinting() && LeaderSpeed2D >= RunGate,
				FString::Printf(TEXT("Solo mode: leader itself stands and runs (stance %d, sprint %d, speed %.0f >= %.0f)"),
					static_cast<int32>(Leader->GetStance()), Leader->IsSprinting() ? 1 : 0, LeaderSpeed2D, RunGate));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Leader && !Member->bGuarding)
				{
					Check(Member->GetStance() == EOperativeStance::Crouching && !Member->IsSprinting(),
						FString::Printf(TEXT("Solo untouched: %s stayed CROUCHED (stance %d, sprint %d)"),
							*Member->DisplayName.ToString(), static_cast<int32>(Member->GetStance()), Member->IsSprinting() ? 1 : 0));
				}
			}
			Squad->ExitSoloMode(false);
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
		Runner = MakeShared<FRunner>();
		Runner->World = World;
		Runner->StepSince = World->GetTimeSeconds();
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnFrame);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationRunStanceSmoke"),
		TEXT("Task 150: crouched/prone followers stand up and sprint on formation double-click within 1s, walk on single-click, crouch on C; Solo untouched."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
