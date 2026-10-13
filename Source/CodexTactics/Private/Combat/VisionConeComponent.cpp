#include "Combat/VisionConeComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace VisionConeConsole
{
	static TAutoConsoleVariable<int32> CVarVisionCones(TEXT("Codex.Debug.VisionCones"), -1,
		TEXT("Vision cones on the ground (operatives and enemies): 0 all off, 1 all on, -1 each character's bShowVisionCone (default)."));
	static TAutoConsoleVariable<int32> CVarPlayerEnemyCones(TEXT("Codex.VisionCones.Enemies"), 0,
		TEXT("The player's enemy vision-cone toggle (O / pause menu): 1 every living enemy's cone is drawn, 0 each enemy's own setting (default)."));
}

UVisionConeComponent::UVisionConeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false; // the owner drives it (UpdateCone)
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	bUseAsyncCooking = false;
	SetVisibility(false);
	FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent")));
}

int32 UVisionConeComponent::GetGlobalMode()
{
	return VisionConeConsole::CVarVisionCones.GetValueOnGameThread();
}

bool UVisionConeComponent::IsPlayerEnemyConesOn()
{
	return VisionConeConsole::CVarPlayerEnemyCones.GetValueOnGameThread() != 0;
}

void UVisionConeComponent::SetPlayerEnemyCones(bool bOn)
{
	VisionConeConsole::CVarPlayerEnemyCones->Set(bOn ? 1 : 0, ECVF_SetByCode);
}

int32 UVisionConeComponent::GetFillVertexCount() const
{
	const FProcMeshSection* Section = const_cast<UVisionConeComponent*>(this)->GetProcMeshSection(0);
	return Section ? Section->ProcVertexBuffer.Num() : 0;
}

void UVisionConeComponent::UpdateCone(bool bShow, float RangeCm, float HalfAngleDeg, const FLinearColor& TargetColor, float DeltaSeconds)
{
	if (!bShow || RangeCm <= 0.f)
	{
		if (bShown)
		{
			bShown = false;
			SetVisibility(false);
		}
		return;
	}
	const float Half = VisionConeRules::ClampHalfAngle(HalfAngleDeg);
	if (FMath::Abs(RangeCm - BuiltRange) > 1.f || FMath::Abs(Half - BuiltHalfAngle) > 0.1f)
	{
		Rebuild(RangeCm, Half);
	}
	ShownColor = bColorInitialised ? VisionConeRules::StepColor(ShownColor, TargetColor, DeltaSeconds, ColorLerpSpeed) : TargetColor;
	bColorInitialised = true;
	ApplyColor();
	PlaceAtFeet();
	if (!bShown)
	{
		bShown = true;
		SetVisibility(true);
	}
}

void UVisionConeComponent::Rebuild(float RangeCm, float HalfAngleDeg)
{
	BuiltRange = RangeCm;
	BuiltHalfAngle = HalfAngleDeg;
	ClearAllMeshSections();
	auto AddSection = [this](int32 Index, const FVisionConeMesh& Mesh, float Alpha)
	{
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		Normals.Init(FVector::UpVector, Mesh.Vertices.Num());
		Colors.Init(FLinearColor(1.f, 1.f, 1.f, Alpha), Mesh.Vertices.Num());
		Tangents.Init(FProcMeshTangent(1.f, 0.f, 0.f), Mesh.Vertices.Num());
		UV.Reserve(Mesh.Vertices.Num());
		for (const FVector& Vertex : Mesh.Vertices)
		{
			UV.Add(FVector2D(Vertex.X / FMath::Max(BuiltRange, 1.f), 0.5f + Vertex.Y / FMath::Max(2.f * BuiltRange, 1.f)));
		}
		CreateMeshSection_LinearColor(Index, Mesh.Vertices, Mesh.Triangles, Normals, UV, Colors, Tangents, /*bCreateCollision*/ false);
	};
	// The mesh lies at local Z 0; PlaceAtFeet moves the component to the feet + HeightAboveFeetCm.
	AddSection(0, VisionConeRules::BuildFill(RangeCm, HalfAngleDeg, Segments), FillAlpha);
	if (bDrawOutline)
	{
		// A hair above the fill (Godot 0.5 cm) so the band never z-fights with it.
		AddSection(1, VisionConeRules::BuildOutline(RangeCm, HalfAngleDeg, Segments, OutlineWidthCm, 0.5f), OutlineAlpha);
	}

	UMaterialInterface* Base = ConeMaterial ? ConeMaterial.Get() : (FallbackMaterial.IsNull() ? nullptr : FallbackMaterial.LoadSynchronous());
	if (Base)
	{
		if (!FillMaterial || FillMaterial->Parent != Base)
		{
			FillMaterial = UMaterialInstanceDynamic::Create(Base, this);
			LineMaterial = UMaterialInstanceDynamic::Create(Base, this);
		}
		SetMaterial(0, FillMaterial);
		if (bDrawOutline)
		{
			SetMaterial(1, LineMaterial);
		}
	}
	ApplyColor();
}

void UVisionConeComponent::ApplyColor()
{
	auto Push = [this](UMaterialInstanceDynamic* Material, float Alpha)
	{
		if (!Material)
		{
			return;
		}
		if (!ColorParameterName.IsNone())
		{
			Material->SetVectorParameterValue(ColorParameterName, FLinearColor(ShownColor.R, ShownColor.G, ShownColor.B, Alpha));
		}
		if (!OpacityParameterName.IsNone())
		{
			Material->SetScalarParameterValue(OpacityParameterName, Alpha);
		}
	};
	Push(FillMaterial, FillAlpha);
	Push(LineMaterial, OutlineAlpha);
}

void UVisionConeComponent::PlaceAtFeet()
{
	const AActor* Owner = GetOwner();
	const UCapsuleComponent* Capsule = Owner ? Cast<UCapsuleComponent>(Owner->GetRootComponent()) : nullptr;
	const float FeetZ = Capsule && GetAttachParent() == Capsule ? -Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
	const FVector Wanted(0.f, 0.f, FeetZ + HeightAboveFeetCm);
	if (!GetRelativeLocation().Equals(Wanted, 0.5f))
	{
		SetRelativeLocation(Wanted);
	}
}
