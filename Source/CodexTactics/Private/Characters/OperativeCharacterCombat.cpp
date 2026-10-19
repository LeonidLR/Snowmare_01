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
#include "Combat/EncounterQueries.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/TurretActor.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Telemetry/RunTelemetrySubsystem.h"
#include "UI/FloatingTextSubsystem.h"

namespace OperativeFireTrace
{
	static TAutoConsoleVariable<int32> CVarFireTrace(TEXT("Codex.Debug.FireTrace"), 0,
		TEXT("Dev trace (2026-10-18): 1 logs every operative's real-time fire decision when its reason changes, and every 2 s while an enemy is within 5 m and no shot leaves"));
}

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

bool AOperativeCharacter::EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked,
	const TCHAR** OutReason) const
{
	bOutBarricadeBlocked = false;
	const TCHAR* Unused = nullptr;
	const TCHAR*& Reason = OutReason ? *OutReason : Unused;
	Reason = TEXT("ok");
	UWorld* World = GetWorld();
	// Sprint 08: an enemy the squad does not see is not aimed at (blind fire at its silhouette is an order).
	if (const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
		Sight && Enemy && !Sight->IsVisibleToSquad(Enemy))
	{
		Reason = TEXT("not visible to the squad");
		return false;
	}
	const FVector MyFeet = OperativeCharacterInternal::ShotFeet(this);
	const FVector EnemyFeet = OperativeCharacterInternal::ShotFeet(Enemy);
	// Sprint 12: nothing to fire round / over from this cover (a full wall without a corner). User decision 2026-10-06:
	// only a target behind the wall / around the corner needs that; one out on the open side gets a normal shot.
	const bool bCornerShot = IsCornerShotTarget(EnemyFeet);
	if (bCornerShot && !CanFireFromCover())
	{
		Reason = TEXT("corner shot impossible from this cover");
		return false;
	}
	if (!World || SquadFireRules::IsInDeadZone(MyFeet, EnemyFeet))
	{
		Reason = TEXT("dead zone");
		return false;
	}
	const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(MyFeet.Z, EnemyFeet.Z);
	const float Range = (CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f) * SquadFireRules::GetPostureRangeMultiplier(Stance)
		* (Elevation.bElevated ? Elevation.RangeMultiplier : 1.f);
	// Godot: from the operative's centre (1 m above the feet) to the enemy's feet.
	const float Distance = FVector::Dist(MyFeet + FVector(0.f, 0.f, 100.f), EnemyFeet);
	if (Distance > Range)
	{
		Reason = TEXT("out of range");
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadShotLine), false, this);
	OperativeCharacterInternal::AddShotLineAllies(World, Params); // the squad + the turrets (Godot "allies"), gathered once per frame
	EShotLineHit Kind = EShotLineHit::Clear;
	AActor* HitEnemy = Enemy;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, bCornerShot ? GetCoverFireOrigin() : GetMuzzleLocation(), EnemyFeet + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params))
	{
		AActor* Blocker = Hit.GetActor();
		if (Blocker && Blocker != Enemy)
		{
			if (Blocker->ActorHasTag(CombatQueries::EnemyTag))
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
		Reason = Kind == EShotLineHit::Barricade ? TEXT("line blocked by a barricade") : TEXT("line blocked");
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
	for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		if (OperativeCharacterInternal::IsLiveEnemy(Enemy) && EvaluateShotLine(Enemy, false, Candidate, bBlocked) && Candidate.Distance < BestDistance)
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
	// 1b. Commander Mode: the target the ROE policy picked (Sprint 07-C) - if the posture lets him fire at it on his own.
	if (AActor* Chosen = AutonomyTarget.Get())
	{
		if (!OperativeCharacterInternal::IsLiveEnemy(Chosen))
		{
			AutonomyTarget.Reset();
		}
		else if (MayAutoFireAtEnemy(Chosen) && EvaluateShotLine(Chosen, true, Candidate, bBlocked))
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
	for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		// User decision 2026-10-19: a Defensive operative leaves the enemies that have not detected the squad alone.
		if (!OperativeCharacterInternal::IsLiveEnemy(Enemy) || !MayAutoFireAtEnemy(Enemy))
		{
			continue;
		}
		if (EvaluateShotLine(Enemy, false, Candidate, bBlocked))
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

bool AOperativeCharacter::MayAutoFireAtEnemy(const AActor* Enemy) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AEnemyCharacter* Character = Cast<AEnemyCharacter>(Enemy);
	// Only a character can be "aware" of the squad; any other enemy actor (a turret-like target) counts as aware.
	const bool bDetected = !Character || Character->IsHuntingSquad();
	return FirePostureRules::MayAutoFireAt(GetFirePosture(), bProvokedThisFight, Squad && Squad->IsSquadProvoked(), bDetected,
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
	AActor* Target = nullptr;
	const TCHAR* Reason = RunCombatShooting(DeltaTime, Target);
	TraceFireDecision(Reason, Target);
}

const TCHAR* AOperativeCharacter::RunCombatShooting(float DeltaTime, AActor*& OutTarget)
{
	OutTarget = nullptr;
	// Godot: a panicking soldier neither shoots nor reloads (can_shoot = false, _process_reloading skipped).
	if (!bRecruited || bTacticalCeaseFire || !HealthComponent || !HealthComponent->IsAlive() || (IsPanicking() && !IsRaging()))
	{
		return bTacticalCeaseFire ? TEXT("cease fire") : (IsPanicking() ? TEXT("panicking") : TEXT("not recruited / dead"));
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
		return TEXT("reloading");
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
		return IsKnockedDown() ? TEXT("knocked down") : (bSprinting ? TEXT("sprinting") : (bCarrying ? TEXT("carrying")
			: TEXT("cannot shoot (frozen weapon)")));
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
				// Fix 2026-10-19 (game designer: "they see each other, the AGGRESSIVE squad never shoots"): the gate asked for an
				// AMBUSH level only, while contact with an enemy placed in the map starts the fight on a «button» level too (the
				// Demo 01 lake: combat_start "button" + placed patrols) - the encounter system decides per enemy below.
				const bool bContactStartsFight = Encounter && (Encounter->IsAmbushCombatStart() || Encounter->GetNumLevelEnemies() > 0);
				if (!Encounter || bRaging || bIsReloading || ShootTimer > 0.f || (UsesAmmo() && CurrentClip <= 0)
					|| !FirePostureRules::MayAutoFireInExploration(GetFirePosture(), bContactStartsFight, Flow->GetPhase(),
						Flow->IsCombatUnlocked(), USquadSubsystem::GetPostureConfig()))
				{
					return TEXT("not a real-time fight");
				}
				const FShootCandidate Opening = FindShootTarget(DeltaTime, true);
				if (!Opening.Enemy || Opening.bBlind
					|| !ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::SquadAutoFire, Opening.Enemy))
				{
					CurrentCombatTarget.Reset();
					return TEXT("exploring: no opening shot");
				}
				UE_LOG(LogCodexTactics, Display, TEXT("%s opens fire on %s (aggressive posture): the ambush fight starts"),
					*DisplayName.ToString(), *Opening.Enemy->GetName());
				// The fight is on now (WaveCombat / RealTime): the normal fire below shoots this frame.
			}
		}
	}

	// Fire posture (user request 2026-10-06): Passive operatives open no fire of their own; an unprovoked Defensive one only at the
	// enemies that have detected the squad (FindShootTarget filters them, 2026-10-19) and throws no grenade of its own.
	// Sprint 12: Commander Mode may hold the fire behind the cover (direct orders still fire).
	const bool bCoverHold = bInCover && bCoverHoldFire;
	const bool bAutoFire = MayAutoFireNow() && !bCoverHold;
	const bool bAutoTargets = FirePostureRules::AllowsAutoTargets(GetFirePosture()) && !bCoverHold;

	// Godot: a clustered pack gets a grenade before the rifle (not while raging or reloading).
	if (bAutoFire && !bRaging && !bIsReloading && GrenadesCount > 0 && AIGrenadeCooldown <= 0.f && TryAIGrenadeThrow())
	{
		ShootTimer = 1.f;
		return TEXT("threw a grenade");
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
		return TEXT("empty clip");
	}

	// Raging: a random enemy in reach is sprayed (Godot rage_comp.get_chaotic_target, cover 1).
	FShootCandidate Shot;
	if (AActor* Chaotic = bRaging ? RageComponent->GetChaoticTarget() : nullptr)
	{
		Shot.Enemy = Chaotic;
	}
	else
	{
		Shot = FindShootTarget(DeltaTime, bAutoTargets);
	}
	AActor* Target = Shot.Enemy;
	OutTarget = Target;
	if (!Target)
	{
		return bAutoFire ? TEXT("no target") : (bAutoTargets ? TEXT("no target (defensive: none has detected the squad)")
			: TEXT("no target (auto fire held by the posture)"));
	}

	// Sniper rifle (user request 2026-10-09, SniperRules): only kneeling / prone and still. A direct order (priority target,
	// blind fire at a silhouette) stops her and she kneels by herself; automatic fire waits while she walks under a move order.
	ESniperFireStep SniperStep = ESniperFireStep::Ready;
	if (IsSniperWeaponEquipped())
	{
		SniperStep = PrepareSniperShot(Target == ManualPriorityTarget.Get() || Shot.bBlind);
		if (SniperStep == ESniperFireStep::WaitForMove)
		{
			return TEXT("sniper: waits for the move");
		}
	}

	// User decision 2026-10-06: a target out on the open side (in front of the wall) is no corner shot: he steps off the
	// wall and fires normally, then comes back to the slot (UpdateOpenShotReturn).
	if (bInCover && !IsCornerShotTarget(Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation()) && !BeginOpenShotFromCover(Target))
	{
		return TEXT("open shot waits (shimmying)");
	}

	// Turn towards target smoothly (the barrel onto it, see UpdateCombatFacing); in cover the back stays to the wall.
	const FVector AimAt = Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation();
	// The arms throw a grenade: no rifle shot until the throw clip is done (CornerHoldSmoke 2026-10-07 caught shots from
	// the throw pose, the barrel 100+ deg off the target).
	if (const UOperativeAnimInstance* ArmsAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		ArmsAnim && ArmsAnim->IsThrowingGrenade())
	{
		return TEXT("throwing a grenade");
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
			return TEXT("open stance: turning, outside the aim cone");
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
			return TEXT("cover: facing not aligned");
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
			return TEXT("cover: re-entry delay after a duck");
		}
		// Arriving at an edge: no shot until the snap to the corner stand-off is decided / done (CornerEntrySmoke 2026-10-07:
		// a shot fired from the wall while side-stepping to the corner, the rifle not on the target).
		if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && (bCoverSnapPending || (bShimmying && bCoverAutoSnap)))
		{
			return TEXT("cover: corner snap pending");
		}
		// The corner hold stance fires only once the upper body twisted onto the target (user rule 2026-10-07).
		if (IsCornerHoldSpot() && (!bCornerAimActive || GetShotAimResidualDeg(AimAt) > AimConeDeg))
		{
			return bCornerAimActive ? TEXT("corner hold: outside the aim cone") : TEXT("corner hold: aim not active");
		}
	}
	if (ShootTimer > 0.0f || MisfireCooldownTimer > 0.0f || SniperStep != ESniperFireStep::Ready)
	{
		return TEXT("cooldown");
	}
	if (bInCover)
	{
		BeginCoverShot(); // Sprint 12: lean out, or keep the head down for a blind shot
	}
	TGuardValue<bool> Blind(bBlindShot, Shot.bBlind);
	TGuardValue<FVector> Aim(BlindAimPoint, Shot.AimPoint);
	TGuardValue<bool> CoverBlind(bCoverBlindShot, bInCover && CoverFireMode == ECoverFireMode::BlindFire);
	ShootAtTarget(Target, Shot.Cover);
	return TEXT("fired");
}

void AOperativeCharacter::TraceFireDecision(const TCHAR* Reason, const AActor* Target)
{
	UWorld* World = GetWorld();
	if (!World || OperativeFireTrace::CVarFireTrace.GetValueOnGameThread() == 0 || bTacticalCeaseFire)
	{
		return;
	}
	const bool bFired = FCString::Strcmp(Reason, TEXT("fired")) == 0;
	const bool bCooldown = FCString::Strcmp(Reason, TEXT("cooldown")) == 0;
	const double Now = World->GetTimeSeconds();
	// Fired <-> cooldown is the normal rhythm of a burst: not a change worth a line.
	const bool bRhythm = (bCooldown && LastFireTraceReason == TEXT("fired")) || (bFired && LastFireTraceReason == TEXT("cooldown"));
	const bool bChanged = LastFireTraceReason != Reason && !bRhythm;
	if (!bChanged && (bFired || bCooldown || Now - LastFireTraceTime < 2.0))
	{
		return;
	}
	// The nearest live enemies and why each one is (not) a shot.
	TArray<TPair<float, AActor*>> Near;
	for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
		if (IsValid(Enemy) && (!Health || Health->IsAlive()))
		{
			Near.Emplace(static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), GetActorLocation())), Enemy);
		}
	}
	Near.Sort([](const TPair<float, AActor*>& A, const TPair<float, AActor*>& B) { return A.Key < B.Key; });
	// Unchanged: a line every 2 s only while no shot leaves with an enemy within 5 m.
	if (!bChanged && (Near.IsEmpty() || Near[0].Key > 500.f))
	{
		return;
	}
	LastFireTraceReason = Reason;
	LastFireTraceTime = Now;
	FString Enemies;
	for (int32 Index = 0; Index < FMath::Min(3, Near.Num()); ++Index)
	{
		AActor* Enemy = Near[Index].Value;
		FShootCandidate Candidate;
		bool bBlocked = false;
		const TCHAR* LineReason = TEXT("");
		EvaluateShotLine(Enemy, true, Candidate, bBlocked, &LineReason);
		Enemies += FString::Printf(TEXT(" | %s %.0f cm: %s%s, residual %.0f deg%s"), *Enemy->GetName(), Near[Index].Key, LineReason,
			Enemy->IsHidden() ? TEXT(" (hidden)") : TEXT(""), GetShotAimResidualDeg(Enemy->GetActorLocation()),
			IsCornerShotTarget(Enemy->GetActorLocation()) ? TEXT(" corner") : TEXT(""));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FireTrace] %s: %s, target %s | %s, stance %d, clip %d, shoot timer %.2f, return pending %d%s"),
		*DisplayName.ToString(), Reason, Target ? *Target->GetName() : TEXT("-"),
		bInCover ? (bCornerAimActive ? TEXT("cover (corner aim)") : TEXT("cover")) : TEXT("open"), static_cast<int32>(Stance),
		CurrentClip, ShootTimer, bOpenShotReturnPending ? 1 : 0, *Enemies);
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
