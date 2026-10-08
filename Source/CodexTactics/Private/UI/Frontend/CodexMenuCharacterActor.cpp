#include "UI/Frontend/CodexMenuCharacterActor.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"

ACodexMenuCharacterActor::ACodexMenuCharacterActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
}

void ACodexMenuCharacterActor::BeginPlay()
{
	Super::BeginPlay();
	if (IdleAnimation && Mesh)
	{
		Mesh->PlayAnimation(IdleAnimation, /*bLooping*/ true);
	}
}
