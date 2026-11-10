// AOperativeCharacter - who fires the RPG-7 and when (the game designer's answers on the RPG-7, 2026-11-09, item 7: "the AI must actually use
// the RPG, including as suppression"; docs/systems/weapons.md "The RPG-7: who fires it and when"; new - no Godot reference).
//
// Why the Commander rarely fired it before (measured in CodexTactics.RpgAiSmoke stage 1, the rule switched off): nothing but the player ever
// put the launcher in his hands - he spawns with the M16 (the loadout's first weapon), the fire loop only ever shoots the weapon in hands, the
// squad autonomy's weapon step knows the pistol / shotgun / knife only, and the M16's 25 m reach keeps a cluster at 30-100 m out of his
// target list altogether. When the player did cycle to it, the one rocket went, the reload started and the sidearm rule (an enemy within
// 10 m) or the autonomy's point-blank switch swapped it for the pistol - which then went back to the PRIMARY, the M16: one rocket per
// manual pick.
//
// The rule (LauncherTacticsRules, launcher_tactics.json) runs in the shared fire loop for the squad and the PMC fighters alike:
// UpdateLauncherChoice after the target search (it sees the targets beyond the rifle's reach too), UpdateLauncherReturn before the
// reload / sidearm step. The blast's suppression is USuppressionSubsystem::ReportLauncherBlast (AProjectileActor, the rocket's burst).

#include "Characters/OperativeCharacter.h"

#include "Characters/OperativeCharacterInternal.h"
#include "CodexTactics.h"
#include "Combat/LauncherRules.h"
#include "Combat/LauncherTacticsRules.h"
#include "Combat/MortarSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/TurretActor.h"
#include "UI/FloatingTextSubsystem.h"
#include "Vehicles/VehiclePawn.h"

namespace OperativeLauncherAIInternal
{
	static TAutoConsoleVariable<int32> CVarLauncherAI(TEXT("Codex.Combat.LauncherAI"), 1,
		TEXT("1 (default): operatives and PMC fighters draw the RPG-7 on their own (LauncherTacticsRules, launcher_tactics.json); 0: only the weapon in hands "
			 "(the pre-2026-11-09 behaviour: the launcher fires only when the player picked it)."),
		ECVF_Default);

	bool IsLauncherWeapon(const UWeaponDataAsset* Weapon)
	{
		return Weapon && Weapon->Stats.Handling == EWeaponHandling::Launcher && Weapon->Stats.Kind == EWeaponKind::Projectile;
	}

	const UWeaponDataAsset* FindLauncherAsset(const TArray<TObjectPtr<UWeaponDataAsset>>& Weapons)
	{
		for (const UWeaponDataAsset* Weapon : Weapons)
		{
			if (IsLauncherWeapon(Weapon))
			{
				return Weapon;
			}
		}
		return nullptr;
	}

	/** A vehicle, a turret or a mortar crewman. */
	bool IsHeavyTarget(const UWorld* World, const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (Actor->IsA<AVehiclePawn>() || Actor->IsA<ATurretActor>())
		{
			return true;
		}
		const AOperativeCharacter* Man = Cast<AOperativeCharacter>(Actor);
		const UMortarSubsystem* Mortars = Man && World ? UMortarSubsystem::Get(World) : nullptr;
		return Mortars && Mortars->FindNestOf(Man) != nullptr;
	}

	/** Shooter's side sees Actor now (the squad's sight for the player's men, the team's for a hostile fighter). */
	bool IsSeenBySide(const AOperativeCharacter& Shooter, const AActor* Actor)
	{
		const UWorld* World = Shooter.GetWorld();
		if (Shooter.IsPlayerTeam())
		{
			const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
			return !Sight || Sight->IsVisibleToSquad(Actor);
		}
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		return !Pmc || Pmc->IsVisibleToTeam(Shooter.TeamId, Actor);
	}

	/** The rocket's aim point on Target (its body's centre, as FireLauncherAt aims). */
	FVector AimPointOf(const AActor* Target)
	{
		return Target->GetActorLocation();
	}
}

void AOperativeCharacter::NotePlayerWeaponChoice()
{
	const UWorld* World = GetWorld();
	PlayerWeaponPickId = CurrentWeapon ? CurrentWeapon->WeaponId : FString();
	PlayerWeaponPickTime = World ? World->GetTimeSeconds() : 0.0;
	bLauncherFallbackActive = false;
	bLauncherDrawnByRule = false;
	LauncherStrikeTarget.Reset();
	NoteLauncherDecision(FString::Printf(TEXT("the player picked %s (the rule leaves it for %.0f s)"), *PlayerWeaponPickId,
		LauncherTacticsRules::GetConfig().PlayerChoiceHoldSeconds));
}

void AOperativeCharacter::ResetLauncherChoice()
{
	PlayerWeaponPickId.Reset();
	PlayerWeaponPickTime = -1000.0;
	bLauncherFallbackActive = false;
	bLauncherDrawnByRule = false;
	LauncherStrikeTarget.Reset();
	LauncherStrikeSince = 0.0;
	LauncherRuleTarget.Reset();
	LauncherNoTargetSeconds = 0.f;
	LauncherDecisionTimer = 0.f;
	LauncherReturnTime = -100.0;
}

FString AOperativeCharacter::GetLauncherWeaponId() const
{
	const UWeaponDataAsset* Launcher = OperativeLauncherAIInternal::FindLauncherAsset(AvailableWeapons);
	if (!Launcher)
	{
		return FString();
	}
	const FWeaponAmmoState Ammo = GetAmmoState(Launcher->WeaponId);
	return Ammo.Clip > 0 || Ammo.Reserve > 0 || Ammo.Reserve == -1 ? Launcher->WeaponId : FString();
}

bool AOperativeCharacter::HasLauncherRockets() const
{
	return !GetLauncherWeaponId().IsEmpty();
}

void AOperativeCharacter::NoteLauncherDecision(const FString& Line)
{
	LastLauncherDecision = Line;
	UE_LOG(LogCodexTactics, Display, TEXT("[RpgChoice] %s (team %d): %s"), *DisplayName.ToString(), TeamId, *Line);
}

FLauncherChoice AOperativeCharacter::EvaluateLauncherTarget(AActor* Target, float Threshold, FLauncherTargetView* OutView) const
{
	using namespace OperativeLauncherAIInternal;
	const FLauncherTacticsConfig& Config = LauncherTacticsRules::GetConfig();
	FLauncherTargetView View;
	const UWeaponDataAsset* Launcher = FindLauncherAsset(AvailableWeapons);
	UWorld* World = GetWorld();
	if (!Launcher || !Target || !World)
	{
		View.RocketsLeft = 0;
		if (OutView)
		{
			*OutView = View;
		}
		return LauncherTacticsRules::Evaluate(Config, View, Threshold < 0.f ? Config.Threshold : Threshold);
	}
	const FWeaponStats& Stats = Launcher->Stats;
	const FVector Aim = AimPointOf(Target);
	View.DistanceCm = static_cast<float>(FVector::Dist(GetActorLocation(), Aim));
	View.MinRangeCm = LauncherRules::MinimumRangeCm(Stats);
	View.MaxRangeCm = Stats.Range.MaxRangeCm;
	const FWeaponAmmoState Ammo = GetAmmoState(Launcher->WeaponId);
	View.RocketsLeft = Ammo.Reserve == -1 ? 99 : FMath::Max(0, Ammo.Clip) + FMath::Max(0, Ammo.Reserve);
	View.bHeavy = IsHeavyTarget(World, Target);

	// The enemies his side sees round the aim point: the cluster, the group, and whether the group is a threat now.
	TArray<FVector> Seen;
	bool bGroupThreat = false;
	for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
	{
		if (!OperativeCharacterInternal::IsLiveTargetOf(*this, Other) || (Other != Target && !IsSeenBySide(*this, Other)))
		{
			continue;
		}
		const FVector Location = Other->GetActorLocation();
		Seen.Add(Location);
		if (FVector::Dist(Location, Aim) > Config.SuppressGroupRadiusCm)
		{
			continue;
		}
		// A threat: closing in on him, a monster hunting the squad, a fighter whose combat target is one of his side.
		const FVector ToMe = (GetActorLocation() - Location).GetSafeNormal2D();
		bGroupThreat |= FVector::DotProduct(Other->GetVelocity(), ToMe) > 50.f;
		if (const AEnemyCharacter* Monster = Cast<AEnemyCharacter>(Other))
		{
			bGroupThreat |= Monster->IsHuntingSquad();
		}
		else if (const AOperativeCharacter* Fighter = Cast<AOperativeCharacter>(Other))
		{
			const AOperativeCharacter* Aimed = Cast<AOperativeCharacter>(Fighter->GetCurrentCombatTarget());
			bGroupThreat |= Aimed && Aimed->TeamId == TeamId;
		}
	}
	AOperativeCharacter* Shooter = nullptr;
	FVector Flash;
	bGroupThreat |= GetUnderFireThreat(Shooter, Flash);
	View.ClusterCount = FMath::Max(1, LauncherTacticsRules::CountWithin(Seen, Aim, Config.ClusterRadiusCm));
	View.SuppressGroup = FMath::Max(1, LauncherTacticsRules::CountWithin(Seen, Aim, Config.SuppressGroupRadiusCm));
	View.bGroupThreat = bGroupThreat;

	// The rocket's line: a wall in front of him (within the blast) = he is in cover and the burst still reaches him; a wall well short of him
	// = the rocket would burst there.
	const FVector Muzzle = GetWeaponMuzzleLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LauncherTargetLine), false, this);
	OperativeCharacterInternal::AddShotLineAllies(World, Params);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	const float BlastCm = FMath::Max(100.f, Stats.Explosion.RadiusCm);
	if (World->LineTraceSingleByChannel(Hit, Muzzle, Aim, ECC_Visibility, Params))
	{
		const float ShortOfTarget = static_cast<float>(FVector::Dist(Hit.ImpactPoint, Aim));
		View.bInCover = ShortOfTarget <= 0.8f * BlastCm;
		View.bLineBlockedShort = !View.bInCover;
	}
	else
	{
		// A clear line to his chest: behind a low wall when the line to his knees is blocked close in front of him.
		const FVector Knees = OperativeCharacterInternal::ShotFeet(Target) + FVector(0.f, 0.f, 40.f);
		FHitResult Low;
		View.bInCover = World->LineTraceSingleByChannel(Low, Muzzle, Knees, ECC_Visibility, Params) && FVector::Dist(Low.ImpactPoint, Knees) <= 250.f;
	}
	if (const AOperativeCharacter* TargetMan = Cast<AOperativeCharacter>(Target))
	{
		View.bInCover |= TargetMan->bInCover;
	}

	// His own side near the impact or in the rocket's path.
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		const AOperativeCharacter* Mate = *It;
		if (Mate == this || Mate->TeamId != TeamId || !Mate->HealthComponent || !Mate->HealthComponent->IsAlive() || Mate->IsKilledInAction())
		{
			continue;
		}
		const FVector MateLocation = Mate->GetActorLocation();
		if (FVector::Dist(MateLocation, Aim) <= Config.FriendlyClearCm || FMath::PointDistToSegment(MateLocation, Muzzle, Aim) <= 100.f)
		{
			++View.FriendliesNear;
		}
	}
	if (OutView)
	{
		*OutView = View;
	}
	return LauncherTacticsRules::Evaluate(Config, View, Threshold < 0.f ? Config.Threshold : Threshold);
}

AActor* AOperativeCharacter::FindBestLauncherTarget(float Threshold, FLauncherChoice& OutChoice) const
{
	using namespace OperativeLauncherAIInternal;
	OutChoice = FLauncherChoice();
	const UWeaponDataAsset* Launcher = FindLauncherAsset(AvailableWeapons);
	if (!Launcher)
	{
		return nullptr;
	}
	const float Reach = Launcher->Stats.Range.MaxRangeCm;
	AActor* Best = nullptr;
	for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
	{
		if (!OperativeCharacterInternal::IsLiveTargetOf(*this, Other) || !IsSeenBySide(*this, Other)
			|| (Reach > 0.f && FVector::Dist(GetActorLocation(), Other->GetActorLocation()) > Reach))
		{
			continue;
		}
		const FLauncherChoice Choice = EvaluateLauncherTarget(Other, Threshold);
		if (Choice.bUse && (!Best || Choice.Score > OutChoice.Score))
		{
			Best = Other;
			OutChoice = Choice;
		}
	}
	return Best;
}

FShootCandidate AOperativeCharacter::MakeLauncherCandidate(AActor* Target) const
{
	FShootCandidate Candidate;
	bool bBlocked = false;
	if (!EvaluateShotLine(Target, true, Candidate, bBlocked))
	{
		// Behind cover: the rocket goes at his centre, the wall in front of him takes the burst (the blast reaches behind it).
		Candidate = FShootCandidate();
		Candidate.Enemy = Target;
		Candidate.Distance = static_cast<float>(FVector::Dist(GetActorLocation(), Target->GetActorLocation()));
		Candidate.Cover = 1.f;
	}
	Candidate.Enemy = Target;
	Candidate.bBlind = false;
	Candidate.bReturnFire = false;
	Candidate.AimPoint = Target->GetActorLocation();
	Candidate.bExposedAim = false;
	return Candidate;
}

bool AOperativeCharacter::OrderLauncherStrike(AActor* Target)
{
	if (!Target || !HasLauncherRockets() || !OperativeCharacterInternal::IsLiveTargetOf(*this, Target))
	{
		return false;
	}
	const UWorld* World = GetWorld();
	LauncherStrikeTarget = Target;
	LauncherStrikeSince = World ? World->GetTimeSeconds() : 0.0;
	LauncherDecisionTimer = 0.f;
	NoteLauncherDecision(FString::Printf(TEXT("ordered rocket strike at %s"), *Target->GetName()));
	return true;
}

void AOperativeCharacter::CancelLauncherStrike(const TCHAR* Why)
{
	if (LauncherStrikeTarget.IsValid() || LauncherStrikeSince > 0.0)
	{
		NoteLauncherDecision(FString::Printf(TEXT("rocket strike dropped (%s)"), Why));
	}
	LauncherStrikeTarget.Reset();
	LauncherStrikeSince = 0.0;
}

bool AOperativeCharacter::ReturnFromLauncher(const TCHAR* Why)
{
	FString To = LauncherReturnWeaponId;
	const TObjectPtr<UWeaponDataAsset>* Back = AvailableWeapons.FindByPredicate([&To](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == To; });
	if (To.IsEmpty() || !Back || OperativeLauncherAIInternal::IsLauncherWeapon(*Back))
	{
		To = GetPrimaryWeaponId();
	}
	if (To.IsEmpty() || !SwitchToWeaponById(To))
	{
		return false;
	}
	const UWorld* World = GetWorld();
	LauncherReturnTime = World ? World->GetTimeSeconds() : 0.0;
	LauncherNoTargetSeconds = 0.f;
	LauncherRuleTarget.Reset();
	LauncherStrikeTarget.Reset();
	++LauncherAutoReturns;
	ShootTimer = FMath::Max(ShootTimer, LauncherTacticsRules::GetConfig().DrawSeconds * 0.5f);
	NoteLauncherDecision(FString::Printf(TEXT("back to the %s (%s)"), *To, Why));
	return true;
}

bool AOperativeCharacter::UpdateLauncherReturn(float DeltaTime)
{
	if (!bLauncherDrawnByRule || !IsLauncherEquipped() || !CurrentWeapon)
	{
		return false;
	}
	// An enemy inside the minimum range (his own blast): the rifle at once - the tube's reload, if any, waits for the next draw.
	const float MinRange = LauncherRules::MinimumRangeCm(CurrentWeapon->Stats);
	for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
	{
		if (OperativeCharacterInternal::IsLiveTargetOf(*this, Other) && FVector::Dist(GetActorLocation(), Other->GetActorLocation()) < MinRange)
		{
			return ReturnFromLauncher(*FString::Printf(TEXT("%s inside the minimum range %.0f m"), *Other->GetName(), MinRange / 100.f));
		}
	}
	return false;
}

const TCHAR* AOperativeCharacter::UpdateLauncherChoice(float DeltaTime, FShootCandidate& InOutShot, bool bAutoFire)
{
	using namespace OperativeLauncherAIInternal;
	UWorld* World = GetWorld();
	if (!World || !CurrentWeapon || IsRaging())
	{
		return nullptr;
	}
	const FLauncherTacticsConfig& Config = LauncherTacticsRules::GetConfig();
	const double Now = World->GetTimeSeconds();
	const bool bRuleOn = Config.Enabled > 0.f && CVarLauncherAI.GetValueOnGameThread() != 0;
	const bool bPicked = LauncherTacticsRules::IsPlayerPickHeld(Config, PlayerWeaponPickTime < -999.0 ? -1.f : static_cast<float>(Now - PlayerWeaponPickTime));
	// Ordered attacks (Ctrl + click, blind fire at a silhouette, the return fire at a flash) use the weapon in hands.
	const bool bOrderedShot = InOutShot.Enemy && (InOutShot.Enemy == ManualPriorityTarget.Get() || InOutShot.bBlind || InOutShot.bReturnFire);
	if (AActor* Strike = LauncherStrikeTarget.Get())
	{
		if (!OperativeCharacterInternal::IsLiveTargetOf(*this, Strike))
		{
			CancelLauncherStrike(TEXT("the target is down"));
		}
		else if (Now - LauncherStrikeSince > Config.StrikeTimeoutSeconds)
		{
			CancelLauncherStrike(TEXT("timed out"));
		}
	}
	else if (LauncherStrikeSince > 0.0)
	{
		CancelLauncherStrike(TEXT("the target is gone"));
	}

	if (IsLauncherEquipped())
	{
		if (bLauncherDrawnByRule)
		{
			// The rule's launcher: the strike target first, else the best rocket target he sees.
			FLauncherChoice Choice;
			AActor* Best = nullptr;
			if (AActor* Strike = LauncherStrikeTarget.Get())
			{
				Choice = EvaluateLauncherTarget(Strike, Config.OrderedThreshold);
				Best = Choice.bUse ? Strike : nullptr;
			}
			if (!Best)
			{
				Best = FindBestLauncherTarget(LauncherStrikeTarget.IsValid() ? Config.OrderedThreshold : Config.Threshold, Choice);
			}
			if (Best)
			{
				LauncherNoTargetSeconds = 0.f;
				if (InOutShot.Enemy != Best)
				{
					InOutShot = MakeLauncherCandidate(Best);
				}
				if (LauncherRuleTarget.Get() != Best)
				{
					LauncherRuleTarget = Best;
					NoteLauncherDecision(FString::Printf(TEXT("aims the rocket at %s: %s, score %.2f"), *Best->GetName(), LauncherTacticsRules::UseName(Choice.Reason), Choice.Score));
				}
				return nullptr;
			}
			// Nothing worth a rocket: a rifle target -> the rifle now; none -> after return_after_s.
			LauncherNoTargetSeconds += DeltaTime;
			if (InOutShot.Enemy || LauncherNoTargetSeconds >= Config.ReturnAfterSeconds || !bRuleOn)
			{
				ReturnFromLauncher(InOutShot.Enemy ? TEXT("a rifle target, no rocket target") : TEXT("no rocket target"));
				return TEXT("launcher: back to the rifle");
			}
			InOutShot = FShootCandidate();
			return TEXT("launcher: no rocket target");
		}
		// The player's launcher (or one held for another reason): any target without a veto gets the rocket.
		if (!InOutShot.Enemy || bOrderedShot)
		{
			return nullptr;
		}
		if (EvaluateLauncherTarget(InOutShot.Enemy, 0.f).Veto == ELauncherVeto::None)
		{
			return nullptr;
		}
		AActor* Valid = nullptr;
		float ValidScore = -1.f;
		for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
		{
			if (!OperativeCharacterInternal::IsLiveTargetOf(*this, Other) || !IsSeenBySide(*this, Other))
			{
				continue;
			}
			const FLauncherChoice Choice = EvaluateLauncherTarget(Other, 0.f);
			if (Choice.Veto == ELauncherVeto::None && Choice.Score > ValidScore)
			{
				Valid = Other;
				ValidScore = Choice.Score;
			}
		}
		if (Valid)
		{
			InOutShot = MakeLauncherCandidate(Valid);
			return nullptr;
		}
		// No valid rocket target (too close / a mate near it): the rifle meanwhile; back to the launcher when a valid target shows.
		const FString Primary = GetPrimaryWeaponId();
		const FString From = CurrentWeapon->WeaponId;
		if (!Primary.IsEmpty() && Primary != From && SwitchToWeaponById(Primary))
		{
			bLauncherFallbackActive = true;
			++LauncherFallbacks;
			ShootTimer = FMath::Max(ShootTimer, Config.DrawSeconds);
			NoteLauncherDecision(FString::Printf(TEXT("the player's %s has no valid target (%s: %s) - falls back to the %s"), *From,
				*InOutShot.Enemy->GetName(), LauncherTacticsRules::VetoName(EvaluateLauncherTarget(InOutShot.Enemy, 0.f).Veto), *Primary));
			return TEXT("launcher: falls back to the rifle");
		}
		return nullptr;
	}

	// The rifle (or another weapon) in hands.
	if (CurrentWeapon->WeaponId == TEXT("grenade") || bQuickFireActive || bSuppressActive || (bOrderedShot && !LauncherStrikeTarget.IsValid()))
	{
		return nullptr;
	}
	const FString Launcher = GetLauncherWeaponId();
	if (Launcher.IsEmpty())
	{
		bLauncherFallbackActive = false;
		if (LauncherStrikeTarget.IsValid())
		{
			CancelLauncherStrike(TEXT("no rockets"));
		}
		return nullptr;
	}
	// The player's launcher put away by another rule (the sidearm during its reload, an empty-weapon switch): the same as the fallback.
	bLauncherFallbackActive |= bPicked && PlayerWeaponPickId == Launcher;
	if (bLauncherFallbackActive)
	{
		// The player's launcher swapped for the rifle: back to it as soon as a target without a veto shows (none inside its minimum range).
		if (!bPicked || PlayerWeaponPickId != Launcher)
		{
			bLauncherFallbackActive = false;
			return nullptr;
		}
		LauncherDecisionTimer -= DeltaTime;
		if (LauncherDecisionTimer > 0.f)
		{
			return nullptr;
		}
		LauncherDecisionTimer = Config.DecisionIntervalSeconds;
		const UWeaponDataAsset* LauncherAsset = FindLauncherAsset(AvailableWeapons);
		const float MinRange = LauncherAsset ? LauncherRules::MinimumRangeCm(LauncherAsset->Stats) : 0.f;
		for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
		{
			if (OperativeCharacterInternal::IsLiveTargetOf(*this, Other) && FVector::Dist(GetActorLocation(), Other->GetActorLocation()) < MinRange)
			{
				return nullptr; // an enemy inside the minimum range: the rifle / pistol stays
			}
		}
		for (AActor* Other : OperativeCharacterInternal::GatherTargetsFor(*this))
		{
			if (OperativeCharacterInternal::IsLiveTargetOf(*this, Other) && IsSeenBySide(*this, Other)
				&& EvaluateLauncherTarget(Other, 0.f).Veto == ELauncherVeto::None && SwitchToWeaponById(Launcher))
			{
				bLauncherFallbackActive = false;
				ShootTimer = FMath::Max(ShootTimer, Config.DrawSeconds);
				NoteLauncherDecision(FString::Printf(TEXT("back to the player's %s (%s is a valid target)"), *Launcher, *Other->GetName()));
				return TEXT("launcher: back to the player's launcher");
			}
		}
		return nullptr;
	}
	if (bPicked && !LauncherStrikeTarget.IsValid())
	{
		return nullptr; // the player's own pick wins
	}
	const bool bStrike = LauncherStrikeTarget.IsValid();
	if (!bStrike)
	{
		if (!bRuleOn || !bAutoFire || Now - LauncherReturnTime < Config.RedrawCooldownSeconds)
		{
			return nullptr;
		}
		LauncherDecisionTimer -= DeltaTime;
		if (LauncherDecisionTimer > 0.f)
		{
			return nullptr;
		}
		LauncherDecisionTimer = Config.DecisionIntervalSeconds;
	}
	FLauncherChoice Choice;
	AActor* Best = nullptr;
	FLauncherTargetView View;
	if (AActor* Strike = LauncherStrikeTarget.Get())
	{
		Choice = EvaluateLauncherTarget(Strike, Config.OrderedThreshold, &View);
		Best = Choice.bUse ? Strike : FindBestLauncherTarget(Config.OrderedThreshold, Choice);
	}
	else
	{
		Best = FindBestLauncherTarget(Config.Threshold, Choice);
	}
	if (Best && Best != LauncherStrikeTarget.Get())
	{
		EvaluateLauncherTarget(Best, -1.f, &View);
	}
	if (!Best)
	{
		// The decision path for the log (every 2 s at most): the best target he saw and why it got no rocket.
		if (Now - LauncherDecisionLogTime > 2.0 && InOutShot.Enemy)
		{
			LauncherDecisionLogTime = Now;
			FLauncherTargetView Seen;
			const FLauncherChoice Kept = EvaluateLauncherTarget(InOutShot.Enemy, -1.f, &Seen);
			NoteLauncherDecision(FString::Printf(TEXT("keeps the %s for %s (%.0f m): %s, score %.2f < %.2f, veto %s (cluster %d, group %d, cover %d)"),
				*CurrentWeapon->WeaponId, *InOutShot.Enemy->GetName(), Seen.DistanceCm / 100.f, LauncherTacticsRules::UseName(Kept.Reason), Kept.Score,
				bStrike ? Config.OrderedThreshold : Config.Threshold, LauncherTacticsRules::VetoName(Kept.Veto), Seen.ClusterCount, Seen.SuppressGroup, Seen.bInCover ? 1 : 0));
		}
		return nullptr;
	}
	const FString From = CurrentWeapon->WeaponId;
	if (!SwitchToWeaponById(Launcher))
	{
		return nullptr;
	}
	bLauncherDrawnByRule = true;
	LauncherReturnWeaponId = From;
	LauncherRuleTarget = Best;
	LauncherNoTargetSeconds = 0.f;
	ShootTimer = FMath::Max(ShootTimer, Config.DrawSeconds);
	++LauncherAutoDraws;
	NoteLauncherDecision(FString::Printf(TEXT("draws the %s%s for %s (%.0f m): %s, score %.2f (cluster %d, group %d%s, cover %d, heavy %d, rockets %d)"),
		*Launcher, bStrike ? TEXT(" (ordered strike)") : TEXT(""), *Best->GetName(), View.DistanceCm / 100.f, LauncherTacticsRules::UseName(Choice.Reason),
		Choice.Score, View.ClusterCount, View.SuppressGroup, View.bGroupThreat ? TEXT(" threat") : TEXT(""), View.bInCover ? 1 : 0, View.bHeavy ? 1 : 0, View.RocketsLeft));
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("RPG!"), FLinearColor(1.f, 0.55f, 0.15f));
	return TEXT("launcher drawn");
}
