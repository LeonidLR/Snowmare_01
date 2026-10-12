// Dev-only headless check of the weather idle poses (user request 2026-10-12, Survival/WarmIdleRules + UWarmIdleSubsystem) on
// L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.WarmIdleSmoke
//  1. cold idle: the commander (male) and the medic (Female Soldier) cold (60 %) and idle in exploration -> the Cold_1 set
//     (AS_He_* / AS_She_*); an order to the commander ends his pose at once (no busy time) while he walks;
//  2. barrel: a burning barrel next to the commander -> he walks to a hand-warming place ~90 cm from it, faces it, plays
//     AS_He_Cold_Fire_1 (weapon hidden); the barrel goes out -> the End clip, weapon back;
//  3. campfire, 3 operatives (cold 55 / 30 / 70): the two coldest (medic, commander) sit on the inner ring (distinct seats,
//     facing the fire, Cold_Fire_2), the warmest (engineer) stands on the outer ring warming his hands (Cold_Fire_1); a cold
//     change does not swap the roles; the cold drops faster at the fire exploring (x1);
//  4. an order to the engineer ends only HIS pose; the sitters keep sitting;
//  5. gas stove (commander 60, medic 40, engineer 10): two sit, one stands;
//  6. the wave fight starts: nobody sits / warms hands any more (End clips, then nothing) and the warming at the fire is x0.5.
// Headless runs do not render: the checks read the anim instance's montage / clip state, not bones.
// "shot" (rendered, offscreen: UnrealEditor.exe <uproject> /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720
// -ExecCmds="CodexTactics.WarmIdleSmoke shot") saves stills to Saved/Screenshots/WeatherAnim/ (cold idle male / female, barrel,
// campfire sitting).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/FirePuddleActor.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Data/TacticalHeatTuning.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/CampfireActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/PortableStoveActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Survival/WarmIdleSubsystem.h"
#include "TimerManager.h"

namespace WarmIdleSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 150.f;

	enum class EStage : uint8
	{
		ColdIdle,
		ColdOrder,
		BarrelLight,
		BarrelWarm,
		BarrelOut,
		CampfireLight,
		CampfireSeated,
		CampfireOrder,
		StoveLight,
		StoveSeated,
		Combat,
		CombatSettle,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::ColdIdle;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<ABarrelActor> Barrel;
		TWeakObjectPtr<ADeployableActor> Fire;
		TWeakObjectPtr<ADeployableActor> Stove;
		/** Rendered stills ("shot"): queued photos, the stage machine waits while they are taken. */
		bool bShot = false;
		struct FPhoto
		{
			TWeakObjectPtr<AActor> Subject;
			FString Name;
			float Distance = 380.f;
			float YawOffset = 0.f;
			float Delay = 0.f;
			float Pitch = -15.f;
			float Time = 0.f;
		};
		TArray<FPhoto> Photos;
		TWeakObjectPtr<ACameraActor> Camera;
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

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("WarmIdleSmoke"));
		return false;
	}

	AOperativeCharacter* FindRole(UWorld* World, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	UOperativeAnimInstance* Anim(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	FVector Feet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	/** True when the clip log of Operative has a clip whose name contains Part. */
	bool Played(const AOperativeCharacter* Operative, const TCHAR* Part)
	{
		const UOperativeAnimInstance* Instance = Anim(Operative);
		return Instance && Instance->GetWarmClipLog().ContainsByPredicate([Part](const FString& Name) { return Name.Contains(Part); });
	}

	FString PoseText(const AOperativeCharacter* Operative)
	{
		const UOperativeAnimInstance* Instance = Anim(Operative);
		return FString::Printf(TEXT("%s pose %s step %s clip %s"), Operative ? *Operative->DisplayName.ToString() : TEXT("-"),
			Instance ? *UEnum::GetValueAsString(Instance->WarmIdlePose) : TEXT("-"),
			Instance ? *UEnum::GetValueAsString(Instance->GetWarmClipStep()) : TEXT("-"),
			Instance && Instance->GetWarmClip() ? *Instance->GetWarmClip()->GetName() : TEXT("-"));
	}

	bool InPose(const AOperativeCharacter* Operative, EWarmIdlePose Pose)
	{
		const UOperativeAnimInstance* Instance = Anim(Operative);
		return Instance && Instance->bWarmIdleActive && Instance->WarmIdlePose == Pose && Instance->GetWarmClipStep() != EWarmClipStep::End;
	}

	float FacingError(const AOperativeCharacter* Operative, const AActor* Fire)
	{
		const float Want = WarmIdleRules::FaceYaw(Operative->GetActorLocation(), Fire->GetActorLocation());
		return FMath::Abs(FMath::FindDeltaAngleDegrees(Operative->GetActorRotation().Yaw, Want));
	}

	ADeployableActor* SpawnDeployable(UWorld* World, EDeployableType Type, const FVector& Ground)
	{
		const URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		UClass* Class = Relocation ? Relocation->GetDeployableClass(Type).Get() : nullptr;
		if (!Class)
		{
			Class = Type == EDeployableType::Campfire ? ACampfireActor::StaticClass() : APortableStoveActor::StaticClass();
		}
		const ADeployableActor* Default = Class->GetDefaultObject<ADeployableActor>();
		const float HalfHeight = Default && Default->Box ? Default->Box->GetUnscaledBoxExtent().Z : 20.f;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ADeployableActor>(Class, Ground + FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator, Params);
	}

	/** Holds the followers (no formation pull) and puts the squad in a triangle Radius around Centre; returns Centre. */
	FVector PlaceAround(UWorld* World, const TArray<AOperativeCharacter*>& Squad, const FVector& Centre, float Radius)
	{
		World->GetSubsystem<USquadSubsystem>()->SetFollowersHolding(true);
		for (int32 Index = 0; Index < Squad.Num(); ++Index)
		{
			const FVector Around = Centre + FVector(Radius, 0.f, 0.f).RotateAngleAxis(120.f * Index + 180.f, FVector::UpVector);
			Squad[Index]->StopOperative();
			Squad[Index]->TeleportTo(SmokeUtils::FreeSpot(World, Around + FVector(0.f, 0.f, 92.f), Squad[Index]), Squad[Index]->GetActorRotation(), false, true);
		}
		return Centre;
	}

	/**
	 * A clean slate between the stages (the test intent is poses, not damage): the barrel stage leaves a fire puddle (core 120 cm, 12-15 s)
	 * exactly where the later stages put the squad - a Medic-Sapper with a low max HP (BP_Operative_FemaleSoldier: 120) burned to death in
	 * it 1 s after sitting at the stove (2026-10-12). Puddles go, everybody is healed; the HP is logged.
	 */
	void ResetBodies(UWorld* World, const TCHAR* Label)
	{
		int32 Puddles = 0;
		for (TActorIterator<AFirePuddleActor> It(World); It; ++It)
		{
			It->Destroy();
			++Puddles;
		}
		FString Hp;
		for (AOperativeCharacter* Each : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (UHealthComponent* Health = Each->HealthComponent)
			{
				Hp += FString::Printf(TEXT(" %s %.0f/%.0f"), *Each->DisplayName.ToString(), Health->GetCurrentHealth(), Health->GetMaxHealth());
				Health->Heal(Health->GetMaxHealth() - Health->GetCurrentHealth());
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s - %d fire puddle(s) removed, HP before the heal:%s"), Label, Puddles, *Hp);
	}

	/** Checks the fire roles: Sitters sit on distinct inner seats facing the fire, Standers stand on the outer ring. */
	void CheckFireRoles(FState& State, UWorld* World, const AActor* Fire, const TArray<AOperativeCharacter*>& Sitters,
		const TArray<AOperativeCharacter*>& Standers, float SeatDistance, float StandDistance, const TCHAR* Label)
	{
		const UWarmIdleSubsystem* Warm = World->GetSubsystem<UWarmIdleSubsystem>();
		TArray<FVector> Seats;
		for (const AOperativeCharacter* Each : Sitters)
		{
			const float Distance = FVector::Dist2D(Each->GetActorLocation(), Fire->GetActorLocation());
			const UWarmIdleSubsystem::FDebugState Debug = Warm->GetDebugState(Each);
			Check(State, InPose(Each, EWarmIdlePose::SitWarm) && Debug.bInnerRing && FMath::Abs(Distance - SeatDistance) <= 45.f
				&& FacingError(Each, Fire) <= 30.f && Played(Each, TEXT("Cold_Fire_2_")),
				FString::Printf(TEXT("%s: %s sits at %.0f cm (seat ring %.0f), facing error %.0f deg [%s]"), Label, *Each->DisplayName.ToString(),
					Distance, SeatDistance, FacingError(Each, Fire), *PoseText(Each)));
			Seats.Add(Each->GetActorLocation());
		}
		for (int32 A = 0; A < Seats.Num(); ++A)
		{
			for (int32 B = A + 1; B < Seats.Num(); ++B)
			{
				Check(State, FVector::Dist2D(Seats[A], Seats[B]) >= 60.f, FString::Printf(TEXT("%s: distinct seats %.0f cm apart"), Label,
					FVector::Dist2D(Seats[A], Seats[B])));
			}
		}
		for (const AOperativeCharacter* Each : Standers)
		{
			const float Distance = FVector::Dist2D(Each->GetActorLocation(), Fire->GetActorLocation());
			Check(State, InPose(Each, EWarmIdlePose::HandWarm) && !Warm->GetDebugState(Each).bInnerRing && FMath::Abs(Distance - StandDistance) <= 45.f
				&& FacingError(Each, Fire) <= 30.f && Played(Each, TEXT("Cold_Fire_1_")),
				FString::Printf(TEXT("%s: %s stands warming his hands at %.0f cm (outer ring %.0f), facing error %.0f deg [%s]"), Label,
					*Each->DisplayName.ToString(), Distance, StandDistance, FacingError(Each, Fire), *PoseText(Each)));
		}
	}

	bool AllPosed(const TArray<AOperativeCharacter*>& Sitters, const TArray<AOperativeCharacter*>& Standers)
	{
		for (const AOperativeCharacter* Each : Sitters)
		{
			if (!InPose(Each, EWarmIdlePose::SitWarm))
			{
				return false;
			}
		}
		for (const AOperativeCharacter* Each : Standers)
		{
			if (!InPose(Each, EWarmIdlePose::HandWarm))
			{
				return false;
			}
		}
		return true;
	}

	/** Queues a rendered still of Subject (front view + YawOffset, from Distance) after Delay seconds ("shot" only). */
	void QueuePhoto(FState& State, AActor* Subject, const FString& Name, float Distance, float YawOffset, float Delay, float Pitch = -15.f)
	{
		if (State.bShot && Subject)
		{
			FState::FPhoto Photo;
			Photo.Subject = Subject;
			Photo.Name = Name;
			Photo.Distance = Distance;
			Photo.YawOffset = YawOffset;
			Photo.Delay = Delay;
			Photo.Pitch = Pitch;
			State.Photos.Add(Photo);
		}
	}

	/** Takes the first queued photo; true while one is in progress (the stage machine waits). */
	bool StepPhotos(UWorld* World, FState& State)
	{
		if (State.Photos.IsEmpty())
		{
			return false;
		}
		FState::FPhoto& Photo = State.Photos[0];
		const float Before = Photo.Time;
		Photo.Time += StepSeconds;
		AActor* Subject = Photo.Subject.Get();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Subject || !PC)
		{
			State.Photos.RemoveAt(0);
			return true;
		}
		const float Aim = Photo.Delay;
		if (Before < Aim + StepSeconds && Photo.Time >= Aim + StepSeconds)
		{
			if (!State.Camera.IsValid())
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State.Camera = World->SpawnActor<ACameraActor>(Subject->GetActorLocation(), FRotator::ZeroRotator, Params);
				if (State.Camera.IsValid())
				{
					State.Camera->GetCameraComponent()->SetFieldOfView(55.f);
					State.Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				}
			}
			if (ACameraActor* Camera = State.Camera.Get())
			{
				const FVector Target = Subject->GetActorLocation() + FVector(0.f, 0.f, Cast<APawn>(Subject) ? -5.f : 40.f);
				const FRotator View(Photo.Pitch, Subject->GetActorRotation().Yaw + 180.f + Photo.YawOffset, 0.f);
				const FVector Eye = Target - View.Vector() * Photo.Distance;
				Camera->SetActorLocationAndRotation(Eye, View);
				PC->SetViewTarget(Camera);
			}
		}
		if (Before < Aim + 1.25f && Photo.Time >= Aim + 1.25f)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("WeatherAnim") / (Photo.Name + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
		}
		if (Photo.Time >= Aim + 1.75f)
		{
			State.Photos.RemoveAt(0);
		}
		return true;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (StepPhotos(World, State))
		{
			State.StageTime -= StepSeconds; // the stage clock waits for the photo
			return true;
		}
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Flow || !Commander || !Engineer || !Medic)
		{
			if (State.Time < 15.f)
			{
				State.StageTime = 0.f;
				return true; // the squad spawns late on a slow start
			}
			FString Who;
			for (const AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
			{
				Who += Member->DisplayName.ToString() + TEXT(" ");
			}
			Check(State, false, FString::Printf(TEXT("squad of three (stage %d; alive in the squad: %s) - see the [Health] line above for who died of what"),
				static_cast<int32>(State.Stage), *Who));
			return Finish(State, false);
		}
		const TArray<AOperativeCharacter*> Squad = { Commander, Engineer, Medic };
		const FTacticalHeatConfig& Heat = TacticalHeatTuning::Get();

		switch (State.Stage)
		{
		case EStage::ColdIdle:
			if (State.StageTime <= StepSeconds)
			{
				Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration"));
				for (AOperativeCharacter* Each : Squad)
				{
					Each->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					Each->StopOperative();
				}
				Commander->ColdLevel = 60.f;
				Medic->ColdLevel = 60.f;
				Engineer->ColdLevel = 10.f;
				Check(State, Anim(Medic) && Anim(Medic)->UsesFemaleWarmClips() && Anim(Commander) && !Anim(Commander)->UsesFemaleWarmClips(),
					TEXT("gender: the medic uses AS_She_*, the commander AS_He_*"));
			}
			else
			{
				Commander->ColdLevel = FMath::Max(Commander->ColdLevel, 60.f);
				Medic->ColdLevel = FMath::Max(Medic->ColdLevel, 60.f);
			}
			if (InPose(Commander, EWarmIdlePose::ColdIdle) && InPose(Medic, EWarmIdlePose::ColdIdle) && State.StageTime >= 4.f)
			{
				Check(State, Played(Commander, TEXT("AS_He_Cold_1_1Start")) && Played(Medic, TEXT("AS_She_Cold_1_1Start")),
					FString::Printf(TEXT("cold + idle: cold idle started (%s; %s)"), *PoseText(Commander), *PoseText(Medic)));
				Check(State, !Anim(Engineer)->bWarmIdleActive, FString::Printf(TEXT("the warm engineer does not shiver (%s)"), *PoseText(Engineer)));
				Check(State, !Commander->WeaponMesh->IsVisible(), TEXT("weapon hidden while shivering"));
				if (State.bShot && State.Photos.IsEmpty() && !State.Camera.IsValid())
				{
					QueuePhoto(State, Commander, TEXT("cold_idle_male"), 420.f, 25.f, 0.f);
					QueuePhoto(State, Medic, TEXT("cold_idle_female"), 420.f, 25.f, 0.f);
					return true; // the order follows the photos
				}
				Commander->OrderMoveTo(Commander->GetActorLocation() + Commander->GetActorForwardVector() * 300.f, false);
				Enter(State, EStage::ColdOrder);
			}
			else if (State.StageTime > 10.f)
			{
				Check(State, false, FString::Printf(TEXT("cold idle within 10 s (%s; %s)"), *PoseText(Commander), *PoseText(Medic)));
				Enter(State, EStage::BarrelLight);
			}
			return true;

		case EStage::ColdOrder:
			if (State.StageTime >= 0.5f)
			{
				Check(State, !Anim(Commander)->bWarmIdleActive && Commander->GetVelocity().Size2D() > 30.f && Commander->WeaponMesh->IsVisible(),
					FString::Printf(TEXT("an order ends the cold idle at once: walking %.0f cm/s, weapon back (%s)"), Commander->GetVelocity().Size2D(),
						*PoseText(Commander)));
				Enter(State, EStage::BarrelLight);
			}
			return true;

		case EStage::BarrelLight:
			if (State.StageTime >= 1.5f)
			{
				SmokeUtils::PlaceSquadAtTestStart(World);
				for (AOperativeCharacter* Each : Squad)
				{
					Each->ColdLevel = 10.f;
				}
				Commander->ColdLevel = 30.f;
				const FVector Ground = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 220.f);
				const float HalfHeight = GetDefault<ABarrelActor>()->Box->GetUnscaledBoxExtent().Z;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(FVector(Ground.X, Ground.Y, Feet(Commander).Z + HalfHeight), FRotator::ZeroRotator, Params);
				Check(State, Barrel && Barrel->IgniteForTurnBased(), TEXT("barrel burning 2.2 m in front of the commander"));
				State.Barrel = Barrel;
				Enter(State, EStage::BarrelWarm);
			}
			return true;

		case EStage::BarrelWarm:
		{
			ABarrelActor* Barrel = State.Barrel.Get();
			if (Barrel && InPose(Commander, EWarmIdlePose::HandWarm) && State.StageTime >= 1.f)
			{
				const float Distance = FVector::Dist2D(Commander->GetActorLocation(), Barrel->GetActorLocation());
				const UWarmIdleSubsystem::FDebugState Debug = World->GetSubsystem<UWarmIdleSubsystem>()->GetDebugState(Commander);
				Check(State, Debug.bHasSpot && FVector::Dist2D(Debug.Spot, Commander->GetActorLocation()) <= 40.f && Distance >= Heat.BarrelStandDistance - 40.f
					&& Distance <= Heat.BarrelStandDistance + 40.f && FacingError(Commander, Barrel) <= 30.f
					&& Played(Commander, TEXT("AS_He_Cold_Fire_1_")) && !Commander->WeaponMesh->IsVisible(),
					FString::Printf(TEXT("barrel: hand warming at %.0f cm, facing error %.0f deg, weapon hidden [%s]"), Distance,
						FacingError(Commander, Barrel), *PoseText(Commander)));
				if (State.bShot && !State.Photos.ContainsByPredicate([](const FState::FPhoto& P) { return P.Name == TEXT("barrel"); }) && State.StageTime < 50.f)
				{
					QueuePhoto(State, State.Barrel.Get(), TEXT("barrel"), 650.f, 0.f, 2.f, -30.f);
					State.StageTime = 50.f; // taken once
					return true;
				}
				Barrel->ExtinguishNow();
				Enter(State, EStage::BarrelOut);
			}
			else if (State.StageTime > 15.f)
			{
				Check(State, false, FString::Printf(TEXT("barrel hand warming within 15 s [%s]"), *PoseText(Commander)));
				Enter(State, EStage::CampfireLight);
			}
			return true;
		}

		case EStage::BarrelOut:
			if (State.StageTime >= 3.5f)
			{
				Check(State, !Anim(Commander)->bWarmIdleActive && Played(Commander, TEXT("AS_He_Cold_Fire_1_6End")) && Commander->WeaponMesh->IsVisible(),
					FString::Printf(TEXT("barrel out: the End clip played, pose over, weapon back [%s]"), *PoseText(Commander)));
				if (AActor* Barrel = State.Barrel.Get())
				{
					Barrel->Destroy();
				}
				ResetBodies(World, TEXT("barrel stage over"));
				Enter(State, EStage::CampfireLight);
			}
			return true;

		case EStage::CampfireLight:
			if (State.StageTime >= 0.5f)
			{
				// Followers held in a triangle 2.2 m round the fire (inside its 3.5 m zone; the formation would pull them about).
				ResetBodies(World, TEXT("campfire stage"));
				SmokeUtils::PlaceSquadAtTestStart(World);
				const FVector FireAt = PlaceAround(World, Squad, SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 250.f), 220.f);
				Commander->ColdLevel = 55.f;
				Engineer->ColdLevel = 30.f;
				Medic->ColdLevel = 70.f;
				ADeployableActor* Fire = SpawnDeployable(World, EDeployableType::Campfire, FireAt);
				if (ACampfireActor* Campfire = Cast<ACampfireActor>(Fire))
				{
					Campfire->Kindle(Commander);
				}
				Check(State, Fire && Cast<ACampfireActor>(Fire) && Cast<ACampfireActor>(Fire)->IsBurning(), TEXT("campfire lit in the middle of the squad"));
				State.Fire = Fire;
				Enter(State, EStage::CampfireSeated);
			}
			return true;

		case EStage::CampfireSeated:
		{
			const AActor* Fire = State.Fire.Get();
			if (Fire && AllPosed({ Medic, Commander }, { Engineer }) && State.StageTime >= 1.f)
			{
				CheckFireRoles(State, World, Fire, { Medic, Commander }, { Engineer }, Heat.CampfireSeatDistance, Heat.CampfireStandDistance, TEXT("campfire"));
				// No flicker: the sitter warmed through stays seated, the stander keeps standing.
				Medic->ColdLevel = 0.f;
				World->GetSubsystem<UWarmIdleSubsystem>()->Evaluate(0.1f);
				Check(State, World->GetSubsystem<UWarmIdleSubsystem>()->GetDebugState(Medic).Role == EWarmFireRole::Sit
					&& World->GetSubsystem<UWarmIdleSubsystem>()->GetDebugState(Engineer).Role == EWarmFireRole::Stand,
					TEXT("roles kept while the cold drops (no flicker)"));
				// Exploration warming at the fire: x1 (8 %/s x fortitude boost).
				Commander->ColdLevel = 50.f;
				Commander->ColdSurvival->StepCold(1.f);
				const float Expected = 50.f - FMath::Max(Commander->ColdSurvival->Config.WarmthRecoveryRate * (1.f + Commander->ColdSurvival->Fortitude
					* Commander->ColdSurvival->Config.FortitudeWarmupPerPoint), Commander->ColdSurvival->GetHeatSourceWarmthRate()) * Heat.ExplorationWarmthMultiplier;
				Check(State, FMath::IsNearlyEqual(Commander->ColdLevel, Expected, 0.05f),
					FString::Printf(TEXT("exploring at the fire: cold 50 -> %.2f in 1 s (x%.1f, expected %.2f)"), Commander->ColdLevel, Heat.ExplorationWarmthMultiplier, Expected));
				Commander->ColdLevel = 30.f;
				if (State.bShot && State.StageTime < 50.f)
				{
					QueuePhoto(State, State.Fire.Get(), TEXT("campfire_sitting"), 700.f, 0.f, 3.5f, -35.f);
					QueuePhoto(State, State.Fire.Get(), TEXT("campfire_sitting_side"), 700.f, 120.f, 0.f, -25.f);
					State.StageTime = 50.f;
					return true;
				}
				// The order goes to the engineer only.
				Engineer->OrderMoveTo(Engineer->GetActorLocation() + (Engineer->GetActorLocation() - Fire->GetActorLocation()).GetSafeNormal2D() * 250.f, false);
				Enter(State, EStage::CampfireOrder);
			}
			else if (State.StageTime > 20.f)
			{
				const UWarmIdleSubsystem::FDebugState D = World->GetSubsystem<UWarmIdleSubsystem>()->GetDebugState(Medic);
				Check(State, false, FString::Printf(TEXT("campfire roles within 20 s [%s] [%s] [%s] medic idle %.1f s heat %d moving %d speed %.0f fire %.0f cm cold %.0f"),
					*PoseText(Medic), *PoseText(Commander), *PoseText(Engineer), D.IdleSeconds, static_cast<int32>(D.Heat), Medic->IsMoving() ? 1 : 0,
					Medic->GetVelocity().Size2D(), State.Fire.IsValid() ? FVector::Dist2D(State.Fire->GetActorLocation(), Medic->GetActorLocation()) : -1.f, Medic->ColdLevel));
				Enter(State, EStage::StoveLight);
			}
			return true;
		}

		case EStage::CampfireOrder:
			if (State.StageTime >= 1.f)
			{
				Check(State, !InPose(Engineer, EWarmIdlePose::HandWarm), FString::Printf(TEXT("the ordered engineer stopped warming [%s]"), *PoseText(Engineer)));
				Check(State, InPose(Medic, EWarmIdlePose::SitWarm) && InPose(Commander, EWarmIdlePose::SitWarm),
					FString::Printf(TEXT("the others keep sitting [%s] [%s]"), *PoseText(Medic), *PoseText(Commander)));
				if (AActor* Fire = State.Fire.Get())
				{
					Fire->Destroy();
				}
				Enter(State, EStage::StoveLight);
			}
			return true;

		case EStage::StoveLight:
			if (State.StageTime >= 3.5f)
			{
				// The stove warms only 2.2 m: the followers' formation slots lie outside it, so they hold a tight triangle round it.
				ResetBodies(World, TEXT("stove stage"));
				SmokeUtils::PlaceSquadAtTestStart(World);
				const FVector Centre = PlaceAround(World, Squad, SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 250.f), 150.f);
				Commander->ColdLevel = 60.f;
				Engineer->ColdLevel = 10.f;
				Medic->ColdLevel = 40.f;
				ADeployableActor* Stove = SpawnDeployable(World, EDeployableType::PortableStove, Centre);
				if (APortableStoveActor* Gas = Cast<APortableStoveActor>(Stove))
				{
					Gas->StartWithGas(180.f);
				}
				Check(State, Cast<APortableStoveActor>(Stove) && Cast<APortableStoveActor>(Stove)->IsBurning(), TEXT("gas stove burning in the middle of the squad"));
				State.Stove = Stove;
				Enter(State, EStage::StoveSeated);
			}
			return true;

		case EStage::StoveSeated:
		{
			const AActor* Stove = State.Stove.Get();
			if (Stove && AllPosed({ Commander, Medic }, { Engineer }) && State.StageTime >= 1.f)
			{
				CheckFireRoles(State, World, Stove, { Commander, Medic }, { Engineer }, Heat.StoveSeatDistance, Heat.StoveStandDistance, TEXT("stove"));
				SmokeUtils::StartWaveFight(Flow);
				Enter(State, EStage::Combat);
			}
			else if (State.StageTime > 20.f)
			{
				Check(State, false, FString::Printf(TEXT("stove roles within 20 s [%s] [%s] [%s]"), *PoseText(Commander), *PoseText(Medic), *PoseText(Engineer)));
				SmokeUtils::StartWaveFight(Flow);
				Enter(State, EStage::Combat);
			}
			return true;
		}

		case EStage::Combat:
			if (State.StageTime >= 0.5f)
			{
				Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave fight"));
				Check(State, Anim(Commander)->GetWarmIdleRequest() == EWarmIdlePose::None && Anim(Medic)->GetWarmIdleRequest() == EWarmIdlePose::None
					&& Anim(Engineer)->GetWarmIdleRequest() == EWarmIdlePose::None,
					TEXT("combat: no weather pose requested any more"));
				Check(State, Played(Commander, TEXT("AS_He_Cold_Fire_2_6End")) || !Anim(Commander)->bWarmIdleActive,
					FString::Printf(TEXT("the sitting commander stands up (End clip) [%s]"), *PoseText(Commander)));
				// Combat warming at the stove: x0.5.
				const AActor* Stove = State.Stove.Get();
				Commander->ColdLevel = 50.f;
				const bool bWarm = Commander->ColdSurvival->IsNearHeatSource();
				Commander->ColdSurvival->StepCold(1.f);
				const float Expected = 50.f - FMath::Max(Commander->ColdSurvival->Config.WarmthRecoveryRate * (1.f + Commander->ColdSurvival->Fortitude
					* Commander->ColdSurvival->Config.FortitudeWarmupPerPoint), Commander->ColdSurvival->GetHeatSourceWarmthRate()) * Heat.CombatWarmthMultiplier;
				Check(State, bWarm && FMath::IsNearlyEqual(Commander->ColdLevel, Expected, 0.05f),
					FString::Printf(TEXT("fighting at the stove (%.0f cm): cold 50 -> %.2f in 1 s (x%.1f, expected %.2f)"),
						Stove ? FVector::Dist2D(Stove->GetActorLocation(), Commander->GetActorLocation()) : -1.f, Commander->ColdLevel,
						Heat.CombatWarmthMultiplier, Expected));
				Enter(State, EStage::CombatSettle);
			}
			return true;

		case EStage::CombatSettle:
			if (State.StageTime >= 4.f)
			{
				bool bNone = true;
				for (const AOperativeCharacter* Each : Squad)
				{
					bNone &= !Anim(Each)->bWarmIdleActive;
				}
				Check(State, bNone, FString::Printf(TEXT("in combat nobody sits or warms hands [%s] [%s] [%s]"), *PoseText(Commander), *PoseText(Medic),
					*PoseText(Engineer)));
				return Finish(State, true);
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bShot)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = bShot;
		SmokeUtils::PlaceSquadAtTestStart(World);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State);
		}), StepSeconds);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const bool bShot = Args.Contains(TEXT("shot"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, bShot]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, bShot);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WarmIdleSmoke"),
		TEXT("Dev check (2026-10-12): weather idle poses - cold idle, barrel hand warming, campfire / stove seats by cold, orders and combat end them; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
