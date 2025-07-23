#include "CheckpointManager.h"
#include "Kismet/GameplayStatics.h"
#include "Checkpoint.h"

void UCheckpointManager::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(&InWorld, ACheckpoint::StaticClass(), FoundActors);

    for (AActor* Actor : FoundActors)
    {
        CheckpointMap.Empty();
        if (ACheckpoint* Checkpoint = Cast<ACheckpoint>(Actor))
        {
            CheckpointMap.Add(Checkpoint->GetCheckpointIndex(), Checkpoint->GetActorTransform());
        }
    }
}
