#include "Formation/FormationCustomRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace FormationCustomRulesLocal
{
	/** custom.ts round1: sign(v) x round(|v| x 10) / 10, -0 -> 0 (Math.round = floor(x + 0.5) for x >= 0). */
	double RoundTenth(double V)
	{
		const double R = FMath::Sign(V) * FMath::FloorToDouble(FMath::Abs(V) * 10.0 + 0.5) / 10.0;
		return R == 0.0 ? 0.0 : R;
	}

	/** geometry.ts localToWorld: Origin + forward x Local.X + right x Local.Y (right = (-f.y, f.x)). */
	FVector2D LocalToWorld(const FVector2D& Origin, double HeadingYaw, double Fwd, double Right)
	{
		const FVector2D F = FormationMath::DirOf(HeadingYaw);
		const FVector2D R(-F.Y, F.X);
		return FVector2D(Origin.X + F.X * Fwd + R.X * Right, Origin.Y + F.Y * Fwd + R.Y * Right);
	}
}

namespace FormationCustomRules
{
	FFormationCustomShape RoundShape(const FFormationCustomShape& Shape)
	{
		using namespace FormationCustomRulesLocal;
		FFormationCustomShape Out;
		for (const FFormationCustomMember& Member : Shape.Members)
		{
			FFormationCustomMember& Each = Out.Members.AddDefaulted_GetRef();
			Each.Squad = Member.Squad;
			Each.Fwd = static_cast<float>(RoundTenth(Member.Fwd));
			Each.Right = static_cast<float>(RoundTenth(Member.Right));
			Each.Look = static_cast<float>(FormationMath::NormYaw(RoundTenth(Member.Look)));
		}
		return Out;
	}

	TArray<FFormationCustomSlot> CustomSlots(const FFormationCustomShape& Shape, const TArray<int32>& Present, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config)
	{
		using namespace FormationCustomRulesLocal;
		TArray<FFormationCustomSlot> Result;
		TArray<int32> Who = Present;
		if (Who.Num() > MaxMembers)
		{
			Who.SetNum(MaxMembers);
		}
		if (Shape.IsEmpty() || Who.IsEmpty())
		{
			return Result;
		}
		const int32 LeaderSquad = Who[0];
		const int32 AnchorSquad = Shape.Members[0].Squad;
		// The first stored place of each squad index.
		const auto StoredPlace = [&Shape](int32 Squad) -> int32
		{
			return Shape.Members.IndexOfByPredicate([Squad](const FFormationCustomMember& M) { return M.Squad == Squad; });
		};
		const int32 LeaderPlace = StoredPlace(LeaderSquad);
		TArray<int32> Places;
		for (const int32 Squad : Who)
		{
			if (Squad == LeaderSquad)
			{
				Places.Add(0);
			}
			else if (Squad == AnchorSquad)
			{
				Places.Add(LeaderPlace); // the anchor steps into the leader's place (INDEX_NONE: an extra)
			}
			else
			{
				Places.Add(StoredPlace(Squad));
			}
		}
		double MinFwd = 0.0;
		for (const int32 Place : Places)
		{
			if (Place != INDEX_NONE)
			{
				MinFwd = FMath::Min(MinFwd, static_cast<double>(Shape.Members[Place].Fwd));
			}
		}
		const TArray<EFormationRole> Roles = FormationShapeRules::Roles(Who.Num());
		int32 Extra = 0;
		for (int32 Index = 0; Index < Who.Num(); ++Index)
		{
			FFormationCustomSlot& Slot = Result.AddDefaulted_GetRef();
			Slot.Squad = Who[Index];
			Slot.Role = Roles.IsValidIndex(Index) ? Roles[Index] : EFormationRole::Rear;
			const int32 Place = Places[Index];
			if (Place != INDEX_NONE)
			{
				const FFormationCustomMember& Member = Shape.Members[Place];
				Slot.Position = LocalToWorld(LeaderPos, LeaderYaw, Member.Fwd, Member.Right);
				Slot.Yaw = static_cast<float>(FormationMath::NormYaw(static_cast<double>(LeaderYaw) + Member.Look));
				Slot.bFixed = true;
			}
			else
			{
				++Extra;
				Slot.Position = LocalToWorld(LeaderPos, LeaderYaw, MinFwd - static_cast<double>(Extra) * Config.ColumnSpacing, 0.0);
				Slot.Yaw = static_cast<float>(FormationMath::NormYaw(static_cast<double>(LeaderYaw) + 180.0));
				Slot.bFixed = false;
			}
		}
		return Result;
	}

	TArray<EFormationRole> BlockedRoles(const TArray<FFormationWall>& Walls, const TArray<FFormationCustomSlot>& Slots, const FVector2D& LeaderPos,
		const FFormationConfig& Config)
	{
		TArray<EFormationRole> Result;
		for (int32 Index = 1; Index < Slots.Num(); ++Index)
		{
			if (FormationMath::SegmentBlocked(LeaderPos, Slots[Index].Position, Walls, Config.GuardClearance))
			{
				Result.Add(Slots[Index].Role);
			}
		}
		return Result;
	}

	FFormationGuardState Resolve(const TArray<FFormationWall>& Walls, const FFormationCustomShape& Shape, const TArray<int32>& Present, const FVector2D& LeaderPos,
		float LeaderYaw, const TOptional<FFormationPose>& Ahead, const FFormationGuardState& State, float StepDistance, const FFormationConfig& Config,
		bool bAllInSight)
	{
		const auto BlockedAt = [&](const FVector2D& Pos, float Yaw)
		{
			return BlockedRoles(Walls, CustomSlots(Shape, Present, Pos, Yaw, Config), Pos, Config).Num() > 0;
		};
		const bool bRawTrail = BlockedAt(LeaderPos, LeaderYaw) || (Ahead.IsSet() && BlockedAt(Ahead->Pos, Ahead->Yaw)) || !bAllInSight;
		const EFormationGuardLevel Raw = bRawTrail ? EFormationGuardLevel::Trail : EFormationGuardLevel::Requested;
		const auto At = [](EFormationGuardLevel Level, TOptional<EFormationGuardLevel> Candidate, float CandidateDistance)
		{
			FFormationGuardState Out;
			Out.Requested = EFormationKind::Custom;
			Out.Level = Level;
			Out.Effective = Level == EFormationGuardLevel::Requested ? EFormationKind::Custom : EFormationKind::Column;
			Out.Candidate = Candidate;
			Out.CandidateDistance = CandidateDistance;
			if (Level != EFormationGuardLevel::Requested)
			{
				Out.Cause = EFormationGuardCause::Wall;
			}
			return Out;
		};
		const bool bSameRequest = State.Requested.IsSet() && State.Requested.GetValue() == EFormationKind::Custom;
		if (!bSameRequest || static_cast<uint8>(Raw) >= static_cast<uint8>(State.Level))
		{
			return At(Raw, TOptional<EFormationGuardLevel>(), 0.f);
		}
		const bool bWaiting = State.Candidate.IsSet() && State.Candidate.GetValue() == EFormationGuardLevel::Requested;
		const double Walked = (bWaiting ? static_cast<double>(State.CandidateDistance) : 0.0) + FMath::Max(0.0, static_cast<double>(StepDistance));
		if (Walked >= static_cast<double>(Config.ClassifyHoldDistance))
		{
			return At(EFormationGuardLevel::Requested, TOptional<EFormationGuardLevel>(), 0.f);
		}
		FFormationGuardState Held = At(EFormationGuardLevel::Trail, EFormationGuardLevel::Requested, static_cast<float>(Walked));
		Held.Cause = State.Cause.IsSet() ? State.Cause : TOptional<EFormationGuardCause>(EFormationGuardCause::Wall);
		return Held;
	}

	float Look(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float CurrentYaw, const FFormationConfig& Config, const TOptional<float>& Held)
	{
		const double Reach = Config.LookWallDist;
		const double Half = Config.LookWallAngle;
		bool bBlocked = false;
		for (const FFormationWall& Wall : Walls)
		{
			const FVector2D Q = FormationMath::ClosestOnSegment(Pos, Wall.A, Wall.B);
			const double D = FVector2D::Distance(Pos, Q);
			if (FMath::Max(0.0, D - static_cast<double>(Wall.Thickness) / 2.0) > Reach || D < 1e-6)
			{
				continue;
			}
			if (FMath::Abs(FormationMath::NormYaw(static_cast<double>(Yaw) - FormationMath::YawOfD(Q - Pos))) < Half - 1e-6)
			{
				bBlocked = true;
				break;
			}
		}
		return bBlocked ? FormationSectorRules::ClearLook(Walls, Pos, Yaw, CurrentYaw, Config, Held) : static_cast<float>(FormationMath::NormYaw(Yaw));
	}

	float DragLook(const FVector2D& GhostPos, const FVector2D& CursorPos, float DefaultYaw)
	{
		return FVector2D::Distance(GhostPos, CursorPos) < DragMin ? static_cast<float>(FormationMath::NormYaw(DefaultYaw))
			: static_cast<float>(FormationMath::YawOfD(CursorPos - GhostPos));
	}

	EFormationCustomPlace CanPlace(const TArray<FFormationCustomGhost>& Ghosts, int32 AnchorSquad, int32 Squad, const FVector2D& Pos, const FFormationConfig& Config)
	{
		TArray<FFormationCustomGhost> After = Ghosts;
		FFormationCustomGhost* Own = After.FindByPredicate([Squad](const FFormationCustomGhost& G) { return G.Squad == Squad; });
		if (Own)
		{
			Own->Position = Pos;
		}
		else
		{
			FFormationCustomGhost& Added = After.AddDefaulted_GetRef();
			Added.Squad = Squad;
			Added.Position = Pos;
		}
		for (const FFormationCustomGhost& Ghost : After)
		{
			if (Ghost.Squad != Squad && FVector2D::Distance(Ghost.Position, Pos) < static_cast<double>(Config.SlotMinSpacing))
			{
				return EFormationCustomPlace::TooClose;
			}
		}
		const FFormationCustomGhost* Anchor = After.FindByPredicate([AnchorSquad](const FFormationCustomGhost& G) { return G.Squad == AnchorSquad; });
		if (Anchor)
		{
			for (const FFormationCustomGhost& Ghost : After)
			{
				if (FVector2D::Distance(Ghost.Position, Anchor->Position) > MaxRadius)
				{
					return EFormationCustomPlace::TooFar;
				}
			}
		}
		return EFormationCustomPlace::Ok;
	}

	FFormationCustomShape ShapeFrom(const TArray<FFormationCustomGhost>& Ghosts, int32 AnchorSquad)
	{
		FFormationCustomShape Shape;
		const FFormationCustomGhost* Anchor = Ghosts.FindByPredicate([AnchorSquad](const FFormationCustomGhost& G) { return G.Squad == AnchorSquad; });
		if (!Anchor)
		{
			return Shape;
		}
		const FVector2D F = FormationMath::DirOf(Anchor->Yaw);
		const FVector2D R(-F.Y, F.X);
		TArray<FFormationCustomGhost> Others = Ghosts.FilterByPredicate([AnchorSquad](const FFormationCustomGhost& G) { return G.Squad != AnchorSquad; });
		Others.StableSort([](const FFormationCustomGhost& A, const FFormationCustomGhost& B) { return A.Squad < B.Squad; });
		FFormationCustomMember& First = Shape.Members.AddDefaulted_GetRef();
		First.Squad = AnchorSquad;
		for (int32 Index = 0; Index < Others.Num() && Index < MaxMembers - 1; ++Index)
		{
			const FVector2D D = Others[Index].Position - Anchor->Position;
			FFormationCustomMember& Member = Shape.Members.AddDefaulted_GetRef();
			Member.Squad = Others[Index].Squad;
			Member.Fwd = static_cast<float>(D.X * F.X + D.Y * F.Y);
			Member.Right = static_cast<float>(D.X * R.X + D.Y * R.Y);
			Member.Look = static_cast<float>(FormationMath::NormYaw(static_cast<double>(Others[Index].Yaw) - Anchor->Yaw));
		}
		return RoundShape(Shape);
	}

	TSharedPtr<FJsonObject> ToJson(const FFormationCustomShape& Shape)
	{
		if (Shape.IsEmpty())
		{
			return nullptr;
		}
		TArray<TSharedPtr<FJsonValue>> Members;
		for (const FFormationCustomMember& Member : Shape.Members)
		{
			const TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
			Each->SetNumberField(TEXT("squad"), Member.Squad);
			Each->SetNumberField(TEXT("fwd"), Member.Fwd);
			Each->SetNumberField(TEXT("right"), Member.Right);
			Each->SetNumberField(TEXT("look"), Member.Look);
			Members.Add(MakeShared<FJsonValueObject>(Each));
		}
		const TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetArrayField(TEXT("members"), Members);
		return Object;
	}

	FFormationCustomShape FromJson(const TSharedPtr<FJsonObject>& Object)
	{
		FFormationCustomShape Shape;
		const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("members"), Members) || !Members)
		{
			return Shape;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Members)
		{
			const TSharedPtr<FJsonObject>* Each = nullptr;
			double Squad = 0.0;
			if (Shape.Members.Num() >= MaxMembers || !Value.IsValid() || !Value->TryGetObject(Each) || !Each || !(*Each)->TryGetNumberField(TEXT("squad"), Squad)
				|| Shape.Find(static_cast<int32>(Squad)))
			{
				continue;
			}
			FFormationCustomMember& Member = Shape.Members.AddDefaulted_GetRef();
			Member.Squad = static_cast<int32>(Squad);
			double Number = 0.0;
			Member.Fwd = (*Each)->TryGetNumberField(TEXT("fwd"), Number) ? static_cast<float>(Number) : 0.f;
			Member.Right = (*Each)->TryGetNumberField(TEXT("right"), Number) ? static_cast<float>(Number) : 0.f;
			Member.Look = (*Each)->TryGetNumberField(TEXT("look"), Number) ? static_cast<float>(FormationMath::NormYaw(Number)) : 0.f;
		}
		return Shape;
	}

	FString ToJsonString(const FFormationCustomShape& Shape)
	{
		const TSharedPtr<FJsonObject> Object = ToJson(Shape);
		if (!Object.IsValid())
		{
			return FString();
		}
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Text;
	}

	FFormationCustomShape FromJsonString(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return FFormationCustomShape();
		}
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			return FFormationCustomShape();
		}
		return FromJson(Object);
	}
}
