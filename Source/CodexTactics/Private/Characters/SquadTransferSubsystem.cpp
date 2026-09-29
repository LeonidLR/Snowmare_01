#include "Characters/SquadTransferSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interactables/DeployableRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "SquadTransferSubsystem"

namespace
{
	/** Godot: a click within 2.2 m of a squad mate counts as a click on it. */
	constexpr float TransferPickRadius = 220.f;
	/** Godot torus 0.75..1.05 m: the ring at its mean radius. */
	constexpr float TransferRingRadius = 90.f;
}

ATransferCursorActor::ATransferCursorActor()
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

void ATransferCursorActor::ShowAt(const FVector& Ground, bool bOverMate)
{
	if (!bMaterialReady)
	{
		bMaterialReady = true;
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback")))
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Glow, this);
			Instance->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.88f, 0.35f, 1.f)); // Godot emission
			Instance->SetScalarParameterValue(TEXT("Intensity"), 3.f);
			Ring->SetMaterial(0, Instance);
		}
	}
	const float Radius = TransferRingRadius * (bOverMate ? 1.25f : 0.9f);
	const FVector Center = Ground + FVector(0.f, 0.f, 15.f);
	TArray<FTransform> Segments;
	constexpr int32 Count = 36;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float A0 = 2.f * PI * Index / Count;
		const float A1 = 2.f * PI * (Index + 1) / Count;
		const FVector P0 = Center + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.f) * Radius;
		const FVector P1 = Center + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.f) * Radius;
		const FVector Delta = P1 - P0;
		Segments.Add(FTransform(Delta.Rotation(), (P0 + P1) * 0.5f, FVector(Delta.Size() / 100.f, 0.3f, 1.f)));
	}
	Ring->ClearInstances();
	Ring->AddInstances(Segments, false, true);
	SetActorHiddenInGame(false);
}

bool USquadTransferSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USquadTransferSubsystem::Post(const FText& Speaker, const FString& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, FText::FromString(Text));
	}
}

void USquadTransferSubsystem::StartTransferMode(ETransferItem InItem)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad || !Squad->GetLeader())
	{
		return;
	}
	bTransferring = true;
	Item = InItem;
	if (!Cursor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Cursor = GetWorld()->SpawnActor<ATransferCursorActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (Cursor)
	{
		const AOperativeCharacter* Leader = Squad->GetLeader();
		Cursor->ShowAt(Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight()), false);
	}
	Post(LOCTEXT("Transfer", "Передача"), FString::Printf(TEXT("🟣 Наведите фиолетовый круг на соратника и кликните ЛКМ, чтобы передать %s (ПКМ / Esc — отмена)."),
		*TransferRules::GetPromptName(InItem)));
}

void USquadTransferSubsystem::CancelTransferMode()
{
	bTransferring = false;
	if (Cursor)
	{
		Cursor->SetActorHiddenInGame(true);
	}
}

AOperativeCharacter* USquadTransferSubsystem::FindMate(const FVector& CursorPoint, AActor* HitActor, bool bAllowLeader) const
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}
	const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	if (AOperativeCharacter* Hit = Cast<AOperativeCharacter>(HitActor); Hit && Members.Contains(Hit) && (bAllowLeader || Hit != Squad->GetLeader()))
	{
		return Hit;
	}
	for (AOperativeCharacter* Member : Members)
	{
		if (Member != Squad->GetLeader() && FVector::Dist2D(Member->GetActorLocation(), CursorPoint) <= TransferPickRadius)
		{
			return Member;
		}
	}
	return nullptr;
}

void USquadTransferSubsystem::UpdatePreview(const FVector& CursorPoint, AActor* HitActor)
{
	if (!bTransferring || !Cursor)
	{
		return;
	}
	const AOperativeCharacter* Hovered = Cast<AOperativeCharacter>(HitActor);
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (Hovered && Squad && Squad->GetMembers().Contains(Hovered) && Hovered != Squad->GetLeader())
	{
		Cursor->ShowAt(Hovered->GetActorLocation() - FVector(0.f, 0.f, Hovered->GetSimpleCollisionHalfHeight()), true);
	}
	else
	{
		Cursor->ShowAt(CursorPoint, false);
	}
}

bool USquadTransferSubsystem::HandleClick(const FVector& CursorPoint, AActor* HitActor)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!bTransferring || !Leader)
	{
		return false;
	}
	AOperativeCharacter* Target = FindMate(CursorPoint, HitActor, true);
	if (!Target)
	{
		Post(LOCTEXT("Transfer", "Передача"), TEXT("Кликните непосредственно по соратнику, которому хотите передать предмет!"));
		return false;
	}
	if (Target == Leader)
	{
		Post(LOCTEXT("Transfer", "Передача"), TEXT("Нельзя передать предмет самому себе! Выберите напарника."));
		return false;
	}
	const bool bDone = TransferItem(Leader, Target, Item);
	CancelTransferMode();
	return bDone;
}

bool USquadTransferSubsystem::TransferItem(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem)
{
	if (!Sender || !Recipient)
	{
		return false;
	}
	const FTransferResult Result = TransferRules::Transfer(*Sender, *Recipient, InItem);
	if (Result.bRecipientFull)
	{
		const EDeployableType Type = InItem == ETransferItem::Turret ? EDeployableType::Turret
			: (InItem == ETransferItem::Barricade ? EDeployableType::Barricade : EDeployableType::Mine);
		const TCHAR* Plural = Type == EDeployableType::Turret ? TEXT("турелей") : (Type == EDeployableType::Barricade ? TEXT("баррикад") : TEXT("мин"));
		Post(Sender->DisplayName, FString::Printf(TEXT("⚠️ Инвентарь %s полон %s (%d/%d)!"), *Recipient->DisplayName.ToString(), Plural,
			Recipient->GetDeployableCount(Type), DeployableRules::GetMaxCarried(Type)));
		return false;
	}
	if (!Result.bDone)
	{
		Post(Sender->DisplayName, TEXT("В вашем инвентаре закончился этот предмет или патроны!"));
		return false;
	}
	Post(Sender->DisplayName, FString::Printf(TEXT("🟣 Передал(а) %s бойцу %s!"), *Result.Feedback, *Recipient->DisplayName.ToString()));
	return true;
}

#undef LOCTEXT_NAMESPACE
