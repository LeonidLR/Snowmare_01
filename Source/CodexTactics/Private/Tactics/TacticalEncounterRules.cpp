#include "Tactics/TacticalEncounterRules.h"

TArray<int32> TacticalEncounterRules::SelectEnemies(const TArray<FCandidate>& Candidates, float Radius, int32 MaxEnemies)
{
	TArray<int32> InRadius;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Distance <= Radius)
		{
			InRadius.Add(Index);
		}
	}
	if (InRadius.Num() <= MaxEnemies)
	{
		return InRadius;
	}
	auto Closer = [&Candidates](int32 A, int32 B) { return Candidates[A].Distance < Candidates[B].Distance; };

	// A: the nearest representative of every species (species in order of first appearance, like Godot's dictionary).
	TArray<FName> Species;
	TMap<FName, int32> Nearest;
	for (const int32 Index : InRadius)
	{
		const FName Key = Candidates[Index].Species;
		int32* Best = Nearest.Find(Key);
		if (!Best)
		{
			Species.Add(Key);
			Nearest.Add(Key, Index);
		}
		else if (Closer(Index, *Best))
		{
			*Best = Index;
		}
	}
	TArray<int32> Selected;
	for (const FName& Key : Species)
	{
		Selected.Add(Nearest[Key]);
		if (Selected.Num() >= MaxEnemies)
		{
			return Selected;
		}
	}
	// B: the nearest of the others fill the free slots.
	TArray<int32> Rest = InRadius.FilterByPredicate([&Selected](int32 Index) { return !Selected.Contains(Index); });
	Rest.StableSort(Closer);
	for (const int32 Index : Rest)
	{
		if (Selected.Num() >= MaxEnemies)
		{
			break;
		}
		Selected.Add(Index);
	}
	return Selected;
}
