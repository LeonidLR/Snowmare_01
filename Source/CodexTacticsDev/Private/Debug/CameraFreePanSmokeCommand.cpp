// Dev-only headless check of the FREE camera pan (the game designer's play-test 2026-11-01, bug_04 B: "after panning far enough along X or Y
// the camera stops, tied to the selected character; only a click on him gets it moving again"):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CameraFreePanSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions; 120 x 120 m)
// 1. a WASD-like pan (ATacticalCameraPawn::NudgePan, 1 m a step) 70 m along X from the leader, then 70 m along Y (99 m from him - past the old
// MaxPanCombat leash of 85 m round the focus frozen at the selected character): the view keeps moving over each leg's last 10 m and ends
// where it was panned; 2. on past the level's edge: the view stops at the level's area + FreePanLevelMarginCm (by axis), not at a radius;
// 3. the leader's number re-centres the camera on him. A/B: -PreCmds "Codex.Camera.FreePanBounds 0" = the old leash (step 1 fails).
// Code: Camera/TacticalCameraPawn.cpp (LimitPan), Camera/TacticalCameraRules.cpp (ClampFreePan), UI/MinimapSubsystem.h (GetLevelArea).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MinimapSubsystem.h"

namespace CameraFreePanSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** One pan step (a WASD frame at 2000 cm/s is ~33 cm; bigger steps keep the run short), cm. */
	constexpr float StepCm = 100.f;
	/** Each leg, cm (two legs: 99 m from the leader, past the old 85 m leash). */
	constexpr float LegCm = 7000.f;
	/** The view must still move this much over a leg's last 10 m, cm. */
	constexpr float TailMoveCm = 900.f;
	constexpr float SettleSeconds = 2.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector LeaderAt = FVector::ZeroVector;
		FVector ViewStart = FVector::ZeroVector;
		FVector2D Dir = FVector2D(1.0, 1.0);
		float Panned = 0.f;
		FVector ViewAtTail = FVector::ZeroVector;
		bool bTailTaken = false;
		bool bArea = false;
		FBox2D Area = FBox2D(ForceInit);
		float Margin = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CameraFreePanSmoke"));
		return false;
	}

	/** One pan leg along Axis (0 = X, 1 = Y) in Sign's direction, StepCm a step; true while it runs. */
	bool PanLeg(FState& State, ATacticalCameraPawn& Camera, int32 Axis, float Sign, float Length)
	{
		if (State.Panned >= Length)
		{
			return false;
		}
		if (!State.bTailTaken && State.Panned >= Length - 1000.f)
		{
			State.bTailTaken = true;
			State.ViewAtTail = Camera.GetViewCentre();
		}
		Camera.NudgePan(Axis == 0 ? FVector(Sign * StepCm, 0.f, 0.f) : FVector(0.f, Sign * StepCm, 0.f));
		State.Panned += StepCm;
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 120.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Camera || !Leader)
		{
			return State.Time < 10.f;
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; State.Panned = 0.f; State.bTailTaken = false; };
		const IConsoleVariable* Bounds = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Camera.FreePanBounds"));
		switch (State.Stage)
		{
		case 0: // settle; the leader's spot; the level's area
		{
			if (State.StageTime < 4.f)
			{
				return true;
			}
			State.LeaderAt = Leader->GetActorLocation();
			State.ViewStart = Camera->GetViewCentre();
			if (const UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(World))
			{
				State.bArea = Minimap->GetLevelArea(State.Area);
			}
			State.Margin = Camera->Config.FreePanLevelMarginCm;
			// Into the level from the leader (the lab is 120 x 120 m round its centre).
			State.Dir = FVector2D(State.LeaderAt.X > 0.0 ? -1.0 : 1.0, State.LeaderAt.Y > 0.0 ? -1.0 : 1.0);
			UE_LOG(LogCodexTactics, Display, TEXT("CameraFreePanSmoke: leader %s at (%.0f, %.0f), view (%.0f, %.0f); level area %s (%.0f..%.0f, %.0f..%.0f), margin %.0f cm; FreePanBounds %d"),
				*Leader->DisplayName.ToString(), State.LeaderAt.X, State.LeaderAt.Y, State.ViewStart.X, State.ViewStart.Y, State.bArea ? TEXT("known") : TEXT("none"),
				State.Area.Min.X, State.Area.Max.X, State.Area.Min.Y, State.Area.Max.Y, State.Margin, Bounds ? Bounds->GetInt() : -1);
			Next();
			return true;
		}
		case 1: // leg 1: 70 m along X
			if (PanLeg(State, *Camera, 0, static_cast<float>(State.Dir.X), LegCm) || State.StageTime < LegCm / StepCm * StepSeconds + SettleSeconds)
			{
				return true;
			}
			{
				const FVector View = Camera->GetViewCentre();
				const float AlongX = static_cast<float>((View.X - State.LeaderAt.X) * State.Dir.X);
				const float Tail = static_cast<float>((View.X - State.ViewAtTail.X) * State.Dir.X);
				UE_LOG(LogCodexTactics, Display, TEXT("CameraFreePanSmoke: leg X - view (%.0f, %.0f), %.0f cm along X from the leader, %.0f cm over the last 10 m"), View.X, View.Y, AlongX, Tail);
				Check(State, Camera->IsDetachedFromFollow(), TEXT("the pan detached the camera from the leader"));
				Check(State, AlongX >= LegCm - 150.f, FString::Printf(TEXT("70 m along X from the selected operative: the view went %.0f cm"), AlongX));
				Check(State, Tail >= TailMoveCm, FString::Printf(TEXT("... and still moved over the leg's last 10 m (%.0f cm)"), Tail));
			}
			Next();
			return true;
		case 2: // leg 2: 70 m along Y (99 m from him: past the old 85 m leash)
			if (PanLeg(State, *Camera, 1, static_cast<float>(State.Dir.Y), LegCm) || State.StageTime < LegCm / StepCm * StepSeconds + SettleSeconds)
			{
				return true;
			}
			{
				const FVector View = Camera->GetViewCentre();
				const float AlongY = static_cast<float>((View.Y - State.LeaderAt.Y) * State.Dir.Y);
				const float Tail = static_cast<float>((View.Y - State.ViewAtTail.Y) * State.Dir.Y);
				const float FromLeader = static_cast<float>(FVector::Dist2D(View, State.LeaderAt));
				UE_LOG(LogCodexTactics, Display, TEXT("CameraFreePanSmoke: leg Y - view (%.0f, %.0f), %.0f cm along Y, %.0f cm from the leader, %.0f cm over the last 10 m"), View.X, View.Y,
					AlongY, FromLeader, Tail);
				Check(State, AlongY >= LegCm - 150.f, FString::Printf(TEXT("then 70 m along Y: the view went %.0f cm"), AlongY));
				Check(State, Tail >= TailMoveCm, FString::Printf(TEXT("... and still moved over the leg's last 10 m (%.0f cm; the old leash stopped it 85 m from him)"), Tail));
				Check(State, FromLeader > 9000.f, FString::Printf(TEXT("the view is %.0f cm from the selected operative (no leash)"), FromLeader));
			}
			Next();
			return true;
		case 3: // on past the level's west / south edge: the level stops the view, not a radius
		{
			const float Out = 20000.f;
			if (PanLeg(State, *Camera, 0, static_cast<float>(-State.Dir.X), Out) || State.StageTime < Out / StepCm * StepSeconds + SettleSeconds)
			{
				return true;
			}
			const FVector View = Camera->GetViewCentre();
			if (State.bArea)
			{
				const double Edge = State.Dir.X > 0.0 ? State.Area.Min.X - State.Margin : State.Area.Max.X + State.Margin;
				UE_LOG(LogCodexTactics, Display, TEXT("CameraFreePanSmoke: past the edge - view x %.0f, the level's edge + margin %.0f"), View.X, Edge);
				Check(State, FMath::Abs(View.X - Edge) <= 60.0, FString::Printf(TEXT("panned past the level: the view stops at its edge + %.0f cm (x %.0f, edge %.0f)"), State.Margin, View.X, Edge));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("CameraFreePanSmoke: no level area known (no minimap setup) - the edge check is skipped; view x %.0f"), View.X);
			}
			PC->SelectSquadMember(Leader->SquadIndex);
			Next();
			return true;
		}
		case 4:
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, !Camera->IsDetachedFromFollow() && FVector::Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation()) < 25.f,
				FString::Printf(TEXT("the leader's number re-centres the camera on him (%.0f cm off)"), FVector::Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation())));
			return Finish(State, true);
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CameraFreePanSmoke"),
		TEXT("Dev check (bug_04 B, 2026-11-01): the free camera pan has no leash to the selected operative - 70 m along X then 70 m along Y keeps moving, the level's area stops it; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
