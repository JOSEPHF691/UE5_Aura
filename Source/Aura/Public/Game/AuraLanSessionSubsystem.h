// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AuraLanSessionSubsystem.generated.h"

/** LAN-only session discovery for the demo; no account identity or remote profile promises. */
UCLASS()
class AURA_API UAuraLanSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool HostRoom();
	bool SearchRooms();
	bool JoinRoom(int32 Index);
	bool IsBusy() const { return bBusy; }
	const FString& GetStatus() const { return Status; }
	const TArray<FString>& GetRoomLabels() const { return RoomLabels; }

private:
	void OnCreateCompleted(FName SessionName, bool bSuccess);
	void OnFindCompleted(bool bSuccess);
	void OnJoinCompleted(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	IOnlineSessionPtr GetSessionInterface() const;

	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FOnlineSessionSearchResult> FoundRooms;
	TArray<FString> RoomLabels;
	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FString Status = TEXT("仅支持局域网房间");
	bool bBusy = false;
};
