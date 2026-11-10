// Dev-only headless check of the PRONE CRAWL UNDER FIRE (the game designer's CrawlAndShoot_Bug_01, 2026-11-09, ProneCrawlRules;
// docs/systems/animation-setup.md "Prone crawl layer") on L_Gen_Depot_1:
//   Scripts/smoke_parallel.ps1 -Commands ProneCrawlSmoke
// The leader lies prone in an open spot of the Depot, a PMC fighter 14 m off (his own fire held, his AI off: a still target). The leader's
// squad posture is AGGRESSIVE, so lying still he fires at the fighter. Then he is ordered to crawl 3 m four ways relative to the threat
// (toward, right, back, left). Leg 0 runs first as main did before the fix (Codex.Combat.ProneCrawlHoldFire 0, Codex.Anim.ProneCrawlLayer 0;
// the "before" number, logged only).
// Checks per leg: 0 rounds while crawling on the order; the body yaw within 15 deg of the threat (after a 0.75 s settle); the crawl layer drives
// the body (weight > 0.5) with the clip of the move's direction (F / R / B / L); the first round within 1.2 s of arrival. Logs the slide
// (the slowest of hands / knees / feet over the ground against the body's speed, median).
// Arg "shots" (rendered run): one frame per leg, no HUD, to Saved/Screenshots/ProneCrawl/crawl_<dir>.png.
// Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ProneCrawlSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LegCm = 300.f;
	constexpr float ThreatCm = 1400.f;
	constexpr float FacingLimitDeg = 15.f;
	constexpr float ResumeLimitSeconds = 1.2f;
	constexpr int32 LegCount = 5; // leg 0 = the old rule (before), legs 1..4 = toward / right / back / left
	static const TCHAR* LegNames[LegCount] = { TEXT("before"), TEXT("toward"), TEXT("right"), TEXT("back"), TEXT("left") };
	/** The clip-name tag the leg's dominant crawl clip must carry (relative to the body that faces the threat). */
	static const TCHAR* LegClip[LegCount] = { nullptr, TEXT("_F_"), TEXT("_R_"), TEXT("_B_"), TEXT("_L_") };
	static const TCHAR* Limbs[6] = { TEXT("hand_l"), TEXT("hand_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("ball_l"), TEXT("ball_r") };

	enum class EPhase : uint8 { Aim, Crawl, Resume };

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		FString Dir;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 Leg = 0;
		EPhase Phase = EPhase::Aim;
		double LastShot = 0.0;
		int32 LegShots = 0;
		float MaxFacingErr = 0.f;
		double FacingErrSum = 0.0;
		int32 FacingSamples = 0;
		float MaxWeight = 0.f;
		TMap<FString, int32> ClipVotes;
		float SpeedSum = 0.f;
		int32 SpeedSamples = 0;
		TArray<float> PlantSpeeds;
		FVector LastLimb[6];
		bool bLimbValid = false;
		bool bShotTaken = false;
		float ArriveTime = 0.f;
		// Totals for the report.
		int32 BeforeShots = 0;
		int32 AfterShots = 0;
		float WorstFacing = 0.f;
		float WorstResume = 0.f;
		TArray<float> SlideShares;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.ProneCrawlHoldFire"))->Set(1, ECVF_SetByConsole);
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Anim.ProneCrawlLayer"))->Set(1, ECVF_SetByConsole);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ProneCrawlSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	float FacingError(const AOperativeCharacter& Op, const AActor& Threat)
	{
		const float ToThreat = static_cast<float>((Threat.GetActorLocation() - Op.GetActorLocation()).Rotation().Yaw);
		return FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Op.GetActorRotation().Yaw) - ToThreat));
	}

	void FrameCamera(UWorld* World, FState& State, const AOperativeCharacter& Op)
	{
		const FVector Body = Op.GetActorLocation() - FVector(0.f, 0.f, 50.f);
		const FVector Forward = Op.GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Eye = Body + Forward * 260.f + Right * 260.f + FVector(0.f, 0.f, 180.f);
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

	/**
	 * The best-planted limb this frame: the slowest of the hands, knees (calf_*) and balls of the feet over the ground (world, horizontal) - a
	 * crawl pushes off at least one limb at a time, so its speed against the body's is the slide (0 % = a limb stands, 100 % = all of them
	 * glide with the body).
	 */
	void SampleLimbs(FState& State, const AOperativeCharacter& Op)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		float Slowest = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FVector Now = Mesh->GetBoneLocation(Limbs[Index]);
			Slowest = FMath::Min(Slowest, static_cast<float>(FVector::Dist2D(Now, State.LastLimb[Index])) / StepSeconds);
			State.LastLimb[Index] = Now;
		}
		if (State.bLimbValid)
		{
			State.PlantSpeeds.Add(Slowest);
		}
		State.bLimbValid = true;
	}

	FVector LegDirection(const FState& State, const AOperativeCharacter& Op, const AActor& Threat)
	{
		const FVector T = (Threat.GetActorLocation() - Op.GetActorLocation()).GetSafeNormal2D();
		const FVector R = FVector::CrossProduct(FVector::UpVector, T);
		switch (State.Leg)
		{
		case 0: return R;
		case 1: return T;
		case 2: return R;
		case 3: return -T;
		default: return -R;
		}
	}

	void StartLeg(FState& State, AOperativeCharacter& Op)
	{
		State.Phase = EPhase::Aim;
		State.StageTime = 0.f;
		State.LegShots = 0;
		State.MaxFacingErr = 0.f;
		State.FacingErrSum = 0.0;
		State.FacingSamples = 0;
		State.MaxWeight = 0.f;
		State.ClipVotes.Reset();
		State.SpeedSum = 0.f;
		State.SpeedSamples = 0;
		State.PlantSpeeds.Reset();
		State.bLimbValid = false;
		State.bShotTaken = false;
		State.LastShot = Op.GetLastShotTime();
		// Leg 0 = main before the fix: no fire hold, no crawl layer (the ABP's prone locomotion).
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.ProneCrawlHoldFire"))->Set(State.Leg == 0 ? 0 : 1, ECVF_SetByConsole);
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Anim.ProneCrawlLayer"))->Set(State.Leg == 0 ? 0 : 1, ECVF_SetByConsole);
	}

	void EndCrawlPhase(FState& State)
	{
		const AOperativeCharacter* Op = State.Op.Get();
		const TCHAR* Name = LegNames[State.Leg];
		FString TopClip;
		int32 TopVotes = 0;
		for (const TPair<FString, int32>& Vote : State.ClipVotes)
		{
			if (Vote.Value > TopVotes)
			{
				TopVotes = Vote.Value;
				TopClip = Vote.Key;
			}
		}
		const float Speed = State.SpeedSamples > 0 ? State.SpeedSum / State.SpeedSamples : 0.f;
		State.PlantSpeeds.Sort();
		const float Plant = State.PlantSpeeds.IsEmpty() ? -1.f : State.PlantSpeeds[State.PlantSpeeds.Num() / 2];
		const float Slide = Speed > 1.f && Plant >= 0.f ? Plant / Speed : -1.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %s - rounds while crawling %d, facing error max %.1f / mean %.1f deg (%d samples), crawl weight max %.2f, clip %s, speed %.0f cm/s, best-planted limb %.1f cm/s (%d samples, slide %.0f %%), fire holds %d"),
			Name, State.LegShots, State.MaxFacingErr, State.FacingSamples > 0 ? State.FacingErrSum / State.FacingSamples : 0.0, State.FacingSamples,
			State.MaxWeight, *TopClip, Speed, Plant, State.PlantSpeeds.Num(), Slide * 100.f, Op ? Op->CrawlFireHolds : -1);
		if (State.Leg == 0)
		{
			State.BeforeShots = State.LegShots;
			return;
		}
		State.AfterShots += State.LegShots;
		State.WorstFacing = FMath::Max(State.WorstFacing, State.MaxFacingErr);
		if (Slide >= 0.f)
		{
			State.SlideShares.Add(Slide);
		}
		Check(State, State.LegShots == 0, FString::Printf(TEXT("%s: no round while crawling on the order (%d)"), Name, State.LegShots));
		Check(State, State.FacingSamples > 0 && State.MaxFacingErr <= FacingLimitDeg,
			FString::Printf(TEXT("%s: the body faces the threat while crawling (max error %.1f deg, limit %.0f)"), Name, State.MaxFacingErr, FacingLimitDeg));
		Check(State, State.MaxWeight > 0.5f, FString::Printf(TEXT("%s: the prone crawl layer drives the body (weight %.2f)"), Name, State.MaxWeight));
		Check(State, TopClip.Contains(LegClip[State.Leg]), FString::Printf(TEXT("%s: the crawl clip of the move's direction plays (%s, wanted *%s*)"), Name, *TopClip, LegClip[State.Leg]));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d leg %d"), State.Stage, State.Leg));
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
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
			UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
			if (!Op || !Pmc)
			{
				Check(State, false, TEXT("a squad leader and the PMC subsystem"));
				return Finish(State, false);
			}
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World))
			{
				Encounters->CancelPending();
			}
			if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
			}
			// The leader alone in a clear 20 x 20 m patch (the mates parked out of it, frozen).
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Op->GetActorLocation(), Op->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-600.f, -600.f), FVector2D(ThreatCm + 300.f, 600.f)), Ignore);
			Check(State, Arena.bFound, TEXT("a clear patch for the crawl and the threat"));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			const FVector Start = SmokeUtils::FreeSpot(World, Arena.Origin, Op);
			Op->SetActorLocation(Start + FVector(0.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Op->SetActorRotation(Arena.Forward.Rotation());
			int32 Park = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Op)
				{
					Member->SetActorLocation(Start - Arena.Forward * (2500.f + 200.f * Park++), false, nullptr, ETeleportType::TeleportPhysics);
					Member->bTacticalCeaseFire = true;
					Member->SetActorTickEnabled(false);
				}
			}
			if (Op->HealthComponent)
			{
				Op->HealthComponent->SetMaxHealth(100000.f, false);
				Op->HealthComponent->Heal(100000.f);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Op->bTacticalCeaseFire = false;
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // -nullrhi: real bones
			Op->SetStance(EOperativeStance::Prone);
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, Start + Arena.Forward * ThreatCm, nullptr),
				(-Arena.Forward).Rotation(), 1, 47);
			AOperativeCharacter* Fighter = Spawned.IsEmpty() ? nullptr : Spawned[0];
			Check(State, Fighter != nullptr, TEXT("a PMC fighter 14 m off"));
			if (!Fighter)
			{
				return Finish(State, false);
			}
			Pmc->SetTeamCommanded(1, false);
			if (Fighter->HealthComponent)
			{
				Fighter->HealthComponent->SetMaxHealth(10000000.f, false);
				Fighter->HealthComponent->Heal(10000000.f);
			}
			Fighter->SetStance(EOperativeStance::Standing);
			Fighter->bTacticalCeaseFire = true;
			State.Op = Op;
			State.Fighter = Fighter;
			State.Stage = 1;
			State.StageTime = 0.f;
			State.Leg = 0;
			StartLeg(State, *Op);
			return true;
		}
		AOperativeCharacter* Op = State.Op.Get();
		AOperativeCharacter* Fighter = State.Fighter.Get();
		if (!Op || !Fighter)
		{
			Check(State, false, TEXT("the leader and the fighter stay"));
			return Finish(State, false);
		}
		if (Fighter->IsActorTickEnabled() && State.Time > 4.5f)
		{
			Fighter->SetActorTickEnabled(false); // a still target (no manoeuvre under the leader's fire)
		}
		const bool bShot = Op->GetLastShotTime() != State.LastShot;
		State.LastShot = Op->GetLastShotTime();
		UOperativeAnimInstance* Anim = AnimOf(Op);
		if (State.Phase == EPhase::Resume && FMath::Fmod(State.StageTime, 0.25f) < StepSeconds * 0.5f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: diag %s %s t=%.2f body err %.1f, barrel offset %.1f, residual %.1f, aim yaw %.1f, crawl w %.2f, aiming %d"),
				LegNames[State.Leg], State.Phase == EPhase::Aim ? TEXT("aim") : TEXT("resume"), State.StageTime, FacingError(*Op, *Fighter),
				Op->GetBarrelYawOffset(), Op->GetShotAimResidualDeg(Fighter->GetActorLocation()), Op->GetAppliedAimYaw(), Anim ? Anim->GetProneCrawlWeight() : -1.f,
				Anim && Anim->bIsAiming ? 1 : 0);
		}
		switch (State.Phase)
		{
		case EPhase::Aim:
		{
			// Lying still he fires (the posture's auto fire): wait for a round (or 8 s), then order the crawl.
			State.bShotTaken |= bShot;
			if ((!State.bShotTaken && State.StageTime < 8.f) || State.StageTime < 2.5f)
			{
				return true;
			}
			if (State.Leg == 1)
			{
				Check(State, State.bShotTaken, TEXT("lying prone and still he fires at the threat (the posture's auto fire)"));
			}
			const FVector Goal = SmokeUtils::ClearPoint(World, Op->GetActorLocation(), Op->GetActorLocation() + LegDirection(State, *Op, *Fighter) * LegCm);
			const EOperativeOrderResult Result = Op->OrderMoveTo(Goal, false);
			Check(State, Result == EOperativeOrderResult::Accepted && Op->GetStance() == EOperativeStance::Prone,
				FString::Printf(TEXT("%s: the crawl order is accepted, he stays prone"), LegNames[State.Leg]));
			State.Phase = EPhase::Crawl;
			State.StageTime = 0.f;
			State.LegShots = 0;
			return true;
		}
		case EPhase::Crawl:
		{
			if (Op->HasOwnMoveOrder() && State.StageTime < 20.f)
			{
				State.LegShots += bShot ? 1 : 0;
				const float Speed = static_cast<float>(Op->GetVelocity().Size2D());
				if (State.StageTime > 0.75f && Speed > 10.f)
				{
					const float Err = FacingError(*Op, *Fighter);
					if (Err > FacingLimitDeg)
					{
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s t=%.2f facing error %.1f deg (facing kind %d, speed %.0f)"), LegNames[State.Leg], State.StageTime, Err,
							static_cast<int32>(Op->GetCrawlFacingKind()), Speed);
					}
					State.MaxFacingErr = FMath::Max(State.MaxFacingErr, Err);
					State.FacingErrSum += Err;
					++State.FacingSamples;
					State.SpeedSum += Speed;
					++State.SpeedSamples;
					SampleLimbs(State, *Op);
					if (Anim)
					{
						State.MaxWeight = FMath::Max(State.MaxWeight, Anim->GetProneCrawlWeight());
						if (const UAnimSequenceBase* Clip = Anim->GetProneCrawlDominantClip())
						{
							State.ClipVotes.FindOrAdd(Clip->GetName() + TEXT("_"))++;
						}
					}
					if (State.bShots && State.FacingSamples == 20)
					{
						FrameCamera(World, State, *Op);
						FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s/crawl_%s.png"), *State.Dir, LegNames[State.Leg]), false, false);
					}
				}
				return true;
			}
			Check(State, !Op->HasOwnMoveOrder(), FString::Printf(TEXT("%s: the crawl ends (arrived in %.1f s)"), LegNames[State.Leg], State.StageTime));
			EndCrawlPhase(State);
			State.Phase = EPhase::Resume;
			State.ArriveTime = State.Time;
			State.StageTime = 0.f;
			return true;
		}
		case EPhase::Resume:
		{
			if (!bShot && State.StageTime < 4.f)
			{
				return true;
			}
			const float Resume = bShot ? State.StageTime : -1.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %s - first round %.2f s after the stop"), LegNames[State.Leg], Resume);
			if (State.Leg > 0)
			{
				State.WorstResume = FMath::Max(State.WorstResume, Resume < 0.f ? 99.f : Resume);
				Check(State, bShot && Resume <= ResumeLimitSeconds,
					FString::Printf(TEXT("%s: the fire resumes after the stop (%.2f s, limit %.1f)"), LegNames[State.Leg], Resume, ResumeLimitSeconds));
			}
			if (++State.Leg >= LegCount)
			{
				State.SlideShares.Sort();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: SUMMARY - rounds while crawling: before %d (old rule), after %d; worst facing error %.1f deg; worst fire resume %.2f s; median slide of the best-planted limb %.0f %%"),
					State.BeforeShots, State.AfterShots, State.WorstFacing, State.WorstResume,
					State.SlideShares.IsEmpty() ? -100.f : State.SlideShares[State.SlideShares.Num() / 2] * 100.f);
				return Finish(State, true);
			}
			StartLeg(State, *Op);
			return true;
		}
		}
		return true;
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ProneCrawlSmoke"),
		TEXT("Dev check (CrawlAndShoot_Bug_01, L_Gen_Depot_1): a prone leader ordered to crawl four ways from a threat holds his fire, faces the threat, plays the directional crawl clip, fires again on arrival; logs PASS/FAIL, then exits. Arg \"shots\": rendered frames to Saved/Screenshots/ProneCrawl."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			TWeakObjectPtr<UWorld> WeakWorld(World);
			TSharedRef<FState> State = MakeShared<FState>();
			State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shots"), ESearchCase::IgnoreCase); });
			State->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/ProneCrawl"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float) { return Step(WeakWorld, *State); }), StepSeconds);
		}));
}

#endif // !UE_BUILD_SHIPPING
