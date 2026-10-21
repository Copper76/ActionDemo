#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PortalFloorWalkZTestGameMode.generated.h"

class APortalFloorWalkZTest;

UCLASS(NotPlaceable, Transient)
class APortalFloorWalkZTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APortalFloorWalkZTestGameMode();

	virtual void StartPlay() override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

private:
	UPROPERTY()
	TObjectPtr<APortalFloorWalkZTest> Test = nullptr;
};
