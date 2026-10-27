// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuGameMode.h"

#include "MainMenuPlayerController.h"
#include "MainMenuWidget.h"
#include "Blueprint/UserWidget.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	PlayerControllerClass = AMainMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	MenuWidgetClass = UMainMenuWidget::StaticClass();
}

void AMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!MenuWidgetClass)
	{
		return;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	MenuWidget = CreateWidget<UMainMenuWidget>(PC, MenuWidgetClass);
	if (MenuWidget)
	{
		MenuWidget->AddToViewport(0);
	}
}
