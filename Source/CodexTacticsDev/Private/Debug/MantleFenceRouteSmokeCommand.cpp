// Dev-only console command: a Solo move order across a low (135 cm) fence takes the mantle over it on a generated map (designer report
// 2026-11-16, batch 2 item 9: "on the generated maps operatives never vault or mantle - at a low fence they run around it";
// docs/port/MANTLING_BATCH2_REPORT.md):
//   Scripts/smoke_parallel.ps1 -Commands MantleFenceRouteSmoke -ExtraArgs -Multiprocess        (L_Gen_Depot_1)
//   Scripts/smoke.ps1 -Command CodexTactics.MantleFenceRouteSmoke -Map /Game/Maps/Generated/<map>  (any generated map)
// Checks: the map got its mantle links (none dropped by the links.max_total cap); up to MaxFences 135 cm fences whose way round is longer than
// the mantle area's entering cost by links.entering_cost_cm: the fence carries a link; a ground click beyond it (the controller's
// HandleWorldHit) makes the Solo leader mantle over it (high_fence_over) and arrive within the time limit.
// B17-4 (designer 2026-11-17: "his body passes THROUGH the fence first, only then the vault clip plays"): on that fence four ways over it -
// the ground click beyond (the path's link, walking), a double ground click beyond (the link, running), a double click on the fence's face
// (the run climb order) and a click on its top (the walk climb order; skipped when the camera cannot hit the 10 cm top) - each time, before
// the clip's hand plant neither the pelvis nor the capsule's centre is past the fence's near face plane, and at the plant the nearer hand is on
// the fence's top (within align.hand_edge_tolerance_cm of the grip).

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
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace MantleFenceRouteSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** He stands this far off the fence's face; the goal lies this far past its far face. */
	constexpr float StandOffCm = 150.f;
	constexpr float GoalOffCm = 200.f;
	/** B17-4: each way over the fence starts this much farther out than StandOffCm when that ground is navmesh (a natural approach), cm. */
	constexpr float ExtraRunUpCm = 150.f;
	/** Fences tried (one that qualifies is enough). */
	constexpr int32 MaxFences = 6;
	/** A fence order is over after this long, s. */
	constexpr float ArriveLimitSeconds = 20.f;
	constexpr float ArriveRadiusCm = 120.f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Tried = 0;
		int32 Passed = 0;
		int32 MantlesBefore = 0;
		bool bMantledHere = false;
		MantleSmokeSites::FSite Site;
		FVector Goal = FVector::ZeroVector;
		TSet<const AActor*> Used;
		/** B17-4: the way over the qualifying fence (EVariant) and the climb's frames. */
		int32 Variant = 0;
		bool bSampling = false;
		bool bDumped = false;
		TArray<MantleSmokeSites::FClimbSample> Samples;
		FTSTicker::FDelegateHandle SampleTicker;
	};

	/** B17-4: the ways over the fence (the path's link walking / running, the climb order running / walking). */
	enum EVariant : int32 { PathWalk, PathRun, FaceDoubleClick, TopClick, VariantCount };
	const TCHAR* VariantName(int32 Variant)
	{
		switch (Variant)
		{
		case PathWalk: return TEXT("path link, walking (a ground click beyond)");
		case PathRun: return TEXT("path link, running (a double ground click beyond)");
		case FaceDoubleClick: return TEXT("run climb order (a double click on its face)");
		default: return TEXT("walk climb order (a click on its top)");
		}
	}

	bool Sample(TSharedRef<FState> State)
	{
		if (const AOperativeCharacter* Leader = State->Leader.Get(); Leader && State->bSampling)
		{
			State->Samples.Add(MantleSmokeSites::SampleClimb(*Leader));
		}
		return true;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(State.SampleTicker);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleFenceRouteSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector FeetOf(const AOperativeCharacter& Leader)
	{
		return Leader.GetActorLocation() - FVector(0.f, 0.f, Leader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/** B17-4: the body before the clip's hand plant stays on the near side of the fence; at the plant the hands are on its top. */
	void CheckNoPenetration(FState& State, AOperativeCharacter& Leader)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		if (!State.bDumped)
		{
			State.bDumped = true;
			MantleSmokeSites::DumpClipTrack(Leader, Leader.GetLastMantleClipIndex(), TEXT("MantleFenceRouteSmoke"));
		}
		const MantleSmokeSites::FContactMeasure M = MantleSmokeSites::MeasureContact(State.Samples, Leader.GetMantleFacePoint(), Leader.GetMantleDir(),
			Leader.GetMantleTopZ(), MantleSmokeSites::ClimbContactSeconds(Leader));
		const float Radius = Leader.GetCapsuleComponent()->GetScaledCapsuleRadius();
		Check(State, M.bValid && M.bFromClip && M.MaxPelvisFwdBeforeCm < 0.f && M.MaxCapsuleFwdBeforeCm < 0.f && M.HandGapCm <= Config.AlignHandEdgeToleranceCm,
			FString::Printf(TEXT("%s, %s (%s): before the hand plant (%s %.2f s) the pelvis reaches %+.1f cm and the capsule's centre %+.1f cm past the fence's near face ")
				TEXT("(< 0 = still in front; the capsule's front %+.1f cm); at the plant the nearer hand is %.1f cm from the grip on its top (%+.1f cm past the face, %+.1f cm over the top; ")
				TEXT("tolerance %.0f); the clip started %.0f cm off the face"),
				*GetNameSafe(State.Site.Actor.Get()), VariantName(State.Variant), *Leader.GetLastMantleClip(), M.bFromClip ? TEXT("the clip's plant at") : TEXT("NO plant -"),
				M.Seconds, M.MaxPelvisFwdBeforeCm, M.MaxCapsuleFwdBeforeCm, M.MaxCapsuleFwdBeforeCm + Radius, M.HandGapCm, M.HandFwdCm, M.HandUpCm,
				Config.AlignHandEdgeToleranceCm, M.StartFaceCm));
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
		if (State->Time > 500.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
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
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FMantleConfig& Config = MantleRules::GetConfig();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || !PC || !Nav || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 240.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			State->Leader->bTacticalCeaseFire = true;
			// The pose ticks and the bones refresh as on screen (a headless game skips an unseen mesh's pose): the pelvis / hands vs the fence.
			State->Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State->SampleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Sample(State); }));
			int32 FenceLinks = 0;
			for (const FMantleLink& Link : Mantle->GetLinks())
			{
				FenceLinks += Link.ClassId == FName(TEXT("high_fence_over")) ? 1 : 0;
			}
			Check(*State, Mantle->HasGenerated() && !Mantle->WasLinkCapReached() && FenceLinks > 0, FString::Printf(
				TEXT("the map's mantle links: %d (%d over fences), the cap (links.max_total %.0f) %s"), Mantle->GetLinks().Num(), FenceLinks, Config.LinkMaxTotal,
				Mantle->WasLinkCapReached() ? TEXT("REACHED - links dropped") : TEXT("not reached")));
			Next(*State, 1);
			return true;
		}
		case 1: // a 135 cm fence whose way round is long enough for the link to pay
		{
			if (State->Tried >= MaxFences || State->Passed > 0)
			{
				Check(*State, State->Passed > 0, FString::Printf(TEXT("a Solo ground click across a 135 cm fence mantles over it (%d of %d fences tried)"), State->Passed,
					State->Tried));
				return Finish(*State, State->Tried > 0);
			}
			State->Site = MantleSmokeSites::FindSite(World, Leader, TEXT("fence"), 135.f, StandOffCm, Leader->GetActorLocation(), State->Used, true, GoalOffCm);
			if (!State->Site.bFound)
			{
				Check(*State, State->Tried > 0, FString::Printf(TEXT("a 135 cm fence with free ground on both sides (%d tried)"), State->Tried));
				return Finish(*State, false);
			}
			State->Used.Add(State->Site.Actor.Get());
			const FMantleSite& Probe = State->Site.Probe;
			State->Goal = Probe.FacePoint + Probe.Dir * (Probe.Probe.DepthCm + GoalOffCm);
			State->Goal.Z = State->Site.Feet.Z;
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(State->Goal, OnNav, FVector(60.f, 60.f, 100.f)))
			{
				return true;
			}
			State->Goal = OnNav.Location;
			const UNavigationPath* Round = Nav->FindPathToLocationSynchronously(World, State->Site.Feet, State->Goal, Leader, UNavFilter_OperativeNoMantle::StaticClass());
			const float Direct = static_cast<float>(FVector::Dist2D(State->Site.Feet, State->Goal));
			const float RoundCm = Round && Round->IsValid() && !Round->IsPartial() ? static_cast<float>(Round->GetPathLength()) : TNumericLimits<float>::Max();
			int32 Links = 0;
			for (const FMantleLink& Link : Mantle->GetLinks())
			{
				Links += Link.Obstacle.Get() == State->Site.Actor.Get() && !Link.bDown ? 1 : 0;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleFenceRouteSmoke] %s: %d links over it, the way round %.0f cm vs %.0f straight (entering cost %.0f)"),
				*GetNameSafe(State->Site.Actor.Get()), Links, RoundCm >= TNumericLimits<float>::Max() ? -1.f : RoundCm, Direct, Config.LinkEnteringCostCm);
			if (RoundCm < Direct + Config.LinkEnteringCostCm + 100.f)
			{
				return true; // the way round is short: the path rightly walks round - another fence
			}
			++State->Tried;
			Check(*State, Links > 0, FString::Printf(TEXT("%s (135 cm fence, way round %.0f cm): it carries mantle links (%d)"), *GetNameSafe(State->Site.Actor.Get()),
				RoundCm >= TNumericLimits<float>::Max() ? -1.f : RoundCm, Links));
			State->Variant = PathWalk;
			Next(*State, 20);
			return true;
		}
		case 20: // (each variant) back in front of the fence
		{
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			// B17-4: the variants walk up from farther out when the ground allows it (the vault's own start lies ~182 cm off the face: he reaches it
			// on his way, as in play), else from the site's spot.
			FVector Start = State->Site.Feet;
			FNavLocation Back;
			if (Nav->ProjectPointToNavigation(State->Site.Feet - State->Site.Dir * ExtraRunUpCm, Back, FVector(40.f, 40.f, 60.f))
				&& FMath::Abs(Back.Location.Z - State->Site.Feet.Z) < 30.f)
			{
				Start = FVector(Back.Location.X, Back.Location.Y, State->Site.Feet.Z);
			}
			Leader->TeleportTo(Start + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), State->Site.Dir.Rotation(), false, true);
			Next(*State, 2);
			return true;
		}
		case 2: // settled: the click(s) of the variant
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			State->MantlesBefore = Leader->GetMantleCount();
			const FMantleSite& Probe = State->Site.Probe;
			FHitResult Hit;
			bool bHit = false;
			int32 Clicks = 1;
			if (State->Variant == PathWalk || State->Variant == PathRun)
			{
				bHit = World->LineTraceSingleByChannel(Hit, State->Goal + FVector(0.f, 0.f, 150.f), State->Goal - FVector(0.f, 0.f, 150.f), ECC_Visibility);
				Clicks = State->Variant == PathRun ? 2 : 1;
			}
			else
			{
				// The camera's ray through the fence's face (half height) / the middle of its top.
				const bool bTop = State->Variant == TopClick;
				const FVector Aim = bTop ? FVector(Probe.EdgeTop.X, Probe.EdgeTop.Y, Probe.TopZ) + Probe.Dir * (Probe.Probe.DepthCm * 0.5f)
					: FVector(Probe.FacePoint.X, Probe.FacePoint.Y, State->Site.Feet.Z + Probe.Probe.HeightCm * 0.5f) - Probe.Dir * 1.f;
				bHit = MantleSmokeSites::CameraRayHit(World, Aim, Hit) && Hit.GetActor() == State->Site.Actor.Get()
					&& (bTop ? Hit.ImpactNormal.Z >= 0.7f : FMath::Abs(Hit.ImpactNormal.Z) < 0.3f);
				Clicks = bTop ? 1 : 2;
			}
			if (!bHit)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[MantleFenceRouteSmoke] %s, %s: the click cannot hit it from this camera - skipped"), *GetNameSafe(State->Site.Actor.Get()),
					VariantName(State->Variant));
				Next(*State, 4);
				return true;
			}
			for (int32 Click = 0; Click < Clicks; ++Click)
			{
				PC->HandleWorldHit(Hit);
			}
			State->Samples.Reset();
			State->bSampling = true;
			Next(*State, 3);
			return true;
		}
		case 3:
		{
			const FVector Feet = FeetOf(*Leader);
			const bool bMantled = Leader->GetMantleCount() > State->MantlesBefore;
			const bool bPath = State->Variant == PathWalk || State->Variant == PathRun;
			const bool bThere = bPath ? FVector::Dist2D(Feet, State->Goal) <= ArriveRadiusCm && !Leader->IsMantling() : bMantled && !Leader->IsMantling();
			if (!bThere && State->StageTime < ArriveLimitSeconds)
			{
				return true;
			}
			State->bSampling = false;
			if (bPath)
			{
				Check(*State, bThere && bMantled, FString::Printf(TEXT("%s: the click beyond it (%s) - %s, %s in %.1f s (%.0f cm off the goal)"),
					*GetNameSafe(State->Site.Actor.Get()), VariantName(State->Variant),
					bMantled ? *FString::Printf(TEXT("mantled over it (%s)"), *Leader->GetLastMantleClass().ToString()) : TEXT("NO mantle"),
					bThere ? TEXT("arrived") : TEXT("NOT arrived"), State->StageTime, FVector::Dist2D(Feet, State->Goal)));
				State->Passed += State->Variant == PathWalk && bThere && bMantled ? 1 : 0;
			}
			else if (State->Variant == TopClick && !bMantled)
			{
				// A fence's 10 cm top is no top to climb onto (the controller's comment): only measured when the click does climb it.
				UE_LOG(LogCodexTactics, Display, TEXT("[MantleFenceRouteSmoke] %s, %s: no climb (not a click order for a fence) - nothing to measure"),
					*GetNameSafe(State->Site.Actor.Get()), VariantName(State->Variant));
			}
			else
			{
				Check(*State, bMantled && Leader->GetLastMantleClass() == FName(TEXT("high_fence_over")), FString::Printf(TEXT("%s: the %s climbs over it (%s, %s)"),
					*GetNameSafe(State->Site.Actor.Get()), VariantName(State->Variant), bMantled ? *Leader->GetLastMantleClip() : TEXT("NO climb"),
					*Leader->GetLastMantleClass().ToString()));
			}
			if (bMantled && Leader->GetLastMantleClass() == FName(TEXT("high_fence_over")))
			{
				CheckNoPenetration(*State, *Leader);
			}
			Next(*State, 4);
			return true;
		}
		case 4: // the next variant on the same fence (the first one failing: the next fence)
		{
			if (Leader->IsMantling() && State->StageTime < 6.f)
			{
				return true;
			}
			++State->Variant;
			Next(*State, State->Variant < VariantCount && State->Passed > 0 ? 20 : 1);
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
		TEXT("CodexTactics.MantleFenceRouteSmoke"),
		TEXT("Dev check (batch 2 item 9, any generated map): the map's mantle links are complete (no cap), a Solo ground click across a 135 cm fence whose way ")
		TEXT("round is long mantles over it and arrives; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
