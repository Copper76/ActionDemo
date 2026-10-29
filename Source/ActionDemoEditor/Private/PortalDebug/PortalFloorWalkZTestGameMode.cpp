#include "PortalFloorWalkZTestGameMode.h"

#include "PortalFloorWalkZTest.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

APortalFloorWalkZTestGameMode::APortalFloorWalkZTestGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> CharacterBP(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	if (CharacterBP.Succeeded())
	{
		DefaultPawnClass = CharacterBP.Class;
	}
	else
	{
		DefaultPawnClass = AActionDemoCharacter::StaticClass();
	}
	PlayerControllerClass = APlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

APawn* APortalFloorWalkZTestGameMode::SpawnDefaultPawnAtTransform_Implementation(
	AController* NewPlayer, const FTransform& SpawnTransform)
{
	FTransform Parked = SpawnTransform;
	Parked.SetLocation(FVector(-5000.f, -5000.f, 500.f));
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Parked);
}

void APortalFloorWalkZTestGameMode::StartPlay()
{
	Super::StartPlay();

	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Test = World->SpawnActor<APortalFloorWalkZTest>(Params);
	}
}
