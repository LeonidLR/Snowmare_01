#include "Characters/MarksmanEnemyCharacter.h"

#include "AIController.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/FloatingTextSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMarksmanEnemyCharacter::AMarksmanEnemyCharacter()
{
	Archetype = EEnemyArchetype::Marksman;
	EnemyDisplayName = TEXT("Снайпер");
	bFearsFire = false;

	AimBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AimBeam"));
	AimBeam->SetupAttachment(RootComponent);
	AimBeam->SetUsingAbsoluteLocation(true);
	AimBeam->SetUsingAbsoluteRotation(true);
	AimBeam->SetUsingAbsoluteScale(true);
	AimBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AimBeam->SetCastShadow(false);
	AimBeam->SetHiddenInGame(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeamMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (BeamMesh.Succeeded())
	{
		AimBeam->SetStaticMesh(BeamMesh.Object);
	}
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VFX/Materials/M_SniperScope_Beam.M_SniperScope_Beam")));
}

namespace MarksmanTuning
{
	// -1 keeps the asset value. Set by Scripts/Tools/jev_ai_coach.py through -dpcvars= for its experiments.
	static TAutoConsoleVariable<float> CVarRetreatCooldown(TEXT("Codex.Marksman.RetreatCooldown"), -1.f, TEXT("Marksman kiting cooldown, s (-1: asset)"));
	static TAutoConsoleVariable<float> CVarRetreatMax(TEXT("Codex.Marksman.RetreatMaxSeconds"), -1.f, TEXT("Marksman longest retreat, s (-1: asset)"));
	static TAutoConsoleVariable<float> CVarShotDamage(TEXT("Codex.Marksman.ShotDamage"), -1.f, TEXT("Marksman shot damage (-1: asset)"));
	static TAutoConsoleVariable<float> CVarAccuracy(TEXT("Codex.Marksman.BaseAccuracy"), -1.f, TEXT("Marksman base hit chance (-1: asset)"));
	static TAutoConsoleVariable<float> CVarAim(TEXT("Codex.Marksman.AimDuration"), -1.f, TEXT("Marksman aim before a shot, s (-1: asset)"));
	static TAutoConsoleVariable<float> CVarCooldown(TEXT("Codex.Marksman.ShotCooldown"), -1.f, TEXT("Marksman pause after a shot, s (-1: asset)"));

	void Override(float& Value, const TAutoConsoleVariable<float>& CVar)
	{
		const float Tuned = CVar.GetValueOnGameThread();
		if (Tuned >= 0.f)
		{
			Value = Tuned;
		}
	}
}

void AMarksmanEnemyCharacter::ApplyTuningOverrides()
{
	using namespace MarksmanTuning;
	Override(MarksmanConfig.RetreatCooldownSeconds, CVarRetreatCooldown);
	Override(MarksmanConfig.RetreatMaxSeconds, CVarRetreatMax);
	Override(MarksmanConfig.ShotDamage, CVarShotDamage);
	Override(MarksmanConfig.BaseAccuracy, CVarAccuracy);
	Override(MarksmanConfig.AimDuration, CVarAim);
	Override(MarksmanConfig.ShotCooldown, CVarCooldown);
}

void AMarksmanEnemyCharacter::HandleMarksmanDied(AActor* Victim, const FString& AttackerSource)
{
	float Distance = 0.f;
	FindClosestOperative(Distance);
	UE_LOG(LogCodexTactics, Display, TEXT("[Marksman] %s dies at %.0f m (by %s)"), *GetName(), Distance / 100.f, *AttackerSource);
}

bool AMarksmanEnemyCharacter::CanKite() const
{
	return GetWorld()->GetTimeSeconds() - LastKiteTime >= MarksmanConfig.RetreatCooldownSeconds;
}

void AMarksmanEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	ApplyTuningOverrides();
	SpawnLocation = GetActorLocation();
	// Waypoints are authored relative to the actor.
	for (FVector& Point : PatrolRoute)
	{
		Point = GetActorTransform().TransformPosition(Point);
	}
	// Sprint 06-G: spawned by a wave (or during its preparation) he fights at once.
	AIState = PatrolRoute.IsEmpty() || IsFightOn() ? EMarksmanAIState::Engage : EMarksmanAIState::Patrol;
	if (HealthComponent)
	{
		HealthComponent->OnDamaged.AddDynamic(this, &AMarksmanEnemyCharacter::HandleMarksmanDamaged);
		HealthComponent->OnDied.AddDynamic(this, &AMarksmanEnemyCharacter::HandleMarksmanDied);
	}
	UMaterialInterface* Material = BeamMaterial.IsNull() ? nullptr : BeamMaterial.LoadSynchronous();
	if (Material)
	{
		AimBeam->SetMaterial(0, Material);
	}
	BeamMaterialInstance = AimBeam->CreateDynamicMaterialInstance(0);
	if (BeamMaterialInstance && !Material)
	{
		BeamMaterialInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.05f, 0.02f)); // engine BasicShapeMaterial
	}
}

float AMarksmanEnemyCharacter::GetAimProgress() const
{
	return bIsAimingAtTarget ? FMath::Clamp(AimTimer / FMath::Max(MarksmanConfig.AimDuration, 0.01f), 0.f, 1.f) : 0.f;
}

FVector AMarksmanEnemyCharacter::GetFeet() const
{
	return GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void AMarksmanEnemyCharacter::SetMarksmanStance(EOperativeStance NewStance)
{
	if (NewStance == Stance)
	{
		return;
	}
	Stance = NewStance;
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float OldHalf = Capsule->GetUnscaledCapsuleHalfHeight();
	const float NewHalf = MarksmanAIRules::GetHalfHeight(MarksmanConfig, NewStance);
	const float Delta = NewHalf - OldHalf;
	// The engine keeps the half height >= the radius: a prone capsule (1/3) gets a smaller radius too.
	if (StandRadius <= 0.f)
	{
		StandRadius = Capsule->GetUnscaledCapsuleRadius();
	}
	Capsule->SetCapsuleSize(FMath::Min(StandRadius, NewHalf), NewHalf);
	// Feet stay on the ground: the actor moves by the height change, the art mesh keeps its world height.
	AddActorWorldOffset(FVector(0.f, 0.f, Delta));
	if (GetMesh())
	{
		GetMesh()->AddRelativeLocation(FVector(0.f, 0.f, -Delta));
	}
	if (BodyMesh)
	{
		FVector Scale = BodyMesh->GetRelativeScale3D();
		Scale.Z *= NewHalf / FMath::Max(OldHalf, 1.f);
		BodyMesh->SetRelativeScale3D(Scale);
	}
	GetCharacterMovement()->MaxWalkSpeed = NewStance == EOperativeStance::Standing ? MarksmanConfig.WalkSpeed
		: (NewStance == EOperativeStance::Crouching ? MarksmanConfig.WalkSpeed * 0.55f : 0.f);
}

void AMarksmanEnemyCharacter::Alert()
{
	if (AIState == EMarksmanAIState::Patrol)
	{
		AIState = EMarksmanAIState::Engage;
	}
}

bool AMarksmanEnemyCharacter::IsFightOn() const
{
	const UWorld* World = GetWorld();
	const UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return (Waves && Waves->IsWaveActive())
		|| (Flow && (Flow->GetPhase() == ECodexGamePhase::WaveCombat || Flow->GetPhase() == ECodexGamePhase::Preparation));
}

void AMarksmanEnemyCharacter::HandleMarksmanDamaged(const FDamageSpec& Spec, float FinalDamage)
{
	if (bIsDying || AIState == EMarksmanAIState::Ambushed || AIState == EMarksmanAIState::Retreat)
	{
		return;
	}
	float Distance = 0.f;
	AOperativeCharacter* Attacker = FindClosestOperative(Distance);
	// The shooter by name (a grenade / turret source falls back to the closest operative).
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive() && Member->DisplayName.ToString() == Spec.AttackerSource)
			{
				Attacker = Member;
				Distance = FVector::Dist(GetActorLocation(), Member->GetActorLocation());
				break;
			}
		}
	}
	// Sprint 06-G: in a fight he answers instead of lying blind — face the shooter, alert the others, then kite
	// (too close), take cover and return fire (line of fire in range), or seek a firing position.
	if (Attacker && (AIState != EMarksmanAIState::Patrol || IsFightOn()))
	{
		SetActorRotation(FRotator(0.f, (Attacker->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
		RetaliationTarget = Attacker;
		RetaliationTime = RetaliationSeconds;
		for (TActorIterator<AMarksmanEnemyCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != this && FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= MarksmanConfig.AlertRadius)
			{
				It->Alert();
			}
		}
		if (CanKite() && MarksmanAIRules::ShouldRetreat(Distance, MarksmanConfig.RetreatDistance))
		{
			StartRetreat(Attacker);
			return;
		}
		AIState = EMarksmanAIState::Engage;
		if (Distance <= MarksmanConfig.PreferredMaxRange + 300.f && TraceLine(Attacker).bHasLos)
		{
			if (AAIController* AIC = Cast<AAIController>(GetController()))
			{
				AIC->StopMovement();
			}
			GetWorldTimerManager().ClearTimer(RiseTimerHandle);
			const bool bElevated = GetFeet().Z - (Attacker->GetActorLocation().Z - Attacker->GetSimpleCollisionHalfHeight()) >= 150.f;
			SetMarksmanStance(MarksmanAIRules::EvaluateBestStance(HasLowCoverTowards(Attacker), bElevated, false));
			bHolding = true;
			AttackTimer = 0.f;
			if (!bIsAimingAtTarget || CurrentTarget.Get() != Attacker)
			{
				CancelAim();
				StartAim();
			}
			CurrentTarget = Attacker;
		}
		else
		{
			FiringSearchCooldown = 0.f; // seek a firing position on the next tick
		}
		return;
	}
	// Shot from afar while patrolling: down at once (smaller profile), alert the others, then relocate.
	if (AIState == EMarksmanAIState::Patrol || Distance > MarksmanConfig.RetreatDistance)
	{
		CancelAim();
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		SetMarksmanStance(EOperativeStance::Prone);
		AIState = EMarksmanAIState::Ambushed;
		StateTimer = 0.f;
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("ЗАСАДА!"), FLinearColor(1.f, 0.8f, 0.2f));
		for (TActorIterator<AMarksmanEnemyCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != this && FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= MarksmanConfig.AlertRadius)
			{
				It->Alert();
			}
		}
	}
}

bool AMarksmanEnemyCharacter::FindFiringPosition(const AOperativeCharacter* Target, FVector& OutPosition) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Target || !Nav)
	{
		return false;
	}
	const float TargetHeight = Target->GetStance() == EOperativeStance::Prone ? 30.f : (Target->GetStance() == EOperativeStance::Crouching ? 90.f : 150.f);
	const FVector Aim = Target->GetActorLocation() - FVector(0.f, 0.f, Target->GetSimpleCollisionHalfHeight() - TargetHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MarksmanFiringPosition), false, this);
	Params.AddIgnoredActor(Target);
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	const float Radii[] = { MarksmanConfig.PreferredMinRange + 200.f, (MarksmanConfig.PreferredMinRange + MarksmanConfig.PreferredMaxRange) * 0.5f,
		MarksmanConfig.PreferredMaxRange - 200.f };
	float BestCost = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const float Radius : Radii)
	{
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Sample = Target->GetActorLocation() + FVector(1.f, 0.f, 0.f).RotateAngleAxis(Step * 22.5f, FVector::UpVector) * Radius;
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Sample, OnNav, FVector(150.f, 150.f, 400.f)))
			{
				continue;
			}
			// A standing scope there sees the target (the stance chosen there only lowers it behind low cover).
			FHitResult Hit;
			const FVector Scope = OnNav.Location + FVector(0.f, 0.f, 150.f);
			if (World->LineTraceSingleByChannel(Hit, Scope, Aim, ECC_Visibility, Params) && Hit.GetActor()
				&& !Hit.GetActor()->IsA<AOperativeCharacter>())
			{
				continue;
			}
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, GetActorLocation(), OnNav.Location, const_cast<AMarksmanEnemyCharacter*>(this));
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{
				continue;
			}
			const float Cost = Path->GetPathLength();
			if (Cost < BestCost)
			{
				BestCost = Cost;
				OutPosition = OnNav.Location;
				bFound = true;
			}
		}
	}
	return bFound;
}

bool AMarksmanEnemyCharacter::HasLineOfFireTo(const AOperativeCharacter* Target) const
{
	return Target && TraceLine(Target).bHasLos;
}

FVector AMarksmanEnemyCharacter::GetSquadCentroid(const FVector& Fallback) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			Sum += Member->GetActorLocation();
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : Fallback;
}

AOperativeCharacter* AMarksmanEnemyCharacter::FindClosestOperative(float& OutDistance) const
{
	OutDistance = TNumericLimits<float>::Max();
	AOperativeCharacter* Best = nullptr;
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			const float Distance = FVector::Dist(GetActorLocation(), Member->GetActorLocation());
			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				Best = Member;
			}
		}
	}
	return Best;
}

AMarksmanEnemyCharacter::FMarksmanLine AMarksmanEnemyCharacter::TraceLine(const AOperativeCharacter* Target) const
{
	FMarksmanLine Line;
	const FVector Feet = GetFeet();
	const float ScopeHeight = Stance == EOperativeStance::Prone ? 35.f : (Stance == EOperativeStance::Crouching ? 95.f : 150.f);
	const float TargetHeight = Target->GetStance() == EOperativeStance::Prone ? 30.f : (Target->GetStance() == EOperativeStance::Crouching ? 90.f : 150.f);
	Line.Start = Feet + FVector(0.f, 0.f, ScopeHeight);
	Line.End = Target->GetActorLocation() - FVector(0.f, 0.f, Target->GetSimpleCollisionHalfHeight() - TargetHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MarksmanLine), false, this);
	Params.AddIgnoredActor(Target);
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	EShotLineHit Kind = EShotLineHit::Clear;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Line.Start, Line.End, ECC_Visibility, Params) && Hit.GetActor()
		&& !Hit.GetActor()->IsA<AOperativeCharacter>())
	{
		Kind = Hit.GetActor()->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
	}
	const FSpitterLine Judged = EnemyAIRules::JudgeSpitterLine(Kind, Target->GetStance(), AIConfig.CrouchCoverReduction);
	Line.bHasLos = Judged.bHasLos;
	Line.Cover = Judged.Cover;
	Line.bTargetInCover = Kind != EShotLineHit::Clear;
	return Line;
}

bool AMarksmanEnemyCharacter::HasLowCoverTowards(const AActor* Target) const
{
	// Something knee-high within 1.5 m towards the target, nothing at chest height: a low obstacle to crouch behind.
	const FVector Feet = GetFeet();
	const FVector Dir = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MarksmanCover), false, this);
	FHitResult Hit;
	const bool bLow = GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0.f, 0.f, 50.f), Feet + FVector(0.f, 0.f, 50.f) + Dir * 150.f,
		ECC_Visibility, Params) && !Hit.GetActor()->IsA<ACharacter>();
	const bool bHigh = GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0.f, 0.f, 140.f), Feet + FVector(0.f, 0.f, 140.f) + Dir * 150.f,
		ECC_Visibility, Params);
	return bLow && !bHigh;
}

void AMarksmanEnemyCharacter::MoveTo(const FVector& Goal, bool bSprint)
{
	MoveGoal = Goal;
	bPendingSprint = bSprint;
	if (GetWorldTimerManager().IsTimerActive(RiseTimerHandle))
	{
		return; // still getting up: the run starts towards the latest goal
	}
	if (Stance == EOperativeStance::Prone)
	{
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		SetMarksmanStance(EOperativeStance::Standing);
		GetCharacterMovement()->MaxWalkSpeed = 0.f; // no creeping while the get-up plays
		GetWorldTimerManager().SetTimer(RiseTimerHandle, this, &AMarksmanEnemyCharacter::FinishRise, RiseDelay, false);
		return;
	}
	ExecuteMoveTo(Goal, bSprint);
}

void AMarksmanEnemyCharacter::FinishRise()
{
	// Dropped again (hit and took cover) or dying meanwhile: no run.
	if (bIsDying || Stance != EOperativeStance::Standing || bHolding)
	{
		return;
	}
	ExecuteMoveTo(MoveGoal, bPendingSprint);
}

void AMarksmanEnemyCharacter::ExecuteMoveTo(const FVector& Goal, bool bSprint)
{
	SetMarksmanStance(EOperativeStance::Standing);
	GetCharacterMovement()->MaxWalkSpeed = bSprint ? MarksmanConfig.SprintSpeed : MarksmanConfig.WalkSpeed;
	MoveGoal = Goal;
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->MoveToLocation(Goal, 60.f, false, true);
	}
}

void AMarksmanEnemyCharacter::StartRetreat(const AOperativeCharacter* Target)
{
	CancelAim();
	bHolding = false;
	const FVector Away = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	AIState = EMarksmanAIState::Retreat;
	StateTimer = 0.f;
	LastKiteTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogCodexTactics, Display, TEXT("[Marksman] %s retreats from %s (%.0f m)"), *GetName(), *Target->DisplayName.ToString(),
		FVector::Dist(GetActorLocation(), Target->GetActorLocation()) / 100.f);
	// Back to the middle of the band; a firing position (reachable, with a line of fire) first — the straight-away point
	// may lie behind a wall with no path, which left him standing 4 m from the squad in a retreat / engage loop.
	const float Wanted = (MarksmanConfig.PreferredMinRange + MarksmanConfig.PreferredMaxRange) * 0.5f;
	FVector Goal = Target->GetActorLocation() + Away * Wanted;
	if (FindFiringPosition(Target, FiringPosition))
	{
		Goal = FiringPosition;
	}
	MoveTo(Goal, true);
}

void AMarksmanEnemyCharacter::StartFlank(const AOperativeCharacter* Target)
{
	CancelAim();
	bHolding = false;
	CampTimer = 0.f;
	AIState = EMarksmanAIState::Flank;
	StateTimer = 0.f;
	MoveTo(MarksmanAIRules::ComputeFlankDestination(GetActorLocation(), Target->GetActorLocation(), Target->GetActorForwardVector(),
		MarksmanConfig.FlankAngleDegrees, MarksmanConfig.FlankDistance), true);
}

void AMarksmanEnemyCharacter::StartAim()
{
	bIsAimingAtTarget = true;
	AimTimer = 0.f;
	AIState = EMarksmanAIState::Aim;
	AimBeam->SetHiddenInGame(false);
}

void AMarksmanEnemyCharacter::CancelAim()
{
	bIsAimingAtTarget = false;
	AimTimer = 0.f;
	AimBeam->SetHiddenInGame(true);
	if (AIState == EMarksmanAIState::Aim)
	{
		AIState = EMarksmanAIState::Engage;
	}
}

void AMarksmanEnemyCharacter::UpdateBeam(const FMarksmanLine& Line)
{
	const FVector Span = Line.End - Line.Start;
	const float Length = Span.Size();
	if (Length < 1.f)
	{
		return;
	}
	// The engine cylinder: 100 cm tall along Z, 100 cm wide; the beam narrows as the aim settles.
	const float Width = FMath::Lerp(0.04f, 0.012f, GetAimProgress());
	AimBeam->SetWorldLocationAndRotation((Line.Start + Line.End) * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
	AimBeam->SetWorldScale3D(FVector(Width, Width, Length / 100.f));
	if (BeamMaterialInstance)
	{
		BeamMaterialInstance->SetScalarParameterValue(TEXT("AimProgress"), GetAimProgress());
	}
}

void AMarksmanEnemyCharacter::Fire(AOperativeCharacter* Target, const FMarksmanLine& Line, float Distance)
{
	++ShotsFired;
	OnAttackStarted(Target);
	if (UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->NotifyAttack(); // the stance's fire clip (UMarksmanAnimInstance)
	}
	const float Chance = MarksmanAIRules::ComputeSniperHitChance(MarksmanConfig, Stance, Target->GetStance(), Line.Cover, Distance);
	const bool bHit = FMath::FRand() < Chance;
	FVector End = Line.End;
	if (bHit)
	{
		const bool bCrit = FMath::FRand() < MarksmanConfig.CritChance;
		Target->TakeHit(MarksmanConfig.ShotDamage * (bCrit ? MarksmanConfig.CritMultiplier : 1.f), EnemyDisplayName, bCrit, false, this);
	}
	else
	{
		End += FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(0.f, 1.f)).GetSafeNormal() * 120.f;
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("ПРОМАХ"), FLinearColor(0.8f, 0.8f, 0.8f));
	}
	// One line per shot for the AI coach (Scripts/Tools/jev_ai_coach.py).
	UE_LOG(LogCodexTactics, Display, TEXT("[Marksman] %s fires at %s: %.0f m, chance %.2f, %s"), *GetName(), *Target->DisplayName.ToString(),
		Distance / 100.f, Chance, bHit ? TEXT("hit") : TEXT("miss"));
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTracer(Line.Start, End, FLinearColor(1.f, 0.85f, 0.3f));
	}
}

void AMarksmanEnemyCharacter::TickBehavior(float DeltaTime)
{
	if (bIsDying || !HealthComponent || !HealthComponent->IsAlive())
	{
		CancelAim();
		return;
	}
	AAIController* AIC = Cast<AAIController>(GetController());
	if (HealthComponent->HasStatusEffect(EStatusEffect::Stagger))
	{
		CancelAim();
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	if (AttackTimer > 0.f)
	{
		AttackTimer -= DeltaTime;
	}
	StateTimer += DeltaTime;

	float Distance = 0.f;
	AOperativeCharacter* Target = FindClosestOperative(Distance);
	// The closest one behind a wall is no target while another stands in his line of fire (a squad split by the yard
	// wall flipped him between the two sides).
	if (Target && AIState != EMarksmanAIState::Patrol && !TraceLine(Target).bHasLos)
	{
		float BestDistance = MarksmanConfig.PreferredMaxRange * 1.5f;
		if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float MemberDistance = Member ? FVector::Dist(GetActorLocation(), Member->GetActorLocation()) : 0.f;
				if (Member && Member != Target && Member->HealthComponent && Member->HealthComponent->IsAlive()
					&& MemberDistance < BestDistance && TraceLine(Member).bHasLos)
				{
					BestDistance = MemberDistance;
					Target = Member;
					Distance = MemberDistance;
				}
			}
		}
	}
	// Sprint 06-G: for a while after being hit he answers the shooter, not the closest operative.
	RetaliationTime -= DeltaTime;
	AOperativeCharacter* Shooter = RetaliationTarget.Get();
	if (RetaliationTime > 0.f && Shooter && Shooter->HealthComponent && Shooter->HealthComponent->IsAlive())
	{
		Target = Shooter;
		Distance = FVector::Dist(GetActorLocation(), Shooter->GetActorLocation());
	}
	CurrentTarget = Target;

	switch (AIState)
	{
	case EMarksmanAIState::Patrol:
		TickPatrol(DeltaTime, Target, Distance);
		return;
	case EMarksmanAIState::Ambushed:
		// Stays down, then breaks for a new firing position off the squad's line.
		if (StateTimer >= MarksmanConfig.AmbushProneSeconds)
		{
			if (Target)
			{
				StartFlank(Target);
			}
			else
			{
				AIState = EMarksmanAIState::Engage;
			}
		}
		return;
	case EMarksmanAIState::Retreat:
	case EMarksmanAIState::Flank:
		// Done on arrival, after 8 s, once stuck for 1.5 s (no way there) or (retreating) once back in the band.
		if (FVector::Dist2D(GetActorLocation(), MoveGoal) < 120.f || StateTimer > 8.f
			|| (AIState == EMarksmanAIState::Retreat && StateTimer > MarksmanConfig.RetreatMaxSeconds)
			|| (StateTimer > 1.5f && GetVelocity().Size2D() < 20.f)
			|| (AIState == EMarksmanAIState::Retreat && Distance >= MarksmanConfig.PreferredMinRange))
		{
			AIState = EMarksmanAIState::Engage;
			if (AIC)
			{
				AIC->StopMovement();
			}
		}
		return;
	default:
		TickEngage(DeltaTime, Target, Distance);
		return;
	}
}

void AMarksmanEnemyCharacter::TickPatrol(float DeltaTime, AOperativeCharacter* Target, float Distance)
{
	// Sprint 06-D: a wave (or its preparation) is on — the fight is known, no more patrolling; otherwise an operative
	// in sight within the detection range wakes him.
	if (IsFightOn() || (Target && Distance <= MarksmanConfig.DetectionRange && TraceLine(Target).bHasLos))
	{
		AIState = EMarksmanAIState::Engage;
		return;
	}
	if (PatrolRoute.IsEmpty())
	{
		return;
	}
	const FVector& Point = PatrolRoute[PatrolIndex % PatrolRoute.Num()];
	if (FVector::Dist2D(GetActorLocation(), Point) < 100.f)
	{
		PatrolIndex = (PatrolIndex + 1) % PatrolRoute.Num();
		return;
	}
	if (MoveGoal != Point || GetVelocity().IsNearlyZero())
	{
		MoveTo(Point, false);
	}
}

void AMarksmanEnemyCharacter::TickEngage(float DeltaTime, AOperativeCharacter* Target, float Distance)
{
	AAIController* AIC = Cast<AAIController>(GetController());
	if (!Target)
	{
		CancelAim();
		bHolding = false;
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	const FMarksmanLine Line = TraceLine(Target);
	CampTimer = Line.bTargetInCover && Distance <= MarksmanConfig.PreferredMaxRange * 1.5f ? CampTimer + DeltaTime : 0.f;
	if (MarksmanAIRules::ShouldFlank(Line.bTargetInCover, CampTimer, MarksmanConfig.FlankAfterCampSeconds))
	{
		StartFlank(Target);
		return;
	}
	EMarksmanMove Move = MarksmanAIRules::ChooseMove(MarksmanConfig, Distance, Line.bHasLos, CanKite());
	// Hysteresis: a holding marksman tolerates 3 m past the band edges (no stand / prone flicker at the edge).
	if (bHolding && Line.bHasLos && ((Move == EMarksmanMove::Approach && Distance < MarksmanConfig.PreferredMaxRange + 300.f)
		|| (Move == EMarksmanMove::BackOff && Distance > MarksmanConfig.PreferredMinRange - 300.f)))
	{
		Move = EMarksmanMove::Hold;
	}
	switch (Move)
	{
	case EMarksmanMove::Retreat:
		StartRetreat(Target);
		return;
	case EMarksmanMove::Approach:
	{
		// Sprint 06-D: beyond 35 m, or no line of fire (then to a firing position 20-33 m out) -> advance over the navmesh
		// (never idle); with a line of fire inside the band he settles, aims and fires.
		CancelAim();
		bHolding = false;
		// Without a line of fire: a firing position (searched every 3 s), else the squad's centre.
		FiringSearchCooldown -= DeltaTime;
		// At the firing position and still no line of fire (the target moved): look again at once.
		if (bHasFiringPosition && FVector::Dist2D(GetActorLocation(), FiringPosition) < 150.f)
		{
			FiringSearchCooldown = 0.f;
		}
		if (!Line.bHasLos && FiringSearchCooldown <= 0.f)
		{
			FiringSearchCooldown = 3.f;
			bHasFiringPosition = FindFiringPosition(Target, FiringPosition);
		}
		const FVector Goal = !Line.bHasLos && bHasFiringPosition ? FiringPosition : GetSquadCentroid(Target->GetActorLocation());
		// No progress towards the goal for 3 s (the navmesh path ended short, e.g. at a wall): flank round it instead.
		const float GoalDistance = FVector::Dist2D(GetActorLocation(), Goal);
		if (GoalDistance < ApproachBestDistance - 50.f)
		{
			ApproachBestDistance = GoalDistance;
			ApproachStallTime = 0.f;
		}
		else if ((ApproachStallTime += DeltaTime) > 3.f)
		{
			ApproachStallTime = 0.f;
			ApproachBestDistance = TNumericLimits<float>::Max();
			FiringSearchCooldown = 0.f; // look for another firing position next time
			// A reachable firing position: keep on to it (re-searched); none: flank round the obstacle.
			if (!bHasFiringPosition || Line.bHasLos)
			{
				StartFlank(Target);
				return;
			}
		}
		if (!MoveGoal.Equals(Goal, 200.f) || GetVelocity().IsNearlyZero())
		{
			MoveTo(Goal, false);
		}
		return;
	}
	case EMarksmanMove::BackOff:
		CancelAim();
		bHolding = false;
		LastKiteTime = GetWorld()->GetTimeSeconds();
		if (GetVelocity().IsNearlyZero() || FVector::Dist2D(GetActorLocation(), MoveGoal) < 150.f)
		{
			MoveTo(GetActorLocation() + (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D() * 400.f, false);
		}
		return;
	default:
		break;
	}

	// Hold: settle into the best stance, face the target, aim 2 s, fire.
	if (!bHolding)
	{
		bHolding = true;
		GetWorldTimerManager().ClearTimer(RiseTimerHandle);
		if (AIC)
		{
			AIC->StopMovement();
		}
		const bool bElevated = GetFeet().Z - (Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight()) >= 150.f;
		SetMarksmanStance(MarksmanAIRules::EvaluateBestStance(HasLowCoverTowards(Target), bElevated, false));
	}
	FaceYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaTime, 6.f);
	if (AttackTimer > 0.f)
	{
		return;
	}
	if (!bIsAimingAtTarget)
	{
		StartAim();
	}
	AimTimer += DeltaTime;
	// The beam follows from the scope re-traced at the new stance.
	const FMarksmanLine AimLine = TraceLine(Target);
	if (!AimLine.bHasLos)
	{
		CancelAim(); // the aim breaks on a lost line of fire
		return;
	}
	UpdateBeam(AimLine);
	if (AimTimer >= MarksmanConfig.AimDuration)
	{
		Fire(Target, AimLine, Distance);
		AttackTimer = MarksmanConfig.ShotCooldown;
		CancelAim();
	}
}
