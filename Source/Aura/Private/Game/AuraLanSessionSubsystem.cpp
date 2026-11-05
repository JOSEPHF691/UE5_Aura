// Copyright yosep

#include "Game/AuraLanSessionSubsystem.h"

#include "Game/AuraRunSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraLan, Log, All);

namespace
{
	const FName AuraSessionKey(TEXT("AURA_RPG_DEMO"));
}

void UAuraLanSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IOnlineSessionPtr Session = GetSessionInterface())
	{
		CreateHandle = Session->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(
				this, &UAuraLanSessionSubsystem::OnCreateCompleted));
		FindHandle = Session->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(
				this, &UAuraLanSessionSubsystem::OnFindCompleted));
		JoinHandle = Session->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(
				this, &UAuraLanSessionSubsystem::OnJoinCompleted));
	}
	else Status = TEXT("局域网服务不可用");
}

void UAuraLanSessionSubsystem::Deinitialize()
{
	if (IOnlineSessionPtr Session = GetSessionInterface())
	{
		Session->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Session->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Session->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}
	Search.Reset();
	FoundRooms.Reset();
	Super::Deinitialize();
}

IOnlineSessionPtr UAuraLanSessionSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* Online = IOnlineSubsystem::Get(FName(TEXT("NULL")));
	return Online ? Online->GetSessionInterface() : nullptr;
}

bool UAuraLanSessionSubsystem::HostRoom()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Session = GetSessionInterface();
	if (bBusy || !World || World->GetNetMode() != NM_Standalone || !Session)
	{
		Status = TEXT("只能在独立游戏中创建房间");
		return false;
	}
	if (Session->GetNamedSession(NAME_GameSession))
	{
		Status = TEXT("当前已有活动房间");
		return false;
	}
	if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
	{
		Run->FlushPersistentSave();
	}
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.NumPublicConnections = 2;
	Settings.Set(AuraSessionKey, true, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	bBusy = true;
	Status = TEXT("正在创建局域网房间……");
	if (!Session->CreateSession(0, NAME_GameSession, Settings))
	{
		bBusy = false;
		Status = TEXT("无法创建局域网房间");
		return false;
	}
	return true;
}

bool UAuraLanSessionSubsystem::SearchRooms()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Session = GetSessionInterface();
	if (bBusy || !World || World->GetNetMode() != NM_Standalone || !Session)
	{
		Status = TEXT("只能在独立游戏中搜索房间");
		return false;
	}
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 20;
	FoundRooms.Reset();
	RoomLabels.Reset();
	bBusy = true;
	Status = TEXT("正在搜索局域网房间……");
	if (!Session->FindSessions(0, Search.ToSharedRef()))
	{
		bBusy = false;
		Status = TEXT("局域网搜索失败");
		return false;
	}
	return true;
}

bool UAuraLanSessionSubsystem::JoinRoom(const int32 Index)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Session = GetSessionInterface();
	if (bBusy || !World || World->GetNetMode() != NM_Standalone || !Session
		|| !FoundRooms.IsValidIndex(Index))
	{
		Status = TEXT("请选择可用的局域网房间");
		return false;
	}
	bBusy = true;
	Status = TEXT("正在加入局域网房间……");
	if (!Session->JoinSession(0, NAME_GameSession, FoundRooms[Index]))
	{
		bBusy = false;
		Status = TEXT("无法加入局域网房间");
		return false;
	}
	return true;
}

void UAuraLanSessionSubsystem::OnCreateCompleted(
	const FName SessionName, const bool bSuccess)
{
	if (SessionName != NAME_GameSession) return;
	bBusy = false;
	Status = bSuccess ? TEXT("房间已创建，正在加载地牢……")
		: TEXT("局域网房间创建失败");
	UE_LOG(LogAuraLan, Display, TEXT("M15 LAN host created. Success=%s"),
		bSuccess ? TEXT("true") : TEXT("false"));
	if (bSuccess && GetGameInstance() && GetGameInstance()->GetWorld())
	{
		UGameplayStatics::OpenLevel(GetGameInstance()->GetWorld(),
			FName(TEXT("/Game/Map/DungeonDemo")), true, TEXT("listen"));
	}
}

void UAuraLanSessionSubsystem::OnFindCompleted(const bool bSuccess)
{
	bBusy = false;
	FoundRooms.Reset();
	RoomLabels.Reset();
	if (bSuccess && Search.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			bool bAuraGame = false;
			Result.Session.SessionSettings.Get(AuraSessionKey, bAuraGame);
			if (!bAuraGame || !Result.IsValid()) continue;
			FoundRooms.Add(Result);
			RoomLabels.Add(FString::Printf(TEXT("%s  |  %d/2 人  |  %d 毫秒"),
				*Result.Session.OwningUserName,
				2 - Result.Session.NumOpenPublicConnections,
				Result.PingInMs));
		}
	}
	Status = bSuccess
		? FString::Printf(TEXT("找到 %d 个局域网房间"), FoundRooms.Num())
		: TEXT("局域网搜索失败");
	UE_LOG(LogAuraLan, Display, TEXT("M15 LAN search. Success=%s Rooms=%d"),
		bSuccess ? TEXT("true") : TEXT("false"), FoundRooms.Num());
}

void UAuraLanSessionSubsystem::OnJoinCompleted(
	const FName SessionName, const EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != NAME_GameSession) return;
	bBusy = false;
	FString URL;
	const bool bResolved = Result == EOnJoinSessionCompleteResult::Success
		&& GetSessionInterface().IsValid()
		&& GetSessionInterface()->GetResolvedConnectString(NAME_GameSession, URL);
	if (!bResolved)
	{
		Status = TEXT("无法解析房主地址");
		UE_LOG(LogAuraLan, Error, TEXT("M15 LAN join failed. Result=%d"),
			static_cast<int32>(Result));
		return;
	}
	Status = TEXT("正在连接房主……");
	UE_LOG(LogAuraLan, Display, TEXT("M15 LAN joining. URL=%s"), *URL);
	if (APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController())
	{
		Controller->ClientTravel(URL, TRAVEL_Absolute);
	}
}
