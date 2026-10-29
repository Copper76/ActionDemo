#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PortalPhysicsProbeGameMode.generated.h"

class APortalPhysicsProbe;

UCLASS(NotPlaceable, Transient)
class APortalPhysicsProbeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APortalPhysicsProbeGameMode();

	virtual void StartPlay() override;

private:
	UPROPERTY()
	TObjectPtr<APortalPhysicsProbe> Probe = nullptr;
};
