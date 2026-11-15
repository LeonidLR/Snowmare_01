#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LadderPreparedClipsSubsystem.generated.h"

class UAnimSequence;
struct FLadderPreparedClips;

/** World-lifetime cache of ladder root tracks; UE-only, no Godot equivalent. Never ticks or loads assets. */
UCLASS()
class CODEXTACTICS_API ULadderPreparedClipsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	TSharedPtr<const FLadderPreparedClips> Find(UAnimSequence* Up, UAnimSequence* Out, UAnimSequence* Into, const FQuat& Rotation);
	void Store(UAnimSequence* Up, UAnimSequence* Out, UAnimSequence* Into, const FQuat& Rotation, TSharedPtr<const FLadderPreparedClips> Data);
	int32 GetEntryCount() const { return Entries.Num(); }
	int32 GetReuseCount() const { return ReuseCount; }
	virtual void Deinitialize() override;
private:
	struct FEntry
	{
		TObjectPtr<UAnimSequence> Up;
		TObjectPtr<UAnimSequence> Out;
		TObjectPtr<UAnimSequence> Into;
		FQuat Rotation;
		TSharedPtr<const FLadderPreparedClips> Data;
	};
	// RetainedClips is the reflected GC owner for the object identities in Entries.
	TArray<FEntry> Entries;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> RetainedClips;
	int32 ReuseCount = 0;
};
