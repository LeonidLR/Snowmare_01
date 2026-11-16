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
		return FString::Printf(TEXT("%s/%s/%s.%s"), ClipRoot, bFemale ? TEXT("Female") : TEXT("Male"), *Name, *Name);
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

void UFirstAidSubsystem::Deinitialize()
{
	Treatments.Reset();
	Records.Reset();
	Super::Deinitialize();
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
	return Operative.IsKnockedDown() || Operative.IsClimbingLadder() || Operative.IsVaulting() || Operative.IsMantling() || Operative.IsRagdollActive()
		|| Operative.bInCover || Operative.bCarrying || IsInTreatment(&Operative);
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
		Post(Who, FString::Printf(TEXT("%s is busy (cover, ladder, climbing or another treatment)."), *WhoName));
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
		// Walk up beside him (his side toward the helper), then treat.
		const FVector Away = (Helper->GetActorLocation() - Patient->GetActorLocation()).GetSafeNormal2D();
		const FVector Goal = Patient->GetActorLocation() + (Away.IsNearlyZero() ? Patient->GetActorRightVector() : Away) * Config.ApproachStopCm;
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
		Treatment.Record.bApproached = true;
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
	return Patient && FirstAidRules::TreatsOnDrop(FirstAidRules::GetConfig(), TierOf(*Patient), bHandOverOnly);
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

void UFirstAidSubsystem::BeginTreating(FTreatment& Treatment)
{
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	if (!Helper || !Patient)
	{
		return;
	}
	Treatment.bApproaching = false;
	if (Helper == Patient)
	{
		Helper->StopOperative();
	}
	else
	{
		Helper->StopAndFace(Patient->GetActorLocation());
		Patient->StopOperative();
	}
	Treatment.HelperSpot = Helper->GetActorLocation();
	Treatment.PatientSpot = Patient->GetActorLocation();
	Treatment.HelperOrderSerial = Helper->GetMoveOrderSerial();
	Treatment.PatientOrderSerial = Patient->GetMoveOrderSerial();
	Treatment.HelperStance = static_cast<uint8>(Helper->GetStance());
	Treatment.PatientStance = static_cast<uint8>(Patient->GetStance());
	Treatment.HelperHealth = FirstAidSubsystemLocal::HealthOf(Helper);
	Treatment.PatientHealth = FirstAidSubsystemLocal::HealthOf(Patient);
	PlayClips(Treatment, false);
}

void UFirstAidSubsystem::PlayClips(FTreatment& Treatment, bool bRestart, bool bHelperSide, bool bPatientSide)
{
	const FirstAidRules::FFirstAidConfig& Config = FirstAidRules::GetConfig();
	const FirstAidRules::FItemConfig& Data = Config.Item(Treatment.Item);
	auto PlayOn = [this, &Treatment, &Config, &Data, bRestart](AOperativeCharacter* Who, bool bPatient, TWeakObjectPtr<UAnimSequenceBase>& Clip, FString& RecordName)
	{
		UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Who);
		if (!Who || !Anim)
		{
			return;
		}
		if (!bRestart || !Clip.IsValid())
		{
			const TArray<FString>& Set = FirstAidRules::ClipSet(Data, Treatment.Mode, bPatient);
			int32& Last = LastClipIndex.FindOrAdd(&Set, INDEX_NONE);
			const int32 First = FirstAidRules::PickClipIndex(Set.Num(), FMath::FRand(), Last);
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
					*Who->DisplayName.ToString(), bPatient ? TEXT("patient") : TEXT("treatment"), FirstAidRules::ModeName(Treatment.Mode), Set.Num());
				return;
			}
		}
		UAnimSequenceBase* Sequence = Clip.Get();
		const float Length = Sequence->GetPlayLength();
		const float Rate = FirstAidRules::FitPlayRate(Length, Treatment.Clock.Seconds, Config.MinPlayRate, Config.MaxPlayRate);
		const float Start = bRestart && Length > 0.f ? FMath::Fmod(Treatment.Clock.Elapsed * Rate, Length) : 0.f;
		Anim->PlayFirstAidClip(Sequence, FMath::Max(0.1f, Treatment.Clock.Seconds - (bRestart ? Treatment.Clock.Elapsed : 0.f)), Config.MinPlayRate,
			Config.MaxPlayRate, Config.BlendInSeconds, Config.BlendOutSeconds, Start, Config.bHideWeapon);
	};
	AOperativeCharacter* Helper = Treatment.Helper.Get();
	AOperativeCharacter* Patient = Treatment.Patient.Get();
	if (bHelperSide)
	{
		PlayOn(Helper, false, Treatment.HelperClip, Treatment.Record.HelperClip);
	}
	// The patient's lying clip only for a mate's aid on a body that lies (a standing patient keeps his own pose).
	if (bPatientSide && Treatment.Mode == FirstAidRules::EMode::Aid && Patient && Patient != Helper && Patient->GetStance() == EOperativeStance::Prone)
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
	for (AOperativeCharacter* Who : { Helper, Patient })
	{
		if (UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Who))
		{
			Anim->StopFirstAidClip(Config.BlendOutSeconds);
		}
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

void UFirstAidSubsystem::Tick(float DeltaTime)
{
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
			// Arrived: his walk ended within reach, or he is already at the stop distance (the walk may still brake).
			const bool bArrived = Input.DistanceCm <= Config.ReachCm && (!Helper->HasOwnMoveOrder()
				|| Input.DistanceCm <= Config.ApproachStopCm + FirstAidSubsystemLocal::ArrivedSlackCm);
			if (bArrived)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s reached %s after %.1f s (%.0f cm)"), *Helper->DisplayName.ToString(),
					*Patient->DisplayName.ToString(), Treatment.ApproachSeconds, Input.DistanceCm);
				BeginTreating(Treatment);
			}
			continue;
		}
		++Treatment.Record.Frames;
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
