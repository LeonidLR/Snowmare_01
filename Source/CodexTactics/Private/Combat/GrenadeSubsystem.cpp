#include "Combat/GrenadeSubsystem.h"
#include "AI/StealthLureRules.h"
#include "AI/StealthLureSubsystem.h"
#include "Combat/SignalFlareActor.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/TacticalHeatTuning.h"
#include "CodexTactics.h"
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
	GlowInstances.Reset();
	for (UInstancedStaticMeshComponent* Component : { RangeRing.Get(), BlastRing.Get(), Arc.Get() })
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Glow, this);
		Instance->SetScalarParameterValue(TEXT("Intensity"), 2.5f);
		Component->SetMaterial(0, Instance);
		GlowInstances.Add(Instance);
	}
	const bool bWanted = bLureStyle;
	bLureStyle = !bWanted; // force the colours
	SetLureStyle(bWanted);
	if (UMaterialInterface* Blast = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_AoeBlast.M_AoeBlast")))
	{
		BlastZone->SetMaterial(0, Blast);
	}
}

void AGrenadeAimActor::SetLureStyle(bool bLure)
{
	if (bLureStyle == bLure && bMaterialsReady)
	{
		return;
	}
	bLureStyle = bLure;
	if (GlowInstances.Num() != 3)
	{
		return; // applied when the materials are set up
	}
	// Grenade: Godot range ring (1, 0.72, 0.15), AoE ring (1, 0.3, 0.15), arc (1, 0.82, 0.25). Lure: a pale blue hearing ring.
	const FLinearColor Colors[] = {
		bLure ? FLinearColor(0.35f, 0.45f, 0.55f) : FLinearColor(1.f, 0.72f, 0.15f),
		bLure ? FLinearColor(0.2f, 0.75f, 1.f) : FLinearColor(1.f, 0.3f, 0.15f),
		bLure ? FLinearColor(0.9f, 0.95f, 1.f) : FLinearColor(1.f, 0.82f, 0.25f) };
	// Lure: the range ring dim, the hearing circle bright (it is what the player reads).
	const float Intensities[] = { bLure ? 0.6f : 2.5f, bLure ? 3.f : 2.5f, bLure ? 1.5f : 2.5f };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (GlowInstances[Index])
		{
			GlowInstances[Index]->SetVectorParameterValue(TEXT("Color"), Colors[Index]);
			GlowInstances[Index]->SetScalarParameterValue(TEXT("Intensity"), Intensities[Index]);
		}
	}
}

void AGrenadeAimActor::ShowRing(const FVector& Center, float Radius)
{
	SetupMaterials();
	TArray<FTransform> Ring;
	AddCircle(Ring, Center + FVector(0.f, 0.f, 4.f), Radius, 48, GrenadeLineWidth * 1.5f);
	RangeRing->ClearInstances();
	Arc->ClearInstances();
	BlastRing->ClearInstances();
	BlastRing->AddInstances(Ring, false, true);
	BlastZone->SetVisibility(false);
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

void AGrenadeAimActor::Show(const FVector& ThrowerGround, float MaxRange, const FVector& Target, float BlastRadius, const FVector& Hand, bool bShowBlastZone)
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
	BlastZone->SetVisibility(bShowBlastZone);
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
		Post(LOCTEXT("Squad", "SQUAD"), LOCTEXT("NoGrenades", "The selected operative has no grenades."));
		return false;
	}
	Thrower = InThrower;
	bFlareAim = false;
	bLureAim = false;
	ShowAimActor(InThrower);
	const FText Stance = InThrower->GetStance() == EOperativeStance::Crouching ? LOCTEXT("Crouch", "crouched (max 9 m)")
		: (InThrower->GetStance() == EOperativeStance::Prone ? LOCTEXT("Prone", "prone (max 6 m)") : LOCTEXT("Stand", "standing (max 12 m)"));
	Post(InThrower->DisplayName, FText::Format(LOCTEXT("AimPrompt", "🧨 Pick a throw point, {0}. RMB or Esc to cancel."), Stance));
	return true;
}

void UGrenadeSubsystem::ShowAimActor(AOperativeCharacter* InThrower)
{
	if (!AimActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AimActor = GetWorld()->SpawnActor<AGrenadeAimActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (AimActor)
	{
		AimActor->SetLureStyle(bLureAim);
		AimActor->SetActorHiddenInGame(false);
	}
	UpdateAim(InThrower->GetActorLocation() + InThrower->GetActorForwardVector() * 500.f);
}

void UGrenadeSubsystem::CancelAim(bool bRevertWeapon)
{
	if (bRevertWeapon && !bFlareAim && !bLureAim)
	{
		TakeFirearm(Thrower.Get());
	}
	Thrower.Reset();
	bFlareAim = false;
	bLureAim = false;
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
	Info.MaxRange = bLureAim ? StealthLureRules::GetThrowRange(UStealthLureSubsystem::GetParams(), ThrowerActor->GetStance())
		: bFlareAim ? TacticalHeatRules::GetFlareThrowRange(TacticalHeatTuning::Get(), ThrowerActor->GetStance())
		: GrenadeRules::EffectiveRange(ThrowerActor->GrenadeThrowRange > 0.f ? ThrowerActor->GrenadeThrowRange : 1200.f, ThrowerActor->GetStance());
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
	UStealthLureSubsystem* Lure = GetWorld()->GetSubsystem<UStealthLureSubsystem>();
	if (bLureAim)
	{
		// The lure works only exploring undetected: a fight / a detection ends the aim (one line with the reason).
		const EStealthLureRefusal Refusal = Lure ? Lure->GetAvailability(ThrowerActor, EStealthLureKind::Throw) : EStealthLureRefusal::Unable;
		if (Refusal != EStealthLureRefusal::None)
		{
			Post(ThrowerActor->DisplayName, StealthLureRules::GetRefusalText(EStealthLureKind::Throw, Refusal,
				Lure ? Lure->GetCooldownLeft(ThrowerActor, EStealthLureKind::Throw) : 0.f));
			CancelAim(false);
			return;
		}
	}
	else if ((bFlareAim ? ThrowerActor->FlaresCount : ThrowerActor->GrenadesCount) <= 0)
	{
		CancelAim();
		return;
	}
	const FGrenadeAimInfo Info = GetAimInfo(CursorPoint);
	if (AimActor && Info.bValid)
	{
		const FVector Ground = ThrowerActor->GetActorLocation() - FVector(0.f, 0.f, ThrowerActor->GetSimpleCollisionHalfHeight());
		const float EffectRadius = bLureAim ? StealthLureRules::GetRadius(UStealthLureSubsystem::GetParams(), EStealthLureKind::Throw)
			: bFlareAim ? TacticalHeatTuning::Get().FlareRevealRadius
			: (ThrowerActor->GrenadeEffectRadius > 0.f ? ThrowerActor->GrenadeEffectRadius : 400.f);
		AimActor->SetLureStyle(bLureAim);
		AimActor->Show(Ground, Info.MaxRange, Info.Target, EffectRadius,
			ThrowerActor->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight), !bLureAim);
	}
	if (bLureAim && Lure && Info.bValid)
	{
		Lure->UpdatePreview(Info.Target, EStealthLureKind::Throw, false); // the enemies that would hear it (HUD highlight)
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
		Post(LOCTEXT("Grenade", "Grenade"), LOCTEXT("NoPoint", "Could not find a landing point."));
		return nullptr;
	}
	if (bLureAim)
	{
		ThrowLureAtCursor(CursorPoint);
		return nullptr;
	}
	if (bFlareAim)
	{
		// Sprint 15: a click next to the thrower lights the flare at his feet, otherwise it flies along the arc.
		const bool bAtFeet = FVector::Dist2D(ThrowerActor->GetActorLocation(), CursorPoint) <= TacticalHeatTuning::Get().FlareAtFeetDistance;
		const ASignalFlareActor* Flare = bAtFeet ? LightFlareAtFeet(ThrowerActor) : ThrowFlareAt(ThrowerActor, Info.Target);
		if (Flare)
		{
			Post(ThrowerActor->DisplayName, FText::Format(bAtFeet ? LOCTEXT("FlareLit", "🎆 Flare lit at my feet! Left: {0}.")
				: LOCTEXT("FlareThrown", "🎆 Flare out! Left: {0}."), ThrowerActor->FlaresCount));
			CancelAim(false);
		}
		return nullptr;
	}
	AGrenadeActor* Grenade = ThrowAt(ThrowerActor, Info.Target);
	if (Grenade)
	{
		Post(ThrowerActor->DisplayName, FText::Format(LOCTEXT("Thrown", "🧨 Grenade thrown. Left: {0}."), ThrowerActor->GrenadesCount));
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
	InThrower->GrenadeReleaseSeconds = -1.f; // the anim instance (or the Blueprint event) sets the clip's own release time
	InThrower->OnGrenadeThrowNative.Broadcast();
	InThrower->ReceiveGrenadeThrow();
	Grenade->ConfigureFrom(InThrower, GrenadeRules::ReleaseDelay(InThrower->GrenadeThrowDuration, InThrower->GrenadeReleaseSeconds));
	Grenade->ThrowTo(Target + FVector(0.f, 0.f, 12.f));
	--InThrower->GrenadesCount;
	if (InThrower->GrenadesCount <= 0)
	{
		TakeFirearm(InThrower);
	}
	return Grenade;
}

bool UGrenadeSubsystem::StartLureAim(AOperativeCharacter* InThrower)
{
	UStealthLureSubsystem* Lure = GetWorld()->GetSubsystem<UStealthLureSubsystem>();
	const EStealthLureRefusal Refusal = Lure ? Lure->GetAvailability(InThrower, EStealthLureKind::Throw) : EStealthLureRefusal::Unable;
	if (Refusal != EStealthLureRefusal::None)
	{
		Post(InThrower ? InThrower->DisplayName : LOCTEXT("Squad", "SQUAD"), StealthLureRules::GetRefusalText(EStealthLureKind::Throw, Refusal,
			Lure ? Lure->GetCooldownLeft(InThrower, EStealthLureKind::Throw) : 0.f));
		return false;
	}
	if (IsAiming())
	{
		CancelAim(true);
	}
	Thrower = InThrower;
	bFlareAim = false;
	bLureAim = true;
	ShowAimActor(InThrower);
	const FStealthLureParams Params = UStealthLureSubsystem::GetParams();
	Post(InThrower->DisplayName, FText::Format(LOCTEXT("LureAim",
		"Stone: pick a point (max {0} m). Enemies within {1} m of it will come to look. RMB or Esc to cancel."),
		FMath::RoundToInt(StealthLureRules::GetThrowRange(Params, InThrower->GetStance()) / 100.f),
		FMath::RoundToInt(StealthLureRules::GetRadius(Params, EStealthLureKind::Throw) / 100.f)));
	return true;
}

bool UGrenadeSubsystem::ThrowLureAtCursor(const FVector& CursorPoint)
{
	AOperativeCharacter* ThrowerActor = Thrower.Get();
	UStealthLureSubsystem* Lure = GetWorld()->GetSubsystem<UStealthLureSubsystem>();
	if (!ThrowerActor || !bLureAim || !Lure)
	{
		CancelAim(false);
		return false;
	}
	const FGrenadeAimInfo Info = GetAimInfo(CursorPoint);
	if (!Info.bValid)
	{
		Post(ThrowerActor->DisplayName, LOCTEXT("NoPoint", "Could not find a landing point."));
		return false;
	}
	const bool bThrown = Lure->ThrowAt(ThrowerActor, Info.Target) != nullptr;
	CancelAim(false);
	return bThrown;
}

bool UGrenadeSubsystem::StartFlareAim(AOperativeCharacter* InThrower)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!InThrower || InThrower->FlaresCount <= 0)
	{
		Post(LOCTEXT("Squad", "SQUAD"), LOCTEXT("NoFlares", "The selected operative has no signal flares."));
		return false;
	}
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		Post(LOCTEXT("Squad", "SQUAD"), LOCTEXT("NoFlareTurnBased", "Flares are thrown in real time or the tactical pause, not in turn-based combat."));
		return false;
	}
	if (IsAiming())
	{
		CancelAim(true);
	}
	Thrower = InThrower;
	bFlareAim = true;
	bLureAim = false;
	ShowAimActor(InThrower);
	const float Range = TacticalHeatRules::GetFlareThrowRange(TacticalHeatTuning::Get(), InThrower->GetStance());
	Post(InThrower->DisplayName, FText::Format(LOCTEXT("FlareAim",
		"🎆 Signal flare [x{0}]: pick a point (max {1} m) or click at my feet to light it here. RMB or Esc to cancel."),
		InThrower->FlaresCount, FMath::RoundToInt(Range / 100.f)));
	return true;
}

ASignalFlareActor* UGrenadeSubsystem::ThrowFlareAt(AOperativeCharacter* InThrower, const FVector& Target)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!InThrower || InThrower->FlaresCount <= 0 || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return nullptr;
	}
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	UClass* Class = GameMode && GameMode->SignalFlareClass ? GameMode->SignalFlareClass.Get() : ASignalFlareActor::StaticClass();
	const float Range = TacticalHeatRules::GetFlareThrowRange(TacticalHeatTuning::Get(), InThrower->GetStance());
	const FVector Landing = GrenadeRules::ClampTarget(InThrower->GetActorLocation(), Target, Range);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASignalFlareActor* Flare = GetWorld()->SpawnActor<ASignalFlareActor>(Class, InThrower->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight),
		FRotator::ZeroRotator, Params);
	if (!Flare)
	{
		return nullptr;
	}
	// The grenade throw clip (the same hand movement); the flare leaves the hand at its release point.
	InThrower->GrenadeReleaseSeconds = -1.f;
	InThrower->OnGrenadeThrowNative.Broadcast();
	InThrower->ReceiveGrenadeThrow();
	Flare->Throw(InThrower, Landing, GrenadeRules::ReleaseDelay(InThrower->GrenadeThrowDuration, InThrower->GrenadeReleaseSeconds));
	--InThrower->FlaresCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] %s throws a flare %.1f m (left %d)"), *InThrower->DisplayName.ToString(),
		FVector::Dist2D(InThrower->GetActorLocation(), Landing) / 100.f, InThrower->FlaresCount);
	return Flare;
}

ASignalFlareActor* UGrenadeSubsystem::LightFlareAtFeet(AOperativeCharacter* Operative)
{
	if (!Operative || Operative->FlaresCount <= 0)
	{
		return nullptr;
	}
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight())
		+ Operative->GetActorForwardVector() * 60.f;
	ASignalFlareActor* Flare = ASignalFlareActor::SpawnLit(GetWorld(), Feet, GameMode ? GameMode->SignalFlareClass : nullptr);
	if (Flare)
	{
		--Operative->FlaresCount;
	}
	return Flare;
}

void UGrenadeSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	// Stealth lure: useless once a fight is on - the stone aim ends.
	if (IsAimingLure() && !StealthLureRules::IsExploring(Phase, CombatMode))
	{
		CancelAim(false);
	}
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
		Post(LOCTEXT("HQ", "HQ"), FText::Format(LOCTEXT("Refunded",
			"🔄 Switching to turn-based: throw cancelled, grenades (x{0}) returned to inventory."), Refunded));
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
