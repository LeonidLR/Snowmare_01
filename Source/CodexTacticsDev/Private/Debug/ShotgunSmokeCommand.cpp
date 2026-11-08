// Dev-only console command: the D-1 shotgun end to end (designer decisions 21 / 21+, 2026-11-03; the weapons framework, 2026-11-08).
//   Scripts/smoke_parallel.ps1 -Commands ShotgunSmoke
// 1. loadout: the Engineer spawns with the D-1 in hands ("D-1 Shotgun", id shotgun), no M16, pistol + grenade, 8 shells + 40, the D-1
//    model on the weapon socket (WeaponSkeletalMesh); the shotgun animation layer loaded.
// 2. standing: the layer drives the whole body (mask Full) with a ShotgunLocomotionPack clip; walking: a shotgun walk / jog clip.
// 3. shots at a target 3 / 8 / 14.5 m out, every pellet forced to hit: 8 pellets each, the 14.5 m shot does x the falloff (0.4 at the
//    15 m max reach: 0.443) of the 3 m one, the 8 m shot (= the effective range) the full damage; free shots at 8 m: partial pellet hits,
//    damage proportional to the pellets that hit; every shot plays the shotgun fire clip.
// 4. per-shell reload: one shell per 0.5 s (and a reload clip per shell), empty -> full in ~4 s; a quick fire order between shells stops
//    the reload with the shells already in.
// 5. a PMC fighter given the shotgun runs the same layer (his own body: SKM_PmcOperator).
// 6. #51 (designer video 2026-11-06): firing knocks no body back - the torso (spine_03) of the PMC fighter and of the Engineer moves
//    less than RecoilLimitCm off its pre-shot place while they fire, and the capsule does not move.
// 7. #51 the real cause (CodexTactics.RecoilProbe in PMC arena fights, 2026-11-08): a shot fired while the body rises from prone (the
//    full-body prone -> crouch clip) used to stop that clip and pop the torso 40-80 cm in one frame. The PMC fighter (M16) and the Engineer
//    (D-1) lie down, get up and fire 0.15 s into the rise: the rise keeps playing and the torso never moves more than PopLimitCm in a frame.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DeployableRules.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

namespace ShotgunSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** #51: the torso may sway with the fire clip, never be thrown back (the bug: tens of cm). */
	constexpr float RecoilLimitCm = 25.f;
	/** #51 rise check: the torso's largest move in ONE frame (a sprint is 9 cm a frame at 60 fps; the bug popped 40-80 cm). */
	constexpr float PopLimitCm = 15.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		TWeakObjectPtr<AEnemyCharacter> Target;
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		bool bSawMoveClip = false;
		FString MoveClip;
		// Shots.
		float Damage3 = 0.f;
		float Damage8 = 0.f;
		float Damage14 = 0.f;
		int32 Pellets3 = 0;
		int32 Pellets8 = 0;
		int32 Pellets14 = 0;
		int32 FreeShots = 0;
		int32 PartialShots = 0;
		float WorstPerPelletError = 0.f;
		int32 FireClipsBefore = 0;
		// Reload.
		TArray<float> ShellTimes;
		int32 LastClip = 0;
		int32 ShellClipsBefore = 0;
		// #51 probe: the torso relative to the actor, per frame.
		TWeakObjectPtr<AOperativeCharacter> Probe;
		bool bProbe = false;
		bool bBaseline = false;
		FVector Baseline = FVector::ZeroVector;
		int32 BaselineSamples = 0;
		float MaxTorsoOffset = 0.f;
		FVector ProbeActorStart = FVector::ZeroVector;
		float MaxActorMove = 0.f;
		int32 ProbeShots = 0;
		float FighterTorso = 0.f;
		float FighterActor = 0.f;
		float EngineerTorso = 0.f;
		float EngineerActor = 0.f;
		// #51 rise check: lie down, get up, fire during the rise; the torso's per-frame world move.
		TWeakObjectPtr<AOperativeCharacter> Riser;
		int32 RisePhase = 0;
		float RiseTime = 0.f;
		bool bRiseSampling = false;
		bool bRiseHasLast = false;
		FVector RiseLast = FVector::ZeroVector;
		float RiseMaxPop = 0.f;
		FString RisePopWhen;
		bool bRiseTransitionBefore = false;
		bool bRiseTransitionAfter = false;
		int32 RiseSkippedBefore = 0;
		FDelegateHandle FrameHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ShotgunSmoke"));
		return false;
	}

	void GoTo(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke stage %d"), Stage);
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString DominantName(const AOperativeCharacter* Operative)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		const UAnimSequenceBase* Clip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
		return Clip ? Clip->GetName() : FString(TEXT("none"));
	}

	bool Has(const AOperativeCharacter* Operative, const TCHAR* Id)
	{
		return Operative->AvailableWeapons.ContainsByPredicate([Id](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == Id; });
	}

	/** The target (frozen hound) DistanceCm ahead of the Engineer, the Engineer facing it. */
	void PlaceTarget(FState& State, float DistanceCm)
	{
		AOperativeCharacter* Engineer = State.Engineer.Get();
		AEnemyCharacter* Target = State.Target.Get();
		if (!Engineer || !Target)
		{
			return;
		}
		const FVector Spot = Engineer->GetActorLocation() + State.Forward * DistanceCm;
		Target->TeleportTo(FVector(Spot.X, Spot.Y, Target->GetActorLocation().Z), (-State.Forward).Rotation(), false, true);
		Engineer->SetActorRotation(State.Forward.Rotation());
	}

	/** One forced-hit shot: the damage it did and the pellets that hit. */
	float Shoot(FState& State, AOperativeCharacter* Shooter, bool bForce, int32& OutPellets)
	{
		AEnemyCharacter* Target = State.Target.Get();
		if (!Shooter || !Target)
		{
			return 0.f;
		}
		UHealthComponent* Health = Target->GetHealthComponent();
		Health->SetMaxHealth(100000.f, true);
		const float Before = Health->GetCurrentHealth();
		Shooter->CurrentClip = FMath::Max(Shooter->CurrentClip, 1);
		Shooter->bForceHitForTesting = bForce;
		Shooter->ForcedCritRollForTesting = 0.f; // no crit: the numbers are compared
		Shooter->ShootAtTarget(Target);
		Shooter->bForceHitForTesting = false;
		OutPellets = Shooter->GetLastShotPelletHits();
		return Before - Health->GetCurrentHealth();
	}

	void StartProbe(FState& State, AOperativeCharacter* Who)
	{
		if (Who && Who->GetMesh())
		{
			// Headless (-nullrhi) meshes are never rendered: without this their pose is not evaluated and the bones stand still.
			Who->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		State.Probe = Who;
		State.bProbe = true;
		State.bBaseline = true;
		State.Baseline = FVector::ZeroVector;
		State.BaselineSamples = 0;
		State.MaxTorsoOffset = 0.f;
		State.MaxActorMove = 0.f;
		State.ProbeShots = 0;
		State.ProbeActorStart = Who ? Who->GetActorLocation() : FVector::ZeroVector;
	}

	void SampleRise(FState& State)
	{
		const AOperativeCharacter* Who = State.Riser.Get();
		if (!State.bRiseSampling || !Who || !Who->GetMesh())
		{
			return;
		}
		const FVector Torso = Who->GetMesh()->GetBoneLocation(TEXT("spine_03"));
		if (State.bRiseHasLast)
		{
			const float Pop = static_cast<float>(FVector::Dist(Torso, State.RiseLast));
			if (Pop > State.RiseMaxPop)
			{
				const UOperativeAnimInstance* Anim = AnimOf(Who);
				State.RiseMaxPop = Pop;
				State.RisePopWhen = FString::Printf(TEXT("%.2f s after the rise order, transition %d, shotgun mask %d upper %.2f lower %.2f, delta %s"), State.StageTime - State.RiseTime,
					Anim && Anim->IsPlayingStanceTransition() ? 1 : 0, Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1, Anim ? Anim->GetShotgunUpperWeight() : 0.f,
					Anim ? Anim->GetShotgunLowerWeight() : 0.f, *(Torso - State.RiseLast).ToCompactString());
			}
		}
		State.RiseLast = Torso;
		State.bRiseHasLast = true;
	}

	void StartRise(FState& State, AOperativeCharacter* Who)
	{
		if (Who && Who->GetMesh())
		{
			Who->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		State.Riser = Who;
		State.RisePhase = 0;
		State.bRiseSampling = false;
		State.bRiseHasLast = false;
		State.RiseMaxPop = 0.f;
		State.bRiseTransitionBefore = false;
		State.bRiseTransitionAfter = false;
	}

	/** Runs the rise check on State.Riser; true when it is over (checks logged). */
	bool StepRise(FState& State, const TCHAR* Who)
	{
		AOperativeCharacter* Riser = State.Riser.Get();
		UOperativeAnimInstance* Anim = AnimOf(Riser);
		if (!Riser || !Anim)
		{
			Check(State, false, FString::Printf(TEXT("7: %s for the rise check"), Who));
			return true;
		}
		switch (State.RisePhase)
		{
		case 0:
			Riser->SetStance(EOperativeStance::Prone);
			State.RisePhase = 1;
			return false;
		case 1:
			if (State.StageTime < 2.5f)
			{
				return false;
			}
			Check(State, Riser->GetStance() == EOperativeStance::Prone, FString::Printf(TEXT("7: %s lies down"), Who));
			State.bRiseSampling = true;
			Riser->SetStance(EOperativeStance::Crouching);
			State.RiseTime = State.StageTime;
			State.RisePhase = 2;
			return false;
		case 2:
		{
			if (State.StageTime < State.RiseTime + 0.15f)
			{
				return false;
			}
			State.bRiseTransitionBefore = Anim->IsPlayingStanceTransition();
			State.RiseSkippedBefore = Anim->GetFireClipsSkippedForStance();
			int32 Pellets = 0;
			Shoot(State, Riser, true, Pellets);
			State.bRiseTransitionAfter = Anim->IsPlayingStanceTransition();
			State.RisePhase = 3;
			return false;
		}
		default:
			if (State.StageTime < State.RiseTime + 1.6f)
			{
				return false;
			}
			State.bRiseSampling = false;
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunSmoke: #51 rise check %s: transition before the shot %d, after %d, torso max %.1f cm in one frame (%s)"), Who,
				State.bRiseTransitionBefore ? 1 : 0, State.bRiseTransitionAfter ? 1 : 0, State.RiseMaxPop, *State.RisePopWhen);
			Check(State, State.bRiseTransitionBefore, FString::Printf(TEXT("7: %s: the prone -> crouch clip plays when he fires 0.15 s into the rise"), Who));
			Check(State, !State.bRiseTransitionBefore || (State.bRiseTransitionAfter && Anim->GetFireClipsSkippedForStance() > State.RiseSkippedBefore),
				TEXT("7: ... the shot leaves the rise playing (no fire clip over it)"));
			Check(State, State.RiseMaxPop < PopLimitCm, FString::Printf(TEXT("7: ... the torso never pops (max %.1f cm in one frame; limit %.0f, the bug 40-80)"),
				State.RiseMaxPop, PopLimitCm));
			Riser->SetStance(EOperativeStance::Standing);
			return true;
		}
	}

	void SampleProbe(FState& State)
	{
		SampleRise(State);
		const AOperativeCharacter* Who = State.Probe.Get();
		if (!State.bProbe || !Who || !Who->GetMesh())
		{
			return;
		}
		static const FName Torso(TEXT("spine_03"));
		const FVector Local = Who->GetActorTransform().InverseTransformPosition(Who->GetMesh()->GetBoneLocation(Torso));
		if (State.bBaseline)
		{
			State.Baseline = (State.Baseline * State.BaselineSamples + Local) / (State.BaselineSamples + 1);
			++State.BaselineSamples;
			return;
		}
		State.MaxTorsoOffset = FMath::Max(State.MaxTorsoOffset, static_cast<float>(FVector::Dist(Local, State.Baseline)));
		State.MaxActorMove = FMath::Max(State.MaxActorMove, static_cast<float>(FVector::Dist2D(Who->GetActorLocation(), State.ProbeActorStart)));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		UWorld* World = WeakWorld.Get();
		FState& State = *StateRef;
		if (!World)
		{
			return false;
		}
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		switch (State.Stage)
		{
		case 0: // wait for the squad, then the loadout
		{
			if (State.Time < 2.f || !Squad || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Engineer = Member;
				}
			}
			Engineer = State.Engineer.Get();
			if (!Engineer)
			{
				Check(State, false, TEXT("the Engineer is there"));
				return Finish(State, false);
			}
			Squad->SetLeader(Engineer); // no formation walk under him
			Engineer->StopOperative();
			State.Start = Engineer->GetActorLocation();
			State.Forward = Engineer->GetActorForwardVector();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Engineer)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, State.Start - State.Forward * 900.f + Engineer->GetActorRightVector()
						* (Member->SquadRole == EOperativeRole::Commander ? -300.f : 300.f), Member), Member->GetActorRotation(), false, true);
				}
			}
			const UWeaponDataAsset* Held = Engineer->CurrentWeapon;
			Check(State, Held && Held->WeaponId == TEXT("shotgun"), FString::Printf(TEXT("1: the Engineer spawns with the shotgun in hands (%s)"),
				Held ? *Held->WeaponId : TEXT("none")));
			Check(State, Held && Held->WeaponName.ToString() == TEXT("D-1 Shotgun"), FString::Printf(TEXT("1: ... named \"D-1 Shotgun\" (%s)"),
				Held ? *Held->WeaponName.ToString() : TEXT("-")));
			Check(State, !Has(Engineer, TEXT("m16")) && Has(Engineer, TEXT("pistol")) && Has(Engineer, TEXT("grenade")),
				TEXT("1: no M16; the pistol and the grenade stay"));
			Check(State, Engineer->CurrentClip == 8 && Engineer->ReserveAmmo == 40, FString::Printf(TEXT("1: 8 shells + 40 (%d + %d)"),
				Engineer->CurrentClip, Engineer->ReserveAmmo));
			const USkeletalMesh* Model = Engineer->WeaponSkeletalMesh ? Engineer->WeaponSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
			Check(State, Model != nullptr && Engineer->WeaponSkeletalMesh->IsVisible(), FString::Printf(TEXT("1: the D-1 model on the weapon socket (%s)"),
				Model ? *Model->GetName() : TEXT("none")));
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			Check(State, Anim && Anim->GetShotgunClipCount() >= 20, FString::Printf(TEXT("1: the shotgun clip set loaded (%d clips)"), Anim ? Anim->GetShotgunClipCount() : 0));
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					Check(State, Member->CurrentWeapon && Member->CurrentWeapon->WeaponId == TEXT("m16"), TEXT("1: the Commander keeps the M16"));
				}
			}
			// The target: a frozen Frost Hound (light 0.10 armour; the shotgun is light class too: its 0.1 penetration counts).
			const FVector Spot = State.Start + State.Forward * 300.f;
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 60.f), (-State.Forward).Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
				State.Target = Hound;
			}
			Check(State, State.Target.IsValid(), TEXT("a target hound"));
			GoTo(State, 1);
			return true;
		}
		case 1: // standing: the layer owns the body
		{
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			const FString Clip = DominantName(Engineer);
			Check(State, Anim && Anim->GetShotgunLayerMask() == EWeaponLayerMask::Full && Anim->GetShotgunUpperWeight() > 0.95f,
				FString::Printf(TEXT("2: standing, the shotgun layer drives the body (mask %d, upper %.2f, lower %.2f)"), Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1,
					Anim ? Anim->GetShotgunUpperWeight() : 0.f, Anim ? Anim->GetShotgunLowerWeight() : 0.f));
			const float StandSpeed = Engineer->GetVelocity().Size2D();
			Check(State, Clip.StartsWith(TEXT("anim_shotgun_")) && (StandSpeed > 10.f || Clip.Contains(TEXT("idle"))),
				FString::Printf(TEXT("2: ... a pack idle clip standing still (%s, %.0f cm/s)"), *Clip, StandSpeed));
			// The operative's UE4 skeleton plays the IK-retargeted copies (the pack's UE5 clips bend its chest: the gun pointed sideways).
			const UAnimSequenceBase* Dominant = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			const FString ClipPath = Dominant ? Dominant->GetPathName() : FString();
			Check(State, Anim && !Anim->GetShotgunRetargetFolder().IsEmpty() && ClipPath.StartsWith(Anim->GetShotgunRetargetFolder() + TEXT("/")),
				FString::Printf(TEXT("2: ... the retargeted copy for this skeleton (%s)"), *ClipPath));
			Engineer->OrderMoveTo(SmokeUtils::ClearPoint(World, Engineer->GetActorLocation(), Engineer->GetActorLocation() + Engineer->GetActorRightVector() * 600.f), false);
			GoTo(State, 2);
			return true;
		}
		case 2: // walking: a walk / jog clip
		{
			if (Engineer->GetVelocity().Size2D() > 80.f)
			{
				const FString Clip = DominantName(Engineer);
				if (Clip.Contains(TEXT("walk")) || Clip.Contains(TEXT("jog")))
				{
					State.bSawMoveClip = true;
					State.MoveClip = Clip;
				}
			}
			if (!State.bSawMoveClip && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, State.bSawMoveClip, FString::Printf(TEXT("2: walking, a shotgun walk / jog clip plays (%s)"), *State.MoveClip));
			Engineer->StopOperative();
			GoTo(State, 3);
			return true;
		}
		case 3: // let him stop, then the forced shots
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.Forward = Engineer->GetActorForwardVector();
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			State.FireClipsBefore = Anim ? Anim->GetShotgunFireClips() : 0;
			PlaceTarget(State, 300.f);
			State.Damage3 = Shoot(State, Engineer, true, State.Pellets3);
			PlaceTarget(State, 800.f);
			State.Damage8 = Shoot(State, Engineer, true, State.Pellets8);
			PlaceTarget(State, 1450.f);
			State.Damage14 = Shoot(State, Engineer, true, State.Pellets14);
			Check(State, State.Pellets3 == 8 && State.Pellets8 == 8 && State.Pellets14 == 8,
				FString::Printf(TEXT("3: forced shots: 8 pellets each (%d / %d / %d)"), State.Pellets3, State.Pellets8, State.Pellets14));
			Check(State, State.Damage3 > 1.f && FMath::IsNearlyEqual(State.Damage8, State.Damage3, State.Damage3 * 0.02f),
				FString::Printf(TEXT("3: full damage up to the effective range (3 m %.2f, 8 m %.2f)"), State.Damage3, State.Damage8));
			const float Ratio = State.Damage3 > 0.f ? State.Damage14 / State.Damage3 : 0.f;
			Check(State, FMath::IsNearlyEqual(Ratio, 1.f - 0.6f * (6.5f / 7.f), 0.03f), FString::Printf(TEXT("3: 14.5 m: x%.3f of it (falloff to 0.4 at 15 m: x0.443; %.2f)"), Ratio, State.Damage14));
			PlaceTarget(State, 800.f);
			GoTo(State, 4);
			return true;
		}
		case 4: // free shots at 8 m: partial pellet hits, damage proportional
		{
			if (State.FreeShots < 12)
			{
				int32 Pellets = 0;
				const float Damage = Shoot(State, Engineer, false, Pellets);
				++State.FreeShots;
				State.PartialShots += Pellets > 0 && Pellets < 8 ? 1 : 0;
				if (Pellets > 0)
				{
					const float Expected = State.Damage8 * Pellets / 8.f;
					State.WorstPerPelletError = FMath::Max(State.WorstPerPelletError, FMath::Abs(Damage - Expected) / FMath::Max(1.f, Expected));
				}
				UE_LOG(LogCodexTactics, Display, TEXT("ShotgunSmoke: free shot at 8 m: %d pellets, %.2f damage"), Pellets, Damage);
				return true;
			}
			Check(State, State.PartialShots >= 1, FString::Printf(TEXT("3: free shots at 8 m: some hit with only part of the pellets (%d of %d)"), State.PartialShots, State.FreeShots));
			Check(State, State.WorstPerPelletError < 0.05f, FString::Printf(TEXT("3: ... the damage = the pellets that hit x 1/8 of the shot (worst error %.1f %%)"),
				State.WorstPerPelletError * 100.f));
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			Check(State, Anim && Anim->GetShotgunFireClips() - State.FireClipsBefore >= 10, FString::Printf(TEXT("3: every shot plays the shotgun fire clip (%d)"),
				Anim ? Anim->GetShotgunFireClips() - State.FireClipsBefore : 0));
			// The per-shell reload from empty (a cease fire freezes the reload: off; the target parked so nothing interrupts it).
			Engineer->bTacticalCeaseFire = false;
			SmokeUtils::ParkEnemyFarAway(State.Target.Get());
			Engineer->CurrentClip = 0;
			Engineer->ReserveAmmo = 40;
			State.ShellTimes.Reset();
			State.LastClip = 0;
			State.ShellClipsBefore = Anim ? Anim->GetShotgunShellClips() : 0;
			Engineer->StartReload();
			Check(State, Engineer->bIsReloading, TEXT("4: the reload starts"));
			GoTo(State, 5);
			return true;
		}
		case 5: // shells one by one
		{
			if (Engineer->CurrentClip > State.LastClip)
			{
				State.ShellTimes.Add(State.StageTime);
				State.LastClip = Engineer->CurrentClip;
			}
			if (Engineer->bIsReloading && State.StageTime < 8.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			const float Total = State.ShellTimes.IsEmpty() ? 0.f : State.ShellTimes.Last();
			Check(State, Engineer->CurrentClip == 8 && Engineer->ReserveAmmo == 32 && State.ShellTimes.Num() == 8,
				FString::Printf(TEXT("4: one shell at a time, 8 steps (clip %d, reserve %d, %d steps)"), Engineer->CurrentClip, Engineer->ReserveAmmo, State.ShellTimes.Num()));
			Check(State, Total > 3.5f && Total < 4.6f, FString::Printf(TEXT("4: empty -> full in ~4 s (%.1f s; was 2.4 s for the magazine)"), Total));
			Check(State, Anim && Anim->GetShotgunShellClips() - State.ShellClipsBefore >= 8, FString::Printf(TEXT("4: a reload clip per shell (%d)"),
				Anim ? Anim->GetShotgunShellClips() - State.ShellClipsBefore : 0));
			// Interruption: 2 shells in, then a quick fire order.
			Engineer->CurrentClip = 0;
			Engineer->ReserveAmmo = 40;
			Engineer->StartReload();
			GoTo(State, 6);
			return true;
		}
		case 6:
		{
			if (Engineer->CurrentClip < 2 && State.StageTime < 3.f)
			{
				return true;
			}
			Engineer->bTacticalCeaseFire = false;
			Check(State, Engineer->OrderQuickFire(State.Target.IsValid() ? State.Target->GetActorLocation() : Engineer->GetActorLocation() + State.Forward * 800.f, nullptr),
				TEXT("4: a quick fire order between shells"));
			GoTo(State, 7);
			return true;
		}
		case 7:
		{
			if (Engineer->bIsReloading && State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, !Engineer->bIsReloading && Engineer->CurrentClip < 8, FString::Printf(TEXT("4: ... stops the reload with the shells in (clip %d, reloading %d)"),
				Engineer->CurrentClip, Engineer->bIsReloading ? 1 : 0));
			Engineer->bTacticalCeaseFire = true;
			// A PMC fighter given the shotgun.
			UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
			AOperativeCharacter* Fighter = Pmc ? Pmc->SpawnFighter(1, SmokeUtils::FreeSpot(World, State.Start + Engineer->GetActorRightVector() * -700.f), State.Forward.Rotation(), 7, 0) : nullptr;
			Check(State, Fighter != nullptr, TEXT("5: a PMC fighter"));
			if (!Fighter)
			{
				return Finish(State, false);
			}
			Fighter->bTacticalCeaseFire = true;
			Fighter->HealthComponent->SetMaxHealth(100000.f, true);
			if (UWeaponDataAsset* Shotgun = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_shotgun.DA_Weapon_shotgun")))
			{
				Fighter->AddArsenalWeapon(Shotgun);
			}
			Fighter->SwitchToWeaponById(TEXT("shotgun"));
			State.Fighter = Fighter;
			GoTo(State, 8);
			return true;
		}
		case 8:
		{
			AOperativeCharacter* Fighter = State.Fighter.Get();
			if (State.StageTime < 1.5f || !Fighter)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Fighter);
			const FString Clip = DominantName(Fighter);
			Check(State, Fighter->CurrentWeapon && Fighter->CurrentWeapon->WeaponId == TEXT("shotgun") && Anim && Anim->GetShotgunLayerMask() != EWeaponLayerMask::Off
				&& Clip.StartsWith(TEXT("anim_shotgun_")), FString::Printf(TEXT("5: the PMC fighter with the shotgun runs the same layer (%s, body %s)"), *Clip,
					Fighter->GetMesh() && Fighter->GetMesh()->GetSkeletalMeshAsset() ? *Fighter->GetMesh()->GetSkeletalMeshAsset()->GetName() : TEXT("?")));
			const UAnimSequenceBase* FighterClip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			Check(State, FighterClip && FighterClip->GetPathName().StartsWith(TEXT("/Game/ShotgunLocomotionPack/")),
				FString::Printf(TEXT("5: ... the pack clips themselves (Manny-hierarchy skeleton: %s)"), FighterClip ? *FighterClip->GetPathName() : TEXT("none")));
			// #51 probe: back to the M16 (the video's case), the target in front of him, the torso measured over 3 shots.
			Fighter->SwitchToWeaponById(TEXT("m16"));
			if (AEnemyCharacter* Target = State.Target.Get())
			{
				const FVector Spot = Fighter->GetActorLocation() + Fighter->GetActorForwardVector() * 800.f;
				Target->TeleportTo(FVector(Spot.X, Spot.Y, Target->GetActorLocation().Z), (-Fighter->GetActorForwardVector()).Rotation(), false, true);
			}
			StartProbe(State, Fighter);
			GoTo(State, 9);
			return true;
		}
		case 9: // baseline 0.6 s, then a shot every 0.8 s
		case 10:
		{
			AOperativeCharacter* Who = State.Probe.Get();
			if (!Who)
			{
				return Finish(State, false);
			}
			if (State.bBaseline)
			{
				if (State.StageTime >= 0.6f)
				{
					State.bBaseline = false;
					State.StageTime = 0.f;
				}
				return true;
			}
			if (State.ProbeShots < 3)
			{
				if (State.StageTime >= 0.8f * State.ProbeShots)
				{
					int32 Pellets = 0;
					Shoot(State, Who, true, Pellets);
					++State.ProbeShots;
				}
				return true;
			}
			if (State.StageTime < 0.8f * 3.f + 0.6f)
			{
				return true;
			}
			State.bProbe = false;
			if (State.Stage == 9)
			{
				State.FighterTorso = State.MaxTorsoOffset;
				State.FighterActor = State.MaxActorMove;
				UE_LOG(LogCodexTactics, Display, TEXT("ShotgunSmoke: #51 PMC fighter (M16): torso max %.1f cm off its place, capsule moved %.1f cm"), State.FighterTorso, State.FighterActor);
				// The real #51 case: the fighter fires while he gets up from prone.
				StartRise(State, State.Fighter.Get());
				GoTo(State, 11);
				return true;
			}
			State.EngineerTorso = State.MaxTorsoOffset;
			State.EngineerActor = State.MaxActorMove;
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunSmoke: #51 Engineer (D-1): torso max %.1f cm off its place, capsule moved %.1f cm"), State.EngineerTorso, State.EngineerActor);
			Check(State, State.FighterTorso < RecoilLimitCm && State.FighterActor < 5.f, FString::Printf(TEXT("6: #51 the firing PMC fighter is not thrown back (torso %.1f cm, capsule %.1f cm; limit %.0f)"),
				State.FighterTorso, State.FighterActor, RecoilLimitCm));
			Check(State, State.EngineerTorso < RecoilLimitCm && State.EngineerActor < 5.f, FString::Printf(TEXT("6: ... nor the Engineer with the D-1 (torso %.1f cm, capsule %.1f cm)"),
				State.EngineerTorso, State.EngineerActor));
			return Finish(State, true);
		}
		case 11: // #51 rise check: the PMC fighter (M16)
			if (StepRise(State, TEXT("the PMC fighter (M16)")))
			{
				if (AEnemyCharacter* Target = State.Target.Get())
				{
					const FVector Spot = Engineer->GetActorLocation() + Engineer->GetActorForwardVector() * 600.f;
					Target->TeleportTo(FVector(Spot.X, Spot.Y, Target->GetActorLocation().Z), (-Engineer->GetActorForwardVector()).Rotation(), false, true);
				}
				StartRise(State, Engineer);
				GoTo(State, 12);
			}
			return true;
		case 12: // ... and the Engineer (D-1), then his recoil probe standing
			if (StepRise(State, TEXT("the Engineer (D-1)")))
			{
				GoTo(State, 13);
			}
			return true;
		case 13: // let him stand up before the recoil probe
			if (State.StageTime >= 1.5f)
			{
				StartProbe(State, Engineer);
				GoTo(State, 10);
			}
			return true;
		default:
			Check(State, false, FString::Printf(TEXT("unknown stage %d"), State.Stage));
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		// The damage ratios are compared shot by shot: no knockdown (a downed target takes x1.33).
		if (IConsoleVariable* Chance = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
		{
			Chance->Set(0.f, ECVF_SetByConsole);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleProbe(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ShotgunSmoke"),
		TEXT("Dev check: the D-1 shotgun (Engineer loadout, pellets, falloff, per-shell reload, the shotgun anim layer, a PMC fighter's layer, #51 no knock-back); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
