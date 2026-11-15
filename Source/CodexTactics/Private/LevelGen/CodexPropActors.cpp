#include "LevelGen/CodexPropActors.h"
#include "CodexTactics.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"

// =================================================================================================
// UCodexPropTrapComponent
// =================================================================================================

UCodexPropTrapComponent::UCodexPropTrapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bArmed = true;
	bInert = false;
}

void UCodexPropTrapComponent::Arm(const FString& InTrapMineId, bool bInInert)
{
	TrapMineId = InTrapMineId;
	bArmed = true;
	bInert = bInInert;
}

bool UCodexPropTrapComponent::TriggerTrap()
{
	if (!bArmed)
	{
		return false;
	}

	if (bInert)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("Props trap %s on crate triggered (INERT, no detonation)"), *TrapMineId);
		bArmed = false;
		return false;
	}

	UE_LOG(LogCodexTactics, Log, TEXT("Props trap %s on crate DETONATED (damage: %.1f, radius: %.1f cm)"),
		*TrapMineId, BlastDamage, BlastRadiusCm);
	bArmed = false;
	return true;
}

// =================================================================================================
// ACodexPropActor
// =================================================================================================

ACodexPropActor::ACodexPropActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SetCanBeDamaged(false);
}

void ACodexPropActor::InitProp(
	const FString& InPropId,
	const FString& InLayerId,
	const FString& InVariant,
	const FString& InWalkLayer,
	bool bInNavAffecting,
	const FVector& InExtent,
	TSubclassOf<UNavArea> InNavArea)
{
	PropId = InPropId;
	LayerId = InLayerId;
	Variant = InVariant;
	WalkLayer = InWalkLayer;
	bNavAffecting = bInNavAffecting;

	// Tags matching PROPS_LAYERS.md 3.2: CodexProp, prop_layer=<layer>, prop_id=<id>
	Tags.AddUnique(FName(TEXT("CodexProp")));
	if (!InLayerId.IsEmpty())
	{
		Tags.AddUnique(FName(*FString::Printf(TEXT("prop_layer=%s"), *InLayerId)));
	}
	if (!InPropId.IsEmpty())
	{
		Tags.AddUnique(FName(*FString::Printf(TEXT("prop_id=%s"), *InPropId)));
	}

	if (bNavAffecting)
	{
		if (!NavModifierComponent)
		{
			NavModifierComponent = NewObject<UNavModifierComponent>(this, TEXT("PropNavModifier"));
			if (NavModifierComponent)
			{
				TSubclassOf<UNavArea> AreaClass = InNavArea ? InNavArea : TSubclassOf<UNavArea>(UNavArea_Null::StaticClass());
				NavModifierComponent->SetAreaClass(AreaClass);
				const FVector SafeExtent = InExtent.IsNearlyZero() ? FVector(50.f, 50.f, 50.f) : InExtent;
				NavModifierComponent->FailsafeExtent = SafeExtent;
				NavModifierComponent->RegisterComponent();
			}
		}
	}
}

// =================================================================================================
// ACodexPropCrate
// =================================================================================================

ACodexPropCrate::ACodexPropCrate()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	HP = 100.f;
	bNavAffecting = true;
}

UCodexPropTrapComponent* ACodexPropCrate::EnsureTrapComponent()
{
	if (!TrapComponent)
	{
		TrapComponent = NewObject<UCodexPropTrapComponent>(this, TEXT("CrateTrapComponent"));
		if (TrapComponent)
		{
			TrapComponent->RegisterComponent();
			bDangerous = true;
		}
	}
	return TrapComponent;
}

// =================================================================================================
// ACodexPropBarrel
// =================================================================================================

ACodexPropBarrel::ACodexPropBarrel()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	HP = 60.f;
	bNavAffecting = true;
	bExplosive = false;
	ExplosionRadiusCm = 350.f;
	ExplosionDamage = 120.f;
}

// =================================================================================================
// ACodexPropTurret
// =================================================================================================

ACodexPropTurret::ACodexPropTurret()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	HP = 200.f;
	bNavAffecting = true;
	ArcDeg = 90.f;
	CoveredM = 0;
}

// =================================================================================================
// ACodexPropMine
// =================================================================================================

ACodexPropMine::ACodexPropMine()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bNavAffecting = false; // Mines never block navigation (PROPS_LAYERS.md 2.3)
	TriggerType = TEXT("proximity");
	WidthCm = 180.f;
	BlastRadiusCm = 250.f;
	BlastDamage = 90.f;
	bMinesInert = false;
}

void ACodexPropMine::SetupTrigger(const FString& InTriggerType, float InWidthCm, float InTriggerRadiusCm)
{
	TriggerType = InTriggerType;
	WidthCm = InWidthCm > 0.f ? InWidthCm : 180.f;

	if (InTriggerType.Equals(TEXT("tripwire"), ESearchCase::IgnoreCase))
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(this, TEXT("MineTripwireTrigger"));
		if (Box)
		{
			Box->SetupAttachment(RootComponent);
			// Half-extent: half of door width along Y/X, depth ~20 cm, height ~30 cm
			Box->SetBoxExtent(FVector(20.f, WidthCm * 0.5f, 30.f));
			Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
			Box->SetGenerateOverlapEvents(true);
			Box->RegisterComponent();
			TriggerShape = Box;
		}
	}
	else // Proximity
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(this, TEXT("MineProximityTrigger"));
		if (Sphere)
		{
			Sphere->SetupAttachment(RootComponent);
			const float Radius = InTriggerRadiusCm > 0.f ? InTriggerRadiusCm : 120.f;
			Sphere->SetSphereRadius(Radius);
			Sphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
			Sphere->SetGenerateOverlapEvents(true);
			Sphere->RegisterComponent();
			TriggerShape = Sphere;
		}
	}
}

// =================================================================================================
// ACodexPropLootPile
// =================================================================================================

ACodexPropLootPile::ACodexPropLootPile()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bNavAffecting = false; // Loot never blocks navigation (PROPS_LAYERS.md 2.4)
}
