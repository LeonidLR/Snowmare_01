// Dev-only headless check of the weather idle poses (user request 2026-10-12, Survival/WarmIdleRules + UWarmIdleSubsystem) on
// L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.WarmIdleSmoke
//  1. cold idle: the commander (male) and the medic (Female Soldier) cold (60 %) and idle in exploration -> the Cold_1 set
//     (AS_He_* / AS_She_*); 2026-10-15: with cold_idle_variant_chance forced to 1 (cooldown 0; the run is seeded, -FixedSeed) the
//     Polygonmaker variant Anim_Regular_idle_cold plays inside the cold idle for both bodies, then the loop goes on; an order to the
//     commander ends his pose at once (no busy time) while he walks;
//  2. barrel: a burning barrel next to the commander -> he walks to a hand-warming place ~90 cm from it, faces it, plays
//     AS_He_Cold_Fire_1 (weapon hidden); the barrel goes out -> the End clip, weapon back;
//  3. campfire, 3 operatives (cold 55 / 30 / 70): the two coldest (medic, commander) sit on the inner ring (distinct seats,
//     facing the fire, Cold_Fire_2), the warmest (engineer) stands on the outer ring warming his hands (Cold_Fire_1); a cold
//     change does not swap the roles; the cold drops faster at the fire exploring (x1);
//  4. an order to the engineer ends only HIS pose; the sitters keep sitting;
//  5. gas stove (commander 60, medic 40, engineer 10): two sit, one stands;
//  6. the wave fight starts: nobody sits / warms hands any more (End clips, then nothing) and the warming at the fire is x0.5;
//  7. squad gathers at the fire (user decision 2026-10-12): the followers are NOT held, 9 m / 11 m from a campfire the leader walks
//     up to (then from a burning stove he sits at) -> both walk to its ring, the two coldest sit, the third stands right beside them; the leader leaves -> they follow him again; an order to
//     one follower, a guard (T) and solo mode keep that one out of the pull; in the wave fight nobody is pulled.
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
	constexpr float Timeout = 420.f;

	enum class EStage : uint8
	{
		ColdIdle,
		ColdVariant,
		ColdOrder,
		BarrelLight,
		BarrelWarm,
		BarrelOut,
		CampfireLight,
		CampfireSeated,
		CampfireOrder,
		StoveLight,
		StoveSeated,
		PullSetup,
		PullWalk,
		PullLeave,
		OrderSetup,
		OrderCheck,
		GuardSetup,
		GuardCheck,
		SoloSetup,
		SoloCheck,
		FinalSetup,
		FinalSeated,
		Combat,
		CombatSettle,
		CombatPull,
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
		/** Squad-gathers scenes: where the followers started (Medic, Engineer), cm from the stove, and checks done once per stage. */
		float StartDistMedic = 0.f;
		float StartDistEngineer = 0.f;
		FVector HeldMedicAt = FVector::ZeroVector;
		FVector HeldEngineerAt = FVector::ZeroVector;
		bool bStageChecked = false;
		/** The heat config while the cold idle variant is forced (restored after). */
		TOptional<FTacticalHeatConfig> SavedHeat;
		/** The squad-gathers scenes run twice: first at a campfire (the leader walks up to it), then at the gas stove (the leader stands at it). */
		bool bCampfire = true;
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

	/** Puts the followers Medic / Engineer DistMedic / DistEngineer (cm) from Centre on open ground (a walkable, free spot). */
	void PlaceFollowersAway(UWorld* World, FState& State, AOperativeCharacter* Leader, AOperativeCharacter* Medic, AOperativeCharacter* Engineer,
		const FVector& Centre, float DistMedic, float DistEngineer)
	{
		const FVector Forward = Leader->GetActorForwardVector();
		AOperativeCharacter* Followers[2] = { Medic, Engineer };
		const float Distances[2] = { DistMedic, DistEngineer };
		const float Angles[2] = { 60.f, -60.f };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Dir = Forward.RotateAngleAxis(Angles[Index], FVector::UpVector);
			const FVector Spot = SmokeUtils::ClearPoint(World, Centre, Centre + Dir * Distances[Index]);
			Followers[Index]->StopOperative();
			Followers[Index]->TeleportTo(SmokeUtils::FreeSpot(World, Spot + FVector(0.f, 0.f, 92.f), Followers[Index]), Followers[Index]->GetActorRotation(), false, true);
		}
		State.StartDistMedic = FVector::Dist2D(Medic->GetActorLocation(), Centre);
		State.StartDistEngineer = FVector::Dist2D(Engineer->GetActorLocation(), Centre);
	}

	/**
	 * The squad-gathers scene: followers free (not held), a gas stove burning with the leader (Commander, cold 60) 120 cm from it, the
	 * Medic (cold 40) 9 m and the Engineer (cold 10) 11 m away. The old stove goes.
	 */
	void SetupPullScene(UWorld* World, FState& State, AOperativeCharacter* Commander, AOperativeCharacter* Medic, AOperativeCharacter* Engineer,
		bool bCampfire = false)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (AActor* Old = State.Stove.Get())
		{
			Old->Destroy();
		}
		ResetBodies(World, TEXT("pull scene"));
		if (Squad->IsSoloMode())
		{
			Squad->ExitSoloMode();
		}
		Squad->SetFollowersHolding(false);
		SmokeUtils::PlaceSquadAtTestStart(World);
		if (Squad->GetLeader() != Commander)
		{
			Squad->SetLeader(Commander, true);
		}
		for (AOperativeCharacter* Each : { Commander, Medic, Engineer })
		{
			Each->StopOperative();
		}
		const FVector Centre = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) + Commander->GetActorForwardVector() * 250.f);
		ADeployableActor* Stove = SpawnDeployable(World, bCampfire ? EDeployableType::Campfire : EDeployableType::PortableStove, Centre);
		if (APortableStoveActor* Gas = Cast<APortableStoveActor>(Stove))
		{
			Gas->StartWithGas(180.f);
		}
		if (ACampfireActor* Campfire = Cast<ACampfireActor>(Stove))
		{
			Campfire->Kindle(Commander);
		}
		State.Stove = Stove;
		// The stove scene: the leader already sits next to it. The campfire scene: he starts 6 m away and WALKS up to it.
		const float LeaderDistance = bCampfire ? 600.f : 120.f;
		Commander->TeleportTo(SmokeUtils::FreeSpot(World, Centre + FVector(0.f, 0.f, 92.f) - Commander->GetActorForwardVector() * LeaderDistance, Commander),
			Commander->GetActorRotation(), false, true);
		if (bCampfire)
		{
			const FVector Dir = (Commander->GetActorLocation() - Centre).GetSafeNormal2D();
			Commander->OrderMoveTo(SmokeUtils::ClearPoint(World, Feet(Commander), Centre + Dir * 160.f), false);
		}
		Commander->ColdLevel = 60.f;
		Medic->ColdLevel = 40.f;
		Engineer->ColdLevel = 10.f;
		PlaceFollowersAway(World, State, Commander, Medic, Engineer, Centre, 900.f, 1100.f);
		State.bStageChecked = false;
	}

	/** True while Follower is being pulled to the stove, by the subsystem's own account. */
	bool IsPulledTo(UWorld* World, const AOperativeCharacter* Follower, const AActor* Stove)
	{
		return Stove && World->GetSubsystem<UWarmIdleSubsystem>()->GetFollowerPullSource(Follower) == Stove;
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
				// The medic's body follows the designer's roster (BP_CodexTacticsGameMode, 2026-10-14): She clips only on the Female Soldier body.
				const bool bMedicFemale = Medic->GetMesh()->GetSkeletalMeshAsset() && Medic->GetMesh()->GetSkeletalMeshAsset()->GetName().Contains(TEXT("Female"));
				Check(State, Anim(Medic) && Anim(Medic)->UsesFemaleWarmClips() == bMedicFemale && Anim(Commander) && !Anim(Commander)->UsesFemaleWarmClips(),
					FString::Printf(TEXT("gender: the medic uses %s, the commander AS_He_*"), bMedicFemale ? TEXT("AS_She_*") : TEXT("AS_He_*")));
			}
			else
			{
				Commander->ColdLevel = FMath::Max(Commander->ColdLevel, 60.f);
				Medic->ColdLevel = FMath::Max(Medic->ColdLevel, 60.f);
			}
			if (InPose(Commander, EWarmIdlePose::ColdIdle) && InPose(Medic, EWarmIdlePose::ColdIdle) && State.StageTime >= 4.f)
			{
				Check(State, Played(Commander, TEXT("AS_He_Cold_1_1Start")) && Played(Medic, Anim(Medic)->UsesFemaleWarmClips() ? TEXT("AS_She_Cold_1_1Start") : TEXT("AS_He_Cold_1_1Start")),
					FString::Printf(TEXT("cold + idle: cold idle started (%s; %s)"), *PoseText(Commander), *PoseText(Medic)));
				Check(State, !Anim(Engineer)->bWarmIdleActive, FString::Printf(TEXT("the warm engineer does not shiver (%s)"), *PoseText(Engineer)));
				Check(State, !Commander->WeaponMesh->IsVisible(), TEXT("weapon hidden while shivering"));
				if (State.bShot && State.Photos.IsEmpty() && !State.Camera.IsValid())
				{
					QueuePhoto(State, Commander, TEXT("cold_idle_male"), 420.f, 25.f, 0.f);
					QueuePhoto(State, Medic, TEXT("cold_idle_female"), 420.f, 25.f, 0.f);
					return true; // the order follows the photos
				}
				// The variant inside the cold idle (game designer 2026-10-15): forced chance 1 / cooldown 0 for this stage.
				State.SavedHeat = Heat;
				FTacticalHeatConfig Forced = Heat;
				Forced.ColdIdleVariantChance = 1.f;
				Forced.ColdIdleVariantCooldownSeconds = 0.f;
				TacticalHeatTuning::Set(Forced);
				Enter(State, EStage::ColdVariant);
			}
			else if (State.StageTime > 10.f)
			{
				Check(State, false, FString::Printf(TEXT("cold idle within 10 s (%s; %s)"), *PoseText(Commander), *PoseText(Medic)));
				Enter(State, EStage::BarrelLight);
			}
			return true;

		case EStage::ColdVariant:
		{
			Commander->ColdLevel = FMath::Max(Commander->ColdLevel, 60.f);
			Medic->ColdLevel = FMath::Max(Medic->ColdLevel, 60.f);
			const bool bBoth = Anim(Commander)->GetColdVariantsPlayed() >= 1 && Anim(Medic)->GetColdVariantsPlayed() >= 1;
			if (!bBoth && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bBoth && Played(Commander, TEXT("Anim_Regular_idle_cold")) && Played(Medic, TEXT("Anim_Regular_idle_cold"))
				&& InPose(Commander, EWarmIdlePose::ColdIdle) && InPose(Medic, EWarmIdlePose::ColdIdle),
				FString::Printf(TEXT("cold idle variant Anim_Regular_idle_cold plays inside the cold idle (commander %d, medic %d, %.1f s; %s; %s)"),
					Anim(Commander)->GetColdVariantsPlayed(), Anim(Medic)->GetColdVariantsPlayed(), State.StageTime, *PoseText(Commander), *PoseText(Medic)));
			if (State.SavedHeat.IsSet())
			{
				TacticalHeatTuning::Set(State.SavedHeat.GetValue());
				State.SavedHeat.Reset();
			}
			Commander->OrderMoveTo(Commander->GetActorLocation() + Commander->GetActorForwardVector() * 300.f, false);
			Enter(State, EStage::ColdOrder);
			return true;
		}

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
				Enter(State, EStage::PullSetup);
			}
			else if (State.StageTime > 20.f)
			{
				Check(State, false, FString::Printf(TEXT("stove roles within 20 s [%s] [%s] [%s]"), *PoseText(Commander), *PoseText(Medic), *PoseText(Engineer)));
				Enter(State, EStage::PullSetup);
			}
			return true;
		}

		case EStage::PullSetup:
			Check(State, Heat.bWarmPullFollowers, TEXT("squad gathers: warm_pull_followers is on"));
			SetupPullScene(World, State, Commander, Medic, Engineer, State.bCampfire);
			Check(State, State.StartDistMedic >= 600.f && State.StartDistMedic <= 1500.f && State.StartDistEngineer >= 600.f && State.StartDistEngineer <= 1500.f,
				FString::Printf(TEXT("pull scene (%s): followers start %.0f / %.0f cm from the fire (formation, not held), the leader %s"),
					State.bCampfire ? TEXT("campfire") : TEXT("stove"), State.StartDistMedic, State.StartDistEngineer,
					State.bCampfire ? TEXT("walks up to it") : TEXT("sits at it")));
			Enter(State, EStage::PullWalk);
			return true;

		case EStage::PullWalk:
		{
			const AActor* Stove = State.Stove.Get();
			if (!State.bStageChecked && State.StageTime >= (State.bCampfire ? 8.f : 5.5f))
			{
				State.bStageChecked = true;
				const float Medic2D = Stove ? FVector::Dist2D(Medic->GetActorLocation(), Stove->GetActorLocation()) : -1.f;
				const float Engineer2D = Stove ? FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation()) : -1.f;
				Check(State, IsPulledTo(World, Medic, Stove) && IsPulledTo(World, Engineer, Stove) && Medic2D <= State.StartDistMedic - 150.f
					&& Engineer2D <= State.StartDistEngineer - 150.f,
					FString::Printf(TEXT("the leader is at the %s: both followers are pulled in and walk to it (Medic %.0f -> %.0f cm, Engineer %.0f -> %.0f cm)"),
						State.bCampfire ? TEXT("campfire") : TEXT("stove"), State.StartDistMedic, Medic2D, State.StartDistEngineer, Engineer2D));
			}
			if (Stove && AllPosed({ Medic, Commander }, { Engineer }) && State.StageTime >= 6.f)
			{
				CheckFireRoles(State, World, Stove, { Commander, Medic }, { Engineer }, State.bCampfire ? Heat.CampfireSeatDistance : Heat.StoveSeatDistance,
					State.bCampfire ? Heat.CampfireStandDistance : Heat.StoveStandDistance, State.bCampfire ? TEXT("squad at the campfire") : TEXT("squad at the stove"));
				// The standing one is right beside the sitters (outer ring), not off at formation distance.
				Check(State, FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation()) <= 330.f && FVector::Dist2D(Engineer->GetActorLocation(), Commander->GetActorLocation()) <= 330.f,
					FString::Printf(TEXT("the standing Engineer is right beside the sitters (%.0f / %.0f cm)"), FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation()),
						FVector::Dist2D(Engineer->GetActorLocation(), Commander->GetActorLocation())));
				const FVector Away = SmokeUtils::ClearPoint(World, Feet(Commander), Feet(Commander) - Commander->GetActorForwardVector() * 1300.f);
				Commander->OrderMoveTo(Away, false);
				Enter(State, EStage::PullLeave);
			}
			else if (State.StageTime > 45.f)
			{
				Check(State, false, FString::Printf(TEXT("pulled followers seated within 45 s [%s] [%s] [%s]"), *PoseText(Commander), *PoseText(Medic), *PoseText(Engineer)));
				Enter(State, State.bCampfire ? EStage::PullSetup : EStage::OrderSetup);
				State.bCampfire = false;
			}
			return true;
		}

		case EStage::PullLeave:
		{
			const AActor* Stove = State.Stove.Get();
			const float LeaderFromStove = Stove ? FVector::Dist2D(Commander->GetActorLocation(), Stove->GetActorLocation()) : 0.f;
			const bool bNear = FVector::Dist2D(Medic->GetActorLocation(), Commander->GetActorLocation()) < 700.f
				&& FVector::Dist2D(Engineer->GetActorLocation(), Commander->GetActorLocation()) < 700.f;
			if (LeaderFromStove > 600.f && bNear && !Anim(Medic)->bWarmIdleActive && !Anim(Engineer)->bWarmIdleActive)
			{
				Check(State, !IsPulledTo(World, Medic, Stove) && !IsPulledTo(World, Engineer, Stove),
					FString::Printf(TEXT("the leader left the %s (%.0f cm): the squad un-contracts, the followers are back in the formation within 7 m of him, no pull"),
						State.bCampfire ? TEXT("campfire") : TEXT("stove"), LeaderFromStove));
				Enter(State, State.bCampfire ? EStage::PullSetup : EStage::OrderSetup);
				State.bCampfire = false;
			}
			else if (State.StageTime > 40.f)
			{
				Check(State, false, FString::Printf(TEXT("followers follow the leader again within 40 s (leader %.0f cm from the stove, medic %.0f cm, engineer %.0f cm from him) [%s] [%s]"),
					LeaderFromStove, FVector::Dist2D(Medic->GetActorLocation(), Commander->GetActorLocation()), FVector::Dist2D(Engineer->GetActorLocation(), Commander->GetActorLocation()),
					*PoseText(Medic), *PoseText(Engineer)));
				Enter(State, State.bCampfire ? EStage::PullSetup : EStage::OrderSetup);
				State.bCampfire = false;
			}
			return true;
		}

		case EStage::OrderSetup:
			SetupPullScene(World, State, Commander, Medic, Engineer);
			Enter(State, EStage::OrderCheck);
			return true;

		case EStage::OrderCheck:
		{
			const AActor* Stove = State.Stove.Get();
			if (!State.bStageChecked && State.StageTime >= 5.f)
			{
				State.bStageChecked = true;
				Check(State, IsPulledTo(World, Medic, Stove) && IsPulledTo(World, Engineer, Stove), TEXT("order scene: both followers are being pulled"));
				const FVector Away = SmokeUtils::ClearPoint(World, Feet(Medic), Feet(Medic) + (Medic->GetActorLocation() - Stove->GetActorLocation()).GetSafeNormal2D() * 500.f);
				Medic->OrderMoveTo(Away, false);
			}
			else if (State.bStageChecked && State.StageTime >= 11.f)
			{
				Check(State, !IsPulledTo(World, Medic, Stove) && !InPose(Medic, EWarmIdlePose::SitWarm) && !InPose(Medic, EWarmIdlePose::HandWarm),
					FString::Printf(TEXT("an explicit order to the Medic: she leaves the pull and stays out while the leader sits (%s)"), *PoseText(Medic)));
				Check(State, FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation()) <= 260.f,
					FString::Printf(TEXT("... the Engineer still gathers at the fire (%.0f cm)"), FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation())));
				Enter(State, EStage::GuardSetup);
			}
			return true;
		}

		case EStage::GuardSetup:
			SetupPullScene(World, State, Commander, Medic, Engineer);
			World->GetSubsystem<USquadSubsystem>()->ToggleGuard(Medic);
			State.HeldMedicAt = Medic->GetActorLocation();
			Enter(State, EStage::GuardCheck);
			return true;

		case EStage::GuardCheck:
			if (State.StageTime >= 13.f)
			{
				const AActor* Stove = State.Stove.Get();
				Check(State, Medic->bGuarding && !IsPulledTo(World, Medic, Stove) && FVector::Dist2D(Medic->GetActorLocation(), State.HeldMedicAt) < 100.f,
					FString::Printf(TEXT("a guard (T) holds her post: not pulled, moved %.0f cm"), FVector::Dist2D(Medic->GetActorLocation(), State.HeldMedicAt)));
				Check(State, FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation()) <= 260.f,
					FString::Printf(TEXT("... the Engineer (formation) is gathered at the fire (%.0f cm)"), FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation())));
				World->GetSubsystem<USquadSubsystem>()->ToggleGuard(Medic);
				Enter(State, EStage::SoloSetup);
			}
			return true;

		case EStage::SoloSetup:
			SetupPullScene(World, State, Commander, Medic, Engineer);
			World->GetSubsystem<USquadSubsystem>()->EnterSoloMode();
			State.HeldMedicAt = Medic->GetActorLocation();
			State.HeldEngineerAt = Engineer->GetActorLocation();
			Enter(State, EStage::SoloCheck);
			return true;

		case EStage::SoloCheck:
			if (State.StageTime >= 9.f)
			{
				const AActor* Stove = State.Stove.Get();
				Check(State, World->GetSubsystem<USquadSubsystem>()->IsSoloMode() && !IsPulledTo(World, Medic, Stove) && !IsPulledTo(World, Engineer, Stove)
					&& FVector::Dist2D(Medic->GetActorLocation(), State.HeldMedicAt) < 100.f && FVector::Dist2D(Engineer->GetActorLocation(), State.HeldEngineerAt) < 100.f,
					FString::Printf(TEXT("solo mode: the held followers are not pulled (moved %.0f / %.0f cm)"), FVector::Dist2D(Medic->GetActorLocation(), State.HeldMedicAt),
						FVector::Dist2D(Engineer->GetActorLocation(), State.HeldEngineerAt)));
				World->GetSubsystem<USquadSubsystem>()->ExitSoloMode();
				Enter(State, EStage::FinalSetup);
			}
			return true;

		case EStage::FinalSetup:
			SetupPullScene(World, State, Commander, Medic, Engineer);
			Enter(State, EStage::FinalSeated);
			return true;

		case EStage::FinalSeated:
			if (State.Stove.IsValid() && AllPosed({ Medic, Commander }, { Engineer }) && State.StageTime >= 6.f)
			{
				SmokeUtils::StartWaveFight(Flow);
				Enter(State, EStage::Combat);
			}
			else if (State.StageTime > 45.f)
			{
				Check(State, false, FString::Printf(TEXT("pulled followers seated within 45 s [%s] [%s] [%s]"), *PoseText(Commander), *PoseText(Medic), *PoseText(Engineer)));
				SmokeUtils::StartWaveFight(Flow);
				Enter(State, EStage::Combat);
			}
			return true;

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
				if (const AActor* Stove = State.Stove.Get())
				{
					PlaceFollowersAway(World, State, Commander, Medic, Engineer, Stove->GetActorLocation() - FVector(0.f, 0.f, Stove->GetSimpleCollisionHalfHeight()), 900.f, 1100.f);
				}
				Enter(State, EStage::CombatPull);
			}
			return true;

		case EStage::CombatPull:
			if (State.StageTime >= 5.f)
			{
				const AActor* Stove = State.Stove.Get();
				Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && !IsPulledTo(World, Medic, Stove) && !IsPulledTo(World, Engineer, Stove)
					&& Stove && FVector::Dist2D(Medic->GetActorLocation(), Stove->GetActorLocation()) >= State.StartDistMedic - 100.f
					&& FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation()) >= State.StartDistEngineer - 100.f,
					FString::Printf(TEXT("wave fight (phase %d): nobody is pulled to the stove (Medic %.0f cm, Engineer %.0f cm away, started %.0f / %.0f)"),
						static_cast<int32>(Flow->GetPhase()),
						Stove ? FVector::Dist2D(Medic->GetActorLocation(), Stove->GetActorLocation()) : -1.f,
						Stove ? FVector::Dist2D(Engineer->GetActorLocation(), Stove->GetActorLocation()) : -1.f, State.StartDistMedic, State.StartDistEngineer));
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
