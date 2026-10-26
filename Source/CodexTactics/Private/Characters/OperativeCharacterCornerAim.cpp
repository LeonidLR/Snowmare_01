// AOperativeCharacter: Corner hold, sustained corner aim, manual corner peek, corner snap, open shots off the wall.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// UE-only (user decisions 2026-10-06 / 2026-10-07, rev 5c-5e corner peek; no Godot reference). Pure rules:
// Tactics/CoverDecisionRules (DecideCornerAim), CoverFacingRules.

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Combat/SuppressionSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TrapSpotRules.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"

void AOperativeCharacter::BeginCornerAim()
{
	if (!bInCover || CurrentCoverHeight == ECoverHeight::None || bShimmying)
	{
		return; // the high wall's corner, or over the top of a low cover (user request 2026-10-07: crouched too)
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CornerAimLastTargetTime = Now;
	if (!bCornerAimActive)
	{
		bCornerAimActive = true;
		bIsCornerLeaning = true; // leaned out (exposed) from the first frame of the aim, not one update later
		LeanTimer = FMath::Max(LeanTimer, 0.2f);
		CornerAimEvalTimer = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: leans out round the corner and holds the fire stance"), *DisplayName.ToString());
	}
}

void AOperativeCharacter::EndCornerAim(ECornerAimDecision Reason, const TCHAR* Why)
{
	if (!bCornerAimActive)
	{
		return;
	}
	bCornerAimActive = false;
	bIsCornerLeaning = false; // back behind the corner
	LeanTimer = 0.f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CornerAimEndTime = Now;
	if (Reason == ECornerAimDecision::DuckForSafety)
	{
		CornerAimDuckUntil = Now + GetCoverDecisionConfig().AimReentryDelaySeconds;
	}
	LastCornerAimBreak = Reason;
	++CornerAimBreakCounts[static_cast<int32>(Reason)];
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: breaks the corner aim: %s (%s)"), *DisplayName.ToString(),
		CoverDecisionRules::CornerAimDecisionName(Reason), Why);
	if (Reason == ECornerAimDecision::DuckToReload && !bIsReloading && UsesAmmo() && ReserveAmmo > 0
		&& CurrentClip < (CurrentWeapon ? CurrentWeapon->MaxClipSize : 30))
	{
		StartReload(); // reload behind the corner (the AnimInstance plays the fire -> idle exit, then the reload)
	}
}

FCornerAimSituation AOperativeCharacter::GatherCornerAimSituation() const
{
	const FCoverDecisionConfig Config = GetCoverDecisionConfig();
	FCornerAimSituation Situation;
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const int32 MaxClip = FMath::Max(1, CurrentWeapon ? CurrentWeapon->MaxClipSize : 30);
	Situation.ClipFraction = UsesAmmo() ? static_cast<float>(CurrentClip) / MaxClip : 1.f;
	Situation.bHasReserve = !UsesAmmo() || ReserveAmmo > 0;
	Situation.SecondsWithoutTarget = static_cast<float>(FMath::Max(0.0, Now - CornerAimLastTargetTime));
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	Situation.bHoldWithoutTargets = TurnBased && TurnBased->IsActive(); // in his turn the pose stays between his shots
	Situation.HealthFraction = HealthComponent ? HealthComponent->GetHealthFraction() : 1.f;
	Situation.RecentIncomingDamage = RecentRangedDamage;
	Situation.bRangedThreatPresent = false;
	CornerAimTrace = FCornerAimTrace();
	CornerAimTrace.AllDamage = RecentIncomingDamage;
	if (!World)
	{
		return Situation;
	}
	const FVector Here = GetActorLocation();
	int32 Shooters = 0;
	for (AEnemyCharacter* Registered : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		const AEnemyCharacter* Enemy = Registered;
		if (Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive())
		{
			continue;
		}
		const bool bRanged = OperativeCharacterInternal::IsRangedEnemyActor(Enemy);
		const float Distance = static_cast<float>(FVector::Dist2D(Here, Enemy->GetActorLocation()));
		const bool bOpenSide = !IsCornerShotTarget(Enemy->GetActorLocation());
		if (Enemy->GetCurrentTarget() == this && Distance <= 3000.f)
		{
			++CornerAimTrace.AllAimers; // the old rule counted every enemy targeting him, melee rushers included
			Shooters += bRanged ? 1 : 0;
		}
		if (bRanged && !Enemy->IsHidden() && Distance <= 3000.f)
		{
			Situation.bRangedThreatPresent = true;
		}
		if (!bRanged && Distance <= 500.f)
		{
			++Situation.MeleeRushers;
		}
		if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Enemy);
			Marksman && Marksman->IsAimingAtTarget() && Marksman->GetCurrentTarget() == this)
		{
			Situation.bSniperLaserOnMe = true;
		}
		// An enemy on his open side (in front of the wall) close by: the wall does not shield him from it. Only a RANGED
		// one is a reason to duck (2026-10-07 horde fix: melee rushers coming round the corner are the targets).
		if (!Enemy->IsHidden() && Distance <= Config.AimFlankDangerCm && bOpenSide)
		{
			CornerAimTrace.bAnyOpenSideNear = true;
			CornerAimTrace.NearestOpenSideCm = FMath::Min(CornerAimTrace.NearestOpenSideCm, Distance);
			Situation.bFlankEnemyNear |= bRanged;
		}
	}
	Situation.RangedAimers = Shooters;
	Situation.SuppressionPressure = ForcedCornerAimSuppressionForTesting >= 0.f ? ForcedCornerAimSuppressionForTesting
		: CoverDecisionRules::SuppressionFromShooters(Config, Shooters);
	CornerAimTrace.AllPressure = CoverDecisionRules::SuppressionFromShooters(Config, CornerAimTrace.AllAimers);
	for (TActorIterator<AGrenadeActor> It(World); It; ++It)
	{
		// 2026-10-07: only an ENEMY grenade is a reason to duck. The squad's own grenades (all AGrenadeActor today: only
		// operatives throw) sit in his hand before the release (distance 0) or fly at the horde — they made him duck at
		// once. A friendly grenade that has LANDED next to him counts only with bCornerAimDuckForFriendlyGrenades.
		const bool bFriendly = It->GetThrower() != nullptr;
		if (bFriendly && !(bCornerAimDuckForFriendlyGrenades && It->HasLanded()))
		{
			continue;
		}
		const float GrenadeDistance = static_cast<float>(FVector::Dist2D(Here, It->GetActorLocation()));
		CornerAimTrace.NearestGrenadeCm = FMath::Min(CornerAimTrace.NearestGrenadeCm, GrenadeDistance);
		if (GrenadeDistance <= Config.AimGrenadeDangerCm)
		{
			Situation.bGrenadeNearby = true;
		}
	}
	return Situation;
}

void AOperativeCharacter::UpdateCornerAim(float DeltaTime)
{
	if (!bCornerAimActive)
	{
		return;
	}
	if (!bInCover || bShimmying || CurrentCoverHeight == ECoverHeight::None)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("no longer at the corner"));
		return;
	}
	if (bIsReloading)
	{
		EndCornerAim(ECornerAimDecision::DuckToReload, TEXT("reloading"));
		return;
	}
	// Suppressive fire (game designer decision 2026-10-26): suppressed / pinned he keeps his head down - no peeking.
	if (!USuppressionSubsystem::MayExposeNow(this))
	{
		EndCornerAim(ECornerAimDecision::DuckForSafety, TEXT("suppressed: head down"));
		return;
	}
	if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && !IsCornerHoldSpot() && !IsManualCornerPeekSpot())
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("no longer at the exposed edge"));
		return;
	}
	// The hold outside a fight lasts while a threat is known (or he fired lately): then back to hugging the wall.
	if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && !WantsCornerHold() && GetWorld()
		&& GetWorld()->GetTimeSeconds() - CoverLastShotTime > CoverEngageHoldSeconds)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("no fight, no threat known"));
		return;
	}
	// Leaned out (sight: exposed) for as long as the aim lasts.
	bIsCornerLeaning = true;
	LeanTimer = FMath::Max(LeanTimer, 0.2f);
	CornerAimEvalTimer -= DeltaTime;
	if (CornerAimEvalTimer > 0.f)
	{
		return;
	}
	CornerAimEvalTimer = 0.1f;
	const FCoverDecisionConfig Config = GetCoverDecisionConfig();
	const FCornerAimSituation Situation = GatherCornerAimSituation();
	const TCHAR* Reason = TEXT("");
	const ECornerAimDecision Decision = CoverDecisionRules::DecideCornerAim(Config, Situation, &Reason);
	// What the pre-fix rule (melee counted as suppression / flank, every hit counted) would have decided â€” the trace shows
	// the horde regression's cause next to the fixed decision.
	FCornerAimSituation Legacy = Situation;
	Legacy.SuppressionPressure = CornerAimTrace.AllPressure;
	Legacy.bFlankEnemyNear = CornerAimTrace.bAnyOpenSideNear;
	Legacy.RecentIncomingDamage = CornerAimTrace.AllDamage;
	Legacy.bRangedThreatPresent = true;
	const TCHAR* LegacyReason = TEXT("");
	const ECornerAimDecision LegacyDecision = CoverDecisionRules::DecideCornerAim(Config, Legacy, &LegacyReason);
	CornerAimLabel = FString::Printf(TEXT("%s: %s | legacy %s: %s | hp %.0f%% rngDmg %.0f allDmg %.0f | press %.2f (rng aimers %d, all %d) | melee<5m %d | laser %d | grenade %.0f | openSide %.0f | noTarget %.1fs | clip %.0f%%"),
		CoverDecisionRules::CornerAimDecisionName(Decision), Reason, CoverDecisionRules::CornerAimDecisionName(LegacyDecision), LegacyReason,
		Situation.HealthFraction * 100.f, Situation.RecentIncomingDamage, CornerAimTrace.AllDamage, Situation.SuppressionPressure,
		Situation.RangedAimers, CornerAimTrace.AllAimers, Situation.MeleeRushers, Situation.bSniperLaserOnMe ? 1 : 0,
		CornerAimTrace.NearestGrenadeCm < 1.0e8f ? CornerAimTrace.NearestGrenadeCm : -1.f,
		CornerAimTrace.NearestOpenSideCm < 1.0e8f ? CornerAimTrace.NearestOpenSideCm : -1.f, Situation.SecondsWithoutTarget,
		Situation.ClipFraction * 100.f);
	if (Decision != LastCornerAimTraceDecision || LegacyDecision != LastCornerAimLegacyDecision || FCString::Strcmp(Reason, *LastCornerAimTraceReason) != 0)
	{
		LastCornerAimTraceDecision = Decision;
		LastCornerAimLegacyDecision = LegacyDecision;
		LastCornerAimTraceReason = Reason;
		UE_LOG(LogCodexTactics, Display, TEXT("[CornerAim] %s: %s"), *DisplayName.ToString(), *CornerAimLabel);
	}
	// A manual corner peek holds with no target at all (only the safety / reload breaks of the legacy mode apply to it).
	if (Decision != ECornerAimDecision::StayAndFire && bCornerAutoDuck
		&& !(Decision == ECornerAimDecision::ReturnNoTargets && IsManualCornerPeekSpot()))
	{
		EndCornerAim(Decision, Reason);
	}
}

void AOperativeCharacter::TrySnapToCoverCorner()
{
	bCoverSnapPending = false;
	if (!bInCover || bShimmying || IsRaging() || IsPanicking() || bManualCornerPeek)
	{
		return; // (a manual corner peek walks to its own corner)
	}
	if (bHoldGroupCoverSlot)
	{
		return; // a group cover order's slot: his neighbours stand shoulder to shoulder (GroupCoverRules; 2026-10-20)
	}
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (TurnBased && TurnBased->IsActive())
	{
		return; // grid positions stay on their cells
	}
	if (CurrentCoverHeight == ECoverHeight::LowCover)
	{
		return; // a low cover is fired over the top anywhere: no walk to its end
	}
	float Shift = 0.f;
	if (!CoverFacingRules::ShouldSnapToCorner(CoverSlot, CoverFacing, Shift, GetCoverFacingConfig(), Stance == EOperativeStance::Crouching))
	{
		return;
	}
	const FVector Point = CoverSlot.WallPoint + CoverFacingRules::AlongWallDirection(CoverSlot, CoverFacing) * Shift;
	FCoverSlot Corner;
	if (CoverTraceRules::FindShimmySlot(GetWorld(), CoverSlot, Point, Corner) && OrderShimmyTo(Corner) == EOperativeOrderResult::Accepted && bShimmying)
	{
		bCoverAutoSnap = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: to the %s corner (%.0f cm)"), *DisplayName.ToString(),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Shift);
	}
}

bool AOperativeCharacter::IsCornerShotTarget(const FVector& TargetLocation) const
{
	if (!bInCover)
	{
		return false;
	}
	// Corner hold (user decision 2026-10-07): from the edge he fires at what the corner stance can aim at - behind the wall
	// or past the edge, within the twist limit + the aim cone. The sticky "everything beyond the edge, at any depth" rule
	// of the horde fix is gone: in CoverBug_02 (58 s) it kept a flank rush in front of the wall as corner targets, so he
	// shot hounds at his side from the corner pose without turning to them.
	if (IsCornerHoldSpot())
	{
		return CoverFacingRules::IsCornerHoldTarget(CoverSlot, CoverFacing, GetCoverFireOrigin(), TargetLocation, CornerAimOutwardDeg,
			GetCornerHoldReachDeg());
	}
	return CoverFacingRules::ShouldCornerShot(CoverSlot, TargetLocation, GetCoverFacingConfig());
}

bool AOperativeCharacter::IsCornerHoldSpot() const
{
	// Not while the walk to the corner stand-off is still due (bCoverSnapPending: set on entry, decided once the move order
	// ended) - CornerEntrySmoke 2026-10-07: entering 90 cm from the edge he leaned out and broke the aim for the snap shimmy
	// in the same instant; with a later snap that is step out -> tuck back -> step out.
	return bCornerHoldAtEdge && bInCover && !bShimmying && !bCoverSnapPending && CurrentCoverHeight == ECoverHeight::HighCover
		&& bAtCoverCorner && CoverSlot.IsEdgeExposed(CoverFacing);
}

bool AOperativeCharacter::IsManualCornerPeekSpot() const
{
	return bManualCornerPeek && bInCover && !bShimmying && !bCoverSnapPending && CurrentCoverHeight != ECoverHeight::None
		&& CoverFacing == ManualCornerPeekSide && bAtCoverCorner && CoverSlot.IsEdgeExposed(ManualCornerPeekSide);
}

EOperativeOrderResult AOperativeCharacter::OrderCornerPeek(ECoverFacing Side, float EdgeAlongCm)
{
	if (!IsInPeekableCover())
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsRaging() || IsPanicking() || IsKnockedDown())
	{
		return EOperativeOrderResult::Refused;
	}
	// Rev 5e (game designer 2026-10-14): low cover peeks too - crouched, round the end of the low wall / barricade (the crouched
	// corner stance cvr_crch_fire_idle_L / _R steps out sideways; baseline pose, the user polishes the AnimBP).
	const bool bLowCover = CurrentCoverHeight == ECoverHeight::LowCover;
	if (bLowCover && Stance == EOperativeStance::Standing)
	{
		SetStance(EOperativeStance::Crouching);
	}
	const FCoverFacingConfig Config = GetCoverFacingConfig();
	const float StandOff = CoverFacingRules::CornerStandOff(Side, bLowCover || Stance == EOperativeStance::Crouching, Config);
	const float Stand = CoverFacingRules::CornerPeekStandAlong(Side, EdgeAlongCm, StandOff);
	const float BodyAlong = CoverTraceRules::AlongWallDistanceFrom(CoverSlot, CoverSlot.WorldLocation, GetActorLocation());
	// Rev 5d (user bug 2026-10-14): at / near the clicked corner the aim starts in place; turn-based anywhere at that corner.
	const bool bAtThatCorner = CoverFacingRules::IsAtCorner(CoverSlot, Side, Config);
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const CoverFacingRules::ECornerPeekOrder Order = CoverFacingRules::DecideCornerPeekOrder(Stand, BodyAlong, bShimmying, bAtThatCorner,
		TurnBased && TurnBased->IsActive(), Config);
	if (Order == CoverFacingRules::ECornerPeekOrder::Refused)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: corner peek refused in turn-based (%.0f cm from the corner: a grid move first)"),
			*DisplayName.ToString(), FMath::Abs(Stand - BodyAlong));
		return EOperativeOrderResult::Refused;
	}
	bool bThere = Order == CoverFacingRules::ECornerPeekOrder::PoseInPlace;
	if (bCornerAimActive && CoverFacing != Side)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("the other corner"));
	}
	if (!bThere)
	{
		FCoverSlot Target;
		if (!CoverTraceRules::FindShimmySlot(GetWorld(), CoverSlot, CoverSlot.WallPoint + CoverSlot.RightTangent() * Stand, Target))
		{
			if (!bAtThatCorner || bShimmying)
			{
				return EOperativeOrderResult::Unreachable;
			}
			bThere = true; // the last few cm cannot be probed (the wall's very end): his slot already counts as at that corner
		}
		else
		{
			TGuardValue<bool> Ordering(bCornerPeekOrdering, true);
			const EOperativeOrderResult Result = OrderShimmyTo(Target);
			if (Result != EOperativeOrderResult::Accepted)
			{
				return Result;
			}
		}
	}
	bManualCornerPeek = true;
	ManualCornerPeekSide = Side;
	CoverFacing = Side; // walks to it facing forward; the facing stays on that corner (UpdateCoverFacing)
	bCoverSnapPending = false;
	bManualCornerPeekEventPending = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: corner peek ordered at the %s corner (edge %.0f cm along the wall, stand %.0f, %s)"),
		*DisplayName.ToString(), Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), EdgeAlongCm, Stand,
		bThere ? TEXT("already there") : TEXT("shimmies there"));
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("CORNER PEEK"), FLinearColor(0.9f, 0.85f, 0.4f));
	return EOperativeOrderResult::Accepted;
}

bool AOperativeCharacter::WantsCornerHold() const
{
	if (IsManualCornerPeekSpot())
	{
		return true; // the player ordered the corner aim (rev 5c): held with or without a fight
	}
	if (!IsCornerHoldSpot())
	{
		return false;
	}
	if (!bCornerHoldOnlyWhenEngaged)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const bool bInFight = !Flow || Flow->GetPhase() == ECodexGamePhase::WaveCombat || (TurnBased && TurnBased->IsActive());
	return CoverFacingRules::WantsCornerHold(true, bInFight, IsCoverThreatActive());
}

bool AOperativeCharacter::BeginOpenShotFromCover(const AActor* Target)
{
	if (!bInCover || bShimmying)
	{
		return false;
	}
	const FCoverSlot Slot = CoverSlot;
	bOpenShotReturnToCornerHold = IsCornerHoldSpot();
	OpenShotReturnFacing = CoverFacing;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: steps off the wall for an open shot at %s (%s)"), *DisplayName.ToString(),
		Target ? *Target->GetName() : TEXT("-"), bOpenShotReturnToCornerHold ? TEXT("outside the corner stance's reach") : TEXT("in front of the cover"));
	{
		TGuardValue<bool> QuietLeave(bQuietCoverLeaveRequest, bReturnToCoverAfterOpenShot);
		LeaveCover(TEXT("open shot: target in front of the cover"));
	}
	OpenShotReturnSlot = Slot;
	bOpenShotReturnPending = bReturnToCoverAfterOpenShot;
	OpenShotLastTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	OpenShotStartTime = OpenShotLastTime;
	return true;
}

void AOperativeCharacter::UpdateOpenShotReturn()
{
	if (!bOpenShotReturnPending)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const bool bAlive = HealthComponent && HealthComponent->IsAlive();
	// Ordered elsewhere (a move / cover order), back in some cover, down, or moved away: no return.
	if (bInCover || !bAlive || bHasMoveOrder || bHasPendingCover || !World
		|| FVector::Dist2D(GetActorLocation(), OpenShotReturnSlot.WorldLocation) > 150.f)
	{
		bOpenShotReturnPending = false;
		return;
	}
	if (IsRaging() || IsPanicking())
	{
		OpenShotLastTime = World->GetTimeSeconds(); // raging / panicking: the return waits until it passes (it used to be dropped)
		return;
	}
	// Enemies still in sight on the open side (a stream coming at him): he stays out, the calm only starts once none is
	// in sight. Back at a corner hold the stance covers what is round the corner: those do not keep him out.
	const float Reach = GetCornerHoldReachDeg();
	const FVector CornerOrigin = CornerFireOriginFor(OpenShotReturnSlot, OpenShotReturnFacing,
		FVector(OpenShotReturnSlot.WorldLocation.X, OpenShotReturnSlot.WorldLocation.Y, GetMuzzleLocation().Z));
	// Bug hunt 2026-10-18 (CornerHoldSmoke 1 in 3): a rusher waiting right at his side round the wall's end (covered by the
	// corner stance, no shot at it from here, standing still) no longer leaves him pinned in the open - he went 40 s without
	// a shot while a frostbitten stood 1.7 m off behind the corner. Back at the corner he fights it from the lean-out.
	bool bAnyPins = false;
	bool bAnyWaitsRoundCorner = false;
	bool bAnyWaitsAtHisSide = false;
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive() || Enemy->CustomTimeDilation <= 0.f)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), GetActorLocation()));
		const bool bCoveredByCorner = bOpenShotReturnToCornerHold && CoverFacingRules::IsCornerHoldTarget(OpenShotReturnSlot,
			OpenShotReturnFacing, CornerOrigin, Enemy->GetActorLocation(), CornerAimOutwardDeg, Reach);
		// A rusher close by keeps him facing it; one on the open side keeps him out while in sight within 30 m, or - out of
		// sight for a moment (a pack falling back to flank) - within twice that rush distance (CornerHoldSmoke 2026-10-07:
		// he stepped back to the wall while two flankers circled out of sight, then out again).
		const bool bOnTheMove = Enemy->GetVelocity().SizeSquared2D() > FMath::Square(CoverFacingRules::RusherOnTheMoveCmPerSec);
		const bool bHittableHere = Distance <= FlankRushBreakCm && bCoveredByCorner && !bOnTheMove ? CanHitEnemy(Enemy) : true; // traced only when it matters
		switch (CoverFacingRules::ClassifyOpenShotThreat(Distance, FlankRushBreakCm, bCoveredByCorner, bHittableHere, bOnTheMove))
		{
		case CoverFacingRules::EOpenShotThreat::Pins: bAnyPins = true; break;
		case CoverFacingRules::EOpenShotThreat::WaitsRoundCorner: bAnyWaitsRoundCorner = true; break;
		case CoverFacingRules::EOpenShotThreat::WaitsAtHisSide: bAnyWaitsAtHisSide = true; break;
		default:
			bAnyPins |= !bCoveredByCorner && (Distance <= 2.f * FlankRushBreakCm || (!Enemy->IsHidden() && Distance <= 3000.f));
			break;
		}
		if (bAnyPins)
		{
			break;
		}
	}
	if (CoverFacingRules::OpenShotThreatsKeepHimOut(bAnyPins, bAnyWaitsRoundCorner, bAnyWaitsAtHisSide))
	{
		OpenShotLastTime = World->GetTimeSeconds();
	}
	if (World->GetTimeSeconds() - OpenShotLastTime < OpenShotReturnDelaySeconds || World->GetTimeSeconds() - OpenShotStartTime < OpenShotCommitSeconds
		|| GetVelocity().SizeSquared2D() > 4.f || bIsReloading)
	{
		return; // still firing / enemies in sight / settling
	}
	bOpenShotReturnPending = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: back to the wall after the open shot"), *DisplayName.ToString());
	TGuardValue<bool> Quiet(bQuietCoverEntryRequest, true);
	EnterCover(OpenShotReturnSlot);
}

bool AOperativeCharacter::IsPeekingRoundCorner() const
{
	return bInCover && bCornerAimActive && (IsManualCornerPeekSpot() || IsCornerHoldSpot());
}

namespace OperativePeekSight
{
	static TAutoConsoleVariable<int32> CVarPeekSight(TEXT("Codex.Sight.Peek"), 1,
		TEXT("Peek sight (2026-10-19): 1 a peeking operative sees from his leaned-out eyes in the peek cone (and is seen there); 0 the old rule (his capsule's eyes only)"));
}

EPeekSightPose AOperativeCharacter::GetPeekSightPose() const
{
	if (!bInCover || !PeekSightConfig.bEnabled || OperativePeekSight::CVarPeekSight.GetValueOnGameThread() == 0 || (HealthComponent && !HealthComponent->IsAlive()))
	{
		return EPeekSightPose::None;
	}
	return PeekSightRules::DecidePose(CurrentCoverHeight, Stance, bCornerAimActive, IsManualCornerPeekSpot() || IsCornerHoldSpot());
}

bool AOperativeCharacter::GetPeekSight(FVector& OutEye, FVector& OutDirection) const
{
	const EPeekSightPose Pose = GetPeekSightPose();
	if (Pose == EPeekSightPose::None)
	{
		return false;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const float EyeCm = SightRules::EyeHeight(Stance);
	if (Pose == EPeekSightPose::CornerLean)
	{
		// The eyes leaned out past the corner edge as far as the corner shot's muzzle (CornerFireOriginFor), the aim of the corner stance;
		// 2026-10-20: and out in front of the wall's face line where the clip's head is (CornerLeanDepthCm, measured on the head bone).
		OutEye = PeekSightRules::CornerLeanEyePoint(PeekSightConfig,
			CoverRules::CornerLeanPoint(CoverSlot, CoverFacing, Feet + FVector(0.f, 0.f, EyeCm), CoverRules::GetConfig().CornerPeekOffsetCm), CoverSlot.WallNormal);
		OutDirection = PeekSightRules::TwistedDirection(CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg), GetAppliedAimYaw());
		return true;
	}
	// Over the top of a low wall: the head above its top, looking across it.
	OutEye = PeekSightRules::OverTopEyePoint(PeekSightConfig, Feet, CoverSlot.WallNormal, EyeCm, CoverSlot.WallTopCm);
	OutDirection = (-CoverSlot.WallNormal).GetSafeNormal2D();
	return true;
}

bool AOperativeCharacter::GetPeekExposedPoint(FVector& OutPoint) const
{
	FVector Eye;
	FVector Direction;
	if (!GetPeekSight(Eye, Direction))
	{
		return false;
	}
	// The head and shoulder out there: the profile point as far below the leaned-out eyes as the stance's profile is below its eyes.
	OutPoint = Eye - FVector(0.f, 0.f, FMath::Max(0.f, SightRules::EyeHeight(Stance) - SightRules::ProfileHeight(Stance)));
	return true;
}

void AOperativeCharacter::UpdatePeekTrapSpotting(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !PeekSpotConfig.bEnabled || !IsPeekingRoundCorner() || !HealthComponent || !HealthComponent->IsAlive())
	{
		PeekSpotTimer = 0.f; // the first look comes at once when he leans out
		PeekSpotElapsed = 0.f;
		return;
	}
	PeekSpotElapsed += DeltaTime;
	PeekSpotTimer -= DeltaTime;
	if (PeekSpotTimer > 0.f)
	{
		return;
	}
	PeekSpotTimer = FMath::Max(0.02f, PeekSpotConfig.IntervalSeconds);
	// The proximity rule of the traps: no spotting in the wave fight (the turn-based grid / the fight own the traps there).
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat)
	{
		return;
	}
	// 2026-10-19: from the same leaned-out eye point and aim as the peek sight of enemies (GetPeekSight), the narrower trap cone.
	FVector Origin = GetCoverFireOrigin();
	FVector Direction = CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg);
	FVector PeekEye;
	FVector PeekDirection;
	if (GetPeekSight(PeekEye, PeekDirection))
	{
		Origin = PeekEye;
		Direction = PeekDirection;
	}
	// 2026-10-19: the reach sweeps out from his feet - the mines are found one by one, the nearest first.
	const float Reach = TrapSpotRules::PeekSweepRange(PeekSpotConfig.RangeCm, PeekSpotConfig.SweepSeconds, PeekSpotElapsed);
	TArray<AInteractableActor*> Found;
	// Rev 5i: a mine lying in the snow never hides another one from the peek (their trigger shapes would block the sight line):
	// every hidden trap is ignored by the line of sight, only walls / rocks / objects hide a trap. 2026-10-19: so is an armed mine /
	// wire already spotted - with the sweeping reach the near mine is revealed first and its trigger shape hid the ones behind it.
	TArray<AActor*> HiddenTraps;
	TArray<AActor*> SightIgnored;
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		const bool bTrapHidden = It->IsHiddenTrap();
		if (bTrapHidden)
		{
			HiddenTraps.Add(*It);
		}
		if (bTrapHidden || (It->bTrapped && (It->GetTrapFlavor() == ETrapFlavor::Mine || It->GetTrapFlavor() == ETrapFlavor::Tripwire)))
		{
			SightIgnored.Add(*It);
		}
	}
	for (AActor* TrapActor : HiddenTraps)
	{
		AInteractableActor* Trap = Cast<AInteractableActor>(TrapActor);
		if (!Trap)
		{
			continue;
		}
		const FVector Target = Trap->GetActorLocation() + FVector(0.f, 0.f, PeekSpotConfig.TargetHeightCm);
		if (!TrapSpotRules::IsInPeekSightCone(Origin, Direction, Target, Reach, PeekSpotConfig.HalfAngleDeg))
		{
			continue;
		}
		if (PeekSpotConfig.bRequireLineOfSight)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PeekTrapSight), false, this);
			Params.AddIgnoredActors(SightIgnored);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Origin, Target, ECC_Visibility, Params) && !Cast<APawn>(Hit.GetActor()))
			{
				continue; // a wall / rock in between (a squad mate or an enemy does not hide it)
			}
		}
		Found.Add(Trap);
	}
	// Revealed after the scan: each reveal stops the squad and may switch it to Solo (it must not change the set being iterated).
	for (AInteractableActor* Trap : Found)
	{
		if (IsValid(Trap) && Trap->IsHiddenTrap())
		{
			Trap->RevealByPeek(this);
			++PeekSpottedCount;
		}
	}
}
