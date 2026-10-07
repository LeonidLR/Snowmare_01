// Dev-only headless check: holding a corner against a horde (2026-10-07 regression; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CornerHoldSmoke -Log Smoke-CornerHold.log
// The leader at the right-hand corner of a 6 m x 3 m wall (spawned at runtime, nothing saved), firing on his own.
//  A) 10 frost hounds (melee) pour round the corner from behind the wall, one every 0.6 s;
//  B) a mixed group: 4 frostbitten (melee) + 2 spitters (ranged) behind the wall, round the corner.
// The [CornerAim] trace logs every decision change with its reason and inputs, next to what the pre-fix rule would have
// decided (melee counted as suppression / flank, every hit counted). Checks: A — no duck for safety at all, a run of
// >= 8 consecutive corner shots without a break (only reloads may break it); B — every duck for safety has a ranged
// reason. Counts: shots, ducks by reason, steps off the wall (open shots).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

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
			++State.IKSamples;
			// A rifle-holding pose: not while reloading or throwing a grenade (the left hand leaves the rifle by design).
			if (Anim->LeftHandIKAlpha < 0.9f && !Op->bIsReloading && !Anim->IsThrowingGrenade() && Anim->bLeftHandIKGripValid)
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
		if (!World || State.Time > 240.f)
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
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
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
			SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
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
			if (State.StageTime < 28.f)
			{
				return true;
			}
			LogPhase(TEXT("melee horde"), Phase);
			Check(State, Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)] == 0,
				FString::Printf(TEXT("melee horde: no duck for safety (%d)"), Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)]));
			// User decision 2026-10-07: the corner stance fires along its line; hounds that come round into the open side
			// are fired at from the open stance (the per-shot aim checks are at the end).
			Check(State, Phase.Shots >= 2, FString::Printf(TEXT("melee horde: he fires from the corner hold at the wave in its line (%d corner shots)"), Phase.Shots));
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
			if (State.StageTime < 30.f)
			{
				return true;
			}
			LogPhase(TEXT("mixed group"), Phase);
			// Spitters stop out of the corner stance's line of sight; with the 1D aim offset (no twist) the stance reaches only
			// its own line - the rest is fought from the open stance.
			Check(State, Phase.Shots + Phase.OpenShots >= 4, FString::Printf(TEXT("mixed group: he fights them (%d corner + %d open shots)"), Phase.Shots, Phase.OpenShots));
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
			if (State.StageTime < 26.f)
			{
				return true;
			}
			LogPhase(TEXT("open-side stream"), Phase);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: open-side stream - facing flips %d, cover leaves %d, IK below 0.9 in %d of %d frames"),
				State.FacingFlips, State.CoverLeaves, State.IKLow, State.IKSamples);
			Check(State, State.FacingFlips <= 1, FString::Printf(TEXT("open-side stream: the fire stance keeps its side (%d flips)"), State.FacingFlips));
			Check(State, State.CoverLeaves <= 1, FString::Printf(TEXT("open-side stream: at most one step off the wall per engagement (%d)"), State.CoverLeaves));
			Check(State, Phase.Shots + Phase.OpenShots >= 8, FString::Printf(TEXT("open-side stream: he keeps firing (%d corner + %d open shots)"), Phase.Shots, Phase.OpenShots));
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
			// Back to the corner after the open-side stream (calm), in the corner hold again.
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
			if (!State.bFlankSpawned || State.StageTime < State.FlankStart + 20.f)
			{
				if (State.bFlankSpawned && State.FlankStart < 0.f)
				{
					State.FlankStart = State.StageTime;
				}
				return true;
			}
			int32 FlankDown = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Flank)
			{
				FlankDown += !Enemy.IsValid() || Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive() ? 1 : 0;
			}
			const int32 FlankOpenShots = Op->GetCoverOpenShots() - State.FlankOpenShotsBefore;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: flank rush - %d of %d down, %d open shots, cover leaves %d, facing flips %d, IK below 0.9 in %d of %d frames"),
				FlankDown, State.Flank.Num(), FlankOpenShots, State.CoverLeaves, State.FacingFlips, State.IKLow, State.IKSamples);
			Check(State, FlankOpenShots >= 3, FString::Printf(TEXT("flank rush: he turns to them and fires in the open stance (%d open shots)"), FlankOpenShots));
			Check(State, FlankDown >= 3, FString::Printf(TEXT("flank rush: %d of %d down"), FlankDown, State.Flank.Num()));
			Check(State, State.CoverLeaves == 1, FString::Printf(TEXT("flank rush: exactly one posture switch off the corner to face them (%d)"), State.CoverLeaves));
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
