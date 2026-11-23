// Story-showcase labels (designer test 2026-11-22, item 6) - see StoryLabelSubsystem.h. Dev / test overlay (CodexTacticsDev, never shipped).

#include "Debug/StoryLabelSubsystem.h"

#include "Camera/RoofCutawaySubsystem.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Data/LevelJsonRules.h"
#include "Debug/DebugDrawService.h"
#include "Debug/StoryLabelRules.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DoorActor.h"
#include "Interactables/DoorRules.h"
#include "Interactables/DoorSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace StoryLabelLocal
{
	TAutoConsoleVariable<int32> CVarStoryLabels(TEXT("Codex.Debug.StoryLabels"), -1,
		TEXT("Designer test 2026-11-22: story / door labels over the houses of a generated map (dev overlay). -1 = on the story-showcase maps only, 0 = off, 1 = on any generated map"));

	/** A house label floats this high over its roof, a door label over its leaf, cm. */
	constexpr float HouseAboveRoofCm = 250.f;
	constexpr float DoorAboveLeafCm = 40.f;
	/** Labels refresh (live door states, the cutaway) this often, s. */
	constexpr float RefreshSeconds = 0.25f;

	FString MapNameOf(const UWorld& World)
	{
		return LevelJsonRules::NormalizeMapName(World.GetMapName());
	}

	TSharedPtr<FJsonObject> LoadLayout(const UWorld& World)
	{
		const FString LevelJsonFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / (MapNameOf(World) + TEXT(".json"));
		FString Text;
		TSharedPtr<FJsonObject> LevelJson;
		FString Relative;
		if (!FFileHelper::LoadFileToString(Text, *LevelJsonFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), LevelJson) || !LevelJson.IsValid()
			|| !LevelJson->TryGetStringField(TEXT("levelgen_layout"), Relative) || Relative.IsEmpty())
		{
			return nullptr;
		}
		const FString LayoutPath = FPaths::IsRelative(Relative) ? FPaths::ProjectDir() / Relative : Relative;
		TSharedPtr<FJsonObject> Layout;
		if (!FFileHelper::LoadFileToString(Text, *LayoutPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			return nullptr;
		}
		return Layout;
	}
}

bool UStoryLabelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UStoryLabelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStoryLabelSubsystem, STATGROUP_Tickables);
}

void UStoryLabelSubsystem::Deinitialize()
{
	Clear();
	Super::Deinitialize();
}

int32 UStoryLabelSubsystem::GetHouseLabelCount() const
{
	return Labels.FilterByPredicate([](const FLabel& Label) { return Label.bHouse; }).Num();
}

int32 UStoryLabelSubsystem::GetDoorLabelCount() const
{
	return Labels.FilterByPredicate([](const FLabel& Label) { return !Label.bHouse; }).Num();
}

TArray<FString> UStoryLabelSubsystem::GetLabelTexts(bool bHouses) const
{
	TArray<FString> Out;
	for (const FLabel& Label : Labels)
	{
		if (Label.bHouse == bHouses)
		{
			Out.Add(Label.BuildingId + TEXT(": ") + Label.Text);
		}
	}
	return Out;
}

void UStoryLabelSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	const bool bWant = StoryLabelRules::ShouldShow(StoryLabelLocal::CVarStoryLabels.GetValueOnGameThread(), StoryLabelLocal::MapNameOf(*World));
	if (!bWant)
	{
		if (bBuilt)
		{
			Clear();
		}
		return;
	}
	if (!bBuilt)
	{
		Build();
	}
	RefreshIn -= World->DeltaRealTimeSeconds;
	if (RefreshIn <= 0.f)
	{
		RefreshIn = StoryLabelLocal::RefreshSeconds;
		Refresh();
	}
}

void UStoryLabelSubsystem::Build()
{
	UWorld* World = GetWorld();
	bBuilt = true;
	Labels.Reset();
	const TSharedPtr<FJsonObject> Layout = StoryLabelLocal::LoadLayout(*World);
	TMap<FString, int32> DoorStory;
	if (Layout.IsValid())
	{
		const double GroundZ = Layout->HasField(TEXT("ground_z")) ? Layout->GetNumberField(TEXT("ground_z")) : 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Layout->TryGetArrayField(TEXT("buildings"), Buildings) && Buildings)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Buildings)
			{
				const TSharedPtr<FJsonObject> Building = Value.IsValid() ? Value->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
				if (!Building.IsValid() || !Building->TryGetArrayField(TEXT("footprint"), Footprint) || !Footprint || Footprint->Num() < 2)
				{
					continue;
				}
				TArray<FString> StoryIds;
				const TArray<TSharedPtr<FJsonValue>>* Stories = nullptr;
				if (Building->TryGetArrayField(TEXT("stories"), Stories) && Stories)
				{
					for (const TSharedPtr<FJsonValue>& StoryValue : *Stories)
					{
						const TSharedPtr<FJsonObject> Story = StoryValue.IsValid() ? StoryValue->AsObject() : nullptr;
						if (!Story.IsValid())
						{
							continue;
						}
						const FString Id = Story->GetStringField(TEXT("id"));
						StoryIds.Add(Id);
						TArray<FString> Bricks;
						Story->TryGetStringArrayField(TEXT("bricks"), Bricks);
						StoryLabelRules::DoorStoriesFromBricks(Id, Bricks, DoorStory);
					}
				}
				const TArray<TSharedPtr<FJsonValue>>& A = (*Footprint)[0]->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& B = (*Footprint)[1]->AsArray();
				if (A.Num() < 2 || B.Num() < 2)
				{
					continue;
				}
				FLabel& House = Labels.AddDefaulted_GetRef();
				House.bHouse = true;
				House.BuildingId = Building->GetStringField(TEXT("id"));
				House.Text = StoryLabelRules::HouseLabel(StoryIds);
				const double Height = Building->HasField(TEXT("height")) ? Building->GetNumberField(TEXT("height")) : 600.0;
				House.World = FVector((A[0]->AsNumber() + B[0]->AsNumber()) * 0.5, (A[1]->AsNumber() + B[1]->AsNumber()) * 0.5,
					GroundZ + Height + StoryLabelLocal::HouseAboveRoofCm);
			}
		}
	}
	// Doors: the builder's leaves (door_leaf / door_barricade / opening_fill with door_state=), hidden kit proxies included.
	TSet<FString> Seen;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const TArray<FName>& Tags = It->Tags;
		FString State;
		FString Opening;
		FString BuildingId;
		if (!DoorRules::FindTagValue(Tags, TEXT("door_state"), State) || !DoorRules::FindTagValue(Tags, TEXT("opening"), Opening)
			|| !DoorRules::FindTagValue(Tags, TEXT("building"), BuildingId) || Seen.Contains(Opening))
		{
			continue;
		}
		Seen.Add(Opening);
		FString FloorText;
		int32 Floor = 0;
		if (DoorRules::FindTagValue(Tags, TEXT("floor"), FloorText))
		{
			LexFromString(Floor, *FloorText);
		}
		const FBox Box = It->GetComponentsBoundingBox(true);
		FLabel& Door = Labels.AddDefaulted_GetRef();
		Door.BuildingId = BuildingId;
		Door.OpeningId = Opening;
		Door.BaseState = State;
		Door.Storey = Floor;
		Door.StoryNumber = DoorStory.FindRef(Opening);
		Door.World = FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + StoryLabelLocal::DoorAboveLeafCm);
		Door.Text = StoryLabelRules::DoorLabel(State, FString(), Door.StoryNumber, false);
	}
	Labels.RemoveAll([](const FLabel& Label) { return Label.Text.IsEmpty(); });
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UStoryLabelSubsystem::Draw));
	UE_LOG(LogCodexTactics, Display, TEXT("[StoryLabels] %d house label(s), %d door label(s) on %s (Codex.Debug.StoryLabels)"), GetHouseLabelCount(), GetDoorLabelCount(),
		*StoryLabelLocal::MapNameOf(*World));
	Refresh();
}

void UStoryLabelSubsystem::Clear()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
	Labels.Reset();
	bBuilt = false;
}

void UStoryLabelSubsystem::Refresh()
{
	UWorld* World = GetWorld();
	const UDoorSubsystem* Doors = World->GetSubsystem<UDoorSubsystem>();
	const URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Selected = Squad ? Squad->GetLeader() : nullptr;
	FString InBuilding;
	int32 InStorey = INDEX_NONE;
	if (!Roofs || !Selected || !Roofs->GetOperativeStorey(*Selected, InBuilding, InStorey))
	{
		InStorey = INDEX_NONE;
	}
	for (FLabel& Label : Labels)
	{
		if (Label.bHouse)
		{
			continue;
		}
		Label.bHidden = StoryLabelRules::IsDoorLabelHidden(Label.BuildingId == InBuilding, Label.Storey, InStorey, Label.StoryNumber > 0);
		// The openable doors on: the live state and the strength (an opened door says so).
		if (const ADoorActor* Door = Doors && Doors->IsActive() ? Doors->FindDoor(Label.BuildingId, Label.OpeningId) : nullptr)
		{
			Label.Text = StoryLabelRules::DoorLabel(DoorRules::StateName(Door->GetDoorState()), DoorRules::StrengthName(Door->GetStrength()), Label.StoryNumber, true);
		}
		else
		{
			Label.Text = StoryLabelRules::DoorLabel(Label.BaseState, FString(), Label.StoryNumber, false);
		}
	}
}

void UStoryLabelSubsystem::Draw(UCanvas* Canvas, APlayerController* /*Controller*/)
{
	if (!Canvas || !GEngine || Canvas->SceneView == nullptr)
	{
		return;
	}
	UFont* HouseFont = GEngine->GetLargeFont();
	UFont* DoorFont = GEngine->GetMediumFont();
	int32 Drawn = 0;
	for (const FLabel& Label : Labels)
	{
		if (Label.bHidden || Label.Text.IsEmpty())
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Label.World);
		if (Screen.Z <= 0.f || Screen.X < -200.f || Screen.Y < -50.f || Screen.X > Canvas->ClipX + 200.f || Screen.Y > Canvas->ClipY + 50.f)
		{
			continue; // behind the camera / off screen
		}
		UFont* Font = Label.bHouse ? HouseFont : DoorFont;
		const FLinearColor Color = Label.bHouse ? FLinearColor(1.f, 0.85f, 0.35f)
			: Label.Text.StartsWith(TEXT("заперта")) ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(0.85f, 0.9f, 1.f);
		const FText Text = FText::FromString(Label.bHouse ? FString::Printf(TEXT("%s\n%s"), *Label.Text, *Label.BuildingId) : Label.Text);
		FCanvasTextItem Shadow(FVector2D(Screen.X + 2.f, Screen.Y + 2.f), Text, Font, FLinearColor::Black);
		Shadow.bCentreX = true;
		Shadow.bCentreY = true;
		Canvas->DrawItem(Shadow);
		FCanvasTextItem Item(FVector2D(Screen.X, Screen.Y), Text, Font, Color);
		Item.bCentreX = true;
		Item.bCentreY = true;
		Canvas->DrawItem(Item);
		++Drawn;
	}
	LastDrawn = Drawn;
}
