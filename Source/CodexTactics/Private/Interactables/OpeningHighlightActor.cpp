// Designer 2026-11-23: draw a low-intensity glow on the hovered opening.
#include "Interactables/OpeningHighlightActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace OpeningHighlightLocal
{
	constexpr float GlowIntensity = 0.6f;
	constexpr float CubeSizeCm = 100.f;
	const FName ColorParam(TEXT("Color"));
	const FName IntensityParam(TEXT("Intensity"));
}

AOpeningHighlightActor::AOpeningHighlightActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OpeningGlow"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowAsset(TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback"));
	GlowMaterial = GlowAsset.Object;
}

void AOpeningHighlightActor::ShowBox(const FBox& WorldBox, const FQuat& Rotation, const FLinearColor& Color)
{
	if (!WorldBox.IsValid || !GlowMaterial || !Mesh->GetStaticMesh())
	{
		Hide();
		return;
	}
	if (!Glow)
	{
		Glow = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		Glow->SetScalarParameterValue(OpeningHighlightLocal::IntensityParam, OpeningHighlightLocal::GlowIntensity);
		Mesh->SetMaterial(0, Glow);
	}
	Glow->SetVectorParameterValue(OpeningHighlightLocal::ColorParam, Color);
	const FTransform Transform(Rotation, WorldBox.GetCenter(), WorldBox.GetSize() / OpeningHighlightLocal::CubeSizeCm);
	if (!GetActorTransform().Equals(Transform))
	{
		SetActorTransform(Transform);
	}
	Mesh->SetVisibility(true);
}

void AOpeningHighlightActor::Hide()
{
	Mesh->SetVisibility(false);
}

bool AOpeningHighlightActor::IsShown() const
{
	return Mesh->IsVisible();
}

bool AOpeningHighlightActor::GetShownColor(FLinearColor& OutColor) const
{
	return IsShown() && Glow && Glow->GetVectorParameterValue(OpeningHighlightLocal::ColorParam, OutColor);
}
