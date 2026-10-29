// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionDemoGameInstance.h"

#include "ActionDemoMenuSettings.h"
#include "MainMenuGameMode.h"
#include "MainMenuWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/PlayerController.h"

namespace ActionDemoSession
{
	const FName RoomCodeSetting(TEXT("ROOMCODE"));
}

UActionDemoGameInstance::UActionDemoGameInstance()
{
	const UActionDemoMenuSettings* MenuSettings = GetDefault<UActionDemoMenuSettings>();
	TravelMap = MenuSettings && !MenuSettings->DefaultMap.IsNull()
		? MenuSettings->DefaultMap.ToSoftObjectPath()
		: FSoftObjectPath(TEXT("/Game/FirstPerson/Maps/Level_1.Level_1"));
}

void UActionDemoGameInstance::Init()
{
	Super::Init();
	EnsureRoomCode();
	ApplyNetworkDriver();

	CreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &UActionDemoGameInstance::OnCreateSessionComplete);
	FindSessionsCompleteDelegate = FOnFindSessionsCompleteDelegate::CreateUObject(this, &UActionDemoGameInstance::OnFindSessionsComplete);
	JoinSessionCompleteDelegate = FOnJoinSessionCompleteDelegate::CreateUObject(this, &UActionDemoGameInstance::OnJoinSessionComplete);
	DestroySessionCompleteDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(this, &UActionDemoGameInstance::OnDestroySessionComplete);

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UActionDemoGameInstance::OnPostLoadMap);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UActionDemoGameInstance::OnNetworkFailure);
	}
}

void UActionDemoGameInstance::Shutdown()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (GEngine && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}
	DestroyCurrentSession();
	Super::Shutdown();
}

void UActionDemoGameInstance::OnPostLoadMap(UWorld* LoadedWorld)
{
	TryShowMainMenu(LoadedWorld);
}

void UActionDemoGameInstance::OnNetworkFailure(UWorld* FailedWorld, UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (!FailedWorld || FailedWorld != GetWorld())
	{
		return;
	}

	const FString MapName = UWorld::RemovePIEPrefix(FailedWorld->GetMapName());
	if (MapName.Contains(TEXT("Entry")) || MapName.Contains(TEXT("MainMenu")))
	{
		BroadcastMessage(FString::Printf(TEXT("Connection failed: %s"), *ErrorString));
		return;
	}

	BroadcastMessage(TEXT("The host left. Returning to the main menu..."));
	ReturnToMainMenu();
}

void UActionDemoGameInstance::TryShowMainMenu(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const FString MapName = UWorld::RemovePIEPrefix(World->GetMapName());
	const bool bIsMenuMap = MapName.Contains(TEXT("MainMenu")) || MapName.Contains(TEXT("Entry"));
	if (!bIsMenuMap)
	{
		if (FallbackMenuWidget)
		{
			FallbackMenuWidget->RemoveFromParent();
			FallbackMenuWidget = nullptr;
		}
		return;
	}

	// Prefer MainMenuGameMode's widget; only fall back if another mode loaded the map.
	if (World->GetAuthGameMode() && World->GetAuthGameMode()->IsA(AMainMenuGameMode::StaticClass()))
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->IsLocalController() || FallbackMenuWidget)
	{
		return;
	}

	FallbackMenuWidget = CreateWidget<UMainMenuWidget>(PC, UMainMenuWidget::StaticClass());
	if (FallbackMenuWidget)
	{
		FallbackMenuWidget->AddToViewport(0);
		PC->bShowMouseCursor = true;
		FInputModeUIOnly InputMode;
		PC->SetInputMode(InputMode);
	}
}

void UActionDemoGameInstance::BroadcastMessage(const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("Session: %s"), *Message);
	OnSessionMessage.Broadcast(Message);
}

FString UActionDemoGameInstance::GenerateRoomCode() const
{
	static const TCHAR* Alphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	constexpr int32 CodeLength = 6;

	FString Code;
	Code.Reserve(CodeLength);
	for (int32 i = 0; i < CodeLength; ++i)
	{
		const int32 Index = FMath::RandRange(0, 31);
		Code.AppendChar(Alphabet[Index]);
	}
	return Code;
}

void UActionDemoGameInstance::EnsureRoomCode()
{
	if (CurrentRoomCode.IsEmpty())
	{
		CurrentRoomCode = GenerateRoomCode();
	}
}

FString UActionDemoGameInstance::GetTravelMapAssetName() const
{
	if (!TravelMap.IsValid())
	{
		return TEXT("/Game/FirstPerson/Maps/Level_1");
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(TravelMap.GetAssetPathString());
	return PackageName.IsEmpty() ? TEXT("/Game/FirstPerson/Maps/Level_1") : PackageName;
}

TArray<FString> UActionDemoGameInstance::GetAvailableMapPackageNames() const
{
	TArray<FString> Result;
	const UActionDemoMenuSettings* MenuSettings = GetDefault<UActionDemoMenuSettings>();
	if (!MenuSettings)
	{
		return Result;
	}

	for (const TSoftObjectPtr<UWorld>& Map : MenuSettings->AvailableMaps)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(Map.ToSoftObjectPath().GetAssetPathString());
		if (!PackageName.IsEmpty())
		{
			Result.AddUnique(PackageName);
		}
	}
	return Result;
}

bool UActionDemoGameInstance::SelectTravelMap(const FString& MapPackageName)
{
	const UActionDemoMenuSettings* MenuSettings = GetDefault<UActionDemoMenuSettings>();
	if (!MenuSettings)
	{
		return false;
	}

	for (const TSoftObjectPtr<UWorld>& Map : MenuSettings->AvailableMaps)
	{
		const FSoftObjectPath MapPath = Map.ToSoftObjectPath();
		if (FPackageName::ObjectPathToPackageName(MapPath.GetAssetPathString()).Equals(MapPackageName, ESearchCase::IgnoreCase))
		{
			TravelMap = MapPath;
			BroadcastMessage(FString::Printf(TEXT("Map selected: %s"), *FPackageName::GetShortName(MapPackageName)));
			return true;
		}
	}
	BroadcastMessage(TEXT("That map is not available."));
	return false;
}

IOnlineSessionPtr UActionDemoGameInstance::GetSessionInterface() const
{
	const FName ServiceName = bUseLAN ? FName(TEXT("NULL")) : FName(TEXT("STEAM"));
	// The world-aware lookup gives each PIE instance its own Null subsystem,
	// while packaged games still resolve the normal named provider.
	if (const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld(), ServiceName))
	{
		return OnlineSub->GetSessionInterface();
	}
	return nullptr;
}

void UActionDemoGameInstance::ApplyNetworkDriver() const
{
	if (!GEngine)
	{
		return;
	}

	for (FNetDriverDefinition& Definition : GEngine->NetDriverDefinitions)
	{
		if (Definition.DefName == NAME_GameNetDriver)
		{
			Definition.DriverClassName = bUseLAN
				? FName(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"))
				: FName(TEXT("/Script/OnlineSubsystemSteam.SteamNetDriver"));
			Definition.DriverClassNameFallback = FName(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
			break;
		}
	}
}

bool UActionDemoGameInstance::SetUseLAN(bool bEnableLAN)
{
	if (bUseLAN == bEnableLAN)
	{
		return true;
	}

	if (IOnlineSessionPtr CurrentSessions = GetSessionInterface();
		CurrentSessions.IsValid() && CurrentSessions->GetNamedSession(NAME_GameSession))
	{
		BroadcastMessage(TEXT("Leave the current room before changing network mode."));
		return false;
	}

	bUseLAN = bEnableLAN;
	ApplyNetworkDriver();
	BroadcastMessage(bUseLAN
		? TEXT("LAN test mode enabled. Steam is not required.")
		: TEXT("Internet mode enabled. Steam must be running."));
	return true;
}

void UActionDemoGameInstance::PlaySinglePlayer()
{
	DestroyCurrentSession();
	CurrentRoomCode.Reset();
	BroadcastMessage(TEXT("Starting single player..."));
	UGameplayStatics::OpenLevel(this, FName(*GetTravelMapAssetName()));
}

void UActionDemoGameInstance::ReturnToMainMenu()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, false);
	}

	if (IOnlineSessionPtr Sessions = GetSessionInterface();
		Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		bReturnToMenuAfterDestroy = true;
		DestroyCurrentSession();
		return;
	}

	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")));
}

void UActionDemoGameInstance::CreateRemoteSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		BroadcastMessage(TEXT("Online subsystem unavailable."));
		OnRoomCodeReady.Broadcast(false, FString());
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		bCreateAfterDestroy = true;
		BroadcastMessage(TEXT("Closing the previous room..."));
		DestroyCurrentSession();
		return;
	}

	// The code shown on the menu is the code advertised by this room.
	EnsureRoomCode();

	CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	TSharedRef<FOnlineSessionSettings> Settings = MakeShared<FOnlineSessionSettings>();
	Settings->bIsLANMatch = bUseLAN;
	Settings->NumPublicConnections = MaxSessionPlayers;
	Settings->NumPrivateConnections = 0;
	Settings->bAllowJoinInProgress = true;
	Settings->bAllowJoinViaPresence = true;
	Settings->bShouldAdvertise = true;
	Settings->bUsesPresence = !bUseLAN;
	Settings->bUseLobbiesIfAvailable = !bUseLAN;
	Settings->Set(ActionDemoSession::RoomCodeSetting, CurrentRoomCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	BroadcastMessage(FString::Printf(TEXT("Creating room %s..."), *CurrentRoomCode));

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const int32 LocalUserNum = LocalPlayer ? LocalPlayer->GetControllerId() : 0;
	if (!Sessions->CreateSession(LocalUserNum, NAME_GameSession, *Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		BroadcastMessage(TEXT("Failed to start CreateSession."));
		OnRoomCodeReady.Broadcast(false, FString());
	}
}

void UActionDemoGameInstance::JoinRemoteSession(const FString& RoomCode)
{
	const FString Normalized = RoomCode.TrimStartAndEnd().ToUpper();
	if (Normalized.IsEmpty())
	{
		BroadcastMessage(TEXT("Enter a room code."));
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		BroadcastMessage(TEXT("Online subsystem unavailable."));
		return;
	}

	PendingJoinCode = Normalized;
	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = bUseLAN;
	SessionSearch->MaxSearchResults = 100;
	if (!bUseLAN)
	{
		// Online services can apply this on the server (Steam translates it to a
		// lobby filter), avoiding a scan of unrelated public rooms.
		SessionSearch->QuerySettings.Set(ActionDemoSession::RoomCodeSetting, PendingJoinCode, EOnlineComparisonOp::Equals);
		SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	}

	BroadcastMessage(FString::Printf(TEXT("Searching for room %s..."), *PendingJoinCode));

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const int32 LocalUserNum = LocalPlayer ? LocalPlayer->GetControllerId() : 0;
	if (!Sessions->FindSessions(LocalUserNum, SessionSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		BroadcastMessage(TEXT("Failed to start FindSessions."));
		PendingJoinCode.Reset();
	}
}

void UActionDemoGameInstance::DestroyCurrentSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		return;
	}

	DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);
	Sessions->DestroySession(NAME_GameSession);
}

void UActionDemoGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
	}

	if (!bWasSuccessful)
	{
		BroadcastMessage(TEXT("Create session failed."));
		OnRoomCodeReady.Broadcast(false, FString());
		return;
	}

	BroadcastMessage(FString::Printf(TEXT("Room ready: %s"), *CurrentRoomCode));
	OnRoomCodeReady.Broadcast(true, CurrentRoomCode);

	const FString Options = TEXT("listen");
	UGameplayStatics::OpenLevel(this, FName(*GetTravelMapAssetName()), true, Options);
}

void UActionDemoGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
	}

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		BroadcastMessage(TEXT("Session search failed."));
		PendingJoinCode.Reset();
		return;
	}

	const FOnlineSessionSearchResult* Match = nullptr;
	for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
	{
		FString FoundCode;
		if (Result.Session.SessionSettings.Get(ActionDemoSession::RoomCodeSetting, FoundCode)
			&& FoundCode.Equals(PendingJoinCode, ESearchCase::IgnoreCase))
		{
			Match = &Result;
			break;
		}
	}

	if (!Match)
	{
		BroadcastMessage(FString::Printf(TEXT("No room found for code %s."), *PendingJoinCode));
		PendingJoinCode.Reset();
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		PendingJoinCode.Reset();
		return;
	}

	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const int32 LocalUserNum = LocalPlayer ? LocalPlayer->GetControllerId() : 0;
	BroadcastMessage(FString::Printf(TEXT("Joining room %s..."), *PendingJoinCode));
	if (!Sessions->JoinSession(LocalUserNum, NAME_GameSession, *Match))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		BroadcastMessage(TEXT("Failed to start JoinSession."));
		PendingJoinCode.Reset();
	}
}

void UActionDemoGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		BroadcastMessage(TEXT("Join session failed."));
		PendingJoinCode.Reset();
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		PendingJoinCode.Reset();
		return;
	}

	FString ConnectString;
	if (!Sessions->GetResolvedConnectString(SessionName, ConnectString) || ConnectString.IsEmpty())
	{
		BroadcastMessage(TEXT("Could not resolve host address."));
		PendingJoinCode.Reset();
		return;
	}

	CurrentRoomCode = PendingJoinCode;
	PendingJoinCode.Reset();
	BroadcastMessage(FString::Printf(TEXT("Travelling to %s"), *ConnectString));

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(ConnectString, TRAVEL_Absolute);
	}
}

void UActionDemoGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
	}
	CurrentRoomCode.Reset();
	EnsureRoomCode();
	OnRoomCodeReady.Broadcast(true, CurrentRoomCode);

	if (bReturnToMenuAfterDestroy)
	{
		bReturnToMenuAfterDestroy = false;
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")));
		return;
	}

	if (bCreateAfterDestroy)
	{
		bCreateAfterDestroy = false;
		CreateRemoteSession();
	}
}
