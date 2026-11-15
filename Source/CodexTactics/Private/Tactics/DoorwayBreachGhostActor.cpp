#include "Tactics/DoorwayBreachGhostActor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DoorwaySubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tactics/CoverFacingRules.h"
#include "UObject/ConstructorHelpers.h"

ADoorwayBreachGhostActor::ADoorwayBreachGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Silhouette(TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
	if (Silhouette.Succeeded())
	{
		GhostMaterial = Silhouette.Object;
	}

	SetupSlotVisual(LeftVisual, TEXT("LeftBody"), TEXT("LeftPlaceholder"));
	SetupSlotVisual(RightVisual, TEXT("RightBody"), TEXT("RightPlaceholder"));
	SetupSlotVisual(CenterVisual, TEXT("CenterBody"), TEXT("CenterPlaceholder"));
	SetupSlotVisual(RearVisual, TEXT("RearBody"), TEXT("RearPlaceholder"));

	SetActorHiddenInGame(true);
}

void ADoorwayBreachGhostActor::SetupSlotVisual(FSlotVisual& Visual, const FName& BodyName, const FName& PlaceholderName)
{
	Visual.Body = CreateDefaultSubobject<USkeletalMeshComponent>(BodyName);
	Visual.Body->SetupAttachment(Root);
	Visual.Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual.Body->SetCastShadow(false);
	Visual.Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Visual.Body->SetVisibility(false);

	Visual.Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(PlaceholderName);
	Visual.Placeholder->SetupAttachment(Root);
	Visual.Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual.Placeholder->SetCastShadow(false);
	Visual.Placeholder->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Visual.Placeholder->SetStaticMesh(Cylinder.Object);
	}
}

void ADoorwayBreachGhostActor::ShowBreachPreview(
	const FDoorwayBreachTriad& InTriad,
	const TArray<AOperativeCharacter*>& InOperatives,
	bool bValid)
{
	TArray<FVector> Positions;
	for (const AOperativeCharacter* Op : InOperatives)
	{
		Positions.Add(Op ? Op->GetActorLocation() : FVector::ZeroVector);
	}

	const TArray<FDoorwaySlotAssignment> Assignments = DoorwayBreachRules::AssignSlots(Positions, InTriad);
	ShowBreachPreviewForAssignments(InTriad, Assignments, InOperatives, bValid);
}

void ADoorwayBreachGhostActor::ShowBreachPreviewForAssignments(
	const FDoorwayBreachTriad& InTriad,
	const TArray<FDoorwaySlotAssignment>& InAssignments,
	const TArray<AOperativeCharacter*>& InOperatives,
	bool bValid)
{
	Triad = InTriad;
	bShown = true;

	HideSlotVisual(LeftVisual);
	HideSlotVisual(RightVisual);
	HideSlotVisual(CenterVisual);
	HideSlotVisual(RearVisual);

	const FLinearColor ValidColor(0.f, 0.85f, 1.f, 0.55f); // Cyan
	const FLinearColor WarningColor(1.f, 0.6f, 0.f, 0.65f); // Amber

	for (const FDoorwaySlotAssignment& Assignment : InAssignments)
	{
		const AOperativeCharacter* Operative = InOperatives.IsValidIndex(Assignment.OperativeIndex)
			? InOperatives[Assignment.OperativeIndex]
			: nullptr;

		const FLinearColor Color = (bValid && !Assignment.Slot.bBlocked) ? ValidColor : WarningColor;

		switch (Assignment.Slot.Role)
		{
		case EDoorwayBreachSlotRole::LeftJamb:
			UpdateSlotVisual(LeftVisual, Assignment.Slot, Operative, Color);
			break;
		case EDoorwayBreachSlotRole::RightJamb:
			UpdateSlotVisual(RightVisual, Assignment.Slot, Operative, Color);
			break;
		case EDoorwayBreachSlotRole::CenterPoint:
			UpdateSlotVisual(CenterVisual, Assignment.Slot, Operative, Color);
			break;
		case EDoorwayBreachSlotRole::RearSecurity:
			UpdateSlotVisual(RearVisual, Assignment.Slot, Operative, Color);
			break;
		}
	}

	SetActorHiddenInGame(false);
}

void ADoorwayBreachGhostActor::UpdateSlotVisual(
	FSlotVisual& Visual,
	const FDoorwayBreachSlot& Slot,
	const AOperativeCharacter* Operative,
	const FLinearColor& Color)
{
	Visual.bActive = true;

	UMaterialInterface* BaseMaterial = (Operative && Operative->SilhouetteMaterial)
		? Operative->SilhouetteMaterial.Get()
		: GhostMaterial.Get();

	if (BaseMaterial && (!Visual.Material || Visual.Material->Parent != BaseMaterial))
	{
		Visual.Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	}
	if (Visual.Material)
	{
		Visual.Material->SetVectorParameterValue(TEXT("Color"), Color);
	}

	const float HalfHeight = Operative ? Operative->GetSimpleCollisionHalfHeight() : 96.f;
	const FVector WorldPos = Slot.Location + FVector(0.f, 0.f, HalfHeight);

	const USkeletalMeshComponent* Source = Operative ? Operative->GetMesh() : nullptr;
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;

	if (Asset)
	{
		Visual.Body->SetSkeletalMesh(Asset);
		Visual.Body->SetWorldLocationAndRotation(WorldPos, Slot.Rotation);
		Visual.Body->SetRelativeTransform(Source->GetRelativeTransform());
		Visual.Body->SetWorldLocationAndRotation(WorldPos, Slot.Rotation);
		Visual.Body->SetVisibility(true);
		Visual.Placeholder->SetVisibility(false);

		for (int32 Index = 0; Visual.Material && Index < Visual.Body->GetNumMaterials(); ++Index)
		{
			Visual.Body->SetMaterial(Index, Visual.Material);
		}
		Visual.Body->SetOverlayMaterial(Visual.Material);

		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source->GetAnimInstance());
		UAnimSequenceBase* Clip = nullptr;
		if (Anim)
		{
			if (Slot.Role == EDoorwayBreachSlotRole::LeftJamb || Slot.Role == EDoorwayBreachSlotRole::RightJamb)
			{
				const TArray<TObjectPtr<UAnimSequenceBase>>& Idles = Anim->CoverCrouchFireIdle;
				const int32 Wanted = CoverFacingRules::ClipIndex(Slot.PeekFacing);
				Clip = Idles.IsValidIndex(Wanted) && Idles[Wanted] ? Idles[Wanted].Get() : nullptr;
				for (int32 Index = 0; !Clip && Index < Idles.Num(); ++Index)
				{
					Clip = Idles[Index].Get();
				}
			}
			if (!Clip)
			{
				Clip = Anim->Animations.CrouchIdle.Get();
			}
			if (!Clip)
			{
				Clip = Anim->Animations.Idle.Get();
			}
		}

		if (Clip)
		{
			Visual.Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Visual.Body->SetAnimation(Clip);
			Visual.Body->Play(true);
		}
	}
	else
	{
		Visual.Body->SetVisibility(false);
		Visual.Placeholder->SetVisibility(true);
		Visual.Placeholder->SetWorldLocationAndRotation(WorldPos, Slot.Rotation);
		Visual.Placeholder->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.2f));

		if (Visual.Material)
		{
			Visual.Placeholder->SetMaterial(0, Visual.Material);
			Visual.Placeholder->SetOverlayMaterial(Visual.Material);
		}
	}
}

void ADoorwayBreachGhostActor::HideSlotVisual(FSlotVisual& Visual)
{
	Visual.bActive = false;
	if (Visual.Body)
	{
		Visual.Body->Stop();
		Visual.Body->SetVisibility(false);
	}
	if (Visual.Placeholder)
	{
		Visual.Placeholder->SetVisibility(false);
	}
}

void ADoorwayBreachGhostActor::HideBreachPreview()
{
	bShown = false;
	HideSlotVisual(LeftVisual);
	HideSlotVisual(RightVisual);
	HideSlotVisual(CenterVisual);
	HideSlotVisual(RearVisual);
	SetActorHiddenInGame(true);
}

int32 ADoorwayBreachGhostActor::GetVisibleGhostCount() const
{
	if (!bShown)
	{
		return 0;
	}
	int32 Count = 0;
	Count += LeftVisual.bActive ? 1 : 0;
	Count += RightVisual.bActive ? 1 : 0;
	Count += CenterVisual.bActive ? 1 : 0;
	Count += RearVisual.bActive ? 1 : 0;
	return Count;
}

namespace DoorwayBreachGhostConsole
{
	static void RunShowBreachGhost(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UDoorwaySubsystem* Subsystem = World->GetSubsystem<UDoorwaySubsystem>();
		if (!Subsystem)
		{
			return;
		}

		ADoorwayBreachGhostActor* GhostActor = nullptr;
		for (TActorIterator<ADoorwayBreachGhostActor> It(World); It; ++It)
		{
			GhostActor = *It;
			break;
		}
		if (!GhostActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GhostActor = World->SpawnActor<ADoorwayBreachGhostActor>(SpawnParams);
		}

		if (Args.Num() > 0 && Args[0].Equals(TEXT("hide"), ESearchCase::IgnoreCase))
		{
			if (GhostActor)
			{
				GhostActor->HideBreachPreview();
			}
			return;
		}

		FRuntimeDoorway TargetDoor;
		if (Args.Num() > 0)
		{
			const FRuntimeDoorway* Found = Subsystem->FindDoorwayById(Args[0]);
			if (Found)
			{
				TargetDoor = *Found;
			}
		}
		else
		{
			const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (Leader)
			{
				Subsystem->FindDoorwayNear(Leader->GetActorLocation(), 50000.f, TargetDoor);
			}
			else if (Subsystem->GetDoorwayCount() > 0)
			{
				TargetDoor = Subsystem->GetAllDoorways()[0];
			}
		}

		if (TargetDoor.Id.IsEmpty())
		{
			return;
		}

		FDoorwayBreachTriad Triad;
		Subsystem->GetBreachTriadForDoorway(TargetDoor, Triad);

		TArray<AOperativeCharacter*> Operatives;
		if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->IsPlayerTeam())
				{
					Operatives.Add(Member);
				}
			}
		}

		GhostActor->ShowBreachPreview(Triad, Operatives, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs ShowBreachGhostCmd(
		TEXT("Codex.Test.ShowBreachGhost"),
		TEXT("Displays holographic breach triad preview at a doorway: Codex.Test.ShowBreachGhost [doorway_id | hide]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShowBreachGhost));
}
