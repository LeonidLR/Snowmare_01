// Dev-only console command: the pistol end to end (the weapons week, "the whole pistol", 2026-11-08).
//   Scripts/smoke_parallel.ps1 -Commands PistolSmoke
// 1. loadout: every operative carries the pistol (12 + 24); the Commander M16 / pistol / grenade; the pistol's handling is "pistol".
// 2. the switch to the pistol: the optimized Pistol 9mm + its magazine on the weapon socket, the equip clip, the pistol layer drives the body with
//    the skeleton's retargeted copies.
// 3. damage by distance, forced hits: 5 / 10 m full, 18 m x0.787, 24.5 m x0.613 (full to 10 m, x0.6 at the 25 m reach); every shot plays the
//    (additive) fire clip.
// 4. the reload from empty: the pistol's reload clip on the upper body, 12 rounds in after ~1.3 s.
// 5. the sidearm switch: the M16 runs dry with the hound 6 m away -> the pistol (no reload); the hound gone for 3 s -> back to the M16, which reloads.
// 6. a PMC fighter given the pistol runs the same layer on the source clips (his Manny-hierarchy skeleton).

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
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

namespace PistolSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		TWeakObjectPtr<AEnemyCharacter> Target;
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		int32 EquipBefore = 0;
		float MaxReloadWeight = 0.f;
		float ReloadDone = -1.f;
		int32 DrawsBefore = 0;
		// Upper-body-only probe (designer 2026-11-09).
		float MoveMaxLower = 0.f;
		float MoveMaxUpper = 0.f;
		float MoveMaxSpeed = 0.f;
		int32 MoveSamples = 0;
		bool bSprintLeg = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PistolSmoke"));
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

	FString ClipPath(const AOperativeCharacter* Operative)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		const UAnimSequenceBase* Clip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
		return Clip ? Clip->GetPathName() : FString(TEXT("none"));
	}

	bool Has(const AOperativeCharacter* Operative, const TCHAR* Id)
	{
		return Operative->AvailableWeapons.ContainsByPredicate([Id](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == Id; });
	}

	void PlaceTarget(FState& State, float DistanceCm)
	{
		AOperativeCharacter* Hero = State.Hero.Get();
		AEnemyCharacter* Target = State.Target.Get();
		if (Hero && Target)
		{
			const FVector Spot = Hero->GetActorLocation() + State.Forward * DistanceCm;
			Target->TeleportTo(FVector(Spot.X, Spot.Y, Target->GetActorLocation().Z), (-State.Forward).Rotation(), false, true);
			Hero->SetActorRotation(State.Forward.Rotation());
		}
	}

	float Shoot(FState& State, AOperativeCharacter* Shooter)
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
		Shooter->bForceHitForTesting = true;
		Shooter->ForcedCritRollForTesting = 0.f;
		Shooter->ShootAtTarget(Target);
		Shooter->bForceHitForTesting = false;
		return Before - Health->GetCurrentHealth();
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
		if (State.Time > 120.f)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Hero = State.Hero.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 2.f || !Squad || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			bool bAllPistols = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f, true);
				Member->bTacticalCeaseFire = true;
				bAllPistols &= Has(Member, TEXT("pistol")) && Member->GetAmmoState(TEXT("pistol")).Clip == 12 && Member->GetAmmoState(TEXT("pistol")).Reserve == 24;
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					State.Hero = Member;
				}
			}
			Hero = State.Hero.Get();
			if (!Hero)
			{
				Check(State, false, TEXT("the Commander is there"));
				return Finish(State, false);
			}
			Check(State, bAllPistols, TEXT("1: every operative carries the pistol, 12 + 24 (the Engineer with the D-1 too)"));
			Check(State, Has(Hero, TEXT("m16")) && Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("m16"), TEXT("1: the Commander: the M16 in hands"));
			Squad->SetLeader(Hero);
			Hero->StopOperative();
			State.Start = Hero->GetActorLocation();
			State.Forward = Hero->GetActorForwardVector();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Hero)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, State.Start - State.Forward * 900.f + Hero->GetActorRightVector() * 300.f, Member), Member->GetActorRotation(), false, true);
				}
			}
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, State.Start + State.Forward * 500.f + FVector(0.f, 0.f, 60.f),
				(-State.Forward).Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
				State.Target = Hound;
			}
			Check(State, State.Target.IsValid(), TEXT("a target hound"));
			if (Hero->GetMesh())
			{
				Hero->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Hero);
			State.EquipBefore = Anim ? Anim->GetWeaponEquipClips() : 0;
			Hero->SwitchToWeaponById(TEXT("pistol"));
			Check(State, Hero->CurrentWeapon && Hero->CurrentWeapon->Stats.Handling == EWeaponHandling::Pistol, TEXT("1: the pistol's handling is \"pistol\" (the pistol layer)"));
			GoTo(State, 1);
			return true;
		}
		case 1: // the pistol in hands: model, equip clip, layer
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Hero);
			const UStaticMesh* Model = Hero->WeaponMesh ? Hero->WeaponMesh->GetStaticMesh() : nullptr;
			// The optimized Pistol 9mm (the art package, 2026-11-09): the pistol + its seated magazine.
			const UStaticMesh* Magazine = Hero->WeaponMeshParts.Num() > 0 && Hero->WeaponMeshParts[0] ? Hero->WeaponMeshParts[0]->GetStaticMesh() : nullptr;
			Check(State, Model && Model->GetName() == TEXT("SM_Pistol9mm_Pistol") && Hero->WeaponMesh->IsVisible() && Magazine
				&& Magazine->GetName() == TEXT("SM_Pistol9mm_Magazine"), FString::Printf(TEXT("2: the optimized Pistol 9mm + its magazine on the weapon socket (%s + %s)"),
				Model ? *Model->GetName() : TEXT("none"), Magazine ? *Magazine->GetName() : TEXT("none")));
			Check(State, Anim && Anim->GetWeaponEquipClips() > State.EquipBefore, TEXT("2: ... the equip clip played at the switch"));
			Check(State, Anim && Anim->GetWeaponLayerHandling() == EWeaponHandling::Pistol && Anim->GetShotgunLayerMask() == EWeaponLayerMask::Full,
				FString::Printf(TEXT("2: the pistol layer drives the body (mask %d)"), Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1));
			const FString Path = ClipPath(Hero);
			Check(State, Anim && !Anim->GetShotgunRetargetFolder().IsEmpty() && Path.StartsWith(Anim->GetShotgunRetargetFolder() + TEXT("/")),
				FString::Printf(TEXT("2: ... with the skeleton's retargeted pistol clip (%s)"), *Path));
			if (UOperativeAnimInstance* Probe = AnimOf(Hero))
			{
				Probe->bWeaponLayerProbe = true;
			}
			Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorRightVector() * 600.f), false);
			GoTo(State, 20);
			return true;
		}
		case 20: // walking, then sprinting: the legs are the ABP's, the upper body is the pistol hold
		{
			if (Hero->GetVelocity().Size2D() > 80.f)
			{
				if (const UOperativeAnimInstance* Probe = AnimOf(Hero))
				{
					State.MoveMaxLower = FMath::Max(State.MoveMaxLower, Probe->GetWeaponLayerLowerDeviation());
					State.MoveMaxUpper = FMath::Max(State.MoveMaxUpper, Probe->GetWeaponLayerUpperDeviation());
					State.MoveMaxSpeed = FMath::Max(State.MoveMaxSpeed, Hero->GetVelocity().Size2D());
					++State.MoveSamples;
				}
			}
			if (State.MoveSamples < 8 && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, State.MoveSamples >= 8 && State.MoveMaxLower < 0.01f && State.MoveMaxUpper > 1.f,
				FString::Printf(TEXT("2b: %s at up to %.0f cm/s over %d frames: the lower body is the ABP's (max change %.4f), the upper body is the pistol hold (max change %.1f)"),
					State.bSprintLeg ? TEXT("sprinting") : TEXT("walking"), State.MoveMaxSpeed, State.MoveSamples, State.MoveMaxLower, State.MoveMaxUpper));
			Hero->StopOperative();
			if (!State.bSprintLeg)
			{
				State.bSprintLeg = true;
				State.MoveMaxLower = State.MoveMaxUpper = State.MoveMaxSpeed = 0.f;
				State.MoveSamples = 0;
				Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() - Hero->GetActorRightVector() * 600.f), true);
				GoTo(State, 20);
				return true;
			}
			if (UOperativeAnimInstance* Probe = AnimOf(Hero))
			{
				Probe->bWeaponLayerProbe = false;
			}
			GoTo(State, 21);
			return true;
		}
		case 21: // let him stop before the shots
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			GoTo(State, 2);
			return true;
		}
		case 2: // damage by distance
		{
			const UOperativeAnimInstance* Anim = AnimOf(Hero);
			const int32 FireBefore = Anim ? Anim->GetShotgunFireClips() : 0;
			float Damage[4] = { 0.f, 0.f, 0.f, 0.f };
			const float Distances[4] = { 500.f, 1000.f, 1800.f, 2450.f };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				PlaceTarget(State, Distances[Index]);
				Damage[Index] = Shoot(State, Hero);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("PistolSmoke: damage 5 m %.2f, 10 m %.2f, 18 m %.2f, 24.5 m %.2f"), Damage[0], Damage[1], Damage[2], Damage[3]);
			const float Base = FMath::Max(0.01f, Damage[0]);
			Check(State, Damage[0] > 1.f && FMath::IsNearlyEqual(Damage[1], Damage[0], 0.01f), FString::Printf(TEXT("3: full damage at 5 and 10 m (%.2f / %.2f)"), Damage[0], Damage[1]));
			Check(State, FMath::IsNearlyEqual(Damage[2] / Base, 1.f - 0.4f * 8.f / 15.f, 0.02f), FString::Printf(TEXT("3: 18 m: x%.3f of it (expected x0.787)"), Damage[2] / Base));
			Check(State, FMath::IsNearlyEqual(Damage[3] / Base, 1.f - 0.4f * 14.5f / 15.f, 0.02f), FString::Printf(TEXT("3: 24.5 m: x%.3f (expected x0.613; x0.6 at the 25 m reach)"),
				Damage[3] / Base));
			Check(State, Anim && Anim->GetShotgunFireClips() - FireBefore >= 4, FString::Printf(TEXT("3: every shot plays the pistol's fire clip (%d)"),
				Anim ? Anim->GetShotgunFireClips() - FireBefore : 0));
			PlaceTarget(State, 600.f);
			Hero->CurrentClip = 0;
			Hero->ReserveAmmo = 24;
			Hero->bTacticalCeaseFire = false;
			SmokeUtils::ParkEnemyFarAway(State.Target.Get());
			Hero->StartReload();
			State.MaxReloadWeight = 0.f;
			State.ReloadDone = -1.f;
			Check(State, Hero->bIsReloading, TEXT("4: the pistol reload starts"));
			GoTo(State, 3);
			return true;
		}
		case 3: // the reload
		{
			const UOperativeAnimInstance* Anim = AnimOf(Hero);
			State.MaxReloadWeight = FMath::Max(State.MaxReloadWeight, Anim ? Anim->GetWeaponReloadWeight() : 0.f);
			if (Hero->bIsReloading && State.StageTime < 4.f)
			{
				return true;
			}
			if (State.ReloadDone < 0.f)
			{
				State.ReloadDone = State.StageTime;
				Check(State, Hero->CurrentClip == 12 && Hero->ReserveAmmo == 12, FString::Printf(TEXT("4: 12 rounds in (clip %d, reserve %d)"), Hero->CurrentClip, Hero->ReserveAmmo));
				Check(State, State.ReloadDone > 1.0f && State.ReloadDone < 1.8f, FString::Printf(TEXT("4: ... after %.1f s (the pistol's 1.3 s)"), State.ReloadDone));
				Check(State, State.MaxReloadWeight > 0.9f, FString::Printf(TEXT("4: ... the pistol's reload clip on the upper body (weight %.2f)"), State.MaxReloadWeight));
			}
			// The sidearm switch: the M16's last round at the hound 6 m out, the empty magazine starts its reload -> the pistol instead.
			Hero->SwitchToWeaponById(TEXT("m16"));
			Hero->ReserveAmmo = 30;
			// A fresh hound 6 m out (the first one was parked for the reload).
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + State.Forward * 600.f) + FVector(0.f, 0.f, 60.f), (-State.Forward).Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
				State.Target = Hound;
			}
			Hero->SetActorRotation(State.Forward.Rotation());
			State.DrawsBefore = Hero->GetSidearmDraws();
			Hero->CurrentClip = 1;
			Shoot(State, Hero);
			Check(State, Hero->CurrentClip == 0, TEXT("5: the M16's last round fired"));
			Hero->StartReload();
			Hero->bTacticalCeaseFire = false;
			GoTo(State, 4);
			return true;
		}
		case 4:
		{
			const bool bDrawn = Hero->GetSidearmDraws() > State.DrawsBefore;
			if (!bDrawn && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, bDrawn && Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("pistol") && Hero->IsSidearmDrawnByRule(),
				FString::Printf(TEXT("5: the M16 ran dry with the hound 6 m away -> the pistol (drawn %d, holding %s)"), bDrawn ? 1 : 0,
					Hero->CurrentWeapon ? *Hero->CurrentWeapon->WeaponId : TEXT("-")));
			const FWeaponAmmoState Rifle = Hero->GetAmmoState(TEXT("m16"));
			Check(State, Rifle.Clip == 0 && Rifle.Reserve == 30, FString::Printf(TEXT("5: ... the M16 kept for later: empty, 30 in reserve (%d + %d)"), Rifle.Clip, Rifle.Reserve));
			Check(State, !Hero->bIsReloading, TEXT("5: ... no reload standing in the open"));
			SmokeUtils::ParkEnemyFarAway(State.Target.Get());
			GoTo(State, 5);
			return true;
		}
		case 5:
		{
			const bool bBack = Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("m16");
			if (!bBack && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, bBack && State.StageTime >= 2.5f, FString::Printf(TEXT("5: the hound gone -> back to the M16 after %.1f s (holster after 3 s without a target)"), State.StageTime));
			Check(State, bBack && (Hero->bIsReloading || Hero->CurrentClip > 0), TEXT("5: ... and it reloads"));
			Hero->bTacticalCeaseFire = true;
			// A PMC fighter given the pistol.
			UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
			AOperativeCharacter* Fighter = Pmc ? Pmc->SpawnFighter(1, SmokeUtils::FreeSpot(World, State.Start + Hero->GetActorRightVector() * -700.f), State.Forward.Rotation(), 7, 0) : nullptr;
			Check(State, Fighter != nullptr, TEXT("6: a PMC fighter"));
			if (!Fighter)
			{
				return Finish(State, false);
			}
			Fighter->bTacticalCeaseFire = true;
			Fighter->HealthComponent->SetMaxHealth(100000.f, true);
			if (!Has(Fighter, TEXT("pistol")))
			{
				if (UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol")))
				{
					Fighter->AddArsenalWeapon(Pistol);
				}
			}
			Fighter->SwitchToWeaponById(TEXT("pistol"));
			State.Fighter = Fighter;
			GoTo(State, 6);
			return true;
		}
		case 6:
		{
			AOperativeCharacter* Fighter = State.Fighter.Get();
			if (State.StageTime < 1.5f || !Fighter)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Fighter);
			const FString Path = ClipPath(Fighter);
			Check(State, Anim && Anim->GetWeaponLayerHandling() == EWeaponHandling::Pistol && Anim->GetShotgunLayerMask() != EWeaponLayerMask::Off
				&& (Path.StartsWith(TEXT("/Game/Characters/Animations/Pistol/Source/")) || Path.StartsWith(TEXT("/Game/Characters/Mannequins/"))),
				FString::Printf(TEXT("6: the PMC fighter with the pistol runs the pistol layer on the source clips (%s)"), *Path));
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
		if (IConsoleVariable* Chance = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
		{
			Chance->Set(0.f, ECVF_SetByConsole); // the damage ratios are compared shot by shot
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PistolSmoke"),
		TEXT("Dev check: the pistol (loadout, the 9mm model, damage by distance, reload, the pistol layer, the sidearm switch, a PMC fighter's layer); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
