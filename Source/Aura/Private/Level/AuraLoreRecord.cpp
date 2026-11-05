// Copyright yosep

#include "Level/AuraLoreRecord.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "UObject/ConstructorHelpers.h"

AAuraLoreRecord::AAuraLoreRecord()
{
	PrimaryActorTick.bCanEverTick = false;
	// Map actors are present on every client. Reading a clue is cosmetic and local.
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerMesh"));
	MarkerMesh->SetupAttachment(SceneRoot);
	MarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerMesh->SetRelativeScale3D(FVector(0.55f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerAsset(
		TEXT("/Game/Assets/Dungeon/Beacon/SM_Beacon.SM_Beacon"));
	if (MarkerAsset.Succeeded()) MarkerMesh->SetStaticMesh(MarkerAsset.Object);

	InteractionLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InteractionLabel"));
	AuraWorldTextFont::Apply(InteractionLabel);
	InteractionLabel->SetupAttachment(SceneRoot);
	InteractionLabel->SetRelativeLocation(FVector(0.f, 0.f, 185.f));
	InteractionLabel->SetHorizontalAlignment(EHTA_Center);
	InteractionLabel->SetWorldSize(25.f);
	InteractionLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MarkerLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MarkerLight"));
	MarkerLight->SetupAttachment(SceneRoot);
	MarkerLight->SetRelativeLocation(FVector(0.f, 0.f, 95.f));
	MarkerLight->SetAttenuationRadius(480.f);
	MarkerLight->SetIntensity(1000.f);
	MarkerLight->SetCastShadows(false);
}

void AAuraLoreRecord::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshVisuals();
}

void AAuraLoreRecord::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
}

bool AAuraLoreRecord::IsInRange(const FVector& PlayerLocation) const
{
	// Full 3D distance matters: the dungeon now stacks three floors.
	return FVector::DistSquared(PlayerLocation, GetActorLocation())
		<= FMath::Square(InteractionRadius);
}

void AAuraLoreRecord::RefreshVisuals()
{
	if (InteractionLabel)
	{
		InteractionLabel->SetText(FText::FromString(
			FString::Printf(TEXT("[F] 调查 · %s"), *RecordTitle)));
		InteractionLabel->SetTextRenderColor(AccentColor.ToFColor(true));
	}
	if (MarkerLight) MarkerLight->SetLightColor(AccentColor);
}
