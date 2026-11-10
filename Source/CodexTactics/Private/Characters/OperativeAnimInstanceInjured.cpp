// UOperativeAnimInstance: the injured locomotion layer (the game designer 2026-11-10: "operatives sometimes walk very slowly and I cannot see why -
// give a wounded operative a visible INJURED walk"; feat/injured-locomotion). UE-only (Godot had no injured walk).
// The designer's /Game/CON_Injured UE4-mannequin set (weapon_anim_sets.json "injured"; IK-retargeted copies /Game/Characters/Animations/Injured/
// {Male,Female} by Scripts/Editor/setup_injured_animation.py - the pack itself is never touched) blended natively over ABP_Operative, built like
// the prone crawl layer (OperativeAnimInstanceProneCrawl.cpp): no AnimGraph node, the user's ABP stays as it is.
// - Wounded (IsWounded) standing: the 8-direction injured walk (Walk_Injured_BS clips by the velocity relative to the body, one shared phase,
//   per-loop left-foot strike offsets) + the injured idle. Prone: the prone crawl layer plays the injured crawl forward (ActiveCrawlSet).
// - Cold walking (no wound): Freezing (>= "cold_from") the 8-direction injured walk, Frostbite (>= "cold_slow_from") the slow injured walk
//   (Walk_Injured_Slow) by a forward cone; still = the ABP's shiver idle.
// - The wound tiers (the game designer 2026-11-10, feat/injured-tiers-fullbody): light = the 8-direction walk + Run_Injured + the walk idles
//   (the run idles after a run), heavy = the slow walk forward + the slow idles, no run, critical = the injured crawl + the crawl idles (lying
//   still too: OperativeAnimInstanceProneCrawl.cpp); the idles alternate per loop.
// - FULL BODY while wounded and not firing (the hand on the wound, the rifle hanging from the right hand); a fire attempt / a shot / a reload
//   raise the torso back into the ABP's / the weapon layer's weapon pose over "raise_to_fire_s" (the shot waits for it), "lower_after_s" without
//   one lowers it. The cold walks keep the weapon torso (the lower body only, as before).
// Rules: Characters/InjuredLocomotionRules.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/InjuredLocomotionRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProneCrawlRules.h"
#include "Characters/WeaponLayerHandoffRules.h"
#include "HAL/IConsoleManager.h"

namespace OperativeInjuredLayer
{
	constexpr float InjuredMinWeight = 0.005f;

	TAutoConsoleVariable<int32> CVarInjuredLayer(TEXT("Codex.Anim.InjuredLayer"), 1,
		TEXT("1 = the native injured locomotion layer (CON_Injured, weapon_anim_sets.json \"injured\"): wounded = the injured walk / idle / crawl, cold = the slow injured walk; 0 = off (the ABP's locomotion; dev A/B)."));

	/** Bone's component-space rotation in Pose: its local rotations up the chain. */
	FQuat InjuredChainRotation(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
	{
		FQuat Rotation = FQuat::Identity;
		for (; Bone.GetInt() != INDEX_NONE; Bone = Pose.GetParentBoneIndex(Bone))
		{
			Rotation = Pose[Bone].GetRotation() * Rotation;
		}
		return Rotation;
	}

	FCompactPoseBoneIndex InjuredBone(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	}

	/** A bone's component-space location in the reference pose (zero for a missing bone). */
	FVector InjuredRefComponentLocation(const FBoneContainer& Bones, FCompactPoseBoneIndex Bone)
	{
		FTransform Chain = FTransform::Identity;
		for (; Bone.GetInt() != INDEX_NONE; Bone = Bones.GetParentBoneIndex(Bone))
		{
			Chain = Chain * Bones.GetRefPoseTransform(Bone);
		}
		return Chain.GetLocation();
	}
}

void UOperativeAnimInstance::LoadInjuredAnimSet()
{
	LoadWeaponAnimSet(TEXT("injured"), InjuredSetData, InjuredRetargetFolder);
	// The injured crawl = the prone crawl set with the injured crawl in its forward slot and the injured crawl idle (the other directions keep the
	// mocap crawl - the pack has one forward crawl); its own authored speed and left-hand reach offset.
	InjuredCrawlSetData = CrawlSetData;
	bHasInjuredCrawl = CrawlSetData.StandIdle && CrawlSetData.Walk[0] && InjuredSetData.InjuredCrawl && InjuredSetData.InjuredCrawlIdle;
	if (bHasInjuredCrawl)
	{
		InjuredCrawlSetData.Walk[0] = InjuredSetData.InjuredCrawl;
		InjuredCrawlSetData.WalkSpeeds[0] = InjuredSetData.InjuredCrawlSpeed;
		InjuredCrawlSetData.StandIdle = InjuredSetData.InjuredCrawlIdle;
		if (const float* Offset = InjuredSetData.PhaseOffsets.Find(InjuredSetData.InjuredCrawl->GetFName()))
		{
			InjuredCrawlSetData.PhaseOffsets.Add(InjuredSetData.InjuredCrawl->GetFName(), *Offset);
		}
	}
}

bool UOperativeAnimInstance::IsInjuredLayerEligible() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	return Operative && InjuredSetData.StandIdle && OperativeInjuredLayer::CVarInjuredLayer.GetValueOnGameThread() != 0 && !bIsProne && !bIsCrouching
		&& !bIsDead && !bKnockedDown && !bIsClimbing && !bIsVaulting && !bInCover && !bFallAirborne && !bSniperPose && !Operative->bCarrying
		&& !IsPlayingStanceTransition();
}

FString UOperativeAnimInstance::DescribeInjuredLayerGates() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	TArray<FString> Closed;
	auto Gate = [&Closed](bool bClosed, const TCHAR* Name)
	{
		if (bClosed)
		{
			Closed.Add(Name);
		}
	};
	Gate(!Operative, TEXT("no operative"));
	Gate(!InjuredSetData.StandIdle, TEXT("no set"));
	Gate(OperativeInjuredLayer::CVarInjuredLayer.GetValueOnGameThread() == 0, TEXT("cvar off"));
	Gate(bIsProne, TEXT("prone"));
	Gate(bIsCrouching, TEXT("crouched"));
	Gate(bIsDead, TEXT("dead"));
	Gate(bKnockedDown, TEXT("knocked down"));
	Gate(bIsClimbing, TEXT("ladder"));
	Gate(bIsVaulting, TEXT("vault"));
	Gate(bInCover, TEXT("cover"));
	Gate(bFallAirborne, TEXT("airborne"));
	Gate(bSniperPose, TEXT("sniper pose"));
	Gate(Operative && Operative->bCarrying, TEXT("carrying"));
	Gate(IsPlayingStanceTransition(), TEXT("stance clip"));
	Gate(GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f, TEXT("full-body clip"));
	return Closed.IsEmpty() ? FString(TEXT("open")) : FString::Join(Closed, TEXT(", "));
}

const UAnimSequenceBase* UOperativeAnimInstance::GetInjuredDominantClip() const
{
	const FOperativeClipState* Best = nullptr;
	for (const FOperativeClipState& Clip : InjuredClips)
	{
		Best = Clip.Sequence && (!Best || Clip.Weight > Best->Weight) ? &Clip : Best;
	}
	return Best && InjuredWeight > OperativeInjuredLayer::InjuredMinWeight ? Best->Sequence.Get() : nullptr;
}

void UOperativeAnimInstance::DemandInjuredRaise()
{
	const UWorld* World = GetWorld();
	LastInjuredDemandTime = World ? World->GetTimeSeconds() : 0.0;
	if (bInjuredCrawlActive)
	{
		HoldAimPose(); // the critical body rises from the crawl idle into the ABP's prone aim (the rifle on the target, like a stopped crawl)
	}
}

bool UOperativeAnimInstance::IsInjuredRaisePending() const
{
	const UWorld* World = GetWorld();
	if (!World || LastInjuredUpdateTime < 0.0 || World->GetTimeSeconds() - LastInjuredUpdateTime > 0.25)
	{
		return false; // the animation does not tick (off screen, no body): never hold a shot for a pose nobody sees
	}
	return InjuredLocomotionRules::HoldsShotForRaise(FMath::Max(InjuredUpperShare, bCriticalCrawlIdle ? CrawlWeight : 0.f));
}

void UOperativeAnimInstance::StepInjuredIdle(FInjuredIdleCycle& Cycle, int32 SetId, const TArray<UAnimSequence*>& Clips, float DeltaSeconds, bool bRandom,
	float FadeSeconds)
{
	auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
	if (Clips.IsEmpty())
	{
		Cycle = FInjuredIdleCycle();
		return;
	}
	Cycle.PrevTime = Cycle.Prev ? FMath::Fmod(Cycle.PrevTime + DeltaSeconds, Length(Cycle.Prev)) : 0.f;
	if (SetId != Cycle.SetId || !Cycle.Clip)
	{
		// A new set (a wound tier change, the run idle -> the walk idle): its first variant, cross-faded from what played.
		Cycle.Prev = Cycle.Clip;
		Cycle.PrevTime = Cycle.Time;
		Cycle.SetId = SetId;
		Cycle.Variant = InjuredLocomotionRules::NextIdleVariant(-1, Clips.Num(), bRandom, 0.f);
		Cycle.Clip = Clips[Cycle.Variant];
		Cycle.Time = 0.f;
		Cycle.Fade = Cycle.Prev ? 0.f : 1.f;
	}
	else
	{
		Cycle.Time += DeltaSeconds;
		const float ClipLength = Length(Cycle.Clip);
		if (Cycle.Time >= ClipLength)
		{
			// The loop's end: the next variant (alternating, or a random other one), cross-faded - the pack's idles start / end in one stance.
			const int32 Next = InjuredLocomotionRules::NextIdleVariant(Cycle.Variant, Clips.Num(), bRandom, FMath::FRand());
			if (Next != Cycle.Variant && Clips.IsValidIndex(Next))
			{
				Cycle.Prev = Cycle.Clip;
				Cycle.PrevTime = ClipLength - KINDA_SMALL_NUMBER;
				Cycle.Variant = Next;
				Cycle.Clip = Clips[Next];
				Cycle.Fade = 0.f;
				++Cycle.Switches;
			}
			Cycle.Time = FMath::Fmod(Cycle.Time - ClipLength, Length(Cycle.Clip));
		}
	}
	Cycle.Fade = FadeSeconds > KINDA_SMALL_NUMBER ? FMath::Min(1.f, Cycle.Fade + DeltaSeconds / FadeSeconds) : 1.f;
	if (Cycle.Fade >= 1.f)
	{
		Cycle.Prev = nullptr;
	}
}

void UOperativeAnimInstance::UpdateInjuredLayer(float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	using namespace InjuredLocomotionRules;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FShotgunAnimSet& Set = InjuredSetData;
	for (FOperativeClipState& Clip : InjuredClips)
	{
		Clip = FOperativeClipState();
	}
	if (!Operative)
	{
		InjuredWeight = 0.f;
		InjuredGait = EInjuredGait::None;
		bInjuredCrawlActive = false;
		InjuredRaise = InjuredUpperShare = InjuredCueOnly = 0.f;
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	LastInjuredUpdateTime = Now;
	const bool bLayerOn = OperativeInjuredLayer::CVarInjuredLayer.GetValueOnGameThread() != 0;
	// The wound tier (feat/injured-tiers-fullbody): light = the injured walk / run, heavy (and critical standing) = the slow walk, the cold keeps
	// its mapping unless the wound is worse.
	InjuredTier = Operative->GetWoundTier();
	InjuredGait = ChooseTierGait(Set.Triggers, InjuredTier, Operative->ColdLevel / 100.f);
	bInjuredCrawlActive = bLayerOn && UsesInjuredCrawl(InjuredGait, bIsProne, bHasInjuredCrawl);
	if (InjuredGait != EInjuredGait::None)
	{
		InjuredClipGait = InjuredGait; // a fading layer keeps the clips of the gait that ended
	}
	// The raise (the full body): a fire attempt (DemandInjuredRaise), a shot (HandleWeaponFired), a reload, the turn-based attack mode want the
	// weapon up; lower_after_s without one lowers it back into the injured clip.
	if (bIsReloading || (bIsAiming && AimTimer <= 0.f))
	{
		LastInjuredDemandTime = Now;
	}
	const float SinceDemand = LastInjuredDemandTime < 0.0 ? -1.f : static_cast<float>(Now - LastInjuredDemandTime);
	InjuredRaise = StepRaiseShare(InjuredRaise, SinceDemand, DeltaSeconds, Set.Raise);
	// The direction: the velocity relative to the body, eased and cross-faded on a reversal like the crawl (its own: the ABP's Direction is held
	// below 30 cm/s and a wounded, frozen walk is slower).
	const FVector Velocity = Operative->GetVelocity();
	const float MoveSpeed = static_cast<float>(Velocity.Size2D());
	if (MoveSpeed > 3.f || !InjuredDirection.bValid)
	{
		const float Raw = MoveSpeed > 3.f ? ProneCrawlRules::RelativeMoveDeg(static_cast<float>(Operative->GetActorRotation().Yaw), static_cast<float>(Velocity.Rotation().Yaw))
			: 0.f;
		WeaponLayerHandoffRules::StepLayerDirection(InjuredDirection, Raw, DeltaSeconds, Set.DirectionSmoothSeconds, Set.DirectionDeadZoneDeg,
			Set.DirectionSnapDeg, Set.DirectionFadeSeconds);
	}
	const float Move = MoveWeight(LocomotionSpeed, Set.MoveStartSpeed, Set.MoveFullSpeed);
	// A full-body clip owns the body (a hit, the warm idles, an interaction, the death): the layer fades out under it; an upper-body clip (fire,
	// reload, grenade, the blizzard shield) keeps the legs injured and takes the torso (UpperBodyShare).
	const bool bFullBodyClip = GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f;
	const float Target = LayerTarget(InjuredGait, IsInjuredLayerEligible() && !bFullBodyClip, Move);
	InjuredWeight = FMath::FInterpConstantTo(InjuredWeight, Target, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds));
	InjuredTorsoShare = TorsoPelvisShare(Set.TorsoPelvisShare, bIsAiming);
	// Crouched / in cover and wounded: no pack clip crouches or hugs a wall - the pose stays the ABP's / the cover clip's, the hunch of the limp
	// cue alone shows the wound (crouch_scale / cover_scale; off while the weapon is up).
	const bool bWound = bLayerOn && IsWoundGait(InjuredGait);
	const bool bCueOnlyPose = bWound && !bIsProne && !bIsDead && !bKnockedDown && !bIsClimbing && !bIsVaulting && !bFullBodyClip && (bInCover || bIsCrouching);
	const float CueOnlyTarget = bCueOnlyPose ? (bInCover ? Set.LimpCue.CoverScale : Set.LimpCue.CrouchScale) * (1.f - InjuredRaise) : 0.f;
	InjuredCueOnly = Set.LimpCue.bEnabled ? FMath::FInterpConstantTo(InjuredCueOnly, CueOnlyTarget, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds)) : 0.f;
	InjuredRunShare = 0.f;
	if (InjuredSinceRun >= 0.f)
	{
		InjuredSinceRun += DeltaSeconds;
	}
	if (InjuredWeight <= OperativeInjuredLayer::InjuredMinWeight)
	{
		InjuredWeight = 0.f;
		InjuredPlayRate = 0.f;
		InjuredUpperShare = 0.f;
		InjuredIdleSetNow = EInjuredIdleSet::None;
		InjuredIdle = FInjuredIdleCycle();
		InjuredLimpAmount = InjuredCueOnly;
		InjuredLimpAngles = InjuredCueOnly > OperativeInjuredLayer::InjuredMinWeight ? LimpCueAngles(Set.LimpCue, InjuredCueOnly, 0.f, 0.f, false) : FVector2D::ZeroVector;
		if (Target <= 0.f)
		{
			InjuredClipGait = InjuredGait;
		}
		return;
	}
	auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
	int32 Next = 0;
	auto Add = [this, &Next](UAnimSequence* Clip, float Weight, float Time)
	{
		if (Clip && Weight > OperativeInjuredLayer::InjuredMinWeight && Next < InjuredClips.Num())
		{
			InjuredClips[Next++] = { Clip, Time, Weight };
		}
	};
	auto At = [this, &Set](const UAnimSequence* Clip)
	{
		return Clip ? SyncedClipTime(InjuredPhase, Set.PhaseOffsets.FindRef(Clip->GetFName()), Clip->GetPlayLength()) : 0.f;
	};
	// The 8-direction injured walk (light wound, freezing; the heavy wound / frostbite outside the slow walk's forward cone), the slow walk (heavy
	// wound, frostbite, by the cone) and the run (light wound, by the speed band and its forward cone) on ONE shared phase: each loop = one gait
	// cycle, its stride = its authored speed x its length; the cycle is the clips' blend, played at the body's real speed - the feet of every
	// loop in play strike together (the per-loop left-heel strike offsets). A wound: its idle set against the walk by the smoothed speed; cold:
	// no idle (the layer's weight is the move share - standing still is the ABP's shiver).
	const TStaticArray<bool, 8> Avail = [&Set]()
	{
		TStaticArray<bool, 8> Out;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out[Index] = Set.Walk[Index] != nullptr;
		}
		return Out;
	}();
	auto Banded = [&Set, &Avail](float Angle)
	{
		FWeaponLayerDirectionBlend Blend = BlendDirections(Angle, Avail);
		Blend.Alpha = BandedAlpha(Blend.Alpha, Set.DirectionClipBandDeg);
		return Blend;
	};
	const FWeaponLayerDirectionBlend Dir = Banded(InjuredDirection.Current);
	const FWeaponLayerDirectionBlend FromDir = Banded(InjuredDirection.From);
	const float FromShare = 1.f - InjuredDirection.Fade;
	const float Slow = Set.SlowWalk ? SlowWalkShare(InjuredClipGait, ForwardConeShare(InjuredDirection.Current, Set.SlowConeFullDeg, Set.SlowConeZeroDeg)) : 0.f;
	const float Run = Set.Run ? RunShare(InjuredClipGait, LocomotionSpeed, Set.RunStartSpeed, Set.RunFullSpeed,
		ForwardConeShare(InjuredDirection.Current, Set.RunConeFullDeg, Set.RunConeZeroDeg)) : 0.f;
	InjuredRunShare = Run;
	if (Run >= 0.5f)
	{
		InjuredSinceRun = 0.f; // standing within run_idle_hold_s from now on plays the run idles (out of breath)
	}
	const bool bWoundClips = IsWoundGait(InjuredClipGait);
	if (bWoundClips)
	{
		InjuredIdleSetNow = ChooseIdleSet(InjuredClipGait, InjuredSinceRun, Set.RunIdleHoldSeconds);
		const TArray<UAnimSequence*>* Clips = &Set.StandIdles;
		if (InjuredIdleSetNow == EInjuredIdleSet::Run && !Set.RunIdles.IsEmpty())
		{
			Clips = &Set.RunIdles;
		}
		else if (InjuredIdleSetNow == EInjuredIdleSet::Slow && !Set.SlowIdles.IsEmpty())
		{
			Clips = &Set.SlowIdles;
		}
		StepInjuredIdle(InjuredIdle, static_cast<int32>(InjuredIdleSetNow), *Clips, DeltaSeconds, Set.bIdleRandom, Set.IdleCrossfadeSeconds);
		Add(InjuredIdle.Clip, (1.f - Move) * InjuredIdle.Fade, InjuredIdle.Time);
		Add(InjuredIdle.Prev, (1.f - Move) * (1.f - InjuredIdle.Fade), InjuredIdle.PrevTime);
	}
	else
	{
		InjuredIdleSetNow = EInjuredIdleSet::None;
		InjuredIdle = FInjuredIdleCycle();
	}
	const float WalkShare = bWoundClips ? Move : 1.f;
	auto CycleOf = [&Set, &Length](const FWeaponLayerDirectionBlend& Blend)
	{
		const float Nominal = BlendedNominalSpeed(Blend, Set.WalkSpeeds, Set.WalkSpeed);
		return Nominal * FMath::Lerp(Length(Set.Walk[static_cast<int32>(Blend.A)]), Length(Set.Walk[static_cast<int32>(Blend.B)]), Blend.Alpha);
	};
	const float WalkCycle = FMath::Lerp(CycleOf(Dir), CycleOf(FromDir), FMath::Clamp(FromShare, 0.f, 1.f));
	const float SlowLength = Length(Set.SlowWalk);
	const float Cycle = FMath::Lerp(FMath::Lerp(WalkCycle, Set.SlowWalkSpeed * SlowLength, Slow), Set.RunSpeed * Length(Set.Run), Run);
	UAnimSequence* Lead = Run >= 0.5f ? Set.Run : (Slow >= 0.5f ? Set.SlowWalk : Set.Walk[static_cast<int32>(Dir.Alpha < 0.5f ? Dir.A : Dir.B)]);
	const float LeadLength = Length(Lead);
	const float MaxRate = FMath::Lerp(Set.MaxPlayRate, Set.SlowMaxPlayRate > 0.f ? Set.SlowMaxPlayRate : Set.MaxPlayRate, Slow);
	const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, Cycle, 0.f, 0.f, MaxRate, LeadLength);
	InjuredPhase = FMath::Fmod(InjuredPhase + DeltaSeconds * Rate, 1.f);
	InjuredPlayRate = Rate * LeadLength;
	// The torso limp cue (bug 2026-11-10: from the game camera the lower-body limp under the upright rifle torso did not read): the hunch by the
	// layer's weight, the sway by the walk share on the shared gait phase (once a cycle - the hurt leg's stance). Under the full body it shows
	// only while the weapon is up (the proxy blends it away with the clip's own torso).
	InjuredLimpAmount = Set.LimpCue.bEnabled ? InjuredWeight : 0.f;
	InjuredLimpAngles = LimpCueAngles(Set.LimpCue, InjuredWeight, WalkShare, InjuredPhase, bIsAiming);
	// The full body (a wound only; the cold walks keep the weapon torso): the torso's share in the injured clip.
	InjuredUpperShare = bWoundClips ? UpperBodyShare(Set.Raise, InjuredWeight, InjuredRaise, GetSlotMontageGlobalWeight(UpperBodySlot)) : 0.f;
	auto AddPair = [&Add, &At, &Set](const FWeaponLayerDirectionBlend& Blend, float Weight)
	{
		UAnimSequence* A = Set.Walk[static_cast<int32>(Blend.A)];
		UAnimSequence* B = Set.Walk[static_cast<int32>(Blend.B)];
		Add(A, Weight * (1.f - Blend.Alpha), At(A));
		Add(B, Weight * Blend.Alpha, At(B));
	};
	Add(Set.Run, WalkShare * Run, At(Set.Run));
	Add(Set.SlowWalk, WalkShare * (1.f - Run) * Slow, At(Set.SlowWalk));
	AddPair(Dir, WalkShare * (1.f - Run) * (1.f - Slow) * (1.f - FromShare));
	if (FromShare > OperativeInjuredLayer::InjuredMinWeight)
	{
		AddPair(FromDir, WalkShare * (1.f - Run) * (1.f - Slow) * FromShare);
	}
}

void FOperativeAnimInstanceProxy::BlendInjuredLayer(FPoseContext& Output) const
{
	using namespace OperativeInjuredLayer;
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex Spine = InjuredBone(Bones, TEXT("spine_01"));
	const FCompactPoseBoneIndex SpineParent = Spine.GetInt() != INDEX_NONE ? Output.Pose.GetParentBoneIndex(Spine) : FCompactPoseBoneIndex(INDEX_NONE);
	// The body's forward / lateral axes in component space for the limp cue: from the reference pose (the root goes to the reference below),
	// forward = the left foot -> its ball, horizontal; the hunch axis = up x forward (rotates up towards forward).
	FVector CueForward = FVector::ForwardVector;
	FVector CueHunchAxis = FVector::RightVector;
	if (!InjuredLimpAngles.IsNearlyZero())
	{
		const FVector Foot = InjuredRefComponentLocation(Bones, InjuredBone(Bones, TEXT("foot_l")));
		const FVector Ball = InjuredRefComponentLocation(Bones, InjuredBone(Bones, TEXT("ball_l")));
		const FVector Toward = FVector(Ball.X - Foot.X, Ball.Y - Foot.Y, 0.f).GetSafeNormal();
		if (!Toward.IsNearlyZero())
		{
			CueForward = Toward;
			CueHunchAxis = FVector::CrossProduct(FVector::UpVector, CueForward).GetSafeNormal();
		}
	}
	auto ApplyCue = [this, &CueForward, &CueHunchAxis](const FQuat& ParentNow, const FQuat& Local)
	{
		// The limp cue: a component-space bend of the whole torso about the pelvis - the hunch about the body's lateral axis (up towards forward),
		// the sway about its forward axis - re-expressed in spine_01's local space (the aim's yaw untouched).
		const FQuat Cue = (FQuat(CueHunchAxis, FMath::DegreesToRadians(InjuredLimpAngles.X)) * FQuat(CueForward, FMath::DegreesToRadians(InjuredLimpAngles.Y))).GetNormalized();
		return (ParentNow.Inverse() * Cue * ParentNow * Local).GetNormalized();
	};
	if (InjuredWeight <= InjuredMinWeight)
	{
		// The cue alone (crouched / in cover and wounded): the pose stays the graph's, the torso hunches.
		if (!InjuredLimpAngles.IsNearlyZero() && Spine.GetInt() != INDEX_NONE && SpineParent.GetInt() != INDEX_NONE)
		{
			Output.Pose[Spine].SetRotation(ApplyCue(InjuredChainRotation(Output.Pose, SpineParent), Output.Pose[Spine].GetRotation()));
		}
		return;
	}
	// The layer's pose: its clips blended by weight (the crawl layer's sampling; at most eight clips, inline - nothing allocates on the heap).
	TArray<FCompactPose, TInlineAllocator<8>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<8>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<8>> Attributes;
	TArray<float, TInlineAllocator<8>> Weights;
	float Total = 0.f;
	for (const FOperativeClipState& Clip : InjuredClips)
	{
		if (!Clip.Sequence || Clip.Weight < InjuredMinWeight)
		{
			continue;
		}
		FCompactPose& Pose = Poses.AddDefaulted_GetRef();
		Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve& Curve = Curves.AddDefaulted_GetRef();
		Curve.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer& Attribute = Attributes.AddDefaulted_GetRef();
		FAnimationPoseData PoseData(Pose, Curve, Attribute);
		Clip.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Clip.Time), false));
		Weights.Add(Clip.Weight);
		Total += Clip.Weight;
	}
	if (Poses.IsEmpty() || Total <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	for (float& Weight : Weights)
	{
		Weight /= Total;
	}
	FCompactPose Layer;
	Layer.SetBoneContainer(&Output.Pose.GetBoneContainer());
	FBlendedCurve LayerCurve;
	LayerCurve.InitFrom(Output.Curve);
	UE::Anim::FStackAttributeContainer LayerAttributes;
	FAnimationPoseData LayerData(Layer, LayerCurve, LayerAttributes);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, LayerData);

	const float Weight = FMath::Min(1.f, InjuredWeight);
	// The full body (feat/injured-tiers-fullbody): the torso's share in the clip - the hand on the wound, the rifle hanging from the right hand
	// (it stays on its hand socket); 0 = the weapon pose (raised to fire).
	const float UpperShare = FMath::Clamp(InjuredUpperShare, 0.f, 1.f);
	const FCompactPoseBoneIndex HandIK = InjuredBone(Bones, TEXT("ik_hand_root"));
	// The graph's (+ the weapon layer's) torso and hand IK placement before the legs change: kept in component space.
	const FCompactPoseBoneIndex Root(0);
	const FTransform GraphRoot = Output.Pose[Root];
	const FQuat SpineParentGraph = SpineParent.GetInt() != INDEX_NONE ? InjuredChainRotation(Output.Pose, SpineParent) : FQuat::Identity;
	const float TorsoShare = FMath::Clamp(InjuredTorsoShare, 0.f, 1.f);
	// 1 = the torso chain (spine_01 and below), 2 = the hand IK chain (ik_hand_root and below).
	TArray<uint8, TInlineAllocator<128>> Chain;
	Chain.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Chain[Index.GetInt()] = Index == Spine ? 1 : (Index == HandIK ? 2 : (Parent.GetInt() != INDEX_NONE ? Chain[Parent.GetInt()] : 0));
		if (Index.IsRootBone())
		{
			// The root faces the capsule (the reference): the ABP's orientation warping / root yaw turns it towards the movement for ITS forward
			// legs - the directional injured legs on that turned root would walk off the travel and slide (the weapon sets' and the crawl's rule).
			const FTransform& Reference = Bones.GetRefPoseTransform(Index);
			Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Reference.GetRotation(), Weight).GetNormalized());
			Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Reference.GetTranslation(), Weight));
			continue;
		}
		if (Index == Spine && SpineParent.GetInt() != INDEX_NONE)
		{
			// The torso keeps the graph's mesh-space rotation on the injured pelvis (its local rotation re-expressed under the new pelvis chain -
			// parents are final, they come first), TorsoShare of the pelvis's own limp riding along (0 while aiming: the aim stays exact) + the
			// limp cue; then UpperShare towards the clip's own torso (the full body). The local translation stays: the torso rides the pelvis's bob.
			const FQuat Graph = Output.Pose[Index].GetRotation();
			const FQuat ParentNow = InjuredChainRotation(Output.Pose, SpineParent);
			const FQuat Kept = (ParentNow.Inverse() * SpineParentGraph * Graph).GetNormalized();
			FQuat Local = FQuat::Slerp(Kept, Graph, TorsoShare).GetNormalized();
			if (!InjuredLimpAngles.IsNearlyZero())
			{
				Local = ApplyCue(ParentNow, Local);
			}
			if (UpperShare > InjuredMinWeight)
			{
				Local = FQuat::Slerp(Local, Layer[Index].GetRotation(), UpperShare).GetNormalized();
				Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Layer[Index].GetTranslation(), UpperShare));
			}
			Output.Pose[Index].SetRotation(Local);
			continue;
		}
		if (Index == HandIK && Parent.GetInt() != INDEX_NONE)
		{
			// The hand IK bones (under the root) keep their component-space transform: the root under them turned.
			const FTransform ParentNow = Parent.IsRootBone() ? Output.Pose[Parent] : FTransform(InjuredChainRotation(Output.Pose, Parent));
			const FTransform ParentGraph = Parent.IsRootBone() ? GraphRoot : ParentNow;
			Output.Pose[Index] = Output.Pose[Index] * ParentGraph * ParentNow.Inverse();
			continue;
		}
		const uint8 Kind = Chain[Index.GetInt()];
		if (Kind == 0)
		{
			Output.Pose[Index].BlendWith(Layer[Index], Weight);
		}
		else if (Kind == 1 && UpperShare > InjuredMinWeight)
		{
			Output.Pose[Index].BlendWith(Layer[Index], UpperShare); // the arms, the neck, the head: the clip's (the hand on the wound)
		}
	}
}
