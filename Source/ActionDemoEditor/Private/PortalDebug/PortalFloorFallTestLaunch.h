// Shared launch options for Portal.FloorFallTest. Lives only under PortalDebug.

#pragma once

#include "CoreMinimal.h"

struct FPortalFloorFallTestLaunch
{
	static constexpr TCHAR LevelPath[] = TEXT("/Game/PortalDebug/L_PortalPhysicsProbe");
	static constexpr TCHAR FallbackLevelPath[] = TEXT("/Game/StarterContent/Maps/Minimal_Default");
	static constexpr TCHAR GameModePath[] = TEXT("/Script/ActionDemo.PortalFloorFallTestGameMode");
	static constexpr TCHAR ReturnLevelPath[] = TEXT("/Game/FirstPerson/Maps/FirstPersonMap");

	static bool bRequested;
	static bool bAutoQuit;
	/** Extra cm above floor-portal plane before drop (0 = stand in aperture). */
	static float DropHeightCm;
	static float TimeLimitSec;
};
