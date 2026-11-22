#include "Interactables/DoorActor.h"

#include "AI/StealthLureSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "Survival/ColdShelterSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace DoorActorLocal
{
	/** A capsule this close to the leaf plane stands in the doorway (40 cm capsule + 5 cm). */
	constexpr float DoorwayHalfDepthCm = 45.f;
	/** The cold shelter's fallback search radius for an opening without an id match. */
	constexpr float ColdOpeningRadiusCm = 150.f;

	void MakeLeaf(UStaticMeshComponent* Leaf)
	{
		Leaf->SetMobility(EComponentMobility::Movable);
		Leaf->SetCanEverAffectNavigation(false); // the nav box carries the door's navmesh rule (ADR-0003)
		Leaf->SetGenerateOverlapEvents(false);
		Leaf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Leaf->SetCollisionObjectType(ECC_WorldStatic); // as the builder leaf it replaces: every WorldStatic query keeps seeing it
	}
}

UNavArea_DoorClosed::UNavArea_DoorClosed()
{
	DefaultCost = 1.f;
	// doors.json nav_entering_cost_cm, read when the class default is built (the navmesh registers its areas once, at start-up).
	FixedAreaEnteringCost = DoorTuning::Get().NavEnteringCostCm;
	DrawColor = FColor(140, 90, 40);
}

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetMobility(EComponentMobility::Movable);
	RootComponent = Frame;

	HingeA = CreateDefaultSubobject<USceneComponent>(TEXT("HingeA"));
	HingeA->SetupAttachment(Frame);
	HingeB = CreateDefaultSubobject<USceneComponent>(TEXT("HingeB"));
	HingeB->SetupAttachment(Frame);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	LeafA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafA"));
	LeafA->SetupAttachment(HingeA);
	LeafB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafB"));
	LeafB->SetupAttachment(HingeB);
	for (UStaticMeshComponent* Leaf : { LeafA.Get(), LeafB.Get() })
	{
		DoorActorLocal::MakeLeaf(Leaf);
		if (CubeMesh.Succeeded())
		{
			Leaf->SetStaticMesh(CubeMesh.Object);
		}
	}

	NavBox = CreateDefaultSubobject<UBoxComponent>(TEXT("NavBox"));
	NavBox->SetupAttachment(Frame);
	NavBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NavBox->SetGenerateOverlapEvents(false);
	NavBox->bDynamicObstacle = true; // a nav modifier only: never cuts geometry
	NavBox->SetCanEverAffectNavigation(false);
	NavBox->SetHiddenInGame(true);
}

void ADoorActor::Setup(const FDoorSetup& InSetup)
{
	Setup_ = InSetup;
	SetActorTransform(Setup_.Frame);
	const FDoorTuning& Tuning = DoorTuning::Get();
	LeafCount = DoorRules::CountLeaves(Setup_.WidthCm, Tuning);
	const float LeafWidth = Setup_.WidthCm / LeafCount;
	const float HalfWidth = Setup_.WidthCm * 0.5f;
	HingeA->SetRelativeLocation(FVector(-HalfWidth, 0.f, 0.f));
	HingeB->SetRelativeLocation(FVector(HalfWidth, 0.f, 0.f));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Leaf = Index == 0 ? LeafA.Get() : LeafB.Get();
		if (Setup_.LeafMesh)
		{
			Leaf->SetStaticMesh(Setup_.LeafMesh);
		}
		for (int32 Slot = 0; Slot < Setup_.LeafMaterials.Num(); ++Slot)
		{
			Leaf->SetMaterial(Slot, Setup_.LeafMaterials[Slot]);
		}
		const FBoxSphereBounds MeshBounds = Leaf->GetStaticMesh() ? Leaf->GetStaticMesh()->GetBounds() : FBoxSphereBounds(FVector::ZeroVector, FVector(50.f), 50.f);
		const FVector MeshSize = (MeshBounds.BoxExtent * 2.f).ComponentMax(FVector(1.f));
		const FVector Scale(LeafWidth / MeshSize.X, Setup_.ThicknessCm / MeshSize.Y, Setup_.HeightCm / MeshSize.Z);
		// Leaf A runs +X from its hinge, leaf B -X from its own; the mesh's bounds origin is moved onto the leaf's centre.
		const float Along = Index == 0 ? LeafWidth * 0.5f : -LeafWidth * 0.5f;
		Leaf->SetRelativeScale3D(Scale);
		Leaf->SetRelativeLocation(FVector(Along, 0.f, Setup_.HeightCm * 0.5f) - MeshBounds.Origin * Scale);
		const bool bShown = !Setup_.bVisualHidden && Index < LeafCount;
		Leaf->SetVisibility(bShown);
		Leaf->SetHiddenInGame(!bShown);
	}
	// From NavBelowCm under the threshold (the navmesh lies a cell above / below the floor) up to the doorway's top.
	constexpr float NavBelowCm = 40.f;
	NavBox->SetBoxExtent(FVector(HalfWidth, Tuning.NavBoxDepthCm * 0.5f, (Setup_.HeightCm + NavBelowCm) * 0.5f));
	NavBox->SetRelativeLocation(FVector(0.f, 0.f, (Setup_.HeightCm - NavBelowCm) * 0.5f));
	State = Setup_.State;
	OpenAlpha = TargetAlpha = DoorRules::BlocksSightAndFire(State) ? 0.f : 1.f;
	ApplyLeafPose();
	ApplyCollision(DoorRules::BlocksSightAndFire(State));
	ApplyNav();
}

void ADoorActor::ApplyCollision(bool bBlocking)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Leaf = Index == 0 ? LeafA.Get() : LeafB.Get();
		const bool bActive = bBlocking && Index < LeafCount;
		Leaf->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bActive)
		{
			Leaf->SetCollisionResponseToAllChannels(ECR_Block); // sight (Visibility), bullets, camera, bodies (Pawn)
		}
	}
	for (UStaticMeshComponent* Kit : KitLeaves)
	{
		if (Kit)
		{
			Kit->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the kit copy is visual; the grey leaves carry the collision
		}
	}
}

void ADoorActor::ApplyNav()
{
	const EDoorPassage Passage = DoorRules::GetPassage(State);
	if (Passage == EDoorPassage::Free)
	{
		if (NavBox->CanEverAffectNavigation())
		{
			NavBox->SetCanEverAffectNavigation(false);
		}
		return;
	}
	NavBox->SetAreaClassOverride(Passage == EDoorPassage::Blocked ? TSubclassOf<UNavAreaBase>(UNavArea_Null::StaticClass())
		: TSubclassOf<UNavAreaBase>(UNavArea_DoorClosed::StaticClass()));
	if (!NavBox->CanEverAffectNavigation())
	{
		NavBox->SetCanEverAffectNavigation(true);
	}
	else
	{
		FNavigationSystem::UpdateComponentData(*NavBox);
	}
}

void ADoorActor::ApplyLeafPose()
{
	const float Yaw = SwingSign_ * OpenAlpha * DoorTuning::Get().SwingDegrees;
	HingeA->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	HingeB->SetRelativeRotation(FRotator(0.f, -Yaw, 0.f));
}

bool ADoorActor::OpenDoor(AActor* InstigatorActor)
{
	EDoorLeafState Next;
	if (!DoorRules::ApplyAction(State, EDoorAction::Open, DoorTuning::Get(), Next))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bLockForced = false;
	StartSwing(true, DoorRules::SwingSign(InstigatorActor ? ToLocal(InstigatorActor->GetActorLocation()).Y : 0.f));
	NotifyChanged(Old, InstigatorActor, false);
	return true;
}

bool ADoorActor::KickDoor(AActor* InstigatorActor, bool bLands)
{
	EDoorLeafState Next;
	if (!DoorRules::ApplyAction(State, EDoorAction::Kick, DoorTuning::Get(), Next))
	{
		return false;
	}
	if (UStealthLureSubsystem* Lure = GetWorld()->GetSubsystem<UStealthLureSubsystem>(); Lure && DoorTuning::Get().KickNoiseCm > 0.f)
	{
		Lure->EmitNoise(GetDoorwayCenter(), EStealthLureKind::Throw, InstigatorActor, DoorTuning::Get().KickNoiseCm, FName(TEXT("DoorKick")));
	}
	if (!bLands)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Door %s (%s %s): a kick - it holds"), *GetName(), *Setup_.BuildingId, *Setup_.OpeningId);
		GameplayEvents::Raise(this, FName(TEXT("DoorKickHeld")), this);
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bKicked = true;
	StartSwing(true, DoorRules::SwingSign(InstigatorActor ? ToLocal(InstigatorActor->GetActorLocation()).Y : 0.f));
	NotifyChanged(Old, InstigatorActor, true);
	return true;
}

bool ADoorActor::UnlockDoor(AActor* InstigatorActor)
{
	EDoorLeafState Next;
	if (!DoorRules::ApplyAction(State, EDoorAction::Unlock, DoorTuning::Get(), Next))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bLockForced = true; // R2: the next open is the forced push
	ApplyNav(); // blocked -> priced
	NotifyChanged(Old, InstigatorActor, false, FName(TEXT("DoorUnlocked")));
	return true;
}

bool ADoorActor::BreakLock(AActor* InstigatorActor)
{
	EDoorLeafState Next;
	if (State != EDoorLeafState::Locked || !DoorRules::ApplyAction(State, EDoorAction::Unlock, DoorTuning::Get(), Next))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bLockShot = true;
	bLockForced = true;
	ApplyNav(); // blocked -> priced (the shut leaf still blocks sight and bodies)
	NotifyChanged(Old, InstigatorActor, false, FName(TEXT("DoorLockShot")));
	return true;
}

float ADoorActor::GetHandleLocalX() const
{
	return LeafCount > 1 ? 0.f : Setup_.WidthCm * 0.5f - 12.f;
}

bool ADoorActor::ShootLockOpen(AActor* InstigatorActor)
{
	EDoorLeafState Next;
	if (!DoorRules::ApplyAction(State, EDoorAction::ShootLock, DoorTuning::Get(), Next))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bLockShot = true;
	StartSwing(true, DoorRules::SwingSign(InstigatorActor ? ToLocal(InstigatorActor->GetActorLocation()).Y : 0.f));
	NotifyChanged(Old, InstigatorActor, false, FName(TEXT("DoorLockShot")));
	return true;
}

bool ADoorActor::BlastDoor(AActor* InstigatorActor)
{
	EDoorLeafState Next;
	if (bBroken || !DoorRules::ApplyAction(State, EDoorAction::Blast, DoorTuning::Get(), Next))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = Next;
	bBroken = true;
	bSwinging = false;
	SetActorTickEnabled(false);
	OpenAlpha = TargetAlpha = 1.f;
	// The leaf is gone (P1 / ADR-0004 swaps in a broken leaf mesh and debris; R1 hides it).
	for (UStaticMeshComponent* Leaf : { LeafA.Get(), LeafB.Get() })
	{
		Leaf->SetVisibility(false);
		Leaf->SetHiddenInGame(true);
	}
	for (UStaticMeshComponent* Kit : KitLeaves)
	{
		if (Kit)
		{
			Kit->SetVisibility(false);
			Kit->SetHiddenInGame(true);
		}
	}
	ApplyCollision(false);
	ApplyNav();
	NotifyChanged(Old, InstigatorActor, false, FName(TEXT("DoorBlown")));
	return true;
}

bool ADoorActor::IsHeavy() const
{
	return Setup_.bHeavyTag || DoorRules::IsHeavy(Setup_.WidthCm, DoorTuning::Get());
}

FVector ADoorActor::GetLockPoint() const
{
	// A pair: where the leaves meet (the middle); one leaf: its free edge (hinge A is at -X).
	const float X = LeafCount > 1 ? 0.f : Setup_.WidthCm * 0.5f - 12.f;
	return GetActorLocation() + GetTangent() * X + FVector(0.f, 0.f, FMath::Min(100.f, Setup_.HeightCm * 0.5f));
}

bool ADoorActor::CloseDoor(AActor* InstigatorActor)
{
	if (!DoorRules::CanClose(State, IsDoorwayOccupied(), bBroken) || bSwinging)
	{
		return false;
	}
	// The door counts as Open until it is shut (the leaves block only then): Tick sets Closed at the end of the swing.
	StartSwing(false, SwingSign_);
	return true;
}

bool ADoorActor::ForceState(EDoorLeafState NewState, AActor* InstigatorActor)
{
	if (NewState == State || (!DoorRules::IsAdoptable(NewState) && NewState != EDoorLeafState::Open))
	{
		return false;
	}
	const EDoorLeafState Old = State;
	State = NewState;
	bSwinging = false;
	SetActorTickEnabled(false);
	OpenAlpha = TargetAlpha = DoorRules::BlocksSightAndFire(State) ? 0.f : 1.f;
	ApplyLeafPose();
	ApplyCollision(DoorRules::BlocksSightAndFire(State));
	ApplyNav();
	NotifyChanged(Old, InstigatorActor, false);
	return true;
}

void ADoorActor::StartSwing(bool bOpen, float Sign)
{
	if (bOpen)
	{
		SwingSign_ = Sign;
		ApplyCollision(false); // passes at once: nobody is swept by the leaf
		ApplyNav();
	}
	TargetAlpha = bOpen ? 1.f : 0.f;
	bSwinging = !FMath::IsNearlyEqual(OpenAlpha, TargetAlpha);
	SetActorTickEnabled(bSwinging);
	if (!bSwinging)
	{
		ApplyLeafPose();
	}
}

void ADoorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSwinging)
	{
		SetActorTickEnabled(false);
		return;
	}
	const FDoorTuning& Tuning = DoorTuning::Get();
	const bool bOpening = TargetAlpha > OpenAlpha;
	const float Seconds = FMath::Max(0.01f, bOpening ? Tuning.OpenSeconds : Tuning.CloseSeconds);
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, TargetAlpha, DeltaSeconds, 1.f / Seconds);
	ApplyLeafPose();
	if (!FMath::IsNearlyEqual(OpenAlpha, TargetAlpha))
	{
		return;
	}
	OpenAlpha = TargetAlpha;
	bSwinging = false;
	SetActorTickEnabled(false);
	if (!bOpening)
	{
		if (IsDoorwayOccupied())
		{
			StartSwing(true, SwingSign_); // somebody stepped in while it swung: it goes back open, still Open
			return;
		}
		const EDoorLeafState Old = State;
		State = EDoorLeafState::Closed;
		ApplyCollision(true);
		ApplyNav();
		NotifyChanged(Old, nullptr, false);
	}
}

void ADoorActor::NotifyChanged(EDoorLeafState OldState, AActor* InstigatorActor, bool bKick, FName EventOverride)
{
	const bool bOpen = State == EDoorLeafState::Open;
	if (UColdShelterSubsystem* Cold = GetWorld()->GetSubsystem<UColdShelterSubsystem>())
	{
		if (Setup_.BuildingId.IsEmpty() || Setup_.OpeningId.IsEmpty() || !Cold->SetOpeningOpen(Setup_.BuildingId, Setup_.OpeningId, bOpen))
		{
			const FVector Center = GetDoorwayCenter();
			Cold->SetOpeningOpenAt(FVector2D(Center.X, Center.Y), Setup_.Floor, DoorActorLocal::ColdOpeningRadiusCm, bOpen);
		}
	}
	const FName Event = !EventOverride.IsNone() ? EventOverride : bKick ? FName(TEXT("DoorKicked")) : bOpen ? FName(TEXT("DoorOpened")) : FName(TEXT("DoorClosed"));
	UE_LOG(LogCodexTactics, Display, TEXT("Door %s (%s %s): %s -> %s [%s] by %s"), *GetName(), *Setup_.BuildingId, *Setup_.OpeningId,
		DoorRules::StateName(OldState), DoorRules::StateName(State), *Event.ToString(), InstigatorActor ? *InstigatorActor->GetName() : TEXT("-"));
	GameplayEvents::Raise(this, Event, this);
	ReceiveStateChanged(OldState, State, bKick);
}

FVector ADoorActor::GetDoorwayCenter() const
{
	return GetActorLocation() + FVector(0.f, 0.f, Setup_.HeightCm * 0.5f);
}

FVector ADoorActor::GetNormal() const
{
	return GetActorRightVector().GetSafeNormal2D();
}

FVector ADoorActor::GetTangent() const
{
	return GetActorForwardVector().GetSafeNormal2D();
}

FVector2D ADoorActor::ToLocal(const FVector& World) const
{
	const FVector Delta = World - GetActorLocation();
	return FVector2D(FVector::DotProduct(Delta, GetTangent()), FVector::DotProduct(Delta, GetNormal()));
}

bool ADoorActor::IsAtDoorLevel(const FVector& FeetWorld) const
{
	const float Dz = FeetWorld.Z - GetActorLocation().Z;
	return Dz >= -60.f && Dz <= Setup_.HeightCm;
}

FVector ADoorActor::GetApproachPoint(const FVector& From) const
{
	const float Side = ToLocal(From).Y >= 0.f ? 1.f : -1.f;
	return GetActorLocation() + GetNormal() * (Side * DoorTuning::Get().ApproachCm);
}

bool ADoorActor::IsDoorwayOccupied() const
{
	const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(Setup_.WidthCm * 0.5f, DoorActorLocal::DoorwayHalfDepthCm, Setup_.HeightCm * 0.5f - 5.f));
	return GetWorld()->OverlapAnyTestByObjectType(GetDoorwayCenter(), GetActorQuat(), FCollisionObjectQueryParams(ECC_Pawn), Shape);
}

FVector ADoorActor::GetLeafFootprintMid(int32 LeafIndex) const
{
	const float LeafWidth = Setup_.WidthCm / LeafCount;
	const USceneComponent* Hinge = LeafIndex == 0 ? HingeA.Get() : HingeB.Get();
	return Hinge->GetComponentLocation() + Hinge->GetForwardVector() * (LeafIndex == 0 ? LeafWidth * 0.5f : -LeafWidth * 0.5f);
}

bool ADoorActor::IsNearOpenLeaf(const FVector& Point) const
{
	if (State != EDoorLeafState::Open || !IsAtDoorLevel(Point))
	{
		return false;
	}
	const float Snap = DoorTuning::Get().ClickSnapCm;
	const float LeafWidth = Setup_.WidthCm / LeafCount;
	for (int32 Index = 0; Index < LeafCount; ++Index)
	{
		const USceneComponent* Hinge = Index == 0 ? HingeA.Get() : HingeB.Get();
		const FVector Start = Hinge->GetComponentLocation();
		const FVector End = Start + Hinge->GetForwardVector() * (Index == 0 ? LeafWidth : -LeafWidth);
		const FVector Closest = FMath::ClosestPointOnSegment(FVector(Point.X, Point.Y, Start.Z), Start, FVector(End.X, End.Y, Start.Z));
		if (FVector::Dist2D(Closest, Point) <= Snap)
		{
			return true;
		}
	}
	return false;
}

bool ADoorActor::OwnsComponent(const UPrimitiveComponent* Component) const
{
	return Component && Component->GetOwner() == this;
}

void ADoorActor::AddLiftedInstance(UInstancedStaticMeshComponent* Component, int32 Index, const FTransform& WorldTransform)
{
	if (!Component)
	{
		return;
	}
	// The kit leaf rides on the hinge it is nearer to (a pair: each half on its own).
	const float X = ToLocal(WorldTransform.GetLocation()).X;
	USceneComponent* Hinge = LeafCount > 1 && X > 0.f ? HingeB.Get() : HingeA.Get();
	UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(this);
	Copy->SetMobility(EComponentMobility::Movable);
	Copy->SetStaticMesh(Component->GetStaticMesh());
	for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
	{
		Copy->SetMaterial(Slot, Component->GetMaterial(Slot));
	}
	Copy->SetupAttachment(Hinge);
	Copy->RegisterComponent();
	Copy->SetWorldTransform(WorldTransform);
	Copy->SetCanEverAffectNavigation(false);
	Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	KitLeaves.Add(Copy);
	FTransform Hidden = WorldTransform;
	Hidden.SetScale3D(FVector(KINDA_SMALL_NUMBER));
	Component->UpdateInstanceTransform(Index, Hidden, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	Lifted.Add({ Component, Index, WorldTransform });
}

void ADoorActor::RestoreLiftedInstances()
{
	for (const FLiftedInstance& Instance : Lifted)
	{
		if (UInstancedStaticMeshComponent* Component = Instance.Component.Get())
		{
			Component->UpdateInstanceTransform(Instance.Index, Instance.Original, true, true, true);
		}
	}
	Lifted.Reset();
}

void ADoorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreLiftedInstances();
	Super::EndPlay(EndPlayReason);
}
