// Fill out your copyright notice in the Description page of Project Settings.


#include "Checkpoint.h"
#include "GameFramework/Character.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "Kismet/GameplayStatics.h"

void ACheckpoint::BeginPlay()
{
    Super::BeginPlay();
    OnActorBeginOverlap.AddDynamic(this, &ACheckpoint::OnBeginOverlap);
}

void ACheckpoint::OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    AActionDemoCharacter* Player = Cast<AActionDemoCharacter>(OtherActor);
    if (!Player) return;

    Player->SetCheckpoint(CheckpointIndex, GetActorTransform());
}