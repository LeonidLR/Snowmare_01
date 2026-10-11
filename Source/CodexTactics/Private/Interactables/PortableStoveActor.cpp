#include "Interactables/PortableStoveActor.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/World.h"
#include "Interactables/HeatSourceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Survival/TacticalHeatActor.h"
#include "Survival/TacticalHeatTimerComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "PortableStoveActor"

APortableStoveActor::APortableStoveActor()
{
	DeployableType = EDeployableType::PortableStove;
	DisplayName = LOCTEXT("StoveName", "Portable gas stove");
	bDeployable = true;
	bCanBeRelocated = true;
	// Placeholder: a 50 x 40 x 30 cm camping stove box (the base cube fills the box).
	Box->SetBoxExtent(FVector(25.f, 20.f, 15.f));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.f); // also on the CDO: the placement ghost copies it

	const FTacticalHeatConfig Defaults;
	HeatSource->Radius = Defaults.StoveHeatRadius;
	HeatSource->Tolerance = Defaults.HeatTolerance;
	HeatSource->bHeatActive = false;
	HeatSource->SetRelativeLocation(FVector(0.f, 0.f, 90.f - 15.f)); // body height (the zone is a sphere, operatives are measured at mid-body)
	DismantleSeconds = Defaults.StovePackUpSeconds;

	FlameLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlameLight"));
	FlameLight->SetupAttachment(Box);
	FlameLight->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	FlameLight->SetLightColor(FLinearColor(0.35f, 0.55f, 1.f));
	FlameLight->SetAttenuationRadius(350.f);
	FlameLight->SetIntensityUnits(ELightUnits::Candelas);
	FlameLight->SetIntensity(FlameLightIntensity);
	FlameLight->SetCastShadows(false);
	FlameLight->SetVisibility(false);

	FlameEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlameEffect"));
	FlameEffect->SetupAttachment(Box);
	FlameEffect->SetRelativeLocation(FVector(0.f, 0.f, 15.f));
	FlameEffect->SetAutoActivate(false);
	FlameEffectAsset = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/Heat/NS_PortableStove.NS_PortableStove")));

	Timer = CreateDefaultSubobject<UTacticalHeatTimerComponent>(TEXT("Timer"));
	Tags.Add(TEXT("PortableStove"));
}

void APortableStoveActor::BeginPlay()
{
	Super::BeginPlay();
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	HeatSource->Radius = Config.StoveHeatRadius;
	HeatSource->Tolerance = Config.HeatTolerance;
	DismantleSeconds = Config.StovePackUpSeconds;
	Timer->OnBurntOut.AddUObject(this, &APortableStoveActor::HandleBurntOut);
	ApplyVisuals();
}

bool APortableStoveActor::IsBurning() const
{
	return Timer && Timer->IsBurning();
}

float APortableStoveActor::GetGasLeft() const
{
	return Timer ? Timer->GetTimeLeft() : 0.f;
}

void APortableStoveActor::OnDeployedBy(AOperativeCharacter* Worker)
{
	float Gas = 0.f;
	if (Worker)
	{
		Gas = Worker->StoveGasSeconds; // the canister leaves his supply with the stove
		Worker->StoveGasSeconds = 0.f;
	}
	StartWithGas(Gas);
	if (Worker)
	{
		PostLine(Worker->DisplayName, Gas > 0.f
			? FText::Format(LOCTEXT("Deployed", "🔵 Gas stove set up: a quiet blue flame, warmth within {0} m (gas: {1} s)."),
				FText::AsNumber(HeatSource->Radius / 100.f), FMath::RoundToInt(Gas))
			: LOCTEXT("DeployedEmpty", "The stove is set up, but the gas canister is empty."));
	}
}

void APortableStoveActor::StartWithGas(float Gas)
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	if (Gas > 0.f)
	{
		Timer->Ignite(Gas, Config.StoveGasSecondsPerRound);
	}
	else
	{
		Timer->Restore(false, true, 0.f, Config.StoveGasSecondsPerRound);
	}
	ApplyVisuals();
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] stove %s: gas %.1f s, radius %.0f"), *GetName(), Gas, HeatSource->Radius);
}

void APortableStoveActor::RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound)
{
	Timer->Restore(bBurning, bBurntOut, TimeLeft, SecondsPerRound);
	ApplyVisuals();
}

void APortableStoveActor::OnPickedUpBy(AOperativeCharacter& Recipient)
{
	Recipient.StoveGasSeconds = GetGasLeft(); // the rest of the canister stays with the packed stove
	Timer->Stop();
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] stove packed up by %s with %.1f s of gas"), *Recipient.DisplayName.ToString(), Recipient.StoveGasSeconds);
}

void APortableStoveActor::HandleBurntOut()
{
	ApplyVisuals();
	PostLine(LOCTEXT("Squad", "SQUAD"), LOCTEXT("OutOfGas", "The gas stove sputters out - the canister is empty."));
}

void APortableStoveActor::ApplyVisuals()
{
	const bool bBurning = IsBurning();
	HeatSource->SetHeatActive(bBurning);
	FlameLight->SetVisibility(bBurning);
	ATacticalHeatActor::SetEffectActive(FlameEffect, FlameEffectAsset, bBurning);
	if (!BodyMaterial)
	{
		BodyMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), bBurning ? LitColor : BodyColor);
	}
	if (bBurning != bLastBurning)
	{
		bLastBurning = bBurning;
		ReceiveBurningChanged(bBurning);
	}
}

void APortableStoveActor::DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
	bool& bOutDisabled) const
{
	OutTitle = LOCTEXT("MenuTitle", "🔵 Portable gas stove");
	OutDescription = FText::Format(IsBurning()
			? LOCTEXT("MenuBurning", "Burning quietly, no smoke. Gas left: {0} s.\nPack up: the stove and its canister go back into the inventory.")
			: LOCTEXT("MenuCold", "Cold. Gas left: {0} s.\nPack up: the stove and its canister go back into the inventory."),
		FMath::RoundToInt(GetGasLeft()));
	OutConfirm = LOCTEXT("PackUp", "Pack up");
	bOutDisabled = false;
}

#undef LOCTEXT_NAMESPACE
