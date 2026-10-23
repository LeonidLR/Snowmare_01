// ULadderClimbComponent: the climb state machine (docs/design/LADDERS.md section 5). UE-only system (no Godot reference).

#include "Ladders/LadderClimbComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderSubsystem.h"
#include "NavLinkCustomComponent.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace LadderClimbInternal
{
	const TCHAR* const UpLoopPath = TEXT("/Game/Characters/Operatives/Anims/Ladder/AS_Ladder_UpLoop.AS_Ladder_UpLoop");
	const TCHAR* const ClimbOutPath = TEXT("/Game/Characters/Operatives/Anims/Ladder/AS_Ladder_ClimbOutTop.AS_Ladder_ClimbOutTop");
	const TCHAR* const IntoFromTopPath = TEXT("/Game/Characters/Operatives/Anims/Ladder/AS_Ladder_IntoFromTop.AS_Ladder_IntoFromTop");

	FString StateName(ELadderClimbState State)
	{
		const UEnum* Enum = StaticEnum<ELadderClimbState>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(State)) : FString(TEXT("?"));
	}

	const TCHAR* RefusalText(ELadderRefusal Refusal)
	{
		switch (Refusal)
		{
		case ELadderRefusal::TurnBased: return TEXT("NO LADDERS IN TURN-BASED COMBAT");
		case ELadderRefusal::Carrying: return TEXT("CAN'T CLIMB WHILE CARRYING");
		case ELadderRefusal::Mounted: return TEXT("CAN'T CLIMB FROM A VEHICLE");
		case ELadderRefusal::KnockedDown: return TEXT("KNOCKED DOWN");
		case ELadderRefusal::NotClimbable: return TEXT("THIS LADDER CAN'T BE CLIMBED");
		case ELadderRefusal::NoClips: return TEXT("CAN'T CLIMB");
		default: return TEXT("CAN'T CLIMB");
		}
	}
}

ULadderClimbComponent::ULadderClimbComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	UpLoopClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(LadderClimbInternal::UpLoopPath));
	ClimbOutClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(LadderClimbInternal::ClimbOutPath));
	IntoFromTopClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(LadderClimbInternal::IntoFromTopPath));
}

void ULadderClimbComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		Health->OnDied.AddUniqueDynamic(this, &ULadderClimbComponent::HandleOwnerDied);
	}
	// The mesh evaluates after this component positioned the montage and moved the actor (no one-frame lag on the ladder).
	if (ACharacter* Character = GetCharacter(); Character && Character->GetMesh())
	{
		Character->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
}

void ULadderClimbComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State != ELadderClimbState::None)
	{
		AbortClimb(ELadderAbortReason::EndPlay);
	}
	Super::EndPlay(EndPlayReason);
}

ACharacter* ULadderClimbComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

UAnimInstance* ULadderClimbComponent::GetAnimInstance() const
{
	const ACharacter* Character = GetCharacter();
	return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
}

float ULadderClimbComponent::GetHalfHeight() const
{
	const ACharacter* Character = GetCharacter();
	return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
}

// --- Clips -----------------------------------------------------------------------------------------------------------

FVector ULadderClimbComponent::FRootTrack::Sample(float Time) const
{
	if (Samples.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	const float Index = FMath::Clamp(Time * SampleRate, 0.f, static_cast<float>(Samples.Num() - 1));
	const int32 Low = FMath::FloorToInt(Index);
	const int32 High = FMath::Min(Low + 1, Samples.Num() - 1);
	return FMath::Lerp(Samples[Low], Samples[High], Index - Low);
}

ULadderClimbComponent::FRootTrack ULadderClimbComponent::ExtractTrack(UAnimSequence* Clip) const
{
	FRootTrack Track;
	if (!Clip)
	{
		return Track;
	}
	Track.Length = Clip->GetPlayLength();
	// Mesh space -> the climber's frame (the skeletal mesh is turned on the capsule, e.g. -90 yaw for +Y-forward rigs).
	const ACharacter* Character = GetCharacter();
	const FQuat MeshToActor = Character && Character->GetMesh() ? Character->GetMesh()->GetRelativeRotation().Quaternion() : FQuat::Identity;
	const FVector Origin = Clip->ExtractRootTrackTransform(FAnimExtractContext(0.0), nullptr).GetLocation();
	const int32 Count = FMath::Max(2, FMath::CeilToInt(Track.Length * Track.SampleRate) + 1);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Time = FMath::Min(static_cast<double>(Index) / Track.SampleRate, static_cast<double>(Track.Length));
		const FVector Root = Clip->ExtractRootTrackTransform(FAnimExtractContext(Time), nullptr).GetLocation();
		Track.Samples.Add(MeshToActor.RotateVector(Root - Origin));
	}
	return Track;
}

bool ULadderClimbComponent::LoadClips()
{
	if (bClipsLoaded)
	{
		return LoadedUpLoop && LoadedClimbOut && LoadedIntoFromTop;
	}
	bClipsLoaded = true;
	LoadedUpLoop = UpLoopClip.LoadSynchronous();
	LoadedClimbOut = ClimbOutClip.LoadSynchronous();
	LoadedIntoFromTop = IntoFromTopClip.LoadSynchronous();
	if (!LoadedUpLoop || !LoadedClimbOut || !LoadedIntoFromTop)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s: ladder clips missing (up %d, out %d, into %d) - cannot climb"), *GetNameSafe(GetOwner()),
			LoadedUpLoop ? 1 : 0, LoadedClimbOut ? 1 : 0, LoadedIntoFromTop ? 1 : 0);
		return false;
	}
	const FRootTrack Loop = ExtractTrack(LoadedUpLoop);
	LoopLength = Loop.Length;
	const float Rise = Loop.End().Z;
	LoopRise.Reset();
	for (const FVector& Sample : Loop.Samples)
	{
		LoopRise.Add(Rise > 1.f ? Sample.Z / Rise : 0.f);
	}
	ClimbOutTrack = ExtractTrack(LoadedClimbOut);
	IntoFromTopTrack = ExtractTrack(LoadedIntoFromTop);
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] clips: up loop %.3f s rise %.1f cm; climb out %.3f s %s; into from top %.3f s %s (climber frame: X to the wall)"),
		LoopLength, Rise, ClimbOutTrack.Length, *ClimbOutTrack.End().ToCompactString(), IntoFromTopTrack.Length, *IntoFromTopTrack.End().ToCompactString());
	return LoopLength > 0.1f && Rise > 1.f;
}

float ULadderClimbComponent::LoopFraction(float Time) const
{
	if (LoopRise.Num() < 2 || LoopLength <= 0.f)
	{
		return LoopLength > 0.f ? Time / LoopLength : 0.f;
	}
	const float Index = FMath::Clamp(Time / LoopLength, 0.f, 1.f) * (LoopRise.Num() - 1);
	const int32 Low = FMath::FloorToInt(Index);
	const int32 High = FMath::Min(Low + 1, LoopRise.Num() - 1);
	return FMath::Lerp(LoopRise[Low], LoopRise[High], Index - Low);
}

FName ULadderClimbComponent::GetClipSlot() const
{
	// An enemy body plays its one-shots on its own full-body slot (the Frostbitten graph's DefaultSlot), not the operatives' FullBody.
	const UEnemyAnimInstance* EnemyAnim = Cast<UEnemyAnimInstance>(GetAnimInstance());
	return EnemyAnim ? EnemyAnim->OneShotSlot : MontageSlot;
}

UAnimMontage* ULadderClimbComponent::PlayClip(UAnimSequence* Clip, float Time, float BlendIn)
{
	UAnimInstance* Anim = GetAnimInstance();
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	CurrentMontage = nullptr; // the montage it replaces ends as "interrupted": not an interrupt of the climb
	const FName Slot = GetClipSlot();
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Clip, Slot, BlendIn, ExitBlendSeconds, 1.f, 1);
	if (!Montage || Anim->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, Time, /*bStopAllMontages*/ true) <= 0.f)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s: could not play %s on slot %s"), *GetNameSafe(GetOwner()), *Clip->GetName(), *Slot.ToString());
		return nullptr;
	}
	// The component sets the position every tick (the root track it follows is sampled at the same time).
	Anim->Montage_Pause(Montage);
	Anim->Montage_SetPosition(Montage, Time);
	FOnMontageEnded Ended;
	Ended.BindUObject(this, &ULadderClimbComponent::HandleMontageEnded);
	Anim->Montage_SetEndDelegate(Ended, Montage);
	CurrentMontage = Montage;
	return Montage;
}

void ULadderClimbComponent::SetClipTime(float Time)
{
	UAnimInstance* Anim = GetAnimInstance();
	if (Anim && CurrentMontage.IsValid())
	{
		Anim->Montage_SetPosition(CurrentMontage.Get(), Time);
	}
}

void ULadderClimbComponent::StopClip(float BlendOut)
{
	UAnimMontage* Montage = CurrentMontage.Get();
	CurrentMontage = nullptr;
	if (UAnimInstance* Anim = GetAnimInstance(); Anim && Montage)
	{
		Anim->Montage_Stop(BlendOut, Montage);
	}
}

void ULadderClimbComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Another full-body clip took the slot (a death, a stray one-shot): the one interrupt path, never a deadlock.
	if (Montage && Montage == CurrentMontage.Get() && bInterrupted && IsClimbing())
	{
		CurrentMontage = nullptr;
		const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
		AbortClimb(Health && !Health->IsAlive() ? ELadderAbortReason::Death : ELadderAbortReason::MontageInterrupted);
	}
}

// --- State machine ---------------------------------------------------------------------------------------------------

void ULadderClimbComponent::Enter(ELadderClimbState NewState, float ExpectedSeconds)
{
	if (NewState != State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: %s -> %s"), *GetNameSafe(GetOwner()), *LadderClimbInternal::StateName(State),
			*LadderClimbInternal::StateName(NewState));
	}
	State = NewState;
	StateTime = 0.f;
	StateTimeout = LadderRules::StateTimeout(ExpectedSeconds);
}

void ULadderClimbComponent::Fire(ELadderClimbEvent Event, float ExpectedSeconds)
{
	Enter(LadderRules::Transition(State, Event), ExpectedSeconds);
}

void ULadderClimbComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateTime += DeltaTime;
	AlignWalkSpeed = 0.f; // TickAlignTop sets it while he walks to the ladder head
	if (bEntryPending)
	{
		StartEntry();
	}
	const bool bLoop = State == ELadderClimbState::ClimbUp || State == ELadderClimbState::ClimbDown;
	IKWeight = FMath::FInterpConstantTo(IKWeight, bLoop ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(ClipBlendSeconds, 0.05f));
	switch (State)
	{
	case ELadderClimbState::None:
		PollLinks();
		break;
	case ELadderClimbState::Queue:
		QueueRetry -= DeltaTime;
		QueueSeconds += DeltaTime;
		if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()); Enemy && QueueSeconds > EnemyQueueSeconds)
		{
			// An enemy does not wait for ever: its AI decides afresh (the target may have moved; it may queue again).
			UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s stops waiting for %s after %.1f s"), *Enemy->GetName(), *GetNameSafe(Ladder.Get()), QueueSeconds);
			const FVector Destination = bHasPendingDestination ? PendingDestination : Enemy->GetActorLocation();
			AbortClimb(ELadderAbortReason::None);
			Enemy->ResumeMoveAfterLadder(Destination);
			break;
		}
		if (QueueRetry <= 0.f)
		{
			QueueRetry = 0.25f;
			PendingLadder = Ladder;
			bPendingUp = bGoingUp;
			StartEntry();
		}
		break;
	case ELadderClimbState::AlignBottom: TickAlignBottom(DeltaTime); break;
	case ELadderClimbState::AlignTop: TickAlignTop(DeltaTime); break;
	case ELadderClimbState::EnterTop:
	case ELadderClimbState::ExitTop: TickWarpClip(DeltaTime); break;
	case ELadderClimbState::ClimbUp:
	case ELadderClimbState::ClimbDown: TickLoop(DeltaTime); break;
	case ELadderClimbState::ExitBottom: TickExitBottom(DeltaTime); break;
	case ELadderClimbState::Dropping: TickDropping(DeltaTime); break;
	default: break;
	}
	UpdateIKFrame();
	// Both hands on the rungs: the rifle stays hidden for the whole climb (a weather pose ending in the same frame shows it again).
	if (IsClimbing())
	{
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner()); Operative && Operative->WeaponMesh && Operative->WeaponMesh->IsVisible())
		{
			Operative->WeaponMesh->SetVisibility(false, true);
		}
	}
	if (StateTimeout > 0.f && StateTime > StateTimeout && IsOnLadder())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s: %s timed out after %.1f s"), *GetNameSafe(GetOwner()), *LadderClimbInternal::StateName(State), StateTime);
		AbortClimb(ELadderAbortReason::Timeout);
	}
}

void ULadderClimbComponent::PollLinks()
{
	// Backup of the smart link's callback: a path that runs through a ladder's link and is at its start point.
	PollTimer -= GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	if (PollTimer > 0.f || bEntryPending)
	{
		return;
	}
	PollTimer = 0.1f;
	const ACharacter* Character = GetCharacter();
	const AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const ULadderSubsystem* Ladders = GetWorld() ? GetWorld()->GetSubsystem<ULadderSubsystem>() : nullptr;
	if (!Following || !Ladders || Following->GetStatus() != EPathFollowingStatus::Moving || !Following->GetPath().IsValid())
	{
		return;
	}
	bool bTop = false;
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	ALadderActor* Near = Ladders->FindLinkEndNear(Feet, 45.f, bTop);
	if (!Near || !Near->GetNavLink())
	{
		return;
	}
	const FNavLinkId LinkId = Near->GetNavLink()->GetId();
	bool bUsesLink = false;
	const FNavigationPath& Path = *Following->GetPath();
	for (const FNavPathPoint& Point : Path.GetPathPoints())
	{
		bUsesLink |= Point.CustomNavLinkId == LinkId;
	}
	if (const FNavMeshPath* NavPath = Path.CastPath<FNavMeshPath>(); !bUsesLink && NavPath)
	{
		if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavPath->GetNavigationDataUsed()))
		{
			for (const NavNodeRef Ref : NavPath->PathCorridor)
			{
				bUsesLink |= Recast->GetNavLinkUserId(Ref) == LinkId;
			}
		}
	}
	if (bUsesLink)
	{
		NotifyLinkReached(Near, !bTop);
	}
}

bool ULadderClimbComponent::NotifyLinkReached(ALadderActor* InLadder, bool bInGoingUp)
{
	if (!InLadder || IsOnLadder() || bEntryPending)
	{
		return InLadder != nullptr;
	}
	// The path's goal: he walks on there from the other end (7.1).
	const ACharacter* Character = GetCharacter();
	const AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	if (Following && Following->GetPath().IsValid() && Following->GetPath()->IsValid())
	{
		SetPendingDestination(Following->GetPath()->GetDestinationLocation());
	}
	else if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
	{
		SetPendingDestination(Operative->GetLastMoveDestination());
	}
	PendingLadder = InLadder;
	bPendingUp = bInGoingUp;
	bEntryPending = true; // the smart link calls back from inside the crowd update: start on the next tick
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s reached %s's link going %s"), *GetNameSafe(GetOwner()), *InLadder->GetName(), bInGoingUp ? TEXT("up") : TEXT("down"));
	return true;
}

void ULadderClimbComponent::SetPendingDestination(const FVector& Destination)
{
	PendingDestination = Destination;
	bHasPendingDestination = true;
}

void ULadderClimbComponent::Refuse(ELadderRefusal Refusal)
{
	LastRefusal = Refusal;
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: climb refused (%d)"), *GetNameSafe(GetOwner()), static_cast<int32>(Refusal));
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner()))
	{
		Operative->StopOperative();
		UFloatingTextSubsystem::SpawnAboveOperative(Operative, LadderClimbInternal::RefusalText(Refusal), FLinearColor(1.f, 0.6f, 0.2f));
	}
	else if (ACharacter* Character = GetCharacter(); Character && Character->GetController())
	{
		Character->GetController()->StopMovement();
	}
	if (ALadderActor* Held = Ladder.Get())
	{
		Held->ReleaseLease(GetOwner());
	}
	bHasPendingDestination = false;
	Ladder = nullptr;
	Enter(ELadderClimbState::None, 0.f);
}

void ULadderClimbComponent::StartEntry()
{
	bEntryPending = false;
	ALadderActor* Target = PendingLadder.Get();
	ACharacter* Character = GetCharacter();
	if (!Target || !Character)
	{
		return;
	}
	Ladder = Target;
	bGoingUp = bPendingUp;
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Character);
	const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	LadderRules::FEntryContext Context;
	Context.bDead = (Health && !Health->IsAlive()) || (Enemy && Enemy->IsDying());
	Context.bClimbable = Target->IsClimbable() && (!Enemy || LadderRules::EnemyMayClimb(Enemy->bCanClimbLadders, Enemy->CanClimbLadders(), Target->bAllowEnemies));
	Context.bHasClips = LoadClips();
	Context.bTurnBased = (TurnBased && TurnBased->IsActive()) || (Enemy && Enemy->IsTurnBasedHeld());
	Context.bMounted = VehicleQueries::IsMounted(Character);
	Context.bKnockedDown = (Operative && Operative->IsKnockedDown()) || (Enemy && Enemy->IsKnockedDown());
	Context.bCarrying = Operative && Operative->bCarrying;
	Context.bLeasedByOther = Target->IsLeasedByOther(Character);
	Context.bStanding = !Operative || Operative->GetStance() == EOperativeStance::Standing;
	const ELadderRefusal Refusal = LadderRules::CanStartClimb(Context);
	const bool bWasQueued = State == ELadderClimbState::Queue;
	if (Refusal == ELadderRefusal::Leased || Refusal == ELadderRefusal::MustStand
		|| (Refusal == ELadderRefusal::None && Operative && Operative->GetMoveHoldSeconds() > 0.f))
	{
		// He waits at the link: the ladder is busy, or he stands up first (prone / crouched is not a refusal).
		if (!bWasQueued)
		{
			QueueSeconds = 0.f;
			if (Operative)
			{
				const FVector Keep = PendingDestination;
				const bool bKeep = bHasPendingDestination;
				Operative->StopOperative();
				PendingDestination = Keep;
				bHasPendingDestination = bKeep;
			}
			else if (Character->GetController())
			{
				Character->GetController()->StopMovement();
			}
			Fire(ELadderClimbEvent::MustWait);
			QueueRetry = 0.25f;
		}
		if (Refusal == ELadderRefusal::MustStand && Operative)
		{
			Operative->SetStance(EOperativeStance::Standing);
		}
		LastRefusal = Refusal;
		return;
	}
	if (Refusal != ELadderRefusal::None)
	{
		Refuse(Refusal);
		return;
	}
	if (!Target->TryLease(Character))
	{
		Fire(ELadderClimbEvent::MustWait);
		return;
	}
	LastRefusal = ELadderRefusal::None;

	// Take the body over: the path ends here (it resumes at the other end), Flying, root motion ignored, the weapon hidden.
	{
		const FVector Keep = PendingDestination;
		const bool bKeep = bHasPendingDestination;
		if (Operative)
		{
			Operative->StopOperative();
		}
		else if (Character->GetController())
		{
			Character->GetController()->StopMovement();
		}
		PendingDestination = Keep;
		bHasPendingDestination = bKeep;
	}
	if (State == ELadderClimbState::Queue)
	{
		Enter(ELadderClimbState::None, 0.f);
	}
	TakeSnapshot();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->BrakingDecelerationFlying = 5000.f;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	if (UAnimInstance* Anim = GetAnimInstance())
	{
		Anim->RootMotionMode = ERootMotionMode::IgnoreRootMotion;
	}
	if (Operative && Operative->WeaponMesh)
	{
		Operative->WeaponMesh->SetVisibility(false, true);
	}
	if (Enemy)
	{
		Enemy->OnLadderClimbStarted();
	}

	const LadderRules::FClipContract Contract = Target->GetContract();
	const int32 Rungs = Target->GetRungCount();
	const float HalfHeight = GetHalfHeight();
	GlideFrom = Character->GetActorLocation();
	GlideFromYaw = Character->GetActorRotation().Yaw;
	if (bGoingUp)
	{
		TargetPos = LadderRules::ClimbOutStart(Contract, Rungs);
		LoopPos = LadderRules::BottomPos(TargetPos.Rung, LoopLength);
		const float FeetZ = LadderRules::HeightAt(LoopPos, LoopFraction(LoopPos.Time), Contract.Pitch);
		GlideTo = Target->GetHangWorldLocation(FeetZ) + FVector(0.f, 0.f, HalfHeight);
		GlideToYaw = Target->GetClimberYaw();
	}
	else if (bDescendFacingLadder)
	{
		// User feedback 2026-10-23: he walks to the ladder head (where ClimbOutTop ends), turns in place until he faces the ladder - his back
		// to the drop - and only then climbs down (ClimbOutTop backwards: the body faces the ladder the whole way).
		FVector Hang, Top, Scale;
		TopWarp(LadderRules::ClimbOutStart(Contract, Rungs).Rung, Hang, Top, Scale);
		GlideTo = Top;
		GlideToYaw = Target->GetClimberYaw();
	}
	else
	{
		// The old way: facing the drop (pre-turn when needed, 5.4); the IntoFromTop clip turns the body itself while it sinks.
		GlideTo = Target->GetTopLinkWorld() + FVector(0.f, 0.f, HalfHeight);
		GlideToYaw = Target->GetActorRotation().Yaw;
	}
	const float Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(GlideFromYaw, GlideToYaw));
	GlideSeconds = FMath::Max3(AlignMinSeconds, static_cast<float>(FVector::Dist(GlideFrom, GlideTo)) / AlignSpeed, Turn / AlignTurnRate);
	if (!bGoingUp && bDescendFacingLadder)
	{
		// Two phases at the top: the walk to the ladder head (facing the way he walks), then the turn in place (feet planted).
		const float Walk = static_cast<float>(FVector::Dist2D(GlideFrom, GlideTo));
		WalkSeconds = Walk > 5.f ? Walk / AlignSpeed : 0.f;
		const float WalkYaw = Walk > 10.f ? (GlideTo - GlideFrom).GetSafeNormal2D().Rotation().Yaw : GlideFromYaw;
		TurnSeconds = FMath::Max(TopTurnMinSeconds, FMath::Abs(FMath::FindDeltaAngleDegrees(WalkYaw, GlideToYaw)) / 360.f);
		GlideSeconds = WalkSeconds + TurnSeconds;
	}
	if (bGoingUp)
	{
		// The loop's start pose blends in while he steps onto the bottom rungs.
		PlayClip(LoadedUpLoop, LoopPos.Time, GlideSeconds);
		Fire(ELadderClimbEvent::LinkReachedBottom, GlideSeconds);
	}
	else
	{
		Fire(ELadderClimbEvent::LinkReachedTop, GlideSeconds);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s climbs %s %s (%d rungs, top %.0f cm, %s, loop rung %d t %.2f -> %d)"), *GetNameSafe(Character),
		bGoingUp ? TEXT("up") : TEXT("down"), *Target->GetName(), Rungs, LadderRules::TopZ(Rungs, Contract.Pitch),
		Target->HasWall() ? TEXT("wall") : TEXT("free-standing"), LoopPos.Rung, LoopPos.Time, TargetPos.Rung);
}

void ULadderClimbComponent::PlaceAt(const FVector& CapsuleCentre, float Yaw)
{
	if (ACharacter* Character = GetCharacter())
	{
		// The body's own hang offset (BodyHangOffset), faded with the rung IK: full on the loop, none at the ends.
		const ALadderActor* Target = Ladder.Get();
		const FVector BodyOffset = Target && IKWeight > 0.f
			? (Target->GetOutward() * BodyHangOffset.X + FVector::UpVector * BodyHangOffset.Y) * IKWeight : FVector::ZeroVector;
		Character->SetActorLocationAndRotation(CapsuleCentre + BodyOffset, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::None);
		Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}
}

void ULadderClimbComponent::TickAlignBottom(float DeltaTime)
{
	const float Alpha = FMath::Clamp(StateTime / FMath::Max(GlideSeconds, 0.01f), 0.f, 1.f);
	const float Smooth = FMath::SmoothStep(0.f, 1.f, Alpha);
	PlaceAt(FMath::Lerp(GlideFrom, GlideTo, Smooth), GlideFromYaw + FMath::FindDeltaAngleDegrees(GlideFromYaw, GlideToYaw) * Smooth);
	SetClipTime(LoopPos.Time);
	if (Alpha >= 1.f)
	{
		BeginLoop(true);
	}
}

void ULadderClimbComponent::TickAlignTop(float DeltaTime)
{
	if (bDescendFacingLadder)
	{
		// Phase 1: walk to the ladder head facing the way he walks; phase 2: turn in place to face the ladder (back to the drop).
		const float Walk = static_cast<float>(FVector::Dist2D(GlideFrom, GlideTo));
		const float WalkYaw = Walk > 10.f ? (GlideTo - GlideFrom).GetSafeNormal2D().Rotation().Yaw : GlideFromYaw;
		if (StateTime < WalkSeconds)
		{
			const float Alpha = FMath::Clamp(StateTime / FMath::Max(WalkSeconds, 0.01f), 0.f, 1.f);
			const float TurnIn = FMath::Clamp(StateTime / 0.2f, 0.f, 1.f);
			PlaceAt(FMath::Lerp(GlideFrom, GlideTo, Alpha), GlideFromYaw + FMath::FindDeltaAngleDegrees(GlideFromYaw, WalkYaw) * TurnIn);
			AlignWalkSpeed = Walk / FMath::Max(WalkSeconds, 0.01f);
			return;
		}
		const float Alpha = FMath::Clamp((StateTime - WalkSeconds) / FMath::Max(TurnSeconds, 0.01f), 0.f, 1.f);
		PlaceAt(GlideTo, WalkYaw + FMath::FindDeltaAngleDegrees(WalkYaw, GlideToYaw) * FMath::SmoothStep(0.f, 1.f, Alpha));
		if (Alpha >= 1.f)
		{
			BeginEnterTop();
		}
		return;
	}
	const float Alpha = FMath::Clamp(StateTime / FMath::Max(GlideSeconds, 0.01f), 0.f, 1.f);
	const float Smooth = FMath::SmoothStep(0.f, 1.f, Alpha);
	PlaceAt(FMath::Lerp(GlideFrom, GlideTo, Smooth), GlideFromYaw + FMath::FindDeltaAngleDegrees(GlideFromYaw, GlideToYaw) * Smooth);
	if (Alpha >= 1.f)
	{
		BeginEnterTop();
	}
}

void ULadderClimbComponent::BeginEnterTop()
{
	ALadderActor* Target = Ladder.Get();
	ACharacter* Character = GetCharacter();
	if (!Target || !Character)
	{
		AbortClimb(ELadderAbortReason::LeaseLost);
		return;
	}
	WarpYaw = Target->GetClimberYaw();
	PlaceAt(GlideTo, WarpYaw);
	Character->SetActorEnableCollision(false);
	if (bDescendFacingLadder)
	{
		// Facing the ladder, back to the drop: ClimbOutTop backwards from its last frame (he stands there now) down to the hang pose.
		FVector Hang, Top, Scale;
		TopWarp(LadderRules::ClimbOutStart(Target->GetContract(), Target->GetRungCount()).Rung, Hang, Top, Scale);
		WarpTrack = &ClimbOutTrack;
		WarpStart = Hang;
		WarpScale = Scale;
		bWarpReverse = true;
		ClipTime = ClimbOutTrack.Length;
		PlayClip(LoadedClimbOut, FMath::Max(0.f, ClimbOutTrack.Length - 0.001f), ClipBlendSeconds);
		Fire(ELadderClimbEvent::AlignDone, ClimbOutTrack.Length / ClimbPlayRate);
		return;
	}
	// The old way, wall-facing from here on (5.4): the clip's first frame still shows the body facing the drop - blend-in 0, nothing pops.
	bWarpReverse = false;
	const LadderRules::FClipContract Contract = Target->GetContract();
	const LadderRules::FLoopPos Landing = LadderRules::IntoFromTopEnd(Contract, Target->GetRungCount());
	const FVector End = Target->GetHangWorldLocation(LadderRules::HeightAt(Landing, 0.f, Contract.Pitch)) + FVector(0.f, 0.f, GetHalfHeight());
	WarpTrack = &IntoFromTopTrack;
	WarpStart = Character->GetActorLocation();
	const FVector Delta = FRotator(0.f, WarpYaw, 0.f).UnrotateVector(End - WarpStart);
	const FVector ClipEnd = IntoFromTopTrack.End();
	WarpScale = FVector(FMath::Abs(ClipEnd.X) > 1.f ? Delta.X / ClipEnd.X : 1.f, 1.f, FMath::Abs(ClipEnd.Z) > 1.f ? Delta.Z / ClipEnd.Z : 1.f);
	ClipTime = 0.f;
	PlayClip(LoadedIntoFromTop, 0.f, 0.f);
	Fire(ELadderClimbEvent::AlignDone, IntoFromTopTrack.Length / ClimbPlayRate);
}

void ULadderClimbComponent::TickWarpClip(float DeltaTime)
{
	if (!WarpTrack)
	{
		AbortClimb(ELadderAbortReason::Timeout);
		return;
	}
	ClipTime += (bWarpReverse ? -1.f : 1.f) * DeltaTime * ClimbPlayRate;
	const float Time = FMath::Clamp(ClipTime, 0.f, WarpTrack->Length);
	PlaceAt(WarpStart + FRotator(0.f, WarpYaw, 0.f).RotateVector(WarpTrack->Sample(Time) * WarpScale), WarpYaw);
	SetClipTime(Time);
	if (bWarpReverse ? ClipTime > 0.f : ClipTime < WarpTrack->Length)
	{
		return;
	}
	if (State == ELadderClimbState::ExitTop)
	{
		FinishClimb();
	}
	else
	{
		// IntoFromTop done: the loop pose IntoFromTopRungs below the platform, down the loop from here.
		if (ACharacter* Character = GetCharacter())
		{
			Character->SetActorEnableCollision(true);
		}
		const ALadderActor* Target = Ladder.Get();
		const LadderRules::FClipContract Contract = Target ? Target->GetContract() : LadderRules::FClipContract();
		const int32 Rungs = Target ? Target->GetRungCount() : 0;
		// The loop's start pose: ClimbOutRungs below the platform after the reversed ClimbOutTop, IntoFromTopRungs after IntoFromTop.
		LoopPos = bWarpReverse ? LadderRules::ClimbOutStart(Contract, Rungs) : LadderRules::IntoFromTopEnd(Contract, Rungs);
		bWarpReverse = false;
		TargetPos = LadderRules::BottomPos(LadderRules::ClimbOutStart(Contract, Rungs).Rung, LoopLength);
		PlayClip(LoadedUpLoop, LoopPos.Time, ClipBlendSeconds);
		BeginLoop(false);
	}
}

void ULadderClimbComponent::BeginLoop(bool bUp)
{
	const double Steps = FMath::Abs((TargetPos.Rung - LoopPos.Rung) * (LoopLength * 0.5) + (TargetPos.Time - LoopPos.Time));
	Fire(bUp ? ELadderClimbEvent::AlignDone : ELadderClimbEvent::EnterDone, static_cast<float>(Steps) / ClimbPlayRate);
}

void ULadderClimbComponent::TickLoop(float DeltaTime)
{
	ALadderActor* Target = Ladder.Get();
	if (!Target)
	{
		AbortClimb(ELadderAbortReason::LeaseLost);
		return;
	}
	const bool bUp = State == ELadderClimbState::ClimbUp;
	const bool bReached = LadderRules::Advance(LoopPos, DeltaTime * ClimbPlayRate, bUp ? 1 : -1, TargetPos, LoopLength);
	SetClipTime(LoopPos.Time);
	// Rung-index snapping: every loop boundary lands exactly on Floor + k * Pitch (HeightAt), the clip's error stays in one step.
	const float FeetZ = LadderRules::HeightAt(LoopPos, LoopFraction(LoopPos.Time), Target->GetContract().Pitch);
	PlaceAt(Target->GetHangWorldLocation(FeetZ) + FVector(0.f, 0.f, GetHalfHeight()), Target->GetClimberYaw());
	if (!bReached)
	{
		return;
	}
	if (bUp)
	{
		BeginExitTop();
	}
	else
	{
		BeginExitBottom();
	}
}

void ULadderClimbComponent::TopWarp(int32 Rung, FVector& OutHang, FVector& OutTop, FVector& OutScale) const
{
	const ALadderActor* Target = Ladder.Get();
	OutScale = FVector::OneVector;
	if (!Target)
	{
		OutHang = OutTop = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		return;
	}
	const FVector Up(0.f, 0.f, GetHalfHeight());
	const LadderRules::FClipContract Contract = Target->GetContract();
	OutHang = Target->GetHangWorldLocation(LadderRules::HeightAt(LadderRules::FLoopPos{ Rung, 0.f }, 0.f, Contract.Pitch)) + Up;
	const FRotator Facing(0.f, Target->GetClimberYaw(), 0.f);
	const FVector Delta = Facing.UnrotateVector(Target->GetTopExitWorld() + Up - OutHang);
	const FVector ClipEnd = ClimbOutTrack.End();
	// Forward / up scaled onto the exit point, the clip's own sideways step kept.
	OutScale = FVector(FMath::Abs(ClipEnd.X) > 1.f ? Delta.X / ClipEnd.X : 1.f, 1.f, FMath::Abs(ClipEnd.Z) > 1.f ? Delta.Z / ClipEnd.Z : 1.f);
	OutTop = OutHang + Facing.RotateVector(ClipEnd * OutScale);
}

void ULadderClimbComponent::BeginExitTop()
{
	ALadderActor* Target = Ladder.Get();
	ACharacter* Character = GetCharacter();
	if (!Target || !Character)
	{
		AbortClimb(ELadderAbortReason::LeaseLost);
		return;
	}
	// Over the edge with his own collision off (the platform / wall keep theirs, like the vault).
	Character->SetActorEnableCollision(false);
	bWarpReverse = false;
	WarpYaw = Target->GetClimberYaw();
	WarpTrack = &ClimbOutTrack;
	WarpStart = Character->GetActorLocation();
	const FVector End = Target->GetTopExitWorld() + FVector(0.f, 0.f, GetHalfHeight());
	const FVector Delta = FRotator(0.f, WarpYaw, 0.f).UnrotateVector(End - WarpStart);
	const FVector ClipEnd = ClimbOutTrack.End();
	WarpScale = FVector(FMath::Abs(ClipEnd.X) > 1.f ? Delta.X / ClipEnd.X : 1.f, 1.f, FMath::Abs(ClipEnd.Z) > 1.f ? Delta.Z / ClipEnd.Z : 1.f);
	ClipTime = 0.f;
	PlayClip(LoadedClimbOut, 0.f, ClipBlendSeconds);
	Fire(ELadderClimbEvent::TargetReached, ClimbOutTrack.Length / ClimbPlayRate);
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s climbs out on top (warp x %.2f z %.2f)"), *GetNameSafe(Character), WarpScale.X, WarpScale.Z);
}

void ULadderClimbComponent::BeginExitBottom()
{
	Fire(ELadderClimbEvent::TargetReached, ExitBlendSeconds);
	StopClip(ExitBlendSeconds);
	if (const ALadderActor* Target = Ladder.Get())
	{
		GlideFrom = GetOwner()->GetActorLocation();
		GlideTo = Target->GetHangWorldLocation(0.f) + FVector(0.f, 0.f, GetHalfHeight());
	}
}

void ULadderClimbComponent::TickExitBottom(float DeltaTime)
{
	const float Alpha = FMath::Clamp(StateTime / FMath::Max(ExitBlendSeconds, 0.01f), 0.f, 1.f);
	if (const ALadderActor* Target = Ladder.Get())
	{
		PlaceAt(FMath::Lerp(GlideFrom, GlideTo, Alpha), Target->GetClimberYaw());
	}
	if (Alpha >= 1.f)
	{
		FinishClimb();
	}
}

void ULadderClimbComponent::FinishClimb()
{
	ACharacter* Character = GetCharacter();
	ALadderActor* Target = Ladder.Get();
	const bool bTop = State == ELadderClimbState::ExitTop;
	if (Character)
	{
		// End fit: back with his collision on where the capsule fits.
		Character->SetActorEnableCollision(true);
		FVector Spot = Character->GetActorLocation();
		FRotator Facing = Character->GetActorRotation();
		if (GetWorld() && GetWorld()->FindTeleportSpot(Character, Spot, Facing))
		{
			Character->SetActorLocation(Spot, false, nullptr, ETeleportType::None);
		}
	}
	if (bTop)
	{
		StopClip(ExitBlendSeconds);
		++ClimbsUp;
	}
	else
	{
		++ClimbsDown;
	}
	RestoreSnapshot(false);
	if (Target)
	{
		Target->ReleaseLease(GetOwner());
	}
	Fire(ELadderClimbEvent::ExitDone);
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s is off %s at the %s"), *GetNameSafe(Character), *GetNameSafe(Target), bTop ? TEXT("top") : TEXT("bottom"));
	ResumePath();
}

void ULadderClimbComponent::ResumePath()
{
	const FVector Destination = PendingDestination;
	const bool bHadDestination = bHasPendingDestination;
	bHasPendingDestination = false;
	// Turn-based (LADDERS.md 7.6): the fight started while he climbed - he stays where the ladder left him and joins its next round.
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (TurnBased && TurnBased->IsActive())
	{
		return;
	}
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		Enemy->ResumeMoveAfterLadder(bHadDestination ? Destination : Enemy->GetActorLocation());
		return;
	}
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (bHadDestination && Operative && FVector::Dist2D(Destination, Operative->GetActorLocation()) > 60.f)
	{
		Operative->ResumeMoveAfterLadder(Destination);
	}
}

void ULadderClimbComponent::AbortClimb(ELadderAbortReason Reason)
{
	ACharacter* Character = GetCharacter();
	ALadderActor* Target = Ladder.Get();
	LastAbort = Reason;
	bEntryPending = false;
	if (State == ELadderClimbState::None || State == ELadderClimbState::Queue || State == ELadderClimbState::Dropping)
	{
		if (Target && State == ELadderClimbState::Queue)
		{
			Target->ReleaseLease(GetOwner());
		}
		if (State == ELadderClimbState::Queue)
		{
			Enter(ELadderClimbState::None, 0.f);
		}
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: climb aborted in %s (reason %d)"), *GetNameSafe(Character), *LadderClimbInternal::StateName(State),
		static_cast<int32>(Reason));
	StopClip(0.15f);
	if (Target)
	{
		Target->ReleaseLease(GetOwner());
	}
	if (!Character)
	{
		Enter(ELadderClimbState::None, 0.f);
		return;
	}
	if ((Reason == ELadderAbortReason::Death || Reason == ELadderAbortReason::Knockdown) && Reason != ELadderAbortReason::EndPlay)
	{
		// He falls to the bottom (7.7): off the hang line, his collision on, gravity.
		DropFeetZ = Character->GetActorLocation().Z - GetHalfHeight();
		DropSpeed = 0.f;
		bDropFloorFound = false;
		RestoreSnapshot(true);
		if (Target)
		{
			Character->SetActorLocation(Character->GetActorLocation() + Target->GetOutward() * DropAwayCm, false, nullptr, ETeleportType::None);
			// The floor below (a killed enemy's capsule has no collision: it falls by code onto it, see TickDropping).
			FHitResult Floor;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderDropFloor), false, Character);
			const FVector From = Character->GetActorLocation();
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Floor, From, From - FVector(0.f, 0.f, 5000.f), ECC_Visibility, Params);
			DropFloorZ = bHit ? static_cast<float>(Floor.ImpactPoint.Z) : Target->GetFloorZ();
			bDropFloorFound = true;
		}
		Fire(ELadderClimbEvent::Drop, 6.f);
		return;
	}
	RestoreSnapshot(false);
	if (Target && Reason != ELadderAbortReason::EndPlay)
	{
		// The nearest valid end (5.3); a blocked spot -> the other end.
		const bool bTop = LadderRules::NearestEndIsTop(LoopPos.Rung, Target->GetRungCount());
		FRotator Facing = Character->GetActorRotation();
		for (const bool bTryTop : { bTop, !bTop })
		{
			FVector Spot = (bTryTop ? Target->GetTopLinkWorld() : Target->GetBottomLinkWorld()) + FVector(0.f, 0.f, GetHalfHeight() + 2.f);
			if (GetWorld()->FindTeleportSpot(Character, Spot, Facing))
			{
				Character->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
				break;
			}
		}
	}
	Fire(ELadderClimbEvent::Abort);
}

bool ULadderClimbComponent::DropForKnockdown(EKnockdownDirection Direction, EKnockdownCause Cause)
{
	if (!IsClimbing())
	{
		return false;
	}
	bKnockdownOnLanding = true;
	DropDirection = Direction == EKnockdownDirection::None ? EKnockdownDirection::Back : Direction;
	DropCause = Cause;
	AbortClimb(ELadderAbortReason::Knockdown);
	return true;
}

void ULadderClimbComponent::TickDropping(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Enter(ELadderClimbState::None, 0.f);
		return;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (bDropFloorFound && Capsule && Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		// No collision to land with (a killed enemy): the fall by code, gravity onto the floor found at the drop.
		Character->GetCharacterMovement()->DisableMovement();
		DropSpeed += 980.f * DeltaTime;
		const float FeetZ = static_cast<float>(Character->GetActorLocation().Z) - GetHalfHeight();
		const float NewFeetZ = FMath::Max(DropFloorZ, FeetZ - DropSpeed * DeltaTime);
		Character->SetActorLocation(FVector(Character->GetActorLocation().X, Character->GetActorLocation().Y, NewFeetZ + GetHalfHeight()), false, nullptr,
			ETeleportType::TeleportPhysics);
		if (NewFeetZ > DropFloorZ + 0.5f && StateTime < 6.f)
		{
			return;
		}
	}
	else if (!Character->GetCharacterMovement()->IsMovingOnGround() && StateTime < 6.f)
	{
		return;
	}
	// Landed: fall damage (safe 1 m, 0.1 HP / cm, max 40), then the knockdown that caused the fall, by the normal rules.
	LastDropHeight = FMath::Max(0.f, DropFeetZ - (Character->GetActorLocation().Z - GetHalfHeight()));
	LastFallDamage = LadderRules::FallDamage(LastDropHeight, LadderRules::FFallDamage{ FallSafeHeightCm, FallDamagePerCm, FallDamageMax });
	UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	const bool bAlive = !Health || Health->IsAlive();
	Fire(ELadderClimbEvent::Landed);
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s landed after a %.0f cm fall: %.1f fall damage%s"), *GetNameSafe(Character), LastDropHeight, LastFallDamage,
		bKnockdownOnLanding ? TEXT(", knocked down") : TEXT(""));
	if (Health && bAlive && LastFallDamage > 0.f)
	{
		Health->ApplyDirectHealthLoss(LastFallDamage, TEXT("Fall"));
	}
	if (bKnockdownOnLanding && (!Health || Health->IsAlive()))
	{
		if (UKnockdownComponent* Knockdown = Character->FindComponentByClass<UKnockdownComponent>())
		{
			Knockdown->ForceKnockDown(DropDirection, DropCause);
		}
	}
	bKnockdownOnLanding = false;
}

void ULadderClimbComponent::HandleOwnerDied(AActor* Victim, const FString& AttackerSource)
{
	if (IsClimbing())
	{
		AbortClimb(ELadderAbortReason::Death);
	}
}

FVector ULadderClimbComponent::GetSaveLocation() const
{
	const ALadderActor* Target = Ladder.Get();
	const AActor* Owner = GetOwner();
	if (!Target || !Owner || !IsOnLadder())
	{
		return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	}
	const bool bTop = bGoingUp && State != ELadderClimbState::Dropping;
	return (bTop ? Target->GetTopLinkWorld() : Target->GetBottomLinkWorld()) + FVector(0.f, 0.f, GetHalfHeight() + 2.f);
}

// --- Snapshot ----------------------------------------------------------------------------------------------------------

void ULadderClimbComponent::TakeSnapshot()
{
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Snapshot.bValid = true;
	Snapshot.MovementMode = static_cast<uint8>(Movement->MovementMode.GetValue());
	Snapshot.GravityScale = Movement->GravityScale;
	Snapshot.BrakingFlying = Movement->BrakingDecelerationFlying;
	Snapshot.bOrientToMovement = Movement->bOrientRotationToMovement;
	Snapshot.bControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
	const UAnimInstance* Anim = GetAnimInstance();
	Snapshot.RootMotionMode = Anim ? static_cast<uint8>(Anim->RootMotionMode.GetValue()) : static_cast<uint8>(ERootMotionMode::RootMotionFromMontagesOnly);
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	Snapshot.bWeaponVisible = !Operative || !Operative->WeaponMesh || Operative->WeaponMesh->IsVisible();
}

void ULadderClimbComponent::RestoreSnapshot(bool bFalling)
{
	ACharacter* Character = GetCharacter();
	IKFrame.bActive = false;
	IKWeight = 0.f;
	if (!Character)
	{
		return;
	}
	Character->SetActorEnableCollision(true);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (Snapshot.bValid)
	{
		Movement->GravityScale = Snapshot.GravityScale;
		Movement->BrakingDecelerationFlying = Snapshot.BrakingFlying;
		Movement->bOrientRotationToMovement = Snapshot.bOrientToMovement;
		Movement->bUseControllerDesiredRotation = Snapshot.bControllerDesiredRotation;
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			Anim->RootMotionMode = static_cast<ERootMotionMode::Type>(Snapshot.RootMotionMode);
		}
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character); Operative && Operative->WeaponMesh)
		{
			// The weapon shows again with a skeletal body (the snapshot may have been taken under a weather pose that hid it).
			Operative->WeaponMesh->SetVisibility(Snapshot.bWeaponVisible || !Operative->UsesPlaceholderBody(), true);
		}
	}
	Movement->Velocity = FVector::ZeroVector;
	Movement->SetMovementMode(bFalling ? MOVE_Falling : MOVE_Walking);
	Snapshot.bValid = false;
}

// --- Rung IK -----------------------------------------------------------------------------------------------------------

void ULadderClimbComponent::UpdateIKFrame()
{
	const ALadderActor* Target = Ladder.Get();
	const bool bLoop = State == ELadderClimbState::ClimbUp || State == ELadderClimbState::ClimbDown;
	IKFrame.bActive = bRungIK && Target && IKWeight > 0.f && (bLoop || State == ELadderClimbState::ExitTop || State == ELadderClimbState::ExitBottom
		|| State == ELadderClimbState::EnterTop);
	if (!IKFrame.bActive || LoopLength <= 0.f)
	{
		IKFrame.bActive = false;
		return;
	}
	IKFrame.Outward = Target->GetOutward();
	IKFrame.Across = Target->GetActorRightVector().GetSafeNormal2D();
	IKFrame.HalfWidth = Target->GetHalfWidth() - 6.f;
	IKFrame.HandOffset = HandGripOffset;
	IKFrame.FootOffset = FootGripOffset;
	const float Normalized = FMath::Clamp(LoopPos.Time / LoopLength, 0.f, 1.f);
	for (int32 Index = 0; Index < static_cast<int32>(ELadderLimb::Count); ++Index)
	{
		const LadderRules::FLimbGrip Grip = LadderRules::LimbGrip(static_cast<ELadderLimb>(Index), Normalized);
		const int32 Rung = LoopPos.Rung + Grip.RungOffset;
		// The floor (rung 0) and rungs past the top are no grip: the clip's own foot / hand stays.
		const bool bValid = Rung >= 1 && Rung <= Target->GetRungCount();
		IKFrame.RungCentre[Index] = Target->GetRungWorldLocation(FMath::Clamp(Rung, 0, Target->GetRungCount()));
		IKFrame.Alpha[Index] = bValid ? Grip.Alpha * IKWeight : 0.f;
	}
}
