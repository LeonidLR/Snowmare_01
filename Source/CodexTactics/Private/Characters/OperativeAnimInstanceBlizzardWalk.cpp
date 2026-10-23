// Blizzard walk layer of UOperativeAnimInstance (game designer 2026-10-22; UE-only, no Godot reference).
// Outdoors in a blizzard a walking / jogging operative shields himself against the wind: the Storm_1 loops of
// /Game/90+_Weather_Animations (IK-retargeted copies by Scripts/Editor/setup_operative_storm_animation.py, arms stretched out in front
// of the face) play on UpperBodySlot - the ABP's layered blend from spine_01, so the legs keep the walk. The gate is the pure
// BlizzardWalkRules (weather x indoors x speed x state); the clips cycle like the weather idles (loop A x n -> transit -> loop B x n
// -> transit ...), blend in / out over 0.3 s, out over 0.15 s when the rifle is needed. The ABP graph is not changed.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "Camera/CameraZoneVolume.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Survival/ColdSurvivalComponent.h"

namespace
{
	constexpr int32 BlizzardClipLogMax = 128;
	/** A clip hands over to the next this long before its blend-out would start, s. */
	constexpr float BlizzardHandoffMargin = 0.05f;
	/** Shelter check period (a scan of the camera zones), s of game time. */
	constexpr double BlizzardShelterCheckSeconds = 0.25;
	/** A loop clip's montage repeats this often at most (the layer hands over to the transit long before). */
	constexpr int32 BlizzardLoopRepeat = 1;
	const TCHAR* const BlizzardClipDir = TEXT("/Game/Characters/Operatives/Anims/Weather/Storm/");

	/** The first clip of a montage's first slot track. */
	const UAnimSequenceBase* BlizzardMontageClip(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.IsEmpty() || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
		{
			return nullptr;
		}
		return Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get();
	}

	bool IsBlizzardSetClip(const FWarmIdleClipSet& Set, const UAnimSequenceBase* Clip)
	{
		return Clip && (Clip == Set.LoopA || Clip == Set.TransitAB || Clip == Set.LoopB || Clip == Set.TransitBA);
	}
}

void UOperativeAnimInstance::LoadDefaultBlizzardWalkClips()
{
	auto Fill = [](FWarmIdleClipSet& Set, const TCHAR* Folder, const TCHAR* Prefix)
	{
		auto Load = [Folder, Prefix](TObjectPtr<UAnimSequenceBase>& Field, const TCHAR* Tag)
		{
			if (!Field)
			{
				const FString Name = FString::Printf(TEXT("AS_%s_Storm_1_%s"), Prefix, Tag);
				Field = LoadObject<UAnimSequenceBase>(nullptr, *FString::Printf(TEXT("%s%s/%s.%s"), BlizzardClipDir, Folder, *Name, *Name), nullptr,
					LOAD_NoWarn | LOAD_Quiet);
			}
		};
		if (Set.LoopA)
		{
			return; // assigned in the class defaults
		}
		Load(Set.LoopA, TEXT("2Loop"));
		Load(Set.TransitAB, TEXT("3Tranzit"));
		Load(Set.LoopB, TEXT("4Loop"));
		Load(Set.TransitBA, TEXT("5Tranzit"));
	};
	Fill(BlizzardWalkMale, TEXT("Male"), TEXT("He"));
	Fill(BlizzardWalkFemale, TEXT("Female"), TEXT("She"));
}

const FWarmIdleClipSet* UOperativeAnimInstance::GetBlizzardWalkClipSet() const
{
	const bool bFemale = UsesFemaleWarmClips();
	const FWarmIdleClipSet& Preferred = bFemale ? BlizzardWalkFemale : BlizzardWalkMale;
	const FWarmIdleClipSet& Other = bFemale ? BlizzardWalkMale : BlizzardWalkFemale;
	return Preferred.IsValid() ? &Preferred : Other.IsValid() ? &Other : nullptr;
}

float UOperativeAnimInstance::GetBlizzardWalkWeight() const
{
	float Weight = 0.f;
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->Montage->IsValidSlot(UpperBodySlot)
			&& (IsBlizzardSetClip(BlizzardWalkMale, BlizzardMontageClip(Instance->Montage)) || IsBlizzardSetClip(BlizzardWalkFemale, BlizzardMontageClip(Instance->Montage))))
		{
			Weight += Instance->GetWeight();
		}
	}
	return FMath::Clamp(Weight, 0.f, 1.f);
}

bool UOperativeAnimInstance::HasOtherUpperBodyMontage() const
{
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		if (!Instance || !Instance->Montage || Instance->IsStopped() || !Instance->Montage->IsValidSlot(UpperBodySlot))
		{
			continue;
		}
		const UAnimSequenceBase* Clip = BlizzardMontageClip(Instance->Montage);
		if (!IsBlizzardSetClip(BlizzardWalkMale, Clip) && !IsBlizzardSetClip(BlizzardWalkFemale, Clip))
		{
			return true;
		}
	}
	return false;
}

bool UOperativeAnimInstance::PlayBlizzardWalkClip(EWarmClipStep Step, float BlendIn)
{
	const FWarmIdleClipSet* Set = GetBlizzardWalkClipSet();
	if (!Set)
	{
		return false;
	}
	UAnimSequenceBase* Clip = nullptr;
	switch (Step)
	{
	case EWarmClipStep::LoopA: Clip = Set->LoopA; break;
	case EWarmClipStep::TransitAB: Clip = Set->TransitAB; break;
	case EWarmClipStep::LoopB: Clip = Set->LoopB; break;
	case EWarmClipStep::TransitBA: Clip = Set->TransitBA; break;
	default: break;
	}
	if (!Clip || Clip->IsA<UAnimMontage>())
	{
		return false;
	}
	// Not "one montage per group": the previous storm clip fades out under the new one (the handoff), nothing else is stopped.
	const FMontageBlendSettings In(BlendIn);
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Clip, UpperBodySlot, In,
		FMontageBlendSettings(BlizzardWalkBlendSeconds), 1.f, BlizzardLoopRepeat);
	if (!Montage || Montage_PlayWithBlendSettings(Montage, In, 1.f, EMontagePlayReturnType::MontageLength, 0.f, /*bStopAllMontages*/ false) <= 0.f)
	{
		return false;
	}
	BlizzardWalkMontage = Montage;
	BlizzardWalkClip = Clip;
	BlizzardWalkStep = Step;
	if (BlizzardWalkClipLog.Num() >= BlizzardClipLogMax)
	{
		BlizzardWalkClipLog.RemoveAt(0);
	}
	BlizzardWalkClipLog.Add(Clip->GetName());
	return true;
}

void UOperativeAnimInstance::StopBlizzardWalk(float BlendOut)
{
	// Every storm montage (also a handoff still fading out).
	for (FAnimMontageInstance* Instance : MontageInstances)
	{
		if (Instance && Instance->Montage && !Instance->IsStopped() && Instance->Montage->IsValidSlot(UpperBodySlot)
			&& (IsBlizzardSetClip(BlizzardWalkMale, BlizzardMontageClip(Instance->Montage)) || IsBlizzardSetClip(BlizzardWalkFemale, BlizzardMontageClip(Instance->Montage))))
		{
			Instance->Stop(FAlphaBlend(FMath::Max(BlendOut, 0.f)));
		}
	}
	BlizzardWalkStep = EWarmClipStep::None;
	BlizzardWalkLoopsDone = 0;
	BlizzardWalkMontage.Reset();
	BlizzardWalkClip.Reset();
}

void UOperativeAnimInstance::UpdateBlizzardWalkLayer(const AOperativeCharacter& Operative)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// Weather (read per frame: cheap getters; the 45 s transitions fade the amount).
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	BlizzardAmount = Environment && Environment->IsInitialised()
		? BlizzardWalkRules::GetBlizzardAmount(Environment->GetWeather(), Environment->GetNextWeather(), Environment->GetTransitionBlend()) : 0.f;

	// Indoors: a Closed / Shelter camera zone (the cold's and the campfire's indoor rule) or a heat source's warm zone.
	if (BlizzardAmount > 0.f && (BlizzardShelterCheckTime < 0.0 || Now - BlizzardShelterCheckTime >= BlizzardShelterCheckSeconds || Now < BlizzardShelterCheckTime))
	{
		BlizzardShelterCheckTime = Now;
		bBlizzardSheltered = ACameraZoneVolume::IsShelteredLocation(World, Operative.GetActorLocation())
			|| (Operative.ColdSurvival && Operative.ColdSurvival->IsNearHeatSource());
	}

	UAnimMontage* Montage = BlizzardWalkMontage.Get();
	const bool bPlaying = Montage && Montage_IsPlaying(Montage);
	if (BlizzardWalkStep != EWarmClipStep::None && !bPlaying)
	{
		StopBlizzardWalk(BlizzardWalkInterruptBlendSeconds); // another layer stopped it (a fire / hit / reload montage of the same group)
	}
	const bool bActive = BlizzardWalkStep != EWarmClipStep::None;

	FBlizzardWalkInput Input;
	Input.BlizzardAmount = bUseBlizzardWalk ? BlizzardAmount : 0.f;
	Input.bSheltered = bBlizzardSheltered;
	Input.Speed = Speed;
	Input.Stance = Stance;
	Input.bSprinting = bIsSprinting;
	Input.bAiming = bIsAiming;
	FVector AimPoint;
	Input.bHasAimTarget = Operative.GetAimTargetPoint(AimPoint);
	Input.bReloading = bIsReloading;
	Input.bInCover = bInCover;
	Input.bVaulting = bIsVaulting;
	Input.bCarrying = Operative.bCarrying;
	Input.bDead = bIsDead;
	Input.bKnockedDown = bKnockedDown;
	Input.bOtherUpperBodyClip = HasOtherUpperBodyMontage() || IsInteractionClipActive() || LeftHandIKBlockSeconds > 0.f;
	Input.bFullBodyClip = bWarmIdleActive || IsPlayingStanceTransition() || GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f;
	FBlizzardWalkTuning Tuning;
	Tuning.StartSpeed = BlizzardWalkStartSpeed;
	Tuning.StopSpeed = BlizzardWalkStopSpeed;
	Tuning.MaxSpeed = BlizzardWalkMaxSpeed;
	BlizzardWalkBlock = BlizzardWalkRules::GetBlock(Input, Tuning, bActive);
	const bool bWanted = BlizzardWalkBlock == EBlizzardWalkBlock::None && GetBlizzardWalkClipSet() != nullptr;

	if (bActive && !bWanted)
	{
		// The rifle is needed / something else takes the body: quick; he stops / goes indoors / the storm ends: the normal blend.
		const bool bInterrupt = BlizzardWalkBlock == EBlizzardWalkBlock::Aiming || BlizzardWalkBlock == EBlizzardWalkBlock::Busy
			|| BlizzardWalkBlock == EBlizzardWalkBlock::OtherClip || BlizzardWalkBlock == EBlizzardWalkBlock::Down;
		UE_LOG(LogCodexTactics, Verbose, TEXT("[BlizzardWalk] %s: off (%s)"), *Operative.DisplayName.ToString(), BlizzardWalkRules::BlockName(BlizzardWalkBlock));
		StopBlizzardWalk(bInterrupt ? BlizzardWalkInterruptBlendSeconds : BlizzardWalkBlendSeconds);
	}
	else if (!bActive && bWanted)
	{
		BlizzardWalkLoopsDone = 0;
		BlizzardWalkLoopsTarget = FMath::RandRange(FMath::Max(1, BlizzardWalkLoopsMin), FMath::Max(BlizzardWalkLoopsMin, BlizzardWalkLoopsMax));
		// Either loop to start with (two operatives side by side do not mirror each other).
		const EWarmClipStep First = BlizzardWalkFirstStepForTesting != EWarmClipStep::None ? BlizzardWalkFirstStepForTesting
			: FMath::RandBool() ? EWarmClipStep::LoopA : EWarmClipStep::LoopB;
		if (PlayBlizzardWalkClip(First, BlizzardWalkBlendSeconds) || PlayBlizzardWalkClip(EWarmClipStep::LoopA, BlizzardWalkBlendSeconds))
		{
			UE_LOG(LogCodexTactics, Verbose, TEXT("[BlizzardWalk] %s: on (%s)"), *Operative.DisplayName.ToString(),
				BlizzardWalkClip.IsValid() ? *BlizzardWalkClip->GetName() : TEXT("-"));
		}
	}
	else if (bActive && Montage)
	{
		// Hand over to the next clip before this one blends out (the weather idles' cycle: loop A x n -> transit -> loop B x n ...).
		const float Rate = FMath::Max(FMath::Abs(Montage_GetPlayRate(Montage)), 0.01f);
		const float Left = (Montage->GetPlayLength() - Montage_GetPosition(Montage)) / Rate;
		if (Left <= BlizzardWalkBlendSeconds + BlizzardHandoffMargin)
		{
			const FWarmIdleClipSet* Set = GetBlizzardWalkClipSet();
			const bool bLoop = BlizzardWalkStep == EWarmClipStep::LoopA || BlizzardWalkStep == EWarmClipStep::LoopB;
			const int32 Done = bLoop ? BlizzardWalkLoopsDone + 1 : 0;
			EWarmClipStep Next = WarmIdleRules::NextClipStep(BlizzardWalkStep, Done, BlizzardWalkLoopsTarget, Set && Set->LoopB);
			if (Set && Next == EWarmClipStep::TransitAB && !Set->TransitAB)
			{
				Next = EWarmClipStep::LoopB;
			}
			if (Set && Next == EWarmClipStep::TransitBA && !Set->TransitBA)
			{
				Next = EWarmClipStep::LoopA;
			}
			BlizzardWalkLoopsDone = Next == BlizzardWalkStep ? Done : 0;
			if (Next != BlizzardWalkStep && (Next == EWarmClipStep::LoopA || Next == EWarmClipStep::LoopB))
			{
				BlizzardWalkLoopsTarget = FMath::RandRange(FMath::Max(1, BlizzardWalkLoopsMin), FMath::Max(BlizzardWalkLoopsMin, BlizzardWalkLoopsMax));
			}
			if (!PlayBlizzardWalkClip(Next, BlizzardWalkBlendSeconds))
			{
				PlayBlizzardWalkClip(EWarmClipStep::LoopA, BlizzardWalkBlendSeconds);
			}
		}
	}

	bBlizzardWalkActive = BlizzardWalkStep != EWarmClipStep::None;
	// The weapon goes while the pose is on and comes back as it blends out (UpdateWarmIdleLayer applies it with the weather idle's).
	bBlizzardWeaponHidden = bHideWeaponInBlizzardWalk && bBlizzardWalkActive && !bIsDead;
}
