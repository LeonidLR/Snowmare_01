#include "Interactables/RadiusRingSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

const FLinearColor URadiusRingSubsystem::Green(0.2f, 0.95f, 0.4f);
const FLinearColor URadiusRingSubsystem::Cyan(0.2f, 0.9f, 1.f);
const FLinearColor URadiusRingSubsystem::Red(1.f, 0.25f, 0.25f);

ARadiusRingActor::ARadiusRingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Ring = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(GetRootComponent());
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		Ring->SetStaticMesh(PlaneFinder.Object);
	}
}

void ARadiusRingActor::ShowRing(const FVector& Ground, float Radius, const FLinearColor& Color, float Width)
{
	SetActorHiddenInGame(false);
	if (!Material)
	{
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback")))
		{
			Material = UMaterialInstanceDynamic::Create(Glow, this);
			Material->SetScalarParameterValue(TEXT("Intensity"), 2.f);
			Ring->SetMaterial(0, Material);
		}
	}
	if (Material && !Color.Equals(ShownColor))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		ShownColor = Color;
	}
	const FVector Center = Ground + FVector(0.f, 0.f, 2.f); // Godot y 0.02
	if (Center.Equals(ShownCenter, 1.f) && FMath::IsNearlyEqual(Radius, ShownRadius, 1.f) && FMath::IsNearlyEqual(Width, ShownWidth))
	{
		return;
	}
	ShownCenter = Center;
	ShownRadius = Radius;
	ShownWidth = Width;
	TArray<FTransform> Segments;
	constexpr int32 Count = 72;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float A0 = 2.f * PI * Index / Count;
		const float A1 = 2.f * PI * (Index + 1) / Count;
		const FVector P0 = Center + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.f) * Radius;
		const FVector P1 = Center + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.f) * Radius;
		const FVector Delta = P1 - P0;
		Segments.Add(FTransform(Delta.Rotation(), (P0 + P1) * 0.5f, FVector(Delta.Size() / 100.f, Width / 100.f, 1.f)));
	}
	Ring->ClearInstances();
	Ring->AddInstances(Segments, false, true);
}

bool URadiusRingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URadiusRingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URadiusRingSubsystem, STATGROUP_Tickables);
}

void URadiusRingSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const URelocationSubsystem* Relocation = World ? World->GetSubsystem<URelocationSubsystem>() : nullptr;
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Flow || !Relocation || !Squad)
	{
		return;
	}
	const bool bPause = Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	FVector Center = FVector::ZeroVector;
	float Radius = 0.f;
	FLinearColor Color = Green;
	Mode = ERadiusRingMode::Hidden;
	const AOperativeCharacter* Worker = Relocation->GetPlacingWorker();
	if (bPause && Relocation->IsPlacing() && Worker)
	{
		Mode = Relocation->IsPlacingDeployable() ? ERadiusRingMode::Deploy : ERadiusRingMode::Relocation;
		Center = Relocation->GetOrigin(*Worker);
		Radius = Relocation->GetRadius(*Worker);
		Color = Relocation->IsGhostValid() ? (Mode == ERadiusRingMode::Deploy ? Green : Cyan) : Red;
		Center.Z = Worker->GetActorLocation().Z - Worker->GetSimpleCollisionHalfHeight();
	}
	else if (bPause && Squad->GetLeader())
	{
		const AOperativeCharacter* Leader = Squad->GetLeader();
		Mode = ERadiusRingMode::PauseOrders;
		Center = Leader->GetActorLocation();
		Squad->GetPauseOrigin(Leader, Center);
		Center.Z = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight();
		Radius = Flow->GetConfig().PauseOrderRadius;
	}
	if (Mode == ERadiusRingMode::Hidden)
	{
		if (Ring)
		{
			Ring->SetActorHiddenInGame(true);
		}
		return;
	}
	if (!Ring)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ring = World->SpawnActor<ARadiusRingActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (Ring)
	{
		Ring->ShowRing(Center, Radius, Color);
	}
}
