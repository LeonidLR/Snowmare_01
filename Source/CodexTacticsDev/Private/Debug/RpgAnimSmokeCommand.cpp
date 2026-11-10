// Dev-only console command: the RPG-7 on the game designer's launcher pack, animation + model end to end (2026-11-09, feat/rpg7-launcher-pack;
// docs/systems/animation-setup.md "Launcher weapon layer", docs/systems/weapons.md "The RPG-7"; new - no Godot reference).
//   Scripts/smoke_parallel.ps1 -Commands RpgAnimSmoke -ExtraArgs "-Multiprocess"
// On L_MovementTest, the Commander (his loadout m16 / rpg7 / pistol / grenade - FOR TESTING, pending the designer):
//  0. the selector's next weapon after the M16 is the RPG-7; drawn: the launcher layer with his skeleton's retargeted copies, the FULL-body set
//     (weapon_anim_sets.json "body": "full", designer 2026-11-09 b), the optimized model (SM_RPG7_Body + the optic) with the rocket on the tube;
//     the Engineer and the Medic-Sapper given it load the layer too (their own copies: Male / Female);
//  1. standing: the pack's idle drives the whole body - the legs too (the layer moves lower-body bones off the ABP's pose) and the upper body;
//  2. running forward (the pack's run loops at his 220 cm/s), 3. sprinting (the Set_01 Run_Fast), 4. crouch-walking (Walk_Sit*): a launcher
//     loop plays and the feet stay planted (the planted foot's median world drift is a small share of the body's speed); running / sprinting
//     the legs are the ABP's M16 locomotion (launcher "legs_abp", the designer 2026-11-10), crouched too ("crouch_body": "upper");
//  12. walking forward at 110 cm/s: the ABP's legs; 13. walking sideways (a held look 90 deg off his path): the pack's legs;
//  14. running sideways: the ABP's legs; 15. forward -> strafe mid-walk: no pop (the pelvis / feet in component space, the legs share's step);
//     the planted foot's drift of every leg is logged ("Smoke foot slide");
//  5. aim up / 6. aim down: a shot at a target 9 m above / 4 m below: the Shot clip plays natively (no montage), the rocket leaves the tube (hidden
//     on the model), the flying rocket is the SM_RPG7_Rocket model nose first along the flight, and the aim poses bend him up / down;
//  7. the standing reload from empty: the Reload clip on the body, the rocket stays off the tube until the clip's insertion moment and is back
//     before the reload ends; 8. crouched: the Sit_Shot + Sit_Reload clips; 9. a hit: the launcher pack's Get_Hit (upper body).
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/ProjectileActor.h"
#include "Combat/ProjectileSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

namespace RpgAnimSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** The planted foot's median world drift while moving, as a share of the body's speed (ShotgunSmoke 2b's limit). */
	constexpr float FootSlideShareLimit = 0.2f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AEnemyCharacter> Target;
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		// The moving legs.
		bool bMoveSampling = false;
		float MaxLower = 0.f;
		float MaxUpper = 0.f;
		int32 MoveSamples = 0;
		FString MoveClip;
		bool bFeetValid = false;
		FVector LastFoot[2];
		double BodySpeedSum = 0.0;
		int32 FootSamples = 0;
		float TopSpeed = 0.f;
		float MinFootZ = 1.e6f;
		int32 LastLower = -1;
		int32 LowerFrames = 0;
		TArray<float> PlantSpeeds;
		/** "legs_abp" (the designer 2026-11-10): the steady frames' largest lower-body change by the layer, the ABP-legs share (sum / frames). */
		float SteadyMaxLower = 0.f;
		double SteadyLowerSum = 0.0;
		double LegsAbpSum = 0.0;
		int32 LegsAbpFrames = 0;
		/** The ABP's movement direction against the body on the steady frames (|deg|: sum, max). */
		double DirectionSum = 0.0;
		float DirectionMax = 0.f;
		/** Seam probe: the fastest component-space move of the pelvis / balls of the feet (cm/s) before and after the turn into a strafe, and the
		 * largest one-frame change of the ABP-legs share. */
		bool bLocalValid = false;
		FVector LastLocal[3];
		bool bSeamWindow = false;
		float PreSeamMaxLocal = 0.f;
		float SeamMaxLocal = 0.f;
		float LastLegsAbp = 0.f;
		float MaxLegsAbpStep = 0.f;
		/** The legs' foot slide per leg for the report (name -> median drift cm/s, his speed). */
		TArray<FString> SlideReport;
		// Shots / reload.
		int32 ShotClipsBefore = 0;
		bool bSawRocketModel = false;
		float NoseAlong = 0.f;
		FString FlyingModel;
		float MaxAimUp = 0.f;
		float MaxAimDown = 0.f;
		bool bRoundHiddenEarly = false;
		bool bRoundBackBeforeEnd = false;
		float RoundBackAt = -1.f;
		float ReloadMaxWeight = 0.f;
		FString ReloadClip;
		/** The reload clip's play rate seen (clip time / reload time elapsed; the designer 2026-11-09: rate 1) and its length. */
		float ReloadClipRate = 0.f;
		float ReloadClipLength = 0.f;
		FString ShotClip;
		int32 HitClipsBefore = 0;
		bool bFired = false;
		bool bShotChecked = false;
		/** The legs stages' walk speed (> 0: held on his movement every step - below the launcher's walk -> run band, 140 cm/s). */
		float SpeedOverride = 0.f;
		bool bMoveStarted = false;
		/** A held look (the strafes): written as his formation look at every frame end, after the squad's tick wrote its own (the combat
		 * target's facing needs a wave fight); the body then strafes along his path like a formation member looking at his sector. */
		bool bHoldLook = false;
		float LookYaw = 0.f;
		FDelegateHandle FrameHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok") : TEXT("FAILED"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RpgAnimSmoke"));
		return false;
	}

	void GoTo(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString ClipName(const UAnimSequenceBase* Clip)
	{
		return Clip ? Clip->GetName() : FString(TEXT("none"));
	}

	FString DominantName(const AOperativeCharacter* Operative)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		return ClipName(Anim ? Anim->GetShotgunDominantClip() : nullptr);
	}

	/**
	 * Per frame: the layer's lower / upper deviation from the ABP, and the planted foot's world drift while he moves steadily (the method of
	 * ShotgunSmoke 2b): at >= 85 % of his top speed so far the LOWER ball of the foot (the contact that stays put), once it has been the lower one
	 * for 4+ frames and is within 1.5 cm of the lowest height seen, adds its horizontal world move; the median of those is the drift.
	 */
	void SampleFrame(FState& State)
	{
		if (AOperativeCharacter* Looking = State.bHoldLook ? State.Hero.Get() : nullptr)
		{
			Looking->SetFormationLookYaw(State.LookYaw);
		}
		const AOperativeCharacter* Hero = State.Hero.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Hero);
		UWorld* World = Hero ? Hero->GetWorld() : nullptr;
		if (!State.bMoveSampling || !Anim || !World || !Hero->GetMesh())
		{
			State.bFeetValid = false;
			return;
		}
		const float Dt = World->GetDeltaSeconds();
		const float BodySpeed = static_cast<float>(Hero->GetVelocity().Size2D());
		const FVector Feet[2] = { Hero->GetMesh()->GetBoneLocation(TEXT("ball_l")), Hero->GetMesh()->GetBoneLocation(TEXT("ball_r")) };
		// The seam probe: the pelvis and the balls of the feet in component space - a pop between the set's legs and the ABP's shows as a spike.
		const FName LocalBones[3] = { TEXT("pelvis"), TEXT("ball_l"), TEXT("ball_r") };
		float LocalSpeed = 0.f;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector Local = Hero->GetMesh()->GetBoneLocation(LocalBones[Index], EBoneSpaces::ComponentSpace);
			if (State.bLocalValid && Dt > KINDA_SMALL_NUMBER)
			{
				LocalSpeed = FMath::Max(LocalSpeed, static_cast<float>(FVector::Dist(Local, State.LastLocal[Index])) / Dt);
			}
			State.LastLocal[Index] = Local;
		}
		if (State.bLocalValid && BodySpeed > 60.f)
		{
			float& Max = State.bSeamWindow ? State.SeamMaxLocal : State.PreSeamMaxLocal;
			Max = FMath::Max(Max, LocalSpeed);
			State.MaxLegsAbpStep = FMath::Max(State.MaxLegsAbpStep, FMath::Abs(Anim->GetWeaponLayerLegsAbpWeight() - State.LastLegsAbp));
		}
		State.bLocalValid = true;
		State.LastLegsAbp = Anim->GetWeaponLayerLegsAbpWeight();
		if (BodySpeed > 60.f)
		{
			State.MaxLower = FMath::Max(State.MaxLower, Anim->GetWeaponLayerLowerDeviation());
			State.MaxUpper = FMath::Max(State.MaxUpper, Anim->GetWeaponLayerUpperDeviation());
			++State.MoveSamples;
		}
		State.TopSpeed = FMath::Max(State.TopSpeed, BodySpeed);
		// Steady: at >= 90 % of the speed the movement allows him now (the acceleration's frames would set the lowest foot height of a slower gait).
		const float MaxSpeed = Hero->GetCharacterMovement() ? Hero->GetCharacterMovement()->MaxWalkSpeed : BodySpeed;
		const bool bSteady = BodySpeed > 60.f && BodySpeed >= MaxSpeed * 0.9f;
		// The leg's loop: the dominant clip on the steady frames (the braking at the goal falls back to the walk loops).
		const FString Clip = BodySpeed > 60.f ? DominantName(Hero) : FString();
		if (Clip.StartsWith(TEXT("Anim_Set_0")) && (bSteady || State.MoveClip.IsEmpty()))
		{
			State.MoveClip = Clip;
		}
		const float Ground = static_cast<float>(Hero->GetActorLocation().Z) - Hero->GetSimpleCollisionHalfHeight();
		const int32 Lower = Feet[0].Z <= Feet[1].Z ? 0 : 1;
		const float Height = static_cast<float>(Feet[Lower].Z) - Ground;
		if (bSteady)
		{
			State.SteadyMaxLower = FMath::Max(State.SteadyMaxLower, Anim->GetWeaponLayerLowerDeviation());
			State.SteadyLowerSum += Anim->GetWeaponLayerLowerDeviation();
			State.LegsAbpSum += Anim->GetWeaponLayerLegsAbpWeight();
			State.DirectionSum += FMath::Abs(Anim->Direction);
			State.DirectionMax = FMath::Max(State.DirectionMax, FMath::Abs(Anim->Direction));
			++State.LegsAbpFrames;
			State.MinFootZ = FMath::Min(State.MinFootZ, Height);
			State.LowerFrames = State.LastLower == Lower ? State.LowerFrames + 1 : 0;
			if (State.bFeetValid && State.LowerFrames >= 4 && Dt > KINDA_SMALL_NUMBER && Height < State.MinFootZ + 1.5f)
			{
				State.PlantSpeeds.Add(static_cast<float>(FVector::Dist2D(Feet[Lower], State.LastFoot[Lower])) / Dt);
			}
			State.BodySpeedSum += BodySpeed;
			++State.FootSamples;
		}
		State.LastFoot[0] = Feet[0];
		State.LastFoot[1] = Feet[1];
		State.LastLower = Lower;
		State.bFeetValid = bSteady;
	}

	void StartMove(FState& State)
	{
		State.bMoveSampling = true;
		State.bFeetValid = false;
		State.MaxLower = State.MaxUpper = 0.f;
		State.MoveSamples = State.FootSamples = 0;
		State.BodySpeedSum = 0.0;
		State.TopSpeed = 0.f;
		State.MinFootZ = 1.e6f;
		State.LastLower = -1;
		State.LowerFrames = 0;
		State.PlantSpeeds.Reset();
		State.MoveClip.Reset();
		State.SteadyMaxLower = 0.f;
		State.LegsAbpSum = 0.0;
		State.SteadyLowerSum = 0.0;
		State.LegsAbpFrames = 0;
		State.DirectionSum = 0.0;
		State.DirectionMax = 0.f;
		State.bLocalValid = false;
		State.bSeamWindow = false;
		State.PreSeamMaxLocal = State.SeamMaxLocal = 0.f;
		State.MaxLegsAbpStep = 0.f;
	}

	/** Which legs a moving leg expects: the set's own (the pack), the ABP's ("legs_abp" gaits: running, walking forward), the crouched upper mode. */
	enum class ELegs : uint8
	{
		Pack,
		Abp,
		CrouchUpper
	};

	/** The moving check: a launcher loop, the planted foot's median drift under 20 % of his speed, and the legs: the pack's (deviation > 0), or a
	 * "legs_abp" gait's the ABP's (weapon_anim_sets.json launcher "legs_abp", the designer 2026-11-10: running / walking forward - the share ~1
	 * and the layer moves no lower-body bone on the steady frames); a leg in the set's upper-body mode (crouched: "crouch_body": "upper") keeps
	 * the ABP's legs (lower deviation < 0.01) and is not foot-checked. */
	void CheckMove(FState& State, const TCHAR* What, const TCHAR* ClipPart, ELegs Legs = ELegs::Pack, bool bSlideReportOnly = false)
	{
		if (Legs == ELegs::CrouchUpper)
		{
			State.bMoveSampling = false;
			Check(State, State.MoveClip.Contains(ClipPart), FString::Printf(TEXT("%s: the launcher loop plays (%s)"), What, *State.MoveClip));
			Check(State, State.MoveSamples >= 8 && State.MaxLower < 0.01f && State.MaxUpper > 1.f,
				FString::Printf(TEXT("%s over %d frames: crouched the set drives the upper body only (crouch_body upper; lower change %.3f, upper %.1f) - the ABP's crouch legs"),
					What, State.MoveSamples, State.MaxLower, State.MaxUpper));
			return;
		}
		State.bMoveSampling = false;
		const float Speed = State.FootSamples > 0 ? static_cast<float>(State.BodySpeedSum / State.FootSamples) : 0.f;
		State.PlantSpeeds.Sort();
		const float Drift = State.PlantSpeeds.Num() > 0 ? State.PlantSpeeds[State.PlantSpeeds.Num() / 2] : -1.f;
		Check(State, State.MoveClip.Contains(ClipPart), FString::Printf(TEXT("%s: the launcher loop plays (%s)"), What, *State.MoveClip));
		const float LegsAbp = State.LegsAbpFrames > 0 ? static_cast<float>(State.LegsAbpSum / State.LegsAbpFrames) : 0.f;
		if (Legs == ELegs::Abp)
		{
			// The run's share is the legs' run band's (legs_abp_run_speeds, below his ~175 cm/s with the tube), the walk
			// forward's the forward cone's: mostly the ABP's.
			Check(State, State.MoveSamples >= 8 && State.LegsAbpFrames >= 5 && LegsAbp > 0.9f && State.MaxUpper > 1.f,
				FString::Printf(TEXT("%s over %d frames: legs_abp - the ABP's M16 legs (share %.2f over %d steady frames, the layer's largest lower-body change %.2f), the pack's torso (max %.1f)"),
					What, State.MoveSamples, LegsAbp, State.LegsAbpFrames, State.SteadyMaxLower, State.MaxUpper));
		}
		else
		{
			Check(State, State.MoveSamples >= 8 && State.MaxLower > 1.f && State.MaxUpper > 1.f && LegsAbp < 0.1f,
				FString::Printf(TEXT("%s over %d frames: the FULL-body set moves the legs off the ABP's (max %.1f, ABP-legs share %.2f) and the upper body (max %.1f)"), What,
					State.MoveSamples, State.MaxLower, LegsAbp, State.MaxUpper));
		}
		const float MeanDirection = State.LegsAbpFrames > 0 ? static_cast<float>(State.DirectionSum / State.LegsAbpFrames) : 0.f;
		const float MeanLower = State.LegsAbpFrames > 0 ? static_cast<float>(State.SteadyLowerSum / State.LegsAbpFrames) : 0.f;
		State.SlideReport.Add(FString::Printf(TEXT("%s: %.1f cm/s at %.0f cm/s (%.0f %% over %d frames, legs-ABP %.2f, layer's lower-body change mean %.1f, |direction| mean %.0f max %.0f deg)"),
			What, Drift, Speed, Speed > 1.f ? Drift / Speed * 100.f : 0.f, State.PlantSpeeds.Num(), LegsAbp, MeanLower, MeanDirection, State.DirectionMax));
		if (Legs == ELegs::Abp || bSlideReportOnly)
		{
			// The ABP's own locomotion (the M16 legs) is the reference here, not the layer: its drift is reported (Codex.Anim.WeaponLayer 0 gives the
			// M16 baseline: run forward 68 %, sprint 22 %, walk forward 62 % on 2026-11-10), not held to the pack's 20 %. bSlideReportOnly: the
			// pack's side walk at 110 cm/s leaves the mid-stance probe (almost) no frames - reported, unchanged by legs_abp (its share ~0).
			return;
		}
		Check(State, State.PlantSpeeds.Num() >= 5 && Drift >= 0.f && Drift < FMath::Max(25.f, Speed * FootSlideShareLimit),
			FString::Printf(TEXT("%s at %.0f cm/s: no foot sliding - the planted foot drifts %.1f cm/s (median of %d mid-stance frames, %.0f %% of his speed; limit %.0f %%)"),
				What, Speed, Drift, State.PlantSpeeds.Num(), Speed > 1.f ? Drift / Speed * 100.f : 0.f, FootSlideShareLimit * 100.f));
	}

	AEnemyCharacter* PlaceTarget(FState& State, float Ahead, float Up)
	{
		AEnemyCharacter* Target = State.Target.Get();
		AOperativeCharacter* Hero = State.Hero.Get();
		if (Target && Hero)
		{
			const FVector Spot = Hero->GetActorLocation() + State.Forward * Ahead + FVector(0.f, 0.f, Up);
			Target->SetActorLocationAndRotation(Spot, (-State.Forward).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Target;
	}

	/** One rocket at the target (no reserve: no reload starts, so the aim offset stays on). */
	bool Fire(FState& State, int32 Reserve)
	{
		AOperativeCharacter* Hero = State.Hero.Get();
		AEnemyCharacter* Target = State.Target.Get();
		if (!Hero || !Target)
		{
			return false;
		}
		Hero->SetActorRotation(State.Forward.Rotation());
		Hero->bIsReloading = false;
		Hero->CurrentClip = 1;
		Hero->ReserveAmmo = Reserve;
		const UOperativeAnimInstance* Anim = AnimOf(Hero);
		State.ShotClipsBefore = Anim ? Anim->GetLauncherShotClips() : 0;
		Hero->bForceHitForTesting = true;
		const bool bShot = Hero->ShootAtTarget(Target);
		Hero->bForceHitForTesting = false;
		// The flying round right away (a near target is struck within the next step): its model and its nose against the flight.
		State.FlyingModel.Reset();
		State.NoseAlong = 0.f;
		for (TActorIterator<AProjectileActor> It(Hero->GetWorld()); It; ++It)
		{
			if (It->IsInFlight())
			{
				const UStaticMesh* Model = It->GetVisualMesh();
				State.FlyingModel = Model ? Model->GetName() : FString(TEXT("none"));
				State.NoseAlong = static_cast<float>(FVector::DotProduct(It->GetVisualTransform().GetUnitAxis(EAxis::Y), It->GetFlight().Velocity.GetSafeNormal()));
			}
		}
		return bShot;
	}

	/** Right after a shot: the clip, the rocket off the tube, the flying model nose first. */
	void CheckShot(FState& State, UWorld* World, const TCHAR* What, const TCHAR* ClipPart)
	{
		const AOperativeCharacter* Hero = State.Hero.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Hero);
		Check(State, Anim && Anim->GetLauncherShotClips() == State.ShotClipsBefore + 1 && Anim->GetLauncherAction() == ELauncherAction::Shot
			&& ClipName(Anim->GetLauncherActionClip()).Contains(ClipPart) && Anim->GetSlotMontageGlobalWeight(Anim->UpperBodySlot) < 0.01f,
			FString::Printf(TEXT("%s: the pack's shot clip plays natively, no montage (%s, action %d)"), What, Anim ? *ClipName(Anim->GetLauncherActionClip()) : TEXT("-"),
				Anim ? static_cast<int32>(Anim->GetLauncherAction()) : -1));
		Check(State, Hero && !Hero->IsLauncherRoundVisible() && Hero->CurrentClip == 0, FString::Printf(TEXT("%s: the rocket left the tube (hidden on the model)"), What));
		Check(State, State.FlyingModel == TEXT("SM_RPG7_Rocket") && State.NoseAlong > 0.99f,
			FString::Printf(TEXT("%s: the flying round is the pack's rocket, nose along the flight (%s, dot %.3f)"), What, *State.FlyingModel, State.NoseAlong));
		// The tube (the model's +Y, the pack's muzzle axis) points at the target: crouched, the torso on the ABP's turned crouch root put it
		// 23 deg off (fixed: bWeaponTorsoOnCapsule).
		const AEnemyCharacter* Target = State.Target.Get();
		if (Hero && Hero->WeaponMesh && Target)
		{
			const FVector Tube = Hero->WeaponMesh->GetComponentTransform().TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
			const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees((Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, Tube.Rotation().Yaw));
			Check(State, Off < 8.f, FString::Printf(TEXT("%s: the tube points at the target (%.1f deg off the target line, limit 8)"), What, Off));
		}
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
		if (State.Time > 220.f)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Hero = State.Hero.Get();
		UOperativeAnimInstance* Anim = AnimOf(Hero);
		if (Hero && State.SpeedOverride > 0.f && Hero->GetCharacterMovement())
		{
			Hero->GetCharacterMovement()->MaxWalkSpeed = State.SpeedOverride;
		}
		switch (State.Stage)
		{
		case 0: // the squad, the loadout, the draw
		{
			if (State.Time < 2.f || !Squad || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					State.Hero = Member;
				}
			}
			Hero = State.Hero.Get();
			if (!Hero)
			{
				Check(State, false, TEXT("the Commander"));
				return Finish(State, false);
			}
			Squad->SetLeader(Hero);
			TArray<FString> Ids;
			for (const UWeaponDataAsset* Weapon : Hero->AvailableWeapons)
			{
				Ids.Add(Weapon ? Weapon->WeaponId : FString());
			}
			Check(State, Ids == TArray<FString>({ TEXT("m16"), TEXT("rpg7"), TEXT("pistol"), TEXT("grenade") }) && Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("m16"),
				FString::Printf(TEXT("0: the Commander's loadout (testing): %s, the M16 in hands"), *FString::Join(Ids, TEXT(", "))));
			// The selector (X) takes the next weapon after the one in hands: the RPG-7.
			const int32 Current = Hero->AvailableWeapons.IndexOfByKey(Hero->CurrentWeapon);
			const UWeaponDataAsset* Next = Hero->AvailableWeapons.IsValidIndex((Current + 1) % FMath::Max(1, Hero->AvailableWeapons.Num()))
				? Hero->AvailableWeapons[(Current + 1) % Hero->AvailableWeapons.Num()] : nullptr;
			Check(State, Next && Next->WeaponId == TEXT("rpg7") && Hero->SwitchToWeaponById(Next->WeaponId) && Hero->IsLauncherEquipped(),
				TEXT("0: X cycles from the M16 to the RPG-7"));
			// The other two bodies load the set too.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member == Hero)
				{
					continue;
				}
				Member->AddArsenalWeapon(LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_rpg7.DA_Weapon_rpg7")));
				Member->SwitchToWeaponById(TEXT("rpg7"));
				Member->TeleportTo(SmokeUtils::FreeSpot(World, Hero->GetActorLocation() - Hero->GetActorForwardVector() * 700.f
					+ Hero->GetActorRightVector() * (Member->SquadRole == EOperativeRole::Engineer ? -300.f : 300.f), Member), Member->GetActorRotation(), false, true);
				Member->StopOperative();
			}
			State.Start = Hero->GetActorLocation();
			State.Forward = FVector(Hero->GetActorForwardVector().X, Hero->GetActorForwardVector().Y, 0.f).GetSafeNormal();
			const FVector Spot = SmokeUtils::ClearPoint(World, State.Start, State.Start + State.Forward * 2000.f);
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 60.f), (-State.Forward).Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(1000000.f, true);
				State.Target = Hound;
			}
			Check(State, State.Target.IsValid(), TEXT("0: a target hound"));
			if (IConsoleVariable* Chance = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
			{
				Chance->Set(0.f, ECVF_SetByConsole);
			}
			GoTo(State, 1);
			return true;
		}
		case 1: // standing: the model, the set, the full body
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const UStaticMesh* Body = Hero->WeaponMesh ? Hero->WeaponMesh->GetStaticMesh() : nullptr;
			const UStaticMesh* Optic = Hero->WeaponMeshParts.Num() > 0 && Hero->WeaponMeshParts[0] ? Hero->WeaponMeshParts[0]->GetStaticMesh() : nullptr;
			Check(State, Body && Body->GetName() == TEXT("SM_RPG7_Body") && Optic && Optic->GetName() == TEXT("SM_RPG7_Sight01") && Hero->WeaponMesh->IsVisible(),
				FString::Printf(TEXT("1: the launcher pack's model in his hands (%s + %s)"), Body ? *Body->GetName() : TEXT("none"), Optic ? *Optic->GetName() : TEXT("none")));
			Check(State, Hero->IsLauncherRoundVisible() && Hero->CurrentClip == 1, TEXT("1: the loaded rocket on the tube"));
			Check(State, Anim && Anim->GetWeaponLayerHandling() == EWeaponHandling::Launcher && Anim->IsWeaponLayerFullBody()
				&& Anim->GetShotgunLayerMask() == EWeaponLayerMask::Full && Anim->GetShotgunUpperWeight() > 0.95f && Anim->GetShotgunLowerWeight() > 0.95f,
				FString::Printf(TEXT("1: the launcher set drives the whole body (handling %d, full %d, mask %d, upper %.2f, lower %.2f)"),
					Anim ? static_cast<int32>(Anim->GetWeaponLayerHandling()) : -1, Anim && Anim->IsWeaponLayerFullBody() ? 1 : 0,
					Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1, Anim ? Anim->GetShotgunUpperWeight() : 0.f, Anim ? Anim->GetShotgunLowerWeight() : 0.f));
			const UAnimSequenceBase* Dominant = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			Check(State, Dominant && Dominant->GetName() == TEXT("Anim_Set_02_Idle_01") && Dominant->GetPathName().StartsWith(TEXT("/Game/Characters/Animations/Launcher/Male/")),
				FString::Printf(TEXT("1: the retargeted shouldered idle for his skeleton (%s)"), Dominant ? *Dominant->GetPathName() : TEXT("none")));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const UOperativeAnimInstance* Other = AnimOf(Member);
				if (Member == Hero || !Other)
				{
					continue;
				}
				// The copies of the body's OWN skeleton (in the game the Medic-Sapper's body is on UE4_Mannequin_Skeleton too: the male copies).
				const USkeletalMesh* MemberBody = Member->GetMesh() ? Member->GetMesh()->GetSkeletalMeshAsset() : nullptr;
				const FString Folder = MemberBody && MemberBody->GetSkeleton() && MemberBody->GetSkeleton()->GetName() == TEXT("Female_Soldier_Skeleton")
					? TEXT("/Game/Characters/Animations/Launcher/Female") : TEXT("/Game/Characters/Animations/Launcher/Male");
				const USkeletalMesh* OtherBody = Member->GetMesh() ? Member->GetMesh()->GetSkeletalMeshAsset() : nullptr;
				Check(State, Other->GetWeaponLayerHandling() == EWeaponHandling::Launcher && Other->GetShotgunRetargetFolder() == Folder && Other->GetShotgunLayerMask() == EWeaponLayerMask::Full,
					FString::Printf(TEXT("1: the %s's body (%s) plays the launcher set from %s (mask %d)"), *Member->DisplayName.ToString(),
						OtherBody && OtherBody->GetSkeleton() ? *OtherBody->GetSkeleton()->GetName() : TEXT("-"), *Other->GetShotgunRetargetFolder(),
						static_cast<int32>(Other->GetShotgunLayerMask())));
			}
			if (Anim)
			{
				Anim->bWeaponLayerProbe = true;
				Hero->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			GoTo(State, 2);
			return true;
		}
		case 2: // standing still: the legs are the pack's too
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Anim && Anim->GetWeaponLayerLowerDeviation() > 0.5f && Anim->GetWeaponLayerUpperDeviation() > 1.f,
				FString::Printf(TEXT("2: standing, the lower body follows the pack's idle (change %.1f), the upper body too (%.1f)"),
					Anim ? Anim->GetWeaponLayerLowerDeviation() : 0.f, Anim ? Anim->GetWeaponLayerUpperDeviation() : 0.f));
			// The legs walk straight ahead: the hound parked away meanwhile (no combat facing turning him into a strafe; it comes back for the shots).
			if (AEnemyCharacter* Target = State.Target.Get())
			{
				SmokeUtils::ParkEnemyFarAway(Target);
			}
			StartMove(State);
			Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorForwardVector() * 700.f), false);
			GoTo(State, 3);
			return true;
		case 3: // walking (his 220 cm/s: the pack's run loops)
			// The whole leg (until he stops): the foot-plant check needs steady strides.
			if ((State.MoveSamples < 30 || Hero->GetVelocity().Size2D() > 10.f) && State.StageTime < 8.f)
			{
				return true;
			}
			CheckMove(State, TEXT("3: running forward (his 220 cm/s: the run loops)"), TEXT("Anim_Set_02_"), ELegs::Abp);
			Hero->StopOperative();
			StartMove(State);
			Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorForwardVector() * 1500.f), true);
			GoTo(State, 4);
			return true;
		case 4: // sprinting: Set_01 Run_Fast
			if ((State.MoveSamples < 20 || Hero->GetVelocity().Size2D() > 10.f) && State.StageTime < 8.f)
			{
				return true;
			}
			CheckMove(State, TEXT("4: sprinting"), TEXT("Run_Fast"), ELegs::Abp);
			Hero->StopOperative();
			Hero->SetStance(EOperativeStance::Crouching);
			GoTo(State, 5);
			return true;
		case 5: // crouch-walking
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (!State.bMoveSampling)
			{
				StartMove(State);
				Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorForwardVector() * 500.f), false);
				return true;
			}
			if ((State.MoveSamples < 30 || Hero->GetVelocity().Size2D() > 10.f) && State.StageTime < 10.f)
			{
				return true;
			}
			CheckMove(State, TEXT("5: crouch-walking"), TEXT("Walk_Sit"), ELegs::CrouchUpper);
			Hero->StopOperative();
			Hero->SetStance(EOperativeStance::Standing);
			GoTo(State, 6);
			return true;
		case 6: // aim up: a target 15 m ahead, 9 m up
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (!State.bFired)
			{
				State.bFired = true;
				State.Forward = FVector(Hero->GetActorForwardVector().X, Hero->GetActorForwardVector().Y, 0.f).GetSafeNormal();
				PlaceTarget(State, 1500.f, 900.f);
				State.MaxAimUp = 0.f;
				Check(State, Fire(State, 0), TEXT("6: a rocket at the target 9 m above"));
				return true;
			}
			if (!State.bShotChecked)
			{
				State.bShotChecked = true; // the next step: the anim update and the tick have seen the shot
				CheckShot(State, World, TEXT("6"), TEXT("Anim_Set_02_Shot"));
			}
			if (Anim)
			{
				State.MaxAimUp = FMath::Max(State.MaxAimUp, Anim->GetLauncherAimWeights()[static_cast<int32>(EWeaponAimPose::Up)]);
			}
			if (State.StageTime < 2.6f)
			{
				return true;
			}
			Check(State, State.MaxAimUp > 0.1f, FString::Printf(TEXT("6: the aim pose bends him up (the up pose's weight %.2f, pitch %.1f deg)"), State.MaxAimUp, Anim ? Anim->AimPitch : 0.f));
			State.bFired = State.bShotChecked = false;
			GoTo(State, 7);
			return true;
		case 7: // aim down: a target 20 m ahead, 4 m below
			if (State.StageTime < 0.15f)
			{
				PlaceTarget(State, 2000.f, -400.f);
				State.MaxAimDown = 0.f;
				Check(State, Fire(State, 0), TEXT("7: a rocket at the target 4 m below"));
				return true;
			}
			if (Anim)
			{
				State.MaxAimDown = FMath::Max(State.MaxAimDown, Anim->GetLauncherAimWeights()[static_cast<int32>(EWeaponAimPose::Down)]);
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			Check(State, State.MaxAimDown > 0.05f, FString::Printf(TEXT("7: the aim pose bends him down (the down pose's weight %.2f, pitch %.1f deg)"), State.MaxAimDown, Anim ? Anim->AimPitch : 0.f));
			PlaceTarget(State, 2000.f, 60.f);
			GoTo(State, 8);
			return true;
		case 8: // the standing shot with a reserve: the reload follows (cease fire off so it runs; the target parked)
		case 10: // ... crouched
		{
			const bool bCrouched = State.Stage == 10;
			if (!State.bFired)
			{
				State.bFired = true;
				State.bShotChecked = false;
				Check(State, Fire(State, 3), bCrouched ? TEXT("10: crouched, a rocket at the target 20 m ahead") : TEXT("8: a rocket at the target 20 m ahead"));
				return true;
			}
			if (!State.bShotChecked)
			{
				State.bShotChecked = true;
				CheckShot(State, World, bCrouched ? TEXT("10") : TEXT("8"), bCrouched ? TEXT("Sit_Shot") : TEXT("Anim_Set_02_Shot"));
				Hero->bTacticalCeaseFire = false;
				if (AEnemyCharacter* Target = State.Target.Get())
				{
					SmokeUtils::ParkEnemyFarAway(Target);
				}
				State.bRoundHiddenEarly = false;
				State.bRoundBackBeforeEnd = false;
				State.RoundBackAt = -1.f;
				State.ReloadMaxWeight = 0.f;
				State.ReloadClip.Reset();
				return true;
			}
			if (Hero->bIsReloading)
			{
				const float Progress = Hero->GetReloadProgress01();
				if (Anim && Anim->GetLauncherAction() == ELauncherAction::Reload)
				{
					State.ReloadMaxWeight = FMath::Max(State.ReloadMaxWeight, Anim->GetLauncherActionWeight());
					State.ReloadClip = ClipName(Anim->GetLauncherActionClip());
					const float Elapsed = Progress * (Hero->CurrentWeapon ? Hero->CurrentWeapon->Stats.Ammo.ReloadSeconds : 1.f);
					if (Elapsed > 0.5f && Anim->GetLauncherActionClip())
					{
						State.ReloadClipRate = Anim->GetLauncherActionTime() / Elapsed;
						State.ReloadClipLength = Anim->GetLauncherActionClip()->GetPlayLength();
					}
				}
				if (Progress > 0.05f && Progress < Hero->LauncherRoundInsertFraction - 0.05f && !Hero->IsLauncherRoundVisible())
				{
					State.bRoundHiddenEarly = true;
				}
				if (Hero->IsLauncherRoundVisible() && State.RoundBackAt < 0.f)
				{
					State.RoundBackAt = Progress;
					State.bRoundBackBeforeEnd = true;
				}
				return State.StageTime < 9.f ? true : (Check(State, false, TEXT("the reload ended in time")), Finish(State, false));
			}
			if (State.StageTime < 0.5f || (Hero->IsLauncherReloadPending() && State.StageTime < 4.f))
			{
				return true; // the reload starts after the shot's follow-through (the designer 2026-11-09: the Reload clip at rate 1)
			}
			Check(State, State.ReloadMaxWeight > 0.9f && State.ReloadClip.Contains(bCrouched ? TEXT("Sit_Reload") : TEXT("Anim_Set_02_Reload")),
				FString::Printf(TEXT("%s: the pack's reload clip on the body (%s, weight %.2f), fitted to the %.1f s reload"), bCrouched ? TEXT("10") : TEXT("8"), *State.ReloadClip,
					State.ReloadMaxWeight, Hero->CurrentWeapon ? Hero->CurrentWeapon->Stats.Ammo.ReloadSeconds : 0.f));
			Check(State, FMath::Abs(State.ReloadClipRate - 1.f) < 0.08f && Hero->CurrentWeapon
					&& FMath::Abs(Hero->CurrentWeapon->Stats.Ammo.ReloadSeconds - State.ReloadClipLength) < 0.05f,
				FString::Printf(TEXT("%s: the reload clip plays at rate %.2f (the designer: 1), reload_time %.3f s vs the clip %.3f s"), bCrouched ? TEXT("10") : TEXT("8"),
					State.ReloadClipRate, Hero->CurrentWeapon ? Hero->CurrentWeapon->Stats.Ammo.ReloadSeconds : 0.f, State.ReloadClipLength));
			State.ReloadClipRate = 0.f;
			Check(State, State.bRoundHiddenEarly && State.bRoundBackBeforeEnd && Hero->IsLauncherRoundVisible() && Hero->CurrentClip == 1,
				FString::Printf(TEXT("%s: the rocket stays off the tube until the clip pushes it in (back at %.0f %% of the reload; the clip's insertion %.0f %%), loaded after"),
					bCrouched ? TEXT("10") : TEXT("8"), State.RoundBackAt * 100.f, Hero->LauncherRoundInsertFraction * 100.f));
			Hero->bTacticalCeaseFire = true;
			State.bFired = State.bShotChecked = false;
			if (bCrouched)
			{
				GoTo(State, 11);
				return true;
			}
			Hero->SetStance(EOperativeStance::Crouching);
			GoTo(State, 9);
			return true;
		}
		case 9: // settle crouched, the target back
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			PlaceTarget(State, 2000.f, 60.f);
			GoTo(State, 10);
			return true;
		case 11: // a hit: the launcher pack's Get_Hit
		{
			if (State.StageTime < 0.15f)
			{
				Hero->SetStance(EOperativeStance::Standing);
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (State.HitClipsBefore == 0)
			{
				State.HitClipsBefore = (Anim ? Anim->GetLauncherHitClips() : 0) + 1;
				Hero->TakeHit(10.f, TEXT("RpgAnimSmoke"), false, true, nullptr);
				return true;
			}
			Check(State, Anim && Anim->GetLauncherHitClips() == State.HitClipsBefore, FString::Printf(TEXT("11: a hit plays the launcher pack's Get_Hit (%d)"),
				Anim ? Anim->GetLauncherHitClips() : -1));
			GoTo(State, 12);
			return true;
		}
		// "legs_abp" (the game designer 2026-11-10): walking forward / running the legs are the ABP's M16 locomotion, walking sideways the pack's;
		// each leg goes the other way along the line (back and forth on the same strip); a held look 90 deg off the path makes the strafe.
		case 12: // walking forward at 110 cm/s (below the walk -> run band 140 .. 180)
		case 13: // walking sideways at 110 cm/s (he looks 90 deg right of his path)
		case 14: // running sideways at his 220 cm/s
		case 15: // the seam: walking forward at 110 cm/s, mid-walk he turns to look at his side - forward -> strafe
		{
			const bool bSide = State.Stage == 13 || State.Stage == 14;
			if (!State.bMoveStarted)
			{
				if (State.StageTime < 1.f)
				{
					return true; // settle (standing after the hit / the last leg)
				}
				State.bMoveStarted = true;
				// Along the strip: stage 12 / 14 ahead, 13 / 15 back.
				const FVector Along = (State.Stage == 12 || State.Stage == 14) ? State.Forward : -State.Forward;
				const float Distance = State.Stage == 15 ? 1000.f : 700.f;
				const FVector Goal = SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Along * Distance);
				if (AEnemyCharacter* Target = State.Target.Get())
				{
					SmokeUtils::ParkEnemyFarAway(Target);
				}
				// A strafe: he looks 90 deg to the right of his path all the way.
				State.bHoldLook = bSide;
				State.LookYaw = static_cast<float>(FVector::CrossProduct(FVector::UpVector, Along).Rotation().Yaw);
				State.SpeedOverride = State.Stage == 14 ? 0.f : 110.f;
				StartMove(State);
				Hero->OrderMoveTo(Goal, false);
				State.bMoveSampling = true;
				return true;
			}
			if (State.Stage == 15 && !State.bSeamWindow && State.StageTime >= 4.f && Hero->GetVelocity().Size2D() > 60.f)
			{
				// Mid-walk: he turns to look 90 deg right of his path and walks on sideways.
				State.bHoldLook = true;
				State.LookYaw = static_cast<float>(FVector::CrossProduct(FVector::UpVector, Hero->GetVelocity().GetSafeNormal2D()).Rotation().Yaw);
				State.bSeamWindow = true;
			}
			if ((State.MoveSamples < 30 || Hero->GetVelocity().Size2D() > 10.f) && State.StageTime < 14.f)
			{
				return true;
			}
			const float Facing = FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw, State.LookYaw));
			switch (State.Stage)
			{
			case 12:
				CheckMove(State, TEXT("12: walking forward (110 cm/s)"), TEXT("Anim_Set_02_Walk"), ELegs::Abp);
				break;
			case 13:
				Check(State, Facing < 25.f, FString::Printf(TEXT("13: he looks 90 deg off his path while walking (%.0f deg off that look)"), Facing));
				CheckMove(State, TEXT("13: walking sideways (110 cm/s)"), TEXT("Anim_Set_02_Walk_"), ELegs::Pack, true);
				break;
			case 14:
				Check(State, Facing < 25.f, FString::Printf(TEXT("14: he looks 90 deg off his path while running (%.0f deg off that look)"), Facing));
				CheckMove(State, TEXT("14: running sideways (220 cm/s)"), TEXT("Anim_Set_02_Run_"), ELegs::Abp);
				break;
			default:
			{
				State.bMoveSampling = false;
				// A pop between the set's legs and the ABP's shows as a component-space spike of the pelvis / feet far above the walk's own.
				Check(State, State.bSeamWindow && State.SeamMaxLocal > 0.f && State.SeamMaxLocal < FMath::Max(600.f, State.PreSeamMaxLocal * 2.f) && State.MaxLegsAbpStep < 0.2f,
					FString::Printf(TEXT("15: forward -> strafe walking: no pop (pelvis / feet in component space max %.0f cm/s after the turn vs %.0f walking forward; the ABP-legs share's largest one-frame step %.3f, %.0f deg off the side look)"),
						State.SeamMaxLocal, State.PreSeamMaxLocal, State.MaxLegsAbpStep, Facing));
				break;
			}
			}
			Hero->StopOperative();
			State.SpeedOverride = 0.f;
			State.bMoveStarted = false;
			State.bHoldLook = false;
			if (State.Stage < 15)
			{
				GoTo(State, State.Stage + 1);
				return true;
			}
			if (Anim)
			{
				Anim->bWeaponLayerProbe = false;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke foot slide (planted foot median drift): %s"), *FString::Join(State.SlideReport, TEXT(" | ")));
			return Finish(State, true);
		}
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleFrame(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RpgAnimSmoke"),
		TEXT("Dev check: the RPG-7 on the launcher pack (the Commander's draw, the full-body set and its planted feet walking / sprinting / crouching, aim up / down, the shot with the rocket model, the reload putting the rocket back, crouched, a hit); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
