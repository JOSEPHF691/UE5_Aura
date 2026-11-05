// Copyright yosep

#include "Level/AuraArchmage.h"

#include "Animation/AnimSequence.h"
#include "Character/AuraCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "UObject/ConstructorHelpers.h"

AAuraArchmage::AAuraArchmage()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	MageMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MageMesh"));
	MageMesh->SetupAttachment(SceneRoot);
	MageMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MageMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	MageMesh->SetRelativeScale3D(FVector(1.3f));
	ArcaneStaff = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArcaneStaff"));
	ArcaneStaff->SetupAttachment(SceneRoot);
	ArcaneStaff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArcaneStaff->SetRelativeLocation(FVector(0.f, -65.f, 45.f));
	ArcaneStaff->SetRelativeRotation(FRotator(0.f, 0.f, -18.f));
	InteractionLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InteractionLabel"));
	AuraWorldTextFont::Apply(InteractionLabel);
	InteractionLabel->SetupAttachment(SceneRoot);
	InteractionLabel->SetRelativeLocation(FVector(0.f, 0.f, 225.f));
	InteractionLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	InteractionLabel->SetText(FText::FromString(TEXT("大魔法师  [F]")));
	InteractionLabel->SetTextRenderColor(FColor(180, 125, 255));
	InteractionLabel->SetWorldSize(26.f);
	ArcaneLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ArcaneLight"));
	ArcaneLight->SetupAttachment(SceneRoot);
	ArcaneLight->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	ArcaneLight->SetLightColor(FLinearColor(0.36f, 0.15f, 1.f));
	ArcaneLight->SetIntensity(1800.f);
	ArcaneLight->SetAttenuationRadius(380.f);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(
		TEXT("/Game/Assets/Enemies/Shaman/SKM_Shaman.SKM_Shaman"));
	if (Mesh.Succeeded()) MageMesh->SetSkeletalMesh(Mesh.Object);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(
		TEXT("/Game/Assets/Enemies/Shaman/Animations/Shaman_Idle.Shaman_Idle"));
	if (Idle.Succeeded())
	{
		MageMesh->PlayAnimation(Idle.Object, true);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Staff(
		TEXT("/Game/Assets/Characters/Aura/Staff/SM_Staff.SM_Staff"));
	if (Staff.Succeeded()) ArcaneStaff->SetStaticMesh(Staff.Object);
}

bool AAuraArchmage::CanServe(const AAuraCharacter* Character,
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
		OutFailureReason = TEXT("请靠近大魔法师");
		return false;
	}
	if (Character->IsInCombat())
	{
		OutFailureReason = TEXT("战斗中无法使用大魔法师服务");
		return false;
	}
	return true;
}
