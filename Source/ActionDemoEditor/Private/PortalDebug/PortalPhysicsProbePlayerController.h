#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PortalPhysicsProbePlayerController.generated.h"

class APortalPhysicsProbe;

UCLASS(NotPlaceable, Transient)
class APortalPhysicsProbePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APortalPhysicsProbePlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void SetProbe(APortalPhysicsProbe* InProbe);

	void RequestSingleFrameStep();
	bool IsSingleFrameStepPending() const { return bSingleFrameStep; }
	bool TryCompleteSingleFrameStep();

	void RefreshHelpMessage() const;

private:
	void RestartSimulation();
	void TogglePauseSim();
	void StepOneFrame();
	void RunPresetN(int32 Index1Based);
	void RunAllPresets();

	UPROPERTY()
	TObjectPtr<APortalPhysicsProbe> Probe = nullptr;

	bool bSingleFrameStep = false;
	uint64 SingleStepRequestFrame = 0;
};
