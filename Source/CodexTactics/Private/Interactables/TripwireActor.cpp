#include "Interactables/TripwireActor.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/TripwireRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavModifierComponent.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/OverheadLabel.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Tripwire"

UNavArea_Tripwire::UNavArea_Tripwire()
{
	DefaultCost = 25.f; // the squad goes round its own wire when there is any way round
	DrawColor = FColor(255, 160, 0);
}

ATripwireActor::ATripwireActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DisplayName = LOCTEXT("Name", "Растяжка МУВ-3");
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
	bPreview = bInPreview;
	WireA = GroundA;
	WireB = GroundB;
	const FVector Span = FVector(GroundB.X - GroundA.X, GroundB.Y - GroundA.Y, 0.f);
	const float Length = FMath::Max(10.f, static_cast<float>(Span.Size()));
	const float GroundZ = (GroundA.Z + GroundB.Z) * 0.5f;
	SetActorLocationAndRotation(FVector((GroundA.X + GroundB.X) * 0.5f, (GroundA.Y + GroundB.Y) * 0.5f, GroundZ + TripwireRules::WireHeightCm),
		Span.IsNearlyZero() ? FRotator::ZeroRotator : Span.Rotation());
	Box->SetBoxExtent(FVector(Length * 0.5f, 4.f, 4.f));
	Mesh->SetRelativeScale3D(FVector(Length / 100.f, 0.02f, 0.02f));
	// A peg: a 40 cm stake driven into the snow; a bracket: a small block on the object at the wire.
	PegA->SetRelativeLocation(FVector(-Length * 0.5f, 0.f, bAOnObject ? 0.f : -TripwireRules::WireHeightCm + 20.f));
	PegA->SetRelativeScale3D(bAOnObject ? FVector(0.12f, 0.12f, 0.12f) : FVector(0.05f, 0.05f, 0.4f));
	PegB->SetRelativeLocation(FVector(Length * 0.5f, 0.f, bBOnObject ? 0.f : -TripwireRules::WireHeightCm + 20.f));
	PegB->SetRelativeScale3D(bBOnObject ? FVector(0.12f, 0.12f, 0.12f) : FVector(0.05f, 0.05f, 0.4f));
	if (!WireMaterial)
	{
		WireMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (WireMaterial)
	{
		WireMaterial->SetVectorParameterValue(TEXT("Color"), bPreview ? FLinearColor(0.f, 0.9f, 1.f) : FLinearColor(0.55f, 0.55f, 0.5f));
	}
	if (bPreview)
	{
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}
	ArmingLeft = TripwireRules::ArmingSeconds;
	if (RiggedBy)
	{
		Ignored.Add(RiggedBy);
	}
	// The squad's path cost: a 1.2 m wide band along the wire.
	NavBox->SetBoxExtent(FVector(Length * 0.5f + 30.f, 60.f, 40.f));
	if (!NavModifier)
	{
		NavModifier = NewObject<UNavModifierComponent>(this);
		NavModifier->SetAreaClass(UNavArea_Tripwire::StaticClass());
		NavModifier->RegisterComponent();
	}
}

void ATripwireActor::SetPreviewValid(bool bValid)
{
	if (WireMaterial)
	{
		WireMaterial->SetVectorParameterValue(TEXT("Color"), bValid ? FLinearColor(0.f, 0.9f, 1.f) : FLinearColor(1.f, 0.1f, 0.1f));
	}
}

void ATripwireActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPreview || bTripped)
	{
		return;
	}
	if (ArmingLeft > 0.f)
	{
		ArmingLeft -= DeltaSeconds;
		if (ArmingLeft <= 0.f)
		{
			UFloatingTextSubsystem::SpawnAboveMine(this, TEXT("🪤 РАСТЯЖКА ВЗВЕДЕНА"), FLinearColor(1.f, 0.6f, 0.2f));
		}
		return;
	}
	const FVector2D A(WireA.X, WireA.Y);
	const FVector2D B(WireB.X, WireB.Y);
	const FVector Mid = GetActorLocation();
	const float Reach = FVector::Dist2D(WireA, WireB) * 0.5f + 150.f;
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		ACharacter* Body = *It;
		const bool bOperative = Body->IsA<AOperativeCharacter>();
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Body);
		if ((!bOperative && !Enemy) || FVector::Dist2D(Body->GetActorLocation(), Mid) > Reach || Body == Disarmer.Get())
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
	if (bTripped || bPreview)
	{
		return;
	}
	bTripped = true;
	UFloatingTextSubsystem::SpawnAboveMine(this, TEXT("❗ ЩЁЛК!"), FLinearColor(1.f, 0.85f, 0.2f));
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: pin pulled by %s"), *GetName(), Tripper ? *Tripper->GetName() : TEXT("-"));
	ReceivePinPulled();
	GetWorldTimerManager().SetTimer(FuseTimer, FTimerDelegate::CreateUObject(this, &ATripwireActor::Detonate), TripwireRules::FuseDelaySeconds, false);
}

void ATripwireActor::Detonate()
{
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s: paired Ф-1 blast"), *GetName());
	ApplyBlast(TripwireRules::BlastDamage, TripwireRules::BlastDamage * TripwireRules::SquadDamageShare, TripwireRules::BlastRadiusCm,
		TripwireRules::ArmorPenetration, EDamageType::Explosive, LOCTEXT("Source", "Растяжка Ф-1"),
		LOCTEXT("SquadLine", "💥 Подрыв на растяжке! -{0} HP"), EStatusEffect::Stagger, TripwireRules::StaggerSeconds);
	// Sprint 11: the blast is heard — patrols within 20 m break off.
	AEnemyCharacter::AlertPatrolsNearTrap(GetWorld(), (WireA + WireB) * 0.5f);
	Destroy();
}

FActionMenuRequest ATripwireActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const bool bSapper = Leader && Leader->SquadRole == EOperativeRole::MedicSapper;
	return FActionMenuRequest::MakeMenu(DisplayName,
		bSapper ? LOCTEXT("DescSapper", "Две Ф-1 на взрывателе МУВ-3. Медик-сапёр снимет её лёжа за 3 с и вернёт гранаты (при срыве — одну).")
				: LOCTEXT("DescOther", "Две Ф-1 на взрывателе МУВ-3. Снять растяжку может только медик-сапёр."),
		LOCTEXT("Disarm", "Обезвредить (3 с)"), LOCTEXT("Cancel", "Отмена"), !bSapper);
}

void ATripwireActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User || User->SquadRole != EOperativeRole::MedicSapper || bTripped || DisarmTimer.IsValid())
	{
		return;
	}
	Disarmer = User;
	User->StopOperative();
	User->SetFacingPoint(GetActorLocation());
	User->SetStance(EOperativeStance::Prone); // flat under the wire
	PostLine(User->DisplayName, LOCTEXT("Disarming", "🪤 Снимаю растяжку, не подходить!"));
	GetWorldTimerManager().SetTimer(DisarmTimer, FTimerDelegate::CreateUObject(this, &ATripwireActor::FinishDisarm), TripwireRules::DisarmSeconds, false);
}

void ATripwireActor::FinishDisarm()
{
	AOperativeCharacter* User = Disarmer.Get();
	if (!User || bTripped)
	{
		return;
	}
	const bool bFumble = FMath::FRand() < TripwireRules::DisarmFumbleChance;
	const int32 Returned = TripwireRules::GrenadesReturned(bFumble);
	User->GrenadesCount += Returned;
	PostLine(User->DisplayName, FText::Format(bFumble ? LOCTEXT("DisarmedFumble", "🪤 Растяжка снята, одна граната испорчена. +{0} граната")
		: LOCTEXT("Disarmed", "🪤 Растяжка снята. +{0} гранаты"), Returned));
	UE_LOG(LogCodexTactics, Display, TEXT("Tripwire %s disarmed by %s: +%d grenades"), *GetName(), *User->DisplayName.ToString(), Returned);
	Destroy();
}

bool ATripwireActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (bPreview || bTripped)
	{
		return false;
	}
	OutLabel.Text = ArmingLeft > 0.f ? TEXT("🪤 Растяжка (взводится)") : TEXT("🪤 Растяжка");
	OutLabel.Color = FLinearColor(1.f, 0.6f, 0.2f);
	OutLabel.HeightCm = 50.f;
	return true;
}

#undef LOCTEXT_NAMESPACE
