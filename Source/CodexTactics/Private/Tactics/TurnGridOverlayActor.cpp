#include "Tactics/TurnGridOverlayActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tactics/GorkyGridManager.h"
#include "UObject/ConstructorHelpers.h"

ATurnGridOverlayActor::ATurnGridOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback"));
	if (Glow.Succeeded())
	{
		GlowMaterial = Glow.Object;
	}
	// Godot tactical_grid_overlay.gd colours (reachable green, attack red, enemy red 50 %, active cyan, warning yellow).
	struct FLayerSpec
	{
		ETurnOverlayLayer Layer;
		const TCHAR* Name;
		FLinearColor Color;
		float Intensity;
		float Height;
	};
	const FLayerSpec Specs[] = {
		{ ETurnOverlayLayer::Grid, TEXT("GridLayer"), FLinearColor(0.5f, 0.7f, 0.9f), 0.06f, 2.f },
		{ ETurnOverlayLayer::Reachable, TEXT("ReachableLayer"), FLinearColor(0.2f, 1.f, 0.4f), 0.3f, 3.f },
		{ ETurnOverlayLayer::Attack, TEXT("AttackLayer"), FLinearColor(1.f, 0.2f, 0.15f), 0.7f, 4.f },
		{ ETurnOverlayLayer::EnemyReach, TEXT("EnemyReachLayer"), FLinearColor(0.95f, 0.15f, 0.15f), 0.4f, 3.5f },
		{ ETurnOverlayLayer::Fear, TEXT("FearLayer"), FLinearColor(1.f, 0.45f, 0.1f), 0.5f, 4.5f },
		{ ETurnOverlayLayer::Active, TEXT("ActiveLayer"), FLinearColor(0.2f, 0.9f, 1.f), 1.2f, 5.f },
		{ ETurnOverlayLayer::Warning, TEXT("WarningLayer"), FLinearColor(1.f, 0.9f, 0.2f), 1.5f, 5.5f },
	};
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	for (const FLayerSpec& Spec : Specs)
	{
		UInstancedStaticMeshComponent* Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Spec.Name);
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetCanEverAffectNavigation(false);
		if (Plane.Succeeded())
		{
			Mesh->SetStaticMesh(Plane.Object);
		}
		OverlayLayers.Add(Spec.Layer, Mesh);
		LayerColors.Add(Spec.Layer, Spec.Color);
		LayerIntensity.Add(Spec.Layer, Spec.Intensity);
		LayerHeights.Add(Spec.Layer, Spec.Height);
	}
}

void ATurnGridOverlayActor::SetGrid(const UGorkyGridManager* InGrid)
{
	Grid = InGrid;
	for (const TPair<ETurnOverlayLayer, TObjectPtr<UInstancedStaticMeshComponent>>& Entry : OverlayLayers)
	{
		if (GlowMaterial && Entry.Value)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
			Instance->SetVectorParameterValue(TEXT("Color"), LayerColors[Entry.Key]);
			Instance->SetScalarParameterValue(TEXT("Intensity"), LayerIntensity[Entry.Key]);
			Entry.Value->SetMaterial(0, Instance);
		}
	}
	TArray<FIntPoint> All;
	if (InGrid)
	{
		for (int32 X = 0; X < InGrid->GridSize.X; ++X)
		{
			for (int32 Y = 0; Y < InGrid->GridSize.Y; ++Y)
			{
				All.Add(FIntPoint(X, Y));
			}
		}
	}
	SetCells(ETurnOverlayLayer::Grid, All);
}

void ATurnGridOverlayActor::SetCells(ETurnOverlayLayer Layer, const TArray<FIntPoint>& Cells)
{
	UInstancedStaticMeshComponent* Mesh = OverlayLayers.FindRef(Layer);
	const UGorkyGridManager* GridManager = Grid.Get();
	if (!Mesh)
	{
		return;
	}
	Mesh->ClearInstances();
	if (!GridManager)
	{
		return;
	}
	// The engine plane is 100 x 100 cm; tiles leave a small gap so the grid stays readable.
	const float Scale = GridManager->CellSize * (Layer == ETurnOverlayLayer::Grid ? 0.96f : 0.88f) / 100.f;
	TArray<FTransform> Transforms;
	Transforms.Reserve(Cells.Num());
	for (const FIntPoint& Cell : Cells)
	{
		Transforms.Add(FTransform(FRotator::ZeroRotator, GridManager->GridToWorld(Cell) + FVector(0.f, 0.f, LayerHeights[Layer]),
			FVector(Scale, Scale, 1.f)));
	}
	Mesh->AddInstances(Transforms, false, true);
}

int32 ATurnGridOverlayActor::GetCellCount(ETurnOverlayLayer Layer) const
{
	const UInstancedStaticMeshComponent* Mesh = OverlayLayers.FindRef(Layer);
	return Mesh ? Mesh->GetInstanceCount() : 0;
}
