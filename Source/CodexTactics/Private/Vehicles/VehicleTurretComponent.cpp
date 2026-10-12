#include "Vehicles/VehicleTurretComponent.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
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
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
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
	if (const AEnemyCharacter* Target = CurrentTarget.Get())
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
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bTurnBased = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	const bool bHeld = !Gunner || bTurnBased || UWorldAIPauseSubsystem::IsPausedIn(World);
	bMgFiringNow = false;
	if (bHeld)
	{
		CurrentTarget.Reset();
		CurrentWeapon = EVehicleTurretWeapon::None;
		MgHeat = VehicleCombatRules::StepHeat(MgHeat, false, DeltaTime, bMgOverheated, Config);
		return;
	}
	CannonTimer = FMath::Max(0.f, CannonTimer - DeltaTime);
	MgFiredEventTimer = FMath::Max(0.f, MgFiredEventTimer - DeltaTime);
	NoiseTimer = FMath::Max(0.f, NoiseTimer - DeltaTime);
	RetargetTimer -= DeltaTime;
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
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDying() && FVector::Dist(It->GetActorLocation(), Impact) <= Config.CannonSplashRadiusCm + 50.f)
		{
			Victims.Add(*It);
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
