// Fill out your copyright notice in the Description page of Project Settings.


#include "Checkpoint.h"
#include "GameFramework/Character.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "CheckpointManager.h"

void ACheckpoint::BeginPlay()
{
    Super::BeginPlay();

    OnActorBeginOverlap.AddDynamic(this, &ACheckpoint::OnBeginOverlap);
}

void ACheckpoint::OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    if (UCheckpointManager* CheckpointManager = GetWorld()->GetSubsystem<UCheckpointManager>())
    {
        CheckpointManager->SetCheckpointIndex(CheckpointIndex);
    }
}