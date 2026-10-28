// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "ActionDemoGameInstance.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionMessage, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoomCodeReady, bool, bSuccess, const FString&, RoomCode);

UCLASS()
class ACTIONDEMO_API UActionDemoGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UActionDemoGameInstance();

	/** Map opened for single-player and multiplayer hosts (soft path, no leading /Game required in BP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
	FSoftObjectPath TravelMap;

	/** Max players for a hosted remote session (including host). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu", meta = (ClampMin = "2", ClampMax = "16"))
	int32 MaxSessionPlayers = 4;

	/** LAN sessions (OnlineSubsystemNull). Turn off when using Steam/EOS for internet play. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
	bool bUseLAN = false;

	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnSessionMessage OnSessionMessage;

	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnRoomCodeReady OnRoomCodeReady;

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void PlaySinglePlayer();

	/** Host a listen-server session and travel to TravelMap. Generates a short room code. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void CreateRemoteSession();

	/** Find a LAN/online session whose ROOMCODE setting matches, then join it. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void JoinRemoteSession(const FString& RoomCode);

	UFUNCTION(BlueprintPure, Category = "Menu")
	FString GetCurrentRoomCode() const { return CurrentRoomCode; }

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void DestroyCurrentSession();

protected:
	virtual void Init() override;
	virtual void Shutdown() override;

private:
	void OnPostLoadMap(UWorld* LoadedWorld);
	void TryShowMainMenu(UWorld* World);
	void BroadcastMessage(const FString& Message);
	FString GenerateRoomCode() const;
	FString GetTravelMapAssetName() const;
	IOnlineSessionPtr GetSessionInterface() const;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	FString CurrentRoomCode;
	FString PendingJoinCode;

	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;
	FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;

	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;
	FDelegateHandle DestroySessionCompleteHandle;
	FDelegateHandle PostLoadMapHandle;

	UPROPERTY()
	TObjectPtr<class UMainMenuWidget> FallbackMenuWidget;
};
