#include "Interactables/CampfireActor.h"

#include "Camera/CameraZoneVolume.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/HeatSourceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Survival/TacticalHeatActor.h"
#include "Survival/TacticalHeatTimerComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "CampfireActor"

ACampfireActor::ACampfireActor()
{
	DeployableType = EDeployableType::Campfire;
	DisplayName = LOCTEXT("CampfireName", "Campfire");
	bDeployable = false; // ashes stay; it cannot be packed up
	bCanBeRelocated = false;
	// Placeholder: a ring of logs 1.1 m wide, 30 cm high (the box is the walk / click shape).
	Box->SetBoxExtent(FVector(55.f, 55.f, 15.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.f); // also on the CDO: the placement ghost copies it

	const FTacticalHeatConfig Defaults;
	HeatSource->Radius = Defaults.CampfireHeatRadius;
	HeatSource->Tolerance = Defaults.HeatTolerance;
	HeatSource->bHeatActive = false;
	HeatSource->SetRelativeLocation(FVector(0.f, 0.f, 90.f - 15.f)); // body height (the zone is a sphere, operatives are measured at mid-body)

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(Box);
	FireLight->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	FireLight->SetLightColor(FLinearColor(1.f, 0.52f, 0.16f));
	FireLight->SetAttenuationRadius(900.f);
	FireLight->SetIntensityUnits(ELightUnits::Candelas);
	FireLight->SetIntensity(FireLightIntensity);
	FireLight->SetCastShadows(false);
	FireLight->SetVisibility(false);

	FireEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireEffect"));
	FireEffect->SetupAttachment(Box);
	FireEffect->SetAutoActivate(false);
	FireEffectAsset = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/Heat/NS_Campfire.NS_Campfire")));

	Timer = CreateDefaultSubobject<UTacticalHeatTimerComponent>(TEXT("Timer"));
	Tags.Add(TEXT("Campfire"));
}

void ACampfireActor::BeginPlay()
{
	Super::BeginPlay();
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	HeatSource->Radius = Config.CampfireHeatRadius;
	HeatSource->Tolerance = Config.HeatTolerance;
	Timer->OnAdvanced.AddUObject(this, &ACampfireActor::HandleAdvanced);
	Timer->OnBurntOut.AddUObject(this, &ACampfireActor::HandleBurntOut);
	ApplyVisuals();
}

bool ACampfireActor::IsBurning() const
{
	return Timer && Timer->IsBurning();
}

bool ACampfireActor::IsBurntOut() const
{
	return Timer && Timer->IsBurntOut();
}

float ACampfireActor::GetTimeLeft() const
{
	return Timer ? Timer->GetTimeLeft() : 0.f;
}

void ACampfireActor::OnDeployedBy(AOperativeCharacter* Worker)
{
	Kindle(Worker);
}

void ACampfireActor::Kindle(AOperativeCharacter* Worker)
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	const float GroundZ = GetActorLocation().Z - Box->GetScaledBoxExtent().Z;
	bWindExposed = TacticalHeatRules::IsWindExposed(Config, GroundZ);
	const float Burn = TacticalHeatRules::GetCampfireBurnSeconds(Config, bWindExposed);
	Timer->Ignite(Burn, TacticalHeatRules::SecondsPerRound(Burn, Config.CampfireTurnRounds));
	NoiseClock = 0.f;
	ApplyVisuals();
	const FText Who = Worker ? Worker->DisplayName : LOCTEXT("Squad", "SQUAD");
	PostLine(Who, bWindExposed
		? FText::Format(LOCTEXT("KindledWind", "🔥 Campfire lit - the wind up here eats the wood: {0} s of warmth within {1} m. The smoke can be seen from afar!"),
			FMath::RoundToInt(Burn), FText::AsNumber(Config.CampfireHeatRadius / 100.f))
		: FText::Format(LOCTEXT("Kindled", "🔥 Campfire lit: {0} s of warmth within {1} m. The crackle and the smoke can give us away!"),
			FMath::RoundToInt(Burn), FText::AsNumber(Config.CampfireHeatRadius / 100.f)));
	UFloatingTextSubsystem* Floating = GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr;
	if (Floating)
	{
		Floating->Spawn(GetActorLocation() + FVector(0.f, 0.f, 160.f), TEXT("🔥 CAMPFIRE"), FLinearColor(1.f, 0.6f, 0.2f), 1.5f, 60.f);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] campfire %s kindled: %.1f s (wind %d), radius %.0f"), *GetName(), Burn, bWindExposed ? 1 : 0,
		HeatSource->Radius);
	AlertPatrols();
}

void ACampfireActor::RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound)
{
	Timer->Restore(bBurning, bBurntOut, TimeLeft, SecondsPerRound);
	NoiseClock = 0.f;
	ApplyVisuals();
}

bool ACampfireActor::IsIndoors() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if ((It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter)
			&& It->ContainsLocation(GetActorLocation()))
		{
			return true;
		}
	}
	return false;
}

int32 ACampfireActor::AlertPatrols()
{
	const float Radius = TacticalHeatRules::GetCampfireNoiseRadius(TacticalHeatTuning::Get(), IsIndoors());
	if (Radius <= 0.f || !IsBurning())
	{
		return 0;
	}
	TArray<AEnemyCharacter*> Heard;
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsOnPatrol() && !It->IsSearching() && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= Radius)
		{
			Heard.Add(*It);
		}
	}
	for (AEnemyCharacter* Enemy : Heard)
	{
		if (IsValid(Enemy) && !Enemy->IsSearching())
		{
			Enemy->StartPatrolSearch(GetActorLocation());
		}
	}
	if (!Heard.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Heat] campfire %s: %d patrol(s) noticed the smoke and search"), *GetName(), Heard.Num());
	}
	return Heard.Num();
}

void ACampfireActor::HandleAdvanced(float DeltaSeconds)
{
	NoiseClock += DeltaSeconds;
	const float Interval = FMath::Max(0.5f, TacticalHeatTuning::Get().CampfireNoiseIntervalSeconds);
	if (NoiseClock >= Interval)
	{
		NoiseClock = FMath::Fmod(NoiseClock, Interval);
		AlertPatrols();
	}
	const float TimeLeft = GetTimeLeft();
	const float Strength = TimeLeft < 8.f ? FMath::Clamp(TimeLeft / 8.f, 0.f, 1.f) : 1.f;
	FireLight->SetIntensity(FireLightIntensity * Strength * FMath::FRandRange(0.85f, 1.15f));
}

void ACampfireActor::HandleBurntOut()
{
	ApplyVisuals();
	PostLine(LOCTEXT("Squad", "SQUAD"), LOCTEXT("BurntOut", "🪵 The campfire has burnt down to ashes."));
}

void ACampfireActor::ApplyVisuals()
{
	const bool bBurning = IsBurning();
	HeatSource->SetHeatActive(bBurning);
	FireLight->SetVisibility(bBurning);
	FireLight->SetIntensity(FireLightIntensity);
	ATacticalHeatActor::SetEffectActive(FireEffect, FireEffectAsset, bBurning);
	if (!BodyMaterial)
	{
		BodyMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), bBurning ? EmberColor : (IsBurntOut() ? AshColor : LogColor));
	}
	if (bBurning != bLastBurning)
	{
		bLastBurning = bBurning;
		ReceiveBurningChanged(bBurning);
	}
}

FActionMenuRequest ACampfireActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	if (IsBurning())
	{
		return FActionMenuRequest::MakeMessage(LOCTEXT("Squad", "SQUAD"), FText::Format(LOCTEXT("BurningMsg",
			"🔥 The campfire burns ({0} s left). Stay within {1} m to warm up."), FMath::CeilToInt(GetTimeLeft()),
			FText::AsNumber(Config.CampfireHeatRadius / 100.f)));
	}
	const int32 Craftable = Leader ? Leader->GetDeployableCount(EDeployableType::Campfire) : 0;
	return FActionMenuRequest::MakeMenu(LOCTEXT("AshesTitle", "🪵 Campfire ashes"),
		FText::Format(LOCTEXT("AshesDesc", "Rekindle the fire: {0} match + {1} firewood (or {2} units of flamethrower fuel).\n{3} can make {4}."),
			Config.CampfireMatchesCost, Config.CampfireFirewoodCost, Config.CampfireFlameFuelCost,
			Leader ? Leader->DisplayName : LOCTEXT("Nobody", "Nobody"), Craftable),
		Craftable > 0 ? LOCTEXT("Rekindle", "Rekindle") : LOCTEXT("NoMaterials", "No materials"), LOCTEXT("Cancel", "Cancel"), Craftable <= 0);
}

void ACampfireActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User || IsBurning())
	{
		return;
	}
	if (User->GetDeployableCount(EDeployableType::Campfire) <= 0)
	{
		PostLine(User->DisplayName, LOCTEXT("NoMats", "I have nothing to light it with (a match and firewood needed)."));
		return;
	}
	User->AddDeployable(EDeployableType::Campfire, -1);
	Kindle(User);
}

#undef LOCTEXT_NAMESPACE
