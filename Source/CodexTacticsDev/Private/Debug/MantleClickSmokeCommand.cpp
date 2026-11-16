// Dev-only console command: ordering a climb by the mouse and its hover preview on L_Gen_Depot_1 (designer bug report 2026-11-16, batch 2:
// item 3 "clicking (even double-clicking) on an obstacle does not make the operative run to it and climb", MantlingProblem_03 / 04; item 4 the
// hover ghost, "the same way the game shows a ghost when hovering a building corner for peeking"; docs/port/MANTLING_BATCH2_REPORT.md):
//   Scripts/smoke_parallel.ps1 -Commands MantleClickSmoke -ExtraArgs -Multiprocess
// The clicks take the REAL input path: the cursor's ray from the player's camera (ECC_Visibility, as GetHitResultUnderCursor) through a point of
// the obstacle, the hit handed to ACodexTacticsPlayerController::HandleWorldHit (the left click's dispatcher). Cases (one Solo operative, 4 m off):
//   a  click on the 140 cm wreck's top            -> he walks to it and climbs it (on top);
//   b  double click on the 140 cm wreck's face    -> he climbs it (the face the camera sees is the obstacle too);
//   b2 single click on the 140 cm wreck's face    -> no climb: the cover preview, as before (decision for the designer);
//   c  double click on the 230 cm parapet's face  -> he climbs it;
//   d  click on a 330 cm fence (a wall)           -> no climb (the click keeps its old meaning);
//   e  hover over a climbable top / face          -> the mantle ghost shows at the landing (the corner-peek ghost's look, ACoverGhostActor);
//   f  hover over the 330 cm fence                -> no mantle ghost.
// Enemies are parked far away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Tactics/CoverGhostActor.h"

namespace MantleClickSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** He stands this far off the face before the click. */
	constexpr float StandOffCm = 400.f;
	/** A climb ordered by the click has to have started within this long, s. */
	constexpr float ClimbStartLimitSeconds = 10.f;
	/** A mantle ghost counts when it stands within this of the planned landing (2D), cm. */
	constexpr float GhostNearLandingCm = 120.f;

	enum class EAim : uint8 { Top, Face };

	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* Kind;
		float HeightCm;
		EAim Aim;
		int32 Clicks;
		/** The obstacle is climbable: the hover shows the mantle ghost (else none). */
		bool bClimbable;
		/** The click climbs it (a single click on a face keeps the cover preview: no climb). */
		bool bClickClimbs;
	};

	static const FCase Cases[] = {
		{ TEXT("a: click on the 140 cm wreck's top"), TEXT("wreck"), 140.f, EAim::Top, 1, true, true },
		{ TEXT("b: double click on the 140 cm wreck's face"), TEXT("wreck"), 140.f, EAim::Face, 2, true, true },
		{ TEXT("b2: single click on the 140 cm wreck's face"), TEXT("wreck"), 140.f, EAim::Face, 1, true, false },
		{ TEXT("c: double click on the 230 cm parapet's face"), TEXT("cut_wall"), 230.f, EAim::Face, 2, true, true },
		{ TEXT("d: click on a 330 cm fence"), TEXT("fence"), 330.f, EAim::Face, 1, false, false },
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Case = 0;
		int32 Failures = 0;
		int32 MantlesBefore = 0;
		bool bHover = false;
		MantleSmokeSites::FSite Site;
		FHitResult Hit;
		TSet<const AActor*> Used;
		/** Obstacles whose aimed face the camera cannot see (another obstacle in front): another one of the kind is taken. */
		TSet<const AActor*> Hidden;
		int32 Retries = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleClickSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	/**
	 * The point the cursor is put on: the top's middle over the face he stands at, or the middle (half height) of the face the CAMERA sees
	 * (an isometric camera shows a side face of the obstacle - the designer clicks there).
	 */
	FVector AimPoint(UWorld* World, const MantleSmokeSites::FSite& Site, EAim Aim)
	{
		const FMantleSite& Probe = Site.Probe;
		if (Aim == EAim::Top)
		{
			const FVector Top = Probe.EdgeTop + Probe.Dir * FMath::Max(20.f, Probe.Probe.DepthCm * 0.5f);
			return FVector(Top.X, Top.Y, Probe.TopZ);
		}
		const AActor* Obstacle = Site.Actor.Get();
		const APlayerController* PC = World->GetFirstPlayerController();
		const FBox Local = Obstacle ? Obstacle->CalculateComponentsBoundingBoxInLocalSpace(false) : FBox(ForceInit);
		if (!Obstacle || !PC || !PC->PlayerCameraManager || !Local.IsValid)
		{
			return FVector(Probe.FacePoint.X, Probe.FacePoint.Y, Site.Feet.Z + Probe.Probe.HeightCm * 0.5f) - Probe.Dir * 1.f;
		}
		const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
		const FTransform Transform = Obstacle->GetActorTransform();
		FVector Best = Probe.FacePoint;
		double BestDot = -2.0;
		for (int32 Face = 0; Face < 4; ++Face)
		{
			const int32 Axis = Face % 2;
			FVector LocalFace = Local.GetCenter();
			LocalFace[Axis] = Face < 2 ? Local.Max[Axis] : Local.Min[Axis];
			FVector LocalNormal = FVector::ZeroVector;
			LocalNormal[Axis] = Face < 2 ? 1.f : -1.f;
			const FVector Normal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal2D();
			const FVector Point = Transform.TransformPosition(LocalFace);
			const double Dot = FVector::DotProduct(Normal, (Eye - Point).GetSafeNormal2D());
			if (Dot > BestDot)
			{
				BestDot = Dot;
				Best = FVector(Point.X, Point.Y, Site.Feet.Z + Probe.Probe.HeightCm * 0.5f) + Normal * 1.f;
			}
		}
		return Best;
	}

	/** A mantle ghost shown now (an ACoverGhostActor up near the planned landing / the obstacle). */
	const ACoverGhostActor* FindMantleGhost(UWorld* World, const FVector& Near)
	{
		for (TActorIterator<ACoverGhostActor> It(World); It; ++It)
		{
			if (It->IsShown() && It->IsMantlePose() && !It->IsHidden() && FVector::Dist2D(It->GetActorLocation(), Near) <= GhostNearLandingCm)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The hover under the cursor (the controller's PlayerTick feeds the real cursor hit; here the smoke's ray). */
	void Hover(ACodexTacticsPlayerController& PC, AOperativeCharacter& Leader, const FHitResult* Hit)
	{
		PC.bFreezeMantleHoverForTesting = Hit != nullptr; // PlayerTick would feed the (absent) real cursor
		PC.UpdateMantleHover(&Leader, Hit);
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(*State, false, State->PreloadGate.FailReason);
			return Finish(*State, false);
		default:
			break;
		}
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		if (State->Time > 400.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d, case %d"), State->Stage, State->Case));
			return Finish(*State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation > 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = State->Leader.Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || !PC || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			State->Leader->bTacticalCeaseFire = true;
			Next(*State, 1);
			return true;
		}
		case 1: // place him StandOffCm in front of the case's obstacle
		{
			const int32 CaseCount = UE_ARRAY_COUNT(Cases);
			if (State->Case >= CaseCount * 2)
			{
				return Finish(*State, true);
			}
			State->bHover = State->Case >= CaseCount; // the second pass: the hover over the same obstacles
			const FCase& Case = Cases[State->Case % CaseCount];
			TSet<const AActor*> Skip = State->Used;
			Skip.Append(State->Hidden);
			State->Site = MantleSmokeSites::FindSite(World, Leader, Case.Kind, Case.HeightCm, StandOffCm, Leader->GetActorLocation(), Skip, false, 0.f,
				Case.bClimbable);
			if (!State->Site.bFound)
			{
				Check(*State, false, FString::Printf(TEXT("%s: a test site on the map"), Case.Label));
				++State->Case;
				return true;
			}
			if (!State->bHover)
			{
				State->Used.Add(State->Site.Actor.Get());
			}
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			Leader->TeleportTo(State->Site.Feet + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), State->Site.Dir.Rotation(),
				false, true);
			State->MantlesBefore = Leader->GetMantleCount();
			Next(*State, 2);
			return true;
		}
		case 2: // the camera has followed him: the click (or the hover)
		{
			if (State->StageTime < 1.5f)
			{
				return true;
			}
			const FCase& Case = Cases[State->Case % UE_ARRAY_COUNT(Cases)];
			FVector Eye = FVector::ZeroVector;
			const FVector Aim = AimPoint(World, State->Site, Case.Aim);
			if (!MantleSmokeSites::CameraRayHit(World, Aim, State->Hit, &Eye))
			{
				Check(*State, false, FString::Printf(TEXT("%s: the cursor's ray from the camera hits something"), Case.Label));
				++State->Case;
				Next(*State, 1);
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleClickSmoke] %s: the cursor's ray from %s hits %s at %s (normal z %.2f; aimed at %s on %s)"),
				Case.Label, *Eye.ToCompactString(), *GetNameSafe(State->Hit.GetActor()), *State->Hit.ImpactPoint.ToCompactString(), State->Hit.ImpactNormal.Z,
				*Aim.ToCompactString(), *GetNameSafe(State->Site.Actor.Get()));
			// The camera cannot see that face / top (another obstacle in front of it): the same case on another obstacle of the kind.
			if (State->Hit.GetActor() != State->Site.Actor.Get() && State->Retries < 8)
			{
				State->Hidden.Add(State->Site.Actor.Get());
				++State->Retries;
				Next(*State, 1);
				return true;
			}
			State->Retries = 0;
			if (State->bHover)
			{
				Hover(*PC, *Leader, &State->Hit);
				Next(*State, 4);
				return true;
			}
			for (int32 Click = 0; Click < Case.Clicks; ++Click)
			{
				PC->HandleWorldHit(State->Hit);
			}
			Next(*State, 3);
			return true;
		}
		case 3: // the click's outcome
		{
			const FCase& Case = Cases[State->Case % UE_ARRAY_COUNT(Cases)];
			const bool bClimbed = Leader->GetMantleCount() > State->MantlesBefore;
			if (!bClimbed && State->StageTime < ClimbStartLimitSeconds)
			{
				return true;
			}
			if (Case.bClickClimbs)
			{
				Check(*State, bClimbed && Leader->GetLastMantleRefusal() == EMantleRefusal::None, FString::Printf(
					TEXT("%s: he goes to it and climbs it (%s after %.1f s; hit %s, last refusal %s)"), Case.Label, bClimbed ? *Leader->GetLastMantleClip() : TEXT("NO climb"),
					State->StageTime, *GetNameSafe(State->Hit.GetActor()), MantleRules::RefusalName(Leader->GetLastMantleRefusal())));
			}
			else
			{
				Check(*State, !bClimbed, FString::Printf(TEXT("%s: no climb (%s; the cover preview %s)"), Case.Label, bClimbed ? TEXT("he climbed") : TEXT("none"),
					PC->HasCoverPreview() ? TEXT("is up") : TEXT("is not up")));
			}
			PC->HideCoverPreview();
			// Let the climb end before the next case.
			Next(*State, 30);
			return true;
		}
		case 30:
		{
			if (Leader->IsMantling() && State->StageTime < 12.f)
			{
				return true;
			}
			++State->Case;
			Next(*State, 1);
			return true;
		}
		case 4: // the hover's outcome (a few frames for the ghost)
		{
			if (State->StageTime < 0.3f)
			{
				return true;
			}
			const FCase& Case = Cases[State->Case % UE_ARRAY_COUNT(Cases)];
			const FVector Near = Case.bClimbable ? State->Site.Probe.Landing : State->Site.Probe.FacePoint;
			const ACoverGhostActor* Ghost = FindMantleGhost(World, Near);
			if (Case.bClimbable)
			{
				Check(*State, Ghost != nullptr, FString::Printf(TEXT("e: hovering %s (%s) shows the mantle ghost at the landing (%s)"), *GetNameSafe(State->Site.Actor.Get()),
					Case.Label, Ghost ? *FString::Printf(TEXT("%.0f cm off the landing"), FVector::Dist2D(Ghost->GetActorLocation(), Near)) : TEXT("NO ghost")));
			}
			else
			{
				Check(*State, Ghost == nullptr, FString::Printf(TEXT("f: hovering %s (%s) shows no mantle ghost (%s)"), *GetNameSafe(State->Site.Actor.Get()), Case.Label,
					Ghost ? TEXT("a ghost is up") : TEXT("none")));
			}
			Hover(*PC, *Leader, nullptr);
			++State->Case;
			Next(*State, 1);
			return true;
		}
		default:
			return Finish(*State, false);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MantleClickSmoke"),
		TEXT("Dev check (mantle click order + hover ghost, designer bug report 2026-11-16 batch 2, L_Gen_Depot_1): a click through the controller's path on a ")
		TEXT("140 wreck's top / face and a double click on the 230 parapet climb them, a 330 fence does not; hovering a climbable obstacle shows the mantle ghost, ")
		TEXT("a fence none; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
