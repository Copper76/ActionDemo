// Shared launch options for Portal.PhysicsProbe. Lives only under PortalDebug.

#pragma once

#include "CoreMinimal.h"

struct FPortalPhysicsProbeLaunch
{
	// Temporary probe level (blank-ish). Game mode is forced via travel URL, not the map defaults.
	static constexpr TCHAR LevelPath[] = TEXT("/Game/PortalDebug/L_PortalPhysicsProbe");
	static constexpr TCHAR FallbackLevelPath[] = TEXT("/Game/StarterContent/Maps/Minimal_Default");
	static constexpr TCHAR GameModePath[] = TEXT("/Script/ActionDemo.PortalPhysicsProbeGameMode");
	static constexpr TCHAR ReturnLevelPath[] = TEXT("/Game/FirstPerson/Maps/FirstPersonMap");

	static int32 DumpFrames;
	static float SinkThresholdCm;
	static bool bRequested;
};
