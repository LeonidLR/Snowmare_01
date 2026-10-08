#include "Characters/SquadTransferSubsystem.h"
#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
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

	/** Sprint 13 approach: check period, give-up time, tolerances. */
	constexpr float PendingTickSeconds = 0.2f;
	constexpr double PendingTimeoutSeconds = 45.0;
	constexpr double PendingOrderGraceSeconds = 1.0;
	constexpr float PendingPathTolerance = 150.f;
	constexpr float PendingOtherOrderTolerance = 60.f;
	constexpr float PendingRecipientMoved = 100.f;

	bool TransferIsAlive(const AOperativeCharacter* Operative)
	{
		return Operative && (!Operative->HealthComponent || Operative->HealthComponent->IsAlive());
	}
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
	Post(LOCTEXT("Transfer", "Hand-over"), FString::Printf(TEXT("🟣 Put the purple circle on a teammate and LMB to hand over %s (RMB / Esc - cancel)."),
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
		Post(LOCTEXT("Transfer", "Hand-over"), TEXT("Click directly on the teammate you want to hand the item to!"));
		return false;
	}
	if (Target == Leader)
	{
		Post(LOCTEXT("Transfer", "Hand-over"), TEXT("You can't hand an item to yourself! Pick a teammate."));
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
		const TCHAR* Plural = Type == EDeployableType::Turret ? TEXT("turrets") : (Type == EDeployableType::Barricade ? TEXT("barricades") : TEXT("mines"));
		Post(Sender->DisplayName, FString::Printf(TEXT("⚠️ %s can't carry more %s (%d/%d)!"), *Recipient->DisplayName.ToString(), Plural,
			Recipient->GetDeployableCount(Type), DeployableRules::GetMaxCarried(Type)));
		return false;
	}
	if (!Result.bDone)
	{
		Post(Sender->DisplayName, TEXT("You're out of this item or ammo!"));
		return false;
	}
	Post(Sender->DisplayName, FString::Printf(TEXT("🟣 Handed %s to %s!"), *Result.Feedback, *Recipient->DisplayName.ToString()));
	return true;
}

FTransferRequest FTransferRequest::MakeGive(AOperativeCharacter* Sender, AOperativeCharacter* InRecipient, ETransferItem InItem, int32 InQuantity)
{
	FTransferRequest Request;
	Request.Action = ETransferAction::Give;
	Request.Operative = Sender;
	Request.Recipient = InRecipient;
	Request.Item = InItem;
	Request.Quantity = InQuantity;
	return Request;
}

FTransferRequest FTransferRequest::MakeDrop(AOperativeCharacter* Sender, const FVector& InPoint, ETransferItem InItem, int32 InQuantity)
{
	FTransferRequest Request;
	Request.Action = ETransferAction::DropToGround;
	Request.Operative = Sender;
	Request.Point = InPoint;
	Request.Item = InItem;
	Request.Quantity = InQuantity;
	return Request;
}

FTransferRequest FTransferRequest::MakeStore(AOperativeCharacter* Sender, AActor* InContainer, ETransferItem InItem, int32 InQuantity)
{
	FTransferRequest Request;
	Request.Action = ETransferAction::Store;
	Request.Operative = Sender;
	Request.Container = InContainer;
	Request.Item = InItem;
	Request.Quantity = InQuantity;
	return Request;
}

FTransferRequest FTransferRequest::MakeTake(AOperativeCharacter* Taker, AActor* InContainer, ETransferItem InItem, int32 InQuantity)
{
	FTransferRequest Request;
	Request.Action = ETransferAction::Take;
	Request.Operative = Taker;
	Request.Container = InContainer;
	Request.Item = InItem;
	Request.Quantity = InQuantity;
	return Request;
}

AOperativeCharacter* USquadTransferSubsystem::ResolveDropRecipient(const AOperativeCharacter* Sender, AActor* HitActor, const FVector& Point,
	float PickRadius) const
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}
	const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	AOperativeCharacter* Hit = Cast<AOperativeCharacter>(HitActor);
	if (!Hit && HitActor)
	{
		Hit = Cast<AOperativeCharacter>(HitActor->GetOwner());
	}
	if (Hit && Hit != Sender && Members.Contains(Hit) && TransferIsAlive(Hit))
	{
		return Hit;
	}
	AOperativeCharacter* Closest = nullptr;
	float ClosestDistance = PickRadius;
	for (AOperativeCharacter* Member : Members)
	{
		const float Distance = FVector::Dist2D(Member->GetActorLocation(), Point);
		if (Member != Sender && TransferIsAlive(Member) && Distance <= ClosestDistance)
		{
			Closest = Member;
			ClosestDistance = Distance;
		}
	}
	return Closest;
}

FTransferRangeContext USquadTransferSubsystem::BuildRangeContext(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient) const
{
	return BuildRequestContext(FTransferRequest::MakeGive(const_cast<AOperativeCharacter*>(&Sender), const_cast<AOperativeCharacter*>(&Recipient),
		ETransferItem::Medkit));
}

FTransferRangeContext USquadTransferSubsystem::BuildRequestContext(const FTransferRequest& InRequest) const
{
	FTransferRangeContext Context;
	const AOperativeCharacter* Walker = InRequest.Operative.Get();
	if (!Walker)
	{
		Context.bCanMove = false;
		return Context;
	}
	Context.Distance = GetRequestDistance(InRequest);
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	Context.bInCombat = Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat;
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	Context.bTurnBased = TurnBased && TurnBased->IsActive();
	Context.bUnderFire = bForceUnderFireForTesting;
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It && !Context.bUnderFire; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		Context.bUnderFire = It->GetCurrentTarget() == Walker && (!Health || Health->IsAlive());
	}
	Context.bCanMove = TransferIsAlive(Walker) && !Walker->IsRaging();
	return Context;
}

UItemStashComponent* USquadTransferSubsystem::GetContainerStash(AActor* Container, bool bSync)
{
	if (ALootCrateActor* Crate = Cast<ALootCrateActor>(Container))
	{
		return bSync ? Crate->GetSyncedStash() : Crate->GetStash();
	}
	if (const ADroppedItemActor* Pile = Cast<ADroppedItemActor>(Container))
	{
		return Pile->GetStash();
	}
	return nullptr;
}

FVector USquadTransferSubsystem::GetTargetLocation(const FTransferRequest& InRequest)
{
	switch (InRequest.Action)
	{
	case ETransferAction::Give: return InRequest.Recipient.IsValid() ? InRequest.Recipient->GetActorLocation() : FVector::ZeroVector;
	case ETransferAction::DropToGround: return InRequest.Point;
	default: return InRequest.Container.IsValid() ? InRequest.Container->GetActorLocation() : FVector::ZeroVector;
	}
}

float USquadTransferSubsystem::GetRequestDistance(const FTransferRequest& InRequest)
{
	const AOperativeCharacter* Walker = InRequest.Operative.Get();
	if (!Walker)
	{
		return MAX_flt;
	}
	if (InRequest.Action == ETransferAction::Store || InRequest.Action == ETransferAction::Take)
	{
		if (const AInteractableActor* Object = Cast<AInteractableActor>(InRequest.Container.Get()))
		{
			return Object->GetDistanceTo(Walker->GetActorLocation()); // to the crate's box, not its centre
		}
		if (!InRequest.Container.IsValid())
		{
			return MAX_flt;
		}
	}
	if (InRequest.Action == ETransferAction::Give && !InRequest.Recipient.IsValid())
	{
		return MAX_flt;
	}
	return FVector::Dist2D(Walker->GetActorLocation(), GetTargetLocation(InRequest));
}

int32 USquadTransferSubsystem::GetMaxQuantity(const FTransferRequest& InRequest) const
{
	const AOperativeCharacter* Operative = InRequest.Operative.Get();
	if (!Operative)
	{
		return 0;
	}
	const int32 Stock = TransferRules::GetAvailable(*Operative, InRequest.Item);
	switch (InRequest.Action)
	{
	case ETransferAction::Give:
		return InRequest.Recipient.IsValid() ? TransferRules::GetMaxTransferQuantity(*Operative, *InRequest.Recipient, InRequest.Item) : 0;
	case ETransferAction::DropToGround:
		return Stock;
	case ETransferAction::Store:
	{
		const UItemStashComponent* Stash = GetContainerStash(InRequest.Container.Get(), true);
		return Stash ? FMath::Max(0, FMath::Min(Stock, Stash->GetFreeSpace())) : 0;
	}
	default:
	{
		const UItemStashComponent* Stash = GetContainerStash(InRequest.Container.Get(), true);
		return Stash ? FMath::Max(0, FMath::Min(Stash->GetCount(InRequest.Item), TransferRules::GetRecipientCapacity(*Operative, InRequest.Item))) : 0;
	}
	}
}

ETransferRequestOutcome USquadTransferSubsystem::RequestTransfer(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem, int32 Quantity)
{
	if (!Sender || !Recipient || Sender == Recipient)
	{
		return ETransferRequestOutcome::Failed;
	}
	return Request(FTransferRequest::MakeGive(Sender, Recipient, InItem, Quantity));
}

ETransferRequestOutcome USquadTransferSubsystem::Request(const FTransferRequest& InRequest)
{
	AOperativeCharacter* Walker = InRequest.Operative.Get();
	if (!Walker || InRequest.Quantity <= 0)
	{
		return ETransferRequestOutcome::Failed;
	}
	if (HasPendingTransfer())
	{
		CancelPendingTransfer(false);
	}
	switch (TransferRules::DecideRange(BuildRequestContext(InRequest)))
	{
	case ETransferRangeDecision::InRange:
		return Execute(InRequest) > 0 ? ETransferRequestOutcome::Transferred : ETransferRequestOutcome::Failed;
	case ETransferRangeDecision::Approach:
		Pending.Request = InRequest;
		Pending.StartTime = GetWorld()->GetTimeSeconds();
		if (IssueApproach(*Walker, InRequest))
		{
			GetWorld()->GetTimerManager().SetTimer(PendingTimer, FTimerDelegate::CreateUObject(this, &USquadTransferSubsystem::TickPending),
				PendingTickSeconds, true);
			const FString What = TransferRules::GetItemName(InRequest.Item);
			switch (InRequest.Action)
			{
			case ETransferAction::Give:
				Post(Walker->DisplayName, FString::Printf(TEXT("🟣 Moving to %s to hand over: %s."), *InRequest.Recipient->DisplayName.ToString(), *What));
				break;
			case ETransferAction::DropToGround:
				Post(Walker->DisplayName, FString::Printf(TEXT("Moving to drop on the ground: %s."), *What));
				break;
			case ETransferAction::Store:
				Post(Walker->DisplayName, FString::Printf(TEXT("Moving to the crate to stow: %s."), *What));
				break;
			default:
				Post(Walker->DisplayName, FString::Printf(TEXT("Moving to pick up: %s."), *What));
				break;
			}
			return ETransferRequestOutcome::Approaching;
		}
		Pending = FPendingTransfer();
		break; // cannot walk over: blocked
	default:
		break;
	}
	if (InRequest.Action == ETransferAction::DropToGround)
	{
		// Decision: a drop out of reach while he cannot walk over goes down at his feet (getting rid of a load must
		// always work, also under fire); stores / takes / hand-overs need the target within reach.
		FTransferRequest AtFeet = InRequest;
		AtFeet.Point = Walker->GetActorLocation();
		return Execute(AtFeet) > 0 ? ETransferRequestOutcome::DroppedAtFeet : ETransferRequestOutcome::Failed;
	}
	Post(Walker->DisplayName, TEXT("Too far to hand over (max 2 m)"));
	return ETransferRequestOutcome::Blocked;
}

int32 USquadTransferSubsystem::ExecuteTransferQuantity(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem, int32 Quantity)
{
	if (!Sender || !Recipient || Sender == Recipient)
	{
		return 0;
	}
	return Execute(FTransferRequest::MakeGive(Sender, Recipient, InItem, Quantity));
}

int32 USquadTransferSubsystem::Execute(const FTransferRequest& InRequest)
{
	AOperativeCharacter* Operative = InRequest.Operative.Get();
	if (!Operative || InRequest.Quantity <= 0)
	{
		return 0;
	}
	const FString Name = TransferRules::GetItemName(InRequest.Item);
	switch (InRequest.Action)
	{
	case ETransferAction::Give:
	{
		AOperativeCharacter* Recipient = InRequest.Recipient.Get();
		if (!Recipient || Recipient == Operative)
		{
			return 0;
		}
		const FTransferResult Result = TransferRules::TransferQuantity(*Operative, *Recipient, InRequest.Item, InRequest.Quantity);
		if (Result.bRecipientFull)
		{
			Post(Operative->DisplayName, FString::Printf(TEXT("⚠️ %s has no room: %s."), *Recipient->DisplayName.ToString(), *Name));
			return 0;
		}
		if (!Result.bDone)
		{
			Post(Operative->DisplayName, TEXT("You're out of this item or ammo!"));
			return 0;
		}
		Post(Operative->DisplayName, FString::Printf(TEXT("🟣 Handed %s to %s!%s"), *Result.Feedback, *Recipient->DisplayName.ToString(),
			Result.bClampedByCapacity ? TEXT(" (no room for more)") : TEXT("")));
		return Result.Moved;
	}
	case ETransferAction::DropToGround:
	{
		const int32 Removed = TransferRules::RemoveFromOperative(*Operative, InRequest.Item, InRequest.Quantity);
		if (Removed <= 0)
		{
			Post(Operative->DisplayName, TEXT("You're out of this item or ammo!"));
			return 0;
		}
		if (!ADroppedItemActor::SpawnOrMerge(GetWorld(), InRequest.Point, InRequest.Item, Removed))
		{
			TransferRules::AddToOperative(*Operative, InRequest.Item, Removed); // nothing could be spawned: keep it
			return 0;
		}
		Post(Operative->DisplayName, FString::Printf(TEXT("Dropped on the ground: %s x%d."), *Name, Removed));
		return Removed;
	}
	case ETransferAction::Store:
	{
		ALootCrateActor* Crate = Cast<ALootCrateActor>(InRequest.Container.Get());
		UItemStashComponent* Stash = GetContainerStash(InRequest.Container.Get(), true);
		if (!Stash || (Crate && !Crate->CanStore()))
		{
			Post(Operative->DisplayName, TEXT("Can't stow anything in this crate now (trapped or destroyed)."));
			return 0;
		}
		const FTransferResult Result = TransferRules::StoreInStash(*Operative, *Stash, InRequest.Item, InRequest.Quantity);
		if (Result.bRecipientFull)
		{
			Post(Operative->DisplayName, TEXT("⚠️ The crate is full!"));
			return 0;
		}
		if (!Result.bDone)
		{
			Post(Operative->DisplayName, TEXT("You're out of this item or ammo!"));
			return 0;
		}
		Post(Operative->DisplayName, FString::Printf(TEXT("📦 Stowed in the crate: %s x%d.%s"), *Name, Result.Moved,
			Result.bClampedByCapacity ? TEXT(" (crate full, I keep the rest)") : TEXT("")));
		return Result.Moved;
	}
	default:
	{
		AActor* Container = InRequest.Container.Get();
		UItemStashComponent* Stash = GetContainerStash(Container, true);
		if (!Stash)
		{
			return 0;
		}
		const bool bPile = Cast<ADroppedItemActor>(Container) != nullptr;
		const int32 Wanted = FMath::Min(InRequest.Quantity, Stash->GetCount(InRequest.Item));
		const FTransferResult Result = TransferRules::TakeFromStash(*Stash, *Operative, InRequest.Item, InRequest.Quantity);
		if (Result.bRecipientFull)
		{
			Post(Operative->DisplayName, FString::Printf(TEXT("⚠️ Can't carry any more: %s."), *Name));
			return 0;
		}
		if (!Result.bDone)
		{
			return 0;
		}
		const FString Left = Result.bClampedByCapacity ? FString::Printf(TEXT(" (no room for %d, left %s)"), Wanted - Result.Moved,
			bPile ? TEXT("on the ground") : TEXT("in the crate")) : FString();
		Post(Operative->DisplayName, bPile ? FString::Printf(TEXT("Picked up: %s x%d.%s"), *Name, Result.Moved, *Left)
			: FString::Printf(TEXT("📦 Took from the crate: %s x%d.%s"), *Name, Result.Moved, *Left));
		return Result.Moved;
	}
	}
}

void USquadTransferSubsystem::PickUpAll(AOperativeCharacter* Leader, ADroppedItemActor* Pile)
{
	if (!Leader || !Pile)
	{
		return;
	}
	TWeakObjectPtr<ADroppedItemActor> WeakPile(Pile);
	const UItemStashComponent* Stash = Pile->GetStash();
	for (const ETransferItem Type : Stash->GetItemTypes())
	{
		if (!WeakPile.IsValid() || WeakPile->IsActorBeingDestroyed())
		{
			break; // emptied (and destroyed) by the previous take
		}
		Execute(FTransferRequest::MakeTake(Leader, Pile, Type, Stash->GetCount(Type)));
	}
}

void USquadTransferSubsystem::CancelPendingTransfer(bool bNotify)
{
	AOperativeCharacter* Walker = Pending.Request.Operative.Get();
	Pending = FPendingTransfer();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingTimer);
	}
	if (bNotify && Walker)
	{
		Post(Walker->DisplayName, TEXT("Hand-over cancelled."));
	}
}

bool USquadTransferSubsystem::IssueApproach(AOperativeCharacter& Walker, const FTransferRequest& InRequest)
{
	const FVector Anchor = GetTargetLocation(InRequest);
	FVector Away = (Walker.GetActorLocation() - Anchor).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	FVector Destination = Anchor + Away * TransferRules::ApproachStopDistance;
	if (InRequest.Action == ETransferAction::DropToGround)
	{
		Destination = Anchor + Away * TransferRules::GroundDropStopDistance;
	}
	else if (const AInteractableActor* Object = Cast<AInteractableActor>(InRequest.Container.Get()))
	{
		Destination = Object->GetApproachPoint(Walker.GetActorLocation());
	}
	if (Walker.OrderMoveTo(Destination, false) != EOperativeOrderResult::Accepted)
	{
		return false;
	}
	Pending.Destination = Destination;
	Pending.RecipientAnchor = Anchor;
	Pending.PathDestination.Reset();
	Pending.OrderTime = GetWorld()->GetTimeSeconds();
	// Baseline of our own path at once: an order given in the same frame (before the first check) must read as "another
	// order", not as the start of ours. A deferred move (stand-up clip first) is captured by TickPending.
	const AAIController* AI = Cast<AAIController>(Walker.GetController());
	const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (Path && Path->GetStatus() == EPathFollowingStatus::Moving && FVector::Dist2D(Path->GetPathDestination(), Destination) <= PendingPathTolerance)
	{
		Pending.PathDestination = Path->GetPathDestination();
	}
	return true;
}

void USquadTransferSubsystem::TickPending()
{
	const FTransferRequest Request = Pending.Request;
	AOperativeCharacter* Walker = Request.Operative.Get();
	const bool bTargetGone = (Request.Action == ETransferAction::Give && !TransferIsAlive(Request.Recipient.Get()))
		|| ((Request.Action == ETransferAction::Store || Request.Action == ETransferAction::Take)
			&& (!Request.Container.IsValid() || Request.Container->IsActorBeingDestroyed()));
	if (!TransferIsAlive(Walker) || bTargetGone)
	{
		CancelPendingTransfer(Walker != nullptr);
		return;
	}
	if (GetRequestDistance(Request) <= TransferRules::MaxTransferDistance)
	{
		CancelPendingTransfer(false);
		Walker->StopOperative();
		Execute(Request);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - Pending.StartTime > PendingTimeoutSeconds)
	{
		CancelPendingTransfer(true);
		return;
	}
	const bool bMovingTarget = Request.Action == ETransferAction::Give;
	const AAIController* AI = Cast<AAIController>(Walker->GetController());
	const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
	const bool bFollowingPath = Path && Path->GetStatus() == EPathFollowingStatus::Moving;
	const bool bGrace = Now - Pending.OrderTime < PendingOrderGraceSeconds;
	if (bFollowingPath)
	{
		const FVector PathEnd = Path->GetPathDestination();
		if (!Pending.PathDestination.IsSet())
		{
			if (FVector::Dist2D(PathEnd, Pending.Destination) <= PendingPathTolerance)
			{
				Pending.PathDestination = PathEnd;
			}
			else if (!bGrace)
			{
				CancelPendingTransfer(true); // walking somewhere else: another order replaced ours
				return;
			}
		}
		else if (FVector::Dist2D(PathEnd, Pending.PathDestination.GetValue()) > PendingOtherOrderTolerance)
		{
			CancelPendingTransfer(true); // another order
			return;
		}
		// The recipient walked off: aim at him again (only while our own path is confirmed; never over a foreign order).
		if (bMovingTarget && Pending.PathDestination.IsSet()
			&& FVector::Dist2D(GetTargetLocation(Request), Pending.RecipientAnchor) > PendingRecipientMoved && !IssueApproach(*Walker, Request))
		{
			CancelPendingTransfer(true);
		}
		return;
	}
	if (!Walker->IsMoving() && !bGrace)
	{
		// Stopped short: follow a recipient who moved, otherwise the order was replaced / stopped.
		if (!bMovingTarget || FVector::Dist2D(GetTargetLocation(Request), Pending.RecipientAnchor) <= PendingRecipientMoved
			|| !IssueApproach(*Walker, Request))
		{
			CancelPendingTransfer(true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
