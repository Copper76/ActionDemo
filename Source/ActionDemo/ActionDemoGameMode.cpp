// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionDemoGameMode.h"
#include "ActionDemoCharacter.h"
#include "UObject/ConstructorHelpers.h"

AActionDemoGameMode::AActionDemoGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
