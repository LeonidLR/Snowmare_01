// Dev-only check of the blizzard walk upper body (game designer 2026-10-22; Characters/BlizzardWalkRules,
// OperativeAnimInstanceBlizzardWalk.cpp) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BlizzardWalkSmoke -Log Smoke-BlizzardWalk.log
// The commander walks up and down an empty lane of the map (SmokeUtils::FindClearArena), the weather forced by
// UEnvironmentSubsystem::ForceWeather (= Codex.Env.SetWeather <w> instant):
//  1. Clear: the storm layer is off (weight 0); the hands' forward offset from the chest (spine_03, along his facing) is measured;
//  2. Blizzard: the storm upper body is in (weight > 0.8) within 0.5 s, both hands > 25 cm in front of the chest and > 15 cm above it
//     (shielding the face; the rifle walk holds them at chest height, hand_l already 55 cm out on the handguard), the weapon stays visible (2026-10-26),
//     the left-hand IK is off - and the legs still walk (the feet swing fore and aft, no full-body clip, speed kept);
//  3. a shot (combat aim) -> weight 0 within 0.5 s and stays 0 while the aim holds; the aim over -> back in;
//  4. indoors (a Closed camera zone spawned over the lane) -> 0; out again -> back in;
//  5. he stops -> 0; walking again in -> Clear weather -> 0; crouch-walking in the blizzard -> 0.
// Bones are read from the mesh (AlwaysTickPoseAndRefreshBones), so the check works headless.
// "clip" (rendered, offscreen, fixed 30 fps): UnrealEditor.exe <uproject> /Game/Maps/L_MovementTest -game -RenderOffscreen
// -ResX=1280 -ResY=720 -benchmark -fps=30 -ExecCmds="CodexTactics.BlizzardWalkSmoke clip" -> ~10 s of frames in
// Saved/Screenshots/BlizzardWalk/frame_NNNN.png: he walks in the blizzard with the storm upper body, stops, then aims (blend-outs).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace BlizzardWalkSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LaneLength = 1600.f;

	enum class EStage : uint8
	{
		Setup,
		ClearWalk,
		BlizzardIn,
		BlizzardHold,
		Aim,
		AimHold,
		AimOver,
		Indoors,
		Outdoors,
		Stop,
		WalkAgain,
		ClearAgain,
		Crouch,
		Done,
		// "clip": the rendered sequence.
		ClipWalk,
		ClipStop,
		ClipAim,
		ClipEnd
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bClip = false;
		int32 Frame = 0;
		FString Dir;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		bool bToFar = true;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<ACameraZoneVolume> Zone;
		TWeakObjectPtr<ACameraActor> Camera;
		/** Hands forward of the chest, sums + count (normal walk / storm walk). */
		FVector4 NormalHands = FVector4(0.f, 0.f, 0.f, 0.f);
		FVector4 StormHands = FVector4(0.f, 0.f, 0.f, 0.f);
		int32 NormalSamples = 0;
		int32 StormSamples = 0;
		float FeetMin = 1e9f;
		float FeetMax = -1e9f;
		float MaxFullBody = 0.f;
		float MinSpeed = 1e9f;
		float MinStormWeight = 1.f;
		float MaxIKAlpha = 0.f;
		bool bWeaponShownAlways = true;
		float MarkTime = 0.f;
		float MaxWeight = 0.f;
		double LastWorldTime = -1.0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool Finish(FState& State)
	{
		if (AActor* Target = State.Target.Get())
		{
			Target->Destroy();
		}
		if (ACameraZoneVolume* Zone = State.Zone.Get())
		{
			Zone->Destroy();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BlizzardWalkSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op ? UOperativeAnimInstance::Of(Op) : nullptr;
	}

	/** Hands against the chest (spine_03), cm: X / Y = hand_l / hand_r forward along his facing, Z / W = hand_l / hand_r height above it. */
	FVector4 HandsForward(const AOperativeCharacter& Op)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		const FVector Forward = Op.GetActorForwardVector().GetSafeNormal2D();
		const FVector Chest = Mesh->GetBoneLocation(TEXT("spine_03"));
		const FVector L = Mesh->GetBoneLocation(TEXT("hand_l")) - Chest;
		const FVector R = Mesh->GetBoneLocation(TEXT("hand_r")) - Chest;
		return FVector4(FVector::DotProduct(L, Forward), FVector::DotProduct(R, Forward), L.Z, R.Z);
	}

	/** Keeps him walking up and down the lane (a new order at each end). */
	void KeepWalking(FState& State, AOperativeCharacter& Op)
	{
		const FVector Goal = State.O + State.F * (State.bToFar ? LaneLength : 0.f);
		const bool bArrived = FVector::Dist2D(Op.GetActorLocation(), Goal) < 160.f;
		if (bArrived)
		{
			State.bToFar = !State.bToFar;
		}
		if (bArrived || Op.GetVelocity().Size2D() < 5.f)
		{
			Op.OrderMoveTo(State.O + State.F * (State.bToFar ? LaneLength : 0.f), false);
		}
	}

	void SetWeather(UWorld* World, EWeatherState Weather)
	{
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->ForceWeather(Weather, /*bInstant*/ true);
		}
	}

	void LogLayer(const TCHAR* Label, const UOperativeAnimInstance& Anim, const AOperativeCharacter& Op)
	{
		const FVector4 Hands = HandsForward(Op);
		UE_LOG(LogCodexTactics, Display, TEXT("BlizzardWalk %s: weight %.2f, active %d, amount %.2f, block %s, clip %s, speed %.0f, hands fwd l %.0f r %.0f up l %.0f r %.0f, weapon %s, IK %.2f, sheltered %d"),
			Label, Anim.GetBlizzardWalkWeight(), Anim.bBlizzardWalkActive ? 1 : 0, Anim.BlizzardAmount,
			BlizzardWalkRules::BlockName(Anim.GetBlizzardWalkBlock()), Anim.GetBlizzardWalkClip() ? *Anim.GetBlizzardWalkClip()->GetName() : TEXT("-"),
			Op.GetVelocity().Size2D(), Hands.X, Hands.Y, Hands.Z, Hands.W, Op.WeaponMesh && Op.WeaponMesh->IsVisible() ? TEXT("shown") : TEXT("hidden"),
			Anim.LeftHandIKAlpha, Anim.IsBlizzardSheltered() ? 1 : 0);
	}

	/** A cube 6 m ahead-left to shoot at (no enemy: exploration stays exploration). */
	AActor* SpawnTarget(UWorld* World, const FState& State, const AOperativeCharacter& Op)
	{
		const FVector At = Op.GetActorLocation() + Op.GetActorForwardVector().GetSafeNormal2D() * 600.f - Op.GetActorRightVector() * 150.f;
		AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
		if (Cube)
		{
			Cube->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Cube->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Cube->SetActorScale3D(FVector(0.5f));
		}
		return Cube;
	}

	void FrameCamera(UWorld* World, FState& State, const AOperativeCharacter& Op)
	{
		const FVector Chest = Op.GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector Forward = Op.GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Eye = Chest + Forward * 380.f + Right * 190.f + FVector(0.f, 0.f, 70.f);
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (Chest - Eye).Rotation());
			if (ACameraActor* Camera = State.Camera.Get())
			{
				UCameraComponent* Component = Camera->GetCameraComponent();
				Component->bConstrainAspectRatio = false;
				Component->SetFieldOfView(55.f);
				Component->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Component->PostProcessSettings.MotionBlurAmount = 0.f;
			}
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			// Smoothly follows (no shake from the walk direction flips at the lane ends).
			const FVector Smoothed = FMath::VInterpTo(Camera->GetActorLocation(), Eye, StepSeconds, 3.f);
			Camera->SetActorLocationAndRotation(Smoothed, (Chest - Smoothed).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		// Game time (the blend checks are in game seconds; the ticker runs in real time).
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		const float Delta = State.LastWorldTime < 0.0 ? 0.f : static_cast<float>(FMath::Max(0.0, Now - State.LastWorldTime));
		State.LastWorldTime = Now;
		State.Time += Delta;
		State.StageTime += Delta;
		if (!World || State.Time > 150.f)
		{
			if (World)
			{
				Check(State, false, TEXT("finished in time"));
			}
			return World ? Finish(State) : false;
		}
		AOperativeCharacter* Op = State.Op.Get();
		UOperativeAnimInstance* Anim = AnimOf(Op);
		if (Op)
		{
			Op->ColdLevel = 0.f;
			Op->bTacticalCeaseFire = true;
		}
		if (State.bClip && Op)
		{
			FrameCamera(World, State, *Op);
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s/frame_%04d.png"), *State.Dir, State.Frame++), false, false);
		}
		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			Op = Squad ? Squad->GetLeader() : nullptr;
			State.Op = Op;
			Anim = AnimOf(Op);
			UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
			Check(State, Op && Anim, TEXT("leader + operative anim instance"));
			Check(State, Environment && Environment->IsInitialised(), TEXT("environment subsystem initialised (weather can be forced)"));
			if (!Op || !Anim || !Environment || !Environment->IsInitialised())
			{
				return Finish(State);
			}
			// The storm pose starts with a random loop in the game; the hand-height checks are written for loop A (4Loop's left hand is
			// lower), so the smoke pins it instead of passing or failing with the dice.
			Anim->BlizzardWalkFirstStepForTesting = EWarmClipStep::LoopA;
			Squad->SetFollowersHolding(true);
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				Ignore.Add(Pawn);
			}
			const FVector Feet = Op->GetActorLocation() - FVector(0.f, 0.f, Op->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Op->GetActorForwardVector(),
				FBox2D(FVector2D(-200.f, -300.f), FVector2D(LaneLength + 200.f, 300.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: lane %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			Check(State, Arena.bFound, TEXT("an empty, flat, navigable 20 x 6 m lane"));
			if (!Arena.bFound)
			{
				return Finish(State);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				const FVector Spot = Member == Op ? State.O : State.O - State.F * 400.f + State.R * (Member->SquadIndex % 2 ? 250.f : -250.f) * 2.f;
				Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + Member->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
				Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			if (State.bClip)
			{
				Environment->SetHour(12.5f); // daylight for the picture
				SetWeather(World, EWeatherState::Blizzard);
				Enter(State, EStage::ClipWalk);
				return true;
			}
			SetWeather(World, EWeatherState::Clear);
			Enter(State, EStage::ClearWalk);
			return true;
		}
		case EStage::ClearWalk:
			KeepWalking(State, *Op);
			if (State.StageTime < 2.5f || Op->GetVelocity().Size2D() < 150.f)
			{
				if (State.StageTime >= 12.f)
				{
					Check(State, false, TEXT("he walks the lane (Clear)"));
					return Finish(State);
				}
				return true;
			}
			State.NormalHands += HandsForward(*Op);
			++State.NormalSamples;
			State.MaxWeight = FMath::Max(State.MaxWeight, Anim->GetBlizzardWalkWeight());
			if (State.NormalSamples >= 20)
			{
				LogLayer(TEXT("clear walk"), *Anim, *Op);
				Check(State, State.MaxWeight < 0.01f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::NoBlizzard,
					FString::Printf(TEXT("Clear weather: storm weight %.2f = 0 (block %s)"), State.MaxWeight, BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock())));
				SetWeather(World, EWeatherState::Blizzard);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: Codex.Env.SetWeather Blizzard instant"));
				Enter(State, EStage::BlizzardIn);
			}
			return true;
		case EStage::BlizzardIn:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() > 0.8f)
			{
				LogLayer(TEXT("blizzard in"), *Anim, *Op);
				Check(State, State.StageTime <= 0.5f + StepSeconds,
					FString::Printf(TEXT("Blizzard while walking outdoors: storm weight %.2f > 0.8 after %.2f s (<= 0.5)"), Anim->GetBlizzardWalkWeight(), State.StageTime));
				State.MarkTime = 0.f;
				Enter(State, EStage::BlizzardHold);
			}
			else if (State.StageTime > 2.f)
			{
				LogLayer(TEXT("blizzard in"), *Anim, *Op);
				Check(State, false, FString::Printf(TEXT("Blizzard while walking: storm weight %.2f never > 0.8"), Anim->GetBlizzardWalkWeight()));
				return Finish(State);
			}
			return true;
		case EStage::BlizzardHold:
		{
			KeepWalking(State, *Op);
			if (State.StageTime < 0.6f)
			{
				return true; // the arms are up (the loop holds them); the lane end turn-arounds are fine
			}
			const FVector Forward = Op->GetActorForwardVector().GetSafeNormal2D();
			const float FeetFore = static_cast<float>(FVector::DotProduct(Op->GetMesh()->GetBoneLocation(TEXT("foot_l")) - Op->GetMesh()->GetBoneLocation(TEXT("foot_r")), Forward));
			State.FeetMin = FMath::Min(State.FeetMin, FeetFore);
			State.FeetMax = FMath::Max(State.FeetMax, FeetFore);
			State.MaxFullBody = FMath::Max(State.MaxFullBody, Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot));
			State.MinSpeed = FMath::Min(State.MinSpeed, static_cast<float>(Op->GetVelocity().Size2D()));
			State.MinStormWeight = FMath::Min(State.MinStormWeight, Anim->GetBlizzardWalkWeight());
			State.MaxIKAlpha = FMath::Max(State.MaxIKAlpha, Anim->LeftHandIKAlpha);
			State.bWeaponShownAlways &= (Op->WeaponMesh && Op->WeaponMesh->IsVisible());
			State.StormHands += HandsForward(*Op);
			++State.StormSamples;
			if (State.StormSamples < 30)
			{
				return true;
			}
			LogLayer(TEXT("blizzard walk"), *Anim, *Op);
			const FVector4 Normal = State.NormalHands / static_cast<float>(FMath::Max(1, State.NormalSamples));
			const FVector4 Storm = State.StormHands / static_cast<float>(FMath::Max(1, State.StormSamples));
			UE_LOG(LogCodexTactics, Display, TEXT("BlizzardWalk hands vs the chest (fwd l / r, up l / r): normal walk %.0f / %.0f, %.0f / %.0f; storm walk %.0f / %.0f, %.0f / %.0f cm"),
				Normal.X, Normal.Y, Normal.Z, Normal.W, Storm.X, Storm.Y, Storm.Z, Storm.W);
			Check(State, State.MinStormWeight > 0.8f, FString::Printf(TEXT("storm weight stays > 0.8 while walking (min %.2f)"), State.MinStormWeight));
			// The normal rifle walk already holds hand_l ~55 cm out on the handguard (hand_r ~28 at the grip, both at chest height), so
			// "further forward than the normal walk" does not hold for the left hand: the storm pose is checked as both hands well in
			// front of the chest AND raised before the face, and the hands' mean height rises clearly over the rifle hold.
			const float NormalUp = (Normal.Z + Normal.W) * 0.5f;
			const float StormUp = (Storm.Z + Storm.W) * 0.5f;
			Check(State, Storm.X > 25.f && Storm.Y > 25.f && Storm.Z > 15.f && Storm.W > 15.f && StormUp - NormalUp > 12.f,
				FString::Printf(TEXT("hands forward and up (shielding the face): hand_l %.0f / hand_r %.0f cm in front of the chest (> 25), %.0f / %.0f cm above it (> 15); mean height +%.0f cm vs the rifle walk (> 12; hand_r forward %+.0f, hand_l %+.0f)"),
					Storm.X, Storm.Y, Storm.Z, Storm.W, StormUp - NormalUp, Storm.Y - Normal.Y, Storm.X - Normal.X));
			Check(State, State.FeetMax - State.FeetMin > 20.f && State.MaxFullBody < 0.01f && State.MinSpeed > 100.f,
				FString::Printf(TEXT("lower body still walks: feet swing %.0f cm (> 20), full-body slot %.2f (0), speed >= %.0f"),
					State.FeetMax - State.FeetMin, State.MaxFullBody, State.MinSpeed));
			Check(State, State.bWeaponShownAlways && State.MaxIKAlpha < 0.05f,
				FString::Printf(TEXT("weapon stays in the hand (game designer 2026-10-26), left-hand IK off (max alpha %.2f) during the storm pose"), State.MaxIKAlpha));
			State.Target = SpawnTarget(World, State, *Op);
			Op->bForceHitForTesting = true;
			const bool bShot = Op->ShootAtTarget(State.Target.Get(), 0.f);
			Check(State, bShot, TEXT("a shot fired while walking (combat aim)"));
			Enter(State, EStage::Aim);
			return true;
		}
		case EStage::Aim:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() <= 0.01f)
			{
				LogLayer(TEXT("aim"), *Anim, *Op);
				Check(State, State.StageTime <= 0.5f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::Aiming,
					FString::Printf(TEXT("aims: storm weight 0 after %.2f s (<= 0.5), block %s"), State.StageTime, BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock())));
				Check(State, Op->WeaponMesh && Op->WeaponMesh->IsVisible(), TEXT("aims: the weapon is back in the hands"));
				State.MaxWeight = 0.f;
				Enter(State, EStage::AimHold);
			}
			else if (State.StageTime > 1.5f)
			{
				LogLayer(TEXT("aim"), *Anim, *Op);
				Check(State, false, TEXT("aims: storm weight never 0"));
				Enter(State, EStage::AimHold);
			}
			return true;
		case EStage::AimHold:
			KeepWalking(State, *Op);
			State.MaxWeight = FMath::Max(State.MaxWeight, Anim->GetBlizzardWalkWeight());
			if (State.StageTime >= 1.f)
			{
				Check(State, State.MaxWeight < 0.01f, FString::Printf(TEXT("aim held 1 s: storm weight stays 0 (max %.2f)"), State.MaxWeight));
				if (AActor* Target = State.Target.Get())
				{
					Target->Destroy();
				}
				Enter(State, EStage::AimOver);
			}
			return true;
		case EStage::AimOver:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() > 0.8f)
			{
				LogLayer(TEXT("aim over"), *Anim, *Op);
				Check(State, true, FString::Printf(TEXT("aim over: storm upper body back after %.1f s"), State.StageTime));
				// Indoors: a Closed camera zone (heated room) spawned over the whole lane.
				ACameraZoneVolume* Zone = World->SpawnActor<ACameraZoneVolume>(State.O + State.F * (LaneLength * 0.5f) + FVector(0.f, 0.f, 100.f), State.F.Rotation());
				if (Zone)
				{
					Zone->Environment = ECameraZoneEnvironment::Closed;
					Zone->bEnableCameraSwitch = false;
					Zone->SetActorScale3D(FVector(3.f, 3.f, 1.5f));
				}
				State.Zone = Zone;
				Check(State, Zone && ACameraZoneVolume::IsShelteredLocation(World, Op->GetActorLocation()), TEXT("indoor zone spawned over the lane"));
				Enter(State, EStage::Indoors);
			}
			else if (State.StageTime > 10.f)
			{
				LogLayer(TEXT("aim over"), *Anim, *Op);
				Check(State, false, TEXT("aim over: storm upper body never came back"));
				return Finish(State);
			}
			return true;
		case EStage::Indoors:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() <= 0.01f || State.StageTime > 1.5f)
			{
				LogLayer(TEXT("indoors"), *Anim, *Op);
				Check(State, Anim->GetBlizzardWalkWeight() <= 0.01f && State.StageTime <= 0.8f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::Sheltered,
					FString::Printf(TEXT("indoors (Closed zone): storm weight %.2f = 0 after %.2f s, block %s"), Anim->GetBlizzardWalkWeight(), State.StageTime,
						BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock())));
				if (ACameraZoneVolume* Zone = State.Zone.Get())
				{
					Zone->Destroy();
				}
				Enter(State, EStage::Outdoors);
			}
			return true;
		case EStage::Outdoors:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() > 0.8f || State.StageTime > 2.f)
			{
				Check(State, Anim->GetBlizzardWalkWeight() > 0.8f, FString::Printf(TEXT("outdoors again: storm weight %.2f > 0.8"), Anim->GetBlizzardWalkWeight()));
				Op->StopOperative();
				Enter(State, EStage::Stop);
			}
			return true;
		case EStage::Stop:
			if (Op->GetVelocity().Size2D() > 5.f)
			{
				State.MarkTime = State.StageTime;
			}
			if (State.StageTime - State.MarkTime > 0.6f || State.StageTime > 4.f)
			{
				LogLayer(TEXT("stopped"), *Anim, *Op);
				Check(State, Anim->GetBlizzardWalkWeight() <= 0.01f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::NotWalking,
					FString::Printf(TEXT("stopped: storm weight %.2f = 0 (block %s)"), Anim->GetBlizzardWalkWeight(), BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock())));
				Enter(State, EStage::WalkAgain);
			}
			return true;
		case EStage::WalkAgain:
			KeepWalking(State, *Op);
			if (Anim->GetBlizzardWalkWeight() > 0.8f || State.StageTime > 4.f)
			{
				Check(State, Anim->GetBlizzardWalkWeight() > 0.8f, FString::Printf(TEXT("walking again: storm weight %.2f > 0.8"), Anim->GetBlizzardWalkWeight()));
				SetWeather(World, EWeatherState::Clear);
				Enter(State, EStage::ClearAgain);
			}
			return true;
		case EStage::ClearAgain:
			KeepWalking(State, *Op);
			if (State.StageTime >= 0.6f)
			{
				LogLayer(TEXT("clear again"), *Anim, *Op);
				Check(State, Anim->GetBlizzardWalkWeight() <= 0.01f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::NoBlizzard,
					FString::Printf(TEXT("Clear weather while walking: storm weight %.2f = 0 (block %s)"), Anim->GetBlizzardWalkWeight(), BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock())));
				Check(State, Op->WeaponMesh && Op->WeaponMesh->IsVisible(), TEXT("Clear weather: the weapon is shown"));
				SetWeather(World, EWeatherState::Blizzard);
				Op->SetStance(EOperativeStance::Crouching);
				State.MaxWeight = 0.f;
				Enter(State, EStage::Crouch);
			}
			return true;
		case EStage::Crouch:
			KeepWalking(State, *Op);
			if (State.StageTime > 1.f)
			{
				State.MaxWeight = FMath::Max(State.MaxWeight, Anim->GetBlizzardWalkWeight());
			}
			if (State.StageTime >= 3.f)
			{
				LogLayer(TEXT("crouch walk"), *Anim, *Op);
				Check(State, State.MaxWeight <= 0.01f && Anim->GetBlizzardWalkBlock() == EBlizzardWalkBlock::Stance && Op->GetVelocity().Size2D() > 30.f,
					FString::Printf(TEXT("crouch-walking in the blizzard: storm weight max %.2f = 0 (block %s, speed %.0f)"), State.MaxWeight,
						BlizzardWalkRules::BlockName(Anim->GetBlizzardWalkBlock()), Op->GetVelocity().Size2D()));
				Op->SetStance(EOperativeStance::Standing);
				SetWeather(World, EWeatherState::Clear);
				return Finish(State);
			}
			return true;

		// --- "clip": ~10 s rendered at 30 fps ---
		case EStage::ClipWalk:
			KeepWalking(State, *Op);
			if (State.StageTime >= 5.5f)
			{
				LogLayer(TEXT("clip walk"), *Anim, *Op);
				Op->StopOperative();
				Enter(State, EStage::ClipStop);
			}
			return true;
		case EStage::ClipStop:
			if (State.StageTime >= 1.8f)
			{
				LogLayer(TEXT("clip stop"), *Anim, *Op);
				KeepWalking(State, *Op);
				Enter(State, EStage::ClipAim);
			}
			return true;
		case EStage::ClipAim:
			if (State.StageTime < 0.9f)
			{
				KeepWalking(State, *Op); // the storm pose comes back while he walks on ...
				return true;
			}
			if (!State.Target.IsValid())
			{
				LogLayer(TEXT("clip before aim"), *Anim, *Op);
				State.Target = SpawnTarget(World, State, *Op);
				Op->bForceHitForTesting = true;
				Op->ShootAtTarget(State.Target.Get(), 0.f); // ... and goes when he shoots (combat aim)
			}
			KeepWalking(State, *Op);
			if (State.StageTime >= 2.f)
			{
				LogLayer(TEXT("clip aim"), *Anim, *Op);
				Enter(State, EStage::ClipEnd);
			}
			return true;
		case EStage::ClipEnd:
			if (State.StageTime >= 0.4f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("BlizzardWalkSmoke clip: %d frames in %s"), State.Frame, *State.Dir);
				return Finish(State);
			}
			return true;
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bClip = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("clip"), ESearchCase::IgnoreCase); });
		State->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/BlizzardWalk"));
		// The clip ticks every rendered frame (-benchmark -fps=30); the check every 0.05 s.
		const float Interval = State->bClip ? 0.f : StepSeconds;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), Interval);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.BlizzardWalkSmoke"),
		TEXT("Dev check: the storm upper body while walking in a blizzard (on / aim / indoors / stop / clear / crouch); PASS / FAIL. Arg \"clip\": rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
