// Dev-only console command for a headless check on L_Gen_Depot_1 (the obstacles reworked onto the height classes, mantling slice 1):
//   Scripts/smoke_parallel.ps1 -Commands MantleSmoke
// docs/design/MANTLING.md 11.5 / 11.6, docs/port/MANTLING_SLICE1_REPORT.md. One operative in Solo, the Depot's own obstacles:
//   a  85 cm crate: the standing low mantle, on top in a crouch (warped landing on the planned spot), then a move order hops him down;
//   b 140 cm wreck: the high mantle on top;
//   c the cut's 230 cm parapet from the ground (the "canal" climb): the tall ledge on top;
//   d a 135 cm fence: over it, landing on the far side's ground;
//   d2 R17-2 (designer 2026-11-17): in stealth the 140 cm wreck and the 135 cm fence again - stealth (crouched) is still on after landing;
//   e a sandbag: no mantle - the vault still takes it; e2 (R17-2) the same vault in stealth keeps stealth;
//   f no run-up: sprinting right at a wreck -> he stops, the standing clip;
//   g a sprint from far off with a click on the wreck's top: the run clip;
//   h a 330 cm fence: refused (a wall); a beam over a crate's top: refused (no headroom);
//   i a knockdown mid-climb (the tall ledge): he drops back to the base, the state restored exactly once, he gets up;
//   j a path order across an obstacle with a mantle link: he mantles by himself on the way and arrives;
//   k the same order in formation: no mantle (the formation path never takes the link).
// Enemies are parked far away; the Depot JSON keeps the squad immortal.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/VaultNavigation.h"
#include "NavigationSystem.h"

namespace MantleSmoke
{
	constexpr float StepSeconds = 0.05f;

	/** One test site: stand at Feet facing Dir (into the obstacle). */
	struct FSite
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Feet = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FMantleSite Probe;
		FMantleDecision Decision;
		bool bFound = false;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Restores = 0;
		int32 Count = 0;
		int32 StealthDropFrames = 0;
		bool bSawMantle = false;
		FSite Site;
		FVector Goal = FVector::ZeroVector;
		TWeakObjectPtr<AActor> Beam;
		int32 LinkIndex = INDEX_NONE;
		TArray<int32> SquadCounts;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bSawMantle = false;
	}

	bool HasKind(const AActor& Actor, const TCHAR* Kind)
	{
		return Actor.Tags.Contains(FName(Kind));
	}

	float TopOf(const AActor& Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor.GetActorBounds(false, Origin, Extent);
		return static_cast<float>(Origin.Z + Extent.Z);
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/**
	 * A site at an obstacle of Kind about HeightCm tall (+-6) whose face has free ground StandOffCm out, on the navmesh, and where the
	 * probe from there measures this obstacle; Accept(decision) picks the outcome the case needs. Nearest to Near first.
	 */
	FSite FindSite(UWorld* World, AOperativeCharacter* Leader, const TCHAR* Kind, float HeightCm, float StandOffCm, const FVector& Near,
		TFunctionRef<bool(const FMantleSite&, const FMantleDecision&)> Accept)
	{
		FSite Best;
		float BestDist = TNumericLimits<float>::Max();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float Radius = Leader->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FMantleConfig& Config = MantleRules::GetConfig();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MantleSmoke), false, Leader);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!HasKind(**It, Kind))
			{
				continue;
			}
			const FBox Local = It->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FTransform Transform = It->GetActorTransform();
			const float Bottom = static_cast<float>(Transform.TransformPosition(FVector(Local.GetCenter().X, Local.GetCenter().Y, Local.Min.Z)).Z);
			if (FMath::Abs(TopOf(**It) - Bottom - HeightCm) > 6.f)
			{
				continue;
			}
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const int32 Axis = Face % 2;
				const float Sign = Face < 2 ? 1.f : -1.f;
				FVector LocalFace = Local.GetCenter();
				LocalFace[Axis] = Sign > 0.f ? Local.Max[Axis] : Local.Min[Axis];
				FVector LocalNormal = FVector::ZeroVector;
				LocalNormal[Axis] = Sign;
				const int32 TangentAxis = 1 - Axis;
				if ((Local.Max[TangentAxis] - Local.Min[TangentAxis]) * Transform.GetScale3D().GetAbs()[TangentAxis] < 2.f * Radius + 20.f)
				{
					continue; // an end face (a fence's 10 cm end): no square climb there
				}
				const FVector Normal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal2D();
				FVector FaceWorld = Transform.TransformPosition(LocalFace);
				FaceWorld.Z = Bottom;
				const FVector Spot = FaceWorld + Normal * StandOffCm;
				const float Dist = static_cast<float>(FVector::Dist2D(Spot, Near));
				if (Dist >= BestDist)
				{
					continue;
				}
				FNavLocation OnNav;
				if (!Nav || !Nav->ProjectPointToNavigation(Spot, OnNav, FVector(50.f, 50.f, 100.f)) || FMath::Abs(OnNav.Location.Z - Bottom) > 30.f
					|| World->OverlapAnyTestByChannel(Spot + FVector(0.f, 0.f, HalfHeight + 6.f), FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
				{
					continue;
				}
				FMantleSite Probe;
				if (!MantleNavigation::ProbeSite(*World, Spot, -Normal, StandOffCm + 100.f, Radius, HalfHeight, Params, Config, Probe)
					|| Probe.Obstacle.Get() != *It)
				{
					continue;
				}
				const FMantleDecision Decision = MantleRules::Decide(Config, Probe.Probe);
				if (!Accept(Probe, Decision))
				{
					continue;
				}
				BestDist = Dist;
				Best.Actor = *It;
				Best.Feet = Spot;
				Best.Dir = -Normal;
				Best.Probe = Probe;
				Best.Decision = Decision;
				Best.bFound = true;
			}
		}
		return Best;
	}

	void PlaceAt(AOperativeCharacter* Leader, const FVector& Feet, const FVector& Dir)
	{
		Leader->StopOperative();
		Leader->SetStance(EOperativeStance::Standing);
		Leader->TeleportTo(Feet + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), Dir.Rotation(), false, true);
	}

	bool IsGo(const FMantleDecision& Decision) { return Decision.IsGo(); }

	/**
	 * Climb case: place, TryStartMantle, wait out the clip, check the landing. Returns true when the case is over.
	 * bStealth (R17-2): he enters stealth at the site (crouched) and must still be in it, crouched, after landing; stealth is left again after the check.
	 */
	bool ClimbCase(FState& State, const TCHAR* What, FName WantClass, bool bOnTop, bool bStealth = false)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		if (State.StageTime < StepSeconds * 1.5f)
		{
			if (!State.Site.bFound)
			{
				Check(State, false, FString::Printf(TEXT("%s: a test site on the map"), What));
				return true;
			}
			PlaceAt(Leader, State.Site.Feet, State.Site.Dir);
			State.Restores = Leader->GetMantleRestoreCount();
			State.Count = Leader->GetMantleCount();
			State.StealthDropFrames = 0;
			if (bStealth)
			{
				Check(State, Leader->SetStealthMode(true) && Leader->IsInStealthMode(), FString::Printf(TEXT("%s: stealth on at the site (Solo)"), What));
			}
			return false;
		}
		// Stealth: the crouch entry clip settles before the climb.
		if (State.StageTime < (bStealth ? 1.5f : 0.6f))
		{
			return false;
		}
		if (bStealth && !Leader->IsInStealthMode())
		{
			++State.StealthDropFrames;
		}
		if (Leader->GetMantleCount() == State.Count)
		{
			EMantleRefusal Refusal = EMantleRefusal::None;
			const bool bStarted = Leader->TryStartMantle(State.Site.Dir, &Refusal, true);
			Check(State, bStarted && Leader->IsMantling(), FString::Printf(TEXT("%s: the climb starts (%s, %s)"), What, *GetNameSafe(State.Site.Actor.Get()),
				bStarted ? TEXT("go") : MantleRules::RefusalName(Refusal)));
			if (!bStarted)
			{
				return true;
			}
			Check(State, Leader->GetLastMantleClass() == WantClass, FString::Printf(TEXT("%s: class %s (want %s), clip %s"), What,
				*Leader->GetLastMantleClass().ToString(), *WantClass.ToString(), *Leader->GetLastMantleClip()));
			Check(State, !Leader->GetActorEnableCollision() && Leader->GetCharacterMovement()->MovementMode == MOVE_Flying,
				FString::Printf(TEXT("%s: flown with the collision off during the climb"), What));
			return false;
		}
		State.bSawMantle |= Leader->IsMantling();
		if (Leader->IsMantling() && State.StageTime < 12.f)
		{
			return false;
		}
		const FVector Landing = Leader->GetMantleLandingFeet();
		const float Error = static_cast<float>(FVector::Dist(Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), Landing));
		Check(State, !Leader->IsMantling(), FString::Printf(TEXT("%s: the climb ended (%.1f s)"), What, State.StageTime));
		Check(State, Leader->GetLastMantleLandingError() < 35.f, FString::Printf(TEXT("%s: the warped clip ended %.0f cm off the planned landing"), What,
			Leader->GetLastMantleLandingError()));
		Check(State, Error < 15.f, FString::Printf(TEXT("%s: standing on the landing (%.0f cm off)"), What, Error));
		Check(State, Leader->GetMantleRestoreCount() == State.Restores + 1, FString::Printf(TEXT("%s: the state restored exactly once (%d)"), What,
			Leader->GetMantleRestoreCount() - State.Restores));
		Check(State, Leader->GetActorEnableCollision() && Leader->GetCharacterMovement()->MovementMode != MOVE_Flying
			&& Leader->WeaponMesh && Leader->WeaponMesh->IsVisible(), FString::Printf(TEXT("%s: collision, walking and the weapon back"), What));
		if (bStealth)
		{
			Check(State, State.StealthDropFrames == 0 && Leader->IsInStealthMode() && Leader->GetStance() == EOperativeStance::Crouching,
				FString::Printf(TEXT("%s: still in stealth, crouched, after landing (stealth off for %d frames, stance %d)"), What, State.StealthDropFrames,
					static_cast<int32>(Leader->GetStance())));
			Leader->SetStealthMode(false);
		}
		if (bOnTop)
		{
			Check(State, FMath::Abs(FeetZ(*Leader) - State.Site.Probe.TopZ) < 12.f, FString::Printf(TEXT("%s: on top (feet %.0f, top %.0f)"), What, FeetZ(*Leader),
				State.Site.Probe.TopZ));
			Check(State, Leader->GetStance() == EOperativeStance::Crouching, FString::Printf(TEXT("%s: crouching on top"), What));
		}
		else
		{
			const float Past = static_cast<float>(FVector::DotProduct(Leader->GetActorLocation() - State.Site.Probe.FacePoint, State.Site.Dir));
			Check(State, Past > State.Site.Probe.Probe.DepthCm + 30.f && FMath::Abs(FeetZ(*Leader) - State.Site.Feet.Z) < 20.f,
				FString::Printf(TEXT("%s: on the far side's ground (%.0f cm past the face, feet %.0f / %.0f)"), What, Past, FeetZ(*Leader), State.Site.Feet.Z));
		}
		return true;
	}

	bool Step(TSharedRef<FState> State)
	{
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		if (State->Time > 420.f)
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
		AOperativeCharacter* Leader = State->Leader.Get();
		const FName LowMantle(TEXT("low_mantle"));
		const FName HighMantle(TEXT("high_mantle"));
		const FName TallLedge(TEXT("tall_ledge"));
		const FName FenceOver(TEXT("high_fence_over"));
		auto Near = [Leader]() { return Leader ? Leader->GetActorLocation() : FVector::ZeroVector; };
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Leader->bTacticalCeaseFire = true;
			Check(*State, Mantle->HasGenerated(), FString::Printf(TEXT("mantle links generated: %d (%d samples, %.0f ms)"), Mantle->GetLinks().Num(),
				Mantle->GetSampledCount(), Mantle->GetGenerationMs()));
			Check(*State, Leader->MotionWarping != nullptr, TEXT("the operative has a Motion Warping component"));
			State->Site = FindSite(World, Leader, TEXT("crate"), 85.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
			Next(*State, 1);
			return true;
		}
		case 1: // a: the 85 cm crate
			if (ClimbCase(*State, TEXT("a 85 cm crate"), LowMantle, true))
			{
				// Down again by a move order (a hop down off the low top, or its down link).
				State->Goal = State->Site.Feet - State->Site.Dir * 250.f;
				Leader->OrderMoveTo(State->Goal, false);
				Next(*State, 2);
			}
			return true;
		case 2:
		{
			const bool bDown = FMath::Abs(FeetZ(*Leader) - State->Site.Feet.Z) < 20.f && !Leader->IsVaulting();
			if (!bDown && State->StageTime < 10.f)
			{
				return true;
			}
			Check(*State, bDown, FString::Printf(TEXT("a move order from the crate's top brings him down (feet %.0f, ground %.0f, %.1f s)"), FeetZ(*Leader),
				State->Site.Feet.Z, State->StageTime));
			State->Site = FindSite(World, Leader, TEXT("wreck"), 140.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
			Next(*State, 3);
			return true;
		}
		case 3: // b: the 140 cm wreck
			if (ClimbCase(*State, TEXT("a 140 cm wreck"), HighMantle, true))
			{
				State->Site = FindSite(World, Leader, TEXT("cut_wall"), 230.f, 60.f, Near(),
					[](const FMantleSite& P, const FMantleDecision& D) { return D.IsGo() && P.Probe.HeightCm > 220.f; });
				Next(*State, 4);
			}
			return true;
		case 4: // c: the cut's 230 cm parapet
			if (ClimbCase(*State, TEXT("the cut's 230 cm parapet (the canal climb)"), TallLedge, true))
			{
				State->Site = FindSite(World, Leader, TEXT("fence"), 135.f, 110.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
				Next(*State, 5);
			}
			return true;
		case 5: // d: the 135 cm fence
			if (ClimbCase(*State, TEXT("a 135 cm fence"), FenceOver, false))
			{
				State->Site = FindSite(World, Leader, TEXT("wreck"), 140.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
				Next(*State, 51);
			}
			return true;
		case 51: // d2 (R17-2): the 140 cm wreck in stealth
			if (ClimbCase(*State, TEXT("stealth: a 140 cm wreck"), HighMantle, true, true))
			{
				State->Site = FindSite(World, Leader, TEXT("fence"), 135.f, 110.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
				Next(*State, 52);
			}
			return true;
		case 52: // d2 (R17-2): the 135 cm fence in stealth
			if (ClimbCase(*State, TEXT("stealth: a 135 cm fence"), FenceOver, false, true))
			{
				Next(*State, 6);
			}
			return true;
		case 6: // e: a sandbag - the vault, not a mantle
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				State->Site = FindSite(World, Leader, TEXT("sandbag"), 95.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision&) { return true; });
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
				}
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			EMantleRefusal Refusal = EMantleRefusal::None;
			Check(*State, State->Site.bFound, TEXT("a sandbag on the map"));
			Check(*State, State->Site.bFound && !Leader->TryStartMantle(State->Site.Dir, &Refusal, true),
				FString::Printf(TEXT("a sandbag is no mantle (%s)"), MantleRules::RefusalName(Refusal)));
			Check(*State, State->Site.bFound && (Leader->IsVaulting() || Leader->TryVault(State->Site.Dir, true)), TEXT("... the vault takes it"));
			Next(*State, 60);
			return true;
		}
		case 60: // the vault over the sandbag ends first
			if (Leader->IsVaulting() && State->StageTime < 5.f)
			{
				return true;
			}
			Next(*State, 61);
			return true;
		case 61: // e2 (R17-2): the sandbag vault again, in stealth
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				State->Site = FindSite(World, Leader, TEXT("sandbag"), 95.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision&) { return true; });
				State->StealthDropFrames = 0;
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
					Check(*State, Leader->SetStealthMode(true) && Leader->IsInStealthMode(), TEXT("stealth: on at the sandbag (Solo)"));
				}
				return true;
			}
			if (State->StageTime < 1.5f) // the crouch entry clip settles first
			{
				return true;
			}
			Check(*State, State->Site.bFound && (Leader->IsVaulting() || Leader->TryVault(State->Site.Dir, true)), TEXT("stealth: the vault takes the sandbag"));
			Next(*State, 62);
			return true;
		}
		case 62: // the stealth vault ends; stealth (crouched) still on
			if (State->Site.bFound)
			{
				State->StealthDropFrames += Leader->IsInStealthMode() ? 0 : 1;
				if (Leader->IsVaulting() && State->StageTime < 5.f)
				{
					return true;
				}
				Check(*State, State->StealthDropFrames == 0 && Leader->IsInStealthMode() && Leader->GetStance() == EOperativeStance::Crouching,
					FString::Printf(TEXT("stealth: still in stealth, crouched, after the sandbag vault (stealth off for %d frames)"), State->StealthDropFrames));
			}
			Leader->SetStealthMode(false);
			Next(*State, 7);
			return true;
		case 7: // f: sprinting at a wreck, the click only 2 m off its face - no run-up for the run clip: he stops, the standing clip
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				State->Site = FindSite(World, Leader, TEXT("wreck"), 140.f, 340.f, Near(), [World](const FMantleSite& P, const FMantleDecision& D)
				{
					FHitResult Hit;
					return D.IsGo() && !World->SweepSingleByChannel(Hit, P.Feet + FVector(0.f, 0.f, 100.f), P.FacePoint - P.Dir * 90.f + FVector(0.f, 0.f, 100.f),
						FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(45.f));
				});
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
				}
				State->Count = Leader->GetMantleCount();
				State->Goal = FVector::ZeroVector;
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			if (!State->Site.bFound)
			{
				Check(*State, false, TEXT("no run-up: a wreck with 3.4 m of free ground in front"));
				Next(*State, 8);
				return true;
			}
			const float FaceOff = static_cast<float>(FVector::DotProduct(Leader->GetActorLocation() - State->Site.Probe.FacePoint, -State->Site.Dir));
			if (State->Goal.IsZero())
			{
				State->Goal = State->Site.Probe.FacePoint - State->Site.Dir * 60.f;
				Check(*State, Leader->OrderMoveTo(State->Goal, true) == EOperativeOrderResult::Accepted, TEXT("no run-up: a sprint towards the wreck"));
				return true;
			}
			if (Leader->GetMantleCount() == State->Count && !Leader->HasMantleOrder() && !State->bSawMantle)
			{
				if (FaceOff > 200.f && State->StageTime < 6.f)
				{
					return true;
				}
				FHitResult Top;
				const FVector TopPoint = State->Site.Probe.EdgeTop + State->Site.Dir * 40.f;
				World->LineTraceSingleByChannel(Top, TopPoint + FVector(0.f, 0.f, 60.f), TopPoint - FVector(0.f, 0.f, 40.f), ECC_Visibility);
				FString Reason;
				const bool bOrdered = Top.bBlockingHit && Leader->OrderMantleTop(Top, Leader->IsSprinting(), &Reason);
				Check(*State, bOrdered, FString::Printf(TEXT("no run-up: the click on the wreck's top %.0f cm off its face while running (%s)"), FaceOff,
					bOrdered ? TEXT("ok") : *Reason));
				State->bSawMantle = true; // the order is given
				if (!bOrdered)
				{
					Next(*State, 8);
				}
				return true;
			}
			if ((Leader->IsMantling() || Leader->HasMantleOrder()) && State->StageTime < 12.f)
			{
				return true;
			}
			Check(*State, Leader->GetMantleCount() > State->Count, TEXT("no run-up: he climbed"));
			Check(*State, Leader->GetLastMantleGait() == EMantleGait::Standing && Leader->DidLastMantleStopFirst(),
				FString::Printf(TEXT("no run-up: he stopped and the standing clip played (%s, stop %d)"), *Leader->GetLastMantleClip(),
					Leader->DidLastMantleStopFirst() ? 1 : 0));
			Leader->SetSprinting(false);
			Next(*State, 8);
			return true;
		}
		case 8: // g: a sprint from far off, the click on the wreck's top: the run clip
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				State->Site = FindSite(World, Leader, TEXT("wreck"), 140.f, 750.f, Near(), [World](const FMantleSite& P, const FMantleDecision& D)
				{
					// A straight free run-up to the face.
					FHitResult Hit;
					return D.IsGo() && !World->SweepSingleByChannel(Hit, P.Feet + FVector(0.f, 0.f, 100.f), P.FacePoint - P.Dir * 90.f + FVector(0.f, 0.f, 100.f),
						FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(45.f));
				});
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
				}
				State->Count = Leader->GetMantleCount();
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			if (Leader->GetMantleCount() == State->Count && !Leader->HasMantleOrder() && !State->bSawMantle)
			{
				Check(*State, State->Site.bFound, TEXT("run-up: a wreck with 7.5 m of free ground in front"));
				if (!State->Site.bFound)
				{
					Next(*State, 9);
					return true;
				}
				Leader->OrderMoveTo(State->Site.Probe.FacePoint - State->Site.Dir * 80.f, true); // he runs
				FHitResult Top;
				const FVector TopPoint = State->Site.Probe.EdgeTop + State->Site.Dir * 40.f;
				World->LineTraceSingleByChannel(Top, TopPoint + FVector(0.f, 0.f, 60.f), TopPoint - FVector(0.f, 0.f, 40.f), ECC_Visibility);
				FString Reason;
				const bool bOrdered = Top.bBlockingHit && Leader->OrderMantleTop(Top, Leader->IsSprinting(), &Reason);
				Check(*State, bOrdered, FString::Printf(TEXT("run-up: the click on the wreck's top while running (%s)"), bOrdered ? TEXT("ok") : *Reason));
				State->bSawMantle = true; // the order is given (the flag only gates this branch)
				if (!bOrdered)
				{
					Next(*State, 9);
				}
				return true;
			}
			if ((Leader->IsMantling() || Leader->HasMantleOrder()) && State->StageTime < 15.f)
			{
				return true;
			}
			Check(*State, Leader->GetMantleCount() > State->Count, TEXT("run-up: he climbed"));
			Check(*State, Leader->GetLastMantleGait() == EMantleGait::Running, FString::Printf(TEXT("run-up: the run clip played (%s)"), *Leader->GetLastMantleClip()));
			Check(*State, FMath::Abs(FeetZ(*Leader) - State->Site.Probe.TopZ) < 15.f, TEXT("run-up: on top"));
			Leader->SetSprinting(false);
			Next(*State, 9);
			return true;
		}
		case 9: // h: refusals - a 330 cm fence (a wall), a beam over a crate's top (no headroom)
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				State->Site = FindSite(World, Leader, TEXT("fence"), 330.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision&) { return true; });
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
				}
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			EMantleRefusal Refusal = EMantleRefusal::None;
			Check(*State, State->Site.bFound && !Leader->TryStartMantle(State->Site.Dir, &Refusal, true) && Refusal == EMantleRefusal::Wall,
				FString::Printf(TEXT("a 330 cm fence: refused (%s)"), MantleRules::RefusalName(Refusal)));
			State->Site = FindSite(World, Leader, TEXT("crate"), 85.f, 70.f, Near(), [](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
			if (State->Site.bFound)
			{
				// A beam 140 cm over the top's landing.
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AStaticMeshActor* Beam = World->SpawnActor<AStaticMeshActor>(State->Site.Probe.Landing + FVector(0.f, 0.f, 140.f + 10.f), FRotator::ZeroRotator, Params);
				if (Beam)
				{
					Beam->SetMobility(EComponentMobility::Movable);
					Beam->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					Beam->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					Beam->SetActorScale3D(FVector(1.f, 1.f, 0.2f));
				}
				State->Beam = Beam;
				PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
			}
			Next(*State, 10);
			return true;
		}
		case 10:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			EMantleRefusal Refusal = EMantleRefusal::None;
			Check(*State, State->Beam.IsValid() && !Leader->TryStartMantle(State->Site.Dir, &Refusal, true) && Refusal == EMantleRefusal::NoHeadroom,
				FString::Printf(TEXT("a beam 140 cm over the crate's landing: refused (%s)"), MantleRules::RefusalName(Refusal)));
			if (AActor* Beam = State->Beam.Get())
			{
				Beam->Destroy();
			}
			State->Site = FindSite(World, Leader, TEXT("cut_wall"), 230.f, 60.f, Near(),
				[](const FMantleSite& P, const FMantleDecision& D) { return D.IsGo() && P.Probe.HeightCm > 220.f; });
			Next(*State, 11);
			return true;
		}
		case 11: // i: a knockdown mid-climb
		{
			if (State->StageTime < StepSeconds * 1.5f)
			{
				if (State->Site.bFound)
				{
					PlaceAt(Leader, State->Site.Feet, State->Site.Dir);
				}
				State->Restores = Leader->GetMantleRestoreCount();
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			if (!State->bSawMantle)
			{
				State->bSawMantle = State->Site.bFound && Leader->TryStartMantle(State->Site.Dir, nullptr, true);
				Check(*State, State->bSawMantle, TEXT("knockdown: the tall-ledge climb starts"));
				if (!State->bSawMantle)
				{
					Next(*State, 12);
				}
				return true;
			}
			if (State->StageTime < 1.8f)
			{
				return true;
			}
			if (Leader->IsMantling())
			{
				Check(*State, Leader->KnockdownComponent && Leader->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion),
					TEXT("knockdown: knocked down mid-climb"));
				Check(*State, !Leader->IsMantling() && Leader->GetLastMantleCancel() == EMantleCancel::Knockdown, TEXT("knockdown: the climb is cancelled"));
				Check(*State, Leader->GetMantleRestoreCount() == State->Restores + 1, FString::Printf(TEXT("knockdown: the state restored exactly once (%d)"),
					Leader->GetMantleRestoreCount() - State->Restores));
				return true;
			}
			if (State->StageTime < 3.f)
			{
				return true;
			}
			const float Off = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State->Site.Feet));
			Check(*State, Off < 40.f && FMath::Abs(FeetZ(*Leader) - State->Site.Feet.Z) < 30.f, FString::Printf(TEXT("knockdown: back at the base (%.0f cm, feet %.0f / %.0f)"),
				Off, FeetZ(*Leader), State->Site.Feet.Z));
			Check(*State, Leader->GetActorEnableCollision() && Leader->GetCharacterMovement()->MovementMode != MOVE_Flying, TEXT("knockdown: collision and movement back"));
			Check(*State, Leader->GetMantleRestoreCount() == State->Restores + 1, TEXT("knockdown: still restored once (no second restore)"));
			Next(*State, 12);
			return true;
		}
		case 12:
		{
			// Up again, then a path over an obstacle with a mantle link.
			if (Leader->IsKnockedDown() && State->StageTime < 15.f)
			{
				return true;
			}
			Check(*State, !Leader->IsKnockedDown(), TEXT("knockdown: he got up"));
			State->LinkIndex = INDEX_NONE;
			if (Mantle)
			{
				float BestDist = TNumericLimits<float>::Max();
				for (int32 Index = 0; Index < Mantle->GetLinks().Num(); ++Index)
				{
					const FMantleLink& Link = Mantle->GetLinks()[Index];
					// A fence link first (the way round is long), else any climb up.
					const float Score = static_cast<float>(FVector::Dist2D(Link.Start, Leader->GetActorLocation())) + (Link.ClassId == FenceOver ? 0.f : 100000.f);
					if (!Link.bDown && Score < BestDist)
					{
						BestDist = Score;
						State->LinkIndex = Index;
					}
				}
			}
			Check(*State, State->LinkIndex != INDEX_NONE, TEXT("path: a mantle link on the map"));
			if (State->LinkIndex == INDEX_NONE)
			{
				return Finish(*State, false);
			}
			const FMantleLink& Link = Mantle->GetLinks()[State->LinkIndex];
			PlaceAt(Leader, Link.Start - Link.Dir * 150.f, Link.Dir);
			State->Goal = Link.ClassId == FenceOver ? Link.End + Link.Dir * 150.f : Link.End;
			State->Count = Leader->GetMantleCount();
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleSmoke] path case on link %d (%s, %.0f cm, %s)"), State->LinkIndex, *Link.ClassId.ToString(), Link.HeightCm,
				*GetNameSafe(Link.Obstacle.Get()));
			Next(*State, 13);
			return true;
		}
		case 13:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			if (State->StageTime < 0.6f + StepSeconds * 1.5f)
			{
				Check(*State, Leader->OrderMoveTo(State->Goal, false) == EOperativeOrderResult::Accepted && Leader->IsMantlePathAllowed(),
					TEXT("path: a Solo order across the obstacle (the mantle links open for him)"));
				return true;
			}
			State->bSawMantle |= Leader->IsMantling();
			const float Left = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State->Goal));
			if ((Left > 120.f || Leader->IsMantling() || Leader->IsVaulting()) && State->StageTime < 25.f)
			{
				return true;
			}
			Check(*State, State->bSawMantle && Leader->GetMantleCount() > State->Count, FString::Printf(TEXT("path: he mantled by himself on the way (%s, %s)"),
				*Leader->GetLastMantleClass().ToString(), *Leader->GetLastMantleClip()));
			Check(*State, Left <= 120.f, FString::Printf(TEXT("path: ... and arrived (%.0f cm from the goal, %.1f s)"), Left, State->StageTime));
			// The same order in formation.
			const FMantleLink& Link = Mantle->GetLinks()[State->LinkIndex];
			Squad->ExitSoloMode();
			State->SquadCounts.Reset();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				State->SquadCounts.Add(Member ? Member->GetMantleCount() : 0);
			}
			PlaceAt(Leader, Link.Start - Link.Dir * 150.f, Link.Dir);
			Next(*State, 14);
			return true;
		}
		case 14:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			if (State->StageTime < 0.6f + StepSeconds * 1.5f)
			{
				Leader->OrderMoveTo(State->Goal, false);
				Check(*State, !Leader->IsMantlePathAllowed(), TEXT("formation: his order keeps the mantle links closed"));
				return true;
			}
			State->bSawMantle |= Leader->IsMantling();
			if (State->StageTime < 10.f)
			{
				return true;
			}
			int32 Climbed = 0;
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			for (int32 Index = 0; Index < Members.Num() && Index < State->SquadCounts.Num(); ++Index)
			{
				Climbed += Members[Index] && Members[Index]->GetMantleCount() > State->SquadCounts[Index] ? 1 : 0;
			}
			Check(*State, !State->bSawMantle && Climbed == 0, FString::Printf(TEXT("formation: nobody mantled (%d)"), Climbed));
			// l: a female body climbs with her own retargeted copies (the montage only plays on a matching skeleton).
			State->Leader = nullptr;
			for (AOperativeCharacter* Member : Members)
			{
				if (Member && Member->GetMantleBody() == EMantleBody::Female)
				{
					State->Leader = Member;
					break;
				}
			}
			if (!State->Leader.IsValid())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[MantleSmoke] no female operative in this squad - the female case is skipped"));
				return Finish(*State, true);
			}
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			State->Leader->bTacticalCeaseFire = true;
			State->Site = FindSite(World, State->Leader.Get(), TEXT("wreck"), 140.f, 70.f, State->Leader->GetActorLocation(),
				[](const FMantleSite&, const FMantleDecision& D) { return D.IsGo(); });
			Next(*State, 15);
			return true;
		}
		case 15:
			if (ClimbCase(*State, TEXT("a female operative on a 140 cm wreck"), HighMantle, true))
			{
				return Finish(*State, true);
			}
			return true;
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
		TEXT("CodexTactics.MantleSmoke"),
		TEXT("Dev check (mantling slice 1, L_Gen_Depot_1): low / high mantle on top in a crouch, the cut's tall ledge, a fence over, the vault kept, ")
		TEXT("the run-up rule, refusals (wall, headroom), a knockdown mid-climb, a path mantle in Solo, none in formation; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
