#include "PortalPhysicsProbeGameMode.h"

#include "PortalPhysicsProbe.h"
#include "PortalPhysicsProbeCamera.h"
#include "PortalPhysicsProbePlayerController.h"
#include "Engine/World.h"

APortalPhysicsProbeGameMode::APortalPhysicsProbeGameMode()
{
	DefaultPawnClass = APortalPhysicsProbeCamera::StaticClass();
	PlayerControllerClass = APortalPhysicsProbePlayerController::StaticClass();
	SpectatorClass = APortalPhysicsProbeCamera::StaticClass();
	bStartPlayersAsSpectators = false;
}

void APortalPhysicsProbeGameMode::StartPlay()
{
	UWorld* World = GetWorld();
	if (World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Probe = World->SpawnActor<APortalPhysicsProbe>(Params);
	}

	Super::StartPlay();

	// Camera stays at the level PlayerStart / editor view — do not reframe.
	if (World && Probe)
	{
		if (APortalPhysicsProbePlayerController* PC = Cast<APortalPhysicsProbePlayerController>(World->GetFirstPlayerController()))
		{
			PC->SetProbe(Probe);
			Probe->SetOwningController(PC);
		}
	}
}
