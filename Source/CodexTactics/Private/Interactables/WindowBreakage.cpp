// UE-only (no Godot reference).
#include "Interactables/WindowBreakage.h"
#include "CodexTactics.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

bool WindowBreakage::IsOverSill(const FBox& SillBox, const FVector& Point, float MarginCm, float HeightCm)
{
	return SillBox.IsValid && Point.X >= SillBox.Min.X - MarginCm && Point.X <= SillBox.Max.X + MarginCm && Point.Y >= SillBox.Min.Y - MarginCm
		&& Point.Y <= SillBox.Max.Y + MarginCm && Point.Z >= SillBox.Min.Z - 20.f && Point.Z <= SillBox.Max.Z + HeightCm;
}

AActor* WindowBreakage::FindWindowSillAt(UWorld& World, const FHitResult& Hit)
{
	static const FName SillTag(TEXT("window_sill"));
	AActor* HitActor = Hit.GetActor();
	if (!Hit.bBlockingHit)
	{
		return nullptr;
	}
	if (HitActor && HitActor->ActorHasTag(SillTag))
	{
		return HitActor;
	}
	static TWeakObjectPtr<UWorld> CachedWorld;
	static TArray<TPair<TWeakObjectPtr<AActor>, FBox>> Sills;
	if (CachedWorld.Get() != &World || (!Sills.IsEmpty() && !Sills[0].Key.IsValid()))
	{
		CachedWorld = &World;
		Sills.Reset();
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (It->ActorHasTag(SillTag))
			{
				Sills.Emplace(*It, It->GetComponentsBoundingBox(true));
			}
		}
	}
	AActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	const bool bFace = Hit.ImpactNormal.Z <= 0.7f;
	const FVector Start = Hit.TraceStart;
	const FVector Delta = Hit.ImpactPoint - Start;
	for (const TPair<TWeakObjectPtr<AActor>, FBox>& Entry : Sills)
	{
		AActor* Sill = Entry.Key.Get();
		if (!Sill)
		{
			continue;
		}
		// The window face itself (glass, frame, the wall round it; a floor hit beside the window is a plain move), or a ray that went THROUGH
		// the window opening (the cursor on the window, landing on the floor behind it: the opening has no glass collision).
		const FBox& S = Entry.Value;
		const FBox Opening(FVector(S.Min.X - 10.f, S.Min.Y - 10.f, S.Max.Z + 5.f), FVector(S.Max.X + 10.f, S.Max.Y + 10.f, S.Max.Z + 140.f));
		const bool bThrough = !Delta.IsNearlyZero() && !Start.IsNearlyZero() && FMath::LineBoxIntersection(Opening, Start, Hit.ImpactPoint, Delta);
		if ((bFace && IsOverSill(S, Hit.ImpactPoint, 30.f, 260.f)) || bThrough)
		{
			const double Distance = FVector::DistSquared2D(Entry.Value.GetCenter(), Hit.ImpactPoint);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Sill;
			}
		}
	}
	return Best;
}

int32 WindowBreakage::HideBrokenWindow(AActor& Sill)
{
	UWorld* World = Sill.GetWorld();
	const FBox SillBox = Sill.GetComponentsBoundingBox(true);
	if (!World || !SillBox.IsValid)
	{
		return 0;
	}
	static const FName GlassTag(TEXT("window_glass"));
	static const FName KitWindowTag(TEXT("kit_part=window"));
	int32 Hidden = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == &Sill)
		{
			continue;
		}
		// The grey builder's pane over this opening.
		if (Actor->ActorHasTag(GlassTag) && !Actor->IsHidden() && IsOverSill(SillBox, Actor->GetComponentsBoundingBox(true).GetCenter()))
		{
			Actor->SetActorHiddenInGame(true);
			++Hidden;
			continue;
		}
		// The kit's window instances over it (kit_dress: one ISM per mesh per storey, tagged kit_part=window).
		TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Actor);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			if (!Component->ComponentTags.Contains(KitWindowTag))
			{
				continue;
			}
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Instance;
				if (!Component->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true) || Instance.GetScale3D().GetMin() < 0.01)
				{
					continue;
				}
				const FBox Bounds = Component->GetStaticMesh() ? Component->GetStaticMesh()->GetBounds().GetBox().TransformBy(Instance) : FBox(Instance.GetLocation(), Instance.GetLocation());
				if (IsOverSill(SillBox, Bounds.GetCenter()))
				{
					Instance.SetScale3D(FVector(0.001));
					Component->UpdateInstanceTransform(Index, Instance, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
					++Hidden;
				}
			}
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Window] %s broken: %d glass piece(s) hidden"), *Sill.GetName(), Hidden);
	return Hidden;
}
