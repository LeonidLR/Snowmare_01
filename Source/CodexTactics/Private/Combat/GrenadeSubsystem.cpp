#include "Combat/GrenadeSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeRules.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "GrenadeSubsystem"

namespace
{
	constexpr float GrenadeLineWidth = 8.f;

	/** A segment is the 100 x 100 cm engine plane stretched from A to B (tilted along the arc). */
	FTransform GrenadeSegment(const FVector& A, const FVector& B, float Width)
	{
		const FVector Delta = B - A;
		return FTransform(Delta.Rotation(), (A + B) * 0.5f, FVector(Delta.Size() / 100.f, Width / 100.f, 1.f));
	}
}

// --- Aim indicators -------------------------------------------------------------------------------------------------

AGrenadeAimActor::AGrenadeAimActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* PlaneMesh = PlaneFinder.Object;
	auto Make = [this, PlaneMesh](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetCanEverAffectNavigation(false);
		if (PlaneMesh)
		{
			Mesh->SetStaticMesh(PlaneMesh);
		}
		return Mesh;
	};
	RangeRing = Make(TEXT("RangeRing"));
	BlastRing = Make(TEXT("BlastRing"));
	Arc = Make(TEXT("Arc"));
	BlastZone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlastZone"));
	BlastZone->SetupAttachment(GetRootComponent());
	BlastZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlastZone->SetCastShadow(false);
	BlastZone->SetCanEverAffectNavigation(false);
	BlastZone->SetVisibility(false);
	if (PlaneMesh)
	{
		BlastZone->SetStaticMesh(PlaneMesh);
	}
}

void AGrenadeAimActor::SetupMaterials()
{
	if (bMaterialsReady)
	{
		return;
	}
	bMaterialsReady = true;
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback"));
	if (!Glow)
	{
		return;
	}
	const TPair<UInstancedStaticMeshComponent*, FLinearColor> Specs[] = {
		{ RangeRing, FLinearColor(1.f, 0.72f, 0.15f) },
		{ BlastRing, FLinearColor(1.f, 0.3f, 0.15f) },
		{ Arc, FLinearColor(1.f, 0.82f, 0.25f) } };
	for (const TPair<UInstancedStaticMeshComponent*, FLinearColor>& Spec : Specs)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Glow, this);
		Instance->SetVectorParameterValue(TEXT("Color"), Spec.Value);
		Instance->SetScalarParameterValue(TEXT("Intensity"), 2.5f);
		Spec.Key->SetMaterial(0, Instance);
	}
	if (UMaterialInterface* Blast = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_AoeBlast.M_AoeBlast")))
	{
		BlastZone->SetMaterial(0, Blast);
	}
}

void AGrenadeAimActor::AddCircle(TArray<FTransform>& Out, const FVector& Center, float Radius, int32 Segments, float Width)
{
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = 2.f * PI * Index / Segments;
		const float A1 = 2.f * PI * (Index + 1) / Segments;
		Out.Add(GrenadeSegment(Center + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.f) * Radius,
			Center + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.f) * Radius, Width));
	}
}

void AGrenadeAimActor::Show(const FVector& ThrowerGround, float MaxRange, const FVector& Target, float BlastRadius, const FVector& Hand)
{
	SetupMaterials();
	TArray<FTransform> Range, Blast, ArcSegments;
	AddCircle(Range, ThrowerGround + FVector(0.f, 0.f, 3.f), MaxRange, 64, GrenadeLineWidth);
	AddCircle(Blast, Target + FVector(0.f, 0.f, 4.f), BlastRadius, 32, GrenadeLineWidth * 1.5f);
	// Godot: 24 segments from the hand (1.35 m) to 5 cm above the target, 2.6 m high.
	const FVector End = Target + FVector(0.f, 0.f, 5.f);
	FVector Previous = Hand;
	for (int32 Index = 1; Index <= 24; ++Index)
	{
		const FVector Point = GrenadeRules::ArcPoint(Hand, End, Index / 24.f);
		ArcSegments.Add(GrenadeSegment(Previous, Point, GrenadeLineWidth));
		Previous = Point;
	}
	RangeRing->ClearInstances();
	BlastRing->ClearInstances();
	Arc->ClearInstances();
	RangeRing->AddInstances(Range, false, true);
	BlastRing->AddInstances(Blast, false, true);
	Arc->AddInstances(ArcSegments, false, true);
	// Godot plane 8.2 m for the 4 m radius: the engine plane is 100 cm.
	BlastZone->SetWorldLocationAndRotation(Target + FVector(0.f, 0.f, 3.f), FRotator::ZeroRotator);
	BlastZone->SetWorldScale3D(FVector(BlastRadius * 2.05f / 100.f, BlastRadius * 2.05f / 100.f, 1.f));
	BlastZone->SetVisibility(true);
}

bool AGrenadeAimActor::IsBlastZoneShown() const
{
	return BlastZone && BlastZone->IsVisible() && BlastZone->GetMaterial(0) && BlastZone->GetMaterial(0)->GetName() == TEXT("M_AoeBlast");
}

int32 AGrenadeAimActor::GetSegmentCount() const
{
	return RangeRing->GetInstanceCount() + BlastRing->GetInstanceCount() + Arc->GetInstanceCount();
}

// --- Subsystem ------------------------------------------------------------------------------------------------------

void UGrenadeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!GrenadeClass)
	{
		GrenadeClass = AGrenadeActor::StaticClass();
	}
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UGrenadeSubsystem::HandleGameFlowChanged);
	}
}

bool UGrenadeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGrenadeSubsystem::Post(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

void UGrenadeSubsystem::TakeFirearm(AOperativeCharacter* Operative)
{
	if (Operative && Operative->CurrentWeapon && Operative->CurrentWeapon->WeaponId == TEXT("grenade"))
	{
		if (!Operative->SwitchToWeaponById(TEXT("m16")) && !Operative->SwitchToWeaponById(TEXT("pistol")))
		{
			Operative->SwitchToWeaponById(TEXT("knife"));
		}
	}
}

bool UGrenadeSubsystem::StartAim(AOperativeCharacter* InThrower)
{
	if (!InThrower || InThrower->GrenadesCount <= 0)
	{
		Post(LOCTEXT("Squad", "Отряд"), LOCTEXT("NoGrenades", "В личном инвентаре выбранного бойца нет гранат."));
		return false;
	}
	Thrower = InThrower;
	if (!AimActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AimActor = GetWorld()->SpawnActor<AGrenadeAimActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (AimActor)
	{
		AimActor->SetActorHiddenInGame(false);
	}
	UpdateAim(InThrower->GetActorLocation() + InThrower->GetActorForwardVector() * 500.f);
	const FText Stance = InThrower->GetStance() == EOperativeStance::Crouching ? LOCTEXT("Crouch", "сидя (макс. 9м)")
		: (InThrower->GetStance() == EOperativeStance::Prone ? LOCTEXT("Prone", "лёжа (макс. 6м)") : LOCTEXT("Stand", "стоя (макс. 12м)"));
	Post(InThrower->DisplayName, FText::Format(LOCTEXT("AimPrompt", "🧨 Выберите точку броска {0}. ПКМ или Esc — отмена."), Stance));
	return true;
}

void UGrenadeSubsystem::CancelAim(bool bRevertWeapon)
{
	if (bRevertWeapon)
	{
		TakeFirearm(Thrower.Get());
	}
	Thrower.Reset();
	if (AimActor)
	{
		AimActor->SetActorHiddenInGame(true);
	}
}

FGrenadeAimInfo UGrenadeSubsystem::GetAimInfo(const FVector& CursorPoint) const
{
	FGrenadeAimInfo Info;
	const AOperativeCharacter* ThrowerActor = Thrower.Get();
	if (!ThrowerActor)
	{
		return Info;
	}
	const FVector From = ThrowerActor->GetActorLocation();
	Info.MaxRange = GrenadeRules::EffectiveRange(ThrowerActor->GrenadeThrowRange > 0.f ? ThrowerActor->GrenadeThrowRange : 1200.f, ThrowerActor->GetStance());
	const float Flat = FVector::Dist2D(From, CursorPoint);
	Info.bValid = true;
	Info.Target = GrenadeRules::ClampTarget(From, CursorPoint, Info.MaxRange);
	Info.Distance = FMath::Min(Flat, Info.MaxRange);
	Info.bInRange = Flat <= Info.MaxRange;
	return Info;
}

void UGrenadeSubsystem::UpdateAim(const FVector& CursorPoint)
{
	AOperativeCharacter* ThrowerActor = Thrower.Get();
	if (!ThrowerActor)
	{
		return;
	}
	if (ThrowerActor->GrenadesCount <= 0)
	{
		CancelAim();
		return;
	}
	const FGrenadeAimInfo Info = GetAimInfo(CursorPoint);
	if (AimActor && Info.bValid)
	{
		const FVector Ground = ThrowerActor->GetActorLocation() - FVector(0.f, 0.f, ThrowerActor->GetSimpleCollisionHalfHeight());
		AimActor->Show(Ground, Info.MaxRange, Info.Target, ThrowerActor->GrenadeEffectRadius > 0.f ? ThrowerActor->GrenadeEffectRadius : 400.f,
			ThrowerActor->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight));
	}
}

AGrenadeActor* UGrenadeSubsystem::ThrowAtCursor(const FVector& CursorPoint)
{
	AOperativeCharacter* ThrowerActor = Thrower.Get();
	if (!ThrowerActor)
	{
		CancelAim();
		return nullptr;
	}
	const FGrenadeAimInfo Info = GetAimInfo(CursorPoint);
	if (!Info.bValid)
	{
		Post(LOCTEXT("Grenade", "Граната"), LOCTEXT("NoPoint", "Не удалось определить точку падения."));
		return nullptr;
	}
	AGrenadeActor* Grenade = ThrowAt(ThrowerActor, Info.Target);
	if (Grenade)
	{
		Post(ThrowerActor->DisplayName, FText::Format(LOCTEXT("Thrown", "🧨 Граната брошена. Осталось: {0}."), ThrowerActor->GrenadesCount));
		CancelAim(false);
	}
	return Grenade;
}

AGrenadeActor* UGrenadeSubsystem::ThrowAt(AOperativeCharacter* InThrower, const FVector& Target)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!InThrower || InThrower->GrenadesCount <= 0 || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGrenadeActor* Grenade = GetWorld()->SpawnActor<AGrenadeActor>(GrenadeClass ? *GrenadeClass : AGrenadeActor::StaticClass(),
		InThrower->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight), FRotator::ZeroRotator, Params);
	if (!Grenade)
	{
		return nullptr;
	}
	InThrower->ReceiveGrenadeThrow();
	Grenade->ConfigureFrom(InThrower, InThrower->GrenadeThrowDuration * GrenadeRules::ReleaseAnimRatio);
	Grenade->ThrowTo(Target + FVector(0.f, 0.f, 12.f));
	--InThrower->GrenadesCount;
	if (InThrower->GrenadesCount <= 0)
	{
		TakeFirearm(InThrower);
	}
	return Grenade;
}

void UGrenadeSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (CombatMode != ECodexCombatMode::TurnBased)
	{
		return;
	}
	// Godot entering Gorky 17: no aim, grenades in flight go back, nobody keeps a grenade in hands.
	if (IsAiming())
	{
		CancelAim(true);
	}
	int32 Refunded = 0;
	for (TActorIterator<AGrenadeActor> It(GetWorld()); It; ++It)
	{
		Refunded += It->CancelAndRefund() ? 1 : 0;
	}
	if (Refunded > 0)
	{
		Post(LOCTEXT("HQ", "ШТАБ"), FText::Format(LOCTEXT("Refunded",
			"🔄 Переход в пошаговый режим: бросок отменён, гранаты ({0} шт.) возвращены в инвентарь."), Refunded));
	}
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			TakeFirearm(Member);
		}
	}
}

#undef LOCTEXT_NAMESPACE
