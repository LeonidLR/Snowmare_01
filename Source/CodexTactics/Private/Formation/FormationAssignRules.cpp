#include "Formation/FormationAssignRules.h"

namespace FormationAssignRules
{
	struct FFormationAssignSearch
	{
		const TArray<FVector2D>* Members = nullptr;
		const TArray<FVector2D>* Places = nullptr;
		int32 ToAssign = 0;
		TArray<bool> Used;
		TArray<int32> Current;
		TArray<int32> Best;
		double BestCost = TNumericLimits<double>::Max();
	};

	static void FormationAssignFrom(FFormationAssignSearch& Search, int32 Member, double Cost)
	{
		if (Cost >= Search.BestCost - 1e-3)
		{
			return;
		}
		if (Member == Search.ToAssign)
		{
			Search.BestCost = Cost;
			Search.Best = Search.Current;
			return;
		}
		for (int32 Place = 0; Place < Search.Places->Num(); ++Place)
		{
			if (Search.Used[Place])
			{
				continue;
			}
			Search.Used[Place] = true;
			Search.Current[Member] = Place;
			FormationAssignFrom(Search, Member + 1, Cost + FVector2D::Distance((*Search.Members)[Member], (*Search.Places)[Place]));
			Search.Used[Place] = false;
			Search.Current[Member] = INDEX_NONE;
		}
	}

	TArray<int32> AssignPlaces(const TArray<FVector2D>& Members, const TArray<FVector2D>& Places)
	{
		FFormationAssignSearch Search;
		Search.Members = &Members;
		Search.Places = &Places;
		Search.ToAssign = FMath::Min(Members.Num(), Places.Num());
		Search.Used.Init(false, Places.Num());
		Search.Current.Init(INDEX_NONE, Members.Num());
		// The identity until a full assignment is found (rules.ts: members beyond the place count get -1).
		Search.Best.SetNum(Members.Num());
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			Search.Best[Index] = Index < Search.ToAssign ? Index : INDEX_NONE;
		}
		FormationAssignFrom(Search, 0, 0.0);
		return Search.Best;
	}
}
