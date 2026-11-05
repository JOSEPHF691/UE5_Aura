// Copyright yosep

#include "Level/AuraLootChest.h"

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
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraLootChest, Log, All);

AAuraLootChest::AAuraLootChest()
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

	LootBeacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootBeacon"));
	LootBeacon->SetupAttachment(SceneRoot);
	LootBeacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RewardTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("RewardTrigger"));
	RewardTrigger->SetupAttachment(SceneRoot);
	RewardTrigger->SetRelativeLocation(FVector(0.f, 0.f, 65.f));
	RewardTrigger->InitSphereRadius(210.f);
	RewardTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RewardTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	RewardTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RewardTrigger->SetGenerateOverlapEvents(true);
	RewardTrigger->OnComponentBeginOverlap.AddDynamic(
		this, &AAuraLootChest::OnRewardTriggerOverlap);

	RewardLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RewardLight"));
	RewardLight->SetupAttachment(SceneRoot);
	RewardLight->SetRelativeLocation(FVector(0.f, 0.f, 85.f));
	RewardLight->SetLightColor(FLinearColor(0.12f, 0.62f, 1.f));
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
	RewardLabel->SetTextRenderColor(FColor(70, 190, 255));
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
	if (BeaconAsset.Succeeded()) LootBeacon->SetStaticMesh(BeaconAsset.Object);

	RefreshGeometry();
}

void AAuraLootChest::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && GetNetMode() == NM_Standalone)
	{
		if (const UAuraRunSubsystem* RunSubsystem =
			GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
			RunSubsystem && RunSubsystem->IsTreasureClaimed(GetLootChestId()))
		{
			bOpened = true;
			bGuardLocked = false;
			RemainingGuardCount = 0;
		}
	}
	CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	RefreshGeometry();
	RefreshChestState(true);
	UE_LOG(LogAuraLootChest, Display,
		TEXT("Loot chest ready. Name=%s Id=%s Opened=%s Locked=%s Remaining=%d Profile=%d"),
		*GetName(), *GetLootChestId().ToString(), bOpened ? TEXT("true") : TEXT("false"),
		bGuardLocked ? TEXT("true") : TEXT("false"), RemainingGuardCount,
		static_cast<int32>(LootProfile));
}

void AAuraLootChest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	RefreshGeometry();
	RefreshChestState(true);
}

void AAuraLootChest::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimationTime += DeltaSeconds;
	CurrentOpenAlpha = FMath::FInterpTo(
		CurrentOpenAlpha, bOpened ? 1.f : 0.f, DeltaSeconds, 5.5f);
	RefreshChestState(false);
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

void AAuraLootChest::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraLootChest, bOpened);
	DOREPLIFETIME(AAuraLootChest, bGuardLocked);
	DOREPLIFETIME(AAuraLootChest, RemainingGuardCount);
}

bool AAuraLootChest::HasBeenClaimedBy(const APlayerState* PlayerState) const
{
	const AAuraPlayerState* AuraPlayerState = Cast<AAuraPlayerState>(PlayerState);
	return AuraPlayerState && AuraPlayerState->HasClaimedTreasure(GetLootChestId());
}

void AAuraLootChest::SetGuardLocked(const bool bLocked, const int32 RemainingGuards)
{
	if (!HasAuthority()) return;
	const bool bWasLocked = bGuardLocked;
	bGuardLocked = bLocked && !bOpened;
	RemainingGuardCount = bGuardLocked ? FMath::Max(0, RemainingGuards) : 0;
	RefreshChestState(false);
	ForceNetUpdate();
	if (bWasLocked != bGuardLocked || bGuardLocked)
	{
		UE_LOG(LogAuraLootChest, Display,
			TEXT("Loot chest guard state changed. Name=%s Locked=%s Remaining=%d"),
			*GetName(), bGuardLocked ? TEXT("true") : TEXT("false"), RemainingGuardCount);
	}

	// Unlocking only exposes the interaction; the player still presses F.
	if (bWasLocked && !bGuardLocked && RewardTrigger)
	{
		TArray<AActor*> OverlappingActors;
		RewardTrigger->GetOverlappingActors(OverlappingActors, AAuraCharacter::StaticClass());
		for (AActor* OverlappingActor : OverlappingActors)
		{
			if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(OverlappingActor))
			{
				if (AAuraPlayerController* Controller =
					Cast<AAuraPlayerController>(AuraCharacter->GetController()))
				{
					Controller->ShowCoopMessage(TEXT("按 F 打开战利品宝箱"),
						FLinearColor(0.12f, 0.62f, 1.f), 2.f, TEXT("战利品宝箱已解锁"));
				}
			}
		}
	}
}

bool AAuraLootChest::ClaimForCharacter(AAuraCharacter* AuraCharacter)
{
	if (!HasAuthority() || !AuraCharacter || AuraCharacter->IsDead()) return false;
	constexpr float MaximumClaimDistance = 320.f;
	if (FVector::DistSquared(AuraCharacter->GetActorLocation(), GetActorLocation())
		> FMath::Square(MaximumClaimDistance))
	{
		UE_LOG(LogAuraLootChest, Warning,
			TEXT("Loot chest claim rejected by distance. Chest=%s Character=%s Distance=%.1f"),
			*GetName(), *GetNameSafe(AuraCharacter),
			FVector::Dist(AuraCharacter->GetActorLocation(), GetActorLocation()));
		return false;
	}
	if (bGuardLocked)
	{
		if (AAuraPlayerController* Controller =
			Cast<AAuraPlayerController>(AuraCharacter->GetController()))
		{
			Controller->ShowCoopMessage(
				RemainingGuardCount > 0
					? FString::Printf(TEXT("宝箱封印中 · 剩余 %d 名守卫"), RemainingGuardCount)
					: TEXT("宝箱尚未解锁"),
				FLinearColor(1.f, 0.18f, 0.08f), 1.6f);
		}
		return false;
	}
	AAuraPlayerState* AuraPlayerState = AuraCharacter->GetPlayerState<AAuraPlayerState>();
	if (!AuraPlayerState || HasBeenClaimedBy(AuraPlayerState)) return false;
	const int32 MetalReward = AAuraPlayerState::GetChestMetalReward(GetLootChestId());
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
		*AuraPlayerState, LootProfile,
		GetActorLocation() + GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 40.f),
		EAuraItemSourceType::Treasure, GetLootChestId());
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
	if (!AuraPlayerState->GrantChestMetal(MetalReward, GetLootChestId())) return false;

	AuraPlayerState->MarkTreasureClaimed(GetLootChestId());
	if (GetNetMode() == NM_Standalone)
	{
		if (UAuraRunSubsystem* RunSubsystem =
			GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			RunSubsystem->MarkTreasureClaimed(GetLootChestId());
		}
	}
	bOpened = true;
	RefreshChestState(false);
	ForceNetUpdate();
	OnLootChestClaimed.Broadcast(this, AuraPlayerState);
	UE_LOG(LogAuraLootChest, Display,
		TEXT("Loot chest claimed. Name=%s Id=%s Player=%s Spawned=%d"),
		*GetName(), *GetLootChestId().ToString(), *GetNameSafe(AuraPlayerState), Spawned);
	return true;
}

void AAuraLootChest::OnRewardTriggerOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority()) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(OtherActor);
	if (!AuraCharacter) return;
	if (bGuardLocked)
	{
		if (AAuraPlayerController* Controller =
			Cast<AAuraPlayerController>(AuraCharacter->GetController()))
		{
			Controller->ShowCoopMessage(
				RemainingGuardCount > 0
					? FString::Printf(TEXT("宝箱封印中 · 剩余 %d 名守卫"), RemainingGuardCount)
					: TEXT("宝箱尚未解锁"),
				FLinearColor(1.f, 0.18f, 0.08f), 1.6f);
		}
		return;
	}
	if (HasBeenClaimedBy(AuraCharacter->GetPlayerState())) return;
	if (AAuraPlayerController* Controller =
		Cast<AAuraPlayerController>(AuraCharacter->GetController()))
	{
		Controller->ShowCoopMessage(TEXT("按 F 打开战利品宝箱"),
			FLinearColor(0.12f, 0.62f, 1.f), 1.8f, TEXT("战利品宝箱已解锁"));
	}
}

void AAuraLootChest::OnRep_ChestState()
{
	RefreshChestState(false);
}

void AAuraLootChest::RefreshGeometry()
{
	if (!ChestBase || !ChestLid || !LidPivot || !ChestBandLeft
		|| !ChestBandRight || !ChestLock || !LootBeacon)
	{
		return;
	}
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
	LootBeacon->SetVisibility(false, true);
}

void AAuraLootChest::RefreshChestState(const bool bSnapAnimation)
{
	if (bSnapAnimation) CurrentOpenAlpha = bOpened ? 1.f : 0.f;
	if (LidPivot)
	{
		LidPivot->SetRelativeRotation(FRotator(0.f, 0.f, 105.f * CurrentOpenAlpha));
	}
	if (RewardTrigger) RewardTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	if (LootBeacon) LootBeacon->SetVisibility(false, true);
	if (RewardLight)
	{
		RewardLight->SetLightColor(bGuardLocked
			? FLinearColor(1.f, 0.035f, 0.015f) : FLinearColor(0.12f, 0.62f, 1.f));
		RewardLight->SetIntensity(FMath::Lerp(
			bGuardLocked ? 380.f : 620.f, 110.f, CurrentOpenAlpha));
	}
	if (RewardLabel)
	{
		const FString Label = bOpened
			? (GetNetMode() == NM_Standalone ? TEXT("宝箱已开启") : TEXT("队伍战利品宝箱"))
			: (bGuardLocked
				? (RemainingGuardCount > 0
					? FString::Printf(TEXT("封印宝箱 · 剩余 %d 名守卫"), RemainingGuardCount)
					: TEXT("宝箱尚未解锁"))
				: TEXT("[F] 打开战利品宝箱"));
		RewardLabel->SetText(FText::FromString(Label));
		RewardLabel->SetTextRenderColor(bOpened ? FColor(135, 135, 135)
			: (bGuardLocked ? FColor(255, 55, 35) : FColor(70, 190, 255)));
	}
}
