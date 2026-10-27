// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class ACTIONDEMO_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildLayout();
	void SetStatus(const FString& Text);

	UFUNCTION()
	void OnPlaySinglePlayerClicked();

	UFUNCTION()
	void OnCreateRemoteClicked();

	UFUNCTION()
	void OnJoinRemoteClicked();

	UFUNCTION()
	void HandleSessionMessage(const FString& Message);

	UFUNCTION()
	void HandleRoomCodeReady(bool bSuccess, const FString& RoomCode);

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> RoomCodeText;

	UPROPERTY()
	TObjectPtr<UButton> PlaySingleButton;

	UPROPERTY()
	TObjectPtr<UButton> CreateRemoteButton;

	UPROPERTY()
	TObjectPtr<UButton> JoinRemoteButton;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> RoomCodeInput;
};
