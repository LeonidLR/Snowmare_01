#include "Interactables/TripwireActor.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireRules.h"
#include "Level/CrawlGapActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavModifierComponent.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/OverheadLabel.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Tripwire"

namespace TripwireLook
{
	const FLinearColor SquadWire(0.55f, 0.55f, 0.5f);
	const FLinearColor PMCWire(0.85f, 0.12f, 0.08f);
	const FLinearColor PreviewValid(0.f, 0.9f, 1.f);

	/** A peg: a 40 cm stake driven into the snow; a bracket: a small block on the object at the wire. */
	FVector PegScale(bool bBracket)
	{
		return bBracket ? FVector(0.12f, 0.12f, 0.12f) : FVector(0.05f, 0.05f, 0.4f);
	}
}

UNavArea_Tripwire::UNavArea_Tripwire()
{
	DefaultCost = 25.f; // AUTO mode: the squad goes round a known trap when there is any way round
	DrawColor = FColor(255, 160, 0);
}

namespace TripwireNav
{
	/** Known traps cost nothing (a literal player order; rev 5e). */
	void AddLiteralTrapArea(TArray<FNavigationFilterArea>& Areas)
	{
		FNavigationFilterArea& Wire = Areas.AddDefaulted_GetRef();
		Wire.AreaClass = UNavArea_Tripwire::StaticClass();
		Wire.bOverrideTravelCost = true;
		Wire.TravelCostOverride = 1.f;
		Wire.bOverrideEnteringCost = true;
		Wire.EnteringCostOverride = 0.f;
	}
}

UNavFilter_TrapLiteral::UNavFilter_TrapLiteral()
{
	TripwireNav::AddLiteralTrapArea(Areas);
}

UNavFilter_NoCrawlTrapLiteral::UNavFilter_NoCrawlTrapLiteral()
{
	TripwireNav::AddLiteralTrapArea(Areas);
	FNavigationFilterArea& Crawl = Areas.AddDefaulted_GetRef();
	Crawl.AreaClass = UNavArea_CrawlOnly::StaticClass();
	Crawl.bIsExcluded = true;
}

ATripwireActor::ATripwireActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DisplayName = LOCTEXT("Name", "MUV-3 Tripwire Mine");
	bCanBeRelocated = false;
	InteractionDistance = 150.f;

	// The wire: a thin bar at 30 cm, clickable (blocks the cursor's Visibility trace), never on the navmesh.
	Box->SetBoxExtent(FVector(100.f, 4.f, 4.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* CylinderMesh = Cylinder.Object;
	UMaterialInterface* ShapeMaterial = BaseMaterial.Object;
	auto MakePeg = [this, CylinderMesh, ShapeMaterial](const TCHAR* Name)
	{
		UStaticMeshComponent* Peg = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Peg->SetupAttachment(Box);
		Peg->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Peg->SetCanEverAffectNavigation(false);
		Peg->SetCastShadow(false);
		if (CylinderMesh)
		{
			Peg->SetStaticMesh(CylinderMesh);
		}
		if (ShapeMaterial)
		{
			Peg->SetMaterial(0, ShapeMaterial);
		}
		return Peg;
	};
	PegA = MakePeg(TEXT("PegA"));
	PegB = MakePeg(TEXT("PegB"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}

	NavBox = CreateDefaultSubobject<UBoxComponent>(TEXT("NavBox"));
	NavBox->SetupAttachment(Box);
	NavBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NavBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	NavBox->SetCanEverAffectNavigation(false);
}

APMCTripwireActor::APMCTripwireActor()
{
	Faction = ETripwireFaction::PMC;
	bArmedAtLevelStart = true;
	bHiddenUntilSpotted = true;
	DisplayName = LOCTEXT("PMCName", "PMC Tripwire");
}

void ATripwireActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!bSetUp)
	{
		// In the editor the wire is drawn between the two viewport handles (actor space).
		LayoutLocal(AnchorA, AnchorB);
	}
}

void ATripwireActor::LayoutLocal(const FVector& LocalA, const FVector& LocalB)
{
	const FVector Span(LocalB.X - LocalA.X, LocalB.Y - LocalA.Y, 0.f);
	const float Length = FMath::Max(10.f, static_cast<float>(Span.Size()));
	const FVector Mid = (LocalA + LocalB) * 0.5f;
	Mesh->SetRelativeLocation(FVector(Mid.X, Mid.Y, Mid.Z + TripwireRules::WireHeightCm));
	Mesh->SetRelativeRotation(Span.IsNearlyZero() ? FRotator::ZeroRotator : FRotator(0.f, Span.Rotation().Yaw, 0.f));
	Mesh->SetRelativeScale3D(FVector(Length / 100.f, 0.02f, 0.02f));
	PegA->SetRelativeLocation(LocalA + FVector(0.f, 0.f, bAnchorABracket ? TripwireRules::WireHeightCm : 20.f));
	PegA->SetRelativeRotation(FRotator::ZeroRotator);
	PegA->SetRelativeScale3D(TripwireLook::PegScale(bAnchorABracket));
	PegB->SetRelativeLocation(LocalB + FVector(0.f, 0.f, bAnchorBBracket ? TripwireRules::WireHeightCm : 20.f));
	PegB->SetRelativeRotation(FRotator::ZeroRotator);
	PegB->SetRelativeScale3D(TripwireLook::PegScale(bAnchorBBracket));
}

void ATripwireActor::BeginPlay()
{
	Super::BeginPlay();
	if (bArmedAtLevelStart && !bSetUp && !bPreview)
	{
		ArmFromPlacement();
	}
}

void ATripwireActor::ArmFromPlacement()
{
	const FTransform Transform = GetActorTransform();
	auto Ground = [this](const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TripwireAnchorGround), false, this);
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		if (GetWorld() && GetWorld()->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, 60.f), Point - FVector(0.f, 0.f, 400.f), Objects, Params))
		{
			return FVector(Hit.ImpactPoint);
		}
		return Point;
	};
	const FVector GroundA = Ground(Transform.TransformPosition(AnchorA));
	const FVector GroundB = Ground(Transform.TransformPosition(AnchorB));
	const float Span = FVector::Dist2D(GroundA, GroundB);
	if (!TripwireRules::IsSpanValid(Span))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Tripwire %s: placed span %.1f m is outside 1-5 m (kept as placed)"), *GetName(), Span / 100.f);
	}
	Setup(GroundA, GroundB, bAnchorABracket, bAnchorBBracket, false, nullptr);
	ArmingLeft = 0.f; // a placed wire is armed from the start
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: %s wire laid at level start (%.1f m, %s)"), *GetName(), IsHostile() ? TEXT("PMC") : TEXT("squad"),
		Span / 100.f, IsConcealed() ? TEXT("hidden") : TEXT("visible"));
}

float ATripwireActor::BodyHeight(const AActor& Actor)
{
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(&Actor))
	{
		return SightRules::ProfileHeight(Operative->GetStance());
	}
	if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(&Actor))
	{
		return SightRules::ProfileHeight(Marksman->GetStance());
	}
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	return Character ? Character->GetSimpleCollisionHalfHeight() * 2.f * 0.85f : 100.f;
}

void ATripwireActor::Setup(const FVector& GroundA, const FVector& GroundB, bool bAOnObject, bool bBOnObject, bool bInPreview, AActor* RiggedBy)
{
	bSetUp = true;
	bPreview = bInPreview;
	bAnchorAOnObject = bAOnObject;
	bAnchorBOnObject = bBOnObject;
	WireA = GroundA;
	WireB = GroundB;
	const FVector Span = FVector(GroundB.X - GroundA.X, GroundB.Y - GroundA.Y, 0.f);
	const float Length = FMath::Max(10.f, static_cast<float>(Span.Size()));
	const float GroundZ = (GroundA.Z + GroundB.Z) * 0.5f;
	SetActorLocationAndRotation(FVector((GroundA.X + GroundB.X) * 0.5f, (GroundA.Y + GroundB.Y) * 0.5f, GroundZ + TripwireRules::WireHeightCm),
		Span.IsNearlyZero() ? FRotator::ZeroRotator : Span.Rotation());
	Box->SetBoxExtent(FVector(Length * 0.5f, 4.f, 4.f));
	Mesh->SetRelativeLocation(FVector::ZeroVector);
	Mesh->SetRelativeRotation(FRotator::ZeroRotator);
	Mesh->SetRelativeScale3D(FVector(Length / 100.f, 0.02f, 0.02f));
	// A peg: a 40 cm stake driven into the snow; a bracket: a small block on the object at the wire.
	PegA->SetRelativeRotation(FRotator::ZeroRotator);
	PegB->SetRelativeRotation(FRotator::ZeroRotator);
	PegA->SetRelativeLocation(FVector(-Length * 0.5f, 0.f, bAOnObject ? 0.f : -TripwireRules::WireHeightCm + 20.f));
	PegA->SetRelativeScale3D(TripwireLook::PegScale(bAOnObject));
	PegB->SetRelativeLocation(FVector(Length * 0.5f, 0.f, bBOnObject ? 0.f : -TripwireRules::WireHeightCm + 20.f));
	PegB->SetRelativeScale3D(TripwireLook::PegScale(bBOnObject));
	if (!WireMaterial)
	{
		WireMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (WireMaterial)
	{
		WireMaterial->SetVectorParameterValue(TEXT("Color"), bPreview ? TripwireLook::PreviewValid : (IsHostile() ? TripwireLook::PMCWire : TripwireLook::SquadWire));
	}
	if (bPreview)
	{
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}
	// The squad's own wire arms after the rigger stepped away; a PMC wire is a live charge (bTrapped: the defusal rules).
	ArmingLeft = IsHostile() ? 0.f : TripwireRules::ArmingSeconds;
	bTrapped = IsHostile();
	if (RiggedBy)
	{
		Ignored.Add(RiggedBy);
	}
	// The squad's path cost (AUTO mode, rev 5e): a 1.2 m wide band along the wire, 80 cm past each anchor so a path round its end
	// keeps a body clear of the peg (a PMC wire only once the squad knows of it).
	NavBox->SetBoxExtent(FVector(Length * 0.5f + 80.f, 60.f, 40.f));
	ApplyConcealment();
}

void ATripwireActor::EnsureNavModifier()
{
	if (!NavModifier && !bPreview)
	{
		NavModifier = NewObject<UNavModifierComponent>(this);
		NavModifier->SetAreaClass(UNavArea_Tripwire::StaticClass());
		// The shapes of the wire are no navigation geometry (CanEverAffectNavigation false): the modifier uses its failsafe box -
		// sized to the 1.2 m band along the wire (actor space: X along it), not the 2 x 2 m default.
		NavModifier->FailsafeExtent = NavBox->GetUnscaledBoxExtent();
		NavModifier->RegisterComponent();
	}
}

void ATripwireActor::ApplyConcealment()
{
	const bool bShow = !IsConcealed();
	Mesh->SetVisibility(bShow);
	PegA->SetVisibility(bShow);
	PegB->SetVisibility(bShow);
	Box->SetCollisionResponseToChannel(ECC_Visibility, bShow ? ECR_Block : ECR_Ignore);
	if (bShow)
	{
		EnsureNavModifier();
	}
	else if (NavModifier)
	{
		NavModifier->DestroyComponent(); // hidden again (a load): the squad does not know to path round it
		NavModifier = nullptr;
	}
}

void ATripwireActor::RestoreSavedState(bool bInHostile, bool bInSpotted)
{
	Faction = bInHostile ? ETripwireFaction::PMC : ETripwireFaction::Squad;
	bSpotted = bInSpotted;
	if (bSetUp && !bPreview)
	{
		ApplyConcealment();
	}
}

void ATripwireActor::SetPreviewValid(bool bValid)
{
	if (WireMaterial)
	{
		WireMaterial->SetVectorParameterValue(TEXT("Color"), bValid ? TripwireLook::PreviewValid : FLinearColor(1.f, 0.1f, 0.1f));
	}
}

void ATripwireActor::Reveal(AOperativeCharacter* Spotter)
{
	RevealBy(Spotter, false);
}

bool ATripwireActor::IsHiddenTrap() const
{
	return IsConcealed() && bSetUp && !bPreview && !bTripped && ArmingLeft <= 0.f;
}

void ATripwireActor::RevealByPeek(AOperativeCharacter* Spotter)
{
	if (Spotter && IsHiddenTrap())
	{
		RevealBy(Spotter, true);
	}
}

void ATripwireActor::RevealBy(AOperativeCharacter* Spotter, bool bByPeek)
{
	if (!IsConcealed())
	{
		return;
	}
	bSpotted = true;
	ApplyConcealment();
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: spotted by %s%s"), *GetName(), Spotter ? *Spotter->DisplayName.ToString() : TEXT("-"),
		bByPeek ? TEXT(" (corner peek)") : TEXT(""));
	if (!Spotter)
	{
		return;
	}
	// The mine rule (Godot _on_mine_spotted): the whole squad stops, the spotter faces the wire and warns. Rev 5h: a peek spotter
	// keeps his corner pose; the squad switches to Solo (NotifyTrapSpotted -> USquadSubsystem::HoldForSpottedTrap).
	if (!bByPeek)
	{
		const FRotator Facing = (GetActorLocation() - Spotter->GetActorLocation()).Rotation();
		Spotter->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	}
	UFloatingTextSubsystem::SpawnAboveOperative(Spotter, TEXT("⚠️ TRIPWIRE SPOTTED!"), FLinearColor(1.f, 0.85f, 0.1f));
	PostLine(Spotter->DisplayName, IsHostile()
		? LOCTEXT("SpottedHow", "⚠️ Tripwire! Wire at shin height - everyone hold position! Click the wire to disarm it (anyone can try, the Medic-Sapper is best), or crawl under it.")
		: LOCTEXT("Spotted", "⚠️ Tripwire! Wire at shin height - everyone hold position!"));
	NotifyTrapSpotted(Spotter, bByPeek, LOCTEXT("TrapName", "Tripwire"));
	GameplayEvents::Raise(this, GameplayEvents::TripwireSpotted, Spotter);
	if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->DropAllForMine();
	}
}

void ATripwireActor::ScanForSpotters()
{
	// The mine rule: operatives scan outside the wave fight.
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat)
	{
		return;
	}
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad || !CanBeSpottedByProximity())
	{
		return; // rev 5i: a lesson trap waits for its step (only a corner peek reveals it before)
	}
	const FVector2D A(WireA.X, WireA.Y);
	const FVector2D B(WireB.X, WireB.Y);
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member)
			|| FMath::Abs(Member->GetActorLocation().Z - GetActorLocation().Z) > 300.f)
		{
			continue;
		}
		const float Distance = TripwireRules::DistanceToWire(FVector2D(Member->GetActorLocation()), A, B);
		if (TripwireRules::CanSpot(Distance, Member->SquadRole, SpotRangeCm, SapperSpotBonusCm))
		{
			Reveal(Member);
			return;
		}
	}
}

void ATripwireActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPreview || bTripped || !bSetUp || IsAwaitingPickup())
	{
		return; // rev 2026-10-19: a disarmed wire waits harmlessly for the pickup grab
	}
	if (ArmingLeft > 0.f)
	{
		ArmingLeft = DeployableRules::StepArmingCountdown(ArmingLeft, DeltaSeconds); // never negative: saves round-trip exactly
		if (ArmingLeft <= 0.f)
		{
			UFloatingTextSubsystem::SpawnAboveMine(this, TEXT("🪤 TRIPWIRE ARMED"), FLinearColor(1.f, 0.6f, 0.2f));
		}
		return;
	}
	if (IsConcealed())
	{
		ScanForSpotters();
	}
	const FVector2D A(WireA.X, WireA.Y);
	const FVector2D B(WireB.X, WireB.Y);
	const FVector Mid = GetActorLocation();
	const float Reach = FVector::Dist2D(WireA, WireB) * 0.5f + 150.f;
	// APC P4: a vehicle whose hull crosses the wire pulls the pin (friendly side: like an operative) - mines are its threat.
	if (TripwireRules::TriggersOn(IsHostile(), true))
	{
		if (const UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
		{
			for (AVehiclePawn* Vehicle : Vehicles->GetVehicles())
			{
				if (FMath::Abs(Vehicle->GetActorLocation().Z - (Mid.Z - TripwireRules::WireHeightCm)) > 200.f)
				{
					continue;
				}
				bool bCrosses = false;
				for (int32 Step = 0; Step <= 8 && !bCrosses; ++Step)
				{
					bCrosses = Vehicle->IsInsideFootprint(FMath::Lerp(WireA, WireB, Step / 8.f), 20.f);
				}
				if (bCrosses)
				{
					BlastTriggeredByVehicle = Vehicle;
					Trip(Vehicle);
					return;
				}
			}
		}
	}
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		ACharacter* Body = *It;
		const bool bOperative = Body->IsA<AOperativeCharacter>();
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Body);
		if ((!bOperative && !Enemy) || !TripwireRules::TriggersOn(IsHostile(), bOperative)
			|| FVector::Dist2D(Body->GetActorLocation(), Mid) > Reach || Body == Disarmer.Get())
		{
			continue;
		}
		const UHealthComponent* Health = Body->FindComponentByClass<UHealthComponent>();
		if ((Health && !Health->IsAlive()) || (Enemy && Enemy->IsDying()))
		{
			continue;
		}
		// The wire hangs 30 cm over the ground: only bodies standing near that ground meet it (not ones on a ledge above).
		const float Feet = Body->GetActorLocation().Z - Body->GetSimpleCollisionHalfHeight();
		if (FMath::Abs(Feet - (Mid.Z - TripwireRules::WireHeightCm)) > 80.f)
		{
			continue;
		}
		const bool bTouches = TripwireRules::TouchesWire(FVector2D(Body->GetActorLocation()), Body->GetSimpleCollisionRadius(), A, B);
		const int32 IgnoredIndex = Ignored.IndexOfByPredicate([Body](const TWeakObjectPtr<AActor>& Entry) { return Entry.Get() == Body; });
		if (IgnoredIndex != INDEX_NONE)
		{
			if (!bTouches)
			{
				Ignored.RemoveAt(IgnoredIndex); // the rigger stepped off: the wire counts for him too from now on
			}
			continue;
		}
		if (bTouches && TripwireRules::TripsWire(BodyHeight(*Body)))
		{
			Trip(Body);
			return;
		}
	}
}

void ATripwireActor::Trip(AActor* Tripper)
{
	if (bTripped || bPreview || IsAwaitingPickup())
	{
		return;
	}
	bTripped = true;
	bDisarming = false;
	GetWorldTimerManager().ClearTimer(DisarmTimer);
	if (IsConcealed())
	{
		bSpotted = true; // the click gives it away
		ApplyConcealment();
	}
	UFloatingTextSubsystem::SpawnAboveMine(this, TEXT("❗ CLICK!"), FLinearColor(1.f, 0.85f, 0.2f));
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: pin pulled by %s"), *GetName(), Tripper ? *Tripper->GetName() : TEXT("-"));
	GameplayEvents::Raise(this, GameplayEvents::TripwireTriggered, Tripper);
	ReceivePinPulled();
	GetWorldTimerManager().SetTimer(FuseTimer, FTimerDelegate::CreateUObject(this, &ATripwireActor::Detonate), TripwireRules::FuseDelaySeconds, false);
}

void ATripwireActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	Trip(nullptr);
}

void ATripwireActor::Detonate()
{
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: paired F-1 blast"), *GetName());
	bTrapped = false;
	ApplyBlast(BlastDamage, BlastDamage * SquadDamageShare, BlastRadiusCm,
		TripwireRules::ArmorPenetration, EDamageType::Explosive, LOCTEXT("Source", "F-1 Tripwire"),
		LOCTEXT("SquadLine", "💥 Tripwire blast! -{0} HP"), EStatusEffect::Stagger, TripwireRules::StaggerSeconds);
	// Sprint 11: the blast is heard — patrols within 20 m break off.
	AEnemyCharacter::AlertPatrolsNearTrap(GetWorld(), (WireA + WireB) * 0.5f);
	Destroy();
}

ATripwireActor* ATripwireActor::FindClickableWireNear(const UWorld* World, const FVector& Point)
{
	if (!World)
	{
		return nullptr;
	}
	ATripwireActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ATripwireActor> It(World); It; ++It)
	{
		ATripwireActor* Wire = *It;
		if (!Wire->bSetUp || Wire->bPreview || Wire->bTripped || Wire->IsConcealed() || Wire->IsActorBeingDestroyed()
			|| FMath::Abs(Point.Z - (Wire->GetActorLocation().Z - TripwireRules::WireHeightCm)) > 100.f)
		{
			continue;
		}
		const FVector2D A(Wire->WireA.X, Wire->WireA.Y);
		const FVector2D B(Wire->WireB.X, Wire->WireB.Y);
		const FVector2D Click(Point.X, Point.Y);
		const float Distance = TripwireRules::DistanceToWire(Click, A, B);
		if (TripwireRules::IsClickOnWire(Click, A, B) && Distance < BestDistance)
		{
			Best = Wire;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool ATripwireActor::CanBeDisarmedBy(const AOperativeCharacter* Operative) const
{
	return Operative && TripwireRules::CanDisarm(IsHostile(), Operative->SquadRole);
}

float ATripwireActor::GetDisarmSecondsFor(const AOperativeCharacter* Operative) const
{
	const EOperativeRole SquadRole = Operative ? Operative->SquadRole : EOperativeRole::Commander;
	if (!IsHostile())
	{
		return TripwireRules::GetDisarmSeconds(false, SquadRole);
	}
	switch (SquadRole)
	{
	case EOperativeRole::MedicSapper: return DisarmSecondsSapper;
	case EOperativeRole::Engineer: return DisarmSecondsEngineer;
	default: return DisarmSecondsOther;
	}
}

FDefusalChance ATripwireActor::GetDefusalChance(const AOperativeCharacter* Operative) const
{
	if (!IsHostile() || !Operative)
	{
		return Super::GetDefusalChance(Operative);
	}
	return TripwireRules::GetHostileDisarmChance(Operative->SquadRole, Operative->Luck, Operative->ColdLevel, FailedDefusalAttempts, bDefusalWarned);
}

FActionMenuRequest ATripwireActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	if (IsHostile())
	{
		const FDefusalChance Odds = GetDefusalChance(Leader);
		const FText Status = Odds.bDangerous
			? LOCTEXT("PMCHighRisk", "⚠️ HIGH RISK OF DETONATION!")
			: FText::Format(LOCTEXT("PMCChance", "Success chance: ~{0}%"), FMath::FloorToInt(Odds.Chance));
		const FText Cold = Odds.ColdPenalty > 0.f
			? FText::Format(LOCTEXT("PMCCold", "\nCold hands: -{0}% (warm up first)."), FMath::FloorToInt(Odds.ColdPenalty))
			: FText::GetEmpty();
		const float Seconds = GetDisarmSecondsFor(Leader);
		return FActionMenuRequest::MakeMenu(DisplayName,
			FText::Format(LOCTEXT("PMCDesc", "Two grenades on a wire at shin height, rigged by the PMC. Anyone can try to disarm it crouched ({0} s); the Medic-Sapper is best.\nOperator: {1} | {2}{3}"),
				FText::AsNumber(FMath::RoundToInt(Seconds)), Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Operative"), Status, Cold),
			FText::Format(LOCTEXT("PMCDisarm", "Disarm ({0} s)"), FText::AsNumber(FMath::RoundToInt(Seconds))), LOCTEXT("Cancel", "Cancel"),
			bDisarming || bTripped);
	}
	const bool bSapper = Leader && Leader->SquadRole == EOperativeRole::MedicSapper;
	return FActionMenuRequest::MakeMenu(DisplayName,
		bSapper ? LOCTEXT("DescSapper", "Two F-1s on an MUV-3 fuze. The Medic-Sapper disarms it prone in 3 s and recovers the grenades (one on a fumble).")
				: LOCTEXT("DescOther", "Two F-1s on an MUV-3 fuze. Only the Medic-Sapper can disarm the tripwire."),
		LOCTEXT("Disarm", "Disarm (3 s)"), LOCTEXT("Cancel", "Cancel"), !bSapper);
}

void ATripwireActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User || bTripped || bDisarming || !CanBeDisarmedBy(User) || IsConcealed())
	{
		return;
	}
	Disarmer = User;
	DisarmerStance = User->GetStance();
	bDisarming = true;
	User->StopOperative();
	User->SetFacingPoint(GetDisarmFocusPoint(User->GetActorLocation())); // 2026-10-15: the wire point in front of him
	// The squad wire: flat under it; a PMC wire: crouched over the fuze, as a mine.
	User->SetStance(IsHostile() ? EOperativeStance::Crouching : EOperativeStance::Prone);
	PostLine(User->DisplayName, LOCTEXT("Disarming", "🪤 Disarming the tripwire, stay back!"));
	GetWorldTimerManager().SetTimer(DisarmTimer, FTimerDelegate::CreateUObject(this, &ATripwireActor::FinishDisarm),
		FMath::Max(0.1f, GetDisarmSecondsFor(User)), false);
}

void ATripwireActor::FinishDisarm()
{
	bDisarming = false;
	AOperativeCharacter* User = Disarmer.Get();
	if (!User || bTripped)
	{
		Disarmer.Reset();
		return;
	}
	if (IsHostile())
	{
		ResolveHostileDisarm(User, DisarmerStance);
		return;
	}
	// The squad's own wire is disarmed lying under it: he stays prone, and prone has no pickup clip yet - the grab is at once.
	BeginDisarmPickup(User, User->GetStance());
}

void ATripwireActor::CompleteDisarmPickup(AOperativeCharacter* User)
{
	PostDeferredDefusalLine();
	if (IsHostile())
	{
		if (User)
		{
			User->GrenadesCount += GrenadesRecovered;
			PostLine(User->DisplayName, FText::Format(LOCTEXT("PMCDisarmed", "🪤 PMC tripwire disarmed. +{0} grenades"), GrenadesRecovered));
		}
	}
	else if (User)
	{
		const bool bFumble = FMath::FRand() < TripwireRules::DisarmFumbleChance;
		const int32 Returned = TripwireRules::GrenadesReturned(bFumble);
		User->GrenadesCount += Returned;
		PostLine(User->DisplayName, FText::Format(bFumble ? LOCTEXT("DisarmedFumble", "🪤 Tripwire disarmed, one grenade ruined. +{0} grenade")
			: LOCTEXT("Disarmed", "🪤 Tripwire disarmed. +{0} grenades"), Returned));
		UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s disarmed by %s: +%d grenades"), *GetName(), *User->DisplayName.ToString(), Returned);
	}
	GameplayEvents::Raise(this, GameplayEvents::TripwireDisarmed, User);
	Destroy();
}

FVector ATripwireActor::GetDisarmFocusPoint(const FVector& From) const
{
	const FVector2D Closest = TrapApproachRules::ClosestPointOnWire(FVector2D(WireA), FVector2D(WireB), FVector2D(From));
	return FVector(Closest.X, Closest.Y, GetActorLocation().Z - TripwireRules::WireHeightCm);
}

FVector ATripwireActor::GetDisarmApproachPoint(const FVector& From) const
{
	const FVector2D Point = TrapApproachRules::WireApproachPoint(FVector2D(WireA), FVector2D(WireB), FVector2D(From),
		TrapApproachRules::GoalDistance(DisarmStandOffCm));
	return FVector(Point.X, Point.Y, From.Z);
}

EDefusalResult ATripwireActor::ResolveHostileDisarm(AOperativeCharacter* User, EOperativeStance OriginalStance)
{
	const FDefusalChance Odds = GetDefusalChance(User);
	EDefusalResult Result = EDefusalResult::Warning;
	{
		TGuardValue<bool> DeferLine(bDeferDefusalSuccessLine, true); // the success line waits for the pickup grab
		Result = AttemptDefusal(User); // a detonation trips the wire (DetonateTrap)
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: PMC disarm by %s (chance %.0f%%, cold -%.0f): %s"), *GetName(), *User->DisplayName.ToString(),
		Odds.Chance, Odds.ColdPenalty, Result == EDefusalResult::Success ? TEXT("success") : Result == EDefusalResult::Warning ? TEXT("warning")
			: Result == EDefusalResult::Detonation ? TEXT("detonation") : TEXT("slip"));
	if (Result != EDefusalResult::Detonation)
	{
		Disarmer.Reset();
	}
	if (Result == EDefusalResult::Success)
	{
		// Game designer 2026-10-15 (as the mine): a standing operative stands up again after the crouched work and picks the grenades
		// up (full-body clip rising from the low pose); crouched: the upper-body pickup over the crouched legs; prone: none (at once).
		// Rev 2026-10-19: the wire stays (harmless) until the clip's grab moment; then grenades, feed, TripwireDisarmed, removal.
		if (OriginalStance == EOperativeStance::Standing && User->GetStance() != EOperativeStance::Standing)
		{
			User->SetStance(EOperativeStance::Standing);
		}
		BeginDisarmPickup(User, OriginalStance);
	}
	return Result;
}

bool ATripwireActor::TryTurnBasedDisarm(AOperativeCharacter* User)
{
	if (!User || !IsHostile() || IsConcealed() || bTripped || bDisarming)
	{
		return false;
	}
	const FRotator Facing = (GetDisarmFocusPoint(User->GetActorLocation()) - User->GetActorLocation()).Rotation();
	User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	ResolveHostileDisarm(User, User->GetStance()); // no stance change on the grid: the pickup by his stance
	return true;
}

bool ATripwireActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (bPreview || bTripped || IsConcealed())
	{
		return false;
	}
	if (IsHostile())
	{
		// Rev 5e (user bug 2026-10-14): say how to deal with it - a click on the wire opens its Disarm menu.
		OutLabel.Text = bDisarming ? TEXT("⚠️ PMC Tripwire (disarming)") : TEXT("⚠️ PMC Tripwire - click to disarm");
		OutLabel.Color = FLinearColor(1.f, 0.25f, 0.15f);
		OutLabel.HeightCm = 50.f;
		return true;
	}
	OutLabel.Text = ArmingLeft > 0.f ? TEXT("🪤 Tripwire (arming)") : TEXT("🪤 Tripwire");
	OutLabel.Color = FLinearColor(1.f, 0.6f, 0.2f);
	OutLabel.HeightCm = 50.f;
	return true;
}

#undef LOCTEXT_NAMESPACE
