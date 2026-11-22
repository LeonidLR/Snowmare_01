// Dev-only check of the data-driven cover animation sets (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md): on a generated map with
// buildings (default the dressed /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit) the leader is put inside a building (the roof cutaway's storey
// index; building=<id>, default the first indexed one) and runs the cover states at an inside wall: hug (stand), a long walk along the wall
// (start / loop / stop), reload, grenade throw, corner peek-aim, crouched hug + crouched peek, and at a LOW inside cover (a window sill) the
// over-the-top fire set; each must play the indoor set's clips (AS_Cover_*, set "riffle", context "indoor"). Control: the same man at a wall
// OUTSIDE the buildings keeps the native M4 set. shots=1 (needs rendering) writes <dir>/<NN>_<pose>.png per pose. PASS / FAIL, then exits.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -RenderOffscreen -ResX=1600 -ResY=900 -nosound
//     -LevelJson=L_Gen_StoryShowcase_Kit.json -ExecCmds="CodexTactics.CoverIndoorSmoke shots=1 dir=<abs>"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace CoverIndoorSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 MaxFrames = 60 * 300;

	enum class EStep : uint8
	{
		Warmup, Place, Hug, Walk, Reload, Grenade, Peek, Crouch, CrouchPeek, Window, Outdoor, Done
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		double StepStart = 0.0;
		int32 Failures = 0;
		FString BuildingArg;
		FString BuildingId;
		bool bShots = false;
		FString Dir;
		int32 ShotIndex = 0;
		TArray<FString> PendingShots;
		FDelegateHandle EndFrameHandle;
		FCoverSlot High;
		FCoverSlot Low;
		bool bHaveLow = false;
		FVector Spot = FVector::ZeroVector;
		bool bOrdered = false;
		bool bShotTaken = false;
		bool bOutdoorOrdered = false;
		TSet<FString> KindsSeen;
		TSet<FString> ClipsSeen;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<ATargetPoint> Target;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("CoverIndoorSmoke"));
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter& Op)
	{
		return Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
	}

	FString LastClip(const UOperativeAnimInstance& Anim)
	{
		const TArray<FString>& Log = Anim.GetCoverClipLog();
		return Log.IsEmpty() ? FString(TEXT("none")) : Log.Last();
	}

	void Put(AOperativeCharacter& Member, const FVector& Spot)
	{
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Member.GetActorRotation(), false, true);
		Member.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** A navmesh spot on the building's ground storey: inside its largest slab, the first that lands on that floor. */
	bool FindInsideSpot(UWorld* World, const URoofCutawaySubsystem& Roofs, const FString& Building, FVector& OutSpot)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TArray<FBox> Slabs = Roofs.GetStoreySlabs(Building);
		Slabs.Sort([](const FBox& A, const FBox& B) { return A.Min.Z < B.Min.Z; });
		const float Fractions[] = { 0.5f, 0.3f, 0.7f, 0.2f, 0.8f };
		for (const FBox& Slab : Slabs)
		{
			for (const float FX : Fractions)
			{
				for (const float FY : Fractions)
				{
					const FVector Try(FMath::Lerp(Slab.Min.X, Slab.Max.X, FX), FMath::Lerp(Slab.Min.Y, Slab.Max.Y, FY), Slab.Max.Z + 40.f);
					FNavLocation OnNav;
					if (Nav && Nav->ProjectPointToNavigation(Try, OnNav, FVector(60.f, 60.f, 90.f)) && FMath::Abs(OnNav.Location.Z - Slab.Max.Z) < 60.f)
					{
						OutSpot = OnNav.Location;
						return true;
					}
				}
			}
		}
		return false;
	}

	/** Walls round Spot (16 rays at chest height): a high cover slot with an exposed edge first, and a low one (a window sill). */
	void FindWalls(UWorld* World, const AOperativeCharacter& Op, const FVector& Spot, FRunner& R, float MaxCm)
	{
		R.High = FCoverSlot();
		R.bHaveLow = false;
		float BestHigh = TNumericLimits<float>::Max();
		float BestLow = TNumericLimits<float>::Max();
		FCollisionQueryParams Params(TEXT("CoverIndoorSmoke"), false, &Op);
		for (int32 Ray = 0; Ray < 16; ++Ray)
		{
			const FVector Dir = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
			for (const float Height : { 100.f, 60.f })
			{
				const FVector From = Spot + FVector(0.f, 0.f, Height);
				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(Hit, From, From + Dir * MaxCm, ECC_Visibility, Params))
				{
					continue;
				}
				FCoverSlot Slot;
				if (!CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, Dir, Slot, FCoverTraceConfig(), { &Op }))
				{
					continue;
				}
				const float Dist = static_cast<float>(FVector::Dist2D(Spot, Slot.WorldLocation));
				const float Room = FMath::Max(Slot.LeftEdgeDistanceCm, Slot.RightEdgeDistanceCm);
				if (Slot.Height == ECoverHeight::HighCover && Slot.HasExposedEdge() && Room > 280.f && Dist < BestHigh)
				{
					BestHigh = Dist;
					R.High = Slot;
				}
				else if (Slot.Height == ECoverHeight::LowCover && Dist < BestLow)
				{
					BestLow = Dist;
					R.Low = Slot;
					R.bHaveLow = true;
				}
			}
		}
	}

	void AimCamera(UWorld* World, const AOperativeCharacter& Op, const FCoverSlot& Slot)
	{
		FRunner& R = *Runner;
		if (!R.bShots)
		{
			return;
		}
		if (!R.Camera.IsValid())
		{
			R.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
		}
		ACameraActor* Camera = R.Camera.Get();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Camera || !PC)
		{
			return;
		}
		const FVector Chest = Op.GetActorLocation() + FVector(0.f, 0.f, 20.f);
		const FVector Eye = Chest + Slot.WallNormal * 330.f + Slot.RightTangent() * 120.f + FVector(0.f, 0.f, 120.f);
		Camera->SetActorLocationAndRotation(Eye, (Chest - Eye).Rotation());
		PC->SetViewTarget(Camera);
	}

	void Shot(const FString& Name)
	{
		FRunner& R = *Runner;
		if (!R.bShots)
		{
			return;
		}
		const FString File = R.Dir / FString::Printf(TEXT("%02d_%s.png"), ++R.ShotIndex, *Name);
		FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		R.PendingShots.Add(File);
		UE_LOG(LogCodexTactics, Display, TEXT("CoverIndoorSmoke: shot %s"), *File);
	}

	void Observe(const UOperativeAnimInstance& Anim)
	{
		if (!Anim.GetCoverSetOneShotKind().IsEmpty())
		{
			Runner->KindsSeen.Add(Anim.GetCoverSetOneShotKind());
		}
		Runner->ClipsSeen.Add(LastClip(Anim));
	}

	bool SawClip(const TCHAR* Part)
	{
		for (const FString& Clip : Runner->ClipsSeen)
		{
			if (Clip.Contains(Part))
			{
				return true;
			}
		}
		return false;
	}

	void Tick()
	{
		if (!Runner.IsValid())
		{
			return;
		}
		FRunner& R = *Runner;
		if (R.Step == EStep::Done)
		{
			return;
		}
		if (++R.Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		URoofCutawaySubsystem* Roofs = World ? World->GetSubsystem<URoofCutawaySubsystem>() : nullptr;
		AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
		UOperativeAnimInstance* Anim = Op ? AnimOf(*Op) : nullptr;
		if (!World || !Roofs || !Op || !Anim)
		{
			return;
		}
		if (R.bShots)
		{
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
		}
		const double Now = World->GetTimeSeconds();
		const double T = Now - R.StepStart;
		auto Go = [&R, Now](EStep Step)
		{
			R.Step = Step;
			R.StepStart = Now;
			R.bOrdered = false;
			R.bShotTaken = false;
			R.KindsSeen.Reset();
			R.ClipsSeen.Reset();
		};
		auto ShotOnce = [&R](double At, double TNow, const TCHAR* Name)
		{
			if (!R.bShotTaken && TNow >= At)
			{
				R.bShotTaken = true;
				Shot(Name);
			}
		};
		auto InSetCheck = [&](const TCHAR* What, const TCHAR* ClipPart)
		{
			Check(Anim->GetActiveCoverAnimSet() == TEXT("riffle") && Anim->GetCoverAnimContext() == TEXT("indoor"),
				FString::Printf(TEXT("%s: set '%s', context %s"), What, *Anim->GetActiveCoverAnimSet(), *Anim->GetCoverAnimContext()));
			Check(SawClip(ClipPart), FString::Printf(TEXT("%s: a clip with '%s' played (last %s)"), What, ClipPart, *LastClip(*Anim)));
		};
		if (Op->bInCover)
		{
			Observe(*Anim);
		}
		switch (R.Step)
		{
		case EStep::Warmup:
		{
			if (R.Frame < WarmupFrames || Roofs->GetStoreyBuildings().IsEmpty())
			{
				if (R.Frame > WarmupFrames * 4)
				{
					Check(false, TEXT("no building in the storey index (needs a generated map with buildings)"));
					Finish();
				}
				return;
			}
			if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World); Environment && R.bShots)
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(13.f);
				GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
			}
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
			for (const TPair<FString, int32>& Building : Roofs->GetStoreyBuildings())
			{
				if (R.BuildingArg.IsEmpty() || Building.Key == R.BuildingArg)
				{
					R.BuildingId = Building.Key;
					if (FindInsideSpot(World, *Roofs, Building.Key, R.Spot))
					{
						break;
					}
				}
			}
			Check(!R.BuildingId.IsEmpty() && !R.Spot.IsZero(), FString::Printf(TEXT("a spot inside %s"), *R.BuildingId));
			if (R.Spot.IsZero())
			{
				Finish();
				return;
			}
			Put(*Op, R.Spot);
			Go(EStep::Place);
			return;
		}
		case EStep::Place:
			if (T < 1.0)
			{
				return;
			}
			FindWalls(World, *Op, Op->GetActorLocation() - FVector(0.f, 0.f, Op->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), R, 900.f);
			Check(R.High.IsValid(), FString::Printf(TEXT("an inside high wall with an exposed edge near %s (low / window cover: %s)"), *R.Spot.ToString(),
				R.bHaveLow ? TEXT("yes") : TEXT("none")));
			if (!R.High.IsValid())
			{
				Finish();
				return;
			}
			{
				FString Building;
				int32 Storey = INDEX_NONE;
				Check(Roofs->GetOperativeStorey(*Op, Building, Storey), FString::Printf(TEXT("the storey rule puts him in %s (storey %d)"), *Building, Storey));
			}
			Go(EStep::Hug);
			return;
		case EStep::Hug:
			if (!R.bOrdered)
			{
				R.bOrdered = true;
				Op->SetStance(EOperativeStance::Standing);
				Check(Op->OrderTakeCover(R.High, false) == EOperativeOrderResult::Accepted, TEXT("take cover at the inside wall"));
				return;
			}
			if (!Op->bInCover && T < 12.0)
			{
				return;
			}
			if (T < 3.0 || (Op->bInCover && R.ClipsSeen.IsEmpty()))
			{
				AimCamera(World, *Op, Op->GetCoverSlot());
				return;
			}
			ShotOnce(3.0, T, TEXT("stand_hug"));
			if (T < 3.6)
			{
				return;
			}
			Check(Op->bInCover, TEXT("in cover inside"));
			InSetCheck(TEXT("standing hug"), TEXT("AS_Cover_Stand"));
			Go(EStep::Walk);
			return;
		case EStep::Walk:
		{
			if (!R.bOrdered)
			{
				R.bOrdered = true;
				const FCoverSlot& Here = Op->GetCoverSlot();
				const float Sign = Here.RightEdgeDistanceCm >= Here.LeftEdgeDistanceCm ? 1.f : -1.f;
				const float Along = FMath::Min(260.f, FMath::Max(Here.RightEdgeDistanceCm, Here.LeftEdgeDistanceCm) - 60.f);
				FCoverSlot Target;
				const bool bFound = CoverTraceRules::FindShimmySlot(World, Here, Here.WorldLocation + Here.RightTangent() * Sign * Along, Target);
				Check(bFound && Op->OrderShimmyTo(Target) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("a %.0f cm walk along the wall"), Along));
				return;
			}
			AimCamera(World, *Op, Op->GetCoverSlot());
			ShotOnce(1.0, T, TEXT("stand_walk"));
			if ((Op->bShimmying || T < 1.2) && T < 10.0)
			{
				return;
			}
			InSetCheck(TEXT("walk along the wall"), TEXT("Walk"));
			Check(R.KindsSeen.Contains(TEXT("walk_start")), TEXT("walk >= 1.5 m: the start clip played (Q5)"));
			Check(R.KindsSeen.Contains(TEXT("walk_stop")) || SawClip(TEXT("Walk_Stop")) || SawClip(TEXT("Walk_End")), TEXT("walk >= 1.5 m: the stop clip played (Q5)"));
			Go(EStep::Reload);
			return;
		}
		case EStep::Reload:
			if (!R.bOrdered)
			{
				if (T < 1.0)
				{
					return;
				}
				R.bOrdered = true;
				Op->StartReload();
				return;
			}
			ShotOnce(1.6, T, TEXT("stand_reload"));
			if (T < 2.2 || (Op->bIsReloading && T < 8.0))
			{
				return;
			}
			Check(R.KindsSeen.Contains(TEXT("reload")), TEXT("the set's cover reload played"));
			InSetCheck(TEXT("reload"), TEXT("_Reload"));
			Go(EStep::Grenade);
			return;
		case EStep::Grenade:
			if (!R.bOrdered)
			{
				if (T < 1.0)
				{
					return;
				}
				R.bOrdered = true;
				Op->OnGrenadeThrowNative.Broadcast(); // the throw's animation event (no grenade is spawned by the event itself)
				return;
			}
			ShotOnce(1.6, T, TEXT("stand_grenade"));
			if (T < 3.2)
			{
				return;
			}
			Check(R.KindsSeen.Contains(TEXT("grenade")), TEXT("the set's grenade throw from cover played"));
			InSetCheck(TEXT("grenade"), TEXT("_Grenade_Throw"));
			Check(Op->GrenadeReleaseSeconds > 0.f && Op->GrenadeReleaseSeconds < Op->GrenadeThrowDuration,
				FString::Printf(TEXT("release %.2f s of %.2f s"), Op->GrenadeReleaseSeconds, Op->GrenadeThrowDuration));
			Go(EStep::Peek);
			return;
		case EStep::Peek:
		case EStep::CrouchPeek:
		{
			const bool bCrouch = R.Step == EStep::CrouchPeek;
			if (!R.bOrdered)
			{
				if (T < 1.0)
				{
					return;
				}
				R.bOrdered = true;
				const FCoverSlot& Here = Op->GetCoverSlot();
				const ECoverFacing Side = Here.IsEdgeExposed(ECoverFacing::Right) ? ECoverFacing::Right : ECoverFacing::Left;
				const float Edge = Side == ECoverFacing::Right ? Here.RightEdgeDistanceCm : -Here.LeftEdgeDistanceCm;
				Check(Op->OrderCornerPeek(Side, Edge) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s corner peek ordered"), bCrouch ? TEXT("crouched") : TEXT("standing")));
				return;
			}
			AimCamera(World, *Op, Op->GetCoverSlot());
			ShotOnce(5.0, T, bCrouch ? TEXT("crouch_peek") : TEXT("stand_peek"));
			if (T < 5.6)
			{
				return;
			}
			Check(Anim->IsCoverInFirePose(), TEXT("in the fire stance at the corner"));
			InSetCheck(bCrouch ? TEXT("crouched peek-aim") : TEXT("standing peek-aim"), TEXT("Aim"));
			Check(!Anim->IsCoverOverTopFireSet(), TEXT("the corner uses the side peek set"));
			Go(bCrouch ? (R.bHaveLow ? EStep::Window : EStep::Outdoor) : EStep::Crouch);
			return;
		}
		case EStep::Crouch:
			if (!R.bOrdered)
			{
				R.bOrdered = true;
				Op->OrderTakeCover(Op->GetCoverSlot(), false); // ends the peek, back to the wall
				Op->SetStance(EOperativeStance::Crouching);
				return;
			}
			AimCamera(World, *Op, Op->GetCoverSlot());
			ShotOnce(3.0, T, TEXT("crouch_hug"));
			if (T < 3.6)
			{
				return;
			}
			InSetCheck(TEXT("crouched hug"), TEXT("AS_Cover_Crouch"));
			Go(EStep::CrouchPeek);
			return;
		case EStep::Window:
			if (!R.bOrdered)
			{
				R.bOrdered = true;
				Op->SetStance(EOperativeStance::Crouching);
				Check(Op->OrderTakeCover(R.Low, false) == EOperativeOrderResult::Accepted, TEXT("take cover at the low inside cover (window sill)"));
				R.Target = World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(),
					FTransform(R.Low.WallPoint - R.Low.WallNormal * 900.f + FVector(0.f, 0.f, 60.f)));
				return;
			}
			if (Op->bInCover && T > 2.0 && FMath::Fmod(T, 1.0) < 0.05 && R.Target.IsValid())
			{
				Op->PlayCoverShot(R.Target.Get(), false); // a threat beyond the sill: the fire stance over the top
			}
			AimCamera(World, *Op, Op->GetCoverSlot());
			ShotOnce(6.0, T, TEXT("window_over_top"));
			if (T < 6.6)
			{
				return;
			}
			Check(Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("in low cover"));
			Check(Anim->IsCoverOverTopFireSet() || Op->bAtCoverCorner, TEXT("away from its corner: the over-the-top set (Aim/2)"));
			InSetCheck(TEXT("window over the top"), Op->bAtCoverCorner ? TEXT("Aim") : TEXT("Aim2"));
			Go(EStep::Outdoor);
			return;
		case EStep::Outdoor:
		{
			if (!R.bOrdered)
			{
				R.bOrdered = true;
				// Outside the building: the nearest navmesh point 6 m beyond its slabs, then a wall there.
				FBox Box(ForceInit);
				for (const FBox& Slab : Roofs->GetStoreySlabs(R.BuildingId))
				{
					Box += Slab;
				}
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				FNavLocation Out;
				const FVector Try(Box.Max.X + 600.f, Box.GetCenter().Y, Box.Min.Z + 40.f);
				if (Nav && Nav->ProjectPointToNavigation(Try, Out, FVector(400.f, 400.f, 200.f)))
				{
					Op->SetStance(EOperativeStance::Standing);
					Put(*Op, Out.Location);
				}
				return;
			}
			if (T < 1.0)
			{
				return;
			}
			if (!R.bOutdoorOrdered)
			{
				R.bOutdoorOrdered = true;
				FindWalls(World, *Op, Op->GetActorLocation() - FVector(0.f, 0.f, Op->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), R, 1500.f);
				if (!R.High.IsValid() || Op->OrderTakeCover(R.High, false) != EOperativeOrderResult::Accepted)
				{
					Check(true, TEXT("outdoor control skipped: no wall outside within 15 m"));
					Finish();
				}
				return;
			}
			if (!Op->bInCover && T < 14.0)
			{
				return;
			}
			if (T < 4.0)
			{
				return;
			}
			FString Building;
			int32 Storey = INDEX_NONE;
			const bool bInside = Roofs->GetOperativeStorey(*Op, Building, Storey);
			Check(!bInside ? Anim->GetActiveCoverAnimSet().IsEmpty() : true,
				FString::Printf(TEXT("outdoor control: in cover %d, inside %d, set '%s' (native expected), last clip %s"), Op->bInCover ? 1 : 0, bInside ? 1 : 0,
					*Anim->GetActiveCoverAnimSet(), *LastClip(*Anim)));
			Finish();
			return;
		}
		default:
			return;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CoverIndoor"));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("building=")))
			{
				Runner->BuildingArg = Arg.Mid(9);
			}
			else if (Arg.StartsWith(TEXT("shots=")))
			{
				Runner->bShots = Arg.Mid(6) != TEXT("0");
			}
			else if (Arg.StartsWith(TEXT("dir=")))
			{
				Runner->Dir = Arg.Mid(4);
			}
		}
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		if (Runner->bShots)
		{
			IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("CoverIndoorSmoke: building %s, shots %d -> %s"), Runner->BuildingArg.IsEmpty() ? TEXT("(auto)") : *Runner->BuildingArg,
			Runner->bShots ? 1 : 0, *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverIndoorSmoke"),
		TEXT("Dev check (designer 2026-11-22): the indoor cover set inside a generated building - hug, walk start / stop, reload, grenade, peek-aim standing / crouched, ")
		TEXT("window over-the-top; outside the native set. [building=<id>] [shots=1 dir=<abs>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
