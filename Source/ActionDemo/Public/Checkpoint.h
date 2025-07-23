// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "Checkpoint.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class ACTIONDEMO_API ACheckpoint : public ATriggerBox
{
	GENERATED_BODY()

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

public:
    inline int32 GetCheckpointIndex() { return CheckpointIndex; }

private:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint", meta=(AllowPrivateAccess))
    int32 CheckpointIndex = 0;
};
