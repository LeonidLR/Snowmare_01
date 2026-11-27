// Read-only X6 access audit. No world geometry or designer data is changed.
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Ladders/LadderActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavLinkCustomComponent.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Interactables/WindowBreakage.h"

namespace RoofAccessAudit155
{
struct FBuilding
{
	FString Id, Number;
	FBox2D Box;
	float RoofZ = 0.f;
};
TSharedPtr<FJsonObject> Read(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Result;
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
	}
	return Result;
}
FString Tag(const AActor& Actor, const TCHAR* Prefix)
{
	for (const FName& T : Actor.Tags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(Prefix))
		{
			return S.Mid(FCString::Strlen(Prefix));
		}
	}
	return FString();
}
void Audit(UWorld* World)
{
	const auto Layout = Read(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json"));
	const auto Index = Read(FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Gen_Depot_1_X6.buildings_index.json"));
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
	const ANavigationData* NavData =
		Nav && Op ? Nav->GetNavDataForProps(Op->GetNavAgentPropertiesRef(), Op->GetNavAgentLocation()) : nullptr;
	if (!Layout || !Index || !NavData)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL (audit inputs / infantry nav missing)"));
		return;
	}
	TArray<FBuilding> Buildings;
	for (const auto& V : Layout->GetArrayField(TEXT("buildings")))
	{
		const auto B = V->AsObject();
		FBuilding Entry;
		Entry.Id = B->GetStringField(TEXT("id"));
		for (const auto& Pair : Index->Values)
		{
			if (Pair.Value->AsString() == Entry.Id)
			{
				Entry.Number = Pair.Key;
			}
		}
		const auto& P = B->GetArrayField(TEXT("footprint"));
		Entry.Box = FBox2D(FVector2D(P[0]->AsArray()[0]->AsNumber(), P[0]->AsArray()[1]->AsNumber()),
						   FVector2D(P[1]->AsArray()[0]->AsNumber(), P[1]->AsArray()[1]->AsNumber()));
		Entry.RoofZ = Layout->GetNumberField(TEXT("ground_z")) + B->GetNumberField(TEXT("height"));
		Buildings.Add(Entry);
	}
	auto BuildingAt = [&Buildings](const AActor& Actor, const FVector& Top) -> const FBuilding*
	{
		FString Id = Tag(Actor, TEXT("building="));
		if (Id.IsEmpty())
		{
			const FString To = Tag(Actor, TEXT("to="));
			for (const FBuilding& B : Buildings)
			{
				if (To.Contains(B.Id))
				{
					Id = B.Id;
					break;
				}
			}
		}
		if (!Id.IsEmpty())
		{
			return Buildings.FindByPredicate([&](const FBuilding& B) { return B.Id == Id; });
		}
		const FBuilding* Best = nullptr;
		double Distance = 500.0 * 500.0;
		for (const FBuilding& B : Buildings)
		{
			const double D = B.Box.ComputeSquaredDistanceToPoint(FVector2D(Top));
			if (D < Distance)
			{
				Best = &B;
				Distance = D;
			}
		}
		return Best;
	};
	auto OnNav = [&](const FVector& P, FNavLocation& Out)
	{ return Nav->ProjectPointToNavigation(P, Out, FVector(60.f, 60.f, 60.f), NavData) && FMath::Abs(Out.Location.Z - P.Z) <= 60.f; };
	auto Path = [&](const FNavLocation& A, const FNavLocation& B)
	{
		FPathFindingQuery Q(nullptr, *NavData, A.Location, B.Location);
		Q.SetAllowPartialPaths(false);
		return Nav->FindPathSync(Q);
	};
	int32 Count = 0, Broken = 0, LadderCount = 0, RampCount = 0;
	FString Table = TEXT("# X6 placed access audit (155)\n\nRead-only runtime geometry; Default infantry navmesh, +/-60 cm endpoint snap. "
						 "Heights cm. A complete alternate path alone does not prove the audited ladder link.\n\n| Building | Actor / "
						 "component | Kind | Bottom Z | Top Z | Claimed landing Z | Bottom nav | Top nav | Connected | Findings / proposed "
						 "data fix |\n|---|---|---|---:|---:|---:|---|---|---|---|\n");
	auto Row = [&](const AActor& Actor, const FString& Name, const TCHAR* Kind, const FVector& Bottom, const FVector& Top,
				   const FVector& Landing, float Expected, const UNavLinkCustomComponent* Link, FString Issue)
	{
		++Count;
		FNavLocation A, B;
		const bool Low = OnNav(Bottom, A), High = OnNav(Landing, B);
		bool Connected = false;
		if (Link && !Link->IsEnabled())
		{
			Issue += TEXT("nav link disabled; ");
		}
		if (Link)
		{
			FNavLocation LinkTop;
			if (!OnNav(Top, LinkTop))
			{
				Issue += TEXT("top link endpoint off nav; ");
			}
		}
		if (Low && High)
		{
			const auto R = Path(A, B);
			Connected = R.IsSuccessful() && R.Path.IsValid() && !R.Path->IsPartial();
			if (Link && Connected)
			{
				const FNavMeshPath* M = R.Path->CastPath<FNavMeshPath>();
				if (!M || !M->CustomNavLinkIds.Contains(Link->GetId()))
				{
					Issue += TEXT("link not selected by endpoint path (inspect attachment); ");
				}
			}
		}
		if (FMath::Abs(Top.Z - Expected) > 25.f)
		{
			Issue += TEXT("top misses claimed floor: correct Height/rungs or ramp endpoint; ");
		}
		if (!Low)
		{
			Issue += TEXT("bottom off nav: move foot onto infantry polygon; ");
		}
		if (!High)
		{
			Issue += TEXT("top off nav: extend/align landing into floor polygon; ");
		}
		if (!Connected)
		{
			Issue += TEXT("disconnected: inspect gap, slab hole, wall and link endpoints; rebake after data fix; ");
		}
		if (!Issue.IsEmpty())
		{
			++Broken;
		}
		const FBuilding* Bld = BuildingAt(Actor, Top);
		const FString Line = FString::Printf(TEXT("| %s %s | %s | %s | %.1f | %.1f | %.1f | %d | %d | %d | %s |"),
											 Bld ? *Bld->Number : TEXT("?"), Bld ? *Bld->Id : TEXT("unindexed"), *Name, Kind, Bottom.Z,
											 Top.Z, Expected, Low, High, Connected, Issue.IsEmpty() ? TEXT("OK") : *Issue);
		Table += Line + TEXT("\n");
		UE_LOG(LogCodexTactics, Display, TEXT("[AccessAudit155] %s"), *Line);
	};
	for (TActorIterator<ALadderActor> It(World); It; ++It)
	{
		++LadderCount;
		const FBuilding* B = BuildingAt(**It, It->GetTopLinkWorld());
		float Expected = It->GetTopWorldZ();
		const FString To = Tag(**It, TEXT("to="));
		if (B && To.StartsWith(TEXT("roof:")))
		{
			Expected = B->RoofZ;
		}
		const FString Id = Tag(**It, TEXT("ladder="));
		for (const auto& L : Layout->GetArrayField(TEXT("ladders")))
		{
			const auto Data = L->AsObject();
			if (Data->GetStringField(TEXT("id")) == Id)
			{
				Expected = Layout->GetNumberField(TEXT("ground_z")) + Data->GetNumberField(TEXT("level_to")) * 300.f;
				break;
			}
		}
		FString Fix = It->IsClimbable() ? FString() : TEXT("decoration / no enabled climb; ");
		if (FMath::Abs(It->GetTopWorldZ() - Expected) > 25.f)
		{
			Fix += FString::Printf(TEXT("Height %.1f -> %.1f (%d rungs at pitch %.1f); "), It->Height,
				Expected - It->GetFloorZ(), FMath::RoundToInt((Expected - It->GetFloorZ()) / It->Pitch), It->Pitch);
		}
		Row(**It, It->GetName() + TEXT(" ") + Id, TEXT("ladder"), It->GetBottomLinkWorld(), It->GetTopLinkWorld(), It->GetTopExitWorld(),
			Expected, It->GetNavLink(), Fix);
	}
	// Enumerate the PLACED sloped slabs, including untagged legacy ramps; axis-aligned bounds would lose their top/foot heights.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<APawn>())
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Components(*It);
		for (const UStaticMeshComponent* C : Components)
		{
			if (!C->GetStaticMesh() || !C->IsCollisionEnabled())
			{
				continue;
			}
			const FTransform T = C->GetComponentTransform();
			const double Slope = FMath::Abs(T.GetUnitAxis(EAxis::X).Z);
			if (Slope < 0.09 || Slope > 0.85)
			{
				continue;
			}
			const FBox Local = C->GetStaticMesh()->GetBoundingBox();
			const FVector Size = Local.GetSize() * T.GetScale3D().GetAbs();
			if (Size.X < 150.f || Size.Y < 50.f || Size.Z > 80.f || Size.X < Size.Z * 3.f)
			{
				continue;
			}
			FVector A = T.TransformPosition(FVector(Local.Min.X, Local.GetCenter().Y, Local.Max.Z));
			FVector B = T.TransformPosition(FVector(Local.Max.X, Local.GetCenter().Y, Local.Max.Z));
			if (A.Z > B.Z)
			{
				Swap(A, B);
			}
			const FBuilding* Building = BuildingAt(**It, B);
			const float Expected = Building ? FMath::RoundToFloat(B.Z / 300.f) * 300.f : B.Z;
			const FVector Direction = (B - A).GetSafeNormal2D();
			// Sample just inside the ramp and just beyond its end: the roof has to join the sloped polygon.
			FVector Foot = A + Direction * 60.f;
			Foot.Z = A.Z + (B.Z - A.Z) * 60.f / FMath::Max(1.0, FVector::Dist2D(A, B));
			FVector Landing = B + Direction * 90.f;
			Landing.Z = Expected;
			FString RampIssue;
			const double Run = FVector::Dist2D(A, B);
			for (double Along = 60.0; Along < Run; Along += 100.0)
			{
				const FVector Sample = FMath::Lerp(A, B, Along / Run);
				FNavLocation Surface;
				if (!OnNav(Sample, Surface))
				{
					RampIssue += FString::Printf(TEXT("ramp surface off nav %.0f cm from foot; "), Along);
					break;
				}
			}
			FNavLocation Before, After;
			if (OnNav(FMath::Lerp(A, B, FMath::Max(0.0, 1.0 - 60.0 / Run)), Before) && OnNav(Landing, After))
			{
				const auto Join = Path(Before, After);
				if (!Join.IsSuccessful() || !Join.Path.IsValid() || Join.Path->IsPartial() || Join.Path->GetLength() > 350.f)
				{
					RampIssue += TEXT("top does not join adjacent floor directly: align slab edge / landing, remove seam obstruction; ");
				}
			}

			++RampCount;
			Row(**It, It->GetName() + TEXT("/") + C->GetName(), TEXT("ramp"), Foot, B, Landing, Expected, nullptr,
				RampIssue + (FMath::Abs(B.Z - Expected) > 60.f
								 ? FString::Printf(TEXT("physical ramp top %.1f misses floor %.1f; "), B.Z, Expected)
								 : FString()));
		}
	}
	Table += FString::Printf(TEXT("\n%d placed accesses inspected; %d require review/data repair.\n"), Count, Broken);
	FFileHelper::SaveStringToFile(Table, *(FPaths::ProjectSavedDir() / TEXT("Logs/155-roof-access-audit.md")));
	UE_LOG(LogCodexTactics, Display, TEXT("[AccessAudit155] COMPLETE accesses=%d review=%d"), Count, Broken);
	int32 ExpectedRamps = 0;
	for (const auto& V : Layout->GetArrayField(TEXT("buildings")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
		if (V->AsObject()->TryGetArrayField(TEXT("stairs"), Stairs)) { ExpectedRamps += Stairs->Num(); }
	}
	const bool InventoryComplete = LadderCount >= Layout->GetArrayField(TEXT("ladders")).Num() && RampCount >= ExpectedRamps;
	UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: placed inventory %d ladders / %d ramp slabs; layout claims %d ladders / %d stairs"),
		InventoryComplete ? TEXT("ok") : TEXT("FAIL"), LadderCount, RampCount, Layout->GetArrayField(TEXT("ladders")).Num(), ExpectedRamps);
	int32 CodeFailures = InventoryComplete ? 0 : 1, RoofFaces = 0;
	bool Case2Click = false;
	ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
	if (PC && Op)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("parapet")) || It->GetComponentsBoundingBox(true).Min.Z <= Op->GetFloorReferenceLocation().Z + 150.f)
			{
				continue;
			}
			++RoofFaces;
			FHitResult Raw(*It, It->FindComponentByClass<UPrimitiveComponent>(), It->GetComponentsBoundingBox(true).GetCenter(),
						   It->GetActorForwardVector());
			Raw.bBlockingHit = true;
			const FHitResult Kept = PC->LowerBuildingHit(Raw);
			CodeFailures += Kept.GetActor() != Raw.GetActor() || !Kept.ImpactPoint.Equals(Raw.ImpactPoint, 0.1) ? 1 : 0;
			CodeFailures += WindowBreakage::FindWindowSillAt(*World, Raw) != nullptr ? 1 : 0;
			if (!Case2Click && It->ActorHasTag(TEXT("building=B_x1_16_t7_block")))
			{
				// An actual placed case-2 roof edge through the full player dispatch. Historical cursor rays were not recorded.
				const FBox Box = It->GetComponentsBoundingBox(true);
				Raw.ImpactPoint = Box.GetCenter();
				const FVector2D Away = (FVector2D(Box.GetCenter()) - FVector2D(-8050.f,16418.f)).GetSafeNormal();
				Raw.ImpactNormal = Box.GetSize().X < Box.GetSize().Y ? FVector(FMath::Sign(Away.X),0,0) : FVector(0,FMath::Sign(Away.Y),0);
				PC->HandlePlayerClick(Raw);
				Case2Click = FMath::Abs(Op->GetLastMoveDestination().Z - Box.Min.Z) <= 60.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: case2 %s roof click retains requested floor %.0f (actual goal %s)"),
					Case2Click ? TEXT("ok") : TEXT("FAIL"), *It->GetName(), Box.Min.Z, *Op->GetLastMoveDestination().ToCompactString());
				CodeFailures += Case2Click ? 0 : 1;
			}
		}
		const EOperativeOrderResult Refused = Op->OrderMoveTo(Op->GetFloorReferenceLocation() + FVector(0.f, 0.f, 5000.f), false);
		const UGameMessageSubsystem* Feed = World->GetSubsystem<UGameMessageSubsystem>();
		const bool Explained =
			Feed && !Feed->GetHistory().IsEmpty() && Feed->GetHistory().Last().Text.ToString().Contains(TEXT("No way up"));
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		const bool FloatingShown = Floating && Floating->GetTexts().ContainsByPredicate([](const FCombatFloatingText& T)
		{ return T.Text.Contains(TEXT("NO WAY UP")); });
		CodeFailures += FloatingShown ? 0 : 1;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: refused roof order spawns live floating NO WAY UP text"), FloatingShown ? TEXT("ok") : TEXT("FAIL"));
		CodeFailures += Refused != EOperativeOrderResult::Unreachable || !Explained ? 1 : 0;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: elevated order refused with on-screen No way up reason"),
			   Explained && Refused == EOperativeOrderResult::Unreachable ? TEXT("ok") : TEXT("FAIL"));
	}
	CodeFailures += RoofFaces == 0 || !Case2Click ? 1 : 0;
	UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %d elevated parapet faces preserve raw hit height; code failures=%d"),
		   CodeFailures == 0 ? TEXT("ok") : TEXT("FAIL"), RoofFaces, CodeFailures);
	// This gate proves exhaustive execution, not map health; every defect remains in the table.
	UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Count > 0 && CodeFailures == 0 ? TEXT("PASS") : TEXT("FAIL"));
}
void Run(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}
	SmokeUtils::StopLevelPmc(World);
	TWeakObjectPtr<UWorld> Weak(World);
	TSharedRef<int32> Frames = MakeShared<int32>(0);
	TSharedRef<SmokeUtils::FPreloadGate> Gate = MakeShared<SmokeUtils::FPreloadGate>();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Weak, Frames, Gate](float)
		{
			if (Gate->Poll(Weak.Get()) == SmokeUtils::EPreloadGate::Waiting)
			{
				return true;
			}
			if (!Gate->FailReason.IsEmpty())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL (%s)"), *Gate->FailReason);
				FPlatformMisc::RequestExit(false, TEXT("RoofAccessAuditSmoke"));
				return false;
			}
			if (++*Frames < 300)
			{
				return true;
			}
			if (UWorld* W = Weak.Get())
			{
				Audit(W);
			}
			FPlatformMisc::RequestExit(false, TEXT("RoofAccessAuditSmoke"));
			return false;
		}));
}
static FAutoConsoleCommandWithWorldAndArgs Command(
	TEXT("CodexTactics.RoofAccessAuditSmoke"),
	TEXT("155 read-only X6 placed ramp/ladder audit; PASS means audit completed, findings remain data defects."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
} // namespace RoofAccessAudit155
#endif
