#include "LandscapeAuthoringLibrary.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "RHIShaderPlatform.h"

DEFINE_LOG_CATEGORY_STATIC(LogLandscapeAuthoring, Log, All);

uint16 ULandscapeAuthoringLibrary::EncodeHeight(float HeightCm, float ScaleZ)
{
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(ScaleZ));
	const float Value = 32768.f + HeightCm * LANDSCAPE_INV_ZSCALE / Scale;
	return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Value), 0, 65535));
}

ALandscape* ULandscapeAuthoringLibrary::CreateLandscapeFromHeights(UWorld* World, FVector Location, FVector Scale, int32 ComponentsX, int32 ComponentsY,
	int32 QuadsPerComponent, const TArray<float>& HeightsCm, UMaterialInterface* Material)
{
	static const int32 ValidQuads[] = { 7, 15, 31, 63, 127, 255 };
	bool bValidQuads = false;
	for (const int32 Quads : ValidQuads)
	{
		bValidQuads |= Quads == QuadsPerComponent;
	}
	if (!World || ComponentsX < 1 || ComponentsY < 1 || !bValidQuads)
	{
		UE_LOG(LogLandscapeAuthoring, Error, TEXT("CreateLandscapeFromHeights: bad input (world %s, components %d x %d, quads %d)"), *GetNameSafe(World),
			ComponentsX, ComponentsY, QuadsPerComponent);
		return nullptr;
	}
	const int32 SizeX = ComponentsX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentsY * QuadsPerComponent + 1;
	if (HeightsCm.Num() != SizeX * SizeY)
	{
		UE_LOG(LogLandscapeAuthoring, Error, TEXT("CreateLandscapeFromHeights: %d heights given, %d x %d = %d expected"), HeightsCm.Num(), SizeX, SizeY, SizeX * SizeY);
		return nullptr;
	}

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(HeightsCm.Num());
	for (int32 Index = 0; Index < HeightsCm.Num(); ++Index)
	{
		Heights[Index] = EncodeHeight(HeightsCm[Index], Scale.Z);
	}
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	if (!Landscape)
	{
		UE_LOG(LogLandscapeAuthoring, Error, TEXT("CreateLandscapeFromHeights: could not spawn a landscape"));
		return nullptr;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, /*InNumSubsections*/ 1, /*InSubsectionSizeQuads*/ QuadsPerComponent, HeightDataPerLayers,
		TEXT(""), MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	// The edit layers resolve the final heightmap on the GPU, normally on the editor tick: force it now (commandlet).
	Landscape->ForceUpdateLayersContent();
	UE_LOG(LogLandscapeAuthoring, Display, TEXT("CreateLandscapeFromHeights: %s, %d x %d vertices, scale %s, material %s"), *Landscape->GetName(), SizeX, SizeY,
		*Scale.ToCompactString(), *GetNameSafe(Material));
	return Landscape;
}

FString ULandscapeAuthoringLibrary::GetMaxShaderPlatformName()
{
	return FDataDrivenShaderPlatformInfo::GetName(GMaxRHIShaderPlatform).ToString();
}
