// PMC defender roles (docs/design/PMC_DEFENCE.md D1, G008). Pure rules, no world access.

#include "AI/PmcDefenceRules.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcDefenceRules
{
	namespace PmcDefenceLocal
	{
		float NormDegrees(float Degrees)
		{
			float Wrapped = FMath::Fmod(Degrees + 180.f, 360.f);
			if (Wrapped < 0.f)
			{
				Wrapped += 360.f;
			}
			return Wrapped - 180.f;
		}

		/** Bearing of Point from Origin, minus Start, in [0, 360). */
		float Delta360(float BearingDeg, float StartDeg)
		{
			float Delta = FMath::Fmod(BearingDeg - StartDeg, 360.f);
			if (Delta < 0.f)
			{
				Delta += 360.f;
			}
			return Delta;
		}

		bool SameLevel(float FeetZ, float LevelZ, float ToleranceCm)
		{
			return FMath::Abs(FeetZ - LevelZ) <= ToleranceCm;
		}

		bool ReadVec2(const TArray<TSharedPtr<FJsonValue>>* Values, FVector2D& Out)
		{
			if (Values == nullptr || Values->Num() < 2 || !(*Values)[0].IsValid() || !(*Values)[1].IsValid())
			{
				return false;
			}
			Out.X = static_cast<float>((*Values)[0]->AsNumber());
			Out.Y = static_cast<float>((*Values)[1]->AsNumber());
			return true;
		}

		/** Rotate Point about Pivot by Degrees (UE yaw: +X toward +Y). */
		FVector2D RotateAbout(const FVector2D& Point, const FVector2D& Pivot, float Degrees)
		{
			if (FMath::IsNearlyZero(Degrees))
			{
				return Point;
			}
			const float Radians = FMath::DegreesToRadians(Degrees);
			const float C = FMath::Cos(Radians);
			const float S = FMath::Sin(Radians);
			const float DX = Point.X - Pivot.X;
			const float DY = Point.Y - Pivot.Y;
			return FVector2D(Pivot.X + DX * C - DY * S, Pivot.Y + DX * S + DY * C);
		}

		bool InFootprint(const FPmcLayoutBuilding& Building, const FVector& Point)
		{
			const FVector2D Centre = (Building.Min + Building.Max) * 0.5f;
			const FVector2D Local = RotateAbout(FVector2D(Point.X, Point.Y), Centre, -Building.RotationDeg);
			return Local.X >= Building.Min.X && Local.X <= Building.Max.X && Local.Y >= Building.Min.Y && Local.Y <= Building.Max.Y;
		}

		float FootprintArea(const FPmcLayoutBuilding& Building)
		{
			return FMath::Max(0.f, Building.Max.X - Building.Min.X) * FMath::Max(0.f, Building.Max.Y - Building.Min.Y);
		}

		void SplitAccessToken(const FString& Token, EPmcAccessKind& OutKind, FString& OutId)
		{
			FString Left;
			FString Right;
			if (Token.Split(TEXT(":"), &Left, &Right))
			{
				OutKind = ParseAccessKind(Left);
				OutId = Right;
			}
			else
			{
				OutKind = EPmcAccessKind::None;
				OutId = Token;
			}
		}

		bool CollectIds(const FPmcRoofRef& Roof, TArray<FString>& OutIds)
		{
			for (const FString& Token : Roof.AccessIds)
			{
				EPmcAccessKind Kind = EPmcAccessKind::None;
				FString Id;
				SplitAccessToken(Token, Kind, Id);
				if (Id.IsEmpty())
				{
					continue;
				}
				if (Kind == EPmcAccessKind::None || Kind == Roof.AccessKind)
				{
					OutIds.Add(Id);
				}
			}
			return OutIds.Num() > 0;
		}

		const FPmcLayoutBuilding* FindBuilding(const FPmcLayoutAccess& Layout, const FString& Id)
		{
			for (const FPmcLayoutBuilding& Building : Layout.Buildings)
			{
				if (Building.Id == Id)
				{
					return &Building;
				}
			}
			return nullptr;
		}

		float SectorStartDeg(float ApproachYawDeg, float ArcDeg)
		{
			return ApproachYawDeg - ArcDeg * 0.5f;
		}

		/** Spots of one wedge, ordered by bearing along the arc (lowest index first on an equal bearing). */
		void SpotsInSector(TConstArrayView<FPmcDefenceSpot> Spots, const FVector& Origin, float ApproachYawDeg, int32 Sector,
			int32 SectorCount, float ArcDeg, TArray<int32>& OutOrdered)
		{
			OutOrdered.Reset();
			if (Sector < 0 || SectorCount <= 0)
			{
				return;
			}
			struct FOrdered
			{
				int32 Index = 0;
				float Bearing = 0.f;
			};
			TArray<FOrdered> Ordered;
			const float Start = SectorStartDeg(ApproachYawDeg, ArcDeg);
			for (int32 Index = 0; Index < Spots.Num(); ++Index)
			{
				if (SectorIndex(Origin, Spots[Index].Location, ApproachYawDeg, SectorCount, ArcDeg) != Sector)
				{
					continue;
				}
				Ordered.Add({ Index, Delta360(YawDegrees(Origin, Spots[Index].Location), Start) });
			}
			Ordered.StableSort([](const FOrdered& A, const FOrdered& B)
			{
				if (!FMath::IsNearlyEqual(A.Bearing, B.Bearing, 0.01f))
				{
					return A.Bearing < B.Bearing;
				}
				return A.Index < B.Index;
			});
			for (const FOrdered& Entry : Ordered)
			{
				OutOrdered.Add(Entry.Index);
			}
		}
	}

	const TCHAR* RoleName(EPmcDefenceRole Role)
	{
		switch (Role)
		{
		case EPmcDefenceRole::AccessGuard: return TEXT("access_guard");
		case EPmcDefenceRole::SectorShooter: return TEXT("sector_shooter");
		default: return TEXT("none");
		}
	}

	const TCHAR* AccessKindName(EPmcAccessKind Kind)
	{
		switch (Kind)
		{
		case EPmcAccessKind::Stairs: return TEXT("stairs");
		case EPmcAccessKind::Hatch: return TEXT("hatch");
		case EPmcAccessKind::Ladder: return TEXT("ladder");
		default: return TEXT("none");
		}
	}

	const TCHAR* AccessSourceName(EPmcAccessSource Source)
	{
		switch (Source)
		{
		case EPmcAccessSource::Tactical: return TEXT("tactical");
		case EPmcAccessSource::Layout: return TEXT("layout");
		default: return TEXT("none");
		}
	}

	EPmcAccessKind ParseAccessKind(const FString& Name)
	{
		if (Name.Equals(TEXT("stairs"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("stair"), ESearchCase::IgnoreCase))
		{
			return EPmcAccessKind::Stairs;
		}
		if (Name.Equals(TEXT("hatch"), ESearchCase::IgnoreCase))
		{
			return EPmcAccessKind::Hatch;
		}
		if (Name.Equals(TEXT("ladder"), ESearchCase::IgnoreCase))
		{
			return EPmcAccessKind::Ladder;
		}
		return EPmcAccessKind::None;
	}

	bool ParseLayout(const FString& JsonText, FPmcLayoutAccess& Out, TArray<FString>& Errors)
	{
		using namespace PmcDefenceLocal;
		Out = FPmcLayoutAccess();
		Errors.Reset();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Errors.Add(TEXT("layout JSON did not parse"));
			return false;
		}

		double GroundZ = 0.0;
		if (Root->TryGetNumberField(TEXT("ground_z"), GroundZ))
		{
			Out.GroundZ = static_cast<float>(GroundZ);
		}

		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Root->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Buildings)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					Errors.Add(TEXT("building entry is not an object"));
					continue;
				}
				FPmcLayoutBuilding Building;
				Object->TryGetStringField(TEXT("id"), Building.Id);
				const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
				bool bFoot = Object->TryGetArrayField(TEXT("footprint"), Footprint) && Footprint->Num() >= 2;
				if (bFoot)
				{
					const TArray<TSharedPtr<FJsonValue>>* Min = nullptr;
					const TArray<TSharedPtr<FJsonValue>>* Max = nullptr;
					bFoot = (*Footprint)[0].IsValid() && (*Footprint)[0]->TryGetArray(Min)
						&& (*Footprint)[1].IsValid() && (*Footprint)[1]->TryGetArray(Max)
						&& ReadVec2(Min, Building.Min) && ReadVec2(Max, Building.Max);
				}
				if (!bFoot || Building.Id.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("building %s: footprint [min, max] required"), *Building.Id));
					continue;
				}
				double Floors = 1.0;
				if (Object->TryGetNumberField(TEXT("floors"), Floors))
				{
					Building.Floors = FMath::Max(1, FMath::RoundToInt(Floors));
				}
				double Rotation = 0.0;
				if (Object->TryGetNumberField(TEXT("rotation"), Rotation))
				{
					Building.RotationDeg = static_cast<float>(Rotation);
				}
				Object->TryGetBoolField(TEXT("roof_walkable"), Building.bRoofWalkable);

				const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
				if (Object->TryGetArrayField(TEXT("stairs"), Stairs))
				{
					for (const TSharedPtr<FJsonValue>& StairValue : *Stairs)
					{
						const TSharedPtr<FJsonObject> StairObject = StairValue.IsValid() ? StairValue->AsObject() : nullptr;
						if (!StairObject.IsValid())
						{
							continue;
						}
						FPmcLayoutStair Stair;
						StairObject->TryGetStringField(TEXT("id"), Stair.Id);
						const TArray<TSharedPtr<FJsonValue>>* Top = nullptr;
						if (Stair.Id.IsEmpty() || !StairObject->TryGetArrayField(TEXT("top"), Top) || !ReadVec2(Top, Stair.Top))
						{
							Errors.Add(FString::Printf(TEXT("building %s: stair %s has no top"), *Building.Id, *Stair.Id));
							continue;
						}
						double From = 0.0;
						double To = 0.0;
						if (StairObject->TryGetNumberField(TEXT("from"), From))
						{
							Stair.From = FMath::RoundToInt(From);
						}
						if (StairObject->TryGetNumberField(TEXT("to"), To))
						{
							Stair.To = FMath::RoundToInt(To);
						}
						Building.Stairs.Add(Stair);
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Hatches = nullptr;
				if (Object->TryGetArrayField(TEXT("hatches"), Hatches))
				{
					for (const TSharedPtr<FJsonValue>& HatchValue : *Hatches)
					{
						const TSharedPtr<FJsonObject> HatchObject = HatchValue.IsValid() ? HatchValue->AsObject() : nullptr;
						if (!HatchObject.IsValid())
						{
							continue;
						}
						FPmcLayoutHatch Hatch;
						HatchObject->TryGetStringField(TEXT("id"), Hatch.Id);
						const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
						if (Hatch.Id.IsEmpty() || !HatchObject->TryGetArrayField(TEXT("pos"), Pos) || !ReadVec2(Pos, Hatch.Pos))
						{
							Errors.Add(FString::Printf(TEXT("building %s: hatch %s has no pos"), *Building.Id, *Hatch.Id));
							continue;
						}
						double Floor = 0.0;
						if (HatchObject->TryGetNumberField(TEXT("floor"), Floor))
						{
							Hatch.Floor = FMath::RoundToInt(Floor);
						}
						Building.Hatches.Add(Hatch);
					}
				}
				Out.Buildings.Add(Building);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Ladders = nullptr;
		if (Root->TryGetArrayField(TEXT("ladders"), Ladders))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Ladders)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					continue;
				}
				FPmcLayoutLadder Ladder;
				Object->TryGetStringField(TEXT("id"), Ladder.Id);
				Object->TryGetStringField(TEXT("to"), Ladder.To);
				const TArray<TSharedPtr<FJsonValue>>* Top = nullptr;
				if (Ladder.Id.IsEmpty() || !Object->TryGetArrayField(TEXT("top"), Top) || !ReadVec2(Top, Ladder.Top))
				{
					Errors.Add(FString::Printf(TEXT("ladder %s has no top"), *Ladder.Id));
					continue;
				}
				double LevelTo = 0.0;
				if (Object->TryGetNumberField(TEXT("level_to"), LevelTo))
				{
					Ladder.LevelTo = FMath::RoundToInt(LevelTo);
				}
				Out.Ladders.Add(Ladder);
			}
		}
		return true;
	}

	int32 BuildingAt(const FPmcLayoutAccess& Layout, const FVector& Point)
	{
		using namespace PmcDefenceLocal;
		int32 Best = INDEX_NONE;
		float BestArea = 0.f;
		for (int32 Index = 0; Index < Layout.Buildings.Num(); ++Index)
		{
			const FPmcLayoutBuilding& Building = Layout.Buildings[Index];
			if (!InFootprint(Building, Point))
			{
				continue;
			}
			const float Area = FootprintArea(Building);
			if (Best == INDEX_NONE || Area < BestArea)
			{
				Best = Index;
				BestArea = Area;
			}
		}
		return Best;
	}

	float RoofZ(const FPmcLayoutAccess& Layout, const FPmcLayoutBuilding& Building)
	{
		return Layout.GroundZ + static_cast<float>(FMath::Max(Building.Floors, 0)) * Layout.LevelHeightCm;
	}

	bool IsWalkableRoofSite(const FPmcLayoutAccess& Layout, int32 BuildingIndex, float SiteZ, float ToleranceCm)
	{
		if (!Layout.Buildings.IsValidIndex(BuildingIndex))
		{
			return false;
		}
		const FPmcLayoutBuilding& Building = Layout.Buildings[BuildingIndex];
		if (!Building.bRoofWalkable)
		{
			return false;
		}
		return FMath::Abs(SiteZ - RoofZ(Layout, Building)) <= ToleranceCm;
	}

	FPmcDefenceAccess ResolveLayoutAccess(const FPmcLayoutAccess& Layout, int32 BuildingIndex, const FVector& Objective)
	{
		FPmcDefenceAccess Access;
		if (!Layout.Buildings.IsValidIndex(BuildingIndex))
		{
			return Access;
		}
		const FPmcLayoutBuilding& Building = Layout.Buildings[BuildingIndex];
		const float Z = RoofZ(Layout, Building);
		const FVector2D Goal(Objective.X, Objective.Y);

		const FPmcLayoutStair* Stair = nullptr;
		float StairDistance = 0.f;
		for (const FPmcLayoutStair& Candidate : Building.Stairs)
		{
			if (Candidate.To < Building.Floors)
			{
				continue;
			}
			const float Distance = FVector2D::Distance(Candidate.Top, Goal);
			if (Stair == nullptr || Distance < StairDistance)
			{
				Stair = &Candidate;
				StairDistance = Distance;
			}
		}
		if (Stair != nullptr)
		{
			Access.Kind = EPmcAccessKind::Stairs;
			Access.Source = EPmcAccessSource::Layout;
			Access.Id = Stair->Id;
			Access.Location = FVector(Stair->Top.X, Stair->Top.Y, Z);
			return Access;
		}

		const FPmcLayoutHatch* Hatch = nullptr;
		float HatchDistance = 0.f;
		for (const FPmcLayoutHatch& Candidate : Building.Hatches)
		{
			const float Distance = FVector2D::Distance(Candidate.Pos, Goal);
			if (Hatch == nullptr || Distance < HatchDistance)
			{
				Hatch = &Candidate;
				HatchDistance = Distance;
			}
		}
		if (Hatch != nullptr)
		{
			Access.Kind = EPmcAccessKind::Hatch;
			Access.Source = EPmcAccessSource::Layout;
			Access.Id = Hatch->Id;
			Access.Location = FVector(Hatch->Pos.X, Hatch->Pos.Y, Z);
			return Access;
		}

		const FString RoofTo = FString::Printf(TEXT("roof:%s"), *Building.Id);
		const FPmcLayoutLadder* Ladder = nullptr;
		float LadderDistance = 0.f;
		for (const FPmcLayoutLadder& Candidate : Layout.Ladders)
		{
			if (!Candidate.To.Equals(RoofTo, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const float Distance = FVector2D::Distance(Candidate.Top, Goal);
			if (Ladder == nullptr || Distance < LadderDistance)
			{
				Ladder = &Candidate;
				LadderDistance = Distance;
			}
		}
		if (Ladder != nullptr)
		{
			Access.Kind = EPmcAccessKind::Ladder;
			Access.Source = EPmcAccessSource::Layout;
			Access.Id = Ladder->Id;
			Access.Location = FVector(Ladder->Top.X, Ladder->Top.Y, Z);
		}
		return Access;
	}

	FPmcDefenceAccess ResolveTacticalAccess(const FPmcRoofRef& Roof, TConstArrayView<FPmcLadderTop> LadderTops, const FPmcLayoutAccess* Layout)
	{
		using namespace PmcDefenceLocal;
		FPmcDefenceAccess Access;
		TArray<FString> Ids;
		CollectIds(Roof, Ids);
		if (Roof.AccessKind == EPmcAccessKind::Ladder)
		{
			const FPmcLadderTop* Match = nullptr;
			for (const FPmcLadderTop& Top : LadderTops)
			{
				if (!Roof.Building.IsEmpty() && !Top.Building.Equals(Roof.Building, ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (Ids.Num() > 0 && !Ids.Contains(Top.LadderId))
				{
					continue;
				}
				Match = &Top;
				break;
			}
			if (Match == nullptr)
			{
				for (const FPmcLadderTop& Top : LadderTops)
				{
					if (Ids.Contains(Top.LadderId))
					{
						Match = &Top;
						break;
					}
				}
			}
			if (Match != nullptr)
			{
				Access.Kind = EPmcAccessKind::Ladder;
				Access.Source = EPmcAccessSource::Tactical;
				Access.Id = Match->LadderId;
				Access.Location = Match->Location;
			}
			return Access;
		}

		if (Layout == nullptr || (Roof.AccessKind != EPmcAccessKind::Stairs && Roof.AccessKind != EPmcAccessKind::Hatch))
		{
			return Access;
		}
		const FPmcLayoutBuilding* Preferred = FindBuilding(*Layout, Roof.Building);
		auto Search = [&](const FPmcLayoutBuilding& Building) -> bool
		{
			if (Roof.AccessKind == EPmcAccessKind::Stairs)
			{
				for (const FPmcLayoutStair& Stair : Building.Stairs)
				{
					if (!Ids.Contains(Stair.Id))
					{
						continue;
					}
					Access.Kind = EPmcAccessKind::Stairs;
					Access.Source = EPmcAccessSource::Layout;
					Access.Id = Stair.Id;
					Access.Location = FVector(Stair.Top.X, Stair.Top.Y, Roof.Z);
					return true;
				}
			}
			else
			{
				for (const FPmcLayoutHatch& Hatch : Building.Hatches)
				{
					if (!Ids.Contains(Hatch.Id))
					{
						continue;
					}
					Access.Kind = EPmcAccessKind::Hatch;
					Access.Source = EPmcAccessSource::Layout;
					Access.Id = Hatch.Id;
					Access.Location = FVector(Hatch.Pos.X, Hatch.Pos.Y, Roof.Z);
					return true;
				}
			}
			return false;
		};
		if (Preferred != nullptr && Search(*Preferred))
		{
			return Access;
		}
		for (const FPmcLayoutBuilding& Building : Layout->Buildings)
		{
			if (Preferred != nullptr && &Building == Preferred)
			{
				continue;
			}
			if (Search(Building))
			{
				return Access;
			}
		}
		return Access;
	}

	int32 PickRoof(TConstArrayView<FPmcRoofRef> Roofs, const FVector& Objective, float LevelToleranceCm)
	{
		int32 Best = INDEX_NONE;
		float BestDistance = 0.f;
		for (int32 Index = 0; Index < Roofs.Num(); ++Index)
		{
			const FPmcRoofRef& Roof = Roofs[Index];
			if (FMath::Abs(Roof.Z - Objective.Z) > LevelToleranceCm)
			{
				continue;
			}
			for (const FPmcDefenceSpot& Spot : Roof.Spots)
			{
				const float Distance = FVector::Dist2D(Spot.Location, Objective);
				if (Best == INDEX_NONE || Distance < BestDistance)
				{
					Best = Index;
					BestDistance = Distance;
				}
			}
		}
		return Best;
	}

	FVector SpotCentroid(TConstArrayView<FPmcDefenceSpot> Spots)
	{
		if (Spots.Num() == 0)
		{
			return FVector::ZeroVector;
		}
		FVector Sum = FVector::ZeroVector;
		for (const FPmcDefenceSpot& Spot : Spots)
		{
			Sum += Spot.Location;
		}
		return Sum / static_cast<float>(Spots.Num());
	}

	FVector GuardPostLocation(const FPmcDefenceAccess& Access, const FVector& InwardTarget, float GuardInsetCm)
	{
		FVector Direction = InwardTarget - Access.Location;
		Direction.Z = 0.f;
		if (!Direction.Normalize())
		{
			Direction = FVector(1.f, 0.f, 0.f);
		}
		FVector Post = Access.Location + Direction * GuardInsetCm;
		Post.Z = Access.Location.Z;
		return Post;
	}

	float YawDegrees(const FVector& From, const FVector& To)
	{
		const FVector Delta = To - From;
		if (FMath::IsNearlyZero(Delta.X) && FMath::IsNearlyZero(Delta.Y))
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	}

	int32 SectorIndex(const FVector& Origin, const FVector& Point, float ApproachYawDeg, int32 SectorCount, float ArcDeg)
	{
		using namespace PmcDefenceLocal;
		if (SectorCount <= 0 || ArcDeg <= 0.f)
		{
			return INDEX_NONE;
		}
		if (FVector::Dist2D(Origin, Point) < 1.f)
		{
			return INDEX_NONE;
		}
		const float Relative = Delta360(YawDegrees(Origin, Point), SectorStartDeg(ApproachYawDeg, ArcDeg));
		if (Relative > ArcDeg + 0.05f)
		{
			return INDEX_NONE;
		}
		const float Wedge = ArcDeg / static_cast<float>(SectorCount);
		int32 Index = FMath::FloorToInt(Relative / Wedge);
		return FMath::Clamp(Index, 0, SectorCount - 1);
	}

	int32 PickSectorSpot(TConstArrayView<FPmcDefenceSpot> Spots, const FVector& Origin, float ApproachYawDeg, int32 Sector,
		int32 SectorCount, float ArcDeg)
	{
		int32 Best = INDEX_NONE;
		float BestCovered = 0.f;
		for (int32 Index = 0; Index < Spots.Num(); ++Index)
		{
			if (SectorIndex(Origin, Spots[Index].Location, ApproachYawDeg, SectorCount, ArcDeg) != Sector)
			{
				continue;
			}
			if (Best == INDEX_NONE || Spots[Index].CoveredM > BestCovered)
			{
				Best = Index;
				BestCovered = Spots[Index].CoveredM;
			}
		}
		return Best;
	}

	int32 NextEdgeSpot(TConstArrayView<FPmcDefenceSpot> Spots, const FVector& Origin, float ApproachYawDeg, int32 Sector,
		int32 SectorCount, float ArcDeg, int32 CurrentSpot)
	{
		using namespace PmcDefenceLocal;
		TArray<int32> Ordered;
		SpotsInSector(Spots, Origin, ApproachYawDeg, Sector, SectorCount, ArcDeg, Ordered);
		if (Ordered.Num() == 0)
		{
			return INDEX_NONE;
		}
		const int32 Found = Ordered.IndexOfByKey(CurrentSpot);
		if (Found == INDEX_NONE)
		{
			return Ordered[0];
		}
		return Ordered[(Found + 1) % Ordered.Num()];
	}

	TArray<FPmcDefencePost> AssignRoles(TConstArrayView<FPmcDefenceMan> Men, const FPmcDefenceAccess& Access, const FVector& Objective,
		float ApproachYawDeg, TConstArrayView<FPmcDefenceSpot> Spots, TConstArrayView<FPmcDefenceSlot> Slots, const FVector* InwardTarget,
		const FPmcDefenceConfig& Config)
	{
		TArray<FPmcDefencePost> Posts;
		Posts.SetNum(Men.Num());
		TArray<int32> Order;
		Order.Reserve(Men.Num());
		for (int32 Slot = 0; Slot < Men.Num(); ++Slot)
		{
			Posts[Slot].Index = Men[Slot].Index;
			Order.Add(Slot);
		}
		Order.StableSort([&Men](int32 A, int32 B) { return Men[A].Index < Men[B].Index; });

		int32 GuardSlot = INDEX_NONE;
		if (Access.IsSet() && Men.Num() > 0)
		{
			for (const int32 Slot : Order)
			{
				if (Men[Slot].PreviousRole == EPmcDefenceRole::AccessGuard)
				{
					GuardSlot = Slot;
					break;
				}
			}
			if (GuardSlot == INDEX_NONE)
			{
				float BestDistance = 0.f;
				for (const int32 Slot : Order)
				{
					const float Distance = FVector::Dist2D(Men[Slot].Feet, Access.Location);
					if (GuardSlot == INDEX_NONE || Distance < BestDistance - 0.01f)
					{
						GuardSlot = Slot;
						BestDistance = Distance;
					}
				}
			}
		}

		TArray<int32> Shooters;
		for (const int32 Slot : Order)
		{
			if (Slot != GuardSlot)
			{
				Shooters.Add(Slot);
			}
		}

		FVector Inward = Objective;
		if (InwardTarget != nullptr)
		{
			Inward = *InwardTarget;
		}
		else if (Spots.Num() > 0)
		{
			Inward = SpotCentroid(Spots);
		}

		if (GuardSlot != INDEX_NONE)
		{
			FPmcDefencePost& Guard = Posts[GuardSlot];
			Guard.Role = EPmcDefenceRole::AccessGuard;
			Guard.Sector = INDEX_NONE;
			Guard.Location = GuardPostLocation(Access, Inward, Config.GuardInsetCm);
			Guard.Yaw = YawDegrees(Guard.Location, Access.Location);
			Guard.bHasPost = true;
		}

		const int32 SectorCount = Shooters.Num();
		float FallbackRadius = 400.f;
		for (const FPmcDefenceSpot& Spot : Spots)
		{
			FallbackRadius = FMath::Max(FallbackRadius, FVector::Dist2D(Objective, Spot.Location));
		}
		for (int32 Sector = 0; Sector < SectorCount; ++Sector)
		{
			FPmcDefencePost& Post = Posts[Shooters[Sector]];
			Post.Role = EPmcDefenceRole::SectorShooter;
			Post.Sector = Sector;
			Post.Yaw = ApproachYawDeg;
			Post.bHasPost = true;
			const int32 SpotIndex = PickSectorSpot(Spots, Objective, ApproachYawDeg, Sector, SectorCount, Config.SectorArcDeg);
			if (SpotIndex != INDEX_NONE)
			{
				Post.SpotIndex = SpotIndex;
				Post.Location = Spots[SpotIndex].Location;
				continue;
			}
			int32 SlotIndex = INDEX_NONE;
			float BestScore = 0.f;
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				if (SectorIndex(Objective, Slots[Index].Location, ApproachYawDeg, SectorCount, Config.SectorArcDeg) != Sector)
				{
					continue;
				}
				if (SlotIndex == INDEX_NONE || Slots[Index].Score > BestScore)
				{
					SlotIndex = Index;
					BestScore = Slots[Index].Score;
				}
			}
			if (SlotIndex != INDEX_NONE)
			{
				Post.Location = Slots[SlotIndex].Location;
				continue;
			}
			const float Wedge = Config.SectorArcDeg / static_cast<float>(SectorCount);
			const float Yaw = ApproachYawDeg - Config.SectorArcDeg * 0.5f + (static_cast<float>(Sector) + 0.5f) * Wedge;
			const float Radians = FMath::DegreesToRadians(Yaw);
			Post.Location = Objective + FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.f) * FallbackRadius;
			Post.Location.Z = Objective.Z;
		}
		return Posts;
	}

	bool IsInsideDefendedArea(const FVector& Feet, const FVector& Objective, float LevelZ, float RadiusCm,
		TConstArrayView<FPmcDefenceSpot> Spots, const FPmcDefenceConfig& Config)
	{
		using namespace PmcDefenceLocal;
		if (!SameLevel(Feet.Z, LevelZ, Config.LevelToleranceCm))
		{
			return false;
		}
		if (Spots.Num() > 0)
		{
			const FVector Centroid = SpotCentroid(Spots);
			float Furthest = 0.f;
			for (const FPmcDefenceSpot& Spot : Spots)
			{
				Furthest = FMath::Max(Furthest, FVector::Dist2D(Centroid, Spot.Location));
			}
			return FVector::Dist2D(Feet, Centroid) <= Furthest + Config.AreaSlackCm;
		}
		return FVector::Dist2D(Feet, Objective) <= RadiusCm * Config.AreaFactor;
	}

	bool IsInsideAreaFallback(bool bOnObjectiveLevel, float DistanceCm, float RadiusCm, float AreaFactor)
	{
		return bOnObjectiveLevel && DistanceCm <= RadiusCm * AreaFactor;
	}

	float DefenderObjectiveScore(bool bInsideArea, float ObjectiveWeight, float HealthFrac)
	{
		if (bInsideArea)
		{
			return 0.f;
		}
		return FMath::Max(ObjectiveWeight, 0.f) * 0.7f * FMath::Max(0.5f, HealthFrac);
	}

	bool ShouldSectorShift(float SecondsOnPost, bool bUnderFire, float SecondsNoTarget, float SecondsSinceShift,
		const FPmcDefenceConfig& Config)
	{
		if (Config.ShiftAfterSeconds <= 0.f || SecondsOnPost <= 0.f || SecondsOnPost < Config.ShiftAfterSeconds)
		{
			return false;
		}
		if (SecondsSinceShift < Config.ShiftCooldownSeconds)
		{
			return false;
		}
		const bool bIdle = Config.ShiftNoTargetSeconds > 0.f && SecondsNoTarget >= Config.ShiftNoTargetSeconds;
		return bUnderFire || bIdle;
	}

	bool IsGoalOnPostLevel(bool bHasDefencePost, float GoalZ, float PostZ, float ToleranceCm)
	{
		return !bHasDefencePost || ToleranceCm <= 0.f || FMath::Abs(GoalZ - PostZ) <= ToleranceCm;
	}

	FString CoverObjectKey(const FString& WallActorId, const FVector& Point)
	{
		if (!WallActorId.IsEmpty() && !WallActorId.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return WallActorId;
		}
		const int32 X = FMath::RoundToInt(Point.X / 100.f);
		const int32 Y = FMath::RoundToInt(Point.Y / 100.f);
		return FString::Printf(TEXT("@%d,%d"), X, Y);
	}

	float SpacingFactor(const FVector& Slot, const FString& SlotKey, TConstArrayView<FVector> Claimed,
		TConstArrayView<FString> ClaimedKeys, const FPmcDefenceConfig& Config)
	{
		bool bClose = false;
		for (int32 Index = 0; Index < Claimed.Num(); ++Index)
		{
			const FString& Other = ClaimedKeys.IsValidIndex(Index) ? ClaimedKeys[Index] : FString();
			if (!SlotKey.IsEmpty() && !Other.IsEmpty() && SlotKey == Other)
			{
				return 0.f;
			}
			if (FVector::Dist2D(Slot, Claimed[Index]) < Config.MinSpacingCm)
			{
				bClose = true;
			}
		}
		return bClose ? Config.SpacingPenalty : 1.f;
	}

	int32 SharedCoverPairs(TConstArrayView<FString> CoverKeys)
	{
		TMap<FString, int32> Counts;
		for (const FString& Key : CoverKeys)
		{
			if (Key.IsEmpty())
			{
				continue;
			}
			Counts.FindOrAdd(Key) += 1;
		}
		int32 Pairs = 0;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			if (Pair.Value >= 2)
			{
				Pairs += Pair.Value * (Pair.Value - 1) / 2;
			}
		}
		return Pairs;
	}

	float MeanSpreadM(TConstArrayView<FVector> Feet)
	{
		if (Feet.Num() < 2)
		{
			return 0.f;
		}
		double Sum = 0.0;
		int32 Pairs = 0;
		for (int32 I = 0; I < Feet.Num(); ++I)
		{
			for (int32 J = I + 1; J < Feet.Num(); ++J)
			{
				Sum += FVector::Dist2D(Feet[I], Feet[J]);
				++Pairs;
			}
		}
		return Pairs > 0 ? static_cast<float>(Sum / static_cast<double>(Pairs)) / 100.f : 0.f;
	}

	FPmcGuardSample SampleGuard(TConstArrayView<FVector> LivingFeet, const FPmcDefenceAccess& Access, float GuardDistCm, float LevelToleranceCm)
	{
		using namespace PmcDefenceLocal;
		FPmcGuardSample Sample;
		if (!Access.IsSet())
		{
			return Sample;
		}
		Sample.bApplicable = true;
		for (const FVector& Feet : LivingFeet)
		{
			if (!SameLevel(Feet.Z, Access.Location.Z, LevelToleranceCm))
			{
				continue;
			}
			if (FVector::Dist2D(Feet, Access.Location) <= GuardDistCm)
			{
				Sample.bNear = true;
				break;
			}
		}
		return Sample;
	}

	bool TryGuardShare(int32 NearSamples, int32 ApplicableSamples, float& OutShare)
	{
		if (ApplicableSamples <= 0)
		{
			return false;
		}
		OutShare = static_cast<float>(NearSamples) / static_cast<float>(ApplicableSamples);
		return true;
	}

	float ObjectiveOrderShare(int32 ObjectiveOrders, int32 AllOrders)
	{
		return AllOrders > 0 ? static_cast<float>(ObjectiveOrders) / static_cast<float>(AllOrders) : 0.f;
	}

	FString AccessLogLine(int32 Team, const FPmcDefenceAccess& Access)
	{
		if (!Access.IsSet())
		{
			return FString::Printf(TEXT("[PmcDefence] team %d access none"), Team);
		}
		return FString::Printf(TEXT("[PmcDefence] team %d access %s at (%.0f, %.0f, %.0f) source=%s"),
			Team, AccessKindName(Access.Kind), Access.Location.X, Access.Location.Y, Access.Location.Z, AccessSourceName(Access.Source));
	}
}
