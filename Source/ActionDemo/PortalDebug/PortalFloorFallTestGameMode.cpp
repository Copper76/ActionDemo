#include "PortalFloorFallTestGameMode.h"

#include "PortalFloorFallTest.h"
#include "PortalFloorFallTestLaunch.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

APortalFloorFallTestGameMode::APortalFloorFallTestGameMode()
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

APawn* APortalFloorFallTestGameMode::SpawnDefaultPawnAtTransform_Implementation(
	AController* NewPlayer, const FTransform& SpawnTransform)
{
	// Park the default pawn off to the side; the test repositions / may respawn.
	FTransform Parked = SpawnTransform;
	Parked.SetLocation(FVector(-5000.f, -5000.f, 500.f));
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Parked);
}

void APortalFloorFallTestGameMode::StartPlay()
{
	// Spawn default pawn first — FloorFallTest::BeginPlay needs a possessed character.
	Super::StartPlay();

	UWorld* World = GetWorld();
	if (World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Test = World->SpawnActor<APortalFloorFallTest>(Params);
	}
}
