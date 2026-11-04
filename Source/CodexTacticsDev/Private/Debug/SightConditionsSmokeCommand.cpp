// Dev-only check of the sight range by the conditions (user request 2026-11-04 on L_Gen_Depot_1: "the cone stops in the middle of an open
// yard although everything is visible - sight at least 2x on a clear day, night cuts it drastically whatever the weather, snowfall / blizzard /
// fog reduce it, the same for the enemies"; docs/systems/sight-and-vision-cones.md "Sight by the conditions"):
//   Scripts/smoke.ps1 -Command CodexTactics.SightConditionsSmoke -Map /Game/Maps/Generated/L_Gen_Depot_1 -Extra "-LevelJson=L_Gen_Depot_1.json"
// From the squad's start on the Depot, along the longest clear ground line (a far target 62-110 m out and a near one 25-40 m out):
//  1. clear noon / blizzard noon / clear night / blizzard night (UEnvironmentSubsystem ForceWeather + SetHour, the clock frozen): the
//     conditions multiplier = 1 / 0.5 / 0.35 / 0.175; the leader's range = 120 m x it (x his height bonus); CanOperativeSee of both targets =
//     distance <= that range; his drawn cone's range = that range; an armed human (a Marksman) at each target: its patrol range = 45 m x2 x it,
//     it spots the leader = distance <= that range; a frost hound's = 15 m x it (a beast: no x2), its fight range 60 m x it;
//  2. the cone's cost: 20 rebuilds at 60 m and at 120 m (rays / traces / task ms per rebuild logged, "[SightConditions] cone perf");
//  3. a weather transition Clear -> Blizzard blends the multiplier (no step > 0.01 per frame);
//  4. the pressure still finds a concealed ambush spot on the Depot at clear noon (the longest sight).
// Arg Shot (a rendered run: -RenderOffscreen instead of -nullrhi): over-the-shoulder screenshots of the leader's cone at clear noon and at
// night -> Saved/Screenshots/SightConditions/clear_day.png, night.png.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/EnemyPerception.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace SightConditionsSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float Timeout = 240.f;
	/** A check of "seen == distance <= range" is skipped this close to the range edge (the mates stand a few metres round the leader). */
	constexpr float EdgeMarginCm = 400.f;

	enum class EStage : uint8 { Setup, Apply, Check, Perf, Transition, Pressure, ShotApply, ShotTake, Done };

	struct FCondition
	{
		const TCHAR* Name;
		EWeatherState Weather;
		float Hour;
		float Nominal;
	};

	const FCondition Conditions[] = {
		{ TEXT("clear noon"), EWeatherState::Clear, 12.f, 1.f },
		{ TEXT("blizzard noon"), EWeatherState::Blizzard, 12.f, 0.5f },
		{ TEXT("clear night"), EWeatherState::Clear, 0.f, 0.35f },
		{ TEXT("blizzard night"), EWeatherState::Blizzard, 0.f, 0.175f },
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotRequested = false;
		int32 Condition = 0;
		int32 ShotIndex = 0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		FVector Feet = FVector::ZeroVector;
		float Yaw = 0.f;
		/** The far / near ground targets on the leader's line (feet points) and their planar distances. */
		FVector Far = FVector::ZeroVector;
		FVector Near = FVector::ZeroVector;
		bool bNear = false;
		/** Seen / not seen outcomes met over the conditions (each side must show both). */
		int32 OperativeSeen = 0;
		int32 OperativeHidden = 0;
		int32 MarksmanSeen = 0;
		int32 MarksmanHidden = 0;
		/** Perf: build stats snapshots. */
		int32 PerfFrame = 0;
		FVisionConeBuildStats PerfStart;
		double PerfMs[2] = { 0.0, 0.0 };
		double PerfTraces[2] = { 0.0, 0.0 };
		double PerfRays[2] = { 0.0, 0.0 };
		/** Transition sampling. */
		float LastMultiplier = -1.f;
		float MaxStep = 0.f;
		float MinSeen = 2.f;
		float MaxSeen = -1.f;
		bool bSavedShowsSight = true;
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

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	FCollisionQueryParams NoPawns(UWorld* World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SightConditionsSmoke), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return Params;
	}

	/** The ground under a planar point at about Z (within 60 cm; no roof / crate in the way). */
	bool GroundNear(UWorld* World, const FVector& Point, float Z, FVector& Out)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, Z + 2500.f), FVector(Point.X, Point.Y, Z - 500.f), ECC_Visibility, NoPawns(World))
			|| FMath::Abs(Hit.ImpactPoint.Z - Z) > 60.f)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}

	void FocusCamera(UWorld* World, const FVector& Point, float Distance, float Yaw)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
			{
				Camera->SetCinematicLock(true);
				Camera->SetCinematicYaw(Yaw);
				Camera->SmoothFocusOnPosition(Point, 0.2f, Distance);
			}
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
		}
	}

	void SetEnvironment(UWorld* World, EWeatherState Weather, float Hour)
	{
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(Weather, true);
			Environment->SetHour(Hour);
		}
	}

	void HoldLeader(FState& State)
	{
		if (AOperativeCharacter* Leader = State.Leader.Get())
		{
			if (FVector::Dist2D(FeetOf(*Leader), State.Feet) > 15.f)
			{
				Leader->StopOperative();
				Leader->SetActorLocation(State.Feet + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			Leader->SetActorRotation(FRotator(0.f, State.Yaw, 0.f));
		}
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		UVisionConeComponent::SetPlayerSquadCones(false);
		if (AOperativeCharacter* Leader = State.Leader.Get())
		{
			Leader->bVisionConeShowsSightRange = State.bSavedShowsSight;
		}
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->SetClockScale(1.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("SightConditionsSmoke"));
		return false;
	}

	/** One condition's checks (the clock frozen at its weather / hour). */
	void CheckCondition(UWorld* World, FState& State, const FCondition& Condition)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const float Multiplier = Sight->GetSightConditionsMultiplier();
		const float Expected = SightRules::ConditionsSightMultiplier(EnemyPerception::GetSightConditions(), Condition.Weather, Condition.Weather, 0.f,
			Environment->GetSunElevation());
		Check(State, FMath::IsNearlyEqual(Multiplier, Condition.Nominal, 0.002f) && FMath::IsNearlyEqual(Multiplier, Expected, 0.002f),
			FString::Printf(TEXT("%s (%s, %02.0f:00, sun %.0f deg): conditions x%.3f (expected x%.3f)"), Condition.Name, *Environment->GetWeatherDisplayName(),
				Environment->GetHour(), Environment->GetSunElevation(), Multiplier, Condition.Nominal));
		// The leader's range: 120 m x height x conditions, and his drawn cone shows it.
		const float Height = Sight->GetSightRangeMultiplier(*Leader);
		const float Range = Sight->GetSightRangeCm(*Leader);
		Check(State, FMath::IsNearlyEqual(Range, UTacticalSightSubsystem::GetSquadBaseSightCm() * Height * Condition.Nominal, 2.f)
			&& FMath::IsNearlyEqual(UTacticalSightSubsystem::GetSquadBaseSightCm(), 12000.f, 1.f),
			FString::Printf(TEXT("... leader's sight range %.1f m (120 m x height %.2f x %.3f)"), Range / 100.f, Height, Condition.Nominal));
		const float ConeRange = Leader->VisionCone ? Leader->VisionCone->GetConeRangeCm() : -1.f;
		Check(State, FMath::IsNearlyEqual(Leader->GetVisionConeRange(), Range, 1.f) && FMath::IsNearlyEqual(ConeRange, Range, FMath::Max(2.f, 0.015f * Range)),
			FString::Printf(TEXT("... his cone shows the sight range: %.1f m drawn (the weapon reaches %.0f m)"), ConeRange / 100.f,
				Leader->CurrentWeapon ? Leader->CurrentWeapon->AttackRangeCm / 100.f : 0.f));
		// His eyes vs the targets: seen exactly within the range.
		for (int32 Index = 0; Index < (State.bNear ? 2 : 1); ++Index)
		{
			const FVector Target = Index == 0 ? State.Far : State.Near;
			const FVector Profile = Target + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
			const float Distance = FVector::Dist(UTacticalSightSubsystem::EyePoint(*Leader), Profile);
			Leader->SetActorRotation(FRotator(0.f, static_cast<float>((Target - State.Feet).GetSafeNormal2D().Rotation().Yaw), 0.f));
			const bool bSees = Sight->CanOperativeSee(*Leader, Profile);
			if (FMath::Abs(Distance - Range) > EdgeMarginCm)
			{
				const bool bWant = Distance <= Range;
				(bSees ? State.OperativeSeen : State.OperativeHidden) += 1;
				Check(State, bSees == bWant, FString::Printf(TEXT("... the leader %s a man %.1f m out (range %.1f m)"), bSees ? TEXT("SEES") : TEXT("does not see"),
					Distance / 100.f, Range / 100.f));
			}
		}
		Leader->SetActorRotation(FRotator(0.f, State.Yaw, 0.f));
		// An armed human (one model): 45 m x2 x the conditions; it spots the leader exactly within that.
		for (int32 Index = 0; Index < (State.bNear ? 2 : 1) && Waves; ++Index)
		{
			const FVector Target = Index == 0 ? State.Far : State.Near;
			const float YawToLeader = static_cast<float>((State.Feet - Target).GetSafeNormal2D().Rotation().Yaw);
			AEnemyCharacter* Marksman = Waves->SpawnLevelEnemy(EEnemyArchetype::Marksman, Target + FVector(0.f, 0.f, 110.f), FRotator(0.f, YawToLeader, 0.f));
			if (!Marksman)
			{
				Check(State, false, TEXT("... a Marksman spawned"));
				continue;
			}
			Marksman->SetActorLocationAndRotation(Target + FVector(0.f, 0.f, Marksman->GetSimpleCollisionHalfHeight() + 2.f), FRotator(0.f, YawToLeader, 0.f), false,
				nullptr, ETeleportType::TeleportPhysics);
			const float RowCm = EnemyPerception::Get(EEnemyArchetype::Marksman).SightRangeCm;
			const float PatrolCm = Marksman->GetPerception().SightRangeCm * Marksman->GetSightRangeScale();
			const float WantCm = RowCm * UTacticalSightSubsystem::GetHumanEnemyRangeMultiplier() * Sight->GetSightRangeMultiplier(*Marksman) * Condition.Nominal;
			if (Index == 0)
			{
				Check(State, Marksman->UsesHumanSight() && FMath::IsNearlyEqual(PatrolCm, WantCm, 2.f) && FMath::IsNearlyEqual(UTacticalSightSubsystem::GetHumanEnemyRangeMultiplier(), 2.f),
					FString::Printf(TEXT("... a Marksman's patrol sight %.1f m (its row %.0f m x2 x %.3f)"), PatrolCm / 100.f, RowCm / 100.f, Condition.Nominal));
				Check(State, FMath::IsNearlyEqual(Sight->GetSightRangeCm(*Marksman), Sight->GetSightRangeCm(*Leader) / FMath::Max(Height, 0.01f)
					* Sight->GetSightRangeMultiplier(*Marksman), 2.f), FString::Printf(TEXT("... its fight sight = the squad's model: %.1f m"), Sight->GetSightRangeCm(*Marksman) / 100.f));
			}
			float SeenDistance = 0.f;
			float SeenRange = 0.f;
			const AActor* Seen = Marksman->FindVisibleOperativeNow(SeenDistance, SeenRange);
			const float Distance = static_cast<float>(FVector::Dist(Marksman->GetActorLocation(), Leader->GetActorLocation()));
			const bool bClear = Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Marksman), UTacticalSightSubsystem::ProfilePoint(*Leader));
			if (bClear && FMath::Abs(Distance - PatrolCm) > EdgeMarginCm)
			{
				const bool bWant = Distance <= PatrolCm;
				(Seen ? State.MarksmanSeen : State.MarksmanHidden) += 1;
				Check(State, (Seen != nullptr) == bWant, FString::Printf(TEXT("... the Marksman %.1f m out %s the squad (its range %.1f m)"), Distance / 100.f,
					Seen ? TEXT("SPOTS") : TEXT("does not spot"), PatrolCm / 100.f));
			}
			Marksman->Destroy();
		}
		// A beast: its own row x the conditions (no x2), the fight's 60 m x the conditions.
		if (Waves)
		{
			AEnemyCharacter* Hound = Waves->SpawnLevelEnemy(EEnemyArchetype::FrostHound, State.Near + FVector(0.f, 0.f, 80.f), FRotator::ZeroRotator);
			if (Hound)
			{
				const float RowCm = EnemyPerception::Get(EEnemyArchetype::FrostHound).SightRangeCm;
				const float PatrolCm = Hound->GetPerception().SightRangeCm * Hound->GetSightRangeScale();
				Check(State, !Hound->UsesHumanSight() && FMath::IsNearlyEqual(PatrolCm, RowCm * Condition.Nominal, 2.f)
					&& FMath::IsNearlyEqual(Sight->GetSightRangeCm(*Hound), UTacticalSightSubsystem::MaxSightCm * Condition.Nominal, 2.f),
					FString::Printf(TEXT("... a frost hound (a beast): patrol sight %.1f m (its row %.0f m x %.3f, no x2), fight sight %.1f m"), PatrolCm / 100.f,
						RowCm / 100.f, Condition.Nominal, Sight->GetSightRangeCm(*Hound) / 100.f));
				Hound->Destroy();
			}
		}
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(World, State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		if (!Squad || !Sight || !Squad->GetLeader() || !Environment || !Environment->IsInitialised())
		{
			Check(State, false, TEXT("squad with a leader, the sight and an initialised environment subsystem"));
			return Finish(World, State, false);
		}
		AOperativeCharacter* Leader = State.Leader.Get();
		if (State.Stage != EStage::Setup && !Leader)
		{
			Check(State, false, TEXT("the leader still there"));
			return Finish(World, State, false);
		}
		if (State.Stage != EStage::Setup)
		{
			HoldLeader(State);
		}

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UPmcEncounterSubsystem* Pmc = UPmcEncounterSubsystem::Get(World))
			{
				Pmc->CancelPending();
			}
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(1, ECVF_SetByConsole);
			}
			Leader = Squad->GetLeader();
			State.Leader = Leader;
			Squad->EnterSoloMode();
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			State.Feet = FeetOf(*Leader);
			State.bSavedShowsSight = Leader->bVisionConeShowsSightRange;
			Leader->bVisionConeShowsSightRange = true;
			// The longest clear ground line from his standing eye: a far target 62-110 m out, a near one 25-40 m out on the same bearing family.
			const FVector Eye = State.Feet + FVector(0.f, 0.f, SightRules::EyeHeight(EOperativeStance::Standing));
			float BestFar = 0.f;
			for (float Yaw = 0.f; Yaw < 360.f; Yaw += 5.f)
			{
				const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
				for (float Distance = 11000.f; Distance >= 6200.f && Distance > BestFar; Distance -= 300.f)
				{
					FVector Ground;
					if (GroundNear(World, State.Feet + Dir * Distance, static_cast<float>(State.Feet.Z), Ground)
						&& Sight->HasClearSight(Eye, Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing))))
					{
						BestFar = Distance;
						State.Far = Ground;
						State.Yaw = Yaw;
						break;
					}
				}
			}
			Check(State, BestFar > 0.f, FString::Printf(TEXT("a clear ground line from the leader at (%.0f, %.0f, %.0f): a target %.1f m out at yaw %.0f"),
				State.Feet.X, State.Feet.Y, State.Feet.Z, BestFar / 100.f, State.Yaw));
			if (BestFar <= 0.f)
			{
				return Finish(World, State, false);
			}
			const FVector FarDir = FRotator(0.f, State.Yaw, 0.f).Vector();
			for (float Distance = 3800.f; Distance >= 2500.f && !State.bNear; Distance -= 200.f)
			{
				FVector Ground;
				if (GroundNear(World, State.Feet + FarDir * Distance, static_cast<float>(State.Feet.Z), Ground)
					&& Sight->HasClearSight(Eye, Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing))))
				{
					State.Near = Ground;
					State.bNear = true;
				}
			}
			Check(State, State.bNear, FString::Printf(TEXT("a near target on that line %.1f m out"), FVector::Dist2D(State.Near, State.Feet) / 100.f));
			UVisionConeComponent::SetPlayerSquadCones(true);
			State.Condition = 0;
			Enter(State, EStage::Apply);
			return true;
		}

		case EStage::Apply:
		{
			const FCondition& Condition = Conditions[State.Condition];
			SetEnvironment(World, Condition.Weather, Condition.Hour);
			Enter(State, EStage::Check);
			return true;
		}

		case EStage::Check:
		{
			if (State.StageTime < 0.5f)
			{
				return true; // the cone rebuilds with the new range (the batch uploads a frame later)
			}
			CheckCondition(World, State, Conditions[State.Condition]);
			if (++State.Condition < UE_ARRAY_COUNT(Conditions))
			{
				Enter(State, EStage::Apply);
				return true;
			}
			Check(State, State.OperativeSeen > 0 && State.OperativeHidden > 0, FString::Printf(TEXT("the leader both saw (%d) and did not see (%d) the targets over the conditions"),
				State.OperativeSeen, State.OperativeHidden));
			Check(State, State.MarksmanSeen > 0 && State.MarksmanHidden > 0, FString::Printf(TEXT("the Marksman both spotted (%d) and did not spot (%d) the squad over the conditions"),
				State.MarksmanSeen, State.MarksmanHidden));
			SetEnvironment(World, EWeatherState::Clear, 12.f);
			State.PerfFrame = 0;
			Enter(State, EStage::Perf);
			return true;
		}

		case EStage::Perf:
		{
			// 20 rebuilds of the leader's cone at 60 m (the old sight) and 20 at 120 m (clear noon): RequestRebuild each frame.
			const int32 Pass = State.PerfFrame / 22;
			const int32 Frame = State.PerfFrame % 22;
			if (Pass >= 2)
			{
				const double Ratio = State.PerfMs[0] > 0.0 ? State.PerfMs[1] / State.PerfMs[0] : 0.0;
				UE_LOG(LogCodexTactics, Display, TEXT("[SightConditions] cone perf: 60 m %.0f rays / %.0f traces / %.3f ms task per rebuild; 120 m %.0f rays / %.0f traces / %.3f ms (x%.2f)"),
					State.PerfRays[0], State.PerfTraces[0], State.PerfMs[0], State.PerfRays[1], State.PerfTraces[1], State.PerfMs[1], Ratio);
				Check(State, State.PerfMs[1] > 0.0 && State.PerfMs[1] < 6.0, FString::Printf(TEXT("a 120 m cone rebuild costs %.3f ms of task-thread work (< 6 ms; 60 m: %.3f ms)"),
					State.PerfMs[1], State.PerfMs[0]));
				Leader->bVisionConeShowsSightRange = true;
				State.LastMultiplier = -1.f;
				Environment->SetClockScale(1.f);
				Environment->ForceWeather(EWeatherState::Blizzard, false);
				Enter(State, EStage::Transition);
				return true;
			}
			if (Frame == 0)
			{
				Leader->bVisionConeShowsSightRange = Pass == 1;
				Leader->bVisionConeRangeFromWeapon = false;
				Leader->VisionConeRangeCm = UTacticalSightSubsystem::MaxSightCm;
			}
			if (Frame == 2)
			{
				State.PerfStart = UVisionConeComponent::GetBuildStats();
			}
			if (Frame >= 2)
			{
				Leader->VisionCone->RequestRebuild();
			}
			if (Frame == 21)
			{
				const FVisionConeBuildStats& Now = UVisionConeComponent::GetBuildStats();
				const double Builds = FMath::Max<double>(1.0, static_cast<double>(Now.Builds - State.PerfStart.Builds));
				State.PerfMs[Pass] = (Now.TaskSeconds - State.PerfStart.TaskSeconds) * 1000.0 / Builds;
				State.PerfTraces[Pass] = static_cast<double>(Now.Traces - State.PerfStart.Traces) / Builds;
				State.PerfRays[Pass] = static_cast<double>(Now.Rays - State.PerfStart.Rays) / Builds;
			}
			++State.PerfFrame;
			return true;
		}

		case EStage::Transition:
		{
			const float Multiplier = Sight->GetSightConditionsMultiplier();
			if (State.LastMultiplier >= 0.f)
			{
				State.MaxStep = FMath::Max(State.MaxStep, FMath::Abs(Multiplier - State.LastMultiplier));
			}
			State.LastMultiplier = Multiplier;
			State.MinSeen = FMath::Min(State.MinSeen, Multiplier);
			State.MaxSeen = FMath::Max(State.MaxSeen, Multiplier);
			if (State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, State.MaxStep <= 0.01f && State.MinSeen < State.MaxSeen && State.MinSeen >= 0.5f - 0.002f && State.MaxSeen <= 1.002f,
				FString::Printf(TEXT("Clear -> Blizzard blends the sight (x%.3f .. x%.3f over 4 s, blend %.3f, the largest step %.4f per frame)"), State.MaxSeen,
					State.MinSeen, Environment->GetTransitionBlend(), State.MaxStep));
			SetEnvironment(World, EWeatherState::Clear, 12.f);
			Enter(State, EStage::Pressure);
			return true;
		}

		case EStage::Pressure:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			// The longest sight (clear noon): the pressure still finds a hiding spot nobody sees.
			UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
			if (Pressure)
			{
				const int32 Before = Pressure->GetSpawnRecords().Num();
				const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerEvent(EPressureSpawnKind::Ambush, 1, true);
				const TArray<FPressureSpawnRecord>& Records = Pressure->GetSpawnRecords();
				const bool bConcealedAmbush = Records.Num() > Before && Records.Last().Kind == EPressureSpawnKind::Ambush && Records.Last().bConcealed;
				Check(State, Spawned.Num() > 0 && bConcealedAmbush, FString::Printf(TEXT("clear noon (120 m sight): the pressure still finds a concealed ambush spot (%d spawned, %s, %.1f m from the squad)"),
					Spawned.Num(), Records.Num() > Before ? (Records.Last().bConcealed ? TEXT("concealed") : TEXT("SEEN")) : TEXT("no record"),
					Records.Num() > Before ? Records.Last().SquadDistanceCm / 100.f : 0.f));
				Pressure->DespawnAll();
			}
			if (!State.bShot)
			{
				return Finish(World, State, true);
			}
			State.ShotIndex = 0;
			Enter(State, EStage::ShotApply);
			return true;
		}

		case EStage::ShotApply:
		{
			if (State.StageTime < 0.05f)
			{
				SetEnvironment(World, EWeatherState::Clear, State.ShotIndex == 0 ? 12.f : 0.5f);
				// A state set snaps the visuals (sun / moon / sky) instead of the in-play smoothing.
				FEnvironmentState Snap = Environment->GetState();
				Environment->SetState(Snap);
				Leader->bShowVisionCone = true;
			}
			// Behind him, looking down his line into the yard (the cinematic yaw convention of VisionHeightSmoke: facing - 90).
			const FVector Facing = FRotator(0.f, State.Yaw, 0.f).Vector();
			FocusCamera(World, State.Feet + Facing * (State.ShotIndex == 0 ? 3000.f : 1500.f), State.ShotIndex == 0 ? 7500.f : 4500.f, State.Yaw - 90.f);
			if (State.StageTime < 5.f)
			{
				return true;
			}
			State.bShotRequested = false;
			Enter(State, EStage::ShotTake);
			return true;
		}

		case EStage::ShotTake:
		{
			if (!State.bShotRequested)
			{
				State.bShotRequested = true;
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("SightConditions") / (State.ShotIndex == 0 ? TEXT("clear_day.png") : TEXT("night.png"));
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				UE_LOG(LogCodexTactics, Display, TEXT("[SightConditions] screenshot requested: %s (cone %.1f m)"), *Path, Leader->VisionCone->GetConeRangeCm() / 100.f);
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (++State.ShotIndex < 2)
			{
				Enter(State, EStage::ShotApply);
				return true;
			}
			Leader->bShowVisionCone = false;
			return Finish(World, State, true);
		}

		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Num() > 0 && Args[0].Equals(TEXT("Shot"), ESearchCase::IgnoreCase);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SightConditionsSmoke"),
		TEXT("Dev check (user request 2026-11-04, L_Gen_Depot_1): the sight range by the weather and the time of day - clear noon 120 m, blizzard x0.5, night x0.35, ")
		TEXT("blizzard night x0.175 - for the squad (his cone shows it), an armed human (45 m x2) and a beast (its row), the transition blends, the cone's cost, ")
		TEXT("the pressure still hides. Arg Shot saves Saved/Screenshots/SightConditions/*.png. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
