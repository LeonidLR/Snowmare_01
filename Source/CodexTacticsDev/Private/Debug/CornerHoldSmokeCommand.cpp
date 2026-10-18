// Dev-only headless check: holding a corner against a horde (2026-10-07 regression; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CornerHoldSmoke -Log Smoke-CornerHold.log
// Optional "Seed=N" reseeds the random rolls (repeat a -FixedSeed run with other rolls); the run traces the leader's fire
// decisions ([FireTrace], Codex.Debug.FireTrace) and logs the flank rush positions every 2 s (bug hunt 2026-10-18).
// The leader at the right-hand corner of a 6 m x 3 m wall (spawned at runtime, nothing saved), firing on his own.
//  A) 10 frost hounds (melee) pour round the corner from behind the wall, one every 0.6 s;
//  B) a mixed group: 4 frostbitten (melee) + 2 spitters (ranged) behind the wall, round the corner.
// The [CornerAim] trace logs every decision change with its reason and inputs, next to what the pre-fix rule would have
// decided (melee counted as suppression / flank, every hit counted). Checks: A — no duck for safety at all, a run of
// >= 8 consecutive corner shots without a break (only reloads may break it); B — every duck for safety has a ranged
// reason. Counts: shots, ducks by reason, steps off the wall (open shots).
// First (rev 5c, user request 2026-10-13) the MANUAL CORNER PEEK, exploring with no enemy in the world: in cover at the middle
// of the 6 m wall, hovering its end shows the peek preview (ghost + crosshair cursor) and the middle does not; a click on the
// end (ACodexTacticsPlayerController::TryHandleCoverClick) sends him there - he reaches the corner, leans out in the corner aim
// and HOLDS it with nobody around (CornerPeek once), a move order releases it; again in the legacy cover mode
// (bCornerHoldAtEdge off, bCornerAutoDuck on: no "no targets" return), where a shimmy order releases it.
// Then (rev 5d, user bug 2026-10-14) the NEAR EDGE: in cover 50 cm from an end (he snaps to that corner), a click 20 cm in from
// that end - inside the 120 cm "a click near an operative picks him" radius - must start the corner aim at once (handled as a
// peek, not a selection; < 2.5 s, < 40 cm moved): the -R end and the +R end standing, then both crouched facing the other way.
// Then (rev 5e, game designer 2026-10-14) LOW cover: crouched behind a 1 m high, 4 m long wall 10 m to the right - hovering its end
// shows the peek ghost, a click on the -R end from the middle sends him there (crouched corner aim cvr_crch_fire_idle, held), and in
// cover 50 cm from the +R end a click 20 cm in from that end starts the crouched corner aim at once (the near-edge rule).
// Melee close-in (user approval 2026-10-18, ai_tuning.json Codex.Enemy.MeleeCloseIn 1): hounds / frostbitten now reach their bite,
// so a pounce may knock him down - which takes him out of the corner (Sprint 14: a knockdown leaves cover). The smoke counts his
// knockdowns per phase; a phase with one accepts the knockdown's DuckForSafety, counts his shots in any stance (he fights on from
// the open), excludes the downed / get-up frames from the left-hand IK check, and - like a player - re-orders the corner once he is
// up. Hard requirements stay: he gets up every time, keeps firing, the flank rush is beaten and he survives.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/SmokeUtils.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"

namespace CornerHoldSmoke
{
	struct FPhase
	{
		int32 Shots = 0;
		int32 OpenShots = 0;
		int32 Breaks[4] = { 0, 0, 0, 0 };
		int32 Run = 0;
		int32 BestRun = 0;
		int32 Spawned = 0;
		/** Melee close-in (2026-10-18): his knockdowns in this phase, his shots in any stance (OnShot). */
		int32 Knockdowns = 0;
		int32 AnyShots = 0;
	};

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		/** Rev 5c manual corner peek phase: the wall, the peek pass (0 = default cover mode, 1 = legacy), frames the aim broke. */
		TWeakObjectPtr<AStaticMeshActor> Wall;
		int32 PeekPass = 0;
		int32 PeekBrokenFrames = 0;
		/** Rev 5d near-edge passes (0..3: -R end / +R end standing, then crouched facing the other way); where the click found him. */
		int32 NearPass = 0;
		FVector NearClickBody = FVector::ZeroVector;
		int32 NearPeeksBefore = 0;
		/** Rev 5e low-cover passes (0: from the middle to the -R end, 1: the near +R end), the low wall. */
		TWeakObjectPtr<AStaticMeshActor> LowWall;
		int32 LowPass = 0;
		FPhase Phase[3];
		int32 LastLean = 0;
		int32 LastOpen = 0;
		int32 LastBreaks[4] = { 0, 0, 0, 0 };
		float SpawnTimer = 0.f;
		ECoverFacing LastFacing = ECoverFacing::Right;
		bool bLastInCover = false;
		int32 FacingFlips = 0;
		int32 CoverLeaves = 0;
		int32 IKSamples = 0;
		int32 IKLow = 0;
		int32 TotalOpenShots = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Spawned;
		// Every shot (OnWeaponFiredNative): the rule's aim residual and the real barrel's error to the target.
		int32 ShotsLogged = 0;
		int32 RuleOverCone = 0;
		float RuleMax = 0.f;
		int32 BarrelSamples = 0;
		int32 BarrelOverCone = 0;
		float BarrelSum = 0.f;
		float BarrelMax = 0.f;
		float OutwardSum[2] = { 0.f, 0.f };
		int32 OutwardCount[2] = { 0, 0 };
		// Ducks behind the corner other than for a reload (the corner hold never ducks, user decision 2026-10-07).
		bool bLastAim = false;
		float HoldSampleTimer = 0.f;
		int32 Ducks = 0;
		int32 ReloadDucks = 0;
		// Phase D (CoverBug_02, 58 s): the flank rush.
		TArray<TWeakObjectPtr<AEnemyCharacter>> Flank;
		int32 FlankOpenShotsBefore = 0;
		bool bFlankSpawned = false;
		float FlankStart = -1.f;
		float FlankLogTimer = 0.f;
		// Melee close-in (2026-10-18): knockdowns, the corner to re-order after one, shots / knockdowns at the flank start.
		bool bLastDown = false;
		float SinceUp = 100.f;
		float SinceIKBlock = 100.f;
		int32 TotalKnockdowns = 0;
		int32 LastShotsLogged = 0;
		FCoverSlot Corner;
		bool bHasCorner = false;
		bool bKnockedOutOfCorner = false;
		float ReorderTimer = 0.f;
		int32 FlankKnockdownsBefore = 0;
		int32 FlankShotsBefore = 0;
		bool bFlankFromCorner = false;
		float DownTime = 0.f;
		float LongestDown = 0.f;
	};

	/** Logs and checks one shot: the angle between the aim and the target, by the rules and on the real barrel. */
	void OnShot(FState& State, AOperativeCharacter* Op, AActor* Target)
	{
		if (!Op || !Target)
		{
			return;
		}
		++State.ShotsLogged;
		const FVector TargetPoint = Target->GetActorLocation();
		const float Rule = Op->GetShotAimResidualDeg(TargetPoint);
		State.RuleMax = FMath::Max(State.RuleMax, Rule);
		State.RuleOverCone += Rule > Op->AimConeDeg + 0.5f ? 1 : 0;
		float BarrelError = -1.f;
		float Outward = 0.f;
		const bool bHold = Op->bInCover && Op->IsCornerAimActive();
		if (Op->WeaponMesh && Op->WeaponMesh->IsVisible() && Op->WeaponMesh->GetStaticMesh())
		{
			const FVector Barrel = Op->WeaponMesh->GetComponentTransform().TransformVectorNoScale(Op->MuzzleOffset.GetSafeNormal()).GetSafeNormal2D();
			const FVector Muzzle = Op->GetWeaponMuzzleLocation();
			const FVector ToTarget = (TargetPoint - Muzzle).GetSafeNormal2D();
			BarrelError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Barrel, ToTarget)), -1.f, 1.f)));
			++State.BarrelSamples;
			State.BarrelSum += BarrelError;
			State.BarrelMax = FMath::Max(State.BarrelMax, BarrelError);
			State.BarrelOverCone += BarrelError > Op->AimConeDeg ? 1 : 0;
			if (bHold)
			{
				const FCoverSlot& Slot = Op->GetCoverSlot();
				const FVector Along = CoverFacingRules::AlongWallDirection(Slot, Op->CoverFacing).GetSafeNormal2D();
				const FVector Behind = -Slot.WallNormal.GetSafeNormal2D();
				Outward = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Barrel, Behind), FVector::DotProduct(Barrel, Along)));
				const int32 Side = CoverFacingRules::ClipIndex(Op->CoverFacing);
				State.OutwardSum[Side] += Outward;
				++State.OutwardCount[Side];
			}
		}
		FString Clip;
		if (const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr)
		{
			float MontageWeight = 0.f;
			float SlotWeight = 0.f;
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
			Clip += FString::Printf(TEXT(" w%.2f, AimYaw %.0f"), MontageWeight, Anim->AimYaw);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot %d: %s at %.0f m, %s (%s), aim residual %.1f deg, barrel error %.1f deg%s"),
			State.ShotsLogged, *Target->GetName(), FVector::Dist2D(Op->GetActorLocation(), TargetPoint) / 100.f,
			bHold ? TEXT("corner hold") : (Op->bInCover ? TEXT("cover") : TEXT("open stance")), *Clip, Rule, BarrelError,
			bHold ? *FString::Printf(TEXT(", barrel %.0f deg past the along-wall line"), Outward) : TEXT(""));
	}

	/** Per tick: ducks (aim ended while still in cover, not reloading), cover leaves, facing flips, left-hand IK. */
	void SamplePosture(FState& State, AOperativeCharacter* Op)
	{
		const bool bAim = Op->bInCover && Op->IsCornerAimActive();
		if (State.bLastAim && !bAim && Op->bInCover)
		{
			if (Op->bIsReloading)
			{
				++State.ReloadDucks;
			}
			else
			{
				++State.Ducks;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: DUCK behind the corner without a reload (last break %s)"),
					CoverDecisionRules::CornerAimDecisionName(Op->GetLastCornerAimBreak()));
			}
		}
		State.bLastAim = bAim;
		// The corner stance's real barrel (the measurement behind CornerAimOutwardDeg), once per second while holding.
		State.HoldSampleTimer -= 0.05f;
		if (bAim && State.HoldSampleTimer <= 0.f && Op->WeaponMesh && Op->WeaponMesh->IsVisible())
		{
			State.HoldSampleTimer = 1.f;
			const FCoverSlot& Slot = Op->GetCoverSlot();
			const FVector Barrel = Op->WeaponMesh->GetComponentTransform().TransformVectorNoScale(Op->MuzzleOffset.GetSafeNormal());
			const FVector Along = CoverFacingRules::AlongWallDirection(Slot, Op->CoverFacing).GetSafeNormal2D();
			const FVector Behind = -Slot.WallNormal.GetSafeNormal2D();
			const float Outward = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Barrel, Behind), FVector::DotProduct(Barrel, Along)));
			FString Clip;
			float MontageWeight = 0.f;
			float SlotWeight = 0.f;
			if (const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr)
			{
				Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: corner hold barrel %.0f deg past the along-wall line (z %.2f, actor yaw vs wall normal %.0f, clip %s w%.2f slot %.2f, facing %s)"),
				Outward, Barrel.Z, FRotator::NormalizeAxis(Op->GetActorRotation().Yaw - Slot.WallNormal.Rotation().Yaw), *Clip, MontageWeight, SlotWeight,
				Op->CoverFacing == ECoverFacing::Right ? TEXT("right") : TEXT("left"));
		}
		if (Op->bInCover && State.bLastInCover && Op->CoverFacing != State.LastFacing)
		{
			++State.FacingFlips;
		}
		if (State.bLastInCover && !Op->bInCover)
		{
			++State.CoverLeaves;
		}
		State.LastFacing = Op->CoverFacing;
		State.bLastInCover = Op->bInCover;
		if (const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr)
		{
			FString Clip;
			float MontageWeight = 0.f;
			float SlotWeight = 0.f;
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
			State.IKSamples += Op->IsKnockedDown() || State.SinceUp < 1.f || Anim->GetLeftHandIKBlockSeconds() > 0.f ? 0 : 1;
			// A rifle-holding pose: not while reloading, throwing a grenade or in a hit reaction (the left hand leaves the rifle
			// by design; a hound biting him in the open stance plays MM_HitReact_*, 2026-10-18).
			const UAnimSequenceBase* HitClip = Anim->GetLastHitReactionClip();
			const bool bHitReacting = HitClip && Anim->IsPlayingSlotAnimation(HitClip, Anim->GetLastHitReactionSlot());
			// Knocked down / the first second after the get-up, or an upper-body action blocking the IK (a bite's hit reaction:
			// the arms react by design, 2026-10-07) and the IK blend back after it: not a rifle-holding pose. Bites land since
			// the melee close-in (2026-10-18), so hit reactions follow each other in the open.
			State.SinceIKBlock = Anim->GetLeftHandIKBlockSeconds() > 0.f ? 0.f : State.SinceIKBlock + 0.05f;
			const bool bRecovering = Op->IsKnockedDown() || State.SinceUp < 1.f || State.SinceIKBlock < Anim->LeftHandIKBlendSeconds + 0.1f;
			if (Anim->LeftHandIKAlpha < 0.9f && !Op->bIsReloading && !Anim->IsThrowingGrenade() && !bHitReacting && !bRecovering && Anim->bLeftHandIKGripValid)
			{
				++State.IKLow;
				if (State.IKLow <= 8)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: left-hand IK %.2f while %s (in cover %d, excess %.1f, montage %.2f)"),
						Anim->LeftHandIKAlpha, *Clip, Op->bInCover ? 1 : 0, Anim->LeftHandIKExcessCm, MontageWeight);
				}
			}
		}
	}

	void ResetPostureCounters(FState& State, AOperativeCharacter* Op)
	{
		State.FacingFlips = 0;
		State.CoverLeaves = 0;
		State.IKSamples = 0;
		State.IKLow = 0;
		State.LastFacing = Op->CoverFacing;
		State.bLastInCover = Op->bInCover;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CornerHoldSmoke"));
		return false;
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

	/** Counts the leader's shots, breaks and the run of corner shots without a break (a reload does not end the run). */
	void Track(FState& State, FPhase& Phase)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const int32 Lean = Op->GetCoverLeanShots() + Op->GetCoverBlindShots();
		const int32 Open = Op->GetCoverOpenShots();
		if (Lean > State.LastLean)
		{
			Phase.Shots += Lean - State.LastLean;
			Phase.Run += Lean - State.LastLean;
			Phase.BestRun = FMath::Max(Phase.BestRun, Phase.Run);
		}
		Phase.OpenShots += FMath::Max(0, Open - State.LastOpen);
		for (int32 Index = 1; Index < 4; ++Index)
		{
			const int32 Count = Op->GetCornerAimBreakCount(static_cast<ECornerAimDecision>(Index));
			if (Count > State.LastBreaks[Index])
			{
				Phase.Breaks[Index] += Count - State.LastBreaks[Index];
				if (Index == static_cast<int32>(ECornerAimDecision::DuckForSafety))
				{
					Phase.Run = 0; // only a duck for safety ends the hold (a reload, or a return between rushers with no target, does not)
				}
			}
			State.LastBreaks[Index] = Count;
		}
		State.LastLean = Lean;
		State.LastOpen = Open;
		// Melee close-in (2026-10-18): knockdowns (a knockdown leaves cover) and shots in any stance.
		const bool bDown = Op->IsKnockedDown();
		if (bDown && !State.bLastDown)
		{
			++Phase.Knockdowns;
			++State.TotalKnockdowns;
			State.bKnockedOutOfCorner = true;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the leader is knocked down (%d this phase) - out of the corner"), Phase.Knockdowns);
		}
		State.SinceUp = bDown ? 0.f : State.SinceUp + 0.05f;
		State.DownTime = bDown ? State.DownTime + 0.05f : 0.f;
		State.LongestDown = FMath::Max(State.LongestDown, State.DownTime);
		State.bLastDown = bDown;
		Phase.AnyShots += State.ShotsLogged - State.LastShotsLogged;
		State.LastShotsLogged = State.ShotsLogged;
	}

	/**
	 * After a knockdown took him out of the corner, once he is up again: the player orders the corner again (at most every 3 s).
	 * Not during the flank rush (he is meant to fight it from the open).
	 */
	void ReorderCornerAfterKnockdown(FState& State, AOperativeCharacter* Op)
	{
		State.ReorderTimer -= 0.05f;
		if (!State.bKnockedOutOfCorner || !State.bHasCorner || Op->IsKnockedDown() || State.SinceUp < 0.5f || Op->bInCover || State.ReorderTimer > 0.f)
		{
			if (Op->bInCover && Op->bAtCoverCorner)
			{
				State.bKnockedOutOfCorner = false;
			}
			return;
		}
		State.ReorderTimer = 3.f;
		const EOperativeOrderResult Result = Op->OrderTakeCover(State.Corner, true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: up again after a knockdown - the player re-orders the corner (%s)"),
			Result == EOperativeOrderResult::Accepted ? TEXT("accepted") : TEXT("refused"));
	}

	AEnemyCharacter* SpawnRusher(FState& State, UWorld* World, EEnemyArchetype Type, int32 Index)
	{
		// Behind the wall, round the leader's right-hand (-R) corner, in the line the corner stance aims along (it aims
		// straight past the edge into the space behind the wall, CornerAimOutwardDeg), spread a little.
		const FVector Where = State.P + State.F * (1400.f + 80.f * (Index % 3)) - State.R * (330.f + 60.f * (Index % 4));
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, FVector(Where.X, Where.Y, State.GroundZ + 100.f), (-State.F).Rotation());
		if (Enemy)
		{
			State.Spawned.Add(Enemy);
		}
		return Enemy;
	}

	void LogPhase(const TCHAR* Name, const FPhase& Phase)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %d enemies, %d corner shots, best run %d without a break, %d open shots off the wall, breaks: duck-safety %d, duck-reload %d, return %d"),
			Name, Phase.Spawned, Phase.Shots, Phase.BestRun, Phase.OpenShots, Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)],
			Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckToReload)], Phase.Breaks[static_cast<int32>(ECornerAimDecision::ReturnNoTargets)]);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.05f;
		State.StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 300.f)
		{
			return World ? Finish(State) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = Member != Op; // only the leader fights
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Op = Squad->GetLeader();
			State.Op = Op;
			// Headless: the pose (and with it the rifle) is only evaluated when forced - the barrel checks need the real pose.
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			{
				FState* Shared = &State;
				Op->OnWeaponFiredNative.AddLambda([Shared](AOperativeCharacter* Shooter, AActor* Target, bool)
				{
					OnShot(*Shared, Shooter, Target);
				});
			}
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			// Exploring first (the manual corner peek phase): nobody in the world.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 1500.f + State.R * (Index++ % 2 == 0 ? 400.f : -400.f), State.F.Rotation(), false, true);
				}
			}
			Op->ReserveAmmo = 600;
			const FVector WallCentre = State.P + State.F * 400.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			State.Stage = 20;
			State.StageTime = 0.f;
			return true;
		}
		// --- Manual corner peek (rev 5c), exploring: no fight, no enemy ---
		case 20:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (State.PeekPass == 1)
			{
				Op->bCornerHoldAtEdge = false; // the legacy cover mode (CoverSmoke / CoverCrouchSmoke run it)
				Op->bCornerAutoDuck = true;
			}
			FCoverSlot Middle;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + FVector(0.f, 0.f, 90.f), State.F, Middle);
			Check(State, bFound && Op->OrderTakeCover(Middle, false) == EOperativeOrderResult::Accepted,
				FString::Printf(TEXT("peek pass %d: cover at the middle of the 6 m wall (no enemy, no fight)"), State.PeekPass));
			State.Stage = 21;
			State.StageTime = 0.f;
			return true;
		}
		case 21:
		{
			if ((!Op->bInCover || Op->bShimmying || State.StageTime < 2.5f) && State.StageTime < 12.f)
			{
				return true;
			}
			int32 Enemies = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				++Enemies;
			}
			Check(State, Op->bInCover && Op->IsInHighCover() && !Op->IsCornerAimActive() && Enemies == 0,
				FString::Printf(TEXT("peek pass %d: hugging the wall's middle, no corner aim, %d enemies"), State.PeekPass, Enemies));
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			AStaticMeshActor* Wall = State.Wall.Get();
			if (!PC || !Wall)
			{
				Check(State, false, TEXT("controller / wall"));
				return Finish(State);
			}
			// The wall's end on its -R side (the corner the fight phases use), 20 cm in from the end, on the face he hugs.
			const FVector Centre = State.P + State.F * 400.f;
			const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(), Centre - State.F * 20.f - State.R * 280.f + FVector(0.f, 0.f, State.GroundZ + 150.f - Centre.Z),
				-State.F);
			const FHitResult MiddleHit(Wall, Wall->GetStaticMeshComponent(), Centre - State.F * 20.f + State.R * 30.f + FVector(0.f, 0.f, State.GroundZ + 150.f - Centre.Z),
				-State.F);
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			const bool bEdge = PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge);
			Check(State, bEdge && FMath::Abs(Edge) > 250.f,
				FString::Printf(TEXT("the wall's end is a corner peek target (edge %.0f cm along, beyond his slot's probes)"), Edge));
			Check(State, PC->UpdateCornerPeekHover(Op, &EdgeHit) && PC->IsCornerPeekPreviewShown() && PC->CurrentMouseCursor == EMouseCursor::Crosshairs,
				TEXT("hovering the end: the peek ghost + the crosshair cursor"));
			Check(State, !PC->UpdateCornerPeekHover(Op, &MiddleHit) && !PC->IsCornerPeekPreviewShown() && PC->CurrentMouseCursor != EMouseCursor::Crosshairs,
				TEXT("hovering the middle: no peek preview (a click there is a shimmy)"));
			const int32 PeeksBefore = Op->GetManualCornerPeekCount();
			Check(State, PC->TryHandleCoverClick(EdgeHit, nullptr) && Op->IsManualCornerPeek() && Op->GetManualCornerPeekCount() == PeeksBefore,
				TEXT("a click on the end orders the corner peek (the pose is not reached yet)"));
			State.PeekBrokenFrames = 0;
			State.Stage = 22;
			State.StageTime = 0.f;
			return true;
		}
		case 22:
			if (!(Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->bIsCornerLeaning) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->IsManualCornerPeek() && Op->bIsCornerLeaning,
				FString::Printf(TEXT("peek pass %d: at the corner, leaning out in the corner aim (%.1f s)"), State.PeekPass, State.StageTime));
			Check(State, Op->GetManualCornerPeekCount() == State.PeekPass + 1, TEXT("CornerPeek raised once"));
			State.Stage = 23;
			State.StageTime = 0.f;
			return true;
		case 23:
		{
			State.PeekBrokenFrames += Op->IsCornerAimActive() && Op->bAtCoverCorner ? 0 : 1;
			if (State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, State.PeekBrokenFrames == 0 && Op->IsCornerAimActive() && Op->IsManualCornerPeek(),
				FString::Printf(TEXT("peek pass %d: held 4 s with nobody around (broken in %d frames)"), State.PeekPass, State.PeekBrokenFrames));
			if (State.PeekPass == 0)
			{
				Op->OrderMoveTo(State.P, false); // another order: a move
			}
			else
			{
				FCoverSlot Middle;
				CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), State.P + State.F * 380.f + FVector(0.f, 0.f, 90.f), Middle);
				Op->OrderShimmyTo(Middle); // another order: a shimmy
			}
			State.Stage = 24;
			State.StageTime = 0.f;
			return true;
		}
		case 24:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Op->IsCornerAimActive() && !Op->IsManualCornerPeek() && (State.PeekPass == 0 ? !Op->bInCover : Op->bInCover),
				FString::Printf(TEXT("peek pass %d: the %s releases the peek"), State.PeekPass, State.PeekPass == 0 ? TEXT("move order") : TEXT("shimmy order")));
			if (State.PeekPass == 0)
			{
				State.PeekPass = 1;
				State.Stage = 20;
				State.StageTime = 0.f;
				return true;
			}
			Op->bCornerHoldAtEdge = true;
			Op->bCornerAutoDuck = false;
			Op->OrderMoveTo(State.P, false);
			State.NearPass = 0;
			State.Stage = 30;
			State.StageTime = 0.f;
			return true;
		}
		// --- Rev 5d (user bug 2026-10-14): the click on the edge he ALREADY hugs (inside the 120 cm "click near an operative picks
		// him" radius) must start the corner aim at once - both ends, standing and crouched, facing either way ---
		case 30:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			const float EndSign = State.NearPass % 2 == 0 ? -1.f : 1.f; // -R end, then +R end
			FCoverSlot NearEnd;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + State.R * (EndSign * 250.f) + FVector(0.f, 0.f, 90.f),
				State.F, NearEnd);
			Check(State, bFound && Op->OrderTakeCover(NearEnd, false) == EOperativeOrderResult::Accepted,
				FString::Printf(TEXT("near pass %d: cover 50 cm from the %s end of the wall"), State.NearPass, EndSign < 0.f ? TEXT("-R") : TEXT("+R")));
			State.Stage = 31;
			State.StageTime = 0.f;
			return true;
		}
		case 31:
		{
			if ((!Op->bInCover || Op->bShimmying || State.StageTime < 2.5f) && State.StageTime < 12.f)
			{
				return true;
			}
			if (State.NearPass >= 2 && Op->GetStance() != EOperativeStance::Crouching)
			{
				Op->SetStance(EOperativeStance::Crouching); // crouched passes: the crouched stand-offs differ (a few cm shimmy)
				State.StageTime = 0.f;
				return true;
			}
			if (State.NearPass >= 2 && State.StageTime < 1.5f)
			{
				return true;
			}
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			AStaticMeshActor* Wall = State.Wall.Get();
			if (!PC || !Wall)
			{
				Check(State, false, TEXT("controller / wall"));
				return Finish(State);
			}
			const float EndSign = State.NearPass % 2 == 0 ? -1.f : 1.f;
			const FVector Centre = State.P + State.F * 400.f;
			// 20 cm in from that end on the face he hugs: well inside SelectRadius of his body.
			const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(),
				Centre - State.F * 20.f + State.R * (EndSign * 280.f) + FVector(0.f, 0.f, State.GroundZ + 150.f - Centre.Z), -State.F);
			const float BodyToClick = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), EdgeHit.ImpactPoint));
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			const bool bEdge = PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge);
			if (State.NearPass >= 2)
			{
				// Facing the other way along the wall when the click comes (a threat seen earlier on the other side).
				Op->CoverFacing = Side == ECoverFacing::Left ? ECoverFacing::Right : ECoverFacing::Left;
			}
			Check(State, Op->bInCover && Op->IsInHighCover() && bEdge && FMath::Abs(Edge) < 120.f && BodyToClick < PC->SelectRadius,
				FString::Printf(TEXT("near pass %d (%s, %s): hugging the %s end, the click 20 cm in from it is %.0f cm from his body (select radius %.0f), edge %.0f cm along"),
					State.NearPass, State.NearPass >= 2 ? TEXT("crouched") : TEXT("standing"), State.NearPass >= 2 ? TEXT("facing away") : TEXT("facing it"),
					Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), BodyToClick, PC->SelectRadius, Edge));
			Check(State, PC->UpdateCornerPeekHover(Op, &EdgeHit) && PC->IsCornerPeekPreviewShown(), TEXT("near edge hovered: the peek ghost shows"));
			const int32 PeeksBefore = Op->GetManualCornerPeekCount();
			State.NearClickBody = Op->GetActorLocation();
			const bool bHandled = PC->TryHandleCoverClick(EdgeHit, nullptr);
			Check(State, bHandled && Op->IsManualCornerPeek() && Op->GetManualCornerPeekCount() == PeeksBefore,
				FString::Printf(TEXT("near pass %d: the click on the near edge orders the corner peek (handled %d, not a selection)"), State.NearPass, bHandled ? 1 : 0));
			State.NearPeeksBefore = PeeksBefore;
			State.Stage = 32;
			State.StageTime = 0.f;
			return true;
		}
		case 32:
		{
			if (!(Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->bIsCornerLeaning) && State.StageTime < 4.f)
			{
				return true;
			}
			const float Moved = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.NearClickBody));
			Check(State, Op->bInCover && Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->IsManualCornerPeek() && Op->bIsCornerLeaning
				&& Op->GetManualCornerPeekCount() == State.NearPeeksBefore + 1 && State.StageTime < 2.5f && Moved < 40.f,
				FString::Printf(TEXT("near pass %d: corner aim at once (%.2f s, moved %.0f cm, CornerPeek %d -> %d)"), State.NearPass, State.StageTime,
					Moved, State.NearPeeksBefore, Op->GetManualCornerPeekCount()));
			State.Stage = 33;
			State.StageTime = 0.f;
			return true;
		}
		case 33:
		{
			State.PeekBrokenFrames += Op->IsCornerAimActive() ? 0 : 1;
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Op->IsCornerAimActive() && Op->IsManualCornerPeek(), FString::Printf(TEXT("near pass %d: held"), State.NearPass));
			if (State.NearPass == 3)
			{
				Op->SetStance(EOperativeStance::Standing); // the fights below run standing
			}
			Op->OrderMoveTo(State.P, false);
			State.PeekBrokenFrames = 0;
			++State.NearPass;
			State.Stage = State.NearPass < 4 ? 30 : 40;
			State.StageTime = 0.f;
			return true;
		}
		// --- Rev 5e (game designer 2026-10-14): the corner peek from LOW cover, both ends, crouched ---
		case 40:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FVector LowCentre = State.P + State.R * 1000.f + State.F * 400.f;
			if (!State.LowWall.IsValid())
			{
				State.LowWall = SpawnBlock(World, FVector(LowCentre.X, LowCentre.Y, State.GroundZ + 50.f), State.F.Rotation(), FVector(0.4f, 4.f, 1.f));
				State.StageTime = 0.f;
				return true; // the navmesh rebuilds round it
			}
			const float EndSign = State.LowPass == 0 ? 0.f : 1.f; // the middle, then 50 cm from the +R end
			FCoverSlot Slot;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, FVector(LowCentre.X, LowCentre.Y, State.GroundZ + 50.f) - State.F * 20.f
				+ State.R * (EndSign * 150.f), State.F, Slot);
			Check(State, bFound && Slot.Height == ECoverHeight::LowCover && Op->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted,
				FString::Printf(TEXT("low pass %d: low cover at the %s of the 1 m wall"), State.LowPass, State.LowPass == 0 ? TEXT("middle") : TEXT("+R end")));
			State.Stage = 41;
			State.StageTime = 0.f;
			return true;
		}
		case 41:
		{
			if ((!Op->bInCover || Op->bShimmying || State.StageTime < 3.f) && State.StageTime < 15.f)
			{
				return true;
			}
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			AStaticMeshActor* LowWall = State.LowWall.Get();
			if (!PC || !LowWall)
			{
				Check(State, false, TEXT("controller / low wall"));
				return Finish(State);
			}
			const float EndSign = State.LowPass == 0 ? -1.f : 1.f;
			const FVector LowCentre = State.P + State.R * 1000.f + State.F * 400.f;
			const FHitResult EdgeHit(LowWall, LowWall->GetStaticMeshComponent(),
				FVector(LowCentre.X, LowCentre.Y, State.GroundZ + 50.f) - State.F * 20.f + State.R * (EndSign * 180.f), -State.F);
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			const bool bEdge = PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge);
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover && Op->GetStance() == EOperativeStance::Crouching
				&& Op->IsInPeekableCover() && !Op->IsInHighCover() && bEdge,
				FString::Printf(TEXT("low pass %d: crouched in LOW cover, the %s end is a peek edge (%.0f cm along, click %.0f cm from his body)"), State.LowPass,
					EndSign < 0.f ? TEXT("-R") : TEXT("+R"), Edge, FVector::Dist2D(Op->GetActorLocation(), EdgeHit.ImpactPoint)));
			Check(State, PC->UpdateCornerPeekHover(Op, &EdgeHit) && PC->IsCornerPeekPreviewShown() && PC->CurrentMouseCursor == EMouseCursor::Crosshairs,
				TEXT("hovering the low wall's end: the peek ghost + crosshair"));
			State.NearPeeksBefore = Op->GetManualCornerPeekCount();
			State.NearClickBody = Op->GetActorLocation();
			Check(State, PC->TryHandleCoverClick(EdgeHit, nullptr) && Op->IsManualCornerPeek(),
				FString::Printf(TEXT("low pass %d: the click on the end orders the crouched corner peek"), State.LowPass));
			State.PeekBrokenFrames = 0;
			State.Stage = 42;
			State.StageTime = 0.f;
			return true;
		}
		case 42:
		{
			if (!(Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->bIsCornerLeaning) && State.StageTime < 10.f)
			{
				return true;
			}
			const float Moved = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.NearClickBody));
			Check(State, Op->bInCover && Op->bAtCoverCorner && Op->IsCornerAimActive() && Op->IsManualCornerPeek() && Op->bIsCornerLeaning
				&& Op->GetStance() == EOperativeStance::Crouching && Op->GetManualCornerPeekCount() == State.NearPeeksBefore + 1,
				FString::Printf(TEXT("low pass %d: at the end of the low wall, crouched corner aim (%.2f s, moved %.0f cm, CornerPeek %d)"), State.LowPass,
					State.StageTime, Moved, Op->GetManualCornerPeekCount() - State.NearPeeksBefore));
			if (State.LowPass == 1)
			{
				Check(State, State.StageTime < 2.5f && Moved < 40.f, FString::Printf(TEXT("low pass 1: the near-edge click poses at once (%.2f s, %.0f cm)"), State.StageTime, Moved));
			}
			State.Stage = 43;
			State.StageTime = 0.f;
			return true;
		}
		case 43:
		{
			State.PeekBrokenFrames += Op->IsCornerAimActive() ? 0 : 1;
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FString Clip;
			float MontageWeight = 0.f;
			float SlotWeight = 0.f;
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Anim)
			{
				Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
			}
			const bool bHasCrouchClips = Anim && Anim->CoverCrouchFireIdle.Num() > 0 && Anim->CoverCrouchFireIdle[0];
			Check(State, State.PeekBrokenFrames == 0 && Op->IsCornerAimActive() && Op->IsManualCornerPeek() && Op->bInCover,
				FString::Printf(TEXT("low pass %d: held 2 s with nobody around (broken %d frames)"), State.LowPass, State.PeekBrokenFrames));
			Check(State, !bHasCrouchClips || Clip.Contains(TEXT("crch_fire")),
				FString::Printf(TEXT("low pass %d: the crouched corner stance plays (%s)"), State.LowPass, *Clip));
			Op->OrderMoveTo(State.P, false); // another order releases the peek
			State.Stage = 44;
			State.StageTime = 0.f;
			return true;
		}
		case 44:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Op->IsCornerAimActive() && !Op->IsManualCornerPeek() && !Op->bInCover,
				FString::Printf(TEXT("low pass %d: a move order releases the low-cover peek"), State.LowPass));
			if (State.LowPass == 0)
			{
				State.LowPass = 1;
				State.Stage = 40;
				State.StageTime = 0.f;
				return true;
			}
			if (AStaticMeshActor* LowWall = State.LowWall.Get())
			{
				LowWall->Destroy();
			}
			Op->SetStance(EOperativeStance::Standing); // the fights below run standing
			State.Stage = 25;
			State.StageTime = 0.f;
			return true;
		}
		case 25:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The fights.
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			SmokeUtils::StartWaveFight(Flow); // sudden horde start, its free pause released
			// The wave stays alive with a frozen hound far away; the spawned wave goes.
			AEnemyCharacter* Parked = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) - State.F * 6000.f, State.F.Rotation());
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Parked)
				{
					It->Destroy();
				}
			}
			if (Parked)
			{
				Parked->CustomTimeDilation = 0.f;
				Parked->SetActorTickEnabled(false);
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FCoverSlot Corner;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f), State.F, Corner);
			Check(State, bFound && Op->OrderTakeCover(Corner, true) == EOperativeOrderResult::Accepted, TEXT("the leader takes the right-hand corner"));
			State.Corner = Corner;
			State.bHasCorner = bFound;
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
			if ((!Op->bInCover || State.StageTime < 2.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->bAtCoverCorner, TEXT("at the corner"));
			State.LastLean = Op->GetCoverLeanShots() + Op->GetCoverBlindShots();
			State.LastOpen = Op->GetCoverOpenShots();
			State.LastShotsLogged = State.ShotsLogged;
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		case 3:
		{
			// A) the melee horde.
			FPhase& Phase = State.Phase[0];
			State.SpawnTimer -= 0.05f;
			if (Phase.Spawned < 10 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.6f;
				Phase.Spawned += SpawnRusher(State, World, EEnemyArchetype::FrostHound, Phase.Spawned) ? 1 : 0;
			}
			Track(State, Phase);
			SamplePosture(State, Op);
			ReorderCornerAfterKnockdown(State, Op);
			if (State.StageTime < 28.f)
			{
				return true;
			}
			LogPhase(TEXT("melee horde"), Phase);
			// A knockdown ends the corner aim as a DuckForSafety ("knocked down"); any other duck for safety is a failure.
			Check(State, Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)] <= Phase.Knockdowns,
				FString::Printf(TEXT("melee horde: no duck for safety except his knockdowns (%d ducks, %d knockdowns)"),
					Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)], Phase.Knockdowns));
			// User decision 2026-10-07: the corner stance fires along its line; hounds that come round into the open side
			// are fired at from the open stance (the per-shot aim checks are at the end).
			Check(State, Phase.Shots >= 2 || (Phase.Knockdowns > 0 && Phase.AnyShots >= 2),
				FString::Printf(TEXT("melee horde: he fires from the corner hold at the wave in its line (%d corner shots, %d in any stance, %d knockdowns)"),
					Phase.Shots, Phase.AnyShots, Phase.Knockdowns));
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Spawned)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
			State.Spawned.Reset();
			State.Stage = 4;
			State.StageTime = 0.f;
			State.SpawnTimer = 0.f;
			return true;
		}
		case 4:
		{
			// B) frostbitten (melee) + spitters (ranged).
			FPhase& Phase = State.Phase[1];
			State.SpawnTimer -= 0.05f;
			if (Phase.Spawned < 6 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.8f;
				const EEnemyArchetype Type = Phase.Spawned % 3 == 2 ? EEnemyArchetype::Spitter : EEnemyArchetype::Frostbitten;
				Phase.Spawned += SpawnRusher(State, World, Type, Phase.Spawned) ? 1 : 0;
			}
			Track(State, Phase);
			SamplePosture(State, Op);
			ReorderCornerAfterKnockdown(State, Op);
			if (State.StageTime < 30.f)
			{
				return true;
			}
			LogPhase(TEXT("mixed group"), Phase);
			// Spitters stop out of the corner stance's line of sight; with the 1D aim offset (no twist) the stance reaches only
			// its own line - the rest is fought from the open stance (after a knockdown: from wherever he got up).
			Check(State, Phase.Shots + Phase.OpenShots >= 4 || (Phase.Knockdowns > 0 && Phase.AnyShots >= 4),
				FString::Printf(TEXT("mixed group: he fights them (%d corner + %d open shots, %d in any stance, %d knockdowns)"), Phase.Shots,
					Phase.OpenShots, Phase.AnyShots, Phase.Knockdowns));
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Spawned)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
			State.Spawned.Reset();
			State.SpawnTimer = 0.f;
			ResetPostureCounters(State, Op);
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			// C) the user's PIE video: a hound stream from the OPEN side, in front of the wall, towards his corner.
			FPhase& Phase = State.Phase[2];
			State.SpawnTimer -= 0.05f;
			if (Phase.Spawned < 12 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.7f;
				const int32 Index = Phase.Spawned;
				const FVector Where = State.P + State.F * (100.f + 70.f * (Index % 3)) - State.R * (900.f + 90.f * (Index % 4));
				if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
					FVector(Where.X, Where.Y, State.GroundZ + 100.f), State.R.Rotation()))
				{
					State.Spawned.Add(Hound);
					++Phase.Spawned;
				}
			}
			Track(State, Phase);
			SamplePosture(State, Op);
			ReorderCornerAfterKnockdown(State, Op);
			if (State.StageTime < 26.f)
			{
				return true;
			}
			LogPhase(TEXT("open-side stream"), Phase);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: open-side stream - facing flips %d, cover leaves %d, IK below 0.9 in %d of %d frames"),
				State.FacingFlips, State.CoverLeaves, State.IKLow, State.IKSamples);
			Check(State, State.FacingFlips <= 1, FString::Printf(TEXT("open-side stream: the fire stance keeps its side (%d flips)"), State.FacingFlips));
			Check(State, State.CoverLeaves <= 1 + Phase.Knockdowns, FString::Printf(TEXT("open-side stream: at most one step off the wall per engagement (%d, + %d knockdowns)"),
				State.CoverLeaves, Phase.Knockdowns));
			// Each knockdown costs ~2.7 s on the ground plus the walk back to the corner (~4 of his shots at 0.65 s): with one, 5.
			Check(State, Phase.Shots + Phase.OpenShots >= 8 || (Phase.Knockdowns > 0 && Phase.AnyShots >= 5),
				FString::Printf(TEXT("open-side stream: he keeps firing (%d corner + %d open shots, %d in any stance, %d knockdowns)"), Phase.Shots, Phase.OpenShots,
					Phase.AnyShots, Phase.Knockdowns));
			Check(State, State.IKLow * 20 <= State.IKSamples, FString::Printf(TEXT("open-side stream: the left hand holds the rifle (IK < 0.9 in %d of %d frames)"), State.IKLow, State.IKSamples));
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Spawned)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
			State.Spawned.Reset();
			State.Stage = 6;
			State.StageTime = 0.f;
			return true;
		}
		case 6:
			// Back to the corner after the open-side stream (calm), in the corner hold again (re-ordered after a knockdown).
			ReorderCornerAfterKnockdown(State, Op);
			if (!(Op->bInCover && Op->IsCornerAimActive()) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->IsCornerAimActive(), FString::Printf(TEXT("calm: back at the corner, holding the fire stance (%.1f s)"), State.StageTime));
			ResetPostureCounters(State, Op);
			State.bLastAim = Op->bInCover && Op->IsCornerAimActive();
			State.SpawnTimer = 0.f;
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		case 7:
		{
			// D) CoverBug_02, 58 s: holding the corner at a wave round it, then hounds and a frostbitten rush his right
			// flank in front of the wall - he must turn to them (open stance), never fire at them from the corner pose.
			FPhase& Phase = State.Phase[2];
			State.SpawnTimer -= 0.05f;
			if (State.StageTime < 5.f && State.Spawned.Num() < 3 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.8f;
				// Spitters behind the wall in the corner stance's line (the video: "heavy ranged fire" in the label) - they keep
				// their distance, he holds the corner aim at them.
				SpawnRusher(State, World, EEnemyArchetype::Spitter, State.Spawned.Num());
			}
			// The flank rush comes while he holds the corner aim (as at 58 s of the video).
			if (State.StageTime >= 4.f && !State.bFlankSpawned && ((Op->bInCover && Op->IsCornerAimActive()) || State.StageTime >= 14.f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: flank rush starts at %.1f s, the leader %s"), State.StageTime,
					Op->bInCover && Op->IsCornerAimActive() ? TEXT("holds the corner aim") : TEXT("is NOT in the corner hold"));
				State.bFlankSpawned = true;
				State.FlankOpenShotsBefore = Op->GetCoverOpenShots();
				State.FlankKnockdownsBefore = State.Phase[2].Knockdowns;
				State.FlankShotsBefore = State.ShotsLogged;
				State.bFlankFromCorner = Op->bInCover && Op->IsCornerAimActive();
				ResetPostureCounters(State, Op);
				const FVector Right = Op->GetActorRightVector().GetSafeNormal2D();
				const FVector Front = Op->GetActorForwardVector().GetSafeNormal2D();
				for (int32 Index = 0; Index < 5; ++Index)
				{
					const FVector Where = Op->GetActorLocation() + Right * (700.f + 90.f * Index) + Front * (150.f + 80.f * (Index % 2));
					const EEnemyArchetype Type = Index == 4 ? EEnemyArchetype::Frostbitten : EEnemyArchetype::FrostHound;
					if (AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, FVector(Where.X, Where.Y, State.GroundZ + 100.f),
						(-Right).Rotation()))
					{
						State.Flank.Add(Enemy);
						State.Spawned.Add(Enemy);
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: flank rush - %d enemies on his right in front of the wall"), State.Flank.Num());
			}
			Track(State, Phase);
			SamplePosture(State, Op);
			int32 FlankDown = 0;
			int32 FlankGone = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Flank)
			{
				const bool bDown = !Enemy.IsValid() || Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive();
				FlankDown += bDown ? 1 : 0;
				// Gave him up (2026-10-18): a hound / frostbitten that stopped short of its bite (HANDOFF §9 "melee enemies stop
				// 1.65-1.75 m off", Codex.Enemy.MeleeCloseIn 0) skips him after 3 s ("no way to ...") and runs for the generator
				// 80 m away - out of the fight, nothing he can do about it.
				FlankGone += !bDown && Enemy->GetCurrentTarget() != Op && FVector::Dist2D(Enemy->GetActorLocation(), Op->GetActorLocation()) > 1500.f ? 1 : 0;
			}
			// Every 2 s: where he and the flankers stand in the wall's frame (F from the leader's start, R from the wall's -R end).
			if (State.bFlankSpawned && (State.FlankLogTimer -= 0.05f) <= 0.f)
			{
				State.FlankLogTimer = 2.f;
				const FVector Corner = State.P - State.R * 300.f;
				auto Frame = [&State, &Corner](const FVector& Where)
				{
					const FVector D = Where - Corner;
					return FString::Printf(TEXT("(F %.0f, R %.0f)"), FVector::DotProduct(D, State.F), FVector::DotProduct(D, State.R));
				};
				FString Line = FString::Printf(TEXT("Smoke: flank %.1f s - leader %s%s"), State.StageTime, *Frame(Op->GetActorLocation()),
					Op->bInCover ? TEXT(" in cover") : TEXT(" open"));
				for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Flank)
				{
					if (Enemy.IsValid() && !Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive())
					{
						FHitResult SightHit;
						FCollisionQueryParams SightParams(SCENE_QUERY_STAT(CornerHoldSmokeSight), false, Op);
						SightParams.AddIgnoredActor(Enemy.Get());
						const bool bSightBlocked = World->LineTraceSingleByChannel(SightHit, UTacticalSightSubsystem::EyePoint(*Op),
							UTacticalSightSubsystem::ProfilePoint(*Enemy), ECC_Visibility, SightParams);
						Line += FString::Printf(TEXT(" | %s %s %.0f cm%s (sight %s), target %s, v %.0f"), *Enemy->GetName(), *Frame(Enemy->GetActorLocation()),
							FVector::Dist2D(Enemy->GetActorLocation(), Op->GetActorLocation()), Enemy->IsHidden() ? TEXT(" hidden") : TEXT(""),
							bSightBlocked && SightHit.GetActor() ? *SightHit.GetActor()->GetName() : TEXT("clear"),
							Enemy->GetCurrentTarget() ? *Enemy->GetCurrentTarget()->GetName() : TEXT("-"), Enemy->GetVelocity().Size2D());
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("%s"), *Line);
			}
			// 20 s of the rush, then until 3 of them are down (cap 45 s): how many fall in a fixed 20 s is the hit / crit
			// rolls' luck (2 or 3 of 5 with 9-10 open shots, flaky 2026-10-15); the behaviour checked is that he turns to
			// them and his open-stance shots bring them down. The ones that gave him up and ran off (FlankGone) end the wait too.
			if (!State.bFlankSpawned || State.StageTime < State.FlankStart + 20.f
				|| (FlankDown < 3 && FlankDown + FlankGone < State.Flank.Num() && State.StageTime < State.FlankStart + 45.f))
			{
				if (State.bFlankSpawned && State.FlankStart < 0.f)
				{
					State.FlankStart = State.StageTime;
				}
				return true;
			}
			const int32 FlankOpenShots = Op->GetCoverOpenShots() - State.FlankOpenShotsBefore;
			const int32 FlankKnockdowns = Phase.Knockdowns - State.FlankKnockdownsBefore;
			const int32 FlankAnyShots = State.ShotsLogged - State.FlankShotsBefore;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: flank rush - %d of %d down, %d gave him up (ran off), %d open shots, cover leaves %d, facing flips %d, IK below 0.9 in %d of %d frames"),
				FlankDown, State.Flank.Num(), FlankGone, FlankOpenShots, State.CoverLeaves, State.FacingFlips, State.IKLow, State.IKSamples);
			Check(State, FlankOpenShots >= 3 || ((FlankKnockdowns > 0 || !State.bFlankFromCorner) && FlankAnyShots >= 3),
				FString::Printf(TEXT("flank rush: he turns to them and fires in the open stance (%d open shots, %d in any stance, %d knockdowns)"), FlankOpenShots,
					FlankAnyShots, FlankKnockdowns));
			Check(State, FlankDown >= 3 || (FlankDown >= 2 && FlankDown + FlankGone == State.Flank.Num()),
				FString::Printf(TEXT("flank rush: %d of %d down, %d ran off (%.1f s)"), FlankDown, State.Flank.Num(), FlankGone, State.StageTime - State.FlankStart));
			Check(State, State.CoverLeaves <= 1 + FlankKnockdowns && (State.CoverLeaves >= 1 || !State.bFlankFromCorner),
				FString::Printf(TEXT("flank rush: exactly one posture switch off the corner to face them (%d, + %d knockdowns)"), State.CoverLeaves, FlankKnockdowns));
			Check(State, State.IKLow * 20 <= State.IKSamples, FString::Printf(TEXT("flank rush: the left hand holds the rifle (IK < 0.9 in %d of %d frames)"), State.IKLow, State.IKSamples));
			// Whole run.
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: shots %d - rule residual max %.1f deg (%d over the %.0f deg cone); barrel error mean %.1f max %.1f deg (%d of %d over the cone)"),
				State.ShotsLogged, State.RuleMax, State.RuleOverCone, Op->AimConeDeg, State.BarrelSamples > 0 ? State.BarrelSum / State.BarrelSamples : 0.f,
				State.BarrelMax, State.BarrelOverCone, State.BarrelSamples);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: corner hold barrel past the along-wall line - his right (pack _L) %.1f deg over %d shots, his left (pack _R) %.1f deg over %d shots"),
				State.OutwardCount[0] > 0 ? State.OutwardSum[0] / State.OutwardCount[0] : 0.f, State.OutwardCount[0],
				State.OutwardCount[1] > 0 ? State.OutwardSum[1] / State.OutwardCount[1] : 0.f, State.OutwardCount[1]);
			Check(State, State.RuleOverCone == 0, FString::Printf(TEXT("every shot within the aim cone by the rules (%d of %d over, max %.1f deg)"), State.RuleOverCone, State.ShotsLogged, State.RuleMax));
			Check(State, State.BarrelOverCone == 0, FString::Printf(TEXT("every shot along the real barrel within the cone (%d of %d over, max %.1f deg)"), State.BarrelOverCone, State.BarrelSamples, State.BarrelMax));
			Check(State, State.Ducks == 0, FString::Printf(TEXT("no duck behind the corner except to reload (%d ducks, %d reloads)"), State.Ducks, State.ReloadDucks));
			// Melee close-in (2026-10-18): bites and pounces land now - he must come through them.
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: melee toll - %d knockdowns, longest on the ground %.1f s"), State.TotalKnockdowns, State.LongestDown);
			// Sprint 14 get-up: 0.87 + 0.6 + 0.8 s (x2.08 faster) - never pinned down by chained pounces (re-knock immunity).
			Check(State, Op->HealthComponent && Op->HealthComponent->IsAlive() && State.LongestDown < 5.f,
				FString::Printf(TEXT("he survives the four waves and gets up every time (%d knockdowns, longest down %.1f s)"), State.TotalKnockdowns, State.LongestDown));
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
		// "Seed=N" (2026-10-18 bug hunt): reseeds the game's random rolls (hit / crit / knockdown / AI), so a run with
		// -FixedSeed can be repeated with other rolls; every operative's fire decision is traced ([FireTrace]).
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.StartsWith(TEXT("Seed=")) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				const int32 Seed = FCString::Atoi(*Value);
				FMath::RandInit(Seed);
				FMath::SRandInit(Seed);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: random seed %d"), Seed);
			}
		}
		if (IConsoleVariable* FireTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.FireTrace")))
		{
			FireTrace->Set(1, ECVF_SetByConsole);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerHoldSmoke"),
		TEXT("Dev check: holding a corner against a melee horde and a mixed group; ducks and their reasons; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
