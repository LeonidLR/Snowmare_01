// Dev-only headless check of the INJURED LOCOMOTION (the game designer 2026-11-10, InjuredLocomotionRules; docs/systems/animation-setup.md
// "Injured locomotion layer") on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands InjuredLocomotionSmoke
// The leader alone in a clear patch (the mates parked, the enemies parked), his body held facing the patch's forward (a held look) so every
// leg walks a known direction relative to the body:
//  1. full HP walking forward 3 m: the injured layer stays off (weight 0);
//  2. HP 40 % (wounded): standing still the injured idle drives the legs; 3. walking 3 m in 8 directions relative to the body: the layer drives
//     the legs (weight > 0.9) with the clip of that direction (Walk_Injured_<dir>); the best-planted foot's world drift (foot slide) per leg;
//  4. prone: crawling forward 2.5 m the prone crawl layer plays the injured crawl (Crawl_Injured); the best-planted limb's slide;
//  5. full HP, cold 75 % (Freezing): walking forward / sideways = the injured walk (Walk_Injured_Front / _Right); standing still = the existing
//     shiver idle (the warm-idle Cold_1 set), the layer off; 6. cold 92 % (>= cold_slow_from 0.9): walking forward = the slow injured walk.
//  7. the CRITICAL wound (the designer 2026-11-10, CriticalWoundRules): HP 15 % standing -> he goes down by himself (prone, held down); the
//     player's stand order (Alt + Z path) and a direct SetStance are refused with the feed line "... is critically wounded - heal him", no
//     sprint; crawling forward plays the injured crawl; healed to 40 % -> the lock lifts (still prone), the stand order stands him up, walking
//     forward = the injured walk (he limps).
// Before the copies are imported (/Game/Characters/Animations/Injured), run with -ExtraArgs "-AnimPackFallback=injured": the pack's clips by bone
// name - the logic is checked, the foot numbers are only logged (the pack mannequin's proportions).
// Arg "shots" (rendered run): one frame per state, no HUD, to Saved/Screenshots/InjuredLocomotion/<state>.png.
// Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/InjuredLocomotionRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "UI/GameMessageSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace InjuredLocomotionSmoke
{
	using InjuredLocomotionRules::EInjuredGait;

	constexpr float StepSeconds = 0.05f;
	constexpr float LegCm = 300.f;
	constexpr float CrawlCm = 250.f;
	constexpr float WoundedHealth = 40.f;
	constexpr float FreezingCold = 75.f;
	constexpr float FrostCold = 92.f;
	constexpr float CriticalHealth = 15.f;
	/**
	 * The best-planted foot's median world drift as a share of the body's speed, with a floor in cm/s: forward / back / the slow walk; the pack's
	 * diagonals / strafes pivot the stance foot (Scripts/Editor/setup_injured_animation.py: their own slide 25-38 % on the copies at their best
	 * speed - no bone stands still), so those legs get the wider limit.
	 */
	constexpr float FootSlideShareLimit = 0.25f;
	constexpr float FootSlideShareLimitSide = 0.45f;
	constexpr float FootSlideFloorCm = 25.f;

	enum class EStage : uint8
	{
		Setup,
		Healthy,
		WoundedIdle,
		WoundedWalk,
		Prone,
		Crawl,
		ColdWalk,
		ColdStrafe,
		ColdIdle,
		FrostWalk,
		CriticalDown,
		CriticalCrawl,
		CriticalHealed,
		CriticalWalk,
		Done
	};

	/** The 8 legs relative to the body (deg clockwise from its forward) and the clip each must play. */
	static const float LegAngles[8] = { 0.f, 45.f, 90.f, 135.f, 180.f, -135.f, -90.f, -45.f };
	static const TCHAR* LegClips[8] = { TEXT("Walk_Injured_Front"), TEXT("Walk_Injured_Front_Right"), TEXT("Walk_Injured_Right"),
		TEXT("Walk_Injured_Back_Right"), TEXT("Walk_Injured_Back"), TEXT("Walk_Injured_Back_Left"), TEXT("Walk_Injured_Left"), TEXT("Walk_Injured_Front_Left") };
	static const TCHAR* Limbs[6] = { TEXT("hand_l"), TEXT("hand_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("ball_l"), TEXT("ball_r") };
	static const TCHAR* Feet[4] = { TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") };

	struct FMoveSamples
	{
		bool bActive = false;
		bool bCrawl = false;
		int32 Frames = 0;
		int32 SteadyFrames = 0;
		float MaxWeight = 0.f;
		double WeightSum = 0.0;
		double SpeedSum = 0.0;
		double RateSum = 0.0;
		/** The layer's direction (deg relative to the body) and the body's yaw error against the held look, summed over the steady frames. */
		double DirSum = 0.0;
		double FacingErrSum = 0.0;
		float MaxCrawlWeight = 0.f;
		int32 InjuredCrawlFrames = 0;
		TMap<FString, int32> ClipVotes;
		TArray<float> PlantSpeeds;
		FVector LastFoot[4];
		FVector LastLimb[6];
		bool bFeetValid = false;
		/** The leg's frame was taken ("shots"). */
		bool bShotTaken = false;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		bool bShots = false;
		bool bPackFallback = false;
		bool bMoveStarted = false;
		int32 Leg = 0;
		FString Dir;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		bool bHoldLook = false;
		float LookYaw = 0.f;
		bool bHoldCold = false;
		float HeldCold = 0.f;
		FMoveSamples Move;
		float IdleMaxWeight = 0.f;
		TMap<FString, int32> IdleVotes;
		bool bShiverSeen = false;
		TArray<FString> SlideReport;
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
		if (!State.SlideReport.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke foot slide (planted foot / best-planted limb median drift): %s"), *FString::Join(State.SlideReport, TEXT(" | ")));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("InjuredLocomotionSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString ClipName(const UAnimSequenceBase* Clip)
	{
		return Clip ? Clip->GetName() : FString(TEXT("none"));
	}

	FString TopVote(const TMap<FString, int32>& Votes)
	{
		FString Top = TEXT("none");
		int32 Best = 0;
		for (const TPair<FString, int32>& Vote : Votes)
		{
			if (Vote.Value > Best)
			{
				Best = Vote.Value;
				Top = Vote.Key;
			}
		}
		return Top;
	}

	void FrameCamera(UWorld* World, FState& State, const AOperativeCharacter& Op, float HeightCm)
	{
		const FVector Body = Op.GetActorLocation() - FVector(0.f, 0.f, HeightCm);
		const FVector Forward = Op.GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Eye = Body + Forward * 280.f + Right * 280.f + FVector(0.f, 0.f, 150.f);
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (Body - Eye).Rotation());
			if (ACameraActor* Camera = State.Camera.Get())
			{
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				Camera->GetCameraComponent()->SetFieldOfView(55.f);
				Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			}
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Body - Eye).Rotation());
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->SetViewTarget(Camera);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
		}
	}

	void Shot(UWorld* World, FState& State, const AOperativeCharacter& Op, const TCHAR* Name, float HeightCm = 40.f)
	{
		if (State.bShots)
		{
			FrameCamera(World, State, Op, HeightCm);
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s/%s.png"), *State.Dir, Name), false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: frame %s/%s.png"), *State.Dir, Name);
		}
	}

	/**
	 * Per frame (end of frame, bones final): the held look, the held cold, and while a move is sampled - the layer's weight, its dominant clip, and
	 * at a steady speed the best-planted foot's world drift (the slowest of the heels / balls); a crawl: the slowest of the hands / knees / balls
	 * (ProneCrawlSmoke's method).
	 */
	void SampleFrame(FState& State)
	{
		AOperativeCharacter* Op = State.Op.Get();
		if (!Op || !Op->GetMesh())
		{
			return;
		}
		if (State.bHoldLook)
		{
			Op->SetFormationLookYaw(State.LookYaw);
		}
		if (State.bHoldCold)
		{
			Op->ColdLevel = State.HeldCold;
		}
		FMoveSamples& Move = State.Move;
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		UWorld* World = Op->GetWorld();
		if (!Move.bActive || !Anim || !World)
		{
			Move.bFeetValid = false;
			return;
		}
		const float Dt = World->GetDeltaSeconds();
		const float Speed = static_cast<float>(Op->GetVelocity().Size2D());
		const float MaxSpeed = Op->GetCharacterMovement() ? Op->GetCharacterMovement()->MaxWalkSpeed : Speed;
		const bool bSteady = Speed > 20.f && Speed >= MaxSpeed * 0.85f;
		++Move.Frames;
		if (!bSteady || Dt <= KINDA_SMALL_NUMBER)
		{
			Move.bFeetValid = false;
			return;
		}
		++Move.SteadyFrames;
		Move.SpeedSum += Speed;
		if (Move.bCrawl)
		{
			Move.MaxCrawlWeight = FMath::Max(Move.MaxCrawlWeight, Anim->GetProneCrawlWeight());
			Move.InjuredCrawlFrames += Anim->IsInjuredCrawlActive() ? 1 : 0;
			Move.MaxWeight = FMath::Max(Move.MaxWeight, Anim->GetInjuredWeight());
			Move.ClipVotes.FindOrAdd(ClipName(Anim->GetProneCrawlDominantClip()))++;
			Move.RateSum += Anim->GetProneCrawlPlayRate();
			float Slowest = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const FVector Now = Op->GetMesh()->GetBoneLocation(Limbs[Index]);
				Slowest = FMath::Min(Slowest, static_cast<float>(FVector::Dist2D(Now, Move.LastLimb[Index])) / Dt);
				Move.LastLimb[Index] = Now;
			}
			if (Move.bFeetValid)
			{
				Move.PlantSpeeds.Add(Slowest);
			}
			Move.bFeetValid = true;
			return;
		}
		Move.MaxWeight = FMath::Max(Move.MaxWeight, Anim->GetInjuredWeight());
		Move.WeightSum += Anim->GetInjuredWeight();
		Move.RateSum += Anim->GetInjuredPlayRate();
		Move.DirSum += Anim->GetInjuredDirection();
		Move.FacingErrSum += FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), State.LookYaw));
		Move.ClipVotes.FindOrAdd(ClipName(Anim->GetInjuredDominantClip()))++;
		// A walk always stands on a foot (single / double support, no flight): the slowest of the heels / balls over the ground (world, horizontal)
		// against the body's speed is the slide (0 % = a foot stands, 100 % = both glide with the body). Robust to a limp's uneven heights (the
		// lowest-foot method of RpgAnimSmoke found no stance frames on several injured legs).
		float Slowest = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector Now = Op->GetMesh()->GetBoneLocation(Feet[Index]);
			Slowest = FMath::Min(Slowest, static_cast<float>(FVector::Dist2D(Now, Move.LastFoot[Index])) / Dt);
			Move.LastFoot[Index] = Now;
		}
		if (Move.bFeetValid)
		{
			Move.PlantSpeeds.Add(Slowest);
		}
		Move.bFeetValid = true;
	}

	/** Back to the patch's origin facing its forward, standing still. */
	void Reset(AOperativeCharacter& Op, const FState& State)
	{
		Op.SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
		Op.SetActorRotation(State.Forward.Rotation());
		if (UCharacterMovementComponent* Movement = Op.GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	void StartMove(FState& State, AOperativeCharacter& Op, const FVector& WorldDir, float Distance, bool bCrawl)
	{
		State.Move = FMoveSamples();
		State.Move.bActive = true;
		State.Move.bCrawl = bCrawl;
		State.bMoveStarted = true;
		const FVector Goal = SmokeUtils::ClearPoint(Op.GetWorld(), Op.GetActorLocation(), Op.GetActorLocation() + WorldDir * Distance);
		const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(WorldDir.Rotation().Yaw),
			static_cast<float>((Goal - Op.GetActorLocation()).Rotation().Yaw)));
		if (Off > 5.f)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Smoke: the leg's goal turned %.0f deg off the wanted direction (an obstacle)"), Off);
		}
		Op.OrderMoveTo(Goal, false);
	}

	/** Ends a sampled move: logs and returns the dominant clip, the steady weight mean and the foot slide; checks the slide unless bReportOnly. */
	FString EndMove(FState& State, const FString& Label, bool bCheckSlide, float* OutMeanWeight = nullptr, float SlideLimit = FootSlideShareLimit)
	{
		FMoveSamples& Move = State.Move;
		Move.bActive = false;
		const FString Top = TopVote(Move.ClipVotes);
		const float Speed = Move.SteadyFrames > 0 ? static_cast<float>(Move.SpeedSum / Move.SteadyFrames) : 0.f;
		const float Mean = Move.SteadyFrames > 0 ? static_cast<float>(Move.WeightSum / Move.SteadyFrames) : 0.f;
		const float Rate = Move.SteadyFrames > 0 ? static_cast<float>(Move.RateSum / Move.SteadyFrames) : 0.f;
		Move.PlantSpeeds.Sort();
		const float Drift = Move.PlantSpeeds.IsEmpty() ? -1.f : Move.PlantSpeeds[Move.PlantSpeeds.Num() / 2];
		State.SlideReport.Add(FString::Printf(TEXT("%s: %.1f cm/s at %.0f cm/s (%.0f %%, %d frames, rate %.2f)"), *Label, Drift, Speed,
			Speed > 1.f && Drift >= 0.f ? Drift / Speed * 100.f : -1.f, Move.PlantSpeeds.Num(), Rate));
		const int32 N = FMath::Max(1, Move.SteadyFrames);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - steady frames %d, speed %.0f cm/s, injured weight max %.2f mean %.2f, crawl weight max %.2f, clip %s, play rate %.2f, planted drift %.1f cm/s, layer direction mean %.0f deg, body off the look %.1f deg"),
			*Label, Move.SteadyFrames, Speed, Move.MaxWeight, Mean, Move.MaxCrawlWeight, *Top, Rate, Drift, Move.DirSum / N, Move.FacingErrSum / N);
		if (bCheckSlide && !State.bPackFallback)
		{
			Check(State, Move.PlantSpeeds.Num() >= 5 && Drift >= 0.f && Drift < FMath::Max(FootSlideFloorCm, Speed * SlideLimit),
				FString::Printf(TEXT("%s at %.0f cm/s: the best-planted foot drifts %.1f cm/s (median of %d frames, %.0f %% of his speed; limit %.0f %%)"), *Label, Speed,
					Drift, Move.PlantSpeeds.Num(), Speed > 1.f ? Drift / Speed * 100.f : 0.f, SlideLimit * 100.f));
		}
		if (OutMeanWeight)
		{
			*OutMeanWeight = Mean;
		}
		return Top;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bMoveStarted = false;
		State.IdleMaxWeight = 0.f;
		State.IdleVotes.Reset();
	}

	bool StepWound(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 300.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d leg %d"), static_cast<int32>(State.Stage), State.Leg));
			return Finish(State, false);
		}
		for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			if (Enemy && Enemy->IsActorTickEnabled())
			{
				SmokeUtils::ParkEnemyFarAway(Enemy);
				Enemy->SetActorTickEnabled(false);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (State.Stage == EStage::Setup)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
			UOperativeAnimInstance* Anim = AnimOf(Op);
			if (!Op || !Anim || !Op->HealthComponent)
			{
				Check(State, false, TEXT("a squad leader with the operative anim instance"));
				return Finish(State, false);
			}
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
			}
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Op->GetActorLocation(), Op->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-450.f, -450.f), FVector2D(450.f, 450.f)), Ignore);
			Check(State, Arena.bFound, TEXT("a clear 9 x 9 m patch for the eight legs"));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.Origin = SmokeUtils::FreeSpot(World, Arena.Origin, Op);
			State.Forward = Arena.Forward;
			int32 Park = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Op)
				{
					Member->SetActorLocation(State.Origin - Arena.Forward * (2500.f + 200.f * Park++), false, nullptr, ETeleportType::TeleportPhysics);
					Member->bTacticalCeaseFire = true;
					Member->SetActorTickEnabled(false);
				}
			}
			Op->bTacticalCeaseFire = true;
			Op->ColdLevel = 0.f;
			Op->HealthComponent->RestoreHealth(100.f, 100.f);
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // -nullrhi: real bones
			Op->SetStance(EOperativeStance::Standing);
			Reset(*Op, State);
			State.Op = Op;
			State.bHoldLook = true;
			State.LookYaw = static_cast<float>(State.Forward.Rotation().Yaw);
			Check(State, Anim->HasInjuredSet(), FString::Printf(TEXT("the injured set is loaded for %s (%s)"), *Op->GetMesh()->GetSkeletalMeshAsset()->GetName(),
				State.bPackFallback ? TEXT("-AnimPackFallback: the pack's clips by bone name") : TEXT("the retargeted copies")));
			if (!Anim->HasInjuredSet())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: no injured set - import the copies (Scripts/Editor/setup_injured_animation.py) or run with -AnimPackFallback=injured"));
				return Finish(State, false);
			}
			Enter(State, EStage::Healthy);
			return true;
		}
		AOperativeCharacter* Op = State.Op.Get();
		UOperativeAnimInstance* Anim = AnimOf(Op);
		if (!Op || !Anim)
		{
			Check(State, false, TEXT("the leader stays"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case EStage::Healthy:
		{
			if (!State.bMoveStarted)
			{
				if (State.StageTime < 1.f)
				{
					return true;
				}
				Check(State, !Op->IsWounded() && Anim->GetInjuredGait() == EInjuredGait::None, TEXT("1: full HP - not wounded, no injured gait"));
				StartMove(State, *Op, State.Forward, LegCm, false);
				return true;
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 12.f)
			{
				return true;
			}
			float Mean = 0.f;
			EndMove(State, TEXT("healthy forward"), false, &Mean);
			Check(State, State.Move.SteadyFrames > 10 && State.Move.MaxWeight < 0.01f,
				FString::Printf(TEXT("1: full HP walking: the injured layer stays off (weight max %.3f, %d frames)"), State.Move.MaxWeight, State.Move.SteadyFrames));
			Op->HealthComponent->RestoreHealth(WoundedHealth, 100.f);
			Reset(*Op, State);
			Enter(State, EStage::WoundedIdle);
			return true;
		}
		case EStage::WoundedIdle:
		{
			if (State.StageTime > 1.f)
			{
				State.IdleMaxWeight = FMath::Max(State.IdleMaxWeight, Anim->GetInjuredWeight());
				State.IdleVotes.FindOrAdd(ClipName(Anim->GetInjuredDominantClip()))++;
			}
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const FString Top = TopVote(State.IdleVotes);
			Check(State, Op->IsWounded() && Anim->GetInjuredGait() == EInjuredGait::Wounded,
				FString::Printf(TEXT("2: HP %.0f %% - wounded, the injured gait (%d)"), Op->HealthComponent->GetHealthFraction() * 100.f, static_cast<int32>(Anim->GetInjuredGait())));
			Check(State, State.IdleMaxWeight > 0.9f && Top.StartsWith(TEXT("Idle_Walk_Injured")),
				FString::Printf(TEXT("2: standing still wounded: the injured idle drives the legs (weight %.2f, clip %s)"), State.IdleMaxWeight, *Top));
			Shot(World, State, *Op, TEXT("wounded_idle"));
			State.Leg = 0;
			Reset(*Op, State);
			Enter(State, EStage::WoundedWalk);
			return true;
		}
		case EStage::WoundedWalk:
		{
			if (!State.bMoveStarted)
			{
				if (State.StageTime < 1.f)
				{
					return true;
				}
				StartMove(State, *Op, State.Forward.RotateAngleAxis(LegAngles[State.Leg], FVector::UpVector), LegCm, false);
				return true;
			}
			if (State.bShots && !State.Move.bShotTaken && State.Move.SteadyFrames >= 25)
			{
				State.Move.bShotTaken = true;
				if (State.Leg == 0)
				{
					Shot(World, State, *Op, TEXT("wounded_walk_forward"));
				}
				else if (State.Leg == 2)
				{
					Shot(World, State, *Op, TEXT("wounded_strafe_right"));
				}
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 15.f)
			{
				return true;
			}
			const FString Label = FString::Printf(TEXT("wounded %s"), LegClips[State.Leg] + 13); // after "Walk_Injured_"
			float Mean = 0.f;
			const bool bStraight = State.Leg == 0 || State.Leg == 4;
			const FString Top = EndMove(State, Label, true, &Mean, bStraight ? FootSlideShareLimit : FootSlideShareLimitSide);
			Check(State, State.Move.SteadyFrames > 10 && Mean > 0.9f,
				FString::Printf(TEXT("3: %s: the injured layer drives the legs (steady weight mean %.2f, %d frames)"), *Label, Mean, State.Move.SteadyFrames));
			Check(State, Top == LegClips[State.Leg], FString::Printf(TEXT("3: %s: the clip of the direction plays (%s, wanted %s)"), *Label, *Top, LegClips[State.Leg]));
			if (++State.Leg < 8)
			{
				Reset(*Op, State);
				Enter(State, EStage::WoundedWalk);
				return true;
			}
			Reset(*Op, State);
			Op->SetStance(EOperativeStance::Prone);
			Enter(State, EStage::Prone);
			return true;
		}
		case EStage::Prone:
		{
			if (State.StageTime < 3.f || Anim->IsStanceChangePending())
			{
				return true;
			}
			Check(State, Op->GetStance() == EOperativeStance::Prone && Anim->GetInjuredWeight() < 0.01f,
				FString::Printf(TEXT("4: prone wounded: the standing injured layer is off (weight %.3f)"), Anim->GetInjuredWeight()));
			Enter(State, EStage::Crawl);
			return true;
		}
		case EStage::Crawl:
		{
			if (!State.bMoveStarted)
			{
				StartMove(State, *Op, State.Forward, CrawlCm, true);
				return true;
			}
			if (State.bShots && !State.Move.bShotTaken && State.Move.SteadyFrames >= 30)
			{
				State.Move.bShotTaken = true;
				Shot(World, State, *Op, TEXT("wounded_crawl"), 80.f);
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 20.f)
			{
				return true;
			}
			const FString Top = EndMove(State, TEXT("wounded crawl"), false);
			Check(State, State.Move.SteadyFrames > 10 && State.Move.MaxCrawlWeight > 0.5f && State.Move.InjuredCrawlFrames > State.Move.SteadyFrames / 2,
				FString::Printf(TEXT("4: crawling wounded: the prone crawl layer drives the body with the injured crawl set (weight %.2f, injured set %d / %d frames)"),
					State.Move.MaxCrawlWeight, State.Move.InjuredCrawlFrames, State.Move.SteadyFrames));
			Check(State, Top.StartsWith(TEXT("Crawl_Injured")), FString::Printf(TEXT("4: the injured crawl clip plays (%s)"), *Top));
			Op->SetStance(EOperativeStance::Standing);
			Op->HealthComponent->RestoreHealth(100.f, 100.f);
			State.bHoldCold = true;
			State.HeldCold = FreezingCold;
			Op->ColdLevel = FreezingCold;
			Enter(State, EStage::ColdWalk);
			return true;
		}
		case EStage::ColdWalk:
		case EStage::ColdStrafe:
		case EStage::FrostWalk:
		{
			const bool bStrafe = State.Stage == EStage::ColdStrafe;
			const bool bFrost = State.Stage == EStage::FrostWalk;
			if (!State.bMoveStarted)
			{
				if (State.StageTime < (bStrafe ? 1.f : (bFrost ? 2.f : 3.5f)) || Anim->IsStanceChangePending() || Op->GetStance() != EOperativeStance::Standing)
				{
					return true;
				}
				Reset(*Op, State);
				StartMove(State, *Op, bStrafe ? State.Forward.RotateAngleAxis(90.f, FVector::UpVector) : State.Forward, LegCm, false);
				return true;
			}
			if (State.bShots && !bStrafe && !State.Move.bShotTaken && State.Move.SteadyFrames >= 25)
			{
				State.Move.bShotTaken = true;
				Shot(World, State, *Op, bFrost ? TEXT("frostbite_slow_walk") : TEXT("freezing_walk_forward"));
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < (bFrost ? 20.f : 15.f))
			{
				return true;
			}
			float Mean = 0.f;
			const TCHAR* Label = bFrost ? TEXT("frostbite forward") : (bStrafe ? TEXT("freezing strafe right") : TEXT("freezing forward"));
			const FString Top = EndMove(State, Label, true, &Mean, bStrafe ? FootSlideShareLimitSide : FootSlideShareLimit);
			const EInjuredGait Wanted = bFrost ? EInjuredGait::ColdSlow : EInjuredGait::Cold;
			const TCHAR* WantedClip = bFrost ? TEXT("Walk_Injured_Slow") : (bStrafe ? TEXT("Walk_Injured_Right") : TEXT("Walk_Injured_Front"));
			Check(State, Anim->GetInjuredGait() == Wanted && !Op->IsWounded(),
				FString::Printf(TEXT("%s: cold %.0f %%, full HP: the %s gait (%d)"), bFrost ? TEXT("6") : TEXT("5"), Op->ColdLevel,
					bFrost ? TEXT("frostbite") : TEXT("freezing"), static_cast<int32>(Anim->GetInjuredGait())));
			Check(State, State.Move.SteadyFrames > 10 && Mean > 0.7f && Top == WantedClip,
				FString::Printf(TEXT("%s: %s: %s (weight mean %.2f, clip %s)"), bFrost ? TEXT("6") : TEXT("5"), Label,
					bFrost ? TEXT("the slow injured walk") : TEXT("the injured walk"), Mean, *Top));
			if (bFrost)
			{
				State.bHoldCold = false;
				Op->ColdLevel = 0.f;
				Reset(*Op, State);
				Op->HealthComponent->RestoreHealth(CriticalHealth, 100.f);
				Enter(State, EStage::CriticalDown);
				return true;
			}
			Reset(*Op, State);
			Enter(State, bStrafe ? EStage::ColdIdle : EStage::ColdStrafe);
			return true;
		}
		case EStage::ColdIdle:
		{
			if (State.StageTime > 0.75f)
			{
				State.IdleMaxWeight = FMath::Max(State.IdleMaxWeight, Anim->GetInjuredWeight());
			}
			const bool bShiver = Anim->bWarmIdleActive && Anim->WarmIdlePose == EWarmIdlePose::ColdIdle;
			if (bShiver && !State.bShiverSeen)
			{
				State.bShiverSeen = true;
				State.StageTime = FMath::Min(State.StageTime, 9.f); // 2 s more in the pose (the frame)
			}
			if (State.bShiverSeen && State.bShots && !State.Move.bShotTaken && State.StageTime >= 10.5f)
			{
				State.Move.bShotTaken = true;
				Shot(World, State, *Op, TEXT("cold_idle_shiver"));
			}
			if (State.StageTime < (State.bShiverSeen ? 11.f : 14.f))
			{
				return true;
			}
			Check(State, State.bShiverSeen, FString::Printf(TEXT("5: standing still frozen: the shiver idle plays (warm-idle pose %d, active %d, clip %s)"),
				static_cast<int32>(Anim->WarmIdlePose), Anim->bWarmIdleActive ? 1 : 0, *ClipName(Anim->GetWarmClip())));
			Check(State, State.IdleMaxWeight < 0.05f, FString::Printf(TEXT("5: ... and the injured layer stays off (weight max %.3f)"), State.IdleMaxWeight));
			State.HeldCold = FrostCold;
			Op->ColdLevel = FrostCold;
			Reset(*Op, State);
			Enter(State, EStage::FrostWalk);
			return true;
		}
		case EStage::CriticalDown:
		{
			if (State.StageTime < 3.f || Anim->IsStanceChangePending())
			{
				return true;
			}
			Check(State, Op->IsCriticallyWounded() && Op->IsHeldDownByCriticalWound() && Op->GetStance() == EOperativeStance::Prone,
				FString::Printf(TEXT("7: HP %.0f %%: critically wounded, he went down by himself (stance %d, held %d, downs %d)"),
					Op->HealthComponent->GetHealthFraction() * 100.f, static_cast<int32>(Op->GetStance()), Op->IsHeldDownByCriticalWound() ? 1 : 0, Op->GetCriticalWoundDowns()));
			UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
			const int32 Before = Messages ? Messages->GetHistory().Num() : 0;
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
			{
				PC->SetEntireSquadStance(EOperativeStance::Standing); // the player's stand order (Alt + Z)
			}
			Op->SetStance(EOperativeStance::Crouching); // any other caller (formation, autonomy, cover)
			bool bLine = false;
			if (Messages)
			{
				for (int32 Index = Before; Index < Messages->GetHistory().Num(); ++Index)
				{
					bLine |= Messages->GetHistory()[Index].Text.ToString().Contains(TEXT("is critically wounded - heal him"));
				}
			}
			Check(State, Op->GetStance() == EOperativeStance::Prone && !Op->CanSprint(),
				FString::Printf(TEXT("7: the stand / crouch orders are refused, no sprint (stance %d)"), static_cast<int32>(Op->GetStance())));
			Check(State, bLine, TEXT("7: the feed line \"... is critically wounded - heal him\""));
			Shot(World, State, *Op, TEXT("critical_down"), 80.f);
			Enter(State, EStage::CriticalCrawl);
			return true;
		}
		case EStage::CriticalCrawl:
		{
			if (!State.bMoveStarted)
			{
				StartMove(State, *Op, State.Forward, CrawlCm, true);
				return true;
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 20.f)
			{
				return true;
			}
			const FString Top = EndMove(State, TEXT("critical crawl"), false);
			Check(State, Op->GetStance() == EOperativeStance::Prone && State.Move.SteadyFrames > 10 && State.Move.InjuredCrawlFrames > State.Move.SteadyFrames / 2
				&& Top.StartsWith(TEXT("Crawl_Injured")), FString::Printf(TEXT("7: critical, he crawls the injured crawl (%s, injured set %d / %d frames)"), *Top,
					State.Move.InjuredCrawlFrames, State.Move.SteadyFrames));
			Op->HealthComponent->RestoreHealth(WoundedHealth, 100.f); // a medkit / the medic on the spot
			Enter(State, EStage::CriticalHealed);
			return true;
		}
		case EStage::CriticalHealed:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (!State.bMoveStarted)
			{
				Check(State, !Op->IsCriticallyWounded() && !Op->IsHeldDownByCriticalWound() && Op->GetStance() == EOperativeStance::Prone,
					FString::Printf(TEXT("7: healed to %.0f %%: the lock lifts, he stays prone until ordered up (stance %d)"), Op->HealthComponent->GetHealthFraction() * 100.f,
						static_cast<int32>(Op->GetStance())));
				if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
				{
					PC->SetEntireSquadStance(EOperativeStance::Standing);
				}
				State.bMoveStarted = true;
				return true;
			}
			if ((Anim->IsStanceChangePending() || Op->GetStance() != EOperativeStance::Standing) && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Op->GetStance() == EOperativeStance::Standing, FString::Printf(TEXT("7: healed, the stand order stands him up (stance %d)"), static_cast<int32>(Op->GetStance())));
			Reset(*Op, State);
			Enter(State, EStage::CriticalWalk);
			return true;
		}
		case EStage::CriticalWalk:
		{
			if (!State.bMoveStarted)
			{
				if (State.StageTime < 1.f || (Anim->GetInjuredWeight() < 0.95f && State.StageTime < 4.f)) // the layer back in after the stand-up clip
				{
					return true;
				}
				StartMove(State, *Op, State.Forward, LegCm, false);
				return true;
			}
			if (State.bShots && !State.Move.bShotTaken && State.Move.SteadyFrames >= 25)
			{
				State.Move.bShotTaken = true;
				Shot(World, State, *Op, TEXT("healed_walk_limp"));
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 15.f)
			{
				return true;
			}
			float Mean = 0.f;
			const FString Top = EndMove(State, TEXT("healed wounded forward"), false, &Mean);
			Check(State, State.Move.SteadyFrames > 10 && Mean > 0.9f && Top == TEXT("Walk_Injured_Front") && Anim->GetInjuredLimpAmount() > 0.5f,
				FString::Printf(TEXT("7: healed to 40 %%, he limps forward (weight %.2f, clip %s, limp cue %.2f)"), Mean, *Top, Anim->GetInjuredLimpAmount()));
			return Finish(State, true);
		}
		default:
			break;
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shots"), ESearchCase::IgnoreCase); });
		State->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/InjuredLocomotion"));
		FString Fallback;
		State->bPackFallback = FParse::Value(FCommandLine::Get(), TEXT("AnimPackFallback="), Fallback) && Fallback.Contains(TEXT("injured"));
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleFrame(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float) { return StepWound(WeakWorld, State); }), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.InjuredLocomotionSmoke"),
		TEXT("Dev check (the injured locomotion, L_MovementTest): full HP the layer is off; at 40 % HP the injured idle, the 8-direction injured walk (clip per direction, foot slide), the injured crawl prone; cold 75 % the injured walk forward / sideways and the shiver idle standing, cold 92 % the slow injured walk forward; HP 15 % the critical wound (down, stand refused, the injured crawl; healed to 40 % he stands and limps). Logs PASS/FAIL, then exits. Arg \"shots\": rendered frames to Saved/Screenshots/InjuredLocomotion."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
