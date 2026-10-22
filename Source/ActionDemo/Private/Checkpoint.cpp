#include "Checkpoint.h"

#include "ActionDemo/ActionDemoCharacter.h"
#include "CheckpointManager.h"

void ACheckpoint::BeginPlay()
{
	Super::BeginPlay();

	OnActorBeginOverlap.AddDynamic(this, &ACheckpoint::OnBeginOverlap);
}

void ACheckpoint::OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!HasAuthority())
	{
		return;
	}

	AActionDemoCharacter* Character = Cast<AActionDemoCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	if (UCheckpointManager* Manager = GetWorld()->GetSubsystem<UCheckpointManager>())
	{
		Manager->SetCharacterCheckpoint(Character, CheckpointIndex);
	}
}
