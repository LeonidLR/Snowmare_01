#include "Combat/FirePuddleActor.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/PointLightComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/HeatSourceComponent.h"
#include "NiagaraSystem.h"
#include "Survival/TacticalHeatTimerComponent.h"

AFirePuddleActor::AFirePuddleActor()
{
	// Placeholder: a flat burning disc of the core's size (the ring is invisible warm air).
	const FTacticalHeatConfig Defaults;
	const float DiscScale = Defaults.PuddleCoreRadius / 50.f; // the engine cylinder is 100 cm wide
	SetPlaceholderShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(DiscScale, DiscScale, 0.03f), FVector(0.f, 0.f, 1.5f));
	BurningColor = FLinearColor(1.f, 0.42f, 0.05f);
	SpentColor = FLinearColor(0.05f, 0.04f, 0.04f);
	Light->SetLightColor(FLinearColor(1.f, 0.48f, 0.12f));
	Light->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	Light->SetAttenuationRadius(Defaults.PuddleHeatRadius * 1.6f);
	LightIntensity = 80.f;
	FadeSeconds = 3.f;
	SpentLifetime = 2.f;
	EffectAsset = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/Heat/NS_FirePuddle.NS_FirePuddle")));
	HeatSource->Radius = Defaults.PuddleHeatRadius;
	HeatSource->WarmthRate = Defaults.PuddleWarmthRate;
	Tags.Add(TEXT("FirePuddle"));
}

AFirePuddleActor* AFirePuddleActor::SpawnAt(UWorld* World, const FVector& GroundPoint, const FString& InSourceName, TSubclassOf<AFirePuddleActor> Class)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFirePuddleActor* Puddle = World->SpawnActor<AFirePuddleActor>(Class ? *Class : AFirePuddleActor::StaticClass(), GroundPoint,
		FRotator::ZeroRotator, Params);
	if (Puddle)
	{
		Puddle->SourceName = InSourceName.IsEmpty() ? Puddle->SourceName : InSourceName;
		Puddle->LightPuddle(TacticalHeatRules::GetPuddleLifetime(TacticalHeatTuning::Get(), FMath::FRand()));
	}
	return Puddle;
}

void AFirePuddleActor::ApplyTuning()
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	HeatSource->Radius = Config.PuddleHeatRadius;
	HeatSource->Tolerance = Config.HeatTolerance;
	HeatSource->WarmthRate = Config.PuddleWarmthRate;
}

void AFirePuddleActor::LightPuddle(float Lifetime)
{
	ApplyTuning();
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	// One turn-based round burns the whole puddle (PuddleTurnRounds 1).
	Ignite(Lifetime, TacticalHeatRules::SecondsPerRound(Lifetime, Config.PuddleTurnRounds));
	PulseClock = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] fire puddle at %s: core %.0f cm, ring %.0f cm, %.1f s"), *GetActorLocation().ToCompactString(),
		Config.PuddleCoreRadius, Config.PuddleHeatRadius, Lifetime);
}

void AFirePuddleActor::RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound)
{
	ApplyTuning();
	PulseClock = 0.f;
	Super::RestoreHeatState(bBurning, bBurntOut, TimeLeft, SecondsPerRound);
}

void AFirePuddleActor::HandleAdvanced(float DeltaSeconds)
{
	Super::HandleAdvanced(DeltaSeconds);
	const float Interval = FMath::Max(0.1f, TacticalHeatTuning::Get().PuddleDamageIntervalSeconds);
	PulseClock += DeltaSeconds;
	while (PulseClock >= Interval && IsBurning())
	{
		PulseClock -= Interval;
		ApplyCorePulse();
	}
}

int32 AFirePuddleActor::ApplyCorePulse()
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	const float Damage = Config.PuddleCoreDamagePerSecond * FMath::Max(0.1f, Config.PuddleDamageIntervalSeconds);
	if (Damage <= 0.f || !IsBurning())
	{
		return 0;
	}
	const FVector Center = GetActorLocation();
	auto InCore = [&Config, &Center](const AActor& Actor)
	{
		const FVector Feet = Actor.GetActorLocation() - FVector(0.f, 0.f, Actor.GetSimpleCollisionHalfHeight());
		return FMath::Abs(Feet.Z - Center.Z) < 150.f
			&& TacticalHeatRules::ClassifyPuddleZone(Config, FVector::Dist2D(Feet, Center)) == EFirePuddleZone::Core;
	};
	FDamageSpec Spec;
	Spec.Amount = Damage;
	Spec.DamageType = EDamageType::Fire;
	Spec.AttackerSource = SourceName;
	Spec.StatusEffect = Config.PuddleBurningSeconds > 0.f ? EStatusEffect::Burning : EStatusEffect::None;
	Spec.StatusDuration = Config.PuddleBurningSeconds;
	Spec.StatusTickDamage = Config.PuddleBurningTickDamage;
	int32 Hit = 0;
	TArray<AActor*> Victims;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FName(TEXT("Enemy"))) && InCore(**It))
		{
			Victims.Add(*It);
		}
	}
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && InCore(*Member))
			{
				Victims.Add(Member);
			}
		}
	}
	for (AActor* Victim : Victims)
	{
		UHealthComponent* Health = Victim ? Victim->FindComponentByClass<UHealthComponent>() : nullptr;
		if (Health && Health->IsAlive())
		{
			Health->TakeDamage(Spec);
			++Hit;
		}
	}
	return Hit;
}
