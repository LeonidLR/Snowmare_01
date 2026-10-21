#include "Interactables/VisualMeshRules.h"

FTransform VisualMeshRules::ResolveTransform(bool bArtMesh, const FVector& BoxSizeCm, const FTransform& ArtTransform)
{
	if (bArtMesh)
	{
		return ArtTransform;
	}
	return FTransform(FQuat::Identity, FVector::ZeroVector, BoxSizeCm / PlaceholderCubeCm);
}

FTransform VisualMeshRules::MirrorAcrossY(const FTransform& LeftTransform)
{
	FVector Location = LeftTransform.GetLocation();
	Location.Y = -Location.Y;
	// Mirroring across the XZ plane: negate Y of the location, flip the yaw / roll and the Y scale.
	FRotator Rotation = LeftTransform.Rotator();
	Rotation.Yaw = -Rotation.Yaw;
	Rotation.Roll = -Rotation.Roll;
	FVector Scale = LeftTransform.GetScale3D();
	Scale.Y = -Scale.Y;
	return FTransform(Rotation, Location, Scale);
}
