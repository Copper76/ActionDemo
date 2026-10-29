#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PortalFloorFallTestGameMode.generated.h"

class APortalFloorFallTest;

UCLASS(NotPlaceable, Transient)
class APortalFloorFallTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APortalFloorFallTestGameMode();

	virtual void StartPlay() override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

private:
	UPROPERTY()
	TObjectPtr<APortalFloorFallTest> Test = nullptr;
};
