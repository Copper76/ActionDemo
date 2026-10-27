#include "CheckpointManager.h"

#include "ActionDemo/ActionDemoCharacter.h"
#include "Checkpoint.h"
#include "DemoMyCharacterMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
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
			FCheckpointRespawnData Data;
			Data.Transform = Checkpoint->GetActorTransform();
			// Use actor up (not GetGravityDirection) so this works even if Actor BeginPlay hasn't run yet.
			Data.GravityDirection = -Checkpoint->GetActorUpVector().GetSafeNormal();
			if (Data.GravityDirection.IsNearlyZero())
			{
				Data.GravityDirection = FVector::DownVector;
			}
			CheckpointMap.Add(Checkpoint->GetCheckpointIndex(), Data);
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

	if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		State.SpawnGravityDirection = Move->GetGravityDirection().GetSafeNormal();
	}
	else
	{
		State.SpawnGravityDirection = FVector::DownVector;
	}
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

FCheckpointRespawnData UCheckpointManager::GetRespawnData(AActionDemoCharacter* Character) const
{
	const FCharacterCheckpointState* State = CharacterStates.Find(Character);
	const int32 Index = State ? State->CheckpointIndex : 0;

	if (const FCheckpointRespawnData* CheckpointData = CheckpointMap.Find(Index))
	{
		return *CheckpointData;
	}

	FCheckpointRespawnData Fallback;
	if (State)
	{
		Fallback.Transform = State->SpawnTransform;
		Fallback.GravityDirection = State->SpawnGravityDirection;
		return Fallback;
	}

	Fallback.Transform = Character ? Character->GetActorTransform() : FTransform::Identity;
	Fallback.GravityDirection = FVector::DownVector;
	return Fallback;
}

void UCheckpointManager::RespawnCharacter(AActionDemoCharacter* Character)
{
	if (!IsAuthorityWorld() || !Character)
	{
		return;
	}

	Character->PushGrappleCableVisual(false, Character->GetActorLocation());

	if (UDemoMyCharacterMovementComponent* Move = Cast<UDemoMyCharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		Move->SetMovementMode(MOVE_Falling);
		Move->StopMovementImmediately();
		Move->ClearHitSpeedPenalty();
	}

	const FCheckpointRespawnData RespawnData = GetRespawnData(Character);
	const FTransform& RespawnTransform = RespawnData.Transform;
	const FVector GravityDirection = RespawnData.GravityDirection.GetSafeNormal();
	const FRotator ControlRotation = RespawnTransform.GetRotation().Rotator();

	Character->SetActorLocationAndRotation(
		RespawnTransform.GetLocation(),
		RespawnTransform.GetRotation(),
		false,
		nullptr,
		ETeleportType::ResetPhysics);

	if (UDemoMyCharacterMovementComponent* Move = Cast<UDemoMyCharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		Move->SetGravityDirection(GravityDirection.IsNearlyZero() ? FVector::DownVector : GravityDirection);
		Move->bJustTeleported = true;
	}

	if (AController* CharacterController = Character->GetController())
	{
		CharacterController->SetControlRotation(ControlRotation);
	}

	// Gravity is not replicated with CMC; sync owning client explicitly.
	Character->ClientApplyRespawnOrientation(
		GravityDirection.IsNearlyZero() ? FVector::DownVector : GravityDirection,
		ControlRotation);

	Character->RestoreAfterRespawn();
}
