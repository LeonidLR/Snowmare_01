#include "Core/SquadSpawnRules.h"

SquadSpawnRules::FMemberClassChoice SquadSpawnRules::ChooseMemberClass(bool bMemberBlueprintSet, UClass* MemberClass, UClass* DefaultClass)
{
	FMemberClassChoice Choice;
	if (bMemberBlueprintSet && MemberClass)
	{
		Choice.Class = MemberClass;
		Choice.bApplyBodyMesh = false; // the member Blueprint owns its mesh, materials and attachments
		return Choice;
	}
	Choice.Class = DefaultClass;
	Choice.bApplyBodyMesh = true;
	Choice.bMemberBlueprintMissing = bMemberBlueprintSet;
	return Choice;
}
