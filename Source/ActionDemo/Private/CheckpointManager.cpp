#include "CheckpointManager.h"

#include "ActionDemo/ActionDemoCharacter.h"
#include "Checkpoint.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

bool UCheckpointManager::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		// No need on pure clients; authority runs register / overlap / respawn.
		return World->GetNetMode() != NM_Client;
	}
	return false;
}

bool UCheckpointManager::IsAuthorityWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UCheckpointManager::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!IsAuthorityWorld())
	{
		return;
	}

	CheckpointMap.Empty();

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(&InWorld, ACheckpoint::StaticClass(), FoundActors);
	for (AActor* Actor : FoundActors)
	{
		if (ACheckpoint* Checkpoint = Cast<ACheckpoint>(Actor))
		{
			CheckpointMap.Add(Checkpoint->GetCheckpointIndex(), Checkpoint->GetActorTransform());
		}
	}
}

void UCheckpointManager::RegisterCharacter(AActionDemoCharacter* Character)
{
	if (!IsAuthorityWorld() || !Character)
	{
		return;
	}

	FCharacterCheckpointState& State = CharacterStates.FindOrAdd(Character);
	State.CheckpointIndex = 0;
	State.SpawnTransform = Character->GetActorTransform();
}

void UCheckpointManager::UnregisterCharacter(AActionDemoCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	CharacterStates.Remove(Character);
}

void UCheckpointManager::SetCharacterCheckpoint(AActionDemoCharacter* Character, int32 CheckpointIndex)
{
	if (!IsAuthorityWorld() || !Character)
	{
		return;
	}

	FCharacterCheckpointState& State = CharacterStates.FindOrAdd(Character);
	State.CheckpointIndex = FMath::Max(State.CheckpointIndex, CheckpointIndex);
}

FTransform UCheckpointManager::GetRespawnTransform(AActionDemoCharacter* Character) const
{
	const FCharacterCheckpointState* State = CharacterStates.Find(Character);
	const int32 Index = State ? State->CheckpointIndex : 0;

	if (const FTransform* CheckpointTransform = CheckpointMap.Find(Index))
	{
		return *CheckpointTransform;
	}

	if (State)
	{
		return State->SpawnTransform;
	}

	return Character ? Character->GetActorTransform() : FTransform::Identity;
}

void UCheckpointManager::RespawnCharacter(AActionDemoCharacter* Character)
{
	if (!IsAuthorityWorld() || !Character)
	{
		return;
	}

	const FTransform RespawnTransform = GetRespawnTransform(Character);

	if (AController* CharacterController = Character->GetController())
	{
		CharacterController->SetControlRotation(RespawnTransform.GetRotation().Rotator());
	}

	Character->SetActorLocationAndRotation(
		RespawnTransform.GetLocation(),
		RespawnTransform.GetRotation(),
		false,
		nullptr,
		ETeleportType::ResetPhysics);
}
