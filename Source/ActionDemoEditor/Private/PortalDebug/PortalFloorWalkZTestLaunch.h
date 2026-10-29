// Launch options for Portal.FloorWalkZTest.

#pragma once

#include "CoreMinimal.h"

struct FPortalFloorWalkZTestLaunch
{
	static constexpr TCHAR LevelPath[] = TEXT("/Game/PortalDebug/L_PortalPhysicsProbe");
	static constexpr TCHAR FallbackLevelPath[] = TEXT("/Game/StarterContent/Maps/Minimal_Default");
	static constexpr TCHAR GameModePath[] = TEXT("/Script/ActionDemo.PortalFloorWalkZTestGameMode");
	static constexpr TCHAR ReturnLevelPath[] = TEXT("/Game/FirstPerson/Maps/FirstPersonMap");

	static bool bRequested;
	static bool bAutoQuit;
	static float TimeLimitSec;
	/** Fail if capsule Z rises more than this above the start baseline (cm). */
	static float MaxStepCm;
};
