// Copyright yosep

#include "Level/AuraDungeonGate.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraDungeonGate, Log, All);

AAuraDungeonGate::AAuraDungeonGate()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMesh"));
	GateMesh->SetupAttachment(SceneRoot);
	GateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(SceneRoot);
	Blocker->SetCollisionObjectType(ECC_WorldStatic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Block);
	Blocker->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> GateMeshAsset(
		TEXT("/Game/Assets/Dungeon/SM_Block4x4x1.SM_Block4x4x1"));
	if (GateMeshAsset.Succeeded())
	{
		GateMesh->SetStaticMesh(GateMeshAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FadeMaterialAsset(
		TEXT("/Game/Aura/Materials/M_DungeonGateFade.M_DungeonGateFade"));
	if (FadeMaterialAsset.Succeeded()) FadeMaterial = FadeMaterialAsset.Object;
}

void AAuraDungeonGate::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) bOpen = bStartsOpen;
	CurrentOpenAlpha = bOpen ? 1.f : 0.f;
	if (GetNetMode() != NM_DedicatedServer && FadeMaterial)
	{
		FadeMaterialInstance = GateMesh->CreateDynamicMaterialInstance(0, FadeMaterial);
	}
	if (!FadeMaterialInstance && GetNetMode() != NM_DedicatedServer)
	{
		UE_LOG(LogAuraDungeonGate, Warning,
			TEXT("Dungeon gate fade material unavailable. Name=%s"), *GetName());
	}
	RefreshGeometry(true);
	RefreshCollision();
	UE_LOG(LogAuraDungeonGate, Display, TEXT("Dungeon gate ready. Name=%s Open=%s"),
		*GetName(), bOpen ? TEXT("true") : TEXT("false"));
}

void AAuraDungeonGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CurrentOpenAlpha = bStartsOpen ? 1.f : 0.f;
	RefreshGeometry(true);
}

void AAuraDungeonGate::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float TargetAlpha = bOpen ? 1.f : 0.f;
	CurrentOpenAlpha = FMath::FInterpTo(
		CurrentOpenAlpha, TargetAlpha, DeltaSeconds, OpenInterpSpeed);
	if (FMath::IsNearlyEqual(CurrentOpenAlpha, TargetAlpha, 0.002f))
	{
		CurrentOpenAlpha = TargetAlpha;
	}
	RefreshGeometry(false);
}

void AAuraDungeonGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraDungeonGate, bOpen);
}

void AAuraDungeonGate::SetGateOpen(const bool bNewOpen, const bool bInstant)
{
	if (GetWorld() && GetWorld()->IsGameWorld() && !HasAuthority()) return;
	if (bOpen == bNewOpen && !bInstant) return;

	bOpen = bNewOpen;
	if (bInstant)
	{
		CurrentOpenAlpha = bOpen ? 1.f : 0.f;
	}
	RefreshCollision();
	RefreshGeometry(bInstant);
	ForceNetUpdate();
	UE_LOG(LogAuraDungeonGate, Display, TEXT("Dungeon gate state changed. Name=%s Open=%s"),
		*GetName(), bOpen ? TEXT("true") : TEXT("false"));
}

void AAuraDungeonGate::OnRep_Open()
{
	RefreshCollision();
}

void AAuraDungeonGate::SetCameraOccluded(const bool bOccluded)
{
	if (bCameraOccluded == bOccluded) return;
	bCameraOccluded = bOccluded;
	RefreshGeometry(false);
}

void AAuraDungeonGate::RefreshGeometry(const bool bSnapToState)
{
	if (!GateMesh || !Blocker) return;
	if (bSnapToState)
	{
		CurrentOpenAlpha = (GetWorld() && GetWorld()->IsGameWorld() ? bOpen : bStartsOpen) ? 1.f : 0.f;
	}

	GateMesh->SetRelativeScale3D(GateMeshScale);
	if (const UStaticMesh* Mesh = GateMesh->GetStaticMesh())
	{
		const FVector Origin = Mesh->GetBounds().Origin;
		ClosedVisualLocation = FVector(
			-Origin.X * GateMeshScale.X,
			-Origin.Y * GateMeshScale.Y,
			-Origin.Z * GateMeshScale.Z);
	}
	GateMesh->SetRelativeLocation(ClosedVisualLocation + OpenOffset * CurrentOpenAlpha);
	if (GetWorld() && GetWorld()->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
	{
		const float Opacity = FMath::Clamp(1.f - CurrentOpenAlpha, 0.f, 1.f)
			* (bCameraOccluded ? 0.5f : 1.f);
		if (FadeMaterialInstance)
		{
			FadeMaterialInstance->SetScalarParameterValue(TEXT("GateOpacity"), Opacity);
			GateMesh->SetVisibility(Opacity > 0.001f);
		}
		else GateMesh->SetVisibility(Opacity > 0.5f);
	}
	Blocker->SetBoxExtent(BlockerExtent);
}

void AAuraDungeonGate::RefreshCollision()
{
	if (!Blocker) return;
	Blocker->SetCollisionEnabled(
		bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}
