#include "Vehicles/VehicleTurretComponent.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/PlayerCommands.h"
#include "Data/WeaponTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Vehicles/VehiclePawn.h"
#include "Combat/TargetedShotRules.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/ProximityMineActor.h"
#include "UI/GameMessageSubsystem.h"

UVehicleTurretComponent::UVehicleTurretComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after the hull moved: aim from where it is now
	MuzzleFlash = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
		TEXT("/Game/VigilanteContent/Vehicles/East_APC_BTR82/FX/PS_MuzzleFire_01_East_APC_BTR82.PS_MuzzleFire_01_East_APC_BTR82")));
}

void UVehicleTurretComponent::BeginPlay()
{
	Super::BeginPlay();
	WeaponTuning::ApplyVehicleWeapons(&Config, nullptr);
}

AVehiclePawn* UVehicleTurretComponent::GetVehicle() const
{
	return Cast<AVehiclePawn>(GetOwner());
}

AOperativeCharacter* UVehicleTurretComponent::GetGunner() const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	return Vehicle ? Vehicle->GetOccupant(EVehicleSeat::Gunner) : nullptr;
}

AActor* UVehicleTurretComponent::GetCurrentTarget() const
{
	return CurrentTarget.Get();
}

void UVehicleTurretComponent::InitShells()
{
	if (bShellsInitialised)
	{
		return;
	}
	bShellsInitialised = true;
	// The level JSON "vehicle": {"shells": N} overrides the Blueprint / weapons_tuning value per map.
	int32 LevelShells = -1;
	const ACodexTacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	if (const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr)
	{
		LevelShells = Level->Config.VehicleShells;
	}
	ShellsMax = VehicleCombatRules::GetShellsForMap(LevelShells, Config);
	ShellsLeft = ShellsMax;
}

void UVehicleTurretComponent::SetShellsLeft(int32 Shells)
{
	InitShells();
	ShellsLeft = FMath::Clamp(Shells, 0, FMath::Max(ShellsMax, Shells));
}

void UVehicleTurretComponent::SetPriorityTarget(AActor* Target)
{
	PriorityTarget = Target;
	RetargetTimer = 0.f;
}

FVector UVehicleTurretComponent::GetMuzzleLocation() const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		return FVector::ZeroVector;
	}
	if (Vehicle->Mesh && Vehicle->Mesh->GetSkeletalMeshAsset() && Vehicle->Mesh->DoesSocketExist(MuzzleSocket))
	{
		return Vehicle->Mesh->GetSocketLocation(MuzzleSocket);
	}
	return Vehicle->GetHullCentreWorld() + FVector(0.f, 0.f, Vehicle->HullHalfExtent.Z + 60.f);
}

FString UVehicleTurretComponent::GetHudLine() const
{
	const AOperativeCharacter* Gunner = GetGunner();
	return VehicleCombatRules::GetHudLine(Gunner != nullptr, ShellsLeft, ShellsMax, MgHeat, bMgOverheated,
		Gunner ? Gunner->GetFirePosture() : FirePostureRules::DefaultPosture);
}

void UVehicleTurretComponent::SetAimImmediate(float YawDeg, float PitchDeg)
{
	TurretYaw = DesiredYaw = FRotator::NormalizeAxis(YawDeg);
	GunPitch = DesiredPitch = VehicleCombatRules::ClampElevation(PitchDeg, Config);
	PushAnim();
}

void UVehicleTurretComponent::PushAnim() const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	UAnimInstance* Anim = Vehicle && Vehicle->Mesh ? Vehicle->Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	auto Set = [Anim](FName Name, float Value)
	{
		if (FNumericProperty* Property = CastField<FNumericProperty>(Anim->GetClass()->FindPropertyByName(Name)); Property && Property->IsFloatingPoint())
		{
			Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Anim), Value);
		}
	};
	Set(AnimTurretYawVariable, TurretYaw * AnimTurretYawSign);
	Set(AnimGunPitchVariable, GunPitch * AnimGunPitchSign);
}

bool UVehicleTurretComponent::HasLineOfFire(const FVector& From, const AActor* Target) const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleTurretLine), false, Vehicle);
	if (Vehicle)
	{
		for (const FVehicleCrewMember& Member : Vehicle->GetCrew())
		{
			Params.AddIgnoredActor(Member.Operative.Get());
		}
	}
	FHitResult Hit;
	const FVector To = Target->GetActorLocation();
	if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
	{
		return true;
	}
	// Another enemy in the way still takes the burst; a wall / rock does not.
	const AActor* Blocker = Hit.GetActor();
	return Blocker == Target || (Blocker && Blocker->IsA<AEnemyCharacter>());
}

void UVehicleTurretComponent::GatherCandidates(TArray<VehicleCombatRules::FTurretCandidate>& OutCandidates, TArray<AEnemyCharacter*>& OutEnemies) const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	const FVector Muzzle = GetMuzzleLocation();
	const float Reach = FMath::Max(Config.CannonRangeCm, Config.MgRangeCm) + 200.f;
	for (AEnemyCharacter* Registered : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		AEnemyCharacter* Enemy = Registered;
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (Enemy->IsDying() || (Health && !Health->IsAlive()) || Enemy->IsHidden() || !Enemy->ActorHasTag(TEXT("Enemy"))
			|| FVector::Dist(Muzzle, Enemy->GetActorLocation()) > Reach)
		{
			continue;
		}
		VehicleCombatRules::FTurretCandidate Candidate;
		Candidate.DistanceCm = FVector::Dist(Muzzle, Enemy->GetActorLocation());
		Candidate.HullDistanceCm = Vehicle->GetHullDistance2D(Enemy->GetActorLocation());
		Candidate.bLarge = Enemy->GetArchetype() == EEnemyArchetype::Brute;
		Candidate.bMarksman = Enemy->GetArchetype() == EEnemyArchetype::Marksman;
		Candidate.bVisible = HasLineOfFire(Muzzle, Enemy);
		OutCandidates.Add(Candidate);
		OutEnemies.Add(Enemy);
	}
	const float GroupRadius = Config.CannonSplashRadiusCm * 2.f;
	for (int32 Index = 0; Index < OutEnemies.Num(); ++Index)
	{
		for (int32 Other = 0; Other < OutEnemies.Num(); ++Other)
		{
			if (Other != Index && FVector::Dist(OutEnemies[Index]->GetActorLocation(), OutEnemies[Other]->GetActorLocation()) <= GroupRadius)
			{
				++OutCandidates[Index].NeighbourCount;
			}
		}
	}
}

void UVehicleTurretComponent::Retarget()
{
	AOperativeCharacter* Gunner = GetGunner();
	TArray<VehicleCombatRules::FTurretCandidate> Candidates;
	TArray<AEnemyCharacter*> Enemies;
	GatherCandidates(Candidates, Enemies);
	VehicleCombatRules::FTurretContext Context;
	Context.Posture = Gunner->GetFirePosture();
	Context.bProvoked = Gunner->MayAutoFireNow();
	Context.ShellsLeft = ShellsLeft;
	Context.bMgOverheated = bMgOverheated;
	AActor* Priority = PriorityTarget.Get();
	if (Priority)
	{
		const UHealthComponent* Health = Priority->FindComponentByClass<UHealthComponent>();
		const AEnemyCharacter* PriorityEnemy = Cast<AEnemyCharacter>(Priority);
		if (!Health || !Health->IsAlive() || (PriorityEnemy && PriorityEnemy->IsDying()))
		{
			PriorityTarget.Reset();
			Priority = nullptr;
		}
	}
	Context.PriorityIndex = Priority ? Enemies.IndexOfByKey(Priority) : INDEX_NONE;
	const int32 Chosen = VehicleCombatRules::ChooseTarget(Candidates, Context, Config);
	// The dead zone (smokes / tutorial V3b): an enemy the gun would otherwise take stands too close to the hull.
	bool bBlockedByDeadZone = false;
	for (const VehicleCombatRules::FTurretCandidate& Candidate : Candidates)
	{
		bBlockedByDeadZone |= Candidate.bVisible && Candidate.bAlive && VehicleCombatRules::IsInDeadZone(Candidate.HullDistanceCm, Config)
			&& Context.Posture != ESquadFirePosture::Passive;
	}
	if (Chosen == INDEX_NONE)
	{
		DeadZoneRefusals += bBlockedByDeadZone ? 1 : 0;
		CurrentTarget.Reset();
		CurrentWeapon = EVehicleTurretWeapon::None;
		return;
	}
	if (CurrentTarget.Get() != Enemies[Chosen])
	{
		BurstLeft = 0;
	}
	CurrentTarget = Enemies[Chosen];
	CurrentWeapon = VehicleCombatRules::ChooseWeapon(Candidates[Chosen], Chosen == Context.PriorityIndex, ShellsLeft, bMgOverheated, Config);
}

void UVehicleTurretComponent::UpdateAim(float DeltaTime)
{
	const AVehiclePawn* Vehicle = GetVehicle();
	const AActor* Target = ObjectTarget.IsValid() ? ObjectTarget.Get() : static_cast<const AActor*>(CurrentTarget.Get());
	if (Target)
	{
		const FVector Pivot = Vehicle->GetHullCentreWorld() + FVector(0.f, 0.f, Vehicle->HullHalfExtent.Z);
		const FVector To = Target->GetActorLocation() - Pivot;
		const float WorldYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		DesiredYaw = FRotator::NormalizeAxis(WorldYaw - Vehicle->GetHeadingYaw());
		DesiredPitch = VehicleCombatRules::ClampElevation(FMath::RadiansToDegrees(FMath::Atan2(To.Z, FMath::Max(1.f, To.Size2D()))), Config);
		IdleSeconds = 0.f;
	}
	else
	{
		IdleSeconds += DeltaTime;
		if (IdleSeconds > 4.f)
		{
			DesiredYaw = 0.f; // back to the travel direction
			DesiredPitch = 0.f;
		}
	}
	TurretYaw = VehicleCombatRules::StepAngle(TurretYaw, DesiredYaw, Config.TraverseDegPerSec, DeltaTime);
	GunPitch = VehicleCombatRules::StepAngle(GunPitch, DesiredPitch, Config.ElevationDegPerSec, DeltaTime);
	PushAnim();
}

void UVehicleTurretComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UWorld* World = GetWorld();
	AVehiclePawn* Vehicle = GetVehicle();
	if (!World || !Vehicle || DeltaTime <= 0.f)
	{
		return;
	}
	InitShells();
	if (FlashLeft > 0.f)
	{
		FlashLeft -= DeltaTime;
		if (FlashLeft <= 0.f && FlashComponent)
		{
			FlashComponent->DeactivateSystem();
		}
	}
	AOperativeCharacter* Gunner = GetGunner();
	const bool bHeld = IsHeld();
	bMgFiringNow = false;
	if (bHeld)
	{
		ObjectTarget.Reset();
		CurrentTarget.Reset();
		CurrentWeapon = EVehicleTurretWeapon::None;
		MgHeat = VehicleCombatRules::StepHeat(MgHeat, false, DeltaTime, bMgOverheated, Config);
		return;
	}
	CannonTimer = FMath::Max(0.f, CannonTimer - DeltaTime);
	MgFiredEventTimer = FMath::Max(0.f, MgFiredEventTimer - DeltaTime);
	NoiseTimer = FMath::Max(0.f, NoiseTimer - DeltaTime);
	RetargetTimer -= DeltaTime;
	// P4: a Ctrl+click object order goes first - one shell once the gun points at it.
	if (AActor* Object = ObjectTarget.Get())
	{
		ObjectTargetAge += DeltaTime;
		const EVehicleObjectShot Check = CheckObjectShot(Object);
		if (Check != EVehicleObjectShot::None || ObjectTargetAge > ObjectTargetTimeoutSeconds)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] turret drops the object order %s (%s, %.1f s)"), *Object->GetName(),
				*StaticEnum<EVehicleObjectShot>()->GetNameStringByValue(static_cast<int64>(Check)), ObjectTargetAge);
			ObjectTarget.Reset();
		}
		else
		{
			CurrentTarget.Reset();
			CurrentWeapon = EVehicleTurretWeapon::Cannon;
			UpdateAim(DeltaTime);
			if (CannonTimer <= 0.f && VehicleCombatRules::IsAimed(FMath::FindDeltaAngleDegrees(TurretYaw, DesiredYaw), GunPitch - DesiredPitch, Config))
			{
				FireCannonAtObject(Object, Gunner);
			}
			MgHeat = VehicleCombatRules::StepHeat(MgHeat, false, DeltaTime, bMgOverheated, Config);
			return;
		}
	}
	const AEnemyCharacter* Current = CurrentTarget.Get();
	if (RetargetTimer <= 0.f || (Current && (Current->IsDying() || !Current->GetHealthComponent() || !Current->GetHealthComponent()->IsAlive())))
	{
		RetargetTimer = Config.RetargetSeconds;
		Retarget();
	}
	UpdateAim(DeltaTime);
	AEnemyCharacter* Target = CurrentTarget.Get();
	const bool bAimed = Target && VehicleCombatRules::IsAimed(FMath::FindDeltaAngleDegrees(TurretYaw, DesiredYaw), GunPitch - DesiredPitch, Config);
	if (bAimed && CurrentWeapon == EVehicleTurretWeapon::Cannon && ShellsLeft > 0 && CannonTimer <= 0.f)
	{
		FireCannon(Target, Gunner);
	}
	else if (bAimed && CurrentWeapon == EVehicleTurretWeapon::MachineGun && !bMgOverheated)
	{
		bMgFiringNow = true;
		MgRoundAccumulator += DeltaTime * Config.MgRoundsPerSecond;
		while (MgRoundAccumulator >= 1.f && CurrentTarget.IsValid() && !CurrentTarget->IsDying())
		{
			MgRoundAccumulator -= 1.f;
			FireMgRound(CurrentTarget.Get(), Gunner);
		}
	}
	else
	{
		MgRoundAccumulator = FMath::Min(MgRoundAccumulator, 1.f);
	}
	const bool bWasOverheated = bMgOverheated;
	MgHeat = VehicleCombatRules::StepHeat(MgHeat, bMgFiringNow, DeltaTime, bMgOverheated, Config);
	if (bMgOverheated && !bWasOverheated)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s: the coaxial MG overheated"), *Vehicle->GetName());
		RetargetTimer = 0.f; // the cannon may take over (big targets) or the gunner waits
	}
}

void UVehicleTurretComponent::ApplyHit(AEnemyCharacter* Enemy, float Amount, EDamageType Type, float ArmourPenetration, AOperativeCharacter* Gunner,
	EVehicleTurretWeapon Weapon)
{
	UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
	if (!Health || !Health->IsAlive() || Enemy->IsDying() || Amount <= 0.f)
	{
		return;
	}
	FDamageSpec Spec;
	Spec.Amount = Amount;
	Spec.DamageType = Type;
	Spec.ArmorPenetration = FMath::Clamp(ArmourPenetration, 0.f, 1.f);
	Spec.AttackerSource = Gunner ? Gunner->DisplayName.ToString() : TEXT("APC");
	Health->TakeDamage(Spec);
	if (!Health->IsAlive() || Enemy->IsDying())
	{
		++Kills;
		UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] turret (%s) killed %s"), Weapon == EVehicleTurretWeapon::Cannon ? TEXT("cannon") : TEXT("MG"), *Enemy->GetName());
		FGameplayEventContext Event;
		Event.Subject = Gunner;
		Event.Target = Enemy;
		Event.Kind = Weapon == EVehicleTurretWeapon::Cannon ? FName(TEXT("Cannon")) : FName(TEXT("MachineGun"));
		Event.Location = Enemy->GetActorLocation();
		GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleTurretKill, Event);
	}
}

void UVehicleTurretComponent::FireCannon(AEnemyCharacter* Target, AOperativeCharacter* Gunner)
{
	const FVector Muzzle = GetMuzzleLocation();
	const bool bHit = FMath::FRand() < Config.CannonHitChance;
	// A miss lands 1.5 - 3 m off the target (the splash may still catch him).
	FVector Impact = Target->GetActorLocation();
	if (!bHit)
	{
		const FVector2D Off = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal() * FMath::FRandRange(150.f, 300.f);
		Impact += FVector(Off.X, Off.Y, -40.f);
	}
	--ShellsLeft;
	++CannonShots;
	if (BurstLeft <= 0)
	{
		BurstLeft = Config.CannonBurstShots;
	}
	--BurstLeft;
	CannonTimer = BurstLeft > 0 ? Config.CannonShotIntervalSeconds : Config.CannonBurstPauseSeconds;
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTracer(Muzzle, Impact, FLinearColor(1.f, 0.55f, 0.15f), EDamageType::Explosive);
	}
	PlayMuzzleFlash(true);
	// Direct hit + splash on everyone round the impact (the gunner's kill credit).
	TArray<AEnemyCharacter*> Victims;
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (!Enemy->IsDying() && FVector::Dist(Enemy->GetActorLocation(), Impact) <= Config.CannonSplashRadiusCm + 50.f)
		{
			Victims.Add(Enemy);
		}
	}
	if (bHit)
	{
		Victims.AddUnique(Target);
	}
	const float Penetration = 1.f - Config.CannonArmourFactor;
	for (AEnemyCharacter* Victim : Victims)
	{
		const bool bDirect = bHit && Victim == Target;
		const float Damage = VehicleCombatRules::GetCannonDamage(FVector::Dist(Victim->GetActorLocation(), Impact), bDirect, Config);
		ApplyHit(Victim, Damage, EDamageType::Explosive, Penetration, Gunner, EVehicleTurretWeapon::Cannon);
	}
	GameplayEvents::RaiseExplosion(this, GameplayEvents::ExplosionKinds::Cannon, Impact, Config.CannonSplashRadiusCm, GetOwner()); // visual listeners only (snow crater)
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = 1.f;
		AEnemyCharacter::NotifySquadNoise(GetWorld(), GetOwner()->GetActorLocation(), ESquadNoise::Explosion);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] cannon fires at %s (%s, %d shells left)"), *Target->GetName(), bHit ? TEXT("hit") : TEXT("miss"), ShellsLeft);
	RaiseFired(Target, EVehicleTurretWeapon::Cannon);
}

void UVehicleTurretComponent::FireMgRound(AEnemyCharacter* Target, AOperativeCharacter* Gunner)
{
	const FVector Muzzle = GetMuzzleLocation();
	const bool bHit = FMath::FRand() < Config.MgHitChance;
	FVector End = Target->GetActorLocation();
	if (!bHit)
	{
		End += FVector(FMath::FRandRange(-80.f, 80.f), FMath::FRandRange(-80.f, 80.f), FMath::FRandRange(-40.f, 60.f));
	}
	++MgRounds;
	// Every other round draws a tracer (8 rounds/s would flood the screen).
	if ((MgRounds & 1) == 1)
	{
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnTracer(Muzzle, End, UCombatFeedbackSubsystem::DefaultTracerColor(), EDamageType::Kinetic);
		}
	}
	PlayMuzzleFlash(false);
	if (bHit)
	{
		// User decision 2026-10-12: the MG just deals damage (no suppression stagger).
		ApplyHit(Target, Config.MgDamage, EDamageType::Kinetic, 0.2f, Gunner, EVehicleTurretWeapon::MachineGun);
	}
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = 1.f;
		AEnemyCharacter::NotifySquadNoise(GetWorld(), GetOwner()->GetActorLocation(), ESquadNoise::Gunshot);
	}
	if (MgFiredEventTimer <= 0.f)
	{
		MgFiredEventTimer = 1.f; // one VehicleTurretFired per second of MG fire (a "burst")
		RaiseFired(Target, EVehicleTurretWeapon::MachineGun);
	}
}

void UVehicleTurretComponent::PlayMuzzleFlash(bool bCannon)
{
	AVehiclePawn* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		return;
	}
	if (!FlashComponent && Vehicle->Mesh && Vehicle->Mesh->DoesSocketExist(MuzzleSocket))
	{
		if (UParticleSystem* Template = MuzzleFlash.LoadSynchronous())
		{
			FlashComponent = UGameplayStatics::SpawnEmitterAttached(Template, Vehicle->Mesh, MuzzleSocket, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, false, EPSCPoolMethod::None, false);
		}
	}
	if (FlashComponent)
	{
		if (!FlashComponent->IsActive())
		{
			FlashComponent->ActivateSystem(true);
		}
		FlashLeft = bCannon ? 0.2f : 0.15f;
	}
	Vehicle->ReceiveTurretFired(bCannon ? EVehicleTurretWeapon::Cannon : EVehicleTurretWeapon::MachineGun, GetMuzzleLocation());
}

void UVehicleTurretComponent::RaiseFired(AActor* Target, EVehicleTurretWeapon Weapon)
{
	FGameplayEventContext Event;
	Event.Subject = GetGunner();
	Event.Target = Target;
	Event.Kind = Weapon == EVehicleTurretWeapon::Cannon ? FName(TEXT("Cannon")) : FName(TEXT("MachineGun"));
	Event.Location = Target ? Target->GetActorLocation() : FVector::ZeroVector;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleTurretFired, Event);
}

// --- Object shots (P4) ----------------------------------------------------------------------------------------------

bool UVehicleTurretComponent::IsHeld() const
{
	const UWorld* World = GetWorld();
	const AVehiclePawn* Vehicle = GetVehicle();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bTurnBased = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	return !GetGunner() || bTurnBased || UWorldAIPauseSubsystem::IsPausedIn(World) || !Vehicle || Vehicle->IsDisabled();
}

bool UVehicleTurretComponent::HasLineOfFireToObject(const FVector& From, const AActor* Object) const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleTurretObjectLine), false, Vehicle);
	if (Vehicle)
	{
		for (const FVehicleCrewMember& Member : Vehicle->GetCrew())
		{
			Params.AddIgnoredActor(Member.Operative.Get());
		}
	}
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		Params.AddIgnoredActor(Enemy); // the shell is aimed at the object, past the pack
	}
	FVector Origin;
	FVector Extent;
	Object->GetActorBounds(true, Origin, Extent);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, From, Origin, ECC_Visibility, Params))
	{
		return true;
	}
	// The object itself (or something attached to it) takes the shell; a wall / rock in between does not.
	const AActor* Blocker = Hit.GetActor();
	return Blocker == Object || (Blocker && Blocker->GetAttachParentActor() == Object) || FVector::Dist(Hit.ImpactPoint, Origin) <= Extent.Size() + 30.f;
}

EVehicleObjectShot UVehicleTurretComponent::CheckObjectShot(const AActor* Object) const
{
	const AVehiclePawn* Vehicle = GetVehicle();
	VehicleHullRules::FObjectShotCheck Check;
	const ETargetedShotKind Kind = AOperativeCharacter::ClassifyShotTarget(Object);
	const ABarrelActor* Barrel = Cast<ABarrelActor>(Object);
	const AInteractableActor* Interactable = Cast<AInteractableActor>(Object);
	Check.bShootable = Object && Vehicle
		&& ((Kind == ETargetedShotKind::Barrel && Barrel && !Barrel->IsBurning())
			|| ((Kind == ETargetedShotKind::Mine || Kind == ETargetedShotKind::Crate || Kind == ETargetedShotKind::TrappedObject) && Interactable && Interactable->bTrapped));
	if (!Check.bShootable)
	{
		return VehicleHullRules::CheckObjectShot(Check);
	}
	const FVector Muzzle = GetMuzzleLocation();
	Check.bHasGunner = GetGunner() != nullptr;
	Check.bDisabled = Vehicle->IsDisabled();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	Check.bHeld = (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased) || UWorldAIPauseSubsystem::IsPausedIn(GetWorld());
	Check.ShellsLeft = bShellsInitialised ? ShellsLeft : VehicleCombatRules::GetShellsForMap(-1, Config);
	Check.DistanceCm = FVector::Dist(Muzzle, Object->GetActorLocation());
	Check.HullDistanceCm = Vehicle->GetHullDistance2D(Object->GetActorLocation());
	Check.CannonRangeCm = Config.CannonRangeCm;
	Check.DeadZoneCm = Config.DeadZoneCm;
	// The line (a trace) last: only when everything else allows the shot.
	const EVehicleObjectShot Early = VehicleHullRules::CheckObjectShot(Check);
	if (Early != EVehicleObjectShot::None)
	{
		return Early;
	}
	Check.bLineOfFire = HasLineOfFireToObject(Muzzle, Object);
	return VehicleHullRules::CheckObjectShot(Check);
}

EVehicleObjectShot UVehicleTurretComponent::SetObjectTarget(AActor* Object)
{
	InitShells();
	const EVehicleObjectShot Check = CheckObjectShot(Object);
	if (Check == EVehicleObjectShot::None)
	{
		ObjectTarget = Object;
		ObjectTargetAge = 0.f;
		CurrentTarget.Reset();
		BurstLeft = 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] turret: object order %s (%.1f m)"), *Object->GetName(),
			FVector::Dist(GetMuzzleLocation(), Object->GetActorLocation()) / 100.f);
	}
	return Check;
}

void UVehicleTurretComponent::FireCannonAtObject(AActor* Object, AOperativeCharacter* Gunner)
{
	const FVector Muzzle = GetMuzzleLocation();
	const ETargetedShotKind Kind = AOperativeCharacter::ClassifyShotTarget(Object);
	FVector Origin;
	FVector Extent;
	Object->GetActorBounds(true, Origin, Extent);
	--ShellsLeft;
	++CannonShots;
	++ObjectShots;
	BurstLeft = 0;
	CannonTimer = Config.CannonBurstPauseSeconds;
	ObjectTarget.Reset();
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTracer(Muzzle, Origin, FLinearColor(1.f, 0.55f, 0.15f), EDamageType::Explosive);
	}
	PlayMuzzleFlash(true);
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = 1.f;
		AEnemyCharacter::NotifySquadNoise(GetWorld(), GetOwner()->GetActorLocation(), ESquadNoise::Explosion);
	}
	const FText Shooter = Gunner ? Gunner->DisplayName : (GetVehicle() ? GetVehicle()->DisplayName : FText::GetEmpty());
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] cannon shell at %s (%d shells left)"), *Object->GetName(), ShellsLeft);
	RaiseFired(Object, EVehicleTurretWeapon::Cannon);
	// The detonation an operative's targeted shot would set off (a barrel blast, a mine, a trap).
	const bool bWentOff = AOperativeCharacter::DetonateShotTarget(Object, Kind, Shooter);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Shooter, bWentOff ? NSLOCTEXT("CodexVehicle", "ShellObjectBoom", "Shell on target - it went up!")
			: NSLOCTEXT("CodexVehicle", "ShellObjectDud", "Shell on target - nothing went off."));
	}
}
