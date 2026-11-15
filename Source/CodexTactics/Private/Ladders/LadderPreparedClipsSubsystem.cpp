#include "Ladders/LadderPreparedClipsSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Ladders/LadderClimbComponent.h"

TSharedPtr<const FLadderPreparedClips> ULadderPreparedClipsSubsystem::Find(UAnimSequence* Up, UAnimSequence* Out, UAnimSequence* Into, const FQuat& Rotation)
{
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Up == Up && Entry.Out == Out && Entry.Into == Into
			&& Entry.Rotation.X == Rotation.X && Entry.Rotation.Y == Rotation.Y
			&& Entry.Rotation.Z == Rotation.Z && Entry.Rotation.W == Rotation.W)
		{
			++ReuseCount;
			return Entry.Data;
		}
	}
	return nullptr;
}

void ULadderPreparedClipsSubsystem::Store(UAnimSequence* Up, UAnimSequence* Out, UAnimSequence* Into, const FQuat& Rotation, TSharedPtr<const FLadderPreparedClips> Data)
{
	RetainedClips.AddUnique(Up);
	RetainedClips.AddUnique(Out);
	RetainedClips.AddUnique(Into);
	Entries.Add({ Up, Out, Into, Rotation, MoveTemp(Data) });
}

void ULadderPreparedClipsSubsystem::Deinitialize()
{
	Entries.Empty();
	RetainedClips.Empty();
	ReuseCount = 0;
	Super::Deinitialize();
}
