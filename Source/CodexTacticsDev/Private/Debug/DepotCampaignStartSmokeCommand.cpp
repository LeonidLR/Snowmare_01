// Dev-only check of the start of campaign level 0.1 "0.1 Depot" (game designer 2026-11-02, reconfirmed 2026-11-08: the generated Depot
// /Game/Maps/Generated/L_Gen_Depot_1 replaces Frozen Factory as the campaign's second level - campaign = 0.0 Frozen Dam -> 0.1 Depot):
//   Scripts/smoke_parallel.ps1 -Commands DepotCampaignStartSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, Scripts/test_map.json)
//  0. a fresh Depot start (no carry): the squad of three on the navmesh; each operative is given a distinct "lake" state that differs from
//     both the roster's defaults and the Depot's squad_loadout (level / EXP / unspent points / accuracy / luck, a raised maximum health
//     while wounded, medkits / grenades / heat packs / M16 reserve, chilled) and captured the way ULevelExitSubsystem does it on the
//     lake's exit (UCampaignSubsystem::CaptureSquad, FromMap = the lake); then the Depot is opened again as the campaign arrival;
//  1. the arrival: the Depot's own level JSON (the run's -LevelJson is ignored on an arrival map), the start behind a black screen with the
//     title card "0.1 Depot" that fades in over the arrival default (the Depot sets no "start_fade_seconds"), no intro briefing; the
//     three operatives at the Depot's PlayerStart (its spawn of the generated layout, on the navmesh, the leader facing its way) with the
//     carried level / EXP / points / stats / kit - NOT the Depot's squad_loadout - at full health of the carried maximum, warm; the Depot
//     is the campaign's last level;
//  2. after the camera settled: the leader in the start camera's frustum, the kit still the carried one (no stage loadout on a carried
//     squad, LoadoutRules::ShouldApplyStageLoadout);
//  3. the fade-in clears by hold + fade and never darkens on the way.
// Godot: none (UE-only campaign level). The lake -> Depot transition itself: CodexTactics.GateExitLakeSmoke.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Data/WaveConfigTypes.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFlow/CampaignRules.h"
#include "GameFlow/CampaignSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace DepotCampaignStartSmoke
{
	constexpr float StepSeconds = 0.1f;
	const TCHAR* DepotMap = TEXT("/Game/Maps/Generated/L_Gen_Depot_1");
	const TCHAR* DepotShort = TEXT("L_Gen_Depot_1");
	const TCHAR* LakeShort = TEXT("L_Demo01_A_FrozenLake");

	enum class EStage : uint8
	{
		Setup,
		Travel,
		Arrival,
		Settled,
		Fade
	};

	/** What an operative brought along (checked on the arrival). */
	struct FExpect
	{
		EOperativeRole Role = EOperativeRole::Commander;
		FString Name;
		int32 Level = 1;
		int32 Exp = 0;
		int32 Points = 0;
		float Accuracy = 0.f;
		float Luck = 0.f;
		float MaxHealth = 0.f;
		int32 Medkits = 0;
		int32 Grenades = 0;
		int32 HeatPacks = 0;
		int32 RifleReserve = 0;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		TArray<FExpect> Expect;
		TWeakObjectPtr<UWorld> FirstWorld;
		TWeakObjectPtr<UWorld> ArrivalWorld;
		TWeakObjectPtr<UGameInstance> GameInstance;
		float FirstAlpha = -1.f;
		float FirstGameTime = -1.f;
		float LastAlpha = 2.f;
		bool bBrightened = false;
		float ClearAt = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DepotCampaignStartSmoke"));
		return false;
	}

	void Next(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	/** Point inside the camera's view frustum (16:9, the camera's horizontal FOV). */
	bool InFrustum(const FVector& Eye, const FRotator& View, float HorizontalFovDeg, const FVector& Point)
	{
		const FRotationMatrix Axes(View);
		const FVector Delta = Point - Eye;
		const float Forward = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::X));
		if (Forward <= 10.f)
		{
			return false;
		}
		const float HalfH = FMath::DegreesToRadians(HorizontalFovDeg * 0.5f);
		const float HalfV = FMath::Atan(FMath::Tan(HalfH) * 9.f / 16.f);
		const float Right = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::Y));
		const float Up = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::Z));
		return FMath::Abs(FMath::Atan2(Right, Forward)) <= HalfH && FMath::Abs(FMath::Atan2(Up, Forward)) <= HalfV;
	}

	/** The squad's three on the navmesh. */
	bool SquadReady(UWorld* World, const USquadSubsystem* Squad)
	{
		const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Probe;
		return Squad && Squad->GetMembers().Num() >= 3 && Leader && Nav
			&& Nav->ProjectPointToNavigation(Leader->GetActorLocation(), Probe, FVector(60.f, 60.f, 200.f));
	}

	/**
	 * A distinct "lake" state per operative: values that differ from the roster's defaults AND from the Depot's squad_loadout
	 * (medkits 2, grenades 2, heat packs 1, M16 reserve 120), so a level loadout that overwrote the carry would show.
	 */
	void StageLakeState(USquadSubsystem& Squad)
	{
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			Member->Level = 2 + Index;
			Member->CurrentExp = 140 + 75 * Index;
			Member->UnspentStatPoints = (Index + 1) % 3; // 1, 2, 0
			Member->Accuracy += 3.f + Index;
			Member->Luck += 2.f;
			if (UHealthComponent* Health = Member->HealthComponent)
			{
				const float RaisedMax = Health->GetMaxHealth() + 10.f + 5.f * Index;
				Health->RestoreHealth(RaisedMax * 0.4f, RaisedMax); // wounded on the way out, the maximum raised by spent points
			}
			Member->MedkitsCount = 1 + 2 * (Index % 2); // 1, 3, 1
			Member->GrenadesCount = Index % 2; // 0, 1, 0
			Member->HeatPacksCount = Index == 1 ? 0 : 3;
			if (FWeaponAmmoState* Rifle = Member->AmmoInventory.Find(TEXT("m16")))
			{
				Rifle->Reserve = 37 + 11 * Index;
			}
			Member->ColdLevel = 55.f; // chilled on the lake: cold is not carried
			++Index;
		}
	}

	void RecordExpect(FState& State, const USquadSubsystem& Squad)
	{
		State.Expect.Reset();
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			FExpect& Expect = State.Expect.AddDefaulted_GetRef();
			Expect.Role = Member->SquadRole;
			Expect.Name = Member->DisplayName.ToString();
			Expect.Level = Member->Level;
			Expect.Exp = Member->CurrentExp;
			Expect.Points = Member->UnspentStatPoints;
			Expect.Accuracy = Member->Accuracy;
			Expect.Luck = Member->Luck;
			Expect.MaxHealth = Member->GetStatValue(EProgressStat::Health);
			Expect.Medkits = Member->MedkitsCount;
			Expect.Grenades = Member->GrenadesCount;
			Expect.HeatPacks = Member->HeatPacksCount;
			Expect.RifleReserve = Member->GetAmmoState(TEXT("m16")).Reserve;
		}
	}

	/** The carried kit on the squad (the arrival and again once the level runs). */
	void CheckKit(FState& State, const USquadSubsystem& Squad, const TCHAR* When)
	{
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const FExpect* Expect = State.Expect.FindByPredicate([Member](const FExpect& E) { return E.Role == Member->SquadRole; });
			Check(State, Expect && Member->MedkitsCount == Expect->Medkits && Member->GrenadesCount == Expect->Grenades && Member->HeatPacksCount == Expect->HeatPacks
				&& Member->GetAmmoState(TEXT("m16")).Reserve == Expect->RifleReserve,
				FString::Printf(TEXT("%s %s: the carried kit, not the Depot's squad_loadout - %d medkits (want %d), %d grenades (%d), %d heat packs (%d), M16 reserve %d (%d)"),
					*Member->DisplayName.ToString(), When, Member->MedkitsCount, Expect ? Expect->Medkits : -1, Member->GrenadesCount, Expect ? Expect->Grenades : -1,
					Member->HeatPacksCount, Expect ? Expect->HeatPacks : -1, Member->GetAmmoState(TEXT("m16")).Reserve, Expect ? Expect->RifleReserve : -1));
		}
	}

	bool StepSetup(FState& State)
	{
		UWorld* World = State.FirstWorld.Get();
		if (!World)
		{
			Check(State, false, TEXT("the first Depot world went away"));
			return Finish(State, false);
		}
		if (!World->GetMapName().Contains(DepotShort))
		{
			Check(State, false, FString::Printf(TEXT("run on %s (this is %s)"), DepotShort, *World->GetMapName()));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World);
		if (!Campaign || !SquadReady(World, Squad))
		{
			if (State.StageTime > 90.f)
			{
				Check(State, false, TEXT("three operatives on the navmesh, the campaign subsystem"));
				return Finish(State, false);
			}
			return true;
		}
		Check(State, Campaign->GetCarryFor(World) == nullptr, TEXT("a fresh Depot start: no campaign carry yet"));
		Check(State, CampaignRules::FindLevelIndex(Campaign->GetConfig(), DepotShort) == 1 && Campaign->GetFirstLevelMap() == TEXT("/Game/Maps/L_Demo01_A_FrozenLake"),
			TEXT("campaign.json: 0.0 the lake -> 0.1 the Depot"));
		StageLakeState(*Squad);
		RecordExpect(State, *Squad);
		// The lake's exit (ULevelExitSubsystem): CaptureSquad for the next map, then OpenLevel. Captured here on the Depot, so the carry's
		// FromMap is set to the lake (the log line / "fell on" names read it).
		Campaign->CaptureSquad(World, DepotMap);
		FCampaignSquadCarry Carry = Campaign->GetCarry();
		Carry.FromMap = LakeShort;
		Campaign->SetCarry(Carry);
		Check(State, Carry.ToMap == DepotShort && Carry.Members.Num() == 3 && !Carry.Members.ContainsByPredicate([](const FCarriedOperative& M) { return M.bKilledInAction; }),
			FString::Printf(TEXT("the squad handed over: %s -> %s, %d operatives"), *Carry.FromMap, *Carry.ToMap, Carry.Members.Num()));
		UE_LOG(LogCodexTactics, Display, TEXT("[DepotStart] opening %s as the campaign arrival"), DepotMap);
		UGameplayStatics::OpenLevel(World, FName(DepotMap));
		Next(State, EStage::Travel);
		return true;
	}

	bool StepArrival(FState& State)
	{
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		const bool bArrived = World && World != State.FirstWorld.Get() && World->GetMapName().Contains(DepotShort);
		USquadSubsystem* Squad = bArrived ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		ULevelExitSubsystem* Exit = bArrived ? World->GetSubsystem<ULevelExitSubsystem>() : nullptr;
		if (State.Stage == EStage::Travel)
		{
			if (State.StageTime > 150.f)
			{
				Check(State, false, TEXT("the Depot re-opened as the campaign arrival"));
				return Finish(State, false);
			}
			if (!Squad || !Exit || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			State.ArrivalWorld = World;
		}
		if (!Squad || !Exit || World != State.ArrivalWorld.Get())
		{
			Check(State, false, TEXT("the arrival world stays"));
			return Finish(State, false);
		}
		// The fade timeline, sampled from the first step it is resolved on.
		if (Exit->IsStartFadeResolved())
		{
			const float Alpha = Exit->GetStartFadeAlpha();
			if (State.FirstAlpha < 0.f)
			{
				State.FirstAlpha = Alpha;
				State.FirstGameTime = World->GetTimeSeconds();
			}
			State.bBrightened |= Alpha > State.LastAlpha + 0.001f;
			State.LastAlpha = Alpha;
			if (Alpha <= 0.f && State.ClearAt < 0.f)
			{
				State.ClearAt = World->GetTimeSeconds();
			}
		}
		switch (State.Stage)
		{
		case EStage::Travel:
		{
			if (!Exit->IsStartFadeResolved() || !SquadReady(World, Squad))
			{
				return true;
			}
			Check(State, true, FString::Printf(TEXT("the Depot opened as the campaign arrival (%.1f s after the travel)"), State.StageTime));
			UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World);
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			Check(State, Campaign && Campaign->GetCarryFor(World) != nullptr, TEXT("the carry is for this map (a restart brings the squad back as it arrived)"));
			Check(State, Level && Level->Config.LevelId == DepotShort && Level->Config.LevelName.ToString() == TEXT("0.1 Depot") && !Level->Config.bCheatImmortal,
				FString::Printf(TEXT("the Depot's own level JSON: %s \"%s\", mortal"), Level ? *Level->Config.LevelId : TEXT("none"),
					Level ? *Level->Config.LevelName.ToString() : TEXT("")));
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow && Flow->GetConfig().TotalWaves == 1, FString::Printf(TEXT("... its wave list (%d waves)"), Flow ? Flow->GetConfig().TotalWaves : -1));
			const float WantFade = CampaignRules::ResolveStartFadeSeconds(Level ? Level->Config.StartFadeSeconds : 0.f, true);
			Check(State, WantFade > 0.f && FMath::IsNearlyEqual(Exit->GetStartFadeSeconds(), WantFade) && Exit->GetStartTitle() == TEXT("0.1 Depot"),
				FString::Printf(TEXT("the level starts with a %.1f s fade-in (want %.1f: the arrival default) and the title card \"%s\""), Exit->GetStartFadeSeconds(),
					WantFade, *Exit->GetStartTitle()));
			Check(State, State.FirstGameTime > CampaignRules::StartHoldSeconds || State.FirstAlpha >= 0.99f,
				FString::Printf(TEXT("black at the start (alpha %.2f at %.2f s)"), State.FirstAlpha, State.FirstGameTime));
			Check(State, Campaign && Campaign->FindNextLevel(World) == nullptr, TEXT("the Depot is the campaign's last level"));
			// "intro_dialogue": false - no MovementTest briefing (neither the window nor its radio line).
			const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
			const FString IntroRadio = MissionRules::GetModeRadio(EMissionStartMode::Game).ToString();
			const bool bIntroLine = Messages && Messages->GetHistory().ContainsByPredicate([&IntroRadio](const FGameMessage& Message)
			{
				return Message.Text.ToString() == IntroRadio;
			});
			const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Check(State, !bIntroLine && !(Dialogue && Dialogue->IsDialogueOpen()), TEXT("no intro briefing (no window, no radio line)"));

			Check(State, Squad->GetMembers().Num() == 3, FString::Printf(TEXT("the carried squad spawned (%d)"), Squad->GetMembers().Num()));
			const AActor* Start = UGameplayStatics::GetActorOfClass(World, APlayerStart::StaticClass());
			Check(State, Start != nullptr, TEXT("the Depot has its PlayerStart (the generated layout's spawn = the campaign start)"));
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FExpect* Expect = State.Expect.FindByPredicate([Member](const FExpect& E) { return E.Role == Member->SquadRole; });
				Check(State, Expect && Member->Level == Expect->Level && Member->CurrentExp == Expect->Exp && Member->UnspentStatPoints == Expect->Points
					&& FMath::IsNearlyEqual(Member->Accuracy, Expect->Accuracy) && FMath::IsNearlyEqual(Member->Luck, Expect->Luck)
					&& FMath::IsNearlyEqual(Member->GetStatValue(EProgressStat::Health), Expect->MaxHealth),
					FString::Printf(TEXT("%s carried: level %d, EXP %d, %d points, accuracy %.0f, luck %.0f, max HP %.0f (want %d / %d / %d / %.0f / %.0f / %.0f)"),
						*Member->DisplayName.ToString(), Member->Level, Member->CurrentExp, Member->UnspentStatPoints, Member->Accuracy, Member->Luck,
						Member->GetStatValue(EProgressStat::Health), Expect ? Expect->Level : -1, Expect ? Expect->Exp : -1, Expect ? Expect->Points : -1,
						Expect ? Expect->Accuracy : -1.f, Expect ? Expect->Luck : -1.f, Expect ? Expect->MaxHealth : -1.f));
				const UHealthComponent* Health = Member->HealthComponent;
				Check(State, Health && FMath::IsNearlyEqual(Health->GetCurrentHealth(), Health->GetMaxHealth()) && Member->ColdLevel < 5.f,
					FString::Printf(TEXT("%s arrives at full health of the carried maximum (%.0f / %.0f), warm (cold %.2f)"), *Member->DisplayName.ToString(),
						Health ? Health->GetCurrentHealth() : -1.f, Health ? Health->GetMaxHealth() : -1.f, Member->ColdLevel));
				if (Start)
				{
					const FVector StartAt = Start->GetActorLocation();
					const float Away = FVector::Dist2D(Member->GetActorLocation(), StartAt);
					FNavLocation Probe;
					const bool bOnNav = Nav && Nav->ProjectPointToNavigation(Member->GetActorLocation(), Probe, FVector(60.f, 60.f, 200.f));
					// Formation look sectors (FORMATION_SECTORS.md, 2026-10-25): halted, each follower watches his sector centre, so only the
					// leader must face the PlayerStart's way.
					const bool bLeader = Squad->GetLeader() == Member;
					const float Facing = FVector::DotProduct(Member->GetActorForwardVector().GetSafeNormal2D(), Start->GetActorForwardVector().GetSafeNormal2D());
					const float MinFacing = bLeader ? 0.7f : -1.01f;
					Check(State, Away < 1200.f && FMath::Abs(Member->GetActorLocation().Z - StartAt.Z) < 300.f && bOnNav && Facing > MinFacing,
						FString::Printf(TEXT("%s spawned at the Depot's start: %.0f cm from the PlayerStart, on the navmesh %d, facing its way (%.2f, min %.1f)"),
							*Member->DisplayName.ToString(), Away, bOnNav ? 1 : 0, Facing, MinFacing));
				}
			}
			CheckKit(State, *Squad, TEXT("on arrival"));
			Next(State, EStage::Arrival);
			return true;
		}
		case EStage::Arrival:
		{
			// The camera settles on the leader (it follows from the level's start).
			if (State.StageTime < 3.f)
			{
				return true;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
			const AOperativeCharacter* Leader = Squad->GetLeader();
			Check(State, Camera && Leader && InFrustum(Camera->GetCameraLocation(), Camera->GetCameraRotation(), Camera->GetFOVAngle(), Leader->GetActorLocation()),
				FString::Printf(TEXT("the start camera frames the leader (eye %s, view %s)"), Camera ? *Camera->GetCameraLocation().ToCompactString() : TEXT("-"),
					Camera ? *Camera->GetCameraRotation().ToCompactString() : TEXT("-")));
			CheckKit(State, *Squad, TEXT("3 s in"));
			Next(State, EStage::Fade);
			return true;
		}
		case EStage::Fade:
		{
			// The fade is over by hold + fade (+ a step), and the screen never got darker again on the way. In game time (the fade advances by
			// frame deltas): wall-clock stalls under parallel load must not fail it.
			const float End = CampaignRules::StartHoldSeconds + Exit->GetStartFadeSeconds();
			if (State.ClearAt < 0.f && World->GetTimeSeconds() < End + 2.f)
			{
				return true;
			}
			Check(State, State.ClearAt >= 0.f && State.ClearAt <= End + 0.5f && !State.bBrightened,
				FString::Printf(TEXT("the fade-in clears by %.1f s (clear at %.2f s), never darkening on the way"), End, State.ClearAt));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 280.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		return State.Stage == EStage::Setup ? StepSetup(State) : StepArrival(State);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->FirstWorld = World;
		State->GameInstance = World->GetGameInstance();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DepotCampaignStartSmoke"),
		TEXT("Dev check (2026-11-08): campaign level 0.1 L_Gen_Depot_1 (it replaced Frozen Factory) as the arrival from the lake - a staged carry, then the "
			"Depot re-opened: its own level JSON, the fade-in from black with the title card \"0.1 Depot\", the carried squad at its PlayerStart with the "
			"carried level / EXP / points / stats / kit (not the Depot's squad_loadout), full health, warm; the last campaign level."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
