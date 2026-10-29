// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ActionDemoGameMode.generated.h"

UCLASS(minimalapi)
class AActionDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AActionDemoGameMode();

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

private:
	TMap<TWeakObjectPtr<AController>, int32> PlayerSpawnIndices;
	int32 NextSpawnIndex = 1;
};
