#include "LevelGen/PropsValidator.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"

namespace PropsValidatorLocal
{
	bool IsLockedOrHand(const FLevelPropsResolvedObject& ResolvedObj)
	{
		const FLevelPropsObject& Obj = ResolvedObj.Object;
		return Obj.bLocked || ResolvedObj.bHand || ResolvedObj.bPinned
			|| Obj.Origin.Equals(TEXT("hand"), ESearchCase::IgnoreCase);
	}

	void LogIssue(const FPropsValidationIssue& Issue)
	{
		if (Issue.bLocked || Issue.bHand)
		{
			UE_LOG(LogCodexTactics, Warning,
				TEXT("Props validator: LOCKED/HAND INVALID object '%s' on layer '%s': %s (dropped)"),
				*Issue.ObjectId, *Issue.LayerId, *Issue.Reason);
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning,
				TEXT("Props validator: dropped invalid object '%s' on layer '%s': %s"),
				*Issue.ObjectId, *Issue.LayerId, *Issue.Reason);
		}
	}
}

bool FPropsValidator::Validate(
	const FPropsLayoutView& LayoutView,
	const FLevelPropsFile& File,
	FPropsValidationResult& OutResult,
	const FJsonObject* Balance)
{
	TArray<FString> Warnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File, &Warnings);
	return ValidateResolved(LayoutView, Resolved, OutResult, Balance);
}

bool FPropsValidator::ValidateResolved(
	const FPropsLayoutView& LayoutView,
	const FLevelPropsResolved& Resolved,
	FPropsValidationResult& OutResult,
	const FJsonObject* Balance)
{
	return ValidateObjects(LayoutView, Resolved.Objects, Resolved.KeepOuts, OutResult, Balance);
}

bool FPropsValidator::ValidateObjects(
	const FPropsLayoutView& LayoutView,
	const TArray<FLevelPropsResolvedObject>& Objects,
	const TArray<FLevelPropsKeepOut>& KeepOuts,
	FPropsValidationResult& OutResult,
	const FJsonObject* Balance)
{
	using namespace PropsValidatorLocal;

	OutResult = FPropsValidationResult();
	OutResult.TotalObjects = Objects.Num();

	TArray<FLevelPropsResolvedObject> ProvisionallyValid;
	TSet<FString> ProvisionallyValidIds;

	// Pass 1: Spatial & Geometry Validation
	for (const FLevelPropsResolvedObject& ResolvedObj : Objects)
	{
		const FLevelPropsObject& Obj = ResolvedObj.Object;
		const bool bLockedOrHand = IsLockedOrHand(ResolvedObj);
		const int32 HalfSize = GetVariantHalfSize(Obj.Variant, Balance);

		FString FailReason;

		// 1. Walk layer exists
		const FPropsWalkLayer* WalkLayer = LayoutView.FindLayer(Obj.WalkLayer);
		if (!WalkLayer)
		{
			FailReason = FString::Printf(TEXT("unknown walk layer '%s'"), *Obj.WalkLayer);
		}
		// 2. Position is walkable
		else if (!LayoutView.IsWalkable(Obj.Pos, Obj.WalkLayer))
		{
			FailReason = FString::Printf(TEXT("not on its walk layer (unwalkable at [%d, %d])"), Obj.Pos.X, Obj.Pos.Y);
		}
		// 3. Blocked by layout geometry (non-attached props)
		else if (Obj.Attach.IsEmpty() && LayoutView.IsBlocked(Obj.Pos, Obj.WalkLayer, HalfSize))
		{
			FailReason = FString::Printf(TEXT("blocked by layout geometry at [%d, %d]"), Obj.Pos.X, Obj.Pos.Y);
		}
		// 4. Doorway clear corridor (90 cm wide, 150 cm deep on both sides)
		else
		{
			FString BlockingDoorId;
			if (IsInDoorwayCorridor(LayoutView, Obj, HalfSize, &BlockingDoorId))
			{
				FailReason = FString::Printf(TEXT("inside 90 cm clear corridor of doorway '%s'"), *BlockingDoorId);
			}
			// 5. Ladder / Stair clear disks (100 cm disk)
			else
			{
				FString BlockingLadderId;
				if (IsInLadderOrStairDisk(LayoutView, Obj, HalfSize, &BlockingLadderId))
				{
					FailReason = FString::Printf(TEXT("inside 100 cm keep-out disk of ladder/stair '%s'"), *BlockingLadderId);
				}
				// 6. Keep-outs from deleted objects
				else
				{
					for (const FLevelPropsKeepOut& KO : KeepOuts)
					{
						if (KO.LayerId.IsEmpty() || KO.LayerId == ResolvedObj.LayerId)
						{
							const int32 Radius = KO.RadiusCm > 0 ? KO.RadiusCm : 120;
							const int64 DX = static_cast<int64>(Obj.Pos.X) - KO.Pos.X;
							const int64 DY = static_cast<int64>(Obj.Pos.Y) - KO.Pos.Y;
							if (DX * DX + DY * DY < static_cast<int64>(Radius) * Radius)
							{
								FailReason = FString::Printf(TEXT("inside keep-out of deleted object '%s'"), *KO.Id);
								break;
							}
						}
					}
				}
			}
		}

		if (FailReason.IsEmpty())
		{
			ProvisionallyValid.Add(ResolvedObj);
			ProvisionallyValidIds.Add(Obj.Id);
		}
		else
		{
			FPropsValidationIssue Issue;
			Issue.ObjectId = Obj.Id;
			Issue.LayerId = ResolvedObj.LayerId;
			Issue.bLocked = Obj.bLocked;
			Issue.bHand = bLockedOrHand;
			Issue.Reason = FailReason;
			Issue.bDropped = true;

			OutResult.Issues.Add(Issue);
			OutResult.DroppedObjects.Add(ResolvedObj);
			LogIssue(Issue);
		}
	}

	// Pass 2: Attach Target Dependency Validation (e.g. crate mines, loot crates)
	for (const FLevelPropsResolvedObject& ResolvedObj : ProvisionallyValid)
	{
		const FLevelPropsObject& Obj = ResolvedObj.Object;
		const bool bLockedOrHand = IsLockedOrHand(ResolvedObj);

		if (!Obj.Attach.IsEmpty() && !ProvisionallyValidIds.Contains(Obj.Attach))
		{
			FPropsValidationIssue Issue;
			Issue.ObjectId = Obj.Id;
			Issue.LayerId = ResolvedObj.LayerId;
			Issue.bLocked = Obj.bLocked;
			Issue.bHand = bLockedOrHand;
			Issue.Reason = FString::Printf(TEXT("attach target '%s' not found or dropped"), *Obj.Attach);
			Issue.bDropped = true;

			OutResult.Issues.Add(Issue);
			OutResult.DroppedObjects.Add(ResolvedObj);
			LogIssue(Issue);
		}
		else
		{
			OutResult.ValidObjects.Add(ResolvedObj);
		}
	}

	return OutResult.DroppedObjects.Num() == 0;
}

bool FPropsValidator::IsInDoorwayCorridor(
	const FPropsLayoutView& LayoutView,
	const FLevelPropsObject& Object,
	int32 HalfSize,
	FString* OutBlockingDoorwayId)
{
	const TArray<FPropsDoorway>& Doorways = LayoutView.GetDoorways();
	for (const FPropsDoorway& Door : Doorways)
	{
		if (Door.WalkLayer != Object.WalkLayer)
		{
			continue;
		}

		// Exception: the doorway's own mine is allowed
		if (!Object.Door.IsEmpty() && Object.Door.Equals(Door.Id, ESearchCase::CaseSensitive))
		{
			continue;
		}

		// Doorway corridor: Along is unit axis along wall; Normal is perpendicular across opening
		const FIntPoint Delta = Object.Pos - Door.Pos;
		const FIntPoint Normal(-Door.Along.Y, Door.Along.X);

		const int64 LateralDist = FMath::Abs(static_cast<int64>(Delta.X) * Door.Along.X + static_cast<int64>(Delta.Y) * Door.Along.Y);
		const int64 NormalDist = FMath::Abs(static_cast<int64>(Delta.X) * Normal.X + static_cast<int64>(Delta.Y) * Normal.Y);

		const int64 LateralLimit = 45 + FMath::Max(0, HalfSize);
		const int64 NormalLimit = 150 + FMath::Max(0, HalfSize);

		if (LateralDist <= LateralLimit && NormalDist <= NormalLimit)
		{
			if (OutBlockingDoorwayId)
			{
				*OutBlockingDoorwayId = Door.Id;
			}
			return true;
		}
	}
	return false;
}

bool FPropsValidator::IsInLadderOrStairDisk(
	const FPropsLayoutView& LayoutView,
	const FLevelPropsObject& Object,
	int32 HalfSize,
	FString* OutBlockingId)
{
	FPropsExclusionParams Params;
	Params.LadderCm = 100 + FMath::Max(0, HalfSize);
	Params.StairCm = 100 + FMath::Max(0, HalfSize);
	const TArray<FPropsExclusion> Exclusions = LayoutView.BuildExclusions(Params);

	for (const FPropsExclusion& Exc : Exclusions)
	{
		if (Exc.Kind == EPropsExclusionKind::LadderFoot ||
			Exc.Kind == EPropsExclusionKind::LadderTop ||
			Exc.Kind == EPropsExclusionKind::Stair)
		{
			if (Exc.Contains(Object.Pos, Object.WalkLayer))
			{
				if (OutBlockingId)
				{
					*OutBlockingId = Exc.SourceId;
				}
				return true;
			}
		}
	}
	return false;
}

int32 FPropsValidator::GetVariantHalfSize(const FString& Variant, const FJsonObject* Balance)
{
	if (Balance)
	{
		const TSharedPtr<FJsonValue> KindsVal = Balance->TryGetField(TEXT("kinds"));
		if (KindsVal.IsValid() && KindsVal->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Kinds = KindsVal->AsObject();
			const TSharedPtr<FJsonValue> VarVal = Kinds->TryGetField(Variant);
			if (VarVal.IsValid() && VarVal->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> K = VarVal->AsObject();
				const TSharedPtr<FJsonValue> RadiusVal = K->TryGetField(TEXT("radius_cm"));
				if (RadiusVal.IsValid() && RadiusVal->Type == EJson::Number)
				{
					return FMath::Max(0, FMath::RoundToInt(RadiusVal->AsNumber()));
				}
				const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
				if (K->TryGetArrayField(TEXT("size_cm"), SizeArray) && SizeArray && SizeArray->Num() >= 2)
				{
					const int32 X = FMath::RoundToInt((*SizeArray)[0]->AsNumber());
					const int32 Y = FMath::RoundToInt((*SizeArray)[1]->AsNumber());
					return (FMath::Max3(X, Y, 0) + 1) / 2;
				}
			}
		}
	}

	if (Variant.StartsWith(TEXT("barrel"), ESearchCase::IgnoreCase))
	{
		return 30;
	}
	if (Variant.Equals(TEXT("crate_l"), ESearchCase::IgnoreCase))
	{
		return 80;
	}
	if (Variant.StartsWith(TEXT("crate"), ESearchCase::IgnoreCase))
	{
		return 60;
	}
	if (Variant.StartsWith(TEXT("turret"), ESearchCase::IgnoreCase))
	{
		return 50;
	}
	if (Variant.StartsWith(TEXT("loot_pile"), ESearchCase::IgnoreCase))
	{
		return 30;
	}
	return 0;
}
