// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ActionDemoMenuSettings.generated.h"

class UWorld;

/** Editor-facing configuration for the main menu. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Action Demo Menu"))
class ACTIONDEMO_API UActionDemoMenuSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UActionDemoMenuSettings();

	/** Initially selected map in the main menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	TSoftObjectPtr<UWorld> DefaultMap;

	/** Maps displayed by the main-menu selector. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps", meta = (TitleProperty = "AssetPathName"))
	TArray<TSoftObjectPtr<UWorld>> AvailableMaps;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
