#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CheckpointManager.generated.h"

/**
 *
 */
UCLASS()
class ACTIONDEMO_API UCheckpointManager : public UWorldSubsystem
{
    GENERATED_BODY()

protected:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

public:
    inline void SetCheckpointIndex(const int32 InCheckpointIndex) { CurrentCheckpoint = FMath::Max(CurrentCheckpoint, InCheckpointIndex); }

    inline const int32 GetCheckpointIndex() const { return CurrentCheckpoint; }

    inline void AddCheckpoint(const int32 InCheckpointIndex, const FTransform InCheckpoint) { CheckpointMap.Add(InCheckpointIndex, InCheckpoint); }

    inline FTransform* GetCheckpoint(const int32 CheckpointIndex) { return CheckpointMap.Find(CheckpointIndex); }

    inline FTransform* GetLastCheckpoint() { return CheckpointMap.Find(CurrentCheckpoint); }

private:
    TMap<int32, FTransform> CheckpointMap;

    int32 CurrentCheckpoint = 0;
};
