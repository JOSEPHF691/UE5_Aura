// Copyright yosep

#include "Pickup/AuraPickup.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/StaticMesh.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerController.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraPickup, Log, All);

AAuraPickup::AAuraPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(SceneRoot);
	PickupSphere->SetRelativeLocation(FVector(0.f, 0.f, 58.f));
	PickupSphere->InitSphereRadius(82.f);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupSphere->SetCollisionObjectType(ECC_WorldDynamic);
	PickupSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupSphere->SetGenerateOverlapEvents(true);
	PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AAuraPickup::OnPickupSphereOverlap);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(VisualRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickupLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PickupLight"));
	PickupLight->SetupAttachment(VisualRoot);
	PickupLight->SetRelativeLocation(FVector(0.f, 0.f, 18.f));
	PickupLight->SetIntensity(900.f);
	PickupLight->SetAttenuationRadius(240.f);
	PickupLight->SetCastShadows(false);

	PickupLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PickupLabel"));
	AuraWorldTextFont::Apply(PickupLabel);
	PickupLabel->SetupAttachment(SceneRoot);
	PickupLabel->SetRelativeLocation(FVector(0.f, 0.f, 142.f));
	PickupLabel->SetHorizontalAlignment(EHTA_Center);
	PickupLabel->SetVerticalAlignment(EVRTA_TextCenter);
	PickupLabel->SetWorldSize(24.f);
	PickupLabel->SetTranslucentSortPriority(10);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> HealthMeshAsset(
		TEXT("/Game/Assets/Pickups/Potions/SM_PotionBottle.SM_PotionBottle"));
	if (HealthMeshAsset.Succeeded()) HealthMesh = HealthMeshAsset.Object;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ManaMeshAsset(
		TEXT("/Game/Assets/Pickups/Potions/SM_PotionBottle.SM_PotionBottle"));
	if (ManaMeshAsset.Succeeded()) ManaMesh = ManaMeshAsset.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedLiquidAsset(
		TEXT("/Game/Assets/Pickups/Potions/Liquid/MI_RedLiquid.MI_RedLiquid"));
	if (RedLiquidAsset.Succeeded()) RedLiquidMaterial = RedLiquidAsset.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueLiquidAsset(
		TEXT("/Game/Assets/Pickups/Potions/Liquid/MI_BlueLiquid.MI_BlueLiquid"));
	if (BlueLiquidAsset.Succeeded()) BlueLiquidMaterial = BlueLiquidAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> ConsumeSoundAsset(
		TEXT("/Game/Assets/Sounds/Potions/Consume/sfx_Potion_Consume.sfx_Potion_Consume"));
	if (ConsumeSoundAsset.Succeeded()) ConsumeSound = ConsumeSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> RespawnSoundAsset(
		TEXT("/Game/Assets/Sounds/Potions/Spawn/sfx_Potion_Spawn.sfx_Potion_Spawn"));
	if (RespawnSoundAsset.Succeeded()) RespawnSound = RespawnSoundAsset.Object;
}

void AAuraPickup::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
	ApplyAvailabilityState();
	UE_LOG(LogAuraPickup, Display, TEXT("Pickup ready. Name=%s Type=%s Restore=%.1f AutoRespawn=%s RespawnDelay=%.1fs"),
		*GetName(), PickupType == EAuraPickupType::Health ? TEXT("Health") : TEXT("Mana"),
		GetRestoreAmount(), bRespawnsAutomatically ? TEXT("true") : TEXT("false"), GetRespawnDelay());
}

void AAuraPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshVisuals();
}

void AAuraPickup::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAvailable || !VisualRoot) return;

	AnimationTime += DeltaSeconds;
	VisualRoot->SetRelativeLocation(FVector(0.f, 0.f,
		VisualBaseHeight + FMath::Sin(AnimationTime * 2.4f) * 8.f));
	VisualRoot->AddLocalRotation(FRotator(0.f, 48.f * DeltaSeconds, 0.f));
	if (PickupLabel)
	{
		if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			PickupLabel->SetWorldRotation(
				(CameraManager->GetCameraLocation() - PickupLabel->GetComponentLocation()).Rotation());
		}
	}
}

void AAuraPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraPickup, PickupType);
	DOREPLIFETIME(AAuraPickup, bAvailable);
}

float AAuraPickup::GetRestoreAmount() const
{
	const FAuraWorldRecoveryBalance& Recovery = UAuraCombatBalanceData::Get().Recovery;
	return PickupType == EAuraPickupType::Health
		? Recovery.HealthPickupRestore : Recovery.ManaPickupRestore;
}

float AAuraPickup::GetRespawnDelay() const
{
	return UAuraCombatBalanceData::Get().Recovery.TrainingPickupRespawnDelay;
}

void AAuraPickup::OnPickupSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bAvailable) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(OtherActor);
	if (!AuraCharacter || AuraCharacter->IsDead()) return;

	UAbilitySystemComponent* AbilitySystem = AuraCharacter->GetAbilitySystemComponent();
	if (!AbilitySystem) return;

	const bool bHealthPickup = PickupType == EAuraPickupType::Health;
	const FGameplayAttribute Attribute = bHealthPickup
		? UAuraAttributeSet::GetHealthAttribute() : UAuraAttributeSet::GetManaAttribute();
	const FGameplayAttribute MaxAttribute = bHealthPickup
		? UAuraAttributeSet::GetMaxHealthAttribute() : UAuraAttributeSet::GetMaxManaAttribute();
	const float OldValue = AbilitySystem->GetNumericAttribute(Attribute);
	const float MaxValue = AbilitySystem->GetNumericAttribute(MaxAttribute);
	if (OldValue >= MaxValue - KINDA_SMALL_NUMBER) return;

	const TSubclassOf<UGameplayEffect> RestoreEffectClass = bHealthPickup
		? UAuraHealthPickupEffect::StaticClass() : UAuraManaPickupEffect::StaticClass();
	FGameplayEffectContextHandle EffectContext = AbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	FGameplayEffectSpecHandle RestoreSpec = AbilitySystem->MakeOutgoingSpec(
		RestoreEffectClass, 1.f, EffectContext);
	if (!RestoreSpec.IsValid()) return;
	RestoreSpec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Restore")), GetRestoreAmount());
	AbilitySystem->ApplyGameplayEffectSpecToSelf(*RestoreSpec.Data.Get());

	const float NewValue = AbilitySystem->GetNumericAttribute(Attribute);
	if (NewValue <= OldValue + KINDA_SMALL_NUMBER) return;
	if (bHealthPickup) AuraCharacter->MulticastHealthRestoreEffect();
	else AuraCharacter->MulticastManaRestoreEffect();

	SetAvailable(false);
	MulticastPlayPickupSound(false);
	if (bRespawnsAutomatically)
	{
		GetWorldTimerManager().SetTimer(
			RespawnTimer, this, &AAuraPickup::Respawn, GetRespawnDelay(), false);
	}

	if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(AuraCharacter->GetController()))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(AuraController->GetHUD()))
		{
			const FString RestoreMessage = bHealthPickup
				? FString::Printf(TEXT("生命 +%.0f"), NewValue - OldValue)
				: FString::Printf(TEXT("法力 +%.0f"), NewValue - OldValue);
			AuraHUD->ShowStatusMessage(
				RestoreMessage,
				bHealthPickup ? FLinearColor(0.2f, 1.f, 0.28f) : FLinearColor(0.2f, 0.55f, 1.f));
		}
	}

	UE_LOG(LogAuraPickup, Display,
		TEXT("Pickup consumed. Name=%s Type=%s Old=%.1f New=%.1f AutoRespawn=%s RespawnIn=%.1fs"),
		*GetName(), bHealthPickup ? TEXT("Health") : TEXT("Mana"), OldValue, NewValue,
		bRespawnsAutomatically ? TEXT("true") : TEXT("false"),
		bRespawnsAutomatically ? GetRespawnDelay() : 0.f);
}

void AAuraPickup::Respawn()
{
	ResetForCheckpoint();
}

void AAuraPickup::ResetForCheckpoint()
{
	if (!HasAuthority() || bAvailable) return;
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	SetAvailable(true);
	MulticastPlayPickupSound(true);
	UE_LOG(LogAuraPickup, Display, TEXT("Pickup reset. Name=%s Type=%s"),
		*GetName(), PickupType == EAuraPickupType::Health ? TEXT("Health") : TEXT("Mana"));
}

void AAuraPickup::OnRep_Available()
{
	ApplyAvailabilityState();
}

void AAuraPickup::OnRep_PickupType()
{
	RefreshVisuals();
}

void AAuraPickup::SetAvailable(const bool bNewAvailable)
{
	bAvailable = bNewAvailable;
	ApplyAvailabilityState();
	ForceNetUpdate();
}

void AAuraPickup::ApplyAvailabilityState()
{
	if (VisualRoot) VisualRoot->SetVisibility(bAvailable, true);
	if (PickupLabel) PickupLabel->SetVisibility(bAvailable);
	if (PickupSphere)
	{
		PickupSphere->SetCollisionEnabled(
			bAvailable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	SetActorTickEnabled(bAvailable);
}

void AAuraPickup::RefreshVisuals()
{
	if (!VisualMesh || !VisualRoot || !PickupLight || !PickupLabel) return;

	UStaticMesh* SelectedMesh = PickupType == EAuraPickupType::Health ? HealthMesh.Get() : ManaMesh.Get();
	VisualMesh->SetStaticMesh(SelectedMesh);
	VisualMesh->EmptyOverrideMaterials();
	UMaterialInterface* LiquidMaterial = PickupType == EAuraPickupType::Health
		? RedLiquidMaterial.Get() : BlueLiquidMaterial.Get();
	if (SelectedMesh && LiquidMaterial)
	{
		int32 LiquidMaterialIndex = INDEX_NONE;
		const TArray<FStaticMaterial>& Materials = SelectedMesh->GetStaticMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			const FStaticMaterial& Material = Materials[Index];
			const FString Descriptor = Material.MaterialSlotName.ToString()
				+ TEXT(" ") + GetNameSafe(Material.MaterialInterface);
			if (Descriptor.Contains(TEXT("Liquid"), ESearchCase::IgnoreCase))
			{
				LiquidMaterialIndex = Index;
				break;
			}
		}
		if (LiquidMaterialIndex == INDEX_NONE && !Materials.IsEmpty())
		{
			LiquidMaterialIndex = Materials.Num() - 1;
		}
		if (LiquidMaterialIndex != INDEX_NONE)
		{
			VisualMesh->SetMaterial(LiquidMaterialIndex, LiquidMaterial);
		}
	}

	float UniformScale = 1.f;
	if (SelectedMesh && SelectedMesh->GetBounds().BoxExtent.Z > 1.f)
	{
		UniformScale = 92.f / (SelectedMesh->GetBounds().BoxExtent.Z * 2.f);
	}
	VisualMesh->SetRelativeScale3D(FVector(UniformScale));
	VisualMesh->SetRelativeRotation(FRotator(0.f, 0.f, -6.f));
	VisualBaseHeight = 54.f;
	VisualRoot->SetRelativeLocation(FVector(0.f, 0.f, VisualBaseHeight));
	PickupLight->SetLightColor(PickupType == EAuraPickupType::Health
		? FLinearColor(1.f, 0.04f, 0.02f) : FLinearColor(0.02f, 0.2f, 1.f));
	const FString RestoreLabel = PickupType == EAuraPickupType::Health
		? FString::Printf(TEXT("生命 +%.0f"), GetRestoreAmount())
		: FString::Printf(TEXT("法力 +%.0f"), GetRestoreAmount());
	PickupLabel->SetText(FText::FromString(RestoreLabel));
	PickupLabel->SetTextRenderColor(PickupType == EAuraPickupType::Health
		? FColor(255, 72, 64) : FColor(72, 148, 255));
}

void AAuraPickup::MulticastPlayPickupSound_Implementation(const bool bRespawning)
{
	USoundBase* Sound = bRespawning ? RespawnSound.Get() : ConsumeSound.Get();
	if (Sound) UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
}
