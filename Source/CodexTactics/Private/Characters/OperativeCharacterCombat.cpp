// AOperativeCharacter: Real-time target selection and fire: lines of fire, blind fire, fire posture, ShootAtTarget.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (_find_shoot_target, _process_combat_shooting, _shoot_at_target,
// is_behind_barricade, _notify_barricade_blocked). Fire posture: FirePostureRules (UE-only).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/TurretActor.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Telemetry/RunTelemetrySubsystem.h"
#include "UI/FloatingTextSubsystem.h"

bool AOperativeCharacter::IsBehindBarricade() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Centre = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 100.f);
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (!IsValid(*It) || (Health && !Health->IsAlive()))
		{
			continue;
		}
		FVector Origin;
		FVector Extent;
		It->GetActorBounds(true, Origin, Extent);
		if (FVector::Dist(Centre, FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z)) <= 220.f)
		{
			return true;
		}
	}
	return false;
}

bool AOperativeCharacter::EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked) const
{
	bOutBarricadeBlocked = false;
	UWorld* World = GetWorld();
	// Sprint 08: an enemy the squad does not see is not aimed at (blind fire at its silhouette is an order).
	if (const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
		Sight && Enemy && !Sight->IsVisibleToSquad(Enemy))
	{
		return false;
	}
	const FVector MyFeet = OperativeCharacterInternal::ShotFeet(this);
	const FVector EnemyFeet = OperativeCharacterInternal::ShotFeet(Enemy);
	// Sprint 12: nothing to fire round / over from this cover (a full wall without a corner). User decision 2026-10-06:
	// only a target behind the wall / around the corner needs that; one out on the open side gets a normal shot.
	const bool bCornerShot = IsCornerShotTarget(EnemyFeet);
	if (bCornerShot && !CanFireFromCover())
	{
		return false;
	}
	if (!World || SquadFireRules::IsInDeadZone(MyFeet, EnemyFeet))
	{
		return false;
	}
	const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(MyFeet.Z, EnemyFeet.Z);
	const float Range = (CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f) * SquadFireRules::GetPostureRangeMultiplier(Stance)
		* (Elevation.bElevated ? Elevation.RangeMultiplier : 1.f);
	// Godot: from the operative's centre (1 m above the feet) to the enemy's feet.
	const float Distance = FVector::Dist(MyFeet + FVector(0.f, 0.f, 100.f), EnemyFeet);
	if (Distance > Range)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadShotLine), false, this);
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // the squad
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // Godot "allies"
	}
	EShotLineHit Kind = EShotLineHit::Clear;
	AActor* HitEnemy = Enemy;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, bCornerShot ? GetCoverFireOrigin() : GetMuzzleLocation(), EnemyFeet + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params))
	{
		AActor* Blocker = Hit.GetActor();
		if (Blocker && Blocker != Enemy)
		{
			if (Blocker->ActorHasTag(FName(TEXT("Enemy"))))
			{
				HitEnemy = bKeepTarget ? Enemy : Blocker;
			}
			else
			{
				Kind = Blocker->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
			}
		}
	}
	const FShotLineVerdict Verdict = SquadFireRules::JudgeLine(Kind, Stance, Elevation);
	bOutBarricadeBlocked = Verdict.bBarricadeBlocked;
	if (!Verdict.bCanHit)
	{
		return false;
	}
	Out.Enemy = HitEnemy;
	Out.Distance = Distance;
	Out.Cover = Verdict.Cover;
	return true;
}

void AOperativeCharacter::SetBlindFireTarget(AEnemyGhostActor* Ghost)
{
	BlindFireGhost = Ghost;
	BlindFireSource = Ghost ? Ghost->GetSource() : nullptr;
	if (Ghost)
	{
		ManualPriorityTarget.Reset();
	}
}

AEnemyGhostActor* AOperativeCharacter::GetBlindFireTarget() const
{
	return BlindFireGhost.Get();
}

bool AOperativeCharacter::EvaluateBlindLine(const AEnemyGhostActor& Ghost, FShootCandidate& Out) const
{
	UWorld* World = GetWorld();
	const FVector MyFeet = OperativeCharacterInternal::ShotFeet(this);
	const FVector GhostFeet = Ghost.GetLastKnownFeet();
	const bool bCornerShot = IsCornerShotTarget(GhostFeet);
	if (bCornerShot && !CanFireFromCover())
	{
		return false; // Sprint 12 (a silhouette out on the open side: a normal shot, user decision 2026-10-06)
	}
	if (!World || SquadFireRules::IsInDeadZone(MyFeet, GhostFeet))
	{
		return false;
	}
	const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(MyFeet.Z, GhostFeet.Z);
	const float Range = (CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f) * SquadFireRules::GetPostureRangeMultiplier(Stance)
		* (Elevation.bElevated ? Elevation.RangeMultiplier : 1.f);
	const float Distance = FVector::Dist(MyFeet + FVector(0.f, 0.f, 100.f), GhostFeet);
	if (Distance > Range)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BlindShotLine), false, this);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // fired into the suspected spot: pawns do not stop the line check
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	EShotLineHit Kind = EShotLineHit::Clear;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, bCornerShot ? GetCoverFireOrigin() : GetMuzzleLocation(), Ghost.GetAimPoint(), ECC_Visibility, Params) && Hit.GetActor())
	{
		Kind = Hit.GetActor()->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
	}
	// Suppression into cover: a barricade at the silhouette does not stop a standing / crouched shooter (JudgeLine).
	const FShotLineVerdict Verdict = SquadFireRules::JudgeLine(Kind, Stance, Elevation);
	if (!Verdict.bCanHit)
	{
		return false;
	}
	Out.Enemy = Ghost.GetSource();
	Out.Distance = Distance;
	Out.Cover = Verdict.Cover;
	Out.bBlind = true;
	Out.AimPoint = Ghost.GetAimPoint();
	return Out.Enemy != nullptr;
}

AActor* AOperativeCharacter::FindBestCombatTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FShootCandidate Candidate;
	bool bBlocked = false;
	if (AActor* Priority = ManualPriorityTarget.Get())
	{
		if (!OperativeCharacterInternal::IsLiveEnemy(Priority))
		{
			ManualPriorityTarget.Reset();
		}
		else if (EvaluateShotLine(Priority, true, Candidate, bBlocked))
		{
			return Priority;
		}
	}
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (OperativeCharacterInternal::IsLiveEnemy(*It) && EvaluateShotLine(*It, false, Candidate, bBlocked) && Candidate.Distance < BestDistance)
		{
			BestDistance = Candidate.Distance;
			Best = Candidate.Enemy;
		}
	}
	return Best;
}

bool AOperativeCharacter::CanHitEnemy(AActor* Enemy) const
{
	FShootCandidate Candidate;
	bool bBlocked = false;
	return OperativeCharacterInternal::IsLiveEnemy(Enemy) && EvaluateShotLine(Enemy, true, Candidate, bBlocked);
}

FShootCandidate AOperativeCharacter::FindShootTarget(float DeltaTime, bool bAllowAutoTargets)
{
	UWorld* World = GetWorld();
	FShootCandidate Candidate;
	bool bBlocked = false;
	// 1. The manual priority target (Ctrl + click) while it can be hit.
	if (AActor* Priority = ManualPriorityTarget.Get())
	{
		if (!OperativeCharacterInternal::IsLiveEnemy(Priority))
		{
			ManualPriorityTarget.Reset();
		}
		else if (EvaluateShotLine(Priority, true, Candidate, bBlocked))
		{
			CurrentCombatTarget = Priority;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
		}
		else if (bBlocked)
		{
			NotifyBarricadeBlocked();
		}
	}
	// 1a. Blind fire at a silhouette (Sprint 08-F); once the enemy is seen again it becomes the priority target.
	if (BlindFireSource.IsValid() || BlindFireGhost.IsValid())
	{
		AEnemyGhostActor* Ghost = BlindFireGhost.Get();
		AActor* Source = BlindFireSource.Get();
		if (!Ghost)
		{
			if (Source && OperativeCharacterInternal::IsLiveEnemy(Source))
			{
				ManualPriorityTarget = Source;
			}
			BlindFireSource.Reset();
		}
		else if (!Source || !OperativeCharacterInternal::IsLiveEnemy(Source))
		{
			BlindFireGhost.Reset();
			BlindFireSource.Reset();
		}
		else if (EvaluateBlindLine(*Ghost, Candidate))
		{
			CurrentCombatTarget = Source;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
		}
	}
	// The fire posture holds the automatic fire (user request 2026-10-06): only the direct orders above count.
	if (!bAllowAutoTargets)
	{
		CurrentCombatTarget.Reset();
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return FShootCandidate();
	}
	// 1b. Commander Mode: the target the ROE policy picked (Sprint 07-C).
	if (AActor* Chosen = AutonomyTarget.Get())
	{
		if (!OperativeCharacterInternal::IsLiveEnemy(Chosen))
		{
			AutonomyTarget.Reset();
		}
		else if (EvaluateShotLine(Chosen, true, Candidate, bBlocked))
		{
			CurrentCombatTarget = Chosen;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
		}
	}
	// 2. Visible enemies, closest first.
	TArray<FShootCandidate> Visible;
	int32 BarricadeBlocked = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!OperativeCharacterInternal::IsLiveEnemy(*It))
		{
			continue;
		}
		if (EvaluateShotLine(*It, false, Candidate, bBlocked))
		{
			Visible.Add(Candidate);
		}
		else if (bBlocked)
		{
			++BarricadeBlocked;
		}
	}
	if (Visible.IsEmpty())
	{
		CurrentCombatTarget.Reset();
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		if (Stance == EOperativeStance::Prone && BarricadeBlocked > 0)
		{
			NotifyBarricadeBlocked();
		}
		return FShootCandidate();
	}
	Visible.Sort([](const FShootCandidate& A, const FShootCandidate& B) { return A.Distance < B.Distance; });
	const FShootCandidate& Closest = Visible[0];
	const FShootCandidate* Current = OperativeCharacterInternal::IsLiveEnemy(CurrentCombatTarget.Get())
		? Visible.FindByPredicate([this](const FShootCandidate& Entry) { return Entry.Enemy == CurrentCombatTarget.Get(); }) : nullptr;
	// A melee enemy closing in on him at a cover (a flank rush while he holds the corner, user PIE video CoverBug_02
	// 2026-10-07) takes over at once. Out in the open the Godot stance reaction delay below stays (parity: SquadFireSmoke
	// "flank hound taken after 0.15 s, not at once").
	if ((bInCover || bOpenShotReturnPending) && Current && Closest.Enemy != Current->Enemy && Closest.Distance <= FlankRushBreakCm
		&& !OperativeCharacterInternal::IsRangedEnemyActor(Closest.Enemy))
	{
		CurrentCombatTarget = Closest.Enemy;
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return Closest;
	}
	if (!Current)
	{
		CurrentCombatTarget = Closest.Enemy;
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return Closest;
	}
	// A much closer enemy (flank) takes over after the stance's reaction delay.
	const int32 StanceIndex = Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	if (Closest.Enemy != CurrentCombatTarget.Get() && SquadFireRules::IsSignificantlyCloser(Closest.Distance, Current->Distance, FireConfig.SwitchRatio[StanceIndex]))
	{
		if (PendingFlankTarget.Get() != Closest.Enemy)
		{
			PendingFlankTarget = Closest.Enemy;
			TargetSwitchTimer = FireConfig.SwitchDelay[StanceIndex];
		}
		TargetSwitchTimer -= DeltaTime;
		if (TargetSwitchTimer <= 0.f)
		{
			CurrentCombatTarget = PendingFlankTarget;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Closest;
		}
	}
	else
	{
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
	}
	return *Current;
}

ESquadFirePosture AOperativeCharacter::GetFirePosture() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad ? Squad->GetEffectivePosture(this)
		: FirePostureRules::Resolve(FirePostureRules::DefaultPosture, bHasPostureOverride, PostureOverride);
}

bool AOperativeCharacter::MayAutoFireNow() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return FirePostureRules::MayAutoFire(GetFirePosture(), bProvokedThisFight, Squad && Squad->IsSquadProvoked(),
		USquadSubsystem::GetPostureConfig());
}

void AOperativeCharacter::NotifyBarricadeBlocked()
{
	if (BarricadeBlockNotifyTimer > 0.f)
	{
		return;
	}
	BarricadeBlockNotifyTimer = 3.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🚫 Barricade blocks the shot (crouch)!"), FLinearColor(1.f, 0.75f, 0.2f));
}

void AOperativeCharacter::ProcessCombatShooting(float DeltaTime)
{
	// Godot: a panicking soldier neither shoots nor reloads (can_shoot = false, _process_reloading skipped).
	if (!bRecruited || bTacticalCeaseFire || !HealthComponent || !HealthComponent->IsAlive() || (IsPanicking() && !IsRaging()))
	{
		return;
	}

	if (MisfireCooldownTimer > 0.0f)
	{
		MisfireCooldownTimer -= DeltaTime;
	}

	const bool bRaging = IsRaging();
	if (bIsReloading && !bRaging) // Godot: no reloading while raging
	{
		ReloadTimer -= DeltaTime;
		if (ReloadTimer <= 0.0f)
		{
			bIsReloading = false;
			const int32 MaxClip = CurrentWeapon ? CurrentWeapon->MaxClipSize : 30;
			const int32 Needed = MaxClip - CurrentClip;
			const int32 Added = FMath::Min(Needed, ReserveAmmo);
			CurrentClip += Added;
			ReserveAmmo -= Added;
		}
		return;
	}

	if (ShootTimer > 0.0f)
	{
		ShootTimer -= DeltaTime;
	}
	WeaponFreezeNotifyTimer = FMath::Max(0.f, WeaponFreezeNotifyTimer - DeltaTime);
	BarricadeBlockNotifyTimer = FMath::Max(0.f, BarricadeBlockNotifyTimer - DeltaTime);
	AIGrenadeCooldown = FMath::Max(0.f, AIGrenadeCooldown - DeltaTime);

	if (!CanShoot())
	{
		// Godot _can_fire: a frozen weapon says so while there is something to shoot at.
		const UGameFlowSubsystem* FrozenFlow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
		if (ColdSurvival && ColdSurvival->IsWeaponFrozen() && WeaponFreezeNotifyTimer <= 0.f && FrozenFlow
			&& FrozenFlow->GetPhase() == ECodexGamePhase::WaveCombat && FrozenFlow->GetCombatMode() == ECodexCombatMode::RealTime
			&& FindBestCombatTarget())
		{
			NotifyWeaponFrozen();
		}
		return;
	}

	// Only auto-shoot when in WaveCombat real-time mode (or if no game flow subsystem exists, e.g. standalone test).
	// Exception (user request 2026-10-06): an Aggressive operative on an ambush level still exploring opens fire on an
	// enemy it sees — a squad attack that starts the ambush fight right here.
	if (UWorld* World = GetWorld())
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat || Flow->GetCombatMode() != ECodexCombatMode::RealTime)
			{
				const ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
				if (!Encounter || bRaging || bIsReloading || ShootTimer > 0.f || (UsesAmmo() && CurrentClip <= 0)
					|| !FirePostureRules::MayAutoFireInExploration(GetFirePosture(), Encounter->IsAmbushCombatStart(), Flow->GetPhase(),
						Flow->IsCombatUnlocked(), USquadSubsystem::GetPostureConfig()))
				{
					return;
				}
				const FShootCandidate Opening = FindShootTarget(DeltaTime, true);
				if (!Opening.Enemy || Opening.bBlind
					|| !ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::SquadAutoFire, Opening.Enemy))
				{
					CurrentCombatTarget.Reset();
					return;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("%s opens fire on %s (aggressive posture): the ambush fight starts"),
					*DisplayName.ToString(), *Opening.Enemy->GetName());
				// The fight is on now (WaveCombat / RealTime): the normal fire below shoots this frame.
			}
		}
	}

	// Fire posture (user request 2026-10-06): Passive / unprovoked Defensive operatives open no fire of their own.
	// Sprint 12: Commander Mode may hold the fire behind the cover (direct orders still fire).
	const bool bAutoFire = MayAutoFireNow() && !(bInCover && bCoverHoldFire);

	// Godot: a clustered pack gets a grenade before the rifle (not while raging or reloading).
	if (bAutoFire && !bRaging && !bIsReloading && GrenadesCount > 0 && AIGrenadeCooldown <= 0.f && TryAIGrenadeThrow())
	{
		ShootTimer = 1.f;
		return;
	}

	const bool bInfiniteRageAmmo = bRaging && RageComponent->Config.bInfiniteAmmo;
	if (UsesAmmo() && CurrentClip <= 0 && !bInfiniteRageAmmo)
	{
		if (PanicComponent)
		{
			PanicComponent->OnLowAmmo(); // Godot: the clip ran dry under fire
		}
		if (ReserveAmmo > 0)
		{
			StartReload();
		}
		else
		{
			AutoSwitchOnEmpty();
		}
		return;
	}

	// Raging: a random enemy in reach is sprayed (Godot rage_comp.get_chaotic_target, cover 1).
	FShootCandidate Shot;
	if (AActor* Chaotic = bRaging ? RageComponent->GetChaoticTarget() : nullptr)
	{
		Shot.Enemy = Chaotic;
	}
	else
	{
		Shot = FindShootTarget(DeltaTime, bAutoFire);
	}
	AActor* Target = Shot.Enemy;
	if (!Target)
	{
		return;
	}

	// Sniper rifle (user request 2026-10-09, SniperRules): only kneeling / prone and still. A direct order (priority target,
	// blind fire at a silhouette) stops her and she kneels by herself; automatic fire waits while she walks under a move order.
	ESniperFireStep SniperStep = ESniperFireStep::Ready;
	if (IsSniperWeaponEquipped())
	{
		SniperStep = PrepareSniperShot(Target == ManualPriorityTarget.Get() || Shot.bBlind);
		if (SniperStep == ESniperFireStep::WaitForMove)
		{
			return;
		}
	}

	// User decision 2026-10-06: a target out on the open side (in front of the wall) is no corner shot: he steps off the
	// wall and fires normally, then comes back to the slot (UpdateOpenShotReturn).
	if (bInCover && !IsCornerShotTarget(Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation()) && !BeginOpenShotFromCover(Target))
	{
		return; // shimmying: the shot waits for the slot
	}

	// Turn towards target smoothly (the barrel onto it, see UpdateCombatFacing); in cover the back stays to the wall.
	const FVector AimAt = Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation();
	// The arms throw a grenade: no rifle shot until the throw clip is done (CornerHoldSmoke 2026-10-07 caught shots from
	// the throw pose, the barrel 100+ deg off the target).
	if (const UOperativeAnimInstance* ArmsAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		ArmsAnim && ArmsAnim->IsThrowingGrenade())
	{
		return;
	}
	if (!bInCover)
	{
		const FRotator LookRot(0.f, (AimAt - GetActorLocation()).Rotation().Yaw - BarrelYawOffset, 0.f);
		// Eased, but never slower than AimTurnRateDegPerSec (a quick, visible turn to a flank rush).
		const FRotator Eased = FMath::RInterpTo(GetActorRotation(), LookRot, DeltaTime, 12.0f);
		const FRotator Constant = FMath::RInterpConstantTo(GetActorRotation(), LookRot, DeltaTime, AimTurnRateDegPerSec);
		const float EasedLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Eased.Yaw));
		const float ConstantLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Constant.Yaw));
		SetActorRotation(EasedLeft <= ConstantLeft ? Eased : Constant);
		// User rule 2026-10-07: no shot where he does not aim - first the turn, then the shot.
		if (GetShotAimResidualDeg(AimAt) > AimConeDeg)
		{
			return;
		}
	}

	if (bInCover)
	{
		// User design rule 2026-10-06: the target sets the threat side; he turns along the wall to it before the corner
		// shot (lean / pop up / blind) — never a plain shot turned away from the wall.
		CoverShotTarget = Target;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		if (!CoverFacingRules::IsFacingAligned(GetActorRotation().Yaw, CoverSlot, CoverFacing, FCoverFacingConfig().FacingToleranceDeg))
		{
			return;
		}
		// Sustained corner aim (user request 2026-10-07): a corner-shot target in sight keeps him leaned out; after a duck
		// for safety he stays behind the corner for the re-entry delay.
		const UWorld* AimWorld = GetWorld();
		const double Now = AimWorld ? AimWorld->GetTimeSeconds() : 0.0;
		if (bCornerAimActive)
		{
			CornerAimLastTargetTime = Now;
		}
		else if (Now < CornerAimDuckUntil)
		{
			return;
		}
		// Arriving at an edge: no shot until the snap to the corner stand-off is decided / done (CornerEntrySmoke 2026-10-07:
		// a shot fired from the wall while side-stepping to the corner, the rifle not on the target).
		if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && (bCoverSnapPending || (bShimmying && bCoverAutoSnap)))
		{
			return;
		}
		// The corner hold stance fires only once the upper body twisted onto the target (user rule 2026-10-07).
		if (IsCornerHoldSpot() && (!bCornerAimActive || GetShotAimResidualDeg(AimAt) > AimConeDeg))
		{
			return;
		}
	}
	if (ShootTimer <= 0.0f && MisfireCooldownTimer <= 0.0f && SniperStep == ESniperFireStep::Ready)
	{
		if (bInCover)
		{
			BeginCoverShot(); // Sprint 12: lean out, or keep the head down for a blind shot
		}
		TGuardValue<bool> Blind(bBlindShot, Shot.bBlind);
		TGuardValue<FVector> Aim(BlindAimPoint, Shot.AimPoint);
		TGuardValue<bool> CoverBlind(bCoverBlindShot, bInCover && CoverFireMode == ECoverFireMode::BlindFire);
		ShootAtTarget(Target, Shot.Cover);
	}
}

bool AOperativeCharacter::ShootAtTarget(AActor* Target, float Cover)
{
	if (!Target || (UsesAmmo() && CurrentClip <= 0) || !SniperShotAllowed())
	{
		return false;
	}

	const bool bRagingShot = IsRaging();
	if (UsesAmmo() && !(bRagingShot && RageComponent->Config.bInfiniteAmmo))
	{
		CurrentClip--;
	}

	// Cold misfire (Godot player.gd `_shoot_at_target`, balance.tres: >= 60 %, linear up to 30 %, never near heat).
	bool bMisfire = bForceMisfireForTesting;
	if (!bMisfire && ColdSurvival && FMath::FRand() < ColdSurvival->GetMisfireChance())
	{
		bMisfire = true;
	}

	if (bMisfire)
	{
		MisfireCooldownTimer = ColdSurvival ? ColdSurvival->Config.MisfireDelay : 0.45f;
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("❄️ MISFIRE! (Bolt jammed)"), FLinearColor(0.4f, 0.85f, 1.f));
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return false;
	}

	ShootTimer = FMath::Max(0.08f, (CurrentWeapon ? CurrentWeapon->FireRate : 0.65f) * (bRagingShot ? RageComponent->Config.FireRateMultiplier : 1.f));
	// Sprint 08-E: the muzzle flash gives the shooter away for 2 s.
	if (UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr)
	{
		Sight->NotifyFired(this);
	}
	// Patrols hear the shot (enemy_perception.json hear_gunshot_m) and engage (user request 2026-10-06).
	AEnemyCharacter::NotifySquadNoise(GetWorld(), GetActorLocation(), ESquadNoise::Gunshot);

	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	const float DistM = Dist / 100.0f;
	const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));

	float HitChance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;
	// Shivering hands above 50 % cold (Godot realtime_cold_aim_penalty_max 0.30).
	if (ColdSurvival && ColdLevel > 50.0f)
	{
		HitChance = FMath::Clamp(HitChance - ColdSurvival->GetAimPenalty(), 0.05f, 0.99f);
	}
	// Blind fire at a silhouette: -80 %, and nothing to hit once the enemy left that spot (Sprint 08-F). Cover blind fire
	// (Sprint 12, head down): -40 %. Both apply multiplied, floored at the tunable minimum (user decision 2026-10-06).
	if (bBlindShot && FVector::Dist2D(OperativeCharacterInternal::ShotFeet(Target), BlindAimPoint) > SightRules::BlindFireHitRadiusCm)
	{
		HitChance = 0.f; // the enemy left the silhouette's spot
	}
	else if (bBlindShot || bCoverBlindShot)
	{
		HitChance = CoverRules::CombinedBlindFireHitChance(CoverRules::GetConfig(), HitChance, bCoverBlindShot, bBlindShot,
			SightRules::BlindFireAccuracyMultiplier);
	}
	if (bInCover)
	{
		(bCoverBlindShot ? CoverBlindShots : CoverLeanShots) += 1;
	}
	else if (bOpenShotReturnPending)
	{
		++CoverOpenShots;
		OpenShotLastTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}

	const bool bHit = bForceHitForTesting || (FMath::FRand() <= HitChance);
	if (!bHit)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("MISS!"), FLinearColor(0.75f, 0.75f, 0.75f));
	}
	NoteShotAim(Target, bBlindShot ? BlindAimPoint : Target->GetActorLocation(), bBlindShot);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);

	// Godot _spawn_muzzle_tracer: to the target, or deflected next to it on a miss.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		FVector End = bBlindShot ? BlindAimPoint : Target->GetActorLocation();
		if (!bHit)
		{
			FVector Offset(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(20.f, 120.f));
			if (Offset.SizeSquared() < 50.f * 50.f)
			{
				Offset = FVector(100.f, 0.f, 50.f);
			}
			End += Offset;
		}
		Feedback->SpawnTracer(GetWeaponMuzzleLocation(), End, CurrentWeapon ? CurrentWeapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
			CurrentWeapon ? CurrentWeapon->DamageType : EDamageType::Kinetic);
	}

	if (bHit)
	{
		// Godot _shoot_at_target: crit on luck (x2), elevation (+15 %), stance, cover and distance factors.
		const float CritRoll = ForcedCritRollForTesting >= 0.f ? (ForcedCritRollForTesting >= 1.f ? 0.f : 1.f) : FMath::FRand();
		ForcedCritRollForTesting = -1.f;
		const bool bCrit = SquadFireRules::IsCrit(Luck + (bRagingShot ? 30.f : 0.f), CritRoll); // rage: +30 luck
		const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(
			GetActorLocation().Z - GetSimpleCollisionHalfHeight(), Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight());
		float DistanceMultiplier = 1.f;
		if (CurrentWeapon && !CurrentWeapon->DistanceDamageMultipliers.IsEmpty())
		{
			DistanceMultiplier = CurrentWeapon->DistanceDamageMultipliers[FMath::Clamp(DistCells - 1, 0, CurrentWeapon->DistanceDamageMultipliers.Num() - 1)];
		}
		if (bCrit)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🎯 CRIT x2!"), FLinearColor(1.f, 0.85f, 0.1f));
		}
		else if (Elevation.bElevated)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⛰️ +15% HEIGHT"), FLinearColor(0.35f, 0.9f, 1.f));
		}

		FDamageSpec Spec;
		Spec.Amount = SquadFireRules::ComputeShotDamage(CurrentWeapon ? CurrentWeapon->BaseDamage : 18.0f, Stance, Cover, bCrit, Elevation, DistanceMultiplier)
			* (bRagingShot ? RageComponent->Config.DamageMultiplier : 1.f);
		Spec.bIsCritical = bCrit;
		Spec.bMelee = !UsesAmmo(); // the knife
		Spec.DamageType = CurrentWeapon ? CurrentWeapon->DamageType : EDamageType::Kinetic;
		Spec.ArmorPenetration = CurrentWeapon ? CurrentWeapon->ArmorPenetration : 0.20f;
		Spec.AttackerSource = DisplayName.ToString();
		if (CurrentWeapon)
		{
			Spec.StatusEffect = CurrentWeapon->StatusEffect;
			Spec.StatusDuration = CurrentWeapon->StatusDuration;
			Spec.StatusTickDamage = CurrentWeapon->StatusTickDamage;
		}

		if (UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>())
		{
			TargetHealth->TakeDamage(Spec);
		}
		// Run telemetry (Godot _record_weapon_shot: hits with their damage, per wave and weapon).
		if (URunTelemetrySubsystem* Telemetry = GetWorld() ? GetWorld()->GetSubsystem<URunTelemetrySubsystem>() : nullptr)
		{
			Telemetry->RecordWeaponHit(this, CurrentWeapon ? CurrentWeapon->WeaponId : FString(TEXT("m16")), Spec.Amount);
		}
		// Cryo weapons chill the shooter, fire weapons warm him (Godot self_cold / self_warmth_generation).
		if (CurrentWeapon && CurrentWeapon->SelfColdGeneration > 0.f)
		{
			ColdLevel = FMath::Min(100.f, ColdLevel + CurrentWeapon->SelfColdGeneration);
		}
		if (CurrentWeapon && CurrentWeapon->SelfWarmthGeneration > 0.f)
		{
			ColdLevel = FMath::Max(0.f, ColdLevel - CurrentWeapon->SelfWarmthGeneration);
		}
	}

	return bHit;
}
