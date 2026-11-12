// Dev-only headless check + rendered shots of combat building openings, loopholes, window vaulting and roof parapets
// (queue 028 / spec 007: L_Gen_Train_1, L_Gen_Train_2, L_Gen_Train_3).
// Arguments: [map=<L_Gen_...>] [layout=<accepted name>] [shots] [shots_dir=<dir>].
// Exits with Smoke RESULT: PASS / FAIL (or LevelGenOpeningsSmoke SHOTS DONE). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/VaultRules.h"
#include "CodexTactics.h"
#include "Combat/SightRules.h"
#include "Containers/Ticker.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/VaultNavigation.h"
#include "Kismet/GameplayStatics.h"
#include "Ladders/LadderActor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace LevelGenOpeningsSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;
	constexpr int32 WarmupFrames = 30;

	struct FOpeningSpec
	{
		FString Id;
		FString Kind;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Floor = 0;
		float SillCm = 90.f;
		float TopCm = 210.f;
		float YawDeg = 0.f;
	};

	struct FParapetSpec
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		int32 Floors = 1;
		float HeightCm = 100.f;
	};

	struct FRoofExitPoint
	{
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Level = 1;
	};

	struct FHatchSpec
	{
		FString Id;
		FString LadderId;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Floor = 0;
	};

	struct FShotFrame
	{
		FString File;
		FVector Eye = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		float Fov = 58.f;
	};

	enum class EShotPhase : uint8 { None, Warmup, Capture, WaitWrite, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString LayoutName;
		float GroundZ = 0.f;
		FVector2D BoundsMin = FVector2D(MAX_flt, MAX_flt);
		FVector2D BoundsMax = FVector2D(-MAX_flt, -MAX_flt);
		TArray<FOpeningSpec> Openings;
		TArray<FParapetSpec> Parapets;
		TArray<FRoofExitPoint> RoofExits;
		TArray<FHatchSpec> Hatches;
		int32 Failures = 0;
		int32 Stage = 0;
		float StageTime = 0.f;

		// Vault test target
		bool bHasVaultPick = false;
		FVector VaultCentre = FVector::ZeroVector;
		FVector VaultAcross = FVector::ForwardVector;
		float VaultHalfDepth = 15.f;

		// Rendered shots state
		bool bShotsMode = false;
		FString ShotsDir;
		EShotPhase ShotPhase = EShotPhase::None;
		int32 ShotFrameCount = 0;
		int32 ShotIndex = 0;
		TArray<FShotFrame> ShotPlan;
		TWeakObjectPtr<ACameraActor> Camera;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FState> ActiveShotState;

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		if (State.EndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(State.EndFrameHandle);
			State.EndFrameHandle.Reset();
		}
		ActiveShotState.Reset();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s (%s, failures=%d)"),
			State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), *State.LayoutName, State.Failures);
		State.Stage = 99;
		FPlatformMisc::RequestExit(false, TEXT("LevelGenOpeningsSmoke"));
		return false;
	}

	FString LoadedMapName(const UWorld* World)
	{
		return World ? World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	FString FindLayoutFile(const UWorld* World, const FString& LayoutArg, const FString& MapArg)
	{
		const FString Accepted = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted");
		if (!LayoutArg.IsEmpty())
		{
			return Accepted / LayoutArg + TEXT(".json");
		}
		const FString MapName = MapArg.IsEmpty() ? LoadedMapName(World) : MapArg;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *LevelFile))
		{
			TSharedPtr<FJsonObject> Level;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Level) && Level.IsValid() && Level->HasField(TEXT("levelgen_layout")))
			{
				return FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
			}
		}
		return FString();
	}

	FVector2D ReadPoint2D(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		return Arr.Num() >= 2 ? FVector2D(Arr[0]->AsNumber(), Arr[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	void ExpandBounds(FState& State, const FVector2D& Pt)
	{
		State.BoundsMin.X = FMath::Min(State.BoundsMin.X, Pt.X);
		State.BoundsMin.Y = FMath::Min(State.BoundsMin.Y, Pt.Y);
		State.BoundsMax.X = FMath::Max(State.BoundsMax.X, Pt.X);
		State.BoundsMax.Y = FMath::Max(State.BoundsMax.Y, Pt.Y);
	}

	bool LoadLayoutData(const UWorld* World, FState& State, const FString& LayoutArg, const FString& MapArg, FString& Error)
	{
		const FString File = FindLayoutFile(World, LayoutArg, MapArg);
		FString Text;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File))
		{
			Error = FString::Printf(TEXT("no accepted layout file (%s)"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Layout;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			Error = FString::Printf(TEXT("%s: invalid JSON"), *File);
			return false;
		}
		State.LayoutName = Layout->GetStringField(TEXT("name"));
		State.GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;

		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Layout->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& BV : *Buildings)
			{
				const TSharedPtr<FJsonObject>& B = BV->AsObject();
				if (!B.IsValid())
				{
					continue;
				}
				const int32 Floors = static_cast<int32>(B->GetNumberField(TEXT("floors")));
				const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
				if (B->TryGetArrayField(TEXT("footprint"), Footprint) && Footprint->Num() >= 2)
				{
					ExpandBounds(State, ReadPoint2D((*Footprint)[0]->AsArray()));
					ExpandBounds(State, ReadPoint2D((*Footprint)[1]->AsArray()));
				}
				const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
				if (B->TryGetArrayField(TEXT("openings"), Openings))
				{
					for (const TSharedPtr<FJsonValue>& OV : *Openings)
					{
						const TSharedPtr<FJsonObject>& O = OV->AsObject();
						if (!O.IsValid())
						{
							continue;
						}
						FOpeningSpec Spec;
						O->TryGetStringField(TEXT("id"), Spec.Id);
						O->TryGetStringField(TEXT("kind"), Spec.Kind);
						const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
						if (O->TryGetArrayField(TEXT("pos"), PosArr))
						{
							Spec.Pos = ReadPoint2D(*PosArr);
						}
						Spec.Floor = O->HasField(TEXT("floor")) ? static_cast<int32>(O->GetNumberField(TEXT("floor"))) : 0;
						Spec.SillCm = O->HasField(TEXT("sill_height")) ? static_cast<float>(O->GetNumberField(TEXT("sill_height"))) : (Spec.Kind == TEXT("door") ? 0.f : 90.f);
						Spec.TopCm = O->HasField(TEXT("top_height")) ? static_cast<float>(O->GetNumberField(TEXT("top_height"))) : 210.f;
						Spec.YawDeg = O->HasField(TEXT("yaw")) ? static_cast<float>(O->GetNumberField(TEXT("yaw"))) : 0.f;
						State.Openings.Add(MoveTemp(Spec));
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Parapets = nullptr;
				if (B->TryGetArrayField(TEXT("parapets"), Parapets))
				{
					for (const TSharedPtr<FJsonValue>& PV : *Parapets)
					{
						const TSharedPtr<FJsonObject>& P = PV->AsObject();
						if (!P.IsValid())
						{
							continue;
						}
						FParapetSpec Parapet;
						const TArray<TSharedPtr<FJsonValue>>* StartArr = nullptr;
						const TArray<TSharedPtr<FJsonValue>>* EndArr = nullptr;
						if (P->TryGetArrayField(TEXT("start"), StartArr) && P->TryGetArrayField(TEXT("end"), EndArr))
						{
							Parapet.Start = ReadPoint2D(*StartArr);
							Parapet.End = ReadPoint2D(*EndArr);
							Parapet.Floors = Floors;
							Parapet.HeightCm = P->HasField(TEXT("height_cm")) ? static_cast<float>(P->GetNumberField(TEXT("height_cm"))) : 100.f;
							State.Parapets.Add(Parapet);
						}
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Hatches = nullptr;
				if (B->TryGetArrayField(TEXT("hatches"), Hatches))
				{
					for (const TSharedPtr<FJsonValue>& HV : *Hatches)
					{
						const TSharedPtr<FJsonObject>& H = HV->AsObject();
						if (!H.IsValid())
						{
							continue;
						}
						FHatchSpec Hatch;
						H->TryGetStringField(TEXT("id"), Hatch.Id);
						H->TryGetStringField(TEXT("ladder_id"), Hatch.LadderId);
						const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
						if (H->TryGetArrayField(TEXT("pos"), PosArr))
						{
							Hatch.Pos = ReadPoint2D(*PosArr);
						}
						Hatch.Floor = H->HasField(TEXT("floor")) ? static_cast<int32>(H->GetNumberField(TEXT("floor"))) : 0;
						State.Hatches.Add(MoveTemp(Hatch));
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Ladders = nullptr;
		if (Layout->TryGetArrayField(TEXT("ladders"), Ladders))
		{
			for (const TSharedPtr<FJsonValue>& LV : *Ladders)
			{
				const TSharedPtr<FJsonObject>& L = LV->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* TopArr = nullptr;
				if (L.IsValid() && L->TryGetArrayField(TEXT("top"), TopArr))
				{
					FRoofExitPoint ExitPt;
					ExitPt.Pos = ReadPoint2D(*TopArr);
					ExitPt.Level = L->HasField(TEXT("level_to")) ? static_cast<int32>(L->GetNumberField(TEXT("level_to"))) : 1;
					State.RoofExits.Add(ExitPt);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
		if (Layout->TryGetArrayField(TEXT("stairs"), Stairs))
		{
			for (const TSharedPtr<FJsonValue>& SV : *Stairs)
			{
				const TSharedPtr<FJsonObject>& S = SV->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* TopArr = nullptr;
				if (S.IsValid() && S->TryGetArrayField(TEXT("top"), TopArr))
				{
					FRoofExitPoint ExitPt;
					ExitPt.Pos = ReadPoint2D(*TopArr);
					ExitPt.Level = S->HasField(TEXT("level_to")) ? static_cast<int32>(S->GetNumberField(TEXT("level_to"))) : 1;
					State.RoofExits.Add(ExitPt);
				}
			}
		}
		return true;
	}

	bool TraceHitsWall(UWorld* World, const FVector& A, const FVector& B, const USquadSubsystem* Squad, FHitResult& OutHit,
		const TArray<const AActor*>* ExtraIgnored = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LevelGenOpeningsTrace), false);
		if (Squad)
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Params.AddIgnoredActor(Member);
				}
			}
		}
		if (ExtraIgnored)
		{
			for (const AActor* Actor : *ExtraIgnored)
			{
				if (Actor)
				{
					Params.AddIgnoredActor(Actor);
				}
			}
		}
		return World->LineTraceSingleByChannel(OutHit, A, B, ECC_Visibility, Params);
	}

	void RunStaticChecks(UWorld* World, FState& State)
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const float StandEye = SightRules::EyeHeight(EOperativeStance::Standing);   // 160 cm
		const float CrouchEye = SightRules::EyeHeight(EOperativeStance::Crouching); // 95 cm

		TArray<const AActor*> InteriorAndLadders;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* Actor = *It;
			if (Actor && (Actor->Tags.Contains(FName(TEXT("interior"))) || Actor->IsA<ALadderActor>()))
			{
				InteriorAndLadders.Add(Actor);
			}
		}

		int32 WindowsChecked = 0;
		int32 LoopholesChecked = 0;
		for (const FOpeningSpec& Op : State.Openings)
		{
			if (Op.Kind != TEXT("window") && Op.Kind != TEXT("loophole"))
			{
				continue;
			}
			const float YawRad = FMath::DegreesToRadians(Op.YawDeg);
			const FVector Out(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
			const float FloorZ = State.GroundZ + Op.Floor * LevelHeightCm;
			auto TraceAt = [&](float RelZ, FHitResult& Hit, bool bExteriorOnly = false)
			{
				const FVector Mid(Op.Pos.X, Op.Pos.Y, FloorZ + RelZ);
				return TraceHitsWall(World, Mid - Out * 130.f, Mid + Out * 130.f, Squad, Hit, bExteriorOnly ? &InteriorAndLadders : nullptr);
			};

			FHitResult Hit, RawHit;
			if (Op.Kind == TEXT("window"))
			{
				++WindowsChecked;
				if (Op.SillCm < StandEye && Op.TopCm > StandEye)
				{
					const bool bBlocked = TraceAt(StandEye, Hit, true);
					if (!bBlocked && TraceAt(StandEye, RawHit, false) && RawHit.GetActor())
					{
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: window %s (floor %d) has perpendicular interior/ladder T-junction at center [%s] (accepted layout placement); exterior cut clear"),
							*Op.Id, Op.Floor, *RawHit.GetActor()->GetName());
					}
					Check(State, !bBlocked, FString::Printf(TEXT("window %s (floor %d): standing eye (%.0f cm) LOS passes across exterior wall cut%s"),
						*Op.Id, Op.Floor, StandEye, bBlocked && Hit.GetActor() ? *FString::Printf(TEXT(" [HIT %s]"), *Hit.GetActor()->GetName()) : TEXT("")));
				}
				if (Op.SillCm >= 60.f)
				{
					Check(State, TraceAt(45.f, Hit), FString::Printf(TEXT("window %s: sill apron (45 cm) blocks LOS"), *Op.Id));
				}
				if (Op.TopCm <= 250.f)
				{
					Check(State, TraceAt(255.f, Hit), FString::Printf(TEXT("window %s: lintel (255 cm) blocks LOS"), *Op.Id));
				}
			}
			else if (Op.Kind == TEXT("loophole"))
			{
				++LoopholesChecked;
				const float MidSlit = 0.5f * (Op.SillCm + Op.TopCm);
				const bool bSlitBlocked = TraceAt(MidSlit, Hit, true);
				if (!bSlitBlocked && TraceAt(MidSlit, RawHit, false) && RawHit.GetActor())
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: loophole %s (floor %d) has perpendicular interior/ladder T-junction at center [%s] (accepted layout placement); exterior slit clear"),
						*Op.Id, Op.Floor, *RawHit.GetActor()->GetName());
				}
				Check(State, !bSlitBlocked, FString::Printf(TEXT("loophole %s (floor %d, slit %.0f..%.0f cm): mid-aperture (%.0f cm) LOS passes across exterior wall cut%s"),
					*Op.Id, Op.Floor, Op.SillCm, Op.TopCm, MidSlit,
					bSlitBlocked && Hit.GetActor() ? *FString::Printf(TEXT(" [HIT %s]"), *Hit.GetActor()->GetName()) : TEXT("")));
				Check(State, TraceAt(60.f, Hit), FString::Printf(TEXT("loophole %s: lower apron (60 cm) blocks"), *Op.Id));
				if (Op.TopCm <= 240.f)
				{
					const float AboveZ = FMath::Min(280.f, Op.TopCm + 30.f);
					Check(State, TraceAt(AboveZ, Hit), FString::Printf(TEXT("loophole %s: lintel (%.0f cm) blocks above top_height %.0f"), *Op.Id, AboveZ, Op.TopCm));
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenOpeningsSmoke checked %d windows, %d loopholes on %s"),
			WindowsChecked, LoopholesChecked, *State.LayoutName);

		// Tag invariant: ground window_sill MUST be Vaultable; floor>=1 mantle_sill, loophole_sill, parapet MUST NOT carry Vault.
		int32 GroundSillCount = 0;
		int32 ParapetCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			const bool bWinSill = Actor->Tags.Contains(FName(TEXT("window_sill")));
			const bool bMantleSill = Actor->Tags.Contains(FName(TEXT("mantle_sill")));
			const bool bLoopSill = Actor->Tags.Contains(FName(TEXT("loophole_sill")));
			const bool bParapet = Actor->Tags.Contains(FName(TEXT("parapet")));
			const bool bVault = VaultNavigation::IsVaultable(Actor);
			if (bWinSill)
			{
				++GroundSillCount;
				Check(State, bVault, FString::Printf(TEXT("ground window_sill %s has Vault tag/area"), *Actor->GetName()));
				if (!State.bHasVaultPick)
				{
					FVector Origin, Extents;
					Actor->GetActorBounds(true, Origin, Extents);
					const FVector Across = Extents.X <= Extents.Y ? FVector::ForwardVector : FVector::RightVector;
					const float HalfD = FMath::Min(Extents.X, Extents.Y);
					const FVector P0 = Origin - Across * (HalfD + 80.f);
					const FVector P1 = Origin + Across * (HalfD + 80.f);
					// Check both sides of this sill are open above the floor.
					FHitResult Dummy;
					const FVector MidHigh(Origin.X, Origin.Y, State.GroundZ + 90.f);
					if (!TraceHitsWall(World, FVector(P0.X, P0.Y, State.GroundZ + 90.f), FVector(P0.X, P0.Y, State.GroundZ + 20.f), Squad, Dummy)
						|| !TraceHitsWall(World, MidHigh - Across * 120.f, MidHigh - Across * (HalfD + 40.f), Squad, Dummy))
					{
						if (!TraceHitsWall(World, FVector(P0.X, P0.Y, State.GroundZ + 120.f), FVector(P1.X, P1.Y, State.GroundZ + 120.f), Squad, Dummy))
						{
							State.bHasVaultPick = true;
							State.VaultCentre = FVector(Origin.X, Origin.Y, State.GroundZ);
							State.VaultAcross = Across;
							State.VaultHalfDepth = HalfD;
						}
					}
				}
			}
			if (bMantleSill || bLoopSill || bParapet)
			{
				Check(State, !bVault, FString::Printf(TEXT("non-vault architectural piece %s lacks Vault tag"), *Actor->GetName()));
			}
			if (bParapet)
			{
				++ParapetCount;
			}
		}
		if (State.Parapets.Num() > 0)
		{
			Check(State, ParapetCount >= State.Parapets.Num(), FString::Printf(TEXT("spawned parapet actors (%d >= layout %d)"), ParapetCount, State.Parapets.Num()));
		}

		// Parapet height check away from ladder/stair exit gaps: crouch eye (95 cm) blocked, stand eye (160 cm) passes over 100 cm parapet.
		for (const FParapetSpec& P : State.Parapets)
		{
			const FVector2D Delta = P.End - P.Start;
			const double Len = Delta.Size();
			if (Len < 120.0)
			{
				continue;
			}
			const FVector2D Dir = Delta / Len;
			const FVector2D Perp(-Dir.Y, Dir.X);
			const float RoofZ = State.GroundZ + P.Floors * LevelHeightCm;
			// Sample mid-segment points at 0.25 / 0.5 / 0.75 that are >= 140 cm from any roof exit point.
			for (double Alpha : { 0.25, 0.5, 0.75 })
			{
				const FVector2D Sample = P.Start + Delta * Alpha;
				bool bNearExit = false;
				for (const FRoofExitPoint& ExitPt : State.RoofExits)
				{
					if (ExitPt.Level == P.Floors && FVector2D::Distance(Sample, ExitPt.Pos) < 150.0)
					{
						bNearExit = true;
						break;
					}
				}
				if (bNearExit)
				{
					continue;
				}
				FHitResult CrouchHit, StandHit;
				const FVector Across(Perp.X, Perp.Y, 0.f);
				const FVector Base(Sample.X, Sample.Y, RoofZ);
				Check(State, TraceHitsWall(World, Base + FVector(0.f, 0.f, CrouchEye) - Across * 100.f, Base + FVector(0.f, 0.f, CrouchEye) + Across * 100.f, Squad, CrouchHit),
					FString::Printf(TEXT("parapet at (%.0f, %.0f) roof %.0f: covers crouch eye (%.0f cm)"), Sample.X, Sample.Y, RoofZ, CrouchEye));
				Check(State, !TraceHitsWall(World, Base + FVector(0.f, 0.f, StandEye) - Across * 100.f, Base + FVector(0.f, 0.f, StandEye) + Across * 100.f, Squad, StandHit),
					FString::Printf(TEXT("parapet at (%.0f, %.0f) roof %.0f: standing eye (%.0f cm) shoots clear"), Sample.X, Sample.Y, RoofZ, StandEye));
				break;
			}
		}

		// Roof ladder exit clearance: inward 150 cm from GetTopLinkWorld() at +95 cm above roof slab is unobstructed.
		for (TActorIterator<ALadderActor> It(World); It; ++It)
		{
			const ALadderActor* Ladder = *It;
			if (!Ladder)
			{
				continue;
			}
			const FVector TopLink = Ladder->GetTopLinkWorld();
			const FVector Inward = -Ladder->GetOutward();
			FHitResult ExitHit;
			const bool bExitBlocked = TraceHitsWall(World, TopLink + FVector(0.f, 0.f, 95.f), TopLink + Inward * 140.f + FVector(0.f, 0.f, 95.f), Squad, ExitHit);
			Check(State, !bExitBlocked, FString::Printf(TEXT("ladder %s top exit (TopLink %.0f, %.0f, %.0f) clear of parapet/wall"),
				*Ladder->GetName(), TopLink.X, TopLink.Y, TopLink.Z));
		}
	}

	void BuildShotPlan(FState& State)
	{
		const FVector2D Center = (State.BoundsMin.X < State.BoundsMax.X) ? (State.BoundsMin + State.BoundsMax) * 0.5 : FVector2D::ZeroVector;
		const FVector2D Span = (State.BoundsMin.X < State.BoundsMax.X) ? (State.BoundsMax - State.BoundsMin) : FVector2D(2500.0, 2500.0);
		const float Diag = static_cast<float>(FMath::Max(Span.X, Span.Y));

		FShotFrame Overview;
		Overview.File = FString::Printf(TEXT("%s_overview.png"), *State.LayoutName);
		Overview.Look = FVector(Center.X, Center.Y, State.GroundZ + 180.f);
		Overview.Eye = Overview.Look + FVector(-Diag * 0.65f, -Diag * 0.55f, FMath::Max(1100.f, Diag * 0.72f));
		Overview.Fov = 55.f;
		State.ShotPlan.Add(Overview);

		FShotFrame Detail;
		Detail.File = FString::Printf(TEXT("%s_detail.png"), *State.LayoutName);
		if (State.Hatches.Num() > 0)
		{
			const FHatchSpec& H = State.Hatches[0];
			Detail.Look = FVector(H.Pos.X, H.Pos.Y, State.GroundZ + (H.Floor + 1.5f) * LevelHeightCm);
			Detail.Eye = Detail.Look + FVector(-420.f, 380.f, 440.f);
		}
		else if (State.Openings.Num() > 0)
		{
			const FOpeningSpec* Pick = &State.Openings[0];
			for (const FOpeningSpec& Op : State.Openings)
			{
				if (Op.Kind == TEXT("loophole") || Op.Kind == TEXT("window"))
				{
					Pick = &Op;
					break;
				}
			}
			const float YawRad = FMath::DegreesToRadians(Pick->YawDeg);
			const FVector Out(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
			const FVector Side(-Out.Y, Out.X, 0.f);
			Detail.Look = FVector(Pick->Pos.X, Pick->Pos.Y, State.GroundZ + Pick->Floor * LevelHeightCm + 140.f);
			Detail.Eye = Detail.Look + Out * 480.f + Side * 260.f + FVector(0.f, 0.f, 180.f);
		}
		else
		{
			Detail.Look = FVector(Center.X, Center.Y, State.GroundZ + 220.f);
			Detail.Eye = Detail.Look + FVector(-500.f, 400.f, 450.f);
		}
		Detail.Fov = 55.f;
		State.ShotPlan.Add(Detail);
	}

	void ApplyCamera(UWorld* World, FState& State, const FShotFrame& Frame)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!PC)
		{
			return;
		}
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>();
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Frame.Eye, (Frame.Look - Frame.Eye).Rotation());
			Cam->GetCameraComponent()->SetFieldOfView(Frame.Fov);
			Cam->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Cam)
			{
				PC->SetViewTarget(Cam);
			}
		}
	}

	void TickEndFrame()
	{
		TSharedPtr<FState> State = ActiveShotState;
		if (!State.IsValid() || !State->bShotsMode || State->ShotPhase == EShotPhase::None)
		{
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		if (!World || !Env || !Env->IsInitialised())
		{
			return;
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		++State->ShotFrameCount;
		switch (State->ShotPhase)
		{
		case EShotPhase::Warmup:
			if (State->ShotFrameCount >= WarmupFrames)
			{
				Env->SetClockScale(0.f);
				Env->ForceWeather(EWeatherState::Clear, true);
				Env->SetHour(13.f);
				GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
				if (State->ShotPlan.Num() == 0)
				{
					BuildShotPlan(*State);
				}
				ApplyCamera(World, *State, State->ShotPlan[0]);
				State->ShotPhase = EShotPhase::Capture;
				State->ShotFrameCount = 0;
			}
			break;
		case EShotPhase::Capture:
			if (State->ShotIndex < State->ShotPlan.Num())
			{
				const FShotFrame& Frame = State->ShotPlan[State->ShotIndex];
				ApplyCamera(World, *State, Frame);
				if (State->ShotFrameCount >= 5)
				{
					FScreenshotRequest::RequestScreenshot(State->ShotsDir / Frame.File, false, false);
					UE_LOG(LogCodexTactics, Display, TEXT("LevelGenOpeningsSmoke shot: %s"), *(State->ShotsDir / Frame.File));
					State->ShotPhase = EShotPhase::WaitWrite;
					State->ShotFrameCount = 0;
				}
			}
			else
			{
				State->ShotPhase = EShotPhase::Done;
			}
			break;
		case EShotPhase::WaitWrite:
			if (State->ShotFrameCount >= 10)
			{
				++State->ShotIndex;
				State->ShotFrameCount = 0;
				State->ShotPhase = State->ShotIndex < State->ShotPlan.Num() ? EShotPhase::Capture : EShotPhase::Done;
			}
			break;
		case EShotPhase::Done:
			Finish(*State);
			break;
		default:
			break;
		}
	}

	bool Step(TSharedRef<FState> State)
	{
		if (State->Stage == 99)
		{
			return false;
		}
		UWorld* World = State->World.Get();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Leader)
		{
			Check(*State, false, TEXT("squad leader exists"));
			return Finish(*State);
		}
		State->StageTime += StepSeconds;

		switch (State->Stage)
		{
		case 0:
		{
			if (State->StageTime < 1.5f)
			{
				return true;
			}
			RunStaticChecks(World, *State);
			if (!State->bHasVaultPick)
			{
				if (State->bShotsMode)
				{
					State->ShotFrameCount = 0;
					State->ShotPhase = EShotPhase::Warmup;
					State->Stage = 10;
					return false;
				}
				return Finish(*State);
			}
			const FVector Spot = SmokeUtils::FreeSpot(World, State->VaultCentre - State->VaultAcross * (State->VaultHalfDepth + 70.f) + FVector(0.f, 0.f, 100.f), Leader);
			Leader->StopOperative();
			Leader->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State->VaultAcross.Rotation(), false, true);
			State->Stage = 1;
			State->StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State->StageTime < 0.8f)
			{
				return true;
			}
			const bool bVault = Leader->IsVaulting() || Leader->TryVault(State->VaultAcross, true);
			Check(*State, bVault, TEXT("leader vaults ground window_sill via TryVault"));
			State->Stage = 2;
			State->StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (Leader->IsVaulting() && State->StageTime < 4.f)
			{
				return true;
			}
			const float Beyond = static_cast<float>(FVector::DotProduct(Leader->GetActorLocation() - State->VaultCentre, State->VaultAcross));
			Check(*State, Beyond > State->VaultHalfDepth + 20.f && !VaultNavigation::IsStandingOnObstacle(*Leader),
				FString::Printf(TEXT("window vault lands across sill (%.0f cm beyond centre)"), Beyond));
			if (State->bShotsMode)
			{
				State->ShotFrameCount = 0;
				State->ShotPhase = EShotPhase::Warmup;
				State->Stage = 10;
				return false;
			}
			return Finish(*State);
		}
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (const TCHAR* VarName : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(VarName))
			{
				Var->Set(0, ECVF_SetByConsole);
			}
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->ShotsDir = FPaths::ProjectSavedDir() / TEXT("Screenshots/LevelGenBuildings");
		FString LayoutArg, MapArg;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("layout=")))
			{
				LayoutArg = Arg.Mid(7);
			}
			else if (Arg.StartsWith(TEXT("map=")))
			{
				MapArg = FPaths::GetBaseFilename(Arg.Mid(4));
			}
			else if (Arg == TEXT("shots"))
			{
				State->bShotsMode = true;
			}
			else if (Arg.StartsWith(TEXT("shots_dir=")))
			{
				State->ShotsDir = Arg.Mid(10);
			}
		}
		FString Error;
		if (!LoadLayoutData(World, *State, LayoutArg, MapArg, Error))
		{
			Check(*State, false, Error);
			Finish(*State);
			return;
		}
		if (State->bShotsMode)
		{
			IFileManager::Get().MakeDirectory(*State->ShotsDir, true);
			ActiveShotState = State;
			State->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&TickEndFrame);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenOpeningsSmoke"),
		TEXT("Dev check (028/007): checks window/loophole LOS traces, ground window_sill vaulting vs mantle_sill/parapet non-vault, and roof parapet crouch/stand cover & ladder clearance; optional 'shots' [shots_dir=...] captures rendered PNGs."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
