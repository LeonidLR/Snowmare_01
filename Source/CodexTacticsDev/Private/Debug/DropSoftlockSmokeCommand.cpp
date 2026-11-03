// Dev-only headless check of the soft-lock after a drop through a roof hole (user bug 2026-11-03, editor PIE on L_Gen_Depot_1: "When dropping
// through a hole in the roof the characters just got stuck. All three got hard stuck."). docs/systems/stances-and-movement.md "Falls and
// drops" (the soft-lock follow-up). On /Game/Maps/Generated/L_Gen_Depot_1, the pressure off:
//  1. user  - the user's case: the squad on the ruin's roof (+2) where his log had it, squad mode, the leader clicked onto the floor below
//             (-2738, 2783, +1) - the path drops through a roof hole; then a click on the ground outside the ruin (his next click): every member
//             must get there (no "unreachable" order, nobody standing still off the navmesh);
//  2. holes - the same through every roof / floor hole of the layout: the squad dropped onto the slab below and walked back out;
//  3. sweep - every drop link (UDropLinkSubsystem) and hole link (the builder's NavLinkProxy, tag LevelGenDrop) lands in an area that reaches the
//             main navmesh (DropLinkReach: the landing poly reaches the player start / the squad's spawn);
//  4. rescue - the safety net: an operative put off the navmesh after a fall is brought back onto the nearest reachable navmesh of his level.
// PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands DropSoftlockSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: stages=<user+holes+sweep+rescue> (a subset; "," works from the console), shots (rendered pictures into
// Saved/Screenshots/DropSoftlock/; run with -RenderOffscreen), regen (the sweep regenerates the links first), probe=<x>_<y>_<z> (the sweep logs
// the pathfinder's way from that point to the player start), trace (every member every 0.5 s). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Debug/DropReachSweep.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/LedgeFallComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationSystem.h"

namespace DropSoftlockSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelTolCm = 45.f;
	constexpr float StillFailSeconds = 12.f;
	constexpr float TotalLimitSeconds = 1400.f;

	/** The user's PIE log (2026-11-03): the squad on the ruin's roof (feet 602), the click onto the floor below. */
	const FVector2D UserLeaderXY(-3375.f, 2713.f);
	const FVector2D UserFollowerXY[] = { FVector2D(-3560.f, 2560.f), FVector2D(-3620.f, 2780.f) };
	const FVector UserClick(-2738.f, 2783.f, 300.f);
	/** His next click: the ground outside the ruin at the external stair (logged "unreachable" for every member). */
	const FVector GroundClick(-4240.f, 1991.f, 0.f);

	enum class EStage : uint8 { User, Holes, Sweep, Rescue };

	struct FLeg
	{
		EStage Stage = EStage::User;
		FString Name;
		/** Where the squad starts (feet; the leader first) and the two clicks. */
		TArray<FVector> Starts;
		FVector Click1 = FVector::ZeroVector;
		FVector Click2 = FVector::ZeroVector;
		float Limit1 = 30.f;
		float Limit2 = 90.f;
		/** The rescue stage: the root fix off (Codex.Fall.LinkCommit 0) - the soft-lock happens and the safety net must bring the squad out. */
		bool bLinkCommitOff = false;
		/** A walking leg passes only with no rescue (the root fix alone), the rescue leg only with one at least. */
		bool bExpectRescue = false;
	};

	/** The user's soft-lock spot: the Engineer ON the rim of hole_roof_1 (feet 596, his centre 22 cm over the hole: off the navmesh). */
	const FVector PerchFeet(-3178.f, 2586.f, 596.f);

	enum class EPhase : uint8 { Settle, Setup, Click1, Run1, Click2, Run2, Sweep, Rescue, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		EPhase Phase = EPhase::Settle;
		float Time = 0.f;
		float PhaseTime = 0.f;
		float DoneFor = 0.f;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> StillFor;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> LastPos;
		TMap<TWeakObjectPtr<AOperativeCharacter>, int32> LandingsAtStart;
		int32 Failures = 0;
		int32 LegsOk = 0;
		TArray<FString> Summary;
		TSet<FString> StageFilter;
		bool bTrace = false;
		bool bRegen = false;
		/** shots: rendered pictures into Saved/Screenshots/DropSoftlock/ (run with -RenderOffscreen); the names already taken this run. */
		bool bShots = false;
		TSet<FString> ShotsTaken;
		TWeakObjectPtr<ACameraActor> Camera;
		FString PendingShot;
		int32 PendingShotFrames = 0;
		TArray<FVector> Probes;
		float TraceClock = 0.f;
		int32 RescuesAtStart = 0;
		TWeakObjectPtr<AOperativeCharacter> RescueWho;
	};

	int32 CountRescues(USquadSubsystem& Squad)
	{
		int32 Count = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const ULedgeFallComponent* Fall = Member ? Member->FindComponentByClass<ULedgeFallComponent>() : nullptr;
			Count += Fall ? Fall->GetRescueCount() : 0;
		}
		return Count;
	}

	void SetLinkCommit(bool bOn)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Fall.LinkCommit")))
		{
			Var->Set(bOn ? 1 : 0, ECVF_SetByConsole);
		}
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
		State.Summary.Add(FString::Printf(TEXT("%-4s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), *What.Left(1200)));
	}

	bool Finish(FState& State, bool bComplete)
	{
		for (const FString& Line : State.Summary)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: DropSoftlockSmoke: %d / %d legs passed, %d failures"), State.LegsOk, State.Legs.Num(), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("DropSoftlockSmoke"));
		return false;
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** On the navmesh where he stands (the AI's own start projection: a small box under his feet). */
	bool IsOnNav(UWorld* World, const AOperativeCharacter& Operative)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		const FVector Feet = Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		return Nav && Nav->ProjectPointToNavigation(Feet, Out, FVector(50.f, 50.f, 100.f)) && FMath::Abs(Out.Location.Z - Feet.Z) < 80.f;
	}

	FVector OnNav(UWorld* World, const FVector& P, const FVector& Extent = FVector(150.f, 150.f, 60.f))
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, Extent) ? Out.Location : P;
	}

	FString PathNote(UWorld* World, const FVector& From, const FVector& To, AActor* Context)
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Context);
		if (!Path || !Path->IsValid())
		{
			return TEXT("no navmesh path");
		}
		const FVector End = Path->PathPoints.Num() ? Path->PathPoints.Last() : From;
		return FString::Printf(TEXT("path %s, %d points, ends %.0f cm from the goal at z %.0f"), Path->IsPartial() ? TEXT("PARTIAL") : TEXT("complete"),
			Path->PathPoints.Num(), FVector::Dist2D(End, To), End.Z);
	}

	void Click(UWorld* World, const FVector& Goal)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!PC)
		{
			return;
		}
		FHitResult Hit;
		Hit.ImpactPoint = Goal;
		Hit.Location = Goal;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		PC->HandleWorldHit(Hit);
	}

	FString Describe(UWorld* World, USquadSubsystem& Squad, const FVector& Goal, const FState& State)
	{
		FString Out;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			const float* Still = State.StillFor.Find(Member);
			const ULedgeFallComponent* Fall = Member->FindComponentByClass<ULedgeFallComponent>();
			Out += FString::Printf(TEXT(" | %s%s at %s feet %.0f%s, speed %.0f, still %.1f s, fall state %d, landings %d, order %s -> %s, %s"), *Member->DisplayName.ToString(),
				Member == Squad.GetLeader() ? TEXT(" (leader)") : TEXT(""), *FVector2D(Member->GetActorLocation()).ToString(), FeetZ(*Member),
				IsOnNav(World, *Member) ? TEXT("") : TEXT(" OFF THE NAVMESH"), Member->GetVelocity().Size2D(), Still ? *Still : 0.f,
				Fall ? static_cast<int32>(Fall->GetState()) : -1, Fall ? Fall->GetLandingCount() : 0, Member->HasOwnMoveOrder() ? TEXT("own") : TEXT("none/formation"),
				*FVector2D(Member->GetLastMoveDestination()).ToString(), *PathNote(World, Member->GetActorLocation(), Goal, Member));
		}
		return Out;
	}

	/** The layout's roof / floor holes of the ruin: (x, y, upper level) - L_Gen_Depot_1's accepted seed 1 (gen_ref_01_02_depot_1.json). */
	struct FHole
	{
		FString Id;
		FVector2D XY;
		int32 Upper = 2;
	};

	TArray<FHole> RuinHoles()
	{
		return { { TEXT("hole_roof_0"), FVector2D(-2720.f, 1720.f), 2 }, { TEXT("hole_roof_1"), FVector2D(-3080.f, 2575.f), 2 },
			{ TEXT("hole_floor_0"), FVector2D(-3260.f, 1815.f), 1 } };
	}

	void PlanLegs(FState& State)
	{
		auto Wanted = [&State](const TCHAR* Key) { return State.StageFilter.IsEmpty() || State.StageFilter.Contains(Key); };
		if (Wanted(TEXT("user")))
		{
			FLeg Leg;
			Leg.Stage = EStage::User;
			Leg.Name = TEXT("user's case: roof (+2) -> click on the floor below (-2738, 2783, +1) -> click on the ground outside");
			Leg.Starts = { FVector(UserLeaderXY, 600.f), FVector(UserFollowerXY[0], 600.f), FVector(UserFollowerXY[1], 600.f) };
			Leg.Click1 = UserClick;
			Leg.Click2 = GroundClick;
			Leg.Limit1 = 30.f;
			Leg.Limit2 = 100.f;
			State.Legs.Add(Leg);
		}
		if (Wanted(TEXT("holes")))
		{
			for (const FHole& Hole : RuinHoles())
			{
				FLeg Leg;
				Leg.Stage = EStage::Holes;
				Leg.Name = FString::Printf(TEXT("%s: level +%d -> the slab below under the hole -> the ground outside"), *Hole.Id, Hole.Upper);
				const float Up = Hole.Upper * 300.f;
				// The squad 3-5 m off the hole on its upper slab (west of it: inside the ruin's footprint), the click right under the hole.
				Leg.Starts = { FVector(Hole.XY + FVector2D(0.f, -330.f), Up), FVector(Hole.XY + FVector2D(-150.f, -450.f), Up), FVector(Hole.XY + FVector2D(150.f, -450.f), Up) };
				Leg.Click1 = FVector(Hole.XY + FVector2D(60.f, 0.f), Up - 300.f);
				Leg.Click2 = GroundClick;
				Leg.Limit1 = 30.f;
				Leg.Limit2 = 110.f;
				State.Legs.Add(Leg);
			}
		}
		if (Wanted(TEXT("sweep")))
		{
			FLeg Leg;
			Leg.Stage = EStage::Sweep;
			Leg.Name = TEXT("sweep: every drop / hole link lands where the main navmesh is reachable");
			State.Legs.Add(Leg);
		}
		if (Wanted(TEXT("rescue")))
		{
			FLeg Perch;
			Perch.Stage = EStage::Rescue;
			Perch.Name = TEXT("rescue A: an operative left on a hole's rim off the navmesh after a fall is put back on the navmesh of his level");
			State.Legs.Add(Perch);
			// The user's case with the root fix off: the followers stop on the hole's rim off the navmesh (the soft-lock) - the next click must
			// still bring everybody to the ground (the failed order starts the rescue).
			FLeg Leg = State.Legs.IsEmpty() || State.Legs[0].Stage != EStage::User ? FLeg() : State.Legs[0];
			if (Leg.Starts.IsEmpty())
			{
				Leg.Starts = { FVector(UserLeaderXY, 600.f), FVector(UserFollowerXY[0], 600.f), FVector(UserFollowerXY[1], 600.f) };
				Leg.Click1 = UserClick;
				Leg.Click2 = GroundClick;
				Leg.Limit1 = 30.f;
				Leg.Limit2 = 100.f;
			}
			Leg.Stage = EStage::User;
			Leg.Name = TEXT("rescue B: the user's case with the root fix off (Codex.Fall.LinkCommit 0) - the safety net gets the squad out");
			Leg.bLinkCommitOff = true;
			Leg.bExpectRescue = true;
			State.Legs.Add(Leg);
		}
	}

	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FLeg& Leg)
	{
		TArray<AOperativeCharacter*> Members = Squad.GetMembers();
		Members.Remove(nullptr);
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			const FVector Want = Leg.Starts.IsValidIndex(Index) ? Leg.Starts[Index] : Leg.Starts.Last() + FVector(80.f * Index, 0.f, 0.f);
			const FVector At = OnNav(World, Want + FVector(0.f, 0.f, 20.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s placed at %s (asked %s)"), *Member->DisplayName.ToString(), *At.ToCompactString(), *Want.ToCompactString());
			Member->TeleportTo(At + FVector(0.f, 0.f, Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator::ZeroRotator, false, true);
			if (AAIController* AI = Cast<AAIController>(Member->GetController()))
			{
				AI->StopMovement();
			}
		}
	}

	/** Every member's standing-still clock (2 cm per step counts as moving). Returns true when one is still StillFailSeconds off his goal level. */
	bool UpdateStill(FState& State, USquadSubsystem& Squad, float Dt, float GoalZ, const FVector& Goal)
	{
		bool bStuck = false;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			const FVector Pos = Member->GetActorLocation();
			const FVector* Last = State.LastPos.Find(Member);
			float& Still = State.StillFor.FindOrAdd(Member);
			Still = Last && FVector::Dist(*Last, Pos) < 2.f ? Still + Dt : 0.f;
			State.LastPos.Add(Member, Pos);
			const bool bThere = FMath::Abs(FeetZ(*Member) - GoalZ) <= LevelTolCm && FVector::Dist2D(Pos, Goal) <= 700.f;
			bStuck |= !bThere && Still >= StillFailSeconds;
		}
		return bStuck;
	}

	void Trace(FState& State, UWorld* World, USquadSubsystem& Squad, float Dt)
	{
		State.TraceClock += Dt;
		if (!State.bTrace || State.TraceClock < 0.5f)
		{
			return;
		}
		State.TraceClock = 0.f;
		FString Line;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member)
			{
				const ULedgeFallComponent* Fall = Member->FindComponentByClass<ULedgeFallComponent>();
				const AAIController* AI = Cast<AAIController>(Member->GetController());
				const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
				const bool bPath = Following && Following->GetPath().IsValid() && Following->GetPath()->IsValid();
				Line += FString::Printf(TEXT(" | %s%s (%.0f, %.0f) feet %.0f v %.0f mode %d walkoff %d fall %d%s%s path %d seg %d/%d -> %s"), *Member->DisplayName.ToString(),
					Member == Squad.GetLeader() ? TEXT("*") : TEXT(""), Member->GetActorLocation().X, Member->GetActorLocation().Y, FeetZ(*Member), Member->GetVelocity().Size2D(),
					static_cast<int32>(Member->GetCharacterMovement()->MovementMode.GetValue()), Member->GetCharacterMovement()->CanWalkOffLedges() ? 1 : 0,
					Fall ? static_cast<int32>(Fall->GetState()) : -1, IsOnNav(World, *Member) ? TEXT("") : TEXT(" OFFNAV"), Member->HasOwnMoveOrder() ? TEXT(" own") : TEXT(""),
					Following ? static_cast<int32>(Following->GetStatus()) : -1, Following ? Following->GetCurrentPathIndex() : -1,
					bPath ? Following->GetPath()->GetPathPoints().Num() : 0,
					bPath ? *Following->GetCurrentTargetLocation().ToCompactString() : TEXT("-"));
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s:%s"), State.PhaseTime, *Line);
		// The running paths point by point (the link points flagged) - the diagnosis of a walk over a ledge.
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const AAIController* AI = Member ? Cast<AAIController>(Member->GetController()) : nullptr;
			const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
			if (!Following || !Following->GetPath().IsValid() || !Following->GetPath()->IsValid())
			{
				continue;
			}
			FString Points;
			for (const FNavPathPoint& Point : Following->GetPath()->GetPathPoints())
			{
				Points += FString::Printf(TEXT(" (%.0f, %.0f, %.0f)%s"), Point.Location.X, Point.Location.Y, Point.Location.Z,
					Point.CustomNavLinkId.IsValid() ? TEXT("[custom link]") : FNavMeshNodeFlags(Point.Flags).IsNavLink() ? TEXT("[link]") : TEXT(""));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace path %s:%s"), *Member->DisplayName.ToString(), *Points);
		}
	}

	/** The pictures' camera: high over the ruin's roof south-west of hole_roof_1, looking at the hole (the roof, the hole's rim and the stair). */
	void AimCamera(FState& State, UWorld* World)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (!State.bShots || !PC)
		{
			return;
		}
		const FVector Look(-3150.f, 2450.f, 380.f);
		const FVector Eye = Look + FVector(-1050.f, -900.f, 1250.f);
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation());
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(70.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	/** A picture of this moment (once per name): the camera now, the screenshot a few frames later. */
	void Shoot(FState& State, UWorld* World, const FString& Name)
	{
		if (!State.bShots || State.ShotsTaken.Contains(Name) || !State.PendingShot.IsEmpty())
		{
			return;
		}
		State.ShotsTaken.Add(Name);
		AimCamera(State, World);
		State.PendingShot = Name;
		State.PendingShotFrames = 3;
	}

	void TickShots(FState& State, UWorld* World)
	{
		if (State.PendingShot.IsEmpty())
		{
			return;
		}
		AimCamera(State, World);
		if (--State.PendingShotFrames <= 0)
		{
			const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/DropSoftlock") / State.PendingShot + TEXT(".png");
			FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot: %s"), *File);
			State.PendingShot.Reset();
		}
	}

	void EndLeg(FState& State, bool bOk, const FString& What)
	{
		const FLeg& Leg = State.Legs[State.Leg];
		Check(State, bOk, FString::Printf(TEXT("%s: %s"), *Leg.Name, *What));
		State.LegsOk += bOk ? 1 : 0;
		++State.Leg;
		State.Phase = EPhase::Setup;
	}

	/** The sweep: every drop link and hole link's landing reaches the main navmesh. */
	void RunSweep(FState& State, UWorld* World)
	{
		for (const FVector& Probe : State.Probes)
		{
			// Diagnosis: the pathfinder's way from a point to the player start, point by point (links flagged).
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Probe, It->GetActorLocation());
				FString Points;
				if (Path && Path->GetPath().IsValid())
				{
					for (const FNavPathPoint& Point : Path->GetPath()->GetPathPoints())
					{
						Points += FString::Printf(TEXT(" (%.0f, %.0f, %.0f)%s"), Point.Location.X, Point.Location.Y, Point.Location.Z,
							Point.CustomNavLinkId.IsValid() ? TEXT("[custom link]") : FNavMeshNodeFlags(Point.Flags).IsNavLink() ? TEXT("[link]") : TEXT(""));
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke probe %s -> start: %s%s"), *Probe.ToCompactString(),
					!Path || !Path->IsValid() ? TEXT("no path") : Path->IsPartial() ? TEXT("PARTIAL") : TEXT("complete"), *Points);
				break;
			}
		}
		if (State.bRegen)
		{
			if (UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>())
			{
				Links->Generate(); // diagnosis: the generation's dead-end verdicts on the settled navmesh (logged by [DropLinks])
			}
		}
		const DropReachSweep::FResult Result = DropReachSweep::Run(*World);
		EndLeg(State, Result.bBuilt && Result.Bad == 0 && Result.DropLinks + Result.HoleLinks > 0, Result.Line);
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World || State.Phase == EPhase::Done)
		{
			return false;
		}
		const float Dt = World->IsPaused() ? 0.f : StepSeconds;
		State.Time += Dt;
		State.PhaseTime += Dt;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		TickShots(State, World);
		if (State.Time > TotalLimitSeconds)
		{
			Check(State, false, TEXT("the smoke ran out of time"));
			return Finish(State, false);
		}
		switch (State.Phase)
		{
		case EPhase::Settle:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
			if (!Leader || State.PhaseTime < 3.5f || (Links && !Links->HasGenerated() && State.PhaseTime < 60.f)
				|| (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.PhaseTime < 15.f))
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			PlanLegs(State);
			Check(State, State.Legs.Num() > 0 && Squad->GetMembers().Num() >= 3 && Links && Links->HasGenerated(),
				FString::Printf(TEXT("%d legs planned, %d operatives, %d drop links"), State.Legs.Num(), Squad->GetMembers().Num(), Links ? Links->GetLinks().Num() : -1));
			State.Phase = EPhase::Setup;
			return true;
		}
		case EPhase::Setup:
		{
			if (!State.PendingShot.IsEmpty())
			{
				return true; // the last leg's picture is still being taken
			}
			if (State.Leg >= State.Legs.Num())
			{
				return Finish(State, true);
			}
			const FLeg& Leg = State.Legs[State.Leg];
			State.PhaseTime = 0.f;
			State.DoneFor = 0.f;
			State.StillFor.Reset();
			State.LastPos.Reset();
			if (Leg.Stage == EStage::Sweep)
			{
				State.Phase = EPhase::Sweep;
				return true;
			}
			if (Leg.Stage == EStage::Rescue)
			{
				// The Engineer alone on the rim (the others far off on the ground), his fall watch armed as after a landing.
				SetLinkCommit(true);
				State.RescuesAtStart = CountRescues(*Squad);
				if (!Squad->IsSoloMode())
				{
					Squad->EnterSoloMode();
				}
				Squad->SetFollowersHolding(true);
				AOperativeCharacter* Who = Squad->GetMembers().Num() > 1 ? Squad->GetMembers()[1] : nullptr;
				State.RescueWho = Who;
				ULedgeFallComponent* Fall = Who ? Who->FindComponentByClass<ULedgeFallComponent>() : nullptr;
				if (!Who || !Fall)
				{
					EndLeg(State, false, TEXT("no operative / fall component for the rescue"));
					return true;
				}
				if (AAIController* AI = Cast<AAIController>(Who->GetController()))
				{
					AI->StopMovement();
				}
				Who->TeleportTo(PerchFeet + FVector(0.f, 0.f, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f), FRotator(0.f, 0.f, 0.f), false, true);
				Who->GetCharacterMovement()->StopMovementImmediately();
				Fall->ArmRescueWatch();
				Shoot(State, World, TEXT("rescueA_engineer_on_the_rim"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s put on the rim of hole_roof_1 at %s (on the navmesh: %d), the fall watch armed"), *Who->DisplayName.ToString(),
					*PerchFeet.ToCompactString(), IsOnNav(World, *Who) ? 1 : 0);
				State.Phase = EPhase::Rescue;
				return true;
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(false);
			SetLinkCommit(!Leg.bLinkCommitOff);
			State.RescuesAtStart = CountRescues(*Squad);
			PlaceSquad(World, *Squad, Leg);
			Squad->SetLeader(Squad->GetMembers()[0]);
			Squad->SetSelectedGroup(TArray<AOperativeCharacter*>{ Squad->GetLeader() }, false);
			State.LandingsAtStart.Reset();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (const ULedgeFallComponent* Fall = Member ? Member->FindComponentByClass<ULedgeFallComponent>() : nullptr)
				{
					State.LandingsAtStart.Add(Member, Fall->GetLandingCount());
				}
			}
			State.Phase = EPhase::Click1;
			return true;
		}
		case EPhase::Click1:
		{
			if (State.PhaseTime < 1.5f)
			{
				return true; // the teleport settles
			}
			const FLeg& Leg = State.Legs[State.Leg];
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %d / %d - %s: %s clicks %s; paths:%s"), State.Leg + 1, State.Legs.Num(), *Leg.Name,
				*Leader->DisplayName.ToString(), *Leg.Click1.ToCompactString(), *Describe(World, *Squad, Leg.Click1, State));
			Click(World, Leg.Click1);
			State.PhaseTime = 0.f;
			State.Phase = EPhase::Run1;
			return true;
		}
		case EPhase::Run1:
		{
			const FLeg& Leg = State.Legs[State.Leg];
			Trace(State, World, *Squad, Dt);
			const TCHAR* LegKey = Leg.bExpectRescue ? TEXT("rescueB") : State.Leg == 0 ? TEXT("user") : nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				// A follower in the air in the hole (the root fix: he drops instead of stopping on the rim).
				if (LegKey && Member && Member != Leader && Member->GetCharacterMovement()->IsFalling() && FeetZ(*Member) < 560.f && FeetZ(*Member) > 380.f)
				{
					Shoot(State, World, FString::Printf(TEXT("%s_follower_drops"), LegKey));
				}
			}
			// The squad drops through the hole and gathers below; the second click comes when the leader is down and the squad is still (or
			// the first leg's time is up - the user clicked on after ~14 s).
			const bool bLeaderDown = Leader && FMath::Abs(FeetZ(*Leader) - Leg.Click1.Z) <= LevelTolCm;
			UpdateStill(State, *Squad, Dt, Leg.Click1.Z, Leg.Click1);
			bool bAllStill = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float* Still = Member ? State.StillFor.Find(Member) : nullptr;
				bAllStill &= Still && *Still >= 2.f;
			}
			if ((bLeaderDown && bAllStill && State.PhaseTime > 6.f) || State.PhaseTime > Leg.Limit1)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: after the first click (%.1f s):%s"), State.PhaseTime, *Describe(World, *Squad, Leg.Click2, State));
				if (Leg.bExpectRescue)
				{
					Shoot(State, World, TEXT("rescueB_followers_on_the_rim")); // the soft-lock itself (the root fix off)
				}
				State.PhaseTime = 0.f;
				State.Phase = EPhase::Click2;
			}
			return true;
		}
		case EPhase::Click2:
		{
			const FLeg& Leg = State.Legs[State.Leg];
			Click(World, Leg.Click2);
			State.StillFor.Reset();
			State.LastPos.Reset();
			State.PhaseTime = 0.f;
			State.Phase = EPhase::Run2;
			return true;
		}
		case EPhase::Run2:
		{
			const FLeg& Leg = State.Legs[State.Leg];
			Trace(State, World, *Squad, Dt);
			const bool bStuck = UpdateStill(State, *Squad, Dt, Leg.Click2.Z, Leg.Click2);
			if (Leg.bExpectRescue && CountRescues(*Squad) > State.RescuesAtStart)
			{
				Shoot(State, World, TEXT("rescueB_after_the_rescue"));
			}
			bool bAllThere = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAllThere &= Member && FMath::Abs(FeetZ(*Member) - Leg.Click2.Z) <= LevelTolCm && FVector::Dist2D(Member->GetActorLocation(), Leg.Click2) <= 700.f;
			}
			State.DoneFor = bAllThere ? State.DoneFor + Dt : 0.f;
			if (State.DoneFor >= 1.f)
			{
				FString Drops;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					const ULedgeFallComponent* Fall = Member ? Member->FindComponentByClass<ULedgeFallComponent>() : nullptr;
					const int32* At = State.LandingsAtStart.Find(Member);
					Drops += FString::Printf(TEXT(" %s %d"), Member ? *Member->DisplayName.ToString() : TEXT("?"), Fall && At ? Fall->GetLandingCount() - *At : -1);
				}
				const int32 Rescues = CountRescues(*Squad) - State.RescuesAtStart;
				const bool bRescueOk = Leg.bExpectRescue ? Rescues > 0 : Rescues == 0;
				if (State.Leg == 0)
				{
					Shoot(State, World, TEXT("user_squad_on_the_ground"));
				}
				SetLinkCommit(true);
				EndLeg(State, bRescueOk, FString::Printf(TEXT("all on the ground at the stair in %.1f s (landings:%s; rescues %d - %s):%s"), State.PhaseTime, *Drops, Rescues,
					Leg.bExpectRescue ? TEXT("the safety net must act") : TEXT("the root fix alone, no rescue wanted"), *Describe(World, *Squad, Leg.Click2, State)));
				return true;
			}
			if (bStuck || State.PhaseTime > Leg.Limit2)
			{
				SetLinkCommit(true);
				EndLeg(State, false, FString::Printf(TEXT("%s after %.0f s:%s"), bStuck ? TEXT("STUCK") : TEXT("TIMED OUT"), State.PhaseTime,
					*Describe(World, *Squad, Leg.Click2, State)));
			}
			return true;
		}
		case EPhase::Sweep:
			RunSweep(State, World);
			return true;
		case EPhase::Rescue:
		{
			AOperativeCharacter* Who = State.RescueWho.Get();
			const ULedgeFallComponent* Fall = Who ? Who->FindComponentByClass<ULedgeFallComponent>() : nullptr;
			const int32 Rescues = CountRescues(*Squad) - State.RescuesAtStart;
			const bool bBack = Who && Rescues > 0 && IsOnNav(World, *Who) && FMath::Abs(FeetZ(*Who) - PerchFeet.Z) <= 60.f;
			if (bBack && State.PhaseTime < 6.f)
			{
				Shoot(State, World, TEXT("rescueA_back_on_the_navmesh"));
			}
			if ((bBack && State.PhaseTime > 1.5f && State.ShotsTaken.Contains(TEXT("rescueA_back_on_the_navmesh")) == State.bShots && State.PendingShot.IsEmpty())
				|| State.PhaseTime > 8.f)
			{
				const float Moved = Fall ? static_cast<float>(FVector::Dist(Fall->GetLastRescueFrom(), Fall->GetLastRescueTo())) : -1.f;
				const bool bOk = bBack && Moved <= 200.f;
				Squad->ExitSoloMode();
				Squad->SetFollowersHolding(false);
				EndLeg(State, bOk, FString::Printf(TEXT("%s after %.1f s: %d rescue(s), moved %.0f cm (%s -> %s), now on the navmesh %d at feet %.0f (the rim's level %.0f)"),
					bBack ? TEXT("back on the navmesh") : TEXT("NOT RESCUED"), State.PhaseTime, Rescues, Moved,
					Fall ? *Fall->GetLastRescueFrom().ToCompactString() : TEXT("-"), Fall ? *Fall->GetLastRescueTo().ToCompactString() : TEXT("-"),
					Who && IsOnNav(World, *Who) ? 1 : 0, Who ? FeetZ(*Who) : 0.f, PerchFeet.Z));
			}
			return true;
		}
		default:
			return false;
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
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("stages=")))
			{
				TArray<FString> Parts;
				Arg.Mid(7).Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Parts, TEXT(",")); // "+" too: -ExecCmds splits on commas
				State->StageFilter.Append(Parts);
			}
			State->bTrace |= Arg == TEXT("trace");
			State->bRegen |= Arg == TEXT("regen");
			State->bShots |= Arg == TEXT("shots");
			if (Arg.StartsWith(TEXT("probe=")))
			{
				TArray<FString> Parts;
				Arg.Mid(6).ParseIntoArray(Parts, TEXT("_"));
				if (Parts.Num() == 3)
				{
					State->Probes.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
				}
			}
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DropSoftlockSmoke"),
		TEXT("Dev check (user bug 2026-11-03, the soft-lock after a roof-hole drop on L_Gen_Depot_1): the user's case, every ruin hole, the drop / hole link ")
		TEXT("reach sweep and the off-navmesh rescue; [stages=user,holes,sweep,rescue] [trace]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
