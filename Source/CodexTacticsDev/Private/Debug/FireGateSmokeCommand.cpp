// Dev-only headless check of the fire gate (bugs 058c / 061, the game designer 2026-11-15: "he faces the wall and his rounds fly at the
// enemy"; "they fire first and turn after - it must be turn, raise, then the shot", EVERYWHERE). Runs on L_MovementTest in its own empty
// arena (SmokeUtils::FindClearArena; the map is never edited), a real-time wave fight, a frozen marksman as the target (every enemy seen:
// the sight's cinematic reveal), the commander's pose evaluated headless (AlwaysTickPoseAndRefreshBones: the barrel is the animated one):
//   Scripts/smoke_parallel.ps1 -Commands FireGateSmoke
//  A the designer's case: badly wounded (30 % HP, the heavy tier), CROUCHED next to a low box, his body facing the box, the marksman 8 m
//    to his side - the automatic fire turns him first, raises the weapon out of the wounded hunch (InjuredRaise), only then a round; no
//    round with the body or the rifle off the target;
//  B the same, IN COVER at the box, the marksman over the box (inside its frontal arc, ~35 deg to the side): every round from the cover
//    pose has the animated barrel on him (or he stepped off the box and the body is on him), none before the cover fire stance is up;
//  C quick fire (Ctrl + click on the snow over the box) from the cover: the same - it used to leave from the wall at once;
//  D the SUPPRESS order (Shift + click on the marksman over the box) from the cover: the same;
//  F wounded in cover at the box, the marksman over it far out to the side (~70 deg): no round from the cover pose with the barrel off him;
//  E a targeted shot at a barrel behind him (Ctrl + click, IssueTargetedShot -> ShootAtObject): no snap and no instant round - he turns,
//    raises, the round leaves with the body on the barrel; one round.
// Every round is sampled on OnWeaponFiredNative. Logs "Smoke ok / FAIL" lines and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/FireGateRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"

namespace FireGateSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float Timeout = 150.f;
	/** Slack on the measured angles: one frame of the turn (360 deg/s at 60 fps = 6 deg) plus the pose's own wobble. */
	constexpr float BodySlackDeg = 3.f;
	constexpr float BarrelSlackDeg = 10.f;

	struct FRound
	{
		float Time = 0.f;
		float BodyErrorDeg = 0.f;
		float BarrelErrorDeg = -1.f;
		bool bInCover = false;
		float InjuredRaise = 1.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		SmokeUtils::FPreloadGate PreloadGate;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AStaticMeshActor> Box;
		TWeakObjectPtr<ABarrelActor> Barrel;
		FCoverSlot Slot;
		bool bSlot = false;
		FVector Side = FVector::RightVector;
		// The scene's watch
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		FDelegateHandle ShotHandle;
		TArray<FRound> Rounds;
		FVector AimPoint = FVector::ZeroVector;
		double WatchStart = 0.0;
		float InitialBodyErrorDeg = 0.f;
		int32 HoldsAtStart = 0;
		int32 ClipBefore = 0;
		bool bLeftCover = false;
		bool bInstantRound = false;
		float NextLog = 0.f;
		bool bRevealBefore = false;
		bool bRevealOwned = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Unwatch(FState& State)
	{
		if (AOperativeCharacter* Shooter = State.Shooter.Get(); Shooter && State.ShotHandle.IsValid())
		{
			Shooter->OnWeaponFiredNative.Remove(State.ShotHandle);
		}
		State.ShotHandle.Reset();
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		Unwatch(State);
		if (State.bRevealOwned && World)
		{
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->SetCinematicReveal(State.bRevealBefore);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FireGateSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		State.NextLog = 0.f;
	}

	/** Planar angle between a direction and the line from a point to a target, deg (180 for a degenerate one). */
	float PlanarErrorDeg(const FVector& From, const FVector& Direction, const FVector& Target)
	{
		const FVector To = (Target - From).GetSafeNormal2D();
		const FVector Dir = Direction.GetSafeNormal2D();
		return To.IsNearlyZero() || Dir.IsNearlyZero() ? 180.f : FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, Dir), -1.f, 1.f)));
	}

	/** The body's aim line (the actor yaw + the barrel's pose offset, as the gate judges it) to the point, deg. */
	float BodyErrorDeg(const AOperativeCharacter& Operative, const FVector& Target)
	{
		const FVector Line = FRotator(0.f, Operative.GetActorRotation().Yaw + Operative.GetBarrelYawOffset(), 0.f).Vector();
		return PlanarErrorDeg(Operative.GetActorLocation(), Line, Target);
	}

	/** The animated rifle's planar error to the point from its muzzle, deg; -1 without a level rifle. */
	float BarrelErrorDeg(const AOperativeCharacter& Operative, const FVector& Target)
	{
		const UStaticMeshComponent* Weapon = Operative.WeaponMesh;
		if (!Weapon || !Weapon->GetStaticMesh() || !Weapon->IsVisible())
		{
			return -1.f;
		}
		const FVector Barrel = Weapon->GetComponentTransform().TransformVectorNoScale(Operative.GetBarrelAxisOnModel());
		return Barrel.Size2D() < 0.3f ? -1.f : PlanarErrorDeg(Operative.GetWeaponMuzzleLocation(), Barrel, Target);
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	int32 AllHolds(const AOperativeCharacter& Operative)
	{
		int32 Sum = 0;
		for (int32 Block = 1; Block < static_cast<int32>(EFireGateBlock::Count); ++Block)
		{
			Sum += Operative.GetFireGateHolds(static_cast<EFireGateBlock>(Block));
		}
		return Sum;
	}

	/** Samples every round the shooter fires from now on, judged against Point (the scene's target). */
	void Watch(FState& State, AOperativeCharacter* Shooter, const FVector& Point)
	{
		Unwatch(State);
		State.Rounds.Reset();
		State.Shooter = Shooter;
		State.AimPoint = Point;
		State.bLeftCover = false;
		State.WatchStart = Shooter->GetWorld()->GetTimeSeconds(); // game time: the fixed-step game runs faster than the ticker
		State.InitialBodyErrorDeg = BodyErrorDeg(*Shooter, Point);
		State.HoldsAtStart = AllHolds(*Shooter);
		State.ClipBefore = Shooter->CurrentClip;
		FState* Raw = &State;
		State.ShotHandle = Shooter->OnWeaponFiredNative.AddLambda([Raw](AOperativeCharacter* Who, AActor*, bool)
		{
			if (!Who || !Who->GetWorld())
			{
				return;
			}
			FRound Round;
			Round.Time = static_cast<float>(Who->GetWorld()->GetTimeSeconds() - Raw->WatchStart);
			Round.BodyErrorDeg = BodyErrorDeg(*Who, Raw->AimPoint);
			Round.BarrelErrorDeg = BarrelErrorDeg(*Who, Raw->AimPoint);
			Round.bInCover = Who->bInCover;
			const UOperativeAnimInstance* Anim = AnimOf(Who);
			Round.InjuredRaise = Anim ? Anim->GetInjuredRaise() : 1.f;
			Raw->Rounds.Add(Round);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s fired at %.2f s: body %.0f deg, rifle %.0f deg off the target, in cover %d, injured raise %.2f, stance %d"),
				*Who->DisplayName.ToString(), Round.Time, Round.BodyErrorDeg, Round.BarrelErrorDeg, Round.bInCover ? 1 : 0, Round.InjuredRaise,
				static_cast<int32>(Who->GetStance()));
		});
	}

	void LogAim(FState& State)
	{
		const AOperativeCharacter* Shooter = State.Shooter.Get();
		if (!Shooter || State.StageTime < State.NextLog)
		{
			return;
		}
		State.NextLog = State.StageTime + 0.25f;
		State.bLeftCover |= State.Stage > 0 && !Shooter->bInCover;
		const UOperativeAnimInstance* Anim = AnimOf(Shooter);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %.2f s %s: body %.0f, rifle %.0f deg off, gate residual %.0f, gate %s, in cover %d (%s), aim pose up %.2f s, injured raise %.2f"),
			State.StageTime, *Shooter->DisplayName.ToString(), BodyErrorDeg(*Shooter, State.AimPoint), BarrelErrorDeg(*Shooter, State.AimPoint),
			Shooter->GetFireGateResidualDeg(State.AimPoint), FireGateRules::BlockName(Shooter->GetLastFireGateBlock()), Shooter->bInCover ? 1 : 0,
			Shooter->IsCornerAimActive() ? TEXT("fire stance") : TEXT("plain"), Anim ? Anim->GetAimPoseUpSeconds() : -1.f, Anim ? Anim->GetInjuredRaise() : -1.f);
	}

	/**
	 * The gate over the rounds watched: at least one, none with the body (open) / the animated barrel (cover pose) off the target, the first
	 * one after the gate held (he turned / raised first) and no sooner than MinFirstSeconds; wounded: the weapon up out of the injured pose.
	 */
	void CheckRounds(FState& State, const TCHAR* Scene, float MinFirstSeconds, bool bWounded)
	{
		const AOperativeCharacter* Shooter = State.Shooter.Get();
		Check(State, !State.Rounds.IsEmpty(), FString::Printf(TEXT("%s: rounds leave (%d)"), Scene, State.Rounds.Num()));
		if (State.Rounds.IsEmpty() || !Shooter)
		{
			return;
		}
		const float Arc = Shooter->GetBodyArcDeg();
		const float Cone = Shooter->AimConeDeg;
		float WorstOpenBody = 0.f;
		float WorstBarrel = 0.f;
		int32 CoverRounds = 0;
		for (const FRound& Round : State.Rounds)
		{
			if (Round.bInCover)
			{
				++CoverRounds;
				WorstBarrel = FMath::Max(WorstBarrel, Round.BarrelErrorDeg < 0.f ? 180.f : Round.BarrelErrorDeg);
			}
			else
			{
				WorstOpenBody = FMath::Max(WorstOpenBody, Round.BodyErrorDeg);
				WorstBarrel = FMath::Max(WorstBarrel, Round.BarrelErrorDeg < 0.f ? 180.f : Round.BarrelErrorDeg);
			}
		}
		Check(State, WorstOpenBody <= Arc + BodySlackDeg, FString::Printf(TEXT("%s: every open round with the body on the target (worst %.0f deg, arc %.0f)"),
			Scene, WorstOpenBody, Arc));
		Check(State, WorstBarrel <= Cone + BarrelSlackDeg, FString::Printf(TEXT("%s: every round with the animated rifle on the target (worst %.0f deg, cone %.0f; %d of %d from the cover pose)"),
			Scene, WorstBarrel, Cone, CoverRounds, State.Rounds.Num()));
		Check(State, AllHolds(*Shooter) > State.HoldsAtStart, FString::Printf(TEXT("%s: the gate held the fire before the first round (turn / raise: %d holds)"),
			Scene, AllHolds(*Shooter) - State.HoldsAtStart));
		Check(State, State.Rounds[0].Time >= MinFirstSeconds, FString::Printf(TEXT("%s: the first round at %.2f s, not before the turn / raise (>= %.2f s)"),
			Scene, State.Rounds[0].Time, MinFirstSeconds));
		if (bWounded)
		{
			Check(State, State.Rounds[0].InjuredRaise >= 0.97f, FString::Printf(TEXT("%s: wounded - the weapon is up out of the injured pose at the first round (raise %.2f)"),
				Scene, State.Rounds[0].InjuredRaise));
		}
	}

	AStaticMeshActor* SpawnBox(UWorld* World, const FVector& Centre, const FVector& Size, const FRotator& Rotation)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Centre, Rotation, Params);
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Box || !Cube)
		{
			return Box;
		}
		Box->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetActorScale3D(Size / 100.f);
		return Box;
	}

	void Freeze(AEnemyCharacter* Enemy)
	{
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f;
			Enemy->SetActorTickEnabled(false);
		}
	}

	/** The enemy to Spot (frozen there, facing the commander). */
	void PlaceEnemy(FState& State, const FVector& Spot)
	{
		if (AEnemyCharacter* Enemy = State.Enemy.Get())
		{
			const FVector Toward = State.Members.IsValidIndex(0) && State.Members[0].IsValid() ? State.Members[0]->GetActorLocation() - Spot : -State.F;
			Enemy->TeleportTo(FVector(Spot.X, Spot.Y, State.O.Z + 100.f), Toward.GetSafeNormal2D().Rotation(), false, true);
		}
	}

	void ParkEnemy(FState& State)
	{
		PlaceEnemy(State, State.O + State.F * 6000.f + State.R * 6000.f);
	}

	/** The commander back at the arena origin, out of cover, no orders, his fire held until the scene starts. */
	void Reset(AOperativeCharacter* Commander, FState& State, EOperativeStance Stance, float HealthPercent)
	{
		Commander->CancelQuickFire();
		Commander->CancelSuppressFire(TEXT("FireGateSmoke"));
		Commander->SetManualPriorityTarget(nullptr);
		if (Commander->bInCover)
		{
			Commander->LeaveCover(TEXT("FireGateSmoke"));
		}
		Commander->StopOperative();
		Commander->bTacticalCeaseFire = true;
		Commander->GrenadesCount = 0;
		Commander->CurrentClip = Commander->CurrentWeapon ? Commander->CurrentWeapon->Stats.Ammo.MagazineSize : 30;
		Commander->ReserveAmmo = 300;
		if (Commander->HealthComponent)
		{
			Commander->HealthComponent->RestoreHealth(HealthPercent, 100.f);
		}
		Commander->SetStance(Stance);
		const FVector Spot = SmokeUtils::FreeSpot(Commander->GetWorld(), State.O, Commander);
		Commander->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State.F.Rotation(), false, true);
	}

	bool PutInCover(AOperativeCharacter* Commander, const FCoverSlot& Slot)
	{
		if (Commander->bInCover)
		{
			Commander->LeaveCover(TEXT("FireGateSmoke"));
		}
		Commander->StopOperative();
		Commander->TeleportTo(Slot.WorldLocation + FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight() + 5.f), Slot.WallNormal.Rotation(), false, true);
		Commander->EnterCover(Slot);
		return Commander->bInCover;
	}

	/** A click on the ground under Point (the controller's own click dispatcher). */
	FHitResult GroundHit(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		const FVector From(Point.X, Point.Y, Point.Z + 500.f);
		const FVector To(Point.X, Point.Y, Point.Z - 1000.f);
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility))
		{
			Hit = FHitResult(nullptr, nullptr, FVector(Point.X, Point.Y, Point.Z), FVector::UpVector);
		}
		return Hit;
	}

	/** The point over the box: Depth cm behind its face, Along cm to the threat side (State.Side). */
	FVector OverTheBox(const FState& State, float Depth, float Along)
	{
		return State.Slot.WallPoint - State.Slot.WallNormal.GetSafeNormal2D() * Depth + State.Side * Along;
	}

	bool Step(UWorld* World, FState& State)
	{
		switch (State.PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.PreloadGate.FailReason);
			return Finish(World, State, false);
		default:
			break;
		}
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Squad || !Flow || !PC || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d at %.1fs"), State.Stage, State.Time);
			Check(State, false, TEXT("finished in time (and every subsystem there)"));
			return Finish(World, State, false);
		}
		AOperativeCharacter* Commander = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		AEnemyCharacter* Enemy = State.Enemy.Get();
		if (State.Stage > 0 && (!Commander || !Enemy || !State.Box.IsValid()))
		{
			Check(State, false, TEXT("the commander, the marksman and the box are there"));
			return Finish(World, State, false);
		}
		if (State.Stage > 0)
		{
			LogAim(State);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Enemy && It->CustomTimeDilation > 0.f)
				{
					SmokeUtils::ParkEnemyFarAway(*It); // the wave's own enemies stay out of it
					Freeze(*It);
				}
			}
			Freeze(Enemy);
			// Commander Mode's cover autonomy must not hold his fire behind the box (the gate is under test, not the ROE).
			Commander->bCoverHoldFire = false;
			Commander->bHasPostureOverride = true;
			Commander->PostureOverride = ESquadFirePosture::Aggressive;
			if (USuppressionSubsystem* Suppression = World->GetSubsystem<USuppressionSubsystem>())
			{
				Suppression->SetLevelForTesting(Commander, 0.f); // nobody suppresses him: he may lean out / step off
			}
		}
		const float Raise = FireGateRules::GetConfig().RaiseSeconds;
		const float CoverRaise = FireGateRules::GetConfig().CoverRaiseSeconds;

		switch (State.Stage)
		{
		case 0: // Set-up: the arena, a real-time fight, the squad's two others out of it, the frozen marksman, the low box.
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			if (Roster.Num() < 1 || !Roster[0])
			{
				Check(State, false, TEXT("a squad"));
				return Finish(World, State, false);
			}
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-1500.f, -1200.f), FVector2D(1500.f, 1200.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(World, State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				Freeze(*It);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Roster.Num(); ++Index)
			{
				AOperativeCharacter* Member = Roster[Index];
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				Member->bTacticalCeaseFire = true;
				if (Member->PanicComponent)
				{
					Member->PanicComponent->bEnabled = false;
				}
				if (Member->RageComponent)
				{
					Member->RageComponent->Config.RequiredCrits = 100000;
				}
				if (USkeletalMeshComponent* Body = Member->GetMesh())
				{
					Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // the barrel as animated
				}
				// The two others well behind the arena, holding their fire throughout.
				const FVector Spot = SmokeUtils::FreeSpot(World, Index == 0 ? State.O : State.O - State.F * 1300.f - State.R * (Index * 250.f), Member);
				Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State.F.Rotation(), false, true);
				State.Members.Add(Member);
			}
			Squad->SetLeader(Roster[0]);
			if (IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.FireTrace")))
			{
				Trace->Set(1, ECVF_SetByConsole); // [FireTrace] lines: why a round is held (the gate's verdicts among them)
			}
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				State.bRevealBefore = Sight->IsCinematicReveal();
				State.bRevealOwned = true;
				Sight->SetCinematicReveal(true); // the gate under test, not the fog of war's acquisition
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			AEnemyCharacter* Marksman = Waves ? Waves->SpawnEnemy(EEnemyArchetype::Marksman, State.O + State.F * 6000.f + State.R * 6000.f + FVector(0.f, 0.f, 100.f),
				(-State.F).Rotation()) : nullptr;
			Check(State, Marksman != nullptr, TEXT("a marksman spawned (the frozen target)"));
			if (!Marksman)
			{
				return Finish(World, State, false);
			}
			State.Enemy = Marksman;
			if (Marksman->GetHealthComponent())
			{
				Marksman->GetHealthComponent()->SetMaxHealth(1000000.f); // he lives through every scene
			}
			Freeze(Marksman);
			// The low box: 60 cm deep along F, 160 cm wide, 60 cm high (a barricade: the crouched rifle clears its top), its face 70 cm in front of him.
			State.Box = SpawnBox(World, State.O + State.F * 100.f + FVector(0.f, 0.f, 30.f), FVector(60.f, 160.f, 60.f), State.F.Rotation());
			Check(State, State.Box.IsValid(), TEXT("a low box (60 x 160 x 60 cm, the barricade height) in front of the commander"));
			if (!State.Box.IsValid())
			{
				return Finish(World, State, false);
			}
			Next(State);
			return true;
		}
		case 1: // A: badly wounded, crouched next to the box, facing it; the marksman 8 m to his right.
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Crouching, 30.f);
			Check(State, Commander->IsWounded() && Commander->GetWoundTier() == CriticalWoundRules::EWoundTier::Heavy,
				FString::Printf(TEXT("A: badly wounded (the heavy tier, tier %d)"), static_cast<int32>(Commander->GetWoundTier())));
			Next(State);
			return true;
		}
		case 2: // A: the stance clip and the wounded pose settle, then the target appears at his side.
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Commander->GetStance() == EOperativeStance::Crouching && !Commander->bInCover, TEXT("A: crouched next to the box (not in cover)"));
			const UOperativeAnimInstance* Anim = AnimOf(Commander);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: A: injured gait %d, raise %.2f, aiming %d"), Anim ? static_cast<int32>(Anim->GetInjuredGait()) : -1,
				Anim ? Anim->GetInjuredRaise() : -1.f, Anim && Anim->bIsAiming ? 1 : 0);
			const FVector Target = State.O + State.R * 800.f;
			PlaceEnemy(State, Target);
			Watch(State, Commander, Enemy->GetActorLocation());
			Commander->bTacticalCeaseFire = false;
			Check(State, State.InitialBodyErrorDeg >= 70.f, FString::Printf(TEXT("A: the marksman %.0f deg off his body (to his side)"), State.InitialBodyErrorDeg));
			Next(State);
			return true;
		}
		case 3: // A: the automatic fire.
		{
			if (State.Rounds.Num() < 3 && State.StageTime < 8.f)
			{
				return true;
			}
			const float TurnSeconds = 0.8f * FMath::Max(0.f, State.InitialBodyErrorDeg - Commander->GetBodyArcDeg()) / FMath::Max(1.f, Commander->AimTurnRateDegPerSec);
			CheckRounds(State, TEXT("A (wounded, crouched, target at his side)"), FMath::Max(TurnSeconds, Raise * 0.8f), true);
			Commander->bTacticalCeaseFire = true;
			Unwatch(State);
			ParkEnemy(State);
			Next(State);
			return true;
		}
		case 4: // B: wounded, in cover at the box; the marksman over the box ~35 deg to the side.
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Crouching, 30.f);
			TArray<const AActor*> Ignore;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				Ignore.Add(Member.Get());
			}
			State.bSlot = CoverTraceRules::FindCoverSlotAt(World, State.O + State.F * 50.f + FVector(0.f, 0.f, 30.f), State.F, State.Slot, FCoverTraceConfig(), Ignore);
			Check(State, State.bSlot && State.Slot.Height == ECoverHeight::LowCover, FString::Printf(TEXT("B: a low cover slot at the box (found %d, height %d)"),
				State.bSlot ? 1 : 0, static_cast<int32>(State.Slot.Height)));
			if (!State.bSlot)
			{
				return Finish(World, State, false);
			}
			State.Side = FVector::DotProduct(State.Slot.RightTangent(), State.R) >= 0.f ? State.Slot.RightTangent() : -State.Slot.RightTangent();
			Check(State, PutInCover(Commander, State.Slot) && Commander->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("B: the commander in cover at the box"));
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			PlaceEnemy(State, OverTheBox(State, 900.f, 600.f));
			Watch(State, Commander, Enemy->GetActorLocation());
			Commander->bTacticalCeaseFire = false;
			Next(State);
			return true;
		}
		case 6: // B: the automatic fire from the cover.
		{
			if (State.Rounds.Num() < 3 && State.StageTime < 8.f)
			{
				return true;
			}
			CheckRounds(State, TEXT("B (wounded, in cover at the box, target over it)"), CoverRaise * 0.8f, true);
			Commander->bTacticalCeaseFire = true;
			Unwatch(State);
			ParkEnemy(State);
			Next(State);
			return true;
		}
		case 7: // C: healthy, in cover at the box; Ctrl + click on the snow over the box (quick fire, no enemy near the point).
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Crouching, 100.f);
			Check(State, PutInCover(Commander, State.Slot) && Commander->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("C: the commander in cover at the box"));
			Next(State);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FVector Point = OverTheBox(State, 800.f, 350.f);
			const FHitResult Hit = GroundHit(World, Point);
			Watch(State, Commander, FVector(Hit.ImpactPoint));
			Commander->bTacticalCeaseFire = false;
			PC->SetSimulatedAttackModifier(true);
			PC->HandleWorldHit(Hit);
			PC->SetSimulatedAttackModifier(false);
			Check(State, Commander->HasQuickFireOrder(), TEXT("C: Ctrl + click on the snow over the box = a quick fire order"));
			State.bInstantRound = !State.Rounds.IsEmpty();
			Next(State);
			return true;
		}
		case 9: // C: the burst.
		{
			if (Commander->HasQuickFireOrder() && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !State.bInstantRound, TEXT("C: no round in the frame of the order (it used to leave from the wall at once)"));
			CheckRounds(State, TEXT("C (quick fire from cover)"), CoverRaise * 0.8f, false);
			Commander->CancelQuickFire();
			Commander->bTacticalCeaseFire = true;
			Unwatch(State);
			Next(State);
			return true;
		}
		case 10: // D: in cover again; Shift + click on the marksman over the box (the SUPPRESS order).
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Crouching, 100.f);
			Check(State, PutInCover(Commander, State.Slot) && Commander->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("D: the commander in cover at the box"));
			Next(State);
			return true;
		}
		case 11:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			PlaceEnemy(State, OverTheBox(State, 900.f, 450.f));
			Watch(State, Commander, Enemy->GetActorLocation());
			Commander->bTacticalCeaseFire = false;
			PC->SetSimulatedShiftModifier(true);
			PC->HandleWorldHit(FHitResult(Enemy, nullptr, Enemy->GetActorLocation(), FVector::UpVector));
			PC->SetSimulatedShiftModifier(false);
			Check(State, Commander->HasSuppressOrder(), TEXT("D: Shift + click on the marksman over the box = the SUPPRESS order"));
			State.bInstantRound = !State.Rounds.IsEmpty();
			Next(State);
			return true;
		}
		case 12: // D: the suppressive rounds.
		{
			if (State.Rounds.Num() < 4 && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !State.bInstantRound, TEXT("D: no round in the frame of the order"));
			CheckRounds(State, TEXT("D (suppress from cover)"), CoverRaise * 0.8f, false);
			Commander->CancelSuppressFire(TEXT("FireGateSmoke"));
			Commander->bTacticalCeaseFire = true;
			Unwatch(State);
			ParkEnemy(State);
			Next(State);
			return true;
		}
		case 13: // E: standing in the open, a barrel 7 m behind him to the right; Ctrl + click on it.
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Standing, 100.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Spot = State.O - State.F * 700.f + State.R * 150.f + FVector(0.f, 0.f, 60.f);
			State.Barrel = World->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params);
			Check(State, State.Barrel.IsValid(), TEXT("E: a barrel 7 m behind him"));
			if (!State.Barrel.IsValid())
			{
				return Finish(World, State, false);
			}
			Next(State);
			return true;
		}
		case 14:
		{
			ABarrelActor* Barrel = State.Barrel.Get();
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (!Barrel)
			{
				Check(State, false, TEXT("E: the barrel is there"));
				return Finish(World, State, false);
			}
			Watch(State, Commander, Barrel->GetActorLocation());
			Check(State, State.InitialBodyErrorDeg >= 100.f, FString::Printf(TEXT("E: the barrel %.0f deg off his body (behind him)"), State.InitialBodyErrorDeg));
			PC->IssueTargetedShot(Barrel);
			Check(State, !Barrel->IsBurning() && Commander->IsObjectShotPending() && FMath::Abs(BodyErrorDeg(*Commander, Barrel->GetActorLocation()) - State.InitialBodyErrorDeg) < 5.f,
				TEXT("E: no snap and no round in the frame of the order - the shot waits (pending)"));
			Next(State);
			return true;
		}
		case 15: // E: he turns, raises, then the round.
		{
			ABarrelActor* Barrel = State.Barrel.Get();
			if (Barrel && !Barrel->IsBurning() && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Barrel && Barrel->IsBurning(), TEXT("E: the barrel explodes after the turn"));
			Check(State, Commander->CurrentClip == State.ClipBefore - 1 && State.Rounds.Num() == 1 && Commander->GetObjectShotsFired() >= 1,
				FString::Printf(TEXT("E: one round (clip %d -> %d, %d round(s))"), State.ClipBefore, Commander->CurrentClip, State.Rounds.Num()));
			const float TurnSeconds = 0.8f * FMath::Max(0.f, State.InitialBodyErrorDeg - Commander->GetBodyArcDeg()) / FMath::Max(1.f, Commander->AimTurnRateDegPerSec);
			CheckRounds(State, TEXT("E (targeted shot at a barrel behind him)"), FMath::Max(TurnSeconds, Raise * 0.8f), false);
			Unwatch(State);
			Next(State);
			return true;
		}
		case 16: // F: wounded, in cover at the box; the marksman over the box but far out to the side (~70 deg off straight over it).
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Reset(Commander, State, EOperativeStance::Crouching, 30.f);
			Check(State, PutInCover(Commander, State.Slot) && Commander->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("F: the commander in cover at the box"));
			Next(State);
			return true;
		}
		case 17:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			PlaceEnemy(State, OverTheBox(State, 350.f, 950.f));
			Watch(State, Commander, Enemy->GetActorLocation());
			Commander->bTacticalCeaseFire = false;
			Next(State);
			return true;
		}
		case 18: // F: the cover pose cannot bring the barrel round that far: no round from it with the barrel off - he leans / steps out first.
		{
			if (State.Rounds.Num() < 3 && State.StageTime < 8.f)
			{
				return true;
			}
			CheckRounds(State, TEXT("F (wounded, in cover, target far out to the side over the box)"), CoverRaise * 0.8f, true);
			Commander->bTacticalCeaseFire = true;
			Unwatch(State);
			return Finish(World, State, true);
		}
		default:
			return Finish(World, State, false);
		}
	}

	void Start(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* Current = WeakWorld.Get();
			if (!Current)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped: the world is gone (stage %d)"), State->Stage);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL"));
				return false;
			}
			return Step(Current, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FireGateSmoke"),
		TEXT("Dev check (bugs 058c / 061, the fire gate): wounded crouched at a box / in cover, quick fire and suppress from cover, a targeted shot behind him - every round only after the turn and the raise, with the body / the animated rifle on the target; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World); }));
}

#endif // !UE_BUILD_SHIPPING
