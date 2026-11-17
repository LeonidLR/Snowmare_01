// Dev-only headless check of FIRST AID BY WOUND TIER (the game designer 2026-11-16, Survival/FirstAidRules, UFirstAidSubsystem) on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FirstAidSmoke
// Two operatives in Solo (the others parked, the enemies removed, autonomy off, cease fire), through the REAL input path:
//  1. LIGHT (40 % HP): the drawer's medkit line (UInventoryDrawerWidget::Activate = a click) -> a STANDING self-treatment: a self_standing clip
//     on the FullBody slot, the weapon hidden, the HP lands at apply_at, one medkit used;
//  2. MEDIUM (28 %): the same click -> a KNEELING clip (self_kneeling);
//  3. HEAVY (15 %, he goes down by himself): H / the drawer click on HIMSELF is refused ("cannot treat himself"), nothing used;
//  4. HEAVY: the other operative's drawer medkit line dragged (CreateDragOperation) onto the wounded's PORTRAIT (UActionBarWidget::
//     HandleTransferDropOnSlot) from 6 m: he walks up, the helper plays the medkit's FAST helper clip, the wounded the lying patient clip, the
//     heal lands, the tier improves, one medkit used;
//  5. HEAVY: the medical bag dragged onto the wounded's 3D MODEL (ACodexTacticsHUD::HandleTransferDropOnActor, the world drop): the bag's longer
//     helper clip, a longer treatment, more HP than the medkit, the bag used;
//  6. CANCEL: a medkit drop again, a move order for the helper mid-treatment -> interrupted (a new order), no heal, the medkit KEPT
//     (consume_on_cancel false), the clips stopped.
//  7. G006 (designer 2026-11-17): at level start the Medic-Sapper's drawer lists the medical bag (start_medical_bags, not a smoke assignment);
//     a medkit dragged onto a LIGHT-wounded mate starts aid (not a transfer); the same drop with Shift hands the medkit over.
//  8. R17-6 (designer 2026-11-17, the lying patient): the heavy patient lies on all fours (pose "crawl"); the medkit aid and the bag aid run with
//     two pick seeds (UFirstAidSubsystem::SetPickSeed) chosen so they pick DIFFERENT variants of the crawl set (a crawl idle / the lie-down onto the
//     side), each the seed's predicted pick; through the whole transition (blend-in, treatment, blend-out) the patient's VISIBLE body - the long axis
//     pelvis -> head from the mesh bones - turns slower than patient.max_body_turn_deg_s (0.2 s windows; the old 0.25 s blend into the side-lying
//     clip swung it ~150 deg = ~600 deg/s); the helper kneels at his SIDE (not at his head / feet) facing his chest; after the completed aid the
//     patient gets up (patient.poses.crawl.end "stand") once his clip has blended out.
// Before the copies exist (Scripts/Editor/setup_first_aid_animation.py) the clip checks fail with "no clip"; the rules still run.
// Nothing is saved. Godot reference: none (UE-only; Godot's medkit was instant, player.gd heal_with_item).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/FirstAidRules.h"
#include "Survival/FirstAidSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryDrawerWidget.h"

namespace FirstAidSmoke
{
	using namespace FirstAidRules;

	constexpr float Timeout = 300.f;
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float LightPct = 0.40f;
	constexpr float MediumPct = 0.28f;
	constexpr float HeavyPct = 0.15f;
	constexpr float HelperDistanceCm = 600.f;
	/** A stage waits this long for its treatment to end (s; the longest is the bag's 11 s + the walk). */
	constexpr float StageLimit = 40.f;

	enum class EStage : uint8
	{
		Setup,
		BagListed,
		LightDrag,
		ShiftDrag,
		LightStart,
		LightRun,
		MediumStart,
		MediumRun,
		HeavySelf,
		HeavyGoDown,
		MedkitAidStart,
		MedkitAidRun,
		BagAidStart,
		BagAidRun,
		CancelStart,
		CancelRun,
		CancelCheck,
		Done
	};

	struct FRunSamples
	{
		int32 Frames = 0;
		int32 HelperClipFrames = 0;
		int32 PatientClipFrames = 0;
		int32 WeaponHiddenFrames = 0;
		FString HelperClip;
		FString PatientClip;
		float MaxHelperSlot = 0.f;
		float MaxPatientSlot = 0.f;
		/** R17-6: the patient's visible body heading (the long axis pelvis -> head, mesh bones) and his capsule yaw, (time s, yaw deg). */
		TArray<FVector2D> BodyYaw;
		TArray<FVector2D> CapsuleYaw;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		bool bDone = false;
		SmokeUtils::FPreloadGate Gate;
		TWeakObjectPtr<AOperativeCharacter> A;
		TWeakObjectPtr<AOperativeCharacter> B;
		FVector Spot = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		int32 RecordsBefore = 0;
		int32 CountBefore = 0;
		float HealthBefore = 0.f;
		float MedkitGain = 0.f;
		float MedkitSeconds = 0.f;
		/** R17-6: the record of the aid under test was checked; now its blend-out / stand-up is watched. */
		bool bRecordChecked = false;
		FString AidPatientClip;
		FString MedkitPatientClip;
		int32 StandUpsBefore = 0;
		float PatientYaw0 = 0.f;
		int32 MedkitSeed = 0;
		int32 BagSeed = 0;
		FString MedkitPredicted;
		FString BagPredicted;
		FRunSamples Samples;
		TArray<FString> Report;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FirstAidRules::ResetConfig();
		for (const FString& Line : State.Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("FirstAidSmoke"));
		return false;
	}

	void Next(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void SetHealthShare(AOperativeCharacter* Op, float Share)
	{
		if (Op && Op->HealthComponent)
		{
			Op->HealthComponent->RestoreHealth(Share * Op->HealthComponent->GetMaxHealth(), Op->HealthComponent->GetMaxHealth());
		}
	}

	float Health(const AOperativeCharacter* Op)
	{
		return Op && Op->HealthComponent ? Op->HealthComponent->GetCurrentHealth() : 0.f;
	}

	ACodexTacticsHUD* GetHud(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
	}

	/** The newest record after the stage started (null while the treatment runs). */
	const FFirstAidRecord* NewRecord(const UFirstAidSubsystem* FirstAid, const FState& State)
	{
		return FirstAid && FirstAid->GetRecords().Num() > State.RecordsBefore ? &FirstAid->GetRecords().Last() : nullptr;
	}

	void Sample(FState& State)
	{
		const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(State.A.Get());
		const UOperativeAnimInstance* PatientAnim = State.B.IsValid() && State.B != State.A ? UOperativeAnimInstance::Of(State.B.Get()) : nullptr;
		++State.Samples.Frames;
		if (HelperAnim && HelperAnim->IsFirstAidClipActive())
		{
			++State.Samples.HelperClipFrames;
			State.Samples.HelperClip = HelperAnim->GetFirstAidClipName();
			State.Samples.MaxHelperSlot = FMath::Max(State.Samples.MaxHelperSlot, HelperAnim->GetSlotMontageGlobalWeight(HelperAnim->FullBodySlot));
		}
		if (PatientAnim && PatientAnim->IsFirstAidClipActive())
		{
			++State.Samples.PatientClipFrames;
			State.Samples.PatientClip = PatientAnim->GetFirstAidClipName();
			State.Samples.MaxPatientSlot = FMath::Max(State.Samples.MaxPatientSlot, PatientAnim->GetSlotMontageGlobalWeight(PatientAnim->FullBodySlot));
		}
		const AOperativeCharacter* A = State.A.Get();
		State.Samples.WeaponHiddenFrames += A && A->WeaponMesh && !A->WeaponMesh->IsVisible() ? 1 : 0;
	}

	/** R17-6: the patient's visible body heading (the long axis from the mesh bones, when he lies) and his capsule yaw. */
	void SamplePatientBody(FState& State)
	{
		const AOperativeCharacter* B = State.B.Get();
		const USkeletalMeshComponent* Mesh = B ? B->GetMesh() : nullptr;
		if (!Mesh)
		{
			return;
		}
		const FVector Axis = Mesh->GetSocketLocation(TEXT("head")) - Mesh->GetSocketLocation(TEXT("pelvis"));
		if (Axis.Size2D() > 25.f)
		{
			State.Samples.BodyYaw.Add(FVector2D(State.Time, Axis.Rotation().Yaw));
		}
		State.Samples.CapsuleYaw.Add(FVector2D(State.Time, B->GetActorRotation().Yaw));
	}

	/** The patient clip a seed picks first in the crawl set (UFirstAidSubsystem::SetPickSeed: the patient's first draw of FRandomStream(Seed)). */
	FString PredictedPatientClip(const TArray<FString>& Set, int32 Seed)
	{
		FRandomStream Stream(Seed);
		const int32 Index = PickClipIndex(Set.Num(), Stream.FRand(), INDEX_NONE);
		return Set.IsValidIndex(Index) ? Set[Index] : FString();
	}

	/** Two seeds for the two aids: the medkit's picks a crawl idle (no turn), the bag's the most-turning clip (the lie-down), else any other one. */
	void ChooseSeeds(FState& State, const FFirstAidConfig& Config)
	{
		const TArray<FString>& Set = PatientClipSet(Config, Config.Medkit, EPatientPose::Crawl);
		auto Turn = [&Config](const FString& Clip) { return FMath::Abs(PatientClipPlay(Config, EPatientPose::Crawl, Clip).LongAxisDeg); };
		float MostTurn = 0.f;
		for (const FString& Clip : Set)
		{
			MostTurn = FMath::Max(MostTurn, Turn(Clip));
		}
		State.MedkitSeed = State.BagSeed = 0;
		for (int32 Seed = 1; Seed < 500 && (State.MedkitSeed == 0 || State.BagSeed == 0); ++Seed)
		{
			const FString Clip = PredictedPatientClip(Set, Seed);
			if (State.MedkitSeed == 0 && Turn(Clip) < 1.f)
			{
				State.MedkitSeed = Seed;
				State.MedkitPredicted = Clip;
			}
			else if (State.MedkitSeed != 0 && State.BagSeed == 0 && Clip != State.MedkitPredicted && (Turn(Clip) >= MostTurn || MostTurn < 1.f))
			{
				State.BagSeed = Seed;
				State.BagPredicted = Clip;
			}
		}
	}

	/** Starts a stage's treatment bookkeeping. */
	void Arm(FState& State, const UFirstAidSubsystem* FirstAid, AOperativeCharacter* CountOwner, EFirstAidItem Item, const AOperativeCharacter* Patient)
	{
		State.RecordsBefore = FirstAid ? FirstAid->GetRecords().Num() : 0;
		State.CountBefore = CountOwner ? UFirstAidSubsystem::CountOf(*CountOwner, Item) : 0;
		State.HealthBefore = Health(Patient);
		State.Samples = FRunSamples();
	}

	void Place(UWorld* World, AOperativeCharacter* Who, const FVector& Spot, const FVector& Facing)
	{
		if (Who)
		{
			Who->StopOperative();
			Who->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Who), FRotator(0.f, Facing.Rotation().Yaw, 0.f), false, true);
		}
	}

	/** The slot index of Member on the action bar (INDEX_NONE when not shown). */
	int32 SlotOf(const UActionBarWidget* Bar, const AOperativeCharacter* Member)
	{
		for (int32 Index = 0; Bar && Index < 8; ++Index)
		{
			if (Bar->GetSlotMember(Index) == Member)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** Checks a finished self-treatment record. */
	void CheckSelfRecord(FState& State, const FFirstAidRecord& Record, EMode Mode, const TArray<FString>& Set, const AOperativeCharacter* A, const TCHAR* Name)
	{
		Check(State, Record.bCompleted && Record.bApplied && Record.Mode == Mode, FString::Printf(TEXT("[%s] completed in mode '%s' (%s, %.1f / %.1f s, interrupt %s)"),
			Name, ModeName(Record.Mode), Record.bCompleted ? TEXT("done") : TEXT("not done"), Record.Elapsed, Record.Seconds, InterruptName(Record.Interrupt)));
		Check(State, Set.Contains(Record.HelperClip) && State.Samples.HelperClipFrames >= State.Samples.Frames * 8 / 10 && State.Samples.MaxHelperSlot > 0.9f,
			FString::Printf(TEXT("[%s] the clip '%s' (one of the %s set) played on the FullBody slot %d / %d frames (weight %.2f)"), Name, *Record.HelperClip,
				ModeName(Mode), State.Samples.HelperClipFrames, State.Samples.Frames, State.Samples.MaxHelperSlot));
		Check(State, Health(A) > State.HealthBefore + 1.f && Record.HealthAfter > Record.HealthBefore,
			FString::Printf(TEXT("[%s] HP up %.0f -> %.0f, tier %s -> %s"), Name, State.HealthBefore, Health(A), TierKey(Record.TierBefore), TierKey(Record.TierAfter)));
		Check(State, A && A->MedkitsCount == State.CountBefore - 1, FString::Printf(TEXT("[%s] one medkit used (%d -> %d)"), Name, State.CountBefore, A ? A->MedkitsCount : -1));
		Check(State, State.Samples.WeaponHiddenFrames > 0 && A && A->WeaponMesh && A->WeaponMesh->IsVisible(),
			FString::Printf(TEXT("[%s] the weapon hidden while treating (%d frames) and shown again"), Name, State.Samples.WeaponHiddenFrames));
		State.Report.Add(FString::Printf(TEXT("%s: %s, %.1f s, %.0f -> %.0f HP"), Name, *Record.HelperClip, Record.Seconds, Record.HealthBefore, Record.HealthAfter));
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		switch (State.Gate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.Gate.FailReason);
			return Finish(State, false);
		default: break;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UFirstAidSubsystem* FirstAid = UFirstAidSubsystem::Get(World);
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ACodexTacticsHUD* Hud = GetHud(World);
		UInventoryDrawerWidget* Drawer = Hud ? Hud->GetInventoryDrawer() : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		if (!Squad || !FirstAid || !PC || !Drawer || !Bar || Squad->GetMembers().Num() < 2)
		{
			if (State.Time < 15.f)
			{
				return true;
			}
			Check(State, false, TEXT("squad of 2+, the first-aid subsystem, the player controller, the drawer and the action bar"));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		AOperativeCharacter* A = State.A.Get();
		AOperativeCharacter* B = State.B.Get();
		if (State.Stage != EStage::Setup && (!A || !B))
		{
			Check(State, false, TEXT("the two operatives are still there"));
			return Finish(State, false);
		}
		const FFirstAidConfig& Config = GetConfig();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < NavWarmupSeconds)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.A = A = Members[0];
			State.B = B = Members[1];
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Squad->SetAutonomousSquadCombat(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetLeader(A);
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
				if (Member->GetMesh())
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, A->GetActorLocation(), A->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-400.f, -400.f), FVector2D(HelperDistanceCm + 400.f, 400.f)), Ignore);
			State.Spot = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, A->GetSimpleCollisionHalfHeight() + 5.f) : A->GetActorLocation();
			State.Forward = Arena.bFound ? Arena.Forward.GetSafeNormal2D() : A->GetActorForwardVector().GetSafeNormal2D();
			Check(State, Arena.bFound, FString::Printf(TEXT("a clear %.0f m patch at %s"), (HelperDistanceCm + 400.f) / 100.f, *State.Spot.ToCompactString()));
			// The others far away (no medic heals the wounded on his own).
			for (int32 Index = 2; Index < Members.Num(); ++Index)
			{
				Place(World, Members[Index], State.Spot - State.Forward * 2500.f + FVector(0.f, 300.f * Index, 0.f), State.Forward);
			}
			Place(World, A, State.Spot, State.Forward);
			Place(World, B, State.Spot + State.Forward * HelperDistanceCm, -State.Forward);
			A->MedkitsCount = 5;
			B->MedkitsCount = 2;
			UE_LOG(LogCodexTactics, Display, TEXT("FirstAidSmoke: %s (helper / self) and %s (the wounded), max HP %.0f / %.0f"), *A->DisplayName.ToString(),
				*B->DisplayName.ToString(), A->HealthComponent->GetMaxHealth(), B->HealthComponent->GetMaxHealth());
			Next(State, EStage::BagListed);
			return true;
		}
		case EStage::BagListed:
		{
			AOperativeCharacter* Medic = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->SquadRole == EOperativeRole::MedicSapper)
				{
					Medic = Member;
					break;
				}
			}
			const int32 Expected = StartMedicalBags(Config, TEXT("MedicSapper"));
			Check(State, Medic && Expected > 0 && Medic->MedicalBagsCount == Expected,
				FString::Printf(TEXT("[bag] the medic-sapper starts with %d medical bag(s) (has %d)"), Expected, Medic ? Medic->MedicalBagsCount : -1));
			if (!Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			Drawer->Refresh();
			if (A && A->SquadRole != EOperativeRole::MedicSapper && A->MedicalBagsCount <= 0 && Medic && Medic->MedicalBagsCount > 0)
			{
				const FString LeaderLine = Drawer->GetSlotText(EInventoryDrawerSlot::MedicalBag).ToString();
				Check(State, LeaderLine.Contains(TEXT("Medical bag")) && LeaderLine.Contains(TEXT("in squad")),
					FString::Printf(TEXT("[bag] the leader's inventory names the squad bag (%s)"), *LeaderLine));
			}
			if (Medic)
			{
				Squad->SetLeader(Medic);
				Drawer->Refresh();
				const FString Line = Drawer->GetSlotText(EInventoryDrawerSlot::MedicalBag).ToString();
				Check(State, Drawer->IsSlotVisible(EInventoryDrawerSlot::MedicalBag) && Drawer->IsSlotEnabled(EInventoryDrawerSlot::MedicalBag)
					&& Line.Contains(TEXT("Medical bag")) && Line.Contains(FString::Printf(TEXT("x%d"), Expected)),
					FString::Printf(TEXT("[bag] the medic's inventory line lists the bag (%s)"), *Line));
			}
			if (Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			Squad->SetLeader(A);
			Next(State, EStage::LightDrag);
			return true;
		}
		case EStage::LightDrag:
		{
			SetHealthShare(B, LightPct);
			Squad->SetLeader(A);
			Hud->bForceHandOverModifierForTesting = false;
			Arm(State, FirstAid, A, EFirstAidItem::Medkit, B);
			UInventoryDragDropOperation* Drag = Drawer->CreateDragOperation(EInventoryDrawerSlot::Medkit);
			Check(State, Drag && Drag->Sender.Get() == A, TEXT("[light-drag] the medkit row drags from the helper"));
			if (!Drag)
			{
				return Finish(State, false);
			}
			const ETransferRequestOutcome Outcome = Hud->HandleTransferDropOnActor(A, ETransferItem::Medkit, B, B->GetActorLocation());
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::Aid && FirstAid->IsInTreatment(B) && Outcome != ETransferRequestOutcome::Failed,
				FString::Printf(TEXT("[light-drag] a LIGHT wound starts aid, not a transfer (outcome %d, %s)"), static_cast<int32>(Outcome),
					Running ? (Running->bApproached ? TEXT("walking over") : TEXT("in reach")) : TEXT("no treatment")));
			Check(State, A->MedkitsCount == State.CountBefore && B->MedkitsCount == 2,
				FString::Printf(TEXT("[light-drag] not handed over (helper %d, patient %d)"), A->MedkitsCount, B->MedkitsCount));
			FirstAid->Cancel(A, EInterrupt::Order);
			Place(World, A, State.Spot, State.Forward);
			Next(State, EStage::ShiftDrag);
			return true;
		}
		case EStage::ShiftDrag:
		{
			SetHealthShare(B, LightPct);
			Place(World, A, B->GetActorLocation() - State.Forward * 120.f, State.Forward);
			Squad->SetLeader(A);
			Hud->bForceHandOverModifierForTesting = true;
			// A stack opens the quantity dialog (the old hand-over). One medkit changes hands at once.
			const int32 HelperStock = A->MedkitsCount;
			const int32 PatientBefore = B->MedkitsCount;
			A->MedkitsCount = 1;
			UInventoryDragDropOperation* Drag = Drawer->CreateDragOperation(EInventoryDrawerSlot::Medkit);
			const ETransferRequestOutcome Outcome = Drag
				? Hud->HandleTransferDropOnActor(A, ETransferItem::Medkit, B, B->GetActorLocation())
				: ETransferRequestOutcome::Failed;
			Check(State, Drag && !FirstAid->IsInTreatment(A) && !FirstAid->IsInTreatment(B) && Outcome == ETransferRequestOutcome::Transferred
				&& A->MedkitsCount == 0 && B->MedkitsCount == PatientBefore + 1,
				FString::Printf(TEXT("[shift-drag] Shift hands the medkit over (outcome %d, helper 1 -> %d, patient %d -> %d)"),
					static_cast<int32>(Outcome), A->MedkitsCount, PatientBefore, B->MedkitsCount));
			A->MedkitsCount = HelperStock;
			B->MedkitsCount = PatientBefore;
			Hud->bForceHandOverModifierForTesting = false;
			Place(World, A, State.Spot, State.Forward);
			Next(State, EStage::LightStart);
			return true;
		}
		case EStage::LightStart:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			SetHealthShare(A, LightPct);
			Check(State, UFirstAidSubsystem::TierOf(*A) == ETier::Light, FString::Printf(TEXT("[light] %.0f %% HP = the light tier (%s)"), LightPct * 100.f,
				TierKey(UFirstAidSubsystem::TierOf(*A))));
			Arm(State, FirstAid, A, EFirstAidItem::Medkit, A);
			Hud->ToggleInventoryDrawer();
			Drawer->Activate(EInventoryDrawerSlot::Medkit); // the drawer's medkit line clicked
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::SelfStanding, FString::Printf(TEXT("[light] the drawer click starts a STANDING self-treatment (%s)"),
				Running ? ModeName(Running->Mode) : TEXT("none")));
			Check(State, A->MedkitsCount == State.CountBefore && Health(A) == State.HealthBefore, TEXT("[light] nothing applied at the start (the clip first)"));
			Next(State, EStage::LightRun);
			return true;
		}
		case EStage::LightRun:
		case EStage::MediumRun:
		{
			const bool bLight = State.Stage == EStage::LightRun;
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			if (!Record)
			{
				Sample(State);
				if (State.StageTime > StageLimit)
				{
					Check(State, false, TEXT("the self-treatment never ended"));
					return Finish(State, false);
				}
				return true;
			}
			CheckSelfRecord(State, *Record, bLight ? EMode::SelfStanding : EMode::SelfKneeling,
				bLight ? Config.Medkit.SelfStandingClips : Config.Medkit.SelfKneelingClips, A, bLight ? TEXT("light") : TEXT("medium"));
			Check(State, FMath::IsNearlyEqual(Record->Seconds, TreatmentSeconds(Config, EFirstAidItem::Medkit, bLight ? ETier::Light : ETier::Medium, true), 0.01f),
				FString::Printf(TEXT("[%s] the treatment's length from first_aid.json (%.1f s)"), bLight ? TEXT("light") : TEXT("medium"), Record->Seconds));
			Next(State, bLight ? EStage::MediumStart : EStage::HeavySelf);
			return true;
		}
		case EStage::MediumStart:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			SetHealthShare(A, MediumPct);
			Check(State, UFirstAidSubsystem::TierOf(*A) == ETier::Medium, FString::Printf(TEXT("[medium] %.0f %% HP = the medium tier (%s)"), MediumPct * 100.f,
				TierKey(UFirstAidSubsystem::TierOf(*A))));
			Arm(State, FirstAid, A, EFirstAidItem::Medkit, A);
			Drawer->Activate(EInventoryDrawerSlot::Medkit);
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::SelfKneeling, FString::Printf(TEXT("[medium] the drawer click starts a KNEELING self-treatment (%s)"),
				Running ? ModeName(Running->Mode) : TEXT("none")));
			Next(State, EStage::MediumRun);
			return true;
		}
		case EStage::HeavySelf:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// B goes heavy; as the leader he cannot treat himself (the drawer's medkit line / H).
			SetHealthShare(B, HeavyPct);
			Squad->SetLeader(B);
			Arm(State, FirstAid, B, EFirstAidItem::Medkit, B);
			PC->UseSquadItem(EPersonalItem::Medkit); // H
			Check(State, !FirstAid->IsInTreatment(B) && B->MedkitsCount == State.CountBefore && FMath::IsNearlyEqual(Health(B), State.HealthBefore, 0.5f),
				FString::Printf(TEXT("[heavy] H on himself is refused (tier %s): no treatment, nothing used"), TierKey(UFirstAidSubsystem::TierOf(*B))));
			Squad->SetLeader(A);
			Next(State, EStage::HeavyGoDown);
			return true;
		}
		case EStage::HeavyGoDown:
		{
			SetHealthShare(B, HeavyPct);
			if (B->GetStance() != EOperativeStance::Prone || State.StageTime < 2.f)
			{
				if (State.StageTime > 15.f)
				{
					Check(State, false, TEXT("[heavy] the wounded never went down"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, B->IsCriticallyWounded() && UFirstAidSubsystem::TierOf(*B) == ETier::Heavy, TEXT("[heavy] 15 % HP: down, the heavy tier"));
			Next(State, EStage::MedkitAidStart);
			return true;
		}
		case EStage::MedkitAidStart:
		case EStage::BagAidStart:
		case EStage::CancelStart:
		{
			// The patient down again (the previous aid stood him up): prone, on all fours, the stance clip over.
			SetHealthShare(B, HeavyPct);
			if (State.StageTime < 1.f || B->GetStance() != EOperativeStance::Prone || (State.Stage != EStage::MedkitAidStart && State.StageTime < 3.f))
			{
				if (State.StageTime > 15.f)
				{
					Check(State, false, TEXT("[aid] the wounded never went down again"));
					return Finish(State, false);
				}
				return true;
			}
			const bool bBag = State.Stage == EStage::BagAidStart;
			const EFirstAidItem Item = bBag ? EFirstAidItem::MedicalBag : EFirstAidItem::Medkit;
			if (State.Stage == EStage::MedkitAidStart)
			{
				ChooseSeeds(State, Config);
				Check(State, State.MedkitSeed != 0 && State.BagSeed != 0, FString::Printf(TEXT("[variants] seeds %d / %d pick '%s' / '%s' of the crawl set"),
					State.MedkitSeed, State.BagSeed, *State.MedkitPredicted, *State.BagPredicted));
			}
			if (State.Stage != EStage::CancelStart)
			{
				FirstAid->SetPickSeed(bBag ? State.BagSeed : State.MedkitSeed);
			}
			State.bRecordChecked = false;
			State.StandUpsBefore = FirstAid->GetStandUps();
			State.PatientYaw0 = static_cast<float>(B->GetActorRotation().Yaw);
			if (bBag)
			{
				// The helper in this smoke is the commander. His bag is the treatment under test, not the medic's start kit.
				A->MedicalBagsCount = FMath::Max(1, A->MedicalBagsCount);
			}
			SetHealthShare(B, HeavyPct);
			Squad->SetLeader(A);
			// The helper back 6 m away (the cancel stage: within reach - the treatment starts at once).
			Place(World, A, State.Stage == EStage::CancelStart ? B->GetActorLocation() - State.Forward * 120.f : State.Spot, State.Forward);
			Arm(State, FirstAid, A, Item, B);
			if (!Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			UInventoryDragDropOperation* Drag = Drawer->CreateDragOperation(bBag ? EInventoryDrawerSlot::MedicalBag : EInventoryDrawerSlot::Medkit);
			Check(State, Drag && Drag->Sender.Get() == A, FString::Printf(TEXT("[%s] the drawer's %s line drags (the leader's stock)"), ItemKey(Item), ItemKey(Item)));
			if (!Drag)
			{
				return Finish(State, false);
			}
			ETransferRequestOutcome Outcome = ETransferRequestOutcome::Failed;
			if (bBag)
			{
				Outcome = Hud->HandleTransferDropOnActor(Drag->Sender.Get(), Drag->Item, B, B->GetActorLocation()); // dropped on his 3D model
			}
			else
			{
				Outcome = Bar->HandleTransferDropOnSlot(SlotOf(Bar, B), Drag->Sender.Get(), Drag->Item); // dropped on his portrait
			}
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::Aid && FirstAid->IsInTreatment(B) && Outcome != ETransferRequestOutcome::Failed,
				FString::Printf(TEXT("[%s] the drop on the heavily wounded starts first aid (outcome %d, %s)"), ItemKey(Item), static_cast<int32>(Outcome),
					Running ? (Running->bApproached ? TEXT("walking over") : TEXT("in reach")) : TEXT("no treatment")));
			Check(State, UFirstAidSubsystem::CountOf(*A, Item) == State.CountBefore && UFirstAidSubsystem::CountOf(*B, Item) == (bBag ? 0 : 2),
				FString::Printf(TEXT("[%s] not handed over, not used yet"), ItemKey(Item)));
			Next(State, State.Stage == EStage::MedkitAidStart ? EStage::MedkitAidRun : (bBag ? EStage::BagAidRun : EStage::CancelRun));
			return true;
		}
		case EStage::MedkitAidRun:
		case EStage::BagAidRun:
		{
			const bool bBag = State.Stage == EStage::BagAidRun;
			const EFirstAidItem Item = bBag ? EFirstAidItem::MedicalBag : EFirstAidItem::Medkit;
			const FItemConfig& Data = Config.Item(Item);
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			if (!Record)
			{
				const FFirstAidRecord* Running = FirstAid->FindRunning(A);
				if (Running && Running->Frames > 0)
				{
					Sample(State);
					SamplePatientBody(State);
				}
				SetHealthShare(A, 1.f);
				if (State.StageTime > StageLimit)
				{
					Check(State, false, FString::Printf(TEXT("[%s] the aid never ended"), ItemKey(Item)));
					return Finish(State, false);
				}
				return true;
			}
			if (State.bRecordChecked)
			{
				// R17-6: the patient's clip blends out (his capsule turns back), then he gets up.
				if (FirstAid->IsSettling(B) && State.StageTime < StageLimit + 8.f)
				{
					SamplePatientBody(State);
					return true;
				}
				const float BodyRate = MaxTurnRate(State.Samples.BodyYaw, 0.2f);
				const float CapsuleRate = MaxTurnRate(State.Samples.CapsuleYaw, 0.2f);
				const float BodyEnd = State.Samples.BodyYaw.IsEmpty() ? 0.f
					: FMath::Abs(FRotator3f::NormalizeAxis(static_cast<float>(State.Samples.BodyYaw.Last().Y - State.Samples.BodyYaw[0].Y)));
				Check(State, State.Samples.BodyYaw.Num() > 30 && BodyRate < Config.Patient.MaxBodyTurnDegPerSecond,
					FString::Printf(TEXT("[%s] the patient's body never spins: its long axis turns at most %.0f deg/s (< %.0f; %d samples, %.0f deg start -> end; his capsule %.0f deg/s under it, clip '%s')"),
						ItemKey(Item), BodyRate, Config.Patient.MaxBodyTurnDegPerSecond, State.Samples.BodyYaw.Num(), BodyEnd, CapsuleRate, *State.AidPatientClip));
				Check(State, FMath::Abs(FRotator3f::NormalizeAxis(static_cast<float>(B->GetActorRotation().Yaw) - State.PatientYaw0)) < 5.f,
					FString::Printf(TEXT("[%s] his capsule back on its yaw after the blend-out (%.0f -> %.0f)"), ItemKey(Item), State.PatientYaw0, B->GetActorRotation().Yaw));
				Check(State, B->GetStance() == EOperativeStance::Standing && FirstAid->GetStandUps() == State.StandUpsBefore + 1,
					FString::Printf(TEXT("[%s] the patched-up patient gets up after the aid (crawl end 'stand': stance %d, stand-ups %d -> %d)"), ItemKey(Item),
						static_cast<int32>(B->GetStance()), State.StandUpsBefore, FirstAid->GetStandUps()));
				State.Report.Add(FString::Printf(TEXT("%s aid: patient body %.0f deg/s max (capsule %.0f deg/s), clip %s"), ItemKey(Item), BodyRate, CapsuleRate,
					*State.AidPatientClip));
				Next(State, bBag ? EStage::CancelStart : EStage::BagAidStart);
				return true;
			}
			State.bRecordChecked = true;
			State.AidPatientClip = Record->PatientClip;
			const float Gain = Record->HealthAfter - Record->HealthBefore;
			Check(State, Record->bCompleted && Record->bApplied && Record->bApproached,
				FString::Printf(TEXT("[%s] walked over and completed (%.1f / %.1f s, interrupt %s)"), ItemKey(Item), Record->Elapsed, Record->Seconds,
					InterruptName(Record->Interrupt)));
			Check(State, Data.HelperClips.Contains(Record->HelperClip) && State.Samples.HelperClipFrames >= State.Samples.Frames * 8 / 10 && State.Samples.MaxHelperSlot > 0.9f,
				FString::Printf(TEXT("[%s] the helper's clip '%s' (the %s set) on the FullBody slot %d / %d frames"), ItemKey(Item), *Record->HelperClip, ItemKey(Item),
					State.Samples.HelperClipFrames, State.Samples.Frames));
			Check(State, Record->PatientPose == EPatientPose::Crawl && PatientClipSet(Config, Data, Record->PatientPose).Contains(Record->PatientClip)
				&& State.Samples.PatientClipFrames >= State.Samples.Frames * 8 / 10,
				FString::Printf(TEXT("[%s] the wounded (pose '%s') plays '%s' of the crawl set %d / %d frames"), ItemKey(Item), PatientPoseKey(Record->PatientPose),
					*Record->PatientClip, State.Samples.PatientClipFrames, State.Samples.Frames));
			Check(State, Record->PatientClip == (bBag ? State.BagPredicted : State.MedkitPredicted),
				FString::Printf(TEXT("[%s] seed %d picked its predicted variant ('%s')"), ItemKey(Item), bBag ? State.BagSeed : State.MedkitSeed,
					bBag ? *State.BagPredicted : *State.MedkitPredicted));
			if (bBag)
			{
				Check(State, Record->PatientClip != State.MedkitPatientClip,
					FString::Printf(TEXT("[variants] two seeds, two variants: '%s' / '%s'"), *State.MedkitPatientClip, *Record->PatientClip));
			}
			else
			{
				State.MedkitPatientClip = Record->PatientClip;
			}
			{
				// Beside him (not at his head / feet), facing his chest (helper_beside_lying, helper_chest_cm), on his lying line (PatientYaw0).
				const FVector Forward0 = FRotator(0.f, State.PatientYaw0, 0.f).Vector();
				const FVector Right0 = FRotator(0.f, State.PatientYaw0 + 90.f, 0.f).Vector();
				const FVector Offset = A->GetActorLocation() - B->GetActorLocation();
				const FVector Chest = B->GetActorLocation() + Forward0 * Config.Patient.HelperChestCm;
				const float FaceError = FMath::Abs(FRotator3f::NormalizeAxis(static_cast<float>(A->GetActorRotation().Yaw - (Chest - A->GetActorLocation()).Rotation().Yaw)));
				Check(State, FMath::Abs(FVector::DotProduct(Offset, Right0)) > FMath::Abs(FVector::DotProduct(Offset, Forward0)) && FaceError < 25.f,
					FString::Printf(TEXT("[%s] the helper kneels at his side (%.0f cm across, %.0f along his body) facing his chest (%.0f deg off)"), ItemKey(Item),
						FVector::DotProduct(Offset, Right0), FVector::DotProduct(Offset, Forward0), FaceError));
			}
			Check(State, FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) <= Config.ReachCm + 30.f,
				FString::Printf(TEXT("[%s] the helper beside the wounded (%.0f cm)"), ItemKey(Item), FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation())));
			Check(State, Gain > 1.f && static_cast<int32>(Record->TierAfter) < static_cast<int32>(Record->TierBefore),
				FString::Printf(TEXT("[%s] +%.0f HP, the tier improves %s -> %s"), ItemKey(Item), Gain, TierKey(Record->TierBefore), TierKey(Record->TierAfter)));
			Check(State, UFirstAidSubsystem::CountOf(*A, Item) == State.CountBefore - 1, FString::Printf(TEXT("[%s] one used (%d -> %d)"), ItemKey(Item),
				State.CountBefore, UFirstAidSubsystem::CountOf(*A, Item)));
			Check(State, FMath::IsNearlyEqual(Record->Seconds, TreatmentSeconds(Config, Item, ETier::Heavy, false), 0.01f),
				FString::Printf(TEXT("[%s] the aid's length from first_aid.json (%.1f s)"), ItemKey(Item), Record->Seconds));
			if (bBag)
			{
				Check(State, Record->Seconds > State.MedkitSeconds && Gain > State.MedkitGain,
					FString::Printf(TEXT("[bag] longer (%.1f vs %.1f s) and more HP (+%.0f vs +%.0f) than the medkit"), Record->Seconds, State.MedkitSeconds, Gain,
						State.MedkitGain));
			}
			else
			{
				State.MedkitGain = Gain;
				State.MedkitSeconds = Record->Seconds;
				Check(State, Record->HelperClip.Contains(TEXT("Fast")), FString::Printf(TEXT("[medkit] the FAST helper variant (%s)"), *Record->HelperClip));
			}
			State.Report.Add(FString::Printf(TEXT("%s aid: helper %s, patient %s (%s), %.1f s, %.0f -> %.0f HP"), ItemKey(Item), *Record->HelperClip, *Record->PatientClip,
				PatientPoseKey(Record->PatientPose), Record->Seconds, Record->HealthBefore, Record->HealthAfter));
			return true;
		}
		case EStage::CancelRun:
		{
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			SetHealthShare(A, 1.f);
			if (Running && Running->Frames > 0)
			{
				Sample(State);
			}
			// Mid-treatment (before the apply moment): a move order for the helper.
			const float Before = TreatmentSeconds(Config, EFirstAidItem::Medkit, ETier::Heavy, false) * Config.Medkit.ApplyAt * 0.5f;
			if (!Running || State.StageTime < Before)
			{
				if (!Running && !NewRecord(FirstAid, State))
				{
					Check(State, false, TEXT("[cancel] no treatment running"));
					return Finish(State, false);
				}
				return !Running ? (Next(State, EStage::CancelCheck), true) : true;
			}
			Check(State, State.Samples.HelperClipFrames > 0, TEXT("[cancel] the clip plays before the order"));
			const EOperativeOrderResult Order = A->OrderMoveTo(State.Spot - State.Forward * 300.f, false);
			Check(State, Order == EOperativeOrderResult::Accepted, TEXT("[cancel] the move order accepted"));
			Next(State, EStage::CancelCheck);
			return true;
		}
		case EStage::CancelCheck:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			Check(State, Record && !Record->bCompleted && Record->Interrupt == EInterrupt::Order && !Record->bApplied,
				FString::Printf(TEXT("[cancel] interrupted by the order before the apply moment (%s, %.1f s)"), Record ? InterruptName(Record->Interrupt) : TEXT("no record"),
					Record ? Record->Elapsed : -1.f));
			Check(State, A->MedkitsCount == State.CountBefore && Record && !Record->bConsumed,
				FString::Printf(TEXT("[cancel] the medkit kept (consume_on_cancel false: %d -> %d)"), State.CountBefore, A->MedkitsCount));
			Check(State, FMath::IsNearlyEqual(Health(B), State.HealthBefore, 0.5f), FString::Printf(TEXT("[cancel] no heal (%.0f HP)"), Health(B)));
			const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(A);
			const UOperativeAnimInstance* PatientAnim = UOperativeAnimInstance::Of(B);
			Check(State, HelperAnim && !HelperAnim->IsFirstAidClipActive() && PatientAnim && !PatientAnim->IsFirstAidClipActive() && !FirstAid->IsInTreatment(A),
				TEXT("[cancel] both clips stopped, no treatment left"));
			Next(State, EStage::Done);
			return true;
		}
		default:
			if (Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			return Finish(State, true);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FirstAidSmoke"),
		TEXT("Dev check (the game designer 2026-11-16, L_MovementTest): first aid by wound tier through the drawer / drag input - light = standing self clip, ")
		TEXT("medium = kneeling, heavy = refused on himself, a mate treats him (medkit fast via the portrait, the medical bag longer + stronger via the model), ")
		TEXT("a move order cancels before the apply moment (the medkit kept); R17-6: two seeds pick two crawl variants, the lying patient's body never spins, ")
		TEXT("the helper kneels at his side, the patient gets up after the aid. Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
