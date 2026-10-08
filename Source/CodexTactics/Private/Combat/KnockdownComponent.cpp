#include "Combat/KnockdownComponent.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "UI/GameMessageSubsystem.h"

UKnockdownComponent::UKnockdownComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetClipFolder(FString(), true);
}

void UKnockdownComponent::SetClipFolder(const FString& SubFolder, bool bMontages)
{
	const FString Folder = SubFolder.IsEmpty() ? TEXT("/Game/Animations_KnockDown") : FString::Printf(TEXT("/Game/Animations_KnockDown/%s"), *SubFolder);
	auto Path = [&Folder, bMontages](const TCHAR* Name)
	{
		const FString Asset = bMontages ? FString::Printf(TEXT("AM_%s"), Name) : FString(Name);
		return TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), *Folder, *Asset, *Asset)));
	};
	KnockedBackClip = Path(TEXT("Knocked_Back"));
	KnockedFrontClip = Path(TEXT("Knocked_Front"));
	ReviveBackClip = Path(TEXT("Revive_Back"));
	ReviveFrontClip = Path(TEXT("Revive_Front"));
	DeathBackClip = Path(TEXT("Death_Back"));
	DeathFrontClip = Path(TEXT("Death_Front"));
}

void UKnockdownComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		Health->OnDied.AddUniqueDynamic(this, &UKnockdownComponent::HandleOwnerDied);
	}
}

void UKnockdownComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const bool bFrozen = IsFrozenNow();
	if (!IsDown())
	{
		if (!bFrozen)
		{
			SecondsSinceGetUp += DeltaTime;
		}
		return;
	}
	KnockdownRules::SetTurnBased(State, IsTurnBasedNow());
	const EKnockdownPhase Old = State.Phase;
	const EKnockdownStep Step = KnockdownRules::Advance(State, DeltaTime, bFrozen);
	switch (Step)
	{
	case EKnockdownStep::Landed:
		SetPhase(EKnockdownPhase::Downed, Old);
		break;
	case EKnockdownStep::StartGetUp:
		SetPhase(EKnockdownPhase::GettingUp, Old);
		PlayPhaseClip();
		break;
	case EKnockdownStep::Recovered:
	{
		SecondsSinceGetUp = 0.f;
		++RecoveryCount;
		PlayingClip.Reset();
		PlayingMontage.Reset();
		SetPhase(EKnockdownPhase::None, Old);
		// Orders given while he lay run now (the owner restored movement in its phase handler).
		if (BufferedOrder)
		{
			TFunction<void()> Order = MoveTemp(BufferedOrder);
			BufferedOrder = nullptr;
			Order();
		}
		break;
	}
	default:
		break;
	}
}

bool UKnockdownComponent::CanFallNow() const
{
	const AActor* Owner = GetOwner();
	const UHealthComponent* Health = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Owner || !bCanBeKnockedDown || bDiedWhileDown || (Health && !Health->IsAlive()))
	{
		return false;
	}
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Owner))
	{
		// Already lying in the snow (prone) or mid-vault: nothing to knock over.
		return Operative->GetStance() != EOperativeStance::Prone && !Operative->IsVaulting();
	}
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Owner))
	{
		return !Enemy->IsDying() && !Enemy->IsJumpAttacking();
	}
	return true;
}

bool UKnockdownComponent::TryKnockDown(EKnockdownCause Cause, const FVector& SourceLocation, float Damage, bool bCritical)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !CanFallNow())
	{
		return false;
	}
	const FKnockdownConfig& Config = KnockdownRules::GetConfig();
	FKnockdownHit Hit;
	Hit.Cause = Cause;
	Hit.Damage = Damage;
	Hit.bCritical = bCritical;
	Hit.ExplosionDistance = FVector::Dist(Owner->GetActorLocation(), SourceLocation);
	FKnockdownTarget Target;
	Target.bCanBeKnockedDown = bCanBeKnockedDown;
	Target.bHeavyPoise = bHeavyPoise;
	Target.bAlreadyDown = IsDown();
	Target.SecondsSinceGetUp = SecondsSinceGetUp;
	if (!KnockdownRules::ShouldKnockDown(Config, Hit, Target))
	{
		return false;
	}
	return StartKnockdown(KnockdownRules::DirectionFromLocations(Owner->GetActorForwardVector(), Owner->GetActorLocation(), SourceLocation), Cause);
}

bool UKnockdownComponent::ForceKnockDown(EKnockdownDirection Direction, EKnockdownCause Cause)
{
	if (IsDown() || !CanFallNow())
	{
		return false;
	}
	return StartKnockdown(Direction, Cause);
}

int32 UKnockdownComponent::NotifyExplosion(UWorld* World, const FVector& Center)
{
	if (!World)
	{
		return 0;
	}
	const float Radius = KnockdownRules::GetConfig().ExplosionRadius;
	int32 Count = 0;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Center) >= FMath::Square(Radius + 100.f))
		{
			continue; // the rules decide on the exact distance; this only skips the far ones
		}
		if (UKnockdownComponent* Knockdown = It->FindComponentByClass<UKnockdownComponent>())
		{
			Count += Knockdown->TryKnockDown(EKnockdownCause::Explosion, Center) ? 1 : 0;
		}
	}
	return Count;
}

bool UKnockdownComponent::StartKnockdown(EKnockdownDirection Direction, EKnockdownCause Cause)
{
	const FKnockdownConfig& Config = KnockdownRules::GetConfig();
	const EKnockdownDirection Side = Direction == EKnockdownDirection::None ? EKnockdownDirection::Back : Direction;
	const UAnimSequenceBase* Fall = LoadClip(Side == EKnockdownDirection::Front ? KnockedFrontClip : KnockedBackClip);
	const UAnimSequenceBase* GetUp = LoadClip(Side == EKnockdownDirection::Front ? ReviveFrontClip : ReviveBackClip);
	const EKnockdownPhase Old = State.Phase;
	State = KnockdownRules::Start(Config, Side, Cause, ClipLength(Fall), ClipLength(GetUp), IsTurnBasedNow());
	GetUpPlayRate = KnockdownRules::ClipPlayRate(ClipLength(GetUp), Config.GetUpSeconds, Config.GetUpMaxPlayRate);
	++KnockdownCount;
	BufferedOrder = nullptr;
	SetPhase(EKnockdownPhase::Falling, Old); // the owner interrupts its actions first (stops the upper-body montages)
	PlayPhaseClip();
	static const TCHAR* Causes[] = { TEXT("knocked down"), TEXT("knocked down by a pounce"), TEXT("knocked down by a heavy blow"),
		TEXT("knocked down by the blast"), TEXT("knocked down by a heavy hit") };
	PostFeedLine(FString::Printf(TEXT("%s is %s!"), *GetUnitName(), Causes[FMath::Clamp(static_cast<int32>(Cause), 0, 4)]));
	UE_LOG(LogCodexTactics, Display, TEXT("[Knockdown] %s %s, falls on the %s (fall %.2f s, downed %.2f s, get-up %.2f s at %.2fx)"),
		*GetUnitName(), Causes[FMath::Clamp(static_cast<int32>(Cause), 0, 4)], Side == EKnockdownDirection::Front ? TEXT("face") : TEXT("back"),
		State.FallDuration, State.DownedDuration, State.GetUpDuration, GetUpPlayRate);
	return true;
}

void UKnockdownComponent::SetPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase)
{
	OnPhaseChanged.Broadcast(NewPhase, OldPhase);
	OnKnockdownChanged.Broadcast(NewPhase, State.Direction);
}

void UKnockdownComponent::PlayPhaseClip()
{
	const bool bFront = State.Direction == EKnockdownDirection::Front;
	switch (State.Phase)
	{
	case EKnockdownPhase::Falling:
		// The fall holds its last frame through the downed phase (the montage's auto blend-out is off on the instance).
		PlayClip(LoadClip(bFront ? KnockedFrontClip : KnockedBackClip), FallBlendInSeconds, 0.f, 1.f, true);
		break;
	case EKnockdownPhase::GettingUp:
		PlayClip(LoadClip(bFront ? ReviveFrontClip : ReviveBackClip), GetUpBlendInSeconds, GetUpBlendOutSeconds, GetUpPlayRate, false);
		break;
	default:
		break;
	}
}

UAnimInstance* UKnockdownComponent::GetAnimInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
}

FName UKnockdownComponent::GetSlotName() const
{
	const UAnimInstance* Anim = GetAnimInstance();
	if (const UOperativeAnimInstance* Operative = Cast<UOperativeAnimInstance>(Anim))
	{
		return Operative->FullBodySlot;
	}
	if (const UEnemyAnimInstance* Enemy = Cast<UEnemyAnimInstance>(Anim))
	{
		return Enemy->OneShotSlot;
	}
	return TEXT("DefaultSlot");
}

UAnimMontage* UKnockdownComponent::PlayClip(UAnimSequenceBase* Clip, float BlendIn, float BlendOut, float PlayRate, bool bHold)
{
	UAnimInstance* Anim = GetAnimInstance();
	PlayingClip = Clip;
	PlayingMontage.Reset();
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	const FName Slot = GetSlotName();
	UAnimMontage* Played = nullptr;
	if (UAnimMontage* Montage = Cast<UAnimMontage>(Clip))
	{
		if (Montage->IsValidSlot(Slot))
		{
			Played = Anim->Montage_Play(Montage, PlayRate) > 0.f ? Montage : nullptr;
		}
		else if (!Montage->SlotAnimTracks.IsEmpty() && !Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
		{
			// A montage authored on another slot: its clip plays on this anim's full-body slot.
			if (UAnimSequenceBase* Inner = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference())
			{
				Played = Anim->PlaySlotAnimationAsDynamicMontage(Inner, Slot, BlendIn, BlendOut, PlayRate);
			}
		}
	}
	else
	{
		Played = Anim->PlaySlotAnimationAsDynamicMontage(Clip, Slot, BlendIn, BlendOut, PlayRate);
	}
	if (FAnimMontageInstance* Instance = Played ? Anim->GetActiveInstanceForMontage(Played) : nullptr)
	{
		// Hold: the running instance's flag (the asset's is copied only when it starts).
		Instance->bEnableAutoBlendOut = !bHold;
	}
	PlayingMontage = Played;
	return Played;
}

UAnimSequenceBase* UKnockdownComponent::LoadClip(const TSoftObjectPtr<UAnimSequenceBase>& Clip) const
{
	return Clip.IsNull() ? nullptr : Clip.LoadSynchronous();
}

float UKnockdownComponent::ClipLength(const UAnimSequenceBase* Clip)
{
	return Clip ? Clip->GetPlayLength() : 0.f;
}

bool UKnockdownComponent::IsFrozenNow() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return KnockdownRules::AreTimersFrozen(Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
		UWorldAIPauseSubsystem::IsPausedIn(World));
}

bool UKnockdownComponent::IsTurnBasedNow() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
}

float UKnockdownComponent::GetDamageMultiplier(bool bMelee) const
{
	return GetBlowMultiplier(bMelee ? EKnockdownBlow::Melee : EKnockdownBlow::Ranged);
}

float UKnockdownComponent::GetBlowMultiplier(EKnockdownBlow Blow) const
{
	return IsDown() ? KnockdownRules::DownedBlowMultiplier(KnockdownRules::GetConfig(), Blow) : 1.f;
}

FKnockdownTurnDecision UKnockdownComponent::HandleTurn(int32& ActionPoints)
{
	const FKnockdownTurnDecision Decision = KnockdownRules::DecideTurnGetUp(KnockdownRules::GetConfig(), State, ActionPoints);
	if (Decision.bGetUp)
	{
		ActionPoints -= Decision.ActionPointsSpent;
		const EKnockdownPhase Old = State.Phase;
		KnockdownRules::BeginTurnGetUp(State);
		SetPhase(EKnockdownPhase::GettingUp, Old);
		PlayPhaseClip();
	}
	return Decision;
}

float UKnockdownComponent::GetUpForTurn()
{
	if (State.Phase != EKnockdownPhase::Downed)
	{
		return 0.f;
	}
	const EKnockdownPhase Old = State.Phase;
	KnockdownRules::BeginTurnGetUp(State);
	SetPhase(EKnockdownPhase::GettingUp, Old);
	PlayPhaseClip();
	return State.GetUpDuration;
}

void UKnockdownComponent::BufferOrder(TFunction<void()> Order)
{
	BufferedOrder = MoveTemp(Order);
}

bool UKnockdownComponent::HandleDeath()
{
	if (bDiedWhileDown)
	{
		return true;
	}
	if (!IsDown())
	{
		return false;
	}
	bDiedWhileDown = true;
	const bool bFront = State.Direction == EKnockdownDirection::Front;
	const EKnockdownPhase Old = State.Phase;
	const EKnockdownDirection Side = State.Direction;
	if (Old == EKnockdownPhase::GettingUp)
	{
		// Half up again: the fall plays once more from where it is (no standing death from a crouch).
		PlayClip(LoadClip(bFront ? KnockedFrontClip : KnockedBackClip), 0.15f, 0.f, 1.f, true);
	}
	else
	{
		PlayClip(LoadClip(bFront ? DeathFrontClip : DeathBackClip), 0.2f, 0.f, 1.f, true);
	}
	BufferedOrder = nullptr;
	State = FKnockdownState();
	State.Direction = Side;
	SetPhase(EKnockdownPhase::None, Old);
	UE_LOG(LogCodexTactics, Display, TEXT("[Knockdown] %s died while down (%s)"), *GetUnitName(), bFront ? TEXT("Death_Front") : TEXT("Death_Back"));
	return true;
}

void UKnockdownComponent::HandleOwnerDied(AActor* Victim, const FString& AttackerSource)
{
	HandleDeath();
}

void UKnockdownComponent::PostFeedLine(const FString& Line) const
{
	UWorld* World = GetWorld();
	UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	if (Messages)
	{
		Messages->PostMessage(FText::FromString(TEXT("COMBAT")), FText::FromString(Line));
	}
}

FString UKnockdownComponent::GetUnitName() const
{
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner()))
	{
		return Operative->DisplayName.ToString();
	}
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		return Enemy->GetEnemyDisplayName();
	}
	return GetOwner() ? GetOwner()->GetName() : FString();
}
