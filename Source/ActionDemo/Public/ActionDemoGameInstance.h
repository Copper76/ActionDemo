// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Net/Core/Connection/NetEnums.h"
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

	/** Use LAN discovery instead of the configured internet service. Leave off for Steam rooms. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
	bool bUseLAN = true;

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

	/** Select LAN/PIE testing (Null + IP driver) or internet play (Steam). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool SetUseLAN(bool bEnableLAN);

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsUsingLAN() const { return bUseLAN; }

	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool SelectTravelMap(const FString& MapPackageName);

	UFUNCTION(BlueprintPure, Category = "Menu")
	TArray<FString> GetAvailableMapPackageNames() const;

	UFUNCTION(BlueprintPure, Category = "Menu")
	FString GetSelectedMapPackageName() const { return GetTravelMapAssetName(); }

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ReturnToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void DestroyCurrentSession();

protected:
	virtual void Init() override;
	virtual void Shutdown() override;

private:
	void OnPostLoadMap(UWorld* LoadedWorld);
	void OnNetworkFailure(UWorld* FailedWorld, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void TryShowMainMenu(UWorld* World);
	void BroadcastMessage(const FString& Message);
	FString GenerateRoomCode() const;
	void EnsureRoomCode();
	void ApplyNetworkDriver() const;
	FString GetTravelMapAssetName() const;
	IOnlineSessionPtr GetSessionInterface() const;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	FString CurrentRoomCode;
	FString PendingJoinCode;
	bool bCreateAfterDestroy = false;
	bool bReturnToMenuAfterDestroy = false;

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
	FDelegateHandle NetworkFailureHandle;

	UPROPERTY()
	TObjectPtr<class UMainMenuWidget> FallbackMenuWidget;
};
