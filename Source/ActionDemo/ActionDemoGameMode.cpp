// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionDemoGameMode.h"
#include "ActionDemoCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "UObject/ConstructorHelpers.h"

AActionDemoGameMode::AActionDemoGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;
}

void AActionDemoGameMode::PostLogin(APlayerController* NewPlayer)
{
	PlayerSpawnIndices.FindOrAdd(NewPlayer) = NextSpawnIndex++;
	Super::PostLogin(NewPlayer);
}

void AActionDemoGameMode::Logout(AController* Exiting)
{
	PlayerSpawnIndices.Remove(Exiting);
	Super::Logout(Exiting);
}

AActor* AActionDemoGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	const int32* SpawnIndex = PlayerSpawnIndices.Find(Player);
	if (SpawnIndex)
	{
		const FName RequiredTag(*FString::FromInt(*SpawnIndex));
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == RequiredTag)
			{
				return *It;
			}
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

APawn* AActionDemoGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	if (!StartSpot)
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}
	FRotator StartRotation = StartSpot->GetActorRotation();
	FVector StartLocation = StartSpot->GetActorLocation();

	FTransform Transform = FTransform(StartRotation, StartLocation);
	return SpawnDefaultPawnAtTransform(NewPlayer, Transform);
}
