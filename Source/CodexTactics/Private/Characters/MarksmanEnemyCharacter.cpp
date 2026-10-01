#include "Characters/MarksmanEnemyCharacter.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/FloatingTextSubsystem.h"
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

void AMarksmanEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	SpawnLocation = GetActorLocation();
	// Waypoints are authored relative to the actor.
	for (FVector& Point : PatrolRoute)
	{
		Point = GetActorTransform().TransformPosition(Point);
	}
	AIState = PatrolRoute.IsEmpty() ? EMarksmanAIState::Engage : EMarksmanAIState::Patrol;
	if (HealthComponent)
	{
		HealthComponent->OnDamaged.AddDynamic(this, &AMarksmanEnemyCharacter::HandleMarksmanDamaged);
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

void AMarksmanEnemyCharacter::HandleMarksmanDamaged(const FDamageSpec& Spec, float FinalDamage)
{
	if (bIsDying || AIState == EMarksmanAIState::Ambushed || AIState == EMarksmanAIState::Retreat)
	{
		return;
	}
	float Distance = 0.f;
	FindClosestOperative(Distance);
	// Shot from afar while patrolling / aiming: down at once (smaller profile), alert the others, then relocate.
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
	// Back to the middle of the band.
	const float Wanted = (MarksmanConfig.PreferredMinRange + MarksmanConfig.PreferredMaxRange) * 0.5f;
	MoveTo(Target->GetActorLocation() + Away * Wanted, true);
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
		// Done on arrival, after 8 s, or (retreating) once back in the band.
		if (FVector::Dist2D(GetActorLocation(), MoveGoal) < 120.f || StateTimer > 8.f
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
	if (Target && Distance <= MarksmanConfig.DetectionRange && TraceLine(Target).bHasLos)
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
	EMarksmanMove Move = MarksmanAIRules::ChooseMove(MarksmanConfig, Distance, Line.bHasLos);
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
		CancelAim();
		bHolding = false;
		if (!MoveGoal.Equals(Target->GetActorLocation(), 200.f) || GetVelocity().IsNearlyZero())
		{
			MoveTo(Target->GetActorLocation(), false);
		}
		return;
	case EMarksmanMove::BackOff:
		CancelAim();
		bHolding = false;
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
