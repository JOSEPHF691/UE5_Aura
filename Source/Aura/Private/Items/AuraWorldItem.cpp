// Copyright yosep

#include "Items/AuraWorldItem.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/AuraCharacter.h"
#include "Components/BillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "Data/AuraItemDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerState.h"
#include "Player/AuraPlayerController.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraWorldItem, Log, All);

namespace
{
	constexpr float WeaponVisualLongestDimension = 100.f;
	constexpr float ArmorVisualLongestDimension = 60.f;
	constexpr float HeadVisualLongestDimension = 50.f;
	constexpr float ShoesVisualLongestDimension = 45.f;
	constexpr float DefaultEquipmentVisualLongestDimension = 50.f;
	constexpr float EquipmentIconLongestDimension = 144.f;
	constexpr float HighlightedEquipmentIconLongestDimension = 168.f;
	constexpr float EquipmentGroundMarkerScale = 1.65f;
	constexpr float HighlightedEquipmentGroundMarkerScale = 2.f;
	constexpr float EquipmentIconGroundClearance = 2.f;
	constexpr float EquipmentAuraBackdropDepth = 8.f;
	constexpr float EquipmentAuraRingInnerRadius = 0.24f;
	constexpr float EquipmentAuraRingInnerFeather = 0.06f;
	constexpr float EquipmentAuraRingOuterScale = 2.05f;

	FIntPoint GetStableTextureSize(const UTexture2D* Texture)
	{
		if (!Texture) return FIntPoint::ZeroValue;

		const FIntPoint ImportedSize = Texture->GetImportedSize();
		if (ImportedSize.X > 0 && ImportedSize.Y > 0)
		{
			return ImportedSize;
		}

		return FIntPoint(Texture->GetSizeX(), Texture->GetSizeY());
	}

	float GetTextureLongestDimension(const UTexture2D* Texture)
	{
		// Platform data can temporarily point at the editor default texture while
		// async compilation finishes. ImportedSize is stable during that window and
		// is also serialized for cooked builds.
		const FIntPoint TextureSize = GetStableTextureSize(Texture);
		return static_cast<float>(FMath::Max(TextureSize.X, TextureSize.Y));
	}

	float GetBillboardScaleForLongestDimension(
		const UTexture2D* Texture, const float TargetLongestDimension)
	{
		const float TextureLongestDimension = GetTextureLongestDimension(Texture);
		return TextureLongestDimension > 0.f
			? TargetLongestDimension / TextureLongestDimension
			: 0.05f;
	}

	float GetEquipmentVisualTargetLongestDimension(const UAuraItemDefinition* Definition)
	{
		if (!Definition) return DefaultEquipmentVisualLongestDimension;

		switch (Definition->EquipmentSlot)
		{
		case EAuraEquipmentSlot::Weapon:
			return WeaponVisualLongestDimension;
		case EAuraEquipmentSlot::Armor:
			return ArmorVisualLongestDimension;
		case EAuraEquipmentSlot::Head:
			return HeadVisualLongestDimension;
		case EAuraEquipmentSlot::Shoes:
			return ShoesVisualLongestDimension;
		default:
			return DefaultEquipmentVisualLongestDimension;
		}
	}
}

AAuraWorldItem::AAuraWorldItem()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(6000.f));

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(SceneRoot);
	PickupSphere->InitSphereRadius(88.f);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupSphere->SetCollisionObjectType(ECC_WorldDynamic);
	PickupSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupSphere->SetGenerateOverlapEvents(true);
	PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AAuraWorldItem::OnPickupSphereOverlap);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	VisualRoot->SetRelativeLocation(FVector::ZeroVector);

	GroundMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundMarker"));
	GroundMarker->SetupAttachment(SceneRoot);
	GroundMarker->SetRelativeLocation(FVector(0.f, 0.f, 1.5f));
	GroundMarker->SetRelativeScale3D(FVector(1.05f));
	GroundMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundMarker->SetCastShadow(false);
	GroundMarker->SetTranslucentSortPriority(10);
	GroundMarker->SetMobility(EComponentMobility::Movable);

	LootBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootBeam"));
	LootBeam->SetupAttachment(SceneRoot);
	LootBeam->SetRelativeLocation(FVector(0.f, 0.f, 67.5f));
	LootBeam->SetRelativeScale3D(FVector(0.065f, 0.065f, 1.35f));
	LootBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootBeam->SetCastShadow(false);
	LootBeam->SetReceivesDecals(false);
	LootBeam->SetTranslucentSortPriority(9);

	LootBeamCross = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootBeamCross"));
	LootBeamCross->SetupAttachment(SceneRoot);
	LootBeamCross->SetRelativeLocation(FVector(0.f, 0.f, 67.5f));
	LootBeamCross->SetRelativeScale3D(FVector(0.20f, 0.20f, 1.35f));
	LootBeamCross->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootBeamCross->SetCastShadow(false);
	LootBeamCross->SetReceivesDecals(false);
	LootBeamCross->SetTranslucentSortPriority(9);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(VisualRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ItemLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ItemLight"));
	ItemLight->SetupAttachment(VisualRoot);
	ItemLight->SetIntensity(520.f);
	ItemLight->SetAttenuationRadius(180.f);
	ItemLight->SetCastShadows(false);

	ItemLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ItemLabel"));
	AuraWorldTextFont::Apply(ItemLabel);
	ItemLabel->SetupAttachment(SceneRoot);
	ItemLabel->SetRelativeLocation(FVector(0.f, 0.f, 135.f));
	ItemLabel->SetHorizontalAlignment(EHTA_Center);
	ItemLabel->SetVerticalAlignment(EVRTA_TextCenter);
	ItemLabel->SetWorldSize(22.f);
	ItemLabel->SetTranslucentSortPriority(12);

	ItemIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("ItemIcon"));
	ItemIcon->SetupAttachment(SceneRoot);
	ItemIcon->SetRelativeLocation(FVector(0.f, 0.f, 155.f));
	ItemIcon->SetRelativeScale3D(FVector(0.22f));
	ItemIcon->SetTranslucentSortPriority(13);
	ItemIcon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Billboard components default to editor-only presentation. This icon is part
	// of the actual loot read, so explicitly opt it into runtime rendering.
	ItemIcon->SetHiddenInGame(false);
	ItemIcon->SetCastShadow(false);
	ItemIcon->bIsScreenSizeScaled = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> GroundMarkerMeshAsset(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (GroundMarkerMeshAsset.Succeeded())
	{
		GroundMarker->SetStaticMesh(GroundMarkerMeshAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> LootBeamMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (LootBeamMeshAsset.Succeeded())
	{
		LootBeam->SetStaticMesh(LootBeamMeshAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> LootBeamHaloMeshAsset(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (LootBeamHaloMeshAsset.Succeeded())
	{
		LootBeamCross->SetStaticMesh(LootBeamHaloMeshAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMarkerMaterialAsset(
		TEXT("/Game/Aura/Materials/M_LootMarkerGroundRingV3.M_LootMarkerGroundRingV3"));
	if (GroundMarkerMaterialAsset.Succeeded())
	{
		LootGroundMaterial = GroundMarkerMaterialAsset.Object;
		GroundMarker->SetMaterial(0, LootGroundMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LootBeamMaterialAsset(
		TEXT("/Game/Aura/Materials/M_LootMarkerBeamGradientV1.M_LootMarkerBeamGradientV1"));
	if (LootBeamMaterialAsset.Succeeded())
	{
		LootBeamMaterial = LootBeamMaterialAsset.Object;
		LootBeam->SetMaterial(0, LootBeamMaterial);
		LootBeamCross->SetMaterial(0, LootBeamMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackMeshAsset(
		TEXT("/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal.SM_HealthCrystal"));
	if (FallbackMeshAsset.Succeeded()) FallbackMesh = FallbackMeshAsset.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedLiquidAsset(
		TEXT("/Game/Assets/Pickups/Potions/Liquid/MI_RedLiquid.MI_RedLiquid"));
	if (RedLiquidAsset.Succeeded()) RedLiquidMaterial = RedLiquidAsset.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueLiquidAsset(
		TEXT("/Game/Assets/Pickups/Potions/Liquid/MI_BlueLiquid.MI_BlueLiquid"));
	if (BlueLiquidAsset.Succeeded()) BlueLiquidMaterial = BlueLiquidAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> PickupSoundAsset(
		TEXT("/Game/Assets/Sounds/Potions/Consume/sfx_Potion_Consume.sfx_Potion_Consume"));
	if (PickupSoundAsset.Succeeded()) PickupSound = PickupSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> SpawnSoundAsset(
		TEXT("/Game/Assets/Sounds/Potions/Spawn/sfx_Potion_Spawn.sfx_Potion_Spawn"));
	if (SpawnSoundAsset.Succeeded()) SpawnSound = SpawnSoundAsset.Object;
}

void AAuraWorldItem::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && !ItemInstance.IsValid() && PlacedDefinition)
	{
		ItemInstance.Definition = PlacedDefinition;
		ItemInstance.Quantity = FMath::Max(1, PlacedQuantity);
		ItemInstance.Rarity = PlacedRarity;
		ItemInstance.EnsureIdentity();
		ForceNetUpdate();
	}
	RefreshVisuals();
	if (ItemInstance.IsValid() && SpawnSound && GetNetMode() != NM_DedicatedServer
		&& ShouldShowForLocalPlayer())
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSound, GetActorLocation(), 0.7f);
	}
	if (PickupSphere)
	{
		PickupSphere->SetCollisionEnabled(
			ItemInstance.IsValid() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	UE_LOG(LogAuraWorldItem, Display,
		TEXT("World item ready. Actor=%s Item=%s Quantity=%d Rarity=%s Valid=%s"),
		*GetName(), ItemInstance.Definition ? *ItemInstance.Definition->ItemId.ToString() : TEXT("None"),
		ItemInstance.Quantity, AuraItems::GetRarityName(ItemInstance.Rarity),
		ItemInstance.IsValid() ? TEXT("true") : TEXT("false"));
}

void AAuraWorldItem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ItemInstance.IsValid() || !VisualRoot) return;
	AnimationTime += DeltaSeconds;
	VisualRoot->SetRelativeLocation(FVector(
		0.f, 0.f, VisualBaseHeight
			+ FMath::Sin(AnimationTime * 2.2f) * VisualBobAmplitude));
	VisualRoot->AddLocalRotation(FRotator(0.f, 42.f * DeltaSeconds, 0.f));
	if (LootBeam && LootBeamCross && LootBeam->IsVisible())
	{
		const float CoreRadius = bLocallyHighlighted ? 0.09f : 0.065f;
		const float HaloRadius = bLocallyHighlighted ? 0.27f : 0.20f;
		const float Pulse = 1.f + FMath::Sin(AnimationTime * 3.1f) * 0.08f;
		LootBeam->SetRelativeScale3D(FVector(CoreRadius * Pulse, CoreRadius * Pulse, 1.35f));
		LootBeamCross->SetRelativeScale3D(FVector(HaloRadius * Pulse, HaloRadius * Pulse, 1.35f));
	}
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		if (RequiresManualPickup() && GroundMarker && ItemIcon
			&& GroundMarker->IsVisible() && ItemIcon->IsVisible())
		{
			const FVector IconLocation = ItemIcon->GetComponentLocation();
			const FVector CameraForward =
				Camera->GetCameraRotation().Vector().GetSafeNormal();
			// UBillboardComponent is parallel to the camera plane, not aimed along the
			// per-item camera ray. Match that exact orientation and keep the aura on a
			// separate depth layer so the two planes cannot cross as the camera moves.
			GroundMarker->SetWorldLocation(
				IconLocation + CameraForward * EquipmentAuraBackdropDepth);
			GroundMarker->SetWorldRotation(FQuat::FindBetweenNormals(
				FVector::UpVector, -CameraForward));
		}
		if (ItemLabel)
		{
			ItemLabel->SetWorldRotation(
				(Camera->GetCameraLocation() - ItemLabel->GetComponentLocation()).Rotation());
		}
	}
}

void AAuraWorldItem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!ItemInstance.IsValid() && PlacedDefinition)
	{
		ItemInstance.Definition = PlacedDefinition;
		ItemInstance.Quantity = FMath::Max(1, PlacedQuantity);
		ItemInstance.Rarity = PlacedRarity;
	}
	RefreshVisuals();
}

void AAuraWorldItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraWorldItem, ItemInstance);
	DOREPLIFETIME(AAuraWorldItem, ReservedPlayer);
	DOREPLIFETIME(AAuraWorldItem, bExclusiveOwner);
}

void AAuraWorldItem::InitializeItem(
	const FAuraItemInstance& InItem, APlayerState* InReservedPlayer,
	const float ProtectionDuration, const bool bInExclusiveOwner,
	const float LifetimeSeconds)
{
	if (!HasAuthority()) return;
	ItemInstance = InItem;
	ItemInstance.EnsureIdentity();
	PlacedDefinition = ItemInstance.Definition;
	PlacedQuantity = ItemInstance.Quantity;
	PlacedRarity = ItemInstance.Rarity;
	ReservedPlayer = InReservedPlayer;
	bExclusiveOwner = bInExclusiveOwner;
	bOnlyRelevantToOwner = bExclusiveOwner;
	bAlwaysRelevant = false;
	if (bExclusiveOwner && ReservedPlayer)
	{
		SetOwner(ReservedPlayer->GetPlayerController());
	}
	ReservationEndTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.f, ProtectionDuration) : 0.f;
	if (LifetimeSeconds > 0.f) SetLifeSpan(FMath::Max(0.1f, LifetimeSeconds));
	ForceNetUpdate();
}

int32 AAuraWorldItem::TryPickupBy(AAuraCharacter* Character)
{
	if (!HasAuthority() || bPickupInProgress || !Character || Character->IsDead()
		|| !ItemInstance.IsValid())
	{
		return 0;
	}
	AAuraPlayerState* PlayerState = Character->GetPlayerState<AAuraPlayerState>();
	if (!PlayerState) return 0;
	if (!CanBePickedBy(PlayerState))
	{
		// Exclusive personal loot is hidden from every non-owner. A short message
		// is still useful for temporarily reserved player-dropped items.
		if (!bExclusiveOwner)
		{
			if (AAuraPlayerController* Controller =
				Cast<AAuraPlayerController>(Character->GetController()))
			{
				Controller->ShowCoopMessage(TEXT("该物品属于另一位玩家"),
					FLinearColor(1.f, 0.42f, 0.12f), 1.4f);
			}
		}
		return 0;
	}
	constexpr float MaximumPickupDistance = 240.f;
	if (FVector::DistSquared(Character->GetActorLocation(), GetActorLocation())
		> FMath::Square(MaximumPickupDistance))
	{
		UE_LOG(LogAuraWorldItem, Warning,
			TEXT("World item pickup rejected by distance. Actor=%s Player=%s Distance=%.1f Max=%.1f"),
			*GetName(), *GetNameSafe(PlayerState),
			FVector::Dist(Character->GetActorLocation(), GetActorLocation()),
			MaximumPickupDistance);
		return 0;
	}
	UAuraInventoryComponent* Inventory = PlayerState->GetInventoryComponent();
	if (!Inventory) return 0;

	bPickupInProgress = true;
	FString FailureReason;
	const int32 Added = Inventory->TryAddItem(ItemInstance, FailureReason);
	if (Added > 0)
	{
		ItemInstance.Quantity -= Added;
		MulticastPlayPickupSound();
		UE_LOG(LogAuraWorldItem, Display,
			TEXT("World item picked up. Actor=%s Player=%s Added=%d Remaining=%d"),
			*GetName(), *GetNameSafe(PlayerState), Added, ItemInstance.Quantity);
		if (ItemInstance.Quantity <= 0)
		{
			Destroy();
			return Added;
		}
		ForceNetUpdate();
		RefreshVisuals();
	}
	else if (!FailureReason.IsEmpty())
	{
		UE_LOG(LogAuraWorldItem, Display,
			TEXT("World item pickup rejected. Actor=%s Player=%s Reason=%s"),
			*GetName(), *GetNameSafe(PlayerState), *FailureReason);
		if (AAuraPlayerController* Controller =
			Cast<AAuraPlayerController>(Character->GetController()))
		{
			Controller->ShowCoopMessage(FailureReason.ToUpper(),
				FLinearColor(1.f, 0.32f, 0.12f), 1.8f);
		}
	}
	bPickupInProgress = false;
	return Added;
}

void AAuraWorldItem::OnPickupSphereOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	// Equipment is an explicit interaction: nearby-list click or the highlighted
	// F target. Consumables retain the fast walk-over pickup used by recovery drops.
	if (RequiresManualPickup()) return;
	TryPickupBy(Cast<AAuraCharacter>(OtherActor));
}

void AAuraWorldItem::OnRep_ItemState()
{
	RefreshVisuals();
}

void AAuraWorldItem::MulticastPlayPickupSound_Implementation()
{
	if (PickupSound) UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
}

bool AAuraWorldItem::CanBePickedBy(const APlayerState* PlayerState) const
{
	if (!ReservedPlayer || ReservedPlayer == PlayerState) return true;
	if (bExclusiveOwner) return false;
	return !GetWorld() || GetWorld()->GetTimeSeconds() >= ReservationEndTime;
}

bool AAuraWorldItem::IsVisibleToLocalPlayer() const
{
	return ShouldShowForLocalPlayer();
}

bool AAuraWorldItem::RequiresManualPickup() const
{
	const UAuraItemDefinition* Definition = ItemInstance.Definition
		? ItemInstance.Definition.Get() : PlacedDefinition.Get();
	return Definition && Definition->ItemType == EAuraItemType::Equipment;
}

float AAuraWorldItem::GetEquipmentVisualLongestDimension() const
{
	if (!VisualMesh || !VisualMesh->GetStaticMesh()) return 0.f;
	const FVector MeshSize = VisualMesh->GetStaticMesh()->GetBounds().BoxExtent * 2.f;
	return FMath::Max3(MeshSize.X, MeshSize.Y, MeshSize.Z)
		* VisualMesh->GetComponentScale().GetAbsMax();
}

float AAuraWorldItem::GetEquipmentVisualFloorOffset() const
{
	if (!VisualMesh) return TNumericLimits<float>::Max();
	return VisualMesh->Bounds.Origin.Z - VisualMesh->Bounds.BoxExtent.Z
		- GetActorLocation().Z;
}

bool AAuraWorldItem::HasCompactGroundedEquipmentVisual() const
{
	if (!RequiresManualPickup() || !VisualMesh || !VisualMesh->GetStaticMesh()) return false;
	const UAuraItemDefinition* Definition = ItemInstance.Definition
		? ItemInstance.Definition.Get() : PlacedDefinition.Get();
	const float LongestDimension = GetEquipmentVisualLongestDimension();
	const float TargetLongestDimension = GetEquipmentVisualTargetLongestDimension(Definition);
	const float FloorOffset = GetEquipmentVisualFloorOffset();
	const float IconLongestDimension = ItemIcon && ItemIcon->Sprite
		? GetTextureLongestDimension(ItemIcon->Sprite)
			* ItemIcon->GetComponentScale().GetAbsMax()
		: 0.f;
	const FIntPoint IconTextureSize = ItemIcon && ItemIcon->Sprite
		? GetStableTextureSize(ItemIcon->Sprite) : FIntPoint::ZeroValue;
	const float IconRenderedHeight = static_cast<float>(IconTextureSize.Y)
		* (ItemIcon ? ItemIcon->GetComponentScale().GetAbsMax() : 0.f);
	const float IconBottomOffset = ItemIcon
		? ItemIcon->GetRelativeLocation().Z - IconRenderedHeight * 0.5f
		: TNumericLimits<float>::Max();
	const FVector GroundMarkerSize = GroundMarker
		? GroundMarker->Bounds.BoxExtent * 2.f : FVector::ZeroVector;
	const float GroundMarkerLongestDimension = FMath::Max(
		GroundMarkerSize.X, GroundMarkerSize.Y);
	const bool bWorldIconCompact = ItemIcon && ItemIcon->IsVisible()
		&& IconLongestDimension > 1.f
		&& IconLongestDimension <= HighlightedEquipmentIconLongestDimension + 0.5f;
	const bool bGroundMarkerCoversIcon = GroundMarker && GroundMarker->IsVisible()
		&& GroundMarkerLongestDimension >= IconLongestDimension * 1.1f;
	const bool bGroundMarkerDepthSeparatedFromIcon = GroundMarker && ItemIcon
		&& FMath::IsNearlyEqual(FVector::Distance(
			GroundMarker->GetComponentLocation(), ItemIcon->GetComponentLocation()),
			EquipmentAuraBackdropDepth, 1.5f);
	const bool bIconGroundAligned = IconBottomOffset >= 0.f
		&& IconBottomOffset <= EquipmentIconGroundClearance + 0.5f;
	const bool bLootBeamHidden = LootBeam && LootBeamCross
		&& !LootBeam->IsVisible() && !LootBeamCross->IsVisible();
	const bool bUsesPlaceholderMesh = VisualMesh->GetStaticMesh() == FallbackMesh;
	const bool bUsesIconPresentation = ItemIcon && ItemIcon->Sprite;
	const bool bMeshVisibilityValid = bUsesIconPresentation
		? !VisualMesh->IsVisible()
		: (bUsesPlaceholderMesh ? !VisualMesh->IsVisible() : VisualMesh->IsVisible());
	return LongestDimension > 1.f && LongestDimension <= TargetLongestDimension + 0.5f
		&& FloorOffset >= -0.5f && FloorOffset <= 2.f
		&& bWorldIconCompact
		&& bGroundMarkerCoversIcon
		&& bGroundMarkerDepthSeparatedFromIcon
		&& bIconGroundAligned
		&& bLootBeamHidden
		&& bMeshVisibilityValid;
}

bool AAuraWorldItem::IsPickupEligibleFor(const APlayerState* PlayerState) const
{
	return ItemInstance.IsValid() && PlayerState && CanBePickedBy(PlayerState);
}

FVector AAuraWorldItem::FindGroundedSpawnLocation(
	UWorld* World, const FVector& DesiredLocation, const AActor* IgnoredActor)
{
	if (!World) return DesiredLocation;
	FHitResult GroundHit;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraWorldItemGrounding), false);
	if (IgnoredActor) QueryParams.AddIgnoredActor(IgnoredActor);
	const FVector TraceStart = DesiredLocation + FVector(0.f, 0.f, 500.f);
	const FVector TraceEnd = DesiredLocation - FVector(0.f, 0.f, 1500.f);
	if (World->LineTraceSingleByObjectType(
		GroundHit, TraceStart, TraceEnd, ObjectQuery, QueryParams))
	{
		return GroundHit.ImpactPoint + FVector(0.f, 0.f, 1.f);
	}
	return DesiredLocation;
}

void AAuraWorldItem::SetLocallyHighlighted(const bool bHighlighted)
{
	if (bLocallyHighlighted == bHighlighted) return;
	bLocallyHighlighted = bHighlighted;
	RefreshVisuals();
}

bool AAuraWorldItem::ShouldShowForLocalPlayer() const
{
	if (!bExclusiveOwner) return true;
	if (!ReservedPlayer || !GetWorld()) return false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (Controller && Controller->IsLocalController())
		{
			return Controller->PlayerState == ReservedPlayer;
		}
	}
	return false;
}

void AAuraWorldItem::RefreshVisuals()
{
	if (!VisualMesh || !GroundMarker || !LootBeam || !LootBeamCross
		|| !ItemLight || !ItemLabel || !ItemIcon) return;
	const bool bLocallyVisible = ShouldShowForLocalPlayer();
	VisualRoot->SetVisibility(bLocallyVisible, true);
	GroundMarker->SetVisibility(bLocallyVisible);
	// The drop presentation now uses only the ground aura. The former vertical
	// core and cone are retained as components for asset compatibility but never
	// rendered.
	LootBeam->SetVisibility(false);
	LootBeamCross->SetVisibility(false);
	UAuraItemDefinition* Definition = ItemInstance.Definition
		? ItemInstance.Definition.Get() : PlacedDefinition.Get();
	if (!Definition)
	{
		VisualMesh->SetStaticMesh(nullptr);
		GroundMarker->SetVisibility(false);
		LootBeam->SetVisibility(false);
		LootBeamCross->SetVisibility(false);
		ItemIcon->SetSprite(nullptr);
		ItemIcon->SetVisibility(false);
		ItemLabel->SetVisibility(false);
		return;
	}

	UTexture2D* IconTexture = Definition->Icon.LoadSynchronous();
	UStaticMesh* Mesh = Definition->WorldMesh.LoadSynchronous();
	if (!Mesh) Mesh = FallbackMesh;
	VisualMesh->SetStaticMesh(Mesh);
	const bool bEquipment = Definition->ItemType == EAuraItemType::Equipment;
	const bool bUsesPlaceholderEquipmentMesh = bEquipment && Mesh == FallbackMesh;
	// Equipment with an authored icon uses one consistent billboard presentation.
	// Drawing a genuine mesh (the staff) underneath the icon made the translucent
	// aura intersect that mesh differently at each camera angle.
	const bool bUsesEquipmentIconPresentation = bEquipment && IconTexture != nullptr;
	VisualMesh->SetVisibility(bLocallyVisible && !bUsesPlaceholderEquipmentMesh
		&& !bUsesEquipmentIconPresentation);
	VisualMesh->EmptyOverrideMaterials();
	if (Definition->ConsumableEffect == EAuraConsumableEffect::Health && RedLiquidMaterial)
	{
		VisualMesh->SetMaterial(FMath::Max(0, VisualMesh->GetNumMaterials() - 1), RedLiquidMaterial);
	}
	else if (Definition->ConsumableEffect == EAuraConsumableEffect::Mana && BlueLiquidMaterial)
	{
		VisualMesh->SetMaterial(FMath::Max(0, VisualMesh->GetNumMaterials() - 1), BlueLiquidMaterial);
	}
	const float AuthoredScaleMultiplier = FMath::Max(0.05f, Definition->WorldMeshScale);
	float Scale = AuthoredScaleMultiplier;
	if (Mesh)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FVector MeshSize = Bounds.BoxExtent * 2.f;
		if (bEquipment)
		{
			// Equipment meshes have very different authored axes. Normalize their
			// longest dimension with a larger target for narrow weapons, then apply
			// the Data Asset multiplier so future tuning remains effective.
			const float LongestDimension = FMath::Max3(MeshSize.X, MeshSize.Y, MeshSize.Z);
			if (LongestDimension > 1.f)
			{
				const float TargetLongestDimension =
					GetEquipmentVisualTargetLongestDimension(Definition);
				Scale = TargetLongestDimension * AuthoredScaleMultiplier
					/ LongestDimension;
			}
		}
		else if (MeshSize.Z > 1.f)
		{
			Scale *= 70.f / MeshSize.Z;
		}
		VisualMesh->SetRelativeLocation(FVector(
			-Bounds.Origin.X * Scale,
			-Bounds.Origin.Y * Scale,
			-(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale + 0.2f));
	}
	VisualMesh->SetRelativeScale3D(FVector(Scale));
	VisualBaseHeight = bEquipment ? 0.f : 36.f;
	VisualBobAmplitude = bEquipment ? 0.f : 4.f;
	ItemLabel->SetRelativeLocation(FVector(0.f, 0.f, bEquipment ? 66.f : 120.f));
	ItemIcon->SetRelativeLocation(FVector(0.f, 0.f, bEquipment ? 0.f : 140.f));

	const EAuraItemRarity Rarity = ItemInstance.IsValid()
		? ItemInstance.Rarity : PlacedRarity;
	const FLinearColor RarityColor = AuraItems::GetRarityColor(Rarity);
	const float RingStrength = bLocallyHighlighted ? 1.6f : 1.f;
	const FLinearColor RingColor(
		RarityColor.R * RingStrength,
		RarityColor.G * RingStrength,
		RarityColor.B * RingStrength, 1.f);
	const FLinearColor PaleEdgeColor = FLinearColor::LerpUsingHSV(
		RarityColor, FLinearColor::White, 0.70f);
	GroundMarker->SetVectorParameterValueOnMaterials(
		TEXT("MarkerColor"), FVector(RingColor.R, RingColor.G, RingColor.B));
	GroundMarker->SetVectorParameterValueOnMaterials(
		TEXT("EdgeColor"), FVector(PaleEdgeColor.R, PaleEdgeColor.G, PaleEdgeColor.B));
	GroundMarker->SetScalarParameterValueOnMaterials(
		TEXT("EmissiveStrength"), bLocallyHighlighted ? 17.f : 12.f);
	GroundMarker->SetScalarParameterValueOnMaterials(
		TEXT("Opacity"), bLocallyHighlighted ? 1.25f : 1.f);
	GroundMarker->SetScalarParameterValueOnMaterials(
		TEXT("InnerRadius"), EquipmentAuraRingInnerRadius);
	GroundMarker->SetScalarParameterValueOnMaterials(
		TEXT("InnerFeather"), EquipmentAuraRingInnerFeather);
	GroundMarker->SetScalarParameterValueOnMaterials(
		TEXT("OuterScale"), EquipmentAuraRingOuterScale);
	LootBeam->SetVectorParameterValueOnMaterials(
		TEXT("MarkerColor"), FVector(RarityColor.R, RarityColor.G, RarityColor.B));
	LootBeamCross->SetVectorParameterValueOnMaterials(
		TEXT("MarkerColor"), FVector(PaleEdgeColor.R, PaleEdgeColor.G, PaleEdgeColor.B));
	LootBeam->SetVectorParameterValueOnMaterials(
		TEXT("EdgeColor"), FVector(PaleEdgeColor.R, PaleEdgeColor.G, PaleEdgeColor.B));
	LootBeamCross->SetVectorParameterValueOnMaterials(
		TEXT("EdgeColor"), FVector::OneVector);
	LootBeam->SetScalarParameterValueOnMaterials(
		TEXT("EmissiveStrength"), bLocallyHighlighted ? 1.3f : 1.f);
	LootBeamCross->SetScalarParameterValueOnMaterials(
		TEXT("EmissiveStrength"), bLocallyHighlighted ? 0.6f : 0.4f);
	LootBeam->SetScalarParameterValueOnMaterials(
		TEXT("Opacity"), bLocallyHighlighted ? 0.9f : 0.75f);
	LootBeamCross->SetScalarParameterValueOnMaterials(
		TEXT("Opacity"), bLocallyHighlighted ? 0.32f : 0.22f);
	ItemLight->SetLightColor(RarityColor);
	// The screen-facing aura supplies the equipment glow. Keeping the point light
	// would leave a second bright patch on the floor below the centered halo.
	ItemLight->SetVisibility(bLocallyVisible && !bEquipment);
	const int32 Quantity = ItemInstance.IsValid()
		? ItemInstance.Quantity : FMath::Max(1, PlacedQuantity);
	ItemIcon->SetSprite(IconTexture);
	// Equipment source icons vary from roughly 1200px to 1500px. Normalize their
	// world-space size instead of applying the old fixed 0.22 scale, which made
	// large source textures cover several floor tiles.
	const bool bShowWorldIcon = bLocallyVisible && IconTexture != nullptr;
	ItemIcon->SetVisibility(bShowWorldIcon);
	ItemIcon->SetHiddenInGame(false);
	// Network relevancy and the explicit local visibility check already enforce
	// personal loot. Primitive owner-only flags can reject the owning pawn's view.
	ItemIcon->SetOnlyOwnerSee(false);
	GroundMarker->SetOnlyOwnerSee(false);
	LootBeam->SetOnlyOwnerSee(false);
	LootBeamCross->SetOnlyOwnerSee(false);
	VisualMesh->SetOnlyOwnerSee(false);
	ItemLabel->SetOnlyOwnerSee(false);
	const FString Label = FString::Printf(TEXT("%s%s\n%s%s"),
		*Definition->DisplayName.ToString(), Quantity > 1
			? *FString::Printf(TEXT(" x%d"), Quantity) : TEXT(""),
		AuraItems::GetRarityName(Rarity), bExclusiveOwner ? TEXT("  •  专属") : TEXT(""));
	ItemLabel->SetText(FText::FromString(Label));
	ItemLabel->SetTextRenderColor(RarityColor.ToFColor(true));
	// An icon is clearer than floating item-name text and remains readable in
	// every camera mode. Keep the label only as a fallback for legacy items.
	ItemLabel->SetVisibility(bLocallyVisible && IconTexture == nullptr);
	ItemLight->SetIntensity(360.f);
	ItemLight->SetAttenuationRadius(155.f);
	const float NormalIconScale = bEquipment
		? GetBillboardScaleForLongestDimension(IconTexture, EquipmentIconLongestDimension)
		: 0.22f;
	ItemIcon->SetRelativeScale3D(FVector(NormalIconScale));
	GroundMarker->SetRelativeScale3D(FVector(
		bEquipment ? EquipmentGroundMarkerScale : 0.95f));
	LootBeam->SetRelativeLocation(FVector(0.f, 0.f, 67.5f));
	LootBeamCross->SetRelativeLocation(FVector(0.f, 0.f, 67.5f));
	LootBeam->SetRelativeScale3D(FVector(0.065f, 0.065f, 1.35f));
	LootBeamCross->SetRelativeScale3D(FVector(0.20f, 0.20f, 1.35f));
	VisualMesh->SetRenderCustomDepth(false);
	if (bLocallyVisible && bLocallyHighlighted && RequiresManualPickup())
	{
		ItemLight->SetIntensity(1200.f);
		ItemLight->SetAttenuationRadius(250.f);
		const float HighlightedIconScale = bEquipment
			? GetBillboardScaleForLongestDimension(
				IconTexture, HighlightedEquipmentIconLongestDimension)
			: 0.29f;
		ItemIcon->SetRelativeScale3D(FVector(HighlightedIconScale));
		GroundMarker->SetRelativeScale3D(FVector(
			bEquipment ? HighlightedEquipmentGroundMarkerScale : 1.08f));
		VisualMesh->SetRenderCustomDepth(true);
		VisualMesh->SetCustomDepthStencilValue(252);
		ItemLabel->SetText(FText::FromString(FString::Printf(
			TEXT("[ F ]  拾取\n%s"), *Definition->DisplayName.ToString())));
		ItemLabel->SetVisibility(true);
	}
	if (bEquipment && IconTexture)
	{
		const FIntPoint TextureSize = GetStableTextureSize(IconTexture);
		const float RenderedIconHeight = static_cast<float>(TextureSize.Y)
			* ItemIcon->GetRelativeScale3D().GetAbsMax();
		ItemIcon->SetRelativeLocation(FVector(
			0.f, 0.f, RenderedIconHeight * 0.5f + EquipmentIconGroundClearance));
	}
	if (PickupSphere)
	{
		PickupSphere->SetCollisionEnabled(
			Definition ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
