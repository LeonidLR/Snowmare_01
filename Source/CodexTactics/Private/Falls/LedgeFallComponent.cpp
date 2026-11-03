// ULedgeFallComponent: running drops through the generated drop links, knockback falls over an edge and the landing's fall damage
// (game designer items 6 / 10 / 11a, 2026-11-03). UE-only system (no Godot reference).

#include "Falls/LedgeFallComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/FallRules.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderClimbComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace LedgeFallInternal
{
	static TAutoConsoleVariable<int32> CVarFallSlotClips(
		TEXT("Codex.Fall.SlotClips"),
		2,
		TEXT("The fall loop / land clips on the full-body slot from C++ (the fallback of the operative ABP's FallStates state machine, 2026-11-03): ")
		TEXT("0 never, 1 always, 2 (default) only when the body's ABP has no FallStates machine."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarFallAirGravity(
		TEXT("Codex.Fall.AirGravity"),
		2,
		TEXT("The ledge fall's stronger gravity (falls.json air.gravity_scale, 2026-11-03): 0 off, 1 drop links and knockback throws, ")
		TEXT("2 (default) also any other walk / push off a ledge."),
		ECVF_Default);

	/** An "other" fall (walked / pushed off) gets the air gravity once this far below its start. */
	constexpr float OtherFallGravityDropCm = 40.f;

	/** The operative ABP's fall state machine (Scripts/Editor/setup_operative_fall_animation.py). */
	const FName FallStateMachineName(TEXT("FallStates"));

	const TCHAR* const FallLoopPath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Loop.MM_Rifle_Jump_Fall_Loop");
	const TCHAR* const LandPath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Land.MM_Rifle_Jump_Fall_Land");

	const TCHAR* KindName(ELedgeFallKind Kind)
	{
		switch (Kind)
		{
		case ELedgeFallKind::DropLink: return TEXT("drop");
		case ELedgeFallKind::Knockback: return TEXT("knockback fall");
		case ELedgeFallKind::Other: return TEXT("fall");
		default: return TEXT("-");
		}
	}

	/** Every pawn ignored (the probes look at the level, never at a body standing near the edge). */
	void IgnorePawns(UWorld& World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<APawn> It(&World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
	}
}

ULedgeFallComponent::ULedgeFallComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	FallLoopClip = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(LedgeFallInternal::FallLoopPath));
	LandClip = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(LedgeFallInternal::LandPath));
}

void ULedgeFallComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = GetCharacter())
	{
		Character->MovementModeChangedDelegate.AddUniqueDynamic(this, &ULedgeFallComponent::HandleMovementModeChanged);
	}
}

void ULedgeFallComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = GetCharacter())
	{
		Character->MovementModeChangedDelegate.RemoveDynamic(this, &ULedgeFallComponent::HandleMovementModeChanged);
	}
	if (UDropLinkSubsystem* Links = GetLinks(); Links && ActiveLink != INDEX_NONE)
	{
		Links->ReleaseLease(ActiveLink, GetOwner(), 0.f);
	}
	SetAirGravity(false);
	Super::EndPlay(EndPlayReason);
}

ACharacter* ULedgeFallComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

float ULedgeFallComponent::GetHalfHeight() const
{
	const ACharacter* Character = GetCharacter();
	return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
}

float ULedgeFallComponent::FeetZ() const
{
	const AActor* Owner = GetOwner();
	return Owner ? static_cast<float>(Owner->GetActorLocation().Z) - GetHalfHeight() : 0.f;
}

float ULedgeFallComponent::WorldGravity() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Abs(World->GetGravityZ()) : 980.f;
}

float ULedgeFallComponent::Gravity() const
{
	return FallRules::AirGravity(WorldGravity(), FallRules::GetConfig());
}

void ULedgeFallComponent::SetAirGravity(bool bOn)
{
	const ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || bOn == bAirGravityOn || (bOn && LedgeFallInternal::CVarFallAirGravity.GetValueOnGameThread() <= 0))
	{
		return;
	}
	if (bOn)
	{
		SavedGravityScale = Movement->GravityScale;
		Movement->GravityScale = SavedGravityScale * FMath::Max(0.1f, FallRules::GetConfig().AirGravityScale);
	}
	else
	{
		Movement->GravityScale = SavedGravityScale;
	}
	bAirGravityOn = bOn;
}

UDropLinkSubsystem* ULedgeFallComponent::GetLinks() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UDropLinkSubsystem>() : nullptr;
}

float ULedgeFallComponent::GetTakeoffCap() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	// A jumping enemy (the hound, the cutter) pounces off the edge (enemy_drops.jumper_max_speed_cms).
	const float Cap = Operative ? Operative->GetSprintSpeedCap()
		: Enemy && Enemy->GetDropLinkUse() == FallRules::EEnemyDropUse::Jumper ? FMath::Max(Config.TakeoffMaxSpeedCms, Config.JumperTakeoffMaxSpeedCms)
		: Config.TakeoffMaxSpeedCms;
	return Cap * FMath::Max(0.f, TakeoffSpeedScale);
}

FallRules::FTakeoffPlan ULedgeFallComponent::PredictDrop(const FDropLink& Link) const
{
	const ACharacter* Character = GetCharacter();
	const float Along = Character ? FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Character->GetActorLocation() - Link.Start, Link.Dir))) : 0.f;
	const float Speed = State == ELedgeFallState::None && Character ? static_cast<float>(Character->GetVelocity().Size2D()) : ApproachSpeed;
	return FallRules::PlanTakeoff(Speed, GetTakeoffCap(), FMath::Max(0.f, Link.EdgeCm - Along), Link.NearCm - Along, Link.FarCm > 0.f ? Link.FarCm - Along : 0.f,
		Link.DropCm, Gravity(), FallRules::GetConfig());
}

FVector ULedgeFallComponent::GetSaveLocation() const
{
	const AActor* Owner = GetOwner();
	const UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links && (State == ELedgeFallState::RunUp || State == ELedgeFallState::Airborne) ? Links->GetLink(ActiveLink) : nullptr;
	if (Link)
	{
		return Link->End + FVector(0.f, 0.f, GetHalfHeight() + 2.f);
	}
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void ULedgeFallComponent::EnterState(ELedgeFallState NewState)
{
	State = NewState;
	StateTime = 0.f;
}

// --- Tick ----------------------------------------------------------------------------------------------------------------

void ULedgeFallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateTime += DeltaTime;
	UpdateAnimPhase(DeltaTime);
	if (bPendingFallKnockdown)
	{
		bPendingFallKnockdown = false;
		StartFallKnockdown(EKnockdownDirection::Back, EKnockdownCause::Fall);
	}
	if (PendingLink != INDEX_NONE)
	{
		StartDrop();
	}
	switch (State)
	{
	case ELedgeFallState::None:
		PollLinks(DeltaTime);
		TickRescueWatch(DeltaTime);
		if (bTrackingOtherFall)
		{
			PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
			// Walked / pushed off a ledge: the same faster fall once it is a real fall (40 cm down) - never on a stair step's micro-fall
			// (that broke a stair walk: LevelGenRampSmoke, 2026-11-03).
			if (LedgeFallInternal::CVarFallAirGravity.GetValueOnGameThread() >= 2 && PeakFeetZ - FeetZ() > LedgeFallInternal::OtherFallGravityDropCm)
			{
				SetAirGravity(true);
			}
		}
		break;
	case ELedgeFallState::Waiting: TickWaiting(DeltaTime); break;
	case ELedgeFallState::RunUp: TickRunUp(DeltaTime); break;
	case ELedgeFallState::Airborne: TickAirborne(DeltaTime); break;
	case ELedgeFallState::Landed: TickLanded(DeltaTime); break;
	default: break;
	}
}

void ULedgeFallComponent::PollLinks(float DeltaTime)
{
	// Backup of the smart link's callback: a path that runs through a drop link and is at its start point.
	PollTimer -= DeltaTime;
	if (PollTimer > 0.f)
	{
		return;
	}
	PollTimer = 0.1f;
	const ACharacter* Character = GetCharacter();
	const AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const UDropLinkSubsystem* Links = GetLinks();
	if (!Following || !Links || Links->GetLinks().IsEmpty() || Following->GetStatus() != EPathFollowingStatus::Moving || !Following->GetPath().IsValid()
		|| !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	TArray<int32> OnPath;
	Links->FindLinksOnPath(*Following->GetPath(), OnPath);
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	for (const int32 Index : OnPath)
	{
		const FDropLink* Link = Links->GetLink(Index);
		if (Link && FVector::Dist2D(Feet, Link->Start) <= 45.f && FMath::Abs(Feet.Z - Link->Start.Z) <= 60.f)
		{
			NotifyDropLinkReached(Index);
			return;
		}
	}
}

bool ULedgeFallComponent::NotifyDropLinkReached(int32 Index)
{
	if (IsBusy())
	{
		return true;
	}
	const UDropLinkSubsystem* Links = GetLinks();
	if (!Links || !Links->GetLink(Index))
	{
		return false;
	}
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	ApproachSpeed = static_cast<float>(Character->GetVelocity().Size2D());
	// The path's goal: he walks on there from the landing (like the ladder, LADDERS.md 7.1).
	const AAIController* Controller = Cast<AAIController>(Character->GetController());
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	bHasPendingDestination = false;
	if (Operative && Operative->IsOwnMoveSteered())
	{
		PendingDestination = Operative->GetLastMoveDestination(); // a bypass waypoint's path: the ORDER's destination after the drop
		bHasPendingDestination = true;
	}
	else if (Following && Following->GetPath().IsValid() && Following->GetPath()->IsValid())
	{
		PendingDestination = Following->GetPath()->GetDestinationLocation();
		bHasPendingDestination = true;
	}
	else if (Operative)
	{
		PendingDestination = Operative->GetLastMoveDestination();
		bHasPendingDestination = true;
	}
	bOwnOrderAtEntry = Operative && Operative->HasOwnMoveOrder();
	PendingLink = Index; // the smart link calls back from inside the crowd update: the drop starts on the next tick
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s reached drop link %d at %.0f cm/s"), *GetNameSafe(GetOwner()), Index, ApproachSpeed);
	return true;
}

void ULedgeFallComponent::HandleNewOrder(const FVector& Destination)
{
	PendingDestination = Destination;
	bHasPendingDestination = true;
}

void ULedgeFallComponent::CancelWait()
{
	if (State != ELedgeFallState::Waiting)
	{
		return;
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	bHasPendingDestination = false;
	EnterState(ELedgeFallState::None);
}

void ULedgeFallComponent::StopOwnerMove()
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	const FVector Keep = PendingDestination;
	const bool bKeep = bHasPendingDestination;
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
	{
		Operative->StopOperative();
	}
	else if (AController* Controller = Character->GetController())
	{
		Controller->StopMovement();
	}
	PendingDestination = Keep;
	bHasPendingDestination = bKeep;
}

void ULedgeFallComponent::Refuse(const FString& Line)
{
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s: drop refused (%s)"), *GetNameSafe(GetOwner()), *Line);
	StopOwnerMove();
	bHasPendingDestination = false;
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner()))
	{
		UFloatingTextSubsystem::SpawnAboveOperative(Operative, Line, FLinearColor(1.f, 0.6f, 0.2f));
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	EnterState(ELedgeFallState::None);
}

void ULedgeFallComponent::StartDrop()
{
	const int32 Index = PendingLink;
	PendingLink = INDEX_NONE;
	ACharacter* Character = GetCharacter();
	UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links ? Links->GetLink(Index) : nullptr;
	if (!Character || !Link)
	{
		return;
	}
	const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	ActiveLink = Index;
	Kind = ELedgeFallKind::DropLink;
	if ((Health && !Health->IsAlive()) || (Operative && Operative->IsKnockedDown()) || VehicleQueries::IsMounted(Character))
	{
		Refuse(TEXT("CAN'T DROP"));
		return;
	}
	if (TurnBased && TurnBased->IsActive())
	{
		Refuse(TEXT("NO DROPS IN TURN-BASED COMBAT"));
		return;
	}
	const bool bMustStand = Operative && (Operative->GetStance() != EOperativeStance::Standing || Operative->GetMoveHoldSeconds() > 0.f);
	if (bMustStand || !Links->TryLease(Index, Character))
	{
		// He waits at the link: another man's drop lands there now (one at a time), or he stands up first.
		if (State != ELedgeFallState::Waiting)
		{
			StopOwnerMove();
			EnterState(ELedgeFallState::Waiting);
			UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s waits at drop link %d (%s)"), *GetNameSafe(Character), Index, bMustStand ? TEXT("stands up first") : TEXT("the landing is taken"));
		}
		if (bMustStand && Operative && Operative->GetStance() != EOperativeStance::Standing)
		{
			Operative->SetStance(EOperativeStance::Standing);
		}
		RetryTimer = 0.2f;
		return;
	}

	// Take the body over: the path ends here (it resumes at the landing); the run-up off the edge.
	StopOwnerMove();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bMovementSaved)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		bSavedWalkOffLedges = Movement->bCanWalkOffLedges;
		bMovementSaved = true;
	}
	Movement->bCanWalkOffLedges = true;
	StartFeetZ = FeetZ();
	PeakFeetZ = StartFeetZ;
	FallStartFeet = FVector(Character->GetActorLocation().X, Character->GetActorLocation().Y, StartFeetZ);
	// The plan from where he stands (he is at the link start, give or take the crowd's arrival radius).
	const float Along = static_cast<float>(FVector::DotProduct(Character->GetActorLocation() - Link->Start, Link->Dir));
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const float Edge = FMath::Max(0.f, Link->EdgeCm - Along);
	const FallRules::FTakeoffPlan Plan = FallRules::PlanTakeoff(ApproachSpeed, GetTakeoffCap(), Edge, Link->NearCm - Along,
		Link->FarCm > 0.f ? Link->FarCm - Along : 0.f, Link->DropCm, Gravity(), Config);
	PlannedSpeed = FMath::Max(Plan.SpeedCms, 50.f);
	bPlannedReach = Plan.bReaches;
	// Aim at the landing (the crowd hands the link over up to ~80 cm before its start, maybe a little to the side of it).
	const FVector ToLanding = (Link->End - Character->GetActorLocation()).GetSafeNormal2D();
	RunDir = FVector::DotProduct(ToLanding, Link->Dir) > 0.7f ? ToLanding : Link->Dir;
	Movement->MaxWalkSpeed = PlannedSpeed;
	Movement->Velocity = RunDir * PlannedSpeed;
	Character->SetActorRotation(FRotator(0.f, RunDir.Rotation().Yaw, 0.f));
	// The run-up's time limit: the way to the edge and a body width past it (the capsule rolls over the edge) at the planned speed, at least the tunable.
	RunUpLimitSeconds = FMath::Max(Config.RunUpTimeoutSeconds, (Edge + 2.f * Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + 50.f) / PlannedSpeed + 0.5f);
	++DropCount;
	LastAirSpeed = 0.f;
	bPlayedFallClip = false;
	bLastFallAnimFalling = false;
	EnterState(ELedgeFallState::RunUp);
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s drops off link %d (%s -> %s, out %s) from %s: %.0f cm down, edge %.0f / lower surface %.0f..%.0f cm out; approach %.0f cm/s, cap %.0f -> run-up %.0f cm/s, lands at %.0f cm (%s)"),
		*GetNameSafe(Character), Index, *Link->Start.ToCompactString(), *Link->End.ToCompactString(), *Link->Dir.ToCompactString(), *Character->GetActorLocation().ToCompactString(),
		Link->DropCm, Edge, Link->NearCm - Along, Link->FarCm > 0.f ? Link->FarCm - Along : -1.f, ApproachSpeed, GetTakeoffCap(), PlannedSpeed,
		Plan.LandingCm, Plan.bReaches ? TEXT("reaches") : Plan.bTooSlow ? TEXT("TOO SLOW: falls short") : TEXT("overshoots"));
}

void ULedgeFallComponent::TickWaiting(float DeltaTime)
{
	RetryTimer -= DeltaTime;
	if (RetryTimer > 0.f)
	{
		return;
	}
	RetryTimer = 0.2f;
	PendingLink = ActiveLink; // StartDrop re-checks the stance and the lease
	StartDrop();
}

void ULedgeFallComponent::TickRunUp(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (Movement->IsFalling())
	{
		BeginAirborne();
		return;
	}
	if (StateTime > RunUpLimitSeconds)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] %s: the run-up off link %d never left the edge (%.1f s, at %s) - the link is switched off, the path goes on"),
			*GetNameSafe(Character), ActiveLink, StateTime, *Character->GetActorLocation().ToCompactString());
		if (UDropLinkSubsystem* Links = GetLinks())
		{
			Links->DisableLink(ActiveLink, TEXT("the run-up never left the edge"));
		}
		Finish(true);
		return;
	}
	// Keep running straight off the edge at the planned speed (no path following now).
	Movement->MaxWalkSpeed = PlannedSpeed;
	const FVector Run = RunDir * PlannedSpeed;
	Movement->Velocity = FVector(Run.X, Run.Y, Movement->Velocity.Z);
	Movement->RequestDirectMove(Run, false);
	Character->SetActorRotation(FRotator(0.f, RunDir.Rotation().Yaw, 0.f));
}

void ULedgeFallComponent::BeginAirborne()
{
	SetAirGravity(true); // the falls look 1.5x faster (air.gravity_scale)
	PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
	EnterState(ELedgeFallState::Airborne);
	SetRootMotionIgnored(true);
	PlayFallLoop();
}

void ULedgeFallComponent::TickAirborne(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
	LastAirSpeed = FMath::Max(LastAirSpeed, static_cast<float>(Movement->Velocity.Size2D()));
	if (!Movement->IsFalling() || StateTime > FallRules::GetConfig().AirTimeoutSeconds)
	{
		if (Movement->IsFalling())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] %s: still falling after %.1f s - landed by force"), *GetNameSafe(Character), StateTime);
		}
		Land();
	}
}

void ULedgeFallComponent::Land()
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const float LandFeet = FeetZ();
	const float Height = FMath::Max(0.f, PeakFeetZ - LandFeet);
	LastFallKind = Kind;
	UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links ? Links->GetLink(ActiveLink) : nullptr;
	bLastLandedOnTarget = Link && FMath::Abs(LandFeet - Link->End.Z) <= 60.f;
	StopFallLoop(0.1f);
	// The landing takes most of the speed (no sliding on across a small roof); his own speeds again.
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->Velocity = FVector(Movement->Velocity.X * 0.3f, Movement->Velocity.Y * 0.3f, Movement->Velocity.Z);
	RestoreMovement();
	const FallRules::FFallVerdict Verdict = ApplyLanding(*Character, Height);
	const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	const bool bAlive = !Health || Health->IsAlive();
	const bool bKnockdown = bKnockdownOnLanding || (Verdict.bKnockdown && bAlive);
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s landed after a %.0f cm %s: %.1f fall damage%s%s (run-up %.0f cm/s, air speed %.0f cm/s%s)"), *GetNameSafe(Character),
		LastFallHeight, LedgeFallInternal::KindName(Kind), FMath::Min(LastFallDamage, 99999.f),
		Verdict.Outcome == FallRules::EFallOutcome::Lethal ? TEXT(", LETHAL") : Verdict.Outcome == FallRules::EFallOutcome::HeavyWound ? TEXT(", a heavy wound") : TEXT(""),
		bKnockdown ? TEXT(", knocked down") : TEXT(""), PlannedSpeed, LastAirSpeed,
		Link ? (bLastLandedOnTarget ? TEXT(", on the link's lower surface") : TEXT(", MISSED the link's lower surface")) : TEXT(""));
	if (Links && ActiveLink != INDEX_NONE)
	{
		Links->ReleaseLease(ActiveLink, Character, Config.LeaseHoldAfterLandingSeconds);
	}
	ArmRescueWatch(); // the soft-lock safety net: off the navmesh / in a dead end after the landing -> brought back
	if (bKnockdown)
	{
		// The knockdown that threw him plays now (by the normal rules: the immunity, the death while down); a heavy wound (9-12 m) floors him the
		// same way (user decision 2026-11-03): he lies for the knockdown's downed time, then gets up.
		const EKnockdownDirection Side = bKnockdownOnLanding ? KnockdownSide : EKnockdownDirection::Back;
		const EKnockdownCause Cause = bKnockdownOnLanding ? KnockdownCause : EKnockdownCause::Fall;
		bKnockdownOnLanding = false;
		Finish(false);
		StartFallKnockdown(Side, Cause);
		return;
	}
	PlayLandClip();
	EnterState(ELedgeFallState::Landed);
}

FallRules::FFallVerdict ULedgeFallComponent::ApplyLanding(ACharacter& Character, float Height)
{
	UHealthComponent* Health = Character.FindComponentByClass<UHealthComponent>();
	const FallRules::FFallVerdict Verdict = FallRules::JudgeFallForBody(Height, Health ? Health->GetMaxHealth() : 0.f, FallRules::GetConfig());
	LastFallHeight = Height;
	LastFallDamage = Verdict.Damage;
	LastFallOutcome = Verdict.Outcome;
	LastLandingLocation = Character.GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	++LandingCount;
	HeavyWoundCount += Verdict.Outcome == FallRules::EFallOutcome::HeavyWound ? 1 : 0;
	LethalFallCount += Verdict.Outcome == FallRules::EFallOutcome::Lethal ? 1 : 0;
	if (Health && Health->IsAlive() && Verdict.Damage > 0.f)
	{
		Health->ApplyDirectHealthLoss(Verdict.Damage, TEXT("Fall"));
		if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(&Character))
		{
			const FString Text = Verdict.Outcome == FallRules::EFallOutcome::Lethal ? FString(TEXT("FATAL FALL"))
				: FString::Printf(TEXT("%s -%d"), Verdict.Outcome == FallRules::EFallOutcome::HeavyWound ? TEXT("HEAVY FALL") : TEXT("FALL"),
					FMath::RoundToInt(FMath::Min(Verdict.Damage, 999.f)));
			UFloatingTextSubsystem::SpawnAboveOperative(Operative, Text, FLinearColor(1.f, 0.45f, 0.15f));
		}
	}
	return Verdict;
}

void ULedgeFallComponent::StartFallKnockdown(EKnockdownDirection Side, EKnockdownCause Cause)
{
	ACharacter* Character = GetCharacter();
	const UHealthComponent* Health = Character ? Character->FindComponentByClass<UHealthComponent>() : nullptr;
	UKnockdownComponent* Knockdown = Character ? Character->FindComponentByClass<UKnockdownComponent>() : nullptr;
	if (!Knockdown || (Health && !Health->IsAlive()))
	{
		return;
	}
	TGuardValue<bool> NoSecondThrow(bLandingKnockdown, true); // the knockdown at the landing spot never throws him over another edge
	if (!Knockdown->ForceKnockDown(Side, Cause))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s: no knockdown on landing (this body cannot be knocked down now)"), *GetNameSafe(Character));
	}
}

void ULedgeFallComponent::TickLanded(float DeltaTime)
{
	if (StateTime >= FallRules::GetConfig().LandSettleSeconds)
	{
		Finish(true);
	}
}

void ULedgeFallComponent::RestoreMovement()
{
	SetAirGravity(false);
	ACharacter* Character = GetCharacter();
	if (Character && bMovementSaved)
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->bCanWalkOffLedges = bSavedWalkOffLedges;
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
		{
			Operative->ApplyMovementParams();
		}
	}
	bMovementSaved = false;
}

void ULedgeFallComponent::Finish(bool bResume)
{
	RestoreMovement();
	SetRootMotionIgnored(false);
	if (UDropLinkSubsystem* Links = GetLinks(); Links && ActiveLink != INDEX_NONE && State != ELedgeFallState::Landed)
	{
		Links->ReleaseLease(ActiveLink, GetOwner(), 0.f);
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	EnterState(ELedgeFallState::None);
	if (bResume)
	{
		ResumePath();
	}
	else
	{
		bHasPendingDestination = false;
	}
}

void ULedgeFallComponent::ResumePath()
{
	const FVector Destination = PendingDestination;
	const bool bHadDestination = bHasPendingDestination;
	bHasPendingDestination = false;
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (TurnBased && TurnBased->IsActive())
	{
		return; // the fight started while he dropped: he stays where he landed
	}
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		Enemy->ResumeMoveAfterLadder(bHadDestination ? Destination : Enemy->GetActorLocation());
		return;
	}
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (bHadDestination && Operative && FVector::Dist2D(Destination, Operative->GetActorLocation()) > 60.f)
	{
		Operative->ResumeMoveAfterLadder(Destination, bOwnOrderAtEntry || Operative->HasOwnMoveOrder());
	}
}

// --- Knockback falls -----------------------------------------------------------------------------------------------------

bool ULedgeFallComponent::TryKnockbackFall(EKnockdownDirection Side, EKnockdownCause Cause, bool bHasSource, const FVector& SourceLocation)
{
	ACharacter* Character = GetCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World || IsBusy() || bLandingKnockdown || !Character->GetCharacterMovement()->IsMovingOnGround() || VehicleQueries::IsMounted(Character))
	{
		return false;
	}
	if (const ULadderClimbComponent* Ladder = Character->FindComponentByClass<ULadderClimbComponent>(); Ladder && Ladder->IsBusy())
	{
		return false;
	}
	if (const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>(); TurnBased && TurnBased->IsActive())
	{
		return false; // the turn-based grid keeps everybody on his level
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const FVector Dir = FallRules::KnockbackDirection(Side, Character->GetActorForwardVector(), bHasSource, Character->GetActorLocation(), SourceLocation);
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Feet = FeetZ();
	const FVector Base(Character->GetActorLocation().X, Character->GetActorLocation().Y, Feet);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LedgeKnockback), false, Character);
	LedgeFallInternal::IgnorePawns(*World, Params);
	// The edge: the first probe along the throw with no floor at his feet' height (+-step tolerance) under it.
	float EdgeCm = -1.f;
	for (float D = 10.f; D <= Config.KnockbackReachCm + Radius; D += 10.f)
	{
		const FVector At = Base + Dir * D;
		FHitResult Hit;
		const bool bFloor = World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 30.f), At - FVector(0.f, 0.f, Config.StepToleranceCm), ECC_Pawn, Params);
		if (!bFloor)
		{
			EdgeCm = D;
			break;
		}
	}
	if (EdgeCm < 0.f)
	{
		return false;
	}
	// How deep beyond it.
	FHitResult Below;
	const FVector Beyond = Base + Dir * (EdgeCm + Radius);
	const float DropCm = World->LineTraceSingleByChannel(Below, Beyond + FVector(0.f, 0.f, 20.f), Beyond - FVector(0.f, 0.f, 5000.f), ECC_Pawn, Params)
		? Feet - static_cast<float>(Below.ImpactPoint.Z) : 5000.f;
	// A parapet / railing up to his waist between him and the edge stops the throw.
	bool bObstacle = false;
	for (const float Height : { 30.f, FMath::Max(35.f, Config.ParapetHeightCm - 10.f) })
	{
		FHitResult Wall;
		const FVector From = Base + FVector(0.f, 0.f, Height);
		bObstacle |= World->SweepSingleByChannel(Wall, From, From + Dir * (EdgeCm + 15.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(12.f), Params);
	}
	// The world's gravity here: the throw's speeds go up by sqrt(air.gravity_scale) below, so its arc (and reach) is the unscaled one.
	if (!FallRules::KnockbackGoesOver(EdgeCm, bObstacle, DropCm, Radius, WorldGravity(), Config))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Fall] %s: no knockback fall (edge %.0f cm, drop %.0f cm, parapet %d)"), *GetNameSafe(Character), EdgeCm, DropCm, bObstacle ? 1 : 0);
		return false;
	}
	StopOwnerMove();
	bHasPendingDestination = false; // the knockdown buffers his orders
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bMovementSaved)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		bSavedWalkOffLedges = Movement->bCanWalkOffLedges;
		bMovementSaved = true;
	}
	Movement->bCanWalkOffLedges = true;
	Kind = ELedgeFallKind::Knockback;
	ActiveLink = INDEX_NONE;
	bKnockdownOnLanding = true;
	KnockdownSide = Side == EKnockdownDirection::None ? EKnockdownDirection::Back : Side;
	KnockdownCause = Cause;
	StartFeetZ = Feet;
	PeakFeetZ = Feet;
	FallStartFeet = Base;
	PlannedSpeed = Config.KnockbackSpeedCms * FallRules::KnockbackLaunchScale(Config);
	bPlannedReach = false;
	LastAirSpeed = 0.f;
	bPlayedFallClip = false;
	bLastFallAnimFalling = false;
	++KnockbackFallCount;
	BeginAirborne(); // the state first: the movement mode change below is this component's own fall
	Movement->SetMovementMode(MOVE_Falling);
	const float Launch = FallRules::KnockbackLaunchScale(Config);
	Movement->Velocity = (Dir * Config.KnockbackSpeedCms + FVector(0.f, 0.f, Config.KnockbackUpCms)) * Launch;
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s is thrown over the edge %.0f cm away (%.0f cm drop, cause %d)"), *GetNameSafe(Character), EdgeCm, DropCm,
		static_cast<int32>(Cause));
	return true;
}

// --- Other falls ---------------------------------------------------------------------------------------------------------

void ULedgeFallComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	if (Character && !Character->GetCharacterMovement()->IsFalling() && State != ELedgeFallState::Airborne)
	{
		SetAirGravity(false); // a fall of any kind is over
	}
	if (!Character || State != ELedgeFallState::None)
	{
		bTrackingOtherFall = false;
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bWasGround = PrevMovementMode == MOVE_Walking || PrevMovementMode == MOVE_NavWalking;
	const ULadderClimbComponent* Ladder = Character->FindComponentByClass<ULadderClimbComponent>();
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Character);
	const bool bIgnored = (Ladder && Ladder->IsOnLadder()) || (Operative && Operative->IsVaulting()) || (Enemy && Enemy->IsJumpAttacking())
		|| VehicleQueries::IsMounted(Character);
	if (Movement->IsFalling() && bWasGround && !bIgnored)
	{
		bTrackingOtherFall = true;
		bLastFallAnimFalling = false;
		StartFeetZ = FeetZ();
		PeakFeetZ = StartFeetZ;
		FallStartFeet = FVector(Character->GetActorLocation().X, Character->GetActorLocation().Y, StartFeetZ);
		return;
	}
	if (PrevMovementMode == MOVE_Falling && Movement->IsMovingOnGround() && bTrackingOtherFall)
	{
		bTrackingOtherFall = false;
		const float Height = FMath::Max(0.f, PeakFeetZ - FeetZ());
		const FallRules::FFallConfig& Config = FallRules::GetConfig();
		if (bIgnored || Height < Config.MinTrackedFallCm)
		{
			return;
		}
		LastFallKind = ELedgeFallKind::Other;
		bLastLandedOnTarget = false;
		const FallRules::FFallVerdict Verdict = ApplyLanding(*Character, Height);
		const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
		UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s landed after a %.0f cm fall: %.1f fall damage%s"), *GetNameSafe(Character), Height, FMath::Min(LastFallDamage, 99999.f),
			Verdict.Outcome == FallRules::EFallOutcome::Lethal ? TEXT(", LETHAL") : Verdict.Outcome == FallRules::EFallOutcome::HeavyWound ? TEXT(", a heavy wound: knocked down") : TEXT(""));
		ArmRescueWatch(); // the soft-lock safety net (a walk-off through a hole link left the whole squad stuck, 2026-11-03)
		// A heavy wound: the knockdown starts on the next tick (not inside the movement mode change).
		bPendingFallKnockdown = Verdict.bKnockdown && (!Health || Health->IsAlive());
	}
}

void ULedgeFallComponent::UpdateAnimPhase(float DeltaTime)
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	// The ladder, a vault, a pounce and a vehicle have their own animation: no fall state then.
	const ULadderClimbComponent* Ladder = Character->FindComponentByClass<ULadderClimbComponent>();
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Character);
	const bool bOwnAnimation = (Ladder && Ladder->IsBusy()) || (Operative && Operative->IsVaulting()) || (Enemy && Enemy->IsJumpAttacking())
		|| VehicleQueries::IsMounted(Character);
	const bool bDropAirborne = State == ELedgeFallState::Airborne && !bOwnAnimation;
	FallingSeconds = Movement->IsFalling() && !bOwnAnimation ? FallingSeconds + DeltaTime : 0.f;
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const bool bFallingNow = bDropAirborne || (FallingSeconds > 0.f && FallingSeconds >= Config.AnimFallMinSeconds);
	if (AnimPhase == FallRules::EFallAnimPhase::Falling && !bFallingNow)
	{
		SecondsSinceFallEnded = 0.f;
	}
	else if (SecondsSinceFallEnded >= 0.f)
	{
		SecondsSinceFallEnded = bFallingNow || SecondsSinceFallEnded > Config.AnimLandSeconds + 1.f ? -1.f : SecondsSinceFallEnded + DeltaTime;
	}
	AnimPhase = FallRules::FallAnimPhase(bDropAirborne, FallingSeconds, SecondsSinceFallEnded, Config);
	bLastFallAnimFalling |= AnimPhase == FallRules::EFallAnimPhase::Falling;
}

// --- Clips ---------------------------------------------------------------------------------------------------------------

UAnimSequenceBase* ULedgeFallComponent::LoadClip(const TSoftObjectPtr<UAnimSequenceBase>& Clip) const
{
	return Clip.IsNull() ? nullptr : Clip.LoadSynchronous();
}

void ULedgeFallComponent::SetRootMotionIgnored(bool bIgnored)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		bRootMotionSaved = false;
		return;
	}
	if (bIgnored && !bRootMotionSaved)
	{
		SavedRootMotionMode = static_cast<uint8>(Anim->RootMotionMode.GetValue());
		bRootMotionSaved = true;
		Anim->RootMotionMode = ERootMotionMode::IgnoreRootMotion; // the fall keeps its ballistic speed (a clip's root motion would replace it)
	}
	else if (!bIgnored && bRootMotionSaved)
	{
		Anim->RootMotionMode = static_cast<ERootMotionMode::Type>(SavedRootMotionMode);
		bRootMotionSaved = false;
	}
}

bool ULedgeFallComponent::UseSlotClips() const
{
	const int32 Mode = LedgeFallInternal::CVarFallSlotClips.GetValueOnGameThread();
	if (Mode <= 0 || (FallLoopClip.IsNull() && LandClip.IsNull()))
	{
		return false;
	}
	if (Mode == 1)
	{
		return true;
	}
	const ACharacter* Character = GetCharacter();
	const UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	return Anim && Anim->GetStateMachineIndex(LedgeFallInternal::FallStateMachineName) == INDEX_NONE;
}

void ULedgeFallComponent::PlayFallLoop()
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	UAnimSequenceBase* Clip = UseSlotClips() ? LoadClip(FallLoopClip) : nullptr;
	if (!Anim || !Clip)
	{
		return;
	}
	FallMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, 0.15f, 0.12f, 1.f, 1000);
	bPlayedFallClip = FallMontage.IsValid();
}

void ULedgeFallComponent::StopFallLoop(float BlendOut)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && FallMontage.IsValid())
	{
		Anim->Montage_Stop(BlendOut, FallMontage.Get());
	}
	FallMontage.Reset();
}

void ULedgeFallComponent::PlayLandClip()
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	UAnimSequenceBase* Clip = UseSlotClips() ? LoadClip(LandClip) : nullptr;
	if (Anim && Clip)
	{
		LandMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, 0.05f, 0.2f, 1.f, 1);
	}
}
