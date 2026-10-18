#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "PortalPhysicsProbeCamera.generated.h"

// Free-look camera only. Spawns at the level PlayerStart and stays there.
// Movement and look continue while the sim is paused.
UCLASS(NotPlaceable, Transient)
class APortalPhysicsProbeCamera : public ADefaultPawn
{
	GENERATED_BODY()

public:
	APortalPhysicsProbeCamera();

	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;
};
