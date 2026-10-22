#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CheckpointManager.generated.h"

class AActionDemoCharacter;

struct FCharacterCheckpointState
{
	int32 CheckpointIndex = 0;
	FTransform SpawnTransform = FTransform::Identity;
};

/**
 * Server/listen/standalone world subsystem: level checkpoints + per-character progress.
 * Clients request respawn via character Server RPC; this only exists on authority worlds.
 */
UCLASS()
class ACTIONDEMO_API UCheckpointManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void RegisterCharacter(AActionDemoCharacter* Character);
	void UnregisterCharacter(AActionDemoCharacter* Character);

	void SetCharacterCheckpoint(AActionDemoCharacter* Character, int32 CheckpointIndex);

	/** Teleport the character to their last reached checkpoint (authority only). */
	void RespawnCharacter(AActionDemoCharacter* Character);

private:
	bool IsAuthorityWorld() const;
	FTransform GetRespawnTransform(AActionDemoCharacter* Character) const;

	/** Level-placed checkpoint index → transform. */
	TMap<int32, FTransform> CheckpointMap;

	TMap<TObjectPtr<AActionDemoCharacter>, FCharacterCheckpointState> CharacterStates;
};
