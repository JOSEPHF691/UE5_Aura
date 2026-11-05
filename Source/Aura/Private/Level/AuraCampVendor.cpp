// Copyright yosep

#include "Level/AuraCampVendor.h"

#include "Animation/AnimSequence.h"
#include "Character/AuraCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"

AAuraCampVendor::AAuraCampVendor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	VendorMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VendorMesh"));
	VendorMesh->SetupAttachment(SceneRoot);
	VendorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VendorMesh->SetRelativeLocation(FVector(-50.f, 0.f, 0.f));
	VendorMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	WorkSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorkSurface"));
	WorkSurface->SetupAttachment(SceneRoot);
	WorkSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorkSurface->SetRelativeLocation(FVector(55.f, 0.f, 5.f));
	WorkSurface->SetRelativeScale3D(FVector(0.5f, 0.7f, 0.38f));
	DisplayProp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayProp"));
	DisplayProp->SetupAttachment(SceneRoot);
	DisplayProp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InteractionLabel"));
	AuraWorldTextFont::Apply(InteractionLabel);
	InteractionLabel->SetupAttachment(SceneRoot);
	InteractionLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	InteractionLabel->SetWorldSize(27.f);
	ServiceLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ServiceLight"));
	ServiceLight->SetupAttachment(SceneRoot);
	ServiceLight->SetRelativeLocation(FVector(15.f, 0.f, 165.f));
	ServiceLight->SetIntensity(1600.f);
	ServiceLight->SetAttenuationRadius(340.f);
}

void AAuraCampVendor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// A dedicated server needs only the service type, position and radius.
	// Text rendering may dereference a render/font resource in -nullrhi.
	if (GetNetMode() == NM_DedicatedServer) return;
	static const TCHAR* TablePath = TEXT("/Game/Assets/Dungeon/SM_Block_1x1x1.SM_Block_1x1x1");
	WorkSurface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TablePath));
	if (ServiceType == EAuraCampService::Merchant)
	{
		VendorMesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Assets/Enemies/Goblin/SKM_Goblin.SKM_Goblin")));
		VendorMesh->SetRelativeScale3D(FVector(1.3f));
		if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr,
			TEXT("/Game/Assets/Enemies/Goblin/Animations/Spear/Idle_Spear2.Idle_Spear2")))
		{
			VendorMesh->PlayAnimation(Idle, true);
		}
		DisplayProp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
			TEXT("/Game/Assets/Pickups/Potions/SM_PotionBottle.SM_PotionBottle")));
		DisplayProp->SetRelativeLocation(FVector(55.f, 0.f, 80.f));
		DisplayProp->SetRelativeScale3D(FVector(0.7f));
		InteractionLabel->SetText(FText::FromString(TEXT("商人  [F]")));
		InteractionLabel->SetRelativeLocation(FVector(0.f, 0.f, 195.f));
		InteractionLabel->SetTextRenderColor(FColor(255, 200, 90));
		ServiceLight->SetLightColor(FLinearColor(1.f, 0.47f, 0.1f));
	}
	else
	{
		VendorMesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Assets/Enemies/Shaman/SKM_Shaman.SKM_Shaman")));
		VendorMesh->SetRelativeScale3D(FVector(1.15f));
		if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr,
			TEXT("/Game/Assets/Enemies/Shaman/Animations/Shaman_Idle.Shaman_Idle")))
		{
			VendorMesh->PlayAnimation(Idle, true);
		}
		DisplayProp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
			TEXT("/Game/Assets/Characters/Aura/Staff/SM_Staff.SM_Staff")));
		DisplayProp->SetRelativeLocation(FVector(55.f, 0.f, 82.f));
		DisplayProp->SetRelativeRotation(FRotator(0.f, 0.f, 70.f));
		DisplayProp->SetRelativeScale3D(FVector(0.65f));
		InteractionLabel->SetText(FText::FromString(TEXT("锻造大师  [F]")));
		InteractionLabel->SetRelativeLocation(FVector(0.f, 0.f, 225.f));
		InteractionLabel->SetTextRenderColor(FColor(255, 125, 75));
		ServiceLight->SetLightColor(FLinearColor(1.f, 0.19f, 0.06f));
	}
}

bool AAuraCampVendor::CanServe(const AAuraCharacter* Character,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(Character) || Character->IsDead())
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}
	if (FVector::DistSquared2D(GetActorLocation(), Character->GetActorLocation())
		> FMath::Square(InteractionRadius)
		|| FMath::Abs(GetActorLocation().Z - Character->GetActorLocation().Z) > 220.f)
	{
		OutFailureReason = TEXT("请靠近营地服务人员");
		return false;
	}
	if (Character->IsInCombat())
	{
		OutFailureReason = TEXT("战斗中无法使用营地服务");
		return false;
	}
	return true;
}
