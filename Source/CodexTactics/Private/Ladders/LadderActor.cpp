// ALadderActor: the procedural ladder (docs/design/LADDERS.md section 3). UE-only system (no Godot reference).

#include "Ladders/LadderActor.h"

#include "AIController.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderNavigation.h"
#include "Ladders/LadderSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavLinkCustomComponent.h"
#include "Navigation/PathFollowingComponent.h"

UNavArea_Ladder::UNavArea_Ladder()
{
	// A ladder of 2.5-4 m costs as much as 8-12 m of open ground: taken when the way round is longer.
	DefaultCost = 3.f;
	DrawColor = FColor(80, 200, 255);
}

namespace LadderActorInternal
{
	const TCHAR* const CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	UInstancedStaticMeshComponent* MakeVisual(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Component = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		// Visual only: no collision, no navmesh cut (like VaultNavigation::MakeVaultable's components).
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->bReceivesDecals = false;
		return Component;
	}

	UStaticMesh* LoadMesh(const TSoftObjectPtr<UStaticMesh>& Soft, const TCHAR* Fallback)
	{
		UStaticMesh* Mesh = Soft.IsNull() ? nullptr : Soft.LoadSynchronous();
		return Mesh ? Mesh : LoadObject<UStaticMesh>(nullptr, Fallback);
	}

	/** Half extents and the bottom (pivot offset) of a mesh from its bounds, never assumed (LADDERS.md 3.3). */
	void MeshBox(const UStaticMesh* Mesh, FVector& OutExtent, FVector& OutOrigin)
	{
		const FBoxSphereBounds Bounds = Mesh ? Mesh->GetBounds() : FBoxSphereBounds(FVector::ZeroVector, FVector(50.f), 50.f);
		OutExtent = Bounds.BoxExtent.ComponentMax(FVector(0.5f));
		OutOrigin = Bounds.Origin;
	}
}

ALadderActor::ALadderActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Rails = LadderActorInternal::MakeVisual(this, Root, TEXT("Rails"));
	Rungs = LadderActorInternal::MakeVisual(this, Root, TEXT("Rungs"));
	Brackets = LadderActorInternal::MakeVisual(this, Root, TEXT("Brackets"));
	GrabBars = LadderActorInternal::MakeVisual(this, Root, TEXT("GrabBars"));

	// The cursor traces Visibility (HandleWorldHit): the rungs alone would be unclickable.
	ClickProxy = CreateDefaultSubobject<UBoxComponent>(TEXT("ClickProxy"));
	ClickProxy->SetupAttachment(Root);
	ClickProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickProxy->SetGenerateOverlapEvents(false);
	ClickProxy->SetCanEverAffectNavigation(false);
	ClickProxy->SetHiddenInGame(true);

	NavLink = CreateDefaultSubobject<UNavLinkCustomComponent>(TEXT("NavLink"));
	NavLink->SetEnabledArea(UNavArea_Ladder::StaticClass());
	NavLink->SetDisabledArea(UNavArea_Null::StaticClass());
	// The infantry navmesh only (agent 0 "Default"; the APC's navmesh never gets a ladder).
	FNavAgentSelector Agents;
	Agents.Empty();
	Agents.Set(0);
	NavLink->SetSupportedAgents(Agents);

#if WITH_EDITORONLY_DATA
	ValidationLabel = CreateEditorOnlyDefaultSubobject<UTextRenderComponent>(TEXT("ValidationLabel"));
	if (ValidationLabel)
	{
		ValidationLabel->SetupAttachment(Root);
		ValidationLabel->SetHiddenInGame(true);
		ValidationLabel->SetHorizontalAlignment(EHTA_Center);
		ValidationLabel->SetWorldSize(18.f);
		ValidationLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ValidationLabel->SetCanEverAffectNavigation(false);
	}
#endif
}

void ALadderActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Generate();
}

void ALadderActor::BeginPlay()
{
	Super::BeginPlay();
	if (Rungs->GetInstanceCount() == 0)
	{
		Generate(); // a deferred / runtime spawn whose construction ran before its parameters were set
	}
	NavLink->SetMoveReachedLink(this, &ALadderActor::HandleLinkReached);
	if (ULadderSubsystem* Ladders = GetWorld() ? GetWorld()->GetSubsystem<ULadderSubsystem>() : nullptr)
	{
		Ladders->Register(this);
	}
}

void ALadderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULadderSubsystem* Ladders = GetWorld() ? GetWorld()->GetSubsystem<ULadderSubsystem>() : nullptr)
	{
		Ladders->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool ALadderActor::IsClimbable() const
{
	return LadderRules::IsClimbable(RungCount, bTopExit, GetContract().MinClimbableRungs);
}

LadderRules::FClipContract ALadderActor::GetContract() const
{
	LadderRules::FClipContract Contract;
	Contract.Pitch = Pitch;
	Contract.HangStandoff = HangStandoff;
	Contract.HangLateral = HangLateral;
	Contract.TopStandoff = TopStandoff;
	Contract.TopExitStandoff = TopExitStandoff;
	return Contract;
}

float ALadderActor::GetTopWorldZ() const
{
	return GetActorLocation().Z + LadderRules::TopZ(RungCount, Pitch);
}

FVector ALadderActor::GetRungWorldLocation(int32 K) const
{
	return FTransform(GetActorRotation(), GetActorLocation()).TransformPosition(LadderRules::RungLocal(K, Pitch));
}

FVector ALadderActor::GetHangWorldLocation(float FeetZ) const
{
	return FTransform(GetActorRotation(), GetActorLocation()).TransformPosition(LadderRules::HangLocal(GetContract(), FeetZ));
}

FVector ALadderActor::GetBottomLinkWorld() const
{
	return FTransform(GetActorRotation(), GetActorLocation()).TransformPosition(LadderRules::BottomLinkLocal(GetContract()));
}

FVector ALadderActor::GetTopLinkWorld() const
{
	return FTransform(GetActorRotation(), GetActorLocation()).TransformPosition(LadderRules::TopLinkLocal(GetContract(), RungCount));
}

FVector ALadderActor::GetTopExitWorld() const
{
	return FTransform(GetActorRotation(), GetActorLocation()).TransformPosition(LadderRules::TopExitLocal(GetContract(), RungCount));
}

float ALadderActor::GetClimberYaw() const
{
	return LadderRules::ClimberYaw(GetActorRotation().Yaw);
}

bool ALadderActor::TryLease(AActor* Climber)
{
	if (!Climber || IsBusyFor(Climber))
	{
		return false;
	}
	LeaseHolder = Climber;
	LeaveQueue(Climber);
	return true;
}

bool ALadderActor::IsWaitingHere(const AActor* Climber) const
{
	const ULadderClimbComponent* Climb = Climber ? Climber->FindComponentByClass<ULadderClimbComponent>() : nullptr;
	return Climb && Climb->IsQueued() && Climb->GetLadder() == this;
}

void ALadderActor::JoinQueue(AActor* Climber)
{
	if (Climber && !Waiting.Contains(Climber))
	{
		Waiting.Add(Climber);
	}
}

void ALadderActor::LeaveQueue(AActor* Climber)
{
	Waiting.RemoveAll([Climber](const TWeakObjectPtr<AActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Climber; });
}

int32 ALadderActor::GetQueueIndex(const AActor* Climber) const
{
	int32 Index = 0;
	for (const TWeakObjectPtr<AActor>& Entry : Waiting)
	{
		const AActor* Waiter = Entry.Get();
		if (!Waiter || !IsWaitingHere(Waiter))
		{
			continue; // left without leaving the list (killed, re-ordered): skipped
		}
		if (Waiter == Climber)
		{
			return Index;
		}
		++Index;
	}
	return INDEX_NONE;
}

int32 ALadderActor::GetQueueLength() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& Entry : Waiting)
	{
		Count += Entry.IsValid() && IsWaitingHere(Entry.Get()) ? 1 : 0;
	}
	return Count;
}

bool ALadderActor::IsBusyFor(const AActor* Climber) const
{
	if (IsLeasedByOther(Climber))
	{
		return true;
	}
	for (const TWeakObjectPtr<AActor>& Entry : Waiting)
	{
		const AActor* Waiter = Entry.Get();
		if (Waiter && IsWaitingHere(Waiter))
		{
			return Waiter != Climber; // the first live waiter goes next
		}
	}
	return false;
}

void ALadderActor::ReleaseLease(AActor* Climber)
{
	if (!LeaseHolder.IsValid() || LeaseHolder.Get() == Climber)
	{
		LeaseHolder.Reset();
	}
}

int32 ALadderActor::GetRungInstanceCount() const
{
	return Rungs ? Rungs->GetInstanceCount() : 0;
}

int32 ALadderActor::GetRailInstanceCount() const
{
	return Rails ? Rails->GetInstanceCount() : 0;
}

int32 ALadderActor::GetBracketInstanceCount() const
{
	return Brackets ? Brackets->GetInstanceCount() : 0;
}

const FLadderMeshData& ALadderActor::ResolveMeshData() const
{
	if (const FLadderMeshData* Row = LadderType.DataTable ? LadderType.GetRow<FLadderMeshData>(TEXT("ALadderActor")) : nullptr)
	{
		return *Row;
	}
	return MeshData;
}

void ALadderActor::ApplyLook(UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh, const FLinearColor& Color, UMaterialInterface* Material)
{
	if (!Component)
	{
		return;
	}
	Component->SetStaticMesh(Mesh);
	if (Material)
	{
		UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Material, this);
		Look->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Look);
	}
}

bool ALadderActor::DetectWall() const
{
	const UWorld* World = GetWorld();
	if (!World || RungCount <= 0)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderWall), false, this);
	const FTransform Frame(GetActorRotation(), GetActorLocation());
	const float Top = LadderRules::TopZ(RungCount, Pitch);
	int32 Hits = 0;
	for (const float Share : { 0.3f, 0.6f, 0.85f })
	{
		const FVector From = Frame.TransformPosition(FVector(2.f, 0.f, Top * Share));
		const FVector To = Frame.TransformPosition(FVector(-WallSearchCm, 0.f, Top * Share));
		FHitResult Hit;
		Hits += World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) ? 1 : 0;
	}
	return Hits >= 2;
}

void ALadderActor::Generate()
{
	using namespace LadderActorInternal;
	RungCount = LadderRules::RungCountForHeight(Height, Pitch);
	const float Top = LadderRules::TopZ(RungCount, Pitch);
	const FLadderMeshData& Look = ResolveMeshData();
	UStaticMesh* RailMesh = LoadMesh(Look.RailMesh, CylinderPath);
	UStaticMesh* RungMesh = LoadMesh(Look.RungMesh, CylinderPath);
	UStaticMesh* BracketMesh = LoadMesh(Look.BracketMesh, CubePath);
	UMaterialInterface* Material = Look.Material.IsNull() ? LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath) : Look.Material.LoadSynchronous();
	ApplyLook(Rails, RailMesh, Look.RailColor, Material);
	ApplyLook(GrabBars, RailMesh, Look.RailColor, Material);
	ApplyLook(Rungs, RungMesh, Look.RungColor, Material);
	ApplyLook(Brackets, BracketMesh, Look.RailColor, Material);
	for (UInstancedStaticMeshComponent* Component : { Rails.Get(), Rungs.Get(), Brackets.Get(), GrabBars.Get() })
	{
		Component->ClearInstances();
	}

	const float HalfWidth = Width * 0.5f;
	FVector RailExtent, RailOrigin;
	MeshBox(RailMesh, RailExtent, RailOrigin);
	const float SegmentLen = 2.f * RailExtent.Z;
	const int32 Segments = LadderRules::RailSegmentCount(Top, SegmentLen, bTopExit ? RailOvershootSegments : 0, Pitch);
	if (Segments == INDEX_NONE)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s: bad geometry (segment %.1f cm, pitch %.1f cm, top %.1f cm) - nothing drawn"),
			*GetName(), SegmentLen, Pitch, Top);
		ValidationText = TEXT("Error: bad pitch / rail mesh");
		UpdateNavLink();
		return;
	}
	const float RailScaleXY = Look.RailDiameterCm / (2.f * FMath::Max(RailExtent.X, RailExtent.Y));
	const float RailBottom = RailOrigin.Z - RailExtent.Z; // the mesh pivot read from the bounds
	for (const float Side : { -1.f, 1.f })
	{
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const FVector Location(0.f, Side * HalfWidth, Index * SegmentLen - RailBottom);
			Rails->AddInstance(FTransform(FRotator::ZeroRotator, Location, FVector(RailScaleXY, RailScaleXY, 1.f)));
		}
	}

	FVector RungExtent, RungOrigin;
	MeshBox(RungMesh, RungExtent, RungOrigin);
	const float RungScaleXY = Look.RungDiameterCm / (2.f * FMath::Max(RungExtent.X, RungExtent.Y));
	const float RungScaleZ = Width / (2.f * RungExtent.Z);
	for (int32 K = 1; K <= RungCount; ++K)
	{
		// The mesh's length axis (Z) turned across the ladder (Y); its centre on the rung centre.
		const FRotator Across(0.f, 0.f, 90.f);
		const FVector Centre = LadderRules::RungLocal(K, Pitch) - Across.RotateVector(RungOrigin * FVector(RungScaleXY, RungScaleXY, RungScaleZ));
		Rungs->AddInstance(FTransform(Across, Centre, FVector(RungScaleXY, RungScaleXY, RungScaleZ)));
	}

	// Wall brackets (auto: only with a wall within WallSearchCm behind the rungs).
	bHasWall = WallBracketCount > 0 || DetectWall();
	const int32 BracketCount = WallBracketCount >= 0 ? WallBracketCount : (bHasWall ? 3 : 0);
	FVector BracketExtent, BracketOrigin;
	MeshBox(BracketMesh, BracketExtent, BracketOrigin);
	for (const float Z : LadderRules::BracketHeights(BracketCount, Pitch, Top))
	{
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Scale(WallSearchCm / (2.f * BracketExtent.X), 4.f / (2.f * BracketExtent.Y), 4.f / (2.f * BracketExtent.Z));
			Brackets->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-WallSearchCm * 0.5f, Side * HalfWidth, Z) - BracketOrigin * Scale, Scale));
		}
	}

	// Grab bars: from the rail top back over the platform and down onto it (the climber passes between them).
	if (bTopGrabBars && bTopExit && RailOvershootSegments > 0)
	{
		const float BarTop = Top + FMath::Min(100.f, RailOvershootSegments * SegmentLen);
		const float Back = TopExitStandoff + 10.f;
		for (const float Side : { -1.f, 1.f })
		{
			const float Y = Side * (HalfWidth + 2.f);
			// Across, at the bar top (length Back along -X).
			const FRotator AlongX(90.f, 0.f, 0.f);
			const FVector TopScale(RailScaleXY, RailScaleXY, Back / SegmentLen);
			GrabBars->AddInstance(FTransform(AlongX, FVector(-Back * 0.5f, Y, BarTop) - AlongX.RotateVector(RailOrigin * TopScale), TopScale));
			// The leg down to the platform.
			const FVector LegScale(RailScaleXY, RailScaleXY, (BarTop - Top) / SegmentLen);
			GrabBars->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-Back, Y, Top + (BarTop - Top) * 0.5f) - RailOrigin * LegScale, LegScale));
		}
	}

	// The click proxy around the rungs.
	ClickProxy->SetBoxExtent(FVector(10.f, HalfWidth + 6.f, FMath::Max(Top, Pitch) * 0.5f));
	ClickProxy->SetRelativeLocation(FVector(0.f, 0.f, FMath::Max(Top, Pitch) * 0.5f));
	ClickProxy->SetCollisionEnabled(IsClimbable() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

	UpdateNavLink();
	Validate();
}

void ALadderActor::UpdateNavLink()
{
	if (!NavLink)
	{
		return;
	}
	const LadderRules::FClipContract Contract = GetContract();
	NavLink->SetLinkData(LadderRules::BottomLinkLocal(Contract), LadderRules::TopLinkLocal(Contract, RungCount), ENavLinkDirection::BothWays);
	// The climbing enemies' filter opens UNavArea_Ladder only: a ladder closed to the enemies gets the squad-only area (7.5).
	NavLink->SetEnabledArea(bAllowEnemies ? UNavArea_Ladder::StaticClass() : UNavArea_LadderSquadOnly::StaticClass());
	// A decoration ladder (< 7 rungs / no top exit): the link is off (its disabled area is Null - nothing paths over it).
	NavLink->SetEnabled(IsClimbable());
}

void ALadderActor::Validate()
{
	TArray<FString> Errors;
	TArray<FString> Warnings;
	if (RungCount < GetContract().MinClimbableRungs)
	{
		Warnings.Add(FString::Printf(TEXT("Decoration: < %d rungs, not climbable"), GetContract().MinClimbableRungs));
	}
	else if (!bTopExit)
	{
		Warnings.Add(TEXT("Decoration: no top exit"));
	}
	const UWorld* World = GetWorld();
	if (World && IsClimbable())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderValidate), false, this);
		FHitResult Hit;
		const FVector Bottom = GetBottomLinkWorld();
		if (!World->LineTraceSingleByChannel(Hit, Bottom + FVector(0.f, 0.f, 50.f), Bottom - FVector(0.f, 0.f, 20.f), ECC_Visibility, Params))
		{
			Errors.Add(TEXT("no floor under the bottom link point (<= 20 cm)"));
		}
		const FVector TopPoint = GetTopLinkWorld();
		if (!World->LineTraceSingleByChannel(Hit, TopPoint + FVector(0.f, 0.f, 8.f), TopPoint - FVector(0.f, 0.f, 8.f), ECC_Visibility, Params))
		{
			Errors.Add(FString::Printf(TEXT("no top floor at %.0f cm (+-8) under the top link point"), LadderRules::TopZ(RungCount, Pitch)));
		}
	}
	ValidationText = FString::Join(Errors, TEXT("; "));
	const FString WarningText = FString::Join(Warnings, TEXT("; "));
	if (!ValidationText.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s: %s"), *GetName(), *ValidationText);
	}
#if WITH_EDITORONLY_DATA
	if (ValidationLabel)
	{
		const bool bError = !ValidationText.IsEmpty();
		const FString Shown = bError ? TEXT("Error: ") + ValidationText : WarningText;
		ValidationLabel->SetText(FText::FromString(Shown));
		ValidationLabel->SetTextRenderColor(bError ? FColor(255, 40, 40) : FColor(255, 180, 40));
		ValidationLabel->SetRelativeLocation(FVector(30.f, 0.f, LadderRules::TopZ(RungCount, Pitch) + 60.f));
		ValidationLabel->SetVisibility(!Shown.IsEmpty());
	}
#endif
	if (ValidationText.IsEmpty() && !WarningText.IsEmpty())
	{
		ValidationText.Empty(); // warnings are not errors (the editor text shows them in amber)
	}
}

void ALadderActor::HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint)
{
	UPathFollowingComponent* PathFollowing = Cast<UPathFollowingComponent>(PathComp);
	AController* Controller = PathFollowing ? Cast<AController>(PathFollowing->GetOwner()) : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	ULadderClimbComponent* Climb = Pawn ? Pawn->FindComponentByClass<ULadderClimbComponent>() : nullptr;
	const bool bGoingUp = DestPoint.Z > (GetFloorZ() + GetTopWorldZ()) * 0.5f;
	if (Climb && Climb->NotifyLinkReached(this, bGoingUp))
	{
		return; // the climber's component owns the move now (it aborts the path and resumes it at the other end)
	}
	// Nobody to climb it (a body without the climb component - the enemies' filter keeps them off the link): the path ends here.
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: %s reached the link without a climb - path stopped"), *GetName(), *GetNameSafe(Pawn));
	if (Controller)
	{
		Controller->StopMovement();
	}
}
