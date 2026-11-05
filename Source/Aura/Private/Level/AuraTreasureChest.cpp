// Copyright yosep

#include "Level/AuraTreasureChest.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/AuraCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "Engine/StaticMesh.h"
#include "Game/AuraRunSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/AuraLootSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraTreasure, Log, All);

AAuraTreasureChest::AAuraTreasureChest()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ChestBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBase"));
	ChestBase->SetupAttachment(SceneRoot);
	ChestBase->SetCollisionObjectType(ECC_WorldStatic);
	ChestBase->SetCollisionResponseToAllChannels(ECR_Block);

	LidPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LidPivot"));
	LidPivot->SetupAttachment(SceneRoot);

	ChestLid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestLid"));
	ChestLid->SetupAttachment(LidPivot);
	ChestLid->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ChestBandLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBandLeft"));
	ChestBandLeft->SetupAttachment(SceneRoot);
	ChestBandLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ChestBandRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBandRight"));
	ChestBandRight->SetupAttachment(SceneRoot);
	ChestBandRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ChestLock = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestLock"));
	ChestLock->SetupAttachment(SceneRoot);
	ChestLock->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RewardBeacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RewardBeacon"));
	RewardBeacon->SetupAttachment(SceneRoot);
	RewardBeacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RewardTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("RewardTrigger"));
	RewardTrigger->SetupAttachment(SceneRoot);
	RewardTrigger->SetRelativeLocation(FVector(0.f, 0.f, 65.f));
	RewardTrigger->InitSphereRadius(210.f);
	RewardTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RewardTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	RewardTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RewardTrigger->SetGenerateOverlapEvents(true);
	RewardTrigger->OnComponentBeginOverlap.AddDynamic(
		this, &AAuraTreasureChest::OnRewardTriggerOverlap);

	RewardLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RewardLight"));
	RewardLight->SetupAttachment(SceneRoot);
	RewardLight->SetRelativeLocation(FVector(0.f, 0.f, 85.f));
	RewardLight->SetLightColor(FLinearColor(1.f, 0.42f, 0.08f));
	RewardLight->SetIntensity(520.f);
	RewardLight->SetAttenuationRadius(280.f);
	RewardLight->SetCastShadows(false);

	RewardLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RewardLabel"));
	AuraWorldTextFont::Apply(RewardLabel);
	RewardLabel->SetupAttachment(SceneRoot);
	RewardLabel->SetRelativeLocation(FVector(0.f, 0.f, 112.f));
	RewardLabel->SetHorizontalAlignment(EHTA_Center);
	RewardLabel->SetVerticalAlignment(EVRTA_TextCenter);
	RewardLabel->SetWorldSize(18.f);
	RewardLabel->SetTextRenderColor(FColor(255, 180, 45));
	RewardLabel->SetTranslucentSortPriority(10);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChestMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (ChestMeshAsset.Succeeded())
	{
		ChestBase->SetStaticMesh(ChestMeshAsset.Object);
		ChestLid->SetStaticMesh(ChestMeshAsset.Object);
		ChestBandLeft->SetStaticMesh(ChestMeshAsset.Object);
		ChestBandRight->SetStaticMesh(ChestMeshAsset.Object);
		ChestLock->SetStaticMesh(ChestMeshAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChestMaterialAsset(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ChestMaterialAsset.Succeeded())
	{
		ChestBase->SetMaterial(0, ChestMaterialAsset.Object);
		ChestLid->SetMaterial(0, ChestMaterialAsset.Object);
		ChestBandLeft->SetMaterial(0, ChestMaterialAsset.Object);
		ChestBandRight->SetMaterial(0, ChestMaterialAsset.Object);
		ChestLock->SetMaterial(0, ChestMaterialAsset.Object);
	}
	// Reuse the dungeon kit's weathered stone and rail materials so the chest
	// reads as a heavy crypt coffer instead of a brightly coloured debug box.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DungeonBodyMaterialAsset(
		TEXT("/Game/Assets/Dungeon/Materials/TilesetCorner_splotches/M_TilesetCorner_wall_dark.M_TilesetCorner_wall_dark"));
	if (DungeonBodyMaterialAsset.Succeeded())
	{
		ChestBase->SetMaterial(0, DungeonBodyMaterialAsset.Object);
		ChestLid->SetMaterial(0, DungeonBodyMaterialAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DungeonIronMaterialAsset(
		TEXT("/Game/Assets/Dungeon/Materials/TilesetEdge/M_Edge_rail.M_Edge_rail"));
	if (DungeonIronMaterialAsset.Succeeded())
	{
		ChestBandLeft->SetMaterial(0, DungeonIronMaterialAsset.Object);
		ChestBandRight->SetMaterial(0, DungeonIronMaterialAsset.Object);
		ChestLock->SetMaterial(0, DungeonIronMaterialAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeaconAsset(
		TEXT("/Game/Assets/Dungeon/Beacon/SM_Beacon.SM_Beacon"));
	if (BeaconAsset.Succeeded()) RewardBeacon->SetStaticMesh(BeaconAsset.Object);

	RefreshGeometry();
}

void AAuraTreasureChest::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (GetNetMode() == NM_Standalone)
		{
			if (const UAuraRunSubsystem* RunSubsystem =
				GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
				RunSubsystem && RunSubsystem->IsTreasureClaimed(GetTreasureId()))
			{
				bOpened = true;
			}
		}
		if (bOpened)
		{
			bGuardLocked = false;
			RemainingGuardCount = 0;
		}
	}
	CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	RefreshGeometry();
	RefreshOpenState(true);
	UE_LOG(LogAuraTreasure, Display,
		TEXT("Treasure cache ready. Name=%s Opened=%s GuardLocked=%s Remaining=%d"),
		*GetName(), bOpened ? TEXT("true") : TEXT("false"),
		bGuardLocked ? TEXT("true") : TEXT("false"), RemainingGuardCount);
}

void AAuraTreasureChest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	RefreshGeometry();
	RefreshOpenState(true);
}

void AAuraTreasureChest::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimationTime += DeltaSeconds;
	CurrentOpenAlpha = FMath::FInterpTo(
		CurrentOpenAlpha, bOpened ? 1.f : 0.f, DeltaSeconds, 5.5f);
	RefreshOpenState(false);

	if (RewardLabel)
	{
		if (const APlayerCameraManager* CameraManager =
			UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			RewardLabel->SetWorldRotation(
				(CameraManager->GetCameraLocation() - RewardLabel->GetComponentLocation()).Rotation());
		}
	}
}

void AAuraTreasureChest::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraTreasureChest, bOpened);
	DOREPLIFETIME(AAuraTreasureChest, bGuardLocked);
	DOREPLIFETIME(AAuraTreasureChest, RemainingGuardCount);
}

void AAuraTreasureChest::OnRewardTriggerOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority()) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(OtherActor);
	if (!AuraCharacter) return;
	const AAuraPlayerState* AuraPlayerState =
		AuraCharacter->GetPlayerState<AAuraPlayerState>();
	if ((AuraPlayerState && AuraPlayerState->HasClaimedTreasure(GetTreasureId()))
		|| (GetNetMode() == NM_Standalone && bOpened))
	{
		return;
	}
	if (bGuardLocked)
	{
		if (AAuraPlayerController* AuraController =
			Cast<AAuraPlayerController>(AuraCharacter->GetController()))
		{
			AuraController->ShowCoopMessage(
				RemainingGuardCount > 0
					? FString::Printf(TEXT("宝箱尚未解锁 · 击败 %d 名守卫"),
						RemainingGuardCount)
					: TEXT("宝箱尚未解锁 · 击败守卫"),
				FLinearColor(1.f, 0.18f, 0.08f), 1.6f);
		}
		UE_LOG(LogAuraTreasure, Display,
			TEXT("Guarded treasure interaction blocked. Name=%s Remaining=%d"),
			*GetName(), RemainingGuardCount);
		return;
	}
	if (AAuraPlayerController* AuraController =
		Cast<AAuraPlayerController>(AuraCharacter->GetController()))
	{
		AuraController->ShowCoopMessage(TEXT("按 F 打开奖励宝箱"),
			FLinearColor(1.f, 0.68f, 0.12f), 1.8f, TEXT("奖励宝箱已解锁"));
	}
}

void AAuraTreasureChest::SetGuardLocked(const bool bLocked, const int32 RemainingGuards)
{
	if (!HasAuthority()) return;
	const bool bWasLocked = bGuardLocked;
	bGuardLocked = bLocked && !bOpened;
	RemainingGuardCount = bGuardLocked ? FMath::Max(0, RemainingGuards) : 0;
	RefreshOpenState(false);
	ForceNetUpdate();

	if (bWasLocked != bGuardLocked || bGuardLocked)
	{
		UE_LOG(LogAuraTreasure, Display,
			TEXT("Treasure guard state changed. Name=%s Locked=%s Remaining=%d"),
			*GetName(), bGuardLocked ? TEXT("true") : TEXT("false"), RemainingGuardCount);
	}

	// Unlocking never auto-claims the reward. A chest should feel deliberately
	// opened, not like a floor switch the player accidentally stepped on.
	if (bWasLocked && !bGuardLocked && !bOpened && RewardTrigger)
	{
		TArray<AActor*> OverlappingActors;
		RewardTrigger->GetOverlappingActors(OverlappingActors, AAuraCharacter::StaticClass());
		for (AActor* OverlappingActor : OverlappingActors)
		{
			if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(OverlappingActor))
			{
				if (AAuraPlayerController* AuraController =
					Cast<AAuraPlayerController>(AuraCharacter->GetController()))
				{
					AuraController->ShowCoopMessage(TEXT("按 F 打开奖励宝箱"),
						FLinearColor(1.f, 0.68f, 0.12f), 2.f,
						TEXT("奖励宝箱已解锁"));
				}
			}
		}
	}
}

bool AAuraTreasureChest::ClaimForCharacter(AAuraCharacter* AuraCharacter)
{
	if (!HasAuthority() || !AuraCharacter || AuraCharacter->IsDead()) return false;
	constexpr float MaximumClaimDistance = 320.f;
	if (FVector::DistSquared(AuraCharacter->GetActorLocation(), GetActorLocation())
		> FMath::Square(MaximumClaimDistance))
	{
		UE_LOG(LogAuraTreasure, Warning,
			TEXT("Reward chest claim rejected by distance. Chest=%s Character=%s Distance=%.1f"),
			*GetName(), *GetNameSafe(AuraCharacter),
			FVector::Dist(AuraCharacter->GetActorLocation(), GetActorLocation()));
		return false;
	}
	if (bGuardLocked)
	{
		if (AAuraPlayerController* AuraController =
			Cast<AAuraPlayerController>(AuraCharacter->GetController()))
		{
			AuraController->ShowCoopMessage(
				RemainingGuardCount > 0
					? FString::Printf(TEXT("宝箱封印中 · 击败 %d 名守卫"),
						RemainingGuardCount)
					: TEXT("宝箱尚未解锁"),
				FLinearColor(1.f, 0.18f, 0.08f), 1.6f);
		}
		return false;
	}
	return OpenChest(AuraCharacter);
}

bool AAuraTreasureChest::OpenChest(AAuraCharacter* AuraCharacter)
{
	if (bGuardLocked || !AuraCharacter || AuraCharacter->IsDead()) return false;
	AAuraPlayerState* AuraPlayerState = AuraCharacter->GetPlayerState<AAuraPlayerState>();
	if (!AuraPlayerState || AuraPlayerState->HasClaimedTreasure(GetTreasureId())) return false;
	const int32 MetalReward = AAuraPlayerState::GetChestMetalReward(GetTreasureId());
	if (!AuraPlayerState->CanAcceptChestMetal(MetalReward))
	{
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(AuraCharacter->GetController()))
			Controller->ShowCoopMessage(TEXT("背包已满，腾出位置后再开宝箱"),
				FLinearColor(1.f, 0.3f, 0.1f), 1.8f);
		return false;
	}
	UAuraLootSubsystem* LootSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UAuraLootSubsystem>() : nullptr;
	if (!LootSubsystem) return false;
	const int32 Spawned = LootSubsystem->SpawnPersonalLootForPlayer(
		*AuraPlayerState, EAuraLootProfileType::Treasure,
		GetActorLocation() + GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 40.f),
		EAuraItemSourceType::Treasure, GetTreasureId());
	if (Spawned <= 0)
	{
		if (AAuraPlayerController* Controller =
			Cast<AAuraPlayerController>(AuraCharacter->GetController()))
		{
			Controller->ShowCoopMessage(TEXT("个人掉落物已达上限"),
				FLinearColor(1.f, 0.22f, 0.08f), 1.8f, TEXT("请先拾取现有物品"));
		}
		return false;
	}
	if (!AuraPlayerState->GrantChestMetal(MetalReward, GetTreasureId())) return false;

	bOpened = true;
	bGuardLocked = false;
	RemainingGuardCount = 0;
	AuraPlayerState->MarkTreasureClaimed(GetTreasureId());
	if (GetNetMode() == NM_Standalone)
	{
		if (UAuraRunSubsystem* RunSubsystem =
			GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			RunSubsystem->MarkTreasureClaimed(GetTreasureId());
		}
	}
	RefreshOpenState(false);
	ForceNetUpdate();
	OnTreasureClaimed.Broadcast(this, AuraPlayerState);

	UE_LOG(LogAuraTreasure, Display,
		TEXT("Treasure cache opened. Name=%s Id=%s Player=%s ItemDrops=%d UpgradeChoice=false"),
		*GetName(), *GetTreasureId().ToString(), *GetNameSafe(AuraPlayerState), Spawned);
	return true;
}

void AAuraTreasureChest::OnRep_Opened()
{
	RefreshOpenState(false);
}

void AAuraTreasureChest::OnRep_GuardState()
{
	RefreshOpenState(false);
}

void AAuraTreasureChest::RefreshGeometry()
{
	if (!ChestBase || !ChestLid || !LidPivot || !ChestBandLeft
		|| !ChestBandRight || !ChestLock || !RewardBeacon)
	{
		return;
	}

	// A squat stone coffer with oversized iron braces fits the dungeon kit and
	// keeps a recognisable chest silhouette even from the overview camera.
	const FVector BaseScale(1.55f, 0.85f, 0.50f);
	const FVector LidScale(1.64f, 0.93f, 0.19f);
	ChestBase->SetRelativeScale3D(BaseScale);
	ChestLid->SetRelativeScale3D(LidScale);
	ChestBase->SetRelativeLocation(FVector(0.f, 0.f, 25.f));
	LidPivot->SetRelativeLocation(FVector(0.f, -46.5f, 50.f));
	ChestLid->SetRelativeLocation(FVector(0.f, 46.5f, 9.5f));

	ChestBandLeft->SetRelativeScale3D(FVector(0.12f, 0.89f, 0.52f));
	ChestBandLeft->SetRelativeLocation(FVector(-48.f, 0.f, 26.f));
	ChestBandRight->SetRelativeScale3D(FVector(0.12f, 0.89f, 0.52f));
	ChestBandRight->SetRelativeLocation(FVector(48.f, 0.f, 26.f));
	ChestLock->SetRelativeScale3D(FVector(0.32f, 0.12f, 0.36f));
	ChestLock->SetRelativeLocation(FVector(0.f, 44.5f, 37.f));
	RewardBeacon->SetVisibility(false, true);
}

void AAuraTreasureChest::RefreshOpenState(const bool bSnapAnimation)
{
	if (bSnapAnimation) CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	if (LidPivot)
	{
		LidPivot->SetRelativeRotation(FRotator(0.f, 0.f, 105.f * CurrentOpenAlpha));
	}
	if (RewardTrigger)
	{
		// In co-op the lid is shared, but every PlayerState may claim this cache once.
		RewardTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	if (RewardBeacon) RewardBeacon->SetVisibility(false, true);
	if (RewardLight)
	{
		RewardLight->SetLightColor(bGuardLocked
			? FLinearColor(1.f, 0.035f, 0.015f)
			: FLinearColor(1.f, 0.42f, 0.08f));
		const float ClosedIntensity = bGuardLocked ? 380.f : 620.f;
		RewardLight->SetIntensity(FMath::Lerp(ClosedIntensity, 110.f, CurrentOpenAlpha));
	}
	if (RewardLabel)
	{
		const FString Label = bOpened
			? (GetNetMode() == NM_Standalone ? TEXT("宝箱已开启") : TEXT("队伍奖励宝箱"))
			: (bGuardLocked
				? (RemainingGuardCount > 0
					? FString::Printf(TEXT("封印宝箱 · 剩余 %d 名守卫"), RemainingGuardCount)
					: TEXT("封印宝箱"))
				: TEXT("[F] 打开奖励宝箱"));
		RewardLabel->SetText(FText::FromString(Label));
		RewardLabel->SetTextRenderColor(bOpened
			? FColor(135, 135, 135)
			: (bGuardLocked ? FColor(255, 55, 35) : FColor(255, 180, 45)));
	}
}
