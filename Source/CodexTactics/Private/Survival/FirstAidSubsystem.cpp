#include "Survival/FirstAidSubsystem.h"

#include "Animation/AnimSequenceBase.h"
#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/WarmIdleRules.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace FirstAidSubsystemLocal
{

	const TCHAR* const ClipRoot = TEXT("/Game/Characters/Animations/FirstAid");
	/** "Folder/Name" clip names (R17-6: the CON_Injured / crawl copies other setup scripts made) live under this root. */
	const TCHAR* const AnimRoot = TEXT("/Game/Characters/Animations");
	constexpr int32 MaxClipRestarts = 3;
	constexpr int32 MaxRecords = 64;
	/** The helper counts as arrived once his own walk ended within reach (or he is this close). */
	constexpr float ArrivedSlackCm = 25.f;

	bool UsesFemaleBody(const AOperativeCharacter& Operative)
	{
		const USkeletalMesh* Mesh = Operative.GetMesh() ? Operative.GetMesh()->GetSkeletalMeshAsset() : nullptr;
		const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
		return WarmIdleRules::UsesFemaleClips(Mesh ? Mesh->GetName() : FString(), Skeleton ? Skeleton->GetName() : FString());
	}

	FString ClipPath(bool bFemale, const FString& Name)
	{
		const TCHAR* const Body = bFemale ? TEXT("Female") : TEXT("Male");
		FString Folder;
		FString Clip;
		if (Name.Split(TEXT("/"), &Folder, &Clip, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && !Folder.IsEmpty() && !Clip.IsEmpty())
		{
			return FString::Printf(TEXT("%s/%s/%s/%s.%s"), AnimRoot, *Folder, Body, *Clip, *Clip);
		}
		return FString::Printf(TEXT("%s/%s/%s.%s"), ClipRoot, Body, *Name, *Name);
	}

	FString ItemName(EFirstAidItem Item)
	{
		return Item == EFirstAidItem::MedicalBag ? TEXT("medical bag") : TEXT("medkit");
	}

	float HealthOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent ? Operative->HealthComponent->GetCurrentHealth() : 0.f;
	}

	bool IsAliveOperative(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKilledInAction();
	}
}

bool UFirstAidSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFirstAidSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	HelperStream.Initialize(FMath::Rand());
	PatientStream.Initialize(FMath::Rand());
}

void UFirstAidSubsystem::Deinitialize()
{
	Treatments.Reset();
	Records.Reset();
	Settles.Reset();
	Super::Deinitialize();
}

void UFirstAidSubsystem::SetPickSeed(int32 Seed)
{
	PatientStream.Initialize(Seed);
	HelperStream.Initialize(static_cast<int32>(HashCombine(GetTypeHash(Seed), 0x9E3779B9u)));
	LastClipIndex.Reset();
}

bool UFirstAidSubsystem::IsSettling(const AOperativeCharacter* Operative) const
{
	return Operative && Settles.ContainsByPredicate([Operative](const FSettle& Each) { return Each.Patient.Get() == Operative; });
}

FirstAidRules::EPatientPose UFirstAidSubsystem::PoseOf(const AOperativeCharacter& Patient)
{
	// On all fours: the critical wound always (it forces prone + the injured crawl), or the injured crawl layer drives him now (wounded, prone).
	const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(&Patient);
	const bool bOnAllFours = Patient.IsCriticallyWounded() || (Anim && (Anim->IsInjuredCrawlActive() || Anim->IsInjuredCrawlIdleActive()));
	return FirstAidRules::ChoosePatientPose(Patient.IsKnockedDown(), Patient.GetStance() == EOperativeStance::Prone, bOnAllFours,
		Patient.GetStance() == EOperativeStance::Crouching);
}

void UFirstAidSubsystem::HoldBodyHeading(AOperativeCharacter& Patient, float BaseYaw, const FirstAidRules::FPatientClipPlay& Play, float DeltaTime)
{
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(&Patient);
	if (!Config.Patient.bHoldBodyHeading || !Anim || FMath::IsNearlyZero(Play.LongAxisDeg, 0.5f))
	{
		return;
	}
	// The clip's lying body points LongAxisDeg off his forward: the capsule turns the other way by the body's turn at the clip's blend weight (the
	// measured curve), so the body stays.
	const float Now = static_cast<float>(Patient.GetActorRotation().Yaw);
	const float Target = FirstAidRules::CompensatedYaw(BaseYaw, FirstAidRules::BodyTurnAt(Play.LongAxisCurve, Play.LongAxisDeg, Anim->GetFirstAidClipWeight()), 1.f);
	const float Yaw = FirstAidRules::TurnToward(Now, Target, Config.Patient.CounterTurnMaxDegPerSecond, DeltaTime);
	if (!FMath::IsNearlyZero(FRotator3f::NormalizeAxis(Yaw - Now), 0.01f))
	{
		Patient.SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
}

TStatId UFirstAidSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFirstAidSubsystem, STATGROUP_Tickables);
}

UFirstAidSubsystem* UFirstAidSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFirstAidSubsystem>() : nullptr;
}

FirstAidRules::ETier UFirstAidSubsystem::TierOf(const AOperativeCharacter& Operative)
{
	return FirstAidRules::FromWoundTier(Operative.GetWoundTier());
}

FirstAidRules::FTierBounds UFirstAidSubsystem::BoundsOf(const AOperativeCharacter& Operative)
{
	const CriticalWoundRules::FCriticalWoundConfig& Wound = CriticalWoundRules::GetWoundConfig();
	FirstAidRules::FTierBounds Bounds;
	Bounds.LightFrom = Operative.MovementConfig.WoundedHealthThreshold;
	Bounds.MediumFrom = Wound.HeavyFrom;
	Bounds.HeavyFrom = Wound.CriticalFrom;
	return Bounds;
}

int32 UFirstAidSubsystem::CountOf(const AOperativeCharacter& Operative, EFirstAidItem Item)
{
	return Item == EFirstAidItem::MedicalBag ? Operative.MedicalBagsCount : Operative.MedkitsCount;
}

UAnimSequenceBase* UFirstAidSubsystem::ResolveClip(const AOperativeCharacter& Operative, const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}
	const FSoftObjectPath Path(FirstAidSubsystemLocal::ClipPath(FirstAidSubsystemLocal::UsesFemaleBody(Operative), Name));
	if (UAnimSequenceBase* Resident = Cast<UAnimSequenceBase>(Path.ResolveObject()))
	{
		return Resident;
	}
	return LoadObject<UAnimSequenceBase>(nullptr, *Path.ToString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void UFirstAidSubsystem::GatherClipPaths(TArray<FSoftObjectPath>& OutPaths)
{
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	for (const FirstAidRules::FItemConfig* Item : { &Config.Medkit, &Config.MedicalBag })
	{
		for (const TArray<FString>* Set : { &Item->SelfStandingClips, &Item->SelfKneelingClips, &Item->SelfLyingClips, &Item->HelperClips, &Item->PatientClips })
		{
			for (const FString& Name : *Set)
			{
				OutPaths.AddUnique(FSoftObjectPath(FirstAidSubsystemLocal::ClipPath(false, Name)));
				OutPaths.AddUnique(FSoftObjectPath(FirstAidSubsystemLocal::ClipPath(true, Name)));
			}
		}
	}
	for (const FirstAidRules::FPatientPoseConfig& Pose : Config.Patient.Poses)
	{
		for (const FString& Name : Pose.Clips)
		{
			OutPaths.AddUnique(FSoftObjectPath(FirstAidSubsystemLocal::ClipPath(false, Name)));
			OutPaths.AddUnique(FSoftObjectPath(FirstAidSubsystemLocal::ClipPath(true, Name)));
		}
	}
}

bool UFirstAidSubsystem::IsInTreatment(const AOperativeCharacter* Operative) const
{
	return Operative && Treatments.ContainsByPredicate([Operative](const FTreatment& Each)
	{
		return Each.Helper.Get() == Operative || Each.Patient.Get() == Operative;
	});
}

const FFirstAidRecord* UFirstAidSubsystem::FindRunning(const AOperativeCharacter* Operative) const
{
	const FTreatment* Found = Operative ? Treatments.FindByPredicate([Operative](const FTreatment& Each)
	{
		return Each.Helper.Get() == Operative || Each.Patient.Get() == Operative;
	}) : nullptr;
	return Found ? &Found->Record : nullptr;
}

bool UFirstAidSubsystem::IsBusy(const AOperativeCharacter& Operative) const
{
	// Designer 2026-11-26: being in cover (hugging a wall / behind a low cover) no longer blocks first aid - the player's order takes him
	// off the wall (RequestSelf / RequestAid leave the cover once the request is accepted).
	return Operative.IsKnockedDown() || Operative.IsClimbingLadder() || Operative.IsVaulting() || Operative.IsMantling() || Operative.IsRagdollActive()
		|| Operative.bCarrying || IsInTreatment(&Operative);
}

void UFirstAidSubsystem::Post(const AOperativeCharacter* Speaker, const FString& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
	{
		Messages->PostMessage(Speaker ? Speaker->DisplayName : FText::FromString(TEXT("First aid")), FText::FromString(Text));
	}
}

FirstAidRules::ERefusal UFirstAidSubsystem::Refuse(AOperativeCharacter* Who, FirstAidRules::ERefusal Reason, const AOperativeCharacter* Patient,
	EFirstAidItem Item, bool bPostFeed) const
{
	const FString WhoName = Who ? Who->DisplayName.ToString() : TEXT("?");
	const FString PatientName = Patient ? Patient->DisplayName.ToString() : WhoName;
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s -> %s: %s refused (%s)"), *WhoName, *PatientName, FirstAidRules::ItemKey(Item), FirstAidRules::RefusalName(Reason));
	if (!bPostFeed)
	{
		return Reason;
	}
	switch (Reason)
	{
	case FirstAidRules::ERefusal::NoItem:
		Post(Who, FString::Printf(TEXT("%s has no %s in personal inventory!"), *WhoName, *FirstAidSubsystemLocal::ItemName(Item)));
		break;
	case FirstAidRules::ERefusal::FullHealth:
		Post(Who, FString::Printf(TEXT("%s is not hurt - no treatment needed."), *PatientName));
		break;
	case FirstAidRules::ERefusal::CannotTreatSelf:
		Post(Who, FString::Printf(TEXT("%s is too badly wounded to treat himself - drag a medkit or the medical bag onto him from a mate's inventory."),
			*WhoName));
		break;
	case FirstAidRules::ERefusal::TierNotAllowed:
		Post(Who, FString::Printf(TEXT("The %s cannot treat this wound."), *FirstAidSubsystemLocal::ItemName(Item)));
		break;
	case FirstAidRules::ERefusal::HelperUnfit:
		Post(Who, FString::Printf(TEXT("%s cannot help - he needs help himself."), *WhoName));
		break;
	case FirstAidRules::ERefusal::Busy:
		Post(Who, FString::Printf(TEXT("%s is busy (ladder, climbing, carrying or another treatment)."), *WhoName));
		break;
	default:
		break;
	}
	return Reason;
}

FirstAidRules::ERefusal UFirstAidSubsystem::RequestSelf(AOperativeCharacter* Operative, EFirstAidItem Item, bool bPostFeed)
{
	if (!IsValid(Operative) || !Operative->HealthComponent)
	{
		return FirstAidRules::ERefusal::Dead;
	}
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	const FirstAidRules::ETier Tier = TierOf(*Operative);
	const bool bProne = Operative->GetStance() == EOperativeStance::Prone;
	const FirstAidRules::ERefusal Reason = FirstAidRules::CheckSelf(Config, Item, Tier, Operative->HealthComponent->GetCurrentHealth(), Operative->HealthComponent->GetMaxHealth(),
		CountOf(*Operative, Item), IsBusy(*Operative), bProne);
	if (Reason != FirstAidRules::ERefusal::None)
	{
		return Refuse(Operative, Reason, Operative, Item, bPostFeed);
	}
	if (Operative->bInCover)
	{
		Operative->LeaveCover(TEXT("first aid"));
	}
	FTreatment& Treatment = Treatments.AddDefaulted_GetRef();
	Treatment.Helper = Operative;
	Treatment.Patient = Operative;
	Treatment.Item = Item;
	Treatment.Tier = Tier;
	Treatment.Mode = FirstAidRules::ChooseSelfMode(Tier, bProne);
	Treatment.Clock.Seconds = FirstAidRules::TreatmentSeconds(Config, Item, Tier, true);
	Treatment.Clock.ApplyDelay = FirstAidRules::ApplyDelay(Treatment.Clock.Seconds, Config.Item(Item).ApplyAt);
	Treatment.Record.Helper = Treatment.Record.Patient = Operative->DisplayName.ToString();
	Treatment.Record.Item = Item;
	Treatment.Record.Mode = Treatment.Mode;
	Treatment.Record.TierBefore = Treatment.Record.TierAfter = Tier;
	Treatment.Record.Seconds = Treatment.Clock.Seconds;
	Treatment.Record.HealthBefore = Treatment.Record.HealthAfter = Operative->HealthComponent->GetCurrentHealth();
	BeginTreating(Treatment);
	Post(Operative, FString::Printf(TEXT("%s treats himself (%s, %.0f s)..."), *Operative->DisplayName.ToString(), *FirstAidSubsystemLocal::ItemName(Item),
		Treatment.Clock.Seconds));
	return FirstAidRules::ERefusal::None;
}

FirstAidRules::ERefusal UFirstAidSubsystem::RequestAid(AOperativeCharacter* Helper, AOperativeCharacter* Patient, EFirstAidItem Item, bool bPostFeed)
{
	if (!IsValid(Helper) || !IsValid(Patient) || !Helper->HealthComponent || !Patient->HealthComponent)
	{
		return FirstAidRules::ERefusal::Dead;
	}
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	const FirstAidRules::ETier PatientTier = TierOf(*Patient);
	const FirstAidRules::ERefusal Reason = FirstAidRules::CheckAid(Config, Item, Helper == Patient, FirstAidSubsystemLocal::IsAliveOperative(Helper), Helper->IsKnockedDown(),
		TierOf(*Helper), CountOf(*Helper, Item), IsBusy(*Helper), FirstAidSubsystemLocal::IsAliveOperative(Patient), PatientTier,
		Patient->HealthComponent->GetCurrentHealth(), Patient->HealthComponent->GetMaxHealth(), IsInTreatment(Patient) || Patient->IsClimbingLadder());
	if (Reason != FirstAidRules::ERefusal::None)
	{
		return Refuse(Helper, Reason, Patient, Item, bPostFeed);
	}
	if (Helper->bInCover)
	{
		Helper->LeaveCover(TEXT("first aid"));
	}
	const float Distance = FVector::Dist2D(Helper->GetActorLocation(), Patient->GetActorLocation());
	FTreatment Treatment;
	Treatment.Helper = Helper;
	Treatment.Patient = Patient;
	Treatment.Item = Item;
	Treatment.Tier = PatientTier;
	Treatment.Mode = FirstAidRules::EMode::Aid;
	Treatment.Clock.Seconds = FirstAidRules::TreatmentSeconds(Config, Item, PatientTier, false);
	Treatment.Clock.ApplyDelay = FirstAidRules::ApplyDelay(Treatment.Clock.Seconds, Config.Item(Item).ApplyAt);
	Treatment.Record.Helper = Helper->DisplayName.ToString();
	Treatment.Record.Patient = Patient->DisplayName.ToString();
	Treatment.Record.Item = Item;
	Treatment.Record.Mode = FirstAidRules::EMode::Aid;
	Treatment.Record.TierBefore = Treatment.Record.TierAfter = PatientTier;
	Treatment.Record.Seconds = Treatment.Clock.Seconds;
	Treatment.Record.HealthBefore = Treatment.Record.HealthAfter = Patient->HealthComponent->GetCurrentHealth();
	Treatment.HelperHealth = Helper->HealthComponent->GetCurrentHealth();
	Treatment.PatientHealth = Patient->HealthComponent->GetCurrentHealth();
	Treatment.PatientOrderSerial = Patient->GetMoveOrderSerial();
	Treatment.PatientStance = static_cast<uint8>(Patient->GetStance());
	if (Distance > Config.ReachCm)
	{
		// Walk up beside him: a lying patient at his side level with his chest (the side the helper comes from), else on the helper's side.
		const FVector Goal = FirstAidRules::HelperGoal(Config, FirstAidRules::IsLyingPose(PoseOf(*Patient)), Patient->GetActorLocation(),
			Patient->GetActorForwardVector(), Helper->GetActorLocation());
		if (Helper->OrderMoveTo(Goal, false) != EOperativeOrderResult::Accepted)
		{
			if (bPostFeed)
			{
				Post(Helper, FString::Printf(TEXT("%s cannot reach %s."), *Helper->DisplayName.ToString(), *Patient->DisplayName.ToString()));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s -> %s: no path to him"), *Helper->DisplayName.ToString(), *Patient->DisplayName.ToString());
			return FirstAidRules::ERefusal::Busy;
		}
		Treatment.bApproaching = true;
		Treatment.bHasApproachGoal = true;
		Treatment.ApproachGoal = Goal;
		Treatment.Record.bApproached = true;
		Treatment.HelperOrderSerial = Helper->GetMoveOrderSerial();
		Treatment.HelperStance = static_cast<uint8>(Helper->GetStance());
		Treatments.Add(MoveTemp(Treatment));
	}
	else if (Patient->IsKnockedDown())
	{
		// In reach, but he is still down in a knockdown (it owns the FullBody slot): wait until he lies still / is up (Tick), then treat.
		Treatment.bApproaching = true;
		Treatment.Record.bWaitedForKnockdown = true;
		Helper->StopOperative();
		Treatment.HelperOrderSerial = Helper->GetMoveOrderSerial();
		Treatment.HelperStance = static_cast<uint8>(Helper->GetStance());
		Treatments.Add(MoveTemp(Treatment));
	}
	else
	{
		BeginTreating(Treatments.Add_GetRef(MoveTemp(Treatment)));
	}
	Post(Helper, FString::Printf(TEXT("%s gives %s first aid (%s, %.0f s)..."), *Helper->DisplayName.ToString(), *Patient->DisplayName.ToString(),
		*FirstAidSubsystemLocal::ItemName(Item), Treatments.Last().Clock.Seconds));
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s -> %s: %s, tier %s, %.1f s (apply at %.1f s), %s"), *Helper->DisplayName.ToString(),
		*Patient->DisplayName.ToString(), FirstAidRules::ItemKey(Item), FirstAidRules::TierKey(PatientTier), Treatments.Last().Clock.Seconds, Treatments.Last().Clock.ApplyDelay,
		Treatments.Last().bApproaching ? TEXT("walking over") : TEXT("in reach"));
	return FirstAidRules::ERefusal::None;
}

bool UFirstAidSubsystem::ShouldTreatOnDrop(const AOperativeCharacter* Patient, bool bHandOverOnly) const
{
	const UHealthComponent* Health = Patient ? Patient->HealthComponent.Get() : nullptr;
	return Health && FirstAidRules::TreatsOnDrop(FirstAidRules::GetConfig(), TierOf(*Patient), bHandOverOnly, Health->GetCurrentHealth(),
		Health->GetMaxHealth());
}

bool UFirstAidSubsystem::Cancel(const AOperativeCharacter* Operative, FirstAidRules::EInterrupt Reason)
{
	for (int32 Index = Treatments.Num() - 1; Index >= 0; --Index)
	{
		if (Treatments[Index].Helper.Get() == Operative || Treatments[Index].Patient.Get() == Operative)
		{
			Finish(Index, Reason == FirstAidRules::EInterrupt::None ? FirstAidRules::EInterrupt::Order : Reason);
			return true;
		}
	}
	return false;
}

bool UFirstAidSubsystem::IsActivelyTreating(const AOperativeCharacter* Operative) const
{
	if (!Operative)
	{
		return false;
	}
	for (const FTreatment& Treatment : Treatments)
	{
		if (!Treatment.bApproaching && (Treatment.Helper.Get() == Operative || Treatment.Patient.Get() == Operative))
		{
			return true;
		}
	}
	return false;
}

void UFirstAidSubsystem::BeginTreating(FTreatment& Treatment)
{
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	if (!Helper || !Patient)
	{
		return;
	}
	Treatment.bApproaching = false;
	Settles.RemoveAll([Helper, Patient](const FSettle& Each) { return Each.Patient.Get() == Helper || Each.Patient.Get() == Patient; });
	if (Helper == Patient)
	{
		Helper->StopOperative();
	}
	else
	{
		const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
		Patient->StopOperative();
		Treatment.PatientPose = PoseOf(*Patient);
		Treatment.Record.PatientPose = Treatment.PatientPose;
		Treatment.PatientBaseYaw = static_cast<float>(Patient->GetActorRotation().Yaw);
		// He faces the lying patient's chest (else the patient): turned at helper_turn_deg_s in Tick (0 = at once, the old snap).
		const FVector Face = FirstAidRules::HelperFacePoint(Config, FirstAidRules::IsLyingPose(Treatment.PatientPose), Patient->GetActorLocation(),
			Patient->GetActorForwardVector());
		Treatment.HelperFaceYaw = FirstAidRules::HelperFaceYaw(Config, FirstAidRules::IsLyingPose(Treatment.PatientPose), Patient->GetActorLocation(),
			Treatment.PatientBaseYaw, Helper->GetActorLocation(), static_cast<float>(Helper->GetActorRotation().Yaw));
		if (Config.Patient.HelperTurnDegPerSecond > 0.f)
		{
			Helper->SetSprinting(false);
			Helper->StopOperative();
			Treatment.bHelperFacing = true;
			Treatment.bWaitingToFace = !FirstAidRules::IsHelperFacingReady(static_cast<float>(Helper->GetActorRotation().Yaw), Treatment.HelperFaceYaw);
		}
		else
		{
			Helper->StopAndFace(Face);
			Treatment.bHelperFacing = true;
			Treatment.bWaitingToFace = false;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s: the patient lies '%s' (yaw %.0f), the helper turns to %.0f"), *Patient->DisplayName.ToString(),
			FirstAidRules::PatientPoseKey(Treatment.PatientPose), Treatment.PatientBaseYaw, Treatment.HelperFaceYaw);
	}
	Treatment.HelperSpot = Helper->GetActorLocation();
	Treatment.PatientSpot = Patient->GetActorLocation();
	Treatment.HelperOrderSerial = Helper->GetMoveOrderSerial();
	Treatment.PatientOrderSerial = Patient->GetMoveOrderSerial();
	Treatment.HelperStance = static_cast<uint8>(Helper->GetStance());
	Treatment.PatientStance = static_cast<uint8>(Patient->GetStance());
	Treatment.HelperHealth = FirstAidSubsystemLocal::HealthOf(Helper);
	Treatment.PatientHealth = FirstAidSubsystemLocal::HealthOf(Patient);
	if (!Treatment.bWaitingToFace)
	{
		PlayClips(Treatment, false);
	}
}

void UFirstAidSubsystem::PlayClips(FTreatment& Treatment, bool bRestart, bool bHelperSide, bool bPatientSide)
{
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	const FirstAidRules::FItemConfig& Data = Config.Item(Treatment.Item);
	auto PlayOn = [this, &Treatment, &Config, &Data, bRestart](AOperativeCharacter* Who, bool bPatient, TWeakObjectPtr<UAnimSequenceBase>& Clip, FString& RecordName)
	{
		// The patient's set by the pose he lies in (R17-6), the helper's / self sets by mode.
		const TArray<FString>& Set = bPatient ? FirstAidRules::PatientClipSet(Config, Data, Treatment.PatientPose) : FirstAidRules::ClipSet(Data, Treatment.Mode, false);
		FRandomStream& Stream = bPatient ? PatientStream : HelperStream;
		UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Who);
		if (!Who || !Anim)
		{
			return;
		}
		if (!bRestart || !Clip.IsValid())
		{
			int32& Last = LastClipIndex.FindOrAdd(&Set, INDEX_NONE);
			const int32 First = FirstAidRules::PickClipIndex(Set.Num(), Stream.FRand(), Last);
			Clip.Reset();
			RecordName.Reset();
			// The picked clip, else the next one that exists for this body (a copy not retargeted yet is skipped).
			for (int32 Offset = 0; First != INDEX_NONE && Offset < Set.Num() && !Clip.IsValid(); ++Offset)
			{
				const int32 Index = (First + Offset) % Set.Num();
				if (UAnimSequenceBase* Found = ResolveClip(*Who, Set[Index]))
				{
					Clip = Found;
					RecordName = Set[Index];
					Last = Index;
				}
			}
			if (!Clip.IsValid())
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[FirstAid] %s: no %s clip for '%s' (%d names; run Scripts/Editor/setup_first_aid_animation.py)"),
					*Who->DisplayName.ToString(), bPatient ? TEXT("patient") : TEXT("treatment"),
					bPatient ? FirstAidRules::PatientPoseKey(Treatment.PatientPose) : FirstAidRules::ModeName(Treatment.Mode), Set.Num());
				return;
			}
			if (bPatient)
			{
				Treatment.PatientPlay = FirstAidRules::PatientClipPlay(Config, Treatment.PatientPose, RecordName);
			}
		}
		// A restart picks up where the treatment is (the same rate / loops, fitted to the whole treatment).
		const FirstAidRules::FPatientClipPlay& Lying = Treatment.PatientPlay;
		Anim->PlayFirstAidClip(Clip.Get(), Treatment.Clock.Seconds, bPatient ? Lying.MinRate : Config.MinPlayRate, bPatient ? Lying.MaxRate : Config.MaxPlayRate,
			bPatient ? Lying.BlendInSeconds : Config.BlendInSeconds, bPatient ? Lying.BlendOutSeconds : Config.BlendOutSeconds,
			bRestart ? Treatment.Clock.Elapsed : 0.f, Config.bHideWeapon);
	};
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	if (bHelperSide)
	{
		PlayOn(Helper, false, Treatment.HelperClip, Treatment.Record.HelperClip);
	}
	// The patient's clip only for a mate's aid on a body that lies / crouches (a standing patient keeps his own pose).
	if (bPatientSide && Treatment.Mode == FirstAidRules::EMode::Aid && Patient && Patient != Helper && Treatment.PatientPose != FirstAidRules::EPatientPose::None)
	{
		PlayOn(Patient, true, Treatment.PatientClip, Treatment.Record.PatientClip);
	}
}

void UFirstAidSubsystem::ApplyEffect(FTreatment& Treatment)
{
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	if (!Helper || !Patient || !Patient->HealthComponent)
	{
		return;
	}
	const FirstAidRules::FItemConfig& Data = FirstAidRules::GetConfig().Item(Treatment.Item);
	UHealthComponent* Health = Patient->HealthComponent;
	const float Before = Health->GetCurrentHealth();
	const float Target = FirstAidRules::HealedHealth(Data, BoundsOf(*Patient), TierOf(*Patient), Before, Health->GetMaxHealth());
	if (Target > Before)
	{
		Health->Heal(Target - Before);
	}
	int32& Count = Treatment.Item == EFirstAidItem::MedicalBag ? Helper->MedicalBagsCount : Helper->MedkitsCount;
	Count = FMath::Max(0, Count - 1);
	const float After = Health->GetCurrentHealth();
	Treatment.PatientHealth = After;
	if (Helper == Patient)
	{
		Treatment.HelperHealth = After;
	}
	Treatment.Record.bApplied = true;
	Treatment.Record.bConsumed = true;
	Treatment.Record.HealthAfter = After;
	Treatment.Record.TierAfter = TierOf(*Patient);
	const int32 Gained = FMath::FloorToInt(After - Before);
	if (UFloatingTextSubsystem* Floating = Gained > 0 && GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr)
	{
		Floating->Spawn(Patient->GetActorLocation() - FVector(0.f, 0.f, Patient->GetSimpleCollisionHalfHeight() - 210.f), FString::Printf(TEXT("+%d HP"), Gained),
			FLinearColor(0.2f, 1.f, 0.4f), 0.9f, 70.f);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s treated %s with the %s: %.0f -> %.0f HP, tier %s -> %s (%d %s left)"), *Helper->DisplayName.ToString(),
		*Patient->DisplayName.ToString(), FirstAidRules::ItemKey(Treatment.Item), Before, After, FirstAidRules::TierKey(Treatment.Tier), FirstAidRules::TierKey(Treatment.Record.TierAfter), Count,
		FirstAidRules::ItemKey(Treatment.Item));
	Post(Helper, Helper == Patient
		? FString::Printf(TEXT("%s is patched up: +%d HP (%s left: x%d)"), *Helper->DisplayName.ToString(), Gained, *FirstAidSubsystemLocal::ItemName(Treatment.Item), Count)
		: FString::Printf(TEXT("%s patched up %s: +%d HP (%s left: x%d)"), *Helper->DisplayName.ToString(), *Patient->DisplayName.ToString(), Gained,
			*FirstAidSubsystemLocal::ItemName(Treatment.Item), Count));
	// Godot inventory_drawer.gd item_used: the tutorial's medkit lesson hears "ItemUsed.MEDKIT" with the healed operative as the subject.
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnItemUsed.Broadcast(Patient, FirstAidRules::ItemEventId(Treatment.Item));
	}
}

void UFirstAidSubsystem::Finish(int32 Index, FirstAidRules::EInterrupt Reason)
{
	if (!Treatments.IsValidIndex(Index))
	{
		return;
	}
	FTreatment Treatment = MoveTemp(Treatments[Index]);
	Treatments.RemoveAt(Index);
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	const bool bPatientClip = Treatment.PatientClip.IsValid() && Patient && Patient != Helper;
	for (AOperativeCharacter* Who : { Helper, Patient })
	{
		if (UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Who))
		{
			Anim->StopFirstAidClip(Who == Patient && bPatientClip ? Treatment.PatientPlay.BlendOutSeconds : Config.BlendOutSeconds);
		}
	}
	// The patient's clip blends out: his capsule turns back with it, then (a completed aid, able to stand) he gets up (R17-6).
	if (bPatientClip && FirstAidSubsystemLocal::IsAliveOperative(Patient))
	{
		FSettle& Settle = Settles.AddDefaulted_GetRef();
		Settle.Patient = Patient;
		Settle.BaseYaw = Treatment.PatientBaseYaw;
		Settle.Play = Treatment.PatientPlay;
		Settle.SecondsLeft = Treatment.PatientPlay.BlendOutSeconds + 0.1f;
		Settle.bStand = FirstAidRules::StandsAfter(Config, Treatment.PatientPose, Reason == FirstAidRules::EInterrupt::None, !Patient->IsCriticallyWounded());
		Settle.OrderSerial = Patient->GetMoveOrderSerial();
		Settle.Stance = static_cast<uint8>(Patient->GetStance());
	}
	Treatment.Record.Elapsed = Treatment.Clock.Elapsed;
	Treatment.Record.Interrupt = Reason;
	Treatment.Record.bCompleted = Reason == FirstAidRules::EInterrupt::None;
	if (Reason != FirstAidRules::EInterrupt::None)
	{
		if (Helper && FirstAidRules::ConsumesOnInterrupt(Config, Treatment.Clock.bApplied))
		{
			int32& Count = Treatment.Item == EFirstAidItem::MedicalBag ? Helper->MedicalBagsCount : Helper->MedkitsCount;
			Count = FMath::Max(0, Count - 1);
			Treatment.Record.bConsumed = true;
		}
		if (Reason != FirstAidRules::EInterrupt::Lost && Helper)
		{
			Post(Helper, FString::Printf(TEXT("Treatment interrupted (%s)%s"), FirstAidRules::InterruptName(Reason),
				Treatment.Clock.bApplied ? TEXT(" - already patched up") : (Treatment.Record.bConsumed ? TEXT(" - the item is lost") : TEXT(" - the item is kept"))));
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s -> %s: %s %s after %.2f / %.2f s (%s, applied %d, consumed %d, clips '%s' / '%s', clip frames %d / %d of %d)"),
		*Treatment.Record.Helper, *Treatment.Record.Patient, FirstAidRules::ItemKey(Treatment.Item), Reason == FirstAidRules::EInterrupt::None ? TEXT("done") : TEXT("INTERRUPTED"),
		Treatment.Clock.Elapsed, Treatment.Clock.Seconds, FirstAidRules::InterruptName(Reason), Treatment.Clock.bApplied ? 1 : 0, Treatment.Record.bConsumed ? 1 : 0,
		*Treatment.Record.HelperClip, *Treatment.Record.PatientClip, Treatment.Record.HelperClipFrames, Treatment.Record.PatientClipFrames, Treatment.Record.Frames);
	if (Records.Num() >= FirstAidSubsystemLocal::MaxRecords)
	{
		Records.RemoveAt(0);
	}
	Records.Add(Treatment.Record);
}

void UFirstAidSubsystem::TickSettles(float DeltaTime)
{
	for (int32 Index = Settles.Num() - 1; Index >= 0; --Index)
	{
		FSettle& Settle = Settles[Index];
		AOperativeCharacter* Patient = Settle.Patient.Get();
		// A new order / stance / death: he is the player's again (no stand-up, no more turning).
		if (!FirstAidSubsystemLocal::IsAliveOperative(Patient) || Patient->GetMoveOrderSerial() != Settle.OrderSerial
			|| static_cast<uint8>(Patient->GetStance()) != Settle.Stance)
		{
			Settles.RemoveAt(Index);
			continue;
		}
		HoldBodyHeading(*Patient, Settle.BaseYaw, Settle.Play, DeltaTime);
		Settle.SecondsLeft -= DeltaTime;
		const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Patient);
		if (Settle.SecondsLeft > 0.f && Anim && Anim->GetFirstAidClipWeight() > 0.001f)
		{
			continue;
		}
		if (!FMath::IsNearlyZero(Settle.Play.LongAxisDeg, 0.5f) && FirstAidRules::GetConfig().Patient.bHoldBodyHeading)
		{
			Patient->SetActorRotation(FRotator(0.f, Settle.BaseYaw, 0.f)); // the clip is out: his capsule back where it lay
		}
		const bool bStand = Settle.bStand && !Patient->IsCriticallyWounded();
		Settles.RemoveAt(Index);
		if (bStand)
		{
			Patient->SetStance(EOperativeStance::Standing);
			const bool bUp = Patient->GetStance() == EOperativeStance::Standing;
			StandUps += bUp ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s: patched up - %s"), *Patient->DisplayName.ToString(),
				bUp ? TEXT("gets up") : TEXT("cannot get up (stance refused)"));
		}
	}
}

void UFirstAidSubsystem::Tick(float DeltaTime)
{
	TickSettles(DeltaTime);
	if (Treatments.IsEmpty())
	{
		return;
	}
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	for (int32 Index = Treatments.Num() - 1; Index >= 0; --Index)
	{
		FTreatment& Treatment = Treatments[Index];
		AOperativeCharacter* Helper = Treatment.Helper.Get();
		AOperativeCharacter* Patient = Treatment.Patient.Get();
		FirstAidRules::FInterruptInput Input;
		Input.bApproaching = Treatment.bApproaching;
		Input.bSelf = Treatment.Mode != FirstAidRules::EMode::Aid;
		Input.bHelperValid = IsValid(Helper) && Helper->HealthComponent != nullptr;
		Input.bPatientValid = IsValid(Patient) && Patient->HealthComponent != nullptr;
		if (Input.bHelperValid && Input.bPatientValid)
		{
			Input.bHelperAlive = FirstAidSubsystemLocal::IsAliveOperative(Helper);
			Input.bPatientAlive = FirstAidSubsystemLocal::IsAliveOperative(Patient);
			Input.bHelperKnockedDown = Helper->IsKnockedDown();
			const float HelperNow = FirstAidSubsystemLocal::HealthOf(Helper);
			const float PatientNow = FirstAidSubsystemLocal::HealthOf(Patient);
			Input.HelperDamage = FMath::Max(0.f, Treatment.HelperHealth - HelperNow);
			Input.PatientDamage = FMath::Max(0.f, Treatment.PatientHealth - PatientNow);
			Treatment.HelperHealth = HelperNow;
			Treatment.PatientHealth = PatientNow;
			// An order: a new move order of his (the serial moves), or a stance change once the treatment runs.
			Input.bHelperOrdered = Helper->GetMoveOrderSerial() != Treatment.HelperOrderSerial
				|| (!Treatment.bApproaching && static_cast<uint8>(Helper->GetStance()) != Treatment.HelperStance) || Helper->bInCover;
			Input.bPatientOrdered = Patient->GetMoveOrderSerial() != Treatment.PatientOrderSerial
				|| (!Treatment.bApproaching && static_cast<uint8>(Patient->GetStance()) != Treatment.PatientStance);
			Input.DistanceCm = FVector::Dist2D(Helper->GetActorLocation(), Patient->GetActorLocation());
			if (Treatment.bApproaching)
			{
				Treatment.ApproachSeconds += DeltaTime;
				Input.ApproachSeconds = Treatment.ApproachSeconds;
				Input.bApproachStalled = !Helper->HasOwnMoveOrder() && Input.DistanceCm > Config.ReachCm + FirstAidSubsystemLocal::ArrivedSlackCm;
			}
			else
			{
				Input.HelperMovedCm = FVector::Dist2D(Helper->GetActorLocation(), Treatment.HelperSpot);
				Input.PatientMovedCm = FVector::Dist2D(Patient->GetActorLocation(), Treatment.PatientSpot);
			}
		}
		const FirstAidRules::EInterrupt Reason = FirstAidRules::CheckInterrupt(Config, Input);
		if (Reason != FirstAidRules::EInterrupt::None)
		{
			Finish(Index, Reason);
			continue;
		}
		if (Treatment.bApproaching)
		{
			// Arrived: his walk ended within reach, or he is already at his spot (the walk may still brake) - the goal beside a lying patient
			// (R17-6: not the first point within the stop distance, which lies on the way in, at the patient's head or feet).
			const bool bAtSpot = Treatment.bHasApproachGoal
				? FVector::Dist2D(Helper->GetActorLocation(), Treatment.ApproachGoal) <= FirstAidSubsystemLocal::ArrivedSlackCm
				: Input.DistanceCm <= Config.ApproachStopCm + FirstAidSubsystemLocal::ArrivedSlackCm;
			const bool bArrived = Input.DistanceCm <= Config.ReachCm && (!Helper->HasOwnMoveOrder() || bAtSpot);
			if (bArrived && Patient->IsKnockedDown())
			{
				Treatment.Record.bWaitedForKnockdown = true; // the knockdown owns his FullBody slot: treat once he lies still / is up
				continue;
			}
			if (bArrived)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s reached %s after %.1f s (%.0f cm)"), *Helper->DisplayName.ToString(),
					*Patient->DisplayName.ToString(), Treatment.ApproachSeconds, Input.DistanceCm);
				BeginTreating(Treatment);
			}
			continue;
		}
		if (Treatment.bHelperFacing && Helper != Patient)
		{
			Treatment.HelperFaceYaw = FirstAidRules::HelperFaceYaw(Config, FirstAidRules::IsLyingPose(Treatment.PatientPose),
				Patient->GetActorLocation(), Treatment.PatientBaseYaw, Helper->GetActorLocation(), Treatment.HelperFaceYaw);
			const float TurnRate = Treatment.bWaitingToFace
				? FMath::Max(Config.Patient.HelperTurnDegPerSecond, FirstAidRules::MinHelperPreTurnDegPerSec)
				: FMath::Max(Config.Patient.HelperTurnDegPerSecond, 360.f);
			const float Yaw = FirstAidRules::TurnToward(static_cast<float>(Helper->GetActorRotation().Yaw), Treatment.HelperFaceYaw,
				TurnRate, DeltaTime);
			Helper->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			if (Treatment.bWaitingToFace)
			{
				if (FirstAidRules::IsHelperFacingReady(Yaw, Treatment.HelperFaceYaw))
				{
					Treatment.bWaitingToFace = false;
					Treatment.Clock.Elapsed = 0.f;
					Treatment.Clock.bApplied = false;
					Treatment.Record.Frames = 0;
					Treatment.HelperSpot = Helper->GetActorLocation();
					Treatment.PatientSpot = Patient->GetActorLocation();
					PlayClips(Treatment, false);
				}
				else
				{
					continue;
				}
			}
		}
		++Treatment.Record.Frames;
		if (Treatment.PatientClip.IsValid() && Patient != Helper)
		{
			HoldBodyHeading(*Patient, Treatment.PatientBaseYaw, Treatment.PatientPlay, DeltaTime);
		}
		const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(Helper);
		const UOperativeAnimInstance* PatientAnim = Patient != Helper ? UOperativeAnimInstance::Of(Patient) : nullptr;
		const bool bHelperClip = HelperAnim && HelperAnim->IsFirstAidClipActive();
		const bool bPatientClip = PatientAnim && PatientAnim->IsFirstAidClipActive();
		Treatment.Record.HelperClipFrames += bHelperClip ? 1 : 0;
		Treatment.Record.PatientClipFrames += bPatientClip ? 1 : 0;
		// Another clip took the FullBody slot (a fire / hit clip of the same montage group): back into the treatment pose, a few times at most.
		const bool bHelperLost = Treatment.HelperClip.IsValid() && !bHelperClip && Treatment.HelperRestarts < FirstAidSubsystemLocal::MaxClipRestarts;
		const bool bPatientLost = Treatment.PatientClip.IsValid() && PatientAnim && !bPatientClip
			&& Treatment.PatientRestarts < FirstAidSubsystemLocal::MaxClipRestarts;
		if ((bHelperLost || bPatientLost) && Treatment.Clock.Elapsed + 0.5f < Treatment.Clock.Seconds)
		{
			Treatment.HelperRestarts += bHelperLost ? 1 : 0;
			Treatment.PatientRestarts += bPatientLost ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s: the treatment clip was cut (helper %d, patient %d) - restarted at %.2f s"),
				*Treatment.Record.Helper, bHelperLost ? 1 : 0, bPatientLost ? 1 : 0, Treatment.Clock.Elapsed);
			PlayClips(Treatment, true, bHelperLost, bPatientLost);
		}
		if (FirstAidRules::AdvanceClock(Treatment.Clock, DeltaTime))
		{
			ApplyEffect(Treatment);
		}
		if (FirstAidRules::IsFinished(Treatment.Clock))
		{
			Finish(Index, FirstAidRules::EInterrupt::None);
		}
	}
}
