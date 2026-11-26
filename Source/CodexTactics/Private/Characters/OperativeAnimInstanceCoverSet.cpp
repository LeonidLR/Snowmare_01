// Copyright CodexTactics. Unreal-only feature: data-driven cover animation sets (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md).
// The indoor Riffle Cover set today (Content/Data/Animation/cover_anim_sets.json); the stealth wall set / per-operative wall styles later.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/CoverAnimSetRules.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "CodexTactics.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tactics/CoverFacingRules.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCoverAnimSets(TEXT("Codex.Cover.AnimSets"), -1,
		TEXT("Cover animation sets (cover_anim_sets.json): -1 = the JSON's 'enabled' and the ABP's bUseCoverAnimSets, 0 = the native M4 clips everywhere, 1 = on."));

	TAutoConsoleVariable<int32> CVarRiffleOutdoor(TEXT("Codex.Cover.RiffleOutdoor"), 0,
		TEXT("Try the Riffle cover animation set on outdoor walls (0 = normal context selection, 1 = Riffle outdoors). Enter cover again after changing it."));

	FCoverAnimSetConfig& CoverSetConfigStorage()
	{
		static FCoverAnimSetConfig Config;
		return Config;
	}
	bool GCoverSetConfigLoaded = false;

	const FCoverAnimSetConfig& CoverSetConfig()
	{
		if (!GCoverSetConfigLoaded)
		{
			GCoverSetConfigLoaded = true;
			FString Text;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Animation/cover_anim_sets.json");
			FString Error;
			if (!FFileHelper::LoadFileToString(Text, *Path) || !CoverAnimSetRules::ParseConfig(Text, CoverSetConfigStorage(), Error))
			{
				CoverSetConfigStorage() = FCoverAnimSetConfig();
				UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s not read (%s): the native cover clips everywhere"), *Path, Error.IsEmpty() ? TEXT("missing") : *Error);
			}
		}
		return CoverSetConfigStorage();
	}

	FAutoConsoleCommand CmdReloadCoverSets(TEXT("Codex.Cover.ReloadAnimSets"), TEXT("Re-reads Content/Data/Animation/cover_anim_sets.json (the next cover entry uses it)."),
		FConsoleCommandDelegate::CreateLambda([]() { UOperativeAnimInstance::ReloadCoverAnimSetConfig(); }));

	/** A native cover array and the set clip key base that replaces it (nullptr: an active set clears it - no M4 clip mixed into a set). */
	struct FCoverSlotMap
	{
		const TCHAR* Slot;
		const TCHAR* KeyBase;
	};
	const FCoverSlotMap GCoverSlots[] = {
		{ TEXT("CoverStandIdle"), TEXT("stand_idle") },
		{ TEXT("CoverCrouchIdle"), TEXT("crouch_idle") },
		{ TEXT("CoverStandCorner"), TEXT("stand_idle") }, // no look-round idle in the set: the plain idle
		{ TEXT("CoverCrouchCorner"), TEXT("crouch_idle") },
		// One walk per side, named by the direction it moves (like M4's suffix): forward and backward both index it by the direction.
		{ TEXT("CoverStandMoveForward"), TEXT("stand_walk") },
		{ TEXT("CoverStandMoveBackward"), TEXT("stand_walk") },
		{ TEXT("CoverCrouchMoveForward"), TEXT("crouch_walk") },
		{ TEXT("CoverCrouchMoveBackward"), TEXT("crouch_walk") },
		// No per-shot clip: the shot replays the aim idle (the fire stance holds; the recoil is the weapon's).
		{ TEXT("CoverStandFire"), TEXT("stand_fire_idle") },
		{ TEXT("CoverCrouchFire"), TEXT("crouch_fire_idle") },
		{ TEXT("CoverStandFireIdle"), TEXT("stand_fire_idle") },
		{ TEXT("CoverCrouchFireIdle"), TEXT("crouch_fire_idle") },
		{ TEXT("CoverStandFireEnter"), TEXT("stand_fire_enter") },
		{ TEXT("CoverCrouchFireEnter"), TEXT("crouch_fire_enter") },
		{ TEXT("CoverStandFireExit"), TEXT("stand_fire_exit") },
		{ TEXT("CoverCrouchFireExit"), TEXT("crouch_fire_exit") },
		// No stance-switch clips: the target stance's idle is the switch (a short blend).
		{ TEXT("CoverStandToCrouch"), TEXT("crouch_idle") },
		{ TEXT("CoverCrouchToStand"), TEXT("stand_idle") },
		{ TEXT("CoverBlindFire"), nullptr },
		{ TEXT("CoverStandEnter"), TEXT("stand_enter") },
		{ TEXT("CoverCrouchEnter"), TEXT("crouch_enter") },
		{ TEXT("CoverStandSwitchEnter"), nullptr },
		{ TEXT("CoverCrouchSwitchEnter"), nullptr },
		{ TEXT("CoverStandSwitchSprintEnter"), nullptr },
		{ TEXT("CoverCrouchSwitchSprintEnter"), nullptr },
	};
	/** Set clips with no native array (the states the M4 set has none for). */
	const TCHAR* GCoverExtras[] = {
		TEXT("stand_reload"), TEXT("crouch_reload"), TEXT("stand_grenade"), TEXT("crouch_grenade"),
		TEXT("stand_side_switch"), TEXT("crouch_side_switch"),
		TEXT("stand_walk_start"), TEXT("crouch_walk_start"), TEXT("stand_walk_stop"), TEXT("crouch_walk_stop"),
		TEXT("stand_walk_turn"), TEXT("crouch_walk_turn"),
		TEXT("crouch_fire_enter"), TEXT("crouch_fire_idle"), TEXT("crouch_fire_exit"),
		TEXT("crouch_over_fire_enter"), TEXT("crouch_over_fire_idle"), TEXT("crouch_over_fire_exit"),
		TEXT("stand_ao"), TEXT("crouch_ao"), TEXT("crouch_over_ao"),
	};

	bool IsCoverSetsOn(bool bClassDefault)
	{
		const int32 Mode = CVarCoverAnimSets.GetValueOnGameThread();
		return Mode == 0 ? false : Mode > 0 ? true : bClassDefault && CoverSetConfig().bEnabled;
	}

	FString SkeletonNameOf(const USkeletalMeshComponent* Mesh)
	{
		const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		const USkeleton* Skeleton = Asset ? Asset->GetSkeleton() : nullptr;
		return Skeleton ? Skeleton->GetName() : FString();
	}
}

void UOperativeAnimInstance::ReloadCoverAnimSetConfig()
{
	GCoverSetConfigLoaded = false;
	CoverSetConfig();
}

TArray<TObjectPtr<UAnimSequenceBase>>* UOperativeAnimInstance::FindCoverArray(FName Slot)
{
	if (FArrayProperty* Property = FindFProperty<FArrayProperty>(GetClass(), Slot))
	{
		return Property->ContainerPtrToValuePtr<TArray<TObjectPtr<UAnimSequenceBase>>>(this);
	}
	return nullptr;
}

void UOperativeAnimInstance::ResolveCoverAnimSets(TArray<FSoftObjectPath>* OutPaths)
{
	const FCoverAnimSetConfig& Config = CoverSetConfig();
	if (OutPaths)
	{
		for (const TPair<FString, FCoverAnimSetData>& Set : Config.Sets)
		{
			for (const TPair<FString, TMap<FString, FString>>& Body : Set.Value.Bodies)
			{
				for (const TPair<FString, FString>& Clip : Body.Value)
				{
					OutPaths->AddUnique(FSoftObjectPath(CoverAnimSetRules::ToObjectPath(Clip.Value)));
				}
			}
		}
		return;
	}
	if (!bUseCoverAnimSets || !bUseNativeCoverClips)
	{
		return;
	}
	const FString SkeletonName = SkeletonNameOf(GetSkelMeshComponent());
	LoadedCoverSets.Reset();
	for (const TPair<FString, FCoverAnimSetData>& Set : Config.Sets)
	{
		const TMap<FString, FString>* Clips = CoverAnimSetRules::FindBodyClips(Config, Set.Key, SkeletonName);
		if (!Clips)
		{
			continue;
		}
		FCoverAnimSetLoaded Loaded;
		Loaded.GrenadeReleaseSeconds = Set.Value.GrenadeReleaseSeconds;
		Loaded.ShimmyClipSpeed = Set.Value.ShimmyClipSpeed;
		int32 Found = 0;
		int32 Missing = 0;
		auto LoadPair = [&](const TCHAR* KeyBase, bool bBlendSpace) -> FCoverSetAssets
		{
			FCoverSetAssets Pair;
			Pair.Assets.SetNum(2);
			for (const ECoverFacing Facing : { ECoverFacing::Left, ECoverFacing::Right })
			{
				const FString* Entry = Clips->Find(CoverAnimSetRules::SideKey(KeyBase, Facing));
				if (!Entry)
				{
					continue;
				}
				const FString ObjectPath = CoverAnimSetRules::ToObjectPath(*Entry);
				const FString* Path = &ObjectPath;
				UObject* Asset = bBlendSpace ? static_cast<UObject*>(LoadObject<UBlendSpace>(nullptr, **Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
					: static_cast<UObject*>(ResolvePreparedAnimationAsset(*Path));
				Pair.Assets[CoverFacingRules::ClipIndex(Facing)] = Asset;
				Asset ? ++Found : ++Missing;
				if (!Asset)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("[CoverSet] %s: %s missing (%s) - the native clip of that slot stays"), *Set.Key, **Path,
						*CoverAnimSetRules::SideKey(KeyBase, Facing));
				}
			}
			return Pair;
		};
		for (const FCoverSlotMap& Map : GCoverSlots)
		{
			FCoverSetAssets Pair = Map.KeyBase ? LoadPair(Map.KeyBase, false) : FCoverSetAssets();
			Loaded.Slots.Add(Map.Slot, MoveTemp(Pair));
		}
		if (!CoverAnimSetRules::CanActivateSet(Found))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s: set '%s' for skeleton %s has no loaded cover slots; using native clips"),
				*GetNameSafe(GetOwningActor()), *Set.Key, *SkeletonName);
			continue;
		}
		for (const TCHAR* Extra : GCoverExtras)
		{
			Loaded.Extras.Add(Extra, LoadPair(Extra, FString(Extra).EndsWith(TEXT("_ao"))));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s: set '%s' for skeleton %s - %d assets, %d missing"), *GetNameSafe(GetOwningActor()), *Set.Key,
			*SkeletonName, Found, Missing);
		LoadedCoverSets.Add(Set.Key, MoveTemp(Loaded));
	}
}

void UOperativeAnimInstance::ChooseCoverAnimSet(const AOperativeCharacter& Operative)
{
	FString Wanted;
	CoverSetSlotPoint = Operative.GetCoverSlot().WallPoint;
	if (IsCoverSetsOn(bUseCoverAnimSets) && bUseNativeCoverClips)
	{
		FString Building;
		int32 Storey = INDEX_NONE;
		const URoofCutawaySubsystem* Cutaway = Operative.GetWorld() ? Operative.GetWorld()->GetSubsystem<URoofCutawaySubsystem>() : nullptr;
		const bool bInBuilding = Cutaway && Cutaway->GetOperativeStorey(Operative, Building, Storey);
		const ECoverAnimContext Context = CoverAnimSetRules::DecideContext(bInBuilding, Operative.IsInStealthMode());
		CoverAnimContextName = CoverAnimSetRules::ContextName(Context);
		Wanted = CoverAnimSetRules::ChooseSet(CoverSetConfig(), Context, { Operative.DisplayName.ToString(), Operative.GetName() },
			CVarRiffleOutdoor.GetValueOnGameThread() != 0);
		if (!LoadedCoverSets.Contains(Wanted))
		{
			Wanted.Reset(); // the set has no clips for this body
		}
		// A Shift-click look at the corner peek uses the CoverIndoor (riffle) poses even outdoors. A plain peek keeps
		// the set this entry already chose.
		if (Operative.WantsCornerLookPose() && LoadedCoverSets.Contains(TEXT("riffle")))
		{
			Wanted = TEXT("riffle");
		}
	}
	else
	{
		CoverAnimContextName = TEXT("off");
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s: cover at %s judged %s -> set '%s'"), *Operative.DisplayName.ToString(),
		*Operative.GetCoverSlot().WallPoint.ToCompactString(), *CoverAnimContextName, Wanted.IsEmpty() ? CoverAnimSetRules::NativeSetName : *Wanted);
	if (Wanted != ActiveCoverAnimSet || !bCoverNativeArraysCaptured)
	{
		ApplyCoverAnimSet(Wanted);
	}
}

void UOperativeAnimInstance::ApplyCoverAnimSet(const FString& Set)
{
	if (!bCoverNativeArraysCaptured)
	{
		bCoverNativeArraysCaptured = true;
		for (const FCoverSlotMap& Map : GCoverSlots)
		{
			if (const TArray<TObjectPtr<UAnimSequenceBase>>* Native = FindCoverArray(Map.Slot))
			{
				FCoverSetAssets& Copy = NativeCoverArrays.Add(Map.Slot);
				for (const TObjectPtr<UAnimSequenceBase>& Clip : *Native)
				{
					Copy.Assets.Add(Clip.Get());
				}
			}
		}
	}
	const FCoverAnimSetLoaded* Loaded = Set.IsEmpty() ? nullptr : LoadedCoverSets.Find(Set);
	for (const FCoverSlotMap& Map : GCoverSlots)
	{
		TArray<TObjectPtr<UAnimSequenceBase>>* Target = FindCoverArray(Map.Slot);
		const FCoverSetAssets* Native = NativeCoverArrays.Find(Map.Slot);
		if (!Target || !Native)
		{
			continue;
		}
		const int32 Size = Target->Num();
		const FCoverSetAssets* Override = Loaded ? Loaded->Slots.Find(Map.Slot) : nullptr;
		for (int32 Index = 0; Index < Size; ++Index)
		{
			UObject* Asset = nullptr;
			if (Override)
			{
				Asset = Override->Assets.IsValidIndex(Index) ? Override->Assets[Index].Get() : nullptr;
				if (!Asset && Map.KeyBase && Native->Assets.IsValidIndex(Index))
				{
					Asset = Native->Assets[Index].Get(); // a missing set clip keeps the native one
				}
			}
			else if (Native->Assets.IsValidIndex(Index))
			{
				Asset = Native->Assets[Index].Get();
			}
			(*Target)[Index] = Cast<UAnimSequenceBase>(Asset);
		}
	}
	ActiveCoverAnimSet = Loaded ? Set : FString();
	bCoverOverTopFireSet = false;
	ResetCoverOverTopSwitch();
}

const FCoverSetAssets* UOperativeAnimInstance::FindCoverSetExtra(const TCHAR* KeyBase) const
{
	const FCoverAnimSetLoaded* Loaded = ActiveCoverAnimSet.IsEmpty() ? nullptr : LoadedCoverSets.Find(ActiveCoverAnimSet);
	return Loaded ? Loaded->Extras.Find(KeyBase) : nullptr;
}

UAnimSequenceBase* UOperativeAnimInstance::PickCoverSetClip(const TCHAR* KeyBase, ECoverFacing Facing) const
{
	const FCoverSetAssets* Pair = FindCoverSetExtra(KeyBase);
	const int32 Index = CoverFacingRules::ClipIndex(Facing);
	return Pair && Pair->Assets.IsValidIndex(Index) ? Cast<UAnimSequenceBase>(Pair->Assets[Index].Get()) : nullptr;
}

bool UOperativeAnimInstance::HasCoverSetClip(const TCHAR* KeyBase, ECoverFacing Facing) const
{
	return PickCoverSetClip(KeyBase, Facing) != nullptr;
}

bool UOperativeAnimInstance::PlayCoverSetOneShot(UAnimSequenceBase* Clip, const TCHAR* Kind, float BlendIn, float BlendOut, float PlayRate)
{
	if (!PlayCoverOneShot(Clip, BlendIn, BlendOut))
	{
		return false;
	}
	if (UAnimMontage* Montage = CoverOneShotMontage.Get(); Montage && !FMath::IsNearlyEqual(PlayRate, 1.f))
	{
		Montage_SetPlayRate(Montage, PlayRate);
	}
	CoverSetOneShotKind = Kind;
	CoverSetOneShotMontage = CoverOneShotMontage;
	UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s: %s one-shot %s (rate %.2f)"), *GetNameSafe(GetOwningActor()), Kind, *Clip->GetName(), PlayRate);
	return true;
}

bool UOperativeAnimInstance::IsCoverSetOneShotPlaying()
{
	if (CoverSetOneShotKind.IsEmpty())
	{
		return false;
	}
	const UAnimMontage* Montage = CoverSetOneShotMontage.Get();
	if (Montage && Montage_IsPlaying(Montage) && CoverOneShotMontage.Get() == Montage)
	{
		return true;
	}
	CoverSetOneShotKind.Reset();
	CoverSetOneShotMontage.Reset();
	return false;
}

void UOperativeAnimInstance::TrackCoverSetChanges(const AOperativeCharacter& Operative)
{
	// Every frame in cover (before any early return of the layer): what changed since the last frame, kept until a state consumes it.
	const float ShimmyDir = Operative.ShimmyDirection;
	if (bShimmying && !bCoverSetWasShimmying)
	{
		bCoverSetShimmyStartPending = true;
		bCoverSetStopPlayed = false;
		bCoverSetLongWalk = false;
	}
	else if (bShimmying && CoverSetLastShimmyDirection != 0.f && ShimmyDir != 0.f && FMath::Sign(ShimmyDir) != FMath::Sign(CoverSetLastShimmyDirection))
	{
		bCoverSetShimmyReversePending = true;
		CoverSetReverseFromFacing = CoverSetLastFacing;
	}
	if (!bShimmying)
	{
		bCoverSetShimmyStartPending = false;
		bCoverSetShimmyReversePending = false;
		bCoverSetLongWalk = false;
	}
	if (CoverFacing != CoverSetLastFacing)
	{
		// A turn while standing at the wall (not the facing a shimmy order gave): the side switch.
		bCoverSetFacingChangePending = !bShimmying && !bCoverSetWasShimmying && !bCoverEnteredThisFrame;
	}
	if (bShimmying || bCoverEnteredThisFrame)
	{
		bCoverSetFacingChangePending = false;
	}
	bCoverSetWasShimmying = bShimmying;
	CoverSetLastShimmyDirection = bShimmying ? ShimmyDir : 0.f;
	CoverSetLastFacing = CoverFacing;
}

bool UOperativeAnimInstance::UpdateCoverSetStates(const AOperativeCharacter& Operative)
{
	if (ActiveCoverAnimSet.IsEmpty())
	{
		return false;
	}
	// The fire set: the over-the-top shot crouched at a low cover away from its corner (a window sill), else the side peek. Switched only
	// outside the fire pose, so a pose never changes under its own exit.
	if (!bCoverInFirePose && CoverOverTopPhase != ECoverOverTopPhase::SwitchingIn)
	{
		const bool bHasOver = HasCoverSetClip(TEXT("crouch_over_fire_idle"), CoverFacing)
			&& (CoverFacing != ECoverFacing::Right || HasCoverSetClip(TEXT("crouch_side_switch"), ECoverFacing::Left));
		const bool bOver = CoverAnimSetRules::DecideFireSet(bIsCrouching, CoverHeight, bCoverAtCorner, bHasOver, bCoverWindow) == ECoverFireSet::Over;
		if (bOver != bCoverOverTopFireSet)
		{
			bCoverOverTopFireSet = bOver;
			const TPair<const TCHAR*, const TCHAR*> Slots[] = {
				{ TEXT("CoverCrouchFireEnter"), TEXT("enter") }, { TEXT("CoverCrouchFireIdle"), TEXT("idle") },
				{ TEXT("CoverCrouchFireExit"), TEXT("exit") }, { TEXT("CoverCrouchFire"), TEXT("idle") } };
			for (const TPair<const TCHAR*, const TCHAR*>& Slot : Slots)
			{
				TArray<TObjectPtr<UAnimSequenceBase>>* Target = FindCoverArray(Slot.Key);
				const FCoverSetAssets* Native = NativeCoverArrays.Find(Slot.Key);
				const FString SideKey = FString(TEXT("crouch_fire_")) + Slot.Value;
				const FString OverKey = FString(TEXT("crouch_over_fire_")) + Slot.Value;
				for (const ECoverFacing Facing : { ECoverFacing::Left, ECoverFacing::Right })
				{
					const int32 Index = CoverFacingRules::ClipIndex(Facing);
					if (Target && Target->IsValidIndex(Index))
					{
						UAnimSequenceBase* NativeClip = Native && Native->Assets.IsValidIndex(Index)
							? Cast<UAnimSequenceBase>(Native->Assets[Index].Get()) : nullptr;
						(*Target)[Index] = CoverAnimSetRules::ResolveFireClip(bOver ? ECoverFireSet::Over : ECoverFireSet::Side,
							bOver ? PickCoverSetClip(*OverKey, Facing) : nullptr, PickCoverSetClip(*SideKey, Facing), NativeClip);
					}
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverSet] %s: fire set -> %s"), *Operative.DisplayName.ToString(), bOver ? TEXT("over the top") : TEXT("side peek"));
		}
	}
	if (IsCoverSetOneShotPlaying() || bCoverInFirePose || IsPlayingStanceTransition() || bIsReloading)
	{
		return false;
	}
	if (CoverOverTopPhase == ECoverOverTopPhase::Firing)
	{
		CoverOverTopPhase = CoverAnimSetRules::AdvanceOverTopPhase(CoverOverTopPhase, ECoverOverTopEvent::FireEnded);
		UAnimSequenceBase* Back = PickCoverSetClip(TEXT("crouch_side_switch"), ECoverFacing::Right);
		if (PlayCoverSetOneShot(Back, TEXT("over_switch_out"), 0.15f, 0.2f))
		{
			return true;
		}
	}
	CoverOverTopPhase = CoverAnimSetRules::AdvanceOverTopPhase(CoverOverTopPhase, ECoverOverTopEvent::SwitchOutFinished);
	if (CoverOverTopPhase == ECoverOverTopPhase::SwitchingIn)
	{
		return false; // fire-ready takes over from the completed switch-in
	}
	const TCHAR* StanceKey = bIsCrouching ? TEXT("crouch") : TEXT("stand");
	// The walk along the wall: start / turn / stop clips for walks of ShimmyTransitionMinCm and more (designer Q5).
	if (bShimmying)
	{
		const float Remaining = Operative.GetShimmyRemainingCm();
		const float MinCm = CoverSetConfig().ShimmyTransitionMinCm;
		if (bCoverSetShimmyStartPending)
		{
			bCoverSetShimmyStartPending = false;
			UAnimSequenceBase* Start = PickCoverSetClip(*FString::Printf(TEXT("%s_walk_start"), StanceKey), CoverFacing);
			bCoverSetLongWalk = CoverAnimSetRules::UseShimmyTransitions(Remaining, MinCm, Start != nullptr);
			if (bCoverSetLongWalk && PlayCoverSetOneShot(Start, TEXT("walk_start"), 0.15f, 0.2f))
			{
				return true;
			}
		}
		if (bCoverSetShimmyReversePending)
		{
			// Reversed while walking: the turn clip of the side he was walking to.
			bCoverSetShimmyReversePending = false;
			UAnimSequenceBase* Turn = PickCoverSetClip(*FString::Printf(TEXT("%s_walk_turn"), StanceKey), CoverSetReverseFromFacing);
			bCoverSetLongWalk = CoverAnimSetRules::UseShimmyTransitions(Remaining, MinCm, Turn != nullptr);
			bCoverSetStopPlayed = false;
			if (bCoverSetLongWalk && PlayCoverSetOneShot(Turn, TEXT("walk_turn"), 0.15f, 0.2f))
			{
				return true;
			}
		}
		if (bCoverSetLongWalk && !bCoverSetStopPlayed)
		{
			UAnimSequenceBase* Stop = PickCoverSetClip(*FString::Printf(TEXT("%s_walk_stop"), StanceKey), CoverFacing);
			if (Stop && CoverAnimSetRules::ShouldStartShimmyStop(Remaining, Speed, Stop->GetPlayLength()))
			{
				bCoverSetStopPlayed = true;
				if (PlayCoverSetOneShot(Stop, TEXT("walk_stop"), 0.15f, 0.25f))
				{
					return true;
				}
			}
		}
		return false;
	}
	// Facing turned while standing still at the wall (a threat on the other side, an order): the set's side switch.
	if (bCoverSetFacingChangePending)
	{
		bCoverSetFacingChangePending = false;
		if (UAnimSequenceBase* Switch = PickCoverSetClip(*FString::Printf(TEXT("%s_side_switch"), StanceKey), CoverFacing))
		{
			return PlayCoverSetOneShot(Switch, TEXT("side_switch"), 0.15f, 0.2f);
		}
	}
	return false;
}

bool UOperativeAnimInstance::TryCoverSetOverSideSwitch()
{
	if (CoverOverTopPhase == ECoverOverTopPhase::SwitchingIn || CoverOverTopPhase == ECoverOverTopPhase::SwitchingOut)
	{
		return IsCoverSetOneShotPlaying();
	}
	if (CoverOverTopPhase != ECoverOverTopPhase::Idle || ActiveCoverAnimSet.IsEmpty()
		|| !bCoverOverTopFireSet || !bIsCrouching || bShimmying || CoverFacing != ECoverFacing::Right)
	{
		return false;
	}
	UAnimSequenceBase* Switch = PickCoverSetClip(TEXT("crouch_side_switch"), ECoverFacing::Left); // the pack's CrouchR_To_L
	if (!Switch || !PlayCoverSetOneShot(Switch, TEXT("over_switch_in"), 0.15f, 0.2f))
	{
		return false;
	}
	CoverOverTopPhase = CoverAnimSetRules::AdvanceOverTopPhase(CoverOverTopPhase, ECoverOverTopEvent::SwitchInStarted);
	return true;
}

void UOperativeAnimInstance::ResetCoverOverTopSwitch()
{
	bCoverOverTopShotPending = false;
	if (CoverOverTopPhase == ECoverOverTopPhase::Idle)
	{
		return;
	}
	if (UAnimMontage* Switch = CoverSetOneShotMontage.Get(); CoverSetOneShotKind.StartsWith(TEXT("over_switch_"))
		&& Switch && CoverOneShotMontage.Get() == Switch)
	{
		Montage_Stop(0.2f, Switch);
		CoverOneShotMontage.Reset();
		CoverSetOneShotMontage.Reset();
		CoverSetOneShotKind.Reset();
	}
	CoverOverTopPhase = CoverAnimSetRules::AdvanceOverTopPhase(CoverOverTopPhase, ECoverOverTopEvent::Cancelled);
	CoverPendingFireClip.Reset();
}

bool UOperativeAnimInstance::IsCoverSetWalkOneShotPlaying() const
{
	const UAnimMontage* Montage = CoverSetOneShotMontage.Get();
	return CoverSetOneShotKind.StartsWith(TEXT("walk_")) && Montage && CoverOneShotMontage.Get() == Montage && Montage_IsPlaying(Montage);
}

float UOperativeAnimInstance::GetCoverSetShimmyClipSpeed() const
{
	const FCoverAnimSetLoaded* Loaded = ActiveCoverAnimSet.IsEmpty() ? nullptr : LoadedCoverSets.Find(ActiveCoverAnimSet);
	const float* Pace = Loaded ? Loaded->ShimmyClipSpeed.Find(bIsCrouching ? TEXT("crouch") : TEXT("stand")) : nullptr;
	return Pace ? *Pace : CoverShimmyClipSpeed;
}

bool UOperativeAnimInstance::WantsCoverSetReload() const
{
	return bInCover && !bIsProne && !bShimmying && !ActiveCoverAnimSet.IsEmpty()
		&& HasCoverSetClip(bIsCrouching ? TEXT("crouch_reload") : TEXT("stand_reload"), CoverFacing);
}

bool UOperativeAnimInstance::UpdateCoverSetReload(const AOperativeCharacter& Operative)
{
	if (!bIsReloading)
	{
		bCoverSetReloadPlaying = false;
		return false;
	}
	if (ActiveCoverAnimSet.IsEmpty() || bShimmying)
	{
		return false;
	}
	if (bCoverSetReloadPlaying)
	{
		return true; // the clip runs out with the reload (its rate fits the reload time)
	}
	UAnimSequenceBase* Clip = PickCoverSetClip(bIsCrouching ? TEXT("crouch_reload") : TEXT("stand_reload"), CoverFacing);
	if (!Clip)
	{
		return false;
	}
	const float Duration = FMath::Max(0.1f, Operative.ReloadTimer);
	bCoverSetReloadPlaying = PlayCoverSetOneShot(Clip, TEXT("reload"), 0.2f, 0.2f, Clip->GetPlayLength() / Duration);
	return bCoverSetReloadPlaying;
}

bool UOperativeAnimInstance::PlayCoverSetGrenadeThrow(AOperativeCharacter& Operative)
{
	if (!bInCover || ActiveCoverAnimSet.IsEmpty() || bIsProne || bShimmying)
	{
		return false;
	}
	const TCHAR* StanceKey = bIsCrouching ? TEXT("crouch") : TEXT("stand");
	UAnimSequenceBase* Clip = PickCoverSetClip(*FString::Printf(TEXT("%s_grenade"), StanceKey), CoverFacing);
	if (!Clip)
	{
		return false;
	}
	const FCoverAnimSetLoaded* Loaded = LoadedCoverSets.Find(ActiveCoverAnimSet);
	const FString ReleaseKey = CoverAnimSetRules::SideKey(StanceKey, CoverFacing);
	const float* Release = Loaded ? Loaded->GrenadeReleaseSeconds.Find(ReleaseKey) : nullptr;
	ResetCoverOverTopSwitch();
	bCoverInFirePose = false;
	CoverPendingFireClip.Reset();
	if (!PlayCoverSetOneShot(Clip, TEXT("grenade"), 0.15f, 0.2f))
	{
		return false;
	}
	Operative.GrenadeThrowDuration = Clip->GetPlayLength();
	Operative.GrenadeReleaseSeconds = Release ? FMath::Min(*Release, Clip->GetPlayLength()) : Clip->GetPlayLength() * 0.6f;
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Clip->GetPlayLength());
	GrenadeThrowSecondsLeft = Clip->GetPlayLength() - 0.2f;
	return true;
}

void UOperativeAnimInstance::UpdateCoverSetAimOffset()
{
	// The wired pin drives a cover set's own aim offset in any fire stance. The corner look pose does the same for
	// CoverIndoor once that pose is on, whether or not the designer has ticked the pin for every cover fire.
	if ((!bCoverSetAimOffsetWired && !bCornerLookPoseApplied) || ActiveCoverAnimSet.IsEmpty() || !bInCover
		|| (!bCoverInFirePose && !bCornerLookPoseApplied))
	{
		return;
	}
	const TCHAR* Key = !bIsCrouching ? TEXT("stand_ao") : bCoverOverTopFireSet ? TEXT("crouch_over_ao") : TEXT("crouch_ao");
	const FCoverSetAssets* Pair = FindCoverSetExtra(Key);
	const int32 Index = CoverFacingRules::ClipIndex(bCoverInFirePose ? CoverFirePoseFacing : CoverFacing);
	if (UBlendSpace* Offset = Pair && Pair->Assets.IsValidIndex(Index) ? Cast<UBlendSpace>(Pair->Assets[Index].Get()) : nullptr)
	{
		ActiveAimOffset = Offset;
	}
}
