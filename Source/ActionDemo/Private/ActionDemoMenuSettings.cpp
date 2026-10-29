// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionDemoMenuSettings.h"

UActionDemoMenuSettings::UActionDemoMenuSettings()
{
	DefaultMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/FirstPerson/Maps/Level_1.Level_1")));
	AvailableMaps = {
		TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/FirstPerson/Maps/Level_1.Level_1"))),
		TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/FirstPerson/Maps/FirstPersonMap.FirstPersonMap"))),
		TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/FirstPerson/Maps/Arena.Arena")))
	};
}
