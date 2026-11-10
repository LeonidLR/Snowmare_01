// Dev-only headless check of the INJURED LOCOMOTION in the game designer's own situation (bug 2026-11-10, fix/injured-layer-visible: PIE on
// the Depot, the Commander badly wounded at 66 / 140 HP walked on without a visible limp; InjuredLocomotionRules, docs/systems/animation-setup.md
// "Injured locomotion layer") on L_Gen_Depot_1 with a pinned level JSON (SmokeFixtures/injured_depot_smoke.json - never the designer's file):
//   Scripts/smoke_parallel.ps1 -Commands InjuredLocomotionDepotSmoke
// The Commander leads the whole squad (the mates follow in the formation, their fire held). Every leg is a squad move order 6 m sideways
// (ACodexTacticsPlayerController::OrderGroupMove - the player's ground click):
//  0. full HP, M16 / the RPG-7: the references (the torso lean per weapon) - the injured layer stays off;
//  1. 45 % HP, M16, no threat: the injured walk drives the legs;
//  2. 45 % HP, the RPG-7 drawn (the launcher's full-body set, its "legs_abp" walk), no threat;
//  3. 45 % HP, M16, a PMC fighter 14 m ahead (immortal, his fire held): the Commander aims at him walking;
//  4. 45 % HP, the RPG-7 drawn, the same fighter: aiming the tube walking.
// Checks per wounded leg: the layer's steady weight mean > 0.9, an injured walk loop dominates, the limp cue is on ("limp" block); not aiming,
// the torso (pelvis -> neck_01) leans >= 7 deg further forward than the healthy walk and sways >= 2 deg wider; in the combat legs the
// Commander aims most of the walk. Per leg it logs the gates that held the layer off
// (UOperativeAnimInstance::DescribeInjuredLayerGates) - the bug's diagnosis.
// Arg "shots" (rendered run): one frame per leg from the GAME camera (the player's view, its own distance), HUD on, to
// Saved/Screenshots/InjuredLocomotion/depot_<leg>.png. Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/InjuredLocomotionRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace InjuredLocomotionDepotSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float WoundedShare = 0.45f;
	constexpr float LegCm = 600.f;
	constexpr float ThreatCm = 1400.f;

	struct FLeg
	{
		const TCHAR* Name;
		bool bWounded;
		bool bRpg;
		bool bThreat;
	};
	static const FLeg Legs[] = {
		{ TEXT("healthy_m16"), false, false, false },
		{ TEXT("healthy_rpg"), false, true, false },
		{ TEXT("wounded_m16"), true, false, false },
		{ TEXT("wounded_rpg"), true, true, false },
		{ TEXT("wounded_m16_aiming"), true, false, true },
		{ TEXT("wounded_rpg_aiming"), true, true, true },
	};
	constexpr int32 LegCount = UE_ARRAY_COUNT(Legs);

	struct FSamples
	{
		bool bActive = false;
		int32 SteadyFrames = 0;
		int32 AimFrames = 0;
		double WeightSum = 0.0;
		float MaxWeight = 0.f;
		double SpeedSum = 0.0;
		double LimpSum = 0.f;
		/** The torso's lean (pelvis -> neck_01) from the vertical, deg: forward (mean) and sideways (min / max over the walk). */
		double LeanSum = 0.0;
		float SideMin = 0.f;
		float SideMax = 0.f;
		TMap<FString, int32> ClipVotes;
		TMap<FString, int32> ClosedGates;
		bool bShotTaken = false;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0; // 0 setup, 1 legs
		int32 Leg = 0;
		bool bMoveStarted = false;
		bool bShots = false;
		int32 Failures = 0;
		FString Dir;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		FSamples Samples;
		/** The healthy legs' mean forward lean and sway per weapon ([0] M16, [1] RPG-7: the references for the cue checks). */
		float HealthyLean[2] = { 0.f, 0.f };
		float HealthySway[2] = { 0.f, 0.f };
		TArray<FString> Summary;
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
		for (const FString& Line : State.Summary)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("InjuredLocomotionDepotSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
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

	FString Histogram(const TMap<FString, int32>& Votes)
	{
		TArray<FString> Parts;
		for (const TPair<FString, int32>& Vote : Votes)
		{
			Parts.Add(FString::Printf(TEXT("%s x%d"), *Vote.Key, Vote.Value));
		}
		return Parts.IsEmpty() ? FString(TEXT("-")) : FString::Join(Parts, TEXT("; "));
	}

	void SampleFrame(FState& State)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		FSamples& S = State.Samples;
		if (!S.bActive || !Op || !Anim)
		{
			return;
		}
		const float Speed = static_cast<float>(Op->GetVelocity().Size2D());
		const float MaxSpeed = Op->GetCharacterMovement() ? Op->GetCharacterMovement()->MaxWalkSpeed : Speed;
		if (Speed <= 20.f || Speed < MaxSpeed * 0.8f)
		{
			return;
		}
		++S.SteadyFrames;
		S.SpeedSum += Speed;
		S.AimFrames += Anim->bIsAiming ? 1 : 0;
		const float Weight = Anim->GetInjuredWeight();
		S.WeightSum += Weight;
		S.MaxWeight = FMath::Max(S.MaxWeight, Weight);
		S.LimpSum += Anim->GetInjuredLimpAmount();
		const FVector Torso = Op->GetMesh()->GetBoneLocation(TEXT("neck_01")) - Op->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		const FVector Fwd = Op->GetActorForwardVector().GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
		const float Up = static_cast<float>(Torso.Z);
		const float Lean = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::DotProduct(Torso, Fwd)), Up));
		const float SideLean = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::DotProduct(Torso, Side)), Up));
		S.LeanSum += Lean;
		S.SideMin = S.SteadyFrames == 1 ? SideLean : FMath::Min(S.SideMin, SideLean);
		S.SideMax = S.SteadyFrames == 1 ? SideLean : FMath::Max(S.SideMax, SideLean);
		const UAnimSequenceBase* Clip = Anim->GetInjuredDominantClip();
		S.ClipVotes.FindOrAdd(Clip ? Clip->GetName() : FString(TEXT("none")))++;
		if (Weight < 0.9f)
		{
			FString Gates = Anim->DescribeInjuredLayerGates();
			if (Gates.Contains(TEXT("full-body clip")))
			{
				FString FullClip;
				float MontageWeight = 0.f;
				float SlotWeight = 0.f;
				Anim->GetCoverPlayback(FullClip, MontageWeight, SlotWeight);
				Gates += FString::Printf(TEXT(" [%s]"), *FullClip);
			}
			S.ClosedGates.FindOrAdd(FString::Printf(TEXT("%s (gait %d)"), *Gates, static_cast<int32>(Anim->GetInjuredGait())))++;
		}
	}

	/** The squad back at the origin: the Commander there facing forward, the mates in a short column behind him; everybody still. */
	void PlaceSquad(USquadSubsystem& Squad, AOperativeCharacter& Op, const FState& State)
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
		const FVector Start = State.Origin - Right * (LegCm * 0.5f);
		Op.SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		Op.SetActorRotation(State.Forward.Rotation());
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != &Op)
			{
				++Index;
				Member->SetActorLocation(SmokeUtils::FreeSpot(Op.GetWorld(), Start - State.Forward * (180.f * Index), Member), false, nullptr, ETeleportType::TeleportPhysics);
				Member->SetActorRotation(State.Forward.Rotation());
			}
		}
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member->GetCharacterMovement())
			{
				Member->GetCharacterMovement()->StopMovementImmediately();
			}
		}
	}

	void Equip(AOperativeCharacter& Op, bool bRpg)
	{
		const FString Want = bRpg ? TEXT("rpg7") : TEXT("m16");
		if (Op.CurrentWeapon && Op.CurrentWeapon->WeaponId == Want)
		{
			return;
		}
		if (!Op.SwitchToWeaponById(Want) && bRpg)
		{
			Op.AddArsenalWeapon(LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_rpg7.DA_Weapon_rpg7")));
			Op.SwitchToWeaponById(Want);
		}
	}

	void StartLeg(FState& State, USquadSubsystem& Squad, AOperativeCharacter& Op)
	{
		const FLeg& Leg = Legs[State.Leg];
		State.StageTime = 0.f;
		State.bMoveStarted = false;
		State.Samples = FSamples();
		if (Op.HealthComponent)
		{
			const float Max = Op.HealthComponent->GetMaxHealth();
			Op.HealthComponent->RestoreHealth(Max * (Leg.bWounded ? WoundedShare : 1.f), Max);
		}
		Op.ColdLevel = 0.f;
		Op.bTacticalCeaseFire = !Leg.bThreat;
		Equip(Op, Leg.bRpg);
		if (AOperativeCharacter* Fighter = State.Fighter.Get())
		{
			const FVector Spot = Leg.bThreat ? SmokeUtils::FreeSpot(Op.GetWorld(), State.Origin + State.Forward * ThreatCm, Fighter)
				: State.Origin - State.Forward * 6000.f;
			Fighter->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
			Fighter->SetActorRotation((-State.Forward).Rotation());
			Fighter->SetActorHiddenInGame(!Leg.bThreat);
		}
		PlaceSquad(Squad, Op, State);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 400.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout in leg %d"), State.Leg));
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
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
			UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
			if (!Op || !AnimOf(Op) || !Pmc || !PC || !Op->HealthComponent)
			{
				Check(State, false, TEXT("the squad leader with the operative anim instance, the PMC subsystem and the player controller"));
				return Finish(State, false);
			}
			Check(State, Op->SquadRole == EOperativeRole::Commander, FString::Printf(TEXT("the leader is the Commander (%s, body %s)"), *Op->DisplayName.ToString(),
				Op->GetMesh()->GetSkeletalMeshAsset() ? *Op->GetMesh()->GetSkeletalMeshAsset()->GetName() : TEXT("-")));
			Check(State, AnimOf(Op)->HasInjuredSet(), TEXT("the injured set (the retargeted copies) is loaded for his body"));
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
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Op->GetActorLocation(), Op->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-700.f, -500.f), FVector2D(ThreatCm + 300.f, 500.f)), Ignore);
			Check(State, Arena.bFound, TEXT("a clear patch for the squad's walk and the threat"));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.Origin = SmokeUtils::FreeSpot(World, Arena.Origin, Op);
			State.Forward = Arena.Forward;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Member->bTacticalCeaseFire = true;
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // -nullrhi: real bones
				}
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, State.Origin + State.Forward * ThreatCm, nullptr),
				(-State.Forward).Rotation(), 1, 47);
			AOperativeCharacter* Fighter = Spawned.IsEmpty() ? nullptr : Spawned[0];
			Check(State, Fighter != nullptr, TEXT("a PMC fighter for the combat legs"));
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
			State.Leg = 0;
			StartLeg(State, *Squad, *Op);
			return true;
		}
		AOperativeCharacter* Op = State.Op.Get();
		UOperativeAnimInstance* Anim = AnimOf(Op);
		if (!Op || !Anim || !Squad || !PC)
		{
			Check(State, false, TEXT("the Commander stays"));
			return Finish(State, false);
		}
		if (AOperativeCharacter* Fighter = State.Fighter.Get(); Fighter && Fighter->IsActorTickEnabled() && State.Time > 4.5f)
		{
			Fighter->SetActorTickEnabled(false); // a still target
		}
		const FLeg& Leg = Legs[State.Leg];
		FSamples& S = State.Samples;
		if (!State.bMoveStarted)
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
			State.bMoveStarted = true;
			S.bActive = true;
			PC->OrderGroupMove(SmokeUtils::ClearPoint(World, Op->GetActorLocation(), Op->GetActorLocation() + Right * LegCm), false, false);
			return true;
		}
		if (State.bShots && !S.bShotTaken && S.SteadyFrames >= 25)
		{
			S.bShotTaken = true;
			const FString File = FString::Printf(TEXT("%s/depot_%s.png"), *State.Dir, Leg.Name);
			FScreenshotRequest::RequestScreenshot(File, true, false); // the player's view: the game camera, the HUD on
			const APlayerCameraManager* Camera = PC->PlayerCameraManager;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: frame %s (game camera %.0f cm from him, pitch %.0f)"), *File,
				Camera ? FVector::Dist(Camera->GetCameraLocation(), Op->GetActorLocation()) : -1.f, Camera ? Camera->GetCameraRotation().Pitch : 0.f);
		}
		const bool bArrived = State.StageTime > 1.5f && Op->GetVelocity().Size2D() < 5.f && !Op->HasOwnMoveOrder();
		if (!bArrived && State.StageTime < 14.f)
		{
			return true;
		}
		S.bActive = false;
		const int32 N = FMath::Max(1, S.SteadyFrames);
		const float Mean = static_cast<float>(S.WeightSum / N);
		const float Limp = static_cast<float>(S.LimpSum / N);
		const float Aim = static_cast<float>(S.AimFrames) / N;
		const FString Top = TopVote(S.ClipVotes);
		const float Lean = static_cast<float>(S.LeanSum / N);
		const float Sway = (S.SideMax - S.SideMin) * 0.5f;
		const int32 Weapon = Leg.bRpg ? 1 : 0;
		if (!Leg.bWounded)
		{
			State.HealthyLean[Weapon] = Lean;
			State.HealthySway[Weapon] = Sway;
		}
		const FString Line = FString::Printf(TEXT("%s: steady %d frames at %.0f cm/s, injured weight mean %.2f max %.2f, clip %s, limp cue %.2f, torso lean fwd %.1f deg (healthy %.1f), side sway +-%.1f deg (healthy +-%.1f), aiming %.0f %%, weapon %s, gates closed: %s"),
			Leg.Name, S.SteadyFrames, S.SpeedSum / N, Mean, S.MaxWeight, *Top, Limp, Lean, State.HealthyLean[Weapon], Sway, State.HealthySway[Weapon], Aim * 100.f,
			Op->CurrentWeapon ? *Op->CurrentWeapon->WeaponId : TEXT("-"), *Histogram(S.ClosedGates));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s"), *Line);
		State.Summary.Add(Line);
		Check(State, S.SteadyFrames > 10, FString::Printf(TEXT("%s: the Commander walked the squad order (%d steady frames)"), Leg.Name, S.SteadyFrames));
		Check(State, Op->CurrentWeapon && Op->CurrentWeapon->WeaponId == (Leg.bRpg ? TEXT("rpg7") : TEXT("m16")),
			FString::Printf(TEXT("%s: the weapon in his hands (%s)"), Leg.Name, Op->CurrentWeapon ? *Op->CurrentWeapon->WeaponId : TEXT("-")));
		if (Leg.bWounded)
		{
			Check(State, Mean > 0.9f && Top.StartsWith(TEXT("Walk_Injured")), FString::Printf(TEXT("%s: the injured walk drives the legs (weight mean %.2f, clip %s)"), Leg.Name, Mean, *Top));
			Check(State, Limp > 0.5f, FString::Printf(TEXT("%s: the limp cue is on (%.2f)"), Leg.Name, Limp));
			if (!Leg.bThreat)
			{
				// Not aiming: the full hunch (the data's hunch_deg 14) - at least 7 deg more forward lean than the healthy walk, a wider sway.
				Check(State, Lean - State.HealthyLean[Weapon] >= 7.f, FString::Printf(TEXT("%s: the torso hunches (lean %.1f vs healthy %.1f deg, same weapon)"), Leg.Name, Lean,
					State.HealthyLean[Weapon]));
				Check(State, Sway - State.HealthySway[Weapon] >= 2.f, FString::Printf(TEXT("%s: the torso sways with the limp (+-%.1f vs healthy +-%.1f deg, same weapon)"), Leg.Name, Sway,
					State.HealthySway[Weapon]));
			}
		}
		else
		{
			Check(State, S.MaxWeight < 0.01f, FString::Printf(TEXT("%s: full HP - the injured layer stays off (weight max %.3f)"), Leg.Name, S.MaxWeight));
		}
		if (Leg.bThreat)
		{
			Check(State, Aim > 0.5f, FString::Printf(TEXT("%s: he aims at the fighter walking (%.0f %% of the steady frames)"), Leg.Name, Aim * 100.f));
		}
		if (++State.Leg >= LegCount)
		{
			return Finish(State, true);
		}
		StartLeg(State, *Squad, *Op);
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
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleFrame(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float) { return Step(WeakWorld, State); }), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.InjuredLocomotionDepotSmoke"),
		TEXT("Dev check (the designer's bug 2026-11-10, L_Gen_Depot_1 + SmokeFixtures/injured_depot_smoke.json): the Commander at 45 % HP walks squad move orders with the M16 / the RPG-7, with and without aiming at a PMC fighter - the injured walk drives his legs, the limp cue is on; logs the gates that held the layer off. Arg \"shots\": game-camera frames to Saved/Screenshots/InjuredLocomotion/depot_<leg>.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
